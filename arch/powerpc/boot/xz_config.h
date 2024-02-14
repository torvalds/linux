/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __XZ_CONFIG_H__
#define __XZ_CONFIG_H__

/*
 * most of this is copied from lib/xz/xz_private.h, we can't use their defines
 * since the boot wrapper is not built in the same environment as the rest of
 * the kernel.
 */

#include "types.h"
#include "swab.h"

static inline uint32_t swab32p(void *p)
{
	uint32_t *q = p;

	return swab32(*q);
}

#ifdef __LITTLE_ENDIAN__
#define get_le32(p) (*((uint32_t *) (p)))
#define cpu_to_be32(x) swab32(x)
static inline u32 be32_to_cpup(const u32 *p)
{
	return swab32p((u32 *)p);
}
#else
#define get_le32(p) swab32p(p)
#define cpu_to_be32(x) (x)
static inline u32 be32_to_cpup(const u32 *p)
{
	return *p;
}
#endif

static inline uint32_t get_unaligned_be32(const void *p)
{
	return be32_to_cpup(p);
}

static inline void put_unaligned_be32(u32 val, void *p)
{
	*((u32 *)p) = cpu_to_be32(val);
}

#define memeq(a, b, size) (memcmp(a, b, size) == 0)
#define memzero(buf, size) memset(buf, 0, size)

/* prevent the inclusion of the xz-preboot MM headers */
#define DECOMPR_MM_H
#define memmove memmove
#define XZ_EXTERN static

/* xz.h needs to be included directly since we need enum xz_mode */
#include "../../../include/linux/xz.h"

#undef XZ_EXTERN

#endif
