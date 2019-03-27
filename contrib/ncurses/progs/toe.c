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
 *	toe.c --- table of entries report generator
 */

#include <progs.priv.h>

#include <sys/stat.h>

#if USE_HASHED_DB
#include <hashed_db.h>
#endif

MODULE_ID("$Id: toe.c,v 1.74 2013/12/15 01:08:28 tom Exp $")

#define isDotname(name) (!strcmp(name, ".") || !strcmp(name, ".."))

typedef struct {
    int db_index;
    unsigned long checksum;
    char *term_name;
    char *description;
} TERMDATA;

const char *_nc_progname;

static TERMDATA *ptr_termdata;	/* array of terminal data */
static size_t use_termdata;	/* actual usage in ptr_termdata[] */
static size_t len_termdata;	/* allocated size of ptr_termdata[] */

#if NO_LEAKS
#undef ExitProgram
static void ExitProgram(int code) GCC_NORETURN;
static void
ExitProgram(int code)
{
    _nc_free_entries(_nc_head);
    _nc_free_tic(code);
}
#endif

static void failed(const char *) GCC_NORETURN;

static void
failed(const char *msg)
{
    perror(msg);
    ExitProgram(EXIT_FAILURE);
}

static char *
strmalloc(const char *value)
{
    char *result = strdup(value);
    if (result == 0) {
	failed("strmalloc");
    }
    return result;
}

static TERMDATA *
new_termdata(void)
{
    size_t want = use_termdata + 1;

    if (want >= len_termdata) {
	len_termdata = (2 * want) + 10;
	ptr_termdata = typeRealloc(TERMDATA, len_termdata, ptr_termdata);
	if (ptr_termdata == 0)
	    failed("ptr_termdata");
    }

    return ptr_termdata + use_termdata++;
}

static int
compare_termdata(const void *a, const void *b)
{
    const TERMDATA *p = (const TERMDATA *) a;
    const TERMDATA *q = (const TERMDATA *) b;
    int result = strcmp(p->term_name, q->term_name);

    if (result == 0) {
	result = (p->db_index - q->db_index);
    }
    return result;
}

/*
 * Sort the array of TERMDATA and print it.  If more than one database is being
 * reported, add a column to show which database has a given entry.
 */
static void
show_termdata(int eargc, char **eargv)
{
    int j, k;
    size_t n;

    if (use_termdata) {
	if (eargc > 1) {
	    for (j = 0; j < eargc; ++j) {
		for (k = 0; k <= j; ++k) {
		    printf("--");
		}
		printf("> ");
		printf("%s\n", eargv[j]);
	    }
	}
	if (use_termdata > 1)
	    qsort(ptr_termdata, use_termdata, sizeof(TERMDATA), compare_termdata);
	for (n = 0; n < use_termdata; ++n) {

	    /*
	     * If there is more than one database, show how they differ.
	     */
	    if (eargc > 1) {
		unsigned long check = 0;
		k = 0;
		for (;;) {
		    for (; k < ptr_termdata[n].db_index; ++k) {
			printf("--");
		    }

		    /*
		     * If this is the first entry, or its checksum differs
		     * from the first entry's checksum, print "*". Otherwise
		     * it looks enough like a duplicate to print "+".
		     */
		    printf("%c-", ((check == 0
				    || (check != ptr_termdata[n].checksum))
				   ? '*'
				   : '+'));
		    check = ptr_termdata[n].checksum;

		    ++k;
		    if ((n + 1) >= use_termdata
			|| strcmp(ptr_termdata[n].term_name,
				  ptr_termdata[n + 1].term_name)) {
			break;
		    }
		    ++n;
		}
		for (; k < eargc; ++k) {
		    printf("--");
		}
		printf(":\t");
	    }

	    (void) printf("%-10s\t%s\n",
			  ptr_termdata[n].term_name,
			  ptr_termdata[n].description);
	}
    }
}

static void
free_termdata(void)
{
    if (ptr_termdata != 0) {
	while (use_termdata != 0) {
	    --use_termdata;
	    free(ptr_termdata[use_termdata].term_name);
	    free(ptr_termdata[use_termdata].description);
	}
	free(ptr_termdata);
	ptr_termdata = 0;
    }
    use_termdata = 0;
    len_termdata = 0;
}

static char **
allocArgv(size_t count)
{
    char **result = typeCalloc(char *, count + 1);
    if (result == 0)
	failed("realloc eargv");

    assert(result != 0);
    return result;
}

static void
freeArgv(char **argv)
{
    if (argv) {
	int count = 0;
	while (argv[count]) {
	    free(argv[count++]);
	}
	free(argv);
    }
}

#if USE_HASHED_DB
static bool
make_db_name(char *dst, const char *src, unsigned limit)
{
    static const char suffix[] = DBM_SUFFIX;

    bool result = FALSE;
    size_t lens = sizeof(suffix) - 1;
    size_t size = strlen(src);
    size_t need = lens + size;

    if (need <= limit) {
	if (size >= lens
	    && !strcmp(src + size - lens, suffix)) {
	    _nc_STRCPY(dst, src, PATH_MAX);
	} else {
	    _nc_SPRINTF(dst, _nc_SLIMIT(PATH_MAX) "%s%s", src, suffix);
	}
	result = TRUE;
    }
    return result;
}
#endif

typedef void (DescHook) (int /* db_index */ ,
			 int /* db_limit */ ,
			 const char * /* term_name */ ,
			 TERMTYPE * /* term */ );

static const char *
term_description(TERMTYPE *tp)
{
    const char *desc;

    if (tp->term_names == 0
	|| (desc = strrchr(tp->term_names, '|')) == 0
	|| (*++desc == '\0')) {
	desc = "(No description)";
    }

    return desc;
}

/* display a description for the type */
static void
deschook(int db_index, int db_limit, const char *term_name, TERMTYPE *tp)
{
    (void) db_index;
    (void) db_limit;
    (void) printf("%-10s\t%s\n", term_name, term_description(tp));
}

static unsigned long
string_sum(const char *value)
{
    unsigned long result = 0;

    if ((intptr_t) value == (intptr_t) (-1)) {
	result = ~result;
    } else if (value) {
	while (*value) {
	    result += UChar(*value);
	    ++value;
	}
    }
    return result;
}

static unsigned long
checksum_of(TERMTYPE *tp)
{
    unsigned long result = string_sum(tp->term_names);
    unsigned i;

    for (i = 0; i < NUM_BOOLEANS(tp); i++) {
	result += (unsigned long) (tp->Booleans[i]);
    }
    for (i = 0; i < NUM_NUMBERS(tp); i++) {
	result += (unsigned long) (tp->Numbers[i]);
    }
    for (i = 0; i < NUM_STRINGS(tp); i++) {
	result += string_sum(tp->Strings[i]);
    }
    return result;
}

/* collect data, to sort before display */
static void
sorthook(int db_index, int db_limit, const char *term_name, TERMTYPE *tp)
{
    TERMDATA *data = new_termdata();

    data->db_index = db_index;
    data->checksum = ((db_limit > 1) ? checksum_of(tp) : 0);
    data->term_name = strmalloc(term_name);
    data->description = strmalloc(term_description(tp));
}

#if NCURSES_USE_TERMCAP
static void
show_termcap(int db_index, int db_limit, char *buffer, DescHook hook)
{
    TERMTYPE data;
    char *next = strchr(buffer, ':');
    char *last;
    char *list = buffer;

    if (next)
	*next = '\0';

    last = strrchr(buffer, '|');
    if (last)
	++last;

    memset(&data, 0, sizeof(data));
    data.term_names = strmalloc(buffer);
    while ((next = strtok(list, "|")) != 0) {
	if (next != last)
	    hook(db_index, db_limit, next, &data);
	list = 0;
    }
    free(data.term_names);
}
#endif

#if NCURSES_USE_DATABASE
static char *
copy_entryname(DIRENT * src)
{
    size_t len = NAMLEN(src);
    char *result = malloc(len + 1);
    if (result == 0)
	failed("copy entryname");
    memcpy(result, src->d_name, len);
    result[len] = '\0';

    return result;
}
#endif

static int
typelist(int eargc, char *eargv[],
	 int verbosity,
	 DescHook hook)
/* apply a function to each entry in given terminfo directories */
{
    int i;

    for (i = 0; i < eargc; i++) {
#if NCURSES_USE_DATABASE
	if (_nc_is_dir_path(eargv[i])) {
	    char *cwd_buf = 0;
	    DIR *termdir;
	    DIRENT *subdir;

	    if ((termdir = opendir(eargv[i])) == 0) {
		(void) fflush(stdout);
		(void) fprintf(stderr,
			       "%s: can't open terminfo directory %s\n",
			       _nc_progname, eargv[i]);
		continue;
	    }

	    if (verbosity)
		(void) printf("#\n#%s:\n#\n", eargv[i]);

	    while ((subdir = readdir(termdir)) != 0) {
		size_t cwd_len;
		char *name_1;
		DIR *entrydir;
		DIRENT *entry;

		name_1 = copy_entryname(subdir);
		if (isDotname(name_1)) {
		    free(name_1);
		    continue;
		}

		cwd_len = NAMLEN(subdir) + strlen(eargv[i]) + 3;
		cwd_buf = typeRealloc(char, cwd_len, cwd_buf);
		if (cwd_buf == 0)
		    failed("realloc cwd_buf");

		assert(cwd_buf != 0);

		_nc_SPRINTF(cwd_buf, _nc_SLIMIT(cwd_len)
			    "%s/%s/", eargv[i], name_1);
		free(name_1);

		if (chdir(cwd_buf) != 0)
		    continue;

		entrydir = opendir(".");
		if (entrydir == 0) {
		    perror(cwd_buf);
		    continue;
		}
		while ((entry = readdir(entrydir)) != 0) {
		    char *name_2;
		    TERMTYPE lterm;
		    char *cn;
		    int status;

		    name_2 = copy_entryname(entry);
		    if (isDotname(name_2) || !_nc_is_file_path(name_2)) {
			free(name_2);
			continue;
		    }

		    status = _nc_read_file_entry(name_2, &lterm);
		    if (status <= 0) {
			(void) fflush(stdout);
			(void) fprintf(stderr,
				       "%s: couldn't open terminfo file %s.\n",
				       _nc_progname, name_2);
			free(cwd_buf);
			free(name_2);
			closedir(entrydir);
			closedir(termdir);
			return (EXIT_FAILURE);
		    }

		    /* only visit things once, by primary name */
		    cn = _nc_first_name(lterm.term_names);
		    if (!strcmp(cn, name_2)) {
			/* apply the selected hook function */
			hook(i, eargc, cn, &lterm);
		    }
		    _nc_free_termtype(&lterm);
		    free(name_2);
		}
		closedir(entrydir);
	    }
	    closedir(termdir);
	    if (cwd_buf != 0)
		free(cwd_buf);
	    continue;
	}
#if USE_HASHED_DB
	else {
	    DB *capdbp;
	    char filename[PATH_MAX];

	    if (verbosity)
		(void) printf("#\n#%s:\n#\n", eargv[i]);

	    if (make_db_name(filename, eargv[i], sizeof(filename))) {
		if ((capdbp = _nc_db_open(filename, FALSE)) != 0) {
		    DBT key, data;
		    int code;

		    code = _nc_db_first(capdbp, &key, &data);
		    while (code == 0) {
			TERMTYPE lterm;
			int used;
			char *have;
			char *cn;

			if (_nc_db_have_data(&key, &data, &have, &used)) {
			    if (_nc_read_termtype(&lterm, have, used) > 0) {
				/* only visit things once, by primary name */
				cn = _nc_first_name(lterm.term_names);
				/* apply the selected hook function */
				hook(i, eargc, cn, &lterm);
				_nc_free_termtype(&lterm);
			    }
			}
			code = _nc_db_next(capdbp, &key, &data);
		    }

		    _nc_db_close(capdbp);
		    continue;
		}
	    }
	}
#endif
#endif
#if NCURSES_USE_TERMCAP
#if HAVE_BSD_CGETENT
	{
	    CGETENT_CONST char *db_array[2];
	    char *buffer = 0;

	    if (verbosity)
		(void) printf("#\n#%s:\n#\n", eargv[i]);

	    db_array[0] = eargv[i];
	    db_array[1] = 0;

	    if (cgetfirst(&buffer, db_array) > 0) {
		show_termcap(i, eargc, buffer, hook);
		free(buffer);
		while (cgetnext(&buffer, db_array) > 0) {
		    show_termcap(i, eargc, buffer, hook);
		    free(buffer);
		}
		cgetclose();
		continue;
	    }
	}
#else
	/* scan termcap text-file only */
	if (_nc_is_file_path(eargv[i])) {
	    char buffer[2048];
	    FILE *fp;

	    if (verbosity)
		(void) printf("#\n#%s:\n#\n", eargv[i]);

	    if ((fp = fopen(eargv[i], "r")) != 0) {
		while (fgets(buffer, sizeof(buffer), fp) != 0) {
		    if (*buffer == '#')
			continue;
		    if (isspace(*buffer))
			continue;
		    show_termcap(i, eargc, buffer, hook);
		}
		fclose(fp);
	    }
	}
#endif
#endif
    }

    if (hook == sorthook) {
	show_termdata(eargc, eargv);
	free_termdata();
    }

    return (EXIT_SUCCESS);
}

static void
usage(void)
{
    (void) fprintf(stderr, "usage: %s [-ahsuUV] [-v n] [file...]\n", _nc_progname);
    ExitProgram(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
    bool all_dirs = FALSE;
    bool direct_dependencies = FALSE;
    bool invert_dependencies = FALSE;
    bool header = FALSE;
    char *report_file = 0;
    unsigned i;
    int code;
    int this_opt, last_opt = '?';
    unsigned v_opt = 0;
    DescHook *hook = deschook;

    _nc_progname = _nc_rootname(argv[0]);

    while ((this_opt = getopt(argc, argv, "0123456789ahsu:vU:V")) != -1) {
	/* handle optional parameter */
	if (isdigit(this_opt)) {
	    switch (last_opt) {
	    case 'v':
		v_opt = (unsigned) (this_opt - '0');
		break;
	    default:
		if (isdigit(last_opt))
		    v_opt *= 10;
		else
		    v_opt = 0;
		v_opt += (unsigned) (this_opt - '0');
		last_opt = this_opt;
	    }
	    continue;
	}
	switch (this_opt) {
	case 'a':
	    all_dirs = TRUE;
	    break;
	case 'h':
	    header = TRUE;
	    break;
	case 's':
	    hook = sorthook;
	    break;
	case 'u':
	    direct_dependencies = TRUE;
	    report_file = optarg;
	    break;
	case 'v':
	    v_opt = 1;
	    break;
	case 'U':
	    invert_dependencies = TRUE;
	    report_file = optarg;
	    break;
	case 'V':
	    puts(curses_version());
	    ExitProgram(EXIT_SUCCESS);
	default:
	    usage();
	}
    }
    set_trace_level(v_opt);

    if (report_file != 0) {
	if (freopen(report_file, "r", stdin) == 0) {
	    (void) fflush(stdout);
	    fprintf(stderr, "%s: can't open %s\n", _nc_progname, report_file);
	    ExitProgram(EXIT_FAILURE);
	}

	/* parse entries out of the source file */
	_nc_set_source(report_file);
	_nc_read_entry_source(stdin, 0, FALSE, FALSE, NULLHOOK);
    }

    /* maybe we want a direct-dependency listing? */
    if (direct_dependencies) {
	ENTRY *qp;

	for_entry_list(qp) {
	    if (qp->nuses) {
		unsigned j;

		(void) printf("%s:", _nc_first_name(qp->tterm.term_names));
		for (j = 0; j < qp->nuses; j++)
		    (void) printf(" %s", qp->uses[j].name);
		putchar('\n');
	    }
	}

	ExitProgram(EXIT_SUCCESS);
    }

    /* maybe we want a reverse-dependency listing? */
    if (invert_dependencies) {
	ENTRY *qp, *rp;
	int matchcount;

	for_entry_list(qp) {
	    matchcount = 0;
	    for_entry_list(rp) {
		if (rp->nuses == 0)
		    continue;

		for (i = 0; i < rp->nuses; i++)
		    if (_nc_name_match(qp->tterm.term_names,
				       rp->uses[i].name, "|")) {
			if (matchcount++ == 0)
			    (void) printf("%s:",
					  _nc_first_name(qp->tterm.term_names));
			(void) printf(" %s",
				      _nc_first_name(rp->tterm.term_names));
		    }
	    }
	    if (matchcount)
		putchar('\n');
	}

	ExitProgram(EXIT_SUCCESS);
    }

    /*
     * If we get this far, user wants a simple terminal type listing.
     */
    if (optind < argc) {
	code = typelist(argc - optind, argv + optind, header, hook);
    } else if (all_dirs) {
	DBDIRS state;
	int offset;
	int pass;
	const char *path;
	char **eargv = 0;

	code = EXIT_FAILURE;
	for (pass = 0; pass < 2; ++pass) {
	    size_t count = 0;

	    _nc_first_db(&state, &offset);
	    while ((path = _nc_next_db(&state, &offset)) != 0) {
		if (pass) {
		    eargv[count] = strmalloc(path);
		}
		++count;
	    }
	    if (!pass) {
		eargv = allocArgv(count);
		if (eargv == 0)
		    failed("eargv");
	    } else {
		code = typelist((int) count, eargv, header, hook);
		freeArgv(eargv);
	    }
	}
    } else {
	DBDIRS state;
	int offset;
	const char *path;
	char **eargv = allocArgv((size_t) 2);
	size_t count = 0;

	if (eargv == 0)
	    failed("eargv");
	_nc_first_db(&state, &offset);
	if ((path = _nc_next_db(&state, &offset)) != 0) {
	    eargv[count++] = strmalloc(path);
	}

	code = typelist((int) count, eargv, header, hook);

	freeArgv(eargv);
    }
    _nc_last_db();

    ExitProgram(code);
}
