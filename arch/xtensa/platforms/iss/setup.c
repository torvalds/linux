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
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
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


void __init platform_init(bp_tag_t* bootparam)
{

}

#ifdef CONFIG_PCI
void platform_pcibios_init(void)
{
}
#endif

void platform_halt(void)
{
	pr_info(" ** Called platform_halt() **\n");
	__asm__ __volatile__("movi a2, 1\nsimcall\n");
}

void platform_power_off(void)
{
	pr_info(" ** Called platform_power_off() **\n");
	__asm__ __volatile__("movi a2, 1\nsimcall\n");
}
void platform_restart(void)
{
	/* Flush and reset the mmu, simulate a processor reset, and
	 * jump to the reset vector. */

	__asm__ __volatile__("movi	a2, 15\n\t"
			     "wsr	a2, " __stringify(ICOUNTLEVEL) "\n\t"
			     "movi	a2, 0\n\t"
			     "wsr	a2, " __stringify(ICOUNT) "\n\t"
			     "wsr	a2, " __stringify(IBREAKENABLE) "\n\t"
			     "wsr	a2, " __stringify(LCOUNT) "\n\t"
			     "movi	a2, 0x1f\n\t"
			     "wsr	a2, " __stringify(PS) "\n\t"
			     "isync\n\t"
			     "jx	%0\n\t"
			     :
			     : "a" (XCHAL_RESET_VECTOR_VADDR)
			     : "a2");

	/* control never gets here */
}

extern void iss_net_poll(void);

const char twirl[]="|/-\\|/-\\";

void platform_heartbeat(void)
{
#if 0
	static int i = 0, j = 0;

	if (--i < 0) {
		i = 99;
		printk("\r%c\r", twirl[j++]);
		if (j == 8)
			j = 0;
	}
#endif
}



static int
iss_panic_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	__asm__ __volatile__("movi a2, -1; simcall\n");
	return NOTIFY_DONE;
}

static struct notifier_block iss_panic_block = {
	iss_panic_event,
	NULL,
	0
};

void __init platform_setup(char **p_cmdline)
{
	atomic_notifier_chain_register(&panic_notifier_list, &iss_panic_block);
}
