/*-
 * Copyright (c) 2009 Michihiro NAKAJIMA
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
 * Check that a "bootable CD" ISO 9660 image is correctly created.
 */

static const unsigned char primary_id[] = {
    0x01, 0x43, 0x44, 0x30, 0x30, 0x31, 0x01, 0x00
};
static const unsigned char volumesize[] = {
    0x26, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x26
};
static const unsigned char volumeidu16[] = {
    0x00, 0x43, 0x00, 0x44, 0x00, 0x52, 0x00, 0x4f,
    0x00, 0x4d, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20,
    0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20,
    0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20
};
static const unsigned char boot_id[] = {
    0x00, 0x43, 0x44, 0x30, 0x30, 0x31, 0x01, 0x45,
    0x4c, 0x20, 0x54, 0x4f, 0x52, 0x49, 0x54, 0x4f,
    0x20, 0x53, 0x50, 0x45, 0x43, 0x49, 0x46, 0x49,
    0x43, 0x41, 0x54, 0x49, 0x4f, 0x4e, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const unsigned char supplementary_id[] = {
    0x02, 0x43, 0x44, 0x30, 0x30, 0x31, 0x01, 0x00
};
static const unsigned char terminator_id[] = {
    0xff, 0x43, 0x44, 0x30, 0x30, 0x31, 0x01, 0x00
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

static const unsigned char info_table[] = {
    0x10, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00,
    0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
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

static char buff2[1024];

static void
_test_write_format_iso9660_boot(int write_info_tbl)
{
	unsigned char nullb[2048];
	struct archive *a;
	struct archive_entry *ae;
	unsigned char *buff;
	size_t buffsize = 39 * 2048;
	size_t used;
	unsigned int i;

	memset(nullb, 0, sizeof(nullb));
	buff = malloc(buffsize);
	assert(buff != NULL);

	/* ISO9660 format: Create a new archive in memory. */
	assert((a = archive_write_new()) != NULL);
	assertA(0 == archive_write_set_format_iso9660(a));
	assertA(0 == archive_write_add_filter_none(a));
	assertA(0 == archive_write_set_option(a, NULL, "boot", "boot.img"));
	if (write_info_tbl)
		assertA(0 == archive_write_set_option(a, NULL, "boot-info-table", "1"));
	assertA(0 == archive_write_set_option(a, NULL, "pad", NULL));
	assertA(0 == archive_write_open_memory(a, buff, buffsize, &used));

	/*
	 * "boot.img" has a bunch of attributes and 10K bytes of null data.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 20);
	archive_entry_set_birthtime(ae, 3, 30);
	archive_entry_set_ctime(ae, 4, 40);
	archive_entry_set_mtime(ae, 5, 50);
	archive_entry_copy_pathname(ae, "boot.img");
	archive_entry_set_mode(ae, S_IFREG | 0755);
	archive_entry_set_nlink(ae, 1);
	archive_entry_set_size(ae, 10*1024);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	for (i = 0; i < 10; i++)
		assertEqualIntA(a, 1024, archive_write_data(a, nullb, 1024));

	/* Close out the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_free(a));

	assert(used == 2048 * 38);
	/* Check System Area. */
	for (i = 0; i < 2048 * 16; i += 2048) {
		assertEqualMem(buff+i, nullb, 2048);
	}

	/* Primary Volume. */
	failure("Primary Volume Descriptor should be in 16 Logical Sector.");
	assertEqualMem(buff+2048*16, primary_id, 8);
	assertEqualMem(buff+2048*16+0x28,
	    "CDROM                           ", 32);
	assertEqualMem(buff+2048*16+0x50, volumesize, 8);

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
	assertEqualMem(buff+2048*18+0x50, volumesize, 8);
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
		assert(buff[2048*19+i] == 0);
	}

	/* Check signature of El-Torito. */
	assertEqualMem(buff+2048*31, el_torito_signature, 237);
	assertEqualMem(buff+2048*31+237, nullb, 2048-237);

	/* Check contents of "boot.catalog". */
	assertEqualMem(buff+2048*32, boot_catalog, 64);
	assertEqualMem(buff+2048*32+64, nullb, 2048-64);

	/* Check contents of "boot.img". */
	if (write_info_tbl) {
		assertEqualMem(buff+2048*33, nullb, 8);
		assertEqualMem(buff+2048*33+8, info_table, 56);
		assertEqualMem(buff+2048*33+64, nullb, 2048-64);
	} else {
		assertEqualMem(buff+2048*33, nullb, 2048);
	}
	for (i = 2048*34; i < 2048*38; i += 2048) {
		assertEqualMem(buff+i, nullb, 2048);
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
	if (write_info_tbl) {
		assertEqualMem(buff2, nullb, 8);
		assertEqualMem(buff2+8, info_table, 56);
		assertEqualMem(buff2+64, nullb, 1024-64);
	} else
		assertEqualMem(buff2, nullb, 1024);

	/*
	 * Verify the end of the archive.
	 */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_free(a));

	free(buff);
}

DEFINE_TEST(test_write_format_iso9660_boot)
{
	_test_write_format_iso9660_boot(0);
	/* Use 'boot-info-table' option. */
	_test_write_format_iso9660_boot(1);
}
