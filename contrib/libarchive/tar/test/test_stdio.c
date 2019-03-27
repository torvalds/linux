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

DEFINE_TEST(test_stdio)
{
	FILE *filelist;
	char *p;
	size_t s;
	int r;

	assertUmask(0);

	/*
	 * Create a couple of files on disk.
	 */
	/* File */
	assertMakeFile("f", 0755, "abc");
	/* Link to above file. */
	assertMakeHardlink("l", "f");

	/* Create file list (text mode here) */
	filelist = fopen("filelist", "w");
	assert(filelist != NULL);
	fprintf(filelist, "f\n");
	fprintf(filelist, "l\n");
	fclose(filelist);

	/*
	 * Archive/dearchive with a variety of options, verifying
	 * stdio paths.
	 */

	/* 'cf' should generate no output unless there's an error. */
	r = systemf("%s cf archive f l >cf.out 2>cf.err", testprog);
	assertEqualInt(r, 0);
	assertEmptyFile("cf.out");
	assertEmptyFile("cf.err");

	/* 'cvf' should generate file list on stderr, empty stdout. */
	r = systemf("%s cvf archive f l >cvf.out 2>cvf.err", testprog);
	assertEqualInt(r, 0);
	failure("'cv' writes filenames to stderr, nothing to stdout (SUSv2)\n"
	    "Note that GNU tar writes the file list to stdout by default.");
	assertEmptyFile("cvf.out");
	/* TODO: Verify cvf.err has file list in SUSv2-prescribed format. */

	/* 'cvf -' should generate file list on stderr, archive on stdout. */
	r = systemf("%s cvf - f l >cvf-.out 2>cvf-.err", testprog);
	assertEqualInt(r, 0);
	failure("cvf - should write archive to stdout");
	/* TODO: Verify cvf-.out has archive. */
	failure("cvf - should write file list to stderr (SUSv2)");
	/* TODO: Verify cvf-.err has verbose file list. */

	/* 'tf' should generate file list on stdout, empty stderr. */
	r = systemf("%s tf archive >tf.out 2>tf.err", testprog);
	assertEqualInt(r, 0);
	assertEmptyFile("tf.err");
	failure("'t' mode should write results to stdout");
	/* TODO: Verify tf.out has file list. */

	/* 'tvf' should generate file list on stdout, empty stderr. */
	r = systemf("%s tvf archive >tvf.out 2>tvf.err", testprog);
	assertEqualInt(r, 0);
	assertEmptyFile("tvf.err");
	failure("'tv' mode should write results to stdout");
	/* TODO: Verify tvf.out has file list. */

	/* 'tvf -' uses stdin, file list on stdout, empty stderr. */
	r = systemf("%s tvf - < archive >tvf-.out 2>tvf-.err", testprog);
	assertEqualInt(r, 0);
	assertEmptyFile("tvf-.err");
	/* TODO: Verify tvf-.out has file list. */

	/* Basic 'xf' should generate no output on stdout or stderr. */
	r = systemf("%s xf archive >xf.out 2>xf.err", testprog);
	assertEqualInt(r, 0);
	assertEmptyFile("xf.err");
	assertEmptyFile("xf.out");

	/* 'xvf' should generate list on stderr, empty stdout. */
	r = systemf("%s xvf archive >xvf.out 2>xvf.err", testprog);
	assertEqualInt(r, 0);
	assertEmptyFile("xvf.out");
	/* TODO: Verify xvf.err */

	/* 'xvOf' should generate list on stderr, file contents on stdout. */
	r = systemf("%s xvOf archive >xvOf.out 2>xvOf.err", testprog);
	assertEqualInt(r, 0);
	/* Verify xvOf.out is the file contents */
	p = slurpfile(&s, "xvOf.out");
	assertEqualInt((int)s, 3);
	assertEqualMem(p, "abc", 3);
	/* TODO: Verify xvf.err */
	free(p);

	/* 'xvf -' should generate list on stderr, empty stdout. */
	r = systemf("%s xvf - < archive >xvf-.out 2>xvf-.err", testprog);
	assertEqualInt(r, 0);
	assertEmptyFile("xvf-.out");
	/* TODO: Verify xvf-.err */
}
