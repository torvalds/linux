/*-
 * Copyright (c) 2016 IBM Corporation
 * Copyright (c) 2003-2007 Tim Kientzle
 *
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
 *
 * This test case's code has been derived from test_entry.c
 */
#include "test.h"

DEFINE_TEST(test_schily_xattr_pax)
{
	struct archive *a;
	struct archive_entry *ae;
	const char *refname = "test_read_pax_schily_xattr.tar";
	const char *xname; /* For xattr tests. */
	const void *xval; /* For xattr tests. */
	size_t xsize; /* For xattr tests. */
	const char *string, *array;

	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));

	extract_reference_file(refname);
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_open_filename(a, refname, 10240));

	assertEqualInt(ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_xattr_count(ae));
	assertEqualInt(2, archive_entry_xattr_reset(ae));

	assertEqualInt(0, archive_entry_xattr_next(ae, &xname, &xval, &xsize));
	assertEqualString(xname, "security.selinux");
	string = "system_u:object_r:unlabeled_t:s0";
	assertEqualString(xval, string);
	/* the xattr's value also contains the terminating \0 */
	assertEqualInt((int)xsize, strlen(string) + 1);

	assertEqualInt(0, archive_entry_xattr_next(ae, &xname, &xval, &xsize));
	assertEqualString(xname, "security.ima");
	assertEqualInt((int)xsize, 265);
	/* we only compare the first 12 bytes */
	array = "\x03\x02\x04\xb0\xe9\xd6\x79\x01\x00\x2b\xad\x1e";
	assertEqualMem(xval, array, 12);

	/* Close the archive. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}
