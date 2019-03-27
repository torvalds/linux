/*
 * Copyright (c) 2003-2005 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef __HEIM_ANY_H__
#define __HEIM_ANY_H__ 1

int	encode_heim_any(unsigned char *, size_t, const heim_any *, size_t *);
int	decode_heim_any(const unsigned char *, size_t, heim_any *, size_t *);
void	free_heim_any(heim_any *);
size_t	length_heim_any(const heim_any *);
int	copy_heim_any(const heim_any *, heim_any *);

int	encode_heim_any_set(unsigned char *, size_t,
			    const heim_any_set *, size_t *);
int	decode_heim_any_set(const unsigned char *, size_t,
			    heim_any_set *,size_t *);
void	free_heim_any_set(heim_any_set *);
size_t	length_heim_any_set(const heim_any_set *);
int	copy_heim_any_set(const heim_any_set *, heim_any_set *);
int	heim_any_cmp(const heim_any_set *, const heim_any_set *);

#endif /* __HEIM_ANY_H__ */
