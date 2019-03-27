/*	$NetBSD: returns.h,v 1.1 2011/04/10 09:55:09 blymn Exp $	*/

/*-
 * Copyright 2009 Brett Lymn <blymn@NetBSD.org>
 *
 * All rights reserved.
 *
 * This code has been donated to The NetBSD Foundation by the Author.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software withough specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 */
#ifndef CTF_RETURNS_H
#define CTF_RETURNS_H 1


typedef enum {
	ret_number = 1,
	ret_string,
	ret_byte,
	ret_err,
	ret_ok,
	ret_null,
	ret_nonnull,
	ret_var,
	ret_ref,
	ret_count,
	ret_slave_error
} returns_enum_t;

typedef struct {
	returns_enum_t	return_type;
	void		*return_value;	/* used if return_type is ret_num or
					   or ret_byte or ret_string */
	size_t		return_len;	/* number of bytes in return_value iff
					   return_type is ret_byte */
	int		return_index;	/* index into var array for return
					   if return_type is ret_var */
} returns_t;

typedef struct {
	size_t	count;
	size_t	allocated;
	size_t	readp;
	char	*data;
} saved_data_t;

#endif /* CTF_RETURNS_H */
