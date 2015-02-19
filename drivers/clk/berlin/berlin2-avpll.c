/*
 * Copyright (c) 2014 Marvell Technology Group Ltd.
 *
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 * Alexandre Belloni <alexandre.belloni@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#include "berlin2-avpll.h"

/*
 * Berlin2 SoCs comprise up to two PLLs called AVPLL built upon a
 * VCO with 8 channels each, channel 8 is the odd-one-out and does
 * not provide mul/div.
 *
 * Unfortunately, its registers are not named but just numbered. To
 * get in at least some kind of structure, we split each AVPLL into
 * the VCOs and each channel into separate clock drivers.
 *
 * Also, here and there the VCO registers are a bit different with
 * respect to bit shifts. Make sure to add a comment for those.
 */
#define NUM_CHANNELS	8

#define AVPLL_CTRL(x)		((x) * 0x4)

#define VCO_CTRL0		AVPLL_CTRL(0)
/* BG2/BG2CDs VCO_B has an additional shift of 4 for its VCO_CTRL0 reg */
#define  VCO_RESET		BIT(0)
#define  VCO_POWERUP		BIT(1)
#define  VCO_INTERPOL_SHIFT	2
#define  VCO_INTERPOL_MASK	(0xf << VCO_INTERPOL_SHIFT)
#define  VCO_REG1V45_SEL_SHIFT	6
#define  VCO_REG1V45_SEL(x)	((x) << VCO_REG1V45_SEL_SHIFT)
#define  VCO_REG1V45_SEL_1V40	VCO_REG1V45_SEL(0)
#define  VCO_REG1V45_SEL_1V45	VCO_REG1V45_SEL(1)
#define  VCO_REG1V45_SEL_1V50	VCO_REG1V45_SEL(2)
#define  VCO_REG1V45_SEL_1V55	VCO_REG1V45_SEL(3)
#define  VCO_REG1V45_SEL_MASK	VCO_REG1V45_SEL(3)
#define  VCO_REG0V9_SEL_SHIFT	8
#define  VCO_REG0V9_SEL_MASK	(0xf << VCO_REG0V9_SEL_SHIFT)
#define  VCO_VTHCAL_SHIFT	12
#define  VCO_VTHCAL(x)		((x) << VCO_VTHCAL_SHIFT)
#define  VCO_VTHCAL_0V90	VCO_VTHCAL(0)
#define  VCO_VTHCAL_0V95	VCO_VTHCAL(1)
#define  VCO_VTHCAL_1V00	VCO_VTHCAL(2)
#define  VCO_VTHCAL_1V05	VCO_VTHCAL(3)
#define  VCO_VTHCAL_MASK	VCO_VTHCAL(3)
#define  VCO_KVCOEXT_SHIFT	14
#define  VCO_KVCOEXT_MASK	(0x3 << VCO_KVCOEXT_SHIFT)
#define  VCO_KVCOEXT_ENABLE	BIT(17)
#define  VCO_V2IEXT_SHIFT	18
#define  VCO_V2IEXT_MASK	(0xf << VCO_V2IEXT_SHIFT)
#define  VCO_V2IEXT_ENABLE	BIT(22)
#define  VCO_SPEED_SHIFT	23
#define  VCO_SPEED(x)		((x) << VCO_SPEED_SHIFT)
#define  VCO_SPEED_1G08_1G21	VCO_SPEED(0)
#define  VCO_SPEED_1G21_1G40	VCO_SPEED(1)
#define  VCO_SPEED_1G40_1G61	VCO_SPEED(2)
#define  VCO_SPEED_1G61_1G86	VCO_SPEED(3)
#define  VCO_SPEED_1G86_2G00	VCO_SPEED(4)
#define  VCO_SPEED_2G00_2G22	VCO_SPEED(5)
#define  VCO_SPEED_2G22		VCO_SPEED(6)
#define  VCO_SPEED_MASK		VCO_SPEED(0x7)
#define  VCO_CLKDET_ENABLE	BIT(26)
#define VCO_CTRL1		AVPLL_CTRL(1)
#define  VCO_REFDIV_SHIFT	0
#define  VCO_REFDIV(x)		((x) << VCO_REFDIV_SHIFT)
#define  VCO_REFDIV_1		VCO_REFDIV(0)
#define  VCO_REFDIV_2		VCO_REFDIV(1)
#define  VCO_REFDIV_4		VCO_REFDIV(2)
#define  VCO_REFDIV_3		VCO_REFDIV(3)
#define  VCO_REFDIV_MASK	VCO_REFDIV(0x3f)
#define  VCO_FBDIV_SHIFT	6
#define  VCO_FBDIV(x)		((x) << VCO_FBDIV_SHIFT)
#define  VCO_FBDIV_MASK		VCO_FBDIV(0xff)
#define  VCO_ICP_SHIFT		14
/* PLL Charge Pump Current = 10uA * (x + 1) */
#define  VCO_ICP(x)		((x) << VCO_ICP_SHIFT)
#define  VCO_ICP_MASK		VCO_ICP(0xf)
#define  VCO_LOAD_CAP		BIT(18)
#define  VCO_CALIBRATION_START	BIT(19)
#define VCO_FREQOFFSETn(x)	AVPLL_CTRL(3 + (x))
#define  VCO_FREQOFFSET_MASK	0x7ffff
#define VCO_CTRL10		AVPLL_CTRL(10)
#define  VCO_POWERUP_CH1	BIT(20)
#define VCO_CTRL11		AVPLL_CTRL(11)
#define VCO_CTRL12		AVPLL_CTRL(12)
#define VCO_CTRL13		AVPLL_CTRL(13)
#define VCO_CTRL14		AVPLL_CTRL(14)
#define VCO_CTRL15		AVPLL_CTRL(15)
#define VCO_SYNC1n(x)		AVPLL_CTRL(15 + (x))
#define  VCO_SYNC1_MASK		0x1ffff
#define VCO_SYNC2n(x)		AVPLL_CTRL(23 + (x))
#define  VCO_SYNC2_MASK		0x1ffff
#define VCO_CTRL30		AVPLL_CTRL(30)
#define  VCO_DPLL_CH1_ENABLE	BIT(17)

struct berlin2_avpll_vco {
	struct clk_hw hw;
	void __iomem *base;
	u8 flags;
};

#define to_avpll_vco(hw) container_of(hw, struct berlin2_avpll_vco, hw)

static int berlin2_avpll_vco_is_enabled(struct clk_hw *hw)
{
	struct berlin2_avpll_vco *vco = to_avpll_vco(hw);
	u32 reg;

	reg = readl_relaxed(vco->base + VCO_CTRL0);
	if (vco->flags & BERLIN2_AVPLL_BIT_QUIRK)
		reg >>= 4;

	return !!(reg & VCO_POWERUP);
}

static int berlin2_avpll_vco_enable(struct clk_hw *hw)
{
	struct berlin2_avpll_vco *vco = to_avpll_vco(hw);
	u32 reg;

	reg = readl_relaxed(vco->base + VCO_CTRL0);
	if (vco->flags & BERLIN2_AVPLL_BIT_QUIRK)
		reg |= VCO_POWERUP << 4;
	else
		reg |= VCO_POWERUP;
	writel_relaxed(reg, vco->base + VCO_CTRL0);

	return 0;
}

static void berlin2_avpll_vco_disable(struct clk_hw *hw)
{
	struct berlin2_avpll_vco *vco = to_avpll_vco(hw);
	u32 reg;

	reg = readl_relaxed(vco->base + VCO_CTRL0);
	if (vco->flags & BERLIN2_AVPLL_BIT_QUIRK)
		reg &= ~(VCO_POWERUP << 4);
	else
		reg &= ~VCO_POWERUP;
	writel_relaxed(reg, vco->base + VCO_CTRL0);
}

static u8 vco_refdiv[] = { 1, 2, 4, 3 };

static unsigned long
berlin2_avpll_vco_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct berlin2_avpll_vco *vco = to_avpll_vco(hw);
	u32 reg, refdiv, fbdiv;
	u64 freq = parent_rate;

	/* AVPLL VCO frequency: Fvco = (Fref / refdiv) * fbdiv */
	reg = readl_relaxed(vco->base + VCO_CTRL1);
	refdiv = (reg & VCO_REFDIV_MASK) >> VCO_REFDIV_SHIFT;
	refdiv = vco_refdiv[refdiv];
	fbdiv = (reg & VCO_FBDIV_MASK) >> VCO_FBDIV_SHIFT;
	freq *= fbdiv;
	do_div(freq, refdiv);

	return (unsigned long)freq;
}

static const struct clk_ops berlin2_avpll_vco_ops = {
	.is_enabled	= berlin2_avpll_vco_is_enabled,
	.enable		= berlin2_avpll_vco_enable,
	.disable	= berlin2_avpll_vco_disable,
	.recalc_rate	= berlin2_avpll_vco_recalc_rate,
};

struct clk * __init berlin2_avpll_vco_register(void __iomem *base,
			       const char *name, const char *parent_name,
			       u8 vco_flags, unsigned long flags)
{
	struct berlin2_avpll_vco *vco;
	struct clk_init_data init;

	vco = kzalloc(sizeof(*vco), GFP_KERNEL);
	if (!vco)
		return ERR_PTR(-ENOMEM);

	vco->base = base;
	vco->flags = vco_flags;
	vco->hw.init = &init;
	init.name = name;
	init.ops = &berlin2_avpll_vco_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = flags;

	return clk_register(NULL, &vco->hw);
}

struct berlin2_avpll_channel {
	struct clk_hw hw;
	void __iomem *base;
	u8 flags;
	u8 index;
};

#define to_avpll_channel(hw) container_of(hw, struct berlin2_avpll_channel, hw)

static int berlin2_avpll_channel_is_enabled(struct clk_hw *hw)
{
	struct berlin2_avpll_channel *ch = to_avpll_channel(hw);
	u32 reg;

	if (ch->index == 7)
		return 1;

	reg = readl_relaxed(ch->base + VCO_CTRL10);
	reg &= VCO_POWERUP_CH1 << ch->index;

	return !!reg;
}

static int berlin2_avpll_channel_enable(struct clk_hw *hw)
{
	struct berlin2_avpll_channel *ch = to_avpll_channel(hw);
	u32 reg;

	reg = readl_relaxed(ch->base + VCO_CTRL10);
	reg |= VCO_POWERUP_CH1 << ch->index;
	writel_relaxed(reg, ch->base + VCO_CTRL10);

	return 0;
}

static void berlin2_avpll_channel_disable(struct clk_hw *hw)
{
	struct berlin2_avpll_channel *ch = to_avpll_channel(hw);
	u32 reg;

	reg = readl_relaxed(ch->base + VCO_CTRL10);
	reg &= ~(VCO_POWERUP_CH1 << ch->index);
	writel_relaxed(reg, ch->base + VCO_CTRL10);
}

static const u8 div_hdmi[] = { 1, 2, 4, 6 };
static const u8 div_av1[] = { 1, 2, 5, 5 };

static unsigned long
berlin2_avpll_channel_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct berlin2_avpll_channel *ch = to_avpll_channel(hw);
	u32 reg, div_av2, div_av3, divider = 1;
	u64 freq = parent_rate;

	reg = readl_relaxed(ch->base + VCO_CTRL30);
	if ((reg & (VCO_DPLL_CH1_ENABLE << ch->index)) == 0)
		goto skip_div;

	/*
	 * Fch = (Fref * sync2) /
	 *    (sync1 * div_hdmi * div_av1 * div_av2 * div_av3)
	 */

	reg = readl_relaxed(ch->base + VCO_SYNC1n(ch->index));
	/* BG2/BG2CDs SYNC1 reg on AVPLL_B channel 1 is shifted by 4 */
	if (ch->flags & BERLIN2_AVPLL_BIT_QUIRK && ch->index == 0)
		reg >>= 4;
	divider = reg & VCO_SYNC1_MASK;

	reg = readl_relaxed(ch->base + VCO_SYNC2n(ch->index));
	freq *= reg & VCO_SYNC2_MASK;

	/* Channel 8 has no dividers */
	if (ch->index == 7)
		goto skip_div;

	/*
	 * HDMI divider start at VCO_CTRL11, bit 7; MSB is enable, lower 2 bit
	 * determine divider.
	 */
	reg = readl_relaxed(ch->base + VCO_CTRL11) >> 7;
	reg = (reg >> (ch->index * 3));
	if (reg & BIT(2))
		divider *= div_hdmi[reg & 0x3];

	/*
	 * AV1 divider start at VCO_CTRL11, bit 28; MSB is enable, lower 2 bit
	 * determine divider.
	 */
	if (ch->index == 0) {
		reg = readl_relaxed(ch->base + VCO_CTRL11);
		reg >>= 28;
	} else {
		reg = readl_relaxed(ch->base + VCO_CTRL12);
		reg >>= (ch->index-1) * 3;
	}
	if (reg & BIT(2))
		divider *= div_av1[reg & 0x3];

	/*
	 * AV2 divider start at VCO_CTRL12, bit 18; each 7 bits wide,
	 * zero is not a valid value.
	 */
	if (ch->index < 2) {
		reg = readl_relaxed(ch->base + VCO_CTRL12);
		reg >>= 18 + (ch->index * 7);
	} else if (ch->index < 7) {
		reg = readl_relaxed(ch->base + VCO_CTRL13);
		reg >>= (ch->index - 2) * 7;
	} else {
		reg = readl_relaxed(ch->base + VCO_CTRL14);
	}
	div_av2 = reg & 0x7f;
	if (div_av2)
		divider *= div_av2;

	/*
	 * AV3 divider start at VCO_CTRL14, bit 7; each 4 bits wide.
	 * AV2/AV3 form a fractional divider, where only specfic values for AV3
	 * are allowed. AV3 != 0 divides by AV2/2, AV3=0 is bypass.
	 */
	if (ch->index < 6) {
		reg = readl_relaxed(ch->base + VCO_CTRL14);
		reg >>= 7 + (ch->index * 4);
	} else {
		reg = readl_relaxed(ch->base + VCO_CTRL15);
	}
	div_av3 = reg & 0xf;
	if (div_av2 && div_av3)
		freq *= 2;

skip_div:
	do_div(freq, divider);
	return (unsigned long)freq;
}

static const struct clk_ops berlin2_avpll_channel_ops = {
	.is_enabled	= berlin2_avpll_channel_is_enabled,
	.enable		= berlin2_avpll_channel_enable,
	.disable	= berlin2_avpll_channel_disable,
	.recalc_rate	= berlin2_avpll_channel_recalc_rate,
};

/*
 * Another nice quirk:
 * On some production SoCs, AVPLL channels are scrambled with respect
 * to the channel numbering in the registers but still referenced by
 * their original channel numbers. We deal with it by having a flag
 * and a translation table for the index.
 */
static const u8 quirk_index[] __initconst = { 0, 6, 5, 4, 3, 2, 1, 7 };

struct clk * __init berlin2_avpll_channel_register(void __iomem *base,
			   const char *name, u8 index, const char *parent_name,
			   u8 ch_flags, unsigned long flags)
{
	struct berlin2_avpll_channel *ch;
	struct clk_init_data init;

	ch = kzalloc(sizeof(*ch), GFP_KERNEL);
	if (!ch)
		return ERR_PTR(-ENOMEM);

	ch->base = base;
	if (ch_flags & BERLIN2_AVPLL_SCRAMBLE_QUIRK)
		ch->index = quirk_index[index];
	else
		ch->index = index;

	ch->flags = ch_flags;
	ch->hw.init = &init;
	init.name = name;
	init.ops = &berlin2_avpll_channel_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = flags;

	return clk_register(NULL, &ch->hw);
}
