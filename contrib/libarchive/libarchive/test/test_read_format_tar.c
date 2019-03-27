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

/*
 * Each of these archives is a short archive with a single entry.  The
 * corresponding verify function verifies the entry structure returned
 * from libarchive is what it should be.  The support functions pad with
 * lots of zeros, so we can trim trailing zero bytes from each hardcoded
 * archive to save space.
 *
 * The naming here follows the tar file type flags.  E.g. '1' is a hardlink,
 * '2' is a symlink, '5' is a dir, etc.
 */

/* Empty archive. */
static unsigned char archiveEmpty[] = {
	/* 512 zero bytes */
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,

	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,

	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,

	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0
};

static void verifyEmpty(void)
{
	struct archive_entry *ae;
	struct archive *a;

	assert((a = archive_read_new()) != NULL);
	assertA(0 == archive_read_support_filter_all(a));
	assertA(0 == archive_read_support_format_all(a));
	assertA(0 == archive_read_open_memory(a, archiveEmpty, 512));
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualInt(archive_filter_code(a, 0), ARCHIVE_FILTER_NONE);
	assertEqualString(archive_filter_name(a, 0), "none");
	failure("512 zero bytes should be recognized as a tar archive.");
	assertEqualInt(archive_format(a), ARCHIVE_FORMAT_TAR);
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);

	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

/* Single entry with a hardlink. */
static unsigned char archive1[] = {
'h','a','r','d','l','i','n','k',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'0','0',
'0','6','4','4',' ',0,'0','0','1','7','5','0',' ',0,'0','0','1','7','5','0',
' ',0,'0','0','0','0','0','0','0','0','0','0','0',' ','1','0','6','4','6',
'0','5','2','6','6','2',' ','0','1','3','0','5','7',0,' ','1','f','i','l',
'e',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'u','s','t','a','r',0,'0',
'0','t','i','m',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
't','i','m',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'0',
'0','0','0','0','0',' ',0,'0','0','0','0','0','0',' '};

static void verify1(struct archive_entry *ae)
{
	/* A hardlink is not a symlink. */
	assert(archive_entry_filetype(ae) != AE_IFLNK);
	/* Nor is it a directory. */
	assert(archive_entry_filetype(ae) != AE_IFDIR);
	assertEqualInt(archive_entry_mode(ae) & 0777, 0644);
	assertEqualInt(archive_entry_uid(ae), 1000);
	assertEqualInt(archive_entry_gid(ae), 1000);
	assertEqualString(archive_entry_uname(ae), "tim");
	assertEqualString(archive_entry_gname(ae), "tim");
	assertEqualString(archive_entry_pathname(ae), "hardlink");
	assertEqualString(archive_entry_hardlink(ae), "file");
	assert(archive_entry_symlink(ae) == NULL);
	assertEqualInt(archive_entry_mtime(ae), 1184388530);
}

/* Verify that symlinks are read correctly. */
static unsigned char archive2[] = {
's','y','m','l','i','n','k',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'0','0',
'0','0','7','5','5',' ','0','0','0','1','7','5','0',' ','0','0','0','1','7',
'5','0',' ','0','0','0','0','0','0','0','0','0','0','0',' ','1','0','6','4',
'6','0','5','4','1','0','1',' ','0','0','1','3','3','2','3',' ','2','f','i',
'l','e',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'u','s','t','a','r',0,
'0','0','t','i','m',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,'t','i','m',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
'0','0','0','0','0','0','0',' ','0','0','0','0','0','0','0',' '};

static void verify2(struct archive_entry *ae)
{
	assertEqualInt(archive_entry_filetype(ae), AE_IFLNK);
	assertEqualInt(archive_entry_mode(ae) & 0777, 0755);
	assertEqualInt(archive_entry_uid(ae), 1000);
	assertEqualInt(archive_entry_gid(ae), 1000);
	assertEqualString(archive_entry_uname(ae), "tim");
	assertEqualString(archive_entry_gname(ae), "tim");
	assertEqualString(archive_entry_pathname(ae), "symlink");
	assertEqualString(archive_entry_symlink(ae), "file");
	assert(archive_entry_hardlink(ae) == NULL);
	assertEqualInt(archive_entry_mtime(ae), 1184389185);
}

/* Character device node. */
static unsigned char archive3[] = {
'd','e','v','c','h','a','r',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'0','0',
'0','0','7','5','5',' ','0','0','0','1','7','5','0',' ','0','0','0','1','7',
'5','0',' ','0','0','0','0','0','0','0','0','0','0','0',' ','1','0','6','4',
'6','0','5','4','1','0','1',' ','0','0','1','2','4','1','2',' ','3',0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'u','s','t','a','r',0,
'0','0','t','i','m',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,'t','i','m',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
'0','0','0','0','0','0','0',' ','0','0','0','0','0','0','0',' '};

static void verify3(struct archive_entry *ae)
{
	assertEqualInt(archive_entry_filetype(ae), AE_IFCHR);
	assertEqualInt(archive_entry_mode(ae) & 0777, 0755);
	assertEqualInt(archive_entry_uid(ae), 1000);
	assertEqualInt(archive_entry_gid(ae), 1000);
	assertEqualString(archive_entry_uname(ae), "tim");
	assertEqualString(archive_entry_gname(ae), "tim");
	assertEqualString(archive_entry_pathname(ae), "devchar");
	assert(archive_entry_symlink(ae) == NULL);
	assert(archive_entry_hardlink(ae) == NULL);
	assertEqualInt(archive_entry_mtime(ae), 1184389185);
}

/* Block device node. */
static unsigned char archive4[] = {
'd','e','v','b','l','o','c','k',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'0','0',
'0','0','7','5','5',' ','0','0','0','1','7','5','0',' ','0','0','0','1','7',
'5','0',' ','0','0','0','0','0','0','0','0','0','0','0',' ','1','0','6','4',
'6','0','5','4','1','0','1',' ','0','0','1','2','5','7','0',' ','4',0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'u','s','t','a','r',0,
'0','0','t','i','m',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,'t','i','m',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
'0','0','0','0','0','0','0',' ','0','0','0','0','0','0','0',' '};

static void verify4(struct archive_entry *ae)
{
	assertEqualInt(archive_entry_filetype(ae), AE_IFBLK);
	assertEqualInt(archive_entry_mode(ae) & 0777, 0755);
	assertEqualInt(archive_entry_uid(ae), 1000);
	assertEqualInt(archive_entry_gid(ae), 1000);
	assertEqualString(archive_entry_uname(ae), "tim");
	assertEqualString(archive_entry_gname(ae), "tim");
	assertEqualString(archive_entry_pathname(ae), "devblock");
	assert(archive_entry_symlink(ae) == NULL);
	assert(archive_entry_hardlink(ae) == NULL);
	assertEqualInt(archive_entry_mtime(ae), 1184389185);
}

/* Directory. */
static unsigned char archive5[] = {
'.',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'0','0','0',
'7','5','5',' ',0,'0','0','1','7','5','0',' ',0,'0','0','1','7','5','0',
' ',0,'0','0','0','0','0','0','0','0','0','0','0',' ','1','0','3','3',
'4','0','4','1','7','3','6',' ','0','1','0','5','6','1',0,' ','5',0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'u','s','t','a','r',0,
'0','0','t','i','m',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,'t','i','m',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,'0','0','0','0','0','0',' ',0,'0','0','0','0','0','0',' '};

static void verify5(struct archive_entry *ae)
{
	assertEqualInt(archive_entry_filetype(ae), AE_IFDIR);
	assertEqualInt(archive_entry_mtime(ae), 1131430878);
	assertEqualInt(archive_entry_mode(ae) & 0777, 0755);
	assertEqualInt(archive_entry_uid(ae), 1000);
	assertEqualInt(archive_entry_gid(ae), 1000);
	assertEqualString(archive_entry_uname(ae), "tim");
	assertEqualString(archive_entry_gname(ae), "tim");
}

/* fifo */
static unsigned char archive6[] = {
'f','i','f','o',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'0','0',
'0','0','7','5','5',' ','0','0','0','1','7','5','0',' ','0','0','0','1','7',
'5','0',' ','0','0','0','0','0','0','0','0','0','0','0',' ','1','0','6','4',
'6','0','5','4','1','0','1',' ','0','0','1','1','7','2','4',' ','6',0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'u','s','t','a','r',0,
'0','0','t','i','m',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,'t','i','m',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
'0','0','0','0','0','0','0',' ','0','0','0','0','0','0','0',' '};

static void verify6(struct archive_entry *ae)
{
	assertEqualInt(archive_entry_filetype(ae), AE_IFIFO);
	assertEqualInt(archive_entry_mode(ae) & 0777, 0755);
	assertEqualInt(archive_entry_uid(ae), 1000);
	assertEqualInt(archive_entry_gid(ae), 1000);
	assertEqualString(archive_entry_uname(ae), "tim");
	assertEqualString(archive_entry_gname(ae), "tim");
	assertEqualString(archive_entry_pathname(ae), "fifo");
	assert(archive_entry_symlink(ae) == NULL);
	assert(archive_entry_hardlink(ae) == NULL);
	assertEqualInt(archive_entry_mtime(ae), 1184389185);
}

/* GNU long link name */
static unsigned char archiveK[] = {
'.','/','.','/','@','L','o','n','g','L','i','n','k',0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,'0','0','0','0','0','0','0',0,'0','0','0','0','0','0','0',0,'0','0','0',
'0','0','0','0',0,'0','0','0','0','0','0','0','0','6','6','6',0,'0','0','0',
'0','0','0','0','0','0','0','0',0,'0','1','1','7','1','5',0,' ','K',0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'u','s','t','a','r',' ',' ',
0,'r','o','o','t',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
'w','h','e','e','l',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'t',
'h','i','s','_','i','s','_','a','_','v','e','r','y','_','l','o','n','g','_',
's','y','m','l','i','n','k','_','b','o','d','y','_','a','b','c','d','e','f',
'g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y',
'z','_','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q',
'r','s','t','u','v','w','x','y','z','_','a','b','c','d','e','f','g','h','i',
'j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z','_','a',
'b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t',
'u','v','w','x','y','z','_','a','b','c','d','e','f','g','h','i','j','k','l',
'm','n','o','p','q','r','s','t','u','v','w','x','y','z','_','a','b','c','d',
'e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w',
'x','y','z','_','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o',
'p','q','r','s','t','u','v','w','x','y','z','_','a','b','c','d','e','f','g',
'h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z',
'_','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r',
's','t','u','v','w','x','y','z','_','a','b','c','d','e','f','g','h','i','j',
'k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z','_','a','b',
'c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u',
'v','w','x','y','z','_','a','b','c','d','e','f','g','h','i','j','k','l','m',
'n','o','p','q','r','s','t','u','v','w','x','y','z','_','a','b','c','d','e',
'f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x',
'y','z','_','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p',
'q','r','s','t','u','v','w','x','y','z','_','a','b','c','d','e','f','g','h',
'i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z',0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
's','y','m','l','i','n','k',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'0','1',
'2','0','7','5','5',0,'0','0','0','1','7','5','0',0,'0','0','0','1','7','5',
'0',0,'0','0','0','0','0','0','0','0','0','0','0',0,'1','0','6','4','6','0',
'5','6','7','7','0',0,'0','3','5','4','4','7',0,' ','2','t','h','i','s','_',
'i','s','_','a','_','v','e','r','y','_','l','o','n','g','_','s','y','m','l',
'i','n','k','_','b','o','d','y','_','a','b','c','d','e','f','g','h','i','j',
'k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z','_','a','b',
'c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u',
'v','w','x','y','z','_','a','b','c','d','e','f','g','h','i','j','k','l',0,
'u','s','t','a','r',' ',' ',0,'t','i','m',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,'t','i','m'};

static void verifyK(struct archive_entry *ae)
{
	assertEqualInt(archive_entry_filetype(ae), AE_IFLNK);
	assertEqualInt(archive_entry_mode(ae) & 0777, 0755);
	assertEqualInt(archive_entry_uid(ae), 1000);
	assertEqualInt(archive_entry_gid(ae), 1000);
	assertEqualString(archive_entry_uname(ae), "tim");
	assertEqualString(archive_entry_gname(ae), "tim");
	assertEqualString(archive_entry_pathname(ae), "symlink");
	assertEqualString(archive_entry_symlink(ae),
	    "this_is_a_very_long_symlink_body_abcdefghijklmnopqrstuvwxyz_"
	    "abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_"
	    "abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_"
	    "abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_"
	    "abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_"
	    "abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_"
	    "abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_"
	    "abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz");
	assert(archive_entry_hardlink(ae) == NULL);
	assertEqualInt(archive_entry_mtime(ae), 1184390648);
}

/* TODO: GNU long name */

/* TODO: Solaris ACL */

/* Pax extended long link name */
static unsigned char archivexL[] = {
'.','/','P','a','x','H','e','a','d','e','r','s','.','8','6','9','7','5','/',
's','y','m','l','i','n','k',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,'0','0','0','0','6','4','4',0,'0','0','0','1',
'7','5','0',0,'0','0','0','1','7','5','0',0,'0','0','0','0','0','0','0','0',
'7','5','3',0,'1','0','6','4','6','0','5','7','6','1','1',0,'0','1','3','7',
'1','4',0,' ','x',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'u',
's','t','a','r',0,'0','0',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,'0','0','0','0','0','0','0',0,'0','0','0','0','0','0','0',0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'4','5','1',' ','l','i','n','k','p','a','t',
'h','=','t','h','i','s','_','i','s','_','a','_','v','e','r','y','_','l','o',
'n','g','_','s','y','m','l','i','n','k','_','b','o','d','y','_','a','b','c',
'd','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v',
'w','x','y','z','_','a','b','c','d','e','f','g','h','i','j','k','l','m','n',
'o','p','q','r','s','t','u','v','w','x','y','z','_','a','b','c','d','e','f',
'g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y',
'z','_','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q',
'r','s','t','u','v','w','x','y','z','_','a','b','c','d','e','f','g','h','i',
'j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z','_','a',
'b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t',
'u','v','w','x','y','z','_','a','b','c','d','e','f','g','h','i','j','k','l',
'm','n','o','p','q','r','s','t','u','v','w','x','y','z','_','a','b','c','d',
'e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w',
'x','y','z','_','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o',
'p','q','r','s','t','u','v','w','x','y','z','_','a','b','c','d','e','f','g',
'h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z',
'_','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r',
's','t','u','v','w','x','y','z','_','a','b','c','d','e','f','g','h','i','j',
'k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z','_','a','b',
'c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u',
'v','w','x','y','z','_','a','b','c','d','e','f','g','h','i','j','k','l','m',
'n','o','p','q','r','s','t','u','v','w','x','y','z','_','a','b','c','d','e',
'f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x',
'y','z',10,'2','0',' ','a','t','i','m','e','=','1','1','8','4','3','9','1',
'0','2','5',10,'2','0',' ','c','t','i','m','e','=','1','1','8','4','3','9',
'0','6','4','8',10,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'s','y','m',
'l','i','n','k',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'0','0','0','0','7',
'5','5',0,'0','0','0','1','7','5','0',0,'0','0','0','1','7','5','0',0,'0',
'0','0','0','0','0','0','0','0','0','0',0,'1','0','6','4','6','0','5','6',
'7','7','0',0,'0','3','7','1','2','1',0,' ','2','t','h','i','s','_','i','s',
'_','a','_','v','e','r','y','_','l','o','n','g','_','s','y','m','l','i','n',
'k','_','b','o','d','y','_','a','b','c','d','e','f','g','h','i','j','k','l',
'm','n','o','p','q','r','s','t','u','v','w','x','y','z','_','a','b','c','d',
'e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w',
'x','y','z','_','a','b','c','d','e','f','g','h','i','j','k','l','m','u','s',
't','a','r',0,'0','0','t','i','m',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,'t','i','m',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,'0','0','0','0','0','0','0',0,'0','0','0','0','0','0','0'};

static void verifyxL(struct archive_entry *ae)
{
	assertEqualInt(archive_entry_filetype(ae), AE_IFLNK);
	assertEqualInt(archive_entry_mode(ae) & 0777, 0755);
	assertEqualInt(archive_entry_uid(ae), 1000);
	assertEqualInt(archive_entry_gid(ae), 1000);
	assertEqualString(archive_entry_uname(ae), "tim");
	assertEqualString(archive_entry_gname(ae), "tim");
	assertEqualString(archive_entry_pathname(ae), "symlink");
	assertEqualString(archive_entry_symlink(ae),
	    "this_is_a_very_long_symlink_body_abcdefghijklmnopqrstuvwxyz_"
	    "abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_"
	    "abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_"
	    "abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_"
	    "abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_"
	    "abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_"
	    "abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_"
	    "abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz");
	assert(archive_entry_hardlink(ae) == NULL);
	assertEqualInt(archive_entry_mtime(ae), 1184390648);
}


/* TODO: Any other types of headers? */

static void verify(unsigned char *d, size_t s,
    void (*f)(struct archive_entry *),
    int compression, int format)
{
	struct archive_entry *ae;
	struct archive *a;
	unsigned char *buff = malloc(100000);

	memcpy(buff, d, s);
	memset(buff + s, 0, 2048);

	assert((a = archive_read_new()) != NULL);
	assertA(0 == archive_read_support_filter_all(a));
	assertA(0 == archive_read_support_format_all(a));
	assertA(0 == archive_read_open_memory(a, buff, s + 1024));
	assertA(0 == archive_read_next_header(a, &ae));
	assertEqualInt(archive_filter_code(a, 0), compression);
	assertEqualInt(archive_format(a), format);
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);

	/* Verify the only entry. */
	f(ae);

	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	free(buff);
}

DEFINE_TEST(test_read_format_tar)
{
	verifyEmpty();
	verify(archive1, sizeof(archive1), verify1,
	    ARCHIVE_FILTER_NONE, ARCHIVE_FORMAT_TAR_USTAR);
	verify(archive2, sizeof(archive2), verify2,
	    ARCHIVE_FILTER_NONE, ARCHIVE_FORMAT_TAR_USTAR);
	verify(archive3, sizeof(archive3), verify3,
	    ARCHIVE_FILTER_NONE, ARCHIVE_FORMAT_TAR_USTAR);
	verify(archive4, sizeof(archive4), verify4,
	    ARCHIVE_FILTER_NONE, ARCHIVE_FORMAT_TAR_USTAR);
	verify(archive5, sizeof(archive5), verify5,
	    ARCHIVE_FILTER_NONE, ARCHIVE_FORMAT_TAR_USTAR);
	verify(archive6, sizeof(archive6), verify6,
	    ARCHIVE_FILTER_NONE, ARCHIVE_FORMAT_TAR_USTAR);
	verify(archiveK, sizeof(archiveK), verifyK,
	    ARCHIVE_FILTER_NONE, ARCHIVE_FORMAT_TAR_GNUTAR);
	verify(archivexL, sizeof(archivexL), verifyxL,
	    ARCHIVE_FILTER_NONE, ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE);
}


