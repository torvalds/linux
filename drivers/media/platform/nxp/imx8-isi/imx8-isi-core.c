// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019-2020 NXP
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include <media/media-device.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mc.h>

#include "imx8-isi-core.h"

/* -----------------------------------------------------------------------------
 * V4L2 async subdevs
 */

struct mxc_isi_async_subdev {
	struct v4l2_async_subdev asd;
	unsigned int port;
};

static inline struct mxc_isi_async_subdev *
asd_to_mxc_isi_async_subdev(struct v4l2_async_subdev *asd)
{
	return container_of(asd, struct mxc_isi_async_subdev, asd);
};

static inline struct mxc_isi_dev *
notifier_to_mxc_isi_dev(struct v4l2_async_notifier *n)
{
	return container_of(n, struct mxc_isi_dev, notifier);
};

static int mxc_isi_async_notifier_bound(struct v4l2_async_notifier *notifier,
					struct v4l2_subdev *sd,
					struct v4l2_async_subdev *asd)
{
	const unsigned int link_flags = MEDIA_LNK_FL_IMMUTABLE
				      | MEDIA_LNK_FL_ENABLED;
	struct mxc_isi_dev *isi = notifier_to_mxc_isi_dev(notifier);
	struct mxc_isi_async_subdev *masd = asd_to_mxc_isi_async_subdev(asd);
	struct media_pad *pad = &isi->crossbar.pads[masd->port];
	struct device_link *link;

	dev_dbg(isi->dev, "Bound subdev %s to crossbar input %u\n", sd->name,
		masd->port);

	/*
	 * Enforce suspend/resume ordering between the source (supplier) and
	 * the ISI (consumer). The source will be suspended before and resume
	 * after the ISI.
	 */
	link = device_link_add(isi->dev, sd->dev, DL_FLAG_STATELESS);
	if (!link) {
		dev_err(isi->dev,
			"Failed to create device link to source %s\n", sd->name);
		return -EINVAL;
	}

	return v4l2_create_fwnode_links_to_pad(sd, pad, link_flags);
}

static int mxc_isi_async_notifier_complete(struct v4l2_async_notifier *notifier)
{
	struct mxc_isi_dev *isi = notifier_to_mxc_isi_dev(notifier);
	int ret;

	dev_dbg(isi->dev, "All subdevs bound\n");

	ret = v4l2_device_register_subdev_nodes(&isi->v4l2_dev);
	if (ret < 0) {
		dev_err(isi->dev,
			"Failed to register subdev nodes: %d\n", ret);
		return ret;
	}

	return media_device_register(&isi->media_dev);
}

static const struct v4l2_async_notifier_operations mxc_isi_async_notifier_ops = {
	.bound = mxc_isi_async_notifier_bound,
	.complete = mxc_isi_async_notifier_complete,
};

static int mxc_isi_pipe_register(struct mxc_isi_pipe *pipe)
{
	int ret;

	ret = v4l2_device_register_subdev(&pipe->isi->v4l2_dev, &pipe->sd);
	if (ret < 0)
		return ret;

	return mxc_isi_video_register(pipe, &pipe->isi->v4l2_dev);
}

static void mxc_isi_pipe_unregister(struct mxc_isi_pipe *pipe)
{
	mxc_isi_video_unregister(pipe);
}

static int mxc_isi_v4l2_init(struct mxc_isi_dev *isi)
{
	struct fwnode_handle *node = dev_fwnode(isi->dev);
	struct media_device *media_dev = &isi->media_dev;
	struct v4l2_device *v4l2_dev = &isi->v4l2_dev;
	unsigned int i;
	int ret;

	/* Initialize the media device. */
	strscpy(media_dev->model, "FSL Capture Media Device",
		sizeof(media_dev->model));
	media_dev->dev = isi->dev;

	media_device_init(media_dev);

	/* Initialize and register the V4L2 device. */
	v4l2_dev->mdev = media_dev;
	strscpy(v4l2_dev->name, "mx8-img-md", sizeof(v4l2_dev->name));

	ret = v4l2_device_register(isi->dev, v4l2_dev);
	if (ret < 0) {
		dev_err(isi->dev,
			"Failed to register V4L2 device: %d\n", ret);
		goto err_media;
	}

	/* Register the crossbar switch subdev. */
	ret = mxc_isi_crossbar_register(&isi->crossbar);
	if (ret < 0) {
		dev_err(isi->dev, "Failed to register crossbar: %d\n", ret);
		goto err_v4l2;
	}

	/* Register the pipeline subdevs and link them to the crossbar switch. */
	for (i = 0; i < isi->pdata->num_channels; ++i) {
		struct mxc_isi_pipe *pipe = &isi->pipes[i];

		ret = mxc_isi_pipe_register(pipe);
		if (ret < 0) {
			dev_err(isi->dev, "Failed to register pipe%u: %d\n", i,
				ret);
			goto err_v4l2;
		}

		ret = media_create_pad_link(&isi->crossbar.sd.entity,
					    isi->crossbar.num_sinks + i,
					    &pipe->sd.entity,
					    MXC_ISI_PIPE_PAD_SINK,
					    MEDIA_LNK_FL_IMMUTABLE |
					    MEDIA_LNK_FL_ENABLED);
		if (ret < 0)
			goto err_v4l2;
	}

	/* Register the M2M device. */
	ret = mxc_isi_m2m_register(isi, v4l2_dev);
	if (ret < 0) {
		dev_err(isi->dev, "Failed to register M2M device: %d\n", ret);
		goto err_v4l2;
	}

	/* Initialize, fill and register the async notifier. */
	v4l2_async_nf_init(&isi->notifier);
	isi->notifier.ops = &mxc_isi_async_notifier_ops;

	for (i = 0; i < isi->pdata->num_ports; ++i) {
		struct mxc_isi_async_subdev *masd;
		struct fwnode_handle *ep;

		ep = fwnode_graph_get_endpoint_by_id(node, i, 0,
						     FWNODE_GRAPH_ENDPOINT_NEXT);

		if (!ep)
			continue;

		masd = v4l2_async_nf_add_fwnode_remote(&isi->notifier, ep,
						       struct mxc_isi_async_subdev);
		fwnode_handle_put(ep);

		if (IS_ERR(masd)) {
			ret = PTR_ERR(masd);
			goto err_m2m;
		}

		masd->port = i;
	}

	ret = v4l2_async_nf_register(v4l2_dev, &isi->notifier);
	if (ret < 0) {
		dev_err(isi->dev,
			"Failed to register async notifier: %d\n", ret);
		goto err_m2m;
	}

	return 0;

err_m2m:
	mxc_isi_m2m_unregister(isi);
	v4l2_async_nf_cleanup(&isi->notifier);
err_v4l2:
	v4l2_device_unregister(v4l2_dev);
err_media:
	media_device_cleanup(media_dev);
	return ret;
}

static void mxc_isi_v4l2_cleanup(struct mxc_isi_dev *isi)
{
	unsigned int i;

	v4l2_async_nf_unregister(&isi->notifier);
	v4l2_async_nf_cleanup(&isi->notifier);

	v4l2_device_unregister(&isi->v4l2_dev);
	media_device_unregister(&isi->media_dev);

	mxc_isi_m2m_unregister(isi);

	for (i = 0; i < isi->pdata->num_channels; ++i)
		mxc_isi_pipe_unregister(&isi->pipes[i]);

	mxc_isi_crossbar_unregister(&isi->crossbar);

	media_device_cleanup(&isi->media_dev);
}

/* -----------------------------------------------------------------------------
 * Device information
 */

/* Panic will assert when the buffers are 50% full */

/* For i.MX8QXP C0 and i.MX8MN ISI IER version */
static const struct mxc_isi_ier_reg mxc_imx8_isi_ier_v1 = {
	.oflw_y_buf_en = { .offset = 19, .mask = 0x80000  },
	.oflw_u_buf_en = { .offset = 21, .mask = 0x200000 },
	.oflw_v_buf_en = { .offset = 23, .mask = 0x800000 },

	.panic_y_buf_en = {.offset = 20, .mask = 0x100000  },
	.panic_u_buf_en = {.offset = 22, .mask = 0x400000  },
	.panic_v_buf_en = {.offset = 24, .mask = 0x1000000 },
};

/* For i.MX8MP ISI IER version */
static const struct mxc_isi_ier_reg mxc_imx8_isi_ier_v2 = {
	.oflw_y_buf_en = { .offset = 18, .mask = 0x40000  },
	.oflw_u_buf_en = { .offset = 20, .mask = 0x100000 },
	.oflw_v_buf_en = { .offset = 22, .mask = 0x400000 },

	.panic_y_buf_en = {.offset = 19, .mask = 0x80000  },
	.panic_u_buf_en = {.offset = 21, .mask = 0x200000 },
	.panic_v_buf_en = {.offset = 23, .mask = 0x800000 },
};

/* Panic will assert when the buffers are 50% full */
static const struct mxc_isi_set_thd mxc_imx8_isi_thd_v1 = {
	.panic_set_thd_y = { .mask = 0x0000f, .offset = 0,  .threshold = 0x7 },
	.panic_set_thd_u = { .mask = 0x00f00, .offset = 8,  .threshold = 0x7 },
	.panic_set_thd_v = { .mask = 0xf0000, .offset = 16, .threshold = 0x7 },
};

static const struct clk_bulk_data mxc_imx8mn_clks[] = {
	{ .id = "axi" },
	{ .id = "apb" },
};

static const struct mxc_isi_plat_data mxc_imx8mn_data = {
	.model			= MXC_ISI_IMX8MN,
	.num_ports		= 1,
	.num_channels		= 1,
	.reg_offset		= 0,
	.ier_reg		= &mxc_imx8_isi_ier_v1,
	.set_thd		= &mxc_imx8_isi_thd_v1,
	.clks			= mxc_imx8mn_clks,
	.num_clks		= ARRAY_SIZE(mxc_imx8mn_clks),
	.buf_active_reverse	= false,
	.has_gasket		= true,
	.has_36bit_dma		= false,
};

static const struct mxc_isi_plat_data mxc_imx8mp_data = {
	.model			= MXC_ISI_IMX8MP,
	.num_ports		= 2,
	.num_channels		= 2,
	.reg_offset		= 0x2000,
	.ier_reg		= &mxc_imx8_isi_ier_v2,
	.set_thd		= &mxc_imx8_isi_thd_v1,
	.clks			= mxc_imx8mn_clks,
	.num_clks		= ARRAY_SIZE(mxc_imx8mn_clks),
	.buf_active_reverse	= true,
	.has_gasket		= true,
	.has_36bit_dma		= true,
};

/* -----------------------------------------------------------------------------
 * Power management
 */

static int mxc_isi_pm_suspend(struct device *dev)
{
	struct mxc_isi_dev *isi = dev_get_drvdata(dev);
	unsigned int i;

	for (i = 0; i < isi->pdata->num_channels; ++i) {
		struct mxc_isi_pipe *pipe = &isi->pipes[i];

		mxc_isi_video_suspend(pipe);
	}

	return pm_runtime_force_suspend(dev);
}

static int mxc_isi_pm_resume(struct device *dev)
{
	struct mxc_isi_dev *isi = dev_get_drvdata(dev);
	unsigned int i;
	int err = 0;
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret < 0)
		return ret;

	for (i = 0; i < isi->pdata->num_channels; ++i) {
		struct mxc_isi_pipe *pipe = &isi->pipes[i];

		ret = mxc_isi_video_resume(pipe);
		if (ret) {
			dev_err(dev, "Failed to resume pipeline %u (%d)\n", i,
				ret);
			/*
			 * Record the last error as it's as meaningful as any,
			 * and continue resuming the other pipelines.
			 */
			err = ret;
		}
	}

	return err;
}

static int mxc_isi_runtime_suspend(struct device *dev)
{
	struct mxc_isi_dev *isi = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(isi->pdata->num_clks, isi->clks);

	return 0;
}

static int mxc_isi_runtime_resume(struct device *dev)
{
	struct mxc_isi_dev *isi = dev_get_drvdata(dev);
	int ret;

	ret = clk_bulk_prepare_enable(isi->pdata->num_clks, isi->clks);
	if (ret) {
		dev_err(dev, "Failed to enable clocks (%d)\n", ret);
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops mxc_isi_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(mxc_isi_pm_suspend, mxc_isi_pm_resume)
	RUNTIME_PM_OPS(mxc_isi_runtime_suspend, mxc_isi_runtime_resume, NULL)
};

/* -----------------------------------------------------------------------------
 * Probe, remove & driver
 */

static int mxc_isi_clk_get(struct mxc_isi_dev *isi)
{
	unsigned int size = isi->pdata->num_clks
			  * sizeof(*isi->clks);
	int ret;

	isi->clks = devm_kmalloc(isi->dev, size, GFP_KERNEL);
	if (!isi->clks)
		return -ENOMEM;

	memcpy(isi->clks, isi->pdata->clks, size);

	ret = devm_clk_bulk_get(isi->dev, isi->pdata->num_clks,
				isi->clks);
	if (ret < 0) {
		dev_err(isi->dev, "Failed to acquire clocks: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int mxc_isi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mxc_isi_dev *isi;
	unsigned int dma_size;
	unsigned int i;
	int ret = 0;

	isi = devm_kzalloc(dev, sizeof(*isi), GFP_KERNEL);
	if (!isi)
		return -ENOMEM;

	isi->dev = dev;
	platform_set_drvdata(pdev, isi);

	isi->pdata = of_device_get_match_data(dev);

	isi->pipes = kcalloc(isi->pdata->num_channels, sizeof(isi->pipes[0]),
			     GFP_KERNEL);
	if (!isi->pipes)
		return -ENOMEM;

	ret = mxc_isi_clk_get(isi);
	if (ret < 0) {
		dev_err(dev, "Failed to get clocks\n");
		return ret;
	}

	isi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(isi->regs)) {
		dev_err(dev, "Failed to get ISI register map\n");
		return PTR_ERR(isi->regs);
	}

	if (isi->pdata->has_gasket) {
		isi->gasket = syscon_regmap_lookup_by_phandle(dev->of_node,
							      "fsl,blk-ctrl");
		if (IS_ERR(isi->gasket)) {
			ret = PTR_ERR(isi->gasket);
			dev_err(dev, "failed to get gasket: %d\n", ret);
			return ret;
		}
	}

	dma_size = isi->pdata->has_36bit_dma ? 36 : 32;
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(dma_size));
	if (ret) {
		dev_err(dev, "failed to set DMA mask\n");
		return ret;
	}

	pm_runtime_enable(dev);

	ret = mxc_isi_crossbar_init(isi);
	if (ret) {
		dev_err(dev, "Failed to initialize crossbar: %d\n", ret);
		goto err_pm;
	}

	for (i = 0; i < isi->pdata->num_channels; ++i) {
		ret = mxc_isi_pipe_init(isi, i);
		if (ret < 0) {
			dev_err(dev, "Failed to initialize pipe%u: %d\n", i,
				ret);
			goto err_xbar;
		}
	}

	ret = mxc_isi_v4l2_init(isi);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize V4L2: %d\n", ret);
		goto err_xbar;
	}

	mxc_isi_debug_init(isi);

	return 0;

err_xbar:
	mxc_isi_crossbar_cleanup(&isi->crossbar);
err_pm:
	pm_runtime_disable(isi->dev);
	return ret;
}

static int mxc_isi_remove(struct platform_device *pdev)
{
	struct mxc_isi_dev *isi = platform_get_drvdata(pdev);
	unsigned int i;

	mxc_isi_debug_cleanup(isi);

	for (i = 0; i < isi->pdata->num_channels; ++i) {
		struct mxc_isi_pipe *pipe = &isi->pipes[i];

		mxc_isi_pipe_cleanup(pipe);
	}

	mxc_isi_crossbar_cleanup(&isi->crossbar);
	mxc_isi_v4l2_cleanup(isi);

	pm_runtime_disable(isi->dev);

	return 0;
}

static const struct of_device_id mxc_isi_of_match[] = {
	{ .compatible = "fsl,imx8mn-isi", .data = &mxc_imx8mn_data },
	{ .compatible = "fsl,imx8mp-isi", .data = &mxc_imx8mp_data },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mxc_isi_of_match);

static struct platform_driver mxc_isi_driver = {
	.probe		= mxc_isi_probe,
	.remove		= mxc_isi_remove,
	.driver = {
		.of_match_table = mxc_isi_of_match,
		.name		= MXC_ISI_DRIVER_NAME,
		.pm		= pm_ptr(&mxc_isi_pm_ops),
	}
};
module_platform_driver(mxc_isi_driver);

MODULE_ALIAS("ISI");
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("IMX8 Image Sensing Interface driver");
MODULE_LICENSE("GPL");
