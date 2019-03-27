/*-
 * Copyright (c) 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#ifndef lint
static const char sccsid[] = "$Id: vs_msg.c,v 10.88 2013/03/19 09:59:03 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/common.h"
#include "vi.h"

typedef enum {
	SCROLL_W,			/* User wait. */
	SCROLL_W_EX,			/* User wait, or enter : to continue. */
	SCROLL_W_QUIT			/* User wait, or enter q to quit. */
					/*
					 * SCROLL_W_QUIT has another semantic
					 * -- only wait if the screen is full
					 */
} sw_t;

static void	vs_divider(SCR *);
static void	vs_msgsave(SCR *, mtype_t, char *, size_t);
static void	vs_output(SCR *, mtype_t, const char *, int);
static void	vs_scroll(SCR *, int *, sw_t);
static void	vs_wait(SCR *, int *, sw_t);

/*
 * vs_busy --
 *	Display, update or clear a busy message.
 *
 * This routine is the default editor interface for vi busy messages.  It
 * implements a standard strategy of stealing lines from the bottom of the
 * vi text screen.  Screens using an alternate method of displaying busy
 * messages, e.g. X11 clock icons, should set their scr_busy function to the
 * correct function before calling the main editor routine.
 *
 * PUBLIC: void vs_busy(SCR *, const char *, busy_t);
 */
void
vs_busy(SCR *sp, const char *msg, busy_t btype)
{
	GS *gp;
	VI_PRIVATE *vip;
	static const char flagc[] = "|/-\\";
	struct timespec ts, ts_diff;
	const struct timespec ts_min = { 0, 125000000 };
	size_t len, notused;
	const char *p;

	/* Ex doesn't display busy messages. */
	if (F_ISSET(sp, SC_EX | SC_SCR_EXWROTE))
		return;

	gp = sp->gp;
	vip = VIP(sp);

	/*
	 * Most of this routine is to deal with the screen sharing real estate
	 * between the normal edit messages and the busy messages.  Logically,
	 * all that's needed is something that puts up a message, periodically
	 * updates it, and then goes away.
	 */
	switch (btype) {
	case BUSY_ON:
		++vip->busy_ref;
		if (vip->totalcount != 0 || vip->busy_ref != 1)
			break;

		/* Initialize state for updates. */
		vip->busy_ch = 0;
		timepoint_steady(&vip->busy_ts);

		/* Save the current cursor. */
		(void)gp->scr_cursor(sp, &vip->busy_oldy, &vip->busy_oldx);

		/* Display the busy message. */
		p = msg_cat(sp, msg, &len);
		(void)gp->scr_move(sp, LASTLINE(sp), 0);
		(void)gp->scr_addstr(sp, p, len);
		(void)gp->scr_cursor(sp, &notused, &vip->busy_fx);
		(void)gp->scr_clrtoeol(sp);
		(void)gp->scr_move(sp, LASTLINE(sp), vip->busy_fx);
		break;
	case BUSY_OFF:
		if (vip->busy_ref == 0)
			break;
		--vip->busy_ref;

		/*
		 * If the line isn't in use for another purpose, clear it.
		 * Always return to the original position.
		 */
		if (vip->totalcount == 0 && vip->busy_ref == 0) {
			(void)gp->scr_move(sp, LASTLINE(sp), 0);
			(void)gp->scr_clrtoeol(sp);
		}
		(void)gp->scr_move(sp, vip->busy_oldy, vip->busy_oldx);
		break;
	case BUSY_UPDATE:
		if (vip->totalcount != 0 || vip->busy_ref == 0)
			break;

		/* Update no more than every 1/8 of a second. */
		timepoint_steady(&ts);
		ts_diff = ts;
		timespecsub(&ts_diff, &vip->busy_ts);
		if (timespeccmp(&ts_diff, &ts_min, <))
			return;
		vip->busy_ts = ts;

		/* Display the update. */
		if (vip->busy_ch == sizeof(flagc) - 1)
			vip->busy_ch = 0;
		(void)gp->scr_move(sp, LASTLINE(sp), vip->busy_fx);
		(void)gp->scr_addstr(sp, flagc + vip->busy_ch++, 1);
		(void)gp->scr_move(sp, LASTLINE(sp), vip->busy_fx);
		break;
	}
	(void)gp->scr_refresh(sp, 0);
}

/* 
 * vs_home --
 *	Home the cursor to the bottom row, left-most column.
 *
 * PUBLIC: void vs_home(SCR *);
 */
void
vs_home(SCR *sp)
{
	(void)sp->gp->scr_move(sp, LASTLINE(sp), 0);
	(void)sp->gp->scr_refresh(sp, 0);
}

/*
 * vs_update --
 *	Update a command.
 *
 * PUBLIC: void vs_update(SCR *, const char *, const CHAR_T *);
 */
void
vs_update(SCR *sp, const char *m1, const CHAR_T *m2)
{
	GS *gp;
	size_t len, mlen, oldx, oldy;
	CONST char *np;
	size_t nlen;

	gp = sp->gp;

	/*
	 * This routine displays a message on the bottom line of the screen,
	 * without updating any of the command structures that would keep it
	 * there for any period of time, i.e. it is overwritten immediately.
	 *
	 * It's used by the ex read and ! commands when the user's command is
	 * expanded, and by the ex substitution confirmation prompt.
	 */
	if (F_ISSET(sp, SC_SCR_EXWROTE)) {
		if (m2 != NULL)
			INT2CHAR(sp, m2, STRLEN(m2) + 1, np, nlen);
		(void)ex_printf(sp,
		    "%s\n", m1 == NULL? "" : m1, m2 == NULL ? "" : np);
		(void)ex_fflush(sp);
	}

	/*
	 * Save the cursor position, the substitute-with-confirmation code
	 * will have already set it correctly.
	 */
	(void)gp->scr_cursor(sp, &oldy, &oldx);

	/* Clear the bottom line. */
	(void)gp->scr_move(sp, LASTLINE(sp), 0);
	(void)gp->scr_clrtoeol(sp);

	/*
	 * XXX
	 * Don't let long file names screw up the screen.
	 */
	if (m1 != NULL) {
		mlen = len = strlen(m1);
		if (len > sp->cols - 2)
			mlen = len = sp->cols - 2;
		(void)gp->scr_addstr(sp, m1, mlen);
	} else
		len = 0;
	if (m2 != NULL) {
		mlen = STRLEN(m2);
		if (len + mlen > sp->cols - 2)
			mlen = (sp->cols - 2) - len;
		(void)gp->scr_waddstr(sp, m2, mlen);
	}

	(void)gp->scr_move(sp, oldy, oldx);
	(void)gp->scr_refresh(sp, 0);
}

/*
 * vs_msg --
 *	Display ex output or error messages for the screen.
 *
 * This routine is the default editor interface for all ex output, and all ex
 * and vi error/informational messages.  It implements the standard strategy
 * of stealing lines from the bottom of the vi text screen.  Screens using an
 * alternate method of displaying messages, e.g. dialog boxes, should set their
 * scr_msg function to the correct function before calling the editor.
 *
 * PUBLIC: void vs_msg(SCR *, mtype_t, char *, size_t);
 */
void
vs_msg(SCR *sp, mtype_t mtype, char *line, size_t len)
{
	GS *gp;
	VI_PRIVATE *vip;
	size_t maxcols, oldx, oldy, padding;
	const char *e, *s, *t;

	gp = sp->gp;
	vip = VIP(sp);

	/*
	 * Ring the bell if it's scheduled.
	 *
	 * XXX
	 * Shouldn't we save this, too?
	 */
	if (F_ISSET(sp, SC_TINPUT_INFO) || F_ISSET(gp, G_BELLSCHED))
		if (F_ISSET(sp, SC_SCR_VI)) {
			F_CLR(gp, G_BELLSCHED);
			(void)gp->scr_bell(sp);
		} else
			F_SET(gp, G_BELLSCHED);

	/*
	 * If vi is using the error line for text input, there's no screen
	 * real-estate for the error message.  Nothing to do without some
	 * information as to how important the error message is.
	 */
	if (F_ISSET(sp, SC_TINPUT_INFO))
		return;

	/*
	 * Ex or ex controlled screen output.
	 *
	 * If output happens during startup, e.g., a .exrc file, we may be
	 * in ex mode but haven't initialized the screen.  Initialize here,
	 * and in this case, stay in ex mode.
	 *
	 * If the SC_SCR_EXWROTE bit is set, then we're switching back and
	 * forth between ex and vi, but the screen is trashed and we have
	 * to respect that.  Switch to ex mode long enough to put out the
	 * message.
	 *
	 * If the SC_EX_WAIT_NO bit is set, turn it off -- we're writing to
	 * the screen, so previous opinions are ignored.
	 */
	if (F_ISSET(sp, SC_EX | SC_SCR_EXWROTE)) {
		if (!F_ISSET(sp, SC_SCR_EX))
			if (F_ISSET(sp, SC_SCR_EXWROTE)) {
				if (sp->gp->scr_screen(sp, SC_EX))
					return;
			} else
				if (ex_init(sp))
					return;

		if (mtype == M_ERR)
			(void)gp->scr_attr(sp, SA_INVERSE, 1);
		(void)printf("%.*s", (int)len, line);
		if (mtype == M_ERR)
			(void)gp->scr_attr(sp, SA_INVERSE, 0);
		(void)fflush(stdout);

		F_CLR(sp, SC_EX_WAIT_NO);

		if (!F_ISSET(sp, SC_SCR_EX))
			(void)sp->gp->scr_screen(sp, SC_VI);
		return;
	}

	/* If the vi screen isn't ready, save the message. */
	if (!F_ISSET(sp, SC_SCR_VI)) {
		(void)vs_msgsave(sp, mtype, line, len);
		return;
	}

	/* Save the cursor position. */
	(void)gp->scr_cursor(sp, &oldy, &oldx);

	/* If it's an ex output message, just write it out. */
	if (mtype == M_NONE) {
		vs_output(sp, mtype, line, len);
		goto ret;
	}

	/*
	 * If it's a vi message, strip the trailing <newline> so we can
	 * try and paste messages together.
	 */
	if (line[len - 1] == '\n')
		--len;

	/*
	 * If a message won't fit on a single line, try to split on a <blank>.
	 * If a subsequent message fits on the same line, write a separator
	 * and output it.  Otherwise, put out a newline.
	 *
	 * Need up to two padding characters normally; a semi-colon and a
	 * separating space.  If only a single line on the screen, add some
	 * more for the trailing continuation message.
	 *
	 * XXX
	 * Assume that periods and semi-colons take up a single column on the
	 * screen.
	 *
	 * XXX
	 * There are almost certainly pathological cases that will break this
	 * code.
	 */
	if (IS_ONELINE(sp))
		(void)msg_cmsg(sp, CMSG_CONT_S, &padding);
	else
		padding = 0;
	padding += 2;

	maxcols = sp->cols - 1;
	if (vip->lcontinue != 0)
		if (len + vip->lcontinue + padding > maxcols)
			vs_output(sp, vip->mtype, ".\n", 2);
		else  {
			vs_output(sp, vip->mtype, ";", 1);
			vs_output(sp, M_NONE, " ", 1);
		}
	vip->mtype = mtype;
	for (s = line;; s = t) {
		for (; len > 0 && isblank(*s); --len, ++s);
		if (len == 0)
			break;
		if (len + vip->lcontinue > maxcols) {
			for (e = s + (maxcols - vip->lcontinue);
			    e > s && !isblank(*e); --e);
			if (e == s)
				 e = t = s + (maxcols - vip->lcontinue);
			else
				for (t = e; isblank(e[-1]); --e);
		} else
			e = t = s + len;

		/*
		 * If the message ends in a period, discard it, we want to
		 * gang messages where possible.
		 */
		len -= t - s;
		if (len == 0 && (e - s) > 1 && s[(e - s) - 1] == '.')
			--e;
		vs_output(sp, mtype, s, e - s);

		if (len != 0)
			vs_output(sp, M_NONE, "\n", 1);

		if (INTERRUPTED(sp))
			break;
	}

ret:	(void)gp->scr_move(sp, oldy, oldx);
	(void)gp->scr_refresh(sp, 0);
}

/*
 * vs_output --
 *	Output the text to the screen.
 */
static void
vs_output(SCR *sp, mtype_t mtype, const char *line, int llen)
{
	GS *gp;
	VI_PRIVATE *vip;
	size_t notused;
	int len, rlen, tlen;
	const char *p, *t;
	char *cbp, *ecbp, cbuf[128];

	gp = sp->gp;
	vip = VIP(sp);
	for (p = line, rlen = llen; llen > 0;) {
		/* Get the next physical line. */
		if ((p = memchr(line, '\n', llen)) == NULL)
			len = llen;
		else
			len = p - line;

		/*
		 * The max is sp->cols characters, and we may have already
		 * written part of the line.
		 */
		if (len + vip->lcontinue > sp->cols)
			len = sp->cols - vip->lcontinue;

		/*
		 * If the first line output, do nothing.  If the second line
		 * output, draw the divider line.  If drew a full screen, we
		 * remove the divider line.  If it's a continuation line, move
		 * to the continuation point, else, move the screen up.
		 */
		if (vip->lcontinue == 0) {
			if (!IS_ONELINE(sp)) {
				if (vip->totalcount == 1) {
					(void)gp->scr_move(sp,
					    LASTLINE(sp) - 1, 0);
					(void)gp->scr_clrtoeol(sp);
					(void)vs_divider(sp);
					F_SET(vip, VIP_DIVIDER);
					++vip->totalcount;
					++vip->linecount;
				}
				if (vip->totalcount == sp->t_maxrows &&
				    F_ISSET(vip, VIP_DIVIDER)) {
					--vip->totalcount;
					--vip->linecount;
					F_CLR(vip, VIP_DIVIDER);
				}
			}
			if (vip->totalcount != 0)
				vs_scroll(sp, NULL, SCROLL_W_QUIT);

			(void)gp->scr_move(sp, LASTLINE(sp), 0);
			++vip->totalcount;
			++vip->linecount;

			if (INTERRUPTED(sp))
				break;
		} else
			(void)gp->scr_move(sp, LASTLINE(sp), vip->lcontinue);

		/* Error messages are in inverse video. */
		if (mtype == M_ERR)
			(void)gp->scr_attr(sp, SA_INVERSE, 1);

		/* Display the line, doing character translation. */
#define	FLUSH {								\
	*cbp = '\0';							\
	(void)gp->scr_addstr(sp, cbuf, cbp - cbuf);			\
	cbp = cbuf;							\
}
		ecbp = (cbp = cbuf) + sizeof(cbuf) - 1;
		for (t = line, tlen = len; tlen--; ++t) {
			/*
			 * Replace tabs with spaces, there are places in
			 * ex that do column calculations without looking
			 * at <tabs> -- and all routines that care about
			 * <tabs> do their own expansions.  This catches
			 * <tabs> in things like tag search strings.
			 */
			if (cbp + 1 >= ecbp)
				FLUSH;
			*cbp++ = *t == '\t' ? ' ' : *t;
		}
		if (cbp > cbuf)
			FLUSH;
		if (mtype == M_ERR)
			(void)gp->scr_attr(sp, SA_INVERSE, 0);

		/* Clear the rest of the line. */
		(void)gp->scr_clrtoeol(sp);

		/* If we loop, it's a new line. */
		vip->lcontinue = 0;

		/* Reset for the next line. */
		line += len;
		llen -= len;
		if (p != NULL) {
			++line;
			--llen;
		}
	}

	/* Set up next continuation line. */
	if (p == NULL)
		gp->scr_cursor(sp, &notused, &vip->lcontinue);
}

/*
 * vs_ex_resolve --
 *	Deal with ex message output.
 *
 * This routine is called when exiting a colon command to resolve any ex
 * output that may have occurred.
 *
 * PUBLIC: int vs_ex_resolve(SCR *, int *);
 */
int
vs_ex_resolve(SCR *sp, int *continuep)
{
	EVENT ev;
	GS *gp;
	VI_PRIVATE *vip;
	sw_t wtype;

	gp = sp->gp;
	vip = VIP(sp);
	*continuep = 0;

	/* If we ran any ex command, we can't trust the cursor position. */
	F_SET(vip, VIP_CUR_INVALID);

	/* Terminate any partially written message. */
	if (vip->lcontinue != 0) {
		vs_output(sp, vip->mtype, ".", 1);
		vip->lcontinue = 0;

		vip->mtype = M_NONE;
	}

	/*
	 * If we switched out of the vi screen into ex, switch back while we
	 * figure out what to do with the screen and potentially get another
	 * command to execute.
	 *
	 * If we didn't switch into ex, we're not required to wait, and less
	 * than 2 lines of output, we can continue without waiting for the
	 * wait.
	 *
	 * Note, all other code paths require waiting, so we leave the report
	 * of modified lines until later, so that we won't wait for no other
	 * reason than a threshold number of lines were modified.  This means
	 * we display cumulative line modification reports for groups of ex
	 * commands.  That seems right to me (well, at least not wrong).
	 */
	if (F_ISSET(sp, SC_SCR_EXWROTE)) {
		if (sp->gp->scr_screen(sp, SC_VI))
			return (1);
	} else
		if (!F_ISSET(sp, SC_EX_WAIT_YES) && vip->totalcount < 2) {
			F_CLR(sp, SC_EX_WAIT_NO);
			return (0);
		}

	/* Clear the required wait flag, it's no longer needed. */
	F_CLR(sp, SC_EX_WAIT_YES);

	/*
	 * Wait, unless explicitly told not to wait or the user interrupted
	 * the command.  If the user is leaving the screen, for any reason,
	 * they can't continue with further ex commands.
	 */
	if (!F_ISSET(sp, SC_EX_WAIT_NO) && !INTERRUPTED(sp)) {
		wtype = F_ISSET(sp, SC_EXIT | SC_EXIT_FORCE |
		    SC_FSWITCH | SC_SSWITCH) ? SCROLL_W : SCROLL_W_EX;
		if (F_ISSET(sp, SC_SCR_EXWROTE))
			vs_wait(sp, continuep, wtype);
		else
			vs_scroll(sp, continuep, wtype);
		if (*continuep)
			return (0);
	}

	/* If ex wrote on the screen, refresh the screen image. */
	if (F_ISSET(sp, SC_SCR_EXWROTE))
		F_SET(vip, VIP_N_EX_PAINT);

	/*
	 * If we're not the bottom of the split screen stack, the screen
	 * image itself is wrong, so redraw everything.
	 */
	if (TAILQ_NEXT(sp, q) != NULL)
		F_SET(sp, SC_SCR_REDRAW);

	/* If ex changed the underlying file, the map itself is wrong. */
	if (F_ISSET(vip, VIP_N_EX_REDRAW))
		F_SET(sp, SC_SCR_REFORMAT);

	/* Ex may have switched out of the alternate screen, return. */
	(void)gp->scr_attr(sp, SA_ALTERNATE, 1);

	/*
	 * Whew.  We're finally back home, after what feels like years.
	 * Kiss the ground.
	 */
	F_CLR(sp, SC_SCR_EXWROTE | SC_EX_WAIT_NO);

	/*
	 * We may need to repaint some of the screen, e.g.:
	 *
	 *	:set
	 *	:!ls
	 *
	 * gives us a combination of some lines that are "wrong", and a need
	 * for a full refresh.
	 */
	if (vip->totalcount > 1) {
		/* Set up the redraw of the overwritten lines. */
		ev.e_event = E_REPAINT;
		ev.e_flno = vip->totalcount >=
		    sp->rows ? 1 : sp->rows - vip->totalcount;
		ev.e_tlno = sp->rows;

		/* Reset the count of overwriting lines. */
		vip->linecount = vip->lcontinue = vip->totalcount = 0;

		/* Redraw. */
		(void)vs_repaint(sp, &ev);
	} else
		/* Reset the count of overwriting lines. */
		vip->linecount = vip->lcontinue = vip->totalcount = 0;

	return (0);
}

/*
 * vs_resolve --
 *	Deal with message output.
 *
 * PUBLIC: int vs_resolve(SCR *, SCR *, int);
 */
int
vs_resolve(SCR *sp, SCR *csp, int forcewait)
{
	EVENT ev;
	GS *gp;
	MSGS *mp;
	VI_PRIVATE *vip;
	size_t oldy, oldx;
	int redraw;

	/*
	 * Vs_resolve is called from the main vi loop and the refresh function
	 * to periodically ensure that the user has seen any messages that have
	 * been displayed and that any status lines are correct.  The sp screen
	 * is the screen we're checking, usually the current screen.  When it's
	 * not, csp is the current screen, used for final cursor positioning.
	 */
	gp = sp->gp;
	vip = VIP(sp);
	if (csp == NULL)
		csp = sp;

	/* Save the cursor position. */
	(void)gp->scr_cursor(csp, &oldy, &oldx);

	/* Ring the bell if it's scheduled. */
	if (F_ISSET(gp, G_BELLSCHED)) {
		F_CLR(gp, G_BELLSCHED);
		(void)gp->scr_bell(sp);
	}

	/* Display new file status line. */
	if (F_ISSET(sp, SC_STATUS)) {
		F_CLR(sp, SC_STATUS);
		msgq_status(sp, sp->lno, MSTAT_TRUNCATE);
	}

	/* Report on line modifications. */
	mod_rpt(sp);

	/*
	 * Flush any saved messages.  If the screen isn't ready, refresh
	 * it.  (A side-effect of screen refresh is that we can display
	 * messages.)  Once this is done, don't trust the cursor.  That
	 * extra refresh screwed the pooch.
	 */
	if (!SLIST_EMPTY(gp->msgq)) {
		if (!F_ISSET(sp, SC_SCR_VI) && vs_refresh(sp, 1))
			return (1);
		while ((mp = SLIST_FIRST(gp->msgq)) != NULL) {
			gp->scr_msg(sp, mp->mtype, mp->buf, mp->len);
			SLIST_REMOVE_HEAD(gp->msgq, q);
			free(mp->buf);
			free(mp);
		}
		F_SET(vip, VIP_CUR_INVALID);
	}

	switch (vip->totalcount) {
	case 0:
		redraw = 0;
		break;
	case 1:
		/*
		 * If we're switching screens, we have to wait for messages,
		 * regardless.  If we don't wait, skip updating the modeline.
		 */
		if (forcewait)
			vs_scroll(sp, NULL, SCROLL_W);
		else
			F_SET(vip, VIP_S_MODELINE);

		redraw = 0;
		break;
	default:
		/*
		 * If >1 message line in use, prompt the user to continue and
		 * repaint overwritten lines.
		 */
		vs_scroll(sp, NULL, SCROLL_W);

		ev.e_event = E_REPAINT;
		ev.e_flno = vip->totalcount >=
		    sp->rows ? 1 : sp->rows - vip->totalcount;
		ev.e_tlno = sp->rows;

		redraw = 1;
		break;
	}

	/* Reset the count of overwriting lines. */
	vip->linecount = vip->lcontinue = vip->totalcount = 0;

	/* Redraw. */
	if (redraw)
		(void)vs_repaint(sp, &ev);

	/* Restore the cursor position. */
	(void)gp->scr_move(csp, oldy, oldx);

	return (0);
}

/*
 * vs_scroll --
 *	Scroll the screen for output.
 */
static void
vs_scroll(SCR *sp, int *continuep, sw_t wtype)
{
	GS *gp;
	VI_PRIVATE *vip;

	gp = sp->gp;
	vip = VIP(sp);
	if (!IS_ONELINE(sp)) {
		/*
		 * Scroll the screen.  Instead of scrolling the entire screen,
		 * delete the line above the first line output so preserve the
		 * maximum amount of the screen.
		 */
		(void)gp->scr_move(sp, vip->totalcount <
		    sp->rows ? LASTLINE(sp) - vip->totalcount : 0, 0);
		(void)gp->scr_deleteln(sp);

		/* If there are screens below us, push them back into place. */
		if (TAILQ_NEXT(sp, q) != NULL) {
			(void)gp->scr_move(sp, LASTLINE(sp), 0);
			(void)gp->scr_insertln(sp);
		}
	}
	if (wtype == SCROLL_W_QUIT && vip->linecount < sp->t_maxrows)
		return;
	vs_wait(sp, continuep, wtype);
}

/*
 * vs_wait --
 *	Prompt the user to continue.
 */
static void
vs_wait(SCR *sp, int *continuep, sw_t wtype)
{
	EVENT ev;
	VI_PRIVATE *vip;
	const char *p;
	GS *gp;
	size_t len;

	gp = sp->gp;
	vip = VIP(sp);

	(void)gp->scr_move(sp, LASTLINE(sp), 0);
	if (IS_ONELINE(sp))
		p = msg_cmsg(sp, CMSG_CONT_S, &len);
	else
		switch (wtype) {
		case SCROLL_W_QUIT:
			p = msg_cmsg(sp, CMSG_CONT_Q, &len);
			break;
		case SCROLL_W_EX:
			p = msg_cmsg(sp, CMSG_CONT_EX, &len);
			break;
		case SCROLL_W:
			p = msg_cmsg(sp, CMSG_CONT, &len);
			break;
		default:
			abort();
			/* NOTREACHED */
		}
	(void)gp->scr_addstr(sp, p, len);

	++vip->totalcount;
	vip->linecount = 0;

	(void)gp->scr_clrtoeol(sp);
	(void)gp->scr_refresh(sp, 0);

	/* Get a single character from the terminal. */
	if (continuep != NULL)
		*continuep = 0;
	for (;;) {
		if (v_event_get(sp, &ev, 0, 0))
			return;
		if (ev.e_event == E_CHARACTER)
			break;
		if (ev.e_event == E_INTERRUPT) {
			ev.e_c = CH_QUIT;
			F_SET(gp, G_INTERRUPTED);
			break;
		}
		(void)gp->scr_bell(sp);
	}
	switch (wtype) {
	case SCROLL_W_QUIT:
		if (ev.e_c == CH_QUIT)
			F_SET(gp, G_INTERRUPTED);
		break;
	case SCROLL_W_EX:
		if (ev.e_c == ':' && continuep != NULL)
			*continuep = 1;
		break;
	case SCROLL_W:
		break;
	}
}

/*
 * vs_divider --
 *	Draw a dividing line between the screen and the output.
 */
static void
vs_divider(SCR *sp)
{
	GS *gp;
	size_t len;

#define	DIVIDESTR	"+=+=+=+=+=+=+=+"
	len =
	    sizeof(DIVIDESTR) - 1 > sp->cols ? sp->cols : sizeof(DIVIDESTR) - 1;
	gp = sp->gp;
	(void)gp->scr_attr(sp, SA_INVERSE, 1);
	(void)gp->scr_addstr(sp, DIVIDESTR, len);
	(void)gp->scr_attr(sp, SA_INVERSE, 0);
}

/*
 * vs_msgsave --
 *	Save a message for later display.
 */
static void
vs_msgsave(SCR *sp, mtype_t mt, char *p, size_t len)
{
	GS *gp;
	MSGS *mp_c, *mp_n;

	/*
	 * We have to handle messages before we have any place to put them.
	 * If there's no screen support yet, allocate a msg structure, copy
	 * in the message, and queue it on the global structure.  If we can't
	 * allocate memory here, we're genuinely screwed, dump the message
	 * to stderr in the (probably) vain hope that someone will see it.
	 */
	CALLOC_GOTO(sp, mp_n, MSGS *, 1, sizeof(MSGS));
	MALLOC_GOTO(sp, mp_n->buf, char *, len);

	memmove(mp_n->buf, p, len);
	mp_n->len = len;
	mp_n->mtype = mt;

	gp = sp->gp;
	if (SLIST_EMPTY(gp->msgq)) {
		SLIST_INSERT_HEAD(gp->msgq, mp_n, q);
	} else {
		SLIST_FOREACH(mp_c, gp->msgq, q)
			if (SLIST_NEXT(mp_c, q) == NULL)
				break;
		SLIST_INSERT_AFTER(mp_c, mp_n, q);
	}
	return;

alloc_err:
	if (mp_n != NULL)
		free(mp_n);
	(void)fprintf(stderr, "%.*s\n", (int)len, p);
}
