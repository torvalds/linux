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
 * Github Issue #745 describes a bug in the sandboxing code that
 * allows one to use a symlink to edit the permissions on a file or
 * directory outside of the sandbox.
 */

DEFINE_TEST(test_write_disk_secure745)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
	skipping("archive_write_disk security checks not supported on Windows");
#else
	struct archive *a;
	struct archive_entry *ae;

	/* Start with a known umask. */
	assertUmask(UMASK);

	/* Create an archive_write_disk object. */
	assert((a = archive_write_disk_new()) != NULL);
	archive_write_disk_set_options(a, ARCHIVE_EXTRACT_SECURE_SYMLINKS);

	/* The target dir:  The one we're going to try to change permission on */
	assertMakeDir("target", 0700);

	/* The sandbox dir we're going to run inside of. */
	assertMakeDir("sandbox", 0700);
	assertChdir("sandbox");

	/* Create a symlink pointing to the target directory */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "sym");
	archive_entry_set_mode(ae, AE_IFLNK | 0777);
	archive_entry_copy_symlink(ae, "../target");
	assert(0 == archive_write_header(a, ae));
	archive_entry_free(ae);

	/* Try to alter the target dir through the symlink; this should fail. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "sym");
	archive_entry_set_mode(ae, S_IFDIR | 0777);
	assert(0 == archive_write_header(a, ae));
	archive_entry_free(ae);

	/* Permission of target dir should not have changed. */
	assertFileMode("../target", 0700);

	assert(0 == archive_write_close(a));
	archive_write_free(a);
#endif
}
