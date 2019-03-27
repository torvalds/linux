/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * Copyright (c) 2012 Michihiro NAKAJIMA
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

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#if defined(__APPLE__) && defined(UF_COMPRESSED) && defined(HAVE_SYS_XATTR_H)\
	&& defined(HAVE_ZLIB_H)
static int
has_xattr(const char *filename, const char *xattrname)
{
	char *nl, *nlp;
	ssize_t r;
	int exisiting;

	r = listxattr(filename, NULL, 0, XATTR_SHOWCOMPRESSION);
	if (r < 0)
		return (0);
	if (r == 0)
		return (0);

	assert((nl = malloc(r)) != NULL);
	if (nl == NULL)
		return (0);

	r = listxattr(filename, nl, r, XATTR_SHOWCOMPRESSION);
	if (r < 0) {
		free(nl);
		return (0);
	}

	exisiting = 0;
	for (nlp = nl; nlp < nl + r; nlp += strlen(nlp) + 1) {
		if (strcmp(nlp, xattrname) == 0) {
			exisiting = 1;
			break;
		}
	}
	free(nl);
	return (exisiting);
}
static int
get_rsrc_footer(const char *filename, char *buff, size_t s)
{
	ssize_t r;

	r = getxattr(filename, "com.apple.ResourceFork", NULL, 0, 0,
	    XATTR_SHOWCOMPRESSION);
	if (r < (ssize_t)s)
		return (-1);
	r = getxattr(filename, "com.apple.ResourceFork", buff, s,
	    r - s, XATTR_SHOWCOMPRESSION);
	if (r < (ssize_t)s)
		return (-1);
	return (0);
}

#endif

/*
 * Exercise HFS+ Compression.
 */
DEFINE_TEST(test_write_disk_hfs_compression)
{
#if !defined(__APPLE__) || !defined(UF_COMPRESSED) || !defined(HAVE_SYS_XATTR_H)\
	|| !defined(HAVE_ZLIB_H)
	skipping("MacOS-specific HFS+ Compression test");
#else
	const char *refname = "test_write_disk_hfs_compression.tgz";
	struct archive *ad, *a;
	struct archive_entry *ae;
	struct stat st;
	char rsrc[50];
	static const char rsrc_footer[50] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x1c, 0x00, 0x32, 0x00, 0x00, 'c',  'm',
		'p', 'f',   0x00, 0x00, 0x00, 0x0a, 0x00, 0x01,
		0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00
	};

	extract_reference_file(refname);

	/*
	 * Extract an archive to disk with HFS+ Compression.
	 */
	assert((ad = archive_write_disk_new()) != NULL);
	assertEqualIntA(ad, ARCHIVE_OK,
	    archive_write_disk_set_standard_lookup(ad));
	assertEqualIntA(ad, ARCHIVE_OK,
	    archive_write_disk_set_options(ad,
		ARCHIVE_EXTRACT_TIME |
		ARCHIVE_EXTRACT_SECURE_SYMLINKS |
		ARCHIVE_EXTRACT_SECURE_NODOTDOT |
		ARCHIVE_EXTRACT_HFS_COMPRESSION_FORCED));

	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_filename(a,
	    refname, 512 * 20));

	assertMakeDir("hfscmp", 0755);
	assertChdir("hfscmp");

	/* Extract file1. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_extract2(a, ae, ad));
	/* Extract README. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_extract2(a, ae, ad));
	/* Extract NEWS. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_extract2(a, ae, ad));
	/* Extract Makefile. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_extract2(a, ae, ad));

	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	assertEqualIntA(ad, ARCHIVE_OK, archive_write_free(ad));

	/* Test file1. */
	assertEqualInt(0, stat("file1", &st));
	assertEqualInt(UF_COMPRESSED, st.st_flags & UF_COMPRESSED);
	assertFileSize("file1", 8);
	failure("'%s' should not have Resource Fork", "file1");
	assertEqualInt(0, has_xattr("file1", "com.apple.ResourceFork"));
	failure("'%s' should have decompfs xattr", "file1");
	assertEqualInt(1, has_xattr("file1", "com.apple.decmpfs"));

	/* Test README. */
	assertEqualInt(0, stat("README", &st));
	assertEqualInt(UF_COMPRESSED, st.st_flags & UF_COMPRESSED);
	assertFileSize("README", 6586);
	failure("'%s' should not have Resource Fork", "README");
	assertEqualInt(0, has_xattr("README", "com.apple.ResourceFork"));
	failure("'%s' should have decompfs xattr", "README");
	assertEqualInt(1, has_xattr("README", "com.apple.decmpfs"));

	/* Test NEWS. */
	assertEqualInt(0, stat("NEWS", &st));
	assertEqualInt(UF_COMPRESSED, st.st_flags & UF_COMPRESSED);
	assertFileSize("NEWS", 28438);
	failure("'%s' should have Resource Fork", "NEWS");
	assertEqualInt(1, has_xattr("NEWS", "com.apple.ResourceFork"));
	failure("'%s' should have decompfs xattr", "NEWS");
	assertEqualInt(1, has_xattr("NEWS", "com.apple.decmpfs"));
	assertEqualInt(0, get_rsrc_footer("NEWS", rsrc, sizeof(rsrc)));
	failure("Resource Fork should have consistent 50 bytes data");
	assertEqualMem(rsrc_footer, rsrc, sizeof(rsrc));

	/* Test Makefile. */
	assertEqualInt(0, stat("Makefile", &st));
	assertEqualInt(UF_COMPRESSED, st.st_flags & UF_COMPRESSED);
	assertFileSize("Makefile", 1264000);
	failure("'%s' should have Resource Fork", "Makefile");
	assertEqualInt(1, has_xattr("Makefile", "com.apple.ResourceFork"));
	failure("'%s' should have decompfs xattr", "Makefile");
	assertEqualInt(1, has_xattr("Makefile", "com.apple.decmpfs"));
	assertEqualInt(0, get_rsrc_footer("Makefile", rsrc, sizeof(rsrc)));
	failure("Resource Fork should have consistent 50 bytes data");
	assertEqualMem(rsrc_footer, rsrc, sizeof(rsrc));

	assertChdir("..");

	/*
	 * Extract an archive to disk without HFS+ Compression.
	 */
	assert((ad = archive_write_disk_new()) != NULL);
	assertEqualIntA(ad, ARCHIVE_OK,
	    archive_write_disk_set_standard_lookup(ad));
	assertEqualIntA(ad, ARCHIVE_OK,
	    archive_write_disk_set_options(ad,
		ARCHIVE_EXTRACT_TIME |
		ARCHIVE_EXTRACT_SECURE_SYMLINKS |
		ARCHIVE_EXTRACT_SECURE_NODOTDOT));

	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_filename(a,
	    refname, 512 * 20));

	assertMakeDir("nocmp", 0755);
	assertChdir("nocmp");

	/* Extract file1. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_extract2(a, ae, ad));
	/* Extract README. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_extract2(a, ae, ad));
	/* Extract NEWS. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_extract2(a, ae, ad));
	/* Extract Makefile. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_extract2(a, ae, ad));

	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	assertEqualIntA(ad, ARCHIVE_OK, archive_write_free(ad));

	/* Test file1. */
	assertEqualInt(0, stat("file1", &st));
	assertEqualInt(0, st.st_flags & UF_COMPRESSED);
	assertFileSize("file1", 8);
	failure("'%s' should not have Resource Fork", "file1");
	assertEqualInt(0, has_xattr("file1", "com.apple.ResourceFork"));
	failure("'%s' should not have decmpfs", "file1");
	assertEqualInt(0, has_xattr("file1", "com.apple.decmpfs"));

	/* Test README. */
	assertEqualInt(0, stat("README", &st));
	assertEqualInt(0, st.st_flags & UF_COMPRESSED);
	assertFileSize("README", 6586);
	failure("'%s' should not have Resource Fork", "README");
	assertEqualInt(0, has_xattr("README", "com.apple.ResourceFork"));
	failure("'%s' should not have decmpfs", "README");
	assertEqualInt(0, has_xattr("README", "com.apple.decmpfs"));

	/* Test NEWS. */
	assertEqualInt(0, stat("NEWS", &st));
	assertEqualInt(0, st.st_flags & UF_COMPRESSED);
	assertFileSize("NEWS", 28438);
	failure("'%s' should not have Resource Fork", "NEWS");
	assertEqualInt(0, has_xattr("NEWS", "com.apple.ResourceFork"));
	failure("'%s' should not have decmpfs", "NEWS");
	assertEqualInt(0, has_xattr("NEWS", "com.apple.decmpfs"));

	/* Test Makefile. */
	assertEqualInt(0, stat("Makefile", &st));
	assertEqualInt(0, st.st_flags & UF_COMPRESSED);
	assertFileSize("Makefile", 1264000);
	failure("'%s' should not have Resource Fork", "Makefile");
	assertEqualInt(0, has_xattr("Makefile", "com.apple.ResourceFork"));
	failure("'%s' should not have decmpfs", "Makefile");
	assertEqualInt(0, has_xattr("Makefile", "com.apple.decmpfs"));

	assertChdir("..");

	assertEqualFile("hfscmp/file1", "nocmp/file1");
	assertEqualFile("hfscmp/README", "nocmp/README");
	assertEqualFile("hfscmp/NEWS", "nocmp/NEWS");
	assertEqualFile("hfscmp/Makefile", "nocmp/Makefile");
#endif
}
