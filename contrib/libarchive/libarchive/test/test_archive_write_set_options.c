/*-
 * Copyright (c) 2011 Tim Kientzle
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

#define should(__a, __code, __opts) \
assertEqualInt(__code, archive_write_set_options(__a, __opts))

static void
test(int pristine)
{
	struct archive* a = archive_write_new();
	int halfempty_options_rv = pristine ? ARCHIVE_FAILED : ARCHIVE_OK;
	int known_option_rv = pristine ? ARCHIVE_FAILED : ARCHIVE_OK;

	if (!pristine) {
		archive_write_add_filter_gzip(a);
		archive_write_set_format_iso9660(a);
	}

	/* NULL and "" denote `no option', so they're ok no matter
	 * what, if any, formats are registered */
	should(a, ARCHIVE_OK, NULL);
	should(a, ARCHIVE_OK, "");

	/* unknown modules and options */
	should(a, ARCHIVE_FAILED, "fubar:snafu");
	assertEqualString("Unknown module name: `fubar'",
	    archive_error_string(a));
	should(a, ARCHIVE_FAILED, "fubar:snafu=betcha");
	assertEqualString("Unknown module name: `fubar'",
	    archive_error_string(a));

	/* unknown modules and options */
	should(a, ARCHIVE_FAILED, "snafu");
	assertEqualString("Undefined option: `snafu'",
	    archive_error_string(a));
	should(a, ARCHIVE_FAILED, "snafu=betcha");
	assertEqualString("Undefined option: `snafu'",
	    archive_error_string(a));

	/* ARCHIVE_OK with iso9660 loaded, ARCHIVE_FAILED otherwise */
	should(a, known_option_rv, "iso9660:joliet");
	if (pristine) {
		assertEqualString("Unknown module name: `iso9660'",
		    archive_error_string(a));
	}
	should(a, known_option_rv, "iso9660:joliet");
	if (pristine) {
		assertEqualString("Unknown module name: `iso9660'",
		    archive_error_string(a));
	}
	should(a, known_option_rv, "joliet");
	if (pristine) {
		assertEqualString("Undefined option: `joliet'",
		    archive_error_string(a));
	}
	should(a, known_option_rv, "!joliet");
	if (pristine) {
		assertEqualString("Undefined option: `joliet'",
		    archive_error_string(a));
	}

	should(a, ARCHIVE_OK, ",");
	should(a, ARCHIVE_OK, ",,");

	should(a, halfempty_options_rv, ",joliet");
	if (pristine) {
		assertEqualString("Undefined option: `joliet'",
		    archive_error_string(a));
	}
	should(a, halfempty_options_rv, "joliet,");
	if (pristine) {
		assertEqualString("Undefined option: `joliet'",
		    archive_error_string(a));
	}

	should(a, ARCHIVE_FAILED, "joliet,snafu");
	if (pristine) {
		assertEqualString("Undefined option: `joliet'",
		    archive_error_string(a));
	} else {
		assertEqualString("Undefined option: `snafu'",
		    archive_error_string(a));
	}

	should(a, ARCHIVE_FAILED, "iso9660:snafu");
	if (pristine) {
		assertEqualString("Unknown module name: `iso9660'",
		    archive_error_string(a));
	} else {
		assertEqualString("Undefined option: `iso9660:snafu'",
		    archive_error_string(a));
	}

	archive_write_free(a);
}

DEFINE_TEST(test_archive_write_set_options)
{
	test(1);
	test(0);
}
