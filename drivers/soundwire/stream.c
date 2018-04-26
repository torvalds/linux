// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-18 Intel Corporation.

/*
 *  stream.c - SoundWire Bus stream operations.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/soundwire/sdw.h>
#include "bus.h"

/**
 * sdw_release_stream() - Free the assigned stream runtime
 *
 * @stream: SoundWire stream runtime
 *
 * sdw_release_stream should be called only once per stream
 */
void sdw_release_stream(struct sdw_stream_runtime *stream)
{
	kfree(stream);
}
EXPORT_SYMBOL(sdw_release_stream);

/**
 * sdw_alloc_stream() - Allocate and return stream runtime
 *
 * @stream_name: SoundWire stream name
 *
 * Allocates a SoundWire stream runtime instance.
 * sdw_alloc_stream should be called only once per stream. Typically
 * invoked from ALSA/ASoC machine/platform driver.
 */
struct sdw_stream_runtime *sdw_alloc_stream(char *stream_name)
{
	struct sdw_stream_runtime *stream;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream)
		return NULL;

	stream->name = stream_name;
	stream->state = SDW_STREAM_ALLOCATED;

	return stream;
}
EXPORT_SYMBOL(sdw_alloc_stream);

/**
 * sdw_alloc_master_rt() - Allocates and initialize Master runtime handle
 *
 * @bus: SDW bus instance
 * @stream_config: Stream configuration
 * @stream: Stream runtime handle.
 *
 * This function is to be called with bus_lock held.
 */
static struct sdw_master_runtime
*sdw_alloc_master_rt(struct sdw_bus *bus,
			struct sdw_stream_config *stream_config,
			struct sdw_stream_runtime *stream)
{
	struct sdw_master_runtime *m_rt;

	m_rt = stream->m_rt;

	/*
	 * check if Master is already allocated (as a result of Slave adding
	 * it first), if so skip allocation and go to configure
	 */
	if (m_rt)
		goto stream_config;

	m_rt = kzalloc(sizeof(*m_rt), GFP_KERNEL);
	if (!m_rt)
		return NULL;

	/* Initialization of Master runtime handle */
	INIT_LIST_HEAD(&m_rt->port_list);
	INIT_LIST_HEAD(&m_rt->slave_rt_list);
	stream->m_rt = m_rt;

	list_add_tail(&m_rt->bus_node, &bus->m_rt_list);

stream_config:
	m_rt->ch_count = stream_config->ch_count;
	m_rt->bus = bus;
	m_rt->stream = stream;
	m_rt->direction = stream_config->direction;

	return m_rt;
}

/**
 * sdw_alloc_slave_rt() - Allocate and initialize Slave runtime handle.
 *
 * @slave: Slave handle
 * @stream_config: Stream configuration
 * @stream: Stream runtime handle
 *
 * This function is to be called with bus_lock held.
 */
static struct sdw_slave_runtime
*sdw_alloc_slave_rt(struct sdw_slave *slave,
			struct sdw_stream_config *stream_config,
			struct sdw_stream_runtime *stream)
{
	struct sdw_slave_runtime *s_rt = NULL;

	s_rt = kzalloc(sizeof(*s_rt), GFP_KERNEL);
	if (!s_rt)
		return NULL;

	INIT_LIST_HEAD(&s_rt->port_list);
	s_rt->ch_count = stream_config->ch_count;
	s_rt->direction = stream_config->direction;
	s_rt->slave = slave;

	return s_rt;
}

static void sdw_master_port_release(struct sdw_bus *bus,
			struct sdw_master_runtime *m_rt)
{
	struct sdw_port_runtime *p_rt, *_p_rt;

	list_for_each_entry_safe(p_rt, _p_rt,
			&m_rt->port_list, port_node) {
		list_del(&p_rt->port_node);
		kfree(p_rt);
	}
}

static void sdw_slave_port_release(struct sdw_bus *bus,
			struct sdw_slave *slave,
			struct sdw_stream_runtime *stream)
{
	struct sdw_port_runtime *p_rt, *_p_rt;
	struct sdw_master_runtime *m_rt = stream->m_rt;
	struct sdw_slave_runtime *s_rt;

	list_for_each_entry(s_rt, &m_rt->slave_rt_list, m_rt_node) {
		if (s_rt->slave != slave)
			continue;

		list_for_each_entry_safe(p_rt, _p_rt,
				&s_rt->port_list, port_node) {
			list_del(&p_rt->port_node);
			kfree(p_rt);
		}
	}
}

/**
 * sdw_release_slave_stream() - Free Slave(s) runtime handle
 *
 * @slave: Slave handle.
 * @stream: Stream runtime handle.
 *
 * This function is to be called with bus_lock held.
 */
static void sdw_release_slave_stream(struct sdw_slave *slave,
			struct sdw_stream_runtime *stream)
{
	struct sdw_slave_runtime *s_rt, *_s_rt;
	struct sdw_master_runtime *m_rt = stream->m_rt;

	/* Retrieve Slave runtime handle */
	list_for_each_entry_safe(s_rt, _s_rt,
			&m_rt->slave_rt_list, m_rt_node) {

		if (s_rt->slave == slave) {
			list_del(&s_rt->m_rt_node);
			kfree(s_rt);
			return;
		}
	}
}

/**
 * sdw_release_master_stream() - Free Master runtime handle
 *
 * @stream: Stream runtime handle.
 *
 * This function is to be called with bus_lock held
 * It frees the Master runtime handle and associated Slave(s) runtime
 * handle. If this is called first then sdw_release_slave_stream() will have
 * no effect as Slave(s) runtime handle would already be freed up.
 */
static void sdw_release_master_stream(struct sdw_stream_runtime *stream)
{
	struct sdw_master_runtime *m_rt = stream->m_rt;
	struct sdw_slave_runtime *s_rt, *_s_rt;

	list_for_each_entry_safe(s_rt, _s_rt,
			&m_rt->slave_rt_list, m_rt_node)
		sdw_stream_remove_slave(s_rt->slave, stream);

	list_del(&m_rt->bus_node);
}

/**
 * sdw_stream_remove_master() - Remove master from sdw_stream
 *
 * @bus: SDW Bus instance
 * @stream: SoundWire stream
 *
 * This removes and frees port_rt and master_rt from a stream
 */
int sdw_stream_remove_master(struct sdw_bus *bus,
		struct sdw_stream_runtime *stream)
{
	mutex_lock(&bus->bus_lock);

	sdw_release_master_stream(stream);
	sdw_master_port_release(bus, stream->m_rt);
	stream->state = SDW_STREAM_RELEASED;
	kfree(stream->m_rt);
	stream->m_rt = NULL;

	mutex_unlock(&bus->bus_lock);

	return 0;
}
EXPORT_SYMBOL(sdw_stream_remove_master);

/**
 * sdw_stream_remove_slave() - Remove slave from sdw_stream
 *
 * @slave: SDW Slave instance
 * @stream: SoundWire stream
 *
 * This removes and frees port_rt and slave_rt from a stream
 */
int sdw_stream_remove_slave(struct sdw_slave *slave,
		struct sdw_stream_runtime *stream)
{
	mutex_lock(&slave->bus->bus_lock);

	sdw_slave_port_release(slave->bus, slave, stream);
	sdw_release_slave_stream(slave, stream);

	mutex_unlock(&slave->bus->bus_lock);

	return 0;
}
EXPORT_SYMBOL(sdw_stream_remove_slave);

/**
 * sdw_config_stream() - Configure the allocated stream
 *
 * @dev: SDW device
 * @stream: SoundWire stream
 * @stream_config: Stream configuration for audio stream
 * @is_slave: is API called from Slave or Master
 *
 * This function is to be called with bus_lock held.
 */
static int sdw_config_stream(struct device *dev,
		struct sdw_stream_runtime *stream,
		struct sdw_stream_config *stream_config, bool is_slave)
{
	/*
	 * Update the stream rate, channel and bps based on data
	 * source. For more than one data source (multilink),
	 * match the rate, bps, stream type and increment number of channels.
	 *
	 * If rate/bps is zero, it means the values are not set, so skip
	 * comparison and allow the value to be set and stored in stream
	 */
	if (stream->params.rate &&
			stream->params.rate != stream_config->frame_rate) {
		dev_err(dev, "rate not matching, stream:%s", stream->name);
		return -EINVAL;
	}

	if (stream->params.bps &&
			stream->params.bps != stream_config->bps) {
		dev_err(dev, "bps not matching, stream:%s", stream->name);
		return -EINVAL;
	}

	stream->type = stream_config->type;
	stream->params.rate = stream_config->frame_rate;
	stream->params.bps = stream_config->bps;

	/* TODO: Update this check during Device-device support */
	if (is_slave)
		stream->params.ch_count += stream_config->ch_count;

	return 0;
}

static int sdw_is_valid_port_range(struct device *dev,
				struct sdw_port_runtime *p_rt)
{
	if (!SDW_VALID_PORT_RANGE(p_rt->num)) {
		dev_err(dev,
			"SoundWire: Invalid port number :%d", p_rt->num);
		return -EINVAL;
	}

	return 0;
}

static struct sdw_port_runtime *sdw_port_alloc(struct device *dev,
				struct sdw_port_config *port_config,
				int port_index)
{
	struct sdw_port_runtime *p_rt;

	p_rt = kzalloc(sizeof(*p_rt), GFP_KERNEL);
	if (!p_rt)
		return NULL;

	p_rt->ch_mask = port_config[port_index].ch_mask;
	p_rt->num = port_config[port_index].num;

	return p_rt;
}

static int sdw_master_port_config(struct sdw_bus *bus,
			struct sdw_master_runtime *m_rt,
			struct sdw_port_config *port_config,
			unsigned int num_ports)
{
	struct sdw_port_runtime *p_rt;
	int i;

	/* Iterate for number of ports to perform initialization */
	for (i = 0; i < num_ports; i++) {
		p_rt = sdw_port_alloc(bus->dev, port_config, i);
		if (!p_rt)
			return -ENOMEM;

		/*
		 * TODO: Check port capabilities for requested
		 * configuration (audio mode support)
		 */

		list_add_tail(&p_rt->port_node, &m_rt->port_list);
	}

	return 0;
}

static int sdw_slave_port_config(struct sdw_slave *slave,
			struct sdw_slave_runtime *s_rt,
			struct sdw_port_config *port_config,
			unsigned int num_config)
{
	struct sdw_port_runtime *p_rt;
	int i, ret;

	/* Iterate for number of ports to perform initialization */
	for (i = 0; i < num_config; i++) {
		p_rt = sdw_port_alloc(&slave->dev, port_config, i);
		if (!p_rt)
			return -ENOMEM;

		/*
		 * TODO: Check valid port range as defined by DisCo/
		 * slave
		 */
		ret = sdw_is_valid_port_range(&slave->dev, p_rt);
		if (ret < 0) {
			kfree(p_rt);
			return ret;
		}

		/*
		 * TODO: Check port capabilities for requested
		 * configuration (audio mode support)
		 */

		list_add_tail(&p_rt->port_node, &s_rt->port_list);
	}

	return 0;
}

/**
 * sdw_stream_add_master() - Allocate and add master runtime to a stream
 *
 * @bus: SDW Bus instance
 * @stream_config: Stream configuration for audio stream
 * @port_config: Port configuration for audio stream
 * @num_ports: Number of ports
 * @stream: SoundWire stream
 */
int sdw_stream_add_master(struct sdw_bus *bus,
		struct sdw_stream_config *stream_config,
		struct sdw_port_config *port_config,
		unsigned int num_ports,
		struct sdw_stream_runtime *stream)
{
	struct sdw_master_runtime *m_rt = NULL;
	int ret;

	mutex_lock(&bus->bus_lock);

	m_rt = sdw_alloc_master_rt(bus, stream_config, stream);
	if (!m_rt) {
		dev_err(bus->dev,
				"Master runtime config failed for stream:%s",
				stream->name);
		ret = -ENOMEM;
		goto error;
	}

	ret = sdw_config_stream(bus->dev, stream, stream_config, false);
	if (ret)
		goto stream_error;

	ret = sdw_master_port_config(bus, m_rt, port_config, num_ports);
	if (ret)
		goto stream_error;

	stream->state = SDW_STREAM_CONFIGURED;

stream_error:
	sdw_release_master_stream(stream);
error:
	mutex_unlock(&bus->bus_lock);
	return ret;
}
EXPORT_SYMBOL(sdw_stream_add_master);

/**
 * sdw_stream_add_slave() - Allocate and add master/slave runtime to a stream
 *
 * @slave: SDW Slave instance
 * @stream_config: Stream configuration for audio stream
 * @stream: SoundWire stream
 * @port_config: Port configuration for audio stream
 * @num_ports: Number of ports
 */
int sdw_stream_add_slave(struct sdw_slave *slave,
		struct sdw_stream_config *stream_config,
		struct sdw_port_config *port_config,
		unsigned int num_ports,
		struct sdw_stream_runtime *stream)
{
	struct sdw_slave_runtime *s_rt;
	struct sdw_master_runtime *m_rt;
	int ret;

	mutex_lock(&slave->bus->bus_lock);

	/*
	 * If this API is invoked by Slave first then m_rt is not valid.
	 * So, allocate m_rt and add Slave to it.
	 */
	m_rt = sdw_alloc_master_rt(slave->bus, stream_config, stream);
	if (!m_rt) {
		dev_err(&slave->dev,
				"alloc master runtime failed for stream:%s",
				stream->name);
		ret = -ENOMEM;
		goto error;
	}

	s_rt = sdw_alloc_slave_rt(slave, stream_config, stream);
	if (!s_rt) {
		dev_err(&slave->dev,
				"Slave runtime config failed for stream:%s",
				stream->name);
		ret = -ENOMEM;
		goto stream_error;
	}

	ret = sdw_config_stream(&slave->dev, stream, stream_config, true);
	if (ret)
		goto stream_error;

	list_add_tail(&s_rt->m_rt_node, &m_rt->slave_rt_list);

	ret = sdw_slave_port_config(slave, s_rt, port_config, num_ports);
	if (ret)
		goto stream_error;

	stream->state = SDW_STREAM_CONFIGURED;
	goto error;

stream_error:
	/*
	 * we hit error so cleanup the stream, release all Slave(s) and
	 * Master runtime
	 */
	sdw_release_master_stream(stream);
error:
	mutex_unlock(&slave->bus->bus_lock);
	return ret;
}
EXPORT_SYMBOL(sdw_stream_add_slave);
