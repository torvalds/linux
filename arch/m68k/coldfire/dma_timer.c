/*
 * dma_timer.c -- Freescale ColdFire DMA Timer.
 *
 * Copyright (C) 2007, Benedikt Spranger <b.spranger@linutronix.de>
 * Copyright (C) 2008. Sebastian Siewior, Linutronix
 *
 */

#include <linux/clocksource.h>
#include <linux/io.h>

#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcfpit.h>
#include <asm/mcfsim.h>

#define DMA_TIMER_0	(0x00)
#define DMA_TIMER_1	(0x40)
#define DMA_TIMER_2	(0x80)
#define DMA_TIMER_3	(0xc0)

#define DTMR0	(MCF_IPSBAR + DMA_TIMER_0 + 0x400)
#define DTXMR0	(MCF_IPSBAR + DMA_TIMER_0 + 0x402)
#define DTER0	(MCF_IPSBAR + DMA_TIMER_0 + 0x403)
#define DTRR0	(MCF_IPSBAR + DMA_TIMER_0 + 0x404)
#define DTCR0	(MCF_IPSBAR + DMA_TIMER_0 + 0x408)
#define DTCN0	(MCF_IPSBAR + DMA_TIMER_0 + 0x40c)

#define DMA_FREQ    ((MCF_CLK / 2) / 16)

/* DTMR */
#define DMA_DTMR_RESTART	(1 << 3)
#define DMA_DTMR_CLK_DIV_1	(1 << 1)
#define DMA_DTMR_CLK_DIV_16	(2 << 1)
#define DMA_DTMR_ENABLE		(1 << 0)

static u64 cf_dt_get_cycles(struct clocksource *cs)
{
	return __raw_readl(DTCN0);
}

static struct clocksource clocksource_cf_dt = {
	.name		= "coldfire_dma_timer",
	.rating		= 200,
	.read		= cf_dt_get_cycles,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static int __init  init_cf_dt_clocksource(void)
{
	/*
	 * We setup DMA timer 0 in free run mode. This incrementing counter is
	 * used as a highly precious clock source. With MCF_CLOCK = 150 MHz we
	 * get a ~213 ns resolution and the 32bit register will overflow almost
	 * every 15 minutes.
	 */
	__raw_writeb(0x00, DTXMR0);
	__raw_writeb(0x00, DTER0);
	__raw_writel(0x00000000, DTRR0);
	__raw_writew(DMA_DTMR_CLK_DIV_16 | DMA_DTMR_ENABLE, DTMR0);
	return clocksource_register_hz(&clocksource_cf_dt, DMA_FREQ);
}

arch_initcall(init_cf_dt_clocksource);

#define CYC2NS_SCALE_FACTOR 10 /* 2^10, carefully chosen */
#define CYC2NS_SCALE	((1000000 << CYC2NS_SCALE_FACTOR) / (DMA_FREQ / 1000))

static unsigned long long cycles2ns(unsigned long cycl)
{
	return (unsigned long long) ((unsigned long long)cycl *
			CYC2NS_SCALE) >> CYC2NS_SCALE_FACTOR;
}

unsigned long long sched_clock(void)
{
	unsigned long cycl = __raw_readl(DTCN0);

	return cycles2ns(cycl);
}
