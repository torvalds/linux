// SPDX-License-Identifier: GPL-2.0
/*
 * stf_isp_hw_ops.c
 *
 * Register interface file for StarFive ISP driver
 *
 * Copyright (C) 2021-2023 StarFive Technology Co., Ltd.
 *
 */

#include "stf-camss.h"

static void stf_isp_config_obc(struct stfcamss *stfcamss)
{
	u32 reg_val, reg_add;

	stf_isp_reg_write(stfcamss, ISP_REG_OBC_CFG, OBC_W_H(11) | OBC_W_W(11));

	reg_val = GAIN_D_POINT(0x40) | GAIN_C_POINT(0x40) |
		  GAIN_B_POINT(0x40) | GAIN_A_POINT(0x40);
	for (reg_add = ISP_REG_OBCG_CFG_0; reg_add <= ISP_REG_OBCG_CFG_3;) {
		stf_isp_reg_write(stfcamss, reg_add, reg_val);
		reg_add += 4;
	}

	reg_val = OFFSET_D_POINT(0) | OFFSET_C_POINT(0) |
		  OFFSET_B_POINT(0) | OFFSET_A_POINT(0);
	for (reg_add = ISP_REG_OBCO_CFG_0; reg_add <= ISP_REG_OBCO_CFG_3;) {
		stf_isp_reg_write(stfcamss, reg_add, reg_val);
		reg_add += 4;
	}
}

static void stf_isp_config_oecf(struct stfcamss *stfcamss)
{
	u32 reg_add, par_val;
	u16 par_h, par_l;

	par_h = 0x10; par_l = 0;
	par_val = OCEF_PAR_H(par_h) | OCEF_PAR_L(par_l);
	for (reg_add = ISP_REG_OECF_X0_CFG0; reg_add <= ISP_REG_OECF_Y3_CFG0;) {
		stf_isp_reg_write(stfcamss, reg_add, par_val);
		reg_add += 0x20;
	}

	par_h = 0x40; par_l = 0x20;
	par_val = OCEF_PAR_H(par_h) | OCEF_PAR_L(par_l);
	for (reg_add = ISP_REG_OECF_X0_CFG1; reg_add <= ISP_REG_OECF_Y3_CFG1;) {
		stf_isp_reg_write(stfcamss, reg_add, par_val);
		reg_add += 0x20;
	}

	par_h = 0x80; par_l = 0x60;
	par_val = OCEF_PAR_H(par_h) | OCEF_PAR_L(par_l);
	for (reg_add = ISP_REG_OECF_X0_CFG2; reg_add <= ISP_REG_OECF_Y3_CFG2;) {
		stf_isp_reg_write(stfcamss, reg_add, par_val);
		reg_add += 0x20;
	}

	par_h = 0xc0; par_l = 0xa0;
	par_val = OCEF_PAR_H(par_h) | OCEF_PAR_L(par_l);
	for (reg_add = ISP_REG_OECF_X0_CFG3; reg_add <= ISP_REG_OECF_Y3_CFG3;) {
		stf_isp_reg_write(stfcamss, reg_add, par_val);
		reg_add += 0x20;
	}

	par_h = 0x100; par_l = 0xe0;
	par_val = OCEF_PAR_H(par_h) | OCEF_PAR_L(par_l);
	for (reg_add = ISP_REG_OECF_X0_CFG4; reg_add <= ISP_REG_OECF_Y3_CFG4;) {
		stf_isp_reg_write(stfcamss, reg_add, par_val);
		reg_add += 0x20;
	}

	par_h = 0x200; par_l = 0x180;
	par_val = OCEF_PAR_H(par_h) | OCEF_PAR_L(par_l);
	for (reg_add = ISP_REG_OECF_X0_CFG5; reg_add <= ISP_REG_OECF_Y3_CFG5;) {
		stf_isp_reg_write(stfcamss, reg_add, par_val);
		reg_add += 0x20;
	}

	par_h = 0x300; par_l = 0x280;
	par_val = OCEF_PAR_H(par_h) | OCEF_PAR_L(par_l);
	for (reg_add = ISP_REG_OECF_X0_CFG6; reg_add <= ISP_REG_OECF_Y3_CFG6;) {
		stf_isp_reg_write(stfcamss, reg_add, par_val);
		reg_add += 0x20;
	}

	par_h = 0x3fe; par_l = 0x380;
	par_val = OCEF_PAR_H(par_h) | OCEF_PAR_L(par_l);
	for (reg_add = ISP_REG_OECF_X0_CFG7; reg_add <= ISP_REG_OECF_Y3_CFG7;) {
		stf_isp_reg_write(stfcamss, reg_add, par_val);
		reg_add += 0x20;
	}

	par_h = 0x80; par_l = 0x80;
	par_val = OCEF_PAR_H(par_h) | OCEF_PAR_L(par_l);
	for (reg_add = ISP_REG_OECF_S0_CFG0; reg_add <= ISP_REG_OECF_S3_CFG7;) {
		stf_isp_reg_write(stfcamss, reg_add, par_val);
		reg_add += 4;
	}
}

static void stf_isp_config_lccf(struct stfcamss *stfcamss)
{
	u32 reg_add;

	stf_isp_reg_write(stfcamss, ISP_REG_LCCF_CFG_0,
			  Y_DISTANCE(0x21C) | X_DISTANCE(0x3C0));
	stf_isp_reg_write(stfcamss, ISP_REG_LCCF_CFG_1, LCCF_MAX_DIS(0xb));

	for (reg_add = ISP_REG_LCCF_CFG_2; reg_add <= ISP_REG_LCCF_CFG_5;) {
		stf_isp_reg_write(stfcamss, reg_add,
				  LCCF_F2_PAR(0x0) | LCCF_F1_PAR(0x0));
		reg_add += 4;
	}
}

static void stf_isp_config_awb(struct stfcamss *stfcamss)
{
	u32 reg_val, reg_add;
	u16 symbol_h, symbol_l;

	symbol_h = 0x0; symbol_l = 0x0;
	reg_val = AWB_X_SYMBOL_H(symbol_h) | AWB_X_SYMBOL_L(symbol_l);

	for (reg_add = ISP_REG_AWB_X0_CFG_0; reg_add <= ISP_REG_AWB_X3_CFG_1;) {
		stf_isp_reg_write(stfcamss, reg_add, reg_val);
		reg_add += 4;
	}

	symbol_h = 0x0, symbol_l = 0x0;
	reg_val = AWB_Y_SYMBOL_H(symbol_h) | AWB_Y_SYMBOL_L(symbol_l);

	for (reg_add = ISP_REG_AWB_Y0_CFG_0; reg_add <= ISP_REG_AWB_Y3_CFG_1;) {
		stf_isp_reg_write(stfcamss, reg_add, reg_val);
		reg_add += 4;
	}

	symbol_h = 0x80, symbol_l = 0x80;
	reg_val = AWB_S_SYMBOL_H(symbol_h) | AWB_S_SYMBOL_L(symbol_l);

	for (reg_add = ISP_REG_AWB_S0_CFG_0; reg_add <= ISP_REG_AWB_S3_CFG_1;) {
		stf_isp_reg_write(stfcamss, reg_add, reg_val);
		reg_add += 4;
	}
}

static void stf_isp_config_grgb(struct stfcamss *stfcamss)
{
	stf_isp_reg_write(stfcamss, ISP_REG_ICTC,
			  GF_MODE(1) | MAXGT(0x140) | MINGT(0x40));
	stf_isp_reg_write(stfcamss, ISP_REG_IDBC, BADGT(0x200) | BADXT(0x200));
}

static void stf_isp_config_cfa(struct stfcamss *stfcamss)
{
	stf_isp_reg_write(stfcamss, ISP_REG_RAW_FORMAT_CFG,
			  SMY13(0) | SMY12(1) | SMY11(0) | SMY10(1) | SMY3(2) |
			  SMY2(3) | SMY1(2) | SMY0(3));
	stf_isp_reg_write(stfcamss, ISP_REG_ICFAM, CROSS_COV(3) | HV_W(2));
}

static void stf_isp_config_ccm(struct stfcamss *stfcamss)
{
	u32 reg_add;

	stf_isp_reg_write(stfcamss, ISP_REG_ICAMD_0, DNRM_F(6) | CCM_M_DAT(0));

	for (reg_add = ISP_REG_ICAMD_12; reg_add <= ISP_REG_ICAMD_20;) {
		stf_isp_reg_write(stfcamss, reg_add, CCM_M_DAT(0x80));
		reg_add += 0x10;
	}

	stf_isp_reg_write(stfcamss, ISP_REG_ICAMD_24, CCM_M_DAT(0x700));
	stf_isp_reg_write(stfcamss, ISP_REG_ICAMD_25, CCM_M_DAT(0x200));
}

static void stf_isp_config_gamma(struct stfcamss *stfcamss)
{
	u32 reg_val, reg_add;
	u16 gamma_slope_v, gamma_v;

	gamma_slope_v = 0x2400; gamma_v = 0x0;
	reg_val = GAMMA_S_VAL(gamma_slope_v) | GAMMA_VAL(gamma_v);
	stf_isp_reg_write(stfcamss, ISP_REG_GAMMA_VAL0, reg_val);

	gamma_slope_v = 0x800; gamma_v = 0x20;
	for (reg_add = ISP_REG_GAMMA_VAL1; reg_add <= ISP_REG_GAMMA_VAL7;) {
		reg_val = GAMMA_S_VAL(gamma_slope_v) | GAMMA_VAL(gamma_v);
		stf_isp_reg_write(stfcamss, reg_add, reg_val);
		reg_add += 4;
		gamma_v += 0x20;
	}

	gamma_v = 0x100;
	for (reg_add = ISP_REG_GAMMA_VAL8; reg_add <= ISP_REG_GAMMA_VAL13;) {
		reg_val = GAMMA_S_VAL(gamma_slope_v) | GAMMA_VAL(gamma_v);
		stf_isp_reg_write(stfcamss, reg_add, reg_val);
		reg_add += 4;
		gamma_v += 0x80;
	}

	gamma_v = 0x3fe;
	reg_val = GAMMA_S_VAL(gamma_slope_v) | GAMMA_VAL(gamma_v);
	stf_isp_reg_write(stfcamss, ISP_REG_GAMMA_VAL14, reg_val);
}

static void stf_isp_config_r2y(struct stfcamss *stfcamss)
{
	stf_isp_reg_write(stfcamss, ISP_REG_R2Y_0, 0x4C);
	stf_isp_reg_write(stfcamss, ISP_REG_R2Y_1, 0x97);
	stf_isp_reg_write(stfcamss, ISP_REG_R2Y_2, 0x1d);
	stf_isp_reg_write(stfcamss, ISP_REG_R2Y_3, 0x1d5);
	stf_isp_reg_write(stfcamss, ISP_REG_R2Y_4, 0x1ac);
	stf_isp_reg_write(stfcamss, ISP_REG_R2Y_5, 0x80);
	stf_isp_reg_write(stfcamss, ISP_REG_R2Y_6, 0x80);
	stf_isp_reg_write(stfcamss, ISP_REG_R2Y_7, 0x194);
	stf_isp_reg_write(stfcamss, ISP_REG_R2Y_8, 0x1ec);
}

static void stf_isp_config_y_curve(struct stfcamss *stfcamss)
{
	u32 reg_add;
	u16 y_curve;

	y_curve = 0x0;
	for (reg_add = ISP_REG_YCURVE_0; reg_add <= ISP_REG_YCURVE_63;) {
		stf_isp_reg_write(stfcamss, reg_add, y_curve);
		reg_add += 4;
		y_curve += 0x10;
	}
}

static void stf_isp_config_sharpen(struct stfcamss *sc)
{
	u32 reg_add;

	stf_isp_reg_write(sc, ISP_REG_SHARPEN0, S_DELTA(0x7) | S_WEIGHT(0xf));
	stf_isp_reg_write(sc, ISP_REG_SHARPEN1, S_DELTA(0x18) | S_WEIGHT(0xf));
	stf_isp_reg_write(sc, ISP_REG_SHARPEN2, S_DELTA(0x80) | S_WEIGHT(0xf));
	stf_isp_reg_write(sc, ISP_REG_SHARPEN3, S_DELTA(0x100) | S_WEIGHT(0xf));
	stf_isp_reg_write(sc, ISP_REG_SHARPEN4, S_DELTA(0x10) | S_WEIGHT(0xf));
	stf_isp_reg_write(sc, ISP_REG_SHARPEN5, S_DELTA(0x60) | S_WEIGHT(0xf));
	stf_isp_reg_write(sc, ISP_REG_SHARPEN6, S_DELTA(0x100) | S_WEIGHT(0xf));
	stf_isp_reg_write(sc, ISP_REG_SHARPEN7, S_DELTA(0x190) | S_WEIGHT(0xf));
	stf_isp_reg_write(sc, ISP_REG_SHARPEN8, S_DELTA(0x0) | S_WEIGHT(0xf));

	for (reg_add = ISP_REG_SHARPEN9; reg_add <= ISP_REG_SHARPEN14;) {
		stf_isp_reg_write(sc, reg_add, S_WEIGHT(0xf));
		reg_add += 4;
	}

	for (reg_add = ISP_REG_SHARPEN_FS0; reg_add <= ISP_REG_SHARPEN_FS5;) {
		stf_isp_reg_write(sc, reg_add, S_FACTOR(0x10) | S_SLOPE(0x0));
		reg_add += 4;
	}

	stf_isp_reg_write(sc, ISP_REG_SHARPEN_WN,
			  PDIRF(0x8) | NDIRF(0x8) | WSUM(0xd7c));
	stf_isp_reg_write(sc, ISP_REG_IUVS1, UVDIFF2(0xC0) | UVDIFF1(0x40));
	stf_isp_reg_write(sc, ISP_REG_IUVS2, UVF(0xff) | UVSLOPE(0x0));
	stf_isp_reg_write(sc, ISP_REG_IUVCKS1,
			  UVCKDIFF2(0xa0) | UVCKDIFF1(0x40));
}

static void stf_isp_config_dnyuv(struct stfcamss *stfcamss)
{
	u32 reg_val;

	reg_val = YUVSW5(7) | YUVSW4(7) | YUVSW3(7) | YUVSW2(7) |
		  YUVSW1(7) | YUVSW0(7);
	stf_isp_reg_write(stfcamss, ISP_REG_DNYUV_YSWR0, reg_val);
	stf_isp_reg_write(stfcamss, ISP_REG_DNYUV_CSWR0, reg_val);

	reg_val = YUVSW3(7) | YUVSW2(7) | YUVSW1(7) | YUVSW0(7);
	stf_isp_reg_write(stfcamss, ISP_REG_DNYUV_YSWR1, reg_val);
	stf_isp_reg_write(stfcamss, ISP_REG_DNYUV_CSWR1, reg_val);

	reg_val = CURVE_D_H(0x60) | CURVE_D_L(0x40);
	stf_isp_reg_write(stfcamss, ISP_REG_DNYUV_YDR0, reg_val);
	stf_isp_reg_write(stfcamss, ISP_REG_DNYUV_CDR0, reg_val);

	reg_val = CURVE_D_H(0xd8) | CURVE_D_L(0x90);
	stf_isp_reg_write(stfcamss, ISP_REG_DNYUV_YDR1, reg_val);
	stf_isp_reg_write(stfcamss, ISP_REG_DNYUV_CDR1, reg_val);

	reg_val = CURVE_D_H(0x1e6) | CURVE_D_L(0x144);
	stf_isp_reg_write(stfcamss, ISP_REG_DNYUV_YDR2, reg_val);
	stf_isp_reg_write(stfcamss, ISP_REG_DNYUV_CDR2, reg_val);
}

static void stf_isp_config_sat(struct stfcamss *stfcamss)
{
	stf_isp_reg_write(stfcamss, ISP_REG_CS_GAIN, CMAD(0x0) | CMAB(0x100));
	stf_isp_reg_write(stfcamss, ISP_REG_CS_THRESHOLD, CMD(0x1f) | CMB(0x1));
	stf_isp_reg_write(stfcamss, ISP_REG_CS_OFFSET, VOFF(0x0) | UOFF(0x0));
	stf_isp_reg_write(stfcamss, ISP_REG_CS_HUE_F, SIN(0x0) | COS(0x100));
	stf_isp_reg_write(stfcamss, ISP_REG_CS_SCALE, 0x8);
	stf_isp_reg_write(stfcamss, ISP_REG_YADJ0, YOIR(0x401) | YIMIN(0x1));
	stf_isp_reg_write(stfcamss, ISP_REG_YADJ1, YOMAX(0x3ff) | YOMIN(0x1));
}

int stf_isp_reset(struct stf_isp_dev *isp_dev)
{
	stf_isp_reg_set_bit(isp_dev->stfcamss, ISP_REG_ISP_CTRL_0,
			    ISPC_RST_MASK, ISPC_RST);
	stf_isp_reg_set_bit(isp_dev->stfcamss, ISP_REG_ISP_CTRL_0,
			    ISPC_RST_MASK, 0);

	return 0;
}

void stf_isp_init_cfg(struct stf_isp_dev *isp_dev)
{
	stf_isp_reg_write(isp_dev->stfcamss, ISP_REG_DC_CFG_1, DC_AXI_ID(0x0));
	stf_isp_reg_write(isp_dev->stfcamss, ISP_REG_DEC_CFG,
			  DEC_V_KEEP(0x0) |
			  DEC_V_PERIOD(0x0) |
			  DEC_H_KEEP(0x0) |
			  DEC_H_PERIOD(0x0));

	stf_isp_config_obc(isp_dev->stfcamss);
	stf_isp_config_oecf(isp_dev->stfcamss);
	stf_isp_config_lccf(isp_dev->stfcamss);
	stf_isp_config_awb(isp_dev->stfcamss);
	stf_isp_config_grgb(isp_dev->stfcamss);
	stf_isp_config_cfa(isp_dev->stfcamss);
	stf_isp_config_ccm(isp_dev->stfcamss);
	stf_isp_config_gamma(isp_dev->stfcamss);
	stf_isp_config_r2y(isp_dev->stfcamss);
	stf_isp_config_y_curve(isp_dev->stfcamss);
	stf_isp_config_sharpen(isp_dev->stfcamss);
	stf_isp_config_dnyuv(isp_dev->stfcamss);
	stf_isp_config_sat(isp_dev->stfcamss);

	stf_isp_reg_write(isp_dev->stfcamss, ISP_REG_CSI_MODULE_CFG,
			  CSI_DUMP_EN | CSI_SC_EN | CSI_AWB_EN |
			  CSI_LCCF_EN | CSI_OECF_EN | CSI_OBC_EN | CSI_DEC_EN);
	stf_isp_reg_write(isp_dev->stfcamss, ISP_REG_ISP_CTRL_1,
			  CTRL_SAT(1) | CTRL_DBC | CTRL_CTC | CTRL_YHIST |
			  CTRL_YCURVE | CTRL_BIYUV | CTRL_SCE | CTRL_EE |
			  CTRL_CCE | CTRL_RGE | CTRL_CME | CTRL_AE | CTRL_CE);
}

static void stf_isp_config_crop(struct stfcamss *stfcamss,
				struct v4l2_rect *crop)
{
	u32 bpp = stfcamss->isp_dev.current_fmt->bpp;
	u32 val;

	val = VSTART_CAP(crop->top) | HSTART_CAP(crop->left);
	stf_isp_reg_write(stfcamss, ISP_REG_PIC_CAPTURE_START_CFG, val);

	val = VEND_CAP(crop->height + crop->top - 1) |
	      HEND_CAP(crop->width + crop->left - 1);
	stf_isp_reg_write(stfcamss, ISP_REG_PIC_CAPTURE_END_CFG, val);

	val = H_ACT_CAP(crop->height) | W_ACT_CAP(crop->width);
	stf_isp_reg_write(stfcamss, ISP_REG_PIPELINE_XY_SIZE, val);

	val = ALIGN(crop->width * bpp / 8, STFCAMSS_FRAME_WIDTH_ALIGN_8);
	stf_isp_reg_write(stfcamss, ISP_REG_STRIDE, val);
}

static void stf_isp_config_raw_fmt(struct stfcamss *stfcamss, u32 mcode)
{
	u32 val, val1;

	switch (mcode) {
	case MEDIA_BUS_FMT_SRGGB10_1X10:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
		/* 3 2 3 2 1 0 1 0 B Gb B Gb Gr R Gr R */
		val = SMY13(3) | SMY12(2) | SMY11(3) | SMY10(2) |
		      SMY3(1) | SMY2(0) | SMY1(1) | SMY0(0);
		val1 = CTRL_SAT(0x0);
		break;
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
		/* 2 3 2 3 0 1 0 1, Gb B Gb B R Gr R Gr */
		val = SMY13(2) | SMY12(3) | SMY11(2) | SMY10(3) |
		      SMY3(0) | SMY2(1) | SMY1(0) | SMY0(1);
		val1 = CTRL_SAT(0x2);
		break;
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
		/* 1 0 1 0 3 2 3 2, Gr R Gr R B Gb B Gb */
		val = SMY13(1) | SMY12(0) | SMY11(1) | SMY10(0) |
		      SMY3(3) | SMY2(2) | SMY1(3) | SMY0(2);
		val1 = CTRL_SAT(0x3);
		break;
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SBGGR8_1X8:
		/* 0 1 0 1 2 3 2 3 R Gr R Gr Gb B Gb B */
		val = SMY13(0) | SMY12(1) | SMY11(0) | SMY10(1) |
		      SMY3(2) | SMY2(3) | SMY1(2) | SMY0(3);
		val1 = CTRL_SAT(0x1);
		break;
	default:
		val = SMY13(0) | SMY12(1) | SMY11(0) | SMY10(1) |
		      SMY3(2) | SMY2(3) | SMY1(2) | SMY0(3);
		val1 = CTRL_SAT(0x1);
		break;
	}
	stf_isp_reg_write(stfcamss, ISP_REG_RAW_FORMAT_CFG, val);
	stf_isp_reg_set_bit(stfcamss, ISP_REG_ISP_CTRL_1, CTRL_SAT_MASK, val1);
}

void stf_isp_settings(struct stf_isp_dev *isp_dev,
		      struct v4l2_rect *crop, u32 mcode)
{
	struct stfcamss *stfcamss = isp_dev->stfcamss;

	stf_isp_config_crop(stfcamss, crop);
	stf_isp_config_raw_fmt(stfcamss, mcode);

	stf_isp_reg_set_bit(stfcamss, ISP_REG_DUMP_CFG_1,
			    DUMP_BURST_LEN_MASK | DUMP_SD_MASK,
			    DUMP_BURST_LEN(3));

	stf_isp_reg_write(stfcamss, ISP_REG_ITIIWSR,
			  ITI_HSIZE(IMAGE_MAX_HEIGH) |
			  ITI_WSIZE(IMAGE_MAX_WIDTH));
	stf_isp_reg_write(stfcamss, ISP_REG_ITIDWLSR, 0x960);
	stf_isp_reg_write(stfcamss, ISP_REG_ITIDRLSR, 0x960);
	stf_isp_reg_write(stfcamss, ISP_REG_SENSOR, IMAGER_SEL(1));
}

void stf_isp_stream_set(struct stf_isp_dev *isp_dev)
{
	struct stfcamss *stfcamss = isp_dev->stfcamss;

	stf_isp_reg_write_delay(stfcamss, ISP_REG_ISP_CTRL_0,
				ISPC_ENUO | ISPC_ENLS | ISPC_RST, 10);
	stf_isp_reg_write_delay(stfcamss, ISP_REG_ISP_CTRL_0,
				ISPC_ENUO | ISPC_ENLS, 10);
	stf_isp_reg_write(stfcamss, ISP_REG_IESHD, SHAD_UP_M);
	stf_isp_reg_write_delay(stfcamss, ISP_REG_ISP_CTRL_0,
				ISPC_ENUO | ISPC_ENLS | ISPC_EN, 10);
	stf_isp_reg_write_delay(stfcamss, ISP_REG_CSIINTS,
				CSI_INTS(1) | CSI_SHA_M(4), 10);
	stf_isp_reg_write_delay(stfcamss, ISP_REG_CSIINTS,
				CSI_INTS(2) | CSI_SHA_M(4), 10);
	stf_isp_reg_write_delay(stfcamss, ISP_REG_CSI_INPUT_EN_AND_STATUS,
				CSI_EN_S, 10);
}
