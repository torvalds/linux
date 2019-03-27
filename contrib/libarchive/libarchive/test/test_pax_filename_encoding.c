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

#include <locale.h>

/*
 * Pax interchange is supposed to encode filenames into
 * UTF-8.  Of course, that's not always possible.  This
 * test is intended to verify that filenames always get
 * stored and restored correctly, regardless of the encodings.
 */

/*
 * Read a manually-created archive that has filenames that are
 * stored in binary instead of UTF-8 and verify that we get
 * the right filename returned and that we get a warning only
 * if the header isn't marked as binary.
 */
static void
test_pax_filename_encoding_1(void)
{
	static const char testname[] = "test_pax_filename_encoding.tar";
	/*
	 * \314\214 is a valid 2-byte UTF-8 sequence.
	 * \374 is invalid in UTF-8.
	 */
	char filename[] = "abc\314\214mno\374xyz";
	struct archive *a;
	struct archive_entry *entry;

	/*
	 * Read an archive that has non-UTF8 pax filenames in it.
	 */
	extract_reference_file(testname);
	a = archive_read_new();
	assertEqualInt(ARCHIVE_OK, archive_read_support_format_tar(a));
	assertEqualInt(ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualInt(ARCHIVE_OK,
	    archive_read_open_filename(a, testname, 10240));
	/*
	 * First entry in this test archive has an invalid UTF-8 sequence
	 * in it, but the header is not marked as hdrcharset=BINARY, so that
	 * requires a warning.
	 */
	failure("Invalid UTF8 in a pax archive pathname should cause a warning");
	assertEqualInt(ARCHIVE_WARN, archive_read_next_header(a, &entry));
	assertEqualString(filename, archive_entry_pathname(entry));
	/*
	 * Second entry is identical except that it does have
	 * hdrcharset=BINARY, so no warning should be generated.
	 */
	failure("A pathname with hdrcharset=BINARY can have invalid UTF8\n"
	    " characters in it without generating a warning");
	assertEqualInt(ARCHIVE_OK, archive_read_next_header(a, &entry));
	assertEqualString(filename, archive_entry_pathname(entry));
	archive_read_free(a);
}

/*
 * Set the locale and write a pathname containing invalid characters.
 * This should work; the underlying implementation should automatically
 * fall back to storing the pathname in binary.
 */
static void
test_pax_filename_encoding_2(void)
{
	char filename[] = "abc\314\214mno\374xyz";
	struct archive *a;
	struct archive_entry *entry;
	char buff[65536];
	char longname[] = "abc\314\214mno\374xyz"
	    "/abc\314\214mno\374xyz/abcdefghijklmnopqrstuvwxyz"
	    "/abc\314\214mno\374xyz/abcdefghijklmnopqrstuvwxyz"
	    "/abc\314\214mno\374xyz/abcdefghijklmnopqrstuvwxyz"
	    "/abc\314\214mno\374xyz/abcdefghijklmnopqrstuvwxyz"
	    "/abc\314\214mno\374xyz/abcdefghijklmnopqrstuvwxyz"
	    "/abc\314\214mno\374xyz/abcdefghijklmnopqrstuvwxyz"
	    ;
	size_t used;

	/*
	 * We need a starting locale which has invalid sequences.
	 * en_US.UTF-8 seems to be commonly supported.
	 */
	/* If it doesn't exist, just warn and return. */
	if (NULL == setlocale(LC_ALL, "en_US.UTF-8")) {
		skipping("invalid encoding tests require a suitable locale;"
		    " en_US.UTF-8 not available on this system");
		return;
	}

	assert((a = archive_write_new()) != NULL);
	assertEqualIntA(a, 0, archive_write_set_format_pax(a));
	assertEqualIntA(a, 0, archive_write_add_filter_none(a));
	assertEqualIntA(a, 0, archive_write_set_bytes_per_block(a, 0));
	assertEqualInt(0,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	assert((entry = archive_entry_new()) != NULL);
	/* Set pathname, gname, uname, hardlink to nonconvertible values. */
	archive_entry_copy_pathname(entry, filename);
	archive_entry_copy_gname(entry, filename);
	archive_entry_copy_uname(entry, filename);
	archive_entry_copy_hardlink(entry, filename);
	archive_entry_set_filetype(entry, AE_IFREG);
	failure("This should generate a warning for nonconvertible names.");
	assertEqualInt(ARCHIVE_WARN, archive_write_header(a, entry));
	archive_entry_free(entry);

	assert((entry = archive_entry_new()) != NULL);
	/* Set path, gname, uname, and symlink to nonconvertible values. */
	archive_entry_copy_pathname(entry, filename);
	archive_entry_copy_gname(entry, filename);
	archive_entry_copy_uname(entry, filename);
	archive_entry_copy_symlink(entry, filename);
	archive_entry_set_filetype(entry, AE_IFLNK);
	failure("This should generate a warning for nonconvertible names.");
	assertEqualInt(ARCHIVE_WARN, archive_write_header(a, entry));
	archive_entry_free(entry);

	assert((entry = archive_entry_new()) != NULL);
	/* Set pathname to a very long nonconvertible value. */
	archive_entry_copy_pathname(entry, longname);
	archive_entry_set_filetype(entry, AE_IFREG);
	failure("This should generate a warning for nonconvertible names.");
	assertEqualInt(ARCHIVE_WARN, archive_write_header(a, entry));
	archive_entry_free(entry);

	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/*
	 * Now read the entries back.
	 */

	assert((a = archive_read_new()) != NULL);
	assertEqualInt(0, archive_read_support_format_tar(a));
	assertEqualInt(0, archive_read_open_memory(a, buff, used));

	assertEqualInt(0, archive_read_next_header(a, &entry));
	assertEqualString(filename, archive_entry_pathname(entry));
	assertEqualString(filename, archive_entry_gname(entry));
	assertEqualString(filename, archive_entry_uname(entry));
	assertEqualString(filename, archive_entry_hardlink(entry));

	assertEqualInt(0, archive_read_next_header(a, &entry));
	assertEqualString(filename, archive_entry_pathname(entry));
	assertEqualString(filename, archive_entry_gname(entry));
	assertEqualString(filename, archive_entry_uname(entry));
	assertEqualString(filename, archive_entry_symlink(entry));

	assertEqualInt(0, archive_read_next_header(a, &entry));
	assertEqualString(longname, archive_entry_pathname(entry));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

#if 0 /* Disable this until Tim check out it. */

/*
 * Create an entry starting from a wide-character Unicode pathname,
 * read it back into "C" locale, which doesn't support the name.
 * TODO: Figure out the "right" behavior here.
 */
static void
test_pax_filename_encoding_3(void)
{
	wchar_t badname[] = L"xxxAyyyBzzz";
	const char badname_utf8[] = "xxx\xE1\x88\xB4yyy\xE5\x99\xB8zzz";
	struct archive *a;
	struct archive_entry *entry;
	char buff[65536];
	size_t used;

	badname[3] = 0x1234;
	badname[7] = 0x5678;

	/* If it doesn't exist, just warn and return. */
	if (NULL == setlocale(LC_ALL, "C")) {
		skipping("Can't set \"C\" locale, so can't exercise "
		    "certain character-conversion failures");
		return;
	}

	/* If wctomb is broken, warn and return. */
	if (wctomb(buff, 0x1234) > 0) {
		skipping("Cannot test conversion failures because \"C\" "
		    "locale on this system has no invalid characters.");
		return;
	}

	/* If wctomb is broken, warn and return. */
	if (wctomb(buff, 0x1234) > 0) {
		skipping("Cannot test conversion failures because \"C\" "
		    "locale on this system has no invalid characters.");
		return;
	}

	/* Skip test if archive_entry_update_pathname_utf8() is broken. */
	/* In particular, this is currently broken on Win32 because
	 * setlocale() does not set the default encoding for CP_ACP. */
	entry = archive_entry_new();
	if (archive_entry_update_pathname_utf8(entry, badname_utf8)) {
		archive_entry_free(entry);
		skipping("Cannot test conversion failures.");
		return;
	}
	archive_entry_free(entry);

	assert((a = archive_write_new()) != NULL);
	assertEqualIntA(a, 0, archive_write_set_format_pax(a));
	assertEqualIntA(a, 0, archive_write_add_filter_none(a));
	assertEqualIntA(a, 0, archive_write_set_bytes_per_block(a, 0));
	assertEqualInt(0,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	assert((entry = archive_entry_new()) != NULL);
	/* Set pathname to non-convertible wide value. */
	archive_entry_copy_pathname_w(entry, badname);
	archive_entry_set_filetype(entry, AE_IFREG);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);

	assert((entry = archive_entry_new()) != NULL);
	archive_entry_copy_pathname_w(entry, L"abc");
	/* Set gname to non-convertible wide value. */
	archive_entry_copy_gname_w(entry, badname);
	archive_entry_set_filetype(entry, AE_IFREG);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);

	assert((entry = archive_entry_new()) != NULL);
	archive_entry_copy_pathname_w(entry, L"abc");
	/* Set uname to non-convertible wide value. */
	archive_entry_copy_uname_w(entry, badname);
	archive_entry_set_filetype(entry, AE_IFREG);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);

	assert((entry = archive_entry_new()) != NULL);
	archive_entry_copy_pathname_w(entry, L"abc");
	/* Set hardlink to non-convertible wide value. */
	archive_entry_copy_hardlink_w(entry, badname);
	archive_entry_set_filetype(entry, AE_IFREG);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);

	assert((entry = archive_entry_new()) != NULL);
	archive_entry_copy_pathname_w(entry, L"abc");
	/* Set symlink to non-convertible wide value. */
	archive_entry_copy_symlink_w(entry, badname);
	archive_entry_set_filetype(entry, AE_IFLNK);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);

	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/*
	 * Now read the entries back.
	 */

	assert((a = archive_read_new()) != NULL);
	assertEqualInt(0, archive_read_support_format_tar(a));
	assertEqualInt(0, archive_read_open_memory(a, buff, used));

	failure("A non-convertible pathname should cause a warning.");
	assertEqualInt(ARCHIVE_WARN, archive_read_next_header(a, &entry));
	assertEqualWString(badname, archive_entry_pathname_w(entry));
	failure("If native locale can't convert, we should get UTF-8 back.");
	assertEqualString(badname_utf8, archive_entry_pathname(entry));

	failure("A non-convertible gname should cause a warning.");
	assertEqualInt(ARCHIVE_WARN, archive_read_next_header(a, &entry));
	assertEqualWString(badname, archive_entry_gname_w(entry));
	failure("If native locale can't convert, we should get UTF-8 back.");
	assertEqualString(badname_utf8, archive_entry_gname(entry));

	failure("A non-convertible uname should cause a warning.");
	assertEqualInt(ARCHIVE_WARN, archive_read_next_header(a, &entry));
	assertEqualWString(badname, archive_entry_uname_w(entry));
	failure("If native locale can't convert, we should get UTF-8 back.");
	assertEqualString(badname_utf8, archive_entry_uname(entry));

	failure("A non-convertible hardlink should cause a warning.");
	assertEqualInt(ARCHIVE_WARN, archive_read_next_header(a, &entry));
	assertEqualWString(badname, archive_entry_hardlink_w(entry));
	failure("If native locale can't convert, we should get UTF-8 back.");
	assertEqualString(badname_utf8, archive_entry_hardlink(entry));

	failure("A non-convertible symlink should cause a warning.");
	assertEqualInt(ARCHIVE_WARN, archive_read_next_header(a, &entry));
	assertEqualWString(badname, archive_entry_symlink_w(entry));
	assertEqualWString(NULL, archive_entry_hardlink_w(entry));
	failure("If native locale can't convert, we should get UTF-8 back.");
	assertEqualString(badname_utf8, archive_entry_symlink(entry));

	assertEqualInt(ARCHIVE_EOF, archive_read_next_header(a, &entry));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}
#else
static void
test_pax_filename_encoding_3(void)
{
}
#endif

/*
 * Verify that KOI8-R filenames are correctly translated to Unicode and UTF-8.
 */
DEFINE_TEST(test_pax_filename_encoding_KOI8R)
{
  	struct archive *a;
  	struct archive_entry *entry;
	char buff[4096];
	size_t used;

	if (NULL == setlocale(LC_ALL, "ru_RU.KOI8-R")) {
		skipping("KOI8-R locale not available on this system.");
		return;
	}

	/* Check if the platform completely supports the string conversion. */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_pax(a));
	if (archive_write_set_options(a, "hdrcharset=UTF-8") != ARCHIVE_OK) {
		skipping("This system cannot convert character-set"
		    " from KOI8-R to UTF-8.");
		archive_write_free(a);
		return;
	}
	archive_write_free(a);

	/* Re-create a write archive object since filenames should be written
	 * in UTF-8 by default. */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_pax(a));
	assertEqualInt(ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	entry = archive_entry_new2(a);
	archive_entry_set_pathname(entry, "\xD0\xD2\xC9");
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_size(entry, 0);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* Above three characters in KOI8-R should translate to the following
	 * three characters (two bytes each) in UTF-8. */
	assertEqualMem(buff + 512, "15 path=\xD0\xBF\xD1\x80\xD0\xB8\x0A", 15);
}

/*
 * Verify that CP1251 filenames are correctly translated to Unicode and UTF-8.
 */
DEFINE_TEST(test_pax_filename_encoding_CP1251)
{
  	struct archive *a;
  	struct archive_entry *entry;
	char buff[4096];
	size_t used;

	if (NULL == setlocale(LC_ALL, "Russian_Russia") &&
	    NULL == setlocale(LC_ALL, "ru_RU.CP1251")) {
		skipping("KOI8-R locale not available on this system.");
		return;
	}

	/* Check if the platform completely supports the string conversion. */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_pax(a));
	if (archive_write_set_options(a, "hdrcharset=UTF-8") != ARCHIVE_OK) {
		skipping("This system cannot convert character-set"
		    " from KOI8-R to UTF-8.");
		archive_write_free(a);
		return;
	}
	archive_write_free(a);

	/* Re-create a write archive object since filenames should be written
	 * in UTF-8 by default. */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_pax(a));
	assertEqualInt(ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	entry = archive_entry_new2(a);
	archive_entry_set_pathname(entry, "\xef\xf0\xe8");
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_size(entry, 0);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* Above three characters in KOI8-R should translate to the following
	 * three characters (two bytes each) in UTF-8. */
	assertEqualMem(buff + 512, "15 path=\xD0\xBF\xD1\x80\xD0\xB8\x0A", 15);
}

/*
 * Verify that EUC-JP filenames are correctly translated to Unicode and UTF-8.
 */
DEFINE_TEST(test_pax_filename_encoding_EUCJP)
{
  	struct archive *a;
  	struct archive_entry *entry;
	char buff[4096];
	size_t used;

	if (NULL == setlocale(LC_ALL, "ja_JP.eucJP")) {
		skipping("eucJP locale not available on this system.");
		return;
	}

	/* Check if the platform completely supports the string conversion. */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_pax(a));
	if (archive_write_set_options(a, "hdrcharset=UTF-8") != ARCHIVE_OK) {
		skipping("This system cannot convert character-set"
		    " from eucJP to UTF-8.");
		archive_write_free(a);
		return;
	}
	archive_write_free(a);

	/* Re-create a write archive object since filenames should be written
	 * in UTF-8 by default. */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_pax(a));
	assertEqualInt(ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	entry = archive_entry_new2(a);
	archive_entry_set_pathname(entry, "\xC9\xBD.txt");
	/* Check the Unicode version. */
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_size(entry, 0);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* Check UTF-8 version. */
	assertEqualMem(buff + 512, "16 path=\xE8\xA1\xA8.txt\x0A", 16);

}

/*
 * Verify that CP932/SJIS filenames are correctly translated to Unicode and UTF-8.
 */
DEFINE_TEST(test_pax_filename_encoding_CP932)
{
  	struct archive *a;
  	struct archive_entry *entry;
	char buff[4096];
	size_t used;

	if (NULL == setlocale(LC_ALL, "Japanese_Japan") &&
	    NULL == setlocale(LC_ALL, "ja_JP.SJIS")) {
		skipping("eucJP locale not available on this system.");
		return;
	}

	/* Check if the platform completely supports the string conversion. */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_pax(a));
	if (archive_write_set_options(a, "hdrcharset=UTF-8") != ARCHIVE_OK) {
		skipping("This system cannot convert character-set"
		    " from CP932/SJIS to UTF-8.");
		archive_write_free(a);
		return;
	}
	archive_write_free(a);

	/* Re-create a write archive object since filenames should be written
	 * in UTF-8 by default. */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_pax(a));
	assertEqualInt(ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	entry = archive_entry_new2(a);
	archive_entry_set_pathname(entry, "\x95\x5C.txt");
	/* Check the Unicode version. */
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_size(entry, 0);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* Check UTF-8 version. */
	assertEqualMem(buff + 512, "16 path=\xE8\xA1\xA8.txt\x0A", 16);

}

/*
 * Verify that KOI8-R filenames are not translated to Unicode and UTF-8
 * when using hdrcharset=BINARY option.
 */
DEFINE_TEST(test_pax_filename_encoding_KOI8R_BINARY)
{
  	struct archive *a;
  	struct archive_entry *entry;
	char buff[4096];
	size_t used;

	if (NULL == setlocale(LC_ALL, "ru_RU.KOI8-R")) {
		skipping("KOI8-R locale not available on this system.");
		return;
	}

	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_pax(a));
	/* BINARY mode should be accepted. */
	assertEqualInt(ARCHIVE_OK,
	    archive_write_set_options(a, "hdrcharset=BINARY"));
	assertEqualInt(ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	entry = archive_entry_new2(a);
	archive_entry_set_pathname(entry, "\xD0\xD2\xC9");
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_size(entry, 0);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* "hdrcharset=BINARY" pax attribute should be written. */
	assertEqualMem(buff + 512, "21 hdrcharset=BINARY\x0A", 21);
	/* Above three characters in KOI8-R should not translate to any
	 * character-set. */
	assertEqualMem(buff + 512+21, "12 path=\xD0\xD2\xC9\x0A", 12);
}

/*
 * Pax format writer only accepts both BINARY and UTF-8.
 * If other character-set name is specified, you will get ARCHIVE_FAILED.
 */
DEFINE_TEST(test_pax_filename_encoding_KOI8R_CP1251)
{
  	struct archive *a;

	if (NULL == setlocale(LC_ALL, "ru_RU.KOI8-R")) {
		skipping("KOI8-R locale not available on this system.");
		return;
	}

	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_pax(a));
	/* pax format writer only accepts both BINARY and UTF-8. */
	assertEqualInt(ARCHIVE_FAILED,
	    archive_write_set_options(a, "hdrcharset=CP1251"));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));
}


DEFINE_TEST(test_pax_filename_encoding)
{
	test_pax_filename_encoding_1();
	test_pax_filename_encoding_2();
	test_pax_filename_encoding_3();
}
