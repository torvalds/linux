/*
 * Scaler library
 *
 * Copyright (c) 2013 Texas Instruments Inc.
 *
 * David Griego, <dagriego@biglakesoftware.com>
 * Dale Farnsworth, <dale@farnsworth.org>
 * Archit Taneja, <archit@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "sc.h"
#include "sc_coeff.h"

void sc_dump_regs(struct sc_data *sc)
{
	struct device *dev = &sc->pdev->dev;

#define DUMPREG(r) dev_dbg(dev, "%-35s %08x\n", #r, \
	ioread32(sc->base + CFG_##r))

	DUMPREG(SC0);
	DUMPREG(SC1);
	DUMPREG(SC2);
	DUMPREG(SC3);
	DUMPREG(SC4);
	DUMPREG(SC5);
	DUMPREG(SC6);
	DUMPREG(SC8);
	DUMPREG(SC9);
	DUMPREG(SC10);
	DUMPREG(SC11);
	DUMPREG(SC12);
	DUMPREG(SC13);
	DUMPREG(SC17);
	DUMPREG(SC18);
	DUMPREG(SC19);
	DUMPREG(SC20);
	DUMPREG(SC21);
	DUMPREG(SC22);
	DUMPREG(SC23);
	DUMPREG(SC24);
	DUMPREG(SC25);

#undef DUMPREG
}

/*
 * set the horizontal scaler coefficients according to the ratio of output to
 * input widths, after accounting for up to two levels of decimation
 */
void sc_set_hs_coeffs(struct sc_data *sc, void *addr, unsigned int src_w,
		unsigned int dst_w)
{
	int sixteenths;
	int idx;
	int i, j;
	u16 *coeff_h = addr;
	const u16 *cp;

	if (dst_w > src_w) {
		idx = HS_UP_SCALE;
	} else {
		if ((dst_w << 1) < src_w)
			dst_w <<= 1;	/* first level decimation */
		if ((dst_w << 1) < src_w)
			dst_w <<= 1;	/* second level decimation */

		if (dst_w == src_w) {
			idx = HS_LE_16_16_SCALE;
		} else {
			sixteenths = (dst_w << 4) / src_w;
			if (sixteenths < 8)
				sixteenths = 8;
			idx = HS_LT_9_16_SCALE + sixteenths - 8;
		}
	}

	if (idx == sc->hs_index)
		return;

	cp = scaler_hs_coeffs[idx];

	for (i = 0; i < SC_NUM_PHASES * 2; i++) {
		for (j = 0; j < SC_H_NUM_TAPS; j++)
			*coeff_h++ = *cp++;
		/*
		 * for each phase, the scaler expects space for 8 coefficients
		 * in it's memory. For the horizontal scaler, we copy the first
		 * 7 coefficients and skip the last slot to move to the next
		 * row to hold coefficients for the next phase
		 */
		coeff_h += SC_NUM_TAPS_MEM_ALIGN - SC_H_NUM_TAPS;
	}

	sc->hs_index = idx;

	sc->load_coeff_h = true;
}

/*
 * set the vertical scaler coefficients according to the ratio of output to
 * input heights
 */
void sc_set_vs_coeffs(struct sc_data *sc, void *addr, unsigned int src_h,
		unsigned int dst_h)
{
	int sixteenths;
	int idx;
	int i, j;
	u16 *coeff_v = addr;
	const u16 *cp;

	if (dst_h > src_h) {
		idx = VS_UP_SCALE;
	} else if (dst_h == src_h) {
		idx = VS_1_TO_1_SCALE;
	} else {
		sixteenths = (dst_h << 4) / src_h;
		if (sixteenths < 8)
			sixteenths = 8;
		idx = VS_LT_9_16_SCALE + sixteenths - 8;
	}

	if (idx == sc->vs_index)
		return;

	cp = scaler_vs_coeffs[idx];

	for (i = 0; i < SC_NUM_PHASES * 2; i++) {
		for (j = 0; j < SC_V_NUM_TAPS; j++)
			*coeff_v++ = *cp++;
		/*
		 * for the vertical scaler, we copy the first 5 coefficients and
		 * skip the last 3 slots to move to the next row to hold
		 * coefficients for the next phase
		 */
		coeff_v += SC_NUM_TAPS_MEM_ALIGN - SC_V_NUM_TAPS;
	}

	sc->vs_index = idx;
	sc->load_coeff_v = true;
}

void sc_config_scaler(struct sc_data *sc, u32 *sc_reg0, u32 *sc_reg8,
		u32 *sc_reg17, unsigned int src_w, unsigned int src_h,
		unsigned int dst_w, unsigned int dst_h)
{
	struct device *dev = &sc->pdev->dev;
	u32 val;
	int dcm_x, dcm_shift;
	bool use_rav;
	unsigned long lltmp;
	u32 lin_acc_inc, lin_acc_inc_u;
	u32 col_acc_offset;
	u16 factor = 0;
	int row_acc_init_rav = 0, row_acc_init_rav_b = 0;
	u32 row_acc_inc = 0, row_acc_offset = 0, row_acc_offset_b = 0;
	/*
	 * location of SC register in payload memory with respect to the first
	 * register in the mmr address data block
	 */
	u32 *sc_reg9 = sc_reg8 + 1;
	u32 *sc_reg12 = sc_reg8 + 4;
	u32 *sc_reg13 = sc_reg8 + 5;
	u32 *sc_reg24 = sc_reg17 + 7;

	val = sc_reg0[0];

	/* clear all the features(they may get enabled elsewhere later) */
	val &= ~(CFG_SELFGEN_FID | CFG_TRIM | CFG_ENABLE_SIN2_VER_INTP |
		CFG_INTERLACE_I | CFG_DCM_4X | CFG_DCM_2X | CFG_AUTO_HS |
		CFG_ENABLE_EV | CFG_USE_RAV | CFG_INVT_FID | CFG_SC_BYPASS |
		CFG_INTERLACE_O | CFG_Y_PK_EN | CFG_HP_BYPASS | CFG_LINEAR);

	if (src_w == dst_w && src_h == dst_h) {
		val |= CFG_SC_BYPASS;
		sc_reg0[0] = val;
		return;
	}

	/* we only support linear scaling for now */
	val |= CFG_LINEAR;

	/* configure horizontal scaler */

	/* enable 2X or 4X decimation */
	dcm_x = src_w / dst_w;
	if (dcm_x > 4) {
		val |= CFG_DCM_4X;
		dcm_shift = 2;
	} else if (dcm_x > 2) {
		val |= CFG_DCM_2X;
		dcm_shift = 1;
	} else {
		dcm_shift = 0;
	}

	lltmp = dst_w - 1;
	lin_acc_inc = div64_u64(((u64)(src_w >> dcm_shift) - 1) << 24, lltmp);
	lin_acc_inc_u = 0;
	col_acc_offset = 0;

	dev_dbg(dev, "hs config: src_w = %d, dst_w = %d, decimation = %s, lin_acc_inc = %08x\n",
		src_w, dst_w, dcm_shift == 2 ? "4x" :
		(dcm_shift == 1 ? "2x" : "none"), lin_acc_inc);

	/* configure vertical scaler */

	/* use RAV for vertical scaler if vertical downscaling is > 4x */
	if (dst_h < (src_h >> 2)) {
		use_rav = true;
		val |= CFG_USE_RAV;
	} else {
		use_rav = false;
	}

	if (use_rav) {
		/* use RAV */
		factor = (u16) ((dst_h << 10) / src_h);

		row_acc_init_rav = factor + ((1 + factor) >> 1);
		if (row_acc_init_rav >= 1024)
			row_acc_init_rav -= 1024;

		row_acc_init_rav_b = row_acc_init_rav +
				(1 + (row_acc_init_rav >> 1)) -
				(1024 >> 1);

		if (row_acc_init_rav_b < 0) {
			row_acc_init_rav_b += row_acc_init_rav;
			row_acc_init_rav *= 2;
		}

		dev_dbg(dev, "vs config(RAV): src_h = %d, dst_h = %d, factor = %d, acc_init = %08x, acc_init_b = %08x\n",
			src_h, dst_h, factor, row_acc_init_rav,
			row_acc_init_rav_b);
	} else {
		/* use polyphase */
		row_acc_inc = ((src_h - 1) << 16) / (dst_h - 1);
		row_acc_offset = 0;
		row_acc_offset_b = 0;

		dev_dbg(dev, "vs config(POLY): src_h = %d, dst_h = %d,row_acc_inc = %08x\n",
			src_h, dst_h, row_acc_inc);
	}


	sc_reg0[0] = val;
	sc_reg0[1] = row_acc_inc;
	sc_reg0[2] = row_acc_offset;
	sc_reg0[3] = row_acc_offset_b;

	sc_reg0[4] = ((lin_acc_inc_u & CFG_LIN_ACC_INC_U_MASK) <<
			CFG_LIN_ACC_INC_U_SHIFT) | (dst_w << CFG_TAR_W_SHIFT) |
			(dst_h << CFG_TAR_H_SHIFT);

	sc_reg0[5] = (src_w << CFG_SRC_W_SHIFT) | (src_h << CFG_SRC_H_SHIFT);

	sc_reg0[6] = (row_acc_init_rav_b << CFG_ROW_ACC_INIT_RAV_B_SHIFT) |
		(row_acc_init_rav << CFG_ROW_ACC_INIT_RAV_SHIFT);

	*sc_reg9 = lin_acc_inc;

	*sc_reg12 = col_acc_offset << CFG_COL_ACC_OFFSET_SHIFT;

	*sc_reg13 = factor;

	*sc_reg24 = (src_w << CFG_ORG_W_SHIFT) | (src_h << CFG_ORG_H_SHIFT);
}

struct sc_data *sc_create(struct platform_device *pdev)
{
	struct sc_data *sc;

	dev_dbg(&pdev->dev, "sc_create\n");

	sc = devm_kzalloc(&pdev->dev, sizeof(*sc), GFP_KERNEL);
	if (!sc) {
		dev_err(&pdev->dev, "couldn't alloc sc_data\n");
		return ERR_PTR(-ENOMEM);
	}

	sc->pdev = pdev;

	sc->res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sc");
	if (!sc->res) {
		dev_err(&pdev->dev, "missing platform resources data\n");
		return ERR_PTR(-ENODEV);
	}

	sc->base = devm_ioremap_resource(&pdev->dev, sc->res);
	if (IS_ERR(sc->base)) {
		dev_err(&pdev->dev, "failed to ioremap\n");
		return ERR_CAST(sc->base);
	}

	return sc;
}
