/*
 *
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#ifndef __AP1302_H__
#define __AP1302_H__

#include "../include/linux/atomisp_platform.h"
#include <linux/regmap.h>
#include <linux/types.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#define AP1302_NAME		"ap1302"
#define AP1302_CHIP_ID		0x265
#define AP1302_I2C_MAX_LEN	65534
#define AP1302_FW_WINDOW_OFFSET	0x8000
#define AP1302_FW_WINDOW_SIZE	0x2000

#define AP1302_REG16		2
#define AP1302_REG32		4

#define REG_CHIP_VERSION	0x0000
#define REG_CHIP_REV		0x0050
#define REG_MF_ID		0x0004
#define REG_ERROR		0x0006
#define REG_CTRL		0x1000
#define REG_DZ_TGT_FCT		0x1010
#define REG_SFX_MODE		0x1016
#define REG_SS_HEAD_PT0		0x1174
#define REG_AE_BV_OFF		0x5014
#define REG_AE_BV_BIAS		0x5016
#define REG_AWB_CTRL		0x5100
#define REG_FLICK_CTRL		0x5440
#define REG_SCENE_CTRL		0x5454
#define REG_BOOTDATA_STAGE	0x6002
#define REG_SENSOR_SELECT	0x600C
#define REG_SYS_START		0x601A
#define REG_SIP_CRC		0xF052

#define REG_PREVIEW_BASE	0x2000
#define REG_SNAPSHOT_BASE	0x3000
#define REG_VIDEO_BASE		0x4000
#define CNTX_WIDTH		0x00
#define CNTX_HEIGHT		0x02
#define CNTX_ROI_X0		0x04
#define CNTX_ROI_Y0		0x06
#define CNTX_ROI_X1		0x08
#define CNTX_ROI_Y1		0x0A
#define CNTX_ASPECT		0x0C
#define CNTX_LOCK		0x0E
#define CNTX_ENABLE		0x10
#define CNTX_OUT_FMT		0x12
#define CNTX_SENSOR_MODE	0x14
#define CNTX_MIPI_CTRL		0x16
#define CNTX_MIPI_II_CTRL	0x18
#define CNTX_LINE_TIME		0x1C
#define CNTX_MAX_FPS		0x20
#define CNTX_AE_USG		0x22
#define CNTX_AE_UPPER_ET	0x24
#define CNTX_AE_MAX_ET		0x28
#define CNTX_SS			0x2C
#define CNTX_S1_SENSOR_MODE	0x2E
#define CNTX_HINF_CTRL		0x30

#define CTRL_CNTX_MASK		0x03
#define CTRL_CNTX_OFFSET	0x00
#define HINF_CTRL_LANE_MASK	0x07
#define HINF_CTRL_LANE_OFFSET	0x00
#define MIPI_CTRL_IMGVC_MASK	0xC0
#define MIPI_CTRL_IMGVC_OFFSET	0x06
#define MIPI_CTRL_IMGTYPE_AUTO	0x3F
#define MIPI_CTRL_SSVC_MASK	0xC000
#define MIPI_CTRL_SSVC_OFFSET	0x0E
#define MIPI_CTRL_SSTYPE_MASK	0x3F00
#define MIPI_CTRL_SSTYPE_OFFSET	0x08
#define OUT_FMT_IIS_MASK	0x30
#define OUT_FMT_IIS_OFFSET	0x08
#define OUT_FMT_SS_MASK		0x1000
#define OUT_FMT_SS_OFFSET	0x12
#define OUT_FMT_TYPE_MASK	0xFF
#define SENSOR_SELECT_MASK	0x03
#define SENSOR_SELECT_OFFSET	0x00
#define AWB_CTRL_MODE_MASK	0x0F
#define AWB_CTRL_MODE_OFFSET	0x00
#define AWB_CTRL_FLASH_MASK	0x100

#define AP1302_FMT_UYVY422	0x50

#define AP1302_SYS_ACTIVATE	0x8010
#define AP1302_SYS_SWITCH	0x8140
#define AP1302_SENSOR_PRI	0x01
#define AP1302_SENSOR_SEC	0x02
#define AP1302_SS_CTRL		0x31

#define AP1302_MAX_RATIO_MISMATCH	10 /* Unit in percentage */
#define AP1302_MAX_EV		2
#define AP1302_MIN_EV		-2

enum ap1302_contexts {
	CONTEXT_PREVIEW = 0,
	CONTEXT_SNAPSHOT,
	CONTEXT_VIDEO,
	CONTEXT_NUM
};

/* The context registers are defined according to preview/video registers.
   Preview and video context have the same register definition.
   But snapshot context does not have register S1_SENSOR_MODE.
   When setting snapshot registers, if the offset exceeds
   S1_SENSOR_MODE, the actual offset needs to minus 2. */
struct ap1302_context_config {
	u16 width;
	u16 height;
	u16 roi_x0;
	u16 roi_y0;
	u16 roi_x1;
	u16 roi_y1;
	u16 aspect_factor;
	u16 lock;
	u16 enable;
	u16 out_fmt;
	u16 sensor_mode;
	u16 mipi_ctrl;
	u16 mipi_ii_ctrl;
	u16 padding;
	u32 line_time;
	u16 max_fps;
	u16 ae_usg;
	u32 ae_upper_et;
	u32 ae_max_et;
	u16 ss;
	u16 s1_sensor_mode;
	u16 hinf_ctrl;
	u32 reserved;
};

struct ap1302_res_struct {
	u16 width;
	u16 height;
	u16 fps;
};

struct ap1302_context_res {
	u32 res_num;
	u32 cur_res;
	struct ap1302_res_struct *res_table;
};

struct ap1302_device {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct camera_sensor_platform_data *platform_data;
	const struct firmware *fw;
	struct mutex input_lock; /* serialize sensor's ioctl */
	struct v4l2_mbus_framefmt format;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *run_mode;
	struct ap1302_context_config cntx_config[CONTEXT_NUM];
	struct ap1302_context_res cntx_res[CONTEXT_NUM];
	enum ap1302_contexts cur_context;
	unsigned int num_lanes;
	struct regmap *regmap16;
	struct regmap *regmap32;
	bool sys_activated;
	bool power_on;
};

struct ap1302_firmware {
	u32 crc;
	u32 pll_init_size;
	u32 total_size;
	u32 reserved;
};

struct ap1302_context_info {
	u16 offset;
	u16 len;
	char *name;
};

#endif
