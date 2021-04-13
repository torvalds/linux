/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKCIF_VERSION_H
#define _RKCIF_VERSION_H
#include <linux/version.h>
#include <linux/rkcif-config.h>

/*
 *RKCIF DRIVER VERSION NOTE
 *
 *v0.1.0:
 *1. First version;
 *v0.1.1
 *1. Support the mipi vc multi-channel input in cif driver for rk1808
 *v0.1.2
 *1. support output yuyv fmt by setting the input mode to raw8
 *2. Compatible with cif only have single dma mode in driver
 *3. Support cif works with mipi channel for rk3288
 *4. Support switching between oneframe and pingpong for cif
 *5. Support sampling raw data for cif
 *6. fix the bug that dummpy buffer size is error
 *7. Add framesizes and frmintervals callback
 *8. fix dvp camera fails to link with cif on rk1808
 *9. add camera support hotplug for n4
 *10. reconstruct register's reading and writing
 *v0.1.3
 *1. support kernel-4.19 and support vicap single dvp for rv1126
 *2. support vicap + mipi(single) for rv1126
 *3. support vicap + mipi hdr for rv1126
 *4. add luma device node for rv1126 vicap
 *v0.1.4
 *1. support vicap-full lvds interface to work in linear and hdr mode for rv1126
 *2. add vicap-lite device for rv1126
 *v0.1.5
 *1. support crop function
 *2. fix compile error when config with module
 *3. support mipi yuv
 *4. support selection ioctl for cropping
 *5. support cif compact mode(lvds & mipi) can be set from user space
 *v0.1.6
 *1. add cif self-defined ioctrl cmd:V4L2_CID_CIF_DATA_COMPACT
 *v0.1.7
 *1. support dvp and mipi/lvds run simultaneously
 *2. add subdev as interface for isp
 *3. support hdr_x3 mode
 *4. support rk1808 mipi interface in kernel-4.19
 *v0.1.8
 *1. add proc interface
 *2. add reset mechanism to resume when csi crc err
 *3. support bt1120 single path
 *v0.1.9
 *1. support rk3568 cif
 *2. support rk3568 csi-host
 *3. add dvp sof
 *4. add extended lines to out image for normal & hdr short frame
 *5. modify reset mechanism drivered by real-time frame rate
 *6. support rk356x iommu uses vb2 sg type
 *7. register cif sd itf when pipeline completed
 *v0.1.10
 *1. rv1126/rk356x support bt656/bt1120 multi channels function
 *2. add dynamic cropping function
 *3. optimize dts config of cif's pipeline
 *4. register cif itf dev when clear unready subdev
 */

#define RKCIF_DRIVER_VERSION RKCIF_API_VERSION

#endif
