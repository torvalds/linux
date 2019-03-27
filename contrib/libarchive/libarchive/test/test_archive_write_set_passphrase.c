/*-
 * Copyright (c) 2011 Tim Kientzle
 * Copyright (c) 2014 Michihiro NAKAJIMA
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
__FBSDID("$FreeBSD$");

struct archive_write;
extern const char * __archive_write_get_passphrase(struct archive_write *);

static void
test(int pristine)
{
	struct archive* a = archive_write_new();
	struct archive_write* aw = (struct archive_write *)a;

	if (!pristine) {
		archive_write_add_filter_gzip(a);
		archive_write_set_format_iso9660(a);
        }

	assertEqualInt(ARCHIVE_OK, archive_write_set_passphrase(a, "pass1"));
	/* An empty passphrase cannot be accepted. */
	assertEqualInt(ARCHIVE_FAILED, archive_write_set_passphrase(a, ""));
	/* NULL passphrases cannot be accepted. */
	assertEqualInt(ARCHIVE_FAILED, archive_write_set_passphrase(a, NULL));
	/* Check a passphrase. */
	assertEqualString("pass1", __archive_write_get_passphrase(aw));
	/* Change the passphrase. */
	assertEqualInt(ARCHIVE_OK, archive_write_set_passphrase(a, "pass2"));
	assertEqualString("pass2", __archive_write_get_passphrase(aw));

	archive_write_free(a);
}

DEFINE_TEST(test_archive_write_set_passphrase)
{
	test(1);
	test(0);
}


static const char *
callback1(struct archive *a, void *_client_data)
{
	int *cnt;

	(void)a; /* UNUSED */

	cnt = (int *)_client_data;
	*cnt += 1;
	return ("passCallBack");
}

DEFINE_TEST(test_archive_write_set_passphrase_callback)
{
	struct archive* a = archive_write_new();
	struct archive_write* aw = (struct archive_write *)a;
	int cnt = 0;

	archive_write_set_format_zip(a);

	assertEqualInt(ARCHIVE_OK,
	    archive_write_set_passphrase_callback(a, &cnt, callback1));
	/* Check a passphrase. */
	assertEqualString("passCallBack", __archive_write_get_passphrase(aw));
	assertEqualInt(1, cnt);
	/* Callback function should be called just once. */
	assertEqualString("passCallBack", __archive_write_get_passphrase(aw));
	assertEqualInt(1, cnt);

	archive_write_free(a);
}
