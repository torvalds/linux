// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
// Copyright(c) 2015-2020 Intel Corporation.

/*
 * Bandwidth management algorithm based on 2^n gears
 *
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/soundwire/sdw.h>
#include "bus.h"

#define SDW_STRM_RATE_GROUPING		1

struct sdw_group_params {
	unsigned int rate;
	int full_bw;
	int payload_bw;
	int hwidth;
};

struct sdw_group {
	unsigned int count;
	unsigned int max_size;
	unsigned int *rates;
};

void sdw_compute_slave_ports(struct sdw_master_runtime *m_rt,
			     struct sdw_transport_data *t_data)
{
	struct sdw_slave_runtime *s_rt = NULL;
	struct sdw_port_runtime *p_rt;
	int port_bo, sample_int;
	unsigned int rate, bps, ch = 0;
	unsigned int slave_total_ch;
	struct sdw_bus_params *b_params = &m_rt->bus->params;

	port_bo = t_data->block_offset;

	list_for_each_entry(s_rt, &m_rt->slave_rt_list, m_rt_node) {
		rate = m_rt->stream->params.rate;
		bps = m_rt->stream->params.bps;
		sample_int = (m_rt->bus->params.curr_dr_freq / rate);
		slave_total_ch = 0;

		list_for_each_entry(p_rt, &s_rt->port_list, port_node) {
			ch = hweight32(p_rt->ch_mask);

			sdw_fill_xport_params(&p_rt->transport_params,
					      p_rt->num, false,
					      SDW_BLK_GRP_CNT_1,
					      sample_int, port_bo, port_bo >> 8,
					      t_data->hstart,
					      t_data->hstop,
					      SDW_BLK_PKG_PER_PORT, 0x0);

			sdw_fill_port_params(&p_rt->port_params,
					     p_rt->num, bps,
					     SDW_PORT_FLOW_MODE_ISOCH,
					     b_params->s_data_mode);

			port_bo += bps * ch;
			slave_total_ch += ch;
		}

		if (m_rt->direction == SDW_DATA_DIR_TX &&
		    m_rt->ch_count == slave_total_ch) {
			/*
			 * Slave devices were configured to access all channels
			 * of the stream, which indicates that they operate in
			 * 'mirror mode'. Make sure we reset the port offset for
			 * the next device in the list
			 */
			port_bo = t_data->block_offset;
		}
	}
}
EXPORT_SYMBOL(sdw_compute_slave_ports);

static void sdw_compute_master_ports(struct sdw_master_runtime *m_rt,
				     struct sdw_group_params *params,
				     int *port_bo, int hstop)
{
	struct sdw_transport_data t_data = {0};
	struct sdw_port_runtime *p_rt;
	struct sdw_bus *bus = m_rt->bus;
	struct sdw_bus_params *b_params = &bus->params;
	int sample_int, hstart = 0;
	unsigned int rate, bps, ch;

	rate = m_rt->stream->params.rate;
	bps = m_rt->stream->params.bps;
	ch = m_rt->ch_count;
	sample_int = (bus->params.curr_dr_freq / rate);

	if (rate != params->rate)
		return;

	t_data.hstop = hstop;
	hstart = hstop - params->hwidth + 1;
	t_data.hstart = hstart;

	list_for_each_entry(p_rt, &m_rt->port_list, port_node) {

		sdw_fill_xport_params(&p_rt->transport_params, p_rt->num,
				      false, SDW_BLK_GRP_CNT_1, sample_int,
				      *port_bo, (*port_bo) >> 8, hstart, hstop,
				      SDW_BLK_PKG_PER_PORT, 0x0);

		sdw_fill_port_params(&p_rt->port_params,
				     p_rt->num, bps,
				     SDW_PORT_FLOW_MODE_ISOCH,
				     b_params->m_data_mode);

		/* Check for first entry */
		if (!(p_rt == list_first_entry(&m_rt->port_list,
					       struct sdw_port_runtime,
					       port_node))) {
			(*port_bo) += bps * ch;
			continue;
		}

		t_data.hstart = hstart;
		t_data.hstop = hstop;
		t_data.block_offset = *port_bo;
		t_data.sub_block_offset = 0;
		(*port_bo) += bps * ch;
	}

	sdw_compute_slave_ports(m_rt, &t_data);
}

static void _sdw_compute_port_params(struct sdw_bus *bus,
				     struct sdw_group_params *params, int count)
{
	struct sdw_master_runtime *m_rt;
	int hstop = bus->params.col - 1;
	int port_bo, i;

	/* Run loop for all groups to compute transport parameters */
	for (i = 0; i < count; i++) {
		port_bo = 1;

		list_for_each_entry(m_rt, &bus->m_rt_list, bus_node) {
			sdw_compute_master_ports(m_rt, &params[i], &port_bo, hstop);
		}

		hstop = hstop - params[i].hwidth;
	}
}

static int sdw_compute_group_params(struct sdw_bus *bus,
				    struct sdw_group_params *params,
				    int *rates, int count)
{
	struct sdw_master_runtime *m_rt;
	int sel_col = bus->params.col;
	unsigned int rate, bps, ch;
	int i, column_needed = 0;

	/* Calculate bandwidth per group */
	for (i = 0; i < count; i++) {
		params[i].rate = rates[i];
		params[i].full_bw = bus->params.curr_dr_freq / params[i].rate;
	}

	list_for_each_entry(m_rt, &bus->m_rt_list, bus_node) {
		rate = m_rt->stream->params.rate;
		bps = m_rt->stream->params.bps;
		ch = m_rt->ch_count;

		for (i = 0; i < count; i++) {
			if (rate == params[i].rate)
				params[i].payload_bw += bps * ch;
		}
	}

	for (i = 0; i < count; i++) {
		params[i].hwidth = (sel_col *
			params[i].payload_bw + params[i].full_bw - 1) /
			params[i].full_bw;

		column_needed += params[i].hwidth;
	}

	if (column_needed > sel_col - 1)
		return -EINVAL;

	return 0;
}

static int sdw_add_element_group_count(struct sdw_group *group,
				       unsigned int rate)
{
	int num = group->count;
	int i;

	for (i = 0; i <= num; i++) {
		if (rate == group->rates[i])
			break;

		if (i != num)
			continue;

		if (group->count >= group->max_size) {
			unsigned int *rates;

			group->max_size += 1;
			rates = krealloc(group->rates,
					 (sizeof(int) * group->max_size),
					 GFP_KERNEL);
			if (!rates)
				return -ENOMEM;
			group->rates = rates;
		}

		group->rates[group->count++] = rate;
	}

	return 0;
}

static int sdw_get_group_count(struct sdw_bus *bus,
			       struct sdw_group *group)
{
	struct sdw_master_runtime *m_rt;
	unsigned int rate;
	int ret = 0;

	group->count = 0;
	group->max_size = SDW_STRM_RATE_GROUPING;
	group->rates = kcalloc(group->max_size, sizeof(int), GFP_KERNEL);
	if (!group->rates)
		return -ENOMEM;

	list_for_each_entry(m_rt, &bus->m_rt_list, bus_node) {
		rate = m_rt->stream->params.rate;
		if (m_rt == list_first_entry(&bus->m_rt_list,
					     struct sdw_master_runtime,
					     bus_node)) {
			group->rates[group->count++] = rate;

		} else {
			ret = sdw_add_element_group_count(group, rate);
			if (ret < 0) {
				kfree(group->rates);
				return ret;
			}
		}
	}

	return ret;
}

/**
 * sdw_compute_port_params: Compute transport and port parameters
 *
 * @bus: SDW Bus instance
 */
static int sdw_compute_port_params(struct sdw_bus *bus)
{
	struct sdw_group_params *params = NULL;
	struct sdw_group group;
	int ret;

	ret = sdw_get_group_count(bus, &group);
	if (ret < 0)
		return ret;

	if (group.count == 0)
		goto out;

	params = kcalloc(group.count, sizeof(*params), GFP_KERNEL);
	if (!params) {
		ret = -ENOMEM;
		goto out;
	}

	/* Compute transport parameters for grouped streams */
	ret = sdw_compute_group_params(bus, params,
				       &group.rates[0], group.count);
	if (ret < 0)
		goto free_params;

	_sdw_compute_port_params(bus, params, group.count);

free_params:
	kfree(params);
out:
	kfree(group.rates);

	return ret;
}

static int sdw_select_row_col(struct sdw_bus *bus, int clk_freq)
{
	struct sdw_master_prop *prop = &bus->prop;
	int frame_int, frame_freq;
	int r, c;

	for (c = 0; c < SDW_FRAME_COLS; c++) {
		for (r = 0; r < SDW_FRAME_ROWS; r++) {
			if (sdw_rows[r] != prop->default_row ||
			    sdw_cols[c] != prop->default_col)
				continue;

			frame_int = sdw_rows[r] * sdw_cols[c];
			frame_freq = clk_freq / frame_int;

			if ((clk_freq - (frame_freq * SDW_FRAME_CTRL_BITS)) <
			    bus->params.bandwidth)
				continue;

			bus->params.row = sdw_rows[r];
			bus->params.col = sdw_cols[c];
			return 0;
		}
	}

	return -EINVAL;
}

/**
 * sdw_compute_bus_params: Compute bus parameters
 *
 * @bus: SDW Bus instance
 */
static int sdw_compute_bus_params(struct sdw_bus *bus)
{
	unsigned int curr_dr_freq = 0;
	struct sdw_master_prop *mstr_prop = &bus->prop;
	int i, clk_values, ret;
	bool is_gear = false;
	u32 *clk_buf;

	if (mstr_prop->num_clk_gears) {
		clk_values = mstr_prop->num_clk_gears;
		clk_buf = mstr_prop->clk_gears;
		is_gear = true;
	} else if (mstr_prop->num_clk_freq) {
		clk_values = mstr_prop->num_clk_freq;
		clk_buf = mstr_prop->clk_freq;
	} else {
		clk_values = 1;
		clk_buf = NULL;
	}

	for (i = 0; i < clk_values; i++) {
		if (!clk_buf)
			curr_dr_freq = bus->params.max_dr_freq;
		else
			curr_dr_freq = (is_gear) ?
				(bus->params.max_dr_freq >>  clk_buf[i]) :
				clk_buf[i] * SDW_DOUBLE_RATE_FACTOR;

		if (curr_dr_freq <= bus->params.bandwidth)
			continue;

		break;

		/*
		 * TODO: Check all the Slave(s) port(s) audio modes and find
		 * whether given clock rate is supported with glitchless
		 * transition.
		 */
	}

	if (i == clk_values) {
		dev_err(bus->dev, "%s: could not find clock value for bandwidth %d\n",
			__func__, bus->params.bandwidth);
		return -EINVAL;
	}

	ret = sdw_select_row_col(bus, curr_dr_freq);
	if (ret < 0) {
		dev_err(bus->dev, "%s: could not find frame configuration for bus dr_freq %d\n",
			__func__, curr_dr_freq);
		return -EINVAL;
	}

	bus->params.curr_dr_freq = curr_dr_freq;
	return 0;
}

/**
 * sdw_compute_params: Compute bus, transport and port parameters
 *
 * @bus: SDW Bus instance
 */
int sdw_compute_params(struct sdw_bus *bus)
{
	int ret;

	/* Computes clock frequency, frame shape and frame frequency */
	ret = sdw_compute_bus_params(bus);
	if (ret < 0)
		return ret;

	/* Compute transport and port params */
	ret = sdw_compute_port_params(bus);
	if (ret < 0) {
		dev_err(bus->dev, "Compute transport params failed: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(sdw_compute_params);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("SoundWire Generic Bandwidth Allocation");
