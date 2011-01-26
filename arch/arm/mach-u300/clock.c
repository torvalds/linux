/*
 *
 * arch/arm/mach-u300/clock.c
 *
 *
 * Copyright (C) 2007-2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * Define clocks in the app platform.
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com>
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/io.h>
#include <linux/seq_file.h>
#include <linux/clkdev.h>

#include <mach/hardware.h>
#include <mach/syscon.h>

#include "clock.h"

/*
 * TODO:
 * - move all handling of the CCR register into this file and create
 *   a spinlock for the CCR register
 * - switch to the clkdevice lookup mechanism that maps clocks to
 *   device ID:s instead when it becomes available in kernel 2.6.29.
 * - implement rate get/set for all clocks that need it.
 */

/*
 * Syscon clock I/O registers lock so clock requests don't collide
 * NOTE: this is a local lock only used to lock access to clock and
 * reset registers in syscon.
 */
static DEFINE_SPINLOCK(syscon_clkreg_lock);
static DEFINE_SPINLOCK(syscon_resetreg_lock);

/*
 * The clocking hierarchy currently looks like this.
 * NOTE: the idea is NOT to show how the clocks are routed on the chip!
 * The ideas is to show dependencies, so a clock higher up in the
 * hierarchy has to be on in order for another clock to be on. Now,
 * both CPU and DMA can actually be on top of the hierarchy, and that
 * is not modeled currently. Instead we have the backbone AMBA bus on
 * top. This bus cannot be programmed in any way but conceptually it
 * needs to be active for the bridges and devices to transport data.
 *
 * Please be aware that a few clocks are hw controlled, which mean that
 * the hw itself can turn on/off or change the rate of the clock when
 * needed!
 *
 *  AMBA bus
 *  |
 *  +- CPU
 *  +- FSMC NANDIF NAND Flash interface
 *  +- SEMI Shared Memory interface
 *  +- ISP Image Signal Processor (U335 only)
 *  +- CDS (U335 only)
 *  +- DMA Direct Memory Access Controller
 *  +- AAIF APP/ACC Inteface (Mobile Scalable Link, MSL)
 *  +- APEX
 *  +- VIDEO_ENC AVE2/3 Video Encoder
 *  +- XGAM Graphics Accelerator Controller
 *  +- AHB
 *  |
 *  +- ahb:0 AHB Bridge
 *  |  |
 *  |  +- ahb:1 INTCON Interrupt controller
 *  |  +- ahb:3 MSPRO  Memory Stick Pro controller
 *  |  +- ahb:4 EMIF   External Memory interface
 *  |
 *  +- fast:0 FAST bridge
 *  |  |
 *  |  +- fast:1 MMCSD MMC/SD card reader controller
 *  |  +- fast:2 I2S0  PCM I2S channel 0 controller
 *  |  +- fast:3 I2S1  PCM I2S channel 1 controller
 *  |  +- fast:4 I2C0  I2C channel 0 controller
 *  |  +- fast:5 I2C1  I2C channel 1 controller
 *  |  +- fast:6 SPI   SPI controller
 *  |  +- fast:7 UART1 Secondary UART (U335 only)
 *  |
 *  +- slow:0 SLOW bridge
 *     |
 *     +- slow:1 SYSCON (not possible to control)
 *     +- slow:2 WDOG Watchdog
 *     +- slow:3 UART0 primary UART
 *     +- slow:4 TIMER_APP Application timer - used in Linux
 *     +- slow:5 KEYPAD controller
 *     +- slow:6 GPIO controller
 *     +- slow:7 RTC controller
 *     +- slow:8 BT Bus Tracer (not used currently)
 *     +- slow:9 EH Event Handler (not used currently)
 *     +- slow:a TIMER_ACC Access style timer (not used currently)
 *     +- slow:b PPM (U335 only, what is that?)
 */

/*
 * Reset control functions. We remember if a block has been
 * taken out of reset and don't remove the reset assertion again
 * and vice versa. Currently we only remove resets so the
 * enablement function is defined out.
 */
static void syscon_block_reset_enable(struct clk *clk)
{
	u16 val;
	unsigned long iflags;

	/* Not all blocks support resetting */
	if (!clk->res_reg || !clk->res_mask)
		return;
	spin_lock_irqsave(&syscon_resetreg_lock, iflags);
	val = readw(clk->res_reg);
	val |= clk->res_mask;
	writew(val, clk->res_reg);
	spin_unlock_irqrestore(&syscon_resetreg_lock, iflags);
	clk->reset = true;
}

static void syscon_block_reset_disable(struct clk *clk)
{
	u16 val;
	unsigned long iflags;

	/* Not all blocks support resetting */
	if (!clk->res_reg || !clk->res_mask)
		return;
	spin_lock_irqsave(&syscon_resetreg_lock, iflags);
	val = readw(clk->res_reg);
	val &= ~clk->res_mask;
	writew(val, clk->res_reg);
	spin_unlock_irqrestore(&syscon_resetreg_lock, iflags);
	clk->reset = false;
}

int __clk_get(struct clk *clk)
{
	u16 val;

	/* The MMC and MSPRO clocks need some special set-up */
	if (!strcmp(clk->name, "MCLK")) {
		/* Set default MMC clock divisor to 18.9 MHz */
		writew(0x0054U, U300_SYSCON_VBASE + U300_SYSCON_MMF0R);
		val = readw(U300_SYSCON_VBASE + U300_SYSCON_MMCR);
		/* Disable the MMC feedback clock */
		val &= ~U300_SYSCON_MMCR_MMC_FB_CLK_SEL_ENABLE;
		/* Disable MSPRO frequency */
		val &= ~U300_SYSCON_MMCR_MSPRO_FREQSEL_ENABLE;
		writew(val, U300_SYSCON_VBASE + U300_SYSCON_MMCR);
	}
	if (!strcmp(clk->name, "MSPRO")) {
		val = readw(U300_SYSCON_VBASE + U300_SYSCON_MMCR);
		/* Disable the MMC feedback clock */
		val &= ~U300_SYSCON_MMCR_MMC_FB_CLK_SEL_ENABLE;
		/* Enable MSPRO frequency */
		val |= U300_SYSCON_MMCR_MSPRO_FREQSEL_ENABLE;
		writew(val, U300_SYSCON_VBASE + U300_SYSCON_MMCR);
	}
	return 1;
}
EXPORT_SYMBOL(__clk_get);

void __clk_put(struct clk *clk)
{
}
EXPORT_SYMBOL(__clk_put);

static void syscon_clk_disable(struct clk *clk)
{
	unsigned long iflags;

	/* Don't touch the hardware controlled clocks */
	if (clk->hw_ctrld)
		return;

	spin_lock_irqsave(&syscon_clkreg_lock, iflags);
	writew(clk->clk_val, U300_SYSCON_VBASE + U300_SYSCON_SBCDR);
	spin_unlock_irqrestore(&syscon_clkreg_lock, iflags);
}

static void syscon_clk_enable(struct clk *clk)
{
	unsigned long iflags;

	/* Don't touch the hardware controlled clocks */
	if (clk->hw_ctrld)
		return;

	spin_lock_irqsave(&syscon_clkreg_lock, iflags);
	writew(clk->clk_val, U300_SYSCON_VBASE + U300_SYSCON_SBCER);
	spin_unlock_irqrestore(&syscon_clkreg_lock, iflags);
}

static u16 syscon_clk_get_rate(void)
{
	u16 val;
	unsigned long iflags;

	spin_lock_irqsave(&syscon_clkreg_lock, iflags);
	val = readw(U300_SYSCON_VBASE + U300_SYSCON_CCR);
	val &= U300_SYSCON_CCR_CLKING_PERFORMANCE_MASK;
	spin_unlock_irqrestore(&syscon_clkreg_lock, iflags);
	return val;
}

#ifdef CONFIG_MACH_U300_USE_I2S_AS_MASTER
static void enable_i2s0_vcxo(void)
{
	u16 val;
	unsigned long iflags;

	spin_lock_irqsave(&syscon_clkreg_lock, iflags);
	/* Set I2S0 to use the VCXO 26 MHz clock */
	val = readw(U300_SYSCON_VBASE + U300_SYSCON_CCR);
	val |= U300_SYSCON_CCR_TURN_VCXO_ON;
	writew(val, U300_SYSCON_VBASE + U300_SYSCON_CCR);
	val |= U300_SYSCON_CCR_I2S0_USE_VCXO;
	writew(val, U300_SYSCON_VBASE + U300_SYSCON_CCR);
	val = readw(U300_SYSCON_VBASE + U300_SYSCON_CEFR);
	val |= U300_SYSCON_CEFR_I2S0_CLK_EN;
	writew(val, U300_SYSCON_VBASE + U300_SYSCON_CEFR);
	spin_unlock_irqrestore(&syscon_clkreg_lock, iflags);
}

static void enable_i2s1_vcxo(void)
{
	u16 val;
	unsigned long iflags;

	spin_lock_irqsave(&syscon_clkreg_lock, iflags);
	/* Set I2S1 to use the VCXO 26 MHz clock */
	val = readw(U300_SYSCON_VBASE + U300_SYSCON_CCR);
	val |= U300_SYSCON_CCR_TURN_VCXO_ON;
	writew(val, U300_SYSCON_VBASE + U300_SYSCON_CCR);
	val |= U300_SYSCON_CCR_I2S1_USE_VCXO;
	writew(val, U300_SYSCON_VBASE + U300_SYSCON_CCR);
	val = readw(U300_SYSCON_VBASE + U300_SYSCON_CEFR);
	val |= U300_SYSCON_CEFR_I2S1_CLK_EN;
	writew(val, U300_SYSCON_VBASE + U300_SYSCON_CEFR);
	spin_unlock_irqrestore(&syscon_clkreg_lock, iflags);
}

static void disable_i2s0_vcxo(void)
{
	u16 val;
	unsigned long iflags;

	spin_lock_irqsave(&syscon_clkreg_lock, iflags);
	/* Disable I2S0 use of the VCXO 26 MHz clock */
	val = readw(U300_SYSCON_VBASE + U300_SYSCON_CCR);
	val &= ~U300_SYSCON_CCR_I2S0_USE_VCXO;
	writew(val, U300_SYSCON_VBASE + U300_SYSCON_CCR);
	/* Deactivate VCXO if noone else is using VCXO */
	if (!(val & U300_SYSCON_CCR_I2S1_USE_VCXO))
		val &= ~U300_SYSCON_CCR_TURN_VCXO_ON;
	writew(val, U300_SYSCON_VBASE + U300_SYSCON_CCR);
	val = readw(U300_SYSCON_VBASE + U300_SYSCON_CEFR);
	val &= ~U300_SYSCON_CEFR_I2S0_CLK_EN;
	writew(val, U300_SYSCON_VBASE + U300_SYSCON_CEFR);
	spin_unlock_irqrestore(&syscon_clkreg_lock, iflags);
}

static void disable_i2s1_vcxo(void)
{
	u16 val;
	unsigned long iflags;

	spin_lock_irqsave(&syscon_clkreg_lock, iflags);
	/* Disable I2S1 use of the VCXO 26 MHz clock */
	val = readw(U300_SYSCON_VBASE + U300_SYSCON_CCR);
	val &= ~U300_SYSCON_CCR_I2S1_USE_VCXO;
	writew(val, U300_SYSCON_VBASE + U300_SYSCON_CCR);
	/* Deactivate VCXO if noone else is using VCXO */
	if (!(val & U300_SYSCON_CCR_I2S0_USE_VCXO))
		val &= ~U300_SYSCON_CCR_TURN_VCXO_ON;
	writew(val, U300_SYSCON_VBASE + U300_SYSCON_CCR);
	val = readw(U300_SYSCON_VBASE + U300_SYSCON_CEFR);
	val &= ~U300_SYSCON_CEFR_I2S0_CLK_EN;
	writew(val, U300_SYSCON_VBASE + U300_SYSCON_CEFR);
	spin_unlock_irqrestore(&syscon_clkreg_lock, iflags);
}
#endif /* CONFIG_MACH_U300_USE_I2S_AS_MASTER */


static void syscon_clk_rate_set_mclk(unsigned long rate)
{
	u16 val;
	u32 reg;
	unsigned long iflags;

	switch (rate) {
	case 18900000:
		val = 0x0054;
		break;
	case 20800000:
		val = 0x0044;
		break;
	case 23100000:
		val = 0x0043;
		break;
	case 26000000:
		val = 0x0033;
		break;
	case 29700000:
		val = 0x0032;
		break;
	case 34700000:
		val = 0x0022;
		break;
	case 41600000:
		val = 0x0021;
		break;
	case 52000000:
		val = 0x0011;
		break;
	case 104000000:
		val = 0x0000;
		break;
	default:
		printk(KERN_ERR "Trying to set MCLK to unknown speed! %ld\n",
		       rate);
		return;
	}

	spin_lock_irqsave(&syscon_clkreg_lock, iflags);
	reg = readw(U300_SYSCON_VBASE + U300_SYSCON_MMF0R) &
		~U300_SYSCON_MMF0R_MASK;
	writew(reg | val, U300_SYSCON_VBASE + U300_SYSCON_MMF0R);
	spin_unlock_irqrestore(&syscon_clkreg_lock, iflags);
}

void syscon_clk_rate_set_cpuclk(unsigned long rate)
{
	u16 val;
	unsigned long iflags;

	switch (rate) {
	case 13000000:
		val = U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER;
		break;
	case 52000000:
		val = U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE;
		break;
	case 104000000:
		val = U300_SYSCON_CCR_CLKING_PERFORMANCE_HIGH;
		break;
	case 208000000:
		val = U300_SYSCON_CCR_CLKING_PERFORMANCE_BEST;
		break;
	default:
		return;
	}
	spin_lock_irqsave(&syscon_clkreg_lock, iflags);
	val |= readw(U300_SYSCON_VBASE + U300_SYSCON_CCR) &
		~U300_SYSCON_CCR_CLKING_PERFORMANCE_MASK ;
	writew(val, U300_SYSCON_VBASE + U300_SYSCON_CCR);
	spin_unlock_irqrestore(&syscon_clkreg_lock, iflags);
}
EXPORT_SYMBOL(syscon_clk_rate_set_cpuclk);

void clk_disable(struct clk *clk)
{
	unsigned long iflags;

	spin_lock_irqsave(&clk->lock, iflags);
	if (clk->usecount > 0 && !(--clk->usecount)) {
		/* some blocks lack clocking registers and cannot be disabled */
		if (clk->disable)
			clk->disable(clk);
		if (likely((u32)clk->parent))
			clk_disable(clk->parent);
	}
#ifdef CONFIG_MACH_U300_USE_I2S_AS_MASTER
	if (unlikely(!strcmp(clk->name, "I2S0")))
		disable_i2s0_vcxo();
	if (unlikely(!strcmp(clk->name, "I2S1")))
		disable_i2s1_vcxo();
#endif
	spin_unlock_irqrestore(&clk->lock, iflags);
}
EXPORT_SYMBOL(clk_disable);

int clk_enable(struct clk *clk)
{
	int ret = 0;
	unsigned long iflags;

	spin_lock_irqsave(&clk->lock, iflags);
	if (clk->usecount++ == 0) {
		if (likely((u32)clk->parent))
			ret = clk_enable(clk->parent);

		if (unlikely(ret != 0))
			clk->usecount--;
		else {
			/* remove reset line (we never enable reset again) */
			syscon_block_reset_disable(clk);
			/* clocks without enable function are always on */
			if (clk->enable)
				clk->enable(clk);
#ifdef CONFIG_MACH_U300_USE_I2S_AS_MASTER
			if (unlikely(!strcmp(clk->name, "I2S0")))
				enable_i2s0_vcxo();
			if (unlikely(!strcmp(clk->name, "I2S1")))
				enable_i2s1_vcxo();
#endif
		}
	}
	spin_unlock_irqrestore(&clk->lock, iflags);
	return ret;

}
EXPORT_SYMBOL(clk_enable);

/* Returns the clock rate in Hz */
static unsigned long clk_get_rate_cpuclk(struct clk *clk)
{
	u16 val;

	val = syscon_clk_get_rate();

	switch (val) {
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW:
		return 13000000;
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE:
		return 52000000;
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_HIGH:
		return 104000000;
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_BEST:
		return 208000000;
	default:
		break;
	}
	return clk->rate;
}

static unsigned long clk_get_rate_ahb_clk(struct clk *clk)
{
	u16 val;

	val = syscon_clk_get_rate();

	switch (val) {
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW:
		return 6500000;
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE:
		return 26000000;
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_HIGH:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_BEST:
		return 52000000;
	default:
		break;
	}
	return clk->rate;

}

static unsigned long clk_get_rate_emif_clk(struct clk *clk)
{
	u16 val;

	val = syscon_clk_get_rate();

	switch (val) {
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW:
		return 13000000;
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE:
		return 52000000;
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_HIGH:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_BEST:
		return 104000000;
	default:
		break;
	}
	return clk->rate;

}

static unsigned long clk_get_rate_xgamclk(struct clk *clk)
{
	u16 val;

	val = syscon_clk_get_rate();

	switch (val) {
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW:
		return 6500000;
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE:
		return 26000000;
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_HIGH:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_BEST:
		return 52000000;
	default:
		break;
	}

	return clk->rate;
}

static unsigned long clk_get_rate_mclk(struct clk *clk)
{
	u16 val;

	val = syscon_clk_get_rate();

	switch (val) {
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER:
		/*
		 * Here, the 208 MHz PLL gets shut down and the always
		 * on 13 MHz PLL used for RTC etc kicks into use
		 * instead.
		 */
		return 13000000;
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_HIGH:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_BEST:
	{
		/*
		 * This clock is under program control. The register is
		 * divided in two nybbles, bit 7-4 gives cycles-1 to count
		 * high, bit 3-0 gives cycles-1 to count low. Distribute
		 * these with no more than 1 cycle difference between
		 * low and high and add low and high to get the actual
		 * divisor. The base PLL is 208 MHz. Writing 0x00 will
		 * divide by 1 and 1 so the highest frequency possible
		 * is 104 MHz.
		 *
		 * e.g. 0x54 =>
		 * f = 208 / ((5+1) + (4+1)) = 208 / 11 = 18.9 MHz
		 */
		u16 val = readw(U300_SYSCON_VBASE + U300_SYSCON_MMF0R) &
			U300_SYSCON_MMF0R_MASK;
		switch (val) {
		case 0x0054:
			return 18900000;
		case 0x0044:
			return 20800000;
		case 0x0043:
			return 23100000;
		case 0x0033:
			return 26000000;
		case 0x0032:
			return 29700000;
		case 0x0022:
			return 34700000;
		case 0x0021:
			return 41600000;
		case 0x0011:
			return 52000000;
		case 0x0000:
			return 104000000;
		default:
			break;
		}
	}
	default:
		break;
	}

	return clk->rate;
}

static unsigned long clk_get_rate_i2s_i2c_spi(struct clk *clk)
{
	u16 val;

	val = syscon_clk_get_rate();

	switch (val) {
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW:
		return 13000000;
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_HIGH:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_BEST:
		return 26000000;
	default:
		break;
	}

	return clk->rate;
}

unsigned long clk_get_rate(struct clk *clk)
{
	if (clk->get_rate)
		return clk->get_rate(clk);
	else
		return clk->rate;
}
EXPORT_SYMBOL(clk_get_rate);

static unsigned long clk_round_rate_mclk(struct clk *clk, unsigned long rate)
{
	if (rate <= 18900000)
		return 18900000;
	if (rate <= 20800000)
		return 20800000;
	if (rate <= 23100000)
		return 23100000;
	if (rate <= 26000000)
		return 26000000;
	if (rate <= 29700000)
		return 29700000;
	if (rate <= 34700000)
		return 34700000;
	if (rate <= 41600000)
		return 41600000;
	if (rate <= 52000000)
		return 52000000;
	return -EINVAL;
}

static unsigned long clk_round_rate_cpuclk(struct clk *clk, unsigned long rate)
{
	if (rate <= 13000000)
		return 13000000;
	if (rate <= 52000000)
		return 52000000;
	if (rate <= 104000000)
		return 104000000;
	if (rate <= 208000000)
		return 208000000;
	return -EINVAL;
}

/*
 * This adjusts a requested rate to the closest exact rate
 * a certain clock can provide. For a fixed clock it's
 * mostly clk->rate.
 */
long clk_round_rate(struct clk *clk, unsigned long rate)
{
	/* TODO: get apropriate switches for EMIFCLK, AHBCLK and MCLK */
	/* Else default to fixed value */

	if (clk->round_rate) {
		return (long) clk->round_rate(clk, rate);
	} else {
		printk(KERN_ERR "clock: Failed to round rate of %s\n",
		       clk->name);
	}
	return (long) clk->rate;
}
EXPORT_SYMBOL(clk_round_rate);

static int clk_set_rate_mclk(struct clk *clk, unsigned long rate)
{
	syscon_clk_rate_set_mclk(clk_round_rate(clk, rate));
	return 0;
}

static int clk_set_rate_cpuclk(struct clk *clk, unsigned long rate)
{
	syscon_clk_rate_set_cpuclk(clk_round_rate(clk, rate));
	return 0;
}

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	/* TODO: set for EMIFCLK and AHBCLK */
	/* Else assume the clock is fixed and fail */
	if (clk->set_rate) {
		return clk->set_rate(clk, rate);
	} else {
		printk(KERN_ERR "clock: Failed to set %s to %ld hz\n",
		       clk->name, rate);
		return -EINVAL;
	}
}
EXPORT_SYMBOL(clk_set_rate);

/*
 * Clock definitions. The clock parents are set to respective
 * bridge and the clock framework makes sure that the clocks have
 * parents activated and are brought out of reset when in use.
 *
 * Clocks that have hw_ctrld = true are hw controlled, and the hw
 * can by itself turn these clocks on and off.
 * So in other words, we don't really have to care about them.
 */

static struct clk amba_clk = {
	.name	    = "AMBA",
	.rate	    = 52000000, /* this varies! */
	.hw_ctrld   = true,
	.reset	    = false,
	.lock       = __SPIN_LOCK_UNLOCKED(amba_clk.lock),
};

/*
 * These blocks are connected directly to the AMBA bus
 * with no bridge.
 */

static struct clk cpu_clk = {
	.name	    = "CPU",
	.parent	    = &amba_clk,
	.rate	    = 208000000, /* this varies! */
	.hw_ctrld   = true,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RRR,
	.res_mask   = U300_SYSCON_RRR_CPU_RESET_EN,
	.set_rate   = clk_set_rate_cpuclk,
	.get_rate   = clk_get_rate_cpuclk,
	.round_rate = clk_round_rate_cpuclk,
	.lock       = __SPIN_LOCK_UNLOCKED(cpu_clk.lock),
};

static struct clk nandif_clk = {
	.name       = "FSMC",
	.parent	    = &amba_clk,
	.hw_ctrld   = false,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RRR,
	.res_mask   = U300_SYSCON_RRR_NANDIF_RESET_EN,
	.clk_val    = U300_SYSCON_SBCER_NANDIF_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.lock       = __SPIN_LOCK_UNLOCKED(nandif_clk.lock),
};

static struct clk semi_clk = {
	.name       = "SEMI",
	.parent	    = &amba_clk,
	.rate       = 0, /* FIXME */
	/* It is not possible to reset SEMI */
	.hw_ctrld   = false,
	.reset	    = false,
	.clk_val    = U300_SYSCON_SBCER_SEMI_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.lock       = __SPIN_LOCK_UNLOCKED(semi_clk.lock),
};

#ifdef CONFIG_MACH_U300_BS335
static struct clk isp_clk = {
	.name	    = "ISP",
	.parent	    = &amba_clk,
	.rate	    = 0, /* FIXME */
	.hw_ctrld   = false,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RRR,
	.res_mask   = U300_SYSCON_RRR_ISP_RESET_EN,
	.clk_val    = U300_SYSCON_SBCER_ISP_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.lock       = __SPIN_LOCK_UNLOCKED(isp_clk.lock),
};

static struct clk cds_clk = {
	.name	    = "CDS",
	.parent	    = &amba_clk,
	.rate	    = 0, /* FIXME */
	.hw_ctrld   = false,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RRR,
	.res_mask   = U300_SYSCON_RRR_CDS_RESET_EN,
	.clk_val    = U300_SYSCON_SBCER_CDS_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.lock       = __SPIN_LOCK_UNLOCKED(cds_clk.lock),
};
#endif

static struct clk dma_clk = {
	.name       = "DMA",
	.parent	    = &amba_clk,
	.rate       = 52000000, /* this varies! */
	.hw_ctrld   = true,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RRR,
	.res_mask   = U300_SYSCON_RRR_DMAC_RESET_EN,
	.clk_val    = U300_SYSCON_SBCER_DMAC_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.lock       = __SPIN_LOCK_UNLOCKED(dma_clk.lock),
};

static struct clk aaif_clk = {
	.name       = "AAIF",
	.parent	    = &amba_clk,
	.rate       = 52000000, /* this varies! */
	.hw_ctrld   = true,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RRR,
	.res_mask   = U300_SYSCON_RRR_AAIF_RESET_EN,
	.clk_val    = U300_SYSCON_SBCER_AAIF_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.lock       = __SPIN_LOCK_UNLOCKED(aaif_clk.lock),
};

static struct clk apex_clk = {
	.name       = "APEX",
	.parent	    = &amba_clk,
	.rate       = 0, /* FIXME */
	.hw_ctrld   = true,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RRR,
	.res_mask   = U300_SYSCON_RRR_APEX_RESET_EN,
	.clk_val    = U300_SYSCON_SBCER_APEX_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.lock       = __SPIN_LOCK_UNLOCKED(apex_clk.lock),
};

static struct clk video_enc_clk = {
	.name       = "VIDEO_ENC",
	.parent	    = &amba_clk,
	.rate       = 208000000, /* this varies! */
	.hw_ctrld   = false,
	.reset	    = false,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RRR,
	/* This has XGAM in the name but refers to the video encoder */
	.res_mask   = U300_SYSCON_RRR_XGAM_VC_SYNC_RESET_EN,
	.clk_val    = U300_SYSCON_SBCER_VIDEO_ENC_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.lock       = __SPIN_LOCK_UNLOCKED(video_enc_clk.lock),
};

static struct clk xgam_clk = {
	.name       = "XGAMCLK",
	.parent	    = &amba_clk,
	.rate       = 52000000, /* this varies! */
	.hw_ctrld   = false,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RRR,
	.res_mask   = U300_SYSCON_RRR_XGAM_RESET_EN,
	.clk_val    = U300_SYSCON_SBCER_XGAM_CLK_EN,
	.get_rate   = clk_get_rate_xgamclk,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.lock       = __SPIN_LOCK_UNLOCKED(xgam_clk.lock),
};

/* This clock is used to activate the video encoder */
static struct clk ahb_clk = {
	.name	    = "AHB",
	.parent	    = &amba_clk,
	.rate	    = 52000000, /* this varies! */
	.hw_ctrld   = false, /* This one is set to false due to HW bug */
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RRR,
	.res_mask   = U300_SYSCON_RRR_AHB_RESET_EN,
	.clk_val    = U300_SYSCON_SBCER_AHB_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.get_rate   = clk_get_rate_ahb_clk,
	.lock       = __SPIN_LOCK_UNLOCKED(ahb_clk.lock),
};


/*
 * Clocks on the AHB bridge
 */

static struct clk ahb_subsys_clk = {
	.name	    = "AHB_SUBSYS",
	.parent	    = &amba_clk,
	.rate	    = 52000000, /* this varies! */
	.hw_ctrld   = true,
	.reset	    = false,
	.clk_val    = U300_SYSCON_SBCER_AHB_SUBSYS_BRIDGE_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.get_rate   = clk_get_rate_ahb_clk,
	.lock       = __SPIN_LOCK_UNLOCKED(ahb_subsys_clk.lock),
};

static struct clk intcon_clk = {
	.name	    = "INTCON",
	.parent	    = &ahb_subsys_clk,
	.rate	    = 52000000, /* this varies! */
	.hw_ctrld   = false,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RRR,
	.res_mask   = U300_SYSCON_RRR_INTCON_RESET_EN,
	/* INTCON can be reset but not clock-gated */
	.lock       = __SPIN_LOCK_UNLOCKED(intcon_clk.lock),

};

static struct clk mspro_clk = {
	.name       = "MSPRO",
	.parent	    = &ahb_subsys_clk,
	.rate       = 0, /* FIXME */
	.hw_ctrld   = false,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RRR,
	.res_mask   = U300_SYSCON_RRR_MSPRO_RESET_EN,
	.clk_val    = U300_SYSCON_SBCER_MSPRO_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.lock       = __SPIN_LOCK_UNLOCKED(mspro_clk.lock),
};

static struct clk emif_clk = {
	.name	    = "EMIF",
	.parent	    = &ahb_subsys_clk,
	.rate	    = 104000000, /* this varies! */
	.hw_ctrld   = false,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RRR,
	.res_mask   = U300_SYSCON_RRR_EMIF_RESET_EN,
	.clk_val    = U300_SYSCON_SBCER_EMIF_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.get_rate   = clk_get_rate_emif_clk,
	.lock       = __SPIN_LOCK_UNLOCKED(emif_clk.lock),
};


/*
 * Clocks on the FAST bridge
 */
static struct clk fast_clk = {
	.name	    = "FAST_BRIDGE",
	.parent	    = &amba_clk,
	.rate	    = 13000000, /* this varies! */
	.hw_ctrld   = true,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RFR,
	.res_mask   = U300_SYSCON_RFR_FAST_BRIDGE_RESET_ENABLE,
	.clk_val    = U300_SYSCON_SBCER_FAST_BRIDGE_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.lock       = __SPIN_LOCK_UNLOCKED(fast_clk.lock),
};

/*
 * The MMCI apb_pclk is hardwired to the same terminal as the
 * external MCI clock. Thus this will be referenced twice.
 */
static struct clk mmcsd_clk = {
	.name       = "MCLK",
	.parent	    = &fast_clk,
	.rate       = 18900000, /* this varies! */
	.hw_ctrld   = false,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RFR,
	.res_mask   = U300_SYSCON_RFR_MMC_RESET_ENABLE,
	.clk_val    = U300_SYSCON_SBCER_MMC_CLK_EN,
	.get_rate   = clk_get_rate_mclk,
	.set_rate   = clk_set_rate_mclk,
	.round_rate = clk_round_rate_mclk,
	.disable    = syscon_clk_disable,
	.enable     = syscon_clk_enable,
	.lock       = __SPIN_LOCK_UNLOCKED(mmcsd_clk.lock),
};

static struct clk i2s0_clk = {
	.name       = "i2s0",
	.parent	    = &fast_clk,
	.rate       = 26000000, /* this varies! */
	.hw_ctrld   = true,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RFR,
	.res_mask   = U300_SYSCON_RFR_PCM_I2S0_RESET_ENABLE,
	.clk_val    = U300_SYSCON_SBCER_I2S0_CORE_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.get_rate   = clk_get_rate_i2s_i2c_spi,
	.lock       = __SPIN_LOCK_UNLOCKED(i2s0_clk.lock),
};

static struct clk i2s1_clk = {
	.name       = "i2s1",
	.parent	    = &fast_clk,
	.rate       = 26000000, /* this varies! */
	.hw_ctrld   = true,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RFR,
	.res_mask   = U300_SYSCON_RFR_PCM_I2S1_RESET_ENABLE,
	.clk_val    = U300_SYSCON_SBCER_I2S1_CORE_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.get_rate   = clk_get_rate_i2s_i2c_spi,
	.lock       = __SPIN_LOCK_UNLOCKED(i2s1_clk.lock),
};

static struct clk i2c0_clk = {
	.name       = "I2C0",
	.parent	    = &fast_clk,
	.rate       = 26000000, /* this varies! */
	.hw_ctrld   = false,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RFR,
	.res_mask   = U300_SYSCON_RFR_I2C0_RESET_ENABLE,
	.clk_val    = U300_SYSCON_SBCER_I2C0_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.get_rate   = clk_get_rate_i2s_i2c_spi,
	.lock       = __SPIN_LOCK_UNLOCKED(i2c0_clk.lock),
};

static struct clk i2c1_clk = {
	.name       = "I2C1",
	.parent	    = &fast_clk,
	.rate       = 26000000, /* this varies! */
	.hw_ctrld   = false,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RFR,
	.res_mask   = U300_SYSCON_RFR_I2C1_RESET_ENABLE,
	.clk_val    = U300_SYSCON_SBCER_I2C1_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.get_rate   = clk_get_rate_i2s_i2c_spi,
	.lock       = __SPIN_LOCK_UNLOCKED(i2c1_clk.lock),
};

/*
 * The SPI apb_pclk is hardwired to the same terminal as the
 * external SPI clock. Thus this will be referenced twice.
 */
static struct clk spi_clk = {
	.name       = "SPI",
	.parent	    = &fast_clk,
	.rate       = 26000000, /* this varies! */
	.hw_ctrld   = false,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RFR,
	.res_mask   = U300_SYSCON_RFR_SPI_RESET_ENABLE,
	.clk_val    = U300_SYSCON_SBCER_SPI_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.get_rate   = clk_get_rate_i2s_i2c_spi,
	.lock       = __SPIN_LOCK_UNLOCKED(spi_clk.lock),
};

#ifdef CONFIG_MACH_U300_BS335
static struct clk uart1_pclk = {
	.name	    = "UART1_PCLK",
	.parent	    = &fast_clk,
	.hw_ctrld   = false,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RFR,
	.res_mask   = U300_SYSCON_RFR_UART1_RESET_ENABLE,
	.clk_val    = U300_SYSCON_SBCER_UART1_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.lock       = __SPIN_LOCK_UNLOCKED(uart1_pclk.lock),
};

/* This one is hardwired to PLL13 */
static struct clk uart1_clk = {
	.name	    = "UART1_CLK",
	.rate	    = 13000000,
	.hw_ctrld   = true,
	.lock       = __SPIN_LOCK_UNLOCKED(uart1_clk.lock),
};
#endif


/*
 * Clocks on the SLOW bridge
 */
static struct clk slow_clk = {
	.name	    = "SLOW_BRIDGE",
	.parent	    = &amba_clk,
	.rate	    = 13000000,
	.hw_ctrld   = true,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RSR,
	.res_mask   = U300_SYSCON_RSR_SLOW_BRIDGE_RESET_EN,
	.clk_val    = U300_SYSCON_SBCER_SLOW_BRIDGE_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.lock       = __SPIN_LOCK_UNLOCKED(slow_clk.lock),
};

/* TODO: implement SYSCON clock? */

static struct clk wdog_clk = {
	.name	    = "WDOG",
	.parent	    = &slow_clk,
	.hw_ctrld   = false,
	.rate	    = 32768,
	.reset	    = false,
	/* This is always on, cannot be enabled/disabled or reset */
	.lock       = __SPIN_LOCK_UNLOCKED(wdog_clk.lock),
};

static struct clk uart0_pclk = {
	.name	    = "UART0_PCLK",
	.parent	    = &slow_clk,
	.hw_ctrld   = false,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RSR,
	.res_mask   = U300_SYSCON_RSR_UART_RESET_EN,
	.clk_val    = U300_SYSCON_SBCER_UART_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.lock       = __SPIN_LOCK_UNLOCKED(uart0_pclk.lock),
};

/* This one is hardwired to PLL13 */
static struct clk uart0_clk = {
	.name	    = "UART0_CLK",
	.parent	    = &slow_clk,
	.rate	    = 13000000,
	.hw_ctrld   = true,
	.lock       = __SPIN_LOCK_UNLOCKED(uart0_clk.lock),
};

static struct clk keypad_clk = {
	.name       = "KEYPAD",
	.parent	    = &slow_clk,
	.rate       = 32768,
	.hw_ctrld   = false,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RSR,
	.res_mask   = U300_SYSCON_RSR_KEYPAD_RESET_EN,
	.clk_val    = U300_SYSCON_SBCER_KEYPAD_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.lock       = __SPIN_LOCK_UNLOCKED(keypad_clk.lock),
};

static struct clk gpio_clk = {
	.name       = "GPIO",
	.parent	    = &slow_clk,
	.rate       = 13000000,
	.hw_ctrld   = true,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RSR,
	.res_mask   = U300_SYSCON_RSR_GPIO_RESET_EN,
	.clk_val    = U300_SYSCON_SBCER_GPIO_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.lock       = __SPIN_LOCK_UNLOCKED(gpio_clk.lock),
};

static struct clk rtc_clk = {
	.name	    = "RTC",
	.parent	    = &slow_clk,
	.rate	    = 32768,
	.hw_ctrld   = true,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RSR,
	.res_mask   = U300_SYSCON_RSR_RTC_RESET_EN,
	/* This clock is always on, cannot be enabled/disabled */
	.lock       = __SPIN_LOCK_UNLOCKED(rtc_clk.lock),
};

static struct clk bustr_clk = {
	.name       = "BUSTR",
	.parent	    = &slow_clk,
	.rate       = 13000000,
	.hw_ctrld   = true,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RSR,
	.res_mask   = U300_SYSCON_RSR_BTR_RESET_EN,
	.clk_val    = U300_SYSCON_SBCER_BTR_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.lock       = __SPIN_LOCK_UNLOCKED(bustr_clk.lock),
};

static struct clk evhist_clk = {
	.name       = "EVHIST",
	.parent	    = &slow_clk,
	.rate       = 13000000,
	.hw_ctrld   = true,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RSR,
	.res_mask   = U300_SYSCON_RSR_EH_RESET_EN,
	.clk_val    = U300_SYSCON_SBCER_EH_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.lock       = __SPIN_LOCK_UNLOCKED(evhist_clk.lock),
};

static struct clk timer_clk = {
	.name       = "TIMER",
	.parent	    = &slow_clk,
	.rate       = 13000000,
	.hw_ctrld   = true,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RSR,
	.res_mask   = U300_SYSCON_RSR_ACC_TMR_RESET_EN,
	.clk_val    = U300_SYSCON_SBCER_ACC_TMR_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.lock       = __SPIN_LOCK_UNLOCKED(timer_clk.lock),
};

/*
 * There is a binary divider in the hardware that divides
 * the 13MHz PLL by 13 down to 1 MHz.
 */
static struct clk app_timer_clk = {
	.name       = "TIMER_APP",
	.parent	    = &slow_clk,
	.rate       = 1000000,
	.hw_ctrld   = true,
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RSR,
	.res_mask   = U300_SYSCON_RSR_APP_TMR_RESET_EN,
	.clk_val    = U300_SYSCON_SBCER_APP_TMR_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.lock       = __SPIN_LOCK_UNLOCKED(app_timer_clk.lock),
};

#ifdef CONFIG_MACH_U300_BS335
static struct clk ppm_clk = {
	.name	    = "PPM",
	.parent	    = &slow_clk,
	.rate	    = 0, /* FIXME */
	.hw_ctrld   = true, /* TODO: Look up if it is hw ctrld or not */
	.reset	    = true,
	.res_reg    = U300_SYSCON_VBASE + U300_SYSCON_RSR,
	.res_mask   = U300_SYSCON_RSR_PPM_RESET_EN,
	.clk_val    = U300_SYSCON_SBCER_PPM_CLK_EN,
	.enable     = syscon_clk_enable,
	.disable    = syscon_clk_disable,
	.lock       = __SPIN_LOCK_UNLOCKED(ppm_clk.lock),
};
#endif

#define DEF_LOOKUP(devid, clkref)		\
	{					\
	.dev_id = devid,			\
	.clk = clkref,				\
	}

#define DEF_LOOKUP_CON(devid, conid, clkref)	\
	{					\
	.dev_id = devid,			\
	.con_id = conid,			\
	.clk = clkref,				\
	}

/*
 * Here we only define clocks that are meaningful to
 * look up through clockdevice.
 */
static struct clk_lookup lookups[] = {
	/* Connected directly to the AMBA bus */
	DEF_LOOKUP("amba",      &amba_clk),
	DEF_LOOKUP("cpu",       &cpu_clk),
	DEF_LOOKUP("fsmc-nand", &nandif_clk),
	DEF_LOOKUP("semi",      &semi_clk),
#ifdef CONFIG_MACH_U300_BS335
	DEF_LOOKUP("isp",       &isp_clk),
	DEF_LOOKUP("cds",       &cds_clk),
#endif
	DEF_LOOKUP("dma",       &dma_clk),
	DEF_LOOKUP("msl",       &aaif_clk),
	DEF_LOOKUP("apex",      &apex_clk),
	DEF_LOOKUP("video_enc", &video_enc_clk),
	DEF_LOOKUP("xgam",      &xgam_clk),
	DEF_LOOKUP("ahb",       &ahb_clk),
	/* AHB bridge clocks */
	DEF_LOOKUP("ahb_subsys", &ahb_subsys_clk),
	DEF_LOOKUP("intcon",    &intcon_clk),
	DEF_LOOKUP_CON("intcon", "apb_pclk", &intcon_clk),
	DEF_LOOKUP("mspro",     &mspro_clk),
	DEF_LOOKUP("pl172",     &emif_clk),
	DEF_LOOKUP_CON("pl172", "apb_pclk", &emif_clk),
	/* FAST bridge clocks */
	DEF_LOOKUP("fast",      &fast_clk),
	DEF_LOOKUP("mmci",      &mmcsd_clk),
	DEF_LOOKUP_CON("mmci", "apb_pclk", &mmcsd_clk),
	/*
	 * The .0 and .1 identifiers on these comes from the platform device
	 * .id field and are assigned when the platform devices are registered.
	 */
	DEF_LOOKUP("i2s.0",     &i2s0_clk),
	DEF_LOOKUP("i2s.1",     &i2s1_clk),
	DEF_LOOKUP("stu300.0",  &i2c0_clk),
	DEF_LOOKUP("stu300.1",  &i2c1_clk),
	DEF_LOOKUP("pl022",     &spi_clk),
	DEF_LOOKUP_CON("pl022", "apb_pclk", &spi_clk),
#ifdef CONFIG_MACH_U300_BS335
	DEF_LOOKUP("uart1",     &uart1_clk),
	DEF_LOOKUP_CON("uart1", "apb_pclk", &uart1_pclk),
#endif
	/* SLOW bridge clocks */
	DEF_LOOKUP("slow",      &slow_clk),
	DEF_LOOKUP("coh901327_wdog",      &wdog_clk),
	DEF_LOOKUP("uart0",     &uart0_clk),
	DEF_LOOKUP_CON("uart0", "apb_pclk", &uart0_pclk),
	DEF_LOOKUP("apptimer",  &app_timer_clk),
	DEF_LOOKUP("coh901461-keypad",    &keypad_clk),
	DEF_LOOKUP("u300-gpio", &gpio_clk),
	DEF_LOOKUP("rtc-coh901331",      &rtc_clk),
	DEF_LOOKUP("bustr",     &bustr_clk),
	DEF_LOOKUP("evhist",    &evhist_clk),
	DEF_LOOKUP("timer",     &timer_clk),
#ifdef CONFIG_MACH_U300_BS335
	DEF_LOOKUP("ppm",       &ppm_clk),
#endif
};

static void __init clk_register(void)
{
	/* Register the lookups */
	clkdev_add_table(lookups, ARRAY_SIZE(lookups));
}

#if (defined(CONFIG_DEBUG_FS) && defined(CONFIG_U300_DEBUG))
/*
 * The following makes it possible to view the status (especially
 * reference count and reset status) for the clocks in the platform
 * by looking into the special file <debugfs>/u300_clocks
 */

/* A list of all clocks in the platform */
static struct clk *clks[] = {
	/* Top node clock for the AMBA bus */
	&amba_clk,
	/* Connected directly to the AMBA bus */
	&cpu_clk,
	&nandif_clk,
	&semi_clk,
#ifdef CONFIG_MACH_U300_BS335
	&isp_clk,
	&cds_clk,
#endif
	&dma_clk,
	&aaif_clk,
	&apex_clk,
	&video_enc_clk,
	&xgam_clk,
	&ahb_clk,

	/* AHB bridge clocks */
	&ahb_subsys_clk,
	&intcon_clk,
	&mspro_clk,
	&emif_clk,
	/* FAST bridge clocks */
	&fast_clk,
	&mmcsd_clk,
	&i2s0_clk,
	&i2s1_clk,
	&i2c0_clk,
	&i2c1_clk,
	&spi_clk,
#ifdef CONFIG_MACH_U300_BS335
	&uart1_clk,
	&uart1_pclk,
#endif
	/* SLOW bridge clocks */
	&slow_clk,
	&wdog_clk,
	&uart0_clk,
	&uart0_pclk,
	&app_timer_clk,
	&keypad_clk,
	&gpio_clk,
	&rtc_clk,
	&bustr_clk,
	&evhist_clk,
	&timer_clk,
#ifdef CONFIG_MACH_U300_BS335
	&ppm_clk,
#endif
};

static int u300_clocks_show(struct seq_file *s, void *data)
{
	struct clk *clk;
	int i;

	seq_printf(s, "CLOCK           DEVICE          RESET STATE\t" \
		   "ACTIVE\tUSERS\tHW CTRL FREQ\n");
	seq_printf(s, "---------------------------------------------" \
		   "-----------------------------------------\n");
	for (i = 0; i < ARRAY_SIZE(clks); i++) {
		clk = clks[i];
		if (clk != ERR_PTR(-ENOENT)) {
			/* Format clock and device name nicely */
			char cdp[33];
			int chars;

			chars = snprintf(&cdp[0], 17, "%s", clk->name);
			while (chars < 16) {
				cdp[chars] = ' ';
				chars++;
			}
			chars = snprintf(&cdp[16], 17, "%s", clk->dev ?
					 dev_name(clk->dev) : "N/A");
			while (chars < 16) {
				cdp[chars+16] = ' ';
				chars++;
			}
			cdp[32] = '\0';
			if (clk->get_rate || clk->rate != 0)
				seq_printf(s,
					   "%s%s\t%s\t%d\t%s\t%lu Hz\n",
					   &cdp[0],
					   clk->reset ?
					   "ASSERTED" : "RELEASED",
					   clk->usecount ? "ON" : "OFF",
					   clk->usecount,
					   clk->hw_ctrld  ? "YES" : "NO ",
					   clk_get_rate(clk));
			else
				seq_printf(s,
					   "%s%s\t%s\t%d\t%s\t" \
					   "(unknown rate)\n",
					   &cdp[0],
					   clk->reset ?
					   "ASSERTED" : "RELEASED",
					   clk->usecount ? "ON" : "OFF",
					   clk->usecount,
					   clk->hw_ctrld  ? "YES" : "NO ");
		}
	}
	return 0;
}

static int u300_clocks_open(struct inode *inode, struct file *file)
{
	return single_open(file, u300_clocks_show, NULL);
}

static const struct file_operations u300_clocks_operations = {
	.open		= u300_clocks_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init init_clk_read_debugfs(void)
{
	/* Expose a simple debugfs interface to view all clocks */
	(void) debugfs_create_file("u300_clocks", S_IFREG | S_IRUGO,
				   NULL, NULL,
				   &u300_clocks_operations);
	return 0;
}
/*
 * This needs to come in after the core_initcall() for the
 * overall clocks, because debugfs is not available until
 * the subsystems come up.
 */
module_init(init_clk_read_debugfs);
#endif

int __init u300_clock_init(void)
{
	u16 val;

	/*
	 * FIXME: shall all this powermanagement stuff really live here???
	 */

	/* Set system to run at PLL208, max performance, a known state. */
	val = readw(U300_SYSCON_VBASE + U300_SYSCON_CCR);
	val &= ~U300_SYSCON_CCR_CLKING_PERFORMANCE_MASK;
	writew(val, U300_SYSCON_VBASE + U300_SYSCON_CCR);
	/* Wait for the PLL208 to lock if not locked in yet */
	while (!(readw(U300_SYSCON_VBASE + U300_SYSCON_CSR) &
		 U300_SYSCON_CSR_PLL208_LOCK_IND));

	/* Power management enable */
	val = readw(U300_SYSCON_VBASE + U300_SYSCON_PMCR);
	val |= U300_SYSCON_PMCR_PWR_MGNT_ENABLE;
	writew(val, U300_SYSCON_VBASE + U300_SYSCON_PMCR);

	clk_register();

	/*
	 * Some of these may be on when we boot the system so make sure they
	 * are turned OFF.
	 */
	syscon_block_reset_enable(&timer_clk);
	timer_clk.disable(&timer_clk);

	/*
	 * These shall be turned on by default when we boot the system
	 * so make sure they are ON. (Adding CPU here is a bit too much.)
	 * These clocks will be claimed by drivers later.
	 */
	syscon_block_reset_disable(&semi_clk);
	syscon_block_reset_disable(&emif_clk);
	clk_enable(&semi_clk);
	clk_enable(&emif_clk);

	return 0;
}
