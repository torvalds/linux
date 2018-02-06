/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * include/asm-xtensa/poll.h
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_POLL_H
#define _XTENSA_POLL_H

#ifndef __KERNEL__
#define POLLWRNORM	POLLOUT
#define POLLWRBAND	(__force __poll_t)0x0100
#define POLLREMOVE	(__force __poll_t)0x0800
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

#endif /* _XTENSA_POLL_H */
