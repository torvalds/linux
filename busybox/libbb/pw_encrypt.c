/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include <crypt.h>
#include "libbb.h"

/* static const uint8_t ascii64[] ALIGN1 =
 * "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
 */

static int i64c(int i)
{
	i &= 0x3f;
	if (i == 0)
		return '.';
	if (i == 1)
		return '/';
	if (i < 12)
		return ('0' - 2 + i);
	if (i < 38)
		return ('A' - 12 + i);
	return ('a' - 38 + i);
}

int FAST_FUNC crypt_make_salt(char *p, int cnt /*, int x */)
{
	/* was: x += ... */
	unsigned x = getpid() + monotonic_us();
	do {
		/* x = (x*1664525 + 1013904223) % 2^32 generator is lame
		 * (low-order bit is not "random", etc...),
		 * but for our purposes it is good enough */
		x = x*1664525 + 1013904223;
		/* BTW, Park and Miller's "minimal standard generator" is
		 * x = x*16807 % ((2^31)-1)
		 * It has no problem with visibly alternating lowest bit
		 * but is also weak in cryptographic sense + needs div,
		 * which needs more code (and slower) on many CPUs */
		*p++ = i64c(x >> 16);
		*p++ = i64c(x >> 22);
	} while (--cnt);
	*p = '\0';
	return x;
}

char* FAST_FUNC crypt_make_pw_salt(char salt[MAX_PW_SALT_LEN], const char *algo)
{
	int len = 2/2;
	char *salt_ptr = salt;

	/* Standard chpasswd uses uppercase algos ("MD5", not "md5").
	 * Need to be case-insensitive in the code below.
	 */
	if ((algo[0]|0x20) != 'd') { /* not des */
		len = 8/2; /* so far assuming md5 */
		*salt_ptr++ = '$';
		*salt_ptr++ = '1';
		*salt_ptr++ = '$';
#if !ENABLE_USE_BB_CRYPT || ENABLE_USE_BB_CRYPT_SHA
		if ((algo[0]|0x20) == 's') { /* sha */
			salt[1] = '5' + (strcasecmp(algo, "sha512") == 0);
			len = 16/2;
		}
#endif
	}
	crypt_make_salt(salt_ptr, len);
	return salt_ptr;
}

#if ENABLE_USE_BB_CRYPT

static char*
to64(char *s, unsigned v, int n)
{
	while (--n >= 0) {
		/* *s++ = ascii64[v & 0x3f]; */
		*s++ = i64c(v);
		v >>= 6;
	}
	return s;
}

/*
 * DES and MD5 crypt implementations are taken from uclibc.
 * They were modified to not use static buffers.
 */

#include "pw_encrypt_des.c"
#include "pw_encrypt_md5.c"
#if ENABLE_USE_BB_CRYPT_SHA
#include "pw_encrypt_sha.c"
#endif

/* Other advanced crypt ids (TODO?): */
/* $2$ or $2a$: Blowfish */

static struct const_des_ctx *des_cctx;
static struct des_ctx *des_ctx;

/* my_crypt returns malloc'ed data */
static char *my_crypt(const char *key, const char *salt)
{
	/* MD5 or SHA? */
	if (salt[0] == '$' && salt[1] && salt[2] == '$') {
		if (salt[1] == '1')
			return md5_crypt(xzalloc(MD5_OUT_BUFSIZE), (unsigned char*)key, (unsigned char*)salt);
#if ENABLE_USE_BB_CRYPT_SHA
		if (salt[1] == '5' || salt[1] == '6')
			return sha_crypt((char*)key, (char*)salt);
#endif
	}

	if (!des_cctx)
		des_cctx = const_des_init();
	des_ctx = des_init(des_ctx, des_cctx);
	return des_crypt(des_ctx, xzalloc(DES_OUT_BUFSIZE), (unsigned char*)key, (unsigned char*)salt);
}

/* So far nobody wants to have it public */
static void my_crypt_cleanup(void)
{
	free(des_cctx);
	free(des_ctx);
	des_cctx = NULL;
	des_ctx = NULL;
}

char* FAST_FUNC pw_encrypt(const char *clear, const char *salt, int cleanup)
{
	char *encrypted;

	encrypted = my_crypt(clear, salt);

	if (cleanup)
		my_crypt_cleanup();

	return encrypted;
}

#else /* if !ENABLE_USE_BB_CRYPT */

char* FAST_FUNC pw_encrypt(const char *clear, const char *salt, int cleanup)
{
	char *s;

	s = crypt(clear, salt);
	/*
	 * glibc used to return "" on malformed salts (for example, ""),
	 * but since 2.17 it returns NULL.
	 */
	return xstrdup(s ? s : "");
}

#endif
