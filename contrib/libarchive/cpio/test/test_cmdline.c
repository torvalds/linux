/*-
 * Copyright (c) 2003-2009 Tim Kientzle
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
 * Test the command-line parsing.
 */

DEFINE_TEST(test_cmdline)
{
	FILE *f;

	/* Create an empty file. */
	f = fopen("empty", "wb");
	assert(f != NULL);
	fclose(f);

	failure("-Q is an invalid option on every cpio program I know of");
	assert(0 != systemf("%s -i -Q <empty >1.out 2>1.err", testprog));
	assertEmptyFile("1.out");

	failure("-f requires an argument");
	assert(0 != systemf("%s -if <empty >2.out 2>2.err", testprog));
	assertEmptyFile("2.out");

	failure("-f requires an argument");
	assert(0 != systemf("%s -i -f <empty >3.out 2>3.err", testprog));
	assertEmptyFile("3.out");

	failure("--format requires an argument");
	assert(0 != systemf("%s -i --format <empty >4.out 2>4.err", testprog));
	assertEmptyFile("4.out");

	failure("--badopt is an invalid option");
	assert(0 != systemf("%s -i --badop <empty >5.out 2>5.err", testprog));
	assertEmptyFile("5.out");

	failure("--badopt is an invalid option");
	assert(0 != systemf("%s -i --badopt <empty >6.out 2>6.err", testprog));
	assertEmptyFile("6.out");

	failure("--n is ambiguous");
	assert(0 != systemf("%s -i --n <empty >7.out 2>7.err", testprog));
	assertEmptyFile("7.out");

	failure("--create forbids an argument");
	assert(0 != systemf("%s --create=arg <empty >8.out 2>8.err", testprog));
	assertEmptyFile("8.out");

	failure("-i with empty input should succeed");
	assert(0 == systemf("%s -i <empty >9.out 2>9.err", testprog));
	assertEmptyFile("9.out");

	failure("-o with empty input should succeed");
	assert(0 == systemf("%s -o <empty >10.out 2>10.err", testprog));

	failure("-i -p is nonsense");
	assert(0 != systemf("%s -i -p <empty >11.out 2>11.err", testprog));
	assertEmptyFile("11.out");

	failure("-p -i is nonsense");
	assert(0 != systemf("%s -p -i <empty >12.out 2>12.err", testprog));
	assertEmptyFile("12.out");

	failure("-i -o is nonsense");
	assert(0 != systemf("%s -i -o <empty >13.out 2>13.err", testprog));
	assertEmptyFile("13.out");

	failure("-o -i is nonsense");
	assert(0 != systemf("%s -o -i <empty >14.out 2>14.err", testprog));
	assertEmptyFile("14.out");

	failure("-o -p is nonsense");
	assert(0 != systemf("%s -o -p <empty >15.out 2>15.err", testprog));
	assertEmptyFile("15.out");

	failure("-p -o is nonsense");
	assert(0 != systemf("%s -p -o <empty >16.out 2>16.err", testprog));
	assertEmptyFile("16.out");

	failure("-p with empty input should fail");
	assert(0 != systemf("%s -p <empty >17.out 2>17.err", testprog));
	assertEmptyFile("17.out");
}
