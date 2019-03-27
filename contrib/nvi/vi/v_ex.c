/*-
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#ifndef lint
static const char sccsid[] = "$Id: v_ex.c,v 10.61 2011/12/22 18:41:53 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/common.h"
#include "vi.h"

static int v_ecl(SCR *);
static int v_ecl_init(SCR *);
static int v_ecl_log(SCR *, TEXT *);
static int v_ex_done(SCR *, VICMD *);
static int v_exec_ex(SCR *, VICMD *, EXCMD *);

/*
 * v_again -- &
 *	Repeat the previous substitution.
 *
 * PUBLIC: int v_again(SCR *, VICMD *);
 */
int
v_again(SCR *sp, VICMD *vp)
{
	EXCMD cmd;

	ex_cinit(sp, &cmd, C_SUBAGAIN, 2, vp->m_start.lno, vp->m_start.lno, 1);
	argv_exp0(sp, &cmd, L(""), 1);
	return (v_exec_ex(sp, vp, &cmd));
}

/*
 * v_exmode -- Q
 *	Switch the editor into EX mode.
 *
 * PUBLIC: int v_exmode(SCR *, VICMD *);
 */
int
v_exmode(SCR *sp, VICMD *vp)
{
	GS *gp;

	gp = sp->gp;

	/* Try and switch screens -- the screen may not permit it. */
	if (gp->scr_screen(sp, SC_EX)) {
		msgq(sp, M_ERR,
		    "207|The Q command requires the ex terminal interface");
		return (1);
	}
	(void)gp->scr_attr(sp, SA_ALTERNATE, 0);

	/* Save the current cursor position. */
	sp->frp->lno = sp->lno;
	sp->frp->cno = sp->cno;
	F_SET(sp->frp, FR_CURSORSET);

	/* Switch to ex mode. */
	F_CLR(sp, SC_VI | SC_SCR_VI);
	F_SET(sp, SC_EX);

	/* Move out of the vi screen. */
	(void)ex_puts(sp, "\n");

	return (0);
}

/*
 * v_join -- [count]J
 *	Join lines together.
 *
 * PUBLIC: int v_join(SCR *, VICMD *);
 */
int
v_join(SCR *sp, VICMD *vp)
{
	EXCMD cmd;
	int lno;

	/*
	 * YASC.
	 * The general rule is that '#J' joins # lines, counting the current
	 * line.  However, 'J' and '1J' are the same as '2J', i.e. join the
	 * current and next lines.  This doesn't map well into the ex command
	 * (which takes two line numbers), so we handle it here.  Note that
	 * we never test for EOF -- historically going past the end of file
	 * worked just fine.
	 */
	lno = vp->m_start.lno + 1;
	if (F_ISSET(vp, VC_C1SET) && vp->count > 2)
		lno = vp->m_start.lno + (vp->count - 1);

	ex_cinit(sp, &cmd, C_JOIN, 2, vp->m_start.lno, lno, 0);
	return (v_exec_ex(sp, vp, &cmd));
}

/*
 * v_shiftl -- [count]<motion
 *	Shift lines left.
 *
 * PUBLIC: int v_shiftl(SCR *, VICMD *);
 */
int
v_shiftl(SCR *sp, VICMD *vp)
{
	EXCMD cmd;

	ex_cinit(sp, &cmd, C_SHIFTL, 2, vp->m_start.lno, vp->m_stop.lno, 0);
	argv_exp0(sp, &cmd, L("<"), 2);
	return (v_exec_ex(sp, vp, &cmd));
}

/*
 * v_shiftr -- [count]>motion
 *	Shift lines right.
 *
 * PUBLIC: int v_shiftr(SCR *, VICMD *);
 */
int
v_shiftr(SCR *sp, VICMD *vp)
{
	EXCMD cmd;

	ex_cinit(sp, &cmd, C_SHIFTR, 2, vp->m_start.lno, vp->m_stop.lno, 0);
	argv_exp0(sp, &cmd, L(">"), 2);
	return (v_exec_ex(sp, vp, &cmd));
}

/*
 * v_suspend -- ^Z
 *	Suspend vi.
 *
 * PUBLIC: int v_suspend(SCR *, VICMD *);
 */
int
v_suspend(SCR *sp, VICMD *vp)
{
	EXCMD cmd;

	ex_cinit(sp, &cmd, C_STOP, 0, OOBLNO, OOBLNO, 0);
	argv_exp0(sp, &cmd, L("suspend"), SIZE(L("suspend")));
	return (v_exec_ex(sp, vp, &cmd));
}

/*
 * v_switch -- ^^
 *	Switch to the previous file.
 *
 * PUBLIC: int v_switch(SCR *, VICMD *);
 */
int
v_switch(SCR *sp, VICMD *vp)
{
	EXCMD cmd;
	char *name;
	CHAR_T *wp;
	size_t wlen;

	/*
	 * Try the alternate file name, then the previous file
	 * name.  Use the real name, not the user's current name.
	 */
	if ((name = sp->alt_name) == NULL) {
		msgq(sp, M_ERR, "180|No previous file to edit");
		return (1);
	}

	/* If autowrite is set, write out the file. */
	if (file_m1(sp, 0, FS_ALL))
		return (1);

	ex_cinit(sp, &cmd, C_EDIT, 0, OOBLNO, OOBLNO, 0);
	CHAR2INT(sp, name, strlen(name) + 1, wp, wlen);
	argv_exp0(sp, &cmd, wp, wlen);
	return (v_exec_ex(sp, vp, &cmd));
}

/*
 * v_tagpush -- ^[
 *	Do a tag search on the cursor keyword.
 *
 * PUBLIC: int v_tagpush(SCR *, VICMD *);
 */
int
v_tagpush(SCR *sp, VICMD *vp)
{
	EXCMD cmd;

	ex_cinit(sp, &cmd, C_TAG, 0, OOBLNO, 0, 0);
	argv_exp0(sp, &cmd, VIP(sp)->keyw, STRLEN(VIP(sp)->keyw) + 1);
	return (v_exec_ex(sp, vp, &cmd));
}

/*
 * v_tagpop -- ^T
 *	Pop the tags stack.
 *
 * PUBLIC: int v_tagpop(SCR *, VICMD *);
 */
int
v_tagpop(SCR *sp, VICMD *vp)
{
	EXCMD cmd;

	ex_cinit(sp, &cmd, C_TAGPOP, 0, OOBLNO, 0, 0);
	return (v_exec_ex(sp, vp, &cmd));
}

/*
 * v_filter -- [count]!motion command(s)
 *	Run range through shell commands, replacing text.
 *
 * PUBLIC: int v_filter(SCR *, VICMD *);
 */
int
v_filter(SCR *sp, VICMD *vp)
{
	EXCMD cmd;
	TEXT *tp;

	/*
	 * !!!
	 * Historical vi permitted "!!" in an empty file, and it's handled
	 * as a special case in the ex_bang routine.  Don't modify this setup
	 * without understanding that one.  In particular, note that we're
	 * manipulating the ex argument structures behind ex's back.
	 *
	 * !!!
	 * Historical vi did not permit the '!' command to be associated with
	 * a non-line oriented motion command, in general, although it did
	 * with search commands.  So, !f; and !w would fail, but !/;<CR>
	 * would succeed, even if they all moved to the same location in the
	 * current line.  I don't see any reason to disallow '!' using any of
	 * the possible motion commands.
	 *
	 * !!!
	 * Historical vi ran the last bang command if N or n was used as the
	 * search motion.
	 */
	if (F_ISSET(vp, VC_ISDOT) ||
	    ISCMD(vp->rkp, 'N') || ISCMD(vp->rkp, 'n')) {
		ex_cinit(sp,
		    &cmd, C_BANG, 2, vp->m_start.lno, vp->m_stop.lno, 0);
		EXP(sp)->argsoff = 0;			/* XXX */

		if (argv_exp1(sp, &cmd, L("!"), 1, 1))
			return (1);
		cmd.argc = EXP(sp)->argsoff;		/* XXX */
		cmd.argv = EXP(sp)->args;		/* XXX */
		return (v_exec_ex(sp, vp, &cmd));
	}

	/* Get the command from the user. */
	if (v_tcmd(sp, vp,
	    '!', TXT_BS | TXT_CR | TXT_ESCAPE | TXT_FILEC | TXT_PROMPT))
		return (1);

	/*
	 * Check to see if the user changed their mind.
	 *
	 * !!!
	 * Entering <escape> on an empty line was historically an error,
	 * this implementation doesn't bother.
	 */
	tp = TAILQ_FIRST(sp->tiq);
	if (tp->term != TERM_OK) {
		vp->m_final.lno = sp->lno;
		vp->m_final.cno = sp->cno;
		return (0);
	}

	/* Home the cursor. */
	vs_home(sp);

	ex_cinit(sp, &cmd, C_BANG, 2, vp->m_start.lno, vp->m_stop.lno, 0);
	EXP(sp)->argsoff = 0;			/* XXX */

	if (argv_exp1(sp, &cmd, tp->lb + 1, tp->len - 1, 1))
		return (1);
	cmd.argc = EXP(sp)->argsoff;		/* XXX */
	cmd.argv = EXP(sp)->args;		/* XXX */
	return (v_exec_ex(sp, vp, &cmd));
}

/*
 * v_exec_ex --
 *	Execute an ex command.
 */
static int
v_exec_ex(SCR *sp, VICMD *vp, EXCMD *exp)
{
	int rval;

	rval = exp->cmd->fn(sp, exp);
	return (v_ex_done(sp, vp) || rval);
}

/*
 * v_ex -- :
 *	Execute a colon command line.
 *
 * PUBLIC: int v_ex(SCR *, VICMD *);
 */
int
v_ex(SCR *sp, VICMD *vp)
{
	GS *gp;
	TEXT *tp;
	int do_cedit, do_resolution, ifcontinue;

	gp = sp->gp;

	/*
	 * !!!
	 * If we put out more than a single line of messages, or ex trashes
	 * the screen, the user may continue entering ex commands.  We find
	 * this out when we do the screen/message resolution.  We can't enter
	 * completely into ex mode however, because the user can elect to
	 * return into vi mode by entering any key, i.e. we have to be in raw
	 * mode.
	 */
	for (do_cedit = do_resolution = 0;;) {
		/*
		 * !!!
		 * There may already be an ex command waiting to run.  If
		 * so, we continue with it.
		 */
		if (!EXCMD_RUNNING(gp)) {
			/* Get a command. */
			if (v_tcmd(sp, vp, ':',
			    TXT_BS | TXT_CEDIT | TXT_FILEC | TXT_PROMPT))
				return (1);
			tp = TAILQ_FIRST(sp->tiq);

			/*
			 * If the user entered a single <esc>, they want to
			 * edit their colon command history.  If they already
			 * entered some text, move it into the edit history.
			 */
			if (tp->term == TERM_CEDIT) {
				if (tp->len > 1 && v_ecl_log(sp, tp))
					return (1);
				do_cedit = 1;
				break;
			}

			/* If the user didn't enter anything, return. */
			if (tp->term == TERM_BS)
				break;

			/* If the user changed their mind, return. */
			if (tp->term != TERM_OK)
				break;

			/* Log the command. */
			if (O_STR(sp, O_CEDIT) != NULL && v_ecl_log(sp, tp))
				return (1);

			/* Push a command on the command stack. */
			if (ex_run_str(sp, NULL, tp->lb, tp->len, 0, 1))
				return (1);
		}

		/* Home the cursor. */
		vs_home(sp);

		/*
		 * !!!
		 * If the editor wrote the screen behind curses back, put out
		 * a <newline> so that we don't overwrite the user's command
		 * with its output or the next want-to-continue? message.  This
		 * doesn't belong here, but I can't find another place to put
		 * it.  See, we resolved the output from the last ex command,
		 * and the user entered another one.  This is the only place
		 * where we have control before the ex command writes output.
		 * We could get control in vs_msg(), but we have no way to know
		 * if command didn't put out any output when we try and resolve
		 * this command.  This fixes a bug where combinations of ex
		 * commands, e.g. ":set<CR>:!date<CR>:set" didn't look right.
		 */
		if (F_ISSET(sp, SC_SCR_EXWROTE))
			(void)putchar('\n');

		/* Call the ex parser. */
		(void)ex_cmd(sp);

		/* Flush ex messages. */
		(void)ex_fflush(sp);

		/* Resolve any messages. */
		if (vs_ex_resolve(sp, &ifcontinue))
			return (1);

		/*
		 * Continue or return.  If continuing, make sure that we
		 * eventually do resolution.
		 */
		if (!ifcontinue)
			break;
		do_resolution = 1;

		/* If we're continuing, it's a new command. */
		++sp->ccnt;
	}

	/*
	 * If the user previously continued an ex command, we have to do
	 * resolution to clean up the screen.  Don't wait, we already did
	 * that.
	 */
	if (do_resolution) {
		F_SET(sp, SC_EX_WAIT_NO);
		if (vs_ex_resolve(sp, &ifcontinue))
			return (1);
	}

	/* Cleanup from the ex command. */
	if (v_ex_done(sp, vp))
		return (1);

	/* The user may want to edit their colon command history. */
	if (do_cedit)
		return (v_ecl(sp));

	return (0);
}

/*
 * v_ex_done --
 *	Cleanup from an ex command.
 */
static int
v_ex_done(SCR *sp, VICMD *vp)
{
	size_t len;

	/*
	 * The only cursor modifications are real, however, the underlying
	 * line may have changed; don't trust anything.  This code has been
	 * a remarkably fertile place for bugs.  Do a reality check on a
	 * cursor value, and make sure it's okay.  If necessary, change it.
	 * Ex keeps track of the line number, but it cares less about the
	 * column and it may have disappeared.
	 *
	 * Don't trust ANYTHING.
	 *
	 * XXX
	 * Ex will soon have to start handling the column correctly; see
	 * the POSIX 1003.2 standard.
	 */
	if (db_eget(sp, sp->lno, NULL, &len, NULL)) {
		sp->lno = 1;
		sp->cno = 0;
	} else if (sp->cno >= len)
		sp->cno = len ? len - 1 : 0;

	vp->m_final.lno = sp->lno;
	vp->m_final.cno = sp->cno;

	/*
	 * Don't re-adjust the cursor after executing an ex command,
	 * and ex movements are permanent.
	 */
	F_CLR(vp, VM_RCM_MASK);
	F_SET(vp, VM_RCM_SET);

	return (0);
}

/*
 * v_ecl --
 *	Start an edit window on the colon command-line commands.
 */
static int
v_ecl(SCR *sp)
{
	GS *gp;
	SCR *new;

	/* Initialize the screen, if necessary. */
	gp = sp->gp;
	if (gp->ccl_sp == NULL && v_ecl_init(sp))
		return (1);

	/* Get a new screen. */
	if (screen_init(gp, sp, &new))
		return (1);
	if (vs_split(sp, new, 1)) {
		(void)screen_end(new);
		return (1);
	}

	/* Attach to the screen. */
	new->ep = gp->ccl_sp->ep;
	++new->ep->refcnt;

	new->frp = gp->ccl_sp->frp;
	new->frp->flags = sp->frp->flags;

	/* Move the cursor to the end. */
	(void)db_last(new, &new->lno);
	if (new->lno == 0)
		new->lno = 1;

	/* Remember the originating window. */
	sp->ccl_parent = sp;

	/* It's a special window. */
	F_SET(new, SC_COMEDIT);

#if defined(USE_WIDECHAR) && defined(USE_ICONV)
	/* Bypass iconv on writing to DB. */
	o_set(new, O_FILEENCODING, OS_STRDUP, codeset(), 0);
#endif

	/* Set up the switch. */
	sp->nextdisp = new;
	F_SET(sp, SC_SSWITCH);
	return (0);
}

/*
 * v_ecl_exec --
 *	Execute a command from a colon command-line window.
 *
 * PUBLIC: int v_ecl_exec(SCR *);
 */
int
v_ecl_exec(SCR *sp)
{
	size_t len;
	CHAR_T *p;

	if (db_get(sp, sp->lno, 0, &p, &len) && sp->lno == 1) {
		v_emsg(sp, NULL, VIM_EMPTY);
		return (1);
	}
	if (len == 0) {
		msgq(sp, M_BERR, "307|No ex command to execute");
		return (1);
	}
	
	/* Push the command on the command stack. */
	if (ex_run_str(sp, NULL, p, len, 0, 0))
		return (1);

	/* Set up the switch. */
	sp->nextdisp = sp->ccl_parent;
	F_SET(sp, SC_EXIT);
	return (0);
}

/*
 * v_ecl_log --
 *	Log a command into the colon command-line log file.
 */
static int
v_ecl_log(SCR *sp, TEXT *tp)
{
	recno_t lno;
	int rval;
	CHAR_T *p;
	size_t len;
	SCR *ccl_sp;

	/* Initialize the screen, if necessary. */
	if (sp->gp->ccl_sp == NULL && v_ecl_init(sp))
		return (1);

	ccl_sp = sp->gp->ccl_sp;

	/*
	 * Don't log colon command window commands into the colon command
	 * window...
	 */
	if (sp->ep == ccl_sp->ep)
		return (0);

	if (db_last(ccl_sp, &lno)) {
		return (1);
	}
	/* Don't log line that is identical to previous one */
	if (lno > 0 &&
	    !db_get(ccl_sp, lno, 0, &p, &len) &&
	    len == tp->len &&
	    !MEMCMP(tp->lb, p, len))
		rval = 0;
	else {
		rval = db_append(ccl_sp, 0, lno, tp->lb, tp->len);
		/* XXXX end "transaction" on ccl */
		/* Is this still necessary now that we no longer hijack sp ? */
		log_cursor(ccl_sp);
	}

	return (rval);
}

/*
 * v_ecl_init --
 *	Initialize the colon command-line log file.
 */
static int
v_ecl_init(SCR *sp)
{
	FREF *frp;
	GS *gp;

	gp = sp->gp;

	/* Get a temporary file. */
	if ((frp = file_add(sp, NULL)) == NULL)
		return (1);

	/*
	 * XXX
	 * Create a screen -- the file initialization code wants one.
	 */
	if (screen_init(gp, sp, &gp->ccl_sp))
		return (1);
	if (file_init(gp->ccl_sp, frp, NULL, 0)) {
		(void)screen_end(gp->ccl_sp);
		gp->ccl_sp = NULL;
		return (1);
	}

	/* The underlying file isn't recoverable. */
	F_CLR(gp->ccl_sp->ep, F_RCV_ON);

	return (0);
}
