/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/mach-omap1/clock.h
 *
 *  Copyright (C) 2004 - 2005, 2009 Nokia corporation
 *  Written by Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 *  Based on clocks.h by Tony Lindgren, Gordon McNutt and RidgeRun, Inc
 */

#ifndef __ARCH_ARM_MACH_OMAP1_CLOCK_H
#define __ARCH_ARM_MACH_OMAP1_CLOCK_H

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>

struct module;
struct omap1_clk;

struct omap_clk {
	u16				cpu;
	struct clk_lookup		lk;
};

#define CLK(dev, con, ck, cp)		\
	{				\
		 .cpu = cp,		\
		.lk = {			\
			.dev_id = dev,	\
			.con_id = con,	\
			.clk_hw = ck,	\
		},			\
	}

/* Platform flags for the clkdev-OMAP integration code */
#define CK_310		(1 << 0)
#define CK_7XX		(1 << 1)	/* 7xx, 850 */
#define CK_1510		(1 << 2)
#define CK_16XX		(1 << 3)	/* 16xx, 17xx, 5912 */
#define CK_1710		(1 << 4)	/* 1710 extra for rate selection */

/**
 * struct clkops - some clock function pointers
 * @enable: fn ptr that enables the current clock in hardware
 * @disable: fn ptr that enables the current clock in hardware
 * @allow_idle: fn ptr that enables autoidle for the current clock in hardware
 */
struct clkops {
	int			(*enable)(struct omap1_clk *clk);
	void			(*disable)(struct omap1_clk *clk);
};

/*
 * struct clk.flags possibilities
 *
 * XXX document the rest of the clock flags here
 */
#define ENABLE_REG_32BIT	(1 << 0)	/* Use 32-bit access */
#define CLOCK_IDLE_CONTROL	(1 << 1)
#define CLOCK_NO_IDLE_PARENT	(1 << 2)

/**
 * struct omap1_clk - OMAP1 struct clk
 * @hw: struct clk_hw for common clock framework integration
 * @ops: struct clkops * for this clock
 * @rate: current clock rate
 * @enable_reg: register to write to enable the clock (see @enable_bit)
 * @recalc: fn ptr that returns the clock's current rate
 * @set_rate: fn ptr that can change the clock's current rate
 * @round_rate: fn ptr that can round the clock's current rate
 * @init: fn ptr to do clock-specific initialization
 * @enable_bit: bitshift to write to enable/disable the clock (see @enable_reg)
 * @fixed_div: when > 0, this clock's rate is its parent's rate / @fixed_div
 * @flags: see "struct clk.flags possibilities" above
 * @rate_offset: bitshift for rate selection bitfield (OMAP1 only)
 */
struct omap1_clk {
	struct clk_hw		hw;
	const struct clkops	*ops;
	unsigned long		rate;
	void __iomem		*enable_reg;
	unsigned long		(*recalc)(struct omap1_clk *clk, unsigned long rate);
	int			(*set_rate)(struct omap1_clk *clk, unsigned long rate,
					    unsigned long p_rate);
	long			(*round_rate)(struct omap1_clk *clk, unsigned long rate,
					      unsigned long *p_rate);
	int			(*init)(struct omap1_clk *clk);
	u8			enable_bit;
	u8			fixed_div;
	u8			flags;
	u8			rate_offset;
};
#define to_omap1_clk(_hw)	container_of(_hw, struct omap1_clk, hw)

void propagate_rate(struct omap1_clk *clk);
unsigned long followparent_recalc(struct omap1_clk *clk, unsigned long p_rate);
unsigned long omap_fixed_divisor_recalc(struct omap1_clk *clk, unsigned long p_rate);

extern struct omap1_clk dummy_ck;

int omap1_clk_init(void);
void omap1_clk_late_init(void);
unsigned long omap1_ckctl_recalc(struct omap1_clk *clk, unsigned long p_rate);
long omap1_round_sossi_rate(struct omap1_clk *clk, unsigned long rate, unsigned long *p_rate);
int omap1_set_sossi_rate(struct omap1_clk *clk, unsigned long rate, unsigned long p_rate);
unsigned long omap1_sossi_recalc(struct omap1_clk *clk, unsigned long p_rate);
unsigned long omap1_ckctl_recalc_dsp_domain(struct omap1_clk *clk, unsigned long p_rate);
int omap1_clk_set_rate_dsp_domain(struct omap1_clk *clk, unsigned long rate,
				  unsigned long p_rate);
long omap1_round_uart_rate(struct omap1_clk *clk, unsigned long rate, unsigned long *p_rate);
int omap1_set_uart_rate(struct omap1_clk *clk, unsigned long rate, unsigned long p_rate);
unsigned long omap1_uart_recalc(struct omap1_clk *clk, unsigned long p_rate);
int omap1_set_ext_clk_rate(struct omap1_clk *clk, unsigned long rate, unsigned long p_rate);
long omap1_round_ext_clk_rate(struct omap1_clk *clk, unsigned long rate, unsigned long *p_rate);
int omap1_init_ext_clk(struct omap1_clk *clk);
int omap1_select_table_rate(struct omap1_clk *clk, unsigned long rate, unsigned long p_rate);
long omap1_round_to_table_rate(struct omap1_clk *clk, unsigned long rate, unsigned long *p_rate);
int omap1_clk_set_rate_ckctl_arm(struct omap1_clk *clk, unsigned long rate, unsigned long p_rate);
long omap1_clk_round_rate_ckctl_arm(struct omap1_clk *clk, unsigned long rate,
				    unsigned long *p_rate);

struct uart_clk {
	struct omap1_clk	clk;
	unsigned long		sysc_addr;
};

/* Provide a method for preventing idling some ARM IDLECT clocks */
struct arm_idlect1_clk {
	struct omap1_clk	clk;
	unsigned long		no_idle_count;
	__u8			idlect_shift;
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

extern __u32 arm_idlect1_mask;
extern struct omap1_clk *api_ck_p, *ck_dpll1_p, *ck_ref_p;

extern const struct clkops clkops_dspck;
extern const struct clkops clkops_uart_16xx;
extern const struct clkops clkops_generic;

/* used for passing SoC type to omap1_{select,round_to}_table_rate() */
extern u32 cpu_mask;

extern const struct clk_ops omap1_clk_null_ops;
extern const struct clk_ops omap1_clk_gate_ops;
extern const struct clk_ops omap1_clk_rate_ops;
extern const struct clk_ops omap1_clk_full_ops;

#endif
