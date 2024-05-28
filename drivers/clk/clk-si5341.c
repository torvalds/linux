// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Silicon Labs Si5340, Si5341, Si5342, Si5344 and Si5345
 * Copyright (C) 2019 Topic Embedded Products
 * Author: Mike Looijmans <mike.looijmans@topic.nl>
 *
 * The Si5341 has 10 outputs and 5 synthesizers.
 * The Si5340 is a smaller version of the Si5341 with only 4 outputs.
 * The Si5345 is similar to the Si5341, with the addition of fractional input
 * dividers and automatic input selection.
 * The Si5342 and Si5344 are smaller versions of the Si5345.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/gcd.h>
#include <linux/math64.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#define SI5341_NUM_INPUTS 4

#define SI5340_MAX_NUM_OUTPUTS 4
#define SI5341_MAX_NUM_OUTPUTS 10
#define SI5342_MAX_NUM_OUTPUTS 2
#define SI5344_MAX_NUM_OUTPUTS 4
#define SI5345_MAX_NUM_OUTPUTS 10

#define SI5340_NUM_SYNTH 4
#define SI5341_NUM_SYNTH 5
#define SI5342_NUM_SYNTH 2
#define SI5344_NUM_SYNTH 4
#define SI5345_NUM_SYNTH 5

/* Range of the synthesizer fractional divider */
#define SI5341_SYNTH_N_MIN	10
#define SI5341_SYNTH_N_MAX	4095

/* The chip can get its input clock from 3 input pins or an XTAL */

/* There is one PLL running at 13500–14256 MHz */
#define SI5341_PLL_VCO_MIN 13500000000ull
#define SI5341_PLL_VCO_MAX 14256000000ull

/* The 5 frequency synthesizers obtain their input from the PLL */
struct clk_si5341_synth {
	struct clk_hw hw;
	struct clk_si5341 *data;
	u8 index;
};
#define to_clk_si5341_synth(_hw) \
	container_of(_hw, struct clk_si5341_synth, hw)

/* The output stages can be connected to any synth (full mux) */
struct clk_si5341_output {
	struct clk_hw hw;
	struct clk_si5341 *data;
	struct regulator *vddo_reg;
	u8 index;
};
#define to_clk_si5341_output(_hw) \
	container_of(_hw, struct clk_si5341_output, hw)

struct clk_si5341 {
	struct clk_hw hw;
	struct regmap *regmap;
	struct i2c_client *i2c_client;
	struct clk_si5341_synth synth[SI5341_NUM_SYNTH];
	struct clk_si5341_output clk[SI5341_MAX_NUM_OUTPUTS];
	struct clk *input_clk[SI5341_NUM_INPUTS];
	const char *input_clk_name[SI5341_NUM_INPUTS];
	const u16 *reg_output_offset;
	const u16 *reg_rdiv_offset;
	u64 freq_vco; /* 13500–14256 MHz */
	u8 num_outputs;
	u8 num_synth;
	u16 chip_id;
	bool xaxb_ext_clk;
	bool iovdd_33;
};
#define to_clk_si5341(_hw)	container_of(_hw, struct clk_si5341, hw)

struct clk_si5341_output_config {
	u8 out_format_drv_bits;
	u8 out_cm_ampl_bits;
	u8 vdd_sel_bits;
	bool synth_master;
	bool always_on;
};

#define SI5341_PAGE		0x0001
#define SI5341_PN_BASE		0x0002
#define SI5341_DEVICE_REV	0x0005
#define SI5341_STATUS		0x000C
#define SI5341_LOS		0x000D
#define SI5341_STATUS_STICKY	0x0011
#define SI5341_LOS_STICKY	0x0012
#define SI5341_SOFT_RST		0x001C
#define SI5341_IN_SEL		0x0021
#define SI5341_DEVICE_READY	0x00FE
#define SI5341_XAXB_CFG		0x090E
#define SI5341_IO_VDD_SEL	0x0943
#define SI5341_IN_EN		0x0949
#define SI5341_INX_TO_PFD_EN	0x094A

/* Status bits */
#define SI5341_STATUS_SYSINCAL	BIT(0)
#define SI5341_STATUS_LOSXAXB	BIT(1)
#define SI5341_STATUS_LOSREF	BIT(2)
#define SI5341_STATUS_LOL	BIT(3)

/* Input selection */
#define SI5341_IN_SEL_MASK	0x06
#define SI5341_IN_SEL_SHIFT	1
#define SI5341_IN_SEL_REGCTRL	0x01
#define SI5341_INX_TO_PFD_SHIFT	4

/* XTAL config bits */
#define SI5341_XAXB_CFG_EXTCLK_EN	BIT(0)
#define SI5341_XAXB_CFG_PDNB		BIT(1)

/* Input dividers (48-bit) */
#define SI5341_IN_PDIV(x)	(0x0208 + ((x) * 10))
#define SI5341_IN_PSET(x)	(0x020E + ((x) * 10))
#define SI5341_PX_UPD		0x0230

/* PLL configuration */
#define SI5341_PLL_M_NUM	0x0235
#define SI5341_PLL_M_DEN	0x023B

/* Output configuration */
#define SI5341_OUT_CONFIG(output)	\
			((output)->data->reg_output_offset[(output)->index])
#define SI5341_OUT_FORMAT(output)	(SI5341_OUT_CONFIG(output) + 1)
#define SI5341_OUT_CM(output)		(SI5341_OUT_CONFIG(output) + 2)
#define SI5341_OUT_MUX_SEL(output)	(SI5341_OUT_CONFIG(output) + 3)
#define SI5341_OUT_R_REG(output)	\
			((output)->data->reg_rdiv_offset[(output)->index])

#define SI5341_OUT_MUX_VDD_SEL_MASK 0x38

/* Synthesize N divider */
#define SI5341_SYNTH_N_NUM(x)	(0x0302 + ((x) * 11))
#define SI5341_SYNTH_N_DEN(x)	(0x0308 + ((x) * 11))
#define SI5341_SYNTH_N_UPD(x)	(0x030C + ((x) * 11))

/* Synthesizer output enable, phase bypass, power mode */
#define SI5341_SYNTH_N_CLK_TO_OUTX_EN	0x0A03
#define SI5341_SYNTH_N_PIBYP		0x0A04
#define SI5341_SYNTH_N_PDNB		0x0A05
#define SI5341_SYNTH_N_CLK_DIS		0x0B4A

#define SI5341_REGISTER_MAX	0xBFF

/* SI5341_OUT_CONFIG bits */
#define SI5341_OUT_CFG_PDN		BIT(0)
#define SI5341_OUT_CFG_OE		BIT(1)
#define SI5341_OUT_CFG_RDIV_FORCE2	BIT(2)

/* Static configuration (to be moved to firmware) */
struct si5341_reg_default {
	u16 address;
	u8 value;
};

static const char * const si5341_input_clock_names[] = {
	"in0", "in1", "in2", "xtal"
};

/* Output configuration registers 0..9 are not quite logically organized */
/* Also for si5345 */
static const u16 si5341_reg_output_offset[] = {
	0x0108,
	0x010D,
	0x0112,
	0x0117,
	0x011C,
	0x0121,
	0x0126,
	0x012B,
	0x0130,
	0x013A,
};

/* for si5340, si5342 and si5344 */
static const u16 si5340_reg_output_offset[] = {
	0x0112,
	0x0117,
	0x0126,
	0x012B,
};

/* The location of the R divider registers */
static const u16 si5341_reg_rdiv_offset[] = {
	0x024A,
	0x024D,
	0x0250,
	0x0253,
	0x0256,
	0x0259,
	0x025C,
	0x025F,
	0x0262,
	0x0268,
};
static const u16 si5340_reg_rdiv_offset[] = {
	0x0250,
	0x0253,
	0x025C,
	0x025F,
};

/*
 * Programming sequence from ClockBuilder, settings to initialize the system
 * using only the XTAL input, without pre-divider.
 * This also contains settings that aren't mentioned anywhere in the datasheet.
 * The "known" settings like synth and output configuration are done later.
 */
static const struct si5341_reg_default si5341_reg_defaults[] = {
	{ 0x0017, 0x3A }, /* INT mask (disable interrupts) */
	{ 0x0018, 0xFF }, /* INT mask */
	{ 0x0021, 0x0F }, /* Select XTAL as input */
	{ 0x0022, 0x00 }, /* Not in datasheet */
	{ 0x002B, 0x02 }, /* SPI config */
	{ 0x002C, 0x20 }, /* LOS enable for XTAL */
	{ 0x002D, 0x00 }, /* LOS timing */
	{ 0x002E, 0x00 },
	{ 0x002F, 0x00 },
	{ 0x0030, 0x00 },
	{ 0x0031, 0x00 },
	{ 0x0032, 0x00 },
	{ 0x0033, 0x00 },
	{ 0x0034, 0x00 },
	{ 0x0035, 0x00 },
	{ 0x0036, 0x00 },
	{ 0x0037, 0x00 },
	{ 0x0038, 0x00 }, /* LOS setting (thresholds) */
	{ 0x0039, 0x00 },
	{ 0x003A, 0x00 },
	{ 0x003B, 0x00 },
	{ 0x003C, 0x00 },
	{ 0x003D, 0x00 }, /* LOS setting (thresholds) end */
	{ 0x0041, 0x00 }, /* LOS0_DIV_SEL */
	{ 0x0042, 0x00 }, /* LOS1_DIV_SEL */
	{ 0x0043, 0x00 }, /* LOS2_DIV_SEL */
	{ 0x0044, 0x00 }, /* LOS3_DIV_SEL */
	{ 0x009E, 0x00 }, /* Not in datasheet */
	{ 0x0102, 0x01 }, /* Enable outputs */
	{ 0x013F, 0x00 }, /* Not in datasheet */
	{ 0x0140, 0x00 }, /* Not in datasheet */
	{ 0x0141, 0x40 }, /* OUT LOS */
	{ 0x0202, 0x00 }, /* XAXB_FREQ_OFFSET (=0)*/
	{ 0x0203, 0x00 },
	{ 0x0204, 0x00 },
	{ 0x0205, 0x00 },
	{ 0x0206, 0x00 }, /* PXAXB (2^x) */
	{ 0x0208, 0x00 }, /* Px divider setting (usually 0) */
	{ 0x0209, 0x00 },
	{ 0x020A, 0x00 },
	{ 0x020B, 0x00 },
	{ 0x020C, 0x00 },
	{ 0x020D, 0x00 },
	{ 0x020E, 0x00 },
	{ 0x020F, 0x00 },
	{ 0x0210, 0x00 },
	{ 0x0211, 0x00 },
	{ 0x0212, 0x00 },
	{ 0x0213, 0x00 },
	{ 0x0214, 0x00 },
	{ 0x0215, 0x00 },
	{ 0x0216, 0x00 },
	{ 0x0217, 0x00 },
	{ 0x0218, 0x00 },
	{ 0x0219, 0x00 },
	{ 0x021A, 0x00 },
	{ 0x021B, 0x00 },
	{ 0x021C, 0x00 },
	{ 0x021D, 0x00 },
	{ 0x021E, 0x00 },
	{ 0x021F, 0x00 },
	{ 0x0220, 0x00 },
	{ 0x0221, 0x00 },
	{ 0x0222, 0x00 },
	{ 0x0223, 0x00 },
	{ 0x0224, 0x00 },
	{ 0x0225, 0x00 },
	{ 0x0226, 0x00 },
	{ 0x0227, 0x00 },
	{ 0x0228, 0x00 },
	{ 0x0229, 0x00 },
	{ 0x022A, 0x00 },
	{ 0x022B, 0x00 },
	{ 0x022C, 0x00 },
	{ 0x022D, 0x00 },
	{ 0x022E, 0x00 },
	{ 0x022F, 0x00 }, /* Px divider setting (usually 0) end */
	{ 0x026B, 0x00 }, /* DESIGN_ID (ASCII string) */
	{ 0x026C, 0x00 },
	{ 0x026D, 0x00 },
	{ 0x026E, 0x00 },
	{ 0x026F, 0x00 },
	{ 0x0270, 0x00 },
	{ 0x0271, 0x00 },
	{ 0x0272, 0x00 }, /* DESIGN_ID (ASCII string) end */
	{ 0x0339, 0x1F }, /* N_FSTEP_MSK */
	{ 0x033B, 0x00 }, /* Nx_FSTEPW (Frequency step) */
	{ 0x033C, 0x00 },
	{ 0x033D, 0x00 },
	{ 0x033E, 0x00 },
	{ 0x033F, 0x00 },
	{ 0x0340, 0x00 },
	{ 0x0341, 0x00 },
	{ 0x0342, 0x00 },
	{ 0x0343, 0x00 },
	{ 0x0344, 0x00 },
	{ 0x0345, 0x00 },
	{ 0x0346, 0x00 },
	{ 0x0347, 0x00 },
	{ 0x0348, 0x00 },
	{ 0x0349, 0x00 },
	{ 0x034A, 0x00 },
	{ 0x034B, 0x00 },
	{ 0x034C, 0x00 },
	{ 0x034D, 0x00 },
	{ 0x034E, 0x00 },
	{ 0x034F, 0x00 },
	{ 0x0350, 0x00 },
	{ 0x0351, 0x00 },
	{ 0x0352, 0x00 },
	{ 0x0353, 0x00 },
	{ 0x0354, 0x00 },
	{ 0x0355, 0x00 },
	{ 0x0356, 0x00 },
	{ 0x0357, 0x00 },
	{ 0x0358, 0x00 }, /* Nx_FSTEPW (Frequency step) end */
	{ 0x0359, 0x00 }, /* Nx_DELAY */
	{ 0x035A, 0x00 },
	{ 0x035B, 0x00 },
	{ 0x035C, 0x00 },
	{ 0x035D, 0x00 },
	{ 0x035E, 0x00 },
	{ 0x035F, 0x00 },
	{ 0x0360, 0x00 },
	{ 0x0361, 0x00 },
	{ 0x0362, 0x00 }, /* Nx_DELAY end */
	{ 0x0802, 0x00 }, /* Not in datasheet */
	{ 0x0803, 0x00 }, /* Not in datasheet */
	{ 0x0804, 0x00 }, /* Not in datasheet */
	{ 0x090E, 0x02 }, /* XAXB_EXTCLK_EN=0 XAXB_PDNB=1 (use XTAL) */
	{ 0x091C, 0x04 }, /* ZDM_EN=4 (Normal mode) */
	{ 0x0949, 0x00 }, /* IN_EN (disable input clocks) */
	{ 0x094A, 0x00 }, /* INx_TO_PFD_EN (disabled) */
	{ 0x0A02, 0x00 }, /* Not in datasheet */
	{ 0x0B44, 0x0F }, /* PDIV_ENB (datasheet does not mention what it is) */
	{ 0x0B57, 0x10 }, /* VCO_RESET_CALCODE (not described in datasheet) */
	{ 0x0B58, 0x05 }, /* VCO_RESET_CALCODE (not described in datasheet) */
};

/* Read and interpret a 44-bit followed by a 32-bit value in the regmap */
static int si5341_decode_44_32(struct regmap *regmap, unsigned int reg,
	u64 *val1, u32 *val2)
{
	int err;
	u8 r[10];

	err = regmap_bulk_read(regmap, reg, r, 10);
	if (err < 0)
		return err;

	*val1 = ((u64)((r[5] & 0x0f) << 8 | r[4]) << 32) |
		 (get_unaligned_le32(r));
	*val2 = get_unaligned_le32(&r[6]);

	return 0;
}

static int si5341_encode_44_32(struct regmap *regmap, unsigned int reg,
	u64 n_num, u32 n_den)
{
	u8 r[10];

	/* Shift left as far as possible without overflowing */
	while (!(n_num & BIT_ULL(43)) && !(n_den & BIT(31))) {
		n_num <<= 1;
		n_den <<= 1;
	}

	/* 44 bits (6 bytes) numerator */
	put_unaligned_le32(n_num, r);
	r[4] = (n_num >> 32) & 0xff;
	r[5] = (n_num >> 40) & 0x0f;
	/* 32 bits denominator */
	put_unaligned_le32(n_den, &r[6]);

	/* Program the fraction */
	return regmap_bulk_write(regmap, reg, r, sizeof(r));
}

/* VCO, we assume it runs at a constant frequency */
static unsigned long si5341_clk_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_si5341 *data = to_clk_si5341(hw);
	int err;
	u64 res;
	u64 m_num;
	u32 m_den;
	unsigned int shift;

	/* Assume that PDIV is not being used, just read the PLL setting */
	err = si5341_decode_44_32(data->regmap, SI5341_PLL_M_NUM,
				&m_num, &m_den);
	if (err < 0)
		return 0;

	if (!m_num || !m_den)
		return 0;

	/*
	 * Though m_num is 64-bit, only the upper bits are actually used. While
	 * calculating m_num and m_den, they are shifted as far as possible to
	 * the left. To avoid 96-bit division here, we just shift them back so
	 * we can do with just 64 bits.
	 */
	shift = 0;
	res = m_num;
	while (res & 0xffff00000000ULL) {
		++shift;
		res >>= 1;
	}
	res *= parent_rate;
	do_div(res, (m_den >> shift));

	/* We cannot return the actual frequency in 32 bit, store it locally */
	data->freq_vco = res;

	/* Report kHz since the value is out of range */
	do_div(res, 1000);

	return (unsigned long)res;
}

static int si5341_clk_get_selected_input(struct clk_si5341 *data)
{
	int err;
	u32 val;

	err = regmap_read(data->regmap, SI5341_IN_SEL, &val);
	if (err < 0)
		return err;

	return (val & SI5341_IN_SEL_MASK) >> SI5341_IN_SEL_SHIFT;
}

static u8 si5341_clk_get_parent(struct clk_hw *hw)
{
	struct clk_si5341 *data = to_clk_si5341(hw);
	int res = si5341_clk_get_selected_input(data);

	if (res < 0)
		return 0; /* Apparently we cannot report errors */

	return res;
}

static int si5341_clk_reparent(struct clk_si5341 *data, u8 index)
{
	int err;
	u8 val;

	val = (index << SI5341_IN_SEL_SHIFT) & SI5341_IN_SEL_MASK;
	/* Enable register-based input selection */
	val |= SI5341_IN_SEL_REGCTRL;

	err = regmap_update_bits(data->regmap,
		SI5341_IN_SEL, SI5341_IN_SEL_REGCTRL | SI5341_IN_SEL_MASK, val);
	if (err < 0)
		return err;

	if (index < 3) {
		/* Enable input buffer for selected input */
		err = regmap_update_bits(data->regmap,
				SI5341_IN_EN, 0x07, BIT(index));
		if (err < 0)
			return err;

		/* Enables the input to phase detector */
		err = regmap_update_bits(data->regmap, SI5341_INX_TO_PFD_EN,
				0x7 << SI5341_INX_TO_PFD_SHIFT,
				BIT(index + SI5341_INX_TO_PFD_SHIFT));
		if (err < 0)
			return err;

		/* Power down XTAL oscillator and buffer */
		err = regmap_update_bits(data->regmap, SI5341_XAXB_CFG,
				SI5341_XAXB_CFG_PDNB, 0);
		if (err < 0)
			return err;

		/*
		 * Set the P divider to "1". There's no explanation in the
		 * datasheet of these registers, but the clockbuilder software
		 * programs a "1" when the input is being used.
		 */
		err = regmap_write(data->regmap, SI5341_IN_PDIV(index), 1);
		if (err < 0)
			return err;

		err = regmap_write(data->regmap, SI5341_IN_PSET(index), 1);
		if (err < 0)
			return err;

		/* Set update PDIV bit */
		err = regmap_write(data->regmap, SI5341_PX_UPD, BIT(index));
		if (err < 0)
			return err;
	} else {
		/* Disable all input buffers */
		err = regmap_update_bits(data->regmap, SI5341_IN_EN, 0x07, 0);
		if (err < 0)
			return err;

		/* Disable input to phase detector */
		err = regmap_update_bits(data->regmap, SI5341_INX_TO_PFD_EN,
				0x7 << SI5341_INX_TO_PFD_SHIFT, 0);
		if (err < 0)
			return err;

		/* Power up XTAL oscillator and buffer, select clock mode */
		err = regmap_update_bits(data->regmap, SI5341_XAXB_CFG,
				SI5341_XAXB_CFG_PDNB | SI5341_XAXB_CFG_EXTCLK_EN,
				SI5341_XAXB_CFG_PDNB | (data->xaxb_ext_clk ?
					SI5341_XAXB_CFG_EXTCLK_EN : 0));
		if (err < 0)
			return err;
	}

	return 0;
}

static int si5341_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_si5341 *data = to_clk_si5341(hw);

	return si5341_clk_reparent(data, index);
}

static const struct clk_ops si5341_clk_ops = {
	.determine_rate = clk_hw_determine_rate_no_reparent,
	.set_parent = si5341_clk_set_parent,
	.get_parent = si5341_clk_get_parent,
	.recalc_rate = si5341_clk_recalc_rate,
};

/* Synthesizers, there are 5 synthesizers that connect to any of the outputs */

/* The synthesizer is on if all power and enable bits are set */
static int si5341_synth_clk_is_on(struct clk_hw *hw)
{
	struct clk_si5341_synth *synth = to_clk_si5341_synth(hw);
	int err;
	u32 val;
	u8 index = synth->index;

	err = regmap_read(synth->data->regmap,
			SI5341_SYNTH_N_CLK_TO_OUTX_EN, &val);
	if (err < 0)
		return 0;

	if (!(val & BIT(index)))
		return 0;

	err = regmap_read(synth->data->regmap, SI5341_SYNTH_N_PDNB, &val);
	if (err < 0)
		return 0;

	if (!(val & BIT(index)))
		return 0;

	/* This bit must be 0 for the synthesizer to receive clock input */
	err = regmap_read(synth->data->regmap, SI5341_SYNTH_N_CLK_DIS, &val);
	if (err < 0)
		return 0;

	return !(val & BIT(index));
}

static void si5341_synth_clk_unprepare(struct clk_hw *hw)
{
	struct clk_si5341_synth *synth = to_clk_si5341_synth(hw);
	u8 index = synth->index; /* In range 0..5 */
	u8 mask = BIT(index);

	/* Disable output */
	regmap_update_bits(synth->data->regmap,
		SI5341_SYNTH_N_CLK_TO_OUTX_EN, mask, 0);
	/* Power down */
	regmap_update_bits(synth->data->regmap,
		SI5341_SYNTH_N_PDNB, mask, 0);
	/* Disable clock input to synth (set to 1 to disable) */
	regmap_update_bits(synth->data->regmap,
		SI5341_SYNTH_N_CLK_DIS, mask, mask);
}

static int si5341_synth_clk_prepare(struct clk_hw *hw)
{
	struct clk_si5341_synth *synth = to_clk_si5341_synth(hw);
	int err;
	u8 index = synth->index;
	u8 mask = BIT(index);

	/* Power up */
	err = regmap_update_bits(synth->data->regmap,
		SI5341_SYNTH_N_PDNB, mask, mask);
	if (err < 0)
		return err;

	/* Enable clock input to synth (set bit to 0 to enable) */
	err = regmap_update_bits(synth->data->regmap,
		SI5341_SYNTH_N_CLK_DIS, mask, 0);
	if (err < 0)
		return err;

	/* Enable output */
	return regmap_update_bits(synth->data->regmap,
		SI5341_SYNTH_N_CLK_TO_OUTX_EN, mask, mask);
}

/* Synth clock frequency: Fvco * n_den / n_den, with Fvco in 13500-14256 MHz */
static unsigned long si5341_synth_clk_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_si5341_synth *synth = to_clk_si5341_synth(hw);
	u64 f;
	u64 n_num;
	u32 n_den;
	int err;

	err = si5341_decode_44_32(synth->data->regmap,
			SI5341_SYNTH_N_NUM(synth->index), &n_num, &n_den);
	if (err < 0)
		return err;
	/* Check for bogus/uninitialized settings */
	if (!n_num || !n_den)
		return 0;

	/*
	 * n_num and n_den are shifted left as much as possible, so to prevent
	 * overflow in 64-bit math, we shift n_den 4 bits to the right
	 */
	f = synth->data->freq_vco;
	f *= n_den >> 4;

	/* Now we need to do 64-bit division: f/n_num */
	/* And compensate for the 4 bits we dropped */
	f = div64_u64(f, (n_num >> 4));

	return f;
}

static long si5341_synth_clk_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *parent_rate)
{
	struct clk_si5341_synth *synth = to_clk_si5341_synth(hw);
	u64 f;

	/* The synthesizer accuracy is such that anything in range will work */
	f = synth->data->freq_vco;
	do_div(f, SI5341_SYNTH_N_MAX);
	if (rate < f)
		return f;

	f = synth->data->freq_vco;
	do_div(f, SI5341_SYNTH_N_MIN);
	if (rate > f)
		return f;

	return rate;
}

static int si5341_synth_program(struct clk_si5341_synth *synth,
	u64 n_num, u32 n_den, bool is_integer)
{
	int err;
	u8 index = synth->index;

	err = si5341_encode_44_32(synth->data->regmap,
			SI5341_SYNTH_N_NUM(index), n_num, n_den);

	err = regmap_update_bits(synth->data->regmap,
		SI5341_SYNTH_N_PIBYP, BIT(index), is_integer ? BIT(index) : 0);
	if (err < 0)
		return err;

	return regmap_write(synth->data->regmap,
		SI5341_SYNTH_N_UPD(index), 0x01);
}


static int si5341_synth_clk_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_si5341_synth *synth = to_clk_si5341_synth(hw);
	u64 n_num;
	u32 n_den;
	u32 r;
	u32 g;
	bool is_integer;

	n_num = synth->data->freq_vco;

	/* see if there's an integer solution */
	r = do_div(n_num, rate);
	is_integer = (r == 0);
	if (is_integer) {
		/* Integer divider equal to n_num */
		n_den = 1;
	} else {
		/* Calculate a fractional solution */
		g = gcd(r, rate);
		n_den = rate / g;
		n_num *= n_den;
		n_num += r / g;
	}

	dev_dbg(&synth->data->i2c_client->dev,
			"%s(%u): n=0x%llx d=0x%x %s\n", __func__,
				synth->index, n_num, n_den,
				is_integer ? "int" : "frac");

	return si5341_synth_program(synth, n_num, n_den, is_integer);
}

static const struct clk_ops si5341_synth_clk_ops = {
	.is_prepared = si5341_synth_clk_is_on,
	.prepare = si5341_synth_clk_prepare,
	.unprepare = si5341_synth_clk_unprepare,
	.recalc_rate = si5341_synth_clk_recalc_rate,
	.round_rate = si5341_synth_clk_round_rate,
	.set_rate = si5341_synth_clk_set_rate,
};

static int si5341_output_clk_is_on(struct clk_hw *hw)
{
	struct clk_si5341_output *output = to_clk_si5341_output(hw);
	int err;
	u32 val;

	err = regmap_read(output->data->regmap,
			SI5341_OUT_CONFIG(output), &val);
	if (err < 0)
		return err;

	/* Bit 0=PDN, 1=OE so only a value of 0x2 enables the output */
	return (val & 0x03) == SI5341_OUT_CFG_OE;
}

/* Disables and then powers down the output */
static void si5341_output_clk_unprepare(struct clk_hw *hw)
{
	struct clk_si5341_output *output = to_clk_si5341_output(hw);

	regmap_update_bits(output->data->regmap,
			SI5341_OUT_CONFIG(output),
			SI5341_OUT_CFG_OE, 0);
	regmap_update_bits(output->data->regmap,
			SI5341_OUT_CONFIG(output),
			SI5341_OUT_CFG_PDN, SI5341_OUT_CFG_PDN);
}

/* Powers up and then enables the output */
static int si5341_output_clk_prepare(struct clk_hw *hw)
{
	struct clk_si5341_output *output = to_clk_si5341_output(hw);
	int err;

	err = regmap_update_bits(output->data->regmap,
			SI5341_OUT_CONFIG(output),
			SI5341_OUT_CFG_PDN, 0);
	if (err < 0)
		return err;

	return regmap_update_bits(output->data->regmap,
			SI5341_OUT_CONFIG(output),
			SI5341_OUT_CFG_OE, SI5341_OUT_CFG_OE);
}

static unsigned long si5341_output_clk_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_si5341_output *output = to_clk_si5341_output(hw);
	int err;
	u32 val;
	u32 r_divider;
	u8 r[3];

	err = regmap_read(output->data->regmap,
			SI5341_OUT_CONFIG(output), &val);
	if (err < 0)
		return err;

	/* If SI5341_OUT_CFG_RDIV_FORCE2 is set, r_divider is 2 */
	if (val & SI5341_OUT_CFG_RDIV_FORCE2)
		return parent_rate / 2;

	err = regmap_bulk_read(output->data->regmap,
			SI5341_OUT_R_REG(output), r, 3);
	if (err < 0)
		return err;

	/* Calculate value as 24-bit integer*/
	r_divider = r[2] << 16 | r[1] << 8 | r[0];

	/* If Rx_REG is zero, the divider is disabled, so return a "0" rate */
	if (!r_divider)
		return 0;

	/* Divider is 2*(Rx_REG+1) */
	r_divider += 1;
	r_divider <<= 1;


	return parent_rate / r_divider;
}

static int si5341_output_clk_determine_rate(struct clk_hw *hw,
					    struct clk_rate_request *req)
{
	unsigned long rate = req->rate;
	unsigned long r;

	if (!rate)
		return 0;

	r = req->best_parent_rate >> 1;

	/* If rate is an even divisor, no changes to parent required */
	if (r && !(r % rate))
		return 0;

	if (clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT) {
		if (rate > 200000000) {
			/* minimum r-divider is 2 */
			r = 2;
		} else {
			/* Take a parent frequency near 400 MHz */
			r = (400000000u / rate) & ~1;
		}
		req->best_parent_rate = r * rate;
	} else {
		/* We cannot change our parent's rate, report what we can do */
		r /= rate;
		rate = req->best_parent_rate / (r << 1);
	}

	req->rate = rate;
	return 0;
}

static int si5341_output_clk_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_si5341_output *output = to_clk_si5341_output(hw);
	u32 r_div;
	int err;
	u8 r[3];

	if (!rate)
		return -EINVAL;

	/* Frequency divider is (r_div + 1) * 2 */
	r_div = (parent_rate / rate) >> 1;

	if (r_div <= 1)
		r_div = 0;
	else if (r_div >= BIT(24))
		r_div = BIT(24) - 1;
	else
		--r_div;

	/* For a value of "2", we set the "OUT0_RDIV_FORCE2" bit */
	err = regmap_update_bits(output->data->regmap,
			SI5341_OUT_CONFIG(output),
			SI5341_OUT_CFG_RDIV_FORCE2,
			(r_div == 0) ? SI5341_OUT_CFG_RDIV_FORCE2 : 0);
	if (err < 0)
		return err;

	/* Always write Rx_REG, because a zero value disables the divider */
	r[0] = r_div ? (r_div & 0xff) : 1;
	r[1] = (r_div >> 8) & 0xff;
	r[2] = (r_div >> 16) & 0xff;
	return regmap_bulk_write(output->data->regmap,
			SI5341_OUT_R_REG(output), r, 3);
}

static int si5341_output_reparent(struct clk_si5341_output *output, u8 index)
{
	return regmap_update_bits(output->data->regmap,
		SI5341_OUT_MUX_SEL(output), 0x07, index);
}

static int si5341_output_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_si5341_output *output = to_clk_si5341_output(hw);

	if (index >= output->data->num_synth)
		return -EINVAL;

	return si5341_output_reparent(output, index);
}

static u8 si5341_output_get_parent(struct clk_hw *hw)
{
	struct clk_si5341_output *output = to_clk_si5341_output(hw);
	u32 val;

	regmap_read(output->data->regmap, SI5341_OUT_MUX_SEL(output), &val);

	return val & 0x7;
}

static const struct clk_ops si5341_output_clk_ops = {
	.is_prepared = si5341_output_clk_is_on,
	.prepare = si5341_output_clk_prepare,
	.unprepare = si5341_output_clk_unprepare,
	.recalc_rate = si5341_output_clk_recalc_rate,
	.determine_rate = si5341_output_clk_determine_rate,
	.set_rate = si5341_output_clk_set_rate,
	.set_parent = si5341_output_set_parent,
	.get_parent = si5341_output_get_parent,
};

/*
 * The chip can be bought in a pre-programmed version, or one can program the
 * NVM in the chip to boot up in a preset mode. This routine tries to determine
 * if that's the case, or if we need to reset and program everything from
 * scratch. Returns negative error, or true/false.
 */
static int si5341_is_programmed_already(struct clk_si5341 *data)
{
	int err;
	u8 r[4];

	/* Read the PLL divider value, it must have a non-zero value */
	err = regmap_bulk_read(data->regmap, SI5341_PLL_M_DEN,
			r, ARRAY_SIZE(r));
	if (err < 0)
		return err;

	return !!get_unaligned_le32(r);
}

static struct clk_hw *
of_clk_si5341_get(struct of_phandle_args *clkspec, void *_data)
{
	struct clk_si5341 *data = _data;
	unsigned int idx = clkspec->args[1];
	unsigned int group = clkspec->args[0];

	switch (group) {
	case 0:
		if (idx >= data->num_outputs) {
			dev_err(&data->i2c_client->dev,
				"invalid output index %u\n", idx);
			return ERR_PTR(-EINVAL);
		}
		return &data->clk[idx].hw;
	case 1:
		if (idx >= data->num_synth) {
			dev_err(&data->i2c_client->dev,
				"invalid synthesizer index %u\n", idx);
			return ERR_PTR(-EINVAL);
		}
		return &data->synth[idx].hw;
	case 2:
		if (idx > 0) {
			dev_err(&data->i2c_client->dev,
				"invalid PLL index %u\n", idx);
			return ERR_PTR(-EINVAL);
		}
		return &data->hw;
	default:
		dev_err(&data->i2c_client->dev, "invalid group %u\n", group);
		return ERR_PTR(-EINVAL);
	}
}

static int si5341_probe_chip_id(struct clk_si5341 *data)
{
	int err;
	u8 reg[4];
	u16 model;

	err = regmap_bulk_read(data->regmap, SI5341_PN_BASE, reg,
				ARRAY_SIZE(reg));
	if (err < 0) {
		dev_err(&data->i2c_client->dev, "Failed to read chip ID\n");
		return err;
	}

	model = get_unaligned_le16(reg);

	dev_info(&data->i2c_client->dev, "Chip: %x Grade: %u Rev: %u\n",
		 model, reg[2], reg[3]);

	switch (model) {
	case 0x5340:
		data->num_outputs = SI5340_MAX_NUM_OUTPUTS;
		data->num_synth = SI5340_NUM_SYNTH;
		data->reg_output_offset = si5340_reg_output_offset;
		data->reg_rdiv_offset = si5340_reg_rdiv_offset;
		break;
	case 0x5341:
		data->num_outputs = SI5341_MAX_NUM_OUTPUTS;
		data->num_synth = SI5341_NUM_SYNTH;
		data->reg_output_offset = si5341_reg_output_offset;
		data->reg_rdiv_offset = si5341_reg_rdiv_offset;
		break;
	case 0x5342:
		data->num_outputs = SI5342_MAX_NUM_OUTPUTS;
		data->num_synth = SI5342_NUM_SYNTH;
		data->reg_output_offset = si5340_reg_output_offset;
		data->reg_rdiv_offset = si5340_reg_rdiv_offset;
		break;
	case 0x5344:
		data->num_outputs = SI5344_MAX_NUM_OUTPUTS;
		data->num_synth = SI5344_NUM_SYNTH;
		data->reg_output_offset = si5340_reg_output_offset;
		data->reg_rdiv_offset = si5340_reg_rdiv_offset;
		break;
	case 0x5345:
		data->num_outputs = SI5345_MAX_NUM_OUTPUTS;
		data->num_synth = SI5345_NUM_SYNTH;
		data->reg_output_offset = si5341_reg_output_offset;
		data->reg_rdiv_offset = si5341_reg_rdiv_offset;
		break;
	default:
		dev_err(&data->i2c_client->dev, "Model '%x' not supported\n",
			model);
		return -EINVAL;
	}

	data->chip_id = model;

	return 0;
}

/* Read active settings into the regmap cache for later reference */
static int si5341_read_settings(struct clk_si5341 *data)
{
	int err;
	u8 i;
	u8 r[10];

	err = regmap_bulk_read(data->regmap, SI5341_PLL_M_NUM, r, 10);
	if (err < 0)
		return err;

	err = regmap_bulk_read(data->regmap,
				SI5341_SYNTH_N_CLK_TO_OUTX_EN, r, 3);
	if (err < 0)
		return err;

	err = regmap_bulk_read(data->regmap,
				SI5341_SYNTH_N_CLK_DIS, r, 1);
	if (err < 0)
		return err;

	for (i = 0; i < data->num_synth; ++i) {
		err = regmap_bulk_read(data->regmap,
					SI5341_SYNTH_N_NUM(i), r, 10);
		if (err < 0)
			return err;
	}

	for (i = 0; i < data->num_outputs; ++i) {
		err = regmap_bulk_read(data->regmap,
					data->reg_output_offset[i], r, 4);
		if (err < 0)
			return err;

		err = regmap_bulk_read(data->regmap,
					data->reg_rdiv_offset[i], r, 3);
		if (err < 0)
			return err;
	}

	return 0;
}

static int si5341_write_multiple(struct clk_si5341 *data,
	const struct si5341_reg_default *values, unsigned int num_values)
{
	unsigned int i;
	int res;

	for (i = 0; i < num_values; ++i) {
		res = regmap_write(data->regmap,
			values[i].address, values[i].value);
		if (res < 0) {
			dev_err(&data->i2c_client->dev,
				"Failed to write %#x:%#x\n",
				values[i].address, values[i].value);
			return res;
		}
	}

	return 0;
}

static const struct si5341_reg_default si5341_preamble[] = {
	{ 0x0B25, 0x00 },
	{ 0x0502, 0x01 },
	{ 0x0505, 0x03 },
	{ 0x0957, 0x17 },
	{ 0x0B4E, 0x1A },
};

static const struct si5341_reg_default si5345_preamble[] = {
	{ 0x0B25, 0x00 },
	{ 0x0540, 0x01 },
};

static int si5341_send_preamble(struct clk_si5341 *data)
{
	int res;
	u32 revision;

	/* For revision 2 and up, the values are slightly different */
	res = regmap_read(data->regmap, SI5341_DEVICE_REV, &revision);
	if (res < 0)
		return res;

	/* Write "preamble" as specified by datasheet */
	res = regmap_write(data->regmap, 0xB24, revision < 2 ? 0xD8 : 0xC0);
	if (res < 0)
		return res;

	/* The si5342..si5345 require a different preamble */
	if (data->chip_id > 0x5341)
		res = si5341_write_multiple(data,
			si5345_preamble, ARRAY_SIZE(si5345_preamble));
	else
		res = si5341_write_multiple(data,
			si5341_preamble, ARRAY_SIZE(si5341_preamble));
	if (res < 0)
		return res;

	/* Datasheet specifies a 300ms wait after sending the preamble */
	msleep(300);

	return 0;
}

/* Perform a soft reset and write post-amble */
static int si5341_finalize_defaults(struct clk_si5341 *data)
{
	int res;
	u32 revision;

	res = regmap_write(data->regmap, SI5341_IO_VDD_SEL,
			   data->iovdd_33 ? 1 : 0);
	if (res < 0)
		return res;

	res = regmap_read(data->regmap, SI5341_DEVICE_REV, &revision);
	if (res < 0)
		return res;

	dev_dbg(&data->i2c_client->dev, "%s rev=%u\n", __func__, revision);

	res = regmap_write(data->regmap, SI5341_SOFT_RST, 0x01);
	if (res < 0)
		return res;

	/* The si5342..si5345 have an additional post-amble */
	if (data->chip_id > 0x5341) {
		res = regmap_write(data->regmap, 0x540, 0x0);
		if (res < 0)
			return res;
	}

	/* Datasheet does not explain these nameless registers */
	res = regmap_write(data->regmap, 0xB24, revision < 2 ? 0xDB : 0xC3);
	if (res < 0)
		return res;
	res = regmap_write(data->regmap, 0x0B25, 0x02);
	if (res < 0)
		return res;

	return 0;
}


static const struct regmap_range si5341_regmap_volatile_range[] = {
	regmap_reg_range(0x000C, 0x0012), /* Status */
	regmap_reg_range(0x001C, 0x001E), /* reset, finc/fdec */
	regmap_reg_range(0x00E2, 0x00FE), /* NVM, interrupts, device ready */
	/* Update bits for P divider and synth config */
	regmap_reg_range(SI5341_PX_UPD, SI5341_PX_UPD),
	regmap_reg_range(SI5341_SYNTH_N_UPD(0), SI5341_SYNTH_N_UPD(0)),
	regmap_reg_range(SI5341_SYNTH_N_UPD(1), SI5341_SYNTH_N_UPD(1)),
	regmap_reg_range(SI5341_SYNTH_N_UPD(2), SI5341_SYNTH_N_UPD(2)),
	regmap_reg_range(SI5341_SYNTH_N_UPD(3), SI5341_SYNTH_N_UPD(3)),
	regmap_reg_range(SI5341_SYNTH_N_UPD(4), SI5341_SYNTH_N_UPD(4)),
};

static const struct regmap_access_table si5341_regmap_volatile = {
	.yes_ranges = si5341_regmap_volatile_range,
	.n_yes_ranges = ARRAY_SIZE(si5341_regmap_volatile_range),
};

/* Pages 0, 1, 2, 3, 9, A, B are valid, so there are 12 pages */
static const struct regmap_range_cfg si5341_regmap_ranges[] = {
	{
		.range_min = 0,
		.range_max = SI5341_REGISTER_MAX,
		.selector_reg = SI5341_PAGE,
		.selector_mask = 0xff,
		.selector_shift = 0,
		.window_start = 0,
		.window_len = 256,
	},
};

static int si5341_wait_device_ready(struct i2c_client *client)
{
	int count;

	/* Datasheet warns: Any attempt to read or write any register other
	 * than DEVICE_READY before DEVICE_READY reads as 0x0F may corrupt the
	 * NVM programming and may corrupt the register contents, as they are
	 * read from NVM. Note that this includes accesses to the PAGE register.
	 * Also: DEVICE_READY is available on every register page, so no page
	 * change is needed to read it.
	 * Do this outside regmap to avoid automatic PAGE register access.
	 * May take up to 300ms to complete.
	 */
	for (count = 0; count < 15; ++count) {
		s32 result = i2c_smbus_read_byte_data(client,
						      SI5341_DEVICE_READY);
		if (result < 0)
			return result;
		if (result == 0x0F)
			return 0;
		msleep(20);
	}
	dev_err(&client->dev, "timeout waiting for DEVICE_READY\n");
	return -EIO;
}

static const struct regmap_config si5341_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_MAPLE,
	.ranges = si5341_regmap_ranges,
	.num_ranges = ARRAY_SIZE(si5341_regmap_ranges),
	.max_register = SI5341_REGISTER_MAX,
	.volatile_table = &si5341_regmap_volatile,
};

static int si5341_dt_parse_dt(struct clk_si5341 *data,
			      struct clk_si5341_output_config *config)
{
	struct device_node *child;
	struct device_node *np = data->i2c_client->dev.of_node;
	u32 num;
	u32 val;

	memset(config, 0, sizeof(struct clk_si5341_output_config) *
				SI5341_MAX_NUM_OUTPUTS);

	for_each_child_of_node(np, child) {
		if (of_property_read_u32(child, "reg", &num)) {
			dev_err(&data->i2c_client->dev, "missing reg property of %s\n",
				child->name);
			goto put_child;
		}

		if (num >= SI5341_MAX_NUM_OUTPUTS) {
			dev_err(&data->i2c_client->dev, "invalid clkout %d\n", num);
			goto put_child;
		}

		if (!of_property_read_u32(child, "silabs,format", &val)) {
			/* Set cm and ampl conservatively to 3v3 settings */
			switch (val) {
			case 1: /* normal differential */
				config[num].out_cm_ampl_bits = 0x33;
				break;
			case 2: /* low-power differential */
				config[num].out_cm_ampl_bits = 0x13;
				break;
			case 4: /* LVCMOS */
				config[num].out_cm_ampl_bits = 0x33;
				/* Set SI recommended impedance for LVCMOS */
				config[num].out_format_drv_bits |= 0xc0;
				break;
			default:
				dev_err(&data->i2c_client->dev,
					"invalid silabs,format %u for %u\n",
					val, num);
				goto put_child;
			}
			config[num].out_format_drv_bits &= ~0x07;
			config[num].out_format_drv_bits |= val & 0x07;
			/* Always enable the SYNC feature */
			config[num].out_format_drv_bits |= 0x08;
		}

		if (!of_property_read_u32(child, "silabs,common-mode", &val)) {
			if (val > 0xf) {
				dev_err(&data->i2c_client->dev,
					"invalid silabs,common-mode %u\n",
					val);
				goto put_child;
			}
			config[num].out_cm_ampl_bits &= 0xf0;
			config[num].out_cm_ampl_bits |= val & 0x0f;
		}

		if (!of_property_read_u32(child, "silabs,amplitude", &val)) {
			if (val > 0xf) {
				dev_err(&data->i2c_client->dev,
					"invalid silabs,amplitude %u\n",
					val);
				goto put_child;
			}
			config[num].out_cm_ampl_bits &= 0x0f;
			config[num].out_cm_ampl_bits |= (val << 4) & 0xf0;
		}

		if (of_property_read_bool(child, "silabs,disable-high"))
			config[num].out_format_drv_bits |= 0x10;

		config[num].synth_master =
			of_property_read_bool(child, "silabs,synth-master");

		config[num].always_on =
			of_property_read_bool(child, "always-on");

		config[num].vdd_sel_bits = 0x08;
		if (data->clk[num].vddo_reg) {
			int vdd = regulator_get_voltage(data->clk[num].vddo_reg);

			switch (vdd) {
			case 3300000:
				config[num].vdd_sel_bits |= 0 << 4;
				break;
			case 1800000:
				config[num].vdd_sel_bits |= 1 << 4;
				break;
			case 2500000:
				config[num].vdd_sel_bits |= 2 << 4;
				break;
			default:
				dev_err(&data->i2c_client->dev,
					"unsupported vddo voltage %d for %s\n",
					vdd, child->name);
				goto put_child;
			}
		} else {
			/* chip seems to default to 2.5V when not set */
			dev_warn(&data->i2c_client->dev,
				"no regulator set, defaulting vdd_sel to 2.5V for %s\n",
				child->name);
			config[num].vdd_sel_bits |= 2 << 4;
		}
	}

	return 0;

put_child:
	of_node_put(child);
	return -EINVAL;
}

/*
 * If not pre-configured, calculate and set the PLL configuration manually.
 * For low-jitter performance, the PLL should be set such that the synthesizers
 * only need integer division.
 * Without any user guidance, we'll set the PLL to 14GHz, which still allows
 * the chip to generate any frequency on its outputs, but jitter performance
 * may be sub-optimal.
 */
static int si5341_initialize_pll(struct clk_si5341 *data)
{
	struct device_node *np = data->i2c_client->dev.of_node;
	u32 m_num = 0;
	u32 m_den = 0;
	int sel;

	if (of_property_read_u32(np, "silabs,pll-m-num", &m_num)) {
		dev_err(&data->i2c_client->dev,
			"PLL configuration requires silabs,pll-m-num\n");
	}
	if (of_property_read_u32(np, "silabs,pll-m-den", &m_den)) {
		dev_err(&data->i2c_client->dev,
			"PLL configuration requires silabs,pll-m-den\n");
	}

	if (!m_num || !m_den) {
		dev_err(&data->i2c_client->dev,
			"PLL configuration invalid, assume 14GHz\n");
		sel = si5341_clk_get_selected_input(data);
		if (sel < 0)
			return sel;

		m_den = clk_get_rate(data->input_clk[sel]) / 10;
		m_num = 1400000000;
	}

	return si5341_encode_44_32(data->regmap,
			SI5341_PLL_M_NUM, m_num, m_den);
}

static int si5341_clk_select_active_input(struct clk_si5341 *data)
{
	int res;
	int err;
	int i;

	res = si5341_clk_get_selected_input(data);
	if (res < 0)
		return res;

	/* If the current register setting is invalid, pick the first input */
	if (!data->input_clk[res]) {
		dev_dbg(&data->i2c_client->dev,
			"Input %d not connected, rerouting\n", res);
		res = -ENODEV;
		for (i = 0; i < SI5341_NUM_INPUTS; ++i) {
			if (data->input_clk[i]) {
				res = i;
				break;
			}
		}
		if (res < 0) {
			dev_err(&data->i2c_client->dev,
				"No clock input available\n");
			return res;
		}
	}

	/* Make sure the selected clock is also enabled and routed */
	err = si5341_clk_reparent(data, res);
	if (err < 0)
		return err;

	err = clk_prepare_enable(data->input_clk[res]);
	if (err < 0)
		return err;

	return res;
}

static ssize_t input_present_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct clk_si5341 *data = dev_get_drvdata(dev);
	u32 status;
	int res = regmap_read(data->regmap, SI5341_STATUS, &status);

	if (res < 0)
		return res;
	res = !(status & SI5341_STATUS_LOSREF);
	return sysfs_emit(buf, "%d\n", res);
}
static DEVICE_ATTR_RO(input_present);

static ssize_t input_present_sticky_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct clk_si5341 *data = dev_get_drvdata(dev);
	u32 status;
	int res = regmap_read(data->regmap, SI5341_STATUS_STICKY, &status);

	if (res < 0)
		return res;
	res = !(status & SI5341_STATUS_LOSREF);
	return sysfs_emit(buf, "%d\n", res);
}
static DEVICE_ATTR_RO(input_present_sticky);

static ssize_t pll_locked_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct clk_si5341 *data = dev_get_drvdata(dev);
	u32 status;
	int res = regmap_read(data->regmap, SI5341_STATUS, &status);

	if (res < 0)
		return res;
	res = !(status & SI5341_STATUS_LOL);
	return sysfs_emit(buf, "%d\n", res);
}
static DEVICE_ATTR_RO(pll_locked);

static ssize_t pll_locked_sticky_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct clk_si5341 *data = dev_get_drvdata(dev);
	u32 status;
	int res = regmap_read(data->regmap, SI5341_STATUS_STICKY, &status);

	if (res < 0)
		return res;
	res = !(status & SI5341_STATUS_LOL);
	return sysfs_emit(buf, "%d\n", res);
}
static DEVICE_ATTR_RO(pll_locked_sticky);

static ssize_t clear_sticky_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct clk_si5341 *data = dev_get_drvdata(dev);
	long val;

	if (kstrtol(buf, 10, &val))
		return -EINVAL;
	if (val) {
		int res = regmap_write(data->regmap, SI5341_STATUS_STICKY, 0);

		if (res < 0)
			return res;
	}
	return count;
}
static DEVICE_ATTR_WO(clear_sticky);

static const struct attribute *si5341_attributes[] = {
	&dev_attr_input_present.attr,
	&dev_attr_input_present_sticky.attr,
	&dev_attr_pll_locked.attr,
	&dev_attr_pll_locked_sticky.attr,
	&dev_attr_clear_sticky.attr,
	NULL
};

static int si5341_probe(struct i2c_client *client)
{
	struct clk_si5341 *data;
	struct clk_init_data init;
	struct clk *input;
	const char *root_clock_name;
	const char *synth_clock_names[SI5341_NUM_SYNTH] = { NULL };
	int err;
	unsigned int i;
	struct clk_si5341_output_config config[SI5341_MAX_NUM_OUTPUTS];
	bool initialization_required;
	u32 status;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->i2c_client = client;

	/* Must be done before otherwise touching hardware */
	err = si5341_wait_device_ready(client);
	if (err)
		return err;

	for (i = 0; i < SI5341_NUM_INPUTS; ++i) {
		input = devm_clk_get(&client->dev, si5341_input_clock_names[i]);
		if (IS_ERR(input)) {
			if (PTR_ERR(input) == -EPROBE_DEFER)
				return -EPROBE_DEFER;
			data->input_clk_name[i] = si5341_input_clock_names[i];
		} else {
			data->input_clk[i] = input;
			data->input_clk_name[i] = __clk_get_name(input);
		}
	}

	for (i = 0; i < SI5341_MAX_NUM_OUTPUTS; ++i) {
		char reg_name[10];

		snprintf(reg_name, sizeof(reg_name), "vddo%d", i);
		data->clk[i].vddo_reg = devm_regulator_get_optional(
			&client->dev, reg_name);
		if (IS_ERR(data->clk[i].vddo_reg)) {
			err = PTR_ERR(data->clk[i].vddo_reg);
			data->clk[i].vddo_reg = NULL;
			if (err == -ENODEV)
				continue;
			goto cleanup;
		} else {
			err = regulator_enable(data->clk[i].vddo_reg);
			if (err) {
				dev_err(&client->dev,
					"failed to enable %s regulator: %d\n",
					reg_name, err);
				data->clk[i].vddo_reg = NULL;
				goto cleanup;
			}
		}
	}

	err = si5341_dt_parse_dt(data, config);
	if (err)
		goto cleanup;

	if (of_property_read_string(client->dev.of_node, "clock-output-names",
			&init.name))
		init.name = client->dev.of_node->name;
	root_clock_name = init.name;

	data->regmap = devm_regmap_init_i2c(client, &si5341_regmap_config);
	if (IS_ERR(data->regmap)) {
		err = PTR_ERR(data->regmap);
		goto cleanup;
	}

	i2c_set_clientdata(client, data);

	err = si5341_probe_chip_id(data);
	if (err < 0)
		goto cleanup;

	if (of_property_read_bool(client->dev.of_node, "silabs,reprogram")) {
		initialization_required = true;
	} else {
		err = si5341_is_programmed_already(data);
		if (err < 0)
			goto cleanup;

		initialization_required = !err;
	}
	data->xaxb_ext_clk = of_property_read_bool(client->dev.of_node,
						   "silabs,xaxb-ext-clk");
	data->iovdd_33 = of_property_read_bool(client->dev.of_node,
					       "silabs,iovdd-33");

	if (initialization_required) {
		/* Populate the regmap cache in preparation for "cache only" */
		err = si5341_read_settings(data);
		if (err < 0)
			goto cleanup;

		err = si5341_send_preamble(data);
		if (err < 0)
			goto cleanup;

		/*
		 * We intend to send all 'final' register values in a single
		 * transaction. So cache all register writes until we're done
		 * configuring.
		 */
		regcache_cache_only(data->regmap, true);

		/* Write the configuration pairs from the firmware blob */
		err = si5341_write_multiple(data, si5341_reg_defaults,
					ARRAY_SIZE(si5341_reg_defaults));
		if (err < 0)
			goto cleanup;
	}

	/* Input must be up and running at this point */
	err = si5341_clk_select_active_input(data);
	if (err < 0)
		goto cleanup;

	if (initialization_required) {
		/* PLL configuration is required */
		err = si5341_initialize_pll(data);
		if (err < 0)
			goto cleanup;
	}

	/* Register the PLL */
	init.parent_names = data->input_clk_name;
	init.num_parents = SI5341_NUM_INPUTS;
	init.ops = &si5341_clk_ops;
	init.flags = 0;
	data->hw.init = &init;

	err = devm_clk_hw_register(&client->dev, &data->hw);
	if (err) {
		dev_err(&client->dev, "clock registration failed\n");
		goto cleanup;
	}

	init.num_parents = 1;
	init.parent_names = &root_clock_name;
	init.ops = &si5341_synth_clk_ops;
	for (i = 0; i < data->num_synth; ++i) {
		synth_clock_names[i] = devm_kasprintf(&client->dev, GFP_KERNEL,
				"%s.N%u", client->dev.of_node->name, i);
		if (!synth_clock_names[i]) {
			err = -ENOMEM;
			goto free_clk_names;
		}
		init.name = synth_clock_names[i];
		data->synth[i].index = i;
		data->synth[i].data = data;
		data->synth[i].hw.init = &init;
		err = devm_clk_hw_register(&client->dev, &data->synth[i].hw);
		if (err) {
			dev_err(&client->dev,
				"synth N%u registration failed\n", i);
			goto free_clk_names;
		}
	}

	init.num_parents = data->num_synth;
	init.parent_names = synth_clock_names;
	init.ops = &si5341_output_clk_ops;
	for (i = 0; i < data->num_outputs; ++i) {
		init.name = kasprintf(GFP_KERNEL, "%s.%d",
			client->dev.of_node->name, i);
		if (!init.name) {
			err = -ENOMEM;
			goto free_clk_names;
		}
		init.flags = config[i].synth_master ? CLK_SET_RATE_PARENT : 0;
		data->clk[i].index = i;
		data->clk[i].data = data;
		data->clk[i].hw.init = &init;
		if (config[i].out_format_drv_bits & 0x07) {
			regmap_write(data->regmap,
				SI5341_OUT_FORMAT(&data->clk[i]),
				config[i].out_format_drv_bits);
			regmap_write(data->regmap,
				SI5341_OUT_CM(&data->clk[i]),
				config[i].out_cm_ampl_bits);
			regmap_update_bits(data->regmap,
				SI5341_OUT_MUX_SEL(&data->clk[i]),
				SI5341_OUT_MUX_VDD_SEL_MASK,
				config[i].vdd_sel_bits);
		}
		err = devm_clk_hw_register(&client->dev, &data->clk[i].hw);
		kfree(init.name); /* clock framework made a copy of the name */
		if (err) {
			dev_err(&client->dev,
				"output %u registration failed\n", i);
			goto free_clk_names;
		}
		if (config[i].always_on)
			clk_prepare(data->clk[i].hw.clk);
	}

	err = devm_of_clk_add_hw_provider(&client->dev, of_clk_si5341_get,
			data);
	if (err) {
		dev_err(&client->dev, "unable to add clk provider\n");
		goto free_clk_names;
	}

	if (initialization_required) {
		/* Synchronize */
		regcache_cache_only(data->regmap, false);
		err = regcache_sync(data->regmap);
		if (err < 0)
			goto free_clk_names;

		err = si5341_finalize_defaults(data);
		if (err < 0)
			goto free_clk_names;
	}

	/* wait for device to report input clock present and PLL lock */
	err = regmap_read_poll_timeout(data->regmap, SI5341_STATUS, status,
		!(status & (SI5341_STATUS_LOSREF | SI5341_STATUS_LOL)),
	       10000, 250000);
	if (err) {
		dev_err(&client->dev, "Error waiting for input clock or PLL lock\n");
		goto free_clk_names;
	}

	/* clear sticky alarm bits from initialization */
	err = regmap_write(data->regmap, SI5341_STATUS_STICKY, 0);
	if (err) {
		dev_err(&client->dev, "unable to clear sticky status\n");
		goto free_clk_names;
	}

	err = sysfs_create_files(&client->dev.kobj, si5341_attributes);
	if (err)
		dev_err(&client->dev, "unable to create sysfs files\n");

free_clk_names:
	/* Free the names, clk framework makes copies */
	for (i = 0; i < data->num_synth; ++i)
		 devm_kfree(&client->dev, (void *)synth_clock_names[i]);

cleanup:
	if (err) {
		for (i = 0; i < SI5341_MAX_NUM_OUTPUTS; ++i) {
			if (data->clk[i].vddo_reg)
				regulator_disable(data->clk[i].vddo_reg);
		}
	}
	return err;
}

static void si5341_remove(struct i2c_client *client)
{
	struct clk_si5341 *data = i2c_get_clientdata(client);
	int i;

	sysfs_remove_files(&client->dev.kobj, si5341_attributes);

	for (i = 0; i < SI5341_MAX_NUM_OUTPUTS; ++i) {
		if (data->clk[i].vddo_reg)
			regulator_disable(data->clk[i].vddo_reg);
	}
}

static const struct i2c_device_id si5341_id[] = {
	{ "si5340", 0 },
	{ "si5341", 1 },
	{ "si5342", 2 },
	{ "si5344", 4 },
	{ "si5345", 5 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, si5341_id);

static const struct of_device_id clk_si5341_of_match[] = {
	{ .compatible = "silabs,si5340" },
	{ .compatible = "silabs,si5341" },
	{ .compatible = "silabs,si5342" },
	{ .compatible = "silabs,si5344" },
	{ .compatible = "silabs,si5345" },
	{ }
};
MODULE_DEVICE_TABLE(of, clk_si5341_of_match);

static struct i2c_driver si5341_driver = {
	.driver = {
		.name = "si5341",
		.of_match_table = clk_si5341_of_match,
	},
	.probe		= si5341_probe,
	.remove		= si5341_remove,
	.id_table	= si5341_id,
};
module_i2c_driver(si5341_driver);

MODULE_AUTHOR("Mike Looijmans <mike.looijmans@topic.nl>");
MODULE_DESCRIPTION("Si5341 driver");
MODULE_LICENSE("GPL");
