// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2000,2001,2004 Broadcom Corporation
 */
#include <linux/clocksource.h>
#include <linux/sched_clock.h>

#include <asm/addrspace.h>
#include <asm/io.h>
#include <asm/time.h>

#include <asm/sibyte/bcm1480_regs.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/bcm1480_int.h>
#include <asm/sibyte/bcm1480_scd.h>

#include <asm/sibyte/sb1250.h>

static u64 bcm1480_hpt_read(struct clocksource *cs)
{
	return (u64) __raw_readq(IOADDR(A_SCD_ZBBUS_CYCLE_COUNT));
}

struct clocksource bcm1480_clocksource = {
	.name	= "zbbus-cycles",
	.rating = 200,
	.read	= bcm1480_hpt_read,
	.mask	= CLOCKSOURCE_MASK(64),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

static u64 notrace sb1480_read_sched_clock(void)
{
	return __raw_readq(IOADDR(A_SCD_ZBBUS_CYCLE_COUNT));
}

void __init sb1480_clocksource_init(void)
{
	struct clocksource *cs = &bcm1480_clocksource;
	unsigned int plldiv;
	unsigned long zbbus;

	plldiv = G_BCM1480_SYS_PLL_DIV(__raw_readq(IOADDR(A_SCD_SYSTEM_CFG)));
	zbbus = ((plldiv >> 1) * 50000000) + ((plldiv & 1) * 25000000);
	clocksource_register_hz(cs, zbbus);

	sched_clock_register(sb1480_read_sched_clock, 64, zbbus);
}
