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

/* The root node of the prom device tree. */
phandle prom_root_node;
EXPORT_SYMBOL(prom_root_node);

/* Pointer to the device tree operations structure. */
struct linux_nodeops *prom_nodeops;

/* You must call prom_init() before you attempt to use any of the
 * routines in the prom library.  It returns 0 on success, 1 on
 * failure.  It gets passed the pointer to the PROM vector.
 */

extern void prom_meminit(void);
extern void prom_ranges_init(void);

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
	prom_nodeops = romvec->pv_nodeops;

	prom_root_node = prom_getsibling(0);
	if ((prom_root_node == 0) || ((s32)prom_root_node == -1))
		prom_halt();

	if((((unsigned long) prom_nodeops) == 0) || 
	   (((unsigned long) prom_nodeops) == -1))
		prom_halt();

	prom_meminit();

	prom_ranges_init();

	printk("PROMLIB: Sun Boot Prom Version %d Revision %d\n",
	       romvec->pv_romvers, prom_rev);

	/* Initialization successful. */
}
