/*
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_MDEVICE_H_
#define FIMC_MDEVICE_H_

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#include "fimc-core.h"
#include "mipi-csis.h"

/* Group IDs of sensor, MIPI CSIS and the writeback subdevs. */
#define SENSOR_GROUP_ID		(1 << 8)
#define CSIS_GROUP_ID		(1 << 9)
#define WRITEBACK_GROUP_ID	(1 << 10)

#define FIMC_MAX_SENSORS	8
#define FIMC_MAX_CAMCLKS	2

struct fimc_csis_info {
	struct v4l2_subdev *sd;
	int id;
};

struct fimc_camclk_info {
	struct clk *clock;
	int use_count;
	unsigned long frequency;
};

/**
 * struct fimc_sensor_info - image data source subdev information
 * @pdata: sensor's atrributes passed as media device's platform data
 * @subdev: image sensor v4l2 subdev
 * @host: fimc device the sensor is currently linked to
 * @clk_on: sclk_cam clock's state associated with this subdev
 *
 * This data structure applies to image sensor and the writeback subdevs.
 */
struct fimc_sensor_info {
	struct s5p_fimc_isp_info *pdata;
	struct v4l2_subdev *subdev;
	struct fimc_dev *host;
	bool clk_on;
};

/**
 * struct fimc_md - fimc media device information
 * @csis: MIPI CSIS subdevs data
 * @sensor: array of registered sensor subdevs
 * @num_sensors: actual number of registered sensors
 * @camclk: external sensor clock information
 * @fimc: array of registered fimc devices
 * @media_dev: top level media device
 * @v4l2_dev: top level v4l2_device holding up the subdevs
 * @pdev: platform device this media device is hooked up into
 * @user_subdev_api: true if subdevs are not configured by the host driver
 * @slock: spinlock protecting @sensor array
 */
struct fimc_md {
	struct fimc_csis_info csis[CSIS_MAX_ENTITIES];
	struct fimc_sensor_info sensor[FIMC_MAX_SENSORS];
	int num_sensors;
	struct fimc_camclk_info camclk[FIMC_MAX_CAMCLKS];
	struct fimc_dev *fimc[FIMC_MAX_DEVS];
	struct media_device media_dev;
	struct v4l2_device v4l2_dev;
	struct platform_device *pdev;
	bool user_subdev_api;
	spinlock_t slock;
};

#define is_subdev_pad(pad) (pad == NULL || \
	media_entity_type(pad->entity) == MEDIA_ENT_T_V4L2_SUBDEV)

#define me_subtype(me) \
	((me->type) & (MEDIA_ENT_TYPE_MASK | MEDIA_ENT_SUBTYPE_MASK))

#define subdev_has_devnode(__sd) (__sd->flags & V4L2_SUBDEV_FL_HAS_DEVNODE)

static inline struct fimc_md *entity_to_fimc_mdev(struct media_entity *me)
{
	return me->parent == NULL ? NULL :
		container_of(me->parent, struct fimc_md, media_dev);
}

static inline void fimc_md_graph_lock(struct fimc_dev *fimc)
{
	BUG_ON(fimc->vid_cap.vfd == NULL);
	mutex_lock(&fimc->vid_cap.vfd->entity.parent->graph_mutex);
}

static inline void fimc_md_graph_unlock(struct fimc_dev *fimc)
{
	BUG_ON(fimc->vid_cap.vfd == NULL);
	mutex_unlock(&fimc->vid_cap.vfd->entity.parent->graph_mutex);
}

int fimc_md_set_camclk(struct v4l2_subdev *sd, bool on);
void fimc_pipeline_prepare(struct fimc_dev *fimc, struct media_entity *me);
int fimc_pipeline_initialize(struct fimc_dev *fimc, struct media_entity *me,
			     bool resume);
int fimc_pipeline_shutdown(struct fimc_dev *fimc);
int fimc_pipeline_s_power(struct fimc_dev *fimc, int state);
int fimc_pipeline_s_stream(struct fimc_dev *fimc, int state);

#endif
