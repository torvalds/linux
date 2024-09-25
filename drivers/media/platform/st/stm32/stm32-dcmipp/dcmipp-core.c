// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for STM32 Digital Camera Memory Interface Pixel Processor
 *
 * Copyright (C) STMicroelectronics SA 2023
 * Authors: Hugues Fruchet <hugues.fruchet@foss.st.com>
 *          Alain Volmat <alain.volmat@foss.st.com>
 *          for STMicroelectronics.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/reset.h>
#include <media/media-device.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#include "dcmipp-common.h"

#define DCMIPP_MDEV_MODEL_NAME "DCMIPP MDEV"

#define DCMIPP_ENT_LINK(src, srcpad, sink, sinkpad, link_flags) {	\
	.src_ent = src,						\
	.src_pad = srcpad,					\
	.sink_ent = sink,					\
	.sink_pad = sinkpad,					\
	.flags = link_flags,					\
}

struct dcmipp_device {
	/* The platform device */
	struct platform_device		pdev;
	struct device			*dev;

	/* Hardware resources */
	void __iomem			*regs;
	struct clk			*kclk;

	/* The pipeline configuration */
	const struct dcmipp_pipeline_config	*pipe_cfg;

	/* The Associated media_device parent */
	struct media_device		mdev;

	/* Internal v4l2 parent device*/
	struct v4l2_device		v4l2_dev;

	/* Entities */
	struct dcmipp_ent_device	**entity;

	struct v4l2_async_notifier	notifier;
};

static inline struct dcmipp_device *
notifier_to_dcmipp(struct v4l2_async_notifier *n)
{
	return container_of(n, struct dcmipp_device, notifier);
}

/* Structure which describes individual configuration for each entity */
struct dcmipp_ent_config {
	const char *name;
	struct dcmipp_ent_device *(*init)
		(struct device *dev, const char *entity_name,
		 struct v4l2_device *v4l2_dev, void __iomem *regs);
	void (*release)(struct dcmipp_ent_device *ved);
};

/* Structure which describes links between entities */
struct dcmipp_ent_link {
	unsigned int src_ent;
	u16 src_pad;
	unsigned int sink_ent;
	u16 sink_pad;
	u32 flags;
};

/* Structure which describes the whole topology */
struct dcmipp_pipeline_config {
	const struct dcmipp_ent_config *ents;
	size_t num_ents;
	const struct dcmipp_ent_link *links;
	size_t num_links;
};

/* --------------------------------------------------------------------------
 * Topology Configuration
 */

static const struct dcmipp_ent_config stm32mp13_ent_config[] = {
	{
		.name = "dcmipp_parallel",
		.init = dcmipp_par_ent_init,
		.release = dcmipp_par_ent_release,
	},
	{
		.name = "dcmipp_dump_postproc",
		.init = dcmipp_byteproc_ent_init,
		.release = dcmipp_byteproc_ent_release,
	},
	{
		.name = "dcmipp_dump_capture",
		.init = dcmipp_bytecap_ent_init,
		.release = dcmipp_bytecap_ent_release,
	},
};

#define ID_PARALLEL 0
#define ID_DUMP_BYTEPROC 1
#define ID_DUMP_CAPTURE 2

static const struct dcmipp_ent_link stm32mp13_ent_links[] = {
	DCMIPP_ENT_LINK(ID_PARALLEL,      1, ID_DUMP_BYTEPROC, 0,
			MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE),
	DCMIPP_ENT_LINK(ID_DUMP_BYTEPROC, 1, ID_DUMP_CAPTURE,  0,
			MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE),
};

static const struct dcmipp_pipeline_config stm32mp13_pipe_cfg = {
	.ents		= stm32mp13_ent_config,
	.num_ents	= ARRAY_SIZE(stm32mp13_ent_config),
	.links		= stm32mp13_ent_links,
	.num_links	= ARRAY_SIZE(stm32mp13_ent_links)
};

#define LINK_FLAG_TO_STR(f) ((f) == 0 ? "" :\
			     (f) == MEDIA_LNK_FL_ENABLED ? "ENABLED" :\
			     (f) == MEDIA_LNK_FL_IMMUTABLE ? "IMMUTABLE" :\
			     (f) == (MEDIA_LNK_FL_ENABLED |\
				     MEDIA_LNK_FL_IMMUTABLE) ?\
					"ENABLED, IMMUTABLE" :\
			     "UNKNOWN")

static int dcmipp_create_links(struct dcmipp_device *dcmipp)
{
	unsigned int i;
	int ret;

	/* Initialize the links between entities */
	for (i = 0; i < dcmipp->pipe_cfg->num_links; i++) {
		const struct dcmipp_ent_link *link =
			&dcmipp->pipe_cfg->links[i];
		struct dcmipp_ent_device *ved_src =
			dcmipp->entity[link->src_ent];
		struct dcmipp_ent_device *ved_sink =
			dcmipp->entity[link->sink_ent];

		dev_dbg(dcmipp->dev, "Create link \"%s\":%d -> %d:\"%s\" [%s]\n",
			dcmipp->pipe_cfg->ents[link->src_ent].name,
			link->src_pad, link->sink_pad,
			dcmipp->pipe_cfg->ents[link->sink_ent].name,
			LINK_FLAG_TO_STR(link->flags));

		ret = media_create_pad_link(ved_src->ent, link->src_pad,
					    ved_sink->ent, link->sink_pad,
					    link->flags);
		if (ret)
			return ret;
	}

	return 0;
}

static int dcmipp_graph_init(struct dcmipp_device *dcmipp);

static int dcmipp_create_subdevs(struct dcmipp_device *dcmipp)
{
	int ret, i;

	/* Call all subdev inits */
	for (i = 0; i < dcmipp->pipe_cfg->num_ents; i++) {
		const char *name = dcmipp->pipe_cfg->ents[i].name;

		dev_dbg(dcmipp->dev, "add subdev %s\n", name);
		dcmipp->entity[i] =
			dcmipp->pipe_cfg->ents[i].init(dcmipp->dev, name,
						       &dcmipp->v4l2_dev,
						       dcmipp->regs);
		if (IS_ERR(dcmipp->entity[i])) {
			dev_err(dcmipp->dev, "failed to init subdev %s\n",
				name);
			ret = PTR_ERR(dcmipp->entity[i]);
			goto err_init_entity;
		}
	}

	/* Initialize links */
	ret = dcmipp_create_links(dcmipp);
	if (ret)
		goto err_init_entity;

	ret = dcmipp_graph_init(dcmipp);
	if (ret < 0)
		goto err_init_entity;

	return 0;

err_init_entity:
	while (i-- > 0)
		dcmipp->pipe_cfg->ents[i].release(dcmipp->entity[i]);
	return ret;
}

static const struct of_device_id dcmipp_of_match[] = {
	{ .compatible = "st,stm32mp13-dcmipp", .data = &stm32mp13_pipe_cfg },
	{ /* end node */ },
};
MODULE_DEVICE_TABLE(of, dcmipp_of_match);

static irqreturn_t dcmipp_irq_thread(int irq, void *arg)
{
	struct dcmipp_device *dcmipp = arg;
	struct dcmipp_ent_device *ved;
	unsigned int i;

	/* Call irq thread of each entities of pipeline */
	for (i = 0; i < dcmipp->pipe_cfg->num_ents; i++) {
		ved = dcmipp->entity[i];
		if (ved->thread_fn && ved->handler_ret == IRQ_WAKE_THREAD)
			ved->thread_fn(irq, ved);
	}

	return IRQ_HANDLED;
}

static irqreturn_t dcmipp_irq_callback(int irq, void *arg)
{
	struct dcmipp_device *dcmipp = arg;
	struct dcmipp_ent_device *ved;
	irqreturn_t ret = IRQ_HANDLED;
	unsigned int i;

	/* Call irq handler of each entities of pipeline */
	for (i = 0; i < dcmipp->pipe_cfg->num_ents; i++) {
		ved = dcmipp->entity[i];
		if (ved->handler)
			ved->handler_ret = ved->handler(irq, ved);
		else if (ved->thread_fn)
			ved->handler_ret = IRQ_WAKE_THREAD;
		else
			ved->handler_ret = IRQ_HANDLED;
		if (ved->handler_ret != IRQ_HANDLED)
			ret = ved->handler_ret;
	}

	return ret;
}

static int dcmipp_graph_notify_bound(struct v4l2_async_notifier *notifier,
				     struct v4l2_subdev *subdev,
				     struct v4l2_async_connection *asd)
{
	struct dcmipp_device *dcmipp = notifier_to_dcmipp(notifier);
	unsigned int ret;
	int src_pad;
	struct dcmipp_ent_device *sink;
	struct v4l2_fwnode_endpoint vep = { .bus_type = V4L2_MBUS_PARALLEL };
	struct fwnode_handle *ep;

	dev_dbg(dcmipp->dev, "Subdev \"%s\" bound\n", subdev->name);

	/*
	 * Link this sub-device to DCMIPP, it could be
	 * a parallel camera sensor or a CSI-2 to parallel bridge
	 */
	src_pad = media_entity_get_fwnode_pad(&subdev->entity,
					      subdev->fwnode,
					      MEDIA_PAD_FL_SOURCE);

	/* Get bus characteristics from devicetree */
	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(dcmipp->dev), 0, 0,
					     FWNODE_GRAPH_ENDPOINT_NEXT);
	if (!ep) {
		dev_err(dcmipp->dev, "Could not find the endpoint\n");
		return -ENODEV;
	}

	/* Check for parallel bus-type first, then bt656 */
	ret = v4l2_fwnode_endpoint_parse(ep, &vep);
	if (ret) {
		vep.bus_type = V4L2_MBUS_BT656;
		ret = v4l2_fwnode_endpoint_parse(ep, &vep);
		if (ret) {
			dev_err(dcmipp->dev, "Could not parse the endpoint\n");
			fwnode_handle_put(ep);
			return ret;
		}
	}

	fwnode_handle_put(ep);

	if (vep.bus.parallel.bus_width == 0) {
		dev_err(dcmipp->dev, "Invalid parallel interface bus-width\n");
		return -ENODEV;
	}

	/* Only 8 bits bus width supported with BT656 bus */
	if (vep.bus_type == V4L2_MBUS_BT656 &&
	    vep.bus.parallel.bus_width != 8) {
		dev_err(dcmipp->dev, "BT656 bus conflicts with %u bits bus width (8 bits required)\n",
			vep.bus.parallel.bus_width);
		return -ENODEV;
	}

	/* Parallel input device detected, connect it to parallel subdev */
	sink = dcmipp->entity[ID_PARALLEL];
	sink->bus.flags = vep.bus.parallel.flags;
	sink->bus.bus_width = vep.bus.parallel.bus_width;
	sink->bus.data_shift = vep.bus.parallel.data_shift;
	sink->bus_type = vep.bus_type;
	ret = media_create_pad_link(&subdev->entity, src_pad, sink->ent, 0,
				    MEDIA_LNK_FL_IMMUTABLE |
				    MEDIA_LNK_FL_ENABLED);
	if (ret) {
		dev_err(dcmipp->dev, "Failed to create media pad link with subdev \"%s\"\n",
			subdev->name);
		return ret;
	}

	dev_dbg(dcmipp->dev, "DCMIPP is now linked to \"%s\"\n", subdev->name);

	return 0;
}

static void dcmipp_graph_notify_unbind(struct v4l2_async_notifier *notifier,
				       struct v4l2_subdev *sd,
				       struct v4l2_async_connection *asd)
{
	struct dcmipp_device *dcmipp = notifier_to_dcmipp(notifier);

	dev_dbg(dcmipp->dev, "Removing %s\n", sd->name);
}

static int dcmipp_graph_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct dcmipp_device *dcmipp = notifier_to_dcmipp(notifier);
	int ret;

	/* Register the media device */
	ret = media_device_register(&dcmipp->mdev);
	if (ret) {
		dev_err(dcmipp->mdev.dev,
			"media device register failed (err=%d)\n", ret);
		return ret;
	}

	/* Expose all subdev's nodes*/
	ret = v4l2_device_register_subdev_nodes(&dcmipp->v4l2_dev);
	if (ret) {
		dev_err(dcmipp->mdev.dev,
			"dcmipp subdev nodes registration failed (err=%d)\n",
			ret);
		media_device_unregister(&dcmipp->mdev);
		return ret;
	}

	dev_dbg(dcmipp->dev, "Notify complete !\n");

	return 0;
}

static const struct v4l2_async_notifier_operations dcmipp_graph_notify_ops = {
	.bound = dcmipp_graph_notify_bound,
	.unbind = dcmipp_graph_notify_unbind,
	.complete = dcmipp_graph_notify_complete,
};

static int dcmipp_graph_init(struct dcmipp_device *dcmipp)
{
	struct v4l2_async_connection *asd;
	struct fwnode_handle *ep;
	int ret;

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(dcmipp->dev), 0, 0,
					     FWNODE_GRAPH_ENDPOINT_NEXT);
	if (!ep) {
		dev_err(dcmipp->dev, "Failed to get next endpoint\n");
		return -EINVAL;
	}

	v4l2_async_nf_init(&dcmipp->notifier, &dcmipp->v4l2_dev);

	asd = v4l2_async_nf_add_fwnode_remote(&dcmipp->notifier, ep,
					      struct v4l2_async_connection);

	fwnode_handle_put(ep);

	if (IS_ERR(asd)) {
		dev_err(dcmipp->dev, "Failed to add fwnode remote subdev\n");
		return PTR_ERR(asd);
	}

	dcmipp->notifier.ops = &dcmipp_graph_notify_ops;

	ret = v4l2_async_nf_register(&dcmipp->notifier);
	if (ret < 0) {
		dev_err(dcmipp->dev, "Failed to register notifier\n");
		v4l2_async_nf_cleanup(&dcmipp->notifier);
		return ret;
	}

	return 0;
}

static int dcmipp_probe(struct platform_device *pdev)
{
	struct dcmipp_device *dcmipp;
	struct clk *kclk;
	const struct dcmipp_pipeline_config *pipe_cfg;
	struct reset_control *rstc;
	int irq;
	int ret;

	dcmipp = devm_kzalloc(&pdev->dev, sizeof(*dcmipp), GFP_KERNEL);
	if (!dcmipp)
		return -ENOMEM;

	dcmipp->dev = &pdev->dev;

	pipe_cfg = device_get_match_data(dcmipp->dev);
	if (!pipe_cfg) {
		dev_err(&pdev->dev, "Can't get device data\n");
		return -ENODEV;
	}
	dcmipp->pipe_cfg = pipe_cfg;

	platform_set_drvdata(pdev, dcmipp);

	/* Get hardware resources from devicetree */
	rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(rstc))
		return dev_err_probe(&pdev->dev, PTR_ERR(rstc),
				     "Could not get reset control\n");

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	dcmipp->regs = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(dcmipp->regs)) {
		dev_err(&pdev->dev, "Could not map registers\n");
		return PTR_ERR(dcmipp->regs);
	}

	ret = devm_request_threaded_irq(&pdev->dev, irq, dcmipp_irq_callback,
					dcmipp_irq_thread, IRQF_ONESHOT,
					dev_name(&pdev->dev), dcmipp);
	if (ret) {
		dev_err(&pdev->dev, "Unable to request irq %d\n", irq);
		return ret;
	}

	/* Reset device */
	ret = reset_control_assert(rstc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to assert the reset line\n");
		return ret;
	}

	usleep_range(3000, 5000);

	ret = reset_control_deassert(rstc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to deassert the reset line\n");
		return ret;
	}

	kclk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(kclk))
		return dev_err_probe(&pdev->dev, PTR_ERR(kclk),
				     "Unable to get kclk\n");
	dcmipp->kclk = kclk;

	dcmipp->entity = devm_kcalloc(&pdev->dev, dcmipp->pipe_cfg->num_ents,
				      sizeof(*dcmipp->entity), GFP_KERNEL);
	if (!dcmipp->entity)
		return -ENOMEM;

	/* Register the v4l2 struct */
	ret = v4l2_device_register(&pdev->dev, &dcmipp->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev,
			"v4l2 device register failed (err=%d)\n", ret);
		return ret;
	}

	/* Link the media device within the v4l2_device */
	dcmipp->v4l2_dev.mdev = &dcmipp->mdev;

	/* Initialize media device */
	strscpy(dcmipp->mdev.model, DCMIPP_MDEV_MODEL_NAME,
		sizeof(dcmipp->mdev.model));
	dcmipp->mdev.dev = &pdev->dev;
	media_device_init(&dcmipp->mdev);

	/* Initialize subdevs */
	ret = dcmipp_create_subdevs(dcmipp);
	if (ret) {
		media_device_cleanup(&dcmipp->mdev);
		v4l2_device_unregister(&dcmipp->v4l2_dev);
		return ret;
	}

	pm_runtime_enable(dcmipp->dev);

	dev_info(&pdev->dev, "Probe done");

	return 0;
}

static void dcmipp_remove(struct platform_device *pdev)
{
	struct dcmipp_device *dcmipp = platform_get_drvdata(pdev);
	unsigned int i;

	pm_runtime_disable(&pdev->dev);

	v4l2_async_nf_unregister(&dcmipp->notifier);
	v4l2_async_nf_cleanup(&dcmipp->notifier);

	for (i = 0; i < dcmipp->pipe_cfg->num_ents; i++)
		dcmipp->pipe_cfg->ents[i].release(dcmipp->entity[i]);

	media_device_unregister(&dcmipp->mdev);
	media_device_cleanup(&dcmipp->mdev);

	v4l2_device_unregister(&dcmipp->v4l2_dev);
}

static int dcmipp_runtime_suspend(struct device *dev)
{
	struct dcmipp_device *dcmipp = dev_get_drvdata(dev);

	clk_disable_unprepare(dcmipp->kclk);

	return 0;
}

static int dcmipp_runtime_resume(struct device *dev)
{
	struct dcmipp_device *dcmipp = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(dcmipp->kclk);
	if (ret)
		dev_err(dev, "%s: Failed to prepare_enable kclk\n", __func__);

	return ret;
}

static int dcmipp_suspend(struct device *dev)
{
	/* disable clock */
	pm_runtime_force_suspend(dev);

	/* change pinctrl state */
	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int dcmipp_resume(struct device *dev)
{
	/* restore pinctl default state */
	pinctrl_pm_select_default_state(dev);

	/* clock enable */
	pm_runtime_force_resume(dev);

	return 0;
}

static const struct dev_pm_ops dcmipp_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(dcmipp_suspend, dcmipp_resume)
	RUNTIME_PM_OPS(dcmipp_runtime_suspend, dcmipp_runtime_resume, NULL)
};

static struct platform_driver dcmipp_pdrv = {
	.probe		= dcmipp_probe,
	.remove		= dcmipp_remove,
	.driver		= {
		.name	= DCMIPP_PDEV_NAME,
		.of_match_table = dcmipp_of_match,
		.pm = pm_ptr(&dcmipp_pm_ops),
	},
};

module_platform_driver(dcmipp_pdrv);

MODULE_AUTHOR("Hugues Fruchet <hugues.fruchet@foss.st.com>");
MODULE_AUTHOR("Alain Volmat <alain.volmat@foss.st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 Digital Camera Memory Interface with Pixel Processor driver");
MODULE_LICENSE("GPL");
