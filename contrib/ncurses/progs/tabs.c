/****************************************************************************
 * Copyright (c) 2008-2012,2013 Free Software Foundation, Inc.              *
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
 *  Author: Thomas E. Dickey                        2008                    *
 ****************************************************************************/

/*
 * tabs.c --  set terminal hard-tabstops
 */

#define USE_LIBTINFO
#include <progs.priv.h>

MODULE_ID("$Id: tabs.c,v 1.34 2013/06/11 08:18:27 tom Exp $")

static void usage(void) GCC_NORETURN;

static char *prg_name;
static int max_cols;

static void
failed(const char *s)
{
    perror(s);
    ExitProgram(EXIT_FAILURE);
}

static int
putch(int c)
{
    return putchar(c);
}

static void
do_tabs(int *tab_list)
{
    int last = 1;
    int stop;

    putchar('\r');
    while ((stop = *tab_list++) > 0) {
	if (last < stop) {
	    while (last++ < stop) {
		if (last > max_cols)
		    break;
		putchar(' ');
	    }
	}
	if (stop <= max_cols) {
	    tputs(tparm(set_tab, stop), 1, putch);
	    last = stop;
	} else {
	    break;
	}
    }
    putchar('\n');
}

static int *
decode_tabs(const char *tab_list)
{
    int *result = typeCalloc(int, strlen(tab_list) + (unsigned) max_cols);
    int n = 0;
    int value = 0;
    int prior = 0;
    int ch;

    if (result == 0)
	failed("decode_tabs");

    while ((ch = *tab_list++) != '\0') {
	if (isdigit(UChar(ch))) {
	    value *= 10;
	    value += (ch - '0');
	} else if (ch == ',') {
	    result[n] = value + prior;
	    if (n > 0 && result[n] <= result[n - 1]) {
		fprintf(stderr,
			"%s: tab-stops are not in increasing order: %d %d\n",
			prg_name, value, result[n - 1]);
		free(result);
		result = 0;
		break;
	    }
	    ++n;
	    value = 0;
	    prior = 0;
	} else if (ch == '+') {
	    if (n)
		prior = result[n - 1];
	}
    }

    if (result != 0) {
	/*
	 * If there is only one value, then it is an option such as "-8".
	 */
	if ((n == 0) && (value > 0)) {
	    int step = value;
	    value = 1;
	    while (n < max_cols - 1) {
		result[n++] = value;
		value += step;
	    }
	}

	/*
	 * Add the last value, if any.
	 */
	result[n++] = value + prior;
	result[n] = 0;
    }

    return result;
}

static void
print_ruler(int *tab_list)
{
    int last = 0;
    int stop;
    int n;

    /* first print a readable ruler */
    for (n = 0; n < max_cols; n += 10) {
	int ch = 1 + (n / 10);
	char buffer[20];
	_nc_SPRINTF(buffer, _nc_SLIMIT(sizeof(buffer))
		    "----+----%c",
		    ((ch < 10)
		     ? (ch + '0')
		     : (ch + 'A' - 10)));
	printf("%.*s", ((max_cols - n) > 10) ? 10 : (max_cols - n), buffer);
    }
    putchar('\n');

    /* now, print '*' for each stop */
    for (n = 0, last = 0; (tab_list[n] > 0) && (last < max_cols); ++n) {
	stop = tab_list[n];
	while (++last < stop) {
	    if (last <= max_cols) {
		putchar('-');
	    } else {
		break;
	    }
	}
	if (last <= max_cols) {
	    putchar('*');
	    last = stop;
	} else {
	    break;
	}
    }
    while (++last <= max_cols)
	putchar('-');
    putchar('\n');
}

/*
 * Write an '*' on each tabstop, to demonstrate whether it lines up with the
 * ruler.
 */
static void
write_tabs(int *tab_list)
{
    int stop;

    while ((stop = *tab_list++) > 0 && stop <= max_cols) {
	fputs((stop == 1) ? "*" : "\t*", stdout);
    };
    /* also show a tab _past_ the stops */
    if (stop < max_cols)
	fputs("\t+", stdout);
    putchar('\n');
}

/*
 * Trim leading/trailing blanks, as well as blanks after a comma.
 * Convert embedded blanks to commas.
 */
static char *
trimmed_tab_list(const char *source)
{
    char *result = strdup(source);
    int ch, j, k, last;

    if (result != 0) {
	for (j = k = last = 0; result[j] != 0; ++j) {
	    ch = UChar(result[j]);
	    if (isspace(ch)) {
		if (last == '\0') {
		    continue;
		} else if (isdigit(last) || last == ',') {
		    ch = ',';
		}
	    } else if (ch == ',') {
		;
	    } else {
		if (last == ',')
		    result[k++] = (char) last;
		result[k++] = (char) ch;
	    }
	    last = ch;
	}
	result[k] = '\0';
    }
    return result;
}

static bool
comma_is_needed(const char *source)
{
    bool result = FALSE;

    if (source != 0) {
	size_t len = strlen(source);
	if (len != 0)
	    result = (source[len - 1] != ',');
    } else {
	result = FALSE;
    }
    return result;
}

/*
 * Add a command-line parameter to the tab-list.  It can be blank- or comma-
 * separated (or a mixture).  For simplicity, empty tabs are ignored, e.g.,
 *	tabs 1,,6,11
 *	tabs 1,6,11
 * are treated the same.
 */
static const char *
add_to_tab_list(char **append, const char *value)
{
    char *result = *append;
    char *copied = trimmed_tab_list(value);

    if (copied != 0 && *copied != '\0') {
	const char *comma = ",";
	size_t need = 1 + strlen(copied);

	if (*copied == ',')
	    comma = "";
	else if (!comma_is_needed(*append))
	    comma = "";

	need += strlen(comma);
	if (*append != 0)
	    need += strlen(*append);

	result = malloc(need);
	if (result == 0)
	    failed("add_to_tab_list");

	*result = '\0';
	if (*append != 0) {
	    _nc_STRCPY(result, *append, need);
	    free(*append);
	}
	_nc_STRCAT(result, comma, need);
	_nc_STRCAT(result, copied, need);

	*append = result;
    }
    return result;
}

/*
 * Check for illegal characters in the tab-list.
 */
static bool
legal_tab_list(const char *tab_list)
{
    bool result = TRUE;

    if (tab_list != 0 && *tab_list != '\0') {
	if (comma_is_needed(tab_list)) {
	    int n, ch;
	    for (n = 0; tab_list[n] != '\0'; ++n) {
		ch = UChar(tab_list[n]);
		if (!(isdigit(ch) || ch == ',' || ch == '+')) {
		    fprintf(stderr,
			    "%s: unexpected character found '%c'\n",
			    prg_name, ch);
		    result = FALSE;
		    break;
		}
	    }
	} else {
	    fprintf(stderr, "%s: trailing comma found '%s'\n", prg_name, tab_list);
	    result = FALSE;
	}
    } else {
	fprintf(stderr, "%s: no tab-list given\n", prg_name);
	result = FALSE;
    }
    return result;
}

static char *
skip_list(char *value)
{
    while (*value != '\0' &&
	   (isdigit(UChar(*value)) ||
	    isspace(UChar(*value)) ||
	    strchr("+,", UChar(*value)) != 0)) {
	++value;
    }
    return value;
}

static void
usage(void)
{
    static const char *msg[] =
    {
	"Usage: tabs [options] [tabstop-list]"
	,""
	,"Options:"
	,"  -0       reset tabs"
	,"  -8       set tabs to standard interval"
	,"  -a       Assembler, IBM S/370, first format"
	,"  -a2      Assembler, IBM S/370, second format"
	,"  -c       COBOL, normal format"
	,"  -c2      COBOL compact format"
	,"  -c3      COBOL compact format extended"
	,"  -d       debug (show ruler with expected/actual tab positions)"
	,"  -f       FORTRAN"
	,"  -n       no-op (do not modify terminal settings)"
	,"  -p       PL/I"
	,"  -s       SNOBOL"
	,"  -u       UNIVAC 1100 Assembler"
	,"  -T name  use terminal type 'name'"
	,"  -V       print version"
	,""
	,"A tabstop-list is an ordered list of column numbers, e.g., 1,11,21"
	,"or 1,+10,+10 which is the same."
    };
    unsigned n;

    fflush(stdout);
    for (n = 0; n < SIZEOF(msg); ++n) {
	fprintf(stderr, "%s\n", msg[n]);
    }
    ExitProgram(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
    int rc = EXIT_FAILURE;
    bool debug = FALSE;
    bool no_op = FALSE;
    int n, ch;
    NCURSES_CONST char *term_name = 0;
    char *append = 0;
    const char *tab_list = 0;

    prg_name = _nc_rootname(argv[0]);

    if ((term_name = getenv("TERM")) == 0)
	term_name = "ansi+tabs";

    /* cannot use getopt, since some options are two-character */
    for (n = 1; n < argc; ++n) {
	char *option = argv[n];
	switch (option[0]) {
	case '-':
	    while ((ch = *++option) != '\0') {
		switch (ch) {
		case 'a':
		    switch (*++option) {
		    default:
		    case '\0':
			tab_list = "1,10,16,36,72";
			option--;
			/* Assembler, IBM S/370, first format */
			break;
		    case '2':
			tab_list = "1,10,16,40,72";
			/* Assembler, IBM S/370, second format */
			break;
		    }
		    break;
		case 'c':
		    switch (*++option) {
		    default:
		    case '\0':
			tab_list = "1,8,12,16,20,55";
			option--;
			/* COBOL, normal format */
			break;
		    case '2':
			tab_list = "1,6,10,14,49";
			/* COBOL compact format */
			break;
		    case '3':
			tab_list = "1,6,10,14,18,22,26,30,34,38,42,46,50,54,58,62,67";
			/* COBOL compact format extended */
			break;
		    }
		    break;
		case 'd':	/* ncurses extension */
		    debug = TRUE;
		    break;
		case 'f':
		    tab_list = "1,7,11,15,19,23";
		    /* FORTRAN */
		    break;
		case 'n':	/* ncurses extension */
		    no_op = TRUE;
		    break;
		case 'p':
		    tab_list = "1,5,9,13,17,21,25,29,33,37,41,45,49,53,57,61";
		    /* PL/I */
		    break;
		case 's':
		    tab_list = "1,10,55";
		    /* SNOBOL */
		    break;
		case 'u':
		    tab_list = "1,12,20,44";
		    /* UNIVAC 1100 Assembler */
		    break;
		case 'T':
		    ++n;
		    if (*++option != '\0') {
			term_name = option;
		    } else {
			term_name = argv[n++];
			option--;
		    }
		    option += ((int) strlen(option)) - 1;
		    continue;
		case 'V':
		    puts(curses_version());
		    ExitProgram(EXIT_SUCCESS);
		default:
		    if (isdigit(UChar(*option))) {
			char *copy = strdup(option);
			*skip_list(copy) = '\0';
			tab_list = copy;
			option = skip_list(option) - 1;
		    } else {
			usage();
		    }
		    break;
		}
	    }
	    break;
	case '+':
	    while ((ch = *++option) != '\0') {
		switch (ch) {
		case 'm':
		    /*
		     * The "+mXXX" option is unimplemented because only the long-obsolete
		     * att510d implements smgl, which is needed to support
		     * this option.
		     */
		    break;
		default:
		    /* special case of relative stops separated by spaces? */
		    if (option == argv[n] + 1) {
			tab_list = add_to_tab_list(&append, argv[n]);
		    }
		    break;
		}
	    }
	    break;
	default:
	    if (append != 0) {
		if (tab_list != (const char *) append) {
		    /* one of the predefined options was used */
		    free(append);
		    append = 0;
		}
	    }
	    tab_list = add_to_tab_list(&append, option);
	    break;
	}
    }

    setupterm(term_name, STDOUT_FILENO, (int *) 0);

    max_cols = (columns > 0) ? columns : 80;

    if (!VALID_STRING(clear_all_tabs)) {
	fprintf(stderr,
		"%s: terminal type '%s' cannot reset tabs\n",
		prg_name, term_name);
    } else if (!VALID_STRING(set_tab)) {
	fprintf(stderr,
		"%s: terminal type '%s' cannot set tabs\n",
		prg_name, term_name);
    } else if (legal_tab_list(tab_list)) {
	int *list = decode_tabs(tab_list);

	if (!no_op)
	    tputs(clear_all_tabs, 1, putch);

	if (list != 0) {
	    if (!no_op)
		do_tabs(list);
	    if (debug) {
		fflush(stderr);
		printf("tabs %s\n", tab_list);
		print_ruler(list);
		write_tabs(list);
	    }
	    free(list);
	} else if (debug) {
	    fflush(stderr);
	    printf("tabs %s\n", tab_list);
	}
	rc = EXIT_SUCCESS;
    }
    if (append != 0)
	free(append);
    ExitProgram(rc);
}
