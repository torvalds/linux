/*	$Id: dba_write.h,v 1.1 2016/07/19 21:31:55 schwarze Exp $ */
/*
 * Copyright (c) 2016 Ingo Schwarze <schwarze@openbsd.org>
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
 *
 * Internal interface to low-level functions
 * for serializing allocation-based data to disk.
 * For use by dba_array.c and dba.c only.
 */

int	 dba_open(const char *);
int	 dba_close(void);
int32_t	 dba_tell(void);
void	 dba_seek(int32_t);
int32_t	 dba_align(void);
int32_t	 dba_skip(int32_t, int32_t);
void	 dba_char_write(int);
void	 dba_str_write(const char *);
void	 dba_int_write(int32_t);
