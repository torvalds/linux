/* $OpenBSD: bitmap.h,v 1.2 2017/10/20 01:56:39 djm Exp $ */
/*
 * Copyright (c) 2015 Damien Miller <djm@mindrot.org>
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

#ifndef _BITMAP_H
#define _BITMAP_H

#include <sys/types.h>

/* Simple bit vector routines */

struct bitmap;

/* Allocate a new bitmap. Returns NULL on allocation failure. */
struct bitmap *bitmap_new(void);

/* Free a bitmap */
void bitmap_free(struct bitmap *b);

/* Zero an existing bitmap */
void bitmap_zero(struct bitmap *b);

/* Test whether a bit is set in a bitmap. */
int bitmap_test_bit(struct bitmap *b, u_int n);

/* Set a bit in a bitmap. Returns 0 on success or -1 on error */
int bitmap_set_bit(struct bitmap *b, u_int n);

/* Clear a bit in a bitmap */
void bitmap_clear_bit(struct bitmap *b, u_int n);

/* Return the number of bits in a bitmap (i.e. the position of the MSB) */
size_t bitmap_nbits(struct bitmap *b);

/* Return the number of bytes needed to represent a bitmap */
size_t bitmap_nbytes(struct bitmap *b);

/* Convert a bitmap to a big endian byte string */
int bitmap_to_string(struct bitmap *b, void *p, size_t l);

/* Convert a big endian byte string to a bitmap */
int bitmap_from_string(struct bitmap *b, const void *p, size_t l);

#endif /* _BITMAP_H */
