/*
 * Greybus audio commands
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/kernel.h>

#include "greybus.h"
#include "audio.h"

#define GB_I2S_MGMT_VERSION_MAJOR		0x00
#define GB_I2S_MGMT_VERSION_MINOR		0x01

#define GB_I2S_DATA_VERSION_MAJOR		0x00
#define GB_I2S_MGMT_VERSION_MINOR		0x01

/***********************************
 * GB I2S helper functions
 ***********************************/
int gb_i2s_mgmt_get_version(struct gb_connection *connection)
{
	struct gb_protocol_version_response response;

	memset(&response, 0, sizeof(response));
	return gb_protocol_get_version(connection,
				       GB_I2S_MGMT_TYPE_PROTOCOL_VERSION,
				       NULL, 0, &response,
				       GB_I2S_MGMT_VERSION_MAJOR);
}

int gb_i2s_data_get_version(struct gb_connection *connection)
{
	struct gb_protocol_version_response response;

	memset(&response, 0, sizeof(response));
	return gb_protocol_get_version(connection,
				       GB_I2S_DATA_TYPE_PROTOCOL_VERSION,
				       NULL, 0, &response,
				       GB_I2S_DATA_VERSION_MAJOR);
}

int gb_i2s_mgmt_activate_cport(struct gb_connection *connection,
				      uint16_t cport)
{
	struct gb_i2s_mgmt_activate_cport_request request;

	memset(&request, 0, sizeof(request));
	request.cport = cpu_to_le16(cport);

	return gb_operation_sync(connection, GB_I2S_MGMT_TYPE_ACTIVATE_CPORT,
				 &request, sizeof(request), NULL, 0);
}

int gb_i2s_mgmt_deactivate_cport(struct gb_connection *connection,
					uint16_t cport)
{
	struct gb_i2s_mgmt_deactivate_cport_request request;

	memset(&request, 0, sizeof(request));
	request.cport = cpu_to_le16(cport);

	return gb_operation_sync(connection, GB_I2S_MGMT_TYPE_DEACTIVATE_CPORT,
				 &request, sizeof(request), NULL, 0);
}

int gb_i2s_mgmt_get_supported_configurations(
	struct gb_connection *connection,
	struct gb_i2s_mgmt_get_supported_configurations_response *get_cfg,
	size_t size)
{
	return gb_operation_sync(connection,
				 GB_I2S_MGMT_TYPE_GET_SUPPORTED_CONFIGURATIONS,
				 NULL, 0, get_cfg, size);
}

int gb_i2s_mgmt_set_configuration(struct gb_connection *connection,
			struct gb_i2s_mgmt_set_configuration_request *set_cfg)
{
	return gb_operation_sync(connection, GB_I2S_MGMT_TYPE_SET_CONFIGURATION,
				 set_cfg, sizeof(*set_cfg), NULL, 0);
}

int gb_i2s_mgmt_set_samples_per_message(
				struct gb_connection *connection,
				uint16_t samples_per_message)
{
	struct gb_i2s_mgmt_set_samples_per_message_request request;

	memset(&request, 0, sizeof(request));
	request.samples_per_message = cpu_to_le16(samples_per_message);

	return gb_operation_sync(connection,
				 GB_I2S_MGMT_TYPE_SET_SAMPLES_PER_MESSAGE,
				 &request, sizeof(request), NULL, 0);
}

int gb_i2s_mgmt_get_cfgs(struct gb_snd *snd_dev,
			 struct gb_connection *connection)
{
	struct gb_i2s_mgmt_get_supported_configurations_response *get_cfg;
	size_t size;
	int ret;

	size = sizeof(*get_cfg) +
	       (CONFIG_COUNT_MAX * sizeof(get_cfg->config[0]));

	get_cfg = kzalloc(size, GFP_KERNEL);
	if (!get_cfg)
		return -ENOMEM;

	ret = gb_i2s_mgmt_get_supported_configurations(connection, get_cfg,
						       size);
	if (ret) {
		pr_err("get_supported_config failed: %d\n", ret);
		goto err_free_get_cfg;
	}

	snd_dev->i2s_configs = get_cfg;

	return 0;

err_free_get_cfg:
	kfree(get_cfg);
	return ret;
}

void gb_i2s_mgmt_free_cfgs(struct gb_snd *snd_dev)
{
	kfree(snd_dev->i2s_configs);
	snd_dev->i2s_configs = NULL;
}

int gb_i2s_mgmt_set_cfg(struct gb_snd *snd_dev, int rate, int chans,
			int bytes_per_chan, int is_le)
{
	struct gb_i2s_mgmt_set_configuration_request set_cfg;
	struct gb_i2s_mgmt_configuration *cfg;
	int i, ret;
	u8 byte_order = GB_I2S_MGMT_BYTE_ORDER_NA;

	if (bytes_per_chan > 1) {
		if (is_le)
			byte_order = GB_I2S_MGMT_BYTE_ORDER_LE;
		else
			byte_order = GB_I2S_MGMT_BYTE_ORDER_BE;
	}

	for (i = 0, cfg = snd_dev->i2s_configs->config;
	     i < CONFIG_COUNT_MAX;
	     i++, cfg++) {
		if ((cfg->sample_frequency == cpu_to_le32(rate)) &&
		    (cfg->num_channels == chans) &&
		    (cfg->bytes_per_channel == bytes_per_chan) &&
		    (cfg->byte_order & byte_order) &&
		    (cfg->ll_protocol &
			     cpu_to_le32(GB_I2S_MGMT_PROTOCOL_I2S)) &&
		    (cfg->ll_mclk_role & GB_I2S_MGMT_ROLE_MASTER) &&
		    (cfg->ll_bclk_role & GB_I2S_MGMT_ROLE_MASTER) &&
		    (cfg->ll_wclk_role & GB_I2S_MGMT_ROLE_MASTER) &&
		    (cfg->ll_wclk_polarity & GB_I2S_MGMT_POLARITY_NORMAL) &&
		    (cfg->ll_wclk_change_edge & GB_I2S_MGMT_EDGE_FALLING) &&
		    (cfg->ll_wclk_tx_edge & GB_I2S_MGMT_EDGE_RISING) &&
		    (cfg->ll_wclk_rx_edge & GB_I2S_MGMT_EDGE_FALLING) &&
		    (cfg->ll_data_offset == 1))
			break;
	}

	if (i >= CONFIG_COUNT_MAX) {
		pr_err("No valid configuration\n");
		return -EINVAL;
	}

	memcpy(&set_cfg, cfg, sizeof(set_cfg));
	set_cfg.config.byte_order = byte_order;
	set_cfg.config.ll_protocol = cpu_to_le32(GB_I2S_MGMT_PROTOCOL_I2S);
	set_cfg.config.ll_mclk_role = GB_I2S_MGMT_ROLE_MASTER;
	set_cfg.config.ll_bclk_role = GB_I2S_MGMT_ROLE_MASTER;
	set_cfg.config.ll_wclk_role = GB_I2S_MGMT_ROLE_MASTER;
	set_cfg.config.ll_wclk_polarity = GB_I2S_MGMT_POLARITY_NORMAL;
	set_cfg.config.ll_wclk_change_edge = GB_I2S_MGMT_EDGE_FALLING;
	set_cfg.config.ll_wclk_tx_edge = GB_I2S_MGMT_EDGE_RISING;
	set_cfg.config.ll_wclk_rx_edge = GB_I2S_MGMT_EDGE_FALLING;

	ret = gb_i2s_mgmt_set_configuration(snd_dev->mgmt_connection, &set_cfg);
	if (ret)
		pr_err("set_configuration failed: %d\n", ret);

	return ret;
}

int gb_i2s_send_data(struct gb_connection *connection,
					void *req_buf, void *source_addr,
					size_t len, int sample_num)
{
	struct gb_i2s_send_data_request *gb_req;
	int ret;

	gb_req = req_buf;
	gb_req->sample_number = cpu_to_le32(sample_num);

	memcpy((void *)&gb_req->data[0], source_addr, len);

	if (len < MAX_SEND_DATA_LEN)
		for (; len < MAX_SEND_DATA_LEN; len++)
			gb_req->data[len] = gb_req->data[len - SAMPLE_SIZE];

	gb_req->size = cpu_to_le32(len);

	ret = gb_operation_sync(connection, GB_I2S_DATA_TYPE_SEND_DATA,
				(void *) gb_req, SEND_DATA_BUF_LEN, NULL, 0);
	return ret;
}
