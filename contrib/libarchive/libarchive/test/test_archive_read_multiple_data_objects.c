/*-
 * Copyright (c) 2011 Tim Kientzle
 * Copyright (c) 2011-2012 Andres Mejia
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

#if defined(_WIN32) && !defined(__CYGWIN__)
#define open _open
#define close _close
#define read _read
#if !defined(__BORLANDC__)
#ifdef lseek
#undef lseek
#endif
#define lseek(f, o, w) _lseek(f, (long)(o), (int)(w))
#endif
#endif

static void
test_splitted_file(void)
{
  char buff[64];
  static const char *reffiles[] =
  {
    "test_read_splitted_rar_aa",
    "test_read_splitted_rar_ab",
    "test_read_splitted_rar_ac",
    "test_read_splitted_rar_ad",
    NULL
  };
  const char test_txt[] = "test text document\r\n";
  int size = sizeof(test_txt)-1;
  struct archive_entry *ae;
  struct archive *a;

  extract_reference_files(reffiles);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_support_filter_all(a));
  assertA(0 == archive_read_support_format_all(a));
  assertA(0 == archive_read_open_filenames(a, reffiles, 10240));

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

static void
test_large_splitted_file(void)
{
  static const char *reffiles[] =
  {
    "test_read_large_splitted_rar_aa",
    "test_read_large_splitted_rar_ab",
    "test_read_large_splitted_rar_ac",
    "test_read_large_splitted_rar_ad",
    "test_read_large_splitted_rar_ae",
    NULL
  };
  const char test_txt[] = "gin-bottom: 0in\"><BR>\n</P>\n</BODY>\n</HTML>";
  int size = 241647978, offset = 0;
  char buff[64];
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
  assertEqualInt(size, archive_entry_size(ae));
  assertEqualInt(33188, archive_entry_mode(ae));
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

#define BLOCK_SIZE 10240
struct mydata {
  char *filename;
  void *buffer;
  int fd;
};

static int
file_open(struct archive *a, void *data)
{
  struct mydata *mydata = (struct mydata *)data;
  (void)a;
  if (mydata->fd < 0)
  {
    mydata->fd = open(mydata->filename, O_RDONLY | O_BINARY);
    if (mydata->fd >= 0)
    {
      if ((mydata->buffer = (void*)calloc(1, BLOCK_SIZE)) == NULL)
        return (ARCHIVE_FAILED);
    }
  }
  return (ARCHIVE_OK);
}
static ssize_t
file_read(struct archive *a, void *data, const void **buff)
{
  struct mydata *mydata = (struct mydata *)data;
  (void)a;
  *buff = mydata->buffer;
  return read(mydata->fd, mydata->buffer, BLOCK_SIZE);
}
static int64_t
file_skip(struct archive *a, void *data, int64_t request)
{
  struct mydata *mydata = (struct mydata *)data;
  int64_t result = lseek(mydata->fd, SEEK_CUR, request);
  if (result >= 0)
    return result;
  archive_set_error(a, errno, "Error seeking in '%s'", mydata->filename);
  return -1;
}
static int
file_switch(struct archive *a, void *data1, void *data2)
{
  struct mydata *mydata1 = (struct mydata *)data1;
  struct mydata *mydata2 = (struct mydata *)data2;
  int r = (ARCHIVE_OK);

  (void)a;
  if (mydata1 && mydata1->fd >= 0)
  {
    close(mydata1->fd);
    free(mydata1->buffer);
    mydata1->buffer = NULL;
    mydata1->fd = -1;
  }
  if (mydata2)
  {
    r = file_open(a, mydata2);
  }
  return (r);
}
static int
file_close(struct archive *a, void *data)
{
  struct mydata *mydata = (struct mydata *)data;
  if (mydata == NULL)
    return (ARCHIVE_FATAL);
  file_switch(a, mydata, NULL);
  free(mydata->filename);
  free(mydata);
  return (ARCHIVE_OK);
}
static int64_t
file_seek(struct archive *a, void *data, int64_t request, int whence)
{
  struct mydata *mine = (struct mydata *)data;
  int64_t r;

  (void)a;
  r = lseek(mine->fd, request, whence);
  if (r >= 0)
    return r;
  return (ARCHIVE_FATAL);
}

static void
test_customized_multiple_data_objects(void)
{
  char buff[64];
  static const char *reffiles[] =
  {
    "test_read_splitted_rar_aa",
    "test_read_splitted_rar_ab",
    "test_read_splitted_rar_ac",
    "test_read_splitted_rar_ad",
    NULL
  };
  const char test_txt[] = "test text document\r\n";
  int size = sizeof(test_txt)-1;
  struct archive_entry *ae;
  struct archive *a;
  struct mydata *mydata;
  const char *filename = *reffiles;
  int i;

  extract_reference_files(reffiles);
  assert((a = archive_read_new()) != NULL);
  assertA(0 == archive_read_support_filter_all(a));
  assertA(0 == archive_read_support_format_all(a));

  for (i = 0; filename != NULL;)
  {
    assert((mydata = (struct mydata *)calloc(1, sizeof(*mydata))) != NULL);
    if (mydata == NULL) {
      assertEqualInt(ARCHIVE_OK, archive_read_free(a));
      return;
    }
    assert((mydata->filename =
      (char *)calloc(1, strlen(filename) + 1)) != NULL);
    if (mydata->filename == NULL) {
      free(mydata);
      assertEqualInt(ARCHIVE_OK, archive_read_free(a));
      return;
    }
    strcpy(mydata->filename, filename);
    mydata->fd = -1;
    filename = reffiles[++i];
    assertA(0 == archive_read_append_callback_data(a, mydata));
  }
	assertA(0 == archive_read_set_open_callback(a, file_open));
	assertA(0 == archive_read_set_read_callback(a, file_read));
	assertA(0 == archive_read_set_skip_callback(a, file_skip));
	assertA(0 == archive_read_set_close_callback(a, file_close));
	assertA(0 == archive_read_set_switch_callback(a, file_switch));
  assertA(0 == archive_read_set_seek_callback(a, file_seek));
  assertA(0 == archive_read_open1(a));

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

DEFINE_TEST(test_archive_read_multiple_data_objects)
{
  test_splitted_file();
  test_large_splitted_file();
  test_customized_multiple_data_objects();
}
