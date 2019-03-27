/*-
 * THE BEER-WARE LICENSE
 *
 * <dan@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff.  If we meet some day, and you
 * think this stuff is worth it, you can buy me a beer in return.
 *
 * Dan Moschuk
 */
#if !defined(SOLARIS2)
# include <sys/cdefs.h>
#endif

#include <sys/types.h>
#include <sys/param.h>
#ifdef __FreeBSD__
# include <sys/kernel.h>
#endif
# include <sys/random.h>
#ifdef __FreeBSD__
# include <sys/libkern.h>
#endif
#include <sys/lock.h>
# include <sys/mutex.h>
#include <sys/time.h>

#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include "netinet/ip_compat.h"
#ifdef HAS_SYS_MD5_H
# include <sys/md5.h>
#else
# include "md5.h"
#endif

#ifdef NEED_LOCAL_RAND
#if !defined(__GNUC__)
# define __inline
#endif

#define	ARC4_RESEED_BYTES 65536
#define	ARC4_RESEED_SECONDS 300
#define	ARC4_KEYBYTES (256 / 8)

static u_int8_t arc4_i, arc4_j;
static int arc4_numruns = 0;
static u_int8_t arc4_sbox[256];
static time_t arc4_t_reseed;
static ipfmutex_t arc4_mtx;
static MD5_CTX md5ctx;

static u_int8_t arc4_randbyte(void);
static int ipf_read_random(void *dest, int length);

static __inline void
arc4_swap(u_int8_t *a, u_int8_t *b)
{
	u_int8_t c;

	c = *a;
	*a = *b;
	*b = c;
}

/*
 * Stir our S-box.
 */
static void
arc4_randomstir (void)
{
	u_int8_t key[256];
	int r, n;
	struct timeval tv_now;

	/*
	 * XXX read_random() returns unsafe numbers if the entropy
	 * device is not loaded -- MarkM.
	 */
	r = ipf_read_random(key, ARC4_KEYBYTES);
	GETKTIME(&tv_now);
	MUTEX_ENTER(&arc4_mtx);
	/* If r == 0 || -1, just use what was on the stack. */
	if (r > 0) {
		for (n = r; n < sizeof(key); n++)
			key[n] = key[n % r];
	}

	for (n = 0; n < 256; n++) {
		arc4_j = (arc4_j + arc4_sbox[n] + key[n]) % 256;
		arc4_swap(&arc4_sbox[n], &arc4_sbox[arc4_j]);
	}

	/* Reset for next reseed cycle. */
	arc4_t_reseed = tv_now.tv_sec + ARC4_RESEED_SECONDS;
	arc4_numruns = 0;

	/*
	 * Throw away the first N words of output, as suggested in the
	 * paper "Weaknesses in the Key Scheduling Algorithm of RC4"
	 * by Fluher, Mantin, and Shamir.  (N = 768 in our case.)
	 */
	for (n = 0; n < 768*4; n++)
		arc4_randbyte();
	MUTEX_EXIT(&arc4_mtx);
}

/*
 * Initialize our S-box to its beginning defaults.
 */
static void
arc4_init(void)
{
	int n;

	MD5Init(&md5ctx);

	MUTEX_INIT(&arc4_mtx, "arc4_mtx");
	arc4_i = arc4_j = 0;
	for (n = 0; n < 256; n++)
		arc4_sbox[n] = (u_int8_t) n;

	arc4_t_reseed = 0;
}


/*
 * Generate a random byte.
 */
static u_int8_t
arc4_randbyte(void)
{
	u_int8_t arc4_t;

	arc4_i = (arc4_i + 1) % 256;
	arc4_j = (arc4_j + arc4_sbox[arc4_i]) % 256;

	arc4_swap(&arc4_sbox[arc4_i], &arc4_sbox[arc4_j]);

	arc4_t = (arc4_sbox[arc4_i] + arc4_sbox[arc4_j]) % 256;
	return arc4_sbox[arc4_t];
}

/*
 * MPSAFE
 */
void
arc4rand(void *ptr, u_int len, int reseed)
{
	u_int8_t *p;
	struct timeval tv;

	GETKTIME(&tv);
	if (reseed ||
	   (arc4_numruns > ARC4_RESEED_BYTES) ||
	   (tv.tv_sec > arc4_t_reseed))
		arc4_randomstir();

	MUTEX_ENTER(&arc4_mtx);
	arc4_numruns += len;
	p = ptr;
	while (len--)
		*p++ = arc4_randbyte();
	MUTEX_EXIT(&arc4_mtx);
}

uint32_t
ipf_random(void)
{
	uint32_t ret;

	arc4rand(&ret, sizeof ret, 0);
	return ret;
}


static u_char pot[ARC4_RESEED_BYTES];
static u_char *pothead = pot, *pottail = pot;
static int inpot = 0;

/*
 * This is not very strong, and this is understood, but the aim isn't to
 * be cryptographically strong - it is just to make up something that is
 * pseudo random.
 */
void
ipf_rand_push(void *src, int length)
{
	static int arc4_inited = 0;
	u_char *nsrc;
	int mylen;

	if (arc4_inited == 0) {
		arc4_init();
		arc4_inited = 1;
	}

	if (length < 64) {
		MD5Update(&md5ctx, src, length);
		return;
	}

	nsrc = src;
	mylen = length;

#if defined(_SYS_MD5_H) && defined(SOLARIS2)
# define	buf	buf_un.buf8
#endif
	MUTEX_ENTER(&arc4_mtx);
	while ((mylen > 64)  && (sizeof(pot) - inpot > sizeof(md5ctx.buf))) {
		MD5Update(&md5ctx, nsrc, 64);
		mylen -= 64;
		nsrc += 64;
		if (pottail + sizeof(md5ctx.buf) > pot + sizeof(pot)) {
			int left, numbytes;

			numbytes = pot + sizeof(pot) - pottail;
			bcopy(md5ctx.buf, pottail, numbytes);
			left = sizeof(md5ctx.buf) - numbytes;
			pottail = pot;
			bcopy(md5ctx.buf + sizeof(md5ctx.buf) - left,
			      pottail, left);
			pottail += left;
		} else {
			bcopy(md5ctx.buf, pottail, sizeof(md5ctx.buf));
			pottail += sizeof(md5ctx.buf);
		}
		inpot += 64;
	}
	MUTEX_EXIT(&arc4_mtx);
#if defined(_SYS_MD5_H) && defined(SOLARIS2)
# undef buf
#endif
}


static int
ipf_read_random(void *dest, int length)
{
	if (length > inpot)
		return 0;

	MUTEX_ENTER(&arc4_mtx);
	if (pothead + length > pot + sizeof(pot)) {
		int left, numbytes;

		left = length;
		numbytes = pot + sizeof(pot) - pothead;
		bcopy(pothead, dest, numbytes);
		left -= numbytes;
		pothead = pot;
		bcopy(pothead, dest + length - left, left);
		pothead += left;
	} else {
		bcopy(pothead, dest, length);
		pothead += length;
	}
	inpot -= length;
	if (inpot == 0)
		pothead = pottail = pot;
	MUTEX_EXIT(&arc4_mtx);

	return length;
}

#endif /* NEED_LOCAL_RAND */
