/*-
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * David Hitz of Auspex Systems, Inc.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#ifndef lint
static const char sccsid[] = "$Id: ex_tag.c,v 10.54 2012/04/12 07:17:30 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/stat.h>

#include <bitstring.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/common.h"
#include "../vi/vi.h"
#include "tag.h"

static char	*binary_search(char *, char *, char *);
static int	 compare(char *, char *, char *);
static void	 ctag_file(SCR *, TAGF *, char *, char **, size_t *);
static int	 ctag_search(SCR *, CHAR_T *, size_t, char *);
static int	 ctag_sfile(SCR *, TAGF *, TAGQ *, char *);
static TAGQ	*ctag_slist(SCR *, CHAR_T *);
static char	*linear_search(char *, char *, char *, long);
static int	 tag_copy(SCR *, TAG *, TAG **);
static int	 tag_pop(SCR *, TAGQ *, int);
static int	 tagf_copy(SCR *, TAGF *, TAGF **);
static int	 tagf_free(SCR *, TAGF *);
static int	 tagq_copy(SCR *, TAGQ *, TAGQ **);

/*
 * ex_tag_first --
 *	The tag code can be entered from main, e.g., "vi -t tag".
 *
 * PUBLIC: int ex_tag_first(SCR *, CHAR_T *);
 */
int
ex_tag_first(SCR *sp, CHAR_T *tagarg)
{
	EXCMD cmd;

	/* Build an argument for the ex :tag command. */
	ex_cinit(sp, &cmd, C_TAG, 0, OOBLNO, OOBLNO, 0);
	argv_exp0(sp, &cmd, tagarg, STRLEN(tagarg));

	/*
	 * XXX
	 * Historic vi went ahead and created a temporary file when it failed
	 * to find the tag.  We match historic practice, but don't distinguish
	 * between real error and failure to find the tag.
	 */
	if (ex_tag_push(sp, &cmd))
		return (0);

	/* Display tags in the center of the screen. */
	F_CLR(sp, SC_SCR_TOP);
	F_SET(sp, SC_SCR_CENTER);

	return (0);
}

/*
 * ex_tag_push -- ^]
 *		  :tag[!] [string]
 *
 * Enter a new TAGQ context based on a ctag string.
 *
 * PUBLIC: int ex_tag_push(SCR *, EXCMD *);
 */
int
ex_tag_push(SCR *sp, EXCMD *cmdp)
{
	EX_PRIVATE *exp;
	TAGQ *tqp;
	long tl;

	exp = EXP(sp);
	switch (cmdp->argc) {
	case 1:
		if (exp->tag_last != NULL)
			free(exp->tag_last);

		if ((exp->tag_last = v_wstrdup(sp, cmdp->argv[0]->bp,
		    cmdp->argv[0]->len)) == NULL) {
			msgq(sp, M_SYSERR, NULL);
			return (1);
		}

		/* Taglength may limit the number of characters. */
		if ((tl =
		    O_VAL(sp, O_TAGLENGTH)) != 0 && STRLEN(exp->tag_last) > tl)
			exp->tag_last[tl] = '\0';
		break;
	case 0:
		if (exp->tag_last == NULL) {
			msgq(sp, M_ERR, "158|No previous tag entered");
			return (1);
		}
		break;
	default:
		abort();
	}

	/* Get the tag information. */
	if ((tqp = ctag_slist(sp, exp->tag_last)) == NULL)
		return (1);

	if (tagq_push(sp, tqp, F_ISSET(cmdp, E_NEWSCREEN), 
			       FL_ISSET(cmdp->iflags, E_C_FORCE)))
		return 1;

	return 0;
}

/* 
 * ex_tag_next --
 *	Switch context to the next TAG.
 *
 * PUBLIC: int ex_tag_next(SCR *, EXCMD *);
 */
int
ex_tag_next(SCR *sp, EXCMD *cmdp)
{
	EX_PRIVATE *exp;
	TAG *tp;
	TAGQ *tqp;
	char *np;
	size_t nlen;

	exp = EXP(sp);
	if ((tqp = TAILQ_FIRST(exp->tq)) == NULL) {
		tag_msg(sp, TAG_EMPTY, NULL);
		return (1);
	}
	if ((tp = TAILQ_NEXT(tqp->current, q)) == NULL) {
		msgq(sp, M_ERR, "282|Already at the last tag of this group");
		return (1);
	}
	if (ex_tag_nswitch(sp, tp, FL_ISSET(cmdp->iflags, E_C_FORCE)))
		return (1);
	tqp->current = tp;

	if (F_ISSET(tqp, TAG_CSCOPE))
		(void)cscope_search(sp, tqp, tp);
	else
		(void)ctag_search(sp, tp->search, tp->slen, tqp->tag);
	if (tqp->current->msg) {
	    INT2CHAR(sp, tqp->current->msg, tqp->current->mlen + 1,
		     np, nlen);
	    msgq(sp, M_INFO, "%s", np);
	}
	return (0);
}

/* 
 * ex_tag_prev --
 *	Switch context to the next TAG.
 *
 * PUBLIC: int ex_tag_prev(SCR *, EXCMD *);
 */
int
ex_tag_prev(SCR *sp, EXCMD *cmdp)
{
	EX_PRIVATE *exp;
	TAG *tp;
	TAGQ *tqp;
	char *np;
	size_t nlen;

	exp = EXP(sp);
	if ((tqp = TAILQ_FIRST(exp->tq)) == NULL) {
		tag_msg(sp, TAG_EMPTY, NULL);
		return (0);
	}
	if ((tp = TAILQ_PREV(tqp->current, _tagqh, q)) == NULL) {
		msgq(sp, M_ERR, "255|Already at the first tag of this group");
		return (1);
	}
	if (ex_tag_nswitch(sp, tp, FL_ISSET(cmdp->iflags, E_C_FORCE)))
		return (1);
	tqp->current = tp;

	if (F_ISSET(tqp, TAG_CSCOPE))
		(void)cscope_search(sp, tqp, tp);
	else
		(void)ctag_search(sp, tp->search, tp->slen, tqp->tag);
	if (tqp->current->msg) {
	    INT2CHAR(sp, tqp->current->msg, tqp->current->mlen + 1,
		     np, nlen);
	    msgq(sp, M_INFO, "%s", np);
	}
	return (0);
}

/*
 * ex_tag_nswitch --
 *	Switch context to the specified TAG.
 *
 * PUBLIC: int ex_tag_nswitch(SCR *, TAG *, int);
 */
int
ex_tag_nswitch(SCR *sp, TAG *tp, int force)
{
	/* Get a file structure. */
	if (tp->frp == NULL && (tp->frp = file_add(sp, tp->fname)) == NULL)
		return (1);

	/* If not changing files, return, we're done. */
	if (tp->frp == sp->frp)
		return (0);

	/* Check for permission to leave. */
	if (file_m1(sp, force, FS_ALL | FS_POSSIBLE))
		return (1);

	/* Initialize the new file. */
	if (file_init(sp, tp->frp, NULL, FS_SETALT))
		return (1);

	/* Display tags in the center of the screen. */
	F_CLR(sp, SC_SCR_TOP);
	F_SET(sp, SC_SCR_CENTER);

	/* Switch. */
	F_SET(sp, SC_FSWITCH);
	return (0);
}

/*
 * ex_tag_Nswitch --
 *	Switch context to the specified TAG in a new screen.
 *
 * PUBLIC: int ex_tag_Nswitch(SCR *, TAG *, int);
 */
int
ex_tag_Nswitch(SCR *sp, TAG *tp, int force)
{
	SCR *new;

	/* Get a file structure. */
	if (tp->frp == NULL && (tp->frp = file_add(sp, tp->fname)) == NULL)
		return (1);

	/* Get a new screen. */
	if (screen_init(sp->gp, sp, &new))
		return (1);
	if (vs_split(sp, new, 0)) {
		(void)file_end(new, new->ep, 1);
		(void)screen_end(new);
		return (1);
	}

	/* Get a backing file. */
	if (tp->frp == sp->frp) {
		/* Copy file state. */
		new->ep = sp->ep;
		++new->ep->refcnt;

		new->frp = tp->frp;
		new->frp->flags = sp->frp->flags;
	} else if (file_init(new, tp->frp, NULL, force)) {
		(void)vs_discard(new, NULL);
		(void)screen_end(new);
		return (1);
	}

	/* Create the argument list. */
	new->cargv = new->argv = ex_buildargv(sp, NULL, tp->frp->name);

	/* Display tags in the center of the screen. */
	F_CLR(new, SC_SCR_TOP);
	F_SET(new, SC_SCR_CENTER);

	/* Switch. */
	sp->nextdisp = new;
	F_SET(sp, SC_SSWITCH);

	return (0);
}

/*
 * ex_tag_pop -- ^T
 *		 :tagp[op][!] [number | file]
 *
 *	Pop to a previous TAGQ context.
 *
 * PUBLIC: int ex_tag_pop(SCR *, EXCMD *);
 */
int
ex_tag_pop(SCR *sp, EXCMD *cmdp)
{
	EX_PRIVATE *exp;
	TAGQ *tqp, *dtqp;
	size_t arglen;
	long off;
	char *arg, *p, *t;
	size_t nlen;

	/* Check for an empty stack. */
	exp = EXP(sp);
	if (TAILQ_EMPTY(exp->tq)) {
		tag_msg(sp, TAG_EMPTY, NULL);
		return (1);
	}

	/* Find the last TAG structure that we're going to DISCARD! */
	switch (cmdp->argc) {
	case 0:				/* Pop one tag. */
		dtqp = TAILQ_FIRST(exp->tq);
		break;
	case 1:				/* Name or number. */
		INT2CHAR(sp, cmdp->argv[0]->bp, cmdp->argv[0]->len+1, 
			 arg, nlen);
		off = strtol(arg, &p, 10);
		if (*p != '\0')
			goto filearg;

		/* Number: pop that many queue entries. */
		if (off < 1)
			return (0);
		TAILQ_FOREACH(tqp, exp->tq, q)
			if (--off <= 1) break;
		if (tqp == NULL) {
			msgq(sp, M_ERR,
	"159|Less than %s entries on the tags stack; use :display t[ags]",
			    arg);
			return (1);
		}
		dtqp = tqp;
		break;

		/* File argument: pop to that queue entry. */
filearg:	arglen = strlen(arg);
		for (tqp = TAILQ_FIRST(exp->tq); tqp;
		    dtqp = tqp, tqp = TAILQ_NEXT(tqp, q)) {
			/* Don't pop to the current file. */
			if (tqp == TAILQ_FIRST(exp->tq))
				continue;
			p = tqp->current->frp->name;
			if ((t = strrchr(p, '/')) == NULL)
				t = p;
			else
				++t;
			if (!strncmp(arg, t, arglen))
				break;
		}
		if (tqp == NULL) {
			msgq_str(sp, M_ERR, arg,
	"160|No file %s on the tags stack to return to; use :display t[ags]");
			return (1);
		}
		if (tqp == TAILQ_FIRST(exp->tq))
			return (0);
		break;
	default:
		abort();
	}

	return (tag_pop(sp, dtqp, FL_ISSET(cmdp->iflags, E_C_FORCE)));
}

/*
 * ex_tag_top -- :tagt[op][!]
 *	Clear the tag stack.
 *
 * PUBLIC: int ex_tag_top(SCR *, EXCMD *);
 */
int
ex_tag_top(SCR *sp, EXCMD *cmdp)
{
	EX_PRIVATE *exp;

	exp = EXP(sp);

	/* Check for an empty stack. */
	if (TAILQ_EMPTY(exp->tq)) {
		tag_msg(sp, TAG_EMPTY, NULL);
		return (1);
	}

	/* Return to the oldest information. */
	return (tag_pop(sp, TAILQ_PREV(TAILQ_LAST(exp->tq, _tqh), _tqh, q),
	    FL_ISSET(cmdp->iflags, E_C_FORCE)));
}

/*
 * tag_pop --
 *	Pop up to and including the specified TAGQ context.
 */
static int
tag_pop(SCR *sp, TAGQ *dtqp, int force)
{
	EX_PRIVATE *exp;
	TAG *tp;
	TAGQ *tqp;

	exp = EXP(sp);

	/*
	 * Update the cursor from the saved TAG information of the TAG
	 * structure we're moving to.
	 */
	tp = TAILQ_NEXT(dtqp, q)->current;
	if (tp->frp == sp->frp) {
		sp->lno = tp->lno;
		sp->cno = tp->cno;
	} else {
		if (file_m1(sp, force, FS_ALL | FS_POSSIBLE))
			return (1);

		tp->frp->lno = tp->lno;
		tp->frp->cno = tp->cno;
		F_SET(sp->frp, FR_CURSORSET);
		if (file_init(sp, tp->frp, NULL, FS_SETALT))
			return (1);

		F_SET(sp, SC_FSWITCH);
	}

	/* Pop entries off the queue up to and including dtqp. */
	do {
		tqp = TAILQ_FIRST(exp->tq);
		if (tagq_free(sp, tqp))
			return (0);
	} while (tqp != dtqp);

	/*
	 * If only a single tag left, we've returned to the first tag point,
	 * and the stack is now empty.
	 */
	if (TAILQ_NEXT(TAILQ_FIRST(exp->tq), q) == NULL)
		tagq_free(sp, TAILQ_FIRST(exp->tq));

	return (0);
}

/*
 * ex_tag_display --
 *	Display the list of tags.
 *
 * PUBLIC: int ex_tag_display(SCR *);
 */
int
ex_tag_display(SCR *sp)
{
	EX_PRIVATE *exp;
	TAG *tp;
	TAGQ *tqp;
	int cnt;
	size_t len;
	char *p;

	exp = EXP(sp);
	if (TAILQ_EMPTY(exp->tq)) {
		tag_msg(sp, TAG_EMPTY, NULL);
		return (0);
	}

	/*
	 * We give the file name 20 columns and the search string the rest.
	 * If there's not enough room, we don't do anything special, it's
	 * not worth the effort, it just makes the display more confusing.
	 *
	 * We also assume that characters in file names map 1-1 to printing
	 * characters.  This might not be true, but I don't think it's worth
	 * fixing.  (The obvious fix is to pass the filenames through the
	 * msg_print function.)
	 */
#define	L_NAME	30		/* Name. */
#define	L_SLOP	 4		/* Leading number plus trailing *. */
#define	L_SPACE	 5		/* Spaces after name, before tag. */
#define	L_TAG	20		/* Tag. */
	if (sp->cols <= L_NAME + L_SLOP) {
		msgq(sp, M_ERR, "292|Display too small.");
		return (0);
	}

	/*
	 * Display the list of tags for each queue entry.  The first entry
	 * is numbered, and the current tag entry has an asterisk appended.
	 */
	for (cnt = 1, tqp = TAILQ_FIRST(exp->tq); !INTERRUPTED(sp) &&
	    tqp != NULL; ++cnt, tqp = TAILQ_NEXT(tqp, q))
		TAILQ_FOREACH(tp, tqp->tagq, q) {
			if (tp == TAILQ_FIRST(tqp->tagq))
				(void)ex_printf(sp, "%2d ", cnt);
			else
				(void)ex_printf(sp, "   ");
			p = tp->frp == NULL ? tp->fname : tp->frp->name;
			if ((len = strlen(p)) > L_NAME) {
				len = len - (L_NAME - 4);
				(void)ex_printf(sp, "   ... %*.*s",
				    L_NAME - 4, L_NAME - 4, p + len);
			} else
				(void)ex_printf(sp,
				    "   %*.*s", L_NAME, L_NAME, p);
			if (tqp->current == tp)
				(void)ex_printf(sp, "*");

			if (tp == TAILQ_FIRST(tqp->tagq) && tqp->tag != NULL &&
			    (sp->cols - L_NAME) >= L_TAG + L_SPACE) {
				len = strlen(tqp->tag);
				if (len > sp->cols - (L_NAME + L_SPACE))
					len = sp->cols - (L_NAME + L_SPACE);
				(void)ex_printf(sp, "%s%.*s",
				    tqp->current == tp ? "    " : "     ",
				    (int)len, tqp->tag);
			}
			(void)ex_printf(sp, "\n");
		}
	return (0);
}

/*
 * ex_tag_copy --
 *	Copy a screen's tag structures.
 *
 * PUBLIC: int ex_tag_copy(SCR *, SCR *);
 */
int
ex_tag_copy(SCR *orig, SCR *sp)
{
	EX_PRIVATE *oexp, *nexp;
	TAGQ *aqp, *tqp;
	TAG *ap, *tp;
	TAGF *atfp, *tfp;

	oexp = EXP(orig);
	nexp = EXP(sp);

	/* Copy tag queue and tags stack. */
	TAILQ_FOREACH(aqp, oexp->tq, q) {
		if (tagq_copy(sp, aqp, &tqp))
			return (1);
		TAILQ_FOREACH(ap, aqp->tagq, q) {
			if (tag_copy(sp, ap, &tp))
				return (1);
			/* Set the current pointer. */
			if (aqp->current == ap)
				tqp->current = tp;
			TAILQ_INSERT_TAIL(tqp->tagq, tp, q);
		}
		TAILQ_INSERT_TAIL(nexp->tq, tqp, q);
	}

	/* Copy list of tag files. */
	TAILQ_FOREACH(atfp, oexp->tagfq, q) {
		if (tagf_copy(sp, atfp, &tfp))
			return (1);
		TAILQ_INSERT_TAIL(nexp->tagfq, tfp, q);
	}

	/* Copy the last tag. */
	if (oexp->tag_last != NULL &&
	    (nexp->tag_last = v_wstrdup(sp, oexp->tag_last, 
					STRLEN(oexp->tag_last))) == NULL) {
		msgq(sp, M_SYSERR, NULL);
		return (1);
	}
	return (0);
}

/*
 * tagf_copy --
 *	Copy a TAGF structure and return it in new memory.
 */
static int
tagf_copy(SCR *sp, TAGF *otfp, TAGF **tfpp)
{
	TAGF *tfp;

	MALLOC_RET(sp, tfp, TAGF *, sizeof(TAGF));
	*tfp = *otfp;

	/* XXX: Allocate as part of the TAGF structure!!! */
	if ((tfp->name = strdup(otfp->name)) == NULL)
		return (1);

	*tfpp = tfp;
	return (0);
}

/*
 * tagq_copy --
 *	Copy a TAGQ structure and return it in new memory.
 */
static int
tagq_copy(SCR *sp, TAGQ *otqp, TAGQ **tqpp)
{
	TAGQ *tqp;
	size_t len;

	len = sizeof(TAGQ);
	if (otqp->tag != NULL)
		len += otqp->tlen + 1;
	MALLOC_RET(sp, tqp, TAGQ *, len);
	memcpy(tqp, otqp, len);

	TAILQ_INIT(tqp->tagq);
	tqp->current = NULL;
	if (otqp->tag != NULL)
		tqp->tag = tqp->buf;

	*tqpp = tqp;
	return (0);
}

/*
 * tag_copy --
 *	Copy a TAG structure and return it in new memory.
 */
static int
tag_copy(SCR *sp, TAG *otp, TAG **tpp)
{
	TAG *tp;
	size_t len;

	len = sizeof(TAG);
	if (otp->fname != NULL)
		len += otp->fnlen + 1;
	if (otp->search != NULL)
		len += otp->slen + 1;
	if (otp->msg != NULL)
		len += otp->mlen + 1;
	MALLOC_RET(sp, tp, TAG *, len);
	memcpy(tp, otp, len);

	if (otp->fname != NULL)
		tp->fname = (char *)tp->buf;
	if (otp->search != NULL)
		tp->search = tp->buf + (otp->search - otp->buf);
	if (otp->msg != NULL)
		tp->msg = tp->buf + (otp->msg - otp->buf);

	*tpp = tp;
	return (0);
}

/*
 * tagf_free --
 *	Free a TAGF structure.
 */
static int
tagf_free(SCR *sp, TAGF *tfp)
{
	EX_PRIVATE *exp;

	exp = EXP(sp);
	TAILQ_REMOVE(exp->tagfq, tfp, q);
	free(tfp->name);
	free(tfp);
	return (0);
}

/*
 * tagq_free --
 *	Free a TAGQ structure (and associated TAG structures).
 *
 * PUBLIC: int tagq_free(SCR *, TAGQ *);
 */
int
tagq_free(SCR *sp, TAGQ *tqp)
{
	EX_PRIVATE *exp;
	TAG *tp;

	exp = EXP(sp);
	while ((tp = TAILQ_FIRST(tqp->tagq)) != NULL) {
		TAILQ_REMOVE(tqp->tagq, tp, q);
		free(tp);
	}
	/*
	 * !!!
	 * If allocated and then the user failed to switch files, the TAGQ
	 * structure was never attached to any list.
	 */
	if (TAILQ_ENTRY_ISVALID(tqp, q))
		TAILQ_REMOVE(exp->tq, tqp, q);
	free(tqp);
	return (0);
}

/*
 * PUBLIC: int tagq_push(SCR*, TAGQ*, int, int );
 */
int
tagq_push(SCR *sp, TAGQ *tqp, int new_screen, int force)
{
	EX_PRIVATE *exp;
	FREF *frp;
	TAG *rtp;
	TAGQ *rtqp;
	recno_t lno;
	size_t cno;
	int istmp;
	char *np;
	size_t nlen;

	exp = EXP(sp);

	/*
	 * Allocate all necessary memory before swapping screens.  Initialize
	 * flags so we know what to free.
	 */
	rtp = NULL;
	rtqp = NULL;
	if (TAILQ_EMPTY(exp->tq)) {
		/* Initialize the `local context' tag queue structure. */
		CALLOC_GOTO(sp, rtqp, TAGQ *, 1, sizeof(TAGQ));
		TAILQ_INIT(rtqp->tagq);

		/* Initialize and link in its tag structure. */
		CALLOC_GOTO(sp, rtp, TAG *, 1, sizeof(TAG));
		TAILQ_INSERT_HEAD(rtqp->tagq, rtp, q);
		rtqp->current = rtp;
	}

	/*
	 * Stick the current context information in a convenient place, we're
	 * about to lose it.  Note, if we're called on editor startup, there
	 * will be no FREF structure.
	 */
	frp = sp->frp;
	lno = sp->lno;
	cno = sp->cno;
	istmp = frp == NULL ||
	    (F_ISSET(frp, FR_TMPFILE) && !new_screen);

	/* Try to switch to the preset tag. */
	if (new_screen) {
		if (ex_tag_Nswitch(sp, tqp->current, force))
			goto err;

		/* Everything else gets done in the new screen. */
		sp = sp->nextdisp;
		exp = EXP(sp);
	} else
		if (ex_tag_nswitch(sp, tqp->current, force))
			goto err;

	/*
	 * If this is the first tag, put a `current location' queue entry
	 * in place, so we can pop all the way back to the current mark.
	 * Note, it doesn't point to much of anything, it's a placeholder.
	 */
	if (TAILQ_EMPTY(exp->tq)) {
		TAILQ_INSERT_HEAD(exp->tq, rtqp, q);
	} else
		rtqp = TAILQ_FIRST(exp->tq);

	/* Link the new TAGQ structure into place. */
	TAILQ_INSERT_HEAD(exp->tq, tqp, q);

	(void)ctag_search(sp,
	    tqp->current->search, tqp->current->slen, tqp->tag);
	if (tqp->current->msg) {
	    INT2CHAR(sp, tqp->current->msg, tqp->current->mlen + 1,
		     np, nlen);
	    msgq(sp, M_INFO, "%s", np);
	}

	/*
	 * Move the current context from the temporary save area into the
	 * right structure.
	 *
	 * If we were in a temporary file, we don't have a context to which
	 * we can return, so just make it be the same as what we're moving
	 * to.  It will be a little odd that ^T doesn't change anything, but
	 * I don't think it's a big deal.
	 */
	if (istmp) {
		rtqp->current->frp = sp->frp;
		rtqp->current->lno = sp->lno;
		rtqp->current->cno = sp->cno;
	} else {
		rtqp->current->frp = frp;
		rtqp->current->lno = lno;
		rtqp->current->cno = cno;
	}
	return (0);

err:
alloc_err:
	if (rtqp != NULL)
		free(rtqp);
	if (rtp != NULL)
		free(rtp);
	tagq_free(sp, tqp);
	return (1);
}

/*
 * tag_msg
 *	A few common messages.
 *
 * PUBLIC: void tag_msg(SCR *, tagmsg_t, char *);
 */
void
tag_msg(SCR *sp, tagmsg_t msg, char *tag)
{
	switch (msg) {
	case TAG_BADLNO:
		msgq_str(sp, M_ERR, tag,
	    "164|%s: the tag's line number is past the end of the file");
		break;
	case TAG_EMPTY:
		msgq(sp, M_INFO, "165|The tags stack is empty");
		break;
	case TAG_SEARCH:
		msgq_str(sp, M_ERR, tag, "166|%s: search pattern not found");
		break;
	default:
		abort();
	}
}

/*
 * ex_tagf_alloc --
 *	Create a new list of ctag files.
 *
 * PUBLIC: int ex_tagf_alloc(SCR *, char *);
 */
int
ex_tagf_alloc(SCR *sp, char *str)
{
	EX_PRIVATE *exp;
	TAGF *tfp;
	size_t len;
	char *p, *t;

	/* Free current queue. */
	exp = EXP(sp);
	while ((tfp = TAILQ_FIRST(exp->tagfq)) != NULL)
		tagf_free(sp, tfp);

	/* Create new queue. */
	for (p = t = str;; ++p) {
		if (*p == '\0' || cmdskip(*p)) {
			if ((len = p - t)) {
				MALLOC_RET(sp, tfp, TAGF *, sizeof(TAGF));
				MALLOC(sp, tfp->name, char *, len + 1);
				if (tfp->name == NULL) {
					free(tfp);
					return (1);
				}
				memcpy(tfp->name, t, len);
				tfp->name[len] = '\0';
				tfp->flags = 0;
				TAILQ_INSERT_TAIL(exp->tagfq, tfp, q);
			}
			t = p + 1;
		}
		if (*p == '\0')
			 break;
	}
	return (0);
}
						/* Free previous queue. */
/*
 * ex_tag_free --
 *	Free the ex tag information.
 *
 * PUBLIC: int ex_tag_free(SCR *);
 */
int
ex_tag_free(SCR *sp)
{
	EX_PRIVATE *exp;
	TAGF *tfp;
	TAGQ *tqp;

	/* Free up tag information. */
	exp = EXP(sp);
	while ((tqp = TAILQ_FIRST(exp->tq)) != NULL)
		tagq_free(sp, tqp);
	while ((tfp = TAILQ_FIRST(exp->tagfq)) != NULL)
		tagf_free(sp, tfp);
	if (exp->tag_last != NULL)
		free(exp->tag_last);
	return (0);
}

/*
 * ctag_search --
 *	Search a file for a tag.
 */
static int
ctag_search(SCR *sp, CHAR_T *search, size_t slen, char *tag)
{
	MARK m;
	char *p;
	char *np;
	size_t nlen;

	/*
	 * !!!
	 * The historic tags file format (from a long, long time ago...)
	 * used a line number, not a search string.  I got complaints, so
	 * people are still using the format.  POSIX 1003.2 permits it.
	 */
	if (ISDIGIT(search[0])) {
		INT2CHAR(sp, search, slen+1, np, nlen);
		m.lno = atoi(np);
		if (!db_exist(sp, m.lno)) {
			tag_msg(sp, TAG_BADLNO, tag);
			return (1);
		}
	} else {
		/*
		 * Search for the tag; cheap fallback for C functions
		 * if the name is the same but the arguments have changed.
		 */
		m.lno = 1;
		m.cno = 0;
		if (f_search(sp, &m, &m,
		    search, slen, NULL, SEARCH_FILE | SEARCH_TAG)) {
			INT2CHAR(sp, search, slen, np, nlen);
			if ((p = strrchr(np, '(')) != NULL) {
				slen = p - np;
				if (f_search(sp, &m, &m, search, slen,
				    NULL, SEARCH_FILE | SEARCH_TAG))
					goto notfound;
			} else {
notfound:			tag_msg(sp, TAG_SEARCH, tag);
				return (1);
			}
		}
		/*
		 * !!!
		 * Historically, tags set the search direction if it wasn't
		 * already set.
		 */
		if (sp->searchdir == NOTSET)
			sp->searchdir = FORWARD;
	}

	/*
	 * !!!
	 * Tags move to the first non-blank, NOT the search pattern start.
	 */
	sp->lno = m.lno;
	sp->cno = 0;
	(void)nonblank(sp, sp->lno, &sp->cno);
	return (0);
}

/*
 * ctag_slist --
 *	Search the list of tags files for a tag, and return tag queue.
 */
static TAGQ *
ctag_slist(SCR *sp, CHAR_T *tag)
{
	EX_PRIVATE *exp;
	TAGF *tfp;
	TAGQ *tqp;
	size_t len;
	int echk = 0;
	char *np;
	size_t nlen;

	exp = EXP(sp);

	/* Allocate and initialize the tag queue structure. */
	INT2CHAR(sp, tag, STRLEN(tag) + 1, np, nlen);
	len = nlen - 1;
	CALLOC_GOTO(sp, tqp, TAGQ *, 1, sizeof(TAGQ) + len + 1);
	TAILQ_INIT(tqp->tagq);
	tqp->tag = tqp->buf;
	memcpy(tqp->tag, np, (tqp->tlen = len) + 1);

	/*
	 * Find the tag, only display missing file messages once, and
	 * then only if we didn't find the tag.
	 */
	TAILQ_FOREACH(tfp, exp->tagfq, q)
		if (ctag_sfile(sp, tfp, tqp, tqp->tag)) {
			echk = 1;
			F_SET(tfp, TAGF_ERR);
		} else
			F_CLR(tfp, TAGF_ERR | TAGF_ERR_WARN);

	/* Check to see if we found anything. */
	if (TAILQ_EMPTY(tqp->tagq)) {
		msgq_str(sp, M_ERR, tqp->tag, "162|%s: tag not found");
		if (echk)
			TAILQ_FOREACH(tfp, exp->tagfq, q)
				if (F_ISSET(tfp, TAGF_ERR) &&
				    !F_ISSET(tfp, TAGF_ERR_WARN)) {
					errno = tfp->errnum;
					msgq_str(sp, M_SYSERR, tfp->name, "%s");
					F_SET(tfp, TAGF_ERR_WARN);
				}
		free(tqp);
		return (NULL);
	}

	return (tqp);

alloc_err:
	return (NULL);
}

/*
 * ctag_sfile --
 *	Search a tags file for a tag, adding any found to the tag queue.
 */
static int
ctag_sfile(SCR *sp, TAGF *tfp, TAGQ *tqp, char *tname)
{
	struct stat sb;
	TAG *tp;
	size_t dlen, nlen = 0, slen;
	int fd, i, nf1, nf2;
	char *back, *front, *map, *p, *search, *t;
	char *cname = NULL, *dname = NULL, *name = NULL;
	CHAR_T *wp;
	size_t wlen;
	long tl;

	if ((fd = open(tfp->name, O_RDONLY, 0)) < 0) {
		tfp->errnum = errno;
		return (1);
	}

	if (fstat(fd, &sb) != 0 ||
	    (map = mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
		tfp->errnum = errno;
		(void)close(fd);
		return (1);
	}

	tl = O_VAL(sp, O_TAGLENGTH);
	front = map;
	back = front + sb.st_size;
	front = binary_search(tname, front, back);
	front = linear_search(tname, front, back, tl);
	if (front == NULL)
		goto done;

	/*
	 * Initialize and link in the tag structure(s).  The historic ctags
	 * file format only permitted a single tag location per tag.  The
	 * obvious extension to permit multiple tags locations per tag is to
	 * output multiple records in the standard format.  Unfortunately,
	 * this won't work correctly with historic ex/vi implementations,
	 * because their binary search assumes that there's only one record
	 * per tag, and so will use a random tag entry if there si more than
	 * one.  This code handles either format.
	 *
	 * The tags file is in the following format:
	 *
	 *	<tag> <filename> <line number> | <pattern>
	 *
	 * Figure out how long everything is so we can allocate in one swell
	 * foop, but discard anything that looks wrong.
	 */
	for (;;) {
		/* Nul-terminate the end of the line. */
		for (p = front; p < back && *p != '\n'; ++p);
		if (p == back || *p != '\n')
			break;
		*p = '\0';

		/* Update the pointers for the next time. */
		t = p + 1;
		p = front;
		front = t;

		/* Break the line into tokens. */
		for (i = 0; i < 2 && (t = strsep(&p, "\t ")) != NULL; ++i)
			switch (i) {
			case 0:			/* Tag. */
				cname = t;
				break;
			case 1:			/* Filename. */
				name = t;
				nlen = strlen(name);
				break;
			}

		/* Check for corruption. */
		if (i != 2 || p == NULL || t == NULL)
			goto corrupt;

		/* The rest of the string is the search pattern. */
		search = p;
		if ((slen = strlen(p)) == 0) {
corrupt:		p = msg_print(sp, tname, &nf1);
			t = msg_print(sp, tfp->name, &nf2);
			msgq(sp, M_ERR, "163|%s: corrupted tag in %s", p, t);
			if (nf1)
				FREE_SPACE(sp, p, 0);
			if (nf2)
				FREE_SPACE(sp, t, 0);
			continue;
		}

		/* Check for passing the last entry. */
		if (tl ? strncmp(tname, cname, tl) : strcmp(tname, cname))
			break;

		/* Resolve the file name. */
		ctag_file(sp, tfp, name, &dname, &dlen);

		CALLOC_GOTO(sp, tp,
		    TAG *, 1, sizeof(TAG) + dlen + 2 + nlen + 1 + 
		    (slen + 1) * sizeof(CHAR_T));
		tp->fname = (char *)tp->buf;
		if (dlen == 1 && *dname == '.')
			--dlen;
		else if (dlen != 0) {
			memcpy(tp->fname, dname, dlen);
			tp->fname[dlen] = '/';
			++dlen;
		}
		memcpy(tp->fname + dlen, name, nlen + 1);
		tp->fnlen = dlen + nlen;
		tp->search = (CHAR_T*)(tp->fname + tp->fnlen + 1);
		CHAR2INT(sp, search, slen + 1, wp, wlen);
		MEMCPY(tp->search, wp, (tp->slen = slen) + 1);
		TAILQ_INSERT_TAIL(tqp->tagq, tp, q);

		/* Try to preset the tag within the current file. */
		if (sp->frp != NULL && sp->frp->name != NULL &&
		    tqp->current == NULL && !strcmp(tp->fname, sp->frp->name))
			tqp->current = tp;
	}

	if (tqp->current == NULL)
		tqp->current = TAILQ_FIRST(tqp->tagq);

alloc_err:
done:	if (munmap(map, sb.st_size))
		msgq(sp, M_SYSERR, "munmap");
	if (close(fd))
		msgq(sp, M_SYSERR, "close");
	return (0);
}

/*
 * ctag_file --
 *	Search for the right path to this file.
 */
static void
ctag_file(SCR *sp, TAGF *tfp, char *name, char **dirp, size_t *dlenp)
{
	struct stat sb;
	char *p, *buf;

	/*
	 * !!!
	 * If the tag file path is a relative path, see if it exists.  If it
	 * doesn't, look relative to the tags file path.  It's okay for a tag
	 * file to not exist, and historically, vi simply displayed a "new"
	 * file.  However, if the path exists relative to the tag file, it's
	 * pretty clear what's happening, so we may as well get it right.
	 */
	*dlenp = 0;
	if (name[0] != '/' &&
	    stat(name, &sb) && (p = strrchr(tfp->name, '/')) != NULL) {
		*p = '\0';
		if ((buf = join(tfp->name, name)) == NULL) {
			msgq(sp, M_SYSERR, NULL);
			return;
		}
		if (stat(buf, &sb) == 0) {
			*dirp = tfp->name;
			*dlenp = strlen(*dirp);
		}
		free(buf);
		*p = '/';
	}
}

/*
 * Binary search for "string" in memory between "front" and "back".
 *
 * This routine is expected to return a pointer to the start of a line at
 * *or before* the first word matching "string".  Relaxing the constraint
 * this way simplifies the algorithm.
 *
 * Invariants:
 * 	front points to the beginning of a line at or before the first
 *	matching string.
 *
 * 	back points to the beginning of a line at or after the first
 *	matching line.
 *
 * Base of the Invariants.
 * 	front = NULL;
 *	back = EOF;
 *
 * Advancing the Invariants:
 *
 * 	p = first newline after halfway point from front to back.
 *
 * 	If the string at "p" is not greater than the string to match,
 *	p is the new front.  Otherwise it is the new back.
 *
 * Termination:
 *
 * 	The definition of the routine allows it return at any point,
 *	since front is always at or before the line to print.
 *
 * 	In fact, it returns when the chosen "p" equals "back".  This
 *	implies that there exists a string is least half as long as
 *	(back - front), which in turn implies that a linear search will
 *	be no more expensive than the cost of simply printing a string or two.
 *
 * 	Trying to continue with binary search at this point would be
 *	more trouble than it's worth.
 */
#define	EQUAL		0
#define	GREATER		1
#define	LESS		(-1)

#define	SKIP_PAST_NEWLINE(p, back)					\
	while (p < back && *p++ != '\n') continue;

static char *
binary_search(char *string, char *front, char *back)
{
	char *p;

	p = front + (back - front) / 2;
	SKIP_PAST_NEWLINE(p, back);

	while (p != back) {
		if (compare(string, p, back) == GREATER)
			front = p;
		else
			back = p;
		p = front + (back - front) / 2;
		SKIP_PAST_NEWLINE(p, back);
	}
	return (front);
}

/*
 * Find the first line that starts with string, linearly searching from front
 * to back.
 *
 * Return NULL for no such line.
 *
 * This routine assumes:
 *
 * 	o front points at the first character in a line.
 *	o front is before or at the first line to be printed.
 */
static char *
linear_search(char *string, char *front, char *back, long tl)
{
	char *end;
	while (front < back) {
		end = tl && back-front > tl ? front+tl : back;
		switch (compare(string, front, end)) {
		case EQUAL:		/* Found it. */
			return (front);
		case LESS:		/* No such string. */
			return (NULL);
		case GREATER:		/* Keep going. */
			break;
		}
		SKIP_PAST_NEWLINE(front, back);
	}
	return (NULL);
}

/*
 * Return LESS, GREATER, or EQUAL depending on how the string1 compares
 * with string2 (s1 ??? s2).
 *
 * 	o Matches up to len(s1) are EQUAL.
 *	o Matches up to len(s2) are GREATER.
 *
 * The string "s1" is null terminated.  The string s2 is '\t', space, (or
 * "back") terminated.
 *
 * !!!
 * Reasonably modern ctags programs use tabs as separators, not spaces.
 * However, historic programs did use spaces, and, I got complaints.
 */
static int
compare(char *s1, char *s2, char *back)
{
	for (; *s1 && s2 < back && (*s2 != '\t' && *s2 != ' '); ++s1, ++s2)
		if (*s1 != *s2)
			return (*s1 < *s2 ? LESS : GREATER);
	return (*s1 ? GREATER : s2 < back &&
	    (*s2 != '\t' && *s2 != ' ') ? LESS : EQUAL);
}
