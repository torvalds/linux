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
 * find the remote device node given local endpoint node
 */
static bool of_get_remote(struct device_node *epnode,
			  struct device_node **remote_node)
{
	struct device_node *rp, *rpp;
	struct device_node *remote;
	bool is_csi_port;

	rp = of_graph_get_remote_port(epnode);
	rpp = of_graph_get_remote_port_parent(epnode);

	if (of_device_is_compatible(rpp, "fsl,imx6q-ipu")) {
		/* the remote is one of the CSI ports */
		remote = rp;
		of_node_put(rpp);
		is_csi_port = true;
	} else {
		remote = rpp;
		of_node_put(rp);
		is_csi_port = false;
	}

	if (!of_device_is_available(remote)) {
		of_node_put(remote);
		*remote_node = NULL;
	} else {
		*remote_node = remote;
	}

	return is_csi_port;
}

static int
of_parse_subdev(struct imx_media_dev *imxmd, struct device_node *sd_np,
		bool is_csi_port)
{
	int i, num_ports, ret;

	if (!of_device_is_available(sd_np)) {
		dev_dbg(imxmd->md.dev, "%s: %s not enabled\n", __func__,
			sd_np->name);
		/* unavailable is not an error */
		return 0;
	}

	/* register this subdev with async notifier */
	ret = imx_media_add_async_subdev(imxmd, of_fwnode_handle(sd_np),
					 NULL);
	if (ret) {
		if (ret == -EEXIST) {
			/* already added, everything is fine */
			return 0;
		}

		/* other error, can't continue */
		return ret;
	}

	/*
	 * the ipu-csi has one sink port. The source pads are not
	 * represented in the device tree by port nodes, but are
	 * described by the internal pads and links later.
	 */
	num_ports = is_csi_port ? 1 : of_get_port_count(sd_np);

	for (i = 0; i < num_ports; i++) {
		struct device_node *epnode = NULL, *port, *remote_np;

		port = is_csi_port ? sd_np : of_graph_get_port_by_id(sd_np, i);
		if (!port)
			continue;

		for_each_child_of_node(port, epnode) {
			bool remote_is_csi;

			remote_is_csi = of_get_remote(epnode, &remote_np);
			if (!remote_np)
				continue;

			ret = of_parse_subdev(imxmd, remote_np, remote_is_csi);
			of_node_put(remote_np);
			if (ret)
				break;
		}

		if (port != sd_np)
			of_node_put(port);
		if (ret) {
			of_node_put(epnode);
			break;
		}
	}

	return ret;
}

int imx_media_add_of_subdevs(struct imx_media_dev *imxmd,
			     struct device_node *np)
{
	struct device_node *csi_np;
	int i, ret;

	for (i = 0; ; i++) {
		csi_np = of_parse_phandle(np, "ports", i);
		if (!csi_np)
			break;

		ret = of_parse_subdev(imxmd, csi_np, true);
		of_node_put(csi_np);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * Create a single media link to/from sd using a fwnode link.
 *
 * NOTE: this function assumes an OF port node is equivalent to
 * a media pad (port id equal to media pad index), and that an
 * OF endpoint node is equivalent to a media link.
 */
static int create_of_link(struct imx_media_dev *imxmd,
			  struct v4l2_subdev *sd,
			  struct v4l2_fwnode_link *link)
{
	struct v4l2_subdev *remote, *src, *sink;
	int src_pad, sink_pad;

	if (link->local_port >= sd->entity.num_pads)
		return -EINVAL;

	remote = imx_media_find_subdev_by_fwnode(imxmd, link->remote_node);
	if (!remote)
		return 0;

	if (sd->entity.pads[link->local_port].flags & MEDIA_PAD_FL_SINK) {
		src = remote;
		src_pad = link->remote_port;
		sink = sd;
		sink_pad = link->local_port;
	} else {
		src = sd;
		src_pad = link->local_port;
		sink = remote;
		sink_pad = link->remote_port;
	}

	/* make sure link doesn't already exist before creating */
	if (media_entity_find_link(&src->entity.pads[src_pad],
				   &sink->entity.pads[sink_pad]))
		return 0;

	v4l2_info(sd->v4l2_dev, "%s:%d -> %s:%d\n",
		  src->name, src_pad, sink->name, sink_pad);

	return media_create_pad_link(&src->entity, src_pad,
				     &sink->entity, sink_pad, 0);
}

/*
 * Create media links to/from sd using its device-tree endpoints.
 */
int imx_media_create_of_links(struct imx_media_dev *imxmd,
			      struct v4l2_subdev *sd)
{
	struct v4l2_fwnode_link link;
	struct device_node *ep;
	int ret;

	for_each_endpoint_of_node(sd->dev->of_node, ep) {
		ret = v4l2_fwnode_parse_link(of_fwnode_handle(ep), &link);
		if (ret)
			continue;

		ret = create_of_link(imxmd, sd, &link);
		v4l2_fwnode_put_link(&link);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * Create media links to the given CSI subdevice's sink pads,
 * using its device-tree endpoints.
 */
int imx_media_create_csi_of_links(struct imx_media_dev *imxmd,
				  struct v4l2_subdev *csi)
{
	struct device_node *csi_np = csi->dev->of_node;
	struct device_node *ep;

	for_each_child_of_node(csi_np, ep) {
		struct fwnode_handle *fwnode, *csi_ep;
		struct v4l2_fwnode_link link;
		int ret;

		memset(&link, 0, sizeof(link));

		link.local_node = of_fwnode_handle(csi_np);
		link.local_port = CSI_SINK_PAD;

		csi_ep = of_fwnode_handle(ep);

		fwnode = fwnode_graph_get_remote_endpoint(csi_ep);
		if (!fwnode)
			continue;

		fwnode = fwnode_get_parent(fwnode);
		fwnode_property_read_u32(fwnode, "reg", &link.remote_port);
		fwnode = fwnode_get_next_parent(fwnode);
		if (is_of_node(fwnode) &&
		    of_node_cmp(to_of_node(fwnode)->name, "ports") == 0)
			fwnode = fwnode_get_next_parent(fwnode);
		link.remote_node = fwnode;

		ret = create_of_link(imxmd, csi, &link);
		fwnode_handle_put(link.remote_node);
		if (ret)
			return ret;
	}

	return 0;
}
