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
 *	read_entry.c -- Routine for reading in a compiled terminfo file
 */

#include <curses.priv.h>
#include <hashed_db.h>

#include <tic.h>

MODULE_ID("$Id: read_entry.c,v 1.126 2013/12/15 00:35:36 tom Exp $")

#define TYPE_CALLOC(type,elts) typeCalloc(type, (unsigned)(elts))

#if NCURSES_USE_DATABASE
static void
convert_shorts(char *buf, short *Numbers, int count)
{
    int i;
    for (i = 0; i < count; i++) {
	if (IS_NEG1(buf + 2 * i))
	    Numbers[i] = ABSENT_NUMERIC;
	else if (IS_NEG2(buf + 2 * i))
	    Numbers[i] = CANCELLED_NUMERIC;
	else
	    Numbers[i] = (short) LOW_MSB(buf + 2 * i);
	TR(TRACE_DATABASE, ("get Numbers[%d]=%d", i, Numbers[i]));
    }
}

static void
convert_strings(char *buf, char **Strings, int count, int size, char *table)
{
    int i;
    char *p;

    for (i = 0; i < count; i++) {
	if (IS_NEG1(buf + 2 * i)) {
	    Strings[i] = ABSENT_STRING;
	} else if (IS_NEG2(buf + 2 * i)) {
	    Strings[i] = CANCELLED_STRING;
	} else if ((int) LOW_MSB(buf + 2 * i) > size) {
	    Strings[i] = ABSENT_STRING;
	} else {
	    Strings[i] = (LOW_MSB(buf + 2 * i) + table);
	    TR(TRACE_DATABASE, ("Strings[%d] = %s", i, _nc_visbuf(Strings[i])));
	}

	/* make sure all strings are NUL terminated */
	if (VALID_STRING(Strings[i])) {
	    for (p = Strings[i]; p <= table + size; p++)
		if (*p == '\0')
		    break;
	    /* if there is no NUL, ignore the string */
	    if (p > table + size)
		Strings[i] = ABSENT_STRING;
	}
    }
}

static int
fake_read(char *src, int *offset, int limit, char *dst, unsigned want)
{
    int have = (limit - *offset);

    if (have > 0) {
	if ((int) want > have)
	    want = (unsigned) have;
	memcpy(dst, src + *offset, (size_t) want);
	*offset += (int) want;
    } else {
	want = 0;
    }
    return (int) want;
}

#define Read(buf, count) fake_read(buffer, &offset, limit, (char *) buf, (unsigned) count)

#define read_shorts(buf, count) \
	(Read(buf, (count)*2) == (int) (count)*2)

#define even_boundary(value) \
    if ((value) % 2 != 0) Read(buf, 1)
#endif

NCURSES_EXPORT(void)
_nc_init_termtype(TERMTYPE *const tp)
{
    unsigned i;

#if NCURSES_XNAMES
    tp->num_Booleans = BOOLCOUNT;
    tp->num_Numbers = NUMCOUNT;
    tp->num_Strings = STRCOUNT;
    tp->ext_Booleans = 0;
    tp->ext_Numbers = 0;
    tp->ext_Strings = 0;
#endif
    if (tp->Booleans == 0)
	TYPE_MALLOC(NCURSES_SBOOL, BOOLCOUNT, tp->Booleans);
    if (tp->Numbers == 0)
	TYPE_MALLOC(short, NUMCOUNT, tp->Numbers);
    if (tp->Strings == 0)
	TYPE_MALLOC(char *, STRCOUNT, tp->Strings);

    for_each_boolean(i, tp)
	tp->Booleans[i] = FALSE;

    for_each_number(i, tp)
	tp->Numbers[i] = ABSENT_NUMERIC;

    for_each_string(i, tp)
	tp->Strings[i] = ABSENT_STRING;
}

#if NCURSES_USE_DATABASE
/*
 * Return TGETENT_YES if read, TGETENT_NO if not found or garbled.
 */
NCURSES_EXPORT(int)
_nc_read_termtype(TERMTYPE *ptr, char *buffer, int limit)
{
    int offset = 0;
    int name_size, bool_count, num_count, str_count, str_size;
    int i;
    char buf[MAX_ENTRY_SIZE + 2];
    char *string_table;
    unsigned want, have;

    TR(TRACE_DATABASE, ("READ termtype header @%d", offset));

    memset(ptr, 0, sizeof(*ptr));

    /* grab the header */
    if (!read_shorts(buf, 6)
	|| !IS_TIC_MAGIC(buf)) {
	return (TGETENT_NO);
    }

    name_size = LOW_MSB(buf + 2);
    bool_count = LOW_MSB(buf + 4);
    num_count = LOW_MSB(buf + 6);
    str_count = LOW_MSB(buf + 8);
    str_size = LOW_MSB(buf + 10);

    TR(TRACE_DATABASE,
       ("TERMTYPE name_size=%d, bool=%d/%d, num=%d/%d str=%d/%d(%d)",
	name_size, bool_count, BOOLCOUNT, num_count, NUMCOUNT,
	str_count, STRCOUNT, str_size));
    if (name_size < 0
	|| bool_count < 0
	|| num_count < 0
	|| str_count < 0
	|| str_size < 0) {
	return (TGETENT_NO);
    }

    want = (unsigned) (str_size + name_size + 1);
    if (str_size) {
	/* try to allocate space for the string table */
	if (str_count * 2 >= MAX_ENTRY_SIZE
	    || (string_table = typeMalloc(char, want)) == 0) {
	    return (TGETENT_NO);
	}
    } else {
	str_count = 0;
	if ((string_table = typeMalloc(char, want)) == 0) {
	    return (TGETENT_NO);
	}
    }

    /* grab the name (a null-terminated string) */
    want = min(MAX_NAME_SIZE, (unsigned) name_size);
    ptr->str_table = string_table;
    ptr->term_names = string_table;
    if ((have = (unsigned) Read(ptr->term_names, want)) != want) {
	memset(ptr->term_names + have, 0, (size_t) (want - have));
    }
    ptr->term_names[want] = '\0';
    string_table += (want + 1);

    if (have > MAX_NAME_SIZE)
	offset = (int) (have - MAX_NAME_SIZE);

    /* grab the booleans */
    if ((ptr->Booleans = TYPE_CALLOC(NCURSES_SBOOL,
				     max(BOOLCOUNT, bool_count))) == 0
	|| Read(ptr->Booleans, (unsigned) bool_count) < bool_count) {
	return (TGETENT_NO);
    }

    /*
     * If booleans end on an odd byte, skip it.  The machine they
     * originally wrote terminfo on must have been a 16-bit
     * word-oriented machine that would trap out if you tried a
     * word access off a 2-byte boundary.
     */
    even_boundary(name_size + bool_count);

    /* grab the numbers */
    if ((ptr->Numbers = TYPE_CALLOC(short, max(NUMCOUNT, num_count))) == 0
	|| !read_shorts(buf, num_count)) {
	return (TGETENT_NO);
    }
    convert_shorts(buf, ptr->Numbers, num_count);

    if ((ptr->Strings = TYPE_CALLOC(char *, max(STRCOUNT, str_count))) == 0)
	  return (TGETENT_NO);

    if (str_count) {
	/* grab the string offsets */
	if (!read_shorts(buf, str_count)) {
	    return (TGETENT_NO);
	}
	/* finally, grab the string table itself */
	if (Read(string_table, (unsigned) str_size) != str_size)
	    return (TGETENT_NO);
	convert_strings(buf, ptr->Strings, str_count, str_size, string_table);
    }
#if NCURSES_XNAMES

    ptr->num_Booleans = BOOLCOUNT;
    ptr->num_Numbers = NUMCOUNT;
    ptr->num_Strings = STRCOUNT;

    /*
     * Read extended entries, if any, after the normal end of terminfo data.
     */
    even_boundary(str_size);
    TR(TRACE_DATABASE, ("READ extended_header @%d", offset));
    if (_nc_user_definable && read_shorts(buf, 5)) {
	int ext_bool_count = LOW_MSB(buf + 0);
	int ext_num_count = LOW_MSB(buf + 2);
	int ext_str_count = LOW_MSB(buf + 4);
	int ext_str_size = LOW_MSB(buf + 6);
	int ext_str_limit = LOW_MSB(buf + 8);
	unsigned need = (unsigned) (ext_bool_count + ext_num_count + ext_str_count);
	int base = 0;

	if (need >= (MAX_ENTRY_SIZE / 2)
	    || ext_str_size >= MAX_ENTRY_SIZE
	    || ext_str_limit >= MAX_ENTRY_SIZE
	    || ext_bool_count < 0
	    || ext_num_count < 0
	    || ext_str_count < 0
	    || ext_str_size < 0
	    || ext_str_limit < 0)
	    return (TGETENT_NO);

	ptr->num_Booleans = UShort(BOOLCOUNT + ext_bool_count);
	ptr->num_Numbers = UShort(NUMCOUNT + ext_num_count);
	ptr->num_Strings = UShort(STRCOUNT + ext_str_count);

	TYPE_REALLOC(NCURSES_SBOOL, ptr->num_Booleans, ptr->Booleans);
	TYPE_REALLOC(short, ptr->num_Numbers, ptr->Numbers);
	TYPE_REALLOC(char *, ptr->num_Strings, ptr->Strings);

	TR(TRACE_DATABASE, ("extended header is %d/%d/%d(%d:%d)",
			    ext_bool_count, ext_num_count, ext_str_count,
			    ext_str_size, ext_str_limit));

	TR(TRACE_DATABASE, ("READ %d extended-booleans @%d",
			    ext_bool_count, offset));
	if ((ptr->ext_Booleans = UShort(ext_bool_count)) != 0) {
	    if (Read(ptr->Booleans + BOOLCOUNT, (unsigned)
		     ext_bool_count) != ext_bool_count)
		return (TGETENT_NO);
	}
	even_boundary(ext_bool_count);

	TR(TRACE_DATABASE, ("READ %d extended-numbers @%d",
			    ext_num_count, offset));
	if ((ptr->ext_Numbers = UShort(ext_num_count)) != 0) {
	    if (!read_shorts(buf, ext_num_count))
		return (TGETENT_NO);
	    TR(TRACE_DATABASE, ("Before converting extended-numbers"));
	    convert_shorts(buf, ptr->Numbers + NUMCOUNT, ext_num_count);
	}

	TR(TRACE_DATABASE, ("READ extended-offsets @%d", offset));
	if ((unsigned) (ext_str_count + (int) need) >= (MAX_ENTRY_SIZE / 2))
	    return (TGETENT_NO);
	if ((ext_str_count || need)
	    && !read_shorts(buf, ext_str_count + (int) need))
	    return (TGETENT_NO);

	TR(TRACE_DATABASE, ("READ %d bytes of extended-strings @%d",
			    ext_str_limit, offset));

	if (ext_str_limit) {
	    ptr->ext_str_table = typeMalloc(char, (size_t) ext_str_limit);
	    if (ptr->ext_str_table == 0)
		return (TGETENT_NO);
	    if (Read(ptr->ext_str_table, (unsigned) ext_str_limit) != ext_str_limit)
		return (TGETENT_NO);
	    TR(TRACE_DATABASE, ("first extended-string is %s", _nc_visbuf(ptr->ext_str_table)));
	}

	if ((ptr->ext_Strings = UShort(ext_str_count)) != 0) {
	    TR(TRACE_DATABASE,
	       ("Before computing extended-string capabilities str_count=%d, ext_str_count=%d",
		str_count, ext_str_count));
	    convert_strings(buf, ptr->Strings + str_count, ext_str_count,
			    ext_str_limit, ptr->ext_str_table);
	    for (i = ext_str_count - 1; i >= 0; i--) {
		TR(TRACE_DATABASE, ("MOVE from [%d:%d] %s",
				    i, i + str_count,
				    _nc_visbuf(ptr->Strings[i + str_count])));
		ptr->Strings[i + STRCOUNT] = ptr->Strings[i + str_count];
		if (VALID_STRING(ptr->Strings[i + STRCOUNT]))
		    base += (int) (strlen(ptr->Strings[i + STRCOUNT]) + 1);
		TR(TRACE_DATABASE, ("... to    [%d] %s",
				    i + STRCOUNT,
				    _nc_visbuf(ptr->Strings[i + STRCOUNT])));
	    }
	}

	if (need) {
	    if (ext_str_count >= (MAX_ENTRY_SIZE / 2))
		return (TGETENT_NO);
	    if ((ptr->ext_Names = TYPE_CALLOC(char *, need)) == 0)
		  return (TGETENT_NO);
	    TR(TRACE_DATABASE,
	       ("ext_NAMES starting @%d in extended_strings, first = %s",
		base, _nc_visbuf(ptr->ext_str_table + base)));
	    convert_strings(buf + (2 * ext_str_count),
			    ptr->ext_Names,
			    (int) need,
			    ext_str_limit, ptr->ext_str_table + base);
	}

	TR(TRACE_DATABASE,
	   ("...done reading terminfo bool %d(%d) num %d(%d) str %d(%d)",
	    ptr->num_Booleans, ptr->ext_Booleans,
	    ptr->num_Numbers, ptr->ext_Numbers,
	    ptr->num_Strings, ptr->ext_Strings));

	TR(TRACE_DATABASE, ("extend: num_Booleans:%d", ptr->num_Booleans));
    } else
#endif /* NCURSES_XNAMES */
    {
	TR(TRACE_DATABASE, ("...done reading terminfo bool %d num %d str %d",
			    bool_count, num_count, str_count));
#if NCURSES_XNAMES
	TR(TRACE_DATABASE, ("normal: num_Booleans:%d", ptr->num_Booleans));
#endif
    }

    for (i = bool_count; i < BOOLCOUNT; i++)
	ptr->Booleans[i] = FALSE;
    for (i = num_count; i < NUMCOUNT; i++)
	ptr->Numbers[i] = ABSENT_NUMERIC;
    for (i = str_count; i < STRCOUNT; i++)
	ptr->Strings[i] = ABSENT_STRING;

    return (TGETENT_YES);
}

/*
 *	int
 *	_nc_read_file_entry(filename, ptr)
 *
 *	Read the compiled terminfo entry in the given file into the
 *	structure pointed to by ptr, allocating space for the string
 *	table.
 */
NCURSES_EXPORT(int)
_nc_read_file_entry(const char *const filename, TERMTYPE *ptr)
/* return 1 if read, 0 if not found or garbled */
{
    FILE *fp = 0;
    int code;
    int limit;
    char buffer[MAX_ENTRY_SIZE + 1];

    if (_nc_access(filename, R_OK) < 0
	|| (fp = fopen(filename, "rb")) == 0) {
	TR(TRACE_DATABASE, ("cannot open terminfo %s (errno=%d)", filename, errno));
	code = TGETENT_NO;
    } else {
	if ((limit = (int) fread(buffer, sizeof(char), sizeof(buffer), fp))
	    > 0) {

	    TR(TRACE_DATABASE, ("read terminfo %s", filename));
	    if ((code = _nc_read_termtype(ptr, buffer, limit)) == TGETENT_NO) {
		_nc_free_termtype(ptr);
	    }
	} else {
	    code = TGETENT_NO;
	}
	fclose(fp);
    }

    return (code);
}

#if USE_HASHED_DB
/*
 * Return if if we can build the filename of a ".db" file.
 */
static bool
make_db_filename(char *filename, unsigned limit, const char *const path)
{
    static const char suffix[] = DBM_SUFFIX;

    size_t lens = sizeof(suffix) - 1;
    size_t size = strlen(path);
    size_t test = lens + size;
    bool result = FALSE;

    if (test < limit) {
	if (size >= lens
	    && !strcmp(path + size - lens, suffix))
	    _nc_STRCPY(filename, path, limit);
	else
	    _nc_SPRINTF(filename, _nc_SLIMIT(limit) "%s%s", path, suffix);
	result = TRUE;
    }
    return result;
}
#endif

/*
 * Return true if we can build the name of a filesystem entry.
 */
static bool
make_dir_filename(char *filename,
		  unsigned limit,
		  const char *const path,
		  const char *name)
{
    bool result = FALSE;

#if NCURSES_USE_TERMCAP
    if (_nc_is_dir_path(path))
#endif
    {
	unsigned need = (unsigned) (LEAF_LEN + 3 + strlen(path) + strlen(name));

	if (need <= limit) {
	    _nc_SPRINTF(filename, _nc_SLIMIT(limit)
			"%s/" LEAF_FMT "/%s", path, *name, name);
	    result = TRUE;
	}
    }
    return result;
}

/*
 * Build a terminfo pathname and try to read the data.  Returns TGETENT_YES on
 * success, TGETENT_NO on failure.
 */
static int
_nc_read_tic_entry(char *filename,
		   unsigned limit,
		   const char *const path,
		   const char *name,
		   TERMTYPE *const tp)
{
    int code = TGETENT_NO;

#if USE_HASHED_DB
    DB *capdbp;

    if (make_db_filename(filename, limit, path)
	&& (capdbp = _nc_db_open(filename, FALSE)) != 0) {

	DBT key, data;
	int reccnt = 0;
	char *save = strdup(name);

	memset(&key, 0, sizeof(key));
	key.data = save;
	key.size = strlen(save);

	/*
	 * This lookup could return termcap data, which we do not want.  We are
	 * looking for compiled (binary) terminfo data.
	 *
	 * cgetent uses a two-level lookup.  On the first it uses the given
	 * name to return a record containing only the aliases for an entry. 
	 * On the second (using that list of aliases as a key), it returns the
	 * content of the terminal description.  We expect second lookup to
	 * return data beginning with the same set of aliases.
	 *
	 * For compiled terminfo, the list of aliases in the second case will
	 * be null-terminated.  A termcap entry will not be, and will run on
	 * into the description.  So we can easily distinguish between the two
	 * (source/binary) by checking the lengths.
	 */
	while (_nc_db_get(capdbp, &key, &data) == 0) {
	    int used = (int) data.size - 1;
	    char *have = (char *) data.data;

	    if (*have++ == 0) {
		if (data.size > key.size
		    && IS_TIC_MAGIC(have)) {
		    code = _nc_read_termtype(tp, have, used);
		    if (code == TGETENT_NO) {
			_nc_free_termtype(tp);
		    }
		}
		break;
	    }

	    /*
	     * Just in case we have a corrupt database, do not waste time with
	     * it.
	     */
	    if (++reccnt >= 3)
		break;

	    /*
	     * Prepare for the second level.
	     */
	    key.data = have;
	    key.size = used;
	}

	free(save);
    } else			/* may be either filesystem or flat file */
#endif
    if (make_dir_filename(filename, limit, path, name)) {
	code = _nc_read_file_entry(filename, tp);
    }
#if NCURSES_USE_TERMCAP
    else if (code != TGETENT_YES) {
	code = _nc_read_termcap_entry(name, tp);
	_nc_SPRINTF(filename, _nc_SLIMIT(PATH_MAX)
		    "%.*s", PATH_MAX - 1, _nc_get_source());
    }
#endif
    return code;
}
#endif /* NCURSES_USE_DATABASE */

/*
 *	_nc_read_entry(char *name, char *filename, TERMTYPE *tp)
 *
 *	Find and read the compiled entry for a given terminal type,
 *	if it exists.  We take pains here to make sure no combination
 *	of environment variables and terminal type name can be used to
 *	overrun the file buffer.
 */

NCURSES_EXPORT(int)
_nc_read_entry(const char *const name, char *const filename, TERMTYPE *const tp)
{
    int code = TGETENT_NO;

    _nc_SPRINTF(filename, _nc_SLIMIT(PATH_MAX)
		"%.*s", PATH_MAX - 1, name);

    if (strlen(name) == 0
	|| strcmp(name, ".") == 0
	|| strcmp(name, "..") == 0
	|| _nc_pathlast(name) != 0
	|| strchr(name, NCURSES_PATHSEP) != 0) {
	TR(TRACE_DATABASE, ("illegal or missing entry name '%s'", name));
    } else {
#if NCURSES_USE_DATABASE
	DBDIRS state;
	int offset;
	const char *path;

	_nc_first_db(&state, &offset);
	while ((path = _nc_next_db(&state, &offset)) != 0) {
	    TR(TRACE_DATABASE, ("_nc_read_tic_entry path=%s, name=%s", path, name));
	    code = _nc_read_tic_entry(filename, PATH_MAX, path, name, tp);
	    if (code == TGETENT_YES) {
		_nc_last_db();
		break;
	    }
	}
#elif NCURSES_USE_TERMCAP
	if (code != TGETENT_YES) {
	    code = _nc_read_termcap_entry(name, tp);
	    _nc_SPRINTF(filename, _nc_SLIMIT(PATH_MAX)
			"%.*s", PATH_MAX - 1, _nc_get_source());
	}
#endif
    }
    return code;
}
