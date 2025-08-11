// SPDX-License-Identifier: GPL-2.0
/*
 * Common clock framework driver for the Versaclock7 family of timing devices.
 *
 * Copyright (c) 2022 Renesas Electronics Corporation
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/i2c.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/swab.h>

/*
 * 16-bit register address: the lower 8 bits of the register address come
 * from the offset addr byte and the upper 8 bits come from the page register.
 */
#define VC7_PAGE_ADDR			0xFD
#define VC7_PAGE_WINDOW			256
#define VC7_MAX_REG			0x364

/* Maximum number of banks supported by VC7 */
#define VC7_NUM_BANKS			7

/* Maximum number of FODs supported by VC7 */
#define VC7_NUM_FOD			3

/* Maximum number of IODs supported by VC7 */
#define VC7_NUM_IOD			4

/* Maximum number of outputs supported by VC7 */
#define VC7_NUM_OUT			12

/* VCO valid range is 9.5 GHz to 10.7 GHz */
#define VC7_APLL_VCO_MIN		9500000000UL
#define VC7_APLL_VCO_MAX		10700000000UL

/* APLL denominator is fixed at 2^27 */
#define VC7_APLL_DENOMINATOR_BITS	27

/* FOD 1st stage denominator is fixed 2^34 */
#define VC7_FOD_DENOMINATOR_BITS	34

/* IOD can operate between 1kHz and 650MHz */
#define VC7_IOD_RATE_MIN		1000UL
#define VC7_IOD_RATE_MAX		650000000UL
#define VC7_IOD_MIN_DIVISOR		14
#define VC7_IOD_MAX_DIVISOR		0x1ffffff /* 25-bit */

#define VC7_FOD_RATE_MIN		1000UL
#define VC7_FOD_RATE_MAX		650000000UL
#define VC7_FOD_1ST_STAGE_RATE_MIN	33000000UL /* 33 MHz */
#define VC7_FOD_1ST_STAGE_RATE_MAX	650000000UL /* 650 MHz */
#define VC7_FOD_1ST_INT_MAX		324
#define VC7_FOD_2ND_INT_MIN		2
#define VC7_FOD_2ND_INT_MAX		0x1ffff /* 17-bit */

/* VC7 Registers */

#define VC7_REG_XO_CNFG			0x2C
#define VC7_REG_XO_CNFG_COUNT		4
#define VC7_REG_XO_IB_H_DIV_SHIFT	24
#define VC7_REG_XO_IB_H_DIV_MASK	GENMASK(28, VC7_REG_XO_IB_H_DIV_SHIFT)

#define VC7_REG_APLL_FB_DIV_FRAC	0x120
#define VC7_REG_APLL_FB_DIV_FRAC_COUNT	4
#define VC7_REG_APLL_FB_DIV_FRAC_MASK	GENMASK(26, 0)

#define VC7_REG_APLL_FB_DIV_INT		0x124
#define VC7_REG_APLL_FB_DIV_INT_COUNT	2
#define VC7_REG_APLL_FB_DIV_INT_MASK	GENMASK(9, 0)

#define VC7_REG_APLL_CNFG		0x127
#define VC7_REG_APLL_EN_DOUBLER		BIT(0)

#define VC7_REG_OUT_BANK_CNFG(idx)	(0x280 + (0x4 * (idx)))
#define VC7_REG_OUTPUT_BANK_SRC_MASK	GENMASK(2, 0)

#define VC7_REG_FOD_INT_CNFG(idx)	(0x1E0 + (0x10 * (idx)))
#define VC7_REG_FOD_INT_CNFG_COUNT	8
#define VC7_REG_FOD_1ST_INT_MASK	GENMASK(8, 0)
#define VC7_REG_FOD_2ND_INT_SHIFT	9
#define VC7_REG_FOD_2ND_INT_MASK	GENMASK(25, VC7_REG_FOD_2ND_INT_SHIFT)
#define VC7_REG_FOD_FRAC_SHIFT		26
#define VC7_REG_FOD_FRAC_MASK		GENMASK_ULL(59, VC7_REG_FOD_FRAC_SHIFT)

#define VC7_REG_IOD_INT_CNFG(idx)	(0x1C0 + (0x8 * (idx)))
#define VC7_REG_IOD_INT_CNFG_COUNT	4
#define VC7_REG_IOD_INT_MASK		GENMASK(24, 0)

#define VC7_REG_ODRV_EN(idx)		(0x240 + (0x4 * (idx)))
#define VC7_REG_OUT_DIS			BIT(0)

struct vc7_driver_data;
static const struct regmap_config vc7_regmap_config;

/* Supported Renesas VC7 models */
enum vc7_model {
	VC7_RC21008A,
};

struct vc7_chip_info {
	const enum vc7_model model;
	const unsigned int banks[VC7_NUM_BANKS];
	const unsigned int num_banks;
	const unsigned int outputs[VC7_NUM_OUT];
	const unsigned int num_outputs;
};

/*
 * Changing the APLL frequency is currently not supported.
 * The APLL will consist of an opaque block between the XO and FOD/IODs and
 * its frequency will be computed based on the current state of the device.
 */
struct vc7_apll_data {
	struct clk *clk;
	struct vc7_driver_data *vc7;
	u8 xo_ib_h_div;
	u8 en_doubler;
	u16 apll_fb_div_int;
	u32 apll_fb_div_frac;
};

struct vc7_fod_data {
	struct clk_hw hw;
	struct vc7_driver_data *vc7;
	unsigned int num;
	u32 fod_1st_int;
	u32 fod_2nd_int;
	u64 fod_frac;
};

struct vc7_iod_data {
	struct clk_hw hw;
	struct vc7_driver_data *vc7;
	unsigned int num;
	u32 iod_int;
};

struct vc7_out_data {
	struct clk_hw hw;
	struct vc7_driver_data *vc7;
	unsigned int num;
	unsigned int out_dis;
};

struct vc7_driver_data {
	struct i2c_client *client;
	struct regmap *regmap;
	const struct vc7_chip_info *chip_info;

	struct clk *pin_xin;
	struct vc7_apll_data clk_apll;
	struct vc7_fod_data clk_fod[VC7_NUM_FOD];
	struct vc7_iod_data clk_iod[VC7_NUM_IOD];
	struct vc7_out_data clk_out[VC7_NUM_OUT];
};

struct vc7_bank_src_map {
	enum vc7_bank_src_type {
		VC7_FOD,
		VC7_IOD,
	} type;
	union _divider {
		struct vc7_iod_data *iod;
		struct vc7_fod_data *fod;
	} src;
};

static struct clk_hw *vc7_of_clk_get(struct of_phandle_args *clkspec,
				     void *data)
{
	struct vc7_driver_data *vc7 = data;
	unsigned int idx = clkspec->args[0];

	if (idx >= vc7->chip_info->num_outputs)
		return ERR_PTR(-EINVAL);

	return &vc7->clk_out[idx].hw;
}

static const unsigned int RC21008A_index_to_output_mapping[] = {
	1, 2, 3, 6, 7, 8, 10, 11
};

static int vc7_map_index_to_output(const enum vc7_model model, const unsigned int i)
{
	switch (model) {
	case VC7_RC21008A:
		return RC21008A_index_to_output_mapping[i];
	default:
		return i;
	}
}

/* bank to output mapping, same across all variants */
static const unsigned int output_bank_mapping[] = {
	0, /* Output 0 */
	1, /* Output 1 */
	2, /* Output 2 */
	2, /* Output 3 */
	3, /* Output 4 */
	3, /* Output 5 */
	3, /* Output 6 */
	3, /* Output 7 */
	4, /* Output 8 */
	4, /* Output 9 */
	5, /* Output 10 */
	6  /* Output 11 */
};

/**
 * vc7_64_mul_64_to_128() - Multiply two u64 and return an unsigned 128-bit integer
 * as an upper and lower part.
 *
 * @left: The left argument.
 * @right: The right argument.
 * @hi: The upper 64-bits of the 128-bit product.
 * @lo: The lower 64-bits of the 128-bit product.
 *
 * From mul_64_64 in crypto/ecc.c:350 in the linux kernel, accessed in v5.17.2.
 */
static void vc7_64_mul_64_to_128(u64 left, u64 right, u64 *hi, u64 *lo)
{
	u64 a0 = left & 0xffffffffull;
	u64 a1 = left >> 32;
	u64 b0 = right & 0xffffffffull;
	u64 b1 = right >> 32;
	u64 m0 = a0 * b0;
	u64 m1 = a0 * b1;
	u64 m2 = a1 * b0;
	u64 m3 = a1 * b1;

	m2 += (m0 >> 32);
	m2 += m1;

	/* Overflow */
	if (m2 < m1)
		m3 += 0x100000000ull;

	*lo = (m0 & 0xffffffffull) | (m2 << 32);
	*hi = m3 + (m2 >> 32);
}

/**
 * vc7_128_div_64_to_64() - Divides a 128-bit uint by a 64-bit divisor, return a 64-bit quotient.
 *
 * @numhi: The uppper 64-bits of the dividend.
 * @numlo: The lower 64-bits of the dividend.
 * @den: The denominator (divisor).
 * @r: The remainder, pass NULL if the remainder is not needed.
 *
 * Originally from libdivide, modified to use kernel u64/u32 types.
 *
 * See https://github.com/ridiculousfish/libdivide/blob/master/libdivide.h#L471.
 *
 * Return: The 64-bit quotient of the division.
 *
 * In case of overflow of division by zero, max(u64) is returned.
 */
static u64 vc7_128_div_64_to_64(u64 numhi, u64 numlo, u64 den, u64 *r)
{
	/*
	 * We work in base 2**32.
	 * A uint32 holds a single digit. A uint64 holds two digits.
	 * Our numerator is conceptually [num3, num2, num1, num0].
	 * Our denominator is [den1, den0].
	 */
	const u64 b = ((u64)1 << 32);

	/* The high and low digits of our computed quotient. */
	u32 q1, q0;

	/* The normalization shift factor */
	int shift;

	/*
	 * The high and low digits of our denominator (after normalizing).
	 * Also the low 2 digits of our numerator (after normalizing).
	 */
	u32 den1, den0, num1, num0;

	/* A partial remainder; */
	u64 rem;

	/*
	 * The estimated quotient, and its corresponding remainder (unrelated
	 * to true remainder).
	 */
	u64 qhat, rhat;

	/* Variables used to correct the estimated quotient. */
	u64 c1, c2;

	/* Check for overflow and divide by 0. */
	if (numhi >= den) {
		if (r)
			*r = ~0ull;
		return ~0ull;
	}

	/*
	 * Determine the normalization factor. We multiply den by this, so that
	 * its leading digit is at least half b. In binary this means just
	 * shifting left by the number of leading zeros, so that there's a 1 in
	 * the MSB.
	 *
	 * We also shift numer by the same amount. This cannot overflow because
	 * numhi < den.  The expression (-shift & 63) is the same as (64 -
	 * shift), except it avoids the UB of shifting by 64. The funny bitwise
	 * 'and' ensures that numlo does not get shifted into numhi if shift is
	 * 0. clang 11 has an x86 codegen bug here: see LLVM bug 50118. The
	 * sequence below avoids it.
	 */
	shift = __builtin_clzll(den);
	den <<= shift;
	numhi <<= shift;
	numhi |= (numlo >> (-shift & 63)) & (-(s64)shift >> 63);
	numlo <<= shift;

	/*
	 * Extract the low digits of the numerator and both digits of the
	 * denominator.
	 */
	num1 = (u32)(numlo >> 32);
	num0 = (u32)(numlo & 0xFFFFFFFFu);
	den1 = (u32)(den >> 32);
	den0 = (u32)(den & 0xFFFFFFFFu);

	/*
	 * We wish to compute q1 = [n3 n2 n1] / [d1 d0].
	 * Estimate q1 as [n3 n2] / [d1], and then correct it.
	 * Note while qhat may be 2 digits, q1 is always 1 digit.
	 */
	qhat = div64_u64_rem(numhi, den1, &rhat);
	c1 = qhat * den0;
	c2 = rhat * b + num1;
	if (c1 > c2)
		qhat -= (c1 - c2 > den) ? 2 : 1;
	q1 = (u32)qhat;

	/* Compute the true (partial) remainder. */
	rem = numhi * b + num1 - q1 * den;

	/*
	 * We wish to compute q0 = [rem1 rem0 n0] / [d1 d0].
	 * Estimate q0 as [rem1 rem0] / [d1] and correct it.
	 */
	qhat = div64_u64_rem(rem, den1, &rhat);
	c1 = qhat * den0;
	c2 = rhat * b + num0;
	if (c1 > c2)
		qhat -= (c1 - c2 > den) ? 2 : 1;
	q0 = (u32)qhat;

	/* Return remainder if requested. */
	if (r)
		*r = (rem * b + num0 - q0 * den) >> shift;
	return ((u64)q1 << 32) | q0;
}

static int vc7_get_bank_clk(struct vc7_driver_data *vc7,
			    unsigned int bank_idx,
			    unsigned int output_bank_src,
			    struct vc7_bank_src_map *map)
{
	/* Mapping from Table 38 in datasheet */
	if (bank_idx == 0 || bank_idx == 1) {
		switch (output_bank_src) {
		case 0:
			map->type = VC7_IOD,
			map->src.iod = &vc7->clk_iod[0];
			return 0;
		case 1:
			map->type = VC7_IOD,
			map->src.iod = &vc7->clk_iod[1];
			return 0;
		case 4:
			map->type = VC7_FOD,
			map->src.fod = &vc7->clk_fod[0];
			return 0;
		case 5:
			map->type = VC7_FOD,
			map->src.fod = &vc7->clk_fod[1];
			return 0;
		default:
			break;
		}
	} else if (bank_idx == 2) {
		switch (output_bank_src) {
		case 1:
			map->type = VC7_IOD,
			map->src.iod = &vc7->clk_iod[1];
			return 0;
		case 4:
			map->type = VC7_FOD,
			map->src.fod = &vc7->clk_fod[0];
			return 0;
		case 5:
			map->type = VC7_FOD,
			map->src.fod = &vc7->clk_fod[1];
			return 0;
		default:
			break;
		}
	} else if (bank_idx == 3) {
		switch (output_bank_src) {
		case 4:
			map->type = VC7_FOD,
			map->src.fod = &vc7->clk_fod[0];
			return 0;
		case 5:
			map->type = VC7_FOD,
			map->src.fod = &vc7->clk_fod[1];
			return 0;
		case 6:
			map->type = VC7_FOD,
			map->src.fod = &vc7->clk_fod[2];
			return 0;
		default:
			break;
		}
	} else if (bank_idx == 4) {
		switch (output_bank_src) {
		case 0:
			/* CLKIN1 not supported in this driver */
			break;
		case 2:
			map->type = VC7_IOD,
			map->src.iod = &vc7->clk_iod[2];
			return 0;
		case 5:
			map->type = VC7_FOD,
			map->src.fod = &vc7->clk_fod[1];
			return 0;
		case 6:
			map->type = VC7_FOD,
			map->src.fod = &vc7->clk_fod[2];
			return 0;
		case 7:
			/* CLKIN0 not supported in this driver */
			break;
		default:
			break;
		}
	} else if (bank_idx == 5) {
		switch (output_bank_src) {
		case 0:
			/* CLKIN1 not supported in this driver */
			break;
		case 1:
			/* XIN_REFIN not supported in this driver */
			break;
		case 2:
			map->type = VC7_IOD,
			map->src.iod = &vc7->clk_iod[2];
			return 0;
		case 3:
			map->type = VC7_IOD,
			map->src.iod = &vc7->clk_iod[3];
			return 0;
		case 5:
			map->type = VC7_FOD,
			map->src.fod = &vc7->clk_fod[1];
			return 0;
		case 6:
			map->type = VC7_FOD,
			map->src.fod = &vc7->clk_fod[2];
			return 0;
		case 7:
			/* CLKIN0 not supported in this driver */
			break;
		default:
			break;
		}
	} else if (bank_idx == 6) {
		switch (output_bank_src) {
		case 0:
			/* CLKIN1 not supported in this driver */
			break;
		case 2:
			map->type = VC7_IOD,
			map->src.iod = &vc7->clk_iod[2];
			return 0;
		case 3:
			map->type = VC7_IOD,
			map->src.iod = &vc7->clk_iod[3];
			return 0;
		case 5:
			map->type = VC7_FOD,
			map->src.fod = &vc7->clk_fod[1];
			return 0;
		case 6:
			map->type = VC7_FOD,
			map->src.fod = &vc7->clk_fod[2];
			return 0;
		case 7:
			/* CLKIN0 not supported in this driver */
			break;
		default:
			break;
		}
	}

	pr_warn("bank_src%d = %d is not supported\n", bank_idx, output_bank_src);
	return -1;
}

static int vc7_read_apll(struct vc7_driver_data *vc7)
{
	int err;
	u32 val32;
	u16 val16;

	err = regmap_bulk_read(vc7->regmap,
			       VC7_REG_XO_CNFG,
			       (u32 *)&val32,
			       VC7_REG_XO_CNFG_COUNT);
	if (err) {
		dev_err(&vc7->client->dev, "failed to read XO_CNFG\n");
		return err;
	}

	vc7->clk_apll.xo_ib_h_div = (val32 & VC7_REG_XO_IB_H_DIV_MASK)
		>> VC7_REG_XO_IB_H_DIV_SHIFT;

	err = regmap_read(vc7->regmap,
			  VC7_REG_APLL_CNFG,
			  &val32);
	if (err) {
		dev_err(&vc7->client->dev, "failed to read APLL_CNFG\n");
		return err;
	}

	vc7->clk_apll.en_doubler = val32 & VC7_REG_APLL_EN_DOUBLER;

	err = regmap_bulk_read(vc7->regmap,
			       VC7_REG_APLL_FB_DIV_FRAC,
			       (u32 *)&val32,
			       VC7_REG_APLL_FB_DIV_FRAC_COUNT);
	if (err) {
		dev_err(&vc7->client->dev, "failed to read APLL_FB_DIV_FRAC\n");
		return err;
	}

	vc7->clk_apll.apll_fb_div_frac = val32 & VC7_REG_APLL_FB_DIV_FRAC_MASK;

	err = regmap_bulk_read(vc7->regmap,
			       VC7_REG_APLL_FB_DIV_INT,
			       (u16 *)&val16,
			       VC7_REG_APLL_FB_DIV_INT_COUNT);
	if (err) {
		dev_err(&vc7->client->dev, "failed to read APLL_FB_DIV_INT\n");
		return err;
	}

	vc7->clk_apll.apll_fb_div_int = val16 & VC7_REG_APLL_FB_DIV_INT_MASK;

	return 0;
}

static int vc7_read_fod(struct vc7_driver_data *vc7, unsigned int idx)
{
	int err;
	u64 val;

	err = regmap_bulk_read(vc7->regmap,
			       VC7_REG_FOD_INT_CNFG(idx),
			       (u64 *)&val,
			       VC7_REG_FOD_INT_CNFG_COUNT);
	if (err) {
		dev_err(&vc7->client->dev, "failed to read FOD%d\n", idx);
		return err;
	}

	vc7->clk_fod[idx].fod_1st_int = (val & VC7_REG_FOD_1ST_INT_MASK);
	vc7->clk_fod[idx].fod_2nd_int =
	    (val & VC7_REG_FOD_2ND_INT_MASK) >> VC7_REG_FOD_2ND_INT_SHIFT;
	vc7->clk_fod[idx].fod_frac = (val & VC7_REG_FOD_FRAC_MASK)
		>> VC7_REG_FOD_FRAC_SHIFT;

	return 0;
}

static int vc7_write_fod(struct vc7_driver_data *vc7, unsigned int idx)
{
	int err;
	u64 val;

	/*
	 * FOD dividers are part of an atomic group where fod_1st_int,
	 * fod_2nd_int, and fod_frac must be written together. The new divider
	 * is applied when the MSB of fod_frac is written.
	 */

	err = regmap_bulk_read(vc7->regmap,
			       VC7_REG_FOD_INT_CNFG(idx),
			       (u64 *)&val,
			       VC7_REG_FOD_INT_CNFG_COUNT);
	if (err) {
		dev_err(&vc7->client->dev, "failed to read FOD%d\n", idx);
		return err;
	}

	val = u64_replace_bits(val,
			       vc7->clk_fod[idx].fod_1st_int,
			       VC7_REG_FOD_1ST_INT_MASK);
	val = u64_replace_bits(val,
			       vc7->clk_fod[idx].fod_2nd_int,
			       VC7_REG_FOD_2ND_INT_MASK);
	val = u64_replace_bits(val,
			       vc7->clk_fod[idx].fod_frac,
			       VC7_REG_FOD_FRAC_MASK);

	err = regmap_bulk_write(vc7->regmap,
				VC7_REG_FOD_INT_CNFG(idx),
				(u64 *)&val,
				sizeof(u64));
	if (err) {
		dev_err(&vc7->client->dev, "failed to write FOD%d\n", idx);
		return err;
	}

	return 0;
}

static int vc7_read_iod(struct vc7_driver_data *vc7, unsigned int idx)
{
	int err;
	u32 val;

	err = regmap_bulk_read(vc7->regmap,
			       VC7_REG_IOD_INT_CNFG(idx),
			       (u32 *)&val,
			       VC7_REG_IOD_INT_CNFG_COUNT);
	if (err) {
		dev_err(&vc7->client->dev, "failed to read IOD%d\n", idx);
		return err;
	}

	vc7->clk_iod[idx].iod_int = (val & VC7_REG_IOD_INT_MASK);

	return 0;
}

static int vc7_write_iod(struct vc7_driver_data *vc7, unsigned int idx)
{
	int err;
	u32 val;

	/*
	 * IOD divider field is atomic and all bits must be written.
	 * The new divider is applied when the MSB of iod_int is written.
	 */

	err = regmap_bulk_read(vc7->regmap,
			       VC7_REG_IOD_INT_CNFG(idx),
			       (u32 *)&val,
			       VC7_REG_IOD_INT_CNFG_COUNT);
	if (err) {
		dev_err(&vc7->client->dev, "failed to read IOD%d\n", idx);
		return err;
	}

	val = u32_replace_bits(val,
			       vc7->clk_iod[idx].iod_int,
			       VC7_REG_IOD_INT_MASK);

	err = regmap_bulk_write(vc7->regmap,
				VC7_REG_IOD_INT_CNFG(idx),
				(u32 *)&val,
				sizeof(u32));
	if (err) {
		dev_err(&vc7->client->dev, "failed to write IOD%d\n", idx);
		return err;
	}

	return 0;
}

static int vc7_read_output(struct vc7_driver_data *vc7, unsigned int idx)
{
	int err;
	unsigned int val, out_num;

	out_num = vc7_map_index_to_output(vc7->chip_info->model, idx);
	err = regmap_read(vc7->regmap,
			  VC7_REG_ODRV_EN(out_num),
			  &val);
	if (err) {
		dev_err(&vc7->client->dev, "failed to read ODRV_EN[%d]\n", idx);
		return err;
	}

	vc7->clk_out[idx].out_dis = val & VC7_REG_OUT_DIS;

	return 0;
}

static int vc7_write_output(struct vc7_driver_data *vc7, unsigned int idx)
{
	int err;
	unsigned int out_num;

	out_num = vc7_map_index_to_output(vc7->chip_info->model, idx);
	err = regmap_write_bits(vc7->regmap,
				VC7_REG_ODRV_EN(out_num),
				VC7_REG_OUT_DIS,
				vc7->clk_out[idx].out_dis);

	if (err) {
		dev_err(&vc7->client->dev, "failed to write ODRV_EN[%d]\n", idx);
		return err;
	}

	return 0;
}

static unsigned long vc7_get_apll_rate(struct vc7_driver_data *vc7)
{
	int err;
	unsigned long xtal_rate;
	u64 refin_div, apll_rate;

	xtal_rate = clk_get_rate(vc7->pin_xin);
	err = vc7_read_apll(vc7);
	if (err) {
		dev_err(&vc7->client->dev, "unable to read apll\n");
		return err;
	}

	/* 0 is bypassed, 1 is reserved */
	if (vc7->clk_apll.xo_ib_h_div < 2)
		refin_div = xtal_rate;
	else
		refin_div = div64_u64(xtal_rate, vc7->clk_apll.xo_ib_h_div);

	if (vc7->clk_apll.en_doubler)
		refin_div *= 2;

	/* divider = int + (frac / 2^27) */
	apll_rate = (refin_div * vc7->clk_apll.apll_fb_div_int) +
		    ((refin_div * vc7->clk_apll.apll_fb_div_frac) >> VC7_APLL_DENOMINATOR_BITS);

	pr_debug("%s - xo_ib_h_div: %u, apll_fb_div_int: %u, apll_fb_div_frac: %u\n",
		 __func__, vc7->clk_apll.xo_ib_h_div, vc7->clk_apll.apll_fb_div_int,
		 vc7->clk_apll.apll_fb_div_frac);
	pr_debug("%s - refin_div: %llu, apll rate: %llu\n",
		 __func__, refin_div, apll_rate);

	return apll_rate;
}

static void vc7_calc_iod_divider(unsigned long rate, unsigned long parent_rate,
				 u32 *divider)
{
	*divider = DIV_ROUND_UP(parent_rate, rate);
	if (*divider < VC7_IOD_MIN_DIVISOR)
		*divider = VC7_IOD_MIN_DIVISOR;
	if (*divider > VC7_IOD_MAX_DIVISOR)
		*divider = VC7_IOD_MAX_DIVISOR;
}

static void vc7_calc_fod_1st_stage(unsigned long rate, unsigned long parent_rate,
				   u32 *div_int, u64 *div_frac)
{
	u64 rem;

	*div_int = (u32)div64_u64_rem(parent_rate, rate, &rem);
	*div_frac = div64_u64(rem << VC7_FOD_DENOMINATOR_BITS, rate);
}

static unsigned long vc7_calc_fod_1st_stage_rate(unsigned long parent_rate,
						 u32 fod_1st_int, u64 fod_frac)
{
	u64 numer, denom, hi, lo, divisor;

	numer = fod_frac;
	denom = BIT_ULL(VC7_FOD_DENOMINATOR_BITS);

	if (fod_frac) {
		vc7_64_mul_64_to_128(parent_rate, denom, &hi, &lo);
		divisor = ((u64)fod_1st_int * denom) + numer;
		return vc7_128_div_64_to_64(hi, lo, divisor, NULL);
	}

	return div64_u64(parent_rate, fod_1st_int);
}

static unsigned long vc7_calc_fod_2nd_stage_rate(unsigned long parent_rate,
						 u32 fod_1st_int, u32 fod_2nd_int, u64 fod_frac)
{
	unsigned long fod_1st_stage_rate;

	fod_1st_stage_rate = vc7_calc_fod_1st_stage_rate(parent_rate, fod_1st_int, fod_frac);

	if (fod_2nd_int < 2)
		return fod_1st_stage_rate;

	/*
	 * There is a div-by-2 preceding the 2nd stage integer divider
	 * (not shown on block diagram) so the actual 2nd stage integer
	 * divisor is 2 * N.
	 */
	return div64_u64(fod_1st_stage_rate >> 1, fod_2nd_int);
}

static void vc7_calc_fod_divider(unsigned long rate, unsigned long parent_rate,
				 u32 *fod_1st_int, u32 *fod_2nd_int, u64 *fod_frac)
{
	unsigned int allow_frac, i, best_frac_i;
	unsigned long first_stage_rate;

	vc7_calc_fod_1st_stage(rate, parent_rate, fod_1st_int, fod_frac);
	first_stage_rate = vc7_calc_fod_1st_stage_rate(parent_rate, *fod_1st_int, *fod_frac);

	*fod_2nd_int = 0;

	/* Do we need the second stage integer divider? */
	if (first_stage_rate < VC7_FOD_1ST_STAGE_RATE_MIN) {
		allow_frac = 0;
		best_frac_i = VC7_FOD_2ND_INT_MIN;

		for (i = VC7_FOD_2ND_INT_MIN; i <= VC7_FOD_2ND_INT_MAX; i++) {
			/*
			 * 1) There is a div-by-2 preceding the 2nd stage integer divider
			 *    (not shown on block diagram) so the actual 2nd stage integer
			 *    divisor is 2 * N.
			 * 2) Attempt to find an integer solution first. This means stepping
			 *    through each 2nd stage integer and recalculating the 1st stage
			 *    until the 1st stage frequency is out of bounds. If no integer
			 *    solution is found, use the best fractional solution.
			 */
			vc7_calc_fod_1st_stage(parent_rate, rate * 2 * i, fod_1st_int, fod_frac);
			first_stage_rate = vc7_calc_fod_1st_stage_rate(parent_rate,
								       *fod_1st_int,
								       *fod_frac);

			/* Remember the first viable fractional solution */
			if (best_frac_i == VC7_FOD_2ND_INT_MIN &&
			    first_stage_rate > VC7_FOD_1ST_STAGE_RATE_MIN) {
				best_frac_i = i;
			}

			/* Is the divider viable? Prefer integer solutions over fractional. */
			if (*fod_1st_int < VC7_FOD_1ST_INT_MAX &&
			    first_stage_rate >= VC7_FOD_1ST_STAGE_RATE_MIN &&
			    (allow_frac || *fod_frac == 0)) {
				*fod_2nd_int = i;
				break;
			}

			/* Ran out of divisors or the 1st stage frequency is out of range */
			if (i >= VC7_FOD_2ND_INT_MAX ||
			    first_stage_rate > VC7_FOD_1ST_STAGE_RATE_MAX) {
				allow_frac = 1;
				i = best_frac_i;

				/* Restore the best frac and rerun the loop for the last time */
				if (best_frac_i != VC7_FOD_2ND_INT_MIN)
					i--;

				continue;
			}
		}
	}
}

static unsigned long vc7_fod_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct vc7_fod_data *fod = container_of(hw, struct vc7_fod_data, hw);
	struct vc7_driver_data *vc7 = fod->vc7;
	int err;
	unsigned long fod_rate;

	err = vc7_read_fod(vc7, fod->num);
	if (err) {
		dev_err(&vc7->client->dev, "error reading registers for %s\n",
			clk_hw_get_name(hw));
		return err;
	}

	pr_debug("%s - %s: parent_rate: %lu\n", __func__, clk_hw_get_name(hw), parent_rate);

	fod_rate = vc7_calc_fod_2nd_stage_rate(parent_rate, fod->fod_1st_int,
					       fod->fod_2nd_int, fod->fod_frac);

	pr_debug("%s - %s: fod_1st_int: %u, fod_2nd_int: %u, fod_frac: %llu\n",
		 __func__, clk_hw_get_name(hw),
		 fod->fod_1st_int, fod->fod_2nd_int, fod->fod_frac);
	pr_debug("%s - %s rate: %lu\n", __func__, clk_hw_get_name(hw), fod_rate);

	return fod_rate;
}

static int vc7_fod_determine_rate(struct clk_hw *hw,
				  struct clk_rate_request *req)
{
	struct vc7_fod_data *fod = container_of(hw, struct vc7_fod_data, hw);
	unsigned long fod_rate;

	pr_debug("%s - %s: requested rate: %lu, parent_rate: %lu\n",
		 __func__, clk_hw_get_name(hw), req->rate, req->best_parent_rate);

	vc7_calc_fod_divider(req->rate, req->best_parent_rate,
			     &fod->fod_1st_int, &fod->fod_2nd_int, &fod->fod_frac);
	fod_rate = vc7_calc_fod_2nd_stage_rate(req->best_parent_rate, fod->fod_1st_int,
					       fod->fod_2nd_int, fod->fod_frac);

	pr_debug("%s - %s: fod_1st_int: %u, fod_2nd_int: %u, fod_frac: %llu\n",
		 __func__, clk_hw_get_name(hw),
		 fod->fod_1st_int, fod->fod_2nd_int, fod->fod_frac);
	pr_debug("%s - %s rate: %lu\n", __func__, clk_hw_get_name(hw), fod_rate);

	req->rate = fod_rate;

	return 0;
}

static int vc7_fod_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long parent_rate)
{
	struct vc7_fod_data *fod = container_of(hw, struct vc7_fod_data, hw);
	struct vc7_driver_data *vc7 = fod->vc7;
	unsigned long fod_rate;

	pr_debug("%s - %s: rate: %lu, parent_rate: %lu\n",
		 __func__, clk_hw_get_name(hw), rate, parent_rate);

	if (rate < VC7_FOD_RATE_MIN || rate > VC7_FOD_RATE_MAX) {
		dev_err(&vc7->client->dev,
			"requested frequency %lu Hz for %s is out of range\n",
			rate, clk_hw_get_name(hw));
		return -EINVAL;
	}

	vc7_write_fod(vc7, fod->num);

	fod_rate = vc7_calc_fod_2nd_stage_rate(parent_rate, fod->fod_1st_int,
					       fod->fod_2nd_int, fod->fod_frac);

	pr_debug("%s - %s: fod_1st_int: %u, fod_2nd_int: %u, fod_frac: %llu\n",
		 __func__, clk_hw_get_name(hw),
		 fod->fod_1st_int, fod->fod_2nd_int, fod->fod_frac);
	pr_debug("%s - %s rate: %lu\n", __func__, clk_hw_get_name(hw), fod_rate);

	return 0;
}

static const struct clk_ops vc7_fod_ops = {
	.recalc_rate = vc7_fod_recalc_rate,
	.determine_rate = vc7_fod_determine_rate,
	.set_rate = vc7_fod_set_rate,
};

static unsigned long vc7_iod_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct vc7_iod_data *iod = container_of(hw, struct vc7_iod_data, hw);
	struct vc7_driver_data *vc7 = iod->vc7;
	int err;
	unsigned long iod_rate;

	err = vc7_read_iod(vc7, iod->num);
	if (err) {
		dev_err(&vc7->client->dev, "error reading registers for %s\n",
			clk_hw_get_name(hw));
		return err;
	}

	iod_rate = div64_u64(parent_rate, iod->iod_int);

	pr_debug("%s - %s: iod_int: %u\n", __func__, clk_hw_get_name(hw), iod->iod_int);
	pr_debug("%s - %s rate: %lu\n", __func__, clk_hw_get_name(hw), iod_rate);

	return iod_rate;
}

static int vc7_iod_determine_rate(struct clk_hw *hw,
				  struct clk_rate_request *req)
{
	struct vc7_iod_data *iod = container_of(hw, struct vc7_iod_data, hw);
	unsigned long iod_rate;

	pr_debug("%s - %s: requested rate: %lu, parent_rate: %lu\n",
		 __func__, clk_hw_get_name(hw), req->rate, req->best_parent_rate);

	vc7_calc_iod_divider(req->rate, req->best_parent_rate, &iod->iod_int);
	iod_rate = div64_u64(req->best_parent_rate, iod->iod_int);

	pr_debug("%s - %s: iod_int: %u\n", __func__, clk_hw_get_name(hw), iod->iod_int);
	pr_debug("%s - %s rate: %ld\n", __func__, clk_hw_get_name(hw), iod_rate);

	req->rate = iod_rate;

	return 0;
}

static int vc7_iod_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long parent_rate)
{
	struct vc7_iod_data *iod = container_of(hw, struct vc7_iod_data, hw);
	struct vc7_driver_data *vc7 = iod->vc7;
	unsigned long iod_rate;

	pr_debug("%s - %s: rate: %lu, parent_rate: %lu\n",
		 __func__, clk_hw_get_name(hw), rate, parent_rate);

	if (rate < VC7_IOD_RATE_MIN || rate > VC7_IOD_RATE_MAX) {
		dev_err(&vc7->client->dev,
			"requested frequency %lu Hz for %s is out of range\n",
			rate, clk_hw_get_name(hw));
		return -EINVAL;
	}

	vc7_write_iod(vc7, iod->num);

	iod_rate = div64_u64(parent_rate, iod->iod_int);

	pr_debug("%s - %s: iod_int: %u\n", __func__, clk_hw_get_name(hw), iod->iod_int);
	pr_debug("%s - %s rate: %ld\n", __func__, clk_hw_get_name(hw), iod_rate);

	return 0;
}

static const struct clk_ops vc7_iod_ops = {
	.recalc_rate = vc7_iod_recalc_rate,
	.determine_rate = vc7_iod_determine_rate,
	.set_rate = vc7_iod_set_rate,
};

static int vc7_clk_out_prepare(struct clk_hw *hw)
{
	struct vc7_out_data *out = container_of(hw, struct vc7_out_data, hw);
	struct vc7_driver_data *vc7 = out->vc7;
	int err;

	out->out_dis = 0;

	err = vc7_write_output(vc7, out->num);
	if (err) {
		dev_err(&vc7->client->dev, "error writing registers for %s\n",
			clk_hw_get_name(hw));
		return err;
	}

	pr_debug("%s - %s: clk prepared\n", __func__, clk_hw_get_name(hw));

	return 0;
}

static void vc7_clk_out_unprepare(struct clk_hw *hw)
{
	struct vc7_out_data *out = container_of(hw, struct vc7_out_data, hw);
	struct vc7_driver_data *vc7 = out->vc7;
	int err;

	out->out_dis = 1;

	err = vc7_write_output(vc7, out->num);
	if (err) {
		dev_err(&vc7->client->dev, "error writing registers for %s\n",
			clk_hw_get_name(hw));
		return;
	}

	pr_debug("%s - %s: clk unprepared\n", __func__, clk_hw_get_name(hw));
}

static int vc7_clk_out_is_enabled(struct clk_hw *hw)
{
	struct vc7_out_data *out = container_of(hw, struct vc7_out_data, hw);
	struct vc7_driver_data *vc7 = out->vc7;
	int err, is_enabled;

	err = vc7_read_output(vc7, out->num);
	if (err) {
		dev_err(&vc7->client->dev, "error reading registers for %s\n",
			clk_hw_get_name(hw));
		return err;
	}

	is_enabled = !out->out_dis;

	pr_debug("%s - %s: is_enabled=%d\n", __func__, clk_hw_get_name(hw), is_enabled);

	return is_enabled;
}

static const struct clk_ops vc7_clk_out_ops = {
	.prepare = vc7_clk_out_prepare,
	.unprepare = vc7_clk_out_unprepare,
	.is_enabled = vc7_clk_out_is_enabled,
};

static int vc7_probe(struct i2c_client *client)
{
	struct vc7_driver_data *vc7;
	struct clk_init_data clk_init;
	struct vc7_bank_src_map bank_src_map;
	const char *node_name, *apll_name;
	const char *parent_names[1];
	unsigned int i, val, bank_idx, out_num;
	unsigned long apll_rate;
	int ret;

	vc7 = devm_kzalloc(&client->dev, sizeof(*vc7), GFP_KERNEL);
	if (!vc7)
		return -ENOMEM;

	i2c_set_clientdata(client, vc7);
	vc7->client = client;
	vc7->chip_info = i2c_get_match_data(client);

	vc7->pin_xin = devm_clk_get(&client->dev, "xin");
	if (PTR_ERR(vc7->pin_xin) == -EPROBE_DEFER) {
		return dev_err_probe(&client->dev, -EPROBE_DEFER,
				     "xin not specified\n");
	}

	vc7->regmap = devm_regmap_init_i2c(client, &vc7_regmap_config);
	if (IS_ERR(vc7->regmap)) {
		return dev_err_probe(&client->dev, PTR_ERR(vc7->regmap),
				     "failed to allocate register map\n");
	}

	if (of_property_read_string(client->dev.of_node, "clock-output-names",
				    &node_name))
		node_name = client->dev.of_node->name;

	/* Register APLL */
	apll_rate = vc7_get_apll_rate(vc7);
	apll_name = kasprintf(GFP_KERNEL, "%s_apll", node_name);
	vc7->clk_apll.clk = clk_register_fixed_rate(&client->dev, apll_name,
						    __clk_get_name(vc7->pin_xin),
						    0, apll_rate);
	kfree(apll_name); /* ccf made a copy of the name */
	if (IS_ERR(vc7->clk_apll.clk)) {
		return dev_err_probe(&client->dev, PTR_ERR(vc7->clk_apll.clk),
				     "failed to register apll\n");
	}

	/* Register FODs */
	for (i = 0; i < VC7_NUM_FOD; i++) {
		memset(&clk_init, 0, sizeof(clk_init));
		clk_init.name = kasprintf(GFP_KERNEL, "%s_fod%d", node_name, i);
		clk_init.ops = &vc7_fod_ops;
		clk_init.parent_names = parent_names;
		parent_names[0] = __clk_get_name(vc7->clk_apll.clk);
		clk_init.num_parents = 1;
		vc7->clk_fod[i].num = i;
		vc7->clk_fod[i].vc7 = vc7;
		vc7->clk_fod[i].hw.init = &clk_init;
		ret = devm_clk_hw_register(&client->dev, &vc7->clk_fod[i].hw);
		if (ret)
			goto err_clk_register;
		kfree(clk_init.name); /* ccf made a copy of the name */
	}

	/* Register IODs */
	for (i = 0; i < VC7_NUM_IOD; i++) {
		memset(&clk_init, 0, sizeof(clk_init));
		clk_init.name = kasprintf(GFP_KERNEL, "%s_iod%d", node_name, i);
		clk_init.ops = &vc7_iod_ops;
		clk_init.parent_names = parent_names;
		parent_names[0] = __clk_get_name(vc7->clk_apll.clk);
		clk_init.num_parents = 1;
		vc7->clk_iod[i].num = i;
		vc7->clk_iod[i].vc7 = vc7;
		vc7->clk_iod[i].hw.init = &clk_init;
		ret = devm_clk_hw_register(&client->dev, &vc7->clk_iod[i].hw);
		if (ret)
			goto err_clk_register;
		kfree(clk_init.name); /* ccf made a copy of the name */
	}

	/* Register outputs */
	for (i = 0; i < vc7->chip_info->num_outputs; i++) {
		out_num = vc7_map_index_to_output(vc7->chip_info->model, i);

		/*
		 * This driver does not support remapping FOD/IOD to banks.
		 * The device state is read and the driver is setup to match
		 * the device's existing mapping.
		 */
		bank_idx = output_bank_mapping[out_num];

		regmap_read(vc7->regmap, VC7_REG_OUT_BANK_CNFG(bank_idx), &val);
		val &= VC7_REG_OUTPUT_BANK_SRC_MASK;

		memset(&bank_src_map, 0, sizeof(bank_src_map));
		ret = vc7_get_bank_clk(vc7, bank_idx, val, &bank_src_map);
		if (ret) {
			dev_err_probe(&client->dev, ret,
				      "unable to register output %d\n", i);
			return ret;
		}

		switch (bank_src_map.type) {
		case VC7_FOD:
			parent_names[0] = clk_hw_get_name(&bank_src_map.src.fod->hw);
			break;
		case VC7_IOD:
			parent_names[0] = clk_hw_get_name(&bank_src_map.src.iod->hw);
			break;
		}

		memset(&clk_init, 0, sizeof(clk_init));
		clk_init.name = kasprintf(GFP_KERNEL, "%s_out%d", node_name, i);
		clk_init.ops = &vc7_clk_out_ops;
		clk_init.flags = CLK_SET_RATE_PARENT;
		clk_init.parent_names = parent_names;
		clk_init.num_parents = 1;
		vc7->clk_out[i].num = i;
		vc7->clk_out[i].vc7 = vc7;
		vc7->clk_out[i].hw.init = &clk_init;
		ret = devm_clk_hw_register(&client->dev, &vc7->clk_out[i].hw);
		if (ret)
			goto err_clk_register;
		kfree(clk_init.name); /* ccf made a copy of the name */
	}

	ret = of_clk_add_hw_provider(client->dev.of_node, vc7_of_clk_get, vc7);
	if (ret) {
		dev_err_probe(&client->dev, ret, "unable to add clk provider\n");
		goto err_clk;
	}

	return ret;

err_clk_register:
	dev_err_probe(&client->dev, ret,
		      "unable to register %s\n", clk_init.name);
	kfree(clk_init.name); /* ccf made a copy of the name */
err_clk:
	clk_unregister_fixed_rate(vc7->clk_apll.clk);
	return ret;
}

static void vc7_remove(struct i2c_client *client)
{
	struct vc7_driver_data *vc7 = i2c_get_clientdata(client);

	of_clk_del_provider(client->dev.of_node);
	clk_unregister_fixed_rate(vc7->clk_apll.clk);
}

static bool vc7_volatile_reg(struct device *dev, unsigned int reg)
{
	if (reg == VC7_PAGE_ADDR)
		return false;

	return true;
}

static const struct vc7_chip_info vc7_rc21008a_info = {
	.model = VC7_RC21008A,
	.num_banks = 6,
	.num_outputs = 8,
};

static const struct regmap_range_cfg vc7_range_cfg[] = {
{
	.range_min = 0,
	.range_max = VC7_MAX_REG,
	.selector_reg = VC7_PAGE_ADDR,
	.selector_mask = 0xFF,
	.selector_shift = 0,
	.window_start = 0,
	.window_len = VC7_PAGE_WINDOW,
}};

static const struct regmap_config vc7_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = VC7_MAX_REG,
	.ranges = vc7_range_cfg,
	.num_ranges = ARRAY_SIZE(vc7_range_cfg),
	.volatile_reg = vc7_volatile_reg,
	.cache_type = REGCACHE_MAPLE,
	.can_multi_write = true,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};

static const struct i2c_device_id vc7_i2c_id[] = {
	{ "rc21008a", .driver_data = (kernel_ulong_t)&vc7_rc21008a_info },
	{}
};
MODULE_DEVICE_TABLE(i2c, vc7_i2c_id);

static const struct of_device_id vc7_of_match[] = {
	{ .compatible = "renesas,rc21008a", .data = &vc7_rc21008a_info },
	{}
};
MODULE_DEVICE_TABLE(of, vc7_of_match);

static struct i2c_driver vc7_i2c_driver = {
	.driver = {
		.name = "vc7",
		.of_match_table = vc7_of_match,
	},
	.probe = vc7_probe,
	.remove = vc7_remove,
	.id_table = vc7_i2c_id,
};
module_i2c_driver(vc7_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alex Helms <alexander.helms.jy@renesas.com");
MODULE_DESCRIPTION("Renesas Versaclock7 common clock framework driver");
