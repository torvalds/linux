/* $NetBSD: h_comp_zlib.c,v 1.1 2014/01/14 17:51:39 pgoyette Exp $ */

/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <err.h>
#include <fcntl.h>
#include <string.h>
#include <zlib.h>

#include <sys/ioctl.h>
#include <sys/time.h>

#include <crypto/cryptodev.h>

char text[10000] = {0};

int
main(void)
{
	int fd, res;
	struct session_op cs;
	struct crypt_op co1;
	unsigned char buf1[10000], buf2[10000];
	z_stream z;

	fd = open("/dev/crypto", O_RDWR, 0);
	if (fd < 0)
		err(1, "open");
	memset(&cs, 0, sizeof(cs));
	cs.comp_alg = CRYPTO_DEFLATE_COMP;
	res = ioctl(fd, CIOCGSESSION, &cs);
	if (res < 0)
		err(1, "CIOCGSESSION");

	memset(&co1, 0, sizeof(co1));
	co1.ses = cs.ses;
	co1.op = COP_COMP;
	co1.len = sizeof(text);
	co1.src = text;
	co1.dst = buf1;
	co1.dst_len = sizeof(buf1);
	co1.flags = COP_F_BATCH;
	res = ioctl(fd, CIOCCRYPT, &co1);
	if (res < 0)
		err(1, "CIOCCRYPT");

	memset(&z, 0, sizeof(z));
	z.next_in = buf1;
	z.avail_in = co1.dst_len;
	z.zalloc = Z_NULL;
	z.zfree = Z_NULL;
	z.opaque = 0;
	z.next_out = buf2;
	z.avail_out = sizeof(buf2);
	res = inflateInit2(&z, -15);
	if (res != Z_OK)
		errx(1, "inflateInit: %d", res);
	do {
		res = inflate(&z, Z_SYNC_FLUSH);
	} while (res == Z_OK);
	if (res != Z_STREAM_END)
		errx(1, "inflate: %d", res);
	if (z.total_out != sizeof(text))
		errx(1, "decomp len %lu", z.total_out);
	if (memcmp(buf2, text, sizeof(text)))
		errx(1, "decomp data mismatch");
	return 0;
}
