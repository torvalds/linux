#ifndef __GB_GPBRIDGE_H__
#define __GB_GPBRIDGE_H__
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2014 - 2015 Google Inc. All rights reserved.
 * Copyright(c) 2014 - 2015 Linaro Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2014 - 2015 Google Inc. All rights reserved.
 * Copyright(c) 2014 - 2015 Linaro Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of Google Inc. or Linaro Ltd. nor the names of
 *    its contributors may be used to endorse or promote products
 *    derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL GOOGLE INC. OR
 * LINARO LTD. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/* I2C */

/* Version of the Greybus i2c protocol we support */
#define	GB_I2C_VERSION_MAJOR		0x00
#define	GB_I2C_VERSION_MINOR		0x01

/* Greybus i2c request types */
#define	GB_I2C_TYPE_INVALID		0x00
#define	GB_I2C_TYPE_PROTOCOL_VERSION	0x01
#define	GB_I2C_TYPE_FUNCTIONALITY	0x02
#define	GB_I2C_TYPE_TIMEOUT		0x03
#define	GB_I2C_TYPE_RETRIES		0x04
#define	GB_I2C_TYPE_TRANSFER		0x05
#define	GB_I2C_TYPE_RESPONSE		0x80	/* OR'd with rest */

#define	GB_I2C_RETRIES_DEFAULT		3
#define	GB_I2C_TIMEOUT_DEFAULT		1000	/* milliseconds */

/* functionality request has no payload */
struct gb_i2c_functionality_response {
	__le32	functionality;
};

struct gb_i2c_timeout_request {
	__le16	msec;
};
/* timeout response has no payload */

struct gb_i2c_retries_request {
	__u8	retries;
};
/* retries response has no payload */

/*
 * Outgoing data immediately follows the op count and ops array.
 * The data for each write (master -> slave) op in the array is sent
 * in order, with no (e.g. pad) bytes separating them.
 *
 * Short reads cause the entire transfer request to fail So response
 * payload consists only of bytes read, and the number of bytes is
 * exactly what was specified in the corresponding op.  Like
 * outgoing data, the incoming data is in order and contiguous.
 */
struct gb_i2c_transfer_op {
	__le16	addr;
	__le16	flags;
	__le16	size;
};

struct gb_i2c_transfer_request {
	__le16				op_count;
	struct gb_i2c_transfer_op	ops[0];		/* op_count of these */
};
struct gb_i2c_transfer_response {
	__u8				data[0];	/* inbound data */
};


/* GPIO */

/* Version of the Greybus GPIO protocol we support */
#define	GB_GPIO_VERSION_MAJOR		0x00
#define	GB_GPIO_VERSION_MINOR		0x01

/* Greybus GPIO request types */
#define	GB_GPIO_TYPE_INVALID		0x00
#define	GB_GPIO_TYPE_PROTOCOL_VERSION	0x01
#define	GB_GPIO_TYPE_LINE_COUNT		0x02
#define	GB_GPIO_TYPE_ACTIVATE		0x03
#define	GB_GPIO_TYPE_DEACTIVATE		0x04
#define	GB_GPIO_TYPE_GET_DIRECTION	0x05
#define	GB_GPIO_TYPE_DIRECTION_IN	0x06
#define	GB_GPIO_TYPE_DIRECTION_OUT	0x07
#define	GB_GPIO_TYPE_GET_VALUE		0x08
#define	GB_GPIO_TYPE_SET_VALUE		0x09
#define	GB_GPIO_TYPE_SET_DEBOUNCE	0x0a
#define GB_GPIO_TYPE_IRQ_TYPE		0x0b
#define GB_GPIO_TYPE_IRQ_ACK		0x0c
#define GB_GPIO_TYPE_IRQ_MASK		0x0d
#define GB_GPIO_TYPE_IRQ_UNMASK		0x0e
#define GB_GPIO_TYPE_IRQ_EVENT		0x0f
#define	GB_GPIO_TYPE_RESPONSE		0x80	/* OR'd with rest */

#define	GB_GPIO_DEBOUNCE_USEC_DEFAULT	0	/* microseconds */

/* line count request has no payload */
struct gb_gpio_line_count_response {
	__u8	count;
};

struct gb_gpio_activate_request {
	__u8	which;
};
/* activate response has no payload */

struct gb_gpio_deactivate_request {
	__u8	which;
};
/* deactivate response has no payload */

struct gb_gpio_get_direction_request {
	__u8	which;
};
struct gb_gpio_get_direction_response {
	__u8	direction;
};

struct gb_gpio_direction_in_request {
	__u8	which;
};
/* direction in response has no payload */

struct gb_gpio_direction_out_request {
	__u8	which;
	__u8	value;
};
/* direction out response has no payload */

struct gb_gpio_get_value_request {
	__u8	which;
};
struct gb_gpio_get_value_response {
	__u8	value;
};

struct gb_gpio_set_value_request {
	__u8	which;
	__u8	value;
};
/* set value response has no payload */

struct gb_gpio_set_debounce_request {
	__u8	which;
	__le16	usec __packed;
};
/* debounce response has no payload */

struct gb_gpio_irq_type_request {
	__u8	which;
	__u8	type;
};
/* irq type response has no payload */

struct gb_gpio_irq_mask_request {
	__u8	which;
};
/* irq mask response has no payload */

struct gb_gpio_irq_unmask_request {
	__u8	which;
};
/* irq unmask response has no payload */

struct gb_gpio_irq_ack_request {
	__u8	which;
};
/* irq ack response has no payload */

/* irq event requests originate on another module and are handled on the AP */
struct gb_gpio_irq_event_request {
	__u8	which;
};
/* irq event response has no payload */


/* PWM */

/* Version of the Greybus PWM protocol we support */
#define	GB_PWM_VERSION_MAJOR		0x00
#define	GB_PWM_VERSION_MINOR		0x01

/* Greybus PWM request types */
#define	GB_PWM_TYPE_INVALID		0x00
#define	GB_PWM_TYPE_PROTOCOL_VERSION	0x01
#define	GB_PWM_TYPE_PWM_COUNT		0x02
#define	GB_PWM_TYPE_ACTIVATE		0x03
#define	GB_PWM_TYPE_DEACTIVATE		0x04
#define	GB_PWM_TYPE_CONFIG		0x05
#define	GB_PWM_TYPE_POLARITY		0x06
#define	GB_PWM_TYPE_ENABLE		0x07
#define	GB_PWM_TYPE_DISABLE		0x08
#define	GB_PWM_TYPE_RESPONSE		0x80	/* OR'd with rest */

/* pwm count request has no payload */
struct gb_pwm_count_response {
	__u8	count;
};

struct gb_pwm_activate_request {
	__u8	which;
};

struct gb_pwm_deactivate_request {
	__u8	which;
};

struct gb_pwm_config_request {
	__u8	which;
	__le32	duty __packed;
	__le32	period __packed;
};

struct gb_pwm_polarity_request {
	__u8	which;
	__u8	polarity;
};

struct gb_pwm_enable_request {
	__u8	which;
};

struct gb_pwm_disable_request {
	__u8	which;
};

/* I2S */
#ifndef BIT
#define BIT(n) (1UL << (n))
#endif

#define GB_I2S_MGMT_TYPE_PROTOCOL_VERSION		0x01
#define GB_I2S_MGMT_TYPE_GET_SUPPORTED_CONFIGURATIONS	0x02
#define GB_I2S_MGMT_TYPE_SET_CONFIGURATION		0x03
#define GB_I2S_MGMT_TYPE_SET_SAMPLES_PER_MESSAGE	0x04
#define GB_I2S_MGMT_TYPE_GET_PROCESSING_DELAY		0x05
#define GB_I2S_MGMT_TYPE_SET_START_DELAY		0x06
#define GB_I2S_MGMT_TYPE_ACTIVATE_CPORT			0x07
#define GB_I2S_MGMT_TYPE_DEACTIVATE_CPORT		0x08
#define GB_I2S_MGMT_TYPE_REPORT_EVENT			0x09

#define GB_I2S_MGMT_BYTE_ORDER_NA			BIT(0)
#define GB_I2S_MGMT_BYTE_ORDER_BE			BIT(1)
#define GB_I2S_MGMT_BYTE_ORDER_LE			BIT(2)

#define GB_I2S_MGMT_SPATIAL_LOCATION_FL			BIT(0)
#define GB_I2S_MGMT_SPATIAL_LOCATION_FR			BIT(1)
#define GB_I2S_MGMT_SPATIAL_LOCATION_FC			BIT(2)
#define GB_I2S_MGMT_SPATIAL_LOCATION_LFE		BIT(3)
#define GB_I2S_MGMT_SPATIAL_LOCATION_BL			BIT(4)
#define GB_I2S_MGMT_SPATIAL_LOCATION_BR			BIT(5)
#define GB_I2S_MGMT_SPATIAL_LOCATION_FLC		BIT(6)
#define GB_I2S_MGMT_SPATIAL_LOCATION_FRC		BIT(7)
#define GB_I2S_MGMT_SPATIAL_LOCATION_C			BIT(8) /* BC in USB */
#define GB_I2S_MGMT_SPATIAL_LOCATION_SL			BIT(9)
#define GB_I2S_MGMT_SPATIAL_LOCATION_SR			BIT(10)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TC			BIT(11)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TFL		BIT(12)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TFC		BIT(13)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TFR		BIT(14)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TBL		BIT(15)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TBC		BIT(16)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TBR		BIT(17)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TFLC		BIT(18)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TFRC		BIT(19)
#define GB_I2S_MGMT_SPATIAL_LOCATION_LLFE		BIT(20)
#define GB_I2S_MGMT_SPATIAL_LOCATION_RLFE		BIT(21)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TSL		BIT(22)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TSR		BIT(23)
#define GB_I2S_MGMT_SPATIAL_LOCATION_BC			BIT(24)
#define GB_I2S_MGMT_SPATIAL_LOCATION_BLC		BIT(25)
#define GB_I2S_MGMT_SPATIAL_LOCATION_BRC		BIT(26)
#define GB_I2S_MGMT_SPATIAL_LOCATION_RD			BIT(31)

#define GB_I2S_MGMT_PROTOCOL_PCM			BIT(0)
#define GB_I2S_MGMT_PROTOCOL_I2S			BIT(1)
#define GB_I2S_MGMT_PROTOCOL_LR_STEREO			BIT(2)

#define GB_I2S_MGMT_ROLE_MASTER				BIT(0)
#define GB_I2S_MGMT_ROLE_SLAVE				BIT(1)

#define GB_I2S_MGMT_POLARITY_NORMAL			BIT(0)
#define GB_I2S_MGMT_POLARITY_REVERSED			BIT(1)

#define GB_I2S_MGMT_EDGE_RISING				BIT(0)
#define GB_I2S_MGMT_EDGE_FALLING			BIT(1)

#define GB_I2S_MGMT_EVENT_UNSPECIFIED			0x1
#define GB_I2S_MGMT_EVENT_HALT				0x2
#define GB_I2S_MGMT_EVENT_INTERNAL_ERROR		0x3
#define GB_I2S_MGMT_EVENT_PROTOCOL_ERROR		0x4
#define GB_I2S_MGMT_EVENT_FAILURE			0x5
#define GB_I2S_MGMT_EVENT_OUT_OF_SEQUENCE		0x6
#define GB_I2S_MGMT_EVENT_UNDERRUN			0x7
#define GB_I2S_MGMT_EVENT_OVERRUN			0x8
#define GB_I2S_MGMT_EVENT_CLOCKING			0x9
#define GB_I2S_MGMT_EVENT_DATA_LEN			0xa

struct gb_i2s_mgmt_configuration {
	__le32	sample_frequency;
	__u8	num_channels;
	__u8	bytes_per_channel;
	__u8	byte_order;
	__u8	pad;
	__le32	spatial_locations;
	__le32	ll_protocol;
	__u8	ll_bclk_role;
	__u8	ll_wclk_role;
	__u8	ll_wclk_polarity;
	__u8	ll_wclk_change_edge;
	__u8	ll_wclk_tx_edge;
	__u8	ll_wclk_rx_edge;
	__u8	ll_data_offset;
	__u8	ll_pad;
};

/* get supported configurations request has no payload */
struct gb_i2s_mgmt_get_supported_configurations_response {
	__u8					config_count;
	__u8					pad[3];
	struct gb_i2s_mgmt_configuration	config[0];
};

struct gb_i2s_mgmt_set_configuration_request {
	struct gb_i2s_mgmt_configuration	config;
};
/* set configuration response has no payload */

struct gb_i2s_mgmt_set_samples_per_message_request {
	__le16	samples_per_message;
};
/* set samples per message response has no payload */

/* get processing request delay has no payload */
struct gb_i2s_mgmt_get_processing_delay_response {
	__le32	microseconds;
};

struct gb_i2s_mgmt_set_start_delay_request {
	__le32	microseconds;
};
/* set start delay response has no payload */

struct gb_i2s_mgmt_activate_cport_request {
	__le16	cport;
};
/* activate cport response has no payload */

struct gb_i2s_mgmt_deactivate_cport_request {
	__le16	cport;
};
/* deactivate cport response has no payload */

struct gb_i2s_mgmt_report_event_request {
	__u8	event;
};
/* report event response has no payload */

#define GB_I2S_DATA_TYPE_PROTOCOL_VERSION		0x01
#define GB_I2S_DATA_TYPE_SEND_DATA			0x02

struct gb_i2s_send_data_request {
	__le32	sample_number;
	__le32	size;
	__u8	data[0];
};
/* send data has no response at all */

#endif /* __GB_GPBRIDGE_H__ */
