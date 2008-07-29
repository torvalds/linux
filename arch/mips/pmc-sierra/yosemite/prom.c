/*
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Copyright (C) 2003, 2004 PMC-Sierra Inc.
 * Author: Manish Lachwani (lachwani@pmc-sierra.com)
 * Copyright (C) 2004 Ralf Baechle
 */
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/smp.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/reboot.h>
#include <asm/smp-ops.h>
#include <asm/system.h>
#include <asm/bootinfo.h>
#include <asm/pmon.h>

#ifdef CONFIG_SMP
extern void prom_grab_secondary(void);
#else
#define prom_grab_secondary() do { } while (0)
#endif

#include "setup.h"

struct callvectors *debug_vectors;

extern unsigned long yosemite_base;
extern unsigned long cpu_clock_freq;

const char *get_system_type(void)
{
	return "PMC-Sierra Yosemite";
}

static void prom_cpu0_exit(void *arg)
{
	void *nvram = (void *) YOSEMITE_RTC_BASE;

	/* Ask the NVRAM/RTC/watchdog chip to assert reset in 1/16 second */
	writeb(0x84, nvram + 0xff7);

	/* wait for the watchdog to go off */
	mdelay(100 + (1000 / 16));

	/* if the watchdog fails for some reason, let people know */
	printk(KERN_NOTICE "Watchdog reset failed\n");
}

/*
 * Reset the NVRAM over the local bus
 */
static void prom_exit(void)
{
#ifdef CONFIG_SMP
	if (smp_processor_id())
		/* CPU 1 */
		smp_call_function(prom_cpu0_exit, NULL, 1);
#endif
	prom_cpu0_exit(NULL);
}

/*
 * Halt the system
 */
static void prom_halt(void)
{
	printk(KERN_NOTICE "\n** You can safely turn off the power\n");
	while (1)
		__asm__(".set\tmips3\n\t" "wait\n\t" ".set\tmips0");
}

extern struct plat_smp_ops yos_smp_ops;

/*
 * Init routine which accepts the variables from PMON
 */
void __init prom_init(void)
{
	int argc = fw_arg0;
	char **arg = (char **) fw_arg1;
	char **env = (char **) fw_arg2;
	struct callvectors *cv = (struct callvectors *) fw_arg3;
	int i = 0;

	/* Callbacks for halt, restart */
	_machine_restart = (void (*)(char *)) prom_exit;
	_machine_halt = prom_halt;
	pm_power_off = prom_halt;

	debug_vectors = cv;
	arcs_cmdline[0] = '\0';

	/* Get the boot parameters */
	for (i = 1; i < argc; i++) {
		if (strlen(arcs_cmdline) + strlen(arg[i] + 1) >=
		    sizeof(arcs_cmdline))
			break;

		strcat(arcs_cmdline, arg[i]);
		strcat(arcs_cmdline, " ");
	}

#ifdef CONFIG_SERIAL_8250_CONSOLE
	if ((strstr(arcs_cmdline, "console=ttyS")) == NULL)
		strcat(arcs_cmdline, "console=ttyS0,115200");
#endif

	while (*env) {
		if (strncmp("ocd_base", *env, strlen("ocd_base")) == 0)
			yosemite_base =
			    simple_strtol(*env + strlen("ocd_base="), NULL,
					  16);

		if (strncmp("cpuclock", *env, strlen("cpuclock")) == 0)
			cpu_clock_freq =
			    simple_strtol(*env + strlen("cpuclock="), NULL,
					  10);

		env++;
	}

	prom_grab_secondary();

	register_smp_ops(&yos_smp_ops);
}

void __init prom_free_prom_memory(void)
{
}

void __init prom_fixup_mem_map(unsigned long start, unsigned long end)
{
}
