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

#ifdef HAVE_SYS_ACL_H
#include <sys/acl.h>
#endif
#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#if defined(__APPLE__) && defined(UF_COMPRESSED) && defined(HAVE_SYS_XATTR_H)\
	&& defined(HAVE_ZLIB_H)

//
// The test ACL used here is sometimes assigned to the 'Guest' user
// This changes the text and breaks the test.  This function simply
// strips the 'Guest' information from the string to help ensure
// consistent results on different machines.
//
static char _acl_temp[256];
static const char *
clean_acl(const char *acl) {
	char *p, *q;
	if (strlen(acl) >= sizeof(_acl_temp))
		return acl;

	strcpy(_acl_temp, acl);
	p = strstr(_acl_temp, ":Guest:");
	if (p != NULL) {
		fprintf(stderr, "Shortening: %s\n", p + 1);
		memmove(p + 1, p + 6, strlen(p + 6) + 1);
		q = strstr(p + 2, ":");
		fprintf(stderr, "Shortening: %s\n", q);
		memmove(p + 2, q, strlen(q) + 1);
		return _acl_temp;
	}
	return _acl_temp;
}

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

#endif

/*
 * Exercise HFS+ Compression.
 */
DEFINE_TEST(test_write_disk_appledouble)
{
#if !defined(__APPLE__) || !defined(UF_COMPRESSED) || !defined(HAVE_SYS_XATTR_H)\
	|| !defined(HAVE_ZLIB_H)
	skipping("MacOS-specific AppleDouble test");
#else
	const char *refname = "test_write_disk_appledouble.cpio.gz";
	struct archive *ad, *a;
	struct archive_entry *ae;
	struct stat st;
	acl_t acl;

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

	/* Skip "." */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString(".", archive_entry_pathname(ae));
	/* Extract file3. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("./file3", archive_entry_pathname(ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_extract2(a, ae, ad));
	/* Extract ._file3 which will be merged into file3 as metadata. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("./._file3", archive_entry_pathname(ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_extract2(a, ae, ad));

	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	assertEqualIntA(ad, ARCHIVE_OK, archive_write_free(ad));

	/* Test file3. */
	assertEqualInt(0, stat("file3", &st));
	assertEqualInt(UF_COMPRESSED, st.st_flags & UF_COMPRESSED);
	assertFileSize("file3", 8);
	failure("'%s' should not have Resource Fork", "file3");
	assertEqualInt(0, has_xattr("file3", "com.apple.ResourceFork"));
	failure("'%s' should have decompfs xattr", "file3");
	assertEqualInt(1, has_xattr("file3", "com.apple.decmpfs"));
	assert(NULL != (acl = acl_get_file("file3", ACL_TYPE_EXTENDED)));
	assertEqualString(clean_acl(acl_to_text(acl, NULL)),
	    "!#acl 1\n"
	    "user:FFFFEEEE-DDDD-CCCC-BBBB-AAAA000000C9:::deny:read\n"
	    "group:ABCDEFAB-CDEF-ABCD-EFAB-CDEF00000050:admin:80:allow:write\n"
	);
	if (acl) acl_free(acl);
	/* Test ._file3. */
	failure("'file3' should be merged and removed");
	assertFileNotExists("._file3");

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

	/* Skip "." */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString(".", archive_entry_pathname(ae));
	/* Extract file3. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("./file3", archive_entry_pathname(ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_extract2(a, ae, ad));
	/* Extract ._file3 which will be merged into file3 as metadata. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("./._file3", archive_entry_pathname(ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_extract2(a, ae, ad));

	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	assertEqualIntA(ad, ARCHIVE_OK, archive_write_free(ad));

	/* Test file3. */
	assertEqualInt(0, stat("file3", &st));
	assertEqualInt(0, st.st_flags & UF_COMPRESSED);
	assertFileSize("file3", 8);
	failure("'%s' should not have Resource Fork", "file3");
	assertEqualInt(0, has_xattr("file3", "com.apple.ResourceFork"));
	failure("'%s' should not have decmpfs", "file3");
	assertEqualInt(0, has_xattr("file3", "com.apple.decmpfs"));
	assert(NULL != (acl = acl_get_file("file3", ACL_TYPE_EXTENDED)));
	assertEqualString(clean_acl(acl_to_text(acl, NULL)),
	    "!#acl 1\n"
	    "user:FFFFEEEE-DDDD-CCCC-BBBB-AAAA000000C9:::deny:read\n"
	    "group:ABCDEFAB-CDEF-ABCD-EFAB-CDEF00000050:admin:80:allow:write\n"
	);
	if (acl) acl_free(acl);
	/* Test ._file3. */
	failure("'file3' should be merged and removed");
	assertFileNotExists("._file3");

	assertChdir("..");

	assertEqualFile("hfscmp/file3", "nocmp/file3");
#endif
}
