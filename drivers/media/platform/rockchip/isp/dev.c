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
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/pm_runtime.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <dt-bindings/soc/rockchip-system-status.h>
#include <soc/rockchip/rockchip-system-status.h>
#include "common.h"
#include "isp_ispp.h"
#include "regs.h"
#include "rkisp.h"
#include "version.h"

#define RKISP_VERNO_LEN		10

struct isp_irqs_data {
	const char *name;
	irqreturn_t (*irq_hdl)(int irq, void *ctx);
};

struct isp_match_data {
	const char * const *clks;
	int num_clks;
	enum rkisp_isp_ver isp_ver;
	const unsigned int *clk_rate_tbl;
	int num_clk_rate_tbl;
	struct isp_irqs_data *irqs;
	int num_irqs;
};

int rkisp_debug;
module_param_named(debug, rkisp_debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

static char rkisp_version[RKISP_VERNO_LEN];
module_param_string(version, rkisp_version, RKISP_VERNO_LEN, 0444);
MODULE_PARM_DESC(version, "version number");

static DEFINE_MUTEX(rkisp_dev_mutex);
static LIST_HEAD(rkisp_device_list);

static int __maybe_unused __rkisp_clr_unready_dev(void)
{
	struct rkisp_device *isp_dev;

	mutex_lock(&rkisp_dev_mutex);
	list_for_each_entry(isp_dev, &rkisp_device_list, list)
		v4l2_async_notifier_clr_unready_dev(&isp_dev->notifier);
	mutex_unlock(&rkisp_dev_mutex);

	return 0;
}

static int rkisp_clr_unready_dev_param_set(const char *val, const struct kernel_param *kp)
{
#ifdef MODULE
	__rkisp_clr_unready_dev();
#endif

	return 0;
}

module_param_call(clr_unready_dev, rkisp_clr_unready_dev_param_set, NULL, NULL, 0200);
MODULE_PARM_DESC(clr_unready_dev, "clear unready devices");

/**************************** pipeline operations *****************************/

static int __isp_pipeline_prepare(struct rkisp_pipeline *p,
				  struct media_entity *me)
{
	struct rkisp_device *dev = container_of(p, struct rkisp_device, pipe);
	struct v4l2_subdev *sd;
	int i;

	p->num_subdevs = 0;
	memset(p->subdevs, 0, sizeof(p->subdevs));

	if (!(dev->isp_inp & (INP_CSI | INP_DVP | INP_LVDS)))
		return 0;

	while (1) {
		struct media_pad *pad = NULL;

		/* Find remote source pad */
		for (i = 0; i < me->num_pads; i++) {
			struct media_pad *spad = &me->pads[i];

			if (!(spad->flags & MEDIA_PAD_FL_SINK))
				continue;
			pad = rkisp_media_entity_remote_pad(spad);
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

static int __isp_pipeline_s_isp_clk(struct rkisp_pipeline *p)
{
	struct rkisp_device *dev = container_of(p, struct rkisp_device, pipe);
	struct v4l2_subdev *sd;
	struct v4l2_ctrl *ctrl;
	u64 data_rate;
	int i;

	if (!(dev->isp_inp & (INP_CSI | INP_DVP | INP_LVDS))) {
		if (dev->clks[0])
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
	v4l2_dbg(1, rkisp_debug, sd, "set isp clk = %luHz\n",
		 clk_get_rate(dev->clks[0]));

	return 0;
}

static int rkisp_pipeline_open(struct rkisp_pipeline *p,
				struct media_entity *me,
				bool prepare)
{
	int ret;
	struct rkisp_device *dev = container_of(p, struct rkisp_device, pipe);

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

	if (dev->isp_inp & (INP_CSI | INP_RAWRD0 | INP_RAWRD1 | INP_RAWRD2))
		rkisp_csi_config_patch(dev);
	return 0;
}

static int rkisp_pipeline_close(struct rkisp_pipeline *p)
{
	atomic_dec(&p->power_cnt);

	return 0;
}

/*
 * stream-on order: isp_subdev, mipi dphy, sensor
 * stream-off order: mipi dphy, sensor, isp_subdev
 */
static int rkisp_pipeline_set_stream(struct rkisp_pipeline *p, bool on)
{
	struct rkisp_device *dev = container_of(p, struct rkisp_device, pipe);
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

static int rkisp_create_links(struct rkisp_device *dev)
{
	unsigned int s, pad;
	int ret = 0;

	/* sensor links(or mipi-phy) */
	for (s = 0; s < dev->num_sensors; ++s) {
		struct rkisp_sensor_info *sensor = &dev->sensors[s];
		u32 type = sensor->sd->entity.function;
		bool en = s ? 0 : MEDIA_LNK_FL_ENABLED;

		for (pad = 0; pad < sensor->sd->entity.num_pads; pad++)
			if (sensor->sd->entity.pads[pad].flags & MEDIA_PAD_FL_SOURCE)
				break;

		if (pad == sensor->sd->entity.num_pads) {
			dev_err(dev->dev, "failed to find src pad for %s\n",
				sensor->sd->name);
			return -ENXIO;
		}

		/* sensor link -> isp */
		if (type == MEDIA_ENT_F_CAM_SENSOR) {
			dev->isp_inp = INP_DVP;
			ret = media_create_pad_link(&sensor->sd->entity, pad,
				&dev->isp_sdev.sd.entity, RKISP_ISP_PAD_SINK, en);
		} else {
			v4l2_subdev_call(sensor->sd, video,
					 g_mbus_config, &sensor->mbus);
			if (sensor->mbus.type == V4L2_MBUS_CCP2) {
				/* mipi-phy lvds link -> isp */
				dev->isp_inp = INP_LVDS;
				ret = media_create_pad_link(&sensor->sd->entity, pad,
					&dev->isp_sdev.sd.entity, RKISP_ISP_PAD_SINK, en);
			} else {
				/* mipi-phy link -> csi -> isp */
				dev->isp_inp = INP_CSI;
				ret = media_create_pad_link(&sensor->sd->entity,
					pad, &dev->csi_dev.sd.entity, CSI_SINK, en);
				ret |= media_create_pad_link(&dev->csi_dev.sd.entity, CSI_SRC_CH0,
					&dev->isp_sdev.sd.entity, RKISP_ISP_PAD_SINK, en);
				dev->csi_dev.sink[0].linked = en;
				dev->csi_dev.sink[0].index = BIT(0);
			}
		}
		if (ret)
			dev_err(dev->dev, "failed to create link for %s\n", sensor->sd->name);
	}
	return ret;
}

static int _set_pipeline_default_fmt(struct rkisp_device *dev)
{
	struct v4l2_subdev *isp;
	struct v4l2_subdev_format fmt;
	struct v4l2_subdev_selection sel;
	u32 width, height, code;

	isp = &dev->isp_sdev.sd;

	fmt = dev->active_sensor->fmt[0];
	code = fmt.format.code;
	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	fmt.pad = RKISP_ISP_PAD_SINK;
	/* isp input format information from sensor */
	v4l2_subdev_call(isp, pad, set_fmt, NULL, &fmt);

	rkisp_align_sensor_resolution(dev, &sel.r, false);
	width = sel.r.width;
	height = sel.r.height;
	sel.target = V4L2_SEL_TGT_CROP;
	sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	sel.pad = RKISP_ISP_PAD_SINK;
	/* image resolution processed by isp */
	v4l2_subdev_call(isp, pad, set_selection, NULL, &sel);

	/* change fmt&size for RKISP_ISP_PAD_SOURCE_PATH */
	if ((code & RKISP_MEDIA_BUS_FMT_MASK) == RKISP_MEDIA_BUS_FMT_BAYER)
		fmt.format.code = MEDIA_BUS_FMT_YUYV8_2X8;

	sel.r.left = 0;
	sel.r.top = 0;
	fmt.format.width = width;
	fmt.format.height = height;
	fmt.pad = RKISP_ISP_PAD_SOURCE_PATH;
	sel.pad = RKISP_ISP_PAD_SOURCE_PATH;
	v4l2_subdev_call(isp, pad, set_fmt, NULL, &fmt);
	v4l2_subdev_call(isp, pad, set_selection, NULL, &sel);

	/* change fmt&size of MP/SP */
	rkisp_set_stream_def_fmt(dev, RKISP_STREAM_MP,
				 width, height, V4L2_PIX_FMT_YUYV);
	if (dev->isp_ver != ISP_V10_1)
		rkisp_set_stream_def_fmt(dev, RKISP_STREAM_SP,
					 width, height, V4L2_PIX_FMT_YUYV);
	if ((dev->isp_ver == ISP_V12 ||
	     dev->isp_ver == ISP_V13 ||
	     dev->isp_ver == ISP_V20) &&
	    dev->active_sensor->mbus.type == V4L2_MBUS_CSI2) {
		width = dev->active_sensor->fmt[1].format.width;
		height = dev->active_sensor->fmt[1].format.height;
		code = dev->active_sensor->fmt[1].format.code;
		rkisp_set_stream_def_fmt(dev, RKISP_STREAM_DMATX0,
			width, height, rkisp_mbus_pixelcode_to_v4l2(code));
	}
	if (dev->isp_ver == ISP_V20 &&
	    dev->active_sensor->mbus.type == V4L2_MBUS_CSI2) {
		width = dev->active_sensor->fmt[2].format.width;
		height = dev->active_sensor->fmt[2].format.height;
		code = dev->active_sensor->fmt[2].format.code;
		rkisp_set_stream_def_fmt(dev, RKISP_STREAM_DMATX1,
			width, height, rkisp_mbus_pixelcode_to_v4l2(code));
		width = dev->active_sensor->fmt[3].format.width;
		height = dev->active_sensor->fmt[3].format.height;
		code = dev->active_sensor->fmt[3].format.code;
		rkisp_set_stream_def_fmt(dev, RKISP_STREAM_DMATX2,
			width, height, rkisp_mbus_pixelcode_to_v4l2(code));
	}
	return 0;
}

static int subdev_notifier_complete(struct v4l2_async_notifier *notifier)
{
	struct rkisp_device *dev;
	int ret;

	dev = container_of(notifier, struct rkisp_device, notifier);

	mutex_lock(&dev->media_dev.graph_mutex);
	ret = rkisp_create_links(dev);
	if (ret < 0)
		goto unlock;
	ret = v4l2_device_register_subdev_nodes(&dev->v4l2_dev);
	if (ret < 0)
		goto unlock;

	ret = rkisp_update_sensor_info(dev);
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

struct rkisp_async_subdev {
	struct v4l2_async_subdev asd;
	struct v4l2_mbus_config mbus;
};

static int subdev_notifier_bound(struct v4l2_async_notifier *notifier,
				 struct v4l2_subdev *subdev,
				 struct v4l2_async_subdev *asd)
{
	struct rkisp_device *isp_dev = container_of(notifier,
					struct rkisp_device, notifier);
	struct rkisp_async_subdev *s_asd = container_of(asd,
					struct rkisp_async_subdev, asd);

	if (isp_dev->num_sensors == ARRAY_SIZE(isp_dev->sensors))
		return -EBUSY;

	isp_dev->sensors[isp_dev->num_sensors].mbus = s_asd->mbus;
	isp_dev->sensors[isp_dev->num_sensors].sd = subdev;
	++isp_dev->num_sensors;

	v4l2_dbg(1, rkisp_debug, subdev, "Async registered subdev\n");

	return 0;
}

static int rkisp_fwnode_parse(struct device *dev,
			       struct v4l2_fwnode_endpoint *vep,
			       struct v4l2_async_subdev *asd)
{
	struct rkisp_async_subdev *rk_asd =
			container_of(asd, struct rkisp_async_subdev, asd);
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

static int isp_subdev_notifier(struct rkisp_device *isp_dev)
{
	struct v4l2_async_notifier *ntf = &isp_dev->notifier;
	struct device *dev = isp_dev->dev;
	int ret;

	ret = v4l2_async_notifier_parse_fwnode_endpoints(
		dev, ntf, sizeof(struct rkisp_async_subdev),
		rkisp_fwnode_parse);
	if (ret < 0)
		return ret;

	if (!ntf->num_subdevs)
		return -ENODEV;	/* no endpoint */

	ntf->ops = &subdev_notifier_ops;

	return v4l2_async_notifier_register(&isp_dev->v4l2_dev, ntf);
}

/***************************** platform deive *******************************/

static int rkisp_register_platform_subdevs(struct rkisp_device *dev)
{
	int ret;

	ret = rkisp_register_isp_subdev(dev, &dev->v4l2_dev);
	if (ret < 0)
		return ret;

	ret = rkisp_register_csi_subdev(dev, &dev->v4l2_dev);
	if (ret < 0)
		goto err_unreg_isp_subdev;

	ret = rkisp_register_bridge_subdev(dev, &dev->v4l2_dev);
	if (ret < 0)
		goto err_unreg_csi_subdev;

	ret = rkisp_register_stream_vdevs(dev);
	if (ret < 0)
		goto err_unreg_bridge_subdev;

	ret = rkisp_register_dmarx_vdev(dev);
	if (ret < 0)
		goto err_unreg_stream_vdev;

	ret = rkisp_register_stats_vdev(&dev->stats_vdev, &dev->v4l2_dev, dev);
	if (ret < 0)
		goto err_unreg_dmarx_vdev;

	ret = rkisp_register_params_vdev(&dev->params_vdev, &dev->v4l2_dev, dev);
	if (ret < 0)
		goto err_unreg_stats_vdev;

	ret = rkisp_register_luma_vdev(&dev->luma_vdev, &dev->v4l2_dev, dev);
	if (ret < 0)
		goto err_unreg_params_vdev;

	ret = isp_subdev_notifier(dev);
	if (ret < 0) {
		v4l2_err(&dev->v4l2_dev,
			 "Failed to register subdev notifier(%d)\n", ret);
		/* maybe use dmarx to input image */
		ret = v4l2_device_register_subdev_nodes(&dev->v4l2_dev);
		if (ret == 0)
			return 0;
		goto err_unreg_luma_vdev;
	}

	return 0;
err_unreg_luma_vdev:
	rkisp_unregister_luma_vdev(&dev->luma_vdev);
err_unreg_params_vdev:
	rkisp_unregister_params_vdev(&dev->params_vdev);
err_unreg_stats_vdev:
	rkisp_unregister_stats_vdev(&dev->stats_vdev);
err_unreg_dmarx_vdev:
	rkisp_unregister_dmarx_vdev(dev);
err_unreg_stream_vdev:
	rkisp_unregister_stream_vdevs(dev);
err_unreg_bridge_subdev:
	rkisp_unregister_bridge_subdev(dev);
err_unreg_csi_subdev:
	rkisp_unregister_csi_subdev(dev);
err_unreg_isp_subdev:
	rkisp_unregister_isp_subdev(dev);

	return ret;
}

static irqreturn_t rkisp_irq_handler(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp_device *rkisp_dev = dev_get_drvdata(dev);
	unsigned int mis_val, mis_3a = 0;

	mis_val = readl(rkisp_dev->base_addr + CIF_ISP_MIS);
	if (rkisp_dev->isp_ver == ISP_V20)
		mis_3a = readl(rkisp_dev->base_addr + ISP_ISP3A_MIS);
	if (mis_val || mis_3a)
		rkisp_isp_isr(mis_val, mis_3a, rkisp_dev);

	mis_val = readl(rkisp_dev->base_addr + CIF_MIPI_MIS);
	if (mis_val)
		rkisp_mipi_isr(mis_val, rkisp_dev);

	mis_val = readl(rkisp_dev->base_addr + CIF_MI_MIS);
	if (mis_val)
		rkisp_mi_isr(mis_val, rkisp_dev);

	return IRQ_HANDLED;
}

static irqreturn_t rkisp_isp_irq_hdl(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp_device *rkisp_dev = dev_get_drvdata(dev);
	unsigned int mis_val, mis_3a = 0;

	if (rkisp_dev->is_thunderboot)
		return IRQ_HANDLED;

	mis_val = readl(rkisp_dev->base_addr + CIF_ISP_MIS);
	if (rkisp_dev->isp_ver == ISP_V20)
		mis_3a = readl(rkisp_dev->base_addr + ISP_ISP3A_MIS);
	if (mis_val || mis_3a)
		rkisp_isp_isr(mis_val, mis_3a, rkisp_dev);

	return IRQ_HANDLED;
}

static irqreturn_t rkisp_mi_irq_hdl(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp_device *rkisp_dev = dev_get_drvdata(dev);
	unsigned int mis_val;

	if (rkisp_dev->is_thunderboot)
		return IRQ_HANDLED;

	mis_val = readl(rkisp_dev->base_addr + CIF_MI_MIS);
	if (mis_val)
		rkisp_mi_isr(mis_val, rkisp_dev);

	return IRQ_HANDLED;
}

static irqreturn_t rkisp_mipi_irq_hdl(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp_device *rkisp_dev = dev_get_drvdata(dev);

	if (rkisp_dev->is_thunderboot)
		return IRQ_HANDLED;

	if (rkisp_dev->isp_ver == ISP_V13 ||
		rkisp_dev->isp_ver == ISP_V12) {
		u32 err1, err2, err3;

		err1 = readl(rkisp_dev->base_addr + CIF_ISP_CSI0_ERR1);
		err2 = readl(rkisp_dev->base_addr + CIF_ISP_CSI0_ERR2);
		err3 = readl(rkisp_dev->base_addr + CIF_ISP_CSI0_ERR3);

		if (err3 & 0xf)
			rkisp_mipi_dmatx0_end(err3, rkisp_dev);
		if (err1 || err2 || err3)
			rkisp_mipi_v13_isr(err1, err2, err3, rkisp_dev);
	} else if (rkisp_dev->isp_ver == ISP_V20) {
		u32 phy, packet, overflow, state;

		state = readl(rkisp_dev->base_addr + CSI2RX_ERR_STAT);
		phy = readl(rkisp_dev->base_addr + CSI2RX_ERR_PHY);
		packet = readl(rkisp_dev->base_addr + CSI2RX_ERR_PACKET);
		overflow = readl(rkisp_dev->base_addr + CSI2RX_ERR_OVERFLOW);
		if (phy | packet | overflow | state)
			rkisp_mipi_v20_isr(phy, packet, overflow,
					    state, rkisp_dev);
	} else {
		u32 mis_val = readl(rkisp_dev->base_addr + CIF_MIPI_MIS);

		if (mis_val)
			rkisp_mipi_isr(mis_val, rkisp_dev);
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

static const char * const rv1126_isp_clks[] = {
	"clk_isp",
	"aclk_isp",
	"hclk_isp",
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

static const unsigned int rv1126_isp_clk_rate[] = {
	400, 500
};

static struct isp_irqs_data rk1808_isp_irqs[] = {
	{"isp_irq", rkisp_isp_irq_hdl},
	{"mi_irq", rkisp_mi_irq_hdl},
	{"mipi_irq", rkisp_mipi_irq_hdl}
};

static struct isp_irqs_data rk3288_isp_irqs[] = {
	{"isp_irq", rkisp_irq_handler}
};

static struct isp_irqs_data rk3326_isp_irqs[] = {
	{"isp_irq", rkisp_isp_irq_hdl},
	{"mi_irq", rkisp_mi_irq_hdl},
	{"mipi_irq", rkisp_mipi_irq_hdl}
};

static struct isp_irqs_data rk3368_isp_irqs[] = {
	{"isp_irq", rkisp_irq_handler}
};

static struct isp_irqs_data rk3399_isp_irqs[] = {
	{"isp_irq", rkisp_irq_handler}
};

static struct isp_irqs_data rv1126_isp_irqs[] = {
	{"isp_irq", rkisp_isp_irq_hdl},
	{"mi_irq", rkisp_mi_irq_hdl},
	{"mipi_irq", rkisp_mipi_irq_hdl}
};

static const struct isp_match_data rv1126_isp_match_data = {
	.clks = rv1126_isp_clks,
	.num_clks = ARRAY_SIZE(rv1126_isp_clks),
	.isp_ver = ISP_V20,
	.clk_rate_tbl = rv1126_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rv1126_isp_clk_rate),
	.irqs = rv1126_isp_irqs,
	.num_irqs = ARRAY_SIZE(rv1126_isp_irqs)
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

static const struct of_device_id rkisp_plat_of_match[] = {
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
	}, {
		.compatible = "rockchip,rv1126-rkisp",
		.data = &rv1126_isp_match_data,
	},
	{},
};

static void rkisp_disable_sys_clk(struct rkisp_device *rkisp_dev)
{
	int i;

	for (i = rkisp_dev->num_clks - 1; i >= 0; i--)
		if (!IS_ERR(rkisp_dev->clks[i]))
			clk_disable_unprepare(rkisp_dev->clks[i]);
}

static int rkisp_enable_sys_clk(struct rkisp_device *rkisp_dev)
{
	int i, ret = -EINVAL;

	for (i = 0; i < rkisp_dev->num_clks; i++) {
		if (!IS_ERR(rkisp_dev->clks[i])) {
			ret = clk_prepare_enable(rkisp_dev->clks[i]);
			if (ret < 0)
				goto err;
		}
	}
	return 0;
err:
	for (--i; i >= 0; --i)
		if (!IS_ERR(rkisp_dev->clks[i]))
			clk_disable_unprepare(rkisp_dev->clks[i]);
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

static int rkisp_vs_irq_parse(struct platform_device *pdev)
{
	int ret;
	int vs_irq;
	unsigned long vs_irq_flags;
	struct gpio_desc *vs_irq_gpio;
	struct device *dev = &pdev->dev;
	struct rkisp_device *isp_dev = dev_get_drvdata(dev);

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
				       rkisp_vs_isr_handler,
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

static const struct media_device_ops rkisp_media_ops = {
	.link_notify = v4l2_pipeline_link_notify,
};

static int rkisp_get_reserved_mem(struct rkisp_device *isp_dev)
{
	struct device *dev = isp_dev->dev;
	struct device_node *np;
	struct resource r;
	int ret;

	/* Get reserved memory region from Device-tree */
	np = of_parse_phandle(dev->of_node, "memory-region-thunderboot", 0);
	if (!np) {
		dev_err(dev, "No %s specified\n", "memory-region-thunderboot");
		return -1;
	}

	ret = of_address_to_resource(np, 0, &r);
	if (ret) {
		dev_err(dev, "No memory address assigned to the region\n");
		return -1;
	}

	isp_dev->resmem_pa = r.start;
	isp_dev->resmem_size = resource_size(&r);
	dev_info(dev, "Allocated reserved memory, paddr: 0x%x\n",
		(u32)isp_dev->resmem_pa);

	return 0;
}

int rkisp_register_irq(struct rkisp_device *isp_dev)
{
	const struct isp_match_data *match_data = isp_dev->match_data;
	struct platform_device *pdev = isp_dev->pdev;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int i, ret, irq;

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

			if (isp_dev->mipi_irq == irq &&
			    (isp_dev->isp_ver == ISP_V12 ||
			     isp_dev->isp_ver == ISP_V13))
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
				       rkisp_irq_handler,
				       IRQF_SHARED,
				       dev_driver_string(dev),
				       dev);
		if (ret < 0) {
			dev_err(dev, "request irq failed: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int rkisp_plat_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct v4l2_device *v4l2_dev;
	struct rkisp_device *isp_dev;
	const struct isp_match_data *match_data;
	struct resource *res;
	int i, ret;

	sprintf(rkisp_version, "v%02x.%02x.%02x",
		RKISP_DRIVER_VERSION >> 16,
		(RKISP_DRIVER_VERSION & 0xff00) >> 8,
		RKISP_DRIVER_VERSION & 0x00ff);

	dev_info(dev, "rkisp driver version: %s\n", rkisp_version);

	match = of_match_node(rkisp_plat_of_match, node);
	isp_dev = devm_kzalloc(dev, sizeof(*isp_dev), GFP_KERNEL);
	if (!isp_dev)
		return -ENOMEM;

	dev_set_drvdata(dev, isp_dev);
	isp_dev->dev = dev;
	isp_dev->is_thunderboot = IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP);
	dev_info(dev, "is_thunderboot: %d\n", isp_dev->is_thunderboot);

	isp_dev->grf = syscon_regmap_lookup_by_phandle(dev->of_node,
		"rockchip,grf");
	if (IS_ERR(isp_dev->grf))
		dev_warn(dev, "Missing rockchip,grf property\n");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "get resource failed\n");
		return -EINVAL;
	}
	isp_dev->base_addr = devm_ioremap_resource(dev, res);
	if (PTR_ERR(isp_dev->base_addr) == -EBUSY) {
		resource_size_t offset = res->start;
		resource_size_t size = resource_size(res);

		isp_dev->base_addr = devm_ioremap(dev, offset, size);
	}
	if (IS_ERR(isp_dev->base_addr)) {
		dev_err(dev, "ioremap failed\n");
		return PTR_ERR(isp_dev->base_addr);
	}
	rkisp_get_reserved_mem(isp_dev);

	match_data = match->data;
	isp_dev->mipi_irq = -1;
	isp_dev->isp_ver = match_data->isp_ver;

	isp_dev->pdev = pdev;
	isp_dev->match_data = match_data;
	if (!isp_dev->is_thunderboot)
		rkisp_register_irq(isp_dev);

	for (i = 0; i < match_data->num_clks; i++) {
		struct clk *clk = devm_clk_get(dev, match_data->clks[i]);

		if (IS_ERR(clk))
			dev_dbg(dev, "failed to get %s\n", match_data->clks[i]);
		isp_dev->clks[i] = clk;
	}
	isp_dev->num_clks = match_data->num_clks;
	isp_dev->clk_rate_tbl = match_data->clk_rate_tbl;
	isp_dev->num_clk_rate_tbl = match_data->num_clk_rate_tbl;

	mutex_init(&isp_dev->apilock);
	mutex_init(&isp_dev->iqlock);
	atomic_set(&isp_dev->pipe.power_cnt, 0);
	atomic_set(&isp_dev->pipe.stream_cnt, 0);
	init_waitqueue_head(&isp_dev->sync_onoff);
	isp_dev->pipe.open = rkisp_pipeline_open;
	isp_dev->pipe.close = rkisp_pipeline_close;
	isp_dev->pipe.set_stream = rkisp_pipeline_set_stream;

	if (isp_dev->isp_ver == ISP_V20) {
		atomic_set(&isp_dev->hdr.refcnt, 0);
		for (i = 0; i < HDR_DMA_MAX; i++) {
			INIT_LIST_HEAD(&isp_dev->hdr.q_tx[i]);
			INIT_LIST_HEAD(&isp_dev->hdr.q_rx[i]);
		}
	}

	strlcpy(isp_dev->media_dev.model, DRIVER_NAME,
		sizeof(isp_dev->media_dev.model));
	isp_dev->media_dev.dev = &pdev->dev;
	isp_dev->media_dev.ops = &rkisp_media_ops;

	if (!is_iommu_enable(dev)) {
		ret = of_reserved_mem_device_init(dev);
		if (ret) {
			dev_err(dev, "No reserved memory region\n");
			return ret;
		}
	}

	v4l2_dev = &isp_dev->v4l2_dev;
	v4l2_dev->mdev = &isp_dev->media_dev;
	strlcpy(v4l2_dev->name, DRIVER_NAME, sizeof(v4l2_dev->name));
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
	ret = rkisp_register_platform_subdevs(isp_dev);
	if (ret < 0)
		goto err_unreg_media_dev;

	pm_runtime_enable(&pdev->dev);
	if (isp_dev->is_thunderboot)
		pm_runtime_get_sync(&pdev->dev);

	ret = rkisp_vs_irq_parse(pdev);
	if (ret)
		goto err_runtime_disable;

	mutex_lock(&rkisp_dev_mutex);
	list_add_tail(&isp_dev->list, &rkisp_device_list);
	mutex_unlock(&rkisp_dev_mutex);
	return 0;

err_runtime_disable:
	pm_runtime_disable(&pdev->dev);
err_unreg_media_dev:
	media_device_unregister(&isp_dev->media_dev);
err_unreg_v4l2_dev:
	v4l2_device_unregister(&isp_dev->v4l2_dev);

	return ret;
}

static int rkisp_plat_remove(struct platform_device *pdev)
{
	struct rkisp_device *isp_dev = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	media_device_unregister(&isp_dev->media_dev);
	v4l2_device_unregister(&isp_dev->v4l2_dev);
	rkisp_unregister_luma_vdev(&isp_dev->luma_vdev);
	rkisp_unregister_params_vdev(&isp_dev->params_vdev);
	rkisp_unregister_stats_vdev(&isp_dev->stats_vdev);
	rkisp_unregister_stream_vdevs(isp_dev);
	rkisp_unregister_bridge_subdev(isp_dev);
	rkisp_unregister_csi_subdev(isp_dev);
	rkisp_unregister_isp_subdev(isp_dev);
	media_device_cleanup(&isp_dev->media_dev);

	return 0;
}

static int __maybe_unused rkisp_runtime_suspend(struct device *dev)
{
	struct rkisp_device *isp_dev = dev_get_drvdata(dev);
	int ret = 0;

	if (isp_dev->isp_ver == ISP_V12 || isp_dev->isp_ver == ISP_V13) {
		if (isp_dev->mipi_irq >= 0)
			disable_irq(isp_dev->mipi_irq);
	}

	rkisp_disable_sys_clk(isp_dev);
	ret = pinctrl_pm_select_sleep_state(dev);

	return ret;
}

static int __maybe_unused rkisp_runtime_resume(struct device *dev)
{
	struct rkisp_device *isp_dev = dev_get_drvdata(dev);
	int ret;

	ret = pinctrl_pm_select_default_state(dev);
	if (ret < 0)
		return ret;
	rkisp_enable_sys_clk(isp_dev);

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
static int __init rkisp_clr_unready_dev(void)
{
	__rkisp_clr_unready_dev();

	return 0;
}
late_initcall_sync(rkisp_clr_unready_dev);
#endif

static const struct dev_pm_ops rkisp_plat_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(rkisp_runtime_suspend, rkisp_runtime_resume, NULL)
};

static struct platform_driver rkisp_plat_drv = {
	.driver = {
		   .name = DRIVER_NAME,
		   .of_match_table = of_match_ptr(rkisp_plat_of_match),
		   .pm = &rkisp_plat_pm_ops,
	},
	.probe = rkisp_plat_probe,
	.remove = rkisp_plat_remove,
};

#if IS_BUILTIN(CONFIG_VIDEO_ROCKCHIP_ISP) && IS_BUILTIN(CONFIG_VIDEO_ROCKCHIP_ISPP)
static int __init rkisp_plat_drv_init(void)
{
	int ret;

	ret = platform_driver_register(&rkisp_plat_drv);
	if (ret)
		return ret;
	return rkispp_plat_drv_init();
}

module_init(rkisp_plat_drv_init);
#else
module_platform_driver(rkisp_plat_drv);
#endif
MODULE_AUTHOR("Rockchip Camera/ISP team");
MODULE_DESCRIPTION("Rockchip ISP platform driver");
MODULE_LICENSE("Dual BSD/GPL");
