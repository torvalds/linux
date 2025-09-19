// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

/*
 * MIPI Discovery And Configuration (DisCo) Specification for SoundWire
 * specifies properties to be implemented for SoundWire Masters and Slaves.
 * The DisCo spec doesn't mandate these properties. However, SDW bus cannot
 * work without knowing these values.
 *
 * The helper functions read the Master and Slave properties. Implementers
 * of Master or Slave drivers can use any of the below three mechanisms:
 *    a) Use these APIs here as .read_prop() callback for Master and Slave
 *    b) Implement own methods and set those as .read_prop(), but invoke
 *    APIs in this file for generic read and override the values with
 *    platform specific data
 *    c) Implement ones own methods which do not use anything provided
 *    here
 */

#include <linux/device.h>
#include <linux/property.h>
#include <linux/mod_devicetable.h>
#include <linux/soundwire/sdw.h>
#include "bus.h"

static bool mipi_fwnode_property_read_bool(const struct fwnode_handle *fwnode,
					   const char *propname)
{
	int ret;
	u8 val;

	if (!fwnode_property_present(fwnode, propname))
		return false;
	ret = fwnode_property_read_u8_array(fwnode, propname, &val, 1);
	if (ret < 0)
		return false;
	return !!val;
}

static bool mipi_device_property_read_bool(const struct device *dev,
					   const char *propname)
{
	return mipi_fwnode_property_read_bool(dev_fwnode(dev), propname);
}

/**
 * sdw_master_read_prop() - Read Master properties
 * @bus: SDW bus instance
 */
int sdw_master_read_prop(struct sdw_bus *bus)
{
	struct sdw_master_prop *prop = &bus->prop;
	struct fwnode_handle *link;
	const char *scales_prop;
	char name[32];
	int nval;
	int ret;
	int i;

	device_property_read_u32(bus->dev,
				 "mipi-sdw-sw-interface-revision",
				 &prop->revision);

	/* Find master handle */
	snprintf(name, sizeof(name),
		 "mipi-sdw-link-%d-subproperties", bus->link_id);

	link = device_get_named_child_node(bus->dev, name);
	if (!link) {
		dev_err(bus->dev, "Master node %s not found\n", name);
		return -EIO;
	}

	if (mipi_fwnode_property_read_bool(link,
				      "mipi-sdw-clock-stop-mode0-supported"))
		prop->clk_stop_modes |= BIT(SDW_CLK_STOP_MODE0);

	if (mipi_fwnode_property_read_bool(link,
				      "mipi-sdw-clock-stop-mode1-supported"))
		prop->clk_stop_modes |= BIT(SDW_CLK_STOP_MODE1);

	fwnode_property_read_u32(link,
				 "mipi-sdw-max-clock-frequency",
				 &prop->max_clk_freq);

	nval = fwnode_property_count_u32(link, "mipi-sdw-clock-frequencies-supported");
	if (nval > 0) {
		prop->num_clk_freq = nval;
		prop->clk_freq = devm_kcalloc(bus->dev, prop->num_clk_freq,
					      sizeof(*prop->clk_freq),
					      GFP_KERNEL);
		if (!prop->clk_freq) {
			fwnode_handle_put(link);
			return -ENOMEM;
		}

		ret = fwnode_property_read_u32_array(link,
				"mipi-sdw-clock-frequencies-supported",
				prop->clk_freq, prop->num_clk_freq);
		if (ret < 0)
			return ret;
	}

	/*
	 * Check the frequencies supported. If FW doesn't provide max
	 * freq, then populate here by checking values.
	 */
	if (!prop->max_clk_freq && prop->clk_freq) {
		prop->max_clk_freq = prop->clk_freq[0];
		for (i = 1; i < prop->num_clk_freq; i++) {
			if (prop->clk_freq[i] > prop->max_clk_freq)
				prop->max_clk_freq = prop->clk_freq[i];
		}
	}

	scales_prop = "mipi-sdw-supported-clock-scales";
	nval = fwnode_property_count_u32(link, scales_prop);
	if (nval == 0) {
		scales_prop = "mipi-sdw-supported-clock-gears";
		nval = fwnode_property_count_u32(link, scales_prop);
	}
	if (nval > 0) {
		prop->num_clk_gears = nval;
		prop->clk_gears = devm_kcalloc(bus->dev, prop->num_clk_gears,
					       sizeof(*prop->clk_gears),
					       GFP_KERNEL);
		if (!prop->clk_gears) {
			fwnode_handle_put(link);
			return -ENOMEM;
		}

		ret = fwnode_property_read_u32_array(link,
					       scales_prop,
					       prop->clk_gears,
					       prop->num_clk_gears);
		if (ret < 0)
			return ret;
	}

	fwnode_property_read_u32(link, "mipi-sdw-default-frame-rate",
				 &prop->default_frame_rate);

	fwnode_property_read_u32(link, "mipi-sdw-default-frame-row-size",
				 &prop->default_row);

	fwnode_property_read_u32(link, "mipi-sdw-default-frame-col-size",
				 &prop->default_col);

	prop->dynamic_frame =  mipi_fwnode_property_read_bool(link,
			"mipi-sdw-dynamic-frame-shape");

	fwnode_property_read_u32(link, "mipi-sdw-command-error-threshold",
				 &prop->err_threshold);

	fwnode_handle_put(link);

	return 0;
}
EXPORT_SYMBOL(sdw_master_read_prop);

static int sdw_slave_read_dp0(struct sdw_slave *slave,
			      struct fwnode_handle *port,
			      struct sdw_dp0_prop *dp0)
{
	int nval;
	int ret;

	fwnode_property_read_u32(port, "mipi-sdw-port-max-wordlength",
				 &dp0->max_word);

	fwnode_property_read_u32(port, "mipi-sdw-port-min-wordlength",
				 &dp0->min_word);

	nval = fwnode_property_count_u32(port, "mipi-sdw-port-wordlength-configs");
	if (nval > 0) {

		dp0->num_words = nval;
		dp0->words = devm_kcalloc(&slave->dev,
					  dp0->num_words, sizeof(*dp0->words),
					  GFP_KERNEL);
		if (!dp0->words)
			return -ENOMEM;

		ret = fwnode_property_read_u32_array(port,
				"mipi-sdw-port-wordlength-configs",
				dp0->words, dp0->num_words);
		if (ret < 0)
			return ret;
	}

	dp0->BRA_flow_controlled = mipi_fwnode_property_read_bool(port,
				"mipi-sdw-bra-flow-controlled");

	dp0->simple_ch_prep_sm = mipi_fwnode_property_read_bool(port,
				"mipi-sdw-simplified-channel-prepare-sm");

	dp0->imp_def_interrupts = mipi_fwnode_property_read_bool(port,
				"mipi-sdw-imp-def-dp0-interrupts-supported");

	nval = fwnode_property_count_u32(port, "mipi-sdw-lane-list");
	if (nval > 0) {
		dp0->num_lanes = nval;
		dp0->lane_list = devm_kcalloc(&slave->dev,
					      dp0->num_lanes, sizeof(*dp0->lane_list),
					      GFP_KERNEL);
		if (!dp0->lane_list)
			return -ENOMEM;

		ret = fwnode_property_read_u32_array(port,
					       "mipi-sdw-lane-list",
					       dp0->lane_list, dp0->num_lanes);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int sdw_slave_read_dpn(struct sdw_slave *slave,
			      struct sdw_dpn_prop *dpn, int count, int ports,
			      char *type)
{
	struct fwnode_handle *node;
	u32 bit, i = 0;
	unsigned long addr;
	char name[40];
	int nval;
	int ret;

	addr = ports;
	/* valid ports are 1 to 14 so apply mask */
	addr &= GENMASK(14, 1);

	for_each_set_bit(bit, &addr, 32) {
		snprintf(name, sizeof(name),
			 "mipi-sdw-dp-%d-%s-subproperties", bit, type);

		dpn[i].num = bit;

		node = device_get_named_child_node(&slave->dev, name);
		if (!node) {
			dev_err(&slave->dev, "%s dpN not found\n", name);
			return -EIO;
		}

		fwnode_property_read_u32(node, "mipi-sdw-port-max-wordlength",
					 &dpn[i].max_word);
		fwnode_property_read_u32(node, "mipi-sdw-port-min-wordlength",
					 &dpn[i].min_word);

		nval = fwnode_property_count_u32(node, "mipi-sdw-port-wordlength-configs");
		if (nval > 0) {
			dpn[i].num_words = nval;
			dpn[i].words = devm_kcalloc(&slave->dev,
						    dpn[i].num_words,
						    sizeof(*dpn[i].words),
						    GFP_KERNEL);
			if (!dpn[i].words) {
				fwnode_handle_put(node);
				return -ENOMEM;
			}

			ret = fwnode_property_read_u32_array(node,
					"mipi-sdw-port-wordlength-configs",
					dpn[i].words, dpn[i].num_words);
			if (ret < 0)
				return ret;
		}

		fwnode_property_read_u32(node, "mipi-sdw-data-port-type",
					 &dpn[i].type);

		fwnode_property_read_u32(node,
					 "mipi-sdw-max-grouping-supported",
					 &dpn[i].max_grouping);

		dpn[i].simple_ch_prep_sm = mipi_fwnode_property_read_bool(node,
				"mipi-sdw-simplified-channelprepare-sm");

		fwnode_property_read_u32(node,
					 "mipi-sdw-port-channelprepare-timeout",
					 &dpn[i].ch_prep_timeout);

		fwnode_property_read_u32(node,
				"mipi-sdw-imp-def-dpn-interrupts-supported",
				&dpn[i].imp_def_interrupts);

		fwnode_property_read_u32(node, "mipi-sdw-min-channel-number",
					 &dpn[i].min_ch);

		fwnode_property_read_u32(node, "mipi-sdw-max-channel-number",
					 &dpn[i].max_ch);

		nval = fwnode_property_count_u32(node, "mipi-sdw-channel-number-list");
		if (nval > 0) {
			dpn[i].num_channels = nval;
			dpn[i].channels = devm_kcalloc(&slave->dev,
						       dpn[i].num_channels,
						       sizeof(*dpn[i].channels),
						 GFP_KERNEL);
			if (!dpn[i].channels) {
				fwnode_handle_put(node);
				return -ENOMEM;
			}

			ret = fwnode_property_read_u32_array(node,
					"mipi-sdw-channel-number-list",
					dpn[i].channels, dpn[i].num_channels);
			if (ret < 0)
				return ret;
		}

		nval = fwnode_property_count_u32(node, "mipi-sdw-channel-combination-list");
		if (nval > 0) {
			dpn[i].num_ch_combinations = nval;
			dpn[i].ch_combinations = devm_kcalloc(&slave->dev,
					dpn[i].num_ch_combinations,
					sizeof(*dpn[i].ch_combinations),
					GFP_KERNEL);
			if (!dpn[i].ch_combinations) {
				fwnode_handle_put(node);
				return -ENOMEM;
			}

			ret = fwnode_property_read_u32_array(node,
					"mipi-sdw-channel-combination-list",
					dpn[i].ch_combinations,
					dpn[i].num_ch_combinations);
			if (ret < 0)
				return ret;
		}

		fwnode_property_read_u32(node,
				"mipi-sdw-modes-supported", &dpn[i].modes);

		fwnode_property_read_u32(node, "mipi-sdw-max-async-buffer",
					 &dpn[i].max_async_buffer);

		dpn[i].block_pack_mode = mipi_fwnode_property_read_bool(node,
				"mipi-sdw-block-packing-mode");

		fwnode_property_read_u32(node, "mipi-sdw-port-encoding-type",
					 &dpn[i].port_encoding);

		nval = fwnode_property_count_u32(node, "mipi-sdw-lane-list");
		if (nval > 0) {
			dpn[i].num_lanes = nval;
			dpn[i].lane_list = devm_kcalloc(&slave->dev,
							dpn[i].num_lanes, sizeof(*dpn[i].lane_list),
							GFP_KERNEL);
			if (!dpn[i].lane_list)
				return -ENOMEM;

			ret = fwnode_property_read_u32_array(node,
						       "mipi-sdw-lane-list",
						       dpn[i].lane_list, dpn[i].num_lanes);
			if (ret < 0)
				return ret;
		}

		fwnode_handle_put(node);

		i++;
	}

	return 0;
}

/*
 * In MIPI DisCo spec for SoundWire, lane mapping for a slave device is done with
 * mipi-sdw-lane-x-mapping properties, where x is 1..7, and the values for those
 * properties are mipi-sdw-manager-lane-x or mipi-sdw-peripheral-link-y, where x
 * is an integer between 1 to 7 if the lane is connected to a manager lane, y is a
 * character between A to E if the lane is connected to another peripheral lane.
 */
int sdw_slave_read_lane_mapping(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	struct device *dev = &slave->dev;
	char prop_name[30];
	const char *prop_val;
	size_t len;
	int ret, i;
	u8 lane;

	for (i = 0; i < SDW_MAX_LANES; i++) {
		snprintf(prop_name, sizeof(prop_name), "mipi-sdw-lane-%d-mapping", i);
		ret = device_property_read_string(dev, prop_name, &prop_val);
		if (ret)
			continue;

		len = strlen(prop_val);
		if (len < 1)
			return -EINVAL;

		/* The last character is enough to identify the connection */
		ret = kstrtou8(&prop_val[len - 1], 10, &lane);
		if (ret)
			return ret;
		if (in_range(lane, 1, SDW_MAX_LANES - 1))
			prop->lane_maps[i] = lane;
	}
	return 0;
}
EXPORT_SYMBOL(sdw_slave_read_lane_mapping);

/**
 * sdw_slave_read_prop() - Read Slave properties
 * @slave: SDW Slave
 */
int sdw_slave_read_prop(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	struct device *dev = &slave->dev;
	struct fwnode_handle *port;
	int nval;
	int ret;

	device_property_read_u32(dev, "mipi-sdw-sw-interface-revision",
				 &prop->mipi_revision);

	prop->wake_capable = mipi_device_property_read_bool(dev,
				"mipi-sdw-wake-up-unavailable");
	prop->wake_capable = !prop->wake_capable;

	prop->test_mode_capable = mipi_device_property_read_bool(dev,
				"mipi-sdw-test-mode-supported");

	prop->clk_stop_mode1 = false;
	if (mipi_device_property_read_bool(dev,
				"mipi-sdw-clock-stop-mode1-supported"))
		prop->clk_stop_mode1 = true;

	prop->simple_clk_stop_capable = mipi_device_property_read_bool(dev,
			"mipi-sdw-simplified-clockstopprepare-sm-supported");

	device_property_read_u32(dev, "mipi-sdw-clockstopprepare-timeout",
				 &prop->clk_stop_timeout);

	ret = device_property_read_u32(dev, "mipi-sdw-peripheral-channelprepare-timeout",
				       &prop->ch_prep_timeout);
	if (ret < 0)
		device_property_read_u32(dev, "mipi-sdw-slave-channelprepare-timeout",
					 &prop->ch_prep_timeout);

	device_property_read_u32(dev,
			"mipi-sdw-clockstopprepare-hard-reset-behavior",
			&prop->reset_behave);

	prop->high_PHY_capable = mipi_device_property_read_bool(dev,
			"mipi-sdw-highPHY-capable");

	prop->paging_support = mipi_device_property_read_bool(dev,
			"mipi-sdw-paging-supported");

	prop->bank_delay_support = mipi_device_property_read_bool(dev,
			"mipi-sdw-bank-delay-supported");

	device_property_read_u32(dev,
			"mipi-sdw-port15-read-behavior", &prop->p15_behave);

	device_property_read_u32(dev, "mipi-sdw-master-count",
				 &prop->master_count);

	device_property_read_u32(dev, "mipi-sdw-source-port-list",
				 &prop->source_ports);

	device_property_read_u32(dev, "mipi-sdw-sink-port-list",
				 &prop->sink_ports);

	device_property_read_u32(dev, "mipi-sdw-sdca-interrupt-register-list",
				 &prop->sdca_interrupt_register_list);

	prop->commit_register_supported = mipi_device_property_read_bool(dev,
			"mipi-sdw-commit-register-supported");

	/*
	 * Read dp0 properties - we don't rely on the 'mipi-sdw-dp-0-supported'
	 * property since the 'mipi-sdw-dp0-subproperties' property is logically
	 * equivalent.
	 */
	port = device_get_named_child_node(dev, "mipi-sdw-dp-0-subproperties");
	if (!port) {
		dev_dbg(dev, "DP0 node not found!!\n");
	} else {
		prop->dp0_prop = devm_kzalloc(&slave->dev,
					      sizeof(*prop->dp0_prop),
					      GFP_KERNEL);
		if (!prop->dp0_prop) {
			fwnode_handle_put(port);
			return -ENOMEM;
		}

		sdw_slave_read_dp0(slave, port, prop->dp0_prop);

		fwnode_handle_put(port);
	}

	/*
	 * Based on each DPn port, get source and sink dpn properties.
	 * Also, some ports can operate as both source or sink.
	 */

	/* Allocate memory for set bits in port lists */
	nval = hweight32(prop->source_ports);
	prop->src_dpn_prop = devm_kcalloc(&slave->dev, nval,
					  sizeof(*prop->src_dpn_prop),
					  GFP_KERNEL);
	if (!prop->src_dpn_prop)
		return -ENOMEM;

	/* Read dpn properties for source port(s) */
	sdw_slave_read_dpn(slave, prop->src_dpn_prop, nval,
			   prop->source_ports, "source");

	nval = hweight32(prop->sink_ports);
	prop->sink_dpn_prop = devm_kcalloc(&slave->dev, nval,
					   sizeof(*prop->sink_dpn_prop),
					   GFP_KERNEL);
	if (!prop->sink_dpn_prop)
		return -ENOMEM;

	/* Read dpn properties for sink port(s) */
	sdw_slave_read_dpn(slave, prop->sink_dpn_prop, nval,
			   prop->sink_ports, "sink");

	return sdw_slave_read_lane_mapping(slave);
}
EXPORT_SYMBOL(sdw_slave_read_prop);
