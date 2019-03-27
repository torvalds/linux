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

DEFINE_TEST(test_read_pax_truncated)
{
	struct archive_entry *ae;
	struct archive *a;
	size_t used, i, buff_size = 1000000;
	size_t filedata_size = 100000;
	char *buff = malloc(buff_size);
	char *buff2 = malloc(buff_size);
	char *filedata = malloc(filedata_size);

	/* Create a new archive in memory. */
	assert((a = archive_write_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_set_format_pax(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_add_filter_none(a));
	assertEqualIntA(a, ARCHIVE_OK,
			archive_write_open_memory(a, buff, buff_size, &used));

	/*
	 * Write a file to it.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "file");
	archive_entry_set_mode(ae, S_IFREG | 0755);
	for (i = 0; i < filedata_size; i++)
		filedata[i] = (unsigned char)rand();
	archive_entry_set_atime(ae, 1, 2);
	archive_entry_set_ctime(ae, 3, 4);
	archive_entry_set_mtime(ae, 5, 6);
	archive_entry_set_size(ae, filedata_size);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualIntA(a, (int)filedata_size, 
	    (int)archive_write_data(a, filedata, filedata_size));

	/* Close out the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* Now, read back a truncated version of the archive and
	 * verify that we get an appropriate error. */
	for (i = 1; i < used + 100; i += 100) {
		assert((a = archive_read_new()) != NULL);
		assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
		assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
		/* If it's truncated very early, the file type detection should fail. */
		if (i < 512) {
			assertEqualIntA(a, ARCHIVE_FATAL, read_open_memory_minimal(a, buff, i, 13));
			goto wrap_up;
		} else {
			assertEqualIntA(a, ARCHIVE_OK, read_open_memory_minimal(a, buff, i, 13));
		}

		/* If it's truncated in a header, the header read should fail. */
		if (i < 1536) {
			assertEqualIntA(a, ARCHIVE_FATAL, archive_read_next_header(a, &ae));
			goto wrap_up;
		} else {
			failure("Archive truncated to %d bytes", i);
			assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
		}

		/* If it's truncated in the body, the body read should fail. */
		if (i < 1536 + filedata_size) {
			assertEqualIntA(a, ARCHIVE_FATAL, archive_read_data(a, filedata, filedata_size));
			goto wrap_up;
		} else {
			failure("Archive truncated to %d bytes", i);
			assertEqualIntA(a, filedata_size,
			    archive_read_data(a, filedata, filedata_size));
		}

		/* Verify the end of the archive. */
		/* Archive must be long enough to capture a 512-byte
		 * block of zeroes after the entry.  (POSIX requires a
		 * second block of zeros to be written but libarchive
		 * does not return an error if it can't consume
		 * it.) */
		if (i < 1536 + 512*((filedata_size + 511)/512) + 512) {
			failure("i=%d minsize=%d", i,
			    1536 + 512*((filedata_size + 511)/512) + 512);
			assertEqualIntA(a, ARCHIVE_FATAL,
			    archive_read_next_header(a, &ae));
		} else {
			assertEqualIntA(a, ARCHIVE_EOF,
			    archive_read_next_header(a, &ae));
		}
	wrap_up:
		assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
		assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	}



	/* Same as above, except skip the body instead of reading it. */
	for (i = 1; i < used + 100; i += 100) {
		assert((a = archive_read_new()) != NULL);
		assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
		assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
		/* If it's truncated very early, file type detection should fail. */
		if (i < 512) {
			assertEqualIntA(a, ARCHIVE_FATAL, read_open_memory(a, buff, i, 7));
			goto wrap_up2;
		} else {
			assertEqualIntA(a, ARCHIVE_OK, read_open_memory(a, buff, i, 7));
		}

		if (i < 1536) {
			assertEqualIntA(a, ARCHIVE_FATAL, archive_read_next_header(a, &ae));
			goto wrap_up2;
		} else {
			assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
		}

		if (i < 1536 + 512*((filedata_size+511)/512)) {
			assertEqualIntA(a, ARCHIVE_FATAL, archive_read_data_skip(a));
			goto wrap_up2;
		} else {
			assertEqualIntA(a, ARCHIVE_OK, archive_read_data_skip(a));
		}

		/* Verify the end of the archive. */
		/* Archive must be long enough to capture a 512-byte
		 * block of zeroes after the entry.  (POSIX requires a
		 * second block of zeros to be written but libarchive
		 * does not return an error if it can't consume
		 * it.) */
		if (i < 1536 + 512*((filedata_size + 511)/512) + 512) {
			assertEqualIntA(a, ARCHIVE_FATAL,
			    archive_read_next_header(a, &ae));
		} else {
			assertEqualIntA(a, ARCHIVE_EOF,
			    archive_read_next_header(a, &ae));
		}
	wrap_up2:
		assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
		assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	}

	/* Now, damage the archive in various ways and test the responses. */

	/* Damage the first size field in the pax attributes. */
	memcpy(buff2, buff, buff_size);
	buff2[512] = '9';
	buff2[513] = '9';
	buff2[514] = 'A'; /* Non-digit in size. */
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_memory(a, buff2, used));
	assertEqualIntA(a, ARCHIVE_WARN, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/* Damage the size field in the pax attributes. */
	memcpy(buff2, buff, buff_size);
	buff2[512] = 'A'; /* First character not a digit. */
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_memory(a, buff2, used));
	assertEqualIntA(a, ARCHIVE_WARN, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/* Damage the size field in the pax attributes. */
	memcpy(buff2, buff, buff_size);
	for (i = 512; i < 520; i++) /* Size over 999999. */
		buff2[i] = '9';
	buff2[i] = ' ';
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_memory(a, buff2, used));
	assertEqualIntA(a, ARCHIVE_WARN, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/* Damage the size field in the pax attributes. */
	memcpy(buff2, buff, buff_size);
	buff2[512] = '9'; /* Valid format, but larger than attribute area. */
	buff2[513] = '9';
	buff2[514] = '9';
	buff2[515] = ' ';
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_memory(a, buff2, used));
	assertEqualIntA(a, ARCHIVE_WARN, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/* Damage the size field in the pax attributes. */
	memcpy(buff2, buff, buff_size);
	buff2[512] = '1'; /* Too small. */
	buff2[513] = ' ';
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_memory(a, buff2, used));
	assertEqualIntA(a, ARCHIVE_WARN, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/* Damage the size field in the pax attributes. */
	memcpy(buff2, buff, buff_size);
	buff2[512] = ' '; /* No size given. */
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_memory(a, buff2, used));
	assertEqualIntA(a, ARCHIVE_WARN, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/* Damage the ustar header. */
	memcpy(buff2, buff, buff_size);
	buff2[1024]++; /* Break the checksum. */
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_memory(a, buff2, used));
	assertEqualIntA(a, ARCHIVE_FATAL, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/*
	 * TODO: Damage the ustar header in various ways and fixup the
	 * checksum in order to test boundary cases in the innermost
	 * ustar header parsing.
	 */

	free(buff);
	free(buff2);
	free(filedata);
}
