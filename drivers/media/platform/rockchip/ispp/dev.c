// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019 Rockchip Electronics Co., Ltd */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/regmap.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-fwnode.h>

#include "dev.h"
#include "regs.h"
#include "version.h"

#define RKISPP_VERNO_LEN 10

struct ispp_irqs_data {
	const char *name;
	irqreturn_t (*irq_hdl)(int irq, void *ctx);
};

struct ispp_match_data {
	int clks_num;
	const char * const *clks;
	enum rkispp_ver ispp_ver;
	struct ispp_irqs_data *irqs;
	int num_irqs;
};

int rkispp_debug;
module_param_named(debug, rkispp_debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-3)");

static int rkisp_ispp_mode = ISP_ISPP_422 | ISP_ISPP_FBC;
module_param_named(mode, rkisp_ispp_mode, int, 0644);
MODULE_PARM_DESC(mode, "isp->ispp mode: bit0 fbc, bit1 yuv422, bit2 quick");

static bool rkispp_stream_sync;
module_param_named(stream_sync, rkispp_stream_sync, bool, 0644);
MODULE_PARM_DESC(stream_sync, "rkispp stream sync output");

static char rkispp_version[RKISPP_VERNO_LEN];
module_param_string(version, rkispp_version, RKISPP_VERNO_LEN, 0444);
MODULE_PARM_DESC(version, "version number");

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

static void get_remote_node_dev(struct rkispp_device *ispp_dev)
{
	struct device *dev = ispp_dev->dev;
	struct device_node *parent = dev->of_node;
	struct platform_device *remote_dev = NULL;
	struct device_node *remote = NULL;
	struct v4l2_subdev *sd = NULL;
	int i;

	for (i = 0; i < 2; i++) {
		remote = of_graph_get_remote_node(parent, 0, i);
		if (!remote)
			continue;
		remote_dev = of_find_device_by_node(remote);
		of_node_put(remote);
		if (!remote_dev) {
			dev_err(dev, "Failed to get remote device(%s)\n",
				of_node_full_name(remote));
			continue;
		} else {
			rkisp_get_bridge_sd(remote_dev, &sd);
			if (!sd) {
				dev_err(dev, "Failed to get isp bridge sd\n");
			} else {
				ispp_dev->ispp_sdev.remote_sd = sd;
				v4l2_set_subdev_hostdata(sd, &ispp_dev->ispp_sdev.sd);
				break;
			}
		}
	}
}

static int rkispp_create_links(struct rkispp_device *ispp_dev)
{
	struct rkispp_stream_vdev *stream_vdev;
	struct media_entity *source, *sink;
	struct rkispp_stream *stream;
	unsigned int flags = 0;
	int ret;

	stream_vdev = &ispp_dev->stream_vdev;
	stream = &stream_vdev->stream[STREAM_II];
	/* input stream links */
	sink = &ispp_dev->ispp_sdev.sd.entity;
	get_remote_node_dev(ispp_dev);
	if (ispp_dev->ispp_sdev.remote_sd) {
		ispp_dev->inp = INP_ISP;
	} else {
		flags = MEDIA_LNK_FL_ENABLED;
		ispp_dev->inp = INP_DDR;
		stream->linked = true;
	}
	source = &stream->vnode.vdev.entity;
	ret = media_create_pad_link(source, 0, sink,
				    RKISPP_PAD_SINK, flags);
	if (ret < 0)
		return ret;

	/* params links */
	flags = MEDIA_LNK_FL_ENABLED;
	source = &ispp_dev->params_vdev.vnode.vdev.entity;
	ret = media_create_pad_link(source, 0, sink,
				    RKISPP_PAD_SINK_PARAMS, flags);
	if (ret < 0)
		return ret;

	/* stats links */
	flags = MEDIA_LNK_FL_ENABLED;
	source = &ispp_dev->ispp_sdev.sd.entity;
	sink = &ispp_dev->stats_vdev.vnode.vdev.entity;
	ret = media_create_pad_link(source, RKISPP_PAD_SOURCE_STATS,
				    sink, 0, flags);
	if (ret < 0)
		return ret;

	/* output stream links */
	flags = rkispp_stream_sync ? 0 : MEDIA_LNK_FL_ENABLED;
	stream = &stream_vdev->stream[STREAM_MB];
	stream->linked = flags;
	source = &ispp_dev->ispp_sdev.sd.entity;
	sink = &stream->vnode.vdev.entity;
	ret = media_create_pad_link(source, RKISPP_PAD_SOURCE,
				    sink, 0, flags);
	if (ret < 0)
		return ret;

	stream = &stream_vdev->stream[STREAM_S0];
	stream->linked = flags;
	sink = &stream->vnode.vdev.entity;
	ret = media_create_pad_link(source, RKISPP_PAD_SOURCE,
				    sink, 0, flags);
	if (ret < 0)
		return ret;

	stream = &stream_vdev->stream[STREAM_S1];
	stream->linked = flags;
	sink = &stream->vnode.vdev.entity;
	ret = media_create_pad_link(source, RKISPP_PAD_SOURCE,
				    sink, 0, flags);
	if (ret < 0)
		return ret;

	stream = &stream_vdev->stream[STREAM_S2];
	stream->linked = flags;
	sink = &stream->vnode.vdev.entity;
	ret = media_create_pad_link(source, RKISPP_PAD_SOURCE,
				    sink, 0, flags);
	if (ret < 0)
		return ret;

	/* default enable tnr (2to1), nr, sharp */
	ispp_dev->stream_vdev.module_ens =
		ISPP_MODULE_TNR | ISPP_MODULE_NR | ISPP_MODULE_SHP;
	return 0;
}

static int rkispp_register_platform_subdevs(struct rkispp_device *ispp_dev)
{
	int ret;

	ret = rkispp_register_stream_vdevs(ispp_dev);
	if (ret < 0)
		return ret;

	ret = rkispp_register_params_vdev(ispp_dev);
	if (ret < 0)
		goto err_unreg_stream_vdevs;

	ret = rkispp_register_stats_vdev(ispp_dev);
	if (ret < 0)
		goto err_unreg_params_vdev;

	ret = rkispp_register_subdev(ispp_dev, &ispp_dev->v4l2_dev);
	if (ret < 0)
		goto err_unreg_stats_vdev;

	ret = rkispp_create_links(ispp_dev);
	if (ret < 0)
		goto err_unreg_ispp_subdev;
	return ret;
err_unreg_ispp_subdev:
	rkispp_unregister_subdev(ispp_dev);
err_unreg_stats_vdev:
	rkispp_unregister_stats_vdev(ispp_dev);
err_unreg_params_vdev:
	rkispp_unregister_params_vdev(ispp_dev);
err_unreg_stream_vdevs:
	rkispp_unregister_stream_vdevs(ispp_dev);
	return ret;
}

static void rkispp_disable_sys_clk(struct rkispp_device *ispp_dev)
{
	int i;

	for (i = 0; i < ispp_dev->clks_num; i++)
		clk_disable_unprepare(ispp_dev->clks[i]);
}

static int rkispp_enable_sys_clk(struct rkispp_device *ispp_dev)
{
	int i, ret = -EINVAL;

	ispp_dev->isp_mode = rkisp_ispp_mode;
	ispp_dev->stream_sync = rkispp_stream_sync;
	for (i = 0; i < ispp_dev->clks_num; i++) {
		ret = clk_prepare_enable(ispp_dev->clks[i]);
		if (ret < 0)
			goto err;
	}

	return 0;
err:
	for (--i; i >= 0; --i)
		clk_disable_unprepare(ispp_dev->clks[i]);
	return ret;
}

static irqreturn_t rkispp_irq_hdl(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkispp_device *ispp_dev = dev_get_drvdata(dev);
	void __iomem *base = ispp_dev->base_addr;
	unsigned int mis_val;

	spin_lock(&ispp_dev->irq_lock);
	mis_val = readl(base + RKISPP_CTRL_INT_STA);
	writel(mis_val, base + RKISPP_CTRL_INT_CLR);
	spin_unlock(&ispp_dev->irq_lock);

	if (mis_val)
		rkispp_isr(mis_val, ispp_dev);

	return IRQ_HANDLED;
}

static const char * const rv1126_ispp_clks[] = {
	"aclk_ispp",
	"hclk_ispp",
	"clk_ispp",
};

static struct ispp_irqs_data rv1126_ispp_irqs[] = {
	{"ispp_irq", rkispp_irq_hdl},
	{"fec_irq", rkispp_irq_hdl},
};

static const struct ispp_match_data rv1126_ispp_match_data = {
	.clks = rv1126_ispp_clks,
	.clks_num = ARRAY_SIZE(rv1126_ispp_clks),
	.irqs = rv1126_ispp_irqs,
	.num_irqs = ARRAY_SIZE(rv1126_ispp_irqs),
	.ispp_ver = ISPP_V10,
};

static const struct of_device_id rkispp_plat_of_match[] = {
	{
		.compatible = "rockchip,rv1126-rkispp",
		.data = &rv1126_ispp_match_data,
	},
	{},
};

static int rkispp_plat_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct ispp_match_data *match_data;
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct v4l2_device *v4l2_dev;
	struct rkispp_device *ispp_dev;
	struct resource *res;
	int i, ret, irq;

	sprintf(rkispp_version, "v%02x.%02x.%02x",
		RKISPP_DRIVER_VERSION >> 16,
		(RKISPP_DRIVER_VERSION & 0xff00) >> 8,
		RKISPP_DRIVER_VERSION & 0x00ff);

	dev_info(dev, "rkispp driver version: %s\n", rkispp_version);

	match = of_match_node(rkispp_plat_of_match, node);
	if (IS_ERR(match))
		return PTR_ERR(match);

	ispp_dev = devm_kzalloc(dev, sizeof(*ispp_dev), GFP_KERNEL);
	if (!ispp_dev)
		return -ENOMEM;

	dev_set_drvdata(dev, ispp_dev);
	ispp_dev->dev = dev;
	match_data = match->data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "get resource failed\n");
		return -EINVAL;
	}
	ispp_dev->base_addr = devm_ioremap_resource(dev, res);
	if (PTR_ERR(ispp_dev->base_addr) == -EBUSY) {
		resource_size_t offset = res->start;
		resource_size_t size = resource_size(res);

		ispp_dev->base_addr = devm_ioremap(dev, offset, size);
	}
	if (IS_ERR(ispp_dev->base_addr)) {
		dev_err(dev, "ioremap failed\n");
		return PTR_ERR(ispp_dev->base_addr);
	}

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
			ret = devm_request_irq(dev, irq,
					       match_data->irqs[i].irq_hdl,
					       IRQF_SHARED,
					       dev_driver_string(dev),
					       dev);
			if (ret < 0) {
				dev_err(dev, "request %s failed: %d\n",
					match_data->irqs[i].name, ret);
				return ret;
			}
		}
	}

	for (i = 0; i < match_data->clks_num; i++) {
		struct clk *clk = devm_clk_get(dev, match_data->clks[i]);

		if (IS_ERR(clk))
			dev_warn(dev, "failed to get %s\n",
				 match_data->clks[i]);
		ispp_dev->clks[i] = clk;
	}
	ispp_dev->clks_num = match_data->clks_num;
	ispp_dev->ispp_ver = match_data->ispp_ver;

	mutex_init(&ispp_dev->apilock);
	mutex_init(&ispp_dev->iqlock);
	spin_lock_init(&ispp_dev->irq_lock);
	init_waitqueue_head(&ispp_dev->sync_onoff);

	strlcpy(ispp_dev->media_dev.model, "rkispp",
		sizeof(ispp_dev->media_dev.model));
	ispp_dev->media_dev.dev = &pdev->dev;
	v4l2_dev = &ispp_dev->v4l2_dev;
	v4l2_dev->mdev = &ispp_dev->media_dev;
	strlcpy(v4l2_dev->name, "rkispp", sizeof(v4l2_dev->name));
	v4l2_ctrl_handler_init(&ispp_dev->ctrl_handler, 5);
	v4l2_dev->ctrl_handler = &ispp_dev->ctrl_handler;

	ret = v4l2_device_register(ispp_dev->dev, v4l2_dev);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "register v4l2 device failed:%d\n", ret);
		return ret;
	}
	media_device_init(&ispp_dev->media_dev);
	ret = media_device_register(&ispp_dev->media_dev);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "register media device failed:%d\n", ret);
		goto err_unreg_v4l2_dev;
	}

	ret = rkispp_register_platform_subdevs(ispp_dev);
	if (ret < 0)
		goto err_unreg_media_dev;

	if (!is_iommu_enable(dev)) {
		ret = of_reserved_mem_device_init(dev);
		if (ret)
			v4l2_warn(v4l2_dev,
				  "No reserved memory region assign to ispp\n");
	}

	pm_runtime_enable(&pdev->dev);

	return 0;

err_unreg_media_dev:
	media_device_unregister(&ispp_dev->media_dev);
err_unreg_v4l2_dev:
	v4l2_device_unregister(&ispp_dev->v4l2_dev);
	return ret;
}

static int rkispp_plat_remove(struct platform_device *pdev)
{
	struct rkispp_device *ispp_dev = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	rkispp_unregister_subdev(ispp_dev);
	rkispp_unregister_stats_vdev(ispp_dev);
	rkispp_unregister_params_vdev(ispp_dev);
	rkispp_unregister_stream_vdevs(ispp_dev);

	media_device_unregister(&ispp_dev->media_dev);
	v4l2_device_unregister(&ispp_dev->v4l2_dev);
	mutex_destroy(&ispp_dev->apilock);
	return 0;
}

static int __maybe_unused rkispp_runtime_suspend(struct device *dev)
{
	struct rkispp_device *ispp_dev = dev_get_drvdata(dev);

	rkispp_disable_sys_clk(ispp_dev);
	return 0;
}

static int __maybe_unused rkispp_runtime_resume(struct device *dev)
{
	struct rkispp_device *ispp_dev = dev_get_drvdata(dev);

	rkispp_enable_sys_clk(ispp_dev);
	return 0;
}

static const struct dev_pm_ops rkispp_plat_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(rkispp_runtime_suspend,
			   rkispp_runtime_resume, NULL)
};

static struct platform_driver rkispp_plat_drv = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(rkispp_plat_of_match),
		.pm = &rkispp_plat_pm_ops,
	},
	.probe = rkispp_plat_probe,
	.remove = rkispp_plat_remove,
};

#if IS_BUILTIN(CONFIG_VIDEO_ROCKCHIP_ISP) && IS_BUILTIN(CONFIG_VIDEO_ROCKCHIP_ISPP)
int __init rkispp_plat_drv_init(void)
{
	return platform_driver_register(&rkispp_plat_drv);
}
#else
module_platform_driver(rkispp_plat_drv);
#endif
MODULE_AUTHOR("Rockchip Camera/ISP team");
MODULE_DESCRIPTION("Rockchip ISPP platform driver");
MODULE_LICENSE("GPL v2");
