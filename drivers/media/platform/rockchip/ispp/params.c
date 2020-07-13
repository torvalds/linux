// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mc.h>
#include <linux/rkisp1-config.h>
#include "dev.h"
#include "regs.h"

#define RKISP1_ISP_PARAMS_REQ_BUFS_MIN	2
#define RKISP1_ISP_PARAMS_REQ_BUFS_MAX	8

#define ISPP_PACK_4BIT(a, b, c, d, e, f, g, h) \
	(((a) & 0xf) << 0 | ((b) & 0xf) << 4 | \
	 ((c) & 0xf) << 8 | ((d) & 0xf) << 12 | \
	 ((e) & 0xf) << 16 | ((f) & 0xf) << 20 | \
	 ((g) & 0xf) << 24 | ((h) & 0xf) << 28)

#define ISPP_PACK_4BYTE(a, b, c, d) \
	(((a) & 0xFF) << 0 | ((b) & 0xFF) << 8 | \
	 ((c) & 0xFF) << 16 | ((d) & 0xFF) << 24)

#define ISPP_PACK_2SHORT(a, b) \
	(((a) & 0xFFFF) << 0 | ((b) & 0xFFFF) << 16)

#define ISPP_PARAM_UD_CK \
	(ISPP_MODULE_TNR | \
	 ISPP_MODULE_NR | \
	 ISPP_MODULE_SHP | \
	 ISPP_MODULE_ORB)

#define ISPP_NOBIG_OVERFLOW_SIZE	(2560 * 1440)

static inline size_t get_input_size(struct rkispp_params_vdev *params_vdev)
{
	struct rkispp_device *dev = params_vdev->dev;
	struct rkispp_subdev *isp_sdev = &dev->ispp_sdev;

	return isp_sdev->out_fmt.width * isp_sdev->out_fmt.height;
}

static void tnr_config(struct rkispp_params_vdev *params_vdev,
		       struct rkispp_tnr_config *arg)
{
	void __iomem *base = params_vdev->dev->base_addr;
	u32 i, val;

	val = arg->opty_en << 2 | arg->optc_en << 3 |
		arg->gain_en << 4;
	rkispp_set_bits(base + RKISPP_TNR_CORE_CTRL,
			SW_TNR_OPTY_EN | SW_TNR_OPTC_EN |
			SW_TNR_GLB_GAIN_EN, val);

	val = ISPP_PACK_4BYTE(arg->pk0_y, arg->pk1_y,
		arg->pk0_c, arg->pk1_c);
	rkispp_write(base + RKISPP_TNR_CORE_PK0, val);

	val = ISPP_PACK_2SHORT(arg->glb_gain_cur, arg->glb_gain_nxt);
	rkispp_write(base + RKISPP_TNR_CORE_GLB_GAIN, val);
	val = ISPP_PACK_2SHORT(arg->glb_gain_cur_div, arg->glb_gain_cur_sqrt);
	rkispp_write(base + RKISPP_TNR_CORE_GLB_GAIN_DIV, val);

	for (i = 0; i < TNR_SIGMA_CURVE_SIZE - 1; i += 2)
		rkispp_write(base + RKISPP_TNR_CORE_SIG_Y01 + i * 2,
			ISPP_PACK_2SHORT(arg->sigma_y[i], arg->sigma_y[i + 1]));
	rkispp_write(base + RKISPP_TNR_CORE_SIG_Y10, arg->sigma_y[16]);
	rkispp_write(base + RKISPP_TNR_CORE_SIG_X18,
		ISPP_PACK_4BIT(arg->sigma_x[0], arg->sigma_x[1],
			arg->sigma_x[2], arg->sigma_x[3],
			arg->sigma_x[4], arg->sigma_x[5],
			arg->sigma_x[6], arg->sigma_x[7]));
	rkispp_write(base + RKISPP_TNR_CORE_SIG_X910,
		ISPP_PACK_4BIT(arg->sigma_x[8], arg->sigma_x[9],
			arg->sigma_x[10], arg->sigma_x[11],
			arg->sigma_x[12], arg->sigma_x[13],
			arg->sigma_x[14], arg->sigma_x[15]));

	for (i = 0; i < TNR_LUMA_CURVE_SIZE; i += 2) {
		val = ISPP_PACK_2SHORT(arg->luma_curve[i], arg->luma_curve[i + 1]);
		rkispp_write(base + RKISPP_TNR_CORE_LUMACURVE_Y01 + i * 2, val);
	}

	val = ISPP_PACK_2SHORT(arg->txt_th0_y, arg->txt_th1_y);
	rkispp_write(base + RKISPP_TNR_CORE_TH_Y, val);
	val = ISPP_PACK_2SHORT(arg->txt_th0_c, arg->txt_th1_c);
	rkispp_write(base + RKISPP_TNR_CORE_TH_C, val);
	val = ISPP_PACK_2SHORT(arg->txt_thy_dlt, arg->txt_thc_dlt);
	rkispp_write(base + RKISPP_TNR_CORE_TH_DLT, val);

	val = ISPP_PACK_4BYTE(arg->gfcoef_y0[0], arg->gfcoef_y0[1],
		arg->gfcoef_y0[2], arg->gfcoef_y0[3]);
	rkispp_write(base + RKISPP_TNR_CORE_GFCOEF_Y0_0, val);
	val = ISPP_PACK_4BYTE(arg->gfcoef_y0[4], arg->gfcoef_y0[5], 0, 0);
	rkispp_write(base + RKISPP_TNR_CORE_GFCOEF_Y0_1, val);
	val = ISPP_PACK_4BYTE(arg->gfcoef_y1[0], arg->gfcoef_y1[1],
		arg->gfcoef_y1[2], 0);
	rkispp_write(base + RKISPP_TNR_CORE_GFCOEF_Y1, val);
	val = ISPP_PACK_4BYTE(arg->gfcoef_y2[0], arg->gfcoef_y2[1],
		arg->gfcoef_y2[2], 0);
	rkispp_write(base + RKISPP_TNR_CORE_GFCOEF_Y2, val);
	val = ISPP_PACK_4BYTE(arg->gfcoef_y3[0], arg->gfcoef_y3[1],
		arg->gfcoef_y3[2], 0);
	rkispp_write(base + RKISPP_TNR_CORE_GFCOEF_Y3, val);

	val = ISPP_PACK_4BYTE(arg->gfcoef_yg0[0], arg->gfcoef_yg0[1],
		arg->gfcoef_yg0[2], arg->gfcoef_yg0[3]);
	rkispp_write(base + RKISPP_TNR_CORE_GFCOEF_YG0_0, val);
	val = ISPP_PACK_4BYTE(arg->gfcoef_yg0[4], arg->gfcoef_yg0[5], 0, 0);
	rkispp_write(base + RKISPP_TNR_CORE_GFCOEF_YG0_1, val);
	val = ISPP_PACK_4BYTE(arg->gfcoef_yg1[0], arg->gfcoef_yg1[1],
		arg->gfcoef_yg1[2], 0);
	rkispp_write(base + RKISPP_TNR_CORE_GFCOEF_YG1, val);
	val = ISPP_PACK_4BYTE(arg->gfcoef_yg2[0], arg->gfcoef_yg2[1],
		arg->gfcoef_yg2[2], 0);
	rkispp_write(base + RKISPP_TNR_CORE_GFCOEF_YG2, val);
	val = ISPP_PACK_4BYTE(arg->gfcoef_yg3[0], arg->gfcoef_yg3[1],
		arg->gfcoef_yg3[2], 0);
	rkispp_write(base + RKISPP_TNR_CORE_GFCOEF_YG3, val);

	val = ISPP_PACK_4BYTE(arg->gfcoef_yl0[0], arg->gfcoef_yl0[1],
		arg->gfcoef_yl0[2], arg->gfcoef_yl0[3]);
	rkispp_write(base + RKISPP_TNR_CORE_GFCOEF_YL0_0, val);
	val = ISPP_PACK_4BYTE(arg->gfcoef_yl0[4], arg->gfcoef_yl0[5], 0, 0);
	rkispp_write(base + RKISPP_TNR_CORE_GFCOEF_YL0_1, val);
	val = ISPP_PACK_4BYTE(arg->gfcoef_yl1[0], arg->gfcoef_yl1[1],
		arg->gfcoef_yl1[2], 0);
	rkispp_write(base + RKISPP_TNR_CORE_GFCOEF_YL1, val);
	val = ISPP_PACK_4BYTE(arg->gfcoef_yl2[0], arg->gfcoef_yl2[1],
		arg->gfcoef_yl2[2], 0);
	rkispp_write(base + RKISPP_TNR_CORE_GFCOEF_YL2, val);

	val = ISPP_PACK_4BYTE(arg->gfcoef_cg0[0], arg->gfcoef_cg0[1],
		arg->gfcoef_cg0[2], arg->gfcoef_cg0[3]);
	rkispp_write(base + RKISPP_TNR_CORE_GFCOEF_CG0_0, val);
	val = ISPP_PACK_4BYTE(arg->gfcoef_cg0[4], arg->gfcoef_cg0[5], 0, 0);
	rkispp_write(base + RKISPP_TNR_CORE_GFCOEF_CG0_1, val);
	val = ISPP_PACK_4BYTE(arg->gfcoef_cg1[0], arg->gfcoef_cg1[1],
		arg->gfcoef_cg1[2], 0);
	rkispp_write(base + RKISPP_TNR_CORE_GFCOEF_CG1, val);
	val = ISPP_PACK_4BYTE(arg->gfcoef_cg2[0], arg->gfcoef_cg2[1],
		arg->gfcoef_cg2[2], 0);
	rkispp_write(base + RKISPP_TNR_CORE_GFCOEF_CG2, val);

	val = ISPP_PACK_4BYTE(arg->gfcoef_cl0[0], arg->gfcoef_cl0[1],
		arg->gfcoef_cl0[2], arg->gfcoef_cl0[3]);
	rkispp_write(base + RKISPP_TNR_CORE_GFCOEF_CL0_0, val);
	val = ISPP_PACK_4BYTE(arg->gfcoef_cl0[4], arg->gfcoef_cl0[5], 0, 0);
	rkispp_write(base + RKISPP_TNR_CORE_GFCOEF_CL0_1, val);
	val = ISPP_PACK_4BYTE(arg->gfcoef_cl1[0], arg->gfcoef_cl1[1],
		arg->gfcoef_cl1[2], 0);
	rkispp_write(base + RKISPP_TNR_CORE_GFCOEF_CL1, val);

	val = ISPP_PACK_2SHORT(arg->scale_yg[0], arg->scale_yg[1]);
	rkispp_write(base + RKISPP_TNR_CORE_SCALE_YG01, val);
	val = ISPP_PACK_2SHORT(arg->scale_yg[2], arg->scale_yg[3]);
	rkispp_write(base + RKISPP_TNR_CORE_SCALE_YG23, val);
	val = ISPP_PACK_2SHORT(arg->scale_yl[0], arg->scale_yl[1]);
	rkispp_write(base + RKISPP_TNR_CORE_SCALE_YL01, val);
	rkispp_write(base + RKISPP_TNR_CORE_SCALE_YL2, arg->scale_yl[2]);
	val = ISPP_PACK_2SHORT(arg->scale_cg[0], arg->scale_y2cg[0]);
	rkispp_write(base + RKISPP_TNR_CORE_SCALE_CG0, val);
	val = ISPP_PACK_2SHORT(arg->scale_cg[1], arg->scale_y2cg[1]);
	rkispp_write(base + RKISPP_TNR_CORE_SCALE_CG1, val);
	val = ISPP_PACK_2SHORT(arg->scale_cg[2], arg->scale_y2cg[2]);
	rkispp_write(base + RKISPP_TNR_CORE_SCALE_CG2, val);
	val = ISPP_PACK_2SHORT(arg->scale_cl[0], arg->scale_y2cl[0]);
	rkispp_write(base + RKISPP_TNR_CORE_SCALE_CL0, val);
	val = ISPP_PACK_2SHORT(arg->scale_cl[1], arg->scale_y2cl[1]);
	rkispp_write(base + RKISPP_TNR_CORE_SCALE_CL1, val);
	val = arg->scale_y2cl[2] << 16;
	rkispp_write(base + RKISPP_TNR_CORE_SCALE_CL2, val);
	val = ISPP_PACK_4BYTE(arg->weight_y[0], arg->weight_y[1],
		arg->weight_y[2], 0);
	rkispp_write(base + RKISPP_TNR_CORE_WEIGHT, val);
}

static void tnr_enable(struct rkispp_params_vdev *params_vdev, bool en)
{
	void __iomem *base = params_vdev->dev->base_addr;

	rkispp_set_bits(base + RKISPP_TNR_CORE_CTRL, SW_TNR_EN, en);
}

static void nr_config(struct rkispp_params_vdev *params_vdev,
		      struct rkispp_nr_config *arg)
{
	void __iomem *base = params_vdev->dev->base_addr;
	u32 i, val;

	rkispp_write(base + RKISPP_NR_UVNR_GAIN_1SIGMA,
		arg->uvnr_gain_1sigma);
	rkispp_write(base + RKISPP_NR_UVNR_GAIN_OFFSET,
		arg->uvnr_gain_offset);
	val = ISPP_PACK_4BYTE(arg->uvnr_gain_uvgain[0],
		arg->uvnr_gain_uvgain[1], arg->uvnr_gain_t2gen,
		arg->uvnr_gain_iso);
	rkispp_write(base + RKISPP_NR_UVNR_GAIN_GBLGAIN, val);
	rkispp_write(base + RKISPP_NR_UVNR_T1GEN_M3ALPHA,
		arg->uvnr_t1gen_m3alpha);
	rkispp_write(base + RKISPP_NR_UVNR_T1FLT_MODE,
		arg->uvnr_t1flt_mode);
	rkispp_write(base + RKISPP_NR_UVNR_T1FLT_MSIGMA,
		arg->uvnr_t1flt_msigma);
	rkispp_write(base + RKISPP_NR_UVNR_T1FLT_WTP,
		arg->uvnr_t1flt_wtp);
	for (i = 0; i < NR_UVNR_T1FLT_WTQ_SIZE; i += 4) {
		val = ISPP_PACK_4BYTE(arg->uvnr_t1flt_wtq[i],
			arg->uvnr_t1flt_wtq[i + 1], arg->uvnr_t1flt_wtq[i + 2],
			arg->uvnr_t1flt_wtq[i + 3]);
		rkispp_write(base + RKISPP_NR_UVNR_T1FLT_WTQ0 + i, val);
	}
	rkispp_write(base + RKISPP_NR_UVNR_T2GEN_M3ALPHA,
		arg->uvnr_t2gen_m3alpha);
	rkispp_write(base + RKISPP_NR_UVNR_T2GEN_MSIGMA,
		arg->uvnr_t2gen_msigma);
	rkispp_write(base + RKISPP_NR_UVNR_T2GEN_WTP,
		arg->uvnr_t2gen_wtp);
	val = ISPP_PACK_4BYTE(arg->uvnr_t2gen_wtq[0],
		arg->uvnr_t2gen_wtq[1], arg->uvnr_t2gen_wtq[2],
		arg->uvnr_t2gen_wtq[3]);
	rkispp_write(base + RKISPP_NR_UVNR_T2GEN_WTQ, val);
	rkispp_write(base + RKISPP_NR_UVNR_T2FLT_MSIGMA,
		arg->uvnr_t2flt_msigma);
	val = ISPP_PACK_4BYTE(arg->uvnr_t2flt_wtp,
		arg->uvnr_t2flt_wt[0], arg->uvnr_t2flt_wt[1],
		arg->uvnr_t2flt_wt[2]);
	rkispp_write(base + RKISPP_NR_UVNR_T2FLT_WT, val);

	val = ISPP_PACK_4BIT(arg->ynr_sgm_dx[0], arg->ynr_sgm_dx[1],
		arg->ynr_sgm_dx[2], arg->ynr_sgm_dx[3],
		arg->ynr_sgm_dx[4], arg->ynr_sgm_dx[5],
		arg->ynr_sgm_dx[6], arg->ynr_sgm_dx[7]);
	rkispp_write(base + RKISPP_NR_YNR_SGM_DX_1_8, val);
	val = ISPP_PACK_4BIT(arg->ynr_sgm_dx[8], arg->ynr_sgm_dx[9],
		arg->ynr_sgm_dx[10], arg->ynr_sgm_dx[11],
		arg->ynr_sgm_dx[12], arg->ynr_sgm_dx[13],
		arg->ynr_sgm_dx[14], arg->ynr_sgm_dx[15]);
	rkispp_write(base + RKISPP_NR_YNR_SGM_DX_9_16, val);

	for (i = 0; i < NR_YNR_SGM_Y_SIZE - 1; i += 2) {
		rkispp_write(base + RKISPP_NR_YNR_LSGM_Y_0_1 + i * 2,
			ISPP_PACK_2SHORT(arg->ynr_lsgm_y[i], arg->ynr_lsgm_y[i + 1]));

		rkispp_write(base + RKISPP_NR_YNR_HSGM_Y_0_1 + i * 2,
			ISPP_PACK_2SHORT(arg->ynr_hsgm_y[i], arg->ynr_hsgm_y[i + 1]));
	}
	rkispp_write(base + RKISPP_NR_YNR_LSGM_Y_16, arg->ynr_lsgm_y[16]);
	rkispp_write(base + RKISPP_NR_YNR_HSGM_Y_16, arg->ynr_hsgm_y[16]);

	val = ISPP_PACK_4BYTE(arg->ynr_lci[0], arg->ynr_lci[1],
		arg->ynr_lci[2], arg->ynr_lci[3]);
	rkispp_write(base + RKISPP_NR_YNR_LCI, val);
	val = ISPP_PACK_4BYTE(arg->ynr_lgain_min[0], arg->ynr_lgain_min[1],
		arg->ynr_lgain_min[2], arg->ynr_lgain_min[3]);
	rkispp_write(base + RKISPP_NR_YNR_LGAIN_DIRE_MIN, val);
	rkispp_write(base + RKISPP_NR_YNR_IGAIN_DIRE_MAX, arg->ynr_lgain_max);
	val = ISPP_PACK_4BYTE(arg->ynr_lmerge_bound, arg->ynr_lmerge_ratio, 0, 0);
	rkispp_write(base + RKISPP_NR_YNR_LMERGE, val);
	val = ISPP_PACK_4BYTE(arg->ynr_lweit_flt[0], arg->ynr_lweit_flt[1],
		arg->ynr_lweit_flt[2], arg->ynr_lweit_flt[3]);
	rkispp_write(base + RKISPP_NR_YNR_LWEIT_FLT, val);
	val = ISPP_PACK_4BYTE(arg->ynr_hlci[0], arg->ynr_hlci[1],
		arg->ynr_hlci[2], arg->ynr_hlci[3]);
	rkispp_write(base + RKISPP_NR_YNR_HLCI, val);
	val = ISPP_PACK_4BYTE(arg->ynr_lhci[0], arg->ynr_lhci[1],
		arg->ynr_lhci[2], arg->ynr_lhci[3]);
	rkispp_write(base + RKISPP_NR_YNR_LHCI, val);
	val = ISPP_PACK_4BYTE(arg->ynr_hhci[0], arg->ynr_hhci[1],
		arg->ynr_hhci[2], arg->ynr_hhci[3]);
	rkispp_write(base + RKISPP_NR_YNR_HHCI, val);
	val = ISPP_PACK_4BYTE(arg->ynr_hgain_sgm[0], arg->ynr_hgain_sgm[1],
		arg->ynr_hgain_sgm[2], arg->ynr_hgain_sgm[3]);
	rkispp_write(base + RKISPP_NR_YNR_HGAIN_SGM, val);

	for (i = 0; i < NR_YNR_HWEIT_D_SIZE; i += 4) {
		val = ISPP_PACK_4BYTE(arg->ynr_hweit_d[i], arg->ynr_hweit_d[i + 1],
			arg->ynr_hweit_d[i + 2], arg->ynr_hweit_d[i + 3]);
		rkispp_write(base + RKISPP_NR_YNR_HWEIT_D0 + i, val);
	}

	for (i = 0; i < NR_YNR_HGRAD_Y_SIZE; i += 4) {
		val = ISPP_PACK_4BYTE(arg->ynr_hgrad_y[i], arg->ynr_hgrad_y[i + 1],
			arg->ynr_hgrad_y[i + 2], arg->ynr_hgrad_y[i + 3]);
		rkispp_write(base + RKISPP_NR_YNR_HGRAD_Y0 + i, val);
	}

	val = ISPP_PACK_2SHORT(arg->ynr_hweit[0], arg->ynr_hweit[1]);
	rkispp_write(base + RKISPP_NR_YNR_HWEIT_1_2, val);
	val = ISPP_PACK_2SHORT(arg->ynr_hweit[2], arg->ynr_hweit[3]);
	rkispp_write(base + RKISPP_NR_YNR_HWEIT_3_4, val);

	rkispp_write(base + RKISPP_NR_YNR_HMAX_ADJUST, arg->ynr_hmax_adjust);
	rkispp_write(base + RKISPP_NR_YNR_HSTRENGTH, arg->ynr_hstrength);

	val = ISPP_PACK_4BYTE(arg->ynr_lweit_cmp[0], arg->ynr_lweit_cmp[1], 0, 0);
	rkispp_write(base + RKISPP_NR_YNR_LWEIT_CMP, val);
	rkispp_write(base + RKISPP_NR_YNR_LMAXGAIN_LV4, arg->ynr_lmaxgain_lv4);

	for (i = 0; i < NR_YNR_HSTV_Y_SIZE - 1; i += 2) {
		val = ISPP_PACK_2SHORT(arg->ynr_hstv_y[i], arg->ynr_hstv_y[i + 1]);
		rkispp_write(base + RKISPP_NR_YNR_HSTV_Y_0_1 + i * 2, val);
	}
	rkispp_write(base + RKISPP_NR_YNR_HSTV_Y_16, arg->ynr_hstv_y[16]);

	val = ISPP_PACK_2SHORT(arg->ynr_st_scale[0], arg->ynr_st_scale[1]);
	rkispp_write(base + RKISPP_NR_YNR_ST_SCALE_LV1_LV2, val);
	rkispp_write(base + RKISPP_NR_YNR_ST_SCALE_LV3, arg->ynr_st_scale[2]);
}

static void nr_enable(struct rkispp_params_vdev *params_vdev, bool en,
		      struct rkispp_nr_config *arg)
{
	void __iomem *base = params_vdev->dev->base_addr;
	u8 big_en, nobig_en;
	u32 val;

	big_en = arg->uvnr_big_en & 0x01;
	nobig_en = arg->uvnr_nobig_en & 0x01;
	if (get_input_size(params_vdev) > ISPP_NOBIG_OVERFLOW_SIZE) {
		big_en = 1;
		nobig_en = 0;
	}

	val = arg->uvnr_step1_en << 1 | arg->uvnr_step2_en << 2 |
	      arg->nr_gain_en << 3 | nobig_en << 5 | big_en << 6;

	if (en)
		val |= SW_NR_EN;

	rkispp_set_bits(base + RKISPP_NR_UVNR_CTRL_PARA,
			SW_UVNR_STEP1_ON | SW_UVNR_STEP2_ON |
			SW_NR_GAIN_BYPASS | SW_UVNR_NOBIG_EN |
			SW_UVNR_BIG_EN | SW_NR_EN, val);
}

static void shp_config(struct rkispp_params_vdev *params_vdev,
		       struct rkispp_sharp_config *arg)
{
	void __iomem *base = params_vdev->dev->base_addr;
	u32 i, val;

	rkispp_set_bits(base + RKISPP_SHARP_CTRL, SW_SHP_WR_ROT_MODE(3),
			SW_SHP_WR_ROT_MODE(arg->rotation));

	rkispp_write(base + RKISPP_SHARP_SC_DOWN,
		(arg->scl_down_v & 0x1) << 1 | (arg->scl_down_h & 0x1));

	rkispp_write(base + RKISPP_SHARP_TILE_IDX,
		(arg->tile_ycnt & 0x1F) << 8 | (arg->tile_xcnt & 0xFF));

	rkispp_write(base + RKISPP_SHARP_HBF_FACTOR, arg->hbf_ratio |
		arg->ehf_th << 16 | arg->pbf_ratio << 24);
	rkispp_write(base + RKISPP_SHARP_EDGE_TH, arg->edge_thed |
		arg->dir_min << 8 | arg->smoth_th4 << 16);
	val = ISPP_PACK_2SHORT(arg->l_alpha, arg->g_alpha);
	rkispp_write(base + RKISPP_SHARP_EDGE_ALPHA, val);
	val = ISPP_PACK_4BYTE(arg->pbf_k[0], arg->pbf_k[1], arg->pbf_k[2], 0);
	rkispp_write(base + RKISPP_SHARP_PBF_KERNEL, val);
	val = ISPP_PACK_4BYTE(arg->mrf_k[0], arg->mrf_k[1], arg->mrf_k[2], arg->mrf_k[3]);
	rkispp_write(base + RKISPP_SHARP_MRF_KERNEL0, val);
	val = ISPP_PACK_4BYTE(arg->mrf_k[4], arg->mrf_k[5], 0, 0);
	rkispp_write(base + RKISPP_SHARP_MRF_KERNEL1, val);

	for (i = 0; i < SHP_MBF_KERNEL_SIZE; i += 4) {
		val = ISPP_PACK_4BYTE(arg->mbf_k[i], arg->mbf_k[i + 1],
			arg->mbf_k[i + 2], arg->mbf_k[i + 3]);
		rkispp_write(base + RKISPP_SHARP_MBF_KERNEL0 + i, val);
	}

	val = ISPP_PACK_4BYTE(arg->hrf_k[0], arg->hrf_k[1], arg->hrf_k[2], arg->hrf_k[3]);
	rkispp_write(base + RKISPP_SHARP_HRF_KERNEL0, val);
	val = ISPP_PACK_4BYTE(arg->hrf_k[4], arg->hrf_k[5], 0, 0);
	rkispp_write(base + RKISPP_SHARP_HRF_KERNEL1, val);
	val = ISPP_PACK_4BYTE(arg->hbf_k[0], arg->hbf_k[1], arg->hbf_k[2], 0);
	rkispp_write(base + RKISPP_SHARP_HBF_KERNEL, val);

	val = ISPP_PACK_4BYTE(arg->eg_coef[0], arg->eg_coef[1], arg->eg_coef[2], 0);
	rkispp_write(base + RKISPP_SHARP_EDGE_COEF, val);
	val = ISPP_PACK_4BYTE(arg->eg_smoth[0], arg->eg_smoth[1], arg->eg_smoth[2], 0);
	rkispp_write(base + RKISPP_SHARP_EDGE_SMOTH, val);
	val = ISPP_PACK_4BYTE(arg->eg_gaus[0], arg->eg_gaus[1], arg->eg_gaus[2], arg->eg_gaus[3]);
	rkispp_write(base + RKISPP_SHARP_EDGE_GAUS0, val);
	val = ISPP_PACK_4BYTE(arg->eg_gaus[4], arg->eg_gaus[5], 0, 0);
	rkispp_write(base + RKISPP_SHARP_EDGE_GAUS1, val);

	val = ISPP_PACK_4BYTE(arg->dog_k[0], arg->dog_k[1], arg->dog_k[2], arg->dog_k[3]);
	rkispp_write(base + RKISPP_SHARP_DOG_KERNEL0, val);
	val = ISPP_PACK_4BYTE(arg->dog_k[4], arg->dog_k[5], 0, 0);
	rkispp_write(base + RKISPP_SHARP_DOG_KERNEL1, val);
	val = ISPP_PACK_4BYTE(arg->lum_point[0], arg->lum_point[1],
		arg->lum_point[2], arg->lum_point[3]);
	rkispp_write(base + RKISPP_SHARP_LUM_POINT0, val);
	val = ISPP_PACK_4BYTE(arg->lum_point[4], arg->lum_point[5], 0, 0);
	rkispp_write(base + RKISPP_SHARP_LUM_POINT1, val);

	val = ISPP_PACK_4BYTE(arg->pbf_shf_bits, arg->mbf_shf_bits, arg->hbf_shf_bits, 0);
	rkispp_write(base + RKISPP_SHARP_SHF_BITS, val);

	for (i = 0; i < SHP_SIGMA_SIZE; i += 4) {
		val = ISPP_PACK_4BYTE(arg->pbf_sigma[i], arg->pbf_sigma[i + 1],
			arg->pbf_sigma[i + 2], arg->pbf_sigma[i + 3]);
		rkispp_write(base + RKISPP_SHARP_PBF_SIGMA_INV0 + i, val);
		val = ISPP_PACK_4BYTE(arg->mbf_sigma[i], arg->mbf_sigma[i + 1],
			arg->mbf_sigma[i + 2], arg->mbf_sigma[i + 3]);
		rkispp_write(base + RKISPP_SHARP_MBF_SIGMA_INV0 + i, val);
		val = ISPP_PACK_4BYTE(arg->hbf_sigma[i], arg->hbf_sigma[i + 1],
			arg->hbf_sigma[i + 2], arg->hbf_sigma[i + 3]);
		rkispp_write(base + RKISPP_SHARP_HBF_SIGMA_INV0 + i, val);
	}

	for (i = 0; i < SHP_LUM_CLP_SIZE; i += 4) {
		val = ISPP_PACK_4BYTE(arg->lum_clp_m[i], arg->lum_clp_m[i + 1],
			arg->lum_clp_m[i + 2], arg->lum_clp_m[i + 3]);
		rkispp_write(base + RKISPP_SHARP_LUM_CLP_M0 + i, val);
		val = ISPP_PACK_4BYTE(arg->lum_clp_h[i], arg->lum_clp_h[i + 1],
			arg->lum_clp_h[i + 2], arg->lum_clp_h[i + 3]);
		rkispp_write(base + RKISPP_SHARP_LUM_CLP_H0 + i, val);
	}

	for (i = 0; i < SHP_LUM_MIN_SIZE; i += 4) {
		val = ISPP_PACK_4BYTE(arg->lum_min_m[i], arg->lum_min_m[i + 1],
			arg->lum_min_m[i + 2], arg->lum_min_m[i + 3]);
		rkispp_write(base + RKISPP_SHARP_LUM_MIN_M0 + i, val);
	}

	for (i = 0; i < SHP_EDGE_LUM_THED_SIZE; i += 4) {
		val = ISPP_PACK_4BYTE(arg->edge_lum_thed[i], arg->edge_lum_thed[i + 1],
			arg->edge_lum_thed[i + 2], arg->edge_lum_thed[i + 3]);
		rkispp_write(base + RKISPP_SHARP_EDGE_LUM_THED0 + i, val);
	}

	for (i = 0; i < SHP_CLAMP_SIZE; i += 4) {
		val = ISPP_PACK_4BYTE(arg->clamp_pos[i], arg->clamp_pos[i + 1],
			arg->clamp_pos[i + 2], arg->clamp_pos[i + 3]);
		rkispp_write(base + RKISPP_SHARP_CLAMP_POS_DOG0 + i, val);
		val = ISPP_PACK_4BYTE(arg->clamp_neg[i], arg->clamp_neg[i + 1],
			arg->clamp_neg[i + 2], arg->clamp_neg[i + 3]);
		rkispp_write(base + RKISPP_SHARP_CLAMP_NEG_DOG0 + i, val);
	}

	for (i = 0; i < SHP_DETAIL_ALPHA_SIZE; i += 4) {
		val = ISPP_PACK_4BYTE(arg->detail_alpha[i], arg->detail_alpha[i + 1],
			arg->detail_alpha[i + 2], arg->detail_alpha[i + 3]);
		rkispp_write(base + RKISPP_SHARP_DETAIL_ALPHA_DOG0 + i, val);
	}

	val = ISPP_PACK_2SHORT(arg->rfl_ratio, arg->rfh_ratio);
	rkispp_write(base + RKISPP_SHARP_RF_RATIO, val);

	val = ISPP_PACK_4BYTE(arg->m_ratio, arg->h_ratio, 0, 0);
	rkispp_write(base + RKISPP_SHARP_GRAD_RATIO, val);
}

static void shp_enable(struct rkispp_params_vdev *params_vdev, bool en,
		       struct rkispp_sharp_config *arg)
{
	void __iomem *base = params_vdev->dev->base_addr;
	u32 ens = params_vdev->dev->stream_vdev.module_ens;
	u32 val;

	if (en && !(ens & ISPP_MODULE_FEC)) {
		rkispp_set_bits(base + RKISPP_SCL0_CTRL,
				SW_SCL_FIRST_MODE, SW_SCL_FIRST_MODE);
		rkispp_set_bits(base + RKISPP_SCL1_CTRL,
				SW_SCL_FIRST_MODE, SW_SCL_FIRST_MODE);
		rkispp_set_bits(base + RKISPP_SCL2_CTRL,
				SW_SCL_FIRST_MODE, SW_SCL_FIRST_MODE);
	} else {
		rkispp_clear_bits(base + RKISPP_SCL0_CTRL, SW_SCL_FIRST_MODE);
		rkispp_clear_bits(base + RKISPP_SCL1_CTRL, SW_SCL_FIRST_MODE);
		rkispp_clear_bits(base + RKISPP_SCL2_CTRL, SW_SCL_FIRST_MODE);
	}

	val = arg->alpha_adp_en << 1 | arg->yin_flt_en << 3 |
	      arg->edge_avg_en << 4;
	if (en)
		val |= SW_SHP_EN;
	rkispp_set_bits(base + RKISPP_SHARP_CORE_CTRL,
			SW_SHP_ALPHA_ADP_EN | SW_SHP_YIN_FLT_EN |
			SW_SHP_EDGE_AVG_EN | SW_SHP_EN, val);
}

static void fec_config(struct rkispp_params_vdev *params_vdev,
		       struct rkispp_fec_config *arg)
{
	struct rkispp_device *dev = params_vdev->dev;
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	void __iomem *base = dev->base_addr;
	struct rkispp_dummy_buffer *buf;
	u32 val;

	if (arg->mesh_size > vdev->fec_buf.mesh_xint.size) {
		v4l2_err(&dev->v4l2_dev,
			 "Input mesh size too large. mesh size 0x%x, 0x%x\n",
			 arg->mesh_size, vdev->fec_buf.mesh_xint.size);
		return;
	}

	val = 0;
	if (arg->mesh_density)
		val = SW_MESH_DENSITY;
	rkispp_set_bits(base + RKISPP_FEC_CORE_CTRL, SW_MESH_DENSITY, val);

	rkispp_write(base + RKISPP_FEC_MESH_SIZE, arg->mesh_size);

	val = (arg->crop_height & 0x1FFFF) << 14 |
	      (arg->crop_width & 0x1FFFF) << 1 | (arg->crop_en & 0x01);
	rkispp_write(base + RKISPP_FEC_CROP, val);

	buf = &vdev->fec_buf.mesh_xint;
	memcpy(buf->vaddr, &arg->meshxi[0], arg->mesh_size * sizeof(u16));
	buf = &vdev->fec_buf.mesh_yint;
	memcpy(buf->vaddr, &arg->meshyi[0], arg->mesh_size * sizeof(u16));
	buf = &vdev->fec_buf.mesh_xfra;
	memcpy(buf->vaddr, &arg->meshxf[0], arg->mesh_size * sizeof(u8));
	buf = &vdev->fec_buf.mesh_yfra;
	memcpy(buf->vaddr, &arg->meshyf[0], arg->mesh_size * sizeof(u8));
}

static void fec_enable(struct rkispp_params_vdev *params_vdev, bool en)
{
	void __iomem *base = params_vdev->dev->base_addr;

	if (en) {
		rkispp_clear_bits(base + RKISPP_SCL0_CTRL, SW_SCL_FIRST_MODE);
		rkispp_clear_bits(base + RKISPP_SCL1_CTRL, SW_SCL_FIRST_MODE);
		rkispp_clear_bits(base + RKISPP_SCL2_CTRL, SW_SCL_FIRST_MODE);
	}
	rkispp_set_bits(base + RKISPP_FEC_CORE_CTRL, SW_FEC_EN, en);
}

static void orb_config(struct rkispp_params_vdev *params_vdev,
		       struct rkispp_orb_config *arg)
{
	void __iomem *base = params_vdev->dev->base_addr;

	rkispp_write(base + RKISPP_ORB_LIMIT_VALUE, arg->limit_value & 0xFF);
	rkispp_write(base + RKISPP_ORB_MAX_FEATURE, arg->max_feature & 0x1FFFFF);
}

static void orb_enable(struct rkispp_params_vdev *params_vdev, bool en)
{
	void __iomem *base = params_vdev->dev->base_addr;

	rkispp_set_bits(base + RKISPP_ORB_CORE_CTRL, SW_ORB_EN, en);
}

static int rkispp_params_enum_fmt_meta_out(struct file *file, void *priv,
					   struct v4l2_fmtdesc *f)
{
	struct video_device *video = video_devdata(file);
	struct rkispp_params_vdev *params_vdev = video_get_drvdata(video);

	if (f->index > 0 || f->type != video->queue->type)
		return -EINVAL;

	f->pixelformat = params_vdev->vdev_fmt.fmt.meta.dataformat;

	return 0;
}

static int rkispp_params_g_fmt_meta_out(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct video_device *video = video_devdata(file);
	struct rkispp_params_vdev *params_vdev = video_get_drvdata(video);
	struct v4l2_meta_format *meta = &f->fmt.meta;

	if (f->type != video->queue->type)
		return -EINVAL;

	memset(meta, 0, sizeof(*meta));
	meta->dataformat = params_vdev->vdev_fmt.fmt.meta.dataformat;
	meta->buffersize = params_vdev->vdev_fmt.fmt.meta.buffersize;

	return 0;
}

static int rkispp_params_querycap(struct file *file,
				  void *priv, struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);
	struct rkispp_params_vdev *params_vdev = video_get_drvdata(vdev);

	snprintf(cap->driver, sizeof(cap->driver),
		 "%s_v%d", DRIVER_NAME,
		 params_vdev->dev->ispp_ver >> 4);
	strlcpy(cap->card, vdev->name, sizeof(cap->card));
	strlcpy(cap->bus_info, "platform: " DRIVER_NAME, sizeof(cap->bus_info));

	return 0;
}

static int rkispp_params_subs_evt(struct v4l2_fh *fh,
				 const struct v4l2_event_subscription *sub)
{
	if (sub->id != 0)
		return -EINVAL;

	switch (sub->type) {
	case CIFISP_V4L2_EVENT_STREAM_START:
	case CIFISP_V4L2_EVENT_STREAM_STOP:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	default:
		return -EINVAL;
	}
}

static const struct v4l2_ioctl_ops rkispp_params_ioctl = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_enum_fmt_meta_out = rkispp_params_enum_fmt_meta_out,
	.vidioc_g_fmt_meta_out = rkispp_params_g_fmt_meta_out,
	.vidioc_s_fmt_meta_out = rkispp_params_g_fmt_meta_out,
	.vidioc_try_fmt_meta_out = rkispp_params_g_fmt_meta_out,
	.vidioc_querycap = rkispp_params_querycap,
	.vidioc_subscribe_event = rkispp_params_subs_evt,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe
};

static int rkispp_params_vb2_queue_setup(struct vb2_queue *vq,
					 unsigned int *num_buffers,
					 unsigned int *num_planes,
					 unsigned int sizes[],
					 struct device *alloc_ctxs[])
{
	struct rkispp_params_vdev *params_vdev = vq->drv_priv;

	*num_buffers = clamp_t(u32, *num_buffers,
			       RKISP1_ISP_PARAMS_REQ_BUFS_MIN,
			       RKISP1_ISP_PARAMS_REQ_BUFS_MAX);

	*num_planes = 1;
	sizes[0] = sizeof(struct rkispp_params_cfg);
	INIT_LIST_HEAD(&params_vdev->params);
	params_vdev->first_params = true;

	return 0;
}

static void rkispp_params_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkispp_buffer *params_buf = to_rkispp_buffer(vbuf);
	struct vb2_queue *vq = vb->vb2_queue;
	struct rkispp_params_vdev *params_vdev = vq->drv_priv;
	struct rkispp_stream_vdev *stream_vdev = &params_vdev->dev->stream_vdev;
	struct rkispp_params_cfg *new_params;
	unsigned long flags;

	new_params = (struct rkispp_params_cfg *)vb2_plane_vaddr(vb, 0);
	spin_lock_irqsave(&params_vdev->config_lock, flags);
	if (params_vdev->first_params) {
		vb2_buffer_done(&params_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		params_vdev->first_params = false;
		*params_vdev->cur_params = *new_params;
		if (new_params->module_init_ens)
			stream_vdev->module_ens = new_params->module_init_ens;
		spin_unlock_irqrestore(&params_vdev->config_lock, flags);
		wake_up(&params_vdev->dev->sync_onoff);
		return;
	}
	spin_unlock_irqrestore(&params_vdev->config_lock, flags);

	new_params->module_init_ens = stream_vdev->module_ens;
	params_buf->vaddr[0] = new_params;
	spin_lock_irqsave(&params_vdev->config_lock, flags);
	list_add_tail(&params_buf->queue, &params_vdev->params);
	spin_unlock_irqrestore(&params_vdev->config_lock, flags);
}

static void rkispp_params_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct rkispp_params_vdev *params_vdev = vq->drv_priv;
	struct rkispp_buffer *buf;
	unsigned long flags;
	int i;

	/* stop params input firstly */
	spin_lock_irqsave(&params_vdev->config_lock, flags);
	params_vdev->streamon = false;
	wake_up(&params_vdev->dev->sync_onoff);
	spin_unlock_irqrestore(&params_vdev->config_lock, flags);

	for (i = 0; i < RKISP1_ISP_PARAMS_REQ_BUFS_MAX; i++) {
		spin_lock_irqsave(&params_vdev->config_lock, flags);
		if (!list_empty(&params_vdev->params)) {
			buf = list_first_entry(&params_vdev->params,
					       struct rkispp_buffer, queue);
			list_del(&buf->queue);
			spin_unlock_irqrestore(&params_vdev->config_lock,
					       flags);
		} else {
			spin_unlock_irqrestore(&params_vdev->config_lock,
					       flags);
			break;
		}

		if (buf)
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		buf = NULL;
	}

	if (params_vdev->cur_buf) {
		vb2_buffer_done(&params_vdev->cur_buf->vb.vb2_buf,
				VB2_BUF_STATE_ERROR);
		params_vdev->cur_buf = NULL;
	}

	/* clean module params */
	params_vdev->cur_params->module_cfg_update = 0;
	params_vdev->cur_params->module_en_update = 0;
}

static int
rkispp_params_vb2_start_streaming(struct vb2_queue *queue, unsigned int count)
{
	struct rkispp_params_vdev *params_vdev = queue->drv_priv;
	unsigned long flags;

	spin_lock_irqsave(&params_vdev->config_lock, flags);
	params_vdev->streamon = true;
	spin_unlock_irqrestore(&params_vdev->config_lock, flags);

	return 0;
}

static int
rkispp_param_fh_open(struct file *filp)
{
	struct rkispp_params_vdev *params = video_drvdata(filp);
	struct rkispp_device *isppdev = params->dev;
	int ret;

	ret = v4l2_fh_open(filp);
	if (!ret) {
		ret = v4l2_pipeline_pm_use(&params->vnode.vdev.entity, 1);
		if (ret < 0) {
			v4l2_err(&isppdev->v4l2_dev,
				 "pipeline power on failed %d\n", ret);
			vb2_fop_release(filp);
		}
	}
	return ret;
}

static int
rkispp_param_fh_release(struct file *filp)
{
	struct rkispp_params_vdev *params = video_drvdata(filp);
	struct rkispp_device *isppdev = params->dev;
	int ret;

	ret = vb2_fop_release(filp);
	if (!ret) {
		ret = v4l2_pipeline_pm_use(&params->vnode.vdev.entity, 0);
		if (ret < 0)
			v4l2_err(&isppdev->v4l2_dev,
				 "pipeline power off failed %d\n", ret);
	}
	return ret;
}

static struct vb2_ops rkispp_params_vb2_ops = {
	.queue_setup = rkispp_params_vb2_queue_setup,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.buf_queue = rkispp_params_vb2_buf_queue,
	.start_streaming = rkispp_params_vb2_start_streaming,
	.stop_streaming = rkispp_params_vb2_stop_streaming,

};

struct v4l2_file_operations rkispp_params_fops = {
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.open = rkispp_param_fh_open,
	.release = rkispp_param_fh_release,
};

static int
rkispp_params_init_vb2_queue(struct vb2_queue *q,
			     struct rkispp_params_vdev *params_vdev)
{
	q->type = V4L2_BUF_TYPE_META_OUTPUT;
	q->io_modes = VB2_MMAP | VB2_DMABUF | VB2_USERPTR;
	q->drv_priv = params_vdev;
	q->ops = &rkispp_params_vb2_ops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->buf_struct_size = sizeof(struct rkispp_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &params_vdev->dev->iqlock;
	q->dev = params_vdev->dev->dev;

	return vb2_queue_init(q);
}

void rkispp_params_isr(struct rkispp_params_vdev *params_vdev, u32 mis)
{
	struct rkispp_params_cfg *new_params = NULL;
	u32 module_en_update, module_cfg_update;
	u32 module_ens, module_init_ens;

	spin_lock(&params_vdev->config_lock);
	if (!params_vdev->streamon) {
		spin_unlock(&params_vdev->config_lock);
		return;
	}

	if (!params_vdev->cur_buf &&
	    !list_empty(&params_vdev->params)) {
		params_vdev->cur_buf = list_first_entry(&params_vdev->params,
					struct rkispp_buffer, queue);
		list_del(&params_vdev->cur_buf->queue);
	}

	if (!params_vdev->cur_buf) {
		spin_unlock(&params_vdev->config_lock);
		return;
	}
	new_params = (struct rkispp_params_cfg *)(params_vdev->cur_buf->vaddr[0]);

	module_init_ens = new_params->module_init_ens;
	module_en_update = new_params->module_en_update;
	module_cfg_update = new_params->module_cfg_update;
	module_ens = new_params->module_ens & module_init_ens;

	if (module_cfg_update & ISPP_MODULE_TNR &&
	    mis & TNR_INT) {
		tnr_config(params_vdev,
			   &new_params->tnr_cfg);
		module_cfg_update &= ~ISPP_MODULE_TNR;
	}
	if (module_en_update & ISPP_MODULE_TNR &&
	    mis & TNR_INT) {
		tnr_enable(params_vdev,
			   !!(module_ens & ISPP_MODULE_TNR));
		module_en_update &= ~ISPP_MODULE_TNR;
	}

	if (module_cfg_update & ISPP_MODULE_NR &&
	    mis & NR_INT) {
		nr_config(params_vdev,
			  &new_params->nr_cfg);
		module_cfg_update &= ~ISPP_MODULE_NR;
	}
	if (module_en_update & ISPP_MODULE_NR &&
	    mis & NR_INT) {
		nr_enable(params_vdev,
			  !!(module_ens & ISPP_MODULE_NR),
			  &new_params->nr_cfg);
		module_en_update &= ~ISPP_MODULE_NR;
	}

	if (module_cfg_update & ISPP_MODULE_SHP &&
	    mis & SHP_INT) {
		shp_config(params_vdev,
			   &new_params->shp_cfg);
		module_cfg_update &= ~ISPP_MODULE_SHP;
	}
	if (module_en_update & ISPP_MODULE_SHP &&
	    mis & SHP_INT) {
		shp_enable(params_vdev,
			   !!(module_ens & ISPP_MODULE_SHP),
			   &new_params->shp_cfg);
		module_en_update &= ~ISPP_MODULE_SHP;
	}

	if (module_cfg_update & ISPP_MODULE_ORB &&
	    mis & ORB_INT) {
		orb_config(params_vdev,
			   &new_params->orb_cfg);
		module_cfg_update &= ~ISPP_MODULE_ORB;
	}
	if (module_en_update & ISPP_MODULE_ORB &&
	    mis & ORB_INT) {
		orb_enable(params_vdev,
			   !!(module_ens & ISPP_MODULE_ORB));
		module_en_update &= ~ISPP_MODULE_ORB;
	}

	if (module_cfg_update & ISPP_MODULE_FEC &&
	    mis & FEC_INT) {
		fec_config(params_vdev,
			   &new_params->fec_cfg);
		module_cfg_update &= ~ISPP_MODULE_FEC;
	}

	new_params->module_en_update = module_en_update;
	new_params->module_cfg_update = module_cfg_update;

	if (!(module_en_update & ISPP_PARAM_UD_CK) &&
	    !(module_cfg_update & (ISPP_PARAM_UD_CK | ISPP_MODULE_FEC))) {
		vb2_buffer_done(&params_vdev->cur_buf->vb.vb2_buf,
				VB2_BUF_STATE_DONE);
		params_vdev->cur_buf = NULL;
	}
	spin_unlock(&params_vdev->config_lock);
}

void rkispp_params_configure(struct rkispp_params_vdev *params_vdev)
{
	u32 module_en_update = params_vdev->cur_params->module_en_update;
	u32 module_cfg_update = params_vdev->cur_params->module_cfg_update;
	u32 module_ens = params_vdev->cur_params->module_ens;
	unsigned long flags;

	spin_lock_irqsave(&params_vdev->config_lock, flags);
	if (params_vdev->first_params) {
		params_vdev->first_params = false;
		v4l2_warn(&params_vdev->dev->v4l2_dev,
			  "can not get first iq setting in stream on\n");
	}
	spin_unlock_irqrestore(&params_vdev->config_lock, flags);

	if (module_cfg_update & ISPP_MODULE_TNR)
		tnr_config(params_vdev,
			   &params_vdev->cur_params->tnr_cfg);
	if (module_en_update & ISPP_MODULE_TNR)
		tnr_enable(params_vdev,
			   !!(module_ens & ISPP_MODULE_TNR));

	if (module_cfg_update & ISPP_MODULE_NR)
		nr_config(params_vdev,
			  &params_vdev->cur_params->nr_cfg);
	if (module_en_update & ISPP_MODULE_NR)
		nr_enable(params_vdev,
			  !!(module_ens & ISPP_MODULE_NR),
			  &params_vdev->cur_params->nr_cfg);

	if (module_cfg_update & ISPP_MODULE_SHP)
		shp_config(params_vdev,
			   &params_vdev->cur_params->shp_cfg);
	if (module_en_update & ISPP_MODULE_SHP)
		shp_enable(params_vdev,
			   !!(module_ens & ISPP_MODULE_SHP),
			   &params_vdev->cur_params->shp_cfg);

	if (module_cfg_update & ISPP_MODULE_FEC)
		fec_config(params_vdev,
			   &params_vdev->cur_params->fec_cfg);
	if (module_en_update & ISPP_MODULE_FEC)
		fec_enable(params_vdev,
			   !!(module_ens & ISPP_MODULE_FEC));

	if (module_cfg_update & ISPP_MODULE_ORB)
		orb_config(params_vdev,
			   &params_vdev->cur_params->orb_cfg);
	if (module_en_update & ISPP_MODULE_ORB)
		orb_enable(params_vdev,
			   !!(module_ens & ISPP_MODULE_ORB));
}

int rkispp_register_params_vdev(struct rkispp_device *dev)
{
	struct rkispp_params_vdev *params_vdev = &dev->params_vdev;
	struct rkispp_vdev_node *node = &params_vdev->vnode;
	struct video_device *vdev = &node->vdev;
	int ret;

	params_vdev->dev = dev;
	params_vdev->cur_params = vmalloc(sizeof(*params_vdev->cur_params));
	if (!params_vdev->cur_params)
		return -ENOMEM;

	params_vdev->cur_params->module_cfg_update = 0;
	params_vdev->cur_params->module_en_update = 0;

	spin_lock_init(&params_vdev->config_lock);
	strlcpy(vdev->name, "rkispp_input_params", sizeof(vdev->name));

	video_set_drvdata(vdev, params_vdev);
	vdev->ioctl_ops = &rkispp_params_ioctl;
	vdev->fops = &rkispp_params_fops;
	vdev->release = video_device_release_empty;
	/*
	 * Provide a mutex to v4l2 core. It will be used
	 * to protect all fops and v4l2 ioctls.
	 */
	vdev->lock = &dev->iqlock;
	vdev->v4l2_dev = &dev->v4l2_dev;
	vdev->queue = &node->buf_queue;
	vdev->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_META_OUTPUT;
	vdev->vfl_dir = VFL_DIR_TX;
	rkispp_params_init_vb2_queue(vdev->queue, params_vdev);
	params_vdev->vdev_fmt.fmt.meta.dataformat =
		V4L2_META_FMT_RK_ISPP_PARAMS;

	node->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&vdev->entity, 1, &node->pad);
	if (ret < 0)
		goto err_release_queue;
	ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		dev_err(&vdev->dev,
			"could not register Video for Linux device\n");
		goto err_cleanup_media_entity;
	}
	return 0;

err_cleanup_media_entity:
	media_entity_cleanup(&vdev->entity);
err_release_queue:
	vb2_queue_release(vdev->queue);

	return ret;
}

void rkispp_unregister_params_vdev(struct rkispp_device *dev)
{
	struct rkispp_params_vdev *params_vdev = &dev->params_vdev;
	struct rkispp_vdev_node *node = &params_vdev->vnode;
	struct video_device *vdev = &node->vdev;

	video_unregister_device(vdev);
	media_entity_cleanup(&vdev->entity);
	vb2_queue_release(vdev->queue);
	vfree(params_vdev->cur_params);
}
