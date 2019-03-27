/*	$NetBSD: h_hash.c,v 1.1 2011/01/02 22:03:25 pgoyette Exp $	*/

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Combined MD5/SHA1 time and regression test.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <md5.h>
#include <sha1.h>


int mflag, rflag, sflag, tflag;

static void
usage(void)
{
	(void)fprintf(stderr,
	    "Usage:\t%s -r[ms] < test-file\n"
	    "\t%s -t[ms]\n",
	    getprogname(), getprogname());
	exit(1);
	/* NOTREACHED */
}

static void
hexdump (unsigned char *buf, int len)
{
	int i;
	for (i=0; i<len; i++) {
		printf("%02x", buf[i]);
	}
	printf("\n");
}


static void
timetest(void)
{
	printf("sorry, not yet\n");
}

#define CHOMP(buf, len, last) 				\
	if ((len > 0) &&				\
	    (buf[len-1] == '\n')) {			\
		buf[len-1] = '\0';			\
		len--;					\
		last = 1;				\
	}

static void
regress(void)
{
	unsigned char buf[1024];
	unsigned char out[20];
	int len, outlen, last;
	
	while (fgets((char *)buf, sizeof(buf), stdin) != NULL) {
		last = 0;

		len = strlen((char *)buf);
		CHOMP(buf, len, last);
		if (mflag) {
			MD5_CTX ctx;

			MD5Init(&ctx);
			MD5Update(&ctx, buf, len);
			while (!last &&
			    fgets((char *)buf, sizeof(buf), stdin) != NULL) {
				len = strlen((char *)buf);
				CHOMP(buf, len, last);
				MD5Update(&ctx, buf, len);
			}
			MD5Final(out, &ctx);
			outlen = 16;
		} else {
			SHA1_CTX ctx;

			SHA1Init(&ctx);
			SHA1Update(&ctx, buf, len);
			while (!last &&
			    fgets((char *)buf, sizeof(buf), stdin) != NULL) {
				len = strlen((char *)buf);
				CHOMP(buf, len, last);				
				SHA1Update(&ctx, buf, len);
			}
			SHA1Final(out, &ctx);
			outlen = 20;
		}
		hexdump(out, outlen);
	}
}

int
main(int argc, char **argv)
{
	int ch;

	while ((ch = getopt(argc, argv, "mrst")) != -1)
		switch (ch) {
		case 'm':
			mflag = 1;
			break;
		case 'r':
			rflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		case 't':
			tflag = 1;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;
	if (argc > 0)
		usage();

	if (!(mflag || sflag))
		mflag = 1;

	if ((mflag ^ sflag) != 1)
		usage();

	if ((tflag ^ rflag) != 1)
		usage();

	if (tflag)
		timetest();

	if (rflag)
		regress();
	
	exit(0);
	
}
