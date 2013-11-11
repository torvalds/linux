/*
 *	Access to VGA videoram
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 */

#ifndef _LINUX_ASM_VGA_H_
#define _LINUX_ASM_VGA_H_

#include <linux/bug.h>
#include <asm/types.h>

#define VT_BUF_HAVE_RW

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

#define VGA_MAP_MEM(x,s) (x)

#endif
