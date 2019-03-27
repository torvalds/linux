/*	$Id: mdoc_man.c,v 1.126 2018/04/11 17:11:13 schwarze Exp $ */
/*
 * Copyright (c) 2011-2018 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"

#include <sys/types.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mandoc_aux.h"
#include "mandoc.h"
#include "roff.h"
#include "mdoc.h"
#include "man.h"
#include "out.h"
#include "main.h"

#define	DECL_ARGS const struct roff_meta *meta, struct roff_node *n

typedef	int	(*int_fp)(DECL_ARGS);
typedef	void	(*void_fp)(DECL_ARGS);

struct	manact {
	int_fp		  cond; /* DON'T run actions */
	int_fp		  pre; /* pre-node action */
	void_fp		  post; /* post-node action */
	const char	 *prefix; /* pre-node string constant */
	const char	 *suffix; /* post-node string constant */
};

static	int	  cond_body(DECL_ARGS);
static	int	  cond_head(DECL_ARGS);
static  void	  font_push(char);
static	void	  font_pop(void);
static	int	  man_strlen(const char *);
static	void	  mid_it(void);
static	void	  post__t(DECL_ARGS);
static	void	  post_aq(DECL_ARGS);
static	void	  post_bd(DECL_ARGS);
static	void	  post_bf(DECL_ARGS);
static	void	  post_bk(DECL_ARGS);
static	void	  post_bl(DECL_ARGS);
static	void	  post_dl(DECL_ARGS);
static	void	  post_en(DECL_ARGS);
static	void	  post_enc(DECL_ARGS);
static	void	  post_eo(DECL_ARGS);
static	void	  post_fa(DECL_ARGS);
static	void	  post_fd(DECL_ARGS);
static	void	  post_fl(DECL_ARGS);
static	void	  post_fn(DECL_ARGS);
static	void	  post_fo(DECL_ARGS);
static	void	  post_font(DECL_ARGS);
static	void	  post_in(DECL_ARGS);
static	void	  post_it(DECL_ARGS);
static	void	  post_lb(DECL_ARGS);
static	void	  post_nm(DECL_ARGS);
static	void	  post_percent(DECL_ARGS);
static	void	  post_pf(DECL_ARGS);
static	void	  post_sect(DECL_ARGS);
static	void	  post_vt(DECL_ARGS);
static	int	  pre__t(DECL_ARGS);
static	int	  pre_an(DECL_ARGS);
static	int	  pre_ap(DECL_ARGS);
static	int	  pre_aq(DECL_ARGS);
static	int	  pre_bd(DECL_ARGS);
static	int	  pre_bf(DECL_ARGS);
static	int	  pre_bk(DECL_ARGS);
static	int	  pre_bl(DECL_ARGS);
static	void	  pre_br(DECL_ARGS);
static	int	  pre_dl(DECL_ARGS);
static	int	  pre_en(DECL_ARGS);
static	int	  pre_enc(DECL_ARGS);
static	int	  pre_em(DECL_ARGS);
static	int	  pre_skip(DECL_ARGS);
static	int	  pre_eo(DECL_ARGS);
static	int	  pre_ex(DECL_ARGS);
static	int	  pre_fa(DECL_ARGS);
static	int	  pre_fd(DECL_ARGS);
static	int	  pre_fl(DECL_ARGS);
static	int	  pre_fn(DECL_ARGS);
static	int	  pre_fo(DECL_ARGS);
static	void	  pre_ft(DECL_ARGS);
static	int	  pre_Ft(DECL_ARGS);
static	int	  pre_in(DECL_ARGS);
static	int	  pre_it(DECL_ARGS);
static	int	  pre_lk(DECL_ARGS);
static	int	  pre_li(DECL_ARGS);
static	int	  pre_nm(DECL_ARGS);
static	int	  pre_no(DECL_ARGS);
static	int	  pre_ns(DECL_ARGS);
static	void	  pre_onearg(DECL_ARGS);
static	int	  pre_pp(DECL_ARGS);
static	int	  pre_rs(DECL_ARGS);
static	int	  pre_sm(DECL_ARGS);
static	void	  pre_sp(DECL_ARGS);
static	int	  pre_sect(DECL_ARGS);
static	int	  pre_sy(DECL_ARGS);
static	void	  pre_syn(const struct roff_node *);
static	void	  pre_ta(DECL_ARGS);
static	int	  pre_vt(DECL_ARGS);
static	int	  pre_xr(DECL_ARGS);
static	void	  print_word(const char *);
static	void	  print_line(const char *, int);
static	void	  print_block(const char *, int);
static	void	  print_offs(const char *, int);
static	void	  print_width(const struct mdoc_bl *,
			const struct roff_node *);
static	void	  print_count(int *);
static	void	  print_node(DECL_ARGS);

static	const void_fp roff_manacts[ROFF_MAX] = {
	pre_br,		/* br */
	pre_onearg,	/* ce */
	pre_ft,		/* ft */
	pre_onearg,	/* ll */
	pre_onearg,	/* mc */
	pre_onearg,	/* po */
	pre_onearg,	/* rj */
	pre_sp,		/* sp */
	pre_ta,		/* ta */
	pre_onearg,	/* ti */
};

static	const struct manact __manacts[MDOC_MAX - MDOC_Dd] = {
	{ NULL, NULL, NULL, NULL, NULL }, /* Dd */
	{ NULL, NULL, NULL, NULL, NULL }, /* Dt */
	{ NULL, NULL, NULL, NULL, NULL }, /* Os */
	{ NULL, pre_sect, post_sect, ".SH", NULL }, /* Sh */
	{ NULL, pre_sect, post_sect, ".SS", NULL }, /* Ss */
	{ NULL, pre_pp, NULL, NULL, NULL }, /* Pp */
	{ cond_body, pre_dl, post_dl, NULL, NULL }, /* D1 */
	{ cond_body, pre_dl, post_dl, NULL, NULL }, /* Dl */
	{ cond_body, pre_bd, post_bd, NULL, NULL }, /* Bd */
	{ NULL, NULL, NULL, NULL, NULL }, /* Ed */
	{ cond_body, pre_bl, post_bl, NULL, NULL }, /* Bl */
	{ NULL, NULL, NULL, NULL, NULL }, /* El */
	{ NULL, pre_it, post_it, NULL, NULL }, /* It */
	{ NULL, pre_em, post_font, NULL, NULL }, /* Ad */
	{ NULL, pre_an, NULL, NULL, NULL }, /* An */
	{ NULL, pre_ap, NULL, NULL, NULL }, /* Ap */
	{ NULL, pre_em, post_font, NULL, NULL }, /* Ar */
	{ NULL, pre_sy, post_font, NULL, NULL }, /* Cd */
	{ NULL, pre_sy, post_font, NULL, NULL }, /* Cm */
	{ NULL, pre_li, post_font, NULL, NULL }, /* Dv */
	{ NULL, pre_li, post_font, NULL, NULL }, /* Er */
	{ NULL, pre_li, post_font, NULL, NULL }, /* Ev */
	{ NULL, pre_ex, NULL, NULL, NULL }, /* Ex */
	{ NULL, pre_fa, post_fa, NULL, NULL }, /* Fa */
	{ NULL, pre_fd, post_fd, NULL, NULL }, /* Fd */
	{ NULL, pre_fl, post_fl, NULL, NULL }, /* Fl */
	{ NULL, pre_fn, post_fn, NULL, NULL }, /* Fn */
	{ NULL, pre_Ft, post_font, NULL, NULL }, /* Ft */
	{ NULL, pre_sy, post_font, NULL, NULL }, /* Ic */
	{ NULL, pre_in, post_in, NULL, NULL }, /* In */
	{ NULL, pre_li, post_font, NULL, NULL }, /* Li */
	{ cond_head, pre_enc, NULL, "\\- ", NULL }, /* Nd */
	{ NULL, pre_nm, post_nm, NULL, NULL }, /* Nm */
	{ cond_body, pre_enc, post_enc, "[", "]" }, /* Op */
	{ NULL, pre_Ft, post_font, NULL, NULL }, /* Ot */
	{ NULL, pre_em, post_font, NULL, NULL }, /* Pa */
	{ NULL, pre_ex, NULL, NULL, NULL }, /* Rv */
	{ NULL, NULL, NULL, NULL, NULL }, /* St */
	{ NULL, pre_em, post_font, NULL, NULL }, /* Va */
	{ NULL, pre_vt, post_vt, NULL, NULL }, /* Vt */
	{ NULL, pre_xr, NULL, NULL, NULL }, /* Xr */
	{ NULL, NULL, post_percent, NULL, NULL }, /* %A */
	{ NULL, pre_em, post_percent, NULL, NULL }, /* %B */
	{ NULL, NULL, post_percent, NULL, NULL }, /* %D */
	{ NULL, pre_em, post_percent, NULL, NULL }, /* %I */
	{ NULL, pre_em, post_percent, NULL, NULL }, /* %J */
	{ NULL, NULL, post_percent, NULL, NULL }, /* %N */
	{ NULL, NULL, post_percent, NULL, NULL }, /* %O */
	{ NULL, NULL, post_percent, NULL, NULL }, /* %P */
	{ NULL, NULL, post_percent, NULL, NULL }, /* %R */
	{ NULL, pre__t, post__t, NULL, NULL }, /* %T */
	{ NULL, NULL, post_percent, NULL, NULL }, /* %V */
	{ NULL, NULL, NULL, NULL, NULL }, /* Ac */
	{ cond_body, pre_aq, post_aq, NULL, NULL }, /* Ao */
	{ cond_body, pre_aq, post_aq, NULL, NULL }, /* Aq */
	{ NULL, NULL, NULL, NULL, NULL }, /* At */
	{ NULL, NULL, NULL, NULL, NULL }, /* Bc */
	{ NULL, pre_bf, post_bf, NULL, NULL }, /* Bf */
	{ cond_body, pre_enc, post_enc, "[", "]" }, /* Bo */
	{ cond_body, pre_enc, post_enc, "[", "]" }, /* Bq */
	{ NULL, pre_bk, post_bk, NULL, NULL }, /* Bsx */
	{ NULL, pre_bk, post_bk, NULL, NULL }, /* Bx */
	{ NULL, pre_skip, NULL, NULL, NULL }, /* Db */
	{ NULL, NULL, NULL, NULL, NULL }, /* Dc */
	{ cond_body, pre_enc, post_enc, "\\(lq", "\\(rq" }, /* Do */
	{ cond_body, pre_enc, post_enc, "\\(lq", "\\(rq" }, /* Dq */
	{ NULL, NULL, NULL, NULL, NULL }, /* Ec */
	{ NULL, NULL, NULL, NULL, NULL }, /* Ef */
	{ NULL, pre_em, post_font, NULL, NULL }, /* Em */
	{ cond_body, pre_eo, post_eo, NULL, NULL }, /* Eo */
	{ NULL, pre_bk, post_bk, NULL, NULL }, /* Fx */
	{ NULL, pre_sy, post_font, NULL, NULL }, /* Ms */
	{ NULL, pre_no, NULL, NULL, NULL }, /* No */
	{ NULL, pre_ns, NULL, NULL, NULL }, /* Ns */
	{ NULL, pre_bk, post_bk, NULL, NULL }, /* Nx */
	{ NULL, pre_bk, post_bk, NULL, NULL }, /* Ox */
	{ NULL, NULL, NULL, NULL, NULL }, /* Pc */
	{ NULL, NULL, post_pf, NULL, NULL }, /* Pf */
	{ cond_body, pre_enc, post_enc, "(", ")" }, /* Po */
	{ cond_body, pre_enc, post_enc, "(", ")" }, /* Pq */
	{ NULL, NULL, NULL, NULL, NULL }, /* Qc */
	{ cond_body, pre_enc, post_enc, "\\(oq", "\\(cq" }, /* Ql */
	{ cond_body, pre_enc, post_enc, "\"", "\"" }, /* Qo */
	{ cond_body, pre_enc, post_enc, "\"", "\"" }, /* Qq */
	{ NULL, NULL, NULL, NULL, NULL }, /* Re */
	{ cond_body, pre_rs, NULL, NULL, NULL }, /* Rs */
	{ NULL, NULL, NULL, NULL, NULL }, /* Sc */
	{ cond_body, pre_enc, post_enc, "\\(oq", "\\(cq" }, /* So */
	{ cond_body, pre_enc, post_enc, "\\(oq", "\\(cq" }, /* Sq */
	{ NULL, pre_sm, NULL, NULL, NULL }, /* Sm */
	{ NULL, pre_em, post_font, NULL, NULL }, /* Sx */
	{ NULL, pre_sy, post_font, NULL, NULL }, /* Sy */
	{ NULL, pre_li, post_font, NULL, NULL }, /* Tn */
	{ NULL, NULL, NULL, NULL, NULL }, /* Ux */
	{ NULL, NULL, NULL, NULL, NULL }, /* Xc */
	{ NULL, NULL, NULL, NULL, NULL }, /* Xo */
	{ NULL, pre_fo, post_fo, NULL, NULL }, /* Fo */
	{ NULL, NULL, NULL, NULL, NULL }, /* Fc */
	{ cond_body, pre_enc, post_enc, "[", "]" }, /* Oo */
	{ NULL, NULL, NULL, NULL, NULL }, /* Oc */
	{ NULL, pre_bk, post_bk, NULL, NULL }, /* Bk */
	{ NULL, NULL, NULL, NULL, NULL }, /* Ek */
	{ NULL, NULL, NULL, NULL, NULL }, /* Bt */
	{ NULL, NULL, NULL, NULL, NULL }, /* Hf */
	{ NULL, pre_em, post_font, NULL, NULL }, /* Fr */
	{ NULL, NULL, NULL, NULL, NULL }, /* Ud */
	{ NULL, NULL, post_lb, NULL, NULL }, /* Lb */
	{ NULL, pre_pp, NULL, NULL, NULL }, /* Lp */
	{ NULL, pre_lk, NULL, NULL, NULL }, /* Lk */
	{ NULL, pre_em, post_font, NULL, NULL }, /* Mt */
	{ cond_body, pre_enc, post_enc, "{", "}" }, /* Brq */
	{ cond_body, pre_enc, post_enc, "{", "}" }, /* Bro */
	{ NULL, NULL, NULL, NULL, NULL }, /* Brc */
	{ NULL, NULL, post_percent, NULL, NULL }, /* %C */
	{ NULL, pre_skip, NULL, NULL, NULL }, /* Es */
	{ cond_body, pre_en, post_en, NULL, NULL }, /* En */
	{ NULL, pre_bk, post_bk, NULL, NULL }, /* Dx */
	{ NULL, NULL, post_percent, NULL, NULL }, /* %Q */
	{ NULL, NULL, post_percent, NULL, NULL }, /* %U */
	{ NULL, NULL, NULL, NULL, NULL }, /* Ta */
};
static	const struct manact *const manacts = __manacts - MDOC_Dd;

static	int		outflags;
#define	MMAN_spc	(1 << 0)  /* blank character before next word */
#define	MMAN_spc_force	(1 << 1)  /* even before trailing punctuation */
#define	MMAN_nl		(1 << 2)  /* break man(7) code line */
#define	MMAN_br		(1 << 3)  /* break output line */
#define	MMAN_sp		(1 << 4)  /* insert a blank output line */
#define	MMAN_PP		(1 << 5)  /* reset indentation etc. */
#define	MMAN_Sm		(1 << 6)  /* horizontal spacing mode */
#define	MMAN_Bk		(1 << 7)  /* word keep mode */
#define	MMAN_Bk_susp	(1 << 8)  /* suspend this (after a macro) */
#define	MMAN_An_split	(1 << 9)  /* author mode is "split" */
#define	MMAN_An_nosplit	(1 << 10) /* author mode is "nosplit" */
#define	MMAN_PD		(1 << 11) /* inter-paragraph spacing disabled */
#define	MMAN_nbrword	(1 << 12) /* do not break the next word */

#define	BL_STACK_MAX	32

static	int		Bl_stack[BL_STACK_MAX];  /* offsets [chars] */
static	int		Bl_stack_post[BL_STACK_MAX];  /* add final .RE */
static	int		Bl_stack_len;  /* number of nested Bl blocks */
static	int		TPremain;  /* characters before tag is full */

static	struct {
	char	*head;
	char	*tail;
	size_t	 size;
}	fontqueue;


static int
man_strlen(const char *cp)
{
	size_t	 rsz;
	int	 skip, sz;

	sz = 0;
	skip = 0;
	for (;;) {
		rsz = strcspn(cp, "\\");
		if (rsz) {
			cp += rsz;
			if (skip) {
				skip = 0;
				rsz--;
			}
			sz += rsz;
		}
		if ('\0' == *cp)
			break;
		cp++;
		switch (mandoc_escape(&cp, NULL, NULL)) {
		case ESCAPE_ERROR:
			return sz;
		case ESCAPE_UNICODE:
		case ESCAPE_NUMBERED:
		case ESCAPE_SPECIAL:
		case ESCAPE_OVERSTRIKE:
			if (skip)
				skip = 0;
			else
				sz++;
			break;
		case ESCAPE_SKIPCHAR:
			skip = 1;
			break;
		default:
			break;
		}
	}
	return sz;
}

static void
font_push(char newfont)
{

	if (fontqueue.head + fontqueue.size <= ++fontqueue.tail) {
		fontqueue.size += 8;
		fontqueue.head = mandoc_realloc(fontqueue.head,
		    fontqueue.size);
	}
	*fontqueue.tail = newfont;
	print_word("");
	printf("\\f");
	putchar(newfont);
	outflags &= ~MMAN_spc;
}

static void
font_pop(void)
{

	if (fontqueue.tail > fontqueue.head)
		fontqueue.tail--;
	outflags &= ~MMAN_spc;
	print_word("");
	printf("\\f");
	putchar(*fontqueue.tail);
}

static void
print_word(const char *s)
{

	if ((MMAN_PP | MMAN_sp | MMAN_br | MMAN_nl) & outflags) {
		/*
		 * If we need a newline, print it now and start afresh.
		 */
		if (MMAN_PP & outflags) {
			if (MMAN_sp & outflags) {
				if (MMAN_PD & outflags) {
					printf("\n.PD");
					outflags &= ~MMAN_PD;
				}
			} else if ( ! (MMAN_PD & outflags)) {
				printf("\n.PD 0");
				outflags |= MMAN_PD;
			}
			printf("\n.PP\n");
		} else if (MMAN_sp & outflags)
			printf("\n.sp\n");
		else if (MMAN_br & outflags)
			printf("\n.br\n");
		else if (MMAN_nl & outflags)
			putchar('\n');
		outflags &= ~(MMAN_PP|MMAN_sp|MMAN_br|MMAN_nl|MMAN_spc);
		if (1 == TPremain)
			printf(".br\n");
		TPremain = 0;
	} else if (MMAN_spc & outflags) {
		/*
		 * If we need a space, only print it if
		 * (1) it is forced by `No' or
		 * (2) what follows is not terminating punctuation or
		 * (3) what follows is longer than one character.
		 */
		if (MMAN_spc_force & outflags || '\0' == s[0] ||
		    NULL == strchr(".,:;)]?!", s[0]) || '\0' != s[1]) {
			if (MMAN_Bk & outflags &&
			    ! (MMAN_Bk_susp & outflags))
				putchar('\\');
			putchar(' ');
			if (TPremain)
				TPremain--;
		}
	}

	/*
	 * Reassign needing space if we're not following opening
	 * punctuation.
	 */
	if (MMAN_Sm & outflags && ('\0' == s[0] ||
	    (('(' != s[0] && '[' != s[0]) || '\0' != s[1])))
		outflags |= MMAN_spc;
	else
		outflags &= ~MMAN_spc;
	outflags &= ~(MMAN_spc_force | MMAN_Bk_susp);

	for ( ; *s; s++) {
		switch (*s) {
		case ASCII_NBRSP:
			printf("\\ ");
			break;
		case ASCII_HYPH:
			putchar('-');
			break;
		case ASCII_BREAK:
			printf("\\:");
			break;
		case ' ':
			if (MMAN_nbrword & outflags) {
				printf("\\ ");
				break;
			}
			/* FALLTHROUGH */
		default:
			putchar((unsigned char)*s);
			break;
		}
		if (TPremain)
			TPremain--;
	}
	outflags &= ~MMAN_nbrword;
}

static void
print_line(const char *s, int newflags)
{

	outflags |= MMAN_nl;
	print_word(s);
	outflags |= newflags;
}

static void
print_block(const char *s, int newflags)
{

	outflags &= ~MMAN_PP;
	if (MMAN_sp & outflags) {
		outflags &= ~(MMAN_sp | MMAN_br);
		if (MMAN_PD & outflags) {
			print_line(".PD", 0);
			outflags &= ~MMAN_PD;
		}
	} else if (! (MMAN_PD & outflags))
		print_line(".PD 0", MMAN_PD);
	outflags |= MMAN_nl;
	print_word(s);
	outflags |= MMAN_Bk_susp | newflags;
}

static void
print_offs(const char *v, int keywords)
{
	char		  buf[24];
	struct roffsu	  su;
	const char	 *end;
	int		  sz;

	print_line(".RS", MMAN_Bk_susp);

	/* Convert v into a number (of characters). */
	if (NULL == v || '\0' == *v || (keywords && !strcmp(v, "left")))
		sz = 0;
	else if (keywords && !strcmp(v, "indent"))
		sz = 6;
	else if (keywords && !strcmp(v, "indent-two"))
		sz = 12;
	else {
		end = a2roffsu(v, &su, SCALE_EN);
		if (end == NULL || *end != '\0')
			sz = man_strlen(v);
		else if (SCALE_EN == su.unit)
			sz = su.scale;
		else {
			/*
			 * XXX
			 * If we are inside an enclosing list,
			 * there is no easy way to add the two
			 * indentations because they are provided
			 * in terms of different units.
			 */
			print_word(v);
			outflags |= MMAN_nl;
			return;
		}
	}

	/*
	 * We are inside an enclosing list.
	 * Add the two indentations.
	 */
	if (Bl_stack_len)
		sz += Bl_stack[Bl_stack_len - 1];

	(void)snprintf(buf, sizeof(buf), "%dn", sz);
	print_word(buf);
	outflags |= MMAN_nl;
}

/*
 * Set up the indentation for a list item; used from pre_it().
 */
static void
print_width(const struct mdoc_bl *bl, const struct roff_node *child)
{
	char		  buf[24];
	struct roffsu	  su;
	const char	 *end;
	int		  numeric, remain, sz, chsz;

	numeric = 1;
	remain = 0;

	/* Convert the width into a number (of characters). */
	if (bl->width == NULL)
		sz = (bl->type == LIST_hang) ? 6 : 0;
	else {
		end = a2roffsu(bl->width, &su, SCALE_MAX);
		if (end == NULL || *end != '\0')
			sz = man_strlen(bl->width);
		else if (SCALE_EN == su.unit)
			sz = su.scale;
		else {
			sz = 0;
			numeric = 0;
		}
	}

	/* XXX Rough estimation, might have multiple parts. */
	if (bl->type == LIST_enum)
		chsz = (bl->count > 8) + 1;
	else if (child != NULL && child->type == ROFFT_TEXT)
		chsz = man_strlen(child->string);
	else
		chsz = 0;

	/* Maybe we are inside an enclosing list? */
	mid_it();

	/*
	 * Save our own indentation,
	 * such that child lists can use it.
	 */
	Bl_stack[Bl_stack_len++] = sz + 2;

	/* Set up the current list. */
	if (chsz > sz && bl->type != LIST_tag)
		print_block(".HP", 0);
	else {
		print_block(".TP", 0);
		remain = sz + 2;
	}
	if (numeric) {
		(void)snprintf(buf, sizeof(buf), "%dn", sz + 2);
		print_word(buf);
	} else
		print_word(bl->width);
	TPremain = remain;
}

static void
print_count(int *count)
{
	char		  buf[24];

	(void)snprintf(buf, sizeof(buf), "%d.\\&", ++*count);
	print_word(buf);
}

void
man_man(void *arg, const struct roff_man *man)
{

	/*
	 * Dump the keep buffer.
	 * We're guaranteed by now that this exists (is non-NULL).
	 * Flush stdout afterward, just in case.
	 */
	fputs(mparse_getkeep(man_mparse(man)), stdout);
	fflush(stdout);
}

void
man_mdoc(void *arg, const struct roff_man *mdoc)
{
	struct roff_node *n;

	printf(".\\\" Automatically generated from an mdoc input file."
	    "  Do not edit.\n");
	for (n = mdoc->first->child; n != NULL; n = n->next) {
		if (n->type != ROFFT_COMMENT)
			break;
		printf(".\\\"%s\n", n->string);
	}

	printf(".TH \"%s\" \"%s\" \"%s\" \"%s\" \"%s\"\n",
	    mdoc->meta.title,
	    (mdoc->meta.msec == NULL ? "" : mdoc->meta.msec),
	    mdoc->meta.date, mdoc->meta.os, mdoc->meta.vol);

	/* Disable hyphenation and if nroff, disable justification. */
	printf(".nh\n.if n .ad l");

	outflags = MMAN_nl | MMAN_Sm;
	if (0 == fontqueue.size) {
		fontqueue.size = 8;
		fontqueue.head = fontqueue.tail = mandoc_malloc(8);
		*fontqueue.tail = 'R';
	}
	for (; n != NULL; n = n->next)
		print_node(&mdoc->meta, n);
	putchar('\n');
}

static void
print_node(DECL_ARGS)
{
	const struct manact	*act;
	struct roff_node	*sub;
	int			 cond, do_sub;

	if (n->flags & NODE_NOPRT)
		return;

	/*
	 * Break the line if we were parsed subsequent the current node.
	 * This makes the page structure be more consistent.
	 */
	if (MMAN_spc & outflags && NODE_LINE & n->flags)
		outflags |= MMAN_nl;

	act = NULL;
	cond = 0;
	do_sub = 1;
	n->flags &= ~NODE_ENDED;

	if (n->type == ROFFT_TEXT) {
		/*
		 * Make sure that we don't happen to start with a
		 * control character at the start of a line.
		 */
		if (MMAN_nl & outflags &&
		    ('.' == *n->string || '\'' == *n->string)) {
			print_word("");
			printf("\\&");
			outflags &= ~MMAN_spc;
		}
		if (n->flags & NODE_DELIMC)
			outflags &= ~(MMAN_spc | MMAN_spc_force);
		else if (outflags & MMAN_Sm)
			outflags |= MMAN_spc_force;
		print_word(n->string);
		if (n->flags & NODE_DELIMO)
			outflags &= ~(MMAN_spc | MMAN_spc_force);
		else if (outflags & MMAN_Sm)
			outflags |= MMAN_spc;
	} else if (n->tok < ROFF_MAX) {
		(*roff_manacts[n->tok])(meta, n);
		return;
	} else {
		assert(n->tok >= MDOC_Dd && n->tok < MDOC_MAX);
		/*
		 * Conditionally run the pre-node action handler for a
		 * node.
		 */
		act = manacts + n->tok;
		cond = act->cond == NULL || (*act->cond)(meta, n);
		if (cond && act->pre != NULL &&
		    (n->end == ENDBODY_NOT || n->child != NULL))
			do_sub = (*act->pre)(meta, n);
	}

	/*
	 * Conditionally run all child nodes.
	 * Note that this iterates over children instead of using
	 * recursion.  This prevents unnecessary depth in the stack.
	 */
	if (do_sub)
		for (sub = n->child; sub; sub = sub->next)
			print_node(meta, sub);

	/*
	 * Lastly, conditionally run the post-node handler.
	 */
	if (NODE_ENDED & n->flags)
		return;

	if (cond && act->post)
		(*act->post)(meta, n);

	if (ENDBODY_NOT != n->end)
		n->body->flags |= NODE_ENDED;
}

static int
cond_head(DECL_ARGS)
{

	return n->type == ROFFT_HEAD;
}

static int
cond_body(DECL_ARGS)
{

	return n->type == ROFFT_BODY;
}

static int
pre_enc(DECL_ARGS)
{
	const char	*prefix;

	prefix = manacts[n->tok].prefix;
	if (NULL == prefix)
		return 1;
	print_word(prefix);
	outflags &= ~MMAN_spc;
	return 1;
}

static void
post_enc(DECL_ARGS)
{
	const char *suffix;

	suffix = manacts[n->tok].suffix;
	if (NULL == suffix)
		return;
	outflags &= ~(MMAN_spc | MMAN_nl);
	print_word(suffix);
}

static int
pre_ex(DECL_ARGS)
{
	outflags |= MMAN_br | MMAN_nl;
	return 1;
}

static void
post_font(DECL_ARGS)
{

	font_pop();
}

static void
post_percent(DECL_ARGS)
{

	if (pre_em == manacts[n->tok].pre)
		font_pop();
	if (n->next) {
		print_word(",");
		if (n->prev &&	n->prev->tok == n->tok &&
				n->next->tok == n->tok)
			print_word("and");
	} else {
		print_word(".");
		outflags |= MMAN_nl;
	}
}

static int
pre__t(DECL_ARGS)
{

	if (n->parent->tok == MDOC_Rs && n->parent->norm->Rs.quote_T) {
		print_word("\\(lq");
		outflags &= ~MMAN_spc;
	} else
		font_push('I');
	return 1;
}

static void
post__t(DECL_ARGS)
{

	if (n->parent->tok  == MDOC_Rs && n->parent->norm->Rs.quote_T) {
		outflags &= ~MMAN_spc;
		print_word("\\(rq");
	} else
		font_pop();
	post_percent(meta, n);
}

/*
 * Print before a section header.
 */
static int
pre_sect(DECL_ARGS)
{

	if (n->type == ROFFT_HEAD) {
		outflags |= MMAN_sp;
		print_block(manacts[n->tok].prefix, 0);
		print_word("");
		putchar('\"');
		outflags &= ~MMAN_spc;
	}
	return 1;
}

/*
 * Print subsequent a section header.
 */
static void
post_sect(DECL_ARGS)
{

	if (n->type != ROFFT_HEAD)
		return;
	outflags &= ~MMAN_spc;
	print_word("");
	putchar('\"');
	outflags |= MMAN_nl;
	if (MDOC_Sh == n->tok && SEC_AUTHORS == n->sec)
		outflags &= ~(MMAN_An_split | MMAN_An_nosplit);
}

/* See mdoc_term.c, synopsis_pre() for comments. */
static void
pre_syn(const struct roff_node *n)
{

	if (NULL == n->prev || ! (NODE_SYNPRETTY & n->flags))
		return;

	if (n->prev->tok == n->tok &&
	    MDOC_Ft != n->tok &&
	    MDOC_Fo != n->tok &&
	    MDOC_Fn != n->tok) {
		outflags |= MMAN_br;
		return;
	}

	switch (n->prev->tok) {
	case MDOC_Fd:
	case MDOC_Fn:
	case MDOC_Fo:
	case MDOC_In:
	case MDOC_Vt:
		outflags |= MMAN_sp;
		break;
	case MDOC_Ft:
		if (MDOC_Fn != n->tok && MDOC_Fo != n->tok) {
			outflags |= MMAN_sp;
			break;
		}
		/* FALLTHROUGH */
	default:
		outflags |= MMAN_br;
		break;
	}
}

static int
pre_an(DECL_ARGS)
{

	switch (n->norm->An.auth) {
	case AUTH_split:
		outflags &= ~MMAN_An_nosplit;
		outflags |= MMAN_An_split;
		return 0;
	case AUTH_nosplit:
		outflags &= ~MMAN_An_split;
		outflags |= MMAN_An_nosplit;
		return 0;
	default:
		if (MMAN_An_split & outflags)
			outflags |= MMAN_br;
		else if (SEC_AUTHORS == n->sec &&
		    ! (MMAN_An_nosplit & outflags))
			outflags |= MMAN_An_split;
		return 1;
	}
}

static int
pre_ap(DECL_ARGS)
{

	outflags &= ~MMAN_spc;
	print_word("'");
	outflags &= ~MMAN_spc;
	return 0;
}

static int
pre_aq(DECL_ARGS)
{

	print_word(n->child != NULL && n->child->next == NULL &&
	    n->child->tok == MDOC_Mt ?  "<" : "\\(la");
	outflags &= ~MMAN_spc;
	return 1;
}

static void
post_aq(DECL_ARGS)
{

	outflags &= ~(MMAN_spc | MMAN_nl);
	print_word(n->child != NULL && n->child->next == NULL &&
	    n->child->tok == MDOC_Mt ?  ">" : "\\(ra");
}

static int
pre_bd(DECL_ARGS)
{

	outflags &= ~(MMAN_PP | MMAN_sp | MMAN_br);

	if (DISP_unfilled == n->norm->Bd.type ||
	    DISP_literal  == n->norm->Bd.type)
		print_line(".nf", 0);
	if (0 == n->norm->Bd.comp && NULL != n->parent->prev)
		outflags |= MMAN_sp;
	print_offs(n->norm->Bd.offs, 1);
	return 1;
}

static void
post_bd(DECL_ARGS)
{

	/* Close out this display. */
	print_line(".RE", MMAN_nl);
	if (DISP_unfilled == n->norm->Bd.type ||
	    DISP_literal  == n->norm->Bd.type)
		print_line(".fi", MMAN_nl);

	/* Maybe we are inside an enclosing list? */
	if (NULL != n->parent->next)
		mid_it();
}

static int
pre_bf(DECL_ARGS)
{

	switch (n->type) {
	case ROFFT_BLOCK:
		return 1;
	case ROFFT_BODY:
		break;
	default:
		return 0;
	}
	switch (n->norm->Bf.font) {
	case FONT_Em:
		font_push('I');
		break;
	case FONT_Sy:
		font_push('B');
		break;
	default:
		font_push('R');
		break;
	}
	return 1;
}

static void
post_bf(DECL_ARGS)
{

	if (n->type == ROFFT_BODY)
		font_pop();
}

static int
pre_bk(DECL_ARGS)
{
	switch (n->type) {
	case ROFFT_BLOCK:
		return 1;
	case ROFFT_BODY:
	case ROFFT_ELEM:
		outflags |= MMAN_Bk;
		return 1;
	default:
		return 0;
	}
}

static void
post_bk(DECL_ARGS)
{
	switch (n->type) {
	case ROFFT_ELEM:
		while ((n = n->parent) != NULL)
			 if (n->tok == MDOC_Bk)
				return;
		/* FALLTHROUGH */
	case ROFFT_BODY:
		outflags &= ~MMAN_Bk;
		break;
	default:
		break;
	}
}

static int
pre_bl(DECL_ARGS)
{
	size_t		 icol;

	/*
	 * print_offs() will increase the -offset to account for
	 * a possible enclosing .It, but any enclosed .It blocks
	 * just nest and do not add up their indentation.
	 */
	if (n->norm->Bl.offs) {
		print_offs(n->norm->Bl.offs, 0);
		Bl_stack[Bl_stack_len++] = 0;
	}

	switch (n->norm->Bl.type) {
	case LIST_enum:
		n->norm->Bl.count = 0;
		return 1;
	case LIST_column:
		break;
	default:
		return 1;
	}

	if (n->child != NULL) {
		print_line(".TS", MMAN_nl);
		for (icol = 0; icol < n->norm->Bl.ncols; icol++)
			print_word("l");
		print_word(".");
	}
	outflags |= MMAN_nl;
	return 1;
}

static void
post_bl(DECL_ARGS)
{

	switch (n->norm->Bl.type) {
	case LIST_column:
		if (n->child != NULL)
			print_line(".TE", 0);
		break;
	case LIST_enum:
		n->norm->Bl.count = 0;
		break;
	default:
		break;
	}

	if (n->norm->Bl.offs) {
		print_line(".RE", MMAN_nl);
		assert(Bl_stack_len);
		Bl_stack_len--;
		assert(0 == Bl_stack[Bl_stack_len]);
	} else {
		outflags |= MMAN_PP | MMAN_nl;
		outflags &= ~(MMAN_sp | MMAN_br);
	}

	/* Maybe we are inside an enclosing list? */
	if (NULL != n->parent->next)
		mid_it();

}

static void
pre_br(DECL_ARGS)
{
	outflags |= MMAN_br;
}

static int
pre_dl(DECL_ARGS)
{

	print_offs("6n", 0);
	return 1;
}

static void
post_dl(DECL_ARGS)
{

	print_line(".RE", MMAN_nl);

	/* Maybe we are inside an enclosing list? */
	if (NULL != n->parent->next)
		mid_it();
}

static int
pre_em(DECL_ARGS)
{

	font_push('I');
	return 1;
}

static int
pre_en(DECL_ARGS)
{

	if (NULL == n->norm->Es ||
	    NULL == n->norm->Es->child)
		return 1;

	print_word(n->norm->Es->child->string);
	outflags &= ~MMAN_spc;
	return 1;
}

static void
post_en(DECL_ARGS)
{

	if (NULL == n->norm->Es ||
	    NULL == n->norm->Es->child ||
	    NULL == n->norm->Es->child->next)
		return;

	outflags &= ~MMAN_spc;
	print_word(n->norm->Es->child->next->string);
	return;
}

static int
pre_eo(DECL_ARGS)
{

	if (n->end == ENDBODY_NOT &&
	    n->parent->head->child == NULL &&
	    n->child != NULL &&
	    n->child->end != ENDBODY_NOT)
		print_word("\\&");
	else if (n->end != ENDBODY_NOT ? n->child != NULL :
	    n->parent->head->child != NULL && (n->child != NULL ||
	    (n->parent->tail != NULL && n->parent->tail->child != NULL)))
		outflags &= ~(MMAN_spc | MMAN_nl);
	return 1;
}

static void
post_eo(DECL_ARGS)
{
	int	 body, tail;

	if (n->end != ENDBODY_NOT) {
		outflags |= MMAN_spc;
		return;
	}

	body = n->child != NULL || n->parent->head->child != NULL;
	tail = n->parent->tail != NULL && n->parent->tail->child != NULL;

	if (body && tail)
		outflags &= ~MMAN_spc;
	else if ( ! (body || tail))
		print_word("\\&");
	else if ( ! tail)
		outflags |= MMAN_spc;
}

static int
pre_fa(DECL_ARGS)
{
	int	 am_Fa;

	am_Fa = MDOC_Fa == n->tok;

	if (am_Fa)
		n = n->child;

	while (NULL != n) {
		font_push('I');
		if (am_Fa || NODE_SYNPRETTY & n->flags)
			outflags |= MMAN_nbrword;
		print_node(meta, n);
		font_pop();
		if (NULL != (n = n->next))
			print_word(",");
	}
	return 0;
}

static void
post_fa(DECL_ARGS)
{

	if (NULL != n->next && MDOC_Fa == n->next->tok)
		print_word(",");
}

static int
pre_fd(DECL_ARGS)
{

	pre_syn(n);
	font_push('B');
	return 1;
}

static void
post_fd(DECL_ARGS)
{

	font_pop();
	outflags |= MMAN_br;
}

static int
pre_fl(DECL_ARGS)
{

	font_push('B');
	print_word("\\-");
	if (n->child != NULL)
		outflags &= ~MMAN_spc;
	return 1;
}

static void
post_fl(DECL_ARGS)
{

	font_pop();
	if (!(n->child != NULL ||
	    n->next == NULL ||
	    n->next->type == ROFFT_TEXT ||
	    n->next->flags & NODE_LINE))
		outflags &= ~MMAN_spc;
}

static int
pre_fn(DECL_ARGS)
{

	pre_syn(n);

	n = n->child;
	if (NULL == n)
		return 0;

	if (NODE_SYNPRETTY & n->flags)
		print_block(".HP 4n", MMAN_nl);

	font_push('B');
	print_node(meta, n);
	font_pop();
	outflags &= ~MMAN_spc;
	print_word("(");
	outflags &= ~MMAN_spc;

	n = n->next;
	if (NULL != n)
		pre_fa(meta, n);
	return 0;
}

static void
post_fn(DECL_ARGS)
{

	print_word(")");
	if (NODE_SYNPRETTY & n->flags) {
		print_word(";");
		outflags |= MMAN_PP;
	}
}

static int
pre_fo(DECL_ARGS)
{

	switch (n->type) {
	case ROFFT_BLOCK:
		pre_syn(n);
		break;
	case ROFFT_HEAD:
		if (n->child == NULL)
			return 0;
		if (NODE_SYNPRETTY & n->flags)
			print_block(".HP 4n", MMAN_nl);
		font_push('B');
		break;
	case ROFFT_BODY:
		outflags &= ~(MMAN_spc | MMAN_nl);
		print_word("(");
		outflags &= ~MMAN_spc;
		break;
	default:
		break;
	}
	return 1;
}

static void
post_fo(DECL_ARGS)
{

	switch (n->type) {
	case ROFFT_HEAD:
		if (n->child != NULL)
			font_pop();
		break;
	case ROFFT_BODY:
		post_fn(meta, n);
		break;
	default:
		break;
	}
}

static int
pre_Ft(DECL_ARGS)
{

	pre_syn(n);
	font_push('I');
	return 1;
}

static void
pre_ft(DECL_ARGS)
{
	print_line(".ft", 0);
	print_word(n->child->string);
	outflags |= MMAN_nl;
}

static int
pre_in(DECL_ARGS)
{

	if (NODE_SYNPRETTY & n->flags) {
		pre_syn(n);
		font_push('B');
		print_word("#include <");
		outflags &= ~MMAN_spc;
	} else {
		print_word("<");
		outflags &= ~MMAN_spc;
		font_push('I');
	}
	return 1;
}

static void
post_in(DECL_ARGS)
{

	if (NODE_SYNPRETTY & n->flags) {
		outflags &= ~MMAN_spc;
		print_word(">");
		font_pop();
		outflags |= MMAN_br;
	} else {
		font_pop();
		outflags &= ~MMAN_spc;
		print_word(">");
	}
}

static int
pre_it(DECL_ARGS)
{
	const struct roff_node *bln;

	switch (n->type) {
	case ROFFT_HEAD:
		outflags |= MMAN_PP | MMAN_nl;
		bln = n->parent->parent;
		if (0 == bln->norm->Bl.comp ||
		    (NULL == n->parent->prev &&
		     NULL == bln->parent->prev))
			outflags |= MMAN_sp;
		outflags &= ~MMAN_br;
		switch (bln->norm->Bl.type) {
		case LIST_item:
			return 0;
		case LIST_inset:
		case LIST_diag:
		case LIST_ohang:
			if (bln->norm->Bl.type == LIST_diag)
				print_line(".B \"", 0);
			else
				print_line(".BR \\& \"", 0);
			outflags &= ~MMAN_spc;
			return 1;
		case LIST_bullet:
		case LIST_dash:
		case LIST_hyphen:
			print_width(&bln->norm->Bl, NULL);
			TPremain = 0;
			outflags |= MMAN_nl;
			font_push('B');
			if (LIST_bullet == bln->norm->Bl.type)
				print_word("\\(bu");
			else
				print_word("-");
			font_pop();
			outflags |= MMAN_nl;
			return 0;
		case LIST_enum:
			print_width(&bln->norm->Bl, NULL);
			TPremain = 0;
			outflags |= MMAN_nl;
			print_count(&bln->norm->Bl.count);
			outflags |= MMAN_nl;
			return 0;
		case LIST_hang:
			print_width(&bln->norm->Bl, n->child);
			TPremain = 0;
			outflags |= MMAN_nl;
			return 1;
		case LIST_tag:
			print_width(&bln->norm->Bl, n->child);
			putchar('\n');
			outflags &= ~MMAN_spc;
			return 1;
		default:
			return 1;
		}
	default:
		break;
	}
	return 1;
}

/*
 * This function is called after closing out an indented block.
 * If we are inside an enclosing list, restore its indentation.
 */
static void
mid_it(void)
{
	char		 buf[24];

	/* Nothing to do outside a list. */
	if (0 == Bl_stack_len || 0 == Bl_stack[Bl_stack_len - 1])
		return;

	/* The indentation has already been set up. */
	if (Bl_stack_post[Bl_stack_len - 1])
		return;

	/* Restore the indentation of the enclosing list. */
	print_line(".RS", MMAN_Bk_susp);
	(void)snprintf(buf, sizeof(buf), "%dn",
	    Bl_stack[Bl_stack_len - 1]);
	print_word(buf);

	/* Remeber to close out this .RS block later. */
	Bl_stack_post[Bl_stack_len - 1] = 1;
}

static void
post_it(DECL_ARGS)
{
	const struct roff_node *bln;

	bln = n->parent->parent;

	switch (n->type) {
	case ROFFT_HEAD:
		switch (bln->norm->Bl.type) {
		case LIST_diag:
			outflags &= ~MMAN_spc;
			print_word("\\ ");
			break;
		case LIST_ohang:
			outflags |= MMAN_br;
			break;
		default:
			break;
		}
		break;
	case ROFFT_BODY:
		switch (bln->norm->Bl.type) {
		case LIST_bullet:
		case LIST_dash:
		case LIST_hyphen:
		case LIST_enum:
		case LIST_hang:
		case LIST_tag:
			assert(Bl_stack_len);
			Bl_stack[--Bl_stack_len] = 0;

			/*
			 * Our indentation had to be restored
			 * after a child display or child list.
			 * Close out that indentation block now.
			 */
			if (Bl_stack_post[Bl_stack_len]) {
				print_line(".RE", MMAN_nl);
				Bl_stack_post[Bl_stack_len] = 0;
			}
			break;
		case LIST_column:
			if (NULL != n->next) {
				putchar('\t');
				outflags &= ~MMAN_spc;
			}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void
post_lb(DECL_ARGS)
{

	if (SEC_LIBRARY == n->sec)
		outflags |= MMAN_br;
}

static int
pre_lk(DECL_ARGS)
{
	const struct roff_node *link, *descr, *punct;

	if ((link = n->child) == NULL)
		return 0;

	/* Find beginning of trailing punctuation. */
	punct = n->last;
	while (punct != link && punct->flags & NODE_DELIMC)
		punct = punct->prev;
	punct = punct->next;

	/* Link text. */
	if ((descr = link->next) != NULL && descr != punct) {
		font_push('I');
		while (descr != punct) {
			print_word(descr->string);
			descr = descr->next;
		}
		font_pop();
		print_word(":");
	}

	/* Link target. */
	font_push('B');
	print_word(link->string);
	font_pop();

	/* Trailing punctuation. */
	while (punct != NULL) {
		print_word(punct->string);
		punct = punct->next;
	}
	return 0;
}

static void
pre_onearg(DECL_ARGS)
{
	outflags |= MMAN_nl;
	print_word(".");
	outflags &= ~MMAN_spc;
	print_word(roff_name[n->tok]);
	if (n->child != NULL)
		print_word(n->child->string);
	outflags |= MMAN_nl;
	if (n->tok == ROFF_ce)
		for (n = n->child->next; n != NULL; n = n->next)
			print_node(meta, n);
}

static int
pre_li(DECL_ARGS)
{

	font_push('R');
	return 1;
}

static int
pre_nm(DECL_ARGS)
{
	char	*name;

	if (n->type == ROFFT_BLOCK) {
		outflags |= MMAN_Bk;
		pre_syn(n);
	}
	if (n->type != ROFFT_ELEM && n->type != ROFFT_HEAD)
		return 1;
	name = n->child == NULL ? NULL : n->child->string;
	if (NULL == name)
		return 0;
	if (n->type == ROFFT_HEAD) {
		if (NULL == n->parent->prev)
			outflags |= MMAN_sp;
		print_block(".HP", 0);
		printf(" %dn", man_strlen(name) + 1);
		outflags |= MMAN_nl;
	}
	font_push('B');
	return 1;
}

static void
post_nm(DECL_ARGS)
{

	switch (n->type) {
	case ROFFT_BLOCK:
		outflags &= ~MMAN_Bk;
		break;
	case ROFFT_HEAD:
	case ROFFT_ELEM:
		if (n->child != NULL && n->child->string != NULL)
			font_pop();
		break;
	default:
		break;
	}
}

static int
pre_no(DECL_ARGS)
{

	outflags |= MMAN_spc_force;
	return 1;
}

static int
pre_ns(DECL_ARGS)
{

	outflags &= ~MMAN_spc;
	return 0;
}

static void
post_pf(DECL_ARGS)
{

	if ( ! (n->next == NULL || n->next->flags & NODE_LINE))
		outflags &= ~MMAN_spc;
}

static int
pre_pp(DECL_ARGS)
{

	if (MDOC_It != n->parent->tok)
		outflags |= MMAN_PP;
	outflags |= MMAN_sp | MMAN_nl;
	outflags &= ~MMAN_br;
	return 0;
}

static int
pre_rs(DECL_ARGS)
{

	if (SEC_SEE_ALSO == n->sec) {
		outflags |= MMAN_PP | MMAN_sp | MMAN_nl;
		outflags &= ~MMAN_br;
	}
	return 1;
}

static int
pre_skip(DECL_ARGS)
{

	return 0;
}

static int
pre_sm(DECL_ARGS)
{

	if (NULL == n->child)
		outflags ^= MMAN_Sm;
	else if (0 == strcmp("on", n->child->string))
		outflags |= MMAN_Sm;
	else
		outflags &= ~MMAN_Sm;

	if (MMAN_Sm & outflags)
		outflags |= MMAN_spc;

	return 0;
}

static void
pre_sp(DECL_ARGS)
{
	if (outflags & MMAN_PP) {
		outflags &= ~MMAN_PP;
		print_line(".PP", 0);
	} else {
		print_line(".sp", 0);
		if (n->child != NULL)
			print_word(n->child->string);
	}
	outflags |= MMAN_nl;
}

static int
pre_sy(DECL_ARGS)
{

	font_push('B');
	return 1;
}

static void
pre_ta(DECL_ARGS)
{
	print_line(".ta", 0);
	for (n = n->child; n != NULL; n = n->next)
		print_word(n->string);
	outflags |= MMAN_nl;
}

static int
pre_vt(DECL_ARGS)
{

	if (NODE_SYNPRETTY & n->flags) {
		switch (n->type) {
		case ROFFT_BLOCK:
			pre_syn(n);
			return 1;
		case ROFFT_BODY:
			break;
		default:
			return 0;
		}
	}
	font_push('I');
	return 1;
}

static void
post_vt(DECL_ARGS)
{

	if (n->flags & NODE_SYNPRETTY && n->type != ROFFT_BODY)
		return;
	font_pop();
}

static int
pre_xr(DECL_ARGS)
{

	n = n->child;
	if (NULL == n)
		return 0;
	print_node(meta, n);
	n = n->next;
	if (NULL == n)
		return 0;
	outflags &= ~MMAN_spc;
	print_word("(");
	print_node(meta, n);
	print_word(")");
	return 0;
}
