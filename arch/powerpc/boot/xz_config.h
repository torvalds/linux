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
#else
#define get_le32(p) swab32p(p)
#endif

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
