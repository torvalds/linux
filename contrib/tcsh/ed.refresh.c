/* $Header: /p/tcsh/cvsroot/tcsh/ed.refresh.c,v 3.51 2015/06/06 21:19:07 christos Exp $ */
/*
 * ed.refresh.c: Lower level screen refreshing functions
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

RCSID("$tcsh: ed.refresh.c,v 3.51 2015/06/06 21:19:07 christos Exp $")

#include "ed.h"
/* #define DEBUG_UPDATE */
/* #define DEBUG_REFRESH */
/* #define DEBUG_LITERAL */

/* refresh.c -- refresh the current set of lines on the screen */

Char   *litptr;
static int vcursor_h, vcursor_v;
static int rprompt_h, rprompt_v;

static	int	MakeLiteral		(Char *, int, Char);
static	int	Draw 			(Char *, int, int);
static	void	Vdraw 			(Char, int);
static	void	RefreshPromptpart	(Char *);
static	void	update_line 		(Char *, Char *, int);
static	void	str_insert		(Char *, int, int, Char *, int);
static	void	str_delete		(Char *, int, int, int);
static	void	str_cp			(Char *, Char *, int);
#ifndef WINNT_NATIVE
static
#else
extern
#endif
	void    PutPlusOne      (Char, int);
static	void	cpy_pad_spaces		(Char *, Char *, int);
#if defined(DEBUG_UPDATE) || defined(DEBUG_REFRESH) || defined(DEBUG_LITERAL)
static	void	reprintf			(char *, ...);
#ifdef DEBUG_UPDATE
static	void	dprintstr		(char *, const Char *, const Char *);

static void
dprintstr(char *str, const Char *f, const Char *t)
{
    reprintf("%s:\"", str);
    while (f < t) {
	if (ASC(*f) & ~ASCII)
	  reprintf("[%x]", *f++);
	else
	  reprintf("%c", CTL_ESC(ASCII & ASC(*f++)));
    }
    reprintf("\"\r\n");
}
#endif /* DEBUG_UPDATE */

/* reprintf():
 *	Print to $DEBUGTTY, so that we can test editing on one pty, and 
 *      print debugging stuff on another. Don't interrupt the shell while
 *	debugging cause you'll mangle up the file descriptors!
 */
static void
reprintf(char *fmt, ...)
{
    static int fd = -1;
    char *dtty;

    if ((dtty = getenv("DEBUGTTY"))) {
	int o;
	va_list va;
	va_start(va, fmt);

	if (fd == -1)
	    fd = xopen(dtty, O_RDWR);
	o = SHOUT;
	flush();
	SHOUT = fd;
	xvprintf(fmt, va);
	va_end(va);
	flush();
	SHOUT = o;
    }
}
#endif  /* DEBUG_UPDATE || DEBUG_REFRESH || DEBUG_LITERAL */

static int litlen = 0, litalloc = 0;

static int MakeLiteral(Char *str, int len, Char addlit)
{
    int i, addlitlen = 0;
    Char *addlitptr = 0;
    if (addlit) {
	if ((addlit & LITERAL) != 0) {
	    addlitptr = litptr + (addlit & ~LITERAL) * LIT_FACTOR;
	    addlitlen = Strlen(addlitptr);
	} else {
	    addlitptr = &addlit;
	    addlitlen = 1;
	}
	for (i = 0; i < litlen; i += LIT_FACTOR)
	    if (!Strncmp(addlitptr, litptr + i, addlitlen) && !Strncmp(str, litptr + i + addlitlen, len) && litptr[i + addlitlen + len] == 0)
		return (i / LIT_FACTOR) | LITERAL;
    } else {
	addlitlen = 0;
	for (i = 0; i < litlen; i += LIT_FACTOR)
	    if (!Strncmp(str, litptr + i, len) && litptr[i + len] == 0)
		return (i / LIT_FACTOR) | LITERAL;
    }
    if (litlen + addlitlen + len + 1 + (LIT_FACTOR - 1) > litalloc) {
	Char *newlitptr;
	int add = 256;
	while (len + addlitlen + 1 + (LIT_FACTOR - 1) > add)
	    add *= 2;
	newlitptr = xrealloc(litptr, (litalloc + add) * sizeof(Char));
	if (!newlitptr)
	    return '?';
	litptr = newlitptr;
	litalloc += add;
	if (addlitptr && addlitptr != &addlit)
	    addlitptr = litptr + (addlit & ~LITERAL) * LIT_FACTOR;
    }
    i = litlen / LIT_FACTOR;
    if (i >= LITERAL || i == CHAR_DBWIDTH)
	return '?';
    if (addlitptr) {
	Strncpy(litptr + litlen, addlitptr, addlitlen);
	litlen += addlitlen;
    }
    Strncpy(litptr + litlen, str, len);
    litlen += len;
    do
	litptr[litlen++] = 0;
    while (litlen % LIT_FACTOR);
    return i | LITERAL;
}

/* draw char at cp, expand tabs, ctl chars */
static int
Draw(Char *cp, int nocomb, int drawPrompt)
{
    int w, i, lv, lh;
    Char c, attr;

#ifdef WIDE_STRINGS
    if (!drawPrompt) {			/* draw command-line */
	attr = 0;
	c = *cp;
    } else {				/* draw prompt */
	/* prompt with attributes(UNDER,BOLD,STANDOUT) */
	if (*cp & (UNDER | BOLD | STANDOUT)) {		/* *cp >= STANDOUT */

	    /* example)
	     * We can't distinguish whether (*cp=)0x02ffffff is
	     * U+02FFFFFF or U+00FFFFFF|STANDOUT.
	     * We handle as U+00FFFFFF|STANDOUT, only when drawing prompt. */
	    attr = (*cp & ATTRIBUTES);
	    /* ~(UNDER | BOLD | STANDOUT) = 0xf1ffffff */
	    c = *cp & ~(UNDER | BOLD | STANDOUT);

	    /* if c is ctrl code, we handle *cp as havnig no attributes */
	    if ((c < 0x20 && c >= 0) || c == 0x7f) {
		attr = 0;
		c = *cp;
	    }
	} else {			/* prompt without attributes */
	    attr = 0;
	    c = *cp;
	}
    }
#else
    attr = *cp & ~CHAR;
    c = *cp & CHAR;
#endif
    w = NLSClassify(c, nocomb, drawPrompt);
    switch (w) {
	case NLSCLASS_NL:
	    Vdraw('\0', 0);		/* assure end of line	 */
	    vcursor_h = 0;		/* reset cursor pos	 */
	    vcursor_v++;
	    break;
	case NLSCLASS_TAB:
	    do {
		Vdraw(' ', 1);
	    } while ((vcursor_h & 07) != 0);
	    break;
	case NLSCLASS_CTRL:
	    Vdraw('^' | attr, 1);
	    if (c == CTL_ESC('\177')) {
		Vdraw('?' | attr, 1);
	    } else {
#ifdef IS_ASCII
		/* uncontrolify it; works only for iso8859-1 like sets */
		Vdraw(c | 0100 | attr, 1);
#else
		Vdraw(_toebcdic[_toascii[c]|0100] | attr, 1);
#endif
	    }
	    break;
	case NLSCLASS_ILLEGAL:
	    Vdraw('\\' | attr, 1);
	    Vdraw((((c >> 6) & 7) + '0') | attr, 1);
	    Vdraw((((c >> 3) & 7) + '0') | attr, 1);
	    Vdraw(((c & 7) + '0') | attr, 1);
	    break;
	case NLSCLASS_ILLEGAL2:
	case NLSCLASS_ILLEGAL3:
	case NLSCLASS_ILLEGAL4:
	case NLSCLASS_ILLEGAL5:
	    Vdraw('\\', 1);
	    Vdraw('U', 1);
	    Vdraw('+', 1);
	    for (i = 16 + 4 * (-w-5); i >= 0; i -= 4)
		Vdraw("0123456789ABCDEF"[(c >> i) & 15] | attr, 1);
	    break;
	case 0:
	    lv = vcursor_v;
	    lh = vcursor_h;
	    for (;;) {
		lh--;
		if (lh < 0) {
		    lv--;
		    if (lv < 0)
			break;
		    lh = Strlen(Vdisplay[lv]) - 1;
		}
		if (Vdisplay[lv][lh] != CHAR_DBWIDTH)
		    break;
	    }
	    if (lv < 0) {
		Vdraw('\\' | attr, 1);
		Vdraw((((c >> 6) & 7) + '0') | attr, 1);
		Vdraw((((c >> 3) & 7) + '0') | attr, 1);
		Vdraw(((c & 7) + '0') | attr, 1);
		break;
	    }
	    Vdisplay[lv][lh] = MakeLiteral(cp, 1, Vdisplay[lv][lh]);
	    break;
	default:
	    Vdraw(*cp, w);
	    break;
    }
    return 1;
}

static void
Vdraw(Char c, int width)	/* draw char c onto V lines */
{
#ifdef DEBUG_REFRESH
# ifdef SHORT_STRINGS
    reprintf("Vdrawing %6.6o '%c' %d\r\n", (unsigned)c, (int)(c & ASCII), width);
# else
    reprintf("Vdrawing %3.3o '%c' %d\r\n", (unsigned)c, (int)c, width);
# endif /* SHORT_STRNGS */
#endif  /* DEBUG_REFRESH */

    /* Hopefully this is what all the terminals do with multi-column characters
       that "span line breaks". */
    while (vcursor_h + width > TermH)
	Vdraw(' ', 1);
    Vdisplay[vcursor_v][vcursor_h] = c;
    if (width)
	vcursor_h++;		/* advance to next place */
    while (--width > 0)
	Vdisplay[vcursor_v][vcursor_h++] = CHAR_DBWIDTH;
    if (vcursor_h >= TermH) {
	Vdisplay[vcursor_v][TermH] = '\0';	/* assure end of line */
	vcursor_h = 0;		/* reset it. */
	vcursor_v++;
#ifdef DEBUG_REFRESH
	if (vcursor_v >= TermV) {	/* should NEVER happen. */
	    reprintf("\r\nVdraw: vcursor_v overflow! Vcursor_v == %d > %d\r\n",
		    vcursor_v, TermV);
	    abort();
	}
#endif /* DEBUG_REFRESH */
    }
}

/*
 *  RefreshPromptpart()
 *	draws a prompt element, expanding literals (we know it's ASCIZ)
 */
static void
RefreshPromptpart(Char *buf)
{
    Char *cp;
    int w;

    if (buf == NULL)
	return;
    for (cp = buf; *cp; ) {
	if (*cp & LITERAL) {
	    Char *litstart = cp;
	    while (*cp & LITERAL)
		cp++;
	    if (*cp) {
		w = NLSWidth(*cp & CHAR);
		Vdraw(MakeLiteral(litstart, cp + 1 - litstart, 0), w);
		cp++;
	    }
	    else {
		/*
		 * XXX: This is a bug, we lose the last literal, if it is not
		 * followed by a normal character, but it is too hard to fix
		 */
		break;
	    }
	}
	else
	    cp += Draw(cp, cp == buf, 1);
    }
}

/*
 *  Refresh()
 *	draws the new virtual screen image from the current input
 *  	line, then goes line-by-line changing the real image to the new
 *	virtual image. The routine to re-draw a line can be replaced
 *	easily in hopes of a smarter one being placed there.
 */
#ifndef WINNT_NATIVE
static
#endif
int OldvcV = 0;

void
Refresh(void)
{
    int cur_line;
    Char *cp;
    int     cur_h, cur_v = 0, new_vcv;
    int     rhdiff;
    Char    oldgetting;

#ifdef DEBUG_REFRESH
    reprintf("Prompt = :%s:\r\n", short2str(Prompt));
    reprintf("InputBuf = :%s:\r\n", short2str(InputBuf));
#endif /* DEBUG_REFRESH */
    oldgetting = GettingInput;
    GettingInput = 0;		/* avoid re-entrance via SIGWINCH */

    /* reset the Vdraw cursor, temporarily draw rprompt to calculate its size */
    vcursor_h = 0;
    vcursor_v = 0;
    RefreshPromptpart(RPrompt);
    rprompt_h = vcursor_h;
    rprompt_v = vcursor_v;

    /* reset the Vdraw cursor, draw prompt */
    vcursor_h = 0;
    vcursor_v = 0;
    RefreshPromptpart(Prompt);
    cur_h = -1;			/* set flag in case I'm not set */

    /* draw the current input buffer */
    for (cp = InputBuf; (cp < LastChar); ) {
	if (cp >= Cursor && cur_h == -1) {
	    cur_h = vcursor_h;	/* save for later */
	    cur_v = vcursor_v;
	    Cursor = cp;
	}
	cp += Draw(cp, cp == InputBuf, 0);
    }

    if (cur_h == -1) {		/* if I haven't been set yet, I'm at the end */
	cur_h = vcursor_h;
	cur_v = vcursor_v;
    }

    rhdiff = TermH - vcursor_h - rprompt_h;
    if (rprompt_h != 0 && rprompt_v == 0 && vcursor_v == 0 && rhdiff > 1) {
			/*
			 * have a right-hand side prompt that will fit on
			 * the end of the first line with at least one
			 * character gap to the input buffer.
			 */
	while (--rhdiff > 0)		/* pad out with spaces */
	    Vdraw(' ', 1);
	RefreshPromptpart(RPrompt);
    }
    else {
	rprompt_h = 0;			/* flag "not using rprompt" */
	rprompt_v = 0;
    }

    new_vcv = vcursor_v;	/* must be done BEFORE the NUL is written */
    Vdraw('\0', 1);		/* put NUL on end */

#if defined (DEBUG_REFRESH)
    reprintf("TermH=%d, vcur_h=%d, vcur_v=%d, Vdisplay[0]=\r\n:%80.80s:\r\n",
	    TermH, vcursor_h, vcursor_v, short2str(Vdisplay[0]));
#endif /* DEBUG_REFRESH */

#ifdef DEBUG_UPDATE
    reprintf("updating %d lines.\r\n", new_vcv);
#endif  /* DEBUG_UPDATE */
    for (cur_line = 0; cur_line <= new_vcv; cur_line++) {
	/* NOTE THAT update_line MAY CHANGE Display[cur_line] */
	update_line(Display[cur_line], Vdisplay[cur_line], cur_line);
#ifdef WINNT_NATIVE
	flush();
#endif /* WINNT_NATIVE */

	/*
	 * Copy the new line to be the current one, and pad out with spaces
	 * to the full width of the terminal so that if we try moving the
	 * cursor by writing the character that is at the end of the
	 * screen line, it won't be a NUL or some old leftover stuff.
	 */
	cpy_pad_spaces(Display[cur_line], Vdisplay[cur_line], TermH);
    }
#ifdef DEBUG_REFRESH
    reprintf("\r\nvcursor_v = %d, OldvcV = %d, cur_line = %d\r\n",
	    vcursor_v, OldvcV, cur_line);
#endif /* DEBUG_REFRESH */
    if (OldvcV > new_vcv) {
	for (; cur_line <= OldvcV; cur_line++) {
	    update_line(Display[cur_line], STRNULL, cur_line);
	    *Display[cur_line] = '\0';
	}
    }
    OldvcV = new_vcv;		/* set for next time */
#ifdef DEBUG_REFRESH
    reprintf("\r\nCursorH = %d, CursorV = %d, cur_h = %d, cur_v = %d\r\n",
	    CursorH, CursorV, cur_h, cur_v);
#endif /* DEBUG_REFRESH */
#ifdef WINNT_NATIVE
    flush();
#endif /* WINNT_NATIVE */
    MoveToLine(cur_v);		/* go to where the cursor is */
    MoveToChar(cur_h);
    SetAttributes(0);		/* Clear all attributes */
    flush();			/* send the output... */
    GettingInput = oldgetting;	/* reset to old value */
}

#ifdef notdef
GotoBottom(void)
{				/* used to go to last used screen line */
    MoveToLine(OldvcV);
}

#endif 

void
PastBottom(void)
{				/* used to go to last used screen line */
    MoveToLine(OldvcV);
    (void) putraw('\r');
    (void) putraw('\n');
    ClearDisp();
    flush();
}


/* insert num characters of s into d (in front of the character) at dat,
   maximum length of d is dlen */
static void
str_insert(Char *d, int dat, int dlen, Char *s, int num)
{
    Char *a, *b;

    if (num <= 0)
	return;
    if (num > dlen - dat)
	num = dlen - dat;

#ifdef DEBUG_REFRESH
    reprintf("str_insert() starting: %d at %d max %d, d == \"%s\"\n",
	    num, dat, dlen, short2str(d));
    reprintf("s == \"%s\"n", short2str(s));
#endif /* DEBUG_REFRESH */

    /* open up the space for num chars */
    if (num > 0) {
	b = d + dlen - 1;
	a = b - num;
	while (a >= &d[dat])
	    *b-- = *a--;
	d[dlen] = '\0';		/* just in case */
    }
#ifdef DEBUG_REFRESH
    reprintf("str_insert() after insert: %d at %d max %d, d == \"%s\"\n",
	    num, dat, dlen, short2str(d));
    reprintf("s == \"%s\"n", short2str(s));
#endif /* DEBUG_REFRESH */

    /* copy the characters */
    for (a = d + dat; (a < d + dlen) && (num > 0); num--)
	*a++ = *s++;

#ifdef DEBUG_REFRESH
    reprintf("str_insert() after copy: %d at %d max %d, d == \"%s\"\n",
	    num, dat, dlen, d, short2str(s));
    reprintf("s == \"%s\"n", short2str(s));
#endif /* DEBUG_REFRESH */
}

/* delete num characters d at dat, maximum length of d is dlen */
static void
str_delete(Char *d, int dat, int dlen, int num)
{
    Char *a, *b;

    if (num <= 0)
	return;
    if (dat + num >= dlen) {
	d[dat] = '\0';
	return;
    }

#ifdef DEBUG_REFRESH
    reprintf("str_delete() starting: %d at %d max %d, d == \"%s\"\n",
	    num, dat, dlen, short2str(d));
#endif /* DEBUG_REFRESH */

    /* open up the space for num chars */
    if (num > 0) {
	b = d + dat;
	a = b + num;
	while (a < &d[dlen])
	    *b++ = *a++;
	d[dlen] = '\0';		/* just in case */
    }
#ifdef DEBUG_REFRESH
    reprintf("str_delete() after delete: %d at %d max %d, d == \"%s\"\n",
	    num, dat, dlen, short2str(d));
#endif /* DEBUG_REFRESH */
}

static void
str_cp(Char *a, Char *b, int n)
{
    while (n-- && *b)
	*a++ = *b++;
}


/* ****************************************************************
    update_line() is based on finding the middle difference of each line
    on the screen; vis:

			     /old first difference
	/beginning of line   |              /old last same       /old EOL
	v		     v              v                    v
old:	eddie> Oh, my little gruntle-buggy is to me, as lurgid as
new:	eddie> Oh, my little buggy says to me, as lurgid as
	^		     ^        ^			   ^
	\beginning of line   |        \new last same	   \new end of line
			     \new first difference

    all are character pointers for the sake of speed.  Special cases for
    no differences, as well as for end of line additions must be handled.
**************************************************************** */

/* Minimum at which doing an insert it "worth it".  This should be about
 * half the "cost" of going into insert mode, inserting a character, and
 * going back out.  This should really be calculated from the termcap
 * data...  For the moment, a good number for ANSI terminals.
 */
#define MIN_END_KEEP	4

static void			/* could be changed to make it smarter */
update_line(Char *old, Char *new, int cur_line)
{
    Char *o, *n, *p, c;
    Char  *ofd, *ols, *oe, *nfd, *nls, *ne;
    Char  *osb, *ose, *nsb, *nse;
    int     fx, sx;

    /*
     * find first diff (won't be CHAR_DBWIDTH in either line)
     */
    for (o = old, n = new; *o && (*o == *n); o++, n++)
	continue;
    ofd = o;
    nfd = n;

    /*
     * Find the end of both old and new
     */
    o = Strend(o);

    /* 
     * Remove any trailing blanks off of the end, being careful not to
     * back up past the beginning.
     */
    if (!(adrof(STRhighlight) && MarkIsSet)) {
    while (ofd < o) {
	if (o[-1] != ' ')
	    break;
	o--;
    }
    }
    oe = o;
    *oe = (Char) 0;

    n = Strend(n);

    /* remove blanks from end of new */
    if (!(adrof(STRhighlight) && MarkIsSet)) {
    while (nfd < n) {
	if (n[-1] != ' ')
	    break;
	n--;
    }
    }
    ne = n;
    *ne = (Char) 0;
  
    /*
     * if no diff, continue to next line of redraw
     */
    if (*ofd == '\0' && *nfd == '\0') {
#ifdef DEBUG_UPDATE
	reprintf("no difference.\r\n");
#endif /* DEBUG_UPDATE */
	return;
    }

    /*
     * find last same pointer
     */
    while ((o > ofd) && (n > nfd) && (*--o == *--n))
	continue;
    if (*o != *n) {
	o++;
	n++;
    }
    while (*o == CHAR_DBWIDTH) {
	o++;
	n++;
    }
    ols = o;
    nls = n;

    /*
     * find same begining and same end
     */
    osb = ols;
    nsb = nls;
    ose = ols;
    nse = nls;

    /*
     * case 1: insert: scan from nfd to nls looking for *ofd
     */
    if (*ofd) {
	for (c = *ofd, n = nfd; n < nls; n++) {
	    if (c == *n) {
		for (o = ofd, p = n; p < nls && o < ols && *o == *p; o++, p++)
		    continue;
		/*
		 * if the new match is longer and it's worth keeping, then we
		 * take it
		 */
		if (((nse - nsb) < (p - n)) && (2 * (p - n) > n - nfd)) {
		    nsb = n;
		    nse = p;
		    osb = ofd;
		    ose = o;
		}
	    }
	}
    }

    /*
     * case 2: delete: scan from ofd to ols looking for *nfd
     */
    if (*nfd) {
	for (c = *nfd, o = ofd; o < ols; o++) {
	    if (c == *o) {
		for (n = nfd, p = o; p < ols && n < nls && *p == *n; p++, n++)
		    continue;
		/*
		 * if the new match is longer and it's worth keeping, then we
		 * take it
		 */
		if (((ose - osb) < (p - o)) && (2 * (p - o) > o - ofd)) {
		    nsb = nfd;
		    nse = n;
		    osb = o;
		    ose = p;
		}
	    }
	}
    }
#ifdef notdef
    /*
     * If `last same' is before `same end' re-adjust
     */
    if (ols < ose)
	ols = ose;
    if (nls < nse)
	nls = nse;
#endif

    /*
     * Pragmatics I: If old trailing whitespace or not enough characters to
     * save to be worth it, then don't save the last same info.
     */
    if ((oe - ols) < MIN_END_KEEP) {
	ols = oe;
	nls = ne;
    }

    /*
     * Pragmatics II: if the terminal isn't smart enough, make the data dumber
     * so the smart update doesn't try anything fancy
     */

    /*
     * fx is the number of characters we need to insert/delete: in the
     * beginning to bring the two same begins together
     */
    fx = (int) ((nsb - nfd) - (osb - ofd));
    /*
     * sx is the number of characters we need to insert/delete: in the end to
     * bring the two same last parts together
     */
    sx = (int) ((nls - nse) - (ols - ose));

    if (!T_CanIns) {
	if (fx > 0) {
	    osb = ols;
	    ose = ols;
	    nsb = nls;
	    nse = nls;
	}
	if (sx > 0) {
	    ols = oe;
	    nls = ne;
	}
	if ((ols - ofd) < (nls - nfd)) {
	    ols = oe;
	    nls = ne;
	}
    }
    if (!T_CanDel) {
	if (fx < 0) {
	    osb = ols;
	    ose = ols;
	    nsb = nls;
	    nse = nls;
	}
	if (sx < 0) {
	    ols = oe;
	    nls = ne;
	}
	if ((ols - ofd) > (nls - nfd)) {
	    ols = oe;
	    nls = ne;
	}
    }

    /*
     * Pragmatics III: make sure the middle shifted pointers are correct if
     * they don't point to anything (we may have moved ols or nls).
     */
    /* if the change isn't worth it, don't bother */
    /* was: if (osb == ose) */
    if ((ose - osb) < MIN_END_KEEP) {
	osb = ols;
	ose = ols;
	nsb = nls;
	nse = nls;
    }

    /*
     * Now that we are done with pragmatics we recompute fx, sx
     */
    fx = (int) ((nsb - nfd) - (osb - ofd));
    sx = (int) ((nls - nse) - (ols - ose));

#ifdef DEBUG_UPDATE
    reprintf("\n");
    reprintf("ofd %d, osb %d, ose %d, ols %d, oe %d\n",
	    ofd - old, osb - old, ose - old, ols - old, oe - old);
    reprintf("nfd %d, nsb %d, nse %d, nls %d, ne %d\n",
	    nfd - new, nsb - new, nse - new, nls - new, ne - new);
    reprintf("xxx-xxx:\"00000000001111111111222222222233333333334\"\r\n");
    reprintf("xxx-xxx:\"01234567890123456789012345678901234567890\"\r\n");
    dprintstr("old- oe", old, oe);
    dprintstr("new- ne", new, ne);
    dprintstr("old-ofd", old, ofd);
    dprintstr("new-nfd", new, nfd);
    dprintstr("ofd-osb", ofd, osb);
    dprintstr("nfd-nsb", nfd, nsb);
    dprintstr("osb-ose", osb, ose);
    dprintstr("nsb-nse", nsb, nse);
    dprintstr("ose-ols", ose, ols);
    dprintstr("nse-nls", nse, nls);
    dprintstr("ols- oe", ols, oe);
    dprintstr("nls- ne", nls, ne);
#endif /* DEBUG_UPDATE */

    /*
     * CursorV to this line cur_line MUST be in this routine so that if we
     * don't have to change the line, we don't move to it. CursorH to first
     * diff char
     */
    MoveToLine(cur_line);

    /*
     * at this point we have something like this:
     * 
     * /old                  /ofd    /osb               /ose    /ols     /oe
     * v.....................v       v..................v       v........v
     * eddie> Oh, my fredded gruntle-buggy is to me, as foo var lurgid as
     * eddie> Oh, my fredded quiux buggy is to me, as gruntle-lurgid as
     * ^.....................^     ^..................^       ^........^ 
     * \new                  \nfd  \nsb               \nse     \nls    \ne
     * 
     * fx is the difference in length between the the chars between nfd and
     * nsb, and the chars between ofd and osb, and is thus the number of
     * characters to delete if < 0 (new is shorter than old, as above),
     * or insert (new is longer than short).
     *
     * sx is the same for the second differences.
     */

    /*
     * if we have a net insert on the first difference, AND inserting the net
     * amount ((nsb-nfd) - (osb-ofd)) won't push the last useful character
     * (which is ne if nls != ne, otherwise is nse) off the edge of the screen
     * (TermH - 1) else we do the deletes first so that we keep everything we
     * need to.
     */

    /*
     * if the last same is the same like the end, there is no last same part,
     * otherwise we want to keep the last same part set p to the last useful
     * old character
     */
    p = (ols != oe) ? oe : ose;

    /*
     * if (There is a diffence in the beginning) && (we need to insert
     * characters) && (the number of characters to insert is less than the term
     * width) We need to do an insert! else if (we need to delete characters)
     * We need to delete characters! else No insert or delete
     */
    if ((nsb != nfd) && fx > 0 && ((p - old) + fx < TermH)) {
#ifdef DEBUG_UPDATE
	reprintf("first diff insert at %d...\r\n", nfd - new);
#endif  /* DEBUG_UPDATE */
	/*
	 * Move to the first char to insert, where the first diff is.
	 */
	MoveToChar(nfd - new);
	/*
	 * Check if we have stuff to keep at end
	 */
	if (nsb != ne) {
#ifdef DEBUG_UPDATE
	    reprintf("with stuff to keep at end\r\n");
#endif  /* DEBUG_UPDATE */
	    /*
	     * insert fx chars of new starting at nfd
	     */
	    if (fx > 0) {
#ifdef DEBUG_UPDATE
		if (!T_CanIns)
		    reprintf("   ERROR: cannot insert in early first diff\n");
#endif  /* DEBUG_UPDATE */
		Insert_write(nfd, fx);
		str_insert(old, (int) (ofd - old), TermH, nfd, fx);
	    }
	    /*
	     * write (nsb-nfd) - fx chars of new starting at (nfd + fx)
	     */
	    so_write(nfd + fx, (nsb - nfd) - fx);
	    str_cp(ofd + fx, nfd + fx, (int) ((nsb - nfd) - fx));
	}
	else {
#ifdef DEBUG_UPDATE
	    reprintf("without anything to save\r\n");
#endif  /* DEBUG_UPDATE */
	    so_write(nfd, (nsb - nfd));
	    str_cp(ofd, nfd, (int) (nsb - nfd));
	    /*
	     * Done
	     */
	    return;
	}
    }
    else if (fx < 0) {
#ifdef DEBUG_UPDATE
	reprintf("first diff delete at %d...\r\n", ofd - old);
#endif  /* DEBUG_UPDATE */
	/*
	 * move to the first char to delete where the first diff is
	 */
	MoveToChar(ofd - old);
	/*
	 * Check if we have stuff to save
	 */
	if (osb != oe) {
#ifdef DEBUG_UPDATE
	    reprintf("with stuff to save at end\r\n");
#endif  /* DEBUG_UPDATE */
	    /*
	     * fx is less than zero *always* here but we check for code
	     * symmetry
	     */
	    if (fx < 0) {
#ifdef DEBUG_UPDATE
		if (!T_CanDel)
		    reprintf("   ERROR: cannot delete in first diff\n");
#endif /* DEBUG_UPDATE */
		DeleteChars(-fx);
		str_delete(old, (int) (ofd - old), TermH, -fx);
	    }
	    /*
	     * write (nsb-nfd) chars of new starting at nfd
	     */
	    so_write(nfd, (nsb - nfd));
	    str_cp(ofd, nfd, (int) (nsb - nfd));

	}
	else {
#ifdef DEBUG_UPDATE
	    reprintf("but with nothing left to save\r\n");
#endif  /* DEBUG_UPDATE */
	    /*
	     * write (nsb-nfd) chars of new starting at nfd
	     */
	    so_write(nfd, (nsb - nfd));
#ifdef DEBUG_REFRESH
	    reprintf("cleareol %d\n", (oe - old) - (ne - new));
#endif  /* DEBUG_UPDATE */
#ifndef WINNT_NATIVE
	    ClearEOL((oe - old) - (ne - new));
#else
	    /*
	     * The calculation above does not work too well on NT
	     */
	    ClearEOL(TermH - CursorH);
#endif /*WINNT_NATIVE*/
	    /*
	     * Done
	     */
	    return;
	}
    }
    else
	fx = 0;

    if (sx < 0) {
#ifdef DEBUG_UPDATE
	reprintf("second diff delete at %d...\r\n", (ose - old) + fx);
#endif  /* DEBUG_UPDATE */
	/*
	 * Check if we have stuff to delete
	 */
	/*
	 * fx is the number of characters inserted (+) or deleted (-)
	 */

	MoveToChar((ose - old) + fx);
	/*
	 * Check if we have stuff to save
	 */
	if (ols != oe) {
#ifdef DEBUG_UPDATE
	    reprintf("with stuff to save at end\r\n");
#endif  /* DEBUG_UPDATE */
	    /*
	     * Again a duplicate test.
	     */
	    if (sx < 0) {
#ifdef DEBUG_UPDATE
		if (!T_CanDel)
		    reprintf("   ERROR: cannot delete in second diff\n");
#endif  /* DEBUG_UPDATE */
		DeleteChars(-sx);
	    }

	    /*
	     * write (nls-nse) chars of new starting at nse
	     */
	    so_write(nse, (nls - nse));
	}
	else {
	    int olen = (int) (oe - old + fx);
	    if (olen > TermH)
		olen = TermH;
#ifdef DEBUG_UPDATE
	    reprintf("but with nothing left to save\r\n");
#endif /* DEBUG_UPDATE */
	    so_write(nse, (nls - nse));
#ifdef DEBUG_REFRESH
	    reprintf("cleareol %d\n", olen - (ne - new));
#endif /* DEBUG_UPDATE */
#ifndef WINNT_NATIVE
	    ClearEOL(olen - (ne - new));
#else
	    /*
	     * The calculation above does not work too well on NT
	     */
	    ClearEOL(TermH - CursorH);
#endif /*WINNT_NATIVE*/
	}
    }

    /*
     * if we have a first insert AND WE HAVEN'T ALREADY DONE IT...
     */
    if ((nsb != nfd) && (osb - ofd) <= (nsb - nfd) && (fx == 0)) {
#ifdef DEBUG_UPDATE
	reprintf("late first diff insert at %d...\r\n", nfd - new);
#endif /* DEBUG_UPDATE */

	MoveToChar(nfd - new);
	/*
	 * Check if we have stuff to keep at the end
	 */
	if (nsb != ne) {
#ifdef DEBUG_UPDATE
	    reprintf("with stuff to keep at end\r\n");
#endif /* DEBUG_UPDATE */
	    /* 
	     * We have to recalculate fx here because we set it
	     * to zero above as a flag saying that we hadn't done
	     * an early first insert.
	     */
	    fx = (int) ((nsb - nfd) - (osb - ofd));
	    if (fx > 0) {
		/*
		 * insert fx chars of new starting at nfd
		 */
#ifdef DEBUG_UPDATE
		if (!T_CanIns)
		    reprintf("   ERROR: cannot insert in late first diff\n");
#endif /* DEBUG_UPDATE */
		Insert_write(nfd, fx);
		str_insert(old, (int) (ofd - old), TermH, nfd, fx);
	    }

	    /*
	     * write (nsb-nfd) - fx chars of new starting at (nfd + fx)
	     */
	    so_write(nfd + fx, (nsb - nfd) - fx);
	    str_cp(ofd + fx, nfd + fx, (int) ((nsb - nfd) - fx));
	}
	else {
#ifdef DEBUG_UPDATE
	    reprintf("without anything to save\r\n");
#endif /* DEBUG_UPDATE */
	    so_write(nfd, (nsb - nfd));
	    str_cp(ofd, nfd, (int) (nsb - nfd));
	}
    }

    /*
     * line is now NEW up to nse
     */
    if (sx >= 0) {
#ifdef DEBUG_UPDATE
	reprintf("second diff insert at %d...\r\n", nse - new);
#endif /* DEBUG_UPDATE */
	MoveToChar(nse - new);
	if (ols != oe) {
#ifdef DEBUG_UPDATE
	    reprintf("with stuff to keep at end\r\n");
#endif /* DEBUG_UPDATE */
	    if (sx > 0) {
		/* insert sx chars of new starting at nse */
#ifdef DEBUG_UPDATE
		if (!T_CanIns)
		    reprintf("   ERROR: cannot insert in second diff\n");
#endif /* DEBUG_UPDATE */
		Insert_write(nse, sx);
	    }

	    /*
	     * write (nls-nse) - sx chars of new starting at (nse + sx)
	     */
	    so_write(nse + sx, (nls - nse) - sx);
	}
	else {
#ifdef DEBUG_UPDATE
	    reprintf("without anything to save\r\n");
#endif /* DEBUG_UPDATE */
	    so_write(nse, (nls - nse));

	    /*
             * No need to do a clear-to-end here because we were doing
	     * a second insert, so we will have over written all of the
	     * old string.
	     */
	}
    }
#ifdef DEBUG_UPDATE
    reprintf("done.\r\n");
#endif /* DEBUG_UPDATE */
}


static void
cpy_pad_spaces(Char *dst, Char *src, int width)
{
    int i;

    for (i = 0; i < width; i++) {
	if (*src == (Char) 0)
	    break;
	*dst++ = *src++;
    }

    while (i < width) {
	*dst++ = ' ';
	i++;
    }
    *dst = (Char) 0;
}

void
RefCursor(void)
{				/* only move to new cursor pos */
    Char *cp;
    int w, h, th, v;

    /* first we must find where the cursor is... */
    h = 0;
    v = 0;
    th = TermH;			/* optimize for speed */

    for (cp = Prompt; cp != NULL && *cp; ) {	/* do prompt */
	if (*cp & LITERAL) {
	    cp++;
	    continue;
	}
	w = NLSClassify(*cp & CHAR, cp == Prompt, 0);
	cp++;
	switch(w) {
	    case NLSCLASS_NL:
		h = 0;
		v++;
		break;
	    case NLSCLASS_TAB:
		while (++h & 07)
		    ;
		break;
	    case NLSCLASS_CTRL:
		h += 2;
		break;
	    case NLSCLASS_ILLEGAL:
		h += 4;
		break;
	    case NLSCLASS_ILLEGAL2:
	    case NLSCLASS_ILLEGAL3:
	    case NLSCLASS_ILLEGAL4:
		h += 3 + 2 * NLSCLASS_ILLEGAL_SIZE(w);
		break;
	    default:
		h += w;
	}
	if (h >= th) {		/* check, extra long tabs picked up here also */
	    h -= th;
	    v++;
	}
    }

    for (cp = InputBuf; cp < Cursor;) {	/* do input buffer to Cursor */
	w = NLSClassify(*cp & CHAR, cp == InputBuf, 0);
	cp++;
	switch(w) {
	    case NLSCLASS_NL:
		h = 0;
		v++;
		break;
	    case NLSCLASS_TAB:
		while (++h & 07)
		    ;
		break;
	    case NLSCLASS_CTRL:
		h += 2;
		break;
	    case NLSCLASS_ILLEGAL:
		h += 4;
		break;
	    case NLSCLASS_ILLEGAL2:
	    case NLSCLASS_ILLEGAL3:
	    case NLSCLASS_ILLEGAL4:
		h += 3 + 2 * NLSCLASS_ILLEGAL_SIZE(w);
		break;
	    default:
		h += w;
	}
	if (h >= th) {		/* check, extra long tabs picked up here also */
	    h -= th;
	    v++;
	}
    }

    /* now go there */
    MoveToLine(v);
    MoveToChar(h);
    if (adrof(STRhighlight) && MarkIsSet) {
	ClearLines();
	ClearDisp();
	Refresh();
    }
    flush();
}

#ifndef WINTT_NATIVE
static void
PutPlusOne(Char c, int width)
{
    while (width > 1 && CursorH + width > TermH)
	PutPlusOne(' ', 1);
    if ((c & LITERAL) != 0) {
	Char *d;
	for (d = litptr + (c & ~LITERAL) * LIT_FACTOR; *d; d++)
	    (void) putwraw(*d);
    } else {
	(void) putwraw(c);
    }
    Display[CursorV][CursorH++] = (Char) c;
    while (--width > 0)
	Display[CursorV][CursorH++] = CHAR_DBWIDTH;
    if (CursorH >= TermH) {	/* if we must overflow */
	CursorH = 0;
	CursorV++;
	OldvcV++;
	if (T_Margin & MARGIN_AUTO) {
	    if (T_Margin & MARGIN_MAGIC) {
		(void) putraw(' ');
		(void) putraw('\b');
	    }
	}
	else {
	    (void) putraw('\r');
	    (void) putraw('\n');
	}
    }
}
#endif

void
RefPlusOne(int l)
{				/* we added just one char, handle it fast.
				 * assumes that screen cursor == real cursor */
    Char *cp, c;
    int w;

    if (Cursor != LastChar) {
	Refresh();		/* too hard to handle */
	return;
    }
    if (rprompt_h != 0 && (TermH - CursorH - rprompt_h < 3)) {
	Refresh();		/* clear out rprompt if less than one char gap*/
	return;
    }
    cp = Cursor - l;
    c = *cp & CHAR;
    w = NLSClassify(c, cp == InputBuf, 0);
    switch(w) {
	case NLSCLASS_CTRL:
	    PutPlusOne('^', 1);
	    if (c == CTL_ESC('\177')) {
		PutPlusOne('?', 1);
		break;
	    }
#ifdef IS_ASCII
	    /* uncontrolify it; works only for iso8859-1 like sets */
	    PutPlusOne((c | 0100), 1);
#else
	    PutPlusOne(_toebcdic[_toascii[c]|0100], 1);
#endif
	    break;
	case NLSCLASS_ILLEGAL:
	    PutPlusOne('\\', 1);
	    PutPlusOne(((c >> 6) & 7) + '0', 1);
	    PutPlusOne(((c >> 3) & 7) + '0', 1);
	    PutPlusOne((c & 7) + '0', 1);
	    break;
	case 1:
	    if (adrof(STRhighlight) && MarkIsSet)
		StartHighlight();
	    if (l > 1)
		PutPlusOne(MakeLiteral(cp, l, 0), 1);
	    else
		PutPlusOne(*cp, 1);
	    if (adrof(STRhighlight) && MarkIsSet)
		StopHighlight();
	    break;
	default:
	    Refresh();		/* too hard to handle */
	    return;
    }
    flush();
}

/* clear the screen buffers so that new new prompt starts fresh. */

void
ClearDisp(void)
{
    int i;

    CursorV = 0;		/* clear the display buffer */
    CursorH = 0;
    for (i = 0; i < TermV; i++)
	(void) memset(Display[i], 0, (TermH + 1) * sizeof(Display[0][0]));
    OldvcV = 0;
    litlen = 0;
}

void
ClearLines(void)
{				/* Make sure all lines are *really* blank */
    int i;

    if (T_CanCEOL) {
	/*
	 * Clear the lines from the bottom up so that if we try moving
	 * the cursor down by writing the character that is at the end
	 * of the screen line, we won't rewrite a character that shouldn't
	 * be there.
	 */
	for (i = OldvcV; i >= 0; i--) {	/* for each line on the screen */
	    MoveToLine(i);
	    MoveToChar(0);
	    ClearEOL(TermH);
	}
    }
    else {
	MoveToLine(OldvcV);	/* go to last line */
	(void) putraw('\r');	/* go to BOL */
	(void) putraw('\n');	/* go to new line */
    }
}
