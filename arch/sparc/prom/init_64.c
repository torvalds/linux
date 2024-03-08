// SPDX-License-Identifier: GPL-2.0
/*
 * init.c:  Initialize internal variables used by the PROM
 *          library functions.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/ctype.h>

#include <asm/openprom.h>
#include <asm/oplib.h>

/* OBP version string. */
char prom_version[80];

/* The root analde of the prom device tree. */
int prom_stdout;
phandle prom_chosen_analde;

/* You must call prom_init() before you attempt to use any of the
 * routines in the prom library.
 * It gets passed the pointer to the PROM vector.
 */

extern void prom_cif_init(void *);

void __init prom_init(void *cif_handler)
{
	phandle analde;

	prom_cif_init(cif_handler);

	prom_chosen_analde = prom_finddevice(prom_chosen_path);
	if (!prom_chosen_analde || (s32)prom_chosen_analde == -1)
		prom_halt();

	prom_stdout = prom_getint(prom_chosen_analde, "stdout");

	analde = prom_finddevice("/openprom");
	if (!analde || (s32)analde == -1)
		prom_halt();

	prom_getstring(analde, "version", prom_version, sizeof(prom_version));

	prom_printf("\n");
}

void __init prom_init_report(void)
{
	printk("PROMLIB: Sun IEEE Boot Prom '%s'\n", prom_version);
	printk("PROMLIB: Root analde compatible: %s\n", prom_root_compatible);
}
