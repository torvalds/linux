/*	$Id: roff_int.h,v 1.9 2017/07/08 17:52:50 schwarze Exp $	*/
/*
 * Copyright (c) 2009, 2010, 2011 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2013, 2014, 2015 Ingo Schwarze <schwarze@openbsd.org>
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

struct roff_node *roff_node_alloc(struct roff_man *, int, int,
			enum roff_type, int);
void		  roff_node_append(struct roff_man *, struct roff_node *);
void		  roff_word_alloc(struct roff_man *, int, int, const char *);
void		  roff_word_append(struct roff_man *, const char *);
void		  roff_elem_alloc(struct roff_man *, int, int, int);
struct roff_node *roff_block_alloc(struct roff_man *, int, int, int);
struct roff_node *roff_head_alloc(struct roff_man *, int, int, int);
struct roff_node *roff_body_alloc(struct roff_man *, int, int, int);
void		  roff_node_unlink(struct roff_man *, struct roff_node *);
void		  roff_node_free(struct roff_node *);
void		  roff_node_delete(struct roff_man *, struct roff_node *);

/*
 * Functions called from roff.c need to be declared here,
 * not in libmdoc.h or libman.h, even if they are specific
 * to either the mdoc(7) or the man(7) parser.
 */

void		  man_breakscope(struct roff_man *, int);
void		  mdoc_argv_free(struct mdoc_arg *);
