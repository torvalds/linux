/* $Header: /p/tcsh/cvsroot/tcsh/ed.chared.c,v 3.103 2015/08/19 14:29:55 christos Exp $ */
/*
 * ed.chared.c: Character editing functions.
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
/*
  Bjorn Knutsson @ Thu Jun 24 19:02:17 1999

  e_dabbrev_expand() did not do proper completion if quoted spaces were present
  in the string being completed. Exemple:

  # echo hello\ world
  hello world
  # echo h<press key bound to dabbrev-expande>
  # echo hello\<cursor>

  Correct behavior is:
  # echo h<press key bound to dabbrev-expande>
  # echo hello\ world<cursor>

  The same problem occured if spaces were present in a string withing quotation
  marks. Example:

  # echo "hello world"
  hello world
  # echo "h<press key bound to dabbrev-expande>
  # echo "hello<cursor>
  
  The former problem could be solved with minor modifications of c_preword()
  and c_endword(). The latter, however, required a significant rewrite of
  c_preword(), since quoted strings must be parsed from start to end to
  determine if a given character is inside or outside the quotation marks.

  Compare the following two strings:

  # echo \"" 'foo \' bar\"
  " 'foo \' bar\
  # echo '\"" 'foo \' bar\"
  \"" foo ' bar"

  The only difference between the two echo lines is in the first character
  after the echo command. The result is either one or three arguments.

 */

#include "sh.h"

RCSID("$tcsh: ed.chared.c,v 3.103 2015/08/19 14:29:55 christos Exp $")

#include "ed.h"
#include "tw.h"
#include "ed.defns.h"

/* #define SDEBUG */

#define TCSHOP_NOP    	  0x00
#define TCSHOP_DELETE 	  0x01
#define TCSHOP_INSERT 	  0x02
#define TCSHOP_CHANGE 	  0x04

#define CHAR_FWD	0
#define CHAR_BACK	1

/*
 * vi word treatment
 * from: Gert-Jan Vons <vons@cesar.crbca1.sinet.slb.com>
 */
#define C_CLASS_WHITE	1
#define C_CLASS_WORD	2
#define C_CLASS_OTHER	3

static Char *InsertPos = InputBuf; /* Where insertion starts */
static Char *ActionPos = 0;	   /* Where action begins  */
static int  ActionFlag = TCSHOP_NOP;	   /* What delayed action to take */
/*
 * Word search state
 */
static int  searchdir = F_UP_SEARCH_HIST; 	/* Direction of last search */
static struct Strbuf patbuf; /* = Strbuf_INIT; Search target */
/*
 * Char search state
 */
static int  srch_dir = CHAR_FWD;		/* Direction of last search */
static Char srch_char = 0;			/* Search target */

/* all routines that start with c_ are private to this set of routines */
static	void	 c_alternativ_key_map	(int);
void	 c_insert		(int);
void	 c_delafter		(int);
void	 c_delbefore		(int);
static 	int	 c_to_class		(Char);
static	Char	*c_prev_word		(Char *, Char *, int);
static	Char	*c_next_word		(Char *, Char *, int);
static	Char	*c_number		(Char *, int *, int);
static	Char	*c_expand		(Char *);
static	int	 c_excl			(Char *);
static	int	 c_substitute		(void);
static	void	 c_delfini		(void);
static	int	 c_hmatch		(Char *);
static	void	 c_hsetpat		(void);
#ifdef COMMENT
static	void	 c_get_word		(Char **, Char **);
#endif
static	Char	*c_preword		(Char *, Char *, int, Char *);
static	Char	*c_nexword		(Char *, Char *, int);
static	Char	*c_endword		(Char *, Char *, int, Char *);
static	Char	*c_eword		(Char *, Char *, int);
static	void	 c_push_kill		(Char *, Char *);
static	void	 c_save_inputbuf	(void);
static  CCRETVAL c_search_line		(Char *, int);
static  CCRETVAL v_repeat_srch		(int);
static	CCRETVAL e_inc_search		(int);
#ifdef notyet
static  CCRETVAL e_insert_str		(Char *);
#endif
static	CCRETVAL v_search		(int);
static	CCRETVAL v_csearch_fwd		(Char, int, int);
static	CCRETVAL v_action		(int);
static	CCRETVAL v_csearch_back		(Char, int, int);

static void
c_alternativ_key_map(int state)
{
    switch (state) {
    case 0:
	CurrentKeyMap = CcKeyMap;
	break;
    case 1:
	CurrentKeyMap = CcAltMap;
	break;
    default:
	return;
    }

    AltKeyMap = (Char) state;
}

void
c_insert(int num)
{
    Char *cp;

    if (LastChar + num >= InputLim)
	return;			/* can't go past end of buffer */

    if (Cursor < LastChar) {	/* if I must move chars */
	for (cp = LastChar; cp >= Cursor; cp--)
	    cp[num] = *cp;
	if (Mark && Mark > Cursor)
		Mark += num;
    }
    LastChar += num;
}

void
c_delafter(int num)
{
    Char *cp, *kp = NULL;

    if (num > LastChar - Cursor)
	num = (int) (LastChar - Cursor);	/* bounds check */

    if (num > 0) {			/* if I can delete anything */
	if (VImode) {
	    kp = UndoBuf;		/* Set Up for VI undo command */
	    UndoAction = TCSHOP_INSERT;
	    UndoSize = num;
	    UndoPtr  = Cursor;
	    for (cp = Cursor; cp <= LastChar; cp++) {
		*kp++ = *cp;	/* Save deleted chars into undobuf */
		*cp = cp[num];
	    }
	}
	else
	    for (cp = Cursor; cp + num <= LastChar; cp++)
		*cp = cp[num];
	LastChar -= num;
	/* Mark was within the range of the deleted word? */
	if (Mark && Mark > Cursor && Mark <= Cursor+num)
		Mark = Cursor;
	/* Mark after the deleted word? */
	else if (Mark && Mark > Cursor)
		Mark -= num;
    }
#ifdef notdef
    else {
	/* 
	 * XXX: We don't want to do that. In emacs mode overwrite should be
	 * sticky. I am not sure how that affects vi mode 
	 */
	inputmode = MODE_INSERT;
    }
#endif /* notdef */
}

void
c_delbefore(int num)		/* delete before dot, with bounds checking */
{
    Char *cp, *kp = NULL;

    if (num > Cursor - InputBuf)
	num = (int) (Cursor - InputBuf);	/* bounds check */

    if (num > 0) {			/* if I can delete anything */
	if (VImode) {
	    kp = UndoBuf;		/* Set Up for VI undo command */
	    UndoAction = TCSHOP_INSERT;
	    UndoSize = num;
	    UndoPtr  = Cursor - num;
	    for (cp = Cursor - num; cp <= LastChar; cp++) {
		*kp++ = *cp;
		*cp = cp[num];
	    }
	}
	else
	    for (cp = Cursor - num; cp + num <= LastChar; cp++)
		*cp = cp[num];
	LastChar -= num;
	Cursor -= num;
	/* Mark was within the range of the deleted word? */
	if (Mark && Mark > Cursor && Mark <= Cursor+num)
		Mark = Cursor;
	/* Mark after the deleted word? */
	else if (Mark && Mark > Cursor)
		Mark -= num;
    }
}

static Char *
c_preword(Char *p, Char *low, int n, Char *delim)
{
  while (n--) {
    Char *prev = low;
    Char *new;

    while (prev < p) {		/* Skip initial non-word chars */
      if (!Strchr(delim, *prev) || *(prev-1) == (Char)'\\')
	break;
      prev++;
    }

    new = prev;

    while (new < p) {
      prev = new;
      new = c_endword(prev-1, p, 1, delim); /* Skip to next non-word char */
      new++;			/* Step away from end of word */
      while (new <= p) {	/* Skip trailing non-word chars */
	if (!Strchr(delim, *new) || *(new-1) == (Char)'\\')
	  break;
	new++;
      }
    }

    p = prev;			/* Set to previous word start */

  }
  if (p < low)
    p = low;
  return (p);
}

/*
 * c_to_class() returns the class of the given character.
 *
 * This is used to make the c_prev_word(), c_next_word() and c_eword() functions
 * work like vi's, which classify characters. A word is a sequence of
 * characters belonging to the same class, classes being defined as
 * follows:
 *
 *	1/ whitespace
 *	2/ alphanumeric chars, + underscore
 *	3/ others
 */
static int
c_to_class(Char ch)
{
    if (Isspace(ch))
        return C_CLASS_WHITE;

    if (isword(ch))
        return C_CLASS_WORD;

    return C_CLASS_OTHER;
}

static Char *
c_prev_word(Char *p, Char *low, int n)
{
    p--;

    if (!VImode) {
	while (n--) {
	    while ((p >= low) && !isword(*p)) 
		p--;
	    while ((p >= low) && isword(*p)) 
		p--;
	}
      
	/* cp now points to one character before the word */
	p++;
	if (p < low)
	    p = low;
	/* cp now points where we want it */
	return(p);
    }
  
    while (n--) {
        int  c_class;

        if (p < low)
            break;

        /* scan until beginning of current word (may be all whitespace!) */
        c_class = c_to_class(*p);
        while ((p >= low) && c_class == c_to_class(*p))
            p--;

        /* if this was a non_whitespace word, we're ready */
        if (c_class != C_CLASS_WHITE)
            continue;

        /* otherwise, move back to beginning of the word just found */
        c_class = c_to_class(*p);
        while ((p >= low) && c_class == c_to_class(*p))
            p--;
    }

    p++;                        /* correct overshoot */

    return (p);
}

static Char *
c_next_word(Char *p, Char *high, int n)
{
    if (!VImode) {
	while (n--) {
	    while ((p < high) && !isword(*p)) 
		p++;
	    while ((p < high) && isword(*p)) 
		p++;
	}
	if (p > high)
	    p = high;
	/* p now points where we want it */
	return(p);
    }

    while (n--) {
        int  c_class;

        if (p >= high)
            break;

        /* scan until end of current word (may be all whitespace!) */
        c_class = c_to_class(*p);
        while ((p < high) && c_class == c_to_class(*p))
            p++;

        /* if this was all whitespace, we're ready */
        if (c_class == C_CLASS_WHITE)
            continue;

	/* if we've found white-space at the end of the word, skip it */
        while ((p < high) && c_to_class(*p) == C_CLASS_WHITE)
            p++;
    }

    p--;                        /* correct overshoot */

    return (p);
}

static Char *
c_nexword(Char *p, Char *high, int n)
{
    while (n--) {
	while ((p < high) && !Isspace(*p)) 
	    p++;
	while ((p < high) && Isspace(*p)) 
	    p++;
    }

    if (p > high)
	p = high;
    /* p now points where we want it */
    return(p);
}

/*
 * Expand-History (originally "Magic-Space") code added by
 * Ray Moody <ray@gibbs.physics.purdue.edu>
 * this is a neat, but odd, addition.
 */

/*
 * c_number: Ignore character p points to, return number appearing after that.
 * A '$' by itself means a big number; "$-" is for negative; '^' means 1.
 * Return p pointing to last char used.
 */

/*
 * dval is the number to subtract from for things like $-3
 */

static Char *
c_number(Char *p, int *num, int dval)
{
    int i;
    int sign = 1;

    if (*++p == '^') {
	*num = 1;
	return(p);
    }
    if (*p == '$') {
	if (*++p != '-') {
	    *num = INT_MAX;	/* Handle $ */
	    return(--p);
	}
	sign = -1;		/* Handle $- */
	++p;
    }
    for (i = 0; *p >= '0' && *p <= '9'; i = 10 * i + *p++ - '0')
	continue;
    *num = (sign < 0 ? dval - i : i);
    return(--p);
}

/*
 * excl_expand: There is an excl to be expanded to p -- do the right thing
 * with it and return a version of p advanced over the expanded stuff.  Also,
 * update tsh_cur and related things as appropriate...
 */

static Char *
c_expand(Char *p)
{
    Char *q;
    struct Hist *h = Histlist.Hnext;
    struct wordent *l;
    int     i, from, to, dval;
    int    all_dig;
    int    been_once = 0;
    Char   *op = p;
    Char   *buf;
    size_t buf_len;
    Char   *modbuf;

    buf = NULL;
    if (!h)
	goto excl_err;
excl_sw:
    switch (*(q = p + 1)) {

    case '^':
	buf = expand_lex(&h->Hlex, 1, 1);
	break;

    case '$':
	if ((l = (h->Hlex).prev) != 0)
	    buf = expand_lex(l->prev->prev, 0, 0);
	break;

    case '*':
	buf = expand_lex(&h->Hlex, 1, INT_MAX);
	break;

    default:
	if (been_once) {	/* unknown argument */
	    /* assume it's a modifier, e.g. !foo:h, and get whole cmd */
	    buf = expand_lex(&h->Hlex, 0, INT_MAX);
	    q -= 2;
	    break;
	}
	been_once = 1;

	if (*q == ':')		/* short form: !:arg */
	    --q;

	if (HIST != '\0' && *q != HIST) {
	    /*
	     * Search for a space, tab, or colon.  See if we have a number (as
	     * in !1234:xyz).  Remember the number.
	     */
	    for (i = 0, all_dig = 1; 
		 *q != ' ' && *q != '\t' && *q != ':' && q < Cursor; q++) {
		/*
		 * PWP: !-4 is a valid history argument too, therefore the test
		 * is if not a digit, or not a - as the first character.
		 */
		if ((*q < '0' || *q > '9') && (*q != '-' || q != p + 1))
		    all_dig = 0;
		else if (*q == '-')
		    all_dig = 2;/* we are sneeky about this */
		else
		    i = 10 * i + *q - '0';
	    }
	    --q;

	    /*
	     * If we have a number, search for event i.  Otherwise, search for
	     * a named event (as in !foo).  (In this case, I is the length of
	     * the named event).
	     */
	    if (all_dig) {
		if (all_dig == 2)
		    i = -i;	/* make it negitive */
		if (i < 0)	/* if !-4 (for example) */
		    i = eventno + 1 + i;	/* remember: i is < 0 */
		for (; h; h = h->Hnext) {
		    if (h->Hnum == i)
			break;
		}
	    }
	    else {
		for (i = (int) (q - p); h; h = h->Hnext) {
		    if ((l = &h->Hlex) != 0) {
			if (!Strncmp(p + 1, l->next->word, (size_t) i))
			    break;
		    }
		}
	    }
	}
	if (!h)
	    goto excl_err;
	if (q[1] == ':' || q[1] == '-' || q[1] == '*' ||
	    q[1] == '$' || q[1] == '^') {	/* get some args */
	    p = q[1] == ':' ? ++q : q;
	    /*
	     * Go handle !foo:*
	     */
	    if ((q[1] < '0' || q[1] > '9') &&
		q[1] != '-' && q[1] != '$' && q[1] != '^')
		goto excl_sw;
	    /*
	     * Go handle !foo:$
	     */
	    if (q[1] == '$' && (q[2] != '-' || q[3] < '0' || q[3] > '9'))
		goto excl_sw;
	    /*
	     * Count up the number of words in this event.  Store it in dval.
	     * Dval will be fed to number.
	     */
	    dval = 0;
	    if ((l = h->Hlex.prev) != 0) {
		for (l = l->prev; l != h->Hlex.next; l = l->prev, dval++)
		    continue;
	    }
	    if (!dval)
		goto excl_err;
	    if (q[1] == '-')
		from = 0;
	    else
		q = c_number(q, &from, dval);
	    if (q[1] == '-') {
		++q;
		if ((q[1] < '0' || q[1] > '9') && q[1] != '$')
		    to = dval - 1;
		else
		    q = c_number(q, &to, dval);
	    }
	    else if (q[1] == '*') {
		++q;
		to = INT_MAX;
	    }
	    else {
		to = from;
	    }
	    if (from < 0 || to < from)
		goto excl_err;
	    buf = expand_lex(&h->Hlex, from, to);
	}
	else			/* get whole cmd */
	    buf = expand_lex(&h->Hlex, 0, INT_MAX);
	break;
    }
    if (buf == NULL)
	buf = SAVE("");

    /*
     * Apply modifiers, if any.
     */
    if (q[1] == ':') {
	modbuf = buf;
	while (q[1] == ':' && modbuf != NULL) {
	    switch (q[2]) {
	    case 'r':
	    case 'e':
	    case 'h':
	    case 't':
	    case 'q':
	    case 'x':
	    case 'u':
	    case 'l':
		if ((modbuf = domod(buf, (int) q[2])) != NULL) {
		    xfree(buf);
		    buf = modbuf;
		}
		++q;
		break;

	    case 'a':
	    case 'g':
		/* Not implemented; this needs to be done before expanding
		 * lex. We don't have the words available to us anymore.
		 */
		++q;
		break;

	    case 'p':
		/* Ok */
		++q;
		break;

	    case '\0':
		break;

	    default:
		++q;
		break;
	    }
	    if (q[1])
		++q;
	}
    }

    buf_len = Strlen(buf);
    /*
     * Now replace the text from op to q inclusive with the text from buf.
     */
    q++;

    /*
     * Now replace text non-inclusively like a real CS major!
     */
    if (LastChar + buf_len - (q - op) >= InputLim)
	goto excl_err;
    (void) memmove(op + buf_len, q, (LastChar - q) * sizeof(Char));
    LastChar += buf_len - (q - op);
    Cursor += buf_len - (q - op);
    (void) memcpy(op, buf, buf_len * sizeof(Char));
    *LastChar = '\0';
    xfree(buf);
    return op + buf_len;
excl_err:
    xfree(buf);
    SoundBeep();
    return(op + 1);
}

/*
 * c_excl: An excl has been found at point p -- back up and find some white
 * space (or the beginning of the buffer) and properly expand all the excl's
 * from there up to the current cursor position. We also avoid (trying to)
 * expanding '>!'
 * Returns number of expansions attempted (doesn't matter whether they succeeded
 * or not).
 */

static int
c_excl(Char *p)
{
    int i;
    Char *q;
    int nr_exp;

    /*
     * if />[SPC TAB]*![SPC TAB]/, back up p to just after the >. otherwise,
     * back p up to just before the current word.
     */
    if ((p[1] == ' ' || p[1] == '\t') &&
	(p[-1] == ' ' || p[-1] == '\t' || p[-1] == '>')) {
	for (q = p - 1; q > InputBuf && (*q == ' ' || *q == '\t'); --q)
	    continue;
	if (*q == '>')
	    ++p;
    }
    else {
	while (*p != ' ' && *p != '\t' && p > InputBuf)
	    --p;
    }

    /*
     * Forever: Look for history char.  (Stop looking when we find the cursor.)
     * Count backslashes.  If odd, skip history char.  Expand if even number of
     * backslashes.
     */
    nr_exp = 0;
    for (;;) {
	if (HIST != '\0')
	    while (*p != HIST && p < Cursor)
		++p;
	for (i = 1; (p - i) >= InputBuf && p[-i] == '\\'; i++)
	    continue;
	if (i % 2 == 0)
	    ++p;
	if (p >= Cursor)   /* all done */
	    return nr_exp;
	if (i % 2 == 1) {
	    p = c_expand(p);
	    ++nr_exp;
	}
    }
}


static int
c_substitute(void)
{
    Char *p;
    int  nr_exp;

    /*
     * Start p out one character before the cursor.  Move it backwards looking
     * for white space, the beginning of the line, or a history character.
     */
    for (p = Cursor - 1; 
	 p > InputBuf && *p != ' ' && *p != '\t' && *p && *p != HIST; --p)
	continue;

    /*
     * If we found a history character, go expand it.
     */
    if (p >= InputBuf && HIST != '\0' && *p == HIST)
	nr_exp = c_excl(p);
    else
        nr_exp = 0;
    Refresh();

    return nr_exp;
}

static void
c_delfini(void)		/* Finish up delete action */
{
    int Size;

    if (ActionFlag & TCSHOP_INSERT)
	c_alternativ_key_map(0);

    ActionFlag = TCSHOP_NOP;

    if (ActionPos == 0) 
	return;

    UndoAction = TCSHOP_INSERT;

    if (Cursor > ActionPos) {
	Size = (int) (Cursor-ActionPos);
	c_delbefore(Size); 
	RefCursor();
    }
    else if (Cursor < ActionPos) {
	Size = (int)(ActionPos-Cursor);
	c_delafter(Size);
    }
    else  {
	Size = 1;
	c_delafter(Size);
    }
    UndoPtr = Cursor;
    UndoSize = Size;
}

static Char *
c_endword(Char *p, Char *high, int n, Char *delim)
{
    Char inquote = 0;
    p++;

    while (n--) {
        while (p < high) {	/* Skip non-word chars */
	  if (!Strchr(delim, *p) || *(p-1) == (Char)'\\')
	    break;
	  p++;
        }
	while (p < high) {	/* Skip string */
	  if ((*p == (Char)'\'' || *p == (Char)'"')) { /* Quotation marks? */
	    if (inquote || *(p-1) != (Char)'\\') { /* Should it be honored? */
	      if (inquote == 0) inquote = *p;
	      else if (inquote == *p) inquote = 0;
	    }
	  }
	  /* Break if unquoted non-word char */
	  if (!inquote && Strchr(delim, *p) && *(p-1) != (Char)'\\')
	    break;
	  p++;
	}
    }

    p--;
    return(p);
}


static Char *
c_eword(Char *p, Char *high, int n)
{
    p++;

    while (n--) {
        int  c_class;

        if (p >= high)
            break;

        /* scan until end of current word (may be all whitespace!) */
        c_class = c_to_class(*p);
        while ((p < high) && c_class == c_to_class(*p))
            p++;

        /* if this was a non_whitespace word, we're ready */
        if (c_class != C_CLASS_WHITE)
            continue;

        /* otherwise, move to the end of the word just found */
        c_class = c_to_class(*p);
        while ((p < high) && c_class == c_to_class(*p))
            p++;
    }

    p--;
    return(p);
}

/* Set the max length of the kill ring */
void
SetKillRing(int max)
{
    CStr *new;
    int count, i, j;

    if (max < 1)
	max = 1;		/* no ring, but always one buffer */
    if (max == KillRingMax)
	return;
    new = xcalloc(max, sizeof(CStr));
    if (KillRing != NULL) {
	if (KillRingLen != 0) {
	    if (max >= KillRingLen) {
		count = KillRingLen;
		j = KillPos;
	    } else {
		count = max;
		j = (KillPos - count + KillRingLen) % KillRingLen;
	    }
	    for (i = 0; i < KillRingLen; i++) {
		if (i < count)	/* copy latest */
		    new[i] = KillRing[j];
		else		/* free the others */
		    xfree(KillRing[j].buf);
		j = (j + 1) % KillRingLen;
	    }
	    KillRingLen = count;
	    KillPos = count % max;
	    YankPos = count - 1;
	}
	xfree(KillRing);
    }
    KillRing = new;
    KillRingMax = max;
}

/* Push string from start upto (but not including) end onto kill ring */
static void
c_push_kill(Char *start, Char *end)
{
    CStr save, *pos;
    Char *dp, *cp, *kp;
    int len = end - start, i, j, k;

    /* Check for duplicates? */
    if (KillRingLen > 0 && (dp = varval(STRkilldup)) != STRNULL) {
	YankPos = (KillPos - 1 + KillRingLen) % KillRingLen;
	if (eq(dp, STRerase)) {	/* erase earlier one (actually move up) */
	    j = YankPos;
	    for (i = 0; i < KillRingLen; i++) {
		if (Strncmp(KillRing[j].buf, start, (size_t) len) == 0 &&
		    KillRing[j].buf[len] == '\0') {
		    save = KillRing[j];
		    for ( ; i > 0; i--) {
			k = j;
			j = (j + 1) % KillRingLen;
			KillRing[k] = KillRing[j];
		    }
		    KillRing[j] = save;
		    return;
		}
		j = (j - 1 + KillRingLen) % KillRingLen;
	    }
	} else if (eq(dp, STRall)) { /* skip if any earlier */
	    for (i = 0; i < KillRingLen; i++)
		if (Strncmp(KillRing[i].buf, start, (size_t) len) == 0 &&
		    KillRing[i].buf[len] == '\0')
		    return;
	} else if (eq(dp, STRprev)) { /* skip if immediately previous */
	    j = YankPos;
	    if (Strncmp(KillRing[j].buf, start, (size_t) len) == 0 &&
		KillRing[j].buf[len] == '\0')
		return;
	}
    }

    /* No duplicate, go ahead and push */
    len++;			/* need space for '\0' */
    YankPos = KillPos;
    if (KillRingLen < KillRingMax)
	KillRingLen++;
    pos = &KillRing[KillPos];
    KillPos = (KillPos + 1) % KillRingMax;
    if (pos->len < len) {
	pos->buf = xrealloc(pos->buf, len * sizeof(Char));
	pos->len = len;
    }
    cp = start;
    kp = pos->buf;
    while (cp < end)
	*kp++ = *cp++;
    *kp = '\0';
}

/* Save InputBuf etc in SavedBuf etc for restore after cmd exec */
static void
c_save_inputbuf(void)
{
    SavedBuf.len = 0;
    Strbuf_append(&SavedBuf, InputBuf);
    Strbuf_terminate(&SavedBuf);
    LastSaved = LastChar - InputBuf;
    CursSaved = Cursor - InputBuf;
    HistSaved = Hist_num;
    RestoreSaved = 1;
}

CCRETVAL
GetHistLine(void)
{
    struct Hist *hp;
    int     h;

    if (Hist_num == 0) {	/* if really the current line */
	if (HistBuf.s != NULL)
	    copyn(InputBuf, HistBuf.s, INBUFSIZE);/*FIXBUF*/
	else
	    *InputBuf = '\0';
	LastChar = InputBuf + HistBuf.len;

#ifdef KSHVI
    if (VImode)
	Cursor = InputBuf;
    else
#endif /* KSHVI */
	Cursor = LastChar;

	return(CC_REFRESH);
    }

    hp = Histlist.Hnext;
    if (hp == NULL)
	return(CC_ERROR);

    for (h = 1; h < Hist_num; h++) {
	if ((hp->Hnext) == NULL) {
	    Hist_num = h;
	    return(CC_ERROR);
	}
	hp = hp->Hnext;
    }

    if (HistLit && hp->histline) {
	copyn(InputBuf, hp->histline, INBUFSIZE);/*FIXBUF*/
	CurrentHistLit = 1;
    }
    else {
	Char *p;

	p = sprlex(&hp->Hlex);
	copyn(InputBuf, p, sizeof(InputBuf) / sizeof(Char));/*FIXBUF*/
	xfree(p);
	CurrentHistLit = 0;
    }
    LastChar = Strend(InputBuf);

    if (LastChar > InputBuf) {
	if (LastChar[-1] == '\n')
	    LastChar--;
#if 0
	if (LastChar[-1] == ' ')
	    LastChar--;
#endif
	if (LastChar < InputBuf)
	    LastChar = InputBuf;
    }
  
#ifdef KSHVI
    if (VImode)
	Cursor = InputBuf;
    else
#endif /* KSHVI */
	Cursor = LastChar;

    return(CC_REFRESH);
}

static CCRETVAL
c_search_line(Char *pattern, int dir)
{
    Char *cp;
    size_t len;

    len = Strlen(pattern);

    if (dir == F_UP_SEARCH_HIST) {
	for (cp = Cursor; cp >= InputBuf; cp--)
	    if (Strncmp(cp, pattern, len) == 0 ||
		Gmatch(cp, pattern)) {
		Cursor = cp;
		return(CC_NORM);
	    }
	return(CC_ERROR);
    } else {
	for (cp = Cursor; *cp != '\0' && cp < InputLim; cp++)
	    if (Strncmp(cp, pattern, len) == 0 ||
		Gmatch(cp, pattern)) {
		Cursor = cp;
		return(CC_NORM);
	    }
	return(CC_ERROR);
    }
}

static CCRETVAL
e_inc_search(int dir)
{
    static const Char STRfwd[] = { 'f', 'w', 'd', '\0' },
		      STRbck[] = { 'b', 'c', 'k', '\0' };
    static Char pchar = ':';	/* ':' = normal, '?' = failed */
    static Char endcmd[2];
    const Char *cp;
    Char ch,
	*oldCursor = Cursor,
	oldpchar = pchar;
    CCRETVAL ret = CC_NORM;
    int oldHist_num = Hist_num,
	oldpatlen = patbuf.len,
	newdir = dir,
        done, redo;

    if (LastChar + sizeof(STRfwd)/sizeof(Char) + 2 + patbuf.len >= InputLim)
	return(CC_ERROR);

    for (;;) {

	if (patbuf.len == 0) {	/* first round */
	    pchar = ':';
	    Strbuf_append1(&patbuf, '*');
	}
	done = redo = 0;
	*LastChar++ = '\n';
	for (cp = newdir == F_UP_SEARCH_HIST ? STRbck : STRfwd; 
	     *cp; *LastChar++ = *cp++)
	    continue;
	*LastChar++ = pchar;
	for (cp = &patbuf.s[1]; cp < &patbuf.s[patbuf.len];
	     *LastChar++ = *cp++)
	    continue;
	*LastChar = '\0';
	if (adrof(STRhighlight) && pchar == ':') {
	    /* if the no-glob-search patch is applied, remove the - 1 below */
	    IncMatchLen = patbuf.len - 1;
	    ClearLines();
	    ClearDisp();
	}
	Refresh();

	if (GetNextChar(&ch) != 1)
	    return(e_send_eof(0));

	switch (ch > NT_NUM_KEYS
		? F_INSERT : CurrentKeyMap[(unsigned char) ch]) {
	case F_INSERT:
	case F_DIGIT:
	case F_MAGIC_SPACE:
	    if (LastChar + 1 >= InputLim) /*FIXBUF*/
		SoundBeep();
	    else {
		Strbuf_append1(&patbuf, ch);
		*LastChar++ = ch;
		*LastChar = '\0';
		Refresh();
	    }
	    break;

	case F_INC_FWD:
	    newdir = F_DOWN_SEARCH_HIST;
	    redo++;
	    break;

	case F_INC_BACK:
	    newdir = F_UP_SEARCH_HIST;
	    redo++;
	    break;

	case F_DELPREV:
	    if (patbuf.len > 1)
		done++;
	    else 
		SoundBeep();
	    break;

	default:
	    switch (ASC(ch)) {
	    case 0007:		/* ^G: Abort */
		ret = CC_ERROR;
		done++;
		break;

	    case 0027:		/* ^W: Append word */
		/* No can do if globbing characters in pattern */
		for (cp = &patbuf.s[1]; ; cp++)
		    if (cp >= &patbuf.s[patbuf.len]) {
			Cursor += patbuf.len - 1;
			cp = c_next_word(Cursor, LastChar, 1);
			while (Cursor < cp && *Cursor != '\n') {
			    if (LastChar + 1 >= InputLim) {/*FIXBUF*/
				SoundBeep();
				break;
			    }
			    Strbuf_append1(&patbuf, *Cursor);
			    *LastChar++ = *Cursor++;
			}
			Cursor = oldCursor;
			*LastChar = '\0';
			Refresh();
			break;
		    } else if (isglob(*cp)) {
			SoundBeep();
			break;
		    }
		break;
	    
	    default:		/* Terminate and execute cmd */
		endcmd[0] = ch;
		PushMacro(endcmd);
		/*FALLTHROUGH*/

	    case 0033:		/* ESC: Terminate */
		ret = CC_REFRESH;
		done++;
		break;
	    }
	    break;
	}

	while (LastChar > InputBuf && *LastChar != '\n')
	    *LastChar-- = '\0';
	*LastChar = '\0';

	if (!done) {

	    /* Can't search if unmatched '[' */
	    for (cp = &patbuf.s[patbuf.len - 1], ch = ']'; cp > patbuf.s; cp--)
		if (*cp == '[' || *cp == ']') {
		    ch = *cp;
		    break;
		}

	    if (patbuf.len > 1 && ch != '[') {
		if (redo && newdir == dir) {
		    if (pchar == '?') {	/* wrap around */
			Hist_num = newdir == F_UP_SEARCH_HIST ? 0 : INT_MAX;
			if (GetHistLine() == CC_ERROR)
			    /* Hist_num was fixed by first call */
			    (void) GetHistLine();
			Cursor = newdir == F_UP_SEARCH_HIST ?
			    LastChar : InputBuf;
		    } else
			Cursor += newdir == F_UP_SEARCH_HIST ? -1 : 1;
		}
		Strbuf_append1(&patbuf, '*');
		Strbuf_terminate(&patbuf);
		if (Cursor < InputBuf || Cursor > LastChar ||
		    (ret = c_search_line(&patbuf.s[1], newdir)) == CC_ERROR) {
		    LastCmd = (KEYCMD) newdir; /* avoid c_hsetpat */
		    ret = newdir == F_UP_SEARCH_HIST ?
			e_up_search_hist(0) : e_down_search_hist(0);
		    if (ret != CC_ERROR) {
			Cursor = newdir == F_UP_SEARCH_HIST ?
			    LastChar : InputBuf;
			(void) c_search_line(&patbuf.s[1], newdir);
		    }
		}
		patbuf.s[--patbuf.len] = '\0';
		if (ret == CC_ERROR) {
		    SoundBeep();
		    if (Hist_num != oldHist_num) {
			Hist_num = oldHist_num;
			if (GetHistLine() == CC_ERROR)
			    return(CC_ERROR);
		    }
		    Cursor = oldCursor;
		    pchar = '?';
		} else {
		    pchar = ':';
		}
	    }

	    ret = e_inc_search(newdir);

	    if (ret == CC_ERROR && pchar == '?' && oldpchar == ':') {
		/* break abort of failed search at last non-failed */
		ret = CC_NORM;
	    }

	}

	if (ret == CC_NORM || (ret == CC_ERROR && oldpatlen == 0)) {
	    /* restore on normal return or error exit */
	    pchar = oldpchar;
	    patbuf.len = oldpatlen;
	    if (Hist_num != oldHist_num) {
		Hist_num = oldHist_num;
		if (GetHistLine() == CC_ERROR)
		    return(CC_ERROR);
	    }
	    Cursor = oldCursor;
	    if (ret == CC_ERROR)
		Refresh();
	}
	if (done || ret != CC_NORM)
	    return(ret);
	    
    }

}

static CCRETVAL
v_search(int dir)
{
    struct Strbuf tmpbuf = Strbuf_INIT;
    Char ch;
    Char *oldbuf;
    Char *oldlc, *oldc;

    cleanup_push(&tmpbuf, Strbuf_cleanup);
    oldbuf = Strsave(InputBuf);
    cleanup_push(oldbuf, xfree);
    oldlc = LastChar;
    oldc = Cursor;
    Strbuf_append1(&tmpbuf, '*');

    InputBuf[0] = '\0';
    LastChar = InputBuf;
    Cursor = InputBuf;
    searchdir = dir;

    c_insert(2);	/* prompt + '\n' */
    *Cursor++ = '\n';
    *Cursor++ = dir == F_UP_SEARCH_HIST ? '?' : '/';
    Refresh();
    for (ch = 0;ch == 0;) {
	if (GetNextChar(&ch) != 1) {
	    cleanup_until(&tmpbuf);
	    return(e_send_eof(0));
	}
	switch (ASC(ch)) {
	case 0010:	/* Delete and backspace */
	case 0177:
	    if (tmpbuf.len > 1) {
		*Cursor-- = '\0';
		LastChar = Cursor;
		tmpbuf.len--;
	    }
	    else {
		copyn(InputBuf, oldbuf, INBUFSIZE);/*FIXBUF*/
		LastChar = oldlc;
		Cursor = oldc;
		cleanup_until(&tmpbuf);
		return(CC_REFRESH);
	    }
	    Refresh();
	    ch = 0;
	    break;

	case 0033:	/* ESC */
#ifdef IS_ASCII
	case '\r':	/* Newline */
	case '\n':
#else
	case '\012':    /* ASCII Line feed */
	case '\015':    /* ASCII (or EBCDIC) Return */
#endif
	    break;

	default:
	    Strbuf_append1(&tmpbuf, ch);
	    *Cursor++ = ch;
	    LastChar = Cursor;
	    Refresh();
	    ch = 0;
	    break;
	}
    }
    cleanup_until(oldbuf);

    if (tmpbuf.len == 1) {
	/*
	 * Use the old pattern, but wild-card it.
	 */
	if (patbuf.len == 0) {
	    InputBuf[0] = '\0';
	    LastChar = InputBuf;
	    Cursor = InputBuf;
	    Refresh();
	    cleanup_until(&tmpbuf);
	    return(CC_ERROR);
	}
	if (patbuf.s[0] != '*') {
	    oldbuf = Strsave(patbuf.s);
	    patbuf.len = 0;
	    Strbuf_append1(&patbuf, '*');
	    Strbuf_append(&patbuf, oldbuf);
	    xfree(oldbuf);
	    Strbuf_append1(&patbuf, '*');
	    Strbuf_terminate(&patbuf);
	}
    }
    else {
	Strbuf_append1(&tmpbuf, '*');
	Strbuf_terminate(&tmpbuf);
	patbuf.len = 0;
	Strbuf_append(&patbuf, tmpbuf.s);
	Strbuf_terminate(&patbuf);
    }
    cleanup_until(&tmpbuf);
    LastCmd = (KEYCMD) dir; /* avoid c_hsetpat */
    Cursor = LastChar = InputBuf;
    if ((dir == F_UP_SEARCH_HIST ? e_up_search_hist(0) : 
				   e_down_search_hist(0)) == CC_ERROR) {
	Refresh();
	return(CC_ERROR);
    }
    else {
	if (ASC(ch) == 0033) {
	    Refresh();
	    *LastChar++ = '\n';
	    *LastChar = '\0';
	    PastBottom();
	    return(CC_NEWLINE);
	}
	else
	    return(CC_REFRESH);
    }
}

/*
 * semi-PUBLIC routines.  Any routine that is of type CCRETVAL is an
 * entry point, called from the CcKeyMap indirected into the
 * CcFuncTbl array.
 */

/*ARGSUSED*/
CCRETVAL
v_cmd_mode(Char c)
{
    USE(c);
    InsertPos = 0;
    ActionFlag = TCSHOP_NOP;	/* [Esc] cancels pending action */
    ActionPos = 0;
    DoingArg = 0;
    if (UndoPtr > Cursor)
	UndoSize = (int)(UndoPtr - Cursor);
    else
	UndoSize = (int)(Cursor - UndoPtr);

    inputmode = MODE_INSERT;
    c_alternativ_key_map(1);
#ifdef notdef
    /*
     * We don't want to move the cursor, because all the editing
     * commands don't include the character under the cursor.
     */
    if (Cursor > InputBuf)
	Cursor--;
#endif
    RefCursor();
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
e_unassigned(Char c)
{				/* bound to keys that arn't really assigned */
    USE(c);
    SoundBeep();
    flush();
    return(CC_NORM);
}

#ifdef notyet
static CCRETVAL
e_insert_str(Char *c)
{
    int i, n;

    n = Strlen(c);
    if (LastChar + Argument * n >= InputLim)
	return(CC_ERROR);	/* end of buffer space */
    if (inputmode != MODE_INSERT) {
	c_delafter(Argument * Strlen(c));
    }
    c_insert(Argument * n);
    while (Argument--) {
	for (i = 0; i < n; i++)
	    *Cursor++ = c[i];
    }
    Refresh();
    return(CC_NORM);
}
#endif

CCRETVAL
e_insert(Char c)
{
#ifndef SHORT_STRINGS
    c &= ASCII;			/* no meta chars ever */
#endif

    if (!c)
	return(CC_ERROR);	/* no NULs in the input ever!! */

    if (LastChar + Argument >= InputLim)
	return(CC_ERROR);	/* end of buffer space */

    if (Argument == 1) {  	/* How was this optimized ???? */

	if (inputmode != MODE_INSERT) {
	    UndoBuf[UndoSize++] = *Cursor;
	    UndoBuf[UndoSize] = '\0';
	    c_delafter(1);   /* Do NOT use the saving ONE */
    	}

        c_insert(1);
	*Cursor++ = (Char) c;
	DoingArg = 0;		/* just in case */
	RefPlusOne(1);		/* fast refresh for one char. */
    }
    else {
	if (inputmode != MODE_INSERT) {
	    int i;
	    for(i = 0; i < Argument; i++) 
		UndoBuf[UndoSize++] = *(Cursor + i);

	    UndoBuf[UndoSize] = '\0';
	    c_delafter(Argument);   /* Do NOT use the saving ONE */
    	}

        c_insert(Argument);

	while (Argument--)
	    *Cursor++ = (Char) c;
	Refresh();
    }

    if (inputmode == MODE_REPLACE_1)
	(void) v_cmd_mode(0);

    return(CC_NORM);
}

int
InsertStr(Char *s)		/* insert ASCIZ s at cursor (for complete) */
{
    int len;

    if ((len = (int) Strlen(s)) <= 0)
	return -1;
    if (LastChar + len >= InputLim)
	return -1;		/* end of buffer space */

    c_insert(len);
    while (len--)
	*Cursor++ = *s++;
    return 0;
}

void
DeleteBack(int n)		/* delete the n characters before . */
{
    if (n <= 0)
	return;
    if (Cursor >= &InputBuf[n]) {
	c_delbefore(n);		/* delete before dot */
    }
}

CCRETVAL
e_digit(Char c)			/* gray magic here */
{
    if (!Isdigit(c))
	return(CC_ERROR);	/* no NULs in the input ever!! */

    if (DoingArg) {		/* if doing an arg, add this in... */
	if (LastCmd == F_ARGFOUR)	/* if last command was ^U */
	    Argument = c - '0';
	else {
	    if (Argument > 1000000)
		return CC_ERROR;
	    Argument = (Argument * 10) + (c - '0');
	}
	return(CC_ARGHACK);
    }
    else {
	if (LastChar + 1 >= InputLim)
	    return CC_ERROR;	/* end of buffer space */

	if (inputmode != MODE_INSERT) {
	    UndoBuf[UndoSize++] = *Cursor;
	    UndoBuf[UndoSize] = '\0';
	    c_delafter(1);   /* Do NOT use the saving ONE */
    	}
	c_insert(1);
	*Cursor++ = (Char) c;
	DoingArg = 0;		/* just in case */
	RefPlusOne(1);		/* fast refresh for one char. */
    }
    return(CC_NORM);
}

CCRETVAL
e_argdigit(Char c)		/* for ESC-n */
{
#ifdef IS_ASCII
    c &= ASCII;
#else
    c = CTL_ESC(ASC(c) & ASCII); /* stripping for EBCDIC done the ASCII way */
#endif

    if (!Isdigit(c))
	return(CC_ERROR);	/* no NULs in the input ever!! */

    if (DoingArg) {		/* if doing an arg, add this in... */
	if (Argument > 1000000)
	    return CC_ERROR;
	Argument = (Argument * 10) + (c - '0');
    }
    else {			/* else starting an argument */
	Argument = c - '0';
	DoingArg = 1;
    }
    return(CC_ARGHACK);
}

CCRETVAL
v_zero(Char c)			/* command mode 0 for vi */
{
    if (DoingArg) {		/* if doing an arg, add this in... */
	if (Argument > 1000000)
	    return CC_ERROR;
	Argument = (Argument * 10) + (c - '0');
	return(CC_ARGHACK);
    }
    else {			/* else starting an argument */
	Cursor = InputBuf;
	if (ActionFlag & TCSHOP_DELETE) {
	   c_delfini();
	   return(CC_REFRESH);
        }
	RefCursor();		/* move the cursor */
	return(CC_NORM);
    }
}

/*ARGSUSED*/
CCRETVAL
e_newline(Char c)
{				/* always ignore argument */
    USE(c);
    if (adrof(STRhighlight) && MarkIsSet) {
	MarkIsSet = 0;
	ClearLines();
	ClearDisp();
	Refresh();
    }
    MarkIsSet = 0;

  /*  PastBottom();  NOW done in ed.inputl.c */
    *LastChar++ = '\n';		/* for the benefit of CSH */
    *LastChar = '\0';		/* just in case */
    if (VImode)
	InsertPos = InputBuf;	/* Reset editing position */
    return(CC_NEWLINE);
}

/*ARGSUSED*/
CCRETVAL
e_newline_hold(Char c)
{
    USE(c);
    c_save_inputbuf();
    HistSaved = 0;
    *LastChar++ = '\n';		/* for the benefit of CSH */
    *LastChar = '\0';		/* just in case */
    return(CC_NEWLINE);
}

/*ARGSUSED*/
CCRETVAL
e_newline_down_hist(Char c)
{
    USE(c);
    if (Hist_num > 1) {
	HistSaved = Hist_num;
    }
    *LastChar++ = '\n';		/* for the benefit of CSH */
    *LastChar = '\0';		/* just in case */
    return(CC_NEWLINE);
}

/*ARGSUSED*/
CCRETVAL
e_send_eof(Char c)
{				/* for when ^D is ONLY send-eof */
    USE(c);
    PastBottom();
    *LastChar = '\0';		/* just in case */
    return(CC_EOF);
}

/*ARGSUSED*/
CCRETVAL
e_complete(Char c)
{
    USE(c);
    *LastChar = '\0';		/* just in case */
    return(CC_COMPLETE);
}

/*ARGSUSED*/
CCRETVAL
e_complete_back(Char c)
{
    USE(c);
    *LastChar = '\0';		/* just in case */
    return(CC_COMPLETE_BACK);
}

/*ARGSUSED*/
CCRETVAL
e_complete_fwd(Char c)
{
    USE(c);
    *LastChar = '\0';		/* just in case */
    return(CC_COMPLETE_FWD);
}

/*ARGSUSED*/
CCRETVAL
e_complete_all(Char c)
{
    USE(c);
    *LastChar = '\0';		/* just in case */
    return(CC_COMPLETE_ALL);
}

/*ARGSUSED*/
CCRETVAL
v_cm_complete(Char c)
{
    USE(c);
    if (Cursor < LastChar)
	Cursor++;
    *LastChar = '\0';		/* just in case */
    return(CC_COMPLETE);
}

/*ARGSUSED*/
CCRETVAL
e_toggle_hist(Char c)
{
    struct Hist *hp;
    int     h;

    USE(c);
    *LastChar = '\0';		/* just in case */

    if (Hist_num <= 0) {
	return CC_ERROR;
    }

    hp = Histlist.Hnext;
    if (hp == NULL) {	/* this is only if no history */
	return(CC_ERROR);
    }

    for (h = 1; h < Hist_num; h++)
	hp = hp->Hnext;

    if (!CurrentHistLit) {
	if (hp->histline) {
	    copyn(InputBuf, hp->histline, INBUFSIZE);/*FIXBUF*/
	    CurrentHistLit = 1;
	}
	else {
	    return CC_ERROR;
	}
    }
    else {
	Char *p;

	p = sprlex(&hp->Hlex);
	copyn(InputBuf, p, sizeof(InputBuf) / sizeof(Char));/*FIXBUF*/
	xfree(p);
	CurrentHistLit = 0;
    }

    LastChar = Strend(InputBuf);
    if (LastChar > InputBuf) {
	if (LastChar[-1] == '\n')
	    LastChar--;
	if (LastChar[-1] == ' ')
	    LastChar--;
	if (LastChar < InputBuf)
	    LastChar = InputBuf;
    }

#ifdef KSHVI
    if (VImode)
	Cursor = InputBuf;
    else
#endif /* KSHVI */
	Cursor = LastChar;

    return(CC_REFRESH);
}

/*ARGSUSED*/
CCRETVAL
e_up_hist(Char c)
{
    Char    beep = 0;

    USE(c);
    UndoAction = TCSHOP_NOP;
    *LastChar = '\0';		/* just in case */

    if (Hist_num == 0) {	/* save the current buffer away */
	HistBuf.len = 0;
	Strbuf_append(&HistBuf, InputBuf);
	Strbuf_terminate(&HistBuf);
    }

    Hist_num += Argument;

    if (GetHistLine() == CC_ERROR) {
	beep = 1;
	(void) GetHistLine(); /* Hist_num was fixed by first call */
    }

    Refresh();
    if (beep)
	return(CC_ERROR);
    else
	return(CC_NORM);	/* was CC_UP_HIST */
}

/*ARGSUSED*/
CCRETVAL
e_down_hist(Char c)
{
    USE(c);
    UndoAction = TCSHOP_NOP;
    *LastChar = '\0';		/* just in case */

    Hist_num -= Argument;

    if (Hist_num < 0) {
	Hist_num = 0;
	return(CC_ERROR);	/* make it beep */
    }

    return(GetHistLine());
}



/*
 * c_hmatch() return True if the pattern matches the prefix
 */
static int
c_hmatch(Char *str)
{
    if (Strncmp(patbuf.s, str, patbuf.len) == 0)
	return 1;
    return Gmatch(str, patbuf.s);
}

/*
 * c_hsetpat(): Set the history seatch pattern
 */
static void
c_hsetpat(void)
{
    if (LastCmd != F_UP_SEARCH_HIST && LastCmd != F_DOWN_SEARCH_HIST) {
	patbuf.len = 0;
	Strbuf_appendn(&patbuf, InputBuf, Cursor - InputBuf);
	Strbuf_terminate(&patbuf);
    }
#ifdef SDEBUG
    xprintf("\nHist_num = %d\n", Hist_num);
    xprintf("patlen = %d\n", (int)patbuf.len);
    xprintf("patbuf = \"%S\"\n", patbuf.s);
    xprintf("Cursor %d LastChar %d\n", Cursor - InputBuf, LastChar - InputBuf);
#endif
}

/*ARGSUSED*/
CCRETVAL
e_up_search_hist(Char c)
{
    struct Hist *hp;
    int h;
    int    found = 0;

    USE(c);
    ActionFlag = TCSHOP_NOP;
    UndoAction = TCSHOP_NOP;
    *LastChar = '\0';		/* just in case */
    if (Hist_num < 0) {
#ifdef DEBUG_EDIT
	xprintf("%s: e_up_search_hist(): Hist_num < 0; resetting.\n", progname);
#endif
	Hist_num = 0;
	return(CC_ERROR);
    }

    if (Hist_num == 0) {
	HistBuf.len = 0;
	Strbuf_append(&HistBuf, InputBuf);
	Strbuf_terminate(&HistBuf);
    }


    hp = Histlist.Hnext;
    if (hp == NULL)
	return(CC_ERROR);

    c_hsetpat();		/* Set search pattern !! */

    for (h = 1; h <= Hist_num; h++)
	hp = hp->Hnext;

    while (hp != NULL) {
	Char *hl;
	int matched;

	if (hp->histline == NULL)
	    hp->histline = sprlex(&hp->Hlex);
	if (HistLit)
	    hl = hp->histline;
	else {
	    hl = sprlex(&hp->Hlex);
	    cleanup_push(hl, xfree);
	}
#ifdef SDEBUG
	xprintf("Comparing with \"%S\"\n", hl);
#endif
	matched = (Strncmp(hl, InputBuf, (size_t) (LastChar - InputBuf)) ||
		   hl[LastChar-InputBuf]) && c_hmatch(hl);
	if (!HistLit)
	    cleanup_until(hl);
	if (matched) {
	    found++;
	    break;
	}
	h++;
	hp = hp->Hnext;
    }

    if (!found) {
#ifdef SDEBUG
	xprintf("not found\n");
#endif
	return(CC_ERROR);
    }

    Hist_num = h;

    return(GetHistLine());
}

/*ARGSUSED*/
CCRETVAL
e_down_search_hist(Char c)
{
    struct Hist *hp;
    int h;
    int    found = 0;

    USE(c);
    ActionFlag = TCSHOP_NOP;
    UndoAction = TCSHOP_NOP;
    *LastChar = '\0';		/* just in case */

    if (Hist_num == 0)
	return(CC_ERROR);

    hp = Histlist.Hnext;
    if (hp == 0)
	return(CC_ERROR);

    c_hsetpat();		/* Set search pattern !! */

    for (h = 1; h < Hist_num && hp; h++) {
	Char *hl;
	if (hp->histline == NULL)
	    hp->histline = sprlex(&hp->Hlex);
	if (HistLit)
	    hl = hp->histline;
	else {
	    hl = sprlex(&hp->Hlex);
	    cleanup_push(hl, xfree);
	}
#ifdef SDEBUG
	xprintf("Comparing with \"%S\"\n", hl);
#endif
	if ((Strncmp(hl, InputBuf, (size_t) (LastChar - InputBuf)) || 
	     hl[LastChar-InputBuf]) && c_hmatch(hl))
	    found = h;
	if (!HistLit)
	    cleanup_until(hl);
	hp = hp->Hnext;
    }

    if (!found) {		/* is it the current history number? */
	if (!c_hmatch(HistBuf.s)) {
#ifdef SDEBUG
	    xprintf("not found\n");
#endif
	    return(CC_ERROR);
	}
    }

    Hist_num = found;

    return(GetHistLine());
}

/*ARGSUSED*/
CCRETVAL
e_helpme(Char c)
{
    USE(c);
    PastBottom();
    *LastChar = '\0';		/* just in case */
    return(CC_HELPME);
}

/*ARGSUSED*/
CCRETVAL
e_correct(Char c)
{
    USE(c);
    *LastChar = '\0';		/* just in case */
    return(CC_CORRECT);
}

/*ARGSUSED*/
CCRETVAL
e_correctl(Char c)
{
    USE(c);
    *LastChar = '\0';		/* just in case */
    return(CC_CORRECT_L);
}

/*ARGSUSED*/
CCRETVAL
e_run_fg_editor(Char c)
{
    struct process *pp;

    USE(c);
    if ((pp = find_stop_ed()) != NULL) {
	/* save our editor state so we can restore it */
	c_save_inputbuf();
	Hist_num = 0;		/* for the history commands */

	/* put the tty in a sane mode */
	PastBottom();
	(void) Cookedmode();	/* make sure the tty is set up correctly */

	/* do it! */
	fg_proc_entry(pp);

	(void) Rawmode();	/* go on */
	Refresh();
	RestoreSaved = 0;
	HistSaved = 0;
    }
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
e_list_choices(Char c)
{
    USE(c);
    PastBottom();
    *LastChar = '\0';		/* just in case */
    return(CC_LIST_CHOICES);
}

/*ARGSUSED*/
CCRETVAL
e_list_all(Char c)
{
    USE(c);
    PastBottom();
    *LastChar = '\0';		/* just in case */
    return(CC_LIST_ALL);
}

/*ARGSUSED*/
CCRETVAL
e_list_glob(Char c)
{
    USE(c);
    PastBottom();
    *LastChar = '\0';		/* just in case */
    return(CC_LIST_GLOB);
}

/*ARGSUSED*/
CCRETVAL
e_expand_glob(Char c)
{
    USE(c);
    *LastChar = '\0';		/* just in case */
    return(CC_EXPAND_GLOB);
}

/*ARGSUSED*/
CCRETVAL
e_normalize_path(Char c)
{
    USE(c);
    *LastChar = '\0';		/* just in case */
    return(CC_NORMALIZE_PATH);
}

/*ARGSUSED*/
CCRETVAL
e_normalize_command(Char c)
{
    USE(c);
    *LastChar = '\0';		/* just in case */
    return(CC_NORMALIZE_COMMAND);
}

/*ARGSUSED*/
CCRETVAL
e_expand_vars(Char c)
{
    USE(c);
    *LastChar = '\0';		/* just in case */
    return(CC_EXPAND_VARS);
}

/*ARGSUSED*/
CCRETVAL
e_which(Char c)
{				/* do a fast command line which(1) */
    USE(c);
    c_save_inputbuf();
    Hist_num = 0;		/* for the history commands */
    PastBottom();
    *LastChar = '\0';		/* just in case */
    return(CC_WHICH);
}

/*ARGSUSED*/
CCRETVAL
e_last_item(Char c)
{				/* insert the last element of the prev. cmd */
    struct Hist *hp;
    struct wordent *wp, *firstp;
    int i;
    Char *expanded;

    USE(c);
    if (Argument <= 0)
	return(CC_ERROR);

    hp = Histlist.Hnext;
    if (hp == NULL) {	/* this is only if no history */
	return(CC_ERROR);
    }

    wp = (hp->Hlex).prev;

    if (wp->prev == (struct wordent *) NULL)
	return(CC_ERROR);	/* an empty history entry */

    firstp = (hp->Hlex).next;

    /* back up arg words in lex */
    for (i = 0; i < Argument && wp != firstp; i++) {
	wp = wp->prev;
    }

    expanded = expand_lex(wp->prev, 0, i - 1);
    if (InsertStr(expanded)) {
	xfree(expanded);
	return(CC_ERROR);
    }

    xfree(expanded);
    return(CC_REFRESH);
}

/*ARGSUSED*/
CCRETVAL
e_dabbrev_expand(Char c)
{				/* expand to preceding word matching prefix */
    Char *cp, *ncp, *bp;
    struct Hist *hp;
    int arg = 0, i;
    size_t len = 0;
    int found = 0;
    Char *hbuf;
    static int oldevent, hist, word;
    static Char *start, *oldcursor;

    USE(c);
    if (Argument <= 0)
	return(CC_ERROR);

    cp = c_preword(Cursor, InputBuf, 1, STRshwordsep);
    if (cp == Cursor || Isspace(*cp))
	return(CC_ERROR);

    hbuf = NULL;
    hp = Histlist.Hnext;
    bp = InputBuf;
    if (Argument == 1 && eventno == oldevent && cp == start &&
	Cursor == oldcursor && patbuf.len > 0
	&& Strncmp(patbuf.s, cp, patbuf.len) == 0){
	/* continue previous search - go to last match (hist/word) */
	if (hist != 0) {		/* need to move up history */
	    for (i = 1; i < hist && hp != NULL; i++)
		hp = hp->Hnext;
	    if (hp == NULL)	/* "can't happen" */
		goto err_hbuf;
	    hbuf = expand_lex(&hp->Hlex, 0, INT_MAX);
	    cp = Strend(hbuf);
	    bp = hbuf;
	    hp = hp->Hnext;
	}
	cp = c_preword(cp, bp, word, STRshwordsep);
    } else {			/* starting new search */
	oldevent = eventno;
	start = cp;
	patbuf.len = 0;
	Strbuf_appendn(&patbuf, cp, Cursor - cp);
	hist = 0;
	word = 0;
    }

    while (!found) {
	ncp = c_preword(cp, bp, 1, STRshwordsep);
	if (ncp == cp || Isspace(*ncp)) { /* beginning of line */
	    hist++;
	    word = 0;
	    if (hp == NULL)
		goto err_hbuf;
	    hbuf = expand_lex(&hp->Hlex, 0, INT_MAX);
	    cp = Strend(hbuf);
	    bp = hbuf;
	    hp = hp->Hnext;
	    continue;
	} else {
	    word++;
	    len = c_endword(ncp-1, cp, 1, STRshwordsep) - ncp + 1;
	    cp = ncp;
	}
	if (len > patbuf.len && Strncmp(cp, patbuf.s, patbuf.len) == 0) {
	    /* We don't fully check distinct matches as Gnuemacs does: */
	    if (Argument > 1) {	/* just count matches */
		if (++arg >= Argument)
		    found++;
	    } else {		/* match if distinct from previous */
		if (len != (size_t)(Cursor - start)
		    || Strncmp(cp, start, len) != 0)
		    found++;
	    }
	}
    }

    if (LastChar + len - (Cursor - start) >= InputLim)
	goto err_hbuf;	/* no room */
    DeleteBack(Cursor - start);
    c_insert(len);
    while (len--)
	*Cursor++ = *cp++;
    oldcursor = Cursor;
    xfree(hbuf);
    return(CC_REFRESH);

 err_hbuf:
    xfree(hbuf);
    return CC_ERROR;
}

/*ARGSUSED*/
CCRETVAL
e_yank_kill(Char c)
{				/* almost like GnuEmacs */
    int len;
    Char *kp, *cp;

    USE(c);
    if (KillRingLen == 0)	/* nothing killed */
	return(CC_ERROR);
    len = Strlen(KillRing[YankPos].buf);
    if (LastChar + len >= InputLim)
	return(CC_ERROR);	/* end of buffer space */

    /* else */
    cp = Cursor;		/* for speed */

    c_insert(len);		/* open the space, */
    for (kp = KillRing[YankPos].buf; *kp; kp++)	/* copy the chars */
	*cp++ = *kp;

    if (Argument == 1) {	/* if no arg */
	Mark = Cursor;		/* mark at beginning, cursor at end */
	Cursor = cp;
    } else {
	Mark = cp;		/* else cursor at beginning, mark at end */
    }

    if (adrof(STRhighlight) && MarkIsSet) {
	ClearLines();
	ClearDisp();
    }
    MarkIsSet = 0;
    return(CC_REFRESH);
}

/*ARGSUSED*/
CCRETVAL
e_yank_pop(Char c)
{				/* almost like GnuEmacs */
    int m_bef_c, del_len, ins_len;
    Char *kp, *cp;

    USE(c);

#if 0
    /* XXX This "should" be here, but doesn't work, since LastCmd
       gets set on CC_ERROR and CC_ARGHACK, which it shouldn't(?).
       (But what about F_ARGFOUR?) I.e. if you hit M-y twice the
       second one will "succeed" even if the first one wasn't preceded
       by a yank, and giving an argument is impossible. Now we "succeed"
       regardless of previous command, which is wrong too of course. */
    if (LastCmd != F_YANK_KILL && LastCmd != F_YANK_POP)
	return(CC_ERROR);
#endif

    if (KillRingLen == 0)	/* nothing killed */
	return(CC_ERROR);
    YankPos -= Argument;
    while (YankPos < 0)
	YankPos += KillRingLen;
    YankPos %= KillRingLen;

    if (Cursor > Mark) {
	del_len = Cursor - Mark;
	m_bef_c = 1;
    } else {
	del_len = Mark - Cursor;
	m_bef_c = 0;
    }
    ins_len = Strlen(KillRing[YankPos].buf);
    if (LastChar + ins_len - del_len >= InputLim)
	return(CC_ERROR);	/* end of buffer space */

    if (m_bef_c) {
	c_delbefore(del_len);
    } else {
	c_delafter(del_len);
    }
    cp = Cursor;		/* for speed */

    c_insert(ins_len);		/* open the space, */
    for (kp = KillRing[YankPos].buf; *kp; kp++)	/* copy the chars */
	*cp++ = *kp;

    if (m_bef_c) {
	Mark = Cursor;		/* mark at beginning, cursor at end */
	Cursor = cp;
    } else {
	Mark = cp;		/* else cursor at beginning, mark at end */
    }

    if (adrof(STRhighlight) && MarkIsSet) {
	ClearLines();
	ClearDisp();
    }
    MarkIsSet = 0;
    return(CC_REFRESH);
}

/*ARGSUSED*/
CCRETVAL
v_delprev(Char c) 		/* Backspace key in insert mode */
{
    int rc;

    USE(c);
    rc = CC_ERROR;

    if (InsertPos != 0) {
	if (Argument <= Cursor - InsertPos) {
	    c_delbefore(Argument);	/* delete before */
	    rc = CC_REFRESH;
	}
    }
    return(rc);
}   /* v_delprev  */

/*ARGSUSED*/
CCRETVAL
e_delprev(Char c)
{
    USE(c);
    if (Cursor > InputBuf) {
	c_delbefore(Argument);	/* delete before dot */
	return(CC_REFRESH);
    }
    else {
	return(CC_ERROR);
    }
}

/*ARGSUSED*/
CCRETVAL
e_delwordprev(Char c)
{
    Char *cp;

    USE(c);
    if (Cursor == InputBuf)
	return(CC_ERROR);
    /* else */

    cp = c_prev_word(Cursor, InputBuf, Argument);

    c_push_kill(cp, Cursor);	/* save the text */

    c_delbefore((int)(Cursor - cp));	/* delete before dot */
    return(CC_REFRESH);
}

/* DCS <dcs@neutron.chem.yale.edu>, 9 Oct 93
 *
 * Changed the names of some of the ^D family of editor functions to
 * correspond to what they actually do and created new e_delnext_list
 * for completeness.
 *   
 *   Old names:			New names:
 *   
 *   delete-char		delete-char-or-eof
 *     F_DELNEXT		  F_DELNEXT_EOF
 *     e_delnext		  e_delnext_eof
 *     edelnxt			  edelnxteof
 *   delete-char-or-eof		delete-char			
 *     F_DELNEXT_EOF		  F_DELNEXT
 *     e_delnext_eof		  e_delnext
 *     edelnxteof		  edelnxt
 *   delete-char-or-list	delete-char-or-list-or-eof
 *     F_LIST_DELNEXT		  F_DELNEXT_LIST_EOF
 *     e_list_delnext		  e_delnext_list_eof
 *   				  edellsteof
 *   (no old equivalent)	delete-char-or-list
 *   				  F_DELNEXT_LIST
 *   				  e_delnext_list
 *   				  e_delnxtlst
 */

/* added by mtk@ari.ncl.omron.co.jp (920818) */
/* rename e_delnext() -> e_delnext_eof() */
/*ARGSUSED*/
CCRETVAL
e_delnext(Char c)
{
    USE(c);
    if (Cursor == LastChar) {/* if I'm at the end */
	if (!VImode) {
		return(CC_ERROR);
	}
	else {
	    if (Cursor != InputBuf)
		Cursor--;
	    else
		return(CC_ERROR);
	}
    }
    c_delafter(Argument);	/* delete after dot */
    if (Cursor > LastChar)
	Cursor = LastChar;	/* bounds check */
    return(CC_REFRESH);
}


/*ARGSUSED*/
CCRETVAL
e_delnext_eof(Char c)
{
    USE(c);
    if (Cursor == LastChar) {/* if I'm at the end */
	if (!VImode) {
	    if (Cursor == InputBuf) {	
		/* if I'm also at the beginning */
		so_write(STReof, 4);/* then do a EOF */
		flush();
		return(CC_EOF);
	    }
	    else 
		return(CC_ERROR);
	}
	else {
	    if (Cursor != InputBuf)
		Cursor--;
	    else
		return(CC_ERROR);
	}
    }
    c_delafter(Argument);	/* delete after dot */
    if (Cursor > LastChar)
	Cursor = LastChar;	/* bounds check */
    return(CC_REFRESH);
}

/*ARGSUSED*/
CCRETVAL
e_delnext_list(Char c)
{
    USE(c);
    if (Cursor == LastChar) {	/* if I'm at the end */
	PastBottom();
	*LastChar = '\0';	/* just in case */
	return(CC_LIST_CHOICES);
    }
    else {
	c_delafter(Argument);	/* delete after dot */
	if (Cursor > LastChar)
	    Cursor = LastChar;	/* bounds check */
	return(CC_REFRESH);
    }
}

/*ARGSUSED*/
CCRETVAL
e_delnext_list_eof(Char c)
{
    USE(c);
    if (Cursor == LastChar) {	/* if I'm at the end */
	if (Cursor == InputBuf) {	/* if I'm also at the beginning */
	    so_write(STReof, 4);/* then do a EOF */
	    flush();
	    return(CC_EOF);
	}
	else {
	    PastBottom();
	    *LastChar = '\0';	/* just in case */
	    return(CC_LIST_CHOICES);
	}
    }
    else {
	c_delafter(Argument);	/* delete after dot */
	if (Cursor > LastChar)
	    Cursor = LastChar;	/* bounds check */
	return(CC_REFRESH);
    }
}

/*ARGSUSED*/
CCRETVAL
e_list_eof(Char c)
{
    CCRETVAL rv;

    USE(c);
    if (Cursor == LastChar && Cursor == InputBuf) {
	so_write(STReof, 4);	/* then do a EOF */
	flush();
	rv = CC_EOF;
    }
    else {
	PastBottom();
	*LastChar = '\0';	/* just in case */
	rv = CC_LIST_CHOICES;
    }
    return rv;
}

/*ARGSUSED*/
CCRETVAL
e_delwordnext(Char c)
{
    Char *cp;

    USE(c);
    if (Cursor == LastChar)
	return(CC_ERROR);
    /* else */

    cp = c_next_word(Cursor, LastChar, Argument);

    c_push_kill(Cursor, cp);	/* save the text */

    c_delafter((int)(cp - Cursor));	/* delete after dot */
    if (Cursor > LastChar)
	Cursor = LastChar;	/* bounds check */
    return(CC_REFRESH);
}

/*ARGSUSED*/
CCRETVAL
e_toend(Char c)
{
    USE(c);
    Cursor = LastChar;
    if (VImode)
	if (ActionFlag & TCSHOP_DELETE) {
	    c_delfini();
	    return(CC_REFRESH);
	}
    RefCursor();		/* move the cursor */
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
e_tobeg(Char c)
{
    USE(c);
    Cursor = InputBuf;

    if (VImode) {
       while (Isspace(*Cursor)) /* We want FIRST non space character */
	Cursor++;
	if (ActionFlag & TCSHOP_DELETE) {
	    c_delfini();
	    return(CC_REFRESH);
	}
    }

    RefCursor();		/* move the cursor */
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
e_killend(Char c)
{
    USE(c);
    c_push_kill(Cursor, LastChar); /* copy it */
    LastChar = Cursor;		/* zap! -- delete to end */
    if (Mark > Cursor)
        Mark = Cursor;
    MarkIsSet = 0;
    return(CC_REFRESH);
}


/*ARGSUSED*/
CCRETVAL
e_killbeg(Char c)
{
    USE(c);
    c_push_kill(InputBuf, Cursor); /* copy it */
    c_delbefore((int)(Cursor - InputBuf));
    if (Mark && Mark > Cursor)
        Mark -= Cursor-InputBuf;
    return(CC_REFRESH);
}

/*ARGSUSED*/
CCRETVAL
e_killall(Char c)
{
    USE(c);
    c_push_kill(InputBuf, LastChar); /* copy it */
    Cursor = Mark = LastChar = InputBuf;	/* zap! -- delete all of it */
    MarkIsSet = 0;
    return(CC_REFRESH);
}

/*ARGSUSED*/
CCRETVAL
e_killregion(Char c)
{
    USE(c);
    if (!Mark)
	return(CC_ERROR);

    if (Mark > Cursor) {
	c_push_kill(Cursor, Mark); /* copy it */
	c_delafter((int)(Mark - Cursor)); /* delete it - UNUSED BY VI mode */
	Mark = Cursor;
    }
    else {			/* mark is before cursor */
	c_push_kill(Mark, Cursor); /* copy it */
	c_delbefore((int)(Cursor - Mark));
    }
    if (adrof(STRhighlight) && MarkIsSet) {
	ClearLines();
	ClearDisp();
    }
    MarkIsSet = 0;
    return(CC_REFRESH);
}

/*ARGSUSED*/
CCRETVAL
e_copyregion(Char c)
{
    USE(c);
    if (!Mark)
	return(CC_ERROR);

    if (Mark > Cursor) {
	c_push_kill(Cursor, Mark); /* copy it */
    }
    else {			/* mark is before cursor */
	c_push_kill(Mark, Cursor); /* copy it */
    }
    return(CC_NORM);		/* don't even need to Refresh() */
}

/*ARGSUSED*/
CCRETVAL
e_charswitch(Char cc)
{
    Char c;

    USE(cc);

    /* do nothing if we are at beginning of line or have only one char */
    if (Cursor == &InputBuf[0] || LastChar == &InputBuf[1]) {
	return(CC_ERROR);
    }

    if (Cursor < LastChar) {
	Cursor++;
    }
    c = Cursor[-2];
    Cursor[-2] = Cursor[-1];
    Cursor[-1] = c;
    return(CC_REFRESH);
}

/*ARGSUSED*/
CCRETVAL
e_gcharswitch(Char cc)
{				/* gosmacs style ^T */
    Char c;

    USE(cc);
    if (Cursor > &InputBuf[1]) {/* must have at least two chars entered */
	c = Cursor[-2];
	Cursor[-2] = Cursor[-1];
	Cursor[-1] = c;
	return(CC_REFRESH);
    }
    else {
	return(CC_ERROR);
    }
}

/*ARGSUSED*/
CCRETVAL
e_charback(Char c)
{
    USE(c);
    if (Cursor > InputBuf) {
	if (Argument > Cursor - InputBuf)
	    Cursor = InputBuf;
	else
	    Cursor -= Argument;

	if (VImode)
	    if (ActionFlag & TCSHOP_DELETE) {
		c_delfini();
		return(CC_REFRESH);
	    }

	RefCursor();
	return(CC_NORM);
    }
    else {
	return(CC_ERROR);
    }
}

/*ARGSUSED*/
CCRETVAL
v_wordback(Char c)
{
    USE(c);
    if (Cursor == InputBuf)
	return(CC_ERROR);
    /* else */

    Cursor = c_preword(Cursor, InputBuf, Argument, STRshwspace); /* bounds check */

    if (ActionFlag & TCSHOP_DELETE) {
	c_delfini();
	return(CC_REFRESH);
    }

    RefCursor();
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
e_wordback(Char c)
{
    USE(c);
    if (Cursor == InputBuf)
	return(CC_ERROR);
    /* else */

    Cursor = c_prev_word(Cursor, InputBuf, Argument); /* bounds check */

    if (VImode) 
	if (ActionFlag & TCSHOP_DELETE) {
	    c_delfini();
	    return(CC_REFRESH);
	}

    RefCursor();
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
e_charfwd(Char c)
{
    USE(c);
    if (Cursor < LastChar) {
	Cursor += Argument;
	if (Cursor > LastChar)
	    Cursor = LastChar;

	if (VImode)
	    if (ActionFlag & TCSHOP_DELETE) {
		c_delfini();
		return(CC_REFRESH);
	    }

	RefCursor();
	return(CC_NORM);
    }
    else {
	return(CC_ERROR);
    }
}

/*ARGSUSED*/
CCRETVAL
e_wordfwd(Char c)
{
    USE(c);
    if (Cursor == LastChar)
	return(CC_ERROR);
    /* else */

    Cursor = c_next_word(Cursor, LastChar, Argument);

    if (VImode)
	if (ActionFlag & TCSHOP_DELETE) {
	    c_delfini();
	    return(CC_REFRESH);
	}

    RefCursor();
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
v_wordfwd(Char c)
{
    USE(c);
    if (Cursor == LastChar)
	return(CC_ERROR);
    /* else */

    Cursor = c_nexword(Cursor, LastChar, Argument);

    if (VImode)
	if (ActionFlag & TCSHOP_DELETE) {
	    c_delfini();
	    return(CC_REFRESH);
	}

    RefCursor();
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
v_wordbegnext(Char c)
{
    USE(c);
    if (Cursor == LastChar)
	return(CC_ERROR);
    /* else */

    Cursor = c_next_word(Cursor, LastChar, Argument);
    if (Cursor < LastChar)
	Cursor++;

    if (VImode)
	if (ActionFlag & TCSHOP_DELETE) {
	    c_delfini();
	    return(CC_REFRESH);
	}

    RefCursor();
    return(CC_NORM);
}

/*ARGSUSED*/
static CCRETVAL
v_repeat_srch(int c)
{
    CCRETVAL rv = CC_ERROR;
#ifdef SDEBUG
    xprintf("dir %d patlen %d patbuf %S\n",
	    c, (int)patbuf.len, patbuf.s);
#endif

    LastCmd = (KEYCMD) c;  /* Hack to stop c_hsetpat */
    LastChar = InputBuf;
    switch (c) {
    case F_DOWN_SEARCH_HIST:
	rv = e_down_search_hist(0);
	break;
    case F_UP_SEARCH_HIST:
	rv = e_up_search_hist(0);
	break;
    default:
	break;
    }
    return rv;
}

static CCRETVAL
v_csearch_back(Char ch, int count, int tflag)
{
    Char *cp;

    cp = Cursor;
    while (count--) {
	if (*cp == ch) 
	    cp--;
	while (cp > InputBuf && *cp != ch) 
	    cp--;
    }

    if (cp < InputBuf || (cp == InputBuf && *cp != ch))
	return(CC_ERROR);

    if (*cp == ch && tflag)
	cp++;

    Cursor = cp;

    if (ActionFlag & TCSHOP_DELETE) {
	Cursor++;
	c_delfini();
	return(CC_REFRESH);
    }

    RefCursor();
    return(CC_NORM);
}

static CCRETVAL
v_csearch_fwd(Char ch, int count, int tflag)
{
    Char *cp;

    cp = Cursor;
    while (count--) {
	if(*cp == ch) 
	    cp++;
	while (cp < LastChar && *cp != ch) 
	    cp++;
    }

    if (cp >= LastChar)
	return(CC_ERROR);

    if (*cp == ch && tflag)
	cp--;

    Cursor = cp;

    if (ActionFlag & TCSHOP_DELETE) {
	Cursor++;
	c_delfini();
	return(CC_REFRESH);
    }
    RefCursor();
    return(CC_NORM);
}

/*ARGSUSED*/
static CCRETVAL
v_action(int c)
{
    Char *cp, *kp;

    if (ActionFlag == TCSHOP_DELETE) {
	ActionFlag = TCSHOP_NOP;
	ActionPos = 0;
	
	UndoSize = 0;
	kp = UndoBuf;
	for (cp = InputBuf; cp < LastChar; cp++) {
	    *kp++ = *cp;
	    UndoSize++;
	}
		
	UndoAction = TCSHOP_INSERT;
	UndoPtr  = InputBuf;
	LastChar = InputBuf;
	Cursor   = InputBuf;
	if (c & TCSHOP_INSERT)
	    c_alternativ_key_map(0);
	    
	return(CC_REFRESH);
    }
#ifdef notdef
    else if (ActionFlag == TCSHOP_NOP) {
#endif
	ActionPos = Cursor;
	ActionFlag = c;
	return(CC_ARGHACK);  /* Do NOT clear out argument */
#ifdef notdef
    }
    else {
	ActionFlag = 0;
	ActionPos = 0;
	return(CC_ERROR);
    }
#endif
}

#ifdef COMMENT
/* by: Brian Allison <uiucdcs!convex!allison@RUTGERS.EDU> */
static void
c_get_word(Char **begin, Char **end)
{
    Char   *cp;

    cp = &Cursor[0];
    while (Argument--) {
	while ((cp <= LastChar) && (isword(*cp)))
	    cp++;
	*end = --cp;
	while ((cp >= InputBuf) && (isword(*cp)))
	    cp--;
	*begin = ++cp;
    }
}
#endif /* COMMENT */

/*ARGSUSED*/
CCRETVAL
e_uppercase(Char c)
{
    Char   *cp, *end;

    USE(c);
    end = c_next_word(Cursor, LastChar, Argument);

    for (cp = Cursor; cp < end; cp++)	/* PWP: was cp=begin */
	if (Islower(*cp))
	    *cp = Toupper(*cp);

    Cursor = end;
    if (Cursor > LastChar)
	Cursor = LastChar;
    return(CC_REFRESH);
}


/*ARGSUSED*/
CCRETVAL
e_capitalcase(Char c)
{
    Char   *cp, *end;

    USE(c);
    end = c_next_word(Cursor, LastChar, Argument);

    cp = Cursor;
    for (; cp < end; cp++) {
	if (Isalpha(*cp)) {
	    if (Islower(*cp))
		*cp = Toupper(*cp);
	    cp++;
	    break;
	}
    }
    for (; cp < end; cp++)
	if (Isupper(*cp))
	    *cp = Tolower(*cp);

    Cursor = end;
    if (Cursor > LastChar)
	Cursor = LastChar;
    return(CC_REFRESH);
}

/*ARGSUSED*/
CCRETVAL
e_lowercase(Char c)
{
    Char   *cp, *end;

    USE(c);
    end = c_next_word(Cursor, LastChar, Argument);

    for (cp = Cursor; cp < end; cp++)
	if (Isupper(*cp))
	    *cp = Tolower(*cp);

    Cursor = end;
    if (Cursor > LastChar)
	Cursor = LastChar;
    return(CC_REFRESH);
}


/*ARGSUSED*/
CCRETVAL
e_set_mark(Char c)
{
    USE(c);
    if (adrof(STRhighlight) && MarkIsSet && Mark != Cursor) {
	ClearLines();
	ClearDisp();
	Refresh();
    }
    Mark = Cursor;
    MarkIsSet = 1;
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
e_exchange_mark(Char c)
{
    Char *cp;

    USE(c);
    cp = Cursor;
    Cursor = Mark;
    Mark = cp;
    RefCursor();
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
e_argfour(Char c)
{				/* multiply current argument by 4 */
    USE(c);
    if (Argument > 1000000)
	return CC_ERROR;
    DoingArg = 1;
    Argument *= 4;
    return(CC_ARGHACK);
}

static void
quote_mode_cleanup(void *unused)
{
    USE(unused);
    QuoteModeOff();
}

/*ARGSUSED*/
CCRETVAL
e_quote(Char c)
{
    Char    ch;
    int     num;

    USE(c);
    QuoteModeOn();
    cleanup_push(&c, quote_mode_cleanup); /* Using &c just as a mark */
    num = GetNextChar(&ch);
    cleanup_until(&c);
    if (num == 1)
	return e_insert(ch);
    else
	return e_send_eof(0);
}

/*ARGSUSED*/
CCRETVAL
e_metanext(Char c)
{
    USE(c);
    MetaNext = 1;
    return(CC_ARGHACK);	/* preserve argument */
}

#ifdef notdef
/*ARGSUSED*/
CCRETVAL
e_extendnext(Char c)
{
    CurrentKeyMap = CcAltMap;
    return(CC_ARGHACK);	/* preserve argument */
}

#endif

/*ARGSUSED*/
CCRETVAL
v_insbeg(Char c)
{				/* move to beginning of line and start vi
				 * insert mode */
    USE(c);
    Cursor = InputBuf;
    InsertPos = Cursor;

    UndoPtr  = Cursor;
    UndoAction = TCSHOP_DELETE;

    RefCursor();		/* move the cursor */
    c_alternativ_key_map(0);
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
v_replone(Char c)
{				/* vi mode overwrite one character */
    USE(c);
    c_alternativ_key_map(0);
    inputmode = MODE_REPLACE_1;
    UndoAction = TCSHOP_CHANGE;	/* Set Up for VI undo command */
    UndoPtr = Cursor;
    UndoSize = 0;
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
v_replmode(Char c)
{				/* vi mode start overwriting */
    USE(c);
    c_alternativ_key_map(0);
    inputmode = MODE_REPLACE;
    UndoAction = TCSHOP_CHANGE;	/* Set Up for VI undo command */
    UndoPtr = Cursor;
    UndoSize = 0;
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
v_substchar(Char c)
{				/* vi mode substitute for one char */
    USE(c);
    c_delafter(Argument);
    c_alternativ_key_map(0);
    return(CC_REFRESH);
}

/*ARGSUSED*/
CCRETVAL
v_substline(Char c)
{				/* vi mode replace whole line */
    USE(c);
    (void) e_killall(0);
    c_alternativ_key_map(0);
    return(CC_REFRESH);
}

/*ARGSUSED*/
CCRETVAL
v_chgtoend(Char c)
{				/* vi mode change to end of line */
    USE(c);
    (void) e_killend(0);
    c_alternativ_key_map(0);
    return(CC_REFRESH);
}

/*ARGSUSED*/
CCRETVAL
v_insert(Char c)
{				/* vi mode start inserting */
    USE(c);
    c_alternativ_key_map(0);

    InsertPos = Cursor;
    UndoPtr = Cursor;
    UndoAction = TCSHOP_DELETE;

    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
v_add(Char c)
{				/* vi mode start adding */
    USE(c);
    c_alternativ_key_map(0);
    if (Cursor < LastChar)
    {
	Cursor++;
	if (Cursor > LastChar)
	    Cursor = LastChar;
	RefCursor();
    }

    InsertPos = Cursor;
    UndoPtr = Cursor;
    UndoAction = TCSHOP_DELETE;

    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
v_addend(Char c)
{				/* vi mode to add at end of line */
    USE(c);
    c_alternativ_key_map(0);
    Cursor = LastChar;

    InsertPos = LastChar;	/* Mark where insertion begins */
    UndoPtr = LastChar;
    UndoAction = TCSHOP_DELETE;

    RefCursor();
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
v_change_case(Char cc)
{
    Char    c;

    USE(cc);
    if (Cursor < LastChar) {
#ifndef WINNT_NATIVE
	c = *Cursor;
#else
	c = CHAR & *Cursor;
#endif /* WINNT_NATIVE */
	if (Isupper(c))
	    *Cursor++ = Tolower(c);
	else if (Islower(c))
	    *Cursor++ = Toupper(c);
	else
	    Cursor++;
	RefPlusOne(1);		/* fast refresh for one char */
	return(CC_NORM);
    }
    return(CC_ERROR);
}

/*ARGSUSED*/
CCRETVAL
e_expand(Char c)
{
    Char *p;

    USE(c);
    for (p = InputBuf; Isspace(*p); p++)
	continue;
    if (p == LastChar)
	return(CC_ERROR);

    justpr++;
    Expand++;
    return(e_newline(0));
}

/*ARGSUSED*/
CCRETVAL
e_startover(Char c)
{				/* erase all of current line, start again */
    USE(c);
    ResetInLine(0);		/* reset the input pointers */
    return(CC_REFRESH);
}

/*ARGSUSED*/
CCRETVAL
e_redisp(Char c)
{
    USE(c);
    ClearLines();
    ClearDisp();
    return(CC_REFRESH);
}

/*ARGSUSED*/
CCRETVAL
e_cleardisp(Char c)
{
    USE(c);
    ClearScreen();		/* clear the whole real screen */
    ClearDisp();		/* reset everything */
    return(CC_REFRESH);
}

/*ARGSUSED*/
CCRETVAL
e_tty_int(Char c)
{			
    USE(c);
#if defined(_MINIX) || defined(WINNT_NATIVE)
    /* SAK PATCH: erase all of current line, start again */
    ResetInLine(0);		/* reset the input pointers */
    xputchar('\n');
    ClearDisp();
    return (CC_REFRESH);
#else /* !_MINIX && !WINNT_NATIVE */
    /* do no editing */
    return (CC_NORM);
#endif /* _MINIX || WINNT_NATIVE */
}

/*
 * From: ghazi@cesl.rutgers.edu (Kaveh R. Ghazi)
 * Function to send a character back to the input stream in cooked
 * mode. Only works if we have TIOCSTI
 */
/*ARGSUSED*/
CCRETVAL
e_stuff_char(Char c)
{
#ifdef TIOCSTI
     int was_raw = Tty_raw_mode;
     char buf[MB_LEN_MAX];
     size_t i, len;

     if (was_raw)
         (void) Cookedmode();

     (void) xwrite(SHIN, "\n", 1);
     len = one_wctomb(buf, c);
     for (i = 0; i < len; i++)
	 (void) ioctl(SHIN, TIOCSTI, (ioctl_t) &buf[i]);

     if (was_raw)
	 (void) Rawmode();
     return(e_redisp(c));
#else /* !TIOCSTI */  
     return(CC_ERROR);
#endif /* !TIOCSTI */  
}

/*ARGSUSED*/
CCRETVAL
e_insovr(Char c)
{
    USE(c);
    inputmode = (inputmode == MODE_INSERT ? MODE_REPLACE : MODE_INSERT);
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
e_tty_dsusp(Char c)
{
    USE(c);
    /* do no editing */
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
e_tty_flusho(Char c)
{
    USE(c);
    /* do no editing */
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
e_tty_quit(Char c)
{
    USE(c);
    /* do no editing */
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
e_tty_tsusp(Char c)
{
    USE(c);
    /* do no editing */
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
e_tty_stopo(Char c)
{
    USE(c);
    /* do no editing */
    return(CC_NORM);
}

/* returns the number of (attempted) expansions */
int
ExpandHistory(void)
{
    *LastChar = '\0';		/* just in case */
    return c_substitute();
}

/*ARGSUSED*/
CCRETVAL
e_expand_history(Char c)
{
    USE(c);
    (void)ExpandHistory();
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
e_magic_space(Char c)
{
    USE(c);
    *LastChar = '\0';		/* just in case */
    (void)c_substitute();
    return(e_insert(' '));
}

/*ARGSUSED*/
CCRETVAL
e_inc_fwd(Char c)
{
    CCRETVAL ret;

    USE(c);
    patbuf.len = 0;
    MarkIsSet = 0;
    ret = e_inc_search(F_DOWN_SEARCH_HIST);
    if (adrof(STRhighlight) && IncMatchLen) {
	IncMatchLen = 0;
	ClearLines();
	ClearDisp();
	Refresh();
    }
    IncMatchLen = 0;
    return ret;
}


/*ARGSUSED*/
CCRETVAL
e_inc_back(Char c)
{
    CCRETVAL ret;

    USE(c);
    patbuf.len = 0;
    MarkIsSet = 0;
    ret = e_inc_search(F_UP_SEARCH_HIST);
    if (adrof(STRhighlight) && IncMatchLen) {
	IncMatchLen = 0;
	ClearLines();
	ClearDisp();
	Refresh();
    }
    IncMatchLen = 0;
    return ret;
}

/*ARGSUSED*/
CCRETVAL
e_copyprev(Char c)
{
    Char *cp, *oldc, *dp;

    USE(c);
    if (Cursor == InputBuf)
	return(CC_ERROR);
    /* else */

    oldc = Cursor;
    /* does a bounds check */
    cp = c_prev_word(Cursor, InputBuf, Argument);	

    c_insert((int)(oldc - cp));
    for (dp = oldc; cp < oldc && dp < LastChar; cp++)
	*dp++ = *cp;

    Cursor = dp;		/* put cursor at end */

    return(CC_REFRESH);
}

/*ARGSUSED*/
CCRETVAL
e_tty_starto(Char c)
{
    USE(c);
    /* do no editing */
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
e_load_average(Char c)
{
    USE(c);
    PastBottom();
#ifdef TIOCSTAT
    /*
     * Here we pass &c to the ioctl because some os's (NetBSD) expect it
     * there even if they don't use it. (lukem@netbsd.org)
     */
    if (ioctl(SHIN, TIOCSTAT, (ioctl_t) &c) < 0) 
#endif
	xprintf("%s", CGETS(5, 1, "Load average unavailable\n"));
    return(CC_REFRESH);
}

/*ARGSUSED*/
CCRETVAL
v_chgmeta(Char c)
{
    USE(c);
    /*
     * Delete with insert == change: first we delete and then we leave in
     * insert mode.
     */
    return(v_action(TCSHOP_DELETE|TCSHOP_INSERT));
}

/*ARGSUSED*/
CCRETVAL
v_delmeta(Char c)
{
    USE(c);
    return(v_action(TCSHOP_DELETE));
}


/*ARGSUSED*/
CCRETVAL
v_endword(Char c)
{
    USE(c);
    if (Cursor == LastChar)
	return(CC_ERROR);
    /* else */

    Cursor = c_endword(Cursor, LastChar, Argument, STRshwspace);

    if (ActionFlag & TCSHOP_DELETE)
    {
	Cursor++;
	c_delfini();
	return(CC_REFRESH);
    }

    RefCursor();
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
v_eword(Char c)
{
    USE(c);
    if (Cursor == LastChar)
	return(CC_ERROR);
    /* else */

    Cursor = c_eword(Cursor, LastChar, Argument);

    if (ActionFlag & TCSHOP_DELETE) {
	Cursor++;
	c_delfini();
	return(CC_REFRESH);
    }

    RefCursor();
    return(CC_NORM);
}

/*ARGSUSED*/
CCRETVAL
v_char_fwd(Char c)
{
    Char ch;

    USE(c);
    if (GetNextChar(&ch) != 1)
	return e_send_eof(0);

    srch_dir = CHAR_FWD;
    srch_char = ch;

    return v_csearch_fwd(ch, Argument, 0);

}

/*ARGSUSED*/
CCRETVAL
v_char_back(Char c)
{
    Char ch;

    USE(c);
    if (GetNextChar(&ch) != 1)
	return e_send_eof(0);

    srch_dir = CHAR_BACK;
    srch_char = ch;

    return v_csearch_back(ch, Argument, 0);
}

/*ARGSUSED*/
CCRETVAL
v_charto_fwd(Char c)
{
    Char ch;

    USE(c);
    if (GetNextChar(&ch) != 1)
	return e_send_eof(0);

    return v_csearch_fwd(ch, Argument, 1);

}

/*ARGSUSED*/
CCRETVAL
v_charto_back(Char c)
{
    Char ch;

    USE(c);
    if (GetNextChar(&ch) != 1)
	return e_send_eof(0);

    return v_csearch_back(ch, Argument, 1);
}

/*ARGSUSED*/
CCRETVAL
v_rchar_fwd(Char c)
{
    USE(c);
    if (srch_char == 0)
	return CC_ERROR;

    return srch_dir == CHAR_FWD ? v_csearch_fwd(srch_char, Argument, 0) : 
			          v_csearch_back(srch_char, Argument, 0);
}

/*ARGSUSED*/
CCRETVAL
v_rchar_back(Char c)
{
    USE(c);
    if (srch_char == 0)
	return CC_ERROR;

    return srch_dir == CHAR_BACK ? v_csearch_fwd(srch_char, Argument, 0) : 
			           v_csearch_back(srch_char, Argument, 0);
}

/*ARGSUSED*/
CCRETVAL
v_undo(Char c)
{
    int  loop;
    Char *kp, *cp;
    Char temp;
    int	 size;

    USE(c);
    switch (UndoAction) {
    case TCSHOP_DELETE|TCSHOP_INSERT:
    case TCSHOP_DELETE:
	if (UndoSize == 0) return(CC_NORM);
	cp = UndoPtr;
	kp = UndoBuf;
	for (loop=0; loop < UndoSize; loop++)	/* copy the chars */
	    *kp++ = *cp++;			/* into UndoBuf   */

	for (cp = UndoPtr; cp <= LastChar; cp++)
	    *cp = cp[UndoSize];

	LastChar -= UndoSize;
	Cursor   =  UndoPtr;
	
	UndoAction = TCSHOP_INSERT;
	break;

    case TCSHOP_INSERT:
	if (UndoSize == 0) return(CC_NORM);
	cp = UndoPtr;
	Cursor = UndoPtr;
	kp = UndoBuf;
	c_insert(UndoSize);		/* open the space, */
	for (loop = 0; loop < UndoSize; loop++)	/* copy the chars */
	    *cp++ = *kp++;

	UndoAction = TCSHOP_DELETE;
	break;

    case TCSHOP_CHANGE:
	if (UndoSize == 0) return(CC_NORM);
	cp = UndoPtr;
	Cursor = UndoPtr;
	kp = UndoBuf;
	size = (int)(Cursor-LastChar); /*  NOT NSL independant */
	if (size < UndoSize)
	    size = UndoSize;
	for(loop = 0; loop < size; loop++) {
	    temp = *kp;
	    *kp++ = *cp;
	    *cp++ = temp;
	}
	break;

    default:
	return(CC_ERROR);
    }

    return(CC_REFRESH);
}

/*ARGSUSED*/
CCRETVAL
v_ush_meta(Char c)
{
    USE(c);
    return v_search(F_UP_SEARCH_HIST);
}

/*ARGSUSED*/
CCRETVAL
v_dsh_meta(Char c)
{
    USE(c);
    return v_search(F_DOWN_SEARCH_HIST);
}

/*ARGSUSED*/
CCRETVAL
v_rsrch_fwd(Char c)
{
    USE(c);
    if (patbuf.len == 0) return(CC_ERROR);
    return(v_repeat_srch(searchdir));
}

/*ARGSUSED*/
CCRETVAL
v_rsrch_back(Char c)
{
    USE(c);
    if (patbuf.len == 0) return(CC_ERROR);
    return(v_repeat_srch(searchdir == F_UP_SEARCH_HIST ? 
			 F_DOWN_SEARCH_HIST : F_UP_SEARCH_HIST));
}

#ifndef WINNT_NATIVE
/* Since ed.defns.h  is generated from ed.defns.c, these empty 
   functions will keep the F_NUM_FNS consistent
 */
CCRETVAL
e_copy_to_clipboard(Char c)
{
    USE(c);
    return CC_ERROR;
}

CCRETVAL
e_paste_from_clipboard(Char c)
{
    USE(c);
    return (CC_ERROR);
}

CCRETVAL
e_dosify_next(Char c)
{
    USE(c);
    return (CC_ERROR);
}
CCRETVAL
e_dosify_prev(Char c)
{
    USE(c);
    return (CC_ERROR);
}
CCRETVAL
e_page_up(Char c)
{
    USE(c);
    return (CC_ERROR);
}
CCRETVAL
e_page_down(Char c)
{
    USE(c);
    return (CC_ERROR);
}
#endif /* !WINNT_NATIVE */

#ifdef notdef
void
MoveCursor(int n)		/* move cursor + right - left char */
{
    Cursor = Cursor + n;
    if (Cursor < InputBuf)
	Cursor = InputBuf;
    if (Cursor > LastChar)
	Cursor = LastChar;
    return;
}

Char *
GetCursor(void)
{
    return(Cursor);
}

int
PutCursor(Char *p)
{
    if (p < InputBuf || p > LastChar)
	return 1;		/* Error */
    Cursor = p;
    return 0;
}
#endif
