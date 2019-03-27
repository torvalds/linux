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
static const char sccsid[] = "$Id: v_txt.c,v 11.5 2013/05/19 20:37:45 bentley Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>

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

static int	 txt_abbrev(SCR *, TEXT *, CHAR_T *, int, int *, int *);
static void	 txt_ai_resolve(SCR *, TEXT *, int *);
static TEXT	*txt_backup(SCR *, TEXTH *, TEXT *, u_int32_t *);
static int	 txt_dent(SCR *, TEXT *, int);
static int	 txt_emark(SCR *, TEXT *, size_t);
static void	 txt_err(SCR *, TEXTH *);
static int	 txt_fc(SCR *, TEXT *, int *);
static int	 txt_fc_col(SCR *, int, ARGS **);
static int	 txt_hex(SCR *, TEXT *);
static int	 txt_insch(SCR *, TEXT *, CHAR_T *, u_int);
static int	 txt_isrch(SCR *, VICMD *, TEXT *, u_int8_t *);
static int	 txt_map_end(SCR *);
static int	 txt_map_init(SCR *);
static int	 txt_margin(SCR *, TEXT *, TEXT *, int *, u_int32_t);
static void	 txt_nomorech(SCR *);
static void	 txt_Rresolve(SCR *, TEXTH *, TEXT *, const size_t);
static int	 txt_resolve(SCR *, TEXTH *, u_int32_t);
static int	 txt_showmatch(SCR *, TEXT *);
static void	 txt_unmap(SCR *, TEXT *, u_int32_t *);

/* Cursor character (space is hard to track on the screen). */
#if defined(DEBUG) && 0
#undef	CH_CURSOR
#define	CH_CURSOR	'+'
#endif

/*
 * v_tcmd --
 *	Fill a buffer from the terminal for vi.
 *
 * PUBLIC: int v_tcmd(SCR *, VICMD *, ARG_CHAR_T, u_int);
 */
int
v_tcmd(SCR *sp, VICMD *vp, ARG_CHAR_T prompt, u_int flags)
{
	/* Normally, we end up where we started. */
	vp->m_final.lno = sp->lno;
	vp->m_final.cno = sp->cno;

	/* Initialize the map. */
	if (txt_map_init(sp))
		return (1);

	/* Move to the last line. */
	sp->lno = TMAP[0].lno;
	sp->cno = 0;

	/* Don't update the modeline for now. */
	F_SET(sp, SC_TINPUT_INFO);

	/* Set the input flags. */
	LF_SET(TXT_APPENDEOL |
	    TXT_CR | TXT_ESCAPE | TXT_INFOLINE | TXT_MAPINPUT);
	if (O_ISSET(sp, O_ALTWERASE))
		LF_SET(TXT_ALTWERASE);
	if (O_ISSET(sp, O_TTYWERASE))
		LF_SET(TXT_TTYWERASE);

	/* Do the input thing. */
	if (v_txt(sp, vp, NULL, NULL, 0, prompt, 0, 1, flags))
		return (1);

	/* Reenable the modeline updates. */
	F_CLR(sp, SC_TINPUT_INFO);

	/* Clean up the map. */
	if (txt_map_end(sp))
		return (1);

	if (IS_ONELINE(sp))
		F_SET(sp, SC_SCR_REDRAW);	/* XXX */

	/* Set the cursor to the resulting position. */
	sp->lno = vp->m_final.lno;
	sp->cno = vp->m_final.cno;

	return (0);
}

/*
 * txt_map_init
 *	Initialize the screen map for colon command-line input.
 */
static int
txt_map_init(SCR *sp)
{
	SMAP *esmp;
	VI_PRIVATE *vip;

	vip = VIP(sp);
	if (!IS_ONELINE(sp)) {
		/*
		 * Fake like the user is doing input on the last line of the
		 * screen.  This makes all of the scrolling work correctly,
		 * and allows us the use of the vi text editing routines, not
		 * to mention practically infinite length ex commands.
		 *
		 * Save the current location.
		 */
		vip->sv_tm_lno = TMAP->lno;
		vip->sv_tm_soff = TMAP->soff;
		vip->sv_tm_coff = TMAP->coff;
		vip->sv_t_maxrows = sp->t_maxrows;
		vip->sv_t_minrows = sp->t_minrows;
		vip->sv_t_rows = sp->t_rows;

		/*
		 * If it's a small screen, TMAP may be small for the screen.
		 * Fix it, filling in fake lines as we go.
		 */
		if (IS_SMALL(sp))
			for (esmp =
			    HMAP + (sp->t_maxrows - 1); TMAP < esmp; ++TMAP) {
				TMAP[1].lno = TMAP[0].lno + 1;
				TMAP[1].coff = HMAP->coff;
				TMAP[1].soff = 1;
			}

		/* Build the fake entry. */
		TMAP[1].lno = TMAP[0].lno + 1;
		TMAP[1].soff = 1;
		TMAP[1].coff = 0;
		SMAP_FLUSH(&TMAP[1]);
		++TMAP;

		/* Reset the screen information. */
		sp->t_rows = sp->t_minrows = ++sp->t_maxrows;
	}
	return (0);
}

/*
 * txt_map_end
 *	Reset the screen map for colon command-line input.
 */
static int
txt_map_end(SCR *sp)
{
	VI_PRIVATE *vip;
	size_t cnt;

	vip = VIP(sp);
	if (!IS_ONELINE(sp)) {
		/* Restore the screen information. */
		sp->t_rows = vip->sv_t_rows;
		sp->t_minrows = vip->sv_t_minrows;
		sp->t_maxrows = vip->sv_t_maxrows;

		/*
		 * If it's a small screen, TMAP may be wrong.  Clear any
		 * lines that might have been overwritten.
		 */
		if (IS_SMALL(sp)) {
			for (cnt = sp->t_rows; cnt <= sp->t_maxrows; ++cnt) {
				(void)sp->gp->scr_move(sp, cnt, 0);
				(void)sp->gp->scr_clrtoeol(sp);
			}
			TMAP = HMAP + (sp->t_rows - 1);
		} else
			--TMAP;

		/*
		 * The map may be wrong if the user entered more than one
		 * (logical) line.  Fix it.  If the user entered a whole
		 * screen, this will be slow, but we probably don't care.
		 */
		if (!O_ISSET(sp, O_LEFTRIGHT))
			while (vip->sv_tm_lno != TMAP->lno ||
			    vip->sv_tm_soff != TMAP->soff)
				if (vs_sm_1down(sp))
					return (1);
	}

	/*
	 * Invalidate the cursor and the line size cache, the line never
	 * really existed.  This fixes bugs where the user searches for
	 * the last line on the screen + 1 and the refresh routine thinks
	 * that's where we just were.
	 */
	VI_SCR_CFLUSH(vip);
	F_SET(vip, VIP_CUR_INVALID);

	return (0);
}

/*
 * If doing input mapping on the colon command line, may need to unmap
 * based on the command.
 */
#define	UNMAP_TST							\
	FL_ISSET(ec_flags, EC_MAPINPUT) && LF_ISSET(TXT_INFOLINE)

/* 
 * Internally, we maintain tp->lno and tp->cno, externally, everyone uses
 * sp->lno and sp->cno.  Make them consistent as necessary.
 */
#define	UPDATE_POSITION(sp, tp) {					\
	(sp)->lno = (tp)->lno;						\
	(sp)->cno = (tp)->cno;						\
}

/*
 * v_txt --
 *	Vi text input.
 *
 * PUBLIC: int v_txt(SCR *, VICMD *, MARK *,
 * PUBLIC:    const CHAR_T *, size_t, ARG_CHAR_T, recno_t, u_long, u_int32_t);
 */
int
v_txt(
	SCR *sp,
	VICMD *vp,
	MARK *tm,		/* To MARK. */
	const CHAR_T *lp,	/* Input line. */
	size_t len,		/* Input line length. */
	ARG_CHAR_T prompt,	/* Prompt to display. */
	recno_t ai_line,	/* Line number to use for autoindent count. */
	u_long rcount,		/* Replay count. */
	u_int32_t flags)	/* TXT_* flags. */
{
	EVENT ev, *evp = NULL;	/* Current event. */
	EVENT fc;		/* File name completion event. */
	GS *gp;
	TEXT *ntp, *tp;		/* Input text structures. */
	TEXT ait;		/* Autoindent text structure. */
	TEXT wmt = {{ 0 }};	/* Wrapmargin text structure. */
	TEXTH *tiqh;
	VI_PRIVATE *vip;
	abb_t abb;		/* State of abbreviation checks. */
	carat_t carat;		/* State of the "[^0]^D" sequences. */
	quote_t quote;		/* State of quotation. */
	size_t owrite, insert;	/* Temporary copies of TEXT fields. */
	size_t margin;		/* Wrapmargin value. */
	size_t rcol;		/* 0-N: insert offset in the replay buffer. */
	size_t tcol;		/* Temporary column. */
	u_int32_t ec_flags;	/* Input mapping flags. */
#define	IS_RESTART	0x01	/* Reset the incremental search. */
#define	IS_RUNNING	0x02	/* Incremental search turned on. */
	u_int8_t is_flags;
	int abcnt, ab_turnoff;	/* Abbreviation character count, switch. */
	int filec_redraw;	/* Redraw after the file completion routine. */
	int hexcnt;		/* Hex character count. */
	int showmatch;		/* Showmatch set on this character. */
	int wm_set, wm_skip;	/* Wrapmargin happened, blank skip flags. */
	int max, tmp;
	int nochange;
	CHAR_T *p;

	gp = sp->gp;
	vip = VIP(sp);

	/*
	 * Set the input flag, so tabs get displayed correctly
	 * and everyone knows that the text buffer is in use.
	 */
	F_SET(sp, SC_TINPUT);

	/*
	 * Get one TEXT structure with some initial buffer space, reusing
	 * the last one if it's big enough.  (All TEXT bookkeeping fields
	 * default to 0 -- text_init() handles this.)  If changing a line,
	 * copy it into the TEXT buffer.
	 */
	tiqh = sp->tiq;
	if (!TAILQ_EMPTY(tiqh)) {
		tp = TAILQ_FIRST(tiqh);
		if (TAILQ_NEXT(tp, q) != NULL ||
		    tp->lb_len < (len + 32) * sizeof(CHAR_T)) {
			text_lfree(tiqh);
			goto newtp;
		}
		tp->ai = tp->insert = tp->offset = tp->owrite = 0;
		if (lp != NULL) {
			tp->len = len;
			BINC_RETW(sp, tp->lb, tp->lb_len, len);
			MEMMOVE(tp->lb, lp, len);
		} else
			tp->len = 0;
	} else {
newtp:		if ((tp = text_init(sp, lp, len, len + 32)) == NULL)
			return (1);
		TAILQ_INSERT_HEAD(tiqh, tp, q);
	}

	/* Set default termination condition. */
	tp->term = TERM_OK;

	/* Set the starting line, column. */
	tp->lno = sp->lno;
	tp->cno = sp->cno;

	/*
	 * Set the insert and overwrite counts.  If overwriting characters,
	 * do insertion afterward.  If not overwriting characters, assume
	 * doing insertion.  If change is to a mark, emphasize it with an
	 * CH_ENDMARK character.
	 */
	if (len) {
		if (LF_ISSET(TXT_OVERWRITE)) {
			tp->owrite = (tm->cno - tp->cno) + 1;
			tp->insert = (len - tm->cno) - 1;
		} else
			tp->insert = len - tp->cno;

		if (LF_ISSET(TXT_EMARK) && txt_emark(sp, tp, tm->cno))
			return (1);
	}

	/*
	 * Many of the special cases in text input are to handle autoindent
	 * support.  Somebody decided that it would be a good idea if "^^D"
	 * and "0^D" deleted all of the autoindented characters.  In an editor
	 * that takes single character input from the user, this beggars the
	 * imagination.  Note also, "^^D" resets the next lines' autoindent,
	 * but "0^D" doesn't.
	 *
	 * We assume that autoindent only happens on empty lines, so insert
	 * and overwrite will be zero.  If doing autoindent, figure out how
	 * much indentation we need and fill it in.  Update input column and
	 * screen cursor as necessary.
	 */
	if (LF_ISSET(TXT_AUTOINDENT) && ai_line != OOBLNO) {
		if (v_txt_auto(sp, ai_line, NULL, 0, tp))
			return (1);
		tp->cno = tp->ai;
	} else {
		/*
		 * The cc and S commands have a special feature -- leading
		 * <blank> characters are handled as autoindent characters.
		 * Beauty!
		 */
		if (LF_ISSET(TXT_AICHARS)) {
			tp->offset = 0;
			tp->ai = tp->cno;
		} else
			tp->offset = tp->cno;
	}

	/* If getting a command buffer from the user, there may be a prompt. */
	if (LF_ISSET(TXT_PROMPT)) {
		tp->lb[tp->cno++] = prompt;
		++tp->len;
		++tp->offset;
	}

	/*
	 * If appending after the end-of-line, add a space into the buffer
	 * and move the cursor right.  This space is inserted, i.e. pushed
	 * along, and then deleted when the line is resolved.  Assumes that
	 * the cursor is already positioned at the end of the line.  This
	 * avoids the nastiness of having the cursor reside on a magical
	 * column, i.e. a column that doesn't really exist.  The only down
	 * side is that we may wrap lines or scroll the screen before it's
	 * strictly necessary.  Not a big deal.
	 */
	if (LF_ISSET(TXT_APPENDEOL)) {
		tp->lb[tp->cno] = CH_CURSOR;
		++tp->len;
		++tp->insert;
		(void)vs_change(sp, tp->lno, LINE_RESET);
	}

	/*
	 * Historic practice is that the wrapmargin value was a distance
	 * from the RIGHT-HAND margin, not the left.  It's more useful to
	 * us as a distance from the left-hand margin, i.e. the same as
	 * the wraplen value.  The wrapmargin option is historic practice.
	 * Nvi added the wraplen option so that it would be possible to
	 * edit files with consistent margins without knowing the number of
	 * columns in the window.
	 *
	 * XXX
	 * Setting margin causes a significant performance hit.  Normally
	 * we don't update the screen if there are keys waiting, but we
	 * have to if margin is set, otherwise the screen routines don't
	 * know where the cursor is.
	 *
	 * !!!
	 * Abbreviated keys were affected by the wrapmargin option in the
	 * historic 4BSD vi.  Mapped keys were usually, but sometimes not.
	 * See the comment in vi/v_text():set_txt_std for more information.
	 *
	 * !!!
	 * One more special case.  If an inserted <blank> character causes
	 * wrapmargin to split the line, the next user entered character is
	 * discarded if it's a <space> character.
	 */
	wm_set = wm_skip = 0;
	if (LF_ISSET(TXT_WRAPMARGIN))
		if ((margin = O_VAL(sp, O_WRAPMARGIN)) != 0)
			margin = sp->cols - margin;
		else
			margin = O_VAL(sp, O_WRAPLEN);
	else
		margin = 0;

	/* Initialize abbreviation checks. */
	abcnt = ab_turnoff = 0;
	abb = F_ISSET(gp, G_ABBREV) &&
	    LF_ISSET(TXT_MAPINPUT) ? AB_INWORD : AB_NOTSET;

	/*
	 * Set up the dot command.  Dot commands are done by saving the actual
	 * characters and then reevaluating them so that things like wrapmargin
	 * can change between the insert and the replay.
	 *
	 * !!!
	 * Historically, vi did not remap or reabbreviate replayed input.  (It
	 * did beep at you if you changed an abbreviation and then replayed the
	 * input.  We're not that compatible.)  We don't have to do anything to
	 * avoid remapping, as we're not getting characters from the terminal
	 * routines.  Turn the abbreviation check off.
	 *
	 * XXX
	 * It would be nice if we could swallow backspaces and such, but it's
	 * not all that easy to do.  What we can do is turn off the common
	 * error messages during the replay.  Otherwise, when the user enters
	 * an illegal command, e.g., "Ia<erase><erase><erase><erase>b<escape>",
	 * and then does a '.', they get a list of error messages after command
	 * completion.
	 */
	rcol = 0;
	if (LF_ISSET(TXT_REPLAY)) {
		abb = AB_NOTSET;
		LF_CLR(TXT_RECORD);
	}

	/* Other text input mode setup. */
	quote = Q_NOTSET;
	carat = C_NOTSET;
	nochange = 0;
	FL_INIT(is_flags,
	    LF_ISSET(TXT_SEARCHINCR) ? IS_RESTART | IS_RUNNING : 0);
	filec_redraw = hexcnt = showmatch = 0;

	/* Initialize input flags. */
	ec_flags = LF_ISSET(TXT_MAPINPUT) ? EC_MAPINPUT : 0;

	/* Refresh the screen. */
	UPDATE_POSITION(sp, tp);
	if (vs_refresh(sp, 1))
		return (1);

	/* If it's dot, just do it now. */
	if (F_ISSET(vp, VC_ISDOT))
		goto replay;

	/* Get an event. */
	evp = &ev;
next:	if (v_event_get(sp, evp, 0, ec_flags))
		return (1);

	/*
	 * If file completion overwrote part of the screen and nothing else has
	 * been displayed, clean up.  We don't do this as part of the normal
	 * message resolution because we know the user is on the colon command
	 * line and there's no reason to enter explicit characters to continue.
	 */
	if (filec_redraw && !F_ISSET(sp, SC_SCR_EXWROTE)) {
		filec_redraw = 0;

		fc.e_event = E_REPAINT;
		fc.e_flno = vip->totalcount >=
		    sp->rows ? 1 : sp->rows - vip->totalcount;
		fc.e_tlno = sp->rows;
		vip->linecount = vip->lcontinue = vip->totalcount = 0;
		(void)vs_repaint(sp, &fc);
		(void)vs_refresh(sp, 1);
	}

	/* Deal with all non-character events. */
	switch (evp->e_event) {
	case E_CHARACTER:
		break;
	case E_ERR:
	case E_EOF:
		F_SET(sp, SC_EXIT_FORCE);
		return (1);
	case E_INTERRUPT:
		/*
		 * !!!
		 * Historically, <interrupt> exited the user from text input
		 * mode or cancelled a colon command, and returned to command
		 * mode.  It also beeped the terminal, but that seems a bit
		 * excessive.
		 */
		goto k_escape;
	case E_REPAINT:
		if (vs_repaint(sp, &ev))
			return (1);
		goto next;
	case E_WRESIZE:
		/* <resize> interrupts the input mode. */
		v_emsg(sp, NULL, VIM_WRESIZE);
		goto k_escape;
	default:
		v_event_err(sp, evp);
		goto k_escape;
	}

	/*
	 * !!!
	 * If the first character of the input is a nul, replay the previous
	 * input.  (Historically, it's okay to replay non-existent input.)
	 * This was not documented as far as I know, and is a great test of vi
	 * clones.
	 */
	if (LF_ISSET(TXT_RECORD) && rcol == 0 && evp->e_c == '\0') {
		if (vip->rep == NULL)
			goto done;

		abb = AB_NOTSET;
		LF_CLR(TXT_RECORD);
		LF_SET(TXT_REPLAY);
		goto replay;
	}

	/*
	 * File name completion and colon command-line editing.   We don't
	 * have enough meta characters, so we expect people to overload
	 * them.  If the two characters are the same, then we do file name
	 * completion if the cursor is past the first column, and do colon
	 * command-line editing if it's not.
	 */
	if (quote == Q_NOTSET) {
		int L__cedit, L__filec;

		L__cedit = L__filec = 0;
		if (LF_ISSET(TXT_CEDIT) && O_STR(sp, O_CEDIT) != NULL &&
		    O_STR(sp, O_CEDIT)[0] == evp->e_c)
			L__cedit = 1;
		if (LF_ISSET(TXT_FILEC) && O_STR(sp, O_FILEC) != NULL &&
		    O_STR(sp, O_FILEC)[0] == evp->e_c)
			L__filec = 1;
		if (L__cedit == 1 && (L__filec == 0 || tp->cno == tp->offset)) {
			tp->term = TERM_CEDIT;
			goto k_escape;
		}
		if (L__filec == 1) {
			if (txt_fc(sp, tp, &filec_redraw))
				goto err;
			goto resolve;
		}
	}

	/* Abbreviation overflow check.  See comment in txt_abbrev(). */
#define	MAX_ABBREVIATION_EXPANSION	256
	if (F_ISSET(&evp->e_ch, CH_ABBREVIATED)) {
		if (++abcnt > MAX_ABBREVIATION_EXPANSION) {
			if (v_event_flush(sp, CH_ABBREVIATED))
				msgq(sp, M_ERR,
"191|Abbreviation exceeded expansion limit: characters discarded");
			abcnt = 0;
			if (LF_ISSET(TXT_REPLAY))
				goto done;
			goto resolve;
		}
	} else
		abcnt = 0;

	/* Check to see if the character fits into the replay buffers. */
	if (LF_ISSET(TXT_RECORD)) {
		BINC_GOTO(sp, EVENT, vip->rep,
		    vip->rep_len, (rcol + 1) * sizeof(EVENT));
		vip->rep[rcol++] = *evp;
	}

replay:	if (LF_ISSET(TXT_REPLAY)) {
		if (rcol == vip->rep_cnt)
			goto k_escape;
		evp = vip->rep + rcol++;
	}

	/* Wrapmargin check for leading space. */
	if (wm_skip) {
		wm_skip = 0;
		if (evp->e_c == ' ')
			goto resolve;
	}

	/* If quoted by someone else, simply insert the character. */
	if (F_ISSET(&evp->e_ch, CH_QUOTED))
		goto insq_ch;

	/*
	 * !!!
	 * If this character was quoted by a K_VLNEXT or a backslash, replace
	 * the placeholder (a carat or a backslash) with the new character.
	 * If it was quoted by a K_VLNEXT, we've already adjusted the cursor
	 * because it has to appear on top of the placeholder character.  If
	 * it was quoted by a backslash, adjust the cursor now, the cursor
	 * doesn't appear on top of it.  Historic practice in both cases.
	 *
	 * Skip tests for abbreviations; ":ab xa XA" followed by "ixa^V<space>"
	 * doesn't perform an abbreviation.  Special case, ^V^J (not ^V^M) is
	 * the same as ^J, historically.
	 */
	if (quote == Q_BTHIS || quote == Q_VTHIS) {
		FL_CLR(ec_flags, EC_QUOTED);
		if (LF_ISSET(TXT_MAPINPUT))
			FL_SET(ec_flags, EC_MAPINPUT);

		if (quote == Q_BTHIS &&
		    (evp->e_value == K_VERASE || evp->e_value == K_VKILL)) {
			quote = Q_NOTSET;
			--tp->cno;
			++tp->owrite;
			goto insl_ch;
		}
		if (quote == Q_VTHIS && evp->e_value != K_NL) {
			quote = Q_NOTSET;
			goto insl_ch;
		}
		quote = Q_NOTSET;
	}

	/*
	 * !!!
	 * Translate "<CH_HEX>[isxdigit()]*" to a character with a hex value:
	 * this test delimits the value by any non-hex character.  Offset by
	 * one, we use 0 to mean that we've found <CH_HEX>.
	 */
	if (hexcnt > 1 && !ISXDIGIT(evp->e_c)) {
		hexcnt = 0;
		if (txt_hex(sp, tp))
			goto err;
	}

	switch (evp->e_value) {
	case K_CR:				/* Carriage return. */
	case K_NL:				/* New line. */
		/* Return in script windows and the command line. */
k_cr:		if (LF_ISSET(TXT_CR)) {
			/*
			 * If this was a map, we may have not displayed
			 * the line.  Display it, just in case.
			 *
			 * If a script window and not the colon line,
			 * push a <cr> so it gets executed.
			 */
			if (LF_ISSET(TXT_INFOLINE)) {
				if (vs_change(sp, tp->lno, LINE_RESET))
					goto err;
			} else if (F_ISSET(sp, SC_SCRIPT))
				(void)v_event_push(sp, NULL, L("\r"), 1, CH_NOMAP);

			/* Set term condition: if empty. */
			if (tp->cno <= tp->offset)
				tp->term = TERM_CR;
			/*
			 * Set term condition: if searching incrementally and
			 * the user entered a pattern, return a completed
			 * search, regardless if the entire pattern was found.
			 */
			if (FL_ISSET(is_flags, IS_RUNNING) &&
			    tp->cno >= tp->offset + 1)
				tp->term = TERM_SEARCH;

			goto k_escape;
		}

#define	LINE_RESOLVE {							\
		/*							\
		 * Handle abbreviations.  If there was one, discard the	\
		 * replay characters.					\
		 */							\
		if (abb == AB_INWORD &&					\
		    !LF_ISSET(TXT_REPLAY) && F_ISSET(gp, G_ABBREV)) {	\
			if (txt_abbrev(sp, tp, &evp->e_c,		\
			    LF_ISSET(TXT_INFOLINE), &tmp,		\
			    &ab_turnoff))				\
				goto err;				\
			if (tmp) {					\
				if (LF_ISSET(TXT_RECORD))		\
					rcol -= tmp + 1;		\
				goto resolve;				\
			}						\
		}							\
		if (abb != AB_NOTSET)					\
			abb = AB_NOTWORD;				\
		if (UNMAP_TST)						\
			txt_unmap(sp, tp, &ec_flags);			\
		/*							\
		 * Delete any appended cursor.  It's possible to get in	\
		 * situations where TXT_APPENDEOL is set but tp->insert	\
		 * is 0 when using the R command and all the characters	\
		 * are tp->owrite characters.				\
		 */							\
		if (LF_ISSET(TXT_APPENDEOL) && tp->insert > 0) {	\
			--tp->len;					\
			--tp->insert;					\
		}							\
}
		LINE_RESOLVE;

		/*
		 * Save the current line information for restoration in
		 * txt_backup(), and set the line final length.
		 */
		tp->sv_len = tp->len;
		tp->sv_cno = tp->cno;
		tp->len = tp->cno;

		/* Update the old line. */
		if (vs_change(sp, tp->lno, LINE_RESET))
			goto err;

		/*
		 * Historic practice, when the autoindent edit option was set,
		 * was to delete <blank> characters following the inserted
		 * newline.  This affected the 'R', 'c', and 's' commands; 'c'
		 * and 's' retained the insert characters only, 'R' moved the
		 * overwrite and insert characters into the next TEXT structure.
		 * We keep track of the number of characters erased for the 'R'
		 * command so that the final resolution of the line is correct.
		 */
		tp->R_erase = 0;
		owrite = tp->owrite;
		insert = tp->insert;
		if (LF_ISSET(TXT_REPLACE) && owrite != 0) {
			for (p = tp->lb + tp->cno; owrite > 0 && isblank(*p);
			    ++p, --owrite, ++tp->R_erase);
			if (owrite == 0)
				for (; insert > 0 && isblank(*p);
				    ++p, ++tp->R_erase, --insert);
		} else {
			p = tp->lb + tp->cno + owrite;
			if (O_ISSET(sp, O_AUTOINDENT))
				for (; insert > 0 &&
				    isblank(*p); ++p, --insert);
			owrite = 0;
		}

		/*
		 * !!!
		 * Create a new line and insert the new TEXT into the queue.
		 * DON'T insert until the old line has been updated, or the
		 * inserted line count in line.c:db_get() will be wrong.
		 */
		if ((ntp = text_init(sp, p,
		    insert + owrite, insert + owrite + 32)) == NULL)
			goto err;
		TAILQ_INSERT_TAIL(sp->tiq, ntp, q);

		/* Set up bookkeeping for the new line. */
		ntp->insert = insert;
		ntp->owrite = owrite;
		ntp->lno = tp->lno + 1;

		/*
		 * Reset the autoindent line value.  0^D keeps the autoindent
		 * line from changing, ^D changes the level, even if there were
		 * no characters in the old line.  Note, if using the current
		 * tp structure, use the cursor as the length, the autoindent
		 * characters may have been erased.
		 */
		if (LF_ISSET(TXT_AUTOINDENT)) {
			if (nochange) {
				nochange = 0;
				if (v_txt_auto(sp, OOBLNO, &ait, ait.ai, ntp))
					goto err;
				FREE_SPACEW(sp, ait.lb, ait.lb_len);
			} else
				if (v_txt_auto(sp, OOBLNO, tp, tp->cno, ntp))
					goto err;
			carat = C_NOTSET;
		}

		/* Reset the cursor. */
		ntp->cno = ntp->ai;

		/*
		 * If we're here because wrapmargin was set and we've broken a
		 * line, there may be additional information (i.e. the start of
		 * a line) in the wmt structure.
		 */
		if (wm_set) {
			if (wmt.offset != 0 ||
			    wmt.owrite != 0 || wmt.insert != 0) {
#define	WMTSPACE	wmt.offset + wmt.owrite + wmt.insert
				BINC_GOTOW(sp, ntp->lb,
				    ntp->lb_len, ntp->len + WMTSPACE + 32);
				MEMMOVE(ntp->lb + ntp->cno, wmt.lb, WMTSPACE);
				ntp->len += WMTSPACE;
				ntp->cno += wmt.offset;
				ntp->owrite = wmt.owrite;
				ntp->insert = wmt.insert;
			}
			wm_set = 0;
		}

		/* New lines are TXT_APPENDEOL. */
		if (ntp->owrite == 0 && ntp->insert == 0) {
			BINC_GOTOW(sp, ntp->lb, ntp->lb_len, ntp->len + 1);
			LF_SET(TXT_APPENDEOL);
			ntp->lb[ntp->cno] = CH_CURSOR;
			++ntp->insert;
			++ntp->len;
		}

		/* Swap old and new TEXT's, and update the new line. */
		tp = ntp;
		if (vs_change(sp, tp->lno, LINE_INSERT))
			goto err;

		goto resolve;
	case K_ESCAPE:				/* Escape. */
		if (!LF_ISSET(TXT_ESCAPE))
			goto ins_ch;

		/* If we have a count, start replaying the input. */
		if (rcount > 1) {
			--rcount;

			vip->rep_cnt = rcol;
			rcol = 0;
			abb = AB_NOTSET;
			LF_CLR(TXT_RECORD);
			LF_SET(TXT_REPLAY);

			/*
			 * Some commands (e.g. 'o') need a <newline> for each
			 * repetition.
			 */
			if (LF_ISSET(TXT_ADDNEWLINE))
				goto k_cr;

			/*
			 * The R command turns into the 'a' command after the
			 * first repetition.
			 */
			if (LF_ISSET(TXT_REPLACE)) {
				tp->insert = tp->owrite;
				tp->owrite = 0;
				LF_CLR(TXT_REPLACE);
			}
			goto replay;
		}

		/* Set term condition: if empty. */
		if (tp->cno <= tp->offset)
			tp->term = TERM_ESC;
		/*
		 * Set term condition: if searching incrementally and the user
		 * entered a pattern, return a completed search, regardless if
		 * the entire pattern was found.
		 */
		if (FL_ISSET(is_flags, IS_RUNNING) && tp->cno >= tp->offset + 1)
			tp->term = TERM_SEARCH;

k_escape:	LINE_RESOLVE;

		/*
		 * Clean up for the 'R' command, restoring overwrite
		 * characters, and making them into insert characters.
		 */
		if (LF_ISSET(TXT_REPLACE))
			txt_Rresolve(sp, sp->tiq, tp, len);

		/*
		 * If there are any overwrite characters, copy down
		 * any insert characters, and decrement the length.
		 */
		if (tp->owrite) {
			if (tp->insert)
				MEMMOVE(tp->lb + tp->cno,
				    tp->lb + tp->cno + tp->owrite, tp->insert);
			tp->len -= tp->owrite;
		}

		/*
		 * Optionally resolve the lines into the file.  If not
		 * resolving the lines into the file, end the line with
		 * a nul.  If the line is empty, then set the length to
		 * 0, the termination condition has already been set.
		 *
		 * XXX
		 * This is wrong, should pass back a length.
		 */
		if (LF_ISSET(TXT_RESOLVE)) {
			if (txt_resolve(sp, sp->tiq, flags))
				goto err;
		} else {
			BINC_GOTOW(sp, tp->lb, tp->lb_len, tp->len + 1);
			tp->lb[tp->len] = '\0';
		}

		/*
		 * Set the return cursor position to rest on the last
		 * inserted character.
		 */
		if (tp->cno != 0)
			--tp->cno;

		/* Update the last line. */
		if (vs_change(sp, tp->lno, LINE_RESET))
			return (1);
		goto done;
	case K_CARAT:			/* Delete autoindent chars. */
		if (tp->cno <= tp->ai && LF_ISSET(TXT_AUTOINDENT))
			carat = C_CARATSET;
		goto ins_ch;
	case K_ZERO:			/* Delete autoindent chars. */
		if (tp->cno <= tp->ai && LF_ISSET(TXT_AUTOINDENT))
			carat = C_ZEROSET;
		goto ins_ch;
	case K_CNTRLD:			/* Delete autoindent char. */
		/*
		 * If in the first column or no characters to erase, ignore
		 * the ^D (this matches historic practice).  If not doing
		 * autoindent or already inserted non-ai characters, it's a
		 * literal.  The latter test is done in the switch, as the
		 * CARAT forms are N + 1, not N.
		 */
		if (!LF_ISSET(TXT_AUTOINDENT))
			goto ins_ch;
		if (tp->cno == 0)
			goto resolve;

		switch (carat) {
		case C_CARATSET:	/* ^^D */
			if (tp->ai == 0 || tp->cno > tp->ai + tp->offset + 1)
				goto ins_ch;

			/* Save the ai string for later. */
			ait.lb = NULL;
			ait.lb_len = 0;
			BINC_GOTOW(sp, ait.lb, ait.lb_len, tp->ai);
			MEMMOVE(ait.lb, tp->lb, tp->ai);
			ait.ai = ait.len = tp->ai;

			carat = C_NOTSET;
			nochange = 1;
			goto leftmargin;
		case C_ZEROSET:		/* 0^D */
			if (tp->ai == 0 || tp->cno > tp->ai + tp->offset + 1)
				goto ins_ch;

			carat = C_NOTSET;
leftmargin:		tp->lb[tp->cno - 1] = ' ';
			tp->owrite += tp->cno - tp->offset;
			tp->ai = 0;
			tp->cno = tp->offset;
			break;
		case C_NOTSET:		/* ^D */
			if (tp->ai == 0 || tp->cno > tp->ai + tp->offset)
				goto ins_ch;

			(void)txt_dent(sp, tp, 0);
			break;
		default:
			abort();
		}
		break;
	case K_VERASE:			/* Erase the last character. */
		/* If can erase over the prompt, return. */
		if (tp->cno <= tp->offset && LF_ISSET(TXT_BS)) {
			tp->term = TERM_BS;
			goto done;
		}

		/*
		 * If at the beginning of the line, try and drop back to a
		 * previously inserted line.
		 */
		if (tp->cno == 0) {
			if ((ntp =
			    txt_backup(sp, sp->tiq, tp, &flags)) == NULL)
				goto err;
			tp = ntp;
			break;
		}

		/* If nothing to erase, bell the user. */
		if (tp->cno <= tp->offset) {
			if (!LF_ISSET(TXT_REPLAY))
				txt_nomorech(sp);
			break;
		}

		/* Drop back one character. */
		--tp->cno;

		/*
		 * Historically, vi didn't replace the erased characters with
		 * <blank>s, presumably because it's easier to fix a minor
		 * typing mistake and continue on if the previous letters are
		 * already there.  This is a problem for incremental searching,
		 * because the user can no longer tell where they are in the
		 * colon command line because the cursor is at the last search
		 * point in the screen.  So, if incrementally searching, erase
		 * the erased characters from the screen.
		 */
		if (FL_ISSET(is_flags, IS_RUNNING))
			tp->lb[tp->cno] = ' ';

		/*
		 * Increment overwrite, decrement ai if deleted.
		 *
		 * !!!
		 * Historic vi did not permit users to use erase characters
		 * to delete autoindent characters.  We do.  Eat hot death,
		 * POSIX.
		 */
		++tp->owrite;
		if (tp->cno < tp->ai)
			--tp->ai;

		/* Reset if we deleted an incremental search character. */
		if (FL_ISSET(is_flags, IS_RUNNING))
			FL_SET(is_flags, IS_RESTART);
		break;
	case K_VWERASE:			/* Skip back one word. */
		/*
		 * If at the beginning of the line, try and drop back to a
		 * previously inserted line.
		 */
		if (tp->cno == 0) {
			if ((ntp =
			    txt_backup(sp, sp->tiq, tp, &flags)) == NULL)
				goto err;
			tp = ntp;
		}

		/*
		 * If at offset, nothing to erase so bell the user.
		 */
		if (tp->cno <= tp->offset) {
			if (!LF_ISSET(TXT_REPLAY))
				txt_nomorech(sp);
			break;
		}

		/*
		 * The first werase goes back to any autoindent column and the
		 * second werase goes back to the offset.
		 *
		 * !!!
		 * Historic vi did not permit users to use erase characters to
		 * delete autoindent characters.
		 */
		if (tp->ai && tp->cno > tp->ai)
			max = tp->ai;
		else {
			tp->ai = 0;
			max = tp->offset;
		}

		/* Skip over trailing space characters. */
		while (tp->cno > max && ISBLANK(tp->lb[tp->cno - 1])) {
			--tp->cno;
			++tp->owrite;
		}
		if (tp->cno == max)
			break;
		/*
		 * There are three types of word erase found on UNIX systems.
		 * They can be identified by how the string /a/b/c is treated
		 * -- as 1, 3, or 6 words.  Historic vi had two classes of
		 * characters, and strings were delimited by them and
		 * <blank>'s, so, 6 words.  The historic tty interface used
		 * <blank>'s to delimit strings, so, 1 word.  The algorithm
		 * offered in the 4.4BSD tty interface (as stty altwerase)
		 * treats it as 3 words -- there are two classes of
		 * characters, and strings are delimited by them and
		 * <blank>'s.  The difference is that the type of the first
		 * erased character erased is ignored, which is exactly right
		 * when erasing pathname components.  The edit options
		 * TXT_ALTWERASE and TXT_TTYWERASE specify the 4.4BSD tty
		 * interface and the historic tty driver behavior,
		 * respectively, and the default is the same as the historic
		 * vi behavior.
		 *
		 * Overwrite erased characters if doing incremental search;
		 * see comment above.
		 */
		if (LF_ISSET(TXT_TTYWERASE))
			while (tp->cno > max) {
				if (ISBLANK(tp->lb[tp->cno - 1]))
					break;
				--tp->cno;
				++tp->owrite;
				if (FL_ISSET(is_flags, IS_RUNNING))
					tp->lb[tp->cno] = ' ';
			}
		else {
			if (LF_ISSET(TXT_ALTWERASE)) {
				--tp->cno;
				++tp->owrite;
				if (FL_ISSET(is_flags, IS_RUNNING))
					tp->lb[tp->cno] = ' ';
			}
			if (tp->cno > max)
				tmp = inword(tp->lb[tp->cno - 1]);
			while (tp->cno > max) {
				if (tmp != inword(tp->lb[tp->cno - 1])
				    || ISBLANK(tp->lb[tp->cno - 1]))
					break;
				--tp->cno;
				++tp->owrite;
				if (FL_ISSET(is_flags, IS_RUNNING))
					tp->lb[tp->cno] = ' ';
			}
		}

		/* Reset if we deleted an incremental search character. */
		if (FL_ISSET(is_flags, IS_RUNNING))
			FL_SET(is_flags, IS_RESTART);
		break;
	case K_VKILL:			/* Restart this line. */
		/*
		 * !!!
		 * If at the beginning of the line, try and drop back to a
		 * previously inserted line.  Historic vi did not permit
		 * users to go back to previous lines.
		 */
		if (tp->cno == 0) {
			if ((ntp =
			    txt_backup(sp, sp->tiq, tp, &flags)) == NULL)
				goto err;
			tp = ntp;
		}

		/* If at offset, nothing to erase so bell the user. */
		if (tp->cno <= tp->offset) {
			if (!LF_ISSET(TXT_REPLAY))
				txt_nomorech(sp);
			break;
		}

		/*
		 * First kill goes back to any autoindent and second kill goes
		 * back to the offset.
		 *
		 * !!!
		 * Historic vi did not permit users to use erase characters to
		 * delete autoindent characters.
		 */
		if (tp->ai && tp->cno > tp->ai)
			max = tp->ai;
		else {
			tp->ai = 0;
			max = tp->offset;
		}
		tp->owrite += tp->cno - max;

		/*
		 * Overwrite erased characters if doing incremental search;
		 * see comment above.
		 */
		if (FL_ISSET(is_flags, IS_RUNNING))
			do {
				tp->lb[--tp->cno] = ' ';
			} while (tp->cno > max);
		else
			tp->cno = max;

		/* Reset if we deleted an incremental search character. */
		if (FL_ISSET(is_flags, IS_RUNNING))
			FL_SET(is_flags, IS_RESTART);
		break;
	case K_CNTRLT:			/* Add autoindent characters. */
		if (!LF_ISSET(TXT_CNTRLT))
			goto ins_ch;
		if (txt_dent(sp, tp, 1))
			goto err;
		goto ebuf_chk;
	case K_BACKSLASH:		/* Quote next erase/kill. */
		/*
		 * !!!
		 * Historic vi tried to make abbreviations after a backslash
		 * escape work.  If you did ":ab x y", and inserted "x\^H",
		 * (assuming the erase character was ^H) you got "x^H", and
		 * no abbreviation was done.  If you inserted "x\z", however,
		 * it tried to back up and do the abbreviation, i.e. replace
		 * 'x' with 'y'.  The problem was it got it wrong, and you
		 * ended up with "zy\".
		 *
		 * This is really hard to do (you have to remember the
		 * word/non-word state, for example), and doesn't make any
		 * sense to me.  Both backslash and the characters it
		 * (usually) escapes will individually trigger the
		 * abbreviation, so I don't see why the combination of them
		 * wouldn't.  I don't expect to get caught on this one,
		 * particularly since it never worked right, but I've been
		 * wrong before.
		 *
		 * Do the tests for abbreviations, so ":ab xa XA",
		 * "ixa\<K_VERASE>" performs the abbreviation.
		 */
		quote = Q_BNEXT;
		goto insq_ch;
	case K_VLNEXT:			/* Quote next character. */
		evp->e_c = '^';
		quote = Q_VNEXT;
		/*
		 * Turn on the quote flag so that the underlying routines
		 * quote the next character where it's possible. Turn off
		 * the input mapbiting flag so that we don't remap the next
		 * character.
		 */
		FL_SET(ec_flags, EC_QUOTED);
		FL_CLR(ec_flags, EC_MAPINPUT);

		/*
		 * !!!
		 * Skip the tests for abbreviations, so ":ab xa XA",
		 * "ixa^V<space>" doesn't perform the abbreviation.
		 */
		goto insl_ch;
	case K_HEXCHAR:
		hexcnt = 1;
		goto insq_ch;
	default:			/* Insert the character. */
		if (LF_ISSET(TXT_SHOWMATCH)) {
			CHAR_T *match_chars, *cp;

			match_chars = VIP(sp)->mcs;
			cp = STRCHR(match_chars, evp->e_c);
			if (cp != NULL && (cp - match_chars) & 1)
				showmatch = 1;
		}
ins_ch:		/*
		 * Historically, vi eliminated nul's out of hand.  If the
		 * beautify option was set, it also deleted any unknown
		 * ASCII value less than space (040) and the del character
		 * (0177), except for tabs.  Unknown is a key word here.
		 * Most vi documentation claims that it deleted everything
		 * but <tab>, <nl> and <ff>, as that's what the original
		 * 4BSD documentation said.  This is obviously wrong,
		 * however, as <esc> would be included in that list.  What
		 * we do is eliminate any unquoted, iscntrl() character that
		 * wasn't a replay and wasn't handled specially, except
		 * <tab> or <ff>.
		 */
		if (LF_ISSET(TXT_BEAUTIFY) && ISCNTRL(evp->e_c) &&
		    evp->e_value != K_FORMFEED && evp->e_value != K_TAB) {
			msgq(sp, M_BERR,
			    "192|Illegal character; quote to enter");
			if (LF_ISSET(TXT_REPLAY))
				goto done;
			break;
		}

insq_ch:	/*
		 * If entering a non-word character after a word, check for
		 * abbreviations.  If there was one, discard replay characters.
		 * If entering a blank character, check for unmap commands,
		 * as well.
		 */
		if (!inword(evp->e_c)) {
			if (abb == AB_INWORD &&
			    !LF_ISSET(TXT_REPLAY) && F_ISSET(gp, G_ABBREV)) {
				if (txt_abbrev(sp, tp, &evp->e_c,
				    LF_ISSET(TXT_INFOLINE), &tmp, &ab_turnoff))
					goto err;
				if (tmp) {
					if (LF_ISSET(TXT_RECORD))
						rcol -= tmp + 1;
					goto resolve;
				}
			}
			if (isblank(evp->e_c) && UNMAP_TST)
				txt_unmap(sp, tp, &ec_flags);
		}
		if (abb != AB_NOTSET)
			abb = inword(evp->e_c) ? AB_INWORD : AB_NOTWORD;

insl_ch:	if (txt_insch(sp, tp, &evp->e_c, flags))
			goto err;

		/*
		 * If we're using K_VLNEXT to quote the next character, then
		 * we want the cursor to position itself on the ^ placeholder
		 * we're displaying, to match historic practice.
		 */
		if (quote == Q_VNEXT) {
			--tp->cno;
			++tp->owrite;
		}

		/*
		 * !!!
		 * Translate "<CH_HEX>[isxdigit()]*" to a character with
		 * a hex value: this test delimits the value by the max
		 * number of hex bytes.  Offset by one, we use 0 to mean
		 * that we've found <CH_HEX>.
		 */
		if (hexcnt != 0 && hexcnt++ == 3) {
			hexcnt = 0;
			if (txt_hex(sp, tp))
				goto err;
		}

		/*
		 * Check to see if we've crossed the margin.
		 *
		 * !!!
		 * In the historic vi, the wrapmargin value was figured out
		 * using the display widths of the characters, i.e. <tab>
		 * characters were counted as two characters if the list edit
		 * option is set, but as the tabstop edit option number of
		 * characters otherwise.  That's what the vs_column() function
		 * gives us, so we use it.
		 */
		if (margin != 0) {
			if (vs_column(sp, &tcol))
				goto err;
			if (tcol >= margin) {
				if (txt_margin(sp, tp, &wmt, &tmp, flags))
					goto err;
				if (tmp) {
					if (isblank(evp->e_c))
						wm_skip = 1;
					wm_set = 1;
					goto k_cr;
				}
			}
		}

		/*
		 * If we've reached the end of the buffer, then we need to
		 * switch into insert mode.  This happens when there's a
		 * change to a mark and the user puts in more characters than
		 * the length of the motion.
		 */
ebuf_chk:	if (tp->cno >= tp->len) {
			BINC_GOTOW(sp, tp->lb, tp->lb_len, tp->len + 1);
			LF_SET(TXT_APPENDEOL);

			tp->lb[tp->cno] = CH_CURSOR;
			++tp->insert;
			++tp->len;
		}

		/* Step the quote state forward. */
		if (quote != Q_NOTSET) {
			if (quote == Q_BNEXT)
				quote = Q_BTHIS;
			if (quote == Q_VNEXT)
				quote = Q_VTHIS;
		}
		break;
	}

#ifdef DEBUG
	if (tp->cno + tp->insert + tp->owrite != tp->len) {
		msgq(sp, M_ERR,
		    "len %zu != cno: %zu ai: %zu insert %zu overwrite %zu",
		    tp->len, tp->cno, tp->ai, tp->insert, tp->owrite);
		if (LF_ISSET(TXT_REPLAY))
			goto done;
		tp->len = tp->cno + tp->insert + tp->owrite;
	}
#endif

resolve:/*
	 * 1: If we don't need to know where the cursor really is and we're
	 *    replaying text, keep going.
	 */
	if (margin == 0 && LF_ISSET(TXT_REPLAY))
		goto replay;

	/*
	 * 2: Reset the line.  Don't bother unless we're about to wait on
	 *    a character or we need to know where the cursor really is.
	 *    We have to do this before showing matching characters so the
	 *    user can see what they're matching.
	 */
	if ((margin != 0 || !KEYS_WAITING(sp)) &&
	    vs_change(sp, tp->lno, LINE_RESET))
		return (1);

	/*
	 * 3: If there aren't keys waiting, display the matching character.
	 *    We have to do this before resolving any messages, otherwise
	 *    the error message from a missing match won't appear correctly.
	 */
	if (showmatch) {
		if (!KEYS_WAITING(sp) && txt_showmatch(sp, tp))
			return (1);
		showmatch = 0;
	}

	/*
	 * 4: If there have been messages and we're not editing on the colon
	 *    command line or doing file name completion, resolve them.
	 */
	if ((vip->totalcount != 0 || F_ISSET(gp, G_BELLSCHED)) &&
	    !F_ISSET(sp, SC_TINPUT_INFO) && !filec_redraw &&
	    vs_resolve(sp, NULL, 0))
		return (1);

	/*
	 * 5: Refresh the screen if we're about to wait on a character or we
	 *    need to know where the cursor really is.
	 */
	if (margin != 0 || !KEYS_WAITING(sp)) {
		UPDATE_POSITION(sp, tp);
		if (vs_refresh(sp, margin != 0))
			return (1);
	}

	/* 6: Proceed with the incremental search. */
	if (FL_ISSET(is_flags, IS_RUNNING) && txt_isrch(sp, vp, tp, &is_flags))
		return (1);

	/* 7: Next character... */
	if (LF_ISSET(TXT_REPLAY))
		goto replay;
	goto next;

done:	/* Leave input mode. */
	F_CLR(sp, SC_TINPUT);

	/* If recording for playback, save it. */
	if (LF_ISSET(TXT_RECORD))
		vip->rep_cnt = rcol;

	/*
	 * If not working on the colon command line, set the final cursor
	 * position.
	 */
	if (!F_ISSET(sp, SC_TINPUT_INFO)) {
		vp->m_final.lno = tp->lno;
		vp->m_final.cno = tp->cno;
	}
	return (0);

err:
alloc_err:
	F_CLR(sp, SC_TINPUT);
	txt_err(sp, sp->tiq);
	return (1);
}

/*
 * txt_abbrev --
 *	Handle abbreviations.
 */
static int
txt_abbrev(SCR *sp, TEXT *tp, CHAR_T *pushcp, int isinfoline, int *didsubp, int *turnoffp)
{
	VI_PRIVATE *vip;
	CHAR_T ch, *p;
	SEQ *qp;
	size_t len, off;

	/* Check to make sure we're not at the start of an append. */
	*didsubp = 0;
	if (tp->cno == tp->offset)
		return (0);

	vip = VIP(sp);

	/*
	 * Find the start of the "word".
	 *
	 * !!!
	 * We match historic practice, which, as far as I can tell, had an
	 * off-by-one error.  The way this worked was that when the inserted
	 * text switched from a "word" character to a non-word character,
	 * vi would check for possible abbreviations.  It would then take the
	 * type (i.e. word/non-word) of the character entered TWO characters
	 * ago, and move backward in the text until reaching a character that
	 * was not that type, or the beginning of the insert, the line, or
	 * the file.  For example, in the string "abc<space>", when the <space>
	 * character triggered the abbreviation check, the type of the 'b'
	 * character was used for moving through the string.  Maybe there's a
	 * reason for not using the first (i.e. 'c') character, but I can't
	 * think of one.
	 *
	 * Terminate at the beginning of the insert or the character after the
	 * offset character -- both can be tested for using tp->offset.
	 */
	off = tp->cno - 1;			/* Previous character. */
	p = tp->lb + off;
	len = 1;				/* One character test. */
	if (off == tp->offset || isblank(p[-1]))
		goto search;
	if (inword(p[-1]))			/* Move backward to change. */
		for (;;) {
			--off; --p; ++len;
			if (off == tp->offset || !inword(p[-1]))
				break;
		}
	else
		for (;;) {
			--off; --p; ++len;
			if (off == tp->offset ||
			    inword(p[-1]) || isblank(p[-1]))
				break;
		}

	/*
	 * !!!
	 * Historic vi exploded abbreviations on the command line.  This has
	 * obvious problems in that unabbreviating the string can be extremely
	 * tricky, particularly if the string has, say, an embedded escape
	 * character.  Personally, I think it's a stunningly bad idea.  Other
	 * examples of problems this caused in historic vi are:
	 *	:ab foo bar
	 *	:ab foo baz
	 * results in "bar" being abbreviated to "baz", which wasn't what the
	 * user had in mind at all.  Also, the commands:
	 *	:ab foo bar
	 *	:unab foo<space>
	 * resulted in an error message that "bar" wasn't mapped.  Finally,
	 * since the string was already exploded by the time the unabbreviate
	 * command got it, all it knew was that an abbreviation had occurred.
	 * Cleverly, it checked the replacement string for its unabbreviation
	 * match, which meant that the commands:
	 *	:ab foo1 bar
	 *	:ab foo2 bar
	 *	:unab foo2
	 * unabbreviate "foo1", and the commands:
	 *	:ab foo bar
	 *	:ab bar baz
	 * unabbreviate "foo"!
	 *
	 * Anyway, people neglected to first ask my opinion before they wrote
	 * macros that depend on this stuff, so, we make this work as follows.
	 * When checking for an abbreviation on the command line, if we get a
	 * string which is <blank> terminated and which starts at the beginning
	 * of the line, we check to see it is the abbreviate or unabbreviate
	 * commands.  If it is, turn abbreviations off and return as if no
	 * abbreviation was found.  Note also, minor trickiness, so that if
	 * the user erases the line and starts another command, we turn the
	 * abbreviations back on.
	 *
	 * This makes the layering look like a Nachos Supreme.
	 */
search:	if (isinfoline)
		if (off == tp->ai || off == tp->offset)
			if (ex_is_abbrev(p, len)) {
				*turnoffp = 1;
				return (0);
			} else
				*turnoffp = 0;
		else
			if (*turnoffp)
				return (0);

	/* Check for any abbreviations. */
	if ((qp = seq_find(sp, NULL, NULL, p, len, SEQ_ABBREV, NULL)) == NULL)
		return (0);

	/*
	 * Push the abbreviation onto the tty stack.  Historically, characters
	 * resulting from an abbreviation expansion were themselves subject to
	 * map expansions, O_SHOWMATCH matching etc.  This means the expanded
	 * characters will be re-tested for abbreviations.  It's difficult to
	 * know what historic practice in this case was, since abbreviations
	 * were applied to :colon command lines, so entering abbreviations that
	 * looped was tricky, although possible.  In addition, obvious loops
	 * didn't work as expected.  (The command ':ab a b|ab b c|ab c a' will
	 * silently only implement and/or display the last abbreviation.)
	 *
	 * This implementation doesn't recover well from such abbreviations.
	 * The main input loop counts abbreviated characters, and, when it
	 * reaches a limit, discards any abbreviated characters on the queue.
	 * It's difficult to back up to the original position, as the replay
	 * queue would have to be adjusted, and the line state when an initial
	 * abbreviated character was received would have to be saved.
	 */
	ch = *pushcp;
	if (v_event_push(sp, NULL, &ch, 1, CH_ABBREVIATED))
		return (1);
	if (v_event_push(sp, NULL, qp->output, qp->olen, CH_ABBREVIATED))
		return (1);

	/*
	 * If the size of the abbreviation is larger than or equal to the size
	 * of the original text, move to the start of the replaced characters,
	 * and add their length to the overwrite count.
	 *
	 * If the abbreviation is smaller than the original text, we have to
	 * delete the additional overwrite characters and copy down any insert
	 * characters.
	 */
	tp->cno -= len;
	if (qp->olen >= len)
		tp->owrite += len;
	else {
		if (tp->insert)
			MEMMOVE(tp->lb + tp->cno + qp->olen,
			    tp->lb + tp->cno + tp->owrite + len, tp->insert);
		tp->owrite += qp->olen;
		tp->len -= len - qp->olen;
	}

	/*
	 * We return the length of the abbreviated characters.  This is so
	 * the calling routine can replace the replay characters with the
	 * abbreviation.  This means that subsequent '.' commands will produce
	 * the same text, regardless of intervening :[un]abbreviate commands.
	 * This is historic practice.
	 */
	*didsubp = len;
	return (0);
}

/*
 * txt_unmap --
 *	Handle the unmap command.
 */
static void
txt_unmap(SCR *sp, TEXT *tp, u_int32_t *ec_flagsp)
{
	size_t len, off;
	CHAR_T *p;

	/* Find the beginning of this "word". */
	for (off = tp->cno - 1, p = tp->lb + off, len = 0;; --p, --off) {
		if (isblank(*p)) {
			++p;
			break;
		}
		++len;
		if (off == tp->ai || off == tp->offset)
			break;
	}

	/*
	 * !!!
	 * Historic vi exploded input mappings on the command line.  See the
	 * txt_abbrev() routine for an explanation of the problems inherent
	 * in this.
	 *
	 * We make this work as follows.  If we get a string which is <blank>
	 * terminated and which starts at the beginning of the line, we check
	 * to see it is the unmap command.  If it is, we return that the input
	 * mapping should be turned off.  Note also, minor trickiness, so that
	 * if the user erases the line and starts another command, we go ahead
	 * an turn mapping back on.
	 */
	if ((off == tp->ai || off == tp->offset) && ex_is_unmap(p, len))
		FL_CLR(*ec_flagsp, EC_MAPINPUT);
	else
		FL_SET(*ec_flagsp, EC_MAPINPUT);
}

/*
 * txt_ai_resolve --
 *	When a line is resolved by <esc>, review autoindent characters.
 */
static void
txt_ai_resolve(SCR *sp, TEXT *tp, int *changedp)
{
	u_long ts;
	int del;
	size_t cno, len, new, old, scno, spaces, tab_after_sp, tabs;
	CHAR_T *p;

	*changedp = 0;

	/*
	 * If the line is empty, has an offset, or no autoindent
	 * characters, we're done.
	 */
	if (!tp->len || tp->offset || !tp->ai)
		return;

	/*
	 * If the length is less than or equal to the autoindent
	 * characters, delete them.
	 */
	if (tp->len <= tp->ai) {
		tp->ai = tp->cno = tp->len = 0;
		return;
	}

	/*
	 * The autoindent characters plus any leading <blank> characters
	 * in the line are resolved into the minimum number of characters.
	 * Historic practice.
	 */
	ts = O_VAL(sp, O_TABSTOP);

	/* Figure out the last <blank> screen column. */
	for (p = tp->lb, scno = 0, len = tp->len,
	    spaces = tab_after_sp = 0; len-- && isblank(*p); ++p)
		if (*p == '\t') {
			if (spaces)
				tab_after_sp = 1;
			scno += COL_OFF(scno, ts);
		} else {
			++spaces;
			++scno;
		}

	/*
	 * If there are no spaces, or no tabs after spaces and less than
	 * ts spaces, it's already minimal.
	 */
	if (!spaces || (!tab_after_sp && spaces < ts))
		return;

	/* Count up spaces/tabs needed to get to the target. */
	for (cno = 0, tabs = 0; cno + COL_OFF(cno, ts) <= scno; ++tabs)
		cno += COL_OFF(cno, ts);
	spaces = scno - cno;

	/*
	 * Figure out how many characters we're dropping -- if we're not
	 * dropping any, it's already minimal, we're done.
	 */
	old = p - tp->lb;
	new = spaces + tabs;
	if (old == new)
		return;

	/* Shift the rest of the characters down, adjust the counts. */
	del = old - new;
	MEMMOVE(p - del, p, tp->len - old);
	tp->len -= del;
	tp->cno -= del;

	/* Fill in space/tab characters. */
	for (p = tp->lb; tabs--;)
		*p++ = '\t';
	while (spaces--)
		*p++ = ' ';
	*changedp = 1;
}

/*
 * v_txt_auto --
 *	Handle autoindent.  If aitp isn't NULL, use it, otherwise,
 *	retrieve the line.
 *
 * PUBLIC: int v_txt_auto(SCR *, recno_t, TEXT *, size_t, TEXT *);
 */
int
v_txt_auto(SCR *sp, recno_t lno, TEXT *aitp, size_t len, TEXT *tp)
{
	size_t nlen;
	CHAR_T *p, *t;

	if (aitp == NULL) {
		/*
		 * If the ex append command is executed with an address of 0,
		 * it's possible to get here with a line number of 0.  Return
		 * an indent of 0.
		 */
		if (lno == 0) {
			tp->ai = 0;
			return (0);
		}
		if (db_get(sp, lno, DBG_FATAL, &t, &len))
			return (1);
	} else
		t = aitp->lb;

	/* Count whitespace characters. */
	for (p = t; len > 0; ++p, --len)
		if (!isblank(*p))
			break;

	/* Set count, check for no indentation. */
	if ((nlen = (p - t)) == 0)
		return (0);

	/* Make sure the buffer's big enough. */
	BINC_RETW(sp, tp->lb, tp->lb_len, tp->len + nlen);

	/* Copy the buffer's current contents up. */
	if (tp->len != 0)
		MEMMOVE(tp->lb + nlen, tp->lb, tp->len);
	tp->len += nlen;

	/* Copy the indentation into the new buffer. */
	MEMMOVE(tp->lb, t, nlen);

	/* Set the autoindent count. */
	tp->ai = nlen;
	return (0);
}

/*
 * txt_backup --
 *	Back up to the previously edited line.
 */
static TEXT *
txt_backup(SCR *sp, TEXTH *tiqh, TEXT *tp, u_int32_t *flagsp)
{
	VI_PRIVATE *vip;
	TEXT *ntp;

	/* Get a handle on the previous TEXT structure. */
	if ((ntp = TAILQ_PREV(tp, _texth, q)) == NULL) {
		if (!FL_ISSET(*flagsp, TXT_REPLAY))
			msgq(sp, M_BERR,
			    "193|Already at the beginning of the insert");
		return (tp);
	}

	/* Bookkeeping. */
	ntp->len = ntp->sv_len;

	/* Handle appending to the line. */
	vip = VIP(sp);
	if (ntp->owrite == 0 && ntp->insert == 0) {
		ntp->lb[ntp->len] = CH_CURSOR;
		++ntp->insert;
		++ntp->len;
		FL_SET(*flagsp, TXT_APPENDEOL);
	} else
		FL_CLR(*flagsp, TXT_APPENDEOL);

	/* Release the current TEXT. */
	TAILQ_REMOVE(tiqh, tp, q);
	text_free(tp);

	/* Update the old line on the screen. */
	if (vs_change(sp, ntp->lno + 1, LINE_DELETE))
		return (NULL);

	/* Return the new/current TEXT. */
	return (ntp);
}

/*
 * Text indentation is truly strange.  ^T and ^D do movements to the next or
 * previous shiftwidth value, i.e. for a 1-based numbering, with shiftwidth=3,
 * ^T moves a cursor on the 7th, 8th or 9th column to the 10th column, and ^D
 * moves it back.
 *
 * !!!
 * The ^T and ^D characters in historical vi had special meaning only when they
 * were the first characters entered after entering text input mode.  As normal
 * erase characters couldn't erase autoindent characters (^T in this case), it
 * meant that inserting text into previously existing text was strange -- ^T
 * only worked if it was the first keystroke(s), and then could only be erased
 * using ^D.  This implementation treats ^T specially anywhere it occurs in the
 * input, and permits the standard erase characters to erase the characters it
 * inserts.
 *
 * !!!
 * A fun test is to try:
 *	:se sw=4 ai list
 *	i<CR>^Tx<CR>^Tx<CR>^Tx<CR>^Dx<CR>^Dx<CR>^Dx<esc>
 * Historic vi loses some of the '$' marks on the line ends, but otherwise gets
 * it right.
 *
 * XXX
 * Technically, txt_dent should be part of the screen interface, as it requires
 * knowledge of character sizes, including <space>s, on the screen.  It's here
 * because it's a complicated little beast, and I didn't want to shove it down
 * into the screen.  It's probable that KEY_COL will call into the screen once
 * there are screens with different character representations.
 *
 * txt_dent --
 *	Handle ^T indents, ^D outdents.
 *
 * If anything changes here, check the ex version to see if it needs similar
 * changes.
 */
static int
txt_dent(SCR *sp, TEXT *tp, int isindent)
{
	CHAR_T ch;
	u_long sw, ts;
	size_t cno, current, spaces, target, tabs;
	int ai_reset;

	ts = O_VAL(sp, O_TABSTOP);
	sw = O_VAL(sp, O_SHIFTWIDTH);

	/*
	 * Since we don't know what precedes the character(s) being inserted
	 * (or deleted), the preceding whitespace characters must be resolved.
	 * An example is a <tab>, which doesn't need a full shiftwidth number
	 * of columns because it's preceded by <space>s.  This is easy to get
	 * if the user sets shiftwidth to a value less than tabstop (or worse,
	 * something for which tabstop isn't a multiple) and then uses ^T to
	 * indent, and ^D to outdent.
	 *
	 * Figure out the current and target screen columns.  In the historic
	 * vi, the autoindent column was NOT determined using display widths
	 * of characters as was the wrapmargin column.  For that reason, we
	 * can't use the vs_column() function, but have to calculate it here.
	 * This is slow, but it's normally only on the first few characters of
	 * a line.
	 */
	for (current = cno = 0; cno < tp->cno; ++cno)
		current += tp->lb[cno] == '\t' ?
		    COL_OFF(current, ts) : KEY_COL(sp, tp->lb[cno]);

	target = current;
	if (isindent)
		target += COL_OFF(target, sw);
	else {
		--target;
		target -= target % sw;
	}

	/*
	 * The AI characters will be turned into overwrite characters if the
	 * cursor immediately follows them.  We test both the cursor position
	 * and the indent flag because there's no single test.  (^T can only
	 * be detected by the cursor position, and while we know that the test
	 * is always true for ^D, the cursor can be in more than one place, as
	 * "0^D" and "^D" are different.)
	 */
	ai_reset = !isindent || tp->cno == tp->ai + tp->offset;

	/*
	 * Back up over any previous <blank> characters, changing them into
	 * overwrite characters (including any ai characters).  Then figure
	 * out the current screen column.
	 */
	for (; tp->cno > tp->offset &&
	    (tp->lb[tp->cno - 1] == ' ' || tp->lb[tp->cno - 1] == '\t');
	    --tp->cno, ++tp->owrite);
	for (current = cno = 0; cno < tp->cno; ++cno)
		current += tp->lb[cno] == '\t' ?
		    COL_OFF(current, ts) : KEY_COL(sp, tp->lb[cno]);

	/*
	 * If we didn't move up to or past the target, it's because there
	 * weren't enough characters to delete, e.g. the first character
	 * of the line was a tp->offset character, and the user entered
	 * ^D to move to the beginning of a line.  An example of this is:
	 *
	 *	:set ai sw=4<cr>i<space>a<esc>i^T^D
	 *
	 * Otherwise, count up the total spaces/tabs needed to get from the
	 * beginning of the line (or the last non-<blank> character) to the
	 * target.
	 */
	if (current >= target)
		spaces = tabs = 0;
	else {
		for (cno = current,
		    tabs = 0; cno + COL_OFF(cno, ts) <= target; ++tabs)
			cno += COL_OFF(cno, ts);
		spaces = target - cno;
	}

	/* If we overwrote ai characters, reset the ai count. */
	if (ai_reset)
		tp->ai = tabs + spaces;

	/*
	 * Call txt_insch() to insert each character, so that we get the
	 * correct effect when we add a <tab> to replace N <spaces>.
	 */
	for (ch = '\t'; tabs > 0; --tabs)
		(void)txt_insch(sp, tp, &ch, 0);
	for (ch = ' '; spaces > 0; --spaces)
		(void)txt_insch(sp, tp, &ch, 0);
	return (0);
}

/*
 * txt_fc --
 *	File name and ex command completion.
 */
static int
txt_fc(SCR *sp, TEXT *tp, int *redrawp)
{
	struct stat sb;
	ARGS **argv;
	EXCMD cmd;
	size_t indx, len, nlen, off;
	int argc;
	CHAR_T *p, *t, *bp;
	char *np, *epd = NULL;
	size_t nplen;
	int fstwd = 1;

	*redrawp = 0;
	ex_cinit(sp, &cmd, 0, 0, OOBLNO, OOBLNO, 0);

	/*
	 * Find the beginning of this "word" -- if we're at the beginning
	 * of the line, it's a special case.
	 */
	if (tp->cno == 1) {
		len = 0;
		p = tp->lb;
	} else {
		CHAR_T *ap;

		for (len = 0,
		    off = MAX(tp->ai, tp->offset), ap = tp->lb + off, p = ap;
		    off < tp->cno; ++off, ++ap) {
			if (IS_ESCAPE(sp, &cmd, *ap)) {
				if (++off == tp->cno)
					break;
				++ap;
				len += 2;
			} else if (cmdskip(*ap)) {
				p = ap + 1;
				if (len > 0)
					fstwd = 0;
				len = 0;
			} else
				++len;
		}
	}

	/*
	 * If we are at the first word, do ex command completion instead of
	 * file name completion.
	 */
	if (fstwd)
		(void)argv_flt_ex(sp, &cmd, p, len);
	else {
		if ((bp = argv_uesc(sp, &cmd, p, len)) == NULL)
			return (1);
		if (argv_flt_path(sp, &cmd, bp, STRLEN(bp))) {
			FREE_SPACEW(sp, bp, 0);
			return (0);
		}
		FREE_SPACEW(sp, bp, 0);
	}
	argc = cmd.argc;
	argv = cmd.argv;

	switch (argc) {
	case 0:				/* No matches. */
		(void)sp->gp->scr_bell(sp);
		return (0);
	case 1:				/* One match. */
		/* Always overwrite the old text. */
		nlen = STRLEN(cmd.argv[0]->bp);
		break;
	default:			/* Multiple matches. */
		*redrawp = 1;
		if (txt_fc_col(sp, argc, argv))
			return (1);

		/* Find the length of the shortest match. */
		for (nlen = cmd.argv[0]->len; --argc > 0;) {
			if (cmd.argv[argc]->len < nlen)
				nlen = cmd.argv[argc]->len;
			for (indx = 0; indx < nlen &&
			    cmd.argv[argc]->bp[indx] == cmd.argv[0]->bp[indx];
			    ++indx);
			nlen = indx;
		}
		break;
	}

	/* Escape the matched part of the path. */
	if (fstwd)
		bp = cmd.argv[0]->bp;
	else {
		if ((bp = argv_esc(sp, &cmd, cmd.argv[0]->bp, nlen)) == NULL)
			return (1);
		nlen = STRLEN(bp);
	}

	/* Overwrite the expanded text first. */
	for (t = bp; len > 0 && nlen > 0; --len, --nlen)
		*p++ = *t++;

	/* If lost text, make the remaining old text overwrite characters. */
	if (len) {
		tp->cno -= len;
		tp->owrite += len;
	}

	/* Overwrite any overwrite characters next. */
	for (; nlen > 0 && tp->owrite > 0; --nlen, --tp->owrite, ++tp->cno)
		*p++ = *t++;

	/* Shift remaining text up, and move the cursor to the end. */
	if (nlen) {
		off = p - tp->lb;
		BINC_RETW(sp, tp->lb, tp->lb_len, tp->len + nlen);
		p = tp->lb + off;

		tp->cno += nlen;
		tp->len += nlen;

		if (tp->insert != 0)
			(void)MEMMOVE(p + nlen, p, tp->insert);
		while (nlen--)
			*p++ = *t++;
	}

	if (!fstwd)
		FREE_SPACEW(sp, bp, 0);

	/* If not a single match of path, we've done. */
	if (argc != 1 || fstwd)
		return (0);

	/* If a single match and it's a directory, append a '/'. */
	INT2CHAR(sp, cmd.argv[0]->bp, cmd.argv[0]->len + 1, np, nplen);
	if ((epd = expanduser(np)) != NULL)
		np = epd;
	if (!stat(np, &sb) && S_ISDIR(sb.st_mode)) {
		if (tp->owrite == 0) {
			off = p - tp->lb;
			BINC_RETW(sp, tp->lb, tp->lb_len, tp->len + 1);
			p = tp->lb + off;
			if (tp->insert != 0)
				(void)MEMMOVE(p + 1, p, tp->insert);
			++tp->len;
		} else
			--tp->owrite;

		++tp->cno;
		*p++ = '/';
	}
	free(epd);
	return (0);
}

/*
 * txt_fc_col --
 *	Display file names for file name completion.
 */
static int
txt_fc_col(SCR *sp, int argc, ARGS **argv)
{
	ARGS **av;
	CHAR_T *p;
	GS *gp;
	size_t base, cnt, col, colwidth, numrows, numcols, prefix, row;
	int ac, nf, reset;
	char *np, *pp;
	size_t nlen;

	gp = sp->gp;

	/* Trim any directory prefix common to all of the files. */
	INT2CHAR(sp, argv[0]->bp, argv[0]->len + 1, np, nlen);
	if ((pp = strrchr(np, '/')) == NULL)
		prefix = 0;
	else {
		prefix = (pp - np) + 1;
		for (ac = argc - 1, av = argv + 1; ac > 0; --ac, ++av)
			if (av[0]->len < prefix ||
			    MEMCMP(av[0]->bp, argv[0]->bp, 
				   prefix)) {
				prefix = 0;
				break;
			}
	}

	/*
	 * Figure out the column width for the longest name.  Output is done on
	 * 6 character "tab" boundaries for no particular reason.  (Since we
	 * don't output tab characters, we ignore the terminal's tab settings.)
	 * Ignore the user's tab setting because we have no idea how reasonable
	 * it is.
	 */
	for (ac = argc, av = argv, colwidth = 0; ac > 0; --ac, ++av) {
		for (col = 0, p = av[0]->bp + prefix; *p != '\0'; ++p)
			col += KEY_COL(sp, *p);
		if (col > colwidth)
			colwidth = col;
	}
	colwidth += COL_OFF(colwidth, 6);

	/*
	 * Writing to the bottom line of the screen is always turned off when
	 * SC_TINPUT_INFO is set.  Turn it back on, we know what we're doing.
	 */
	if (F_ISSET(sp, SC_TINPUT_INFO)) {
		reset = 1;
		F_CLR(sp, SC_TINPUT_INFO);
	} else
		reset = 0;

#define	CHK_INTR							\
	if (F_ISSET(gp, G_INTERRUPTED))					\
		goto intr;

	/* If the largest file name is too large, just print them. */
	if (colwidth >= sp->cols) {
		for (ac = argc, av = argv; ac > 0; --ac, ++av) {
			INT2CHAR(sp, av[0]->bp+prefix, av[0]->len+1-prefix,
				 np, nlen);
			pp = msg_print(sp, np, &nf);
			(void)ex_printf(sp, "%s\n", pp);
			if (nf)
				FREE_SPACE(sp, pp, 0);
			if (F_ISSET(gp, G_INTERRUPTED))
				break;
		}
		CHK_INTR;
	} else {
		/* Figure out the number of columns. */
		numcols = (sp->cols - 1) / colwidth;
		if (argc > numcols) {
			numrows = argc / numcols;
			if (argc % numcols)
				++numrows;
		} else
			numrows = 1;

		/* Display the files in sorted order. */
		for (row = 0; row < numrows; ++row) {
			for (base = row, col = 0; col < numcols; ++col) {
				INT2CHAR(sp, argv[base]->bp+prefix, 
					argv[base]->len+1-prefix, np, nlen);
				pp = msg_print(sp, np, &nf);
				cnt = ex_printf(sp, "%s", pp);
				if (nf)
					FREE_SPACE(sp, pp, 0);
				CHK_INTR;
				if ((base += numrows) >= argc)
					break;
				(void)ex_printf(sp,
				    "%*s", (int)(colwidth - cnt), "");
				CHK_INTR;
			}
			(void)ex_puts(sp, "\n");
			CHK_INTR;
		}
		(void)ex_puts(sp, "\n");
		CHK_INTR;
	}
	(void)ex_fflush(sp);

	if (0) {
intr:		F_CLR(gp, G_INTERRUPTED);
	}
	if (reset)
		F_SET(sp, SC_TINPUT_INFO);

	return (0);
}

/*
 * txt_emark --
 *	Set the end mark on the line.
 */
static int
txt_emark(SCR *sp, TEXT *tp, size_t cno)
{
	CHAR_T ch;
	u_char *kp;
	size_t chlen, nlen, olen;
	CHAR_T *p;

	ch = CH_ENDMARK;

	/*
	 * The end mark may not be the same size as the current character.
	 * Don't let the line shift.
	 */
	nlen = KEY_COL(sp, ch);
	if (tp->lb[cno] == '\t')
		(void)vs_columns(sp, tp->lb, tp->lno, &cno, &olen);
	else
		olen = KEY_COL(sp, tp->lb[cno]);

	/*
	 * If the line got longer, well, it's weird, but it's easy.  If
	 * it's the same length, it's easy.  If it got shorter, we have
	 * to fix it up.
	 */
	if (olen > nlen) {
		BINC_RETW(sp, tp->lb, tp->lb_len, tp->len + olen);
		chlen = olen - nlen;
		if (tp->insert != 0)
			MEMMOVE(tp->lb + cno + 1 + chlen,
			    tp->lb + cno + 1, tp->insert);

		tp->len += chlen;
		tp->owrite += chlen;
		p = tp->lb + cno;
		if (tp->lb[cno] == '\t' ||
		    KEY_NEEDSWIDE(sp, tp->lb[cno]))
			for (cno += chlen; chlen--;)
				*p++ = ' ';
		else
			for (kp = (u_char *)
			    KEY_NAME(sp, tp->lb[cno]),
			    cno += chlen; chlen--;)
				*p++ = *kp++;
	}
	tp->lb[cno] = ch;
	return (vs_change(sp, tp->lno, LINE_RESET));
}

/*
 * txt_err --
 *	Handle an error during input processing.
 */
static void
txt_err(SCR *sp, TEXTH *tiqh)
{
	recno_t lno;

	/*
	 * The problem with input processing is that the cursor is at an
	 * indeterminate position since some input may have been lost due
	 * to a malloc error.  So, try to go back to the place from which
	 * the cursor started, knowing that it may no longer be available.
	 *
	 * We depend on at least one line number being set in the text
	 * chain.
	 */
	for (lno = TAILQ_FIRST(tiqh)->lno;
	    !db_exist(sp, lno) && lno > 0; --lno);

	sp->lno = lno == 0 ? 1 : lno;
	sp->cno = 0;

	/* Redraw the screen, just in case. */
	F_SET(sp, SC_SCR_REDRAW);
}

/*
 * txt_hex --
 *	Let the user insert any character value they want.
 *
 * !!!
 * This is an extension.  The pattern "^X[0-9a-fA-F]*" is a way
 * for the user to specify a character value which their keyboard
 * may not be able to enter.
 */
static int
txt_hex(SCR *sp, TEXT *tp)
{
	CHAR_T savec;
	size_t len, off;
	u_long value;
	CHAR_T *p, *wp;

	/*
	 * Null-terminate the string.  Since nul isn't a legal hex value,
	 * this should be okay, and lets us use a local routine, which
	 * presumably understands the character set, to convert the value.
	 */
	savec = tp->lb[tp->cno];
	tp->lb[tp->cno] = 0;

	/* Find the previous CH_HEX character. */
	for (off = tp->cno - 1, p = tp->lb + off, len = 0;; --p, --off, ++len) {
		if (*p == CH_HEX) {
			wp = p + 1;
			break;
		}
		/* Not on this line?  Shouldn't happen. */
		if (off == tp->ai || off == tp->offset)
			goto nothex;
	}

	/* If length of 0, then it wasn't a hex value. */
	if (len == 0)
		goto nothex;

	/* Get the value. */
	errno = 0;
	value = STRTOL(wp, NULL, 16);
	if (errno || value > UCHAR_MAX) {
nothex:		tp->lb[tp->cno] = savec;
		return (0);
	}

	/* Restore the original character. */
	tp->lb[tp->cno] = savec;

	/* Adjust the bookkeeping. */
	tp->cno -= len;
	tp->len -= len;
	tp->lb[tp->cno - 1] = value;

	/* Copy down any overwrite characters. */
	if (tp->owrite)
		MEMMOVE(tp->lb + tp->cno, tp->lb + tp->cno + len, 
		    tp->owrite);

	/* Copy down any insert characters. */
	if (tp->insert)
		MEMMOVE(tp->lb + tp->cno + tp->owrite,
		    tp->lb + tp->cno + tp->owrite + len, 
		    tp->insert);

	return (0);
}

/*
 * txt_insch --
 *
 * !!!
 * Historic vi did a special screen optimization for tab characters.  As an
 * example, for the keystrokes "iabcd<esc>0C<tab>", the tab overwrote the
 * rest of the string when it was displayed.
 *
 * Because early versions of this implementation redisplayed the entire line
 * on each keystroke, the "bcd" was pushed to the right as it ignored that
 * the user had "promised" to change the rest of the characters.  However,
 * the historic vi implementation had an even worse bug: given the keystrokes
 * "iabcd<esc>0R<tab><esc>", the "bcd" disappears, and magically reappears
 * on the second <esc> key.
 *
 * POSIX 1003.2 requires (will require) that this be fixed, specifying that
 * vi overwrite characters the user has committed to changing, on the basis
 * of the screen space they require, but that it not overwrite other characters.
 */
static int
txt_insch(SCR *sp, TEXT *tp, CHAR_T *chp, u_int flags)
{
	u_char *kp;
	CHAR_T savech;
	size_t chlen, cno, copydown, olen, nlen;
	CHAR_T *p;

	/*
	 * The 'R' command does one-for-one replacement, because there's
	 * no way to know how many characters the user intends to replace.
	 */
	if (LF_ISSET(TXT_REPLACE)) {
		if (tp->owrite) {
			--tp->owrite;
			tp->lb[tp->cno++] = *chp;
			return (0);
		}
	} else if (tp->owrite) {		/* Overwrite a character. */
		cno = tp->cno;

		/*
		 * If the old or new characters are tabs, then the length of the
		 * display depends on the character position in the display.  We
		 * don't even try to handle this here, just ask the screen.
		 */
		if (*chp == '\t') {
			savech = tp->lb[cno];
			tp->lb[cno] = '\t';
			(void)vs_columns(sp, tp->lb, tp->lno, &cno, &nlen);
			tp->lb[cno] = savech;
		} else
			nlen = KEY_COL(sp, *chp);

		/*
		 * Eat overwrite characters until we run out of them or we've
		 * handled the length of the new character.  If we only eat
		 * part of an overwrite character, break it into its component
		 * elements and display the remaining components.
		 */
		for (copydown = 0; nlen != 0 && tp->owrite != 0;) {
			--tp->owrite;

			if (tp->lb[cno] == '\t')
				(void)vs_columns(sp,
				    tp->lb, tp->lno, &cno, &olen);
			else
				olen = KEY_COL(sp, tp->lb[cno]);

			if (olen == nlen) {
				nlen = 0;
				break;
			}
			if (olen < nlen) {
				++copydown;
				nlen -= olen;
			} else {
				BINC_RETW(sp,
				    tp->lb, tp->lb_len, tp->len + olen);
				chlen = olen - nlen;
				MEMMOVE(tp->lb + cno + 1 + chlen,
				    tp->lb + cno + 1, 
				    tp->owrite + tp->insert);

				tp->len += chlen;
				tp->owrite += chlen;
				if (tp->lb[cno] == '\t' ||
				   KEY_NEEDSWIDE(sp, tp->lb[cno]))
					for (p = tp->lb + cno + 1; chlen--;)
						*p++ = ' ';
				else
					for (kp = (u_char *)
					    KEY_NAME(sp, tp->lb[cno]) + nlen,
					    p = tp->lb + cno + 1; chlen--;)
						*p++ = *kp++;
				nlen = 0;
				break;
			}
		}

		/*
		 * If had to erase several characters, we adjust the total
		 * count, and if there are any characters left, shift them
		 * into position.
		 */
		if (copydown != 0 && (tp->len -= copydown) != 0)
			MEMMOVE(tp->lb + cno, tp->lb + cno + copydown,
			    tp->owrite + tp->insert + copydown);

		/* If we had enough overwrite characters, we're done. */
		if (nlen == 0) {
			tp->lb[tp->cno++] = *chp;
			return (0);
		}
	}

	/* Check to see if the character fits into the input buffer. */
	BINC_RETW(sp, tp->lb, tp->lb_len, tp->len + 1);

	++tp->len;
	if (tp->insert) {			/* Insert a character. */
		if (tp->insert == 1)
			tp->lb[tp->cno + 1] = tp->lb[tp->cno];
		else
			MEMMOVE(tp->lb + tp->cno + 1,
			    tp->lb + tp->cno, tp->owrite + tp->insert);
	}
	tp->lb[tp->cno++] = *chp;
	return (0);
}

/*
 * txt_isrch --
 *	Do an incremental search.
 */
static int
txt_isrch(SCR *sp, VICMD *vp, TEXT *tp, u_int8_t *is_flagsp)
{
	MARK start;
	recno_t lno;
	u_int sf;

	/* If it's a one-line screen, we don't do incrementals. */
	if (IS_ONELINE(sp)) {
		FL_CLR(*is_flagsp, IS_RUNNING);
		return (0);
	}

	/*
	 * If the user erases back to the beginning of the buffer, there's
	 * nothing to search for.  Reset the cursor to the starting point.
	 */
	if (tp->cno <= 1) {
		vp->m_final = vp->m_start;
		return (0);
	}

	/*
	 * If it's an RE quote character, and not quoted, ignore it until
	 * we get another character.
	 */
	if (tp->lb[tp->cno - 1] == '\\' &&
	    (tp->cno == 2 || tp->lb[tp->cno - 2] != '\\'))
		return (0);

	/*
	 * If it's a magic shell character, and not quoted, reset the cursor
	 * to the starting point.
	 */
	if (IS_SHELLMETA(sp, tp->lb[tp->cno - 1]) &&
	    (tp->cno == 2 || tp->lb[tp->cno - 2] != '\\'))
		vp->m_final = vp->m_start;

	/*
	 * If we see the search pattern termination character, then quit doing
	 * an incremental search.  There may be more, e.g., ":/foo/;/bar/",
	 * and we can't handle that incrementally.  Also, reset the cursor to
	 * the original location, the ex search routines don't know anything
	 * about incremental searches.
	 */
	if (tp->lb[0] == tp->lb[tp->cno - 1] &&
	    (tp->cno == 2 || tp->lb[tp->cno - 2] != '\\')) {
		vp->m_final = vp->m_start;
		FL_CLR(*is_flagsp, IS_RUNNING);
		return (0);
	}
		
	/*
	 * Remember the input line and discard the special input map,
	 * but don't overwrite the input line on the screen.
	 */
	lno = tp->lno;
	F_SET(VIP(sp), VIP_S_MODELINE);
	F_CLR(sp, SC_TINPUT | SC_TINPUT_INFO);
	if (txt_map_end(sp))
		return (1);

	/*
	 * Specify a starting point and search.  If we find a match, move to
	 * it and refresh the screen.  If we didn't find the match, then we
	 * beep the screen.  When searching from the original cursor position, 
	 * we have to move the cursor, otherwise, we don't want to move the
	 * cursor in case the text at the current position continues to match.
	 */
	if (FL_ISSET(*is_flagsp, IS_RESTART)) {
		start = vp->m_start;
		sf = SEARCH_SET;
	} else {
		start = vp->m_final;
		sf = SEARCH_INCR | SEARCH_SET;
	}

	if (tp->lb[0] == '/' ?
	    !f_search(sp,
	    &start, &vp->m_final, tp->lb + 1, tp->cno - 1, NULL, sf) :
	    !b_search(sp,
	    &start, &vp->m_final, tp->lb + 1, tp->cno - 1, NULL, sf)) {
		sp->lno = vp->m_final.lno;
		sp->cno = vp->m_final.cno;
		FL_CLR(*is_flagsp, IS_RESTART);

		if (!KEYS_WAITING(sp) && vs_refresh(sp, 0))
			return (1);
	} else
		FL_SET(*is_flagsp, IS_RESTART);

	/* Reinstantiate the special input map. */
	if (txt_map_init(sp))
		return (1);
	F_CLR(VIP(sp), VIP_S_MODELINE);
	F_SET(sp, SC_TINPUT | SC_TINPUT_INFO);

	/* Reset the line number of the input line. */
	tp->lno = TMAP[0].lno; 

	/*
	 * If the colon command-line moved, i.e. the screen scrolled,
	 * refresh the input line.
	 *
	 * XXX
	 * We shouldn't be calling vs_line, here -- we need dirty bits
	 * on entries in the SMAP array.
	 */
	if (lno != TMAP[0].lno) {
		if (vs_line(sp, &TMAP[0], NULL, NULL))
			return (1);
		(void)sp->gp->scr_refresh(sp, 0);
	}
	return (0);
}

/*
 * txt_resolve --
 *	Resolve the input text chain into the file.
 */
static int
txt_resolve(SCR *sp, TEXTH *tiqh, u_int32_t flags)
{
	VI_PRIVATE *vip;
	TEXT *tp;
	recno_t lno;
	int changed;

	/*
	 * The first line replaces a current line, and all subsequent lines
	 * are appended into the file.  Resolve autoindented characters for
	 * each line before committing it.  If the latter causes the line to
	 * change, we have to redisplay it, otherwise the information cached
	 * about the line will be wrong.
	 */
	vip = VIP(sp);
	tp = TAILQ_FIRST(tiqh);

	if (LF_ISSET(TXT_AUTOINDENT))
		txt_ai_resolve(sp, tp, &changed);
	else
		changed = 0;
	if (db_set(sp, tp->lno, tp->lb, tp->len) ||
	    (changed && vs_change(sp, tp->lno, LINE_RESET)))
		return (1);

	for (lno = tp->lno; (tp = TAILQ_NEXT(tp, q)) != NULL; ++lno) {
		if (LF_ISSET(TXT_AUTOINDENT))
			txt_ai_resolve(sp, tp, &changed);
		else
			changed = 0;
		if (db_append(sp, 0, lno, tp->lb, tp->len) ||
		    (changed && vs_change(sp, tp->lno, LINE_RESET)))
			return (1);
	}

	/*
	 * Clear the input flag, the look-aside buffer is no longer valid.
	 * Has to be done as part of text resolution, or upon return we'll
	 * be looking at incorrect data.
	 */
	F_CLR(sp, SC_TINPUT);

	return (0);
}

/*
 * txt_showmatch --
 *	Show a character match.
 *
 * !!!
 * Historic vi tried to display matches even in the :colon command line.
 * I think not.
 */
static int
txt_showmatch(SCR *sp, TEXT *tp)
{
	GS *gp;
	VCS cs;
	MARK m;
	int cnt, endc, startc;

	gp = sp->gp;

	/*
	 * Do a refresh first, in case we haven't done one in awhile,
	 * so the user can see what we're complaining about.
	 */
	UPDATE_POSITION(sp, tp);
	if (vs_refresh(sp, 1))
		return (1);

	/*
	 * We don't display the match if it's not on the screen.  Find
	 * out what the first character on the screen is.
	 */
	if (vs_sm_position(sp, &m, 0, P_TOP))
		return (1);

	/* Initialize the getc() interface. */
	cs.cs_lno = tp->lno;
	cs.cs_cno = tp->cno - 1;
	if (cs_init(sp, &cs))
		return (1);
	startc = STRCHR(VIP(sp)->mcs, endc = cs.cs_ch)[-1];

	/* Search for the match. */
	for (cnt = 1;;) {
		if (cs_prev(sp, &cs))
			return (1);
		if (cs.cs_flags != 0) {
			if (cs.cs_flags == CS_EOF || cs.cs_flags == CS_SOF) {
				msgq(sp, M_BERR,
				    "Unmatched %s", KEY_NAME(sp, endc));
				return (0);
			}
			continue;
		}
		if (cs.cs_ch == endc)
			++cnt;
		else if (cs.cs_ch == startc && --cnt == 0)
			break;
	}

	/* If the match is on the screen, move to it. */
	if (cs.cs_lno < m.lno || (cs.cs_lno == m.lno && cs.cs_cno < m.cno))
		return (0);
	sp->lno = cs.cs_lno;
	sp->cno = cs.cs_cno;
	if (vs_refresh(sp, 1))
		return (1);

	/* Wait for timeout or character arrival. */
	return (v_event_get(sp,
	    NULL, O_VAL(sp, O_MATCHTIME) * 100, EC_TIMEOUT));
}

/*
 * txt_margin --
 *	Handle margin wrap.
 */
static int
txt_margin(SCR *sp, TEXT *tp, TEXT *wmtp, int *didbreak, u_int32_t flags)
{
	VI_PRIVATE *vip;
	size_t len, off;
	CHAR_T *p, *wp;

	/* Find the nearest previous blank. */
	for (off = tp->cno - 1, p = tp->lb + off, len = 0;; --off, --p, ++len) {
		if (isblank(*p)) {
			wp = p + 1;
			break;
		}

		/*
		 * If reach the start of the line, there's nowhere to break.
		 *
		 * !!!
		 * Historic vi belled each time a character was entered after
		 * crossing the margin until a space was entered which could
		 * be used to break the line.  I don't as it tends to wake the
		 * cats.
		 */
		if (off == tp->ai || off == tp->offset) {
			*didbreak = 0;
			return (0);
		}
	}

	/*
	 * Store saved information about the rest of the line in the
	 * wrapmargin TEXT structure.
	 *
	 * !!!
	 * The offset field holds the length of the current characters
	 * that the user entered, but which are getting split to the new
	 * line -- it's going to be used to set the cursor value when we
	 * move to the new line.
	 */
	vip = VIP(sp);
	wmtp->lb = p + 1;
	wmtp->offset = len;
	wmtp->insert = LF_ISSET(TXT_APPENDEOL) ?  tp->insert - 1 : tp->insert;
	wmtp->owrite = tp->owrite;

	/* Correct current bookkeeping information. */
	tp->cno -= len;
	if (LF_ISSET(TXT_APPENDEOL)) {
		tp->len -= len + tp->owrite + (tp->insert - 1);
		tp->insert = 1;
	} else {
		tp->len -= len + tp->owrite + tp->insert;
		tp->insert = 0;
	}
	tp->owrite = 0;

	/*
	 * !!!
	 * Delete any trailing whitespace from the current line.
	 */
	for (;; --p, --off) {
		if (!isblank(*p))
			break;
		--tp->cno;
		--tp->len;
		if (off == tp->ai || off == tp->offset)
			break;
	}
	*didbreak = 1;
	return (0);
}

/*
 * txt_Rresolve --
 *	Resolve the input line for the 'R' command.
 */
static void
txt_Rresolve(SCR *sp, TEXTH *tiqh, TEXT *tp, const size_t orig_len)
{
	TEXT *ttp;
	size_t input_len, retain;
	CHAR_T *p;

	/*
	 * Check to make sure that the cursor hasn't moved beyond
	 * the end of the line.
	 */
	if (tp->owrite == 0)
		return;

	/*
	 * Calculate how many characters the user has entered,
	 * plus the blanks erased by <carriage-return>/<newline>s.
	 */
	for (ttp = TAILQ_FIRST(tiqh), input_len = 0;;) {
		input_len += ttp == tp ? tp->cno : ttp->len + ttp->R_erase;
		if ((ttp = TAILQ_NEXT(ttp, q)) == NULL)
			break;
	}

	/*
	 * If the user has entered less characters than the original line
	 * was long, restore any overwriteable characters to the original
	 * characters.  These characters are entered as "insert characters",
	 * because they're after the cursor and we don't want to lose them.
	 * (This is okay because the R command has no insert characters.)
	 * We set owrite to 0 so that the insert characters don't get copied
	 * to somewhere else, which means that the line and the length have
	 * to be adjusted here as well.
	 *
	 * We have to retrieve the original line because the original pinned
	 * page has long since been discarded.  If it doesn't exist, that's
	 * okay, the user just extended the file.
	 */
	if (input_len < orig_len) {
		retain = MIN(tp->owrite, orig_len - input_len);
		if (db_get(sp,
		    TAILQ_FIRST(tiqh)->lno, DBG_FATAL | DBG_NOCACHE, &p, NULL))
			return;
		MEMCPY(tp->lb + tp->cno, p + input_len, retain);
		tp->len -= tp->owrite - retain;
		tp->owrite = 0;
		tp->insert += retain;
	}
}

/*
 * txt_nomorech --
 *	No more characters message.
 */
static void
txt_nomorech(SCR *sp)
{
	msgq(sp, M_BERR, "194|No more characters to erase");
}
