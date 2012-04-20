/*
 * S5P/EXYNOS4 SoC series camera host interface media device driver
 *
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 * Contact: Sylwester Nawrocki, <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 2 of the License,
 * or (at your option) any later version.
 */

#include <linux/bug.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <media/v4l2-ctrls.h>
#include <media/media-device.h>

#include "fimc-core.h"
#include "fimc-mdevice.h"
#include "mipi-csis.h"

static int __fimc_md_set_camclk(struct fimc_md *fmd,
				struct fimc_sensor_info *s_info,
				bool on);
/**
 * fimc_pipeline_prepare - update pipeline information with subdevice pointers
 * @fimc: fimc device terminating the pipeline
 *
 * Caller holds the graph mutex.
 */
void fimc_pipeline_prepare(struct fimc_dev *fimc, struct media_entity *me)
{
	struct media_entity_graph graph;
	struct v4l2_subdev *sd;

	media_entity_graph_walk_start(&graph, me);

	while ((me = media_entity_graph_walk_next(&graph))) {
		if (media_entity_type(me) != MEDIA_ENT_T_V4L2_SUBDEV)
			continue;
		sd = media_entity_to_v4l2_subdev(me);

		if (sd->grp_id == SENSOR_GROUP_ID)
			fimc->pipeline.sensor = sd;
		else if (sd->grp_id == CSIS_GROUP_ID)
			fimc->pipeline.csis = sd;
	}
}

/**
 * __subdev_set_power - change power state of a single subdev
 * @sd: subdevice to change power state for
 * @on: 1 to enable power or 0 to disable
 *
 * Return result of s_power subdev operation or -ENXIO if sd argument
 * is NULL. Return 0 if the subdevice does not implement s_power.
 */
static int __subdev_set_power(struct v4l2_subdev *sd, int on)
{
	int *use_count;
	int ret;

	if (sd == NULL)
		return -ENXIO;

	use_count = &sd->entity.use_count;
	if (on && (*use_count)++ > 0)
		return 0;
	else if (!on && (*use_count == 0 || --(*use_count) > 0))
		return 0;
	ret = v4l2_subdev_call(sd, core, s_power, on);

	return ret != -ENOIOCTLCMD ? ret : 0;
}

/**
 * fimc_pipeline_s_power - change power state of all pipeline subdevs
 * @fimc: fimc device terminating the pipeline
 * @state: 1 to enable power or 0 for power down
 *
 * Need to be called with the graph mutex held.
 */
int fimc_pipeline_s_power(struct fimc_dev *fimc, int state)
{
	int ret = 0;

	if (fimc->pipeline.sensor == NULL)
		return -ENXIO;

	if (state) {
		ret = __subdev_set_power(fimc->pipeline.csis, 1);
		if (ret && ret != -ENXIO)
			return ret;
		return __subdev_set_power(fimc->pipeline.sensor, 1);
	}

	ret = __subdev_set_power(fimc->pipeline.sensor, 0);
	if (ret)
		return ret;
	ret = __subdev_set_power(fimc->pipeline.csis, 0);

	return ret == -ENXIO ? 0 : ret;
}

/**
 * __fimc_pipeline_initialize - update the pipeline information, enable power
 *                              of all pipeline subdevs and the sensor clock
 * @me: media entity to start graph walk with
 * @prep: true to acquire sensor (and csis) subdevs
 *
 * This function must be called with the graph mutex held.
 */
static int __fimc_pipeline_initialize(struct fimc_dev *fimc,
				      struct media_entity *me, bool prep)
{
	int ret;

	if (prep)
		fimc_pipeline_prepare(fimc, me);
	if (fimc->pipeline.sensor == NULL)
		return -EINVAL;
	ret = fimc_md_set_camclk(fimc->pipeline.sensor, true);
	if (ret)
		return ret;
	return fimc_pipeline_s_power(fimc, 1);
}

int fimc_pipeline_initialize(struct fimc_dev *fimc, struct media_entity *me,
			     bool prep)
{
	int ret;

	mutex_lock(&me->parent->graph_mutex);
	ret =  __fimc_pipeline_initialize(fimc, me, prep);
	mutex_unlock(&me->parent->graph_mutex);

	return ret;
}

/**
 * __fimc_pipeline_shutdown - disable the sensor clock and pipeline power
 * @fimc: fimc device terminating the pipeline
 *
 * Disable power of all subdevs in the pipeline and turn off the external
 * sensor clock.
 * Called with the graph mutex held.
 */
int __fimc_pipeline_shutdown(struct fimc_dev *fimc)
{
	int ret = 0;

	if (fimc->pipeline.sensor) {
		ret = fimc_pipeline_s_power(fimc, 0);
		fimc_md_set_camclk(fimc->pipeline.sensor, false);
	}
	return ret == -ENXIO ? 0 : ret;
}

int fimc_pipeline_shutdown(struct fimc_dev *fimc)
{
	struct media_entity *me = &fimc->vid_cap.vfd->entity;
	int ret;

	mutex_lock(&me->parent->graph_mutex);
	ret = __fimc_pipeline_shutdown(fimc);
	mutex_unlock(&me->parent->graph_mutex);

	return ret;
}

/**
 * fimc_pipeline_s_stream - invoke s_stream on pipeline subdevs
 * @fimc: fimc device terminating the pipeline
 * @on: passed as the s_stream call argument
 */
int fimc_pipeline_s_stream(struct fimc_dev *fimc, int on)
{
	struct fimc_pipeline *p = &fimc->pipeline;
	int ret = 0;

	if (p->sensor == NULL)
		return -ENODEV;

	if ((on && p->csis) || !on)
		ret = v4l2_subdev_call(on ? p->csis : p->sensor,
				       video, s_stream, on);
	if (ret < 0 && ret != -ENOIOCTLCMD)
		return ret;
	if ((!on && p->csis) || on)
		ret = v4l2_subdev_call(on ? p->sensor : p->csis,
				       video, s_stream, on);
	return ret == -ENOIOCTLCMD ? 0 : ret;
}

/*
 * Sensor subdevice helper functions
 */
static struct v4l2_subdev *fimc_md_register_sensor(struct fimc_md *fmd,
				   struct fimc_sensor_info *s_info)
{
	struct i2c_adapter *adapter;
	struct v4l2_subdev *sd = NULL;

	if (!s_info || !fmd)
		return NULL;

	adapter = i2c_get_adapter(s_info->pdata->i2c_bus_num);
	if (!adapter) {
		v4l2_warn(&fmd->v4l2_dev,
			  "Failed to get I2C adapter %d, deferring probe\n",
			  s_info->pdata->i2c_bus_num);
		return ERR_PTR(-EPROBE_DEFER);
	}
	sd = v4l2_i2c_new_subdev_board(&fmd->v4l2_dev, adapter,
				       s_info->pdata->board_info, NULL);
	if (IS_ERR_OR_NULL(sd)) {
		i2c_put_adapter(adapter);
		v4l2_warn(&fmd->v4l2_dev,
			  "Failed to acquire subdev %s, deferring probe\n",
			  s_info->pdata->board_info->type);
		return ERR_PTR(-EPROBE_DEFER);
	}
	v4l2_set_subdev_hostdata(sd, s_info);
	sd->grp_id = SENSOR_GROUP_ID;

	v4l2_info(&fmd->v4l2_dev, "Registered sensor subdevice %s\n",
		  s_info->pdata->board_info->type);
	return sd;
}

static void fimc_md_unregister_sensor(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_adapter *adapter;

	if (!client)
		return;
	v4l2_device_unregister_subdev(sd);
	adapter = client->adapter;
	i2c_unregister_device(client);
	if (adapter)
		i2c_put_adapter(adapter);
}

static int fimc_md_register_sensor_entities(struct fimc_md *fmd)
{
	struct s5p_platform_fimc *pdata = fmd->pdev->dev.platform_data;
	struct fimc_dev *fd = NULL;
	int num_clients, ret, i;

	/*
	 * Runtime resume one of the FIMC entities to make sure
	 * the sclk_cam clocks are not globally disabled.
	 */
	for (i = 0; !fd && i < ARRAY_SIZE(fmd->fimc); i++)
		if (fmd->fimc[i])
			fd = fmd->fimc[i];
	if (!fd)
		return -ENXIO;
	ret = pm_runtime_get_sync(&fd->pdev->dev);
	if (ret < 0)
		return ret;

	WARN_ON(pdata->num_clients > ARRAY_SIZE(fmd->sensor));
	num_clients = min_t(u32, pdata->num_clients, ARRAY_SIZE(fmd->sensor));

	fmd->num_sensors = num_clients;
	for (i = 0; i < num_clients; i++) {
		struct v4l2_subdev *sd;

		fmd->sensor[i].pdata = &pdata->isp_info[i];
		ret = __fimc_md_set_camclk(fmd, &fmd->sensor[i], true);
		if (ret)
			break;
		sd = fimc_md_register_sensor(fmd, &fmd->sensor[i]);
		ret = __fimc_md_set_camclk(fmd, &fmd->sensor[i], false);

		if (!IS_ERR(sd)) {
			fmd->sensor[i].subdev = sd;
		} else {
			fmd->sensor[i].subdev = NULL;
			ret = PTR_ERR(sd);
			break;
		}
		if (ret)
			break;
	}
	pm_runtime_put(&fd->pdev->dev);
	return ret;
}

/*
 * MIPI CSIS and FIMC platform devices registration.
 */
static int fimc_register_callback(struct device *dev, void *p)
{
	struct fimc_dev *fimc = dev_get_drvdata(dev);
	struct v4l2_subdev *sd = &fimc->vid_cap.subdev;
	struct fimc_md *fmd = p;
	int ret = 0;

	if (!fimc || !fimc->pdev)
		return 0;
	if (fimc->pdev->id < 0 || fimc->pdev->id >= FIMC_MAX_DEVS)
		return 0;

	fmd->fimc[fimc->pdev->id] = fimc;
	sd->grp_id = FIMC_GROUP_ID;

	ret = v4l2_device_register_subdev(&fmd->v4l2_dev, sd);
	if (ret) {
		v4l2_err(&fmd->v4l2_dev, "Failed to register FIMC.%d (%d)\n",
			 fimc->id, ret);
	}

	return ret;
}

static int csis_register_callback(struct device *dev, void *p)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct platform_device *pdev;
	struct fimc_md *fmd = p;
	int id, ret;

	if (!sd)
		return 0;
	pdev = v4l2_get_subdevdata(sd);
	if (!pdev || pdev->id < 0 || pdev->id >= CSIS_MAX_ENTITIES)
		return 0;
	v4l2_info(sd, "csis%d sd: %s\n", pdev->id, sd->name);

	id = pdev->id < 0 ? 0 : pdev->id;
	fmd->csis[id].sd = sd;
	sd->grp_id = CSIS_GROUP_ID;
	ret = v4l2_device_register_subdev(&fmd->v4l2_dev, sd);
	if (ret)
		v4l2_err(&fmd->v4l2_dev,
			 "Failed to register CSIS subdevice: %d\n", ret);
	return ret;
}

/**
 * fimc_md_register_platform_entities - register FIMC and CSIS media entities
 */
static int fimc_md_register_platform_entities(struct fimc_md *fmd)
{
	struct s5p_platform_fimc *pdata = fmd->pdev->dev.platform_data;
	struct device_driver *driver;
	int ret, i;

	driver = driver_find(FIMC_MODULE_NAME, &platform_bus_type);
	if (!driver) {
		v4l2_warn(&fmd->v4l2_dev,
			 "%s driver not found, deffering probe\n",
			 FIMC_MODULE_NAME);
		return -EPROBE_DEFER;
	}

	ret = driver_for_each_device(driver, NULL, fmd,
				     fimc_register_callback);
	if (ret)
		return ret;
	/*
	 * Check if there is any sensor on the MIPI-CSI2 bus and
	 * if not skip the s5p-csis module loading.
	 */
	if (pdata == NULL)
		return 0;
	for (i = 0; i < pdata->num_clients; i++) {
		if (pdata->isp_info[i].bus_type == FIMC_MIPI_CSI2) {
			ret = 1;
			break;
		}
	}
	if (!ret)
		return 0;

	driver = driver_find(CSIS_DRIVER_NAME, &platform_bus_type);
	if (!driver || !try_module_get(driver->owner)) {
		v4l2_warn(&fmd->v4l2_dev,
			 "%s driver not found, deffering probe\n",
			 CSIS_DRIVER_NAME);
		return -EPROBE_DEFER;
	}

	return driver_for_each_device(driver, NULL, fmd,
				      csis_register_callback);
}

static void fimc_md_unregister_entities(struct fimc_md *fmd)
{
	int i;

	for (i = 0; i < FIMC_MAX_DEVS; i++) {
		if (fmd->fimc[i] == NULL)
			continue;
		v4l2_device_unregister_subdev(&fmd->fimc[i]->vid_cap.subdev);
		fmd->fimc[i] = NULL;
	}
	for (i = 0; i < CSIS_MAX_ENTITIES; i++) {
		if (fmd->csis[i].sd == NULL)
			continue;
		v4l2_device_unregister_subdev(fmd->csis[i].sd);
		module_put(fmd->csis[i].sd->owner);
		fmd->csis[i].sd = NULL;
	}
	for (i = 0; i < fmd->num_sensors; i++) {
		if (fmd->sensor[i].subdev == NULL)
			continue;
		fimc_md_unregister_sensor(fmd->sensor[i].subdev);
		fmd->sensor[i].subdev = NULL;
	}
}

/**
 * __fimc_md_create_fimc_links - create links to all FIMC entities
 * @fmd: fimc media device
 * @source: the source entity to create links to all fimc entities from
 * @sensor: sensor subdev linked to FIMC[fimc_id] entity, may be null
 * @pad: the source entity pad index
 * @fimc_id: index of the fimc device for which link should be enabled
 */
static int __fimc_md_create_fimc_links(struct fimc_md *fmd,
				       struct media_entity *source,
				       struct v4l2_subdev *sensor,
				       int pad, int fimc_id)
{
	struct fimc_sensor_info *s_info;
	struct media_entity *sink;
	unsigned int flags;
	int ret, i;

	for (i = 0; i < FIMC_MAX_DEVS; i++) {
		if (!fmd->fimc[i])
			break;
		/*
		 * Some FIMC variants are not fitted with camera capture
		 * interface. Skip creating a link from sensor for those.
		 */
		if (sensor->grp_id == SENSOR_GROUP_ID &&
		    !fmd->fimc[i]->variant->has_cam_if)
			continue;

		flags = (i == fimc_id) ? MEDIA_LNK_FL_ENABLED : 0;
		sink = &fmd->fimc[i]->vid_cap.subdev.entity;
		ret = media_entity_create_link(source, pad, sink,
					      FIMC_SD_PAD_SINK, flags);
		if (ret)
			return ret;

		/* Notify FIMC capture subdev entity */
		ret = media_entity_call(sink, link_setup, &sink->pads[0],
					&source->pads[pad], flags);
		if (ret)
			break;

		v4l2_info(&fmd->v4l2_dev, "created link [%s] %c> [%s]",
			  source->name, flags ? '=' : '-', sink->name);

		if (flags == 0)
			continue;
		s_info = v4l2_get_subdev_hostdata(sensor);
		if (!WARN_ON(s_info == NULL)) {
			unsigned long irq_flags;
			spin_lock_irqsave(&fmd->slock, irq_flags);
			s_info->host = fmd->fimc[i];
			spin_unlock_irqrestore(&fmd->slock, irq_flags);
		}
	}
	return 0;
}

/**
 * fimc_md_create_links - create default links between registered entities
 *
 * Parallel interface sensor entities are connected directly to FIMC capture
 * entities. The sensors using MIPI CSIS bus are connected through immutable
 * link with CSI receiver entity specified by mux_id. Any registered CSIS
 * entity has a link to each registered FIMC capture entity. Enabled links
 * are created by default between each subsequent registered sensor and
 * subsequent FIMC capture entity. The number of default active links is
 * determined by the number of available sensors or FIMC entities,
 * whichever is less.
 */
static int fimc_md_create_links(struct fimc_md *fmd)
{
	struct v4l2_subdev *sensor, *csis;
	struct s5p_fimc_isp_info *pdata;
	struct fimc_sensor_info *s_info;
	struct media_entity *source, *sink;
	int i, pad, fimc_id = 0;
	int ret = 0;
	u32 flags;

	for (i = 0; i < fmd->num_sensors; i++) {
		if (fmd->sensor[i].subdev == NULL)
			continue;

		sensor = fmd->sensor[i].subdev;
		s_info = v4l2_get_subdev_hostdata(sensor);
		if (!s_info || !s_info->pdata)
			continue;

		source = NULL;
		pdata = s_info->pdata;

		switch (pdata->bus_type) {
		case FIMC_MIPI_CSI2:
			if (WARN(pdata->mux_id >= CSIS_MAX_ENTITIES,
				"Wrong CSI channel id: %d\n", pdata->mux_id))
				return -EINVAL;

			csis = fmd->csis[pdata->mux_id].sd;
			if (WARN(csis == NULL,
				 "MIPI-CSI interface specified "
				 "but s5p-csis module is not loaded!\n"))
				return -EINVAL;

			ret = media_entity_create_link(&sensor->entity, 0,
					      &csis->entity, CSIS_PAD_SINK,
					      MEDIA_LNK_FL_IMMUTABLE |
					      MEDIA_LNK_FL_ENABLED);
			if (ret)
				return ret;

			v4l2_info(&fmd->v4l2_dev, "created link [%s] => [%s]",
				  sensor->entity.name, csis->entity.name);

			source = &csis->entity;
			pad = CSIS_PAD_SOURCE;
			break;

		case FIMC_ITU_601...FIMC_ITU_656:
			source = &sensor->entity;
			pad = 0;
			break;

		default:
			v4l2_err(&fmd->v4l2_dev, "Wrong bus_type: %x\n",
				 pdata->bus_type);
			return -EINVAL;
		}
		if (source == NULL)
			continue;

		ret = __fimc_md_create_fimc_links(fmd, source, sensor, pad,
						  fimc_id++);
	}
	/* Create immutable links between each FIMC's subdev and video node */
	flags = MEDIA_LNK_FL_IMMUTABLE | MEDIA_LNK_FL_ENABLED;
	for (i = 0; i < FIMC_MAX_DEVS; i++) {
		if (!fmd->fimc[i])
			continue;
		source = &fmd->fimc[i]->vid_cap.subdev.entity;
		sink = &fmd->fimc[i]->vid_cap.vfd->entity;
		ret = media_entity_create_link(source, FIMC_SD_PAD_SOURCE,
					      sink, 0, flags);
		if (ret)
			break;
	}

	return ret;
}

/*
 * The peripheral sensor clock management.
 */
static int fimc_md_get_clocks(struct fimc_md *fmd)
{
	char clk_name[32];
	struct clk *clock;
	int i;

	for (i = 0; i < FIMC_MAX_CAMCLKS; i++) {
		snprintf(clk_name, sizeof(clk_name), "sclk_cam%u", i);
		clock = clk_get(NULL, clk_name);
		if (IS_ERR_OR_NULL(clock)) {
			v4l2_err(&fmd->v4l2_dev, "Failed to get clock: %s",
				  clk_name);
			return -ENXIO;
		}
		fmd->camclk[i].clock = clock;
	}
	return 0;
}

static void fimc_md_put_clocks(struct fimc_md *fmd)
{
	int i = FIMC_MAX_CAMCLKS;

	while (--i >= 0) {
		if (IS_ERR_OR_NULL(fmd->camclk[i].clock))
			continue;
		clk_put(fmd->camclk[i].clock);
		fmd->camclk[i].clock = NULL;
	}
}

static int __fimc_md_set_camclk(struct fimc_md *fmd,
					 struct fimc_sensor_info *s_info,
					 bool on)
{
	struct s5p_fimc_isp_info *pdata = s_info->pdata;
	struct fimc_camclk_info *camclk;
	int ret = 0;

	if (WARN_ON(pdata->clk_id >= FIMC_MAX_CAMCLKS) || fmd == NULL)
		return -EINVAL;

	if (s_info->clk_on == on)
		return 0;
	camclk = &fmd->camclk[pdata->clk_id];

	dbg("camclk %d, f: %lu, clk: %p, on: %d",
	    pdata->clk_id, pdata->clk_frequency, camclk, on);

	if (on) {
		if (camclk->use_count > 0 &&
		    camclk->frequency != pdata->clk_frequency)
			return -EINVAL;

		if (camclk->use_count++ == 0) {
			clk_set_rate(camclk->clock, pdata->clk_frequency);
			camclk->frequency = pdata->clk_frequency;
			ret = clk_enable(camclk->clock);
		}
		s_info->clk_on = 1;
		dbg("Enabled camclk %d: f: %lu", pdata->clk_id,
		    clk_get_rate(camclk->clock));

		return ret;
	}

	if (WARN_ON(camclk->use_count == 0))
		return 0;

	if (--camclk->use_count == 0) {
		clk_disable(camclk->clock);
		s_info->clk_on = 0;
		dbg("Disabled camclk %d", pdata->clk_id);
	}
	return ret;
}

/**
 * fimc_md_set_camclk - peripheral sensor clock setup
 * @sd: sensor subdev to configure sclk_cam clock for
 * @on: 1 to enable or 0 to disable the clock
 *
 * There are 2 separate clock outputs available in the SoC for external
 * image processors. These clocks are shared between all registered FIMC
 * devices to which sensors can be attached, either directly or through
 * the MIPI CSI receiver. The clock is allowed here to be used by
 * multiple sensors concurrently if they use same frequency.
 * The per sensor subdev clk_on attribute helps to synchronize accesses
 * to the sclk_cam clocks from the video and media device nodes.
 * This function should only be called when the graph mutex is held.
 */
int fimc_md_set_camclk(struct v4l2_subdev *sd, bool on)
{
	struct fimc_sensor_info *s_info = v4l2_get_subdev_hostdata(sd);
	struct fimc_md *fmd = entity_to_fimc_mdev(&sd->entity);

	return __fimc_md_set_camclk(fmd, s_info, on);
}

static int fimc_md_link_notify(struct media_pad *source,
			       struct media_pad *sink, u32 flags)
{
	struct v4l2_subdev *sd;
	struct fimc_dev *fimc;
	int ret = 0;

	if (media_entity_type(sink->entity) != MEDIA_ENT_T_V4L2_SUBDEV)
		return 0;

	sd = media_entity_to_v4l2_subdev(sink->entity);
	fimc = v4l2_get_subdevdata(sd);

	if (!(flags & MEDIA_LNK_FL_ENABLED)) {
		ret = __fimc_pipeline_shutdown(fimc);
		fimc->pipeline.sensor = NULL;
		fimc->pipeline.csis = NULL;

		mutex_lock(&fimc->lock);
		fimc_ctrls_delete(fimc->vid_cap.ctx);
		mutex_unlock(&fimc->lock);
		return ret;
	}
	/*
	 * Link activation. Enable power of pipeline elements only if the
	 * pipeline is already in use, i.e. its video node is opened.
	 * Recreate the controls destroyed during the link deactivation.
	 */
	mutex_lock(&fimc->lock);
	if (fimc->vid_cap.refcnt > 0) {
		ret = __fimc_pipeline_initialize(fimc, source->entity, true);
		if (!ret)
			ret = fimc_capture_ctrls_create(fimc);
	}
	mutex_unlock(&fimc->lock);

	return ret ? -EPIPE : ret;
}

static ssize_t fimc_md_sysfs_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fimc_md *fmd = platform_get_drvdata(pdev);

	if (fmd->user_subdev_api)
		return strlcpy(buf, "Sub-device API (sub-dev)\n", PAGE_SIZE);

	return strlcpy(buf, "V4L2 video node only API (vid-dev)\n", PAGE_SIZE);
}

static ssize_t fimc_md_sysfs_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fimc_md *fmd = platform_get_drvdata(pdev);
	bool subdev_api;
	int i;

	if (!strcmp(buf, "vid-dev\n"))
		subdev_api = false;
	else if (!strcmp(buf, "sub-dev\n"))
		subdev_api = true;
	else
		return count;

	fmd->user_subdev_api = subdev_api;
	for (i = 0; i < FIMC_MAX_DEVS; i++)
		if (fmd->fimc[i])
			fmd->fimc[i]->vid_cap.user_subdev_api = subdev_api;
	return count;
}
/*
 * This device attribute is to select video pipeline configuration method.
 * There are following valid values:
 *  vid-dev - for V4L2 video node API only, subdevice will be configured
 *  by the host driver.
 *  sub-dev - for media controller API, subdevs must be configured in user
 *  space before starting streaming.
 */
static DEVICE_ATTR(subdev_conf_mode, S_IWUSR | S_IRUGO,
		   fimc_md_sysfs_show, fimc_md_sysfs_store);

static int fimc_md_probe(struct platform_device *pdev)
{
	struct v4l2_device *v4l2_dev;
	struct fimc_md *fmd;
	int ret;

	fmd = devm_kzalloc(&pdev->dev, sizeof(*fmd), GFP_KERNEL);
	if (!fmd)
		return -ENOMEM;

	spin_lock_init(&fmd->slock);
	fmd->pdev = pdev;

	strlcpy(fmd->media_dev.model, "SAMSUNG S5P FIMC",
		sizeof(fmd->media_dev.model));
	fmd->media_dev.link_notify = fimc_md_link_notify;
	fmd->media_dev.dev = &pdev->dev;

	v4l2_dev = &fmd->v4l2_dev;
	v4l2_dev->mdev = &fmd->media_dev;
	v4l2_dev->notify = fimc_sensor_notify;
	snprintf(v4l2_dev->name, sizeof(v4l2_dev->name), "%s",
		 dev_name(&pdev->dev));

	ret = v4l2_device_register(&pdev->dev, &fmd->v4l2_dev);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "Failed to register v4l2_device: %d\n", ret);
		return ret;
	}
	ret = media_device_register(&fmd->media_dev);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "Failed to register media device: %d\n", ret);
		goto err_md;
	}
	ret = fimc_md_get_clocks(fmd);
	if (ret)
		goto err_clk;

	fmd->user_subdev_api = false;

	/* Protect the media graph while we're registering entities */
	mutex_lock(&fmd->media_dev.graph_mutex);

	ret = fimc_md_register_platform_entities(fmd);
	if (ret)
		goto err_unlock;

	if (pdev->dev.platform_data) {
		ret = fimc_md_register_sensor_entities(fmd);
		if (ret)
			goto err_unlock;
	}
	ret = fimc_md_create_links(fmd);
	if (ret)
		goto err_unlock;
	ret = v4l2_device_register_subdev_nodes(&fmd->v4l2_dev);
	if (ret)
		goto err_unlock;

	ret = device_create_file(&pdev->dev, &dev_attr_subdev_conf_mode);
	if (ret)
		goto err_unlock;

	platform_set_drvdata(pdev, fmd);
	mutex_unlock(&fmd->media_dev.graph_mutex);
	return 0;

err_unlock:
	mutex_unlock(&fmd->media_dev.graph_mutex);
err_clk:
	media_device_unregister(&fmd->media_dev);
	fimc_md_put_clocks(fmd);
	fimc_md_unregister_entities(fmd);
err_md:
	v4l2_device_unregister(&fmd->v4l2_dev);
	return ret;
}

static int __devexit fimc_md_remove(struct platform_device *pdev)
{
	struct fimc_md *fmd = platform_get_drvdata(pdev);

	if (!fmd)
		return 0;
	device_remove_file(&pdev->dev, &dev_attr_subdev_conf_mode);
	fimc_md_unregister_entities(fmd);
	media_device_unregister(&fmd->media_dev);
	fimc_md_put_clocks(fmd);
	return 0;
}

static struct platform_driver fimc_md_driver = {
	.probe		= fimc_md_probe,
	.remove		= __devexit_p(fimc_md_remove),
	.driver = {
		.name	= "s5p-fimc-md",
		.owner	= THIS_MODULE,
	}
};

int __init fimc_md_init(void)
{
	int ret;

	request_module("s5p-csis");
	ret = fimc_register_driver();
	if (ret)
		return ret;

	return platform_driver_register(&fimc_md_driver);
}
void __exit fimc_md_exit(void)
{
	platform_driver_unregister(&fimc_md_driver);
	fimc_unregister_driver();
}

module_init(fimc_md_init);
module_exit(fimc_md_exit);

MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_DESCRIPTION("S5P FIMC camera host interface/video postprocessor driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("2.0.1");
