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
 * Check that a "zisofs" ISO 9660 image is correctly created.
 */

static const unsigned char primary_id[] = {
    0x01, 0x43, 0x44, 0x30, 0x30, 0x31, 0x01, 0x00
};
static const unsigned char volumesize[] = {
    0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x23
};
static const unsigned char volumesize2[] = {
    0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36
};
static const unsigned char volumesize3[] = {
    0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28
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

static const unsigned char zisofs_magic[8] = {
    0x37, 0xE4, 0x53, 0x96, 0xC9, 0xDB, 0xD6, 0x07
};

static const unsigned char zisofs_data[24] = {
    0x37, 0xe4, 0x53, 0x96, 0xc9, 0xdb, 0xd6, 0x07,
    0x00, 0x80, 0x00, 0x00, 0x04, 0x0f, 0x00, 0x00,
    0x18, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00
};

static const unsigned char boot_id[] = {
    0x00, 0x43, 0x44, 0x30, 0x30, 0x31, 0x01, 0x45,
    0x4c, 0x20, 0x54, 0x4f, 0x52, 0x49, 0x54, 0x4f,
    0x20, 0x53, 0x50, 0x45, 0x43, 0x49, 0x46, 0x49,
    0x43, 0x41, 0x54, 0x49, 0x4f, 0x4e, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const unsigned char boot_catalog[] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xaa, 0x55, 0x55, 0xaa,
    0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
    0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const unsigned char el_torito_signature[] = {
    "ER\355\001\012T\207\001RRIP_1991ATHE ROCK RIDGE "
    "INTERCHANGE PROTOCOL PROVIDES SUPPORT FOR POSIX "
    "FILE SYSTEM SEMANTICSPLEASE CONTACT DISC PUBLISHER "
    "FOR SPECIFICATION SOURCE.  SEE PUBLISHER IDENTIFIER "
    "IN PRIMARY VOLUME DESCRIPTOR FOR CONTACT INFORMATION."
};

static void
test_write_format_iso9660_zisofs_1(void)
{
	unsigned char buff2[1024];
	unsigned char nullb[1024];
	struct archive *a;
	struct archive_entry *ae;
	unsigned char *buff;
	size_t buffsize = 36 * 2048;
	size_t used;
	unsigned int i;
	int r;

	memset(nullb, 0, sizeof(nullb));
	buff = malloc(buffsize);
	assert(buff != NULL);
	if (buff == NULL)
		return;

	/* ISO9660 format: Create a new archive in memory. */
	assert((a = archive_write_new()) != NULL);
	assertEqualIntA(a, 0, archive_write_set_format_iso9660(a));
	assertEqualIntA(a, 0, archive_write_add_filter_none(a));
	r = archive_write_set_option(a, NULL, "zisofs", "1");
	if (r == ARCHIVE_FATAL) {
		skipping("zisofs option not supported on this platform");
		assertEqualInt(ARCHIVE_OK, archive_write_free(a));
		free(buff);
		return;
	}
	assertEqualIntA(a, 0, archive_write_set_option(a, NULL, "pad", NULL));
	assertEqualIntA(a, 0, archive_write_open_memory(a, buff, buffsize, &used));

	/*
	 * "file1" has a bunch of attributes and 256K bytes of null data.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 20);
	archive_entry_set_birthtime(ae, 3, 30);
	archive_entry_set_ctime(ae, 4, 40);
	archive_entry_set_mtime(ae, 5, 50);
	archive_entry_copy_pathname(ae, "file1");
	archive_entry_set_mode(ae, S_IFREG | 0755);
	archive_entry_set_size(ae, 256*1024);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualIntA(a, 1024, archive_write_data(a, nullb, 1024));

	/*
	 * "file2" has a bunch of attributes and 2048 bytes of null data.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 20);
	archive_entry_set_birthtime(ae, 3, 30);
	archive_entry_set_ctime(ae, 4, 40);
	archive_entry_set_mtime(ae, 5, 50);
	archive_entry_copy_pathname(ae, "file2");
	archive_entry_set_mode(ae, S_IFREG | 0755);
	archive_entry_set_size(ae, 2048);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualIntA(a, 1024, archive_write_data(a, nullb, 1024));

	/*
	 * "file3" has a bunch of attributes and 2049 bytes of null data.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 20);
	archive_entry_set_birthtime(ae, 3, 30);
	archive_entry_set_ctime(ae, 4, 40);
	archive_entry_set_mtime(ae, 5, 50);
	archive_entry_copy_pathname(ae, "file3");
	archive_entry_set_mode(ae, S_IFREG | 0755);
	archive_entry_set_size(ae, 2049);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualIntA(a, 1024, archive_write_data(a, nullb, 1024));

	/*
	 * "file4" has a bunch of attributes and 24 bytes of zisofs data
	 * which is compressed from 32K bytes null data.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 20);
	archive_entry_set_birthtime(ae, 3, 30);
	archive_entry_set_ctime(ae, 4, 40);
	archive_entry_set_mtime(ae, 5, 50);
	archive_entry_copy_pathname(ae, "file4");
	archive_entry_set_mode(ae, S_IFREG | 0755);
	archive_entry_set_size(ae, 24);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualIntA(a, 24, archive_write_data(a, zisofs_data, 24));

	/* Close out the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_free(a));

	failure("The ISO image size should be 71680 bytes.");
	assertEqualInt(used, 2048 * 35);

	/* Check System Area. */
	for (i = 0; i < 2048 * 16; i++) {
		failure("System Area should be all nulls.");
		assertEqualInt(buff[i], 0);
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
		assertEqualInt(buff[2048*18+i], 0);
	}

	/* "file1"  Contents is zisofs data. */
	failure("file1 image should be zisofs'ed.");
	assertEqualMem(buff+2048*31, zisofs_magic, 8);
	/* "file2"  Contents is not zisofs data. */
	failure("file2 image should not be zisofs'ed.");
	assertEqualMem(buff+2048*32, nullb, 8);
	/* "file3"  Contents is zisofs data. */
	failure("file3 image should be zisofs'ed.");
	assertEqualMem(buff+2048*33, zisofs_magic, 8);
	/* "file4"  Contents is zisofs data. */
	failure("file4 image should be zisofs'ed.");
	assertEqualMem(buff+2048*34, zisofs_magic, 8);

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
	 * Read "file1" which has 256K bytes null data.
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	/* assertEqualInt(3, archive_entry_birthtime(ae)); */
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("file1", archive_entry_pathname(ae));
	assert((S_IFREG | 0555) == archive_entry_mode(ae));
	assertEqualInt(256*1024, archive_entry_size(ae));
	assertEqualIntA(a, 1024, archive_read_data(a, buff2, 1024));
	assertEqualMem(buff2, nullb, 1024);

	/*
	 * Read "file2" which has 2048 bytes null data.
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	/* assertEqualInt(3, archive_entry_birthtime(ae)); */
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("file2", archive_entry_pathname(ae));
	assert((S_IFREG | 0555) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));
	assertEqualIntA(a, 1024, archive_read_data(a, buff2, 1024));
	assertEqualMem(buff2, nullb, 1024);

	/*
	 * Read "file3" which has 2049 bytes null data.
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	/* assertEqualInt(3, archive_entry_birthtime(ae)); */
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("file3", archive_entry_pathname(ae));
	assert((S_IFREG | 0555) == archive_entry_mode(ae));
	assertEqualInt(2049, archive_entry_size(ae));
	assertEqualIntA(a, 1024, archive_read_data(a, buff2, 1024));
	assertEqualMem(buff2, nullb, 1024);

	/*
	 * Read "file4" which has 32K bytes null data.
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	/* assertEqualInt(3, archive_entry_birthtime(ae)); */
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("file4", archive_entry_pathname(ae));
	assert((S_IFREG | 0555) == archive_entry_mode(ae));
	assertEqualInt(32768, archive_entry_size(ae));
	assertEqualIntA(a, 1024, archive_read_data(a, buff2, 1024));
	assertEqualMem(buff2, nullb, 1024);

	/*
	 * Verify the end of the archive.
	 */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_free(a));

	free(buff);
}

static void
test_write_format_iso9660_zisofs_2(void)
{
	unsigned char buff2[1024];
	unsigned char data[1024];
	struct archive *a;
	struct archive_entry *ae;
	unsigned char *buff;
	size_t buffsize = 60 * 2048;
	size_t used;
	unsigned int i;
	int r;

	buff = malloc(buffsize);
	assert(buff != NULL);
	if (buff == NULL)
		return;

	/* ISO9660 format: Create a new archive in memory. */
	assert((a = archive_write_new()) != NULL);
	assertEqualIntA(a, 0, archive_write_set_format_iso9660(a));
	assertEqualIntA(a, 0, archive_write_add_filter_none(a));
	r = archive_write_set_option(a, NULL, "zisofs", "1");
	if (r == ARCHIVE_FATAL) {
		skipping("zisofs option not supported on this platform");
		assertEqualInt(ARCHIVE_OK, archive_write_free(a));
		free(buff);
		return;
	}
	assertEqualIntA(a, 0, archive_write_set_option(a, NULL, "pad", NULL));
	assertEqualIntA(a, 0, archive_write_open_memory(a, buff, buffsize, &used));

	/*
	 * "file1" has a bunch of attributes and 256K bytes of random data.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 20);
	archive_entry_set_birthtime(ae, 3, 30);
	archive_entry_set_ctime(ae, 4, 40);
	archive_entry_set_mtime(ae, 5, 50);
	archive_entry_copy_pathname(ae, "file1");
	archive_entry_set_mode(ae, S_IFREG | 0755);
	archive_entry_set_size(ae, 256*1024);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	for (i = 0; i < 256; i++) {
		int j;
		if (i == 0) {
			for (j = 0; j < (int)sizeof(data); j++)
				data[j] = (i^j) & 0xff;
		} else {
			for (j = 0; j < (int)sizeof(data); j++)
				data[j] ^= i+j;
		}
		assertEqualIntA(a, 1024, archive_write_data(a, data, 1024));
	}

	/*
	 * "file2" has a bunch of attributes and 2048 bytes data.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 20);
	archive_entry_set_birthtime(ae, 3, 30);
	archive_entry_set_ctime(ae, 4, 40);
	archive_entry_set_mtime(ae, 5, 50);
	archive_entry_copy_pathname(ae, "file2");
	archive_entry_set_mode(ae, S_IFREG | 0755);
	archive_entry_set_size(ae, 2048);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	memset(data, 'a', sizeof(data));
	assertEqualIntA(a, 1024, archive_write_data(a, data, 1024));
	memset(data, 'b', sizeof(data));
	assertEqualIntA(a, 1024, archive_write_data(a, data, 1024));

	/*
	 * "file3" has a bunch of attributes and 1024 bytes of 'Z'
	 *  + 1025 bytes of null data.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 20);
	archive_entry_set_birthtime(ae, 3, 30);
	archive_entry_set_ctime(ae, 4, 40);
	archive_entry_set_mtime(ae, 5, 50);
	archive_entry_copy_pathname(ae, "file3");
	archive_entry_set_mode(ae, S_IFREG | 0755);
	archive_entry_set_size(ae, 2049);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	memset(data, 'Z', sizeof(data));
	assertEqualIntA(a, 1024, archive_write_data(a, data, 1024));

	/*
	 * "file4" has a bunch of attributes and 24 bytes of zisofs data
	 * which is compressed from 32K bytes null data.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 20);
	archive_entry_set_birthtime(ae, 3, 30);
	archive_entry_set_ctime(ae, 4, 40);
	archive_entry_set_mtime(ae, 5, 50);
	archive_entry_copy_pathname(ae, "file4");
	archive_entry_set_mode(ae, S_IFREG | 0755);
	archive_entry_set_size(ae, 24);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualIntA(a, 24, archive_write_data(a, zisofs_data, 24));

	/* Close out the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_free(a));

	failure("The ISO image size should be 110592 bytes.");
	assertEqualInt(used, 2048 * 54);

	/* Check System Area. */
	for (i = 0; i < 2048 * 16; i++) {
		failure("System Area should be all nulls.");
		assertEqualInt(buff[i], 0);
	}

	/* Primary Volume. */
	failure("Primary Volume Descriptor should be in 16 Logical Sector.");
	assertEqualMem(buff+2048*16, primary_id, 8);
	assertEqualMem(buff+2048*16+0x28,
	    "CDROM                           ", 32);
	assertEqualMem(buff+2048*16+0x50, volumesize2, 8);

	/* Supplementary Volume. */
	failure("Supplementary Volume(Joliet) Descriptor "
	    "should be in 17 Logical Sector.");
	assertEqualMem(buff+2048*17, supplementary_id, 8);
	assertEqualMem(buff+2048*17+0x28, volumeidu16, 32);
	assertEqualMem(buff+2048*17+0x50, volumesize2, 8);
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
		assertEqualInt(buff[2048*18+i], 0);
	}

	/* "file1"  Contents is zisofs data. */
	failure("file1 image should be zisofs'ed.");
	assertEqualMem(buff+2048*31, zisofs_magic, 8);
	/* "file2"  Contents is not zisofs data. */
	memset(data, 'a', sizeof(data));
	failure("file2 image should not be zisofs'ed.");
	assertEqualMem(buff+2048*51, data, 1024);
	memset(data, 'b', sizeof(data));
	failure("file2 image should not be zisofs'ed.");
	assertEqualMem(buff+2048*51+1024, data, 1024);
	/* "file3"  Contents is zisofs data. */
	failure("file3 image should be zisofs'ed.");
	assertEqualMem(buff+2048*52, zisofs_magic, 8);
	/* "file4"  Contents is zisofs data. */
	failure("file4 image should be zisofs'ed.");
	assertEqualMem(buff+2048*53, zisofs_magic, 8);

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
	 * Read "file1" which has 256K bytes random data.
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("file1", archive_entry_pathname(ae));
	assert((S_IFREG | 0555) == archive_entry_mode(ae));
	assertEqualInt(256*1024, archive_entry_size(ae));
	assertEqualIntA(a, 1024, archive_read_data(a, buff2, 1024));

	/*
	 * Read "file2" which has 2048 bytes data.
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("file2", archive_entry_pathname(ae));
	assert((S_IFREG | 0555) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));
	assertEqualIntA(a, 1024, archive_read_data(a, buff2, 1024));
	memset(data, 'a', sizeof(data));
	assertEqualMem(buff2, data, 1024);

	/*
	 * Read "file3" which has 2049 bytes data.
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("file3", archive_entry_pathname(ae));
	assert((S_IFREG | 0555) == archive_entry_mode(ae));
	assertEqualInt(2049, archive_entry_size(ae));
	assertEqualIntA(a, 1024, archive_read_data(a, buff2, 1024));
	memset(data, 'Z', sizeof(data));
	assertEqualMem(buff2, data, 1024);

	/*
	 * Read "file4" which has 32K bytes null data.
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("file4", archive_entry_pathname(ae));
	assert((S_IFREG | 0555) == archive_entry_mode(ae));
	assertEqualInt(32768, archive_entry_size(ae));
	assertEqualIntA(a, 1024, archive_read_data(a, buff2, 1024));
	memset(data, 0, sizeof(data));
	assertEqualMem(buff2, data, 1024);

	/*
	 * Verify the end of the archive.
	 */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_free(a));

	free(buff);
}

/*
 * Make a bootable ISO image with "zisofs" option.
 */
static void
test_write_format_iso9660_zisofs_3(void)
{
	unsigned char buff2[1024];
	unsigned char nullb[2048];
	struct archive *a;
	struct archive_entry *ae;
	unsigned char *buff;
	size_t buffsize = 50 * 2048;
	size_t used;
	unsigned int i;
	int r;

	memset(nullb, 0, sizeof(nullb));
	buff = malloc(buffsize);
	assert(buff != NULL);
	if (buff == NULL)
		return;

	/* ISO9660 format: Create a new archive in memory. */
	assert((a = archive_write_new()) != NULL);
	assertEqualIntA(a, 0, archive_write_set_format_iso9660(a));
	assertEqualIntA(a, 0, archive_write_add_filter_none(a));
	r = archive_write_set_option(a, NULL, "zisofs", "1");
	if (r == ARCHIVE_FATAL) {
		skipping("zisofs option not supported on this platform");
		assertEqualInt(ARCHIVE_OK, archive_write_free(a));
		free(buff);
		return;
	}
	assertEqualIntA(a, 0, archive_write_set_option(a, NULL, "boot", "boot.img"));
	assertEqualIntA(a, 0, archive_write_set_option(a, NULL, "pad", NULL));
	assertEqualIntA(a, 0, archive_write_open_memory(a, buff, buffsize, &used));

	/*
	 * "file1" has a bunch of attributes and 256K bytes of null data.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 20);
	archive_entry_set_birthtime(ae, 3, 30);
	archive_entry_set_ctime(ae, 4, 40);
	archive_entry_set_mtime(ae, 5, 50);
	archive_entry_copy_pathname(ae, "boot.img");
	archive_entry_set_mode(ae, S_IFREG | 0755);
	archive_entry_set_size(ae, 10*1024);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualIntA(a, 1024, archive_write_data(a, nullb, 1024));

	/*
	 * "file2" has a bunch of attributes and 2048 bytes of null data.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 20);
	archive_entry_set_birthtime(ae, 3, 30);
	archive_entry_set_ctime(ae, 4, 40);
	archive_entry_set_mtime(ae, 5, 50);
	archive_entry_copy_pathname(ae, "file2");
	archive_entry_set_mode(ae, S_IFREG | 0755);
	archive_entry_set_size(ae, 2048);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualIntA(a, 1024, archive_write_data(a, nullb, 1024));

	/*
	 * "file3" has a bunch of attributes and 2049 bytes of null data.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 20);
	archive_entry_set_birthtime(ae, 3, 30);
	archive_entry_set_ctime(ae, 4, 40);
	archive_entry_set_mtime(ae, 5, 50);
	archive_entry_copy_pathname(ae, "file3");
	archive_entry_set_mode(ae, S_IFREG | 0755);
	archive_entry_set_size(ae, 2049);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualIntA(a, 1024, archive_write_data(a, nullb, 1024));

	/* Close out the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_free(a));

	failure("The ISO image size should be 81920 bytes.");
	assertEqualInt(used, 2048 * 40);

	/* Check System Area. */
	for (i = 0; i < 2048 * 16; i++) {
		failure("System Area should be all nulls.");
		assertEqualInt(buff[i], 0);
	}

	/* Primary Volume. */
	failure("Primary Volume Descriptor should be in 16 Logical Sector.");
	assertEqualMem(buff+2048*16, primary_id, 8);
	assertEqualMem(buff+2048*16+0x28,
	    "CDROM                           ", 32);
	assertEqualMem(buff+2048*16+0x50, volumesize3, 8);

	/* Boot Volume. */
	failure("Boot Volume Descriptor should be in 17 Logical Sector.");
	assertEqualMem(buff+2048*17, boot_id, sizeof(boot_id));
	for (i = 0x27; i <= 0x46; i++) {
		failure("Unused area must be all nulls.");
		assert(buff[2048*17+i] == 0);
	}
	/* First sector of Boot Catalog. */
	assert(buff[2048*17+0x47] == 0x20);
	assert(buff[2048*17+0x48] == 0x00);
	assert(buff[2048*17+0x49] == 0x00);
	assert(buff[2048*17+0x4a] == 0x00);
	for (i = 0x4a; i <= 0x7ff; i++) {
		failure("Unused area must be all nulls.");
		assert(buff[2048*17+i] == 0);
	}

	/* Supplementary Volume. */
	failure("Supplementary Volume(Joliet) Descriptor "
	    "should be in 18 Logical Sector.");
	assertEqualMem(buff+2048*18, supplementary_id, 8);
	assertEqualMem(buff+2048*18+0x28, volumeidu16, 32);
	assertEqualMem(buff+2048*18+0x50, volumesize3, 8);
	failure("Date and Time of Primary Volume and "
	    "Date and Time of Supplementary Volume "
	    "must be the same.");
	assertEqualMem(buff+2048*16+0x32d, buff+2048*18+0x32d, 0x44);

	/* Terminator. */
	failure("Volume Descriptor Set Terminator "
	    "should be in 19 Logical Sector.");
	assertEqualMem(buff+2048*19, terminator_id, 8);
	for (i = 8; i < 2048; i++) {
		failure("Body of Volume Descriptor Set Terminator "
		    "should be all nulls.");
		assertEqualInt(buff[2048*19+i], 0);
	}

	/* Check signature of El-Torito. */
	assertEqualMem(buff+2048*31, el_torito_signature, 237);
	assertEqualMem(buff+2048*31+237, nullb, 2048-237);

	/* Check contents of "boot.catalog". */
	assertEqualMem(buff+2048*32, boot_catalog, 64);
	assertEqualMem(buff+2048*32+64, nullb, 2048-64);

	/* Check contents of "boot.img". */
	failure("boot.img image should not be zisofs'ed.");
	assertEqualMem(buff+2048*33, nullb, 2048);
	for (i = 2048*34; i < 2048*38; i += 2048) {
		assertEqualMem(buff+i, nullb, 2048);
	}

	/* "file2"  Contents is not zisofs data. */
	failure("file2 image should not be zisofs'ed.");
	assertEqualMem(buff+2048*38, nullb, 8);
	/* "file3"  Contents is zisofs data. */
	failure("file3 image should be zisofs'ed.");
	assertEqualMem(buff+2048*39, zisofs_magic, 8);

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
	 * Read "boot.catalog".
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualString("boot.catalog", archive_entry_pathname(ae));
#if !defined(_WIN32) && !defined(__CYGWIN__)
	assert((S_IFREG | 0444) == archive_entry_mode(ae));
#else
	/* On Windows and CYGWIN, always set all exec bit ON by default. */ 
	assert((S_IFREG | 0555) == archive_entry_mode(ae));
#endif
	assertEqualInt(1, archive_entry_nlink(ae));
	assertEqualInt(2*1024, archive_entry_size(ae));
	assertEqualIntA(a, 1024, archive_read_data(a, buff2, 1024));
	assertEqualMem(buff2, boot_catalog, 64);

	/*
	 * Read "boot.img".
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(3, archive_entry_birthtime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("boot.img", archive_entry_pathname(ae));
	assert((S_IFREG | 0555) == archive_entry_mode(ae));
	assertEqualInt(1, archive_entry_nlink(ae));
	assertEqualInt(10*1024, archive_entry_size(ae));
	assertEqualIntA(a, 1024, archive_read_data(a, buff2, 1024));
	assertEqualMem(buff2, nullb, 1024);

	/*
	 * Read "file2" which has 2048 bytes null data.
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("file2", archive_entry_pathname(ae));
	assert((S_IFREG | 0555) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));
	assertEqualIntA(a, 1024, archive_read_data(a, buff2, 1024));
	assertEqualMem(buff2, nullb, 1024);

	/*
	 * Read "file3" which has 2049 bytes null data.
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("file3", archive_entry_pathname(ae));
	assert((S_IFREG | 0555) == archive_entry_mode(ae));
	assertEqualInt(2049, archive_entry_size(ae));
	assertEqualIntA(a, 1024, archive_read_data(a, buff2, 1024));
	assertEqualMem(buff2, nullb, 1024);

	/*
	 * Verify the end of the archive.
	 */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_free(a));

	free(buff);
}

DEFINE_TEST(test_write_format_iso9660_zisofs)
{
	test_write_format_iso9660_zisofs_1();
	test_write_format_iso9660_zisofs_2();
	test_write_format_iso9660_zisofs_3();
}
