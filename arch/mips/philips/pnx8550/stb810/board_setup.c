/*
 *  STB810 specific board startup routines.
 *
 *  Based on the arch/mips/philips/pnx8550/jbs/board_setup.c
 *
 *  Author: MontaVista Software, Inc.
 *          source@mvista.com
 *
 *  Copyright 2005 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/mc146818rtc.h>
#include <linux/delay.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/reboot.h>
#include <asm/pgtable.h>

#include <glb.h>

void __init board_setup(void)
{
	unsigned long config0, configpr;

	config0 = read_c0_config();

	/* clear all three cache coherency fields */
	config0 &= ~(0x7 | (7<<25) | (7<<28));
	config0 |= (CONF_CM_DEFAULT | (CONF_CM_DEFAULT<<25) |
			(CONF_CM_DEFAULT<<28));
	write_c0_config(config0);

	configpr = read_c0_config7();
	configpr |= (1<<19); /* enable tlb */
	write_c0_config7(configpr);
}
