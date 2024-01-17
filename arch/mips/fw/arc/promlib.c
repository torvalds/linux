/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996 David S. Miller (dm@sgi.com)
 * Compatibility with board caches, Ulf Carlsson
 */
#include <linux/kernel.h>
#include <asm/sgialib.h>
#include <asm/bcache.h>
#include <asm/setup.h>

#if defined(CONFIG_64BIT) && defined(CONFIG_FW_ARC32)
/*
 * For 64bit kernels working with a 32bit ARC PROM pointer arguments
 * for ARC calls need to reside in CKEG0/1. But as soon as the kernel
 * switches to its first kernel thread stack is set to an address in
 * XKPHYS, so anything on stack can't be used anymore. This is solved
 * by using a * static declaration variables are put into BSS, which is
 * linked to a CKSEG0 address. Since this is only used on UP platforms
 * there is no spinlock needed
 */
#define O32_STATIC	static
#else
#define O32_STATIC
#endif

/*
 * IP22 boardcache is not compatible with board caches.	 Thus we disable it
 * during romvec action.  Since r4xx0.c is always compiled and linked with your
 * kernel, this shouldn't cause any harm regardless what MIPS processor you
 * have.
 *
 * The ARC write and read functions seem to interfere with the serial lines
 * in some way. You should be careful with them.
 */

void prom_putchar(char c)
{
	O32_STATIC ULONG cnt;
	O32_STATIC CHAR it;

	it = c;

	bc_disable();
	ArcWrite(1, &it, 1, &cnt);
	bc_enable();
}

char prom_getchar(void)
{
	O32_STATIC ULONG cnt;
	O32_STATIC CHAR c;

	bc_disable();
	ArcRead(0, &c, 1, &cnt);
	bc_enable();

	return c;
}
