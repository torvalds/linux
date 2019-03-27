/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * Copyright (c) 2012 Andres Mejia
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

DEFINE_TEST(test_read_set_format)
{
  char buff[64];
  const char reffile[] = "test_read_format_rar.rar";
  const char test_txt[] = "test text document\r\n";
  int size = sizeof(test_txt)-1;
  struct archive_entry *ae;
  struct archive *a;

  extract_reference_file(reffile);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_set_format(a, ARCHIVE_FORMAT_RAR));
  assertA(0 == archive_read_append_filter(a, ARCHIVE_FILTER_NONE));
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

  /* Fourth header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testdir", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));

  /* Fifth header. */
  assertA(0 == archive_read_next_header(a, &ae));
  assertEqualString("testemptydir", archive_entry_pathname(ae));
  assertA((int)archive_entry_mtime(ae));
  assertA((int)archive_entry_ctime(ae));
  assertA((int)archive_entry_atime(ae));
  assertEqualInt(0, archive_entry_size(ae));
  assertEqualInt(16877, archive_entry_mode(ae));

  /* Test EOF */
  assertA(1 == archive_read_next_header(a, &ae));
  assertEqualInt(5, archive_file_count(a));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_set_wrong_format)
{
  const char reffile[] = "test_read_format_zip.zip";
  struct archive_entry *ae;
  struct archive *a;

  extract_reference_file(reffile);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_set_format(a, ARCHIVE_FORMAT_RAR));
  assertA(0 == archive_read_append_filter(a, ARCHIVE_FILTER_NONE));
  assertA(0 == archive_read_open_filename(a, reffile, 10240));

  /* Check that this actually fails, then close the archive. */
  assertA(archive_read_next_header(a, &ae) < (ARCHIVE_WARN));
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

static unsigned char archive[] = {
31,139,8,0,222,'C','p','C',0,3,211,'c',160,'=','0','0','0','0','7','5','U',
0,210,134,230,166,6,200,'4',28,'(',24,26,24,27,155,24,152,24,154,27,155,')',
24,24,26,152,154,25,'2','(',152,210,193,'m',12,165,197,'%',137,'E','@',167,
148,'d',230,226,'U','G','H',30,234,15,'8','=',10,'F',193,'(',24,5,131,28,
0,0,29,172,5,240,0,6,0,0};

DEFINE_TEST(test_read_append_filter)
{
  struct archive_entry *ae;
  struct archive *a;
  int r;

  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_set_format(a, ARCHIVE_FORMAT_TAR));
  r = archive_read_append_filter(a, ARCHIVE_FILTER_GZIP);
  if (r != ARCHIVE_OK && archive_zlib_version() == NULL && !canGzip()) {
    skipping("gzip tests require zlib or working gzip command");
    assertEqualInt(ARCHIVE_OK, archive_read_free(a));
    return;
  }
  assertEqualIntA(a, ARCHIVE_OK, r);
  assertEqualInt(ARCHIVE_OK,
      archive_read_open_memory(a, archive, sizeof(archive)));
  assertEqualInt(ARCHIVE_OK, archive_read_next_header(a, &ae));
  assertEqualInt(1, archive_file_count(a));
  assertEqualInt(archive_filter_code(a, 0), ARCHIVE_COMPRESSION_GZIP);
  assertEqualInt(archive_format(a), ARCHIVE_FORMAT_TAR_USTAR);
  assertEqualInt(ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK,archive_read_free(a));
}

DEFINE_TEST(test_read_append_wrong_filter)
{
  struct archive_entry *ae;
  struct archive *a;
  int r;

  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_set_format(a, ARCHIVE_FORMAT_TAR));
  r = archive_read_append_filter(a, ARCHIVE_FILTER_XZ);
  if (r == ARCHIVE_WARN && !canXz()) {
    skipping("xz reading not fully supported on this platform");
    assertEqualInt(ARCHIVE_OK, archive_read_free(a));
    return;
  }
  assertEqualInt(ARCHIVE_OK,
      archive_read_open_memory(a, archive, sizeof(archive)));
  assertA(archive_read_next_header(a, &ae) < (ARCHIVE_WARN));
  if (r == ARCHIVE_WARN && canXz()) {
    assertEqualIntA(a, ARCHIVE_WARN, archive_read_close(a));
  } else {
    assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  }
  assertEqualInt(ARCHIVE_OK,archive_read_free(a));
}

DEFINE_TEST(test_read_append_filter_program)
{
  struct archive_entry *ae;
  struct archive *a;

  if (!canGzip()) {
    skipping("Can't run gzip program on this platform");
    return;
  }
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_set_format(a, ARCHIVE_FORMAT_TAR));
  assertEqualIntA(a, ARCHIVE_OK,
      archive_read_append_filter_program(a, "gzip -d"));
  assertEqualIntA(a, ARCHIVE_OK,
      archive_read_open_memory(a, archive, sizeof(archive)));
  assertEqualIntA(a, ARCHIVE_OK,
      archive_read_next_header(a, &ae));
  assertEqualInt(archive_filter_code(a, 0), ARCHIVE_FILTER_PROGRAM);
  assertEqualInt(archive_format(a), ARCHIVE_FORMAT_TAR_USTAR);
  assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_append_filter_wrong_program)
{
  struct archive_entry *ae;
  struct archive *a;
#if !defined(_WIN32) || defined(__CYGWIN__)
  FILE * fp;
  int fd;
  fpos_t pos;
#endif

  /*
   * If we have "bunzip2 -q", try using that.
   */
  if (!canRunCommand("bunzip2 -h")) {
    skipping("Can't run bunzip2 program on this platform");
    return;
  }

#if !defined(_WIN32) || defined(__CYGWIN__)
  /* bunzip2 will write to stderr, redirect it to a file */
  fflush(stderr);
  fgetpos(stderr, &pos);
  assert((fd = dup(fileno(stderr))) != -1);
  fp = freopen("stderr1", "w", stderr);
#endif

  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_set_format(a, ARCHIVE_FORMAT_TAR));
  assertEqualIntA(a, ARCHIVE_OK,
      archive_read_append_filter_program(a, "bunzip2 -q"));
  assertEqualIntA(a, ARCHIVE_OK,
      archive_read_open_memory(a, archive, sizeof(archive)));
  assertA(archive_read_next_header(a, &ae) < (ARCHIVE_WARN));
  assertEqualIntA(a, ARCHIVE_WARN, archive_read_close(a));
  assertEqualInt(ARCHIVE_OK, archive_read_free(a));

#if !defined(_WIN32) || defined(__CYGWIN__)
  /* restore stderr and verify results */
  if (fp != NULL) {
    fflush(stderr);
    dup2(fd, fileno(stderr));
    clearerr(stderr);
    (void)fsetpos(stderr, &pos);
  }
  close(fd);
  assertTextFileContents("bunzip2: (stdin) is not a bzip2 file.\n", "stderr1");
#endif
}
