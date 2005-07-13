/*
 *  linux/arch/arm/plat-omap/clock.h
 *
 *  Copyright (C) 2004 Nokia corporation
 *  Written by Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 *  Based on clocks.h by Tony Lindgren, Gordon McNutt and RidgeRun, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_OMAP_CLOCK_H
#define __ARCH_ARM_OMAP_CLOCK_H

struct module;

struct clk {
	struct list_head	node;
	struct module		*owner;
	const char		*name;
	struct clk		*parent;
	unsigned long		rate;
	__s8			usecount;
	__u16			flags;
	__u32			enable_reg;
	__u8			enable_bit;
	__u8			rate_offset;
	void			(*recalc)(struct clk *);
	int			(*set_rate)(struct clk *, unsigned long);
	long			(*round_rate)(struct clk *, unsigned long);
	void			(*init)(struct clk *);
};


struct mpu_rate {
	unsigned long		rate;
	unsigned long		xtal;
	unsigned long		pll_rate;
	__u16			ckctl_val;
	__u16			dpllctl_val;
};


/* Clock flags */
#define RATE_CKCTL		1
#define RATE_FIXED		2
#define RATE_PROPAGATES		4
#define VIRTUAL_CLOCK		8
#define ALWAYS_ENABLED		16
#define ENABLE_REG_32BIT	32
#define CLOCK_IN_OMAP16XX	64
#define CLOCK_IN_OMAP1510	128
#define CLOCK_IN_OMAP730	256
#define DSP_DOMAIN_CLOCK	512
#define VIRTUAL_IO_ADDRESS	1024

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

/* ARM_IDLECT1 bit shifts */
/*#define IDLWDT_ARM	0*/
/*#define IDLXORP_ARM	1*/
/*#define IDLPER_ARM	2*/
/*#define IDLLCD_ARM	3*/
/*#define IDLLB_ARM	4*/
/*#define IDLHSAB_ARM	5*/
/*#define IDLIF_ARM	6*/
/*#define IDLDPLL_ARM	7*/
/*#define IDLAPI_ARM	8*/
/*#define IDLTIM_ARM	9*/
/*#define SETARM_IDLE	11*/

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
#define USB_MCLK_EN_BIT		4	/* In ULPD_CLKC_CTRL */
#define USB_HOST_HHC_UHOST_EN	9	/* In MOD_CONF_CTRL_0 */
#define SWD_ULPD_PLL_CLK_REQ	1	/* In SWD_CLK_DIV_CTRL_SEL */
#define COM_ULPD_PLL_CLK_REQ	1	/* In COM_CLK_DIV_CTRL_SEL */
#define SWD_CLK_DIV_CTRL_SEL	0xfffe0874
#define COM_CLK_DIV_CTRL_SEL	0xfffe0878
#define SOFT_REQ_REG		0xfffe0834
#define SOFT_REQ_REG2		0xfffe0880

int clk_register(struct clk *clk);
void clk_unregister(struct clk *clk);
int clk_init(void);

#endif
