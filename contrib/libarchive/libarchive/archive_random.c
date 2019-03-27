/*-
 * Copyright (c) 2014 Michihiro NAKAJIMA
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

#include "archive_platform.h"
__FBSDID("$FreeBSD$");

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if !defined(HAVE_ARC4RANDOM_BUF) && (!defined(_WIN32) || defined(__CYGWIN__))

#ifdef HAVE_FCNTL
#include <fcntl.h>
#endif
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#endif

static void arc4random_buf(void *, size_t);

#endif /* HAVE_ARC4RANDOM_BUF */

#include "archive.h"
#include "archive_random_private.h"

#if defined(HAVE_WINCRYPT_H) && !defined(__CYGWIN__)
#include <wincrypt.h>
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC	0
#endif

/*
 * Random number generator function.
 * This simply calls arc4random_buf function if the platform provides it.
 */

int
archive_random(void *buf, size_t nbytes)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
	HCRYPTPROV hProv;
	BOOL success;

	success = CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL,
	    CRYPT_VERIFYCONTEXT);
	if (!success && GetLastError() == (DWORD)NTE_BAD_KEYSET) {
		success = CryptAcquireContext(&hProv, NULL, NULL,
		    PROV_RSA_FULL, CRYPT_NEWKEYSET);
	}
	if (success) {
		success = CryptGenRandom(hProv, (DWORD)nbytes, (BYTE*)buf);
		CryptReleaseContext(hProv, 0);
		if (success)
			return ARCHIVE_OK;
	}
	/* TODO: Does this case really happen? */
	return ARCHIVE_FAILED;
#else
	arc4random_buf(buf, nbytes);
	return ARCHIVE_OK;
#endif
}

#if !defined(HAVE_ARC4RANDOM_BUF) && (!defined(_WIN32) || defined(__CYGWIN__))

/*	$OpenBSD: arc4random.c,v 1.24 2013/06/11 16:59:50 deraadt Exp $	*/
/*
 * Copyright (c) 1996, David Mazieres <dm@uun.org>
 * Copyright (c) 2008, Damien Miller <djm@openbsd.org>
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

/*
 * Arc4 random number generator for OpenBSD.
 *
 * This code is derived from section 17.1 of Applied Cryptography,
 * second edition, which describes a stream cipher allegedly
 * compatible with RSA Labs "RC4" cipher (the actual description of
 * which is a trade secret).  The same algorithm is used as a stream
 * cipher called "arcfour" in Tatu Ylonen's ssh package.
 *
 * RC4 is a registered trademark of RSA Laboratories.
 */

#ifdef __GNUC__
#define inline __inline
#else				/* !__GNUC__ */
#define inline
#endif				/* !__GNUC__ */

struct arc4_stream {
	uint8_t i;
	uint8_t j;
	uint8_t s[256];
};

#define	RANDOMDEV	"/dev/urandom"
#define	KEYSIZE		128
#ifdef HAVE_PTHREAD_H
static pthread_mutex_t	arc4random_mtx = PTHREAD_MUTEX_INITIALIZER;
#define	_ARC4_LOCK()	pthread_mutex_lock(&arc4random_mtx);
#define	_ARC4_UNLOCK()  pthread_mutex_unlock(&arc4random_mtx);
#else
#define	_ARC4_LOCK()
#define	_ARC4_UNLOCK()
#endif

static int rs_initialized;
static struct arc4_stream rs;
static pid_t arc4_stir_pid;
static int arc4_count;

static inline uint8_t arc4_getbyte(void);
static void arc4_stir(void);

static inline void
arc4_init(void)
{
	int     n;

	for (n = 0; n < 256; n++)
		rs.s[n] = n;
	rs.i = 0;
	rs.j = 0;
}

static inline void
arc4_addrandom(u_char *dat, int datlen)
{
	int     n;
	uint8_t si;

	rs.i--;
	for (n = 0; n < 256; n++) {
		rs.i = (rs.i + 1);
		si = rs.s[rs.i];
		rs.j = (rs.j + si + dat[n % datlen]);
		rs.s[rs.i] = rs.s[rs.j];
		rs.s[rs.j] = si;
	}
	rs.j = rs.i;
}

static void
arc4_stir(void)
{
	int done, fd, i;
	struct {
		struct timeval	tv;
		pid_t		pid;
		u_char	 	rnd[KEYSIZE];
	} rdat;

	if (!rs_initialized) {
		arc4_init();
		rs_initialized = 1;
	}
	done = 0;
	fd = open(RANDOMDEV, O_RDONLY | O_CLOEXEC, 0);
	if (fd >= 0) {
		if (read(fd, &rdat, KEYSIZE) == KEYSIZE)
			done = 1;
		(void)close(fd);
	}
	if (!done) {
		(void)gettimeofday(&rdat.tv, NULL);
		rdat.pid = getpid();
		/* We'll just take whatever was on the stack too... */
	}

	arc4_addrandom((u_char *)&rdat, KEYSIZE);

	/*
	 * Discard early keystream, as per recommendations in:
	 * "(Not So) Random Shuffles of RC4" by Ilya Mironov.
	 * As per the Network Operations Division, cryptographic requirements
	 * published on wikileaks on March 2017.
	 */

	for (i = 0; i < 3072; i++)
		(void)arc4_getbyte();
	arc4_count = 1600000;
}

static void
arc4_stir_if_needed(void)
{
	pid_t pid = getpid();

	if (arc4_count <= 0 || !rs_initialized || arc4_stir_pid != pid) {
		arc4_stir_pid = pid;
		arc4_stir();
	}
}

static inline uint8_t
arc4_getbyte(void)
{
	uint8_t si, sj;

	rs.i = (rs.i + 1);
	si = rs.s[rs.i];
	rs.j = (rs.j + si);
	sj = rs.s[rs.j];
	rs.s[rs.i] = sj;
	rs.s[rs.j] = si;
	return (rs.s[(si + sj) & 0xff]);
}

static void
arc4random_buf(void *_buf, size_t n)
{
	u_char *buf = (u_char *)_buf;
	_ARC4_LOCK();
	arc4_stir_if_needed();
	while (n--) {
		if (--arc4_count <= 0)
			arc4_stir();
		buf[n] = arc4_getbyte();
	}
	_ARC4_UNLOCK();
}

#endif /* !HAVE_ARC4RANDOM_BUF */
