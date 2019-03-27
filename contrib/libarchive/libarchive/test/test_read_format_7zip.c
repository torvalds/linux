/*-
 * Copyright (c) 2011 Michihiro NAKAJIMA
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
__FBSDID("$FreeBSD");

#if defined(_WIN32) && !defined(__CYGWIN__)
#define	close		_close
#define	open		_open
#endif

/*
 * Extract a non-encoded file.
 * The header of the 7z archive files is not encoded.
 */
static void
test_copy(int use_open_fd)
{
	const char *refname = "test_read_format_7zip_copy.7z";
	struct archive_entry *ae;
	struct archive *a;
	char buff[128];
	int fd = -1;

	extract_reference_file(refname);
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	if (use_open_fd) {
		fd = open(refname, O_RDONLY | O_BINARY); 
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_read_open_fd(a, fd, 10240));
	} else {
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_read_open_filename(a, refname, 10240));
	}

	/* Verify regular file1. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0666), archive_entry_mode(ae));
	assertEqualString("file1", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualInt(60, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assert(archive_read_has_encrypted_entries(a) > ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);
	assertEqualInt(60, archive_read_data(a, buff, sizeof(buff)));
	assertEqualMem(buff, "    ", 4);

	assertEqualInt(1, archive_file_count(a));

	/* End of archive. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	/* Verify archive format. */
	assertEqualIntA(a, ARCHIVE_FILTER_NONE, archive_filter_code(a, 0));
	assertEqualIntA(a, ARCHIVE_FORMAT_7ZIP, archive_format(a));

	/* Close the archive. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	if (fd != -1)
		close(fd);
}

/*
 * An archive file has no entry.
 */
static void
test_empty_archive()
{
	const char *refname = "test_read_format_7zip_empty_archive.7z";
	struct archive_entry *ae;
	struct archive *a;

	extract_reference_file(refname);
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_open_filename(a, refname, 10240));

	/* End of archive. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	assertEqualInt(0, archive_file_count(a));

	/* Verify archive format. */
	assertEqualIntA(a, ARCHIVE_FILTER_NONE, archive_filter_code(a, 0));
	assertEqualIntA(a, ARCHIVE_FORMAT_7ZIP, archive_format(a));

	/* Close the archive. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

/*
 * An archive file has one empty file. It means there is no content
 * in the archive file except for a header.
 */
static void
test_empty_file()
{
	const char *refname = "test_read_format_7zip_empty_file.7z";
	struct archive_entry *ae;
	struct archive *a;

	extract_reference_file(refname);
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_open_filename(a, refname, 10240));

	/* Verify regular empty. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("empty", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualInt(0, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

	assertEqualInt(1, archive_file_count(a));

	/* End of archive. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	/* Verify archive format. */
	assertEqualIntA(a, ARCHIVE_FILTER_NONE, archive_filter_code(a, 0));
	assertEqualIntA(a, ARCHIVE_FORMAT_7ZIP, archive_format(a));

	/* Close the archive. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

/*
 * Extract an encoded file.
 * The header of the 7z archive files is not encoded.
 */
static void
test_plain_header(const char *refname)
{
	struct archive_entry *ae;
	struct archive *a;
	char buff[128];

	extract_reference_file(refname);
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_open_filename(a, refname, 10240));

	/* Verify regular file1. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("file1", archive_entry_pathname(ae));
	assertEqualInt(1322058763, archive_entry_mtime(ae));
	assertEqualInt(2844, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
	assertEqualInt(sizeof(buff), archive_read_data(a, buff, sizeof(buff)));
	assertEqualMem(buff, "The libarchive distribution ", 28);

	assertEqualInt(1, archive_file_count(a));

	/* End of archive. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	/* Verify archive format. */
	assertEqualIntA(a, ARCHIVE_FILTER_NONE, archive_filter_code(a, 0));
	assertEqualIntA(a, ARCHIVE_FORMAT_7ZIP, archive_format(a));

	/* Close the archive. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

/*
 * Extract multi files.
 * The header of the 7z archive files is encoded with LZMA.
 */
static void
test_extract_all_files(const char *refname)
{
	struct archive_entry *ae;
	struct archive *a;
	char buff[128];

	extract_reference_file(refname);
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_open_filename(a, refname, 10240));

	/* Verify regular file1. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("dir1/file1", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualInt(13, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
	assertEqualInt(13, archive_read_data(a, buff, sizeof(buff)));
	assertEqualMem(buff, "aaaaaaaaaaaa\n", 13);

	/* Verify regular file2. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("file2", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualInt(26, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
	assertEqualInt(26, archive_read_data(a, buff, sizeof(buff)));
	assertEqualMem(buff, "aaaaaaaaaaaa\nbbbbbbbbbbbb\n", 26);

	/* Verify regular file3. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("file3", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualInt(39, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
	assertEqualInt(39, archive_read_data(a, buff, sizeof(buff)));
	assertEqualMem(buff, "aaaaaaaaaaaa\nbbbbbbbbbbbb\ncccccccccccc\n", 39);

	/* Verify regular file4. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("file4", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualInt(52, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
	assertEqualInt(52, archive_read_data(a, buff, sizeof(buff)));
	assertEqualMem(buff,
	    "aaaaaaaaaaaa\nbbbbbbbbbbbb\ncccccccccccc\ndddddddddddd\n", 52);

	/* Verify directory dir1. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFDIR | 0755), archive_entry_mode(ae));
	assertEqualString("dir1/", archive_entry_pathname(ae));
	assertEqualInt(2764801, archive_entry_mtime(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

	assertEqualInt(5, archive_file_count(a));

	/* End of archive. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	/* Verify archive format. */
	assertEqualIntA(a, ARCHIVE_FILTER_NONE, archive_filter_code(a, 0));
	assertEqualIntA(a, ARCHIVE_FORMAT_7ZIP, archive_format(a));

	/* Close the archive. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

/*
 * Extract last file.
 * The header of the 7z archive files is encoded with LZMA.
 */
static void
test_extract_last_file(const char *refname)
{
	struct archive_entry *ae;
	struct archive *a;
	char buff[128];

	extract_reference_file(refname);
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_open_filename(a, refname, 10240));

	/* Verify regular file1. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("dir1/file1", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualInt(13, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

	/* Verify regular file2. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("file2", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualInt(26, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

	/* Verify regular file3. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("file3", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualInt(39, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

	/* Verify regular file4. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("file4", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualInt(52, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
	assertEqualInt(52, archive_read_data(a, buff, sizeof(buff)));
	assertEqualMem(buff,
	    "aaaaaaaaaaaa\nbbbbbbbbbbbb\ncccccccccccc\ndddddddddddd\n", 52);

	/* Verify directory dir1. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFDIR | 0755), archive_entry_mode(ae));
	assertEqualString("dir1/", archive_entry_pathname(ae));
	assertEqualInt(2764801, archive_entry_mtime(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

	assertEqualInt(5, archive_file_count(a));

	/* End of archive. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	/* Verify archive format. */
	assertEqualIntA(a, ARCHIVE_FILTER_NONE, archive_filter_code(a, 0));
	assertEqualIntA(a, ARCHIVE_FORMAT_7ZIP, archive_format(a));

	/* Close the archive. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

/*
 * Extract a mixed archive file which has both LZMA and LZMA2 encoded files.
 *  LZMA: file1, file2, file3, file4
 *  LZMA2: zfile1, zfile2, zfile3, zfile4
 */
static void
test_extract_all_files2(const char *refname)
{
	struct archive_entry *ae;
	struct archive *a;
	char buff[128];

	extract_reference_file(refname);
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_open_filename(a, refname, 10240));

	/* Verify regular file1. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("dir1/file1", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualInt(13, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
	assertEqualInt(13, archive_read_data(a, buff, sizeof(buff)));
	assertEqualMem(buff, "aaaaaaaaaaaa\n", 13);

	/* Verify regular file2. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("file2", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualInt(26, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
	assertEqualInt(26, archive_read_data(a, buff, sizeof(buff)));
	assertEqualMem(buff, "aaaaaaaaaaaa\nbbbbbbbbbbbb\n", 26);

	/* Verify regular file3. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("file3", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualInt(39, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
	assertEqualInt(39, archive_read_data(a, buff, sizeof(buff)));
	assertEqualMem(buff, "aaaaaaaaaaaa\nbbbbbbbbbbbb\ncccccccccccc\n", 39);

	/* Verify regular file4. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("file4", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualInt(52, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
	assertEqualInt(52, archive_read_data(a, buff, sizeof(buff)));
	assertEqualMem(buff,
	    "aaaaaaaaaaaa\nbbbbbbbbbbbb\ncccccccccccc\ndddddddddddd\n", 52);

	/* Verify regular zfile1. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("dir1/zfile1", archive_entry_pathname(ae));
	assertEqualInt(5184001, archive_entry_mtime(ae));
	assertEqualInt(13, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
	assertEqualInt(13, archive_read_data(a, buff, sizeof(buff)));
	assertEqualMem(buff, "aaaaaaaaaaaa\n", 13);

	/* Verify regular zfile2. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("zfile2", archive_entry_pathname(ae));
	assertEqualInt(5184001, archive_entry_mtime(ae));
	assertEqualInt(26, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
	assertEqualInt(26, archive_read_data(a, buff, sizeof(buff)));
	assertEqualMem(buff, "aaaaaaaaaaaa\nbbbbbbbbbbbb\n", 26);

	/* Verify regular zfile3. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("zfile3", archive_entry_pathname(ae));
	assertEqualInt(5184001, archive_entry_mtime(ae));
	assertEqualInt(39, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
	assertEqualInt(39, archive_read_data(a, buff, sizeof(buff)));
	assertEqualMem(buff, "aaaaaaaaaaaa\nbbbbbbbbbbbb\ncccccccccccc\n", 39);

	/* Verify regular zfile4. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("zfile4", archive_entry_pathname(ae));
	assertEqualInt(5184001, archive_entry_mtime(ae));
	assertEqualInt(52, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
	assertEqualInt(52, archive_read_data(a, buff, sizeof(buff)));
	assertEqualMem(buff,
	    "aaaaaaaaaaaa\nbbbbbbbbbbbb\ncccccccccccc\ndddddddddddd\n", 52);

	/* Verify directory dir1. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFDIR | 0755), archive_entry_mode(ae));
	assertEqualString("dir1/", archive_entry_pathname(ae));
	assertEqualInt(2764801, archive_entry_mtime(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

	assertEqualInt(9, archive_file_count(a));

	/* End of archive. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	/* Verify archive format. */
	assertEqualIntA(a, ARCHIVE_FILTER_NONE, archive_filter_code(a, 0));
	assertEqualIntA(a, ARCHIVE_FORMAT_7ZIP, archive_format(a));

	/* Close the archive. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

/*
 * Extract a file compressed with DELTA + LZMA[12].
 */
static void
test_delta_lzma(const char *refname)
{
	struct archive_entry *ae;
	struct archive *a;
	size_t remaining;
	ssize_t bytes;
	char buff[1024];

	extract_reference_file(refname);
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_open_filename(a, refname, 10240));

	/* Verify regular file1. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("file1", archive_entry_pathname(ae));
	assertEqualInt(172802, archive_entry_mtime(ae));
	assertEqualInt(27627, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
	remaining = (size_t)archive_entry_size(ae);
	while (remaining) {
		if (remaining < sizeof(buff))
			assertEqualInt(remaining,
			    bytes = archive_read_data(a, buff, sizeof(buff)));
		else
			assertEqualInt(sizeof(buff),
			    bytes = archive_read_data(a, buff, sizeof(buff)));
		if (bytes > 0)
			remaining -= bytes;
		else
			break;
	}
	assertEqualInt(0, remaining);

	assertEqualInt(1, archive_file_count(a));

	/* End of archive. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	/* Verify archive format. */
	assertEqualIntA(a, ARCHIVE_FILTER_NONE, archive_filter_code(a, 0));
	assertEqualIntA(a, ARCHIVE_FORMAT_7ZIP, archive_format(a));

	/* Close the archive. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

/*
 * Extract a file compressed with BCJ + LZMA2.
 */
static void
test_bcj(const char *refname)
{
	struct archive_entry *ae;
	struct archive *a;
	size_t remaining;
	ssize_t bytes;
	char buff[1024];

	extract_reference_file(refname);
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_open_filename(a, refname, 10240));

	/* Verify regular x86exe. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0444), archive_entry_mode(ae) & ~0111);
	assertEqualString("x86exe", archive_entry_pathname(ae));
	assertEqualInt(172802, archive_entry_mtime(ae));
	assertEqualInt(27328, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
	remaining = (size_t)archive_entry_size(ae);
	while (remaining) {
		if (remaining < sizeof(buff))
			assertEqualInt(remaining,
			    bytes = archive_read_data(a, buff, sizeof(buff)));
		else
			assertEqualInt(sizeof(buff),
			    bytes = archive_read_data(a, buff, sizeof(buff)));
		if (bytes > 0)
			remaining -= bytes;
		else
			break;
	}
	assertEqualInt(0, remaining);

	assertEqualInt(1, archive_file_count(a));

	/* End of archive. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	/* Verify archive format. */
	assertEqualIntA(a, ARCHIVE_FILTER_NONE, archive_filter_code(a, 0));
	assertEqualIntA(a, ARCHIVE_FORMAT_7ZIP, archive_format(a));

	/* Close the archive. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

/*
 * Extract a file compressed with PPMd.
 */
static void
test_ppmd()
{
	const char *refname = "test_read_format_7zip_ppmd.7z";
	struct archive_entry *ae;
	struct archive *a;
	size_t remaining;
	ssize_t bytes;
	char buff[1024];

	extract_reference_file(refname);
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_open_filename(a, refname, 10240));

	/* Verify regular file1. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0666), archive_entry_mode(ae));
	assertEqualString("ppmd_test.txt", archive_entry_pathname(ae));
	assertEqualInt(1322464589, archive_entry_mtime(ae));
	assertEqualInt(102400, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
	remaining = (size_t)archive_entry_size(ae);
	while (remaining) {
		if (remaining < sizeof(buff))
			assertEqualInt(remaining,
			    bytes = archive_read_data(a, buff, sizeof(buff)));
		else
			assertEqualInt(sizeof(buff),
			    bytes = archive_read_data(a, buff, sizeof(buff)));
		if (bytes > 0)
			remaining -= bytes;
		else
			break;
	}
	assertEqualInt(0, remaining);

	assertEqualInt(1, archive_file_count(a));

	/* End of archive. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	/* Verify archive format. */
	assertEqualIntA(a, ARCHIVE_FILTER_NONE, archive_filter_code(a, 0));
	assertEqualIntA(a, ARCHIVE_FORMAT_7ZIP, archive_format(a));

	/* Close the archive. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

static void
test_symname()
{
	const char *refname = "test_read_format_7zip_symbolic_name.7z";
	struct archive_entry *ae;
	struct archive *a;
	char buff[128];

	extract_reference_file(refname);
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_open_filename(a, refname, 10240));

	/* Verify regular file1. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("file1", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualInt(32, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
	assertEqualInt(32, archive_read_data(a, buff, sizeof(buff)));
	assertEqualMem(buff, "hellohellohello\nhellohellohello\n", 32);

	/* Verify symbolic-link symlinkfile. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFLNK | 0755), archive_entry_mode(ae));
	assertEqualString("symlinkfile", archive_entry_pathname(ae));
	assertEqualString("file1", archive_entry_symlink(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

	assertEqualInt(2, archive_file_count(a));

	/* End of archive. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	/* Verify archive format. */
	assertEqualIntA(a, ARCHIVE_FILTER_NONE, archive_filter_code(a, 0));
	assertEqualIntA(a, ARCHIVE_FORMAT_7ZIP, archive_format(a));

	/* Close the archive. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}


DEFINE_TEST(test_read_format_7zip)
{
	struct archive *a;

	assert((a = archive_read_new()) != NULL);

	/* Extracting with liblzma */
	if (ARCHIVE_OK != archive_read_support_filter_xz(a)) {
		skipping("7zip:lzma decoding is not supported on this platform");
	} else {
		test_symname();
		test_extract_all_files("test_read_format_7zip_copy_2.7z");
		test_extract_last_file("test_read_format_7zip_copy_2.7z");
		test_extract_all_files2("test_read_format_7zip_lzma1_lzma2.7z");
		test_bcj("test_read_format_7zip_bcj2_copy_lzma.7z");
	}
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_format_7zip_bzip2)
{
	struct archive *a;

	assert((a = archive_read_new()) != NULL);

	/* Extracting with libbzip2 */
	if (ARCHIVE_OK != archive_read_support_filter_bzip2(a)) {
		skipping("7zip:bzip2 decoding is not supported on this platform");
	} else {
		test_plain_header("test_read_format_7zip_bzip2.7z");
		test_bcj("test_read_format_7zip_bcj_bzip2.7z");
		test_bcj("test_read_format_7zip_bcj2_bzip2.7z");
	}

	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_format_7zip_from_fd)
{
	test_copy(1);/* read a 7zip file from a file descriptor. */
}

DEFINE_TEST(test_read_format_7zip_copy)
{
	test_copy(0);
	test_bcj("test_read_format_7zip_bcj_copy.7z");
	test_bcj("test_read_format_7zip_bcj2_copy_1.7z");
	test_bcj("test_read_format_7zip_bcj2_copy_2.7z");
}

DEFINE_TEST(test_read_format_7zip_deflate)
{
	struct archive *a;

	assert((a = archive_read_new()) != NULL);

	/* Extracting with libz */
	if (ARCHIVE_OK != archive_read_support_filter_gzip(a)) {
		skipping(
		    "7zip:deflate decoding is not supported on this platform");
	} else {
		test_plain_header("test_read_format_7zip_deflate.7z");
		test_bcj("test_read_format_7zip_bcj_deflate.7z");
		test_bcj("test_read_format_7zip_bcj2_deflate.7z");
	}

	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_format_7zip_empty)
{
	test_empty_archive();
	test_empty_file();
}

DEFINE_TEST(test_read_format_7zip_lzma1)
{
	struct archive *a;

	assert((a = archive_read_new()) != NULL);

	/* Extracting with liblzma */
	if (ARCHIVE_OK != archive_read_support_filter_xz(a)) {
		skipping("7zip:lzma decoding is not supported on this platform");
	} else {
		test_plain_header("test_read_format_7zip_lzma1.7z");
		test_extract_all_files("test_read_format_7zip_lzma1_2.7z");
		test_extract_last_file("test_read_format_7zip_lzma1_2.7z");
		test_bcj("test_read_format_7zip_bcj_lzma1.7z");
		test_bcj("test_read_format_7zip_bcj2_lzma1_1.7z");
		test_bcj("test_read_format_7zip_bcj2_lzma1_2.7z");
		test_delta_lzma("test_read_format_7zip_delta_lzma1.7z");
	}
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_format_7zip_lzma2)
{
	struct archive *a;

	assert((a = archive_read_new()) != NULL);

	/* Extracting with liblzma */
	if (ARCHIVE_OK != archive_read_support_filter_xz(a)) {
		skipping("7zip:lzma decoding is not supported on this platform");
	} else {
		test_plain_header("test_read_format_7zip_lzma2.7z");
		test_bcj("test_read_format_7zip_bcj_lzma2.7z");
		test_bcj("test_read_format_7zip_bcj2_lzma2_1.7z");
		test_bcj("test_read_format_7zip_bcj2_lzma2_2.7z");
		test_delta_lzma("test_read_format_7zip_delta_lzma2.7z");
	}
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_format_7zip_ppmd)
{
	test_ppmd();
}
