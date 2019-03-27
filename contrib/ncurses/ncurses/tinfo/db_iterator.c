/****************************************************************************
 * Copyright (c) 2006-2012,2013 Free Software Foundation, Inc.              *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Thomas E. Dickey                                                *
 ****************************************************************************/

/*
 * Iterators for terminal databases.
 */

#include <curses.priv.h>

#include <time.h>
#include <tic.h>

#if USE_HASHED_DB
#include <hashed_db.h>
#endif

MODULE_ID("$Id: db_iterator.c,v 1.38 2013/12/14 21:23:20 tom Exp $")

#define HaveTicDirectory _nc_globals.have_tic_directory
#define KeepTicDirectory _nc_globals.keep_tic_directory
#define TicDirectory     _nc_globals.tic_directory
#define my_blob          _nc_globals.dbd_blob
#define my_list          _nc_globals.dbd_list
#define my_size          _nc_globals.dbd_size
#define my_time          _nc_globals.dbd_time
#define my_vars          _nc_globals.dbd_vars

static void
add_to_blob(const char *text, size_t limit)
{
    (void) limit;

    if (*text != '\0') {
	char *last = my_blob + strlen(my_blob);
	if (last != my_blob)
	    *last++ = NCURSES_PATHSEP;
	_nc_STRCPY(last, text, limit);
    }
}

static bool
check_existence(const char *name, struct stat *sb)
{
    bool result = FALSE;

    if (stat(name, sb) == 0
	&& (S_ISDIR(sb->st_mode) || S_ISREG(sb->st_mode))) {
	result = TRUE;
    }
#if USE_HASHED_DB
    else if (strlen(name) < PATH_MAX - sizeof(DBM_SUFFIX)) {
	char temp[PATH_MAX];
	_nc_SPRINTF(temp, _nc_SLIMIT(sizeof(temp)) "%s%s", name, DBM_SUFFIX);
	if (stat(temp, sb) == 0 && S_ISREG(sb->st_mode)) {
	    result = TRUE;
	}
    }
#endif
    return result;
}

/*
 * Store the latest value of an environment variable in my_vars[] so we can
 * detect if one changes, invalidating the cached search-list.
 */
static bool
update_getenv(const char *name, DBDIRS which)
{
    bool result = FALSE;

    if (which < dbdLAST) {
	char *value;

	if ((value = getenv(name)) == 0 || (value = strdup(value)) == 0) {
	    ;
	} else if (my_vars[which].name == 0 || strcmp(my_vars[which].name, name)) {
	    FreeIfNeeded(my_vars[which].value);
	    my_vars[which].name = name;
	    my_vars[which].value = value;
	    result = TRUE;
	} else if ((my_vars[which].value != 0) ^ (value != 0)) {
	    FreeIfNeeded(my_vars[which].value);
	    my_vars[which].value = value;
	    result = TRUE;
	} else if (value != 0 && strcmp(value, my_vars[which].value)) {
	    FreeIfNeeded(my_vars[which].value);
	    my_vars[which].value = value;
	    result = TRUE;
	} else {
	    free(value);
	}
    }
    return result;
}

static char *
cache_getenv(const char *name, DBDIRS which)
{
    char *result = 0;

    (void) update_getenv(name, which);
    if (which < dbdLAST) {
	result = my_vars[which].value;
    }
    return result;
}

/*
 * The cache expires if at least a second has passed since the initial lookup,
 * or if one of the environment variables changed.
 *
 * Only a few applications use multiple lookups of terminal entries, seems that
 * aside from bulk I/O such as tic and toe, that leaves interactive programs
 * which should not be modifying the terminal databases in a way that would
 * invalidate the search-list.
 *
 * The "1-second" is to allow for user-directed changes outside the program.
 */
static bool
cache_expired(void)
{
    bool result = FALSE;
    time_t now = time((time_t *) 0);

    if (now > my_time) {
	result = TRUE;
    } else {
	DBDIRS n;
	for (n = (DBDIRS) 0; n < dbdLAST; ++n) {
	    if (my_vars[n].name != 0
		&& update_getenv(my_vars[n].name, n)) {
		result = TRUE;
		break;
	    }
	}
    }
    return result;
}

static void
free_cache(void)
{
    FreeAndNull(my_blob);
    FreeAndNull(my_list);
}

/*
 * Record the "official" location of the terminfo directory, according to
 * the place where we're writing to, or the normal default, if not.
 */
NCURSES_EXPORT(const char *)
_nc_tic_dir(const char *path)
{
    T(("_nc_tic_dir %s", NonNull(path)));
    if (!KeepTicDirectory) {
	if (path != 0) {
	    TicDirectory = path;
	    HaveTicDirectory = TRUE;
	} else if (HaveTicDirectory == 0) {
	    if (use_terminfo_vars()) {
		char *envp;
		if ((envp = getenv("TERMINFO")) != 0)
		    return _nc_tic_dir(envp);
	    }
	}
    }
    return TicDirectory ? TicDirectory : TERMINFO;
}

/*
 * Special fix to prevent the terminfo directory from being moved after tic
 * has chdir'd to it.  If we let it be changed, then if $TERMINFO has a
 * relative path, we'll lose track of the actual directory.
 */
NCURSES_EXPORT(void)
_nc_keep_tic_dir(const char *path)
{
    _nc_tic_dir(path);
    KeepTicDirectory = TRUE;
}

/*
 * Cleanup.
 */
NCURSES_EXPORT(void)
_nc_last_db(void)
{
    if (my_blob != 0 && cache_expired()) {
	free_cache();
    }
}

/*
 * This is a simple iterator which allows the caller to step through the
 * possible locations for a terminfo directory.  ncurses uses this to find
 * terminfo files to read.
 */
NCURSES_EXPORT(const char *)
_nc_next_db(DBDIRS * state, int *offset)
{
    const char *result;

    (void) offset;
    if ((int) *state < my_size
	&& my_list != 0
	&& my_list[*state] != 0) {
	result = my_list[*state];
	(*state)++;
    } else {
	result = 0;
    }
    if (result != 0) {
	T(("_nc_next_db %d %s", *state, result));
    }
    return result;
}

NCURSES_EXPORT(void)
_nc_first_db(DBDIRS * state, int *offset)
{
    bool cache_has_expired = FALSE;
    *state = dbdTIC;
    *offset = 0;

    T(("_nc_first_db"));

    /* build a blob containing all of the strings we will use for a lookup
     * table.
     */
    if (my_blob == 0 || (cache_has_expired = cache_expired())) {
	size_t blobsize = 0;
	const char *values[dbdLAST];
	struct stat *my_stat;
	int j, k;

	if (cache_has_expired)
	    free_cache();

	for (j = 0; j < dbdLAST; ++j)
	    values[j] = 0;

	/*
	 * This is the first item in the list, and is used only when tic is
	 * writing to the database, as a performance improvement.
	 */
	values[dbdTIC] = TicDirectory;

#if NCURSES_USE_DATABASE
#ifdef TERMINFO_DIRS
	values[dbdCfgList] = TERMINFO_DIRS;
#endif
#ifdef TERMINFO
	values[dbdCfgOnce] = TERMINFO;
#endif
#endif

#if NCURSES_USE_TERMCAP
	values[dbdCfgList2] = TERMPATH;
#endif

	if (use_terminfo_vars()) {
#if NCURSES_USE_DATABASE
	    values[dbdEnvOnce] = cache_getenv("TERMINFO", dbdEnvOnce);
	    values[dbdHome] = _nc_home_terminfo();
	    (void) cache_getenv("HOME", dbdHome);
	    values[dbdEnvList] = cache_getenv("TERMINFO_DIRS", dbdEnvList);

#endif
#if NCURSES_USE_TERMCAP
	    values[dbdEnvOnce2] = cache_getenv("TERMCAP", dbdEnvOnce2);
	    /* only use $TERMCAP if it is an absolute path */
	    if (values[dbdEnvOnce2] != 0
		&& *values[dbdEnvOnce2] != '/') {
		values[dbdEnvOnce2] = 0;
	    }
	    values[dbdEnvList2] = cache_getenv("TERMPATH", dbdEnvList2);
#endif /* NCURSES_USE_TERMCAP */
	}

	for (j = 0; j < dbdLAST; ++j) {
	    if (values[j] == 0)
		values[j] = "";
	    blobsize += 2 + strlen(values[j]);
	}

	my_blob = malloc(blobsize);
	if (my_blob != 0) {
	    *my_blob = '\0';
	    for (j = 0; j < dbdLAST; ++j) {
		add_to_blob(values[j], blobsize);
	    }

	    /* Now, build an array which will be pointers to the distinct
	     * strings in the blob.
	     */
	    blobsize = 2;
	    for (j = 0; my_blob[j] != '\0'; ++j) {
		if (my_blob[j] == NCURSES_PATHSEP)
		    ++blobsize;
	    }
	    my_list = typeCalloc(char *, blobsize);
	    my_stat = typeCalloc(struct stat, blobsize);
	    if (my_list != 0 && my_stat != 0) {
		k = 0;
		my_list[k++] = my_blob;
		for (j = 0; my_blob[j] != '\0'; ++j) {
		    if (my_blob[j] == NCURSES_PATHSEP) {
			my_blob[j] = '\0';
			my_list[k++] = &my_blob[j + 1];
		    }
		}

		/*
		 * Eliminate duplicates from the list.
		 */
		for (j = 0; my_list[j] != 0; ++j) {
#ifdef TERMINFO
		    if (*my_list[j] == '\0')
			my_list[j] = strdup(TERMINFO);
#endif
		    for (k = 0; k < j; ++k) {
			if (!strcmp(my_list[j], my_list[k])) {
			    k = j - 1;
			    while ((my_list[j] = my_list[j + 1]) != 0) {
				++j;
			    }
			    j = k;
			    break;
			}
		    }
		}

		/*
		 * Eliminate non-existent databases, and those that happen to
		 * be symlinked to another location.
		 */
		for (j = 0; my_list[j] != 0; ++j) {
		    bool found = check_existence(my_list[j], &my_stat[j]);
#if HAVE_LINK
		    if (found) {
			for (k = 0; k < j; ++k) {
			    if (my_stat[j].st_dev == my_stat[k].st_dev
				&& my_stat[j].st_ino == my_stat[k].st_ino) {
				found = FALSE;
				break;
			    }
			}
		    }
#endif
		    if (!found) {
			k = j;
			while ((my_list[k] = my_list[k + 1]) != 0) {
			    ++k;
			}
			--j;
		    }
		}
		my_size = j;
		my_time = time((time_t *) 0);
	    } else {
		FreeAndNull(my_blob);
	    }
	    free(my_stat);
	}
    }
}

#if NO_LEAKS
void
_nc_db_iterator_leaks(void)
{
    DBDIRS which;

    if (my_blob != 0)
	FreeAndNull(my_blob);
    if (my_list != 0)
	FreeAndNull(my_list);
    for (which = 0; (int) which < dbdLAST; ++which) {
	my_vars[which].name = 0;
	FreeIfNeeded(my_vars[which].value);
	my_vars[which].value = 0;
    }
}
#endif
