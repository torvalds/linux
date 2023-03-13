/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

#ifndef DC_HDMI_TYPES_H
#define DC_HDMI_TYPES_H

#include "os_types.h"

/* Address range from 0x00 to 0x1F.*/
#define DP_ADAPTOR_TYPE2_SIZE 0x20
#define DP_ADAPTOR_TYPE2_REG_ID 0x10
#define DP_ADAPTOR_TYPE2_REG_MAX_TMDS_CLK 0x1D
/* Identifies adaptor as Dual-mode adaptor */
#define DP_ADAPTOR_TYPE2_ID 0xA0
/* MHz*/
#define DP_ADAPTOR_TYPE2_MAX_TMDS_CLK 600
/* MHz*/
#define DP_ADAPTOR_TYPE2_MIN_TMDS_CLK 25
/* kHZ*/
#define DP_ADAPTOR_DVI_MAX_TMDS_CLK 165000
/* kHZ*/
#define DP_ADAPTOR_HDMI_SAFE_MAX_TMDS_CLK 165000

struct dp_hdmi_dongle_signature_data {
	int8_t id[15];/* "DP-HDMI ADAPTOR"*/
	uint8_t eot;/* end of transmition '\x4' */
};

/* DP-HDMI dongle slave address for retrieving dongle signature*/
#define DP_HDMI_DONGLE_ADDRESS 0x40
static const uint8_t dp_hdmi_dongle_signature_str[] = "DP-HDMI ADAPTOR";
#define DP_HDMI_DONGLE_SIGNATURE_EOT 0x04


/* SCDC Address defines (HDMI 2.0)*/
#define HDMI_SCDC_WRITE_UPDATE_0_ARRAY 3
#define HDMI_SCDC_ADDRESS  0x54
#define HDMI_SCDC_SINK_VERSION 0x01
#define HDMI_SCDC_SOURCE_VERSION 0x02
#define HDMI_SCDC_UPDATE_0 0x10
#define HDMI_SCDC_TMDS_CONFIG 0x20
#define HDMI_SCDC_SCRAMBLER_STATUS 0x21
#define HDMI_SCDC_CONFIG_0 0x30
#define HDMI_SCDC_CONFIG_1 0x31
#define HDMI_SCDC_SOURCE_TEST_REQ 0x35
#define HDMI_SCDC_STATUS_FLAGS 0x40
#define HDMI_SCDC_ERR_DETECT 0x50
#define HDMI_SCDC_TEST_CONFIG 0xC0

#define HDMI_SCDC_MANUFACTURER_OUI 0xD0
#define HDMI_SCDC_DEVICE_ID 0xDB

union hdmi_scdc_update_read_data {
	uint8_t byte[2];
	struct {
		uint8_t STATUS_UPDATE:1;
		uint8_t CED_UPDATE:1;
		uint8_t RR_TEST:1;
		uint8_t RESERVED:5;
		uint8_t RESERVED2:8;
	} fields;
};

union hdmi_scdc_status_flags_data {
	uint8_t byte;
	struct {
		uint8_t CLOCK_DETECTED:1;
		uint8_t CH0_LOCKED:1;
		uint8_t CH1_LOCKED:1;
		uint8_t CH2_LOCKED:1;
		uint8_t RESERVED:4;
	} fields;
};

union hdmi_scdc_ced_data {
	uint8_t byte[11];
	struct {
		uint8_t CH0_8LOW:8;
		uint8_t CH0_7HIGH:7;
		uint8_t CH0_VALID:1;
		uint8_t CH1_8LOW:8;
		uint8_t CH1_7HIGH:7;
		uint8_t CH1_VALID:1;
		uint8_t CH2_8LOW:8;
		uint8_t CH2_7HIGH:7;
		uint8_t CH2_VALID:1;
		uint8_t CHECKSUM:8;
		uint8_t RESERVED:8;
		uint8_t RESERVED2:8;
		uint8_t RESERVED3:8;
		uint8_t RESERVED4:4;
	} fields;
};

union hdmi_scdc_manufacturer_OUI_data {
	uint8_t byte[3];
	struct {
		uint8_t Manufacturer_OUI_1:8;
		uint8_t Manufacturer_OUI_2:8;
		uint8_t Manufacturer_OUI_3:8;
	} fields;
};

union hdmi_scdc_device_id_data {
	uint8_t byte;
	struct {
		uint8_t Hardware_Minor_Rev:4;
		uint8_t Hardware_Major_Rev:4;
	} fields;
};

#endif /* DC_HDMI_TYPES_H */
