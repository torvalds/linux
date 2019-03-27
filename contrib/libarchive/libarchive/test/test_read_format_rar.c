/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * Copyright (c) 2011 Andres Mejia
 * Copyright (c) 2011-2012 Michihiro NAKAJIMA
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

#include <locale.h>

DEFINE_TEST(test_read_format_rar_basic)
{
  char buff[64];
  const char reffile[] = "test_read_format_rar.rar";
  const char test_txt[] = "test text document\r\n";
  int size = sizeof(test_txt)-1;
  struct archive_entry *ae;
  struct archive *a;

  extract_reference_file(reffile);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_support_filter_all(a));
  assertA(0 == archive_read_support_format_all(a));
  assertA(0 == archive_read_open_filename(a, reffile, 10240));

  /* First header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("test.txt", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(20, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertA(size == archive_read_data(a, buff, size));
  assertEqualMem(buff, test_txt, size);
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Second header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testlink", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(41471, archive_entry_mode(ae));
  assertEqualString("test.txt", archive_entry_symlink(ae));
  assertEqualIntA(a, 0, archive_read_data(a, buff, sizeof(buff)));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Third header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir/test.txt", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(20, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertA(size == archive_read_data(a, buff, size));
  assertEqualMem(buff, test_txt, size);
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Fourth header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Fifth header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testemptydir", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Test EOF */
  assertA(1 == archive_read_next_header(a, &ae));
  assertEqualInt(5, archive_file_count(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_format_rar_subblock)
{
  char buff[64];
  const char reffile[] = "test_read_format_rar_subblock.rar";
  const char test_txt[] = "test text document\r\n";
  int size = sizeof(test_txt)-1;
  struct archive_entry *ae;
  struct archive *a;

  extract_reference_file(reffile);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_support_filter_all(a));
  assertA(0 == archive_read_support_format_all(a));
  assertA(0 == archive_read_open_filename(a, reffile, 10240));

  /* First header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("test.txt", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(20, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertA(size == archive_read_data(a, buff, size));
  assertEqualMem(buff, test_txt, size);
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Test EOF */
  assertA(1 == archive_read_next_header(a, &ae));
  assertEqualInt(1, archive_file_count(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_format_rar_noeof)
{
  char buff[64];
  const char reffile[] = "test_read_format_rar_noeof.rar";
  const char test_txt[] = "test text document\r\n";
  int size = sizeof(test_txt)-1;
  struct archive_entry *ae;
  struct archive *a;

  extract_reference_file(reffile);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_support_filter_all(a));
  assertA(0 == archive_read_support_format_all(a));
  assertA(0 == archive_read_open_filename(a, reffile, 10240));

  /* First header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("test.txt", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(20, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertA(size == archive_read_data(a, buff, size));
  assertEqualMem(buff, test_txt, size);
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Test EOF */
  assertA(1 == archive_read_next_header(a, &ae));
  assertEqualInt(1, archive_file_count(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_format_rar_unicode_UTF8)
{
  char buff[30];
  const char reffile[] = "test_read_format_rar_unicode.rar";
  const char test_txt[] = "kanji";
  struct archive_entry *ae;
  struct archive *a;

  if (NULL == setlocale(LC_ALL, "en_US.UTF-8")) {
	skipping("en_US.UTF-8 locale not available on this system.");
	return;
  }

  extract_reference_file(reffile);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_support_filter_all(a));
  assertA(0 == archive_read_support_format_all(a));
  assertA(0 == archive_read_open_filename(a, reffile, 10240));

  /* First header. */
  assertA(0 == archive_read_next_header(a, &ae));
#if defined(__APPLE__)
#define f1name "\xE8\xA1\xA8\xE3\x81\x9F\xE3\x82\x99\xE3\x82\x88/"\
      "\xE6\x96\xB0\xE3\x81\x97\xE3\x81\x84\xE3\x83\x95\xE3\x82\xA9"\
      "\xE3\x83\xAB\xE3\x82\xBF\xE3\x82\x99/\xE6\x96\xB0\xE8\xA6\x8F"\
      "\xE3\x83\x86\xE3\x82\xAD\xE3\x82\xB9\xE3\x83\x88 "\
      "\xE3\x83\x88\xE3\x82\x99\xE3\x82\xAD\xE3\x83\xA5\xE3\x83\xA1"\
      "\xE3\x83\xB3\xE3\x83\x88.txt" /* NFD */
#else
#define f1name "\xE8\xA1\xA8\xE3\x81\xA0\xE3\x82\x88/"\
      "\xE6\x96\xB0\xE3\x81\x97\xE3\x81\x84\xE3\x83\x95\xE3\x82\xA9"\
      "\xE3\x83\xAB\xE3\x83\x80/\xE6\x96\xB0\xE8\xA6\x8F"\
      "\xE3\x83\x86\xE3\x82\xAD\xE3\x82\xB9\xE3\x83\x88 "\
      "\xE3\x83\x89\xE3\x82\xAD\xE3\x83\xA5\xE3\x83\xA1"\
      "\xE3\x83\xB3\xE3\x83\x88.txt" /* NFC */
#endif
  assertEqualUTF8String(f1name, archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));

  /* Second header. */
  assertA(0 == archive_read_next_header(a, &ae));
#if defined(__APPLE__)
#define f2name "\xE8\xA1\xA8\xE3\x81\x9F\xE3\x82\x99\xE3\x82\x88/"\
      "\xE6\xBC\xA2\xE5\xAD\x97\xE9\x95\xB7\xE3\x81\x84\xE3\x83\x95"\
      "\xE3\x82\xA1\xE3\x82\xA4\xE3\x83\xAB\xE5\x90\x8Dlong-filename-in-"\
      "\xE6\xBC\xA2\xE5\xAD\x97.txt" /* NFD */
#else
#define f2name "\xE8\xA1\xA8\xE3\x81\xA0\xE3\x82\x88/"\
      "\xE6\xBC\xA2\xE5\xAD\x97\xE9\x95\xB7\xE3\x81\x84\xE3\x83\x95"\
      "\xE3\x82\xA1\xE3\x82\xA4\xE3\x83\xAB\xE5\x90\x8Dlong-filename-in-"\
      "\xE6\xBC\xA2\xE5\xAD\x97.txt" /* NFC */
#endif
  assertEqualUTF8String(f2name, archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertEqualInt(5, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualIntA(a, 5, archive_read_data(a, buff, 5));
  assertEqualMem(buff, test_txt, 5);
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Third header. */
  assertA(0 == archive_read_next_header(a, &ae));
#if defined(__APPLE__)
#define f3name "\xE8\xA1\xA8\xE3\x81\x9F\xE3\x82\x99\xE3\x82\x88/"\
      "\xE6\x96\xB0\xE3\x81\x97\xE3\x81\x84\xE3\x83\x95\xE3\x82"\
      "\xA9\xE3\x83\xAB\xE3\x82\xBF\xE3\x82\x99" /* NFD */
#else
#define f3name "\xE8\xA1\xA8\xE3\x81\xA0\xE3\x82\x88/"\
      "\xE6\x96\xB0\xE3\x81\x97\xE3\x81\x84\xE3\x83\x95\xE3\x82"\
      "\xA9\xE3\x83\xAB\xE3\x83\x80" /* NFC */
#endif
  assertEqualUTF8String(f3name, archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));

  /* Fourth header. */
  assertA(0 == archive_read_next_header(a, &ae));
#if defined(__APPLE__)
#define f4name "\xE8\xA1\xA8\xE3\x81\x9F\xE3\x82\x99\xE3\x82\x88" /* NFD */
#else
#define f4name "\xE8\xA1\xA8\xE3\x81\xA0\xE3\x82\x88" /* NFC */
#endif
  assertEqualUTF8String(f4name, archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));

  /* Fifth header, which has a symbolic-link name in multi-byte characters. */
  assertA(0 == archive_read_next_header(a, &ae));
#if defined(__APPLE__)
#define f5name "\xE8\xA1\xA8\xE3\x81\x9F\xE3\x82\x99\xE3\x82\x88/"\
      "\xE3\x83\x95\xE3\x82\xA1\xE3\x82\xA4\xE3\x83\xAB" /* NFD */
#else
#define f5name "\xE8\xA1\xA8\xE3\x81\xA0\xE3\x82\x88/"\
      "\xE3\x83\x95\xE3\x82\xA1\xE3\x82\xA4\xE3\x83\xAB" /* NFC */
#endif
  assertEqualUTF8String(f5name, archive_entry_pathname(ae));
  assertEqualUTF8String(
      "\xE6\xBC\xA2\xE5\xAD\x97\xE9\x95\xB7\xE3\x81\x84\xE3\x83\x95"
      "\xE3\x82\xA1\xE3\x82\xA4\xE3\x83\xAB\xE5\x90\x8Dlong-filename-in-"
      "\xE6\xBC\xA2\xE5\xAD\x97.txt", archive_entry_symlink(ae));
  assertA((int)archive_entry_mtime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(41453, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertEqualIntA(a, 0, archive_read_data(a, buff, sizeof(buff)));

  /* Sixth header */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualUTF8String(
    "abcdefghijklmnopqrs\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88.txt",
    archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertEqualInt(16, archive_entry_size(ae));
  assertEqualInt(33204, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertEqualIntA(a, 16, archive_read_data(a, buff, sizeof(buff)));

  /* Test EOF */
  assertA(1 == archive_read_next_header(a, &ae));
  assertEqualInt(6, archive_file_count(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_format_rar_unicode_CP932)
{
  char buff[30];
  const char reffile[] = "test_read_format_rar_unicode.rar";
  const char test_txt[] = "kanji";
  struct archive_entry *ae;
  struct archive *a;

  if (NULL == setlocale(LC_ALL, "Japanese_Japan") &&
    NULL == setlocale(LC_ALL, "ja_JP.SJIS")) {
	skipping("CP932 locale not available on this system.");
	return;
  }

  extract_reference_file(reffile);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_support_filter_all(a));
  assertA(0 == archive_read_support_format_all(a));
  /* Specify the charset of symbolic-link file name. */
  if (ARCHIVE_OK != archive_read_set_options(a, "rar:hdrcharset=UTF-8")) {
	skipping("This system cannot convert character-set"
	    " from UTF-8 to CP932.");
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	return;
  }
  assertA(0 == archive_read_open_filename(a, reffile, 10240));

  /* First header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("\x95\x5c\x82\xbe\x82\xe6/\x90\x56\x82\xb5\x82\xa2"
      "\x83\x74\x83\x48\x83\x8b\x83\x5f/\x90\x56\x8b\x4b\x83\x65\x83\x4c"
      "\x83\x58\x83\x67 \x83\x68\x83\x4c\x83\x85\x83\x81\x83\x93\x83\x67.txt",
      archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Second header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("\x95\x5c\x82\xbe\x82\xe6/\x8a\xbf\x8e\x9a"
      "\x92\xb7\x82\xa2\x83\x74\x83\x40\x83\x43\x83\x8b\x96\xbc\x6c"
      "\x6f\x6e\x67\x2d\x66\x69\x6c\x65\x6e\x61\x6d\x65\x2d\x69\x6e"
      "\x2d\x8a\xbf\x8e\x9a.txt", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertEqualInt(5, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertA(5 == archive_read_data(a, buff, 5));
  assertEqualMem(buff, test_txt, 5);

  /* Third header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("\x95\x5c\x82\xbe\x82\xe6/"
      "\x90\x56\x82\xb5\x82\xa2\x83\x74\x83\x48\x83\x8b\x83\x5f",
      archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Fourth header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("\x95\x5c\x82\xbe\x82\xe6", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Fifth header, which has a symbolic-link name in multi-byte characters. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("\x95\x5c\x82\xbe\x82\xe6/"
      "\x83\x74\x83\x40\x83\x43\x83\x8B", archive_entry_pathname(ae));
  assertEqualString("\x8a\xbf\x8e\x9a"
      "\x92\xb7\x82\xa2\x83\x74\x83\x40\x83\x43\x83\x8b\x96\xbc\x6c"
      "\x6f\x6e\x67\x2d\x66\x69\x6c\x65\x6e\x61\x6d\x65\x2d\x69\x6e"
      "\x2d\x8a\xbf\x8e\x9a.txt", archive_entry_symlink(ae));
  assertA((int)archive_entry_mtime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(41453, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertEqualIntA(a, 0, archive_read_data(a, buff, sizeof(buff)));

  /* Sixth header */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualUTF8String(
    "abcdefghijklmnopqrs\x83\x65\x83\x58\x83\x67.txt",
    archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertEqualInt(16, archive_entry_size(ae));
  assertEqualInt(33204, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertEqualIntA(a, 16, archive_read_data(a, buff, sizeof(buff)));

  /* Test EOF */
  assertA(1 == archive_read_next_header(a, &ae));
  assertEqualInt(6, archive_file_count(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_format_rar_compress_normal)
{
  const char reffile[] = "test_read_format_rar_compress_normal.rar";
  char file1_buff[20111];
  int file1_size = sizeof(file1_buff);
  const char file1_test_txt[] = "<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                                "</P>\n"
                                "</BODY>\n"
                                "</HTML>";
  char file2_buff[20];
  int file2_size = sizeof(file2_buff);
  const char file2_test_txt[] = "test text document\r\n";
  struct archive_entry *ae;
  struct archive *a;

  extract_reference_file(reffile);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_support_filter_all(a));
  assertA(0 == archive_read_support_format_all(a));
  assertA(0 == archive_read_open_filename(a, reffile, 10240));

  /* First header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("LibarchiveAddingTest.html", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(file1_size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertA(file1_size == archive_read_data(a, file1_buff, file1_size));
  assertEqualMem(&file1_buff[file1_size - sizeof(file1_test_txt) + 1],
                 file1_test_txt, sizeof(file1_test_txt) - 1);

    /* Second header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testlink", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(41471, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertEqualString("LibarchiveAddingTest.html", archive_entry_symlink(ae));
  assertEqualIntA(a, 0, archive_read_data(a, file1_buff, 30));

  /* Third header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir/test.txt", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(file2_size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertA(file2_size == archive_read_data(a, file2_buff, file2_size));
  assertEqualMem(&file2_buff[file2_size + 1 - sizeof(file2_test_txt)],
                 file2_test_txt, sizeof(file2_test_txt) - 1);

  /* Fourth header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir/LibarchiveAddingTest.html",
                    archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(file1_size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertA(file1_size == archive_read_data(a, file1_buff, file1_size));
  assertEqualMem(&file1_buff[file1_size - sizeof(file1_test_txt) + 1],
                 file1_test_txt, sizeof(file1_test_txt) - 1);

  /* Fifth header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Sixth header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testemptydir", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Test EOF */
  assertA(1 == archive_read_next_header(a, &ae));
  assertEqualInt(6, archive_file_count(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

/* This test is for sufficiently large files that would have been compressed
 * using multiple lzss blocks.
 */
DEFINE_TEST(test_read_format_rar_multi_lzss_blocks)
{
  const char reffile[] = "test_read_format_rar_multi_lzss_blocks.rar";
  const char test_txt[] = "-bottom: 0in\"><BR>\n</P>\n</BODY>\n</HTML>";
  int size = 20131111, offset = 0;
  char buff[64];
  struct archive_entry *ae;
  struct archive *a;

  extract_reference_file(reffile);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_support_filter_all(a));
  assertA(0 == archive_read_support_format_all(a));
  assertA(0 == archive_read_open_filename(a, reffile, 10240));

  /* First header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("multi_lzss_blocks_test.txt", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  while (offset + (int)sizeof(buff) < size)
  {
    assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
    offset += sizeof(buff);
  }
  assertA(size - offset == archive_read_data(a, buff, size - offset));
  assertEqualMem(buff, test_txt, size - offset);

  /* Test EOF */
  assertA(1 == archive_read_next_header(a, &ae));
  assertEqualInt(1, archive_file_count(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_format_rar_compress_best)
{
  const char reffile[] = "test_read_format_rar_compress_best.rar";
  char file1_buff[20111];
  int file1_size = sizeof(file1_buff);
  const char file1_test_txt[] = "<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                                "</P>\n"
                                "</BODY>\n"
                                "</HTML>";
  char file2_buff[20];
  int file2_size = sizeof(file2_buff);
  const char file2_test_txt[] = "test text document\r\n";
  struct archive_entry *ae;
  struct archive *a;

  extract_reference_file(reffile);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_support_filter_all(a));
  assertA(0 == archive_read_support_format_all(a));
  assertA(0 == archive_read_open_filename(a, reffile, 10240));

  /* First header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("LibarchiveAddingTest.html", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(file1_size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertA(file1_size == archive_read_data(a, file1_buff, file1_size));
  assertEqualMem(&file1_buff[file1_size - sizeof(file1_test_txt) + 1],
                 file1_test_txt, sizeof(file1_test_txt) - 1);

    /* Second header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testlink", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(41471, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertEqualString("LibarchiveAddingTest.html", archive_entry_symlink(ae));
  assertEqualIntA(a, 0, archive_read_data(a, file1_buff, 30));

  /* Third header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir/test.txt", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(file2_size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertA(file2_size == archive_read_data(a, file2_buff, file2_size));
  assertEqualMem(&file2_buff[file2_size + 1 - sizeof(file2_test_txt)],
                 file2_test_txt, sizeof(file2_test_txt) - 1);

  /* Fourth header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir/LibarchiveAddingTest.html",
                    archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(file1_size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertA(file1_size == archive_read_data(a, file1_buff, file1_size));
  assertEqualMem(&file1_buff[file1_size - sizeof(file1_test_txt) + 1],
                 file1_test_txt, sizeof(file1_test_txt) - 1);

  /* Fifth header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Sixth header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testemptydir", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Test EOF */
  assertA(1 == archive_read_next_header(a, &ae));
  assertEqualInt(6, archive_file_count(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

/* This is a test for RAR files compressed using a technique where compression
 * switches back and forth to and from ppmd and lzss decoding.
 */
DEFINE_TEST(test_read_format_rar_ppmd_lzss_conversion)
{
  const char reffile[] = "test_read_format_rar_ppmd_lzss_conversion.rar";
  const char test_txt[] = "gin-bottom: 0in\"><BR>\n</P>\n</BODY>\n</HTML>";
  int size = 241647978, offset = 0;
  char buff[64];
  struct archive_entry *ae;
  struct archive *a;

  extract_reference_file(reffile);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_support_filter_all(a));
  assertA(0 == archive_read_support_format_all(a));
  assertA(0 == archive_read_open_filename(a, reffile, 10240));

  /* First header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("ppmd_lzss_conversion_test.txt",
                    archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  while (offset + (int)sizeof(buff) < size)
  {
    assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
    offset += sizeof(buff);
  }
  assertA(size - offset == archive_read_data(a, buff, size - offset));
  assertEqualMem(buff, test_txt, size - offset);

  /* Test EOF */
  assertA(1 == archive_read_next_header(a, &ae));
  assertEqualInt(1, archive_file_count(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_format_rar_binary)
{
  const char reffile[] = "test_read_format_rar_binary_data.rar";
  char *file1_buff = malloc(1048576);
  int file1_size = 1048576;
  const char file1_test_txt[] = "\x37\xef\xb2\xbe\x33\xf6\xcc\xcb\xee\x2a\x10"
                                "\x9d\x2e\x01\xe9\xf6\xf9\xe5\xe6\x67\x0c\x2b"
                                "\xd8\x6b\xa0\x26\x9a\xf7\x93\x87\x42\xf1\x08"
                                "\x42\xdc\x9b\x76\x91\x20\xa4\x01\xbe\x67\xbd"
                                "\x08\x74\xde\xec";
  char file2_buff[32618];
  int file2_size = sizeof(file2_buff);
  const char file2_test_txt[] = "\x00\xee\x78\x00\x00\x4d\x45\x54\x41\x2d\x49"
                                "\x4e\x46\x2f\x6d\x61\x6e\x69\x66\x65\x73\x74"
                                "\x2e\x78\x6d\x6c\x50\x4b\x05\x06\x00\x00\x00"
                                "\x00\x12\x00\x12\x00\xaa\x04\x00\x00\xaa\x7a"
                                "\x00\x00\x00\x00";
  struct archive_entry *ae;
  struct archive *a;

  extract_reference_file(reffile);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_support_filter_all(a));
  assertA(0 == archive_read_support_format_all(a));
  assertA(0 == archive_read_open_filename(a, reffile, 10240));

  /* First header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("random_data.bin", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(file1_size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertA(file1_size == archive_read_data(a, file1_buff, file1_size));
  assertEqualMem(&file1_buff[file1_size - sizeof(file1_test_txt) + 1],
                 file1_test_txt, sizeof(file1_test_txt) - 1);

    /* Second header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("LibarchiveAddingTest.odt", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(file2_size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertA(file2_size == archive_read_data(a, file2_buff, file2_size));
  assertEqualMem(&file2_buff[file2_size + 1 - sizeof(file2_test_txt)],
                 file2_test_txt, sizeof(file2_test_txt) - 1);

  /* Test EOF */
  assertA(1 == archive_read_next_header(a, &ae));
  assertEqualInt(2, archive_file_count(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));

  free(file1_buff);
}

DEFINE_TEST(test_read_format_rar_windows)
{
  char buff[441];
  const char reffile[] = "test_read_format_rar_windows.rar";
  const char test_txt[] = "test text file\r\n";
  int size = sizeof(test_txt)-1;
  struct archive_entry *ae;
  struct archive *a;

  extract_reference_file(reffile);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_support_filter_all(a));
  assertA(0 == archive_read_support_format_all(a));
  assertA(0 == archive_read_open_filename(a, reffile, 10240));

  /* First header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir/test.txt", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(16, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertA(size == archive_read_data(a, buff, size));
  assertEqualMem(buff, test_txt, size);

  /* Second header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("test.txt", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(16, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertA(size == archive_read_data(a, buff, size));
  assertEqualMem(buff, test_txt, size);

  /* Third header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testshortcut.lnk", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(sizeof(buff), archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));

  /* Fourth header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Fifth header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testemptydir", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Test EOF */
  assertA(1 == archive_read_next_header(a, &ae));
  assertEqualInt(5, archive_file_count(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_format_rar_multivolume)
{
  const char *reffiles[] =
  {
    "test_read_format_rar_multivolume.part0001.rar",
    "test_read_format_rar_multivolume.part0002.rar",
    "test_read_format_rar_multivolume.part0003.rar",
    "test_read_format_rar_multivolume.part0004.rar",
    NULL
  };
  int file1_size = 241647978, offset = 0;
  char buff[64];
  const char file1_test_txt[] = "gin-bottom: 0in\"><BR>\n</P>\n</BODY>\n"
                                "</HTML>";
  char file2_buff[20111];
  int file2_size = sizeof(file2_buff);
  const char file2_test_txt[] = "<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                                "</P>\n"
                                "</BODY>\n"
                                "</HTML>";
  char file3_buff[20];
  int file3_size = sizeof(file3_buff);
  const char file3_test_txt[] = "test text document\r\n";
  struct archive_entry *ae;
  struct archive *a;

  extract_reference_files(reffiles);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_support_filter_all(a));
  assertA(0 == archive_read_support_format_all(a));
  assertA(0 == archive_read_open_filenames(a, reffiles, 10240));

  /* First header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("ppmd_lzss_conversion_test.txt",
                    archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(file1_size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  while (offset + (int)sizeof(buff) < file1_size)
  {
    assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
    offset += sizeof(buff);
  }
  assertA(file1_size - offset ==
    archive_read_data(a, buff, file1_size - offset));
  assertEqualMem(buff, file1_test_txt, file1_size - offset);

  /* Second header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("LibarchiveAddingTest.html", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(file2_size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertA(file2_size == archive_read_data(a, file2_buff, file2_size));
  assertEqualMem(&file2_buff[file2_size - sizeof(file2_test_txt) + 1],
                 file2_test_txt, sizeof(file2_test_txt) - 1);

  /* Third header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testlink", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(41471, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertEqualString("LibarchiveAddingTest.html", archive_entry_symlink(ae));
  assertEqualIntA(a, 0, archive_read_data(a, file2_buff, 30));

  /* Fourth header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir/test.txt", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(file3_size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertA(file3_size == archive_read_data(a, file3_buff, file3_size));
  assertEqualMem(&file3_buff[file3_size + 1 - sizeof(file3_test_txt)],
                 file3_test_txt, sizeof(file3_test_txt) - 1);

  /* Fifth header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir/LibarchiveAddingTest.html",
                    archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(file2_size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertA(file2_size == archive_read_data(a, file2_buff, file2_size));
  assertEqualMem(&file2_buff[file2_size - sizeof(file2_test_txt) + 1],
                 file2_test_txt, sizeof(file2_test_txt) - 1);

  /* Sixth header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Seventh header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testemptydir", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Test EOF */
  assertA(1 == archive_read_next_header(a, &ae));
  assertEqualInt(7, archive_file_count(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_format_rar_multivolume_skip)
{
  const char *reffiles[] =
  {
    "test_read_format_rar_multivolume.part0001.rar",
    "test_read_format_rar_multivolume.part0002.rar",
    "test_read_format_rar_multivolume.part0003.rar",
    "test_read_format_rar_multivolume.part0004.rar",
    NULL
  };
  int file1_size = 241647978;
  int file2_size = 20111;
  int file3_size = 20;
  struct archive_entry *ae;
  struct archive *a;

  extract_reference_files(reffiles);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_support_filter_all(a));
  assertA(0 == archive_read_support_format_all(a));
  assertA(0 == archive_read_open_filenames(a, reffiles, 10240));

  /* First header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("ppmd_lzss_conversion_test.txt",
                    archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(file1_size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Second header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("LibarchiveAddingTest.html", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(file2_size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Third header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testlink", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(41471, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertEqualString("LibarchiveAddingTest.html", archive_entry_symlink(ae));

  /* Fourth header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir/test.txt", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(file3_size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Fifth header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir/LibarchiveAddingTest.html",
                    archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(file2_size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Sixth header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Seventh header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testemptydir", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Test EOF */
  assertA(1 == archive_read_next_header(a, &ae));
  assertEqualInt(7, archive_file_count(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_format_rar_sfx)
{
  char buff[441];
  const char reffile[] = "test_read_format_rar_sfx.exe";
  const char test_txt[] = "test text file\r\n";
  int size = sizeof(test_txt)-1;
  struct archive_entry *ae;
  struct archive *a;

  extract_reference_file(reffile);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_support_filter_all(a));
  assertA(0 == archive_read_support_format_all(a));
  assertA(0 == archive_read_open_filename(a, reffile, 10240));

  /* First header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("test.txt", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(16, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertA(size == archive_read_data(a, buff, size));
  assertEqualMem(buff, test_txt, size);

  /* Second header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testshortcut.lnk", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(sizeof(buff), archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));

  /* Third header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir/test.txt", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(16, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertA(size == archive_read_data(a, buff, size));
  assertEqualMem(buff, test_txt, size);

  /* Fourth header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Fifth header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testemptydir", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Test EOF */
  assertA(1 == archive_read_next_header(a, &ae));
  assertEqualInt(5, archive_file_count(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_format_rar_multivolume_stored_file)
{
  const char *reffiles[] =
  {
    "test_rar_multivolume_single_file.part1.rar",
    "test_rar_multivolume_single_file.part2.rar",
    "test_rar_multivolume_single_file.part3.rar",
    NULL
  };
  char file_buff[20111];
  int file_size = sizeof(file_buff);
  const char file_test_txt[] = "<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                                "</P>\n"
                                "</BODY>\n"
                                "</HTML>";
  struct archive_entry *ae;
  struct archive *a;

  extract_reference_files(reffiles);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_support_filter_all(a));
  assertA(0 == archive_read_support_format_all(a));
  assertA(0 == archive_read_open_filenames(a, reffiles, 10240));

  /* First header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("LibarchiveAddingTest.html", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(file_size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertA(file_size == archive_read_data(a, file_buff, file_size));
  assertEqualMem(&file_buff[file_size - sizeof(file_test_txt) + 1],
                 file_test_txt, sizeof(file_test_txt) - 1);

  /* Test EOF */
  assertA(1 == archive_read_next_header(a, &ae));
  assertEqualInt(1, archive_file_count(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_format_rar_multivolume_stored_file_skip)
{
  const char *reffiles[] =
  {
    "test_rar_multivolume_single_file.part1.rar",
    "test_rar_multivolume_single_file.part2.rar",
    "test_rar_multivolume_single_file.part3.rar",
    NULL
  };
  int file_size = 20111;
  struct archive_entry *ae;
  struct archive *a;

  extract_reference_files(reffiles);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_support_filter_all(a));
  assertA(0 == archive_read_support_format_all(a));
  assertA(0 == archive_read_open_filenames(a, reffiles, 10240));

  /* First header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("LibarchiveAddingTest.html", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(file_size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Test EOF */
  assertA(1 == archive_read_next_header(a, &ae));
  assertEqualInt(1, archive_file_count(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_format_rar_multivolume_seek_data)
{
  const char *reffiles[] =
  {
    "test_rar_multivolume_single_file.part1.rar",
    "test_rar_multivolume_single_file.part2.rar",
    "test_rar_multivolume_single_file.part3.rar",
    NULL
  };
  char buff[64];
  int file_size = 20111;
  const char file_test_txt1[] = "d. \n</P>\n<P STYLE=\"margin-bottom: 0in\">"
                                "<BR>\n</P>\n</BODY>\n</HTML>";
  const char file_test_txt2[] = "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4."
                                "0 Transitional//EN\">\n<";
  const char file_test_txt3[] = "mplify writing such tests,\ntry to use plat"
                                "form-independent codin";
  const char file_test_txt4[] = "lString</TT> in the example above)\ngenerat"
                                "e detailed log message";
  const char file_test_txt5[] = "SS=\"western\">make check</TT> will usually"
                                " run\n\tall of the tests.";
  struct archive_entry *ae;
  struct archive *a;

  extract_reference_files(reffiles);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_support_filter_all(a));
  assertA(0 == archive_read_support_format_all(a));
  assertA(0 == archive_read_open_filenames(a, reffiles, 10240));

  /* First header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("LibarchiveAddingTest.html", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(file_size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Seek to the end minus 64 bytes */
  assertA(file_size - (int)sizeof(buff) ==
    archive_seek_data(a, file_size - (int)sizeof(buff), SEEK_SET));
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
  assertEqualMem(buff, file_test_txt1, sizeof(file_test_txt1) - 1);

  /* Seek back to the beginning */
  assertA(0 == archive_seek_data(a, 0, SEEK_SET));
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
  assertEqualMem(buff, file_test_txt2, sizeof(file_test_txt2) - 1);

  /* Seek to the middle of the combined data block */
  assertA(10054 == archive_seek_data(a, 10054, SEEK_SET));
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
  assertEqualMem(buff, file_test_txt3, sizeof(file_test_txt3) - 1);

  /* Seek to 32 bytes before the end of the first data sub-block */
  assertA(6860 == archive_seek_data(a, 6860, SEEK_SET));
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
  assertEqualMem(buff, file_test_txt4, sizeof(file_test_txt4) - 1);

  /* Seek to 32 bytes before the end of the second data sub-block */
  assertA(13752 == archive_seek_data(a, 13752, SEEK_SET));
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
  assertEqualMem(buff, file_test_txt5, sizeof(file_test_txt5) - 1);

  /* Use various combinations of SEEK_SET, SEEK_CUR, and SEEK_END */
  assertEqualInt(file_size, archive_seek_data(a, 0, SEEK_END));
  assertEqualInt(0, archive_seek_data(a, 0, SEEK_SET));
  assertEqualInt(0, archive_seek_data(a, 0, SEEK_CUR));
  assertEqualInt(-1, archive_seek_data(a, -10, SEEK_CUR));
  assertEqualInt(10, archive_seek_data(a, 10, SEEK_CUR));
  assertEqualInt(-1, archive_seek_data(a, -20, SEEK_CUR));
  assertEqualInt(10, archive_seek_data(a, 0, SEEK_CUR));
  assertEqualInt(file_size, archive_seek_data(a, 0, SEEK_END));
  assertEqualInt(file_size - 20, archive_seek_data(a, -20, SEEK_END));
  assertEqualInt(file_size + 40, archive_seek_data(a, 40, SEEK_END));
  assertEqualInt(file_size + 40, archive_seek_data(a, 0, SEEK_CUR));
  assertEqualInt(file_size + 40 + 20, archive_seek_data(a, 20, SEEK_CUR));
  assertEqualInt(file_size + 40 + 20 + 20, archive_seek_data(a, 20, SEEK_CUR));
  assertEqualInt(file_size + 20, archive_seek_data(a, 20, SEEK_END));
  assertEqualInt(file_size - 20, archive_seek_data(a, -20, SEEK_END));

  /*
   * Attempt to read from the end of the file. These should return
   * 0 for end of file.
   */
  assertEqualInt(file_size, archive_seek_data(a, 0, SEEK_END));
  assertA(0 == archive_read_data(a, buff, sizeof(buff)));
  assertEqualInt(file_size + 40, archive_seek_data(a, 40, SEEK_CUR));
  assertA(0 == archive_read_data(a, buff, sizeof(buff)));

  /* Seek to the end minus 64 bytes */
  assertA(0 == archive_seek_data(a, 0, SEEK_SET));
  assertA(file_size - (int)sizeof(buff) ==
    archive_seek_data(a, -(int)sizeof(buff), SEEK_END));
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
  assertEqualMem(buff, file_test_txt1, sizeof(file_test_txt1) - 1);

  /* The file position should be at the end of the file here */
  assertA(file_size == archive_seek_data(a, 0, SEEK_CUR));

  /* Seek back to the beginning */
  assertA(0 == archive_seek_data(a, -file_size, SEEK_CUR));
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
  assertEqualMem(buff, file_test_txt2, sizeof(file_test_txt2) - 1);

  /* Seek to the middle of the combined data block */
  assertA(10054 == archive_seek_data(a, 10054 - (int)sizeof(buff), SEEK_CUR));
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
  assertEqualMem(buff, file_test_txt3, sizeof(file_test_txt3) - 1);

  /* Seek to 32 bytes before the end of the first data sub-block */
  assertA(6860 == archive_seek_data(a, 6860 - (10054 + (int)sizeof(buff)),
                                    SEEK_CUR));
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
  assertEqualMem(buff, file_test_txt4, sizeof(file_test_txt4) - 1);

  /* Seek to 32 bytes before the end of the second data sub-block */
  assertA(13752 == archive_seek_data(a, 13752 - file_size, SEEK_END));
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
  assertEqualMem(buff, file_test_txt5, sizeof(file_test_txt5) - 1);

  /* Test EOF */
  assertA(1 == archive_read_next_header(a, &ae));
  assertEqualInt(1, archive_file_count(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_format_rar_multivolume_seek_multiple_files)
{
  const char *reffiles[] =
  {
    "test_rar_multivolume_multiple_files.part1.rar",
    "test_rar_multivolume_multiple_files.part2.rar",
    "test_rar_multivolume_multiple_files.part3.rar",
    "test_rar_multivolume_multiple_files.part4.rar",
    "test_rar_multivolume_multiple_files.part5.rar",
    "test_rar_multivolume_multiple_files.part6.rar",
    NULL
  };
  char buff[64];
  int file_size = 20111;
  const char file_test_txt1[] = "d. \n</P>\n<P STYLE=\"margin-bottom: 0in\">"
                                "<BR>\n</P>\n</BODY>\n</HTML>";
  const char file_test_txt2[] = "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4."
                                "0 Transitional//EN\">\n<";
  const char file_test_txt3[] = "mplify writing such tests,\ntry to use plat"
                                "form-independent codin";
  const char file_test_txt4[] = "\nfailures. \n</P>\n<H1 CLASS=\"western\"><"
                                "A NAME=\"Life_cycle_of_a_te";
  const char file_test_txt5[] = "LE=\"margin-bottom: 0in\">DO use runtime te"
                                "sts for platform\n\tfeatu";
  const char file_test_txt6[] = "rough test suite is essential\nboth for ver"
                                "ifying new ports and f";
  const char file_test_txt7[] = "m: 0in\">Creates a temporary directory\n\tw"
                                "hose name matches the na";
  const char file_test_txt8[] = "lt\ninput file and verify the results. Thes"
                                "e use <TT CLASS=\"weste";
  struct archive_entry *ae;
  struct archive *a;

  extract_reference_files(reffiles);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_support_filter_all(a));
  assertA(0 == archive_read_support_format_all(a));
  assertA(0 == archive_read_open_filenames(a, reffiles, 10240));

  /* First header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("LibarchiveAddingTest2.html", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(file_size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Attempt to read past end of file */
  assertEqualInt(file_size, archive_seek_data(a, 0, SEEK_END));
  assertA(0 == archive_read_data(a, buff, sizeof(buff)));
  assertEqualInt(file_size + 40, archive_seek_data(a, 40, SEEK_CUR));
  assertA(0 == archive_read_data(a, buff, sizeof(buff)));

  /* Seek to the end minus 64 bytes */
  assertA(file_size - (int)sizeof(buff) ==
    archive_seek_data(a, -(int)sizeof(buff), SEEK_END));
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
  assertEqualMem(buff, file_test_txt1, sizeof(file_test_txt1) - 1);

  /* Seek back to the beginning */
  assertA(0 == archive_seek_data(a, -file_size, SEEK_END));
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
  assertEqualMem(buff, file_test_txt2, sizeof(file_test_txt2) - 1);

  /* Seek to the middle of the combined data block */
  assertA(10054 == archive_seek_data(a, 10054 - (int)sizeof(buff), SEEK_CUR));
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
  assertEqualMem(buff, file_test_txt3, sizeof(file_test_txt3) - 1);

  /* Seek to 32 bytes before the end of the first data sub-block */
  assertA(7027 == archive_seek_data(a, 7027 - (10054 + (int)sizeof(buff)),
                                    SEEK_CUR));
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
  assertEqualMem(buff, file_test_txt4, sizeof(file_test_txt4) - 1);

  /* Seek to 32 bytes before the end of the second data sub-block */
  assertA(14086 == archive_seek_data(a, 14086 - file_size, SEEK_END));
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
  assertEqualMem(buff, file_test_txt5, sizeof(file_test_txt5) - 1);

  /* Attempt to read past end of file */
  assertEqualInt(file_size, archive_seek_data(a, 0, SEEK_END));
  assertA(0 == archive_read_data(a, buff, sizeof(buff)));
  assertEqualInt(file_size + 40, archive_seek_data(a, 40, SEEK_CUR));
  assertA(0 == archive_read_data(a, buff, sizeof(buff)));

  /* Second header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("LibarchiveAddingTest.html", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(file_size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Attempt to read past end of file */
  assertEqualInt(file_size, archive_seek_data(a, 0, SEEK_END));
  assertA(0 == archive_read_data(a, buff, sizeof(buff)));
  assertEqualInt(file_size + 40, archive_seek_data(a, 40, SEEK_CUR));
  assertA(0 == archive_read_data(a, buff, sizeof(buff)));

  /* Seek to the end minus 64 bytes */
  assertA(file_size - (int)sizeof(buff) ==
    archive_seek_data(a, file_size - (int)sizeof(buff), SEEK_SET));
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
  assertEqualMem(buff, file_test_txt1, sizeof(file_test_txt1) - 1);

  /* Seek back to the beginning */
  assertA(0 == archive_seek_data(a, 0, SEEK_SET));
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
  assertEqualMem(buff, file_test_txt2, sizeof(file_test_txt2) - 1);

  /* Seek to the middle of the combined data block */
  assertA(10054 == archive_seek_data(a, 10054 - (int)sizeof(buff), SEEK_CUR));
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
  assertEqualMem(buff, file_test_txt3, sizeof(file_test_txt3) - 1);

  /* Seek to 32 bytes before the end of the first data sub-block */
  assertA(969 == archive_seek_data(a, 969 - (10054 + (int)sizeof(buff)), SEEK_CUR));
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
  assertEqualMem(buff, file_test_txt6, sizeof(file_test_txt4) - 1);

  /* Seek to 32 bytes before the end of the second data sub-block */
  assertA(8029 == archive_seek_data(a, 8029 - file_size, SEEK_END));
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
  assertEqualMem(buff, file_test_txt7, sizeof(file_test_txt5) - 1);

  /* Seek to 32 bytes before the end of the third data sub-block */
  assertA(15089 == archive_seek_data(a, 15089 - file_size, SEEK_END));
  assertA(sizeof(buff) == archive_read_data(a, buff, sizeof(buff)));
  assertEqualMem(buff, file_test_txt8, sizeof(file_test_txt5) - 1);

  /* Attempt to read past end of file */
  assertEqualInt(file_size, archive_seek_data(a, 0, SEEK_END));
  assertA(0 == archive_read_data(a, buff, sizeof(buff)));
  assertEqualInt(file_size + 40, archive_seek_data(a, 40, SEEK_CUR));
  assertA(0 == archive_read_data(a, buff, sizeof(buff)));

  /* Test EOF */
  assertA(1 == archive_read_next_header(a, &ae));
  assertEqualInt(2, archive_file_count(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

static void
test_read_format_rar_multivolume_uncompressed_files_helper(struct archive *a)
{
  char buff[64];

  /* Do checks for seeks/reads past beginning and end of file */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, -1, archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_SET));
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualIntA(a, (sizeof(buff)-1), archive_seek_data(a, 0, SEEK_CUR));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD "
                        "HTML 4.0 Transitional//EN\">\n", buff);
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, -1, archive_seek_data(a, -(((int)sizeof(buff)-1)*2), SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1), archive_seek_data(a, 0, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualIntA(a, ((sizeof(buff)-1)*2), archive_seek_data(a, 0, SEEK_CUR));
  assertEqualStringA(a, "<HTML>\n<HEAD>\n\t<META HTTP-EQUIV=\"CONTENT-TYPE\" "
                        "CONTENT=\"text/ht", buff);
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, -1, archive_seek_data(a, -(20111+32), SEEK_END));
  assertEqualIntA(a, ((sizeof(buff)-1)*2), archive_seek_data(a, 0, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualIntA(a, ((sizeof(buff)-1)*3), archive_seek_data(a, 0, SEEK_CUR));
  assertEqualStringA(a, "ml; charset=utf-8\">\n\t<TITLE></TITLE>\n\t<META "
                        "NAME=\"GENERATOR\" CO", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111, archive_seek_data(a, 20111, SEEK_SET));
  assertEqualIntA(a, 20111, archive_seek_data(a, 0, SEEK_CUR));
  assertEqualIntA(a, 0, archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualIntA(a, 20111, archive_seek_data(a, 0, SEEK_CUR));
  assertEqualStringA(a, "", buff);
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 + (sizeof(buff)-1),
    archive_seek_data(a, (sizeof(buff)-1), SEEK_CUR));
  assertEqualIntA(a, 0, archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualIntA(a, 20111 + (sizeof(buff)-1),
    archive_seek_data(a, 0, SEEK_CUR));
  assertEqualStringA(a, "", buff);
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 + ((sizeof(buff)-1)*2),
    archive_seek_data(a, ((sizeof(buff)-1)*2), SEEK_END));
  assertEqualIntA(a, 0, archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualIntA(a, 20111 + ((sizeof(buff)-1)*2),
    archive_seek_data(a, 0, SEEK_CUR));
  assertEqualStringA(a, "", buff);
}

DEFINE_TEST(test_read_format_rar_multivolume_uncompressed_files)
{
  const char *reffiles[] =
  {
    "test_rar_multivolume_uncompressed_files.part01.rar",
    "test_rar_multivolume_uncompressed_files.part02.rar",
    "test_rar_multivolume_uncompressed_files.part03.rar",
    "test_rar_multivolume_uncompressed_files.part04.rar",
    "test_rar_multivolume_uncompressed_files.part05.rar",
    "test_rar_multivolume_uncompressed_files.part06.rar",
    "test_rar_multivolume_uncompressed_files.part07.rar",
    "test_rar_multivolume_uncompressed_files.part08.rar",
    "test_rar_multivolume_uncompressed_files.part09.rar",
    "test_rar_multivolume_uncompressed_files.part10.rar",
    NULL
  };
  char buff[64];
  ssize_t bytes_read;
  struct archive *a;
  struct archive_entry *ae;

  extract_reference_files(reffiles);
  assert((a = archive_read_new()) != NULL);
  assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
  assertEqualIntA(a, ARCHIVE_OK,
                  archive_read_open_filenames(a, reffiles, 10240));

  /*
   * First header.
   */
  assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
  assertEqualStringA(a, "testdir/LibarchiveAddingTest2.html",
                     archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualIntA(a, 20111, archive_entry_size(ae));
  assertEqualIntA(a, 33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  test_read_format_rar_multivolume_uncompressed_files_helper(a);

  /* Read from the beginning to the end of the file */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  do
  {
    memset(buff, 0, sizeof(buff));
    bytes_read = archive_read_data(a, buff, (sizeof(buff)-1));
  } while (bytes_read > 0);

  /* Seek to the end minus (sizeof(buff)-1) bytes */
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  /* Seek back to the beginning */
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0, archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  /* Test that SEEK_SET works correctly between data blocks */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 13164,
    archive_seek_data(a, 13164, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "ertEqualInt,\n\tassertEqualString, "
                        "assertEqualMem to test equalit", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, 0, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 20111 - (int)(sizeof(buff)-1), SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 13164,
    archive_seek_data(a, 13164, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "ertEqualInt,\n\tassertEqualString, "
                        "assertEqualMem to test equalit", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 20111 - (int)(sizeof(buff)-1), SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, 0, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 20111 - (int)(sizeof(buff)-1), SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 13164,
    archive_seek_data(a, 13164, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "ertEqualInt,\n\tassertEqualString, "
                        "assertEqualMem to test equalit", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, 0, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  /* Test that SEEK_CUR works correctly between data blocks */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 13164,
    archive_seek_data(a, 13164, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "ertEqualInt,\n\tassertEqualString, "
                        "assertEqualMem to test equalit", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -13227, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 19985, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 13164,
    archive_seek_data(a, -6947, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "ertEqualInt,\n\tassertEqualString, "
                        "assertEqualMem to test equalit", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 6821, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 19985, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 13164,
    archive_seek_data(a, -6947, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "ertEqualInt,\n\tassertEqualString, "
                        "assertEqualMem to test equalit", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -13227, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  /* Test that SEEK_END works correctly between data blocks */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 13164,
    archive_seek_data(a, -6947, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "ertEqualInt,\n\tassertEqualString, "
                        "assertEqualMem to test equalit", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 13164,
    archive_seek_data(a, -6947, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "ertEqualInt,\n\tassertEqualString, "
                        "assertEqualMem to test equalit", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 13164,
    archive_seek_data(a, -6947, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "ertEqualInt,\n\tassertEqualString, "
                        "assertEqualMem to test equalit", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  test_read_format_rar_multivolume_uncompressed_files_helper(a);

  /*
   * Second header.
   */
  assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
  assertEqualStringA(a, "testdir/testsubdir/LibarchiveAddingTest2.html",
                     archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualIntA(a, 20111, archive_entry_size(ae));
  assertEqualIntA(a, 33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  test_read_format_rar_multivolume_uncompressed_files_helper(a);

  /* Read from the beginning to the end of the file */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  do
  {
    memset(buff, 0, sizeof(buff));
    bytes_read = archive_read_data(a, buff, (sizeof(buff)-1));
  } while (bytes_read > 0);

  /* Seek to the end minus (sizeof(buff)-1) bytes */
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  /* Seek back to the beginning */
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0, archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  /* Test that SEEK_SET works correctly between data blocks */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 6162,
    archive_seek_data(a, 6162, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "arguments satisfy certain conditions. "
                        "If the assertion fails--f", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 19347,
    archive_seek_data(a, 19347, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, " when a block being written out by\n"
                        "the archive writer is the sa", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 20111 - (int)(sizeof(buff)-1), SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 19347,
    archive_seek_data(a, 19347, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, " when a block being written out by\n"
                        "the archive writer is the sa", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 6162,
    archive_seek_data(a, 6162, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "arguments satisfy certain conditions. "
                        "If the assertion fails--f", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, 0, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 20111 - (int)(sizeof(buff)-1), SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 6162,
    archive_seek_data(a, 6162, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "arguments satisfy certain conditions. "
                        "If the assertion fails--f", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 19347,
    archive_seek_data(a, 19347, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, " when a block being written out by\n"
                        "the archive writer is the sa", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, 0, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 20111 - (int)(sizeof(buff)-1), SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, 0, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 19347,
    archive_seek_data(a, 19347, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, " when a block being written out by\n"
                        "the archive writer is the sa", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 6162,
    archive_seek_data(a, 6162, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "arguments satisfy certain conditions. "
                        "If the assertion fails--f", buff);

  /* Test that SEEK_CUR works correctly between data blocks */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 6162,
    archive_seek_data(a, 6162, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "arguments satisfy certain conditions. "
                        "If the assertion fails--f", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 19347,
    archive_seek_data(a, 13122, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, " when a block being written out by\n"
                        "the archive writer is the sa", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 638, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 19347,
    archive_seek_data(a, -764, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, " when a block being written out by\n"
                        "the archive writer is the sa", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 6162,
    archive_seek_data(a, -13248, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "arguments satisfy certain conditions. "
                        "If the assertion fails--f", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -6225, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 19985, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 6162,
    archive_seek_data(a, -13949, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "arguments satisfy certain conditions. "
                        "If the assertion fails--f", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 19347,
    archive_seek_data(a, 13122, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, " when a block being written out by\n"
                        "the archive writer is the sa", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -19410, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 19985, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 19347,
    archive_seek_data(a, 19284, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, " when a block being written out by\n"
                        "the archive writer is the sa", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 6162,
    archive_seek_data(a, -13248, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "arguments satisfy certain conditions. "
                        "If the assertion fails--f", buff);

  /* Test that SEEK_END works correctly between data blocks */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 6162,
    archive_seek_data(a, -13949, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "arguments satisfy certain conditions. "
                        "If the assertion fails--f", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 19347,
    archive_seek_data(a, -764, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, " when a block being written out by\n"
                        "the archive writer is the sa", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 19347,
    archive_seek_data(a, -764, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, " when a block being written out by\n"
                        "the archive writer is the sa", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 6162,
    archive_seek_data(a, -13949, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "arguments satisfy certain conditions. "
                        "If the assertion fails--f", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 6162,
    archive_seek_data(a, -13949, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "arguments satisfy certain conditions. "
                        "If the assertion fails--f", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 19347,
    archive_seek_data(a, -764, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, " when a block being written out by\n"
                        "the archive writer is the sa", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 19347,
    archive_seek_data(a, -764, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, " when a block being written out by\n"
                        "the archive writer is the sa", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 6162,
    archive_seek_data(a, -13949, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "arguments satisfy certain conditions. "
                        "If the assertion fails--f", buff);

  test_read_format_rar_multivolume_uncompressed_files_helper(a);

  /*
   * Third header.
   */
  assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
  assertEqualStringA(a, "LibarchiveAddingTest2.html",
                     archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualIntA(a, 20111, archive_entry_size(ae));
  assertEqualIntA(a, 33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  test_read_format_rar_multivolume_uncompressed_files_helper(a);

  /* Read from the beginning to the end of the file */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  do
  {
    memset(buff, 0, sizeof(buff));
    bytes_read = archive_read_data(a, buff, (sizeof(buff)-1));
  } while (bytes_read > 0);

  /* Seek to the end minus (sizeof(buff)-1) bytes */
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  /* Seek back to the beginning */
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0, archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  /* Test that SEEK_SET works correctly between data blocks */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 12353,
    archive_seek_data(a, 12353, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, " 0.2in\">&nbsp; &nbsp; "
                        "extract_reference_file(&quot;test_foo.tar", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, 0, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 20111 - (int)(sizeof(buff)-1), SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 12353,
    archive_seek_data(a, 12353, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, " 0.2in\">&nbsp; &nbsp; "
                        "extract_reference_file(&quot;test_foo.tar", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 20111 - (int)(sizeof(buff)-1), SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, 0, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 20111 - (int)(sizeof(buff)-1), SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 12353,
    archive_seek_data(a, 12353, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, " 0.2in\">&nbsp; &nbsp; "
                        "extract_reference_file(&quot;test_foo.tar", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, 0, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  /* Test that SEEK_CUR works correctly between data blocks */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 12353,
    archive_seek_data(a, 12353, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, " 0.2in\">&nbsp; &nbsp; "
                        "extract_reference_file(&quot;test_foo.tar", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -12416, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 19985, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 12353,
    archive_seek_data(a, -7758, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, " 0.2in\">&nbsp; &nbsp; "
                        "extract_reference_file(&quot;test_foo.tar", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 7632, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 19985, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 12353,
    archive_seek_data(a, -7758, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, " 0.2in\">&nbsp; &nbsp; "
                        "extract_reference_file(&quot;test_foo.tar", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -12416, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  /* Test that SEEK_END works correctly between data blocks */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 12353,
    archive_seek_data(a, -7758, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, " 0.2in\">&nbsp; &nbsp; "
                        "extract_reference_file(&quot;test_foo.tar", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 12353,
    archive_seek_data(a, -7758, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, " 0.2in\">&nbsp; &nbsp; "
                        "extract_reference_file(&quot;test_foo.tar", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 12353,
    archive_seek_data(a, -7758, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, " 0.2in\">&nbsp; &nbsp; "
                        "extract_reference_file(&quot;test_foo.tar", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  test_read_format_rar_multivolume_uncompressed_files_helper(a);

  /*
   * Fourth header.
   */
  assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
  assertEqualStringA(a, "testdir/LibarchiveAddingTest.html",
                     archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualIntA(a, 20111, archive_entry_size(ae));
  assertEqualIntA(a, 33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  test_read_format_rar_multivolume_uncompressed_files_helper(a);

  /* Read from the beginning to the end of the file */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  do
  {
    memset(buff, 0, sizeof(buff));
    bytes_read = archive_read_data(a, buff, (sizeof(buff)-1));
  } while (bytes_read > 0);

  /* Seek to the end minus (sizeof(buff)-1) bytes */
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  /* Seek back to the beginning */
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0, archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  /* Test that SEEK_SET works correctly between data blocks */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 5371,
    archive_seek_data(a, 5371, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "zip)\n&nbsp; {\n&nbsp; &nbsp; "
                        "/* ... setup omitted ... */\n&nbsp; ", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 13165,
    archive_seek_data(a, 13165, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "rtEqualInt,\n\tassertEqualString, "
                        "assertEqualMem to test equality", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 20111 - (int)(sizeof(buff)-1), SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 13165,
    archive_seek_data(a, 13165, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "rtEqualInt,\n\tassertEqualString, "
                        "assertEqualMem to test equality", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 5371,
    archive_seek_data(a, 5371, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "zip)\n&nbsp; {\n&nbsp; &nbsp; "
                        "/* ... setup omitted ... */\n&nbsp; ", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, 0, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 20111 - (int)(sizeof(buff)-1), SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 5371,
    archive_seek_data(a, 5371, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "zip)\n&nbsp; {\n&nbsp; &nbsp; "
                        "/* ... setup omitted ... */\n&nbsp; ", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 13165,
    archive_seek_data(a, 13165, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "rtEqualInt,\n\tassertEqualString, "
                        "assertEqualMem to test equality", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, 0, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 20111 - (int)(sizeof(buff)-1), SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, 0, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 13165,
    archive_seek_data(a, 13165, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "rtEqualInt,\n\tassertEqualString, "
                        "assertEqualMem to test equality", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 5371,
    archive_seek_data(a, 5371, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "zip)\n&nbsp; {\n&nbsp; &nbsp; "
                        "/* ... setup omitted ... */\n&nbsp; ", buff);

  /* Test that SEEK_CUR works correctly between data blocks */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 5371,
    archive_seek_data(a, 5371, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "zip)\n&nbsp; {\n&nbsp; &nbsp; "
                        "/* ... setup omitted ... */\n&nbsp; ", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 13165,
    archive_seek_data(a, 7731, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "rtEqualInt,\n\tassertEqualString, "
                        "assertEqualMem to test equality", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 6820, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 13165,
    archive_seek_data(a, -6946, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "rtEqualInt,\n\tassertEqualString, "
                        "assertEqualMem to test equality", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 5371,
    archive_seek_data(a, -7857, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "zip)\n&nbsp; {\n&nbsp; &nbsp; "
                        "/* ... setup omitted ... */\n&nbsp; ", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -5434, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 19985, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 5371,
    archive_seek_data(a, -14740, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "zip)\n&nbsp; {\n&nbsp; &nbsp; "
                        "/* ... setup omitted ... */\n&nbsp; ", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 13165,
    archive_seek_data(a, 7731, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "rtEqualInt,\n\tassertEqualString, "
                        "assertEqualMem to test equality", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -13228, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 19985, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 13165,
    archive_seek_data(a, 13102, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "rtEqualInt,\n\tassertEqualString, "
                        "assertEqualMem to test equality", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 5371,
    archive_seek_data(a, -7857, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "zip)\n&nbsp; {\n&nbsp; &nbsp; "
                        "/* ... setup omitted ... */\n&nbsp; ", buff);

  /* Test that SEEK_END works correctly between data blocks */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 5371,
    archive_seek_data(a, -14740, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "zip)\n&nbsp; {\n&nbsp; &nbsp; "
                        "/* ... setup omitted ... */\n&nbsp; ", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 13165,
    archive_seek_data(a, -6946, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "rtEqualInt,\n\tassertEqualString, "
                        "assertEqualMem to test equality", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 13165,
    archive_seek_data(a, -6946, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "rtEqualInt,\n\tassertEqualString, "
                        "assertEqualMem to test equality", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 5371,
    archive_seek_data(a, -14740, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "zip)\n&nbsp; {\n&nbsp; &nbsp; "
                        "/* ... setup omitted ... */\n&nbsp; ", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 5371,
    archive_seek_data(a, -14740, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "zip)\n&nbsp; {\n&nbsp; &nbsp; "
                        "/* ... setup omitted ... */\n&nbsp; ", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 13165,
    archive_seek_data(a, -6946, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "rtEqualInt,\n\tassertEqualString, "
                        "assertEqualMem to test equality", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 13165,
    archive_seek_data(a, -6946, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "rtEqualInt,\n\tassertEqualString, "
                        "assertEqualMem to test equality", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 5371,
    archive_seek_data(a, -14740, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "zip)\n&nbsp; {\n&nbsp; &nbsp; "
                        "/* ... setup omitted ... */\n&nbsp; ", buff);

  test_read_format_rar_multivolume_uncompressed_files_helper(a);

  /*
   * Fifth header.
   */
  assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
  assertEqualStringA(a, "testdir/testsubdir/LibarchiveAddingTest.html",
                     archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualIntA(a, 20111, archive_entry_size(ae));
  assertEqualIntA(a, 33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  test_read_format_rar_multivolume_uncompressed_files_helper(a);

  /* Read from the beginning to the end of the file */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  do
  {
    memset(buff, 0, sizeof(buff));
    bytes_read = archive_read_data(a, buff, (sizeof(buff)-1));
  } while (bytes_read > 0);

  /* Seek to the end minus (sizeof(buff)-1) bytes */
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  /* Seek back to the beginning */
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0, archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  /* Test that SEEK_SET works correctly between data blocks */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 11568,
    archive_seek_data(a, 11568, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ", <TT CLASS=\"western\">assertFileContents</TT>,"
                        "\n\t<TT CLASS=\"west", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, 0, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 20111 - (int)(sizeof(buff)-1), SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 11568,
    archive_seek_data(a, 11568, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ", <TT CLASS=\"western\">assertFileContents</TT>,"
                        "\n\t<TT CLASS=\"west", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 20111 - (int)(sizeof(buff)-1), SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, 0, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 20111 - (int)(sizeof(buff)-1), SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 11568,
    archive_seek_data(a, 11568, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ", <TT CLASS=\"western\">assertFileContents</TT>,"
                        "\n\t<TT CLASS=\"west", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, 0, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  /* Test that SEEK_CUR works correctly between data blocks */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 11568,
    archive_seek_data(a, 11568, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ", <TT CLASS=\"western\">assertFileContents</TT>,"
                        "\n\t<TT CLASS=\"west", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -11631, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 19985, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 11568,
    archive_seek_data(a, -8543, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ", <TT CLASS=\"western\">assertFileContents</TT>,"
                        "\n\t<TT CLASS=\"west", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 8417, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 19985, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 11568,
    archive_seek_data(a, -8543, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ", <TT CLASS=\"western\">assertFileContents</TT>,"
                        "\n\t<TT CLASS=\"west", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -11631, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  /* Test that SEEK_END works correctly between data blocks */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 11568,
    archive_seek_data(a, -8543, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ", <TT CLASS=\"western\">assertFileContents</TT>,"
                        "\n\t<TT CLASS=\"west", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 11568,
    archive_seek_data(a, -8543, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ", <TT CLASS=\"western\">assertFileContents</TT>,"
                        "\n\t<TT CLASS=\"west", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 11568,
    archive_seek_data(a, -8543, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ", <TT CLASS=\"western\">assertFileContents</TT>,"
                        "\n\t<TT CLASS=\"west", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  test_read_format_rar_multivolume_uncompressed_files_helper(a);

  /*
   * Sixth header.
   */
  assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
  assertEqualStringA(a, "LibarchiveAddingTest.html",
                     archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualIntA(a, 20111, archive_entry_size(ae));
  assertEqualIntA(a, 33188, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  test_read_format_rar_multivolume_uncompressed_files_helper(a);

  /* Read from the beginning to the end of the file */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  do
  {
    memset(buff, 0, sizeof(buff));
    bytes_read = archive_read_data(a, buff, (sizeof(buff)-1));
  } while (bytes_read > 0);

  /* Seek to the end minus (sizeof(buff)-1) bytes */
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  /* Seek back to the beginning */
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0, archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  /* Test that SEEK_SET works correctly between data blocks */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 4576,
    archive_seek_data(a, 4576, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "hat was expected. \n</P>\n<H1 CLASS=\"western\"><A "
                        "NAME=\"Basic_test", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 17749,
    archive_seek_data(a, 17749, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "\"></A>Large tar tester</H2>\n<P>The "
                        "large tar tester attempts to", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 20111 - (int)(sizeof(buff)-1), SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 17749,
    archive_seek_data(a, 17749, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "\"></A>Large tar tester</H2>\n<P>The "
                        "large tar tester attempts to", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 4576,
    archive_seek_data(a, 4576, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "hat was expected. \n</P>\n<H1 CLASS=\"western\"><A "
                        "NAME=\"Basic_test", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, 0, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 20111 - (int)(sizeof(buff)-1), SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 4576,
    archive_seek_data(a, 4576, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "hat was expected. \n</P>\n<H1 CLASS=\"western\"><A "
                        "NAME=\"Basic_test", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 17749,
    archive_seek_data(a, 17749, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "\"></A>Large tar tester</H2>\n<P>The "
                        "large tar tester attempts to", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, 0, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 20111 - (int)(sizeof(buff)-1), SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, 0, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 17749,
    archive_seek_data(a, 17749, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "\"></A>Large tar tester</H2>\n<P>The "
                        "large tar tester attempts to", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 4576,
    archive_seek_data(a, 4576, SEEK_SET));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "hat was expected. \n</P>\n<H1 CLASS=\"western\"><A "
                        "NAME=\"Basic_test", buff);

  /* Test that SEEK_CUR works correctly between data blocks */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 4576,
    archive_seek_data(a, 4576, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "hat was expected. \n</P>\n<H1 CLASS=\"western\"><A "
                        "NAME=\"Basic_test", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 17749,
    archive_seek_data(a, 13110, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "\"></A>Large tar tester</H2>\n<P>The "
                        "large tar tester attempts to", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 2236, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 17749,
    archive_seek_data(a, -2362, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "\"></A>Large tar tester</H2>\n<P>The "
                        "large tar tester attempts to", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 4576,
    archive_seek_data(a, -13236, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "hat was expected. \n</P>\n<H1 CLASS=\"western\"><A "
                        "NAME=\"Basic_test", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -4639, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 19985, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 4576,
    archive_seek_data(a, -15535, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "hat was expected. \n</P>\n<H1 CLASS=\"western\"><A "
                        "NAME=\"Basic_test", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 17749,
    archive_seek_data(a, 13110, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "\"></A>Large tar tester</H2>\n<P>The "
                        "large tar tester attempts to", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -17812, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, 19985, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 17749,
    archive_seek_data(a, 17686, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "\"></A>Large tar tester</H2>\n<P>The "
                        "large tar tester attempts to", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 4576,
    archive_seek_data(a, -13236, SEEK_CUR));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "hat was expected. \n</P>\n<H1 CLASS=\"western\"><A "
                        "NAME=\"Basic_test", buff);

  /* Test that SEEK_END works correctly between data blocks */
  assertEqualIntA(a, 0, archive_seek_data(a, 0, SEEK_SET));
  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 4576,
    archive_seek_data(a, -15535, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "hat was expected. \n</P>\n<H1 CLASS=\"western\"><A "
                        "NAME=\"Basic_test", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 17749,
    archive_seek_data(a, -2362, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "\"></A>Large tar tester</H2>\n<P>The "
                        "large tar tester attempts to", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 17749,
    archive_seek_data(a, -2362, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "\"></A>Large tar tester</H2>\n<P>The "
                        "large tar tester attempts to", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 4576,
    archive_seek_data(a, -15535, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "hat was expected. \n</P>\n<H1 CLASS=\"western\"><A "
                        "NAME=\"Basic_test", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 4576,
    archive_seek_data(a, -15535, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "hat was expected. \n</P>\n<H1 CLASS=\"western\"><A "
                        "NAME=\"Basic_test", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 17749,
    archive_seek_data(a, -2362, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "\"></A>Large tar tester</H2>\n<P>The "
                        "large tar tester attempts to", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 20111 - (int)(sizeof(buff)-1),
    archive_seek_data(a, -((int)sizeof(buff)-1), SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, ". \n</P>\n<P STYLE=\"margin-bottom: 0in\"><BR>\n"
                        "</P>\n</BODY>\n</HTML>", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 0,
    archive_seek_data(a, -20111, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 "
                        "Transitional//EN\">\n", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 17749,
    archive_seek_data(a, -2362, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "\"></A>Large tar tester</H2>\n<P>The "
                        "large tar tester attempts to", buff);

  memset(buff, 0, sizeof(buff));
  assertEqualIntA(a, 4576,
    archive_seek_data(a, -15535, SEEK_END));
  assertEqualIntA(a, (sizeof(buff)-1),
    archive_read_data(a, buff, (sizeof(buff)-1)));
  assertEqualStringA(a, "hat was expected. \n</P>\n<H1 CLASS=\"western\"><A "
                        "NAME=\"Basic_test", buff);

  test_read_format_rar_multivolume_uncompressed_files_helper(a);

  /*
   * Seventh header.
   */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir/testsymlink5", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(41471, archive_entry_mode(ae));
  assertEqualString("testsubdir/LibarchiveAddingTest.html",
    archive_entry_symlink(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertEqualIntA(a, 0, archive_read_data(a, buff, sizeof(buff)));

  /*
   * Eighth header.
   */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir/testsymlink6", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(41471, archive_entry_mode(ae));
  assertEqualString("testsubdir/LibarchiveAddingTest2.html",
    archive_entry_symlink(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertEqualIntA(a, 0, archive_read_data(a, buff, sizeof(buff)));

  /*
   * Ninth header.
   */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testsymlink", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(41471, archive_entry_mode(ae));
  assertEqualString("testdir/LibarchiveAddingTest.html",
    archive_entry_symlink(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertEqualIntA(a, 0, archive_read_data(a, buff, sizeof(buff)));

  /*
   * Tenth header.
   */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testsymlink2", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(41471, archive_entry_mode(ae));
  assertEqualString("testdir/LibarchiveAddingTest2.html",
    archive_entry_symlink(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertEqualIntA(a, 0, archive_read_data(a, buff, sizeof(buff)));

  /*
   * Eleventh header.
   */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testsymlink3", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(41471, archive_entry_mode(ae));
  assertEqualString("testdir/testsubdir/LibarchiveAddingTest.html",
    archive_entry_symlink(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertEqualIntA(a, 0, archive_read_data(a, buff, sizeof(buff)));

  /*
   * Twelfth header.
   */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testsymlink4", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(41471, archive_entry_mode(ae));
  assertEqualString("testdir/testsubdir/LibarchiveAddingTest2.html",
    archive_entry_symlink(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);
  assertEqualIntA(a, 0, archive_read_data(a, buff, sizeof(buff)));

  /*
   * Thirteenth header.
   */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir/testemptysubdir", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /*
   * Fourteenth header.
   */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir/testsubdir", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /*
   * Fifteenth header.
   */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /*
   * Sixteenth header.
   */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testemptydir", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));
  assertEqualInt(archive_entry_is_encrypted(ae), 0);
  assertEqualIntA(a, archive_read_has_encrypted_entries(a), 0);

  /* Test EOF */
  assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
  assertEqualIntA(a, 16, archive_file_count(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_free(a));
}
