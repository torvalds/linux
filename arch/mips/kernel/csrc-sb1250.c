// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2000, 2001 Broadcom Corporation
 */
#include <linux/clocksource.h>
#include <linux/sched_clock.h>

#include <asm/addrspace.h>
#include <asm/io.h>
#include <asm/time.h>

#include <asm/sibyte/sb1250.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_int.h>
#include <asm/sibyte/sb1250_scd.h>

#define SB1250_HPT_NUM		3
#define SB1250_HPT_VALUE	M_SCD_TIMER_CNT /* max value */

/*
 * The HPT is free running from SB1250_HPT_VALUE down to 0 then starts over
 * again.
 */
static inline u64 sb1250_hpt_get_cycles(void)
{
	unsigned int count;
	void __iomem *addr;

	addr = IOADDR(A_SCD_TIMER_REGISTER(SB1250_HPT_NUM, R_SCD_TIMER_CNT));
	count = G_SCD_TIMER_CNT(__raw_readq(addr));

	return SB1250_HPT_VALUE - count;
}

static u64 sb1250_hpt_read(struct clocksource *cs)
{
	return sb1250_hpt_get_cycles();
}

struct clocksource bcm1250_clocksource = {
	.name	= "bcm1250-counter-3",
	.rating = 200,
	.read	= sb1250_hpt_read,
	.mask	= CLOCKSOURCE_MASK(23),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

static u64 notrace sb1250_read_sched_clock(void)
{
	return sb1250_hpt_get_cycles();
}

void __init sb1250_clocksource_init(void)
{
	struct clocksource *cs = &bcm1250_clocksource;

	/* Setup hpt using timer #3 but do not enable irq for it */
	__raw_writeq(0,
		     IOADDR(A_SCD_TIMER_REGISTER(SB1250_HPT_NUM,
						 R_SCD_TIMER_CFG)));
	__raw_writeq(SB1250_HPT_VALUE,
		     IOADDR(A_SCD_TIMER_REGISTER(SB1250_HPT_NUM,
						 R_SCD_TIMER_INIT)));
	__raw_writeq(M_SCD_TIMER_ENABLE | M_SCD_TIMER_MODE_CONTINUOUS,
		     IOADDR(A_SCD_TIMER_REGISTER(SB1250_HPT_NUM,
						 R_SCD_TIMER_CFG)));

	clocksource_register_hz(cs, V_SCD_TIMER_FREQ);

	sched_clock_register(sb1250_read_sched_clock, 23, V_SCD_TIMER_FREQ);
}
