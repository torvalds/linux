/*-
 * Copyright (c) 2011 Tim Kientzle
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

#define __LIBARCHIVE_TEST
#include "archive_string.h"

#define EXTENT 32

#define assertStringSizes(strlen, buflen, as) \
	assertEqualInt(strlen, (as).length); \
	assertEqualInt(buflen, (as).buffer_length);

#define assertExactString(strlen, buflen, data, as) \
	do { \
		assertStringSizes(strlen, buflen, as); \
		assertEqualString(data, (as).s); \
	} while (0)

#define assertNonNULLString(strlen, buflen, as) \
	do { \
		assertStringSizes(strlen, buflen, as); \
		assert(NULL != (as).s); \
	} while (0)

static void
test_archive_string_ensure(void)
{
	struct archive_string s;

	archive_string_init(&s);
	assertExactString(0, 0, NULL, s);

	/* single-extent allocation */
	assert(&s == archive_string_ensure(&s, 5));
	assertNonNULLString(0, EXTENT, s);

	/* what happens around extent boundaries? */
	assert(&s == archive_string_ensure(&s, EXTENT - 1));
	assertNonNULLString(0, EXTENT, s);

	assert(&s == archive_string_ensure(&s, EXTENT));
	assertNonNULLString(0, EXTENT, s);

	assert(&s == archive_string_ensure(&s, EXTENT + 1));
	assertNonNULLString(0, 2 * EXTENT, s);

	archive_string_free(&s);
}

static void
test_archive_strcat(void)
{
	struct archive_string s;

	archive_string_init(&s);
	assertExactString(0, 0, NULL, s);

	/* null target, empty source */
	assert(&s == archive_strcat(&s, ""));
	assertExactString(0, EXTENT, "", s);

	/* empty target, empty source */
	assert(&s == archive_strcat(&s, ""));
	assertExactString(0, EXTENT, "", s);

	/* empty target, non-empty source */
	assert(&s == archive_strcat(&s, "fubar"));
	assertExactString(5, EXTENT, "fubar", s);

	/* non-empty target, non-empty source */
	assert(&s == archive_strcat(&s, "baz"));
	assertExactString(8, EXTENT, "fubarbaz", s);

	archive_string_free(&s);
}

static void
test_archive_strappend_char(void)
{
	struct archive_string s;

	archive_string_init(&s);
	assertExactString(0, 0, NULL, s);

	/* null target */
	archive_strappend_char(&s, 'X');
	assertExactString(1, EXTENT, "X", s);

	/* non-empty target */
	archive_strappend_char(&s, 'Y');
	assertExactString(2, EXTENT, "XY", s);

	archive_string_free(&s);
}

/* archive_strnXXX() tests focus on length handling.
 * other behaviors are tested by proxy through archive_strXXX()
 */

static void
test_archive_strncat(void)
{
	struct archive_string s;

	archive_string_init(&s);
	assertExactString(0, 0, NULL, s);

	/* perfect length */
	assert(&s == archive_strncat(&s, "snafu", 5));
	assertExactString(5, EXTENT, "snafu", s);

	/* short read */
	assert(&s == archive_strncat(&s, "barbazqux", 3));
	assertExactString(8, EXTENT, "snafubar", s);

	/* long read is ok too! */
	assert(&s == archive_strncat(&s, "snafu", 8));
	assertExactString(13, EXTENT, "snafubarsnafu", s);

	archive_string_free(&s);
}

static void
test_archive_strncpy(void)
{
	struct archive_string s;

	archive_string_init(&s);
	assertExactString(0, 0, NULL, s);

	/* perfect length */
	assert(&s == archive_strncpy(&s, "fubar", 5));
	assertExactString(5, EXTENT, "fubar", s);

	/* short read */
	assert(&s == archive_strncpy(&s, "snafubar", 5));
	assertExactString(5, EXTENT, "snafu", s);

	/* long read is ok too! */
	assert(&s == archive_strncpy(&s, "snafu", 8));
	assertExactString(5, EXTENT, "snafu", s);

	archive_string_free(&s);
}

static void
test_archive_strcpy(void)
{
	struct archive_string s;

	archive_string_init(&s);
	assertExactString(0, 0, NULL, s);

	/* null target */
	assert(&s == archive_strcpy(&s, "snafu"));
	assertExactString(5, EXTENT, "snafu", s);

	/* dirty target */
	assert(&s == archive_strcpy(&s, "foo"));
	assertExactString(3, EXTENT, "foo", s);

	/* dirty target, empty source */
	assert(&s == archive_strcpy(&s, ""));
	assertExactString(0, EXTENT, "", s);

	archive_string_free(&s);
}

static void
test_archive_string_concat(void)
{
	struct archive_string s, t, u, v;

	archive_string_init(&s);
	assertExactString(0, 0, NULL, s);
	archive_string_init(&t);
	assertExactString(0, 0, NULL, t);
	archive_string_init(&u);
	assertExactString(0, 0, NULL, u);
	archive_string_init(&v);
	assertExactString(0, 0, NULL, v);

	/* null target, null source */
	archive_string_concat(&t, &s);
	assertExactString(0, 0, NULL, s);
	assertExactString(0, EXTENT, "", t);

	/* null target, empty source */
	assert(&s == archive_strcpy(&s, ""));
	archive_string_concat(&u, &s);
	assertExactString(0, EXTENT, "", s);
	assertExactString(0, EXTENT, "", u);

	/* null target, non-empty source */
	assert(&s == archive_strcpy(&s, "foo"));
	archive_string_concat(&v, &s);
	assertExactString(3, EXTENT, "foo", s);
	assertExactString(3, EXTENT, "foo", v);

	/* empty target, empty source */
	assert(&s == archive_strcpy(&s, ""));
	assert(&t == archive_strcpy(&t, ""));
	archive_string_concat(&t, &s);
	assertExactString(0, EXTENT, "", s);
	assertExactString(0, EXTENT, "", t);

	/* empty target, non-empty source */
	assert(&s == archive_strcpy(&s, "snafu"));
	assert(&t == archive_strcpy(&t, ""));
	archive_string_concat(&t, &s);
	assertExactString(5, EXTENT, "snafu", s);
	assertExactString(5, EXTENT, "snafu", t);

	archive_string_free(&v);
	archive_string_free(&u);
	archive_string_free(&t);
	archive_string_free(&s);
}

static void
test_archive_string_copy(void)
{
	struct archive_string s, t, u, v;

	archive_string_init(&s);
	assertExactString(0, 0, NULL, s);
	archive_string_init(&t);
	assertExactString(0, 0, NULL, t);
	archive_string_init(&u);
	assertExactString(0, 0, NULL, u);
	archive_string_init(&v);
	assertExactString(0, 0, NULL, v);

	/* null target, null source */
	archive_string_copy(&t, &s);
	assertExactString(0, 0, NULL, s);
	assertExactString(0, EXTENT, "", t);

	/* null target, empty source */
	archive_string_copy(&u, &t);
	assertExactString(0, EXTENT, "", t);
	assertExactString(0, EXTENT, "", u);

	/* empty target, empty source */
	archive_string_copy(&u, &t);
	assertExactString(0, EXTENT, "", t);
	assertExactString(0, EXTENT, "", u);

	/* null target, non-empty source */
	assert(NULL != archive_strcpy(&s, "snafubar"));
	assertExactString(8, EXTENT, "snafubar", s);

	archive_string_copy(&v, &s);
	assertExactString(8, EXTENT, "snafubar", s);
	assertExactString(8, EXTENT, "snafubar", v);

	/* empty target, non-empty source */
	assertExactString(0, EXTENT, "", t);
	archive_string_copy(&t, &s);
	assertExactString(8, EXTENT, "snafubar", s);
	assertExactString(8, EXTENT, "snafubar", t);

	/* non-empty target, non-empty source */
	assert(NULL != archive_strcpy(&s, "fubar"));
	assertExactString(5, EXTENT, "fubar", s);

	archive_string_copy(&t, &s);
	assertExactString(5, EXTENT, "fubar", s);
	assertExactString(5, EXTENT, "fubar", t);

	archive_string_free(&v);
	archive_string_free(&u);
	archive_string_free(&t);
	archive_string_free(&s);
}

static void
test_archive_string_sprintf(void)
{
	struct archive_string s;
#define S16 "0123456789abcdef"
#define S32 S16 S16
#define S64 S32 S32
#define S128 S64 S64
	const char *s32 = S32;
	const char *s33 = S32 "0";
	const char *s64 = S64;
	const char *s65 = S64 "0";
	const char *s128 = S128;
	const char *s129 = S128 "0";
#undef S16
#undef S32
#undef S64
#undef S128

	archive_string_init(&s);
	assertExactString(0, 0, NULL, s);

	archive_string_sprintf(&s, "%s", "");
	assertExactString(0, 2 * EXTENT, "", s);

	archive_string_empty(&s);
	archive_string_sprintf(&s, "%s", s32);
	assertExactString(32, 2 * EXTENT, s32, s);

	archive_string_empty(&s);
	archive_string_sprintf(&s, "%s", s33);
	assertExactString(33, 2 * EXTENT, s33, s);

	archive_string_empty(&s);
	archive_string_sprintf(&s, "%s", s64);
	assertExactString(64, 4 * EXTENT, s64, s);

	archive_string_empty(&s);
	archive_string_sprintf(&s, "%s", s65);
	assertExactString(65, 4 * EXTENT, s65, s);

	archive_string_empty(&s);
	archive_string_sprintf(&s, "%s", s128);
	assertExactString(128, 8 * EXTENT, s128, s);

	archive_string_empty(&s);
	archive_string_sprintf(&s, "%s", s129);
	assertExactString(129, 8 * EXTENT, s129, s);

	archive_string_empty(&s);
	archive_string_sprintf(&s, "%d", 1234567890);
	assertExactString(10, 8 * EXTENT, "1234567890", s);

	archive_string_free(&s);
}

DEFINE_TEST(test_archive_string)
{
	test_archive_string_ensure();
	test_archive_strcat();
	test_archive_strappend_char();
	test_archive_strncat();
	test_archive_strncpy();
	test_archive_strcpy();
	test_archive_string_concat();
	test_archive_string_copy();
	test_archive_string_sprintf();
}

static const char *strings[] =
{
  "dir/path",
  "dir/path2",
  "dir/path3",
  "dir/path4",
  "dir/path5",
  "dir/path6",
  "dir/path7",
  "dir/path8",
  "dir/path9",
  "dir/subdir/path",
  "dir/subdir/path2",
  "dir/subdir/path3",
  "dir/subdir/path4",
  "dir/subdir/path5",
  "dir/subdir/path6",
  "dir/subdir/path7",
  "dir/subdir/path8",
  "dir/subdir/path9",
  "dir2/path",
  "dir2/path2",
  "dir2/path3",
  "dir2/path4",
  "dir2/path5",
  "dir2/path6",
  "dir2/path7",
  "dir2/path8",
  "dir2/path9",
  NULL
};

DEFINE_TEST(test_archive_string_sort)
{
  unsigned int i, j, size;
  char **test_strings, *tmp;

  srand((unsigned int)time(NULL));
  size = sizeof(strings) / sizeof(char *);
  assert((test_strings = (char **)calloc(1, sizeof(strings))) != NULL);
  for (i = 0; i < (size - 1); i++)
    assert((test_strings[i] = strdup(strings[i])) != NULL);

  /* Shuffle the test strings */
  for (i = 0; i < (size - 1); i++)
  {
    j = rand() % ((size - 1) - i);
    j += i;
    tmp = test_strings[i];
    test_strings[i] = test_strings[j];
    test_strings[j] = tmp;
  }

  /* Sort and test */
  assertEqualInt(ARCHIVE_OK, archive_utility_string_sort(test_strings));
  for (i = 0; i < (size - 1); i++)
    assertEqualString(test_strings[i], strings[i]);

  for (i = 0; i < (size - 1); i++)
    free(test_strings[i]);
  free(test_strings);
}
