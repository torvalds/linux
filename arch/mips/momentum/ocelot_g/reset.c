/*
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Copyright (C) 1997, 2001 Ralf Baechle
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/reboot.h>
#include <asm/system.h>
#include <linux/delay.h>

void momenco_ocelot_restart(char *command)
{
	void *nvram = ioremap_nocache(0x2c807000, 0x1000);

	if (!nvram) {
		printk(KERN_NOTICE "ioremap of reset register failed\n");
		return;
	}
	writeb(0x84, nvram + 0xff7); /* Ask the NVRAM/RTC/watchdog chip to
					assert reset in 1/16 second */
	mdelay(10+(1000/16));
	iounmap(nvram);
	printk(KERN_NOTICE "Watchdog reset failed\n");
}

void momenco_ocelot_halt(void)
{
	printk(KERN_NOTICE "\n** You can safely turn off the power\n");
	while (1)
		__asm__(".set\tmips3\n\t"
	                "wait\n\t"
			".set\tmips0");
}

void momenco_ocelot_power_off(void)
{
	momenco_ocelot_halt();
}
