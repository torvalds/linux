/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_UM_ARCHRANDOM_H__
#define __ASM_UM_ARCHRANDOM_H__

#include <linux/types.h>

/* This is from <os.h>, but better not to #include that in a global header here. */
ssize_t os_getrandom(void *buf, size_t len, unsigned int flags);

static inline bool __must_check arch_get_random_long(unsigned long *v)
{
	return os_getrandom(v, sizeof(*v), 0) == sizeof(*v);
}

static inline bool __must_check arch_get_random_int(unsigned int *v)
{
	return os_getrandom(v, sizeof(*v), 0) == sizeof(*v);
}

static inline bool __must_check arch_get_random_seed_long(unsigned long *v)
{
	return false;
}

static inline bool __must_check arch_get_random_seed_int(unsigned int *v)
{
	return false;
}

#endif
