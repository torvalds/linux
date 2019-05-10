// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Contributors:
 *      Manjunath Hadli <manjunath.hadli@ti.com>
 *      Prabhakar Lad <prabhakar.lad@ti.com>
 */

#include "dm365_ipipe_hw.h"

#define IPIPE_MODE_CONTINUOUS		0
#define IPIPE_MODE_SINGLE_SHOT		1

static void ipipe_clock_enable(void __iomem *base_addr)
{
	/* enable IPIPE MMR for register write access */
	regw_ip(base_addr, IPIPE_GCK_MMR_DEFAULT, IPIPE_GCK_MMR);

	/* enable the clock wb,cfa,dfc,d2f,pre modules */
	regw_ip(base_addr, IPIPE_GCK_PIX_DEFAULT, IPIPE_GCK_PIX);
}

static void
rsz_set_common_params(void __iomem *rsz_base, struct resizer_params *params)
{
	struct rsz_common_params *rsz_common = &params->rsz_common;
	u32 val;

	/* Set mode */
	regw_rsz(rsz_base, params->oper_mode, RSZ_SRC_MODE);

	/* data source selection  and bypass */
	val = (rsz_common->passthrough << RSZ_BYPASS_SHIFT) |
	      rsz_common->source;
	regw_rsz(rsz_base, val, RSZ_SRC_FMT0);

	/* src image selection */
	val = (rsz_common->raw_flip & 1) |
	      (rsz_common->src_img_fmt << RSZ_SRC_IMG_FMT_SHIFT) |
	      ((rsz_common->y_c & 1) << RSZ_SRC_Y_C_SEL_SHIFT);
	regw_rsz(rsz_base, val, RSZ_SRC_FMT1);

	regw_rsz(rsz_base, rsz_common->vps & IPIPE_RSZ_VPS_MASK, RSZ_SRC_VPS);
	regw_rsz(rsz_base, rsz_common->hps & IPIPE_RSZ_HPS_MASK, RSZ_SRC_HPS);
	regw_rsz(rsz_base, rsz_common->vsz & IPIPE_RSZ_VSZ_MASK, RSZ_SRC_VSZ);
	regw_rsz(rsz_base, rsz_common->hsz & IPIPE_RSZ_HSZ_MASK, RSZ_SRC_HSZ);
	regw_rsz(rsz_base, rsz_common->yuv_y_min, RSZ_YUV_Y_MIN);
	regw_rsz(rsz_base, rsz_common->yuv_y_max, RSZ_YUV_Y_MAX);
	regw_rsz(rsz_base, rsz_common->yuv_c_min, RSZ_YUV_C_MIN);
	regw_rsz(rsz_base, rsz_common->yuv_c_max, RSZ_YUV_C_MAX);
	/* chromatic position */
	regw_rsz(rsz_base, rsz_common->out_chr_pos, RSZ_YUV_PHS);
}

static void
rsz_set_rsz_regs(void __iomem *rsz_base, unsigned int rsz_id,
		 struct resizer_params *params)
{
	struct resizer_scale_param *rsc_params;
	struct rsz_ext_mem_param *ext_mem;
	struct resizer_rgb *rgb;
	u32 reg_base;
	u32 val;

	rsc_params = &params->rsz_rsc_param[rsz_id];
	rgb = &params->rsz2rgb[rsz_id];
	ext_mem = &params->ext_mem_param[rsz_id];

	if (rsz_id == RSZ_A) {
		val = rsc_params->h_flip << RSZA_H_FLIP_SHIFT;
		val |= rsc_params->v_flip << RSZA_V_FLIP_SHIFT;
		reg_base = RSZ_EN_A;
	} else {
		val = rsc_params->h_flip << RSZB_H_FLIP_SHIFT;
		val |= rsc_params->v_flip << RSZB_V_FLIP_SHIFT;
		reg_base = RSZ_EN_B;
	}
	/* update flip settings */
	regw_rsz(rsz_base, val, RSZ_SEQ);

	regw_rsz(rsz_base, params->oper_mode, reg_base + RSZ_MODE);

	val = (rsc_params->cen << RSZ_CEN_SHIFT) | rsc_params->yen;
	regw_rsz(rsz_base, val, reg_base + RSZ_420);

	regw_rsz(rsz_base, rsc_params->i_vps & RSZ_VPS_MASK,
		 reg_base + RSZ_I_VPS);
	regw_rsz(rsz_base, rsc_params->i_hps & RSZ_HPS_MASK,
		 reg_base + RSZ_I_HPS);
	regw_rsz(rsz_base, rsc_params->o_vsz & RSZ_O_VSZ_MASK,
		 reg_base + RSZ_O_VSZ);
	regw_rsz(rsz_base, rsc_params->o_hsz & RSZ_O_HSZ_MASK,
		 reg_base + RSZ_O_HSZ);
	regw_rsz(rsz_base, rsc_params->v_phs_y & RSZ_V_PHS_MASK,
		 reg_base + RSZ_V_PHS_Y);
	regw_rsz(rsz_base, rsc_params->v_phs_c & RSZ_V_PHS_MASK,
		 reg_base + RSZ_V_PHS_C);

	/* keep this additional adjustment to zero for now */
	regw_rsz(rsz_base, rsc_params->v_dif & RSZ_V_DIF_MASK,
		 reg_base + RSZ_V_DIF);

	val = (rsc_params->v_typ_y & 1) |
	      ((rsc_params->v_typ_c & 1) << RSZ_TYP_C_SHIFT);
	regw_rsz(rsz_base, val, reg_base + RSZ_V_TYP);

	val = (rsc_params->v_lpf_int_y & RSZ_LPF_INT_MASK) |
	      ((rsc_params->v_lpf_int_c & RSZ_LPF_INT_MASK) <<
	      RSZ_LPF_INT_C_SHIFT);
	regw_rsz(rsz_base, val, reg_base + RSZ_V_LPF);

	regw_rsz(rsz_base, rsc_params->h_phs &
		RSZ_H_PHS_MASK, reg_base + RSZ_H_PHS);

	regw_rsz(rsz_base, 0, reg_base + RSZ_H_PHS_ADJ);
	regw_rsz(rsz_base, rsc_params->h_dif &
		RSZ_H_DIF_MASK, reg_base + RSZ_H_DIF);

	val = (rsc_params->h_typ_y & 1) |
	      ((rsc_params->h_typ_c & 1) << RSZ_TYP_C_SHIFT);
	regw_rsz(rsz_base, val, reg_base + RSZ_H_TYP);

	val = (rsc_params->h_lpf_int_y & RSZ_LPF_INT_MASK) |
		 ((rsc_params->h_lpf_int_c & RSZ_LPF_INT_MASK) <<
		 RSZ_LPF_INT_C_SHIFT);
	regw_rsz(rsz_base, val, reg_base + RSZ_H_LPF);

	regw_rsz(rsz_base, rsc_params->dscale_en & 1, reg_base + RSZ_DWN_EN);

	val = (rsc_params->h_dscale_ave_sz & RSZ_DWN_SCALE_AV_SZ_MASK) |
	      ((rsc_params->v_dscale_ave_sz & RSZ_DWN_SCALE_AV_SZ_MASK) <<
	      RSZ_DWN_SCALE_AV_SZ_V_SHIFT);
	regw_rsz(rsz_base, val, reg_base + RSZ_DWN_AV);

	/* setting rgb conversion parameters */
	regw_rsz(rsz_base, rgb->rgb_en, reg_base + RSZ_RGB_EN);

	val = (rgb->rgb_typ << RSZ_RGB_TYP_SHIFT) |
	      (rgb->rgb_msk0 << RSZ_RGB_MSK0_SHIFT) |
	      (rgb->rgb_msk1 << RSZ_RGB_MSK1_SHIFT);
	regw_rsz(rsz_base, val, reg_base + RSZ_RGB_TYP);

	regw_rsz(rsz_base, rgb->rgb_alpha_val & RSZ_RGB_ALPHA_MASK,
		reg_base + RSZ_RGB_BLD);

	/* setting external memory parameters */
	regw_rsz(rsz_base, ext_mem->rsz_sdr_oft_y, reg_base + RSZ_SDR_Y_OFT);
	regw_rsz(rsz_base, ext_mem->rsz_sdr_ptr_s_y,
		 reg_base + RSZ_SDR_Y_PTR_S);
	regw_rsz(rsz_base, ext_mem->rsz_sdr_ptr_e_y,
		 reg_base + RSZ_SDR_Y_PTR_E);
	regw_rsz(rsz_base, ext_mem->rsz_sdr_oft_c, reg_base + RSZ_SDR_C_OFT);
	regw_rsz(rsz_base, ext_mem->rsz_sdr_ptr_s_c,
		 reg_base + RSZ_SDR_C_PTR_S);
	regw_rsz(rsz_base, (ext_mem->rsz_sdr_ptr_e_c >> 1),
		 reg_base + RSZ_SDR_C_PTR_E);
}

/*set the registers of either RSZ0 or RSZ1 */
static void
ipipe_setup_resizer(void __iomem *rsz_base, struct resizer_params *params)
{
	/* enable MMR gate to write to Resizer */
	regw_rsz(rsz_base, 1, RSZ_GCK_MMR);

	/* Enable resizer if it is not in bypass mode */
	if (params->rsz_common.passthrough)
		regw_rsz(rsz_base, 0, RSZ_GCK_SDR);
	else
		regw_rsz(rsz_base, 1, RSZ_GCK_SDR);

	rsz_set_common_params(rsz_base, params);

	regw_rsz(rsz_base, params->rsz_en[RSZ_A], RSZ_EN_A);

	if (params->rsz_en[RSZ_A])
		/*setting rescale parameters */
		rsz_set_rsz_regs(rsz_base, RSZ_A, params);

	regw_rsz(rsz_base, params->rsz_en[RSZ_B], RSZ_EN_B);

	if (params->rsz_en[RSZ_B])
		rsz_set_rsz_regs(rsz_base, RSZ_B, params);
}

static u32 ipipe_get_color_pat(u32 pix)
{
	switch (pix) {
	case MEDIA_BUS_FMT_SGRBG10_ALAW8_1X8:
	case MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
		return ipipe_sgrbg_pattern;

	default:
		return ipipe_srggb_pattern;
	}
}

static int ipipe_get_data_path(struct vpfe_ipipe_device *ipipe)
{
	u32 temp_pix_fmt;

	switch (ipipe->formats[IPIPE_PAD_SINK].code) {
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SGRBG10_ALAW8_1X8:
	case MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
		temp_pix_fmt = MEDIA_BUS_FMT_SGRBG12_1X12;
		break;

	default:
		temp_pix_fmt = MEDIA_BUS_FMT_UYVY8_2X8;
	}

	if (temp_pix_fmt == MEDIA_BUS_FMT_SGRBG12_1X12) {
		if (ipipe->formats[IPIPE_PAD_SOURCE].code ==
			MEDIA_BUS_FMT_SGRBG12_1X12)
			return IPIPE_RAW2RAW;
		return IPIPE_RAW2YUV;
	}

	return IPIPE_YUV2YUV;
}

static int get_ipipe_mode(struct vpfe_ipipe_device *ipipe)
{
	struct vpfe_device *vpfe_dev = to_vpfe_device(ipipe);
	u16 ipipeif_sink = vpfe_dev->vpfe_ipipeif.input;

	if (ipipeif_sink == IPIPEIF_INPUT_MEMORY)
		return IPIPE_MODE_SINGLE_SHOT;
	if (ipipeif_sink == IPIPEIF_INPUT_ISIF)
		return IPIPE_MODE_CONTINUOUS;

	return -EINVAL;
}

int config_ipipe_hw(struct vpfe_ipipe_device *ipipe)
{
	struct vpfe_ipipe_input_config *config = &ipipe->config.input_config;
	void __iomem *ipipe_base = ipipe->base_addr;
	struct v4l2_mbus_framefmt *outformat;
	u32 color_pat;
	int ipipe_mode;
	u32 data_path;

	/* enable clock to IPIPE */
	vpss_enable_clock(VPSS_IPIPE_CLOCK, 1);
	ipipe_clock_enable(ipipe_base);

	if (ipipe->input == IPIPE_INPUT_NONE) {
		regw_ip(ipipe_base, 0, IPIPE_SRC_EN);
		return 0;
	}

	ipipe_mode = get_ipipe_mode(ipipe);
	if (ipipe_mode < 0) {
		pr_err("Failed to get ipipe mode");
		return -EINVAL;
	}
	regw_ip(ipipe_base, ipipe_mode, IPIPE_SRC_MODE);

	data_path = ipipe_get_data_path(ipipe);
	regw_ip(ipipe_base, data_path, IPIPE_SRC_FMT);

	regw_ip(ipipe_base, config->vst & IPIPE_RSZ_VPS_MASK, IPIPE_SRC_VPS);
	regw_ip(ipipe_base, config->hst & IPIPE_RSZ_HPS_MASK, IPIPE_SRC_HPS);

	outformat = &ipipe->formats[IPIPE_PAD_SOURCE];
	regw_ip(ipipe_base, (outformat->height + 1) & IPIPE_RSZ_VSZ_MASK,
		IPIPE_SRC_VSZ);
	regw_ip(ipipe_base, (outformat->width + 1) & IPIPE_RSZ_HSZ_MASK,
		IPIPE_SRC_HSZ);

	if (data_path == IPIPE_RAW2YUV ||
	    data_path == IPIPE_RAW2RAW) {
		color_pat =
		ipipe_get_color_pat(ipipe->formats[IPIPE_PAD_SINK].code);
		regw_ip(ipipe_base, color_pat, IPIPE_SRC_COL);
	}

	return 0;
}

/*
 * config_rsz_hw() - Performs hardware setup of resizer.
 */
int config_rsz_hw(struct vpfe_resizer_device *resizer,
		  struct resizer_params *config)
{
	struct vpfe_device *vpfe_dev = to_vpfe_device(resizer);
	void __iomem *ipipe_base = vpfe_dev->vpfe_ipipe.base_addr;
	void __iomem *rsz_base = vpfe_dev->vpfe_resizer.base_addr;

	/* enable VPSS clock */
	vpss_enable_clock(VPSS_IPIPE_CLOCK, 1);
	ipipe_clock_enable(ipipe_base);

	ipipe_setup_resizer(rsz_base, config);

	return 0;
}

static void
rsz_set_y_address(void __iomem *rsz_base, unsigned int address,
		  unsigned int offset)
{
	u32 val;

	val = address & SET_LOW_ADDR;
	regw_rsz(rsz_base, val, offset + RSZ_SDR_Y_BAD_L);
	regw_rsz(rsz_base, val, offset + RSZ_SDR_Y_SAD_L);

	val = (address & SET_HIGH_ADDR) >> 16;
	regw_rsz(rsz_base, val, offset + RSZ_SDR_Y_BAD_H);
	regw_rsz(rsz_base, val, offset + RSZ_SDR_Y_SAD_H);
}

static void
rsz_set_c_address(void __iomem *rsz_base, unsigned int address,
		  unsigned int offset)
{
	u32 val;

	val = address & SET_LOW_ADDR;
	regw_rsz(rsz_base, val, offset + RSZ_SDR_C_BAD_L);
	regw_rsz(rsz_base, val, offset + RSZ_SDR_C_SAD_L);

	val = (address & SET_HIGH_ADDR) >> 16;
	regw_rsz(rsz_base, val, offset + RSZ_SDR_C_BAD_H);
	regw_rsz(rsz_base, val, offset + RSZ_SDR_C_SAD_H);
}

/*
 * resizer_set_outaddr() - set the address for given resize_no
 * @rsz_base: resizer base address
 * @params: pointer to ipipe_params structure
 * @resize_no: 0 - Resizer-A, 1 - Resizer B
 * @address: the address to set
 */
int
resizer_set_outaddr(void __iomem *rsz_base, struct resizer_params *params,
		    int resize_no, unsigned int address)
{
	struct resizer_scale_param *rsc_param;
	struct rsz_ext_mem_param *mem_param;
	struct rsz_common_params *rsz_common;
	unsigned int rsz_start_add;
	unsigned int val;

	if (resize_no != RSZ_A && resize_no != RSZ_B)
		return -EINVAL;

	mem_param = &params->ext_mem_param[resize_no];
	rsc_param = &params->rsz_rsc_param[resize_no];
	rsz_common = &params->rsz_common;

	if (resize_no == RSZ_A)
		rsz_start_add = RSZ_EN_A;
	else
		rsz_start_add = RSZ_EN_B;

	/* y_c = 0 for y, = 1 for c */
	if (rsz_common->src_img_fmt == RSZ_IMG_420) {
		if (rsz_common->y_c) {
			/* C channel */
			val = address + mem_param->flip_ofst_c;
			rsz_set_c_address(rsz_base, val, rsz_start_add);
		} else {
			val = address + mem_param->flip_ofst_y;
			rsz_set_y_address(rsz_base, val, rsz_start_add);
		}
	} else {
		if (rsc_param->cen && rsc_param->yen) {
			/* 420 */
			val = address + mem_param->c_offset +
			      mem_param->flip_ofst_c +
			      mem_param->user_y_ofst +
			      mem_param->user_c_ofst;
			if (resize_no == RSZ_B)
				val +=
				params->ext_mem_param[RSZ_A].user_y_ofst +
				params->ext_mem_param[RSZ_A].user_c_ofst;
			/* set C address */
			rsz_set_c_address(rsz_base, val, rsz_start_add);
		}
		val = address + mem_param->flip_ofst_y + mem_param->user_y_ofst;
		if (resize_no == RSZ_B)
			val += params->ext_mem_param[RSZ_A].user_y_ofst +
				params->ext_mem_param[RSZ_A].user_c_ofst;
		/* set Y address */
		rsz_set_y_address(rsz_base, val, rsz_start_add);
	}
	/* resizer must be enabled */
	regw_rsz(rsz_base, params->rsz_en[resize_no], rsz_start_add);

	return 0;
}

void
ipipe_set_lutdpc_regs(void __iomem *base_addr, void __iomem *isp5_base_addr,
		      struct vpfe_ipipe_lutdpc *dpc)
{
	u32 max_tbl_size = LUT_DPC_MAX_SIZE >> 1;
	u32 lut_start_addr = DPC_TB0_START_ADDR;
	u32 val;
	u32 count;

	ipipe_clock_enable(base_addr);
	regw_ip(base_addr, dpc->en, DPC_LUT_EN);

	if (dpc->en != 1)
		return;

	val = LUTDPC_TBL_256_EN | (dpc->repl_white & 1);
	regw_ip(base_addr, val, DPC_LUT_SEL);
	regw_ip(base_addr, LUT_DPC_START_ADDR, DPC_LUT_ADR);
	regw_ip(base_addr, dpc->dpc_size, DPC_LUT_SIZ & LUT_DPC_SIZE_MASK);

	for (count = 0; count < dpc->dpc_size; count++) {
		if (count >= max_tbl_size)
			lut_start_addr = DPC_TB1_START_ADDR;
		val = (dpc->table[count].horz_pos & LUT_DPC_H_POS_MASK) |
		      ((dpc->table[count].vert_pos & LUT_DPC_V_POS_MASK) <<
			LUT_DPC_V_POS_SHIFT) | (dpc->table[count].method <<
			LUT_DPC_CORR_METH_SHIFT);
		w_ip_table(isp5_base_addr, val, (lut_start_addr +
		((count % max_tbl_size) << 2)));
	}
}

static void
set_dpc_thresholds(void __iomem *base_addr,
		   struct vpfe_ipipe_otfdpc_2_0_cfg *dpc_thr)
{
	regw_ip(base_addr, dpc_thr->corr_thr.r & OTFDPC_DPC2_THR_MASK,
		DPC_OTF_2C_THR_R);
	regw_ip(base_addr, dpc_thr->corr_thr.gr & OTFDPC_DPC2_THR_MASK,
		DPC_OTF_2C_THR_GR);
	regw_ip(base_addr, dpc_thr->corr_thr.gb & OTFDPC_DPC2_THR_MASK,
		DPC_OTF_2C_THR_GB);
	regw_ip(base_addr, dpc_thr->corr_thr.b & OTFDPC_DPC2_THR_MASK,
		DPC_OTF_2C_THR_B);
	regw_ip(base_addr, dpc_thr->det_thr.r & OTFDPC_DPC2_THR_MASK,
		DPC_OTF_2D_THR_R);
	regw_ip(base_addr, dpc_thr->det_thr.gr & OTFDPC_DPC2_THR_MASK,
		DPC_OTF_2D_THR_GR);
	regw_ip(base_addr, dpc_thr->det_thr.gb & OTFDPC_DPC2_THR_MASK,
		DPC_OTF_2D_THR_GB);
	regw_ip(base_addr, dpc_thr->det_thr.b & OTFDPC_DPC2_THR_MASK,
		DPC_OTF_2D_THR_B);
}

void ipipe_set_otfdpc_regs(void __iomem *base_addr,
			   struct vpfe_ipipe_otfdpc *otfdpc)
{
	struct vpfe_ipipe_otfdpc_2_0_cfg *dpc_2_0 = &otfdpc->alg_cfg.dpc_2_0;
	struct vpfe_ipipe_otfdpc_3_0_cfg *dpc_3_0 = &otfdpc->alg_cfg.dpc_3_0;
	u32 val;

	ipipe_clock_enable(base_addr);

	regw_ip(base_addr, (otfdpc->en & 1), DPC_OTF_EN);
	if (!otfdpc->en)
		return;

	/* dpc enabled */
	val = (otfdpc->det_method << OTF_DET_METHOD_SHIFT) | otfdpc->alg;
	regw_ip(base_addr, val, DPC_OTF_TYP);

	if (otfdpc->det_method == VPFE_IPIPE_DPC_OTF_MIN_MAX) {
		/* ALG= 0, TYP = 0, DPC_OTF_2D_THR_[x]=0
		 * DPC_OTF_2C_THR_[x] = Maximum thresohld
		 * MinMax method
		 */
		dpc_2_0->det_thr.r = dpc_2_0->det_thr.gb =
		dpc_2_0->det_thr.gr = dpc_2_0->det_thr.b = 0;
		set_dpc_thresholds(base_addr, dpc_2_0);
		return;
	}
	/* MinMax2 */
	if (otfdpc->alg == VPFE_IPIPE_OTFDPC_2_0) {
		set_dpc_thresholds(base_addr, dpc_2_0);
		return;
	}
	regw_ip(base_addr, dpc_3_0->act_adj_shf &
		OTF_DPC3_0_SHF_MASK, DPC_OTF_3_SHF);
	/* Detection thresholds */
	regw_ip(base_addr, ((dpc_3_0->det_thr & OTF_DPC3_0_THR_MASK) <<
		OTF_DPC3_0_THR_SHIFT), DPC_OTF_3D_THR);
	regw_ip(base_addr, dpc_3_0->det_slp &
		OTF_DPC3_0_SLP_MASK, DPC_OTF_3D_SLP);
	regw_ip(base_addr, dpc_3_0->det_thr_min &
		OTF_DPC3_0_DET_MASK, DPC_OTF_3D_MIN);
	regw_ip(base_addr, dpc_3_0->det_thr_max &
		OTF_DPC3_0_DET_MASK, DPC_OTF_3D_MAX);
	/* Correction thresholds */
	regw_ip(base_addr, ((dpc_3_0->corr_thr & OTF_DPC3_0_THR_MASK) <<
		OTF_DPC3_0_THR_SHIFT), DPC_OTF_3C_THR);
	regw_ip(base_addr, dpc_3_0->corr_slp &
		OTF_DPC3_0_SLP_MASK, DPC_OTF_3C_SLP);
	regw_ip(base_addr, dpc_3_0->corr_thr_min &
		OTF_DPC3_0_CORR_MASK, DPC_OTF_3C_MIN);
	regw_ip(base_addr, dpc_3_0->corr_thr_max &
		OTF_DPC3_0_CORR_MASK, DPC_OTF_3C_MAX);
}

/* 2D Noise filter */
void
ipipe_set_d2f_regs(void __iomem *base_addr, unsigned int id,
		   struct vpfe_ipipe_nf *noise_filter)
{

	u32 offset = D2F_1ST;
	int count;
	u32 val;

	if (id == IPIPE_D2F_2ND)
		offset = D2F_2ND;

	ipipe_clock_enable(base_addr);
	regw_ip(base_addr, noise_filter->en & 1, offset + D2F_EN);
	if (!noise_filter->en)
		return;

	/*noise filter enabled */
	/* Combine all the fields to make D2F_CFG register of IPIPE */
	val = ((noise_filter->spread_val & D2F_SPR_VAL_MASK) <<
		D2F_SPR_VAL_SHIFT) | ((noise_filter->shft_val &
		D2F_SHFT_VAL_MASK) << D2F_SHFT_VAL_SHIFT) |
		(noise_filter->gr_sample_meth << D2F_SAMPLE_METH_SHIFT) |
		((noise_filter->apply_lsc_gain & 1) <<
		D2F_APPLY_LSC_GAIN_SHIFT) | D2F_USE_SPR_REG_VAL;
	regw_ip(base_addr, val, offset + D2F_TYP);

	/* edge detection minimum */
	regw_ip(base_addr, noise_filter->edge_det_min_thr &
		D2F_EDGE_DET_THR_MASK, offset + D2F_EDG_MIN);

	/* edge detection maximum */
	regw_ip(base_addr, noise_filter->edge_det_max_thr &
		D2F_EDGE_DET_THR_MASK, offset + D2F_EDG_MAX);

	for (count = 0; count < VPFE_IPIPE_NF_STR_TABLE_SIZE; count++)
		regw_ip(base_addr,
			(noise_filter->str[count] & D2F_STR_VAL_MASK),
			offset + D2F_STR + count * 4);

	for (count = 0; count < VPFE_IPIPE_NF_THR_TABLE_SIZE; count++)
		regw_ip(base_addr, noise_filter->thr[count] & D2F_THR_VAL_MASK,
			offset + D2F_THR + count * 4);
}

#define IPIPE_U8Q5(decimal, integer) \
	(((decimal & 0x1f) | ((integer & 0x7) << 5)))

/* Green Imbalance Correction */
void ipipe_set_gic_regs(void __iomem *base_addr, struct vpfe_ipipe_gic *gic)
{
	u32 val;

	ipipe_clock_enable(base_addr);
	regw_ip(base_addr, gic->en & 1, GIC_EN);

	if (!gic->en)
		return;

	/*gic enabled */
	val = (gic->wt_fn_type << GIC_TYP_SHIFT) |
	      (gic->thr_sel << GIC_THR_SEL_SHIFT) |
	      ((gic->apply_lsc_gain & 1) << GIC_APPLY_LSC_GAIN_SHIFT);
	regw_ip(base_addr, val, GIC_TYP);

	regw_ip(base_addr, gic->gain & GIC_GAIN_MASK, GIC_GAN);

	if (gic->gic_alg != VPFE_IPIPE_GIC_ALG_ADAPT_GAIN) {
		/* Constant Gain. Set threshold to maximum */
		regw_ip(base_addr, GIC_THR_MASK, GIC_THR);
		return;
	}

	if (gic->thr_sel == VPFE_IPIPE_GIC_THR_REG) {
		regw_ip(base_addr, gic->thr & GIC_THR_MASK, GIC_THR);
		regw_ip(base_addr, gic->slope & GIC_SLOPE_MASK, GIC_SLP);
	} else {
		/* Use NF thresholds */
		val = IPIPE_U8Q5(gic->nf2_thr_gain.decimal,
				gic->nf2_thr_gain.integer);
		regw_ip(base_addr, val, GIC_NFGAN);
	}
}

#define IPIPE_U13Q9(decimal, integer) \
	(((decimal & 0x1ff) | ((integer & 0xf) << 9)))
/* White balance */
void ipipe_set_wb_regs(void __iomem *base_addr, struct vpfe_ipipe_wb *wb)
{
	u32 val;

	ipipe_clock_enable(base_addr);
	/* Ofsets. S12 */
	regw_ip(base_addr, wb->ofst_r & WB_OFFSET_MASK, WB2_OFT_R);
	regw_ip(base_addr, wb->ofst_gr & WB_OFFSET_MASK, WB2_OFT_GR);
	regw_ip(base_addr, wb->ofst_gb & WB_OFFSET_MASK, WB2_OFT_GB);
	regw_ip(base_addr, wb->ofst_b & WB_OFFSET_MASK, WB2_OFT_B);

	/* Gains. U13Q9 */
	val = IPIPE_U13Q9(wb->gain_r.decimal, wb->gain_r.integer);
	regw_ip(base_addr, val, WB2_WGN_R);

	val = IPIPE_U13Q9(wb->gain_gr.decimal, wb->gain_gr.integer);
	regw_ip(base_addr, val, WB2_WGN_GR);

	val = IPIPE_U13Q9(wb->gain_gb.decimal, wb->gain_gb.integer);
	regw_ip(base_addr, val, WB2_WGN_GB);

	val = IPIPE_U13Q9(wb->gain_b.decimal, wb->gain_b.integer);
	regw_ip(base_addr, val, WB2_WGN_B);
}

/* CFA */
void ipipe_set_cfa_regs(void __iomem *base_addr, struct vpfe_ipipe_cfa *cfa)
{
	ipipe_clock_enable(base_addr);

	regw_ip(base_addr, cfa->alg, CFA_MODE);
	regw_ip(base_addr, cfa->hpf_thr_2dir & CFA_HPF_THR_2DIR_MASK,
		CFA_2DIR_HPF_THR);
	regw_ip(base_addr, cfa->hpf_slp_2dir & CFA_HPF_SLOPE_2DIR_MASK,
		CFA_2DIR_HPF_SLP);
	regw_ip(base_addr, cfa->hp_mix_thr_2dir & CFA_HPF_MIX_THR_2DIR_MASK,
		CFA_2DIR_MIX_THR);
	regw_ip(base_addr, cfa->hp_mix_slope_2dir & CFA_HPF_MIX_SLP_2DIR_MASK,
		CFA_2DIR_MIX_SLP);
	regw_ip(base_addr, cfa->dir_thr_2dir & CFA_DIR_THR_2DIR_MASK,
		CFA_2DIR_DIR_THR);
	regw_ip(base_addr, cfa->dir_slope_2dir & CFA_DIR_SLP_2DIR_MASK,
		CFA_2DIR_DIR_SLP);
	regw_ip(base_addr, cfa->nd_wt_2dir & CFA_ND_WT_2DIR_MASK,
		CFA_2DIR_NDWT);
	regw_ip(base_addr, cfa->hue_fract_daa & CFA_DAA_HUE_FRA_MASK,
		CFA_MONO_HUE_FRA);
	regw_ip(base_addr, cfa->edge_thr_daa & CFA_DAA_EDG_THR_MASK,
		CFA_MONO_EDG_THR);
	regw_ip(base_addr, cfa->thr_min_daa & CFA_DAA_THR_MIN_MASK,
		CFA_MONO_THR_MIN);
	regw_ip(base_addr, cfa->thr_slope_daa & CFA_DAA_THR_SLP_MASK,
		CFA_MONO_THR_SLP);
	regw_ip(base_addr, cfa->slope_min_daa & CFA_DAA_SLP_MIN_MASK,
		CFA_MONO_SLP_MIN);
	regw_ip(base_addr, cfa->slope_slope_daa & CFA_DAA_SLP_SLP_MASK,
		CFA_MONO_SLP_SLP);
	regw_ip(base_addr, cfa->lp_wt_daa & CFA_DAA_LP_WT_MASK,
		CFA_MONO_LPWT);
}

void
ipipe_set_rgb2rgb_regs(void __iomem *base_addr, unsigned int id,
		       struct vpfe_ipipe_rgb2rgb *rgb)
{
	u32 offset_mask = RGB2RGB_1_OFST_MASK;
	u32 offset = RGB1_MUL_BASE;
	u32 integ_mask = 0xf;
	u32 val;

	ipipe_clock_enable(base_addr);

	if (id == IPIPE_RGB2RGB_2) {
		/*
		 * For second RGB module, gain integer is 3 bits instead
		 * of 4, offset has 11 bits insread of 13
		 */
		offset = RGB2_MUL_BASE;
		integ_mask = 0x7;
		offset_mask = RGB2RGB_2_OFST_MASK;
	}
	/* Gains */
	val = (rgb->coef_rr.decimal & 0xff) |
		((rgb->coef_rr.integer & integ_mask) << 8);
	regw_ip(base_addr, val, offset + RGB_MUL_RR);
	val = (rgb->coef_gr.decimal & 0xff) |
		((rgb->coef_gr.integer & integ_mask) << 8);
	regw_ip(base_addr, val, offset + RGB_MUL_GR);
	val = (rgb->coef_br.decimal & 0xff) |
		((rgb->coef_br.integer & integ_mask) << 8);
	regw_ip(base_addr, val, offset + RGB_MUL_BR);
	val = (rgb->coef_rg.decimal & 0xff) |
		((rgb->coef_rg.integer & integ_mask) << 8);
	regw_ip(base_addr, val, offset + RGB_MUL_RG);
	val = (rgb->coef_gg.decimal & 0xff) |
		((rgb->coef_gg.integer & integ_mask) << 8);
	regw_ip(base_addr, val, offset + RGB_MUL_GG);
	val = (rgb->coef_bg.decimal & 0xff) |
		((rgb->coef_bg.integer & integ_mask) << 8);
	regw_ip(base_addr, val, offset + RGB_MUL_BG);
	val = (rgb->coef_rb.decimal & 0xff) |
		((rgb->coef_rb.integer & integ_mask) << 8);
	regw_ip(base_addr, val, offset + RGB_MUL_RB);
	val = (rgb->coef_gb.decimal & 0xff) |
		((rgb->coef_gb.integer & integ_mask) << 8);
	regw_ip(base_addr, val, offset + RGB_MUL_GB);
	val = (rgb->coef_bb.decimal & 0xff) |
		((rgb->coef_bb.integer & integ_mask) << 8);
	regw_ip(base_addr, val, offset + RGB_MUL_BB);

	/* Offsets */
	regw_ip(base_addr, rgb->out_ofst_r & offset_mask, offset + RGB_OFT_OR);
	regw_ip(base_addr, rgb->out_ofst_g & offset_mask, offset + RGB_OFT_OG);
	regw_ip(base_addr, rgb->out_ofst_b & offset_mask, offset + RGB_OFT_OB);
}

static void
ipipe_update_gamma_tbl(void __iomem *isp5_base_addr,
	struct vpfe_ipipe_gamma_entry *table, int size, u32 addr)
{
	int count;
	u32 val;

	for (count = 0; count < size; count++) {
		val = table[count].slope & GAMMA_MASK;
		val |= (table[count].offset & GAMMA_MASK) << GAMMA_SHIFT;
		w_ip_table(isp5_base_addr, val, (addr + (count * 4)));
	}
}

void
ipipe_set_gamma_regs(void __iomem *base_addr, void __iomem *isp5_base_addr,
			  struct vpfe_ipipe_gamma *gamma)
{
	int table_size;
	u32 val;

	ipipe_clock_enable(base_addr);
	val = (gamma->bypass_r << GAMMA_BYPR_SHIFT) |
		(gamma->bypass_b << GAMMA_BYPG_SHIFT) |
		(gamma->bypass_g << GAMMA_BYPB_SHIFT) |
		(gamma->tbl_sel << GAMMA_TBL_SEL_SHIFT) |
		(gamma->tbl_size << GAMMA_TBL_SIZE_SHIFT);

	regw_ip(base_addr, val, GMM_CFG);

	if (gamma->tbl_sel != VPFE_IPIPE_GAMMA_TBL_RAM)
		return;

	table_size = gamma->tbl_size;

	if (!gamma->bypass_r)
		ipipe_update_gamma_tbl(isp5_base_addr, gamma->table_r,
			table_size, GAMMA_R_START_ADDR);
	if (!gamma->bypass_b)
		ipipe_update_gamma_tbl(isp5_base_addr, gamma->table_b,
			table_size, GAMMA_B_START_ADDR);
	if (!gamma->bypass_g)
		ipipe_update_gamma_tbl(isp5_base_addr, gamma->table_g,
			table_size, GAMMA_G_START_ADDR);
}

void
ipipe_set_3d_lut_regs(void __iomem *base_addr, void __iomem *isp5_base_addr,
			   struct vpfe_ipipe_3d_lut *lut_3d)
{
	struct vpfe_ipipe_3d_lut_entry *tbl;
	u32 bnk_index;
	u32 tbl_index;
	u32 val;
	u32 i;

	ipipe_clock_enable(base_addr);
	regw_ip(base_addr, lut_3d->en, D3LUT_EN);

	if (!lut_3d->en)
		return;

	/* valid table */
	tbl = lut_3d->table;
	for (i = 0; i < VPFE_IPIPE_MAX_SIZE_3D_LUT; i++) {
		/*
		 * Each entry has 0-9 (B), 10-19 (G) and
		 * 20-29 R values
		 */
		val = tbl[i].b & D3_LUT_ENTRY_MASK;
		val |= (tbl[i].g & D3_LUT_ENTRY_MASK) <<
			 D3_LUT_ENTRY_G_SHIFT;
		val |= (tbl[i].r & D3_LUT_ENTRY_MASK) <<
			 D3_LUT_ENTRY_R_SHIFT;
		bnk_index = i % 4;
		tbl_index = i >> 2;
		tbl_index <<= 2;
		if (bnk_index == 0)
			w_ip_table(isp5_base_addr, val,
				   tbl_index + D3L_TB0_START_ADDR);
		else if (bnk_index == 1)
			w_ip_table(isp5_base_addr, val,
				   tbl_index + D3L_TB1_START_ADDR);
		else if (bnk_index == 2)
			w_ip_table(isp5_base_addr, val,
				   tbl_index + D3L_TB2_START_ADDR);
		else
			w_ip_table(isp5_base_addr, val,
				   tbl_index + D3L_TB3_START_ADDR);
	}
}

/* Lumina adjustments */
void
ipipe_set_lum_adj_regs(void __iomem *base_addr, struct ipipe_lum_adj *lum_adj)
{
	u32 val;

	ipipe_clock_enable(base_addr);

	/* combine fields of YUV_ADJ to set brightness and contrast */
	val = lum_adj->contrast << LUM_ADJ_CONTR_SHIFT |
	      lum_adj->brightness << LUM_ADJ_BRIGHT_SHIFT;
	regw_ip(base_addr, val, YUV_ADJ);
}

#define IPIPE_S12Q8(decimal, integer) \
	(((decimal & 0xff) | ((integer & 0xf) << 8)))

void ipipe_set_rgb2ycbcr_regs(void __iomem *base_addr,
			      struct vpfe_ipipe_rgb2yuv *yuv)
{
	u32 val;

	/* S10Q8 */
	ipipe_clock_enable(base_addr);
	val = IPIPE_S12Q8(yuv->coef_ry.decimal, yuv->coef_ry.integer);
	regw_ip(base_addr, val, YUV_MUL_RY);
	val = IPIPE_S12Q8(yuv->coef_gy.decimal, yuv->coef_gy.integer);
	regw_ip(base_addr, val, YUV_MUL_GY);
	val = IPIPE_S12Q8(yuv->coef_by.decimal, yuv->coef_by.integer);
	regw_ip(base_addr, val, YUV_MUL_BY);
	val = IPIPE_S12Q8(yuv->coef_rcb.decimal, yuv->coef_rcb.integer);
	regw_ip(base_addr, val, YUV_MUL_RCB);
	val = IPIPE_S12Q8(yuv->coef_gcb.decimal, yuv->coef_gcb.integer);
	regw_ip(base_addr, val, YUV_MUL_GCB);
	val = IPIPE_S12Q8(yuv->coef_bcb.decimal, yuv->coef_bcb.integer);
	regw_ip(base_addr, val, YUV_MUL_BCB);
	val = IPIPE_S12Q8(yuv->coef_rcr.decimal, yuv->coef_rcr.integer);
	regw_ip(base_addr, val, YUV_MUL_RCR);
	val = IPIPE_S12Q8(yuv->coef_gcr.decimal, yuv->coef_gcr.integer);
	regw_ip(base_addr, val, YUV_MUL_GCR);
	val = IPIPE_S12Q8(yuv->coef_bcr.decimal, yuv->coef_bcr.integer);
	regw_ip(base_addr, val, YUV_MUL_BCR);
	regw_ip(base_addr, yuv->out_ofst_y & RGB2YCBCR_OFST_MASK, YUV_OFT_Y);
	regw_ip(base_addr, yuv->out_ofst_cb & RGB2YCBCR_OFST_MASK, YUV_OFT_CB);
	regw_ip(base_addr, yuv->out_ofst_cr & RGB2YCBCR_OFST_MASK, YUV_OFT_CR);
}

/* YUV 422 conversion */
void
ipipe_set_yuv422_conv_regs(void __iomem *base_addr,
			   struct vpfe_ipipe_yuv422_conv *conv)
{
	u32 val;

	ipipe_clock_enable(base_addr);

	/* Combine all the fields to make YUV_PHS register of IPIPE */
	val = (conv->chrom_pos << 0) | (conv->en_chrom_lpf << 1);
	regw_ip(base_addr, val, YUV_PHS);
}

void
ipipe_set_gbce_regs(void __iomem *base_addr, void __iomem *isp5_base_addr,
		    struct vpfe_ipipe_gbce *gbce)
{
	unsigned int count;
	u32 mask = GBCE_Y_VAL_MASK;

	if (gbce->type == VPFE_IPIPE_GBCE_GAIN_TBL)
		mask = GBCE_GAIN_VAL_MASK;

	ipipe_clock_enable(base_addr);
	regw_ip(base_addr, gbce->en & 1, GBCE_EN);

	if (!gbce->en)
		return;

	regw_ip(base_addr, gbce->type, GBCE_TYP);

	for (count = 0; count < VPFE_IPIPE_MAX_SIZE_GBCE_LUT; count += 2)
		w_ip_table(isp5_base_addr, ((gbce->table[count + 1] & mask) <<
		GBCE_ENTRY_SHIFT) | (gbce->table[count] & mask),
		((count/2) << 2) + GBCE_TB_START_ADDR);
}

void
ipipe_set_ee_regs(void __iomem *base_addr, void __iomem *isp5_base_addr,
		  struct vpfe_ipipe_yee *ee)
{
	unsigned int count;
	u32 val;

	ipipe_clock_enable(base_addr);
	regw_ip(base_addr, ee->en, YEE_EN);

	if (!ee->en)
		return;

	val = ee->en_halo_red & 1;
	val |= ee->merge_meth << YEE_HALO_RED_EN_SHIFT;
	regw_ip(base_addr, val, YEE_TYP);

	regw_ip(base_addr, ee->hpf_shft, YEE_SHF);
	regw_ip(base_addr, ee->hpf_coef_00 & YEE_COEF_MASK, YEE_MUL_00);
	regw_ip(base_addr, ee->hpf_coef_01 & YEE_COEF_MASK, YEE_MUL_01);
	regw_ip(base_addr, ee->hpf_coef_02 & YEE_COEF_MASK, YEE_MUL_02);
	regw_ip(base_addr, ee->hpf_coef_10 & YEE_COEF_MASK, YEE_MUL_10);
	regw_ip(base_addr, ee->hpf_coef_11 & YEE_COEF_MASK, YEE_MUL_11);
	regw_ip(base_addr, ee->hpf_coef_12 & YEE_COEF_MASK, YEE_MUL_12);
	regw_ip(base_addr, ee->hpf_coef_20 & YEE_COEF_MASK, YEE_MUL_20);
	regw_ip(base_addr, ee->hpf_coef_21 & YEE_COEF_MASK, YEE_MUL_21);
	regw_ip(base_addr, ee->hpf_coef_22 & YEE_COEF_MASK, YEE_MUL_22);
	regw_ip(base_addr, ee->yee_thr & YEE_THR_MASK, YEE_THR);
	regw_ip(base_addr, ee->es_gain & YEE_ES_GAIN_MASK, YEE_E_GAN);
	regw_ip(base_addr, ee->es_thr1 & YEE_ES_THR1_MASK, YEE_E_THR1);
	regw_ip(base_addr, ee->es_thr2 & YEE_THR_MASK, YEE_E_THR2);
	regw_ip(base_addr, ee->es_gain_grad & YEE_THR_MASK, YEE_G_GAN);
	regw_ip(base_addr, ee->es_ofst_grad & YEE_THR_MASK, YEE_G_OFT);

	for (count = 0; count < VPFE_IPIPE_MAX_SIZE_YEE_LUT; count += 2)
		w_ip_table(isp5_base_addr, ((ee->table[count + 1] &
		YEE_ENTRY_MASK) << YEE_ENTRY_SHIFT) |
		(ee->table[count] & YEE_ENTRY_MASK),
		((count/2) << 2) + YEE_TB_START_ADDR);
}

/* Chromatic Artifact Correction. CAR */
static void ipipe_set_mf(void __iomem *base_addr)
{
	/* typ to dynamic switch */
	regw_ip(base_addr, VPFE_IPIPE_CAR_DYN_SWITCH, CAR_TYP);
	/* Set SW0 to maximum */
	regw_ip(base_addr, CAR_MF_THR, CAR_SW);
}

static void
ipipe_set_gain_ctrl(void __iomem *base_addr, struct vpfe_ipipe_car *car)
{
	regw_ip(base_addr, VPFE_IPIPE_CAR_CHR_GAIN_CTRL, CAR_TYP);
	regw_ip(base_addr, car->hpf, CAR_HPF_TYP);
	regw_ip(base_addr, car->hpf_shft & CAR_HPF_SHIFT_MASK, CAR_HPF_SHF);
	regw_ip(base_addr, car->hpf_thr, CAR_HPF_THR);
	regw_ip(base_addr, car->gain1.gain, CAR_GN1_GAN);
	regw_ip(base_addr, car->gain1.shft & CAR_GAIN1_SHFT_MASK, CAR_GN1_SHF);
	regw_ip(base_addr, car->gain1.gain_min & CAR_GAIN_MIN_MASK,
		CAR_GN1_MIN);
	regw_ip(base_addr, car->gain2.gain, CAR_GN2_GAN);
	regw_ip(base_addr, car->gain2.shft & CAR_GAIN2_SHFT_MASK, CAR_GN2_SHF);
	regw_ip(base_addr, car->gain2.gain_min & CAR_GAIN_MIN_MASK,
		CAR_GN2_MIN);
}

void ipipe_set_car_regs(void __iomem *base_addr, struct vpfe_ipipe_car *car)
{
	u32 val;

	ipipe_clock_enable(base_addr);
	regw_ip(base_addr, car->en, CAR_EN);

	if (!car->en)
		return;

	switch (car->meth) {
	case VPFE_IPIPE_CAR_MED_FLTR:
		ipipe_set_mf(base_addr);
		break;

	case VPFE_IPIPE_CAR_CHR_GAIN_CTRL:
		ipipe_set_gain_ctrl(base_addr, car);
		break;

	default:
		/* Dynamic switch between MF and Gain Ctrl. */
		ipipe_set_mf(base_addr);
		ipipe_set_gain_ctrl(base_addr, car);
		/* Set the threshold for switching between
		 * the two Here we overwrite the MF SW0 value
		 */
		regw_ip(base_addr, VPFE_IPIPE_CAR_DYN_SWITCH, CAR_TYP);
		val = car->sw1;
		val <<= CAR_SW1_SHIFT;
		val |= car->sw0;
		regw_ip(base_addr, val, CAR_SW);
	}
}

/* Chromatic Gain Suppression */
void ipipe_set_cgs_regs(void __iomem *base_addr, struct vpfe_ipipe_cgs *cgs)
{
	ipipe_clock_enable(base_addr);
	regw_ip(base_addr, cgs->en, CGS_EN);

	if (!cgs->en)
		return;

	/* Set the bright side parameters */
	regw_ip(base_addr, cgs->h_thr, CGS_GN1_H_THR);
	regw_ip(base_addr, cgs->h_slope, CGS_GN1_H_GAN);
	regw_ip(base_addr, cgs->h_shft & CAR_SHIFT_MASK, CGS_GN1_H_SHF);
	regw_ip(base_addr, cgs->h_min, CGS_GN1_H_MIN);
}

void rsz_src_enable(void __iomem *rsz_base, int enable)
{
	regw_rsz(rsz_base, enable, RSZ_SRC_EN);
}

int rsz_enable(void __iomem *rsz_base, int rsz_id, int enable)
{
	if (rsz_id == RSZ_A) {
		regw_rsz(rsz_base, enable, RSZ_EN_A);
		/* We always enable RSZ_A. RSZ_B is enable upon request from
		 * application. So enable RSZ_SRC_EN along with RSZ_A
		 */
		regw_rsz(rsz_base, enable, RSZ_SRC_EN);
	} else if (rsz_id == RSZ_B) {
		regw_rsz(rsz_base, enable, RSZ_EN_B);
	} else {
		BUG();
	}

	return 0;
}
