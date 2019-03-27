/* $OpenBSD: uuencode.c,v 1.28 2015/04/24 01:36:24 deraadt Exp $ */
/*
 * Copyright (c) 2000 Markus Friedl.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 */

#include "includes.h"

#include <sys/types.h>
#include <netinet/in.h>
#include <resolv.h>
#include <stdio.h>
#include <stdlib.h>

#include "xmalloc.h"
#include "uuencode.h"

/*
 * Encode binary 'src' of length 'srclength', writing base64-encoded text
 * to 'target' of size 'targsize'. Will always nul-terminate 'target'.
 * Returns the number of bytes stored in 'target' or -1 on error (inc.
 * 'targsize' too small).
 */
int
uuencode(const u_char *src, u_int srclength,
    char *target, size_t targsize)
{
	return __b64_ntop(src, srclength, target, targsize);
}

/*
 * Decode base64-encoded 'src' into buffer 'target' of 'targsize' bytes.
 * Will skip leading and trailing whitespace. Returns the number of bytes
 * stored in 'target' or -1 on error (inc. targsize too small).
 */
int
uudecode(const char *src, u_char *target, size_t targsize)
{
	int len;
	char *encoded, *p;

	/* copy the 'readonly' source */
	encoded = xstrdup(src);
	/* skip whitespace and data */
	for (p = encoded; *p == ' ' || *p == '\t'; p++)
		;
	for (; *p != '\0' && *p != ' ' && *p != '\t'; p++)
		;
	/* and remove trailing whitespace because __b64_pton needs this */
	*p = '\0';
	len = __b64_pton(encoded, target, targsize);
	free(encoded);
	return len;
}

void
dump_base64(FILE *fp, const u_char *data, u_int len)
{
	char *buf;
	int i, n;

	if (len > 65536) {
		fprintf(fp, "dump_base64: len > 65536\n");
		return;
	}
	buf = xreallocarray(NULL, 2, len);
	n = uuencode(data, len, buf, 2*len);
	for (i = 0; i < n; i++) {
		fprintf(fp, "%c", buf[i]);
		if (i % 70 == 69)
			fprintf(fp, "\n");
	}
	if (i % 70 != 69)
		fprintf(fp, "\n");
	free(buf);
}
