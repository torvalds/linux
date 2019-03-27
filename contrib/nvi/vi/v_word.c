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
static const char sccsid[] = "$Id: v_word.c,v 10.7 2011/12/27 00:49:31 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>

#include "../common/common.h"
#include "vi.h"

/*
 * There are two types of "words".  Bigwords are easy -- groups of anything
 * delimited by whitespace.  Normal words are trickier.  They are either a
 * group of characters, numbers and underscores, or a group of anything but,
 * delimited by whitespace.  When for a word, if you're in whitespace, it's
 * easy, just remove the whitespace and go to the beginning or end of the
 * word.  Otherwise, figure out if the next character is in a different group.
 * If it is, go to the beginning or end of that group, otherwise, go to the
 * beginning or end of the current group.  The historic version of vi didn't
 * get this right, so, for example, there were cases where "4e" was not the
 * same as "eeee" -- in particular, single character words, and commands that
 * began in whitespace were almost always handled incorrectly.  To get it right
 * you have to resolve the cursor after each search so that the look-ahead to
 * figure out what type of "word" the cursor is in will be correct.
 *
 * Empty lines, and lines that consist of only white-space characters count
 * as a single word, and the beginning and end of the file counts as an
 * infinite number of words.
 *
 * Movements associated with commands are different than movement commands.
 * For example, in "abc  def", with the cursor on the 'a', "cw" is from
 * 'a' to 'c', while "w" is from 'a' to 'd'.  In general, trailing white
 * space is discarded from the change movement.  Another example is that,
 * in the same string, a "cw" on any white space character replaces that
 * single character, and nothing else.  Ain't nothin' in here that's easy.
 *
 * One historic note -- in the original vi, the 'w', 'W' and 'B' commands
 * would treat groups of empty lines as individual words, i.e. the command
 * would move the cursor to each new empty line.  The 'e' and 'E' commands
 * would treat groups of empty lines as a single word, i.e. the first use
 * would move past the group of lines.  The 'b' command would just beep at
 * you, or, if you did it from the start of the line as part of a motion
 * command, go absolutely nuts.  If the lines contained only white-space
 * characters, the 'w' and 'W' commands would just beep at you, and the 'B',
 * 'b', 'E' and 'e' commands would treat the group as a single word, and
 * the 'B' and 'b' commands will treat the lines as individual words.  This
 * implementation treats all of these cases as a single white-space word.
 */

enum which {BIGWORD, LITTLEWORD};

static int bword(SCR *, VICMD *, enum which);
static int eword(SCR *, VICMD *, enum which);
static int fword(SCR *, VICMD *, enum which);

/*
 * v_wordW -- [count]W
 *	Move forward a bigword at a time.
 *
 * PUBLIC: int v_wordW(SCR *, VICMD *);
 */
int
v_wordW(SCR *sp, VICMD *vp)
{
	return (fword(sp, vp, BIGWORD));
}

/*
 * v_wordw -- [count]w
 *	Move forward a word at a time.
 *
 * PUBLIC: int v_wordw(SCR *, VICMD *);
 */
int
v_wordw(SCR *sp, VICMD *vp)
{
	return (fword(sp, vp, LITTLEWORD));
}

/*
 * fword --
 *	Move forward by words.
 */
static int
fword(SCR *sp, VICMD *vp, enum which type)
{
	enum { INWORD, NOTWORD } state;
	VCS cs;
	u_long cnt;

	cnt = F_ISSET(vp, VC_C1SET) ? vp->count : 1;
	cs.cs_lno = vp->m_start.lno;
	cs.cs_cno = vp->m_start.cno;
	if (cs_init(sp, &cs))
		return (1);

	/*
	 * If in white-space:
	 *	If the count is 1, and it's a change command, we're done.
	 *	Else, move to the first non-white-space character, which
	 *	counts as a single word move.  If it's a motion command,
	 *	don't move off the end of the line.
	 */
	if (cs.cs_flags == CS_EMP || (cs.cs_flags == 0 && ISBLANK(cs.cs_ch))) {
		if (ISMOTION(vp) && cs.cs_flags != CS_EMP && cnt == 1) {
			if (ISCMD(vp->rkp, 'c'))
				return (0);
			if (ISCMD(vp->rkp, 'd') || ISCMD(vp->rkp, 'y')) {
				if (cs_fspace(sp, &cs))
					return (1);
				goto ret;
			}
		}
		if (cs_fblank(sp, &cs))
			return (1);
		--cnt;
	}

	/*
	 * Cyclically move to the next word -- this involves skipping
	 * over word characters and then any trailing non-word characters.
	 * Note, for the 'w' command, the definition of a word keeps
	 * switching.
	 */
	if (type == BIGWORD)
		while (cnt--) {
			for (;;) {
				if (cs_next(sp, &cs))
					return (1);
				if (cs.cs_flags == CS_EOF)
					goto ret;
				if (cs.cs_flags != 0 || ISBLANK(cs.cs_ch))
					break;
			}
			/*
			 * If a motion command and we're at the end of the
			 * last word, we're done.  Delete and yank eat any
			 * trailing blanks, but we don't move off the end
			 * of the line regardless.
			 */
			if (cnt == 0 && ISMOTION(vp)) {
				if ((ISCMD(vp->rkp, 'd') ||
				    ISCMD(vp->rkp, 'y')) &&
				    cs_fspace(sp, &cs))
					return (1);
				break;
			}

			/* Eat whitespace characters. */
			if (cs_fblank(sp, &cs))
				return (1);
			if (cs.cs_flags == CS_EOF)
				goto ret;
		}
	else
		while (cnt--) {
			state = cs.cs_flags == 0 &&
			    inword(cs.cs_ch) ? INWORD : NOTWORD;
			for (;;) {
				if (cs_next(sp, &cs))
					return (1);
				if (cs.cs_flags == CS_EOF)
					goto ret;
				if (cs.cs_flags != 0 || ISBLANK(cs.cs_ch))
					break;
				if (state == INWORD) {
					if (!inword(cs.cs_ch))
						break;
				} else
					if (inword(cs.cs_ch))
						break;
			}
			/* See comment above. */
			if (cnt == 0 && ISMOTION(vp)) {
				if ((ISCMD(vp->rkp, 'd') ||
				    ISCMD(vp->rkp, 'y')) &&
				    cs_fspace(sp, &cs))
					return (1);
				break;
			}

			/* Eat whitespace characters. */
			if (cs.cs_flags != 0 || ISBLANK(cs.cs_ch))
				if (cs_fblank(sp, &cs))
					return (1);
			if (cs.cs_flags == CS_EOF)
				goto ret;
		}

	/*
	 * If we didn't move, we must be at EOF.
	 *
	 * !!!
	 * That's okay for motion commands, however.
	 */
ret:	if (!ISMOTION(vp) &&
	    cs.cs_lno == vp->m_start.lno && cs.cs_cno == vp->m_start.cno) {
		v_eof(sp, &vp->m_start);
		return (1);
	}

	/* Adjust the end of the range for motion commands. */
	vp->m_stop.lno = cs.cs_lno;
	vp->m_stop.cno = cs.cs_cno;
	if (ISMOTION(vp) && cs.cs_flags == 0)
		--vp->m_stop.cno;

	/*
	 * Non-motion commands move to the end of the range.  Delete
	 * and yank stay at the start, ignore others.
	 */
	vp->m_final = ISMOTION(vp) ? vp->m_start : vp->m_stop;
	return (0);
}

/*
 * v_wordE -- [count]E
 *	Move forward to the end of the bigword.
 *
 * PUBLIC: int v_wordE(SCR *, VICMD *);
 */
int
v_wordE(SCR *sp, VICMD *vp)
{
	return (eword(sp, vp, BIGWORD));
}

/*
 * v_worde -- [count]e
 *	Move forward to the end of the word.
 *
 * PUBLIC: int v_worde(SCR *, VICMD *);
 */
int
v_worde(SCR *sp, VICMD *vp)
{
	return (eword(sp, vp, LITTLEWORD));
}

/*
 * eword --
 *	Move forward to the end of the word.
 */
static int
eword(SCR *sp, VICMD *vp, enum which type)
{
	enum { INWORD, NOTWORD } state;
	VCS cs;
	u_long cnt;

	cnt = F_ISSET(vp, VC_C1SET) ? vp->count : 1;
	cs.cs_lno = vp->m_start.lno;
	cs.cs_cno = vp->m_start.cno;
	if (cs_init(sp, &cs))
		return (1);

	/*
	 * !!!
	 * If in whitespace, or the next character is whitespace, move past
	 * it.  (This doesn't count as a word move.)  Stay at the character
	 * past the current one, it sets word "state" for the 'e' command.
	 */
	if (cs.cs_flags == 0 && !ISBLANK(cs.cs_ch)) {
		if (cs_next(sp, &cs))
			return (1);
		if (cs.cs_flags == 0 && !ISBLANK(cs.cs_ch))
			goto start;
	}
	if (cs_fblank(sp, &cs))
		return (1);

	/*
	 * Cyclically move to the next word -- this involves skipping
	 * over word characters and then any trailing non-word characters.
	 * Note, for the 'e' command, the definition of a word keeps
	 * switching.
	 */
start:	if (type == BIGWORD)
		while (cnt--) {
			for (;;) {
				if (cs_next(sp, &cs))
					return (1);
				if (cs.cs_flags == CS_EOF)
					goto ret;
				if (cs.cs_flags != 0 || ISBLANK(cs.cs_ch))
					break;
			}
			/*
			 * When we reach the start of the word after the last
			 * word, we're done.  If we changed state, back up one
			 * to the end of the previous word.
			 */
			if (cnt == 0) {
				if (cs.cs_flags == 0 && cs_prev(sp, &cs))
					return (1);
				break;
			}

			/* Eat whitespace characters. */
			if (cs_fblank(sp, &cs))
				return (1);
			if (cs.cs_flags == CS_EOF)
				goto ret;
		}
	else
		while (cnt--) {
			state = cs.cs_flags == 0 &&
			    inword(cs.cs_ch) ? INWORD : NOTWORD;
			for (;;) {
				if (cs_next(sp, &cs))
					return (1);
				if (cs.cs_flags == CS_EOF)
					goto ret;
				if (cs.cs_flags != 0 || ISBLANK(cs.cs_ch))
					break;
				if (state == INWORD) {
					if (!inword(cs.cs_ch))
						break;
				} else
					if (inword(cs.cs_ch))
						break;
			}
			/* See comment above. */
			if (cnt == 0) {
				if (cs.cs_flags == 0 && cs_prev(sp, &cs))
					return (1);
				break;
			}

			/* Eat whitespace characters. */
			if (cs.cs_flags != 0 || ISBLANK(cs.cs_ch))
				if (cs_fblank(sp, &cs))
					return (1);
			if (cs.cs_flags == CS_EOF)
				goto ret;
		}

	/*
	 * If we didn't move, we must be at EOF.
	 *
	 * !!!
	 * That's okay for motion commands, however.
	 */
ret:	if (!ISMOTION(vp) &&
	    cs.cs_lno == vp->m_start.lno && cs.cs_cno == vp->m_start.cno) {
		v_eof(sp, &vp->m_start);
		return (1);
	}

	/* Set the end of the range for motion commands. */
	vp->m_stop.lno = cs.cs_lno;
	vp->m_stop.cno = cs.cs_cno;

	/*
	 * Non-motion commands move to the end of the range.
	 * Delete and yank stay at the start, ignore others.
	 */
	vp->m_final = ISMOTION(vp) ? vp->m_start : vp->m_stop;
	return (0);
}

/*
 * v_WordB -- [count]B
 *	Move backward a bigword at a time.
 *
 * PUBLIC: int v_wordB(SCR *, VICMD *);
 */
int
v_wordB(SCR *sp, VICMD *vp)
{
	return (bword(sp, vp, BIGWORD));
}

/*
 * v_wordb -- [count]b
 *	Move backward a word at a time.
 *
 * PUBLIC: int v_wordb(SCR *, VICMD *);
 */
int
v_wordb(SCR *sp, VICMD *vp)
{
	return (bword(sp, vp, LITTLEWORD));
}

/*
 * bword --
 *	Move backward by words.
 */
static int
bword(SCR *sp, VICMD *vp, enum which type)
{
	enum { INWORD, NOTWORD } state;
	VCS cs;
	u_long cnt;

	cnt = F_ISSET(vp, VC_C1SET) ? vp->count : 1;
	cs.cs_lno = vp->m_start.lno;
	cs.cs_cno = vp->m_start.cno;
	if (cs_init(sp, &cs))
		return (1);

	/*
	 * !!!
	 * If in whitespace, or the previous character is whitespace, move
	 * past it.  (This doesn't count as a word move.)  Stay at the
	 * character before the current one, it sets word "state" for the
	 * 'b' command.
	 */
	if (cs.cs_flags == 0 && !ISBLANK(cs.cs_ch)) {
		if (cs_prev(sp, &cs))
			return (1);
		if (cs.cs_flags == 0 && !ISBLANK(cs.cs_ch))
			goto start;
	}
	if (cs_bblank(sp, &cs))
		return (1);

	/*
	 * Cyclically move to the beginning of the previous word -- this
	 * involves skipping over word characters and then any trailing
	 * non-word characters.  Note, for the 'b' command, the definition
	 * of a word keeps switching.
	 */
start:	if (type == BIGWORD)
		while (cnt--) {
			for (;;) {
				if (cs_prev(sp, &cs))
					return (1);
				if (cs.cs_flags == CS_SOF)
					goto ret;
				if (cs.cs_flags != 0 || ISBLANK(cs.cs_ch))
					break;
			}
			/*
			 * When we reach the end of the word before the last
			 * word, we're done.  If we changed state, move forward
			 * one to the end of the next word.
			 */
			if (cnt == 0) {
				if (cs.cs_flags == 0 && cs_next(sp, &cs))
					return (1);
				break;
			}

			/* Eat whitespace characters. */
			if (cs_bblank(sp, &cs))
				return (1);
			if (cs.cs_flags == CS_SOF)
				goto ret;
		}
	else
		while (cnt--) {
			state = cs.cs_flags == 0 &&
			    inword(cs.cs_ch) ? INWORD : NOTWORD;
			for (;;) {
				if (cs_prev(sp, &cs))
					return (1);
				if (cs.cs_flags == CS_SOF)
					goto ret;
				if (cs.cs_flags != 0 || ISBLANK(cs.cs_ch))
					break;
				if (state == INWORD) {
					if (!inword(cs.cs_ch))
						break;
				} else
					if (inword(cs.cs_ch))
						break;
			}
			/* See comment above. */
			if (cnt == 0) {
				if (cs.cs_flags == 0 && cs_next(sp, &cs))
					return (1);
				break;
			}

			/* Eat whitespace characters. */
			if (cs.cs_flags != 0 || ISBLANK(cs.cs_ch))
				if (cs_bblank(sp, &cs))
					return (1);
			if (cs.cs_flags == CS_SOF)
				goto ret;
		}

	/* If we didn't move, we must be at SOF. */
ret:	if (cs.cs_lno == vp->m_start.lno && cs.cs_cno == vp->m_start.cno) {
		v_sof(sp, &vp->m_start);
		return (1);
	}

	/* Set the end of the range for motion commands. */
	vp->m_stop.lno = cs.cs_lno;
	vp->m_stop.cno = cs.cs_cno;

	/*
	 * All commands move to the end of the range.  Motion commands
	 * adjust the starting point to the character before the current
	 * one.
	 *
	 * !!!
	 * The historic vi didn't get this right -- the `yb' command yanked
	 * the right stuff and even updated the cursor value, but the cursor
	 * was not actually updated on the screen.
	 */
	vp->m_final = vp->m_stop;
	if (ISMOTION(vp))
		--vp->m_start.cno;
	return (0);
}
