/*-
 * Copyright (c) 2010-2012 Michihiro NAKAJIMA
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

#include <limits.h>
#if defined(_WIN32) && !defined(__CYGWIN__)
# if !defined(__BORLANDC__)
#  define getcwd _getcwd
# endif
#endif

/*
 * Test if the current filesystem is mounted with noatime option.
 */
static int
atimeIsUpdated(void)
{
	const char *fn = "fs_noatime";
	struct stat st;

	if (!assertMakeFile(fn, 0666, "a"))
		return (0);
	if (!assertUtimes(fn, 1, 0, 1, 0))
		return (0);
	/* Test the file contents in order to update its atime. */
	if (!assertTextFileContents("a", fn))
		return (0);
	if (stat(fn, &st) != 0)
		return (0);
	/* Is atime updated? */
	if (st.st_atime > 1)
		return (1);
	return (0);
}

static void
test_basic(void)
{
	struct archive *a;
	struct archive_entry *ae;
	const void *p;
	char *initial_cwd, *cwd;
	size_t size;
	int64_t offset;
	int file_count;
#if defined(_WIN32) && !defined(__CYGWIN__)
	wchar_t *wcwd, *wp, *fullpath;
#endif

	assertMakeDir("dir1", 0755);
	assertMakeFile("dir1/file1", 0644, "0123456789");
	assertMakeFile("dir1/file2", 0644, "hello world");
	assertMakeDir("dir1/sub1", 0755);
	assertMakeFile("dir1/sub1/file1", 0644, "0123456789");
	assertMakeDir("dir1/sub2", 0755);
	assertMakeFile("dir1/sub2/file1", 0644, "0123456789");
	assertMakeFile("dir1/sub2/file2", 0644, "0123456789");
	assertMakeDir("dir1/sub2/sub1", 0755);
	assertMakeDir("dir1/sub2/sub2", 0755);
	assertMakeDir("dir1/sub2/sub3", 0755);
	assertMakeFile("dir1/sub2/sub3/file", 0644, "xyz");
	file_count = 12;

	assert((ae = archive_entry_new()) != NULL);
	assert((a = archive_read_disk_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_open(a, "dir1"));

	while (file_count--) {
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header2(a, ae));
		if (strcmp(archive_entry_pathname(ae), "dir1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
			assertEqualInt(1, archive_read_disk_can_descend(a));
		} else if (strcmp(archive_entry_pathname(ae),
		    "dir1/file1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 10);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 10);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "0123456789", 10);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 10);
			assertEqualInt(0, archive_read_disk_can_descend(a));
		} else if (strcmp(archive_entry_pathname(ae),
		    "dir1/file2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 11);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 11);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "hello world", 11);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 11);
			assertEqualInt(0, archive_read_disk_can_descend(a));
		} else if (strcmp(archive_entry_pathname(ae),
		    "dir1/sub1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
			assertEqualInt(1, archive_read_disk_can_descend(a));
		} else if (strcmp(archive_entry_pathname(ae),
		    "dir1/sub1/file1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 10);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 10);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "0123456789", 10);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 10);
			assertEqualInt(0, archive_read_disk_can_descend(a));
		} else if (strcmp(archive_entry_pathname(ae),
		    "dir1/sub2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
			assertEqualInt(1, archive_read_disk_can_descend(a));
		} else if (strcmp(archive_entry_pathname(ae),
		    "dir1/sub2/file1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 10);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 10);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "0123456789", 10);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 10);
			assertEqualInt(0, archive_read_disk_can_descend(a));
		} else if (strcmp(archive_entry_pathname(ae),
		    "dir1/sub2/file2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 10);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 10);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "0123456789", 10);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 10);
			assertEqualInt(0, archive_read_disk_can_descend(a));
		} else if (strcmp(archive_entry_pathname(ae),
		    "dir1/sub2/sub1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
			assertEqualInt(1, archive_read_disk_can_descend(a));
		} else if (strcmp(archive_entry_pathname(ae),
		    "dir1/sub2/sub2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
			assertEqualInt(1, archive_read_disk_can_descend(a));
		} else if (strcmp(archive_entry_pathname(ae),
		    "dir1/sub2/sub3") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
			assertEqualInt(1, archive_read_disk_can_descend(a));
		} else if (strcmp(archive_entry_pathname(ae),
		    "dir1/sub2/sub3/file") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 3);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 3);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "xyz", 3);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 3);
			assertEqualInt(0, archive_read_disk_can_descend(a));
		}
		if (archive_entry_filetype(ae) == AE_IFDIR) {
			/* Descend into the current object */
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_disk_descend(a));
		}
	}
	/* There is no entry. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header2(a, ae));

	/* Close the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));

	/*
	 * Test that call archive_read_disk_open_w, wchar_t version.
	 */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_open_w(a, L"dir1"));

	file_count = 12;
	while (file_count--) {
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header2(a, ae));
		if (wcscmp(archive_entry_pathname_w(ae), L"dir1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
			assertEqualInt(1, archive_read_disk_can_descend(a));
		} else if (wcscmp(archive_entry_pathname_w(ae),
		    L"dir1/file1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 10);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 10);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "0123456789", 10);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 10);
			assertEqualInt(0, archive_read_disk_can_descend(a));
		} else if (wcscmp(archive_entry_pathname_w(ae),
		    L"dir1/file2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 11);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 11);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "hello world", 11);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 11);
			assertEqualInt(0, archive_read_disk_can_descend(a));
		} else if (wcscmp(archive_entry_pathname_w(ae),
		    L"dir1/sub1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
			assertEqualInt(1, archive_read_disk_can_descend(a));
		} else if (wcscmp(archive_entry_pathname_w(ae),
		    L"dir1/sub1/file1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 10);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 10);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "0123456789", 10);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 10);
			assertEqualInt(0, archive_read_disk_can_descend(a));
		} else if (wcscmp(archive_entry_pathname_w(ae),
		    L"dir1/sub2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
			assertEqualInt(1, archive_read_disk_can_descend(a));
		} else if (wcscmp(archive_entry_pathname_w(ae),
		    L"dir1/sub2/file1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 10);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 10);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "0123456789", 10);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 10);
			assertEqualInt(0, archive_read_disk_can_descend(a));
		} else if (wcscmp(archive_entry_pathname_w(ae),
		    L"dir1/sub2/file2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 10);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 10);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "0123456789", 10);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 10);
			assertEqualInt(0, archive_read_disk_can_descend(a));
		} else if (wcscmp(archive_entry_pathname_w(ae),
		    L"dir1/sub2/sub1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
			assertEqualInt(1, archive_read_disk_can_descend(a));
		} else if (wcscmp(archive_entry_pathname_w(ae),
		    L"dir1/sub2/sub2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
			assertEqualInt(1, archive_read_disk_can_descend(a));
		} else if (wcscmp(archive_entry_pathname_w(ae),
		    L"dir1/sub2/sub3") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
			assertEqualInt(1, archive_read_disk_can_descend(a));
		} else if (wcscmp(archive_entry_pathname_w(ae),
		    L"dir1/sub2/sub3/file") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 3);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 3);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "xyz", 3);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 3);
			assertEqualInt(0, archive_read_disk_can_descend(a));
		}
		if (archive_entry_filetype(ae) == AE_IFDIR) {
			/* Descend into the current object */
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_disk_descend(a));
		}
	}
	/* There is no entry. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header2(a, ae));

	/* Close the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));

	/*
	 * Test that call archive_read_disk_open with a regular file.
	 */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_open(a, "dir1/file1"));

	/* dir1/file1 */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header2(a, ae));
	assertEqualInt(0, archive_read_disk_can_descend(a));
	assertEqualString(archive_entry_pathname(ae), "dir1/file1");
	assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
	assertEqualInt(archive_entry_size(ae), 10);
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_data_block(a, &p, &size, &offset));
	assertEqualInt((int)size, 10);
	assertEqualInt((int)offset, 0);
	assertEqualMem(p, "0123456789", 10);

	/* There is no entry. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header2(a, ae));

	/* Close the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));


#if defined(_WIN32) && !defined(__CYGWIN__)
	/*
	 * Test for wildcard '*' or '?'
	 */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_open(a, "dir1/*1"));

	/* dir1/file1 */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header2(a, ae));
	assertEqualInt(0, archive_read_disk_can_descend(a));
	assertEqualString(archive_entry_pathname(ae), "dir1/file1");
	assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
	assertEqualInt(archive_entry_size(ae), 10);
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_data_block(a, &p, &size, &offset));
	assertEqualInt((int)size, 10);
	assertEqualInt((int)offset, 0);
	assertEqualMem(p, "0123456789", 10);

	/* dir1/sub1 */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header2(a, ae));
	assertEqualInt(1, archive_read_disk_can_descend(a));
	assertEqualString(archive_entry_pathname(ae), "dir1/sub1");
	assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);

	/* Descend into the current object */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_descend(a));

	/* dir1/sub1/file1 */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header2(a, ae));
	assertEqualInt(0, archive_read_disk_can_descend(a));
	assertEqualString(archive_entry_pathname(ae), "dir1/sub1/file1");
	assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
	assertEqualInt(archive_entry_size(ae), 10);
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_data_block(a, &p, &size, &offset));
	assertEqualInt((int)size, 10);
	assertEqualInt((int)offset, 0);
	assertEqualMem(p, "0123456789", 10);

	/* There is no entry. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header2(a, ae));

	/* Close the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));

	/*
	 * Test for a full-path beginning with "//?/"
	 */
	wcwd = _wgetcwd(NULL, 0);
	fullpath = malloc(sizeof(wchar_t) * (wcslen(wcwd) + 32));
	wcscpy(fullpath, L"//?/");
	wcscat(fullpath, wcwd);
	wcscat(fullpath, L"/dir1/file1");
	free(wcwd);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_open_w(a, fullpath));
	while ((wcwd = wcschr(fullpath, L'\\')) != NULL)
		*wcwd = L'/';

	/* dir1/file1 */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header2(a, ae));
	assertEqualInt(0, archive_read_disk_can_descend(a));
	assertEqualWString(archive_entry_pathname_w(ae), fullpath);
	assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
	assertEqualInt(archive_entry_size(ae), 10);
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_data_block(a, &p, &size, &offset));
	assertEqualInt((int)size, 10);
	assertEqualInt((int)offset, 0);
	assertEqualMem(p, "0123456789", 10);

	/* There is no entry. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header2(a, ae));

	/* Close the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	free(fullpath);

	/*
	 * Test for wild card '*' or '?' with "//?/" prefix.
	 */
	wcwd = _wgetcwd(NULL, 0);
	fullpath = malloc(sizeof(wchar_t) * (wcslen(wcwd) + 32));
	wcscpy(fullpath, L"//?/");
	wcscat(fullpath, wcwd);
	wcscat(fullpath, L"/dir1/*1");
	free(wcwd);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_open_w(a, fullpath));
	while ((wcwd = wcschr(fullpath, L'\\')) != NULL)
		*wcwd = L'/';

	/* dir1/file1 */
	wp = wcsrchr(fullpath, L'/');
	wcscpy(wp+1, L"file1");
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header2(a, ae));
	assertEqualInt(0, archive_read_disk_can_descend(a));
	assertEqualWString(archive_entry_pathname_w(ae), fullpath);
	assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
	assertEqualInt(archive_entry_size(ae), 10);
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_data_block(a, &p, &size, &offset));
	assertEqualInt((int)size, 10);
	assertEqualInt((int)offset, 0);
	assertEqualMem(p, "0123456789", 10);

	/* dir1/sub1 */
	wcscpy(wp+1, L"sub1");
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header2(a, ae));
	assertEqualInt(1, archive_read_disk_can_descend(a));
	assertEqualWString(archive_entry_pathname_w(ae), fullpath);
	assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);

	/* Descend into the current object */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_descend(a));

	/* dir1/sub1/file1 */
	wcscpy(wp+1, L"sub1/file1");
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header2(a, ae));
	assertEqualInt(0, archive_read_disk_can_descend(a));
	assertEqualWString(archive_entry_pathname_w(ae), fullpath);
	assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
	assertEqualInt(archive_entry_size(ae), 10);
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_data_block(a, &p, &size, &offset));
	assertEqualInt((int)size, 10);
	assertEqualInt((int)offset, 0);
	assertEqualMem(p, "0123456789", 10);

	/* There is no entry. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header2(a, ae));

	/* Close the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	free(fullpath);

#endif

	/*
	 * We should be on the initial directory where we performed
	 * archive_read_disk_new() after we perform archive_read_free()
	 * even if we broke off the directory traversals.
	 */

	/* Save current working directory. */
#ifdef PATH_MAX
	initial_cwd = getcwd(NULL, PATH_MAX);/* Solaris getcwd needs the size. */
#else
	initial_cwd = getcwd(NULL, 0);
#endif

	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_open(a, "dir1"));

	/* Step in a deep directory. */
	file_count = 12;
	while (file_count--) {
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_read_next_header2(a, ae));
		if (strcmp(archive_entry_pathname(ae),
		    "dir1/sub1/file1") == 0)
			/*
			 * We are on an another directory at this time.
			 */
			break;
		if (archive_entry_filetype(ae) == AE_IFDIR) {
			/* Descend into the current object */
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_disk_descend(a));
		}
	}
	/* Destroy the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/* We should be on the initial working directory. */
	failure(
	    "Current working directory does not return to the initial"
	    "directory");
#ifdef PATH_MAX
	cwd = getcwd(NULL, PATH_MAX);/* Solaris getcwd needs the size. */
#else
	cwd = getcwd(NULL, 0);
#endif
	assertEqualString(initial_cwd, cwd);
	free(initial_cwd);
	free(cwd);

	archive_entry_free(ae);
}

static void
test_symlink_hybrid(void)
{
	struct archive *a;
	struct archive_entry *ae;
	const void *p;
	size_t size;
	int64_t offset;
	int file_count;

	if (!canSymlink()) {
		skipping("Can't test symlinks on this filesystem");
		return;
	}

	/*
	 * Create a sample archive.
	 */
	assertMakeDir("h", 0755);
	assertChdir("h");
	assertMakeDir("d1", 0755);
	assertMakeSymlink("ld1", "d1");
	assertMakeFile("d1/file1", 0644, "d1/file1");
	assertMakeFile("d1/file2", 0644, "d1/file2");
	assertMakeSymlink("d1/link1", "file1");
	assertMakeSymlink("d1/linkX", "fileX");
	assertMakeSymlink("link2", "d1/file2");
	assertMakeSymlink("linkY", "d1/fileY");
	assertChdir("..");

	assert((ae = archive_entry_new()) != NULL);
	assert((a = archive_read_disk_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_set_symlink_hybrid(a));

	/*
	 * Specified file is a symbolic link file.
	 */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_open(a, "h/ld1"));
	file_count = 5;

	while (file_count--) {
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header2(a, ae));
		if (strcmp(archive_entry_pathname(ae), "h/ld1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
		} else if (strcmp(archive_entry_pathname(ae),
		    "h/ld1/file1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 8);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 8);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "d1/file1", 8);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 8);
		} else if (strcmp(archive_entry_pathname(ae),
		    "h/ld1/file2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 8);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 8);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "d1/file2", 8);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 8);
		} else if (strcmp(archive_entry_pathname(ae),
		    "h/ld1/link1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFLNK);
		} else if (strcmp(archive_entry_pathname(ae),
		    "h/ld1/linkX") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFLNK);
		}
		if (archive_entry_filetype(ae) == AE_IFDIR) {
			/* Descend into the current object */
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_disk_descend(a));
		}
	}
	/* There is no entry. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header2(a, ae));
	/* Close the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));

	/*
	 * Specified file is a directory and it has symbolic files.
	 */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_open(a, "h"));
	file_count = 9;

	while (file_count--) {
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header2(a, ae));
		if (strcmp(archive_entry_pathname(ae), "h") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
		} else if (strcmp(archive_entry_pathname(ae), "h/d1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
		} else if (strcmp(archive_entry_pathname(ae),
		    "h/d1/file1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 8);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 8);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "d1/file1", 8);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 8);
		} else if (strcmp(archive_entry_pathname(ae),
		    "h/d1/file2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 8);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 8);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "d1/file2", 8);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 8);
		} else if (strcmp(archive_entry_pathname(ae), "h/ld1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFLNK);
		} else if (strcmp(archive_entry_pathname(ae),
		    "h/d1/link1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFLNK);
		} else if (strcmp(archive_entry_pathname(ae),
		    "h/d1/linkX") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFLNK);
		} else if (strcmp(archive_entry_pathname(ae),
		    "h/link2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFLNK);
		} else if (strcmp(archive_entry_pathname(ae),
		    "h/linkY") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFLNK);
		}
		if (archive_entry_filetype(ae) == AE_IFDIR) {
			/* Descend into the current object */
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_disk_descend(a));
		}
	}
	/* There is no entry. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header2(a, ae));
	/* Close the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	/* Destroy the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	archive_entry_free(ae);
}

static void
test_symlink_logical(void)
{
	struct archive *a;
	struct archive_entry *ae;
	const void *p;
	size_t size;
	int64_t offset;
	int file_count;

	if (!canSymlink()) {
		skipping("Can't test symlinks on this filesystem");
		return;
	}

	/*
	 * Create a sample archive.
	 */
	assertMakeDir("l", 0755);
	assertChdir("l");
	assertMakeDir("d1", 0755);
	assertMakeSymlink("ld1", "d1");
	assertMakeFile("d1/file1", 0644, "d1/file1");
	assertMakeFile("d1/file2", 0644, "d1/file2");
	assertMakeSymlink("d1/link1", "file1");
	assertMakeSymlink("d1/linkX", "fileX");
	assertMakeSymlink("link2", "d1/file2");
	assertMakeSymlink("linkY", "d1/fileY");
	assertChdir("..");

	/* Note: this test uses archive_read_next_header()
	   instead of archive_read_next_header2() */
	assert((a = archive_read_disk_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_set_symlink_logical(a));

	/*
	 * Specified file is a symbolic link file.
	 */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_open(a, "l/ld1"));
	file_count = 5;

	while (file_count--) {
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
		if (strcmp(archive_entry_pathname(ae), "l/ld1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
		} else if (strcmp(archive_entry_pathname(ae),
		    "l/ld1/file1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 8);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 8);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "d1/file1", 8);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 8);
		} else if (strcmp(archive_entry_pathname(ae),
		    "l/ld1/file2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 8);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 8);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "d1/file2", 8);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 8);
		} else if (strcmp(archive_entry_pathname(ae),
		    "l/ld1/link1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 8);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 8);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "d1/file1", 8);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 8);
		} else if (strcmp(archive_entry_pathname(ae),
		    "l/ld1/linkX") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFLNK);
		}
		if (archive_entry_filetype(ae) == AE_IFDIR) {
			/* Descend into the current object */
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_disk_descend(a));
		}
	}
	/* There is no entry. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	/* Close the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));

	/*
	 * Specified file is a directory and it has symbolic files.
	 */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_open(a, "l"));
	file_count = 13;

	while (file_count--) {
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
		if (strcmp(archive_entry_pathname(ae), "l") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
		} else if (strcmp(archive_entry_pathname(ae), "l/d1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
		} else if (strcmp(archive_entry_pathname(ae),
		    "l/d1/file1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 8);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 8);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "d1/file1", 8);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 8);
		} else if (strcmp(archive_entry_pathname(ae),
		    "l/d1/file2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 8);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 8);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "d1/file2", 8);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 8);
		} else if (strcmp(archive_entry_pathname(ae),
		    "l/d1/link1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 8);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 8);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "d1/file1", 8);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 8);
		} else if (strcmp(archive_entry_pathname(ae),
		    "l/d1/linkX") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFLNK);
		} else if (strcmp(archive_entry_pathname(ae), "l/ld1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
		} else if (strcmp(archive_entry_pathname(ae),
		    "l/ld1/file1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 8);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 8);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "d1/file1", 8);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 8);
		} else if (strcmp(archive_entry_pathname(ae),
		    "l/ld1/file2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 8);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 8);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "d1/file2", 8);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 8);
		} else if (strcmp(archive_entry_pathname(ae),
		    "l/ld1/link1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 8);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 8);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "d1/file1", 8);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 8);
		} else if (strcmp(archive_entry_pathname(ae),
		    "l/ld1/linkX") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFLNK);
		} else if (strcmp(archive_entry_pathname(ae),
		    "l/link2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 8);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 8);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "d1/file2", 8);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 8);
		} else if (strcmp(archive_entry_pathname(ae),
		    "l/linkY") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFLNK);
		}
		if (archive_entry_filetype(ae) == AE_IFDIR) {
			/* Descend into the current object */
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_disk_descend(a));
		}
	}
	/* There is no entry. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	/* Close the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	/* Destroy the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

static void
test_symlink_logical_loop(void)
{
	struct archive *a;
	struct archive_entry *ae;
	const void *p;
	size_t size;
	int64_t offset;
	int file_count;

	if (!canSymlink()) {
		skipping("Can't test symlinks on this filesystem");
		return;
	}

	/*
	 * Create a sample archive.
	 */
	assertMakeDir("l2", 0755);
	assertChdir("l2");
	assertMakeDir("d1", 0755);
	assertMakeDir("d1/d2", 0755);
	assertMakeDir("d1/d2/d3", 0755);
	assertMakeDir("d2", 0755);
	assertMakeFile("d2/file1", 0644, "d2/file1");
	assertMakeSymlink("d1/d2/ld1", "../../d1");
	assertMakeSymlink("d1/d2/ld2", "../../d2");
	assertChdir("..");

	assert((ae = archive_entry_new()) != NULL);
	assert((a = archive_read_disk_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_set_symlink_logical(a));

	/*
	 * Specified file is a symbolic link file.
	 */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_open(a, "l2/d1"));
	file_count = 6;

	while (file_count--) {
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header2(a, ae));
		if (strcmp(archive_entry_pathname(ae), "l2/d1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
		} else if (strcmp(archive_entry_pathname(ae), "l2/d1/d2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
		} else if (strcmp(archive_entry_pathname(ae), "l2/d1/d2/d3") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
		} else if (strcmp(archive_entry_pathname(ae), "l2/d1/d2/ld1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFLNK);
		} else if (strcmp(archive_entry_pathname(ae), "l2/d1/d2/ld2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
		} else if (strcmp(archive_entry_pathname(ae),
		    "l2/d1/d2/ld2/file1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 8);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 8);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "d2/file1", 8);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 8);
		}
		if (archive_entry_filetype(ae) == AE_IFDIR) {
			/* Descend into the current object */
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_disk_descend(a));
		}
	}
	/* There is no entry. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header2(a, ae));
	/* Destroy the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	archive_entry_free(ae);
}

static void
test_restore_atime(void)
{
	struct archive *a;
	struct archive_entry *ae;
	const void *p;
	size_t size;
	int64_t offset;
	int file_count;

	if (!atimeIsUpdated()) {
		skipping("Can't test restoring atime on this filesystem");
		return;
	}

	assertMakeDir("at", 0755);
	assertMakeFile("at/f1", 0644, "0123456789");
	assertMakeFile("at/f2", 0644, "hello world");
	assertMakeFile("at/fe", 0644, NULL);
	assertUtimes("at/f1", 886600, 0, 886600, 0);
	assertUtimes("at/f2", 886611, 0, 886611, 0);
	assertUtimes("at/fe", 886611, 0, 886611, 0);
	assertUtimes("at", 886622, 0, 886622, 0);
	file_count = 4;

	assert((ae = archive_entry_new()) != NULL);
	assert((a = archive_read_disk_new()) != NULL);

	/*
	 * Test1: Traversals without archive_read_disk_set_atime_restored().
	 */
	failure("Directory traversals should work as well");
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_open(a, "at"));
	while (file_count--) {
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header2(a, ae));
		if (strcmp(archive_entry_pathname(ae), "at") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
		} else if (strcmp(archive_entry_pathname(ae), "at/f1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 10);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 10);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "0123456789", 10);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 10);
		} else if (strcmp(archive_entry_pathname(ae), "at/f2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 11);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 11);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "hello world", 11);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 11);
		} else if (strcmp(archive_entry_pathname(ae), "at/fe") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 0);
		}
		if (archive_entry_filetype(ae) == AE_IFDIR) {
			/* Descend into the current object */
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_disk_descend(a));
		}
	}
	/* There is no entry. */
	failure("There must be no entry");
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header2(a, ae));

	/* On FreeBSD (and likely other systems), atime on
	   dirs does not change when it is read. */
	/* failure("Atime should be restored"); */
	/* assertFileAtimeRecent("at"); */
	failure("Atime should be restored");
	assertFileAtimeRecent("at/f1");
	failure("Atime should be restored");
	assertFileAtimeRecent("at/f2");
	failure("The atime of a empty file should not be changed");
	assertFileAtime("at/fe", 886611, 0);

	/* Close the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));

	/*
	 * Test2: Traversals with archive_read_disk_set_atime_restored().
	 */
	assertUtimes("at/f1", 886600, 0, 886600, 0);
	assertUtimes("at/f2", 886611, 0, 886611, 0);
	assertUtimes("at/fe", 886611, 0, 886611, 0);
	assertUtimes("at", 886622, 0, 886622, 0);
	file_count = 4;
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_set_atime_restored(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_open(a, "at"));

	failure("Directory traversals should work as well");
	while (file_count--) {
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header2(a, ae));
		if (strcmp(archive_entry_pathname(ae), "at") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
		} else if (strcmp(archive_entry_pathname(ae), "at/f1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 10);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 10);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "0123456789", 10);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 10);
		} else if (strcmp(archive_entry_pathname(ae), "at/f2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 11);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 11);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "hello world", 11);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 11);
		} else if (strcmp(archive_entry_pathname(ae), "at/fe") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 0);
		}
		if (archive_entry_filetype(ae) == AE_IFDIR) {
			/* Descend into the current object */
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_disk_descend(a));
		}
	}
	/* There is no entry. */
	failure("There must be no entry");
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header2(a, ae));

	failure("Atime should be restored");
	assertFileAtime("at", 886622, 0);
	failure("Atime should be restored");
	assertFileAtime("at/f1", 886600, 0);
	failure("Atime should be restored");
	assertFileAtime("at/f2", 886611, 0);
	failure("The atime of a empty file should not be changed");
	assertFileAtime("at/fe", 886611, 0);

	/* Close the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));

	/*
	 * Test3: Traversals with archive_read_disk_set_atime_restored() but
	 * no data read as a listing.
	 */
	assertUtimes("at/f1", 886600, 0, 886600, 0);
	assertUtimes("at/f2", 886611, 0, 886611, 0);
	assertUtimes("at/fe", 886611, 0, 886611, 0);
	assertUtimes("at", 886622, 0, 886622, 0);
	file_count = 4;
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_set_atime_restored(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_open(a, "at"));

	failure("Directory traversals should work as well");
	while (file_count--) {
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header2(a, ae));
		if (strcmp(archive_entry_pathname(ae), "at") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
		} else if (strcmp(archive_entry_pathname(ae), "at/f1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 10);
		} else if (strcmp(archive_entry_pathname(ae), "at/f2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 11);
		} else if (strcmp(archive_entry_pathname(ae), "at/fe") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 0);
		}
		if (archive_entry_filetype(ae) == AE_IFDIR) {
			/* Descend into the current object */
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_disk_descend(a));
		}
	}
	/* There is no entry. */
	failure("There must be no entry");
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header2(a, ae));

	failure("Atime should be restored");
	assertFileAtime("at", 886622, 0);
	failure("Atime should be restored");
	assertFileAtime("at/f1", 886600, 0);
	failure("Atime should be restored");
	assertFileAtime("at/f2", 886611, 0);
	failure("The atime of a empty file should not be changed");
	assertFileAtime("at/fe", 886611, 0);

	if (!canNodump()) {
		/* Destroy the disk object. */
		assertEqualInt(ARCHIVE_OK, archive_read_free(a));
		archive_entry_free(ae);
		skipping("Can't test atime with nodump on this filesystem");
		return;
	}

	/* Close the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));

	/*
	 * Test4: Traversals with ARCHIVE_READDISK_RESTORE_ATIME and
	 * ARCHIVE_READDISK_HONOR_NODUMP
	 */
	assertSetNodump("at/f1");
	assertSetNodump("at/f2");
	assertUtimes("at/f1", 886600, 0, 886600, 0);
	assertUtimes("at/f2", 886611, 0, 886611, 0);
	assertUtimes("at/fe", 886611, 0, 886611, 0);
	assertUtimes("at", 886622, 0, 886622, 0);
	file_count = 2;
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_set_behavior(a,
		ARCHIVE_READDISK_RESTORE_ATIME | ARCHIVE_READDISK_HONOR_NODUMP));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_open(a, "at"));

	failure("Directory traversals should work as well");
	while (file_count--) {
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header2(a, ae));
		if (strcmp(archive_entry_pathname(ae), "at") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
		} else if (strcmp(archive_entry_pathname(ae), "at/fe") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 0);
		}
		if (archive_entry_filetype(ae) == AE_IFDIR) {
			/* Descend into the current object */
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_disk_descend(a));
		}
	}
	/* There is no entry. */
	failure("There must be no entry");
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header2(a, ae));

	failure("Atime should be restored");
	assertFileAtime("at", 886622, 0);
	failure("Atime should be restored");
	assertFileAtime("at/f1", 886600, 0);
	failure("Atime should be restored");
	assertFileAtime("at/f2", 886611, 0);
	failure("The atime of a empty file should not be changed");
	assertFileAtime("at/fe", 886611, 0);

	/* Destroy the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	archive_entry_free(ae);
}

static int
metadata_filter(struct archive *a, void *data, struct archive_entry *ae)
{
	(void)data; /* UNUSED */

	failure("CTime should be set");
	assertEqualInt(8, archive_entry_ctime_is_set(ae));
	failure("MTime should be set");
	assertEqualInt(16, archive_entry_mtime_is_set(ae));

	if (archive_entry_mtime(ae) < 886611)
		return (0);
	if (archive_read_disk_can_descend(a)) {
		/* Descend into the current object */
		failure("archive_read_disk_can_descend should work"
			" in metadata filter");
		assertEqualIntA(a, 1, archive_read_disk_can_descend(a));
		failure("archive_read_disk_descend should work"
			" in metadata filter");
		assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_descend(a));
	}
	return (1);
}

static void
test_callbacks(void)
{
	struct archive *a;
	struct archive *m;
	struct archive_entry *ae;
	const void *p;
	size_t size;
	int64_t offset;
	int file_count;

	assertMakeDir("cb", 0755);
	assertMakeFile("cb/f1", 0644, "0123456789");
	assertMakeFile("cb/f2", 0644, "hello world");
	assertMakeFile("cb/fe", 0644, NULL);
	assertUtimes("cb/f1", 886600, 0, 886600, 0);
	assertUtimes("cb/f2", 886611, 0, 886611, 0);
	assertUtimes("cb/fe", 886611, 0, 886611, 0);
	assertUtimes("cb", 886622, 0, 886622, 0);

	assert((ae = archive_entry_new()) != NULL);
	assert((a = archive_read_disk_new()) != NULL);
	if (a == NULL) {
		archive_entry_free(ae);
		return;
	}
	assert((m = archive_match_new()) != NULL);
	if (m == NULL) {
		archive_entry_free(ae);
		archive_read_free(a);
		archive_match_free(m);
		return;
	}

	/*
	 * Test1: Traversals with a name filter.
	 */
	file_count = 3;
	assertEqualIntA(m, ARCHIVE_OK,
	    archive_match_exclude_pattern(m, "cb/f2"));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_set_matching(a, m, NULL, NULL));
	failure("Directory traversals should work as well");
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_open(a, "cb"));
	while (file_count--) {
		archive_entry_clear(ae);
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header2(a, ae));
		failure("File 'cb/f2' should be exclueded");
		assert(strcmp(archive_entry_pathname(ae), "cb/f2") != 0);
		if (strcmp(archive_entry_pathname(ae), "cb") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
		} else if (strcmp(archive_entry_pathname(ae), "cb/f1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 10);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 10);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "0123456789", 10);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 10);
		} else if (strcmp(archive_entry_pathname(ae), "cb/fe") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 0);
		}
		if (archive_read_disk_can_descend(a)) {
			/* Descend into the current object */
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_disk_descend(a));
		}
	}
	/* There is no entry. */
	failure("There should be no entry");
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header2(a, ae));

	/* Close the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));

	/* Reset name filter */
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_set_matching(a, NULL, NULL, NULL));

	/*
	 * Test2: Traversals with a metadata filter.
	 */
	assertUtimes("cb/f1", 886600, 0, 886600, 0);
	assertUtimes("cb/f2", 886611, 0, 886611, 0);
	assertUtimes("cb/fe", 886611, 0, 886611, 0);
	assertUtimes("cb", 886622, 0, 886622, 0);
	file_count = 3;
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_set_metadata_filter_callback(a, metadata_filter,
		    NULL));
	failure("Directory traversals should work as well");
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_open(a, "cb"));

	while (file_count--) {
		archive_entry_clear(ae);
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header2(a, ae));
		failure("File 'cb/f1' should be excluded");
		assert(strcmp(archive_entry_pathname(ae), "cb/f1") != 0);
		if (strcmp(archive_entry_pathname(ae), "cb") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
		} else if (strcmp(archive_entry_pathname(ae), "cb/f2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 11);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 11);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "hello world", 11);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 11);
		} else if (strcmp(archive_entry_pathname(ae), "cb/fe") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 0);
		}
	}
	/* There is no entry. */
	failure("There should be no entry");
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header2(a, ae));

	/* Destroy the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	assertEqualInt(ARCHIVE_OK, archive_match_free(m));
	archive_entry_free(ae);
}

static void
test_nodump(void)
{
	struct archive *a;
	struct archive_entry *ae;
	const void *p;
	size_t size;
	int64_t offset;
	int file_count;

	if (!canNodump()) {
		skipping("Can't test nodump on this filesystem");
		return;
	}

	assertMakeDir("nd", 0755);
	assertMakeFile("nd/f1", 0644, "0123456789");
	assertMakeFile("nd/f2", 0644, "hello world");
	assertMakeFile("nd/fe", 0644, NULL);
	assertSetNodump("nd/f2");
	assertUtimes("nd/f1", 886600, 0, 886600, 0);
	assertUtimes("nd/f2", 886611, 0, 886611, 0);
	assertUtimes("nd/fe", 886611, 0, 886611, 0);
	assertUtimes("nd", 886622, 0, 886622, 0);

	assert((ae = archive_entry_new()) != NULL);
	assert((a = archive_read_disk_new()) != NULL);

	/*
	 * Test1: Traversals without ARCHIVE_READDISK_HONOR_NODUMP
	 */
	failure("Directory traversals should work as well");
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_open(a, "nd"));

	file_count = 4;
	while (file_count--) {
		archive_entry_clear(ae);
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header2(a, ae));
		if (strcmp(archive_entry_pathname(ae), "nd") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
		} else if (strcmp(archive_entry_pathname(ae), "nd/f1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 10);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 10);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "0123456789", 10);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 10);
		} else if (strcmp(archive_entry_pathname(ae), "nd/f2") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 11);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 11);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "hello world", 11);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 11);
		} else if (strcmp(archive_entry_pathname(ae), "nd/fe") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 0);
		}
		if (archive_read_disk_can_descend(a)) {
			/* Descend into the current object */
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_disk_descend(a));
		}
	}
	/* There is no entry. */
	failure("There should be no entry");
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header2(a, ae));

	/* Close the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));

	/*
	 * Test2: Traversals with ARCHIVE_READDISK_HONOR_NODUMP
	 */
	assertUtimes("nd/f1", 886600, 0, 886600, 0);
	assertUtimes("nd/f2", 886611, 0, 886611, 0);
	assertUtimes("nd/fe", 886611, 0, 886611, 0);
	assertUtimes("nd", 886622, 0, 886622, 0);

	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_set_behavior(a,
		ARCHIVE_READDISK_RESTORE_ATIME | ARCHIVE_READDISK_HONOR_NODUMP));
	failure("Directory traversals should work as well");
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_open(a, "nd"));

	file_count = 3;
	while (file_count--) {
		archive_entry_clear(ae);
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header2(a, ae));
		failure("File 'nd/f2' should be exclueded");
		assert(strcmp(archive_entry_pathname(ae), "nd/f2") != 0);
		if (strcmp(archive_entry_pathname(ae), "nd") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
		} else if (strcmp(archive_entry_pathname(ae), "nd/f1") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 10);
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 10);
			assertEqualInt((int)offset, 0);
			assertEqualMem(p, "0123456789", 10);
			assertEqualInt(ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
			assertEqualInt((int)offset, 10);
		} else if (strcmp(archive_entry_pathname(ae), "nd/fe") == 0) {
			assertEqualInt(archive_entry_filetype(ae), AE_IFREG);
			assertEqualInt(archive_entry_size(ae), 0);
		}
		if (archive_read_disk_can_descend(a)) {
			/* Descend into the current object */
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_disk_descend(a));
		}
	}
	/* There is no entry. */
	failure("There should be no entry");
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header2(a, ae));

	failure("Atime should be restored");
	assertFileAtime("nd/f2", 886611, 0);

	/* Destroy the disk object. */
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	archive_entry_free(ae);
}

DEFINE_TEST(test_read_disk_directory_traversals)
{
	/* Basic test. */
	test_basic();
	/* Test hybrid mode; follow symlink initially, then not. */
	test_symlink_hybrid();
	/* Test logical mode; follow all symlinks. */
	test_symlink_logical();
	/* Test logical mode; prevent loop in symlinks. */
	test_symlink_logical_loop();
	/* Test to restore atime. */
	test_restore_atime();
	/* Test callbacks. */
	test_callbacks();
	/* Test nodump. */
	test_nodump();
}
