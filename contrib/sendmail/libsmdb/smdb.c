/*
** Copyright (c) 1999-2002 Proofpoint, Inc. and its suppliers.
**	All rights reserved.
**
** By using this file, you agree to the terms and conditions set
** forth in the LICENSE file which can be found at the top level of
** the sendmail distribution.
*/

#include <sm/gen.h>
SM_RCSID("@(#)$Id: smdb.c,v 8.59 2013-11-22 20:51:49 ca Exp $")

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>


#include <sendmail/sendmail.h>
#include <libsmdb/smdb.h>

static bool	smdb_lockfile __P((int, int));

/*
** SMDB_MALLOC_DATABASE -- Allocates a database structure.
**
**	Parameters:
**		None
**
**	Returns:
**		An pointer to an allocated SMDB_DATABASE structure or
**		NULL if it couldn't allocate the memory.
*/

SMDB_DATABASE *
smdb_malloc_database()
{
	SMDB_DATABASE *db;

	db = (SMDB_DATABASE *) malloc(sizeof(SMDB_DATABASE));

	if (db != NULL)
		(void) memset(db, '\0', sizeof(SMDB_DATABASE));

	return db;
}


/*
** SMDB_FREE_DATABASE -- Unallocates a database structure.
**
**	Parameters:
**		database -- a SMDB_DATABASE pointer to deallocate.
**
**	Returns:
**		None
*/

void
smdb_free_database(database)
	SMDB_DATABASE *database;
{
	if (database != NULL)
		free(database);
}
/*
**  SMDB_LOCKFILE -- lock a file using flock or (shudder) fcntl locking
**
**	Parameters:
**		fd -- the file descriptor of the file.
**		type -- type of the lock.  Bits can be:
**			LOCK_EX -- exclusive lock.
**			LOCK_NB -- non-blocking.
**
**	Returns:
**		true if the lock was acquired.
**		false otherwise.
*/

static bool
smdb_lockfile(fd, type)
	int fd;
	int type;
{
	int i;
	int save_errno;
#if !HASFLOCK
	int action;
	struct flock lfd;

	(void) memset(&lfd, '\0', sizeof lfd);
	if (bitset(LOCK_UN, type))
		lfd.l_type = F_UNLCK;
	else if (bitset(LOCK_EX, type))
		lfd.l_type = F_WRLCK;
	else
		lfd.l_type = F_RDLCK;

	if (bitset(LOCK_NB, type))
		action = F_SETLK;
	else
		action = F_SETLKW;

	while ((i = fcntl(fd, action, &lfd)) < 0 && errno == EINTR)
		continue;
	if (i >= 0)
		return true;
	save_errno = errno;

	/*
	**  On SunOS, if you are testing using -oQ/tmp/mqueue or
	**  -oA/tmp/aliases or anything like that, and /tmp is mounted
	**  as type "tmp" (that is, served from swap space), the
	**  previous fcntl will fail with "Invalid argument" errors.
	**  Since this is fairly common during testing, we will assume
	**  that this indicates that the lock is successfully grabbed.
	*/

	if (save_errno == EINVAL)
		return true;

	if (!bitset(LOCK_NB, type) ||
	    (save_errno != EACCES && save_errno != EAGAIN))
	{
# if 0
		int omode = fcntl(fd, F_GETFL, NULL);
		int euid = (int) geteuid();

		syslog(LOG_ERR, "cannot lockf(%s%s, fd=%d, type=%o, omode=%o, euid=%d)",
		       filename, ext, fd, type, omode, euid);
# endif /* 0 */
		errno = save_errno;
		return false;
	}
#else /* !HASFLOCK */

	while ((i = flock(fd, type)) < 0 && errno == EINTR)
		continue;
	if (i >= 0)
		return true;
	save_errno = errno;

	if (!bitset(LOCK_NB, type) || save_errno != EWOULDBLOCK)
	{
# if 0
		int omode = fcntl(fd, F_GETFL, NULL);
		int euid = (int) geteuid();

		syslog(LOG_ERR, "cannot flock(%s%s, fd=%d, type=%o, omode=%o, euid=%d)",
		       filename, ext, fd, type, omode, euid);
# endif /* 0 */
		errno = save_errno;
		return false;
	}
#endif /* !HASFLOCK */
	errno = save_errno;
	return false;
}
/*
** SMDB_OPEN_DATABASE -- Opens a database.
**
**	This opens a database. If type is SMDB_DEFAULT it tries to
**	use a DB1 or DB2 hash. If that isn't available, it will try
**	to use NDBM. If a specific type is given it will try to open
**	a database of that type.
**
**	Parameters:
**		database -- An pointer to a SMDB_DATABASE pointer where the
**			   opened database will be stored. This should
**			   be unallocated.
**		db_name -- The name of the database to open. Do not include
**			  the file name extension.
**		mode -- The mode to set on the database file or files.
**		mode_mask -- Mode bits that must match on an opened database.
**		sff -- Flags to safefile.
**		type -- The type of database to open. Supported types
**		       vary depending on what was compiled in.
**		user_info -- Information on the user to use for file
**			    permissions.
**		params -- Params specific to the database being opened.
**			 Only supports some DB hash options right now
**			 (see smdb_db_open() for details).
**
**	Returns:
**		SMDBE_OK -- Success.
**		Anything else is an error. Look up more info about the
**		error in the comments for the specific open() used.
*/

int
smdb_open_database(database, db_name, mode, mode_mask, sff, type, user_info,
		   params)
	SMDB_DATABASE **database;
	char *db_name;
	int mode;
	int mode_mask;
	long sff;
	SMDB_DBTYPE type;
	SMDB_USER_INFO *user_info;
	SMDB_DBPARAMS *params;
{
#if defined(NEWDB) && defined(NDBM)
	bool type_was_default = false;
#endif

	if (type == SMDB_TYPE_DEFAULT)
	{
#ifdef NEWDB
# ifdef NDBM
		type_was_default = true;
# endif
		type = SMDB_TYPE_HASH;
#else /* NEWDB */
# ifdef NDBM
		type = SMDB_TYPE_NDBM;
# endif /* NDBM */
#endif /* NEWDB */
	}

	if (type == SMDB_TYPE_DEFAULT)
		return SMDBE_UNKNOWN_DB_TYPE;

	if ((strncmp(type, SMDB_TYPE_HASH, SMDB_TYPE_HASH_LEN) == 0) ||
	    (strncmp(type, SMDB_TYPE_BTREE, SMDB_TYPE_BTREE_LEN) == 0))
	{
#ifdef NEWDB
		int result;

		result = smdb_db_open(database, db_name, mode, mode_mask, sff,
				      type, user_info, params);
# ifdef NDBM
		if (result == ENOENT && type_was_default)
			type = SMDB_TYPE_NDBM;
		else
# endif /* NDBM */
			return result;
#else /* NEWDB */
		return SMDBE_UNSUPPORTED_DB_TYPE;
#endif /* NEWDB */
	}

	if (strncmp(type, SMDB_TYPE_NDBM, SMDB_TYPE_NDBM_LEN) == 0)
	{
#ifdef NDBM
		int result;

		result = smdb_ndbm_open(database, db_name, mode, mode_mask,
					sff, type, user_info, params);
		return result;
#else /* NDBM */
		return SMDBE_UNSUPPORTED_DB_TYPE;
#endif /* NDBM */
	}

	return SMDBE_UNKNOWN_DB_TYPE;
}
/*
** SMDB_ADD_EXTENSION -- Adds an extension to a file name.
**
**	Just adds a . followed by a string to a db_name if there
**	is room and the db_name does not already have that extension.
**
**	Parameters:
**		full_name -- The final file name.
**		max_full_name_len -- The max length for full_name.
**		db_name -- The name of the db.
**		extension -- The extension to add.
**
**	Returns:
**		SMDBE_OK -- Success.
**		Anything else is an error. Look up more info about the
**		error in the comments for the specific open() used.
*/

int
smdb_add_extension(full_name, max_full_name_len, db_name, extension)
	char *full_name;
	int max_full_name_len;
	char *db_name;
	char *extension;
{
	int extension_len;
	int db_name_len;

	if (full_name == NULL || db_name == NULL || extension == NULL)
		return SMDBE_INVALID_PARAMETER;

	extension_len = strlen(extension);
	db_name_len = strlen(db_name);

	if (extension_len + db_name_len + 2 > max_full_name_len)
		return SMDBE_DB_NAME_TOO_LONG;

	if (db_name_len < extension_len + 1 ||
	    db_name[db_name_len - extension_len - 1] != '.' ||
	    strcmp(&db_name[db_name_len - extension_len], extension) != 0)
		(void) sm_snprintf(full_name, max_full_name_len, "%s.%s",
				   db_name, extension);
	else
		(void) sm_strlcpy(full_name, db_name, max_full_name_len);

	return SMDBE_OK;
}
/*
**  SMDB_LOCK_FILE -- Locks the database file.
**
**	Locks the actual database file.
**
**	Parameters:
**		lock_fd -- The resulting descriptor for the locked file.
**		db_name -- The name of the database without extension.
**		mode -- The open mode.
**		sff -- Flags to safefile.
**		extension -- The extension for the file.
**
**	Returns:
**		SMDBE_OK -- Success, otherwise errno.
*/

int
smdb_lock_file(lock_fd, db_name, mode, sff, extension)
	int *lock_fd;
	char *db_name;
	int mode;
	long sff;
	char *extension;
{
	int result;
	char file_name[MAXPATHLEN];

	result = smdb_add_extension(file_name, sizeof file_name, db_name,
				    extension);
	if (result != SMDBE_OK)
		return result;

	*lock_fd = safeopen(file_name, mode & ~O_TRUNC, DBMMODE, sff);
	if (*lock_fd < 0)
		return errno;

	return SMDBE_OK;
}
/*
**  SMDB_UNLOCK_FILE -- Unlocks a file
**
**	Unlocks a file.
**
**	Parameters:
**		lock_fd -- The descriptor for the locked file.
**
**	Returns:
**		SMDBE_OK -- Success, otherwise errno.
*/

int
smdb_unlock_file(lock_fd)
	int lock_fd;
{
	int result;

	result = close(lock_fd);
	if (result != 0)
		return errno;

	return SMDBE_OK;
}
/*
**  SMDB_LOCK_MAP -- Locks a database.
**
**	Parameters:
**		database -- database description.
**		type -- type of the lock.  Bits can be:
**			LOCK_EX -- exclusive lock.
**			LOCK_NB -- non-blocking.
**
**	Returns:
**		SMDBE_OK -- Success, otherwise errno.
*/

int
smdb_lock_map(database, type)
	SMDB_DATABASE *database;
	int type;
{
	int fd;

	fd = database->smdb_lockfd(database);
	if (fd < 0)
		return SMDBE_NOT_FOUND;
	if (!smdb_lockfile(fd, type))
		return SMDBE_LOCK_NOT_GRANTED;
	return SMDBE_OK;
}
/*
**  SMDB_UNLOCK_MAP -- Unlocks a database
**
**	Parameters:
**		database -- database description.
**
**	Returns:
**		SMDBE_OK -- Success, otherwise errno.
*/

int
smdb_unlock_map(database)
	SMDB_DATABASE *database;
{
	int fd;

	fd = database->smdb_lockfd(database);
	if (fd < 0)
		return SMDBE_NOT_FOUND;
	if (!smdb_lockfile(fd, LOCK_UN))
		return SMDBE_LOCK_NOT_HELD;
	return SMDBE_OK;
}
/*
**  SMDB_SETUP_FILE -- Gets db file ready for use.
**
**	Makes sure permissions on file are safe and creates it if it
**	doesn't exist.
**
**	Parameters:
**		db_name -- The name of the database without extension.
**		extension -- The extension.
**		sff -- Flags to safefile.
**		mode_mask -- Mode bits that must match.
**		user_info -- Information on the user to use for file
**			    permissions.
**		stat_info -- A place to put the stat info for the file.
**	Returns:
**		SMDBE_OK -- Success, otherwise errno.
*/

int
smdb_setup_file(db_name, extension, mode_mask, sff, user_info, stat_info)
	char *db_name;
	char *extension;
	int mode_mask;
	long sff;
	SMDB_USER_INFO *user_info;
	struct stat *stat_info;
{
	int st;
	int result;
	char db_file_name[MAXPATHLEN];

	result = smdb_add_extension(db_file_name, sizeof db_file_name, db_name,
				    extension);
	if (result != SMDBE_OK)
		return result;

	st = safefile(db_file_name, user_info->smdbu_id,
		      user_info->smdbu_group_id, user_info->smdbu_name,
		      sff, mode_mask, stat_info);
	if (st != 0)
		return st;

	return SMDBE_OK;
}
/*
**  SMDB_FILECHANGED -- Checks to see if a file changed.
**
**	Compares the passed in stat_info with a current stat on
**	the passed in file descriptor. Check filechanged for
**	return values.
**
**	Parameters:
**		db_name -- The name of the database without extension.
**		extension -- The extension.
**		db_fd -- A file descriptor for the database file.
**		stat_info -- An old stat_info.
**	Returns:
**		SMDBE_OK -- Success, otherwise errno.
*/

int
smdb_filechanged(db_name, extension, db_fd, stat_info)
	char *db_name;
	char *extension;
	int db_fd;
	struct stat *stat_info;
{
	int result;
	char db_file_name[MAXPATHLEN];

	result = smdb_add_extension(db_file_name, sizeof db_file_name, db_name,
				    extension);
	if (result != SMDBE_OK)
		return result;
	return filechanged(db_file_name, db_fd, stat_info);
}
/*
** SMDB_PRINT_AVAILABLE_TYPES -- Prints the names of the available types.
**
**	Parameters:
**		None
**
**	Returns:
**		None
*/

void
smdb_print_available_types()
{
#ifdef NDBM
	printf("dbm\n");
#endif /* NDBM */
#ifdef NEWDB
	printf("hash\n");
	printf("btree\n");
#endif /* NEWDB */
}
/*
** SMDB_DB_DEFINITION -- Given a database type, return database definition
**
**	Reads though a structure making an association with the database
**	type and the required cpp define from sendmail/README.
**	List size is dynamic and must be NULL terminated.
**
**	Parameters:
**		type -- The name of the database type.
**
**	Returns:
**		definition for type, otherwise NULL.
*/

typedef struct
{
	SMDB_DBTYPE type;
	char *dbdef;
} dbtype;

static dbtype DatabaseDefs[] =
{
	{ SMDB_TYPE_HASH,	"NEWDB" },
	{ SMDB_TYPE_BTREE,	"NEWDB" },
	{ SMDB_TYPE_NDBM,	"NDBM"	},
	{ NULL,			"OOPS"	}
};

char *
smdb_db_definition(type)
	SMDB_DBTYPE type;
{
	dbtype *ptr = DatabaseDefs;

	while (ptr != NULL && ptr->type != NULL)
	{
		if (strcmp(type, ptr->type) == 0)
			return ptr->dbdef;
		ptr++;
	}
	return NULL;
}
