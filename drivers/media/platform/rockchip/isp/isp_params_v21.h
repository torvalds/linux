/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_ISP_PARAM_V21_H
#define _RKISP_ISP_PARAM_V21_H

#include <linux/rk-isp1-config.h>
#include <linux/rk-preisp.h>
#include "common.h"
#include "isp_params.h"
#include "isp_params_v2x.h"

struct rkisp_isp_params_vdev;
struct rkisp_isp_params_v21_ops {
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
			   const struct isp2x_lsc_cfg *arg);
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
			   const struct isp2x_gammaout_cfg *arg);
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
			      const struct isp21_rawawb_meas_cfg *arg);
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
			      const struct isp21_drc_cfg *arg, enum rkisp_params_type type);
	void (*hdrdrc_enable)(struct rkisp_isp_params_vdev *params_vdev,
			      bool en);
	void (*hdrmge_config)(struct rkisp_isp_params_vdev *params_vdev,
			      const struct isp2x_hdrmge_cfg *arg, enum rkisp_params_type type);
	void (*hdrmge_enable)(struct rkisp_isp_params_vdev *params_vdev,
			      bool en);
	void (*gic_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp21_gic_cfg *arg);
	void (*gic_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en);
	void (*dhaz_config)(struct rkisp_isp_params_vdev *params_vdev,
			    const struct isp21_dhaz_cfg *arg);
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
			   const struct isp21_ynr_cfg *arg);
	void (*ynr_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en, const struct isp21_ynr_cfg *arg);
	void (*cnr_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp21_cnr_cfg *arg);
	void (*cnr_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en, const struct isp21_cnr_cfg *arg);
	void (*sharp_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp21_sharp_cfg *arg);
	void (*sharp_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en);
	void (*baynr_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp21_baynr_cfg *arg);
	void (*baynr_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en);
	void (*bay3d_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct isp21_bay3d_cfg *arg);
	void (*bay3d_enable)(struct rkisp_isp_params_vdev *params_vdev,
			     bool en);
	void (*csm_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp21_csm_cfg *arg);
	void (*cgc_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct isp21_cgc_cfg *arg);
};

struct rkisp_isp_params_val_v21 {
	struct rkisp_dummy_buffer buf_3dlut[RKISP_PARAM_3DLUT_BUF_NUM];
	u32 buf_3dlut_idx;

	struct rkisp_dummy_buffer buf_ldch[ISP2X_LDCH_BUF_NUM];
	u32 buf_ldch_idx;

	struct rkisp_dummy_buffer buf_lsclut[RKISP_PARAM_LSC_LUT_BUF_NUM];
	u32 buf_lsclut_idx;

	struct rkisp_dummy_buffer buf_3dnr;

	u8 dhaz_en;
	u8 wdr_en;
	u8 tmo_en;
	u8 lsc_en;
	u8 mge_en;
};

#if IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISP_VERSION_V21)
int rkisp_init_params_vdev_v21(struct rkisp_isp_params_vdev *params_vdev);
void rkisp_uninit_params_vdev_v21(struct rkisp_isp_params_vdev *params_vdev);
#else
static inline int rkisp_init_params_vdev_v21(struct rkisp_isp_params_vdev *params_vdev) { return 0; }
static inline void rkisp_uninit_params_vdev_v21(struct rkisp_isp_params_vdev *params_vdev) {}
#endif

#endif /* _RKISP_ISP_PARAM_V21_H */
