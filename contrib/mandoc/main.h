/*	$Id: main.h,v 1.27 2017/03/03 14:23:23 schwarze Exp $ */
/*
 * Copyright (c) 2009, 2010, 2011 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2014, 2015 Ingo Schwarze <schwarze@openbsd.org>
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

struct	roff_man;
struct	manoutput;

/*
 * Definitions for main.c-visible output device functions, e.g., -Thtml
 * and -Tascii.  Note that ascii_alloc() is named as such in
 * anticipation of latin1_alloc() and so on, all of which map into the
 * terminal output routines with different character settings.
 */

void		 *html_alloc(const struct manoutput *);
void		  html_mdoc(void *, const struct roff_man *);
void		  html_man(void *, const struct roff_man *);
void		  html_free(void *);

void		  tree_mdoc(void *, const struct roff_man *);
void		  tree_man(void *, const struct roff_man *);

void		  man_mdoc(void *, const struct roff_man *);
void		  man_man(void *, const struct roff_man *);

void		 *locale_alloc(const struct manoutput *);
void		 *utf8_alloc(const struct manoutput *);
void		 *ascii_alloc(const struct manoutput *);
void		  ascii_free(void *);

void		 *pdf_alloc(const struct manoutput *);
void		 *ps_alloc(const struct manoutput *);
void		  pspdf_free(void *);

void		  terminal_mdoc(void *, const struct roff_man *);
void		  terminal_man(void *, const struct roff_man *);
void		  terminal_sepline(void *);

void		  markdown_mdoc(void *, const struct roff_man *);
