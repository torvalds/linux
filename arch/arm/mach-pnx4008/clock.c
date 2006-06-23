/*
 * arch/arm/mach-pnx4008/clock.c
 *
 * Clock control driver for PNX4008
 *
 * Authors: Vitaly Wool, Dmitry Chigirev <source@mvista.com>
 * Generic clock management functions are partially based on:
 *  linux/arch/arm/mach-omap/clock.c
 *
 * 2005-2006 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/delay.h>

#include <asm/semaphore.h>
#include <asm/hardware.h>
#include <asm/io.h>

#include <asm/arch/clock.h>
#include "clock.h"

/*forward declaration*/
static struct clk per_ck;
static struct clk hclk_ck;
static struct clk ck_1MHz;
static struct clk ck_13MHz;
static struct clk ck_pll1;
static int local_set_rate(struct clk *clk, u32 rate);

static inline void clock_lock(void)
{
	local_irq_disable();
}

static inline void clock_unlock(void)
{
	local_irq_enable();
}

static void propagate_rate(struct clk *clk)
{
	struct clk *tmp_clk;

	tmp_clk = clk;
	while (tmp_clk->propagate_next) {
		tmp_clk = tmp_clk->propagate_next;
		local_set_rate(tmp_clk, tmp_clk->user_rate);
	}
}

static inline void clk_reg_disable(struct clk *clk)
{
	if (clk->enable_reg)
		__raw_writel(__raw_readl(clk->enable_reg) &
			     ~(1 << clk->enable_shift), clk->enable_reg);
}

static inline void clk_reg_enable(struct clk *clk)
{
	if (clk->enable_reg)
		__raw_writel(__raw_readl(clk->enable_reg) |
			     (1 << clk->enable_shift), clk->enable_reg);
}

static inline void clk_reg_disable1(struct clk *clk)
{
	if (clk->enable_reg1)
		__raw_writel(__raw_readl(clk->enable_reg1) &
			     ~(1 << clk->enable_shift1), clk->enable_reg1);
}

static inline void clk_reg_enable1(struct clk *clk)
{
	if (clk->enable_reg1)
		__raw_writel(__raw_readl(clk->enable_reg1) |
			     (1 << clk->enable_shift1), clk->enable_reg1);
}

static int clk_wait_for_pll_lock(struct clk *clk)
{
	int i;
	i = 0;
	while (i++ < 0xFFF && !(__raw_readl(clk->scale_reg) & 1)) ;	/*wait for PLL to lock */

	if (!(__raw_readl(clk->scale_reg) & 1)) {
		printk(KERN_ERR
		       "%s ERROR: failed to lock, scale reg data: %x\n",
		       clk->name, __raw_readl(clk->scale_reg));
		return -1;
	}
	return 0;
}

static int switch_to_dirty_13mhz(struct clk *clk)
{
	int i;
	int ret;
	u32 tmp_reg;

	ret = 0;

	if (!clk->rate)
		clk_reg_enable1(clk);

	tmp_reg = __raw_readl(clk->parent_switch_reg);
	/*if 13Mhz clock selected, select 13'MHz (dirty) source from OSC */
	if (!(tmp_reg & 1)) {
		tmp_reg |= (1 << 1);	/* Trigger switch to 13'MHz (dirty) clock */
		__raw_writel(tmp_reg, clk->parent_switch_reg);
		i = 0;
		while (i++ < 0xFFF && !(__raw_readl(clk->parent_switch_reg) & 1)) ;	/*wait for 13'MHz selection status */

		if (!(__raw_readl(clk->parent_switch_reg) & 1)) {
			printk(KERN_ERR
			       "%s ERROR: failed to select 13'MHz, parent sw reg data: %x\n",
			       clk->name, __raw_readl(clk->parent_switch_reg));
			ret = -1;
		}
	}

	if (!clk->rate)
		clk_reg_disable1(clk);

	return ret;
}

static int switch_to_clean_13mhz(struct clk *clk)
{
	int i;
	int ret;
	u32 tmp_reg;

	ret = 0;

	if (!clk->rate)
		clk_reg_enable1(clk);

	tmp_reg = __raw_readl(clk->parent_switch_reg);
	/*if 13'Mhz clock selected, select 13MHz (clean) source from OSC */
	if (tmp_reg & 1) {
		tmp_reg &= ~(1 << 1);	/* Trigger switch to 13MHz (clean) clock */
		__raw_writel(tmp_reg, clk->parent_switch_reg);
		i = 0;
		while (i++ < 0xFFF && (__raw_readl(clk->parent_switch_reg) & 1)) ;	/*wait for 13MHz selection status */

		if (__raw_readl(clk->parent_switch_reg) & 1) {
			printk(KERN_ERR
			       "%s ERROR: failed to select 13MHz, parent sw reg data: %x\n",
			       clk->name, __raw_readl(clk->parent_switch_reg));
			ret = -1;
		}
	}

	if (!clk->rate)
		clk_reg_disable1(clk);

	return ret;
}

static int set_13MHz_parent(struct clk *clk, struct clk *parent)
{
	int ret = -EINVAL;

	if (parent == &ck_13MHz)
		ret = switch_to_clean_13mhz(clk);
	else if (parent == &ck_pll1)
		ret = switch_to_dirty_13mhz(clk);

	return ret;
}

#define PLL160_MIN_FCCO 156000
#define PLL160_MAX_FCCO 320000

/*
 * Calculate pll160 settings.
 * Possible input: up to 320MHz with step of clk->parent->rate.
 * In PNX4008 parent rate for pll160s may be either 1 or 13MHz.
 * Ignored paths: "feedback" (bit 13 set), "div-by-N".
 * Setting ARM PLL4 rate to 0 will put CPU into direct run mode.
 * Setting PLL5 and PLL3 rate to 0 will disable USB and DSP clock input.
 * Please refer to PNX4008 IC manual for details.
 */

static int pll160_set_rate(struct clk *clk, u32 rate)
{
	u32 tmp_reg, tmp_m, tmp_2p, i;
	u32 parent_rate;
	int ret = -EINVAL;

	parent_rate = clk->parent->rate;

	if (!parent_rate)
		goto out;

	/* set direct run for ARM or disable output for others  */
	clk_reg_disable(clk);

	/* disable source input as well (ignored for ARM) */
	clk_reg_disable1(clk);

	tmp_reg = __raw_readl(clk->scale_reg);
	tmp_reg &= ~0x1ffff;	/*clear all settings, power down */
	__raw_writel(tmp_reg, clk->scale_reg);

	rate -= rate % parent_rate;	/*round down the input */

	if (rate > PLL160_MAX_FCCO)
		rate = PLL160_MAX_FCCO;

	if (!rate) {
		clk->rate = 0;
		ret = 0;
		goto out;
	}

	clk_reg_enable1(clk);
	tmp_reg = __raw_readl(clk->scale_reg);

	if (rate == parent_rate) {
		/*enter direct bypass mode */
		tmp_reg |= ((1 << 14) | (1 << 15));
		__raw_writel(tmp_reg, clk->scale_reg);
		clk->rate = parent_rate;
		clk_reg_enable(clk);
		ret = 0;
		goto out;
	}

	i = 0;
	for (tmp_2p = 1; tmp_2p < 16; tmp_2p <<= 1) {
		if (rate * tmp_2p >= PLL160_MIN_FCCO)
			break;
		i++;
	}

	if (tmp_2p > 1)
		tmp_reg |= ((i - 1) << 11);
	else
		tmp_reg |= (1 << 14);	/*direct mode, no divide */

	tmp_m = rate * tmp_2p;
	tmp_m /= parent_rate;

	tmp_reg |= (tmp_m - 1) << 1;	/*calculate M */
	tmp_reg |= (1 << 16);	/*power up PLL */
	__raw_writel(tmp_reg, clk->scale_reg);

	if (clk_wait_for_pll_lock(clk) < 0) {
		clk_reg_disable(clk);
		clk_reg_disable1(clk);

		tmp_reg = __raw_readl(clk->scale_reg);
		tmp_reg &= ~0x1ffff;	/*clear all settings, power down */
		__raw_writel(tmp_reg, clk->scale_reg);
		clk->rate = 0;
		ret = -EFAULT;
		goto out;
	}

	clk->rate = (tmp_m * parent_rate) / tmp_2p;

	if (clk->flags & RATE_PROPAGATES)
		propagate_rate(clk);

	clk_reg_enable(clk);
	ret = 0;

out:
	return ret;
}

/*configure PER_CLK*/
static int per_clk_set_rate(struct clk *clk, u32 rate)
{
	u32 tmp;

	tmp = __raw_readl(clk->scale_reg);
	tmp &= ~(0x1f << 2);
	tmp |= ((clk->parent->rate / clk->rate) - 1) << 2;
	__raw_writel(tmp, clk->scale_reg);
	clk->rate = rate;
	return 0;
}

/*configure HCLK*/
static int hclk_set_rate(struct clk *clk, u32 rate)
{
	u32 tmp;
	tmp = __raw_readl(clk->scale_reg);
	tmp = tmp & ~0x3;
	switch (rate) {
	case 1:
		break;
	case 2:
		tmp |= 1;
		break;
	case 4:
		tmp |= 2;
		break;
	}

	__raw_writel(tmp, clk->scale_reg);
	clk->rate = rate;
	return 0;
}

static u32 hclk_round_rate(struct clk *clk, u32 rate)
{
	switch (rate) {
	case 1:
	case 4:
		return rate;
	}
	return 2;
}

static u32 per_clk_round_rate(struct clk *clk, u32 rate)
{
	return CLK_RATE_13MHZ;
}

static int on_off_set_rate(struct clk *clk, u32 rate)
{
	if (rate) {
		clk_reg_enable(clk);
		clk->rate = 1;
	} else {
		clk_reg_disable(clk);
		clk->rate = 0;
	}
	return 0;
}

static int on_off_inv_set_rate(struct clk *clk, u32 rate)
{
	if (rate) {
		clk_reg_disable(clk);	/*enable bit is inverted */
		clk->rate = 1;
	} else {
		clk_reg_enable(clk);
		clk->rate = 0;
	}
	return 0;
}

static u32 on_off_round_rate(struct clk *clk, u32 rate)
{
	return (rate ? 1 : 0);
}

static u32 pll4_round_rate(struct clk *clk, u32 rate)
{
	if (rate > CLK_RATE_208MHZ)
		rate = CLK_RATE_208MHZ;
	if (rate == CLK_RATE_208MHZ && hclk_ck.user_rate == 1)
		rate = CLK_RATE_208MHZ - CLK_RATE_13MHZ;
	return (rate - (rate % (hclk_ck.user_rate * CLK_RATE_13MHZ)));
}

static u32 pll3_round_rate(struct clk *clk, u32 rate)
{
	if (rate > CLK_RATE_208MHZ)
		rate = CLK_RATE_208MHZ;
	return (rate - rate % CLK_RATE_13MHZ);
}

static u32 pll5_round_rate(struct clk *clk, u32 rate)
{
	return (rate ? CLK_RATE_48MHZ : 0);
}

static u32 ck_13MHz_round_rate(struct clk *clk, u32 rate)
{
	return (rate ? CLK_RATE_13MHZ : 0);
}

static int ck_13MHz_set_rate(struct clk *clk, u32 rate)
{
	if (rate) {
		clk_reg_disable(clk);	/*enable bit is inverted */
		udelay(500);
		clk->rate = CLK_RATE_13MHZ;
		ck_1MHz.rate = CLK_RATE_1MHZ;
	} else {
		clk_reg_enable(clk);
		clk->rate = 0;
		ck_1MHz.rate = 0;
	}
	return 0;
}

static int pll1_set_rate(struct clk *clk, u32 rate)
{
#if 0 /* doesn't work on some boards, probably a HW BUG */
	if (rate) {
		clk_reg_disable(clk);	/*enable bit is inverted */
		if (!clk_wait_for_pll_lock(clk)) {
			clk->rate = CLK_RATE_13MHZ;
		} else {
			clk_reg_enable(clk);
			clk->rate = 0;
		}

	} else {
		clk_reg_enable(clk);
		clk->rate = 0;
	}
#endif
	return 0;
}

/* Clock sources */

static struct clk osc_13MHz = {
	.name = "osc_13MHz",
	.flags = FIXED_RATE,
	.rate = CLK_RATE_13MHZ,
};

static struct clk ck_13MHz = {
	.name = "ck_13MHz",
	.parent = &osc_13MHz,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &ck_13MHz_round_rate,
	.set_rate = &ck_13MHz_set_rate,
	.enable_reg = OSC13CTRL_REG,
	.enable_shift = 0,
	.rate = CLK_RATE_13MHZ,
};

static struct clk osc_32KHz = {
	.name = "osc_32KHz",
	.flags = FIXED_RATE,
	.rate = CLK_RATE_32KHZ,
};

/*attached to PLL5*/
static struct clk ck_1MHz = {
	.name = "ck_1MHz",
	.flags = FIXED_RATE | PARENT_SET_RATE,
	.parent = &ck_13MHz,
};

/* PLL1 (397) - provides 13' MHz clock */
static struct clk ck_pll1 = {
	.name = "ck_pll1",
	.parent = &osc_32KHz,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &ck_13MHz_round_rate,
	.set_rate = &pll1_set_rate,
	.enable_reg = PLLCTRL_REG,
	.enable_shift = 1,
	.scale_reg = PLLCTRL_REG,
	.rate = CLK_RATE_13MHZ,
};

/* CPU/Bus PLL */
static struct clk ck_pll4 = {
	.name = "ck_pll4",
	.parent = &ck_pll1,
	.flags = RATE_PROPAGATES | NEEDS_INITIALIZATION,
	.propagate_next = &per_ck,
	.round_rate = &pll4_round_rate,
	.set_rate = &pll160_set_rate,
	.rate = CLK_RATE_208MHZ,
	.scale_reg = HCLKPLLCTRL_REG,
	.enable_reg = PWRCTRL_REG,
	.enable_shift = 2,
	.parent_switch_reg = SYSCLKCTRL_REG,
	.set_parent = &set_13MHz_parent,
};

/* USB PLL */
static struct clk ck_pll5 = {
	.name = "ck_pll5",
	.parent = &ck_1MHz,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &pll5_round_rate,
	.set_rate = &pll160_set_rate,
	.scale_reg = USBCTRL_REG,
	.enable_reg = USBCTRL_REG,
	.enable_shift = 18,
	.enable_reg1 = USBCTRL_REG,
	.enable_shift1 = 17,
};

/* XPERTTeak DSP PLL */
static struct clk ck_pll3 = {
	.name = "ck_pll3",
	.parent = &ck_pll1,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &pll3_round_rate,
	.set_rate = &pll160_set_rate,
	.scale_reg = DSPPLLCTRL_REG,
	.enable_reg = DSPCLKCTRL_REG,
	.enable_shift = 3,
	.enable_reg1 = DSPCLKCTRL_REG,
	.enable_shift1 = 2,
	.parent_switch_reg = DSPCLKCTRL_REG,
	.set_parent = &set_13MHz_parent,
};

static struct clk hclk_ck = {
	.name = "hclk_ck",
	.parent = &ck_pll4,
	.flags = PARENT_SET_RATE,
	.set_rate = &hclk_set_rate,
	.round_rate = &hclk_round_rate,
	.scale_reg = HCLKDIVCTRL_REG,
	.rate = 2,
	.user_rate = 2,
};

static struct clk per_ck = {
	.name = "per_ck",
	.parent = &ck_pll4,
	.flags = FIXED_RATE,
	.propagate_next = &hclk_ck,
	.set_rate = &per_clk_set_rate,
	.round_rate = &per_clk_round_rate,
	.scale_reg = HCLKDIVCTRL_REG,
	.rate = CLK_RATE_13MHZ,
	.user_rate = CLK_RATE_13MHZ,
};

static struct clk m2hclk_ck = {
	.name = "m2hclk_ck",
	.parent = &hclk_ck,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &on_off_round_rate,
	.set_rate = &on_off_inv_set_rate,
	.rate = 1,
	.enable_shift = 6,
	.enable_reg = PWRCTRL_REG,
};

static struct clk vfp9_ck = {
	.name = "vfp9_ck",
	.parent = &ck_pll4,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &on_off_round_rate,
	.set_rate = &on_off_set_rate,
	.rate = 1,
	.enable_shift = 4,
	.enable_reg = VFP9CLKCTRL_REG,
};

static struct clk keyscan_ck = {
	.name = "keyscan_ck",
	.parent = &osc_32KHz,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &on_off_round_rate,
	.set_rate = &on_off_set_rate,
	.enable_shift = 0,
	.enable_reg = KEYCLKCTRL_REG,
};

static struct clk touch_ck = {
	.name = "touch_ck",
	.parent = &osc_32KHz,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &on_off_round_rate,
	.set_rate = &on_off_set_rate,
	.enable_shift = 0,
	.enable_reg = TSCLKCTRL_REG,
};

static struct clk pwm1_ck = {
	.name = "pwm1_ck",
	.parent = &osc_32KHz,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &on_off_round_rate,
	.set_rate = &on_off_set_rate,
	.enable_shift = 0,
	.enable_reg = PWMCLKCTRL_REG,
};

static struct clk pwm2_ck = {
	.name = "pwm2_ck",
	.parent = &osc_32KHz,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &on_off_round_rate,
	.set_rate = &on_off_set_rate,
	.enable_shift = 2,
	.enable_reg = PWMCLKCTRL_REG,
};

static struct clk jpeg_ck = {
	.name = "jpeg_ck",
	.parent = &hclk_ck,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &on_off_round_rate,
	.set_rate = &on_off_set_rate,
	.enable_shift = 0,
	.enable_reg = JPEGCLKCTRL_REG,
};

static struct clk ms_ck = {
	.name = "ms_ck",
	.parent = &ck_pll4,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &on_off_round_rate,
	.set_rate = &on_off_set_rate,
	.enable_shift = 5,
	.enable_reg = MSCTRL_REG,
};

static struct clk dum_ck = {
	.name = "dum_ck",
	.parent = &hclk_ck,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &on_off_round_rate,
	.set_rate = &on_off_set_rate,
	.enable_shift = 0,
	.enable_reg = DUMCLKCTRL_REG,
};

static struct clk flash_ck = {
	.name = "flash_ck",
	.parent = &hclk_ck,
	.round_rate = &on_off_round_rate,
	.set_rate = &on_off_set_rate,
	.enable_shift = 1,	/* Only MLC clock supported */
	.enable_reg = FLASHCLKCTRL_REG,
};

static struct clk i2c0_ck = {
	.name = "i2c0_ck",
	.parent = &per_ck,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &on_off_round_rate,
	.set_rate = &on_off_set_rate,
	.enable_shift = 0,
	.enable_reg = I2CCLKCTRL_REG,
};

static struct clk i2c1_ck = {
	.name = "i2c1_ck",
	.parent = &per_ck,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &on_off_round_rate,
	.set_rate = &on_off_set_rate,
	.enable_shift = 1,
	.enable_reg = I2CCLKCTRL_REG,
};

static struct clk i2c2_ck = {
	.name = "i2c2_ck",
	.parent = &per_ck,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &on_off_round_rate,
	.set_rate = &on_off_set_rate,
	.enable_shift = 2,
	.enable_reg = USB_OTG_CLKCTRL_REG,
};

static struct clk spi0_ck = {
	.name = "spi0_ck",
	.parent = &hclk_ck,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &on_off_round_rate,
	.set_rate = &on_off_set_rate,
	.enable_shift = 0,
	.enable_reg = SPICTRL_REG,
};

static struct clk spi1_ck = {
	.name = "spi1_ck",
	.parent = &hclk_ck,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &on_off_round_rate,
	.set_rate = &on_off_set_rate,
	.enable_shift = 4,
	.enable_reg = SPICTRL_REG,
};

static struct clk dma_ck = {
	.name = "dma_ck",
	.parent = &hclk_ck,
	.round_rate = &on_off_round_rate,
	.set_rate = &on_off_set_rate,
	.enable_shift = 0,
	.enable_reg = DMACLKCTRL_REG,
};

static struct clk uart3_ck = {
	.name = "uart3_ck",
	.parent = &per_ck,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &on_off_round_rate,
	.set_rate = &on_off_set_rate,
	.rate = 1,
	.enable_shift = 0,
	.enable_reg = UARTCLKCTRL_REG,
};

static struct clk uart4_ck = {
	.name = "uart4_ck",
	.parent = &per_ck,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &on_off_round_rate,
	.set_rate = &on_off_set_rate,
	.enable_shift = 1,
	.enable_reg = UARTCLKCTRL_REG,
};

static struct clk uart5_ck = {
	.name = "uart5_ck",
	.parent = &per_ck,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &on_off_round_rate,
	.set_rate = &on_off_set_rate,
	.rate = 1,
	.enable_shift = 2,
	.enable_reg = UARTCLKCTRL_REG,
};

static struct clk uart6_ck = {
	.name = "uart6_ck",
	.parent = &per_ck,
	.flags = NEEDS_INITIALIZATION,
	.round_rate = &on_off_round_rate,
	.set_rate = &on_off_set_rate,
	.enable_shift = 3,
	.enable_reg = UARTCLKCTRL_REG,
};

/* These clocks are visible outside this module
 * and can be initialized
 */
static struct clk *onchip_clks[] = {
	&ck_13MHz,
	&ck_pll1,
	&ck_pll4,
	&ck_pll5,
	&ck_pll3,
	&vfp9_ck,
	&m2hclk_ck,
	&hclk_ck,
	&dma_ck,
	&flash_ck,
	&dum_ck,
	&keyscan_ck,
	&pwm1_ck,
	&pwm2_ck,
	&jpeg_ck,
	&ms_ck,
	&touch_ck,
	&i2c0_ck,
	&i2c1_ck,
	&i2c2_ck,
	&spi0_ck,
	&spi1_ck,
	&uart3_ck,
	&uart4_ck,
	&uart5_ck,
	&uart6_ck,
};

static int local_set_rate(struct clk *clk, u32 rate)
{
	int ret = -EINVAL;
	if (clk->set_rate) {

		if (clk->user_rate == clk->rate && clk->parent->rate) {
			/* if clock enabled or rate not set */
			clk->user_rate = clk->round_rate(clk, rate);
			ret = clk->set_rate(clk, clk->user_rate);
		} else
			clk->user_rate = clk->round_rate(clk, rate);
		ret = 0;
	}
	return ret;
}

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	int ret = -EINVAL;

	if (clk->flags & FIXED_RATE)
		goto out;

	clock_lock();
	if ((clk->flags & PARENT_SET_RATE) && clk->parent) {

		clk->user_rate = clk->round_rate(clk, rate);
		/* parent clock needs to be refreshed
		   for the setting to take effect */
	} else {
		ret = local_set_rate(clk, rate);
	}
	ret = 0;
	clock_unlock();

out:
	return ret;
}

EXPORT_SYMBOL(clk_set_rate);

struct clk *clk_get(struct device *dev, const char *id)
{
	struct clk *clk = ERR_PTR(-ENOENT);
	struct clk **clkp;

	clock_lock();
	for (clkp = onchip_clks; clkp < onchip_clks + ARRAY_SIZE(onchip_clks);
	     clkp++) {
		if (strcmp(id, (*clkp)->name) == 0
		    && try_module_get((*clkp)->owner)) {
			clk = (*clkp);
			break;
		}
	}
	clock_unlock();

	return clk;
}
EXPORT_SYMBOL(clk_get);

void clk_put(struct clk *clk)
{
	clock_lock();
	if (clk && !IS_ERR(clk))
		module_put(clk->owner);
	clock_unlock();
}
EXPORT_SYMBOL(clk_put);

unsigned long clk_get_rate(struct clk *clk)
{
	unsigned long ret;
	clock_lock();
	ret = clk->rate;
	clock_unlock();
	return ret;
}
EXPORT_SYMBOL(clk_get_rate);

static int local_clk_enable(struct clk *clk)
{
	int ret = 0;

	if (!(clk->flags & FIXED_RATE) && !clk->rate && clk->set_rate
	    && clk->user_rate)
		ret = clk->set_rate(clk, clk->user_rate);
	return ret;
}

static void local_clk_disable(struct clk *clk)
{
	if (!(clk->flags & FIXED_RATE) && clk->rate && clk->set_rate)
		clk->set_rate(clk, 0);
}

int clk_enable(struct clk *clk)
{
	int ret = 0;

	clock_lock();
	ret = local_clk_enable(clk);
	clock_unlock();
	return ret;
}

EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	clock_lock();
	local_clk_disable(clk);
	clock_unlock();
}

EXPORT_SYMBOL(clk_disable);

static void local_clk_unuse(struct clk *clk)
{
	if (clk->usecount > 0 && !(--clk->usecount)) {
		local_clk_disable(clk);
		if (clk->parent)
			local_clk_unuse(clk->parent);
	}
}

static int local_clk_use(struct clk *clk)
{
	int ret = 0;
	if (clk->usecount++ == 0) {
		if (clk->parent)
			ret = local_clk_use(clk->parent);

		if (ret != 0) {
			clk->usecount--;
			goto out;
		}

		ret = local_clk_enable(clk);

		if (ret != 0 && clk->parent) {
			local_clk_unuse(clk->parent);
			clk->usecount--;
		}
	}
out:
	return ret;
}

/* The main purpose of clk_use ans clk_unuse functions
 * is to control switching 13MHz oscillator and PLL1 (13'MHz),
 * so that they are disabled whenever none of PLL2-5 is using them.
 * Although in theory these functions should work with any clock,
 * please use them only on PLL2 - PLL5 to avoid confusion.
 */
int clk_use(struct clk *clk)
{
	int ret = 0;

	clock_lock();
	ret = local_clk_use(clk);
	clock_unlock();
	return ret;
}
EXPORT_SYMBOL(clk_use);

void clk_unuse(struct clk *clk)
{

	clock_lock();
	local_clk_unuse(clk);
	clock_unlock();
}

EXPORT_SYMBOL(clk_unuse);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	long ret;
	clock_lock();
	if (clk->round_rate)
		ret = clk->round_rate(clk, rate);
	else
		ret = clk->rate;
	clock_unlock();
	return ret;
}

EXPORT_SYMBOL(clk_round_rate);

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	int ret = -ENODEV;
	if (!clk->set_parent)
		goto out;

	clock_lock();
	ret = clk->set_parent(clk, parent);
	if (!ret)
		clk->parent = parent;
	clock_unlock();

out:
	return ret;
}

EXPORT_SYMBOL(clk_set_parent);

static int __init clk_init(void)
{
	struct clk **clkp;

	/* Disable autoclocking, as it doesn't seem to work */
	__raw_writel(0xff, AUTOCLK_CTRL);

	for (clkp = onchip_clks; clkp < onchip_clks + ARRAY_SIZE(onchip_clks);
	     clkp++) {
		if (((*clkp)->flags & NEEDS_INITIALIZATION)
		    && ((*clkp)->set_rate)) {
			(*clkp)->user_rate = (*clkp)->rate;
			local_set_rate((*clkp), (*clkp)->user_rate);
			if ((*clkp)->set_parent)
				(*clkp)->set_parent((*clkp), (*clkp)->parent);
		}
		pr_debug("%s: clock %s, rate %ld\n",
			__FUNCTION__, (*clkp)->name, (*clkp)->rate);
	}

	clk_use(&ck_pll4);

	/* if ck_13MHz is not used, disable it. */
	if (ck_13MHz.usecount == 0)
		local_clk_disable(&ck_13MHz);

	/* Disable autoclocking */
	__raw_writeb(0xff, AUTOCLK_CTRL);

	return 0;
}

arch_initcall(clk_init);
