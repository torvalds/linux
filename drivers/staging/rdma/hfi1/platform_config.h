/*
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#ifndef __PLATFORM_CONFIG_H
#define __PLATFORM_CONFIG_H

#define METADATA_TABLE_FIELD_START_SHIFT		0
#define METADATA_TABLE_FIELD_START_LEN_BITS		15
#define METADATA_TABLE_FIELD_LEN_SHIFT			16
#define METADATA_TABLE_FIELD_LEN_LEN_BITS		16

/* Header structure */
#define PLATFORM_CONFIG_HEADER_RECORD_IDX_SHIFT			0
#define PLATFORM_CONFIG_HEADER_RECORD_IDX_LEN_BITS		6
#define PLATFORM_CONFIG_HEADER_TABLE_LENGTH_SHIFT		16
#define PLATFORM_CONFIG_HEADER_TABLE_LENGTH_LEN_BITS		12
#define PLATFORM_CONFIG_HEADER_TABLE_TYPE_SHIFT			28
#define PLATFORM_CONFIG_HEADER_TABLE_TYPE_LEN_BITS		4

enum platform_config_table_type_encoding {
	PLATFORM_CONFIG_TABLE_RESERVED,
	PLATFORM_CONFIG_SYSTEM_TABLE,
	PLATFORM_CONFIG_PORT_TABLE,
	PLATFORM_CONFIG_RX_PRESET_TABLE,
	PLATFORM_CONFIG_TX_PRESET_TABLE,
	PLATFORM_CONFIG_QSFP_ATTEN_TABLE,
	PLATFORM_CONFIG_VARIABLE_SETTINGS_TABLE,
	PLATFORM_CONFIG_TABLE_MAX
};

enum platform_config_system_table_fields {
	SYSTEM_TABLE_RESERVED,
	SYSTEM_TABLE_NODE_STRING,
	SYSTEM_TABLE_SYSTEM_IMAGE_GUID,
	SYSTEM_TABLE_NODE_GUID,
	SYSTEM_TABLE_REVISION,
	SYSTEM_TABLE_VENDOR_OUI,
	SYSTEM_TABLE_META_VERSION,
	SYSTEM_TABLE_DEVICE_ID,
	SYSTEM_TABLE_PARTITION_ENFORCEMENT_CAP,
	SYSTEM_TABLE_QSFP_POWER_CLASS_MAX,
	SYSTEM_TABLE_QSFP_ATTENUATION_DEFAULT_12G,
	SYSTEM_TABLE_QSFP_ATTENUATION_DEFAULT_25G,
	SYSTEM_TABLE_VARIABLE_TABLE_ENTRIES_PER_PORT,
	SYSTEM_TABLE_MAX
};

enum platform_config_port_table_fields {
	PORT_TABLE_RESERVED,
	PORT_TABLE_PORT_TYPE,
	PORT_TABLE_ATTENUATION_12G,
	PORT_TABLE_ATTENUATION_25G,
	PORT_TABLE_LINK_SPEED_SUPPORTED,
	PORT_TABLE_LINK_WIDTH_SUPPORTED,
	PORT_TABLE_VL_CAP,
	PORT_TABLE_MTU_CAP,
	PORT_TABLE_TX_LANE_ENABLE_MASK,
	PORT_TABLE_LOCAL_MAX_TIMEOUT,
	PORT_TABLE_AUTO_LANE_SHEDDING_ENABLED,
	PORT_TABLE_EXTERNAL_LOOPBACK_ALLOWED,
	PORT_TABLE_TX_PRESET_IDX_PASSIVE_CU,
	PORT_TABLE_TX_PRESET_IDX_ACTIVE_NO_EQ,
	PORT_TABLE_TX_PRESET_IDX_ACTIVE_EQ,
	PORT_TABLE_RX_PRESET_IDX,
	PORT_TABLE_CABLE_REACH_CLASS,
	PORT_TABLE_MAX
};

enum platform_config_rx_preset_table_fields {
	RX_PRESET_TABLE_RESERVED,
	RX_PRESET_TABLE_QSFP_RX_CDR_APPLY,
	RX_PRESET_TABLE_QSFP_RX_EQ_APPLY,
	RX_PRESET_TABLE_QSFP_RX_AMP_APPLY,
	RX_PRESET_TABLE_QSFP_RX_CDR,
	RX_PRESET_TABLE_QSFP_RX_EQ,
	RX_PRESET_TABLE_QSFP_RX_AMP,
	RX_PRESET_TABLE_MAX
};

enum platform_config_tx_preset_table_fields {
	TX_PRESET_TABLE_RESERVED,
	TX_PRESET_TABLE_PRECUR,
	TX_PRESET_TABLE_ATTN,
	TX_PRESET_TABLE_POSTCUR,
	TX_PRESET_TABLE_QSFP_TX_CDR_APPLY,
	TX_PRESET_TABLE_QSFP_TX_EQ_APPLY,
	TX_PRESET_TABLE_QSFP_TX_CDR,
	TX_PRESET_TABLE_QSFP_TX_EQ,
	TX_PRESET_TABLE_MAX
};

enum platform_config_qsfp_attn_table_fields {
	QSFP_ATTEN_TABLE_RESERVED,
	QSFP_ATTEN_TABLE_TX_PRESET_IDX,
	QSFP_ATTEN_TABLE_RX_PRESET_IDX,
	QSFP_ATTEN_TABLE_MAX
};

enum platform_config_variable_settings_table_fields {
	VARIABLE_SETTINGS_TABLE_RESERVED,
	VARIABLE_SETTINGS_TABLE_TX_PRESET_IDX,
	VARIABLE_SETTINGS_TABLE_RX_PRESET_IDX,
	VARIABLE_SETTINGS_TABLE_MAX
};

struct platform_config_data {
	u32 *table;
	u32 *table_metadata;
	u32 num_table;
};

/*
 * This struct acts as a quick reference into the platform_data binary image
 * and is populated by parse_platform_config(...) depending on the specific
 * META_VERSION
 */
struct platform_config_cache {
	u8  cache_valid;
	struct platform_config_data config_tables[PLATFORM_CONFIG_TABLE_MAX];
};

static const u32 platform_config_table_limits[PLATFORM_CONFIG_TABLE_MAX] = {
	0,
	SYSTEM_TABLE_MAX,
	PORT_TABLE_MAX,
	RX_PRESET_TABLE_MAX,
	TX_PRESET_TABLE_MAX,
	QSFP_ATTEN_TABLE_MAX,
	VARIABLE_SETTINGS_TABLE_MAX
};

/* This section defines default values and encodings for the
 * fields defined for each table above
 */

/*=====================================================
 *  System table encodings
 *====================================================*/
#define PLATFORM_CONFIG_MAGIC_NUM		0x3d4f5041
#define PLATFORM_CONFIG_MAGIC_NUMBER_LEN	4

/*
 * These power classes are the same as defined in SFF 8636 spec rev 2.4
 * describing byte 129 in table 6-16, except enumerated in a different order
 */
enum platform_config_qsfp_power_class_encoding {
	QSFP_POWER_CLASS_1 = 1,
	QSFP_POWER_CLASS_2,
	QSFP_POWER_CLASS_3,
	QSFP_POWER_CLASS_4,
	QSFP_POWER_CLASS_5,
	QSFP_POWER_CLASS_6,
	QSFP_POWER_CLASS_7
};


/*=====================================================
 *  Port table encodings
 *==================================================== */
enum platform_config_port_type_encoding {
	PORT_TYPE_RESERVED,
	PORT_TYPE_DISCONNECTED,
	PORT_TYPE_FIXED,
	PORT_TYPE_VARIABLE,
	PORT_TYPE_QSFP,
	PORT_TYPE_MAX
};

enum platform_config_link_speed_supported_encoding {
	LINK_SPEED_SUPP_12G = 1,
	LINK_SPEED_SUPP_25G,
	LINK_SPEED_SUPP_12G_25G,
	LINK_SPEED_SUPP_MAX
};

/*
 * This is a subset (not strict) of the link downgrades
 * supported. The link downgrades supported are expected
 * to be supplied to the driver by another entity such as
 * the fabric manager
 */
enum platform_config_link_width_supported_encoding {
	LINK_WIDTH_SUPP_1X = 1,
	LINK_WIDTH_SUPP_2X,
	LINK_WIDTH_SUPP_2X_1X,
	LINK_WIDTH_SUPP_3X,
	LINK_WIDTH_SUPP_3X_1X,
	LINK_WIDTH_SUPP_3X_2X,
	LINK_WIDTH_SUPP_3X_2X_1X,
	LINK_WIDTH_SUPP_4X,
	LINK_WIDTH_SUPP_4X_1X,
	LINK_WIDTH_SUPP_4X_2X,
	LINK_WIDTH_SUPP_4X_2X_1X,
	LINK_WIDTH_SUPP_4X_3X,
	LINK_WIDTH_SUPP_4X_3X_1X,
	LINK_WIDTH_SUPP_4X_3X_2X,
	LINK_WIDTH_SUPP_4X_3X_2X_1X,
	LINK_WIDTH_SUPP_MAX
};

enum platform_config_virtual_lane_capability_encoding {
	VL_CAP_VL0 = 1,
	VL_CAP_VL0_1,
	VL_CAP_VL0_2,
	VL_CAP_VL0_3,
	VL_CAP_VL0_4,
	VL_CAP_VL0_5,
	VL_CAP_VL0_6,
	VL_CAP_VL0_7,
	VL_CAP_VL0_8,
	VL_CAP_VL0_9,
	VL_CAP_VL0_10,
	VL_CAP_VL0_11,
	VL_CAP_VL0_12,
	VL_CAP_VL0_13,
	VL_CAP_VL0_14,
	VL_CAP_MAX
};

/* Max MTU */
enum platform_config_mtu_capability_encoding {
	MTU_CAP_256   = 1,
	MTU_CAP_512   = 2,
	MTU_CAP_1024  = 3,
	MTU_CAP_2048  = 4,
	MTU_CAP_4096  = 5,
	MTU_CAP_8192  = 6,
	MTU_CAP_10240 = 7
};

enum platform_config_local_max_timeout_encoding {
	LOCAL_MAX_TIMEOUT_10_MS = 1,
	LOCAL_MAX_TIMEOUT_100_MS,
	LOCAL_MAX_TIMEOUT_1_S,
	LOCAL_MAX_TIMEOUT_10_S,
	LOCAL_MAX_TIMEOUT_100_S,
	LOCAL_MAX_TIMEOUT_1000_S
};

#endif			/*__PLATFORM_CONFIG_H*/
