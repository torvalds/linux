/*
 * Copyright 2002 Momentum Computer Inc.
 * Author: Matthew Dharm <mdharm@momenco.com>
 *
 * Louis Hamilton, Red Hat, Inc.
 * hamilton@redhat.com  [MIPS64 modifications]
 *
 * Based on Ocelot Linux port, which is
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Added changes for SMP - Manish Lachwani (lachwani@pmc-sierra.com)
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/bootmem.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/mv64340.h>
#include <asm/pmon.h>

#include "jaguar_atx_fpga.h"

extern void ja_setup_console(void);

struct callvectors *debug_vectors;

extern unsigned long cpu_clock;

const char *get_system_type(void)
{
	return "Momentum Jaguar-ATX";
}

#ifdef CONFIG_MV643XX_ETH
extern unsigned char prom_mac_addr_base[6];

static void burn_clocks(void)
{
	int i;

	/* this loop should burn at least 1us -- this should be plenty */
	for (i = 0; i < 0x10000; i++)
		;
}

static u8 exchange_bit(u8 val, u8 cs)
{
	/* place the data */
	JAGUAR_FPGA_WRITE((val << 2) | cs, EEPROM_MODE);
	burn_clocks();

	/* turn the clock on */
	JAGUAR_FPGA_WRITE((val << 2) | cs | 0x2, EEPROM_MODE);
	burn_clocks();

	/* turn the clock off and read-strobe */
	JAGUAR_FPGA_WRITE((val << 2) | cs | 0x10, EEPROM_MODE);

	/* return the data */
	return ((JAGUAR_FPGA_READ(EEPROM_MODE) >> 3) & 0x1);
}

void get_mac(char dest[6])
{
	u8 read_opcode[12] = {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	int i,j;

	for (i = 0; i < 12; i++)
		exchange_bit(read_opcode[i], 1);

	for (j = 0; j < 6; j++) {
		dest[j] = 0;
		for (i = 0; i < 8; i++) {
			dest[j] <<= 1;
			dest[j] |= exchange_bit(0, 1);
		}
	}

	/* turn off CS */
	exchange_bit(0,0);
}
#endif

#ifdef CONFIG_64BIT

unsigned long signext(unsigned long addr)
{
	addr &= 0xffffffff;
	return (unsigned long)((int)addr);
}

void *get_arg(unsigned long args, int arc)
{
	unsigned long ul;
	unsigned char *puc, uc;

	args += (arc * 4);
	ul = (unsigned long)signext(args);
	puc = (unsigned char *)ul;
	if (puc == 0)
		return (void *)0;

#ifdef CONFIG_CPU_LITTLE_ENDIAN
	uc = *puc++;
	l = (unsigned long)uc;
	uc = *puc++;
	ul |= (((unsigned long)uc) << 8);
	uc = *puc++;
	ul |= (((unsigned long)uc) << 16);
	uc = *puc++;
	ul |= (((unsigned long)uc) << 24);
#else
	uc = *puc++;
	ul = ((unsigned long)uc) << 24;
	uc = *puc++;
	ul |= (((unsigned long)uc) << 16);
	uc = *puc++;
	ul |= (((unsigned long)uc) << 8);
	uc = *puc++;
	ul |= ((unsigned long)uc);
#endif
	ul = signext(ul);

	return (void *)ul;
}

char *arg64(unsigned long addrin, int arg_index)
{
	unsigned long args;
	char *p;

	args = signext(addrin);
	p = (char *)get_arg(args, arg_index);

	return p;
}
#endif  /* CONFIG_64BIT */

/* PMON passes arguments in C main() style */
void __init prom_init(void)
{
	int argc = fw_arg0;
	char **arg = (char **) fw_arg1;
	char **env = (char **) fw_arg2;
	struct callvectors *cv = (struct callvectors *) fw_arg3;
	int i;

#ifdef CONFIG_SERIAL_8250_CONSOLE
//	ja_setup_console();	/* The very first thing.  */
#endif

#ifdef CONFIG_64BIT
	char *ptr;

	printk("Mips64 Jaguar-ATX\n");
	/* save the PROM vectors for debugging use */
	debug_vectors = (struct callvectors *)signext((unsigned long)cv);

	/* arg[0] is "g", the rest is boot parameters */
	arcs_cmdline[0] = '\0';

	for (i = 1; i < argc; i++) {
		ptr = (char *)arg64((unsigned long)arg, i);
		if ((strlen(arcs_cmdline) + strlen(ptr) + 1) >=
		    sizeof(arcs_cmdline))
			break;
		strcat(arcs_cmdline, ptr);
		strcat(arcs_cmdline, " ");
	}

	i = 0;
	while (1) {
		ptr = (char *)arg64((unsigned long)env, i);
		if (! ptr)
			break;

		if (strncmp("gtbase", ptr, strlen("gtbase")) == 0) {
			marvell_base = simple_strtol(ptr + strlen("gtbase="),
							NULL, 16);

			if ((marvell_base & 0xffffffff00000000) == 0)
				marvell_base |= 0xffffffff00000000;

			printk("marvell_base set to 0x%016lx\n", marvell_base);
		}
		if (strncmp("cpuclock", ptr, strlen("cpuclock")) == 0) {
			cpu_clock = simple_strtol(ptr + strlen("cpuclock="),
							NULL, 10);
			printk("cpu_clock set to %d\n", cpu_clock);
		}
		i++;
	}
	printk("arcs_cmdline: %s\n", arcs_cmdline);

#else   /* CONFIG_64BIT */
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

	while (*env) {
		if (strncmp("gtbase", *env, strlen("gtbase")) == 0) {
			marvell_base = simple_strtol(*env + strlen("gtbase="),
							NULL, 16);
		}
		if (strncmp("cpuclock", *env, strlen("cpuclock")) == 0) {
			cpu_clock = simple_strtol(*env + strlen("cpuclock="),
							NULL, 10);
		}
		env++;
	}
#endif /* CONFIG_64BIT */
	mips_machgroup = MACH_GROUP_MOMENCO;
	mips_machtype = MACH_MOMENCO_JAGUAR_ATX;

#ifdef CONFIG_MV643XX_ETH
	/* get the base MAC address for on-board ethernet ports */
	get_mac(prom_mac_addr_base);
#endif
}

unsigned long __init prom_free_prom_memory(void)
{
	return 0;
}

void __init prom_fixup_mem_map(unsigned long start, unsigned long end)
{
}

int prom_boot_secondary(int cpu, unsigned long sp, unsigned long gp)
{
	/* Clear the semaphore */
	*(volatile uint32_t *)(0xbb000a68) = 0x80000000;

	return 1;
}

void prom_init_secondary(void)
{
        clear_c0_config(CONF_CM_CMASK);
        set_c0_config(0x2);

	clear_c0_status(ST0_IM);
	set_c0_status(0x1ffff);
}

void prom_smp_finish(void)
{
}
