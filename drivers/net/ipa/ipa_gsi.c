// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
 */

#include <linux/types.h>

#include "ipa_gsi.h"
#include "gsi_trans.h"
#include "ipa.h"
#include "ipa_endpoint.h"
#include "ipa_data.h"

void ipa_gsi_trans_complete(struct gsi_trans *trans)
{
	struct ipa *ipa = container_of(trans->gsi, struct ipa, gsi);

	ipa_endpoint_trans_complete(ipa->channel_map[trans->channel_id], trans);
}

void ipa_gsi_trans_release(struct gsi_trans *trans)
{
	struct ipa *ipa = container_of(trans->gsi, struct ipa, gsi);

	ipa_endpoint_trans_release(ipa->channel_map[trans->channel_id], trans);
}

void ipa_gsi_channel_tx_queued(struct gsi *gsi, u32 channel_id, u32 count,
			       u32 byte_count)
{
	struct ipa *ipa = container_of(gsi, struct ipa, gsi);
	struct ipa_endpoint *endpoint;

	endpoint = ipa->channel_map[channel_id];
	if (endpoint->netdev)
		netdev_sent_queue(endpoint->netdev, byte_count);
}

void ipa_gsi_channel_tx_completed(struct gsi *gsi, u32 channel_id, u32 count,
				  u32 byte_count)
{
	struct ipa *ipa = container_of(gsi, struct ipa, gsi);
	struct ipa_endpoint *endpoint;

	endpoint = ipa->channel_map[channel_id];
	if (endpoint->netdev)
		netdev_completed_queue(endpoint->netdev, count, byte_count);
}

/* Indicate whether an endpoint config data entry is "empty" */
bool ipa_gsi_endpoint_data_empty(const struct ipa_gsi_endpoint_data *data)
{
	return data->ee_id == GSI_EE_AP && !data->channel.tlv_count;
}
