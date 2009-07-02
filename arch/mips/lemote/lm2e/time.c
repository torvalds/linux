/*
 * Copyright (C) 2007 Lemote, Inc. & Institute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 *
 * Copyright (C) 2009 Lemote Inc. & Insititute of Computing Technology
 * Author: Wu Zhangjin, wuzj@lemote.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/init.h>

#include <asm/mc146818-time.h>
#include <asm/time.h>

extern unsigned long cpu_clock_freq;

void __init plat_time_init(void)
{
	/* setup mips r4k timer */
	mips_hpt_frequency = cpu_clock_freq / 2;
}

unsigned long read_persistent_clock(void)
{
	return mc146818_get_cmos_time();
}
