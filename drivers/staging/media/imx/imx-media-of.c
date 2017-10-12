/*
 * Media driver for Freescale i.MX5/6 SOC
 *
 * Open Firmware parsing.
 *
 * Copyright (c) 2016 Mentor Graphics Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/of_platform.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>
#include <linux/of_graph.h>
#include <video/imx-ipu-v3.h>
#include "imx-media.h"

static int of_add_pad_link(struct imx_media_dev *imxmd,
			   struct imx_media_pad *pad,
			   struct device_node *local_sd_node,
			   struct device_node *remote_sd_node,
			   int local_pad, int remote_pad)
{
	dev_dbg(imxmd->md.dev, "%s: adding %s:%d -> %s:%d\n", __func__,
		local_sd_node->name, local_pad,
		remote_sd_node->name, remote_pad);

	return imx_media_add_pad_link(imxmd, pad, remote_sd_node, NULL,
				      local_pad, remote_pad);
}

static void of_parse_sensor(struct imx_media_dev *imxmd,
			    struct imx_media_subdev *sensor,
			    struct device_node *sensor_np)
{
	struct device_node *endpoint;

	endpoint = of_graph_get_next_endpoint(sensor_np, NULL);
	if (endpoint) {
		v4l2_fwnode_endpoint_parse(of_fwnode_handle(endpoint),
					   &sensor->sensor_ep);
		of_node_put(endpoint);
	}
}

static int of_get_port_count(const struct device_node *np)
{
	struct device_node *ports, *child;
	int num = 0;

	/* check if this node has a ports subnode */
	ports = of_get_child_by_name(np, "ports");
	if (ports)
		np = ports;

	for_each_child_of_node(np, child)
		if (of_node_cmp(child->name, "port") == 0)
			num++;

	of_node_put(ports);
	return num;
}

/*
 * find the remote device node and remote port id (remote pad #)
 * given local endpoint node
 */
static void of_get_remote_pad(struct device_node *epnode,
			      struct device_node **remote_node,
			      int *remote_pad)
{
	struct device_node *rp, *rpp;
	struct device_node *remote;

	rp = of_graph_get_remote_port(epnode);
	rpp = of_graph_get_remote_port_parent(epnode);

	if (of_device_is_compatible(rpp, "fsl,imx6q-ipu")) {
		/* the remote is one of the CSI ports */
		remote = rp;
		*remote_pad = 0;
		of_node_put(rpp);
	} else {
		remote = rpp;
		if (of_property_read_u32(rp, "reg", remote_pad))
			*remote_pad = 0;
		of_node_put(rp);
	}

	if (!of_device_is_available(remote)) {
		of_node_put(remote);
		*remote_node = NULL;
	} else {
		*remote_node = remote;
	}
}

static int
of_parse_subdev(struct imx_media_dev *imxmd, struct device_node *sd_np,
		bool is_csi_port, struct imx_media_subdev **subdev)
{
	struct imx_media_subdev *imxsd;
	int i, num_pads, ret;

	if (!of_device_is_available(sd_np)) {
		dev_dbg(imxmd->md.dev, "%s: %s not enabled\n", __func__,
			sd_np->name);
		*subdev = NULL;
		/* unavailable is not an error */
		return 0;
	}

	/* register this subdev with async notifier */
	imxsd = imx_media_add_async_subdev(imxmd, sd_np, NULL);
	ret = PTR_ERR_OR_ZERO(imxsd);
	if (ret) {
		if (ret == -EEXIST) {
			/* already added, everything is fine */
			*subdev = NULL;
			return 0;
		}

		/* other error, can't continue */
		return ret;
	}
	*subdev = imxsd;

	if (is_csi_port) {
		/*
		 * the ipu-csi has one sink port and two source ports.
		 * The source ports are not represented in the device tree,
		 * but are described by the internal pads and links later.
		 */
		num_pads = CSI_NUM_PADS;
		imxsd->num_sink_pads = CSI_NUM_SINK_PADS;
	} else if (of_device_is_compatible(sd_np, "fsl,imx6-mipi-csi2")) {
		num_pads = of_get_port_count(sd_np);
		/* the mipi csi2 receiver has only one sink port */
		imxsd->num_sink_pads = 1;
	} else if (of_device_is_compatible(sd_np, "video-mux")) {
		num_pads = of_get_port_count(sd_np);
		/* for the video mux, all but the last port are sinks */
		imxsd->num_sink_pads = num_pads - 1;
	} else {
		num_pads = of_get_port_count(sd_np);
		if (num_pads != 1) {
			/* confused, but no reason to give up here */
			dev_warn(imxmd->md.dev,
				 "%s: unknown device %s with %d ports\n",
				 __func__, sd_np->name, num_pads);
			return 0;
		}

		/*
		 * we got to this node from this single source port,
		 * there are no sink pads.
		 */
		imxsd->num_sink_pads = 0;
	}

	if (imxsd->num_sink_pads >= num_pads)
		return -EINVAL;

	imxsd->num_src_pads = num_pads - imxsd->num_sink_pads;

	dev_dbg(imxmd->md.dev, "%s: %s has %d pads (%d sink, %d src)\n",
		__func__, sd_np->name, num_pads,
		imxsd->num_sink_pads, imxsd->num_src_pads);

	/*
	 * With no sink, this subdev node is the original source
	 * of video, parse it's media bus for use by the pipeline.
	 */
	if (imxsd->num_sink_pads == 0)
		of_parse_sensor(imxmd, imxsd, sd_np);

	for (i = 0; i < num_pads; i++) {
		struct device_node *epnode = NULL, *port, *remote_np;
		struct imx_media_subdev *remote_imxsd;
		struct imx_media_pad *pad;
		int remote_pad;

		/* init this pad */
		pad = &imxsd->pad[i];
		pad->pad.flags = (i < imxsd->num_sink_pads) ?
			MEDIA_PAD_FL_SINK : MEDIA_PAD_FL_SOURCE;

		if (is_csi_port)
			port = (i < imxsd->num_sink_pads) ? sd_np : NULL;
		else
			port = of_graph_get_port_by_id(sd_np, i);
		if (!port)
			continue;

		for_each_child_of_node(port, epnode) {
			of_get_remote_pad(epnode, &remote_np, &remote_pad);
			if (!remote_np)
				continue;

			ret = of_add_pad_link(imxmd, pad, sd_np, remote_np,
					      i, remote_pad);
			if (ret)
				break;

			if (i < imxsd->num_sink_pads) {
				/* follow sink endpoints upstream */
				ret = of_parse_subdev(imxmd, remote_np,
						      false, &remote_imxsd);
				if (ret)
					break;
			}

			of_node_put(remote_np);
		}

		if (port != sd_np)
			of_node_put(port);
		if (ret) {
			of_node_put(remote_np);
			of_node_put(epnode);
			break;
		}
	}

	return ret;
}

int imx_media_of_parse(struct imx_media_dev *imxmd,
		       struct imx_media_subdev *(*csi)[4],
		       struct device_node *np)
{
	struct imx_media_subdev *lcsi;
	struct device_node *csi_np;
	u32 ipu_id, csi_id;
	int i, ret;

	for (i = 0; ; i++) {
		csi_np = of_parse_phandle(np, "ports", i);
		if (!csi_np)
			break;

		ret = of_parse_subdev(imxmd, csi_np, true, &lcsi);
		if (ret)
			goto err_put;

		ret = of_property_read_u32(csi_np, "reg", &csi_id);
		if (ret) {
			dev_err(imxmd->md.dev,
				"%s: csi port missing reg property!\n",
				__func__);
			goto err_put;
		}

		ipu_id = of_alias_get_id(csi_np->parent, "ipu");
		of_node_put(csi_np);

		if (ipu_id > 1 || csi_id > 1) {
			dev_err(imxmd->md.dev,
				"%s: invalid ipu/csi id (%u/%u)\n",
				__func__, ipu_id, csi_id);
			return -EINVAL;
		}

		(*csi)[ipu_id * 2 + csi_id] = lcsi;
	}

	return 0;
err_put:
	of_node_put(csi_np);
	return ret;
}
