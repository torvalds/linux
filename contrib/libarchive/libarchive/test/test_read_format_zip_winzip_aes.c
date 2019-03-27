/*-
 * Copyright (c) 2013 Konrad Kleine
 * Copyright (c) 2014 Michihiro NAKAJIMA
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
test_winzip_aes(const char *refname, int need_libz)
{
	struct archive_entry *ae;
	struct archive *a;
	char buff[512];

	/* Check if running system has cryptographic functionality. */
	assert((a = archive_write_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_set_format_zip(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_add_filter_none(a));
	if (ARCHIVE_OK != archive_write_set_options(a,
				"zip:encryption=aes256")) {
		skipping("This system does not have cryptographic liberary");
		archive_write_free(a);
		return;
	}
	archive_write_free(a);


	extract_reference_file(refname);

	/*
	 * Extract a zip file without password.
	 */
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, 
               archive_read_open_filename(a, refname, 10240));

	assertEqualIntA(a, ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW,
		archive_read_has_encrypted_entries(a));

	/* Verify encrypted file "README" */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("README", archive_entry_pathname(ae));
	assertEqualInt(6818, archive_entry_size(ae));
	assertEqualInt(1, archive_entry_is_data_encrypted(ae));
	assertEqualInt(0, archive_entry_is_metadata_encrypted(ae));
	assertEqualIntA(a, 1, archive_read_has_encrypted_entries(a));
	assertEqualInt(ARCHIVE_FAILED, archive_read_data(a, buff, sizeof(buff)));
	assertEqualInt(1, archive_file_count(a));

	/* End of archive. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	/* Verify archive format. */
	assertEqualIntA(a, ARCHIVE_FILTER_NONE, archive_filter_code(a, 0));
	assertEqualIntA(a, ARCHIVE_FORMAT_ZIP, archive_format(a));

	/* Close the archive. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));


	/*
	 * Extract a zip file with password.
	 */
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	/* Pass three passphrases to decrypt a file content. */
	assertEqualIntA(a, ARCHIVE_OK,
		archive_read_add_passphrase(a, "invalid_pass"));
	assertEqualIntA(a, ARCHIVE_OK,
		archive_read_add_passphrase(a, "invalid_phrase"));
	assertEqualIntA(a, ARCHIVE_OK,
		archive_read_add_passphrase(a, "password"));
	assertEqualIntA(a, ARCHIVE_OK, 
		archive_read_open_filename(a, refname, 10240));

	assertEqualIntA(a, ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW,
		archive_read_has_encrypted_entries(a));

	/* Verify encrypted file "README" */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("README", archive_entry_pathname(ae));
	assertEqualInt(6818, archive_entry_size(ae));
	assertEqualInt(1, archive_entry_is_data_encrypted(ae));
	assertEqualInt(0, archive_entry_is_metadata_encrypted(ae));
	assertEqualIntA(a, 1, archive_read_has_encrypted_entries(a));
	if (!need_libz || archive_zlib_version() != NULL) {
		assertEqualInt(512, archive_read_data(a, buff, sizeof(buff)));
	} else {
		assertEqualInt(ARCHIVE_FAILED, archive_read_data(a, buff, 19));
		assertEqualString(archive_error_string(a),
		    "Unsupported ZIP compression method (deflation)");
		assert(archive_errno(a) != 0);
	}
	
	assertEqualInt(1, archive_file_count(a));

	/* End of archive. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	/* Verify archive format. */
	assertEqualIntA(a, ARCHIVE_FILTER_NONE, archive_filter_code(a, 0));
	assertEqualIntA(a, ARCHIVE_FORMAT_ZIP, archive_format(a));

	/* Close the archive. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_format_zip_winzip_aes128)
{
	/* WinZip AES-128 encryption. */
	test_winzip_aes("test_read_format_zip_winzip_aes128.zip", 1);
}

DEFINE_TEST(test_read_format_zip_winzip_aes256)
{
	/* WinZip AES-256 encryption. */
	test_winzip_aes("test_read_format_zip_winzip_aes256.zip", 1);
}

DEFINE_TEST(test_read_format_zip_winzip_aes256_stored)
{
	/* WinZip AES-256 encryption with stored data. */
	test_winzip_aes("test_read_format_zip_winzip_aes256_stored.zip", 0);
}
