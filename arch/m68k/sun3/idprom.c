// SPDX-License-Identifier: GPL-2.0
/*
 * idprom.c: Routines to load the idprom into kernel addresses and
 *           interpret the data contained within.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Sun3/3x models added by David Monro (davidm@psrg.cs.usyd.edu.au)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/string.h>

#include <asm/oplib.h>
#include <asm/idprom.h>
#include <asm/machines.h>  /* Fun with Sun released architectures. */

struct idprom *idprom;
EXPORT_SYMBOL(idprom);

static struct idprom idprom_buffer;

/* Here is the master table of Sun machines which use some implementation
 * of the Sparc CPU and have a meaningful IDPROM machtype value that we
 * know about.  See asm-sparc/machines.h for empirical constants.
 */
static struct Sun_Machine_Models Sun_Machines[NUM_SUN_MACHINES] = {
/* First, Sun3's */
    { .name = "Sun 3/160 Series",	.id_machtype = (SM_SUN3 | SM_3_160) },
    { .name = "Sun 3/50",		.id_machtype = (SM_SUN3 | SM_3_50) },
    { .name = "Sun 3/260 Series",	.id_machtype = (SM_SUN3 | SM_3_260) },
    { .name = "Sun 3/110 Series",	.id_machtype = (SM_SUN3 | SM_3_110) },
    { .name = "Sun 3/60",		.id_machtype = (SM_SUN3 | SM_3_60) },
    { .name = "Sun 3/E",		.id_machtype = (SM_SUN3 | SM_3_E) },
/* Now, Sun3x's */
    { .name = "Sun 3/460 Series",	.id_machtype = (SM_SUN3X | SM_3_460) },
    { .name = "Sun 3/80",		.id_machtype = (SM_SUN3X | SM_3_80) },
/* Then, Sun4's */
// { .name = "Sun 4/100 Series",	.id_machtype = (SM_SUN4 | SM_4_110) },
// { .name = "Sun 4/200 Series",	.id_machtype = (SM_SUN4 | SM_4_260) },
// { .name = "Sun 4/300 Series",	.id_machtype = (SM_SUN4 | SM_4_330) },
// { .name = "Sun 4/400 Series",	.id_machtype = (SM_SUN4 | SM_4_470) },
/* And now, Sun4c's */
// { .name = "Sun4c SparcStation 1",	.id_machtype = (SM_SUN4C | SM_4C_SS1) },
// { .name = "Sun4c SparcStation IPC",	.id_machtype = (SM_SUN4C | SM_4C_IPC) },
// { .name = "Sun4c SparcStation 1+",	.id_machtype = (SM_SUN4C | SM_4C_SS1PLUS) },
// { .name = "Sun4c SparcStation SLC",	.id_machtype = (SM_SUN4C | SM_4C_SLC) },
// { .name = "Sun4c SparcStation 2",	.id_machtype = (SM_SUN4C | SM_4C_SS2) },
// { .name = "Sun4c SparcStation ELC",	.id_machtype = (SM_SUN4C | SM_4C_ELC) },
// { .name = "Sun4c SparcStation IPX",	.id_machtype = (SM_SUN4C | SM_4C_IPX) },
/* Finally, early Sun4m's */
// { .name = "Sun4m SparcSystem600",	.id_machtype = (SM_SUN4M | SM_4M_SS60) },
// { .name = "Sun4m SparcStation10/20",	.id_machtype = (SM_SUN4M | SM_4M_SS50) },
// { .name = "Sun4m SparcStation5",	.id_machtype = (SM_SUN4M | SM_4M_SS40) },
/* One entry for the OBP arch's which are sun4d, sun4e, and newer sun4m's */
// { .name = "Sun4M OBP based system",	.id_machtype = (SM_SUN4M_OBP | 0x0) }
};

static void __init display_system_type(unsigned char machtype)
{
	register int i;

	for (i = 0; i < NUM_SUN_MACHINES; i++) {
		if(Sun_Machines[i].id_machtype == machtype) {
			if (machtype != (SM_SUN4M_OBP | 0x00))
				pr_info("TYPE: %s\n", Sun_Machines[i].name);
			else {
#if 0
				char sysname[128];

				prom_getproperty(prom_root_node, "banner-name",
						 sysname, sizeof(sysname));
				pr_info("TYPE: %s\n", sysname);
#endif
			}
			return;
		}
	}

	prom_printf("IDPROM: Bogus id_machtype value, 0x%x\n", machtype);
	prom_halt();
}

void sun3_get_model(unsigned char* model)
{
	register int i;

	for (i = 0; i < NUM_SUN_MACHINES; i++) {
		if(Sun_Machines[i].id_machtype == idprom->id_machtype) {
		        strcpy(model, Sun_Machines[i].name);
			return;
		}
	}
}



/* Calculate the IDPROM checksum (xor of the data bytes). */
static unsigned char __init calc_idprom_cksum(struct idprom *idprom)
{
	unsigned char cksum, i, *ptr = (unsigned char *)idprom;

	for (i = cksum = 0; i <= 0x0E; i++)
		cksum ^= *ptr++;

	return cksum;
}

/* Create a local IDPROM copy, verify integrity, and display information. */
void __init idprom_init(void)
{
	prom_get_idprom((char *) &idprom_buffer, sizeof(idprom_buffer));

	idprom = &idprom_buffer;

	if (idprom->id_format != 0x01)  {
		prom_printf("IDPROM: Unknown format type!\n");
		prom_halt();
	}

	if (idprom->id_cksum != calc_idprom_cksum(idprom)) {
		prom_printf("IDPROM: Checksum failure (nvram=%x, calc=%x)!\n",
			    idprom->id_cksum, calc_idprom_cksum(idprom));
		prom_halt();
	}

	display_system_type(idprom->id_machtype);

	pr_info("Ethernet address: %pM\n", idprom->id_ethaddr);
}
