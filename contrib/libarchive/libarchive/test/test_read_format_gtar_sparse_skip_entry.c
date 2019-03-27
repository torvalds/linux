/*-
 * Copyright (c) 2014 Michihiro NAKAJIMA
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
__FBSDID("$FreeBSD");

#if defined(__BORLANDC__) || (defined(_MSC_VER) &&  _MSC_VER <= 1300)
# define	LITERAL_LL(x)	x##i64
#else
# define	LITERAL_LL(x)	x##ll
#endif
/*
 * To test skip a sparse file entry, this test does not read file data.
 */
DEFINE_TEST(test_read_format_gtar_sparse_skip_entry)
{
#ifndef __FreeBSD__ /* Backport test. */
	const char *refname = "test_read_format_gtar_sparse_skip_entry.tar.Z.uu";
#else
	const char *refname = "test_read_format_gtar_sparse_skip_entry.tar.Z";
#endif
	struct archive *a;
	struct archive_entry *ae;
	const void *p;
	size_t s;
	int64_t o;

#ifndef __FreeBSD__ /* Backport test. */
	copy_reference_file(refname);
#else
	extract_reference_file(refname);
#endif
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_open_filename(a, refname, 10240));

	/* Verify regular first file. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("a", archive_entry_pathname(ae));
	assertEqualInt(LITERAL_LL(10737418244), archive_entry_size(ae));
#ifndef __FreeBSD__ /* Backport test. */
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a),
	    ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);
#endif

	/* Verify regular second file. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("b", archive_entry_pathname(ae));
	assertEqualInt(4, archive_entry_size(ae));
#ifndef __FreeBSD__ /* Backport test. */
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a),
	    ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);
#endif


	/* End of archive. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	/* Verify archive format. */
	assertEqualIntA(a, ARCHIVE_FILTER_COMPRESS, archive_filter_code(a, 0));
	assertEqualIntA(a, ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE,
	    archive_format(a));

	/* Close the archive. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));


	/*
	 * Read just one block of a sparse file and skip it.
	 */
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_open_filename(a, refname, 10240));

	/* Verify regular first file. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("a", archive_entry_pathname(ae));
	assertEqualInt(LITERAL_LL(10737418244), archive_entry_size(ae));
#ifndef __FreeBSD__ /* Backport test. */
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a),
	    ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);
#endif
	assertEqualInt(0, archive_read_data_block(a, &p, &s, &o));
	assertEqualInt(4096, s);
	assertEqualInt(0, o);


	/* Verify regular second file. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("b", archive_entry_pathname(ae));
	assertEqualInt(4, archive_entry_size(ae));
#ifndef __FreeBSD__ /* Backport test. */
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a),
	    ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);
#endif


	/* End of archive. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	/* Verify archive format. */
	assertEqualIntA(a, ARCHIVE_FILTER_COMPRESS, archive_filter_code(a, 0));
	assertEqualIntA(a, ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE,
	    archive_format(a));

	/* Close the archive. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

