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
static const char sccsid[] = "$Id: ex_txt.c,v 10.23 2001/06/25 15:19:21 skimo Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/common.h"
#include "../vi/vi.h"

/*
 * !!!
 * The backslash characters was special when it preceded a newline as part of
 * a substitution replacement pattern.  For example, the input ":a\<cr>" would
 * failed immediately with an error, as the <cr> wasn't part of a substitution
 * replacement pattern.  This implies a frightening integration of the editor
 * and the parser and/or the RE engine.  There's no way I'm going to reproduce
 * those semantics.
 *
 * So, if backslashes are special, this code inserts the backslash and the next
 * character into the string, without regard for the character or the command
 * being entered.  Since "\<cr>" was illegal historically (except for the one
 * special case), and the command will fail eventually, no historical scripts
 * should break (presuming they didn't depend on the failure mode itself or the
 * characters remaining when failure occurred.
 */

static int	txt_dent(SCR *, TEXT *);
static void	txt_prompt(SCR *, TEXT *, ARG_CHAR_T, u_int32_t);

/*
 * ex_txt --
 *	Get lines from the terminal for ex.
 *
 * PUBLIC: int ex_txt(SCR *, TEXTH *, ARG_CHAR_T, u_int32_t);
 */
int
ex_txt(SCR *sp, TEXTH *tiqh, ARG_CHAR_T prompt, u_int32_t flags)
{
	EVENT ev;
	GS *gp;
	TEXT ait, *ntp, *tp;
	carat_t carat_st;
	size_t cnt;
	int rval;
	int nochange;

	rval = 0;

	/*
	 * Get a TEXT structure with some initial buffer space, reusing the
	 * last one if it's big enough.  (All TEXT bookkeeping fields default
	 * to 0 -- text_init() handles this.)
	 */
	if (!TAILQ_EMPTY(tiqh)) {
		tp = TAILQ_FIRST(tiqh);
		if (TAILQ_NEXT(tp, q) != NULL || tp->lb_len < 32) {
			text_lfree(tiqh);
			goto newtp;
		}
		tp->len = 0;
	} else {
newtp:		if ((tp = text_init(sp, NULL, 0, 32)) == NULL)
			goto err;
		TAILQ_INSERT_HEAD(tiqh, tp, q);
	}

	/* Set the starting line number. */
	tp->lno = sp->lno + 1;

	/*
	 * If it's a terminal, set up autoindent, put out the prompt, and
	 * set it up so we know we were suspended.  Otherwise, turn off
	 * the autoindent flag, as that requires less special casing below.
	 *
	 * XXX
	 * Historic practice is that ^Z suspended command mode (but, because
	 * it ran in cooked mode, it was unaffected by the autowrite option.)
	 * On restart, any "current" input was discarded, whether in insert
	 * mode or not, and ex was in command mode.  This code matches historic
	 * practice, but not 'cause it's easier.
	 */
	gp = sp->gp;
	if (F_ISSET(gp, G_SCRIPTED))
		LF_CLR(TXT_AUTOINDENT);
	else {
		if (LF_ISSET(TXT_AUTOINDENT)) {
			LF_SET(TXT_EOFCHAR);
			if (v_txt_auto(sp, sp->lno, NULL, 0, tp))
				goto err;
		}
		txt_prompt(sp, tp, prompt, flags);
	}

	for (carat_st = C_NOTSET, nochange = 0;;) {
		if (v_event_get(sp, &ev, 0, 0))
			goto err;

		/* Deal with all non-character events. */
		switch (ev.e_event) {
		case E_CHARACTER:
			break;
		case E_ERR:
			goto err;
		case E_REPAINT:
		case E_WRESIZE:
			continue;
		case E_EOF:
			rval = 1;
			/* FALLTHROUGH */
		case E_INTERRUPT:
			/*
			 * Handle EOF/SIGINT events by discarding partially
			 * entered text and returning.  EOF returns failure,
			 * E_INTERRUPT returns success.
			 */
			goto notlast;
		default:
			v_event_err(sp, &ev);
			goto notlast;
		}

		/*
		 * Deal with character events.
		 *
		 * Check to see if the character fits into the input buffer.
		 * (Use tp->len, ignore overwrite and non-printable chars.)
		 */
		BINC_GOTOW(sp, tp->lb, tp->lb_len, tp->len + 1);

		switch (ev.e_value) {
		case K_CR:
			/*
			 * !!!
			 * Historically, <carriage-return>'s in the command
			 * weren't special, so the ex parser would return an
			 * unknown command error message.  However, if they
			 * terminated the command if they were in a map.  I'm
			 * pretty sure this still isn't right, but it handles
			 * what I've seen so far.
			 */
			if (!F_ISSET(&ev.e_ch, CH_MAPPED))
				goto ins_ch;
			/* FALLTHROUGH */
		case K_NL:
			/*
			 * '\' can escape <carriage-return>/<newline>.  We
			 * don't discard the backslash because we need it
			 * to get the <newline> through the ex parser.
			 */
			if (LF_ISSET(TXT_BACKSLASH) &&
			    tp->len != 0 && tp->lb[tp->len - 1] == '\\')
				goto ins_ch;

			/*
			 * CR returns from the ex command line.
			 *
			 * XXX
			 * Terminate with a nul, needed by filter.
			 */
			if (LF_ISSET(TXT_CR)) {
				tp->lb[tp->len] = '\0';
				goto done;
			}

			/*
			 * '.' may terminate text input mode; free the current
			 * TEXT.
			 */
			if (LF_ISSET(TXT_DOTTERM) && tp->len == tp->ai + 1 &&
			    tp->lb[tp->len - 1] == '.') {
notlast:			TAILQ_REMOVE(tiqh, tp, q);
				text_free(tp);
				goto done;
			}

			/* Set up bookkeeping for the new line. */
			if ((ntp = text_init(sp, NULL, 0, 32)) == NULL)
				goto err;
			ntp->lno = tp->lno + 1;

			/*
			 * Reset the autoindent line value.  0^D keeps the ai
			 * line from changing, ^D changes the level, even if
			 * there were no characters in the old line.  Note, if
			 * using the current tp structure, use the cursor as
			 * the length, the autoindent characters may have been
			 * erased.
			 */
			if (LF_ISSET(TXT_AUTOINDENT)) {
				if (nochange) {
					nochange = 0;
					if (v_txt_auto(sp,
					    OOBLNO, &ait, ait.ai, ntp))
						goto err;
					free(ait.lb);
				} else
					if (v_txt_auto(sp,
					    OOBLNO, tp, tp->len, ntp))
						goto err;
				carat_st = C_NOTSET;
			}
			txt_prompt(sp, ntp, prompt, flags);

			/*
			 * Swap old and new TEXT's, and insert the new TEXT
			 * into the queue.
			 */
			tp = ntp;
			TAILQ_INSERT_TAIL(tiqh, tp, q);
			break;
		case K_CARAT:			/* Delete autoindent chars. */
			if (tp->len <= tp->ai && LF_ISSET(TXT_AUTOINDENT))
				carat_st = C_CARATSET;
			goto ins_ch;
		case K_ZERO:			/* Delete autoindent chars. */
			if (tp->len <= tp->ai && LF_ISSET(TXT_AUTOINDENT))
				carat_st = C_ZEROSET;
			goto ins_ch;
		case K_CNTRLD:			/* Delete autoindent char. */
			/*
			 * !!!
			 * Historically, the ^D command took (but then ignored)
			 * a count.  For simplicity, we don't return it unless
			 * it's the first character entered.  The check for len
			 * equal to 0 is okay, TXT_AUTOINDENT won't be set.
			 */
			if (LF_ISSET(TXT_CNTRLD)) {
				for (cnt = 0; cnt < tp->len; ++cnt)
					if (!isblank(tp->lb[cnt]))
						break;
				if (cnt == tp->len) {
					tp->len = 1;
					tp->lb[0] = ev.e_c;
					tp->lb[1] = '\0';

					/*
					 * Put out a line separator, in case
					 * the command fails.
					 */
					(void)putchar('\n');
					goto done;
				}
			}

			/*
			 * POSIX 1003.1b-1993, paragraph 7.1.1.9, states that
			 * the EOF characters are discarded if there are other
			 * characters to process in the line, i.e. if the EOF
			 * is not the first character in the line.  For this
			 * reason, historic ex discarded the EOF characters,
			 * even if occurring in the middle of the input line.
			 * We match that historic practice.
			 *
			 * !!!
			 * The test for discarding in the middle of the line is
			 * done in the switch, because the CARAT forms are N+1,
			 * not N.
			 *
			 * !!!
			 * There's considerable magic to make the terminal code
			 * return the EOF character at all.  See that code for
			 * details.
			 */
			if (!LF_ISSET(TXT_AUTOINDENT) || tp->len == 0)
				continue;
			switch (carat_st) {
			case C_CARATSET:		/* ^^D */
				if (tp->len > tp->ai + 1)
					continue;

				/* Save the ai string for later. */
				ait.lb = NULL;
				ait.lb_len = 0;
				BINC_GOTOW(sp, ait.lb, ait.lb_len, tp->ai);
				MEMCPY(ait.lb, tp->lb, tp->ai);
				ait.ai = ait.len = tp->ai;

				carat_st = C_NOTSET;
				nochange = 1;
				goto leftmargin;
			case C_ZEROSET:			/* 0^D */
				if (tp->len > tp->ai + 1)
					continue;

				carat_st = C_NOTSET;
leftmargin:			(void)gp->scr_ex_adjust(sp, EX_TERM_CE);
				tp->ai = tp->len = 0;
				break;
			case C_NOTSET:			/* ^D */
				if (tp->len > tp->ai)
					continue;

				if (txt_dent(sp, tp))
					goto err;
				break;
			default:
				abort();
			}

			/* Clear and redisplay the line. */
			(void)gp->scr_ex_adjust(sp, EX_TERM_CE);
			txt_prompt(sp, tp, prompt, flags);
			break;
		default:
			/*
			 * See the TXT_BEAUTIFY comment in vi/v_txt_ev.c.
			 *
			 * Silently eliminate any iscntrl() character that was
			 * not already handled specially, except for <tab> and
			 * <ff>.
			 */
ins_ch:			if (LF_ISSET(TXT_BEAUTIFY) && ISCNTRL(ev.e_c) &&
			    ev.e_value != K_FORMFEED && ev.e_value != K_TAB)
				break;

			tp->lb[tp->len++] = ev.e_c;
			break;
		}
	}
	/* NOTREACHED */

done:	return (rval);

err:	
alloc_err:
	return (1);
}

/*
 * txt_prompt --
 *	Display the ex prompt, line number, ai characters.  Characters had
 *	better be printable by the terminal driver, but that's its problem,
 *	not ours.
 */
static void
txt_prompt(SCR *sp, TEXT *tp, ARG_CHAR_T prompt, u_int32_t flags)
{
	/* Display the prompt. */
	if (LF_ISSET(TXT_PROMPT))
		(void)ex_printf(sp, "%c", prompt);

	/* Display the line number. */
	if (LF_ISSET(TXT_NUMBER) && O_ISSET(sp, O_NUMBER))
		(void)ex_printf(sp, "%6lu  ", (u_long)tp->lno);

	/* Print out autoindent string. */
	if (LF_ISSET(TXT_AUTOINDENT))
		(void)ex_printf(sp, WVS, (int)tp->ai, tp->lb);
	(void)ex_fflush(sp);
}

/*
 * txt_dent --
 *	Handle ^D outdents.
 *
 * Ex version of vi/v_ntext.c:txt_dent().  See that code for the (usual)
 * ranting and raving.  This is a fair bit simpler as ^T isn't special.
 */
static int
txt_dent(SCR *sp, TEXT *tp)
{
	u_long sw, ts;
	size_t cno, off, scno, spaces, tabs;

	ts = O_VAL(sp, O_TABSTOP);
	sw = O_VAL(sp, O_SHIFTWIDTH);

	/* Get the current screen column. */
	for (off = scno = 0; off < tp->len; ++off)
		if (tp->lb[off] == '\t')
			scno += COL_OFF(scno, ts);
		else
			++scno;

	/* Get the previous shiftwidth column. */
	cno = scno--;
	scno -= scno % sw;

	/*
	 * Since we don't know what comes before the character(s) being
	 * deleted, we have to resolve the autoindent characters .  The
	 * example is a <tab>, which doesn't take up a full shiftwidth
	 * number of columns because it's preceded by <space>s.  This is
	 * easy to get if the user sets shiftwidth to a value less than
	 * tabstop, and then uses ^T to indent, and ^D to outdent.
	 *
	 * Count up spaces/tabs needed to get to the target.
	 */
	for (cno = 0, tabs = 0; cno + COL_OFF(cno, ts) <= scno; ++tabs)
		cno += COL_OFF(cno, ts);
	spaces = scno - cno;

	/* Make sure there's enough room. */
	BINC_RETW(sp, tp->lb, tp->lb_len, tabs + spaces + 1);

	/* Adjust the final ai character count. */
	tp->ai = tabs + spaces;

	/* Enter the replacement characters. */
	for (tp->len = 0; tabs > 0; --tabs)
		tp->lb[tp->len++] = '\t';
	for (; spaces > 0; --spaces)
		tp->lb[tp->len++] = ' ';
	return (0);
}
