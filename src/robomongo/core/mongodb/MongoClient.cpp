#include "robomongo/core/mongodb/MongoClient.h"
#include "robomongo/core/domain/MongoIndex.h"

namespace Robomongo
{
    MongoClient::MongoClient(mongo::DBClientBase *dbclient) :
        _dbclient(dbclient) { }

    QStringList MongoClient::getCollectionNames(const QString &dbname) const
    {
        typedef std::list<std::string> cont_string_t;
        cont_string_t dbs = _dbclient->getCollectionNames(dbname.toStdString());

        QStringList stringList;
        for (cont_string_t::const_iterator i = dbs.begin(); i != dbs.end(); i++) {
            stringList.append(QString::fromStdString(*i));
        }

        stringList.sort();
        return stringList;
    }

    QStringList MongoClient::getDatabaseNames() const
    {
        typedef std::list<std::string> cont_string_t;
        cont_string_t dbs = _dbclient->getDatabaseNames();
        QStringList dbNames;
        for (cont_string_t::const_iterator i = dbs.begin(); i != dbs.end(); ++i)
        {
            dbNames.append(QString::fromStdString(*i));
        }
        dbNames.sort();
        return dbNames;
    }

    QList<MongoUser> MongoClient::getUsers(const QString &dbName)
    {
        MongoNamespace ns(dbName, "system.users");
        QList<MongoUser> users;

        std::auto_ptr<mongo::DBClientCursor> cursor(_dbclient->query(ns.toString().toStdString(), mongo::Query()));

        while (cursor->more()) {
            mongo::BSONObj bsonObj = cursor->next();
            MongoUser user(bsonObj);
            users.append(user);
        }

        return users;
    }

    void MongoClient::createUser(const QString &dbName, const MongoUser &user, bool overwrite)
    {
        MongoNamespace ns(dbName, "system.users");
        mongo::BSONObj obj = user.toBson();

        if (!overwrite) {
            _dbclient->insert(ns.toString().toStdString(), obj);
        } else {
            mongo::BSONElement id = obj.getField("_id");
            mongo::BSONObjBuilder builder;
            builder.append(id);
            mongo::BSONObj bsonQuery = builder.obj();
            mongo::Query query(bsonQuery);

            _dbclient->update(ns.toString().toStdString(), query, obj, true, false);
        }
    }

    void MongoClient::dropUser(const QString &dbName, const mongo::OID &id)
    {
        MongoNamespace ns(dbName, "system.users");

        mongo::BSONObjBuilder builder;
        builder.append("_id", id);
        mongo::BSONObj bsonQuery = builder.obj();
        mongo::Query query(bsonQuery);

        _dbclient->remove(ns.toString().toStdString(), query, true);
    }

    QList<MongoFunction> MongoClient::getFunctions(const QString &dbName)
    {
        MongoNamespace ns(dbName, "system.js");
        QList<MongoFunction> functions;

        std::auto_ptr<mongo::DBClientCursor> cursor(_dbclient->query(ns.toString().toStdString(), mongo::Query()));

        while (cursor->more()) {
            mongo::BSONObj bsonObj = cursor->next();

            try {
                MongoFunction user(bsonObj);
                functions.append(user);
            } catch (const std::exception &) {
            // skip invalid docs
            }
        }
        return functions;
    }

    QList<QString> MongoClient::getIndexes(const MongoCollectionInfo &collection) const
    {
        QList<QString> result;
        std::auto_ptr<mongo::DBClientCursor> cursor(_dbclient->getIndexes(collection.ns().toString().toStdString()));

        while (cursor->more()) {
            mongo::BSONObj bsonObj = cursor->next();
            mongo::BSONElement nameElement = bsonObj.getField("name");
            if (nameElement.eoo())
                continue;

            QString indexName(QString::fromUtf8(nameElement.String().data()));
            result.append(indexName);
        }

        return result;
    }

    void MongoClient::ensureIndex(const MongoCollectionInfo &collection, const QString &name, const QString &request, bool unique, bool backGround, bool dropDuplicates) const
    {
        // TODO: This function should work for creating and editing of indexes.
        // If index with "name" already exists - drop and create new.
        // If index with "name doesn't exist - simply create new.
        //
        // In this case we do not need MongoClient::renameIndexFromCollection(), because
        // we will use MongoClient::ensureIndex() even for name changing of Index.
        // But let's leave MongoClient::renameIndexFromCollection() for future references.

        mongo::BSONObj obj = mongo::fromjson(request.toUtf8());
        _dbclient->ensureIndex(collection.ns().toString().toStdString(), obj, unique, name.toStdString(), true, backGround);
    }

    void MongoClient::renameIndexFromCollection(const MongoCollectionInfo &collection, const QString &oldIndexName, const QString &newIndexName) const
    {
        // This is simply an example of how to perform modifications of
        // BSON objects. Because BSONObj is immutable, you need to create
        // copy of this object, using BSONObjBuilder and BSONObjIterator.
        //
        // But we need to do not just simple renaming of Index name, we
        // also should allow our users to fully modify Index
        // (i.e. change name, keys, unique flag, sparse flag etc.)
        //
        // This should be done using the same dialog as for "Add Index".

        MongoNamespace ns(collection.ns().databaseName(), "system.indexes");
        std::string systemIndexesNs = ns.toString().toStdString();
        std::string collectionNs = collection.ns().toString().toStdString();
        std::string oldIndexNameStd = oldIndexName.toStdString();
        std::string newIndexNameStd = newIndexName.toStdString();

        // Building this JSON: { "name" : "oldIndexName" }
        mongo::BSONObj query(mongo::BSONObjBuilder()
            .append("name", oldIndexNameStd)
            .obj());

        // Searching for index with "oldIndexName"
        // with this query: db.system.indexes.find({ name : "oldIndexName"}
        mongo::BSONObj indexBson = _dbclient->findOne(systemIndexesNs, mongo::Query(query));
        if (indexBson.isEmpty())
            return;

        // Here we are building copy of "indexBson" object and
        // changing "name" field's value from "oldIndexText" to "newIndexText":
        mongo::BSONObjBuilder builder;
        mongo::BSONObjIterator i(indexBson);
        while (i.more()) {
            mongo::BSONElement element = i.next();

            if (mongo::StringData(element.fieldName()).compare("name") == 0) {
                builder.append("name", newIndexNameStd);
                continue;
            }

            builder.append(element);
        }

        _dbclient->dropIndex(collectionNs, oldIndexNameStd);
        _dbclient->insert(systemIndexesNs, builder.obj());


        /*
        std::auto_ptr<mongo::DBClientCursor> cursor(_dbclient->getIndexes(collection.ns().toString().toStdString()));
        std::string deleteIndex = oldIndexText.toStdString();
        std::string newIndex = newIndexText.toStdString();
        if(newIndex!=deleteIndex){
            while (cursor->more()) {
                std::string indexString;
                mongo::BSONObj jsonIndex;
                mongo::BSONObj obj = cursor->next();
                if(getIndex(obj,indexString)&&deleteIndex==indexString&&getJsonIndexAndRename(obj,jsonIndex,deleteIndex,newIndex))
                { 
                    mongo::BSONElement name = obj.getField("name");
                    _dbclient->dropIndex(collection.ns().toString().toStdString(),name.valuestr());
                    _dbclient->ensureIndex(collection.ns().toString().toStdString(),jsonIndex);
                    break;
                }
            }
        }*/
    }

    bool MongoClient::deleteIndexFromCollection(const MongoCollectionInfo &collection, const QString &indexName)const
    {
        _dbclient->dropIndex(collection.ns().toString().toStdString(), indexName.toStdString());

        /*
        std::auto_ptr<mongo::DBClientCursor> cursor(_dbclient->getIndexes(collection.ns().toString().toStdString()));
        std::string deleteIndex = indexText.toStdString();
        while (cursor->more()) {
            std::string indexString;
            mongo::BSONObj obj = cursor->next();
            if(getIndex(obj,indexString)&&deleteIndex==indexString)
            {             
                std::string str = obj.toString();
                mongo::BSONElement name = obj.getField("name");
                _dbclient->dropIndex(collection.ns().toString().toStdString(),name.valuestr());
                break;
            }
        }
        return true;
        */
    }

    void MongoClient::createFunction(const QString &dbName, const MongoFunction &fun, const QString &existingFunctionName /* = QString() */)
    {
        MongoNamespace ns(dbName, "system.js");
        mongo::BSONObj obj = fun.toBson();

        if (existingFunctionName.isEmpty()) { // this is insert
            _dbclient->insert(ns.toString().toStdString(), obj);
        } else { // this is update

        QString name = fun.name();

        if (existingFunctionName == name) {
            mongo::BSONObjBuilder builder;
            builder.append("_id", name.toStdString());
            mongo::BSONObj bsonQuery = builder.obj();
            mongo::Query query(bsonQuery);

            _dbclient->update(ns.toString().toStdString(), query, obj, true, false);
        } else {
            _dbclient->insert(ns.toString().toStdString(), obj);
            std::string res = _dbclient->getLastError();

                // if no errors
                if (res.empty()) {
                    mongo::BSONObjBuilder builder;
                    builder.append("_id", existingFunctionName.toStdString());
                    mongo::BSONObj bsonQuery = builder.obj();
                    mongo::Query query(bsonQuery);
                    _dbclient->remove(ns.toString().toStdString(), query, true);
                }
            }
        }
    }

    void MongoClient::dropFunction(const QString &dbName, const QString &name)
    {
        MongoNamespace ns(dbName, "system.js");

        mongo::BSONObjBuilder builder;
        builder.append("_id", name.toStdString());
        mongo::BSONObj bsonQuery = builder.obj();
        mongo::Query query(bsonQuery);

        _dbclient->remove(ns.toString().toStdString(), query, true);
    }

    void MongoClient::createDatabase(const QString &dbName)
    {
        /*
        *  Here we are going to insert temp document to "<dbName>.temp" collection.
        *  This will create <dbName> database for us.
        *  Finally we are dropping just created temporary collection.
        */

        MongoNamespace ns(dbName, "temp");

        // If <dbName>.temp already exists, stop.
        if (_dbclient->exists(ns.toString().toStdString()))
            return;

        // Building { _id : "temp" } document
        mongo::BSONObjBuilder builder;
        builder.append("_id", "temp");
        mongo::BSONObj obj = builder.obj();

        // Insert this document
        _dbclient->insert(ns.toString().toStdString(), obj);

        // Drop temp collection
        _dbclient->dropCollection(ns.toString().toStdString());
    }

    void MongoClient::dropDatabase(const QString &dbName)
    {
        _dbclient->dropDatabase(dbName.toStdString());
    }

    void MongoClient::createCollection(const QString &dbName, const QString &collectionName)
    {
        MongoNamespace ns(dbName, collectionName);
        _dbclient->createCollection(ns.toString().toStdString());
    }

    void MongoClient::renameCollection(const QString &dbName, const QString &collectionName, const QString &newCollectionName)
    {
        MongoNamespace from(dbName, collectionName);
        MongoNamespace to(dbName, newCollectionName);

        // Building { renameCollection: <source-namespace>, to: <target-namespace> }
        mongo::BSONObjBuilder command; // { collStats: "db.collection", scale : 1 }
        command.append("renameCollection", from.toString().toStdString());
        command.append("to", to.toString().toStdString());

        mongo::BSONObj result;
        _dbclient->runCommand("admin", command.obj(), result); // this command should be run against "admin" db
    }

    void MongoClient::duplicateCollection(const QString &dbName, const QString &collectionName, const QString &newCollectionName)
    {
        MongoNamespace from(dbName, collectionName);
        MongoNamespace to(dbName, newCollectionName);

        std::auto_ptr<mongo::DBClientCursor> cursor(_dbclient->query(from.toString().toStdString(), mongo::Query()));
        while (cursor->more()) {
            mongo::BSONObj bsonObj = cursor->next();
            _dbclient->insert(to.toString().toStdString(), bsonObj);
        }
    }

    void MongoClient::dropCollection(const QString &dbName, const QString &collectionName)
    {
        MongoNamespace ns(dbName, collectionName);
        _dbclient->dropCollection(ns.toString().toStdString());
    }

    void MongoClient::insertDocument(const mongo::BSONObj &obj, const QString &db, const QString &collection)
    {
        MongoNamespace ns(db, collection);
        _dbclient->insert(ns.toString().toStdString(), obj);
    }

    void MongoClient::saveDocument(const mongo::BSONObj &obj, const QString &db, const QString &collection)
    {
        MongoNamespace ns(db, collection);

        mongo::BSONElement id = obj.getField("_id");
        mongo::BSONObjBuilder builder;
        builder.append(id);
        mongo::BSONObj bsonQuery = builder.obj();
        mongo::Query query(bsonQuery);

        _dbclient->update(ns.toString().toStdString(), query, obj, true, false);
        //_dbclient->save(ns.toString().toStdString(), obj);
    }

    void MongoClient::removeDocuments(const QString &db, const QString &collection, mongo::Query query, bool justOne /*= true*/)
    {
        MongoNamespace ns(db, collection);
        _dbclient->remove(ns.toString().toStdString(), query, justOne);
    }

    QList<MongoDocumentPtr> MongoClient::query(const MongoQueryInfo &info)
    {
        MongoNamespace ns(info.databaseName, info.collectionName);

        int limit = (info.limit == 0 || info.limit > 51) ? 50 : info.limit;

        QList<MongoDocumentPtr> docs;
        std::auto_ptr<mongo::DBClientCursor> cursor = _dbclient->query(
            ns.toString().toStdString(), info.query, limit, info.skip,
            info.fields.nFields() ? &info.fields : 0, info.options, info.batchSize);

        while (cursor->more()) {
            mongo::BSONObj bsonObj = cursor->next();
            MongoDocumentPtr doc(new MongoDocument(bsonObj.getOwned()));
            docs.append(doc);
        }

        return docs;
    }

    MongoCollectionInfo MongoClient::runCollStatsCommand(const QString &ns)
    {
        MongoNamespace mongons(ns);

        mongo::BSONObjBuilder command; // { collStats: "db.collection", scale : 1 }
        command.append("collStats", mongons.collectionName().toStdString());
        command.append("scale", 1);

        mongo::BSONObj result;
        _dbclient->runCommand(mongons.databaseName().toStdString(), command.obj(), result);

        MongoCollectionInfo info(result);
        return info;
    }

    QList<MongoCollectionInfo> MongoClient::runCollStatsCommand(const QStringList &namespaces)
    {
        QList<MongoCollectionInfo> infos;
        for(QStringList::const_iterator it=namespaces.begin();it!=namespaces.end();++it){
            MongoCollectionInfo info = runCollStatsCommand(*it);
            infos.append(info);
        }
        return infos;
    }

    void MongoClient::done()
    {
        // do nothing here, because we are not using ScopedDbConnection now
        //_scopedConnection->done();
    }
}
