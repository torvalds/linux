// SPDX-License-Identifier: GPL-2.0-only
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
	V4L2_FWNODE_BUS_TYPE_CSI2_DPHY,
	V4L2_FWNODE_BUS_TYPE_PARALLEL,
	V4L2_FWNODE_BUS_TYPE_BT656,
	NR_OF_V4L2_FWNODE_BUS_TYPE,
};

static const struct v4l2_fwnode_bus_conv {
	enum v4l2_fwnode_bus_type fwnode_bus_type;
	enum v4l2_mbus_type mbus_type;
	const char *name;
} buses[] = {
	{
		V4L2_FWNODE_BUS_TYPE_GUESS,
		V4L2_MBUS_UNKNOWN,
		"not specified",
	}, {
		V4L2_FWNODE_BUS_TYPE_CSI2_CPHY,
		V4L2_MBUS_CSI2_CPHY,
		"MIPI CSI-2 C-PHY",
	}, {
		V4L2_FWNODE_BUS_TYPE_CSI1,
		V4L2_MBUS_CSI1,
		"MIPI CSI-1",
	}, {
		V4L2_FWNODE_BUS_TYPE_CCP2,
		V4L2_MBUS_CCP2,
		"compact camera port 2",
	}, {
		V4L2_FWNODE_BUS_TYPE_CSI2_DPHY,
		V4L2_MBUS_CSI2_DPHY,
		"MIPI CSI-2 D-PHY",
	}, {
		V4L2_FWNODE_BUS_TYPE_PARALLEL,
		V4L2_MBUS_PARALLEL,
		"parallel",
	}, {
		V4L2_FWNODE_BUS_TYPE_BT656,
		V4L2_MBUS_BT656,
		"Bt.656",
	}
};

static const struct v4l2_fwnode_bus_conv *
get_v4l2_fwnode_bus_conv_by_fwnode_bus(enum v4l2_fwnode_bus_type type)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(buses); i++)
		if (buses[i].fwnode_bus_type == type)
			return &buses[i];

	return NULL;
}

static enum v4l2_mbus_type
v4l2_fwnode_bus_type_to_mbus(enum v4l2_fwnode_bus_type type)
{
	const struct v4l2_fwnode_bus_conv *conv =
		get_v4l2_fwnode_bus_conv_by_fwnode_bus(type);

	return conv ? conv->mbus_type : V4L2_MBUS_UNKNOWN;
}

static const char *
v4l2_fwnode_bus_type_to_string(enum v4l2_fwnode_bus_type type)
{
	const struct v4l2_fwnode_bus_conv *conv =
		get_v4l2_fwnode_bus_conv_by_fwnode_bus(type);

	return conv ? conv->name : "not found";
}

static const struct v4l2_fwnode_bus_conv *
get_v4l2_fwnode_bus_conv_by_mbus(enum v4l2_mbus_type type)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(buses); i++)
		if (buses[i].mbus_type == type)
			return &buses[i];

	return NULL;
}

static const char *
v4l2_fwnode_mbus_type_to_string(enum v4l2_mbus_type type)
{
	const struct v4l2_fwnode_bus_conv *conv =
		get_v4l2_fwnode_bus_conv_by_mbus(type);

	return conv ? conv->name : "not found";
}

static int v4l2_fwnode_endpoint_parse_csi2_bus(struct fwnode_handle *fwnode,
					       struct v4l2_fwnode_endpoint *vep,
					       enum v4l2_mbus_type bus_type)
{
	struct v4l2_fwnode_bus_mipi_csi2 *bus = &vep->bus.mipi_csi2;
	bool have_clk_lane = false, have_data_lanes = false,
		have_lane_polarities = false;
	unsigned int flags = 0, lanes_used = 0;
	u32 array[1 + V4L2_FWNODE_CSI2_MAX_DATA_LANES];
	u32 clock_lane = 0;
	unsigned int num_data_lanes = 0;
	bool use_default_lane_mapping = false;
	unsigned int i;
	u32 v;
	int rval;

	if (bus_type == V4L2_MBUS_CSI2_DPHY ||
	    bus_type == V4L2_MBUS_CSI2_CPHY) {
		use_default_lane_mapping = true;

		num_data_lanes = min_t(u32, bus->num_data_lanes,
				       V4L2_FWNODE_CSI2_MAX_DATA_LANES);

		clock_lane = bus->clock_lane;
		if (clock_lane)
			use_default_lane_mapping = false;

		for (i = 0; i < num_data_lanes; i++) {
			array[i] = bus->data_lanes[i];
			if (array[i])
				use_default_lane_mapping = false;
		}

		if (use_default_lane_mapping)
			pr_debug("no lane mapping given, using defaults\n");
	}

	rval = fwnode_property_count_u32(fwnode, "data-lanes");
	if (rval > 0) {
		num_data_lanes =
			min_t(int, V4L2_FWNODE_CSI2_MAX_DATA_LANES, rval);

		fwnode_property_read_u32_array(fwnode, "data-lanes", array,
					       num_data_lanes);

		have_data_lanes = true;
		if (use_default_lane_mapping) {
			pr_debug("data-lanes property exists; disabling default mapping\n");
			use_default_lane_mapping = false;
		}
	}

	for (i = 0; i < num_data_lanes; i++) {
		if (lanes_used & BIT(array[i])) {
			if (have_data_lanes || !use_default_lane_mapping)
				pr_warn("duplicated lane %u in data-lanes, using defaults\n",
					array[i]);
			use_default_lane_mapping = true;
		}
		lanes_used |= BIT(array[i]);

		if (have_data_lanes)
			pr_debug("lane %u position %u\n", i, array[i]);
	}

	rval = fwnode_property_count_u32(fwnode, "lane-polarities");
	if (rval > 0) {
		if (rval != 1 + num_data_lanes /* clock+data */) {
			pr_warn("invalid number of lane-polarities entries (need %u, got %u)\n",
				1 + num_data_lanes, rval);
			return -EINVAL;
		}

		have_lane_polarities = true;
	}

	if (!fwnode_property_read_u32(fwnode, "clock-lanes", &v)) {
		clock_lane = v;
		pr_debug("clock lane position %u\n", v);
		have_clk_lane = true;
	}

	if (have_clk_lane && lanes_used & BIT(clock_lane) &&
	    !use_default_lane_mapping) {
		pr_warn("duplicated lane %u in clock-lanes, using defaults\n",
			v);
		use_default_lane_mapping = true;
	}

	if (fwnode_property_present(fwnode, "clock-noncontinuous")) {
		flags |= V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK;
		pr_debug("non-continuous clock\n");
	} else {
		flags |= V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	}

	if (bus_type == V4L2_MBUS_CSI2_DPHY ||
	    bus_type == V4L2_MBUS_CSI2_CPHY || lanes_used ||
	    have_clk_lane || (flags & ~V4L2_MBUS_CSI2_CONTINUOUS_CLOCK)) {
		/* Only D-PHY has a clock lane. */
		unsigned int dfl_data_lane_index =
			bus_type == V4L2_MBUS_CSI2_DPHY;

		bus->flags = flags;
		if (bus_type == V4L2_MBUS_UNKNOWN)
			vep->bus_type = V4L2_MBUS_CSI2_DPHY;
		bus->num_data_lanes = num_data_lanes;

		if (use_default_lane_mapping) {
			bus->clock_lane = 0;
			for (i = 0; i < num_data_lanes; i++)
				bus->data_lanes[i] = dfl_data_lane_index + i;
		} else {
			bus->clock_lane = clock_lane;
			for (i = 0; i < num_data_lanes; i++)
				bus->data_lanes[i] = array[i];
		}

		if (have_lane_polarities) {
			fwnode_property_read_u32_array(fwnode,
						       "lane-polarities", array,
						       1 + num_data_lanes);

			for (i = 0; i < 1 + num_data_lanes; i++) {
				bus->lane_polarities[i] = array[i];
				pr_debug("lane %u polarity %sinverted",
					 i, array[i] ? "" : "not ");
			}
		} else {
			pr_debug("no lane polarities defined, assuming not inverted\n");
		}
	}

	return 0;
}

#define PARALLEL_MBUS_FLAGS (V4L2_MBUS_HSYNC_ACTIVE_HIGH |	\
			     V4L2_MBUS_HSYNC_ACTIVE_LOW |	\
			     V4L2_MBUS_VSYNC_ACTIVE_HIGH |	\
			     V4L2_MBUS_VSYNC_ACTIVE_LOW |	\
			     V4L2_MBUS_FIELD_EVEN_HIGH |	\
			     V4L2_MBUS_FIELD_EVEN_LOW)

static void
v4l2_fwnode_endpoint_parse_parallel_bus(struct fwnode_handle *fwnode,
					struct v4l2_fwnode_endpoint *vep,
					enum v4l2_mbus_type bus_type)
{
	struct v4l2_fwnode_bus_parallel *bus = &vep->bus.parallel;
	unsigned int flags = 0;
	u32 v;

	if (bus_type == V4L2_MBUS_PARALLEL || bus_type == V4L2_MBUS_BT656)
		flags = bus->flags;

	if (!fwnode_property_read_u32(fwnode, "hsync-active", &v)) {
		flags &= ~(V4L2_MBUS_HSYNC_ACTIVE_HIGH |
			   V4L2_MBUS_HSYNC_ACTIVE_LOW);
		flags |= v ? V4L2_MBUS_HSYNC_ACTIVE_HIGH :
			V4L2_MBUS_HSYNC_ACTIVE_LOW;
		pr_debug("hsync-active %s\n", v ? "high" : "low");
	}

	if (!fwnode_property_read_u32(fwnode, "vsync-active", &v)) {
		flags &= ~(V4L2_MBUS_VSYNC_ACTIVE_HIGH |
			   V4L2_MBUS_VSYNC_ACTIVE_LOW);
		flags |= v ? V4L2_MBUS_VSYNC_ACTIVE_HIGH :
			V4L2_MBUS_VSYNC_ACTIVE_LOW;
		pr_debug("vsync-active %s\n", v ? "high" : "low");
	}

	if (!fwnode_property_read_u32(fwnode, "field-even-active", &v)) {
		flags &= ~(V4L2_MBUS_FIELD_EVEN_HIGH |
			   V4L2_MBUS_FIELD_EVEN_LOW);
		flags |= v ? V4L2_MBUS_FIELD_EVEN_HIGH :
			V4L2_MBUS_FIELD_EVEN_LOW;
		pr_debug("field-even-active %s\n", v ? "high" : "low");
	}

	if (!fwnode_property_read_u32(fwnode, "pclk-sample", &v)) {
		flags &= ~(V4L2_MBUS_PCLK_SAMPLE_RISING |
			   V4L2_MBUS_PCLK_SAMPLE_FALLING);
		flags |= v ? V4L2_MBUS_PCLK_SAMPLE_RISING :
			V4L2_MBUS_PCLK_SAMPLE_FALLING;
		pr_debug("pclk-sample %s\n", v ? "high" : "low");
	}

	if (!fwnode_property_read_u32(fwnode, "data-active", &v)) {
		flags &= ~(V4L2_MBUS_DATA_ACTIVE_HIGH |
			   V4L2_MBUS_DATA_ACTIVE_LOW);
		flags |= v ? V4L2_MBUS_DATA_ACTIVE_HIGH :
			V4L2_MBUS_DATA_ACTIVE_LOW;
		pr_debug("data-active %s\n", v ? "high" : "low");
	}

	if (fwnode_property_present(fwnode, "slave-mode")) {
		pr_debug("slave mode\n");
		flags &= ~V4L2_MBUS_MASTER;
		flags |= V4L2_MBUS_SLAVE;
	} else {
		flags &= ~V4L2_MBUS_SLAVE;
		flags |= V4L2_MBUS_MASTER;
	}

	if (!fwnode_property_read_u32(fwnode, "bus-width", &v)) {
		bus->bus_width = v;
		pr_debug("bus-width %u\n", v);
	}

	if (!fwnode_property_read_u32(fwnode, "data-shift", &v)) {
		bus->data_shift = v;
		pr_debug("data-shift %u\n", v);
	}

	if (!fwnode_property_read_u32(fwnode, "sync-on-green-active", &v)) {
		flags &= ~(V4L2_MBUS_VIDEO_SOG_ACTIVE_HIGH |
			   V4L2_MBUS_VIDEO_SOG_ACTIVE_LOW);
		flags |= v ? V4L2_MBUS_VIDEO_SOG_ACTIVE_HIGH :
			V4L2_MBUS_VIDEO_SOG_ACTIVE_LOW;
		pr_debug("sync-on-green-active %s\n", v ? "high" : "low");
	}

	if (!fwnode_property_read_u32(fwnode, "data-enable-active", &v)) {
		flags &= ~(V4L2_MBUS_DATA_ENABLE_HIGH |
			   V4L2_MBUS_DATA_ENABLE_LOW);
		flags |= v ? V4L2_MBUS_DATA_ENABLE_HIGH :
			V4L2_MBUS_DATA_ENABLE_LOW;
		pr_debug("data-enable-active %s\n", v ? "high" : "low");
	}

	switch (bus_type) {
	default:
		bus->flags = flags;
		if (flags & PARALLEL_MBUS_FLAGS)
			vep->bus_type = V4L2_MBUS_PARALLEL;
		else
			vep->bus_type = V4L2_MBUS_BT656;
		break;
	case V4L2_MBUS_PARALLEL:
		vep->bus_type = V4L2_MBUS_PARALLEL;
		bus->flags = flags;
		break;
	case V4L2_MBUS_BT656:
		vep->bus_type = V4L2_MBUS_BT656;
		bus->flags = flags & ~PARALLEL_MBUS_FLAGS;
		break;
	}
}

static void
v4l2_fwnode_endpoint_parse_csi1_bus(struct fwnode_handle *fwnode,
				    struct v4l2_fwnode_endpoint *vep,
				    enum v4l2_mbus_type bus_type)
{
	struct v4l2_fwnode_bus_mipi_csi1 *bus = &vep->bus.mipi_csi1;
	u32 v;

	if (!fwnode_property_read_u32(fwnode, "clock-inv", &v)) {
		bus->clock_inv = v;
		pr_debug("clock-inv %u\n", v);
	}

	if (!fwnode_property_read_u32(fwnode, "strobe", &v)) {
		bus->strobe = v;
		pr_debug("strobe %u\n", v);
	}

	if (!fwnode_property_read_u32(fwnode, "data-lanes", &v)) {
		bus->data_lane = v;
		pr_debug("data-lanes %u\n", v);
	}

	if (!fwnode_property_read_u32(fwnode, "clock-lanes", &v)) {
		bus->clock_lane = v;
		pr_debug("clock-lanes %u\n", v);
	}

	if (bus_type == V4L2_MBUS_CCP2)
		vep->bus_type = V4L2_MBUS_CCP2;
	else
		vep->bus_type = V4L2_MBUS_CSI1;
}

static int __v4l2_fwnode_endpoint_parse(struct fwnode_handle *fwnode,
					struct v4l2_fwnode_endpoint *vep)
{
	u32 bus_type = V4L2_FWNODE_BUS_TYPE_GUESS;
	enum v4l2_mbus_type mbus_type;
	int rval;

	if (vep->bus_type == V4L2_MBUS_UNKNOWN) {
		/* Zero fields from bus union to until the end */
		memset(&vep->bus, 0,
		       sizeof(*vep) - offsetof(typeof(*vep), bus));
	}

	pr_debug("===== begin parsing endpoint %pfw\n", fwnode);

	/*
	 * Zero the fwnode graph endpoint memory in case we don't end up parsing
	 * the endpoint.
	 */
	memset(&vep->base, 0, sizeof(vep->base));

	fwnode_property_read_u32(fwnode, "bus-type", &bus_type);
	pr_debug("fwnode video bus type %s (%u), mbus type %s (%u)\n",
		 v4l2_fwnode_bus_type_to_string(bus_type), bus_type,
		 v4l2_fwnode_mbus_type_to_string(vep->bus_type),
		 vep->bus_type);
	mbus_type = v4l2_fwnode_bus_type_to_mbus(bus_type);

	if (vep->bus_type != V4L2_MBUS_UNKNOWN) {
		if (mbus_type != V4L2_MBUS_UNKNOWN &&
		    vep->bus_type != mbus_type) {
			pr_debug("expecting bus type %s\n",
				 v4l2_fwnode_mbus_type_to_string(vep->bus_type));
			return -ENXIO;
		}
	} else {
		vep->bus_type = mbus_type;
	}

	switch (vep->bus_type) {
	case V4L2_MBUS_UNKNOWN:
		rval = v4l2_fwnode_endpoint_parse_csi2_bus(fwnode, vep,
							   V4L2_MBUS_UNKNOWN);
		if (rval)
			return rval;

		if (vep->bus_type == V4L2_MBUS_UNKNOWN)
			v4l2_fwnode_endpoint_parse_parallel_bus(fwnode, vep,
								V4L2_MBUS_UNKNOWN);

		pr_debug("assuming media bus type %s (%u)\n",
			 v4l2_fwnode_mbus_type_to_string(vep->bus_type),
			 vep->bus_type);

		break;
	case V4L2_MBUS_CCP2:
	case V4L2_MBUS_CSI1:
		v4l2_fwnode_endpoint_parse_csi1_bus(fwnode, vep, vep->bus_type);

		break;
	case V4L2_MBUS_CSI2_DPHY:
	case V4L2_MBUS_CSI2_CPHY:
		rval = v4l2_fwnode_endpoint_parse_csi2_bus(fwnode, vep,
							   vep->bus_type);
		if (rval)
			return rval;

		break;
	case V4L2_MBUS_PARALLEL:
	case V4L2_MBUS_BT656:
		v4l2_fwnode_endpoint_parse_parallel_bus(fwnode, vep,
							vep->bus_type);

		break;
	default:
		pr_warn("unsupported bus type %u\n", mbus_type);
		return -EINVAL;
	}

	fwnode_graph_parse_endpoint(fwnode, &vep->base);

	return 0;
}

int v4l2_fwnode_endpoint_parse(struct fwnode_handle *fwnode,
			       struct v4l2_fwnode_endpoint *vep)
{
	int ret;

	ret = __v4l2_fwnode_endpoint_parse(fwnode, vep);

	pr_debug("===== end parsing endpoint %pfw\n", fwnode);

	return ret;
}
EXPORT_SYMBOL_GPL(v4l2_fwnode_endpoint_parse);

void v4l2_fwnode_endpoint_free(struct v4l2_fwnode_endpoint *vep)
{
	if (IS_ERR_OR_NULL(vep))
		return;

	kfree(vep->link_frequencies);
	vep->link_frequencies = NULL;
}
EXPORT_SYMBOL_GPL(v4l2_fwnode_endpoint_free);

int v4l2_fwnode_endpoint_alloc_parse(struct fwnode_handle *fwnode,
				     struct v4l2_fwnode_endpoint *vep)
{
	int rval;

	rval = __v4l2_fwnode_endpoint_parse(fwnode, vep);
	if (rval < 0)
		return rval;

	rval = fwnode_property_count_u64(fwnode, "link-frequencies");
	if (rval > 0) {
		unsigned int i;

		vep->link_frequencies =
			kmalloc_array(rval, sizeof(*vep->link_frequencies),
				      GFP_KERNEL);
		if (!vep->link_frequencies)
			return -ENOMEM;

		vep->nr_of_link_frequencies = rval;

		rval = fwnode_property_read_u64_array(fwnode,
						      "link-frequencies",
						      vep->link_frequencies,
						      vep->nr_of_link_frequencies);
		if (rval < 0) {
			v4l2_fwnode_endpoint_free(vep);
			return rval;
		}

		for (i = 0; i < vep->nr_of_link_frequencies; i++)
			pr_info("link-frequencies %u value %llu\n", i,
				vep->link_frequencies[i]);
	}

	pr_debug("===== end parsing endpoint %pfw\n", fwnode);

	return 0;
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
	if (is_of_node(fwnode) && of_node_name_eq(to_of_node(fwnode), "ports"))
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
	if (is_of_node(fwnode) && of_node_name_eq(to_of_node(fwnode), "ports"))
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

static int
v4l2_async_notifier_fwnode_parse_endpoint(struct device *dev,
					  struct v4l2_async_notifier *notifier,
					  struct fwnode_handle *endpoint,
					  unsigned int asd_struct_size,
					  parse_endpoint_func parse_endpoint)
{
	struct v4l2_fwnode_endpoint vep = { .bus_type = 0 };
	struct v4l2_async_subdev *asd;
	int ret;

	asd = kzalloc(asd_struct_size, GFP_KERNEL);
	if (!asd)
		return -ENOMEM;

	asd->match_type = V4L2_ASYNC_MATCH_FWNODE;
	asd->match.fwnode =
		fwnode_graph_get_remote_port_parent(endpoint);
	if (!asd->match.fwnode) {
		dev_dbg(dev, "no remote endpoint found\n");
		ret = -ENOTCONN;
		goto out_err;
	}

	ret = v4l2_fwnode_endpoint_alloc_parse(endpoint, &vep);
	if (ret) {
		dev_warn(dev, "unable to parse V4L2 fwnode endpoint (%d)\n",
			 ret);
		goto out_err;
	}

	ret = parse_endpoint ? parse_endpoint(dev, &vep, asd) : 0;
	if (ret == -ENOTCONN)
		dev_dbg(dev, "ignoring port@%u/endpoint@%u\n", vep.base.port,
			vep.base.id);
	else if (ret < 0)
		dev_warn(dev,
			 "driver could not parse port@%u/endpoint@%u (%d)\n",
			 vep.base.port, vep.base.id, ret);
	v4l2_fwnode_endpoint_free(&vep);
	if (ret < 0)
		goto out_err;

	ret = v4l2_async_notifier_add_subdev(notifier, asd);
	if (ret < 0) {
		/* not an error if asd already exists */
		if (ret == -EEXIST)
			ret = 0;
		goto out_err;
	}

	return 0;

out_err:
	fwnode_handle_put(asd->match.fwnode);
	kfree(asd);

	return ret == -ENOTCONN ? 0 : ret;
}

static int
__v4l2_async_notifier_parse_fwnode_ep(struct device *dev,
				      struct v4l2_async_notifier *notifier,
				      size_t asd_struct_size,
				      unsigned int port,
				      bool has_port,
				      parse_endpoint_func parse_endpoint)
{
	struct fwnode_handle *fwnode;
	int ret = 0;

	if (WARN_ON(asd_struct_size < sizeof(struct v4l2_async_subdev)))
		return -EINVAL;

	fwnode_graph_for_each_endpoint(dev_fwnode(dev), fwnode) {
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

		ret = v4l2_async_notifier_fwnode_parse_endpoint(dev,
								notifier,
								fwnode,
								asd_struct_size,
								parse_endpoint);
		if (ret < 0)
			break;
	}

	fwnode_handle_put(fwnode);

	return ret;
}

int
v4l2_async_notifier_parse_fwnode_endpoints(struct device *dev,
					   struct v4l2_async_notifier *notifier,
					   size_t asd_struct_size,
					   parse_endpoint_func parse_endpoint)
{
	return __v4l2_async_notifier_parse_fwnode_ep(dev, notifier,
						     asd_struct_size, 0,
						     false, parse_endpoint);
}
EXPORT_SYMBOL_GPL(v4l2_async_notifier_parse_fwnode_endpoints);

int
v4l2_async_notifier_parse_fwnode_endpoints_by_port(struct device *dev,
						   struct v4l2_async_notifier *notifier,
						   size_t asd_struct_size,
						   unsigned int port,
						   parse_endpoint_func parse_endpoint)
{
	return __v4l2_async_notifier_parse_fwnode_ep(dev, notifier,
						     asd_struct_size,
						     port, true,
						     parse_endpoint);
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
static int v4l2_fwnode_reference_parse(struct device *dev,
				       struct v4l2_async_notifier *notifier,
				       const char *prop)
{
	struct fwnode_reference_args args;
	unsigned int index;
	int ret;

	for (index = 0;
	     !(ret = fwnode_property_get_reference_args(dev_fwnode(dev),
							prop, NULL, 0,
							index, &args));
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

	for (index = 0;
	     !fwnode_property_get_reference_args(dev_fwnode(dev), prop, NULL,
						 0, index, &args);
	     index++) {
		struct v4l2_async_subdev *asd;

		asd = v4l2_async_notifier_add_fwnode_subdev(notifier,
							    args.fwnode,
							    sizeof(*asd));
		fwnode_handle_put(args.fwnode);
		if (IS_ERR(asd)) {
			/* not an error if asd already exists */
			if (PTR_ERR(asd) == -EEXIST)
				continue;

			return PTR_ERR(asd);
		}
	}

	return 0;
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
 * The child fwnode reached at the end of the iteration is then returned to the
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
 * THIS EXAMPLE EXISTS MERELY TO DOCUMENT THIS FUNCTION. DO NOT USE IT AS A
 * REFERENCE IN HOW ACPI TABLES SHOULD BE WRITTEN!! See documentation under
 * Documentation/acpi/dsd instead and especially graph.txt,
 * data-node-references.txt and leds.txt .
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
static struct fwnode_handle *
v4l2_fwnode_reference_get_int_prop(struct fwnode_handle *fwnode,
				   const char *prop,
				   unsigned int index,
				   const char * const *props,
				   unsigned int nprops)
{
	struct fwnode_reference_args fwnode_args;
	u64 *args = fwnode_args.args;
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

struct v4l2_fwnode_int_props {
	const char *name;
	const char * const *props;
	unsigned int nprops;
};

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
static int
v4l2_fwnode_reference_parse_int_props(struct device *dev,
				      struct v4l2_async_notifier *notifier,
				      const struct v4l2_fwnode_int_props *p)
{
	struct fwnode_handle *fwnode;
	unsigned int index;
	int ret;
	const char *prop = p->name;
	const char * const *props = p->props;
	unsigned int nprops = p->nprops;

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

	for (index = 0;
	     !IS_ERR((fwnode = v4l2_fwnode_reference_get_int_prop(dev_fwnode(dev),
								  prop, index,
								  props,
								  nprops)));
	     index++) {
		struct v4l2_async_subdev *asd;

		asd = v4l2_async_notifier_add_fwnode_subdev(notifier, fwnode,
							    sizeof(*asd));
		fwnode_handle_put(fwnode);
		if (IS_ERR(asd)) {
			ret = PTR_ERR(asd);
			/* not an error if asd already exists */
			if (ret == -EEXIST)
				continue;

			return PTR_ERR(asd);
		}
	}

	return !fwnode || PTR_ERR(fwnode) == -ENOENT ? 0 : PTR_ERR(fwnode);
}

int v4l2_async_notifier_parse_fwnode_sensor_common(struct device *dev,
						   struct v4l2_async_notifier *notifier)
{
	static const char * const led_props[] = { "led" };
	static const struct v4l2_fwnode_int_props props[] = {
		{ "flash-leds", led_props, ARRAY_SIZE(led_props) },
		{ "lens-focus", NULL, 0 },
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(props); i++) {
		int ret;

		if (props[i].props && is_acpi_node(dev_fwnode(dev)))
			ret = v4l2_fwnode_reference_parse_int_props(dev,
								    notifier,
								    &props[i]);
		else
			ret = v4l2_fwnode_reference_parse(dev, notifier,
							  props[i].name);
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

	v4l2_async_notifier_init(notifier);

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

int v4l2_async_register_fwnode_subdev(struct v4l2_subdev *sd,
				      size_t asd_struct_size,
				      unsigned int *ports,
				      unsigned int num_ports,
				      parse_endpoint_func parse_endpoint)
{
	struct v4l2_async_notifier *notifier;
	struct device *dev = sd->dev;
	struct fwnode_handle *fwnode;
	int ret;

	if (WARN_ON(!dev))
		return -ENODEV;

	fwnode = dev_fwnode(dev);
	if (!fwnode_device_is_available(fwnode))
		return -ENODEV;

	notifier = kzalloc(sizeof(*notifier), GFP_KERNEL);
	if (!notifier)
		return -ENOMEM;

	v4l2_async_notifier_init(notifier);

	if (!ports) {
		ret = v4l2_async_notifier_parse_fwnode_endpoints(dev, notifier,
								 asd_struct_size,
								 parse_endpoint);
		if (ret < 0)
			goto out_cleanup;
	} else {
		unsigned int i;

		for (i = 0; i < num_ports; i++) {
			ret = v4l2_async_notifier_parse_fwnode_endpoints_by_port(dev, notifier, asd_struct_size, ports[i], parse_endpoint);
			if (ret < 0)
				goto out_cleanup;
		}
	}

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
EXPORT_SYMBOL_GPL(v4l2_async_register_fwnode_subdev);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sakari Ailus <sakari.ailus@linux.intel.com>");
MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de>");
