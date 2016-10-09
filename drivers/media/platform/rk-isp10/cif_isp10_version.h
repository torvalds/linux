/*
 *************************************************************************
 * Rockchip driver for CIF ISP 1.0
 * (Based on Intel driver for sofiaxxx)
 *
 * Copyright (C) 2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *************************************************************************
 */
 #ifndef _CIF_ISP10_RK_VERSION_H_
#define _CIF_ISP10_RK_VERSION_H_
#include <linux/version.h>

/*
 *       CIF DRIVER VERSION NOTE
 *
 *v0.1.0:
 *1. New mi register update mode is invalidate in raw/jpeg for rk1108,
 * All path used old mode for rk1108;
 *v0.1.1:
 *1. Modify CIF stop sequence for fix isp bus may dead when switch isp:
 *Original stop sequence: Stop ISP(mipi) -> Stop ISP(isp) ->wait for ISP
 *isp off -> Stop ISP(mi)
 *Current stop sequence: ISP(mi) stop in mi frame end -> Stop ISP(mipi)
 *-> Stop ISP(isp) ->wait for ISP isp off;
 * Current stop sequence is only match sensor stream v-blanking >= 1.5ms;
 *
 *v0.1.2:
 *1. Disable CIF_MIPI_ERR_DPHY interrupt here temporary for
 *isp bus may be dead when switch isp;
 *2. Cancel hw restart isp operation in mipi isr, only notice error log;
 *
 *v0.1.3:
 *1. fix camerahal query exp info failed from cifisp_stat_buffer, because
 *wake_up buffer before cif_isp11_sensor_mode_data_sync;
 *
 *v0.1.4:
 *1. Disable DPHY errctrl interrupt, because this dphy erctrl signal
 *is assert and until the next changes in line state. This time is may
 *be too long and cpu is hold in this interrupt. Enable DPHY errctrl
 *interrupt again, if mipi have receive the whole frame without any error.
 *2. Modify mipi_dphy_cfg follow vendor recommended process in
 *document.
 *3. Select the limit dphy setting if sensor mipi datarate is overflow,
 *and print warning information to user.
 *
 *v0.1.5:
 *Exposure list must be queue operation, not stack. list_add switch to
 *list_add_tail in cif_isp11_s_exp;
 *
 *v0.1.6:
 *Add isp output size in struct isp_supplemental_sensor_mode_data.
 *
 */

#define CONFIG_CIFISP10_DRIVER_VERSION KERNEL_VERSION(0, 1, 6)

#endif
