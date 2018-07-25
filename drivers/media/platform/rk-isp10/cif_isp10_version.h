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
 *v0.1.7:
 *Add support to isp1.
 *The running of isp0 or isp1 is ok,
 *but running of isp0 and isp1 at the same time has not been tested.
 *
 *v0.1.8:
 *Fix oops error when soc_clk_disable is called in rk3399.
 *
 *v0.1.9:
 *1. Support bt656 signal interlace: odd and even field interlace generating
 *a frame image.
 *2. fix cif_isp10_img_src_v4l2_subdev_enum_strm_fmts defrect info get error.
 *3. fix cif_isp10_rk3399 cif_clk_pll info doesn't match with dts config.
 *
 *v0.1.0xa
 *Based on version 0.1.9:
 *1. To optimize the readability of the code.
 *2. optimize CIF_MI_CTRL_BURST_LEN param.
 *3. Add the check for cam_itf.type(PLTFRM_CAM_ITF_BT656_8I) on
 * cif_isp10_s_fmt_mp.
 *4. get field_flag value from cif_isp10_isp_isr.
 *
 *v0.1.0xb
 *1. Initialize default format for current stream.
 *2. Implement command VIDIOC_G_FMT.
 *3. Set bytesused of each plane to its real size.
 *4. Support io mode 'VB2_DMABUF'.
 *
 *v0.1.0xc
 *1. support isp0 and isp1 run at the same time.
 *2. support VIDIOC_G_INPUT command.
 *3. support VIDIOC_G_PARM command.
 *4. support VIDIOC_G_PARM command.
 *5. add pix.bytesperline and pix.sizeimage in VIDIOC_G_FMT command.
 *
 *v0.1.0xd
 *1. Support RGB24(xRGB8888) format of SP Path.
 *
 *v0.1.0xe
 *1. fix owned_by_drv_count is not 0 when stop stream.
 *2. fix write fmt is not correct when setting mi_ctrl.
 *3. modify for dumpsys tool.
 *4. remove "Measurement late" check.
 *5. modify for af function.
 *6. add module parameter for dumpsys.
 *
 *v0.1.0xf
 *1. merge modification from rv1108 project.
 *
 *v0.2.0x0
 *1. fix compile warning.
 *2. add check of iommu status.
 *3. support stream on/off/on/off...
 *4. get correct isp out width/height.
 *5. fix the issue that setting of isp0 mipi affect txrx dphy.
 *6. fix the issue cannot set exposure by mp path device.
 *
 */

#define CONFIG_CIFISP10_DRIVER_VERSION KERNEL_VERSION(0, 2, 0x0)

#endif
