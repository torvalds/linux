/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2011 - 2012 Samsung Electronics Co., Ltd.
 */

#ifndef FIMC_MDEVICE_H_
#define FIMC_MDEVICE_H_

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/drv-intf/exynos-fimc.h>

#include "fimc-core.h"
#include "fimc-lite.h"
#include "mipi-csis.h"

#define FIMC_OF_NODE_NAME	"fimc"
#define FIMC_LITE_OF_NODE_NAME	"fimc-lite"
#define FIMC_IS_OF_NODE_NAME	"fimc-is"
#define CSIS_OF_NODE_NAME	"csis"

#define PINCTRL_STATE_IDLE	"idle"

#define FIMC_MAX_SENSORS	4
#define FIMC_MAX_CAMCLKS	2
#define DEFAULT_SENSOR_CLK_FREQ	24000000U

/* LCD/ISP Writeback clocks (PIXELASYNCMx) */
enum {
	CLK_IDX_WB_A,
	CLK_IDX_WB_B,
	FIMC_MAX_WBCLKS
};

enum fimc_subdev_index {
	IDX_SENSOR,
	IDX_CSIS,
	IDX_FLITE,
	IDX_IS_ISP,
	IDX_FIMC,
	IDX_MAX,
};

/*
 * This structure represents a chain of media entities, including a data
 * source entity (e.g. an image sensor subdevice), a data capture entity
 * - a video capture device node and any remaining entities.
 */
struct fimc_pipeline {
	struct exynos_media_pipeline ep;
	struct list_head list;
	struct media_entity *vdev_entity;
	struct v4l2_subdev *subdevs[IDX_MAX];
};

#define to_fimc_pipeline(_ep) container_of(_ep, struct fimc_pipeline, ep)

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
 * @pdata: sensor's attributes passed as media device's platform data
 * @asd: asynchronous subdev registration data structure
 * @subdev: image sensor v4l2 subdev
 * @host: fimc device the sensor is currently linked to
 *
 * This data structure applies to image sensor and the writeback subdevs.
 */
struct fimc_sensor_info {
	struct fimc_source_info pdata;
	struct v4l2_async_subdev asd;
	struct v4l2_subdev *subdev;
	struct fimc_dev *host;
};

struct cam_clk {
	struct clk_hw hw;
	struct fimc_md *fmd;
};
#define to_cam_clk(_hw) container_of(_hw, struct cam_clk, hw)

/**
 * struct fimc_md - fimc media device information
 * @csis: MIPI CSIS subdevs data
 * @sensor: array of registered sensor subdevs
 * @num_sensors: actual number of registered sensors
 * @camclk: external sensor clock information
 * @fimc: array of registered fimc devices
 * @fimc_is: fimc-is data structure
 * @use_isp: set to true when FIMC-IS subsystem is used
 * @pmf: handle to the CAMCLK clock control FIMC helper device
 * @media_dev: top level media device
 * @v4l2_dev: top level v4l2_device holding up the subdevs
 * @pdev: platform device this media device is hooked up into
 * @pinctrl: camera port pinctrl handle
 * @state_default: pinctrl default state handle
 * @state_idle: pinctrl idle state handle
 * @cam_clk_provider: CAMCLK clock provider structure
 * @user_subdev_api: true if subdevs are not configured by the host driver
 * @slock: spinlock protecting @sensor array
 */
struct fimc_md {
	struct fimc_csis_info csis[CSIS_MAX_ENTITIES];
	struct fimc_sensor_info sensor[FIMC_MAX_SENSORS];
	int num_sensors;
	struct fimc_camclk_info camclk[FIMC_MAX_CAMCLKS];
	struct clk *wbclk[FIMC_MAX_WBCLKS];
	struct fimc_lite *fimc_lite[FIMC_LITE_MAX_DEVS];
	struct fimc_dev *fimc[FIMC_MAX_DEVS];
	struct fimc_is *fimc_is;
	bool use_isp;
	struct device *pmf;
	struct media_device media_dev;
	struct v4l2_device v4l2_dev;
	struct platform_device *pdev;

	struct fimc_pinctrl {
		struct pinctrl *pinctrl;
		struct pinctrl_state *state_default;
		struct pinctrl_state *state_idle;
	} pinctl;

	struct cam_clk_provider {
		struct clk *clks[FIMC_MAX_CAMCLKS];
		struct clk_onecell_data clk_data;
		struct device_node *of_node;
		struct cam_clk camclk[FIMC_MAX_CAMCLKS];
		int num_clocks;
	} clk_provider;

	struct v4l2_async_notifier subdev_notifier;

	bool user_subdev_api;
	spinlock_t slock;
	struct list_head pipelines;
	struct media_graph link_setup_graph;
};

static inline
struct fimc_sensor_info *source_to_sensor_info(struct fimc_source_info *si)
{
	return container_of(si, struct fimc_sensor_info, pdata);
}

static inline struct fimc_md *entity_to_fimc_mdev(struct media_entity *me)
{
	return me->graph_obj.mdev == NULL ? NULL :
		container_of(me->graph_obj.mdev, struct fimc_md, media_dev);
}

static inline struct fimc_md *notifier_to_fimc_md(struct v4l2_async_notifier *n)
{
	return container_of(n, struct fimc_md, subdev_notifier);
}

static inline void fimc_md_graph_lock(struct exynos_video_entity *ve)
{
	mutex_lock(&ve->vdev.entity.graph_obj.mdev->graph_mutex);
}

static inline void fimc_md_graph_unlock(struct exynos_video_entity *ve)
{
	mutex_unlock(&ve->vdev.entity.graph_obj.mdev->graph_mutex);
}

int fimc_md_set_camclk(struct v4l2_subdev *sd, bool on);

#ifdef CONFIG_OF
static inline bool fimc_md_is_isp_available(struct device_node *node)
{
	node = of_get_child_by_name(node, FIMC_IS_OF_NODE_NAME);
	return node ? of_device_is_available(node) : false;
}
#else
#define fimc_md_is_isp_available(node) (false)
#endif /* CONFIG_OF */

static inline struct v4l2_subdev *__fimc_md_get_subdev(
				struct exynos_media_pipeline *ep,
				unsigned int index)
{
	struct fimc_pipeline *p = to_fimc_pipeline(ep);

	if (!p || index >= IDX_MAX)
		return NULL;
	else
		return p->subdevs[index];
}

#endif
