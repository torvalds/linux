/*
 * Copyright (c) 1999 - 2005 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
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

struct test_case {
    void *val;
    int byte_len;
    const char *bytes;
    char *name;
};

typedef int (ASN1CALL *generic_encode)(unsigned char *, size_t, void *, size_t *);
typedef int (ASN1CALL *generic_length)(void *);
typedef int (ASN1CALL *generic_decode)(unsigned char *, size_t, void *, size_t *);
typedef int (ASN1CALL *generic_free)(void *);
typedef int (ASN1CALL *generic_copy)(const void *, void *);

int
generic_test (const struct test_case *tests,
	      unsigned ntests,
	      size_t data_size,
	      int (ASN1CALL *encode)(unsigned char *, size_t, void *, size_t *),
	      int (ASN1CALL *length)(void *),
	      int (ASN1CALL *decode)(unsigned char *, size_t, void *, size_t *),
	      int (ASN1CALL *free_data)(void *),
	      int (*cmp)(void *a, void *b),
	      int (ASN1CALL *copy)(const void *a, void *b));

int
generic_decode_fail(const struct test_case *tests,
		    unsigned ntests,
		    size_t data_size,
		    int (ASN1CALL *decode)(unsigned char *, size_t, void *, size_t *));


struct map_page;

enum map_type { OVERRUN, UNDERRUN };

struct map_page;

void *	map_alloc(enum map_type, const void *, size_t, struct map_page **);
void	map_free(struct map_page *, const char *, const char *);
