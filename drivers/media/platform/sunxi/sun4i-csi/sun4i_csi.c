// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 NextThing Co
 * Copyright (C) 2016-2019 Bootlin
 *
 * Author: Maxime Ripard <maxime.ripard@bootlin.com>
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/videodev2.h>

#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mediabus.h>

#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "sun4i_csi.h"

struct sun4i_csi_traits {
	unsigned int channels;
	unsigned int max_width;
	bool has_isp;
};

static const struct media_entity_operations sun4i_csi_video_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct media_entity_operations sun4i_csi_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int sun4i_csi_notify_bound(struct v4l2_async_notifier *notifier,
				  struct v4l2_subdev *subdev,
				  struct v4l2_async_subdev *asd)
{
	struct sun4i_csi *csi = container_of(notifier, struct sun4i_csi,
					     notifier);

	csi->src_subdev = subdev;
	csi->src_pad = media_entity_get_fwnode_pad(&subdev->entity,
						   subdev->fwnode,
						   MEDIA_PAD_FL_SOURCE);
	if (csi->src_pad < 0) {
		dev_err(csi->dev, "Couldn't find output pad for subdev %s\n",
			subdev->name);
		return csi->src_pad;
	}

	dev_dbg(csi->dev, "Bound %s pad: %d\n", subdev->name, csi->src_pad);
	return 0;
}

static int sun4i_csi_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct sun4i_csi *csi = container_of(notifier, struct sun4i_csi,
					     notifier);
	struct v4l2_subdev *subdev = &csi->subdev;
	struct video_device *vdev = &csi->vdev;
	int ret;

	ret = v4l2_device_register_subdev(&csi->v4l, subdev);
	if (ret < 0)
		return ret;

	ret = sun4i_csi_v4l2_register(csi);
	if (ret < 0)
		return ret;

	ret = media_device_register(&csi->mdev);
	if (ret)
		return ret;

	/* Create link from subdev to main device */
	ret = media_create_pad_link(&subdev->entity, CSI_SUBDEV_SOURCE,
				    &vdev->entity, 0,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret)
		goto err_clean_media;

	ret = media_create_pad_link(&csi->src_subdev->entity, csi->src_pad,
				    &subdev->entity, CSI_SUBDEV_SINK,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret)
		goto err_clean_media;

	ret = v4l2_device_register_subdev_nodes(&csi->v4l);
	if (ret < 0)
		goto err_clean_media;

	return 0;

err_clean_media:
	media_device_unregister(&csi->mdev);

	return ret;
}

static const struct v4l2_async_notifier_operations sun4i_csi_notify_ops = {
	.bound		= sun4i_csi_notify_bound,
	.complete	= sun4i_csi_notify_complete,
};

static int sun4i_csi_notifier_init(struct sun4i_csi *csi)
{
	struct v4l2_fwnode_endpoint vep = {
		.bus_type = V4L2_MBUS_PARALLEL,
	};
	struct v4l2_async_subdev *asd;
	struct fwnode_handle *ep;
	int ret;

	v4l2_async_nf_init(&csi->notifier);

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(csi->dev), 0, 0,
					     FWNODE_GRAPH_ENDPOINT_NEXT);
	if (!ep)
		return -EINVAL;

	ret = v4l2_fwnode_endpoint_parse(ep, &vep);
	if (ret)
		goto out;

	csi->bus = vep.bus.parallel;

	asd = v4l2_async_nf_add_fwnode_remote(&csi->notifier, ep,
					      struct v4l2_async_subdev);
	if (IS_ERR(asd)) {
		ret = PTR_ERR(asd);
		goto out;
	}

	csi->notifier.ops = &sun4i_csi_notify_ops;

out:
	fwnode_handle_put(ep);
	return ret;
}

static int sun4i_csi_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct video_device *vdev;
	struct sun4i_csi *csi;
	int ret;
	int irq;

	csi = devm_kzalloc(&pdev->dev, sizeof(*csi), GFP_KERNEL);
	if (!csi)
		return -ENOMEM;
	platform_set_drvdata(pdev, csi);
	csi->dev = &pdev->dev;
	subdev = &csi->subdev;
	vdev = &csi->vdev;

	csi->traits = of_device_get_match_data(&pdev->dev);
	if (!csi->traits)
		return -EINVAL;

	csi->mdev.dev = csi->dev;
	strscpy(csi->mdev.model, "Allwinner Video Capture Device",
		sizeof(csi->mdev.model));
	csi->mdev.hw_revision = 0;
	media_device_init(&csi->mdev);
	csi->v4l.mdev = &csi->mdev;

	csi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(csi->regs))
		return PTR_ERR(csi->regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	csi->bus_clk = devm_clk_get(&pdev->dev, "bus");
	if (IS_ERR(csi->bus_clk)) {
		dev_err(&pdev->dev, "Couldn't get our bus clock\n");
		return PTR_ERR(csi->bus_clk);
	}

	if (csi->traits->has_isp) {
		csi->isp_clk = devm_clk_get(&pdev->dev, "isp");
		if (IS_ERR(csi->isp_clk)) {
			dev_err(&pdev->dev, "Couldn't get our ISP clock\n");
			return PTR_ERR(csi->isp_clk);
		}
	}

	csi->ram_clk = devm_clk_get(&pdev->dev, "ram");
	if (IS_ERR(csi->ram_clk)) {
		dev_err(&pdev->dev, "Couldn't get our ram clock\n");
		return PTR_ERR(csi->ram_clk);
	}

	csi->rst = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(csi->rst)) {
		dev_err(&pdev->dev, "Couldn't get our reset line\n");
		return PTR_ERR(csi->rst);
	}

	/* Initialize subdev */
	v4l2_subdev_init(subdev, &sun4i_csi_subdev_ops);
	subdev->flags = V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	subdev->entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	subdev->entity.ops = &sun4i_csi_subdev_entity_ops;
	subdev->owner = THIS_MODULE;
	snprintf(subdev->name, sizeof(subdev->name), "sun4i-csi-0");
	v4l2_set_subdevdata(subdev, csi);

	csi->subdev_pads[CSI_SUBDEV_SINK].flags =
		MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;
	csi->subdev_pads[CSI_SUBDEV_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&subdev->entity, CSI_SUBDEV_PADS,
				     csi->subdev_pads);
	if (ret < 0)
		return ret;

	csi->vdev_pad.flags = MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;
	vdev->entity.ops = &sun4i_csi_video_entity_ops;
	ret = media_entity_pads_init(&vdev->entity, 1, &csi->vdev_pad);
	if (ret < 0)
		return ret;

	ret = sun4i_csi_dma_register(csi, irq);
	if (ret)
		goto err_clean_pad;

	ret = sun4i_csi_notifier_init(csi);
	if (ret)
		goto err_unregister_media;

	ret = v4l2_async_nf_register(&csi->v4l, &csi->notifier);
	if (ret) {
		dev_err(csi->dev, "Couldn't register our notifier.\n");
		goto err_unregister_media;
	}

	pm_runtime_enable(&pdev->dev);

	return 0;

err_unregister_media:
	media_device_unregister(&csi->mdev);
	sun4i_csi_dma_unregister(csi);

err_clean_pad:
	media_device_cleanup(&csi->mdev);

	return ret;
}

static int sun4i_csi_remove(struct platform_device *pdev)
{
	struct sun4i_csi *csi = platform_get_drvdata(pdev);

	v4l2_async_nf_unregister(&csi->notifier);
	v4l2_async_nf_cleanup(&csi->notifier);
	vb2_video_unregister_device(&csi->vdev);
	media_device_unregister(&csi->mdev);
	sun4i_csi_dma_unregister(csi);
	media_device_cleanup(&csi->mdev);

	return 0;
}

static const struct sun4i_csi_traits sun4i_a10_csi1_traits = {
	.channels = 1,
	.max_width = 24,
	.has_isp = false,
};

static const struct sun4i_csi_traits sun7i_a20_csi0_traits = {
	.channels = 4,
	.max_width = 16,
	.has_isp = true,
};

static const struct of_device_id sun4i_csi_of_match[] = {
	{ .compatible = "allwinner,sun4i-a10-csi1", .data = &sun4i_a10_csi1_traits },
	{ .compatible = "allwinner,sun7i-a20-csi0", .data = &sun7i_a20_csi0_traits },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, sun4i_csi_of_match);

static int __maybe_unused sun4i_csi_runtime_resume(struct device *dev)
{
	struct sun4i_csi *csi = dev_get_drvdata(dev);

	reset_control_deassert(csi->rst);
	clk_prepare_enable(csi->bus_clk);
	clk_prepare_enable(csi->ram_clk);
	clk_set_rate(csi->isp_clk, 80000000);
	clk_prepare_enable(csi->isp_clk);

	writel(1, csi->regs + CSI_EN_REG);

	return 0;
}

static int __maybe_unused sun4i_csi_runtime_suspend(struct device *dev)
{
	struct sun4i_csi *csi = dev_get_drvdata(dev);

	clk_disable_unprepare(csi->isp_clk);
	clk_disable_unprepare(csi->ram_clk);
	clk_disable_unprepare(csi->bus_clk);

	reset_control_assert(csi->rst);

	return 0;
}

static const struct dev_pm_ops sun4i_csi_pm_ops = {
	SET_RUNTIME_PM_OPS(sun4i_csi_runtime_suspend,
			   sun4i_csi_runtime_resume,
			   NULL)
};

static struct platform_driver sun4i_csi_driver = {
	.probe	= sun4i_csi_probe,
	.remove	= sun4i_csi_remove,
	.driver	= {
		.name		= "sun4i-csi",
		.of_match_table	= sun4i_csi_of_match,
		.pm		= &sun4i_csi_pm_ops,
	},
};
module_platform_driver(sun4i_csi_driver);

MODULE_DESCRIPTION("Allwinner A10 Camera Sensor Interface driver");
MODULE_AUTHOR("Maxime Ripard <mripard@kernel.org>");
MODULE_LICENSE("GPL");
