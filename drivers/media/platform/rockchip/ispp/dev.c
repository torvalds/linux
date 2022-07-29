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

int rkispp_debug;
module_param_named(debug, rkispp_debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-3)");

static bool rkispp_clk_dbg;
module_param_named(clk_dbg, rkispp_clk_dbg, bool, 0644);
MODULE_PARM_DESC(clk_dbg, "rkispp clk set by user");

bool rkispp_monitor;
module_param_named(monitor, rkispp_monitor, bool, 0644);
MODULE_PARM_DESC(monitor, "rkispp abnormal restart monitor");

static int rkisp_ispp_mode = ISP_ISPP_FBC;
module_param_named(mode, rkisp_ispp_mode, int, 0644);
MODULE_PARM_DESC(mode, "isp->ispp mode: bit0 fbc, bit1 yuv422, bit2 quick");

static bool rkispp_stream_sync;
module_param_named(stream_sync, rkispp_stream_sync, bool, 0644);
MODULE_PARM_DESC(stream_sync, "rkispp stream sync output");

static char rkispp_version[RKISPP_VERNO_LEN];
module_param_string(version, rkispp_version, RKISPP_VERNO_LEN, 0444);
MODULE_PARM_DESC(version, "version number");

bool rkispp_reg_withstream;
module_param_named(sendreg_withstream, rkispp_reg_withstream, bool, 0644);
MODULE_PARM_DESC(sendreg_withstream, "rkispp send reg out with stream");

char rkispp_reg_withstream_video_name[RKISPP_VIDEO_NAME_LEN];
module_param_string(sendreg_withstream_video_name, rkispp_reg_withstream_video_name,
		    RKISPP_VIDEO_NAME_LEN, 0644);
MODULE_PARM_DESC(sendreg_withstream, "rkispp video send reg out with stream");

unsigned int rkispp_debug_reg = 0x1F;
module_param_named(debug_reg, rkispp_debug_reg, uint, 0644);
MODULE_PARM_DESC(debug_reg, "rkispp debug register");

static unsigned int rkispp_wait_line;
module_param_named(wait_line, rkispp_wait_line, uint, 0644);
MODULE_PARM_DESC(wait_line, "rkispp wait line to buf done early");

char rkispp_dump_path[128];
module_param_string(dump_path, rkispp_dump_path, sizeof(rkispp_dump_path), 0644);
MODULE_PARM_DESC(dump_path, "rkispp dump debug file path");

void rkispp_set_clk_rate(struct clk *clk, unsigned long rate)
{
	if (rkispp_clk_dbg)
		return;

	clk_set_rate(clk, rate);
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
				if (ispp_dev->hw_dev->max_in.w && ispp_dev->hw_dev->max_in.h)
					v4l2_subdev_call(sd, core, ioctl, RKISP_ISPP_CMD_SET_FMT,
							 &ispp_dev->hw_dev->max_in);
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
	ret = media_create_pad_link(source, 0, sink, RKISPP_PAD_SINK, flags);
	if (ret < 0)
		return ret;

	/* params links */
	flags = MEDIA_LNK_FL_ENABLED;
	source = &ispp_dev->params_vdev[PARAM_VDEV_FEC].vnode.vdev.entity;
	ret = media_create_pad_link(source, 0, sink, RKISPP_PAD_SINK_PARAMS, flags);
	if (ret < 0)
		return ret;
	ispp_dev->stream_vdev.module_ens = ISPP_MODULE_FEC;
	if (ispp_dev->ispp_ver == ISPP_V10) {
		/* params links */
		source = &ispp_dev->params_vdev[PARAM_VDEV_TNR].vnode.vdev.entity;
		ret = media_create_pad_link(source, 0, sink, RKISPP_PAD_SINK_PARAMS, flags);
		if (ret < 0)
			return ret;
		source = &ispp_dev->params_vdev[PARAM_VDEV_NR].vnode.vdev.entity;
		ret = media_create_pad_link(source, 0, sink, RKISPP_PAD_SINK_PARAMS, flags);
		if (ret < 0)
			return ret;

		/* stats links */
		source = &ispp_dev->ispp_sdev.sd.entity;
		sink = &ispp_dev->stats_vdev[STATS_VDEV_TNR].vnode.vdev.entity;
		ret = media_create_pad_link(source, RKISPP_PAD_SOURCE_STATS, sink, 0, flags);
		if (ret < 0)
			return ret;
		sink = &ispp_dev->stats_vdev[STATS_VDEV_NR].vnode.vdev.entity;
		ret = media_create_pad_link(source, RKISPP_PAD_SOURCE_STATS, sink, 0, flags);
		if (ret < 0)
			return ret;

		/* output stream links */
		stream = &stream_vdev->stream[STREAM_S0];
		stream->linked = flags;
		sink = &stream->vnode.vdev.entity;
		ret = media_create_pad_link(source, RKISPP_PAD_SOURCE, sink, 0, flags);
		if (ret < 0)
			return ret;

		stream = &stream_vdev->stream[STREAM_S1];
		stream->linked = flags;
		sink = &stream->vnode.vdev.entity;
		ret = media_create_pad_link(source, RKISPP_PAD_SOURCE, sink, 0, flags);
		if (ret < 0)
			return ret;

		stream = &stream_vdev->stream[STREAM_S2];
		stream->linked = flags;
		sink = &stream->vnode.vdev.entity;
		ret = media_create_pad_link(source, RKISPP_PAD_SOURCE, sink, 0, flags);
		if (ret < 0)
			return ret;

		ispp_dev->stream_vdev.module_ens = ISPP_MODULE_NR | ISPP_MODULE_SHP;
	}

	flags = rkispp_stream_sync ? 0 : MEDIA_LNK_FL_ENABLED;
	stream = &stream_vdev->stream[STREAM_MB];
	stream->linked = flags;
	source = &ispp_dev->ispp_sdev.sd.entity;
	sink = &stream->vnode.vdev.entity;
	ret = media_create_pad_link(source, RKISPP_PAD_SOURCE, sink, 0, flags);
	if (ret < 0)
		return ret;

	stream = &stream_vdev->stream[STREAM_VIR];
	stream->linked = flags;
	sink = &stream->vnode.vdev.entity;
	ret = media_create_pad_link(source, RKISPP_PAD_SOURCE, sink, 0, flags);
	if (ret < 0)
		return ret;

	return 0;
}

static int rkispp_register_platform_subdevs(struct rkispp_device *ispp_dev)
{
	int ret;

	ret = rkispp_register_stream_vdevs(ispp_dev);
	if (ret < 0)
		return ret;

	ret = rkispp_register_params_vdevs(ispp_dev);
	if (ret < 0)
		goto err_unreg_stream_vdevs;

	ret = rkispp_register_stats_vdevs(ispp_dev);
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
	rkispp_unregister_stats_vdevs(ispp_dev);
err_unreg_params_vdev:
	rkispp_unregister_params_vdevs(ispp_dev);
err_unreg_stream_vdevs:
	rkispp_unregister_stream_vdevs(ispp_dev);
	return ret;
}

static const struct of_device_id rkispp_plat_of_match[] = {
	{
		.compatible = "rockchip,rv1126-rkispp-vir",
	}, {
		.compatible = "rockchip,rk3588-rkispp-vir",
	},
	{},
};

static int rkispp_plat_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct v4l2_device *v4l2_dev;
	struct rkispp_device *ispp_dev;
	int ret;

	sprintf(rkispp_version, "v%02x.%02x.%02x",
		RKISPP_DRIVER_VERSION >> 16,
		(RKISPP_DRIVER_VERSION & 0xff00) >> 8,
		RKISPP_DRIVER_VERSION & 0x00ff);

	dev_info(dev, "rkispp driver version: %s\n", rkispp_version);

	ispp_dev = devm_kzalloc(dev, sizeof(*ispp_dev), GFP_KERNEL);
	if (!ispp_dev)
		return -ENOMEM;
	ispp_dev->sw_base_addr = devm_kzalloc(dev, RKISP_ISPP_SW_MAX_SIZE, GFP_KERNEL);
	if (!ispp_dev->sw_base_addr)
		return -ENOMEM;

	dev_set_drvdata(dev, ispp_dev);
	ispp_dev->dev = dev;

	ret = rkispp_attach_hw(ispp_dev);
	if (ret)
		return ret;

	sprintf(ispp_dev->media_dev.model, "%s%d",
		DRIVER_NAME, ispp_dev->dev_id);
	ispp_dev->irq_hdl = rkispp_isr;
	mutex_init(&ispp_dev->apilock);
	mutex_init(&ispp_dev->iqlock);
	init_waitqueue_head(&ispp_dev->sync_onoff);

	strscpy(ispp_dev->name, dev_name(dev), sizeof(ispp_dev->name));
	strscpy(ispp_dev->media_dev.driver_name, ispp_dev->name,
		sizeof(ispp_dev->media_dev.driver_name));
	ispp_dev->media_dev.dev = &pdev->dev;
	v4l2_dev = &ispp_dev->v4l2_dev;
	v4l2_dev->mdev = &ispp_dev->media_dev;
	strlcpy(v4l2_dev->name, ispp_dev->name, sizeof(v4l2_dev->name));
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

	rkispp_wait_line = 0;
	of_property_read_u32(pdev->dev.of_node, "wait-line",
			     &rkispp_wait_line);
	rkispp_proc_init(ispp_dev);
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

	rkispp_proc_cleanup(ispp_dev);
	rkispp_unregister_subdev(ispp_dev);
	rkispp_unregister_stats_vdevs(ispp_dev);
	rkispp_unregister_params_vdevs(ispp_dev);
	rkispp_unregister_stream_vdevs(ispp_dev);

	media_device_unregister(&ispp_dev->media_dev);
	v4l2_device_unregister(&ispp_dev->v4l2_dev);
	mutex_destroy(&ispp_dev->apilock);
	mutex_destroy(&ispp_dev->iqlock);
	return 0;
}

static int __maybe_unused rkispp_runtime_suspend(struct device *dev)
{
	struct rkispp_device *ispp_dev = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&ispp_dev->hw_dev->dev_lock);
	ret = pm_runtime_put_sync(ispp_dev->hw_dev->dev);
	mutex_unlock(&ispp_dev->hw_dev->dev_lock);
	return (ret > 0) ? 0 : ret;
}

static int __maybe_unused rkispp_runtime_resume(struct device *dev)
{
	struct rkispp_device *ispp_dev = dev_get_drvdata(dev);
	int ret;

	ispp_dev->isp_mode = rkisp_ispp_mode;
	ispp_dev->stream_sync = rkispp_stream_sync;
	ispp_dev->stream_vdev.monitor.is_en = rkispp_monitor;
	ispp_dev->stream_vdev.wait_line = rkispp_wait_line;

	mutex_lock(&ispp_dev->hw_dev->dev_lock);
	ret = pm_runtime_get_sync(ispp_dev->hw_dev->dev);
	mutex_unlock(&ispp_dev->hw_dev->dev_lock);
	return (ret > 0) ? 0 : ret;
}

static const struct dev_pm_ops rkispp_plat_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(rkispp_runtime_suspend,
			   rkispp_runtime_resume, NULL)
};

struct platform_driver rkispp_plat_drv = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(rkispp_plat_of_match),
		.pm = &rkispp_plat_pm_ops,
	},
	.probe = rkispp_plat_probe,
	.remove = rkispp_plat_remove,
};

MODULE_AUTHOR("Rockchip Camera/ISP team");
MODULE_DESCRIPTION("Rockchip ISPP platform driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
