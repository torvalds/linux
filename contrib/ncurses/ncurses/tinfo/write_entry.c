/****************************************************************************
 * Copyright (c) 1998-2012,2013 Free Software Foundation, Inc.              *
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
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 *     and: Thomas E. Dickey                        1996-on                 *
 ****************************************************************************/

/*
 *	write_entry.c -- write a terminfo structure onto the file system
 */

#include <curses.priv.h>
#include <hashed_db.h>

#include <tic.h>

#if 1
#define TRACE_OUT(p) DEBUG(2, p)
#else
#define TRACE_OUT(p)		/*nothing */
#endif

MODULE_ID("$Id: write_entry.c,v 1.91 2013/12/14 21:29:42 tom Exp $")

static int total_written;

static int make_db_root(const char *);
static int write_object(TERMTYPE *, char *, unsigned *, unsigned);

#if !USE_HASHED_DB
static void
write_file(char *filename, TERMTYPE *tp)
{
    char buffer[MAX_ENTRY_SIZE];
    unsigned limit = sizeof(buffer);
    unsigned offset = 0;

    FILE *fp = (_nc_access(filename, W_OK) == 0) ? fopen(filename, "wb") : 0;
    if (fp == 0) {
	perror(filename);
	_nc_syserr_abort("can't open %s/%s", _nc_tic_dir(0), filename);
    }
    DEBUG(1, ("Created %s", filename));

    if (write_object(tp, buffer, &offset, limit) == ERR
	|| fwrite(buffer, sizeof(char), (size_t) offset, fp) != offset) {
	_nc_syserr_abort("error writing %s/%s", _nc_tic_dir(0), filename);
    }

    fclose(fp);
}

/*
 * Check for access rights to destination directories
 * Create any directories which don't exist.
 *
 * Note:  there's no reason to return the result of make_db_root(), since
 * this function is called only in instances where that has to succeed.
 */
static void
check_writeable(int code)
{
    static const char dirnames[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    static bool verified[sizeof(dirnames)];

    char dir[sizeof(LEAF_FMT)];
    char *s = 0;

    if (code == 0 || (s = (strchr) (dirnames, code)) == 0)
	_nc_err_abort("Illegal terminfo subdirectory \"" LEAF_FMT "\"", code);

    if (verified[s - dirnames])
	return;

    _nc_SPRINTF(dir, _nc_SLIMIT(sizeof(dir)) LEAF_FMT, code);
    if (make_db_root(dir) < 0) {
	_nc_err_abort("%s/%s: permission denied", _nc_tic_dir(0), dir);
    }

    verified[s - dirnames] = TRUE;
}
#endif /* !USE_HASHED_DB */

static int
make_db_path(char *dst, const char *src, size_t limit)
{
    int rc = -1;
    const char *top = _nc_tic_dir(0);

    if (src == top || _nc_is_abs_path(src)) {
	if (strlen(src) + 1 <= limit) {
	    _nc_STRCPY(dst, src, limit);
	    rc = 0;
	}
    } else {
	if (strlen(top) + strlen(src) + 2 <= limit) {
	    _nc_SPRINTF(dst, _nc_SLIMIT(limit) "%s/%s", top, src);
	    rc = 0;
	}
    }
#if USE_HASHED_DB
    if (rc == 0) {
	static const char suffix[] = DBM_SUFFIX;
	size_t have = strlen(dst);
	size_t need = strlen(suffix);
	if (have > need && strcmp(dst + (int) (have - need), suffix)) {
	    if (have + need <= limit) {
		_nc_STRCAT(dst, suffix, limit);
	    } else {
		rc = -1;
	    }
	} else if (_nc_is_dir_path(dst)) {
	    rc = -1;
	}
    }
#endif
    return rc;
}

/*
 * Make a database-root if it doesn't exist.
 */
static int
make_db_root(const char *path)
{
    int rc;
    char fullpath[PATH_MAX];

    if ((rc = make_db_path(fullpath, path, sizeof(fullpath))) == 0) {
#if USE_HASHED_DB
	DB *capdbp;

	if ((capdbp = _nc_db_open(fullpath, TRUE)) == NULL) {
	    rc = -1;
	} else if (_nc_db_close(capdbp) < 0) {
	    rc = -1;
	}
#else
	struct stat statbuf;

	if ((rc = stat(path, &statbuf)) < 0) {
	    rc = mkdir(path
#if !defined(__MINGW32__)
		       ,0777
#endif
		);
	} else if (_nc_access(path, R_OK | W_OK | X_OK) < 0) {
	    rc = -1;		/* permission denied */
	} else if (!(S_ISDIR(statbuf.st_mode))) {
	    rc = -1;		/* not a directory */
	}
#endif
    }
    return rc;
}

/*
 * Set the write directory for compiled entries.
 */
NCURSES_EXPORT(void)
_nc_set_writedir(char *dir)
{
    const char *destination;
    char actual[PATH_MAX];

    if (dir == 0
#ifndef USE_ROOT_ENVIRON
	&& use_terminfo_vars()
#endif
	)
	dir = getenv("TERMINFO");

    if (dir != 0)
	(void) _nc_tic_dir(dir);

    destination = _nc_tic_dir(0);
    if (make_db_root(destination) < 0) {
	char *home = _nc_home_terminfo();

	if (home != 0) {
	    destination = home;
	    if (make_db_root(destination) < 0)
		_nc_err_abort("%s: permission denied (errno %d)",
			      destination, errno);
	}
    }

    /*
     * Note: because of this code, this logic should be exercised
     * *once only* per run.
     */
#if USE_HASHED_DB
    make_db_path(actual, destination, sizeof(actual));
#else
    if (chdir(_nc_tic_dir(destination)) < 0
	|| getcwd(actual, sizeof(actual)) == 0)
	_nc_err_abort("%s: not a directory", destination);
#endif
    _nc_keep_tic_dir(strdup(actual));
}

/*
 *	Save the compiled version of a description in the filesystem.
 *
 *	make a copy of the name-list
 *	break it up into first-name and all-but-last-name
 *	creat(first-name)
 *	write object information to first-name
 *	close(first-name)
 *      for each name in all-but-last-name
 *	    link to first-name
 *
 *	Using 'time()' to obtain a reference for file timestamps is unreliable,
 *	e.g., with NFS, because the filesystem may have a different time
 *	reference.  We check for pre-existence of links by latching the first
 *	timestamp from a file that we create.
 *
 *	The _nc_warning() calls will report a correct line number only if
 *	_nc_curr_line is properly set before the write_entry() call.
 */

NCURSES_EXPORT(void)
_nc_write_entry(TERMTYPE *const tp)
{
#if USE_HASHED_DB

    char buffer[MAX_ENTRY_SIZE + 1];
    unsigned limit = sizeof(buffer);
    unsigned offset = 0;

#else /* !USE_HASHED_DB */

    struct stat statbuf;
    char filename[PATH_MAX];
    char linkname[PATH_MAX];
#if USE_SYMLINKS
    char symlinkname[PATH_MAX];
#if !HAVE_LINK
#undef HAVE_LINK
#define HAVE_LINK 1
#endif
#endif /* USE_SYMLINKS */

    static int call_count;
    static time_t start_time;	/* time at start of writes */

#endif /* USE_HASHED_DB */

    char name_list[MAX_TERMINFO_LENGTH];
    char *first_name, *other_names;
    char *ptr;
    char *term_names = tp->term_names;
    size_t name_size = strlen(term_names);

    if (name_size == 0) {
	_nc_syserr_abort("no terminal name found.");
    } else if (name_size >= sizeof(name_list) - 1) {
	_nc_syserr_abort("terminal name too long: %s", term_names);
    }

    _nc_STRCPY(name_list, term_names, sizeof(name_list));
    DEBUG(7, ("Name list = '%s'", name_list));

    first_name = name_list;

    ptr = &name_list[name_size - 1];
    other_names = ptr + 1;

    while (ptr > name_list && *ptr != '|')
	ptr--;

    if (ptr != name_list) {
	*ptr = '\0';

	for (ptr = name_list; *ptr != '\0' && *ptr != '|'; ptr++)
	    continue;

	if (*ptr == '\0')
	    other_names = ptr;
	else {
	    *ptr = '\0';
	    other_names = ptr + 1;
	}
    }

    DEBUG(7, ("First name = '%s'", first_name));
    DEBUG(7, ("Other names = '%s'", other_names));

    _nc_set_type(first_name);

#if USE_HASHED_DB
    if (write_object(tp, buffer + 1, &offset, limit - 1) != ERR) {
	DB *capdb = _nc_db_open(_nc_tic_dir(0), TRUE);
	DBT key, data;

	if (capdb != 0) {
	    buffer[0] = 0;

	    memset(&key, 0, sizeof(key));
	    key.data = term_names;
	    key.size = name_size;

	    memset(&data, 0, sizeof(data));
	    data.data = buffer;
	    data.size = offset + 1;

	    _nc_db_put(capdb, &key, &data);

	    buffer[0] = 2;

	    key.data = name_list;
	    key.size = strlen(name_list);

	    _nc_STRCPY(buffer + 1,
		       term_names,
		       sizeof(buffer) - 1);
	    data.size = name_size + 1;

	    _nc_db_put(capdb, &key, &data);

	    while (*other_names != '\0') {
		ptr = other_names++;
		assert(ptr < buffer + sizeof(buffer) - 1);
		while (*other_names != '|' && *other_names != '\0')
		    other_names++;

		if (*other_names != '\0')
		    *(other_names++) = '\0';

		key.data = ptr;
		key.size = strlen(ptr);

		_nc_db_put(capdb, &key, &data);
	    }
	}
    }
#else /* !USE_HASHED_DB */
    if (call_count++ == 0) {
	start_time = 0;
    }

    if (strlen(first_name) >= sizeof(filename) - (2 + LEAF_LEN))
	_nc_warning("terminal name too long.");

    _nc_SPRINTF(filename, _nc_SLIMIT(sizeof(filename))
		LEAF_FMT "/%s", first_name[0], first_name);

    /*
     * Has this primary name been written since the first call to
     * write_entry()?  If so, the newer write will step on the older,
     * so warn the user.
     */
    if (start_time > 0 &&
	stat(filename, &statbuf) >= 0
	&& statbuf.st_mtime >= start_time) {
#if HAVE_LINK && !USE_SYMLINKS
	/*
	 * If the file has more than one link, the reason for the previous
	 * write could be that the current primary name used to be an alias for
	 * the previous entry.  In that case, unlink the file so that we will
	 * not modify the previous entry as we write this one.
	 */
	if (statbuf.st_nlink > 1) {
	    _nc_warning("name redefined.");
	    unlink(filename);
	} else {
	    _nc_warning("name multiply defined.");
	}
#else
	_nc_warning("name multiply defined.");
#endif
    }

    check_writeable(first_name[0]);
    write_file(filename, tp);

    if (start_time == 0) {
	if (stat(filename, &statbuf) < 0
	    || (start_time = statbuf.st_mtime) == 0) {
	    _nc_syserr_abort("error obtaining time from %s/%s",
			     _nc_tic_dir(0), filename);
	}
    }
    while (*other_names != '\0') {
	ptr = other_names++;
	while (*other_names != '|' && *other_names != '\0')
	    other_names++;

	if (*other_names != '\0')
	    *(other_names++) = '\0';

	if (strlen(ptr) > sizeof(linkname) - (2 + LEAF_LEN)) {
	    _nc_warning("terminal alias %s too long.", ptr);
	    continue;
	}
	if (strchr(ptr, '/') != 0) {
	    _nc_warning("cannot link alias %s.", ptr);
	    continue;
	}

	check_writeable(ptr[0]);
	_nc_SPRINTF(linkname, _nc_SLIMIT(sizeof(linkname))
		    LEAF_FMT "/%s", ptr[0], ptr);

	if (strcmp(filename, linkname) == 0) {
	    _nc_warning("self-synonym ignored");
	} else if (stat(linkname, &statbuf) >= 0 &&
		   statbuf.st_mtime < start_time) {
	    _nc_warning("alias %s multiply defined.", ptr);
	} else if (_nc_access(linkname, W_OK) == 0)
#if HAVE_LINK
	{
	    int code;
#if USE_SYMLINKS
	    if (first_name[0] == linkname[0])
		strncpy(symlinkname, first_name, sizeof(symlinkname) - 1);
	    else {
		_nc_STRCPY(symlinkname, "../", sizeof(suymlinkname));
		strncat(symlinkname, filename, sizeof(symlinkname) - 4);
	    }
	    symlinkname[sizeof(symlinkname) - 1] = '\0';
#endif /* USE_SYMLINKS */
#if HAVE_REMOVE
	    code = remove(linkname);
#else
	    code = unlink(linkname);
#endif
	    if (code != 0 && errno == ENOENT)
		code = 0;
#if USE_SYMLINKS
	    if (symlink(symlinkname, linkname) < 0)
#else
	    if (link(filename, linkname) < 0)
#endif /* USE_SYMLINKS */
	    {
		/*
		 * If there wasn't anything there, and we cannot
		 * link to the target because it is the same as the
		 * target, then the source must be on a filesystem
		 * that uses caseless filenames, such as Win32, etc.
		 */
		if (code == 0 && errno == EEXIST)
		    _nc_warning("can't link %s to %s", filename, linkname);
		else if (code == 0 && (errno == EPERM || errno == ENOENT))
		    write_file(linkname, tp);
		else {
#if MIXEDCASE_FILENAMES
		    _nc_syserr_abort("can't link %s to %s", filename, linkname);
#else
		    _nc_warning("can't link %s to %s (errno=%d)", filename,
				linkname, errno);
#endif
		}
	    } else {
		DEBUG(1, ("Linked %s", linkname));
	    }
	}
#else /* just make copies */
	    write_file(linkname, tp);
#endif /* HAVE_LINK */
    }
#endif /* USE_HASHED_DB */
}

static size_t
fake_write(char *dst,
	   unsigned *offset,
	   size_t limit,
	   char *src,
	   size_t want,
	   size_t size)
{
    size_t have = (limit - *offset);

    want *= size;
    if (have > 0) {
	if (want > have)
	    want = have;
	memcpy(dst + *offset, src, want);
	*offset += (unsigned) want;
    } else {
	want = 0;
    }
    return (want / size);
}

#define Write(buf, size, count) fake_write(buffer, offset, (size_t) limit, (char *) buf, (size_t) count, (size_t) size)

#undef LITTLE_ENDIAN		/* BSD/OS defines this as a feature macro */
#define HI(x)			((x) / 256)
#define LO(x)			((x) % 256)
#define LITTLE_ENDIAN(p, x)	(p)[0] = (unsigned char)LO(x),  \
                                (p)[1] = (unsigned char)HI(x)

#define WRITE_STRING(str) (Write(str, sizeof(char), strlen(str) + 1) == strlen(str) + 1)

static int
compute_offsets(char **Strings, size_t strmax, short *offsets)
{
    int nextfree = 0;
    size_t i;

    for (i = 0; i < strmax; i++) {
	if (Strings[i] == ABSENT_STRING) {
	    offsets[i] = -1;
	} else if (Strings[i] == CANCELLED_STRING) {
	    offsets[i] = -2;
	} else {
	    offsets[i] = (short) nextfree;
	    nextfree += (int) strlen(Strings[i]) + 1;
	    TRACE_OUT(("put Strings[%d]=%s(%d)", (int) i,
		       _nc_visbuf(Strings[i]), (int) nextfree));
	}
    }
    return nextfree;
}

static void
convert_shorts(unsigned char *buf, short *Numbers, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++) {
	if (Numbers[i] == ABSENT_NUMERIC) {	/* HI/LO won't work */
	    buf[2 * i] = buf[2 * i + 1] = 0377;
	} else if (Numbers[i] == CANCELLED_NUMERIC) {	/* HI/LO won't work */
	    buf[2 * i] = 0376;
	    buf[2 * i + 1] = 0377;
	} else {
	    LITTLE_ENDIAN(buf + 2 * i, Numbers[i]);
	    TRACE_OUT(("put Numbers[%u]=%d", (unsigned) i, Numbers[i]));
	}
    }
}

#define even_boundary(value) \
	    ((value) % 2 != 0 && Write(&zero, sizeof(char), 1) != 1)

#if NCURSES_XNAMES
static unsigned
extended_Booleans(TERMTYPE *tp)
{
    unsigned result = 0;
    unsigned i;

    for (i = 0; i < tp->ext_Booleans; ++i) {
	if (tp->Booleans[BOOLCOUNT + i] == TRUE)
	    result = (i + 1);
    }
    return result;
}

static unsigned
extended_Numbers(TERMTYPE *tp)
{
    unsigned result = 0;
    unsigned i;

    for (i = 0; i < tp->ext_Numbers; ++i) {
	if (tp->Numbers[NUMCOUNT + i] != ABSENT_NUMERIC)
	    result = (i + 1);
    }
    return result;
}

static unsigned
extended_Strings(TERMTYPE *tp)
{
    unsigned short result = 0;
    unsigned short i;

    for (i = 0; i < tp->ext_Strings; ++i) {
	if (tp->Strings[STRCOUNT + i] != ABSENT_STRING)
	    result = (unsigned short) (i + 1);
    }
    return result;
}

/*
 * _nc_align_termtype() will extend entries that are referenced in a use=
 * clause - discard the unneeded data.
 */
static bool
extended_object(TERMTYPE *tp)
{
    bool result = FALSE;

    if (_nc_user_definable) {
	result = ((extended_Booleans(tp)
		   + extended_Numbers(tp)
		   + extended_Strings(tp)) != 0);
    }
    return result;
}
#endif

static int
write_object(TERMTYPE *tp, char *buffer, unsigned *offset, unsigned limit)
{
    char *namelist;
    size_t namelen, boolmax, nummax, strmax;
    char zero = '\0';
    size_t i;
    int nextfree;
    short offsets[MAX_ENTRY_SIZE / 2];
    unsigned char buf[MAX_ENTRY_SIZE];
    unsigned last_bool = BOOLWRITE;
    unsigned last_num = NUMWRITE;
    unsigned last_str = STRWRITE;

#if NCURSES_XNAMES
    /*
     * Normally we limit the list of values to exclude the "obsolete"
     * capabilities.  However, if we are accepting extended names, add
     * these as well, since they are used for supporting translation
     * to/from termcap.
     */
    if (_nc_user_definable) {
	last_bool = BOOLCOUNT;
	last_num = NUMCOUNT;
	last_str = STRCOUNT;
    }
#endif

    namelist = tp->term_names;
    namelen = strlen(namelist) + 1;

    boolmax = 0;
    for (i = 0; i < last_bool; i++) {
	if (tp->Booleans[i] == TRUE)
	    boolmax = i + 1;
    }

    nummax = 0;
    for (i = 0; i < last_num; i++) {
	if (tp->Numbers[i] != ABSENT_NUMERIC)
	    nummax = i + 1;
    }

    strmax = 0;
    for (i = 0; i < last_str; i++) {
	if (tp->Strings[i] != ABSENT_STRING)
	    strmax = i + 1;
    }

    nextfree = compute_offsets(tp->Strings, strmax, offsets);

    /* fill in the header */
    LITTLE_ENDIAN(buf, MAGIC);
    LITTLE_ENDIAN(buf + 2, min(namelen, MAX_NAME_SIZE + 1));
    LITTLE_ENDIAN(buf + 4, boolmax);
    LITTLE_ENDIAN(buf + 6, nummax);
    LITTLE_ENDIAN(buf + 8, strmax);
    LITTLE_ENDIAN(buf + 10, nextfree);

    /* write out the header */
    TRACE_OUT(("Header of %s @%d", namelist, *offset));
    if (Write(buf, 12, 1) != 1
	|| Write(namelist, sizeof(char), namelen) != namelen)
	  return (ERR);

    for (i = 0; i < boolmax; i++)
	if (tp->Booleans[i] == TRUE)
	    buf[i] = TRUE;
	else
	    buf[i] = FALSE;
    if (Write(buf, sizeof(char), boolmax) != boolmax)
	  return (ERR);

    if (even_boundary(namelen + boolmax))
	return (ERR);

    TRACE_OUT(("Numerics begin at %04x", *offset));

    /* the numerics */
    convert_shorts(buf, tp->Numbers, nummax);
    if (Write(buf, 2, nummax) != nummax)
	return (ERR);

    TRACE_OUT(("String offsets begin at %04x", *offset));

    /* the string offsets */
    convert_shorts(buf, offsets, strmax);
    if (Write(buf, 2, strmax) != strmax)
	return (ERR);

    TRACE_OUT(("String table begins at %04x", *offset));

    /* the strings */
    for (i = 0; i < strmax; i++)
	if (VALID_STRING(tp->Strings[i]))
	    if (!WRITE_STRING(tp->Strings[i]))
		return (ERR);

#if NCURSES_XNAMES
    if (extended_object(tp)) {
	unsigned extcnt = (unsigned) NUM_EXT_NAMES(tp);

	if (even_boundary(nextfree))
	    return (ERR);

	nextfree = compute_offsets(tp->Strings + STRCOUNT,
				   (size_t) tp->ext_Strings,
				   offsets);
	TRACE_OUT(("after extended string capabilities, nextfree=%d", nextfree));

	if (tp->ext_Strings >= SIZEOF(offsets))
	    return (ERR);

	nextfree += compute_offsets(tp->ext_Names,
				    (size_t) extcnt,
				    offsets + tp->ext_Strings);
	TRACE_OUT(("after extended capnames, nextfree=%d", nextfree));
	strmax = tp->ext_Strings + extcnt;

	/*
	 * Write the extended header
	 */
	LITTLE_ENDIAN(buf + 0, tp->ext_Booleans);
	LITTLE_ENDIAN(buf + 2, tp->ext_Numbers);
	LITTLE_ENDIAN(buf + 4, tp->ext_Strings);
	LITTLE_ENDIAN(buf + 6, strmax);
	LITTLE_ENDIAN(buf + 8, nextfree);
	TRACE_OUT(("WRITE extended-header @%d", *offset));
	if (Write(buf, 10, 1) != 1)
	    return (ERR);

	TRACE_OUT(("WRITE %d booleans @%d", tp->ext_Booleans, *offset));
	if (tp->ext_Booleans
	    && Write(tp->Booleans + BOOLCOUNT, sizeof(char),
		     tp->ext_Booleans) != tp->ext_Booleans)
	      return (ERR);

	if (even_boundary(tp->ext_Booleans))
	    return (ERR);

	TRACE_OUT(("WRITE %d numbers @%d", tp->ext_Numbers, *offset));
	if (tp->ext_Numbers) {
	    convert_shorts(buf, tp->Numbers + NUMCOUNT, (size_t) tp->ext_Numbers);
	    if (Write(buf, 2, tp->ext_Numbers) != tp->ext_Numbers)
		return (ERR);
	}

	/*
	 * Convert the offsets for the ext_Strings and ext_Names tables,
	 * in that order.
	 */
	convert_shorts(buf, offsets, strmax);
	TRACE_OUT(("WRITE offsets @%d", *offset));
	if (Write(buf, 2, strmax) != strmax)
	    return (ERR);

	/*
	 * Write the string table after the offset tables so we do not
	 * have to do anything about alignment.
	 */
	for (i = 0; i < tp->ext_Strings; i++) {
	    if (VALID_STRING(tp->Strings[i + STRCOUNT])) {
		TRACE_OUT(("WRITE ext_Strings[%d]=%s", (int) i,
			   _nc_visbuf(tp->Strings[i + STRCOUNT])));
		if (!WRITE_STRING(tp->Strings[i + STRCOUNT]))
		    return (ERR);
	    }
	}

	/*
	 * Write the extended names
	 */
	for (i = 0; i < extcnt; i++) {
	    TRACE_OUT(("WRITE ext_Names[%d]=%s", (int) i, tp->ext_Names[i]));
	    if (!WRITE_STRING(tp->ext_Names[i]))
		return (ERR);
	}

    }
#endif /* NCURSES_XNAMES */

    total_written++;
    return (OK);
}

/*
 * Returns the total number of entries written by this process
 */
NCURSES_EXPORT(int)
_nc_tic_written(void)
{
    return total_written;
}
