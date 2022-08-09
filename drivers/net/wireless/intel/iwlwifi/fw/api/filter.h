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

#endif /* __iwl_fw_api_filter_h__ */
