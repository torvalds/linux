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

static int
is_octal(const char *p, size_t l)
{
	while (l > 0) {
		if (*p < '0' || *p > '7')
			return (0);
		--l;
		++p;
	}
	return (1);
}

static int
from_octal(const char *p, size_t l)
{
	int r = 0;

	while (l > 0) {
		r *= 8;
		r += *p - '0';
		--l;
		++p;
	}
	return (r);
}

#if !defined(_WIN32) || defined(__CYGWIN__)
static int
nlinks(const char *p)
{
	struct stat st;
	assertEqualInt(0, stat(p, &st));
	return st.st_nlink;
}
#endif

DEFINE_TEST(test_option_c)
{
	FILE *filelist;
	int r;
	int uid = 1000;
	int dev, ino, gid = 1000;
	time_t t, now;
	char *p, *e;
	size_t s;

	assertUmask(0);

	/*
	 * Create an assortment of files.
	 * TODO: Extend this to cover more filetypes.
	 */
	filelist = fopen("filelist", "w");

	/* "file" */
	assertMakeFile("file", 0644, "1234567890");
	fprintf(filelist, "file\n");

	/* "symlink" */
	if (canSymlink()) {
		assertMakeSymlink("symlink", "file");
		fprintf(filelist, "symlink\n");
	}

	/* "dir" */
	assertMakeDir("dir", 0775);
	/* Record some facts about what we just created: */
	now = time(NULL); /* They were all created w/in last two seconds. */
	fprintf(filelist, "dir\n");

	/* Use the cpio program to create an archive. */
	fclose(filelist);
	r = systemf("%s -R 1000:1000 -oc <filelist >basic.out 2>basic.err", testprog);
	/* Verify that nothing went to stderr. */
	assertTextFileContents("1 block\n", "basic.err");

	/* Assert that the program finished. */
	failure("%s -oc crashed", testprog);
	if (!assertEqualInt(r, 0))
		return;

	/* Verify that stdout is a well-formed cpio file in "odc" format. */
	p = slurpfile(&s, "basic.out");
	assertEqualInt(s, 512);
	e = p;

	/*
	 * Some of these assertions could be stronger, but it's
	 * a little tricky because they depend on the local environment.
	 */

	/* First entry is "file" */
	assert(is_octal(e, 76)); /* Entire header is octal digits. */
	assertEqualMem(e + 0, "070707", 6); /* Magic */
	assert(is_octal(e + 6, 6)); /* dev */
	dev = from_octal(e + 6, 6);
	assert(is_octal(e + 12, 6)); /* ino */
	ino = from_octal(e + 12, 6);
#if defined(_WIN32) && !defined(__CYGWIN__)
	/* Group members bits and others bits do not work. */
	assertEqualMem(e + 18, "100666", 6); /* Mode */
#else
	assertEqualMem(e + 18, "100644", 6); /* Mode */
#endif
	if (uid < 0)
		uid = from_octal(e + 24, 6);
	assertEqualInt(from_octal(e + 24, 6), uid); /* uid */
	assert(is_octal(e + 30, 6)); /* gid */
	gid = from_octal(e + 30, 6);
	assertEqualMem(e + 36, "000001", 6); /* nlink */
	failure("file entries should not have rdev set (dev field was 0%o)",
	    dev);
	assertEqualMem(e + 42, "000000", 6); /* rdev */
	t = from_octal(e + 48, 11); /* mtime */
	assert(t <= now); /* File wasn't created in future. */
	assert(t >= now - 2); /* File was created w/in last 2 secs. */
	assertEqualMem(e + 59, "000005", 6); /* Name size */
	assertEqualMem(e + 65, "00000000012", 11); /* File size */
	assertEqualMem(e + 76, "file\0", 5); /* Name contents */
	assertEqualMem(e + 81, "1234567890", 10); /* File contents */
	e += 91;

	/* "symlink" pointing to "file" */
	if (canSymlink()) {
		assert(is_octal(e, 76)); /* Entire header is octal digits. */
		assertEqualMem(e + 0, "070707", 6); /* Magic */
		assertEqualInt(dev, from_octal(e + 6, 6)); /* dev */
		assert(ino != from_octal(e + 12, 6)); /* ino */
#if !defined(_WIN32) || defined(__CYGWIN__)
		/* On Windows, symbolic link and group members bits and
		 * others bits do not work. */
		assertEqualMem(e + 18, "120777", 6); /* Mode */
#endif
		assertEqualInt(from_octal(e + 24, 6), uid); /* uid */
		assertEqualInt(gid, from_octal(e + 30, 6)); /* gid */
		assertEqualMem(e + 36, "000001", 6); /* nlink */
		failure("file entries should have rdev == 0 (dev was 0%o)",
		    from_octal(e + 6, 6));
		assertEqualMem(e + 42, "000000", 6); /* rdev */
		t = from_octal(e + 48, 11); /* mtime */
		assert(t <= now); /* File wasn't created in future. */
		assert(t >= now - 2); /* File was created w/in last 2 secs. */
		assertEqualMem(e + 59, "000010", 6); /* Name size */
		assertEqualMem(e + 65, "00000000004", 11); /* File size */
		assertEqualMem(e + 76, "symlink\0", 8); /* Name contents */
		assertEqualMem(e + 84, "file", 4); /* Symlink target. */
		e += 88;
	}

	/* "dir" */
	assert(is_octal(e, 76));
	assertEqualMem(e + 0, "070707", 6); /* Magic */
	/* Dev should be same as first entry. */
	assert(is_octal(e + 6, 6)); /* dev */
	assertEqualInt(dev, from_octal(e + 6, 6));
	/* Ino must be different from first entry. */
	assert(is_octal(e + 12, 6)); /* ino */
	assert(ino != from_octal(e + 12, 6));
#if defined(_WIN32) && !defined(__CYGWIN__)
	/* Group members bits and others bits do not work. */
	assertEqualMem(e + 18, "040777", 6); /* Mode */
#else
	/* Accept 042775 to accommodate systems where sgid bit propagates. */
	if (memcmp(e + 18, "042775", 6) != 0)
		assertEqualMem(e + 18, "040775", 6); /* Mode */
#endif
	assertEqualInt(uid, from_octal(e + 24, 6)); /* uid */
	/* Gid should be same as first entry. */
	assert(is_octal(e + 30, 6)); /* gid */
	assertEqualInt(gid, from_octal(e + 30, 6));

#if !defined(_WIN32) || defined(__CYGWIN__)
	assertEqualInt(nlinks("dir"), from_octal(e + 36, 6)); /* Nlink */
#endif

	t = from_octal(e + 48, 11); /* mtime */
	assert(t <= now); /* File wasn't created in future. */
	assert(t >= now - 2); /* File was created w/in last 2 secs. */
	assertEqualMem(e + 59, "000004", 6); /* Name size */
	assertEqualMem(e + 65, "00000000000", 11); /* File size */
	assertEqualMem(e + 76, "dir\0", 4); /* name */
	e += 80;

	/* TODO: Verify other types of entries. */

	/* Last entry is end-of-archive marker. */
	assert(is_octal(e, 76));
	assertEqualMem(e + 0, "070707", 6); /* Magic */
	assertEqualMem(e + 6, "000000", 6); /* dev */
	assertEqualMem(e + 12, "000000", 6); /* ino */
	assertEqualMem(e + 18, "000000", 6); /* Mode */
	assertEqualMem(e + 24, "000000", 6); /* uid */
	assertEqualMem(e + 30, "000000", 6); /* gid */
	assertEqualMem(e + 36, "000001", 6); /* Nlink */
	assertEqualMem(e + 42, "000000", 6); /* rdev */
	assertEqualMem(e + 48, "00000000000", 11); /* mtime */
	assertEqualMem(e + 59, "000013", 6); /* Name size */
	assertEqualMem(e + 65, "00000000000", 11); /* File size */
	assertEqualMem(e + 76, "TRAILER!!!\0", 11); /* Name */

	free(p);
}
