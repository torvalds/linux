/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_ISP_PARAM_V2X_H
#define _RKISP_ISP_PARAM_V2X_H

#include <linux/rkisp1-config.h>
#include <linux/rk-preisp.h>
#include "common.h"
#include "isp_params.h"

#define ISP2X_YUVAE_ENA				BIT(0)
#define ISP2X_YUVAE_WNDNUM_SET			BIT(1)
#define ISP2X_YUVAE_SUBWIN1_EN			BIT(4)
#define ISP2X_YUVAE_SUBWIN2_EN			BIT(5)
#define ISP2X_YUVAE_SUBWIN3_EN			BIT(6)
#define ISP2X_YUVAE_SUBWIN4_EN			BIT(7)
#define ISP2X_YUVAE_YSEL			BIT(16)
#define ISP2X_YUVAE_H_OFFSET_SET(x)		((x) & 0x1FFF)
#define ISP2X_YUVAE_V_OFFSET_SET(x)		(((x) & 0x1FFF) << 16)
#define ISP2X_YUVAE_H_SIZE_SET(x)		((x) & 0x7FF)
#define ISP2X_YUVAE_V_SIZE_SET(x)		(((x) & 0x7FF) << 16)
#define ISP2X_YUVAE_SUBWIN_H_OFFSET_SET(x)	((x) & 0x1FFF)
#define ISP2X_YUVAE_SUBWIN_V_OFFSET_SET(x)	(((x) & 0x1FFF) << 16)
#define ISP2X_YUVAE_SUBWIN_H_SIZE_SET(x)	((x) & 0x1FFF)
#define ISP2X_YUVAE_SUBWIN_V_SIZE_SET(x)	(((x) & 0x1FFF) << 16)

#define ISP2X_RAWAE_LITE_ENA			BIT(0)
#define ISP2X_RAWAE_LITE_WNDNUM_SET(x)		(((x) & 0x1) << 1)
#define ISP2X_RAWAE_LITE_H_OFFSET_SET(x)	((x) & 0x1FFF)
#define ISP2X_RAWAE_LITE_V_OFFSET_SET(x)	(((x) & 0x1FFF) << 16)
#define ISP2X_RAWAE_LITE_H_SIZE_SET(x)		((x) & 0x1FFF)
#define ISP2X_RAWAE_LITE_V_SIZE_SET(x)		(((x) & 0x1FFF) << 16)

#define ISP2X_RAWAEBIG_ENA			BIT(0)
#define ISP2X_RAWAEBIG_WNDNUM_SET(x)		(((x) & 0x3) << 1)
#define ISP2X_RAWAEBIG_SUBWIN1_EN		BIT(4)
#define ISP2X_RAWAEBIG_SUBWIN2_EN		BIT(5)
#define ISP2X_RAWAEBIG_SUBWIN3_EN		BIT(6)
#define ISP2X_RAWAEBIG_SUBWIN4_EN		BIT(7)
#define ISP2X_RAWAEBIG_H_OFFSET_SET(x)		((x) & 0x1FFF)
#define ISP2X_RAWAEBIG_V_OFFSET_SET(x)		(((x) & 0x1FFF) << 16)
#define ISP2X_RAWAEBIG_H_SIZE_SET(x)		((x) & 0x7FF)
#define ISP2X_RAWAEBIG_V_SIZE_SET(x)		(((x) & 0x7FF) << 16)
#define ISP2X_RAWAEBIG_SUBWIN_H_OFFSET_SET(x)	((x) & 0x1FFF)
#define ISP2X_RAWAEBIG_SUBWIN_V_OFFSET_SET(x)	(((x) & 0x1FFF) << 16)
#define ISP2X_RAWAEBIG_SUBWIN_H_SIZE_SET(x)	((x) & 0x1FFF)
#define ISP2X_RAWAEBIG_SUBWIN_V_SIZE_SET(x)	(((x) & 0x1FFF) << 16)

#define ISP2X_SIAWB_YMAX_CMP_EN			BIT(2)
#define ISP2X_SIAWB_RGB_MODE_EN			BIT(31)
#define ISP2X_SIAWB_SET_FRAMES(x)		(((x) & 0x07) << 28)
#define ISP2X_SIAWB_MODE_SET(x)			((x) << 0)

#define ISP2X_SIAF_ENA				BIT(0)
#define ISP2X_SIAF_WIN_X(x)			(((x) & 0x1FFF) << 16)
#define ISP2X_SIAF_WIN_Y(x)			((x) & 0x1FFF)
#define ISP2X_SIAF_SET_SHIFT_A(x, y)		(((x) & 0x7) << 16 | ((y) & 0x7) << 0)
#define ISP2X_SIAF_SET_SHIFT_B(x, y)		(((x) & 0x7) << 20 | ((y) & 0x7) << 4)
#define ISP2X_SIAF_SET_SHIFT_C(x, y)		(((x) & 0x7) << 24 | ((y) & 0x7) << 8)
#define ISP2X_SIAF_GET_LUM_SHIFT_A(x)		(((x) & 0x70000) >> 16)
#define ISP2X_SIAF_GET_AFM_SHIFT_A(x)		((x) & 0x7)

#define ISP2X_RAWAF_ENA				BIT(0)
#define ISP2X_RAWAF_GAMMA_ENA			BIT(1)
#define ISP2X_RAWAF_GAUS_ENA			BIT(2)

#define ISP2X_RAWAF_INT_LINE0_EN		BIT(27)
#define ISP2X_RAWAF_INT_LINE1_EN		BIT(28)
#define ISP2X_RAWAF_INT_LINE2_EN		BIT(29)
#define ISP2X_RAWAF_INT_LINE3_EN		BIT(30)
#define ISP2X_RAWAF_INT_LINE4_EN		BIT(31)
#define ISP2X_RAWAF_INT_LINE0_NUM(x)		(((x) & 0xF) << 0)
#define ISP2X_RAWAF_INT_LINE1_NUM(x)		(((x) & 0xF) << 4)
#define ISP2X_RAWAF_INT_LINE2_NUM(x)		(((x) & 0xF) << 8)
#define ISP2X_RAWAF_INT_LINE3_NUM(x)		(((x) & 0xF) << 12)
#define ISP2X_RAWAF_INT_LINE4_NUM(x)		(((x) & 0xF) << 16)

#define ISP2X_RAWAF_THRES(x)			((x) & 0xFFFF)

#define ISP2X_RAWAF_WIN_X(x)			(((x) & 0x1FFF) << 16)
#define ISP2X_RAWAF_WIN_Y(x)			((x) & 0x1FFF)
#define ISP2X_RAWAF_SET_SHIFT_A(x, y)		(((x) & 0x7) << 16 | ((y) & 0x7) << 0)
#define ISP2X_RAWAF_SET_SHIFT_B(x, y)		(((x) & 0x7) << 20 | ((y) & 0x7) << 4)

#define ISP2X_SIHST_CTRL_EN_SET(x)		(((x) & 0x01) << 0)
#define ISP2X_SIHST_CTRL_EN_MASK		ISP2X_SIHST_CTRL_EN_SET(0x01)
#define ISP2X_SIHST_CTRL_STEPSIZE_SET(x)	(((x) & 0x7F) << 1)
#define ISP2X_SIHST_CTRL_MODE_SET(x)		(((x) & 0x07) << 8)
#define ISP2X_SIHST_CTRL_MODE_MASK		ISP2X_SIHST_CTRL_MODE_SET(0x07)
#define ISP2X_SIHST_CTRL_AUTOSTOP_SET(x)	(((x) & 0x01) << 11)
#define ISP2X_SIHST_CTRL_WATERLINE_SET(x)	(((x) & 0xFFF) << 12)
#define ISP2X_SIHST_CTRL_DATASEL_SET(x)		(((x) & 0x07) << 24)
#define ISP2X_SIHST_CTRL_INTRSEL_SET(x)		(((x) & 0x01) << 27)
#define ISP2X_SIHST_CTRL_INTRSEL_MASK		ISP2X_SIHST_CTRL_INTRSEL_SET(0x01)
#define ISP2X_SIHST_CTRL_WNDNUM_SET(x)		(((x) & 0x03) << 28)
#define ISP2X_SIHST_CTRL_WNDNUM_MASK		ISP2X_SIHST_CTRL_WNDNUM_SET(0x03)

#define ISP2X_SIHST_ROW_NUM			15
#define ISP2X_SIHST_COLUMN_NUM			15
#define ISP2X_SIHST_WEIGHT_REG_SIZE		\
				(ISP2X_SIHST_ROW_NUM * ISP2X_SIHST_COLUMN_NUM)

#define ISP2X_SIHST_WEIGHT_SET(v0, v1, v2, v3)	\
				(((v0) & 0x3F) | (((v1) & 0x3F) << 8) |\
				(((v2) & 0x3F) << 16) |\
				(((v3) & 0x3F) << 24))

#define ISP2X_SIHST_OFFS_SET(v0, v1)		\
				(((v0) & 0x1FFF) | (((v1) & 0x1FFF) << 16))
#define ISP2X_SIHST_SIZE_SET(v0, v1)		\
				(((v0) & 0x1FFF) | (((v1) & 0x1FFF) << 16))

#define ISP2X_RAWHSTBIG_CTRL_EN_SET(x)		(((x) & 0x01) << 0)
#define ISP2X_RAWHSTBIG_CTRL_EN_MASK		ISP2X_RAWHSTBIG_CTRL_EN_SET(0x01)
#define ISP2X_RAWHSTBIG_CTRL_STEPSIZE_SET(x)	(((x) & 0x07) << 1)
#define ISP2X_RAWHSTBIG_CTRL_MODE_SET(x)	(((x) & 0x07) << 8)
#define ISP2X_RAWHSTBIG_CTRL_MODE_MASK		ISP2X_RAWHSTBIG_CTRL_MODE_SET(0x07)
#define ISP2X_RAWHSTBIG_CTRL_WATERLINE_SET(x)	(((x) & 0xFFF) << 12)
#define ISP2X_RAWHSTBIG_CTRL_DATASEL_SET(x)	(((x) & 0x07) << 24)
#define ISP2X_RAWHSTBIG_CTRL_WNDNUM_SET(x)	(((x) & 0x03) << 28)
#define ISP2X_RAWHSTBIG_CTRL_WNDNUM_MASK	ISP2X_RAWHSTBIG_CTRL_WNDNUM_SET(0x03)

#define ISP2X_RAWHSTBIG_WRAM_EN			BIT(31)

#define ISP2X_RAWHSTBIG_ROW_NUM			15
#define ISP2X_RAWHSTBIG_COLUMN_NUM		15
#define ISP2X_RAWHSTBIG_WEIGHT_REG_SIZE		\
				(ISP2X_RAWHSTBIG_ROW_NUM * ISP2X_RAWHSTBIG_COLUMN_NUM)

#define ISP2X_RAWHSTBIG_WEIGHT_SET(v0, v1, v2, v3, v4)	\
				(((v0) & 0x3F) | (((v1) & 0x3F) << 6) |\
				(((v2) & 0x3F) << 12) |\
				(((v3) & 0x3F) << 18) |\
				(((v4) & 0x3F) << 24))

#define ISP2X_RAWHSTBIG_OFFS_SET(v0, v1)	\
				(((v0) & 0x1FFF) | (((v1) & 0x1FFF) << 16))
#define ISP2X_RAWHSTBIG_SIZE_SET(v0, v1)	\
				(((v0) & 0x7FF) | (((v1) & 0x7FF) << 16))

#define ISP2X_RAWHSTLITE_CTRL_EN_SET(x)		(((x) & 0x01) << 0)
#define ISP2X_RAWHSTLITE_CTRL_EN_MASK		ISP2X_RAWHSTBIG_CTRL_EN_SET(0x01)
#define ISP2X_RAWHSTLITE_CTRL_STEPSIZE_SET(x)	(((x) & 0x07) << 1)
#define ISP2X_RAWHSTLITE_CTRL_MODE_SET(x)	(((x) & 0x07) << 8)
#define ISP2X_RAWHSTLITE_CTRL_MODE_MASK		ISP2X_RAWHSTBIG_CTRL_MODE_SET(0x07)
#define ISP2X_RAWHSTLITE_CTRL_WATERLINE_SET(x)	(((x) & 0xFFF) << 12)
#define ISP2X_RAWHSTLITE_CTRL_DATASEL_SET(x)	(((x) & 0x07) << 24)

#define ISP2X_RAWHSTLITE_ROW_NUM		5
#define ISP2X_RAWHSTLITE_COLUMN_NUM		5
#define ISP2X_RAWHSTLITE_WEIGHT_REG_SIZE	\
				(ISP2X_RAWHSTLITE_ROW_NUM * ISP2X_RAWHSTLITE_COLUMN_NUM)

#define ISP2X_RAWHSTLITE_WEIGHT_SET(v0, v1, v2, v3)	\
				(((v0) & 0x3F) | (((v1) & 0x3F) << 8) |\
				(((v2) & 0x3F) << 16) |\
				(((v3) & 0x3F) << 24))

#define ISP2X_RAWHSTLITE_OFFS_SET(v0, v1)	\
				(((v0) & 0x1FFF) | (((v1) & 0x1FFF) << 16))
#define ISP2X_RAWHSTLITE_SIZE_SET(v0, v1)	\
				(((v0) & 0x7FF) | (((v1) & 0x7FF) << 16))

#define ISP2X_RAWAWB_ENA			BIT(0)
#define ISP2X_RAWAWB_WPTH2_SET(x)		(((x) & 0x1FF) << 9)

#define ISP2X_ISPPATH_RAWAE_SEL_SET(x)	(((x) & 0x03) << 16)
#define ISP2X_ISPPATH_RAWAF_SEL_SET(x)	(((x) & 0x03) << 18)
#define ISP2X_ISPPATH_RAWAWB_SEL_SET(x)	(((x) & 0x03) << 20)
#define ISP2X_ISPPATH_RAWAE_SWAP_SET(x)	(((x) & 0x03) << 22)

#define RKISP_PARAM_3DLUT_BUF_NUM		2
#define RKISP_PARAM_3DLUT_BUF_SIZE		(9 * 9 * 9 * 4)

#define RKISP_PARAM_LSC_LUT_BUF_NUM		2
#define RKISP_PARAM_LSC_LUT_TBL_SIZE		(9 * 17 * 4)
#define RKISP_PARAM_LSC_LUT_BUF_SIZE		(RKISP_PARAM_LSC_LUT_TBL_SIZE * 4)

struct rkisp_isp_params_vdev;
struct rkisp_isp_params_v2x_ops {
	void (*dpcc_config)(struct rkisp_isp_params_vdev *params_vdev,
			    const struct isp2x_dpcc_cfg *arg);
	void (*dpcc_enable)(struct rkisp_isp_params_vdev *params_vdev,
			    bool en);
	void (*bls_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp2x_bls_cfg *arg);
	void (*bls_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en);
	void (*sdg_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp2x_sdg_cfg *arg);
	void (*sdg_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en);
	void (*sihst_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp2x_sihst_cfg *arg);
	void (*sihst_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en);
	void (*lsc_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp2x_lsc_cfg *arg);
	void (*lsc_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en);
	void (*awbgain_config)(struct rkisp_isp_params_vdev *params_vdev,
			       const struct isp2x_awb_gain_cfg *arg);
	void (*awbgain_enable)(struct rkisp_isp_params_vdev *params_vdev,
			       bool en);
	void (*debayer_config)(struct rkisp_isp_params_vdev *params_vdev,
			       const struct isp2x_debayer_cfg *arg);
	void (*debayer_enable)(struct rkisp_isp_params_vdev *params_vdev,
			       bool en);
	void (*ccm_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp2x_ccm_cfg *arg);
	void (*ccm_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en);
	void (*goc_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp2x_gammaout_cfg *arg);
	void (*goc_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en);
	void (*cproc_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp2x_cproc_cfg *arg);
	void (*cproc_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en);
	void (*siaf_config)(struct rkisp_isp_params_vdev *params_vdev,
			    const struct isp2x_siaf_cfg *arg);
	void (*siaf_enable)(struct rkisp_isp_params_vdev *params_vdev,
			    bool en);
	void (*siawb_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp2x_siawb_meas_cfg *arg);
	void (*siawb_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en);
	void (*ie_config)(struct rkisp_isp_params_vdev *params_vdev,
			  const struct isp2x_ie_cfg *arg);
	void (*ie_enable)(struct rkisp_isp_params_vdev *params_vdev,
			  bool en);
	void (*yuvae_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp2x_yuvae_meas_cfg *arg);
	void (*yuvae_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en);
	void (*wdr_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp2x_wdr_cfg *arg);
	void (*wdr_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en);
	void (*iesharp_config)(struct rkisp_isp_params_vdev *params_vdev,
			       const struct isp2x_rkiesharp_cfg *arg);
	void (*iesharp_enable)(struct rkisp_isp_params_vdev *params_vdev,
			       bool en);
	void (*rawaf_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp2x_rawaf_meas_cfg *arg);
	void (*rawaf_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en);
	void (*rawae0_config)(struct rkisp_isp_params_vdev *params_vdev,
			      const struct isp2x_rawaelite_meas_cfg *arg);
	void (*rawae0_enable)(struct rkisp_isp_params_vdev *params_vdev,
			      bool en);
	void (*rawae1_config)(struct rkisp_isp_params_vdev *params_vdev,
			      const struct isp2x_rawaebig_meas_cfg *arg);
	void (*rawae1_enable)(struct rkisp_isp_params_vdev *params_vdev,
			      bool en);
	void (*rawae2_config)(struct rkisp_isp_params_vdev *params_vdev,
			      const struct isp2x_rawaebig_meas_cfg *arg);
	void (*rawae2_enable)(struct rkisp_isp_params_vdev *params_vdev,
			      bool en);
	void (*rawae3_config)(struct rkisp_isp_params_vdev *params_vdev,
			      const struct isp2x_rawaebig_meas_cfg *arg);
	void (*rawae3_enable)(struct rkisp_isp_params_vdev *params_vdev,
			      bool en);
	void (*rawawb_config)(struct rkisp_isp_params_vdev *params_vdev,
			      const struct isp2x_rawawb_meas_cfg *arg);
	void (*rawawb_enable)(struct rkisp_isp_params_vdev *params_vdev,
			      bool en);
	void (*rawhst0_config)(struct rkisp_isp_params_vdev *params_vdev,
			       const struct isp2x_rawhistlite_cfg *arg);
	void (*rawhst0_enable)(struct rkisp_isp_params_vdev *params_vdev,
			       bool en);
	void (*rawhst1_config)(struct rkisp_isp_params_vdev *params_vdev,
			       const struct isp2x_rawhistbig_cfg *arg);
	void (*rawhst1_enable)(struct rkisp_isp_params_vdev *params_vdev,
			       bool en);
	void (*rawhst2_config)(struct rkisp_isp_params_vdev *params_vdev,
			       const struct isp2x_rawhistbig_cfg *arg);
	void (*rawhst2_enable)(struct rkisp_isp_params_vdev *params_vdev,
			       bool en);
	void (*rawhst3_config)(struct rkisp_isp_params_vdev *params_vdev,
			       const struct isp2x_rawhistbig_cfg *arg);
	void (*rawhst3_enable)(struct rkisp_isp_params_vdev *params_vdev,
			       bool en);
	void (*hdrmge_config)(struct rkisp_isp_params_vdev *params_vdev,
			      const struct isp2x_hdrmge_cfg *arg, enum rkisp_params_type type);
	void (*hdrmge_enable)(struct rkisp_isp_params_vdev *params_vdev,
			      bool en);
	void (*rawnr_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp2x_rawnr_cfg *arg);
	void (*rawnr_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en);
	void (*hdrtmo_config)(struct rkisp_isp_params_vdev *params_vdev,
			      const struct isp2x_hdrtmo_cfg *arg, enum rkisp_params_type type);
	void (*hdrtmo_enable)(struct rkisp_isp_params_vdev *params_vdev,
			      bool en);
	void (*gic_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp2x_gic_cfg *arg);
	void (*gic_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en);
	void (*dhaz_config)(struct rkisp_isp_params_vdev *params_vdev,
			    const struct isp2x_dhaz_cfg *arg);
	void (*dhaz_enable)(struct rkisp_isp_params_vdev *params_vdev,
			    bool en);
	void (*gain_config)(struct rkisp_isp_params_vdev *params_vdev,
			    const struct isp2x_gain_cfg *arg);
	void (*gain_enable)(struct rkisp_isp_params_vdev *params_vdev,
			    bool en);
	void (*isp3dlut_config)(struct rkisp_isp_params_vdev *params_vdev,
				const struct isp2x_3dlut_cfg *arg);
	void (*isp3dlut_enable)(struct rkisp_isp_params_vdev *params_vdev,
				bool en);
	void (*ldch_config)(struct rkisp_isp_params_vdev *params_vdev,
			    const struct isp2x_ldch_cfg *arg);
	void (*ldch_enable)(struct rkisp_isp_params_vdev *params_vdev,
			    bool en);
	void (*csm_config)(struct rkisp_isp_params_vdev *params_vdev,
			   bool full_range);
};

struct rkisp_isp_params_val_v2x {
	struct rkisp_dummy_buffer buf_3dlut[RKISP_PARAM_3DLUT_BUF_NUM];
	u32 buf_3dlut_idx;

	struct rkisp_dummy_buffer buf_ldch[ISP2X_LDCH_BUF_NUM];
	u32 buf_ldch_idx;

	struct rkisp_dummy_buffer buf_lsclut[RKISP_PARAM_LSC_LUT_BUF_NUM];
	u32 buf_lsclut_idx;

	struct isp2x_hdrtmo_cfg last_hdrtmo;
	struct isp2x_hdrmge_cfg last_hdrmge;
	struct isp2x_hdrtmo_cfg cur_hdrtmo;
	struct isp2x_hdrmge_cfg cur_hdrmge;

	u8 dhaz_en;
	u8 wdr_en;
	u8 tmo_en;
	u8 lsc_en;
	u8 mge_en;

	/*
	 * LDCH will compete with LSC/3DLUT for the DDR bus,
	 * which may cause LDCH to read the map table exception.
	 * so enable LDCH in 2th frame.
	 */
	bool delay_en_ldch;
};

#if IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISP_VERSION_V20)
int rkisp_init_params_vdev_v2x(struct rkisp_isp_params_vdev *params_vdev);
void rkisp_uninit_params_vdev_v2x(struct rkisp_isp_params_vdev *params_vdev);
#else
static inline int rkisp_init_params_vdev_v2x(struct rkisp_isp_params_vdev *params_vdev) { return -EINVAL; }
static inline void rkisp_uninit_params_vdev_v2x(struct rkisp_isp_params_vdev *params_vdev) {}
#endif

#endif /* _RKISP_ISP_PARAM_V2X_H */
