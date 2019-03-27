/* Portable arc4random.c based on arc4random.c from OpenBSD.
 * Portable version by Chris Davis, adapted for Libevent by Nick Mathewson
 * Copyright (c) 2010 Chris Davis, Niels Provos, and Nick Mathewson
 * Copyright (c) 2010-2012 Niels Provos and Nick Mathewson
 *
 * Note that in Libevent, this file isn't compiled directly.  Instead,
 * it's included from evutil_rand.c
 */

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
 * Here the stream cipher has been modified always to include the time
 * when initializing the state.  That makes it impossible to
 * regenerate the same random sequence twice, so this can't be used
 * for encryption, but will generate good random numbers.
 *
 * RC4 is a registered trademark of RSA Laboratories.
 */

#ifndef ARC4RANDOM_EXPORT
#define ARC4RANDOM_EXPORT
#endif

#ifndef ARC4RANDOM_UINT32
#define ARC4RANDOM_UINT32 uint32_t
#endif

#ifndef ARC4RANDOM_NO_INCLUDES
#include "evconfig-private.h"
#ifdef _WIN32
#include <wincrypt.h>
#include <process.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/time.h>
#ifdef EVENT__HAVE_SYS_SYSCTL_H
#include <sys/sysctl.h>
#endif
#endif
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#endif

/* Add platform entropy 32 bytes (256 bits) at a time. */
#define ADD_ENTROPY 32

/* Re-seed from the platform RNG after generating this many bytes. */
#define BYTES_BEFORE_RESEED 1600000

struct arc4_stream {
	unsigned char i;
	unsigned char j;
	unsigned char s[256];
};

#ifdef _WIN32
#define getpid _getpid
#define pid_t int
#endif

static int rs_initialized;
static struct arc4_stream rs;
static pid_t arc4_stir_pid;
static int arc4_count;
static int arc4_seeded_ok;

static inline unsigned char arc4_getbyte(void);

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
arc4_addrandom(const unsigned char *dat, int datlen)
{
	int     n;
	unsigned char si;

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

#ifndef _WIN32
static ssize_t
read_all(int fd, unsigned char *buf, size_t count)
{
	size_t numread = 0;
	ssize_t result;

	while (numread < count) {
		result = read(fd, buf+numread, count-numread);
		if (result<0)
			return -1;
		else if (result == 0)
			break;
		numread += result;
	}

	return (ssize_t)numread;
}
#endif

#ifdef _WIN32
#define TRY_SEED_WIN32
static int
arc4_seed_win32(void)
{
	/* This is adapted from Tor's crypto_seed_rng() */
	static int provider_set = 0;
	static HCRYPTPROV provider;
	unsigned char buf[ADD_ENTROPY];

	if (!provider_set) {
		if (!CryptAcquireContext(&provider, NULL, NULL, PROV_RSA_FULL,
		    CRYPT_VERIFYCONTEXT)) {
			if (GetLastError() != (DWORD)NTE_BAD_KEYSET)
				return -1;
		}
		provider_set = 1;
	}
	if (!CryptGenRandom(provider, sizeof(buf), buf))
		return -1;
	arc4_addrandom(buf, sizeof(buf));
	evutil_memclear_(buf, sizeof(buf));
	arc4_seeded_ok = 1;
	return 0;
}
#endif

#if defined(EVENT__HAVE_SYS_SYSCTL_H) && defined(EVENT__HAVE_SYSCTL)
#if EVENT__HAVE_DECL_CTL_KERN && EVENT__HAVE_DECL_KERN_RANDOM && EVENT__HAVE_DECL_RANDOM_UUID
#define TRY_SEED_SYSCTL_LINUX
static int
arc4_seed_sysctl_linux(void)
{
	/* Based on code by William Ahern, this function tries to use the
	 * RANDOM_UUID sysctl to get entropy from the kernel.  This can work
	 * even if /dev/urandom is inaccessible for some reason (e.g., we're
	 * running in a chroot). */
	int mib[] = { CTL_KERN, KERN_RANDOM, RANDOM_UUID };
	unsigned char buf[ADD_ENTROPY];
	size_t len, n;
	unsigned i;
	int any_set;

	memset(buf, 0, sizeof(buf));

	for (len = 0; len < sizeof(buf); len += n) {
		n = sizeof(buf) - len;

		if (0 != sysctl(mib, 3, &buf[len], &n, NULL, 0))
			return -1;
	}
	/* make sure that the buffer actually got set. */
	for (i=0,any_set=0; i<sizeof(buf); ++i) {
		any_set |= buf[i];
	}
	if (!any_set)
		return -1;

	arc4_addrandom(buf, sizeof(buf));
	evutil_memclear_(buf, sizeof(buf));
	arc4_seeded_ok = 1;
	return 0;
}
#endif

#if EVENT__HAVE_DECL_CTL_KERN && EVENT__HAVE_DECL_KERN_ARND
#define TRY_SEED_SYSCTL_BSD
static int
arc4_seed_sysctl_bsd(void)
{
	/* Based on code from William Ahern and from OpenBSD, this function
	 * tries to use the KERN_ARND syscall to get entropy from the kernel.
	 * This can work even if /dev/urandom is inaccessible for some reason
	 * (e.g., we're running in a chroot). */
	int mib[] = { CTL_KERN, KERN_ARND };
	unsigned char buf[ADD_ENTROPY];
	size_t len, n;
	int i, any_set;

	memset(buf, 0, sizeof(buf));

	len = sizeof(buf);
	if (sysctl(mib, 2, buf, &len, NULL, 0) == -1) {
		for (len = 0; len < sizeof(buf); len += sizeof(unsigned)) {
			n = sizeof(unsigned);
			if (n + len > sizeof(buf))
			    n = len - sizeof(buf);
			if (sysctl(mib, 2, &buf[len], &n, NULL, 0) == -1)
				return -1;
		}
	}
	/* make sure that the buffer actually got set. */
	for (i=any_set=0; i<sizeof(buf); ++i) {
		any_set |= buf[i];
	}
	if (!any_set)
		return -1;

	arc4_addrandom(buf, sizeof(buf));
	evutil_memclear_(buf, sizeof(buf));
	arc4_seeded_ok = 1;
	return 0;
}
#endif
#endif /* defined(EVENT__HAVE_SYS_SYSCTL_H) */

#ifdef __linux__
#define TRY_SEED_PROC_SYS_KERNEL_RANDOM_UUID
static int
arc4_seed_proc_sys_kernel_random_uuid(void)
{
	/* Occasionally, somebody will make /proc/sys accessible in a chroot,
	 * but not /dev/urandom.  Let's try /proc/sys/kernel/random/uuid.
	 * Its format is stupid, so we need to decode it from hex.
	 */
	int fd;
	char buf[128];
	unsigned char entropy[64];
	int bytes, n, i, nybbles;
	for (bytes = 0; bytes<ADD_ENTROPY; ) {
		fd = evutil_open_closeonexec_("/proc/sys/kernel/random/uuid", O_RDONLY, 0);
		if (fd < 0)
			return -1;
		n = read(fd, buf, sizeof(buf));
		close(fd);
		if (n<=0)
			return -1;
		memset(entropy, 0, sizeof(entropy));
		for (i=nybbles=0; i<n; ++i) {
			if (EVUTIL_ISXDIGIT_(buf[i])) {
				int nyb = evutil_hex_char_to_int_(buf[i]);
				if (nybbles & 1) {
					entropy[nybbles/2] |= nyb;
				} else {
					entropy[nybbles/2] |= nyb<<4;
				}
				++nybbles;
			}
		}
		if (nybbles < 2)
			return -1;
		arc4_addrandom(entropy, nybbles/2);
		bytes += nybbles/2;
	}
	evutil_memclear_(entropy, sizeof(entropy));
	evutil_memclear_(buf, sizeof(buf));
	arc4_seeded_ok = 1;
	return 0;
}
#endif

#ifndef _WIN32
#define TRY_SEED_URANDOM
static char *arc4random_urandom_filename = NULL;

static int arc4_seed_urandom_helper_(const char *fname)
{
	unsigned char buf[ADD_ENTROPY];
	int fd;
	size_t n;

	fd = evutil_open_closeonexec_(fname, O_RDONLY, 0);
	if (fd<0)
		return -1;
	n = read_all(fd, buf, sizeof(buf));
	close(fd);
	if (n != sizeof(buf))
		return -1;
	arc4_addrandom(buf, sizeof(buf));
	evutil_memclear_(buf, sizeof(buf));
	arc4_seeded_ok = 1;
	return 0;
}

static int
arc4_seed_urandom(void)
{
	/* This is adapted from Tor's crypto_seed_rng() */
	static const char *filenames[] = {
		"/dev/srandom", "/dev/urandom", "/dev/random", NULL
	};
	int i;
	if (arc4random_urandom_filename)
		return arc4_seed_urandom_helper_(arc4random_urandom_filename);

	for (i = 0; filenames[i]; ++i) {
		if (arc4_seed_urandom_helper_(filenames[i]) == 0) {
			return 0;
		}
	}

	return -1;
}
#endif

static int
arc4_seed(void)
{
	int ok = 0;
	/* We try every method that might work, and don't give up even if one
	 * does seem to work.  There's no real harm in over-seeding, and if
	 * one of these sources turns out to be broken, that would be bad. */
#ifdef TRY_SEED_WIN32
	if (0 == arc4_seed_win32())
		ok = 1;
#endif
#ifdef TRY_SEED_URANDOM
	if (0 == arc4_seed_urandom())
		ok = 1;
#endif
#ifdef TRY_SEED_PROC_SYS_KERNEL_RANDOM_UUID
	if (arc4random_urandom_filename == NULL &&
	    0 == arc4_seed_proc_sys_kernel_random_uuid())
		ok = 1;
#endif
#ifdef TRY_SEED_SYSCTL_LINUX
	/* Apparently Linux is deprecating sysctl, and spewing warning
	 * messages when you try to use it. */
	if (!ok && 0 == arc4_seed_sysctl_linux())
		ok = 1;
#endif
#ifdef TRY_SEED_SYSCTL_BSD
	if (0 == arc4_seed_sysctl_bsd())
		ok = 1;
#endif
	return ok ? 0 : -1;
}

static int
arc4_stir(void)
{
	int     i;

	if (!rs_initialized) {
		arc4_init();
		rs_initialized = 1;
	}

	arc4_seed();
	if (!arc4_seeded_ok)
		return -1;

	/*
	 * Discard early keystream, as per recommendations in
	 * "Weaknesses in the Key Scheduling Algorithm of RC4" by
	 * Scott Fluhrer, Itsik Mantin, and Adi Shamir.
	 * http://www.wisdom.weizmann.ac.il/~itsik/RC4/Papers/Rc4_ksa.ps
	 *
	 * Ilya Mironov's "(Not So) Random Shuffles of RC4" suggests that
	 * we drop at least 2*256 bytes, with 12*256 as a conservative
	 * value.
	 *
	 * RFC4345 says to drop 6*256.
	 *
	 * At least some versions of this code drop 4*256, in a mistaken
	 * belief that "words" in the Fluhrer/Mantin/Shamir paper refers
	 * to processor words.
	 *
	 * We add another sect to the cargo cult, and choose 12*256.
	 */
	for (i = 0; i < 12*256; i++)
		(void)arc4_getbyte();

	arc4_count = BYTES_BEFORE_RESEED;

	return 0;
}


static void
arc4_stir_if_needed(void)
{
	pid_t pid = getpid();

	if (arc4_count <= 0 || !rs_initialized || arc4_stir_pid != pid)
	{
		arc4_stir_pid = pid;
		arc4_stir();
	}
}

static inline unsigned char
arc4_getbyte(void)
{
	unsigned char si, sj;

	rs.i = (rs.i + 1);
	si = rs.s[rs.i];
	rs.j = (rs.j + si);
	sj = rs.s[rs.j];
	rs.s[rs.i] = sj;
	rs.s[rs.j] = si;
	return (rs.s[(si + sj) & 0xff]);
}

static inline unsigned int
arc4_getword(void)
{
	unsigned int val;

	val = arc4_getbyte() << 24;
	val |= arc4_getbyte() << 16;
	val |= arc4_getbyte() << 8;
	val |= arc4_getbyte();

	return val;
}

#ifndef ARC4RANDOM_NOSTIR
ARC4RANDOM_EXPORT int
arc4random_stir(void)
{
	int val;
	ARC4_LOCK_();
	val = arc4_stir();
	ARC4_UNLOCK_();
	return val;
}
#endif

#ifndef ARC4RANDOM_NOADDRANDOM
ARC4RANDOM_EXPORT void
arc4random_addrandom(const unsigned char *dat, int datlen)
{
	int j;
	ARC4_LOCK_();
	if (!rs_initialized)
		arc4_stir();
	for (j = 0; j < datlen; j += 256) {
		/* arc4_addrandom() ignores all but the first 256 bytes of
		 * its input.  We want to make sure to look at ALL the
		 * data in 'dat', just in case the user is doing something
		 * crazy like passing us all the files in /var/log. */
		arc4_addrandom(dat + j, datlen - j);
	}
	ARC4_UNLOCK_();
}
#endif

#ifndef ARC4RANDOM_NORANDOM
ARC4RANDOM_EXPORT ARC4RANDOM_UINT32
arc4random(void)
{
	ARC4RANDOM_UINT32 val;
	ARC4_LOCK_();
	arc4_count -= 4;
	arc4_stir_if_needed();
	val = arc4_getword();
	ARC4_UNLOCK_();
	return val;
}
#endif

ARC4RANDOM_EXPORT void
arc4random_buf(void *buf_, size_t n)
{
	unsigned char *buf = buf_;
	ARC4_LOCK_();
	arc4_stir_if_needed();
	while (n--) {
		if (--arc4_count <= 0)
			arc4_stir();
		buf[n] = arc4_getbyte();
	}
	ARC4_UNLOCK_();
}

#ifndef ARC4RANDOM_NOUNIFORM
/*
 * Calculate a uniformly distributed random number less than upper_bound
 * avoiding "modulo bias".
 *
 * Uniformity is achieved by generating new random numbers until the one
 * returned is outside the range [0, 2**32 % upper_bound).  This
 * guarantees the selected random number will be inside
 * [2**32 % upper_bound, 2**32) which maps back to [0, upper_bound)
 * after reduction modulo upper_bound.
 */
ARC4RANDOM_EXPORT unsigned int
arc4random_uniform(unsigned int upper_bound)
{
	ARC4RANDOM_UINT32 r, min;

	if (upper_bound < 2)
		return 0;

#if (UINT_MAX > 0xffffffffUL)
	min = 0x100000000UL % upper_bound;
#else
	/* Calculate (2**32 % upper_bound) avoiding 64-bit math */
	if (upper_bound > 0x80000000)
		min = 1 + ~upper_bound;		/* 2**32 - upper_bound */
	else {
		/* (2**32 - (x * 2)) % x == 2**32 % x when x <= 2**31 */
		min = ((0xffffffff - (upper_bound * 2)) + 1) % upper_bound;
	}
#endif

	/*
	 * This could theoretically loop forever but each retry has
	 * p > 0.5 (worst case, usually far better) of selecting a
	 * number inside the range we need, so it should rarely need
	 * to re-roll.
	 */
	for (;;) {
		r = arc4random();
		if (r >= min)
			break;
	}

	return r % upper_bound;
}
#endif
