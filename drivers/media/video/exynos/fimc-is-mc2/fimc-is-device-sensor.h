/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_IS_DEVICE_SENSOR_H
#define FIMC_IS_DEVICE_SENSOR_H

#include <linux/pm_qos.h>
#include "fimc-is-framemgr.h"
#include "fimc-is-interface.h"
#include "fimc-is-metadata.h"
#include "fimc-is-video.h"
#include "fimc-is-device-ischain.h"
#include "fimc-is-device-flite.h"

#define SENSOR_MAX_ENUM 100
#define SENSOR_DEFAULT_FRAMERATE	30

enum fimc_is_sensor_output_entity {
	FIMC_IS_SENSOR_OUTPUT_NONE = 0,
	FIMC_IS_SENSOR_OUTPUT_FRONT,
};

struct fimc_is_enum_sensor {
	u32 sensor;
	u32 pixel_width;
	u32 pixel_height;
	u32 active_width;
	u32 active_height;
	u32 max_framerate;
	u32 csi_ch;
	u32 flite_ch;
	u32 i2c_ch;
	struct sensor_open_extended ext;
	char *setfile_name;
};

enum fimc_is_sensor_state {
	FIMC_IS_SENSOR_OPEN,
	FIMC_IS_SENSOR_FRONT_START,
	FIMC_IS_SENSOR_BACK_START
};

struct fimc_is_device_sensor {
	int id_position;		/* 0 : rear camera, 1: front camera */
	u32 instance;
	u32 width;
	u32 height;
	u32 framerate;

	struct fimc_is_video_ctx	*vctx;
	struct fimc_is_device_ischain   *ischain;

	struct fimc_is_enum_sensor	enum_sensor[SENSOR_MAX_ENUM];
	struct fimc_is_enum_sensor	*active_sensor;

	unsigned long			state;
	spinlock_t			slock_state;

	void *dev_data;

	struct fimc_is_device_flite	flite;
};

int fimc_is_sensor_probe(struct fimc_is_device_sensor *device, u32 channel);
int fimc_is_sensor_open(struct fimc_is_device_sensor *device,
	struct fimc_is_video_ctx *vctx);
int fimc_is_sensor_close(struct fimc_is_device_sensor *device);
int fimc_is_sensor_s_active_sensor(struct fimc_is_device_sensor *device,
	struct fimc_is_video_ctx *vctx,
	struct fimc_is_framemgr *framemgr,
	u32 input);
int fimc_is_sensor_s_format(struct fimc_is_device_sensor *device,
	u32 width, u32 height);
int fimc_is_sensor_buffer_queue(struct fimc_is_device_sensor *device,
	u32 index);
int fimc_is_sensor_buffer_finish(struct fimc_is_device_sensor *device,
	u32 index);

int fimc_is_sensor_front_start(struct fimc_is_device_sensor *device);
int fimc_is_sensor_front_stop(struct fimc_is_device_sensor *device);
int fimc_is_sensor_back_start(struct fimc_is_device_sensor *device,
	struct fimc_is_video_ctx *vctx);
int fimc_is_sensor_back_stop(struct fimc_is_device_sensor *device);
int fimc_is_sensor_back_pause(struct fimc_is_device_sensor *device);
void fimc_is_sensor_back_restart(struct fimc_is_device_sensor *device);

int enable_mipi(void);

#endif
