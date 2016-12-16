/*
 * S5P/EXYNOS4 SoC series camera host interface media device driver
 *
 * Copyright (C) 2011 - 2013 Samsung Electronics Co., Ltd.
 * Author: Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 2 of the License,
 * or (at your option) any later version.
 */

#include <linux/bug.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-of.h>
#include <media/media-device.h>
#include <media/drv-intf/exynos-fimc.h>

#include "media-dev.h"
#include "fimc-core.h"
#include "fimc-is.h"
#include "fimc-lite.h"
#include "mipi-csis.h"

/* Set up image sensor subdev -> FIMC capture node notifications. */
static void __setup_sensor_notification(struct fimc_md *fmd,
					struct v4l2_subdev *sensor,
					struct v4l2_subdev *fimc_sd)
{
	struct fimc_source_info *src_inf;
	struct fimc_sensor_info *md_si;
	unsigned long flags;

	src_inf = v4l2_get_subdev_hostdata(sensor);
	if (!src_inf || WARN_ON(fmd == NULL))
		return;

	md_si = source_to_sensor_info(src_inf);
	spin_lock_irqsave(&fmd->slock, flags);
	md_si->host = v4l2_get_subdevdata(fimc_sd);
	spin_unlock_irqrestore(&fmd->slock, flags);
}

/**
 * fimc_pipeline_prepare - update pipeline information with subdevice pointers
 * @me: media entity terminating the pipeline
 *
 * Caller holds the graph mutex.
 */
static void fimc_pipeline_prepare(struct fimc_pipeline *p,
					struct media_entity *me)
{
	struct fimc_md *fmd = entity_to_fimc_mdev(me);
	struct v4l2_subdev *sd;
	struct v4l2_subdev *sensor = NULL;
	int i;

	for (i = 0; i < IDX_MAX; i++)
		p->subdevs[i] = NULL;

	while (1) {
		struct media_pad *pad = NULL;

		/* Find remote source pad */
		for (i = 0; i < me->num_pads; i++) {
			struct media_pad *spad = &me->pads[i];
			if (!(spad->flags & MEDIA_PAD_FL_SINK))
				continue;
			pad = media_entity_remote_pad(spad);
			if (pad)
				break;
		}

		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			break;
		sd = media_entity_to_v4l2_subdev(pad->entity);

		switch (sd->grp_id) {
		case GRP_ID_SENSOR:
			sensor = sd;
			/* fall through */
		case GRP_ID_FIMC_IS_SENSOR:
			p->subdevs[IDX_SENSOR] = sd;
			break;
		case GRP_ID_CSIS:
			p->subdevs[IDX_CSIS] = sd;
			break;
		case GRP_ID_FLITE:
			p->subdevs[IDX_FLITE] = sd;
			break;
		case GRP_ID_FIMC:
			p->subdevs[IDX_FIMC] = sd;
			break;
		case GRP_ID_FIMC_IS:
			p->subdevs[IDX_IS_ISP] = sd;
			break;
		default:
			break;
		}
		me = &sd->entity;
		if (me->num_pads == 1)
			break;
	}

	if (sensor && p->subdevs[IDX_FIMC])
		__setup_sensor_notification(fmd, sensor, p->subdevs[IDX_FIMC]);
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
 * @state: true to power on, false to power off
 *
 * Needs to be called with the graph mutex held.
 */
static int fimc_pipeline_s_power(struct fimc_pipeline *p, bool on)
{
	static const u8 seq[2][IDX_MAX - 1] = {
		{ IDX_IS_ISP, IDX_SENSOR, IDX_CSIS, IDX_FLITE },
		{ IDX_CSIS, IDX_FLITE, IDX_SENSOR, IDX_IS_ISP },
	};
	int i, ret = 0;

	if (p->subdevs[IDX_SENSOR] == NULL)
		return -ENXIO;

	for (i = 0; i < IDX_MAX - 1; i++) {
		unsigned int idx = seq[on][i];

		ret = __subdev_set_power(p->subdevs[idx], on);


		if (ret < 0 && ret != -ENXIO)
			goto error;
	}
	return 0;
error:
	for (; i >= 0; i--) {
		unsigned int idx = seq[on][i];
		__subdev_set_power(p->subdevs[idx], !on);
	}
	return ret;
}

/**
 * __fimc_pipeline_enable - enable power of all pipeline subdevs
 *			    and the sensor clock
 * @ep: video pipeline structure
 * @fmd: fimc media device
 *
 * Called with the graph mutex held.
 */
static int __fimc_pipeline_enable(struct exynos_media_pipeline *ep,
				  struct fimc_md *fmd)
{
	struct fimc_pipeline *p = to_fimc_pipeline(ep);
	int ret;

	/* Enable PXLASYNC clock if this pipeline includes FIMC-IS */
	if (!IS_ERR(fmd->wbclk[CLK_IDX_WB_B]) && p->subdevs[IDX_IS_ISP]) {
		ret = clk_prepare_enable(fmd->wbclk[CLK_IDX_WB_B]);
		if (ret < 0)
			return ret;
	}

	ret = fimc_pipeline_s_power(p, 1);
	if (!ret)
		return 0;

	if (!IS_ERR(fmd->wbclk[CLK_IDX_WB_B]) && p->subdevs[IDX_IS_ISP])
		clk_disable_unprepare(fmd->wbclk[CLK_IDX_WB_B]);

	return ret;
}

/**
 * __fimc_pipeline_open - update the pipeline information, enable power
 *                        of all pipeline subdevs and the sensor clock
 * @me: media entity to start graph walk with
 * @prepare: true to walk the current pipeline and acquire all subdevs
 *
 * Called with the graph mutex held.
 */
static int __fimc_pipeline_open(struct exynos_media_pipeline *ep,
				struct media_entity *me, bool prepare)
{
	struct fimc_md *fmd = entity_to_fimc_mdev(me);
	struct fimc_pipeline *p = to_fimc_pipeline(ep);
	struct v4l2_subdev *sd;

	if (WARN_ON(p == NULL || me == NULL))
		return -EINVAL;

	if (prepare)
		fimc_pipeline_prepare(p, me);

	sd = p->subdevs[IDX_SENSOR];
	if (sd == NULL) {
		pr_warn("%s(): No sensor subdev\n", __func__);
		/*
		 * Pipeline open cannot fail so as to make it possible
		 * for the user space to configure the pipeline.
		 */
		return 0;
	}

	return __fimc_pipeline_enable(ep, fmd);
}

/**
 * __fimc_pipeline_close - disable the sensor clock and pipeline power
 * @fimc: fimc device terminating the pipeline
 *
 * Disable power of all subdevs and turn the external sensor clock off.
 */
static int __fimc_pipeline_close(struct exynos_media_pipeline *ep)
{
	struct fimc_pipeline *p = to_fimc_pipeline(ep);
	struct v4l2_subdev *sd = p ? p->subdevs[IDX_SENSOR] : NULL;
	struct fimc_md *fmd;
	int ret;

	if (sd == NULL) {
		pr_warn("%s(): No sensor subdev\n", __func__);
		return 0;
	}

	ret = fimc_pipeline_s_power(p, 0);

	fmd = entity_to_fimc_mdev(&sd->entity);

	/* Disable PXLASYNC clock if this pipeline includes FIMC-IS */
	if (!IS_ERR(fmd->wbclk[CLK_IDX_WB_B]) && p->subdevs[IDX_IS_ISP])
		clk_disable_unprepare(fmd->wbclk[CLK_IDX_WB_B]);

	return ret == -ENXIO ? 0 : ret;
}

/**
 * __fimc_pipeline_s_stream - call s_stream() on pipeline subdevs
 * @pipeline: video pipeline structure
 * @on: passed as the s_stream() callback argument
 */
static int __fimc_pipeline_s_stream(struct exynos_media_pipeline *ep, bool on)
{
	static const u8 seq[2][IDX_MAX] = {
		{ IDX_FIMC, IDX_SENSOR, IDX_IS_ISP, IDX_CSIS, IDX_FLITE },
		{ IDX_CSIS, IDX_FLITE, IDX_FIMC, IDX_SENSOR, IDX_IS_ISP },
	};
	struct fimc_pipeline *p = to_fimc_pipeline(ep);
	struct fimc_md *fmd = entity_to_fimc_mdev(&p->subdevs[IDX_CSIS]->entity);
	enum fimc_subdev_index sd_id;
	int i, ret = 0;

	if (p->subdevs[IDX_SENSOR] == NULL) {
		if (!fmd->user_subdev_api) {
			/*
			 * Sensor must be already discovered if we
			 * aren't in the user_subdev_api mode
			 */
			return -ENODEV;
		}

		/* Get pipeline sink entity */
		if (p->subdevs[IDX_FIMC])
			sd_id = IDX_FIMC;
		else if (p->subdevs[IDX_IS_ISP])
			sd_id = IDX_IS_ISP;
		else if (p->subdevs[IDX_FLITE])
			sd_id = IDX_FLITE;
		else
			return -ENODEV;

		/*
		 * Sensor could have been linked between open and STREAMON -
		 * check if this is the case.
		 */
		fimc_pipeline_prepare(p, &p->subdevs[sd_id]->entity);

		if (p->subdevs[IDX_SENSOR] == NULL)
			return -ENODEV;

		ret = __fimc_pipeline_enable(ep, fmd);
		if (ret < 0)
			return ret;

	}

	for (i = 0; i < IDX_MAX; i++) {
		unsigned int idx = seq[on][i];

		ret = v4l2_subdev_call(p->subdevs[idx], video, s_stream, on);

		if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
			goto error;
	}

	return 0;
error:
	fimc_pipeline_s_power(p, !on);
	for (; i >= 0; i--) {
		unsigned int idx = seq[on][i];
		v4l2_subdev_call(p->subdevs[idx], video, s_stream, !on);
	}
	return ret;
}

/* Media pipeline operations for the FIMC/FIMC-LITE video device driver */
static const struct exynos_media_pipeline_ops fimc_pipeline_ops = {
	.open		= __fimc_pipeline_open,
	.close		= __fimc_pipeline_close,
	.set_stream	= __fimc_pipeline_s_stream,
};

static struct exynos_media_pipeline *fimc_md_pipeline_create(
						struct fimc_md *fmd)
{
	struct fimc_pipeline *p;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return NULL;

	list_add_tail(&p->list, &fmd->pipelines);

	p->ep.ops = &fimc_pipeline_ops;
	return &p->ep;
}

static void fimc_md_pipelines_free(struct fimc_md *fmd)
{
	while (!list_empty(&fmd->pipelines)) {
		struct fimc_pipeline *p;

		p = list_entry(fmd->pipelines.next, typeof(*p), list);
		list_del(&p->list);
		kfree(p);
	}
}

/* Parse port node and register as a sub-device any sensor specified there. */
static int fimc_md_parse_port_node(struct fimc_md *fmd,
				   struct device_node *port,
				   unsigned int index)
{
	struct fimc_source_info *pd = &fmd->sensor[index].pdata;
	struct device_node *rem, *ep, *np;
	struct v4l2_of_endpoint endpoint;
	int ret;

	/* Assume here a port node can have only one endpoint node. */
	ep = of_get_next_child(port, NULL);
	if (!ep)
		return 0;

	ret = v4l2_of_parse_endpoint(ep, &endpoint);
	if (ret) {
		of_node_put(ep);
		return ret;
	}

	if (WARN_ON(endpoint.base.port == 0) || index >= FIMC_MAX_SENSORS)
		return -EINVAL;

	pd->mux_id = (endpoint.base.port - 1) & 0x1;

	rem = of_graph_get_remote_port_parent(ep);
	of_node_put(ep);
	if (rem == NULL) {
		v4l2_info(&fmd->v4l2_dev, "Remote device at %s not found\n",
							ep->full_name);
		return 0;
	}

	if (fimc_input_is_parallel(endpoint.base.port)) {
		if (endpoint.bus_type == V4L2_MBUS_PARALLEL)
			pd->sensor_bus_type = FIMC_BUS_TYPE_ITU_601;
		else
			pd->sensor_bus_type = FIMC_BUS_TYPE_ITU_656;
		pd->flags = endpoint.bus.parallel.flags;
	} else if (fimc_input_is_mipi_csi(endpoint.base.port)) {
		/*
		 * MIPI CSI-2: only input mux selection and
		 * the sensor's clock frequency is needed.
		 */
		pd->sensor_bus_type = FIMC_BUS_TYPE_MIPI_CSI2;
	} else {
		v4l2_err(&fmd->v4l2_dev, "Wrong port id (%u) at node %s\n",
			 endpoint.base.port, rem->full_name);
	}
	/*
	 * For FIMC-IS handled sensors, that are placed under i2c-isp device
	 * node, FIMC is connected to the FIMC-IS through its ISP Writeback
	 * input. Sensors are attached to the FIMC-LITE hostdata interface
	 * directly or through MIPI-CSIS, depending on the external media bus
	 * used. This needs to be handled in a more reliable way, not by just
	 * checking parent's node name.
	 */
	np = of_get_parent(rem);

	if (np && !of_node_cmp(np->name, "i2c-isp"))
		pd->fimc_bus_type = FIMC_BUS_TYPE_ISP_WRITEBACK;
	else
		pd->fimc_bus_type = pd->sensor_bus_type;

	if (WARN_ON(index >= ARRAY_SIZE(fmd->sensor))) {
		of_node_put(rem);
		return -EINVAL;
	}

	fmd->sensor[index].asd.match_type = V4L2_ASYNC_MATCH_OF;
	fmd->sensor[index].asd.match.of.node = rem;
	fmd->async_subdevs[index] = &fmd->sensor[index].asd;

	fmd->num_sensors++;

	of_node_put(rem);
	return 0;
}

/* Register all SoC external sub-devices */
static int fimc_md_register_sensor_entities(struct fimc_md *fmd)
{
	struct device_node *parent = fmd->pdev->dev.of_node;
	struct device_node *node, *ports;
	int index = 0;
	int ret;

	/*
	 * Runtime resume one of the FIMC entities to make sure
	 * the sclk_cam clocks are not globally disabled.
	 */
	if (!fmd->pmf)
		return -ENXIO;

	ret = pm_runtime_get_sync(fmd->pmf);
	if (ret < 0)
		return ret;

	fmd->num_sensors = 0;

	/* Attach sensors linked to MIPI CSI-2 receivers */
	for_each_available_child_of_node(parent, node) {
		struct device_node *port;

		if (of_node_cmp(node->name, "csis"))
			continue;
		/* The csis node can have only port subnode. */
		port = of_get_next_child(node, NULL);
		if (!port)
			continue;

		ret = fimc_md_parse_port_node(fmd, port, index);
		if (ret < 0) {
			of_node_put(node);
			goto rpm_put;
		}
		index++;
	}

	/* Attach sensors listed in the parallel-ports node */
	ports = of_get_child_by_name(parent, "parallel-ports");
	if (!ports)
		goto rpm_put;

	for_each_child_of_node(ports, node) {
		ret = fimc_md_parse_port_node(fmd, node, index);
		if (ret < 0) {
			of_node_put(node);
			break;
		}
		index++;
	}
rpm_put:
	pm_runtime_put(fmd->pmf);
	return ret;
}

static int __of_get_csis_id(struct device_node *np)
{
	u32 reg = 0;

	np = of_get_child_by_name(np, "port");
	if (!np)
		return -EINVAL;
	of_property_read_u32(np, "reg", &reg);
	return reg - FIMC_INPUT_MIPI_CSI2_0;
}

/*
 * MIPI-CSIS, FIMC and FIMC-LITE platform devices registration.
 */
static int register_fimc_lite_entity(struct fimc_md *fmd,
				     struct fimc_lite *fimc_lite)
{
	struct v4l2_subdev *sd;
	struct exynos_media_pipeline *ep;
	int ret;

	if (WARN_ON(fimc_lite->index >= FIMC_LITE_MAX_DEVS ||
		    fmd->fimc_lite[fimc_lite->index]))
		return -EBUSY;

	sd = &fimc_lite->subdev;
	sd->grp_id = GRP_ID_FLITE;

	ep = fimc_md_pipeline_create(fmd);
	if (!ep)
		return -ENOMEM;

	v4l2_set_subdev_hostdata(sd, ep);

	ret = v4l2_device_register_subdev(&fmd->v4l2_dev, sd);
	if (!ret)
		fmd->fimc_lite[fimc_lite->index] = fimc_lite;
	else
		v4l2_err(&fmd->v4l2_dev, "Failed to register FIMC.LITE%d\n",
			 fimc_lite->index);
	return ret;
}

static int register_fimc_entity(struct fimc_md *fmd, struct fimc_dev *fimc)
{
	struct v4l2_subdev *sd;
	struct exynos_media_pipeline *ep;
	int ret;

	if (WARN_ON(fimc->id >= FIMC_MAX_DEVS || fmd->fimc[fimc->id]))
		return -EBUSY;

	sd = &fimc->vid_cap.subdev;
	sd->grp_id = GRP_ID_FIMC;

	ep = fimc_md_pipeline_create(fmd);
	if (!ep)
		return -ENOMEM;

	v4l2_set_subdev_hostdata(sd, ep);

	ret = v4l2_device_register_subdev(&fmd->v4l2_dev, sd);
	if (!ret) {
		if (!fmd->pmf && fimc->pdev)
			fmd->pmf = &fimc->pdev->dev;
		fmd->fimc[fimc->id] = fimc;
		fimc->vid_cap.user_subdev_api = fmd->user_subdev_api;
	} else {
		v4l2_err(&fmd->v4l2_dev, "Failed to register FIMC.%d (%d)\n",
			 fimc->id, ret);
	}
	return ret;
}

static int register_csis_entity(struct fimc_md *fmd,
				struct platform_device *pdev,
				struct v4l2_subdev *sd)
{
	struct device_node *node = pdev->dev.of_node;
	int id, ret;

	id = node ? __of_get_csis_id(node) : max(0, pdev->id);

	if (WARN_ON(id < 0 || id >= CSIS_MAX_ENTITIES))
		return -ENOENT;

	if (WARN_ON(fmd->csis[id].sd))
		return -EBUSY;

	sd->grp_id = GRP_ID_CSIS;
	ret = v4l2_device_register_subdev(&fmd->v4l2_dev, sd);
	if (!ret)
		fmd->csis[id].sd = sd;
	else
		v4l2_err(&fmd->v4l2_dev,
			 "Failed to register MIPI-CSIS.%d (%d)\n", id, ret);
	return ret;
}

static int register_fimc_is_entity(struct fimc_md *fmd, struct fimc_is *is)
{
	struct v4l2_subdev *sd = &is->isp.subdev;
	struct exynos_media_pipeline *ep;
	int ret;

	/* Allocate pipeline object for the ISP capture video node. */
	ep = fimc_md_pipeline_create(fmd);
	if (!ep)
		return -ENOMEM;

	v4l2_set_subdev_hostdata(sd, ep);

	ret = v4l2_device_register_subdev(&fmd->v4l2_dev, sd);
	if (ret) {
		v4l2_err(&fmd->v4l2_dev,
			 "Failed to register FIMC-ISP (%d)\n", ret);
		return ret;
	}

	fmd->fimc_is = is;
	return 0;
}

static int fimc_md_register_platform_entity(struct fimc_md *fmd,
					    struct platform_device *pdev,
					    int plat_entity)
{
	struct device *dev = &pdev->dev;
	int ret = -EPROBE_DEFER;
	void *drvdata;

	/* Lock to ensure dev->driver won't change. */
	device_lock(dev);

	if (!dev->driver || !try_module_get(dev->driver->owner))
		goto dev_unlock;

	drvdata = dev_get_drvdata(dev);
	/* Some subdev didn't probe successfully id drvdata is NULL */
	if (drvdata) {
		switch (plat_entity) {
		case IDX_FIMC:
			ret = register_fimc_entity(fmd, drvdata);
			break;
		case IDX_FLITE:
			ret = register_fimc_lite_entity(fmd, drvdata);
			break;
		case IDX_CSIS:
			ret = register_csis_entity(fmd, pdev, drvdata);
			break;
		case IDX_IS_ISP:
			ret = register_fimc_is_entity(fmd, drvdata);
			break;
		default:
			ret = -ENODEV;
		}
	}

	module_put(dev->driver->owner);
dev_unlock:
	device_unlock(dev);
	if (ret == -EPROBE_DEFER)
		dev_info(&fmd->pdev->dev, "deferring %s device registration\n",
			dev_name(dev));
	else if (ret < 0)
		dev_err(&fmd->pdev->dev, "%s device registration failed (%d)\n",
			dev_name(dev), ret);
	return ret;
}

/* Register FIMC, FIMC-LITE and CSIS media entities */
static int fimc_md_register_platform_entities(struct fimc_md *fmd,
					      struct device_node *parent)
{
	struct device_node *node;
	int ret = 0;

	for_each_available_child_of_node(parent, node) {
		struct platform_device *pdev;
		int plat_entity = -1;

		pdev = of_find_device_by_node(node);
		if (!pdev)
			continue;

		/* If driver of any entity isn't ready try all again later. */
		if (!strcmp(node->name, CSIS_OF_NODE_NAME))
			plat_entity = IDX_CSIS;
		else if	(!strcmp(node->name, FIMC_IS_OF_NODE_NAME))
			plat_entity = IDX_IS_ISP;
		else if (!strcmp(node->name, FIMC_LITE_OF_NODE_NAME))
			plat_entity = IDX_FLITE;
		else if	(!strcmp(node->name, FIMC_OF_NODE_NAME) &&
			 !of_property_read_bool(node, "samsung,lcd-wb"))
			plat_entity = IDX_FIMC;

		if (plat_entity >= 0)
			ret = fimc_md_register_platform_entity(fmd, pdev,
							plat_entity);
		put_device(&pdev->dev);
		if (ret < 0) {
			of_node_put(node);
			break;
		}
	}

	return ret;
}

static void fimc_md_unregister_entities(struct fimc_md *fmd)
{
	int i;

	for (i = 0; i < FIMC_MAX_DEVS; i++) {
		struct fimc_dev *dev = fmd->fimc[i];
		if (dev == NULL)
			continue;
		v4l2_device_unregister_subdev(&dev->vid_cap.subdev);
		dev->vid_cap.ve.pipe = NULL;
		fmd->fimc[i] = NULL;
	}
	for (i = 0; i < FIMC_LITE_MAX_DEVS; i++) {
		struct fimc_lite *dev = fmd->fimc_lite[i];
		if (dev == NULL)
			continue;
		v4l2_device_unregister_subdev(&dev->subdev);
		dev->ve.pipe = NULL;
		fmd->fimc_lite[i] = NULL;
	}
	for (i = 0; i < CSIS_MAX_ENTITIES; i++) {
		if (fmd->csis[i].sd == NULL)
			continue;
		v4l2_device_unregister_subdev(fmd->csis[i].sd);
		fmd->csis[i].sd = NULL;
	}

	if (fmd->fimc_is)
		v4l2_device_unregister_subdev(&fmd->fimc_is->isp.subdev);

	v4l2_info(&fmd->v4l2_dev, "Unregistered all entities\n");
}

/**
 * __fimc_md_create_fimc_links - create links to all FIMC entities
 * @fmd: fimc media device
 * @source: the source entity to create links to all fimc entities from
 * @sensor: sensor subdev linked to FIMC[fimc_id] entity, may be null
 * @pad: the source entity pad index
 * @link_mask: bitmask of the fimc devices for which link should be enabled
 */
static int __fimc_md_create_fimc_sink_links(struct fimc_md *fmd,
					    struct media_entity *source,
					    struct v4l2_subdev *sensor,
					    int pad, int link_mask)
{
	struct fimc_source_info *si = NULL;
	struct media_entity *sink;
	unsigned int flags = 0;
	int i, ret = 0;

	if (sensor) {
		si = v4l2_get_subdev_hostdata(sensor);
		/* Skip direct FIMC links in the logical FIMC-IS sensor path */
		if (si && si->fimc_bus_type == FIMC_BUS_TYPE_ISP_WRITEBACK)
			ret = 1;
	}

	for (i = 0; !ret && i < FIMC_MAX_DEVS; i++) {
		if (!fmd->fimc[i])
			continue;
		/*
		 * Some FIMC variants are not fitted with camera capture
		 * interface. Skip creating a link from sensor for those.
		 */
		if (!fmd->fimc[i]->variant->has_cam_if)
			continue;

		flags = ((1 << i) & link_mask) ? MEDIA_LNK_FL_ENABLED : 0;

		sink = &fmd->fimc[i]->vid_cap.subdev.entity;
		ret = media_create_pad_link(source, pad, sink,
					      FIMC_SD_PAD_SINK_CAM, flags);
		if (ret)
			return ret;

		/* Notify FIMC capture subdev entity */
		ret = media_entity_call(sink, link_setup, &sink->pads[0],
					&source->pads[pad], flags);
		if (ret)
			break;

		v4l2_info(&fmd->v4l2_dev, "created link [%s] %c> [%s]\n",
			  source->name, flags ? '=' : '-', sink->name);
	}

	for (i = 0; i < FIMC_LITE_MAX_DEVS; i++) {
		if (!fmd->fimc_lite[i])
			continue;

		sink = &fmd->fimc_lite[i]->subdev.entity;
		ret = media_create_pad_link(source, pad, sink,
					       FLITE_SD_PAD_SINK, 0);
		if (ret)
			return ret;

		/* Notify FIMC-LITE subdev entity */
		ret = media_entity_call(sink, link_setup, &sink->pads[0],
					&source->pads[pad], 0);
		if (ret)
			break;

		v4l2_info(&fmd->v4l2_dev, "created link [%s] -> [%s]\n",
			  source->name, sink->name);
	}
	return 0;
}

/* Create links from FIMC-LITE source pads to other entities */
static int __fimc_md_create_flite_source_links(struct fimc_md *fmd)
{
	struct media_entity *source, *sink;
	int i, ret = 0;

	for (i = 0; i < FIMC_LITE_MAX_DEVS; i++) {
		struct fimc_lite *fimc = fmd->fimc_lite[i];

		if (fimc == NULL)
			continue;

		source = &fimc->subdev.entity;
		sink = &fimc->ve.vdev.entity;
		/* FIMC-LITE's subdev and video node */
		ret = media_create_pad_link(source, FLITE_SD_PAD_SOURCE_DMA,
					       sink, 0, 0);
		if (ret)
			break;
		/* Link from FIMC-LITE to IS-ISP subdev */
		sink = &fmd->fimc_is->isp.subdev.entity;
		ret = media_create_pad_link(source, FLITE_SD_PAD_SOURCE_ISP,
					       sink, 0, 0);
		if (ret)
			break;
	}

	return ret;
}

/* Create FIMC-IS links */
static int __fimc_md_create_fimc_is_links(struct fimc_md *fmd)
{
	struct fimc_isp *isp = &fmd->fimc_is->isp;
	struct media_entity *source, *sink;
	int i, ret;

	source = &isp->subdev.entity;

	for (i = 0; i < FIMC_MAX_DEVS; i++) {
		if (fmd->fimc[i] == NULL)
			continue;

		/* Link from FIMC-IS-ISP subdev to FIMC */
		sink = &fmd->fimc[i]->vid_cap.subdev.entity;
		ret = media_create_pad_link(source, FIMC_ISP_SD_PAD_SRC_FIFO,
					       sink, FIMC_SD_PAD_SINK_FIFO, 0);
		if (ret)
			return ret;
	}

	/* Link from FIMC-IS-ISP subdev to fimc-is-isp.capture video node */
	sink = &isp->video_capture.ve.vdev.entity;

	/* Skip this link if the fimc-is-isp video node driver isn't built-in */
	if (sink->num_pads == 0)
		return 0;

	return media_create_pad_link(source, FIMC_ISP_SD_PAD_SRC_DMA,
					sink, 0, 0);
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
	struct v4l2_subdev *csi_sensors[CSIS_MAX_ENTITIES] = { NULL };
	struct v4l2_subdev *sensor, *csis;
	struct fimc_source_info *pdata;
	struct media_entity *source, *sink;
	int i, pad, fimc_id = 0, ret = 0;
	u32 flags, link_mask = 0;

	for (i = 0; i < fmd->num_sensors; i++) {
		if (fmd->sensor[i].subdev == NULL)
			continue;

		sensor = fmd->sensor[i].subdev;
		pdata = v4l2_get_subdev_hostdata(sensor);
		if (!pdata)
			continue;

		source = NULL;

		switch (pdata->sensor_bus_type) {
		case FIMC_BUS_TYPE_MIPI_CSI2:
			if (WARN(pdata->mux_id >= CSIS_MAX_ENTITIES,
				"Wrong CSI channel id: %d\n", pdata->mux_id))
				return -EINVAL;

			csis = fmd->csis[pdata->mux_id].sd;
			if (WARN(csis == NULL,
				 "MIPI-CSI interface specified "
				 "but s5p-csis module is not loaded!\n"))
				return -EINVAL;

			pad = sensor->entity.num_pads - 1;
			ret = media_create_pad_link(&sensor->entity, pad,
					      &csis->entity, CSIS_PAD_SINK,
					      MEDIA_LNK_FL_IMMUTABLE |
					      MEDIA_LNK_FL_ENABLED);
			if (ret)
				return ret;

			v4l2_info(&fmd->v4l2_dev, "created link [%s] => [%s]\n",
				  sensor->entity.name, csis->entity.name);

			source = NULL;
			csi_sensors[pdata->mux_id] = sensor;
			break;

		case FIMC_BUS_TYPE_ITU_601...FIMC_BUS_TYPE_ITU_656:
			source = &sensor->entity;
			pad = 0;
			break;

		default:
			v4l2_err(&fmd->v4l2_dev, "Wrong bus_type: %x\n",
				 pdata->sensor_bus_type);
			return -EINVAL;
		}
		if (source == NULL)
			continue;

		link_mask = 1 << fimc_id++;
		ret = __fimc_md_create_fimc_sink_links(fmd, source, sensor,
						       pad, link_mask);
	}

	for (i = 0; i < CSIS_MAX_ENTITIES; i++) {
		if (fmd->csis[i].sd == NULL)
			continue;

		source = &fmd->csis[i].sd->entity;
		pad = CSIS_PAD_SOURCE;
		sensor = csi_sensors[i];

		link_mask = 1 << fimc_id++;
		ret = __fimc_md_create_fimc_sink_links(fmd, source, sensor,
						       pad, link_mask);
	}

	/* Create immutable links between each FIMC's subdev and video node */
	flags = MEDIA_LNK_FL_IMMUTABLE | MEDIA_LNK_FL_ENABLED;
	for (i = 0; i < FIMC_MAX_DEVS; i++) {
		if (!fmd->fimc[i])
			continue;

		source = &fmd->fimc[i]->vid_cap.subdev.entity;
		sink = &fmd->fimc[i]->vid_cap.ve.vdev.entity;

		ret = media_create_pad_link(source, FIMC_SD_PAD_SOURCE,
					      sink, 0, flags);
		if (ret)
			break;
	}

	ret = __fimc_md_create_flite_source_links(fmd);
	if (ret < 0)
		return ret;

	if (fmd->use_isp)
		ret = __fimc_md_create_fimc_is_links(fmd);

	return ret;
}

/*
 * The peripheral sensor and CAM_BLK (PIXELASYNCMx) clocks management.
 */
static void fimc_md_put_clocks(struct fimc_md *fmd)
{
	int i = FIMC_MAX_CAMCLKS;

	while (--i >= 0) {
		if (IS_ERR(fmd->camclk[i].clock))
			continue;
		clk_put(fmd->camclk[i].clock);
		fmd->camclk[i].clock = ERR_PTR(-EINVAL);
	}

	/* Writeback (PIXELASYNCMx) clocks */
	for (i = 0; i < FIMC_MAX_WBCLKS; i++) {
		if (IS_ERR(fmd->wbclk[i]))
			continue;
		clk_put(fmd->wbclk[i]);
		fmd->wbclk[i] = ERR_PTR(-EINVAL);
	}
}

static int fimc_md_get_clocks(struct fimc_md *fmd)
{
	struct device *dev = &fmd->pdev->dev;
	char clk_name[32];
	struct clk *clock;
	int i, ret = 0;

	for (i = 0; i < FIMC_MAX_CAMCLKS; i++)
		fmd->camclk[i].clock = ERR_PTR(-EINVAL);

	for (i = 0; i < FIMC_MAX_CAMCLKS; i++) {
		snprintf(clk_name, sizeof(clk_name), "sclk_cam%u", i);
		clock = clk_get(dev, clk_name);

		if (IS_ERR(clock)) {
			dev_err(dev, "Failed to get clock: %s\n", clk_name);
			ret = PTR_ERR(clock);
			break;
		}
		fmd->camclk[i].clock = clock;
	}
	if (ret)
		fimc_md_put_clocks(fmd);

	if (!fmd->use_isp)
		return 0;
	/*
	 * For now get only PIXELASYNCM1 clock (Writeback B/ISP),
	 * leave PIXELASYNCM0 out for the LCD Writeback driver.
	 */
	fmd->wbclk[CLK_IDX_WB_A] = ERR_PTR(-EINVAL);

	for (i = CLK_IDX_WB_B; i < FIMC_MAX_WBCLKS; i++) {
		snprintf(clk_name, sizeof(clk_name), "pxl_async%u", i);
		clock = clk_get(dev, clk_name);
		if (IS_ERR(clock)) {
			v4l2_err(&fmd->v4l2_dev, "Failed to get clock: %s\n",
				  clk_name);
			ret = PTR_ERR(clock);
			break;
		}
		fmd->wbclk[i] = clock;
	}
	if (ret)
		fimc_md_put_clocks(fmd);

	return ret;
}

static int __fimc_md_modify_pipeline(struct media_entity *entity, bool enable)
{
	struct exynos_video_entity *ve;
	struct fimc_pipeline *p;
	struct video_device *vdev;
	int ret;

	vdev = media_entity_to_video_device(entity);
	if (vdev->entity.use_count == 0)
		return 0;

	ve = vdev_to_exynos_video_entity(vdev);
	p = to_fimc_pipeline(ve->pipe);
	/*
	 * Nothing to do if we are disabling the pipeline, some link
	 * has been disconnected and p->subdevs array is cleared now.
	 */
	if (!enable && p->subdevs[IDX_SENSOR] == NULL)
		return 0;

	if (enable)
		ret = __fimc_pipeline_open(ve->pipe, entity, true);
	else
		ret = __fimc_pipeline_close(ve->pipe);

	if (ret == 0 && !enable)
		memset(p->subdevs, 0, sizeof(p->subdevs));

	return ret;
}

/* Locking: called with entity->graph_obj.mdev->graph_mutex mutex held. */
static int __fimc_md_modify_pipelines(struct media_entity *entity, bool enable,
				      struct media_entity_graph *graph)
{
	struct media_entity *entity_err = entity;
	int ret;

	/*
	 * Walk current graph and call the pipeline open/close routine for each
	 * opened video node that belongs to the graph of entities connected
	 * through active links. This is needed as we cannot power on/off the
	 * subdevs in random order.
	 */
	media_entity_graph_walk_start(graph, entity);

	while ((entity = media_entity_graph_walk_next(graph))) {
		if (!is_media_entity_v4l2_video_device(entity))
			continue;

		ret  = __fimc_md_modify_pipeline(entity, enable);

		if (ret < 0)
			goto err;
	}

	return 0;

err:
	media_entity_graph_walk_start(graph, entity_err);

	while ((entity_err = media_entity_graph_walk_next(graph))) {
		if (!is_media_entity_v4l2_video_device(entity_err))
			continue;

		__fimc_md_modify_pipeline(entity_err, !enable);

		if (entity_err == entity)
			break;
	}

	return ret;
}

static int fimc_md_link_notify(struct media_link *link, unsigned int flags,
				unsigned int notification)
{
	struct media_entity_graph *graph =
		&container_of(link->graph_obj.mdev, struct fimc_md,
			      media_dev)->link_setup_graph;
	struct media_entity *sink = link->sink->entity;
	int ret = 0;

	/* Before link disconnection */
	if (notification == MEDIA_DEV_NOTIFY_PRE_LINK_CH) {
		ret = media_entity_graph_walk_init(graph,
						   link->graph_obj.mdev);
		if (ret)
			return ret;
		if (!(flags & MEDIA_LNK_FL_ENABLED))
			ret = __fimc_md_modify_pipelines(sink, false, graph);
#if 0
		else
			/* TODO: Link state change validation */
#endif
	/* After link activation */
	} else if (notification == MEDIA_DEV_NOTIFY_POST_LINK_CH) {
		if (link->flags & MEDIA_LNK_FL_ENABLED)
			ret = __fimc_md_modify_pipelines(sink, true, graph);
		media_entity_graph_walk_cleanup(graph);
	}

	return ret ? -EPIPE : 0;
}

static const struct media_device_ops fimc_md_ops = {
	.link_notify = fimc_md_link_notify,
};

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

static int fimc_md_get_pinctrl(struct fimc_md *fmd)
{
	struct device *dev = &fmd->pdev->dev;
	struct fimc_pinctrl *pctl = &fmd->pinctl;

	pctl->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(pctl->pinctrl))
		return PTR_ERR(pctl->pinctrl);

	pctl->state_default = pinctrl_lookup_state(pctl->pinctrl,
					PINCTRL_STATE_DEFAULT);
	if (IS_ERR(pctl->state_default))
		return PTR_ERR(pctl->state_default);

	pctl->state_idle = pinctrl_lookup_state(pctl->pinctrl,
					PINCTRL_STATE_IDLE);
	return 0;
}

static int cam_clk_prepare(struct clk_hw *hw)
{
	struct cam_clk *camclk = to_cam_clk(hw);
	int ret;

	if (camclk->fmd->pmf == NULL)
		return -ENODEV;

	ret = pm_runtime_get_sync(camclk->fmd->pmf);
	return ret < 0 ? ret : 0;
}

static void cam_clk_unprepare(struct clk_hw *hw)
{
	struct cam_clk *camclk = to_cam_clk(hw);

	if (camclk->fmd->pmf == NULL)
		return;

	pm_runtime_put_sync(camclk->fmd->pmf);
}

static const struct clk_ops cam_clk_ops = {
	.prepare = cam_clk_prepare,
	.unprepare = cam_clk_unprepare,
};

static void fimc_md_unregister_clk_provider(struct fimc_md *fmd)
{
	struct cam_clk_provider *cp = &fmd->clk_provider;
	unsigned int i;

	if (cp->of_node)
		of_clk_del_provider(cp->of_node);

	for (i = 0; i < cp->num_clocks; i++)
		clk_unregister(cp->clks[i]);
}

static int fimc_md_register_clk_provider(struct fimc_md *fmd)
{
	struct cam_clk_provider *cp = &fmd->clk_provider;
	struct device *dev = &fmd->pdev->dev;
	int i, ret;

	for (i = 0; i < FIMC_MAX_CAMCLKS; i++) {
		struct cam_clk *camclk = &cp->camclk[i];
		struct clk_init_data init;
		const char *p_name;

		ret = of_property_read_string_index(dev->of_node,
					"clock-output-names", i, &init.name);
		if (ret < 0)
			break;

		p_name = __clk_get_name(fmd->camclk[i].clock);

		/* It's safe since clk_register() will duplicate the string. */
		init.parent_names = &p_name;
		init.num_parents = 1;
		init.ops = &cam_clk_ops;
		init.flags = CLK_SET_RATE_PARENT;
		camclk->hw.init = &init;
		camclk->fmd = fmd;

		cp->clks[i] = clk_register(NULL, &camclk->hw);
		if (IS_ERR(cp->clks[i])) {
			dev_err(dev, "failed to register clock: %s (%ld)\n",
					init.name, PTR_ERR(cp->clks[i]));
			ret = PTR_ERR(cp->clks[i]);
			goto err;
		}
		cp->num_clocks++;
	}

	if (cp->num_clocks == 0) {
		dev_warn(dev, "clk provider not registered\n");
		return 0;
	}

	cp->clk_data.clks = cp->clks;
	cp->clk_data.clk_num = cp->num_clocks;
	cp->of_node = dev->of_node;
	ret = of_clk_add_provider(dev->of_node, of_clk_src_onecell_get,
				  &cp->clk_data);
	if (ret == 0)
		return 0;
err:
	fimc_md_unregister_clk_provider(fmd);
	return ret;
}

static int subdev_notifier_bound(struct v4l2_async_notifier *notifier,
				 struct v4l2_subdev *subdev,
				 struct v4l2_async_subdev *asd)
{
	struct fimc_md *fmd = notifier_to_fimc_md(notifier);
	struct fimc_sensor_info *si = NULL;
	int i;

	/* Find platform data for this sensor subdev */
	for (i = 0; i < ARRAY_SIZE(fmd->sensor); i++)
		if (fmd->sensor[i].asd.match.of.node == subdev->dev->of_node)
			si = &fmd->sensor[i];

	if (si == NULL)
		return -EINVAL;

	v4l2_set_subdev_hostdata(subdev, &si->pdata);

	if (si->pdata.fimc_bus_type == FIMC_BUS_TYPE_ISP_WRITEBACK)
		subdev->grp_id = GRP_ID_FIMC_IS_SENSOR;
	else
		subdev->grp_id = GRP_ID_SENSOR;

	si->subdev = subdev;

	v4l2_info(&fmd->v4l2_dev, "Registered sensor subdevice: %s (%d)\n",
		  subdev->name, fmd->num_sensors);

	fmd->num_sensors++;

	return 0;
}

static int subdev_notifier_complete(struct v4l2_async_notifier *notifier)
{
	struct fimc_md *fmd = notifier_to_fimc_md(notifier);
	int ret;

	mutex_lock(&fmd->media_dev.graph_mutex);

	ret = fimc_md_create_links(fmd);
	if (ret < 0)
		goto unlock;

	ret = v4l2_device_register_subdev_nodes(&fmd->v4l2_dev);
unlock:
	mutex_unlock(&fmd->media_dev.graph_mutex);
	if (ret < 0)
		return ret;

	return media_device_register(&fmd->media_dev);
}

static int fimc_md_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct v4l2_device *v4l2_dev;
	struct fimc_md *fmd;
	int ret;

	fmd = devm_kzalloc(dev, sizeof(*fmd), GFP_KERNEL);
	if (!fmd)
		return -ENOMEM;

	spin_lock_init(&fmd->slock);
	INIT_LIST_HEAD(&fmd->pipelines);
	fmd->pdev = pdev;

	strlcpy(fmd->media_dev.model, "SAMSUNG S5P FIMC",
		sizeof(fmd->media_dev.model));
	fmd->media_dev.ops = &fimc_md_ops;
	fmd->media_dev.dev = dev;

	v4l2_dev = &fmd->v4l2_dev;
	v4l2_dev->mdev = &fmd->media_dev;
	v4l2_dev->notify = fimc_sensor_notify;
	strlcpy(v4l2_dev->name, "s5p-fimc-md", sizeof(v4l2_dev->name));

	fmd->use_isp = fimc_md_is_isp_available(dev->of_node);
	fmd->user_subdev_api = true;

	media_device_init(&fmd->media_dev);

	ret = v4l2_device_register(dev, &fmd->v4l2_dev);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "Failed to register v4l2_device: %d\n", ret);
		return ret;
	}

	ret = fimc_md_get_clocks(fmd);
	if (ret)
		goto err_md;

	ret = fimc_md_get_pinctrl(fmd);
	if (ret < 0) {
		if (ret != EPROBE_DEFER)
			dev_err(dev, "Failed to get pinctrl: %d\n", ret);
		goto err_clk;
	}

	platform_set_drvdata(pdev, fmd);

	ret = fimc_md_register_platform_entities(fmd, dev->of_node);
	if (ret)
		goto err_clk;

	ret = fimc_md_register_sensor_entities(fmd);
	if (ret)
		goto err_m_ent;

	ret = device_create_file(&pdev->dev, &dev_attr_subdev_conf_mode);
	if (ret)
		goto err_m_ent;
	/*
	 * FIMC platform devices need to be registered before the sclk_cam
	 * clocks provider, as one of these devices needs to be activated
	 * to enable the clock.
	 */
	ret = fimc_md_register_clk_provider(fmd);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "clock provider registration failed\n");
		goto err_attr;
	}

	if (fmd->num_sensors > 0) {
		fmd->subdev_notifier.subdevs = fmd->async_subdevs;
		fmd->subdev_notifier.num_subdevs = fmd->num_sensors;
		fmd->subdev_notifier.bound = subdev_notifier_bound;
		fmd->subdev_notifier.complete = subdev_notifier_complete;
		fmd->num_sensors = 0;

		ret = v4l2_async_notifier_register(&fmd->v4l2_dev,
						&fmd->subdev_notifier);
		if (ret)
			goto err_clk_p;
	}

	return 0;

err_clk_p:
	fimc_md_unregister_clk_provider(fmd);
err_attr:
	device_remove_file(&pdev->dev, &dev_attr_subdev_conf_mode);
err_clk:
	fimc_md_put_clocks(fmd);
err_m_ent:
	fimc_md_unregister_entities(fmd);
err_md:
	media_device_cleanup(&fmd->media_dev);
	v4l2_device_unregister(&fmd->v4l2_dev);
	return ret;
}

static int fimc_md_remove(struct platform_device *pdev)
{
	struct fimc_md *fmd = platform_get_drvdata(pdev);

	if (!fmd)
		return 0;

	fimc_md_unregister_clk_provider(fmd);
	v4l2_async_notifier_unregister(&fmd->subdev_notifier);

	v4l2_device_unregister(&fmd->v4l2_dev);
	device_remove_file(&pdev->dev, &dev_attr_subdev_conf_mode);
	fimc_md_unregister_entities(fmd);
	fimc_md_pipelines_free(fmd);
	media_device_unregister(&fmd->media_dev);
	media_device_cleanup(&fmd->media_dev);
	fimc_md_put_clocks(fmd);

	return 0;
}

static const struct platform_device_id fimc_driver_ids[] __always_unused = {
	{ .name = "s5p-fimc-md" },
	{ },
};
MODULE_DEVICE_TABLE(platform, fimc_driver_ids);

static const struct of_device_id fimc_md_of_match[] = {
	{ .compatible = "samsung,fimc" },
	{ },
};
MODULE_DEVICE_TABLE(of, fimc_md_of_match);

static struct platform_driver fimc_md_driver = {
	.probe		= fimc_md_probe,
	.remove		= fimc_md_remove,
	.driver = {
		.of_match_table = of_match_ptr(fimc_md_of_match),
		.name		= "s5p-fimc-md",
	}
};

static int __init fimc_md_init(void)
{
	int ret;

	request_module("s5p-csis");
	ret = fimc_register_driver();
	if (ret)
		return ret;

	return platform_driver_register(&fimc_md_driver);
}

static void __exit fimc_md_exit(void)
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
