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

struct archive_read;
extern void __archive_read_reset_passphrase(struct archive_read *);
extern const char * __archive_read_next_passphrase(struct archive_read *);

static void
test(int pristine)
{
	struct archive* a = archive_read_new();

	if (!pristine) {
		archive_read_support_filter_all(a);
		archive_read_support_format_all(a);
        }

	assertEqualInt(ARCHIVE_OK, archive_read_add_passphrase(a, "pass1"));
	/* An empty passphrase cannot be accepted. */
	assertEqualInt(ARCHIVE_FAILED, archive_read_add_passphrase(a, ""));
	/* NULL passphrases cannot be accepted. */
	assertEqualInt(ARCHIVE_FAILED, archive_read_add_passphrase(a, NULL));

	archive_read_free(a);
}

DEFINE_TEST(test_archive_read_add_passphrase)
{
	test(1);
	test(0);
}

DEFINE_TEST(test_archive_read_add_passphrase_incorrect_sequance)
{
	struct archive* a = archive_read_new();
	struct archive_read *ar = (struct archive_read *)a;

	assertEqualInt(ARCHIVE_OK, archive_read_add_passphrase(a, "pass1"));

	/* No call of __archive_read_reset_passphrase() leads to
	 * get NULL even if a user has passed a passphrases. */
	assertEqualString(NULL, __archive_read_next_passphrase(ar));

	archive_read_free(a);
}

DEFINE_TEST(test_archive_read_add_passphrase_single)
{
	struct archive* a = archive_read_new();
	struct archive_read *ar = (struct archive_read *)a;

	assertEqualInt(ARCHIVE_OK, archive_read_add_passphrase(a, "pass1"));

	__archive_read_reset_passphrase(ar);
	/* Fist call, we should get "pass1" as a passphrase. */
	assertEqualString("pass1", __archive_read_next_passphrase(ar));
	/* Second call, we should get NULL which means all the passphrases
	 * are passed already. */
	assertEqualString(NULL, __archive_read_next_passphrase(ar));

	archive_read_free(a);
}

DEFINE_TEST(test_archive_read_add_passphrase_multiple)
{
	struct archive* a = archive_read_new();
	struct archive_read *ar = (struct archive_read *)a;

	assertEqualInt(ARCHIVE_OK, archive_read_add_passphrase(a, "pass1"));
	assertEqualInt(ARCHIVE_OK, archive_read_add_passphrase(a, "pass2"));

	__archive_read_reset_passphrase(ar);
	/* Fist call, we should get "pass1" as a passphrase. */
	assertEqualString("pass1", __archive_read_next_passphrase(ar));
	/* Second call, we should get "pass2" as a passphrase. */
	assertEqualString("pass2", __archive_read_next_passphrase(ar));
	/* Third call, we should get NULL which means all the passphrases
	 * are passed already. */
	assertEqualString(NULL, __archive_read_next_passphrase(ar));

	archive_read_free(a);
}

static const char *
callback1(struct archive *a, void *_client_data)
{
	(void)a; /* UNUSED */
	(void)_client_data; /* UNUSED */
	return ("passCallBack");
}

DEFINE_TEST(test_archive_read_add_passphrase_set_callback1)
{
	struct archive* a = archive_read_new();
	struct archive_read *ar = (struct archive_read *)a;

	assertEqualInt(ARCHIVE_OK,
	    archive_read_set_passphrase_callback(a, NULL, callback1));

	__archive_read_reset_passphrase(ar);
	/* Fist call, we should get "passCallBack" as a passphrase. */
	assertEqualString("passCallBack", __archive_read_next_passphrase(ar));
	/* Second call, we still get "passCallBack" as a passphrase. */
	assertEqualString("passCallBack", __archive_read_next_passphrase(ar));

	archive_read_free(a);

	/* Without __archive_read_reset_passphrase call, the callback
	 * should work fine. */
	a = archive_read_new();
	ar = (struct archive_read *)a;
	assertEqualInt(ARCHIVE_OK,
	    archive_read_set_passphrase_callback(a, NULL, callback1));
	/* Fist call, we should get "passCallBack" as a passphrase. */
	assertEqualString("passCallBack", __archive_read_next_passphrase(ar));
	/* Second call, we still get "passCallBack" as a passphrase. */
	assertEqualString("passCallBack", __archive_read_next_passphrase(ar));

	archive_read_free(a);
}

static const char *
callback2(struct archive *a, void *_client_data)
{
	int *cd = (int *)_client_data;

	(void)a; /* UNUSED */

	if (*cd == 0) {
		*cd = 1;
		return ("passCallBack");
	}
	return (NULL);
}

DEFINE_TEST(test_archive_read_add_passphrase_set_callback2)
{
	struct archive* a = archive_read_new();
	struct archive_read *ar = (struct archive_read *)a;
	int client_data = 0;

	assertEqualInt(ARCHIVE_OK,
	    archive_read_set_passphrase_callback(a, &client_data, callback2));

	__archive_read_reset_passphrase(ar);
	/* Fist call, we should get "passCallBack" as a passphrase. */
	assertEqualString("passCallBack", __archive_read_next_passphrase(ar));
	/* Second call, we should get NULL which means all the passphrases
	 * are passed already. */
	assertEqualString(NULL, __archive_read_next_passphrase(ar));

	archive_read_free(a);
}

DEFINE_TEST(test_archive_read_add_passphrase_set_callback3)
{
	struct archive* a = archive_read_new();
	struct archive_read *ar = (struct archive_read *)a;
	int client_data = 0;

	assertEqualInt(ARCHIVE_OK,
	    archive_read_set_passphrase_callback(a, &client_data, callback2));

	__archive_read_reset_passphrase(ar);
	/* Fist call, we should get "passCallBack" as a passphrase. */
	assertEqualString("passCallBack", __archive_read_next_passphrase(ar));
	__archive_read_reset_passphrase(ar);
	/* After reset passphrase, we should get "passCallBack" passphrase. */
	assertEqualString("passCallBack", __archive_read_next_passphrase(ar));
	/* Second call, we should get NULL which means all the passphrases
	 * are passed already. */
	assertEqualString(NULL, __archive_read_next_passphrase(ar));

	archive_read_free(a);
}

DEFINE_TEST(test_archive_read_add_passphrase_multiple_with_callback)
{
	struct archive* a = archive_read_new();
	struct archive_read *ar = (struct archive_read *)a;
	int client_data = 0;

	assertEqualInt(ARCHIVE_OK, archive_read_add_passphrase(a, "pass1"));
	assertEqualInt(ARCHIVE_OK, archive_read_add_passphrase(a, "pass2"));
	assertEqualInt(ARCHIVE_OK,
	    archive_read_set_passphrase_callback(a, &client_data, callback2));

	__archive_read_reset_passphrase(ar);
	/* Fist call, we should get "pass1" as a passphrase. */
	assertEqualString("pass1", __archive_read_next_passphrase(ar));
	/* Second call, we should get "pass2" as a passphrase. */
	assertEqualString("pass2", __archive_read_next_passphrase(ar));
	/* Third call, we should get "passCallBack" as a passphrase. */
	assertEqualString("passCallBack", __archive_read_next_passphrase(ar));
	/* Fourth call, we should get NULL which means all the passphrases
	 * are passed already. */
	assertEqualString(NULL, __archive_read_next_passphrase(ar));

	archive_read_free(a);
}

DEFINE_TEST(test_archive_read_add_passphrase_multiple_with_callback2)
{
	struct archive* a = archive_read_new();
	struct archive_read *ar = (struct archive_read *)a;
	int client_data = 0;

	assertEqualInt(ARCHIVE_OK, archive_read_add_passphrase(a, "pass1"));
	assertEqualInt(ARCHIVE_OK, archive_read_add_passphrase(a, "pass2"));
	assertEqualInt(ARCHIVE_OK,
	    archive_read_set_passphrase_callback(a, &client_data, callback2));

	__archive_read_reset_passphrase(ar);
	/* Fist call, we should get "pass1" as a passphrase. */
	assertEqualString("pass1", __archive_read_next_passphrase(ar));
	/* Second call, we should get "pass2" as a passphrase. */
	assertEqualString("pass2", __archive_read_next_passphrase(ar));
	/* Third call, we should get "passCallBack" as a passphrase. */
	assertEqualString("passCallBack", __archive_read_next_passphrase(ar));

	__archive_read_reset_passphrase(ar);
	/* After reset passphrase, we should get "passCallBack" passphrase. */
	assertEqualString("passCallBack", __archive_read_next_passphrase(ar));
	/* Second call, we should get "pass1" as a passphrase. */
	assertEqualString("pass1", __archive_read_next_passphrase(ar));
	/* Third call, we should get "passCallBack" as a passphrase. */
	assertEqualString("pass2", __archive_read_next_passphrase(ar));
	/* Fourth call, we should get NULL which means all the passphrases
	 * are passed already. */
	assertEqualString(NULL, __archive_read_next_passphrase(ar));

	archive_read_free(a);
}

