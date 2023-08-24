/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * drivers/media/i2c/smiapp/ccs.h
 *
 * Generic driver for MIPI CCS/SMIA/SMIA++ compliant camera sensors
 *
 * Copyright (C) 2020 Intel Corporation
 * Copyright (C) 2010--2012 Nokia Corporation
 * Contact: Sakari Ailus <sakari.ailus@linux.intel.com>
 */

#ifndef __CCS_H__
#define __CCS_H__

#include <linux/mutex.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "ccs-data.h"
#include "ccs-limits.h"
#include "ccs-quirk.h"
#include "ccs-regs.h"
#include "ccs-reg-access.h"
#include "../ccs-pll.h"
#include "smiapp-reg-defs.h"

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

#define CCS_COLOUR_COMPONENTS		4

#define SMIAPP_NAME			"smiapp"
#define CCS_NAME			"ccs"

#define CCS_DFL_I2C_ADDR	(0x20 >> 1) /* Default I2C Address */
#define CCS_ALT_I2C_ADDR	(0x6e >> 1) /* Alternate I2C Address */

#define CCS_LIM(sensor, limit) \
	ccs_get_limit(sensor, CCS_L_##limit, 0)

#define CCS_LIM_AT(sensor, limit, offset)	\
	ccs_get_limit(sensor, CCS_L_##limit, CCS_L_##limit##_OFFSET(offset))

struct ccs_flash_strobe_parms {
	u8 mode;
	u32 strobe_width_high_us;
	u16 strobe_delay;
	u16 stobe_start_point;
	u8 trigger;
};

struct ccs_hwconfig {
	/*
	 * Change the cci address if i2c_addr_alt is set.
	 * Both default and alternate cci addr need to be present
	 */
	unsigned short i2c_addr_dfl;	/* Default i2c addr */
	unsigned short i2c_addr_alt;	/* Alternate i2c addr */

	u32 ext_clk;			/* sensor external clk */

	unsigned int lanes;		/* Number of CSI-2 lanes */
	u32 csi_signalling_mode;	/* CCS_CSI_SIGNALLING_MODE_* */
	u64 *op_sys_clock;

	struct ccs_flash_strobe_parms *strobe_setup;
};

struct ccs_quirk;

#define CCS_MODULE_IDENT_FLAG_REV_LE		(1 << 0)

struct ccs_module_ident {
	u16 mipi_manufacturer_id;
	u16 model_id;
	u8 smia_manufacturer_id;
	u8 revision_number_major;

	u8 flags;

	char *name;
	const struct ccs_quirk *quirk;
};

struct ccs_module_info {
	u32 smia_manufacturer_id;
	u32 mipi_manufacturer_id;
	u32 model_id;
	u32 revision_number;

	u32 module_year;
	u32 module_month;
	u32 module_day;

	u32 sensor_smia_manufacturer_id;
	u32 sensor_mipi_manufacturer_id;
	u32 sensor_model_id;
	u32 sensor_revision_number;
	u32 sensor_firmware_version;

	u32 smia_version;
	u32 smiapp_version;
	u32 ccs_version;

	char *name;
	const struct ccs_quirk *quirk;
};

#define CCS_IDENT_FQ(manufacturer, model, rev, fl, _name, _quirk)	\
	{ .smia_manufacturer_id = manufacturer,				\
	  .model_id = model,						\
	  .revision_number_major = rev,					\
	  .flags = fl,							\
	  .name = _name,						\
	  .quirk = _quirk, }

#define CCS_IDENT_LQ(manufacturer, model, rev, _name, _quirk)	\
	{ .smia_manufacturer_id = manufacturer,				\
	  .model_id = model,						\
	  .revision_number_major = rev,					\
	  .flags = CCS_MODULE_IDENT_FLAG_REV_LE,			\
	  .name = _name,						\
	  .quirk = _quirk, }

#define CCS_IDENT_L(manufacturer, model, rev, _name)			\
	{ .smia_manufacturer_id = manufacturer,				\
	  .model_id = model,						\
	  .revision_number_major = rev,					\
	  .flags = CCS_MODULE_IDENT_FLAG_REV_LE,			\
	  .name = _name, }

#define CCS_IDENT_Q(manufacturer, model, rev, _name, _quirk)		\
	{ .smia_manufacturer_id = manufacturer,				\
	  .model_id = model,						\
	  .revision_number_major = rev,					\
	  .flags = 0,							\
	  .name = _name,						\
	  .quirk = _quirk, }

#define CCS_IDENT(manufacturer, model, rev, _name)			\
	{ .smia_manufacturer_id = manufacturer,				\
	  .model_id = model,						\
	  .revision_number_major = rev,					\
	  .flags = 0,							\
	  .name = _name, }

struct ccs_csi_data_format {
	u32 code;
	u8 width;
	u8 compressed;
	u8 pixel_order;
};

#define CCS_SUBDEVS			3

#define CCS_PA_PAD_SRC			0
#define CCS_PAD_SINK			0
#define CCS_PAD_SRC			1
#define CCS_PADS			2

struct ccs_binning_subtype {
	u8 horizontal:4;
	u8 vertical:4;
} __packed;

struct ccs_subdev {
	struct v4l2_subdev sd;
	struct media_pad pads[CCS_PADS];
	unsigned short sink_pad;
	unsigned short source_pad;
	int npads;
	struct ccs_sensor *sensor;
	struct v4l2_ctrl_handler ctrl_handler;
};

/*
 * struct ccs_sensor - Main device structure
 */
struct ccs_sensor {
	/*
	 * "mutex" is used to serialise access to all fields here
	 * except v4l2_ctrls at the end of the struct. "mutex" is also
	 * used to serialise access to file handle specific
	 * information.
	 */
	struct mutex mutex;
	struct ccs_subdev ssds[CCS_SUBDEVS];
	u32 ssds_used;
	struct ccs_subdev *src;
	struct ccs_subdev *binner;
	struct ccs_subdev *scaler;
	struct ccs_subdev *pixel_array;
	struct ccs_hwconfig hwcfg;
	struct regulator_bulk_data *regulators;
	struct clk *ext_clk;
	struct gpio_desc *xshutdown;
	struct gpio_desc *reset;
	void *ccs_limits;
	u8 nbinning_subtypes;
	struct ccs_binning_subtype binning_subtypes[CCS_LIM_BINNING_SUB_TYPE_MAX_N + 1];
	u32 mbus_frame_fmts;
	const struct ccs_csi_data_format *csi_format;
	const struct ccs_csi_data_format *internal_csi_format;
	struct v4l2_rect pa_src, scaler_sink, src_src;
	u32 default_mbus_frame_fmts;
	int default_pixel_order;
	struct ccs_data_container sdata, mdata;

	u8 binning_horizontal;
	u8 binning_vertical;

	u8 scale_m;
	u8 scaling_mode;

	u8 frame_skip;
	u16 embedded_start; /* embedded data start line */
	u16 embedded_end;
	u16 image_start; /* image data start line */
	u16 visible_pixel_start; /* start pixel of the visible image */

	bool streaming;
	bool dev_init_done;
	u8 compressed_min_bpp;

	struct ccs_module_info minfo;

	struct ccs_pll pll;

	/* Is a default format supported for a given BPP? */
	unsigned long *valid_link_freqs;

	/* Pixel array controls */
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *pixel_rate_parray;
	struct v4l2_ctrl *luminance_level;
	/* src controls */
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate_csi;
	/* test pattern colour components */
	struct v4l2_ctrl *test_data[CCS_COLOUR_COMPONENTS];
};

#define to_ccs_subdev(_sd)				\
	container_of(_sd, struct ccs_subdev, sd)

#define to_ccs_sensor(_sd)	\
	(to_ccs_subdev(_sd)->sensor)

void ccs_replace_limit(struct ccs_sensor *sensor,
		       unsigned int limit, unsigned int offset, u32 val);
u32 ccs_get_limit(struct ccs_sensor *sensor, unsigned int limit,
		  unsigned int offset);

#endif /* __CCS_H__ */
