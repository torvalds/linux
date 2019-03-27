/*-
 * Copyright (c) 2003-2007 Tim Kientzle
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

static unsigned char testdata[10 * 1024 * 1024];
static unsigned char testdatacopy[10 * 1024 * 1024];
static unsigned char buff[11 * 1024 * 1024];

#if defined(_WIN32) && !defined(__CYGWIN__)
#define open _open
#define close _close
#endif

/* Check correct behavior on large reads. */
DEFINE_TEST(test_read_large)
{
	unsigned int i;
	int tmpfilefd;
	char tmpfilename[] = "test-read_large.XXXXXX";
	size_t used;
	struct archive *a;
	struct archive_entry *entry;
	FILE *f;

	for (i = 0; i < sizeof(testdata); i++)
		testdata[i] = (unsigned char)(rand());

	assert(NULL != (a = archive_write_new()));
	assertA(0 == archive_write_set_format_ustar(a));
	assertA(0 == archive_write_open_memory(a, buff, sizeof(buff), &used));
	assert(NULL != (entry = archive_entry_new()));
	archive_entry_set_size(entry, sizeof(testdata));
	archive_entry_set_mode(entry, S_IFREG | 0777);
	archive_entry_set_pathname(entry, "test");
	assertA(0 == archive_write_header(a, entry));
	archive_entry_free(entry);
	assertA((int)sizeof(testdata) == archive_write_data(a, testdata, sizeof(testdata)));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	assert(NULL != (a = archive_read_new()));
	assertA(0 == archive_read_support_format_all(a));
	assertA(0 == archive_read_support_filter_all(a));
	assertA(0 == archive_read_open_memory(a, buff, sizeof(buff)));
	assertA(0 == archive_read_next_header(a, &entry));
	assertEqualIntA(a, sizeof(testdatacopy),
	    archive_read_data(a, testdatacopy, sizeof(testdatacopy)));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	assertEqualMem(testdata, testdatacopy, sizeof(testdata));


	assert(NULL != (a = archive_read_new()));
	assertA(0 == archive_read_support_format_all(a));
	assertA(0 == archive_read_support_filter_all(a));
	assertA(0 == archive_read_open_memory(a, buff, sizeof(buff)));
	assertA(0 == archive_read_next_header(a, &entry));
#if defined(__BORLANDC__)
	tmpfilefd = open(tmpfilename, O_WRONLY | O_CREAT | O_BINARY);
#else
	tmpfilefd = open(tmpfilename, O_WRONLY | O_CREAT | O_BINARY, 0755);
#endif
	assert(0 < tmpfilefd);
	assertA(0 == archive_read_data_into_fd(a, tmpfilefd));
	close(tmpfilefd);
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	f = fopen(tmpfilename, "rb");
	assertEqualInt(sizeof(testdatacopy),
	    fread(testdatacopy, 1, sizeof(testdatacopy), f));
	fclose(f);
	assertEqualMem(testdata, testdatacopy, sizeof(testdata));
}
