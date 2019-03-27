/*-
 * Copyright (c) 2003-2007,2016 Tim Kientzle
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

#define UMASK 022

/*
 * Github Issue #744 describes a bug in the sandboxing code that
 * causes very long pathnames to not get checked for symlinks.
 */

DEFINE_TEST(test_write_disk_secure744)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
	skipping("archive_write_disk security checks not supported on Windows");
#else
	struct archive *a;
	struct archive_entry *ae;
	size_t buff_size = 8192;
	char *buff = malloc(buff_size);
	char *p = buff;
	int n = 0;
	int t;

	assert(buff != NULL);

	/* Start with a known umask. */
	assertUmask(UMASK);

	/* Create an archive_write_disk object. */
	assert((a = archive_write_disk_new()) != NULL);
	archive_write_disk_set_options(a, ARCHIVE_EXTRACT_SECURE_SYMLINKS);

	while (p + 500 < buff + buff_size) {
		memset(p, 'x', 100);
		p += 100;
		p[0] = '\0';

		buff[0] = ((n / 1000) % 10) + '0';
		buff[1] = ((n / 100) % 10)+ '0';
		buff[2] = ((n / 10) % 10)+ '0';
		buff[3] = ((n / 1) % 10)+ '0';
		buff[4] = '_';
		++n;

		/* Create a symlink pointing to the testworkdir */
		assert((ae = archive_entry_new()) != NULL);
		archive_entry_copy_pathname(ae, buff);
		archive_entry_set_mode(ae, S_IFREG | 0777);
		archive_entry_copy_symlink(ae, testworkdir);
		assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
		archive_entry_free(ae);

		*p++ = '/';
		sprintf(p, "target%d", n);

		/* Try to create a file through the symlink, should fail. */
		assert((ae = archive_entry_new()) != NULL);
		archive_entry_copy_pathname(ae, buff);
		archive_entry_set_mode(ae, S_IFDIR | 0777);

		t = archive_write_header(a, ae);
		archive_entry_free(ae);
		failure("Attempt to create target%d via %d-character symlink should have failed", n, (int)strlen(buff));
		if(!assertEqualInt(ARCHIVE_FAILED, t)) {
			break;
		}
	}
	archive_free(a);
	free(buff);
#endif
}
