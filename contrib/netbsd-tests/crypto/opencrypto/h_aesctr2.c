/* $NetBSD: h_aesctr2.c,v 1.2 2014/01/17 14:16:08 pgoyette Exp $ */

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

#include <sys/ioctl.h>
#include <sys/time.h>

#include <crypto/cryptodev.h>

unsigned char key[20] = {0xae, 0x68, 0x52, 0xf8, 0x12, 0x10, 0x67, 0xcc,
			 0x4b, 0xf7, 0xa5, 0x76, 0x55, 0x77, 0xf3, 0x9e,
	0x00, 0x00, 0x00, 0x30};
unsigned char iv[8] = {0};
char plaintx[16] = "Single block msg";
const unsigned char ciphertx[16] = {
	0xe4, 0x09, 0x5d, 0x4f, 0xb7, 0xa7, 0xb3, 0x79,
	0x2d, 0x61, 0x75, 0xa3, 0x26, 0x13, 0x11, 0xb8
};

int
main(void)
{
	int fd, res;
	struct session_op cs;
	struct crypt_op co;
	unsigned char ibuf[24];
	unsigned char obuf[24];

	fd = open("/dev/crypto", O_RDWR, 0);
	if (fd < 0)
		err(1, "open");
	memset(&cs, 0, sizeof(cs));
	cs.cipher = CRYPTO_AES_CTR;
	cs.keylen = 20;
	cs.key = key;
	res = ioctl(fd, CIOCGSESSION, &cs);
	if (res < 0)
		err(1, "CIOCGSESSION");

	memcpy(ibuf, iv, 8);
	memcpy(ibuf + 8, plaintx, 16);
	memset(&co, 0, sizeof(co));
	co.ses = cs.ses;
	co.op = COP_ENCRYPT;
	co.len = sizeof(ibuf);
	co.src = ibuf;
	co.dst = obuf;
	co.dst_len = sizeof(obuf);
	res = ioctl(fd, CIOCCRYPT, &co);
	if (res < 0)
		err(1, "CIOCCRYPT");
	memset(ibuf, 0, sizeof(ibuf));
	memset(&co, 0, sizeof(co));
	co.ses = cs.ses;
	co.op = COP_DECRYPT;
	co.len = sizeof(obuf);
	co.src = obuf;
	co.dst = ibuf;
	co.dst_len = sizeof(ibuf);
	res = ioctl(fd, CIOCCRYPT, &co);
	if (res < 0)
		err(1, "CIOCCRYPT");

	if (memcmp((char *)co.dst + 8, plaintx, sizeof(plaintx)))
		warnx("verification failed");

	return 0;
}
