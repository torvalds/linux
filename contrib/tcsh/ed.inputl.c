/* $Header: /p/tcsh/cvsroot/tcsh/ed.inputl.c,v 3.73 2012/10/19 15:23:32 christos Exp $ */
/*
 * ed.inputl.c: Input line handling.
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

RCSID("$tcsh: ed.inputl.c,v 3.73 2012/10/19 15:23:32 christos Exp $")

#include "ed.h"
#include "ed.defns.h"		/* for the function names */
#include "tw.h"			/* for twenex stuff */

#define OKCMD INT_MAX

/* ed.inputl -- routines to get a single line from the input. */

extern int MapsAreInited;

/* mismatched first character */
static Char mismatch[] = { '\\', '-', '%', '\0' };
/* don't Strchr() for '\0', obey current history character settings */
#define MISMATCH(c) ((c) == '\0' || (c) == HIST || (c) == HISTSUB || \
			Strchr(mismatch, (c)))

static	int	Repair		(void);
static	int	GetNextCommand	(KEYCMD *, Char *);
static	int	SpellLine	(int);
static	int	CompleteLine	(void);
static	void	RunCommand	(Char *);
static  void 	doeval1		(Char **);

static int rotate = 0;


static int
Repair(void)
{
    if (NeedsRedraw) {
	ClearLines();
	ClearDisp();
	NeedsRedraw = 0;
    }
    Refresh();
    Argument = 1;
    DoingArg = 0;
    curchoice = -1;
    return (int) (LastChar - InputBuf);
}

/* CCRETVAL */
int
Inputl(void)
{
    CCRETVAL retval;
    KEYCMD  cmdnum = 0;
    unsigned char tch;		/* the place where read() goes */
    Char    ch;
    int     num;		/* how many chars we have read at NL */
    int	    expnum;
    struct varent *crct = inheredoc ? NULL : adrof(STRcorrect);
    struct varent *autol = adrof(STRautolist);
    struct varent *matchbeep = adrof(STRmatchbeep);
    struct varent *imode = adrof(STRinputmode);
    Char   *SaveChar, *CorrChar;
    int     matchval;		/* from tenematch() */
    int     nr_history_exp;     /* number of (attempted) history expansions */
    COMMAND fn;
    int curlen = 0;
    int newlen;
    int idx;
    Char *autoexpand;

    if (!MapsAreInited)		/* double extra just in case */
	ed_InitMaps();

    ClearDisp();		/* reset the display stuff */
    ResetInLine(0);		/* reset the input pointers */
    if (GettingInput)
	MacroLvl = -1;		/* editor was interrupted during input */

    if (imode && imode->vec != NULL) {
	if (!Strcmp(*(imode->vec), STRinsert))
	    inputmode = MODE_INSERT;
	else if (!Strcmp(*(imode->vec), STRoverwrite))
	    inputmode = MODE_REPLACE;
    }

#if defined(FIONREAD) && !defined(OREO)
    if (!Tty_raw_mode && MacroLvl < 0) {
# ifdef SUNOS4
	long chrs = 0;
# else /* !SUNOS4 */
	/* 
	 * *Everyone* else has an int, but SunOS wants long!
	 * This breaks where int != long (alpha)
	 */
	int chrs = 0;
# endif /* SUNOS4 */

	(void) ioctl(SHIN, FIONREAD, (ioctl_t) & chrs);
	if (chrs == 0) {
	    if (Rawmode() < 0)
		return 0;
	}
    }
#endif /* FIONREAD && !OREO */

    GettingInput = 1;
    NeedsRedraw = 0;
    tellwhat = 0;

    if (RestoreSaved) {
	copyn(InputBuf, SavedBuf.s, INBUFSIZE);/*FIXBUF*/
	LastChar = InputBuf + LastSaved;
	Cursor = InputBuf + CursSaved;
	Hist_num = HistSaved;
	HistSaved = 0;
	RestoreSaved = 0;
    }
    if (HistSaved) {
	Hist_num = HistSaved;
	GetHistLine();
	HistSaved = 0;
    }
    if (Expand) {
	(void) e_up_hist(0);
	Expand = 0;
    }
    Refresh();			/* print the prompt */

    for (num = OKCMD; num == OKCMD;) {	/* while still editing this line */
#ifdef DEBUG_EDIT
	if (Cursor > LastChar)
	    xprintf("Cursor > LastChar\r\n");
	if (Cursor < InputBuf)
	    xprintf("Cursor < InputBuf\r\n");
	if (Cursor > InputLim)
	    xprintf("Cursor > InputLim\r\n");
	if (LastChar > InputLim)
	    xprintf("LastChar > InputLim\r\n");
	if (InputLim != &InputBuf[INBUFSIZE - 2])/*FIXBUF*/
	    xprintf("InputLim != &InputBuf[INBUFSIZE-2]\r\n");
	if ((!DoingArg) && (Argument != 1))
	    xprintf("(!DoingArg) && (Argument != 1)\r\n");
	if (CcKeyMap[0] == 0)
	    xprintf("CcKeyMap[0] == 0 (maybe not inited)\r\n");
#endif

	/* if EOF or error */
	if ((num = GetNextCommand(&cmdnum, &ch)) != OKCMD) {
	    break;
	}

	if (cmdnum >= NumFuns) {/* BUG CHECK command */
#ifdef DEBUG_EDIT
	    xprintf(CGETS(6, 1, "ERROR: illegal command from key 0%o\r\n"), ch);
#endif
	    continue;		/* try again */
	}

	/* now do the real command */
	retval = (*CcFuncTbl[cmdnum]) (ch);

	/* save the last command here */
	LastCmd = cmdnum;

	/* make sure fn is initialized */
	fn = (retval == CC_COMPLETE_ALL) ? LIST_ALL : LIST;

	/* use any return value */
	switch (retval) {

	case CC_REFRESH:
	    Refresh();
	    /*FALLTHROUGH*/
	case CC_NORM:		/* normal char */
	    Argument = 1;
	    DoingArg = 0;
	    /*FALLTHROUGH*/
	case CC_ARGHACK:	/* Suggested by Rich Salz */
	    /* <rsalz@pineapple.bbn.com> */
	    curchoice = -1;
	    curlen = (int) (LastChar - InputBuf);
	    break;		/* keep going... */

	case CC_EOF:		/* end of file typed */
	    curchoice = -1;
	    curlen = (int) (LastChar - InputBuf);
	    num = 0;
	    break;

	case CC_WHICH:		/* tell what this command does */
	    tellwhat = 1;
	    *LastChar++ = '\n';	/* for the benifit of CSH */
	    num = (int) (LastChar - InputBuf);	/* number characters read */
	    break;

	case CC_NEWLINE:	/* normal end of line */
	    curlen = 0;
	    curchoice = -1;
	    matchval = 1;
	    if (crct && crct->vec != NULL && (!Strcmp(*(crct->vec), STRcmd) ||
			 !Strcmp(*(crct->vec), STRall))) {
		Char *Origin;

                PastBottom();
		Origin = Strsave(InputBuf);
		cleanup_push(Origin, xfree);
		SaveChar = LastChar;
		if (SpellLine(!Strcmp(*(crct->vec), STRcmd)) == 1) {
		    Char *Change;

                    PastBottom();
		    Change = Strsave(InputBuf);
		    cleanup_push(Change, xfree);
		    *Strchr(Change, '\n') = '\0';
		    CorrChar = LastChar;	/* Save the corrected end */
		    LastChar = InputBuf;	/* Null the current line */
		    SoundBeep();
		    printprompt(2, short2str(Change));
		    cleanup_until(Change);
		    Refresh();
		    if (xread(SHIN, &tch, 1) < 0) {
#ifdef convex
		        /*
			 * need to print error message in case file
			 * is migrated
			 */
                        if (errno)
                            stderror(ERR_SYSTEM, progname, strerror(errno));
#else
			cleanup_until(Origin);
			break;
#endif
		    }
		    ch = tch;
		    if (ch == 'y' || ch == ' ') {
			LastChar = CorrChar;	/* Restore the corrected end */
			xprintf("%s", CGETS(6, 2, "yes\n"));
		    }
		    else {
			Strcpy(InputBuf, Origin);
			LastChar = SaveChar;
			if (ch == 'e') {
			    xprintf("%s", CGETS(6, 3, "edit\n"));
			    *LastChar-- = '\0';
			    Cursor = LastChar;
			    printprompt(3, NULL);
			    ClearLines();
			    ClearDisp();
			    Refresh();
			    cleanup_until(Origin);
			    break;
			}
			else if (ch == 'a') {
			    xprintf("%s", CGETS(6, 4, "abort\n"));
		            LastChar = InputBuf;   /* Null the current line */
			    Cursor = LastChar;
			    printprompt(0, NULL);
			    Refresh();
			    cleanup_until(Origin);
			    break;
			}
			xprintf("%s", CGETS(6, 5, "no\n"));
		    }
		    flush();
		}
		cleanup_until(Origin);
	    } else if (crct && crct->vec != NULL &&
		!Strcmp(*(crct->vec), STRcomplete)) {
                if (LastChar > InputBuf && LastChar[-1] == '\n') {
                    LastChar[-1] = '\0';
                    LastChar--;
                    Cursor = LastChar;
                }
                match_unique_match = 1;  /* match unique matches */
		matchval = CompleteLine();
                match_unique_match = 0;
        	curlen = (int) (LastChar - InputBuf);
		if (matchval != 1) {
                    PastBottom();
		}
		if (matchval == 0) {
		    xprintf("%s", CGETS(6, 6, "No matching command\n"));
		} else if (matchval == 2) {
		    xprintf("%s", CGETS(6, 7, "Ambiguous command\n"));
		}
	        if (NeedsRedraw) {
		    ClearLines();
		    ClearDisp();
		    NeedsRedraw = 0;
	        }
	        Refresh();
	        Argument = 1;
	        DoingArg = 0;
		if (matchval == 1) {
                    PastBottom();
                    *LastChar++ = '\n';
                    *LastChar = '\0';
		}
        	curlen = (int) (LastChar - InputBuf);
            }
	    else
		PastBottom();

	    if (matchval == 1) {
	        tellwhat = 0;	/* just in case */
	        Hist_num = 0;	/* for the history commands */
		/* return the number of chars read */
	        num = (int) (LastChar - InputBuf);
	        /*
	         * For continuation lines, we set the prompt to prompt 2
	         */
	        printprompt(1, NULL);
	    }
	    break;

	case CC_CORRECT:
	    if (tenematch(InputBuf, Cursor - InputBuf, SPELL) < 0)
		SoundBeep();		/* Beep = No match/ambiguous */
	    curlen = Repair();
	    break;

	case CC_CORRECT_L:
	    if (SpellLine(FALSE) < 0)
		SoundBeep();		/* Beep = No match/ambiguous */
	    curlen = Repair();
	    break;


	case CC_COMPLETE:
	case CC_COMPLETE_ALL:
	case CC_COMPLETE_FWD:
	case CC_COMPLETE_BACK:
	    switch (retval) {
	    case CC_COMPLETE:
		fn = RECOGNIZE;
		curlen = (int) (LastChar - InputBuf);
		curchoice = -1;
		rotate = 0;
		break;
	    case CC_COMPLETE_ALL:
		fn = RECOGNIZE_ALL;
		curlen = (int) (LastChar - InputBuf);
		curchoice = -1;
		rotate = 0;
		break;
	    case CC_COMPLETE_FWD:
		fn = RECOGNIZE_SCROLL;
		curchoice++;
		rotate = 1;
		break;
	    case CC_COMPLETE_BACK:
		fn = RECOGNIZE_SCROLL;
		curchoice--;
		rotate = 1;
		break;
	    default:
		abort();
	    }
	    if (InputBuf[curlen] && rotate) {
		newlen = (int) (LastChar - InputBuf);
		for (idx = (int) (Cursor - InputBuf); 
		     idx <= newlen; idx++)
			InputBuf[idx - newlen + curlen] =
			InputBuf[idx];
		LastChar = InputBuf + curlen;
		Cursor = Cursor - newlen + curlen;
	    }
	    curlen = (int) (LastChar - InputBuf);


	    nr_history_exp = 0;
	    autoexpand = varval(STRautoexpand);
	    if (autoexpand != STRNULL)
		nr_history_exp += ExpandHistory();

	    /* try normal expansion only if no history references were found */
	    if (nr_history_exp == 0 ||
		Strcmp(autoexpand, STRonlyhistory) != 0) {
		/*
		 * Modified by Martin Boyer (gamin@ireq-robot.hydro.qc.ca):
		 * A separate variable now controls beeping after
		 * completion, independently of autolisting.
		 */
		expnum = (int) (Cursor - InputBuf);
		switch (matchval = tenematch(InputBuf, Cursor-InputBuf, fn)){
		case 1:
		    if (non_unique_match && matchbeep &&
			matchbeep->vec != NULL &&
			(Strcmp(*(matchbeep->vec), STRnotunique) == 0))
			SoundBeep();
		    break;
		case 0:
		    if (matchbeep && matchbeep->vec != NULL) {
			if (Strcmp(*(matchbeep->vec), STRnomatch) == 0 ||
			    Strcmp(*(matchbeep->vec), STRambiguous) == 0 ||
			    Strcmp(*(matchbeep->vec), STRnotunique) == 0)
			    SoundBeep();
		    }
		    else
			SoundBeep();
		    break;
		default:
		    if (matchval < 0) {	/* Error from tenematch */
			curchoice = -1;
			SoundBeep();
			break;
		    }
		    if (matchbeep && matchbeep->vec != NULL) {
			if ((Strcmp(*(matchbeep->vec), STRambiguous) == 0 ||
			     Strcmp(*(matchbeep->vec), STRnotunique) == 0))
			    SoundBeep();
		    }
		    else
			SoundBeep();
		    /*
		     * Addition by David C Lawrence <tale@pawl.rpi.edu>: If an 
		     * attempted completion is ambiguous, list the choices.  
		     * (PWP: this is the best feature addition to tcsh I have 
		     * seen in many months.)
		     */
		    if (autol && autol->vec != NULL && 
			(Strcmp(*(autol->vec), STRambiguous) != 0 || 
					 expnum == Cursor - InputBuf)) {
			if (adrof(STRhighlight) && MarkIsSet) {
			    /* clear highlighting before showing completions */
			    MarkIsSet = 0;
			    ClearLines();
			    ClearDisp();
			    Refresh();
			    MarkIsSet = 1;
			}
			PastBottom();
			fn = (retval == CC_COMPLETE_ALL) ? LIST_ALL : LIST;
			(void) tenematch(InputBuf, Cursor-InputBuf, fn);
		    }
		    break;
		}
	    }
	    if (NeedsRedraw) {
		PastBottom();
		ClearLines();
		ClearDisp();
		NeedsRedraw = 0;
	    }
	    Refresh();
	    Argument = 1;
	    DoingArg = 0;
	    break;

	case CC_LIST_CHOICES:
	case CC_LIST_ALL:
	    if (InputBuf[curlen] && rotate) {
		newlen = (int) (LastChar - InputBuf);
		for (idx = (int) (Cursor - InputBuf); 
		     idx <= newlen; idx++)
			InputBuf[idx - newlen + curlen] =
			InputBuf[idx];
		LastChar = InputBuf + curlen;
		Cursor = Cursor - newlen + curlen;
	    }
	    curlen = (int) (LastChar - InputBuf);
	    if (curchoice >= 0)
		curchoice--;

	    fn = (retval == CC_LIST_ALL) ? LIST_ALL : LIST;
	    /* should catch ^C here... */
	    if (tenematch(InputBuf, Cursor - InputBuf, fn) < 0)
		SoundBeep();
	    Refresh();
	    Argument = 1;
	    DoingArg = 0;
	    break;


	case CC_LIST_GLOB:
	    if (tenematch(InputBuf, Cursor - InputBuf, GLOB) < 0)
		SoundBeep();
	    curlen = Repair();
	    break;

	case CC_EXPAND_GLOB:
	    if (tenematch(InputBuf, Cursor - InputBuf, GLOB_EXPAND) <= 0)
		SoundBeep();		/* Beep = No match */
	    curlen = Repair();
	    break;

	case CC_NORMALIZE_PATH:
	    if (tenematch(InputBuf, Cursor - InputBuf, PATH_NORMALIZE) <= 0)
		SoundBeep();		/* Beep = No match */
	    curlen = Repair();
	    break;

	case CC_EXPAND_VARS:
	    if (tenematch(InputBuf, Cursor - InputBuf, VARS_EXPAND) <= 0)
		SoundBeep();		/* Beep = No match */
	    curlen = Repair();
	    break;

	case CC_NORMALIZE_COMMAND:
	    if (tenematch(InputBuf, Cursor - InputBuf, COMMAND_NORMALIZE) <= 0)
		SoundBeep();		/* Beep = No match */
	    curlen = Repair();
	    break;

	case CC_HELPME:
	    xputchar('\n');
	    /* should catch ^C here... */
	    (void) tenematch(InputBuf, LastChar - InputBuf, PRINT_HELP);
	    Refresh();
	    Argument = 1;
	    DoingArg = 0;
	    curchoice = -1;
	    curlen = (int) (LastChar - InputBuf);
	    break;

	case CC_FATAL:		/* fatal error, reset to known state */
#ifdef DEBUG_EDIT
	    xprintf(CGETS(7, 8, "*** editor fatal ERROR ***\r\n\n"));
#endif				/* DEBUG_EDIT */
	    /* put (real) cursor in a known place */
	    ClearDisp();	/* reset the display stuff */
	    ResetInLine(1);	/* reset the input pointers */
	    Refresh();		/* print the prompt again */
	    Argument = 1;
	    DoingArg = 0;
	    curchoice = -1;
	    curlen = (int) (LastChar - InputBuf);
	    break;

	case CC_ERROR:
	default:		/* functions we don't know about */
	    if (adrof(STRhighlight)) {
		ClearLines();
		ClearDisp();
		Refresh();
	    }
	    DoingArg = 0;
	    Argument = 1;
	    SoundBeep();
	    flush();
	    curchoice = -1;
	    curlen = (int) (LastChar - InputBuf);
	    break;
	}
    }
    (void) Cookedmode();	/* make sure the tty is set up correctly */
    GettingInput = 0;
    flush();			/* flush any buffered output */
    return num;
}

void
PushMacro(Char *str)
{
    if (str != NULL && MacroLvl + 1 < MAXMACROLEVELS) {
	MacroLvl++;
	KeyMacro[MacroLvl] = str;
    }
    else {
	SoundBeep();
	flush();
    }
}

struct eval1_state
{
    Char **evalvec, *evalp;
};

static void
eval1_cleanup(void *xstate)
{
    struct eval1_state *state;

    state = xstate;
    evalvec = state->evalvec;
    evalp = state->evalp;
    doneinp = 0;
}

/*
 * Like eval, only using the current file descriptors
 */
static void
doeval1(Char **v)
{
    struct eval1_state state;
    Char  **gv;
    int gflag;

    gflag = tglob(v);
    if (gflag) {
	gv = v = globall(v, gflag);
	if (v == 0)
	    stderror(ERR_NOMATCH);
	v = copyblk(v);
    }
    else {
	gv = NULL;
	v = copyblk(v);
	trim(v);
    }
    if (gv)
	cleanup_push(gv, blk_cleanup);

    state.evalvec = evalvec;
    state.evalp = evalp;
    evalvec = v;
    evalp = 0;
    cleanup_push(&state, eval1_cleanup);
    process(0);
    cleanup_until(&state);
    if (gv)
	cleanup_until(gv);
}

static void
RunCommand(Char *str)
{
    Char *cmd[2];

    xputchar('\n');	/* Start on a clean line */

    cmd[0] = str;
    cmd[1] = NULL;

    (void) Cookedmode();
    GettingInput = 0;

    doeval1(cmd);

    (void) Rawmode();
    GettingInput = 1;

    ClearLines();
    ClearDisp();
    NeedsRedraw = 0;
    Refresh();
}

static int
GetNextCommand(KEYCMD *cmdnum, Char *ch)
{
    KEYCMD  cmd = 0;
    int     num;

    while (cmd == 0 || cmd == F_XKEY) {
	if ((num = GetNextChar(ch)) != 1) {	/* if EOF or error */
	    return num;
	}
#ifdef	KANJI
	if (
#ifdef DSPMBYTE
	     _enable_mbdisp &&
#else
	     MB_LEN_MAX == 1 &&
#endif
	     !adrof(STRnokanji) && (*ch & META)) {
	    MetaNext = 0;
	    cmd = F_INSERT;
	    break;
	}
	else
#endif /* KANJI */
	if (MetaNext) {
	    MetaNext = 0;
	    *ch |= META;
	}
	/* XXX: This needs to be fixed so that we don't just truncate
	 * the character, we unquote it.
	 */
	if (*ch < NT_NUM_KEYS)
	    cmd = CurrentKeyMap[*ch];
	else
#ifdef WINNT_NATIVE
	    cmd = CurrentKeyMap[(unsigned char) *ch];
#else
	    cmd = F_INSERT;
#endif
	if (cmd == F_XKEY) {
	    XmapVal val;
	    CStr cstr;
	    cstr.buf = ch;
	    cstr.len = 1;
	    switch (GetXkey(&cstr, &val)) {
	    case XK_CMD:
		cmd = val.cmd;
		break;
	    case XK_STR:
		PushMacro(val.str.buf);
		break;
	    case XK_EXE:
		RunCommand(val.str.buf);
		break;
	    default:
		abort();
		break;
	    }
	}
	if (!AltKeyMap) 
	    CurrentKeyMap = CcKeyMap;
    }
    *cmdnum = cmd;
    return OKCMD;
}

static Char ungetchar;
static int haveungetchar;

void
UngetNextChar(Char cp)
{
    ungetchar = cp;
    haveungetchar = 1;
}

int
GetNextChar(Char *cp)
{
    int num_read;
    int     tried = 0;
    char cbuf[MB_LEN_MAX];
    size_t cbp;

    if (haveungetchar) {
	haveungetchar = 0;
	*cp = ungetchar;
	return 1;
    }
    for (;;) {
	if (MacroLvl < 0) {
	    if (!Load_input_line())
		break;
	}
	if (*KeyMacro[MacroLvl] == 0) {
	    MacroLvl--;
	    continue;
	}
	*cp = *KeyMacro[MacroLvl]++ & CHAR;
	if (*KeyMacro[MacroLvl] == 0) {	/* Needed for QuoteMode On */
	    MacroLvl--;
	}
	return (1);
    }

    if (Rawmode() < 0)		/* make sure the tty is set up correctly */
	return 0;		/* oops: SHIN was closed */

#ifdef WINNT_NATIVE
    __nt_want_vcode = 1;
#endif /* WINNT_NATIVE */
#ifdef SIG_WINDOW
    if (windowchg)
	(void) check_window_size(0);	/* for window systems */
#endif /* SIG_WINDOW */
    cbp = 0;
    for (;;) {
	while ((num_read = xread(SHIN, cbuf + cbp, 1)) == -1) {
	    if (!tried && fixio(SHIN, errno) != -1)
		tried = 1;
	    else {
# ifdef convex
		/* need to print error message in case the file is migrated */
		stderror(ERR_SYSTEM, progname, strerror(errno));
# endif  /* convex */
# ifdef WINNT_NATIVE
		__nt_want_vcode = 0;
# endif /* WINNT_NATIVE */
		*cp = '\0'; /* Loses possible partial character */
		return -1;
	    }
	}
	cbp++;
	if (normal_mbtowc(cp, cbuf, cbp) == -1) {
	    reset_mbtowc();
	    if (cbp < MB_CUR_MAX)
		continue; /* Maybe a partial character */
	    /* And drop the following bytes, if any */
	    *cp = (unsigned char)*cbuf | INVALID_BYTE;
	}
	break;
    }
#ifdef WINNT_NATIVE
    /* This is the part that doesn't work with WIDE_STRINGS */
    if (__nt_want_vcode == 2)
	*cp = __nt_vcode;
    __nt_want_vcode = 0;
#endif /* WINNT_NATIVE */
    return num_read;
}

/*
 * SpellLine - do spelling correction on the entire command line
 * (which may have trailing newline).
 * If cmdonly is set, only check spelling of command words.
 * Return value:
 * -1: Something was incorrectible, and nothing was corrected
 *  0: Everything was correct
 *  1: Something was corrected
 */
static int
SpellLine(int cmdonly)
{
    int     endflag, matchval;
    Char   *argptr, *OldCursor, *OldLastChar;

    OldLastChar = LastChar;
    OldCursor = Cursor;
    argptr = InputBuf;
    endflag = 1;
    matchval = 0;
    do {
	while (ismetahash(*argptr) || iscmdmeta(*argptr))
	    argptr++;
	for (Cursor = argptr;
	     *Cursor != '\0' && ((Cursor != argptr && Cursor[-1] == '\\') ||
				 (!ismetahash(*Cursor) && !iscmdmeta(*Cursor)));
	     Cursor++)
	     continue;
	if (*Cursor == '\0') {
	    Cursor = LastChar;
	    if (LastChar[-1] == '\n')
		Cursor--;
	    endflag = 0;
	}
	if (!MISMATCH(*argptr) &&
	    (!cmdonly || starting_a_command(argptr, InputBuf))) {
#ifdef WINNT_NATIVE
	    /*
	     * This hack avoids correcting drive letter changes
	     */
	    if((Cursor - InputBuf) != 2 || (char)InputBuf[1] != ':')
#endif /* WINNT_NATIVE */
	    {
#ifdef HASH_SPELL_CHECK
		Char save;
		size_t len = Cursor - InputBuf;

		save = InputBuf[len];
		InputBuf[len] = '\0';
		if (find_cmd(InputBuf, 0) != 0) {
		    InputBuf[len] = save;
		    argptr = Cursor;
		    continue;
		}
		InputBuf[len] = save;
#endif /* HASH_SPELL_CHECK */
		switch (tenematch(InputBuf, Cursor - InputBuf, SPELL)) {
		case 1:		/* corrected */
		    matchval = 1;
		    break;
		case -1:		/* couldn't be corrected */
		    if (!matchval)
			matchval = -1;
		    break;
		default:		/* was correct */
		    break;
		}
	    }
	    if (LastChar != OldLastChar) {
		if (argptr < OldCursor)
		    OldCursor += (LastChar - OldLastChar);
		OldLastChar = LastChar;
	    }
	}
	argptr = Cursor;
    } while (endflag);
    Cursor = OldCursor;
    return matchval;
}

/*
 * CompleteLine - do command completion on the entire command line
 * (which may have trailing newline).
 * Return value:
 *  0: No command matched or failure
 *  1: One command matched
 *  2: Several commands matched
 */
static int
CompleteLine(void)
{
    int     endflag, tmatch;
    Char   *argptr, *OldCursor, *OldLastChar;

    OldLastChar = LastChar;
    OldCursor = Cursor;
    argptr = InputBuf;
    endflag = 1;
    do {
	while (ismetahash(*argptr) || iscmdmeta(*argptr))
	    argptr++;
	for (Cursor = argptr;
	     *Cursor != '\0' && ((Cursor != argptr && Cursor[-1] == '\\') ||
				 (!ismetahash(*Cursor) && !iscmdmeta(*Cursor)));
	     Cursor++)
	     continue;
	if (*Cursor == '\0') {
	    Cursor = LastChar;
	    if (LastChar[-1] == '\n')
		Cursor--;
	    endflag = 0;
	}
	if (!MISMATCH(*argptr) && starting_a_command(argptr, InputBuf)) {
	    tmatch = tenematch(InputBuf, Cursor - InputBuf, RECOGNIZE);
	    if (tmatch <= 0) {
                return 0;
            } else if (tmatch > 1) {
                return 2;
	    }
	    if (LastChar != OldLastChar) {
		if (argptr < OldCursor)
		    OldCursor += (LastChar - OldLastChar);
		OldLastChar = LastChar;
	    }
	}
	argptr = Cursor;
    } while (endflag);
    Cursor = OldCursor;
    return 1;
}

