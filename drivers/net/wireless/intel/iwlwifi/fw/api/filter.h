/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2012-2014 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#ifndef __iwl_fw_api_filter_h__
#define __iwl_fw_api_filter_h__

#include "fw/api/mac.h"

#define MAX_PORT_ID_NUM	2
#define MAX_MCAST_FILTERING_ADDRESSES 256

/**
 * struct iwl_mcast_filter_cmd - configure multicast filter.
 * @filter_own: Set 1 to filter out multicast packets sent by station itself
 * @port_id:	Multicast MAC addresses array specifier. This is a strange way
 *		to identify network interface adopted in host-device IF.
 *		It is used by FW as index in array of addresses. This array has
 *		MAX_PORT_ID_NUM members.
 * @count:	Number of MAC addresses in the array
 * @pass_all:	Set 1 to pass all multicast packets.
 * @bssid:	current association BSSID.
 * @reserved:	reserved
 * @addr_list:	Place holder for array of MAC addresses.
 *		IMPORTANT: add padding if necessary to ensure DWORD alignment.
 */
struct iwl_mcast_filter_cmd {
	u8 filter_own;
	u8 port_id;
	u8 count;
	u8 pass_all;
	u8 bssid[6];
	u8 reserved[2];
	u8 addr_list[0];
} __packed; /* MCAST_FILTERING_CMD_API_S_VER_1 */

#define MAX_BCAST_FILTERS 8
#define MAX_BCAST_FILTER_ATTRS 2

/**
 * enum iwl_mvm_bcast_filter_attr_offset - written by fw for each Rx packet
 * @BCAST_FILTER_OFFSET_PAYLOAD_START: offset is from payload start.
 * @BCAST_FILTER_OFFSET_IP_END: offset is from ip header end (i.e.
 *	start of ip payload).
 */
enum iwl_mvm_bcast_filter_attr_offset {
	BCAST_FILTER_OFFSET_PAYLOAD_START = 0,
	BCAST_FILTER_OFFSET_IP_END = 1,
};

/**
 * struct iwl_fw_bcast_filter_attr - broadcast filter attribute
 * @offset_type:	&enum iwl_mvm_bcast_filter_attr_offset.
 * @offset:	starting offset of this pattern.
 * @reserved1:	reserved
 * @val:	value to match - big endian (MSB is the first
 *		byte to match from offset pos).
 * @mask:	mask to match (big endian).
 */
struct iwl_fw_bcast_filter_attr {
	u8 offset_type;
	u8 offset;
	__le16 reserved1;
	__be32 val;
	__be32 mask;
} __packed; /* BCAST_FILTER_ATT_S_VER_1 */

/**
 * enum iwl_mvm_bcast_filter_frame_type - filter frame type
 * @BCAST_FILTER_FRAME_TYPE_ALL: consider all frames.
 * @BCAST_FILTER_FRAME_TYPE_IPV4: consider only ipv4 frames
 */
enum iwl_mvm_bcast_filter_frame_type {
	BCAST_FILTER_FRAME_TYPE_ALL = 0,
	BCAST_FILTER_FRAME_TYPE_IPV4 = 1,
};

/**
 * struct iwl_fw_bcast_filter - broadcast filter
 * @discard: discard frame (1) or let it pass (0).
 * @frame_type: &enum iwl_mvm_bcast_filter_frame_type.
 * @reserved1: reserved
 * @num_attrs: number of valid attributes in this filter.
 * @attrs: attributes of this filter. a filter is considered matched
 *	only when all its attributes are matched (i.e. AND relationship)
 */
struct iwl_fw_bcast_filter {
	u8 discard;
	u8 frame_type;
	u8 num_attrs;
	u8 reserved1;
	struct iwl_fw_bcast_filter_attr attrs[MAX_BCAST_FILTER_ATTRS];
} __packed; /* BCAST_FILTER_S_VER_1 */

/**
 * struct iwl_fw_bcast_mac - per-mac broadcast filtering configuration.
 * @default_discard: default action for this mac (discard (1) / pass (0)).
 * @reserved1: reserved
 * @attached_filters: bitmap of relevant filters for this mac.
 */
struct iwl_fw_bcast_mac {
	u8 default_discard;
	u8 reserved1;
	__le16 attached_filters;
} __packed; /* BCAST_MAC_CONTEXT_S_VER_1 */

/**
 * struct iwl_bcast_filter_cmd - broadcast filtering configuration
 * @disable: enable (0) / disable (1)
 * @max_bcast_filters: max number of filters (MAX_BCAST_FILTERS)
 * @max_macs: max number of macs (NUM_MAC_INDEX_DRIVER)
 * @reserved1: reserved
 * @filters: broadcast filters
 * @macs: broadcast filtering configuration per-mac
 */
struct iwl_bcast_filter_cmd {
	u8 disable;
	u8 max_bcast_filters;
	u8 max_macs;
	u8 reserved1;
	struct iwl_fw_bcast_filter filters[MAX_BCAST_FILTERS];
	struct iwl_fw_bcast_mac macs[NUM_MAC_INDEX_DRIVER];
} __packed; /* BCAST_FILTERING_HCMD_API_S_VER_1 */

#endif /* __iwl_fw_api_filter_h__ */
