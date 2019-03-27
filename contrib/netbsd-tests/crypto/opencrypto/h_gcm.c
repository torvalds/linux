/* $NetBSD: h_gcm.c,v 1.2 2014/01/17 14:16:08 pgoyette Exp $ */

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

unsigned char key[20] = { 0 };
char plaintx[16] = { 0 };
unsigned char iv[16] = { 0 };
const unsigned char ciphertx[16] = {
	0x03, 0x88, 0xda, 0xce, 0x60, 0xb6, 0xa3, 0x92,
	0xf3, 0x28, 0xc2, 0xb9, 0x71, 0xb2, 0xfe, 0x78
};
const unsigned char hash[16] = {
	0xab, 0x6e, 0x47, 0xd4, 0x2c, 0xec, 0x13, 0xbd,
	0xf5, 0x3a, 0x67, 0xb2, 0x12, 0x57, 0xbd, 0xdf
};

int
main(void)
{
	int fd, res;
	struct session_op cs;
	struct crypt_op co;
	unsigned char databuf[16];
	unsigned char macbuf[16];
	unsigned char databuf2[16];

	fd = open("/dev/crypto", O_RDWR, 0);
	if (fd < 0)
		err(1, "open");
	memset(&cs, 0, sizeof(cs));
	cs.mac = CRYPTO_AES_128_GMAC;
	cs.mackeylen = sizeof(key);
	cs.mackey = key;
	cs.cipher = CRYPTO_AES_GCM_16;
	cs.key = key;
	cs.keylen = sizeof(key);
	res = ioctl(fd, CIOCGSESSION, &cs);
	if (res < 0)
		err(1, "CIOCGSESSION");

	memset(&co, 0, sizeof(co));
	memset(databuf, 0, sizeof(databuf));
	memset(macbuf, 0, sizeof(macbuf));
	co.ses = cs.ses;
	co.op = COP_ENCRYPT;
	co.len = sizeof(plaintx);
	co.src = plaintx;
	co.dst = databuf;
	co.mac = macbuf;
	co.iv = iv;
	res = ioctl(fd, CIOCCRYPT, &co);
	if (res < 0)
		err(1, "CIOCCRYPT");
#if 1
	if (memcmp(co.dst, ciphertx, sizeof(ciphertx)))
		errx(1, "verification failed");
	if (memcmp(macbuf, hash, sizeof(hash)))
		errx(1, "hash failed");
#else
	{
		int i;
		for (i = 0; i < sizeof(databuf); i++)
			printf("%02x ", databuf[i]);
		printf("\n");
	}
	{
		int i;
		for (i = 0; i < sizeof(macbuf); i++)
			printf("%02x ", macbuf[i]);
		printf("\n");
	}
#endif
	memset(databuf2, 0, sizeof(databuf2));
	memset(macbuf, 0, sizeof(macbuf));
	co.ses = cs.ses;
	co.op = COP_DECRYPT;
	co.len = sizeof(databuf);
	co.src = databuf;
	co.dst = databuf2;
	co.mac = macbuf;
	co.iv = iv;
	res = ioctl(fd, CIOCCRYPT, &co);
	if (res < 0)
		err(1, "CIOCCRYPT");

	if (memcmp(co.dst, plaintx, sizeof(plaintx)))
		errx(1, "verification failed");
	if (memcmp(macbuf, hash, sizeof(hash)))
		errx(1, "hash failed");

	return 0;
}
