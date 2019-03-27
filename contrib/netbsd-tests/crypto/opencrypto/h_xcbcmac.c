/* $NetBSD: h_xcbcmac.c,v 1.4 2014/01/16 23:56:04 joerg Exp $ */

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


/* test vectors from RFC3566 */
unsigned char key[16] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};
char plaintx[1000] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	0x20, 0x21
};
const struct {
	size_t len;
	unsigned char mac[12];
} tests[] = {
	{    0, { 0x75, 0xf0, 0x25, 0x1d, 0x52, 0x8a,
		  0xc0, 0x1c, 0x45, 0x73, 0xdf, 0xd5 } },
	{    3, { 0x5b, 0x37, 0x65, 0x80, 0xae, 0x2f,
		  0x19, 0xaf, 0xe7, 0x21, 0x9c, 0xee } },
	{   16, { 0xd2, 0xa2, 0x46, 0xfa, 0x34, 0x9b,
		  0x68, 0xa7, 0x99, 0x98, 0xa4, 0x39 } },
	{   20, { 0x47, 0xf5, 0x1b, 0x45, 0x64, 0x96,
		  0x62, 0x15, 0xb8, 0x98, 0x5c, 0x63 } },
	{   32, { 0xf5, 0x4f, 0x0e, 0xc8, 0xd2, 0xb9,
		  0xf3, 0xd3, 0x68, 0x07, 0x73, 0x4b } },
	{   34,	{ 0xbe, 0xcb, 0xb3, 0xbc, 0xcd, 0xb5,
		  0x18, 0xa3, 0x06, 0x77, 0xd5, 0x48 } },
	{ 1000,	{ 0xf0, 0xda, 0xfe, 0xe8, 0x95, 0xdb,
		  0x30, 0x25, 0x37, 0x61, 0x10, 0x3b } },
};

int
main(void)
{
	int fd, res;
	size_t i;
	struct session_op cs;
	struct crypt_op co;
	unsigned char buf[16];

	fd = open("/dev/crypto", O_RDWR, 0);
	if (fd < 0)
		err(1, "open");
	memset(&cs, 0, sizeof(cs));
	cs.mac = CRYPTO_AES_XCBC_MAC_96;
	cs.mackeylen = sizeof(key);
	cs.mackey = key;
	res = ioctl(fd, CIOCGSESSION, &cs);
	if (res < 0)
		err(1, "CIOCGSESSION");

	for (i = 0; i < __arraycount(tests); i++) {
		memset(&co, 0, sizeof(co));
		memset(buf, 0, sizeof(buf));
		if (tests[i].len == sizeof(plaintx))
			memset(&plaintx, 0, sizeof(plaintx));
		co.ses = cs.ses;
		co.op = COP_ENCRYPT;
		co.len = tests[i].len;
		co.src = plaintx;
		co.mac = buf;
		res = ioctl(fd, CIOCCRYPT, &co);
		if (res < 0)
			err(1, "CIOCCRYPT test %zu", i);
		if (memcmp(buf, &tests[i].mac, sizeof(tests[i].mac)))
			errx(1, "verification failed test %zu", i);
	}
	return 0;
}
