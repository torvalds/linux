/*
 * idprom.c: Routines to load the idprom into kernel addresses and
 *           interpret the data contained within.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>

#include <asm/oplib.h>
#include <asm/idprom.h>
#include <asm/machines.h>  /* Fun with Sun released architectures. */

struct idprom *idprom;
static struct idprom idprom_buffer;

/* Here is the master table of Sun machines which use some implementation
 * of the Sparc CPU and have a meaningful IDPROM machtype value that we
 * know about.  See asm-sparc/machines.h for empirical constants.
 */
static struct Sun_Machine_Models Sun_Machines[NUM_SUN_MACHINES] = {
/* First, Sun4's */
{ "Sun 4/100 Series", (SM_SUN4 | SM_4_110) },
{ "Sun 4/200 Series", (SM_SUN4 | SM_4_260) },
{ "Sun 4/300 Series", (SM_SUN4 | SM_4_330) },
{ "Sun 4/400 Series", (SM_SUN4 | SM_4_470) },
/* Now, Sun4c's */
{ "Sun4c SparcStation 1", (SM_SUN4C | SM_4C_SS1) },
{ "Sun4c SparcStation IPC", (SM_SUN4C | SM_4C_IPC) },
{ "Sun4c SparcStation 1+", (SM_SUN4C | SM_4C_SS1PLUS) },
{ "Sun4c SparcStation SLC", (SM_SUN4C | SM_4C_SLC) },
{ "Sun4c SparcStation 2", (SM_SUN4C | SM_4C_SS2) },
{ "Sun4c SparcStation ELC", (SM_SUN4C | SM_4C_ELC) },
{ "Sun4c SparcStation IPX", (SM_SUN4C | SM_4C_IPX) },
/* Finally, early Sun4m's */
{ "Sun4m SparcSystem600", (SM_SUN4M | SM_4M_SS60) },
{ "Sun4m SparcStation10/20", (SM_SUN4M | SM_4M_SS50) },
{ "Sun4m SparcStation5", (SM_SUN4M | SM_4M_SS40) },
/* One entry for the OBP arch's which are sun4d, sun4e, and newer sun4m's */
{ "Sun4M OBP based system", (SM_SUN4M_OBP | 0x0) } };

static void __init display_system_type(unsigned char machtype)
{
	char sysname[128];
	register int i;

	for (i = 0; i < NUM_SUN_MACHINES; i++) {
		if(Sun_Machines[i].id_machtype == machtype) {
			if (machtype != (SM_SUN4M_OBP | 0x00) ||
			    prom_getproperty(prom_root_node, "banner-name",
					     sysname, sizeof(sysname)) <= 0)
				printk("TYPE: %s\n", Sun_Machines[i].name);
			else
				printk("TYPE: %s\n", sysname);
			return;
		}
	}

	prom_printf("IDPROM: Bogus id_machtype value, 0x%x\n", machtype);
	prom_halt();
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

	printk("Ethernet address: %x:%x:%x:%x:%x:%x\n",
		    idprom->id_ethaddr[0], idprom->id_ethaddr[1],
		    idprom->id_ethaddr[2], idprom->id_ethaddr[3],
		    idprom->id_ethaddr[4], idprom->id_ethaddr[5]);
}
