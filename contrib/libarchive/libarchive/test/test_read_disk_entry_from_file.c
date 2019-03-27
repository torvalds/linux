/*-
 * Copyright (c) 2003-2009 Tim Kientzle
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

static const char *
gname_lookup(void *d, int64_t g)
{
	(void)d; /* UNUSED */
	(void)g; /* UNUSED */
	return ("FOOGROUP");
}

static const char *
uname_lookup(void *d, int64_t u)
{
	(void)d; /* UNUSED */
	(void)u; /* UNUSED */
	return ("FOO");
}

DEFINE_TEST(test_read_disk_entry_from_file)
{
	struct archive *a;
	struct archive_entry *entry;
	FILE *f;

	assert((a = archive_read_disk_new()) != NULL);

	assertEqualInt(ARCHIVE_OK, archive_read_disk_set_uname_lookup(a,
			   NULL, &uname_lookup, NULL));
	assertEqualInt(ARCHIVE_OK, archive_read_disk_set_gname_lookup(a,
			   NULL, &gname_lookup, NULL));
	assertEqualString(archive_read_disk_uname(a, 0), "FOO");
	assertEqualString(archive_read_disk_gname(a, 0), "FOOGROUP");

	/* Create a file on disk. */
	f = fopen("foo", "wb");
	assert(f != NULL);
	assertEqualInt(4, fwrite("1234", 1, 4, f));
	fclose(f);

	/* Use archive_read_disk_entry_from_file to get information about it. */
	entry = archive_entry_new();
	assert(entry != NULL);
	archive_entry_copy_pathname(entry, "foo");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, entry, -1, NULL));

	/* Verify the information we got back. */
	assertEqualString(archive_entry_uname(entry), "FOO");
	assertEqualString(archive_entry_gname(entry), "FOOGROUP");
	assertEqualInt(archive_entry_size(entry), 4);

	/* Destroy the archive. */
	archive_entry_free(entry);
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}
