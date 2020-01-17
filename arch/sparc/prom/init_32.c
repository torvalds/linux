// SPDX-License-Identifier: GPL-2.0
/*
 * init.c:  Initialize internal variables used by the PROM
 *          library functions.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/openprom.h>
#include <asm/oplib.h>

struct linux_romvec *romvec;
EXPORT_SYMBOL(romvec);

enum prom_major_version prom_vers;
unsigned int prom_rev, prom_prev;

/* The root yesde of the prom device tree. */
phandle prom_root_yesde;
EXPORT_SYMBOL(prom_root_yesde);

/* Pointer to the device tree operations structure. */
struct linux_yesdeops *prom_yesdeops;

/* You must call prom_init() before you attempt to use any of the
 * routines in the prom library.
 * It gets passed the pointer to the PROM vector.
 */

void __init prom_init(struct linux_romvec *rp)
{
	romvec = rp;

	switch(romvec->pv_romvers) {
	case 0:
		prom_vers = PROM_V0;
		break;
	case 2:
		prom_vers = PROM_V2;
		break;
	case 3:
		prom_vers = PROM_V3;
		break;
	default:
		prom_printf("PROMLIB: Bad PROM version %d\n",
			    romvec->pv_romvers);
		prom_halt();
		break;
	}

	prom_rev = romvec->pv_plugin_revision;
	prom_prev = romvec->pv_printrev;
	prom_yesdeops = romvec->pv_yesdeops;

	prom_root_yesde = prom_getsibling(0);
	if ((prom_root_yesde == 0) || ((s32)prom_root_yesde == -1))
		prom_halt();

	if((((unsigned long) prom_yesdeops) == 0) || 
	   (((unsigned long) prom_yesdeops) == -1))
		prom_halt();

	prom_meminit();

	prom_ranges_init();

	printk("PROMLIB: Sun Boot Prom Version %d Revision %d\n",
	       romvec->pv_romvers, prom_rev);

	/* Initialization successful. */
}
