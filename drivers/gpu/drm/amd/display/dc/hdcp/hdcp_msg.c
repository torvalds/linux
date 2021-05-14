/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include <linux/slab.h>

#include "dm_services.h"
#include "dm_helpers.h"
#include "include/hdcp_types.h"
#include "include/i2caux_interface.h"
#include "include/signal_types.h"
#include "core_types.h"
#include "dc_link_ddc.h"
#include "link_hwss.h"

#define DC_LOGGER \
	link->ctx->logger
#define HDCP14_KSV_SIZE 5
#define HDCP14_MAX_KSV_FIFO_SIZE 127*HDCP14_KSV_SIZE

static const bool hdcp_cmd_is_read[HDCP_MESSAGE_ID_MAX] = {
	[HDCP_MESSAGE_ID_READ_BKSV] = true,
	[HDCP_MESSAGE_ID_READ_RI_R0] = true,
	[HDCP_MESSAGE_ID_READ_PJ] = true,
	[HDCP_MESSAGE_ID_WRITE_AKSV] = false,
	[HDCP_MESSAGE_ID_WRITE_AINFO] = false,
	[HDCP_MESSAGE_ID_WRITE_AN] = false,
	[HDCP_MESSAGE_ID_READ_VH_X] = true,
	[HDCP_MESSAGE_ID_READ_VH_0] = true,
	[HDCP_MESSAGE_ID_READ_VH_1] = true,
	[HDCP_MESSAGE_ID_READ_VH_2] = true,
	[HDCP_MESSAGE_ID_READ_VH_3] = true,
	[HDCP_MESSAGE_ID_READ_VH_4] = true,
	[HDCP_MESSAGE_ID_READ_BCAPS] = true,
	[HDCP_MESSAGE_ID_READ_BSTATUS] = true,
	[HDCP_MESSAGE_ID_READ_KSV_FIFO] = true,
	[HDCP_MESSAGE_ID_READ_BINFO] = true,
	[HDCP_MESSAGE_ID_HDCP2VERSION] = true,
	[HDCP_MESSAGE_ID_RX_CAPS] = true,
	[HDCP_MESSAGE_ID_WRITE_AKE_INIT] = false,
	[HDCP_MESSAGE_ID_READ_AKE_SEND_CERT] = true,
	[HDCP_MESSAGE_ID_WRITE_AKE_NO_STORED_KM] = false,
	[HDCP_MESSAGE_ID_WRITE_AKE_STORED_KM] = false,
	[HDCP_MESSAGE_ID_READ_AKE_SEND_H_PRIME] = true,
	[HDCP_MESSAGE_ID_READ_AKE_SEND_PAIRING_INFO] = true,
	[HDCP_MESSAGE_ID_WRITE_LC_INIT] = false,
	[HDCP_MESSAGE_ID_READ_LC_SEND_L_PRIME] = true,
	[HDCP_MESSAGE_ID_WRITE_SKE_SEND_EKS] = false,
	[HDCP_MESSAGE_ID_READ_REPEATER_AUTH_SEND_RECEIVERID_LIST] = true,
	[HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_SEND_ACK] = false,
	[HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_STREAM_MANAGE] = false,
	[HDCP_MESSAGE_ID_READ_REPEATER_AUTH_STREAM_READY] = true,
	[HDCP_MESSAGE_ID_READ_RXSTATUS] = true,
	[HDCP_MESSAGE_ID_WRITE_CONTENT_STREAM_TYPE] = false
};

static const uint8_t hdcp_i2c_offsets[HDCP_MESSAGE_ID_MAX] = {
	[HDCP_MESSAGE_ID_READ_BKSV] = 0x0,
	[HDCP_MESSAGE_ID_READ_RI_R0] = 0x8,
	[HDCP_MESSAGE_ID_READ_PJ] = 0xA,
	[HDCP_MESSAGE_ID_WRITE_AKSV] = 0x10,
	[HDCP_MESSAGE_ID_WRITE_AINFO] = 0x15,
	[HDCP_MESSAGE_ID_WRITE_AN] = 0x18,
	[HDCP_MESSAGE_ID_READ_VH_X] = 0x20,
	[HDCP_MESSAGE_ID_READ_VH_0] = 0x20,
	[HDCP_MESSAGE_ID_READ_VH_1] = 0x24,
	[HDCP_MESSAGE_ID_READ_VH_2] = 0x28,
	[HDCP_MESSAGE_ID_READ_VH_3] = 0x2C,
	[HDCP_MESSAGE_ID_READ_VH_4] = 0x30,
	[HDCP_MESSAGE_ID_READ_BCAPS] = 0x40,
	[HDCP_MESSAGE_ID_READ_BSTATUS] = 0x41,
	[HDCP_MESSAGE_ID_READ_KSV_FIFO] = 0x43,
	[HDCP_MESSAGE_ID_READ_BINFO] = 0xFF,
	[HDCP_MESSAGE_ID_HDCP2VERSION] = 0x50,
	[HDCP_MESSAGE_ID_WRITE_AKE_INIT] = 0x60,
	[HDCP_MESSAGE_ID_READ_AKE_SEND_CERT] = 0x80,
	[HDCP_MESSAGE_ID_WRITE_AKE_NO_STORED_KM] = 0x60,
	[HDCP_MESSAGE_ID_WRITE_AKE_STORED_KM] = 0x60,
	[HDCP_MESSAGE_ID_READ_AKE_SEND_H_PRIME] = 0x80,
	[HDCP_MESSAGE_ID_READ_AKE_SEND_PAIRING_INFO] = 0x80,
	[HDCP_MESSAGE_ID_WRITE_LC_INIT] = 0x60,
	[HDCP_MESSAGE_ID_READ_LC_SEND_L_PRIME] = 0x80,
	[HDCP_MESSAGE_ID_WRITE_SKE_SEND_EKS] = 0x60,
	[HDCP_MESSAGE_ID_READ_REPEATER_AUTH_SEND_RECEIVERID_LIST] = 0x80,
	[HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_SEND_ACK] = 0x60,
	[HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_STREAM_MANAGE] = 0x60,
	[HDCP_MESSAGE_ID_READ_REPEATER_AUTH_STREAM_READY] = 0x80,
	[HDCP_MESSAGE_ID_READ_RXSTATUS] = 0x70,
	[HDCP_MESSAGE_ID_WRITE_CONTENT_STREAM_TYPE] = 0x0,
};

struct protection_properties {
	bool supported;
	bool (*process_transaction)(
		struct dc_link *link,
		struct hdcp_protection_message *message_info);
};

static const struct protection_properties non_supported_protection = {
	.supported = false
};

static bool hdmi_14_process_transaction(
	struct dc_link *link,
	struct hdcp_protection_message *message_info)
{
	uint8_t *buff = NULL;
	bool result;
	const uint8_t hdcp_i2c_addr_link_primary = 0x3a; /* 0x74 >> 1*/
	const uint8_t hdcp_i2c_addr_link_secondary = 0x3b; /* 0x76 >> 1*/
	struct i2c_command i2c_command;
	uint8_t offset = hdcp_i2c_offsets[message_info->msg_id];
	struct i2c_payload i2c_payloads[] = {
		{ true, 0, 1, &offset },
		/* actual hdcp payload, will be filled later, zeroed for now*/
		{ 0 }
	};

	switch (message_info->link) {
	case HDCP_LINK_SECONDARY:
		i2c_payloads[0].address = hdcp_i2c_addr_link_secondary;
		i2c_payloads[1].address = hdcp_i2c_addr_link_secondary;
		break;
	case HDCP_LINK_PRIMARY:
	default:
		i2c_payloads[0].address = hdcp_i2c_addr_link_primary;
		i2c_payloads[1].address = hdcp_i2c_addr_link_primary;
		break;
	}

	if (hdcp_cmd_is_read[message_info->msg_id]) {
		i2c_payloads[1].write = false;
		i2c_command.number_of_payloads = ARRAY_SIZE(i2c_payloads);
		i2c_payloads[1].length = message_info->length;
		i2c_payloads[1].data = message_info->data;
	} else {
		i2c_command.number_of_payloads = 1;
		buff = kzalloc(message_info->length + 1, GFP_KERNEL);

		if (!buff)
			return false;

		buff[0] = offset;
		memmove(&buff[1], message_info->data, message_info->length);
		i2c_payloads[0].length = message_info->length + 1;
		i2c_payloads[0].data = buff;
	}

	i2c_command.payloads = i2c_payloads;
	i2c_command.engine = I2C_COMMAND_ENGINE_HW;//only HW
	i2c_command.speed = link->ddc->ctx->dc->caps.i2c_speed_in_khz;

	result = dm_helpers_submit_i2c(
			link->ctx,
			link,
			&i2c_command);
	kfree(buff);

	return result;
}

static const struct protection_properties hdmi_14_protection = {
	.supported = true,
	.process_transaction = hdmi_14_process_transaction
};

static const uint32_t hdcp_dpcd_addrs[HDCP_MESSAGE_ID_MAX] = {
	[HDCP_MESSAGE_ID_READ_BKSV] = 0x68000,
	[HDCP_MESSAGE_ID_READ_RI_R0] = 0x68005,
	[HDCP_MESSAGE_ID_READ_PJ] = 0xFFFFFFFF,
	[HDCP_MESSAGE_ID_WRITE_AKSV] = 0x68007,
	[HDCP_MESSAGE_ID_WRITE_AINFO] = 0x6803B,
	[HDCP_MESSAGE_ID_WRITE_AN] = 0x6800c,
	[HDCP_MESSAGE_ID_READ_VH_X] = 0x68014,
	[HDCP_MESSAGE_ID_READ_VH_0] = 0x68014,
	[HDCP_MESSAGE_ID_READ_VH_1] = 0x68018,
	[HDCP_MESSAGE_ID_READ_VH_2] = 0x6801c,
	[HDCP_MESSAGE_ID_READ_VH_3] = 0x68020,
	[HDCP_MESSAGE_ID_READ_VH_4] = 0x68024,
	[HDCP_MESSAGE_ID_READ_BCAPS] = 0x68028,
	[HDCP_MESSAGE_ID_READ_BSTATUS] = 0x68029,
	[HDCP_MESSAGE_ID_READ_KSV_FIFO] = 0x6802c,
	[HDCP_MESSAGE_ID_READ_BINFO] = 0x6802a,
	[HDCP_MESSAGE_ID_RX_CAPS] = 0x6921d,
	[HDCP_MESSAGE_ID_WRITE_AKE_INIT] = 0x69000,
	[HDCP_MESSAGE_ID_READ_AKE_SEND_CERT] = 0x6900b,
	[HDCP_MESSAGE_ID_WRITE_AKE_NO_STORED_KM] = 0x69220,
	[HDCP_MESSAGE_ID_WRITE_AKE_STORED_KM] = 0x692a0,
	[HDCP_MESSAGE_ID_READ_AKE_SEND_H_PRIME] = 0x692c0,
	[HDCP_MESSAGE_ID_READ_AKE_SEND_PAIRING_INFO] = 0x692e0,
	[HDCP_MESSAGE_ID_WRITE_LC_INIT] = 0x692f0,
	[HDCP_MESSAGE_ID_READ_LC_SEND_L_PRIME] = 0x692f8,
	[HDCP_MESSAGE_ID_WRITE_SKE_SEND_EKS] = 0x69318,
	[HDCP_MESSAGE_ID_READ_REPEATER_AUTH_SEND_RECEIVERID_LIST] = 0x69330,
	[HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_SEND_ACK] = 0x693e0,
	[HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_STREAM_MANAGE] = 0x693f0,
	[HDCP_MESSAGE_ID_READ_REPEATER_AUTH_STREAM_READY] = 0x69473,
	[HDCP_MESSAGE_ID_READ_RXSTATUS] = 0x69493,
	[HDCP_MESSAGE_ID_WRITE_CONTENT_STREAM_TYPE] = 0x69494
};

static bool dpcd_access_helper(
	struct dc_link *link,
	uint32_t length,
	uint8_t *data,
	uint32_t dpcd_addr,
	bool is_read)
{
	enum dc_status status;
	uint32_t cur_length = 0;
	uint32_t offset = 0;
	uint32_t ksv_read_size = 0x6803b - 0x6802c;

	/* Read KSV, need repeatedly handle */
	if (dpcd_addr == 0x6802c) {
		if (length % HDCP14_KSV_SIZE) {
			DC_LOG_ERROR("%s: KsvFifo Size(%d) is not a multiple of HDCP14_KSV_SIZE(%d)\n",
				__func__,
				length,
				HDCP14_KSV_SIZE);
		}
		if (length > HDCP14_MAX_KSV_FIFO_SIZE) {
			DC_LOG_ERROR("%s: KsvFifo Size(%d) is greater than HDCP14_MAX_KSV_FIFO_SIZE(%d)\n",
				__func__,
				length,
				HDCP14_MAX_KSV_FIFO_SIZE);
		}

		DC_LOG_ERROR("%s: Reading %d Ksv(s) from KsvFifo\n",
			__func__,
			length / HDCP14_KSV_SIZE);

		while (length > 0) {
			if (length > ksv_read_size) {
				status = core_link_read_dpcd(
					link,
					dpcd_addr + offset,
					data + offset,
					ksv_read_size);

				data += ksv_read_size;
				length -= ksv_read_size;
			} else {
				status = core_link_read_dpcd(
					link,
					dpcd_addr + offset,
					data + offset,
					length);

				data += length;
				length = 0;
			}

			if (status != DC_OK)
				return false;
		}
	} else {
		while (length > 0) {
			if (length > DEFAULT_AUX_MAX_DATA_SIZE)
				cur_length = DEFAULT_AUX_MAX_DATA_SIZE;
			else
				cur_length = length;

			if (is_read) {
				status = core_link_read_dpcd(
					link,
					dpcd_addr + offset,
					data + offset,
					cur_length);
			} else {
				status = core_link_write_dpcd(
					link,
					dpcd_addr + offset,
					data + offset,
					cur_length);
			}

			if (status != DC_OK)
				return false;

			length -= cur_length;
			offset += cur_length;
		}
	}
	return true;
}

static bool dp_11_process_transaction(
	struct dc_link *link,
	struct hdcp_protection_message *message_info)
{
	return dpcd_access_helper(
		link,
		message_info->length,
		message_info->data,
		hdcp_dpcd_addrs[message_info->msg_id],
		hdcp_cmd_is_read[message_info->msg_id]);
}

static const struct protection_properties dp_11_protection = {
	.supported = true,
	.process_transaction = dp_11_process_transaction
};

static const struct protection_properties *get_protection_properties_by_signal(
	struct dc_link *link,
	enum signal_type st,
	enum hdcp_version version)
{
	switch (version) {
	case HDCP_VERSION_14:
		switch (st) {
		case SIGNAL_TYPE_DVI_SINGLE_LINK:
		case SIGNAL_TYPE_DVI_DUAL_LINK:
		case SIGNAL_TYPE_HDMI_TYPE_A:
			return &hdmi_14_protection;
		case SIGNAL_TYPE_DISPLAY_PORT:
			if (link &&
				(link->dpcd_caps.dongle_type == DISPLAY_DONGLE_DP_VGA_CONVERTER ||
				link->dpcd_caps.dongle_caps.dongle_type == DISPLAY_DONGLE_DP_VGA_CONVERTER)) {
				return &non_supported_protection;
			}
			return &dp_11_protection;
		case SIGNAL_TYPE_DISPLAY_PORT_MST:
		case SIGNAL_TYPE_EDP:
			return &dp_11_protection;
		default:
			return &non_supported_protection;
		}
		break;
	case HDCP_VERSION_22:
		switch (st) {
		case SIGNAL_TYPE_DVI_SINGLE_LINK:
		case SIGNAL_TYPE_DVI_DUAL_LINK:
		case SIGNAL_TYPE_HDMI_TYPE_A:
			return &hdmi_14_protection; //todo version2.2
		case SIGNAL_TYPE_DISPLAY_PORT:
		case SIGNAL_TYPE_DISPLAY_PORT_MST:
		case SIGNAL_TYPE_EDP:
			return &dp_11_protection;  //todo version2.2
		default:
			return &non_supported_protection;
		}
		break;
	default:
		return &non_supported_protection;
	}
}

enum hdcp_message_status dc_process_hdcp_msg(
	enum signal_type signal,
	struct dc_link *link,
	struct hdcp_protection_message *message_info)
{
	enum hdcp_message_status status = HDCP_MESSAGE_FAILURE;
	uint32_t i = 0;

	const struct protection_properties *protection_props;

	if (!message_info)
		return HDCP_MESSAGE_UNSUPPORTED;

	if (message_info->msg_id < HDCP_MESSAGE_ID_READ_BKSV ||
		message_info->msg_id >= HDCP_MESSAGE_ID_MAX)
		return HDCP_MESSAGE_UNSUPPORTED;

	protection_props =
		get_protection_properties_by_signal(
			link,
			signal,
			message_info->version);

	if (!protection_props->supported)
		return HDCP_MESSAGE_UNSUPPORTED;

	if (protection_props->process_transaction(
		link,
		message_info)) {
		status = HDCP_MESSAGE_SUCCESS;
	} else {
		for (i = 0; i < message_info->max_retries; i++) {
			if (protection_props->process_transaction(
						link,
						message_info)) {
				status = HDCP_MESSAGE_SUCCESS;
				break;
			}
		}
	}

	return status;
}

