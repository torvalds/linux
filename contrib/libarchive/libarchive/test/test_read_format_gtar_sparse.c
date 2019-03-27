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


struct contents {
	int64_t	o;
	size_t	s;
	const char *d;
};

struct contents archive_contents_sparse[] = {
	{ 1000000, 1, "a" },
	{ 2000000, 1, "a" },
	{ 3145728, 0, NULL }
};

struct contents archive_contents_sparse2[] = {
	{ 1000000, 1, "a" },
	{ 2000000, 1, "a" },
	{ 3000000, 1, "a" },
	{ 4000000, 1, "a" },
	{ 5000000, 1, "a" },
	{ 6000000, 1, "a" },
	{ 7000000, 1, "a" },
	{ 8000000, 1, "a" },
	{ 9000000, 1, "a" },
	{ 10000000, 1, "a" },
	{ 11000000, 1, "a" },
	{ 12000000, 1, "a" },
	{ 13000000, 1, "a" },
	{ 14000000, 1, "a" },
	{ 15000000, 1, "a" },
	{ 16000000, 1, "a" },
	{ 17000000, 1, "a" },
	{ 18000000, 1, "a" },
	{ 19000000, 1, "a" },
	{ 20000000, 1, "a" },
	{ 21000000, 1, "a" },
	{ 22000000, 1, "a" },
	{ 23000000, 1, "a" },
	{ 24000000, 1, "a" },
	{ 25000000, 1, "a" },
	{ 26000000, 1, "a" },
	{ 27000000, 1, "a" },
	{ 28000000, 1, "a" },
	{ 29000000, 1, "a" },
	{ 30000000, 1, "a" },
	{ 31000000, 1, "a" },
	{ 32000000, 1, "a" },
	{ 33000000, 1, "a" },
	{ 34000000, 1, "a" },
	{ 35000000, 1, "a" },
	{ 36000000, 1, "a" },
	{ 37000000, 1, "a" },
	{ 38000000, 1, "a" },
	{ 39000000, 1, "a" },
	{ 40000000, 1, "a" },
	{ 41000000, 1, "a" },
	{ 42000000, 1, "a" },
	{ 43000000, 1, "a" },
	{ 44000000, 1, "a" },
	{ 45000000, 1, "a" },
	{ 46000000, 1, "a" },
	{ 47000000, 1, "a" },
	{ 48000000, 1, "a" },
	{ 49000000, 1, "a" },
	{ 50000000, 1, "a" },
	{ 51000000, 1, "a" },
	{ 52000000, 1, "a" },
	{ 53000000, 1, "a" },
	{ 54000000, 1, "a" },
	{ 55000000, 1, "a" },
	{ 56000000, 1, "a" },
	{ 57000000, 1, "a" },
	{ 58000000, 1, "a" },
	{ 59000000, 1, "a" },
	{ 60000000, 1, "a" },
	{ 61000000, 1, "a" },
	{ 62000000, 1, "a" },
	{ 63000000, 1, "a" },
	{ 64000000, 1, "a" },
	{ 65000000, 1, "a" },
	{ 66000000, 1, "a" },
	{ 67000000, 1, "a" },
	{ 68000000, 1, "a" },
	{ 69000000, 1, "a" },
	{ 70000000, 1, "a" },
	{ 71000000, 1, "a" },
	{ 72000000, 1, "a" },
	{ 73000000, 1, "a" },
	{ 74000000, 1, "a" },
	{ 75000000, 1, "a" },
	{ 76000000, 1, "a" },
	{ 77000000, 1, "a" },
	{ 78000000, 1, "a" },
	{ 79000000, 1, "a" },
	{ 80000000, 1, "a" },
	{ 81000000, 1, "a" },
	{ 82000000, 1, "a" },
	{ 83000000, 1, "a" },
	{ 84000000, 1, "a" },
	{ 85000000, 1, "a" },
	{ 86000000, 1, "a" },
	{ 87000000, 1, "a" },
	{ 88000000, 1, "a" },
	{ 89000000, 1, "a" },
	{ 90000000, 1, "a" },
	{ 91000000, 1, "a" },
	{ 92000000, 1, "a" },
	{ 93000000, 1, "a" },
	{ 94000000, 1, "a" },
	{ 95000000, 1, "a" },
	{ 96000000, 1, "a" },
	{ 97000000, 1, "a" },
	{ 98000000, 1, "a" },
	{ 99000000, 1, "a" },
	{ 99000001, 0, NULL }
};

struct contents archive_contents_nonsparse[] = {
	{ 0, 1, "a" },
	{ 1, 0, NULL }
};

/*
 * Describe an archive with three entries:
 *
 * File 1: named "sparse"
 *   * a length of 3145728 bytes (3MiB)
 *   * a single 'a' byte at offset 1000000
 *   * a single 'a' byte at offset 2000000
 * File 2: named "sparse2"
 *   * a single 'a' byte at offset 1,000,000, 2,000,000, ..., 99,000,000
 *   * length of 99,000,001
 * File 3: named 'non-sparse'
 *   * length of 1 byte
 *   * contains a single byte 'a'
 */

struct archive_contents {
	const char *filename;
	struct contents *contents;
} files[] = {
	{ "sparse", archive_contents_sparse },
	{ "sparse2", archive_contents_sparse2 },
	{ "non-sparse", archive_contents_nonsparse },
	{ NULL, NULL }
};

static void
verify_archive_file(const char *name, struct archive_contents *ac)
{
	struct archive_entry *ae;
	int err;
	/* data, size, offset of next expected block. */
	struct contents expect;
	/* data, size, offset of block read from archive. */
	struct contents actual;
	const void *p;
	struct archive *a;

	extract_reference_file(name);

	assert((a = archive_read_new()) != NULL);
	assert(0 == archive_read_support_filter_all(a));
	assert(0 == archive_read_support_format_tar(a));
	failure("Can't open %s", name);
	assert(0 == archive_read_open_filename(a, name, 3));

	while (ac->filename != NULL) {
		struct contents *cts = ac->contents;

		if (!assertEqualIntA(a, 0, archive_read_next_header(a, &ae))) {
			assertEqualInt(ARCHIVE_OK, archive_read_free(a));
			return;
		}
		failure("Name mismatch in archive %s", name);
		assertEqualString(ac->filename, archive_entry_pathname(ae));
		assertEqualInt(archive_entry_is_encrypted(ae), 0);
		assertEqualIntA(a, archive_read_has_encrypted_entries(a), ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);

		expect = *cts++;
		while (0 == (err = archive_read_data_block(a,
				 &p, &actual.s, &actual.o))) {
			actual.d = p;
			while (actual.s > 0) {
				char c = *actual.d;
				if(actual.o < expect.o) {
					/*
					 * Any byte before the expected
					 * data must be NULL.
					 */
					failure("%s: pad at offset %d "
					    "should be zero", name, actual.o);
					assertEqualInt(c, 0);
				} else if (actual.o == expect.o) {
					/*
					 * Data at matching offsets must match.
					 */
					assertEqualInt(c, *expect.d);
					expect.d++;
					expect.o++;
					expect.s--;
					/* End of expected? step to next expected. */
					if (expect.s <= 0)
						expect = *cts++;
				} else {
					/*
					 * We found data beyond that expected.
					 */
					failure("%s: Unexpected trailing data",
					    name);
					assert(actual.o <= expect.o);
					archive_read_free(a);
					return;
				}
				actual.d++;
				actual.o++;
				actual.s--;
			}
		}
		failure("%s: should be end of entry", name);
		assertEqualIntA(a, err, ARCHIVE_EOF);
		failure("%s: Size returned at EOF must be zero", name);
		assertEqualInt((int)actual.s, 0);
		failure("%s: Offset of final empty chunk must be same as file size", name);
		assertEqualInt(actual.o, expect.o);
		/* Step to next file description. */
		++ac;
	}

	err = archive_read_next_header(a, &ae);
	assertEqualIntA(a, ARCHIVE_EOF, err);

	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}


DEFINE_TEST(test_read_format_gtar_sparse)
{
	/* Two archives that use the "GNU tar sparse format". */
	verify_archive_file("test_read_format_gtar_sparse_1_13.tar", files);
	verify_archive_file("test_read_format_gtar_sparse_1_17.tar", files);

	/*
	 * libarchive < 1.9 doesn't support the newer --posix sparse formats
	 * from GNU tar 1.15 and later.
	 */

	/*
	 * An archive created by GNU tar 1.17 using --posix --sparse-format=0.1
	 */
	verify_archive_file(
		"test_read_format_gtar_sparse_1_17_posix00.tar",
		files);
	/*
	 * An archive created by GNU tar 1.17 using --posix --sparse-format=0.1
	 */
	verify_archive_file(
		"test_read_format_gtar_sparse_1_17_posix01.tar",
		files);
	/*
	 * An archive created by GNU tar 1.17 using --posix --sparse-format=1.0
	 */
	verify_archive_file(
		"test_read_format_gtar_sparse_1_17_posix10.tar",
		files);
	/*
	 * The last test archive here is a little odd.  First, it's
	 * uncompressed, because that exercises some of the block
	 * reassembly code a little harder.  Second, it includes some
	 * leading comments prior to the sparse block description.
	 * GNU tar doesn't do this, but I think it should, so I want
	 * to ensure that libarchive correctly ignores such comments.
	 * Dump the file, looking for "#!gnu-sparse-format" starting
	 * at byte 0x600.
	 */
	verify_archive_file(
		"test_read_format_gtar_sparse_1_17_posix10_modified.tar",
		files);
}


