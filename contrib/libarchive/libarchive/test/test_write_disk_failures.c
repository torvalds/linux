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

DEFINE_TEST(test_write_disk_failures)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
	skipping("archive_write_disk interface");
#else
	struct archive_entry *ae;
	struct archive *a;
	int fd;

	/* Force the umask to something predictable. */
	assertUmask(022);

	/* A directory that we can't write to. */
	assertMakeDir("dir", 0555);

	/* Can we? */
	fd = open("dir/testfile", O_WRONLY | O_CREAT | O_BINARY, 0777);
	if (fd >= 0) {
	  /* Apparently, we can, so the test below won't work. */
	  close(fd);
	  skipping("Can't test writing to non-writable directory");
	  return;
	}

	/* Try to extract a regular file into the directory above. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "dir/file");
	archive_entry_set_mode(ae, S_IFREG | 0755);
	archive_entry_set_size(ae, 8);
	assert((a = archive_write_disk_new()) != NULL);
        archive_write_disk_set_options(a, ARCHIVE_EXTRACT_TIME);
	archive_entry_set_mtime(ae, 123456789, 0);
	assertEqualIntA(a, ARCHIVE_FAILED, archive_write_header(a, ae));
	assertEqualIntA(a, 0, archive_write_finish_entry(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));
	archive_entry_free(ae);
#endif
}
