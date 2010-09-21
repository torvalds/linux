/* arch/arm/mach-rk2818/clock.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

//#define DEBUG
#define pr_fmt(fmt) "clock: %s: " fmt, __func__

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/version.h>
#include <asm/clkdev.h>
#include <asm/tcm.h>
#include <mach/rk2818_iomap.h>
#include <mach/scu.h>
#include <mach/iomux.h>	// CPU_APB_REG0

#define CLKSEL0_REG	(u32 __iomem *)(RK2818_SCU_BASE + SCU_CLKSEL0_CON)
#define CLKSEL1_REG	(u32 __iomem *)(RK2818_SCU_BASE + SCU_CLKSEL1_CON)
#define CLKSEL2_REG	(u32 __iomem *)(RK2818_SCU_BASE + SCU_CLKSEL2_CON)

/* SCU PLL CON */
#define PLL_TEST	(0x01u<<25)
#define PLL_SAT		(0x01u<<24)
#define PLL_FAST	(0x01u<<23)
#define PLL_PD		(0x01u<<22)
#define PLL_CLKR(i)	(((i)&0x3f)<<16)
#define PLL_CLKF(i)	(((i)&0x0fff)<<4)
#define PLL_CLKOD(i)	(((i)&0x07)<<1)
#define PLL_BYPASS	(0X01)

/* SCU MODE CON */
#define SCU_CPUMODE_MASK	(0x03u << 2)
#define SCU_CPUMODE_SLOW	(0x00u << 2)
#define SCU_CPUMODE_NORMAL	(0x01u << 2)
#define SCU_CPUMODE_DSLOW	(0x02u << 2)

#define SCU_DSPMODE_MASK	0x03u
#define SCU_DSPMODE_SLOW	0x00u
#define SCU_DSPMODE_NORMAL	0x01u
#define SCU_DSPMODE_DSLOW	0x02u

/* SCU CLK SEL0 CON */
#define CLK_DDR_REG		CLKSEL0_REG
#define CLK_DDR_SRC_MASK	(3 << 28)
#define CLK_DDR_CODPLL		(0 << 28)
#define CLK_DDR_ARMPLL		(1 << 28)
#define CLK_DDR_DSPPLL		(2 << 28)

#define CLK_SENSOR_REG		CLKSEL0_REG
#define CLK_SENSOR_SRC_MASK	(3 << 23)
#define CLK_SENSOR_24M		(0 << 23)
#define CLK_SENSOR_27M		(1 << 23)
#define CLK_SENSOR_48M		(2 << 23)

#define CLK_USBPHY_REG		CLKSEL0_REG
#define CLK_USBPHY_SRC_MASK	(3 << 18)
#define CLK_USBPHY_24M		(0 << 18)
#define CLK_USBPHY_12M		(1 << 18)
#define CLK_USBPHY_48M		(2 << 18)

#define CLK_LCDC_REG		CLKSEL0_REG
#define CLK_LCDC_DIV_SRC_MASK	(3 << 16)
#define CLK_LCDC_ARMPLL		(0 << 16)
#define CLK_LCDC_DSPPLL		(1 << 16)
#define CLK_LCDC_CODPLL		(2 << 16)
#define CLK_LCDC_SRC_MASK	(1 << 7)
#define CLK_LCDC_DIVOUT		(0 << 7)
#define CLK_LCDC_27M		(1 << 7)

/* SCU CLKSEL1 CON */
#define CLK_UART_REG		CLKSEL1_REG
#define CLK_UART_SRC_MASK	(1 << 31)
#define CLK_UART_24M		(0 << 31)
#define CLK_UART_48M		(1 << 31)

#define CLK_DEMOD_REG		CLKSEL1_REG
#define CLK_DEMOD_SRC_MASK	(1 << 26)
#define CLK_DEMOD_DIVOUT	(0 << 26)
#define CLK_DEMOD_27M		(1 << 26)
#define CLK_DEMOD_DIV_SRC_MASK	(3 << 24)
#define CLK_DEMOD_CODPLL	(0 << 24)
#define CLK_DEMOD_ARMPLL	(1 << 24)
#define CLK_DEMOD_DSPPLL	(2 << 24)

#define CLK_CODEC_REG		CLKSEL1_REG
#define CLK_CODEC_SRC_MASK	(1 << 2)
#define CLK_CODEC_CPLLCLK	(0 << 2)
#define CLK_CODEC_12M		(1 << 2)

#define CLK_CPLL_MASK		0x03u
#define CLK_CPLL_SLOW		0x00u
#define CLK_CPLL_NORMAL		0x01u
#define CLK_CPLL_DSLOW		0x02u

/* Clock flags */
/* bit 0 is free */
#define RATE_FIXED		(1 << 1)	/* Fixed clock rate */
#define CONFIG_PARTICIPANT	(1 << 10)	/* Fundamental clock */
#define ENABLE_ON_INIT		(1 << 11)	/* Enable upon framework init */

#define regfile_readl(offset)	readl(RK2818_REGFILE_BASE + offset)

#define scu_readl(offset)	readl(RK2818_SCU_BASE + offset)
#define scu_writel(v, offset)	writel(v, RK2818_SCU_BASE + offset)
#define scu_writel_force(v, offset)	do { u32 _v = v; u32 _count = 5; do { scu_writel(_v, offset); } while (scu_readl(offset) != _v && _count--); } while (0)	/* huangtao: when write SCU_xPLL_CON, first time may failed, so try again. unknown why. */

#define CLK_GATE_INVALID	-1

#define SCU_CLK_MHZ		(1000*1000)

#define ARM_PLL_DELAY		800	// loop.ARM run at 24M,
// 20100719,HSL@RK , from (200*200) to 300*200 for change ddr failed.
#define OTHER_PLL_DELAY		(300*200)	// loop .ARM run at normal.

#define MAYCHG_VDD		0

struct rockchip_pll_set {
	u32	clk_hz;
	u32	pll_con;

	u8	clk48m_div : 4;
	u8	ahb_div : 2;
	u8	apb_div : 2;

	u32	flags;
};

struct clk {
	struct list_head	node;
	const char		*name;
	struct clk		*parent;
	struct list_head	children;
	struct list_head	sibling;	/* node for children */
	unsigned long		rate;
	u32			flags;
	int			(*mode)(struct clk *clk, int on);
	unsigned long		(*recalc)(struct clk *);
	int			(*set_rate)(struct clk *, unsigned long);
	long			(*round_rate)(struct clk *, unsigned long);
	struct clk*		(*get_parent)(struct clk *);	/* get clk's parent from the hardware */
	int			(*set_parent)(struct clk *, struct clk *);
	s16			usecount;
	u8			gate_idx;
	u8			pll_idx;
	u32 __iomem		*clksel_reg;
	u32			clksel_mask;
	u8			clksel_shift;
	u8			clksel_maxdiv;
};

int __tcmdata ddr_disabled = 0;

static void __clk_disable(struct clk *clk);
static void clk_reparent(struct clk *child, struct clk *parent);
static void propagate_rate(struct clk *tclk);

/* Used for clocks that always have same value as the parent clock */
static unsigned long followparent_recalc(struct clk *clk)
{
	return clk->parent->rate;
}

static unsigned long clksel_recalc(struct clk *clk)
{
	u32 div = ((readl(clk->clksel_reg) & clk->clksel_mask) >> clk->clksel_shift) + 1;
	unsigned long rate = clk->parent->rate / div;
	pr_debug("%s new clock rate is %ld (div %d)\n", clk->name, rate, div);
	return rate;
}

static unsigned long clksel_recalc_shift(struct clk *clk)
{
	u32 shift = (readl(clk->clksel_reg) & clk->clksel_mask) >> clk->clksel_shift;
	unsigned long rate = clk->parent->rate >> shift;
	pr_debug("%s new clock rate is %ld (shift %d)\n", clk->name, rate, shift);
	return rate;
}

static int clksel_set_rate(struct clk *clk, unsigned long rate)
{
	u32 div;

	for (div = 1; div <= clk->clksel_maxdiv; div++) {
		u32 new_rate = clk->parent->rate / div;
		if (new_rate <= rate) {
			u32 *reg = clk->clksel_reg;
			u32 v = readl(reg);
			v &= ~clk->clksel_mask;
			v |= (div - 1) << clk->clksel_shift;
			writel(v, reg);
			clk->rate = new_rate;
			pr_debug("clksel_set_rate for clock %s to rate %ld (div %d)\n", clk->name, rate, div);
			return 0;
		}
	}

	return -ENOENT;
}

static int clksel_set_rate_shift(struct clk *clk, unsigned long rate)
{
	u32 shift;

	for (shift = 0; (1 << shift) <= clk->clksel_maxdiv; shift++) {
		u32 new_rate = clk->parent->rate >> shift;
		if (new_rate <= rate) {
			u32 *reg = clk->clksel_reg;
			u32 v = readl(reg);
			v &= ~clk->clksel_mask;
			v |= shift << clk->clksel_shift;
			writel(v, reg);
			clk->rate = new_rate;
			pr_debug("clksel_set_rate for clock %s to rate %ld (shift %d)\n", clk->name, rate, shift);
			return 0;
		}
	}

	return -ENOENT;
}

static struct clk xin24m = {
	.name		= "xin24m",
	.rate		= 24000000,
	.flags		= RATE_FIXED,
	.gate_idx	= CLK_GATE_INVALID,
};

static struct clk clk12m = {
	.name		= "clk12m",
	.rate		= 12000000,
	.parent		= &xin24m,
	.flags		= RATE_FIXED,
	.gate_idx	= CLK_GATE_INVALID,
};

static struct clk extclk = {
	.name		= "extclk",
	.rate		= 27000000,
	.flags		= RATE_FIXED,
	.gate_idx	= CLK_GATE_INVALID,
};

static unsigned long arm_pll_clk_recalc(struct clk *clk)
{
	unsigned long rate;

	if ((scu_readl(SCU_MODE_CON) & SCU_CPUMODE_MASK) == SCU_CPUMODE_NORMAL) {
		u32 v = scu_readl(SCU_APLL_CON);
		u32 OD = ((v >> 1) & 0x7) + 1;
		u32 NF = ((v >> 4) & 0xfff) + 1;
		u32 NR = ((v >> 16) & 0x3f) + 1;
		rate = clk->parent->rate / NR * NF / OD;
		pr_debug("%s new clock rate is %ld NR %d NF %d OD %d\n", clk->name, rate, NR, NF, OD);
	} else {
		rate = clk->parent->rate;
		pr_debug("%s new clock rate is %ld (slow mode)\n", clk->name, rate);
	}

	return rate;
}

static struct clk arm_pll_clk = {
	.name		= "arm_pll",
	.parent		= &xin24m,
	.recalc		= arm_pll_clk_recalc,
	.gate_idx	= CLK_GATE_INVALID,
};

#define ASM_LOOP_INSTRUCTION_NUM    8

void __tcmfunc tcm_udelay(unsigned long usecs, unsigned long arm_freq_mhz)
{
	unsigned int cycle;

	cycle = usecs * arm_freq_mhz / ASM_LOOP_INSTRUCTION_NUM;

	while (cycle--) {
		nop();
		nop();
		nop();
		barrier();
	}
}

#define ARM_PLL_IDX	0
#define DSP_PLL_IDX	1

static void scu_hw_pll_wait_lock(int pll_idx, int delay)
{
	u32 bit = 0x80u << pll_idx;
	while (delay > 0) {
		if (regfile_readl(CPU_APB_REG0) & bit)
			break;
		delay--;
	}
	if (delay == 0 && !ddr_disabled) {
		pr_warning("wait pll bit 0x%x time out!\n", bit);
	}
}

static void arm_pll_clk_slow_mode(int enter)
{
	scu_writel((scu_readl(SCU_MODE_CON) & ~SCU_CPUMODE_MASK) | (enter ? SCU_CPUMODE_SLOW : SCU_CPUMODE_NORMAL), SCU_MODE_CON);
}

#define CLK_HCLK_PCLK_11	0
#define CLK_HCLK_PCLK_21	1
#define CLK_HCLK_PCLK_41	2

#define CLK_ARM_HCLK_11		0
#define CLK_ARM_HCLK_21		1
#define CLK_ARM_HCLK_31		2
#define CLK_ARM_HCLK_41		3

static void scu_hw_set_ahb_apb_div(int ahb_div, int apb_div)
{
	u32 reg_val;
	reg_val = scu_readl(SCU_CLKSEL0_CON) & ~0xF;
	reg_val |= ((apb_div & 0x3) << 2) | ahb_div;
//	pr_debug("set ahb = %s, apb = %s, clksel0 0x%x\n", ahb_div == 0 ? "1:1" : ahb_div == 1 ? "2:1" : ahb_div == 2 ? "3:1" : "4:1", apb_div == 0 ? "1:1" : apb_div == 1 ? "2:1" : apb_div == 2 ? "4:1" : "bad", reg_val);
	scu_writel(reg_val, SCU_CLKSEL0_CON);
	tcm_udelay(1, 24);	/* huangtao: unknown why, but if no delay, system will unstable. */
}

static void scu_hw_set_clk48m_div(int clk48m_div)
{
	u32 reg_val = scu_readl(SCU_CLKSEL2_CON) & ~(0xF << 4);
	reg_val |= ((clk48m_div & 0xF) << 4);
	scu_writel(reg_val, SCU_CLKSEL2_CON);
}

static int rockchip_scu_set_arm_pllclk_hw(const struct rockchip_pll_set *ps)
{
	const int delay = ARM_PLL_DELAY;

	if (ps->clk_hz == 24 * SCU_CLK_MHZ) {
		arm_pll_clk_slow_mode(1);
		/* 20100615,HSL@RK,when power down,set pll out=300 M. */
		scu_writel_force(ps->pll_con, SCU_APLL_CON);
		pr_debug("set 24M clk, arm power down\n");
		__udelay(delay);
		scu_writel_force(scu_readl(SCU_APLL_CON) | PLL_PD, SCU_APLL_CON);
		scu_hw_set_ahb_apb_div(ps->ahb_div, ps->apb_div);
	} else {
		u32 reg_val;
		unsigned long flags;

		local_irq_save(flags);
		/*20100726,HSL@RK,close irq for change ahb = ahb =1. */
		arm_pll_clk_slow_mode(1);
		/* if change arm pll,we change vdd core and ahb,apb div. */
		scu_hw_set_ahb_apb_div(CLK_ARM_HCLK_11, CLK_HCLK_PCLK_11);
		local_irq_restore(flags);

		local_irq_save(flags);
		reg_val = scu_readl(SCU_APLL_CON);
		if (reg_val & PLL_PD) {
			scu_writel_force(reg_val & ~PLL_PD, SCU_APLL_CON);
			pr_debug("arm pll power on, set to %d, delay = %d\n", ps->clk_hz, delay);
			scu_hw_pll_wait_lock(ARM_PLL_IDX, delay * 2);
		}
		local_irq_restore(flags);

		/* XXX:delay for pll state , for 0.3ms , clkf will lock clkf */
		/* 可能屏幕会倒动,如果不修改 clkr和clkf则不用进入slow mode . */
		local_irq_save(flags);
		scu_writel_force(ps->pll_con, SCU_APLL_CON);
		scu_hw_pll_wait_lock(ARM_PLL_IDX, delay);

		scu_hw_set_ahb_apb_div(ps->ahb_div, ps->apb_div);
		scu_hw_set_clk48m_div(ps->clk48m_div);

		arm_pll_clk_slow_mode(0);
		local_irq_restore(flags);
	}

	return 0;
}

#define ARM_PLL(_clk_hz, nr, nf, od, _clk48m_div, _ahb_div, _apb_div, _flags) \
{							\
	.clk_hz		= _clk_hz,			\
	.pll_con	= PLL_SAT | PLL_FAST | PLL_CLKR(nr-1) | PLL_CLKF(nf-1) | PLL_CLKOD(od-1), \
	.clk48m_div	= _clk48m_div - 1,		\
	.ahb_div	= CLK_ARM_HCLK_##_ahb_div,	\
	.apb_div	= CLK_HCLK_PCLK_##_apb_div,	\
	.flags		= _flags,			\
}

static const struct rockchip_pll_set arm_pll[] = {
// clk_hz = 24*clkf/(clkr*clkod) clkr clkf clkod hdiv pdiv flags (pdiv=1,2,4,no 3!!)
	ARM_PLL(576 * SCU_CLK_MHZ, 4, 96, 1, 12, 31, 41, 1),
	ARM_PLL(384 * SCU_CLK_MHZ, 3, 96, 2,  8, 21, 41, 1),
	ARM_PLL(192 * SCU_CLK_MHZ, 4, 96, 3,  4, 21, 21, 1),
	// last item, pll power down. set real clk == SCU_ARM_MID_CLK
	ARM_PLL( 24 * SCU_CLK_MHZ, 4, 48, 1,  6, 31, 21, 0),	// POWER down
};

static int arm_clk_set_rate(struct clk *clk, unsigned long rate)
{
	const struct rockchip_pll_set *ps, *pt;

	/* found the rockchip_pll_set we want. */
	ps = pt = &arm_pll[0];
	while (1) {
		if (pt->clk_hz == rate) {
			ps = pt;
			break;
		}
		// special clk ,exact match.
		if (pt->flags & 1) {
			pt++;
			continue;
		}
		// we are sorted,and ps->clk_hz > pt->clk_hz.
		if ((pt->clk_hz > rate || (rate - pt->clk_hz < ps->clk_hz - rate)))
			ps = pt;
		if (pt->clk_hz < rate || pt->clk_hz == 24 * SCU_CLK_MHZ)
			break;
		pt++;
	}

	if (ps->clk_hz == clk->rate)
		return 0;

	pr_debug("set arm to %d Hz, cur clk = %ld Hz\n", ps->clk_hz, clk->rate);

	if (ps->clk_hz > clk->rate) {
		// increase freq.
		int i;
		for (i = ARRAY_SIZE(arm_pll) - 2; i >= 0; i--) {
			if (ps->clk_hz > arm_pll[i].clk_hz && arm_pll[i].clk_hz > clk->rate) {
				rockchip_scu_set_arm_pllclk_hw(&arm_pll[i]);
			}
		}
	} else {
		// decrease freq.
		int i;
		for (i = 1; i < ARRAY_SIZE(arm_pll); i++) {
			if (ps->clk_hz < arm_pll[i].clk_hz && arm_pll[i].clk_hz < clk->rate) {
				rockchip_scu_set_arm_pllclk_hw(&arm_pll[i]);
			}
		}
	}

	rockchip_scu_set_arm_pllclk_hw(ps);

	/* adjust arm_pll_clk and arm_pll_clk other children's rate */
	arm_pll_clk.rate = arm_pll_clk.recalc(&arm_pll_clk);
	{
		struct clk *clkp;

		list_for_each_entry(clkp, &arm_pll_clk.children, sibling) {
			if (clkp == clk)
				continue;
			if (clkp->recalc)
				clkp->rate = clkp->recalc(clkp);
			propagate_rate(clkp);
		}
	}

	return 0;
}

static struct clk arm_clk = {
	.name		= "arm",
	.parent		= &arm_pll_clk,
	.recalc		= clksel_recalc,
	.set_rate	= arm_clk_set_rate,
	.gate_idx	= CLK_GATE_INVALID,
	.clksel_reg	= CLKSEL2_REG,
	.clksel_mask	= 0xF,
	.clksel_maxdiv	= 16,
};

static struct clk arm_hclk = {
	.name		= "arm_hclk",
	.parent		= &arm_clk,
	.recalc		= clksel_recalc,
	.gate_idx	= CLK_GATE_INVALID,
	.clksel_reg	= CLKSEL0_REG,
	.clksel_mask	= 0x3,
	.clksel_maxdiv	= 4,
};

static struct clk clk48m = {
	.name		= "clk48m",
	.parent		= &arm_clk,
	.flags		= RATE_FIXED,
	.recalc		= clksel_recalc,
	.gate_idx	= CLK_GATE_INVALID,
	.clksel_reg	= CLKSEL2_REG,
	.clksel_mask	= 0xF << 4,
	.clksel_shift	= 4,
};

static struct clk arm_pclk = {
	.name		= "arm_pclk",
	.parent		= &arm_hclk,
	.flags		= ENABLE_ON_INIT,
	.recalc		= clksel_recalc_shift,
	.set_rate	= clksel_set_rate_shift,
	.gate_idx	= CLK_GATE_INVALID,
	.clksel_reg	= CLKSEL0_REG,
	.clksel_mask	= 0x3 << 2,
	.clksel_shift	= 2,
	.clksel_maxdiv	= 4,
};

static unsigned long dsp_pll_clk_recalc(struct clk *clk)
{
	unsigned long rate;

	if ((scu_readl(SCU_MODE_CON) & SCU_DSPMODE_MASK) == SCU_DSPMODE_NORMAL) {
		u32 v = scu_readl(SCU_DPLL_CON);
		u32 OD = ((v >> 1) & 0x7) + 1;
		u32 NF = ((v >> 4) & 0xfff) + 1;
		u32 NR = ((v >> 16) & 0x3f) + 1;
		rate = clk->parent->rate / NR * NF / OD;
		pr_debug("%s new clock rate is %ld NR %d NF %d OD %d\n", clk->name, rate, NR, NF, OD);
	} else {
		rate = clk->parent->rate;
		pr_debug("%s new clock rate is %ld (slow mode)\n", clk->name, rate);
	}

	return rate;
}

static void dsp_pll_clk_slow_mode(int enter)
{
	scu_writel((scu_readl(SCU_MODE_CON) & ~SCU_DSPMODE_MASK) | (enter ? SCU_DSPMODE_SLOW : SCU_DSPMODE_NORMAL), SCU_MODE_CON);
}

static int rockchip_scu_set_dsp_pllclk_hw(const struct rockchip_pll_set *ps)
{
	const int delay = OTHER_PLL_DELAY;

	if (ps->clk_hz == 24 * SCU_CLK_MHZ) {
		dsp_pll_clk_slow_mode(1);
		/* 20100615,HSL@RK,when power down,set pll out=300 M. */
		scu_writel_force(ps->pll_con, SCU_DPLL_CON);
		pr_debug("set 24M clk, dsp power down\n");
		tcm_udelay(1, 600);
		scu_writel_force(scu_readl(SCU_DPLL_CON) | PLL_PD, SCU_DPLL_CON);
	} else {
		u32 reg_val;

		dsp_pll_clk_slow_mode(1);

		reg_val = scu_readl(SCU_DPLL_CON);
		if (reg_val & PLL_PD) {
			scu_writel_force(reg_val & ~PLL_PD, SCU_DPLL_CON);
			pr_debug("dsp pll power on, set to %d, delay = %d\n", ps->clk_hz, delay);
			scu_hw_pll_wait_lock(DSP_PLL_IDX, delay * 2);
		}

		/* XXX:delay for pll state , for 0.3ms , clkf will lock clkf */
		/* 可能屏幕会倒动,如果不修改 clkr和clkf则不用进入slow mode . */
		scu_writel_force(ps->pll_con, SCU_DPLL_CON);
		scu_hw_pll_wait_lock(DSP_PLL_IDX, delay);

		dsp_pll_clk_slow_mode(0);
	}

	return 0;
}

#define DSP_PLL(_clk_hz, nr, nf, od) \
{							\
	.clk_hz		= _clk_hz,			\
	.pll_con	= PLL_SAT | PLL_FAST | PLL_CLKR(nr-1) | PLL_CLKF(nf-1) | PLL_CLKOD(od-1), \
}

static const struct rockchip_pll_set dsp_pll[] = {
// clk_hz = 24*clkf/(clkr*clkod) clkr clkf clkod
	DSP_PLL(600 * SCU_CLK_MHZ, 6, 150, 1),
	DSP_PLL(560 * SCU_CLK_MHZ, 6, 140, 1),
	DSP_PLL(500 * SCU_CLK_MHZ, 6, 125, 1),
	DSP_PLL(450 * SCU_CLK_MHZ, 4,  75, 1),
	DSP_PLL(400 * SCU_CLK_MHZ, 6, 100, 1),
#if MAYCHG_VDD
	DSP_PLL(364 * SCU_CLK_MHZ, 6,  91, 1),	// VDD_110 for ddr 364M will crash.
#endif
	// pll power down.
	DSP_PLL( 24 * SCU_CLK_MHZ, 4,  50, 1),	// POWER down,by the real clk = 300M
};

static int dsp_pll_clk_set_rate(struct clk *clk, unsigned long rate)
{
	const struct rockchip_pll_set *ps, *pt;
	static const struct rockchip_pll_set mid_pll = DSP_PLL(300 * SCU_CLK_MHZ, 4, 50, 1);

	/* found the rockchip_pll_set we want. */
	ps = pt = &dsp_pll[0];
	while (1) {
		if (pt->clk_hz == rate) {
			ps = pt;
			break;
		}
		// we are sorted,and ps->clk_hz > pt->clk_hz.
		if (pt->clk_hz > rate || (rate - pt->clk_hz < ps->clk_hz - rate))
			ps = pt;
		if (pt->clk_hz < rate || pt->clk_hz == 24 * SCU_CLK_MHZ)
			break;
		pt++;
	}

	if (ps->clk_hz == clk->rate)
		return 0;

	pr_debug("set dsp to %d Hz, cur clk = %ld Hz\n", ps->clk_hz, clk->rate);

	if ((rate > mid_pll.clk_hz && mid_pll.clk_hz > clk->rate) ||
	    (rate < mid_pll.clk_hz && mid_pll.clk_hz < clk->rate))
		rockchip_scu_set_dsp_pllclk_hw(&mid_pll);
	rockchip_scu_set_dsp_pllclk_hw(ps);

	return 0;
}

static struct clk dsp_pll_clk = {
	.name		= "dsp_pll",
	.parent		= &xin24m,
	.recalc		= dsp_pll_clk_recalc,
	.set_rate	= dsp_pll_clk_set_rate,
	.gate_idx	= CLK_GATE_INVALID,
};

static unsigned long codec_pll_clk_recalc(struct clk *clk)
{
	unsigned long rate;

	if ((scu_readl(SCU_CLKSEL1_CON) & CLK_CPLL_MASK) == CLK_CPLL_NORMAL) {
		u32 v = scu_readl(SCU_CPLL_CON);
		u32 OD = ((v >> 1) & 0x7) + 1;
		u32 NF = ((v >> 4) & 0xfff) + 1;
		u32 NR = ((v >> 16) & 0x3f) + 1;
		rate = clk->parent->rate / NR * NF / OD;
		pr_debug("%s new clock rate is %ld NR %d NF %d OD %d\n", clk->name, rate, NR, NF, OD);
	} else {
		rate = clk->parent->rate;
		pr_debug("%s new clock rate is %ld (slow mode)\n", clk->name, rate);
	}

	return rate;
}

#if 0
static void codec_pll_clk_slow_mode(int enter) 
{
	scu_writel((scu_readl(SCU_CLKSEL1_CON) & ~CLK_CPLL_MASK) | (enter ? CLK_CPLL_SLOW : CLK_CPLL_NORMAL), SCU_CLKSEL1_CON);
}
#endif

static struct clk codec_pll_clk = {
	.name		= "codec_pll",
	.parent		= &xin24m,
	.recalc		= codec_pll_clk_recalc,
	.gate_idx	= CLK_GATE_INVALID,
};

static struct clk* demod_divider_clk_get_parent(struct clk *clk)
{
	u32 r = readl(CLK_DEMOD_REG) & CLK_DEMOD_DIV_SRC_MASK;
	return (r == CLK_DEMOD_CODPLL) ? &codec_pll_clk : (r == CLK_DEMOD_ARMPLL) ? &arm_pll_clk : (r == CLK_DEMOD_DSPPLL) ? &dsp_pll_clk : clk->parent;
}

static int demod_divider_clk_set_parent(struct clk *clk, struct clk *parent)
{
	u32 r = readl(CLK_DEMOD_REG) & ~CLK_DEMOD_DIV_SRC_MASK;

	if (parent == &codec_pll_clk) {
		r |= CLK_DEMOD_CODPLL;
	} else if (parent == &arm_pll_clk) {
		r |= CLK_DEMOD_ARMPLL;
	} else if (parent == &dsp_pll_clk) {
		r |= CLK_DEMOD_DSPPLL;
	} else {
		return -EINVAL;
	}
	writel(r, CLK_DEMOD_REG);

	return 0;
}

static struct clk demod_divider_clk = {
	.name		= "demod_divider",
	.parent		= &codec_pll_clk,
	.recalc		= clksel_recalc,
	.set_rate	= clksel_set_rate,
	.get_parent	= demod_divider_clk_get_parent,
	.set_parent	= demod_divider_clk_set_parent,
	.gate_idx	= CLK_GATE_INVALID,
	.clksel_reg	= CLKSEL1_REG,
	.clksel_mask	= 0xFF << 16,
	.clksel_shift	= 16,
	.clksel_maxdiv	= 128,
};

static struct clk* demod_clk_get_parent(struct clk *clk)
{
	u32 r = readl(CLK_DEMOD_REG) & CLK_DEMOD_SRC_MASK;
	return (r == CLK_DEMOD_DIVOUT) ? &demod_divider_clk : &extclk;
}

static int demod_clk_set_parent(struct clk *clk, struct clk *parent)
{
	u32 r = readl(CLK_DEMOD_REG) & ~CLK_DEMOD_SRC_MASK;

	if (parent == &extclk) {
		r |= CLK_DEMOD_27M;
	} else if (parent == &demod_divider_clk) {
		r |= CLK_DEMOD_DIVOUT;
	} else {
		return -EINVAL;
	}
	writel(r, CLK_DEMOD_REG);

	return 0;
}

static struct clk demod_clk = {
	.name		= "demod",
	.parent		= &demod_divider_clk,
	.recalc		= followparent_recalc,
	.get_parent	= demod_clk_get_parent,
	.set_parent	= demod_clk_set_parent,
	.gate_idx	= CLK_GATE_INVALID,
};

static struct clk codec_clk = {
	.name		= "codec",
	.parent		= &codec_pll_clk,
	.recalc		= clksel_recalc,
	.set_rate	= clksel_set_rate,
	.gate_idx	= CLK_GATE_INVALID,
	.clksel_reg	= CLKSEL1_REG,
	.clksel_mask	= 0x1F << 3,
	.clksel_shift	= 3,
	.clksel_maxdiv	= 32,
};

static struct clk* lcdc_divider_clk_get_parent(struct clk *clk)
{
	u32 r = readl(CLK_LCDC_REG) & CLK_LCDC_DIV_SRC_MASK;
	return (r == CLK_LCDC_ARMPLL) ? &arm_pll_clk : (r == CLK_LCDC_DSPPLL) ? &dsp_pll_clk : (r == CLK_LCDC_CODPLL) ? &codec_pll_clk : clk->parent;
}

static int lcdc_divider_clk_set_parent(struct clk *clk, struct clk *parent)
{
	u32 r = readl(CLK_LCDC_REG) & ~CLK_LCDC_DIV_SRC_MASK;

	if (parent == &arm_pll_clk) {
		r |= CLK_LCDC_ARMPLL;
	} else if (parent == &dsp_pll_clk) {
		r |= CLK_LCDC_DSPPLL;
	} else if (parent == &codec_pll_clk) {
		r |= CLK_LCDC_CODPLL;
	} else {
		return -EINVAL;
	}
	writel(r, CLK_LCDC_REG);

	return 0;
}

static struct clk lcdc_divider_clk = {
	.name		= "lcdc_divider",
	.parent		= &arm_pll_clk,
	.recalc		= clksel_recalc,
	.set_rate	= clksel_set_rate,
	.get_parent	= lcdc_divider_clk_get_parent,
	.set_parent	= lcdc_divider_clk_set_parent,
	.gate_idx	= CLK_GATE_INVALID,
	.clksel_reg	= CLKSEL0_REG,
	.clksel_mask	= 0xFF << 8,
	.clksel_shift	= 8,
	.clksel_maxdiv	= 128,
};

static struct clk* otgphy_clk_get_parent(struct clk *clk)
{
	u32 r = readl(CLK_USBPHY_REG) & CLK_USBPHY_SRC_MASK;
	return (r == CLK_USBPHY_24M) ? &xin24m : (r == CLK_USBPHY_12M) ? &clk12m : (r == CLK_USBPHY_48M) ? &clk48m : clk->parent;
}

static int otgphy_clk_set_parent(struct clk *clk, struct clk *parent)
{
	u32 r = readl(CLK_USBPHY_REG) & ~CLK_USBPHY_SRC_MASK;

	if (parent == &xin24m) {
		r |= CLK_USBPHY_24M;
	} else if (parent == &clk12m) {
		r |= CLK_USBPHY_12M;
	} else if (parent == &clk48m) {
		r |= CLK_USBPHY_48M;
	} else {
		return -EINVAL;
	}
	writel(r, CLK_USBPHY_REG);

	return 0;
}

static struct clk* lcdc_clk_get_parent(struct clk *clk)
{
	u32 r = readl(CLK_LCDC_REG) & CLK_LCDC_SRC_MASK;
	return (r == CLK_LCDC_DIVOUT) ? &lcdc_divider_clk : &extclk;
}

static int lcdc_clk_set_parent(struct clk *clk, struct clk *parent)
{
	u32 r = readl(CLK_LCDC_REG) & ~CLK_LCDC_SRC_MASK;

	if (parent == &lcdc_divider_clk) {
		r |= CLK_LCDC_DIVOUT;
	} else if (parent == &extclk) {
		r |= CLK_LCDC_27M;
	} else {
		return -EINVAL;
	}
	writel(r, CLK_LCDC_REG);

	return 0;
}

static struct clk* vip_clk_get_parent(struct clk *clk)
{
	u32 r = readl(CLK_SENSOR_REG) & CLK_SENSOR_SRC_MASK;
	return (r == CLK_SENSOR_24M) ? &xin24m : (r == CLK_SENSOR_27M) ? &extclk : (r == CLK_SENSOR_48M) ? &clk48m : clk->parent;
}

static int vip_clk_set_parent(struct clk *clk, struct clk *parent)
{
	u32 r = readl(CLK_SENSOR_REG) & ~CLK_SENSOR_SRC_MASK;

	if (parent == &xin24m) {
		r |= CLK_SENSOR_24M;
	} else if (parent == &extclk) {
		r |= CLK_SENSOR_27M;
	} else if (parent == &clk48m) {
		r |= CLK_SENSOR_48M;
	} else {
		return -EINVAL;
	}
	writel(r, CLK_SENSOR_REG);

	return 0;
}

static struct clk* ddr_clk_get_parent(struct clk *clk)
{
	u32 r = readl(CLK_DDR_REG) & CLK_DDR_SRC_MASK;
	return (r == CLK_DDR_CODPLL) ? &codec_pll_clk : (r == CLK_DDR_ARMPLL) ? &arm_pll_clk : (r == CLK_DDR_DSPPLL) ? &dsp_pll_clk : clk->parent;
}

static int ddr_clk_set_parent(struct clk *clk, struct clk *parent)
{
	u32 r = readl(CLK_DDR_REG) & ~CLK_DDR_SRC_MASK;

	if (parent == &codec_pll_clk) {
		r |= CLK_DDR_CODPLL;
	} else if (parent == &arm_pll_clk) {
		r |= CLK_DDR_ARMPLL;
	} else if (parent == &dsp_pll_clk) {
		r |= CLK_DDR_DSPPLL;
	} else {
		return -EINVAL;
	}
	writel(r, CLK_DDR_REG);

	return 0;
}

static struct clk* i2s_clk_get_parent(struct clk *clk)
{
	u32 r = readl(CLK_CODEC_REG) & CLK_CODEC_SRC_MASK;
	return (r == CLK_CODEC_CPLLCLK) ? &codec_clk : &clk12m;
}

static int i2s_clk_set_parent(struct clk *clk, struct clk *parent)
{
	u32 r = readl(CLK_CODEC_REG) & ~CLK_CODEC_SRC_MASK;

	if (parent == &codec_clk) {
		r |= CLK_CODEC_CPLLCLK;
	} else if (parent == &clk12m) {
		r |= CLK_CODEC_12M;
	} else {
		return -EINVAL;
	}
	writel(r, CLK_CODEC_REG);

	return 0;
}

static int gate_mode(struct clk *clk, int on)
{
	u32 reg;
	int idx = clk->gate_idx;
	u32 v;

	if (idx >= CLK_GATE_MAX)
		return -EINVAL;

	reg = SCU_CLKGATE0_CON;
	reg += (idx >> 5) << 2;
	idx &= 0x1F;

	v = scu_readl(reg);
	if (on) {
		v &= ~(1 << idx);	// clear bit 
	} else {
		v |= (1 << idx);	// set bit
	}
	scu_writel(v, reg);

	return 0;
}

static struct clk* uart_clk_get_parent(struct clk *clk)
{
	u32 r = readl(CLK_UART_REG) & CLK_UART_SRC_MASK;
	return (r == CLK_UART_24M) ? &xin24m : &clk48m;
}

static int uart_clk_set_parent(struct clk *clk, struct clk *parent)
{
	u32 r = readl(CLK_UART_REG) & ~CLK_UART_SRC_MASK;

	if (parent == &xin24m) {
		r |= CLK_UART_24M;
	} else if (parent == &clk48m) {
		r |= CLK_UART_48M;
	} else {
		return -EINVAL;
	}
	writel(r, CLK_UART_REG);

	return 0;
}

#define UART_CLK(n) \
static struct clk uart##n##_clk = { \
	.name		= "uart"#n, \
	.parent		= &xin24m, \
	.mode		= gate_mode, \
	.recalc		= followparent_recalc, \
	.get_parent	= uart_clk_get_parent, \
	.set_parent	= uart_clk_set_parent, \
	.gate_idx	= CLK_GATE_UART##n, \
}

#define GATE_CLK(NAME,PARENT,ID) \
static struct clk NAME##_clk = { \
	.name		= #NAME, \
	.parent		= &PARENT, \
	.mode		= gate_mode, \
	.recalc		= followparent_recalc, \
	.gate_idx	= CLK_GATE_##ID, \
}

GATE_CLK(arm_core, arm_clk, ARM);
GATE_CLK(dma, arm_hclk, DMA);
GATE_CLK(sramarm, arm_hclk, SRAMARM);
GATE_CLK(sramdsp, arm_hclk, SRAMDSP);
GATE_CLK(hif, arm_hclk, HIF);
GATE_CLK(otgbus, arm_hclk, OTGBUS);
static struct clk otgphy_clk = {
	.name		= "otgphy",
	.parent		= &xin24m,
	.mode		= gate_mode,
	.recalc		= followparent_recalc,
	.get_parent	= otgphy_clk_get_parent,
	.set_parent	= otgphy_clk_set_parent,
	.gate_idx	= CLK_GATE_OTGPHY,
};
GATE_CLK(nandc, arm_hclk, NANDC);
GATE_CLK(intc, arm_hclk, INTC);
GATE_CLK(deblocking_rv, arm_hclk, DEBLK);
static struct clk lcdc_clk = {
	.name		= "lcdc",
	.parent		= &lcdc_divider_clk,
	.mode		= gate_mode,
	.recalc		= followparent_recalc,
	.get_parent	= lcdc_clk_get_parent,
	.set_parent	= lcdc_clk_set_parent,
	.gate_idx	= CLK_GATE_LCDC,
};
static struct clk vip_clk = {
	.name		= "vip",
	.parent		= &xin24m,
	.mode		= gate_mode,
	.recalc		= followparent_recalc,
	.get_parent	= vip_clk_get_parent,
	.set_parent	= vip_clk_set_parent,
	.gate_idx	= CLK_GATE_VIP,
};
static struct clk i2s_clk = {
	.name		= "i2s",
	.parent		= &clk12m,
	.mode		= gate_mode,
	.recalc		= followparent_recalc,
	.get_parent	= i2s_clk_get_parent,
	.set_parent	= i2s_clk_set_parent,
	.gate_idx	= CLK_GATE_I2S,
};
static struct clk sdmmc0_clk = {
	.name		= "sdmmc0",
	.parent		= &arm_hclk,
	.mode		= gate_mode,
	.recalc		= clksel_recalc,
	.set_rate	= clksel_set_rate,
	.gate_idx	= CLK_GATE_SDMMC0,
	.clksel_reg	= CLKSEL0_REG,
	.clksel_mask	= 7 << 4,
	.clksel_shift	= 4,
	.clksel_maxdiv	= 8,
};

GATE_CLK(ebrom, arm_hclk, EBROM);
GATE_CLK(gpio0, arm_pclk, GPIO0);
GATE_CLK(gpio1, arm_pclk, GPIO1);
UART_CLK(0);
UART_CLK(1);
GATE_CLK(i2c0, arm_pclk, I2C0);
GATE_CLK(i2c1, arm_pclk, I2C1);
GATE_CLK(spi0, arm_pclk, SPI0);
GATE_CLK(spi1, arm_pclk, SPI1);
GATE_CLK(pwm, arm_pclk, PWM);
GATE_CLK(timer, arm_pclk, TIMER);
GATE_CLK(wdt, arm_pclk, WDT);
GATE_CLK(rtc, arm_pclk, RTC);
static struct clk lsadc_clk = {
	.name		= "lsadc",
	.parent		= &arm_pclk,
	.mode		= gate_mode,
	.recalc		= clksel_recalc,
	.set_rate	= clksel_set_rate,
	.gate_idx	= CLK_GATE_LSADC,
	.clksel_reg	= CLKSEL1_REG,
	.clksel_mask	= 0xFF << 8,
	.clksel_shift	= 8,
	.clksel_maxdiv	= 128,
};
UART_CLK(2);
UART_CLK(3);
static struct clk sdmmc1_clk = {
	.name		= "sdmmc1",
	.parent		= &arm_hclk,
	.mode		= gate_mode,
	.recalc		= clksel_recalc,
	.set_rate	= clksel_set_rate,
	.gate_idx	= CLK_GATE_SDMMC1,
	.clksel_reg	= CLKSEL2_REG,
	.clksel_mask	= 7 << 8,
	.clksel_shift	= 8,
	.clksel_maxdiv	= 8,
};

static unsigned long hsadc_clk_recalc(struct clk *clk)
{
	return clk->parent->rate >> 1;
}

static struct clk hsadc_clk = {
	.name		= "hsadc",
	.parent		= &demod_clk,
	.mode		= gate_mode,
	.recalc		= hsadc_clk_recalc,
	.gate_idx	= CLK_GATE_HSADC,
};
GATE_CLK(sdram_common, arm_hclk, SDRAM_COMMON);
GATE_CLK(sdram_controller, arm_hclk, SDRAM_CONTROLLER);
GATE_CLK(mobile_sdram_controller, arm_hclk, MOBILE_SDRAM_CONTROLLER);
GATE_CLK(lcdc_share_memory, arm_hclk, LCDC_SHARE_MEMORY);
GATE_CLK(lcdc_hclk, arm_hclk, LCDC_HCLK);
GATE_CLK(deblocking_h264, arm_hclk, DEBLK_H264);
GATE_CLK(gpu, arm_hclk, GPU);
GATE_CLK(ddr_hclk, arm_hclk, DDR_HCLK);
static struct clk ddr_clk = {
	.name		= "ddr",
	.parent		= &codec_pll_clk,
	.mode		= gate_mode,
	.recalc		= clksel_recalc_shift,
	.set_rate	= clksel_set_rate_shift,
	.get_parent	= ddr_clk_get_parent,
	.set_parent	= ddr_clk_set_parent,
	.gate_idx	= CLK_GATE_DDR,
	.clksel_reg	= CLKSEL0_REG,
	.clksel_mask	= 0x3 << 30,
	.clksel_shift	= 30,
	.clksel_maxdiv	= 8,
};
GATE_CLK(customized_sdram_controller, arm_hclk, CUSTOMIZED_SDRAM_CONTROLLER);
GATE_CLK(mcdma, arm_hclk, MCDMA);
GATE_CLK(sdram, arm_hclk, SDRAM);
GATE_CLK(ddr_axi, arm_hclk, DDR_AXI);
GATE_CLK(dsp_timer, arm_hclk, DSP_TIMER);
GATE_CLK(dsp_slave, arm_hclk, DSP_SLAVE);
GATE_CLK(dsp_master, arm_hclk, DSP_MASTER);
GATE_CLK(usb_host, clk48m, USB_HOST);

GATE_CLK(armibus, arm_hclk, ARMIBUS);
GATE_CLK(armdbus, arm_hclk, ARMDBUS);
GATE_CLK(dspbus, arm_hclk, DSPBUS);
GATE_CLK(expbus, arm_hclk, EXPBUS);
GATE_CLK(apbbus, arm_hclk, APBBUS);
GATE_CLK(efuse, arm_pclk, EFUSE);
GATE_CLK(dtcm1, arm_clk, DTCM1);
GATE_CLK(dtcm0, arm_clk, DTCM0);
GATE_CLK(itcm, arm_clk, ITCM);
GATE_CLK(videobus, arm_hclk, VIDEOBUS);

#define CLK(dev, con, ck) \
	{ \
		.dev_id = dev, \
		.con_id = con, \
		.clk = ck, \
	}

#define CLK1(name) \
	{ \
		.dev_id = NULL, \
		.con_id = #name, \
		.clk = &name##_clk, \
	}

static struct clk_lookup clks[] = {
	CLK(NULL, "xin24m", &xin24m),
	CLK(NULL, "extclk", &extclk),

	CLK(NULL, "clk12m", &clk12m),
	CLK1(arm_pll),
	CLK1(dsp_pll),
	CLK1(codec_pll),
	CLK1(arm),
	CLK(NULL, "arm_hclk", &arm_hclk),
	CLK(NULL, "clk48m", &clk48m),
	CLK(NULL, "arm_pclk", &arm_pclk),
	CLK1(demod_divider),
	CLK1(demod),
	CLK1(codec),
	CLK1(lcdc_divider),

	CLK1(arm_core),
	CLK1(dma),
	CLK1(sramarm),
	CLK1(sramdsp),
	CLK1(hif),
	CLK1(otgbus),
	CLK1(otgphy),
	CLK1(nandc),
	CLK1(intc),
	CLK1(deblocking_rv),
	CLK1(lcdc),
	CLK1(vip),
	CLK("rk2818_i2s","i2s",&i2s_clk),
	CLK("rk2818_sdmmc.0", "sdmmc", &sdmmc0_clk),
	CLK1(ebrom),
	CLK1(gpio0),
	CLK1(gpio1),
	CLK("rk2818_serial.0", "uart", &uart0_clk),
	CLK("rk2818_serial.1", "uart", &uart1_clk),
	CLK("rk2818_i2c.0", "i2c", &i2c0_clk),
	CLK("rk2818_i2c.1", "i2c", &i2c1_clk),
	CLK("rk2818_spim.0", "spi", &spi0_clk),
	CLK1(spi1),
	CLK1(pwm),
	CLK1(timer),
	CLK1(wdt),
	CLK1(rtc),
	CLK1(lsadc),
	CLK("rk2818_serial.2", "uart", &uart2_clk),
	CLK("rk2818_serial.3", "uart", &uart3_clk),
	CLK("rk2818_sdmmc.1", "sdmmc", &sdmmc1_clk),

	CLK1(hsadc),
	CLK1(sdram_common),
	CLK1(sdram_controller),
	CLK1(mobile_sdram_controller),
	CLK1(lcdc_share_memory),
	CLK1(lcdc_hclk),
	CLK1(deblocking_h264),
	CLK1(gpu),
	CLK1(ddr_hclk),
	CLK1(ddr),
	CLK1(customized_sdram_controller),
	CLK1(mcdma),
	CLK1(sdram),
	CLK1(ddr_axi),
	CLK1(dsp_timer),
	CLK1(dsp_slave),
	CLK1(dsp_master),
	CLK1(usb_host),

	CLK1(armibus),
	CLK1(armdbus),
	CLK1(dspbus),
	CLK1(expbus),
	CLK1(apbbus),
	CLK1(efuse),
	CLK1(dtcm1),
	CLK1(dtcm0),
	CLK1(itcm),
	CLK1(videobus),
};

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);
static DEFINE_SPINLOCK(clockfw_lock);
#define LOCK() do { WARN_ON(in_irq()); if (!irqs_disabled()) spin_lock_bh(&clockfw_lock); } while (0)
#define UNLOCK() do { if (!irqs_disabled()) spin_unlock_bh(&clockfw_lock); } while (0)

static int __clk_enable(struct clk *clk)
{
	int ret = 0;

	if (clk->usecount == 0) {
		if (clk->parent) {
			ret = __clk_enable(clk->parent);
			if (ret)
				return ret;
		}

		if (clk->mode) {
			ret = clk->mode(clk, 1);
			if (ret) {
				if (clk->parent)
					__clk_disable(clk->parent);
				return ret;
			}
		}
		pr_debug("%s enabled\n", clk->name);
	}
	clk->usecount++;

	return ret;
}

int clk_enable(struct clk *clk)
{
	int ret = 0;

	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	LOCK();
	ret = __clk_enable(clk);
	UNLOCK();

	return ret;
}
EXPORT_SYMBOL(clk_enable);

static void __clk_disable(struct clk *clk)
{
	if (--clk->usecount == 0) {
		if (clk->mode)
			clk->mode(clk, 0);
		pr_debug("%s disabled\n", clk->name);
		if (clk->parent)
			__clk_disable(clk->parent);
	}
}

void clk_disable(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return;

	LOCK();
	if (clk->usecount == 0) {
		printk(KERN_ERR "Trying disable clock %s with 0 usecount\n", clk->name);
		WARN_ON(1);
		goto out;
	}

	__clk_disable(clk);

out:
	UNLOCK();
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	unsigned long ret;

	if (clk == NULL || IS_ERR(clk))
		return 0;

	LOCK();
	ret = clk->rate;
	UNLOCK();

	return ret;
}
EXPORT_SYMBOL(clk_get_rate);

/*-------------------------------------------------------------------------
 * Optional clock functions defined in include/linux/clk.h
 *-------------------------------------------------------------------------*/

/* Given a clock and a rate apply a clock specific rounding function */
static long __clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (clk->round_rate)
		return clk->round_rate(clk, rate);

	if (clk->flags & RATE_FIXED)
		printk(KERN_ERR "clock: clk_round_rate called on fixed-rate clock %s\n", clk->name);

	return clk->rate;
}

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	long ret = 0;

	if (clk == NULL || IS_ERR(clk))
		return ret;

	LOCK();
	ret = __clk_round_rate(clk, rate);
	UNLOCK();

	return ret;
}
EXPORT_SYMBOL(clk_round_rate);

/* Set the clock rate for a clock source */
static int __clk_set_rate(struct clk *clk, unsigned long rate)
{
	int ret = -EINVAL;

	pr_debug("set_rate for clock %s to rate %ld\n", clk->name, rate);

	if (clk->flags & CONFIG_PARTICIPANT)
		return -EINVAL;

	if (clk->set_rate)
		ret = clk->set_rate(clk, rate);

	return ret;
}

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	int ret = -EINVAL;

	if (clk == NULL || IS_ERR(clk))
		return ret;

	LOCK();
	if (rate == clk->rate) {
		ret = 0;
		goto out;
	}
	ret = __clk_set_rate(clk, rate);
	if (ret == 0) {
		if (clk->recalc)
			clk->rate = clk->recalc(clk);
		propagate_rate(clk);
	}
out:
	UNLOCK();

	return ret;
}
EXPORT_SYMBOL(clk_set_rate);

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	int ret = -EINVAL;

	if (clk == NULL || IS_ERR(clk) || parent == NULL || IS_ERR(parent))
		return ret;

	if (clk->set_parent == NULL)
		return ret;

	LOCK();
	if (clk->usecount == 0) {
		ret = clk->set_parent(clk, parent);
		if (ret == 0) {
			clk_reparent(clk, parent);
			if (clk->recalc)
				clk->rate = clk->recalc(clk);
			propagate_rate(clk);
		}
	} else
		ret = -EBUSY;
	UNLOCK();

	return ret;
}
EXPORT_SYMBOL(clk_set_parent);

struct clk *clk_get_parent(struct clk *clk)
{
	return clk->parent;
}
EXPORT_SYMBOL(clk_get_parent);

static void clk_reparent(struct clk *child, struct clk *parent)
{
	if (child->parent == parent)
		return;
	pr_debug("%s reparent to %s (was %s)\n", child->name, parent->name, ((child->parent) ? child->parent->name : "NULL"));

	list_del_init(&child->sibling);
	if (parent)
		list_add(&child->sibling, &parent->children);
	child->parent = parent;
}

/* Propagate rate to children */
static void propagate_rate(struct clk *tclk)
{
	struct clk *clkp;

	list_for_each_entry(clkp, &tclk->children, sibling) {
		if (clkp->recalc)
			clkp->rate = clkp->recalc(clkp);
		propagate_rate(clkp);
	}
}

static LIST_HEAD(root_clks);

/**
 * recalculate_root_clocks - recalculate and propagate all root clocks
 *
 * Recalculates all root clocks (clocks with no parent), which if the
 * clock's .recalc is set correctly, should also propagate their rates.
 * Called at init.
 */
static void recalculate_root_clocks(void)
{
	struct clk *clkp;

	list_for_each_entry(clkp, &root_clks, sibling) {
		if (clkp->recalc)
			clkp->rate = clkp->recalc(clkp);
		propagate_rate(clkp);
	}
}

void clk_recalculate_root_clocks(void)
{
	LOCK();
	recalculate_root_clocks();
	UNLOCK();
}

/**
 * clk_preinit - initialize any fields in the struct clk before clk init
 * @clk: struct clk * to initialize
 *
 * Initialize any struct clk fields needed before normal clk initialization
 * can run.  No return value.
 */
static void clk_preinit(struct clk *clk)
{
	INIT_LIST_HEAD(&clk->children);
}

static int clk_register(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	/*
	 * trap out already registered clocks
	 */
	if (clk->node.next || clk->node.prev)
		return 0;

	mutex_lock(&clocks_mutex);

	if (clk->get_parent)
		clk->parent = clk->get_parent(clk);

	if (clk->parent)
		list_add(&clk->sibling, &clk->parent->children);
	else
		list_add(&clk->sibling, &root_clks);

	list_add(&clk->node, &clocks);

	mutex_unlock(&clocks_mutex);

	return 0;
}

static void clk_enable_init_clocks(void)
{
	struct clk *clkp;

	list_for_each_entry(clkp, &clocks, node) {
		if (clkp->flags & ENABLE_ON_INIT)
			clk_enable(clkp);
	}
}

static unsigned int __initdata armclk = 576000000;

/*
 * You can override arm_clk rate with armclk= cmdline option.
 */
static int __init armclk_setup(char *str)
{
	get_option(&str, &armclk);

	if (!armclk)
		return 1;

	if (armclk < 1000)
		armclk *= 1000000;

	return 1;
}
__setup("armclk=", armclk_setup);

extern void rk2818_timer_update_mult(void);

/*
 * Switch the arm_clk rate if specified on cmdline.
 * We cannot do this early until cmdline is parsed.
 */
static int __init rk2818_clk_arch_init(void)
{
	if (!armclk)
		return -EINVAL;

	if (clk_set_rate(&arm_clk, armclk))
		pr_err("*** Unable to set arm_clk rate\n");

	printk(KERN_INFO "Switched to new clocking rate (pll/arm/hclk/pclk): "
	       "%ld/%ld/%ld/%ld MHz\n",
	       arm_pll_clk.rate / 1000000, arm_clk.rate / 1000000,
	       arm_hclk.rate / 1000000, arm_pclk.rate / 1000000);

	/* cpufreq is not active now, so change timer_mult and loops_per_jiffy manually */
	rk2818_timer_update_mult();
	calibrate_delay();
	printk(KERN_INFO "%lu.%02lu BogoMIPS (lpj=%lu)\n", loops_per_jiffy/(500000/HZ), (loops_per_jiffy/(5000/HZ)) % 100, loops_per_jiffy);

	return 0;
}
core_initcall_sync(rk2818_clk_arch_init);

void __init rk2818_clock_init(void)
{
	struct clk_lookup *lk;

	for (lk = clks; lk < clks + ARRAY_SIZE(clks); lk++)
		clk_preinit(lk->clk);

	for (lk = clks; lk < clks + ARRAY_SIZE(clks); lk++) {
		clkdev_add(lk);
		clk_register(lk->clk);
	}

	recalculate_root_clocks();

	printk(KERN_INFO "Clocking rate (pll/arm/hclk/pclk): "
	       "%ld/%ld/%ld/%ld MHz\n",
	       arm_pll_clk.rate / 1000000, arm_clk.rate / 1000000,
	       arm_hclk.rate / 1000000, arm_pclk.rate / 1000000);

	/*
	 * Only enable those clocks we will need, let the drivers
	 * enable other clocks as necessary
	 */
	clk_enable_init_clocks();
}

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static void dump_clock(struct seq_file *s, struct clk *clk, int deep)
{
	struct clk* ck;
	int i;
	unsigned long rate = clk->rate;

	for (i = 0; i < deep; i++)
		seq_printf(s, "    ");

	seq_printf(s, "%-9s ", clk->name);

	if (clk->gate_idx < CLK_GATE_MAX) {
		u32 reg;
		int idx = clk->gate_idx;
		u32 v;

		reg = SCU_CLKGATE0_CON;
		reg += (idx >> 5) << 2;
		idx &= 0x1F;

		v = scu_readl(reg) & (1 << idx);
		
		seq_printf(s, "%s ", v ? "off" : "on ");
	} else
		seq_printf(s, "%s ", clk->usecount ? "on " : "off");

	if (rate >= 1000000) {
		if (rate % 1000000)
			seq_printf(s, "%ld.%06ld MHz", rate / 1000000, rate % 1000000);
		else
			seq_printf(s, "%ld MHz", rate / 1000000);
	} else if (rate >= 1000) {
		if (rate % 1000)
			seq_printf(s, "%ld.%03ld KHz", rate / 1000, rate % 1000);
		else
			seq_printf(s, "%ld KHz", rate / 1000);
	} else {
		seq_printf(s, "%ld Hz", rate);
	}

	seq_printf(s, " usecount = %d", clk->usecount);

	seq_printf(s, " parent = %s\n", clk->parent ? clk->parent->name : "NULL");

	list_for_each_entry(ck, &clocks, node) {
		if (ck->parent == clk)
			dump_clock(s, ck, deep + 1);
	}
}

static int proc_clk_show(struct seq_file *s, void *v)
{
	struct clk* clk;

	mutex_lock(&clocks_mutex);
	list_for_each_entry(clk, &clocks, node) {
		if (!clk->parent)
			dump_clock(s, clk, 0);
	}
	mutex_unlock(&clocks_mutex);

	seq_printf(s, "\nRegisters:\n");
	seq_printf(s, "SCU_APLL_CON     : 0x%08x\n", scu_readl(SCU_APLL_CON));
	seq_printf(s, "SCU_DPLL_CON     : 0x%08x\n", scu_readl(SCU_DPLL_CON));
	seq_printf(s, "SCU_CPLL_CON     : 0x%08x\n", scu_readl(SCU_CPLL_CON));
	seq_printf(s, "SCU_MODE_CON     : 0x%08x\n", scu_readl(SCU_MODE_CON));
	seq_printf(s, "SCU_PMU_CON      : 0x%08x\n", scu_readl(SCU_PMU_CON));
	seq_printf(s, "SCU_CLKSEL0_CON  : 0x%08x\n", scu_readl(SCU_CLKSEL0_CON));
	seq_printf(s, "SCU_CLKSEL1_CON  : 0x%08x\n", scu_readl(SCU_CLKSEL1_CON));
	seq_printf(s, "SCU_CLKGATE0_CON : 0x%08x\n", scu_readl(SCU_CLKGATE0_CON));
	seq_printf(s, "SCU_CLKGATE1_CON : 0x%08x\n", scu_readl(SCU_CLKGATE1_CON));
	seq_printf(s, "SCU_CLKGATE2_CON : 0x%08x\n", scu_readl(SCU_CLKGATE2_CON));
	seq_printf(s, "SCU_CLKSEL2_CON  : 0x%08x\n", scu_readl(SCU_CLKSEL2_CON));

	return 0;
}

static int proc_clk_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_clk_show, NULL);
}

static const struct file_operations proc_clk_fops = {
	.open		= proc_clk_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init clk_proc_init(void)
{
	proc_create("clocks", 0, NULL, &proc_clk_fops);
	return 0;

}
late_initcall(clk_proc_init);
#endif /* CONFIG_PROC_FS */

