/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#ifndef UFSHCD_PLTFRM_H_
#define UFSHCD_PLTFRM_H_

#include <ufs/ufshcd.h>

#define UFS_PWM_MODE 1
#define UFS_HS_MODE  2

struct ufs_dev_params {
	u32 pwm_rx_gear;        /* pwm rx gear to work in */
	u32 pwm_tx_gear;        /* pwm tx gear to work in */
	u32 hs_rx_gear;         /* hs rx gear to work in */
	u32 hs_tx_gear;         /* hs tx gear to work in */
	u32 rx_lanes;           /* number of rx lanes */
	u32 tx_lanes;           /* number of tx lanes */
	u32 rx_pwr_pwm;         /* rx pwm working pwr */
	u32 tx_pwr_pwm;         /* tx pwm working pwr */
	u32 rx_pwr_hs;          /* rx hs working pwr */
	u32 tx_pwr_hs;          /* tx hs working pwr */
	u32 hs_rate;            /* rate A/B to work in HS */
	u32 desired_working_mode;
};

int ufshcd_get_pwr_dev_param(struct ufs_dev_params *dev_param,
			     struct ufs_pa_layer_attr *dev_max,
			     struct ufs_pa_layer_attr *agreed_pwr);
void ufshcd_init_pwr_dev_param(struct ufs_dev_params *dev_param);
int ufshcd_pltfrm_init(struct platform_device *pdev,
		       const struct ufs_hba_variant_ops *vops);
void ufshcd_pltfrm_shutdown(struct platform_device *pdev);

#endif /* UFSHCD_PLTFRM_H_ */
