/*
 *  linux/arch/arm/mach-omap1/clock.h
 *
 *  Copyright (C) 2004 - 2005 Nokia corporation
 *  Written by Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 *  Based on clocks.h by Tony Lindgren, Gordon McNutt and RidgeRun, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_OMAP1_CLOCK_H
#define __ARCH_ARM_MACH_OMAP1_CLOCK_H

static int omap1_clk_enable_generic(struct clk * clk);
static void omap1_clk_disable_generic(struct clk * clk);
static void omap1_ckctl_recalc(struct clk * clk);
static void omap1_watchdog_recalc(struct clk * clk);
static int omap1_set_sossi_rate(struct clk *clk, unsigned long rate);
static void omap1_sossi_recalc(struct clk *clk);
static void omap1_ckctl_recalc_dsp_domain(struct clk * clk);
static int omap1_clk_enable_dsp_domain(struct clk * clk);
static int omap1_clk_set_rate_dsp_domain(struct clk * clk, unsigned long rate);
static void omap1_clk_disable_dsp_domain(struct clk * clk);
static int omap1_set_uart_rate(struct clk * clk, unsigned long rate);
static void omap1_uart_recalc(struct clk * clk);
static int omap1_clk_enable_uart_functional(struct clk * clk);
static void omap1_clk_disable_uart_functional(struct clk * clk);
static int omap1_set_ext_clk_rate(struct clk * clk, unsigned long rate);
static long omap1_round_ext_clk_rate(struct clk * clk, unsigned long rate);
static void omap1_init_ext_clk(struct clk * clk);
static int omap1_select_table_rate(struct clk * clk, unsigned long rate);
static long omap1_round_to_table_rate(struct clk * clk, unsigned long rate);
static int omap1_clk_enable(struct clk *clk);
static void omap1_clk_disable(struct clk *clk);

struct mpu_rate {
	unsigned long		rate;
	unsigned long		xtal;
	unsigned long		pll_rate;
	__u16			ckctl_val;
	__u16			dpllctl_val;
};

struct uart_clk {
	struct clk	clk;
	unsigned long	sysc_addr;
};

/* Provide a method for preventing idling some ARM IDLECT clocks */
struct arm_idlect1_clk {
	struct clk	clk;
	unsigned long	no_idle_count;
	__u8		idlect_shift;
};

/* ARM_CKCTL bit shifts */
#define CKCTL_PERDIV_OFFSET	0
#define CKCTL_LCDDIV_OFFSET	2
#define CKCTL_ARMDIV_OFFSET	4
#define CKCTL_DSPDIV_OFFSET	6
#define CKCTL_TCDIV_OFFSET	8
#define CKCTL_DSPMMUDIV_OFFSET	10
/*#define ARM_TIMXO		12*/
#define EN_DSPCK		13
/*#define ARM_INTHCK_SEL	14*/ /* Divide-by-2 for mpu inth_ck */
/* DSP_CKCTL bit shifts */
#define CKCTL_DSPPERDIV_OFFSET	0

/* ARM_IDLECT2 bit shifts */
#define EN_WDTCK	0
#define EN_XORPCK	1
#define EN_PERCK	2
#define EN_LCDCK	3
#define EN_LBCK		4 /* Not on 1610/1710 */
/*#define EN_HSABCK	5*/
#define EN_APICK	6
#define EN_TIMCK	7
#define DMACK_REQ	8
#define EN_GPIOCK	9 /* Not on 1610/1710 */
/*#define EN_LBFREECK	10*/
#define EN_CKOUT_ARM	11

/* ARM_IDLECT3 bit shifts */
#define EN_OCPI_CK	0
#define EN_TC1_CK	2
#define EN_TC2_CK	4

/* DSP_IDLECT2 bit shifts (0,1,2 are same as for ARM_IDLECT2) */
#define EN_DSPTIMCK	5

/* Various register defines for clock controls scattered around OMAP chip */
#define SDW_MCLK_INV_BIT	2	/* In ULPD_CLKC_CTRL */
#define USB_MCLK_EN_BIT		4	/* In ULPD_CLKC_CTRL */
#define USB_HOST_HHC_UHOST_EN	9	/* In MOD_CONF_CTRL_0 */
#define SWD_ULPD_PLL_CLK_REQ	1	/* In SWD_CLK_DIV_CTRL_SEL */
#define COM_ULPD_PLL_CLK_REQ	1	/* In COM_CLK_DIV_CTRL_SEL */
#define SWD_CLK_DIV_CTRL_SEL	0xfffe0874
#define COM_CLK_DIV_CTRL_SEL	0xfffe0878
#define SOFT_REQ_REG		0xfffe0834
#define SOFT_REQ_REG2		0xfffe0880

/*-------------------------------------------------------------------------
 * Omap1 MPU rate table
 *-------------------------------------------------------------------------*/
static struct mpu_rate rate_table[] = {
	/* MPU MHz, xtal MHz, dpll1 MHz, CKCTL, DPLL_CTL
	 * NOTE: Comment order here is different from bits in CKCTL value:
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
	{  48000000, 12000000, 192000000, 0x0baf, 0x2810 }, /* 4/4/4/8/8/8 */
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

/*-------------------------------------------------------------------------
 * Omap1 clocks
 *-------------------------------------------------------------------------*/

static struct clk ck_ref = {
	.name		= "ck_ref",
	.rate		= 12000000,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  CLOCK_IN_OMAP310 | ALWAYS_ENABLED,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk ck_dpll1 = {
	.name		= "ck_dpll1",
	.parent		= &ck_ref,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  CLOCK_IN_OMAP310 | RATE_PROPAGATES | ALWAYS_ENABLED,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct arm_idlect1_clk ck_dpll1out = {
	.clk = {
		.name		= "ck_dpll1out",
		.parent		= &ck_dpll1,
		.flags		= CLOCK_IN_OMAP16XX | CLOCK_IDLE_CONTROL |
				  ENABLE_REG_32BIT | RATE_PROPAGATES,
		.enable_reg	= (void __iomem *)ARM_IDLECT2,
		.enable_bit	= EN_CKOUT_ARM,
		.recalc		= &followparent_recalc,
		.enable		= &omap1_clk_enable_generic,
		.disable	= &omap1_clk_disable_generic,
	},
	.idlect_shift	= 12,
};

static struct clk sossi_ck = {
	.name		= "ck_sossi",
	.parent		= &ck_dpll1out.clk,
	.flags		= CLOCK_IN_OMAP16XX | CLOCK_NO_IDLE_PARENT |
			  ENABLE_REG_32BIT,
	.enable_reg	= (void __iomem *)MOD_CONF_CTRL_1,
	.enable_bit	= 16,
	.recalc		= &omap1_sossi_recalc,
	.set_rate	= &omap1_set_sossi_rate,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk arm_ck = {
	.name		= "arm_ck",
	.parent		= &ck_dpll1,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  CLOCK_IN_OMAP310 | RATE_CKCTL | RATE_PROPAGATES |
			  ALWAYS_ENABLED,
	.rate_offset	= CKCTL_ARMDIV_OFFSET,
	.recalc		= &omap1_ckctl_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct arm_idlect1_clk armper_ck = {
	.clk = {
		.name		= "armper_ck",
		.parent		= &ck_dpll1,
		.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
				  CLOCK_IN_OMAP310 | RATE_CKCTL |
				  CLOCK_IDLE_CONTROL,
		.enable_reg	= (void __iomem *)ARM_IDLECT2,
		.enable_bit	= EN_PERCK,
		.rate_offset	= CKCTL_PERDIV_OFFSET,
		.recalc		= &omap1_ckctl_recalc,
		.enable		= &omap1_clk_enable_generic,
		.disable	= &omap1_clk_disable_generic,
	},
	.idlect_shift	= 2,
};

static struct clk arm_gpio_ck = {
	.name		= "arm_gpio_ck",
	.parent		= &ck_dpll1,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310,
	.enable_reg	= (void __iomem *)ARM_IDLECT2,
	.enable_bit	= EN_GPIOCK,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct arm_idlect1_clk armxor_ck = {
	.clk = {
		.name		= "armxor_ck",
		.parent		= &ck_ref,
		.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
				  CLOCK_IN_OMAP310 | CLOCK_IDLE_CONTROL,
		.enable_reg	= (void __iomem *)ARM_IDLECT2,
		.enable_bit	= EN_XORPCK,
		.recalc		= &followparent_recalc,
		.enable		= &omap1_clk_enable_generic,
		.disable	= &omap1_clk_disable_generic,
	},
	.idlect_shift	= 1,
};

static struct arm_idlect1_clk armtim_ck = {
	.clk = {
		.name		= "armtim_ck",
		.parent		= &ck_ref,
		.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
				  CLOCK_IN_OMAP310 | CLOCK_IDLE_CONTROL,
		.enable_reg	= (void __iomem *)ARM_IDLECT2,
		.enable_bit	= EN_TIMCK,
		.recalc		= &followparent_recalc,
		.enable		= &omap1_clk_enable_generic,
		.disable	= &omap1_clk_disable_generic,
	},
	.idlect_shift	= 9,
};

static struct arm_idlect1_clk armwdt_ck = {
	.clk = {
		.name		= "armwdt_ck",
		.parent		= &ck_ref,
		.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
				  CLOCK_IN_OMAP310 | CLOCK_IDLE_CONTROL,
		.enable_reg	= (void __iomem *)ARM_IDLECT2,
		.enable_bit	= EN_WDTCK,
		.recalc		= &omap1_watchdog_recalc,
		.enable		= &omap1_clk_enable_generic,
		.disable	= &omap1_clk_disable_generic,
	},
	.idlect_shift	= 0,
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
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk dsp_ck = {
	.name		= "dsp_ck",
	.parent		= &ck_dpll1,
	.flags		= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  RATE_CKCTL,
	.enable_reg	= (void __iomem *)ARM_CKCTL,
	.enable_bit	= EN_DSPCK,
	.rate_offset	= CKCTL_DSPDIV_OFFSET,
	.recalc		= &omap1_ckctl_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk dspmmu_ck = {
	.name		= "dspmmu_ck",
	.parent		= &ck_dpll1,
	.flags		= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  RATE_CKCTL | ALWAYS_ENABLED,
	.rate_offset	= CKCTL_DSPMMUDIV_OFFSET,
	.recalc		= &omap1_ckctl_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk dspper_ck = {
	.name		= "dspper_ck",
	.parent		= &ck_dpll1,
	.flags		= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  RATE_CKCTL | VIRTUAL_IO_ADDRESS,
	.enable_reg	= (void __iomem *)DSP_IDLECT2,
	.enable_bit	= EN_PERCK,
	.rate_offset	= CKCTL_PERDIV_OFFSET,
	.recalc		= &omap1_ckctl_recalc_dsp_domain,
	.set_rate	= &omap1_clk_set_rate_dsp_domain,
	.enable		= &omap1_clk_enable_dsp_domain,
	.disable	= &omap1_clk_disable_dsp_domain,
};

static struct clk dspxor_ck = {
	.name		= "dspxor_ck",
	.parent		= &ck_ref,
	.flags		= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  VIRTUAL_IO_ADDRESS,
	.enable_reg	= (void __iomem *)DSP_IDLECT2,
	.enable_bit	= EN_XORPCK,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_dsp_domain,
	.disable	= &omap1_clk_disable_dsp_domain,
};

static struct clk dsptim_ck = {
	.name		= "dsptim_ck",
	.parent		= &ck_ref,
	.flags		= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  VIRTUAL_IO_ADDRESS,
	.enable_reg	= (void __iomem *)DSP_IDLECT2,
	.enable_bit	= EN_DSPTIMCK,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_dsp_domain,
	.disable	= &omap1_clk_disable_dsp_domain,
};

/* Tie ARM_IDLECT1:IDLIF_ARM to this logical clock structure */
static struct arm_idlect1_clk tc_ck = {
	.clk = {
		.name		= "tc_ck",
		.parent		= &ck_dpll1,
		.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
				  CLOCK_IN_OMAP730 | CLOCK_IN_OMAP310 |
				  RATE_CKCTL | RATE_PROPAGATES |
				  ALWAYS_ENABLED | CLOCK_IDLE_CONTROL,
		.rate_offset	= CKCTL_TCDIV_OFFSET,
		.recalc		= &omap1_ckctl_recalc,
		.enable		= &omap1_clk_enable_generic,
		.disable	= &omap1_clk_disable_generic,
	},
	.idlect_shift	= 6,
};

static struct clk arminth_ck1510 = {
	.name		= "arminth_ck",
	.parent		= &tc_ck.clk,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 |
			  ALWAYS_ENABLED,
	.recalc		= &followparent_recalc,
	/* Note: On 1510 the frequency follows TC_CK
	 *
	 * 16xx version is in MPU clocks.
	 */
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk tipb_ck = {
	/* No-idle controlled by "tc_ck" */
	.name		= "tipb_ck",
	.parent		= &tc_ck.clk,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 |
			  ALWAYS_ENABLED,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk l3_ocpi_ck = {
	/* No-idle controlled by "tc_ck" */
	.name		= "l3_ocpi_ck",
	.parent		= &tc_ck.clk,
	.flags		= CLOCK_IN_OMAP16XX,
	.enable_reg	= (void __iomem *)ARM_IDLECT3,
	.enable_bit	= EN_OCPI_CK,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk tc1_ck = {
	.name		= "tc1_ck",
	.parent		= &tc_ck.clk,
	.flags		= CLOCK_IN_OMAP16XX,
	.enable_reg	= (void __iomem *)ARM_IDLECT3,
	.enable_bit	= EN_TC1_CK,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk tc2_ck = {
	.name		= "tc2_ck",
	.parent		= &tc_ck.clk,
	.flags		= CLOCK_IN_OMAP16XX,
	.enable_reg	= (void __iomem *)ARM_IDLECT3,
	.enable_bit	= EN_TC2_CK,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk dma_ck = {
	/* No-idle controlled by "tc_ck" */
	.name		= "dma_ck",
	.parent		= &tc_ck.clk,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  CLOCK_IN_OMAP310 | ALWAYS_ENABLED,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk dma_lcdfree_ck = {
	.name		= "dma_lcdfree_ck",
	.parent		= &tc_ck.clk,
	.flags		= CLOCK_IN_OMAP16XX | ALWAYS_ENABLED,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct arm_idlect1_clk api_ck = {
	.clk = {
		.name		= "api_ck",
		.parent		= &tc_ck.clk,
		.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
				  CLOCK_IN_OMAP310 | CLOCK_IDLE_CONTROL,
		.enable_reg	= (void __iomem *)ARM_IDLECT2,
		.enable_bit	= EN_APICK,
		.recalc		= &followparent_recalc,
		.enable		= &omap1_clk_enable_generic,
		.disable	= &omap1_clk_disable_generic,
	},
	.idlect_shift	= 8,
};

static struct arm_idlect1_clk lb_ck = {
	.clk = {
		.name		= "lb_ck",
		.parent		= &tc_ck.clk,
		.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 |
				  CLOCK_IDLE_CONTROL,
		.enable_reg	= (void __iomem *)ARM_IDLECT2,
		.enable_bit	= EN_LBCK,
		.recalc		= &followparent_recalc,
		.enable		= &omap1_clk_enable_generic,
		.disable	= &omap1_clk_disable_generic,
	},
	.idlect_shift	= 4,
};

static struct clk rhea1_ck = {
	.name		= "rhea1_ck",
	.parent		= &tc_ck.clk,
	.flags		= CLOCK_IN_OMAP16XX | ALWAYS_ENABLED,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk rhea2_ck = {
	.name		= "rhea2_ck",
	.parent		= &tc_ck.clk,
	.flags		= CLOCK_IN_OMAP16XX | ALWAYS_ENABLED,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk lcd_ck_16xx = {
	.name		= "lcd_ck",
	.parent		= &ck_dpll1,
	.flags		= CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP730 | RATE_CKCTL,
	.enable_reg	= (void __iomem *)ARM_IDLECT2,
	.enable_bit	= EN_LCDCK,
	.rate_offset	= CKCTL_LCDDIV_OFFSET,
	.recalc		= &omap1_ckctl_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct arm_idlect1_clk lcd_ck_1510 = {
	.clk = {
		.name		= "lcd_ck",
		.parent		= &ck_dpll1,
		.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 |
				  RATE_CKCTL | CLOCK_IDLE_CONTROL,
		.enable_reg	= (void __iomem *)ARM_IDLECT2,
		.enable_bit	= EN_LCDCK,
		.rate_offset	= CKCTL_LCDDIV_OFFSET,
		.recalc		= &omap1_ckctl_recalc,
		.enable		= &omap1_clk_enable_generic,
		.disable	= &omap1_clk_disable_generic,
	},
	.idlect_shift	= 3,
};

static struct clk uart1_1510 = {
	.name		= "uart1_ck",
	/* Direct from ULPD, no real parent */
	.parent		= &armper_ck.clk,
	.rate		= 12000000,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 |
			  ENABLE_REG_32BIT | ALWAYS_ENABLED |
			  CLOCK_NO_IDLE_PARENT,
	.enable_reg	= (void __iomem *)MOD_CONF_CTRL_0,
	.enable_bit	= 29,	/* Chooses between 12MHz and 48MHz */
	.set_rate	= &omap1_set_uart_rate,
	.recalc		= &omap1_uart_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct uart_clk uart1_16xx = {
	.clk	= {
		.name		= "uart1_ck",
		/* Direct from ULPD, no real parent */
		.parent		= &armper_ck.clk,
		.rate		= 48000000,
		.flags		= CLOCK_IN_OMAP16XX | RATE_FIXED |
				  ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
		.enable_reg	= (void __iomem *)MOD_CONF_CTRL_0,
		.enable_bit	= 29,
		.enable		= &omap1_clk_enable_uart_functional,
		.disable	= &omap1_clk_disable_uart_functional,
	},
	.sysc_addr	= 0xfffb0054,
};

static struct clk uart2_ck = {
	.name		= "uart2_ck",
	/* Direct from ULPD, no real parent */
	.parent		= &armper_ck.clk,
	.rate		= 12000000,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  CLOCK_IN_OMAP310 | ENABLE_REG_32BIT |
			  ALWAYS_ENABLED | CLOCK_NO_IDLE_PARENT,
	.enable_reg	= (void __iomem *)MOD_CONF_CTRL_0,
	.enable_bit	= 30,	/* Chooses between 12MHz and 48MHz */
	.set_rate	= &omap1_set_uart_rate,
	.recalc		= &omap1_uart_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk uart3_1510 = {
	.name		= "uart3_ck",
	/* Direct from ULPD, no real parent */
	.parent		= &armper_ck.clk,
	.rate		= 12000000,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 |
			  ENABLE_REG_32BIT | ALWAYS_ENABLED |
			  CLOCK_NO_IDLE_PARENT,
	.enable_reg	= (void __iomem *)MOD_CONF_CTRL_0,
	.enable_bit	= 31,	/* Chooses between 12MHz and 48MHz */
	.set_rate	= &omap1_set_uart_rate,
	.recalc		= &omap1_uart_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct uart_clk uart3_16xx = {
	.clk	= {
		.name		= "uart3_ck",
		/* Direct from ULPD, no real parent */
		.parent		= &armper_ck.clk,
		.rate		= 48000000,
		.flags		= CLOCK_IN_OMAP16XX | RATE_FIXED |
				  ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
		.enable_reg	= (void __iomem *)MOD_CONF_CTRL_0,
		.enable_bit	= 31,
		.enable		= &omap1_clk_enable_uart_functional,
		.disable	= &omap1_clk_disable_uart_functional,
	},
	.sysc_addr	= 0xfffb9854,
};

static struct clk usb_clko = {	/* 6 MHz output on W4_USB_CLKO */
	.name		= "usb_clko",
	/* Direct from ULPD, no parent */
	.rate		= 6000000,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  CLOCK_IN_OMAP310 | RATE_FIXED | ENABLE_REG_32BIT,
	.enable_reg	= (void __iomem *)ULPD_CLOCK_CTRL,
	.enable_bit	= USB_MCLK_EN_BIT,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk usb_hhc_ck1510 = {
	.name		= "usb_hhc_ck",
	/* Direct from ULPD, no parent */
	.rate		= 48000000, /* Actually 2 clocks, 12MHz and 48MHz */
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 |
			  RATE_FIXED | ENABLE_REG_32BIT,
	.enable_reg	= (void __iomem *)MOD_CONF_CTRL_0,
	.enable_bit	= USB_HOST_HHC_UHOST_EN,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk usb_hhc_ck16xx = {
	.name		= "usb_hhc_ck",
	/* Direct from ULPD, no parent */
	.rate		= 48000000,
	/* OTG_SYSCON_2.OTG_PADEN == 0 (not 1510-compatible) */
	.flags		= CLOCK_IN_OMAP16XX |
			  RATE_FIXED | ENABLE_REG_32BIT,
	.enable_reg	= (void __iomem *)OTG_BASE + 0x08 /* OTG_SYSCON_2 */,
	.enable_bit	= 8 /* UHOST_EN */,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk usb_dc_ck = {
	.name		= "usb_dc_ck",
	/* Direct from ULPD, no parent */
	.rate		= 48000000,
	.flags		= CLOCK_IN_OMAP16XX | RATE_FIXED,
	.enable_reg	= (void __iomem *)SOFT_REQ_REG,
	.enable_bit	= 4,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk mclk_1510 = {
	.name		= "mclk",
	/* Direct from ULPD, no parent. May be enabled by ext hardware. */
	.rate		= 12000000,
 	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 | RATE_FIXED,
 	.enable_reg	= (void __iomem *)SOFT_REQ_REG,
 	.enable_bit	= 6,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk mclk_16xx = {
	.name		= "mclk",
	/* Direct from ULPD, no parent. May be enabled by ext hardware. */
	.flags		= CLOCK_IN_OMAP16XX,
	.enable_reg	= (void __iomem *)COM_CLK_DIV_CTRL_SEL,
	.enable_bit	= COM_ULPD_PLL_CLK_REQ,
	.set_rate	= &omap1_set_ext_clk_rate,
	.round_rate	= &omap1_round_ext_clk_rate,
	.init		= &omap1_init_ext_clk,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk bclk_1510 = {
	.name		= "bclk",
	/* Direct from ULPD, no parent. May be enabled by ext hardware. */
	.rate		= 12000000,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 | RATE_FIXED,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk bclk_16xx = {
	.name		= "bclk",
	/* Direct from ULPD, no parent. May be enabled by ext hardware. */
	.flags		= CLOCK_IN_OMAP16XX,
	.enable_reg	= (void __iomem *)SWD_CLK_DIV_CTRL_SEL,
	.enable_bit	= SWD_ULPD_PLL_CLK_REQ,
	.set_rate	= &omap1_set_ext_clk_rate,
	.round_rate	= &omap1_round_ext_clk_rate,
	.init		= &omap1_init_ext_clk,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk mmc1_ck = {
	.name		= "mmc_ck",
	.id		= 1,
	/* Functional clock is direct from ULPD, interface clock is ARMPER */
	.parent		= &armper_ck.clk,
	.rate		= 48000000,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  CLOCK_IN_OMAP310 | RATE_FIXED | ENABLE_REG_32BIT |
			  CLOCK_NO_IDLE_PARENT,
	.enable_reg	= (void __iomem *)MOD_CONF_CTRL_0,
	.enable_bit	= 23,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk mmc2_ck = {
	.name		= "mmc_ck",
	.id		= 2,
	/* Functional clock is direct from ULPD, interface clock is ARMPER */
	.parent		= &armper_ck.clk,
	.rate		= 48000000,
	.flags		= CLOCK_IN_OMAP16XX |
			  RATE_FIXED | ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
	.enable_reg	= (void __iomem *)MOD_CONF_CTRL_0,
	.enable_bit	= 20,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk virtual_ck_mpu = {
	.name		= "mpu",
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  CLOCK_IN_OMAP310 | VIRTUAL_CLOCK | ALWAYS_ENABLED,
	.parent		= &arm_ck, /* Is smarter alias for */
	.recalc		= &followparent_recalc,
	.set_rate	= &omap1_select_table_rate,
	.round_rate	= &omap1_round_to_table_rate,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

/* virtual functional clock domain for I2C. Just for making sure that ARMXOR_CK
remains active during MPU idle whenever this is enabled */
static struct clk i2c_fck = {
	.name		= "i2c_fck",
	.id		= 1,
	.flags		= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  VIRTUAL_CLOCK | CLOCK_NO_IDLE_PARENT |
			  ALWAYS_ENABLED,
	.parent		= &armxor_ck.clk,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk i2c_ick = {
	.name		= "i2c_ick",
	.id		= 1,
	.flags		= CLOCK_IN_OMAP16XX |
			  VIRTUAL_CLOCK | CLOCK_NO_IDLE_PARENT |
			  ALWAYS_ENABLED,
	.parent		= &armper_ck.clk,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk * onchip_clks[] = {
	/* non-ULPD clocks */
	&ck_ref,
	&ck_dpll1,
	/* CK_GEN1 clocks */
	&ck_dpll1out.clk,
	&sossi_ck,
	&arm_ck,
	&armper_ck.clk,
	&arm_gpio_ck,
	&armxor_ck.clk,
	&armtim_ck.clk,
	&armwdt_ck.clk,
	&arminth_ck1510,  &arminth_ck16xx,
	/* CK_GEN2 clocks */
	&dsp_ck,
	&dspmmu_ck,
	&dspper_ck,
	&dspxor_ck,
	&dsptim_ck,
	/* CK_GEN3 clocks */
	&tc_ck.clk,
	&tipb_ck,
	&l3_ocpi_ck,
	&tc1_ck,
	&tc2_ck,
	&dma_ck,
	&dma_lcdfree_ck,
	&api_ck.clk,
	&lb_ck.clk,
	&rhea1_ck,
	&rhea2_ck,
	&lcd_ck_16xx,
	&lcd_ck_1510.clk,
	/* ULPD clocks */
	&uart1_1510,
	&uart1_16xx.clk,
	&uart2_ck,
	&uart3_1510,
	&uart3_16xx.clk,
	&usb_clko,
	&usb_hhc_ck1510, &usb_hhc_ck16xx,
	&usb_dc_ck,
	&mclk_1510,  &mclk_16xx,
	&bclk_1510,  &bclk_16xx,
	&mmc1_ck,
	&mmc2_ck,
	/* Virtual clocks */
	&virtual_ck_mpu,
	&i2c_fck,
	&i2c_ick,
};

#endif
