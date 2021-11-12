/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_ISP_PARAM_V1X_H
#define _RKISP_ISP_PARAM_V1X_H

#include <linux/rkisp1-config.h>
#include <linux/rk-preisp.h>
#include "common.h"
#include "isp_params.h"

struct rkisp_isp_params_vdev;
struct rkisp_isp_params_v1x_ops {
	void (*dpcc_config)(struct rkisp_isp_params_vdev *params_vdev,
			    const struct cifisp_dpcc_config *arg);
	void (*bls_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct cifisp_bls_config *arg);
	void (*lsc_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct cifisp_lsc_config *arg);
	void (*lsc_matrix_config)(struct rkisp_isp_params_vdev *params_vdev,
				  const struct cifisp_lsc_config *pconfig);
	void (*flt_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct cifisp_flt_config *arg);
	void (*bdm_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct cifisp_bdm_config *arg);
	void (*sdg_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct cifisp_sdg_config *arg);
	void (*goc_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct cifisp_goc_config *arg);
	void (*ctk_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct cifisp_ctk_config *arg);
	void (*ctk_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   bool en);
	void (*awb_meas_config)(struct rkisp_isp_params_vdev *params_vdev,
				const struct cifisp_awb_meas_config *arg);
	void (*awb_meas_enable)(struct rkisp_isp_params_vdev *params_vdev,
				const struct cifisp_awb_meas_config *arg,
				bool en);
	void (*awb_gain_config)(struct rkisp_isp_params_vdev *params_vdev,
				const struct cifisp_awb_gain_config *arg);
	void (*aec_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct cifisp_aec_config *arg);
	void (*cproc_config)(struct rkisp_isp_params_vdev *params_vdev,
			     const struct cifisp_cproc_config *arg);
	void (*hst_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct cifisp_hst_config *arg);
	void (*hst_enable)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct cifisp_hst_config *arg, bool en);
	void (*afm_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct cifisp_afc_config *arg);
	void (*ie_config)(struct rkisp_isp_params_vdev *params_vdev,
			  const struct cifisp_ie_config *arg);
	void (*ie_enable)(struct rkisp_isp_params_vdev *params_vdev,
			  bool en);
	void (*csm_config)(struct rkisp_isp_params_vdev *params_vdev,
			   bool full_range);
	void (*dpf_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct cifisp_dpf_config *arg);
	void (*dpf_strength_config)(struct rkisp_isp_params_vdev *params_vdev,
				const struct cifisp_dpf_strength_config *arg);
	void (*wdr_config)(struct rkisp_isp_params_vdev *params_vdev,
			   const struct cifisp_wdr_config *arg);
	void (*wdr_enable)(struct rkisp_isp_params_vdev *params_vdev, bool en);
	void (*demosaiclp_config)(struct rkisp_isp_params_vdev *params_vdev,
				  const struct cifisp_demosaiclp_config *arg);
	void (*demosaiclp_enable)(struct rkisp_isp_params_vdev *params_vdev,
				  bool en);
	void (*rkiesharp_config)(struct rkisp_isp_params_vdev *params_vdev,
				 const struct cifisp_rkiesharp_config *arg);
	void (*rkiesharp_enable)(struct rkisp_isp_params_vdev *params_vdev,
				 bool en);
};

struct rkisp_isp_params_v1x_config {
	const int gamma_out_max_samples;
	const int hst_weight_grids_size;
};

#if IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISP_VERSION_V1X)
int rkisp_init_params_vdev_v1x(struct rkisp_isp_params_vdev *params_vdev);
void rkisp_uninit_params_vdev_v1x(struct rkisp_isp_params_vdev *params_vdev);
#else
static inline int rkisp_init_params_vdev_v1x(struct rkisp_isp_params_vdev *params_vdev) { return -EINVAL; }
static inline void rkisp_uninit_params_vdev_v1x(struct rkisp_isp_params_vdev *params_vdev) {}
#endif

#endif /* _RKISP_ISP_PARAM_V1X_H */
