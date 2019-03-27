/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * Copyright (c) 2011 Andres Mejia
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "test.h"

/* Sanity test of internal digest functionality */

#define __LIBARCHIVE_BUILD 1
#include "archive_digest_private.h"

DEFINE_TEST(test_archive_md5)
{
	archive_md5_ctx ctx;
	unsigned char buf[] = "";
	unsigned char md[16];
	unsigned char actualmd[] = "\x93\xb8\x85\xad\xfe\x0d\xa0\x89"
                             "\xcd\xf6\x34\x90\x4f\xd5\x9f\x71";

	if (ARCHIVE_OK != archive_md5_init(&ctx)) {
		skipping("This platform does not support MD5");
		return;
	}
	assertEqualInt(ARCHIVE_OK, archive_md5_update(&ctx, buf, sizeof(buf)));
	assertEqualInt(ARCHIVE_OK, archive_md5_final(&ctx, md));
	assertEqualMem(md, actualmd, sizeof(md));
}

DEFINE_TEST(test_archive_rmd160)
{
	archive_rmd160_ctx ctx;
	unsigned char buf[] = "";
	unsigned char md[20];
	unsigned char actualmd[] = "\xc8\x1b\x94\x93\x34\x20\x22\x1a\x7a\xc0"
                             "\x04\xa9\x02\x42\xd8\xb1\xd3\xe5\x07\x0d";

	if (ARCHIVE_OK != archive_rmd160_init(&ctx)) {
		skipping("This platform does not support RMD160");
		return;
	}
	assertEqualInt(ARCHIVE_OK, archive_rmd160_update(&ctx, buf, sizeof(buf)));
	assertEqualInt(ARCHIVE_OK, archive_rmd160_final(&ctx, md));
	assertEqualMem(md, actualmd, sizeof(md));
}

DEFINE_TEST(test_archive_sha1)
{
	archive_sha1_ctx ctx;
	unsigned char buf[] = "";
	unsigned char md[20];
	unsigned char actualmd[] = "\x5b\xa9\x3c\x9d\xb0\xcf\xf9\x3f\x52\xb5"
                             "\x21\xd7\x42\x0e\x43\xf6\xed\xa2\x78\x4f";

	if (ARCHIVE_OK != archive_sha1_init(&ctx)) {
		skipping("This platform does not support SHA1");
		return;
	}
	assertEqualInt(ARCHIVE_OK, archive_sha1_update(&ctx, buf, sizeof(buf)));
	assertEqualInt(ARCHIVE_OK, archive_sha1_final(&ctx, md));
	assertEqualMem(md, actualmd, sizeof(md));
}

DEFINE_TEST(test_archive_sha256)
{
	archive_sha256_ctx ctx;
	unsigned char buf[] = "";
	unsigned char md[32];
	unsigned char actualmd[] = "\x6e\x34\x0b\x9c\xff\xb3\x7a\x98"
                             "\x9c\xa5\x44\xe6\xbb\x78\x0a\x2c"
                             "\x78\x90\x1d\x3f\xb3\x37\x38\x76"
                             "\x85\x11\xa3\x06\x17\xaf\xa0\x1d";

	if (ARCHIVE_OK != archive_sha256_init(&ctx)) {
		skipping("This platform does not support SHA256");
		return;
	}
	assertEqualInt(ARCHIVE_OK, archive_sha256_update(&ctx, buf, sizeof(buf)));
	assertEqualInt(ARCHIVE_OK, archive_sha256_final(&ctx, md));
	assertEqualMem(md, actualmd, sizeof(md));
}

DEFINE_TEST(test_archive_sha384)
{
	archive_sha384_ctx ctx;
	unsigned char buf[] = "";
	unsigned char md[48];
	unsigned char actualmd[] = "\xbe\xc0\x21\xb4\xf3\x68\xe3\x06"
                             "\x91\x34\xe0\x12\xc2\xb4\x30\x70"
                             "\x83\xd3\xa9\xbd\xd2\x06\xe2\x4e"
                             "\x5f\x0d\x86\xe1\x3d\x66\x36\x65"
                             "\x59\x33\xec\x2b\x41\x34\x65\x96"
                             "\x68\x17\xa9\xc2\x08\xa1\x17\x17";

	if (ARCHIVE_OK != archive_sha384_init(&ctx)) {
		skipping("This platform does not support SHA384");
		return;
	}
	assertEqualInt(ARCHIVE_OK, archive_sha384_update(&ctx, buf, sizeof(buf)));
	assertEqualInt(ARCHIVE_OK, archive_sha384_final(&ctx, md));
	assertEqualMem(md, actualmd, sizeof(md));
}

DEFINE_TEST(test_archive_sha512)
{
	archive_sha512_ctx ctx;
	unsigned char buf[] = "";
	unsigned char md[64];
	unsigned char actualmd[] = "\xb8\x24\x4d\x02\x89\x81\xd6\x93"
                             "\xaf\x7b\x45\x6a\xf8\xef\xa4\xca"
                             "\xd6\x3d\x28\x2e\x19\xff\x14\x94"
                             "\x2c\x24\x6e\x50\xd9\x35\x1d\x22"
                             "\x70\x4a\x80\x2a\x71\xc3\x58\x0b"
                             "\x63\x70\xde\x4c\xeb\x29\x3c\x32"
                             "\x4a\x84\x23\x34\x25\x57\xd4\xe5"
                             "\xc3\x84\x38\xf0\xe3\x69\x10\xee";

	if (ARCHIVE_OK != archive_sha512_init(&ctx)) {
		skipping("This platform does not support SHA512");
		return;
	}
	assertEqualInt(ARCHIVE_OK, archive_sha512_update(&ctx, buf, sizeof(buf)));
	assertEqualInt(ARCHIVE_OK, archive_sha512_final(&ctx, md));
	assertEqualMem(md, actualmd, sizeof(md));
}
