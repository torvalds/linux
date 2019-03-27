/*	$Id: man.c,v 1.176 2017/06/28 12:52:45 schwarze Exp $ */
/*
 * Copyright (c) 2008, 2009, 2010, 2011 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2013, 2014, 2015, 2017 Ingo Schwarze <schwarze@openbsd.org>
 * Copyright (c) 2011 Joerg Sonnenberger <joerg@netbsd.org>
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
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "mandoc_aux.h"
#include "mandoc.h"
#include "roff.h"
#include "man.h"
#include "libmandoc.h"
#include "roff_int.h"
#include "libman.h"

static	void		 man_descope(struct roff_man *, int, int);
static	int		 man_ptext(struct roff_man *, int, char *, int);
static	int		 man_pmacro(struct roff_man *, int, char *, int);


int
man_parseln(struct roff_man *man, int ln, char *buf, int offs)
{

	if (man->last->type != ROFFT_EQN || ln > man->last->line)
		man->flags |= MAN_NEWLINE;

	return roff_getcontrol(man->roff, buf, &offs) ?
	    man_pmacro(man, ln, buf, offs) :
	    man_ptext(man, ln, buf, offs);
}

static void
man_descope(struct roff_man *man, int line, int offs)
{
	/*
	 * Co-ordinate what happens with having a next-line scope open:
	 * first close out the element scope (if applicable), then close
	 * out the block scope (also if applicable).
	 */

	if (man->flags & MAN_ELINE) {
		man->flags &= ~MAN_ELINE;
		man_unscope(man, man->last->parent);
	}
	if ( ! (man->flags & MAN_BLINE))
		return;
	man->flags &= ~MAN_BLINE;
	man_unscope(man, man->last->parent);
	roff_body_alloc(man, line, offs, man->last->tok);
}

static int
man_ptext(struct roff_man *man, int line, char *buf, int offs)
{
	int		 i;
	const char 	*cp, *sp;
	char		*ep;

	/* Literal free-form text whitespace is preserved. */

	if (man->flags & MAN_LITERAL) {
		roff_word_alloc(man, line, offs, buf + offs);
		man_descope(man, line, offs);
		return 1;
	}

	for (i = offs; buf[i] == ' '; i++)
		/* Skip leading whitespace. */ ;

	/*
	 * Blank lines are ignored in next line scope
	 * and right after headings and cancel preceding \c,
	 * but add a single vertical space elsewhere.
	 */

	if (buf[i] == '\0') {
		if (man->flags & (MAN_ELINE | MAN_BLINE)) {
			mandoc_msg(MANDOCERR_BLK_BLANK, man->parse,
			    line, 0, NULL);
			return 1;
		}
		if (man->last->tok == MAN_SH || man->last->tok == MAN_SS)
			return 1;
		switch (man->last->type) {
		case ROFFT_TEXT:
			sp = man->last->string;
			cp = ep = strchr(sp, '\0') - 2;
			if (cp < sp || cp[0] != '\\' || cp[1] != 'c')
				break;
			while (cp > sp && cp[-1] == '\\')
				cp--;
			if ((ep - cp) % 2)
				break;
			*ep = '\0';
			return 1;
		default:
			break;
		}
		roff_elem_alloc(man, line, offs, ROFF_sp);
		man->next = ROFF_NEXT_SIBLING;
		return 1;
	}

	/*
	 * Warn if the last un-escaped character is whitespace. Then
	 * strip away the remaining spaces (tabs stay!).
	 */

	i = (int)strlen(buf);
	assert(i);

	if (' ' == buf[i - 1] || '\t' == buf[i - 1]) {
		if (i > 1 && '\\' != buf[i - 2])
			mandoc_msg(MANDOCERR_SPACE_EOL, man->parse,
			    line, i - 1, NULL);

		for (--i; i && ' ' == buf[i]; i--)
			/* Spin back to non-space. */ ;

		/* Jump ahead of escaped whitespace. */
		i += '\\' == buf[i] ? 2 : 1;

		buf[i] = '\0';
	}
	roff_word_alloc(man, line, offs, buf + offs);

	/*
	 * End-of-sentence check.  If the last character is an unescaped
	 * EOS character, then flag the node as being the end of a
	 * sentence.  The front-end will know how to interpret this.
	 */

	assert(i);
	if (mandoc_eos(buf, (size_t)i))
		man->last->flags |= NODE_EOS;

	man_descope(man, line, offs);
	return 1;
}

static int
man_pmacro(struct roff_man *man, int ln, char *buf, int offs)
{
	struct roff_node *n;
	const char	*cp;
	size_t		 sz;
	enum roff_tok	 tok;
	int		 ppos;
	int		 bline;

	/* Determine the line macro. */

	ppos = offs;
	tok = TOKEN_NONE;
	for (sz = 0; sz < 4 && strchr(" \t\\", buf[offs]) == NULL; sz++)
		offs++;
	if (sz > 0 && sz < 4)
		tok = roffhash_find(man->manmac, buf + ppos, sz);
	if (tok == TOKEN_NONE) {
		mandoc_msg(MANDOCERR_MACRO, man->parse,
		    ln, ppos, buf + ppos - 1);
		return 1;
	}

	/* Skip a leading escape sequence or tab. */

	switch (buf[offs]) {
	case '\\':
		cp = buf + offs + 1;
		mandoc_escape(&cp, NULL, NULL);
		offs = cp - buf;
		break;
	case '\t':
		offs++;
		break;
	default:
		break;
	}

	/* Jump to the next non-whitespace word. */

	while (buf[offs] == ' ')
		offs++;

	/*
	 * Trailing whitespace.  Note that tabs are allowed to be passed
	 * into the parser as "text", so we only warn about spaces here.
	 */

	if (buf[offs] == '\0' && buf[offs - 1] == ' ')
		mandoc_msg(MANDOCERR_SPACE_EOL, man->parse,
		    ln, offs - 1, NULL);

	/*
	 * Some macros break next-line scopes; otherwise, remember
	 * whether we are in next-line scope for a block head.
	 */

	man_breakscope(man, tok);
	bline = man->flags & MAN_BLINE;

	/*
	 * If the line in next-line scope ends with \c, keep the
	 * next-line scope open for the subsequent input line.
	 * That is not at all portable, only groff >= 1.22.4
	 * does it, but *if* this weird idiom occurs in a manual
	 * page, that's very likely what the author intended.
	 */

	if (bline) {
		cp = strchr(buf + offs, '\0') - 2;
		if (cp >= buf && cp[0] == '\\' && cp[1] == 'c')
			bline = 0;
	}

	/* Call to handler... */

	assert(man_macros[tok].fp);
	(*man_macros[tok].fp)(man, tok, ln, ppos, &offs, buf);

	/* In quick mode (for mandocdb), abort after the NAME section. */

	if (man->quick && tok == MAN_SH) {
		n = man->last;
		if (n->type == ROFFT_BODY &&
		    strcmp(n->prev->child->string, "NAME"))
			return 2;
	}

	/*
	 * If we are in a next-line scope for a block head,
	 * close it out now and switch to the body,
	 * unless the next-line scope is allowed to continue.
	 */

	if ( ! bline || man->flags & MAN_ELINE ||
	    man_macros[tok].flags & MAN_NSCOPED)
		return 1;

	assert(man->flags & MAN_BLINE);
	man->flags &= ~MAN_BLINE;

	man_unscope(man, man->last->parent);
	roff_body_alloc(man, ln, ppos, man->last->tok);
	return 1;
}

void
man_breakscope(struct roff_man *man, int tok)
{
	struct roff_node *n;

	/*
	 * An element next line scope is open,
	 * and the new macro is not allowed inside elements.
	 * Delete the element that is being broken.
	 */

	if (man->flags & MAN_ELINE && (tok < MAN_TH ||
	    ! (man_macros[tok].flags & MAN_NSCOPED))) {
		n = man->last;
		if (n->type == ROFFT_TEXT)
			n = n->parent;
		if (n->tok < MAN_TH ||
		    man_macros[n->tok].flags & MAN_NSCOPED)
			n = n->parent;

		mandoc_vmsg(MANDOCERR_BLK_LINE, man->parse,
		    n->line, n->pos, "%s breaks %s",
		    roff_name[tok], roff_name[n->tok]);

		roff_node_delete(man, n);
		man->flags &= ~MAN_ELINE;
	}

	/*
	 * Weird special case:
	 * Switching fill mode closes section headers.
	 */

	if (man->flags & MAN_BLINE &&
	    (tok == MAN_nf || tok == MAN_fi) &&
	    (man->last->tok == MAN_SH || man->last->tok == MAN_SS)) {
		n = man->last;
		man_unscope(man, n);
		roff_body_alloc(man, n->line, n->pos, n->tok);
		man->flags &= ~MAN_BLINE;
	}

	/*
	 * A block header next line scope is open,
	 * and the new macro is not allowed inside block headers.
	 * Delete the block that is being broken.
	 */

	if (man->flags & MAN_BLINE && (tok < MAN_TH ||
	    man_macros[tok].flags & MAN_BSCOPE)) {
		n = man->last;
		if (n->type == ROFFT_TEXT)
			n = n->parent;
		if (n->tok < MAN_TH ||
		    (man_macros[n->tok].flags & MAN_BSCOPE) == 0)
			n = n->parent;

		assert(n->type == ROFFT_HEAD);
		n = n->parent;
		assert(n->type == ROFFT_BLOCK);
		assert(man_macros[n->tok].flags & MAN_SCOPED);

		mandoc_vmsg(MANDOCERR_BLK_LINE, man->parse,
		    n->line, n->pos, "%s breaks %s",
		    roff_name[tok], roff_name[n->tok]);

		roff_node_delete(man, n);
		man->flags &= ~MAN_BLINE;
	}
}

const struct mparse *
man_mparse(const struct roff_man *man)
{

	assert(man && man->parse);
	return man->parse;
}

void
man_state(struct roff_man *man, struct roff_node *n)
{

	switch(n->tok) {
	case MAN_nf:
	case MAN_EX:
		if (man->flags & MAN_LITERAL && ! (n->flags & NODE_VALID))
			mandoc_msg(MANDOCERR_NF_SKIP, man->parse,
			    n->line, n->pos, "nf");
		man->flags |= MAN_LITERAL;
		break;
	case MAN_fi:
	case MAN_EE:
		if ( ! (man->flags & MAN_LITERAL) &&
		     ! (n->flags & NODE_VALID))
			mandoc_msg(MANDOCERR_FI_SKIP, man->parse,
			    n->line, n->pos, "fi");
		man->flags &= ~MAN_LITERAL;
		break;
	default:
		break;
	}
	man->last->flags |= NODE_VALID;
}

void
man_validate(struct roff_man *man)
{

	man->last = man->first;
	man_node_validate(man);
	man->flags &= ~MAN_LITERAL;
}
