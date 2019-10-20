// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2007 Lemote, Inc. & Institute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 *
 * Copyright (C) 2009 Lemote Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 */
#include <asm/mc146818-time.h>
#include <asm/time.h>
#include <asm/hpet.h>

#include <loongson.h>

void __init plat_time_init(void)
{
	/* setup mips r4k timer */
	mips_hpt_frequency = cpu_clock_freq / 2;

#ifdef CONFIG_RS780_HPET
	setup_hpet_timer();
#endif
}

void read_persistent_clock64(struct timespec64 *ts)
{
	ts->tv_sec = mc146818_get_cmos_time();
	ts->tv_nsec = 0;
}
