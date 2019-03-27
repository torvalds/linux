/*-
 * Copyright (c) 2009 Michihiro NAKAJIMA
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

DEFINE_TEST(test_patterns)
{
	FILE *f;
	int r;
	const char *reffile2 = "test_patterns_2.tar";
	const char *reffile3 = "test_patterns_3.tar";
	const char *reffile4 = "test_patterns_4.tar";

	const char *tar2aExpected[] = {
		"/tmp/foo/bar/",
		"/tmp/foo/bar/baz",
		NULL
	};

	/*
	 * Test basic command-line pattern handling.
	 */

	/*
	 * Test 1: Files on the command line that don't get matched
	 * didn't produce an error.
	 *
	 * John Baldwin reported this problem in PR bin/121598
	 */
	f = fopen("foo", "w");
	assert(f != NULL);
	fclose(f);
	r = systemf("%s cfv tar1.tgz foo > tar1a.out 2> tar1a.err", testprog);
	assertEqualInt(r, 0);
	r = systemf("%s xv --no-same-owner -f tar1.tgz foo bar > tar1b.out 2> tar1b.err", testprog);
	failure("tar should return non-zero because a file was given on the command line that's not in the archive");
	assert(r != 0);

	/*
	 * Test 2: Check basic matching of full paths that start with /
	 */
	extract_reference_file(reffile2);

	r = systemf("%s tf %s /tmp/foo/bar > tar2a.out 2> tar2a.err",
	    testprog, reffile2);
	assertEqualInt(r, 0);
	assertFileContainsLinesAnyOrder("tar2a.out", tar2aExpected);
	assertEmptyFile("tar2a.err");

	/*
	 * Test 3 archive has some entries starting with '/' and some not.
	 */
	extract_reference_file(reffile3);

	/* Test 3a:  Pattern tmp/foo/bar should not match /tmp/foo/bar */
	r = systemf("%s x --no-same-owner -f %s tmp/foo/bar > tar3a.out 2> tar3a.err",
	    testprog, reffile3);
	assert(r != 0);
	assertEmptyFile("tar3a.out");

	/* Test 3b:  Pattern /tmp/foo/baz should not match tmp/foo/baz */
	assertNonEmptyFile("tar3a.err");
	/* Again, with the '/' */
	r = systemf("%s x --no-same-owner -f %s /tmp/foo/baz > tar3b.out 2> tar3b.err",
	    testprog, reffile3);
	assert(r != 0);
	assertEmptyFile("tar3b.out");
	assertNonEmptyFile("tar3b.err");

	/* Test 3c: ./tmp/foo/bar should not match /tmp/foo/bar */
	r = systemf("%s x --no-same-owner -f %s ./tmp/foo/bar > tar3c.out 2> tar3c.err",
	    testprog, reffile3);
	assert(r != 0);
	assertEmptyFile("tar3c.out");
	assertNonEmptyFile("tar3c.err");

	/* Test 3d: ./tmp/foo/baz should match tmp/foo/baz */
	r = systemf("%s x --no-same-owner -f %s ./tmp/foo/baz > tar3d.out 2> tar3d.err",
	    testprog, reffile3);
	assertEqualInt(r, 0);
	assertEmptyFile("tar3d.out");
	assertEmptyFile("tar3d.err");
	assertFileExists("tmp/foo/baz/bar");

	/*
	 * Test 4 archive has some entries starting with windows drive letters
	 * such as 'c:\', '//./c:/' or '//?/c:/'.
	 */
	extract_reference_file(reffile4);

	r = systemf("%s x --no-same-owner -f %s -C tmp > tar4.out 2> tar4.err",
	    testprog, reffile4);
	assert(r != 0);
	assertEmptyFile("tar4.out");
	assertNonEmptyFile("tar4.err");

	for (r = 1; r <= 54; r++) {
		char file_a[] = "tmp/fileXX";
		char file_b1[] = "tmp/server/share/fileXX";
		char file_b2[] = "tmp/server\\share\\fileXX";
		char file_c[] = "tmp/../fileXX";
		char file_d[] = "tmp/../../fileXX";
		char *filex;
		int xsize;

		switch (r) {
		case 15: case 18:
			/*
			 * Including server and share names.
			 * //?/UNC/server/share/file15
			 * //?/unc/server/share/file18
			 */
			filex = file_b1;
			xsize = sizeof(file_b1);
			break;
		case 35: case 38: case 52:
			/*
			 * Including server and share names.
			 * \\?\UNC\server\share\file35
			 * \\?\unc\server\share\file38
			 * \/?/uNc/server\share\file52
			 */
			filex = file_b2;
			xsize = sizeof(file_b2);
			break;
		default:
			filex = file_a;
			xsize = sizeof(file_a);
			break;
		}
		filex[xsize-3] = '0' + r / 10;
		filex[xsize-2] = '0' + r % 10;
		switch (r) {
		case 5: case 6: case 17: case 20: case 25:
		case 26: case 37: case 40: case 43: case 54:
			/*
			 * Not extracted patterns.
			 * D:../file05
			 * c:../../file06
			 * //?/UNC/../file17
			 * //?/unc/../file20
			 * z:..\file25
			 * c:..\..\file26
			 * \\?\UNC\..\file37
			 * \\?\unc\..\file40
			 * c:../..\file43
			 * \/?\UnC\../file54
			 */
			assertFileNotExists(filex);
			if (r == 6 || r == 26 || r == 43) {
				filex = file_d;
				xsize = sizeof(file_d);
			} else {
				filex = file_c;
				xsize = sizeof(file_c);
			}
			filex[xsize-3] = '0' + r / 10;
			filex[xsize-2] = '0' + r % 10;
			assertFileNotExists(filex);
			break;
		default:
			/* Extracted patterns. */
			assertFileExists(filex);
			break;
		}
	}
}
