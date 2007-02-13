/*
 * Copyright 2002 Momentum Computer Inc.
 * Author: Matthew Dharm <mdharm@momenco.com>
 *
 * Louis Hamilton, Red Hat, Inc.
 *   hamilton@redhat.com  [MIPS64 modifications]
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
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/bootmem.h>
#include <linux/mv643xx.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/pmon.h>

#include "ocelot_c_fpga.h"

struct callvectors* debug_vectors;

extern unsigned long marvell_base;
extern unsigned int cpu_clock;

const char *get_system_type(void)
{
#ifdef CONFIG_CPU_SR71000
	return "Momentum Ocelot-CS";
#else
	return "Momentum Ocelot-C";
#endif
}

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
  ul = (unsigned long)uc;
  uc = *puc++;
  ul |= (((unsigned long)uc) << 8);
  uc = *puc++;
  ul |= (((unsigned long)uc) << 16);
  uc = *puc++;
  ul |= (((unsigned long)uc) << 24);
#else  /* CONFIG_CPU_LITTLE_ENDIAN */
  uc = *puc++;
  ul = ((unsigned long)uc) << 24;
  uc = *puc++;
  ul |= (((unsigned long)uc) << 16);
  uc = *puc++;
  ul |= (((unsigned long)uc) << 8);
  uc = *puc++;
  ul |= ((unsigned long)uc);
#endif  /* CONFIG_CPU_LITTLE_ENDIAN */
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


void __init prom_init(void)
{
	int argc = fw_arg0;
	char **arg = (char **) fw_arg1;
	char **env = (char **) fw_arg2;
	struct callvectors *cv = (struct callvectors *) fw_arg3;
	int i;

#ifdef CONFIG_64BIT
	char *ptr;

	printk("prom_init - MIPS64\n");
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
	mips_machtype = MACH_MOMENCO_OCELOT_C;

#ifndef CONFIG_64BIT
	debug_vectors->printf("Booting Linux kernel...\n");
#endif
}

void __init prom_free_prom_memory(void)
{
}
