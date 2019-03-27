/*
 * Copyright (c) 2009 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "hdb_locl.h"
#include "sqlite3.h"

#define MAX_RETRIES 10

typedef struct hdb_sqlite_db {
    double version;
    sqlite3 *db;
    char *db_file;

    sqlite3_stmt *get_version;
    sqlite3_stmt *fetch;
    sqlite3_stmt *get_ids;
    sqlite3_stmt *add_entry;
    sqlite3_stmt *add_principal;
    sqlite3_stmt *add_alias;
    sqlite3_stmt *delete_aliases;
    sqlite3_stmt *update_entry;
    sqlite3_stmt *remove;
    sqlite3_stmt *get_all_entries;

} hdb_sqlite_db;

/* This should be used to mark updates which make the code incompatible
 * with databases created with previous versions. Don't update it if
 * compatibility is not broken. */
#define HDBSQLITE_VERSION 0.1

#define _HDBSQLITE_STRINGIFY(x) #x
#define HDBSQLITE_STRINGIFY(x) _HDBSQLITE_STRINGIFY(x)

#define HDBSQLITE_CREATE_TABLES \
                 " BEGIN TRANSACTION;" \
                 " CREATE TABLE Version (number REAL);" \
                 " INSERT INTO Version (number)" \
                 " VALUES (" HDBSQLITE_STRINGIFY(HDBSQLITE_VERSION) ");" \
                 " CREATE TABLE Principal" \
                 "  (id INTEGER PRIMARY KEY," \
                 "   principal TEXT UNIQUE NOT NULL," \
                 "   canonical INTEGER," \
                 "   entry INTEGER);" \
                 " CREATE TABLE Entry" \
                 "  (id INTEGER PRIMARY KEY," \
                 "   data BLOB);" \
                 " COMMIT"
#define HDBSQLITE_CREATE_TRIGGERS \
                 " CREATE TRIGGER remove_principals AFTER DELETE ON Entry" \
                 " BEGIN" \
                 "  DELETE FROM Principal" \
                 "  WHERE entry = OLD.id;" \
                 " END"
#define HDBSQLITE_GET_VERSION \
                 " SELECT number FROM Version"
#define HDBSQLITE_FETCH \
                 " SELECT Entry.data FROM Principal, Entry" \
                 " WHERE Principal.principal = ? AND" \
                 "       Entry.id = Principal.entry"
#define HDBSQLITE_GET_IDS \
                 " SELECT id, entry FROM Principal" \
                 " WHERE principal = ?"
#define HDBSQLITE_ADD_ENTRY \
                 " INSERT INTO Entry (data) VALUES (?)"
#define HDBSQLITE_ADD_PRINCIPAL \
                 " INSERT INTO Principal (principal, entry, canonical)" \
                 " VALUES (?, last_insert_rowid(), 1)"
#define HDBSQLITE_ADD_ALIAS \
                 " INSERT INTO Principal (principal, entry, canonical)" \
                 " VALUES(?, ?, 0)"
#define HDBSQLITE_DELETE_ALIASES \
                 " DELETE FROM Principal" \
                 " WHERE entry = ? AND canonical = 0"
#define HDBSQLITE_UPDATE_ENTRY \
                 " UPDATE Entry SET data = ?" \
                 " WHERE id = ?"
#define HDBSQLITE_REMOVE \
                 " DELETE FROM ENTRY WHERE id = " \
                 "  (SELECT entry FROM Principal" \
                 "   WHERE principal = ?)"
#define HDBSQLITE_GET_ALL_ENTRIES \
                 " SELECT data FROM Entry"

/**
 * Wrapper around sqlite3_prepare_v2.
 *
 * @param context   The current krb5 context
 * @param statement Where to store the pointer to the statement
 *                  after preparing it
 * @param str       SQL code for the statement
 *
 * @return          0 if OK, an error code if not
 */
static krb5_error_code
hdb_sqlite_prepare_stmt(krb5_context context,
                        sqlite3 *db,
                        sqlite3_stmt **statement,
                        const char *str)
{
    int ret, tries = 0;

    ret = sqlite3_prepare_v2(db, str, -1, statement, NULL);
    while((tries++ < MAX_RETRIES) &&
	  ((ret == SQLITE_BUSY) ||
           (ret == SQLITE_IOERR_BLOCKED) ||
           (ret == SQLITE_LOCKED))) {
	krb5_warnx(context, "hdb-sqlite: prepare busy");
        sleep(1);
        ret = sqlite3_prepare_v2(db, str, -1, statement, NULL);
    }

    if (ret != SQLITE_OK) {
        krb5_set_error_message(context, EINVAL,
			       "Failed to prepare stmt %s: %s",
			       str, sqlite3_errmsg(db));
        return EINVAL;
    }

    return 0;
}

/**
 * A wrapper around sqlite3_exec.
 *
 * @param context    The current krb5 context
 * @param database   An open sqlite3 database handle
 * @param statement  SQL code to execute
 * @param error_code What to return if the statement fails
 *
 * @return           0 if OK, else error_code
 */
static krb5_error_code
hdb_sqlite_exec_stmt(krb5_context context,
                     sqlite3 *database,
                     const char *statement,
                     krb5_error_code error_code)
{
    int ret;

    ret = sqlite3_exec(database, statement, NULL, NULL, NULL);

    while(((ret == SQLITE_BUSY) ||
           (ret == SQLITE_IOERR_BLOCKED) ||
           (ret == SQLITE_LOCKED))) {
	krb5_warnx(context, "hdb-sqlite: exec busy: %d", (int)getpid());
        sleep(1);
        ret = sqlite3_exec(database, statement, NULL, NULL, NULL);
    }

    if (ret != SQLITE_OK && error_code) {
        krb5_set_error_message(context, error_code,
			       "Execute %s: %s", statement,
                              sqlite3_errmsg(database));
        return error_code;
    }

    return 0;
}

/**
 * Opens an sqlite3 database handle to a file, may create the
 * database file depending on flags.
 *
 * @param context The current krb5 context
 * @param db      Heimdal database handle
 * @param flags   Controls whether or not the file may be created,
 *                may be 0 or SQLITE_OPEN_CREATE
 */
static krb5_error_code
hdb_sqlite_open_database(krb5_context context, HDB *db, int flags)
{
    int ret;
    hdb_sqlite_db *hsdb = (hdb_sqlite_db*) db->hdb_db;

    ret = sqlite3_open_v2(hsdb->db_file, &hsdb->db,
                          SQLITE_OPEN_READWRITE | flags, NULL);

    if (ret) {
        if (hsdb->db) {
	    ret = ENOENT;
            krb5_set_error_message(context, ret,
                                  "Error opening sqlite database %s: %s",
                                  hsdb->db_file, sqlite3_errmsg(hsdb->db));
            sqlite3_close(hsdb->db);
            hsdb->db = NULL;
        } else
	    ret = krb5_enomem(context);
        return ret;
    }

    return 0;
}

static int
hdb_sqlite_step(krb5_context context, sqlite3 *db, sqlite3_stmt *stmt)
{
    int ret;

    ret = sqlite3_step(stmt);
    while(((ret == SQLITE_BUSY) ||
           (ret == SQLITE_IOERR_BLOCKED) ||
           (ret == SQLITE_LOCKED))) {
	krb5_warnx(context, "hdb-sqlite: step busy: %d", (int)getpid());
        sleep(1);
        ret = sqlite3_step(stmt);
    }
    return ret;
}

/**
 * Closes the database and frees memory allocated for statements.
 *
 * @param context The current krb5 context
 * @param db      Heimdal database handle
 */
static krb5_error_code
hdb_sqlite_close_database(krb5_context context, HDB *db)
{
    hdb_sqlite_db *hsdb = (hdb_sqlite_db *) db->hdb_db;

    sqlite3_finalize(hsdb->get_version);
    sqlite3_finalize(hsdb->fetch);
    sqlite3_finalize(hsdb->get_ids);
    sqlite3_finalize(hsdb->add_entry);
    sqlite3_finalize(hsdb->add_principal);
    sqlite3_finalize(hsdb->add_alias);
    sqlite3_finalize(hsdb->delete_aliases);
    sqlite3_finalize(hsdb->update_entry);
    sqlite3_finalize(hsdb->remove);
    sqlite3_finalize(hsdb->get_all_entries);

    sqlite3_close(hsdb->db);

    return 0;
}

/**
 * Opens an sqlite database file and prepares it for use.
 * If the file does not exist it will be created.
 *
 * @param context  The current krb5_context
 * @param db       The heimdal database handle
 * @param filename Where to store the database file
 *
 * @return         0 if everything worked, an error code if not
 */
static krb5_error_code
hdb_sqlite_make_database(krb5_context context, HDB *db, const char *filename)
{
    int ret;
    int created_file = 0;
    hdb_sqlite_db *hsdb = (hdb_sqlite_db *) db->hdb_db;

    hsdb->db_file = strdup(filename);
    if(hsdb->db_file == NULL)
        return ENOMEM;

    ret = hdb_sqlite_open_database(context, db, 0);
    if (ret) {
        ret = hdb_sqlite_open_database(context, db, SQLITE_OPEN_CREATE);
        if (ret) goto out;

        created_file = 1;

        ret = hdb_sqlite_exec_stmt(context, hsdb->db,
                                   HDBSQLITE_CREATE_TABLES,
                                   EINVAL);
        if (ret) goto out;

        ret = hdb_sqlite_exec_stmt(context, hsdb->db,
                                   HDBSQLITE_CREATE_TRIGGERS,
                                   EINVAL);
        if (ret) goto out;
    }

    ret = hdb_sqlite_prepare_stmt(context, hsdb->db,
                                  &hsdb->get_version,
                                  HDBSQLITE_GET_VERSION);
    if (ret) goto out;
    ret = hdb_sqlite_prepare_stmt(context, hsdb->db,
                                  &hsdb->fetch,
                                  HDBSQLITE_FETCH);
    if (ret) goto out;
    ret = hdb_sqlite_prepare_stmt(context, hsdb->db,
                                  &hsdb->get_ids,
                                  HDBSQLITE_GET_IDS);
    if (ret) goto out;
    ret = hdb_sqlite_prepare_stmt(context, hsdb->db,
                                  &hsdb->add_entry,
                                  HDBSQLITE_ADD_ENTRY);
    if (ret) goto out;
    ret = hdb_sqlite_prepare_stmt(context, hsdb->db,
                                  &hsdb->add_principal,
                                  HDBSQLITE_ADD_PRINCIPAL);
    if (ret) goto out;
    ret = hdb_sqlite_prepare_stmt(context, hsdb->db,
                                  &hsdb->add_alias,
                                  HDBSQLITE_ADD_ALIAS);
    if (ret) goto out;
    ret = hdb_sqlite_prepare_stmt(context, hsdb->db,
                                  &hsdb->delete_aliases,
                                  HDBSQLITE_DELETE_ALIASES);
    if (ret) goto out;
    ret = hdb_sqlite_prepare_stmt(context, hsdb->db,
                                  &hsdb->update_entry,
                                  HDBSQLITE_UPDATE_ENTRY);
    if (ret) goto out;
    ret = hdb_sqlite_prepare_stmt(context, hsdb->db,
                                  &hsdb->remove,
                                  HDBSQLITE_REMOVE);
    if (ret) goto out;
    ret = hdb_sqlite_prepare_stmt(context, hsdb->db,
                                  &hsdb->get_all_entries,
                                  HDBSQLITE_GET_ALL_ENTRIES);
    if (ret) goto out;

    ret = hdb_sqlite_step(context, hsdb->db, hsdb->get_version);
    if(ret == SQLITE_ROW) {
        hsdb->version = sqlite3_column_double(hsdb->get_version, 0);
    }
    sqlite3_reset(hsdb->get_version);
    ret = 0;

    if(hsdb->version != HDBSQLITE_VERSION) {
        ret = EINVAL;
        krb5_set_error_message(context, ret, "HDBSQLITE_VERSION mismatch");
    }

    if(ret) goto out;

    return 0;

 out:
    if (hsdb->db)
        sqlite3_close(hsdb->db);
    if (created_file)
        unlink(hsdb->db_file);

    return ret;
}

/**
 * Retrieves an entry by searching for the given
 * principal in the Principal database table, both
 * for canonical principals and aliases.
 *
 * @param context   The current krb5_context
 * @param db        Heimdal database handle
 * @param principal The principal whose entry to search for
 * @param flags     Currently only for HDB_F_DECRYPT
 * @param kvno	    kvno to fetch is HDB_F_KVNO_SPECIFIED use used
 *
 * @return          0 if everything worked, an error code if not
 */
static krb5_error_code
hdb_sqlite_fetch_kvno(krb5_context context, HDB *db, krb5_const_principal principal,
		      unsigned flags, krb5_kvno kvno, hdb_entry_ex *entry)
{
    int sqlite_error;
    krb5_error_code ret;
    char *principal_string;
    hdb_sqlite_db *hsdb = (hdb_sqlite_db*)(db->hdb_db);
    sqlite3_stmt *fetch = hsdb->fetch;
    krb5_data value;

    ret = krb5_unparse_name(context, principal, &principal_string);
    if (ret) {
        free(principal_string);
        return ret;
    }

    sqlite3_bind_text(fetch, 1, principal_string, -1, SQLITE_STATIC);

    sqlite_error = hdb_sqlite_step(context, hsdb->db, fetch);
    if (sqlite_error != SQLITE_ROW) {
        if(sqlite_error == SQLITE_DONE) {
            ret = HDB_ERR_NOENTRY;
            goto out;
        } else {
            ret = EINVAL;
            krb5_set_error_message(context, ret,
                                  "sqlite fetch failed: %d",
                                  sqlite_error);
            goto out;
        }
    }

    value.length = sqlite3_column_bytes(fetch, 0);
    value.data = (void *) sqlite3_column_blob(fetch, 0);

    ret = hdb_value2entry(context, &value, &entry->entry);
    if(ret)
        goto out;

    if (db->hdb_master_key_set && (flags & HDB_F_DECRYPT)) {
        ret = hdb_unseal_keys(context, db, &entry->entry);
        if(ret) {
           hdb_free_entry(context, entry);
           goto out;
        }
    }

    ret = 0;

out:

    sqlite3_clear_bindings(fetch);
    sqlite3_reset(fetch);

    free(principal_string);

    return ret;
}

/**
 * Convenience function to step a prepared statement with no
 * value once.
 *
 * @param context   The current krb5_context
 * @param statement A prepared sqlite3 statement
 *
 * @return        0 if everything worked, an error code if not
 */
static krb5_error_code
hdb_sqlite_step_once(krb5_context context, HDB *db, sqlite3_stmt *statement)
{
    int ret;
    hdb_sqlite_db *hsdb = (hdb_sqlite_db *) db->hdb_db;

    ret = hdb_sqlite_step(context, hsdb->db, statement);
    sqlite3_clear_bindings(statement);
    sqlite3_reset(statement);

    return ret;
}


/**
 * Stores an hdb_entry in the database. If flags contains HDB_F_REPLACE
 * a previous entry may be replaced.
 *
 * @param context The current krb5_context
 * @param db      Heimdal database handle
 * @param flags   May currently only contain HDB_F_REPLACE
 * @param entry   The data to store
 *
 * @return        0 if everything worked, an error code if not
 */
static krb5_error_code
hdb_sqlite_store(krb5_context context, HDB *db, unsigned flags,
                 hdb_entry_ex *entry)
{
    int ret;
    int i;
    sqlite_int64 entry_id;
    char *principal_string = NULL;
    char *alias_string;
    const HDB_Ext_Aliases *aliases;

    hdb_sqlite_db *hsdb = (hdb_sqlite_db *)(db->hdb_db);
    krb5_data value;
    sqlite3_stmt *get_ids = hsdb->get_ids;

    ret = hdb_sqlite_exec_stmt(context, hsdb->db,
                               "BEGIN IMMEDIATE TRANSACTION", EINVAL);
    if(ret != SQLITE_OK) {
	ret = EINVAL;
        krb5_set_error_message(context, ret,
			       "SQLite BEGIN TRANSACTION failed: %s",
			       sqlite3_errmsg(hsdb->db));
        goto rollback;
    }

    ret = krb5_unparse_name(context,
                            entry->entry.principal, &principal_string);
    if (ret) {
        goto rollback;
    }

    ret = hdb_seal_keys(context, db, &entry->entry);
    if(ret) {
        goto rollback;
    }

    ret = hdb_entry2value(context, &entry->entry, &value);
    if(ret) {
        goto rollback;
    }

    sqlite3_bind_text(get_ids, 1, principal_string, -1, SQLITE_STATIC);
    ret = hdb_sqlite_step(context, hsdb->db, get_ids);

    if(ret == SQLITE_DONE) { /* No such principal */

        sqlite3_bind_blob(hsdb->add_entry, 1,
                          value.data, value.length, SQLITE_STATIC);
        ret = hdb_sqlite_step(context, hsdb->db, hsdb->add_entry);
        sqlite3_clear_bindings(hsdb->add_entry);
        sqlite3_reset(hsdb->add_entry);
        if(ret != SQLITE_DONE)
            goto rollback;

        sqlite3_bind_text(hsdb->add_principal, 1,
                          principal_string, -1, SQLITE_STATIC);
        ret = hdb_sqlite_step(context, hsdb->db, hsdb->add_principal);
        sqlite3_clear_bindings(hsdb->add_principal);
        sqlite3_reset(hsdb->add_principal);
        if(ret != SQLITE_DONE)
            goto rollback;

        entry_id = sqlite3_column_int64(get_ids, 1);

    } else if(ret == SQLITE_ROW) { /* Found a principal */

        if(! (flags & HDB_F_REPLACE)) /* Not allowed to replace it */
            goto rollback;

        entry_id = sqlite3_column_int64(get_ids, 1);

        sqlite3_bind_int64(hsdb->delete_aliases, 1, entry_id);
        ret = hdb_sqlite_step_once(context, db, hsdb->delete_aliases);
        if(ret != SQLITE_DONE)
            goto rollback;

        sqlite3_bind_blob(hsdb->update_entry, 1,
                          value.data, value.length, SQLITE_STATIC);
        sqlite3_bind_int64(hsdb->update_entry, 2, entry_id);
        ret = hdb_sqlite_step_once(context, db, hsdb->update_entry);
        if(ret != SQLITE_DONE)
            goto rollback;

    } else {
	/* Error! */
        goto rollback;
    }

    ret = hdb_entry_get_aliases(&entry->entry, &aliases);
    if(ret || aliases == NULL)
        goto commit;

    for(i = 0; i < aliases->aliases.len; i++) {

        ret = krb5_unparse_name(context, &aliases->aliases.val[i],
				&alias_string);
        if (ret) {
            free(alias_string);
            goto rollback;
        }

        sqlite3_bind_text(hsdb->add_alias, 1, alias_string,
                          -1, SQLITE_STATIC);
        sqlite3_bind_int64(hsdb->add_alias, 2, entry_id);
        ret = hdb_sqlite_step_once(context, db, hsdb->add_alias);

        free(alias_string);

        if(ret != SQLITE_DONE)
            goto rollback;
    }

    ret = 0;

commit:

    free(principal_string);

    krb5_data_free(&value);

    sqlite3_clear_bindings(get_ids);
    sqlite3_reset(get_ids);

    ret = hdb_sqlite_exec_stmt(context, hsdb->db, "COMMIT", EINVAL);
    if(ret != SQLITE_OK)
	krb5_warnx(context, "hdb-sqlite: COMMIT problem: %d: %s",
		   ret, sqlite3_errmsg(hsdb->db));

    return ret;

rollback:

    krb5_warnx(context, "hdb-sqlite: store rollback problem: %d: %s",
	       ret, sqlite3_errmsg(hsdb->db));

    free(principal_string);

    ret = hdb_sqlite_exec_stmt(context, hsdb->db,
                               "ROLLBACK", EINVAL);
    return ret;
}

/**
 * This may be called often by other code, since the BDB backends
 * can not have several open connections. SQLite can handle
 * many processes with open handles to the database file
 * and closing/opening the handle is an expensive operation.
 * Hence, this function does nothing.
 *
 * @param context The current krb5 context
 * @param db      Heimdal database handle
 *
 * @return        Always returns 0
 */
static krb5_error_code
hdb_sqlite_close(krb5_context context, HDB *db)
{
    return 0;
}

/**
 * The opposite of hdb_sqlite_close. Since SQLite accepts
 * many open handles to the database file the handle does not
 * need to be closed, or reopened.
 *
 * @param context The current krb5 context
 * @param db      Heimdal database handle
 * @param flags
 * @param mode_t
 *
 * @return        Always returns 0
 */
static krb5_error_code
hdb_sqlite_open(krb5_context context, HDB *db, int flags, mode_t mode)
{
    return 0;
}

/**
 * Closes the databse and frees all resources.
 *
 * @param context The current krb5 context
 * @param db      Heimdal database handle
 *
 * @return        0 on success, an error code if not
 */
static krb5_error_code
hdb_sqlite_destroy(krb5_context context, HDB *db)
{
    int ret;
    hdb_sqlite_db *hsdb;

    ret = hdb_clear_master_key(context, db);

    hdb_sqlite_close_database(context, db);

    hsdb = (hdb_sqlite_db*)(db->hdb_db);

    free(hsdb->db_file);
    free(db->hdb_db);
    free(db);

    return ret;
}

/*
 * Not sure if this is needed.
 */
static krb5_error_code
hdb_sqlite_lock(krb5_context context, HDB *db, int operation)
{
    krb5_set_error_message(context, HDB_ERR_CANT_LOCK_DB,
			   "lock not implemented");
    return HDB_ERR_CANT_LOCK_DB;
}

/*
 * Not sure if this is needed.
 */
static krb5_error_code
hdb_sqlite_unlock(krb5_context context, HDB *db)
{
    krb5_set_error_message(context, HDB_ERR_CANT_LOCK_DB,
			  "unlock not implemented");
    return HDB_ERR_CANT_LOCK_DB;
}

/*
 * Should get the next entry, to allow iteration over all entries.
 */
static krb5_error_code
hdb_sqlite_nextkey(krb5_context context, HDB *db, unsigned flags,
                   hdb_entry_ex *entry)
{
    krb5_error_code ret = 0;
    int sqlite_error;
    krb5_data value;

    hdb_sqlite_db *hsdb = (hdb_sqlite_db *) db->hdb_db;

    sqlite_error = hdb_sqlite_step(context, hsdb->db, hsdb->get_all_entries);
    if(sqlite_error == SQLITE_ROW) {
	/* Found an entry */
        value.length = sqlite3_column_bytes(hsdb->get_all_entries, 0);
        value.data = (void *) sqlite3_column_blob(hsdb->get_all_entries, 0);
        memset(entry, 0, sizeof(*entry));
        ret = hdb_value2entry(context, &value, &entry->entry);
    }
    else if(sqlite_error == SQLITE_DONE) {
	/* No more entries */
        ret = HDB_ERR_NOENTRY;
        sqlite3_reset(hsdb->get_all_entries);
    }
    else {
	/* XXX SQLite error. Should be handled in some way. */
        ret = EINVAL;
    }

    return ret;
}

/*
 * Should get the first entry in the database.
 * What is flags used for?
 */
static krb5_error_code
hdb_sqlite_firstkey(krb5_context context, HDB *db, unsigned flags,
                    hdb_entry_ex *entry)
{
    hdb_sqlite_db *hsdb = (hdb_sqlite_db *) db->hdb_db;
    krb5_error_code ret;

    sqlite3_reset(hsdb->get_all_entries);

    ret = hdb_sqlite_nextkey(context, db, flags, entry);
    if(ret)
        return ret;

    return 0;
}

/*
 * Renames the database file.
 */
static krb5_error_code
hdb_sqlite_rename(krb5_context context, HDB *db, const char *new_name)
{
    hdb_sqlite_db *hsdb = (hdb_sqlite_db *) db->hdb_db;
    int ret;

    krb5_warnx(context, "hdb_sqlite_rename");

    if (strncasecmp(new_name, "sqlite:", 7) == 0)
	new_name += 7;

    hdb_sqlite_close_database(context, db);

    ret = rename(hsdb->db_file, new_name);
    free(hsdb->db_file);

    hdb_sqlite_make_database(context, db, new_name);

    return ret;
}

/*
 * Removes a principal, including aliases and associated entry.
 */
static krb5_error_code
hdb_sqlite_remove(krb5_context context, HDB *db,
                  krb5_const_principal principal)
{
    krb5_error_code ret;
    char *principal_string;
    hdb_sqlite_db *hsdb = (hdb_sqlite_db*)(db->hdb_db);
    sqlite3_stmt *remove = hsdb->remove;

    ret = krb5_unparse_name(context, principal, &principal_string);
    if (ret) {
        free(principal_string);
        return ret;
    }

    sqlite3_bind_text(remove, 1, principal_string, -1, SQLITE_STATIC);

    ret = hdb_sqlite_step(context, hsdb->db, remove);
    if (ret != SQLITE_DONE) {
	ret = EINVAL;
        krb5_set_error_message(context, ret,
                              "sqlite remove failed: %d",
                              ret);
    } else
        ret = 0;

    sqlite3_clear_bindings(remove);
    sqlite3_reset(remove);

    return ret;
}

/**
 * Create SQLITE object, and creates the on disk database if its doesn't exists.
 *
 * @param context A Kerberos 5 context.
 * @param db a returned database handle.
 * @param argument filename
 *
 * @return        0 on success, an error code if not
 */

krb5_error_code
hdb_sqlite_create(krb5_context context, HDB **db, const char *argument)
{
    krb5_error_code ret;
    hdb_sqlite_db *hsdb;

    *db = calloc(1, sizeof (**db));
    if (*db == NULL)
	return krb5_enomem(context);

    hsdb = (hdb_sqlite_db*) calloc(1, sizeof (*hsdb));
    if (hsdb == NULL) {
        free(*db);
        *db = NULL;
	return krb5_enomem(context);
    }

    (*db)->hdb_db = hsdb;

    /* XXX make_database should make sure everything else is freed on error */
    ret = hdb_sqlite_make_database(context, *db, argument);
    if (ret) {
        free((*db)->hdb_db);
        free(*db);

        return ret;
    }

    (*db)->hdb_master_key_set = 0;
    (*db)->hdb_openp = 0;
    (*db)->hdb_capability_flags = 0;

    (*db)->hdb_open = hdb_sqlite_open;
    (*db)->hdb_close = hdb_sqlite_close;

    (*db)->hdb_lock = hdb_sqlite_lock;
    (*db)->hdb_unlock = hdb_sqlite_unlock;
    (*db)->hdb_firstkey = hdb_sqlite_firstkey;
    (*db)->hdb_nextkey = hdb_sqlite_nextkey;
    (*db)->hdb_fetch_kvno = hdb_sqlite_fetch_kvno;
    (*db)->hdb_store = hdb_sqlite_store;
    (*db)->hdb_remove = hdb_sqlite_remove;
    (*db)->hdb_destroy = hdb_sqlite_destroy;
    (*db)->hdb_rename = hdb_sqlite_rename;
    (*db)->hdb__get = NULL;
    (*db)->hdb__put = NULL;
    (*db)->hdb__del = NULL;

    return 0;
}
