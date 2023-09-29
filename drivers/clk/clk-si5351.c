// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * clk-si5351.c: Skyworks / Silicon Labs Si5351A/B/C I2C Clock Generator
 *
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 * Rabeeh Khoury <rabeeh@solid-run.com>
 *
 * References:
 * [1] "Si5351A/B/C Data Sheet"
 *     https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/data-sheets/Si5351-B.pdf
 * [2] "AN619: Manually Generating an Si5351 Register Map"
 *     https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/AN619.pdf
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/rational.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/platform_data/si5351.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/div64.h>

#include "clk-si5351.h"

struct si5351_driver_data;

struct si5351_parameters {
	unsigned long	p1;
	unsigned long	p2;
	unsigned long	p3;
	int		valid;
};

struct si5351_hw_data {
	struct clk_hw			hw;
	struct si5351_driver_data	*drvdata;
	struct si5351_parameters	params;
	unsigned char			num;
};

struct si5351_driver_data {
	enum si5351_variant	variant;
	struct i2c_client	*client;
	struct regmap		*regmap;

	struct clk		*pxtal;
	const char		*pxtal_name;
	struct clk_hw		xtal;
	struct clk		*pclkin;
	const char		*pclkin_name;
	struct clk_hw		clkin;

	struct si5351_hw_data	pll[2];
	struct si5351_hw_data	*msynth;
	struct si5351_hw_data	*clkout;
	size_t			num_clkout;
};

static const char * const si5351_input_names[] = {
	"xtal", "clkin"
};
static const char * const si5351_pll_names[] = {
	"si5351_plla", "si5351_pllb", "si5351_vxco"
};
static const char * const si5351_msynth_names[] = {
	"ms0", "ms1", "ms2", "ms3", "ms4", "ms5", "ms6", "ms7"
};
static const char * const si5351_clkout_names[] = {
	"clk0", "clk1", "clk2", "clk3", "clk4", "clk5", "clk6", "clk7"
};

/*
 * Si5351 i2c regmap
 */
static inline u8 si5351_reg_read(struct si5351_driver_data *drvdata, u8 reg)
{
	u32 val;
	int ret;

	ret = regmap_read(drvdata->regmap, reg, &val);
	if (ret) {
		dev_err(&drvdata->client->dev,
			"unable to read from reg%02x\n", reg);
		return 0;
	}

	return (u8)val;
}

static inline int si5351_bulk_read(struct si5351_driver_data *drvdata,
				   u8 reg, u8 count, u8 *buf)
{
	return regmap_bulk_read(drvdata->regmap, reg, buf, count);
}

static inline int si5351_reg_write(struct si5351_driver_data *drvdata,
				   u8 reg, u8 val)
{
	return regmap_write(drvdata->regmap, reg, val);
}

static inline int si5351_bulk_write(struct si5351_driver_data *drvdata,
				    u8 reg, u8 count, const u8 *buf)
{
	return regmap_raw_write(drvdata->regmap, reg, buf, count);
}

static inline int si5351_set_bits(struct si5351_driver_data *drvdata,
				  u8 reg, u8 mask, u8 val)
{
	return regmap_update_bits(drvdata->regmap, reg, mask, val);
}

static inline u8 si5351_msynth_params_address(int num)
{
	if (num > 5)
		return SI5351_CLK6_PARAMETERS + (num - 6);
	return SI5351_CLK0_PARAMETERS + (SI5351_PARAMETERS_LENGTH * num);
}

static void si5351_read_parameters(struct si5351_driver_data *drvdata,
				   u8 reg, struct si5351_parameters *params)
{
	u8 buf[SI5351_PARAMETERS_LENGTH];

	switch (reg) {
	case SI5351_CLK6_PARAMETERS:
	case SI5351_CLK7_PARAMETERS:
		buf[0] = si5351_reg_read(drvdata, reg);
		params->p1 = buf[0];
		params->p2 = 0;
		params->p3 = 1;
		break;
	default:
		si5351_bulk_read(drvdata, reg, SI5351_PARAMETERS_LENGTH, buf);
		params->p1 = ((buf[2] & 0x03) << 16) | (buf[3] << 8) | buf[4];
		params->p2 = ((buf[5] & 0x0f) << 16) | (buf[6] << 8) | buf[7];
		params->p3 = ((buf[5] & 0xf0) << 12) | (buf[0] << 8) | buf[1];
	}
	params->valid = 1;
}

static void si5351_write_parameters(struct si5351_driver_data *drvdata,
				    u8 reg, struct si5351_parameters *params)
{
	u8 buf[SI5351_PARAMETERS_LENGTH];

	switch (reg) {
	case SI5351_CLK6_PARAMETERS:
	case SI5351_CLK7_PARAMETERS:
		buf[0] = params->p1 & 0xff;
		si5351_reg_write(drvdata, reg, buf[0]);
		break;
	default:
		buf[0] = ((params->p3 & 0x0ff00) >> 8) & 0xff;
		buf[1] = params->p3 & 0xff;
		/* save rdiv and divby4 */
		buf[2] = si5351_reg_read(drvdata, reg + 2) & ~0x03;
		buf[2] |= ((params->p1 & 0x30000) >> 16) & 0x03;
		buf[3] = ((params->p1 & 0x0ff00) >> 8) & 0xff;
		buf[4] = params->p1 & 0xff;
		buf[5] = ((params->p3 & 0xf0000) >> 12) |
			((params->p2 & 0xf0000) >> 16);
		buf[6] = ((params->p2 & 0x0ff00) >> 8) & 0xff;
		buf[7] = params->p2 & 0xff;
		si5351_bulk_write(drvdata, reg, SI5351_PARAMETERS_LENGTH, buf);
	}
}

static bool si5351_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SI5351_DEVICE_STATUS:
	case SI5351_INTERRUPT_STATUS:
	case SI5351_PLL_RESET:
		return true;
	}
	return false;
}

static bool si5351_regmap_is_writeable(struct device *dev, unsigned int reg)
{
	/* reserved registers */
	if (reg >= 4 && reg <= 8)
		return false;
	if (reg >= 10 && reg <= 14)
		return false;
	if (reg >= 173 && reg <= 176)
		return false;
	if (reg >= 178 && reg <= 182)
		return false;
	/* read-only */
	if (reg == SI5351_DEVICE_STATUS)
		return false;
	return true;
}

static const struct regmap_config si5351_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_MAPLE,
	.max_register = 187,
	.writeable_reg = si5351_regmap_is_writeable,
	.volatile_reg = si5351_regmap_is_volatile,
};

/*
 * Si5351 xtal clock input
 */
static int si5351_xtal_prepare(struct clk_hw *hw)
{
	struct si5351_driver_data *drvdata =
		container_of(hw, struct si5351_driver_data, xtal);
	si5351_set_bits(drvdata, SI5351_FANOUT_ENABLE,
			SI5351_XTAL_ENABLE, SI5351_XTAL_ENABLE);
	return 0;
}

static void si5351_xtal_unprepare(struct clk_hw *hw)
{
	struct si5351_driver_data *drvdata =
		container_of(hw, struct si5351_driver_data, xtal);
	si5351_set_bits(drvdata, SI5351_FANOUT_ENABLE,
			SI5351_XTAL_ENABLE, 0);
}

static const struct clk_ops si5351_xtal_ops = {
	.prepare = si5351_xtal_prepare,
	.unprepare = si5351_xtal_unprepare,
};

/*
 * Si5351 clkin clock input (Si5351C only)
 */
static int si5351_clkin_prepare(struct clk_hw *hw)
{
	struct si5351_driver_data *drvdata =
		container_of(hw, struct si5351_driver_data, clkin);
	si5351_set_bits(drvdata, SI5351_FANOUT_ENABLE,
			SI5351_CLKIN_ENABLE, SI5351_CLKIN_ENABLE);
	return 0;
}

static void si5351_clkin_unprepare(struct clk_hw *hw)
{
	struct si5351_driver_data *drvdata =
		container_of(hw, struct si5351_driver_data, clkin);
	si5351_set_bits(drvdata, SI5351_FANOUT_ENABLE,
			SI5351_CLKIN_ENABLE, 0);
}

/*
 * CMOS clock source constraints:
 * The input frequency range of the PLL is 10Mhz to 40MHz.
 * If CLKIN is >40MHz, the input divider must be used.
 */
static unsigned long si5351_clkin_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	struct si5351_driver_data *drvdata =
		container_of(hw, struct si5351_driver_data, clkin);
	unsigned long rate;
	unsigned char idiv;

	rate = parent_rate;
	if (parent_rate > 160000000) {
		idiv = SI5351_CLKIN_DIV_8;
		rate /= 8;
	} else if (parent_rate > 80000000) {
		idiv = SI5351_CLKIN_DIV_4;
		rate /= 4;
	} else if (parent_rate > 40000000) {
		idiv = SI5351_CLKIN_DIV_2;
		rate /= 2;
	} else {
		idiv = SI5351_CLKIN_DIV_1;
	}

	si5351_set_bits(drvdata, SI5351_PLL_INPUT_SOURCE,
			SI5351_CLKIN_DIV_MASK, idiv);

	dev_dbg(&drvdata->client->dev, "%s - clkin div = %d, rate = %lu\n",
		__func__, (1 << (idiv >> 6)), rate);

	return rate;
}

static const struct clk_ops si5351_clkin_ops = {
	.prepare = si5351_clkin_prepare,
	.unprepare = si5351_clkin_unprepare,
	.recalc_rate = si5351_clkin_recalc_rate,
};

/*
 * Si5351 vxco clock input (Si5351B only)
 */

static int si5351_vxco_prepare(struct clk_hw *hw)
{
	struct si5351_hw_data *hwdata =
		container_of(hw, struct si5351_hw_data, hw);

	dev_warn(&hwdata->drvdata->client->dev, "VXCO currently unsupported\n");

	return 0;
}

static void si5351_vxco_unprepare(struct clk_hw *hw)
{
}

static unsigned long si5351_vxco_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	return 0;
}

static int si5351_vxco_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent)
{
	return 0;
}

static const struct clk_ops si5351_vxco_ops = {
	.prepare = si5351_vxco_prepare,
	.unprepare = si5351_vxco_unprepare,
	.recalc_rate = si5351_vxco_recalc_rate,
	.set_rate = si5351_vxco_set_rate,
};

/*
 * Si5351 pll a/b
 *
 * Feedback Multisynth Divider Equations [2]
 *
 * fVCO = fIN * (a + b/c)
 *
 * with 15 + 0/1048575 <= (a + b/c) <= 90 + 0/1048575 and
 * fIN = fXTAL or fIN = fCLKIN/CLKIN_DIV
 *
 * Feedback Multisynth Register Equations
 *
 * (1) MSNx_P1[17:0] = 128 * a + floor(128 * b/c) - 512
 * (2) MSNx_P2[19:0] = 128 * b - c * floor(128 * b/c) = (128*b) mod c
 * (3) MSNx_P3[19:0] = c
 *
 * Transposing (2) yields: (4) floor(128 * b/c) = (128 * b / MSNx_P2)/c
 *
 * Using (4) on (1) yields:
 * MSNx_P1 = 128 * a + (128 * b/MSNx_P2)/c - 512
 * MSNx_P1 + 512 + MSNx_P2/c = 128 * a + 128 * b/c
 *
 * a + b/c = (MSNx_P1 + MSNx_P2/MSNx_P3 + 512)/128
 *         = (MSNx_P1*MSNx_P3 + MSNx_P2 + 512*MSNx_P3)/(128*MSNx_P3)
 *
 */
static int _si5351_pll_reparent(struct si5351_driver_data *drvdata,
				int num, enum si5351_pll_src parent)
{
	u8 mask = (num == 0) ? SI5351_PLLA_SOURCE : SI5351_PLLB_SOURCE;

	if (parent == SI5351_PLL_SRC_DEFAULT)
		return 0;

	if (num > 2)
		return -EINVAL;

	if (drvdata->variant != SI5351_VARIANT_C &&
	    parent != SI5351_PLL_SRC_XTAL)
		return -EINVAL;

	si5351_set_bits(drvdata, SI5351_PLL_INPUT_SOURCE, mask,
			(parent == SI5351_PLL_SRC_XTAL) ? 0 : mask);
	return 0;
}

static unsigned char si5351_pll_get_parent(struct clk_hw *hw)
{
	struct si5351_hw_data *hwdata =
		container_of(hw, struct si5351_hw_data, hw);
	u8 mask = (hwdata->num == 0) ? SI5351_PLLA_SOURCE : SI5351_PLLB_SOURCE;
	u8 val;

	val = si5351_reg_read(hwdata->drvdata, SI5351_PLL_INPUT_SOURCE);

	return (val & mask) ? 1 : 0;
}

static int si5351_pll_set_parent(struct clk_hw *hw, u8 index)
{
	struct si5351_hw_data *hwdata =
		container_of(hw, struct si5351_hw_data, hw);

	if (hwdata->drvdata->variant != SI5351_VARIANT_C &&
	    index > 0)
		return -EPERM;

	if (index > 1)
		return -EINVAL;

	return _si5351_pll_reparent(hwdata->drvdata, hwdata->num,
			     (index == 0) ? SI5351_PLL_SRC_XTAL :
			     SI5351_PLL_SRC_CLKIN);
}

static unsigned long si5351_pll_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct si5351_hw_data *hwdata =
		container_of(hw, struct si5351_hw_data, hw);
	u8 reg = (hwdata->num == 0) ? SI5351_PLLA_PARAMETERS :
		SI5351_PLLB_PARAMETERS;
	unsigned long long rate;

	if (!hwdata->params.valid)
		si5351_read_parameters(hwdata->drvdata, reg, &hwdata->params);

	if (hwdata->params.p3 == 0)
		return parent_rate;

	/* fVCO = fIN * (P1*P3 + 512*P3 + P2)/(128*P3) */
	rate  = hwdata->params.p1 * hwdata->params.p3;
	rate += 512 * hwdata->params.p3;
	rate += hwdata->params.p2;
	rate *= parent_rate;
	do_div(rate, 128 * hwdata->params.p3);

	dev_dbg(&hwdata->drvdata->client->dev,
		"%s - %s: p1 = %lu, p2 = %lu, p3 = %lu, parent_rate = %lu, rate = %lu\n",
		__func__, clk_hw_get_name(hw),
		hwdata->params.p1, hwdata->params.p2, hwdata->params.p3,
		parent_rate, (unsigned long)rate);

	return (unsigned long)rate;
}

static int si5351_pll_determine_rate(struct clk_hw *hw,
				     struct clk_rate_request *req)
{
	struct si5351_hw_data *hwdata =
		container_of(hw, struct si5351_hw_data, hw);
	unsigned long rate = req->rate;
	unsigned long rfrac, denom, a, b, c;
	unsigned long long lltmp;

	if (rate < SI5351_PLL_VCO_MIN)
		rate = SI5351_PLL_VCO_MIN;
	if (rate > SI5351_PLL_VCO_MAX)
		rate = SI5351_PLL_VCO_MAX;

	/* determine integer part of feedback equation */
	a = rate / req->best_parent_rate;

	if (a < SI5351_PLL_A_MIN)
		rate = req->best_parent_rate * SI5351_PLL_A_MIN;
	if (a > SI5351_PLL_A_MAX)
		rate = req->best_parent_rate * SI5351_PLL_A_MAX;

	/* find best approximation for b/c = fVCO mod fIN */
	denom = 1000 * 1000;
	lltmp = rate % (req->best_parent_rate);
	lltmp *= denom;
	do_div(lltmp, req->best_parent_rate);
	rfrac = (unsigned long)lltmp;

	b = 0;
	c = 1;
	if (rfrac)
		rational_best_approximation(rfrac, denom,
				    SI5351_PLL_B_MAX, SI5351_PLL_C_MAX, &b, &c);

	/* calculate parameters */
	hwdata->params.p3  = c;
	hwdata->params.p2  = (128 * b) % c;
	hwdata->params.p1  = 128 * a;
	hwdata->params.p1 += (128 * b / c);
	hwdata->params.p1 -= 512;

	/* recalculate rate by fIN * (a + b/c) */
	lltmp  = req->best_parent_rate;
	lltmp *= b;
	do_div(lltmp, c);

	rate  = (unsigned long)lltmp;
	rate += req->best_parent_rate * a;

	dev_dbg(&hwdata->drvdata->client->dev,
		"%s - %s: a = %lu, b = %lu, c = %lu, parent_rate = %lu, rate = %lu\n",
		__func__, clk_hw_get_name(hw), a, b, c,
		req->best_parent_rate, rate);

	req->rate = rate;
	return 0;
}

static int si5351_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct si5351_hw_data *hwdata =
		container_of(hw, struct si5351_hw_data, hw);
	u8 reg = (hwdata->num == 0) ? SI5351_PLLA_PARAMETERS :
		SI5351_PLLB_PARAMETERS;

	/* write multisynth parameters */
	si5351_write_parameters(hwdata->drvdata, reg, &hwdata->params);

	/* plla/pllb ctrl is in clk6/clk7 ctrl registers */
	si5351_set_bits(hwdata->drvdata, SI5351_CLK6_CTRL + hwdata->num,
		SI5351_CLK_INTEGER_MODE,
		(hwdata->params.p2 == 0) ? SI5351_CLK_INTEGER_MODE : 0);

	/* Do a pll soft reset on the affected pll */
	si5351_reg_write(hwdata->drvdata, SI5351_PLL_RESET,
			 hwdata->num == 0 ? SI5351_PLL_RESET_A :
					    SI5351_PLL_RESET_B);

	dev_dbg(&hwdata->drvdata->client->dev,
		"%s - %s: p1 = %lu, p2 = %lu, p3 = %lu, parent_rate = %lu, rate = %lu\n",
		__func__, clk_hw_get_name(hw),
		hwdata->params.p1, hwdata->params.p2, hwdata->params.p3,
		parent_rate, rate);

	return 0;
}

static const struct clk_ops si5351_pll_ops = {
	.set_parent = si5351_pll_set_parent,
	.get_parent = si5351_pll_get_parent,
	.recalc_rate = si5351_pll_recalc_rate,
	.determine_rate = si5351_pll_determine_rate,
	.set_rate = si5351_pll_set_rate,
};

/*
 * Si5351 multisync divider
 *
 * for fOUT <= 150 MHz:
 *
 * fOUT = (fIN * (a + b/c)) / CLKOUTDIV
 *
 * with 6 + 0/1048575 <= (a + b/c) <= 1800 + 0/1048575 and
 * fIN = fVCO0, fVCO1
 *
 * Output Clock Multisynth Register Equations
 *
 * MSx_P1[17:0] = 128 * a + floor(128 * b/c) - 512
 * MSx_P2[19:0] = 128 * b - c * floor(128 * b/c) = (128*b) mod c
 * MSx_P3[19:0] = c
 *
 * MS[6,7] are integer (P1) divide only, P1 = divide value,
 * P2 and P3 are not applicable
 *
 * for 150MHz < fOUT <= 160MHz:
 *
 * MSx_P1 = 0, MSx_P2 = 0, MSx_P3 = 1, MSx_INT = 1, MSx_DIVBY4 = 11b
 */
static int _si5351_msynth_reparent(struct si5351_driver_data *drvdata,
				   int num, enum si5351_multisynth_src parent)
{
	if (parent == SI5351_MULTISYNTH_SRC_DEFAULT)
		return 0;

	if (num > 8)
		return -EINVAL;

	si5351_set_bits(drvdata, SI5351_CLK0_CTRL + num, SI5351_CLK_PLL_SELECT,
			(parent == SI5351_MULTISYNTH_SRC_VCO0) ? 0 :
			SI5351_CLK_PLL_SELECT);
	return 0;
}

static unsigned char si5351_msynth_get_parent(struct clk_hw *hw)
{
	struct si5351_hw_data *hwdata =
		container_of(hw, struct si5351_hw_data, hw);
	u8 val;

	val = si5351_reg_read(hwdata->drvdata, SI5351_CLK0_CTRL + hwdata->num);

	return (val & SI5351_CLK_PLL_SELECT) ? 1 : 0;
}

static int si5351_msynth_set_parent(struct clk_hw *hw, u8 index)
{
	struct si5351_hw_data *hwdata =
		container_of(hw, struct si5351_hw_data, hw);

	return _si5351_msynth_reparent(hwdata->drvdata, hwdata->num,
			       (index == 0) ? SI5351_MULTISYNTH_SRC_VCO0 :
			       SI5351_MULTISYNTH_SRC_VCO1);
}

static unsigned long si5351_msynth_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct si5351_hw_data *hwdata =
		container_of(hw, struct si5351_hw_data, hw);
	u8 reg = si5351_msynth_params_address(hwdata->num);
	unsigned long long rate;
	unsigned long m;

	if (!hwdata->params.valid)
		si5351_read_parameters(hwdata->drvdata, reg, &hwdata->params);

	/*
	 * multisync0-5: fOUT = (128 * P3 * fIN) / (P1*P3 + P2 + 512*P3)
	 * multisync6-7: fOUT = fIN / P1
	 */
	rate = parent_rate;
	if (hwdata->num > 5) {
		m = hwdata->params.p1;
	} else if (hwdata->params.p3 == 0) {
		return parent_rate;
	} else if ((si5351_reg_read(hwdata->drvdata, reg + 2) &
		    SI5351_OUTPUT_CLK_DIVBY4) == SI5351_OUTPUT_CLK_DIVBY4) {
		m = 4;
	} else {
		rate *= 128 * hwdata->params.p3;
		m = hwdata->params.p1 * hwdata->params.p3;
		m += hwdata->params.p2;
		m += 512 * hwdata->params.p3;
	}

	if (m == 0)
		return 0;
	do_div(rate, m);

	dev_dbg(&hwdata->drvdata->client->dev,
		"%s - %s: p1 = %lu, p2 = %lu, p3 = %lu, m = %lu, parent_rate = %lu, rate = %lu\n",
		__func__, clk_hw_get_name(hw),
		hwdata->params.p1, hwdata->params.p2, hwdata->params.p3,
		m, parent_rate, (unsigned long)rate);

	return (unsigned long)rate;
}

static int si5351_msynth_determine_rate(struct clk_hw *hw,
					struct clk_rate_request *req)
{
	struct si5351_hw_data *hwdata =
		container_of(hw, struct si5351_hw_data, hw);
	unsigned long rate = req->rate;
	unsigned long long lltmp;
	unsigned long a, b, c;
	int divby4;

	/* multisync6-7 can only handle freqencies < 150MHz */
	if (hwdata->num >= 6 && rate > SI5351_MULTISYNTH67_MAX_FREQ)
		rate = SI5351_MULTISYNTH67_MAX_FREQ;

	/* multisync frequency is 1MHz .. 160MHz */
	if (rate > SI5351_MULTISYNTH_MAX_FREQ)
		rate = SI5351_MULTISYNTH_MAX_FREQ;
	if (rate < SI5351_MULTISYNTH_MIN_FREQ)
		rate = SI5351_MULTISYNTH_MIN_FREQ;

	divby4 = 0;
	if (rate > SI5351_MULTISYNTH_DIVBY4_FREQ)
		divby4 = 1;

	/* multisync can set pll */
	if (clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT) {
		/*
		 * find largest integer divider for max
		 * vco frequency and given target rate
		 */
		if (divby4 == 0) {
			lltmp = SI5351_PLL_VCO_MAX;
			do_div(lltmp, rate);
			a = (unsigned long)lltmp;
		} else
			a = 4;

		b = 0;
		c = 1;

		req->best_parent_rate = a * rate;
	} else if (hwdata->num >= 6) {
		/* determine the closest integer divider */
		a = DIV_ROUND_CLOSEST(req->best_parent_rate, rate);
		if (a < SI5351_MULTISYNTH_A_MIN)
			a = SI5351_MULTISYNTH_A_MIN;
		if (a > SI5351_MULTISYNTH67_A_MAX)
			a = SI5351_MULTISYNTH67_A_MAX;

		b = 0;
		c = 1;
	} else {
		unsigned long rfrac, denom;

		/* disable divby4 */
		if (divby4) {
			rate = SI5351_MULTISYNTH_DIVBY4_FREQ;
			divby4 = 0;
		}

		/* determine integer part of divider equation */
		a = req->best_parent_rate / rate;
		if (a < SI5351_MULTISYNTH_A_MIN)
			a = SI5351_MULTISYNTH_A_MIN;
		if (a > SI5351_MULTISYNTH_A_MAX)
			a = SI5351_MULTISYNTH_A_MAX;

		/* find best approximation for b/c = fVCO mod fOUT */
		denom = 1000 * 1000;
		lltmp = req->best_parent_rate % rate;
		lltmp *= denom;
		do_div(lltmp, rate);
		rfrac = (unsigned long)lltmp;

		b = 0;
		c = 1;
		if (rfrac)
			rational_best_approximation(rfrac, denom,
			    SI5351_MULTISYNTH_B_MAX, SI5351_MULTISYNTH_C_MAX,
			    &b, &c);
	}

	/* recalculate rate by fOUT = fIN / (a + b/c) */
	lltmp  = req->best_parent_rate;
	lltmp *= c;
	do_div(lltmp, a * c + b);
	rate  = (unsigned long)lltmp;

	/* calculate parameters */
	if (divby4) {
		hwdata->params.p3 = 1;
		hwdata->params.p2 = 0;
		hwdata->params.p1 = 0;
	} else if (hwdata->num >= 6) {
		hwdata->params.p3 = 0;
		hwdata->params.p2 = 0;
		hwdata->params.p1 = a;
	} else {
		hwdata->params.p3  = c;
		hwdata->params.p2  = (128 * b) % c;
		hwdata->params.p1  = 128 * a;
		hwdata->params.p1 += (128 * b / c);
		hwdata->params.p1 -= 512;
	}

	dev_dbg(&hwdata->drvdata->client->dev,
		"%s - %s: a = %lu, b = %lu, c = %lu, divby4 = %d, parent_rate = %lu, rate = %lu\n",
		__func__, clk_hw_get_name(hw), a, b, c, divby4,
		req->best_parent_rate, rate);

	req->rate = rate;

	return 0;
}

static int si5351_msynth_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	struct si5351_hw_data *hwdata =
		container_of(hw, struct si5351_hw_data, hw);
	u8 reg = si5351_msynth_params_address(hwdata->num);
	int divby4 = 0;

	/* write multisynth parameters */
	si5351_write_parameters(hwdata->drvdata, reg, &hwdata->params);

	if (rate > SI5351_MULTISYNTH_DIVBY4_FREQ)
		divby4 = 1;

	/* enable/disable integer mode and divby4 on multisynth0-5 */
	if (hwdata->num < 6) {
		si5351_set_bits(hwdata->drvdata, reg + 2,
				SI5351_OUTPUT_CLK_DIVBY4,
				(divby4) ? SI5351_OUTPUT_CLK_DIVBY4 : 0);
		si5351_set_bits(hwdata->drvdata, SI5351_CLK0_CTRL + hwdata->num,
			SI5351_CLK_INTEGER_MODE,
			(hwdata->params.p2 == 0) ? SI5351_CLK_INTEGER_MODE : 0);
	}

	dev_dbg(&hwdata->drvdata->client->dev,
		"%s - %s: p1 = %lu, p2 = %lu, p3 = %lu, divby4 = %d, parent_rate = %lu, rate = %lu\n",
		__func__, clk_hw_get_name(hw),
		hwdata->params.p1, hwdata->params.p2, hwdata->params.p3,
		divby4, parent_rate, rate);

	return 0;
}

static const struct clk_ops si5351_msynth_ops = {
	.set_parent = si5351_msynth_set_parent,
	.get_parent = si5351_msynth_get_parent,
	.recalc_rate = si5351_msynth_recalc_rate,
	.determine_rate = si5351_msynth_determine_rate,
	.set_rate = si5351_msynth_set_rate,
};

/*
 * Si5351 clkout divider
 */
static int _si5351_clkout_reparent(struct si5351_driver_data *drvdata,
				   int num, enum si5351_clkout_src parent)
{
	u8 val;

	if (num > 8)
		return -EINVAL;

	switch (parent) {
	case SI5351_CLKOUT_SRC_MSYNTH_N:
		val = SI5351_CLK_INPUT_MULTISYNTH_N;
		break;
	case SI5351_CLKOUT_SRC_MSYNTH_0_4:
		/* clk0/clk4 can only connect to its own multisync */
		if (num == 0 || num == 4)
			val = SI5351_CLK_INPUT_MULTISYNTH_N;
		else
			val = SI5351_CLK_INPUT_MULTISYNTH_0_4;
		break;
	case SI5351_CLKOUT_SRC_XTAL:
		val = SI5351_CLK_INPUT_XTAL;
		break;
	case SI5351_CLKOUT_SRC_CLKIN:
		if (drvdata->variant != SI5351_VARIANT_C)
			return -EINVAL;

		val = SI5351_CLK_INPUT_CLKIN;
		break;
	default:
		return 0;
	}

	si5351_set_bits(drvdata, SI5351_CLK0_CTRL + num,
			SI5351_CLK_INPUT_MASK, val);
	return 0;
}

static int _si5351_clkout_set_drive_strength(
	struct si5351_driver_data *drvdata, int num,
	enum si5351_drive_strength drive)
{
	u8 mask;

	if (num > 8)
		return -EINVAL;

	switch (drive) {
	case SI5351_DRIVE_2MA:
		mask = SI5351_CLK_DRIVE_STRENGTH_2MA;
		break;
	case SI5351_DRIVE_4MA:
		mask = SI5351_CLK_DRIVE_STRENGTH_4MA;
		break;
	case SI5351_DRIVE_6MA:
		mask = SI5351_CLK_DRIVE_STRENGTH_6MA;
		break;
	case SI5351_DRIVE_8MA:
		mask = SI5351_CLK_DRIVE_STRENGTH_8MA;
		break;
	default:
		return 0;
	}

	si5351_set_bits(drvdata, SI5351_CLK0_CTRL + num,
			SI5351_CLK_DRIVE_STRENGTH_MASK, mask);
	return 0;
}

static int _si5351_clkout_set_disable_state(
	struct si5351_driver_data *drvdata, int num,
	enum si5351_disable_state state)
{
	u8 reg = (num < 4) ? SI5351_CLK3_0_DISABLE_STATE :
		SI5351_CLK7_4_DISABLE_STATE;
	u8 shift = (num < 4) ? (2 * num) : (2 * (num-4));
	u8 mask = SI5351_CLK_DISABLE_STATE_MASK << shift;
	u8 val;

	if (num > 8)
		return -EINVAL;

	switch (state) {
	case SI5351_DISABLE_LOW:
		val = SI5351_CLK_DISABLE_STATE_LOW;
		break;
	case SI5351_DISABLE_HIGH:
		val = SI5351_CLK_DISABLE_STATE_HIGH;
		break;
	case SI5351_DISABLE_FLOATING:
		val = SI5351_CLK_DISABLE_STATE_FLOAT;
		break;
	case SI5351_DISABLE_NEVER:
		val = SI5351_CLK_DISABLE_STATE_NEVER;
		break;
	default:
		return 0;
	}

	si5351_set_bits(drvdata, reg, mask, val << shift);

	return 0;
}

static void _si5351_clkout_reset_pll(struct si5351_driver_data *drvdata, int num)
{
	u8 val = si5351_reg_read(drvdata, SI5351_CLK0_CTRL + num);
	u8 mask = val & SI5351_CLK_PLL_SELECT ? SI5351_PLL_RESET_B :
						       SI5351_PLL_RESET_A;
	unsigned int v;
	int err;

	switch (val & SI5351_CLK_INPUT_MASK) {
	case SI5351_CLK_INPUT_XTAL:
	case SI5351_CLK_INPUT_CLKIN:
		return;  /* pll not used, no need to reset */
	}

	si5351_reg_write(drvdata, SI5351_PLL_RESET, mask);

	err = regmap_read_poll_timeout(drvdata->regmap, SI5351_PLL_RESET, v,
				 !(v & mask), 0, 20000);
	if (err < 0)
		dev_err(&drvdata->client->dev, "Reset bit didn't clear\n");

	dev_dbg(&drvdata->client->dev, "%s - %s: pll = %d\n",
		__func__, clk_hw_get_name(&drvdata->clkout[num].hw),
		(val & SI5351_CLK_PLL_SELECT) ? 1 : 0);
}

static int si5351_clkout_prepare(struct clk_hw *hw)
{
	struct si5351_hw_data *hwdata =
		container_of(hw, struct si5351_hw_data, hw);
	struct si5351_platform_data *pdata =
		hwdata->drvdata->client->dev.platform_data;

	si5351_set_bits(hwdata->drvdata, SI5351_CLK0_CTRL + hwdata->num,
			SI5351_CLK_POWERDOWN, 0);

	/*
	 * Do a pll soft reset on the parent pll -- needed to get a
	 * deterministic phase relationship between the output clocks.
	 */
	if (pdata->clkout[hwdata->num].pll_reset)
		_si5351_clkout_reset_pll(hwdata->drvdata, hwdata->num);

	si5351_set_bits(hwdata->drvdata, SI5351_OUTPUT_ENABLE_CTRL,
			(1 << hwdata->num), 0);
	return 0;
}

static void si5351_clkout_unprepare(struct clk_hw *hw)
{
	struct si5351_hw_data *hwdata =
		container_of(hw, struct si5351_hw_data, hw);

	si5351_set_bits(hwdata->drvdata, SI5351_CLK0_CTRL + hwdata->num,
			SI5351_CLK_POWERDOWN, SI5351_CLK_POWERDOWN);
	si5351_set_bits(hwdata->drvdata, SI5351_OUTPUT_ENABLE_CTRL,
			(1 << hwdata->num), (1 << hwdata->num));
}

static u8 si5351_clkout_get_parent(struct clk_hw *hw)
{
	struct si5351_hw_data *hwdata =
		container_of(hw, struct si5351_hw_data, hw);
	int index = 0;
	unsigned char val;

	val = si5351_reg_read(hwdata->drvdata, SI5351_CLK0_CTRL + hwdata->num);
	switch (val & SI5351_CLK_INPUT_MASK) {
	case SI5351_CLK_INPUT_MULTISYNTH_N:
		index = 0;
		break;
	case SI5351_CLK_INPUT_MULTISYNTH_0_4:
		index = 1;
		break;
	case SI5351_CLK_INPUT_XTAL:
		index = 2;
		break;
	case SI5351_CLK_INPUT_CLKIN:
		index = 3;
		break;
	}

	return index;
}

static int si5351_clkout_set_parent(struct clk_hw *hw, u8 index)
{
	struct si5351_hw_data *hwdata =
		container_of(hw, struct si5351_hw_data, hw);
	enum si5351_clkout_src parent = SI5351_CLKOUT_SRC_DEFAULT;

	switch (index) {
	case 0:
		parent = SI5351_CLKOUT_SRC_MSYNTH_N;
		break;
	case 1:
		parent = SI5351_CLKOUT_SRC_MSYNTH_0_4;
		break;
	case 2:
		parent = SI5351_CLKOUT_SRC_XTAL;
		break;
	case 3:
		parent = SI5351_CLKOUT_SRC_CLKIN;
		break;
	}

	return _si5351_clkout_reparent(hwdata->drvdata, hwdata->num, parent);
}

static unsigned long si5351_clkout_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct si5351_hw_data *hwdata =
		container_of(hw, struct si5351_hw_data, hw);
	unsigned char reg;
	unsigned char rdiv;

	if (hwdata->num <= 5)
		reg = si5351_msynth_params_address(hwdata->num) + 2;
	else
		reg = SI5351_CLK6_7_OUTPUT_DIVIDER;

	rdiv = si5351_reg_read(hwdata->drvdata, reg);
	if (hwdata->num == 6) {
		rdiv &= SI5351_OUTPUT_CLK6_DIV_MASK;
	} else {
		rdiv &= SI5351_OUTPUT_CLK_DIV_MASK;
		rdiv >>= SI5351_OUTPUT_CLK_DIV_SHIFT;
	}

	return parent_rate >> rdiv;
}

static int si5351_clkout_determine_rate(struct clk_hw *hw,
					struct clk_rate_request *req)
{
	struct si5351_hw_data *hwdata =
		container_of(hw, struct si5351_hw_data, hw);
	unsigned long rate = req->rate;
	unsigned char rdiv;

	/* clkout6/7 can only handle output freqencies < 150MHz */
	if (hwdata->num >= 6 && rate > SI5351_CLKOUT67_MAX_FREQ)
		rate = SI5351_CLKOUT67_MAX_FREQ;

	/* clkout freqency is 8kHz - 160MHz */
	if (rate > SI5351_CLKOUT_MAX_FREQ)
		rate = SI5351_CLKOUT_MAX_FREQ;
	if (rate < SI5351_CLKOUT_MIN_FREQ)
		rate = SI5351_CLKOUT_MIN_FREQ;

	/* request frequency if multisync master */
	if (clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT) {
		/* use r divider for frequencies below 1MHz */
		rdiv = SI5351_OUTPUT_CLK_DIV_1;
		while (rate < SI5351_MULTISYNTH_MIN_FREQ &&
		       rdiv < SI5351_OUTPUT_CLK_DIV_128) {
			rdiv += 1;
			rate *= 2;
		}
		req->best_parent_rate = rate;
	} else {
		unsigned long new_rate, new_err, err;

		/* round to closed rdiv */
		rdiv = SI5351_OUTPUT_CLK_DIV_1;
		new_rate = req->best_parent_rate;
		err = abs(new_rate - rate);
		do {
			new_rate >>= 1;
			new_err = abs(new_rate - rate);
			if (new_err > err || rdiv == SI5351_OUTPUT_CLK_DIV_128)
				break;
			rdiv++;
			err = new_err;
		} while (1);
	}
	rate = req->best_parent_rate >> rdiv;

	dev_dbg(&hwdata->drvdata->client->dev,
		"%s - %s: rdiv = %u, parent_rate = %lu, rate = %lu\n",
		__func__, clk_hw_get_name(hw), (1 << rdiv),
		req->best_parent_rate, rate);

	req->rate = rate;
	return 0;
}

static int si5351_clkout_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	struct si5351_hw_data *hwdata =
		container_of(hw, struct si5351_hw_data, hw);
	unsigned long new_rate, new_err, err;
	unsigned char rdiv;

	/* round to closed rdiv */
	rdiv = SI5351_OUTPUT_CLK_DIV_1;
	new_rate = parent_rate;
	err = abs(new_rate - rate);
	do {
		new_rate >>= 1;
		new_err = abs(new_rate - rate);
		if (new_err > err || rdiv == SI5351_OUTPUT_CLK_DIV_128)
			break;
		rdiv++;
		err = new_err;
	} while (1);

	/* write output divider */
	switch (hwdata->num) {
	case 6:
		si5351_set_bits(hwdata->drvdata, SI5351_CLK6_7_OUTPUT_DIVIDER,
				SI5351_OUTPUT_CLK6_DIV_MASK, rdiv);
		break;
	case 7:
		si5351_set_bits(hwdata->drvdata, SI5351_CLK6_7_OUTPUT_DIVIDER,
				SI5351_OUTPUT_CLK_DIV_MASK,
				rdiv << SI5351_OUTPUT_CLK_DIV_SHIFT);
		break;
	default:
		si5351_set_bits(hwdata->drvdata,
				si5351_msynth_params_address(hwdata->num) + 2,
				SI5351_OUTPUT_CLK_DIV_MASK,
				rdiv << SI5351_OUTPUT_CLK_DIV_SHIFT);
	}

	/* powerup clkout */
	si5351_set_bits(hwdata->drvdata, SI5351_CLK0_CTRL + hwdata->num,
			SI5351_CLK_POWERDOWN, 0);

	dev_dbg(&hwdata->drvdata->client->dev,
		"%s - %s: rdiv = %u, parent_rate = %lu, rate = %lu\n",
		__func__, clk_hw_get_name(hw), (1 << rdiv),
		parent_rate, rate);

	return 0;
}

static const struct clk_ops si5351_clkout_ops = {
	.prepare = si5351_clkout_prepare,
	.unprepare = si5351_clkout_unprepare,
	.set_parent = si5351_clkout_set_parent,
	.get_parent = si5351_clkout_get_parent,
	.recalc_rate = si5351_clkout_recalc_rate,
	.determine_rate = si5351_clkout_determine_rate,
	.set_rate = si5351_clkout_set_rate,
};

/*
 * Si5351 i2c probe and DT
 */
#ifdef CONFIG_OF
static const struct of_device_id si5351_dt_ids[] = {
	{ .compatible = "silabs,si5351a", .data = (void *)SI5351_VARIANT_A, },
	{ .compatible = "silabs,si5351a-msop",
					 .data = (void *)SI5351_VARIANT_A3, },
	{ .compatible = "silabs,si5351b", .data = (void *)SI5351_VARIANT_B, },
	{ .compatible = "silabs,si5351c", .data = (void *)SI5351_VARIANT_C, },
	{ }
};
MODULE_DEVICE_TABLE(of, si5351_dt_ids);

static int si5351_dt_parse(struct i2c_client *client,
			   enum si5351_variant variant)
{
	struct device_node *child, *np = client->dev.of_node;
	struct si5351_platform_data *pdata;
	struct property *prop;
	const __be32 *p;
	int num = 0;
	u32 val;

	if (np == NULL)
		return 0;

	pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	/*
	 * property silabs,pll-source : <num src>, [<..>]
	 * allow to selectively set pll source
	 */
	of_property_for_each_u32(np, "silabs,pll-source", prop, p, num) {
		if (num >= 2) {
			dev_err(&client->dev,
				"invalid pll %d on pll-source prop\n", num);
			return -EINVAL;
		}

		p = of_prop_next_u32(prop, p, &val);
		if (!p) {
			dev_err(&client->dev,
				"missing pll-source for pll %d\n", num);
			return -EINVAL;
		}

		switch (val) {
		case 0:
			pdata->pll_src[num] = SI5351_PLL_SRC_XTAL;
			break;
		case 1:
			if (variant != SI5351_VARIANT_C) {
				dev_err(&client->dev,
					"invalid parent %d for pll %d\n",
					val, num);
				return -EINVAL;
			}
			pdata->pll_src[num] = SI5351_PLL_SRC_CLKIN;
			break;
		default:
			dev_err(&client->dev,
				 "invalid parent %d for pll %d\n", val, num);
			return -EINVAL;
		}
	}

	/* per clkout properties */
	for_each_child_of_node(np, child) {
		if (of_property_read_u32(child, "reg", &num)) {
			dev_err(&client->dev, "missing reg property of %pOFn\n",
				child);
			goto put_child;
		}

		if (num >= 8 ||
		    (variant == SI5351_VARIANT_A3 && num >= 3)) {
			dev_err(&client->dev, "invalid clkout %d\n", num);
			goto put_child;
		}

		if (!of_property_read_u32(child, "silabs,multisynth-source",
					  &val)) {
			switch (val) {
			case 0:
				pdata->clkout[num].multisynth_src =
					SI5351_MULTISYNTH_SRC_VCO0;
				break;
			case 1:
				pdata->clkout[num].multisynth_src =
					SI5351_MULTISYNTH_SRC_VCO1;
				break;
			default:
				dev_err(&client->dev,
					"invalid parent %d for multisynth %d\n",
					val, num);
				goto put_child;
			}
		}

		if (!of_property_read_u32(child, "silabs,clock-source", &val)) {
			switch (val) {
			case 0:
				pdata->clkout[num].clkout_src =
					SI5351_CLKOUT_SRC_MSYNTH_N;
				break;
			case 1:
				pdata->clkout[num].clkout_src =
					SI5351_CLKOUT_SRC_MSYNTH_0_4;
				break;
			case 2:
				pdata->clkout[num].clkout_src =
					SI5351_CLKOUT_SRC_XTAL;
				break;
			case 3:
				if (variant != SI5351_VARIANT_C) {
					dev_err(&client->dev,
						"invalid parent %d for clkout %d\n",
						val, num);
					goto put_child;
				}
				pdata->clkout[num].clkout_src =
					SI5351_CLKOUT_SRC_CLKIN;
				break;
			default:
				dev_err(&client->dev,
					"invalid parent %d for clkout %d\n",
					val, num);
				goto put_child;
			}
		}

		if (!of_property_read_u32(child, "silabs,drive-strength",
					  &val)) {
			switch (val) {
			case SI5351_DRIVE_2MA:
			case SI5351_DRIVE_4MA:
			case SI5351_DRIVE_6MA:
			case SI5351_DRIVE_8MA:
				pdata->clkout[num].drive = val;
				break;
			default:
				dev_err(&client->dev,
					"invalid drive strength %d for clkout %d\n",
					val, num);
				goto put_child;
			}
		}

		if (!of_property_read_u32(child, "silabs,disable-state",
					  &val)) {
			switch (val) {
			case 0:
				pdata->clkout[num].disable_state =
					SI5351_DISABLE_LOW;
				break;
			case 1:
				pdata->clkout[num].disable_state =
					SI5351_DISABLE_HIGH;
				break;
			case 2:
				pdata->clkout[num].disable_state =
					SI5351_DISABLE_FLOATING;
				break;
			case 3:
				pdata->clkout[num].disable_state =
					SI5351_DISABLE_NEVER;
				break;
			default:
				dev_err(&client->dev,
					"invalid disable state %d for clkout %d\n",
					val, num);
				goto put_child;
			}
		}

		if (!of_property_read_u32(child, "clock-frequency", &val))
			pdata->clkout[num].rate = val;

		pdata->clkout[num].pll_master =
			of_property_read_bool(child, "silabs,pll-master");

		pdata->clkout[num].pll_reset =
			of_property_read_bool(child, "silabs,pll-reset");
	}
	client->dev.platform_data = pdata;

	return 0;
put_child:
	of_node_put(child);
	return -EINVAL;
}

static struct clk_hw *
si53351_of_clk_get(struct of_phandle_args *clkspec, void *data)
{
	struct si5351_driver_data *drvdata = data;
	unsigned int idx = clkspec->args[0];

	if (idx >= drvdata->num_clkout) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return &drvdata->clkout[idx].hw;
}
#else
static int si5351_dt_parse(struct i2c_client *client, enum si5351_variant variant)
{
	return 0;
}

static struct clk_hw *
si53351_of_clk_get(struct of_phandle_args *clkspec, void *data)
{
	return NULL;
}
#endif /* CONFIG_OF */

static const struct i2c_device_id si5351_i2c_ids[] = {
	{ "si5351a", SI5351_VARIANT_A },
	{ "si5351a-msop", SI5351_VARIANT_A3 },
	{ "si5351b", SI5351_VARIANT_B },
	{ "si5351c", SI5351_VARIANT_C },
	{ }
};
MODULE_DEVICE_TABLE(i2c, si5351_i2c_ids);

static int si5351_i2c_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_match_id(si5351_i2c_ids, client);
	enum si5351_variant variant = (enum si5351_variant)id->driver_data;
	struct si5351_platform_data *pdata;
	struct si5351_driver_data *drvdata;
	struct clk_init_data init;
	const char *parent_names[4];
	u8 num_parents, num_clocks;
	int ret, n;

	ret = si5351_dt_parse(client, variant);
	if (ret)
		return ret;

	pdata = client->dev.platform_data;
	if (!pdata)
		return -EINVAL;

	drvdata = devm_kzalloc(&client->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	i2c_set_clientdata(client, drvdata);
	drvdata->client = client;
	drvdata->variant = variant;
	drvdata->pxtal = devm_clk_get(&client->dev, "xtal");
	drvdata->pclkin = devm_clk_get(&client->dev, "clkin");

	if (PTR_ERR(drvdata->pxtal) == -EPROBE_DEFER ||
	    PTR_ERR(drvdata->pclkin) == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	/*
	 * Check for valid parent clock: VARIANT_A and VARIANT_B need XTAL,
	 *   VARIANT_C can have CLKIN instead.
	 */
	if (IS_ERR(drvdata->pxtal) &&
	    (drvdata->variant != SI5351_VARIANT_C || IS_ERR(drvdata->pclkin))) {
		dev_err(&client->dev, "missing parent clock\n");
		return -EINVAL;
	}

	drvdata->regmap = devm_regmap_init_i2c(client, &si5351_regmap_config);
	if (IS_ERR(drvdata->regmap)) {
		dev_err(&client->dev, "failed to allocate register map\n");
		return PTR_ERR(drvdata->regmap);
	}

	/* Disable interrupts */
	si5351_reg_write(drvdata, SI5351_INTERRUPT_MASK, 0xf0);
	/* Ensure pll select is on XTAL for Si5351A/B */
	if (drvdata->variant != SI5351_VARIANT_C)
		si5351_set_bits(drvdata, SI5351_PLL_INPUT_SOURCE,
				SI5351_PLLA_SOURCE | SI5351_PLLB_SOURCE, 0);

	/* setup clock configuration */
	for (n = 0; n < 2; n++) {
		ret = _si5351_pll_reparent(drvdata, n, pdata->pll_src[n]);
		if (ret) {
			dev_err(&client->dev,
				"failed to reparent pll %d to %d\n",
				n, pdata->pll_src[n]);
			return ret;
		}
	}

	for (n = 0; n < 8; n++) {
		ret = _si5351_msynth_reparent(drvdata, n,
					      pdata->clkout[n].multisynth_src);
		if (ret) {
			dev_err(&client->dev,
				"failed to reparent multisynth %d to %d\n",
				n, pdata->clkout[n].multisynth_src);
			return ret;
		}

		ret = _si5351_clkout_reparent(drvdata, n,
					      pdata->clkout[n].clkout_src);
		if (ret) {
			dev_err(&client->dev,
				"failed to reparent clkout %d to %d\n",
				n, pdata->clkout[n].clkout_src);
			return ret;
		}

		ret = _si5351_clkout_set_drive_strength(drvdata, n,
							pdata->clkout[n].drive);
		if (ret) {
			dev_err(&client->dev,
				"failed set drive strength of clkout%d to %d\n",
				n, pdata->clkout[n].drive);
			return ret;
		}

		ret = _si5351_clkout_set_disable_state(drvdata, n,
						pdata->clkout[n].disable_state);
		if (ret) {
			dev_err(&client->dev,
				"failed set disable state of clkout%d to %d\n",
				n, pdata->clkout[n].disable_state);
			return ret;
		}
	}

	/* register xtal input clock gate */
	memset(&init, 0, sizeof(init));
	init.name = si5351_input_names[0];
	init.ops = &si5351_xtal_ops;
	init.flags = 0;
	if (!IS_ERR(drvdata->pxtal)) {
		drvdata->pxtal_name = __clk_get_name(drvdata->pxtal);
		init.parent_names = &drvdata->pxtal_name;
		init.num_parents = 1;
	}
	drvdata->xtal.init = &init;
	ret = devm_clk_hw_register(&client->dev, &drvdata->xtal);
	if (ret) {
		dev_err(&client->dev, "unable to register %s\n", init.name);
		return ret;
	}

	/* register clkin input clock gate */
	if (drvdata->variant == SI5351_VARIANT_C) {
		memset(&init, 0, sizeof(init));
		init.name = si5351_input_names[1];
		init.ops = &si5351_clkin_ops;
		if (!IS_ERR(drvdata->pclkin)) {
			drvdata->pclkin_name = __clk_get_name(drvdata->pclkin);
			init.parent_names = &drvdata->pclkin_name;
			init.num_parents = 1;
		}
		drvdata->clkin.init = &init;
		ret = devm_clk_hw_register(&client->dev, &drvdata->clkin);
		if (ret) {
			dev_err(&client->dev, "unable to register %s\n",
				init.name);
			return ret;
		}
	}

	/* Si5351C allows to mux either xtal or clkin to PLL input */
	num_parents = (drvdata->variant == SI5351_VARIANT_C) ? 2 : 1;
	parent_names[0] = si5351_input_names[0];
	parent_names[1] = si5351_input_names[1];

	/* register PLLA */
	drvdata->pll[0].num = 0;
	drvdata->pll[0].drvdata = drvdata;
	drvdata->pll[0].hw.init = &init;
	memset(&init, 0, sizeof(init));
	init.name = si5351_pll_names[0];
	init.ops = &si5351_pll_ops;
	init.flags = 0;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	ret = devm_clk_hw_register(&client->dev, &drvdata->pll[0].hw);
	if (ret) {
		dev_err(&client->dev, "unable to register %s\n", init.name);
		return ret;
	}

	/* register PLLB or VXCO (Si5351B) */
	drvdata->pll[1].num = 1;
	drvdata->pll[1].drvdata = drvdata;
	drvdata->pll[1].hw.init = &init;
	memset(&init, 0, sizeof(init));
	if (drvdata->variant == SI5351_VARIANT_B) {
		init.name = si5351_pll_names[2];
		init.ops = &si5351_vxco_ops;
		init.flags = 0;
		init.parent_names = NULL;
		init.num_parents = 0;
	} else {
		init.name = si5351_pll_names[1];
		init.ops = &si5351_pll_ops;
		init.flags = 0;
		init.parent_names = parent_names;
		init.num_parents = num_parents;
	}
	ret = devm_clk_hw_register(&client->dev, &drvdata->pll[1].hw);
	if (ret) {
		dev_err(&client->dev, "unable to register %s\n", init.name);
		return ret;
	}

	/* register clk multisync and clk out divider */
	num_clocks = (drvdata->variant == SI5351_VARIANT_A3) ? 3 : 8;
	parent_names[0] = si5351_pll_names[0];
	if (drvdata->variant == SI5351_VARIANT_B)
		parent_names[1] = si5351_pll_names[2];
	else
		parent_names[1] = si5351_pll_names[1];

	drvdata->msynth = devm_kcalloc(&client->dev, num_clocks,
				       sizeof(*drvdata->msynth), GFP_KERNEL);
	drvdata->clkout = devm_kcalloc(&client->dev, num_clocks,
				       sizeof(*drvdata->clkout), GFP_KERNEL);
	drvdata->num_clkout = num_clocks;

	if (WARN_ON(!drvdata->msynth || !drvdata->clkout)) {
		ret = -ENOMEM;
		return ret;
	}

	for (n = 0; n < num_clocks; n++) {
		drvdata->msynth[n].num = n;
		drvdata->msynth[n].drvdata = drvdata;
		drvdata->msynth[n].hw.init = &init;
		memset(&init, 0, sizeof(init));
		init.name = si5351_msynth_names[n];
		init.ops = &si5351_msynth_ops;
		init.flags = 0;
		if (pdata->clkout[n].pll_master)
			init.flags |= CLK_SET_RATE_PARENT;
		init.parent_names = parent_names;
		init.num_parents = 2;
		ret = devm_clk_hw_register(&client->dev,
					   &drvdata->msynth[n].hw);
		if (ret) {
			dev_err(&client->dev, "unable to register %s\n",
				init.name);
			return ret;
		}
	}

	num_parents = (drvdata->variant == SI5351_VARIANT_C) ? 4 : 3;
	parent_names[2] = si5351_input_names[0];
	parent_names[3] = si5351_input_names[1];
	for (n = 0; n < num_clocks; n++) {
		parent_names[0] = si5351_msynth_names[n];
		parent_names[1] = (n < 4) ? si5351_msynth_names[0] :
			si5351_msynth_names[4];

		drvdata->clkout[n].num = n;
		drvdata->clkout[n].drvdata = drvdata;
		drvdata->clkout[n].hw.init = &init;
		memset(&init, 0, sizeof(init));
		init.name = si5351_clkout_names[n];
		init.ops = &si5351_clkout_ops;
		init.flags = 0;
		if (pdata->clkout[n].clkout_src == SI5351_CLKOUT_SRC_MSYNTH_N)
			init.flags |= CLK_SET_RATE_PARENT;
		init.parent_names = parent_names;
		init.num_parents = num_parents;
		ret = devm_clk_hw_register(&client->dev,
					   &drvdata->clkout[n].hw);
		if (ret) {
			dev_err(&client->dev, "unable to register %s\n",
				init.name);
			return ret;
		}

		/* set initial clkout rate */
		if (pdata->clkout[n].rate != 0) {
			int ret;
			ret = clk_set_rate(drvdata->clkout[n].hw.clk,
					   pdata->clkout[n].rate);
			if (ret != 0) {
				dev_err(&client->dev, "Cannot set rate : %d\n",
					ret);
			}
		}
	}

	ret = devm_of_clk_add_hw_provider(&client->dev, si53351_of_clk_get,
					  drvdata);
	if (ret) {
		dev_err(&client->dev, "unable to add clk provider\n");
		return ret;
	}

	return 0;
}

static struct i2c_driver si5351_driver = {
	.driver = {
		.name = "si5351",
		.of_match_table = of_match_ptr(si5351_dt_ids),
	},
	.probe = si5351_i2c_probe,
	.id_table = si5351_i2c_ids,
};
module_i2c_driver(si5351_driver);

MODULE_AUTHOR("Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com");
MODULE_DESCRIPTION("Silicon Labs Si5351A/B/C clock generator driver");
MODULE_LICENSE("GPL");
