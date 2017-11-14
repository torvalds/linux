// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus Audio Device Class Protocol helpers
 *
 * Copyright 2015-2016 Google Inc.
 */

#include "greybus.h"
#include "greybus_protocols.h"
#include "operation.h"
#include "audio_codec.h"

/* TODO: Split into separate calls */
int gb_audio_gb_get_topology(struct gb_connection *connection,
			     struct gb_audio_topology **topology)
{
	struct gb_audio_get_topology_size_response size_resp;
	struct gb_audio_topology *topo;
	u16 size;
	int ret;

	ret = gb_operation_sync(connection, GB_AUDIO_TYPE_GET_TOPOLOGY_SIZE,
				NULL, 0, &size_resp, sizeof(size_resp));
	if (ret)
		return ret;

	size = le16_to_cpu(size_resp.size);
	if (size < sizeof(*topo))
		return -ENODATA;

	topo = kzalloc(size, GFP_KERNEL);
	if (!topo)
		return -ENOMEM;

	ret = gb_operation_sync(connection, GB_AUDIO_TYPE_GET_TOPOLOGY, NULL, 0,
				topo, size);
	if (ret) {
		kfree(topo);
		return ret;
	}

	*topology = topo;

	return 0;
}
EXPORT_SYMBOL_GPL(gb_audio_gb_get_topology);

int gb_audio_gb_get_control(struct gb_connection *connection,
			    u8 control_id, u8 index,
			    struct gb_audio_ctl_elem_value *value)
{
	struct gb_audio_get_control_request req;
	struct gb_audio_get_control_response resp;
	int ret;

	req.control_id = control_id;
	req.index = index;

	ret = gb_operation_sync(connection, GB_AUDIO_TYPE_GET_CONTROL,
				&req, sizeof(req), &resp, sizeof(resp));
	if (ret)
		return ret;

	memcpy(value, &resp.value, sizeof(*value));

	return 0;
}
EXPORT_SYMBOL_GPL(gb_audio_gb_get_control);

int gb_audio_gb_set_control(struct gb_connection *connection,
			    u8 control_id, u8 index,
			    struct gb_audio_ctl_elem_value *value)
{
	struct gb_audio_set_control_request req;

	req.control_id = control_id;
	req.index = index;
	memcpy(&req.value, value, sizeof(req.value));

	return gb_operation_sync(connection, GB_AUDIO_TYPE_SET_CONTROL,
				 &req, sizeof(req), NULL, 0);
}
EXPORT_SYMBOL_GPL(gb_audio_gb_set_control);

int gb_audio_gb_enable_widget(struct gb_connection *connection,
			      u8 widget_id)
{
	struct gb_audio_enable_widget_request req;

	req.widget_id = widget_id;

	return gb_operation_sync(connection, GB_AUDIO_TYPE_ENABLE_WIDGET,
				 &req, sizeof(req), NULL, 0);
}
EXPORT_SYMBOL_GPL(gb_audio_gb_enable_widget);

int gb_audio_gb_disable_widget(struct gb_connection *connection,
			       u8 widget_id)
{
	struct gb_audio_disable_widget_request req;

	req.widget_id = widget_id;

	return gb_operation_sync(connection, GB_AUDIO_TYPE_DISABLE_WIDGET,
				 &req, sizeof(req), NULL, 0);
}
EXPORT_SYMBOL_GPL(gb_audio_gb_disable_widget);

int gb_audio_gb_get_pcm(struct gb_connection *connection, u16 data_cport,
			u32 *format, u32 *rate, u8 *channels,
			u8 *sig_bits)
{
	struct gb_audio_get_pcm_request req;
	struct gb_audio_get_pcm_response resp;
	int ret;

	req.data_cport = cpu_to_le16(data_cport);

	ret = gb_operation_sync(connection, GB_AUDIO_TYPE_GET_PCM,
				&req, sizeof(req), &resp, sizeof(resp));
	if (ret)
		return ret;

	*format = le32_to_cpu(resp.format);
	*rate = le32_to_cpu(resp.rate);
	*channels = resp.channels;
	*sig_bits = resp.sig_bits;

	return 0;
}
EXPORT_SYMBOL_GPL(gb_audio_gb_get_pcm);

int gb_audio_gb_set_pcm(struct gb_connection *connection, u16 data_cport,
			u32 format, u32 rate, u8 channels,
			u8 sig_bits)
{
	struct gb_audio_set_pcm_request req;

	req.data_cport = cpu_to_le16(data_cport);
	req.format = cpu_to_le32(format);
	req.rate = cpu_to_le32(rate);
	req.channels = channels;
	req.sig_bits = sig_bits;

	return gb_operation_sync(connection, GB_AUDIO_TYPE_SET_PCM,
				 &req, sizeof(req), NULL, 0);
}
EXPORT_SYMBOL_GPL(gb_audio_gb_set_pcm);

int gb_audio_gb_set_tx_data_size(struct gb_connection *connection,
				 u16 data_cport, u16 size)
{
	struct gb_audio_set_tx_data_size_request req;

	req.data_cport = cpu_to_le16(data_cport);
	req.size = cpu_to_le16(size);

	return gb_operation_sync(connection, GB_AUDIO_TYPE_SET_TX_DATA_SIZE,
				 &req, sizeof(req), NULL, 0);
}
EXPORT_SYMBOL_GPL(gb_audio_gb_set_tx_data_size);

int gb_audio_gb_activate_tx(struct gb_connection *connection,
			    u16 data_cport)
{
	struct gb_audio_activate_tx_request req;

	req.data_cport = cpu_to_le16(data_cport);

	return gb_operation_sync(connection, GB_AUDIO_TYPE_ACTIVATE_TX,
				 &req, sizeof(req), NULL, 0);
}
EXPORT_SYMBOL_GPL(gb_audio_gb_activate_tx);

int gb_audio_gb_deactivate_tx(struct gb_connection *connection,
			      u16 data_cport)
{
	struct gb_audio_deactivate_tx_request req;

	req.data_cport = cpu_to_le16(data_cport);

	return gb_operation_sync(connection, GB_AUDIO_TYPE_DEACTIVATE_TX,
				 &req, sizeof(req), NULL, 0);
}
EXPORT_SYMBOL_GPL(gb_audio_gb_deactivate_tx);

int gb_audio_gb_set_rx_data_size(struct gb_connection *connection,
				 u16 data_cport, u16 size)
{
	struct gb_audio_set_rx_data_size_request req;

	req.data_cport = cpu_to_le16(data_cport);
	req.size = cpu_to_le16(size);

	return gb_operation_sync(connection, GB_AUDIO_TYPE_SET_RX_DATA_SIZE,
				 &req, sizeof(req), NULL, 0);
}
EXPORT_SYMBOL_GPL(gb_audio_gb_set_rx_data_size);

int gb_audio_gb_activate_rx(struct gb_connection *connection,
			    u16 data_cport)
{
	struct gb_audio_activate_rx_request req;

	req.data_cport = cpu_to_le16(data_cport);

	return gb_operation_sync(connection, GB_AUDIO_TYPE_ACTIVATE_RX,
				 &req, sizeof(req), NULL, 0);
}
EXPORT_SYMBOL_GPL(gb_audio_gb_activate_rx);

int gb_audio_gb_deactivate_rx(struct gb_connection *connection,
			      u16 data_cport)
{
	struct gb_audio_deactivate_rx_request req;

	req.data_cport = cpu_to_le16(data_cport);

	return gb_operation_sync(connection, GB_AUDIO_TYPE_DEACTIVATE_RX,
				 &req, sizeof(req), NULL, 0);
}
EXPORT_SYMBOL_GPL(gb_audio_gb_deactivate_rx);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("greybus:audio-gb");
MODULE_DESCRIPTION("Greybus Audio Device Class Protocol library");
MODULE_AUTHOR("Mark Greer <mgreer@animalcreek.com>");
