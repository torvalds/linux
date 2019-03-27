/* $NetBSD: h_comp.c,v 1.1 2014/01/14 17:51:39 pgoyette Exp $ */

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
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/time.h>

#include <crypto/cryptodev.h>

char text[100000] = {0};

int
main(void)
{
	int fd, res;
	struct session_op cs;
	struct crypt_op co1, co2;
	unsigned char buf1[10000], buf2[100000];

	fd = open("/dev/crypto", O_RDWR, 0);
	if (fd < 0)
		err(1, "open");
	memset(&cs, 0, sizeof(cs));
	cs.comp_alg = CRYPTO_GZIP_COMP;
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
	res = ioctl(fd, CIOCCRYPT, &co1);
	if (res < 0)
		err(1, "CIOCCRYPT1");
	fprintf(stderr, "len %d/%d\n", co1.len, co1.dst_len);
#if 0
	buf1[co1.dst_len - 8]++; /* modify CRC */
#endif
	write(1, buf1, co1.dst_len);
	memset(&co2, 0, sizeof(co2));
	co2.ses = cs.ses;
	co2.op = COP_DECOMP;
	co2.len = co1.dst_len;
	co2.src = buf1;
	co2.dst = buf2;
	co2.dst_len = sizeof(buf2);
	buf2[10] = 0x33;
	res = ioctl(fd, CIOCCRYPT, &co2);
	fprintf(stderr, "canary: %x\n", buf2[10]);
	if (res < 0)
		err(1, "CIOCCRYPT2");
	fprintf(stderr, "len %d/%d\n", co2.len, co2.dst_len);
	if (memcmp(text, buf2, co2.dst_len))
		errx(1, "memcmp");
	return 0;
}
