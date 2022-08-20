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

int rkisp_debug;
module_param_named(debug, rkisp_debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

bool rkisp_monitor;
module_param_named(monitor, rkisp_monitor, bool, 0644);
MODULE_PARM_DESC(monitor, "rkisp abnormal restart monitor");

bool rkisp_irq_dbg;
module_param_named(irq_dbg, rkisp_irq_dbg, bool, 0644);
MODULE_PARM_DESC(irq_dbg, "rkisp interrupt runtime");

static bool rkisp_rdbk_auto;
module_param_named(rdbk_auto, rkisp_rdbk_auto, bool, 0644);
MODULE_PARM_DESC(irq_dbg, "rkisp and vicap auto readback mode");

static bool rkisp_clk_dbg;
module_param_named(clk_dbg, rkisp_clk_dbg, bool, 0644);
MODULE_PARM_DESC(clk_dbg, "rkisp clk set by user");

static char rkisp_version[RKISP_VERNO_LEN];
module_param_string(version, rkisp_version, RKISP_VERNO_LEN, 0444);
MODULE_PARM_DESC(version, "version number");

u64 rkisp_debug_reg = 0xFFFFFFFFFLL;
module_param_named(debug_reg, rkisp_debug_reg, ullong, 0644);
MODULE_PARM_DESC(debug_reg, "rkisp debug register");

static unsigned int rkisp_wait_line;
module_param_named(wait_line, rkisp_wait_line, uint, 0644);
MODULE_PARM_DESC(wait_line, "rkisp wait line to buf done early");

static unsigned int rkisp_wrap_line;
module_param_named(wrap_line, rkisp_wrap_line, uint, 0644);
MODULE_PARM_DESC(wrap_line, "rkisp wrap line for mpp");

static DEFINE_MUTEX(rkisp_dev_mutex);
static LIST_HEAD(rkisp_device_list);

void rkisp_set_clk_rate(struct clk *clk, unsigned long rate)
{
	if (rkisp_clk_dbg)
		return;

	clk_set_rate(clk, rate);
}

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

	if (!(dev->isp_inp & (INP_CSI | INP_DVP | INP_LVDS | INP_CIF)))
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
	struct rkisp_hw_dev *hw_dev = dev->hw_dev;
	u32 w = hw_dev->max_in.w ? hw_dev->max_in.w : dev->isp_sdev.in_frm.width;
	struct v4l2_subdev *sd;
	struct v4l2_ctrl *ctrl;
	u64 data_rate;
	int i;

	if (dev->isp_inp & (INP_RAWRD0 | INP_RAWRD1 | INP_RAWRD2)) {
		for (i = 0; i < hw_dev->num_clk_rate_tbl; i++) {
			if (w <= hw_dev->clk_rate_tbl[i].refer_data)
				break;
		}
		if (!hw_dev->is_single)
			i++;

		/* use lager clk in 4 vir-isp mode */
		if (hw_dev->dev_num >= 4)
			i++;

		if (i > hw_dev->num_clk_rate_tbl - 1)
			i = hw_dev->num_clk_rate_tbl - 1;
		goto end;
	}

	if (dev->isp_inp == INP_DMARX_ISP && dev->hw_dev->clks[0]) {
		rkisp_set_clk_rate(hw_dev->clks[0], 400 * 1000000UL);
		return 0;
	}

	/* find the subdev of active sensor or vicap itf */
	sd = p->subdevs[0];
	for (i = 0; i < p->num_subdevs; i++) {
		sd = p->subdevs[i];
		if (sd->entity.function == MEDIA_ENT_F_CAM_SENSOR ||
		    sd->entity.function == MEDIA_ENT_F_PROC_VIDEO_COMPOSER)
			break;
	}

	if (i == p->num_subdevs) {
		v4l2_warn(&dev->v4l2_dev, "No active sensor\n");
		return -EPIPE;
	}

	ctrl = v4l2_ctrl_find(sd->ctrl_handler, V4L2_CID_PIXEL_RATE);
	if (!ctrl) {
		v4l2_warn(&dev->v4l2_dev, "No pixel rate control in subdev\n");
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
	for (i = 0; i < hw_dev->num_clk_rate_tbl; i++)
		if (data_rate <= hw_dev->clk_rate_tbl[i].clk_rate)
			break;
	if (i == hw_dev->num_clk_rate_tbl)
		i--;
end:
	/* set isp clock rate */
	rkisp_set_clk_rate(hw_dev->clks[0], hw_dev->clk_rate_tbl[i].clk_rate * 1000000UL);
	if (hw_dev->is_unite)
		rkisp_set_clk_rate(hw_dev->clks[5], hw_dev->clk_rate_tbl[i].clk_rate * 1000000UL);
	/* aclk equal to core clk */
	if (dev->isp_ver == ISP_V32)
		rkisp_set_clk_rate(hw_dev->clks[1], hw_dev->clk_rate_tbl[i].clk_rate * 1000000UL);
	dev_info(hw_dev->dev, "set isp clk = %luHz\n", clk_get_rate(hw_dev->clks[0]));

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

	if (dev->isp_inp & (INP_CSI | INP_RAWRD0 | INP_RAWRD1 | INP_RAWRD2 | INP_CIF))
		rkisp_csi_config_patch(dev);
	return 0;
}

static int rkisp_pipeline_close(struct rkisp_pipeline *p)
{
	struct rkisp_device *dev = container_of(p, struct rkisp_device, pipe);

	atomic_dec(&p->power_cnt);

	if (!atomic_read(&p->power_cnt) &&
	    (dev->isp_ver == ISP_V30 || dev->isp_ver == ISP_V32))
		rkisp_rx_buf_pool_free(dev);

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
		ret = v4l2_subdev_call(&dev->isp_sdev.sd, video, s_stream, true);
		if (ret < 0)
			goto err;
		/* phy -> sensor */
		for (i = 0; i < p->num_subdevs; ++i) {
			if ((dev->vicap_in.merge_num > 1) &&
			    (p->subdevs[i]->entity.function == MEDIA_ENT_F_CAM_SENSOR))
				continue;
			ret = v4l2_subdev_call(p->subdevs[i], video, s_stream, on);
			if (on && ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
				goto err_stream_off;
		}
	} else {
		/* sensor -> phy */
		for (i = p->num_subdevs - 1; i >= 0; --i) {
			if ((dev->vicap_in.merge_num > 1) &&
			    (p->subdevs[i]->entity.function == MEDIA_ENT_F_CAM_SENSOR))
				continue;
			v4l2_subdev_call(p->subdevs[i], video, s_stream, on);
		}
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
err:
	rockchip_clear_system_status(SYS_STATUS_ISP);
	atomic_dec_return(&p->stream_cnt);
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
		bool en = s ? 0 : true;

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
		} else if (type == MEDIA_ENT_F_PROC_VIDEO_COMPOSER) {
			dev->isp_inp = INP_CIF;
			ret = media_create_pad_link(&sensor->sd->entity, pad,
				&dev->isp_sdev.sd.entity, RKISP_ISP_PAD_SINK, en);
		} else {
			v4l2_subdev_call(sensor->sd, pad,
					 get_mbus_config, 0, &sensor->mbus);
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

static int _set_pipeline_default_fmt(struct rkisp_device *dev, bool is_init)
{
	struct v4l2_subdev *isp;
	struct v4l2_subdev_format fmt;
	struct v4l2_subdev_selection sel;
	u32 i, width, height, code;

	memset(&sel, 0, sizeof(sel));
	memset(&fmt, 0, sizeof(fmt));
	isp = &dev->isp_sdev.sd;

	if (dev->active_sensor) {
		fmt = dev->active_sensor->fmt[0];
		if (!is_init &&
		    fmt.format.code == dev->isp_sdev.in_frm.code &&
		    fmt.format.width == dev->isp_sdev.in_frm.width &&
		    fmt.format.height == dev->isp_sdev.in_frm.height)
			return 0;
	} else {
		fmt.format = dev->isp_sdev.in_frm;
	}
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
				 width, height, V4L2_PIX_FMT_NV12);
	if (dev->isp_ver != ISP_V10_1)
		rkisp_set_stream_def_fmt(dev, RKISP_STREAM_SP,
					 width, height, V4L2_PIX_FMT_NV12);
	if ((dev->isp_ver == ISP_V20 || dev->isp_ver == ISP_V21) &&
	    dev->isp_inp == INP_CSI && dev->active_sensor) {
		width = dev->active_sensor->fmt[1].format.width;
		height = dev->active_sensor->fmt[1].format.height;
		code = dev->active_sensor->fmt[1].format.code;
		rkisp_set_stream_def_fmt(dev, RKISP_STREAM_DMATX0,
			width, height, rkisp_mbus_pixelcode_to_v4l2(code));

		width = dev->active_sensor->fmt[3].format.width;
		height = dev->active_sensor->fmt[3].format.height;
		code = dev->active_sensor->fmt[3].format.code;
		rkisp_set_stream_def_fmt(dev, RKISP_STREAM_DMATX2,
			width, height, rkisp_mbus_pixelcode_to_v4l2(code));

		width = dev->active_sensor->fmt[4].format.width;
		height = dev->active_sensor->fmt[4].format.height;
		code = dev->active_sensor->fmt[4].format.code;
		rkisp_set_stream_def_fmt(dev, RKISP_STREAM_DMATX3,
			width, height, rkisp_mbus_pixelcode_to_v4l2(code));
	}

	if (dev->isp_ver == ISP_V20 &&
	    dev->isp_inp == INP_CSI && dev->active_sensor) {
		width = dev->active_sensor->fmt[2].format.width;
		height = dev->active_sensor->fmt[2].format.height;
		code = dev->active_sensor->fmt[2].format.code;
		rkisp_set_stream_def_fmt(dev, RKISP_STREAM_DMATX1,
			width, height, rkisp_mbus_pixelcode_to_v4l2(code));
	}

	if (dev->isp_ver == ISP_V30) {
		struct v4l2_pix_format_mplane pixm = {
			.width = width,
			.height = height,
			.pixelformat = rkisp_mbus_pixelcode_to_v4l2(code),
		};

		for (i = RKISP_STREAM_RAWRD0; i <= RKISP_STREAM_RAWRD2; i++)
			rkisp_dmarx_set_fmt(&dev->dmarx_dev.stream[i], pixm);
		rkisp_set_stream_def_fmt(dev, RKISP_STREAM_FBC,
					 width, height, V4L2_PIX_FMT_FBC0);
#ifdef RKISP_STREAM_BP_EN
		rkisp_set_stream_def_fmt(dev, RKISP_STREAM_BP,
					 width, height, V4L2_PIX_FMT_NV12);
#endif
	}

	if (dev->isp_ver == ISP_V32) {
		struct v4l2_pix_format_mplane pixm = {
			.width = width,
			.height = height,
			.pixelformat = rkisp_mbus_pixelcode_to_v4l2(code),
		};

		rkisp_dmarx_set_fmt(&dev->dmarx_dev.stream[RKISP_STREAM_RAWRD0], pixm);
		rkisp_dmarx_set_fmt(&dev->dmarx_dev.stream[RKISP_STREAM_RAWRD2], pixm);
		rkisp_set_stream_def_fmt(dev, RKISP_STREAM_BP,
					 width, height, V4L2_PIX_FMT_NV12);
		rkisp_set_stream_def_fmt(dev, RKISP_STREAM_MPDS,
					 width / 4, height / 4, V4L2_PIX_FMT_NV12);
		rkisp_set_stream_def_fmt(dev, RKISP_STREAM_BPDS,
					 width / 4, height / 4, V4L2_PIX_FMT_NV12);
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

	if (dev->isp_inp) {
		ret = rkisp_update_sensor_info(dev);
		if (ret < 0) {
			v4l2_err(&dev->v4l2_dev, "update sensor failed\n");
			goto unlock;
		}
		dev->is_hw_link = true;
	}

	ret = _set_pipeline_default_fmt(dev, true);
	if (ret < 0)
		goto unlock;

	v4l2_info(&dev->v4l2_dev, "Async subdev notifier completed\n");

unlock:
	mutex_unlock(&dev->media_dev.graph_mutex);
	if (!ret && dev->is_thunderboot)
		schedule_work(&dev->cap_dev.fast_work);
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

static void subdev_notifier_unbind(struct v4l2_async_notifier *notifier,
				   struct v4l2_subdev *subdev,
				   struct v4l2_async_subdev *asd)
{
	struct rkisp_device *isp_dev = container_of(notifier, struct rkisp_device, notifier);
	struct rkisp_isp_subdev *isp_sdev = &isp_dev->isp_sdev;
	struct v4l2_subdev *isp_sd = &isp_sdev->sd;
	int i;

	for (i = 0; i < isp_dev->num_sensors; i++) {
		if (isp_dev->sensors[i].sd == subdev) {
			media_entity_call(&isp_sd->entity, link_setup,
				isp_sd->entity.pads, subdev->entity.pads, 0);
			isp_dev->sensors[i].sd = NULL;
		}
	}
}

static const struct v4l2_async_notifier_operations subdev_notifier_ops = {
	.bound = subdev_notifier_bound,
	.complete = subdev_notifier_complete,
	.unbind = subdev_notifier_unbind,
};

static int isp_subdev_notifier(struct rkisp_device *isp_dev)
{
	struct v4l2_async_notifier *ntf = &isp_dev->notifier;
	struct device *dev = isp_dev->dev;
	int ret;

	v4l2_async_notifier_init(ntf);

	ret = v4l2_async_notifier_parse_fwnode_endpoints(
		dev, ntf, sizeof(struct rkisp_async_subdev),
		rkisp_fwnode_parse);
	if (ret < 0)
		return ret;

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

static int rkisp_vs_irq_parse(struct device *dev)
{
	int ret;
	int vs_irq;
	unsigned long vs_irq_flags;
	struct gpio_desc *vs_irq_gpio;
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
		dev_info(dev, "No memory-region-thunderboot specified\n");
		return 0;
	}

	ret = of_address_to_resource(np, 0, &r);
	if (ret) {
		dev_err(dev, "No memory address assigned to the region\n");
		return ret;
	}

	isp_dev->resmem_pa = r.start;
	isp_dev->resmem_size = resource_size(&r);
	isp_dev->resmem_addr = dma_map_single(dev, phys_to_virt(r.start),
					      sizeof(struct rkisp_thunderboot_resmem_head),
					      DMA_BIDIRECTIONAL);
	ret = dma_mapping_error(dev, isp_dev->resmem_addr);
	isp_dev->is_thunderboot = true;
	dev_info(dev, "Allocated reserved memory, paddr: 0x%x\n", (u32)isp_dev->resmem_pa);
	return ret;
}

static int rkisp_plat_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct v4l2_device *v4l2_dev;
	struct rkisp_device *isp_dev;
	int i, ret, mult = 1;

	snprintf(rkisp_version, sizeof(rkisp_version),
		 "v%02x.%02x.%02x",
		 RKISP_DRIVER_VERSION >> 16,
		 (RKISP_DRIVER_VERSION & 0xff00) >> 8,
		 RKISP_DRIVER_VERSION & 0x00ff);

	dev_info(dev, "rkisp driver version: %s\n", rkisp_version);

	isp_dev = devm_kzalloc(dev, sizeof(*isp_dev), GFP_KERNEL);
	if (!isp_dev)
		return -ENOMEM;

	dev_set_drvdata(dev, isp_dev);
	isp_dev->dev = dev;
	ret = rkisp_attach_hw(isp_dev);
	if (ret)
		return ret;

	if (isp_dev->hw_dev->is_unite)
		mult = 2;
	isp_dev->sw_base_addr = devm_kzalloc(dev, RKISP_ISP_SW_MAX_SIZE * mult, GFP_KERNEL);
	if (!isp_dev->sw_base_addr)
		return -ENOMEM;

	ret = rkisp_vs_irq_parse(dev);
	if (ret)
		return ret;

	snprintf(isp_dev->media_dev.model, sizeof(isp_dev->media_dev.model),
		 "%s%d", DRIVER_NAME, isp_dev->dev_id);
	if (!isp_dev->hw_dev->is_unite)
		strscpy(isp_dev->name, dev_name(dev), sizeof(isp_dev->name));
	else
		snprintf(isp_dev->name, sizeof(isp_dev->name),
			 "%s%d", "rkisp-unite", isp_dev->dev_id);
	strscpy(isp_dev->media_dev.driver_name, isp_dev->name,
		sizeof(isp_dev->media_dev.driver_name));

	if (isp_dev->hw_dev->is_thunderboot) {
		ret = rkisp_get_reserved_mem(isp_dev);
		if (ret)
			return ret;
	}

	mutex_init(&isp_dev->apilock);
	mutex_init(&isp_dev->iqlock);
	atomic_set(&isp_dev->pipe.power_cnt, 0);
	atomic_set(&isp_dev->pipe.stream_cnt, 0);
	init_waitqueue_head(&isp_dev->sync_onoff);
	isp_dev->pipe.open = rkisp_pipeline_open;
	isp_dev->pipe.close = rkisp_pipeline_close;
	isp_dev->pipe.set_stream = rkisp_pipeline_set_stream;

	if (isp_dev->isp_ver == ISP_V20 || isp_dev->isp_ver == ISP_V21) {
		atomic_set(&isp_dev->hdr.refcnt, 0);
		for (i = 0; i < HDR_DMA_MAX; i++) {
			INIT_LIST_HEAD(&isp_dev->hdr.q_tx[i]);
			INIT_LIST_HEAD(&isp_dev->hdr.q_rx[i]);
		}
	}

	isp_dev->media_dev.dev = dev;
	isp_dev->media_dev.ops = &rkisp_media_ops;

	v4l2_dev = &isp_dev->v4l2_dev;
	v4l2_dev->mdev = &isp_dev->media_dev;
	strlcpy(v4l2_dev->name, isp_dev->name, sizeof(v4l2_dev->name));
	v4l2_ctrl_handler_init(&isp_dev->ctrl_handler, 5);
	v4l2_dev->ctrl_handler = &isp_dev->ctrl_handler;

	ret = v4l2_device_register(isp_dev->dev, &isp_dev->v4l2_dev);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "Failed to register v4l2 device:%d\n", ret);
		return ret;
	}

	media_device_init(&isp_dev->media_dev);
	ret = media_device_register(&isp_dev->media_dev);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "Failed to register media device:%d\n", ret);
		goto err_unreg_v4l2_dev;
	}

	pm_runtime_enable(dev);
	/* create & register platefom subdev (from of_node) */
	ret = rkisp_register_platform_subdevs(isp_dev);
	if (ret < 0)
		goto err_unreg_media_dev;

	rkisp_wait_line = 0;
	of_property_read_u32(dev->of_node, "wait-line", &rkisp_wait_line);

	rkisp_proc_init(isp_dev);

	mutex_lock(&rkisp_dev_mutex);
	list_add_tail(&isp_dev->list, &rkisp_device_list);
	mutex_unlock(&rkisp_dev_mutex);
	return 0;

err_unreg_media_dev:
	media_device_unregister(&isp_dev->media_dev);
err_unreg_v4l2_dev:
	v4l2_device_unregister(&isp_dev->v4l2_dev);
	return ret;
}

static int rkisp_plat_remove(struct platform_device *pdev)
{
	struct rkisp_device *isp_dev = platform_get_drvdata(pdev);

	isp_dev->is_hw_link = false;
	isp_dev->hw_dev->isp[isp_dev->dev_id] = NULL;

	pm_runtime_disable(&pdev->dev);

	rkisp_proc_cleanup(isp_dev);
	media_device_unregister(&isp_dev->media_dev);
	v4l2_async_notifier_unregister(&isp_dev->notifier);
	v4l2_async_notifier_cleanup(&isp_dev->notifier);
	v4l2_device_unregister(&isp_dev->v4l2_dev);
	v4l2_ctrl_handler_free(&isp_dev->ctrl_handler);
	rkisp_unregister_luma_vdev(&isp_dev->luma_vdev);
	rkisp_unregister_params_vdev(&isp_dev->params_vdev);
	rkisp_unregister_stats_vdev(&isp_dev->stats_vdev);
	rkisp_unregister_dmarx_vdev(isp_dev);
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
	int ret;

	mutex_lock(&isp_dev->hw_dev->dev_lock);
	ret = pm_runtime_put_sync(isp_dev->hw_dev->dev);
	mutex_unlock(&isp_dev->hw_dev->dev_lock);
	return (ret > 0) ? 0 : ret;
}

static int __maybe_unused rkisp_runtime_resume(struct device *dev)
{
	struct rkisp_device *isp_dev = dev_get_drvdata(dev);
	int ret;

	/* power on to config default format from sensor */
	if (isp_dev->isp_inp & (INP_CSI | INP_DVP | INP_LVDS | INP_CIF) &&
	    rkisp_update_sensor_info(isp_dev) >= 0)
		_set_pipeline_default_fmt(isp_dev, false);

	isp_dev->cap_dev.wait_line = rkisp_wait_line;
	isp_dev->cap_dev.wrap_line = rkisp_wrap_line;
	isp_dev->is_rdbk_auto = rkisp_rdbk_auto;
	mutex_lock(&isp_dev->hw_dev->dev_lock);
	ret = pm_runtime_get_sync(isp_dev->hw_dev->dev);
	mutex_unlock(&isp_dev->hw_dev->dev_lock);
	return (ret > 0) ? 0 : ret;
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

static const struct of_device_id rkisp_plat_of_match[] = {
	{
		.compatible = "rockchip,rkisp-vir",
	}, {
		.compatible = "rockchip,rv1126-rkisp-vir",
	},
	{},
};

struct platform_driver rkisp_plat_drv = {
	.driver = {
		   .name = DRIVER_NAME,
		   .of_match_table = of_match_ptr(rkisp_plat_of_match),
		   .pm = &rkisp_plat_pm_ops,
	},
	.probe = rkisp_plat_probe,
	.remove = rkisp_plat_remove,
};

MODULE_AUTHOR("Rockchip Camera/ISP team");
MODULE_DESCRIPTION("Rockchip ISP platform driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
