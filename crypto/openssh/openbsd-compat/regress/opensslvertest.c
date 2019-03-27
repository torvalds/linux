/*
 * Copyright (c) 2014 Darren Tucker
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>

int ssh_compatible_openssl(long, long);

struct version_test {
	long headerver;
	long libver;
	int result;
} version_tests[] = {
	/* built with 0.9.8b release headers */
	{ 0x0090802fL, 0x0090802fL, 1},	/* exact match */
	{ 0x0090802fL, 0x0090804fL, 1},	/* newer library fix version: ok */
	{ 0x0090802fL, 0x0090801fL, 1},	/* older library fix version: ok */
	{ 0x0090802fL, 0x0090702fL, 0},	/* older library minor version: NO */
	{ 0x0090802fL, 0x0090902fL, 0},	/* newer library minor version: NO */
	{ 0x0090802fL, 0x0080802fL, 0},	/* older library major version: NO */
	{ 0x0090802fL, 0x1000100fL, 0},	/* newer library major version: NO */

	/* built with 1.0.1b release headers */
	{ 0x1000101fL, 0x1000101fL, 1},/* exact match */
	{ 0x1000101fL, 0x1000102fL, 1},	/* newer library patch version: ok */
	{ 0x1000101fL, 0x1000100fL, 1},	/* older library patch version: ok */
	{ 0x1000101fL, 0x1000201fL, 1},	/* newer library fix version: ok */
	{ 0x1000101fL, 0x1000001fL, 0},	/* older library fix version: NO */
	{ 0x1000101fL, 0x1010101fL, 0},	/* newer library minor version: NO */
	{ 0x1000101fL, 0x0000101fL, 0},	/* older library major version: NO */
	{ 0x1000101fL, 0x2000101fL, 0},	/* newer library major version: NO */
};

void
fail(long hver, long lver, int result)
{
	fprintf(stderr, "opensslver: header %lx library %lx != %d \n", hver, lver, result);
	exit(1);
}

int
main(void)
{
	unsigned int i;
	int res;
	long hver, lver;

	for (i = 0; i < sizeof(version_tests) / sizeof(version_tests[0]); i++) {
		hver = version_tests[i].headerver;
		lver = version_tests[i].libver;
		res = version_tests[i].result;
		if (ssh_compatible_openssl(hver, lver) != res)
			fail(hver, lver, res);
	}
	exit(0);
}
