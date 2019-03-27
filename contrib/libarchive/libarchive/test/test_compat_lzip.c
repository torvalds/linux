/*-
 * Copyright (c) 2010 Michihiro NAKAJIMA
 * Copyright (c) 2003-2008 Tim Kientzle
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
Execute the following to rebuild the data for this program:
   tail -n +33 test_compat_lzip.c | /bin/sh

# Use lzip command.
zcmd=lzip
zsuffix=lz
ztar_suffix=tlz
mktarfile()
{
mkdir $dir
echo "f1" > $dir/f1
echo "f2" > $dir/f2
echo "f3" > $dir/f3
mkdir $dir/d1
echo "f1" > $dir/d1/f1
echo "f2" > $dir/d1/f2
echo "f3" > $dir/d1/f3
(cd $dir; tar cf ../$name.tar f1 f2 f3 d1/f1 d1/f2 d1/f3)
rm -r $dir
}
#
# Make a lzip file from splitted tar file.
#
name=test_compat_lzip_1
dir="$name`date +%Y%m%d%H%M%S`.$USER"
mktarfile
split -b 3600 $name.tar $name.tar.
rm $name.tar
$zcmd $name.tar.*
cat $name.tar.*.$zsuffix > $name.$ztar_suffix
rm $name.tar.*.$zsuffix
uuencode $name.$ztar_suffix $name.$ztar_suffix > $name.$ztar_suffix.uu
rm -f $name.$ztar_suffix
#
# Make a lzip file with junk data at the end of the file.
#
name=test_compat_lzip_2
dir="$name`date +%Y%m%d%H%M%S`.$USER"
mktarfile
$zcmd $name.tar
mv $name.tar.$zsuffix $name.$ztar_suffix
echo "This is unrelated junk data at the end of the file" >> $name.$ztar_suffix
uuencode $name.$ztar_suffix $name.$ztar_suffix > $name.$ztar_suffix.uu
rm -f $name.$ztar_suffix

exit 0
*/

/*
 * Verify our ability to read sample files compatibly with lzip.
 *
 * In particular:
 *  * lzip will read multiple lzip streams, concatenating the output
 *  * lzip will stop at the end of a stream if the following data
 *    doesn't start with a gzip signature.
 *
 */

/*
 * All of the sample files have the same contents; they're just
 * compressed in different ways.
 */
static void
compat_lzip(const char *name)
{
	const char *n[7] = { "f1", "f2", "f3", "d1/f1", "d1/f2", "d1/f3", NULL };
	struct archive_entry *ae;
	struct archive *a;
	int i, r;

	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	r = archive_read_support_filter_lzip(a);
	if (r == ARCHIVE_WARN) {
		skipping("lzip reading not fully supported on this platform");
		assertEqualInt(ARCHIVE_OK, archive_read_free(a));
		return;
	}
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	extract_reference_file(name);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_filename(a, name, 2));

	/* Read entries, match up names with list above. */
	for (i = 0; i < 6; ++i) {
		failure("Could not read file %d (%s) from %s", i, n[i], name);
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_read_next_header(a, &ae));
		assertEqualString(n[i], archive_entry_pathname(ae));
	}

	/* Verify the end-of-archive. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	/* Verify that the format detection worked. */
	assertEqualInt(archive_filter_code(a, 0), ARCHIVE_FILTER_LZIP);
	assertEqualString(archive_filter_name(a, 0), "lzip");
	assertEqualInt(archive_format(a), ARCHIVE_FORMAT_TAR_USTAR);

	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}


DEFINE_TEST(test_compat_lzip)
{
        /* This sample has been 'split', each piece compressed separately,
         * then concatenated.  lzip will emit the concatenated result. */
        compat_lzip("test_compat_lzip_1.tlz");
        /* This sample has been compressed as a single stream, but then
         * some unrelated garbage text has been appended to the end. */
        compat_lzip("test_compat_lzip_2.tlz");
}
