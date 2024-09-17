// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Cobalt time initialization.
 *
 *  Copyright (C) 2007  Yoichi Yuasa <yuasa@linux-mips.org>
 */
#include <linux/i8253.h>
#include <linux/init.h>

#include <asm/gt64120.h>
#include <asm/time.h>

#define GT641XX_BASE_CLOCK	50000000	/* 50MHz */

void __init plat_time_init(void)
{
	u32 start, end;
	int i = HZ / 10;

	setup_pit_timer();

	gt641xx_set_base_clock(GT641XX_BASE_CLOCK);

	/*
	 * MIPS counter frequency is measured during a 100msec interval
	 * using GT64111 timer0.
	 */
	while (!gt641xx_timer0_state())
		;

	start = read_c0_count();

	while (i--)
		while (!gt641xx_timer0_state())
			;

	end = read_c0_count();

	mips_hpt_frequency = (end - start) * 10;
	printk(KERN_INFO "MIPS counter frequency %dHz\n", mips_hpt_frequency);
}
