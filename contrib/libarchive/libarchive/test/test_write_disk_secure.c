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

#define UMASK 022

/*
 * Exercise security checks that should prevent certain
 * writes.
 */

DEFINE_TEST(test_write_disk_secure)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
	skipping("archive_write_disk security checks not supported on Windows");
#else
	struct archive *a;
	struct archive_entry *ae;
	struct stat st;

	/* Start with a known umask. */
	assertUmask(UMASK);

	/* Create an archive_write_disk object. */
	assert((a = archive_write_disk_new()) != NULL);

	/* Write a regular dir to it. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "dir");
	archive_entry_set_mode(ae, S_IFDIR | 0777);
	assert(0 == archive_write_header(a, ae));
	archive_entry_free(ae);
	assert(0 == archive_write_finish_entry(a));

	/* Write a symlink to the dir above. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "link_to_dir");
	archive_entry_set_mode(ae, S_IFLNK | 0777);
	archive_entry_set_symlink(ae, "dir");
	archive_write_disk_set_options(a, 0);
	assert(0 == archive_write_header(a, ae));
	assert(0 == archive_write_finish_entry(a));

	/*
	 * Without security checks, we should be able to
	 * extract a file through the link.
	 */
	assert(archive_entry_clear(ae) != NULL);
	archive_entry_copy_pathname(ae, "link_to_dir/filea");
	archive_entry_set_mode(ae, S_IFREG | 0777);
	assert(0 == archive_write_header(a, ae));
	assert(0 == archive_write_finish_entry(a));

	/* But with security checks enabled, this should fail. */
	assert(archive_entry_clear(ae) != NULL);
	archive_entry_copy_pathname(ae, "link_to_dir/fileb");
	archive_entry_set_mode(ae, S_IFREG | 0777);
	archive_write_disk_set_options(a, ARCHIVE_EXTRACT_SECURE_SYMLINKS);
	failure("Extracting a file through a symlink should fail here.");
	assertEqualInt(ARCHIVE_FAILED, archive_write_header(a, ae));
	archive_entry_free(ae);
	assert(0 == archive_write_finish_entry(a));

	/* Write an absolute symlink to /tmp. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "/tmp/libarchive_test-test_write_disk_secure-absolute_symlink");
	archive_entry_set_mode(ae, S_IFLNK | 0777);
	archive_entry_set_symlink(ae, "/tmp");
	archive_write_disk_set_options(a, 0);
	assert(0 == archive_write_header(a, ae));
	assert(0 == archive_write_finish_entry(a));

	/* With security checks enabled, this should fail. */
	assert(archive_entry_clear(ae) != NULL);
	archive_entry_copy_pathname(ae, "/tmp/libarchive_test-test_write_disk_secure-absolute_symlink/libarchive_test-test_write_disk_secure-absolute_symlink_path.tmp");
	archive_entry_set_mode(ae, S_IFREG | 0777);
	archive_write_disk_set_options(a, ARCHIVE_EXTRACT_SECURE_SYMLINKS);
	failure("Extracting a file through an absolute symlink should fail here.");
	assertEqualInt(ARCHIVE_FAILED, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertFileNotExists("/tmp/libarchive_test-test_write_disk_secure-absolute_symlink/libarchive_test-test_write_disk_secure-absolute_symlink_path.tmp");
	assert(0 == unlink("/tmp/libarchive_test-test_write_disk_secure-absolute_symlink"));
	unlink("/tmp/libarchive_test-test_write_disk_secure-absolute_symlink_path.tmp");

	/* Create another link. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "link_to_dir2");
	archive_entry_set_mode(ae, S_IFLNK | 0777);
	archive_entry_set_symlink(ae, "dir");
	archive_write_disk_set_options(a, 0);
	assert(0 == archive_write_header(a, ae));
	assert(0 == archive_write_finish_entry(a));

	/*
	 * With symlink check and unlink option, it should remove
	 * the link and create the dir.
	 */
	assert(archive_entry_clear(ae) != NULL);
	archive_entry_copy_pathname(ae, "link_to_dir2/filec");
	archive_entry_set_mode(ae, S_IFREG | 0777);
	archive_write_disk_set_options(a, ARCHIVE_EXTRACT_SECURE_SYMLINKS | ARCHIVE_EXTRACT_UNLINK);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assert(0 == archive_write_finish_entry(a));

	/* Create a nested symlink. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "dir/nested_link_to_dir");
	archive_entry_set_mode(ae, S_IFLNK | 0777);
	archive_entry_set_symlink(ae, "../dir");
	archive_write_disk_set_options(a, 0);
	assert(0 == archive_write_header(a, ae));
	assert(0 == archive_write_finish_entry(a));

	/* But with security checks enabled, this should fail. */
	assert(archive_entry_clear(ae) != NULL);
	archive_entry_copy_pathname(ae, "dir/nested_link_to_dir/filed");
	archive_entry_set_mode(ae, S_IFREG | 0777);
	archive_write_disk_set_options(a, ARCHIVE_EXTRACT_SECURE_SYMLINKS);
	failure("Extracting a file through a symlink should fail here.");
	assertEqualInt(ARCHIVE_FAILED, archive_write_header(a, ae));
	archive_entry_free(ae);
	assert(0 == archive_write_finish_entry(a));

	/*
	 * Without security checks, extracting a dir over a link to a
	 * dir should follow the link.
	 */
	/* Create a symlink to a dir. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "link_to_dir3");
	archive_entry_set_mode(ae, S_IFLNK | 0777);
	archive_entry_set_symlink(ae, "dir");
	archive_write_disk_set_options(a, 0);
	assert(0 == archive_write_header(a, ae));
	assert(0 == archive_write_finish_entry(a));
	/* Extract a dir whose name matches the symlink. */
	assert(archive_entry_clear(ae) != NULL);
	archive_entry_copy_pathname(ae, "link_to_dir3");
	archive_entry_set_mode(ae, S_IFDIR | 0777);
	assert(0 == archive_write_header(a, ae));
	assert(0 == archive_write_finish_entry(a));
	/* Verify link was followed. */
	assertEqualInt(0, lstat("link_to_dir3", &st));
	assert(S_ISLNK(st.st_mode));
	archive_entry_free(ae);

	/*
	 * As above, but a broken link, so the link should get replaced.
	 */
	/* Create a symlink to a dir. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "link_to_dir4");
	archive_entry_set_mode(ae, S_IFLNK | 0777);
	archive_entry_set_symlink(ae, "nonexistent_dir");
	archive_write_disk_set_options(a, 0);
	assert(0 == archive_write_header(a, ae));
	assert(0 == archive_write_finish_entry(a));
	/* Extract a dir whose name matches the symlink. */
	assert(archive_entry_clear(ae) != NULL);
	archive_entry_copy_pathname(ae, "link_to_dir4");
	archive_entry_set_mode(ae, S_IFDIR | 0777);
	assert(0 == archive_write_header(a, ae));
	assert(0 == archive_write_finish_entry(a));
	/* Verify link was replaced. */
	assertEqualInt(0, lstat("link_to_dir4", &st));
	assert(S_ISDIR(st.st_mode));
	archive_entry_free(ae);

	/*
	 * As above, but a link to a non-dir, so the link should get replaced.
	 */
	/* Create a regular file and a symlink to it */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "non_dir");
	archive_entry_set_mode(ae, S_IFREG | 0777);
	archive_write_disk_set_options(a, 0);
	assert(0 == archive_write_header(a, ae));
	assert(0 == archive_write_finish_entry(a));
	/* Create symlink to the file. */
	archive_entry_copy_pathname(ae, "link_to_dir5");
	archive_entry_set_mode(ae, S_IFLNK | 0777);
	archive_entry_set_symlink(ae, "non_dir");
	archive_write_disk_set_options(a, 0);
	assert(0 == archive_write_header(a, ae));
	assert(0 == archive_write_finish_entry(a));
	/* Extract a dir whose name matches the symlink. */
	assert(archive_entry_clear(ae) != NULL);
	archive_entry_copy_pathname(ae, "link_to_dir5");
	archive_entry_set_mode(ae, S_IFDIR | 0777);
	assert(0 == archive_write_header(a, ae));
	assert(0 == archive_write_finish_entry(a));
	/* Verify link was replaced. */
	assertEqualInt(0, lstat("link_to_dir5", &st));
	assert(S_ISDIR(st.st_mode));
	archive_entry_free(ae);

	/*
	 * Without security checks, we should be able to
	 * extract an absolute path.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "/tmp/libarchive_test-test_write_disk_secure-absolute_path.tmp");
	archive_entry_set_mode(ae, S_IFREG | 0777);
	assert(0 == archive_write_header(a, ae));
	assert(0 == archive_write_finish_entry(a));
	assertFileExists("/tmp/libarchive_test-test_write_disk_secure-absolute_path.tmp");
	assert(0 == unlink("/tmp/libarchive_test-test_write_disk_secure-absolute_path.tmp"));

	/* But with security checks enabled, this should fail. */
	assert(archive_entry_clear(ae) != NULL);
	archive_entry_copy_pathname(ae, "/tmp/libarchive_test-test_write_disk_secure-absolute_path.tmp");
	archive_entry_set_mode(ae, S_IFREG | 0777);
	archive_write_disk_set_options(a, ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS);
	failure("Extracting an absolute path should fail here.");
	assertEqualInt(ARCHIVE_FAILED, archive_write_header(a, ae));
	archive_entry_free(ae);
	assert(0 == archive_write_finish_entry(a));
	assertFileNotExists("/tmp/libarchive_test-test_write_disk_secure-absolute_path.tmp");

	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* Test the entries on disk. */
	assert(0 == lstat("dir", &st));
	failure("dir: st.st_mode=%o", st.st_mode);
	assert((st.st_mode & 0777) == 0755);

	assert(0 == lstat("link_to_dir", &st));
	failure("link_to_dir: st.st_mode=%o", st.st_mode);
	assert(S_ISLNK(st.st_mode));
#if HAVE_LCHMOD
	/* Systems that lack lchmod() can't set symlink perms, so skip this. */
	failure("link_to_dir: st.st_mode=%o", st.st_mode);
	assert((st.st_mode & 07777) == 0755);
#endif

	assert(0 == lstat("dir/filea", &st));
	failure("dir/filea: st.st_mode=%o", st.st_mode);
	assert((st.st_mode & 07777) == 0755);

	failure("dir/fileb: This file should not have been created");
	assert(0 != lstat("dir/fileb", &st));

	assert(0 == lstat("link_to_dir2", &st));
	failure("link_to_dir2 should have been re-created as a true dir");
	assert(S_ISDIR(st.st_mode));
	failure("link_to_dir2: Implicit dir creation should obey umask, but st.st_mode=%o", st.st_mode);
	assert((st.st_mode & 0777) == 0755);

	assert(0 == lstat("link_to_dir2/filec", &st));
	assert(S_ISREG(st.st_mode));
	failure("link_to_dir2/filec: st.st_mode=%o", st.st_mode);
	assert((st.st_mode & 07777) == 0755);

	failure("dir/filed: This file should not have been created");
	assert(0 != lstat("dir/filed", &st));
#endif
}
