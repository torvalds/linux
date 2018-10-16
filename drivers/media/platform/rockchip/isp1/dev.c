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
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <media/videobuf2-dma-contig.h>
#include <linux/dma-iommu.h>
#include <dt-bindings/soc/rockchip-system-status.h>
#include <soc/rockchip/rockchip-system-status.h>
#include "regs.h"
#include "rkisp1.h"
#include "common.h"
#include "regs.h"

struct isp_match_data {
	const char * const *clks;
	int num_clks;
	enum rkisp1_isp_ver isp_ver;
	const unsigned int *clk_rate_tbl;
	int num_clk_rate_tbl;
};

int rkisp1_debug;
module_param_named(debug, rkisp1_debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

/**************************** pipeline operations *****************************/

static int __isp_pipeline_prepare(struct rkisp1_pipeline *p,
				  struct media_entity *me)
{
	struct rkisp1_device *dev = container_of(p, struct rkisp1_device, pipe);
	struct v4l2_subdev *sd;
	int i;

	p->num_subdevs = 0;
	memset(p->subdevs, 0, sizeof(p->subdevs));

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
	return 0;
}

static int __subdev_set_power(struct v4l2_subdev *sd, int on)
{
	int ret;

	if (!sd)
		return -ENXIO;

	ret = v4l2_subdev_call(sd, core, s_power, on);

	return ret != -ENOIOCTLCMD ? ret : 0;
}

static int __isp_pipeline_s_power(struct rkisp1_pipeline *p, bool on)
{
	struct rkisp1_device *dev = container_of(p, struct rkisp1_device, pipe);
	int i, ret;

	if (on) {
		__subdev_set_power(&dev->isp_sdev.sd, true);

		for (i = p->num_subdevs - 1; i >= 0; --i) {
			ret = __subdev_set_power(p->subdevs[i], true);
			if (ret < 0 && ret != -ENXIO)
				goto err_power_off;
		}
	} else {
		for (i = 0; i < p->num_subdevs; ++i)
			__subdev_set_power(p->subdevs[i], false);

		__subdev_set_power(&dev->isp_sdev.sd, false);
	}

	return 0;

err_power_off:
	for (++i; i < p->num_subdevs; ++i)
		__subdev_set_power(p->subdevs[i], false);
	__subdev_set_power(&dev->isp_sdev.sd, true);
	return ret;
}

static int __isp_pipeline_s_isp_clk(struct rkisp1_pipeline *p)
{
	struct rkisp1_device *dev = container_of(p, struct rkisp1_device, pipe);
	struct v4l2_subdev *sd;
	struct v4l2_ctrl *ctrl;
	u64 data_rate;
	int i;

	/* find the subdev of active sensor */
	sd = p->subdevs[0];
	for (i = 0; i < p->num_subdevs; i++) {
		sd = p->subdevs[i];
		if (sd->entity.type == MEDIA_ENT_T_V4L2_SUBDEV_SENSOR)
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
	if (prepare)
		__isp_pipeline_prepare(p, me);

	if (!p->num_subdevs)
		return -EINVAL;

	ret = __isp_pipeline_s_isp_clk(p);
	if (ret < 0)
		return ret;

	ret = __isp_pipeline_s_power(p, 1);
	if (ret < 0)
		return ret;

	return 0;
}

static int rkisp1_pipeline_close(struct rkisp1_pipeline *p)
{
	int ret;

	if (atomic_dec_return(&p->power_cnt) > 0)
		return 0;
	ret = __isp_pipeline_s_power(p, 0);

	return ret == -ENXIO ? 0 : ret;
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

		ret = media_entity_create_link(
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
	ret = media_entity_create_link(source, 0, sink,
				       RKISP1_ISP_PAD_SINK_PARAMS, flags);
	if (ret < 0)
		return ret;

	/* create isp internal links */
	/* SP links */
	source = &dev->isp_sdev.sd.entity;
	sink = &dev->stream[RKISP1_STREAM_SP].vnode.vdev.entity;
	ret = media_entity_create_link(source, RKISP1_ISP_PAD_SOURCE_PATH,
				       sink, 0, flags);
	if (ret < 0)
		return ret;

	/* MP links */
	source = &dev->isp_sdev.sd.entity;
	sink = &dev->stream[RKISP1_STREAM_MP].vnode.vdev.entity;
	ret = media_entity_create_link(source, RKISP1_ISP_PAD_SOURCE_PATH,
				       sink, 0, flags);
	if (ret < 0)
		return ret;

	/* 3A stats links */
	source = &dev->isp_sdev.sd.entity;
	sink = &dev->stats_vdev.vnode.vdev.entity;
	return media_entity_create_link(source, RKISP1_ISP_PAD_SOURCE_STATS,
					sink, 0, flags);
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

	dev->alloc_ctx = vb2_dma_contig_init_ctx(dev->v4l2_dev.dev);

	ret = rkisp1_register_isp_subdev(dev, &dev->v4l2_dev);
	if (ret < 0)
		goto err_cleanup_ctx;

	ret = rkisp1_register_stream_vdevs(dev);
	if (ret < 0)
		goto err_unreg_isp_subdev;

	ret = rkisp1_register_stats_vdev(&dev->stats_vdev, &dev->v4l2_dev, dev);
	if (ret < 0)
		goto err_unreg_stream_vdev;

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
err_unreg_stream_vdev:
	rkisp1_unregister_stream_vdevs(dev);
err_unreg_isp_subdev:
	rkisp1_unregister_isp_subdev(dev);
err_cleanup_ctx:
	vb2_dma_contig_cleanup_ctx(dev->alloc_ctx);

	return ret;
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
	400, 500, 600
};

/* isp clock adjustment table (MHz) */
static const unsigned int rk3288_isp_clk_rate[] = {
	384, 500, 594
};

/* isp clock adjustment table (MHz) */
static const unsigned int rk3326_isp_clk_rate[] = {
	300, 347, 400, 520, 600
};

/* isp clock adjustment table (MHz) */
static const unsigned int rk3399_isp_clk_rate[] = {
	300, 400, 600
};

static const struct isp_match_data rk1808_isp_match_data = {
	.clks = rk1808_isp_clks,
	.num_clks = ARRAY_SIZE(rk1808_isp_clks),
	.isp_ver = ISP_V13,
	.clk_rate_tbl = rk1808_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rk1808_isp_clk_rate),
};

static const struct isp_match_data rk3288_isp_match_data = {
	.clks = rk3288_isp_clks,
	.num_clks = ARRAY_SIZE(rk3288_isp_clks),
	.isp_ver = ISP_V10,
	.clk_rate_tbl = rk3288_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rk3288_isp_clk_rate),
};

static const struct isp_match_data rk3326_isp_match_data = {
	.clks = rk3326_isp_clks,
	.num_clks = ARRAY_SIZE(rk3326_isp_clks),
	.isp_ver = ISP_V12,
	.clk_rate_tbl = rk3326_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rk3326_isp_clk_rate),
};

static const struct isp_match_data rk3399_isp_match_data = {
	.clks = rk3399_isp_clks,
	.num_clks = ARRAY_SIZE(rk3399_isp_clks),
	.isp_ver = ISP_V10,
	.clk_rate_tbl = rk3399_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rk3399_isp_clk_rate),
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
		.compatible = "rockchip,rk3399-rkisp1",
		.data = &rk3399_isp_match_data,
	},
	{},
};

static irqreturn_t rkisp1_irq_handler(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp1_device *rkisp1_dev = dev_get_drvdata(dev);
	unsigned int mis_val;
	unsigned int err1, err2, err3;

	mis_val = readl(rkisp1_dev->base_addr + CIF_ISP_MIS);
	if (mis_val)
		rkisp1_isp_isr(mis_val, rkisp1_dev);

	if (rkisp1_dev->isp_ver == ISP_V13) {
		err1 = readl(rkisp1_dev->base_addr + CIF_ISP_CSI0_ERR1);
		err2 = readl(rkisp1_dev->base_addr + CIF_ISP_CSI0_ERR2);
		err3 = readl(rkisp1_dev->base_addr + CIF_ISP_CSI0_ERR3);

		if (err1 || err2 || err3)
			rkisp1_mipi_v13_isr(err1, err2, err3, rkisp1_dev);
	} else {
		mis_val = readl(rkisp1_dev->base_addr + CIF_MIPI_MIS);
		if (mis_val)
			rkisp1_mipi_isr(mis_val, rkisp1_dev);
	}

	mis_val = readl(rkisp1_dev->base_addr + CIF_MI_MIS);
	if (mis_val)
		rkisp1_mi_isr(mis_val, rkisp1_dev);

	return IRQ_HANDLED;
}

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

static int rkisp1_iommu_init(struct rkisp1_device *rkisp1_dev)
{
	struct iommu_group *group;
	int ret;

	rkisp1_dev->domain = iommu_domain_alloc(&platform_bus_type);
	if (!rkisp1_dev->domain) {
		ret = -ENOMEM;
		goto err;
	}

	ret = iommu_get_dma_cookie(rkisp1_dev->domain);
	if (ret)
		goto err;

	group = iommu_group_get(rkisp1_dev->dev);
	if (!group) {
		group = iommu_group_alloc();
		if (IS_ERR(group))
			goto err;
		ret = iommu_group_add_device(group, rkisp1_dev->dev);
		iommu_group_put(group);
		if (ret)
			goto err;
	}

	return 0;

err:
	dev_err(rkisp1_dev->dev, "Failed to setup IOMMU\n");

	return ret;
}

static void rkisp1_iommu_cleanup(struct rkisp1_device *rkisp1_dev)
{
	iommu_domain_free(rkisp1_dev->domain);
}

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

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, rkisp1_irq_handler, IRQF_SHARED,
			       dev_driver_string(dev), dev);
	if (ret < 0) {
		dev_err(dev, "request irq failed: %d\n", ret);
		return ret;
	}

	isp_dev->irq = irq;
	match_data = match->data;
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

	atomic_set(&isp_dev->pipe.power_cnt, 0);
	atomic_set(&isp_dev->pipe.stream_cnt, 0);
	atomic_set(&isp_dev->open_cnt, 0);
	isp_dev->pipe.open = rkisp1_pipeline_open;
	isp_dev->pipe.close = rkisp1_pipeline_close;
	isp_dev->pipe.set_stream = rkisp1_pipeline_set_stream;

	rkisp1_stream_init(isp_dev, RKISP1_STREAM_SP);
	rkisp1_stream_init(isp_dev, RKISP1_STREAM_MP);

	strlcpy(isp_dev->media_dev.model, "rkisp1",
		sizeof(isp_dev->media_dev.model));
	isp_dev->media_dev.dev = &pdev->dev;
	v4l2_dev = &isp_dev->v4l2_dev;
	v4l2_dev->mdev = &isp_dev->media_dev;
	strlcpy(v4l2_dev->name, "rkisp1", sizeof(v4l2_dev->name));
	v4l2_ctrl_handler_init(&isp_dev->ctrl_handler, 5);
	v4l2_dev->ctrl_handler = &isp_dev->ctrl_handler;

	ret = v4l2_device_register(isp_dev->dev, &isp_dev->v4l2_dev);
	if (ret < 0)
		return ret;

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

	rkisp1_iommu_init(isp_dev);
	pm_runtime_enable(&pdev->dev);

	return 0;

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
	rkisp1_iommu_cleanup(isp_dev);

	media_device_unregister(&isp_dev->media_dev);
	v4l2_device_unregister(&isp_dev->v4l2_dev);
	rkisp1_unregister_params_vdev(&isp_dev->params_vdev);
	rkisp1_unregister_stats_vdev(&isp_dev->stats_vdev);
	rkisp1_unregister_stream_vdevs(isp_dev);
	rkisp1_unregister_isp_subdev(isp_dev);
	vb2_dma_contig_cleanup_ctx(isp_dev->alloc_ctx);

	return 0;
}

static int __maybe_unused rkisp1_runtime_suspend(struct device *dev)
{
	struct rkisp1_device *isp_dev = dev_get_drvdata(dev);

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

	return 0;
}

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
