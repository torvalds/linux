/* $Header: /p/tcsh/cvsroot/tcsh/ed.screen.c,v 3.82 2016/11/24 15:04:14 christos Exp $ */
/*
 * ed.screen.c: Editor/termcap-curses interface
 */
/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "sh.h"

RCSID("$tcsh: ed.screen.c,v 3.82 2016/11/24 15:04:14 christos Exp $")

#include "ed.h"
#include "tc.h"
#include "ed.defns.h"

/* #define DEBUG_LITERAL */

/*
 * IMPORTANT NOTE: these routines are allowed to look at the current screen
 * and the current possition assuming that it is correct.  If this is not
 * true, then the update will be WRONG!  This is (should be) a valid
 * assumption...
 */

#define TC_BUFSIZE 2048

#define GoodStr(a) (tstr[a].str != NULL && tstr[a].str[0] != '\0')
#define Str(a) tstr[a].str
#define Val(a) tval[a].val

static const struct {
    const char   *b_name;
    speed_t b_rate;
}       baud_rate[] = {

#ifdef B0
    { "0", B0 },
#endif
#ifdef B50
    { "50", B50 },
#endif
#ifdef B75
    { "75", B75 },
#endif
#ifdef B110
    { "110", B110 },
#endif
#ifdef B134
    { "134", B134 },
#endif
#ifdef B150
    { "150", B150 },
#endif
#ifdef B200
    { "200", B200 },
#endif
#ifdef B300
    { "300", B300 },
#endif
#ifdef B600
    { "600", B600 },
#endif
#ifdef B900
    { "900", B900 },
#endif
#ifdef B1200
    { "1200", B1200 },
#endif
#ifdef B1800
    { "1800", B1800 },
#endif
#ifdef B2400
    { "2400", B2400 },
#endif
#ifdef B3600
    { "3600", B3600 },
#endif
#ifdef B4800
    { "4800", B4800 },
#endif
#ifdef B7200
    { "7200", B7200 },
#endif
#ifdef B9600
    { "9600", B9600 },
#endif
#ifdef EXTA
    { "19200", EXTA },
#endif
#ifdef B19200
    { "19200", B19200 },
#endif
#ifdef EXTB
    { "38400", EXTB },
#endif
#ifdef B38400
    { "38400", B38400 },
#endif
    { NULL, 0 }
};

#define T_at7   0
#define T_al	1
#define T_bl	2
#define T_cd	3
#define T_ce	4
#define T_ch	5
#define T_cl	6
#define	T_dc	7
#define	T_dl	8
#define	T_dm	9
#define	T_ed	10
#define	T_ei	11
#define	T_fs	12
#define	T_ho	13
#define	T_ic	14
#define	T_im	15 
#define	T_ip	16
#define	T_kd	17
#define T_kh    18
#define	T_kl	19
#define T_kr	20
#define T_ku	21
#define T_md	22
#define T_me	23
#define T_mr    24
#define T_nd	25
#define T_se	26
#define T_so	27
#define T_ts	28
#define T_up	29
#define T_us	30
#define T_ue	31
#define T_vb	32
#define T_DC	33
#define T_DO	34
#define T_IC	35
#define T_LE	36
#define T_RI	37
#define T_UP	38
#define T_str   39

static struct termcapstr {
    const char   *name;
    const char   *long_name;
    char   *str;
} tstr[T_str + 1];


#define T_am	0
#define T_pt	1
#define T_li	2
#define T_co	3
#define T_km	4
#define T_xn	5
#define T_val	6
static struct termcapval {
    const char   *name;
    const char   *long_name;
    int     val;
} tval[T_val + 1];

void
terminit(void)
{
#ifdef NLS_CATALOGS
    int i;

    for (i = 0; i < T_str + 1; i++)
	xfree((ptr_t)(intptr_t)tstr[i].long_name);

    for (i = 0; i < T_val + 1; i++)
	xfree((ptr_t)(intptr_t)tval[i].long_name);
#endif

    tstr[T_al].name = "al";
    tstr[T_al].long_name = CSAVS(4, 1, "add new blank line");

    tstr[T_bl].name = "bl";
    tstr[T_bl].long_name = CSAVS(4, 2, "audible bell");

    tstr[T_cd].name = "cd";
    tstr[T_cd].long_name = CSAVS(4, 3, "clear to bottom");

    tstr[T_ce].name = "ce";
    tstr[T_ce].long_name = CSAVS(4, 4, "clear to end of line");

    tstr[T_ch].name = "ch";
    tstr[T_ch].long_name = CSAVS(4, 5, "cursor to horiz pos");

    tstr[T_cl].name = "cl";
    tstr[T_cl].long_name = CSAVS(4, 6, "clear screen");

    tstr[T_dc].name = "dc";
    tstr[T_dc].long_name = CSAVS(4, 7, "delete a character");

    tstr[T_dl].name = "dl";
    tstr[T_dl].long_name = CSAVS(4, 8, "delete a line");

    tstr[T_dm].name = "dm";
    tstr[T_dm].long_name = CSAVS(4, 9, "start delete mode");

    tstr[T_ed].name = "ed";
    tstr[T_ed].long_name = CSAVS(4, 10, "end delete mode");

    tstr[T_ei].name = "ei";
    tstr[T_ei].long_name = CSAVS(4, 11, "end insert mode");

    tstr[T_fs].name = "fs";
    tstr[T_fs].long_name = CSAVS(4, 12, "cursor from status line");

    tstr[T_ho].name = "ho";
    tstr[T_ho].long_name = CSAVS(4, 13, "home cursor");

    tstr[T_ic].name = "ic";
    tstr[T_ic].long_name = CSAVS(4, 14, "insert character");

    tstr[T_im].name = "im";
    tstr[T_im].long_name = CSAVS(4, 15, "start insert mode");

    tstr[T_ip].name = "ip";
    tstr[T_ip].long_name = CSAVS(4, 16, "insert padding");

    tstr[T_kd].name = "kd";
    tstr[T_kd].long_name = CSAVS(4, 17, "sends cursor down");

    tstr[T_kl].name = "kl";
    tstr[T_kl].long_name = CSAVS(4, 18, "sends cursor left");

    tstr[T_kr].name = "kr";
    tstr[T_kr].long_name = CSAVS(4, 19, "sends cursor right");

    tstr[T_ku].name = "ku";
    tstr[T_ku].long_name = CSAVS(4, 20, "sends cursor up");

    tstr[T_md].name = "md";
    tstr[T_md].long_name = CSAVS(4, 21, "begin bold");

    tstr[T_me].name = "me";
    tstr[T_me].long_name = CSAVS(4, 22, "end attributes");

    tstr[T_nd].name = "nd";
    tstr[T_nd].long_name = CSAVS(4, 23, "non destructive space");

    tstr[T_se].name = "se";
    tstr[T_se].long_name = CSAVS(4, 24, "end standout");

    tstr[T_so].name = "so";
    tstr[T_so].long_name = CSAVS(4, 25, "begin standout");

    tstr[T_ts].name = "ts";
    tstr[T_ts].long_name = CSAVS(4, 26, "cursor to status line");

    tstr[T_up].name = "up";
    tstr[T_up].long_name = CSAVS(4, 27, "cursor up one");

    tstr[T_us].name = "us";
    tstr[T_us].long_name = CSAVS(4, 28, "begin underline");

    tstr[T_ue].name = "ue";
    tstr[T_ue].long_name = CSAVS(4, 29, "end underline");

    tstr[T_vb].name = "vb";
    tstr[T_vb].long_name = CSAVS(4, 30, "visible bell");

    tstr[T_DC].name = "DC";
    tstr[T_DC].long_name = CSAVS(4, 31, "delete multiple chars");

    tstr[T_DO].name = "DO";
    tstr[T_DO].long_name = CSAVS(4, 32, "cursor down multiple");

    tstr[T_IC].name = "IC";
    tstr[T_IC].long_name = CSAVS(4, 33, "insert multiple chars");

    tstr[T_LE].name = "LE";
    tstr[T_LE].long_name = CSAVS(4, 34, "cursor left multiple");

    tstr[T_RI].name = "RI";
    tstr[T_RI].long_name = CSAVS(4, 35, "cursor right multiple");

    tstr[T_UP].name = "UP";
    tstr[T_UP].long_name = CSAVS(4, 36, "cursor up multiple");

    tstr[T_kh].name = "kh";
    tstr[T_kh].long_name = CSAVS(4, 43, "send cursor home");

    tstr[T_at7].name = "@7";
    tstr[T_at7].long_name = CSAVS(4, 44, "send cursor end");

    tstr[T_mr].name = "mr";
    tstr[T_mr].long_name = CSAVS(4, 45, "begin reverse video");

    tstr[T_str].name = NULL;
    tstr[T_str].long_name = NULL;


    tval[T_am].name = "am";
    tval[T_am].long_name = CSAVS(4, 37, "Has automatic margins");

    tval[T_pt].name = "pt";
    tval[T_pt].long_name = CSAVS(4, 38, "Can use physical tabs");

    tval[T_li].name = "li";
    tval[T_li].long_name = CSAVS(4, 39, "Number of lines");

    tval[T_co].name = "co";
    tval[T_co].long_name = CSAVS(4, 40, "Number of columns");

    tval[T_km].name = "km";
    tval[T_km].long_name = CSAVS(4, 41, "Has meta key");

    tval[T_xn].name = "xn";
    tval[T_xn].long_name = CSAVS(4, 42, "Newline ignored at right margin");

    tval[T_val].name = NULL;
    tval[T_val].long_name = NULL;
}

/*
 * A very useful table from justin@crim.ca (Justin Bur) :-)
 * (Modified by per@erix.ericsson.se (Per Hedeland)
 *  - first (and second:-) case fixed)
 *
 * Description     Termcap variables       tcsh behavior
 * 		   am      xn              UseRightmost    SendCRLF
 * --------------  ------- -------         ------------    ------------
 * Automargins     yes     no              yes             no
 * Magic Margins   yes     yes             yes             no
 * No Wrap         no      --              yes             yes
 */

static int me_all = 0;		/* does two or more of the attributes use me */

static	void	ReBufferDisplay	(void);
static	void	TCset		(struct termcapstr *, const char *);


static void
TCset(struct termcapstr *t, const char *cap)
{
    if (cap == NULL || *cap == '\0') {
	xfree(t->str);
	t->str = NULL;
    } else {
	size_t size;

	size = strlen(cap) + 1;
	t->str = xrealloc(t->str, size);
	memcpy(t->str, cap, size);
    }
}


/*ARGSUSED*/
void
TellTC(void)
{
    struct termcapstr *t;
    char *first, *s;

    xprintf("%s", CGETS(7, 1, "\n\tTcsh thinks your terminal has the\n"));
    xprintf("%s", CGETS(7, 2, "\tfollowing characteristics:\n\n"));
    xprintf(CGETS(7, 3, "\tIt has %d columns and %d lines\n"),
	    Val(T_co), Val(T_li));
    s = strsave(T_HasMeta ? CGETS(7, 5, "a") : CGETS(7, 6, "no"));
    cleanup_push(s, xfree);
    first = s;
    xprintf(CGETS(7, 4, "\tIt has %s meta key\n"), s);
    s = strsave(T_Tabs ? "" : CGETS(7, 8, " not"));
    cleanup_push(s, xfree);
    xprintf(CGETS(7, 7, "\tIt can%s use tabs\n"), s);
    s = strsave((T_Margin&MARGIN_AUTO) ?
		CGETS(7, 10, "has") : CGETS(7, 11, "does not have"));
    cleanup_push(s, xfree);
    xprintf(CGETS(7, 9, "\tIt %s automatic margins\n"), s);
    if (T_Margin & MARGIN_AUTO) {
        s = strsave((T_Margin & MARGIN_MAGIC) ?
			CGETS(7, 10, "has") : CGETS(7, 11, "does not have"));
	cleanup_push(s, xfree);
	xprintf(CGETS(7, 12, "\tIt %s magic margins\n"), s);
    }
    for (t = tstr; t->name != NULL; t++) {
        s = strsave(t->str && *t->str ? t->str : CGETS(7, 13, "(empty)"));
	cleanup_push(s, xfree);
	xprintf("\t%36s (%s) == %s\n", t->long_name, t->name, s);
	cleanup_until(s);
    }
    xputchar('\n');
    cleanup_until(first);
}


static void
ReBufferDisplay(void)
{
    int i;
    Char **b;

    b = Display;
    Display = NULL;
    blkfree(b);
    b = Vdisplay;
    Vdisplay = NULL;
    blkfree(b);
    TermH = Val(T_co);
    TermV = (INBUFSIZE * 4) / TermH + 1;/*FIXBUF*/
    b = xmalloc(sizeof(*b) * (TermV + 1));
    for (i = 0; i < TermV; i++)
	b[i] = xmalloc(sizeof(*b[i]) * (TermH + 1));
    b[TermV] = NULL;
    Display = b;
    b = xmalloc(sizeof(*b) * (TermV + 1));
    for (i = 0; i < TermV; i++)
	b[i] = xmalloc(sizeof(*b[i]) * (TermH + 1));
    b[TermV] = NULL;
    Vdisplay = b;
}

void
SetTC(char *what, char *how)
{
    struct termcapstr *ts;
    struct termcapval *tv;

    /*
     * Do the strings first
     */
    setname("settc");
    for (ts = tstr; ts->name != NULL; ts++)
	if (strcmp(ts->name, what) == 0)
	    break;
    if (ts->name != NULL) {
	TCset(ts, how);
	/*
	 * Reset variables
	 */
	if (GoodStr(T_me) && GoodStr(T_ue))
	    me_all = (strcmp(Str(T_me), Str(T_ue)) == 0);
	else
	    me_all = 0;
	if (GoodStr(T_me) && GoodStr(T_se))
	    me_all |= (strcmp(Str(T_me), Str(T_se)) == 0);

	T_CanCEOL = GoodStr(T_ce);
	T_CanDel = GoodStr(T_dc) || GoodStr(T_DC);
	T_CanIns = GoodStr(T_im) || GoodStr(T_ic) || GoodStr(T_IC);
	T_CanUP = GoodStr(T_up) || GoodStr(T_UP);
	return;
    }

    /*
     * Do the numeric ones second
     */
    for (tv = tval; tv->name != NULL; tv++)
	if (strcmp(tv->name, what) == 0)
	    break;

    if (tv->name != NULL) {
	if (tv == &tval[T_pt] || tv == &tval[T_km] || 
	    tv == &tval[T_am] || tv == &tval[T_xn]) {
	    if (strcmp(how, "yes") == 0)
		tv->val = 1;
	    else if (strcmp(how, "no") == 0)
		tv->val = 0;
	    else {
		stderror(ERR_SETTCUS, tv->name);
		return;
	    }
	    T_Tabs = Val(T_pt);
	    T_HasMeta = Val(T_km);
	    T_Margin = Val(T_am) ? MARGIN_AUTO : 0;
	    T_Margin |= Val(T_xn) ? MARGIN_MAGIC : 0;
	    if (tv == &tval[T_am] || tv == &tval[T_xn]) 
		ChangeSize(Val(T_li), Val(T_co));
	    return;
	}
	else {
	    tv->val = atoi(how);
	    T_Cols = (Char) Val(T_co);
	    T_Lines = (Char) Val(T_li);
	    if (tv == &tval[T_co] || tv == &tval[T_li])
		ChangeSize(Val(T_li), Val(T_co));
	    return;
	}
    }
    stderror(ERR_NAME | ERR_TCCAP, what);
    return;
}


/*
 * Print the termcap string out with variable substitution
 */
void
EchoTC(Char **v)
{
    char   *cap, *scap, *cv;
    int     arg_need, arg_cols, arg_rows;
    int     verbose = 0, silent = 0;
    char   *area;
    static const char fmts[] = "%s\n", fmtd[] = "%d\n";
    struct termcapstr *t;
    char    buf[TC_BUFSIZE];
    Char **globbed;

    area = buf;

    setname("echotc");

    v = glob_all_or_error(v);
    globbed = v;
    cleanup_push(globbed, blk_cleanup);

    if (!*v || *v[0] == '\0')
	goto end;
    if (v[0][0] == '-') {
	switch (v[0][1]) {
	case 'v':
	    verbose = 1;
	    break;
	case 's':
	    silent = 1;
	    break;
	default:
	    stderror(ERR_NAME | ERR_TCUSAGE);
	    break;
	}
	v++;
    }
    if (!*v || *v[0] == '\0')
	goto end;
    cv = strsave(short2str(*v));
    cleanup_push(cv, xfree);
    if (strcmp(cv, "tabs") == 0) {
	xprintf(fmts, T_Tabs ? CGETS(7, 14, "yes") :
		CGETS(7, 15, "no"));
	goto end_flush;
    }
    else if (strcmp(cv, "meta") == 0) {
	xprintf(fmts, Val(T_km) ? CGETS(7, 14, "yes") :
		CGETS(7, 15, "no"));
	goto end_flush;
    }
    else if (strcmp(cv, "xn") == 0) {
	xprintf(fmts, T_Margin & MARGIN_MAGIC ? CGETS(7, 14, "yes") :
		CGETS(7, 15,  "no"));
	goto end_flush;
    }
    else if (strcmp(cv, "am") == 0) {
	xprintf(fmts, T_Margin & MARGIN_AUTO ? CGETS(7, 14, "yes") :
		CGETS(7, 15, "no"));
	goto end_flush;
    }
    else if (strcmp(cv, "baud") == 0) {
	int     i;

	for (i = 0; baud_rate[i].b_name != NULL; i++)
	    if (T_Speed == baud_rate[i].b_rate) {
		xprintf(fmts, baud_rate[i].b_name);
		goto end_flush;
	    }
	xprintf(fmtd, 0);
	goto end_flush;
    }
    else if (strcmp(cv, "rows") == 0 || strcmp(cv, "lines") == 0 ||
	strcmp(cv, "li") == 0) {
	xprintf(fmtd, Val(T_li));
	goto end_flush;
    }
    else if (strcmp(cv, "cols") == 0 || strcmp(cv, "co") == 0) {
	xprintf(fmtd, Val(T_co));
	goto end_flush;
    }

    /* 
     * Try to use our local definition first
     */
    scap = NULL;
    for (t = tstr; t->name != NULL; t++)
	if (strcmp(t->name, cv) == 0) {
	    scap = t->str;
	    break;
	}
    if (t->name == NULL)
	scap = tgetstr(cv, &area);
    if (!scap || scap[0] == '\0') {
	if (tgetflag(cv)) {
	    xprintf("%s", CGETS(7, 14, "yes\n"));
	    goto end;
	}
	if (silent)
	    goto end;
	else
	    stderror(ERR_NAME | ERR_TCCAP, cv);
    }

    /*
     * Count home many values we need for this capability.
     */
    for (cap = scap, arg_need = 0; *cap; cap++)
	if (*cap == '%')
	    switch (*++cap) {
	    case 'd':
	    case '2':
	    case '3':
	    case '.':
	    case '+':
		arg_need++;
		break;
	    case '%':
	    case '>':
	    case 'i':
	    case 'r':
	    case 'n':
	    case 'B':
	    case 'D':
		break;
	    default:
		/*
		 * hpux has lot's of them...
		 */
		if (verbose)
		    stderror(ERR_NAME | ERR_TCPARM, *cap);
		/* This is bad, but I won't complain */
		break;
	    }

    switch (arg_need) {
    case 0:
	v++;
	if (*v && *v[0]) {
	    if (silent)
		goto end;
	    else
		stderror(ERR_NAME | ERR_TCARGS, cv, arg_need);
	}
	(void) tputs(scap, 1, PUTRAW);
	break;
    case 1:
	v++;
	if (!*v || *v[0] == '\0')
	    stderror(ERR_NAME | ERR_TCNARGS, cv, 1);
	arg_cols = 0;
	arg_rows = atoi(short2str(*v));
	v++;
	if (*v && *v[0]) {
	    if (silent)
		goto end;
	    else
		stderror(ERR_NAME | ERR_TCARGS, cv, arg_need);
	}
	(void) tputs(tgoto(scap, arg_cols, arg_rows), 1, PUTRAW);
	break;
    default:
	/* This is wrong, but I will ignore it... */
	if (verbose)
	    stderror(ERR_NAME | ERR_TCARGS, cv, arg_need);
	/*FALLTHROUGH*/
    case 2:
	v++;
	if (!*v || *v[0] == '\0') {
	    if (silent)
		goto end;
	    else
		stderror(ERR_NAME | ERR_TCNARGS, cv, 2);
	}
	arg_cols = atoi(short2str(*v));
	v++;
	if (!*v || *v[0] == '\0') {
	    if (silent)
		goto end;
	    else
		stderror(ERR_NAME | ERR_TCNARGS, cv, 2);
	}
	arg_rows = atoi(short2str(*v));
	v++;
	if (*v && *v[0]) {
	    if (silent)
		goto end;
	    else
		stderror(ERR_NAME | ERR_TCARGS, cv, arg_need);
	}
	(void) tputs(tgoto(scap, arg_cols, arg_rows), arg_rows, PUTRAW);
	break;
    }
 end_flush:
    flush();
 end:
    cleanup_until(globbed);
}

int    GotTermCaps = 0;

static struct {
    Char   *name;
    int     key;
    XmapVal fun;
    int	    type;
} arrow[] = {
#define A_K_DN	0
    { STRdown,	T_kd, { 0 }, 0 },
#define A_K_UP	1
    { STRup,	T_ku, { 0 }, 0 },
#define A_K_LT	2
    { STRleft,	T_kl, { 0 }, 0 },
#define A_K_RT	3
    { STRright, T_kr, { 0 }, 0 },
#define A_K_HO  4
    { STRhome,  T_kh, { 0 }, 0 },
#define A_K_EN  5
    { STRend,   T_at7, { 0 }, 0}
};
#define A_K_NKEYS 6

void
ResetArrowKeys(void)
{
    arrow[A_K_DN].fun.cmd = F_DOWN_HIST;
    arrow[A_K_DN].type    = XK_CMD;

    arrow[A_K_UP].fun.cmd = F_UP_HIST;
    arrow[A_K_UP].type    = XK_CMD;

    arrow[A_K_LT].fun.cmd = F_CHARBACK;
    arrow[A_K_LT].type    = XK_CMD;

    arrow[A_K_RT].fun.cmd = F_CHARFWD;
    arrow[A_K_RT].type    = XK_CMD;

    arrow[A_K_HO].fun.cmd = F_TOBEG;
    arrow[A_K_HO].type    = XK_CMD;

    arrow[A_K_EN].fun.cmd = F_TOEND;
    arrow[A_K_EN].type    = XK_CMD;
}

void
DefaultArrowKeys(void)
{
    static Char strA[] = {033, '[', 'A', '\0'};
    static Char strB[] = {033, '[', 'B', '\0'};
    static Char strC[] = {033, '[', 'C', '\0'};
    static Char strD[] = {033, '[', 'D', '\0'};
    static Char strH[] = {033, '[', 'H', '\0'};
    static Char strF[] = {033, '[', 'F', '\0'};
    static Char stOA[] = {033, 'O', 'A', '\0'};
    static Char stOB[] = {033, 'O', 'B', '\0'};
    static Char stOC[] = {033, 'O', 'C', '\0'};
    static Char stOD[] = {033, 'O', 'D', '\0'};
    static Char stOH[] = {033, 'O', 'H', '\0'};
    static Char stOF[] = {033, 'O', 'F', '\0'};

    CStr cs;
#ifndef IS_ASCII
    if (strA[0] == 033)
    {
	strA[0] = CTL_ESC('\033');
	strB[0] = CTL_ESC('\033');
	strC[0] = CTL_ESC('\033');
	strD[0] = CTL_ESC('\033');
	strH[0] = CTL_ESC('\033');
	strF[0] = CTL_ESC('\033');
	stOA[0] = CTL_ESC('\033');
	stOB[0] = CTL_ESC('\033');
	stOC[0] = CTL_ESC('\033');
	stOD[0] = CTL_ESC('\033');
	stOH[0] = CTL_ESC('\033');
	stOF[0] = CTL_ESC('\033');
    }
#endif

    cs.len = 3;

    cs.buf = strA; AddXkey(&cs, &arrow[A_K_UP].fun, arrow[A_K_UP].type);
    cs.buf = strB; AddXkey(&cs, &arrow[A_K_DN].fun, arrow[A_K_DN].type);
    cs.buf = strC; AddXkey(&cs, &arrow[A_K_RT].fun, arrow[A_K_RT].type);
    cs.buf = strD; AddXkey(&cs, &arrow[A_K_LT].fun, arrow[A_K_LT].type);
    cs.buf = strH; AddXkey(&cs, &arrow[A_K_HO].fun, arrow[A_K_HO].type);
    cs.buf = strF; AddXkey(&cs, &arrow[A_K_EN].fun, arrow[A_K_EN].type);
    cs.buf = stOA; AddXkey(&cs, &arrow[A_K_UP].fun, arrow[A_K_UP].type);
    cs.buf = stOB; AddXkey(&cs, &arrow[A_K_DN].fun, arrow[A_K_DN].type);
    cs.buf = stOC; AddXkey(&cs, &arrow[A_K_RT].fun, arrow[A_K_RT].type);
    cs.buf = stOD; AddXkey(&cs, &arrow[A_K_LT].fun, arrow[A_K_LT].type);
    cs.buf = stOH; AddXkey(&cs, &arrow[A_K_HO].fun, arrow[A_K_HO].type);
    cs.buf = stOF; AddXkey(&cs, &arrow[A_K_EN].fun, arrow[A_K_EN].type);

    if (VImode) {
	cs.len = 2;
	cs.buf = &strA[1]; AddXkey(&cs, &arrow[A_K_UP].fun, arrow[A_K_UP].type);
	cs.buf = &strB[1]; AddXkey(&cs, &arrow[A_K_DN].fun, arrow[A_K_DN].type);
	cs.buf = &strC[1]; AddXkey(&cs, &arrow[A_K_RT].fun, arrow[A_K_RT].type);
	cs.buf = &strD[1]; AddXkey(&cs, &arrow[A_K_LT].fun, arrow[A_K_LT].type);
	cs.buf = &strH[1]; AddXkey(&cs, &arrow[A_K_HO].fun, arrow[A_K_HO].type);
	cs.buf = &strF[1]; AddXkey(&cs, &arrow[A_K_EN].fun, arrow[A_K_EN].type);
	cs.buf = &stOA[1]; AddXkey(&cs, &arrow[A_K_UP].fun, arrow[A_K_UP].type);
	cs.buf = &stOB[1]; AddXkey(&cs, &arrow[A_K_DN].fun, arrow[A_K_DN].type);
	cs.buf = &stOC[1]; AddXkey(&cs, &arrow[A_K_RT].fun, arrow[A_K_RT].type);
	cs.buf = &stOD[1]; AddXkey(&cs, &arrow[A_K_LT].fun, arrow[A_K_LT].type);
	cs.buf = &stOH[1]; AddXkey(&cs, &arrow[A_K_HO].fun, arrow[A_K_HO].type);
	cs.buf = &stOF[1]; AddXkey(&cs, &arrow[A_K_EN].fun, arrow[A_K_EN].type);
    }
}


int
SetArrowKeys(const CStr *name, XmapVal *fun, int type)
{
    int i;
    for (i = 0; i < A_K_NKEYS; i++)
	if (Strcmp(name->buf, arrow[i].name) == 0) {
	    arrow[i].fun  = *fun;
	    arrow[i].type = type;
	    return 0;
	}
    return -1;
}

int
IsArrowKey(Char *name)
{
    int i;
    for (i = 0; i < A_K_NKEYS; i++)
	if (Strcmp(name, arrow[i].name) == 0)
	    return 1;
    return 0;
}

int
ClearArrowKeys(const CStr *name)
{
    int i;
    for (i = 0; i < A_K_NKEYS; i++)
	if (Strcmp(name->buf, arrow[i].name) == 0) {
	    arrow[i].type = XK_NOD;
	    return 0;
	}
    return -1;
}

void
PrintArrowKeys(const CStr *name)
{
    int i;

    for (i = 0; i < A_K_NKEYS; i++)
	if (name->len == 0 || Strcmp(name->buf, arrow[i].name) == 0)
	    if (arrow[i].type != XK_NOD)
		printOne(arrow[i].name, &arrow[i].fun, arrow[i].type);
}


void
BindArrowKeys(void)
{
    KEYCMD *map, *dmap;
    int     i, j;
    char   *p;
    CStr    cs;

    if (!GotTermCaps)
	return;
    map = VImode ? CcAltMap : CcKeyMap;
    dmap = VImode ? CcViCmdMap : CcEmacsMap;

    DefaultArrowKeys();

    for (i = 0; i < A_K_NKEYS; i++) {
	p = tstr[arrow[i].key].str;
	if (p && *p) {
	    j = (unsigned char) *p;
	    cs.buf = str2short(p);
	    cs.len = Strlen(cs.buf);
	    /*
	     * Assign the arrow keys only if:
	     *
	     * 1. They are multi-character arrow keys and the user 
	     *    has not re-assigned the leading character, or 
	     *    has re-assigned the leading character to be F_XKEY
	     * 2. They are single arrow keys pointing to an unassigned key.
	     */
	    if (arrow[i].type == XK_NOD) {
		ClearXkey(map, &cs);
	    }
	    else {
		if (p[1] && (dmap[j] == map[j] || map[j] == F_XKEY)) {
		    AddXkey(&cs, &arrow[i].fun, arrow[i].type);
		    map[j] = F_XKEY;
		}
		else if (map[j] == F_UNASSIGNED) {
		    ClearXkey(map, &cs);
		    if (arrow[i].type == XK_CMD)
			map[j] = arrow[i].fun.cmd;
		    else
			AddXkey(&cs, &arrow[i].fun, arrow[i].type);
		}
	    }
	}
    }
}

static Char cur_atr = 0;	/* current attributes */

void
SetAttributes(Char atr)
{
    atr &= ATTRIBUTES;
    if (atr != cur_atr) {
	if (me_all && GoodStr(T_me)) {
	    if (((cur_atr & BOLD) && !(atr & BOLD)) ||
		((cur_atr & UNDER) && !(atr & UNDER)) ||
		((cur_atr & STANDOUT) && !(atr & STANDOUT))) {
		(void) tputs(Str(T_me), 1, PUTPURE);
		cur_atr = 0;
	    }
	}
	if ((atr & BOLD) != (cur_atr & BOLD)) {
	    if (atr & BOLD) {
		if (GoodStr(T_md) && GoodStr(T_me)) {
		    (void) tputs(Str(T_md), 1, PUTPURE);
		    cur_atr |= BOLD;
		}
	    }
	    else {
		if (GoodStr(T_md) && GoodStr(T_me)) {
		    (void) tputs(Str(T_me), 1, PUTPURE);
		    if ((cur_atr & STANDOUT) && GoodStr(T_se)) {
			(void) tputs(Str(T_se), 1, PUTPURE);
			cur_atr &= ~STANDOUT;
		    }
		    if ((cur_atr & UNDER) && GoodStr(T_ue)) {
			(void) tputs(Str(T_ue), 1, PUTPURE);
			cur_atr &= ~UNDER;
		    }
		    cur_atr &= ~BOLD;
		}
	    }
	}
	if ((atr & STANDOUT) != (cur_atr & STANDOUT)) {
	    if (atr & STANDOUT) {
		if (GoodStr(T_so) && GoodStr(T_se)) {
		    (void) tputs(Str(T_so), 1, PUTPURE);
		    cur_atr |= STANDOUT;
		}
	    }
	    else {
		if (GoodStr(T_se)) {
		    (void) tputs(Str(T_se), 1, PUTPURE);
		    cur_atr &= ~STANDOUT;
		}
	    }
	}
	if ((atr & UNDER) != (cur_atr & UNDER)) {
	    if (atr & UNDER) {
		if (GoodStr(T_us) && GoodStr(T_ue)) {
		    (void) tputs(Str(T_us), 1, PUTPURE);
		    cur_atr |= UNDER;
		}
	    }
	    else {
		if (GoodStr(T_ue)) {
		    (void) tputs(Str(T_ue), 1, PUTPURE);
		    cur_atr &= ~UNDER;
		}
	    }
	}
    }
}

int highlighting = 0;

void
StartHighlight(void)
{
    (void) tputs(Str(T_mr), 1, PUTPURE);
    highlighting = 1;
}

void
StopHighlight(void)
{
    (void) tputs(Str(T_me), 1, PUTPURE);
    highlighting = 0;
}

/* PWP 6-27-88 -- if the tty driver thinks that we can tab, we ask termcap */
int
CanWeTab(void)
{
    return (Val(T_pt));
}

/* move to line <where> (first line == 0) as efficiently as possible; */
void
MoveToLine(int where)		
{
    int     del;

    if (where == CursorV)
	return;

    if (where > TermV) {
#ifdef DEBUG_SCREEN
	xprintf("MoveToLine: where is ridiculous: %d\r\n", where);
	flush();
#endif /* DEBUG_SCREEN */
	return;
    }

    del = where - CursorV;

    if (del > 0) {
	while (del > 0) {
	    if ((T_Margin & MARGIN_AUTO) && Display[CursorV][0] != '\0') {
		size_t h;

		for (h = TermH - 1; h > 0 && Display[CursorV][h] == CHAR_DBWIDTH;
		     h--)
		    ;
		/* move without newline */
		MoveToChar(h);
		so_write(&Display[CursorV][CursorH], TermH - CursorH); /* updates CursorH/V*/
		del--;
	    }
	    else {
		if ((del > 1) && GoodStr(T_DO)) {
		    (void) tputs(tgoto(Str(T_DO), del, del), del, PUTPURE);
		    del = 0;
		}
		else {
		    for ( ; del > 0; del--) 
			(void) putraw('\n');
		    CursorH = 0;	/* because the \n will become \r\n */
		}
	    }
	}
    }
    else {			/* del < 0 */
	if (GoodStr(T_UP) && (-del > 1 || !GoodStr(T_up)))
	    (void) tputs(tgoto(Str(T_UP), -del, -del), -del, PUTPURE);
	else {
	    int i;
	    if (GoodStr(T_up))
		for (i = 0; i < -del; i++)
		    (void) tputs(Str(T_up), 1, PUTPURE);
	}
    }
    CursorV = where;		/* now where is here */
}

void
MoveToChar(int where)		/* move to character position (where) */
{				/* as efficiently as possible */
    int     del;

mc_again:
    if (where == CursorH)
	return;

    if (where >= TermH) {
#ifdef DEBUG_SCREEN
	xprintf("MoveToChar: where is riduculous: %d\r\n", where);
	flush();
#endif /* DEBUG_SCREEN */
	return;
    }

    if (!where) {		/* if where is first column */
	(void) putraw('\r');	/* do a CR */
	CursorH = 0;
	return;
    }

    del = where - CursorH;

    if ((del < -4 || del > 4) && GoodStr(T_ch))
	/* go there directly */
	(void) tputs(tgoto(Str(T_ch), where, where), where, PUTPURE);
    else {
	int i;
	if (del > 0) {		/* moving forward */
	    if ((del > 4) && GoodStr(T_RI))
		(void) tputs(tgoto(Str(T_RI), del, del), del, PUTPURE);
	    else {
		/* if I can do tabs, use them */
		if (T_Tabs) {
		    if ((CursorH & 0370) != (where & ~0x7)
			&& Display[CursorV][where & ~0x7] != CHAR_DBWIDTH) {
			/* if not within tab stop */
			for (i = (CursorH & 0370); i < (where & ~0x7); i += 8)
			    (void) putraw('\t');	/* then tab over */
			CursorH = where & ~0x7;
			/* Note: considering that we often want to go to
			   TermH - 1 for the wrapping, it would be nice to
			   optimize this case by tabbing to the last column
			   - but this doesn't work for all terminals! */
		    }
		}
		/* it's usually cheaper to just write the chars, so we do. */

		/* NOTE THAT so_write() WILL CHANGE CursorH!!! */
		so_write(&Display[CursorV][CursorH], where - CursorH);

	    }
	}
	else {			/* del < 0 := moving backward */
	    if ((-del > 4) && GoodStr(T_LE))
		(void) tputs(tgoto(Str(T_LE), -del, -del), -del, PUTPURE);
	    else {		/* can't go directly there */
		/* if the "cost" is greater than the "cost" from col 0 */
		if (T_Tabs ? (-del > ((where >> 3) + (where & 07)))
		    : (-del > where)) {
		    (void) putraw('\r');	/* do a CR */
		    CursorH = 0;
		    goto mc_again;	/* and try again */
		}
		for (i = 0; i < -del; i++)
		    (void) putraw('\b');
	    }
	}
    }
    CursorH = where;		/* now where is here */
}

void
so_write(Char *cp, int n)
{
    int cur_pos, prompt_len = 0, region_start = 0, region_end = 0;

    if (n <= 0)
	return;			/* catch bugs */

    if (n > TermH) {
#ifdef DEBUG_SCREEN
	xprintf("so_write: n is riduculous: %d\r\n", n);
	flush();
#endif /* DEBUG_SCREEN */
	return;
    }

    if (adrof(STRhighlight)) {
	/* find length of prompt */
	Char *promptc;
	for (promptc = Prompt; *promptc; promptc++);
	prompt_len = promptc - Prompt;

	/* find region start and end points */
	if (IncMatchLen) {
	    region_start = (Cursor - InputBuf) + prompt_len;
	    region_end = region_start + IncMatchLen;
	} else if (MarkIsSet) {
	    region_start = (min(Cursor, Mark) - InputBuf) + prompt_len;
	    region_end   = (max(Cursor, Mark) - InputBuf) + prompt_len;
	}
    }

    do {
	if (adrof(STRhighlight)) {
	    cur_pos = CursorV * TermH + CursorH;
	    if (!highlighting &&
		cur_pos >= region_start && cur_pos < region_end)
		StartHighlight();
	    else if (highlighting && cur_pos >= region_end)
		StopHighlight();

	    /* don't highlight over the cursor. the highlighting's reverse
	     * video would cancel it out. :P */
	    if (highlighting && cur_pos == (Cursor - InputBuf) + prompt_len)
		StopHighlight();
	}

	if (*cp != CHAR_DBWIDTH) {
	    if (*cp & LITERAL) {
		Char   *d;
#ifdef DEBUG_LITERAL
		xprintf("so: litnum %d\r\n", (int)(*cp & ~LITERAL));
#endif /* DEBUG_LITERAL */
		for (d = litptr + (*cp & ~LITERAL) * LIT_FACTOR; *d; d++)
		    (void) putwraw(*d);
	    }
	    else
		(void) putwraw(*cp);
	}
	cp++;
	CursorH++;
    } while (--n);

    if (adrof(STRhighlight) && highlighting)
	StopHighlight();

    if (CursorH >= TermH) { /* wrap? */
	if (T_Margin & MARGIN_AUTO) { /* yes */
	    CursorH = 0;
	    CursorV++;
	    if (T_Margin & MARGIN_MAGIC) {
		/* force the wrap to avoid the "magic" situation */
		Char xc;
		if ((xc = Display[CursorV][CursorH]) != '\0') {
		    so_write(&xc, 1);
		    while(Display[CursorV][CursorH] == CHAR_DBWIDTH)
			CursorH++;
		}
		else {
		    (void) putraw(' ');
		    CursorH = 1;
		}
	    }
	}
	else			/* no wrap, but cursor stays on screen */
	    CursorH = TermH - 1;
    }
}


void
DeleteChars(int num)		/* deletes <num> characters */
{
    if (num <= 0)
	return;

    if (!T_CanDel) {
#ifdef DEBUG_EDIT
	xprintf(CGETS(7, 16, "ERROR: cannot delete\r\n"));
#endif /* DEBUG_EDIT */
	flush();
	return;
    }

    if (num > TermH) {
#ifdef DEBUG_SCREEN
	xprintf(CGETS(7, 17, "DeleteChars: num is riduculous: %d\r\n"), num);
	flush();
#endif /* DEBUG_SCREEN */
	return;
    }

    if (GoodStr(T_DC))		/* if I have multiple delete */
	if ((num > 1) || !GoodStr(T_dc)) {	/* if dc would be more expen. */
	    (void) tputs(tgoto(Str(T_DC), num, num), num, PUTPURE);
	    return;
	}

    if (GoodStr(T_dm))		/* if I have delete mode */
	(void) tputs(Str(T_dm), 1, PUTPURE);

    if (GoodStr(T_dc))		/* else do one at a time */
	while (num--)
	    (void) tputs(Str(T_dc), 1, PUTPURE);

    if (GoodStr(T_ed))		/* if I have delete mode */
	(void) tputs(Str(T_ed), 1, PUTPURE);
}

/* Puts terminal in insert character mode, or inserts num characters in the
   line */
void
Insert_write(Char *cp, int num)
{
    if (num <= 0)
	return;
    if (!T_CanIns) {
#ifdef DEBUG_EDIT
	xprintf(CGETS(7, 18, "ERROR: cannot insert\r\n"));
#endif /* DEBUG_EDIT */
	flush();
	return;
    }

    if (num > TermH) {
#ifdef DEBUG_SCREEN
	xprintf(CGETS(7, 19, "StartInsert: num is riduculous: %d\r\n"), num);
	flush();
#endif /* DEBUG_SCREEN */
	return;
    }

    if (GoodStr(T_IC))		/* if I have multiple insert */
	if ((num > 1) || !GoodStr(T_ic)) {	/* if ic would be more expen. */
	    (void) tputs(tgoto(Str(T_IC), num, num), num, PUTPURE);
	    so_write(cp, num);	/* this updates CursorH/V */
	    return;
	}

    if (GoodStr(T_im) && GoodStr(T_ei)) { /* if I have insert mode */
	(void) tputs(Str(T_im), 1, PUTPURE);

	so_write(cp, num);	/* this updates CursorH/V */

	if (GoodStr(T_ip))	/* have to make num chars insert */
	    (void) tputs(Str(T_ip), 1, PUTPURE);

	(void) tputs(Str(T_ei), 1, PUTPURE);
	return;
    }

    do {
	if (GoodStr(T_ic))	/* have to make num chars insert */
	    (void) tputs(Str(T_ic), 1, PUTPURE);	/* insert a char */

	so_write(cp++, 1);	/* this updates CursorH/V */

	if (GoodStr(T_ip))	/* have to make num chars insert */
	    (void) tputs(Str(T_ip), 1, PUTPURE);/* pad the inserted char */

    } while (--num);

}

/* clear to end of line.  There are num characters to clear */
void
ClearEOL(int num)
{
    int i;

    if (num <= 0)
	return;

    if (T_CanCEOL && GoodStr(T_ce))
	(void) tputs(Str(T_ce), 1, PUTPURE);
    else {
	for (i = 0; i < num; i++)
	    (void) putraw(' ');
	CursorH += num;		/* have written num spaces */
    }
}

void
ClearScreen(void)
{				/* clear the whole screen and home */
    if (GoodStr(T_cl))
	/* send the clear screen code */
	(void) tputs(Str(T_cl), Val(T_li), PUTPURE);
    else if (GoodStr(T_ho) && GoodStr(T_cd)) {
	(void) tputs(Str(T_ho), Val(T_li), PUTPURE);	/* home */
	/* clear to bottom of screen */
	(void) tputs(Str(T_cd), Val(T_li), PUTPURE);
    }
    else {
	(void) putraw('\r');
	(void) putraw('\n');
    }
}

void
SoundBeep(void)
{				/* produce a sound */
    beep_cmd ();
    if (adrof(STRnobeep))
	return;

    if (GoodStr(T_vb) && adrof(STRvisiblebell))
	(void) tputs(Str(T_vb), 1, PUTPURE);	/* visible bell */
    else if (GoodStr(T_bl))
	/* what termcap says we should use */
	(void) tputs(Str(T_bl), 1, PUTPURE);
    else
	(void) putraw(CTL_ESC('\007'));	/* an ASCII bell; ^G */
}

void
ClearToBottom(void)
{				/* clear to the bottom of the screen */
    if (GoodStr(T_cd))
	(void) tputs(Str(T_cd), Val(T_li), PUTPURE);
    else if (GoodStr(T_ce))
	(void) tputs(Str(T_ce), Val(T_li), PUTPURE);
}

void
GetTermCaps(void)
{				/* read in the needed terminal capabilites */
    int i;
    const char   *ptr;
    char    buf[TC_BUFSIZE];
    static char bp[TC_BUFSIZE];
    char   *area;
    struct termcapstr *t;


#ifdef SIG_WINDOW
    sigset_t oset, set;
    int     lins, cols;

    /* don't want to confuse things here */
    sigemptyset(&set);
    sigaddset(&set, SIG_WINDOW);
    (void)sigprocmask(SIG_BLOCK, &set, &oset);
    cleanup_push(&oset, sigprocmask_cleanup);
#endif /* SIG_WINDOW */
    area = buf;

    GotTermCaps = 1;

    setname("gettermcaps");
    ptr = getenv("TERM");

#ifdef apollo
    /* 
     * If we are on a pad, we pretend that we are dumb. Otherwise the termcap
     * library will put us in a weird screen mode, thinking that we are going
     * to use curses
     */
    if (isapad())
	ptr = "dumb";
#endif /* apollo */

    if (!ptr || !ptr[0] || !strcmp(ptr, "wm") || !strcmp(ptr,"dmx"))
	ptr = "dumb";

    setzero(bp, TC_BUFSIZE);

    i = tgetent(bp, ptr);
    if (i <= 0) {
	if (i == -1) {
#if (SYSVREL == 0) || defined(IRIS3D)
	    xprintf(CGETS(7, 20,
		"%s: The terminal database could not be opened.\n"), progname);
	}
	else if (i == 0) {
#endif /* SYSVREL */
	    xprintf(CGETS(7, 21,
			  "%s: No entry for terminal type \"%s\"\n"), progname,
		    getenv("TERM"));
	}
	xprintf(CGETS(7, 22, "%s: using dumb terminal settings.\n"), progname);
	Val(T_co) = 80;		/* do a dumb terminal */
	Val(T_pt) = Val(T_km) = Val(T_li) = 0;
	for (t = tstr; t->name != NULL; t++)
	    TCset(t, NULL);
    }
    else {
	/* Can we tab */
	Val(T_pt) = tgetflag("pt") && !tgetflag("xt");
	/* do we have a meta? */
	Val(T_km) = (tgetflag("km") || tgetflag("MT"));
	Val(T_am) = tgetflag("am");
	Val(T_xn) = tgetflag("xn");
	Val(T_co) = tgetnum("co");
	Val(T_li) = tgetnum("li");
	for (t = tstr; t->name != NULL; t++)
	    TCset(t, tgetstr(t->name, &area));
    }
    if (Val(T_co) < 2)
	Val(T_co) = 80;		/* just in case */
    if (Val(T_li) < 1)
	Val(T_li) = 24;

    T_Cols = (Char) Val(T_co);
    T_Lines = (Char) Val(T_li);
    if (T_Tabs)
	T_Tabs = Val(T_pt);
    T_HasMeta = Val(T_km);
    T_Margin = Val(T_am) ? MARGIN_AUTO : 0;
    T_Margin |= Val(T_xn) ? MARGIN_MAGIC : 0;
    T_CanCEOL = GoodStr(T_ce);
    T_CanDel = GoodStr(T_dc) || GoodStr(T_DC);
    T_CanIns = GoodStr(T_im) || GoodStr(T_ic) || GoodStr(T_IC);
    T_CanUP = GoodStr(T_up) || GoodStr(T_UP);
    if (GoodStr(T_me) && GoodStr(T_ue))
	me_all = (strcmp(Str(T_me), Str(T_ue)) == 0);
    else
	me_all = 0;
    if (GoodStr(T_me) && GoodStr(T_se))
	me_all |= (strcmp(Str(T_me), Str(T_se)) == 0);


#ifdef DEBUG_SCREEN
    if (!T_CanUP) {
	xprintf(CGETS(7, 23, "%s: WARNING: Your terminal cannot move up.\n",
		progname));
	xprintf(CGETS(7, 24, "Editing may be odd for long lines.\n"));
    }
    if (!T_CanCEOL)
	xprintf(CGETS(7, 25, "no clear EOL capability.\n"));
    if (!T_CanDel)
	xprintf(CGETS(7, 26, "no delete char capability.\n"));
    if (!T_CanIns)
	xprintf(CGETS(7, 27, "no insert char capability.\n"));
#endif /* DEBUG_SCREEN */



#ifdef SIG_WINDOW
    (void) GetSize(&lins, &cols);	/* get the correct window size */
    ChangeSize(lins, cols);

    cleanup_until(&oset);		/* can change it again */
#else /* SIG_WINDOW */
    ChangeSize(Val(T_li), Val(T_co));
#endif /* SIG_WINDOW */

    BindArrowKeys();
}

#ifdef SIG_WINDOW
/* GetSize():
 *	Return the new window size in lines and cols, and
 *	true if the size was changed. This can fail if SHIN
 *	is not a tty, but it will work in most cases.
 */
int
GetSize(int *lins, int *cols)
{
    *cols = Val(T_co);
    *lins = Val(T_li);

#ifdef TIOCGWINSZ
# define KNOWsize
# ifndef lint
    {
	struct winsize ws;	/* from 4.3 */

	if (ioctl(SHIN, TIOCGWINSZ, (ioctl_t) &ws) != -1) {
	    if (ws.ws_col)
		*cols = ws.ws_col;
	    if (ws.ws_row)
		*lins = ws.ws_row;
	}
    }
# endif /* !lint */
#else /* TIOCGWINSZ */
# ifdef TIOCGSIZE
#  define KNOWsize
    {
	struct ttysize ts;	/* from Sun */

	if (ioctl(SHIN, TIOCGSIZE, (ioctl_t) &ts) != -1) {
	    if (ts.ts_cols)
		*cols = ts.ts_cols;
	    if (ts.ts_lines)
		*lins = ts.ts_lines;
	}
    }
# endif /* TIOCGSIZE */
#endif /* TIOCGWINSZ */

    return (Val(T_co) != *cols || Val(T_li) != *lins);
}

#endif /* SIG_WINDOW */

#ifdef KNOWsize
static int
UpdateVal(const Char *tag, int value, Char *termcap, Char *backup)
{
    Char *ptr, *p;
    if ((ptr = Strstr(termcap, tag)) == NULL) {
	(void)Strcpy(backup, termcap);
	return 0;
    } else {
	size_t len = (ptr - termcap) + Strlen(tag);
	(void)Strncpy(backup, termcap, len);
	backup[len] = '\0';
	p = Itoa(value, 0, 0);
	(void) Strcat(backup + len, p);
	xfree(p);
	ptr = Strchr(ptr, ':');
	if (ptr)
	    (void) Strcat(backup, ptr);
	return 1;
    }
}
#endif

void
ChangeSize(int lins, int cols)
{
    /*
     * Just in case
     */
    Val(T_co) = (cols < 2) ? 80 : cols;
    Val(T_li) = (lins < 1) ? 24 : lins;

#ifdef KNOWsize
    /*
     * We want to affect the environment only when we have a valid
     * setup, not when we get bad settings. Consider the following scenario:
     * We just logged in, and we have not initialized the editor yet.
     * We reset termcap with tset, and not $TERMCAP has the right
     * terminal size. But since the editor is not initialized yet, and
     * the kernel's notion of the terminal size might be wrong we arrive
     * here with lines = columns = 0. If we reset the environment we lose
     * our only chance to get the window size right.
     */
    if (Val(T_co) == cols && Val(T_li) == lins) {
	Char   *p;
	char   *tptr;

	if (getenv("COLUMNS")) {
	    p = Itoa(Val(T_co), 0, 0);
	    cleanup_push(p, xfree);
	    tsetenv(STRCOLUMNS, p);
	    cleanup_until(p);
	}

	if (getenv("LINES")) {
	    p = Itoa(Val(T_li), 0, 0);
	    cleanup_push(p, xfree);
	    tsetenv(STRLINES, p);
	    cleanup_until(p);
	}

	if ((tptr = getenv("TERMCAP")) != NULL) {
	    /* Leave 64 characters slop in case we enlarge the termcap string */
	    Char    termcap[TC_BUFSIZE+64], backup[TC_BUFSIZE+64], *ptr;
	    int changed;

	    ptr = str2short(tptr);
	    (void) Strncpy(termcap, ptr, TC_BUFSIZE);
	    termcap[TC_BUFSIZE-1] = '\0';

	    changed = UpdateVal(STRco, Val(T_co), termcap, backup);
	    changed |= UpdateVal(STRli, Val(T_li), termcap, backup);

	    if (changed) {
		/*
		 * Chop the termcap string at TC_BUFSIZE-1 characters to avoid
		 * core-dumps in the termcap routines
		 */
		termcap[TC_BUFSIZE - 1] = '\0';
		tsetenv(STRTERMCAP, termcap);
	    }
	}
    }
#endif /* KNOWsize */

    ReBufferDisplay();		/* re-make display buffers */
    ClearDisp();
}
