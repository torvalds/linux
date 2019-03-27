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
static const char sccsid[] = "$Id: vi.c,v 10.61 2011/12/21 13:08:30 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/common.h"
#include "vi.h"

typedef enum {
	GC_ERR, GC_ERR_NOFLUSH, GC_EVENT, GC_FATAL, GC_INTERRUPT, GC_OK
} gcret_t;

static VIKEYS const
	       *v_alias(SCR *, VICMD *, VIKEYS const *);
static gcret_t	v_cmd(SCR *, VICMD *, VICMD *, VICMD *, int *, int *);
static int	v_count(SCR *, ARG_CHAR_T, u_long *);
static void	v_dtoh(SCR *);
static int	v_init(SCR *);
static gcret_t	v_key(SCR *, int, EVENT *, u_int32_t);
static int	v_motion(SCR *, VICMD *, VICMD *, int *);

#if defined(DEBUG) && defined(COMLOG)
static void	v_comlog(SCR *, VICMD *);
#endif

/*
 * Side-effect:
 *	The dot structure can be set by the underlying vi functions,
 *	see v_Put() and v_put().
 */
#define	DOT		(&VIP(sp)->sdot)
#define	DOTMOTION	(&VIP(sp)->sdotmotion)

/*
 * vi --
 * 	Main vi command loop.
 *
 * PUBLIC: int vi(SCR **);
 */
int
vi(SCR **spp)
{
	GS *gp;
	MARK abs;
	SCR *next, *sp;
	VICMD cmd = { 0 }, *vp;
	VI_PRIVATE *vip;
	int comcount, mapped, rval;

	/* Get the first screen. */
	sp = *spp;
	gp = sp->gp;

	/* Point to the command structure. */
	vp = &cmd;

	/* Reset strange attraction. */
	F_SET(vp, VM_RCM_SET);

	/* Initialize the vi screen. */
	if (v_init(sp))
		return (1);

	/* Set the focus. */
	(void)sp->gp->scr_rename(sp, sp->frp->name, 1);

	for (vip = VIP(sp), rval = 0;;) {
		/* Resolve messages. */
		if (!MAPPED_KEYS_WAITING(sp) && vs_resolve(sp, NULL, 0))
			goto ret;

		/*
		 * If not skipping a refresh, return to command mode and
		 * refresh the screen.
		 */
		if (F_ISSET(vip, VIP_S_REFRESH))
			F_CLR(vip, VIP_S_REFRESH);
		else {
			sp->showmode = SM_COMMAND;
			if (vs_refresh(sp, 0))
				goto ret;
		}

		/* Set the new favorite position. */
		if (F_ISSET(vp, VM_RCM_SET | VM_RCM_SETFNB | VM_RCM_SETNNB)) {
			F_CLR(vip, VIP_RCM_LAST);
			(void)vs_column(sp, &sp->rcm);
		}

		/*
		 * If not currently in a map, log the cursor position,
		 * and set a flag so that this command can become the
		 * DOT command.
		 */
		if (MAPPED_KEYS_WAITING(sp))
			mapped = 1;
		else {
			if (log_cursor(sp))
				goto err;
			mapped = 0;
		}

		/*
		 * There may be an ex command waiting, and we returned here
		 * only because we exited a screen or file.  In this case,
		 * we simply go back into the ex parser.
		 */
		if (EXCMD_RUNNING(gp)) {
			vp->kp = &vikeys[':'];
			goto ex_continue;
		}

		/* Refresh the command structure. */
		memset(vp, 0, sizeof(VICMD));

		/*
		 * We get a command, which may or may not have an associated
		 * motion.  If it does, we get it too, calling its underlying
		 * function to get the resulting mark.  We then call the
		 * command setting the cursor to the resulting mark.
		 *
		 * !!!
		 * Vi historically flushed mapped characters on error, but
		 * entering extra <escape> characters at the beginning of
		 * a map wasn't considered an error -- in fact, users would
		 * put leading <escape> characters in maps to clean up vi
		 * state before the map was interpreted.  Beauty!
		 */
		switch (v_cmd(sp, DOT, vp, NULL, &comcount, &mapped)) {
		case GC_ERR:
			goto err;
		case GC_ERR_NOFLUSH:
			goto gc_err_noflush;
		case GC_EVENT:
			goto gc_event;
		case GC_FATAL:
			goto ret;
		case GC_INTERRUPT:
			goto intr;
		case GC_OK:
			break;
		}

		/* Check for security setting. */
		if (F_ISSET(vp->kp, V_SECURE) && O_ISSET(sp, O_SECURE)) {
			ex_emsg(sp, KEY_NAME(sp, vp->key), EXM_SECURE);
			goto err;
		}

		/*
		 * Historical practice: if a dot command gets a new count,
		 * any motion component goes away, i.e. "d3w2." deletes a
		 * total of 5 words.
		 */
		if (F_ISSET(vp, VC_ISDOT) && comcount)
			DOTMOTION->count = 1;

		/* Copy the key flags into the local structure. */
		F_SET(vp, vp->kp->flags);

		/* Prepare to set the previous context. */
		if (F_ISSET(vp, V_ABS | V_ABS_C | V_ABS_L)) {
			abs.lno = sp->lno;
			abs.cno = sp->cno;
		}

		/*
		 * Set the three cursor locations to the current cursor.  The
		 * underlying routines don't bother if the cursor doesn't move.
		 * This also handles line commands (e.g. Y) defaulting to the
		 * current line.
		 */
		vp->m_start.lno = vp->m_stop.lno = vp->m_final.lno = sp->lno;
		vp->m_start.cno = vp->m_stop.cno = vp->m_final.cno = sp->cno;

		/*
		 * Do any required motion; v_motion sets the from MARK and the
		 * line mode flag, as well as the VM_RCM flags.
		 */
		if (F_ISSET(vp, V_MOTION) &&
		    v_motion(sp, DOTMOTION, vp, &mapped)) {
			if (INTERRUPTED(sp))
				goto intr;
			goto err;
		}

		/*
		 * If a count is set and the command is line oriented, set the
		 * to MARK here relative to the cursor/from MARK.  This is for
		 * commands that take both counts and motions, i.e. "4yy" and
		 * "y%".  As there's no way the command can know which the user
		 * did, we have to do it here.  (There are commands that are
		 * line oriented and that take counts ("#G", "#H"), for which
		 * this calculation is either completely meaningless or wrong.
		 * Each command must validate the value for itself.
		 */
		if (F_ISSET(vp, VC_C1SET) && F_ISSET(vp, VM_LMODE))
			vp->m_stop.lno += vp->count - 1;

		/* Increment the command count. */
		++sp->ccnt;

#if defined(DEBUG) && defined(COMLOG)
		v_comlog(sp, vp);
#endif
		/* Call the function. */
ex_continue:	if (vp->kp->func(sp, vp))
			goto err;
gc_event:
#ifdef DEBUG
		/* Make sure no function left the temporary space locked. */
		if (F_ISSET(gp, G_TMP_INUSE)) {
			F_CLR(gp, G_TMP_INUSE);
			msgq(sp, M_ERR,
			    "232|vi: temporary buffer not released");
		}
#endif
		/*
		 * If we're exiting this screen, move to the next one, or, if
		 * there aren't any more, return to the main editor loop.  The
		 * ordering is careful, don't discard the contents of sp until
		 * the end.
		 */
		if (F_ISSET(sp, SC_EXIT | SC_EXIT_FORCE)) {
			if (file_end(sp, NULL, F_ISSET(sp, SC_EXIT_FORCE)))
				goto ret;
			if (vs_discard(sp, &next))
				goto ret;
			if (next == NULL && vs_swap(sp, &next, NULL))
				goto ret;
			*spp = next;
			if (screen_end(sp))
				goto ret;
			if (next == NULL)
				break;

			/* Switch screens, change focus. */
			sp = next;
			vip = VIP(sp);
			(void)sp->gp->scr_rename(sp, sp->frp->name, 1);

			/* Don't trust the cursor. */
			F_SET(vip, VIP_CUR_INVALID);

			continue;
		}

		/*
		 * Set the dot command structure.
		 *
		 * !!!
		 * Historically, commands which used mapped keys did not
		 * set the dot command, with the exception of the text
		 * input commands.
		 */
		if (F_ISSET(vp, V_DOT) && !mapped) {
			*DOT = cmd;
			F_SET(DOT, VC_ISDOT);

			/*
			 * If a count was supplied for both the command and
			 * its motion, the count was used only for the motion.
			 * Turn the count back on for the dot structure.
			 */
			if (F_ISSET(vp, VC_C1RESET))
				F_SET(DOT, VC_C1SET);

			/* VM flags aren't retained. */
			F_CLR(DOT, VM_COMMASK | VM_RCM_MASK);
		}

		/*
		 * Some vi row movements are "attracted" to the last position
		 * set, i.e. the VM_RCM commands are moths to the VM_RCM_SET
		 * commands' candle.  If the movement is to the EOL the vi
		 * command handles it.  If it's to the beginning, we handle it
		 * here.
		 *
		 * Note, some commands (e.g. _, ^) don't set the VM_RCM_SETFNB
		 * flag, but do the work themselves.  The reason is that they
		 * have to modify the column in case they're being used as a
		 * motion component.  Other similar commands (e.g. +, -) don't
		 * have to modify the column because they are always line mode
		 * operations when used as motions, so the column number isn't
		 * of any interest.
		 *
		 * Does this totally violate the screen and editor layering?
		 * You betcha.  As they say, if you think you understand it,
		 * you don't.
		 */
		switch (F_ISSET(vp, VM_RCM_MASK)) {
		case 0:
		case VM_RCM_SET:
			break;
		case VM_RCM:
			vp->m_final.cno = vs_rcm(sp,
			    vp->m_final.lno, F_ISSET(vip, VIP_RCM_LAST));
			break;
		case VM_RCM_SETLAST:
			F_SET(vip, VIP_RCM_LAST);
			break;
		case VM_RCM_SETFNB:
			vp->m_final.cno = 0;
			/* FALLTHROUGH */
		case VM_RCM_SETNNB:
			if (nonblank(sp, vp->m_final.lno, &vp->m_final.cno))
				goto err;
			break;
		default:
			abort();
		}

		/* Update the cursor. */
		sp->lno = vp->m_final.lno;
		sp->cno = vp->m_final.cno;

		/*
		 * Set the absolute mark -- set even if a tags or similar
		 * command, since the tag may be moving to the same file.
		 */
		if ((F_ISSET(vp, V_ABS) ||
		    (F_ISSET(vp, V_ABS_L) && sp->lno != abs.lno) ||
		    (F_ISSET(vp, V_ABS_C) &&
		    (sp->lno != abs.lno || sp->cno != abs.cno))) &&
		    mark_set(sp, ABSMARK1, &abs, 1))
			goto err;

		if (0) {
err:			if (v_event_flush(sp, CH_MAPPED))
				msgq(sp, M_BERR,
			    "110|Vi command failed: mapped keys discarded");
		}

		/*
		 * Check and clear interrupts.  There's an obvious race, but
		 * it's not worth fixing.
		 */
gc_err_noflush:	if (INTERRUPTED(sp)) {
intr:			CLR_INTERRUPT(sp);
			if (v_event_flush(sp, CH_MAPPED))
				msgq(sp, M_ERR,
				    "231|Interrupted: mapped keys discarded");
			else
				msgq(sp, M_ERR, "236|Interrupted");
		}

		/* If the last command switched screens, update. */
		if (F_ISSET(sp, SC_SSWITCH)) {
			F_CLR(sp, SC_SSWITCH);

			/*
			 * If the current screen is still displayed, it will
			 * need a new status line.
			 */
			F_SET(sp, SC_STATUS);

			/* Switch screens, change focus. */
			sp = sp->nextdisp;
			vip = VIP(sp);
			(void)sp->gp->scr_rename(sp, sp->frp->name, 1);

			/* Don't trust the cursor. */
			F_SET(vip, VIP_CUR_INVALID);

			/* Refresh so we can display messages. */
			if (vs_refresh(sp, 1))
				return (1);
		}

		/* If the last command switched files, change focus. */
		if (F_ISSET(sp, SC_FSWITCH)) {
			F_CLR(sp, SC_FSWITCH);
			(void)sp->gp->scr_rename(sp, sp->frp->name, 1);
		}

		/* If leaving vi, return to the main editor loop. */
		if (F_ISSET(gp, G_SRESTART) || F_ISSET(sp, SC_EX)) {
			*spp = sp;
			v_dtoh(sp);
			gp->scr_discard(sp, NULL);
			break;
		}
	}
	if (0)
ret:		rval = 1;
	return (rval);
}

#define	KEY(key, ec_flags) {						\
	if ((gcret = v_key(sp, 0, &ev, ec_flags)) != GC_OK)		\
		return (gcret);						\
	if (ev.e_value == K_ESCAPE)					\
		goto esc;						\
	if (F_ISSET(&ev.e_ch, CH_MAPPED))				\
		*mappedp = 1;						\
	key = ev.e_c;							\
}

/*
 * The O_TILDEOP option makes the ~ command take a motion instead
 * of a straight count.  This is the replacement structure we use
 * instead of the one currently in the VIKEYS table.
 *
 * XXX
 * This should probably be deleted -- it's not all that useful, and
 * we get help messages wrong.
 */
VIKEYS const tmotion = {
	v_mulcase,	V_CNT|V_DOT|V_MOTION|VM_RCM_SET,
	"[count]~[count]motion",
	" ~ change case to motion"
};

/*
 * v_cmd --
 *
 * The command structure for vi is less complex than ex (and don't think
 * I'm not grateful!)  The command syntax is:
 *
 *	[count] [buffer] [count] key [[motion] | [buffer] [character]]
 *
 * and there are several special cases.  The motion value is itself a vi
 * command, with the syntax:
 *
 *	[count] key [character]
 */
static gcret_t
v_cmd(
	SCR *sp,
	VICMD *dp,
	VICMD *vp,
	VICMD *ismotion,	/* Previous key if getting motion component. */
	int *comcountp,
	int *mappedp)
{
	enum { COMMANDMODE, ISPARTIAL, NOTPARTIAL } cpart;
	EVENT ev;
	VIKEYS const *kp;
	gcret_t gcret;
	u_int flags;
	CHAR_T key;
	char *s;

	/*
	 * Get a key.
	 *
	 * <escape> cancels partial commands, i.e. a command where at least
	 * one non-numeric character has been entered.  Otherwise, it beeps
	 * the terminal.
	 *
	 * !!!
	 * POSIX 1003.2-1992 explicitly disallows cancelling commands where
	 * all that's been entered is a number, requiring that the terminal
	 * be alerted.
	 */
	cpart = ismotion == NULL ? COMMANDMODE : ISPARTIAL;
	if ((gcret =
	    v_key(sp, ismotion == NULL, &ev, EC_MAPCOMMAND)) != GC_OK) {
		if (gcret == GC_EVENT)
			vp->ev = ev;
		return (gcret);
	}
	if (ev.e_value == K_ESCAPE)
		goto esc;
	if (F_ISSET(&ev.e_ch, CH_MAPPED))
		*mappedp = 1;
	key = ev.e_c;

	if (ismotion == NULL)
		cpart = NOTPARTIAL;

	/* Pick up an optional buffer. */
	if (key == '"') {
		cpart = ISPARTIAL;
		if (ismotion != NULL) {
			v_emsg(sp, NULL, VIM_COMBUF);
			return (GC_ERR);
		}
		KEY(vp->buffer, 0);
		F_SET(vp, VC_BUFFER);

		KEY(key, EC_MAPCOMMAND);
	}

	/*
	 * Pick up an optional count, where a leading 0 is not a count,
	 * it's a command.
	 */
	if (ISDIGIT(key) && key != '0') {
		if (v_count(sp, key, &vp->count))
			return (GC_ERR);
		F_SET(vp, VC_C1SET);
		*comcountp = 1;

		KEY(key, EC_MAPCOMMAND);
	} else
		*comcountp = 0;

	/* Pick up optional buffer. */
	if (key == '"') {
		cpart = ISPARTIAL;
		if (F_ISSET(vp, VC_BUFFER)) {
			msgq(sp, M_ERR, "234|Only one buffer may be specified");
			return (GC_ERR);
		}
		if (ismotion != NULL) {
			v_emsg(sp, NULL, VIM_COMBUF);
			return (GC_ERR);
		}
		KEY(vp->buffer, 0);
		F_SET(vp, VC_BUFFER);

		KEY(key, EC_MAPCOMMAND);
	}

	/* Check for an OOB command key. */
	cpart = ISPARTIAL;
	if (key > MAXVIKEY) {
		v_emsg(sp, KEY_NAME(sp, key), VIM_NOCOM);
		return (GC_ERR);
	}
	kp = &vikeys[vp->key = key];

	/*
	 * !!!
	 * Historically, D accepted and then ignored a count.  Match it.
	 */
	if (vp->key == 'D' && F_ISSET(vp, VC_C1SET)) {
		*comcountp = 0;
		vp->count = 0;
		F_CLR(vp, VC_C1SET);
	}

	/* Check for command aliases. */
	if (kp->func == NULL && (kp = v_alias(sp, vp, kp)) == NULL)
		return (GC_ERR);

	/* The tildeop option makes the ~ command take a motion. */
	if (key == '~' && O_ISSET(sp, O_TILDEOP))
		kp = &tmotion;

	vp->kp = kp;

	/*
	 * Find the command.  The only legal command with no underlying
	 * function is dot.  It's historic practice that <escape> doesn't
	 * just erase the preceding number, it beeps the terminal as well.
	 * It's a common problem, so just beep the terminal unless verbose
	 * was set.
	 */
	if (kp->func == NULL) {
		if (key != '.') {
			v_emsg(sp, KEY_NAME(sp, key),
			    ev.e_value == K_ESCAPE ? VIM_NOCOM_B : VIM_NOCOM);
			return (GC_ERR);
		}

		/* If called for a motion command, stop now. */
		if (dp == NULL)
			goto usage;

		/*
		 * !!!
		 * If a '.' is immediately entered after an undo command, we
		 * replay the log instead of redoing the last command.  This
		 * is necessary because 'u' can't set the dot command -- see
		 * vi/v_undo.c:v_undo for details.
		 */
		if (VIP(sp)->u_ccnt == sp->ccnt) {
			vp->kp = &vikeys['u'];
			F_SET(vp, VC_ISDOT);
			return (GC_OK);
		}

		/* Otherwise, a repeatable command must have been executed. */
		if (!F_ISSET(dp, VC_ISDOT)) {
			msgq(sp, M_ERR, "208|No command to repeat");
			return (GC_ERR);
		}

		/* Set new count/buffer, if any, and return. */
		if (F_ISSET(vp, VC_C1SET)) {
			F_SET(dp, VC_C1SET);
			dp->count = vp->count;
		}
		if (F_ISSET(vp, VC_BUFFER))
			dp->buffer = vp->buffer;

		*vp = *dp;
		return (GC_OK);
	}

	/* Set the flags based on the command flags. */
	flags = kp->flags;

	/* Check for illegal count. */
	if (F_ISSET(vp, VC_C1SET) && !LF_ISSET(V_CNT))
		goto usage;

	/* Illegal motion command. */
	if (ismotion == NULL) {
		/* Illegal buffer. */
		if (!LF_ISSET(V_OBUF) && F_ISSET(vp, VC_BUFFER))
			goto usage;

		/* Required buffer. */
		if (LF_ISSET(V_RBUF)) {
			KEY(vp->buffer, 0);
			F_SET(vp, VC_BUFFER);
		}
	}

	/*
	 * Special case: '[', ']' and 'Z' commands.  Doesn't the fact that
	 * the *single* characters don't mean anything but the *doubled*
	 * characters do, just frost your shorts?
	 */
	if (vp->key == '[' || vp->key == ']' || vp->key == 'Z') {
		/*
		 * Historically, half entered [[, ]] or Z commands weren't
		 * cancelled by <escape>, the terminal was beeped instead.
		 * POSIX.2-1992 probably didn't notice, and requires that
		 * they be cancelled instead of beeping.  Seems fine to me.
		 *
		 * Don't set the EC_MAPCOMMAND flag, apparently ] is a popular
		 * vi meta-character, and we don't want the user to wait while
		 * we time out a possible mapping.  This *appears* to match
		 * historic vi practice, but with mapping characters, You Just
		 * Never Know.
		 */
		KEY(key, 0);

		if (vp->key != key) {
usage:			if (ismotion == NULL)
				s = kp->usage;
			else if (ismotion->key == '~' && O_ISSET(sp, O_TILDEOP))
				s = tmotion.usage;
			else
				s = vikeys[ismotion->key].usage;
			v_emsg(sp, s, VIM_USAGE);
			return (GC_ERR);
		}
	}
	/* Special case: 'z' command. */
	if (vp->key == 'z') {
		KEY(vp->character, 0);
		if (ISDIGIT(vp->character)) {
			if (v_count(sp, vp->character, &vp->count2))
				return (GC_ERR);
			F_SET(vp, VC_C2SET);
			KEY(vp->character, 0);
		}
	}

	/*
	 * Commands that have motion components can be doubled to imply the
	 * current line.
	 */
	if (ismotion != NULL && ismotion->key != key && !LF_ISSET(V_MOVE)) {
		msgq(sp, M_ERR, "210|%s may not be used as a motion command",
		    KEY_NAME(sp, key));
		return (GC_ERR);
	}

	/* Pick up required trailing character. */
	if (LF_ISSET(V_CHAR))
		KEY(vp->character, 0);

	/* Get any associated cursor word. */
	if (F_ISSET(kp, V_KEYW) && v_curword(sp))
		return (GC_ERR);

	return (GC_OK);

esc:	switch (cpart) {
	case COMMANDMODE:
		msgq(sp, M_BERR, "211|Already in command mode");
		return (GC_ERR_NOFLUSH);
	case ISPARTIAL:
		break;
	case NOTPARTIAL:
		(void)sp->gp->scr_bell(sp);
		break;
	}
	return (GC_ERR);
}

/*
 * v_motion --
 *
 * Get resulting motion mark.
 */
static int
v_motion(
	SCR *sp,
	VICMD *dm,
	VICMD *vp,
	int *mappedp)
{
	VICMD motion;
	size_t len;
	u_long cnt;
	u_int flags;
	int tilde_reset, notused;

	/*
	 * If '.' command, use the dot motion, else get the motion command.
	 * Clear any line motion flags, the subsequent motion isn't always
	 * the same, i.e. "/aaa" may or may not be a line motion.
	 */
	if (F_ISSET(vp, VC_ISDOT)) {
		motion = *dm;
		F_SET(&motion, VC_ISDOT);
		F_CLR(&motion, VM_COMMASK);
	} else {
		memset(&motion, 0, sizeof(VICMD));
		if (v_cmd(sp, NULL, &motion, vp, &notused, mappedp) != GC_OK)
			return (1);
	}

	/*
	 * A count may be provided both to the command and to the motion, in
	 * which case the count is multiplicative.  For example, "3y4y" is the
	 * same as "12yy".  This count is provided to the motion command and
	 * not to the regular function.
	 */
	cnt = motion.count = F_ISSET(&motion, VC_C1SET) ? motion.count : 1;
	if (F_ISSET(vp, VC_C1SET)) {
		motion.count *= vp->count;
		F_SET(&motion, VC_C1SET);

		/*
		 * Set flags to restore the original values of the command
		 * structure so dot commands can change the count values,
		 * e.g. "2dw" "3." deletes a total of five words.
		 */
		F_CLR(vp, VC_C1SET);
		F_SET(vp, VC_C1RESET);
	}

	/*
	 * Some commands can be repeated to indicate the current line.  In
	 * this case, or if the command is a "line command", set the flags
	 * appropriately.  If not a doubled command, run the function to get
	 * the resulting mark.
 	 */
	if (vp->key == motion.key) {
		F_SET(vp, VM_LDOUBLE | VM_LMODE);

		/* Set the origin of the command. */
		vp->m_start.lno = sp->lno;
		vp->m_start.cno = 0;

		/*
		 * Set the end of the command.
		 *
		 * If the current line is missing, i.e. the file is empty,
		 * historic vi permitted a "cc" or "!!" command to insert
		 * text.
		 */
		vp->m_stop.lno = sp->lno + motion.count - 1;
		if (db_get(sp, vp->m_stop.lno, 0, NULL, &len)) {
			if (vp->m_stop.lno != 1 ||
			   (vp->key != 'c' && vp->key != '!')) {
				v_emsg(sp, NULL, VIM_EMPTY);
				return (1);
			}
			vp->m_stop.cno = 0;
		} else
			vp->m_stop.cno = len ? len - 1 : 0;
	} else {
		/*
		 * Motion commands change the underlying movement (*snarl*).
		 * For example, "l" is illegal at the end of a line, but "dl"
		 * is not.  Set flags so the function knows the situation.
		 */
		motion.rkp = vp->kp;

		/*
		 * XXX
		 * Use yank instead of creating a new motion command, it's a
		 * lot easier for now.
		 */
		if (vp->kp == &tmotion) {
			tilde_reset = 1;
			vp->kp = &vikeys['y'];
		} else
			tilde_reset = 0;

		/*
		 * Copy the key flags into the local structure, except for the
		 * RCM flags -- the motion command will set the RCM flags in
		 * the vp structure if necessary.  This means that the motion
		 * command is expected to determine where the cursor ends up!
		 * However, we save off the current RCM mask and restore it if
		 * it no RCM flags are set by the motion command, with a small
		 * modification.
		 *
		 * We replace the VM_RCM_SET flag with the VM_RCM flag.  This
		 * is so that cursor movement doesn't set the relative position
		 * unless the motion command explicitly specified it.  This
		 * appears to match historic practice, but I've never been able
		 * to develop a hard-and-fast rule.
		 */
		flags = F_ISSET(vp, VM_RCM_MASK);
		if (LF_ISSET(VM_RCM_SET)) {
			LF_SET(VM_RCM);
			LF_CLR(VM_RCM_SET);
		}
		F_CLR(vp, VM_RCM_MASK);
		F_SET(&motion, motion.kp->flags & ~VM_RCM_MASK);

		/*
		 * Set the three cursor locations to the current cursor.  This
		 * permits commands like 'j' and 'k', that are line oriented
		 * motions and have special cursor suck semantics when they are
		 * used as standalone commands, to ignore column positioning.
		 */
		motion.m_final.lno =
		    motion.m_stop.lno = motion.m_start.lno = sp->lno;
		motion.m_final.cno =
		    motion.m_stop.cno = motion.m_start.cno = sp->cno;

		/* Run the function. */
		if ((motion.kp->func)(sp, &motion))
			return (1);

		/*
		 * If the current line is missing, i.e. the file is empty,
		 * historic vi allowed "c<motion>" or "!<motion>" to insert
		 * text.  Otherwise fail -- most motion commands will have
		 * already failed, but some, e.g. G, succeed in empty files.
		 */
		if (!db_exist(sp, vp->m_stop.lno)) {
			if (vp->m_stop.lno != 1 ||
			   (vp->key != 'c' && vp->key != '!')) {
				v_emsg(sp, NULL, VIM_EMPTY);
				return (1);
			}
			vp->m_stop.cno = 0;
		}

		/*
		 * XXX
		 * See above.
		 */
		if (tilde_reset)
			vp->kp = &tmotion;

		/*
		 * Copy cut buffer, line mode and cursor position information
		 * from the motion command structure, i.e. anything that the
		 * motion command can set for us.  The commands can flag the
		 * movement as a line motion (see v_sentence) as well as set
		 * the VM_RCM_* flags explicitly.
		 */
		F_SET(vp, F_ISSET(&motion, VM_COMMASK | VM_RCM_MASK));

		/*
		 * If the motion command set no relative motion flags, use
		 * the (slightly) modified previous values.
		 */
		if (!F_ISSET(vp, VM_RCM_MASK))
			F_SET(vp, flags);

		/*
		 * Commands can change behaviors based on the motion command
		 * used, for example, the ! command repeated the last bang
		 * command if N or n was used as the motion.
		 */
		vp->rkp = motion.kp;

		/*
		 * Motion commands can reset all of the cursor information.
		 * If the motion is in the reverse direction, switch the
		 * from and to MARK's so that it's in a forward direction.
		 * Motions are from the from MARK to the to MARK (inclusive).
		 */
		if (motion.m_start.lno > motion.m_stop.lno ||
		    (motion.m_start.lno == motion.m_stop.lno &&
		    motion.m_start.cno > motion.m_stop.cno)) {
			vp->m_start = motion.m_stop;
			vp->m_stop = motion.m_start;
		} else {
			vp->m_start = motion.m_start;
			vp->m_stop = motion.m_stop;
		}
		vp->m_final = motion.m_final;
	}

	/*
	 * If the command sets dot, save the motion structure.  The motion
	 * count was changed above and needs to be reset, that's why this
	 * is done here, and not in the calling routine.
	 */
	if (F_ISSET(vp->kp, V_DOT)) {
		*dm = motion;
		dm->count = cnt;
	}
	return (0);
}

/*
 * v_init --
 *	Initialize the vi screen.
 */
static int
v_init(SCR *sp)
{
	GS *gp;
	VI_PRIVATE *vip;

	gp = sp->gp;
	vip = VIP(sp);

	/* Switch into vi. */
	if (gp->scr_screen(sp, SC_VI))
		return (1);
	(void)gp->scr_attr(sp, SA_ALTERNATE, 1);

	F_CLR(sp, SC_EX | SC_SCR_EX);
	F_SET(sp, SC_VI);

	/*
	 * Initialize screen values.
	 *
	 * Small windows: see vs_refresh(), section 6a.
	 *
	 * Setup:
	 *	t_minrows is the minimum rows to display
	 *	t_maxrows is the maximum rows to display (rows - 1)
	 *	t_rows is the rows currently being displayed
	 */
	sp->rows = vip->srows = O_VAL(sp, O_LINES);
	sp->cols = O_VAL(sp, O_COLUMNS);
	sp->t_rows = sp->t_minrows = O_VAL(sp, O_WINDOW);
	if (sp->rows != 1) {
		if (sp->t_rows > sp->rows - 1) {
			sp->t_minrows = sp->t_rows = sp->rows - 1;
			msgq(sp, M_INFO,
			    "214|Windows option value is too large, max is %u",
			    (u_int)sp->t_rows);
		}
		sp->t_maxrows = sp->rows - 1;
	} else
		sp->t_maxrows = 1;
	sp->roff = sp->coff = 0;

	/* Create a screen map. */
	CALLOC_RET(sp, HMAP, SMAP *, SIZE_HMAP(sp), sizeof(SMAP));
	TMAP = HMAP + (sp->t_rows - 1);
	HMAP->lno = sp->lno;
	HMAP->coff = 0;
	HMAP->soff = 1;

	/*
	 * Fill the screen map from scratch -- try and center the line.  That
	 * way if we're starting with a file we've seen before, we'll put the
	 * line in the middle, otherwise, it won't work and we'll end up with
	 * the line at the top.
	 */
	F_SET(sp, SC_SCR_REFORMAT | SC_SCR_CENTER);

	/* Invalidate the cursor. */
	F_SET(vip, VIP_CUR_INVALID);

	/* Paint the screen image from scratch. */
	F_SET(vip, VIP_N_EX_PAINT);

	return (0);
}

/*
 * v_dtoh --
 *	Move all but the current screen to the hidden queue.
 */
static void
v_dtoh(SCR *sp)
{
	GS *gp;
	SCR *tsp;
	int hidden;

	/* Move all screens to the hidden queue, tossing screen maps. */
	for (hidden = 0, gp = sp->gp;
	    (tsp = TAILQ_FIRST(gp->dq)) != NULL; ++hidden) {
		if (_HMAP(tsp) != NULL) {
			free(_HMAP(tsp));
			_HMAP(tsp) = NULL;
		}
		TAILQ_REMOVE(gp->dq, tsp, q);
		TAILQ_INSERT_TAIL(gp->hq, tsp, q);
		/* XXXX Change if hidden screens per window */
		gp->scr_discard(tsp, NULL);
	}

	/* Move current screen back to the display queue. */
	TAILQ_REMOVE(gp->hq, sp, q);
	TAILQ_INSERT_TAIL(gp->dq, sp, q);

	if (hidden > 1)
		msgq(sp, M_INFO,
		    "319|%d screens backgrounded; use :display to list them",
		    hidden - 1);
}

/*
 * v_curword --
 *	Get the word (tagstring, actually) the cursor is on.
 *
 * PUBLIC: int v_curword(SCR *);
 */
int
v_curword(SCR *sp)
{
	VI_PRIVATE *vip;
	size_t beg, end, len;
	int moved;
	CHAR_T *p;

	if (db_get(sp, sp->lno, DBG_FATAL, &p, &len))
		return (1);

	/*
	 * !!!
	 * Historically, tag commands skipped over any leading whitespace
	 * characters.  Make this true in general when using cursor words.
	 * If movement, getting a cursor word implies moving the cursor to
	 * its beginning.  Refresh now.
	 *
	 * !!!
	 * Find the beginning/end of the keyword.  Keywords are currently
	 * used for cursor-word searching and for tags.  Historical vi
	 * only used the word in a tag search from the cursor to the end
	 * of the word, i.e. if the cursor was on the 'b' in " abc ", the
	 * tag was "bc".  For consistency, we make cursor word searches
	 * follow the same rule.
	 */
	for (moved = 0,
	    beg = sp->cno; beg < len && ISSPACE(p[beg]); moved = 1, ++beg);
	if (beg >= len) {
		msgq(sp, M_BERR, "212|Cursor not in a word");
		return (1);
	}
	if (moved) {
		sp->cno = beg;
		(void)vs_refresh(sp, 0);
	}

	/*
	 * Find the end of the word.
	 *
	 * !!!
	 * Historically, vi accepted any non-blank as initial character
	 * when building up a tagstring.  Required by IEEE 1003.1-2001.
	 */
	for (end = beg; ++end < len && inword(p[end]););

	vip = VIP(sp);
	vip->klen = len = (end - beg);
	BINC_RETW(sp, vip->keyw, vip->keywlen, len+1);
	MEMMOVE(vip->keyw, p + beg, len);
	vip->keyw[len] = '\0';				/* XXX */
	return (0);
}

/*
 * v_alias --
 *	Check for a command alias.
 */
static VIKEYS const *
v_alias(
	SCR *sp,
	VICMD *vp,
	VIKEYS const *kp)
{
	CHAR_T push;

	switch (vp->key) {
	case 'C':			/* C -> c$ */
		push = '$';
		vp->key = 'c';
		break;
	case 'D':			/* D -> d$ */
		push = '$';
		vp->key = 'd';
		break;
	case 'S':			/* S -> c_ */
		push = '_';
		vp->key = 'c';
		break;
	case 'Y':			/* Y -> y_ */
		push = '_';
		vp->key = 'y';
		break;
	default:
		return (kp);
	}
	return (v_event_push(sp,
	    NULL, &push, 1, CH_NOMAP | CH_QUOTED) ? NULL : &vikeys[vp->key]);
}

/*
 * v_count --
 *	Return the next count.
 */
static int
v_count(
	SCR *sp,
	ARG_CHAR_T fkey,
	u_long *countp)
{
	EVENT ev;
	u_long count, tc;

	ev.e_c = fkey;
	count = tc = 0;
	do {
		/*
		 * XXX
		 * Assume that overflow results in a smaller number.
		 */
		tc = count * 10 + ev.e_c - '0';
		if (count > tc) {
			/* Toss to the next non-digit. */
			do {
				if (v_key(sp, 0, &ev,
				    EC_MAPCOMMAND | EC_MAPNODIGIT) != GC_OK)
					return (1);
			} while (ISDIGIT(ev.e_c));
			msgq(sp, M_ERR,
			    "235|Number larger than %lu", ULONG_MAX);
			return (1);
		}
		count = tc;
		if (v_key(sp, 0, &ev, EC_MAPCOMMAND | EC_MAPNODIGIT) != GC_OK)
			return (1);
	} while (ISDIGIT(ev.e_c));
	*countp = count;
	return (0);
}

/*
 * v_key --
 *	Return the next event.
 */
static gcret_t
v_key(
	SCR *sp,
	int command_events,
	EVENT *evp,
	u_int32_t ec_flags)
{
	u_int32_t quote;

	for (quote = 0;;) {
		if (v_event_get(sp, evp, 0, ec_flags | quote))
			return (GC_FATAL);
		quote = 0;

		switch (evp->e_event) {
		case E_CHARACTER:
			/*
			 * !!!
			 * Historically, ^V was ignored in the command stream,
			 * although it had a useful side-effect of interrupting
			 * mappings.  Adding a quoting bit to the call probably
			 * extends historic practice, but it feels right.
			 */
			if (evp->e_value == K_VLNEXT) {
				quote = EC_QUOTED;
				break;
			}
			return (GC_OK);
		case E_ERR:
		case E_EOF:
			return (GC_FATAL);
		case E_INTERRUPT:
			/*
			 * !!!
			 * Historically, vi beeped on command level interrupts.
			 *
			 * Historically, vi exited to ex mode if no file was
			 * named on the command line, and two interrupts were
			 * generated in a row.  (Just figured you might want
			 * to know that.)
			 */
			(void)sp->gp->scr_bell(sp);
			return (GC_INTERRUPT);
		case E_REPAINT:
			if (vs_repaint(sp, evp))
				return (GC_FATAL);
			break;
		case E_WRESIZE:
			return (GC_ERR);
		default:
			v_event_err(sp, evp);
			return (GC_ERR);
		}
	}
	/* NOTREACHED */
}

#if defined(DEBUG) && defined(COMLOG)
/*
 * v_comlog --
 *	Log the contents of the command structure.
 */
static void
v_comlog(
	SCR *sp,
	VICMD *vp)
{
	TRACE(sp, "vcmd: "WC, vp->key);
	if (F_ISSET(vp, VC_BUFFER))
		TRACE(sp, " buffer: "WC, vp->buffer);
	if (F_ISSET(vp, VC_C1SET))
		TRACE(sp, " c1: %lu", vp->count);
	if (F_ISSET(vp, VC_C2SET))
		TRACE(sp, " c2: %lu", vp->count2);
	TRACE(sp, " flags: 0x%x\n", vp->flags);
}
#endif
