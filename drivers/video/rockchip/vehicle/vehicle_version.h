/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Vehicle driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */

#ifndef _RKVEHICLE_VERSION_H
#define _RKVEHICLE_VERSION_H

#include <linux/version.h>

/*
 *RKVEHICLE DRIVER VERSION NOTE
 *
 * V0.0X01.0X00 first version.
 *  1. add support rk356x dvp/mipi fast vehicle reverse
 *  2. add sample dvp interface sensor gc2145 for test
 *  3. add sample mipi interface sensor nvp6314 one channel for test
 *  4. fixup rga old/new format transform issue
 * V0.0X01.0X01 fixup rga yuvtorgb transform issue
 * V0.0X01.0X02 modify debug log issue
 * V0.0X01.0X03 fix vehicle reverse close crash issue
 * V0.0X01.0X04 fix vehicle reverse reopen not ok issue
 * V0.0X01.0X05 fix after add hwc reserved plane patch, but not use reverse display issue.
 * V0.0X01.0X06 fix reverse open/close probably stay in reverse preview issue.
 * V0.0X01.0X07 rename function & remove deprecated code.
 * V0.0X01.0X08 use Esmart0-win0 plane for vehicle, for Esmart1 depend on Esmart0 open first
 * V0.0X01.0X09
 *  1. fix vehicle plane zpos not update issue
 *  2. use vop_drm_zpos 0x7 & not use drm_direct_disable_kernel_logo to fix kernel logo issue
 * V0.0X01.0Xa
 *  1. add cif output nv16 format to display support
 *  2. use parameter vehicle_dump_data to control dump data
 * V0.0X01.0Xb add cvbs in PAL/NTSC I format to mipi csi support
 * V0.0X01.0Xc fix format switch split issue:
 *     such as: PAL/NTSC I format switch to 720P, cause split problem;
 * V0.0X01.0Xd fix rk356x vehicle 1080P alloc_buffer_failed issue
 *     nvp6324 default use 1080p for test.
 * V0.0X01.0Xe use dummy buffer when request buffer failed case
 *     fix flicker issue
 * V0.0X01.0Xf set ddr scene to fix reverse sys stuck issue
 * V0.0X02.0X0
 *  1. add mipi csi2 hw soft reset
 *  2. add ahd hot plug support, sample driver: vehicle_ad_nvp6324.c
 * V0.0X02.0X1
 *  1. support quit vehicle, switch to normal v4l2 driver
 *  2. sample: vehicle_ad_nvp6324.c, vehicle_ad_gc2145.c
 *  3. switch cmd: echo 88 > /dev/vehicle
 * V0.0x02.0x2 support rk3588 csi2_dphy in kernel-5.10
 * V0.0x02.0x3 support rk3588 csi2_dcphy
 * V0.0x02.0x4 fix some rga3 ioctl and drm interface in kernel-5.10 for rk3588
 * V0.0X02.0X5 support rk3588 dvp interface sensor
 * V0.0X02.0X6 add dts phy_node to adapt different csi2_dphy or dvp sensor
 * V0.0X02.0X7 adapt flinger driver to drm direct show interface
 * V0.0X02.0X8 remove rockchip_ion falloc buf
 * V0.0X02.0X9 fix RGA rotation error
 * V0.0X02.0Xa add support MIPI CONTINUOUS CLOCK
 * V0.0X02.0Xb add support config crtc and plane from dts
 *  1.default crtc video_port3
 *  2.default plane Esmart0-win0
 * V0.0X02.0Xc remove some gpio unnecessary code
 * V0.0X02.0Xd support samsung mipi_dcphy combo one driver
 * V0.0X02.0Xe add GMSL to MIPI max96714 driver support
 * V0.0X02.0Xf add nvp6188 driver support
 * V0.0X03.0X00 update driver
 *  1.fix some code errors
 *  2.default palne Esmart3-win0
 *  3.fix rotation parameters config from dts
 *  4.add vehicle_version.h
 */

#endif
