/*-
 * Copyright (c) 2009,2010 Michihiro NAKAJIMA
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
 * Check that an ISO 9660 image is correctly created.
 */
static void
add_entry(struct archive *a, const char *fname, const char *sym)
{
	struct archive_entry *ae;

	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_birthtime(ae, 2, 20);
	archive_entry_set_atime(ae, 3, 30);
	archive_entry_set_ctime(ae, 4, 40);
	archive_entry_set_mtime(ae, 5, 50);
	archive_entry_copy_pathname(ae, fname);
	if (sym != NULL)
		archive_entry_set_symlink(ae, sym);
	archive_entry_set_mode(ae, S_IFREG | 0555);
	archive_entry_set_size(ae, 0);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
}

struct fns {
	size_t	maxlen;
	size_t	longest_len;
	size_t	maxflen;
	size_t	maxelen;
	size_t	alloc;
	int	cnt;
	char	**names;
	int	opt;
#define	UPPER_CASE_ONLY	0x00001
#define	ONE_DOT		0x00002
#define	ALLOW_LDOT	0x00004
};

enum vtype {
	ROCKRIDGE,
	JOLIET,
	ISO9660
};

/*
 * Verify file
 */
static void
verify_file(struct archive *a, enum vtype type, struct fns *fns)
{
	struct archive_entry *ae;
	int i;

	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	if (type == ROCKRIDGE) {
		assertEqualInt(2, archive_entry_birthtime(ae));
		assertEqualInt(3, archive_entry_atime(ae));
		assertEqualInt(4, archive_entry_ctime(ae));
	} else {
		assertEqualInt(0, archive_entry_birthtime_is_set(ae));
		assertEqualInt(5, archive_entry_atime(ae));
		assertEqualInt(5, archive_entry_ctime(ae));
	}
	assertEqualInt(5, archive_entry_mtime(ae));
	if (type == ROCKRIDGE)
		assert((S_IFREG | 0555) == archive_entry_mode(ae));
	else
		assert((S_IFREG | 0400) == archive_entry_mode(ae));
	assertEqualInt(0, archive_entry_size(ae));

	/*
	 * Check if the same filename does not appear.
	 */
	for (i = 0; i < fns->cnt; i++) {
		const char *p;
		const char *pathname = archive_entry_pathname(ae);
		const char *symlinkname = archive_entry_symlink(ae);
		size_t length;

		if (symlinkname != NULL) {
			length = strlen(symlinkname);
			assert(length == 1 || length == 128 || length == 255);
			assertEqualInt(symlinkname[length-1], 'x');
		}
		failure("Found duplicate for %s", pathname);
		assert(strcmp(fns->names[i], pathname) != 0);
		assert((length = strlen(pathname)) <= fns->maxlen);
		if (length > fns->longest_len)
			fns->longest_len = length;
		p = strrchr(pathname, '.');
		if (p != NULL) {
			/* Check a length of file name. */
			assert((size_t)(p - pathname) <= fns->maxflen);
			/* Check a length of file extension. */
			assert(strlen(p+1) <= fns->maxelen);
			if (fns->opt & ONE_DOT) {
				/* Do not have multi dot. */
				assert(strchr(pathname, '.') == p);
			}
		}
		for (p = pathname; *p; p++) {
			if (fns->opt & UPPER_CASE_ONLY) {
				/* Do not have any lower-case character. */
				assert(*p < 'a' || *p > 'z');
			} else
				break;
		}
		if ((fns->opt & ALLOW_LDOT) == 0)
			/* Do not have a dot at the first position. */
			assert(*pathname != '.');
	}
	/* Save the filename which is appeared to use above next time. */
	fns->names[fns->cnt++] = strdup(archive_entry_pathname(ae));
}

static void
verify(unsigned char *buff, size_t used, enum vtype type, struct fns *fns)
{
	struct archive *a;
	struct archive_entry *ae;
	size_t i;

	/*
	 * Read ISO image.
	 */
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, 0, archive_read_support_format_all(a));
	assertEqualIntA(a, 0, archive_read_support_filter_all(a));
	if (type >= 1)
		assertA(0 == archive_read_set_option(a, NULL, "rockridge",
		    NULL));
	if (type >= 2)
		assertA(0 == archive_read_set_option(a, NULL, "joliet",
		    NULL));
	assertEqualIntA(a, 0, archive_read_open_memory(a, buff, used));

	/*
	 * Read Root Directory
	 * Root Directory entry must be in ISO image.
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(archive_entry_atime(ae), archive_entry_ctime(ae));
	assertEqualInt(archive_entry_atime(ae), archive_entry_mtime(ae));
	assertEqualString(".", archive_entry_pathname(ae));
	switch (type) {
	case ROCKRIDGE:
		assert((S_IFDIR | 0555) == archive_entry_mode(ae));
		break;
	case JOLIET:
		assert((S_IFDIR | 0700) == archive_entry_mode(ae));
		break;
	case ISO9660:
		assert((S_IFDIR | 0700) == archive_entry_mode(ae));
		break;
	}

	/*
	 * Verify file status.
	 */
	memset(fns->names, 0, sizeof(char *) * fns->alloc);
	fns->cnt = 0;
	for (i = 0; i < fns->alloc; i++)
		verify_file(a, type, fns);
	for (i = 0; i < fns->alloc; i++)
		free(fns->names[i]);
	assertEqualInt((int)fns->longest_len, (int)fns->maxlen);

	/*
	 * Verify the end of the archive.
	 */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_free(a));
}

static int
create_iso_image(unsigned char *buff, size_t buffsize, size_t *used,
    const char *opt)
{
	struct archive *a;
	int i, l, fcnt;
	const int lens[] = {
	    0, 1, 3, 5, 7, 8, 9, 29, 30, 31, 32,
		62, 63, 64, 65, 101, 102, 103, 104,
	    191, 192, 193, 194, 204, 205, 206, 207, 208,
		252, 253, 254, 255,
	    -1 };
	char fname1[256];
	char fname2[256];
	char sym1[2];
	char sym128[129];
	char sym255[256];

	/* ISO9660 format: Create a new archive in memory. */
	assert((a = archive_write_new()) != NULL);
	assertA(0 == archive_write_set_format_iso9660(a));
	assertA(0 == archive_write_add_filter_none(a));
	assertA(0 == archive_write_set_option(a, NULL, "pad", NULL));
	if (opt)
		assertA(0 == archive_write_set_options(a, opt));
	assertA(0 == archive_write_set_bytes_per_block(a, 1));
	assertA(0 == archive_write_set_bytes_in_last_block(a, 1));
	assertA(0 == archive_write_open_memory(a, buff, buffsize, used));

	sym1[0] = 'x';
	sym1[1] = '\0';
	for (i = 0; i < (int)sizeof(sym128)-2; i++)
		sym128[i] = 'a';
	sym128[sizeof(sym128)-2] = 'x';
	sym128[sizeof(sym128)-1] = '\0';
	for (i = 0; i < (int)sizeof(sym255)-2; i++)
		sym255[i] = 'a';
	sym255[sizeof(sym255)-2] = 'x';
	sym255[sizeof(sym255)-1] = '\0';

	fcnt = 0;
	for (i = 0; lens[i] >= 0; i++) {
		for (l = 0; l < lens[i]; l++) {
			fname1[l] = 'a';
			fname2[l] = 'A';
		}
		if (l > 0) {
			fname1[l] = '\0';
			fname2[l] = '\0';
			add_entry(a, fname1, NULL);
			add_entry(a, fname2, sym1);
			fcnt += 2;
		}
		if (l < 254) {
			fname1[l] = '.';
			fname1[l+1] = 'c';
			fname1[l+2] = '\0';
			fname2[l] = '.';
			fname2[l+1] = 'C';
			fname2[l+2] = '\0';
			add_entry(a, fname1, sym128);
			add_entry(a, fname2, sym255);
			fcnt += 2;
		}
		if (l < 252) {
			fname1[l] = '.';
			fname1[l+1] = 'p';
			fname1[l+2] = 'n';
			fname1[l+3] = 'g';
			fname1[l+4] = '\0';
			fname2[l] = '.';
			fname2[l+1] = 'P';
			fname2[l+2] = 'N';
			fname2[l+3] = 'G';
			fname2[l+4] = '\0';
			add_entry(a, fname1, NULL);
			add_entry(a, fname2, sym1);
			fcnt += 2;
		}
		if (l < 251) {
			fname1[l] = '.';
			fname1[l+1] = 'j';
			fname1[l+2] = 'p';
			fname1[l+3] = 'e';
			fname1[l+4] = 'g';
			fname1[l+5] = '\0';
			fname2[l] = '.';
			fname2[l+1] = 'J';
			fname2[l+2] = 'P';
			fname2[l+3] = 'E';
			fname2[l+4] = 'G';
			fname2[l+5] = '\0';
			add_entry(a, fname1, sym128);
			add_entry(a, fname2, sym255);
			fcnt += 2;
		}
	}

	/* Close out the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_free(a));

	return (fcnt);
}

DEFINE_TEST(test_write_format_iso9660_filename)
{
	unsigned char *buff;
	size_t buffsize = 120 * 2048;
	size_t used;
	int fcnt;
	struct fns fns;

	buff = malloc(buffsize);
	assert(buff != NULL);
	if (buff == NULL)
		return;
	memset(&fns, 0, sizeof(fns));

	/*
	 * Create ISO image with no option.
	 */
	fcnt = create_iso_image(buff, buffsize, &used, NULL);

	fns.names = (char **)malloc(sizeof(char *) * fcnt);
	assert(fns.names != NULL);
	if (fns.names == NULL) {
		free(buff);
		return;
	}
	fns.alloc = fcnt;

	/* Verify rockridge filenames. */
	fns.longest_len = 0;
	fns.maxlen = fns.maxflen = fns.maxelen = 255;
	fns.opt = ALLOW_LDOT;
	verify(buff, used, ROCKRIDGE, &fns);

	/* Verify joliet filenames. */
	fns.longest_len = 0;
	fns.maxlen = fns.maxflen = fns.maxelen = 64;
	fns.opt = ALLOW_LDOT;
	verify(buff, used, JOLIET, &fns);

	/* Verify ISO9660 filenames. */
	fns.longest_len = 0;
	fns.maxlen = 8+3+1;
	fns.maxflen = 8;
	fns.maxelen = 3;
	fns.opt = UPPER_CASE_ONLY | ONE_DOT;
	verify(buff, used, ISO9660, &fns);

	/*
	 * Create ISO image with iso-level=2.
	 */
	assertEqualInt(fcnt, create_iso_image(buff, buffsize, &used,
	    "iso-level=2"));

	/* Verify rockridge filenames. */
	fns.longest_len = 0;
	fns.maxlen = fns.maxflen = fns.maxelen = 255;
	fns.opt = ALLOW_LDOT;
	verify(buff, used, ROCKRIDGE, &fns);

	/* Verify joliet filenames. */
	fns.longest_len = 0;
	fns.maxlen = fns.maxflen = fns.maxelen = 64;
	fns.opt = ALLOW_LDOT;
	verify(buff, used, JOLIET, &fns);

	/* Verify ISO9660 filenames. */
	fns.longest_len = 0;
	fns.maxlen = 31;
	fns.maxflen = 30;
	fns.maxelen = 30;
	fns.opt = UPPER_CASE_ONLY | ONE_DOT;
	verify(buff, used, ISO9660, &fns);

	/*
	 * Create ISO image with iso-level=3.
	 */
	assertEqualInt(fcnt, create_iso_image(buff, buffsize, &used,
	    "iso-level=3"));

	/* Verify rockridge filenames. */
	fns.longest_len = 0;
	fns.maxlen = fns.maxflen = fns.maxelen = 255;
	fns.opt = ALLOW_LDOT;
	verify(buff, used, ROCKRIDGE, &fns);

	/* Verify joliet filenames. */
	fns.longest_len = 0;
	fns.maxlen = fns.maxflen = fns.maxelen = 64;
	fns.opt = ALLOW_LDOT;
	verify(buff, used, JOLIET, &fns);

	/* Verify ISO9660 filenames. */
	fns.longest_len = 0;
	fns.maxlen = 31;
	fns.maxflen = 30;
	fns.maxelen = 30;
	fns.opt = UPPER_CASE_ONLY | ONE_DOT;
	verify(buff, used, ISO9660, &fns);

	/*
	 * Create ISO image with iso-level=4.
	 */
	assertEqualInt(fcnt, create_iso_image(buff, buffsize, &used,
	    "iso-level=4"));

	/* Verify rockridge filenames. */
	fns.longest_len = 0;
	fns.maxlen = fns.maxflen = fns.maxelen = 255;
	fns.opt = ALLOW_LDOT;
	verify(buff, used, ROCKRIDGE, &fns);

	/* Verify joliet filenames. */
	fns.longest_len = 0;
	fns.maxlen = fns.maxflen = fns.maxelen = 64;
	fns.opt = ALLOW_LDOT;
	verify(buff, used, JOLIET, &fns);

	/* Verify ISO9660 filenames. */
	fns.longest_len = 0;
	fns.maxlen = fns.maxflen = fns.maxelen = 193;
	fns.opt = ALLOW_LDOT;
	verify(buff, used, ISO9660, &fns);

	/*
	 * Create ISO image with iso-level=4 and !rockridge.
	 */
	assertEqualInt(fcnt, create_iso_image(buff, buffsize, &used,
	    "iso-level=4,!rockridge"));

	/* Verify joliet filenames. */
	fns.longest_len = 0;
	fns.maxlen = fns.maxflen = fns.maxelen = 64;
	fns.opt = ALLOW_LDOT;
	verify(buff, used, JOLIET, &fns);

	/* Verify ISO9660 filenames. */
	fns.longest_len = 0;
	fns.maxlen = fns.maxflen = fns.maxelen = 207;
	fns.opt = ALLOW_LDOT;
	verify(buff, used, ISO9660, &fns);

	/*
	 * Create ISO image with joliet=long.
	 */
	assertEqualInt(fcnt, create_iso_image(buff, buffsize, &used,
	    "joliet=long"));

	/* Verify rockridge filenames. */
	fns.longest_len = 0;
	fns.maxlen = fns.maxflen = fns.maxelen = 255;
	fns.opt = ALLOW_LDOT;
	verify(buff, used, ROCKRIDGE, &fns);

	/* Verify joliet filenames. */
	fns.longest_len = 0;
	fns.maxlen = fns.maxflen = fns.maxelen = 103;
	fns.opt = ALLOW_LDOT;
	verify(buff, used, JOLIET, &fns);

	/* Verify ISO9660 filenames. */
	fns.longest_len = 0;
	fns.maxlen = 8+3+1;
	fns.maxflen = 8;
	fns.maxelen = 3;
	fns.opt = UPPER_CASE_ONLY | ONE_DOT;
	verify(buff, used, ISO9660, &fns);

	free(fns.names);
	free(buff);
}
