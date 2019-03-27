/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * Copyright (c) 2010 Michihiro NAKAJIMA
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
execute the following to rebuild the data for this program:
   tail -n +33 test_read_format_cpio_afio.c | /bin/sh

# How to make a sample data.
echo "0123456789abcdef" > file1
echo "0123456789abcdef" > file2
# make afio use a large ASCII header
sudo chown 65536 file2
find . -name "file[12]" | afio -o sample
od -c sample | sed -E -e "s/^0[0-9]+//;s/^  //;s/( +)([^ ]{1,2})/'\2',/g;s/'\\0'/0/g;/^[*]/d" > test_read_format_cpio_afio.sample.txt
rm -f file1 file2 sample
exit1
*/

static unsigned char archive[] = {
'0','7','0','7','0','7','0','0','0','1','4','3','1','2','5','3',
'2','1','1','0','0','6','4','4','0','0','1','7','5','1','0','0',
'1','7','5','1','0','0','0','0','0','1','0','0','0','0','0','0',
'1','1','3','3','2','2','4','5','0','2','0','0','0','0','0','0',
'6','0','0','0','0','0','0','0','0','0','2','1','f','i','l','e',
'1',0,'0','1','2','3','4','5','6','7','8','9','a','b','c','d',
'e','f','\n','0','7','0','7','2','7','0','0','0','0','0','0','6',
'3','0','0','0','0','0','0','0','0','0','0','0','D','A','A','E',
'6','m','1','0','0','6','4','4','0','0','0','1','0','0','0','0',
'0','0','0','0','0','3','E','9','0','0','0','0','0','0','0','1',
'0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0',
'4','B','6','9','4','A','1','0','n','0','0','0','6','0','0','0',
'0','0','0','0','0','s','0','0','0','0','0','0','0','0','0','0',
'0','0','0','0','1','1',':','f','i','l','e','2',0,'0','1','2',
'3','4','5','6','7','8','9','a','b','c','d','e','f','\n','0','7',
'0','7','0','7','0','0','0','0','0','0','0','0','0','0','0','0',
'0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0',
'0','0','0','0','0','0','0','1','0','0','0','0','0','0','0','0',
'0','0','0','0','0','0','0','0','0','0','0','0','0','1','3','0',
'0','0','0','0','0','1','1','2','7','3','T','R','A','I','L','E',
'R','!','!','!',0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

/*
 * XXX This must be removed when we use int64_t for uid.
 */
static int
uid_size(void)
{
	return (sizeof(uid_t));
}

DEFINE_TEST(test_read_format_cpio_afio)
{
	unsigned char *p;
	size_t size;
	struct archive_entry *ae;
	struct archive *a;

	/* The default block size of afio is 5120. we simulate it */
	size = (sizeof(archive) + 5120 -1 / 5120) * 5120;
	assert((p = malloc(size)) != NULL);
	if (p == NULL)
		return;
	memset(p, 0, size);
	memcpy(p, archive, sizeof(archive));
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_memory(a, p, size));
	/*
	 * First entry is odc format.
	 */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt(17, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);
	assertA(archive_filter_code(a, 0) == ARCHIVE_FILTER_NONE);
	assertA(archive_format(a) == ARCHIVE_FORMAT_CPIO_POSIX);
	/*
	 * Second entry is afio large ASCII format.
	 */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt(17, archive_entry_size(ae));
	if (uid_size() > 4)
		assertEqualInt(65536, archive_entry_uid(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);
	assertA(archive_filter_code(a, 0) == ARCHIVE_FILTER_NONE);
	assertA(archive_format(a) == ARCHIVE_FORMAT_CPIO_AFIO_LARGE);
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	free(p);
}
