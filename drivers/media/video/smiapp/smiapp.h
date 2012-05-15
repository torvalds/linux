/*
 * drivers/media/video/smiapp/smiapp.h
 *
 * Generic driver for SMIA/SMIA++ compliant camera modules
 *
 * Copyright (C) 2010--2012 Nokia Corporation
 * Contact: Sakari Ailus <sakari.ailus@maxwell.research.nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __SMIAPP_PRIV_H_
#define __SMIAPP_PRIV_H_

#include <linux/mutex.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/smiapp.h>

#include "smiapp-pll.h"
#include "smiapp-reg.h"
#include "smiapp-regs.h"
#include "smiapp-quirk.h"

/*
 * Standard SMIA++ constants
 */
#define SMIA_VERSION_1			10
#define SMIAPP_VERSION_0_8		8 /* Draft 0.8 */
#define SMIAPP_VERSION_0_9		9 /* Draft 0.9 */
#define SMIAPP_VERSION_1		10

#define SMIAPP_PROFILE_0		0
#define SMIAPP_PROFILE_1		1
#define SMIAPP_PROFILE_2		2

#define SMIAPP_NVM_PAGE_SIZE		64	/* bytes */

#define SMIAPP_RESET_DELAY_CLOCKS	2400
#define SMIAPP_RESET_DELAY(clk)				\
	(1000 +	(SMIAPP_RESET_DELAY_CLOCKS * 1000	\
		 + (clk) / 1000 - 1) / ((clk) / 1000))

#include "smiapp-limits.h"

struct smiapp_quirk;

#define SMIAPP_MODULE_IDENT_FLAG_REV_LE		(1 << 0)

struct smiapp_module_ident {
	u8 manufacturer_id;
	u16 model_id;
	u8 revision_number_major;

	u8 flags;

	char *name;
	const struct smiapp_quirk *quirk;
};

struct smiapp_module_info {
	u32 manufacturer_id;
	u32 model_id;
	u32 revision_number_major;
	u32 revision_number_minor;

	u32 module_year;
	u32 module_month;
	u32 module_day;

	u32 sensor_manufacturer_id;
	u32 sensor_model_id;
	u32 sensor_revision_number;
	u32 sensor_firmware_version;

	u32 smia_version;
	u32 smiapp_version;

	u32 smiapp_profile;

	char *name;
	const struct smiapp_quirk *quirk;
};

#define SMIAPP_IDENT_FQ(manufacturer, model, rev, fl, _name, _quirk)	\
	{ .manufacturer_id = manufacturer,				\
	  .model_id = model,						\
	  .revision_number_major = rev,					\
	  .flags = fl,							\
	  .name = _name,						\
	  .quirk = _quirk, }

#define SMIAPP_IDENT_LQ(manufacturer, model, rev, _name, _quirk)	\
	{ .manufacturer_id = manufacturer,				\
	  .model_id = model,						\
	  .revision_number_major = rev,					\
	  .flags = SMIAPP_MODULE_IDENT_FLAG_REV_LE,			\
	  .name = _name,						\
	  .quirk = _quirk, }

#define SMIAPP_IDENT_L(manufacturer, model, rev, _name)			\
	{ .manufacturer_id = manufacturer,				\
	  .model_id = model,						\
	  .revision_number_major = rev,					\
	  .flags = SMIAPP_MODULE_IDENT_FLAG_REV_LE,			\
	  .name = _name, }

#define SMIAPP_IDENT_Q(manufacturer, model, rev, _name, _quirk)		\
	{ .manufacturer_id = manufacturer,				\
	  .model_id = model,						\
	  .revision_number_major = rev,					\
	  .flags = 0,							\
	  .name = _name,						\
	  .quirk = _quirk, }

#define SMIAPP_IDENT(manufacturer, model, rev, _name)			\
	{ .manufacturer_id = manufacturer,				\
	  .model_id = model,						\
	  .revision_number_major = rev,					\
	  .flags = 0,							\
	  .name = _name, }

struct smiapp_reg_limits {
	u32 addr;
	char *what;
};

extern struct smiapp_reg_limits smiapp_reg_limits[];

struct smiapp_csi_data_format {
	u32 code;
	u8 width;
	u8 compressed;
	u8 pixel_order;
};

#define SMIAPP_SUBDEVS			3

#define SMIAPP_PA_PAD_SRC		0
#define SMIAPP_PAD_SINK			0
#define SMIAPP_PAD_SRC			1
#define SMIAPP_PADS			2

struct smiapp_binning_subtype {
	u8 horizontal:4;
	u8 vertical:4;
} __packed;

struct smiapp_subdev {
	struct v4l2_subdev sd;
	struct media_pad pads[2];
	struct v4l2_rect sink_fmt;
	struct v4l2_rect crop[2];
	struct v4l2_rect compose; /* compose on sink */
	unsigned short sink_pad;
	unsigned short source_pad;
	int npads;
	struct smiapp_sensor *sensor;
	struct v4l2_ctrl_handler ctrl_handler;
};

/*
 * struct smiapp_sensor - Main device structure
 */
struct smiapp_sensor {
	/*
	 * "mutex" is used to serialise access to all fields here
	 * except v4l2_ctrls at the end of the struct. "mutex" is also
	 * used to serialise access to file handle specific
	 * information. The exception to this rule is the power_mutex
	 * below.
	 */
	struct mutex mutex;
	/*
	 * power_mutex is used to serialise power management related
	 * activities. Acquiring "mutex" at that time isn't necessary
	 * since there are no other users anyway.
	 */
	struct mutex power_mutex;
	struct smiapp_subdev ssds[SMIAPP_SUBDEVS];
	u32 ssds_used;
	struct smiapp_subdev *src;
	struct smiapp_subdev *binner;
	struct smiapp_subdev *scaler;
	struct smiapp_subdev *pixel_array;
	struct smiapp_platform_data *platform_data;
	struct regulator *vana;
	u32 limits[SMIAPP_LIMIT_LAST];
	u8 nbinning_subtypes;
	struct smiapp_binning_subtype binning_subtypes[SMIAPP_BINNING_SUBTYPES];
	u32 mbus_frame_fmts;
	const struct smiapp_csi_data_format *csi_format;
	const struct smiapp_csi_data_format *internal_csi_format;
	u32 default_mbus_frame_fmts;
	int default_pixel_order;

	u8 binning_horizontal;
	u8 binning_vertical;

	u8 scale_m;
	u8 scaling_mode;

	u8 hvflip_inv_mask; /* H/VFLIP inversion due to sensor orientation */
	u8 flash_capability;
	u8 frame_skip;

	int power_count;

	bool streaming;
	bool dev_init_done;

	u8 *nvm;		/* nvm memory buffer */
	unsigned int nvm_size;	/* bytes */

	struct smiapp_module_info minfo;

	struct smiapp_pll pll;

	/* Pixel array controls */
	struct v4l2_ctrl *analog_gain;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *pixel_rate_parray;
	/* src controls */
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate_csi;
};

#define to_smiapp_subdev(_sd)				\
	container_of(_sd, struct smiapp_subdev, sd)

#define to_smiapp_sensor(_sd)	\
	(to_smiapp_subdev(_sd)->sensor)

#endif /* __SMIAPP_PRIV_H_ */
