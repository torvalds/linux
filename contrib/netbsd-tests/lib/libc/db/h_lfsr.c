/*-
 * Copyright (c) 2015 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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
#include <sys/cdefs.h>
__RCSID("$NetBSD: h_lfsr.c,v 1.1 2015/11/18 18:35:35 christos Exp $");

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <err.h>
#include <string.h>
#include <unistd.h>
#include <db.h>

#define MAXKEY 0xffff
#ifdef DEBUG
#define DPRINTF(...)	printf(__VA_ARGS__)
#else
#define DPRINTF(...)
#endif

static uint16_t
next(uint16_t *cur)
{
        uint16_t lsb = *cur & 1;
        *cur >>= 1;
        *cur ^= (-lsb) & 0xB400u;
	return *cur;
}

int
main(int argc, char *argv[])
{
	char buf[65536];
	char kb[256];
	DBT key, val;
	DB *db;
	HASHINFO hi;
	uint8_t c;
	uint16_t len;
	uint32_t pagesize = atoi(argv[1]);

	memset(&hi, 0, sizeof(hi));
	memset(buf, 'a', sizeof(buf));
	hi.bsize = pagesize;
	hi.nelem = 65536;
	hi.ffactor = 128;

	key.data = kb;
	val.data = buf;

	db = dbopen(NULL, O_CREAT|O_TRUNC|O_RDWR, 0, DB_HASH, &hi);
	if (db == NULL)
		err(EXIT_FAILURE, "dbopen");

	len = 0xaec1;
	for (size_t i = 0; i < MAXKEY; i++) {
		key.size = (len & 0xff) + 1;
		c = len >> 8;
		memset(kb, c, key.size);
		val.size = (next(&len) & 0xff) + 1;
		switch ((*db->put)(db, &key, &val, R_NOOVERWRITE)) {
		case 0:
			DPRINTF("put %zu %zu %#x\n",
			    key.size, val.size, c);
			break;
		case -1:
			err(EXIT_FAILURE, "put error %zu %zu %#x",
			    key.size, val.size, c);
		case 1:
			errx(EXIT_FAILURE, "put overwrite %zu %zu %#x",
			    key.size, val.size, c);
		default:
			abort();
		}
	}

	len = 0xaec1;
	for (size_t i = 0; i < MAXKEY; i++) {
		key.size = (len & 0xff) + 1;
		c = len >> 8;
		memset(kb, c, key.size);
		next(&len);
		switch ((*db->get)(db, &key, &val, 0)) {
		case 0:
			DPRINTF("get %zu %zu %#x\n",
			    key.size, val.size, c);
			break;
		case -1:
			err(EXIT_FAILURE, "get %zu %zu %#x",
			    key.size, val.size, c);
		case 1:
			errx(EXIT_FAILURE, "get not found %zu %zu %#x",
			    key.size, val.size, c);
		default:
			abort();
		}
		if (memcmp(key.data, kb, key.size) != 0)
			errx(EXIT_FAILURE, "get badkey %zu %zu %#x",
			    key.size, val.size, c);
		if (val.size != (len & 0xff) + 1U)
			errx(EXIT_FAILURE, "get badvallen %zu %zu %#x",
			    key.size, val.size, c);
		if (memcmp(val.data, buf, val.size) != 0)
			errx(EXIT_FAILURE, "get badval %zu %zu %#x",
			    key.size, val.size, c);
	}
	
	len = 0xaec1;
	for (size_t i = 0; i < MAXKEY; i++) {
		key.size = (len & 0xff) + 1;
		c = len >> 8;
		memset(kb, c, key.size);
		next(&len);
		switch ((*db->del)(db, &key, 0)) {
		case 0:
			DPRINTF("del %zu %zu %#x\n",
			    key.size, val.size, c);
			break;
		case -1:
			err(EXIT_FAILURE, "del %zu %zu %#x", key.size,
			    val.size, c);
		case 1:
			errx(EXIT_FAILURE, "del not found %zu %zu %#x",
			    key.size, val.size, c);
		default:
			abort();
		}
	}

	len = 0xaec1;
	for (size_t i = 0; i < MAXKEY; i++) {
		key.size = (len & 0xff) + 1;
		c = len >> 8;
		memset(kb, c, key.size);
		next(&len);
		switch ((*db->get)(db, &key, &val, 0)) {
		case 0:
			errx(EXIT_FAILURE, "get2 found %zu %zu %#x",
			    key.size, val.size, c);
			break;
		case -1:
			err(EXIT_FAILURE, "get2 %zu %zu %#x",
			    key.size, val.size, c);
		case 1:
			DPRINTF("get2 %zu %zu %#x\n",
			    key.size, val.size, c);
			break;
		default:
			abort();
		}
	}
	return 0;
}
