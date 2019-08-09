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

/**
 * sdw_master_read_prop() - Read Master properties
 * @bus: SDW bus instance
 */
int sdw_master_read_prop(struct sdw_bus *bus)
{
	struct sdw_master_prop *prop = &bus->prop;
	struct fwnode_handle *link;
	char name[32];
	int nval, i;

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

	if (fwnode_property_read_bool(link,
				      "mipi-sdw-clock-stop-mode0-supported"))
		prop->clk_stop_modes |= BIT(SDW_CLK_STOP_MODE0);

	if (fwnode_property_read_bool(link,
				      "mipi-sdw-clock-stop-mode1-supported"))
		prop->clk_stop_modes |= BIT(SDW_CLK_STOP_MODE1);

	fwnode_property_read_u32(link,
				 "mipi-sdw-max-clock-frequency",
				 &prop->max_clk_freq);

	nval = fwnode_property_read_u32_array(link,
			"mipi-sdw-clock-frequencies-supported", NULL, 0);
	if (nval > 0) {
		prop->num_clk_freq = nval;
		prop->clk_freq = devm_kcalloc(bus->dev, prop->num_clk_freq,
					      sizeof(*prop->clk_freq),
					      GFP_KERNEL);
		if (!prop->clk_freq)
			return -ENOMEM;

		fwnode_property_read_u32_array(link,
				"mipi-sdw-clock-frequencies-supported",
				prop->clk_freq, prop->num_clk_freq);
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

	nval = fwnode_property_read_u32_array(link,
			"mipi-sdw-supported-clock-gears", NULL, 0);
	if (nval > 0) {
		prop->num_clk_gears = nval;
		prop->clk_gears = devm_kcalloc(bus->dev, prop->num_clk_gears,
					       sizeof(*prop->clk_gears),
					       GFP_KERNEL);
		if (!prop->clk_gears)
			return -ENOMEM;

		fwnode_property_read_u32_array(link,
					       "mipi-sdw-supported-clock-gears",
					       prop->clk_gears,
					       prop->num_clk_gears);
	}

	fwnode_property_read_u32(link, "mipi-sdw-default-frame-rate",
				 &prop->default_frame_rate);

	fwnode_property_read_u32(link, "mipi-sdw-default-frame-row-size",
				 &prop->default_row);

	fwnode_property_read_u32(link, "mipi-sdw-default-frame-col-size",
				 &prop->default_col);

	prop->dynamic_frame =  fwnode_property_read_bool(link,
			"mipi-sdw-dynamic-frame-shape");

	fwnode_property_read_u32(link, "mipi-sdw-command-error-threshold",
				 &prop->err_threshold);

	return 0;
}
EXPORT_SYMBOL(sdw_master_read_prop);

static int sdw_slave_read_dp0(struct sdw_slave *slave,
			      struct fwnode_handle *port,
			      struct sdw_dp0_prop *dp0)
{
	int nval;

	fwnode_property_read_u32(port, "mipi-sdw-port-max-wordlength",
				 &dp0->max_word);

	fwnode_property_read_u32(port, "mipi-sdw-port-min-wordlength",
				 &dp0->min_word);

	nval = fwnode_property_read_u32_array(port,
			"mipi-sdw-port-wordlength-configs", NULL, 0);
	if (nval > 0) {

		dp0->num_words = nval;
		dp0->words = devm_kcalloc(&slave->dev,
					  dp0->num_words, sizeof(*dp0->words),
					  GFP_KERNEL);
		if (!dp0->words)
			return -ENOMEM;

		fwnode_property_read_u32_array(port,
				"mipi-sdw-port-wordlength-configs",
				dp0->words, dp0->num_words);
	}

	dp0->BRA_flow_controlled = fwnode_property_read_bool(port,
				"mipi-sdw-bra-flow-controlled");

	dp0->simple_ch_prep_sm = fwnode_property_read_bool(port,
				"mipi-sdw-simplified-channel-prepare-sm");

	dp0->imp_def_interrupts = fwnode_property_read_bool(port,
				"mipi-sdw-imp-def-dp0-interrupts-supported");

	return 0;
}

static int sdw_slave_read_dpn(struct sdw_slave *slave,
			      struct sdw_dpn_prop *dpn, int count, int ports,
			      char *type)
{
	struct fwnode_handle *node;
	u32 bit, i = 0;
	int nval;
	unsigned long addr;
	char name[40];

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

		nval = fwnode_property_read_u32_array(node,
				"mipi-sdw-port-wordlength-configs", NULL, 0);
		if (nval > 0) {
			dpn[i].num_words = nval;
			dpn[i].words = devm_kcalloc(&slave->dev,
						    dpn[i].num_words,
						    sizeof(*dpn[i].words),
						    GFP_KERNEL);
			if (!dpn[i].words)
				return -ENOMEM;

			fwnode_property_read_u32_array(node,
					"mipi-sdw-port-wordlength-configs",
					dpn[i].words, dpn[i].num_words);
		}

		fwnode_property_read_u32(node, "mipi-sdw-data-port-type",
					 &dpn[i].type);

		fwnode_property_read_u32(node,
					 "mipi-sdw-max-grouping-supported",
					 &dpn[i].max_grouping);

		dpn[i].simple_ch_prep_sm = fwnode_property_read_bool(node,
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

		nval = fwnode_property_read_u32_array(node,
				"mipi-sdw-channel-number-list", NULL, 0);
		if (nval > 0) {
			dpn[i].num_ch = nval;
			dpn[i].ch = devm_kcalloc(&slave->dev, dpn[i].num_ch,
						 sizeof(*dpn[i].ch),
						 GFP_KERNEL);
			if (!dpn[i].ch)
				return -ENOMEM;

			fwnode_property_read_u32_array(node,
					"mipi-sdw-channel-number-list",
					dpn[i].ch, dpn[i].num_ch);
		}

		nval = fwnode_property_read_u32_array(node,
				"mipi-sdw-channel-combination-list", NULL, 0);
		if (nval > 0) {
			dpn[i].num_ch_combinations = nval;
			dpn[i].ch_combinations = devm_kcalloc(&slave->dev,
					dpn[i].num_ch_combinations,
					sizeof(*dpn[i].ch_combinations),
					GFP_KERNEL);
			if (!dpn[i].ch_combinations)
				return -ENOMEM;

			fwnode_property_read_u32_array(node,
					"mipi-sdw-channel-combination-list",
					dpn[i].ch_combinations,
					dpn[i].num_ch_combinations);
		}

		fwnode_property_read_u32(node,
				"mipi-sdw-modes-supported", &dpn[i].modes);

		fwnode_property_read_u32(node, "mipi-sdw-max-async-buffer",
					 &dpn[i].max_async_buffer);

		dpn[i].block_pack_mode = fwnode_property_read_bool(node,
				"mipi-sdw-block-packing-mode");

		fwnode_property_read_u32(node, "mipi-sdw-port-encoding-type",
					 &dpn[i].port_encoding);

		/* TODO: Read audio mode */

		i++;
	}

	return 0;
}

/**
 * sdw_slave_read_prop() - Read Slave properties
 * @slave: SDW Slave
 */
int sdw_slave_read_prop(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	struct device *dev = &slave->dev;
	struct fwnode_handle *port;
	int num_of_ports, nval, i, dp0 = 0;

	device_property_read_u32(dev, "mipi-sdw-sw-interface-revision",
				 &prop->mipi_revision);

	prop->wake_capable = device_property_read_bool(dev,
				"mipi-sdw-wake-up-unavailable");
	prop->wake_capable = !prop->wake_capable;

	prop->test_mode_capable = device_property_read_bool(dev,
				"mipi-sdw-test-mode-supported");

	prop->clk_stop_mode1 = false;
	if (device_property_read_bool(dev,
				"mipi-sdw-clock-stop-mode1-supported"))
		prop->clk_stop_mode1 = true;

	prop->simple_clk_stop_capable = device_property_read_bool(dev,
			"mipi-sdw-simplified-clockstopprepare-sm-supported");

	device_property_read_u32(dev, "mipi-sdw-clockstopprepare-timeout",
				 &prop->clk_stop_timeout);

	device_property_read_u32(dev, "mipi-sdw-slave-channelprepare-timeout",
				 &prop->ch_prep_timeout);

	device_property_read_u32(dev,
			"mipi-sdw-clockstopprepare-hard-reset-behavior",
			&prop->reset_behave);

	prop->high_PHY_capable = device_property_read_bool(dev,
			"mipi-sdw-highPHY-capable");

	prop->paging_support = device_property_read_bool(dev,
			"mipi-sdw-paging-support");

	prop->bank_delay_support = device_property_read_bool(dev,
			"mipi-sdw-bank-delay-support");

	device_property_read_u32(dev,
			"mipi-sdw-port15-read-behavior", &prop->p15_behave);

	device_property_read_u32(dev, "mipi-sdw-master-count",
				 &prop->master_count);

	device_property_read_u32(dev, "mipi-sdw-source-port-list",
				 &prop->source_ports);

	device_property_read_u32(dev, "mipi-sdw-sink-port-list",
				 &prop->sink_ports);

	/* Read dp0 properties */
	port = device_get_named_child_node(dev, "mipi-sdw-dp-0-subproperties");
	if (!port) {
		dev_dbg(dev, "DP0 node not found!!\n");
	} else {
		prop->dp0_prop = devm_kzalloc(&slave->dev,
					      sizeof(*prop->dp0_prop),
					      GFP_KERNEL);
		if (!prop->dp0_prop)
			return -ENOMEM;

		sdw_slave_read_dp0(slave, port, prop->dp0_prop);
		dp0 = 1;
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

	/* some ports are bidirectional so check total ports by ORing */
	nval = prop->source_ports | prop->sink_ports;
	num_of_ports = hweight32(nval) + dp0; /* add DP0 */

	/* Allocate port_ready based on num_of_ports */
	slave->port_ready = devm_kcalloc(&slave->dev, num_of_ports,
					 sizeof(*slave->port_ready),
					 GFP_KERNEL);
	if (!slave->port_ready)
		return -ENOMEM;

	/* Initialize completion */
	for (i = 0; i < num_of_ports; i++)
		init_completion(&slave->port_ready[i]);

	return 0;
}
EXPORT_SYMBOL(sdw_slave_read_prop);
