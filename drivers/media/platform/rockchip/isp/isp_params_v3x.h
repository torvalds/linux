/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_ISP_PARAM_V3X_H
#define _RKISP_ISP_PARAM_V3X_H

#include <linux/rkisp1-config.h>
#include <linux/rk-preisp.h>
#include "common.h"
#include "isp_params.h"

#define ISP3X_3DLUT_BUF_NUM			2
#define ISP3X_3DLUT_BUF_SIZE			(9 * 9 * 9 * 4)

#define ISP3X_LSC_LUT_BUF_NUM			2
#define ISP3X_LSC_LUT_TBL_SIZE			(9 * 17 * 4)
#define ISP3X_LSC_LUT_BUF_SIZE			(ISP3X_LSC_LUT_TBL_SIZE * 4)

#define ISP3X_RAWHISTBIG_ROW_NUM		15
#define ISP3X_RAWHISTBIG_COLUMN_NUM		15
#define ISP3X_RAWHISTBIG_WEIGHT_REG_SIZE	\
	(ISP3X_RAWHISTBIG_ROW_NUM * ISP3X_RAWHISTBIG_COLUMN_NUM)

#define ISP3X_RAWHISTLITE_ROW_NUM		5
#define ISP3X_RAWHISTLITE_COLUMN_NUM		5
#define ISP3X_RAWHISTLITE_WEIGHT_REG_SIZE	\
	(ISP3X_RAWHISTLITE_ROW_NUM * ISP3X_RAWHISTLITE_COLUMN_NUM)

struct rkisp_isp_params_vdev;
struct rkisp_isp_params_ops_v3x {
	void (*dpcc_config)(struct rkisp_isp_params_vdev *params_vdev,
			    const struct isp2x_dpcc_cfg *arg);
	void (*dpcc_enable)(struct rkisp_isp_params_vdev *params_vdev,
			    bool en);
	void (*bls_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp21_bls_cfg *arg);
	void (*bls_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en);
	void (*sdg_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp2x_sdg_cfg *arg);
	void (*sdg_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en);
	void (*lsc_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp3x_lsc_cfg *arg);
	void (*lsc_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en);
	void (*awbgain_config)(struct rkisp_isp_params_vdev *params_vdev,
			       const struct isp21_awb_gain_cfg *arg);
	void (*awbgain_enable)(struct rkisp_isp_params_vdev *params_vdev,
			       bool en);
	void (*debayer_config)(struct rkisp_isp_params_vdev *params_vdev,
			       const struct isp2x_debayer_cfg *arg);
	void (*debayer_enable)(struct rkisp_isp_params_vdev *params_vdev,
			       bool en);
	void (*ccm_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp21_ccm_cfg *arg);
	void (*ccm_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en);
	void (*goc_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp3x_gammaout_cfg *arg);
	void (*goc_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en);
	void (*cproc_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp2x_cproc_cfg *arg);
	void (*cproc_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en);
	void (*ie_config)(struct rkisp_isp_params_vdev *params_vdev,
			  const struct isp2x_ie_cfg *arg);
	void (*ie_enable)(struct rkisp_isp_params_vdev *params_vdev,
			  bool en);
	void (*rawaf_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp3x_rawaf_meas_cfg *arg);
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
			      const struct isp3x_rawawb_meas_cfg *arg);
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
	void (*hdrdrc_config)(struct rkisp_isp_params_vdev *params_vdev,
			      const struct isp3x_drc_cfg *arg, enum rkisp_params_type type);
	void (*hdrdrc_enable)(struct rkisp_isp_params_vdev *params_vdev,
			      bool en);
	void (*hdrmge_config)(struct rkisp_isp_params_vdev *params_vdev,
			      const struct isp3x_hdrmge_cfg *arg, enum rkisp_params_type type);
	void (*hdrmge_enable)(struct rkisp_isp_params_vdev *params_vdev,
			      bool en);
	void (*gic_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp21_gic_cfg *arg);
	void (*gic_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en);
	void (*dhaz_config)(struct rkisp_isp_params_vdev *params_vdev,
			    const struct isp3x_dhaz_cfg *arg);
	void (*dhaz_enable)(struct rkisp_isp_params_vdev *params_vdev,
			    bool en);
	void (*isp3dlut_config)(struct rkisp_isp_params_vdev *params_vdev,
				const struct isp2x_3dlut_cfg *arg);
	void (*isp3dlut_enable)(struct rkisp_isp_params_vdev *params_vdev,
				bool en);
	void (*ldch_config)(struct rkisp_isp_params_vdev *params_vdev,
			    const struct isp2x_ldch_cfg *arg);
	void (*ldch_enable)(struct rkisp_isp_params_vdev *params_vdev,
			    bool en);
	void (*ynr_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp3x_ynr_cfg *arg);
	void (*ynr_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en, const struct isp3x_ynr_cfg *arg);
	void (*cnr_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp3x_cnr_cfg *arg);
	void (*cnr_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en, const struct isp3x_cnr_cfg *arg);
	void (*sharp_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp3x_sharp_cfg *arg);
	void (*sharp_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en);
	void (*baynr_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp3x_baynr_cfg *arg);
	void (*baynr_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en);
	void (*bay3d_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp3x_bay3d_cfg *arg);
	void (*bay3d_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en);
	void (*gain_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp3x_gain_cfg *arg);
	void (*gain_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en);
	void (*cac_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp3x_cac_cfg *arg);
	void (*cac_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en);
};

struct rkisp_isp_params_val_v3x {
	struct rkisp_dummy_buffer buf_3dlut[ISP3X_3DLUT_BUF_NUM];
	u32 buf_3dlut_idx;

	struct rkisp_dummy_buffer buf_ldch[ISP3X_MESH_BUF_NUM];
	u32 buf_ldch_idx;

	struct rkisp_dummy_buffer buf_lsclut[ISP3X_LSC_LUT_BUF_NUM];
	u32 buf_lsclut_idx;

	struct rkisp_dummy_buffer buf_cac[ISP3X_MESH_BUF_NUM];
	u32 buf_cac_idx;

	struct rkisp_dummy_buffer buf_3dnr_iir;
	struct rkisp_dummy_buffer buf_3dnr_cur;
	struct rkisp_dummy_buffer buf_3dnr_ds;

	struct isp3x_hdrmge_cfg last_hdrmge;
	struct isp3x_drc_cfg last_hdrdrc;
	struct isp3x_hdrmge_cfg cur_hdrmge;
	struct isp3x_drc_cfg cur_hdrdrc;
	struct isp3x_lsc_cfg cur_lsccfg;

	bool dhaz_en;
	bool drc_en;
	bool lsc_en;
	bool mge_en;
	bool lut3d_en;
	bool bay3d_en;
};

#if IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISP_VERSION_V30)
int rkisp_init_params_vdev_v3x(struct rkisp_isp_params_vdev *params_vdev);
void rkisp_uninit_params_vdev_v3x(struct rkisp_isp_params_vdev *params_vdev);
#else
static inline int rkisp_init_params_vdev_v3x(struct rkisp_isp_params_vdev *params_vdev) { return 0; }
static inline void rkisp_uninit_params_vdev_v3x(struct rkisp_isp_params_vdev *params_vdev) {}
#endif

#endif /* _RKISP_ISP_PARAM_V3X_H */
