/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	Access to VGA videoram
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 */

#ifndef _LINUX_ASM_VGA_H_
#define _LINUX_ASM_VGA_H_

#include <linux/bug.h>
#include <linux/string.h>
#include <asm/types.h>

#define VT_BUF_HAVE_RW
#define VT_BUF_HAVE_MEMSETW
#define VT_BUF_HAVE_MEMCPYW
#define VT_BUF_HAVE_MEMMOVEW

#undef scr_writew
#undef scr_readw

static inline void scr_writew(u16 val, u16 *addr)
{
	BUG_ON((long) addr >= 0);

	*addr = val;
}

static inline u16 scr_readw(const u16 *addr)
{
	BUG_ON((long) addr >= 0);

	return *addr;
}

static inline void scr_memsetw(u16 *p, u16 v, unsigned int n)
{
	BUG_ON((long) p >= 0);

	memset16(p, cpu_to_le16(v), n / 2);
}

static inline void scr_memcpyw(u16 *d, u16 *s, unsigned int n)
{
	BUG_ON((long) d >= 0);

	memcpy(d, s, n);
}

static inline void scr_memmovew(u16 *d, u16 *s, unsigned int n)
{
	BUG_ON((long) d >= 0);

	memmove(d, s, n);
}

#define VGA_MAP_MEM(x,s) (x)

#endif
