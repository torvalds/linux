/*
 * arch/mips/sni/pcimt_scache.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1997, 1998 by Ralf Baechle
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/bcache.h>
#include <asm/sni.h>

#define cacheconf (*(volatile unsigned int *)PCIMT_CACHECONF)
#define invspace (*(volatile unsigned int *)PCIMT_INVSPACE)

void __init sni_pcimt_sc_init(void)
{
	unsigned int scsiz, sc_size;

	scsiz = cacheconf & 7;
	if (scsiz == 0) {
		printk("Second level cache is deactived.\n");
		return;
	}
	if (scsiz >= 6) {
		printk("Invalid second level cache size configured, "
		       "deactivating second level cache.\n");
		cacheconf = 0;
		return;
	}

	sc_size = 128 << scsiz;
	printk("%dkb second level cache detected, deactivating.\n", sc_size);
	cacheconf = 0;
}
