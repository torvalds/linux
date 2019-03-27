/*	$Id: mdoc.c,v 1.268 2017/08/11 16:56:21 schwarze Exp $ */
/*
 * Copyright (c) 2008, 2009, 2010, 2011 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2010, 2012-2017 Ingo Schwarze <schwarze@openbsd.org>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mandoc_aux.h"
#include "mandoc.h"
#include "roff.h"
#include "mdoc.h"
#include "libmandoc.h"
#include "roff_int.h"
#include "libmdoc.h"

const	char *const __mdoc_argnames[MDOC_ARG_MAX] = {
	"split",		"nosplit",		"ragged",
	"unfilled",		"literal",		"file",
	"offset",		"bullet",		"dash",
	"hyphen",		"item",			"enum",
	"tag",			"diag",			"hang",
	"ohang",		"inset",		"column",
	"width",		"compact",		"std",
	"filled",		"words",		"emphasis",
	"symbolic",		"nested",		"centered"
};
const	char * const *mdoc_argnames = __mdoc_argnames;

static	int		  mdoc_ptext(struct roff_man *, int, char *, int);
static	int		  mdoc_pmacro(struct roff_man *, int, char *, int);


/*
 * Main parse routine.  Parses a single line -- really just hands off to
 * the macro (mdoc_pmacro()) or text parser (mdoc_ptext()).
 */
int
mdoc_parseln(struct roff_man *mdoc, int ln, char *buf, int offs)
{

	if (mdoc->last->type != ROFFT_EQN || ln > mdoc->last->line)
		mdoc->flags |= MDOC_NEWLINE;

	/*
	 * Let the roff nS register switch SYNOPSIS mode early,
	 * such that the parser knows at all times
	 * whether this mode is on or off.
	 * Note that this mode is also switched by the Sh macro.
	 */
	if (roff_getreg(mdoc->roff, "nS"))
		mdoc->flags |= MDOC_SYNOPSIS;
	else
		mdoc->flags &= ~MDOC_SYNOPSIS;

	return roff_getcontrol(mdoc->roff, buf, &offs) ?
	    mdoc_pmacro(mdoc, ln, buf, offs) :
	    mdoc_ptext(mdoc, ln, buf, offs);
}

void
mdoc_macro(MACRO_PROT_ARGS)
{
	assert(tok >= MDOC_Dd && tok < MDOC_MAX);
	(*mdoc_macros[tok].fp)(mdoc, tok, line, ppos, pos, buf);
}

void
mdoc_tail_alloc(struct roff_man *mdoc, int line, int pos, enum roff_tok tok)
{
	struct roff_node *p;

	p = roff_node_alloc(mdoc, line, pos, ROFFT_TAIL, tok);
	roff_node_append(mdoc, p);
	mdoc->next = ROFF_NEXT_CHILD;
}

struct roff_node *
mdoc_endbody_alloc(struct roff_man *mdoc, int line, int pos,
    enum roff_tok tok, struct roff_node *body)
{
	struct roff_node *p;

	body->flags |= NODE_ENDED;
	body->parent->flags |= NODE_ENDED;
	p = roff_node_alloc(mdoc, line, pos, ROFFT_BODY, tok);
	p->body = body;
	p->norm = body->norm;
	p->end = ENDBODY_SPACE;
	roff_node_append(mdoc, p);
	mdoc->next = ROFF_NEXT_SIBLING;
	return p;
}

struct roff_node *
mdoc_block_alloc(struct roff_man *mdoc, int line, int pos,
    enum roff_tok tok, struct mdoc_arg *args)
{
	struct roff_node *p;

	p = roff_node_alloc(mdoc, line, pos, ROFFT_BLOCK, tok);
	p->args = args;
	if (p->args)
		(args->refcnt)++;

	switch (tok) {
	case MDOC_Bd:
	case MDOC_Bf:
	case MDOC_Bl:
	case MDOC_En:
	case MDOC_Rs:
		p->norm = mandoc_calloc(1, sizeof(union mdoc_data));
		break;
	default:
		break;
	}
	roff_node_append(mdoc, p);
	mdoc->next = ROFF_NEXT_CHILD;
	return p;
}

void
mdoc_elem_alloc(struct roff_man *mdoc, int line, int pos,
     enum roff_tok tok, struct mdoc_arg *args)
{
	struct roff_node *p;

	p = roff_node_alloc(mdoc, line, pos, ROFFT_ELEM, tok);
	p->args = args;
	if (p->args)
		(args->refcnt)++;

	switch (tok) {
	case MDOC_An:
		p->norm = mandoc_calloc(1, sizeof(union mdoc_data));
		break;
	default:
		break;
	}
	roff_node_append(mdoc, p);
	mdoc->next = ROFF_NEXT_CHILD;
}

void
mdoc_node_relink(struct roff_man *mdoc, struct roff_node *p)
{

	roff_node_unlink(mdoc, p);
	p->prev = p->next = NULL;
	roff_node_append(mdoc, p);
}

/*
 * Parse free-form text, that is, a line that does not begin with the
 * control character.
 */
static int
mdoc_ptext(struct roff_man *mdoc, int line, char *buf, int offs)
{
	struct roff_node *n;
	const char	 *cp, *sp;
	char		 *c, *ws, *end;

	n = mdoc->last;

	/*
	 * If a column list contains plain text, assume an implicit item
	 * macro.  This can happen one or more times at the beginning
	 * of such a list, intermixed with non-It mdoc macros and with
	 * nodes generated on the roff level, for example by tbl.
	 */

	if ((n->tok == MDOC_Bl && n->type == ROFFT_BODY &&
	     n->end == ENDBODY_NOT && n->norm->Bl.type == LIST_column) ||
	    (n->parent != NULL && n->parent->tok == MDOC_Bl &&
	     n->parent->norm->Bl.type == LIST_column)) {
		mdoc->flags |= MDOC_FREECOL;
		mdoc_macro(mdoc, MDOC_It, line, offs, &offs, buf);
		return 1;
	}

	/*
	 * Search for the beginning of unescaped trailing whitespace (ws)
	 * and for the first character not to be output (end).
	 */

	/* FIXME: replace with strcspn(). */
	ws = NULL;
	for (c = end = buf + offs; *c; c++) {
		switch (*c) {
		case ' ':
			if (NULL == ws)
				ws = c;
			continue;
		case '\t':
			/*
			 * Always warn about trailing tabs,
			 * even outside literal context,
			 * where they should be put on the next line.
			 */
			if (NULL == ws)
				ws = c;
			/*
			 * Strip trailing tabs in literal context only;
			 * outside, they affect the next line.
			 */
			if (MDOC_LITERAL & mdoc->flags)
				continue;
			break;
		case '\\':
			/* Skip the escaped character, too, if any. */
			if (c[1])
				c++;
			/* FALLTHROUGH */
		default:
			ws = NULL;
			break;
		}
		end = c + 1;
	}
	*end = '\0';

	if (ws)
		mandoc_msg(MANDOCERR_SPACE_EOL, mdoc->parse,
		    line, (int)(ws-buf), NULL);

	/*
	 * Blank lines are allowed in no-fill mode
	 * and cancel preceding \c,
	 * but add a single vertical space elsewhere.
	 */

	if (buf[offs] == '\0' && ! (mdoc->flags & MDOC_LITERAL)) {
		switch (mdoc->last->type) {
		case ROFFT_TEXT:
			sp = mdoc->last->string;
			cp = end = strchr(sp, '\0') - 2;
			if (cp < sp || cp[0] != '\\' || cp[1] != 'c')
				break;
			while (cp > sp && cp[-1] == '\\')
				cp--;
			if ((end - cp) % 2)
				break;
			*end = '\0';
			return 1;
		default:
			break;
		}
		mandoc_msg(MANDOCERR_FI_BLANK, mdoc->parse,
		    line, (int)(c - buf), NULL);
		roff_elem_alloc(mdoc, line, offs, ROFF_sp);
		mdoc->last->flags |= NODE_VALID | NODE_ENDED;
		mdoc->next = ROFF_NEXT_SIBLING;
		return 1;
	}

	roff_word_alloc(mdoc, line, offs, buf+offs);

	if (mdoc->flags & MDOC_LITERAL)
		return 1;

	/*
	 * End-of-sentence check.  If the last character is an unescaped
	 * EOS character, then flag the node as being the end of a
	 * sentence.  The front-end will know how to interpret this.
	 */

	assert(buf < end);

	if (mandoc_eos(buf+offs, (size_t)(end-buf-offs)))
		mdoc->last->flags |= NODE_EOS;

	for (c = buf + offs; c != NULL; c = strchr(c + 1, '.')) {
		if (c - buf < offs + 2)
			continue;
		if (end - c < 3)
			break;
		if (c[1] != ' ' ||
		    isalnum((unsigned char)c[-2]) == 0 ||
		    isalnum((unsigned char)c[-1]) == 0 ||
		    (c[-2] == 'n' && c[-1] == 'c') ||
		    (c[-2] == 'v' && c[-1] == 's'))
			continue;
		c += 2;
		if (*c == ' ')
			c++;
		if (*c == ' ')
			c++;
		if (isupper((unsigned char)(*c)))
			mandoc_msg(MANDOCERR_EOS, mdoc->parse,
			    line, (int)(c - buf), NULL);
	}

	return 1;
}

/*
 * Parse a macro line, that is, a line beginning with the control
 * character.
 */
static int
mdoc_pmacro(struct roff_man *mdoc, int ln, char *buf, int offs)
{
	struct roff_node *n;
	const char	 *cp;
	size_t		  sz;
	enum roff_tok	  tok;
	int		  sv;

	/* Determine the line macro. */

	sv = offs;
	tok = TOKEN_NONE;
	for (sz = 0; sz < 4 && strchr(" \t\\", buf[offs]) == NULL; sz++)
		offs++;
	if (sz == 2 || sz == 3)
		tok = roffhash_find(mdoc->mdocmac, buf + sv, sz);
	if (tok == TOKEN_NONE) {
		mandoc_msg(MANDOCERR_MACRO, mdoc->parse,
		    ln, sv, buf + sv - 1);
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

	if ('\0' == buf[offs] && ' ' == buf[offs - 1])
		mandoc_msg(MANDOCERR_SPACE_EOL, mdoc->parse,
		    ln, offs - 1, NULL);

	/*
	 * If an initial macro or a list invocation, divert directly
	 * into macro processing.
	 */

	n = mdoc->last;
	if (n == NULL || tok == MDOC_It || tok == MDOC_El) {
		mdoc_macro(mdoc, tok, ln, sv, &offs, buf);
		return 1;
	}

	/*
	 * If a column list contains a non-It macro, assume an implicit
	 * item macro.  This can happen one or more times at the
	 * beginning of such a list, intermixed with text lines and
	 * with nodes generated on the roff level, for example by tbl.
	 */

	if ((n->tok == MDOC_Bl && n->type == ROFFT_BODY &&
	     n->end == ENDBODY_NOT && n->norm->Bl.type == LIST_column) ||
	    (n->parent != NULL && n->parent->tok == MDOC_Bl &&
	     n->parent->norm->Bl.type == LIST_column)) {
		mdoc->flags |= MDOC_FREECOL;
		mdoc_macro(mdoc, MDOC_It, ln, sv, &sv, buf);
		return 1;
	}

	/* Normal processing of a macro. */

	mdoc_macro(mdoc, tok, ln, sv, &offs, buf);

	/* In quick mode (for mandocdb), abort after the NAME section. */

	if (mdoc->quick && MDOC_Sh == tok &&
	    SEC_NAME != mdoc->last->sec)
		return 2;

	return 1;
}

enum mdelim
mdoc_isdelim(const char *p)
{

	if ('\0' == p[0])
		return DELIM_NONE;

	if ('\0' == p[1])
		switch (p[0]) {
		case '(':
		case '[':
			return DELIM_OPEN;
		case '|':
			return DELIM_MIDDLE;
		case '.':
		case ',':
		case ';':
		case ':':
		case '?':
		case '!':
		case ')':
		case ']':
			return DELIM_CLOSE;
		default:
			return DELIM_NONE;
		}

	if ('\\' != p[0])
		return DELIM_NONE;

	if (0 == strcmp(p + 1, "."))
		return DELIM_CLOSE;
	if (0 == strcmp(p + 1, "fR|\\fP"))
		return DELIM_MIDDLE;

	return DELIM_NONE;
}

void
mdoc_validate(struct roff_man *mdoc)
{

	mdoc->last = mdoc->first;
	mdoc_node_validate(mdoc);
	mdoc_state_reset(mdoc);
}
