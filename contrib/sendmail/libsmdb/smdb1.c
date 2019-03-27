/*
** Copyright (c) 1999-2002, 2004, 2009 Proofpoint, Inc. and its suppliers.
**	All rights reserved.
**
** By using this file, you agree to the terms and conditions set
** forth in the LICENSE file which can be found at the top level of
** the sendmail distribution.
*/

#include <sm/gen.h>
SM_RCSID("@(#)$Id: smdb1.c,v 8.63 2013-11-22 20:51:49 ca Exp $")

#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

#include <sendmail/sendmail.h>
#include <libsmdb/smdb.h>

#if (DB_VERSION_MAJOR == 1)

# define SMDB1_FILE_EXTENSION "db"

struct smdb_db1_struct
{
	DB	*smdb1_db;
	int	smdb1_lock_fd;
	bool	smdb1_cursor_in_use;
};
typedef struct smdb_db1_struct SMDB_DB1_DATABASE;

struct smdb_db1_cursor
{
	SMDB_DB1_DATABASE	*db;
};
typedef struct smdb_db1_cursor SMDB_DB1_CURSOR;

static DBTYPE		smdb_type_to_db1_type __P((SMDB_DBTYPE));
static unsigned int	smdb_put_flags_to_db1_flags __P((SMDB_FLAG));
static int		smdb_cursor_get_flags_to_smdb1 __P((SMDB_FLAG));
static SMDB_DB1_DATABASE *smdb1_malloc_database __P((void));
static int		smdb1_close __P((SMDB_DATABASE *));
static int		smdb1_del __P((SMDB_DATABASE *, SMDB_DBENT *, unsigned int));
static int		smdb1_fd __P((SMDB_DATABASE *, int *));
static int		smdb1_lockfd __P((SMDB_DATABASE *));
static int		smdb1_get __P((SMDB_DATABASE *, SMDB_DBENT *, SMDB_DBENT *, unsigned int));
static int		smdb1_put __P((SMDB_DATABASE *, SMDB_DBENT *, SMDB_DBENT *, unsigned int));
static int		smdb1_set_owner __P((SMDB_DATABASE *, uid_t, gid_t));
static int		smdb1_sync __P((SMDB_DATABASE *, unsigned int));
static int		smdb1_cursor_close __P((SMDB_CURSOR *));
static int		smdb1_cursor_del __P((SMDB_CURSOR *, unsigned int));
static int		smdb1_cursor_get __P((SMDB_CURSOR *, SMDB_DBENT *, SMDB_DBENT *, SMDB_FLAG));
static int		smdb1_cursor_put __P((SMDB_CURSOR *, SMDB_DBENT *, SMDB_DBENT *, SMDB_FLAG));
static int		smdb1_cursor __P((SMDB_DATABASE *, SMDB_CURSOR **, unsigned int));

/*
**  SMDB_TYPE_TO_DB1_TYPE -- Translates smdb database type to db1 type.
**
**	Parameters:
**		type -- The type to translate.
**
**	Returns:
**		The DB1 type that corresponsds to the passed in SMDB type.
**		Returns -1 if there is no equivalent type.
**
*/

static DBTYPE
smdb_type_to_db1_type(type)
	SMDB_DBTYPE type;
{
	if (type == SMDB_TYPE_DEFAULT)
		return DB_HASH;

	if (strncmp(type, SMDB_TYPE_HASH, SMDB_TYPE_HASH_LEN) == 0)
		return DB_HASH;

	if (strncmp(type, SMDB_TYPE_BTREE, SMDB_TYPE_BTREE_LEN) == 0)
		return DB_BTREE;

	/* Should never get here thanks to test in smdb_db_open() */
	return DB_HASH;
}
/*
**  SMDB_PUT_FLAGS_TO_DB1_FLAGS -- Translates smdb put flags to db1 put flags.
**
**	Parameters:
**		flags -- The flags to translate.
**
**	Returns:
**		The db1 flags that are equivalent to the smdb flags.
**
**	Notes:
**		Any invalid flags are ignored.
**
*/

static unsigned int
smdb_put_flags_to_db1_flags(flags)
	SMDB_FLAG flags;
{
	int return_flags;

	return_flags = 0;

	if (bitset(SMDBF_NO_OVERWRITE, flags))
		return_flags |= R_NOOVERWRITE;

	return return_flags;
}
/*
**  SMDB_CURSOR_GET_FLAGS_TO_SMDB1
**
**	Parameters:
**		flags -- The flags to translate.
**
**	Returns:
**		The db1 flags that are equivalent to the smdb flags.
**
**	Notes:
**		Returns -1 if we don't support the flag.
**
*/

static int
smdb_cursor_get_flags_to_smdb1(flags)
	SMDB_FLAG flags;
{
	switch(flags)
	{
		case SMDB_CURSOR_GET_FIRST:
			return R_FIRST;

		case SMDB_CURSOR_GET_LAST:
			return R_LAST;

		case SMDB_CURSOR_GET_NEXT:
			return R_NEXT;

		case SMDB_CURSOR_GET_RANGE:
			return R_CURSOR;

		default:
			return -1;
	}
}

/*
**  The rest of these functions correspond to the interface laid out in smdb.h.
*/

static SMDB_DB1_DATABASE *
smdb1_malloc_database()
{
	SMDB_DB1_DATABASE *db1;

	db1 = (SMDB_DB1_DATABASE *) malloc(sizeof(SMDB_DB1_DATABASE));

	if (db1 != NULL)
	{
		db1->smdb1_lock_fd = -1;
		db1->smdb1_cursor_in_use = false;
	}

	return db1;
}

static int
smdb1_close(database)
	SMDB_DATABASE *database;
{
	int result;
	SMDB_DB1_DATABASE *db1 = (SMDB_DB1_DATABASE *) database->smdb_impl;
	DB *db = ((SMDB_DB1_DATABASE *) database->smdb_impl)->smdb1_db;

	result = db->close(db);
	if (db1->smdb1_lock_fd != -1)
		(void) close(db1->smdb1_lock_fd);

	free(db1);
	database->smdb_impl = NULL;

	return result;
}

static int
smdb1_del(database, key, flags)
	SMDB_DATABASE *database;
	SMDB_DBENT *key;
	unsigned int flags;
{
	DB *db = ((SMDB_DB1_DATABASE *) database->smdb_impl)->smdb1_db;
	DBT dbkey;

	(void) memset(&dbkey, '\0', sizeof dbkey);
	dbkey.data = key->data;
	dbkey.size = key->size;
	return db->del(db, &dbkey, flags);
}

static int
smdb1_fd(database, fd)
	SMDB_DATABASE *database;
	int *fd;
{
	DB *db = ((SMDB_DB1_DATABASE *) database->smdb_impl)->smdb1_db;

	*fd = db->fd(db);
	if (*fd == -1)
		return errno;

	return SMDBE_OK;
}

static int
smdb1_lockfd(database)
	SMDB_DATABASE *database;
{
	SMDB_DB1_DATABASE *db1 = (SMDB_DB1_DATABASE *) database->smdb_impl;

	return db1->smdb1_lock_fd;
}


static int
smdb1_get(database, key, data, flags)
	SMDB_DATABASE *database;
	SMDB_DBENT *key;
	SMDB_DBENT *data;
	unsigned int flags;
{
	int result;
	DB *db = ((SMDB_DB1_DATABASE *) database->smdb_impl)->smdb1_db;
	DBT dbkey, dbdata;

	(void) memset(&dbdata, '\0', sizeof dbdata);
	(void) memset(&dbkey, '\0', sizeof dbkey);
	dbkey.data = key->data;
	dbkey.size = key->size;

	result = db->get(db, &dbkey, &dbdata, flags);
	if (result != 0)
	{
		if (result == 1)
			return SMDBE_NOT_FOUND;
		return errno;
	}
	data->data = dbdata.data;
	data->size = dbdata.size;
	return SMDBE_OK;
}

static int
smdb1_put(database, key, data, flags)
	SMDB_DATABASE *database;
	SMDB_DBENT *key;
	SMDB_DBENT *data;
	unsigned int flags;
{
	DB *db = ((SMDB_DB1_DATABASE *) database->smdb_impl)->smdb1_db;
	DBT dbkey, dbdata;

	(void) memset(&dbdata, '\0', sizeof dbdata);
	(void) memset(&dbkey, '\0', sizeof dbkey);
	dbkey.data = key->data;
	dbkey.size = key->size;
	dbdata.data = data->data;
	dbdata.size = data->size;

	return db->put(db, &dbkey, &dbdata,
		       smdb_put_flags_to_db1_flags(flags));
}

static int
smdb1_set_owner(database, uid, gid)
	SMDB_DATABASE *database;
	uid_t uid;
	gid_t gid;
{
# if HASFCHOWN
	int fd;
	int result;
	DB *db = ((SMDB_DB1_DATABASE *) database->smdb_impl)->smdb1_db;

	fd = db->fd(db);
	if (fd == -1)
		return errno;

	result = fchown(fd, uid, gid);
	if (result < 0)
		return errno;
# endif /* HASFCHOWN */

	return SMDBE_OK;
}

static int
smdb1_sync(database, flags)
	SMDB_DATABASE *database;
	unsigned int flags;
{
	DB *db = ((SMDB_DB1_DATABASE *) database->smdb_impl)->smdb1_db;

	return db->sync(db, flags);
}

static int
smdb1_cursor_close(cursor)
	SMDB_CURSOR *cursor;
{
	SMDB_DB1_CURSOR *db1_cursor = (SMDB_DB1_CURSOR *) cursor->smdbc_impl;
	SMDB_DB1_DATABASE *db1 = db1_cursor->db;

	if (!db1->smdb1_cursor_in_use)
		return SMDBE_NOT_A_VALID_CURSOR;

	db1->smdb1_cursor_in_use = false;
	free(cursor);

	return SMDBE_OK;
}

static int
smdb1_cursor_del(cursor, flags)
	SMDB_CURSOR *cursor;
	unsigned int flags;
{
	SMDB_DB1_CURSOR *db1_cursor = (SMDB_DB1_CURSOR *) cursor->smdbc_impl;
	SMDB_DB1_DATABASE *db1 = db1_cursor->db;
	DB *db = db1->smdb1_db;

	return db->del(db, NULL, R_CURSOR);
}

static int
smdb1_cursor_get(cursor, key, value, flags)
	SMDB_CURSOR *cursor;
	SMDB_DBENT *key;
	SMDB_DBENT *value;
	SMDB_FLAG flags;
{
	int db1_flags;
	int result;
	SMDB_DB1_CURSOR *db1_cursor = (SMDB_DB1_CURSOR *) cursor->smdbc_impl;
	SMDB_DB1_DATABASE *db1 = db1_cursor->db;
	DB *db = db1->smdb1_db;
	DBT dbkey, dbdata;

	(void) memset(&dbdata, '\0', sizeof dbdata);
	(void) memset(&dbkey, '\0', sizeof dbkey);

	db1_flags = smdb_cursor_get_flags_to_smdb1(flags);
	result = db->seq(db, &dbkey, &dbdata, db1_flags);
	if (result == -1)
		return errno;
	if (result == 1)
		return SMDBE_LAST_ENTRY;
	value->data = dbdata.data;
	value->size = dbdata.size;
	key->data = dbkey.data;
	key->size = dbkey.size;
	return SMDBE_OK;
}

static int
smdb1_cursor_put(cursor, key, value, flags)
	SMDB_CURSOR *cursor;
	SMDB_DBENT *key;
	SMDB_DBENT *value;
	SMDB_FLAG flags;
{
	SMDB_DB1_CURSOR *db1_cursor = (SMDB_DB1_CURSOR *) cursor->smdbc_impl;
	SMDB_DB1_DATABASE *db1 = db1_cursor->db;
	DB *db = db1->smdb1_db;
	DBT dbkey, dbdata;

	(void) memset(&dbdata, '\0', sizeof dbdata);
	(void) memset(&dbkey, '\0', sizeof dbkey);
	dbkey.data = key->data;
	dbkey.size = key->size;
	dbdata.data = value->data;
	dbdata.size = value->size;

	return db->put(db, &dbkey, &dbdata, R_CURSOR);
}

static int
smdb1_cursor(database, cursor, flags)
	SMDB_DATABASE *database;
	SMDB_CURSOR **cursor;
	unsigned int flags;
{
	SMDB_DB1_DATABASE *db1 = (SMDB_DB1_DATABASE *) database->smdb_impl;
	SMDB_CURSOR *cur;
	SMDB_DB1_CURSOR *db1_cursor;

	if (db1->smdb1_cursor_in_use)
		return SMDBE_ONLY_SUPPORTS_ONE_CURSOR;

	db1_cursor = (SMDB_DB1_CURSOR *) malloc(sizeof(SMDB_DB1_CURSOR));
	if (db1_cursor == NULL)
		return SMDBE_MALLOC;

	cur = (SMDB_CURSOR *) malloc(sizeof(SMDB_CURSOR));
	if (cur == NULL)
	{
		free(db1_cursor);
		return SMDBE_MALLOC;
	}

	db1->smdb1_cursor_in_use = true;
	db1_cursor->db = db1;
	cur->smdbc_impl = db1_cursor;
	cur->smdbc_close = smdb1_cursor_close;
	cur->smdbc_del = smdb1_cursor_del;
	cur->smdbc_get = smdb1_cursor_get;
	cur->smdbc_put = smdb1_cursor_put;
	*cursor = cur;

	return SMDBE_OK;
}
/*
**  SMDB_DB_OPEN -- Opens a db1 database.
**
**	Parameters:
**		database -- An unallocated database pointer to a pointer.
**		db_name -- The name of the database without extension.
**		mode -- File permisions on the database if created.
**		mode_mask -- Mode bits that must match on an existing database.
**		sff -- Flags for safefile.
**		type -- The type of database to open
**			See smdb_type_to_db1_type for valid types.
**		user_info -- Information on the user to use for file
**			    permissions.
**		db_params --
**			An SMDB_DBPARAMS struct including params. These
**			are processed according to the type of the
**			database. Currently supported params (only for
**			HASH type) are:
**			   num_elements
**			   cache_size
**
**	Returns:
**		SMDBE_OK -- Success, otherwise errno.
*/

int
smdb_db_open(database, db_name, mode, mode_mask, sff, type, user_info,
	     db_params)
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
	int db_fd;
	int lock_fd;
	int result;
	void *params;
	SMDB_DATABASE *smdb_db;
	SMDB_DB1_DATABASE *db1;
	DB *db;
	HASHINFO hash_info;
	BTREEINFO btree_info;
	DBTYPE db_type;
	struct stat stat_info;
	char db_file_name[MAXPATHLEN];

	if (type == NULL ||
	    (strncmp(SMDB_TYPE_HASH, type, SMDB_TYPE_HASH_LEN) != 0 &&
	     strncmp(SMDB_TYPE_BTREE, type, SMDB_TYPE_BTREE_LEN) != 0))
		return SMDBE_UNKNOWN_DB_TYPE;

	result = smdb_add_extension(db_file_name, sizeof db_file_name,
				    db_name, SMDB1_FILE_EXTENSION);
	if (result != SMDBE_OK)
		return result;

	result = smdb_setup_file(db_name, SMDB1_FILE_EXTENSION, mode_mask,
				 sff, user_info, &stat_info);
	if (result != SMDBE_OK)
		return result;

	if (stat_info.st_mode == ST_MODE_NOFILE &&
	    bitset(mode, O_CREAT))
		lockcreated = true;

	lock_fd = -1;
	result = smdb_lock_file(&lock_fd, db_name, mode, sff,
				SMDB1_FILE_EXTENSION);
	if (result != SMDBE_OK)
		return result;

	if (lockcreated)
	{
		mode |= O_TRUNC;
		mode &= ~(O_CREAT|O_EXCL);
	}

	*database = NULL;

	smdb_db = smdb_malloc_database();
	db1 = smdb1_malloc_database();
	if (smdb_db == NULL || db1 == NULL)
	{
		(void) smdb_unlock_file(lock_fd);
		smdb_free_database(smdb_db);
		free(db1);
		return SMDBE_MALLOC;
	}
	db1->smdb1_lock_fd = lock_fd;

	params = NULL;
	if (db_params != NULL &&
	    (strncmp(SMDB_TYPE_HASH, type, SMDB_TYPE_HASH_LEN) == 0))
	{
		(void) memset(&hash_info, '\0', sizeof hash_info);
		hash_info.nelem = db_params->smdbp_num_elements;
		hash_info.cachesize = db_params->smdbp_cache_size;
		params = &hash_info;
	}

	if (db_params != NULL &&
	    (strncmp(SMDB_TYPE_BTREE, type, SMDB_TYPE_BTREE_LEN) == 0))
	{
		(void) memset(&btree_info, '\0', sizeof btree_info);
		btree_info.cachesize = db_params->smdbp_cache_size;
		if (db_params->smdbp_allow_dup)
			btree_info.flags |= R_DUP;
		params = &btree_info;
	}

	db_type = smdb_type_to_db1_type(type);
	db = dbopen(db_file_name, mode, DBMMODE, db_type, params);
	if (db != NULL)
	{
		db_fd = db->fd(db);
		result = smdb_filechanged(db_name, SMDB1_FILE_EXTENSION, db_fd,
					  &stat_info);
	}
	else
	{
		if (errno == 0)
			result = SMDBE_BAD_OPEN;
		else
			result = errno;
	}

	if (result == SMDBE_OK)
	{
		/* Everything is ok. Setup driver */
		db1->smdb1_db = db;

		smdb_db->smdb_close = smdb1_close;
		smdb_db->smdb_del = smdb1_del;
		smdb_db->smdb_fd = smdb1_fd;
		smdb_db->smdb_lockfd = smdb1_lockfd;
		smdb_db->smdb_get = smdb1_get;
		smdb_db->smdb_put = smdb1_put;
		smdb_db->smdb_set_owner = smdb1_set_owner;
		smdb_db->smdb_sync = smdb1_sync;
		smdb_db->smdb_cursor = smdb1_cursor;
		smdb_db->smdb_impl = db1;

		*database = smdb_db;
		return SMDBE_OK;
	}

	if (db != NULL)
		(void) db->close(db);

	/* Error opening database */
	(void) smdb_unlock_file(db1->smdb1_lock_fd);
	free(db1);
	smdb_free_database(smdb_db);

	return result;
}

#endif /* (DB_VERSION_MAJOR == 1) */
