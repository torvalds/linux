/*-
 * Copyright (c) 2009-2011 Michihiro NAKAJIMA
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

/*
 * Check that an "empty" ISO 9660 image is correctly created.
 */

static const unsigned char primary_id[] = {
    0x01, 0x43, 0x44, 0x30, 0x30, 0x31, 0x01, 0x00
};
static const unsigned char volumesize[] = {
    0xb5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb5
};
static const unsigned char volumeidu16[] = {
    0x00, 0x43, 0x00, 0x44, 0x00, 0x52, 0x00, 0x4f,
    0x00, 0x4d, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20,
    0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20,
    0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20
};
static const unsigned char supplementary_id[] = {
    0x02, 0x43, 0x44, 0x30, 0x30, 0x31, 0x01, 0x00
};
static const unsigned char terminator_id[] = {
    0xff, 0x43, 0x44, 0x30, 0x30, 0x31, 0x01, 0x00
};

DEFINE_TEST(test_write_format_iso9660_empty)
{
	struct archive *a;
	struct archive_entry *ae;
	unsigned char *buff;
	size_t buffsize = 190 * 2048;
	size_t used;
	unsigned int i;

	buff = malloc(buffsize);
	assert(buff != NULL);
	if (buff == NULL)
		return;

	/* ISO9660 format: Create a new archive in memory. */
	assert((a = archive_write_new()) != NULL);
	assertA(0 == archive_write_set_format_iso9660(a));
	assertA(0 == archive_write_add_filter_none(a));
	assertA(0 == archive_write_set_bytes_per_block(a, 1));
	assertA(0 == archive_write_set_bytes_in_last_block(a, 1));
	assertA(0 == archive_write_open_memory(a, buff, buffsize, &used));

	/* Add "." entry which must be ignored. */ 
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 0);
	archive_entry_set_ctime(ae, 4, 0);
	archive_entry_set_mtime(ae, 5, 0);
	archive_entry_copy_pathname(ae, ".");
	archive_entry_set_mode(ae, S_IFDIR | 0755);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);

	/* Add ".." entry which must be ignored. */ 
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 0);
	archive_entry_set_ctime(ae, 4, 0);
	archive_entry_set_mtime(ae, 5, 0);
	archive_entry_copy_pathname(ae, "..");
	archive_entry_set_mode(ae, S_IFDIR | 0755);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);

	/* Add "/" entry which must be ignored. */ 
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 0);
	archive_entry_set_ctime(ae, 4, 0);
	archive_entry_set_mtime(ae, 5, 0);
	archive_entry_copy_pathname(ae, "/");
	archive_entry_set_mode(ae, S_IFDIR | 0755);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);

	/* Add "../" entry which must be ignored. */ 
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 0);
	archive_entry_set_ctime(ae, 4, 0);
	archive_entry_set_mtime(ae, 5, 0);
	archive_entry_copy_pathname(ae, "../");
	archive_entry_set_mode(ae, S_IFDIR | 0755);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);

	/* Add "../../." entry which must be ignored. */ 
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 0);
	archive_entry_set_ctime(ae, 4, 0);
	archive_entry_set_mtime(ae, 5, 0);
	archive_entry_copy_pathname(ae, "../../.");
	archive_entry_set_mode(ae, S_IFDIR | 0755);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);

	/* Add "..//.././" entry which must be ignored. */ 
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 0);
	archive_entry_set_ctime(ae, 4, 0);
	archive_entry_set_mtime(ae, 5, 0);
	archive_entry_copy_pathname(ae, "..//.././");
	archive_entry_set_mode(ae, S_IFDIR | 0755);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);

	/* Close out the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_free(a));

	assert(used == 2048 * 181);
	/* Check System Area. */
	for (i = 0; i < 2048 * 16; i++) {
		failure("System Area should be all nulls.");
		assert(buff[i] == 0);
	}

	/* Primary Volume. */
	failure("Primary Volume Descriptor should be in 16 Logical Sector.");
	assertEqualMem(buff+2048*16, primary_id, 8);
	assertEqualMem(buff+2048*16+0x28,
	    "CDROM                           ", 32);
	assertEqualMem(buff+2048*16+0x50, volumesize, 8);

	/* Supplementary Volume. */
	failure("Supplementary Volume(Joliet) Descriptor "
	    "should be in 17 Logical Sector.");
	assertEqualMem(buff+2048*17, supplementary_id, 8);
	assertEqualMem(buff+2048*17+0x28, volumeidu16, 32);
	assertEqualMem(buff+2048*17+0x50, volumesize, 8);
	failure("Date and Time of Primary Volume and "
	    "Date and Time of Supplementary Volume "
	    "must be the same.");
	assertEqualMem(buff+2048*16+0x32d, buff+2048*17+0x32d, 0x44);

	/* Terminator. */
	failure("Volume Descriptor Set Terminator "
	    "should be in 18 Logical Sector.");
	assertEqualMem(buff+2048*18, terminator_id, 8);
	for (i = 8; i < 2048; i++) {
		failure("Body of Volume Descriptor Set Terminator "
		    "should be all nulls.");
		assert(buff[2048*18+i] == 0);
	}

	/* Padding data. */
	for (i = 0; i < 2048*150; i++) {
		failure("Padding data should be all nulls.");
		assert(buff[2048*31+i] == 0);
	}

	/*
	 * Read ISO image.
	 */
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, 0, archive_read_support_format_all(a));
	assertEqualIntA(a, 0, archive_read_support_filter_all(a));
	assertEqualIntA(a, 0, archive_read_open_memory(a, buff, used));

	/*
	 * Read Root Directory
	 * Root Directory entry must be in ISO image.
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(archive_entry_atime(ae), archive_entry_ctime(ae));
	assertEqualInt(archive_entry_atime(ae), archive_entry_mtime(ae));
	assertEqualString(".", archive_entry_pathname(ae));
	assert((S_IFDIR | 0555) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Verify the end of the archive.
	 */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_free(a));

	free(buff);
}
