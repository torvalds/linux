#ifndef __BACKPORT_RANDOM_H
#define __BACKPORT_RANDOM_H
#include_next <linux/random.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)
/* backports 496f2f9 */
#define prandom_seed(_seed)		srandom32(_seed)
#define prandom_u32()			random32()
#define prandom_u32_state(_state)	prandom32(_state)
/* backport 6582c665d6b882dad8329e05749fbcf119f1ab88 */
#define prandom_bytes LINUX_BACKPORT(prandom_bytes)
void prandom_bytes(void *buf, int bytes);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
/**
 * prandom_u32_max - returns a pseudo-random number in interval [0, ep_ro)
 * @ep_ro: right open interval endpoint
 *
 * Returns a pseudo-random number that is in interval [0, ep_ro). Note
 * that the result depends on PRNG being well distributed in [0, ~0U]
 * u32 space. Here we use maximally equidistributed combined Tausworthe
 * generator, that is, prandom_u32(). This is useful when requesting a
 * random index of an array containing ep_ro elements, for example.
 *
 * Returns: pseudo-random number in interval [0, ep_ro)
 */
#define prandom_u32_max LINUX_BACKPORT(prandom_u32_max)
static inline u32 prandom_u32_max(u32 ep_ro)
{
	return (u32)(((u64) prandom_u32() * ep_ro) >> 32);
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0) */

#endif /* __BACKPORT_RANDOM_H */
