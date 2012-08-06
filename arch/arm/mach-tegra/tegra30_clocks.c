/*
 * arch/arm/mach-tegra/tegra30_clocks.c
 *
 * Copyright (c) 2010-2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/syscore_ops.h>

#include <asm/clkdev.h>

#include <mach/iomap.h>

#include "clock.h"
#include "fuse.h"

#define USE_PLL_LOCK_BITS 0

#define RST_DEVICES_L			0x004
#define RST_DEVICES_H			0x008
#define RST_DEVICES_U			0x00C
#define RST_DEVICES_V			0x358
#define RST_DEVICES_W			0x35C
#define RST_DEVICES_SET_L		0x300
#define RST_DEVICES_CLR_L		0x304
#define RST_DEVICES_SET_V		0x430
#define RST_DEVICES_CLR_V		0x434
#define RST_DEVICES_NUM			5

#define CLK_OUT_ENB_L			0x010
#define CLK_OUT_ENB_H			0x014
#define CLK_OUT_ENB_U			0x018
#define CLK_OUT_ENB_V			0x360
#define CLK_OUT_ENB_W			0x364
#define CLK_OUT_ENB_SET_L		0x320
#define CLK_OUT_ENB_CLR_L		0x324
#define CLK_OUT_ENB_SET_V		0x440
#define CLK_OUT_ENB_CLR_V		0x444
#define CLK_OUT_ENB_NUM			5

#define RST_DEVICES_V_SWR_CPULP_RST_DIS	(0x1 << 1)
#define CLK_OUT_ENB_V_CLK_ENB_CPULP_EN	(0x1 << 1)

#define PERIPH_CLK_TO_BIT(c)		(1 << (c->u.periph.clk_num % 32))
#define PERIPH_CLK_TO_RST_REG(c)	\
	periph_clk_to_reg((c), RST_DEVICES_L, RST_DEVICES_V, 4)
#define PERIPH_CLK_TO_RST_SET_REG(c)	\
	periph_clk_to_reg((c), RST_DEVICES_SET_L, RST_DEVICES_SET_V, 8)
#define PERIPH_CLK_TO_RST_CLR_REG(c)	\
	periph_clk_to_reg((c), RST_DEVICES_CLR_L, RST_DEVICES_CLR_V, 8)

#define PERIPH_CLK_TO_ENB_REG(c)	\
	periph_clk_to_reg((c), CLK_OUT_ENB_L, CLK_OUT_ENB_V, 4)
#define PERIPH_CLK_TO_ENB_SET_REG(c)	\
	periph_clk_to_reg((c), CLK_OUT_ENB_SET_L, CLK_OUT_ENB_SET_V, 8)
#define PERIPH_CLK_TO_ENB_CLR_REG(c)	\
	periph_clk_to_reg((c), CLK_OUT_ENB_CLR_L, CLK_OUT_ENB_CLR_V, 8)

#define CLK_MASK_ARM			0x44
#define MISC_CLK_ENB			0x48

#define OSC_CTRL			0x50
#define OSC_CTRL_OSC_FREQ_MASK		(0xF<<28)
#define OSC_CTRL_OSC_FREQ_13MHZ		(0x0<<28)
#define OSC_CTRL_OSC_FREQ_19_2MHZ	(0x4<<28)
#define OSC_CTRL_OSC_FREQ_12MHZ		(0x8<<28)
#define OSC_CTRL_OSC_FREQ_26MHZ		(0xC<<28)
#define OSC_CTRL_OSC_FREQ_16_8MHZ	(0x1<<28)
#define OSC_CTRL_OSC_FREQ_38_4MHZ	(0x5<<28)
#define OSC_CTRL_OSC_FREQ_48MHZ		(0x9<<28)
#define OSC_CTRL_MASK			(0x3f2 | OSC_CTRL_OSC_FREQ_MASK)

#define OSC_CTRL_PLL_REF_DIV_MASK	(3<<26)
#define OSC_CTRL_PLL_REF_DIV_1		(0<<26)
#define OSC_CTRL_PLL_REF_DIV_2		(1<<26)
#define OSC_CTRL_PLL_REF_DIV_4		(2<<26)

#define OSC_FREQ_DET			0x58
#define OSC_FREQ_DET_TRIG		(1<<31)

#define OSC_FREQ_DET_STATUS		0x5C
#define OSC_FREQ_DET_BUSY		(1<<31)
#define OSC_FREQ_DET_CNT_MASK		0xFFFF

#define PERIPH_CLK_SOURCE_I2S1		0x100
#define PERIPH_CLK_SOURCE_EMC		0x19c
#define PERIPH_CLK_SOURCE_OSC		0x1fc
#define PERIPH_CLK_SOURCE_NUM1 \
	((PERIPH_CLK_SOURCE_OSC - PERIPH_CLK_SOURCE_I2S1) / 4)

#define PERIPH_CLK_SOURCE_G3D2		0x3b0
#define PERIPH_CLK_SOURCE_SE		0x42c
#define PERIPH_CLK_SOURCE_NUM2 \
	((PERIPH_CLK_SOURCE_SE - PERIPH_CLK_SOURCE_G3D2) / 4 + 1)

#define AUDIO_DLY_CLK			0x49c
#define AUDIO_SYNC_CLK_SPDIF		0x4b4
#define PERIPH_CLK_SOURCE_NUM3 \
	((AUDIO_SYNC_CLK_SPDIF - AUDIO_DLY_CLK) / 4 + 1)

#define PERIPH_CLK_SOURCE_NUM		(PERIPH_CLK_SOURCE_NUM1 + \
					 PERIPH_CLK_SOURCE_NUM2 + \
					 PERIPH_CLK_SOURCE_NUM3)

#define CPU_SOFTRST_CTRL		0x380

#define PERIPH_CLK_SOURCE_DIVU71_MASK	0xFF
#define PERIPH_CLK_SOURCE_DIVU16_MASK	0xFFFF
#define PERIPH_CLK_SOURCE_DIV_SHIFT	0
#define PERIPH_CLK_SOURCE_DIVIDLE_SHIFT	8
#define PERIPH_CLK_SOURCE_DIVIDLE_VAL	50
#define PERIPH_CLK_UART_DIV_ENB		(1<<24)
#define PERIPH_CLK_VI_SEL_EX_SHIFT	24
#define PERIPH_CLK_VI_SEL_EX_MASK	(0x3<<PERIPH_CLK_VI_SEL_EX_SHIFT)
#define PERIPH_CLK_NAND_DIV_EX_ENB	(1<<8)
#define PERIPH_CLK_DTV_POLARITY_INV	(1<<25)

#define AUDIO_SYNC_SOURCE_MASK		0x0F
#define AUDIO_SYNC_DISABLE_BIT		0x10
#define AUDIO_SYNC_TAP_NIBBLE_SHIFT(c)	((c->reg_shift - 24) * 4)

#define PLL_BASE			0x0
#define PLL_BASE_BYPASS			(1<<31)
#define PLL_BASE_ENABLE			(1<<30)
#define PLL_BASE_REF_ENABLE		(1<<29)
#define PLL_BASE_OVERRIDE		(1<<28)
#define PLL_BASE_LOCK			(1<<27)
#define PLL_BASE_DIVP_MASK		(0x7<<20)
#define PLL_BASE_DIVP_SHIFT		20
#define PLL_BASE_DIVN_MASK		(0x3FF<<8)
#define PLL_BASE_DIVN_SHIFT		8
#define PLL_BASE_DIVM_MASK		(0x1F)
#define PLL_BASE_DIVM_SHIFT		0

#define PLL_OUT_RATIO_MASK		(0xFF<<8)
#define PLL_OUT_RATIO_SHIFT		8
#define PLL_OUT_OVERRIDE		(1<<2)
#define PLL_OUT_CLKEN			(1<<1)
#define PLL_OUT_RESET_DISABLE		(1<<0)

#define PLL_MISC(c)			\
	(((c)->flags & PLL_ALT_MISC_REG) ? 0x4 : 0xc)
#define PLL_MISC_LOCK_ENABLE(c)	\
	(((c)->flags & (PLLU | PLLD)) ? (1<<22) : (1<<18))

#define PLL_MISC_DCCON_SHIFT		20
#define PLL_MISC_CPCON_SHIFT		8
#define PLL_MISC_CPCON_MASK		(0xF<<PLL_MISC_CPCON_SHIFT)
#define PLL_MISC_LFCON_SHIFT		4
#define PLL_MISC_LFCON_MASK		(0xF<<PLL_MISC_LFCON_SHIFT)
#define PLL_MISC_VCOCON_SHIFT		0
#define PLL_MISC_VCOCON_MASK		(0xF<<PLL_MISC_VCOCON_SHIFT)
#define PLLD_MISC_CLKENABLE		(1<<30)

#define PLLU_BASE_POST_DIV		(1<<20)

#define PLLD_BASE_DSIB_MUX_SHIFT	25
#define PLLD_BASE_DSIB_MUX_MASK		(1<<PLLD_BASE_DSIB_MUX_SHIFT)
#define PLLD_BASE_CSI_CLKENABLE		(1<<26)
#define PLLD_MISC_DSI_CLKENABLE		(1<<30)
#define PLLD_MISC_DIV_RST		(1<<23)
#define PLLD_MISC_DCCON_SHIFT		12

#define PLLDU_LFCON_SET_DIVN		600

/* FIXME: OUT_OF_TABLE_CPCON per pll */
#define OUT_OF_TABLE_CPCON		0x8

#define SUPER_CLK_MUX			0x00
#define SUPER_STATE_SHIFT		28
#define SUPER_STATE_MASK		(0xF << SUPER_STATE_SHIFT)
#define SUPER_STATE_STANDBY		(0x0 << SUPER_STATE_SHIFT)
#define SUPER_STATE_IDLE		(0x1 << SUPER_STATE_SHIFT)
#define SUPER_STATE_RUN			(0x2 << SUPER_STATE_SHIFT)
#define SUPER_STATE_IRQ			(0x3 << SUPER_STATE_SHIFT)
#define SUPER_STATE_FIQ			(0x4 << SUPER_STATE_SHIFT)
#define SUPER_LP_DIV2_BYPASS		(0x1 << 16)
#define SUPER_SOURCE_MASK		0xF
#define	SUPER_FIQ_SOURCE_SHIFT		12
#define	SUPER_IRQ_SOURCE_SHIFT		8
#define	SUPER_RUN_SOURCE_SHIFT		4
#define	SUPER_IDLE_SOURCE_SHIFT		0

#define SUPER_CLK_DIVIDER		0x04
#define SUPER_CLOCK_DIV_U71_SHIFT	16
#define SUPER_CLOCK_DIV_U71_MASK	(0xff << SUPER_CLOCK_DIV_U71_SHIFT)
/* guarantees safe cpu backup */
#define SUPER_CLOCK_DIV_U71_MIN		0x2

#define BUS_CLK_DISABLE			(1<<3)
#define BUS_CLK_DIV_MASK		0x3

#define PMC_CTRL			0x0
 #define PMC_CTRL_BLINK_ENB		(1 << 7)

#define PMC_DPD_PADS_ORIDE		0x1c
 #define PMC_DPD_PADS_ORIDE_BLINK_ENB	(1 << 20)

#define PMC_BLINK_TIMER_DATA_ON_SHIFT	0
#define PMC_BLINK_TIMER_DATA_ON_MASK	0x7fff
#define PMC_BLINK_TIMER_ENB		(1 << 15)
#define PMC_BLINK_TIMER_DATA_OFF_SHIFT	16
#define PMC_BLINK_TIMER_DATA_OFF_MASK	0xffff

#define PMC_PLLP_WB0_OVERRIDE				0xf8
#define PMC_PLLP_WB0_OVERRIDE_PLLM_ENABLE		(1 << 12)

#define UTMIP_PLL_CFG2					0x488
#define UTMIP_PLL_CFG2_STABLE_COUNT(x)			(((x) & 0xfff) << 6)
#define UTMIP_PLL_CFG2_ACTIVE_DLY_COUNT(x)		(((x) & 0x3f) << 18)
#define UTMIP_PLL_CFG2_FORCE_PD_SAMP_A_POWERDOWN	(1 << 0)
#define UTMIP_PLL_CFG2_FORCE_PD_SAMP_B_POWERDOWN	(1 << 2)
#define UTMIP_PLL_CFG2_FORCE_PD_SAMP_C_POWERDOWN	(1 << 4)

#define UTMIP_PLL_CFG1					0x484
#define UTMIP_PLL_CFG1_ENABLE_DLY_COUNT(x)		(((x) & 0x1f) << 27)
#define UTMIP_PLL_CFG1_XTAL_FREQ_COUNT(x)		(((x) & 0xfff) << 0)
#define UTMIP_PLL_CFG1_FORCE_PLL_ENABLE_POWERDOWN	(1 << 14)
#define UTMIP_PLL_CFG1_FORCE_PLL_ACTIVE_POWERDOWN	(1 << 12)
#define UTMIP_PLL_CFG1_FORCE_PLLU_POWERDOWN		(1 << 16)

#define PLLE_BASE_CML_ENABLE		(1<<31)
#define PLLE_BASE_ENABLE		(1<<30)
#define PLLE_BASE_DIVCML_SHIFT		24
#define PLLE_BASE_DIVCML_MASK		(0xf<<PLLE_BASE_DIVCML_SHIFT)
#define PLLE_BASE_DIVP_SHIFT		16
#define PLLE_BASE_DIVP_MASK		(0x3f<<PLLE_BASE_DIVP_SHIFT)
#define PLLE_BASE_DIVN_SHIFT		8
#define PLLE_BASE_DIVN_MASK		(0xFF<<PLLE_BASE_DIVN_SHIFT)
#define PLLE_BASE_DIVM_SHIFT		0
#define PLLE_BASE_DIVM_MASK		(0xFF<<PLLE_BASE_DIVM_SHIFT)
#define PLLE_BASE_DIV_MASK		\
	(PLLE_BASE_DIVCML_MASK | PLLE_BASE_DIVP_MASK | \
	 PLLE_BASE_DIVN_MASK | PLLE_BASE_DIVM_MASK)
#define PLLE_BASE_DIV(m, n, p, cml)		\
	 (((cml)<<PLLE_BASE_DIVCML_SHIFT) | ((p)<<PLLE_BASE_DIVP_SHIFT) | \
	  ((n)<<PLLE_BASE_DIVN_SHIFT) | ((m)<<PLLE_BASE_DIVM_SHIFT))

#define PLLE_MISC_SETUP_BASE_SHIFT	16
#define PLLE_MISC_SETUP_BASE_MASK	(0xFFFF<<PLLE_MISC_SETUP_BASE_SHIFT)
#define PLLE_MISC_READY			(1<<15)
#define PLLE_MISC_LOCK			(1<<11)
#define PLLE_MISC_LOCK_ENABLE		(1<<9)
#define PLLE_MISC_SETUP_EX_SHIFT	2
#define PLLE_MISC_SETUP_EX_MASK		(0x3<<PLLE_MISC_SETUP_EX_SHIFT)
#define PLLE_MISC_SETUP_MASK		\
	  (PLLE_MISC_SETUP_BASE_MASK | PLLE_MISC_SETUP_EX_MASK)
#define PLLE_MISC_SETUP_VALUE		\
	  ((0x7<<PLLE_MISC_SETUP_BASE_SHIFT) | (0x0<<PLLE_MISC_SETUP_EX_SHIFT))

#define PLLE_SS_CTRL			0x68
#define	PLLE_SS_INCINTRV_SHIFT		24
#define	PLLE_SS_INCINTRV_MASK		(0x3f<<PLLE_SS_INCINTRV_SHIFT)
#define	PLLE_SS_INC_SHIFT		16
#define	PLLE_SS_INC_MASK		(0xff<<PLLE_SS_INC_SHIFT)
#define	PLLE_SS_MAX_SHIFT		0
#define	PLLE_SS_MAX_MASK		(0x1ff<<PLLE_SS_MAX_SHIFT)
#define PLLE_SS_COEFFICIENTS_MASK	\
	(PLLE_SS_INCINTRV_MASK | PLLE_SS_INC_MASK | PLLE_SS_MAX_MASK)
#define PLLE_SS_COEFFICIENTS_12MHZ	\
	((0x18<<PLLE_SS_INCINTRV_SHIFT) | (0x1<<PLLE_SS_INC_SHIFT) | \
	 (0x24<<PLLE_SS_MAX_SHIFT))
#define PLLE_SS_DISABLE			((1<<12) | (1<<11) | (1<<10))

#define PLLE_AUX			0x48c
#define PLLE_AUX_PLLP_SEL		(1<<2)
#define PLLE_AUX_CML_SATA_ENABLE	(1<<1)
#define PLLE_AUX_CML_PCIE_ENABLE	(1<<0)

#define	PMC_SATA_PWRGT			0x1ac
#define PMC_SATA_PWRGT_PLLE_IDDQ_VALUE	(1<<5)
#define PMC_SATA_PWRGT_PLLE_IDDQ_SWCTL	(1<<4)

#define ROUND_DIVIDER_UP	0
#define ROUND_DIVIDER_DOWN	1

/* FIXME: recommended safety delay after lock is detected */
#define PLL_POST_LOCK_DELAY		100

/**
* Structure defining the fields for USB UTMI clocks Parameters.
*/
struct utmi_clk_param {
	/* Oscillator Frequency in KHz */
	u32 osc_frequency;
	/* UTMIP PLL Enable Delay Count  */
	u8 enable_delay_count;
	/* UTMIP PLL Stable count */
	u8 stable_count;
	/*  UTMIP PLL Active delay count */
	u8 active_delay_count;
	/* UTMIP PLL Xtal frequency count */
	u8 xtal_freq_count;
};

static const struct utmi_clk_param utmi_parameters[] = {
	{
		.osc_frequency = 13000000,
		.enable_delay_count = 0x02,
		.stable_count = 0x33,
		.active_delay_count = 0x05,
		.xtal_freq_count = 0x7F
	},
	{
		.osc_frequency = 19200000,
		.enable_delay_count = 0x03,
		.stable_count = 0x4B,
		.active_delay_count = 0x06,
		.xtal_freq_count = 0xBB},
	{
		.osc_frequency = 12000000,
		.enable_delay_count = 0x02,
		.stable_count = 0x2F,
		.active_delay_count = 0x04,
		.xtal_freq_count = 0x76
	},
	{
		.osc_frequency = 26000000,
		.enable_delay_count = 0x04,
		.stable_count = 0x66,
		.active_delay_count = 0x09,
		.xtal_freq_count = 0xFE
	},
	{
		.osc_frequency = 16800000,
		.enable_delay_count = 0x03,
		.stable_count = 0x41,
		.active_delay_count = 0x0A,
		.xtal_freq_count = 0xA4
	},
};

static void __iomem *reg_clk_base = IO_ADDRESS(TEGRA_CLK_RESET_BASE);
static void __iomem *reg_pmc_base = IO_ADDRESS(TEGRA_PMC_BASE);
static void __iomem *misc_gp_hidrev_base = IO_ADDRESS(TEGRA_APB_MISC_BASE);

#define MISC_GP_HIDREV                  0x804

/*
 * Some peripheral clocks share an enable bit, so refcount the enable bits
 * in registers CLK_ENABLE_L, ... CLK_ENABLE_W
 */
static int tegra_periph_clk_enable_refcount[CLK_OUT_ENB_NUM * 32];

#define clk_writel(value, reg) \
	__raw_writel(value, (u32)reg_clk_base + (reg))
#define clk_readl(reg) \
	__raw_readl((u32)reg_clk_base + (reg))
#define pmc_writel(value, reg) \
	__raw_writel(value, (u32)reg_pmc_base + (reg))
#define pmc_readl(reg) \
	__raw_readl((u32)reg_pmc_base + (reg))
#define chipid_readl() \
	__raw_readl((u32)misc_gp_hidrev_base + MISC_GP_HIDREV)

#define clk_writel_delay(value, reg)					\
	do {								\
		__raw_writel((value), (u32)reg_clk_base + (reg));	\
		udelay(2);						\
	} while (0)


static inline int clk_set_div(struct clk *c, u32 n)
{
	return clk_set_rate(c, (clk_get_rate(c->parent) + n-1) / n);
}

static inline u32 periph_clk_to_reg(
	struct clk *c, u32 reg_L, u32 reg_V, int offs)
{
	u32 reg = c->u.periph.clk_num / 32;
	BUG_ON(reg >= RST_DEVICES_NUM);
	if (reg < 3)
		reg = reg_L + (reg * offs);
	else
		reg = reg_V + ((reg - 3) * offs);
	return reg;
}

static unsigned long clk_measure_input_freq(void)
{
	u32 clock_autodetect;
	clk_writel(OSC_FREQ_DET_TRIG | 1, OSC_FREQ_DET);
	do {} while (clk_readl(OSC_FREQ_DET_STATUS) & OSC_FREQ_DET_BUSY);
	clock_autodetect = clk_readl(OSC_FREQ_DET_STATUS);
	if (clock_autodetect >= 732 - 3 && clock_autodetect <= 732 + 3) {
		return 12000000;
	} else if (clock_autodetect >= 794 - 3 && clock_autodetect <= 794 + 3) {
		return 13000000;
	} else if (clock_autodetect >= 1172 - 3 && clock_autodetect <= 1172 + 3) {
		return 19200000;
	} else if (clock_autodetect >= 1587 - 3 && clock_autodetect <= 1587 + 3) {
		return 26000000;
	} else if (clock_autodetect >= 1025 - 3 && clock_autodetect <= 1025 + 3) {
		return 16800000;
	} else if (clock_autodetect >= 2344 - 3 && clock_autodetect <= 2344 + 3) {
		return 38400000;
	} else if (clock_autodetect >= 2928 - 3 && clock_autodetect <= 2928 + 3) {
		return 48000000;
	} else {
		pr_err("%s: Unexpected clock autodetect value %d", __func__,
			clock_autodetect);
		BUG();
		return 0;
	}
}

static int clk_div71_get_divider(unsigned long parent_rate, unsigned long rate,
				 u32 flags, u32 round_mode)
{
	s64 divider_u71 = parent_rate;
	if (!rate)
		return -EINVAL;

	if (!(flags & DIV_U71_INT))
		divider_u71 *= 2;
	if (round_mode == ROUND_DIVIDER_UP)
		divider_u71 += rate - 1;
	do_div(divider_u71, rate);
	if (flags & DIV_U71_INT)
		divider_u71 *= 2;

	if (divider_u71 - 2 < 0)
		return 0;

	if (divider_u71 - 2 > 255)
		return -EINVAL;

	return divider_u71 - 2;
}

static int clk_div16_get_divider(unsigned long parent_rate, unsigned long rate)
{
	s64 divider_u16;

	divider_u16 = parent_rate;
	if (!rate)
		return -EINVAL;
	divider_u16 += rate - 1;
	do_div(divider_u16, rate);

	if (divider_u16 - 1 < 0)
		return 0;

	if (divider_u16 - 1 > 0xFFFF)
		return -EINVAL;

	return divider_u16 - 1;
}

/* clk_m functions */
static unsigned long tegra30_clk_m_autodetect_rate(struct clk *c)
{
	u32 osc_ctrl = clk_readl(OSC_CTRL);
	u32 auto_clock_control = osc_ctrl & ~OSC_CTRL_OSC_FREQ_MASK;
	u32 pll_ref_div = osc_ctrl & OSC_CTRL_PLL_REF_DIV_MASK;

	c->rate = clk_measure_input_freq();
	switch (c->rate) {
	case 12000000:
		auto_clock_control |= OSC_CTRL_OSC_FREQ_12MHZ;
		BUG_ON(pll_ref_div != OSC_CTRL_PLL_REF_DIV_1);
		break;
	case 13000000:
		auto_clock_control |= OSC_CTRL_OSC_FREQ_13MHZ;
		BUG_ON(pll_ref_div != OSC_CTRL_PLL_REF_DIV_1);
		break;
	case 19200000:
		auto_clock_control |= OSC_CTRL_OSC_FREQ_19_2MHZ;
		BUG_ON(pll_ref_div != OSC_CTRL_PLL_REF_DIV_1);
		break;
	case 26000000:
		auto_clock_control |= OSC_CTRL_OSC_FREQ_26MHZ;
		BUG_ON(pll_ref_div != OSC_CTRL_PLL_REF_DIV_1);
		break;
	case 16800000:
		auto_clock_control |= OSC_CTRL_OSC_FREQ_16_8MHZ;
		BUG_ON(pll_ref_div != OSC_CTRL_PLL_REF_DIV_1);
		break;
	case 38400000:
		auto_clock_control |= OSC_CTRL_OSC_FREQ_38_4MHZ;
		BUG_ON(pll_ref_div != OSC_CTRL_PLL_REF_DIV_2);
		break;
	case 48000000:
		auto_clock_control |= OSC_CTRL_OSC_FREQ_48MHZ;
		BUG_ON(pll_ref_div != OSC_CTRL_PLL_REF_DIV_4);
		break;
	default:
		pr_err("%s: Unexpected clock rate %ld", __func__, c->rate);
		BUG();
	}
	clk_writel(auto_clock_control, OSC_CTRL);
	return c->rate;
}

static void tegra30_clk_m_init(struct clk *c)
{
	pr_debug("%s on clock %s\n", __func__, c->name);
	tegra30_clk_m_autodetect_rate(c);
}

static int tegra30_clk_m_enable(struct clk *c)
{
	pr_debug("%s on clock %s\n", __func__, c->name);
	return 0;
}

static void tegra30_clk_m_disable(struct clk *c)
{
	pr_debug("%s on clock %s\n", __func__, c->name);
	WARN(1, "Attempting to disable main SoC clock\n");
}

struct clk_ops tegra30_clk_m_ops = {
	.init		= tegra30_clk_m_init,
	.enable		= tegra30_clk_m_enable,
	.disable	= tegra30_clk_m_disable,
};

struct clk_ops tegra_clk_m_div_ops = {
	.enable		= tegra30_clk_m_enable,
};

/* PLL reference divider functions */
static void tegra30_pll_ref_init(struct clk *c)
{
	u32 pll_ref_div = clk_readl(OSC_CTRL) & OSC_CTRL_PLL_REF_DIV_MASK;
	pr_debug("%s on clock %s\n", __func__, c->name);

	switch (pll_ref_div) {
	case OSC_CTRL_PLL_REF_DIV_1:
		c->div = 1;
		break;
	case OSC_CTRL_PLL_REF_DIV_2:
		c->div = 2;
		break;
	case OSC_CTRL_PLL_REF_DIV_4:
		c->div = 4;
		break;
	default:
		pr_err("%s: Invalid pll ref divider %d", __func__, pll_ref_div);
		BUG();
	}
	c->mul = 1;
	c->state = ON;
}

struct clk_ops tegra_pll_ref_ops = {
	.init		= tegra30_pll_ref_init,
	.enable		= tegra30_clk_m_enable,
	.disable	= tegra30_clk_m_disable,
};

/* super clock functions */
/* "super clocks" on tegra30 have two-stage muxes, fractional 7.1 divider and
 * clock skipping super divider.  We will ignore the clock skipping divider,
 * since we can't lower the voltage when using the clock skip, but we can if
 * we lower the PLL frequency. We will use 7.1 divider for CPU super-clock
 * only when its parent is a fixed rate PLL, since we can't change PLL rate
 * in this case.
 */
static void tegra30_super_clk_init(struct clk *c)
{
	u32 val;
	int source;
	int shift;
	const struct clk_mux_sel *sel;
	val = clk_readl(c->reg + SUPER_CLK_MUX);
	c->state = ON;
	BUG_ON(((val & SUPER_STATE_MASK) != SUPER_STATE_RUN) &&
		((val & SUPER_STATE_MASK) != SUPER_STATE_IDLE));
	shift = ((val & SUPER_STATE_MASK) == SUPER_STATE_IDLE) ?
		SUPER_IDLE_SOURCE_SHIFT : SUPER_RUN_SOURCE_SHIFT;
	source = (val >> shift) & SUPER_SOURCE_MASK;
	if (c->flags & DIV_2)
		source |= val & SUPER_LP_DIV2_BYPASS;
	for (sel = c->inputs; sel->input != NULL; sel++) {
		if (sel->value == source)
			break;
	}
	BUG_ON(sel->input == NULL);
	c->parent = sel->input;

	if (c->flags & DIV_U71) {
		/* Init safe 7.1 divider value (does not affect PLLX path) */
		clk_writel(SUPER_CLOCK_DIV_U71_MIN << SUPER_CLOCK_DIV_U71_SHIFT,
			   c->reg + SUPER_CLK_DIVIDER);
		c->mul = 2;
		c->div = 2;
		if (!(c->parent->flags & PLLX))
			c->div += SUPER_CLOCK_DIV_U71_MIN;
	} else
		clk_writel(0, c->reg + SUPER_CLK_DIVIDER);
}

static int tegra30_super_clk_enable(struct clk *c)
{
	return 0;
}

static void tegra30_super_clk_disable(struct clk *c)
{
	/* since tegra 3 has 2 CPU super clocks - low power lp-mode clock and
	   geared up g-mode super clock - mode switch may request to disable
	   either of them; accept request with no affect on h/w */
}

static int tegra30_super_clk_set_parent(struct clk *c, struct clk *p)
{
	u32 val;
	const struct clk_mux_sel *sel;
	int shift;

	val = clk_readl(c->reg + SUPER_CLK_MUX);
	BUG_ON(((val & SUPER_STATE_MASK) != SUPER_STATE_RUN) &&
		((val & SUPER_STATE_MASK) != SUPER_STATE_IDLE));
	shift = ((val & SUPER_STATE_MASK) == SUPER_STATE_IDLE) ?
		SUPER_IDLE_SOURCE_SHIFT : SUPER_RUN_SOURCE_SHIFT;
	for (sel = c->inputs; sel->input != NULL; sel++) {
		if (sel->input == p) {
			/* For LP mode super-clock switch between PLLX direct
			   and divided-by-2 outputs is allowed only when other
			   than PLLX clock source is current parent */
			if ((c->flags & DIV_2) && (p->flags & PLLX) &&
			    ((sel->value ^ val) & SUPER_LP_DIV2_BYPASS)) {
				if (c->parent->flags & PLLX)
					return -EINVAL;
				val ^= SUPER_LP_DIV2_BYPASS;
				clk_writel_delay(val, c->reg);
			}
			val &= ~(SUPER_SOURCE_MASK << shift);
			val |= (sel->value & SUPER_SOURCE_MASK) << shift;

			/* 7.1 divider for CPU super-clock does not affect
			   PLLX path */
			if (c->flags & DIV_U71) {
				u32 div = 0;
				if (!(p->flags & PLLX)) {
					div = clk_readl(c->reg +
							SUPER_CLK_DIVIDER);
					div &= SUPER_CLOCK_DIV_U71_MASK;
					div >>= SUPER_CLOCK_DIV_U71_SHIFT;
				}
				c->div = div + 2;
				c->mul = 2;
			}

			if (c->refcnt)
				clk_enable(p);

			clk_writel_delay(val, c->reg);

			if (c->refcnt && c->parent)
				clk_disable(c->parent);

			clk_reparent(c, p);
			return 0;
		}
	}
	return -EINVAL;
}

/*
 * Do not use super clocks "skippers", since dividing using a clock skipper
 * does not allow the voltage to be scaled down. Instead adjust the rate of
 * the parent clock. This requires that the parent of a super clock have no
 * other children, otherwise the rate will change underneath the other
 * children. Special case: if fixed rate PLL is CPU super clock parent the
 * rate of this PLL can't be changed, and it has many other children. In
 * this case use 7.1 fractional divider to adjust the super clock rate.
 */
static int tegra30_super_clk_set_rate(struct clk *c, unsigned long rate)
{
	if ((c->flags & DIV_U71) && (c->parent->flags & PLL_FIXED)) {
		int div = clk_div71_get_divider(c->parent->u.pll.fixed_rate,
					rate, c->flags, ROUND_DIVIDER_DOWN);
		div = max(div, SUPER_CLOCK_DIV_U71_MIN);

		clk_writel(div << SUPER_CLOCK_DIV_U71_SHIFT,
			   c->reg + SUPER_CLK_DIVIDER);
		c->div = div + 2;
		c->mul = 2;
		return 0;
	}
	return clk_set_rate(c->parent, rate);
}

struct clk_ops tegra30_super_ops = {
	.init			= tegra30_super_clk_init,
	.enable			= tegra30_super_clk_enable,
	.disable		= tegra30_super_clk_disable,
	.set_parent		= tegra30_super_clk_set_parent,
	.set_rate		= tegra30_super_clk_set_rate,
};

static int tegra30_twd_clk_set_rate(struct clk *c, unsigned long rate)
{
	/* The input value 'rate' is the clock rate of the CPU complex. */
	c->rate = (rate * c->mul) / c->div;
	return 0;
}

struct clk_ops tegra30_twd_ops = {
	.set_rate	= tegra30_twd_clk_set_rate,
};

/* Blink output functions */

static void tegra30_blink_clk_init(struct clk *c)
{
	u32 val;

	val = pmc_readl(PMC_CTRL);
	c->state = (val & PMC_CTRL_BLINK_ENB) ? ON : OFF;
	c->mul = 1;
	val = pmc_readl(c->reg);

	if (val & PMC_BLINK_TIMER_ENB) {
		unsigned int on_off;

		on_off = (val >> PMC_BLINK_TIMER_DATA_ON_SHIFT) &
			PMC_BLINK_TIMER_DATA_ON_MASK;
		val >>= PMC_BLINK_TIMER_DATA_OFF_SHIFT;
		val &= PMC_BLINK_TIMER_DATA_OFF_MASK;
		on_off += val;
		/* each tick in the blink timer is 4 32KHz clocks */
		c->div = on_off * 4;
	} else {
		c->div = 1;
	}
}

static int tegra30_blink_clk_enable(struct clk *c)
{
	u32 val;

	val = pmc_readl(PMC_DPD_PADS_ORIDE);
	pmc_writel(val | PMC_DPD_PADS_ORIDE_BLINK_ENB, PMC_DPD_PADS_ORIDE);

	val = pmc_readl(PMC_CTRL);
	pmc_writel(val | PMC_CTRL_BLINK_ENB, PMC_CTRL);

	return 0;
}

static void tegra30_blink_clk_disable(struct clk *c)
{
	u32 val;

	val = pmc_readl(PMC_CTRL);
	pmc_writel(val & ~PMC_CTRL_BLINK_ENB, PMC_CTRL);

	val = pmc_readl(PMC_DPD_PADS_ORIDE);
	pmc_writel(val & ~PMC_DPD_PADS_ORIDE_BLINK_ENB, PMC_DPD_PADS_ORIDE);
}

static int tegra30_blink_clk_set_rate(struct clk *c, unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(c->parent);
	if (rate >= parent_rate) {
		c->div = 1;
		pmc_writel(0, c->reg);
	} else {
		unsigned int on_off;
		u32 val;

		on_off = DIV_ROUND_UP(parent_rate / 8, rate);
		c->div = on_off * 8;

		val = (on_off & PMC_BLINK_TIMER_DATA_ON_MASK) <<
			PMC_BLINK_TIMER_DATA_ON_SHIFT;
		on_off &= PMC_BLINK_TIMER_DATA_OFF_MASK;
		on_off <<= PMC_BLINK_TIMER_DATA_OFF_SHIFT;
		val |= on_off;
		val |= PMC_BLINK_TIMER_ENB;
		pmc_writel(val, c->reg);
	}

	return 0;
}

struct clk_ops tegra30_blink_clk_ops = {
	.init			= &tegra30_blink_clk_init,
	.enable			= &tegra30_blink_clk_enable,
	.disable		= &tegra30_blink_clk_disable,
	.set_rate		= &tegra30_blink_clk_set_rate,
};

/* PLL Functions */
static int tegra30_pll_clk_wait_for_lock(struct clk *c, u32 lock_reg,
					 u32 lock_bit)
{
#if USE_PLL_LOCK_BITS
	int i;
	for (i = 0; i < c->u.pll.lock_delay; i++) {
		if (clk_readl(lock_reg) & lock_bit) {
			udelay(PLL_POST_LOCK_DELAY);
			return 0;
		}
		udelay(2);		/* timeout = 2 * lock time */
	}
	pr_err("Timed out waiting for lock bit on pll %s", c->name);
	return -1;
#endif
	udelay(c->u.pll.lock_delay);

	return 0;
}

static void tegra30_utmi_param_configure(struct clk *c)
{
	u32 reg;
	int i;
	unsigned long main_rate =
		clk_get_rate(c->parent->parent);

	for (i = 0; i < ARRAY_SIZE(utmi_parameters); i++) {
		if (main_rate == utmi_parameters[i].osc_frequency)
			break;
	}

	if (i >= ARRAY_SIZE(utmi_parameters)) {
		pr_err("%s: Unexpected main rate %lu\n", __func__, main_rate);
		return;
	}

	reg = clk_readl(UTMIP_PLL_CFG2);

	/* Program UTMIP PLL stable and active counts */
	/* [FIXME] arclk_rst.h says WRONG! This should be 1ms -> 0x50 Check! */
	reg &= ~UTMIP_PLL_CFG2_STABLE_COUNT(~0);
	reg |= UTMIP_PLL_CFG2_STABLE_COUNT(
			utmi_parameters[i].stable_count);

	reg &= ~UTMIP_PLL_CFG2_ACTIVE_DLY_COUNT(~0);

	reg |= UTMIP_PLL_CFG2_ACTIVE_DLY_COUNT(
			utmi_parameters[i].active_delay_count);

	/* Remove power downs from UTMIP PLL control bits */
	reg &= ~UTMIP_PLL_CFG2_FORCE_PD_SAMP_A_POWERDOWN;
	reg &= ~UTMIP_PLL_CFG2_FORCE_PD_SAMP_B_POWERDOWN;
	reg &= ~UTMIP_PLL_CFG2_FORCE_PD_SAMP_C_POWERDOWN;

	clk_writel(reg, UTMIP_PLL_CFG2);

	/* Program UTMIP PLL delay and oscillator frequency counts */
	reg = clk_readl(UTMIP_PLL_CFG1);
	reg &= ~UTMIP_PLL_CFG1_ENABLE_DLY_COUNT(~0);

	reg |= UTMIP_PLL_CFG1_ENABLE_DLY_COUNT(
		utmi_parameters[i].enable_delay_count);

	reg &= ~UTMIP_PLL_CFG1_XTAL_FREQ_COUNT(~0);
	reg |= UTMIP_PLL_CFG1_XTAL_FREQ_COUNT(
		utmi_parameters[i].xtal_freq_count);

	/* Remove power downs from UTMIP PLL control bits */
	reg &= ~UTMIP_PLL_CFG1_FORCE_PLL_ENABLE_POWERDOWN;
	reg &= ~UTMIP_PLL_CFG1_FORCE_PLL_ACTIVE_POWERDOWN;
	reg &= ~UTMIP_PLL_CFG1_FORCE_PLLU_POWERDOWN;

	clk_writel(reg, UTMIP_PLL_CFG1);
}

static void tegra30_pll_clk_init(struct clk *c)
{
	u32 val = clk_readl(c->reg + PLL_BASE);

	c->state = (val & PLL_BASE_ENABLE) ? ON : OFF;

	if (c->flags & PLL_FIXED && !(val & PLL_BASE_OVERRIDE)) {
		const struct clk_pll_freq_table *sel;
		unsigned long input_rate = clk_get_rate(c->parent);
		for (sel = c->u.pll.freq_table; sel->input_rate != 0; sel++) {
			if (sel->input_rate == input_rate &&
				sel->output_rate == c->u.pll.fixed_rate) {
				c->mul = sel->n;
				c->div = sel->m * sel->p;
				return;
			}
		}
		pr_err("Clock %s has unknown fixed frequency\n", c->name);
		BUG();
	} else if (val & PLL_BASE_BYPASS) {
		c->mul = 1;
		c->div = 1;
	} else {
		c->mul = (val & PLL_BASE_DIVN_MASK) >> PLL_BASE_DIVN_SHIFT;
		c->div = (val & PLL_BASE_DIVM_MASK) >> PLL_BASE_DIVM_SHIFT;
		if (c->flags & PLLU)
			c->div *= (val & PLLU_BASE_POST_DIV) ? 1 : 2;
		else
			c->div *= (0x1 << ((val & PLL_BASE_DIVP_MASK) >>
					PLL_BASE_DIVP_SHIFT));
		if (c->flags & PLL_FIXED) {
			unsigned long rate = clk_get_rate_locked(c);
			BUG_ON(rate != c->u.pll.fixed_rate);
		}
	}

	if (c->flags & PLLU)
		tegra30_utmi_param_configure(c);
}

static int tegra30_pll_clk_enable(struct clk *c)
{
	u32 val;
	pr_debug("%s on clock %s\n", __func__, c->name);

#if USE_PLL_LOCK_BITS
	val = clk_readl(c->reg + PLL_MISC(c));
	val |= PLL_MISC_LOCK_ENABLE(c);
	clk_writel(val, c->reg + PLL_MISC(c));
#endif
	val = clk_readl(c->reg + PLL_BASE);
	val &= ~PLL_BASE_BYPASS;
	val |= PLL_BASE_ENABLE;
	clk_writel(val, c->reg + PLL_BASE);

	if (c->flags & PLLM) {
		val = pmc_readl(PMC_PLLP_WB0_OVERRIDE);
		val |= PMC_PLLP_WB0_OVERRIDE_PLLM_ENABLE;
		pmc_writel(val, PMC_PLLP_WB0_OVERRIDE);
	}

	tegra30_pll_clk_wait_for_lock(c, c->reg + PLL_BASE, PLL_BASE_LOCK);

	return 0;
}

static void tegra30_pll_clk_disable(struct clk *c)
{
	u32 val;
	pr_debug("%s on clock %s\n", __func__, c->name);

	val = clk_readl(c->reg);
	val &= ~(PLL_BASE_BYPASS | PLL_BASE_ENABLE);
	clk_writel(val, c->reg);

	if (c->flags & PLLM) {
		val = pmc_readl(PMC_PLLP_WB0_OVERRIDE);
		val &= ~PMC_PLLP_WB0_OVERRIDE_PLLM_ENABLE;
		pmc_writel(val, PMC_PLLP_WB0_OVERRIDE);
	}
}

static int tegra30_pll_clk_set_rate(struct clk *c, unsigned long rate)
{
	u32 val, p_div, old_base;
	unsigned long input_rate;
	const struct clk_pll_freq_table *sel;
	struct clk_pll_freq_table cfg;

	pr_debug("%s: %s %lu\n", __func__, c->name, rate);

	if (c->flags & PLL_FIXED) {
		int ret = 0;
		if (rate != c->u.pll.fixed_rate) {
			pr_err("%s: Can not change %s fixed rate %lu to %lu\n",
			       __func__, c->name, c->u.pll.fixed_rate, rate);
			ret = -EINVAL;
		}
		return ret;
	}

	if (c->flags & PLLM) {
		if (rate != clk_get_rate_locked(c)) {
			pr_err("%s: Can not change memory %s rate in flight\n",
			       __func__, c->name);
			return -EINVAL;
		}
		return 0;
	}

	p_div = 0;
	input_rate = clk_get_rate(c->parent);

	/* Check if the target rate is tabulated */
	for (sel = c->u.pll.freq_table; sel->input_rate != 0; sel++) {
		if (sel->input_rate == input_rate && sel->output_rate == rate) {
			if (c->flags & PLLU) {
				BUG_ON(sel->p < 1 || sel->p > 2);
				if (sel->p == 1)
					p_div = PLLU_BASE_POST_DIV;
			} else {
				BUG_ON(sel->p < 1);
				for (val = sel->p; val > 1; val >>= 1)
					p_div++;
				p_div <<= PLL_BASE_DIVP_SHIFT;
			}
			break;
		}
	}

	/* Configure out-of-table rate */
	if (sel->input_rate == 0) {
		unsigned long cfreq;
		BUG_ON(c->flags & PLLU);
		sel = &cfg;

		switch (input_rate) {
		case 12000000:
		case 26000000:
			cfreq = (rate <= 1000000 * 1000) ? 1000000 : 2000000;
			break;
		case 13000000:
			cfreq = (rate <= 1000000 * 1000) ? 1000000 : 2600000;
			break;
		case 16800000:
		case 19200000:
			cfreq = (rate <= 1200000 * 1000) ? 1200000 : 2400000;
			break;
		default:
			pr_err("%s: Unexpected reference rate %lu\n",
			       __func__, input_rate);
			BUG();
		}

		/* Raise VCO to guarantee 0.5% accuracy */
		for (cfg.output_rate = rate; cfg.output_rate < 200 * cfreq;
		      cfg.output_rate <<= 1)
			p_div++;

		cfg.p = 0x1 << p_div;
		cfg.m = input_rate / cfreq;
		cfg.n = cfg.output_rate / cfreq;
		cfg.cpcon = OUT_OF_TABLE_CPCON;

		if ((cfg.m > (PLL_BASE_DIVM_MASK >> PLL_BASE_DIVM_SHIFT)) ||
		    (cfg.n > (PLL_BASE_DIVN_MASK >> PLL_BASE_DIVN_SHIFT)) ||
		    (p_div > (PLL_BASE_DIVP_MASK >> PLL_BASE_DIVP_SHIFT)) ||
		    (cfg.output_rate > c->u.pll.vco_max)) {
			pr_err("%s: Failed to set %s out-of-table rate %lu\n",
			       __func__, c->name, rate);
			return -EINVAL;
		}
		p_div <<= PLL_BASE_DIVP_SHIFT;
	}

	c->mul = sel->n;
	c->div = sel->m * sel->p;

	old_base = val = clk_readl(c->reg + PLL_BASE);
	val &= ~(PLL_BASE_DIVM_MASK | PLL_BASE_DIVN_MASK |
		 ((c->flags & PLLU) ? PLLU_BASE_POST_DIV : PLL_BASE_DIVP_MASK));
	val |= (sel->m << PLL_BASE_DIVM_SHIFT) |
		(sel->n << PLL_BASE_DIVN_SHIFT) | p_div;
	if (val == old_base)
		return 0;

	if (c->state == ON) {
		tegra30_pll_clk_disable(c);
		val &= ~(PLL_BASE_BYPASS | PLL_BASE_ENABLE);
	}
	clk_writel(val, c->reg + PLL_BASE);

	if (c->flags & PLL_HAS_CPCON) {
		val = clk_readl(c->reg + PLL_MISC(c));
		val &= ~PLL_MISC_CPCON_MASK;
		val |= sel->cpcon << PLL_MISC_CPCON_SHIFT;
		if (c->flags & (PLLU | PLLD)) {
			val &= ~PLL_MISC_LFCON_MASK;
			if (sel->n >= PLLDU_LFCON_SET_DIVN)
				val |= 0x1 << PLL_MISC_LFCON_SHIFT;
		} else if (c->flags & (PLLX | PLLM)) {
			val &= ~(0x1 << PLL_MISC_DCCON_SHIFT);
			if (rate >= (c->u.pll.vco_max >> 1))
				val |= 0x1 << PLL_MISC_DCCON_SHIFT;
		}
		clk_writel(val, c->reg + PLL_MISC(c));
	}

	if (c->state == ON)
		tegra30_pll_clk_enable(c);

	return 0;
}

struct clk_ops tegra30_pll_ops = {
	.init			= tegra30_pll_clk_init,
	.enable			= tegra30_pll_clk_enable,
	.disable		= tegra30_pll_clk_disable,
	.set_rate		= tegra30_pll_clk_set_rate,
};

static int
tegra30_plld_clk_cfg_ex(struct clk *c, enum tegra_clk_ex_param p, u32 setting)
{
	u32 val, mask, reg;

	switch (p) {
	case TEGRA_CLK_PLLD_CSI_OUT_ENB:
		mask = PLLD_BASE_CSI_CLKENABLE;
		reg = c->reg + PLL_BASE;
		break;
	case TEGRA_CLK_PLLD_DSI_OUT_ENB:
		mask = PLLD_MISC_DSI_CLKENABLE;
		reg = c->reg + PLL_MISC(c);
		break;
	case TEGRA_CLK_PLLD_MIPI_MUX_SEL:
		if (!(c->flags & PLL_ALT_MISC_REG)) {
			mask = PLLD_BASE_DSIB_MUX_MASK;
			reg = c->reg + PLL_BASE;
			break;
		}
	/* fall through - error since PLLD2 does not have MUX_SEL control */
	default:
		return -EINVAL;
	}

	val = clk_readl(reg);
	if (setting)
		val |= mask;
	else
		val &= ~mask;
	clk_writel(val, reg);
	return 0;
}

struct clk_ops tegra_plld_ops = {
	.init			= tegra30_pll_clk_init,
	.enable			= tegra30_pll_clk_enable,
	.disable		= tegra30_pll_clk_disable,
	.set_rate		= tegra30_pll_clk_set_rate,
	.clk_cfg_ex		= tegra30_plld_clk_cfg_ex,
};

static void tegra30_plle_clk_init(struct clk *c)
{
	u32 val;

	val = clk_readl(PLLE_AUX);
	c->parent = (val & PLLE_AUX_PLLP_SEL) ?
		tegra_get_clock_by_name("pll_p") :
		tegra_get_clock_by_name("pll_ref");

	val = clk_readl(c->reg + PLL_BASE);
	c->state = (val & PLLE_BASE_ENABLE) ? ON : OFF;
	c->mul = (val & PLLE_BASE_DIVN_MASK) >> PLLE_BASE_DIVN_SHIFT;
	c->div = (val & PLLE_BASE_DIVM_MASK) >> PLLE_BASE_DIVM_SHIFT;
	c->div *= (val & PLLE_BASE_DIVP_MASK) >> PLLE_BASE_DIVP_SHIFT;
}

static void tegra30_plle_clk_disable(struct clk *c)
{
	u32 val;
	pr_debug("%s on clock %s\n", __func__, c->name);

	val = clk_readl(c->reg + PLL_BASE);
	val &= ~(PLLE_BASE_CML_ENABLE | PLLE_BASE_ENABLE);
	clk_writel(val, c->reg + PLL_BASE);
}

static void tegra30_plle_training(struct clk *c)
{
	u32 val;

	/* PLLE is already disabled, and setup cleared;
	 * create falling edge on PLLE IDDQ input */
	val = pmc_readl(PMC_SATA_PWRGT);
	val |= PMC_SATA_PWRGT_PLLE_IDDQ_VALUE;
	pmc_writel(val, PMC_SATA_PWRGT);

	val = pmc_readl(PMC_SATA_PWRGT);
	val |= PMC_SATA_PWRGT_PLLE_IDDQ_SWCTL;
	pmc_writel(val, PMC_SATA_PWRGT);

	val = pmc_readl(PMC_SATA_PWRGT);
	val &= ~PMC_SATA_PWRGT_PLLE_IDDQ_VALUE;
	pmc_writel(val, PMC_SATA_PWRGT);

	do {
		val = clk_readl(c->reg + PLL_MISC(c));
	} while (!(val & PLLE_MISC_READY));
}

static int tegra30_plle_configure(struct clk *c, bool force_training)
{
	u32 val;
	const struct clk_pll_freq_table *sel;
	unsigned long rate = c->u.pll.fixed_rate;
	unsigned long input_rate = clk_get_rate(c->parent);

	for (sel = c->u.pll.freq_table; sel->input_rate != 0; sel++) {
		if (sel->input_rate == input_rate && sel->output_rate == rate)
			break;
	}

	if (sel->input_rate == 0)
		return -ENOSYS;

	/* disable PLLE, clear setup fiels */
	tegra30_plle_clk_disable(c);

	val = clk_readl(c->reg + PLL_MISC(c));
	val &= ~(PLLE_MISC_LOCK_ENABLE | PLLE_MISC_SETUP_MASK);
	clk_writel(val, c->reg + PLL_MISC(c));

	/* training */
	val = clk_readl(c->reg + PLL_MISC(c));
	if (force_training || (!(val & PLLE_MISC_READY)))
		tegra30_plle_training(c);

	/* configure dividers, setup, disable SS */
	val = clk_readl(c->reg + PLL_BASE);
	val &= ~PLLE_BASE_DIV_MASK;
	val |= PLLE_BASE_DIV(sel->m, sel->n, sel->p, sel->cpcon);
	clk_writel(val, c->reg + PLL_BASE);
	c->mul = sel->n;
	c->div = sel->m * sel->p;

	val = clk_readl(c->reg + PLL_MISC(c));
	val |= PLLE_MISC_SETUP_VALUE;
	val |= PLLE_MISC_LOCK_ENABLE;
	clk_writel(val, c->reg + PLL_MISC(c));

	val = clk_readl(PLLE_SS_CTRL);
	val |= PLLE_SS_DISABLE;
	clk_writel(val, PLLE_SS_CTRL);

	/* enable and lock PLLE*/
	val = clk_readl(c->reg + PLL_BASE);
	val |= (PLLE_BASE_CML_ENABLE | PLLE_BASE_ENABLE);
	clk_writel(val, c->reg + PLL_BASE);

	tegra30_pll_clk_wait_for_lock(c, c->reg + PLL_MISC(c), PLLE_MISC_LOCK);

	return 0;
}

static int tegra30_plle_clk_enable(struct clk *c)
{
	pr_debug("%s on clock %s\n", __func__, c->name);
	return tegra30_plle_configure(c, !c->set);
}

struct clk_ops tegra30_plle_ops = {
	.init			= tegra30_plle_clk_init,
	.enable			= tegra30_plle_clk_enable,
	.disable		= tegra30_plle_clk_disable,
};

/* Clock divider ops */
static void tegra30_pll_div_clk_init(struct clk *c)
{
	if (c->flags & DIV_U71) {
		u32 divu71;
		u32 val = clk_readl(c->reg);
		val >>= c->reg_shift;
		c->state = (val & PLL_OUT_CLKEN) ? ON : OFF;
		if (!(val & PLL_OUT_RESET_DISABLE))
			c->state = OFF;

		divu71 = (val & PLL_OUT_RATIO_MASK) >> PLL_OUT_RATIO_SHIFT;
		c->div = (divu71 + 2);
		c->mul = 2;
	} else if (c->flags & DIV_2) {
		c->state = ON;
		if (c->flags & (PLLD | PLLX)) {
			c->div = 2;
			c->mul = 1;
		} else
			BUG();
	} else {
		c->state = ON;
		c->div = 1;
		c->mul = 1;
	}
}

static int tegra30_pll_div_clk_enable(struct clk *c)
{
	u32 val;
	u32 new_val;

	pr_debug("%s: %s\n", __func__, c->name);
	if (c->flags & DIV_U71) {
		val = clk_readl(c->reg);
		new_val = val >> c->reg_shift;
		new_val &= 0xFFFF;

		new_val |= PLL_OUT_CLKEN | PLL_OUT_RESET_DISABLE;

		val &= ~(0xFFFF << c->reg_shift);
		val |= new_val << c->reg_shift;
		clk_writel_delay(val, c->reg);
		return 0;
	} else if (c->flags & DIV_2) {
		return 0;
	}
	return -EINVAL;
}

static void tegra30_pll_div_clk_disable(struct clk *c)
{
	u32 val;
	u32 new_val;

	pr_debug("%s: %s\n", __func__, c->name);
	if (c->flags & DIV_U71) {
		val = clk_readl(c->reg);
		new_val = val >> c->reg_shift;
		new_val &= 0xFFFF;

		new_val &= ~(PLL_OUT_CLKEN | PLL_OUT_RESET_DISABLE);

		val &= ~(0xFFFF << c->reg_shift);
		val |= new_val << c->reg_shift;
		clk_writel_delay(val, c->reg);
	}
}

static int tegra30_pll_div_clk_set_rate(struct clk *c, unsigned long rate)
{
	u32 val;
	u32 new_val;
	int divider_u71;
	unsigned long parent_rate = clk_get_rate(c->parent);

	pr_debug("%s: %s %lu\n", __func__, c->name, rate);
	if (c->flags & DIV_U71) {
		divider_u71 = clk_div71_get_divider(
			parent_rate, rate, c->flags, ROUND_DIVIDER_UP);
		if (divider_u71 >= 0) {
			val = clk_readl(c->reg);
			new_val = val >> c->reg_shift;
			new_val &= 0xFFFF;
			if (c->flags & DIV_U71_FIXED)
				new_val |= PLL_OUT_OVERRIDE;
			new_val &= ~PLL_OUT_RATIO_MASK;
			new_val |= divider_u71 << PLL_OUT_RATIO_SHIFT;

			val &= ~(0xFFFF << c->reg_shift);
			val |= new_val << c->reg_shift;
			clk_writel_delay(val, c->reg);
			c->div = divider_u71 + 2;
			c->mul = 2;
			return 0;
		}
	} else if (c->flags & DIV_2)
		return clk_set_rate(c->parent, rate * 2);

	return -EINVAL;
}

static long tegra30_pll_div_clk_round_rate(struct clk *c, unsigned long rate)
{
	int divider;
	unsigned long parent_rate = clk_get_rate(c->parent);
	pr_debug("%s: %s %lu\n", __func__, c->name, rate);

	if (c->flags & DIV_U71) {
		divider = clk_div71_get_divider(
			parent_rate, rate, c->flags, ROUND_DIVIDER_UP);
		if (divider < 0)
			return divider;
		return DIV_ROUND_UP(parent_rate * 2, divider + 2);
	} else if (c->flags & DIV_2)
		/* no rounding - fixed DIV_2 dividers pass rate to parent PLL */
		return rate;

	return -EINVAL;
}

struct clk_ops tegra30_pll_div_ops = {
	.init			= tegra30_pll_div_clk_init,
	.enable			= tegra30_pll_div_clk_enable,
	.disable		= tegra30_pll_div_clk_disable,
	.set_rate		= tegra30_pll_div_clk_set_rate,
	.round_rate		= tegra30_pll_div_clk_round_rate,
};

/* Periph clk ops */
static inline u32 periph_clk_source_mask(struct clk *c)
{
	if (c->flags & MUX8)
		return 7 << 29;
	else if (c->flags & MUX_PWM)
		return 3 << 28;
	else if (c->flags & MUX_CLK_OUT)
		return 3 << (c->u.periph.clk_num + 4);
	else if (c->flags & PLLD)
		return PLLD_BASE_DSIB_MUX_MASK;
	else
		return 3 << 30;
}

static inline u32 periph_clk_source_shift(struct clk *c)
{
	if (c->flags & MUX8)
		return 29;
	else if (c->flags & MUX_PWM)
		return 28;
	else if (c->flags & MUX_CLK_OUT)
		return c->u.periph.clk_num + 4;
	else if (c->flags & PLLD)
		return PLLD_BASE_DSIB_MUX_SHIFT;
	else
		return 30;
}

static void tegra30_periph_clk_init(struct clk *c)
{
	u32 val = clk_readl(c->reg);
	const struct clk_mux_sel *mux = 0;
	const struct clk_mux_sel *sel;
	if (c->flags & MUX) {
		for (sel = c->inputs; sel->input != NULL; sel++) {
			if (((val & periph_clk_source_mask(c)) >>
			    periph_clk_source_shift(c)) == sel->value)
				mux = sel;
		}
		BUG_ON(!mux);

		c->parent = mux->input;
	} else {
		c->parent = c->inputs[0].input;
	}

	if (c->flags & DIV_U71) {
		u32 divu71 = val & PERIPH_CLK_SOURCE_DIVU71_MASK;
		if ((c->flags & DIV_U71_UART) &&
		    (!(val & PERIPH_CLK_UART_DIV_ENB))) {
			divu71 = 0;
		}
		if (c->flags & DIV_U71_IDLE) {
			val &= ~(PERIPH_CLK_SOURCE_DIVU71_MASK <<
				PERIPH_CLK_SOURCE_DIVIDLE_SHIFT);
			val |= (PERIPH_CLK_SOURCE_DIVIDLE_VAL <<
				PERIPH_CLK_SOURCE_DIVIDLE_SHIFT);
			clk_writel(val, c->reg);
		}
		c->div = divu71 + 2;
		c->mul = 2;
	} else if (c->flags & DIV_U16) {
		u32 divu16 = val & PERIPH_CLK_SOURCE_DIVU16_MASK;
		c->div = divu16 + 1;
		c->mul = 1;
	} else {
		c->div = 1;
		c->mul = 1;
	}

	c->state = ON;
	if (!(clk_readl(PERIPH_CLK_TO_ENB_REG(c)) & PERIPH_CLK_TO_BIT(c)))
		c->state = OFF;
	if (!(c->flags & PERIPH_NO_RESET))
		if (clk_readl(PERIPH_CLK_TO_RST_REG(c)) & PERIPH_CLK_TO_BIT(c))
			c->state = OFF;
}

static int tegra30_periph_clk_enable(struct clk *c)
{
	pr_debug("%s on clock %s\n", __func__, c->name);

	tegra_periph_clk_enable_refcount[c->u.periph.clk_num]++;
	if (tegra_periph_clk_enable_refcount[c->u.periph.clk_num] > 1)
		return 0;

	clk_writel_delay(PERIPH_CLK_TO_BIT(c), PERIPH_CLK_TO_ENB_SET_REG(c));
	if (!(c->flags & PERIPH_NO_RESET) &&
		 !(c->flags & PERIPH_MANUAL_RESET)) {
		if (clk_readl(PERIPH_CLK_TO_RST_REG(c)) &
			 PERIPH_CLK_TO_BIT(c)) {
			udelay(5);	/* reset propagation delay */
			clk_writel(PERIPH_CLK_TO_BIT(c),
				 PERIPH_CLK_TO_RST_CLR_REG(c));
		}
	}
	return 0;
}

static void tegra30_periph_clk_disable(struct clk *c)
{
	unsigned long val;
	pr_debug("%s on clock %s\n", __func__, c->name);

	if (c->refcnt)
		tegra_periph_clk_enable_refcount[c->u.periph.clk_num]--;

	if (tegra_periph_clk_enable_refcount[c->u.periph.clk_num] == 0) {
		/* If peripheral is in the APB bus then read the APB bus to
		 * flush the write operation in apb bus. This will avoid the
		 * peripheral access after disabling clock*/
		if (c->flags & PERIPH_ON_APB)
			val = chipid_readl();

		clk_writel_delay(
			PERIPH_CLK_TO_BIT(c), PERIPH_CLK_TO_ENB_CLR_REG(c));
	}
}

static void tegra30_periph_clk_reset(struct clk *c, bool assert)
{
	unsigned long val;
	pr_debug("%s %s on clock %s\n", __func__,
		 assert ? "assert" : "deassert", c->name);

	if (!(c->flags & PERIPH_NO_RESET)) {
		if (assert) {
			/* If peripheral is in the APB bus then read the APB
			 * bus to flush the write operation in apb bus. This
			 * will avoid the peripheral access after disabling
			 * clock */
			if (c->flags & PERIPH_ON_APB)
				val = chipid_readl();

			clk_writel(PERIPH_CLK_TO_BIT(c),
				   PERIPH_CLK_TO_RST_SET_REG(c));
		} else
			clk_writel(PERIPH_CLK_TO_BIT(c),
				   PERIPH_CLK_TO_RST_CLR_REG(c));
	}
}

static int tegra30_periph_clk_set_parent(struct clk *c, struct clk *p)
{
	u32 val;
	const struct clk_mux_sel *sel;
	pr_debug("%s: %s %s\n", __func__, c->name, p->name);

	if (!(c->flags & MUX))
		return (p == c->parent) ? 0 : (-EINVAL);

	for (sel = c->inputs; sel->input != NULL; sel++) {
		if (sel->input == p) {
			val = clk_readl(c->reg);
			val &= ~periph_clk_source_mask(c);
			val |= (sel->value << periph_clk_source_shift(c));

			if (c->refcnt)
				clk_enable(p);

			clk_writel_delay(val, c->reg);

			if (c->refcnt && c->parent)
				clk_disable(c->parent);

			clk_reparent(c, p);
			return 0;
		}
	}

	return -EINVAL;
}

static int tegra30_periph_clk_set_rate(struct clk *c, unsigned long rate)
{
	u32 val;
	int divider;
	unsigned long parent_rate = clk_get_rate(c->parent);

	if (c->flags & DIV_U71) {
		divider = clk_div71_get_divider(
			parent_rate, rate, c->flags, ROUND_DIVIDER_UP);
		if (divider >= 0) {
			val = clk_readl(c->reg);
			val &= ~PERIPH_CLK_SOURCE_DIVU71_MASK;
			val |= divider;
			if (c->flags & DIV_U71_UART) {
				if (divider)
					val |= PERIPH_CLK_UART_DIV_ENB;
				else
					val &= ~PERIPH_CLK_UART_DIV_ENB;
			}
			clk_writel_delay(val, c->reg);
			c->div = divider + 2;
			c->mul = 2;
			return 0;
		}
	} else if (c->flags & DIV_U16) {
		divider = clk_div16_get_divider(parent_rate, rate);
		if (divider >= 0) {
			val = clk_readl(c->reg);
			val &= ~PERIPH_CLK_SOURCE_DIVU16_MASK;
			val |= divider;
			clk_writel_delay(val, c->reg);
			c->div = divider + 1;
			c->mul = 1;
			return 0;
		}
	} else if (parent_rate <= rate) {
		c->div = 1;
		c->mul = 1;
		return 0;
	}
	return -EINVAL;
}

static long tegra30_periph_clk_round_rate(struct clk *c,
	unsigned long rate)
{
	int divider;
	unsigned long parent_rate = clk_get_rate(c->parent);
	pr_debug("%s: %s %lu\n", __func__, c->name, rate);

	if (c->flags & DIV_U71) {
		divider = clk_div71_get_divider(
			parent_rate, rate, c->flags, ROUND_DIVIDER_UP);
		if (divider < 0)
			return divider;

		return DIV_ROUND_UP(parent_rate * 2, divider + 2);
	} else if (c->flags & DIV_U16) {
		divider = clk_div16_get_divider(parent_rate, rate);
		if (divider < 0)
			return divider;
		return DIV_ROUND_UP(parent_rate, divider + 1);
	}
	return -EINVAL;
}

struct clk_ops tegra30_periph_clk_ops = {
	.init			= &tegra30_periph_clk_init,
	.enable			= &tegra30_periph_clk_enable,
	.disable		= &tegra30_periph_clk_disable,
	.set_parent		= &tegra30_periph_clk_set_parent,
	.set_rate		= &tegra30_periph_clk_set_rate,
	.round_rate		= &tegra30_periph_clk_round_rate,
	.reset			= &tegra30_periph_clk_reset,
};

/* Periph extended clock configuration ops */
static int
tegra30_vi_clk_cfg_ex(struct clk *c, enum tegra_clk_ex_param p, u32 setting)
{
	if (p == TEGRA_CLK_VI_INP_SEL) {
		u32 val = clk_readl(c->reg);
		val &= ~PERIPH_CLK_VI_SEL_EX_MASK;
		val |= (setting << PERIPH_CLK_VI_SEL_EX_SHIFT) &
			PERIPH_CLK_VI_SEL_EX_MASK;
		clk_writel(val, c->reg);
		return 0;
	}
	return -EINVAL;
}

struct clk_ops tegra_vi_clk_ops = {
	.init			= &tegra30_periph_clk_init,
	.enable			= &tegra30_periph_clk_enable,
	.disable		= &tegra30_periph_clk_disable,
	.set_parent		= &tegra30_periph_clk_set_parent,
	.set_rate		= &tegra30_periph_clk_set_rate,
	.round_rate		= &tegra30_periph_clk_round_rate,
	.clk_cfg_ex		= &tegra30_vi_clk_cfg_ex,
	.reset			= &tegra30_periph_clk_reset,
};

static int
tegra30_nand_clk_cfg_ex(struct clk *c, enum tegra_clk_ex_param p, u32 setting)
{
	if (p == TEGRA_CLK_NAND_PAD_DIV2_ENB) {
		u32 val = clk_readl(c->reg);
		if (setting)
			val |= PERIPH_CLK_NAND_DIV_EX_ENB;
		else
			val &= ~PERIPH_CLK_NAND_DIV_EX_ENB;
		clk_writel(val, c->reg);
		return 0;
	}
	return -EINVAL;
}

struct clk_ops tegra_nand_clk_ops = {
	.init			= &tegra30_periph_clk_init,
	.enable			= &tegra30_periph_clk_enable,
	.disable		= &tegra30_periph_clk_disable,
	.set_parent		= &tegra30_periph_clk_set_parent,
	.set_rate		= &tegra30_periph_clk_set_rate,
	.round_rate		= &tegra30_periph_clk_round_rate,
	.clk_cfg_ex		= &tegra30_nand_clk_cfg_ex,
	.reset			= &tegra30_periph_clk_reset,
};

static int
tegra30_dtv_clk_cfg_ex(struct clk *c, enum tegra_clk_ex_param p, u32 setting)
{
	if (p == TEGRA_CLK_DTV_INVERT) {
		u32 val = clk_readl(c->reg);
		if (setting)
			val |= PERIPH_CLK_DTV_POLARITY_INV;
		else
			val &= ~PERIPH_CLK_DTV_POLARITY_INV;
		clk_writel(val, c->reg);
		return 0;
	}
	return -EINVAL;
}

struct clk_ops tegra_dtv_clk_ops = {
	.init			= &tegra30_periph_clk_init,
	.enable			= &tegra30_periph_clk_enable,
	.disable		= &tegra30_periph_clk_disable,
	.set_parent		= &tegra30_periph_clk_set_parent,
	.set_rate		= &tegra30_periph_clk_set_rate,
	.round_rate		= &tegra30_periph_clk_round_rate,
	.clk_cfg_ex		= &tegra30_dtv_clk_cfg_ex,
	.reset			= &tegra30_periph_clk_reset,
};

static int tegra30_dsib_clk_set_parent(struct clk *c, struct clk *p)
{
	const struct clk_mux_sel *sel;
	struct clk *d = tegra_get_clock_by_name("pll_d");

	pr_debug("%s: %s %s\n", __func__, c->name, p->name);

	for (sel = c->inputs; sel->input != NULL; sel++) {
		if (sel->input == p) {
			if (c->refcnt)
				clk_enable(p);

			/* The DSIB parent selection bit is in PLLD base
			   register - can not do direct r-m-w, must be
			   protected by PLLD lock */
			tegra_clk_cfg_ex(
				d, TEGRA_CLK_PLLD_MIPI_MUX_SEL, sel->value);

			if (c->refcnt && c->parent)
				clk_disable(c->parent);

			clk_reparent(c, p);
			return 0;
		}
	}

	return -EINVAL;
}

struct clk_ops tegra_dsib_clk_ops = {
	.init			= &tegra30_periph_clk_init,
	.enable			= &tegra30_periph_clk_enable,
	.disable		= &tegra30_periph_clk_disable,
	.set_parent		= &tegra30_dsib_clk_set_parent,
	.set_rate		= &tegra30_periph_clk_set_rate,
	.round_rate		= &tegra30_periph_clk_round_rate,
	.reset			= &tegra30_periph_clk_reset,
};

/* pciex clock support only reset function */
struct clk_ops tegra_pciex_clk_ops = {
	.reset    = tegra30_periph_clk_reset,
};

/* Output clock ops */

static DEFINE_SPINLOCK(clk_out_lock);

static void tegra30_clk_out_init(struct clk *c)
{
	const struct clk_mux_sel *mux = 0;
	const struct clk_mux_sel *sel;
	u32 val = pmc_readl(c->reg);

	c->state = (val & (0x1 << c->u.periph.clk_num)) ? ON : OFF;
	c->mul = 1;
	c->div = 1;

	for (sel = c->inputs; sel->input != NULL; sel++) {
		if (((val & periph_clk_source_mask(c)) >>
		     periph_clk_source_shift(c)) == sel->value)
			mux = sel;
	}
	BUG_ON(!mux);
	c->parent = mux->input;
}

static int tegra30_clk_out_enable(struct clk *c)
{
	u32 val;
	unsigned long flags;

	pr_debug("%s on clock %s\n", __func__, c->name);

	spin_lock_irqsave(&clk_out_lock, flags);
	val = pmc_readl(c->reg);
	val |= (0x1 << c->u.periph.clk_num);
	pmc_writel(val, c->reg);
	spin_unlock_irqrestore(&clk_out_lock, flags);

	return 0;
}

static void tegra30_clk_out_disable(struct clk *c)
{
	u32 val;
	unsigned long flags;

	pr_debug("%s on clock %s\n", __func__, c->name);

	spin_lock_irqsave(&clk_out_lock, flags);
	val = pmc_readl(c->reg);
	val &= ~(0x1 << c->u.periph.clk_num);
	pmc_writel(val, c->reg);
	spin_unlock_irqrestore(&clk_out_lock, flags);
}

static int tegra30_clk_out_set_parent(struct clk *c, struct clk *p)
{
	u32 val;
	unsigned long flags;
	const struct clk_mux_sel *sel;

	pr_debug("%s: %s %s\n", __func__, c->name, p->name);

	for (sel = c->inputs; sel->input != NULL; sel++) {
		if (sel->input == p) {
			if (c->refcnt)
				clk_enable(p);

			spin_lock_irqsave(&clk_out_lock, flags);
			val = pmc_readl(c->reg);
			val &= ~periph_clk_source_mask(c);
			val |= (sel->value << periph_clk_source_shift(c));
			pmc_writel(val, c->reg);
			spin_unlock_irqrestore(&clk_out_lock, flags);

			if (c->refcnt && c->parent)
				clk_disable(c->parent);

			clk_reparent(c, p);
			return 0;
		}
	}
	return -EINVAL;
}

struct clk_ops tegra_clk_out_ops = {
	.init			= &tegra30_clk_out_init,
	.enable			= &tegra30_clk_out_enable,
	.disable		= &tegra30_clk_out_disable,
	.set_parent		= &tegra30_clk_out_set_parent,
};

/* Clock doubler ops */
static void tegra30_clk_double_init(struct clk *c)
{
	u32 val = clk_readl(c->reg);
	c->mul = val & (0x1 << c->reg_shift) ? 1 : 2;
	c->div = 1;
	c->state = ON;
	if (!(clk_readl(PERIPH_CLK_TO_ENB_REG(c)) & PERIPH_CLK_TO_BIT(c)))
		c->state = OFF;
};

static int tegra30_clk_double_set_rate(struct clk *c, unsigned long rate)
{
	u32 val;
	unsigned long parent_rate = clk_get_rate(c->parent);
	if (rate == parent_rate) {
		val = clk_readl(c->reg) | (0x1 << c->reg_shift);
		clk_writel(val, c->reg);
		c->mul = 1;
		c->div = 1;
		return 0;
	} else if (rate == 2 * parent_rate) {
		val = clk_readl(c->reg) & (~(0x1 << c->reg_shift));
		clk_writel(val, c->reg);
		c->mul = 2;
		c->div = 1;
		return 0;
	}
	return -EINVAL;
}

struct clk_ops tegra30_clk_double_ops = {
	.init			= &tegra30_clk_double_init,
	.enable			= &tegra30_periph_clk_enable,
	.disable		= &tegra30_periph_clk_disable,
	.set_rate		= &tegra30_clk_double_set_rate,
};

/* Audio sync clock ops */
static int tegra30_sync_source_set_rate(struct clk *c, unsigned long rate)
{
	c->rate = rate;
	return 0;
}

struct clk_ops tegra_sync_source_ops = {
	.set_rate		= &tegra30_sync_source_set_rate,
};

static void tegra30_audio_sync_clk_init(struct clk *c)
{
	int source;
	const struct clk_mux_sel *sel;
	u32 val = clk_readl(c->reg);
	c->state = (val & AUDIO_SYNC_DISABLE_BIT) ? OFF : ON;
	source = val & AUDIO_SYNC_SOURCE_MASK;
	for (sel = c->inputs; sel->input != NULL; sel++)
		if (sel->value == source)
			break;
	BUG_ON(sel->input == NULL);
	c->parent = sel->input;
}

static int tegra30_audio_sync_clk_enable(struct clk *c)
{
	u32 val = clk_readl(c->reg);
	clk_writel((val & (~AUDIO_SYNC_DISABLE_BIT)), c->reg);
	return 0;
}

static void tegra30_audio_sync_clk_disable(struct clk *c)
{
	u32 val = clk_readl(c->reg);
	clk_writel((val | AUDIO_SYNC_DISABLE_BIT), c->reg);
}

static int tegra30_audio_sync_clk_set_parent(struct clk *c, struct clk *p)
{
	u32 val;
	const struct clk_mux_sel *sel;
	for (sel = c->inputs; sel->input != NULL; sel++) {
		if (sel->input == p) {
			val = clk_readl(c->reg);
			val &= ~AUDIO_SYNC_SOURCE_MASK;
			val |= sel->value;

			if (c->refcnt)
				clk_enable(p);

			clk_writel(val, c->reg);

			if (c->refcnt && c->parent)
				clk_disable(c->parent);

			clk_reparent(c, p);
			return 0;
		}
	}

	return -EINVAL;
}

struct clk_ops tegra30_audio_sync_clk_ops = {
	.init       = tegra30_audio_sync_clk_init,
	.enable     = tegra30_audio_sync_clk_enable,
	.disable    = tegra30_audio_sync_clk_disable,
	.set_parent = tegra30_audio_sync_clk_set_parent,
};

/* cml0 (pcie), and cml1 (sata) clock ops */
static void tegra30_cml_clk_init(struct clk *c)
{
	u32 val = clk_readl(c->reg);
	c->state = val & (0x1 << c->u.periph.clk_num) ? ON : OFF;
}

static int tegra30_cml_clk_enable(struct clk *c)
{
	u32 val = clk_readl(c->reg);
	val |= (0x1 << c->u.periph.clk_num);
	clk_writel(val, c->reg);
	return 0;
}

static void tegra30_cml_clk_disable(struct clk *c)
{
	u32 val = clk_readl(c->reg);
	val &= ~(0x1 << c->u.periph.clk_num);
	clk_writel(val, c->reg);
}

struct clk_ops tegra_cml_clk_ops = {
	.init			= &tegra30_cml_clk_init,
	.enable			= &tegra30_cml_clk_enable,
	.disable		= &tegra30_cml_clk_disable,
};
