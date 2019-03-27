/*-
 * Copyright (c) 2012 Michihiro NAKAJIMA
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
test_format_by_name(const char *format_name, const char *compression_type,
    int format_id, int dot_stored, const void *image, size_t image_size)
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
	r = archive_write_set_format_by_name(a, format_name);
	if (r == ARCHIVE_WARN) {
		skipping("%s format not fully supported on this platform",
		   compression_type);
		assertEqualInt(ARCHIVE_OK, archive_write_free(a));
		free(buff);
		return;
	}
	assertEqualIntA(a, ARCHIVE_OK, r);
	if (compression_type != NULL &&
	    ARCHIVE_OK != archive_write_set_format_option(a, format_name,
	    "compression", compression_type)) {
		skipping("%s writing not fully supported on this platform",
		   compression_type);
		assertEqualInt(ARCHIVE_OK, archive_write_free(a));
		free(buff);
		return;
	}
	assertEqualIntA(a, ARCHIVE_OK, archive_write_add_filter_none(a));
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

	if (image && image_size > 0) {
		assertEqualMem(buff, image, image_size);
	}
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
		assertEqualIntA(a, ARCHIVE_FILTER_NONE,
		    archive_filter_code(a, 0));
		assertEqualIntA(a, format_id, archive_format(a));

		assertEqualInt(ARCHIVE_OK, archive_read_close(a));
		assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	}
	free(buff);
}

DEFINE_TEST(test_archive_write_set_format_by_name_7zip)
{
	test_format_by_name("7zip", "copy", ARCHIVE_FORMAT_7ZIP, 0,
	    "\x37\x7a\xbc\xaf\x27\x1c\x00\x03", 8);
}

DEFINE_TEST(test_archive_write_set_format_by_name_ar)
{
	test_format_by_name("ar", NULL, ARCHIVE_FORMAT_AR, 0, NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_arbsd)
{
	test_format_by_name("arbsd", NULL, ARCHIVE_FORMAT_AR, 0, NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_argnu)
{
	test_format_by_name("argnu", NULL, ARCHIVE_FORMAT_AR_GNU, 0, NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_arsvr4)
{
	test_format_by_name("arsvr4", NULL, ARCHIVE_FORMAT_AR_GNU, 0, NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_bsdtar)
{
	test_format_by_name("bsdtar", NULL, ARCHIVE_FORMAT_TAR_USTAR, 0, NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_cd9660)
{
	test_format_by_name("cd9660", NULL, ARCHIVE_FORMAT_ISO9660_ROCKRIDGE, 1,
	    NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_cpio)
{
	test_format_by_name("cpio", NULL, ARCHIVE_FORMAT_CPIO_POSIX, 0, NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_gnutar)
{
	test_format_by_name("gnutar", NULL, ARCHIVE_FORMAT_TAR_GNUTAR, 0,
	    NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_iso)
{
	test_format_by_name("iso", NULL, ARCHIVE_FORMAT_ISO9660_ROCKRIDGE, 1,
	    NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_iso9660)
{
	test_format_by_name("iso9660", NULL, ARCHIVE_FORMAT_ISO9660_ROCKRIDGE, 1,
	    NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_mtree)
{
	test_format_by_name("mtree", NULL, ARCHIVE_FORMAT_MTREE, 2, NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_mtree_classic)
{
	test_format_by_name("mtree-classic", NULL, ARCHIVE_FORMAT_MTREE, 1,
	    NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_newc)
{
	test_format_by_name("newc", NULL, ARCHIVE_FORMAT_CPIO_SVR4_NOCRC, 0,
	    NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_odc)
{
	test_format_by_name("odc", NULL, ARCHIVE_FORMAT_CPIO_POSIX, 0, NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_oldtar)
{
	test_format_by_name("oldtar", NULL, ARCHIVE_FORMAT_TAR, 0, NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_pax)
{
	test_format_by_name("pax", NULL, ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE, 0,
	    NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_paxr)
{
	test_format_by_name("paxr", NULL, ARCHIVE_FORMAT_TAR_USTAR, 0, NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_posix)
{
	test_format_by_name("posix", NULL, ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE, 0,
	    NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_rpax)
{
	test_format_by_name("rpax", NULL, ARCHIVE_FORMAT_TAR_USTAR, 0, NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_shar)
{
	test_format_by_name("shar", NULL, -1, 0,
	    "#!/bin/sh\n# This is a shell archive\n", 36);
}

DEFINE_TEST(test_archive_write_set_format_by_name_shardump)
{
	test_format_by_name("shardump", NULL, -1, 0,
	    "#!/bin/sh\n# This is a shell archive\n", 36);
}

DEFINE_TEST(test_archive_write_set_format_by_name_ustar)
{
	test_format_by_name("ustar", NULL, ARCHIVE_FORMAT_TAR_USTAR, 0, NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_v7tar)
{
	test_format_by_name("v7tar", NULL, ARCHIVE_FORMAT_TAR, 0, NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_v7)
{
	test_format_by_name("v7", NULL, ARCHIVE_FORMAT_TAR, 0, NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_warc)
{
	test_format_by_name("warc", NULL, ARCHIVE_FORMAT_WARC, 0, NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_xar)
{
	test_format_by_name("xar", "gzip", ARCHIVE_FORMAT_XAR, 0, NULL, 0);
}

DEFINE_TEST(test_archive_write_set_format_by_name_zip)
{
	test_format_by_name("zip", "store", ARCHIVE_FORMAT_ZIP, 0, NULL, 0);
}
