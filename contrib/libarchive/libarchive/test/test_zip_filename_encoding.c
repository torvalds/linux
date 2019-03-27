/*-
 * Copyright (c) 2011 Michihiro NAKAJIMA
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

DEFINE_TEST(test_zip_filename_encoding_UTF8)
{
  	struct archive *a;
  	struct archive_entry *entry;
	char buff[4096];
	size_t used;

	if (NULL == setlocale(LC_ALL, "en_US.UTF-8")) {
		skipping("en_US.UTF-8 locale not available on this system.");
		return;
	}

	/*
	 * Verify that UTF-8 filenames are correctly stored with
	 * hdrcharset=UTF-8 option.
	 */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_zip(a));
	if (archive_write_set_options(a, "hdrcharset=UTF-8") != ARCHIVE_OK) {
		skipping("This system cannot convert character-set"
		    " for UTF-8.");
		archive_write_free(a);
		return;
	}
	assertEqualInt(ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	entry = archive_entry_new2(a);
	/* Set a UTF-8 filename. */
	archive_entry_set_pathname(entry, "\xD0\xBF\xD1\x80\xD0\xB8");
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_size(entry, 0);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* A bit 11 of general purpose flag should be 0x08,
	 * which indicates the filename charset is UTF-8. */
	assertEqualInt(0x08, buff[7]);
	assertEqualMem(buff + 30, "\xD0\xBF\xD1\x80\xD0\xB8", 6);

	/*
	 * Verify that UTF-8 filenames are correctly stored without
	 * hdrcharset=UTF-8 option.
	 */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_zip(a));
	assertEqualInt(ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	entry = archive_entry_new2(a);
	/* Set a UTF-8 filename. */
	archive_entry_set_pathname(entry, "\xD0\xBF\xD1\x80\xD0\xB8");
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_size(entry, 0);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* A bit 11 of general purpose flag should be 0x08,
	 * which indicates the filename charset is UTF-8. */
	assertEqualInt(0x08, buff[7]);
	assertEqualMem(buff + 30, "\xD0\xBF\xD1\x80\xD0\xB8", 6);

	/*
	 * Verify that A bit 11 of general purpose flag is not set
	 * when ASCII filenames are stored.
	 */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_zip(a));
	assertEqualInt(ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	entry = archive_entry_new2(a);
	/* Set an ASCII filename. */
	archive_entry_set_pathname(entry, "abcABC");
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_size(entry, 0);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* A bit 11 of general purpose flag should be 0,
	 * which indicates the filename charset is unknown. */
	assertEqualInt(0, buff[7]);
	assertEqualMem(buff + 30, "abcABC", 6);
}

DEFINE_TEST(test_zip_filename_encoding_KOI8R)
{
  	struct archive *a;
  	struct archive_entry *entry;
	char buff[4096];
	size_t used;

	if (NULL == setlocale(LC_ALL, "ru_RU.KOI8-R")) {
		skipping("KOI8-R locale not available on this system.");
		return;
	}

	/*
	 * Verify that KOI8-R filenames are correctly translated to UTF-8.
	 */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_zip(a));
	if (archive_write_set_options(a, "hdrcharset=UTF-8") != ARCHIVE_OK) {
		skipping("This system cannot convert character-set"
		    " from KOI8-R to UTF-8.");
		archive_write_free(a);
		return;
	}
	assertEqualInt(ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	entry = archive_entry_new2(a);
	/* Set a KOI8-R filename. */
	archive_entry_set_pathname(entry, "\xD0\xD2\xC9");
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_size(entry, 0);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* A bit 11 of general purpose flag should be 0x08,
	 * which indicates the filename charset is UTF-8. */
	assertEqualInt(0x08, buff[7]);
	/* Above three characters in KOI8-R should translate to the following
	 * three characters (two bytes each) in UTF-8. */
	assertEqualMem(buff + 30, "\xD0\xBF\xD1\x80\xD0\xB8", 6);

	/*
	 * Verify that KOI8-R filenames are not translated to UTF-8.
	 */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_zip(a));
	assertEqualInt(ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	entry = archive_entry_new2(a);
	/* Set a KOI8-R filename. */
	archive_entry_set_pathname(entry, "\xD0\xD2\xC9");
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_size(entry, 0);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* A bit 11 of general purpose flag should be 0,
	 * which indicates the filename charset is unknown. */
	assertEqualInt(0, buff[7]);
	/* Above three characters in KOI8-R should not translate to
	 * any character-set. */
	assertEqualMem(buff + 30, "\xD0\xD2\xC9", 3);

	/*
	 * Verify that A bit 11 of general purpose flag is not set
	 * when ASCII filenames are stored even if hdrcharset=UTF-8
	 * is specified.
	 */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_zip(a));
	if (archive_write_set_options(a, "hdrcharset=UTF-8") != ARCHIVE_OK) {
		skipping("This system cannot convert character-set"
		    " from KOI8-R to UTF-8.");
		archive_write_free(a);
		return;
	}
	assertEqualInt(ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	entry = archive_entry_new2(a);
	/* Set an ASCII filename. */
	archive_entry_set_pathname(entry, "abcABC");
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_size(entry, 0);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* A bit 11 of general purpose flag should be 0,
	 * which indicates the filename charset is unknown. */
	assertEqualInt(0, buff[7]);
	assertEqualMem(buff + 30, "abcABC", 6);
}

/*
 * Do not translate CP1251 into CP866 if non Windows platform.
 */
DEFINE_TEST(test_zip_filename_encoding_ru_RU_CP1251)
{
  	struct archive *a;
  	struct archive_entry *entry;
	char buff[4096];
	size_t used;

	if (NULL == setlocale(LC_ALL, "ru_RU.CP1251")) {
		skipping("Russian_Russia locale not available on this system.");
		return;
	}

	/*
	 * Verify that CP1251 filenames are not translated into any
	 * other character-set, in particular, CP866.
	 */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_zip(a));
	assertEqualInt(ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	entry = archive_entry_new2(a);
	/* Set a CP1251 filename. */
	archive_entry_set_pathname(entry, "\xEF\xF0\xE8");
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_size(entry, 0);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* A bit 11 of general purpose flag should be 0,
	 * which indicates the filename charset is unknown. */
	assertEqualInt(0, buff[7]);
	/* Above three characters in CP1251 should not translate into
	 * any other character-set. */
	assertEqualMem(buff + 30, "\xEF\xF0\xE8", 3);
}

/*
 * Other archiver applications on Windows translate CP1251 filenames
 * into CP866 filenames and store it in the zip file.
 * Test above behavior works well.
 */
DEFINE_TEST(test_zip_filename_encoding_Russian_Russia)
{
  	struct archive *a;
  	struct archive_entry *entry;
	char buff[4096];
	size_t used;

	if (NULL == setlocale(LC_ALL, "Russian_Russia")) {
		skipping("Russian_Russia locale not available on this system.");
		return;
	}

	/*
	 * Verify that Russian_Russia(CP1251) filenames are correctly translated
	 * to UTF-8.
	 */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_zip(a));
	if (archive_write_set_options(a, "hdrcharset=UTF-8") != ARCHIVE_OK) {
		skipping("This system cannot convert character-set"
		    " from Russian_Russia.CP1251 to UTF-8.");
		archive_write_free(a);
		return;
	}
	assertEqualInt(ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	entry = archive_entry_new2(a);
	/* Set a CP1251 filename. */
	archive_entry_set_pathname(entry, "\xEF\xF0\xE8");
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_size(entry, 0);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* A bit 11 of general purpose flag should be 0x08,
	 * which indicates the filename charset is UTF-8. */
	assertEqualInt(0x08, buff[7]);
	/* Above three characters in CP1251 should translate to the following
	 * three characters (two bytes each) in UTF-8. */
	assertEqualMem(buff + 30, "\xD0\xBF\xD1\x80\xD0\xB8", 6);

	/*
	 * Verify that Russian_Russia(CP1251) filenames are correctly translated
	 * to CP866.
	 */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_zip(a));
	assertEqualInt(ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	entry = archive_entry_new2(a);
	/* Set a CP1251 filename. */
	archive_entry_set_pathname(entry, "\xEF\xF0\xE8");
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_size(entry, 0);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* A bit 11 of general purpose flag should be 0,
	 * which indicates the filename charset is unknown. */
	assertEqualInt(0, buff[7]);
	/* Above three characters in CP1251 should translate to the following
	 * three characters in CP866. */
	assertEqualMem(buff + 30, "\xAF\xE0\xA8", 3);
}

DEFINE_TEST(test_zip_filename_encoding_EUCJP)
{
  	struct archive *a;
  	struct archive_entry *entry;
	char buff[4096];
	size_t used;

	if (NULL == setlocale(LC_ALL, "ja_JP.eucJP")) {
		skipping("eucJP locale not available on this system.");
		return;
	}

	/*
	 * Verify that EUC-JP filenames are correctly translated to UTF-8.
	 */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_zip(a));
	if (archive_write_set_options(a, "hdrcharset=UTF-8") != ARCHIVE_OK) {
		skipping("This system cannot convert character-set"
		    " from eucJP to UTF-8.");
		archive_write_free(a);
		return;
	}
	assertEqualInt(ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	entry = archive_entry_new2(a);
	/* Set an EUC-JP filename. */
	archive_entry_set_pathname(entry, "\xC9\xBD.txt");
	/* Check the Unicode version. */
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_size(entry, 0);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* A bit 11 of general purpose flag should be 0x08,
	 * which indicates the filename charset is UTF-8. */
	assertEqualInt(0x08, buff[7]);
	/* Check UTF-8 version. */
	assertEqualMem(buff + 30, "\xE8\xA1\xA8.txt", 7);

	/*
	 * Verify that EUC-JP filenames are not translated to UTF-8.
	 */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_zip(a));
	assertEqualInt(ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	entry = archive_entry_new2(a);
	/* Set an EUC-JP filename. */
	archive_entry_set_pathname(entry, "\xC9\xBD.txt");
	/* Check the Unicode version. */
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_size(entry, 0);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* A bit 11 of general purpose flag should be 0,
	 * which indicates the filename charset is unknown. */
	assertEqualInt(0, buff[7]);
	/* Above three characters in EUC-JP should not translate to
	 * any character-set. */
	assertEqualMem(buff + 30, "\xC9\xBD.txt", 6);

	/*
	 * Verify that A bit 11 of general purpose flag is not set
	 * when ASCII filenames are stored even if hdrcharset=UTF-8
	 * is specified.
	 */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_zip(a));
	if (archive_write_set_options(a, "hdrcharset=UTF-8") != ARCHIVE_OK) {
		skipping("This system cannot convert character-set"
		    " from eucJP to UTF-8.");
		archive_write_free(a);
		return;
	}
	assertEqualInt(ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	entry = archive_entry_new2(a);
	/* Set an ASCII filename. */
	archive_entry_set_pathname(entry, "abcABC");
	/* Check the Unicode version. */
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_size(entry, 0);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* A bit 11 of general purpose flag should be 0,
	 * which indicates the filename charset is unknown. */
	assertEqualInt(0, buff[7]);
	assertEqualMem(buff + 30, "abcABC", 6);
}

DEFINE_TEST(test_zip_filename_encoding_CP932)
{
  	struct archive *a;
  	struct archive_entry *entry;
	char buff[4096];
	size_t used;

	if (NULL == setlocale(LC_ALL, "Japanese_Japan") &&
	    NULL == setlocale(LC_ALL, "ja_JP.SJIS")) {
		skipping("CP932/SJIS locale not available on this system.");
		return;
	}

	/*
	 * Verify that EUC-JP filenames are correctly translated to UTF-8.
	 */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_zip(a));
	if (archive_write_set_options(a, "hdrcharset=UTF-8") != ARCHIVE_OK) {
		skipping("This system cannot convert character-set"
		    " from CP932/SJIS to UTF-8.");
		archive_write_free(a);
		return;
	}
	assertEqualInt(ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	entry = archive_entry_new2(a);
	/* Set a CP932/SJIS filename. */
	archive_entry_set_pathname(entry, "\x95\x5C.txt");
	/* Check the Unicode version. */
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_size(entry, 0);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* A bit 11 of general purpose flag should be 0x08,
	 * which indicates the filename charset is UTF-8. */
	assertEqualInt(0x08, buff[7]);
	/* Check UTF-8 version. */
	assertEqualMem(buff + 30, "\xE8\xA1\xA8.txt", 7);

	/*
	 * Verify that CP932/SJIS filenames are not translated to UTF-8.
	 */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_zip(a));
	assertEqualInt(ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	entry = archive_entry_new2(a);
	/* Set a CP932/SJIS filename. */
	archive_entry_set_pathname(entry, "\x95\x5C.txt");
	/* Check the Unicode version. */
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_size(entry, 0);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* A bit 11 of general purpose flag should be 0,
	 * which indicates the filename charset is unknown. */
	assertEqualInt(0, buff[7]);
	/* Above three characters in CP932/SJIS should not translate to
	 * any character-set. */
	assertEqualMem(buff + 30, "\x95\x5C.txt", 6);

	/*
	 * Verify that A bit 11 of general purpose flag is not set
	 * when ASCII filenames are stored even if hdrcharset=UTF-8
	 * is specified.
	 */
	a = archive_write_new();
	assertEqualInt(ARCHIVE_OK, archive_write_set_format_zip(a));
	if (archive_write_set_options(a, "hdrcharset=UTF-8") != ARCHIVE_OK) {
		skipping("This system cannot convert character-set"
		    " from CP932/SJIS to UTF-8.");
		archive_write_free(a);
		return;
	}
	assertEqualInt(ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	entry = archive_entry_new2(a);
	/* Set an ASCII filename. */
	archive_entry_set_pathname(entry, "abcABC");
	/* Check the Unicode version. */
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_size(entry, 0);
	assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* A bit 11 of general purpose flag should be 0,
	 * which indicates the filename charset is unknown. */
	assertEqualInt(0, buff[7]);
	assertEqualMem(buff + 30, "abcABC", 6);
}
