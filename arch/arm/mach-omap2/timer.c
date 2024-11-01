/*
 * linux/arch/arm/mach-omap2/timer.c
 *
 * OMAP2 GP timer support.
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * Update to use new clocksource/clockevent layers
 * Author: Kevin Hilman, MontaVista Software, Inc. <source@mvista.com>
 * Copyright (C) 2007 MontaVista Software, Inc.
 *
 * Original driver:
 * Copyright (C) 2005 Nokia Corporation
 * Author: Paul Mundt <paul.mundt@nokia.com>
 *         Juha Yrjölä <juha.yrjola@nokia.com>
 * OMAP Dual-mode timer framework support by Timo Teras
 *
 * Some parts based off of TI's 24xx code:
 *
 * Copyright (C) 2004-2009 Texas Instruments, Inc.
 *
 * Roughly modelled after the OMAP1 MPU timer code.
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/clk.h>
#include <linux/clocksource.h>

#include "soc.h"
#include "common.h"
#include "control.h"
#include "omap-secure.h"

#define REALTIME_COUNTER_BASE				0x48243200
#define INCREMENTER_NUMERATOR_OFFSET			0x10
#define INCREMENTER_DENUMERATOR_RELOAD_OFFSET		0x14
#define NUMERATOR_DENUMERATOR_MASK			0xfffff000

static unsigned long arch_timer_freq;

void set_cntfreq(void)
{
	omap_smc1(OMAP5_DRA7_MON_SET_CNTFRQ_INDEX, arch_timer_freq);
}

/*
 * The realtime counter also called master counter, is a free-running
 * counter, which is related to real time. It produces the count used
 * by the CPU local timer peripherals in the MPU cluster. The timer counts
 * at a rate of 6.144 MHz. Because the device operates on different clocks
 * in different power modes, the master counter shifts operation between
 * clocks, adjusting the increment per clock in hardware accordingly to
 * maintain a constant count rate.
 */
static void __init realtime_counter_init(void)
{
	void __iomem *base;
	static struct clk *sys_clk;
	unsigned long rate;
	unsigned int reg;
	unsigned long long num, den;

	base = ioremap(REALTIME_COUNTER_BASE, SZ_32);
	if (!base) {
		pr_err("%s: ioremap failed\n", __func__);
		return;
	}
	sys_clk = clk_get(NULL, "sys_clkin");
	if (IS_ERR(sys_clk)) {
		pr_err("%s: failed to get system clock handle\n", __func__);
		iounmap(base);
		return;
	}

	rate = clk_get_rate(sys_clk);
	clk_put(sys_clk);

	if (soc_is_dra7xx()) {
		/*
		 * Errata i856 says the 32.768KHz crystal does not start at
		 * power on, so the CPU falls back to an emulated 32KHz clock
		 * based on sysclk / 610 instead. This causes the master counter
		 * frequency to not be 6.144MHz but at sysclk / 610 * 375 / 2
		 * (OR sysclk * 75 / 244)
		 *
		 * This affects at least the DRA7/AM572x 1.0, 1.1 revisions.
		 * Of course any board built without a populated 32.768KHz
		 * crystal would also need this fix even if the CPU is fixed
		 * later.
		 *
		 * Either case can be detected by using the two speedselect bits
		 * If they are not 0, then the 32.768KHz clock driving the
		 * coarse counter that corrects the fine counter every time it
		 * ticks is actually rate/610 rather than 32.768KHz and we
		 * should compensate to avoid the 570ppm (at 20MHz, much worse
		 * at other rates) too fast system time.
		 */
		reg = omap_ctrl_readl(DRA7_CTRL_CORE_BOOTSTRAP);
		if (reg & DRA7_SPEEDSELECT_MASK) {
			num = 75;
			den = 244;
			goto sysclk1_based;
		}
	}

	/* Numerator/denumerator values refer TRM Realtime Counter section */
	switch (rate) {
	case 12000000:
		num = 64;
		den = 125;
		break;
	case 13000000:
		num = 768;
		den = 1625;
		break;
	case 19200000:
		num = 8;
		den = 25;
		break;
	case 20000000:
		num = 192;
		den = 625;
		break;
	case 26000000:
		num = 384;
		den = 1625;
		break;
	case 27000000:
		num = 256;
		den = 1125;
		break;
	case 38400000:
	default:
		/* Program it for 38.4 MHz */
		num = 4;
		den = 25;
		break;
	}

sysclk1_based:
	/* Program numerator and denumerator registers */
	reg = readl_relaxed(base + INCREMENTER_NUMERATOR_OFFSET) &
			NUMERATOR_DENUMERATOR_MASK;
	reg |= num;
	writel_relaxed(reg, base + INCREMENTER_NUMERATOR_OFFSET);

	reg = readl_relaxed(base + INCREMENTER_DENUMERATOR_RELOAD_OFFSET) &
			NUMERATOR_DENUMERATOR_MASK;
	reg |= den;
	writel_relaxed(reg, base + INCREMENTER_DENUMERATOR_RELOAD_OFFSET);

	arch_timer_freq = DIV_ROUND_UP_ULL(rate * num, den);
	set_cntfreq();

	iounmap(base);
}

void __init omap5_realtime_timer_init(void)
{
	omap_clk_init();
	realtime_counter_init();

	timer_probe();
}
