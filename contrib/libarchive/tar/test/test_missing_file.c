/*-
 * Copyright (c) 2016 Tim Kientzle
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

DEFINE_TEST(test_missing_file)
{
	const char * invalid_stderr[] = { "INTERNAL ERROR", NULL };
	assertMakeFile("file1", 0644, "file1");
	assertMakeFile("file2", 0644, "file2");
	assert(0 == systemf("%s -cf archive.tar file1 file2 2>stderr1", testprog));
	assertEmptyFile("stderr1");
	assert(0 != systemf("%s -cf archive.tar file1 file2 file3 2>stderr2", testprog));
	assertFileContainsNoInvalidStrings("stderr2", invalid_stderr);
	assert(0 != systemf("%s -cf archive.tar 2>stderr3", testprog));
	assertFileContainsNoInvalidStrings("stderr3", invalid_stderr);
	assert(0 != systemf("%s -cf archive.tar file3 file4 2>stderr4", testprog));
	assertFileContainsNoInvalidStrings("stderr4", invalid_stderr);
}
