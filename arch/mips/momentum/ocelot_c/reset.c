/*
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Copyright (C) 1997, 2001 Ralf Baechle
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * Copyright (C) 2002 Momentum Computer Inc.
 * Author: Matthew Dharm <mdharm@momenco.com>
 *
 * Louis Hamilton, Red Hat, Inc.
 * hamilton@redhat.com  [MIPS64 modifications]
 */
#include <linux/config.h>
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
	/* base address of timekeeper portion of part */
	void *nvram = (void *)
#ifdef CONFIG_MIPS64
		0xfffffffffc807000;
#else
		0xfc807000;
#endif

 	/* Ask the NVRAM/RTC/watchdog chip to assert reset in 1/16 second */
	writeb(0x84, nvram + 0xff7);

	/* wait for the watchdog to go off */
	mdelay(100+(1000/16));

	/* if the watchdog fails for some reason, let people know */
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
