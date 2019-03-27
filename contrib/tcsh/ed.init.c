/* $Header: /p/tcsh/cvsroot/tcsh/ed.init.c,v 3.60 2006/08/24 20:56:31 christos Exp $ */
/*
 * ed.init.c: Editor initializations
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
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTS_ION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "sh.h"

RCSID("$tcsh: ed.init.c,v 3.60 2006/08/24 20:56:31 christos Exp $")

#include "ed.h"
#include "tc.h"
#include "ed.defns.h"

/* ed.init.c -- init routines for the line editor */
/* #define DEBUG_TTY */

int     Tty_raw_mode = 0;	/* Last tty change was to raw mode */
int     MacroLvl = -1;		/* pointer to current macro nesting level; */
				/* (-1 == none) */
static int Tty_quote_mode = 0;	/* Last tty change was to quote mode */
static unsigned char vdisable;	/* The value of _POSIX_VDISABLE from 
				 * pathconf(2) */

int     Tty_eight_bit = -1;	/* does the tty handle eight bits */

extern int GotTermCaps;

static ttydata_t extty, edtty, tstty;
#define qutty tstty

#define SHTTY (insource ? OLDSTD : SHIN)

#define uc unsigned char
static unsigned char ttychars[NN_IO][C_NCC] = {
    {
	(uc)CINTR,	(uc)CQUIT, 	 (uc)CERASE, 	   (uc)CKILL,	
	(uc)CEOF, 	(uc)CEOL, 	 (uc)CEOL2, 	   (uc)CSWTCH, 
	(uc)CDSWTCH,	(uc)CERASE2,	 (uc)CSTART, 	   (uc)CSTOP,
	(uc)CWERASE, 	(uc)CSUSP, 	 (uc)CDSUSP, 	   (uc)CREPRINT,
	(uc)CDISCARD, 	(uc)CLNEXT,	 (uc)CSTATUS,	   (uc)CPAGE,
	(uc)CPGOFF,	(uc)CKILL2, 	 (uc)CBRK, 	   (uc)CMIN,
	(uc)CTIME
    },
    {
	CINTR, 		 CQUIT, 	  CERASE, 	   CKILL, 
	_POSIX_VDISABLE, _POSIX_VDISABLE, _POSIX_VDISABLE, _POSIX_VDISABLE, 
	_POSIX_VDISABLE, CERASE2,	  CSTART, 	   CSTOP, 	   
	_POSIX_VDISABLE, _POSIX_VDISABLE, _POSIX_VDISABLE, _POSIX_VDISABLE, 
	CDISCARD, 	 _POSIX_VDISABLE, _POSIX_VDISABLE, _POSIX_VDISABLE, 
	_POSIX_VDISABLE, _POSIX_VDISABLE, _POSIX_VDISABLE, 1,
	0
    },
    {	
	0,		 0,		  0,		   0,
	0,		 0,		  0,		   0,
	0,		 0,		  0,		   0,
	0,		 0,		  0,		   0,
	0,		 0,		  0,		   0,
	0,		 0,		  0,		   0,
	0
    }
};

#ifdef SIG_WINDOW
void
check_window_size(int force)
{
    int     lins, cols;

    /* don't want to confuse things here */
    pintr_disabled++;
    cleanup_push(&pintr_disabled, disabled_cleanup);
    /*
     * From: bret@shark.agps.lanl.gov (Bret Thaeler) Avoid sunview bug, where a
     * partially hidden window gets a SIG_WINDOW every time the text is
     * scrolled
     */
    if (GetSize(&lins, &cols) || force) {
	if (GettingInput) {
	    ClearLines();
	    ClearDisp();
	    MoveToLine(0);
	    MoveToChar(0);
	    ChangeSize(lins, cols);
	    Refresh();
	}
	else
	    ChangeSize(lins, cols);
    }
    windowchg = 0;
    cleanup_until(&pintr_disabled);	/* can change it again */
}

void
/*ARGSUSED*/
window_change(int snum)
{
    USE(snum);
    windowchg = 1;
}

#endif /* SIG_WINDOW */

void
ed_set_tty_eight_bit(void)
{
    if (tty_getty(SHTTY, &extty) == -1) {
#ifdef DEBUG_TTY
	xprintf("ed_set_tty_eight_bit: tty_getty: %s\n", strerror(errno));
#endif /* DEBUG_TTY */
	return;
    }
    Tty_eight_bit = tty_geteightbit(&extty);
}

			
int
ed_Setup(int rst)
{
    static int havesetup = 0;
    struct varent *imode;

    if (havesetup) 	/* if we have never been called */
	return(0);

#if defined(POSIX) && defined(_PC_VDISABLE) && !defined(BSD4_4) && \
    !defined(WINNT_NATIVE)
    { 
	long pcret;

	if ((pcret = fpathconf(SHTTY, _PC_VDISABLE)) == -1L)
	    vdisable = (unsigned char) _POSIX_VDISABLE;
	else 
	    vdisable = (unsigned char) pcret;
	if (vdisable != (unsigned char) _POSIX_VDISABLE && rst != 0)
	    for (rst = 0; rst < C_NCC; rst++) {
		if (ttychars[ED_IO][rst] == (unsigned char) _POSIX_VDISABLE)
		    ttychars[ED_IO][rst] = vdisable;
		if (ttychars[EX_IO][rst] == (unsigned char) _POSIX_VDISABLE)
		    ttychars[EX_IO][rst] = vdisable;
	    }
    }
#else /* ! POSIX || !_PC_VDISABLE || BSD4_4 || WINNT_NATIVE */
    vdisable = (unsigned char) _POSIX_VDISABLE;
#endif /* POSIX && _PC_VDISABLE && !BSD4_4 && !WINNT_NATIVE */
	
    if ((imode = adrof(STRinputmode)) != NULL && imode->vec != NULL) {
	if (!Strcmp(*(imode->vec), STRinsert))
	    inputmode = MODE_INSERT;
	else if (!Strcmp(*(imode->vec), STRoverwrite))
	    inputmode = MODE_REPLACE;
    }
    else
	inputmode = MODE_INSERT;
    ed_InitMaps();
    Hist_num = 0;
    Expand = 0;
    SetKillRing(getn(varval(STRkillring)));

#ifndef WINNT_NATIVE
    if (tty_getty(SHTTY, &extty) == -1) {
# ifdef DEBUG_TTY
	xprintf("ed_Setup: tty_getty: %s\n", strerror(errno));
# endif /* DEBUG_TTY */
	return(-1);
    }

    tstty = edtty = extty;

    T_Speed = tty_getspeed(&extty);
    T_Tabs = tty_gettabs(&extty);
    Tty_eight_bit = tty_geteightbit(&extty);

# if defined(POSIX) || defined(TERMIO)
    extty.d_t.c_iflag &= ~ttylist[EX_IO][M_INPUT].t_clrmask;
    extty.d_t.c_iflag |=  ttylist[EX_IO][M_INPUT].t_setmask;

    extty.d_t.c_oflag &= ~ttylist[EX_IO][M_OUTPUT].t_clrmask;
    extty.d_t.c_oflag |=  ttylist[EX_IO][M_OUTPUT].t_setmask;

    extty.d_t.c_cflag &= ~ttylist[EX_IO][M_CONTROL].t_clrmask;
    extty.d_t.c_cflag |=  ttylist[EX_IO][M_CONTROL].t_setmask;

    extty.d_t.c_lflag &= ~ttylist[EX_IO][M_LINED].t_clrmask;
    extty.d_t.c_lflag |=  ttylist[EX_IO][M_LINED].t_setmask;

#  if defined(IRIX3_3) && SYSVREL < 4
    extty.d_t.c_line = NTTYDISC;
#  endif /* IRIX3_3 && SYSVREL < 4 */

# else	/* GSTTY */		/* V7, Berkeley style tty */

    if (T_Tabs) {	/* order of &= and |= is important to XTABS */
	extty.d_t.sg_flags &= ~(ttylist[EX_IO][M_CONTROL].t_clrmask|XTABS);
	extty.d_t.sg_flags |=   ttylist[EX_IO][M_CONTROL].t_setmask;
    }
    else {
	extty.d_t.sg_flags &= ~ttylist[EX_IO][M_CONTROL].t_clrmask;
	extty.d_t.sg_flags |= (ttylist[EX_IO][M_CONTROL].t_setmask|XTABS);
    }

    extty.d_lb &= ~ttylist[EX_IO][M_LOCAL].t_clrmask;
    extty.d_lb |=  ttylist[EX_IO][M_LOCAL].t_setmask;

# endif /* GSTTY */
    /*
     * Reset the tty chars to reasonable defaults
     * If they are disabled, then enable them.
     */
    if (rst) {
	if (tty_cooked_mode(&tstty)) {
	    tty_getchar(&tstty, ttychars[TS_IO]);
	    /*
	     * Don't affect CMIN and CTIME for the editor mode
	     */
	    for (rst = 0; rst < C_NCC - 2; rst++) 
		if (ttychars[TS_IO][rst] != vdisable &&
		    ttychars[ED_IO][rst] != vdisable)
		    ttychars[ED_IO][rst] = ttychars[TS_IO][rst];
	    for (rst = 0; rst < C_NCC; rst++) 
		if (ttychars[TS_IO][rst] != vdisable &&
		    ttychars[EX_IO][rst] != vdisable)
		    ttychars[EX_IO][rst] = ttychars[TS_IO][rst];
	}
	tty_setchar(&extty, ttychars[EX_IO]);
	if (tty_setty(SHTTY, &extty) == -1) {
# ifdef DEBUG_TTY
	    xprintf("ed_Setup: tty_setty: %s\n", strerror(errno));
# endif /* DEBUG_TTY */
	    return(-1);
	}
    }
    else
	tty_setchar(&extty, ttychars[EX_IO]);

# ifdef SIG_WINDOW
    {
	sigset_t set;
	(void)signal(SIG_WINDOW, window_change);	/* for window systems */
	sigemptyset(&set);
	sigaddset(&set, SIG_WINDOW);
	(void)sigprocmask(SIG_UNBLOCK, &set, NULL);
    }
# endif
#else /* WINNT_NATIVE */
# ifdef DEBUG
    if (rst)
	xprintf("rst received in ed_Setup() %d\n", rst);
# endif
#endif /* WINNT_NATIVE */
    havesetup = 1;
    return(0);
}

void
ed_Init(void)
{
    ResetInLine(1);		/* reset the input pointers */
    GettingInput = 0;		/* just in case */
#ifdef notdef
    /* XXX This code was here before the kill ring:
    LastKill = KillBuf;		/ * no kill buffer * /
       If there was any reason for that other than to make sure LastKill
       was initialized, the code below should go in here instead - but
       it doesn't seem reasonable to lose the entire kill ring (which is
       "self-initializing") just because you set $term or whatever, so
       presumably this whole '#ifdef notdef' should just be taken out.  */

    {				/* no kill ring - why? */
	int i;
	for (i = 0; i < KillRingMax; i++) {
	    xfree(KillRing[i].buf);
	    KillRing[i].buf = NULL;
	    KillRing[i].len = 0;
	}
	YankPos = KillPos = 0;
	KillRingLen = 0;
    }
#endif

#ifdef DEBUG_EDIT
    CheckMaps();		/* do a little error checking on key maps */
#endif 

    if (ed_Setup(0) == -1)
	return;

    /*
     * if we have been called before but GotTermCaps isn't set, our TERM has
     * changed, so get new termcaps and try again
     */

    if (!GotTermCaps)
	GetTermCaps();		/* does the obvious, but gets term type each
				 * time */

#ifndef WINNT_NATIVE
# if defined(TERMIO) || defined(POSIX)
    edtty.d_t.c_iflag &= ~ttylist[ED_IO][M_INPUT].t_clrmask;
    edtty.d_t.c_iflag |=  ttylist[ED_IO][M_INPUT].t_setmask;

    edtty.d_t.c_oflag &= ~ttylist[ED_IO][M_OUTPUT].t_clrmask;
    edtty.d_t.c_oflag |=  ttylist[ED_IO][M_OUTPUT].t_setmask;

    edtty.d_t.c_cflag &= ~ttylist[ED_IO][M_CONTROL].t_clrmask;
    edtty.d_t.c_cflag |=  ttylist[ED_IO][M_CONTROL].t_setmask;

    edtty.d_t.c_lflag &= ~ttylist[ED_IO][M_LINED].t_clrmask;
    edtty.d_t.c_lflag |=  ttylist[ED_IO][M_LINED].t_setmask;


#  if defined(IRIX3_3) && SYSVREL < 4
    edtty.d_t.c_line = NTTYDISC;
#  endif /* IRIX3_3 && SYSVREL < 4 */

# else /* GSTTY */

    if (T_Tabs) {	/* order of &= and |= is important to XTABS */
	edtty.d_t.sg_flags &= ~(ttylist[ED_IO][M_CONTROL].t_clrmask | XTABS);
	edtty.d_t.sg_flags |=   ttylist[ED_IO][M_CONTROL].t_setmask;
    }
    else {
	edtty.d_t.sg_flags &= ~ttylist[ED_IO][M_CONTROL].t_clrmask;
	edtty.d_t.sg_flags |= (ttylist[ED_IO][M_CONTROL].t_setmask | XTABS);
    }

    edtty.d_lb &= ~ttylist[ED_IO][M_LOCAL].t_clrmask;
    edtty.d_lb |=  ttylist[ED_IO][M_LOCAL].t_setmask;
# endif /* POSIX || TERMIO */

    tty_setchar(&edtty, ttychars[ED_IO]);
#endif /* WINNT_NATIVE */
}

/* 
 * Check and re-init the line. set the terminal into 1 char at a time mode.
 */
int
Rawmode(void)
{
    if (Tty_raw_mode)
	return (0);

#ifdef WINNT_NATIVE
    do_nt_raw_mode();
#else /* !WINNT_NATIVE */
# ifdef _IBMR2
    tty_setdisc(SHTTY, ED_IO);
# endif /* _IBMR2 */

    if (tty_getty(SHTTY, &tstty) == -1) {
# ifdef DEBUG_TTY
	xprintf("Rawmode: tty_getty: %s\n", strerror(errno));
# endif /* DEBUG_TTY */
	return(-1);
    }

    /*
     * We always keep up with the eight bit setting and the speed of the
     * tty. But only we only believe changes that are made to cooked mode!
     */
# if defined(POSIX) || defined(TERMIO)
    Tty_eight_bit = tty_geteightbit(&tstty);
    T_Speed = tty_getspeed(&tstty);

#  ifdef POSIX
    /*
     * Fix from: Steven (Steve) B. Green <xrsbg@charney.gsfc.nasa.gov>
     * Speed was not being set up correctly under POSIX.
     */
    if (tty_getspeed(&extty) != T_Speed || tty_getspeed(&edtty) != T_Speed) {
	(void) cfsetispeed(&extty.d_t, T_Speed);
	(void) cfsetospeed(&extty.d_t, T_Speed);
	(void) cfsetispeed(&edtty.d_t, T_Speed);
	(void) cfsetospeed(&edtty.d_t, T_Speed);
    }
#  endif /* POSIX */
# else /* GSTTY */

    T_Speed = tty_getspeed(&tstty);
    Tty_eight_bit = tty_geteightbit(&tstty);

    if (extty.d_t.sg_ispeed != tstty.d_t.sg_ispeed) {
	extty.d_t.sg_ispeed = tstty.d_t.sg_ispeed;
	edtty.d_t.sg_ispeed = tstty.d_t.sg_ispeed;
    }

    if (extty.d_t.sg_ospeed != tstty.d_t.sg_ospeed) {
	extty.d_t.sg_ospeed = tstty.d_t.sg_ospeed;
	edtty.d_t.sg_ospeed = tstty.d_t.sg_ospeed;
    }
# endif /* POSIX || TERMIO */

    if (tty_cooked_mode(&tstty)) {
	/*
	 * re-test for some things here (like maybe the user typed 
	 * "stty -tabs"
	 */
	if (tty_gettabs(&tstty) == 0)
	    T_Tabs = 0;
	else 
	    T_Tabs = CanWeTab();

# if defined(POSIX) || defined(TERMIO)
	extty.d_t.c_cflag  = tstty.d_t.c_cflag;
	extty.d_t.c_cflag &= ~ttylist[EX_IO][M_CONTROL].t_clrmask;
	extty.d_t.c_cflag |=  ttylist[EX_IO][M_CONTROL].t_setmask;

	edtty.d_t.c_cflag  = tstty.d_t.c_cflag;
	edtty.d_t.c_cflag &= ~ttylist[ED_IO][M_CONTROL].t_clrmask;
	edtty.d_t.c_cflag |=  ttylist[ED_IO][M_CONTROL].t_setmask;

	extty.d_t.c_lflag = tstty.d_t.c_lflag;
	extty.d_t.c_lflag &= ~ttylist[EX_IO][M_LINED].t_clrmask;
	extty.d_t.c_lflag |=  ttylist[EX_IO][M_LINED].t_setmask;

	edtty.d_t.c_lflag = tstty.d_t.c_lflag;
	edtty.d_t.c_lflag &= ~ttylist[ED_IO][M_LINED].t_clrmask;
	edtty.d_t.c_lflag |=  ttylist[ED_IO][M_LINED].t_setmask;

	extty.d_t.c_iflag = tstty.d_t.c_iflag;
	extty.d_t.c_iflag &= ~ttylist[EX_IO][M_INPUT].t_clrmask;
	extty.d_t.c_iflag |=  ttylist[EX_IO][M_INPUT].t_setmask;

	edtty.d_t.c_iflag = tstty.d_t.c_iflag;
	edtty.d_t.c_iflag &= ~ttylist[ED_IO][M_INPUT].t_clrmask;
	edtty.d_t.c_iflag |=  ttylist[ED_IO][M_INPUT].t_setmask;

	extty.d_t.c_oflag = tstty.d_t.c_oflag;
	extty.d_t.c_oflag &= ~ttylist[EX_IO][M_OUTPUT].t_clrmask;
	extty.d_t.c_oflag |=  ttylist[EX_IO][M_OUTPUT].t_setmask;

	edtty.d_t.c_oflag = tstty.d_t.c_oflag;
	edtty.d_t.c_oflag &= ~ttylist[ED_IO][M_OUTPUT].t_clrmask;
	edtty.d_t.c_oflag |=  ttylist[ED_IO][M_OUTPUT].t_setmask;

# else /* GSTTY */

	extty.d_t.sg_flags = tstty.d_t.sg_flags;

	extty.d_t.sg_flags &= ~ttylist[EX_IO][M_CONTROL].t_clrmask;
	extty.d_t.sg_flags |=  ttylist[EX_IO][M_CONTROL].t_setmask;

	if (T_Tabs)		/* order of &= and |= is important to XTABS */
	    extty.d_t.sg_flags &= ~XTABS;
	else 
	    extty.d_t.sg_flags |= XTABS;

	extty.d_lb = tstty.d_lb;
	extty.d_lb &= ~ttylist[EX_IO][M_LOCAL].t_clrmask;
	extty.d_lb |= ttylist[EX_IO][M_LOCAL].t_setmask;

	edtty.d_t.sg_flags = extty.d_t.sg_flags;
	if (T_Tabs) {	/* order of &= and |= is important to XTABS */
	    edtty.d_t.sg_flags &= 
		    ~(ttylist[ED_IO][M_CONTROL].t_clrmask|XTABS);
	    edtty.d_t.sg_flags |=   ttylist[ED_IO][M_CONTROL].t_setmask;
	}
	else {
	    edtty.d_t.sg_flags &= ~ttylist[ED_IO][M_CONTROL].t_clrmask;
	    edtty.d_t.sg_flags |= 
		    (ttylist[ED_IO][M_CONTROL].t_setmask|XTABS);
	}

	edtty.d_lb = tstty.d_lb;
	edtty.d_lb &= ~ttylist[ED_IO][M_LOCAL].t_clrmask;
	edtty.d_lb |= ttylist[ED_IO][M_LOCAL].t_setmask;

# endif /* TERMIO || POSIX */

	{
	    int i;

	    tty_getchar(&tstty, ttychars[TS_IO]);
	    /*
	     * Check if the user made any changes.
	     * If he did, then propagate the changes to the
	     * edit and execute data structures.
	     */
	    for (i = 0; i < C_NCC; i++)
		if (ttychars[TS_IO][i] != ttychars[EX_IO][i])
		    break;
		
	    if (i != C_NCC || didsetty) {
		didsetty = 0;
		/*
		 * Propagate changes only to the unprotected chars
		 * that have been modified just now.
		 */
		for (i = 0; i < C_NCC; i++) {
		    if (!((ttylist[ED_IO][M_CHAR].t_setmask & C_SH(i))) &&
			(ttychars[TS_IO][i] != ttychars[EX_IO][i]))
			ttychars[ED_IO][i] = ttychars[TS_IO][i];
		    if (ttylist[ED_IO][M_CHAR].t_clrmask & C_SH(i))
			ttychars[ED_IO][i] = vdisable;
		}
		tty_setchar(&edtty, ttychars[ED_IO]);

		for (i = 0; i < C_NCC; i++) {
		    if (!((ttylist[EX_IO][M_CHAR].t_setmask & C_SH(i))) &&
			(ttychars[TS_IO][i] != ttychars[EX_IO][i]))
			ttychars[EX_IO][i] = ttychars[TS_IO][i];
		    if (ttylist[EX_IO][M_CHAR].t_clrmask & C_SH(i))
			ttychars[EX_IO][i] = vdisable;
		}
		tty_setchar(&extty, ttychars[EX_IO]);
	    }

	}
    }
    if (tty_setty(SHTTY, &edtty) == -1) {
# ifdef DEBUG_TTY
	xprintf("Rawmode: tty_setty: %s\n", strerror(errno));
# endif /* DEBUG_TTY */
	return(-1);
    }
#endif /* WINNT_NATIVE */
    Tty_raw_mode = 1;
    flush();			/* flush any buffered output */
    return (0);
}

int
Cookedmode(void)
{				/* set tty in normal setup */
#ifdef WINNT_NATIVE
    do_nt_cooked_mode();
#else
    sigset_t set, oset;
    int res;

# ifdef _IBMR2
    tty_setdisc(SHTTY, EX_IO);
# endif /* _IBMR2 */

    if (!Tty_raw_mode)
	return (0);

    /* hold this for reseting tty */
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    (void)sigprocmask(SIG_BLOCK, &set, &oset);
    cleanup_push(&oset, sigprocmask_cleanup);
    res = tty_setty(SHTTY, &extty);
    cleanup_until(&oset);
    if (res == -1) {
# ifdef DEBUG_TTY
	xprintf("Cookedmode: tty_setty: %s\n", strerror(errno));
# endif /* DEBUG_TTY */
	return -1;
    }
#endif /* WINNT_NATIVE */

    Tty_raw_mode = 0;
    return (0);
}

void
ResetInLine(int macro)
{
    Cursor = InputBuf;		/* reset cursor */
    LastChar = InputBuf;
    InputLim = &InputBuf[INBUFSIZE - 2];/*FIXBUF*/
    Mark = InputBuf;
    MarkIsSet = 0;
    MetaNext = 0;
    CurrentKeyMap = CcKeyMap;
    AltKeyMap = 0;
    Hist_num = 0;
    DoingArg = 0;
    Argument = 1;
    LastCmd = F_UNASSIGNED;	/* previous command executed */
    IncMatchLen = 0;
    if (macro)
	MacroLvl = -1;		/* no currently active macros */
}

int
Load_input_line(void)
{
    static Char *Input_Line = NULL;
#ifdef SUNOS4
    long chrs = 0;
#else /* !SUNOS4 */
    /* 
     * *Everyone* else has an int, but SunOS wants long!
     * This breaks where int != long (alpha)
     */
    int chrs = 0;
#endif /* SUNOS4 */

    if (Input_Line)
	xfree(Input_Line);
    Input_Line = NULL;

    if (Tty_raw_mode)
	return 0;

#if defined(FIONREAD) && !defined(OREO)
    (void) ioctl(SHIN, FIONREAD, (ioctl_t) &chrs);
    if (chrs > 0) {
        char    buf[BUFSIZE];

	chrs = xread(SHIN, buf, min(chrs, BUFSIZE - 1));
	if (chrs > 0) {
	    buf[chrs] = '\0';
	    Input_Line = Strsave(str2short(buf));
	    PushMacro(Input_Line);
	}
#ifdef convex
        /* need to print errno message in case file is migrated */
        if (chrs < 0)
            stderror(ERR_SYSTEM, progname, strerror(errno));
#endif
    }
#endif  /* FIONREAD && !OREO */
    return chrs > 0;
}

/*
 * Bugfix (in Swedish) by:
 * Johan Widen
 * SICS, PO Box 1263, S-163 13 SPANGA, SWEDEN
 * {mcvax,munnari,cernvax,diku,inria,prlb2,penet,ukc,unido}!enea!sics.se!jw
 * Internet: jw@sics.se
 *
 * (via Hans J Albertsson (thanks))
 */
void
QuoteModeOn(void)
{
    if (MacroLvl >= 0)
	return;

#ifndef WINNT_NATIVE
    qutty = edtty;

#if defined(TERMIO) || defined(POSIX)
    qutty.d_t.c_iflag &= ~ttylist[QU_IO][M_INPUT].t_clrmask;
    qutty.d_t.c_iflag |=  ttylist[QU_IO][M_INPUT].t_setmask;

    qutty.d_t.c_oflag &= ~ttylist[QU_IO][M_OUTPUT].t_clrmask;
    qutty.d_t.c_oflag |=  ttylist[QU_IO][M_OUTPUT].t_setmask;

    qutty.d_t.c_cflag &= ~ttylist[QU_IO][M_CONTROL].t_clrmask;
    qutty.d_t.c_cflag |=  ttylist[QU_IO][M_CONTROL].t_setmask;

    qutty.d_t.c_lflag &= ~ttylist[QU_IO][M_LINED].t_clrmask;
    qutty.d_t.c_lflag |=  ttylist[QU_IO][M_LINED].t_setmask;
#else /* GSTTY */
    qutty.d_t.sg_flags &= ~ttylist[QU_IO][M_CONTROL].t_clrmask;
    qutty.d_t.sg_flags |= ttylist[QU_IO][M_CONTROL].t_setmask;
    qutty.d_lb &= ~ttylist[QU_IO][M_LOCAL].t_clrmask;
    qutty.d_lb |= ttylist[QU_IO][M_LOCAL].t_setmask;

#endif /* TERMIO || POSIX */
    if (tty_setty(SHTTY, &qutty) == -1) {
#ifdef DEBUG_TTY
	xprintf("QuoteModeOn: tty_setty: %s\n", strerror(errno));
#endif /* DEBUG_TTY */
	return;
    }
#endif /* !WINNT_NATIVE */
    Tty_quote_mode = 1;
    return;
}

void
QuoteModeOff(void)
{
    if (!Tty_quote_mode)
	return;
    Tty_quote_mode = 0;
    if (tty_setty(SHTTY, &edtty) == -1) {
#ifdef DEBUG_TTY
	xprintf("QuoteModeOff: tty_setty: %s\n", strerror(errno));
#endif /* DEBUG_TTY */
	return;
    }
    return;
}
