/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2021, 2025 Intel Corporation
 */
#ifndef __iwl_fw_dhc_utils_h__
#define __iwl_fw_dhc_utils_h__

#include <linux/types.h>
#include "fw/img.h"
#include "api/commands.h"
#include "api/dhc.h"

/**
 * iwl_dhc_resp_status - return status of DHC response
 * @fw: firwmware image information
 * @pkt: response packet, must not be %NULL
 *
 * Returns: the status value of the DHC command or (u32)-1 if the
 *	    response was too short.
 */
static inline u32 iwl_dhc_resp_status(const struct iwl_fw *fw,
				      struct iwl_rx_packet *pkt)
{
	if (iwl_fw_lookup_notif_ver(fw, IWL_ALWAYS_LONG_GROUP,
				    DEBUG_HOST_COMMAND, 1) >= 2) {
		struct iwl_dhc_cmd_resp *resp = (void *)pkt->data;

		if (iwl_rx_packet_payload_len(pkt) < sizeof(*resp))
			return (u32)-1;

		return le32_to_cpu(resp->status);
	} else {
		struct iwl_dhc_cmd_resp_v1 *resp = (void *)pkt->data;

		if (iwl_rx_packet_payload_len(pkt) < sizeof(*resp))
			return (u32)-1;

		return le32_to_cpu(resp->status);
	}
}

/**
 * iwl_dhc_resp_data - return data pointer of DHC response
 * @fw: firwmware image information
 * @pkt: response packet, must not be %NULL
 * @len: where to store the length
 *
 * Returns: The data pointer, or an ERR_PTR() if the data was
 *	    not valid (too short).
 */
static inline void *iwl_dhc_resp_data(const struct iwl_fw *fw,
				      struct iwl_rx_packet *pkt,
				      unsigned int *len)
{
	if (iwl_fw_lookup_notif_ver(fw, IWL_ALWAYS_LONG_GROUP,
				    DEBUG_HOST_COMMAND, 1) >= 2) {
		struct iwl_dhc_cmd_resp *resp = (void *)pkt->data;

		if (iwl_rx_packet_payload_len(pkt) < sizeof(*resp))
			return ERR_PTR(-EINVAL);

		*len = iwl_rx_packet_payload_len(pkt) - sizeof(*resp);
		return (void *)&resp->data;
	} else {
		struct iwl_dhc_cmd_resp_v1 *resp = (void *)pkt->data;

		if (iwl_rx_packet_payload_len(pkt) < sizeof(*resp))
			return ERR_PTR(-EINVAL);

		*len = iwl_rx_packet_payload_len(pkt) - sizeof(*resp);
		return (void *)&resp->data;
	}
}

#endif  /* __iwl_fw_dhc_utils_h__ */
