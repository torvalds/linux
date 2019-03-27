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
 ****************************************************************************/

/*
 * tput.c -- shellscript access to terminal capabilities
 *
 * by Eric S. Raymond <esr@snark.thyrsus.com>, portions based on code from
 * Ross Ridge's mytinfo package.
 */

#define USE_LIBTINFO
#include <progs.priv.h>

#if !PURE_TERMINFO
#include <dump_entry.h>
#include <termsort.c>
#endif
#include <transform.h>

MODULE_ID("$Id: tput.c,v 1.49 2013/09/28 20:57:25 tom Exp $")

#define PUTS(s)		fputs(s, stdout)
#define PUTCHAR(c)	putchar(c)
#define FLUSH		fflush(stdout)

typedef enum {
    Numbers = 0
    ,Num_Str
    ,Num_Str_Str
} TParams;

static char *prg_name;
static bool is_init = FALSE;
static bool is_reset = FALSE;

static void
quit(int status, const char *fmt,...)
{
    va_list argp;

    va_start(argp, fmt);
    fprintf(stderr, "%s: ", prg_name);
    vfprintf(stderr, fmt, argp);
    fprintf(stderr, "\n");
    va_end(argp);
    ExitProgram(status);
}

static void
usage(void)
{
    fprintf(stderr, "usage: %s [-V] [-S] [-T term] capname\n", prg_name);
    ExitProgram(EXIT_FAILURE);
}

static void
check_aliases(const char *name)
{
    is_init = same_program(name, PROG_INIT);
    is_reset = same_program(name, PROG_RESET);
}

/*
 * Lookup the type of call we should make to tparm().  This ignores the actual
 * terminfo capability (bad, because it is not extensible), but makes this
 * code portable to platforms where sizeof(int) != sizeof(char *).
 */
static TParams
tparm_type(const char *name)
{
#define TD(code, longname, ti, tc) {code,longname},{code,ti},{code,tc}
    TParams result = Numbers;
    /* *INDENT-OFF* */
    static const struct {
	TParams code;
	const char *name;
    } table[] = {
	TD(Num_Str,	"pkey_key",	"pfkey",	"pk"),
	TD(Num_Str,	"pkey_local",	"pfloc",	"pl"),
	TD(Num_Str,	"pkey_xmit",	"pfx",		"px"),
	TD(Num_Str,	"plab_norm",	"pln",		"pn"),
	TD(Num_Str_Str, "pkey_plab",	"pfxl",		"xl"),
    };
    /* *INDENT-ON* */

    unsigned n;
    for (n = 0; n < SIZEOF(table); n++) {
	if (!strcmp(name, table[n].name)) {
	    result = table[n].code;
	    break;
	}
    }
    return result;
}

static int
exit_code(int token, int value)
{
    int result = 99;

    switch (token) {
    case BOOLEAN:
	result = !value;	/* TRUE=0, FALSE=1 */
	break;
    case NUMBER:
	result = 0;		/* always zero */
	break;
    case STRING:
	result = value;		/* 0=normal, 1=missing */
	break;
    }
    return result;
}

static int
tput(int argc, char *argv[])
{
    NCURSES_CONST char *name;
    char *s;
    int i, j, c;
    int status;
    FILE *f;
#if !PURE_TERMINFO
    bool termcap = FALSE;
#endif

    if ((name = argv[0]) == 0)
	name = "";
    check_aliases(name);
    if (is_reset || is_init) {
	if (init_prog != 0) {
	    system(init_prog);
	}
	FLUSH;

	if (is_reset && reset_1string != 0) {
	    PUTS(reset_1string);
	} else if (init_1string != 0) {
	    PUTS(init_1string);
	}
	FLUSH;

	if (is_reset && reset_2string != 0) {
	    PUTS(reset_2string);
	} else if (init_2string != 0) {
	    PUTS(init_2string);
	}
	FLUSH;

#ifdef set_lr_margin
	if (set_lr_margin != 0) {
	    PUTS(TPARM_2(set_lr_margin, 0, columns - 1));
	} else
#endif
#ifdef set_left_margin_parm
	    if (set_left_margin_parm != 0
		&& set_right_margin_parm != 0) {
	    PUTS(TPARM_1(set_left_margin_parm, 0));
	    PUTS(TPARM_1(set_right_margin_parm, columns - 1));
	} else
#endif
	    if (clear_margins != 0
		&& set_left_margin != 0
		&& set_right_margin != 0) {
	    PUTS(clear_margins);
	    if (carriage_return != 0) {
		PUTS(carriage_return);
	    } else {
		PUTCHAR('\r');
	    }
	    PUTS(set_left_margin);
	    if (parm_right_cursor) {
		PUTS(TPARM_1(parm_right_cursor, columns - 1));
	    } else {
		for (i = 0; i < columns - 1; i++) {
		    PUTCHAR(' ');
		}
	    }
	    PUTS(set_right_margin);
	    if (carriage_return != 0) {
		PUTS(carriage_return);
	    } else {
		PUTCHAR('\r');
	    }
	}
	FLUSH;

	if (init_tabs != 8) {
	    if (clear_all_tabs != 0 && set_tab != 0) {
		for (i = 0; i < columns - 1; i += 8) {
		    if (parm_right_cursor) {
			PUTS(TPARM_1(parm_right_cursor, 8));
		    } else {
			for (j = 0; j < 8; j++)
			    PUTCHAR(' ');
		    }
		    PUTS(set_tab);
		}
		FLUSH;
	    }
	}

	if (is_reset && reset_file != 0) {
	    f = fopen(reset_file, "r");
	    if (f == 0) {
		quit(4 + errno, "Can't open reset_file: '%s'", reset_file);
	    }
	    while ((c = fgetc(f)) != EOF) {
		PUTCHAR(c);
	    }
	    fclose(f);
	} else if (init_file != 0) {
	    f = fopen(init_file, "r");
	    if (f == 0) {
		quit(4 + errno, "Can't open init_file: '%s'", init_file);
	    }
	    while ((c = fgetc(f)) != EOF) {
		PUTCHAR(c);
	    }
	    fclose(f);
	}
	FLUSH;

	if (is_reset && reset_3string != 0) {
	    PUTS(reset_3string);
	} else if (init_3string != 0) {
	    PUTS(init_3string);
	}
	FLUSH;
	return 0;
    }

    if (strcmp(name, "longname") == 0) {
	PUTS(longname());
	return 0;
    }
#if !PURE_TERMINFO
  retry:
#endif
    if ((status = tigetflag(name)) != -1) {
	return exit_code(BOOLEAN, status);
    } else if ((status = tigetnum(name)) != CANCELLED_NUMERIC) {
	(void) printf("%d\n", status);
	return exit_code(NUMBER, 0);
    } else if ((s = tigetstr(name)) == CANCELLED_STRING) {
#if !PURE_TERMINFO
	if (!termcap) {
	    const struct name_table_entry *np;

	    termcap = TRUE;
	    if ((np = _nc_find_entry(name, _nc_get_hash_table(termcap))) != 0) {
		switch (np->nte_type) {
		case BOOLEAN:
		    if (bool_from_termcap[np->nte_index])
			name = boolnames[np->nte_index];
		    break;

		case NUMBER:
		    if (num_from_termcap[np->nte_index])
			name = numnames[np->nte_index];
		    break;

		case STRING:
		    if (str_from_termcap[np->nte_index])
			name = strnames[np->nte_index];
		    break;
		}
		goto retry;
	    }
	}
#endif
	quit(4, "unknown terminfo capability '%s'", name);
    } else if (s != ABSENT_STRING) {
	if (argc > 1) {
	    int k;
	    int ignored;
	    long numbers[1 + NUM_PARM];
	    char *strings[1 + NUM_PARM];
	    char *p_is_s[NUM_PARM];

	    /* Nasty hack time. The tparm function needs to see numeric
	     * parameters as numbers, not as pointers to their string
	     * representations
	     */

	    for (k = 1; k < argc; k++) {
		char *tmp = 0;
		strings[k] = argv[k];
		numbers[k] = strtol(argv[k], &tmp, 0);
		if (tmp == 0 || *tmp != 0)
		    numbers[k] = 0;
	    }
	    for (k = argc; k <= NUM_PARM; k++) {
		numbers[k] = 0;
		strings[k] = 0;
	    }

	    switch (tparm_type(name)) {
	    case Num_Str:
		s = TPARM_2(s, numbers[1], strings[2]);
		break;
	    case Num_Str_Str:
		s = TPARM_3(s, numbers[1], strings[2], strings[3]);
		break;
	    case Numbers:
	    default:
		(void) _nc_tparm_analyze(s, p_is_s, &ignored);
#define myParam(n) (p_is_s[n - 1] != 0 ? ((TPARM_ARG) strings[n]) : numbers[n])
		s = TPARM_9(s,
			    myParam(1),
			    myParam(2),
			    myParam(3),
			    myParam(4),
			    myParam(5),
			    myParam(6),
			    myParam(7),
			    myParam(8),
			    myParam(9));
		break;
	    }
	}

	/* use putp() in order to perform padding */
	putp(s);
	return exit_code(STRING, 0);
    }
    return exit_code(STRING, 1);
}

int
main(int argc, char **argv)
{
    char *term;
    int errret;
    bool cmdline = TRUE;
    int c;
    char buf[BUFSIZ];
    int result = 0;

    check_aliases(prg_name = _nc_rootname(argv[0]));

    term = getenv("TERM");

    while ((c = getopt(argc, argv, "ST:V")) != -1) {
	switch (c) {
	case 'S':
	    cmdline = FALSE;
	    break;
	case 'T':
	    use_env(FALSE);
	    term = optarg;
	    break;
	case 'V':
	    puts(curses_version());
	    ExitProgram(EXIT_SUCCESS);
	default:
	    usage();
	    /* NOTREACHED */
	}
    }

    /*
     * Modify the argument list to omit the options we processed.
     */
    if (is_reset || is_init) {
	if (optind-- < argc) {
	    argc -= optind;
	    argv += optind;
	}
	argv[0] = prg_name;
    } else {
	argc -= optind;
	argv += optind;
    }

    if (term == 0 || *term == '\0')
	quit(2, "No value for $TERM and no -T specified");

    if (setupterm(term, STDOUT_FILENO, &errret) != OK && errret <= 0)
	quit(3, "unknown terminal \"%s\"", term);

    if (cmdline) {
	if ((argc <= 0) && !is_reset && !is_init)
	    usage();
	ExitProgram(tput(argc, argv));
    }

    while (fgets(buf, sizeof(buf), stdin) != 0) {
	char *argvec[16];	/* command, 9 parms, null, & slop */
	int argnum = 0;
	char *cp;

	/* crack the argument list into a dope vector */
	for (cp = buf; *cp; cp++) {
	    if (isspace(UChar(*cp))) {
		*cp = '\0';
	    } else if (cp == buf || cp[-1] == 0) {
		argvec[argnum++] = cp;
		if (argnum >= (int) SIZEOF(argvec) - 1)
		    break;
	    }
	}
	argvec[argnum] = 0;

	if (argnum != 0
	    && tput(argnum, argvec) != 0) {
	    if (result == 0)
		result = 4;	/* will return value >4 */
	    ++result;
	}
    }

    ExitProgram(result);
}
