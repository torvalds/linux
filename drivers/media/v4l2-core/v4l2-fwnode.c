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
#include <media/v4l2-subdev.h>

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

	if (!fwnode_property_read_u32(fwnode, "data-enable-active", &v))
		flags |= v ? V4L2_MBUS_DATA_ENABLE_HIGH :
			V4L2_MBUS_DATA_ENABLE_LOW;

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

void v4l2_fwnode_endpoint_free(struct v4l2_fwnode_endpoint *vep)
{
	if (IS_ERR_OR_NULL(vep))
		return;

	kfree(vep->link_frequencies);
	kfree(vep);
}
EXPORT_SYMBOL_GPL(v4l2_fwnode_endpoint_free);

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
	asd->match.fwnode =
		fwnode_graph_get_remote_port_parent(endpoint);
	if (!asd->match.fwnode) {
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
	fwnode_handle_put(asd->match.fwnode);
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
		if (!is_available)
			continue;

		if (has_port) {
			struct fwnode_endpoint ep;

			ret = fwnode_graph_parse_endpoint(fwnode, &ep);
			if (ret)
				break;

			if (ep.port != port)
				continue;
		}

		if (WARN_ON(notifier->num_subdevs >= notifier->max_subdevs)) {
			ret = -EINVAL;
			break;
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

/*
 * v4l2_fwnode_reference_parse - parse references for async sub-devices
 * @dev: the device node the properties of which are parsed for references
 * @notifier: the async notifier where the async subdevs will be added
 * @prop: the name of the property
 *
 * Return: 0 on success
 *	   -ENOENT if no entries were found
 *	   -ENOMEM if memory allocation failed
 *	   -EINVAL if property parsing failed
 */
static int v4l2_fwnode_reference_parse(
	struct device *dev, struct v4l2_async_notifier *notifier,
	const char *prop)
{
	struct fwnode_reference_args args;
	unsigned int index;
	int ret;

	for (index = 0;
	     !(ret = fwnode_property_get_reference_args(
		       dev_fwnode(dev), prop, NULL, 0, index, &args));
	     index++)
		fwnode_handle_put(args.fwnode);

	if (!index)
		return -ENOENT;

	/*
	 * Note that right now both -ENODATA and -ENOENT may signal
	 * out-of-bounds access. Return the error in cases other than that.
	 */
	if (ret != -ENOENT && ret != -ENODATA)
		return ret;

	ret = v4l2_async_notifier_realloc(notifier,
					  notifier->num_subdevs + index);
	if (ret)
		return ret;

	for (index = 0; !fwnode_property_get_reference_args(
		     dev_fwnode(dev), prop, NULL, 0, index, &args);
	     index++) {
		struct v4l2_async_subdev *asd;

		if (WARN_ON(notifier->num_subdevs >= notifier->max_subdevs)) {
			ret = -EINVAL;
			goto error;
		}

		asd = kzalloc(sizeof(*asd), GFP_KERNEL);
		if (!asd) {
			ret = -ENOMEM;
			goto error;
		}

		notifier->subdevs[notifier->num_subdevs] = asd;
		asd->match.fwnode = args.fwnode;
		asd->match_type = V4L2_ASYNC_MATCH_FWNODE;
		notifier->num_subdevs++;
	}

	return 0;

error:
	fwnode_handle_put(args.fwnode);
	return ret;
}

/*
 * v4l2_fwnode_reference_get_int_prop - parse a reference with integer
 *					arguments
 * @fwnode: fwnode to read @prop from
 * @notifier: notifier for @dev
 * @prop: the name of the property
 * @index: the index of the reference to get
 * @props: the array of integer property names
 * @nprops: the number of integer property names in @nprops
 *
 * First find an fwnode referred to by the reference at @index in @prop.
 *
 * Then under that fwnode, @nprops times, for each property in @props,
 * iteratively follow child nodes starting from fwnode such that they have the
 * property in @props array at the index of the child node distance from the
 * root node and the value of that property matching with the integer argument
 * of the reference, at the same index.
 *
 * The child fwnode reched at the end of the iteration is then returned to the
 * caller.
 *
 * The core reason for this is that you cannot refer to just any node in ACPI.
 * So to refer to an endpoint (easy in DT) you need to refer to a device, then
 * provide a list of (property name, property value) tuples where each tuple
 * uniquely identifies a child node. The first tuple identifies a child directly
 * underneath the device fwnode, the next tuple identifies a child node
 * underneath the fwnode identified by the previous tuple, etc. until you
 * reached the fwnode you need.
 *
 * An example with a graph, as defined in Documentation/acpi/dsd/graph.txt:
 *
 *	Scope (\_SB.PCI0.I2C2)
 *	{
 *		Device (CAM0)
 *		{
 *			Name (_DSD, Package () {
 *				ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
 *				Package () {
 *					Package () {
 *						"compatible",
 *						Package () { "nokia,smia" }
 *					},
 *				},
 *				ToUUID("dbb8e3e6-5886-4ba6-8795-1319f52a966b"),
 *				Package () {
 *					Package () { "port0", "PRT0" },
 *				}
 *			})
 *			Name (PRT0, Package() {
 *				ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
 *				Package () {
 *					Package () { "port", 0 },
 *				},
 *				ToUUID("dbb8e3e6-5886-4ba6-8795-1319f52a966b"),
 *				Package () {
 *					Package () { "endpoint0", "EP00" },
 *				}
 *			})
 *			Name (EP00, Package() {
 *				ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
 *				Package () {
 *					Package () { "endpoint", 0 },
 *					Package () {
 *						"remote-endpoint",
 *						Package() {
 *							\_SB.PCI0.ISP, 4, 0
 *						}
 *					},
 *				}
 *			})
 *		}
 *	}
 *
 *	Scope (\_SB.PCI0)
 *	{
 *		Device (ISP)
 *		{
 *			Name (_DSD, Package () {
 *				ToUUID("dbb8e3e6-5886-4ba6-8795-1319f52a966b"),
 *				Package () {
 *					Package () { "port4", "PRT4" },
 *				}
 *			})
 *
 *			Name (PRT4, Package() {
 *				ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
 *				Package () {
 *					Package () { "port", 4 },
 *				},
 *				ToUUID("dbb8e3e6-5886-4ba6-8795-1319f52a966b"),
 *				Package () {
 *					Package () { "endpoint0", "EP40" },
 *				}
 *			})
 *
 *			Name (EP40, Package() {
 *				ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
 *				Package () {
 *					Package () { "endpoint", 0 },
 *					Package () {
 *						"remote-endpoint",
 *						Package () {
 *							\_SB.PCI0.I2C2.CAM0,
 *							0, 0
 *						}
 *					},
 *				}
 *			})
 *		}
 *	}
 *
 * From the EP40 node under ISP device, you could parse the graph remote
 * endpoint using v4l2_fwnode_reference_get_int_prop with these arguments:
 *
 *  @fwnode: fwnode referring to EP40 under ISP.
 *  @prop: "remote-endpoint"
 *  @index: 0
 *  @props: "port", "endpoint"
 *  @nprops: 2
 *
 * And you'd get back fwnode referring to EP00 under CAM0.
 *
 * The same works the other way around: if you use EP00 under CAM0 as the
 * fwnode, you'll get fwnode referring to EP40 under ISP.
 *
 * The same example in DT syntax would look like this:
 *
 * cam: cam0 {
 *	compatible = "nokia,smia";
 *
 *	port {
 *		port = <0>;
 *		endpoint {
 *			endpoint = <0>;
 *			remote-endpoint = <&isp 4 0>;
 *		};
 *	};
 * };
 *
 * isp: isp {
 *	ports {
 *		port@4 {
 *			port = <4>;
 *			endpoint {
 *				endpoint = <0>;
 *				remote-endpoint = <&cam 0 0>;
 *			};
 *		};
 *	};
 * };
 *
 * Return: 0 on success
 *	   -ENOENT if no entries (or the property itself) were found
 *	   -EINVAL if property parsing otherwise failed
 *	   -ENOMEM if memory allocation failed
 */
static struct fwnode_handle *v4l2_fwnode_reference_get_int_prop(
	struct fwnode_handle *fwnode, const char *prop, unsigned int index,
	const char * const *props, unsigned int nprops)
{
	struct fwnode_reference_args fwnode_args;
	unsigned int *args = fwnode_args.args;
	struct fwnode_handle *child;
	int ret;

	/*
	 * Obtain remote fwnode as well as the integer arguments.
	 *
	 * Note that right now both -ENODATA and -ENOENT may signal
	 * out-of-bounds access. Return -ENOENT in that case.
	 */
	ret = fwnode_property_get_reference_args(fwnode, prop, NULL, nprops,
						 index, &fwnode_args);
	if (ret)
		return ERR_PTR(ret == -ENODATA ? -ENOENT : ret);

	/*
	 * Find a node in the tree under the referred fwnode corresponding to
	 * the integer arguments.
	 */
	fwnode = fwnode_args.fwnode;
	while (nprops--) {
		u32 val;

		/* Loop over all child nodes under fwnode. */
		fwnode_for_each_child_node(fwnode, child) {
			if (fwnode_property_read_u32(child, *props, &val))
				continue;

			/* Found property, see if its value matches. */
			if (val == *args)
				break;
		}

		fwnode_handle_put(fwnode);

		/* No property found; return an error here. */
		if (!child) {
			fwnode = ERR_PTR(-ENOENT);
			break;
		}

		props++;
		args++;
		fwnode = child;
	}

	return fwnode;
}

/*
 * v4l2_fwnode_reference_parse_int_props - parse references for async
 *					   sub-devices
 * @dev: struct device pointer
 * @notifier: notifier for @dev
 * @prop: the name of the property
 * @props: the array of integer property names
 * @nprops: the number of integer properties
 *
 * Use v4l2_fwnode_reference_get_int_prop to find fwnodes through reference in
 * property @prop with integer arguments with child nodes matching in properties
 * @props. Then, set up V4L2 async sub-devices for those fwnodes in the notifier
 * accordingly.
 *
 * While it is technically possible to use this function on DT, it is only
 * meaningful on ACPI. On Device tree you can refer to any node in the tree but
 * on ACPI the references are limited to devices.
 *
 * Return: 0 on success
 *	   -ENOENT if no entries (or the property itself) were found
 *	   -EINVAL if property parsing otherwisefailed
 *	   -ENOMEM if memory allocation failed
 */
static int v4l2_fwnode_reference_parse_int_props(
	struct device *dev, struct v4l2_async_notifier *notifier,
	const char *prop, const char * const *props, unsigned int nprops)
{
	struct fwnode_handle *fwnode;
	unsigned int index;
	int ret;

	index = 0;
	do {
		fwnode = v4l2_fwnode_reference_get_int_prop(dev_fwnode(dev),
							    prop, index,
							    props, nprops);
		if (IS_ERR(fwnode)) {
			/*
			 * Note that right now both -ENODATA and -ENOENT may
			 * signal out-of-bounds access. Return the error in
			 * cases other than that.
			 */
			if (PTR_ERR(fwnode) != -ENOENT &&
			    PTR_ERR(fwnode) != -ENODATA)
				return PTR_ERR(fwnode);
			break;
		}
		fwnode_handle_put(fwnode);
		index++;
	} while (1);

	ret = v4l2_async_notifier_realloc(notifier,
					  notifier->num_subdevs + index);
	if (ret)
		return -ENOMEM;

	for (index = 0; !IS_ERR((fwnode = v4l2_fwnode_reference_get_int_prop(
					 dev_fwnode(dev), prop, index, props,
					 nprops))); index++) {
		struct v4l2_async_subdev *asd;

		if (WARN_ON(notifier->num_subdevs >= notifier->max_subdevs)) {
			ret = -EINVAL;
			goto error;
		}

		asd = kzalloc(sizeof(struct v4l2_async_subdev), GFP_KERNEL);
		if (!asd) {
			ret = -ENOMEM;
			goto error;
		}

		notifier->subdevs[notifier->num_subdevs] = asd;
		asd->match.fwnode = fwnode;
		asd->match_type = V4L2_ASYNC_MATCH_FWNODE;
		notifier->num_subdevs++;
	}

	return PTR_ERR(fwnode) == -ENOENT ? 0 : PTR_ERR(fwnode);

error:
	fwnode_handle_put(fwnode);
	return ret;
}

int v4l2_async_notifier_parse_fwnode_sensor_common(
	struct device *dev, struct v4l2_async_notifier *notifier)
{
	static const char * const led_props[] = { "led" };
	static const struct {
		const char *name;
		const char * const *props;
		unsigned int nprops;
	} props[] = {
		{ "flash-leds", led_props, ARRAY_SIZE(led_props) },
		{ "lens-focus", NULL, 0 },
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(props); i++) {
		int ret;

		if (props[i].props && is_acpi_node(dev_fwnode(dev)))
			ret = v4l2_fwnode_reference_parse_int_props(
				dev, notifier, props[i].name,
				props[i].props, props[i].nprops);
		else
			ret = v4l2_fwnode_reference_parse(
				dev, notifier, props[i].name);
		if (ret && ret != -ENOENT) {
			dev_warn(dev, "parsing property \"%s\" failed (%d)\n",
				 props[i].name, ret);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_async_notifier_parse_fwnode_sensor_common);

int v4l2_async_register_subdev_sensor_common(struct v4l2_subdev *sd)
{
	struct v4l2_async_notifier *notifier;
	int ret;

	if (WARN_ON(!sd->dev))
		return -ENODEV;

	notifier = kzalloc(sizeof(*notifier), GFP_KERNEL);
	if (!notifier)
		return -ENOMEM;

	ret = v4l2_async_notifier_parse_fwnode_sensor_common(sd->dev,
							     notifier);
	if (ret < 0)
		goto out_cleanup;

	ret = v4l2_async_subdev_notifier_register(sd, notifier);
	if (ret < 0)
		goto out_cleanup;

	ret = v4l2_async_register_subdev(sd);
	if (ret < 0)
		goto out_unregister;

	sd->subdev_notifier = notifier;

	return 0;

out_unregister:
	v4l2_async_notifier_unregister(notifier);

out_cleanup:
	v4l2_async_notifier_cleanup(notifier);
	kfree(notifier);

	return ret;
}
EXPORT_SYMBOL_GPL(v4l2_async_register_subdev_sensor_common);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sakari Ailus <sakari.ailus@linux.intel.com>");
MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de>");
