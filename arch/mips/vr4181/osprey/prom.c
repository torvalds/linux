/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * arch/mips/vr4181/osprey/prom.c
 *     prom code for osprey.
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <asm/bootinfo.h>
#include <asm/addrspace.h>

const char *get_system_type(void)
{
	return "NEC_Vr41xx Osprey";
}

/*
 * [jsun] right now we assume it is the nec debug monitor, which does
 * not pass any arguments.
 */
void __init prom_init(void)
{
	// cmdline is now set in default config
	// strcpy(arcs_cmdline, "ip=bootp ");
	// strcat(arcs_cmdline, "ether=46,0x03fe0300,eth0 ");
	// strcpy(arcs_cmdline, "ether=0,0x0300,eth0 "
	// strcat(arcs_cmdline, "video=vr4181fb:xres:240,yres:320,bpp:8 ");

	mips_machgroup = MACH_GROUP_NEC_VR41XX;
	mips_machtype = MACH_NEC_OSPREY;

	/* 16MB fixed */
	add_memory_region(0, 16 << 20, BOOT_MEM_RAM);
}

unsigned long __init prom_free_prom_memory(void)
{
	return 0;
}
