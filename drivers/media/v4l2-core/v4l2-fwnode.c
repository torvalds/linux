/*
 * V4L2 fwnode binding parsing library
 *
 * The origins of the V4L2 fwnode library are in V4L2 OF library that
 * formerly was located in v4l2-of.c.
 *
 * Copyright (c) 2016 Intel Corporation.
 * Author: Sakari Ailus <sakari.ailus@linux.intel.com>
 *
 * Copyright (C) 2012 - 2013 Samsung Electronics Co., Ltd.
 * Author: Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * Copyright (C) 2012 Renesas Electronics Corp.
 * Author: Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 */
#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include <media/v4l2-async.h>
#include <media/v4l2-fwnode.h>

enum v4l2_fwnode_bus_type {
	V4L2_FWNODE_BUS_TYPE_GUESS = 0,
	V4L2_FWNODE_BUS_TYPE_CSI2_CPHY,
	V4L2_FWNODE_BUS_TYPE_CSI1,
	V4L2_FWNODE_BUS_TYPE_CCP2,
	NR_OF_V4L2_FWNODE_BUS_TYPE,
};

static int v4l2_fwnode_endpoint_parse_csi2_bus(struct fwnode_handle *fwnode,
					       struct v4l2_fwnode_endpoint *vep)
{
	struct v4l2_fwnode_bus_mipi_csi2 *bus = &vep->bus.mipi_csi2;
	bool have_clk_lane = false;
	unsigned int flags = 0, lanes_used = 0;
	unsigned int i;
	u32 v;
	int rval;

	rval = fwnode_property_read_u32_array(fwnode, "data-lanes", NULL, 0);
	if (rval > 0) {
		u32 array[1 + V4L2_FWNODE_CSI2_MAX_DATA_LANES];

		bus->num_data_lanes =
			min_t(int, V4L2_FWNODE_CSI2_MAX_DATA_LANES, rval);

		fwnode_property_read_u32_array(fwnode, "data-lanes", array,
					       bus->num_data_lanes);

		for (i = 0; i < bus->num_data_lanes; i++) {
			if (lanes_used & BIT(array[i]))
				pr_warn("duplicated lane %u in data-lanes\n",
					array[i]);
			lanes_used |= BIT(array[i]);

			bus->data_lanes[i] = array[i];
		}

		rval = fwnode_property_read_u32_array(fwnode,
						      "lane-polarities", NULL,
						      0);
		if (rval > 0) {
			if (rval != 1 + bus->num_data_lanes /* clock+data */) {
				pr_warn("invalid number of lane-polarities entries (need %u, got %u)\n",
					1 + bus->num_data_lanes, rval);
				return -EINVAL;
			}

			fwnode_property_read_u32_array(fwnode,
						       "lane-polarities", array,
						       1 + bus->num_data_lanes);

			for (i = 0; i < 1 + bus->num_data_lanes; i++)
				bus->lane_polarities[i] = array[i];
		}

	}

	if (!fwnode_property_read_u32(fwnode, "clock-lanes", &v)) {
		if (lanes_used & BIT(v))
			pr_warn("duplicated lane %u in clock-lanes\n", v);
		lanes_used |= BIT(v);

		bus->clock_lane = v;
		have_clk_lane = true;
	}

	if (fwnode_property_present(fwnode, "clock-noncontinuous"))
		flags |= V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK;
	else if (have_clk_lane || bus->num_data_lanes > 0)
		flags |= V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	bus->flags = flags;
	vep->bus_type = V4L2_MBUS_CSI2;

	return 0;
}

static void v4l2_fwnode_endpoint_parse_parallel_bus(
	struct fwnode_handle *fwnode, struct v4l2_fwnode_endpoint *vep)
{
	struct v4l2_fwnode_bus_parallel *bus = &vep->bus.parallel;
	unsigned int flags = 0;
	u32 v;

	if (!fwnode_property_read_u32(fwnode, "hsync-active", &v))
		flags |= v ? V4L2_MBUS_HSYNC_ACTIVE_HIGH :
			V4L2_MBUS_HSYNC_ACTIVE_LOW;

	if (!fwnode_property_read_u32(fwnode, "vsync-active", &v))
		flags |= v ? V4L2_MBUS_VSYNC_ACTIVE_HIGH :
			V4L2_MBUS_VSYNC_ACTIVE_LOW;

	if (!fwnode_property_read_u32(fwnode, "field-even-active", &v))
		flags |= v ? V4L2_MBUS_FIELD_EVEN_HIGH :
			V4L2_MBUS_FIELD_EVEN_LOW;
	if (flags)
		vep->bus_type = V4L2_MBUS_PARALLEL;
	else
		vep->bus_type = V4L2_MBUS_BT656;

	if (!fwnode_property_read_u32(fwnode, "pclk-sample", &v))
		flags |= v ? V4L2_MBUS_PCLK_SAMPLE_RISING :
			V4L2_MBUS_PCLK_SAMPLE_FALLING;

	if (!fwnode_property_read_u32(fwnode, "data-active", &v))
		flags |= v ? V4L2_MBUS_DATA_ACTIVE_HIGH :
			V4L2_MBUS_DATA_ACTIVE_LOW;

	if (fwnode_property_present(fwnode, "slave-mode"))
		flags |= V4L2_MBUS_SLAVE;
	else
		flags |= V4L2_MBUS_MASTER;

	if (!fwnode_property_read_u32(fwnode, "bus-width", &v))
		bus->bus_width = v;

	if (!fwnode_property_read_u32(fwnode, "data-shift", &v))
		bus->data_shift = v;

	if (!fwnode_property_read_u32(fwnode, "sync-on-green-active", &v))
		flags |= v ? V4L2_MBUS_VIDEO_SOG_ACTIVE_HIGH :
			V4L2_MBUS_VIDEO_SOG_ACTIVE_LOW;

	bus->flags = flags;

}

static void
v4l2_fwnode_endpoint_parse_csi1_bus(struct fwnode_handle *fwnode,
				    struct v4l2_fwnode_endpoint *vep,
				    u32 bus_type)
{
	struct v4l2_fwnode_bus_mipi_csi1 *bus = &vep->bus.mipi_csi1;
	u32 v;

	if (!fwnode_property_read_u32(fwnode, "clock-inv", &v))
		bus->clock_inv = v;

	if (!fwnode_property_read_u32(fwnode, "strobe", &v))
		bus->strobe = v;

	if (!fwnode_property_read_u32(fwnode, "data-lanes", &v))
		bus->data_lane = v;

	if (!fwnode_property_read_u32(fwnode, "clock-lanes", &v))
		bus->clock_lane = v;

	if (bus_type == V4L2_FWNODE_BUS_TYPE_CCP2)
		vep->bus_type = V4L2_MBUS_CCP2;
	else
		vep->bus_type = V4L2_MBUS_CSI1;
}

/**
 * v4l2_fwnode_endpoint_parse() - parse all fwnode node properties
 * @fwnode: pointer to the endpoint's fwnode handle
 * @vep: pointer to the V4L2 fwnode data structure
 *
 * All properties are optional. If none are found, we don't set any flags. This
 * means the port has a static configuration and no properties have to be
 * specified explicitly. If any properties that identify the bus as parallel
 * are found and slave-mode isn't set, we set V4L2_MBUS_MASTER. Similarly, if
 * we recognise the bus as serial CSI-2 and clock-noncontinuous isn't set, we
 * set the V4L2_MBUS_CSI2_CONTINUOUS_CLOCK flag. The caller should hold a
 * reference to @fwnode.
 *
 * NOTE: This function does not parse properties the size of which is variable
 * without a low fixed limit. Please use v4l2_fwnode_endpoint_alloc_parse() in
 * new drivers instead.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int v4l2_fwnode_endpoint_parse(struct fwnode_handle *fwnode,
			       struct v4l2_fwnode_endpoint *vep)
{
	u32 bus_type = 0;
	int rval;

	fwnode_graph_parse_endpoint(fwnode, &vep->base);

	/* Zero fields from bus_type to until the end */
	memset(&vep->bus_type, 0, sizeof(*vep) -
	       offsetof(typeof(*vep), bus_type));

	fwnode_property_read_u32(fwnode, "bus-type", &bus_type);

	switch (bus_type) {
	case V4L2_FWNODE_BUS_TYPE_GUESS:
		rval = v4l2_fwnode_endpoint_parse_csi2_bus(fwnode, vep);
		if (rval)
			return rval;
		/*
		 * Parse the parallel video bus properties only if none
		 * of the MIPI CSI-2 specific properties were found.
		 */
		if (vep->bus.mipi_csi2.flags == 0)
			v4l2_fwnode_endpoint_parse_parallel_bus(fwnode, vep);

		return 0;
	case V4L2_FWNODE_BUS_TYPE_CCP2:
	case V4L2_FWNODE_BUS_TYPE_CSI1:
		v4l2_fwnode_endpoint_parse_csi1_bus(fwnode, vep, bus_type);

		return 0;
	default:
		pr_warn("unsupported bus type %u\n", bus_type);
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(v4l2_fwnode_endpoint_parse);

/*
 * v4l2_fwnode_endpoint_free() - free the V4L2 fwnode acquired by
 * v4l2_fwnode_endpoint_alloc_parse()
 * @vep - the V4L2 fwnode the resources of which are to be released
 *
 * It is safe to call this function with NULL argument or on a V4L2 fwnode the
 * parsing of which failed.
 */
void v4l2_fwnode_endpoint_free(struct v4l2_fwnode_endpoint *vep)
{
	if (IS_ERR_OR_NULL(vep))
		return;

	kfree(vep->link_frequencies);
	kfree(vep);
}
EXPORT_SYMBOL_GPL(v4l2_fwnode_endpoint_free);

/**
 * v4l2_fwnode_endpoint_alloc_parse() - parse all fwnode node properties
 * @fwnode: pointer to the endpoint's fwnode handle
 *
 * All properties are optional. If none are found, we don't set any flags. This
 * means the port has a static configuration and no properties have to be
 * specified explicitly. If any properties that identify the bus as parallel
 * are found and slave-mode isn't set, we set V4L2_MBUS_MASTER. Similarly, if
 * we recognise the bus as serial CSI-2 and clock-noncontinuous isn't set, we
 * set the V4L2_MBUS_CSI2_CONTINUOUS_CLOCK flag. The caller should hold a
 * reference to @fwnode.
 *
 * v4l2_fwnode_endpoint_alloc_parse() has two important differences to
 * v4l2_fwnode_endpoint_parse():
 *
 * 1. It also parses variable size data.
 *
 * 2. The memory it has allocated to store the variable size data must be freed
 *    using v4l2_fwnode_endpoint_free() when no longer needed.
 *
 * Return: Pointer to v4l2_fwnode_endpoint if successful, on an error pointer
 * on error.
 */
struct v4l2_fwnode_endpoint *v4l2_fwnode_endpoint_alloc_parse(
	struct fwnode_handle *fwnode)
{
	struct v4l2_fwnode_endpoint *vep;
	int rval;

	vep = kzalloc(sizeof(*vep), GFP_KERNEL);
	if (!vep)
		return ERR_PTR(-ENOMEM);

	rval = v4l2_fwnode_endpoint_parse(fwnode, vep);
	if (rval < 0)
		goto out_err;

	rval = fwnode_property_read_u64_array(fwnode, "link-frequencies",
					      NULL, 0);
	if (rval > 0) {
		vep->link_frequencies =
			kmalloc_array(rval, sizeof(*vep->link_frequencies),
				      GFP_KERNEL);
		if (!vep->link_frequencies) {
			rval = -ENOMEM;
			goto out_err;
		}

		vep->nr_of_link_frequencies = rval;

		rval = fwnode_property_read_u64_array(
			fwnode, "link-frequencies", vep->link_frequencies,
			vep->nr_of_link_frequencies);
		if (rval < 0)
			goto out_err;
	}

	return vep;

out_err:
	v4l2_fwnode_endpoint_free(vep);
	return ERR_PTR(rval);
}
EXPORT_SYMBOL_GPL(v4l2_fwnode_endpoint_alloc_parse);

/**
 * v4l2_fwnode_endpoint_parse_link() - parse a link between two endpoints
 * @__fwnode: pointer to the endpoint's fwnode at the local end of the link
 * @link: pointer to the V4L2 fwnode link data structure
 *
 * Fill the link structure with the local and remote nodes and port numbers.
 * The local_node and remote_node fields are set to point to the local and
 * remote port's parent nodes respectively (the port parent node being the
 * parent node of the port node if that node isn't a 'ports' node, or the
 * grand-parent node of the port node otherwise).
 *
 * A reference is taken to both the local and remote nodes, the caller must use
 * v4l2_fwnode_endpoint_put_link() to drop the references when done with the
 * link.
 *
 * Return: 0 on success, or -ENOLINK if the remote endpoint fwnode can't be
 * found.
 */
int v4l2_fwnode_parse_link(struct fwnode_handle *__fwnode,
			   struct v4l2_fwnode_link *link)
{
	const char *port_prop = is_of_node(__fwnode) ? "reg" : "port";
	struct fwnode_handle *fwnode;

	memset(link, 0, sizeof(*link));

	fwnode = fwnode_get_parent(__fwnode);
	fwnode_property_read_u32(fwnode, port_prop, &link->local_port);
	fwnode = fwnode_get_next_parent(fwnode);
	if (is_of_node(fwnode) &&
	    of_node_cmp(to_of_node(fwnode)->name, "ports") == 0)
		fwnode = fwnode_get_next_parent(fwnode);
	link->local_node = fwnode;

	fwnode = fwnode_graph_get_remote_endpoint(__fwnode);
	if (!fwnode) {
		fwnode_handle_put(fwnode);
		return -ENOLINK;
	}

	fwnode = fwnode_get_parent(fwnode);
	fwnode_property_read_u32(fwnode, port_prop, &link->remote_port);
	fwnode = fwnode_get_next_parent(fwnode);
	if (is_of_node(fwnode) &&
	    of_node_cmp(to_of_node(fwnode)->name, "ports") == 0)
		fwnode = fwnode_get_next_parent(fwnode);
	link->remote_node = fwnode;

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_fwnode_parse_link);

/**
 * v4l2_fwnode_put_link() - drop references to nodes in a link
 * @link: pointer to the V4L2 fwnode link data structure
 *
 * Drop references to the local and remote nodes in the link. This function
 * must be called on every link parsed with v4l2_fwnode_parse_link().
 */
void v4l2_fwnode_put_link(struct v4l2_fwnode_link *link)
{
	fwnode_handle_put(link->local_node);
	fwnode_handle_put(link->remote_node);
}
EXPORT_SYMBOL_GPL(v4l2_fwnode_put_link);

static int v4l2_async_notifier_realloc(struct v4l2_async_notifier *notifier,
				       unsigned int max_subdevs)
{
	struct v4l2_async_subdev **subdevs;

	if (max_subdevs <= notifier->max_subdevs)
		return 0;

	subdevs = kvmalloc_array(
		max_subdevs, sizeof(*notifier->subdevs),
		GFP_KERNEL | __GFP_ZERO);
	if (!subdevs)
		return -ENOMEM;

	if (notifier->subdevs) {
		memcpy(subdevs, notifier->subdevs,
		       sizeof(*subdevs) * notifier->num_subdevs);

		kvfree(notifier->subdevs);
	}

	notifier->subdevs = subdevs;
	notifier->max_subdevs = max_subdevs;

	return 0;
}

static int v4l2_async_notifier_fwnode_parse_endpoint(
	struct device *dev, struct v4l2_async_notifier *notifier,
	struct fwnode_handle *endpoint, unsigned int asd_struct_size,
	int (*parse_endpoint)(struct device *dev,
			    struct v4l2_fwnode_endpoint *vep,
			    struct v4l2_async_subdev *asd))
{
	struct v4l2_async_subdev *asd;
	struct v4l2_fwnode_endpoint *vep;
	int ret = 0;

	asd = kzalloc(asd_struct_size, GFP_KERNEL);
	if (!asd)
		return -ENOMEM;

	asd->match_type = V4L2_ASYNC_MATCH_FWNODE;
	asd->match.fwnode.fwnode =
		fwnode_graph_get_remote_port_parent(endpoint);
	if (!asd->match.fwnode.fwnode) {
		dev_warn(dev, "bad remote port parent\n");
		ret = -EINVAL;
		goto out_err;
	}

	vep = v4l2_fwnode_endpoint_alloc_parse(endpoint);
	if (IS_ERR(vep)) {
		ret = PTR_ERR(vep);
		dev_warn(dev, "unable to parse V4L2 fwnode endpoint (%d)\n",
			 ret);
		goto out_err;
	}

	ret = parse_endpoint ? parse_endpoint(dev, vep, asd) : 0;
	if (ret == -ENOTCONN)
		dev_dbg(dev, "ignoring port@%u/endpoint@%u\n", vep->base.port,
			vep->base.id);
	else if (ret < 0)
		dev_warn(dev,
			 "driver could not parse port@%u/endpoint@%u (%d)\n",
			 vep->base.port, vep->base.id, ret);
	v4l2_fwnode_endpoint_free(vep);
	if (ret < 0)
		goto out_err;

	notifier->subdevs[notifier->num_subdevs] = asd;
	notifier->num_subdevs++;

	return 0;

out_err:
	fwnode_handle_put(asd->match.fwnode.fwnode);
	kfree(asd);

	return ret == -ENOTCONN ? 0 : ret;
}

static int __v4l2_async_notifier_parse_fwnode_endpoints(
	struct device *dev, struct v4l2_async_notifier *notifier,
	size_t asd_struct_size, unsigned int port, bool has_port,
	int (*parse_endpoint)(struct device *dev,
			    struct v4l2_fwnode_endpoint *vep,
			    struct v4l2_async_subdev *asd))
{
	struct fwnode_handle *fwnode;
	unsigned int max_subdevs = notifier->max_subdevs;
	int ret;

	if (WARN_ON(asd_struct_size < sizeof(struct v4l2_async_subdev)))
		return -EINVAL;

	for (fwnode = NULL; (fwnode = fwnode_graph_get_next_endpoint(
				     dev_fwnode(dev), fwnode)); ) {
		struct fwnode_handle *dev_fwnode;
		bool is_available;

		dev_fwnode = fwnode_graph_get_port_parent(fwnode);
		is_available = fwnode_device_is_available(dev_fwnode);
		fwnode_handle_put(dev_fwnode);
		if (!is_available)
			continue;

		if (has_port) {
			struct fwnode_endpoint ep;

			ret = fwnode_graph_parse_endpoint(fwnode, &ep);
			if (ret) {
				fwnode_handle_put(fwnode);
				return ret;
			}

			if (ep.port != port)
				continue;
		}
		max_subdevs++;
	}

	/* No subdevs to add? Return here. */
	if (max_subdevs == notifier->max_subdevs)
		return 0;

	ret = v4l2_async_notifier_realloc(notifier, max_subdevs);
	if (ret)
		return ret;

	for (fwnode = NULL; (fwnode = fwnode_graph_get_next_endpoint(
				     dev_fwnode(dev), fwnode)); ) {
		struct fwnode_handle *dev_fwnode;
		bool is_available;

		dev_fwnode = fwnode_graph_get_port_parent(fwnode);
		is_available = fwnode_device_is_available(dev_fwnode);
		fwnode_handle_put(dev_fwnode);

		if (!fwnode_device_is_available(dev_fwnode))
			continue;

		if (WARN_ON(notifier->num_subdevs >= notifier->max_subdevs)) {
			ret = -EINVAL;
			break;
		}

		if (has_port) {
			struct fwnode_endpoint ep;

			ret = fwnode_graph_parse_endpoint(fwnode, &ep);
			if (ret)
				break;

			if (ep.port != port)
				continue;
		}

		ret = v4l2_async_notifier_fwnode_parse_endpoint(
			dev, notifier, fwnode, asd_struct_size, parse_endpoint);
		if (ret < 0)
			break;
	}

	fwnode_handle_put(fwnode);

	return ret;
}

int v4l2_async_notifier_parse_fwnode_endpoints(
	struct device *dev, struct v4l2_async_notifier *notifier,
	size_t asd_struct_size,
	int (*parse_endpoint)(struct device *dev,
			    struct v4l2_fwnode_endpoint *vep,
			    struct v4l2_async_subdev *asd))
{
	return __v4l2_async_notifier_parse_fwnode_endpoints(
		dev, notifier, asd_struct_size, 0, false, parse_endpoint);
}
EXPORT_SYMBOL_GPL(v4l2_async_notifier_parse_fwnode_endpoints);

int v4l2_async_notifier_parse_fwnode_endpoints_by_port(
	struct device *dev, struct v4l2_async_notifier *notifier,
	size_t asd_struct_size, unsigned int port,
	int (*parse_endpoint)(struct device *dev,
			    struct v4l2_fwnode_endpoint *vep,
			    struct v4l2_async_subdev *asd))
{
	return __v4l2_async_notifier_parse_fwnode_endpoints(
		dev, notifier, asd_struct_size, port, true, parse_endpoint);
}
EXPORT_SYMBOL_GPL(v4l2_async_notifier_parse_fwnode_endpoints_by_port);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sakari Ailus <sakari.ailus@linux.intel.com>");
MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de>");
