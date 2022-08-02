/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Vehicle driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */
#ifndef __VEHICLE_CFG
#define __VEHICLE_CFG
#include <media/v4l2-mediabus.h>
#include <linux/rk-camera-module.h>

/* Driver information */
#define VEHICLE_DRIVER_NAME		"Vehicle"

static int vehicle_debug;
#define VEHICLE_DG(format, ...) do {	\
	if (vehicle_debug)	\
		pr_info("%s %s(%d): " format, __func__, __LINE__, ## __VA_ARGS__);	\
	} while (0)

#define VEHICLE_DGERR(format, ...)  \
	pr_info("%s %s(%d):" format, VEHICLE_DRIVER_NAME, __func__, __LINE__, ## __VA_ARGS__)
#define VEHICLE_INFO(format, ...)  \
	pr_info("%s %s(%d):" format, VEHICLE_DRIVER_NAME, __func__, __LINE__, ## __VA_ARGS__)

#define MAX_BUF_NUM (6)

#define CVBS_DOUBLE_FPS_MODE	/*PAL 50fps; NTSC 60fps*/

enum {
	CIF_INPUT_FORMAT_YUV = 0,
	CIF_INPUT_FORMAT_PAL = 2,
	CIF_INPUT_FORMAT_NTSC = 3,
	CIF_INPUT_FORMAT_RAW = 4,
	CIF_INPUT_FORMAT_JPEG = 5,
	CIF_INPUT_FORMAT_MIPI = 6,
	CIF_INPUT_FORMAT_PAL_SW_COMPOSITE = 0xff000000,
	CIF_INPUT_FORMAT_NTSC_SW_COMPOSITE = 0xfe000000,
};

enum {
	CIF_OUTPUT_FORMAT_422 = 0,
	CIF_OUTPUT_FORMAT_420 = 1,
};

struct vehicle_cfg {
	/* output */
	int width;
	int height;
	/* sensor output */
	int src_width;
	int src_height;
	/*
	 * action:	source video data input format.
	 * 000 - YUV
	 * 010 - PAL
	 * 011 - NTSC
	 * 100 - RAW
	 * 101 - JPEG
	 * 110 - MIPI
	 */
	int input_format;
	/*
	 * 0 - output is 422
	 * 1 - output is 420
	 */
	int output_format;
	/*
	 * YUV input order
	 * 00 - UYVY
	 * 01 - YVYU
	 * 10 - VYUY
	 * 11 - YUYV
	 */
	int yuv_order;
	/*
	 * ccir input order
	 * 0 : odd field first
	 * 1 : even field first
	 */
	int field_order;

	/*
	 * BT.656 not use
	 * BT.601 hsync polarity
	 * val:
	 * 0-low active
	 * 1-high active
	 */
	int href;
	/*
	 * BT.656 not use
	 * BT.601 hsync polarity
	 * val :
	 * 0-low active
	 * 1-high active
	 */
	int vsync;

	/*
	 * enum v4l2_mbus_type - media bus type
	 * @V4L2_MBUS_PARALLEL: parallel interface with hsync and vsync
	 * @V4L2_MBUS_BT656:	parallel interface with embedded synchronisation, can
	 *			also be used for BT.1120
	 * @V4L2_MBUS_CSI1: MIPI CSI-1 serial interface
	 * @V4L2_MBUS_CCP2: CCP2 (Compact Camera Port 2)
	 * @V4L2_MBUS_CSI2: MIPI CSI-2 serial interface
	 */
	enum v4l2_mbus_type type;

	/*
	 * Signal polarity flags
	 * Note: in BT.656 mode HSYNC, FIELD, and VSYNC are unused
	 * V4L2_MBUS_[HV]SYNC* flags should be also used for specifying
	 * configuration of hardware that uses [HV]REF signals
	 */
	unsigned int mbus_flags;

	/*
	 * Note: in BT.656/601 mode mipi_freq are unused
	 * only used when v4l2_mbus_type is V4L2_MBUS_CSI2
	 */
	s64 mipi_freq;
	/*
	 * Note: in BT.656/601 mode mipi_freq are unused
	 * only used when v4l2_mbus_type is V4L2_MBUS_CSI2
	 */
	int lanes;

	u32 mbus_code;

	int start_x;
	int start_y;
	int frame_rate;

	unsigned int buf_phy_addr[MAX_BUF_NUM];
	unsigned int buf_num;
	int ad_ready;
	/*0:no, 1:90; 2:180; 4:270; 0x10:mirror-y; 0x20:mirror-x*/
	int rotate_mirror;
	struct rkmodule_csi_dphy_param *dphy_param;
};

#endif
