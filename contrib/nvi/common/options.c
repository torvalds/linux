/*-
 * Copyright (c) 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1991, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#ifndef lint
static const char sccsid[] = "$Id: options.c,v 10.73 2012/10/09 06:14:07 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>

#include <bitstring.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "../vi/vi.h"
#include "pathnames.h"

static int	 	 opts_abbcmp(const void *, const void *);
static int	 	 opts_cmp(const void *, const void *);
static int	 	 opts_print(SCR *, OPTLIST const *);

#ifdef USE_WIDECHAR
#define OPT_WC	    0
#else
#define OPT_WC	    (OPT_NOSAVE | OPT_NDISP)
#endif

/*
 * O'Reilly noted options and abbreviations are from "Learning the VI Editor",
 * Fifth Edition, May 1992.  There's no way of knowing what systems they are
 * actually from.
 *
 * HPUX noted options and abbreviations are from "The Ultimate Guide to the
 * VI and EX Text Editors", 1990.
 */
OPTLIST const optlist[] = {
/* O_ALTWERASE	  4.4BSD */
	{L("altwerase"),	f_altwerase,	OPT_0BOOL,	0},
/* O_AUTOINDENT	    4BSD */
	{L("autoindent"),	NULL,		OPT_0BOOL,	0},
/* O_AUTOPRINT	    4BSD */
	{L("autoprint"),	NULL,		OPT_1BOOL,	0},
/* O_AUTOWRITE	    4BSD */
	{L("autowrite"),	NULL,		OPT_0BOOL,	0},
/* O_BACKUP	  4.4BSD */
	{L("backup"),	NULL,		OPT_STR,	0},
/* O_BEAUTIFY	    4BSD */
	{L("beautify"),	NULL,		OPT_0BOOL,	0},
/* O_CDPATH	  4.4BSD */
	{L("cdpath"),	NULL,		OPT_STR,	0},
/* O_CEDIT	  4.4BSD */
	{L("cedit"),	NULL,		OPT_STR,	0},
/* O_COLUMNS	  4.4BSD */
	{L("columns"),	f_columns,	OPT_NUM,	OPT_NOSAVE},
/* O_COMBINED */
	{L("combined"),	NULL,		OPT_0BOOL,	OPT_NOSET|OPT_WC},
/* O_COMMENT	  4.4BSD */
	{L("comment"),	NULL,		OPT_0BOOL,	0},
/* O_TMPDIR	    4BSD */
	{L("directory"),	NULL,		OPT_STR,	0},
/* O_EDCOMPATIBLE   4BSD */
	{L("edcompatible"),NULL,		OPT_0BOOL,	0},
/* O_ERRORBELLS	    4BSD */
	{L("errorbells"),	NULL,		OPT_0BOOL,	0},
/* O_ESCAPETIME	  4.4BSD */
	{L("escapetime"),	NULL,		OPT_NUM,	0},
/* O_EXRC	System V (undocumented) */
	{L("exrc"),	NULL,		OPT_0BOOL,	0},
/* O_EXTENDED	  4.4BSD */
	{L("extended"),	f_recompile,	OPT_0BOOL,	0},
/* O_FILEC	  4.4BSD */
	{L("filec"),	NULL,		OPT_STR,	0},
/* O_FILEENCODING */
	{L("fileencoding"),f_encoding,	OPT_STR,	OPT_WC},
/* O_FLASH	    HPUX */
	{L("flash"),	NULL,		OPT_1BOOL,	0},
/* O_HARDTABS	    4BSD */
	{L("hardtabs"),	NULL,		OPT_NUM,	0},
/* O_ICLOWER	  4.4BSD */
	{L("iclower"),	f_recompile,	OPT_0BOOL,	0},
/* O_IGNORECASE	    4BSD */
	{L("ignorecase"),	f_recompile,	OPT_0BOOL,	0},
/* O_INPUTENCODING */
	{L("inputencoding"),f_encoding,	OPT_STR,	OPT_WC},
/* O_KEYTIME	  4.4BSD */
	{L("keytime"),	NULL,		OPT_NUM,	0},
/* O_LEFTRIGHT	  4.4BSD */
	{L("leftright"),	f_reformat,	OPT_0BOOL,	0},
/* O_LINES	  4.4BSD */
	{L("lines"),	f_lines,	OPT_NUM,	OPT_NOSAVE},
/* O_LISP	    4BSD
 *	XXX
 *	When the lisp option is implemented, delete the OPT_NOSAVE flag,
 *	so that :mkexrc dumps it.
 */
	{L("lisp"),	f_lisp,		OPT_0BOOL,	OPT_NOSAVE},
/* O_LIST	    4BSD */
	{L("list"),	f_reformat,	OPT_0BOOL,	0},
/* O_LOCKFILES	  4.4BSD
 *	XXX
 *	Locking isn't reliable enough over NFS to require it, in addition,
 *	it's a serious startup performance problem over some remote links.
 */
	{L("lock"),	NULL,		OPT_1BOOL,	0},
/* O_MAGIC	    4BSD */
	{L("magic"),	NULL,		OPT_1BOOL,	0},
/* O_MATCHCHARS	  NetBSD 2.0 */
	{L("matchchars"),	NULL,		OPT_STR,	OPT_PAIRS},
/* O_MATCHTIME	  4.4BSD */
	{L("matchtime"),	NULL,		OPT_NUM,	0},
/* O_MESG	    4BSD */
	{L("mesg"),	NULL,		OPT_1BOOL,	0},
/* O_MODELINE	    4BSD
 *	!!!
 *	This has been documented in historical systems as both "modeline"
 *	and as "modelines".  Regardless of the name, this option represents
 *	a security problem of mammoth proportions, not to mention a stunning
 *	example of what your intro CS professor referred to as the perils of
 *	mixing code and data.  Don't add it, or I will kill you.
 */
	{L("modeline"),	NULL,		OPT_0BOOL,	OPT_NOSET},
/* O_MSGCAT	  4.4BSD */
	{L("msgcat"),	f_msgcat,	OPT_STR,	0},
/* O_NOPRINT	  4.4BSD */
	{L("noprint"),	f_print,	OPT_STR,	0},
/* O_NUMBER	    4BSD */
	{L("number"),	f_reformat,	OPT_0BOOL,	0},
/* O_OCTAL	  4.4BSD */
	{L("octal"),	f_print,	OPT_0BOOL,	0},
/* O_OPEN	    4BSD */
	{L("open"),	NULL,		OPT_1BOOL,	0},
/* O_OPTIMIZE	    4BSD */
	{L("optimize"),	NULL,		OPT_1BOOL,	0},
/* O_PARAGRAPHS	    4BSD */
	{L("paragraphs"),	NULL,		OPT_STR,	OPT_PAIRS},
/* O_PATH	  4.4BSD */
	{L("path"),	NULL,		OPT_STR,	0},
/* O_PRINT	  4.4BSD */
	{L("print"),	f_print,	OPT_STR,	0},
/* O_PROMPT	    4BSD */
	{L("prompt"),	NULL,		OPT_1BOOL,	0},
/* O_READONLY	    4BSD (undocumented) */
	{L("readonly"),	f_readonly,	OPT_0BOOL,	OPT_ALWAYS},
/* O_RECDIR	  4.4BSD */
	{L("recdir"),	NULL,		OPT_STR,	0},
/* O_REDRAW	    4BSD */
	{L("redraw"),	NULL,		OPT_0BOOL,	0},
/* O_REMAP	    4BSD */
	{L("remap"),	NULL,		OPT_1BOOL,	0},
/* O_REPORT	    4BSD */
	{L("report"),	NULL,		OPT_NUM,	0},
/* O_RULER	  4.4BSD */
	{L("ruler"),	NULL,		OPT_0BOOL,	0},
/* O_SCROLL	    4BSD */
	{L("scroll"),	NULL,		OPT_NUM,	0},
/* O_SEARCHINCR	  4.4BSD */
	{L("searchincr"),	NULL,		OPT_0BOOL,	0},
/* O_SECTIONS	    4BSD */
	{L("sections"),	NULL,		OPT_STR,	OPT_PAIRS},
/* O_SECURE	  4.4BSD */
	{L("secure"),	NULL,		OPT_0BOOL,	OPT_NOUNSET},
/* O_SHELL	    4BSD */
	{L("shell"),	NULL,		OPT_STR,	0},
/* O_SHELLMETA	  4.4BSD */
	{L("shellmeta"),	NULL,		OPT_STR,	0},
/* O_SHIFTWIDTH	    4BSD */
	{L("shiftwidth"),	NULL,		OPT_NUM,	OPT_NOZERO},
/* O_SHOWMATCH	    4BSD */
	{L("showmatch"),	NULL,		OPT_0BOOL,	0},
/* O_SHOWMODE	  4.4BSD */
	{L("showmode"),	NULL,		OPT_0BOOL,	0},
/* O_SIDESCROLL	  4.4BSD */
	{L("sidescroll"),	NULL,		OPT_NUM,	OPT_NOZERO},
/* O_SLOWOPEN	    4BSD  */
	{L("slowopen"),	NULL,		OPT_0BOOL,	0},
/* O_SOURCEANY	    4BSD (undocumented)
 *	!!!
 *	Historic vi, on startup, source'd $HOME/.exrc and ./.exrc, if they
 *	were owned by the user.  The sourceany option was an undocumented
 *	feature of historic vi which permitted the startup source'ing of
 *	.exrc files the user didn't own.  This is an obvious security problem,
 *	and we ignore the option.
 */
	{L("sourceany"),	NULL,		OPT_0BOOL,	OPT_NOSET},
/* O_TABSTOP	    4BSD */
	{L("tabstop"),	f_reformat,	OPT_NUM,	OPT_NOZERO},
/* O_TAGLENGTH	    4BSD */
	{L("taglength"),	NULL,		OPT_NUM,	0},
/* O_TAGS	    4BSD */
	{L("tags"),	NULL,		OPT_STR,	0},
/* O_TERM	    4BSD
 *	!!!
 *	By default, the historic vi always displayed information about two
 *	options, redraw and term.  Term seems sufficient.
 */
	{L("term"),	NULL,		OPT_STR,	OPT_ADISP|OPT_NOSAVE},
/* O_TERSE	    4BSD */
	{L("terse"),	NULL,		OPT_0BOOL,	0},
/* O_TILDEOP      4.4BSD */
	{L("tildeop"),	NULL,		OPT_0BOOL,	0},
/* O_TIMEOUT	    4BSD (undocumented) */
	{L("timeout"),	NULL,		OPT_1BOOL,	0},
/* O_TTYWERASE	  4.4BSD */
	{L("ttywerase"),	f_ttywerase,	OPT_0BOOL,	0},
/* O_VERBOSE	  4.4BSD */
	{L("verbose"),	NULL,		OPT_0BOOL,	0},
/* O_W1200	    4BSD */
	{L("w1200"),	f_w1200,	OPT_NUM,	OPT_NDISP|OPT_NOSAVE},
/* O_W300	    4BSD */
	{L("w300"),	f_w300,		OPT_NUM,	OPT_NDISP|OPT_NOSAVE},
/* O_W9600	    4BSD */
	{L("w9600"),	f_w9600,	OPT_NUM,	OPT_NDISP|OPT_NOSAVE},
/* O_WARN	    4BSD */
	{L("warn"),	NULL,		OPT_1BOOL,	0},
/* O_WINDOW	    4BSD */
	{L("window"),	f_window,	OPT_NUM,	0},
/* O_WINDOWNAME	    4BSD */
	{L("windowname"),	NULL,		OPT_0BOOL,	0},
/* O_WRAPLEN	  4.4BSD */
	{L("wraplen"),	NULL,		OPT_NUM,	0},
/* O_WRAPMARGIN	    4BSD */
	{L("wrapmargin"),	NULL,		OPT_NUM,	0},
/* O_WRAPSCAN	    4BSD */
	{L("wrapscan"),	NULL,		OPT_1BOOL,	0},
/* O_WRITEANY	    4BSD */
	{L("writeany"),	NULL,		OPT_0BOOL,	0},
	{NULL},
};

typedef struct abbrev {
	CHAR_T *name;
	int offset;
} OABBREV;

static OABBREV const abbrev[] = {
	{L("ai"),	O_AUTOINDENT},		/*     4BSD */
	{L("ap"),	O_AUTOPRINT},		/*     4BSD */
	{L("aw"),	O_AUTOWRITE},		/*     4BSD */
	{L("bf"),	O_BEAUTIFY},		/*     4BSD */
	{L("co"),	O_COLUMNS},		/*   4.4BSD */
	{L("dir"),	O_TMPDIR},		/*     4BSD */
	{L("eb"),	O_ERRORBELLS},		/*     4BSD */
	{L("ed"),	O_EDCOMPATIBLE},	/*     4BSD */
	{L("ex"),	O_EXRC},		/* System V (undocumented) */
	{L("fe"),	O_FILEENCODING},
	{L("ht"),	O_HARDTABS},		/*     4BSD */
	{L("ic"),	O_IGNORECASE},		/*     4BSD */
	{L("ie"),	O_INPUTENCODING},
	{L("li"),	O_LINES},		/*   4.4BSD */
	{L("modelines"),	O_MODELINE},		/*     HPUX */
	{L("nu"),	O_NUMBER},		/*     4BSD */
	{L("opt"),	O_OPTIMIZE},		/*     4BSD */
	{L("para"),	O_PARAGRAPHS},		/*     4BSD */
	{L("re"),	O_REDRAW},		/* O'Reilly */
	{L("ro"),	O_READONLY},		/*     4BSD (undocumented) */
	{L("scr"),	O_SCROLL},		/*     4BSD (undocumented) */
	{L("sect"),	O_SECTIONS},		/* O'Reilly */
	{L("sh"),	O_SHELL},		/*     4BSD */
	{L("slow"),	O_SLOWOPEN},		/*     4BSD */
	{L("sm"),	O_SHOWMATCH},		/*     4BSD */
	{L("smd"),	O_SHOWMODE},		/*     4BSD */
	{L("sw"),	O_SHIFTWIDTH},		/*     4BSD */
	{L("tag"),	O_TAGS},		/*     4BSD (undocumented) */
	{L("tl"),	O_TAGLENGTH},		/*     4BSD */
	{L("to"),	O_TIMEOUT},		/*     4BSD (undocumented) */
	{L("ts"),	O_TABSTOP},		/*     4BSD */
	{L("tty"),	O_TERM},		/*     4BSD (undocumented) */
	{L("ttytype"),	O_TERM},		/*     4BSD (undocumented) */
	{L("w"),	O_WINDOW},		/* O'Reilly */
	{L("wa"),	O_WRITEANY},		/*     4BSD */
	{L("wi"),	O_WINDOW},		/*     4BSD (undocumented) */
	{L("wl"),	O_WRAPLEN},		/*   4.4BSD */
	{L("wm"),	O_WRAPMARGIN},		/*     4BSD */
	{L("ws"),	O_WRAPSCAN},		/*     4BSD */
	{NULL},
};

/*
 * opts_init --
 *	Initialize some of the options.
 *
 * PUBLIC: int opts_init(SCR *, int *);
 */
int
opts_init(
	SCR *sp,
	int *oargs)
{
	ARGS *argv[2], a, b;
	OPTLIST const *op;
	u_long v;
	int cnt, optindx = 0;
	char *s;
	CHAR_T b2[1024];

	a.bp = b2;
	b.bp = NULL;
	a.len = b.len = 0;
	argv[0] = &a;
	argv[1] = &b;

	/* Set numeric and string default values. */
#define	OI(indx, str) {							\
	a.len = STRLEN(str);						\
	if ((CHAR_T*)str != b2)	  /* GCC puts strings in text-space. */	\
		(void)MEMCPY(b2, str, a.len+1);				\
	if (opts_set(sp, argv, NULL)) {					\
		 optindx = indx;					\
		goto err;						\
	}								\
}
	/*
	 * Indirect global options to global space.  Specifically, set up
	 * terminal, lines, columns first, they're used by other options.
	 * Note, don't set the flags until we've set up the indirection.
	 */
	if (o_set(sp, O_TERM, 0, NULL, GO_TERM))
		goto err;
	F_SET(&sp->opts[O_TERM], OPT_GLOBAL);
	if (o_set(sp, O_LINES, 0, NULL, GO_LINES))
		goto err;
	F_SET(&sp->opts[O_LINES], OPT_GLOBAL);
	if (o_set(sp, O_COLUMNS, 0, NULL, GO_COLUMNS))
		goto err;
	F_SET(&sp->opts[O_COLUMNS], OPT_GLOBAL);
	if (o_set(sp, O_SECURE, 0, NULL, GO_SECURE))
		goto err;
	F_SET(&sp->opts[O_SECURE], OPT_GLOBAL);

	/* Initialize string values. */
	(void)SPRINTF(b2, SIZE(b2),
	    L("cdpath=%s"), (s = getenv("CDPATH")) == NULL ? ":" : s);
	OI(O_CDPATH, b2);
	OI(O_CEDIT, L("cedit=\033"));

	/*
	 * !!!
	 * Vi historically stored temporary files in /var/tmp.  We store them
	 * in /tmp by default, hoping it's a memory based file system.  There
	 * are two ways to change this -- the user can set either the directory
	 * option or the TMPDIR environmental variable.
	 */
	(void)SPRINTF(b2, SIZE(b2),
	    L("directory=%s"), (s = getenv("TMPDIR")) == NULL ? _PATH_TMP : s);
	OI(O_TMPDIR, b2);
	OI(O_ESCAPETIME, L("escapetime=6"));
	OI(O_FILEC, L("filec=\t"));
	OI(O_KEYTIME, L("keytime=6"));
	OI(O_MATCHCHARS, L("matchchars=()[]{}"));
	OI(O_MATCHTIME, L("matchtime=7"));
	(void)SPRINTF(b2, SIZE(b2), L("msgcat=%s"), _PATH_MSGCAT);
	OI(O_MSGCAT, b2);
	OI(O_REPORT, L("report=5"));
	OI(O_PARAGRAPHS, L("paragraphs=IPLPPPQPP LIpplpipbp"));
	(void)SPRINTF(b2, SIZE(b2), L("path=%s"), "");
	OI(O_PATH, b2);
	(void)SPRINTF(b2, SIZE(b2), L("recdir=%s"), _PATH_PRESERVE);
	OI(O_RECDIR, b2);
	OI(O_SECTIONS, L("sections=NHSHH HUnhsh"));
	(void)SPRINTF(b2, SIZE(b2),
	    L("shell=%s"), (s = getenv("SHELL")) == NULL ? _PATH_BSHELL : s);
	OI(O_SHELL, b2);
	OI(O_SHELLMETA, L("shellmeta=~{[*?$`'\"\\"));
	OI(O_SHIFTWIDTH, L("shiftwidth=8"));
	OI(O_SIDESCROLL, L("sidescroll=16"));
	OI(O_TABSTOP, L("tabstop=8"));
	(void)SPRINTF(b2, SIZE(b2), L("tags=%s"), _PATH_TAGS);
	OI(O_TAGS, b2);

	/*
	 * XXX
	 * Initialize O_SCROLL here, after term; initializing term should
	 * have created a LINES/COLUMNS value.
	 */
	if ((v = (O_VAL(sp, O_LINES) - 1) / 2) == 0)
		v = 1;
	(void)SPRINTF(b2, SIZE(b2), L("scroll=%ld"), v);
	OI(O_SCROLL, b2);

	/*
	 * The default window option values are:
	 *		8 if baud rate <=  600
	 *	       16 if baud rate <= 1200
	 *	LINES - 1 if baud rate  > 1200
	 *
	 * Note, the windows option code will correct any too-large value
	 * or when the O_LINES value is 1.
	 */
	if (sp->gp->scr_baud(sp, &v))
		return (1);
	if (v <= 600)
		v = 8;
	else if (v <= 1200)
		v = 16;
	else if ((v = O_VAL(sp, O_LINES) - 1) == 0)
		v = 1;

	(void)SPRINTF(b2, SIZE(b2), L("window=%lu"), v);
	OI(O_WINDOW, b2);

	/*
	 * Set boolean default values, and copy all settings into the default
	 * information.  OS_NOFREE is set, we're copying, not replacing.
	 */
	for (op = optlist, cnt = 0; op->name != NULL; ++op, ++cnt) {
		if (F_ISSET(op, OPT_GLOBAL))
			continue;
		switch (op->type) {
		case OPT_0BOOL:
			break;
		case OPT_1BOOL:
			O_SET(sp, cnt);
			O_D_SET(sp, cnt);
			break;
		case OPT_NUM:
			o_set(sp, cnt, OS_DEF, NULL, O_VAL(sp, cnt));
			break;
		case OPT_STR:
			if (O_STR(sp, cnt) != NULL && o_set(sp, cnt,
			    OS_DEF | OS_NOFREE | OS_STRDUP, O_STR(sp, cnt), 0))
				goto err;
			break;
		default:
			abort();
		}
	}

	/*
	 * !!!
	 * Some options can be initialized by the command name or the
	 * command-line arguments.  They don't set the default values,
	 * it's historic practice.
	 */
	for (; *oargs != -1; ++oargs)
		OI(*oargs, optlist[*oargs].name);
#undef OI
	return (0);

err:	msgq_wstr(sp, M_ERR, optlist[optindx].name,
	    "031|Unable to set default %s option");
	return (1);
}

/*
 * opts_set --
 *	Change the values of one or more options.
 *
 * PUBLIC: int opts_set(SCR *, ARGS *[], char *);
 */
int
opts_set(
	SCR *sp,
	ARGS *argv[],
	char *usage)
{
	enum optdisp disp;
	enum nresult nret;
	OPTLIST const *op;
	OPTION *spo;
	u_long isset, turnoff, value;
	int ch, equals, nf, nf2, offset, qmark, rval;
	CHAR_T *endp, *name, *p, *sep;
	char *p2, *t2;
	char *np;
	size_t nlen;

	disp = NO_DISPLAY;
	for (rval = 0; argv[0]->len != 0; ++argv) {
		/*
		 * The historic vi dumped the options for each occurrence of
		 * "all" in the set list.  Puhleeze.
		 */
		if (!STRCMP(argv[0]->bp, L("all"))) {
			disp = ALL_DISPLAY;
			continue;
		}

		/* Find equals sign or question mark. */
		for (sep = NULL, equals = qmark = 0,
		    p = name = argv[0]->bp; (ch = *p) != '\0'; ++p)
			if (ch == '=' || ch == '?') {
				if (p == name) {
					if (usage != NULL)
						msgq(sp, M_ERR,
						    "032|Usage: %s", usage);
					return (1);
				}
				sep = p;
				if (ch == '=')
					equals = 1;
				else
					qmark = 1;
				break;
			}

		turnoff = 0;
		op = NULL;
		if (sep != NULL)
			*sep++ = '\0';

		/* Search for the name, then name without any leading "no". */
		if ((op = opts_search(name)) == NULL &&
		    name[0] == 'n' && name[1] == 'o') {
			turnoff = 1;
			name += 2;
			op = opts_search(name);
		}
		if (op == NULL) {
			opts_nomatch(sp, name);
			rval = 1;
			continue;
		}

		/* Find current option values. */
		offset = op - optlist;
		spo = sp->opts + offset;

		/*
		 * !!!
		 * Historically, the question mark could be a separate
		 * argument.
		 */
		if (!equals && !qmark &&
		    argv[1]->len == 1 && argv[1]->bp[0] == '?') {
			++argv;
			qmark = 1;
		}

		/* Set name, value. */
		switch (op->type) {
		case OPT_0BOOL:
		case OPT_1BOOL:
			/* Some options may not be reset. */
			if (F_ISSET(op, OPT_NOUNSET) && turnoff) {
				msgq_wstr(sp, M_ERR, name,
			    "291|set: the %s option may not be turned off");
				rval = 1;
				break;
			}

			/* Some options may not be set. */
			if (F_ISSET(op, OPT_NOSET) && !turnoff) {
				msgq_wstr(sp, M_ERR, name,
			    "313|set: the %s option may never be turned on");
				rval = 1;
				break;
			}

			if (equals) {
				msgq_wstr(sp, M_ERR, name,
			    "034|set: [no]%s option doesn't take a value");
				rval = 1;
				break;
			}
			if (qmark) {
				if (!disp)
					disp = SELECT_DISPLAY;
				F_SET(spo, OPT_SELECTED);
				break;
			}

			/*
			 * Do nothing if the value is unchanged, the underlying
			 * functions can be expensive.
			 */
			isset = !turnoff;
			if (!F_ISSET(op, OPT_ALWAYS))
				if (isset) {
					if (O_ISSET(sp, offset))
						break;
				} else
					if (!O_ISSET(sp, offset))
						break;

			/* Report to subsystems. */
			if ((op->func != NULL &&
			    op->func(sp, spo, NULL, &isset)) ||
			    ex_optchange(sp, offset, NULL, &isset) ||
			    v_optchange(sp, offset, NULL, &isset) ||
			    sp->gp->scr_optchange(sp, offset, NULL, &isset)) {
				rval = 1;
				break;
			}

			/* Set the value. */
			if (isset)
				O_SET(sp, offset);
			else
				O_CLR(sp, offset);
			break;
		case OPT_NUM:
			if (turnoff) {
				msgq_wstr(sp, M_ERR, name,
				    "035|set: %s option isn't a boolean");
				rval = 1;
				break;
			}
			if (qmark || !equals) {
				if (!disp)
					disp = SELECT_DISPLAY;
				F_SET(spo, OPT_SELECTED);
				break;
			}

			if (!ISDIGIT(sep[0]))
				goto badnum;
			if ((nret =
			    nget_uslong(&value, sep, &endp, 10)) != NUM_OK) {
				INT2CHAR(sp, name, STRLEN(name) + 1, 
					     np, nlen);
				p2 = msg_print(sp, np, &nf);
				INT2CHAR(sp, sep, STRLEN(sep) + 1, 
					     np, nlen);
				t2 = msg_print(sp, np, &nf2);
				switch (nret) {
				case NUM_ERR:
					msgq(sp, M_SYSERR,
					    "036|set: %s option: %s", p2, t2);
					break;
				case NUM_OVER:
					msgq(sp, M_ERR,
			    "037|set: %s option: %s: value overflow", p2, t2);
					break;
				case NUM_OK:
				case NUM_UNDER:
					abort();
				}
				if (nf)
					FREE_SPACE(sp, p2, 0);
				if (nf2)
					FREE_SPACE(sp, t2, 0);
				rval = 1;
				break;
			}
			if (*endp && !cmdskip(*endp)) {
badnum:				INT2CHAR(sp, name, STRLEN(name) + 1, 
					     np, nlen);
				p2 = msg_print(sp, np, &nf);
				INT2CHAR(sp, sep, STRLEN(sep) + 1, 
					     np, nlen);
				t2 = msg_print(sp, np, &nf2);
				msgq(sp, M_ERR,
		    "038|set: %s option: %s is an illegal number", p2, t2);
				if (nf)
					FREE_SPACE(sp, p2, 0);
				if (nf2)
					FREE_SPACE(sp, t2, 0);
				rval = 1;
				break;
			}

			/* Some options may never be set to zero. */
			if (F_ISSET(op, OPT_NOZERO) && value == 0) {
				msgq_wstr(sp, M_ERR, name,
			    "314|set: the %s option may never be set to 0");
				rval = 1;
				break;
			}

			/*
			 * Do nothing if the value is unchanged, the underlying
			 * functions can be expensive.
			 */
			if (!F_ISSET(op, OPT_ALWAYS) &&
			    O_VAL(sp, offset) == value)
				break;

			/* Report to subsystems. */
			INT2CHAR(sp, sep, STRLEN(sep) + 1, np, nlen);
			if ((op->func != NULL &&
			    op->func(sp, spo, np, &value)) ||
			    ex_optchange(sp, offset, np, &value) ||
			    v_optchange(sp, offset, np, &value) ||
			    sp->gp->scr_optchange(sp, offset, np, &value)) {
				rval = 1;
				break;
			}

			/* Set the value. */
			if (o_set(sp, offset, 0, NULL, value))
				rval = 1;
			break;
		case OPT_STR:
			if (turnoff) {
				msgq_wstr(sp, M_ERR, name,
				    "039|set: %s option isn't a boolean");
				rval = 1;
				break;
			}
			if (qmark || !equals) {
				if (!disp)
					disp = SELECT_DISPLAY;
				F_SET(spo, OPT_SELECTED);
				break;
			}

			/* Check for strings that must have even length. */
			if (F_ISSET(op, OPT_PAIRS) && STRLEN(sep) & 1) {
				msgq_wstr(sp, M_ERR, name,
				    "047|The %s option must be in two character groups");
				rval = 1;
				break;
			}

			/*
			 * Do nothing if the value is unchanged, the underlying
			 * functions can be expensive.
			 */
			INT2CHAR(sp, sep, STRLEN(sep) + 1, np, nlen);
			if (!F_ISSET(op, OPT_ALWAYS) &&
			    O_STR(sp, offset) != NULL &&
			    !strcmp(O_STR(sp, offset), np))
				break;

			/* Report to subsystems. */
			if ((op->func != NULL &&
			    op->func(sp, spo, np, NULL)) ||
			    ex_optchange(sp, offset, np, NULL) ||
			    v_optchange(sp, offset, np, NULL) ||
			    sp->gp->scr_optchange(sp, offset, np, NULL)) {
				rval = 1;
				break;
			}

			/* Set the value. */
			if (o_set(sp, offset, OS_STRDUP, np, 0))
				rval = 1;
			break;
		default:
			abort();
		}
	}
	if (disp != NO_DISPLAY)
		opts_dump(sp, disp);
	return (rval);
}

/*
 * o_set --
 *	Set an option's value.
 *
 * PUBLIC: int o_set(SCR *, int, u_int, char *, u_long);
 */
int
o_set(
	SCR *sp,
	int opt,
	u_int flags,
	char *str,
	u_long val)
{
	OPTION *op;

	/* Set a pointer to the options area. */
	op = F_ISSET(&sp->opts[opt], OPT_GLOBAL) ?
	    &sp->gp->opts[sp->opts[opt].o_cur.val] : &sp->opts[opt];

	/* Copy the string, if requested. */
	if (LF_ISSET(OS_STRDUP) && (str = strdup(str)) == NULL) {
		msgq(sp, M_SYSERR, NULL);
		return (1);
	}

	/* Free the previous string, if requested, and set the value. */
	if LF_ISSET(OS_DEF)
		if (LF_ISSET(OS_STR | OS_STRDUP)) {
			if (!LF_ISSET(OS_NOFREE) && op->o_def.str != NULL)
				free(op->o_def.str);
			op->o_def.str = str;
		} else
			op->o_def.val = val;
	else
		if (LF_ISSET(OS_STR | OS_STRDUP)) {
			if (!LF_ISSET(OS_NOFREE) && op->o_cur.str != NULL)
				free(op->o_cur.str);
			op->o_cur.str = str;
		} else
			op->o_cur.val = val;
	return (0);
}

/*
 * opts_empty --
 *	Return 1 if the string option is invalid, 0 if it's OK.
 *
 * PUBLIC: int opts_empty(SCR *, int, int);
 */
int
opts_empty(
	SCR *sp,
	int off,
	int silent)
{
	char *p;

	if ((p = O_STR(sp, off)) == NULL || p[0] == '\0') {
		if (!silent)
			msgq_wstr(sp, M_ERR, optlist[off].name,
			    "305|No %s edit option specified");
		return (1);
	}
	return (0);
}

/*
 * opts_dump --
 *	List the current values of selected options.
 *
 * PUBLIC: void opts_dump(SCR *, enum optdisp);
 */
void
opts_dump(
	SCR *sp,
	enum optdisp type)
{
	OPTLIST const *op;
	int base, b_num, cnt, col, colwidth, curlen, s_num;
	int numcols, numrows, row;
	int b_op[O_OPTIONCOUNT], s_op[O_OPTIONCOUNT];
	char nbuf[20];

	/*
	 * Options are output in two groups -- those that fit in a column and
	 * those that don't.  Output is done on 6 character "tab" boundaries
	 * for no particular reason.  (Since we don't output tab characters,
	 * we can ignore the terminal's tab settings.)  Ignore the user's tab
	 * setting because we have no idea how reasonable it is.
	 *
	 * Find a column width we can live with, testing from 10 columns to 1.
	 */
	for (numcols = 10; numcols > 1; --numcols) {
		colwidth = sp->cols / numcols & ~(STANDARD_TAB - 1);
		if (colwidth >= 10) {
			colwidth =
			    (colwidth + STANDARD_TAB) & ~(STANDARD_TAB - 1);
			numcols = sp->cols / colwidth;
			break;
		}
		colwidth = 0;
	}

	/*
	 * Get the set of options to list, entering them into
	 * the column list or the overflow list.
	 */
	for (b_num = s_num = 0, op = optlist; op->name != NULL; ++op) {
		cnt = op - optlist;

		/* If OPT_NDISP set, it's never displayed. */
		if (F_ISSET(op, OPT_NDISP))
			continue;

		switch (type) {
		case ALL_DISPLAY:		/* Display all. */
			break;
		case CHANGED_DISPLAY:		/* Display changed. */
			/* If OPT_ADISP set, it's always "changed". */
			if (F_ISSET(op, OPT_ADISP))
				break;
			switch (op->type) {
			case OPT_0BOOL:
			case OPT_1BOOL:
			case OPT_NUM:
				if (O_VAL(sp, cnt) == O_D_VAL(sp, cnt))
					continue;
				break;
			case OPT_STR:
				if (O_STR(sp, cnt) == O_D_STR(sp, cnt) ||
				    (O_D_STR(sp, cnt) != NULL &&
				    !strcmp(O_STR(sp, cnt), O_D_STR(sp, cnt))))
					continue;
				break;
			}
			break;
		case SELECT_DISPLAY:		/* Display selected. */
			if (!F_ISSET(&sp->opts[cnt], OPT_SELECTED))
				continue;
			break;
		default:
		case NO_DISPLAY:
			abort();
		}
		F_CLR(&sp->opts[cnt], OPT_SELECTED);

		curlen = STRLEN(op->name);
		switch (op->type) {
		case OPT_0BOOL:
		case OPT_1BOOL:
			if (!O_ISSET(sp, cnt))
				curlen += 2;
			break;
		case OPT_NUM:
			(void)snprintf(nbuf,
			    sizeof(nbuf), "%ld", O_VAL(sp, cnt));
			curlen += strlen(nbuf);
			break;
		case OPT_STR:
			if (O_STR(sp, cnt) != NULL)
				curlen += strlen(O_STR(sp, cnt));
			curlen += 3;
			break;
		}
		/* Offset by 2 so there's a gap. */
		if (curlen <= colwidth - 2)
			s_op[s_num++] = cnt;
		else
			b_op[b_num++] = cnt;
	}

	if (s_num > 0) {
		/* Figure out the number of rows. */
		if (s_num > numcols) {
			numrows = s_num / numcols;
			if (s_num % numcols)
				++numrows;
		} else
			numrows = 1;

		/* Display the options in sorted order. */
		for (row = 0; row < numrows;) {
			for (base = row, col = 0; col < numcols; ++col) {
				cnt = opts_print(sp, &optlist[s_op[base]]);
				if ((base += numrows) >= s_num)
					break;
				(void)ex_printf(sp, "%*s",
				    (int)(colwidth - cnt), "");
			}
			if (++row < numrows || b_num)
				(void)ex_puts(sp, "\n");
		}
	}

	for (row = 0; row < b_num;) {
		(void)opts_print(sp, &optlist[b_op[row]]);
		if (++row < b_num)
			(void)ex_puts(sp, "\n");
	}
	(void)ex_puts(sp, "\n");
}

/*
 * opts_print --
 *	Print out an option.
 */
static int
opts_print(
	SCR *sp,
	OPTLIST const *op)
{
	int curlen, offset;

	curlen = 0;
	offset = op - optlist;
	switch (op->type) {
	case OPT_0BOOL:
	case OPT_1BOOL:
		curlen += ex_printf(sp,
		    "%s"WS, O_ISSET(sp, offset) ? "" : "no", op->name);
		break;
	case OPT_NUM:
		curlen += ex_printf(sp, WS"=%ld", op->name, O_VAL(sp, offset));
		break;
	case OPT_STR:
		curlen += ex_printf(sp, WS"=\"%s\"", op->name,
		    O_STR(sp, offset) == NULL ? "" : O_STR(sp, offset));
		break;
	}
	return (curlen);
}

/*
 * opts_save --
 *	Write the current configuration to a file.
 *
 * PUBLIC: int opts_save(SCR *, FILE *);
 */
int
opts_save(
	SCR *sp,
	FILE *fp)
{
	OPTLIST const *op;
	CHAR_T ch, *p;
	char nch, *np;
	int cnt;

	for (op = optlist; op->name != NULL; ++op) {
		if (F_ISSET(op, OPT_NOSAVE))
			continue;
		cnt = op - optlist;
		switch (op->type) {
		case OPT_0BOOL:
		case OPT_1BOOL:
			if (O_ISSET(sp, cnt))
				(void)fprintf(fp, "set "WS"\n", op->name);
			else
				(void)fprintf(fp, "set no"WS"\n", op->name);
			break;
		case OPT_NUM:
			(void)fprintf(fp,
			    "set "WS"=%-3ld\n", op->name, O_VAL(sp, cnt));
			break;
		case OPT_STR:
			if (O_STR(sp, cnt) == NULL)
				break;
			(void)fprintf(fp, "set ");
			for (p = op->name; (ch = *p) != '\0'; ++p) {
				if (cmdskip(ch) || ch == '\\')
					(void)putc('\\', fp);
				fprintf(fp, WC, ch);
			}
			(void)putc('=', fp);
			for (np = O_STR(sp, cnt); (nch = *np) != '\0'; ++np) {
				if (cmdskip(nch) || nch == '\\')
					(void)putc('\\', fp);
				(void)putc(nch, fp);
			}
			(void)putc('\n', fp);
			break;
		}
		if (ferror(fp)) {
			msgq(sp, M_SYSERR, NULL);
			return (1);
		}
	}
	return (0);
}

/* 
 * opts_search --
 *	Search for an option.
 *
 * PUBLIC: OPTLIST const *opts_search(CHAR_T *);
 */
OPTLIST const *
opts_search(CHAR_T *name)
{
	OPTLIST const *op, *found;
	OABBREV atmp, *ap;
	OPTLIST otmp;
	size_t len;

	/* Check list of abbreviations. */
	atmp.name = name;
	if ((ap = bsearch(&atmp, abbrev, sizeof(abbrev) / sizeof(OABBREV) - 1,
	    sizeof(OABBREV), opts_abbcmp)) != NULL)
		return (optlist + ap->offset);

	/* Check list of options. */
	otmp.name = name;
	if ((op = bsearch(&otmp, optlist, sizeof(optlist) / sizeof(OPTLIST) - 1,
	    sizeof(OPTLIST), opts_cmp)) != NULL)
		return (op);
		
	/*
	 * Check to see if the name is the prefix of one (and only one)
	 * option.  If so, return the option.
	 */
	len = STRLEN(name);
	for (found = NULL, op = optlist; op->name != NULL; ++op) {
		if (op->name[0] < name[0])
			continue;
		if (op->name[0] > name[0])
			break;
		if (!MEMCMP(op->name, name, len)) {
			if (found != NULL)
				return (NULL);
			found = op;
		}
	}
	return (found);
}

/* 
 * opts_nomatch --
 *	Standard nomatch error message for options.
 *
 * PUBLIC: void opts_nomatch(SCR *, CHAR_T *);
 */
void
opts_nomatch(
	SCR *sp,
	CHAR_T *name)
{
	msgq_wstr(sp, M_ERR, name,
	    "033|set: no %s option: 'set all' gives all option values");
}

static int
opts_abbcmp(
	const void *a,
	const void *b)
{
	return(STRCMP(((OABBREV *)a)->name, ((OABBREV *)b)->name));
}

static int
opts_cmp(
	const void *a,
	const void *b)
{
	return(STRCMP(((OPTLIST *)a)->name, ((OPTLIST *)b)->name));
}

/*
 * opts_copy --
 *	Copy a screen's OPTION array.
 *
 * PUBLIC: int opts_copy(SCR *, SCR *);
 */
int
opts_copy(
	SCR *orig,
	SCR *sp)
{
	int cnt, rval;

	/* Copy most everything without change. */
	memcpy(sp->opts, orig->opts, sizeof(orig->opts));

	/* Copy the string edit options. */
	for (cnt = rval = 0; cnt < O_OPTIONCOUNT; ++cnt) {
		if (optlist[cnt].type != OPT_STR ||
		    F_ISSET(&sp->opts[cnt], OPT_GLOBAL))
			continue;
		/*
		 * If never set, or already failed, NULL out the entries --
		 * have to continue after failure, otherwise would have two
		 * screens referencing the same memory.
		 */
		if (rval || O_STR(sp, cnt) == NULL) {
			o_set(sp, cnt, OS_NOFREE | OS_STR, NULL, 0);
			o_set(sp, cnt, OS_DEF | OS_NOFREE | OS_STR, NULL, 0);
			continue;
		}

		/* Copy the current string. */
		if (o_set(sp, cnt, OS_NOFREE | OS_STRDUP, O_STR(sp, cnt), 0)) {
			o_set(sp, cnt, OS_DEF | OS_NOFREE | OS_STR, NULL, 0);
			goto nomem;
		}

		/* Copy the default string. */
		if (O_D_STR(sp, cnt) != NULL && o_set(sp, cnt,
		    OS_DEF | OS_NOFREE | OS_STRDUP, O_D_STR(sp, cnt), 0)) {
nomem:			msgq(orig, M_SYSERR, NULL);
			rval = 1;
		}
	}
	return (rval);
}

/*
 * opts_free --
 *	Free all option strings
 *
 * PUBLIC: void opts_free(SCR *);
 */
void
opts_free(SCR *sp)
{
	int cnt;

	for (cnt = 0; cnt < O_OPTIONCOUNT; ++cnt) {
		if (optlist[cnt].type != OPT_STR ||
		    F_ISSET(&sp->opts[cnt], OPT_GLOBAL))
			continue;
		if (O_STR(sp, cnt) != NULL)
			free(O_STR(sp, cnt));
		if (O_D_STR(sp, cnt) != NULL)
			free(O_D_STR(sp, cnt));
	}
}
