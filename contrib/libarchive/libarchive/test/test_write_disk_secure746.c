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
 * Github Issue #746 describes a problem in which hardlink targets are
 * not adequately checked and can be used to modify entries outside of
 * the sandbox.
 */

/*
 * Verify that ARCHIVE_EXTRACT_SECURE_NODOTDOT disallows '..' in hardlink
 * targets.
 */
DEFINE_TEST(test_write_disk_secure746a)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
	skipping("archive_write_disk security checks not supported on Windows");
#else
	struct archive *a;
	struct archive_entry *ae;

	/* Start with a known umask. */
	assertUmask(UMASK);

	/* The target directory we're going to try to affect. */
	assertMakeDir("target", 0700);
	assertMakeFile("target/foo", 0700, "unmodified");

	/* The sandbox dir we're going to work within. */
	assertMakeDir("sandbox", 0700);
	assertChdir("sandbox");

	/* Create an archive_write_disk object. */
	assert((a = archive_write_disk_new()) != NULL);
	archive_write_disk_set_options(a, ARCHIVE_EXTRACT_SECURE_NODOTDOT);

	/* Attempt to hardlink to the target directory. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "bar");
	archive_entry_set_mode(ae, AE_IFREG | 0777);
	archive_entry_set_size(ae, 8);
	archive_entry_copy_hardlink(ae, "../target/foo");
	assertEqualInt(ARCHIVE_FAILED, archive_write_header(a, ae));
	assertEqualInt(ARCHIVE_FATAL, archive_write_data(a, "modified", 8));
	archive_entry_free(ae);

	/* Verify that target file contents are unchanged. */
	assertTextFileContents("unmodified", "../target/foo");

	assertEqualIntA(a, ARCHIVE_FATAL, archive_write_close(a));
	archive_write_free(a);
#endif
}

/*
 * Verify that ARCHIVE_EXTRACT_SECURE_NOSYMLINK disallows symlinks in hardlink
 * targets.
 */
DEFINE_TEST(test_write_disk_secure746b)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
	skipping("archive_write_disk security checks not supported on Windows");
#else
	struct archive *a;
	struct archive_entry *ae;

	/* Start with a known umask. */
	assertUmask(UMASK);

	/* The target directory we're going to try to affect. */
	assertMakeDir("target", 0700);
	assertMakeFile("target/foo", 0700, "unmodified");

	/* The sandbox dir we're going to work within. */
	assertMakeDir("sandbox", 0700);
	assertChdir("sandbox");

	/* Create an archive_write_disk object. */
	assert((a = archive_write_disk_new()) != NULL);
	archive_write_disk_set_options(a, ARCHIVE_EXTRACT_SECURE_SYMLINKS);

	/* Create a symlink to the target directory. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "symlink");
	archive_entry_set_mode(ae, AE_IFLNK | 0777);
	archive_entry_copy_symlink(ae, "../target");
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);

	/* Attempt to hardlink to the target directory via the symlink. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "bar");
	archive_entry_set_mode(ae, AE_IFREG | 0777);
	archive_entry_set_size(ae, 8);
	archive_entry_copy_hardlink(ae, "symlink/foo");
	assertEqualIntA(a, ARCHIVE_FAILED, archive_write_header(a, ae));
	assertEqualIntA(a, ARCHIVE_FATAL, archive_write_data(a, "modified", 8));
	archive_entry_free(ae);

	/* Verify that target file contents are unchanged. */
	assertTextFileContents("unmodified", "../target/foo");

	assertEqualIntA(a, ARCHIVE_FATAL, archive_write_close(a));
	archive_write_free(a);
#endif
}
