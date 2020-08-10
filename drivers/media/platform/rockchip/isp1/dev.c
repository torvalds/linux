/*
 * Rockchip isp1 driver
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/pm_runtime.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <media/videobuf2-dma-contig.h>
#include <dt-bindings/soc/rockchip-system-status.h>
#include <soc/rockchip/rockchip-system-status.h>
#include "regs.h"
#include "rkisp1.h"
#include "common.h"
#include "version.h"

#define RKISP_VERNO_LEN		10

struct isp_irqs_data {
	const char *name;
	irqreturn_t (*irq_hdl)(int irq, void *ctx);
};

struct isp_match_data {
	const char * const *clks;
	int num_clks;
	enum rkisp1_isp_ver isp_ver;
	const unsigned int *clk_rate_tbl;
	int num_clk_rate_tbl;
	struct isp_irqs_data *irqs;
	int num_irqs;
};

int rkisp1_debug;
module_param_named(debug, rkisp1_debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

static char rkisp1_version[RKISP_VERNO_LEN];
module_param_string(version, rkisp1_version, RKISP_VERNO_LEN, 0444);
MODULE_PARM_DESC(version, "version number");

static DEFINE_MUTEX(rkisp1_dev_mutex);
static LIST_HEAD(rkisp1_device_list);

static int __maybe_unused __rkisp1_clr_unready_dev(void)
{
	struct rkisp1_device *isp_dev;

	mutex_lock(&rkisp1_dev_mutex);
	list_for_each_entry(isp_dev, &rkisp1_device_list, list)
		v4l2_async_notifier_clr_unready_dev(&isp_dev->notifier);
	mutex_unlock(&rkisp1_dev_mutex);

	return 0;
}

static int rkisp1_clr_unready_dev_param_set(const char *val, const struct kernel_param *kp)
{
#ifdef MODULE
	__rkisp1_clr_unready_dev();
#endif

	return 0;
}

module_param_call(clr_unready_dev, rkisp1_clr_unready_dev_param_set, NULL, NULL, 0200);
MODULE_PARM_DESC(clr_unready_dev, "clear unready devices");

/**************************** pipeline operations *****************************/

static int __isp_pipeline_prepare(struct rkisp1_pipeline *p,
				  struct media_entity *me)
{
	struct rkisp1_device *dev = container_of(p, struct rkisp1_device, pipe);
	struct v4l2_subdev *sd;
	int i;

	p->num_subdevs = 0;
	memset(p->subdevs, 0, sizeof(p->subdevs));

	if (dev->isp_inp == INP_DMARX_ISP)
		return 0;

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

		if (!pad)
			break;

		sd = media_entity_to_v4l2_subdev(pad->entity);
		if (sd != &dev->isp_sdev.sd)
			p->subdevs[p->num_subdevs++] = sd;

		me = &sd->entity;
		if (me->num_pads == 1)
			break;
	}

	if (!p->num_subdevs)
		return -EINVAL;

	return 0;
}

static int __isp_pipeline_s_isp_clk(struct rkisp1_pipeline *p)
{
	struct rkisp1_device *dev = container_of(p, struct rkisp1_device, pipe);
	struct v4l2_subdev *sd;
	struct v4l2_ctrl *ctrl;
	u64 data_rate;
	int i;

	if (dev->isp_inp == INP_DMARX_ISP) {
		clk_set_rate(dev->clks[0], 400 * 1000000UL);
		return 0;
	}

	/* find the subdev of active sensor */
	sd = p->subdevs[0];
	for (i = 0; i < p->num_subdevs; i++) {
		sd = p->subdevs[i];
		if (sd->entity.function == MEDIA_ENT_F_CAM_SENSOR)
			break;
	}

	if (i == p->num_subdevs) {
		v4l2_warn(sd, "No active sensor\n");
		return -EPIPE;
	}

	ctrl = v4l2_ctrl_find(sd->ctrl_handler, V4L2_CID_PIXEL_RATE);
	if (!ctrl) {
		v4l2_warn(sd, "No pixel rate control in subdev\n");
		return -EPIPE;
	}

	/* calculate data rate */
	data_rate = v4l2_ctrl_g_ctrl_int64(ctrl) *
		    dev->isp_sdev.in_fmt.bus_width;
	data_rate >>= 3;
	do_div(data_rate, 1000 * 1000);

	/* increase 25% margin */
	data_rate += data_rate >> 2;

	/* compare with isp clock adjustment table */
	for (i = 0; i < dev->num_clk_rate_tbl; i++)
		if (data_rate <= dev->clk_rate_tbl[i])
			break;
	if (i == dev->num_clk_rate_tbl)
		i--;

	/* set isp clock rate */
	clk_set_rate(dev->clks[0], dev->clk_rate_tbl[i] * 1000000UL);
	v4l2_dbg(1, rkisp1_debug, sd, "set isp clk = %luHz\n",
		 clk_get_rate(dev->clks[0]));

	return 0;
}

static int rkisp1_pipeline_open(struct rkisp1_pipeline *p,
				struct media_entity *me,
				bool prepare)
{
	int ret;

	if (WARN_ON(!p || !me))
		return -EINVAL;
	if (atomic_inc_return(&p->power_cnt) > 1)
		return 0;

	/* go through media graphic and get subdevs */
	if (prepare) {
		ret = __isp_pipeline_prepare(p, me);
		if (ret < 0)
			return ret;
	}

	ret = __isp_pipeline_s_isp_clk(p);
	if (ret < 0)
		return ret;

	return 0;
}

static int rkisp1_pipeline_close(struct rkisp1_pipeline *p)
{
	atomic_dec(&p->power_cnt);

	return 0;
}

/*
 * stream-on order: isp_subdev, mipi dphy, sensor
 * stream-off order: mipi dphy, sensor, isp_subdev
 */
static int rkisp1_pipeline_set_stream(struct rkisp1_pipeline *p, bool on)
{
	struct rkisp1_device *dev = container_of(p, struct rkisp1_device, pipe);
	int i, ret;

	if ((on && atomic_inc_return(&p->stream_cnt) > 1) ||
	    (!on && atomic_dec_return(&p->stream_cnt) > 0))
		return 0;

	if (on) {
		if (dev->vs_irq >= 0)
			enable_irq(dev->vs_irq);
		rockchip_set_system_status(SYS_STATUS_ISP);
		v4l2_subdev_call(&dev->isp_sdev.sd, video, s_stream, true);
	}

	/* phy -> sensor */
	for (i = 0; i < p->num_subdevs; ++i) {
		ret = v4l2_subdev_call(p->subdevs[i], video, s_stream, on);
		if (on && ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
			goto err_stream_off;
	}

	if (!on) {
		if (dev->vs_irq >= 0)
			disable_irq(dev->vs_irq);
		v4l2_subdev_call(&dev->isp_sdev.sd, video, s_stream, false);
		rockchip_clear_system_status(SYS_STATUS_ISP);
	}

	return 0;

err_stream_off:
	for (--i; i >= 0; --i)
		v4l2_subdev_call(p->subdevs[i], video, s_stream, false);
	v4l2_subdev_call(&dev->isp_sdev.sd, video, s_stream, false);
	rockchip_clear_system_status(SYS_STATUS_ISP);
	return ret;
}

/***************************** media controller *******************************/
/* See http://opensource.rock-chips.com/wiki_Rockchip-isp1 for Topology */

static int rkisp1_create_links(struct rkisp1_device *dev)
{
	struct media_entity *source, *sink;
	unsigned int flags, s, pad;
	int ret;

	/* sensor links(or mipi-phy) */
	for (s = 0; s < dev->num_sensors; ++s) {
		struct rkisp1_sensor_info *sensor = &dev->sensors[s];

		for (pad = 0; pad < sensor->sd->entity.num_pads; pad++)
			if (sensor->sd->entity.pads[pad].flags &
				MEDIA_PAD_FL_SOURCE)
				break;

		if (pad == sensor->sd->entity.num_pads) {
			dev_err(dev->dev,
				"failed to find src pad for %s\n",
				sensor->sd->name);

			return -ENXIO;
		}

		ret = media_create_pad_link(
				&sensor->sd->entity, pad,
				&dev->isp_sdev.sd.entity,
				RKISP1_ISP_PAD_SINK,
				s ? 0 : MEDIA_LNK_FL_ENABLED);
		if (ret) {
			dev_err(dev->dev,
				"failed to create link for %s\n",
				sensor->sd->name);
			return ret;
		}
	}

	/* params links */
	source = &dev->params_vdev.vnode.vdev.entity;
	sink = &dev->isp_sdev.sd.entity;
	flags = MEDIA_LNK_FL_ENABLED;
	ret = media_create_pad_link(source, 0, sink,
				       RKISP1_ISP_PAD_SINK_PARAMS, flags);
	if (ret < 0)
		return ret;

	/* create isp internal links */
	if (dev->isp_ver != ISP_V10_1) {
		/* SP links */
		source = &dev->isp_sdev.sd.entity;
		sink = &dev->stream[RKISP1_STREAM_SP].vnode.vdev.entity;
		ret = media_create_pad_link(source,
					    RKISP1_ISP_PAD_SOURCE_PATH,
					    sink, 0, flags);
		if (ret < 0)
			return ret;
	}

	/* MP links */
	source = &dev->isp_sdev.sd.entity;
	sink = &dev->stream[RKISP1_STREAM_MP].vnode.vdev.entity;
	ret = media_create_pad_link(source, RKISP1_ISP_PAD_SOURCE_PATH,
				       sink, 0, flags);
	if (ret < 0)
		return ret;

#if RKISP1_RK3326_USE_OLDMIPI
	if (dev->isp_ver == ISP_V13) {
#else
	if (dev->isp_ver == ISP_V12 ||
		dev->isp_ver == ISP_V13) {
#endif
		/* MIPI RAW links */
		source = &dev->isp_sdev.sd.entity;
		sink = &dev->stream[RKISP1_STREAM_RAW].vnode.vdev.entity;
		ret = media_create_pad_link(source,
			RKISP1_ISP_PAD_SOURCE_PATH, sink, 0, flags);
		if (ret < 0)
			return ret;
	}

	/* 3A stats links */
	source = &dev->isp_sdev.sd.entity;
	sink = &dev->stats_vdev.vnode.vdev.entity;
	return media_create_pad_link(source, RKISP1_ISP_PAD_SOURCE_STATS,
					sink, 0, flags);
}

static int _set_pipeline_default_fmt(struct rkisp1_device *dev)
{
	struct v4l2_subdev *isp;
	struct v4l2_subdev_format fmt;
	struct v4l2_subdev_selection sel;
	struct v4l2_subdev_pad_config cfg;
	u32 width, height;
	u32 ori_width, ori_height, ori_code;

	isp = &dev->isp_sdev.sd;

	fmt = dev->active_sensor->fmt;
	ori_width = fmt.format.width;
	ori_height = fmt.format.height;
	ori_code = fmt.format.code;

	if (dev->isp_ver == ISP_V12) {
		fmt.format.width  = clamp_t(u32, fmt.format.width,
					CIF_ISP_INPUT_W_MIN,
					CIF_ISP_INPUT_W_MAX_V12);
		fmt.format.height = clamp_t(u32, fmt.format.height,
					CIF_ISP_INPUT_H_MIN,
					CIF_ISP_INPUT_H_MAX_V12);
	} else if (dev->isp_ver == ISP_V13) {
		fmt.format.width  = clamp_t(u32, fmt.format.width,
					CIF_ISP_INPUT_W_MIN,
					CIF_ISP_INPUT_W_MAX_V13);
		fmt.format.height = clamp_t(u32, fmt.format.height,
					CIF_ISP_INPUT_H_MIN,
					CIF_ISP_INPUT_H_MAX_V13);
	} else {
		fmt.format.width  = clamp_t(u32, fmt.format.width,
					CIF_ISP_INPUT_W_MIN,
					CIF_ISP_INPUT_W_MAX);
		fmt.format.height = clamp_t(u32, fmt.format.height,
					CIF_ISP_INPUT_H_MIN,
					CIF_ISP_INPUT_H_MAX);
	}

	sel.r.left = 0;
	sel.r.top = 0;
	width = fmt.format.width;
	height = fmt.format.height;
	sel.r.width = fmt.format.width;
	sel.r.height = fmt.format.height;
	sel.target = V4L2_SEL_TGT_CROP;
	sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	memset(&cfg, 0, sizeof(cfg));

	/* change fmt&size for RKISP1_ISP_PAD_SINK */
	fmt.pad = RKISP1_ISP_PAD_SINK;
	sel.pad = RKISP1_ISP_PAD_SINK;
	v4l2_subdev_call(isp, pad, set_fmt, &cfg, &fmt);
	v4l2_subdev_call(isp, pad, set_selection, &cfg, &sel);

	/* change fmt&size for RKISP1_ISP_PAD_SOURCE_PATH */
	if ((fmt.format.code & RKISP1_MEDIA_BUS_FMT_MASK) ==
	    RKISP1_MEDIA_BUS_FMT_BAYER)
		fmt.format.code = MEDIA_BUS_FMT_YUYV8_2X8;

	fmt.pad = RKISP1_ISP_PAD_SOURCE_PATH;
	sel.pad = RKISP1_ISP_PAD_SOURCE_PATH;
	v4l2_subdev_call(isp, pad, set_fmt, &cfg, &fmt);
	v4l2_subdev_call(isp, pad, set_selection, &cfg, &sel);

	/* change fmt&size of MP/SP */
	rkisp1_set_stream_def_fmt(dev, RKISP1_STREAM_MP,
				  width, height, V4L2_PIX_FMT_YUYV);
	if (dev->isp_ver != ISP_V10_1)
		rkisp1_set_stream_def_fmt(dev, RKISP1_STREAM_SP,
					  width, height, V4L2_PIX_FMT_YUYV);
	if (dev->isp_ver == ISP_V12 || dev->isp_ver == ISP_V13)
		rkisp1_set_stream_def_fmt(dev, RKISP1_STREAM_RAW, ori_width,
			ori_height, rkisp1_mbus_pixelcode_to_v4l2(ori_code));

	return 0;
}

static int subdev_notifier_complete(struct v4l2_async_notifier *notifier)
{
	struct rkisp1_device *dev;
	int ret;

	dev = container_of(notifier, struct rkisp1_device, notifier);

	mutex_lock(&dev->media_dev.graph_mutex);
	ret = rkisp1_create_links(dev);
	if (ret < 0)
		goto unlock;
	ret = v4l2_device_register_subdev_nodes(&dev->v4l2_dev);
	if (ret < 0)
		goto unlock;

	ret = rkisp1_update_sensor_info(dev);
	if (ret < 0) {
		v4l2_err(&dev->v4l2_dev, "update sensor failed\n");
		goto unlock;
	}

	ret = _set_pipeline_default_fmt(dev);
	if (ret < 0)
		goto unlock;

	v4l2_info(&dev->v4l2_dev, "Async subdev notifier completed\n");

unlock:
	mutex_unlock(&dev->media_dev.graph_mutex);
	return ret;
}

struct rkisp1_async_subdev {
	struct v4l2_async_subdev asd;
	struct v4l2_mbus_config mbus;
};

static int subdev_notifier_bound(struct v4l2_async_notifier *notifier,
				 struct v4l2_subdev *subdev,
				 struct v4l2_async_subdev *asd)
{
	struct rkisp1_device *isp_dev = container_of(notifier,
					struct rkisp1_device, notifier);
	struct rkisp1_async_subdev *s_asd = container_of(asd,
					struct rkisp1_async_subdev, asd);

	if (isp_dev->num_sensors == ARRAY_SIZE(isp_dev->sensors))
		return -EBUSY;

	isp_dev->sensors[isp_dev->num_sensors].mbus = s_asd->mbus;
	isp_dev->sensors[isp_dev->num_sensors].sd = subdev;
	++isp_dev->num_sensors;

	v4l2_dbg(1, rkisp1_debug, subdev, "Async registered subdev\n");

	return 0;
}

static int rkisp1_fwnode_parse(struct device *dev,
			       struct v4l2_fwnode_endpoint *vep,
			       struct v4l2_async_subdev *asd)
{
	struct rkisp1_async_subdev *rk_asd =
			container_of(asd, struct rkisp1_async_subdev, asd);
	struct v4l2_fwnode_bus_parallel *bus = &vep->bus.parallel;

	/*
	 * MIPI sensor is linked with a mipi dphy and its media bus config can
	 * not be get in here
	 */
	if (vep->bus_type != V4L2_MBUS_BT656 &&
	    vep->bus_type != V4L2_MBUS_PARALLEL)
		return 0;

	rk_asd->mbus.flags = bus->flags;
	rk_asd->mbus.type = vep->bus_type;

	return 0;
}

static const struct v4l2_async_notifier_operations subdev_notifier_ops = {
	.bound = subdev_notifier_bound,
	.complete = subdev_notifier_complete,
};

static int isp_subdev_notifier(struct rkisp1_device *isp_dev)
{
	struct v4l2_async_notifier *ntf = &isp_dev->notifier;
	struct device *dev = isp_dev->dev;
	int ret;

	ret = v4l2_async_notifier_parse_fwnode_endpoints(
		dev, ntf, sizeof(struct rkisp1_async_subdev),
		rkisp1_fwnode_parse);
	if (ret < 0)
		return ret;

	if (!ntf->num_subdevs)
		return -ENODEV;	/* no endpoint */

	ntf->ops = &subdev_notifier_ops;

	return v4l2_async_notifier_register(&isp_dev->v4l2_dev, ntf);
}

/***************************** platform deive *******************************/

static int rkisp1_register_platform_subdevs(struct rkisp1_device *dev)
{
	int ret;

	ret = rkisp1_register_isp_subdev(dev, &dev->v4l2_dev);
	if (ret < 0)
		return ret;

	ret = rkisp1_register_stream_vdevs(dev);
	if (ret < 0)
		goto err_unreg_isp_subdev;

	ret = rkisp1_register_dmarx_vdev(dev);
	if (ret < 0)
		goto err_unreg_stream_vdev;

	ret = rkisp1_register_stats_vdev(&dev->stats_vdev, &dev->v4l2_dev, dev);
	if (ret < 0)
		goto err_unreg_dmarx_vdev;

	ret = rkisp1_register_params_vdev(&dev->params_vdev, &dev->v4l2_dev,
					  dev);
	if (ret < 0)
		goto err_unreg_stats_vdev;

	ret = isp_subdev_notifier(dev);
	if (ret < 0) {
		v4l2_err(&dev->v4l2_dev,
			 "Failed to register subdev notifier(%d)\n", ret);
		goto err_unreg_params_vdev;
	}

	return 0;
err_unreg_params_vdev:
	rkisp1_unregister_params_vdev(&dev->params_vdev);
err_unreg_stats_vdev:
	rkisp1_unregister_stats_vdev(&dev->stats_vdev);
err_unreg_dmarx_vdev:
	rkisp1_unregister_dmarx_vdev(dev);
err_unreg_stream_vdev:
	rkisp1_unregister_stream_vdevs(dev);
err_unreg_isp_subdev:
	rkisp1_unregister_isp_subdev(dev);

	return ret;
}

static irqreturn_t rkisp1_irq_handler(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp1_device *rkisp1_dev = dev_get_drvdata(dev);
	unsigned int mis_val;

	mis_val = readl(rkisp1_dev->base_addr + CIF_ISP_MIS);
	if (mis_val)
		rkisp1_isp_isr(mis_val, rkisp1_dev);

	mis_val = readl(rkisp1_dev->base_addr + CIF_MIPI_MIS);
	if (mis_val)
		rkisp1_mipi_isr(mis_val, rkisp1_dev);

	mis_val = readl(rkisp1_dev->base_addr + CIF_MI_MIS);
	if (mis_val)
		rkisp1_mi_isr(mis_val, rkisp1_dev);

	return IRQ_HANDLED;
}

static irqreturn_t rkisp1_isp_irq_hdl(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp1_device *rkisp1_dev = dev_get_drvdata(dev);
	unsigned int mis_val;

	mis_val = readl(rkisp1_dev->base_addr + CIF_ISP_MIS);
	if (mis_val)
		rkisp1_isp_isr(mis_val, rkisp1_dev);

	return IRQ_HANDLED;
}

static irqreturn_t rkisp1_mi_irq_hdl(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp1_device *rkisp1_dev = dev_get_drvdata(dev);
	unsigned int mis_val;

	mis_val = readl(rkisp1_dev->base_addr + CIF_MI_MIS);
	if (mis_val)
		rkisp1_mi_isr(mis_val, rkisp1_dev);

	return IRQ_HANDLED;
}

static irqreturn_t rkisp1_mipi_irq_hdl(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp1_device *rkisp1_dev = dev_get_drvdata(dev);
	unsigned int mis_val;
	unsigned int err1, err2, err3;

#if RKISP1_RK3326_USE_OLDMIPI
	if (rkisp1_dev->isp_ver == ISP_V13) {
#else
	if (rkisp1_dev->isp_ver == ISP_V13 ||
		rkisp1_dev->isp_ver == ISP_V12) {
#endif
		err1 = readl(rkisp1_dev->base_addr + CIF_ISP_CSI0_ERR1);
		err2 = readl(rkisp1_dev->base_addr + CIF_ISP_CSI0_ERR2);
		err3 = readl(rkisp1_dev->base_addr + CIF_ISP_CSI0_ERR3);

		if (err3 & 0x1)
			rkisp1_mipi_dmatx0_end(err3, rkisp1_dev);
		if (err1 || err2 || err3)
			rkisp1_mipi_v13_isr(err1, err2, err3, rkisp1_dev);
	} else {
		mis_val = readl(rkisp1_dev->base_addr + CIF_MIPI_MIS);
		if (mis_val)
			rkisp1_mipi_isr(mis_val, rkisp1_dev);

		/*
		 * As default interrupt mask for csi_rx are on,
		 * when resetting isp, interrupt from csi_rx maybe arise,
		 * we should clear them.
		 */
#if RKISP1_RK3326_USE_OLDMIPI
		if (rkisp1_dev->isp_ver == ISP_V12) {
			/* read error state register to clear interrupt state */
			readl(rkisp1_dev->base_addr + CIF_ISP_CSI0_ERR1);
			readl(rkisp1_dev->base_addr + CIF_ISP_CSI0_ERR2);
			readl(rkisp1_dev->base_addr + CIF_ISP_CSI0_ERR3);
		}
#endif
	}

	return IRQ_HANDLED;
}

static const char * const rk1808_isp_clks[] = {
	"clk_isp",
	"aclk_isp",
	"hclk_isp",
	"pclk_isp",
};

static const char * const rk3288_isp_clks[] = {
	"clk_isp",
	"aclk_isp",
	"hclk_isp",
	"pclk_isp_in",
	"sclk_isp_jpe",
};

static const char * const rk3326_isp_clks[] = {
	"clk_isp",
	"aclk_isp",
	"hclk_isp",
	"pclk_isp",
};

static const char * const rk3368_isp_clks[] = {
	"clk_isp",
	"aclk_isp",
	"hclk_isp",
	"pclk_isp",
};

static const char * const rk3399_isp_clks[] = {
	"clk_isp",
	"aclk_isp",
	"hclk_isp",
	"aclk_isp_wrap",
	"hclk_isp_wrap",
	"pclk_isp_wrap"
};

/* isp clock adjustment table (MHz) */
static const unsigned int rk1808_isp_clk_rate[] = {
	300, 400, 500, 600
};

/* isp clock adjustment table (MHz) */
static const unsigned int rk3288_isp_clk_rate[] = {
	150, 384, 500, 594
};

/* isp clock adjustment table (MHz) */
static const unsigned int rk3326_isp_clk_rate[] = {
	300, 347, 400, 520, 600
};

/* isp clock adjustment table (MHz) */
static const unsigned int rk3368_isp_clk_rate[] = {
	300, 400, 600
};

/* isp clock adjustment table (MHz) */
static const unsigned int rk3399_isp_clk_rate[] = {
	300, 400, 600
};

static struct isp_irqs_data rk1808_isp_irqs[] = {
	{"isp_irq", rkisp1_isp_irq_hdl},
	{"mi_irq", rkisp1_mi_irq_hdl},
	{"mipi_irq", rkisp1_mipi_irq_hdl}
};

static struct isp_irqs_data rk3288_isp_irqs[] = {
	{"isp_irq", rkisp1_irq_handler}
};

static struct isp_irqs_data rk3326_isp_irqs[] = {
	{"isp_irq", rkisp1_isp_irq_hdl},
	{"mi_irq", rkisp1_mi_irq_hdl},
	{"mipi_irq", rkisp1_mipi_irq_hdl}
};

static struct isp_irqs_data rk3368_isp_irqs[] = {
	{"isp_irq", rkisp1_irq_handler}
};

static struct isp_irqs_data rk3399_isp_irqs[] = {
	{"isp_irq", rkisp1_irq_handler}
};

static const struct isp_match_data rk1808_isp_match_data = {
	.clks = rk1808_isp_clks,
	.num_clks = ARRAY_SIZE(rk1808_isp_clks),
	.isp_ver = ISP_V13,
	.clk_rate_tbl = rk1808_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rk1808_isp_clk_rate),
	.irqs = rk1808_isp_irqs,
	.num_irqs = ARRAY_SIZE(rk1808_isp_irqs)
};

static const struct isp_match_data rk3288_isp_match_data = {
	.clks = rk3288_isp_clks,
	.num_clks = ARRAY_SIZE(rk3288_isp_clks),
	.isp_ver = ISP_V10,
	.clk_rate_tbl = rk3288_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rk3288_isp_clk_rate),
	.irqs = rk3288_isp_irqs,
	.num_irqs = ARRAY_SIZE(rk3288_isp_irqs)
};

static const struct isp_match_data rk3326_isp_match_data = {
	.clks = rk3326_isp_clks,
	.num_clks = ARRAY_SIZE(rk3326_isp_clks),
	.isp_ver = ISP_V12,
	.clk_rate_tbl = rk3326_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rk3326_isp_clk_rate),
	.irqs = rk3326_isp_irqs,
	.num_irqs = ARRAY_SIZE(rk3326_isp_irqs)
};

static const struct isp_match_data rk3368_isp_match_data = {
	.clks = rk3368_isp_clks,
	.num_clks = ARRAY_SIZE(rk3368_isp_clks),
	.isp_ver = ISP_V10_1,
	.clk_rate_tbl = rk3368_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rk3368_isp_clk_rate),
	.irqs = rk3368_isp_irqs,
	.num_irqs = ARRAY_SIZE(rk3368_isp_irqs)
};

static const struct isp_match_data rk3399_isp_match_data = {
	.clks = rk3399_isp_clks,
	.num_clks = ARRAY_SIZE(rk3399_isp_clks),
	.isp_ver = ISP_V10,
	.clk_rate_tbl = rk3399_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rk3399_isp_clk_rate),
	.irqs = rk3399_isp_irqs,
	.num_irqs = ARRAY_SIZE(rk3399_isp_irqs)
};

static const struct of_device_id rkisp1_plat_of_match[] = {
	{
		.compatible = "rockchip,rk1808-rkisp1",
		.data = &rk1808_isp_match_data,
	}, {
		.compatible = "rockchip,rk3288-rkisp1",
		.data = &rk3288_isp_match_data,
	}, {
		.compatible = "rockchip,rk3326-rkisp1",
		.data = &rk3326_isp_match_data,
	}, {
		.compatible = "rockchip,rk3368-rkisp1",
		.data = &rk3368_isp_match_data,
	}, {
		.compatible = "rockchip,rk3399-rkisp1",
		.data = &rk3399_isp_match_data,
	},
	{},
};

static void rkisp1_disable_sys_clk(struct rkisp1_device *rkisp1_dev)
{
	int i;

	for (i = rkisp1_dev->num_clks - 1; i >= 0; i--)
		if (!IS_ERR(rkisp1_dev->clks[i]))
			clk_disable_unprepare(rkisp1_dev->clks[i]);
}

static int rkisp1_enable_sys_clk(struct rkisp1_device *rkisp1_dev)
{
	int i, ret = -EINVAL;

	for (i = 0; i < rkisp1_dev->num_clks; i++) {
		if (!IS_ERR(rkisp1_dev->clks[i])) {
			ret = clk_prepare_enable(rkisp1_dev->clks[i]);
			if (ret < 0)
				goto err;
		}
	}
	return 0;
err:
	for (--i; i >= 0; --i)
		if (!IS_ERR(rkisp1_dev->clks[i]))
			clk_disable_unprepare(rkisp1_dev->clks[i]);
	return ret;
}

static inline bool is_iommu_enable(struct device *dev)
{
	struct device_node *iommu;

	iommu = of_parse_phandle(dev->of_node, "iommus", 0);
	if (!iommu) {
		dev_info(dev, "no iommu attached, using non-iommu buffers\n");
		return false;
	} else if (!of_device_is_available(iommu)) {
		dev_info(dev, "iommu is disabled, using non-iommu buffers\n");
		of_node_put(iommu);
		return false;
	}
	of_node_put(iommu);

	return true;
}

static int rkisp1_vs_irq_parse(struct platform_device *pdev)
{
	int ret;
	int vs_irq;
	unsigned long vs_irq_flags;
	struct gpio_desc *vs_irq_gpio;
	struct device *dev = &pdev->dev;
	struct rkisp1_device *isp_dev = dev_get_drvdata(dev);

	/* this irq recevice the message of sensor vs from preisp */
	isp_dev->vs_irq = -1;
	vs_irq_gpio = devm_gpiod_get(dev, "vsirq", GPIOD_IN);
	if (!IS_ERR(vs_irq_gpio)) {
		vs_irq_flags = IRQF_TRIGGER_RISING |
			       IRQF_ONESHOT | IRQF_SHARED;

		vs_irq = gpiod_to_irq(vs_irq_gpio);
		if (vs_irq < 0) {
			dev_err(dev, "GPIO to interrupt failed\n");
			return vs_irq;
		}

		dev_info(dev, "register_irq: %d\n", vs_irq);
		ret = devm_request_irq(dev,
				       vs_irq,
				       rkisp1_vs_isr_handler,
				       vs_irq_flags,
				       "vs_irq_gpio_int",
				       dev);
		if (ret) {
			dev_err(dev, "devm_request_irq failed: %d\n", ret);
			return ret;
		} else {
			disable_irq(vs_irq);
			isp_dev->vs_irq = vs_irq;
			isp_dev->vs_irq_gpio = vs_irq_gpio;
			dev_info(dev, "vs_gpio_int interrupt is hooked\n");
		}
	}

	return 0;
}

static const struct media_device_ops rkisp1_media_ops = {
	.link_notify = v4l2_pipeline_link_notify,
};

static int rkisp1_plat_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct v4l2_device *v4l2_dev;
	struct rkisp1_device *isp_dev;
	const struct isp_match_data *match_data;
	struct resource *res;
	int i, ret, irq;

	sprintf(rkisp1_version, "v%02x.%02x.%02x",
		RKISP1_DRIVER_VERSION >> 16,
		(RKISP1_DRIVER_VERSION & 0xff00) >> 8,
		RKISP1_DRIVER_VERSION & 0x00ff);

	dev_info(dev, "rkisp1 driver version: %s\n", rkisp1_version);

	match = of_match_node(rkisp1_plat_of_match, node);
	isp_dev = devm_kzalloc(dev, sizeof(*isp_dev), GFP_KERNEL);
	if (!isp_dev)
		return -ENOMEM;

	dev_set_drvdata(dev, isp_dev);
	isp_dev->dev = dev;

	isp_dev->grf = syscon_regmap_lookup_by_phandle(dev->of_node,
		"rockchip,grf");
	if (IS_ERR(isp_dev->grf))
		dev_warn(dev, "Missing rockchip,grf property\n");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	isp_dev->base_addr = devm_ioremap_resource(dev, res);
	if (IS_ERR(isp_dev->base_addr))
		return PTR_ERR(isp_dev->base_addr);

	match_data = match->data;
	isp_dev->mipi_irq = -1;
	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
					   match_data->irqs[0].name);
	if (res) {
		/* there are irq names in dts */
		for (i = 0; i < match_data->num_irqs; i++) {
			irq = platform_get_irq_byname(pdev,
						      match_data->irqs[i].name);
			if (irq < 0) {
				dev_err(dev, "no irq %s in dts\n",
					match_data->irqs[i].name);
				return irq;
			}

			if (!strcmp(match_data->irqs[i].name, "mipi_irq"))
				isp_dev->mipi_irq = irq;

			ret = devm_request_irq(dev, irq,
					       match_data->irqs[i].irq_hdl,
					       IRQF_SHARED,
					       dev_driver_string(dev),
					       dev);
			if (ret < 0) {
				dev_err(dev, "request %s failed: %d\n",
					match_data->irqs[i].name,
					ret);
				return ret;
			}

			if (isp_dev->mipi_irq == irq)
				disable_irq(isp_dev->mipi_irq);
		}
	} else {
		/* no irq names in dts */
		irq = platform_get_irq(pdev, 0);
		if (irq < 0) {
			dev_err(dev, "no isp irq in dts\n");
			return irq;
		}

		ret = devm_request_irq(dev, irq,
				       rkisp1_irq_handler,
				       IRQF_SHARED,
				       dev_driver_string(dev),
				       dev);
		if (ret < 0) {
			dev_err(dev, "request irq failed: %d\n", ret);
			return ret;
		}
	}

	for (i = 0; i < match_data->num_clks; i++) {
		struct clk *clk = devm_clk_get(dev, match_data->clks[i]);

		if (IS_ERR(clk))
			dev_dbg(dev, "failed to get %s\n", match_data->clks[i]);
		isp_dev->clks[i] = clk;
	}
	isp_dev->num_clks = match_data->num_clks;
	isp_dev->isp_ver = match_data->isp_ver;
	isp_dev->clk_rate_tbl = match_data->clk_rate_tbl;
	isp_dev->num_clk_rate_tbl = match_data->num_clk_rate_tbl;

	mutex_init(&isp_dev->apilock);
	mutex_init(&isp_dev->iqlock);
	atomic_set(&isp_dev->pipe.power_cnt, 0);
	atomic_set(&isp_dev->pipe.stream_cnt, 0);
	atomic_set(&isp_dev->open_cnt, 0);
	init_waitqueue_head(&isp_dev->sync_onoff);
	isp_dev->pipe.open = rkisp1_pipeline_open;
	isp_dev->pipe.close = rkisp1_pipeline_close;
	isp_dev->pipe.set_stream = rkisp1_pipeline_set_stream;

	rkisp1_stream_init(isp_dev, RKISP1_STREAM_SP);
	rkisp1_stream_init(isp_dev, RKISP1_STREAM_MP);
	rkisp1_stream_init(isp_dev, RKISP1_STREAM_RAW);

	strlcpy(isp_dev->media_dev.model, "rkisp1",
		sizeof(isp_dev->media_dev.model));
	isp_dev->media_dev.dev = &pdev->dev;
	isp_dev->media_dev.ops = &rkisp1_media_ops;
	v4l2_dev = &isp_dev->v4l2_dev;
	v4l2_dev->mdev = &isp_dev->media_dev;
	strlcpy(v4l2_dev->name, "rkisp1", sizeof(v4l2_dev->name));
	v4l2_ctrl_handler_init(&isp_dev->ctrl_handler, 5);
	v4l2_dev->ctrl_handler = &isp_dev->ctrl_handler;

	ret = v4l2_device_register(isp_dev->dev, &isp_dev->v4l2_dev);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "Failed to register v4l2 device: %d\n",
			 ret);
		return ret;
	}

	media_device_init(&isp_dev->media_dev);
	ret = media_device_register(&isp_dev->media_dev);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "Failed to register media device: %d\n",
			 ret);
		goto err_unreg_v4l2_dev;
	}

	/* create & register platefom subdev (from of_node) */
	ret = rkisp1_register_platform_subdevs(isp_dev);
	if (ret < 0)
		goto err_unreg_media_dev;

	if (!is_iommu_enable(dev)) {
		ret = of_reserved_mem_device_init(dev);
		if (ret)
			v4l2_warn(v4l2_dev,
				  "No reserved memory region assign to isp\n");
	}

	pm_runtime_enable(&pdev->dev);

	ret = rkisp1_vs_irq_parse(pdev);
	if (ret)
		goto err_runtime_disable;

	mutex_lock(&rkisp1_dev_mutex);
	list_add_tail(&isp_dev->list, &rkisp1_device_list);
	mutex_unlock(&rkisp1_dev_mutex);
	return 0;

err_runtime_disable:
	pm_runtime_disable(&pdev->dev);
err_unreg_media_dev:
	media_device_unregister(&isp_dev->media_dev);
err_unreg_v4l2_dev:
	v4l2_device_unregister(&isp_dev->v4l2_dev);

	return ret;
}

static int rkisp1_plat_remove(struct platform_device *pdev)
{
	struct rkisp1_device *isp_dev = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	media_device_unregister(&isp_dev->media_dev);
	v4l2_device_unregister(&isp_dev->v4l2_dev);
	rkisp1_unregister_params_vdev(&isp_dev->params_vdev);
	rkisp1_unregister_stats_vdev(&isp_dev->stats_vdev);
	rkisp1_unregister_stream_vdevs(isp_dev);
	rkisp1_unregister_isp_subdev(isp_dev);
	media_device_cleanup(&isp_dev->media_dev);

	return 0;
}

static int __maybe_unused rkisp1_runtime_suspend(struct device *dev)
{
	struct rkisp1_device *isp_dev = dev_get_drvdata(dev);

	if (isp_dev->isp_ver == ISP_V12 || isp_dev->isp_ver == ISP_V13) {
		if (isp_dev->mipi_irq >= 0)
			disable_irq(isp_dev->mipi_irq);
	}
	rkisp1_disable_sys_clk(isp_dev);
	return pinctrl_pm_select_sleep_state(dev);
}

static int __maybe_unused rkisp1_runtime_resume(struct device *dev)
{
	struct rkisp1_device *isp_dev = dev_get_drvdata(dev);
	int ret;

	ret = pinctrl_pm_select_default_state(dev);
	if (ret < 0)
		return ret;
	rkisp1_enable_sys_clk(isp_dev);

	if (isp_dev->isp_ver == ISP_V12 || isp_dev->isp_ver == ISP_V13) {
		writel(0, isp_dev->base_addr + CIF_ISP_CSI0_MASK1);
		writel(0, isp_dev->base_addr + CIF_ISP_CSI0_MASK2);
		writel(0, isp_dev->base_addr + CIF_ISP_CSI0_MASK3);
		if (isp_dev->mipi_irq >= 0)
			enable_irq(isp_dev->mipi_irq);
	}

	return 0;
}

#ifndef MODULE
static int __init rkisp1_clr_unready_dev(void)
{
	__rkisp1_clr_unready_dev();
	return 0;
}
late_initcall_sync(rkisp1_clr_unready_dev);
#endif

static const struct dev_pm_ops rkisp1_plat_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(rkisp1_runtime_suspend, rkisp1_runtime_resume, NULL)
};

static struct platform_driver rkisp1_plat_drv = {
	.driver = {
		   .name = DRIVER_NAME,
		   .of_match_table = of_match_ptr(rkisp1_plat_of_match),
		   .pm = &rkisp1_plat_pm_ops,
	},
	.probe = rkisp1_plat_probe,
	.remove = rkisp1_plat_remove,
};

module_platform_driver(rkisp1_plat_drv);
MODULE_AUTHOR("Rockchip Camera/ISP team");
MODULE_DESCRIPTION("Rockchip ISP1 platform driver");
MODULE_LICENSE("Dual BSD/GPL");
