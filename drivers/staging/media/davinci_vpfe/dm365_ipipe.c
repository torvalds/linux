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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Contributors:
 *      Manjunath Hadli <manjunath.hadli@ti.com>
 *      Prabhakar Lad <prabhakar.lad@ti.com>
 *
 *
 * IPIPE allows fine tuning of the input image using different
 * tuning modules in IPIPE. Some examples :- Noise filter, Defect
 * pixel correction etc. It essentially operate on Bayer Raw data
 * or YUV raw data. To do image tuning, application call,
 *
 */

#include <linux/slab.h>

#include "dm365_ipipe.h"
#include "dm365_ipipe_hw.h"
#include "vpfe_mc_capture.h"

#define MIN_OUT_WIDTH	32
#define MIN_OUT_HEIGHT	32

/* ipipe input format's */
static const unsigned int ipipe_input_fmts[] = {
	MEDIA_BUS_FMT_UYVY8_2X8,
	MEDIA_BUS_FMT_SGRBG12_1X12,
	MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8,
	MEDIA_BUS_FMT_SGRBG10_ALAW8_1X8,
};

/* ipipe output format's */
static const unsigned int ipipe_output_fmts[] = {
	MEDIA_BUS_FMT_UYVY8_2X8,
};

static int ipipe_validate_lutdpc_params(struct vpfe_ipipe_lutdpc *lutdpc)
{
	int i;

	if (lutdpc->en > 1 || lutdpc->repl_white > 1 ||
	    lutdpc->dpc_size > LUT_DPC_MAX_SIZE)
		return -EINVAL;

	if (lutdpc->en && !lutdpc->table)
		return -EINVAL;

	for (i = 0; i < lutdpc->dpc_size; i++)
		if (lutdpc->table[i].horz_pos > LUT_DPC_H_POS_MASK ||
		   lutdpc->table[i].vert_pos > LUT_DPC_V_POS_MASK)
			return -EINVAL;

	return 0;
}

static int ipipe_set_lutdpc_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_lutdpc *lutdpc = &ipipe->config.lutdpc;
	struct vpfe_ipipe_lutdpc *dpc_param;
	struct device *dev;

	if (!param) {
		memset((void *)lutdpc, 0, sizeof(struct vpfe_ipipe_lutdpc));
		goto success;
	}

	dev = ipipe->subdev.v4l2_dev->dev;
	dpc_param = (struct vpfe_ipipe_lutdpc *)param;
	lutdpc->en = dpc_param->en;
	lutdpc->repl_white = dpc_param->repl_white;
	lutdpc->dpc_size = dpc_param->dpc_size;
	memcpy(&lutdpc->table, &dpc_param->table,
	       (dpc_param->dpc_size * sizeof(struct vpfe_ipipe_lutdpc_entry)));
	if (ipipe_validate_lutdpc_params(lutdpc) < 0)
		return -EINVAL;

success:
	ipipe_set_lutdpc_regs(ipipe->base_addr, ipipe->isp5_base_addr, lutdpc);

	return 0;
}

static int ipipe_get_lutdpc_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_lutdpc *lut_param = (struct vpfe_ipipe_lutdpc *)param;
	struct vpfe_ipipe_lutdpc *lutdpc = &ipipe->config.lutdpc;

	lut_param->en = lutdpc->en;
	lut_param->repl_white = lutdpc->repl_white;
	lut_param->dpc_size = lutdpc->dpc_size;
	memcpy(&lut_param->table, &lutdpc->table,
	   (lutdpc->dpc_size * sizeof(struct vpfe_ipipe_lutdpc_entry)));

	return 0;
}

static int ipipe_set_input_config(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_input_config *config = &ipipe->config.input_config;

	if (!param)
		memset(config, 0, sizeof(struct vpfe_ipipe_input_config));
	else
		memcpy(config, param, sizeof(struct vpfe_ipipe_input_config));
	return 0;
}

static int ipipe_get_input_config(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_input_config *config = &ipipe->config.input_config;

	if (!param)
		return -EINVAL;

	memcpy(param, config, sizeof(struct vpfe_ipipe_input_config));

	return 0;
}

static int ipipe_validate_otfdpc_params(struct vpfe_ipipe_otfdpc *dpc_param)
{
	struct vpfe_ipipe_otfdpc_2_0_cfg *dpc_2_0;
	struct vpfe_ipipe_otfdpc_3_0_cfg *dpc_3_0;

	if (dpc_param->en > 1)
		return -EINVAL;

	if (dpc_param->alg == VPFE_IPIPE_OTFDPC_2_0) {
		dpc_2_0 = &dpc_param->alg_cfg.dpc_2_0;
		if (dpc_2_0->det_thr.r > OTFDPC_DPC2_THR_MASK ||
		    dpc_2_0->det_thr.gr > OTFDPC_DPC2_THR_MASK ||
		    dpc_2_0->det_thr.gb > OTFDPC_DPC2_THR_MASK ||
		    dpc_2_0->det_thr.b > OTFDPC_DPC2_THR_MASK ||
		    dpc_2_0->corr_thr.r > OTFDPC_DPC2_THR_MASK ||
		    dpc_2_0->corr_thr.gr > OTFDPC_DPC2_THR_MASK ||
		    dpc_2_0->corr_thr.gb > OTFDPC_DPC2_THR_MASK ||
		    dpc_2_0->corr_thr.b > OTFDPC_DPC2_THR_MASK)
			return -EINVAL;
		return 0;
	}

	dpc_3_0 = &dpc_param->alg_cfg.dpc_3_0;

	if (dpc_3_0->act_adj_shf > OTF_DPC3_0_SHF_MASK ||
	    dpc_3_0->det_thr > OTF_DPC3_0_DET_MASK ||
	    dpc_3_0->det_slp > OTF_DPC3_0_SLP_MASK ||
	    dpc_3_0->det_thr_min > OTF_DPC3_0_DET_MASK ||
	    dpc_3_0->det_thr_max > OTF_DPC3_0_DET_MASK ||
	    dpc_3_0->corr_thr > OTF_DPC3_0_CORR_MASK ||
	    dpc_3_0->corr_slp > OTF_DPC3_0_SLP_MASK ||
	    dpc_3_0->corr_thr_min > OTF_DPC3_0_CORR_MASK ||
	    dpc_3_0->corr_thr_max > OTF_DPC3_0_CORR_MASK)
		return -EINVAL;

	return 0;
}

static int ipipe_set_otfdpc_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_otfdpc *dpc_param = (struct vpfe_ipipe_otfdpc *)param;
	struct vpfe_ipipe_otfdpc *otfdpc = &ipipe->config.otfdpc;
	struct device *dev;

	if (!param) {
		memset((void *)otfdpc, 0, sizeof(struct ipipe_otfdpc_2_0));
		goto success;
	}
	dev = ipipe->subdev.v4l2_dev->dev;
	memcpy(otfdpc, dpc_param, sizeof(struct vpfe_ipipe_otfdpc));
	if (ipipe_validate_otfdpc_params(otfdpc) < 0) {
		dev_err(dev, "Invalid otfdpc params\n");
		return -EINVAL;
	}

success:
	ipipe_set_otfdpc_regs(ipipe->base_addr, otfdpc);

	return 0;
}

static int ipipe_get_otfdpc_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_otfdpc *dpc_param = (struct vpfe_ipipe_otfdpc *)param;
	struct vpfe_ipipe_otfdpc *otfdpc = &ipipe->config.otfdpc;

	memcpy(dpc_param, otfdpc, sizeof(struct vpfe_ipipe_otfdpc));
	return 0;
}

static int ipipe_validate_nf_params(struct vpfe_ipipe_nf *nf_param)
{
	int i;

	if (nf_param->en > 1 || nf_param->shft_val > D2F_SHFT_VAL_MASK ||
	    nf_param->spread_val > D2F_SPR_VAL_MASK ||
	    nf_param->apply_lsc_gain > 1 ||
	    nf_param->edge_det_min_thr > D2F_EDGE_DET_THR_MASK ||
	    nf_param->edge_det_max_thr > D2F_EDGE_DET_THR_MASK)
		return -EINVAL;

	for (i = 0; i < VPFE_IPIPE_NF_THR_TABLE_SIZE; i++)
		if (nf_param->thr[i] > D2F_THR_VAL_MASK)
			return -EINVAL;

	for (i = 0; i < VPFE_IPIPE_NF_STR_TABLE_SIZE; i++)
		if (nf_param->str[i] > D2F_STR_VAL_MASK)
			return -EINVAL;

	return 0;
}

static int ipipe_set_nf_params(struct vpfe_ipipe_device *ipipe,
			       unsigned int id, void *param)
{
	struct vpfe_ipipe_nf *nf_param = (struct vpfe_ipipe_nf *)param;
	struct vpfe_ipipe_nf *nf = &ipipe->config.nf1;
	struct device *dev;

	if (id == IPIPE_D2F_2ND)
		nf = &ipipe->config.nf2;

	if (!nf_param) {
		memset((void *)nf, 0, sizeof(struct vpfe_ipipe_nf));
		goto success;
	}

	dev = ipipe->subdev.v4l2_dev->dev;
	memcpy(nf, nf_param, sizeof(struct vpfe_ipipe_nf));
	if (ipipe_validate_nf_params(nf) < 0) {
		dev_err(dev, "Invalid nf params\n");
		return -EINVAL;
	}

success:
	ipipe_set_d2f_regs(ipipe->base_addr, id, nf);

	return 0;
}

static int ipipe_set_nf1_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	return ipipe_set_nf_params(ipipe, IPIPE_D2F_1ST, param);
}

static int ipipe_set_nf2_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	return ipipe_set_nf_params(ipipe, IPIPE_D2F_2ND, param);
}

static int ipipe_get_nf_params(struct vpfe_ipipe_device *ipipe,
			       unsigned int id, void *param)
{
	struct vpfe_ipipe_nf *nf_param = (struct vpfe_ipipe_nf *)param;
	struct vpfe_ipipe_nf *nf = &ipipe->config.nf1;

	if (id == IPIPE_D2F_2ND)
		nf = &ipipe->config.nf2;

	memcpy(nf_param, nf, sizeof(struct vpfe_ipipe_nf));

	return 0;
}

static int ipipe_get_nf1_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	return ipipe_get_nf_params(ipipe, IPIPE_D2F_1ST, param);
}

static int ipipe_get_nf2_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	return ipipe_get_nf_params(ipipe, IPIPE_D2F_2ND, param);
}

static int ipipe_validate_gic_params(struct vpfe_ipipe_gic *gic)
{
	if (gic->en > 1 || gic->gain > GIC_GAIN_MASK ||
	    gic->thr > GIC_THR_MASK || gic->slope > GIC_SLOPE_MASK ||
	    gic->apply_lsc_gain > 1 ||
	    gic->nf2_thr_gain.integer > GIC_NFGAN_INT_MASK ||
	    gic->nf2_thr_gain.decimal > GIC_NFGAN_DECI_MASK)
		return -EINVAL;

	return 0;
}

static int ipipe_set_gic_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_gic *gic_param = (struct vpfe_ipipe_gic *)param;
	struct device *dev = ipipe->subdev.v4l2_dev->dev;
	struct vpfe_ipipe_gic *gic = &ipipe->config.gic;

	if (!gic_param) {
		memset((void *)gic, 0, sizeof(struct vpfe_ipipe_gic));
		goto success;
	}

	memcpy(gic, gic_param, sizeof(struct vpfe_ipipe_gic));
	if (ipipe_validate_gic_params(gic) < 0) {
		dev_err(dev, "Invalid gic params\n");
		return -EINVAL;
	}

success:
	ipipe_set_gic_regs(ipipe->base_addr, gic);

	return 0;
}

static int ipipe_get_gic_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_gic *gic_param = (struct vpfe_ipipe_gic *)param;
	struct vpfe_ipipe_gic *gic = &ipipe->config.gic;

	memcpy(gic_param, gic, sizeof(struct vpfe_ipipe_gic));

	return 0;
}

static int ipipe_validate_wb_params(struct vpfe_ipipe_wb *wbal)
{
	if (wbal->ofst_r > WB_OFFSET_MASK ||
	    wbal->ofst_gr > WB_OFFSET_MASK ||
	    wbal->ofst_gb > WB_OFFSET_MASK ||
	    wbal->ofst_b > WB_OFFSET_MASK ||
	    wbal->gain_r.integer > WB_GAIN_INT_MASK ||
	    wbal->gain_r.decimal > WB_GAIN_DECI_MASK ||
	    wbal->gain_gr.integer > WB_GAIN_INT_MASK ||
	    wbal->gain_gr.decimal > WB_GAIN_DECI_MASK ||
	    wbal->gain_gb.integer > WB_GAIN_INT_MASK ||
	    wbal->gain_gb.decimal > WB_GAIN_DECI_MASK ||
	    wbal->gain_b.integer > WB_GAIN_INT_MASK ||
	    wbal->gain_b.decimal > WB_GAIN_DECI_MASK)
		return -EINVAL;

	return 0;
}

static int ipipe_set_wb_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_wb *wb_param = (struct vpfe_ipipe_wb *)param;
	struct vpfe_ipipe_wb *wbal = &ipipe->config.wbal;

	if (!wb_param) {
		const struct vpfe_ipipe_wb wb_defaults = {
			.gain_r  = {2, 0x0},
			.gain_gr = {2, 0x0},
			.gain_gb = {2, 0x0},
			.gain_b  = {2, 0x0}
		};
		memcpy(wbal, &wb_defaults, sizeof(struct vpfe_ipipe_wb));
		goto success;
	}

	memcpy(wbal, wb_param, sizeof(struct vpfe_ipipe_wb));
	if (ipipe_validate_wb_params(wbal) < 0)
		return -EINVAL;

success:
	ipipe_set_wb_regs(ipipe->base_addr, wbal);

	return 0;
}

static int ipipe_get_wb_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_wb *wb_param = (struct vpfe_ipipe_wb *)param;
	struct vpfe_ipipe_wb *wbal = &ipipe->config.wbal;

	memcpy(wb_param, wbal, sizeof(struct vpfe_ipipe_wb));
	return 0;
}

static int ipipe_validate_cfa_params(struct vpfe_ipipe_cfa *cfa)
{
	if (cfa->hpf_thr_2dir > CFA_HPF_THR_2DIR_MASK ||
	    cfa->hpf_slp_2dir > CFA_HPF_SLOPE_2DIR_MASK ||
	    cfa->hp_mix_thr_2dir > CFA_HPF_MIX_THR_2DIR_MASK ||
	    cfa->hp_mix_slope_2dir > CFA_HPF_MIX_SLP_2DIR_MASK ||
	    cfa->dir_thr_2dir > CFA_DIR_THR_2DIR_MASK ||
	    cfa->dir_slope_2dir > CFA_DIR_SLP_2DIR_MASK ||
	    cfa->nd_wt_2dir > CFA_ND_WT_2DIR_MASK ||
	    cfa->hue_fract_daa > CFA_DAA_HUE_FRA_MASK ||
	    cfa->edge_thr_daa > CFA_DAA_EDG_THR_MASK ||
	    cfa->thr_min_daa > CFA_DAA_THR_MIN_MASK ||
	    cfa->thr_slope_daa > CFA_DAA_THR_SLP_MASK ||
	    cfa->slope_min_daa > CFA_DAA_SLP_MIN_MASK ||
	    cfa->slope_slope_daa > CFA_DAA_SLP_SLP_MASK ||
	    cfa->lp_wt_daa > CFA_DAA_LP_WT_MASK)
		return -EINVAL;

	return 0;
}

static int ipipe_set_cfa_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_cfa *cfa_param = (struct vpfe_ipipe_cfa *)param;
	struct vpfe_ipipe_cfa *cfa = &ipipe->config.cfa;

	if (!cfa_param) {
		memset(cfa, 0, sizeof(struct vpfe_ipipe_cfa));
		cfa->alg = VPFE_IPIPE_CFA_ALG_2DIRAC;
		goto success;
	}

	memcpy(cfa, cfa_param, sizeof(struct vpfe_ipipe_cfa));
	if (ipipe_validate_cfa_params(cfa) < 0)
		return -EINVAL;

success:
	ipipe_set_cfa_regs(ipipe->base_addr, cfa);

	return 0;
}

static int ipipe_get_cfa_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_cfa *cfa_param = (struct vpfe_ipipe_cfa *)param;
	struct vpfe_ipipe_cfa *cfa = &ipipe->config.cfa;

	memcpy(cfa_param, cfa, sizeof(struct vpfe_ipipe_cfa));
	return 0;
}

static int
ipipe_validate_rgb2rgb_params(struct vpfe_ipipe_rgb2rgb *rgb2rgb,
			      unsigned int id)
{
	u32 gain_int_upper = RGB2RGB_1_GAIN_INT_MASK;
	u32 offset_upper = RGB2RGB_1_OFST_MASK;

	if (id == IPIPE_RGB2RGB_2) {
		offset_upper = RGB2RGB_2_OFST_MASK;
		gain_int_upper = RGB2RGB_2_GAIN_INT_MASK;
	}

	if (rgb2rgb->coef_rr.decimal > RGB2RGB_GAIN_DECI_MASK ||
	    rgb2rgb->coef_rr.integer > gain_int_upper)
		return -EINVAL;

	if (rgb2rgb->coef_gr.decimal > RGB2RGB_GAIN_DECI_MASK ||
	    rgb2rgb->coef_gr.integer > gain_int_upper)
		return -EINVAL;

	if (rgb2rgb->coef_br.decimal > RGB2RGB_GAIN_DECI_MASK ||
	    rgb2rgb->coef_br.integer > gain_int_upper)
		return -EINVAL;

	if (rgb2rgb->coef_rg.decimal > RGB2RGB_GAIN_DECI_MASK ||
	    rgb2rgb->coef_rg.integer > gain_int_upper)
		return -EINVAL;

	if (rgb2rgb->coef_gg.decimal > RGB2RGB_GAIN_DECI_MASK ||
	    rgb2rgb->coef_gg.integer > gain_int_upper)
		return -EINVAL;

	if (rgb2rgb->coef_bg.decimal > RGB2RGB_GAIN_DECI_MASK ||
	    rgb2rgb->coef_bg.integer > gain_int_upper)
		return -EINVAL;

	if (rgb2rgb->coef_rb.decimal > RGB2RGB_GAIN_DECI_MASK ||
	    rgb2rgb->coef_rb.integer > gain_int_upper)
		return -EINVAL;

	if (rgb2rgb->coef_gb.decimal > RGB2RGB_GAIN_DECI_MASK ||
	    rgb2rgb->coef_gb.integer > gain_int_upper)
		return -EINVAL;

	if (rgb2rgb->coef_bb.decimal > RGB2RGB_GAIN_DECI_MASK ||
	    rgb2rgb->coef_bb.integer > gain_int_upper)
		return -EINVAL;

	if (rgb2rgb->out_ofst_r > offset_upper ||
	    rgb2rgb->out_ofst_g > offset_upper ||
	    rgb2rgb->out_ofst_b > offset_upper)
		return -EINVAL;

	return 0;
}

static int ipipe_set_rgb2rgb_params(struct vpfe_ipipe_device *ipipe,
			      unsigned int id, void *param)
{
	struct vpfe_ipipe_rgb2rgb *rgb2rgb = &ipipe->config.rgb2rgb1;
	struct device *dev = ipipe->subdev.v4l2_dev->dev;
	struct vpfe_ipipe_rgb2rgb *rgb2rgb_param;

	rgb2rgb_param = (struct vpfe_ipipe_rgb2rgb *)param;

	if (id == IPIPE_RGB2RGB_2)
		rgb2rgb = &ipipe->config.rgb2rgb2;

	if (!rgb2rgb_param) {
		const struct vpfe_ipipe_rgb2rgb rgb2rgb_defaults = {
			.coef_rr = {1, 0},	/* 256 */
			.coef_gr = {0, 0},
			.coef_br = {0, 0},
			.coef_rg = {0, 0},
			.coef_gg = {1, 0},	/* 256 */
			.coef_bg = {0, 0},
			.coef_rb = {0, 0},
			.coef_gb = {0, 0},
			.coef_bb = {1, 0},	/* 256 */
		};
		/* Copy defaults for rgb2rgb conversion */
		memcpy(rgb2rgb, &rgb2rgb_defaults,
		       sizeof(struct vpfe_ipipe_rgb2rgb));
		goto success;
	}

	memcpy(rgb2rgb, rgb2rgb_param, sizeof(struct vpfe_ipipe_rgb2rgb));
	if (ipipe_validate_rgb2rgb_params(rgb2rgb, id) < 0) {
		dev_err(dev, "Invalid rgb2rgb params\n");
		return -EINVAL;
	}

success:
	ipipe_set_rgb2rgb_regs(ipipe->base_addr, id, rgb2rgb);

	return 0;
}

static int
ipipe_set_rgb2rgb_1_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	return ipipe_set_rgb2rgb_params(ipipe, IPIPE_RGB2RGB_1, param);
}

static int
ipipe_set_rgb2rgb_2_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	return ipipe_set_rgb2rgb_params(ipipe, IPIPE_RGB2RGB_2, param);
}

static int ipipe_get_rgb2rgb_params(struct vpfe_ipipe_device *ipipe,
			      unsigned int id, void *param)
{
	struct vpfe_ipipe_rgb2rgb *rgb2rgb = &ipipe->config.rgb2rgb1;
	struct vpfe_ipipe_rgb2rgb *rgb2rgb_param;

	rgb2rgb_param = (struct vpfe_ipipe_rgb2rgb *)param;

	if (id == IPIPE_RGB2RGB_2)
		rgb2rgb = &ipipe->config.rgb2rgb2;

	memcpy(rgb2rgb_param, rgb2rgb, sizeof(struct vpfe_ipipe_rgb2rgb));

	return 0;
}

static int
ipipe_get_rgb2rgb_1_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	return ipipe_get_rgb2rgb_params(ipipe, IPIPE_RGB2RGB_1, param);
}

static int
ipipe_get_rgb2rgb_2_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	return ipipe_get_rgb2rgb_params(ipipe, IPIPE_RGB2RGB_2, param);
}

static int
ipipe_validate_gamma_entry(struct vpfe_ipipe_gamma_entry *table, int size)
{
	int i;

	if (!table)
		return -EINVAL;

	for (i = 0; i < size; i++)
		if (table[i].slope > GAMMA_MASK ||
		    table[i].offset > GAMMA_MASK)
			return -EINVAL;

	return 0;
}

static int
ipipe_validate_gamma_params(struct vpfe_ipipe_gamma *gamma, struct device *dev)
{
	int table_size;
	int err;

	if (gamma->bypass_r > 1 ||
	    gamma->bypass_b > 1 ||
	    gamma->bypass_g > 1)
		return -EINVAL;

	if (gamma->tbl_sel != VPFE_IPIPE_GAMMA_TBL_RAM)
		return 0;

	table_size = gamma->tbl_size;
	if (!gamma->bypass_r) {
		err = ipipe_validate_gamma_entry(gamma->table_r, table_size);
		if (err) {
			dev_err(dev, "GAMMA R - table entry invalid\n");
			return err;
		}
	}

	if (!gamma->bypass_b) {
		err = ipipe_validate_gamma_entry(gamma->table_b, table_size);
		if (err) {
			dev_err(dev, "GAMMA B - table entry invalid\n");
			return err;
		}
	}

	if (!gamma->bypass_g) {
		err = ipipe_validate_gamma_entry(gamma->table_g, table_size);
		if (err) {
			dev_err(dev, "GAMMA G - table entry invalid\n");
			return err;
		}
	}

	return 0;
}

static int
ipipe_set_gamma_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_gamma *gamma_param = (struct vpfe_ipipe_gamma *)param;
	struct vpfe_ipipe_gamma *gamma = &ipipe->config.gamma;
	struct device *dev = ipipe->subdev.v4l2_dev->dev;
	int table_size;

	if (!gamma_param) {
		memset(gamma, 0, sizeof(struct vpfe_ipipe_gamma));
		gamma->tbl_sel = VPFE_IPIPE_GAMMA_TBL_ROM;
		goto success;
	}

	gamma->bypass_r = gamma_param->bypass_r;
	gamma->bypass_b = gamma_param->bypass_b;
	gamma->bypass_g = gamma_param->bypass_g;
	gamma->tbl_sel = gamma_param->tbl_sel;
	gamma->tbl_size = gamma_param->tbl_size;

	if (ipipe_validate_gamma_params(gamma, dev) < 0)
		return -EINVAL;

	if (gamma_param->tbl_sel != VPFE_IPIPE_GAMMA_TBL_RAM)
		goto success;

	table_size = gamma->tbl_size;
	if (!gamma_param->bypass_r)
		memcpy(&gamma->table_r, &gamma_param->table_r,
		       (table_size * sizeof(struct vpfe_ipipe_gamma_entry)));

	if (!gamma_param->bypass_b)
		memcpy(&gamma->table_b, &gamma_param->table_b,
		       (table_size * sizeof(struct vpfe_ipipe_gamma_entry)));

	if (!gamma_param->bypass_g)
		memcpy(&gamma->table_g, &gamma_param->table_g,
		       (table_size * sizeof(struct vpfe_ipipe_gamma_entry)));

success:
	ipipe_set_gamma_regs(ipipe->base_addr, ipipe->isp5_base_addr, gamma);

	return 0;
}

static int ipipe_get_gamma_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_gamma *gamma_param = (struct vpfe_ipipe_gamma *)param;
	struct vpfe_ipipe_gamma *gamma = &ipipe->config.gamma;
	struct device *dev = ipipe->subdev.v4l2_dev->dev;
	int table_size;

	gamma_param->bypass_r = gamma->bypass_r;
	gamma_param->bypass_g = gamma->bypass_g;
	gamma_param->bypass_b = gamma->bypass_b;
	gamma_param->tbl_sel = gamma->tbl_sel;
	gamma_param->tbl_size = gamma->tbl_size;

	if (gamma->tbl_sel != VPFE_IPIPE_GAMMA_TBL_RAM)
		return 0;

	table_size = gamma->tbl_size;

	if (!gamma->bypass_r && !gamma_param->table_r) {
		dev_err(dev,
			"ipipe_get_gamma_params: table ptr empty for R\n");
		return -EINVAL;
	}
	memcpy(gamma_param->table_r, gamma->table_r,
	       (table_size * sizeof(struct vpfe_ipipe_gamma_entry)));

	if (!gamma->bypass_g && !gamma_param->table_g) {
		dev_err(dev, "ipipe_get_gamma_params: table ptr empty for G\n");
		return -EINVAL;
	}
	memcpy(gamma_param->table_g, gamma->table_g,
	       (table_size * sizeof(struct vpfe_ipipe_gamma_entry)));

	if (!gamma->bypass_b && !gamma_param->table_b) {
		dev_err(dev, "ipipe_get_gamma_params: table ptr empty for B\n");
		return -EINVAL;
	}
	memcpy(gamma_param->table_b, gamma->table_b,
	       (table_size * sizeof(struct vpfe_ipipe_gamma_entry)));

	return 0;
}

static int ipipe_validate_3d_lut_params(struct vpfe_ipipe_3d_lut *lut)
{
	int i;

	if (!lut->en)
		return 0;

	for (i = 0; i < VPFE_IPIPE_MAX_SIZE_3D_LUT; i++)
		if (lut->table[i].r > D3_LUT_ENTRY_MASK ||
		    lut->table[i].g > D3_LUT_ENTRY_MASK ||
		    lut->table[i].b > D3_LUT_ENTRY_MASK)
			return -EINVAL;

	return 0;
}

static int ipipe_get_3d_lut_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_3d_lut *lut_param = (struct vpfe_ipipe_3d_lut *)param;
	struct vpfe_ipipe_3d_lut *lut = &ipipe->config.lut;
	struct device *dev = ipipe->subdev.v4l2_dev->dev;

	lut_param->en = lut->en;
	if (!lut_param->table) {
		dev_err(dev, "ipipe_get_3d_lut_params: Invalid table ptr\n");
		return -EINVAL;
	}

	memcpy(lut_param->table, &lut->table,
	       (VPFE_IPIPE_MAX_SIZE_3D_LUT *
	       sizeof(struct vpfe_ipipe_3d_lut_entry)));

	return 0;
}

static int
ipipe_set_3d_lut_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_3d_lut *lut_param = (struct vpfe_ipipe_3d_lut *)param;
	struct vpfe_ipipe_3d_lut *lut = &ipipe->config.lut;
	struct device *dev = ipipe->subdev.v4l2_dev->dev;

	if (!lut_param) {
		memset(lut, 0, sizeof(struct vpfe_ipipe_3d_lut));
		goto success;
	}

	memcpy(lut, lut_param, sizeof(struct vpfe_ipipe_3d_lut));
	if (ipipe_validate_3d_lut_params(lut) < 0) {
		dev_err(dev, "Invalid 3D-LUT Params\n");
		return -EINVAL;
	}

success:
	ipipe_set_3d_lut_regs(ipipe->base_addr, ipipe->isp5_base_addr, lut);

	return 0;
}

static int ipipe_validate_rgb2yuv_params(struct vpfe_ipipe_rgb2yuv *rgb2yuv)
{
	if (rgb2yuv->coef_ry.decimal > RGB2YCBCR_COEF_DECI_MASK ||
	   rgb2yuv->coef_ry.integer > RGB2YCBCR_COEF_INT_MASK)
		return -EINVAL;

	if (rgb2yuv->coef_gy.decimal > RGB2YCBCR_COEF_DECI_MASK ||
	   rgb2yuv->coef_gy.integer > RGB2YCBCR_COEF_INT_MASK)
		return -EINVAL;

	if (rgb2yuv->coef_by.decimal > RGB2YCBCR_COEF_DECI_MASK ||
	   rgb2yuv->coef_by.integer > RGB2YCBCR_COEF_INT_MASK)
		return -EINVAL;

	if (rgb2yuv->coef_rcb.decimal > RGB2YCBCR_COEF_DECI_MASK ||
	   rgb2yuv->coef_rcb.integer > RGB2YCBCR_COEF_INT_MASK)
		return -EINVAL;

	if (rgb2yuv->coef_gcb.decimal > RGB2YCBCR_COEF_DECI_MASK ||
	   rgb2yuv->coef_gcb.integer > RGB2YCBCR_COEF_INT_MASK)
		return -EINVAL;

	if (rgb2yuv->coef_bcb.decimal > RGB2YCBCR_COEF_DECI_MASK ||
	   rgb2yuv->coef_bcb.integer > RGB2YCBCR_COEF_INT_MASK)
		return -EINVAL;

	if (rgb2yuv->coef_rcr.decimal > RGB2YCBCR_COEF_DECI_MASK ||
	   rgb2yuv->coef_rcr.integer > RGB2YCBCR_COEF_INT_MASK)
		return -EINVAL;

	if (rgb2yuv->coef_gcr.decimal > RGB2YCBCR_COEF_DECI_MASK ||
	   rgb2yuv->coef_gcr.integer > RGB2YCBCR_COEF_INT_MASK)
		return -EINVAL;

	if (rgb2yuv->coef_bcr.decimal > RGB2YCBCR_COEF_DECI_MASK ||
	   rgb2yuv->coef_bcr.integer > RGB2YCBCR_COEF_INT_MASK)
		return -EINVAL;

	if (rgb2yuv->out_ofst_y > RGB2YCBCR_OFST_MASK ||
	   rgb2yuv->out_ofst_cb > RGB2YCBCR_OFST_MASK ||
	   rgb2yuv->out_ofst_cr > RGB2YCBCR_OFST_MASK)
		return -EINVAL;

	return 0;
}

static int
ipipe_set_rgb2yuv_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_rgb2yuv *rgb2yuv = &ipipe->config.rgb2yuv;
	struct device *dev = ipipe->subdev.v4l2_dev->dev;
	struct vpfe_ipipe_rgb2yuv *rgb2yuv_param;

	rgb2yuv_param = (struct vpfe_ipipe_rgb2yuv *)param;
	if (!rgb2yuv_param) {
		/* Defaults for rgb2yuv conversion */
		const struct vpfe_ipipe_rgb2yuv rgb2yuv_defaults = {
			.coef_ry  = {0, 0x4d},
			.coef_gy  = {0, 0x96},
			.coef_by  = {0, 0x1d},
			.coef_rcb = {0xf, 0xd5},
			.coef_gcb = {0xf, 0xab},
			.coef_bcb = {0, 0x80},
			.coef_rcr = {0, 0x80},
			.coef_gcr = {0xf, 0x95},
			.coef_bcr = {0xf, 0xeb},
			.out_ofst_cb = 0x80,
			.out_ofst_cr = 0x80,
		};
		/* Copy defaults for rgb2yuv conversion  */
		memcpy(rgb2yuv, &rgb2yuv_defaults,
		       sizeof(struct vpfe_ipipe_rgb2yuv));
		goto success;
	}

	memcpy(rgb2yuv, rgb2yuv_param, sizeof(struct vpfe_ipipe_rgb2yuv));
	if (ipipe_validate_rgb2yuv_params(rgb2yuv) < 0) {
		dev_err(dev, "Invalid rgb2yuv params\n");
		return -EINVAL;
	}

success:
	ipipe_set_rgb2ycbcr_regs(ipipe->base_addr, rgb2yuv);

	return 0;
}

static int
ipipe_get_rgb2yuv_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_rgb2yuv *rgb2yuv = &ipipe->config.rgb2yuv;
	struct vpfe_ipipe_rgb2yuv *rgb2yuv_param;

	rgb2yuv_param = (struct vpfe_ipipe_rgb2yuv *)param;
	memcpy(rgb2yuv_param, rgb2yuv, sizeof(struct vpfe_ipipe_rgb2yuv));
	return 0;
}

static int ipipe_validate_gbce_params(struct vpfe_ipipe_gbce *gbce)
{
	u32 max = GBCE_Y_VAL_MASK;
	int i;

	if (!gbce->en)
		return 0;

	if (gbce->type == VPFE_IPIPE_GBCE_GAIN_TBL)
		max = GBCE_GAIN_VAL_MASK;

	for (i = 0; i < VPFE_IPIPE_MAX_SIZE_GBCE_LUT; i++)
		if (gbce->table[i] > max)
			return -EINVAL;

	return 0;
}

static int ipipe_set_gbce_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_gbce *gbce_param = (struct vpfe_ipipe_gbce *)param;
	struct vpfe_ipipe_gbce *gbce = &ipipe->config.gbce;
	struct device *dev = ipipe->subdev.v4l2_dev->dev;

	if (!gbce_param) {
		memset(gbce, 0, sizeof(struct vpfe_ipipe_gbce));
	} else {
		memcpy(gbce, gbce_param, sizeof(struct vpfe_ipipe_gbce));
		if (ipipe_validate_gbce_params(gbce) < 0) {
			dev_err(dev, "Invalid gbce params\n");
			return -EINVAL;
		}
	}

	ipipe_set_gbce_regs(ipipe->base_addr, ipipe->isp5_base_addr, gbce);

	return 0;
}

static int ipipe_get_gbce_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_gbce *gbce_param = (struct vpfe_ipipe_gbce *)param;
	struct vpfe_ipipe_gbce *gbce = &ipipe->config.gbce;
	struct device *dev = ipipe->subdev.v4l2_dev->dev;

	gbce_param->en = gbce->en;
	gbce_param->type = gbce->type;
	if (!gbce_param->table) {
		dev_err(dev, "ipipe_get_gbce_params: Invalid table ptr\n");
		return -EINVAL;
	}

	memcpy(gbce_param->table, gbce->table,
		(VPFE_IPIPE_MAX_SIZE_GBCE_LUT * sizeof(unsigned short)));

	return 0;
}

static int
ipipe_validate_yuv422_conv_params(struct vpfe_ipipe_yuv422_conv *yuv422_conv)
{
	if (yuv422_conv->en_chrom_lpf > 1)
		return -EINVAL;

	return 0;
}

static int
ipipe_set_yuv422_conv_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_yuv422_conv *yuv422_conv = &ipipe->config.yuv422_conv;
	struct vpfe_ipipe_yuv422_conv *yuv422_conv_param;
	struct device *dev = ipipe->subdev.v4l2_dev->dev;

	yuv422_conv_param = (struct vpfe_ipipe_yuv422_conv *)param;
	if (!yuv422_conv_param) {
		memset(yuv422_conv, 0, sizeof(struct vpfe_ipipe_yuv422_conv));
		yuv422_conv->chrom_pos = VPFE_IPIPE_YUV422_CHR_POS_COSITE;
	} else {
		memcpy(yuv422_conv, yuv422_conv_param,
			sizeof(struct vpfe_ipipe_yuv422_conv));
		if (ipipe_validate_yuv422_conv_params(yuv422_conv) < 0) {
			dev_err(dev, "Invalid yuv422 params\n");
			return -EINVAL;
		}
	}

	ipipe_set_yuv422_conv_regs(ipipe->base_addr, yuv422_conv);

	return 0;
}

static int
ipipe_get_yuv422_conv_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_yuv422_conv *yuv422_conv = &ipipe->config.yuv422_conv;
	struct vpfe_ipipe_yuv422_conv *yuv422_conv_param;

	yuv422_conv_param = (struct vpfe_ipipe_yuv422_conv *)param;
	memcpy(yuv422_conv_param, yuv422_conv,
	       sizeof(struct vpfe_ipipe_yuv422_conv));

	return 0;
}

static int ipipe_validate_yee_params(struct vpfe_ipipe_yee *yee)
{
	int i;

	if (yee->en > 1 ||
	    yee->en_halo_red > 1 ||
	    yee->hpf_shft > YEE_HPF_SHIFT_MASK)
		return -EINVAL;

	if (yee->hpf_coef_00 > YEE_COEF_MASK ||
	    yee->hpf_coef_01 > YEE_COEF_MASK ||
	    yee->hpf_coef_02 > YEE_COEF_MASK ||
	    yee->hpf_coef_10 > YEE_COEF_MASK ||
	    yee->hpf_coef_11 > YEE_COEF_MASK ||
	    yee->hpf_coef_12 > YEE_COEF_MASK ||
	    yee->hpf_coef_20 > YEE_COEF_MASK ||
	    yee->hpf_coef_21 > YEE_COEF_MASK ||
	    yee->hpf_coef_22 > YEE_COEF_MASK)
		return -EINVAL;

	if (yee->yee_thr > YEE_THR_MASK ||
	    yee->es_gain > YEE_ES_GAIN_MASK ||
	    yee->es_thr1 > YEE_ES_THR1_MASK ||
	    yee->es_thr2 > YEE_THR_MASK ||
	    yee->es_gain_grad > YEE_THR_MASK ||
	    yee->es_ofst_grad > YEE_THR_MASK)
		return -EINVAL;

	for (i = 0; i < VPFE_IPIPE_MAX_SIZE_YEE_LUT; i++)
		if (yee->table[i] > YEE_ENTRY_MASK)
			return -EINVAL;

	return 0;
}

static int ipipe_set_yee_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_yee *yee_param = (struct vpfe_ipipe_yee *)param;
	struct device *dev = ipipe->subdev.v4l2_dev->dev;
	struct vpfe_ipipe_yee *yee = &ipipe->config.yee;

	if (!yee_param) {
		memset(yee, 0, sizeof(struct vpfe_ipipe_yee));
	} else {
		memcpy(yee, yee_param, sizeof(struct vpfe_ipipe_yee));
		if (ipipe_validate_yee_params(yee) < 0) {
			dev_err(dev, "Invalid yee params\n");
			return -EINVAL;
		}
	}

	ipipe_set_ee_regs(ipipe->base_addr, ipipe->isp5_base_addr, yee);

	return 0;
}

static int ipipe_get_yee_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_yee *yee_param = (struct vpfe_ipipe_yee *)param;
	struct vpfe_ipipe_yee *yee = &ipipe->config.yee;

	yee_param->en = yee->en;
	yee_param->en_halo_red = yee->en_halo_red;
	yee_param->merge_meth = yee->merge_meth;
	yee_param->hpf_shft = yee->hpf_shft;
	yee_param->hpf_coef_00 = yee->hpf_coef_00;
	yee_param->hpf_coef_01 = yee->hpf_coef_01;
	yee_param->hpf_coef_02 = yee->hpf_coef_02;
	yee_param->hpf_coef_10 = yee->hpf_coef_10;
	yee_param->hpf_coef_11 = yee->hpf_coef_11;
	yee_param->hpf_coef_12 = yee->hpf_coef_12;
	yee_param->hpf_coef_20 = yee->hpf_coef_20;
	yee_param->hpf_coef_21 = yee->hpf_coef_21;
	yee_param->hpf_coef_22 = yee->hpf_coef_22;
	yee_param->yee_thr = yee->yee_thr;
	yee_param->es_gain = yee->es_gain;
	yee_param->es_thr1 = yee->es_thr1;
	yee_param->es_thr2 = yee->es_thr2;
	yee_param->es_gain_grad = yee->es_gain_grad;
	yee_param->es_ofst_grad = yee->es_ofst_grad;
	memcpy(yee_param->table, &yee->table,
	       (VPFE_IPIPE_MAX_SIZE_YEE_LUT * sizeof(short)));

	return 0;
}

static int ipipe_validate_car_params(struct vpfe_ipipe_car *car)
{
	if (car->en > 1 || car->hpf_shft > CAR_HPF_SHIFT_MASK ||
	    car->gain1.shft > CAR_GAIN1_SHFT_MASK ||
	    car->gain1.gain_min > CAR_GAIN_MIN_MASK ||
	    car->gain2.shft > CAR_GAIN2_SHFT_MASK ||
	    car->gain2.gain_min > CAR_GAIN_MIN_MASK)
		return -EINVAL;

	return 0;
}

static int ipipe_set_car_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_car *car_param = (struct vpfe_ipipe_car *)param;
	struct device *dev = ipipe->subdev.v4l2_dev->dev;
	struct vpfe_ipipe_car *car = &ipipe->config.car;

	if (!car_param) {
		memset(car, 0, sizeof(struct vpfe_ipipe_car));
	} else {
		memcpy(car, car_param, sizeof(struct vpfe_ipipe_car));
		if (ipipe_validate_car_params(car) < 0) {
			dev_err(dev, "Invalid car params\n");
			return -EINVAL;
		}
	}

	ipipe_set_car_regs(ipipe->base_addr, car);

	return 0;
}

static int ipipe_get_car_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_car *car_param = (struct vpfe_ipipe_car *)param;
	struct vpfe_ipipe_car *car = &ipipe->config.car;

	memcpy(car_param, car, sizeof(struct vpfe_ipipe_car));
	return 0;
}

static int ipipe_validate_cgs_params(struct vpfe_ipipe_cgs *cgs)
{
	if (cgs->en > 1 || cgs->h_shft > CAR_SHIFT_MASK)
		return -EINVAL;

	return 0;
}

static int ipipe_set_cgs_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_cgs *cgs_param = (struct vpfe_ipipe_cgs *)param;
	struct device *dev = ipipe->subdev.v4l2_dev->dev;
	struct vpfe_ipipe_cgs *cgs = &ipipe->config.cgs;

	if (!cgs_param) {
		memset(cgs, 0, sizeof(struct vpfe_ipipe_cgs));
	} else {
		memcpy(cgs, cgs_param, sizeof(struct vpfe_ipipe_cgs));
		if (ipipe_validate_cgs_params(cgs) < 0) {
			dev_err(dev, "Invalid cgs params\n");
			return -EINVAL;
		}
	}

	ipipe_set_cgs_regs(ipipe->base_addr, cgs);

	return 0;
}

static int ipipe_get_cgs_params(struct vpfe_ipipe_device *ipipe, void *param)
{
	struct vpfe_ipipe_cgs *cgs_param = (struct vpfe_ipipe_cgs *)param;
	struct vpfe_ipipe_cgs *cgs = &ipipe->config.cgs;

	memcpy(cgs_param, cgs, sizeof(struct vpfe_ipipe_cgs));

	return 0;
}

static const struct ipipe_module_if ipipe_modules[VPFE_IPIPE_MAX_MODULES] = {
	/* VPFE_IPIPE_INPUT_CONFIG */ {
		offsetof(struct ipipe_module_params, input_config),
		FIELD_SIZEOF(struct ipipe_module_params, input_config),
		offsetof(struct vpfe_ipipe_config, input_config),
		ipipe_set_input_config,
		ipipe_get_input_config,
	}, /* VPFE_IPIPE_LUTDPC */ {
		offsetof(struct ipipe_module_params, lutdpc),
		FIELD_SIZEOF(struct ipipe_module_params, lutdpc),
		offsetof(struct vpfe_ipipe_config, lutdpc),
		ipipe_set_lutdpc_params,
		ipipe_get_lutdpc_params,
	}, /* VPFE_IPIPE_OTFDPC */ {
		offsetof(struct ipipe_module_params, otfdpc),
		FIELD_SIZEOF(struct ipipe_module_params, otfdpc),
		offsetof(struct vpfe_ipipe_config, otfdpc),
		ipipe_set_otfdpc_params,
		ipipe_get_otfdpc_params,
	}, /* VPFE_IPIPE_NF1 */ {
		offsetof(struct ipipe_module_params, nf1),
		FIELD_SIZEOF(struct ipipe_module_params, nf1),
		offsetof(struct vpfe_ipipe_config, nf1),
		ipipe_set_nf1_params,
		ipipe_get_nf1_params,
	}, /* VPFE_IPIPE_NF2 */ {
		offsetof(struct ipipe_module_params, nf2),
		FIELD_SIZEOF(struct ipipe_module_params, nf2),
		offsetof(struct vpfe_ipipe_config, nf2),
		ipipe_set_nf2_params,
		ipipe_get_nf2_params,
	}, /* VPFE_IPIPE_WB */ {
		offsetof(struct ipipe_module_params, wbal),
		FIELD_SIZEOF(struct ipipe_module_params, wbal),
		offsetof(struct vpfe_ipipe_config, wbal),
		ipipe_set_wb_params,
		ipipe_get_wb_params,
	}, /* VPFE_IPIPE_RGB2RGB_1 */ {
		offsetof(struct ipipe_module_params, rgb2rgb1),
		FIELD_SIZEOF(struct ipipe_module_params, rgb2rgb1),
		offsetof(struct vpfe_ipipe_config, rgb2rgb1),
		ipipe_set_rgb2rgb_1_params,
		ipipe_get_rgb2rgb_1_params,
	}, /* VPFE_IPIPE_RGB2RGB_2 */ {
		offsetof(struct ipipe_module_params, rgb2rgb2),
		FIELD_SIZEOF(struct ipipe_module_params, rgb2rgb2),
		offsetof(struct vpfe_ipipe_config, rgb2rgb2),
		ipipe_set_rgb2rgb_2_params,
		ipipe_get_rgb2rgb_2_params,
	}, /* VPFE_IPIPE_GAMMA */ {
		offsetof(struct ipipe_module_params, gamma),
		FIELD_SIZEOF(struct ipipe_module_params, gamma),
		offsetof(struct vpfe_ipipe_config, gamma),
		ipipe_set_gamma_params,
		ipipe_get_gamma_params,
	}, /* VPFE_IPIPE_3D_LUT */ {
		offsetof(struct ipipe_module_params, lut),
		FIELD_SIZEOF(struct ipipe_module_params, lut),
		offsetof(struct vpfe_ipipe_config, lut),
		ipipe_set_3d_lut_params,
		ipipe_get_3d_lut_params,
	}, /* VPFE_IPIPE_RGB2YUV */ {
		offsetof(struct ipipe_module_params, rgb2yuv),
		FIELD_SIZEOF(struct ipipe_module_params, rgb2yuv),
		offsetof(struct vpfe_ipipe_config, rgb2yuv),
		ipipe_set_rgb2yuv_params,
		ipipe_get_rgb2yuv_params,
	}, /* VPFE_IPIPE_YUV422_CONV */ {
		offsetof(struct ipipe_module_params, yuv422_conv),
		FIELD_SIZEOF(struct ipipe_module_params, yuv422_conv),
		offsetof(struct vpfe_ipipe_config, yuv422_conv),
		ipipe_set_yuv422_conv_params,
		ipipe_get_yuv422_conv_params,
	}, /* VPFE_IPIPE_YEE */ {
		offsetof(struct ipipe_module_params, yee),
		FIELD_SIZEOF(struct ipipe_module_params, yee),
		offsetof(struct vpfe_ipipe_config, yee),
		ipipe_set_yee_params,
		ipipe_get_yee_params,
	}, /* VPFE_IPIPE_GIC */ {
		offsetof(struct ipipe_module_params, gic),
		FIELD_SIZEOF(struct ipipe_module_params, gic),
		offsetof(struct vpfe_ipipe_config, gic),
		ipipe_set_gic_params,
		ipipe_get_gic_params,
	}, /* VPFE_IPIPE_CFA */ {
		offsetof(struct ipipe_module_params, cfa),
		FIELD_SIZEOF(struct ipipe_module_params, cfa),
		offsetof(struct vpfe_ipipe_config, cfa),
		ipipe_set_cfa_params,
		ipipe_get_cfa_params,
	}, /* VPFE_IPIPE_CAR */ {
		offsetof(struct ipipe_module_params, car),
		FIELD_SIZEOF(struct ipipe_module_params, car),
		offsetof(struct vpfe_ipipe_config, car),
		ipipe_set_car_params,
		ipipe_get_car_params,
	}, /* VPFE_IPIPE_CGS */ {
		offsetof(struct ipipe_module_params, cgs),
		FIELD_SIZEOF(struct ipipe_module_params, cgs),
		offsetof(struct vpfe_ipipe_config, cgs),
		ipipe_set_cgs_params,
		ipipe_get_cgs_params,
	}, /* VPFE_IPIPE_GBCE */ {
		offsetof(struct ipipe_module_params, gbce),
		FIELD_SIZEOF(struct ipipe_module_params, gbce),
		offsetof(struct vpfe_ipipe_config, gbce),
		ipipe_set_gbce_params,
		ipipe_get_gbce_params,
	},
};

static int ipipe_s_config(struct v4l2_subdev *sd, struct vpfe_ipipe_config *cfg)
{
	struct vpfe_ipipe_device *ipipe = v4l2_get_subdevdata(sd);
	unsigned int i;
	int rval = 0;

	for (i = 0; i < ARRAY_SIZE(ipipe_modules); i++) {
		unsigned int bit = 1 << i;

		if (cfg->flag & bit) {
			const struct ipipe_module_if *module_if =
						&ipipe_modules[i];
			struct ipipe_module_params *params;
			void __user *from = *(void * __user *)
				((void *)cfg + module_if->config_offset);
			size_t size;
			void *to;

			params = kmalloc(sizeof(struct ipipe_module_params),
					 GFP_KERNEL);
			to = (void *)params + module_if->param_offset;
			size = module_if->param_size;

			if (to && from && size) {
				if (copy_from_user(to, from, size)) {
					rval = -EFAULT;
					break;
				}
				rval = module_if->set(ipipe, to);
				if (rval)
					goto error;
			} else if (to && !from && size) {
				rval = module_if->set(ipipe, NULL);
				if (rval)
					goto error;
			}
			kfree(params);
		}
	}
error:
	return rval;
}

static int ipipe_g_config(struct v4l2_subdev *sd, struct vpfe_ipipe_config *cfg)
{
	struct vpfe_ipipe_device *ipipe = v4l2_get_subdevdata(sd);
	unsigned int i;
	int rval = 0;

	for (i = 1; i < ARRAY_SIZE(ipipe_modules); i++) {
		unsigned int bit = 1 << i;

		if (cfg->flag & bit) {
			const struct ipipe_module_if *module_if =
						&ipipe_modules[i];
			struct ipipe_module_params *params;
			void __user *to = *(void * __user *)
				((void *)cfg + module_if->config_offset);
			size_t size;
			void *from;

			params =  kmalloc(sizeof(struct ipipe_module_params),
						GFP_KERNEL);
			from = (void *)params + module_if->param_offset;
			size = module_if->param_size;

			if (to && from && size) {
				rval = module_if->get(ipipe, from);
				if (rval)
					goto error;
				if (copy_to_user(to, from, size)) {
					rval = -EFAULT;
					break;
				}
			}
			kfree(params);
		}
	}
error:
	return rval;
}

/*
 * ipipe_ioctl() - Handle ipipe module private ioctl's
 * @sd: pointer to v4l2 subdev structure
 * @cmd: configuration command
 * @arg: configuration argument
 */
static long ipipe_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	int ret = 0;

	switch (cmd) {
	case VIDIOC_VPFE_IPIPE_S_CONFIG:
		ret = ipipe_s_config(sd, arg);
		break;

	case VIDIOC_VPFE_IPIPE_G_CONFIG:
		ret = ipipe_g_config(sd, arg);
		break;

	default:
		ret = -ENOIOCTLCMD;
	}
	return ret;
}

void vpfe_ipipe_enable(struct vpfe_device *vpfe_dev, int en)
{
	struct vpfe_ipipeif_device *ipipeif = &vpfe_dev->vpfe_ipipeif;
	struct vpfe_ipipe_device *ipipe = &vpfe_dev->vpfe_ipipe;
	unsigned char val;

	if (ipipe->input == IPIPE_INPUT_NONE)
		return;

	/* ipipe is set to single shot */
	if (ipipeif->input == IPIPEIF_INPUT_MEMORY && en) {
		/* for single-shot mode, need to wait for h/w to
		 * reset many register bits
		 */
		do {
			val = regr_ip(vpfe_dev->vpfe_ipipe.base_addr,
				      IPIPE_SRC_EN);
		} while (val);
	}
	regw_ip(vpfe_dev->vpfe_ipipe.base_addr, en, IPIPE_SRC_EN);
}

/*
 * ipipe_set_stream() - Enable/Disable streaming on the ipipe subdevice
 * @sd: pointer to v4l2 subdev structure
 * @enable: 1 == Enable, 0 == Disable
 */
static int ipipe_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct vpfe_ipipe_device *ipipe = v4l2_get_subdevdata(sd);
	struct vpfe_device *vpfe_dev = to_vpfe_device(ipipe);

	if (enable && ipipe->input != IPIPE_INPUT_NONE &&
		ipipe->output != IPIPE_OUTPUT_NONE) {
		if (config_ipipe_hw(ipipe) < 0)
			return -EINVAL;
	}

	vpfe_ipipe_enable(vpfe_dev, enable);

	return 0;
}

/*
 * __ipipe_get_format() - helper function for getting ipipe format
 * @ipipe: pointer to ipipe private structure.
 * @pad: pad number.
 * @cfg: V4L2 subdev pad config
 * @which: wanted subdev format.
 *
 */
static struct v4l2_mbus_framefmt *
__ipipe_get_format(struct vpfe_ipipe_device *ipipe,
		       struct v4l2_subdev_pad_config *cfg, unsigned int pad,
		       enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&ipipe->subdev, cfg, pad);

	return &ipipe->formats[pad];
}

/*
 * ipipe_try_format() - Handle try format by pad subdev method
 * @ipipe: VPFE ipipe device.
 * @cfg: V4L2 subdev pad config
 * @pad: pad num.
 * @fmt: pointer to v4l2 format structure.
 * @which : wanted subdev format
 */
static void
ipipe_try_format(struct vpfe_ipipe_device *ipipe,
		   struct v4l2_subdev_pad_config *cfg, unsigned int pad,
		   struct v4l2_mbus_framefmt *fmt,
		   enum v4l2_subdev_format_whence which)
{
	unsigned int max_out_height;
	unsigned int max_out_width;
	unsigned int i;

	max_out_width = IPIPE_MAX_OUTPUT_WIDTH_A;
	max_out_height = IPIPE_MAX_OUTPUT_HEIGHT_A;

	if (pad == IPIPE_PAD_SINK) {
		for (i = 0; i < ARRAY_SIZE(ipipe_input_fmts); i++)
			if (fmt->code == ipipe_input_fmts[i])
				break;

		/* If not found, use SBGGR10 as default */
		if (i >= ARRAY_SIZE(ipipe_input_fmts))
			fmt->code = MEDIA_BUS_FMT_SGRBG12_1X12;
	} else if (pad == IPIPE_PAD_SOURCE) {
		for (i = 0; i < ARRAY_SIZE(ipipe_output_fmts); i++)
			if (fmt->code == ipipe_output_fmts[i])
				break;

		/* If not found, use UYVY as default */
		if (i >= ARRAY_SIZE(ipipe_output_fmts))
			fmt->code = MEDIA_BUS_FMT_UYVY8_2X8;
	}

	fmt->width = clamp_t(u32, fmt->width, MIN_OUT_HEIGHT, max_out_width);
	fmt->height = clamp_t(u32, fmt->height, MIN_OUT_WIDTH, max_out_height);
}

/*
 * ipipe_set_format() - Handle set format by pads subdev method
 * @sd: pointer to v4l2 subdev structure
 * @cfg: V4L2 subdev pad config
 * @fmt: pointer to v4l2 subdev format structure
 * return -EINVAL or zero on success
 */
static int
ipipe_set_format(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
		     struct v4l2_subdev_format *fmt)
{
	struct vpfe_ipipe_device *ipipe = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __ipipe_get_format(ipipe, cfg, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	ipipe_try_format(ipipe, cfg, fmt->pad, &fmt->format, fmt->which);
	*format = fmt->format;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	if (fmt->pad == IPIPE_PAD_SINK &&
	   (ipipe->input == IPIPE_INPUT_CCDC ||
	    ipipe->input == IPIPE_INPUT_MEMORY))
		ipipe->formats[fmt->pad] = fmt->format;
	else if (fmt->pad == IPIPE_PAD_SOURCE &&
		ipipe->output == IPIPE_OUTPUT_RESIZER)
		ipipe->formats[fmt->pad] = fmt->format;
	else
		return -EINVAL;

	return 0;
}

/*
 * ipipe_get_format() - Handle get format by pads subdev method.
 * @sd: pointer to v4l2 subdev structure.
 * @cfg: V4L2 subdev pad config
 * @fmt: pointer to v4l2 subdev format structure.
 */
static int
ipipe_get_format(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
		     struct v4l2_subdev_format *fmt)
{
	struct vpfe_ipipe_device *ipipe = v4l2_get_subdevdata(sd);

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		fmt->format = ipipe->formats[fmt->pad];
	else
		fmt->format = *(v4l2_subdev_get_try_format(sd, cfg, fmt->pad));

	return 0;
}

/*
 * ipipe_enum_frame_size() - enum frame sizes on pads
 * @sd: pointer to v4l2 subdev structure.
 * @cfg: V4L2 subdev pad config
 * @fse: pointer to v4l2_subdev_frame_size_enum structure.
 */
static int
ipipe_enum_frame_size(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_frame_size_enum *fse)
{
	struct vpfe_ipipe_device *ipipe = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt format;

	if (fse->index != 0)
		return -EINVAL;

	format.code = fse->code;
	format.width = 1;
	format.height = 1;
	ipipe_try_format(ipipe, cfg, fse->pad, &format,
			   V4L2_SUBDEV_FORMAT_TRY);
	fse->min_width = format.width;
	fse->min_height = format.height;

	if (format.code != fse->code)
		return -EINVAL;

	format.code = fse->code;
	format.width = -1;
	format.height = -1;
	ipipe_try_format(ipipe, cfg, fse->pad, &format,
			   V4L2_SUBDEV_FORMAT_TRY);
	fse->max_width = format.width;
	fse->max_height = format.height;

	return 0;
}

/*
 * ipipe_enum_mbus_code() - enum mbus codes for pads
 * @sd: pointer to v4l2 subdev structure.
 * @cfg: V4L2 subdev pad config
 * @code: pointer to v4l2_subdev_mbus_code_enum structure
 */
static int
ipipe_enum_mbus_code(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
		     struct v4l2_subdev_mbus_code_enum *code)
{
	switch (code->pad) {
	case IPIPE_PAD_SINK:
		if (code->index >= ARRAY_SIZE(ipipe_input_fmts))
			return -EINVAL;
		code->code = ipipe_input_fmts[code->index];
		break;

	case IPIPE_PAD_SOURCE:
		if (code->index >= ARRAY_SIZE(ipipe_output_fmts))
			return -EINVAL;
		code->code = ipipe_output_fmts[code->index];
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * ipipe_s_ctrl() - Handle set control subdev method
 * @ctrl: pointer to v4l2 control structure
 */
static int ipipe_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vpfe_ipipe_device *ipipe =
	     container_of(ctrl->handler, struct vpfe_ipipe_device, ctrls);
	struct ipipe_lum_adj *lum_adj = &ipipe->config.lum_adj;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		lum_adj->brightness = ctrl->val;
		ipipe_set_lum_adj_regs(ipipe->base_addr, lum_adj);
		break;

	case V4L2_CID_CONTRAST:
		lum_adj->contrast = ctrl->val;
		ipipe_set_lum_adj_regs(ipipe->base_addr, lum_adj);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * ipipe_init_formats() - Initialize formats on all pads
 * @sd: pointer to v4l2 subdev structure.
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. Try formats are initialized
 * on the file handle.
 */
static int
ipipe_init_formats(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format;

	memset(&format, 0, sizeof(format));
	format.pad = IPIPE_PAD_SINK;
	format.which = V4L2_SUBDEV_FORMAT_TRY;
	format.format.code = MEDIA_BUS_FMT_SGRBG12_1X12;
	format.format.width = IPIPE_MAX_OUTPUT_WIDTH_A;
	format.format.height = IPIPE_MAX_OUTPUT_HEIGHT_A;
	ipipe_set_format(sd, fh->pad, &format);

	memset(&format, 0, sizeof(format));
	format.pad = IPIPE_PAD_SOURCE;
	format.which = V4L2_SUBDEV_FORMAT_TRY;
	format.format.code = MEDIA_BUS_FMT_UYVY8_2X8;
	format.format.width = IPIPE_MAX_OUTPUT_WIDTH_A;
	format.format.height = IPIPE_MAX_OUTPUT_HEIGHT_A;
	ipipe_set_format(sd, fh->pad, &format);

	return 0;
}

/* subdev core operations */
static const struct v4l2_subdev_core_ops ipipe_v4l2_core_ops = {
	.ioctl = ipipe_ioctl,
};

static const struct v4l2_ctrl_ops ipipe_ctrl_ops = {
	.s_ctrl = ipipe_s_ctrl,
};

/* subdev file operations */
static const struct  v4l2_subdev_internal_ops ipipe_v4l2_internal_ops = {
	.open = ipipe_init_formats,
};

/* subdev video operations */
static const struct v4l2_subdev_video_ops ipipe_v4l2_video_ops = {
	.s_stream = ipipe_set_stream,
};

/* subdev pad operations */
static const struct v4l2_subdev_pad_ops ipipe_v4l2_pad_ops = {
	.enum_mbus_code = ipipe_enum_mbus_code,
	.enum_frame_size = ipipe_enum_frame_size,
	.get_fmt = ipipe_get_format,
	.set_fmt = ipipe_set_format,
};

/* v4l2 subdev operation */
static const struct v4l2_subdev_ops ipipe_v4l2_ops = {
	.core = &ipipe_v4l2_core_ops,
	.video = &ipipe_v4l2_video_ops,
	.pad = &ipipe_v4l2_pad_ops,
};

/*
 * Media entity operations
 */

/*
 * ipipe_link_setup() - Setup ipipe connections
 * @entity: ipipe media entity
 * @local: Pad at the local end of the link
 * @remote: Pad at the remote end of the link
 * @flags: Link flags
 *
 * return -EINVAL or zero on success
 */
static int
ipipe_link_setup(struct media_entity *entity, const struct media_pad *local,
		     const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct vpfe_ipipe_device *ipipe = v4l2_get_subdevdata(sd);
	struct vpfe_device *vpfe_dev = to_vpfe_device(ipipe);
	u16 ipipeif_sink = vpfe_dev->vpfe_ipipeif.input;

	switch (local->index | media_entity_type(remote->entity)) {
	case IPIPE_PAD_SINK | MEDIA_ENT_T_V4L2_SUBDEV:
		if (!(flags & MEDIA_LNK_FL_ENABLED)) {
			ipipe->input = IPIPE_INPUT_NONE;
			break;
		}
		if (ipipe->input != IPIPE_INPUT_NONE)
			return -EBUSY;
		if (ipipeif_sink == IPIPEIF_INPUT_MEMORY)
			ipipe->input = IPIPE_INPUT_MEMORY;
		else
			ipipe->input = IPIPE_INPUT_CCDC;
		break;

	case IPIPE_PAD_SOURCE | MEDIA_ENT_T_V4L2_SUBDEV:
		/* out to RESIZER */
		if (flags & MEDIA_LNK_FL_ENABLED)
			ipipe->output = IPIPE_OUTPUT_RESIZER;
		else
			ipipe->output = IPIPE_OUTPUT_NONE;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static const struct media_entity_operations ipipe_media_ops = {
	.link_setup = ipipe_link_setup,
};

/*
 * vpfe_ipipe_unregister_entities() - ipipe unregister entity
 * @vpfe_ipipe: pointer to ipipe subdevice structure.
 */
void vpfe_ipipe_unregister_entities(struct vpfe_ipipe_device *vpfe_ipipe)
{
	/* unregister subdev */
	v4l2_device_unregister_subdev(&vpfe_ipipe->subdev);
	/* cleanup entity */
	media_entity_cleanup(&vpfe_ipipe->subdev.entity);
}

/*
 * vpfe_ipipe_register_entities() - ipipe register entity
 * @ipipe: pointer to ipipe subdevice structure.
 * @vdev: pointer to v4l2 device structure.
 */
int
vpfe_ipipe_register_entities(struct vpfe_ipipe_device *ipipe,
				 struct v4l2_device *vdev)
{
	int ret;

	/* Register the subdev */
	ret = v4l2_device_register_subdev(vdev, &ipipe->subdev);
	if (ret) {
		pr_err("Failed to register ipipe as v4l2 subdevice\n");
		return ret;
	}

	return ret;
}

#define IPIPE_CONTRAST_HIGH		0xff
#define IPIPE_BRIGHT_HIGH		0xff

/*
 * vpfe_ipipe_init() - ipipe module initialization.
 * @ipipe: pointer to ipipe subdevice structure.
 * @pdev: platform device pointer.
 */
int
vpfe_ipipe_init(struct vpfe_ipipe_device *ipipe, struct platform_device *pdev)
{
	struct media_pad *pads = &ipipe->pads[0];
	struct v4l2_subdev *sd = &ipipe->subdev;
	struct media_entity *me = &sd->entity;
	static resource_size_t  res_len;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 4);
	if (!res)
		return -ENOENT;

	res_len = resource_size(res);
	res = request_mem_region(res->start, res_len, res->name);
	if (!res)
		return -EBUSY;
	ipipe->base_addr = ioremap_nocache(res->start, res_len);
	if (!ipipe->base_addr)
		return -EBUSY;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 6);
	if (!res)
		return -ENOENT;
	ipipe->isp5_base_addr = ioremap_nocache(res->start, res_len);
	if (!ipipe->isp5_base_addr)
		return -EBUSY;

	v4l2_subdev_init(sd, &ipipe_v4l2_ops);
	sd->internal_ops = &ipipe_v4l2_internal_ops;
	strlcpy(sd->name, "DAVINCI IPIPE", sizeof(sd->name));
	sd->grp_id = 1 << 16;	/* group ID for davinci subdevs */
	v4l2_set_subdevdata(sd, ipipe);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	pads[IPIPE_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[IPIPE_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	ipipe->input = IPIPE_INPUT_NONE;
	ipipe->output = IPIPE_OUTPUT_NONE;

	me->ops = &ipipe_media_ops;
	v4l2_ctrl_handler_init(&ipipe->ctrls, 2);
	v4l2_ctrl_new_std(&ipipe->ctrls, &ipipe_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, 0,
			  IPIPE_BRIGHT_HIGH, 1, 16);
	v4l2_ctrl_new_std(&ipipe->ctrls, &ipipe_ctrl_ops,
			  V4L2_CID_CONTRAST, 0,
			  IPIPE_CONTRAST_HIGH, 1, 16);


	v4l2_ctrl_handler_setup(&ipipe->ctrls);
	sd->ctrl_handler = &ipipe->ctrls;

	return media_entity_init(me, IPIPE_PADS_NUM, pads, 0);
}

/*
 * vpfe_ipipe_cleanup() - ipipe subdevice cleanup.
 * @ipipe: pointer to ipipe subdevice
 * @dev: pointer to platform device
 */
void vpfe_ipipe_cleanup(struct vpfe_ipipe_device *ipipe,
			struct platform_device *pdev)
{
	struct resource *res;

	v4l2_ctrl_handler_free(&ipipe->ctrls);

	iounmap(ipipe->base_addr);
	iounmap(ipipe->isp5_base_addr);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 4);
	if (res)
		release_mem_region(res->start, resource_size(res));
}
