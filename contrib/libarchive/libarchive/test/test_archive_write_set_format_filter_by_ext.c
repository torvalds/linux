/*-
 * Copyright (c) 2012 Michihiro NAKAJIMA
 * Copyright (c) 2015 Okhotnikov Kirill
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

static void
test_format_filter_by_ext(const char *output_file, 
    int format_id, int filter_id, int dot_stored, char * def_ext)
{
	struct archive_entry *ae;
	struct archive *a;
	size_t used;
	size_t buffsize = 1024 * 1024;
	char *buff;
	int r;

	assert((buff = malloc(buffsize)) != NULL);
	if (buff == NULL)
		return;

	/* Create a new archive in memory. */
	assert((a = archive_write_new()) != NULL);
	if( def_ext == NULL)
          r = archive_write_set_format_filter_by_ext(a, output_file);
        else
          r = archive_write_set_format_filter_by_ext_def(a, output_file, def_ext);
	if (r == ARCHIVE_WARN) {
		skipping("%s format not fully supported on this platform",
		   archive_format_name(a));
		assertEqualInt(ARCHIVE_OK, archive_write_free(a));
		free(buff);
		return;
        } else if (r == ARCHIVE_FATAL &&
	    (strcmp(archive_error_string(a),
		   "lzma compression not supported on this platform") == 0 ||
	     strcmp(archive_error_string(a),
		   "xz compression not supported on this platform") == 0)) {
                const char *filter_name = archive_filter_name(a, 0);
		skipping("%s filter not suported on this platform", filter_name);
		assertEqualInt(ARCHIVE_OK, archive_write_free(a));
		free(buff);
		return;
	} else {
		if (!assertEqualIntA(a, ARCHIVE_OK, r)) {
			assertEqualInt(ARCHIVE_OK, archive_write_free(a));
			free(buff);
			return;
		}
	}
        
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_open_memory(a, buff, buffsize, &used));

	/*
	 * Write a file to it.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_mtime(ae, 1, 0);
	assertEqualInt(1, archive_entry_mtime(ae));
	archive_entry_set_ctime(ae, 1, 0);
	assertEqualInt(1, archive_entry_ctime(ae));
	archive_entry_set_atime(ae, 1, 0);
	assertEqualInt(1, archive_entry_atime(ae));
	archive_entry_copy_pathname(ae, "file");
	assertEqualString("file", archive_entry_pathname(ae));
	archive_entry_set_mode(ae, AE_IFREG | 0755);
	assertEqualInt((AE_IFREG | 0755), archive_entry_mode(ae));
	archive_entry_set_size(ae, 8);
	assertEqualInt(0, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualInt(8, archive_write_data(a, "12345678", 8));

	/* Close out the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	if (format_id > 0) {
		/*
		 * Now, read the data back.
		 */
		/* With the test memory reader -- seeking mode. */
		assert((a = archive_read_new()) != NULL);
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_read_support_format_all(a));
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_read_support_filter_all(a));
		assertEqualIntA(a, ARCHIVE_OK,
		    read_open_memory_seek(a, buff, used, 7));

		if (dot_stored & 1) {
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_read_next_header(a, &ae));
			assertEqualString(".", archive_entry_pathname(ae));
			assertEqualInt(AE_IFDIR, archive_entry_filetype(ae));
		}
		/*
		 * Read and verify the file.
		 */
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
		assertEqualInt(1, archive_entry_mtime(ae));
		if (dot_stored & 2) {
			assertEqualString("./file", archive_entry_pathname(ae));
		} else {
			assertEqualString("file", archive_entry_pathname(ae));
		}
		assertEqualInt(AE_IFREG, archive_entry_filetype(ae));
		assertEqualInt(8, archive_entry_size(ae));

		/* Verify the end of the archive. */
		assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

		/* Verify archive format. */
		assertEqualIntA(a, filter_id,
		    archive_filter_code(a, 0));
		assertEqualIntA(a, format_id, archive_format(a) & ARCHIVE_FORMAT_BASE_MASK );

		assertEqualInt(ARCHIVE_OK, archive_read_close(a));
		assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	}
	free(buff);
}

DEFINE_TEST(test_archive_write_set_format_filter_by_ext_7zip)
{
	test_format_filter_by_ext("./data/test.7z", ARCHIVE_FORMAT_7ZIP, ARCHIVE_FILTER_NONE, 0, NULL);
}

DEFINE_TEST(test_archive_write_set_format_filter_by_ext_zip)
{
	test_format_filter_by_ext("./data/test.zip", ARCHIVE_FORMAT_ZIP, ARCHIVE_FILTER_NONE, 0, NULL);
}

DEFINE_TEST(test_archive_write_set_format_filter_by_ext_jar)
{
	test_format_filter_by_ext("./data/test.jar", ARCHIVE_FORMAT_ZIP, ARCHIVE_FILTER_NONE, 0, NULL);
}

DEFINE_TEST(test_archive_write_set_format_filter_by_ext_a)
{
	test_format_filter_by_ext("./data/test.a", ARCHIVE_FORMAT_AR, ARCHIVE_FILTER_NONE, 0, NULL);
}

DEFINE_TEST(test_archive_write_set_format_filter_by_ext_ar)
{
	test_format_filter_by_ext("./data/test.ar", ARCHIVE_FORMAT_AR, ARCHIVE_FILTER_NONE, 0, NULL);
}

DEFINE_TEST(test_archive_write_set_format_filter_by_ext_cpio)
{
	test_format_filter_by_ext("./data/test.cpio", ARCHIVE_FORMAT_CPIO, ARCHIVE_FILTER_NONE, 0, NULL);
}

DEFINE_TEST(test_archive_write_set_format_filter_by_ext_iso)
{
	test_format_filter_by_ext("./data/test.iso", ARCHIVE_FORMAT_ISO9660, ARCHIVE_FILTER_NONE, 1, NULL);
}

DEFINE_TEST(test_archive_write_set_format_filter_by_ext_tar)
{
	test_format_filter_by_ext("./data/test.tar", ARCHIVE_FORMAT_TAR, ARCHIVE_FILTER_NONE, 0, NULL);
}

DEFINE_TEST(test_archive_write_set_format_filter_by_ext_tar_gz)
{
	test_format_filter_by_ext("./data/test.tar.gz", ARCHIVE_FORMAT_TAR, ARCHIVE_FILTER_GZIP, 20, NULL);
}

DEFINE_TEST(test_archive_write_set_format_filter_by_ext_tar_bz2)
{
	test_format_filter_by_ext("./data/test.tar.bz2", ARCHIVE_FORMAT_TAR, ARCHIVE_FILTER_BZIP2, 0, NULL);
}

DEFINE_TEST(test_archive_write_set_format_filter_by_ext_tar_xz)
{
	test_format_filter_by_ext("./data/test.tar.xz", ARCHIVE_FORMAT_TAR, ARCHIVE_FILTER_XZ, 0, NULL);
}

DEFINE_TEST(test_archive_write_set_format_filter_by_no_ext_def_zip)
{
	test_format_filter_by_ext("./data/test", ARCHIVE_FORMAT_ZIP, ARCHIVE_FILTER_NONE, 0, ".zip");
}

DEFINE_TEST(test_archive_write_set_format_filter_by_ext_tar_bz2_def_zip)
{
	test_format_filter_by_ext("./data/test.tar.bz2", ARCHIVE_FORMAT_TAR, ARCHIVE_FILTER_BZIP2, 0, ".zip");
}
