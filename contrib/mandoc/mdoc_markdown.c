/*	$Id: mdoc_markdown.c,v 1.24 2018/04/11 17:11:13 schwarze Exp $ */
/*
 * Copyright (c) 2017 Ingo Schwarze <schwarze@openbsd.org>
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
#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "mandoc_aux.h"
#include "mandoc.h"
#include "roff.h"
#include "mdoc.h"
#include "main.h"

struct	md_act {
	int		(*cond)(struct roff_node *n);
	int		(*pre)(struct roff_node *n);
	void		(*post)(struct roff_node *n);
	const char	 *prefix; /* pre-node string constant */
	const char	 *suffix; /* post-node string constant */
};

static	void	 md_nodelist(struct roff_node *);
static	void	 md_node(struct roff_node *);
static	const char *md_stack(char c);
static	void	 md_preword(void);
static	void	 md_rawword(const char *);
static	void	 md_word(const char *);
static	void	 md_named(const char *);
static	void	 md_char(unsigned char);
static	void	 md_uri(const char *);

static	int	 md_cond_head(struct roff_node *);
static	int	 md_cond_body(struct roff_node *);

static	int	 md_pre_raw(struct roff_node *);
static	int	 md_pre_word(struct roff_node *);
static	int	 md_pre_skip(struct roff_node *);
static	void	 md_pre_syn(struct roff_node *);
static	int	 md_pre_An(struct roff_node *);
static	int	 md_pre_Ap(struct roff_node *);
static	int	 md_pre_Bd(struct roff_node *);
static	int	 md_pre_Bk(struct roff_node *);
static	int	 md_pre_Bl(struct roff_node *);
static	int	 md_pre_D1(struct roff_node *);
static	int	 md_pre_Dl(struct roff_node *);
static	int	 md_pre_En(struct roff_node *);
static	int	 md_pre_Eo(struct roff_node *);
static	int	 md_pre_Fa(struct roff_node *);
static	int	 md_pre_Fd(struct roff_node *);
static	int	 md_pre_Fn(struct roff_node *);
static	int	 md_pre_Fo(struct roff_node *);
static	int	 md_pre_In(struct roff_node *);
static	int	 md_pre_It(struct roff_node *);
static	int	 md_pre_Lk(struct roff_node *);
static	int	 md_pre_Mt(struct roff_node *);
static	int	 md_pre_Nd(struct roff_node *);
static	int	 md_pre_Nm(struct roff_node *);
static	int	 md_pre_No(struct roff_node *);
static	int	 md_pre_Ns(struct roff_node *);
static	int	 md_pre_Pp(struct roff_node *);
static	int	 md_pre_Rs(struct roff_node *);
static	int	 md_pre_Sh(struct roff_node *);
static	int	 md_pre_Sm(struct roff_node *);
static	int	 md_pre_Vt(struct roff_node *);
static	int	 md_pre_Xr(struct roff_node *);
static	int	 md_pre__T(struct roff_node *);
static	int	 md_pre_br(struct roff_node *);

static	void	 md_post_raw(struct roff_node *);
static	void	 md_post_word(struct roff_node *);
static	void	 md_post_pc(struct roff_node *);
static	void	 md_post_Bk(struct roff_node *);
static	void	 md_post_Bl(struct roff_node *);
static	void	 md_post_D1(struct roff_node *);
static	void	 md_post_En(struct roff_node *);
static	void	 md_post_Eo(struct roff_node *);
static	void	 md_post_Fa(struct roff_node *);
static	void	 md_post_Fd(struct roff_node *);
static	void	 md_post_Fl(struct roff_node *);
static	void	 md_post_Fn(struct roff_node *);
static	void	 md_post_Fo(struct roff_node *);
static	void	 md_post_In(struct roff_node *);
static	void	 md_post_It(struct roff_node *);
static	void	 md_post_Lb(struct roff_node *);
static	void	 md_post_Nm(struct roff_node *);
static	void	 md_post_Pf(struct roff_node *);
static	void	 md_post_Vt(struct roff_node *);
static	void	 md_post__T(struct roff_node *);

static	const struct md_act __md_acts[MDOC_MAX - MDOC_Dd] = {
	{ NULL, NULL, NULL, NULL, NULL }, /* Dd */
	{ NULL, NULL, NULL, NULL, NULL }, /* Dt */
	{ NULL, NULL, NULL, NULL, NULL }, /* Os */
	{ NULL, md_pre_Sh, NULL, NULL, NULL }, /* Sh */
	{ NULL, md_pre_Sh, NULL, NULL, NULL }, /* Ss */
	{ NULL, md_pre_Pp, NULL, NULL, NULL }, /* Pp */
	{ md_cond_body, md_pre_D1, md_post_D1, NULL, NULL }, /* D1 */
	{ md_cond_body, md_pre_Dl, md_post_D1, NULL, NULL }, /* Dl */
	{ md_cond_body, md_pre_Bd, md_post_D1, NULL, NULL }, /* Bd */
	{ NULL, NULL, NULL, NULL, NULL }, /* Ed */
	{ md_cond_body, md_pre_Bl, md_post_Bl, NULL, NULL }, /* Bl */
	{ NULL, NULL, NULL, NULL, NULL }, /* El */
	{ NULL, md_pre_It, md_post_It, NULL, NULL }, /* It */
	{ NULL, md_pre_raw, md_post_raw, "*", "*" }, /* Ad */
	{ NULL, md_pre_An, NULL, NULL, NULL }, /* An */
	{ NULL, md_pre_Ap, NULL, NULL, NULL }, /* Ap */
	{ NULL, md_pre_raw, md_post_raw, "*", "*" }, /* Ar */
	{ NULL, md_pre_raw, md_post_raw, "**", "**" }, /* Cd */
	{ NULL, md_pre_raw, md_post_raw, "**", "**" }, /* Cm */
	{ NULL, md_pre_raw, md_post_raw, "`", "`" }, /* Dv */
	{ NULL, md_pre_raw, md_post_raw, "`", "`" }, /* Er */
	{ NULL, md_pre_raw, md_post_raw, "`", "`" }, /* Ev */
	{ NULL, NULL, NULL, NULL, NULL }, /* Ex */
	{ NULL, md_pre_Fa, md_post_Fa, NULL, NULL }, /* Fa */
	{ NULL, md_pre_Fd, md_post_Fd, "**", "**" }, /* Fd */
	{ NULL, md_pre_raw, md_post_Fl, "**-", "**" }, /* Fl */
	{ NULL, md_pre_Fn, md_post_Fn, NULL, NULL }, /* Fn */
	{ NULL, md_pre_Fd, md_post_raw, "*", "*" }, /* Ft */
	{ NULL, md_pre_raw, md_post_raw, "**", "**" }, /* Ic */
	{ NULL, md_pre_In, md_post_In, NULL, NULL }, /* In */
	{ NULL, md_pre_raw, md_post_raw, "`", "`" }, /* Li */
	{ md_cond_head, md_pre_Nd, NULL, NULL, NULL }, /* Nd */
	{ NULL, md_pre_Nm, md_post_Nm, "**", "**" }, /* Nm */
	{ md_cond_body, md_pre_word, md_post_word, "[", "]" }, /* Op */
	{ NULL, md_pre_Fd, md_post_raw, "*", "*" }, /* Ot */
	{ NULL, md_pre_raw, md_post_raw, "*", "*" }, /* Pa */
	{ NULL, NULL, NULL, NULL, NULL }, /* Rv */
	{ NULL, NULL, NULL, NULL, NULL }, /* St */
	{ NULL, md_pre_raw, md_post_raw, "*", "*" }, /* Va */
	{ NULL, md_pre_Vt, md_post_Vt, "*", "*" }, /* Vt */
	{ NULL, md_pre_Xr, NULL, NULL, NULL }, /* Xr */
	{ NULL, NULL, md_post_pc, NULL, NULL }, /* %A */
	{ NULL, md_pre_raw, md_post_pc, "*", "*" }, /* %B */
	{ NULL, NULL, md_post_pc, NULL, NULL }, /* %D */
	{ NULL, md_pre_raw, md_post_pc, "*", "*" }, /* %I */
	{ NULL, md_pre_raw, md_post_pc, "*", "*" }, /* %J */
	{ NULL, NULL, md_post_pc, NULL, NULL }, /* %N */
	{ NULL, NULL, md_post_pc, NULL, NULL }, /* %O */
	{ NULL, NULL, md_post_pc, NULL, NULL }, /* %P */
	{ NULL, NULL, md_post_pc, NULL, NULL }, /* %R */
	{ NULL, md_pre__T, md_post__T, NULL, NULL }, /* %T */
	{ NULL, NULL, md_post_pc, NULL, NULL }, /* %V */
	{ NULL, NULL, NULL, NULL, NULL }, /* Ac */
	{ md_cond_body, md_pre_word, md_post_word, "<", ">" }, /* Ao */
	{ md_cond_body, md_pre_word, md_post_word, "<", ">" }, /* Aq */
	{ NULL, NULL, NULL, NULL, NULL }, /* At */
	{ NULL, NULL, NULL, NULL, NULL }, /* Bc */
	{ NULL, NULL, NULL, NULL, NULL }, /* Bf XXX not implemented */
	{ md_cond_body, md_pre_word, md_post_word, "[", "]" }, /* Bo */
	{ md_cond_body, md_pre_word, md_post_word, "[", "]" }, /* Bq */
	{ NULL, NULL, NULL, NULL, NULL }, /* Bsx */
	{ NULL, NULL, NULL, NULL, NULL }, /* Bx */
	{ NULL, NULL, NULL, NULL, NULL }, /* Db */
	{ NULL, NULL, NULL, NULL, NULL }, /* Dc */
	{ md_cond_body, md_pre_word, md_post_word, "\"", "\"" }, /* Do */
	{ md_cond_body, md_pre_word, md_post_word, "\"", "\"" }, /* Dq */
	{ NULL, NULL, NULL, NULL, NULL }, /* Ec */
	{ NULL, NULL, NULL, NULL, NULL }, /* Ef */
	{ NULL, md_pre_raw, md_post_raw, "*", "*" }, /* Em */
	{ md_cond_body, md_pre_Eo, md_post_Eo, NULL, NULL }, /* Eo */
	{ NULL, NULL, NULL, NULL, NULL }, /* Fx */
	{ NULL, md_pre_raw, md_post_raw, "**", "**" }, /* Ms */
	{ NULL, md_pre_No, NULL, NULL, NULL }, /* No */
	{ NULL, md_pre_Ns, NULL, NULL, NULL }, /* Ns */
	{ NULL, NULL, NULL, NULL, NULL }, /* Nx */
	{ NULL, NULL, NULL, NULL, NULL }, /* Ox */
	{ NULL, NULL, NULL, NULL, NULL }, /* Pc */
	{ NULL, NULL, md_post_Pf, NULL, NULL }, /* Pf */
	{ md_cond_body, md_pre_word, md_post_word, "(", ")" }, /* Po */
	{ md_cond_body, md_pre_word, md_post_word, "(", ")" }, /* Pq */
	{ NULL, NULL, NULL, NULL, NULL }, /* Qc */
	{ md_cond_body, md_pre_raw, md_post_raw, "'`", "`'" }, /* Ql */
	{ md_cond_body, md_pre_word, md_post_word, "\"", "\"" }, /* Qo */
	{ md_cond_body, md_pre_word, md_post_word, "\"", "\"" }, /* Qq */
	{ NULL, NULL, NULL, NULL, NULL }, /* Re */
	{ md_cond_body, md_pre_Rs, NULL, NULL, NULL }, /* Rs */
	{ NULL, NULL, NULL, NULL, NULL }, /* Sc */
	{ md_cond_body, md_pre_word, md_post_word, "'", "'" }, /* So */
	{ md_cond_body, md_pre_word, md_post_word, "'", "'" }, /* Sq */
	{ NULL, md_pre_Sm, NULL, NULL, NULL }, /* Sm */
	{ NULL, md_pre_raw, md_post_raw, "*", "*" }, /* Sx */
	{ NULL, md_pre_raw, md_post_raw, "**", "**" }, /* Sy */
	{ NULL, md_pre_raw, md_post_raw, "`", "`" }, /* Tn */
	{ NULL, NULL, NULL, NULL, NULL }, /* Ux */
	{ NULL, NULL, NULL, NULL, NULL }, /* Xc */
	{ NULL, NULL, NULL, NULL, NULL }, /* Xo */
	{ NULL, md_pre_Fo, md_post_Fo, "**", "**" }, /* Fo */
	{ NULL, NULL, NULL, NULL, NULL }, /* Fc */
	{ md_cond_body, md_pre_word, md_post_word, "[", "]" }, /* Oo */
	{ NULL, NULL, NULL, NULL, NULL }, /* Oc */
	{ NULL, md_pre_Bk, md_post_Bk, NULL, NULL }, /* Bk */
	{ NULL, NULL, NULL, NULL, NULL }, /* Ek */
	{ NULL, NULL, NULL, NULL, NULL }, /* Bt */
	{ NULL, NULL, NULL, NULL, NULL }, /* Hf */
	{ NULL, md_pre_raw, md_post_raw, "*", "*" }, /* Fr */
	{ NULL, NULL, NULL, NULL, NULL }, /* Ud */
	{ NULL, NULL, md_post_Lb, NULL, NULL }, /* Lb */
	{ NULL, md_pre_Pp, NULL, NULL, NULL }, /* Lp */
	{ NULL, md_pre_Lk, NULL, NULL, NULL }, /* Lk */
	{ NULL, md_pre_Mt, NULL, NULL, NULL }, /* Mt */
	{ md_cond_body, md_pre_word, md_post_word, "{", "}" }, /* Brq */
	{ md_cond_body, md_pre_word, md_post_word, "{", "}" }, /* Bro */
	{ NULL, NULL, NULL, NULL, NULL }, /* Brc */
	{ NULL, NULL, md_post_pc, NULL, NULL }, /* %C */
	{ NULL, md_pre_skip, NULL, NULL, NULL }, /* Es */
	{ md_cond_body, md_pre_En, md_post_En, NULL, NULL }, /* En */
	{ NULL, NULL, NULL, NULL, NULL }, /* Dx */
	{ NULL, NULL, md_post_pc, NULL, NULL }, /* %Q */
	{ NULL, md_pre_Lk, md_post_pc, NULL, NULL }, /* %U */
	{ NULL, NULL, NULL, NULL, NULL }, /* Ta */
};
static	const struct md_act *const md_acts = __md_acts - MDOC_Dd;

static	int	 outflags;
#define	MD_spc		 (1 << 0)  /* Blank character before next word. */
#define	MD_spc_force	 (1 << 1)  /* Even before trailing punctuation. */
#define	MD_nonl		 (1 << 2)  /* Prevent linebreak in markdown code. */
#define	MD_nl		 (1 << 3)  /* Break markdown code line. */
#define	MD_br		 (1 << 4)  /* Insert an output line break. */
#define	MD_sp		 (1 << 5)  /* Insert a paragraph break. */
#define	MD_Sm		 (1 << 6)  /* Horizontal spacing mode. */
#define	MD_Bk		 (1 << 7)  /* Word keep mode. */
#define	MD_An_split	 (1 << 8)  /* Author mode is "split". */
#define	MD_An_nosplit	 (1 << 9)  /* Author mode is "nosplit". */

static	int	 escflags; /* Escape in generated markdown code: */
#define	ESC_BOL	 (1 << 0)  /* "#*+-" near the beginning of a line. */
#define	ESC_NUM	 (1 << 1)  /* "." after a leading number. */
#define	ESC_HYP	 (1 << 2)  /* "(" immediately after "]". */
#define	ESC_SQU	 (1 << 4)  /* "]" when "[" is open. */
#define	ESC_FON	 (1 << 5)  /* "*" immediately after unrelated "*". */
#define	ESC_EOL	 (1 << 6)  /* " " at the and of a line. */

static	int	 code_blocks, quote_blocks, list_blocks;
static	int	 outcount;

void
markdown_mdoc(void *arg, const struct roff_man *mdoc)
{
	outflags = MD_Sm;
	md_word(mdoc->meta.title);
	if (mdoc->meta.msec != NULL) {
		outflags &= ~MD_spc;
		md_word("(");
		md_word(mdoc->meta.msec);
		md_word(")");
	}
	md_word("-");
	md_word(mdoc->meta.vol);
	if (mdoc->meta.arch != NULL) {
		md_word("(");
		md_word(mdoc->meta.arch);
		md_word(")");
	}
	outflags |= MD_sp;

	md_nodelist(mdoc->first->child);

	outflags |= MD_sp;
	md_word(mdoc->meta.os);
	md_word("-");
	md_word(mdoc->meta.date);
	putchar('\n');
}

static void
md_nodelist(struct roff_node *n)
{
	while (n != NULL) {
		md_node(n);
		n = n->next;
	}
}

static void
md_node(struct roff_node *n)
{
	const struct md_act	*act;
	int			 cond, process_children;

	if (n->type == ROFFT_COMMENT || n->flags & NODE_NOPRT)
		return;

	if (outflags & MD_nonl)
		outflags &= ~(MD_nl | MD_sp);
	else if (outflags & MD_spc && n->flags & NODE_LINE)
		outflags |= MD_nl;

	act = NULL;
	cond = 0;
	process_children = 1;
	n->flags &= ~NODE_ENDED;

	if (n->type == ROFFT_TEXT) {
		if (n->flags & NODE_DELIMC)
			outflags &= ~(MD_spc | MD_spc_force);
		else if (outflags & MD_Sm)
			outflags |= MD_spc_force;
		md_word(n->string);
		if (n->flags & NODE_DELIMO)
			outflags &= ~(MD_spc | MD_spc_force);
		else if (outflags & MD_Sm)
			outflags |= MD_spc;
	} else if (n->tok < ROFF_MAX) {
		switch (n->tok) {
		case ROFF_br:
			process_children = md_pre_br(n);
			break;
		case ROFF_sp:
			process_children = md_pre_Pp(n);
			break;
		default:
			process_children = 0;
			break;
		}
	} else {
		assert(n->tok >= MDOC_Dd && n->tok < MDOC_MAX);
		act = md_acts + n->tok;
		cond = act->cond == NULL || (*act->cond)(n);
		if (cond && act->pre != NULL &&
		    (n->end == ENDBODY_NOT || n->child != NULL))
			process_children = (*act->pre)(n);
	}

	if (process_children && n->child != NULL)
		md_nodelist(n->child);

	if (n->flags & NODE_ENDED)
		return;

	if (cond && act->post != NULL)
		(*act->post)(n);

	if (n->end != ENDBODY_NOT)
		n->body->flags |= NODE_ENDED;
}

static const char *
md_stack(char c)
{
	static char	*stack;
	static size_t	 sz;
	static size_t	 cur;

	switch (c) {
	case '\0':
		break;
	case (char)-1:
		assert(cur);
		stack[--cur] = '\0';
		break;
	default:
		if (cur + 1 >= sz) {
			sz += 8;
			stack = mandoc_realloc(stack, sz);
		}
		stack[cur] = c;
		stack[++cur] = '\0';
		break;
	}
	return stack == NULL ? "" : stack;
}

/*
 * Handle vertical and horizontal spacing.
 */
static void
md_preword(void)
{
	const char	*cp;

	/*
	 * If a list block is nested inside a code block or a blockquote,
	 * blank lines for paragraph breaks no longer work; instead,
	 * they terminate the list.  Work around this markdown issue
	 * by using mere line breaks instead.
	 */

	if (list_blocks && outflags & MD_sp) {
		outflags &= ~MD_sp;
		outflags |= MD_br;
	}

	/*
	 * End the old line if requested.
	 * Escape whitespace at the end of the markdown line
	 * such that it won't look like an output line break.
	 */

	if (outflags & MD_sp)
		putchar('\n');
	else if (outflags & MD_br) {
		putchar(' ');
		putchar(' ');
	} else if (outflags & MD_nl && escflags & ESC_EOL)
		md_named("zwnj");

	/* Start a new line if necessary. */

	if (outflags & (MD_nl | MD_br | MD_sp)) {
		putchar('\n');
		for (cp = md_stack('\0'); *cp != '\0'; cp++) {
			putchar(*cp);
			if (*cp == '>')
				putchar(' ');
		}
		outflags &= ~(MD_nl | MD_br | MD_sp);
		escflags = ESC_BOL;
		outcount = 0;

	/* Handle horizontal spacing. */

	} else if (outflags & MD_spc) {
		if (outflags & MD_Bk)
			fputs("&nbsp;", stdout);
		else
			putchar(' ');
		escflags &= ~ESC_FON;
		outcount++;
	}

	outflags &= ~(MD_spc_force | MD_nonl);
	if (outflags & MD_Sm)
		outflags |= MD_spc;
	else
		outflags &= ~MD_spc;
}

/*
 * Print markdown syntax elements.
 * Can also be used for constant strings when neither escaping
 * nor delimiter handling is required.
 */
static void
md_rawword(const char *s)
{
	md_preword();

	if (*s == '\0')
		return;

	if (escflags & ESC_FON) {
		escflags &= ~ESC_FON;
		if (*s == '*' && !code_blocks)
			fputs("&zwnj;", stdout);
	}

	while (*s != '\0') {
		switch(*s) {
		case '*':
			if (s[1] == '\0')
				escflags |= ESC_FON;
			break;
		case '[':
			escflags |= ESC_SQU;
			break;
		case ']':
			escflags |= ESC_HYP;
			escflags &= ~ESC_SQU;
			break;
		default:
			break;
		}
		md_char(*s++);
	}
	if (s[-1] == ' ')
		escflags |= ESC_EOL;
	else
		escflags &= ~ESC_EOL;
}

/*
 * Print text and mdoc(7) syntax elements.
 */
static void
md_word(const char *s)
{
	const char	*seq, *prevfont, *currfont, *nextfont;
	char		 c;
	int		 bs, sz, uc, breakline;

	/* No spacing before closing delimiters. */
	if (s[0] != '\0' && s[1] == '\0' &&
	    strchr("!),.:;?]", s[0]) != NULL &&
	    (outflags & MD_spc_force) == 0)
		outflags &= ~MD_spc;

	md_preword();

	if (*s == '\0')
		return;

	/* No spacing after opening delimiters. */
	if ((s[0] == '(' || s[0] == '[') && s[1] == '\0')
		outflags &= ~MD_spc;

	breakline = 0;
	prevfont = currfont = "";
	while ((c = *s++) != '\0') {
		bs = 0;
		switch(c) {
		case ASCII_NBRSP:
			if (code_blocks)
				c = ' ';
			else {
				md_named("nbsp");
				c = '\0';
			}
			break;
		case ASCII_HYPH:
			bs = escflags & ESC_BOL && !code_blocks;
			c = '-';
			break;
		case ASCII_BREAK:
			continue;
		case '#':
		case '+':
		case '-':
			bs = escflags & ESC_BOL && !code_blocks;
			break;
		case '(':
			bs = escflags & ESC_HYP && !code_blocks;
			break;
		case ')':
			bs = escflags & ESC_NUM && !code_blocks;
			break;
		case '*':
		case '[':
		case '_':
		case '`':
			bs = !code_blocks;
			break;
		case '.':
			bs = escflags & ESC_NUM && !code_blocks;
			break;
		case '<':
			if (code_blocks == 0) {
				md_named("lt");
				c = '\0';
			}
			break;
		case '=':
			if (escflags & ESC_BOL && !code_blocks) {
				md_named("equals");
				c = '\0';
			}
			break;
		case '>':
			if (code_blocks == 0) {
				md_named("gt");
				c = '\0';
			}
			break;
		case '\\':
			uc = 0;
			nextfont = NULL;
			switch (mandoc_escape(&s, &seq, &sz)) {
			case ESCAPE_UNICODE:
				uc = mchars_num2uc(seq + 1, sz - 1);
				break;
			case ESCAPE_NUMBERED:
				uc = mchars_num2char(seq, sz);
				break;
			case ESCAPE_SPECIAL:
				uc = mchars_spec2cp(seq, sz);
				break;
			case ESCAPE_FONTBOLD:
				nextfont = "**";
				break;
			case ESCAPE_FONTITALIC:
				nextfont = "*";
				break;
			case ESCAPE_FONTBI:
				nextfont = "***";
				break;
			case ESCAPE_FONT:
			case ESCAPE_FONTROMAN:
				nextfont = "";
				break;
			case ESCAPE_FONTPREV:
				nextfont = prevfont;
				break;
			case ESCAPE_BREAK:
				breakline = 1;
				break;
			case ESCAPE_NOSPACE:
			case ESCAPE_SKIPCHAR:
			case ESCAPE_OVERSTRIKE:
				/* XXX not implemented */
				/* FALLTHROUGH */
			case ESCAPE_ERROR:
			default:
				break;
			}
			if (nextfont != NULL && !code_blocks) {
				if (*currfont != '\0') {
					outflags &= ~MD_spc;
					md_rawword(currfont);
				}
				prevfont = currfont;
				currfont = nextfont;
				if (*currfont != '\0') {
					outflags &= ~MD_spc;
					md_rawword(currfont);
				}
			}
			if (uc) {
				if ((uc < 0x20 && uc != 0x09) ||
				    (uc > 0x7E && uc < 0xA0))
					uc = 0xFFFD;
				if (code_blocks) {
					seq = mchars_uc2str(uc);
					fputs(seq, stdout);
					outcount += strlen(seq);
				} else {
					printf("&#%d;", uc);
					outcount++;
				}
				escflags &= ~ESC_FON;
			}
			c = '\0';
			break;
		case ']':
			bs = escflags & ESC_SQU && !code_blocks;
			escflags |= ESC_HYP;
			break;
		default:
			break;
		}
		if (bs)
			putchar('\\');
		md_char(c);
		if (breakline &&
		    (*s == '\0' || *s == ' ' || *s == ASCII_NBRSP)) {
			printf("  \n");
			breakline = 0;
			while (*s == ' ' || *s == ASCII_NBRSP)
				s++;
		}
	}
	if (*currfont != '\0') {
		outflags &= ~MD_spc;
		md_rawword(currfont);
	} else if (s[-2] == ' ')
		escflags |= ESC_EOL;
	else
		escflags &= ~ESC_EOL;
}

/*
 * Print a single HTML named character reference.
 */
static void
md_named(const char *s)
{
	printf("&%s;", s);
	escflags &= ~(ESC_FON | ESC_EOL);
	outcount++;
}

/*
 * Print a single raw character and maintain certain escape flags.
 */
static void
md_char(unsigned char c)
{
	if (c != '\0') {
		putchar(c);
		if (c == '*')
			escflags |= ESC_FON;
		else
			escflags &= ~ESC_FON;
		outcount++;
	}
	if (c != ']')
		escflags &= ~ESC_HYP;
	if (c == ' ' || c == '\t' || c == '>')
		return;
	if (isdigit(c) == 0)
		escflags &= ~ESC_NUM;
	else if (escflags & ESC_BOL)
		escflags |= ESC_NUM;
	escflags &= ~ESC_BOL;
}

static int
md_cond_head(struct roff_node *n)
{
	return n->type == ROFFT_HEAD;
}

static int
md_cond_body(struct roff_node *n)
{
	return n->type == ROFFT_BODY;
}

static int
md_pre_raw(struct roff_node *n)
{
	const char	*prefix;

	if ((prefix = md_acts[n->tok].prefix) != NULL) {
		md_rawword(prefix);
		outflags &= ~MD_spc;
		if (*prefix == '`')
			code_blocks++;
	}
	return 1;
}

static void
md_post_raw(struct roff_node *n)
{
	const char	*suffix;

	if ((suffix = md_acts[n->tok].suffix) != NULL) {
		outflags &= ~(MD_spc | MD_nl);
		md_rawword(suffix);
		if (*suffix == '`')
			code_blocks--;
	}
}

static int
md_pre_word(struct roff_node *n)
{
	const char	*prefix;

	if ((prefix = md_acts[n->tok].prefix) != NULL) {
		md_word(prefix);
		outflags &= ~MD_spc;
	}
	return 1;
}

static void
md_post_word(struct roff_node *n)
{
	const char	*suffix;

	if ((suffix = md_acts[n->tok].suffix) != NULL) {
		outflags &= ~(MD_spc | MD_nl);
		md_word(suffix);
	}
}

static void
md_post_pc(struct roff_node *n)
{
	md_post_raw(n);
	if (n->parent->tok != MDOC_Rs)
		return;
	if (n->next != NULL) {
		md_word(",");
		if (n->prev != NULL &&
		    n->prev->tok == n->tok &&
		    n->next->tok == n->tok)
			md_word("and");
	} else {
		md_word(".");
		outflags |= MD_nl;
	}
}

static int
md_pre_skip(struct roff_node *n)
{
	return 0;
}

static void
md_pre_syn(struct roff_node *n)
{
	if (n->prev == NULL || ! (n->flags & NODE_SYNPRETTY))
		return;

	if (n->prev->tok == n->tok &&
	    n->tok != MDOC_Ft &&
	    n->tok != MDOC_Fo &&
	    n->tok != MDOC_Fn) {
		outflags |= MD_br;
		return;
	}

	switch (n->prev->tok) {
	case MDOC_Fd:
	case MDOC_Fn:
	case MDOC_Fo:
	case MDOC_In:
	case MDOC_Vt:
		outflags |= MD_sp;
		break;
	case MDOC_Ft:
		if (n->tok != MDOC_Fn && n->tok != MDOC_Fo) {
			outflags |= MD_sp;
			break;
		}
		/* FALLTHROUGH */
	default:
		outflags |= MD_br;
		break;
	}
}

static int
md_pre_An(struct roff_node *n)
{
	switch (n->norm->An.auth) {
	case AUTH_split:
		outflags &= ~MD_An_nosplit;
		outflags |= MD_An_split;
		return 0;
	case AUTH_nosplit:
		outflags &= ~MD_An_split;
		outflags |= MD_An_nosplit;
		return 0;
	default:
		if (outflags & MD_An_split)
			outflags |= MD_br;
		else if (n->sec == SEC_AUTHORS &&
		    ! (outflags & MD_An_nosplit))
			outflags |= MD_An_split;
		return 1;
	}
}

static int
md_pre_Ap(struct roff_node *n)
{
	outflags &= ~MD_spc;
	md_word("'");
	outflags &= ~MD_spc;
	return 0;
}

static int
md_pre_Bd(struct roff_node *n)
{
	switch (n->norm->Bd.type) {
	case DISP_unfilled:
	case DISP_literal:
		return md_pre_Dl(n);
	default:
		return md_pre_D1(n);
	}
}

static int
md_pre_Bk(struct roff_node *n)
{
	switch (n->type) {
	case ROFFT_BLOCK:
		return 1;
	case ROFFT_BODY:
		outflags |= MD_Bk;
		return 1;
	default:
		return 0;
	}
}

static void
md_post_Bk(struct roff_node *n)
{
	if (n->type == ROFFT_BODY)
		outflags &= ~MD_Bk;
}

static int
md_pre_Bl(struct roff_node *n)
{
	n->norm->Bl.count = 0;
	if (n->norm->Bl.type == LIST_column)
		md_pre_Dl(n);
	outflags |= MD_sp;
	return 1;
}

static void
md_post_Bl(struct roff_node *n)
{
	n->norm->Bl.count = 0;
	if (n->norm->Bl.type == LIST_column)
		md_post_D1(n);
	outflags |= MD_sp;
}

static int
md_pre_D1(struct roff_node *n)
{
	/*
	 * Markdown blockquote syntax does not work inside code blocks.
	 * The best we can do is fall back to another nested code block.
	 */
	if (code_blocks) {
		md_stack('\t');
		code_blocks++;
	} else {
		md_stack('>');
		quote_blocks++;
	}
	outflags |= MD_sp;
	return 1;
}

static void
md_post_D1(struct roff_node *n)
{
	md_stack((char)-1);
	if (code_blocks)
		code_blocks--;
	else
		quote_blocks--;
	outflags |= MD_sp;
}

static int
md_pre_Dl(struct roff_node *n)
{
	/*
	 * Markdown code block syntax does not work inside blockquotes.
	 * The best we can do is fall back to another nested blockquote.
	 */
	if (quote_blocks) {
		md_stack('>');
		quote_blocks++;
	} else {
		md_stack('\t');
		code_blocks++;
	}
	outflags |= MD_sp;
	return 1;
}

static int
md_pre_En(struct roff_node *n)
{
	if (n->norm->Es == NULL ||
	    n->norm->Es->child == NULL)
		return 1;

	md_word(n->norm->Es->child->string);
	outflags &= ~MD_spc;
	return 1;
}

static void
md_post_En(struct roff_node *n)
{
	if (n->norm->Es == NULL ||
	    n->norm->Es->child == NULL ||
	    n->norm->Es->child->next == NULL)
		return;

	outflags &= ~MD_spc;
	md_word(n->norm->Es->child->next->string);
}

static int
md_pre_Eo(struct roff_node *n)
{
	if (n->end == ENDBODY_NOT &&
	    n->parent->head->child == NULL &&
	    n->child != NULL &&
	    n->child->end != ENDBODY_NOT)
		md_preword();
	else if (n->end != ENDBODY_NOT ? n->child != NULL :
	    n->parent->head->child != NULL && (n->child != NULL ||
	    (n->parent->tail != NULL && n->parent->tail->child != NULL)))
		outflags &= ~(MD_spc | MD_nl);
	return 1;
}

static void
md_post_Eo(struct roff_node *n)
{
	if (n->end != ENDBODY_NOT) {
		outflags |= MD_spc;
		return;
	}

	if (n->child == NULL && n->parent->head->child == NULL)
		return;

	if (n->parent->tail != NULL && n->parent->tail->child != NULL)
		outflags &= ~MD_spc;
        else
		outflags |= MD_spc;
}

static int
md_pre_Fa(struct roff_node *n)
{
	int	 am_Fa;

	am_Fa = n->tok == MDOC_Fa;

	if (am_Fa)
		n = n->child;

	while (n != NULL) {
		md_rawword("*");
		outflags &= ~MD_spc;
		md_node(n);
		outflags &= ~MD_spc;
		md_rawword("*");
		if ((n = n->next) != NULL)
			md_word(",");
	}
	return 0;
}

static void
md_post_Fa(struct roff_node *n)
{
	if (n->next != NULL && n->next->tok == MDOC_Fa)
		md_word(",");
}

static int
md_pre_Fd(struct roff_node *n)
{
	md_pre_syn(n);
	md_pre_raw(n);
	return 1;
}

static void
md_post_Fd(struct roff_node *n)
{
	md_post_raw(n);
	outflags |= MD_br;
}

static void
md_post_Fl(struct roff_node *n)
{
	md_post_raw(n);
	if (n->child == NULL && n->next != NULL &&
	    n->next->type != ROFFT_TEXT && !(n->next->flags & NODE_LINE))
		outflags &= ~MD_spc;
}

static int
md_pre_Fn(struct roff_node *n)
{
	md_pre_syn(n);

	if ((n = n->child) == NULL)
		return 0;

	md_rawword("**");
	outflags &= ~MD_spc;
	md_node(n);
	outflags &= ~MD_spc;
	md_rawword("**");
	outflags &= ~MD_spc;
	md_word("(");

	if ((n = n->next) != NULL)
		md_pre_Fa(n);
	return 0;
}

static void
md_post_Fn(struct roff_node *n)
{
	md_word(")");
	if (n->flags & NODE_SYNPRETTY) {
		md_word(";");
		outflags |= MD_sp;
	}
}

static int
md_pre_Fo(struct roff_node *n)
{
	switch (n->type) {
	case ROFFT_BLOCK:
		md_pre_syn(n);
		break;
	case ROFFT_HEAD:
		if (n->child == NULL)
			return 0;
		md_pre_raw(n);
		break;
	case ROFFT_BODY:
		outflags &= ~(MD_spc | MD_nl);
		md_word("(");
		break;
	default:
		break;
	}
	return 1;
}

static void
md_post_Fo(struct roff_node *n)
{
	switch (n->type) {
	case ROFFT_HEAD:
		if (n->child != NULL)
			md_post_raw(n);
		break;
	case ROFFT_BODY:
		md_post_Fn(n);
		break;
	default:
		break;
	}
}

static int
md_pre_In(struct roff_node *n)
{
	if (n->flags & NODE_SYNPRETTY) {
		md_pre_syn(n);
		md_rawword("**");
		outflags &= ~MD_spc;
		md_word("#include <");
	} else {
		md_word("<");
		outflags &= ~MD_spc;
		md_rawword("*");
	}
	outflags &= ~MD_spc;
	return 1;
}

static void
md_post_In(struct roff_node *n)
{
	if (n->flags & NODE_SYNPRETTY) {
		outflags &= ~MD_spc;
		md_rawword(">**");
		outflags |= MD_nl;
	} else {
		outflags &= ~MD_spc;
		md_rawword("*>");
	}
}

static int
md_pre_It(struct roff_node *n)
{
	struct roff_node	*bln;

	switch (n->type) {
	case ROFFT_BLOCK:
		return 1;

	case ROFFT_HEAD:
		bln = n->parent->parent;
		if (bln->norm->Bl.comp == 0 &&
		    bln->norm->Bl.type != LIST_column)
			outflags |= MD_sp;
		outflags |= MD_nl;

		switch (bln->norm->Bl.type) {
		case LIST_item:
			outflags |= MD_br;
			return 0;
		case LIST_inset:
		case LIST_diag:
		case LIST_ohang:
			outflags |= MD_br;
			return 1;
		case LIST_tag:
		case LIST_hang:
			outflags |= MD_sp;
			return 1;
		case LIST_bullet:
			md_rawword("*\t");
			break;
		case LIST_dash:
		case LIST_hyphen:
			md_rawword("-\t");
			break;
		case LIST_enum:
			md_preword();
			if (bln->norm->Bl.count < 99)
				bln->norm->Bl.count++;
			printf("%d.\t", bln->norm->Bl.count);
			escflags &= ~ESC_FON;
			break;
		case LIST_column:
			outflags |= MD_br;
			return 0;
		default:
			return 0;
		}
		outflags &= ~MD_spc;
		outflags |= MD_nonl;
		outcount = 0;
		md_stack('\t');
		if (code_blocks || quote_blocks)
			list_blocks++;
		return 0;

	case ROFFT_BODY:
		bln = n->parent->parent;
		switch (bln->norm->Bl.type) {
		case LIST_ohang:
			outflags |= MD_br;
			break;
		case LIST_tag:
		case LIST_hang:
			md_pre_D1(n);
			break;
		default:
			break;
		}
		return 1;

	default:
		return 0;
	}
}

static void
md_post_It(struct roff_node *n)
{
	struct roff_node	*bln;
	int			 i, nc;

	if (n->type != ROFFT_BODY)
		return;

	bln = n->parent->parent;
	switch (bln->norm->Bl.type) {
	case LIST_bullet:
	case LIST_dash:
	case LIST_hyphen:
	case LIST_enum:
		md_stack((char)-1);
		if (code_blocks || quote_blocks)
			list_blocks--;
		break;
	case LIST_tag:
	case LIST_hang:
		md_post_D1(n);
		break;

	case LIST_column:
		if (n->next == NULL)
			break;

		/* Calculate the array index of the current column. */

		i = 0;
		while ((n = n->prev) != NULL && n->type != ROFFT_HEAD)
			i++;

		/* 
		 * If a width was specified for this column,
		 * subtract what printed, and
		 * add the same spacing as in mdoc_term.c.
		 */

		nc = bln->norm->Bl.ncols;
		i = i < nc ? strlen(bln->norm->Bl.cols[i]) - outcount +
		    (nc < 5 ? 4 : nc == 5 ? 3 : 1) : 1;
		if (i < 1)
			i = 1;
		while (i-- > 0)
			putchar(' ');

		outflags &= ~MD_spc;
		escflags &= ~ESC_FON;
		outcount = 0;
		break;

	default:
		break;
	}
}

static void
md_post_Lb(struct roff_node *n)
{
	if (n->sec == SEC_LIBRARY)
		outflags |= MD_br;
}

static void
md_uri(const char *s)
{
	while (*s != '\0') {
		if (strchr("%()<>", *s) != NULL) {
			printf("%%%2.2hhX", *s);
			outcount += 3;
		} else {
			putchar(*s);
			outcount++;
		}
		s++;
	}
}

static int
md_pre_Lk(struct roff_node *n)
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
	descr = link->next;
	if (descr == punct)
		descr = link;  /* no text */
	md_rawword("[");
	outflags &= ~MD_spc;
	do {
		md_word(descr->string);
		descr = descr->next;
	} while (descr != punct);
	outflags &= ~MD_spc;

	/* Link target. */
	md_rawword("](");
	md_uri(link->string);
	outflags &= ~MD_spc;
	md_rawword(")");

	/* Trailing punctuation. */
	while (punct != NULL) {
		md_word(punct->string);
		punct = punct->next;
	}
	return 0;
}

static int
md_pre_Mt(struct roff_node *n)
{
	const struct roff_node *nch;

	md_rawword("[");
	outflags &= ~MD_spc;
	for (nch = n->child; nch != NULL; nch = nch->next)
		md_word(nch->string);
	outflags &= ~MD_spc;
	md_rawword("](mailto:");
	for (nch = n->child; nch != NULL; nch = nch->next) {
		md_uri(nch->string);
		if (nch->next != NULL) {
			putchar(' ');
			outcount++;
		}
	}
	outflags &= ~MD_spc;
	md_rawword(")");
	return 0;
}

static int
md_pre_Nd(struct roff_node *n)
{
	outflags &= ~MD_nl;
	outflags |= MD_spc;
	md_word("-");
	return 1;
}

static int
md_pre_Nm(struct roff_node *n)
{
	switch (n->type) {
	case ROFFT_BLOCK:
		outflags |= MD_Bk;
		md_pre_syn(n);
		break;
	case ROFFT_HEAD:
	case ROFFT_ELEM:
		md_pre_raw(n);
		break;
	default:
		break;
	}
	return 1;
}

static void
md_post_Nm(struct roff_node *n)
{
	switch (n->type) {
	case ROFFT_BLOCK:
		outflags &= ~MD_Bk;
		break;
	case ROFFT_HEAD:
	case ROFFT_ELEM:
		md_post_raw(n);
		break;
	default:
		break;
	}
}

static int
md_pre_No(struct roff_node *n)
{
	outflags |= MD_spc_force;
	return 1;
}

static int
md_pre_Ns(struct roff_node *n)
{
	outflags &= ~MD_spc;
	return 0;
}

static void
md_post_Pf(struct roff_node *n)
{
	if (n->next != NULL && (n->next->flags & NODE_LINE) == 0)
		outflags &= ~MD_spc;
}

static int
md_pre_Pp(struct roff_node *n)
{
	outflags |= MD_sp;
	return 0;
}

static int
md_pre_Rs(struct roff_node *n)
{
	if (n->sec == SEC_SEE_ALSO)
		outflags |= MD_sp;
	return 1;
}

static int
md_pre_Sh(struct roff_node *n)
{
	switch (n->type) {
	case ROFFT_BLOCK:
		if (n->sec == SEC_AUTHORS)
			outflags &= ~(MD_An_split | MD_An_nosplit);
		break;
	case ROFFT_HEAD:
		outflags |= MD_sp;
		md_rawword(n->tok == MDOC_Sh ? "#" : "##");
		break;
	case ROFFT_BODY:
		outflags |= MD_sp;
		break;
	default:
		break;
	}
	return 1;
}

static int
md_pre_Sm(struct roff_node *n)
{
	if (n->child == NULL)
		outflags ^= MD_Sm;
	else if (strcmp("on", n->child->string) == 0)
		outflags |= MD_Sm;
	else
		outflags &= ~MD_Sm;

	if (outflags & MD_Sm)
		outflags |= MD_spc;

	return 0;
}

static int
md_pre_Vt(struct roff_node *n)
{
	switch (n->type) {
	case ROFFT_BLOCK:
		md_pre_syn(n);
		return 1;
	case ROFFT_BODY:
	case ROFFT_ELEM:
		md_pre_raw(n);
		return 1;
	default:
		return 0;
	}
}

static void
md_post_Vt(struct roff_node *n)
{
	switch (n->type) {
	case ROFFT_BODY:
	case ROFFT_ELEM:
		md_post_raw(n);
		break;
	default:
		break;
	}
}

static int
md_pre_Xr(struct roff_node *n)
{
	n = n->child;
	if (n == NULL)
		return 0;
	md_node(n);
	n = n->next;
	if (n == NULL)
		return 0;
	outflags &= ~MD_spc;
	md_word("(");
	md_node(n);
	md_word(")");
	return 0;
}

static int
md_pre__T(struct roff_node *n)
{
	if (n->parent->tok == MDOC_Rs && n->parent->norm->Rs.quote_T)
		md_word("\"");
	else
		md_rawword("*");
	outflags &= ~MD_spc;
	return 1;
}

static void
md_post__T(struct roff_node *n)
{
	outflags &= ~MD_spc;
	if (n->parent->tok == MDOC_Rs && n->parent->norm->Rs.quote_T)
		md_word("\"");
	else
		md_rawword("*");
	md_post_pc(n);
}

static int
md_pre_br(struct roff_node *n)
{
	outflags |= MD_br;
	return 0;
}
