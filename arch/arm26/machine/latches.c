/*
 *  linux/arch/arm26/kernel/latches.c
 *
 *  Copyright (C) David Alan Gilbert 1995/1996,2000
 *  Copyright (C) Ian Molton 2003
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Support for the latches on the old Archimedes which control the floppy,
 *  hard disc and printer
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>

#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/oldlatches.h>

static unsigned char latch_a_copy;
static unsigned char latch_b_copy;

/* newval=(oldval & ~mask)|newdata */
void oldlatch_aupdate(unsigned char mask,unsigned char newdata)
{
	unsigned long flags;

	BUG_ON(!machine_is_archimedes());

	local_irq_save(flags); //FIXME: was local_save_flags
	latch_a_copy = (latch_a_copy & ~mask) | newdata;
	__raw_writeb(latch_a_copy, LATCHA_BASE);
	local_irq_restore(flags);

	printk("Latch: A = 0x%02x\n", latch_a_copy);
}


/* newval=(oldval & ~mask)|newdata */
void oldlatch_bupdate(unsigned char mask,unsigned char newdata)
{
	unsigned long flags;

	BUG_ON(!machine_is_archimedes());


	local_irq_save(flags);//FIXME: was local_save_flags
	latch_b_copy = (latch_b_copy & ~mask) | newdata;
	__raw_writeb(latch_b_copy, LATCHB_BASE);
	local_irq_restore(flags);

	printk("Latch: B = 0x%02x\n", latch_b_copy);
}

static int __init oldlatch_init(void)
{
	if (machine_is_archimedes()) {
		oldlatch_aupdate(0xff, 0xff);
		/* Thats no FDC reset...*/
		oldlatch_bupdate(0xff, LATCHB_FDCRESET);
	}
	return 0;
}

arch_initcall(oldlatch_init);

EXPORT_SYMBOL(oldlatch_aupdate);
EXPORT_SYMBOL(oldlatch_bupdate);
