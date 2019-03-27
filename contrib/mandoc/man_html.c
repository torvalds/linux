/*	$Id: man_html.c,v 1.153 2018/07/27 17:49:31 schwarze Exp $ */
/*
 * Copyright (c) 2008-2012, 2014 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2013,2014,2015,2017,2018 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"

#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mandoc_aux.h"
#include "mandoc.h"
#include "roff.h"
#include "man.h"
#include "out.h"
#include "html.h"
#include "main.h"

/* FIXME: have PD set the default vspace width. */

#define	MAN_ARGS	  const struct roff_meta *man, \
			  const struct roff_node *n, \
			  struct html *h

struct	htmlman {
	int		(*pre)(MAN_ARGS);
	int		(*post)(MAN_ARGS);
};

static	void		  print_bvspace(struct html *,
				const struct roff_node *);
static	void		  print_man_head(const struct roff_meta *,
				struct html *);
static	void		  print_man_nodelist(MAN_ARGS);
static	void		  print_man_node(MAN_ARGS);
static	int		  fillmode(struct html *, int);
static	int		  man_B_pre(MAN_ARGS);
static	int		  man_HP_pre(MAN_ARGS);
static	int		  man_IP_pre(MAN_ARGS);
static	int		  man_I_pre(MAN_ARGS);
static	int		  man_OP_pre(MAN_ARGS);
static	int		  man_PP_pre(MAN_ARGS);
static	int		  man_RS_pre(MAN_ARGS);
static	int		  man_SH_pre(MAN_ARGS);
static	int		  man_SM_pre(MAN_ARGS);
static	int		  man_SS_pre(MAN_ARGS);
static	int		  man_UR_pre(MAN_ARGS);
static	int		  man_alt_pre(MAN_ARGS);
static	int		  man_ign_pre(MAN_ARGS);
static	int		  man_in_pre(MAN_ARGS);
static	void		  man_root_post(const struct roff_meta *,
				struct html *);
static	void		  man_root_pre(const struct roff_meta *,
				struct html *);

static	const struct htmlman __mans[MAN_MAX - MAN_TH] = {
	{ NULL, NULL }, /* TH */
	{ man_SH_pre, NULL }, /* SH */
	{ man_SS_pre, NULL }, /* SS */
	{ man_IP_pre, NULL }, /* TP */
	{ man_PP_pre, NULL }, /* LP */
	{ man_PP_pre, NULL }, /* PP */
	{ man_PP_pre, NULL }, /* P */
	{ man_IP_pre, NULL }, /* IP */
	{ man_HP_pre, NULL }, /* HP */
	{ man_SM_pre, NULL }, /* SM */
	{ man_SM_pre, NULL }, /* SB */
	{ man_alt_pre, NULL }, /* BI */
	{ man_alt_pre, NULL }, /* IB */
	{ man_alt_pre, NULL }, /* BR */
	{ man_alt_pre, NULL }, /* RB */
	{ NULL, NULL }, /* R */
	{ man_B_pre, NULL }, /* B */
	{ man_I_pre, NULL }, /* I */
	{ man_alt_pre, NULL }, /* IR */
	{ man_alt_pre, NULL }, /* RI */
	{ NULL, NULL }, /* nf */
	{ NULL, NULL }, /* fi */
	{ NULL, NULL }, /* RE */
	{ man_RS_pre, NULL }, /* RS */
	{ man_ign_pre, NULL }, /* DT */
	{ man_ign_pre, NULL }, /* UC */
	{ man_ign_pre, NULL }, /* PD */
	{ man_ign_pre, NULL }, /* AT */
	{ man_in_pre, NULL }, /* in */
	{ man_OP_pre, NULL }, /* OP */
	{ NULL, NULL }, /* EX */
	{ NULL, NULL }, /* EE */
	{ man_UR_pre, NULL }, /* UR */
	{ NULL, NULL }, /* UE */
	{ man_UR_pre, NULL }, /* MT */
	{ NULL, NULL }, /* ME */
};
static	const struct htmlman *const mans = __mans - MAN_TH;


/*
 * Printing leading vertical space before a block.
 * This is used for the paragraph macros.
 * The rules are pretty simple, since there's very little nesting going
 * on here.  Basically, if we're the first within another block (SS/SH),
 * then don't emit vertical space.  If we are (RS), then do.  If not the
 * first, print it.
 */
static void
print_bvspace(struct html *h, const struct roff_node *n)
{

	if (n->body && n->body->child)
		if (n->body->child->type == ROFFT_TBL)
			return;

	if (n->parent->type == ROFFT_ROOT || n->parent->tok != MAN_RS)
		if (NULL == n->prev)
			return;

	print_paragraph(h);
}

void
html_man(void *arg, const struct roff_man *man)
{
	struct html		*h;
	struct roff_node	*n;
	struct tag		*t;

	h = (struct html *)arg;
	n = man->first->child;

	if ((h->oflags & HTML_FRAGMENT) == 0) {
		print_gen_decls(h);
		print_otag(h, TAG_HTML, "");
		if (n->type == ROFFT_COMMENT)
			print_gen_comment(h, n);
		t = print_otag(h, TAG_HEAD, "");
		print_man_head(&man->meta, h);
		print_tagq(h, t);
		print_otag(h, TAG_BODY, "");
	}

	man_root_pre(&man->meta, h);
	t = print_otag(h, TAG_DIV, "c", "manual-text");
	print_man_nodelist(&man->meta, n, h);
	print_tagq(h, t);
	man_root_post(&man->meta, h);
	print_tagq(h, NULL);
}

static void
print_man_head(const struct roff_meta *man, struct html *h)
{
	char	*cp;

	print_gen_head(h);
	mandoc_asprintf(&cp, "%s(%s)", man->title, man->msec);
	print_otag(h, TAG_TITLE, "");
	print_text(h, cp);
	free(cp);
}

static void
print_man_nodelist(MAN_ARGS)
{

	while (n != NULL) {
		print_man_node(man, n, h);
		n = n->next;
	}
}

static void
print_man_node(MAN_ARGS)
{
	static int	 want_fillmode = MAN_fi;
	static int	 save_fillmode;

	struct tag	*t;
	int		 child;

	/*
	 * Handle fill mode switch requests up front,
	 * they would just cause trouble in the subsequent code.
	 */

	switch (n->tok) {
	case MAN_nf:
	case MAN_EX:
		want_fillmode = MAN_nf;
		return;
	case MAN_fi:
	case MAN_EE:
		want_fillmode = MAN_fi;
		if (fillmode(h, 0) == MAN_fi)
			print_otag(h, TAG_BR, "");
		return;
	default:
		break;
	}

	/* Set up fill mode for the upcoming node. */

	switch (n->type) {
	case ROFFT_BLOCK:
		save_fillmode = 0;
		/* Some block macros suspend or cancel .nf. */
		switch (n->tok) {
		case MAN_TP:  /* Tagged paragraphs		*/
		case MAN_IP:  /* temporarily disable .nf	*/
		case MAN_HP:  /* for the head.			*/
			save_fillmode = want_fillmode;
			/* FALLTHROUGH */
		case MAN_SH:  /* Section headers		*/
		case MAN_SS:  /* permanently cancel .nf.	*/
			want_fillmode = MAN_fi;
			/* FALLTHROUGH */
		case MAN_PP:  /* These have no head.		*/
		case MAN_LP:  /* They will simply		*/
		case MAN_P:   /* reopen .nf in the body.	*/
		case MAN_RS:
		case MAN_UR:
		case MAN_MT:
			fillmode(h, MAN_fi);
			break;
		default:
			break;
		}
		break;
	case ROFFT_TBL:
		fillmode(h, MAN_fi);
		break;
	case ROFFT_ELEM:
		/*
		 * Some in-line macros produce tags and/or text
		 * in the handler, so they require fill mode to be
		 * configured up front just like for text nodes.
		 * For the others, keep the traditional approach
		 * of doing the same, for now.
		 */
		fillmode(h, want_fillmode);
		break;
	case ROFFT_TEXT:
		if (fillmode(h, want_fillmode) == MAN_fi &&
		    want_fillmode == MAN_fi &&
		    n->flags & NODE_LINE && *n->string == ' ' &&
		    (h->flags & HTML_NONEWLINE) == 0)
			print_otag(h, TAG_BR, "");
		if (*n->string != '\0')
			break;
		print_paragraph(h);
		return;
	case ROFFT_COMMENT:
		return;
	default:
		break;
	}

	/* Produce output for this node. */

	child = 1;
	switch (n->type) {
	case ROFFT_TEXT:
		t = h->tag;
		print_text(h, n->string);
		break;
	case ROFFT_EQN:
		t = h->tag;
		print_eqn(h, n->eqn);
		break;
	case ROFFT_TBL:
		/*
		 * This will take care of initialising all of the table
		 * state data for the first table, then tearing it down
		 * for the last one.
		 */
		print_tbl(h, n->span);
		return;
	default:
		/*
		 * Close out scope of font prior to opening a macro
		 * scope.
		 */
		if (HTMLFONT_NONE != h->metac) {
			h->metal = h->metac;
			h->metac = HTMLFONT_NONE;
		}

		/*
		 * Close out the current table, if it's open, and unset
		 * the "meta" table state.  This will be reopened on the
		 * next table element.
		 */
		if (h->tblt)
			print_tblclose(h);

		t = h->tag;
		if (n->tok < ROFF_MAX) {
			roff_html_pre(h, n);
			child = 0;
			break;
		}

		assert(n->tok >= MAN_TH && n->tok < MAN_MAX);
		if (mans[n->tok].pre)
			child = (*mans[n->tok].pre)(man, n, h);

		/* Some block macros resume .nf in the body. */
		if (save_fillmode && n->type == ROFFT_BODY)
			want_fillmode = save_fillmode;

		break;
	}

	if (child && n->child)
		print_man_nodelist(man, n->child, h);

	/* This will automatically close out any font scope. */
	print_stagq(h, t);

	if (fillmode(h, 0) == MAN_nf &&
	    n->next != NULL && n->next->flags & NODE_LINE)
		print_endline(h);
}

/*
 * MAN_nf switches to no-fill mode, MAN_fi to fill mode.
 * Other arguments do not switch.
 * The old mode is returned.
 */
static int
fillmode(struct html *h, int want)
{
	struct tag	*pre;
	int		 had;

	for (pre = h->tag; pre != NULL; pre = pre->next)
		if (pre->tag == TAG_PRE)
			break;

	had = pre == NULL ? MAN_fi : MAN_nf;

	if (want && want != had) {
		if (want == MAN_nf)
			print_otag(h, TAG_PRE, "");
		else
			print_tagq(h, pre);
	}
	return had;
}

static void
man_root_pre(const struct roff_meta *man, struct html *h)
{
	struct tag	*t, *tt;
	char		*title;

	assert(man->title);
	assert(man->msec);
	mandoc_asprintf(&title, "%s(%s)", man->title, man->msec);

	t = print_otag(h, TAG_TABLE, "c", "head");
	tt = print_otag(h, TAG_TR, "");

	print_otag(h, TAG_TD, "c", "head-ltitle");
	print_text(h, title);
	print_stagq(h, tt);

	print_otag(h, TAG_TD, "c", "head-vol");
	if (NULL != man->vol)
		print_text(h, man->vol);
	print_stagq(h, tt);

	print_otag(h, TAG_TD, "c", "head-rtitle");
	print_text(h, title);
	print_tagq(h, t);
	free(title);
}

static void
man_root_post(const struct roff_meta *man, struct html *h)
{
	struct tag	*t, *tt;

	t = print_otag(h, TAG_TABLE, "c", "foot");
	tt = print_otag(h, TAG_TR, "");

	print_otag(h, TAG_TD, "c", "foot-date");
	print_text(h, man->date);
	print_stagq(h, tt);

	print_otag(h, TAG_TD, "c", "foot-os");
	if (man->os)
		print_text(h, man->os);
	print_tagq(h, t);
}

static int
man_SH_pre(MAN_ARGS)
{
	char	*id;

	if (n->type == ROFFT_HEAD) {
		id = html_make_id(n, 1);
		print_otag(h, TAG_H1, "cTi", "Sh", id);
		if (id != NULL)
			print_otag(h, TAG_A, "chR", "permalink", id);
	}
	return 1;
}

static int
man_alt_pre(MAN_ARGS)
{
	const struct roff_node	*nn;
	int		 i;
	enum htmltag	 fp;
	struct tag	*t;

	for (i = 0, nn = n->child; nn; nn = nn->next, i++) {
		switch (n->tok) {
		case MAN_BI:
			fp = i % 2 ? TAG_I : TAG_B;
			break;
		case MAN_IB:
			fp = i % 2 ? TAG_B : TAG_I;
			break;
		case MAN_RI:
			fp = i % 2 ? TAG_I : TAG_MAX;
			break;
		case MAN_IR:
			fp = i % 2 ? TAG_MAX : TAG_I;
			break;
		case MAN_BR:
			fp = i % 2 ? TAG_MAX : TAG_B;
			break;
		case MAN_RB:
			fp = i % 2 ? TAG_B : TAG_MAX;
			break;
		default:
			abort();
		}

		if (i)
			h->flags |= HTML_NOSPACE;

		if (fp != TAG_MAX)
			t = print_otag(h, fp, "");

		print_text(h, nn->string);

		if (fp != TAG_MAX)
			print_tagq(h, t);
	}
	return 0;
}

static int
man_SM_pre(MAN_ARGS)
{
	print_otag(h, TAG_SMALL, "");
	if (MAN_SB == n->tok)
		print_otag(h, TAG_B, "");
	return 1;
}

static int
man_SS_pre(MAN_ARGS)
{
	char	*id;

	if (n->type == ROFFT_HEAD) {
		id = html_make_id(n, 1);
		print_otag(h, TAG_H2, "cTi", "Ss", id);
		if (id != NULL)
			print_otag(h, TAG_A, "chR", "permalink", id);
	}
	return 1;
}

static int
man_PP_pre(MAN_ARGS)
{

	if (n->type == ROFFT_HEAD)
		return 0;
	else if (n->type == ROFFT_BLOCK)
		print_bvspace(h, n);

	return 1;
}

static int
man_IP_pre(MAN_ARGS)
{
	const struct roff_node	*nn;

	if (n->type == ROFFT_BODY) {
		print_otag(h, TAG_DD, "");
		return 1;
	} else if (n->type != ROFFT_HEAD) {
		print_otag(h, TAG_DL, "c", "Bl-tag");
		return 1;
	}

	/* FIXME: width specification. */

	print_otag(h, TAG_DT, "");

	/* For IP, only print the first header element. */

	if (MAN_IP == n->tok && n->child)
		print_man_node(man, n->child, h);

	/* For TP, only print next-line header elements. */

	if (MAN_TP == n->tok) {
		nn = n->child;
		while (NULL != nn && 0 == (NODE_LINE & nn->flags))
			nn = nn->next;
		while (NULL != nn) {
			print_man_node(man, nn, h);
			nn = nn->next;
		}
	}

	return 0;
}

static int
man_HP_pre(MAN_ARGS)
{
	if (n->type == ROFFT_HEAD)
		return 0;

	if (n->type == ROFFT_BLOCK) {
		print_bvspace(h, n);
		print_otag(h, TAG_DIV, "c", "HP");
	}
	return 1;
}

static int
man_OP_pre(MAN_ARGS)
{
	struct tag	*tt;

	print_text(h, "[");
	h->flags |= HTML_NOSPACE;
	tt = print_otag(h, TAG_SPAN, "c", "Op");

	if (NULL != (n = n->child)) {
		print_otag(h, TAG_B, "");
		print_text(h, n->string);
	}

	print_stagq(h, tt);

	if (NULL != n && NULL != n->next) {
		print_otag(h, TAG_I, "");
		print_text(h, n->next->string);
	}

	print_stagq(h, tt);
	h->flags |= HTML_NOSPACE;
	print_text(h, "]");
	return 0;
}

static int
man_B_pre(MAN_ARGS)
{
	print_otag(h, TAG_B, "");
	return 1;
}

static int
man_I_pre(MAN_ARGS)
{
	print_otag(h, TAG_I, "");
	return 1;
}

static int
man_in_pre(MAN_ARGS)
{
	print_otag(h, TAG_BR, "");
	return 0;
}

static int
man_ign_pre(MAN_ARGS)
{

	return 0;
}

static int
man_RS_pre(MAN_ARGS)
{
	if (n->type == ROFFT_HEAD)
		return 0;
	if (n->type == ROFFT_BLOCK)
		print_otag(h, TAG_DIV, "c", "Bd-indent");
	return 1;
}

static int
man_UR_pre(MAN_ARGS)
{
	char *cp;
	n = n->child;
	assert(n->type == ROFFT_HEAD);
	if (n->child != NULL) {
		assert(n->child->type == ROFFT_TEXT);
		if (n->tok == MAN_MT) {
			mandoc_asprintf(&cp, "mailto:%s", n->child->string);
			print_otag(h, TAG_A, "cTh", "Mt", cp);
			free(cp);
		} else
			print_otag(h, TAG_A, "cTh", "Lk", n->child->string);
	}

	assert(n->next->type == ROFFT_BODY);
	if (n->next->child != NULL)
		n = n->next;

	print_man_nodelist(man, n->child, h);

	return 0;
}
