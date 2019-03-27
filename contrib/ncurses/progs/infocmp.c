/****************************************************************************
 * Copyright (c) 1998-2013,2014 Free Software Foundation, Inc.              *
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
 *	infocmp.c -- decompile an entry, or compare two entries
 *		written by Eric S. Raymond
 *		and Thomas E Dickey
 */

#include <progs.priv.h>

#include <dump_entry.h>

MODULE_ID("$Id: infocmp.c,v 1.129 2014/02/01 22:11:03 tom Exp $")

#define L_CURL "{"
#define R_CURL "}"

#define MAX_STRING	1024	/* maximum formatted string */

const char *_nc_progname = "infocmp";

typedef char path[PATH_MAX];

/***************************************************************************
 *
 * The following control variables, together with the contents of the
 * terminfo entries, completely determine the actions of the program.
 *
 ***************************************************************************/

static ENTRY *entries;		/* terminfo entries */
static int termcount;		/* count of terminal entries */

static bool limited = TRUE;	/* "-r" option is not set */
static bool quiet = FALSE;
static bool literal = FALSE;
static const char *bool_sep = ":";
static const char *s_absent = "NULL";
static const char *s_cancel = "NULL";
static const char *tversion;	/* terminfo version selected */
static unsigned itrace;		/* trace flag for debugging */
static int mwidth = 60;
static int mheight = 65535;
static int numbers = 0;		/* format "%'char'" to/from "%{number}" */
static int outform = F_TERMINFO;	/* output format */
static int sortmode;		/* sort_mode */

/* main comparison mode */
static int compare;
#define C_DEFAULT	0	/* don't force comparison mode */
#define C_DIFFERENCE	1	/* list differences between two terminals */
#define C_COMMON	2	/* list common capabilities */
#define C_NAND		3	/* list capabilities in neither terminal */
#define C_USEALL	4	/* generate relative use-form entry */
static bool ignorepads;		/* ignore pad prefixes when diffing */

#if NO_LEAKS

typedef struct {
    ENTRY *head;
    ENTRY *tail;
} ENTERED;

static ENTERED *entered;

#undef ExitProgram
static void ExitProgram(int code) GCC_NORETURN;
/* prototype is to get gcc to accept the noreturn attribute */
static void
ExitProgram(int code)
{
    int n;

    for (n = 0; n < termcount; ++n) {
	ENTRY *new_head = _nc_head;
	ENTRY *new_tail = _nc_tail;
	_nc_head = entered[n].head;
	_nc_tail = entered[n].tail;
	_nc_free_entries(entered[n].head);
	_nc_head = new_head;
	_nc_tail = new_tail;
    }
    _nc_leaks_dump_entry();
    free(entries);
    free(entered);
    _nc_free_tic(code);
}
#endif

static void
failed(const char *s)
{
    perror(s);
    ExitProgram(EXIT_FAILURE);
}

static char *
canonical_name(char *ptr, char *buf)
/* extract the terminal type's primary name */
{
    char *bp;

    _nc_STRCPY(buf, ptr, NAMESIZE);
    if ((bp = strchr(buf, '|')) != 0)
	*bp = '\0';

    return (buf);
}

/***************************************************************************
 *
 * Predicates for dump function
 *
 ***************************************************************************/

static int
capcmp(PredIdx idx, const char *s, const char *t)
/* capability comparison function */
{
    if (!VALID_STRING(s) && !VALID_STRING(t))
	return (s != t);
    else if (!VALID_STRING(s) || !VALID_STRING(t))
	return (1);

    if ((idx == acs_chars_index) || !ignorepads)
	return (strcmp(s, t));
    else
	return (_nc_capcmp(s, t));
}

static int
use_predicate(unsigned type, PredIdx idx)
/* predicate function to use for use decompilation */
{
    ENTRY *ep;

    switch (type) {
    case BOOLEAN:
	{
	    int is_set = FALSE;

	    /*
	     * This assumes that multiple use entries are supposed
	     * to contribute the logical or of their boolean capabilities.
	     * This is true if we take the semantics of multiple uses to
	     * be 'each capability gets the first non-default value found
	     * in the sequence of use entries'.
	     *
	     * Note that cancelled or absent booleans are stored as FALSE,
	     * unlike numbers and strings, whose cancelled/absent state is
	     * recorded in the terminfo database.
	     */
	    for (ep = &entries[1]; ep < entries + termcount; ep++)
		if (ep->tterm.Booleans[idx] == TRUE) {
		    is_set = entries[0].tterm.Booleans[idx];
		    break;
		}
	    if (is_set != entries[0].tterm.Booleans[idx])
		return (!is_set);
	    else
		return (FAIL);
	}

    case NUMBER:
	{
	    int value = ABSENT_NUMERIC;

	    /*
	     * We take the semantics of multiple uses to be 'each
	     * capability gets the first non-default value found
	     * in the sequence of use entries'.
	     */
	    for (ep = &entries[1]; ep < entries + termcount; ep++)
		if (VALID_NUMERIC(ep->tterm.Numbers[idx])) {
		    value = ep->tterm.Numbers[idx];
		    break;
		}

	    if (value != entries[0].tterm.Numbers[idx])
		return (value != ABSENT_NUMERIC);
	    else
		return (FAIL);
	}

    case STRING:
	{
	    char *termstr, *usestr = ABSENT_STRING;

	    termstr = entries[0].tterm.Strings[idx];

	    /*
	     * We take the semantics of multiple uses to be 'each
	     * capability gets the first non-default value found
	     * in the sequence of use entries'.
	     */
	    for (ep = &entries[1]; ep < entries + termcount; ep++)
		if (ep->tterm.Strings[idx]) {
		    usestr = ep->tterm.Strings[idx];
		    break;
		}

	    if (usestr == ABSENT_STRING && termstr == ABSENT_STRING)
		return (FAIL);
	    else if (!usestr || !termstr || capcmp(idx, usestr, termstr))
		return (TRUE);
	    else
		return (FAIL);
	}
    }

    return (FALSE);		/* pacify compiler */
}

static bool
useeq(ENTRY * e1, ENTRY * e2)
/* are the use references in two entries equivalent? */
{
    unsigned i, j;

    if (e1->nuses != e2->nuses)
	return (FALSE);

    /* Ugh...this is quadratic again */
    for (i = 0; i < e1->nuses; i++) {
	bool foundmatch = FALSE;

	/* search second entry for given use reference */
	for (j = 0; j < e2->nuses; j++)
	    if (!strcmp(e1->uses[i].name, e2->uses[j].name)) {
		foundmatch = TRUE;
		break;
	    }

	if (!foundmatch)
	    return (FALSE);
    }

    return (TRUE);
}

static bool
entryeq(TERMTYPE *t1, TERMTYPE *t2)
/* are two entries equivalent? */
{
    unsigned i;

    for (i = 0; i < NUM_BOOLEANS(t1); i++)
	if (t1->Booleans[i] != t2->Booleans[i])
	    return (FALSE);

    for (i = 0; i < NUM_NUMBERS(t1); i++)
	if (t1->Numbers[i] != t2->Numbers[i])
	    return (FALSE);

    for (i = 0; i < NUM_STRINGS(t1); i++)
	if (capcmp((PredIdx) i, t1->Strings[i], t2->Strings[i]))
	    return (FALSE);

    return (TRUE);
}

#define TIC_EXPAND(result) _nc_tic_expand(result, outform==F_TERMINFO, numbers)

static void
print_uses(ENTRY * ep, FILE *fp)
/* print an entry's use references */
{
    unsigned i;

    if (!ep->nuses)
	fputs("NULL", fp);
    else
	for (i = 0; i < ep->nuses; i++) {
	    fputs(ep->uses[i].name, fp);
	    if (i < ep->nuses - 1)
		fputs(" ", fp);
	}
}

static const char *
dump_boolean(int val)
/* display the value of a boolean capability */
{
    switch (val) {
    case ABSENT_BOOLEAN:
	return (s_absent);
    case CANCELLED_BOOLEAN:
	return (s_cancel);
    case FALSE:
	return ("F");
    case TRUE:
	return ("T");
    default:
	return ("?");
    }
}

static void
dump_numeric(int val, char *buf)
/* display the value of a boolean capability */
{
    switch (val) {
    case ABSENT_NUMERIC:
	_nc_STRCPY(buf, s_absent, MAX_STRING);
	break;
    case CANCELLED_NUMERIC:
	_nc_STRCPY(buf, s_cancel, MAX_STRING);
	break;
    default:
	_nc_SPRINTF(buf, _nc_SLIMIT(MAX_STRING) "%d", val);
	break;
    }
}

static void
dump_string(char *val, char *buf)
/* display the value of a string capability */
{
    if (val == ABSENT_STRING)
	_nc_STRCPY(buf, s_absent, MAX_STRING);
    else if (val == CANCELLED_STRING)
	_nc_STRCPY(buf, s_cancel, MAX_STRING);
    else {
	_nc_SPRINTF(buf, _nc_SLIMIT(MAX_STRING)
		    "'%.*s'", MAX_STRING - 3, TIC_EXPAND(val));
    }
}

/*
 * Show "comparing..." message for the given terminal names.
 */
static void
show_comparing(char **names)
{
    if (itrace) {
	switch (compare) {
	case C_DIFFERENCE:
	    (void) fprintf(stderr, "%s: dumping differences\n", _nc_progname);
	    break;

	case C_COMMON:
	    (void) fprintf(stderr, "%s: dumping common capabilities\n", _nc_progname);
	    break;

	case C_NAND:
	    (void) fprintf(stderr, "%s: dumping differences\n", _nc_progname);
	    break;
	}
    }
    if (*names) {
	printf("comparing %s", *names++);
	if (*names) {
	    printf(" to %s", *names++);
	    while (*names) {
		printf(", %s", *names++);
	    }
	}
	printf(".\n");
    }
}

/*
 * ncurses stores two types of non-standard capabilities:
 * a) capabilities listed past the "STOP-HERE" comment in the Caps file. 
 *    These are used in the terminfo source file to provide data for termcaps,
 *    e.g., when there is no equivalent capability in terminfo, as well as for
 *    widely-used non-standard capabilities.
 * b) user-definable capabilities, via "tic -x".
 *
 * However, if "-x" is omitted from the tic command, both types of
 * non-standard capability are not loaded into the terminfo database.  This
 * macro is used for limit-checks against the symbols that tic uses to omit
 * the two types of non-standard entry.
 */
#if NCURSES_XNAMES
#define check_user_definable(n,limit) if (!_nc_user_definable && (n) > (limit)) break
#else
#define check_user_definable(n,limit) if ((n) > (limit)) break
#endif

/*
 * Use these macros to simplify loops on C_COMMON and C_NAND:
 */
#define for_each_entry() while (entries[extra].tterm.term_names)
#define next_entry           (&(entries[extra++].tterm))

static void
compare_predicate(PredType type, PredIdx idx, const char *name)
/* predicate function to use for entry difference reports */
{
    ENTRY *e1 = &entries[0];
    ENTRY *e2 = &entries[1];
    char buf1[MAX_STRING];
    char buf2[MAX_STRING];
    int b1, b2;
    int n1, n2;
    char *s1, *s2;
    bool found;
    int extra = 1;

    switch (type) {
    case CMP_BOOLEAN:
	check_user_definable(idx, BOOLWRITE);
	b1 = e1->tterm.Booleans[idx];
	switch (compare) {
	case C_DIFFERENCE:
	    b2 = next_entry->Booleans[idx];
	    if (!(b1 == ABSENT_BOOLEAN && b2 == ABSENT_BOOLEAN) && b1 != b2)
		(void) printf("\t%s: %s%s%s.\n",
			      name,
			      dump_boolean(b1),
			      bool_sep,
			      dump_boolean(b2));
	    break;

	case C_COMMON:
	    if (b1 != ABSENT_BOOLEAN) {
		found = TRUE;
		for_each_entry() {
		    b2 = next_entry->Booleans[idx];
		    if (b1 != b2) {
			found = FALSE;
			break;
		    }
		}
		if (found) {
		    (void) printf("\t%s= %s.\n", name, dump_boolean(b1));
		}
	    }
	    break;

	case C_NAND:
	    if (b1 == ABSENT_BOOLEAN) {
		found = TRUE;
		for_each_entry() {
		    b2 = next_entry->Booleans[idx];
		    if (b1 != b2) {
			found = FALSE;
			break;
		    }
		}
		if (found) {
		    (void) printf("\t!%s.\n", name);
		}
	    }
	    break;
	}
	break;

    case CMP_NUMBER:
	check_user_definable(idx, NUMWRITE);
	n1 = e1->tterm.Numbers[idx];
	switch (compare) {
	case C_DIFFERENCE:
	    n2 = next_entry->Numbers[idx];
	    if (!((n1 == ABSENT_NUMERIC && n2 == ABSENT_NUMERIC)) && n1 != n2) {
		dump_numeric(n1, buf1);
		dump_numeric(n2, buf2);
		(void) printf("\t%s: %s, %s.\n", name, buf1, buf2);
	    }
	    break;

	case C_COMMON:
	    if (n1 != ABSENT_NUMERIC) {
		found = TRUE;
		for_each_entry() {
		    n2 = next_entry->Numbers[idx];
		    if (n1 != n2) {
			found = FALSE;
			break;
		    }
		}
		if (found) {
		    dump_numeric(n1, buf1);
		    (void) printf("\t%s= %s.\n", name, buf1);
		}
	    }
	    break;

	case C_NAND:
	    if (n1 == ABSENT_NUMERIC) {
		found = TRUE;
		for_each_entry() {
		    n2 = next_entry->Numbers[idx];
		    if (n1 != n2) {
			found = FALSE;
			break;
		    }
		}
		if (found) {
		    (void) printf("\t!%s.\n", name);
		}
	    }
	    break;
	}
	break;

    case CMP_STRING:
	check_user_definable(idx, STRWRITE);
	s1 = e1->tterm.Strings[idx];
	switch (compare) {
	case C_DIFFERENCE:
	    s2 = next_entry->Strings[idx];
	    if (capcmp(idx, s1, s2)) {
		dump_string(s1, buf1);
		dump_string(s2, buf2);
		if (strcmp(buf1, buf2))
		    (void) printf("\t%s: %s, %s.\n", name, buf1, buf2);
	    }
	    break;

	case C_COMMON:
	    if (s1 != ABSENT_STRING) {
		found = TRUE;
		for_each_entry() {
		    s2 = next_entry->Strings[idx];
		    if (capcmp(idx, s1, s2) != 0) {
			found = FALSE;
			break;
		    }
		}
		if (found) {
		    (void) printf("\t%s= '%s'.\n", name, TIC_EXPAND(s1));
		}
	    }
	    break;

	case C_NAND:
	    if (s1 == ABSENT_STRING) {
		found = TRUE;
		for_each_entry() {
		    s2 = next_entry->Strings[idx];
		    if (s2 != s1) {
			found = FALSE;
			break;
		    }
		}
		if (found) {
		    (void) printf("\t!%s.\n", name);
		}
	    }
	    break;
	}
	break;

    case CMP_USE:
	/* unlike the other modes, this compares *all* use entries */
	switch (compare) {
	case C_DIFFERENCE:
	    if (!useeq(e1, e2)) {
		(void) fputs("\tuse: ", stdout);
		print_uses(e1, stdout);
		fputs(", ", stdout);
		print_uses(e2, stdout);
		fputs(".\n", stdout);
	    }
	    break;

	case C_COMMON:
	    if (e1->nuses) {
		found = TRUE;
		for_each_entry() {
		    e2 = &entries[extra++];
		    if (e2->nuses != e1->nuses || !useeq(e1, e2)) {
			found = FALSE;
			break;
		    }
		}
		if (found) {
		    (void) fputs("\tuse: ", stdout);
		    print_uses(e1, stdout);
		    fputs(".\n", stdout);
		}
	    }
	    break;

	case C_NAND:
	    if (!e1->nuses) {
		found = TRUE;
		for_each_entry() {
		    e2 = &entries[extra++];
		    if (e2->nuses != e1->nuses) {
			found = FALSE;
			break;
		    }
		}
		if (found) {
		    (void) printf("\t!use.\n");
		}
	    }
	    break;
	}
    }
}

/***************************************************************************
 *
 * Init string analysis
 *
 ***************************************************************************/

typedef struct {
    const char *from;
    const char *to;
} assoc;

static const assoc std_caps[] =
{
    /* these are specified by X.364 and iBCS2 */
    {"\033c", "RIS"},		/* full reset */
    {"\0337", "SC"},		/* save cursor */
    {"\0338", "RC"},		/* restore cursor */
    {"\033[r", "RSR"},		/* not an X.364 mnemonic */
    {"\033[m", "SGR0"},		/* not an X.364 mnemonic */
    {"\033[2J", "ED2"},		/* clear page */

    /* this group is specified by ISO 2022 */
    {"\033(0", "ISO DEC G0"},	/* enable DEC graphics for G0 */
    {"\033(A", "ISO UK G0"},	/* enable UK chars for G0 */
    {"\033(B", "ISO US G0"},	/* enable US chars for G0 */
    {"\033)0", "ISO DEC G1"},	/* enable DEC graphics for G1 */
    {"\033)A", "ISO UK G1"},	/* enable UK chars for G1 */
    {"\033)B", "ISO US G1"},	/* enable US chars for G1 */

    /* these are DEC private controls widely supported by emulators */
    {"\033=", "DECPAM"},	/* application keypad mode */
    {"\033>", "DECPNM"},	/* normal keypad mode */
    {"\033<", "DECANSI"},	/* enter ANSI mode */
    {"\033[!p", "DECSTR"},	/* soft reset */
    {"\033 F", "S7C1T"},	/* 7-bit controls */

    {(char *) 0, (char *) 0}
};

static const assoc std_modes[] =
/* ECMA \E[ ... [hl] modes recognized by many emulators */
{
    {"2", "AM"},		/* keyboard action mode */
    {"4", "IRM"},		/* insert/replace mode */
    {"12", "SRM"},		/* send/receive mode */
    {"20", "LNM"},		/* linefeed mode */
    {(char *) 0, (char *) 0}
};

static const assoc private_modes[] =
/* DEC \E[ ... [hl] modes recognized by many emulators */
{
    {"1", "CKM"},		/* application cursor keys */
    {"2", "ANM"},		/* set VT52 mode */
    {"3", "COLM"},		/* 132-column mode */
    {"4", "SCLM"},		/* smooth scroll */
    {"5", "SCNM"},		/* reverse video mode */
    {"6", "OM"},		/* origin mode */
    {"7", "AWM"},		/* wraparound mode */
    {"8", "ARM"},		/* auto-repeat mode */
    {(char *) 0, (char *) 0}
};

static const assoc ecma_highlights[] =
/* recognize ECMA attribute sequences */
{
    {"0", "NORMAL"},		/* normal */
    {"1", "+BOLD"},		/* bold on */
    {"2", "+DIM"},		/* dim on */
    {"3", "+ITALIC"},		/* italic on */
    {"4", "+UNDERLINE"},	/* underline on */
    {"5", "+BLINK"},		/* blink on */
    {"6", "+FASTBLINK"},	/* fastblink on */
    {"7", "+REVERSE"},		/* reverse on */
    {"8", "+INVISIBLE"},	/* invisible on */
    {"9", "+DELETED"},		/* deleted on */
    {"10", "MAIN-FONT"},	/* select primary font */
    {"11", "ALT-FONT-1"},	/* select alternate font 1 */
    {"12", "ALT-FONT-2"},	/* select alternate font 2 */
    {"13", "ALT-FONT-3"},	/* select alternate font 3 */
    {"14", "ALT-FONT-4"},	/* select alternate font 4 */
    {"15", "ALT-FONT-5"},	/* select alternate font 5 */
    {"16", "ALT-FONT-6"},	/* select alternate font 6 */
    {"17", "ALT-FONT-7"},	/* select alternate font 7 */
    {"18", "ALT-FONT-1"},	/* select alternate font 1 */
    {"19", "ALT-FONT-1"},	/* select alternate font 1 */
    {"20", "FRAKTUR"},		/* Fraktur font */
    {"21", "DOUBLEUNDER"},	/* double underline */
    {"22", "-DIM"},		/* dim off */
    {"23", "-ITALIC"},		/* italic off */
    {"24", "-UNDERLINE"},	/* underline off */
    {"25", "-BLINK"},		/* blink off */
    {"26", "-FASTBLINK"},	/* fastblink off */
    {"27", "-REVERSE"},		/* reverse off */
    {"28", "-INVISIBLE"},	/* invisible off */
    {"29", "-DELETED"},		/* deleted off */
    {(char *) 0, (char *) 0}
};

static int
skip_csi(const char *cap)
{
    int result = 0;
    if (cap[0] == '\033' && cap[1] == '[')
	result = 2;
    else if (UChar(cap[0]) == 0233)
	result = 1;
    return result;
}

static bool
same_param(const char *table, const char *param, size_t length)
{
    bool result = FALSE;
    if (strncmp(table, param, length) == 0) {
	result = !isdigit(UChar(param[length]));
    }
    return result;
}

static char *
lookup_params(const assoc * table, char *dst, char *src)
{
    char *result = 0;
    const char *ep = strtok(src, ";");

    if (ep != 0) {
	const assoc *ap;

	do {
	    bool found = FALSE;

	    for (ap = table; ap->from; ap++) {
		size_t tlen = strlen(ap->from);

		if (same_param(ap->from, ep, tlen)) {
		    _nc_STRCAT(dst, ap->to, MAX_TERMINFO_LENGTH);
		    found = TRUE;
		    break;
		}
	    }

	    if (!found)
		_nc_STRCAT(dst, ep, MAX_TERMINFO_LENGTH);
	    _nc_STRCAT(dst, ";", MAX_TERMINFO_LENGTH);
	} while
	    ((ep = strtok((char *) 0, ";")));

	dst[strlen(dst) - 1] = '\0';

	result = dst;
    }
    return result;
}

static void
analyze_string(const char *name, const char *cap, TERMTYPE *tp)
{
    char buf2[MAX_TERMINFO_LENGTH];
    const char *sp;
    const assoc *ap;
    int tp_lines = tp->Numbers[2];

    if (!VALID_STRING(cap))
	return;
    (void) printf("%s: ", name);

    for (sp = cap; *sp; sp++) {
	int i;
	int csi;
	size_t len = 0;
	size_t next;
	const char *expansion = 0;
	char buf3[MAX_TERMINFO_LENGTH];

	/* first, check other capabilities in this entry */
	for (i = 0; i < STRCOUNT; i++) {
	    char *cp = tp->Strings[i];

	    /* don't use function-key capabilities */
	    if (strnames[i][0] == 'k' && strnames[i][1] == 'f')
		continue;

	    if (VALID_STRING(cp) &&
		cp[0] != '\0' &&
		cp != cap) {
		len = strlen(cp);
		(void) strncpy(buf2, sp, len);
		buf2[len] = '\0';

		if (_nc_capcmp(cp, buf2))
		    continue;

#define ISRS(s)	(!strncmp((s), "is", (size_t) 2) || !strncmp((s), "rs", (size_t) 2))
		/*
		 * Theoretically we just passed the test for translation
		 * (equality once the padding is stripped).  However, there
		 * are a few more hoops that need to be jumped so that
		 * identical pairs of initialization and reset strings
		 * don't just refer to each other.
		 */
		if (ISRS(name) || ISRS(strnames[i]))
		    if (cap < cp)
			continue;
#undef ISRS

		expansion = strnames[i];
		break;
	    }
	}

	/* now check the standard capabilities */
	if (!expansion) {
	    csi = skip_csi(sp);
	    for (ap = std_caps; ap->from; ap++) {
		size_t adj = (size_t) (csi ? 2 : 0);

		len = strlen(ap->from);
		if (csi && skip_csi(ap->from) != csi)
		    continue;
		if (len > adj
		    && strncmp(ap->from + adj, sp + csi, len - adj) == 0) {
		    expansion = ap->to;
		    len -= adj;
		    len += (size_t) csi;
		    break;
		}
	    }
	}

	/* now check for standard-mode sequences */
	if (!expansion
	    && (csi = skip_csi(sp)) != 0
	    && (len = (strspn) (sp + csi, "0123456789;"))
	    && (len < sizeof(buf3))
	    && (next = (size_t) csi + len)
	    && ((sp[next] == 'h') || (sp[next] == 'l'))) {

	    _nc_STRCPY(buf2,
		       ((sp[next] == 'h')
			? "ECMA+"
			: "ECMA-"),
		       sizeof(buf2));
	    (void) strncpy(buf3, sp + csi, len);
	    buf3[len] = '\0';
	    len += (size_t) csi + 1;

	    expansion = lookup_params(std_modes, buf2, buf3);
	}

	/* now check for private-mode sequences */
	if (!expansion
	    && (csi = skip_csi(sp)) != 0
	    && sp[csi] == '?'
	    && (len = (strspn) (sp + csi + 1, "0123456789;"))
	    && (len < sizeof(buf3))
	    && (next = (size_t) csi + 1 + len)
	    && ((sp[next] == 'h') || (sp[next] == 'l'))) {

	    _nc_STRCPY(buf2,
		       ((sp[next] == 'h')
			? "DEC+"
			: "DEC-"),
		       sizeof(buf2));
	    (void) strncpy(buf3, sp + csi + 1, len);
	    buf3[len] = '\0';
	    len += (size_t) csi + 2;

	    expansion = lookup_params(private_modes, buf2, buf3);
	}

	/* now check for ECMA highlight sequences */
	if (!expansion
	    && (csi = skip_csi(sp)) != 0
	    && (len = (strspn) (sp + csi, "0123456789;")) != 0
	    && (len < sizeof(buf3))
	    && (next = (size_t) csi + len)
	    && sp[next] == 'm') {

	    _nc_STRCPY(buf2, "SGR:", sizeof(buf2));
	    (void) strncpy(buf3, sp + csi, len);
	    buf3[len] = '\0';
	    len += (size_t) csi + 1;

	    expansion = lookup_params(ecma_highlights, buf2, buf3);
	}

	if (!expansion
	    && (csi = skip_csi(sp)) != 0
	    && sp[csi] == 'm') {
	    len = (size_t) csi + 1;
	    _nc_STRCPY(buf2, "SGR:", sizeof(buf2));
	    _nc_STRCAT(buf2, ecma_highlights[0].to, sizeof(buf2));
	    expansion = buf2;
	}

	/* now check for scroll region reset */
	if (!expansion
	    && (csi = skip_csi(sp)) != 0) {
	    if (sp[csi] == 'r') {
		expansion = "RSR";
		len = 1;
	    } else {
		_nc_SPRINTF(buf2, _nc_SLIMIT(sizeof(buf2)) "1;%dr", tp_lines);
		len = strlen(buf2);
		if (strncmp(buf2, sp + csi, len) == 0)
		    expansion = "RSR";
	    }
	    len += (size_t) csi;
	}

	/* now check for home-down */
	if (!expansion
	    && (csi = skip_csi(sp)) != 0) {
	    _nc_SPRINTF(buf2, _nc_SLIMIT(sizeof(buf2)) "%d;1H", tp_lines);
	    len = strlen(buf2);
	    if (strncmp(buf2, sp + csi, len) == 0) {
		expansion = "LL";
	    } else {
		_nc_SPRINTF(buf2, _nc_SLIMIT(sizeof(buf2)) "%dH", tp_lines);
		len = strlen(buf2);
		if (strncmp(buf2, sp + csi, len) == 0) {
		    expansion = "LL";
		}
	    }
	    len += (size_t) csi;
	}

	/* now look at the expansion we got, if any */
	if (expansion) {
	    printf("{%s}", expansion);
	    sp += len - 1;
	} else {
	    /* couldn't match anything */
	    buf2[0] = *sp;
	    buf2[1] = '\0';
	    fputs(TIC_EXPAND(buf2), stdout);
	}
    }
    putchar('\n');
}

/***************************************************************************
 *
 * File comparison
 *
 ***************************************************************************/

static void
file_comparison(int argc, char *argv[])
{
#define MAXCOMPARE	2
    /* someday we may allow comparisons on more files */
    int filecount = 0;
    ENTRY *heads[MAXCOMPARE];
    ENTRY *qp, *rp;
    int i, n;

    memset(heads, 0, sizeof(heads));
    dump_init((char *) 0, F_LITERAL, S_TERMINFO, 0, 65535, itrace, FALSE);

    for (n = 0; n < argc && n < MAXCOMPARE; n++) {
	if (freopen(argv[n], "r", stdin) == 0)
	    _nc_err_abort("Can't open %s", argv[n]);

#if NO_LEAKS
	entered[n].head = _nc_head;
	entered[n].tail = _nc_tail;
#endif
	_nc_head = _nc_tail = 0;

	/* parse entries out of the source file */
	_nc_set_source(argv[n]);
	_nc_read_entry_source(stdin, NULL, TRUE, literal, NULLHOOK);

	if (itrace)
	    (void) fprintf(stderr, "Resolving file %d...\n", n - 0);

	/* maybe do use resolution */
	if (!_nc_resolve_uses2(!limited, literal)) {
	    (void) fprintf(stderr,
			   "There are unresolved use entries in %s:\n",
			   argv[n]);
	    for_entry_list(qp) {
		if (qp->nuses) {
		    (void) fputs(qp->tterm.term_names, stderr);
		    (void) fputc('\n', stderr);
		}
	    }
	    ExitProgram(EXIT_FAILURE);
	}

	heads[filecount] = _nc_head;
	filecount++;
    }

    /* OK, all entries are in core.  Ready to do the comparison */
    if (itrace)
	(void) fprintf(stderr, "Entries are now in core...\n");

    /* The entry-matching loop. Sigh, this is intrinsically quadratic. */
    for (qp = heads[0]; qp; qp = qp->next) {
	for (rp = heads[1]; rp; rp = rp->next)
	    if (_nc_entry_match(qp->tterm.term_names, rp->tterm.term_names)) {
		if (qp->ncrosslinks < MAX_CROSSLINKS)
		    qp->crosslinks[qp->ncrosslinks] = rp;
		qp->ncrosslinks++;

		if (rp->ncrosslinks < MAX_CROSSLINKS)
		    rp->crosslinks[rp->ncrosslinks] = qp;
		rp->ncrosslinks++;
	    }
    }

    /* now we have two circular lists with crosslinks */
    if (itrace)
	(void) fprintf(stderr, "Name matches are done...\n");

    for (qp = heads[0]; qp; qp = qp->next) {
	if (qp->ncrosslinks > 1) {
	    (void) fprintf(stderr,
			   "%s in file 1 (%s) has %d matches in file 2 (%s):\n",
			   _nc_first_name(qp->tterm.term_names),
			   argv[0],
			   qp->ncrosslinks,
			   argv[1]);
	    for (i = 0; i < qp->ncrosslinks; i++)
		(void) fprintf(stderr,
			       "\t%s\n",
			       _nc_first_name((qp->crosslinks[i])->tterm.term_names));
	}
    }

    for (rp = heads[1]; rp; rp = rp->next) {
	if (rp->ncrosslinks > 1) {
	    (void) fprintf(stderr,
			   "%s in file 2 (%s) has %d matches in file 1 (%s):\n",
			   _nc_first_name(rp->tterm.term_names),
			   argv[1],
			   rp->ncrosslinks,
			   argv[0]);
	    for (i = 0; i < rp->ncrosslinks; i++)
		(void) fprintf(stderr,
			       "\t%s\n",
			       _nc_first_name((rp->crosslinks[i])->tterm.term_names));
	}
    }

    (void) printf("In file 1 (%s) only:\n", argv[0]);
    for (qp = heads[0]; qp; qp = qp->next)
	if (qp->ncrosslinks == 0)
	    (void) printf("\t%s\n",
			  _nc_first_name(qp->tterm.term_names));

    (void) printf("In file 2 (%s) only:\n", argv[1]);
    for (rp = heads[1]; rp; rp = rp->next)
	if (rp->ncrosslinks == 0)
	    (void) printf("\t%s\n",
			  _nc_first_name(rp->tterm.term_names));

    (void) printf("The following entries are equivalent:\n");
    for (qp = heads[0]; qp; qp = qp->next) {
	if (qp->ncrosslinks == 1) {
	    rp = qp->crosslinks[0];

	    repair_acsc(&qp->tterm);
	    repair_acsc(&rp->tterm);
#if NCURSES_XNAMES
	    _nc_align_termtype(&qp->tterm, &rp->tterm);
#endif
	    if (entryeq(&qp->tterm, &rp->tterm) && useeq(qp, rp)) {
		char name1[NAMESIZE], name2[NAMESIZE];

		(void) canonical_name(qp->tterm.term_names, name1);
		(void) canonical_name(rp->tterm.term_names, name2);

		(void) printf("%s = %s\n", name1, name2);
	    }
	}
    }

    (void) printf("Differing entries:\n");
    termcount = 2;
    for (qp = heads[0]; qp; qp = qp->next) {

	if (qp->ncrosslinks == 1) {
	    rp = qp->crosslinks[0];
#if NCURSES_XNAMES
	    /* sorry - we have to do this on each pass */
	    _nc_align_termtype(&qp->tterm, &rp->tterm);
#endif
	    if (!(entryeq(&qp->tterm, &rp->tterm) && useeq(qp, rp))) {
		char name1[NAMESIZE], name2[NAMESIZE];
		char *names[3];

		names[0] = name1;
		names[1] = name2;
		names[2] = 0;

		entries[0] = *qp;
		entries[1] = *rp;

		(void) canonical_name(qp->tterm.term_names, name1);
		(void) canonical_name(rp->tterm.term_names, name2);

		switch (compare) {
		case C_DIFFERENCE:
		    show_comparing(names);
		    compare_entry(compare_predicate, &entries->tterm, quiet);
		    break;

		case C_COMMON:
		    show_comparing(names);
		    compare_entry(compare_predicate, &entries->tterm, quiet);
		    break;

		case C_NAND:
		    show_comparing(names);
		    compare_entry(compare_predicate, &entries->tterm, quiet);
		    break;

		}
	    }
	}
    }
}

static void
usage(void)
{
    static const char *tbl[] =
    {
	"Usage: infocmp [options] [-A directory] [-B directory] [termname...]"
	,""
	,"Options:"
	,"  -0    print single-row"
	,"  -1    print single-column"
	,"  -K    use termcap-names and BSD syntax"
	,"  -C    use termcap-names"
	,"  -F    compare terminfo-files"
	,"  -I    use terminfo-names"
	,"  -L    use long names"
	,"  -R subset (see manpage)"
	,"  -T    eliminate size limits (test)"
	,"  -U    eliminate post-processing of entries"
	,"  -D    print database locations"
	,"  -V    print version"
#if NCURSES_XNAMES
	,"  -a    with -F, list commented-out caps"
#endif
	,"  -c    list common capabilities"
	,"  -d    list different capabilities"
	,"  -e    format output for C initializer"
	,"  -E    format output as C tables"
	,"  -f    with -1, format complex strings"
	,"  -G    format %{number} to %'char'"
	,"  -g    format %'char' to %{number}"
	,"  -i    analyze initialization/reset"
	,"  -l    output terminfo names"
	,"  -n    list capabilities in neither"
	,"  -p    ignore padding specifiers"
	,"  -q    brief listing, removes headers"
	,"  -r    with -C, output in termcap form"
	,"  -r    with -F, resolve use-references"
	,"  -s [d|i|l|c] sort fields"
#if NCURSES_XNAMES
	,"  -t    suppress commented-out capabilities"
#endif
	,"  -u    produce source with 'use='"
	,"  -v number  (verbose)"
	,"  -w number  (width)"
#if NCURSES_XNAMES
	,"  -x    treat unknown capabilities as user-defined"
#endif
    };
    const size_t first = 3;
    const size_t last = SIZEOF(tbl);
    const size_t left = (last - first + 1) / 2 + first;
    size_t n;

    for (n = 0; n < left; n++) {
	size_t m = (n < first) ? last : n + left - first;
	if (m < last)
	    fprintf(stderr, "%-40.40s%s\n", tbl[n], tbl[m]);
	else
	    fprintf(stderr, "%s\n", tbl[n]);
    }
    ExitProgram(EXIT_FAILURE);
}

static char *
any_initializer(const char *fmt, const char *type)
{
    static char *initializer;
    static size_t need;
    char *s;

    if (initializer == 0) {
	need = (strlen(entries->tterm.term_names)
		+ strlen(type)
		+ strlen(fmt));
	initializer = (char *) malloc(need + 1);
	if (initializer == 0)
	    failed("any_initializer");
    }

    _nc_STRCPY(initializer, entries->tterm.term_names, need);
    for (s = initializer; *s != 0 && *s != '|'; s++) {
	if (!isalnum(UChar(*s)))
	    *s = '_';
    }
    *s = 0;
    _nc_SPRINTF(s, _nc_SLIMIT(need) fmt, type);
    return initializer;
}

static char *
name_initializer(const char *type)
{
    return any_initializer("_%s_data", type);
}

static char *
string_variable(const char *type)
{
    return any_initializer("_s_%s", type);
}

/* dump C initializers for the terminal type */
static void
dump_initializers(TERMTYPE *term)
{
    unsigned n;
    const char *str = 0;

    printf("\nstatic char %s[] = \"%s\";\n\n",
	   name_initializer("alias"), entries->tterm.term_names);

    for_each_string(n, term) {
	char buf[MAX_STRING], *sp, *tp;

	if (VALID_STRING(term->Strings[n])) {
	    tp = buf;
#define TP_LIMIT	((MAX_STRING - 5) - (size_t)(tp - buf))
	    *tp++ = '"';
	    for (sp = term->Strings[n];
		 *sp != 0 && TP_LIMIT > 2;
		 sp++) {
		if (isascii(UChar(*sp))
		    && isprint(UChar(*sp))
		    && *sp != '\\'
		    && *sp != '"')
		    *tp++ = *sp;
		else {
		    _nc_SPRINTF(tp, _nc_SLIMIT(TP_LIMIT) "\\%03o", UChar(*sp));
		    tp += 4;
		}
	    }
	    *tp++ = '"';
	    *tp = '\0';
	    (void) printf("static char %-20s[] = %s;\n",
			  string_variable(ExtStrname(term, (int) n, strnames)),
			  buf);
	}
    }
    printf("\n");

    (void) printf("static char %s[] = %s\n", name_initializer("bool"), L_CURL);

    for_each_boolean(n, term) {
	switch ((int) (term->Booleans[n])) {
	case TRUE:
	    str = "TRUE";
	    break;

	case FALSE:
	    str = "FALSE";
	    break;

	case ABSENT_BOOLEAN:
	    str = "ABSENT_BOOLEAN";
	    break;

	case CANCELLED_BOOLEAN:
	    str = "CANCELLED_BOOLEAN";
	    break;
	}
	(void) printf("\t/* %3u: %-8s */\t%s,\n",
		      n, ExtBoolname(term, (int) n, boolnames), str);
    }
    (void) printf("%s;\n", R_CURL);

    (void) printf("static short %s[] = %s\n", name_initializer("number"), L_CURL);

    for_each_number(n, term) {
	char buf[BUFSIZ];
	switch (term->Numbers[n]) {
	case ABSENT_NUMERIC:
	    str = "ABSENT_NUMERIC";
	    break;
	case CANCELLED_NUMERIC:
	    str = "CANCELLED_NUMERIC";
	    break;
	default:
	    _nc_SPRINTF(buf, _nc_SLIMIT(sizeof(buf)) "%d", term->Numbers[n]);
	    str = buf;
	    break;
	}
	(void) printf("\t/* %3u: %-8s */\t%s,\n", n,
		      ExtNumname(term, (int) n, numnames), str);
    }
    (void) printf("%s;\n", R_CURL);

    (void) printf("static char * %s[] = %s\n", name_initializer("string"), L_CURL);

    for_each_string(n, term) {

	if (term->Strings[n] == ABSENT_STRING)
	    str = "ABSENT_STRING";
	else if (term->Strings[n] == CANCELLED_STRING)
	    str = "CANCELLED_STRING";
	else {
	    str = string_variable(ExtStrname(term, (int) n, strnames));
	}
	(void) printf("\t/* %3u: %-8s */\t%s,\n", n,
		      ExtStrname(term, (int) n, strnames), str);
    }
    (void) printf("%s;\n", R_CURL);

#if NCURSES_XNAMES
    if ((NUM_BOOLEANS(term) != BOOLCOUNT)
	|| (NUM_NUMBERS(term) != NUMCOUNT)
	|| (NUM_STRINGS(term) != STRCOUNT)) {
	(void) printf("static char * %s[] = %s\n",
		      name_initializer("string_ext"), L_CURL);
	for (n = BOOLCOUNT; n < NUM_BOOLEANS(term); ++n) {
	    (void) printf("\t/* %3u: bool */\t\"%s\",\n",
			  n, ExtBoolname(term, (int) n, boolnames));
	}
	for (n = NUMCOUNT; n < NUM_NUMBERS(term); ++n) {
	    (void) printf("\t/* %3u: num */\t\"%s\",\n",
			  n, ExtNumname(term, (int) n, numnames));
	}
	for (n = STRCOUNT; n < NUM_STRINGS(term); ++n) {
	    (void) printf("\t/* %3u: str */\t\"%s\",\n",
			  n, ExtStrname(term, (int) n, strnames));
	}
	(void) printf("%s;\n", R_CURL);
    }
#endif
}

/* dump C initializers for the terminal type */
static void
dump_termtype(TERMTYPE *term)
{
    (void) printf("\t%s\n\t\t%s,\n", L_CURL, name_initializer("alias"));
    (void) printf("\t\t(char *)0,\t/* pointer to string table */\n");

    (void) printf("\t\t%s,\n", name_initializer("bool"));
    (void) printf("\t\t%s,\n", name_initializer("number"));

    (void) printf("\t\t%s,\n", name_initializer("string"));

#if NCURSES_XNAMES
    (void) printf("#if NCURSES_XNAMES\n");
    (void) printf("\t\t(char *)0,\t/* pointer to extended string table */\n");
    (void) printf("\t\t%s,\t/* ...corresponding names */\n",
		  ((NUM_BOOLEANS(term) != BOOLCOUNT)
		   || (NUM_NUMBERS(term) != NUMCOUNT)
		   || (NUM_STRINGS(term) != STRCOUNT))
		  ? name_initializer("string_ext")
		  : "(char **)0");

    (void) printf("\t\t%d,\t\t/* count total Booleans */\n", NUM_BOOLEANS(term));
    (void) printf("\t\t%d,\t\t/* count total Numbers */\n", NUM_NUMBERS(term));
    (void) printf("\t\t%d,\t\t/* count total Strings */\n", NUM_STRINGS(term));

    (void) printf("\t\t%d,\t\t/* count extensions to Booleans */\n",
		  NUM_BOOLEANS(term) - BOOLCOUNT);
    (void) printf("\t\t%d,\t\t/* count extensions to Numbers */\n",
		  NUM_NUMBERS(term) - NUMCOUNT);
    (void) printf("\t\t%d,\t\t/* count extensions to Strings */\n",
		  NUM_STRINGS(term) - STRCOUNT);

    (void) printf("#endif /* NCURSES_XNAMES */\n");
#else
    (void) term;
#endif /* NCURSES_XNAMES */
    (void) printf("\t%s\n", R_CURL);
}

static int
optarg_to_number(void)
{
    char *temp = 0;
    long value = strtol(optarg, &temp, 0);

    if (temp == 0 || temp == optarg || *temp != 0) {
	fprintf(stderr, "Expected a number, not \"%s\"\n", optarg);
	ExitProgram(EXIT_FAILURE);
    }
    return (int) value;
}

static char *
terminal_env(void)
{
    char *terminal;

    if ((terminal = getenv("TERM")) == 0) {
	(void) fprintf(stderr,
		       "%s: environment variable TERM not set\n",
		       _nc_progname);
	exit(EXIT_FAILURE);
    }
    return terminal;
}

/*
 * Show the databases that infocmp knows about.  The location to which it writes is
 */
static void
show_databases(void)
{
    DBDIRS state;
    int offset;
    const char *path2;

    _nc_first_db(&state, &offset);
    while ((path2 = _nc_next_db(&state, &offset)) != 0) {
	printf("%s\n", path2);
    }
    _nc_last_db();
}

/***************************************************************************
 *
 * Main sequence
 *
 ***************************************************************************/

#if NO_LEAKS
#define MAIN_LEAKS() \
    free(myargv); \
    free(tfile); \
    free(tname)
#else
#define MAIN_LEAKS()		/* nothing */
#endif

int
main(int argc, char *argv[])
{
    /* Avoid "local data >32k" error with mwcc */
    /* Also avoid overflowing smaller stacks on systems like AmigaOS */
    path *tfile = 0;
    char **tname = 0;
    size_t maxterms;

    char **myargv;

    char *firstdir, *restdir;
    int c, i, len;
    bool formatted = FALSE;
    bool filecompare = FALSE;
    int initdump = 0;
    bool init_analyze = FALSE;
    bool suppress_untranslatable = FALSE;

    /* where is the terminfo database location going to default to? */
    restdir = firstdir = 0;

#if NCURSES_XNAMES
    use_extended_names(FALSE);
#endif
    _nc_strict_bsd = 0;

    _nc_progname = _nc_rootname(argv[0]);

    /* make sure we have enough space to add two terminal entries */
    myargv = typeCalloc(char *, (size_t) (argc + 3));
    if (myargv == 0)
	failed("myargv");

    memcpy(myargv, argv, (sizeof(char *) * (size_t) argc));
    argv = myargv;

    while ((c = getopt(argc,
		       argv,
		       "01A:aB:CcDdEeFfGgIiKLlnpqR:rs:TtUuVv:w:x")) != -1) {
	switch (c) {
	case '0':
	    mwidth = 65535;
	    mheight = 1;
	    break;

	case '1':
	    mwidth = 0;
	    break;

	case 'A':
	    firstdir = optarg;
	    break;

#if NCURSES_XNAMES
	case 'a':
	    _nc_disable_period = TRUE;
	    use_extended_names(TRUE);
	    break;
#endif
	case 'B':
	    restdir = optarg;
	    break;

	case 'K':
	    _nc_strict_bsd = 1;
	    /* FALLTHRU */
	case 'C':
	    outform = F_TERMCAP;
	    tversion = "BSD";
	    if (sortmode == S_DEFAULT)
		sortmode = S_TERMCAP;
	    break;

	case 'D':
	    show_databases();
	    ExitProgram(EXIT_SUCCESS);
	    break;

	case 'c':
	    compare = C_COMMON;
	    break;

	case 'd':
	    compare = C_DIFFERENCE;
	    break;

	case 'E':
	    initdump |= 2;
	    break;

	case 'e':
	    initdump |= 1;
	    break;

	case 'F':
	    filecompare = TRUE;
	    break;

	case 'f':
	    formatted = TRUE;
	    break;

	case 'G':
	    numbers = 1;
	    break;

	case 'g':
	    numbers = -1;
	    break;

	case 'I':
	    outform = F_TERMINFO;
	    if (sortmode == S_DEFAULT)
		sortmode = S_VARIABLE;
	    tversion = 0;
	    break;

	case 'i':
	    init_analyze = TRUE;
	    break;

	case 'L':
	    outform = F_VARIABLE;
	    if (sortmode == S_DEFAULT)
		sortmode = S_VARIABLE;
	    break;

	case 'l':
	    outform = F_TERMINFO;
	    break;

	case 'n':
	    compare = C_NAND;
	    break;

	case 'p':
	    ignorepads = TRUE;
	    break;

	case 'q':
	    quiet = TRUE;
	    s_absent = "-";
	    s_cancel = "@";
	    bool_sep = ", ";
	    break;

	case 'R':
	    tversion = optarg;
	    break;

	case 'r':
	    tversion = 0;
	    break;

	case 's':
	    if (*optarg == 'd')
		sortmode = S_NOSORT;
	    else if (*optarg == 'i')
		sortmode = S_TERMINFO;
	    else if (*optarg == 'l')
		sortmode = S_VARIABLE;
	    else if (*optarg == 'c')
		sortmode = S_TERMCAP;
	    else {
		(void) fprintf(stderr,
			       "%s: unknown sort mode\n",
			       _nc_progname);
		ExitProgram(EXIT_FAILURE);
	    }
	    break;

	case 'T':
	    limited = FALSE;
	    break;

#if NCURSES_XNAMES
	case 't':
	    _nc_disable_period = FALSE;
	    suppress_untranslatable = TRUE;
	    break;
#endif

	case 'U':
	    literal = TRUE;
	    break;

	case 'u':
	    compare = C_USEALL;
	    break;

	case 'V':
	    puts(curses_version());
	    ExitProgram(EXIT_SUCCESS);

	case 'v':
	    itrace = (unsigned) optarg_to_number();
	    set_trace_level(itrace);
	    break;

	case 'w':
	    mwidth = optarg_to_number();
	    break;

#if NCURSES_XNAMES
	case 'x':
	    use_extended_names(TRUE);
	    break;
#endif

	default:
	    usage();
	}
    }

    maxterms = (size_t) (argc + 2 - optind);
    if ((tfile = typeMalloc(path, maxterms)) == 0)
	failed("tfile");
    if ((tname = typeCalloc(char *, maxterms)) == 0)
	  failed("tname");
    if ((entries = typeCalloc(ENTRY, maxterms)) == 0)
	failed("entries");
#if NO_LEAKS
    if ((entered = typeCalloc(ENTERED, maxterms)) == 0)
	failed("entered");
#endif

    if (tfile == 0
	|| tname == 0
	|| entries == 0) {
	fprintf(stderr, "%s: not enough memory\n", _nc_progname);
	ExitProgram(EXIT_FAILURE);
    }

    /* by default, sort by terminfo name */
    if (sortmode == S_DEFAULT)
	sortmode = S_TERMINFO;

    /* make sure we have at least one terminal name to work with */
    if (optind >= argc)
	argv[argc++] = terminal_env();

    /* if user is after a comparison, make sure we have two entries */
    if (compare != C_DEFAULT && optind >= argc - 1)
	argv[argc++] = terminal_env();

    /* exactly one terminal name with no options means display it */
    /* exactly two terminal names with no options means do -d */
    if (compare == C_DEFAULT) {
	switch (argc - optind) {
	default:
	    fprintf(stderr, "%s: too many names to compare\n", _nc_progname);
	    ExitProgram(EXIT_FAILURE);
	case 1:
	    break;
	case 2:
	    compare = C_DIFFERENCE;
	    break;
	}
    }

    /* set up for display */
    dump_init(tversion, outform, sortmode, mwidth, mheight, itrace, formatted);

    if (!filecompare) {
	/* grab the entries */
	termcount = 0;
	for (; optind < argc; optind++) {
	    const char *directory = termcount ? restdir : firstdir;
	    int status;

	    tname[termcount] = argv[optind];

	    if (directory) {
#if NCURSES_USE_DATABASE
#if MIXEDCASE_FILENAMES
#define LEAF_FMT "%c"
#else
#define LEAF_FMT "%02x"
#endif
		_nc_SPRINTF(tfile[termcount],
			    _nc_SLIMIT(sizeof(path))
			    "%s/" LEAF_FMT "/%s",
			    directory,
			    UChar(*argv[optind]), argv[optind]);
		if (itrace)
		    (void) fprintf(stderr,
				   "%s: reading entry %s from file %s\n",
				   _nc_progname,
				   argv[optind], tfile[termcount]);

		status = _nc_read_file_entry(tfile[termcount],
					     &entries[termcount].tterm);
#else
		(void) fprintf(stderr, "%s: terminfo files not supported\n",
			       _nc_progname);
		MAIN_LEAKS();
		ExitProgram(EXIT_FAILURE);
#endif
	    } else {
		if (itrace)
		    (void) fprintf(stderr,
				   "%s: reading entry %s from database\n",
				   _nc_progname,
				   tname[termcount]);

		status = _nc_read_entry(tname[termcount],
					tfile[termcount],
					&entries[termcount].tterm);
	    }

	    if (status <= 0) {
		(void) fprintf(stderr,
			       "%s: couldn't open terminfo file %s.\n",
			       _nc_progname,
			       tfile[termcount]);
		MAIN_LEAKS();
		ExitProgram(EXIT_FAILURE);
	    }
	    repair_acsc(&entries[termcount].tterm);
	    termcount++;
	}

#if NCURSES_XNAMES
	if (termcount > 1)
	    _nc_align_termtype(&entries[0].tterm, &entries[1].tterm);
#endif

	/* dump as C initializer for the terminal type */
	if (initdump) {
	    if (initdump & 1)
		dump_termtype(&entries[0].tterm);
	    if (initdump & 2)
		dump_initializers(&entries[0].tterm);
	}

	/* analyze the init strings */
	else if (init_analyze) {
#undef CUR
#define CUR	entries[0].tterm.
	    analyze_string("is1", init_1string, &entries[0].tterm);
	    analyze_string("is2", init_2string, &entries[0].tterm);
	    analyze_string("is3", init_3string, &entries[0].tterm);
	    analyze_string("rs1", reset_1string, &entries[0].tterm);
	    analyze_string("rs2", reset_2string, &entries[0].tterm);
	    analyze_string("rs3", reset_3string, &entries[0].tterm);
	    analyze_string("smcup", enter_ca_mode, &entries[0].tterm);
	    analyze_string("rmcup", exit_ca_mode, &entries[0].tterm);
#undef CUR
	} else {

	    /*
	     * Here's where the real work gets done
	     */
	    switch (compare) {
	    case C_DEFAULT:
		if (itrace)
		    (void) fprintf(stderr,
				   "%s: about to dump %s\n",
				   _nc_progname,
				   tname[0]);
		(void) printf("#\tReconstructed via infocmp from file: %s\n",
			      tfile[0]);
		dump_entry(&entries[0].tterm,
			   suppress_untranslatable,
			   limited,
			   numbers,
			   NULL);
		len = show_entry();
		if (itrace)
		    (void) fprintf(stderr, "%s: length %d\n", _nc_progname, len);
		break;

	    case C_DIFFERENCE:
		show_comparing(tname);
		compare_entry(compare_predicate, &entries->tterm, quiet);
		break;

	    case C_COMMON:
		show_comparing(tname);
		compare_entry(compare_predicate, &entries->tterm, quiet);
		break;

	    case C_NAND:
		show_comparing(tname);
		compare_entry(compare_predicate, &entries->tterm, quiet);
		break;

	    case C_USEALL:
		if (itrace)
		    (void) fprintf(stderr, "%s: dumping use entry\n", _nc_progname);
		dump_entry(&entries[0].tterm,
			   suppress_untranslatable,
			   limited,
			   numbers,
			   use_predicate);
		for (i = 1; i < termcount; i++)
		    dump_uses(tname[i], !(outform == F_TERMCAP
					  || outform == F_TCONVERR));
		len = show_entry();
		if (itrace)
		    (void) fprintf(stderr, "%s: length %d\n", _nc_progname, len);
		break;
	    }
	}
    } else if (compare == C_USEALL) {
	(void) fprintf(stderr, "Sorry, -u doesn't work with -F\n");
    } else if (compare == C_DEFAULT) {
	(void) fprintf(stderr, "Use `tic -[CI] <file>' for this.\n");
    } else if (argc - optind != 2) {
	(void) fprintf(stderr,
		       "File comparison needs exactly two file arguments.\n");
    } else {
	file_comparison(argc - optind, argv + optind);
    }

    MAIN_LEAKS();
    ExitProgram(EXIT_SUCCESS);
}

/* infocmp.c ends here */
