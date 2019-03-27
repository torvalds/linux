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

/*
 * As reported by Bernd Walter:  Some people are in the habit of
 * using "find -d" to generate a list for cpio -p because that
 * copies the top-level dir last, which preserves owner and mode
 * information.  That's not necessary for bsdcpio (libarchive defers
 * restoring directory information), but bsdcpio should still generate
 * the correct results with this usage.
 */

DEFINE_TEST(test_passthrough_reverse)
{
	int r;
	FILE *filelist;

	assertUmask(0);

	/*
	 * Create an assortment of files on disk.
	 */
	filelist = fopen("filelist", "w");

	/* Directory. */
	assertMakeDir("dir", 0743);

	/* File with 10 bytes content. */
	assertMakeFile("dir/file", 0644, "1234567890");
	fprintf(filelist, "dir/file\n");

	/* Write dir last. */
	fprintf(filelist, "dir\n");

	/* All done. */
	fclose(filelist);


	/*
	 * Use cpio passthrough mode to copy files to another directory.
	 */
	r = systemf("%s -pdvm out <filelist >stdout 2>stderr", testprog);
	failure("Error invoking %s -pd out", testprog);
	assertEqualInt(r, 0);

	assertChdir("out");

	/* Verify stderr and stdout. */
	assertTextFileContents("out/dir/file\nout/dir\n1 block\n",
	    "../stderr");
	assertEmptyFile("../stdout");

	/* dir */
	assertIsDir("dir", 0743);


	/* Regular file. */
	assertIsReg("dir/file", 0644);
	assertFileSize("dir/file", 10);
	assertFileNLinks("dir/file", 1);
}
