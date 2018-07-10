/*
 * MD5C.C - RSA Data Security, Inc., MD5 message-digest algorithm
 *
 * Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
 * rights reserved.
 *
 * License to copy and use this software is granted provided that it
 * is identified as the "RSA Data Security, Inc. MD5 Message-Digest
 * Algorithm" in all material mentioning or referencing this software
 * or this function.
 *
 * License is also granted to make and use derivative works provided
 * that such works are identified as "derived from the RSA Data
 * Security, Inc. MD5 Message-Digest Algorithm" in all material
 * mentioning or referencing the derived work.
 *
 * RSA Data Security, Inc. makes no representations concerning either
 * the merchantability of this software or the suitability of this
 * software for any particular purpose. It is provided "as is"
 * without express or implied warranty of any kind.
 *
 * These notices must be retained in any copies of any part of this
 * documentation and/or software.
 *
 * $FreeBSD: src/lib/libmd/md5c.c,v 1.9.2.1 1999/08/29 14:57:12 peter Exp $
 *
 * This code is the same as the code published by RSA Inc.  It has been
 * edited for clarity and style only.
 *
 * ----------------------------------------------------------------------------
 * The md5_crypt() function was taken from freeBSD's libcrypt and contains
 * this license:
 *    "THE BEER-WARE LICENSE" (Revision 42):
 *     <phk@login.dknet.dk> wrote this file.  As long as you retain this notice you
 *     can do whatever you want with this stuff. If we meet some day, and you think
 *     this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 *
 * $FreeBSD: src/lib/libcrypt/crypt.c,v 1.7.2.1 1999/08/29 14:56:33 peter Exp $
 *
 * ----------------------------------------------------------------------------
 * On April 19th, 2001 md5_crypt() was modified to make it reentrant
 * by Erik Andersen <andersen@uclibc.org>
 *
 *
 * June 28, 2001             Manuel Novoa III
 *
 * "Un-inlined" code using loops and static const tables in order to
 * reduce generated code size (on i386 from approx 4k to approx 2.5k).
 *
 * June 29, 2001             Manuel Novoa III
 *
 * Completely removed static PADDING array.
 *
 * Reintroduced the loop unrolling in MD5_Transform and added the
 * MD5_SIZE_OVER_SPEED option for configurability.  Define below as:
 *       0    fully unrolled loops
 *       1    partially unrolled (4 ops per loop)
 *       2    no unrolling -- introduces the need to swap 4 variables (slow)
 *       3    no unrolling and all 4 loops merged into one with switch
 *               in each loop (glacial)
 * On i386, sizes are roughly (-Os -fno-builtin):
 *     0: 3k     1: 2.5k     2: 2.2k     3: 2k
 *
 * Since SuSv3 does not require crypt_r, modified again August 7, 2002
 * by Erik Andersen to remove reentrance stuff...
 */

/*
 * UNIX password
 *
 * Use MD5 for what it is best at...
 */
#define MD5_OUT_BUFSIZE 36
static char *
NOINLINE
md5_crypt(char result[MD5_OUT_BUFSIZE], const unsigned char *pw, const unsigned char *salt)
{
	char *p;
	unsigned char final[17]; /* final[16] exists only to aid in looping */
	int sl, pl, i, pw_len;
	md5_ctx_t ctx, ctx1;

	/* NB: in busybox, "$1$" in salt is always present */

	/* Refine the Salt first */

	/* Get the length of the salt including "$1$" */
	sl = 3;
	while (sl < (3 + 8) && salt[sl] && salt[sl] != '$')
		sl++;

	/* Hash. the password first, since that is what is most unknown */
	md5_begin(&ctx);
	pw_len = strlen((char*)pw);
	md5_hash(&ctx, pw, pw_len);

	/* Then the salt including "$1$" */
	md5_hash(&ctx, salt, sl);

	/* Copy salt to result; skip "$1$" */
	memcpy(result, salt, sl);
	result[sl] = '$';
	salt += 3;
	sl -= 3;

	/* Then just as many characters of the MD5(pw, salt, pw) */
	md5_begin(&ctx1);
	md5_hash(&ctx1, pw, pw_len);
	md5_hash(&ctx1, salt, sl);
	md5_hash(&ctx1, pw, pw_len);
	md5_end(&ctx1, final);
	for (pl = pw_len; pl > 0; pl -= 16)
		md5_hash(&ctx, final, pl > 16 ? 16 : pl);

	/* Then something really weird... */
	memset(final, 0, sizeof(final));
	for (i = pw_len; i; i >>= 1) {
		md5_hash(&ctx, ((i & 1) ? final : (const unsigned char *) pw), 1);
	}
	md5_end(&ctx, final);

	/* And now, just to make sure things don't run too fast.
	 * On a 60 Mhz Pentium this takes 34 msec, so you would
	 * need 30 seconds to build a 1000 entry dictionary...
	 */
	for (i = 0; i < 1000; i++) {
		md5_begin(&ctx1);
		if (i & 1)
			md5_hash(&ctx1, pw, pw_len);
		else
			md5_hash(&ctx1, final, 16);

		if (i % 3)
			md5_hash(&ctx1, salt, sl);

		if (i % 7)
			md5_hash(&ctx1, pw, pw_len);

		if (i & 1)
			md5_hash(&ctx1, final, 16);
		else
			md5_hash(&ctx1, pw, pw_len);
		md5_end(&ctx1, final);
	}

	p = result + sl + 4; /* 12 bytes max (sl is up to 8 bytes) */

	/* Add 5*4+2 = 22 bytes of hash, + NUL byte. */
	final[16] = final[5];
	for (i = 0; i < 5; i++) {
		unsigned l = (final[i] << 16) | (final[i+6] << 8) | final[i+12];
		p = to64(p, l, 4);
	}
	p = to64(p, final[11], 2);
	*p = '\0';

	/* Don't leave anything around in vm they could use. */
	memset(final, 0, sizeof(final));

	return result;
}
