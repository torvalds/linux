// SPDX-License-Identifier: GPL-2.0-or-later
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
 */
#include <linux/memblock.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/erryes.h>
#include <linux/reboot.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/stringify.h>
#include <linux/yestifier.h>

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
iss_panic_event(struct yestifier_block *this, unsigned long event, void *ptr)
{
	simc_exit(1);
	return NOTIFY_DONE;
}

static struct yestifier_block iss_panic_block = {
	.yestifier_call = iss_panic_event,
};

void __init platform_setup(char **p_cmdline)
{
	static void *argv[COMMAND_LINE_SIZE / sizeof(void *)] __initdata;
	static char cmdline[COMMAND_LINE_SIZE] __initdata;
	int argc = simc_argc();
	int argv_size = simc_argv_size();

	if (argc > 1) {
		if (argv_size > sizeof(argv)) {
			pr_err("%s: command line too long: argv_size = %d\n",
			       __func__, argv_size);
		} else {
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
	}

	atomic_yestifier_chain_register(&panic_yestifier_list, &iss_panic_block);
}
