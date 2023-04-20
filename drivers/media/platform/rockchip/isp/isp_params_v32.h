/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2022 Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_ISP_PARAM_V32_H
#define _RKISP_ISP_PARAM_V32_H

#include <linux/rk-isp1-config.h>
#include <linux/rk-preisp.h>
#include "common.h"
#include "isp_params.h"

#define ISP32_3DLUT_BUF_NUM			2
#define ISP32_3DLUT_BUF_SIZE			(9 * 9 * 9 * 4)

#define ISP32_LSC_LUT_BUF_NUM			2
#define ISP32_LSC_LUT_TBL_SIZE			(9 * 17 * 4)
#define ISP32_LSC_LUT_BUF_SIZE			(ISP32_LSC_LUT_TBL_SIZE * 4)

#define ISP32_RAWHISTBIG_ROW_NUM		15
#define ISP32_RAWHISTBIG_COLUMN_NUM		15
#define ISP32_RAWHISTBIG_WEIGHT_REG_SIZE	\
	(ISP32_RAWHISTBIG_ROW_NUM * ISP32_RAWHISTBIG_COLUMN_NUM)

#define ISP32_RAWHISTLITE_ROW_NUM		5
#define ISP32_RAWHISTLITE_COLUMN_NUM		5
#define ISP32_RAWHISTLITE_WEIGHT_REG_SIZE	\
	(ISP32_RAWHISTLITE_ROW_NUM * ISP32_RAWHISTLITE_COLUMN_NUM)

struct rkisp_isp_params_vdev;
struct rkisp_isp_params_ops_v32 {
	void (*dpcc_config)(struct rkisp_isp_params_vdev *params_vdev,
			    const struct isp2x_dpcc_cfg *arg, u32 id);
	void (*dpcc_enable)(struct rkisp_isp_params_vdev *params_vdev,
			    bool en, u32 id);
	void (*bls_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp32_bls_cfg *arg, u32 id);
	void (*bls_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en, u32 id);
	void (*sdg_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp2x_sdg_cfg *arg, u32 id);
	void (*sdg_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en, u32 id);
	void (*lsc_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp3x_lsc_cfg *arg, u32 id);
	void (*lsc_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en, u32 id);
	void (*awbgain_config)(struct rkisp_isp_params_vdev *params_vdev,
			       const struct isp32_awb_gain_cfg *arg, u32 id);
	void (*awbgain_enable)(struct rkisp_isp_params_vdev *params_vdev,
			       bool en, u32 id);
	void (*debayer_config)(struct rkisp_isp_params_vdev *params_vdev,
			       const struct isp32_debayer_cfg *arg, u32 id);
	void (*debayer_enable)(struct rkisp_isp_params_vdev *params_vdev,
			       bool en, u32 id);
	void (*ccm_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp32_ccm_cfg *arg, u32 id);
	void (*ccm_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en, u32 id);
	void (*goc_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp3x_gammaout_cfg *arg, u32 id);
	void (*goc_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en, u32 id);
	void (*cproc_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp2x_cproc_cfg *arg, u32 id);
	void (*cproc_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en, u32 id);
	void (*ie_config)(struct rkisp_isp_params_vdev *params_vdev,
			  const struct isp2x_ie_cfg *arg, u32 id);
	void (*ie_enable)(struct rkisp_isp_params_vdev *params_vdev,
			  bool en, u32 id);
	void (*rawaf_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp32_rawaf_meas_cfg *arg, u32 id);
	void (*rawaf_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en, u32 id);
	void (*rawae0_config)(struct rkisp_isp_params_vdev *params_vdev,
			      const struct isp2x_rawaelite_meas_cfg *arg, u32 id);
	void (*rawae0_enable)(struct rkisp_isp_params_vdev *params_vdev,
			      bool en, u32 id);
	void (*rawae1_config)(struct rkisp_isp_params_vdev *params_vdev,
			      const struct isp2x_rawaebig_meas_cfg *arg, u32 id);
	void (*rawae1_enable)(struct rkisp_isp_params_vdev *params_vdev,
			      bool en, u32 id);
	void (*rawae2_config)(struct rkisp_isp_params_vdev *params_vdev,
			      const struct isp2x_rawaebig_meas_cfg *arg, u32 id);
	void (*rawae2_enable)(struct rkisp_isp_params_vdev *params_vdev,
			      bool en, u32 id);
	void (*rawae3_config)(struct rkisp_isp_params_vdev *params_vdev,
			      const struct isp2x_rawaebig_meas_cfg *arg, u32 id);
	void (*rawae3_enable)(struct rkisp_isp_params_vdev *params_vdev,
			      bool en, u32 id);
	void (*rawawb_config)(struct rkisp_isp_params_vdev *params_vdev,
			      const struct isp32_rawawb_meas_cfg *arg, u32 id);
	void (*rawawb_enable)(struct rkisp_isp_params_vdev *params_vdev,
			      bool en, u32 id);
	void (*rawhst0_config)(struct rkisp_isp_params_vdev *params_vdev,
			       const struct isp2x_rawhistlite_cfg *arg, u32 id);
	void (*rawhst0_enable)(struct rkisp_isp_params_vdev *params_vdev,
			       bool en, u32 id);
	void (*rawhst1_config)(struct rkisp_isp_params_vdev *params_vdev,
			       const struct isp2x_rawhistbig_cfg *arg, u32 id);
	void (*rawhst1_enable)(struct rkisp_isp_params_vdev *params_vdev,
			       bool en, u32 id);
	void (*rawhst2_config)(struct rkisp_isp_params_vdev *params_vdev,
			       const struct isp2x_rawhistbig_cfg *arg, u32 id);
	void (*rawhst2_enable)(struct rkisp_isp_params_vdev *params_vdev,
			       bool en, u32 id);
	void (*rawhst3_config)(struct rkisp_isp_params_vdev *params_vdev,
			       const struct isp2x_rawhistbig_cfg *arg, u32 id);
	void (*rawhst3_enable)(struct rkisp_isp_params_vdev *params_vdev,
			       bool en, u32 id);
	void (*hdrdrc_config)(struct rkisp_isp_params_vdev *params_vdev,
			      const struct isp32_drc_cfg *arg,
			      enum rkisp_params_type type, u32 id);
	void (*hdrdrc_enable)(struct rkisp_isp_params_vdev *params_vdev,
			      bool en, u32 id);
	void (*hdrmge_config)(struct rkisp_isp_params_vdev *params_vdev,
			      const struct isp32_hdrmge_cfg *arg,
			      enum rkisp_params_type type, u32 id);
	void (*hdrmge_enable)(struct rkisp_isp_params_vdev *params_vdev,
			      bool en, u32 id);
	void (*gic_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp21_gic_cfg *arg, u32 id);
	void (*gic_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en, u32 id);
	void (*dhaz_config)(struct rkisp_isp_params_vdev *params_vdev,
			    const struct isp32_dhaz_cfg *arg, u32 id);
	void (*dhaz_enable)(struct rkisp_isp_params_vdev *params_vdev,
			    bool en, u32 id);
	void (*isp3dlut_config)(struct rkisp_isp_params_vdev *params_vdev,
				const struct isp2x_3dlut_cfg *arg, u32 id);
	void (*isp3dlut_enable)(struct rkisp_isp_params_vdev *params_vdev,
				bool en, u32 id);
	void (*ldch_config)(struct rkisp_isp_params_vdev *params_vdev,
			    const struct isp32_ldch_cfg *arg, u32 id);
	void (*ldch_enable)(struct rkisp_isp_params_vdev *params_vdev,
			    bool en, u32 id);
	void (*ynr_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp32_ynr_cfg *arg, u32 id);
	void (*ynr_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en, u32 id);
	void (*cnr_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp32_cnr_cfg *arg, u32 id);
	void (*cnr_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en, u32 id);
	void (*sharp_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp32_sharp_cfg *arg, u32 id);
	void (*sharp_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en, u32 id);
	void (*baynr_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp32_baynr_cfg *arg, u32 id);
	void (*baynr_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en, u32 id);
	void (*bay3d_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp32_bay3d_cfg *arg, u32 id);
	void (*bay3d_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en, u32 id);
	void (*gain_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp3x_gain_cfg *arg, u32 id);
	void (*gain_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en, u32 id);
	void (*cac_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp32_cac_cfg *arg, u32 id);
	void (*cac_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en, u32 id);
	void (*csm_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp21_csm_cfg *arg, u32 id);
	void (*cgc_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp21_cgc_cfg *arg, u32 id);
	void (*vsm_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp32_vsm_cfg *arg, u32 id);
	void (*vsm_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en, u32 id);
};

struct rkisp_isp_params_val_v32 {
	struct tasklet_struct lsc_tasklet;

	struct rkisp_dummy_buffer buf_3dlut[ISP3_UNITE_MAX][ISP32_3DLUT_BUF_NUM];
	u32 buf_3dlut_idx[ISP3_UNITE_MAX];

	struct rkisp_dummy_buffer buf_ldch[ISP3_UNITE_MAX][ISP3X_MESH_BUF_NUM];
	u32 buf_ldch_idx[ISP3_UNITE_MAX];

	struct rkisp_dummy_buffer buf_cac[ISP3_UNITE_MAX][ISP3X_MESH_BUF_NUM];
	u32 buf_cac_idx[ISP3_UNITE_MAX];

	struct rkisp_dummy_buffer buf_lsclut[ISP32_LSC_LUT_BUF_NUM];
	u32 buf_lsclut_idx;

	struct rkisp_dummy_buffer buf_info[RKISP_INFO2DDR_BUF_MAX];
	u32 buf_info_owner;
	u32 buf_info_cnt;
	int buf_info_idx;

	u32 bay3d_ds_size;
	u32 bay3d_iir_size;
	u32 bay3d_cur_size;
	u32 bay3d_cur_wsize;
	u32 bay3d_cur_wrap_line;
	struct rkisp_dummy_buffer buf_3dnr_iir;
	struct rkisp_dummy_buffer buf_3dnr_cur;
	struct rkisp_dummy_buffer buf_3dnr_ds;

	struct rkisp_dummy_buffer buf_frm;

	bool dhaz_en;
	bool drc_en;
	bool lsc_en;
	bool mge_en;
	bool lut3d_en;
	bool bay3d_en;
	bool is_bigmode;
	bool is_lo8x8;
	bool is_sram;
};

#if IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISP_VERSION_V32)
int rkisp_init_params_vdev_v32(struct rkisp_isp_params_vdev *params_vdev);
void rkisp_uninit_params_vdev_v32(struct rkisp_isp_params_vdev *params_vdev);
#else
static inline int rkisp_init_params_vdev_v32(struct rkisp_isp_params_vdev *params_vdev) { return -EINVAL; }
static inline void rkisp_uninit_params_vdev_v32(struct rkisp_isp_params_vdev *params_vdev) {}
#endif

#endif /* _RKISP_ISP_PARAM_V32_H */
