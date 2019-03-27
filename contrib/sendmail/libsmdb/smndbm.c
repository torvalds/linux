/*
** Copyright (c) 1999-2002 Proofpoint, Inc. and its suppliers.
**	All rights reserved.
**
** By using this file, you agree to the terms and conditions set
** forth in the LICENSE file which can be found at the top level of
** the sendmail distribution.
*/

#include <sm/gen.h>
SM_RCSID("@(#)$Id: smndbm.c,v 8.55 2013-11-22 20:51:49 ca Exp $")

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <sendmail/sendmail.h>
#include <libsmdb/smdb.h>

#ifdef NDBM

# define SMNDB_DIR_FILE_EXTENSION "dir"
# define SMNDB_PAG_FILE_EXTENSION "pag"

struct smdb_dbm_database_struct
{
	DBM	*smndbm_dbm;
	int	smndbm_lock_fd;
	bool	smndbm_cursor_in_use;
};
typedef struct smdb_dbm_database_struct SMDB_DBM_DATABASE;

struct smdb_dbm_cursor_struct
{
	SMDB_DBM_DATABASE	*smndbmc_db;
	datum			smndbmc_current_key;
};
typedef struct smdb_dbm_cursor_struct SMDB_DBM_CURSOR;

/*
**  SMDB_PUT_FLAGS_TO_NDBM_FLAGS -- Translates smdb put flags to ndbm put flags.
**
**	Parameters:
**		flags -- The flags to translate.
**
**	Returns:
**		The ndbm flags that are equivalent to the smdb flags.
**
**	Notes:
**		Any invalid flags are ignored.
**
*/

int
smdb_put_flags_to_ndbm_flags(flags)
	SMDB_FLAG flags;
{
	int return_flags;

	return_flags = 0;
	if (bitset(SMDBF_NO_OVERWRITE, flags))
		return_flags = DBM_INSERT;
	else
		return_flags = DBM_REPLACE;

	return return_flags;
}

/*
**  Except for smdb_ndbm_open, the rest of these function correspond to the
**  interface laid out in smdb.h.
*/

SMDB_DBM_DATABASE *
smdbm_malloc_database()
{
	SMDB_DBM_DATABASE *db;

	db = (SMDB_DBM_DATABASE *) malloc(sizeof(SMDB_DBM_DATABASE));
	if (db != NULL)
	{
		db->smndbm_dbm = NULL;
		db->smndbm_lock_fd = -1;
		db->smndbm_cursor_in_use = false;
	}

	return db;
}

int
smdbm_close(database)
	SMDB_DATABASE *database;
{
	SMDB_DBM_DATABASE *db = (SMDB_DBM_DATABASE *) database->smdb_impl;
	DBM *dbm = ((SMDB_DBM_DATABASE *) database->smdb_impl)->smndbm_dbm;

	dbm_close(dbm);
	if (db->smndbm_lock_fd != -1)
		close(db->smndbm_lock_fd);

	free(db);
	database->smdb_impl = NULL;

	return SMDBE_OK;
}

int
smdbm_del(database, key, flags)
	SMDB_DATABASE *database;
	SMDB_DBENT *key;
	unsigned int flags;
{
	int result;
	DBM *dbm = ((SMDB_DBM_DATABASE *) database->smdb_impl)->smndbm_dbm;
	datum dbkey;

	(void) memset(&dbkey, '\0', sizeof dbkey);
	dbkey.dptr = key->data;
	dbkey.dsize = key->size;

	errno = 0;
	result = dbm_delete(dbm, dbkey);
	if (result != 0)
	{
		int save_errno = errno;

		if (dbm_error(dbm))
			return SMDBE_IO_ERROR;

		if (save_errno != 0)
			return save_errno;

		return SMDBE_NOT_FOUND;
	}
	return SMDBE_OK;
}

int
smdbm_fd(database, fd)
	SMDB_DATABASE *database;
	int *fd;
{
	DBM *dbm = ((SMDB_DBM_DATABASE *) database->smdb_impl)->smndbm_dbm;

	*fd = dbm_dirfno(dbm);
	if (*fd <= 0)
		return EINVAL;

	return SMDBE_OK;
}

int
smdbm_lockfd(database)
	SMDB_DATABASE *database;
{
	SMDB_DBM_DATABASE *db = (SMDB_DBM_DATABASE *) database->smdb_impl;

	return db->smndbm_lock_fd;
}

int
smdbm_get(database, key, data, flags)
	SMDB_DATABASE *database;
	SMDB_DBENT *key;
	SMDB_DBENT *data;
	unsigned int flags;
{
	DBM *dbm = ((SMDB_DBM_DATABASE *) database->smdb_impl)->smndbm_dbm;
	datum dbkey, dbdata;

	(void) memset(&dbkey, '\0', sizeof dbkey);
	(void) memset(&dbdata, '\0', sizeof dbdata);
	dbkey.dptr = key->data;
	dbkey.dsize = key->size;

	errno = 0;
	dbdata = dbm_fetch(dbm, dbkey);
	if (dbdata.dptr == NULL)
	{
		int save_errno = errno;

		if (dbm_error(dbm))
			return SMDBE_IO_ERROR;

		if (save_errno != 0)
			return save_errno;

		return SMDBE_NOT_FOUND;
	}
	data->data = dbdata.dptr;
	data->size = dbdata.dsize;
	return SMDBE_OK;
}

int
smdbm_put(database, key, data, flags)
	SMDB_DATABASE *database;
	SMDB_DBENT *key;
	SMDB_DBENT *data;
	unsigned int flags;
{
	int result;
	int save_errno;
	DBM *dbm = ((SMDB_DBM_DATABASE *) database->smdb_impl)->smndbm_dbm;
	datum dbkey, dbdata;

	(void) memset(&dbkey, '\0', sizeof dbkey);
	(void) memset(&dbdata, '\0', sizeof dbdata);
	dbkey.dptr = key->data;
	dbkey.dsize = key->size;
	dbdata.dptr = data->data;
	dbdata.dsize = data->size;

	errno = 0;
	result = dbm_store(dbm, dbkey, dbdata,
			   smdb_put_flags_to_ndbm_flags(flags));
	switch (result)
	{
	  case 1:
		return SMDBE_DUPLICATE;

	  case 0:
		return SMDBE_OK;

	  default:
		save_errno = errno;

		if (dbm_error(dbm))
			return SMDBE_IO_ERROR;

		if (save_errno != 0)
			return save_errno;

		return SMDBE_IO_ERROR;
	}
	/* NOTREACHED */
}

int
smndbm_set_owner(database, uid, gid)
	SMDB_DATABASE *database;
	uid_t uid;
	gid_t gid;
{
# if HASFCHOWN
	int fd;
	int result;
	DBM *dbm = ((SMDB_DBM_DATABASE *) database->smdb_impl)->smndbm_dbm;

	fd = dbm_dirfno(dbm);
	if (fd <= 0)
		return EINVAL;

	result = fchown(fd, uid, gid);
	if (result < 0)
		return errno;

	fd = dbm_pagfno(dbm);
	if (fd <= 0)
		return EINVAL;

	result = fchown(fd, uid, gid);
	if (result < 0)
		return errno;
# endif /* HASFCHOWN */

	return SMDBE_OK;
}

int
smdbm_sync(database, flags)
	SMDB_DATABASE *database;
	unsigned int flags;
{
	return SMDBE_UNSUPPORTED;
}

int
smdbm_cursor_close(cursor)
	SMDB_CURSOR *cursor;
{
	SMDB_DBM_CURSOR *dbm_cursor = (SMDB_DBM_CURSOR *) cursor->smdbc_impl;
	SMDB_DBM_DATABASE *db = dbm_cursor->smndbmc_db;

	if (!db->smndbm_cursor_in_use)
		return SMDBE_NOT_A_VALID_CURSOR;

	db->smndbm_cursor_in_use = false;
	free(dbm_cursor);
	free(cursor);

	return SMDBE_OK;
}

int
smdbm_cursor_del(cursor, flags)
	SMDB_CURSOR *cursor;
	unsigned int flags;
{
	int result;
	SMDB_DBM_CURSOR *dbm_cursor = (SMDB_DBM_CURSOR *) cursor->smdbc_impl;
	SMDB_DBM_DATABASE *db = dbm_cursor->smndbmc_db;
	DBM *dbm = db->smndbm_dbm;

	errno = 0;
	result = dbm_delete(dbm, dbm_cursor->smndbmc_current_key);
	if (result != 0)
	{
		int save_errno = errno;

		if (dbm_error(dbm))
			return SMDBE_IO_ERROR;

		if (save_errno != 0)
			return save_errno;

		return SMDBE_NOT_FOUND;
	}
	return SMDBE_OK;
}

int
smdbm_cursor_get(cursor, key, value, flags)
	SMDB_CURSOR *cursor;
	SMDB_DBENT *key;
	SMDB_DBENT *value;
	SMDB_FLAG flags;
{
	SMDB_DBM_CURSOR *dbm_cursor = (SMDB_DBM_CURSOR *) cursor->smdbc_impl;
	SMDB_DBM_DATABASE *db = dbm_cursor->smndbmc_db;
	DBM *dbm = db->smndbm_dbm;
	datum dbkey, dbdata;

	(void) memset(&dbkey, '\0', sizeof dbkey);
	(void) memset(&dbdata, '\0', sizeof dbdata);

	if (flags == SMDB_CURSOR_GET_RANGE)
		return SMDBE_UNSUPPORTED;

	if (dbm_cursor->smndbmc_current_key.dptr == NULL)
	{
		dbm_cursor->smndbmc_current_key = dbm_firstkey(dbm);
		if (dbm_cursor->smndbmc_current_key.dptr == NULL)
		{
			if (dbm_error(dbm))
				return SMDBE_IO_ERROR;
			return SMDBE_LAST_ENTRY;
		}
	}
	else
	{
		dbm_cursor->smndbmc_current_key = dbm_nextkey(dbm);
		if (dbm_cursor->smndbmc_current_key.dptr == NULL)
		{
			if (dbm_error(dbm))
				return SMDBE_IO_ERROR;
			return SMDBE_LAST_ENTRY;
		}
	}

	errno = 0;
	dbdata = dbm_fetch(dbm, dbm_cursor->smndbmc_current_key);
	if (dbdata.dptr == NULL)
	{
		int save_errno = errno;

		if (dbm_error(dbm))
			return SMDBE_IO_ERROR;

		if (save_errno != 0)
			return save_errno;

		return SMDBE_NOT_FOUND;
	}
	value->data = dbdata.dptr;
	value->size = dbdata.dsize;
	key->data = dbm_cursor->smndbmc_current_key.dptr;
	key->size = dbm_cursor->smndbmc_current_key.dsize;

	return SMDBE_OK;
}

int
smdbm_cursor_put(cursor, key, value, flags)
	SMDB_CURSOR *cursor;
	SMDB_DBENT *key;
	SMDB_DBENT *value;
	SMDB_FLAG flags;
{
	int result;
	int save_errno;
	SMDB_DBM_CURSOR *dbm_cursor = (SMDB_DBM_CURSOR *) cursor->smdbc_impl;
	SMDB_DBM_DATABASE *db = dbm_cursor->smndbmc_db;
	DBM *dbm = db->smndbm_dbm;
	datum dbdata;

	(void) memset(&dbdata, '\0', sizeof dbdata);
	dbdata.dptr = value->data;
	dbdata.dsize = value->size;

	errno = 0;
	result = dbm_store(dbm, dbm_cursor->smndbmc_current_key, dbdata,
			   smdb_put_flags_to_ndbm_flags(flags));
	switch (result)
	{
	  case 1:
		return SMDBE_DUPLICATE;

	  case 0:
		return SMDBE_OK;

	  default:
		save_errno = errno;

		if (dbm_error(dbm))
			return SMDBE_IO_ERROR;

		if (save_errno != 0)
			return save_errno;

		return SMDBE_IO_ERROR;
	}
	/* NOTREACHED */
}

int
smdbm_cursor(database, cursor, flags)
	SMDB_DATABASE *database;
	SMDB_CURSOR **cursor;
	SMDB_FLAG flags;
{
	SMDB_DBM_DATABASE *db = (SMDB_DBM_DATABASE *) database->smdb_impl;
	SMDB_CURSOR *cur;
	SMDB_DBM_CURSOR *dbm_cursor;

	if (db->smndbm_cursor_in_use)
		return SMDBE_ONLY_SUPPORTS_ONE_CURSOR;

	db->smndbm_cursor_in_use = true;
	dbm_cursor = (SMDB_DBM_CURSOR *) malloc(sizeof(SMDB_DBM_CURSOR));
	if (dbm_cursor == NULL)
		return SMDBE_MALLOC;
	dbm_cursor->smndbmc_db = db;
	dbm_cursor->smndbmc_current_key.dptr = NULL;
	dbm_cursor->smndbmc_current_key.dsize = 0;

	cur = (SMDB_CURSOR*) malloc(sizeof(SMDB_CURSOR));
	if (cur == NULL)
	{
		free(dbm_cursor);
		return SMDBE_MALLOC;
	}

	cur->smdbc_impl = dbm_cursor;
	cur->smdbc_close = smdbm_cursor_close;
	cur->smdbc_del = smdbm_cursor_del;
	cur->smdbc_get = smdbm_cursor_get;
	cur->smdbc_put = smdbm_cursor_put;
	*cursor = cur;

	return SMDBE_OK;
}
/*
**  SMDB_NDBM_OPEN -- Opens a ndbm database.
**
**	Parameters:
**		database -- An unallocated database pointer to a pointer.
**		db_name -- The name of the database without extension.
**		mode -- File permisions on a created database.
**		mode_mask -- Mode bits that much match on an opened database.
**		sff -- Flags to safefile.
**		type -- The type of database to open.
**			Only SMDB_NDBM is supported.
**		user_info -- Information on the user to use for file
**			    permissions.
**		db_params -- No params are supported.
**
**	Returns:
**		SMDBE_OK -- Success, otherwise errno:
**		SMDBE_MALLOC -- Cannot allocate memory.
**		SMDBE_UNSUPPORTED -- The type is not supported.
**		SMDBE_GDBM_IS_BAD -- We have detected GDBM and we don't
**				    like it.
**		SMDBE_BAD_OPEN -- dbm_open failed and errno was not set.
**		Anything else: errno
*/

int
smdb_ndbm_open(database, db_name, mode, mode_mask, sff, type, user_info,
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
	int result;
	int lock_fd;
	SMDB_DATABASE *smdb_db;
	SMDB_DBM_DATABASE *db;
	DBM *dbm = NULL;
	struct stat dir_stat_info;
	struct stat pag_stat_info;

	result = SMDBE_OK;
	*database = NULL;

	if (type == NULL)
		return SMDBE_UNKNOWN_DB_TYPE;

	result = smdb_setup_file(db_name, SMNDB_DIR_FILE_EXTENSION, mode_mask,
				 sff, user_info, &dir_stat_info);
	if (result != SMDBE_OK)
		return result;

	result = smdb_setup_file(db_name, SMNDB_PAG_FILE_EXTENSION, mode_mask,
				 sff, user_info, &pag_stat_info);
	if (result != SMDBE_OK)
		return result;

	if ((dir_stat_info.st_mode == ST_MODE_NOFILE ||
	     pag_stat_info.st_mode == ST_MODE_NOFILE) &&
	    bitset(mode, O_CREAT))
		lockcreated = true;

	lock_fd = -1;
	result = smdb_lock_file(&lock_fd, db_name, mode, sff,
				SMNDB_DIR_FILE_EXTENSION);
	if (result != SMDBE_OK)
		return result;

	if (lockcreated)
	{
		int pag_fd;

		/* Need to pre-open the .pag file as well with O_EXCL */
		result = smdb_lock_file(&pag_fd, db_name, mode, sff,
					SMNDB_PAG_FILE_EXTENSION);
		if (result != SMDBE_OK)
		{
			(void) close(lock_fd);
			return result;
		}
		(void) close(pag_fd);

		mode |= O_TRUNC;
		mode &= ~(O_CREAT|O_EXCL);
	}

	smdb_db = smdb_malloc_database();
	if (smdb_db == NULL)
		result = SMDBE_MALLOC;

	db = smdbm_malloc_database();
	if (db == NULL)
		result = SMDBE_MALLOC;

	/* Try to open database */
	if (result == SMDBE_OK)
	{
		db->smndbm_lock_fd = lock_fd;

		errno = 0;
		dbm = dbm_open(db_name, mode, DBMMODE);
		if (dbm == NULL)
		{
			if (errno == 0)
				result = SMDBE_BAD_OPEN;
			else
				result = errno;
		}
		db->smndbm_dbm = dbm;
	}

	/* Check for GDBM */
	if (result == SMDBE_OK)
	{
		if (dbm_dirfno(dbm) == dbm_pagfno(dbm))
			result = SMDBE_GDBM_IS_BAD;
	}

	/* Check for filechanged */
	if (result == SMDBE_OK)
	{
		result = smdb_filechanged(db_name, SMNDB_DIR_FILE_EXTENSION,
					  dbm_dirfno(dbm), &dir_stat_info);
		if (result == SMDBE_OK)
		{
			result = smdb_filechanged(db_name,
						  SMNDB_PAG_FILE_EXTENSION,
						  dbm_pagfno(dbm),
						  &pag_stat_info);
		}
	}

	/* XXX Got to get fchown stuff in here */

	/* Setup driver if everything is ok */
	if (result == SMDBE_OK)
	{
		*database = smdb_db;

		smdb_db->smdb_close = smdbm_close;
		smdb_db->smdb_del = smdbm_del;
		smdb_db->smdb_fd = smdbm_fd;
		smdb_db->smdb_lockfd = smdbm_lockfd;
		smdb_db->smdb_get = smdbm_get;
		smdb_db->smdb_put = smdbm_put;
		smdb_db->smdb_set_owner = smndbm_set_owner;
		smdb_db->smdb_sync = smdbm_sync;
		smdb_db->smdb_cursor = smdbm_cursor;

		smdb_db->smdb_impl = db;

		return SMDBE_OK;
	}

	/* If we're here, something bad happened, clean up */
	if (dbm != NULL)
		dbm_close(dbm);

	smdb_unlock_file(db->smndbm_lock_fd);
	free(db);
	smdb_free_database(smdb_db);

	return result;
}
#endif /* NDBM */
