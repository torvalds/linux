/*
 * cpu.c: clock scaling for the iMX
 *
 * Copyright (C) 2000 2001, The Delft University of Technology
 * Copyright (c) 2004 Sascha Hauer <sascha@saschahauer.de>
 * Copyright (C) 2006 Inky Lung <ilung@cwlinux.com>
 * Copyright (C) 2006 Pavel Pisa, PiKRON <ppisa@pikron.com>
 *
 * Based on SA1100 version written by:
 * - Johan Pouwelse (J.A.Pouwelse@its.tudelft.nl): initial version
 * - Erik Mouw (J.A.K.Mouw@its.tudelft.nl):
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/*#define DEBUG*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <asm/system.h>

#include <mach/hardware.h>

#include "generic.h"

#ifndef __val2mfld
#define __val2mfld(mask,val) (((mask)&~((mask)<<1))*(val)&(mask))
#endif
#ifndef __mfld2val
#define __mfld2val(mask,val) (((val)&(mask))/((mask)&~((mask)<<1)))
#endif

#define CR_920T_CLOCK_MODE	0xC0000000
#define CR_920T_FASTBUS_MODE	0x00000000
#define CR_920T_ASYNC_MODE	0xC0000000

static u32 mpctl0_at_boot;
static u32 bclk_div_at_boot;

static struct clk *system_clk, *mcu_clk;

static void imx_set_async_mode(void)
{
	adjust_cr(CR_920T_CLOCK_MODE, CR_920T_ASYNC_MODE);
}

static void imx_set_fastbus_mode(void)
{
	adjust_cr(CR_920T_CLOCK_MODE, CR_920T_FASTBUS_MODE);
}

static void imx_set_mpctl0(u32 mpctl0)
{
	unsigned long flags;

	if (mpctl0 == 0) {
		local_irq_save(flags);
		CSCR &= ~CSCR_MPEN;
		local_irq_restore(flags);
		return;
	}

	local_irq_save(flags);
	MPCTL0 = mpctl0;
	CSCR |= CSCR_MPEN;
	local_irq_restore(flags);
}

/**
 * imx_compute_mpctl - compute new PLL parameters
 * @new_mpctl:	pointer to location assigned by new PLL control register value
 * @cur_mpctl:	current PLL control register parameters
 * @f_ref:	reference source frequency Hz
 * @freq:	required frequency in Hz
 * @relation:	is one of %CPUFREQ_RELATION_L (supremum)
 *		and %CPUFREQ_RELATION_H (infimum)
 */
long imx_compute_mpctl(u32 *new_mpctl, u32 cur_mpctl, u32 f_ref, unsigned long freq, int relation)
{
        u32 mfi;
        u32 mfn;
        u32 mfd;
        u32 pd;
	unsigned long long ll;
	long l;
	long quot;

	/* Fdppl=2*Fref*(MFI+MFN/(MFD+1))/(PD+1) */
	/*  PD=<0,15>, MFD=<1,1023>, MFI=<5,15> MFN=<0,1022> */

	if (cur_mpctl) {
		mfd = ((cur_mpctl >> 16) & 0x3ff) + 1;
		pd =  ((cur_mpctl >> 26) & 0xf) + 1;
	} else {
		pd=2; mfd=313;
	}

	/* pd=2; mfd=313; mfi=8; mfn=183; */
	/* (MFI+MFN/(MFD)) = Fdppl / (2*Fref) * (PD); */

	quot = (f_ref + (1 << 9)) >> 10;
	l = (freq * pd + quot) / (2 * quot);
	mfi = l >> 10;
	mfn = ((l & ((1 << 10) - 1)) * mfd + (1 << 9)) >> 10;

	mfd -= 1;
	pd -= 1;

	*new_mpctl = ((mfi & 0xf) << 10) | (mfn & 0x3ff) | ((mfd & 0x3ff) << 16)
		| ((pd & 0xf) << 26);

	ll = 2 * (unsigned long long)f_ref * ( (mfi<<16) + (mfn<<16) / (mfd+1) );
	quot = (pd+1) * (1<<16);
	ll += quot / 2;
	do_div(ll, quot);
	freq = ll;

	pr_debug(KERN_DEBUG "imx: new PLL parameters pd=%d mfd=%d mfi=%d mfn=%d, freq=%ld\n",
		pd, mfd, mfi, mfn, freq);

	return freq;
}


static int imx_verify_speed(struct cpufreq_policy *policy)
{
	if (policy->cpu != 0)
		return -EINVAL;

	cpufreq_verify_within_limits(policy, policy->cpuinfo.min_freq, policy->cpuinfo.max_freq);

	return 0;
}

static unsigned int imx_get_speed(unsigned int cpu)
{
	unsigned int freq;
	unsigned int cr;
	unsigned int cscr;
	unsigned int bclk_div;

	if (cpu)
		return 0;

	cscr = CSCR;
	bclk_div = __mfld2val(CSCR_BCLK_DIV, cscr) + 1;
	cr = get_cr();

	if((cr & CR_920T_CLOCK_MODE) == CR_920T_FASTBUS_MODE) {
		freq = clk_get_rate(system_clk);
		freq = (freq + bclk_div/2) / bclk_div;
	} else {
		freq = clk_get_rate(mcu_clk);
		if (cscr & CSCR_MPU_PRESC)
			freq /= 2;
	}

	freq = (freq + 500) / 1000;

	return freq;
}

static int imx_set_target(struct cpufreq_policy *policy,
			  unsigned int target_freq,
			  unsigned int relation)
{
	struct cpufreq_freqs freqs;
	u32 mpctl0 = 0;
	u32 cscr;
	unsigned long flags;
	long freq;
	long sysclk;
	unsigned int bclk_div = bclk_div_at_boot;

	/*
	 * Some governors do not respects CPU and policy lower limits
	 * which leads to bad things (division by zero etc), ensure
	 * that such things do not happen.
	 */
	if(target_freq < policy->cpuinfo.min_freq)
		target_freq = policy->cpuinfo.min_freq;

	if(target_freq < policy->min)
		target_freq = policy->min;

	freq = target_freq * 1000;

	pr_debug(KERN_DEBUG "imx: requested frequency %ld Hz, mpctl0 at boot 0x%08x\n",
			freq, mpctl0_at_boot);

	sysclk = clk_get_rate(system_clk);

	if (freq > sysclk / bclk_div_at_boot + 1000000) {
		freq = imx_compute_mpctl(&mpctl0, mpctl0_at_boot, CLK32 * 512, freq, relation);
		if (freq < 0) {
			printk(KERN_WARNING "imx: target frequency %ld Hz cannot be set\n", freq);
			return -EINVAL;
		}
	} else {
		if(freq + 1000 < sysclk) {
			if (relation == CPUFREQ_RELATION_L)
				bclk_div = (sysclk - 1000) / freq;
			else
				bclk_div = (sysclk + freq + 1000) / freq;

			if(bclk_div > 16)
				bclk_div = 16;
			if(bclk_div < bclk_div_at_boot)
				bclk_div = bclk_div_at_boot;
		}
		freq = (sysclk + bclk_div / 2) / bclk_div;
	}

	freqs.old = imx_get_speed(0);
	freqs.new = (freq + 500) / 1000;
	freqs.cpu = 0;
	freqs.flags = 0;

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	local_irq_save(flags);

	imx_set_fastbus_mode();

	imx_set_mpctl0(mpctl0);

	cscr = CSCR;
	cscr &= ~CSCR_BCLK_DIV;
	cscr |= __val2mfld(CSCR_BCLK_DIV, bclk_div - 1);
	CSCR = cscr;

	if(mpctl0) {
		CSCR |= CSCR_MPLL_RESTART;

		/* Wait until MPLL is stabilized */
		while( CSCR & CSCR_MPLL_RESTART );

		imx_set_async_mode();
	}

	local_irq_restore(flags);

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	pr_debug(KERN_INFO "imx: set frequency %ld Hz, running from %s\n",
			freq, mpctl0? "MPLL": "SPLL");

	return 0;
}

static int __init imx_cpufreq_driver_init(struct cpufreq_policy *policy)
{
	printk(KERN_INFO "i.MX cpu freq change driver v1.0\n");

	if (policy->cpu != 0)
		return -EINVAL;

	policy->cur = policy->min = policy->max = imx_get_speed(0);
	policy->cpuinfo.min_freq = 8000;
	policy->cpuinfo.max_freq = 200000;
	 /* Manual states, that PLL stabilizes in two CLK32 periods */
	policy->cpuinfo.transition_latency = 4 * 1000000000LL / CLK32;
	return 0;
}

static struct cpufreq_driver imx_driver = {
	.flags		= CPUFREQ_STICKY,
	.verify		= imx_verify_speed,
	.target		= imx_set_target,
	.get		= imx_get_speed,
	.init		= imx_cpufreq_driver_init,
	.name		= "imx",
};

static int __init imx_cpufreq_init(void)
{
	bclk_div_at_boot = __mfld2val(CSCR_BCLK_DIV, CSCR) + 1;
	mpctl0_at_boot = 0;

	system_clk = clk_get(NULL, "system_clk");
	if (IS_ERR(system_clk))
		return PTR_ERR(system_clk);

	mcu_clk = clk_get(NULL, "mcu_clk");
	if (IS_ERR(mcu_clk)) {
		clk_put(system_clk);
		return PTR_ERR(mcu_clk);
	}

	if((CSCR & CSCR_MPEN) &&
	   ((get_cr() & CR_920T_CLOCK_MODE) != CR_920T_FASTBUS_MODE))
		mpctl0_at_boot = MPCTL0;

	return cpufreq_register_driver(&imx_driver);
}

arch_initcall(imx_cpufreq_init);

