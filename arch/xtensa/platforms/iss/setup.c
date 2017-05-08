/*
 *
 * arch/xtensa/platform-iss/setup.c
 *
 * Platform specific initialization.
 *
 * Authors: Chris Zankel <chris@zankel.net>
 *          Joe Taylor <joe@tensilica.com>
 *
 * Copyright 2001 - 2005 Tensilica Inc.
 * Copyright 2017 Cadence Design Systems Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/bootmem.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/stringify.h>
#include <linux/notifier.h>

#include <asm/platform.h>
#include <asm/bootparam.h>
#include <asm/setup.h>

#include <platform/simcall.h>


void __init platform_init(bp_tag_t* bootparam)
{
}

void platform_halt(void)
{
	pr_info(" ** Called platform_halt() **\n");
	simc_exit(0);
}

void platform_power_off(void)
{
	pr_info(" ** Called platform_power_off() **\n");
	simc_exit(0);
}
void platform_restart(void)
{
	/* Flush and reset the mmu, simulate a processor reset, and
	 * jump to the reset vector. */
	cpu_reset();
	/* control never gets here */
}

void platform_heartbeat(void)
{
}

static int
iss_panic_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	simc_exit(1);
	return NOTIFY_DONE;
}

static struct notifier_block iss_panic_block = {
	.notifier_call = iss_panic_event,
};

void __init platform_setup(char **p_cmdline)
{
	int argc = simc_argc();
	int argv_size = simc_argv_size();

	if (argc > 1) {
		void **argv = alloc_bootmem(argv_size);
		char *cmdline = alloc_bootmem(argv_size);
		int i;

		cmdline[0] = 0;
		simc_argv((void *)argv);

		for (i = 1; i < argc; ++i) {
			if (i > 1)
				strcat(cmdline, " ");
			strcat(cmdline, argv[i]);
		}
		*p_cmdline = cmdline;
	}

	atomic_notifier_chain_register(&panic_notifier_list, &iss_panic_block);
}
