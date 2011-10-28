/* MN10300 Profiling setup
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

/*
 * initialise the profiling if enabled
 * - using with gdbstub will give anomalous results
 * - can't be used with gdbstub if running at IRQ priority 0
 */
static __init int profile_init(void)
{
	u16 tmp;

	if (!prof_buffer)
		return 0;

	/* use timer 11 to drive the profiling interrupts */
	set_intr_stub(EXCEP_IRQ_LEVEL0, profile_handler);

	/* set IRQ priority at which to run */
	set_intr_level(TM11IRQ, GxICR_LEVEL_0);

	/* set up timer 11
	 * - source: (IOCLK 33MHz)*2 = 66MHz
	 * - frequency: (33330000*2) / 8 / 20625 = 202Hz
	 */
	TM11BR = 20625 - 1;
	TM11MD = TM8MD_SRC_IOCLK_8;
	TM11MD |= TM8MD_INIT_COUNTER;
	TM11MD &= ~TM8MD_INIT_COUNTER;
	TM11MD |= TM8MD_COUNT_ENABLE;

	TM11ICR |= GxICR_ENABLE;
	tmp = TM11ICR;

	printk(KERN_INFO "Profiling initiated on timer 11, priority 0, %uHz\n",
	       MN10300_IOCLK / 8 / (TM11BR + 1));
	printk(KERN_INFO "Profile histogram stored %p-%p\n",
	       prof_buffer, (u8 *)(prof_buffer + prof_len) - 1);

	return 0;
}

__initcall(profile_init);
