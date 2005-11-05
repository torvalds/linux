/*
 *  linux/arch/arm/plat-omap/clock.c
 *
 *  Copyright (C) 2004 Nokia corporation
 *  Written by Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>

#include <asm/io.h>
#include <asm/semaphore.h>
#include <asm/hardware/clock.h>
#include <asm/arch/board.h>
#include <asm/arch/usb.h>

#include "clock.h"
#include "sram.h"

static LIST_HEAD(clocks);
static DECLARE_MUTEX(clocks_sem);
static DEFINE_SPINLOCK(clockfw_lock);
static void propagate_rate(struct clk *  clk);
/* UART clock function */
static int set_uart_rate(struct clk * clk, unsigned long rate);
/* External clock (MCLK & BCLK) functions */
static int set_ext_clk_rate(struct clk *  clk, unsigned long rate);
static long round_ext_clk_rate(struct clk *  clk, unsigned long rate);
static void init_ext_clk(struct clk *  clk);
/* MPU virtual clock functions */
static int select_table_rate(struct clk *  clk, unsigned long rate);
static long round_to_table_rate(struct clk *  clk, unsigned long rate);
void clk_setdpll(__u16, __u16);

static struct mpu_rate rate_table[] = {
	/* MPU MHz, xtal MHz, dpll1 MHz, CKCTL, DPLL_CTL
	 * armdiv, dspdiv, dspmmu, tcdiv, perdiv, lcddiv
	 */
#if defined(CONFIG_OMAP_ARM_216MHZ)
	{ 216000000, 12000000, 216000000, 0x050d, 0x2910 }, /* 1/1/2/2/2/8 */
#endif
#if defined(CONFIG_OMAP_ARM_195MHZ)
	{ 195000000, 13000000, 195000000, 0x050e, 0x2790 }, /* 1/1/2/2/4/8 */
#endif
#if defined(CONFIG_OMAP_ARM_192MHZ)
	{ 192000000, 19200000, 192000000, 0x050f, 0x2510 }, /* 1/1/2/2/8/8 */
	{ 192000000, 12000000, 192000000, 0x050f, 0x2810 }, /* 1/1/2/2/8/8 */
	{  96000000, 12000000, 192000000, 0x055f, 0x2810 }, /* 2/2/2/2/8/8 */
	{  48000000, 12000000, 192000000, 0x0baf, 0x2810 }, /* 4/8/4/4/8/8 */
	{  24000000, 12000000, 192000000, 0x0fff, 0x2810 }, /* 8/8/8/8/8/8 */
#endif
#if defined(CONFIG_OMAP_ARM_182MHZ)
	{ 182000000, 13000000, 182000000, 0x050e, 0x2710 }, /* 1/1/2/2/4/8 */
#endif
#if defined(CONFIG_OMAP_ARM_168MHZ)
	{ 168000000, 12000000, 168000000, 0x010f, 0x2710 }, /* 1/1/1/2/8/8 */
#endif
#if defined(CONFIG_OMAP_ARM_150MHZ)
	{ 150000000, 12000000, 150000000, 0x010a, 0x2cb0 }, /* 1/1/1/2/4/4 */
#endif
#if defined(CONFIG_OMAP_ARM_120MHZ)
	{ 120000000, 12000000, 120000000, 0x010a, 0x2510 }, /* 1/1/1/2/4/4 */
#endif
#if defined(CONFIG_OMAP_ARM_96MHZ)
	{  96000000, 12000000,  96000000, 0x0005, 0x2410 }, /* 1/1/1/1/2/2 */
#endif
#if defined(CONFIG_OMAP_ARM_60MHZ)
	{  60000000, 12000000,  60000000, 0x0005, 0x2290 }, /* 1/1/1/1/2/2 */
#endif
#if defined(CONFIG_OMAP_ARM_30MHZ)
	{  30000000, 12000000,  60000000, 0x0555, 0x2290 }, /* 2/2/2/2/2/2 */
#endif
	{ 0, 0, 0, 0, 0 },
};


static void ckctl_recalc(struct clk *  clk);
int __clk_enable(struct clk *clk);
void __clk_disable(struct clk *clk);
void __clk_unuse(struct clk *clk);
int __clk_use(struct clk *clk);


static void followparent_recalc(struct clk *  clk)
{
	clk->rate = clk->parent->rate;
}


static void watchdog_recalc(struct clk *  clk)
{
	clk->rate = clk->parent->rate / 14;
}

static void uart_recalc(struct clk * clk)
{
	unsigned int val = omap_readl(clk->enable_reg);
	if (val & clk->enable_bit)
		clk->rate = 48000000;
	else
		clk->rate = 12000000;
}

static struct clk ck_ref = {
	.name		= "ck_ref",
	.rate		= 12000000,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  ALWAYS_ENABLED,
};

static struct clk ck_dpll1 = {
	.name		= "ck_dpll1",
	.parent		= &ck_ref,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  RATE_PROPAGATES | ALWAYS_ENABLED,
};

static struct clk ck_dpll1out = {
	.name		= "ck_dpll1out",
	.parent		= &ck_dpll1,
	.flags		= CLOCK_IN_OMAP16XX,
	.enable_reg	= ARM_IDLECT2,
	.enable_bit	= EN_CKOUT_ARM,
	.recalc		= &followparent_recalc,
};

static struct clk arm_ck = {
	.name		= "arm_ck",
	.parent		= &ck_dpll1,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  RATE_CKCTL | RATE_PROPAGATES | ALWAYS_ENABLED,
	.rate_offset	= CKCTL_ARMDIV_OFFSET,
	.recalc		= &ckctl_recalc,
};

static struct clk armper_ck = {
	.name		= "armper_ck",
	.parent		= &ck_dpll1,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  RATE_CKCTL,
	.enable_reg	= ARM_IDLECT2,
	.enable_bit	= EN_PERCK,
	.rate_offset	= CKCTL_PERDIV_OFFSET,
	.recalc		= &ckctl_recalc,
};

static struct clk arm_gpio_ck = {
	.name		= "arm_gpio_ck",
	.parent		= &ck_dpll1,
	.flags		= CLOCK_IN_OMAP1510,
	.enable_reg	= ARM_IDLECT2,
	.enable_bit	= EN_GPIOCK,
	.recalc		= &followparent_recalc,
};

static struct clk armxor_ck = {
	.name		= "armxor_ck",
	.parent		= &ck_ref,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX,
	.enable_reg	= ARM_IDLECT2,
	.enable_bit	= EN_XORPCK,
	.recalc		= &followparent_recalc,
};

static struct clk armtim_ck = {
	.name		= "armtim_ck",
	.parent		= &ck_ref,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX,
	.enable_reg	= ARM_IDLECT2,
	.enable_bit	= EN_TIMCK,
	.recalc		= &followparent_recalc,
};

static struct clk armwdt_ck = {
	.name		= "armwdt_ck",
	.parent		= &ck_ref,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX,
	.enable_reg	= ARM_IDLECT2,
	.enable_bit	= EN_WDTCK,
	.recalc		= &watchdog_recalc,
};

static struct clk arminth_ck16xx = {
	.name		= "arminth_ck",
	.parent		= &arm_ck,
	.flags		= CLOCK_IN_OMAP16XX | ALWAYS_ENABLED,
	.recalc		= &followparent_recalc,
	/* Note: On 16xx the frequency can be divided by 2 by programming
	 * ARM_CKCTL:ARM_INTHCK_SEL(14) to 1
	 *
	 * 1510 version is in TC clocks.
	 */
};

static struct clk dsp_ck = {
	.name		= "dsp_ck",
	.parent		= &ck_dpll1,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  RATE_CKCTL,
	.enable_reg	= ARM_CKCTL,
	.enable_bit	= EN_DSPCK,
	.rate_offset	= CKCTL_DSPDIV_OFFSET,
	.recalc		= &ckctl_recalc,
};

static struct clk dspmmu_ck = {
	.name		= "dspmmu_ck",
	.parent		= &ck_dpll1,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  RATE_CKCTL | ALWAYS_ENABLED,
	.rate_offset	= CKCTL_DSPMMUDIV_OFFSET,
	.recalc		= &ckctl_recalc,
};

static struct clk dspper_ck = {
	.name		= "dspper_ck",
	.parent		= &ck_dpll1,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  RATE_CKCTL | DSP_DOMAIN_CLOCK | VIRTUAL_IO_ADDRESS,
	.enable_reg	= DSP_IDLECT2,
	.enable_bit	= EN_PERCK,
	.rate_offset	= CKCTL_PERDIV_OFFSET,
	.recalc		= &followparent_recalc,
	//.recalc		= &ckctl_recalc,
};

static struct clk dspxor_ck = {
	.name		= "dspxor_ck",
	.parent		= &ck_ref,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  DSP_DOMAIN_CLOCK | VIRTUAL_IO_ADDRESS,
	.enable_reg	= DSP_IDLECT2,
	.enable_bit	= EN_XORPCK,
	.recalc		= &followparent_recalc,
};

static struct clk dsptim_ck = {
	.name		= "dsptim_ck",
	.parent		= &ck_ref,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  DSP_DOMAIN_CLOCK | VIRTUAL_IO_ADDRESS,
	.enable_reg	= DSP_IDLECT2,
	.enable_bit	= EN_DSPTIMCK,
	.recalc		= &followparent_recalc,
};

static struct clk tc_ck = {
	.name		= "tc_ck",
	.parent		= &ck_dpll1,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP730 |
			  RATE_CKCTL | RATE_PROPAGATES | ALWAYS_ENABLED,
	.rate_offset	= CKCTL_TCDIV_OFFSET,
	.recalc		= &ckctl_recalc,
};

static struct clk arminth_ck1510 = {
	.name		= "arminth_ck",
	.parent		= &tc_ck,
	.flags		= CLOCK_IN_OMAP1510 | ALWAYS_ENABLED,
	.recalc		= &followparent_recalc,
	/* Note: On 1510 the frequency follows TC_CK
	 *
	 * 16xx version is in MPU clocks.
	 */
};

static struct clk tipb_ck = {
	.name		= "tibp_ck",
	.parent		= &tc_ck,
	.flags		= CLOCK_IN_OMAP1510 | ALWAYS_ENABLED,
	.recalc		= &followparent_recalc,
};

static struct clk l3_ocpi_ck = {
	.name		= "l3_ocpi_ck",
	.parent		= &tc_ck,
	.flags		= CLOCK_IN_OMAP16XX,
	.enable_reg	= ARM_IDLECT3,
	.enable_bit	= EN_OCPI_CK,
	.recalc		= &followparent_recalc,
};

static struct clk tc1_ck = {
	.name		= "tc1_ck",
	.parent		= &tc_ck,
	.flags		= CLOCK_IN_OMAP16XX,
	.enable_reg	= ARM_IDLECT3,
	.enable_bit	= EN_TC1_CK,
	.recalc		= &followparent_recalc,
};

static struct clk tc2_ck = {
	.name		= "tc2_ck",
	.parent		= &tc_ck,
	.flags		= CLOCK_IN_OMAP16XX,
	.enable_reg	= ARM_IDLECT3,
	.enable_bit	= EN_TC2_CK,
	.recalc		= &followparent_recalc,
};

static struct clk dma_ck = {
	.name		= "dma_ck",
	.parent		= &tc_ck,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  ALWAYS_ENABLED,
	.recalc		= &followparent_recalc,
};

static struct clk dma_lcdfree_ck = {
	.name		= "dma_lcdfree_ck",
	.parent		= &tc_ck,
	.flags		= CLOCK_IN_OMAP16XX | ALWAYS_ENABLED,
	.recalc		= &followparent_recalc,
};

static struct clk api_ck = {
	.name		= "api_ck",
	.parent		= &tc_ck,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX,
	.enable_reg	= ARM_IDLECT2,
	.enable_bit	= EN_APICK,
	.recalc		= &followparent_recalc,
};

static struct clk lb_ck = {
	.name		= "lb_ck",
	.parent		= &tc_ck,
	.flags		= CLOCK_IN_OMAP1510,
	.enable_reg	= ARM_IDLECT2,
	.enable_bit	= EN_LBCK,
	.recalc		= &followparent_recalc,
};

static struct clk rhea1_ck = {
	.name		= "rhea1_ck",
	.parent		= &tc_ck,
	.flags		= CLOCK_IN_OMAP16XX | ALWAYS_ENABLED,
	.recalc		= &followparent_recalc,
};

static struct clk rhea2_ck = {
	.name		= "rhea2_ck",
	.parent		= &tc_ck,
	.flags		= CLOCK_IN_OMAP16XX | ALWAYS_ENABLED,
	.recalc		= &followparent_recalc,
};

static struct clk lcd_ck = {
	.name		= "lcd_ck",
	.parent		= &ck_dpll1,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP730 |
			  RATE_CKCTL,
	.enable_reg	= ARM_IDLECT2,
	.enable_bit	= EN_LCDCK,
	.rate_offset	= CKCTL_LCDDIV_OFFSET,
	.recalc		= &ckctl_recalc,
};

static struct clk uart1_1510 = {
	.name		= "uart1_ck",
	/* Direct from ULPD, no parent */
	.rate		= 12000000,
	.flags		= CLOCK_IN_OMAP1510 | ENABLE_REG_32BIT | ALWAYS_ENABLED,
	.enable_reg	= MOD_CONF_CTRL_0,
	.enable_bit	= 29,	/* Chooses between 12MHz and 48MHz */
	.set_rate	= &set_uart_rate,
	.recalc		= &uart_recalc,
};

static struct clk uart1_16xx = {
	.name		= "uart1_ck",
	/* Direct from ULPD, no parent */
	.rate		= 48000000,
	.flags		= CLOCK_IN_OMAP16XX | RATE_FIXED | ENABLE_REG_32BIT,
	.enable_reg	= MOD_CONF_CTRL_0,
	.enable_bit	= 29,
};

static struct clk uart2_ck = {
	.name		= "uart2_ck",
	/* Direct from ULPD, no parent */
	.rate		= 12000000,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | ENABLE_REG_32BIT |
			  ALWAYS_ENABLED,
	.enable_reg	= MOD_CONF_CTRL_0,
	.enable_bit	= 30,	/* Chooses between 12MHz and 48MHz */
	.set_rate	= &set_uart_rate,
	.recalc		= &uart_recalc,
};

static struct clk uart3_1510 = {
	.name		= "uart3_ck",
	/* Direct from ULPD, no parent */
	.rate		= 12000000,
	.flags		= CLOCK_IN_OMAP1510 | ENABLE_REG_32BIT | ALWAYS_ENABLED,
	.enable_reg	= MOD_CONF_CTRL_0,
	.enable_bit	= 31,	/* Chooses between 12MHz and 48MHz */
	.set_rate	= &set_uart_rate,
	.recalc		= &uart_recalc,
};

static struct clk uart3_16xx = {
	.name		= "uart3_ck",
	/* Direct from ULPD, no parent */
	.rate		= 48000000,
	.flags		= CLOCK_IN_OMAP16XX | RATE_FIXED | ENABLE_REG_32BIT,
	.enable_reg	= MOD_CONF_CTRL_0,
	.enable_bit	= 31,
};

static struct clk usb_clko = {	/* 6 MHz output on W4_USB_CLKO */
	.name		= "usb_clko",
	/* Direct from ULPD, no parent */
	.rate		= 6000000,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  RATE_FIXED | ENABLE_REG_32BIT,
	.enable_reg	= ULPD_CLOCK_CTRL,
	.enable_bit	= USB_MCLK_EN_BIT,
};

static struct clk usb_hhc_ck1510 = {
	.name		= "usb_hhc_ck",
	/* Direct from ULPD, no parent */
	.rate		= 48000000, /* Actually 2 clocks, 12MHz and 48MHz */
	.flags		= CLOCK_IN_OMAP1510 |
			  RATE_FIXED | ENABLE_REG_32BIT,
	.enable_reg	= MOD_CONF_CTRL_0,
	.enable_bit	= USB_HOST_HHC_UHOST_EN,
};

static struct clk usb_hhc_ck16xx = {
	.name		= "usb_hhc_ck",
	/* Direct from ULPD, no parent */
	.rate		= 48000000,
	/* OTG_SYSCON_2.OTG_PADEN == 0 (not 1510-compatible) */
	.flags		= CLOCK_IN_OMAP16XX |
			  RATE_FIXED | ENABLE_REG_32BIT,
	.enable_reg	= OTG_BASE + 0x08 /* OTG_SYSCON_2 */,
	.enable_bit	= 8 /* UHOST_EN */,
};

static struct clk usb_dc_ck = {
	.name		= "usb_dc_ck",
	/* Direct from ULPD, no parent */
	.rate		= 48000000,
	.flags		= CLOCK_IN_OMAP16XX | RATE_FIXED,
	.enable_reg	= SOFT_REQ_REG,
	.enable_bit	= 4,
};

static struct clk mclk_1510 = {
	.name		= "mclk",
	/* Direct from ULPD, no parent. May be enabled by ext hardware. */
	.rate		= 12000000,
	.flags		= CLOCK_IN_OMAP1510 | RATE_FIXED,
};

static struct clk mclk_16xx = {
	.name		= "mclk",
	/* Direct from ULPD, no parent. May be enabled by ext hardware. */
	.flags		= CLOCK_IN_OMAP16XX,
	.enable_reg	= COM_CLK_DIV_CTRL_SEL,
	.enable_bit	= COM_ULPD_PLL_CLK_REQ,
	.set_rate	= &set_ext_clk_rate,
	.round_rate	= &round_ext_clk_rate,
	.init		= &init_ext_clk,
};

static struct clk bclk_1510 = {
	.name		= "bclk",
	/* Direct from ULPD, no parent. May be enabled by ext hardware. */
	.rate		= 12000000,
	.flags		= CLOCK_IN_OMAP1510 | RATE_FIXED,
};

static struct clk bclk_16xx = {
	.name		= "bclk",
	/* Direct from ULPD, no parent. May be enabled by ext hardware. */
	.flags		= CLOCK_IN_OMAP16XX,
	.enable_reg	= SWD_CLK_DIV_CTRL_SEL,
	.enable_bit	= SWD_ULPD_PLL_CLK_REQ,
	.set_rate	= &set_ext_clk_rate,
	.round_rate	= &round_ext_clk_rate,
	.init		= &init_ext_clk,
};

static struct clk mmc1_ck = {
	.name		= "mmc1_ck",
	/* Functional clock is direct from ULPD, interface clock is ARMPER */
	.parent		= &armper_ck,
	.rate		= 48000000,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  RATE_FIXED | ENABLE_REG_32BIT,
	.enable_reg	= MOD_CONF_CTRL_0,
	.enable_bit	= 23,
};

static struct clk mmc2_ck = {
	.name		= "mmc2_ck",
	/* Functional clock is direct from ULPD, interface clock is ARMPER */
	.parent		= &armper_ck,
	.rate		= 48000000,
	.flags		= CLOCK_IN_OMAP16XX |
			  RATE_FIXED | ENABLE_REG_32BIT,
	.enable_reg	= MOD_CONF_CTRL_0,
	.enable_bit	= 20,
};

static struct clk virtual_ck_mpu = {
	.name		= "mpu",
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  VIRTUAL_CLOCK | ALWAYS_ENABLED,
	.parent		= &arm_ck, /* Is smarter alias for */
	.recalc		= &followparent_recalc,
	.set_rate	= &select_table_rate,
	.round_rate	= &round_to_table_rate,
};


static struct clk *  onchip_clks[] = {
	/* non-ULPD clocks */
	&ck_ref,
	&ck_dpll1,
	/* CK_GEN1 clocks */
	&ck_dpll1out,
	&arm_ck,
	&armper_ck,
	&arm_gpio_ck,
	&armxor_ck,
	&armtim_ck,
	&armwdt_ck,
	&arminth_ck1510,  &arminth_ck16xx,
	/* CK_GEN2 clocks */
	&dsp_ck,
	&dspmmu_ck,
	&dspper_ck,
	&dspxor_ck,
	&dsptim_ck,
	/* CK_GEN3 clocks */
	&tc_ck,
	&tipb_ck,
	&l3_ocpi_ck,
	&tc1_ck,
	&tc2_ck,
	&dma_ck,
	&dma_lcdfree_ck,
	&api_ck,
	&lb_ck,
	&rhea1_ck,
	&rhea2_ck,
	&lcd_ck,
	/* ULPD clocks */
	&uart1_1510,
	&uart1_16xx,
	&uart2_ck,
	&uart3_1510,
	&uart3_16xx,
	&usb_clko,
	&usb_hhc_ck1510, &usb_hhc_ck16xx,
	&usb_dc_ck,
	&mclk_1510,  &mclk_16xx,
	&bclk_1510,  &bclk_16xx,
	&mmc1_ck,
	&mmc2_ck,
	/* Virtual clocks */
	&virtual_ck_mpu,
};

struct clk *clk_get(struct device *dev, const char *id)
{
	struct clk *p, *clk = ERR_PTR(-ENOENT);

	down(&clocks_sem);
	list_for_each_entry(p, &clocks, node) {
		if (strcmp(id, p->name) == 0 && try_module_get(p->owner)) {
			clk = p;
			break;
		}
	}
	up(&clocks_sem);

	return clk;
}
EXPORT_SYMBOL(clk_get);


void clk_put(struct clk *clk)
{
	if (clk && !IS_ERR(clk))
		module_put(clk->owner);
}
EXPORT_SYMBOL(clk_put);


int __clk_enable(struct clk *clk)
{
	__u16 regval16;
	__u32 regval32;

	if (clk->flags & ALWAYS_ENABLED)
		return 0;

	if (unlikely(clk->enable_reg == 0)) {
		printk(KERN_ERR "clock.c: Enable for %s without enable code\n",
		       clk->name);
		return 0;
	}

	if (clk->flags & DSP_DOMAIN_CLOCK) {
		__clk_use(&api_ck);
	}

	if (clk->flags & ENABLE_REG_32BIT) {
		if (clk->flags & VIRTUAL_IO_ADDRESS) {
			regval32 = __raw_readl(clk->enable_reg);
			regval32 |= (1 << clk->enable_bit);
			__raw_writel(regval32, clk->enable_reg);
		} else {
			regval32 = omap_readl(clk->enable_reg);
			regval32 |= (1 << clk->enable_bit);
			omap_writel(regval32, clk->enable_reg);
		}
	} else {
		if (clk->flags & VIRTUAL_IO_ADDRESS) {
			regval16 = __raw_readw(clk->enable_reg);
			regval16 |= (1 << clk->enable_bit);
			__raw_writew(regval16, clk->enable_reg);
		} else {
			regval16 = omap_readw(clk->enable_reg);
			regval16 |= (1 << clk->enable_bit);
			omap_writew(regval16, clk->enable_reg);
		}
	}

	if (clk->flags & DSP_DOMAIN_CLOCK) {
		__clk_unuse(&api_ck);
	}

	return 0;
}


void __clk_disable(struct clk *clk)
{
	__u16 regval16;
	__u32 regval32;

	if (clk->enable_reg == 0)
		return;

	if (clk->flags & DSP_DOMAIN_CLOCK) {
		__clk_use(&api_ck);
	}

	if (clk->flags & ENABLE_REG_32BIT) {
		if (clk->flags & VIRTUAL_IO_ADDRESS) {
			regval32 = __raw_readl(clk->enable_reg);
			regval32 &= ~(1 << clk->enable_bit);
			__raw_writel(regval32, clk->enable_reg);
		} else {
			regval32 = omap_readl(clk->enable_reg);
			regval32 &= ~(1 << clk->enable_bit);
			omap_writel(regval32, clk->enable_reg);
		}
	} else {
		if (clk->flags & VIRTUAL_IO_ADDRESS) {
			regval16 = __raw_readw(clk->enable_reg);
			regval16 &= ~(1 << clk->enable_bit);
			__raw_writew(regval16, clk->enable_reg);
		} else {
			regval16 = omap_readw(clk->enable_reg);
			regval16 &= ~(1 << clk->enable_bit);
			omap_writew(regval16, clk->enable_reg);
		}
	}

	if (clk->flags & DSP_DOMAIN_CLOCK) {
		__clk_unuse(&api_ck);
	}
}


void __clk_unuse(struct clk *clk)
{
	if (clk->usecount > 0 && !(--clk->usecount)) {
		__clk_disable(clk);
		if (likely(clk->parent))
			__clk_unuse(clk->parent);
	}
}


int __clk_use(struct clk *clk)
{
	int ret = 0;
	if (clk->usecount++ == 0) {
		if (likely(clk->parent))
			ret = __clk_use(clk->parent);

		if (unlikely(ret != 0)) {
			clk->usecount--;
			return ret;
		}

		ret = __clk_enable(clk);

		if (unlikely(ret != 0) && clk->parent) {
			__clk_unuse(clk->parent);
			clk->usecount--;
		}
	}

	return ret;
}


int clk_enable(struct clk *clk)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&clockfw_lock, flags);
	ret = __clk_enable(clk);
	spin_unlock_irqrestore(&clockfw_lock, flags);
	return ret;
}
EXPORT_SYMBOL(clk_enable);


void clk_disable(struct clk *clk)
{
	unsigned long flags;

	spin_lock_irqsave(&clockfw_lock, flags);
	__clk_disable(clk);
	spin_unlock_irqrestore(&clockfw_lock, flags);
}
EXPORT_SYMBOL(clk_disable);


int clk_use(struct clk *clk)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&clockfw_lock, flags);
	ret = __clk_use(clk);
	spin_unlock_irqrestore(&clockfw_lock, flags);
	return ret;
}
EXPORT_SYMBOL(clk_use);


void clk_unuse(struct clk *clk)
{
	unsigned long flags;

	spin_lock_irqsave(&clockfw_lock, flags);
	__clk_unuse(clk);
	spin_unlock_irqrestore(&clockfw_lock, flags);
}
EXPORT_SYMBOL(clk_unuse);


int clk_get_usecount(struct clk *clk)
{
        return clk->usecount;
}
EXPORT_SYMBOL(clk_get_usecount);


unsigned long clk_get_rate(struct clk *clk)
{
	return clk->rate;
}
EXPORT_SYMBOL(clk_get_rate);


static __u16 verify_ckctl_value(__u16 newval)
{
	/* This function checks for following limitations set
	 * by the hardware (all conditions must be true):
	 * DSPMMU_CK == DSP_CK  or  DSPMMU_CK == DSP_CK/2
	 * ARM_CK >= TC_CK
	 * DSP_CK >= TC_CK
	 * DSPMMU_CK >= TC_CK
	 *
	 * In addition following rules are enforced:
	 * LCD_CK <= TC_CK
	 * ARMPER_CK <= TC_CK
	 *
	 * However, maximum frequencies are not checked for!
	 */
	__u8 per_exp;
	__u8 lcd_exp;
	__u8 arm_exp;
	__u8 dsp_exp;
	__u8 tc_exp;
	__u8 dspmmu_exp;

	per_exp = (newval >> CKCTL_PERDIV_OFFSET) & 3;
	lcd_exp = (newval >> CKCTL_LCDDIV_OFFSET) & 3;
	arm_exp = (newval >> CKCTL_ARMDIV_OFFSET) & 3;
	dsp_exp = (newval >> CKCTL_DSPDIV_OFFSET) & 3;
	tc_exp = (newval >> CKCTL_TCDIV_OFFSET) & 3;
	dspmmu_exp = (newval >> CKCTL_DSPMMUDIV_OFFSET) & 3;

	if (dspmmu_exp < dsp_exp)
		dspmmu_exp = dsp_exp;
	if (dspmmu_exp > dsp_exp+1)
		dspmmu_exp = dsp_exp+1;
	if (tc_exp < arm_exp)
		tc_exp = arm_exp;
	if (tc_exp < dspmmu_exp)
		tc_exp = dspmmu_exp;
	if (tc_exp > lcd_exp)
		lcd_exp = tc_exp;
	if (tc_exp > per_exp)
		per_exp = tc_exp;

	newval &= 0xf000;
	newval |= per_exp << CKCTL_PERDIV_OFFSET;
	newval |= lcd_exp << CKCTL_LCDDIV_OFFSET;
	newval |= arm_exp << CKCTL_ARMDIV_OFFSET;
	newval |= dsp_exp << CKCTL_DSPDIV_OFFSET;
	newval |= tc_exp << CKCTL_TCDIV_OFFSET;
	newval |= dspmmu_exp << CKCTL_DSPMMUDIV_OFFSET;

	return newval;
}


static int calc_dsor_exp(struct clk *clk, unsigned long rate)
{
	/* Note: If target frequency is too low, this function will return 4,
	 * which is invalid value. Caller must check for this value and act
	 * accordingly.
	 *
	 * Note: This function does not check for following limitations set
	 * by the hardware (all conditions must be true):
	 * DSPMMU_CK == DSP_CK  or  DSPMMU_CK == DSP_CK/2
	 * ARM_CK >= TC_CK
	 * DSP_CK >= TC_CK
	 * DSPMMU_CK >= TC_CK
	 */
	unsigned long realrate;
	struct clk *  parent;
	unsigned  dsor_exp;

	if (unlikely(!(clk->flags & RATE_CKCTL)))
		return -EINVAL;

	parent = clk->parent;
	if (unlikely(parent == 0))
		return -EIO;

	realrate = parent->rate;
	for (dsor_exp=0; dsor_exp<4; dsor_exp++) {
		if (realrate <= rate)
			break;

		realrate /= 2;
	}

	return dsor_exp;
}


static void ckctl_recalc(struct clk *  clk)
{
	int dsor;

	/* Calculate divisor encoded as 2-bit exponent */
	if (clk->flags & DSP_DOMAIN_CLOCK) {
		/* The clock control bits are in DSP domain,
		 * so api_ck is needed for access.
		 * Note that DSP_CKCTL virt addr = phys addr, so
		 * we must use __raw_readw() instead of omap_readw().
		 */
		__clk_use(&api_ck);
		dsor = 1 << (3 & (__raw_readw(DSP_CKCTL) >> clk->rate_offset));
		__clk_unuse(&api_ck);
	} else {
		dsor = 1 << (3 & (omap_readw(ARM_CKCTL) >> clk->rate_offset));
	}
	if (unlikely(clk->rate == clk->parent->rate / dsor))
		return; /* No change, quick exit */
	clk->rate = clk->parent->rate / dsor;

	if (unlikely(clk->flags & RATE_PROPAGATES))
		propagate_rate(clk);
}


long clk_round_rate(struct clk *clk, unsigned long rate)
{
	int dsor_exp;

	if (clk->flags & RATE_FIXED)
		return clk->rate;

	if (clk->flags & RATE_CKCTL) {
		dsor_exp = calc_dsor_exp(clk, rate);
		if (dsor_exp < 0)
			return dsor_exp;
		if (dsor_exp > 3)
			dsor_exp = 3;
		return clk->parent->rate / (1 << dsor_exp);
	}

	if(clk->round_rate != 0)
		return clk->round_rate(clk, rate);

	return clk->rate;
}
EXPORT_SYMBOL(clk_round_rate);


static void propagate_rate(struct clk *  clk)
{
	struct clk **  clkp;

	for (clkp = onchip_clks; clkp < onchip_clks+ARRAY_SIZE(onchip_clks); clkp++) {
		if (likely((*clkp)->parent != clk)) continue;
		if (likely((*clkp)->recalc))
			(*clkp)->recalc(*clkp);
	}
}


static int select_table_rate(struct clk *  clk, unsigned long rate)
{
	/* Find the highest supported frequency <= rate and switch to it */
	struct mpu_rate *  ptr;

	if (clk != &virtual_ck_mpu)
		return -EINVAL;

	for (ptr = rate_table; ptr->rate; ptr++) {
		if (ptr->xtal != ck_ref.rate)
			continue;

		/* DPLL1 cannot be reprogrammed without risking system crash */
		if (likely(ck_dpll1.rate!=0) && ptr->pll_rate != ck_dpll1.rate)
			continue;

		/* Can check only after xtal frequency check */
		if (ptr->rate <= rate)
			break;
	}

	if (!ptr->rate)
		return -EINVAL;

	/*
	 * In most cases we should not need to reprogram DPLL.
	 * Reprogramming the DPLL is tricky, it must be done from SRAM.
	 */
	omap_sram_reprogram_clock(ptr->dpllctl_val, ptr->ckctl_val);

	ck_dpll1.rate = ptr->pll_rate;
	propagate_rate(&ck_dpll1);
	return 0;
}


static long round_to_table_rate(struct clk *  clk, unsigned long rate)
{
	/* Find the highest supported frequency <= rate */
	struct mpu_rate *  ptr;
	long  highest_rate;

	if (clk != &virtual_ck_mpu)
		return -EINVAL;

	highest_rate = -EINVAL;

	for (ptr = rate_table; ptr->rate; ptr++) {
		if (ptr->xtal != ck_ref.rate)
			continue;

		highest_rate = ptr->rate;

		/* Can check only after xtal frequency check */
		if (ptr->rate <= rate)
			break;
	}

	return highest_rate;
}


int clk_set_rate(struct clk *clk, unsigned long rate)
{
	int  ret = -EINVAL;
	int  dsor_exp;
	__u16  regval;
	unsigned long  flags;

	if (clk->flags & RATE_CKCTL) {
		dsor_exp = calc_dsor_exp(clk, rate);
		if (dsor_exp > 3)
			dsor_exp = -EINVAL;
		if (dsor_exp < 0)
			return dsor_exp;

		spin_lock_irqsave(&clockfw_lock, flags);
		regval = omap_readw(ARM_CKCTL);
		regval &= ~(3 << clk->rate_offset);
		regval |= dsor_exp << clk->rate_offset;
		regval = verify_ckctl_value(regval);
		omap_writew(regval, ARM_CKCTL);
		clk->rate = clk->parent->rate / (1 << dsor_exp);
		spin_unlock_irqrestore(&clockfw_lock, flags);
		ret = 0;
	} else if(clk->set_rate != 0) {
		spin_lock_irqsave(&clockfw_lock, flags);
		ret = clk->set_rate(clk, rate);
		spin_unlock_irqrestore(&clockfw_lock, flags);
	}

	if (unlikely(ret == 0 && (clk->flags & RATE_PROPAGATES)))
		propagate_rate(clk);

	return ret;
}
EXPORT_SYMBOL(clk_set_rate);


static unsigned calc_ext_dsor(unsigned long rate)
{
	unsigned dsor;

	/* MCLK and BCLK divisor selection is not linear:
	 * freq = 96MHz / dsor
	 *
	 * RATIO_SEL range: dsor <-> RATIO_SEL
	 * 0..6: (RATIO_SEL+2) <-> (dsor-2)
	 * 6..48:  (8+(RATIO_SEL-6)*2) <-> ((dsor-8)/2+6)
	 * Minimum dsor is 2 and maximum is 96. Odd divisors starting from 9
	 * can not be used.
	 */
	for (dsor = 2; dsor < 96; ++dsor) {
		if ((dsor & 1) && dsor > 8)
		  	continue;
		if (rate >= 96000000 / dsor)
			break;
	}
	return dsor;
}

/* Only needed on 1510 */
static int set_uart_rate(struct clk * clk, unsigned long rate)
{
	unsigned int val;

	val = omap_readl(clk->enable_reg);
	if (rate == 12000000)
		val &= ~(1 << clk->enable_bit);
	else if (rate == 48000000)
		val |= (1 << clk->enable_bit);
	else
		return -EINVAL;
	omap_writel(val, clk->enable_reg);
	clk->rate = rate;

	return 0;
}

static int set_ext_clk_rate(struct clk *  clk, unsigned long rate)
{
	unsigned dsor;
	__u16 ratio_bits;

	dsor = calc_ext_dsor(rate);
	clk->rate = 96000000 / dsor;
	if (dsor > 8)
		ratio_bits = ((dsor - 8) / 2 + 6) << 2;
	else
		ratio_bits = (dsor - 2) << 2;

	ratio_bits |= omap_readw(clk->enable_reg) & ~0xfd;
	omap_writew(ratio_bits, clk->enable_reg);

	return 0;
}


static long round_ext_clk_rate(struct clk *  clk, unsigned long rate)
{
	return 96000000 / calc_ext_dsor(rate);
}


static void init_ext_clk(struct clk *  clk)
{
	unsigned dsor;
	__u16 ratio_bits;

	/* Determine current rate and ensure clock is based on 96MHz APLL */
	ratio_bits = omap_readw(clk->enable_reg) & ~1;
	omap_writew(ratio_bits, clk->enable_reg);

	ratio_bits = (ratio_bits & 0xfc) >> 2;
	if (ratio_bits > 6)
		dsor = (ratio_bits - 6) * 2 + 8;
	else
		dsor = ratio_bits + 2;

	clk-> rate = 96000000 / dsor;
}


int clk_register(struct clk *clk)
{
	down(&clocks_sem);
	list_add(&clk->node, &clocks);
	if (clk->init)
		clk->init(clk);
	up(&clocks_sem);
	return 0;
}
EXPORT_SYMBOL(clk_register);

void clk_unregister(struct clk *clk)
{
	down(&clocks_sem);
	list_del(&clk->node);
	up(&clocks_sem);
}
EXPORT_SYMBOL(clk_unregister);

#ifdef CONFIG_OMAP_RESET_CLOCKS
/*
 * Resets some clocks that may be left on from bootloader,
 * but leaves serial clocks on. See also omap_late_clk_reset().
 */
static inline void omap_early_clk_reset(void)
{
	//omap_writel(0x3 << 29, MOD_CONF_CTRL_0);
}
#else
#define omap_early_clk_reset()	{}
#endif

int __init clk_init(void)
{
	struct clk **  clkp;
	const struct omap_clock_config *info;
	int crystal_type = 0; /* Default 12 MHz */

	omap_early_clk_reset();

	for (clkp = onchip_clks; clkp < onchip_clks+ARRAY_SIZE(onchip_clks); clkp++) {
		if (((*clkp)->flags &CLOCK_IN_OMAP1510) && cpu_is_omap1510()) {
			clk_register(*clkp);
			continue;
		}

		if (((*clkp)->flags &CLOCK_IN_OMAP16XX) && cpu_is_omap16xx()) {
			clk_register(*clkp);
			continue;
		}

		if (((*clkp)->flags &CLOCK_IN_OMAP730) && cpu_is_omap730()) {
			clk_register(*clkp);
			continue;
		}
	}

	info = omap_get_config(OMAP_TAG_CLOCK, struct omap_clock_config);
	if (info != NULL) {
		if (!cpu_is_omap1510())
			crystal_type = info->system_clock_type;
	}

#if defined(CONFIG_ARCH_OMAP730)
	ck_ref.rate = 13000000;
#elif defined(CONFIG_ARCH_OMAP16XX)
	if (crystal_type == 2)
		ck_ref.rate = 19200000;
#endif

	printk("Clocks: ARM_SYSST: 0x%04x DPLL_CTL: 0x%04x ARM_CKCTL: 0x%04x\n",
	       omap_readw(ARM_SYSST), omap_readw(DPLL_CTL),
	       omap_readw(ARM_CKCTL));

	/* We want to be in syncronous scalable mode */
	omap_writew(0x1000, ARM_SYSST);

#ifdef CONFIG_OMAP_CLOCKS_SET_BY_BOOTLOADER
	/* Use values set by bootloader. Determine PLL rate and recalculate
	 * dependent clocks as if kernel had changed PLL or divisors.
	 */
	{
		unsigned pll_ctl_val = omap_readw(DPLL_CTL);

		ck_dpll1.rate = ck_ref.rate; /* Base xtal rate */
		if (pll_ctl_val & 0x10) {
			/* PLL enabled, apply multiplier and divisor */
			if (pll_ctl_val & 0xf80)
				ck_dpll1.rate *= (pll_ctl_val & 0xf80) >> 7;
			ck_dpll1.rate /= ((pll_ctl_val & 0x60) >> 5) + 1;
		} else {
			/* PLL disabled, apply bypass divisor */
			switch (pll_ctl_val & 0xc) {
			case 0:
				break;
			case 0x4:
				ck_dpll1.rate /= 2;
				break;
			default:
				ck_dpll1.rate /= 4;
				break;
			}
		}
	}
	propagate_rate(&ck_dpll1);
#else
	/* Find the highest supported frequency and enable it */
	if (select_table_rate(&virtual_ck_mpu, ~0)) {
		printk(KERN_ERR "System frequencies not set. Check your config.\n");
		/* Guess sane values (60MHz) */
		omap_writew(0x2290, DPLL_CTL);
		omap_writew(0x1005, ARM_CKCTL);
		ck_dpll1.rate = 60000000;
		propagate_rate(&ck_dpll1);
	}
#endif
	/* Cache rates for clocks connected to ck_ref (not dpll1) */
	propagate_rate(&ck_ref);
	printk(KERN_INFO "Clocking rate (xtal/DPLL1/MPU): "
		"%ld.%01ld/%ld.%01ld/%ld.%01ld MHz\n",
	       ck_ref.rate / 1000000, (ck_ref.rate / 100000) % 10,
	       ck_dpll1.rate / 1000000, (ck_dpll1.rate / 100000) % 10,
	       arm_ck.rate / 1000000, (arm_ck.rate / 100000) % 10);

#ifdef CONFIG_MACH_OMAP_PERSEUS2
	/* Select slicer output as OMAP input clock */
	omap_writew(omap_readw(OMAP730_PCC_UPLD_CTRL) & ~0x1, OMAP730_PCC_UPLD_CTRL);
#endif

	/* Turn off DSP and ARM_TIMXO. Make sure ARM_INTHCK is not divided */
	omap_writew(omap_readw(ARM_CKCTL) & 0x0fff, ARM_CKCTL);

	/* Put DSP/MPUI into reset until needed */
	omap_writew(0, ARM_RSTCT1);
	omap_writew(1, ARM_RSTCT2);
	omap_writew(0x400, ARM_IDLECT1);

	/*
	 * According to OMAP5910 Erratum SYS_DMA_1, bit DMACK_REQ (bit 8)
	 * of the ARM_IDLECT2 register must be set to zero. The power-on
	 * default value of this bit is one.
	 */
	omap_writew(0x0000, ARM_IDLECT2);	/* Turn LCD clock off also */

	/*
	 * Only enable those clocks we will need, let the drivers
	 * enable other clocks as necessary
	 */
	clk_use(&armper_ck);
	clk_use(&armxor_ck);
	clk_use(&armtim_ck);

	if (cpu_is_omap1510())
		clk_enable(&arm_gpio_ck);

	return 0;
}


#ifdef CONFIG_OMAP_RESET_CLOCKS

static int __init omap_late_clk_reset(void)
{
	/* Turn off all unused clocks */
	struct clk *p;
	__u32 regval32;

	/* USB_REQ_EN will be disabled later if necessary (usb_dc_ck) */
	regval32 = omap_readw(SOFT_REQ_REG) & (1 << 4);
	omap_writew(regval32, SOFT_REQ_REG);
	omap_writew(0, SOFT_REQ_REG2);

	list_for_each_entry(p, &clocks, node) {
		if (p->usecount > 0 || (p->flags & ALWAYS_ENABLED) ||
			p->enable_reg == 0)
			continue;

		/* Assume no DSP clocks have been activated by bootloader */
		if (p->flags & DSP_DOMAIN_CLOCK)
			continue;

		/* Is the clock already disabled? */
		if (p->flags & ENABLE_REG_32BIT) {
			if (p->flags & VIRTUAL_IO_ADDRESS)
				regval32 = __raw_readl(p->enable_reg);
			else
				regval32 = omap_readl(p->enable_reg);
		} else {
			if (p->flags & VIRTUAL_IO_ADDRESS)
				regval32 = __raw_readw(p->enable_reg);
			else
				regval32 = omap_readw(p->enable_reg);
		}

		if ((regval32 & (1 << p->enable_bit)) == 0)
			continue;

		/* FIXME: This clock seems to be necessary but no-one
		 * has asked for its activation. */
		if (p == &tc2_ck         // FIX: pm.c (SRAM), CCP, Camera
		    || p == &ck_dpll1out // FIX: SoSSI, SSR
		    || p == &arm_gpio_ck // FIX: GPIO code for 1510
		    ) {
			printk(KERN_INFO "FIXME: Clock \"%s\" seems unused\n",
			       p->name);
			continue;
		}

		printk(KERN_INFO "Disabling unused clock \"%s\"... ", p->name);
		__clk_disable(p);
		printk(" done\n");
	}

	return 0;
}

late_initcall(omap_late_clk_reset);

#endif
