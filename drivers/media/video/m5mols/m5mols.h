/*
 * Header for M-5MOLS 8M Pixel camera sensor with ISP
 *
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 * Author: HeungJun Kim <riverful.kim@samsung.com>
 *
 * Copyright (C) 2009 Samsung Electronics Co., Ltd.
 * Author: Dongsoo Nathaniel Kim <dongsoo45.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef M5MOLS_H
#define M5MOLS_H

#include <media/v4l2-subdev.h>
#include "m5mols_reg.h"

extern int m5mols_debug;

#define to_m5mols(__sd)	container_of(__sd, struct m5mols_info, sd)

#define to_sd(__ctrl) \
	(&container_of(__ctrl->handler, struct m5mols_info, handle)->sd)

enum m5mols_restype {
	M5MOLS_RESTYPE_MONITOR,
	M5MOLS_RESTYPE_CAPTURE,
	M5MOLS_RESTYPE_MAX,
};

/**
 * struct m5mols_resolution - structure for the resolution
 * @type: resolution type according to the pixel code
 * @width: width of the resolution
 * @height: height of the resolution
 * @reg: resolution preset register value
 */
struct m5mols_resolution {
	u8 reg;
	enum m5mols_restype type;
	u16 width;
	u16 height;
};

/**
 * struct m5mols_exif - structure for the EXIF information of M-5MOLS
 * @exposure_time: exposure time register value
 * @shutter_speed: speed of the shutter register value
 * @aperture: aperture register value
 * @exposure_bias: it calls also EV bias
 * @iso_speed: ISO register value
 * @flash: status register value of the flash
 * @sdr: status register value of the Subject Distance Range
 * @qval: not written exact meaning in document
 */
struct m5mols_exif {
	u32 exposure_time;
	u32 shutter_speed;
	u32 aperture;
	u32 brightness;
	u32 exposure_bias;
	u16 iso_speed;
	u16 flash;
	u16 sdr;
	u16 qval;
};

/**
 * struct m5mols_capture - Structure for the capture capability
 * @exif: EXIF information
 * @main: size in bytes of the main image
 * @thumb: size in bytes of the thumb image, if it was accompanied
 * @total: total size in bytes of the produced image
 */
struct m5mols_capture {
	struct m5mols_exif exif;
	u32 main;
	u32 thumb;
	u32 total;
};

/**
 * struct m5mols_scenemode - structure for the scenemode capability
 * @metering: metering light register value
 * @ev_bias: EV bias register value
 * @wb_mode: mode which means the WhiteBalance is Auto or Manual
 * @wb_preset: whitebalance preset register value in the Manual mode
 * @chroma_en: register value whether the Chroma capability is enabled or not
 * @chroma_lvl: chroma's level register value
 * @edge_en: register value Whether the Edge capability is enabled or not
 * @edge_lvl: edge's level register value
 * @af_range: Auto Focus's range
 * @fd_mode: Face Detection mode
 * @mcc: Multi-axis Color Conversion which means emotion color
 * @light: status of the Light
 * @flash: status of the Flash
 * @tone: Tone color which means Contrast
 * @iso: ISO register value
 * @capt_mode: Mode of the Image Stabilization while the camera capturing
 * @wdr: Wide Dynamic Range register value
 *
 * The each value according to each scenemode is recommended in the documents.
 */
struct m5mols_scenemode {
	u8 metering;
	u8 ev_bias;
	u8 wb_mode;
	u8 wb_preset;
	u8 chroma_en;
	u8 chroma_lvl;
	u8 edge_en;
	u8 edge_lvl;
	u8 af_range;
	u8 fd_mode;
	u8 mcc;
	u8 light;
	u8 flash;
	u8 tone;
	u8 iso;
	u8 capt_mode;
	u8 wdr;
};

/**
 * struct m5mols_version - firmware version information
 * @customer:	customer information
 * @project:	version of project information according to customer
 * @fw:		firmware revision
 * @hw:		hardware revision
 * @param:	version of the parameter
 * @awb:	Auto WhiteBalance algorithm version
 * @str:	information about manufacturer and packaging vendor
 * @af:		Auto Focus version
 *
 * The register offset starts the customer version at 0x0, and it ends
 * the awb version at 0x09. The customer, project information occupies 1 bytes
 * each. And also the fw, hw, param, awb each requires 2 bytes. The str is
 * unique string associated with firmware's version. It includes information
 * about manufacturer and the vendor of the sensor's packaging. The least
 * significant 2 bytes of the string indicate packaging manufacturer.
 */
#define VERSION_STRING_SIZE	22
struct m5mols_version {
	u8	customer;
	u8	project;
	u16	fw;
	u16	hw;
	u16	param;
	u16	awb;
	u8	str[VERSION_STRING_SIZE];
	u8	af;
};

/**
 * struct m5mols_info - M-5MOLS driver data structure
 * @pdata: platform data
 * @sd: v4l-subdev instance
 * @pad: media pad
 * @ffmt: current fmt according to resolution type
 * @res_type: current resolution type
 * @code: current code
 * @irq_waitq: waitqueue for the capture
 * @work_irq: workqueue for the IRQ
 * @flags: state variable for the interrupt handler
 * @handle: control handler
 * @autoexposure: Auto Exposure control
 * @exposure: Exposure control
 * @autowb: Auto White Balance control
 * @colorfx: Color effect control
 * @saturation:	Saturation control
 * @zoom: Zoom control
 * @ver: information of the version
 * @cap: the capture mode attributes
 * @power: current sensor's power status
 * @ctrl_sync: true means all controls of the sensor are initialized
 * @int_capture: true means the capture interrupt is issued once
 * @lock_ae: true means the Auto Exposure is locked
 * @lock_awb: true means the Aut WhiteBalance is locked
 * @resolution:	register value for current resolution
 * @interrupt: register value for current interrupt status
 * @mode: register value for current operation mode
 * @mode_save: register value for current operation mode for saving
 * @set_power: optional power callback to the board code
 */
struct m5mols_info {
	const struct m5mols_platform_data *pdata;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_mbus_framefmt ffmt[M5MOLS_RESTYPE_MAX];
	int res_type;
	enum v4l2_mbus_pixelcode code;
	wait_queue_head_t irq_waitq;
	struct work_struct work_irq;
	unsigned long flags;

	struct v4l2_ctrl_handler handle;
	/* Autoexposure/exposure control cluster */
	struct {
		struct v4l2_ctrl *autoexposure;
		struct v4l2_ctrl *exposure;
	};
	struct v4l2_ctrl *autowb;
	struct v4l2_ctrl *colorfx;
	struct v4l2_ctrl *saturation;
	struct v4l2_ctrl *zoom;

	struct m5mols_version ver;
	struct m5mols_capture cap;
	bool power;
	bool ctrl_sync;
	bool lock_ae;
	bool lock_awb;
	u8 resolution;
	u8 interrupt;
	u8 mode;
	u8 mode_save;
	int (*set_power)(struct device *dev, int on);
};

#define ST_CAPT_IRQ 0

#define is_powered(__info) (__info->power)
#define is_ctrl_synced(__info) (__info->ctrl_sync)
#define is_available_af(__info)	(__info->ver.af)
#define is_code(__code, __type) (__code == m5mols_default_ffmt[__type].code)
#define is_manufacturer(__info, __manufacturer)	\
				(__info->ver.str[0] == __manufacturer[0] && \
				 __info->ver.str[1] == __manufacturer[1])
/*
 * I2C operation of the M-5MOLS
 *
 * The I2C read operation of the M-5MOLS requires 2 messages. The first
 * message sends the information about the command, command category, and total
 * message size. The second message is used to retrieve the data specifed in
 * the first message
 *
 *   1st message                                2nd message
 *   +-------+---+----------+-----+-------+     +------+------+------+------+
 *   | size1 | R | category | cmd | size2 |     | d[0] | d[1] | d[2] | d[3] |
 *   +-------+---+----------+-----+-------+     +------+------+------+------+
 *   - size1: message data size(5 in this case)
 *   - size2: desired buffer size of the 2nd message
 *   - d[0..3]: according to size2
 *
 * The I2C write operation needs just one message. The message includes
 * category, command, total size, and desired data.
 *
 *   1st message
 *   +-------+---+----------+-----+------+------+------+------+
 *   | size1 | W | category | cmd | d[0] | d[1] | d[2] | d[3] |
 *   +-------+---+----------+-----+------+------+------+------+
 *   - d[0..3]: according to size1
 */
int m5mols_read_u8(struct v4l2_subdev *sd, u32 reg_comb, u8 *val);
int m5mols_read_u16(struct v4l2_subdev *sd, u32 reg_comb, u16 *val);
int m5mols_read_u32(struct v4l2_subdev *sd, u32 reg_comb, u32 *val);
int m5mols_write(struct v4l2_subdev *sd, u32 reg_comb, u32 val);
int m5mols_busy(struct v4l2_subdev *sd, u8 category, u8 cmd, u8 value);

/*
 * Mode operation of the M-5MOLS
 *
 * Changing the mode of the M-5MOLS is needed right executing order.
 * There are three modes(PARAMETER, MONITOR, CAPTURE) which can be changed
 * by user. There are various categories associated with each mode.
 *
 * +============================================================+
 * | mode	| category					|
 * +============================================================+
 * | FLASH	| FLASH(only after Stand-by or Power-on)	|
 * | SYSTEM	| SYSTEM(only after sensor arm-booting)		|
 * | PARAMETER	| PARAMETER					|
 * | MONITOR	| MONITOR(preview), Auto Focus, Face Detection	|
 * | CAPTURE	| Single CAPTURE, Preview(recording)		|
 * +============================================================+
 *
 * The available executing order between each modes are as follows:
 *   PARAMETER <---> MONITOR <---> CAPTURE
 */
int m5mols_mode(struct m5mols_info *info, u8 mode);

int m5mols_enable_interrupt(struct v4l2_subdev *sd, u8 reg);
int m5mols_sync_controls(struct m5mols_info *info);
int m5mols_start_capture(struct m5mols_info *info);
int m5mols_do_scenemode(struct m5mols_info *info, u8 mode);
int m5mols_lock_3a(struct m5mols_info *info, bool lock);
int m5mols_set_ctrl(struct v4l2_ctrl *ctrl);

/* The firmware function */
int m5mols_update_fw(struct v4l2_subdev *sd,
		     int (*set_power)(struct m5mols_info *, bool));

#endif	/* M5MOLS_H */
