/*
** Copyright (c) 1999-2003, 2009 Proofpoint, Inc. and its suppliers.
**	All rights reserved.
**
** By using this file, you agree to the terms and conditions set
** forth in the LICENSE file which can be found at the top level of
** the sendmail distribution.
*/

#include <sm/gen.h>
SM_RCSID("@(#)$Id: smdb2.c,v 8.83 2013-11-22 20:51:49 ca Exp $")

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>


#include <sendmail/sendmail.h>
#include <libsmdb/smdb.h>

#if (DB_VERSION_MAJOR >= 2)

# define SMDB2_FILE_EXTENSION "db"

struct smdb_db2_database
{
	DB	*smdb2_db;
	int	smdb2_lock_fd;
};
typedef struct smdb_db2_database SMDB_DB2_DATABASE;

/*
**  SMDB_TYPE_TO_DB2_TYPE -- Translates smdb database type to db2 type.
**
**	Parameters:
**		type -- The type to translate.
**
**	Returns:
**		The DB2 type that corresponsds to the passed in SMDB type.
**		Returns -1 if there is no equivalent type.
**
*/

DBTYPE
smdb_type_to_db2_type(type)
	SMDB_DBTYPE type;
{
	if (type == SMDB_TYPE_DEFAULT)
		return DB_HASH;

	if (strncmp(type, SMDB_TYPE_HASH, SMDB_TYPE_HASH_LEN) == 0)
		return DB_HASH;

	if (strncmp(type, SMDB_TYPE_BTREE, SMDB_TYPE_BTREE_LEN) == 0)
		return DB_BTREE;

	return DB_UNKNOWN;
}
/*
**  DB2_ERROR_TO_SMDB -- Translates db2 errors to smdbe errors
**
**	Parameters:
**		error -- The error to translate.
**
**	Returns:
**		The SMDBE error corresponding to the db2 error.
**		If we don't have a corresponding error, it returs errno.
**
*/

int
db2_error_to_smdb(error)
	int error;
{
	int result;

	switch (error)
	{
# ifdef DB_INCOMPLETE
		case DB_INCOMPLETE:
			result = SMDBE_INCOMPLETE;
			break;
# endif /* DB_INCOMPLETE */

# ifdef DB_NOTFOUND
		case DB_NOTFOUND:
			result = SMDBE_NOT_FOUND;
			break;
# endif /* DB_NOTFOUND */

# ifdef DB_KEYEMPTY
		case DB_KEYEMPTY:
			result = SMDBE_KEY_EMPTY;
			break;
# endif /* DB_KEYEMPTY */

# ifdef DB_KEYEXIST
		case DB_KEYEXIST:
			result = SMDBE_KEY_EXIST;
			break;
# endif /* DB_KEYEXIST */

# ifdef DB_LOCK_DEADLOCK
		case DB_LOCK_DEADLOCK:
			result = SMDBE_LOCK_DEADLOCK;
			break;
# endif /* DB_LOCK_DEADLOCK */

# ifdef DB_LOCK_NOTGRANTED
		case DB_LOCK_NOTGRANTED:
			result = SMDBE_LOCK_NOT_GRANTED;
			break;
# endif /* DB_LOCK_NOTGRANTED */

# ifdef DB_LOCK_NOTHELD
		case DB_LOCK_NOTHELD:
			result = SMDBE_LOCK_NOT_HELD;
			break;
# endif /* DB_LOCK_NOTHELD */

# ifdef DB_RUNRECOVERY
		case DB_RUNRECOVERY:
			result = SMDBE_RUN_RECOVERY;
			break;
# endif /* DB_RUNRECOVERY */

# ifdef DB_OLD_VERSION
		case DB_OLD_VERSION:
			result = SMDBE_OLD_VERSION;
			break;
# endif /* DB_OLD_VERSION */

		case 0:
			result = SMDBE_OK;
			break;

		default:
			result = error;
	}
	return result;
}
/*
**  SMDB_PUT_FLAGS_TO_DB2_FLAGS -- Translates smdb put flags to db2 put flags.
**
**	Parameters:
**		flags -- The flags to translate.
**
**	Returns:
**		The db2 flags that are equivalent to the smdb flags.
**
**	Notes:
**		Any invalid flags are ignored.
**
*/

unsigned int
smdb_put_flags_to_db2_flags(flags)
	SMDB_FLAG flags;
{
	int return_flags;

	return_flags = 0;

	if (bitset(SMDBF_NO_OVERWRITE, flags))
		return_flags |= DB_NOOVERWRITE;

	return return_flags;
}
/*
**  SMDB_CURSOR_GET_FLAGS_TO_DB2 -- Translates smdb cursor get flags to db2
**	getflags.
**
**	Parameters:
**		flags -- The flags to translate.
**
**	Returns:
**		The db2 flags that are equivalent to the smdb flags.
**
**	Notes:
**		-1 is returned if flag is unknown.
**
*/

int
smdb_cursor_get_flags_to_db2(flags)
	SMDB_FLAG flags;
{
	switch (flags)
	{
		case SMDB_CURSOR_GET_FIRST:
			return DB_FIRST;

		case SMDB_CURSOR_GET_LAST:
			return DB_LAST;

		case SMDB_CURSOR_GET_NEXT:
			return DB_NEXT;

		case SMDB_CURSOR_GET_RANGE:
			return DB_SET_RANGE;

		default:
			return -1;
	}
}

/*
**  Except for smdb_db_open, the rest of these functions correspond to the
**  interface laid out in smdb.h.
*/

SMDB_DB2_DATABASE *
smdb2_malloc_database()
{
	SMDB_DB2_DATABASE *db2;

	db2 = (SMDB_DB2_DATABASE *) malloc(sizeof(SMDB_DB2_DATABASE));
	if (db2 != NULL)
		db2->smdb2_lock_fd = -1;

	return db2;
}

int
smdb2_close(database)
	SMDB_DATABASE *database;
{
	int result;
	SMDB_DB2_DATABASE *db2 = (SMDB_DB2_DATABASE *) database->smdb_impl;
	DB *db = ((SMDB_DB2_DATABASE *) database->smdb_impl)->smdb2_db;

	result = db2_error_to_smdb(db->close(db, 0));
	if (db2->smdb2_lock_fd != -1)
		close(db2->smdb2_lock_fd);

	free(db2);
	database->smdb_impl = NULL;

	return result;
}

int
smdb2_del(database, key, flags)
	SMDB_DATABASE *database;
	SMDB_DBENT *key;
	unsigned int flags;
{
	DB *db = ((SMDB_DB2_DATABASE *) database->smdb_impl)->smdb2_db;
	DBT dbkey;

	(void) memset(&dbkey, '\0', sizeof dbkey);
	dbkey.data = key->data;
	dbkey.size = key->size;
	return db2_error_to_smdb(db->del(db, NULL, &dbkey, flags));
}

int
smdb2_fd(database, fd)
	SMDB_DATABASE *database;
	int *fd;
{
	DB *db = ((SMDB_DB2_DATABASE *) database->smdb_impl)->smdb2_db;

	return db2_error_to_smdb(db->fd(db, fd));
}

int
smdb2_lockfd(database)
	SMDB_DATABASE *database;
{
	SMDB_DB2_DATABASE *db2 = (SMDB_DB2_DATABASE *) database->smdb_impl;

	return db2->smdb2_lock_fd;
}

int
smdb2_get(database, key, data, flags)
	SMDB_DATABASE *database;
	SMDB_DBENT *key;
	SMDB_DBENT *data;
	unsigned int flags;
{
	int result;
	DB *db = ((SMDB_DB2_DATABASE *) database->smdb_impl)->smdb2_db;
	DBT dbkey, dbdata;

	(void) memset(&dbdata, '\0', sizeof dbdata);
	(void) memset(&dbkey, '\0', sizeof dbkey);
	dbkey.data = key->data;
	dbkey.size = key->size;

	result = db->get(db, NULL, &dbkey, &dbdata, flags);
	data->data = dbdata.data;
	data->size = dbdata.size;
	return db2_error_to_smdb(result);
}

int
smdb2_put(database, key, data, flags)
	SMDB_DATABASE *database;
	SMDB_DBENT *key;
	SMDB_DBENT *data;
	unsigned int flags;
{
	DB *db = ((SMDB_DB2_DATABASE *) database->smdb_impl)->smdb2_db;
	DBT dbkey, dbdata;

	(void) memset(&dbdata, '\0', sizeof dbdata);
	(void) memset(&dbkey, '\0', sizeof dbkey);
	dbkey.data = key->data;
	dbkey.size = key->size;
	dbdata.data = data->data;
	dbdata.size = data->size;

	return db2_error_to_smdb(db->put(db, NULL, &dbkey, &dbdata,
					 smdb_put_flags_to_db2_flags(flags)));
}


int
smdb2_set_owner(database, uid, gid)
	SMDB_DATABASE *database;
	uid_t uid;
	gid_t gid;
{
# if HASFCHOWN
	int fd;
	int result;
	DB *db = ((SMDB_DB2_DATABASE *) database->smdb_impl)->smdb2_db;

	result = db->fd(db, &fd);
	if (result != 0)
		return result;

	result = fchown(fd, uid, gid);
	if (result < 0)
		return errno;
# endif /* HASFCHOWN */

	return SMDBE_OK;
}

int
smdb2_sync(database, flags)
	SMDB_DATABASE *database;
	unsigned int flags;
{
	DB *db = ((SMDB_DB2_DATABASE *) database->smdb_impl)->smdb2_db;

	return db2_error_to_smdb(db->sync(db, flags));
}

int
smdb2_cursor_close(cursor)
	SMDB_CURSOR *cursor;
{
	int ret;
	DBC *dbc = (DBC *) cursor->smdbc_impl;

	ret = db2_error_to_smdb(dbc->c_close(dbc));
	free(cursor);
	return ret;
}

int
smdb2_cursor_del(cursor, flags)
	SMDB_CURSOR *cursor;
	SMDB_FLAG flags;
{
	DBC *dbc = (DBC *) cursor->smdbc_impl;

	return db2_error_to_smdb(dbc->c_del(dbc, 0));
}

int
smdb2_cursor_get(cursor, key, value, flags)
	SMDB_CURSOR *cursor;
	SMDB_DBENT *key;
	SMDB_DBENT *value;
	SMDB_FLAG flags;
{
	int db2_flags;
	int result;
	DBC *dbc = (DBC *) cursor->smdbc_impl;
	DBT dbkey, dbdata;

	(void) memset(&dbdata, '\0', sizeof dbdata);
	(void) memset(&dbkey, '\0', sizeof dbkey);

	db2_flags = smdb_cursor_get_flags_to_db2(flags);
	result = dbc->c_get(dbc, &dbkey, &dbdata, db2_flags);
	if (result == DB_NOTFOUND)
		return SMDBE_LAST_ENTRY;
	key->data = dbkey.data;
	key->size = dbkey.size;
	value->data = dbdata.data;
	value->size = dbdata.size;
	return db2_error_to_smdb(result);
}

int
smdb2_cursor_put(cursor, key, value, flags)
	SMDB_CURSOR *cursor;
	SMDB_DBENT *key;
	SMDB_DBENT *value;
	SMDB_FLAG flags;
{
	DBC *dbc = (DBC *) cursor->smdbc_impl;
	DBT dbkey, dbdata;

	(void) memset(&dbdata, '\0', sizeof dbdata);
	(void) memset(&dbkey, '\0', sizeof dbkey);
	dbkey.data = key->data;
	dbkey.size = key->size;
	dbdata.data = value->data;
	dbdata.size = value->size;

	return db2_error_to_smdb(dbc->c_put(dbc, &dbkey, &dbdata, 0));
}

int
smdb2_cursor(database, cursor, flags)
	SMDB_DATABASE *database;
	SMDB_CURSOR **cursor;
	SMDB_FLAG flags;
{
	int result;
	DB *db = ((SMDB_DB2_DATABASE *) database->smdb_impl)->smdb2_db;
	DBC *db2_cursor;

# if DB_VERSION_MAJOR > 2 || DB_VERSION_MINOR >= 6
	result = db->cursor(db, NULL, &db2_cursor, 0);
# else /* DB_VERSION_MAJOR > 2 || DB_VERSION_MINOR >= 6 */
	result = db->cursor(db, NULL, &db2_cursor);
# endif /* DB_VERSION_MAJOR > 2 || DB_VERSION_MINOR >= 6 */
	if (result != 0)
		return db2_error_to_smdb(result);

	*cursor = (SMDB_CURSOR *) malloc(sizeof(SMDB_CURSOR));
	if (*cursor == NULL)
		return SMDBE_MALLOC;

	(*cursor)->smdbc_close = smdb2_cursor_close;
	(*cursor)->smdbc_del = smdb2_cursor_del;
	(*cursor)->smdbc_get = smdb2_cursor_get;
	(*cursor)->smdbc_put = smdb2_cursor_put;
	(*cursor)->smdbc_impl = db2_cursor;

	return SMDBE_OK;
}

# if DB_VERSION_MAJOR == 2
static int
smdb_db_open_internal(db_name, db_type, db_flags, db_params, db)
	char *db_name;
	DBTYPE db_type;
	int db_flags;
	SMDB_DBPARAMS *db_params;
	DB **db;
{
	void *params;
	DB_INFO db_info;

	params = NULL;
	(void) memset(&db_info, '\0', sizeof db_info);
	if (db_params != NULL)
	{
		db_info.db_cachesize = db_params->smdbp_cache_size;
		if (db_type == DB_HASH)
			db_info.h_nelem = db_params->smdbp_num_elements;
		if (db_params->smdbp_allow_dup)
			db_info.flags |= DB_DUP;
		params = &db_info;
	}
	return db_open(db_name, db_type, db_flags, DBMMODE, NULL, params, db);
}
# endif /* DB_VERSION_MAJOR == 2 */

# if DB_VERSION_MAJOR > 2
static int
smdb_db_open_internal(db_name, db_type, db_flags, db_params, db)
	char *db_name;
	DBTYPE db_type;
	int db_flags;
	SMDB_DBPARAMS *db_params;
	DB **db;
{
	int result;

	result = db_create(db, NULL, 0);
	if (result != 0 || *db == NULL)
		return result;

	if (db_params != NULL)
	{
		result = (*db)->set_cachesize(*db, 0,
					      db_params->smdbp_cache_size, 0);
		if (result != 0)
		{
			(void) (*db)->close((*db), 0);
			*db = NULL;
			return db2_error_to_smdb(result);
		}
		if (db_type == DB_HASH)
		{
			result = (*db)->set_h_nelem(*db, db_params->smdbp_num_elements);
			if (result != 0)
			{
				(void) (*db)->close(*db, 0);
				*db = NULL;
				return db2_error_to_smdb(result);
			}
		}
		if (db_params->smdbp_allow_dup)
		{
			result = (*db)->set_flags(*db, DB_DUP);
			if (result != 0)
			{
				(void) (*db)->close(*db, 0);
				*db = NULL;
				return db2_error_to_smdb(result);
			}
		}
	}

	result = (*db)->open(*db,
			     DBTXN	/* transaction for DB 4.1 */
			     db_name, NULL, db_type, db_flags, DBMMODE);
	if (result != 0)
	{
		(void) (*db)->close(*db, 0);
		*db = NULL;
	}
	return db2_error_to_smdb(result);
}
# endif /* DB_VERSION_MAJOR > 2 */
/*
**  SMDB_DB_OPEN -- Opens a db database.
**
**	Parameters:
**		database -- An unallocated database pointer to a pointer.
**		db_name -- The name of the database without extension.
**		mode -- File permisions for a created database.
**		mode_mask -- Mode bits that must match on an opened database.
**		sff -- Flags for safefile.
**		type -- The type of database to open
**			See smdb_type_to_db2_type for valid types.
**		user_info -- User information for file permissions.
**		db_params --
**			An SMDB_DBPARAMS struct including params. These
**			are processed according to the type of the
**			database. Currently supported params (only for
**			HASH type) are:
**			   num_elements
**			   cache_size
**
**	Returns:
**		SMDBE_OK -- Success, other errno:
**		SMDBE_MALLOC -- Cannot allocate memory.
**		SMDBE_BAD_OPEN -- db_open didn't return an error, but
**				 somehow the DB pointer is NULL.
**		Anything else: translated error from db2
*/

int
smdb_db_open(database, db_name, mode, mode_mask, sff, type, user_info, db_params)
	SMDB_DATABASE **database;
	char *db_name;
	int mode;
	int mode_mask;
	long sff;
	SMDB_DBTYPE type;
	SMDB_USER_INFO *user_info;
	SMDB_DBPARAMS *db_params;
{
	bool lockcreated = false;
	int result;
	int db_flags;
	int lock_fd;
	int db_fd;
	int major_v, minor_v, patch_v;
	SMDB_DATABASE *smdb_db;
	SMDB_DB2_DATABASE *db2;
	DB *db;
	DBTYPE db_type;
	struct stat stat_info;
	char db_file_name[MAXPATHLEN];

	(void) db_version(&major_v, &minor_v, &patch_v);
	if (major_v != DB_VERSION_MAJOR || minor_v != DB_VERSION_MINOR)
		return SMDBE_VERSION_MISMATCH;

	*database = NULL;

	result = smdb_add_extension(db_file_name, sizeof db_file_name,
				    db_name, SMDB2_FILE_EXTENSION);
	if (result != SMDBE_OK)
		return result;

	result = smdb_setup_file(db_name, SMDB2_FILE_EXTENSION,
				 mode_mask, sff, user_info, &stat_info);
	if (result != SMDBE_OK)
		return result;

	lock_fd = -1;

	if (stat_info.st_mode == ST_MODE_NOFILE &&
	    bitset(mode, O_CREAT))
		lockcreated = true;

	result = smdb_lock_file(&lock_fd, db_name, mode, sff,
				SMDB2_FILE_EXTENSION);
	if (result != SMDBE_OK)
		return result;

	if (lockcreated)
	{
		mode |= O_TRUNC;
		mode &= ~(O_CREAT|O_EXCL);
	}

	smdb_db = smdb_malloc_database();
	db2 = smdb2_malloc_database();
	if (db2 == NULL || smdb_db == NULL)
	{
		smdb_unlock_file(lock_fd);
		smdb_free_database(smdb_db);	/* ok to be NULL */
		if (db2 != NULL)
			free(db2);
		return SMDBE_MALLOC;
	}

	db2->smdb2_lock_fd = lock_fd;

	db_type = smdb_type_to_db2_type(type);

	db = NULL;

	db_flags = 0;
	if (bitset(O_CREAT, mode))
		db_flags |= DB_CREATE;
	if (bitset(O_TRUNC, mode))
		db_flags |= DB_TRUNCATE;
	if (mode == O_RDONLY)
		db_flags |= DB_RDONLY;
	SM_DB_FLAG_ADD(db_flags);

	result = smdb_db_open_internal(db_file_name, db_type,
				       db_flags, db_params, &db);

	if (result == 0 && db != NULL)
	{
		result = db->fd(db, &db_fd);
		if (result == 0)
			result = SMDBE_OK;
	}
	else
	{
		/* Try and narrow down on the problem */
		if (result != 0)
			result = db2_error_to_smdb(result);
		else
			result = SMDBE_BAD_OPEN;
	}

	if (result == SMDBE_OK)
		result = smdb_filechanged(db_name, SMDB2_FILE_EXTENSION, db_fd,
					  &stat_info);

	if (result == SMDBE_OK)
	{
		/* Everything is ok. Setup driver */
		db2->smdb2_db = db;

		smdb_db->smdb_close = smdb2_close;
		smdb_db->smdb_del = smdb2_del;
		smdb_db->smdb_fd = smdb2_fd;
		smdb_db->smdb_lockfd = smdb2_lockfd;
		smdb_db->smdb_get = smdb2_get;
		smdb_db->smdb_put = smdb2_put;
		smdb_db->smdb_set_owner = smdb2_set_owner;
		smdb_db->smdb_sync = smdb2_sync;
		smdb_db->smdb_cursor = smdb2_cursor;
		smdb_db->smdb_impl = db2;

		*database = smdb_db;

		return SMDBE_OK;
	}

	if (db != NULL)
		db->close(db, 0);

	smdb_unlock_file(db2->smdb2_lock_fd);
	free(db2);
	smdb_free_database(smdb_db);

	return result;
}

#endif /* (DB_VERSION_MAJOR >= 2) */
