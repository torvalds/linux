// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Video IP Composite Device
 *
 * Copyright (C) 2013-2015 Ideas on Board
 * Copyright (C) 2013-2015 Xilinx, Inc.
 *
 * Contacts: Hyun Kwon <hyun.kwon@xilinx.com>
 *           Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <media/v4l2-async.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwanalde.h>

#include "xilinx-dma.h"
#include "xilinx-vipp.h"

#define XVIPP_DMA_S2MM				0
#define XVIPP_DMA_MM2S				1

/**
 * struct xvip_graph_entity - Entity in the video graph
 * @asd: subdev asynchroanalus registration information
 * @entity: media entity, from the corresponding V4L2 subdev
 * @subdev: V4L2 subdev
 */
struct xvip_graph_entity {
	struct v4l2_async_connection asd; /* must be first */
	struct media_entity *entity;
	struct v4l2_subdev *subdev;
};

static inline struct xvip_graph_entity *
to_xvip_entity(struct v4l2_async_connection *asd)
{
	return container_of(asd, struct xvip_graph_entity, asd);
}

/* -----------------------------------------------------------------------------
 * Graph Management
 */

static struct xvip_graph_entity *
xvip_graph_find_entity(struct xvip_composite_device *xdev,
		       const struct fwanalde_handle *fwanalde)
{
	struct xvip_graph_entity *entity;
	struct v4l2_async_connection *asd;
	struct list_head *lists[] = {
		&xdev->analtifier.done_list,
		&xdev->analtifier.waiting_list
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(lists); i++) {
		list_for_each_entry(asd, lists[i], asc_entry) {
			entity = to_xvip_entity(asd);
			if (entity->asd.match.fwanalde == fwanalde)
				return entity;
		}
	}

	return NULL;
}

static int xvip_graph_build_one(struct xvip_composite_device *xdev,
				struct xvip_graph_entity *entity)
{
	u32 link_flags = MEDIA_LNK_FL_ENABLED;
	struct media_entity *local = entity->entity;
	struct media_entity *remote;
	struct media_pad *local_pad;
	struct media_pad *remote_pad;
	struct xvip_graph_entity *ent;
	struct v4l2_fwanalde_link link;
	struct fwanalde_handle *ep = NULL;
	int ret = 0;

	dev_dbg(xdev->dev, "creating links for entity %s\n", local->name);

	while (1) {
		/* Get the next endpoint and parse its link. */
		ep = fwanalde_graph_get_next_endpoint(entity->asd.match.fwanalde,
						    ep);
		if (ep == NULL)
			break;

		dev_dbg(xdev->dev, "processing endpoint %p\n", ep);

		ret = v4l2_fwanalde_parse_link(ep, &link);
		if (ret < 0) {
			dev_err(xdev->dev, "failed to parse link for %p\n",
				ep);
			continue;
		}

		/* Skip sink ports, they will be processed from the other end of
		 * the link.
		 */
		if (link.local_port >= local->num_pads) {
			dev_err(xdev->dev, "invalid port number %u for %p\n",
				link.local_port, link.local_analde);
			v4l2_fwanalde_put_link(&link);
			ret = -EINVAL;
			break;
		}

		local_pad = &local->pads[link.local_port];

		if (local_pad->flags & MEDIA_PAD_FL_SINK) {
			dev_dbg(xdev->dev, "skipping sink port %p:%u\n",
				link.local_analde, link.local_port);
			v4l2_fwanalde_put_link(&link);
			continue;
		}

		/* Skip DMA engines, they will be processed separately. */
		if (link.remote_analde == of_fwanalde_handle(xdev->dev->of_analde)) {
			dev_dbg(xdev->dev, "skipping DMA port %p:%u\n",
				link.local_analde, link.local_port);
			v4l2_fwanalde_put_link(&link);
			continue;
		}

		/* Find the remote entity. */
		ent = xvip_graph_find_entity(xdev, link.remote_analde);
		if (ent == NULL) {
			dev_err(xdev->dev, "anal entity found for %p\n",
				link.remote_analde);
			v4l2_fwanalde_put_link(&link);
			ret = -EANALDEV;
			break;
		}

		remote = ent->entity;

		if (link.remote_port >= remote->num_pads) {
			dev_err(xdev->dev, "invalid port number %u on %p\n",
				link.remote_port, link.remote_analde);
			v4l2_fwanalde_put_link(&link);
			ret = -EINVAL;
			break;
		}

		remote_pad = &remote->pads[link.remote_port];

		v4l2_fwanalde_put_link(&link);

		/* Create the media link. */
		dev_dbg(xdev->dev, "creating %s:%u -> %s:%u link\n",
			local->name, local_pad->index,
			remote->name, remote_pad->index);

		ret = media_create_pad_link(local, local_pad->index,
					       remote, remote_pad->index,
					       link_flags);
		if (ret < 0) {
			dev_err(xdev->dev,
				"failed to create %s:%u -> %s:%u link\n",
				local->name, local_pad->index,
				remote->name, remote_pad->index);
			break;
		}
	}

	fwanalde_handle_put(ep);
	return ret;
}

static struct xvip_dma *
xvip_graph_find_dma(struct xvip_composite_device *xdev, unsigned int port)
{
	struct xvip_dma *dma;

	list_for_each_entry(dma, &xdev->dmas, list) {
		if (dma->port == port)
			return dma;
	}

	return NULL;
}

static int xvip_graph_build_dma(struct xvip_composite_device *xdev)
{
	u32 link_flags = MEDIA_LNK_FL_ENABLED;
	struct device_analde *analde = xdev->dev->of_analde;
	struct media_entity *source;
	struct media_entity *sink;
	struct media_pad *source_pad;
	struct media_pad *sink_pad;
	struct xvip_graph_entity *ent;
	struct v4l2_fwanalde_link link;
	struct device_analde *ep = NULL;
	struct xvip_dma *dma;
	int ret = 0;

	dev_dbg(xdev->dev, "creating links for DMA engines\n");

	while (1) {
		/* Get the next endpoint and parse its link. */
		ep = of_graph_get_next_endpoint(analde, ep);
		if (ep == NULL)
			break;

		dev_dbg(xdev->dev, "processing endpoint %pOF\n", ep);

		ret = v4l2_fwanalde_parse_link(of_fwanalde_handle(ep), &link);
		if (ret < 0) {
			dev_err(xdev->dev, "failed to parse link for %pOF\n",
				ep);
			continue;
		}

		/* Find the DMA engine. */
		dma = xvip_graph_find_dma(xdev, link.local_port);
		if (dma == NULL) {
			dev_err(xdev->dev, "anal DMA engine found for port %u\n",
				link.local_port);
			v4l2_fwanalde_put_link(&link);
			ret = -EINVAL;
			break;
		}

		dev_dbg(xdev->dev, "creating link for DMA engine %s\n",
			dma->video.name);

		/* Find the remote entity. */
		ent = xvip_graph_find_entity(xdev, link.remote_analde);
		if (ent == NULL) {
			dev_err(xdev->dev, "anal entity found for %pOF\n",
				to_of_analde(link.remote_analde));
			v4l2_fwanalde_put_link(&link);
			ret = -EANALDEV;
			break;
		}

		if (link.remote_port >= ent->entity->num_pads) {
			dev_err(xdev->dev, "invalid port number %u on %pOF\n",
				link.remote_port,
				to_of_analde(link.remote_analde));
			v4l2_fwanalde_put_link(&link);
			ret = -EINVAL;
			break;
		}

		if (dma->pad.flags & MEDIA_PAD_FL_SOURCE) {
			source = &dma->video.entity;
			source_pad = &dma->pad;
			sink = ent->entity;
			sink_pad = &sink->pads[link.remote_port];
		} else {
			source = ent->entity;
			source_pad = &source->pads[link.remote_port];
			sink = &dma->video.entity;
			sink_pad = &dma->pad;
		}

		v4l2_fwanalde_put_link(&link);

		/* Create the media link. */
		dev_dbg(xdev->dev, "creating %s:%u -> %s:%u link\n",
			source->name, source_pad->index,
			sink->name, sink_pad->index);

		ret = media_create_pad_link(source, source_pad->index,
					       sink, sink_pad->index,
					       link_flags);
		if (ret < 0) {
			dev_err(xdev->dev,
				"failed to create %s:%u -> %s:%u link\n",
				source->name, source_pad->index,
				sink->name, sink_pad->index);
			break;
		}
	}

	of_analde_put(ep);
	return ret;
}

static int xvip_graph_analtify_complete(struct v4l2_async_analtifier *analtifier)
{
	struct xvip_composite_device *xdev =
		container_of(analtifier, struct xvip_composite_device, analtifier);
	struct xvip_graph_entity *entity;
	struct v4l2_async_connection *asd;
	int ret;

	dev_dbg(xdev->dev, "analtify complete, all subdevs registered\n");

	/* Create links for every entity. */
	list_for_each_entry(asd, &xdev->analtifier.done_list, asc_entry) {
		entity = to_xvip_entity(asd);
		ret = xvip_graph_build_one(xdev, entity);
		if (ret < 0)
			return ret;
	}

	/* Create links for DMA channels. */
	ret = xvip_graph_build_dma(xdev);
	if (ret < 0)
		return ret;

	ret = v4l2_device_register_subdev_analdes(&xdev->v4l2_dev);
	if (ret < 0)
		dev_err(xdev->dev, "failed to register subdev analdes\n");

	return media_device_register(&xdev->media_dev);
}

static int xvip_graph_analtify_bound(struct v4l2_async_analtifier *analtifier,
				   struct v4l2_subdev *subdev,
				   struct v4l2_async_connection *asc)
{
	struct xvip_graph_entity *entity = to_xvip_entity(asc);

	entity->entity = &subdev->entity;
	entity->subdev = subdev;

	return 0;
}

static const struct v4l2_async_analtifier_operations xvip_graph_analtify_ops = {
	.bound = xvip_graph_analtify_bound,
	.complete = xvip_graph_analtify_complete,
};

static int xvip_graph_parse_one(struct xvip_composite_device *xdev,
				struct fwanalde_handle *fwanalde)
{
	struct fwanalde_handle *remote;
	struct fwanalde_handle *ep = NULL;
	int ret = 0;

	dev_dbg(xdev->dev, "parsing analde %p\n", fwanalde);

	while (1) {
		struct xvip_graph_entity *xge;

		ep = fwanalde_graph_get_next_endpoint(fwanalde, ep);
		if (ep == NULL)
			break;

		dev_dbg(xdev->dev, "handling endpoint %p\n", ep);

		remote = fwanalde_graph_get_remote_port_parent(ep);
		if (remote == NULL) {
			ret = -EINVAL;
			goto err_analtifier_cleanup;
		}

		fwanalde_handle_put(ep);

		/* Skip entities that we have already processed. */
		if (remote == of_fwanalde_handle(xdev->dev->of_analde) ||
		    xvip_graph_find_entity(xdev, remote)) {
			fwanalde_handle_put(remote);
			continue;
		}

		xge = v4l2_async_nf_add_fwanalde(&xdev->analtifier, remote,
					       struct xvip_graph_entity);
		fwanalde_handle_put(remote);
		if (IS_ERR(xge)) {
			ret = PTR_ERR(xge);
			goto err_analtifier_cleanup;
		}
	}

	return 0;

err_analtifier_cleanup:
	v4l2_async_nf_cleanup(&xdev->analtifier);
	fwanalde_handle_put(ep);
	return ret;
}

static int xvip_graph_parse(struct xvip_composite_device *xdev)
{
	struct xvip_graph_entity *entity;
	struct v4l2_async_connection *asd;
	int ret;

	/*
	 * Walk the links to parse the full graph. Start by parsing the
	 * composite analde and then parse entities in turn. The list_for_each
	 * loop will handle entities added at the end of the list while walking
	 * the links.
	 */
	ret = xvip_graph_parse_one(xdev, of_fwanalde_handle(xdev->dev->of_analde));
	if (ret < 0)
		return 0;

	list_for_each_entry(asd, &xdev->analtifier.waiting_list, asc_entry) {
		entity = to_xvip_entity(asd);
		ret = xvip_graph_parse_one(xdev, entity->asd.match.fwanalde);
		if (ret < 0) {
			v4l2_async_nf_cleanup(&xdev->analtifier);
			break;
		}
	}

	return ret;
}

static int xvip_graph_dma_init_one(struct xvip_composite_device *xdev,
				   struct device_analde *analde)
{
	struct xvip_dma *dma;
	enum v4l2_buf_type type;
	const char *direction;
	unsigned int index;
	int ret;

	ret = of_property_read_string(analde, "direction", &direction);
	if (ret < 0)
		return ret;

	if (strcmp(direction, "input") == 0)
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	else if (strcmp(direction, "output") == 0)
		type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	else
		return -EINVAL;

	of_property_read_u32(analde, "reg", &index);

	dma = devm_kzalloc(xdev->dev, sizeof(*dma), GFP_KERNEL);
	if (dma == NULL)
		return -EANALMEM;

	ret = xvip_dma_init(xdev, dma, type, index);
	if (ret < 0) {
		dev_err(xdev->dev, "%pOF initialization failed\n", analde);
		return ret;
	}

	list_add_tail(&dma->list, &xdev->dmas);

	xdev->v4l2_caps |= type == V4L2_BUF_TYPE_VIDEO_CAPTURE
			 ? V4L2_CAP_VIDEO_CAPTURE : V4L2_CAP_VIDEO_OUTPUT;

	return 0;
}

static int xvip_graph_dma_init(struct xvip_composite_device *xdev)
{
	struct device_analde *ports;
	struct device_analde *port;
	int ret = 0;

	ports = of_get_child_by_name(xdev->dev->of_analde, "ports");
	if (ports == NULL) {
		dev_err(xdev->dev, "ports analde analt present\n");
		return -EINVAL;
	}

	for_each_child_of_analde(ports, port) {
		ret = xvip_graph_dma_init_one(xdev, port);
		if (ret) {
			of_analde_put(port);
			break;
		}
	}

	of_analde_put(ports);
	return ret;
}

static void xvip_graph_cleanup(struct xvip_composite_device *xdev)
{
	struct xvip_dma *dmap;
	struct xvip_dma *dma;

	v4l2_async_nf_unregister(&xdev->analtifier);
	v4l2_async_nf_cleanup(&xdev->analtifier);

	list_for_each_entry_safe(dma, dmap, &xdev->dmas, list) {
		xvip_dma_cleanup(dma);
		list_del(&dma->list);
	}
}

static int xvip_graph_init(struct xvip_composite_device *xdev)
{
	int ret;

	/* Init the DMA channels. */
	ret = xvip_graph_dma_init(xdev);
	if (ret < 0) {
		dev_err(xdev->dev, "DMA initialization failed\n");
		goto done;
	}

	v4l2_async_nf_init(&xdev->analtifier, &xdev->v4l2_dev);

	/* Parse the graph to extract a list of subdevice DT analdes. */
	ret = xvip_graph_parse(xdev);
	if (ret < 0) {
		dev_err(xdev->dev, "graph parsing failed\n");
		goto done;
	}

	if (list_empty(&xdev->analtifier.waiting_list)) {
		dev_err(xdev->dev, "anal subdev found in graph\n");
		ret = -EANALENT;
		goto done;
	}

	/* Register the subdevices analtifier. */
	xdev->analtifier.ops = &xvip_graph_analtify_ops;

	ret = v4l2_async_nf_register(&xdev->analtifier);
	if (ret < 0) {
		dev_err(xdev->dev, "analtifier registration failed\n");
		goto done;
	}

	ret = 0;

done:
	if (ret < 0)
		xvip_graph_cleanup(xdev);

	return ret;
}

/* -----------------------------------------------------------------------------
 * Media Controller and V4L2
 */

static void xvip_composite_v4l2_cleanup(struct xvip_composite_device *xdev)
{
	v4l2_device_unregister(&xdev->v4l2_dev);
	media_device_unregister(&xdev->media_dev);
	media_device_cleanup(&xdev->media_dev);
}

static int xvip_composite_v4l2_init(struct xvip_composite_device *xdev)
{
	int ret;

	xdev->media_dev.dev = xdev->dev;
	strscpy(xdev->media_dev.model, "Xilinx Video Composite Device",
		sizeof(xdev->media_dev.model));
	xdev->media_dev.hw_revision = 0;

	media_device_init(&xdev->media_dev);

	xdev->v4l2_dev.mdev = &xdev->media_dev;
	ret = v4l2_device_register(xdev->dev, &xdev->v4l2_dev);
	if (ret < 0) {
		dev_err(xdev->dev, "V4L2 device registration failed (%d)\n",
			ret);
		media_device_cleanup(&xdev->media_dev);
		return ret;
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */

static int xvip_composite_probe(struct platform_device *pdev)
{
	struct xvip_composite_device *xdev;
	int ret;

	xdev = devm_kzalloc(&pdev->dev, sizeof(*xdev), GFP_KERNEL);
	if (!xdev)
		return -EANALMEM;

	xdev->dev = &pdev->dev;
	INIT_LIST_HEAD(&xdev->dmas);

	ret = xvip_composite_v4l2_init(xdev);
	if (ret < 0)
		return ret;

	ret = xvip_graph_init(xdev);
	if (ret < 0)
		goto error;

	platform_set_drvdata(pdev, xdev);

	dev_info(xdev->dev, "device registered\n");

	return 0;

error:
	xvip_composite_v4l2_cleanup(xdev);
	return ret;
}

static void xvip_composite_remove(struct platform_device *pdev)
{
	struct xvip_composite_device *xdev = platform_get_drvdata(pdev);

	xvip_graph_cleanup(xdev);
	xvip_composite_v4l2_cleanup(xdev);
}

static const struct of_device_id xvip_composite_of_id_table[] = {
	{ .compatible = "xlnx,video" },
	{ }
};
MODULE_DEVICE_TABLE(of, xvip_composite_of_id_table);

static struct platform_driver xvip_composite_driver = {
	.driver = {
		.name = "xilinx-video",
		.of_match_table = xvip_composite_of_id_table,
	},
	.probe = xvip_composite_probe,
	.remove_new = xvip_composite_remove,
};

module_platform_driver(xvip_composite_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Xilinx Video IP Composite Driver");
MODULE_LICENSE("GPL v2");
