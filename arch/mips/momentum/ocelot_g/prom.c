/*
 * Copyright 2002 Momentum Computer Inc.
 * Author: Matthew Dharm <mdharm@momenco.com>
 *
 * Based on Ocelot Linux port, which is
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/bootmem.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/pmon.h>
#include <asm/gt64240.h>

#include "ocelot_pld.h"

struct callvectors* debug_vectors;

extern unsigned long marvell_base;
extern unsigned long bus_clock;

#ifdef CONFIG_GALILLEO_GT64240_ETH
extern unsigned char prom_mac_addr_base[6];
#endif

const char *get_system_type(void)
{
	return "Momentum Ocelot";
}

void __init prom_init(void)
{
	int argc = fw_arg0;
	char **arg = (char **) fw_arg1;
	char **env = (char **) fw_arg2;
	struct callvectors *cv = (struct callvectors *) fw_arg3;
	int i;

	/* save the PROM vectors for debugging use */
	debug_vectors = cv;

	/* arg[0] is "g", the rest is boot parameters */
	arcs_cmdline[0] = '\0';
	for (i = 1; i < argc; i++) {
		if (strlen(arcs_cmdline) + strlen(arg[i] + 1)
		    >= sizeof(arcs_cmdline))
			break;
		strcat(arcs_cmdline, arg[i]);
		strcat(arcs_cmdline, " ");
	}

	mips_machgroup = MACH_GROUP_MOMENCO;
	mips_machtype = MACH_MOMENCO_OCELOT_G;

#ifdef CONFIG_GALILLEO_GT64240_ETH
	/* get the base MAC address for on-board ethernet ports */
	memcpy(prom_mac_addr_base, (void*)0xfc807cf2, 6);
#endif

	while (*env) {
		if (strncmp("gtbase", *env, strlen("gtbase")) == 0) {
			marvell_base = simple_strtol(*env + strlen("gtbase="),
							NULL, 16);
		}
		if (strncmp("busclock", *env, strlen("busclock")) == 0) {
			bus_clock = simple_strtol(*env + strlen("busclock="),
							NULL, 10);
		}
		env++;
	}
}

unsigned long __init prom_free_prom_memory(void)
{
	return 0;
}
