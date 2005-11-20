/*
 *  linux/arch/arm/mach-realview/localtimer.c
 *
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/smp.h>

#include <asm/mach/time.h>
#include <asm/hardware/arm_twd.h>
#include <asm/hardware/gic.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>

#define TWD_BASE(cpu)	(__io_address(REALVIEW_TWD_BASE) + \
			 ((cpu) * REALVIEW_TWD_SIZE))

static unsigned long mpcore_timer_rate;

/*
 * local_timer_ack: checks for a local timer interrupt.
 *
 * If a local timer interrupt has occured, acknowledge and return 1.
 * Otherwise, return 0.
 */
int local_timer_ack(void)
{
	void __iomem *base = TWD_BASE(smp_processor_id());

	if (__raw_readl(base + TWD_TIMER_INTSTAT)) {
		__raw_writel(1, base + TWD_TIMER_INTSTAT);
		return 1;
	}

	return 0;
}

void __cpuinit local_timer_setup(unsigned int cpu)
{
	void __iomem *base = TWD_BASE(cpu);
	unsigned int load, offset;
	u64 waitjiffies;
	unsigned int count;

	/*
	 * If this is the first time round, we need to work out how fast
	 * the timer ticks
	 */
	if (mpcore_timer_rate == 0) {
		printk("Calibrating local timer... ");

		/* Wait for a tick to start */
		waitjiffies = get_jiffies_64() + 1;

		while (get_jiffies_64() < waitjiffies)
			udelay(10);

		/* OK, now the tick has started, let's get the timer going */
		waitjiffies += 5;

				 /* enable, no interrupt or reload */
		__raw_writel(0x1, base + TWD_TIMER_CONTROL);

				 /* maximum value */
		__raw_writel(0xFFFFFFFFU, base + TWD_TIMER_COUNTER);

		while (get_jiffies_64() < waitjiffies)
			udelay(10);

		count = __raw_readl(base + TWD_TIMER_COUNTER);

		mpcore_timer_rate = (0xFFFFFFFFU - count) * (HZ / 5);

		printk("%lu.%02luMHz.\n", mpcore_timer_rate / 1000000,
			(mpcore_timer_rate / 100000) % 100);
	}

	load = mpcore_timer_rate / HZ;

	__raw_writel(load, base + TWD_TIMER_LOAD);
	__raw_writel(0x7,  base + TWD_TIMER_CONTROL);

	/*
	 * Now maneuver our local tick into the right part of the jiffy.
	 * Start by working out where within the tick our local timer
	 * interrupt should go.
	 */
	offset = ((mpcore_timer_rate / HZ) / (NR_CPUS + 1)) * (cpu + 1);

	/*
	 * gettimeoffset() will return a number of us since the last tick.
	 * Convert this number of us to a local timer tick count.
	 * Be careful of integer overflow whilst keeping maximum precision.
	 *
	 * with HZ=100 and 1MHz (fpga) ~ 1GHz processor:
	 * load = 1 ~ 10,000
	 * mpcore_timer_rate/10000 = 100 ~ 100,000
	 *
	 * so the multiply value will be less than 10^9 always.
	 */
	load = (system_timer->offset() * (mpcore_timer_rate / 10000)) / 100;

	/* Add on our offset to get the load value */
	load = (load + offset) % (mpcore_timer_rate / HZ);

	__raw_writel(load, base + TWD_TIMER_COUNTER);

	/* Make sure our local interrupt controller has this enabled */
	__raw_writel(1 << IRQ_LOCALTIMER,
		     __io_address(REALVIEW_GIC_DIST_BASE) + GIC_DIST_ENABLE_SET);
}

/*
 * take a local timer down
 */
void __cpuexit local_timer_stop(unsigned int cpu)
{
	__raw_writel(0, TWD_BASE(cpu) + TWD_TIMER_CONTROL);
}
