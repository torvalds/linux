/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 *
 */

#ifndef _UAPI__BFIN_POLL_H
#define _UAPI__BFIN_POLL_H

#ifndef __KERNEL__
#define POLLWRNORM	POLLOUT
#define POLLWRBAND	(__force __poll_t)256
#else
#define __ARCH_HAS_MANGLED_POLL
static inline __u16 mangle_poll(__poll_t val)
{
	__u16 v = (__force __u16)val;
	/* bit 9 -> bit 8, bit 8 -> bit 2 */
	return (v & ~0x300) | ((v & 0x200) >> 1) | ((v & 0x100) >> 6);
}

static inline __poll_t demangle_poll(__u16 v)
{
        /* bit 8 -> bit 9, bit 2 -> bits 2 and 8 */
	return (__force __poll_t)((v & ~0x100) | ((v & 0x100) << 1) |
				((v & 4) << 6));
}
#endif

#include <asm-generic/poll.h>

#endif /* _UAPI__BFIN_POLL_H */
