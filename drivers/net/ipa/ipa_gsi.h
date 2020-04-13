/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
 */
#ifndef _IPA_GSI_TRANS_H_
#define _IPA_GSI_TRANS_H_

#include <linux/types.h>

struct gsi_trans;

/**
 * ipa_gsi_trans_complete() - GSI transaction completion callback
 * @trans:	Transaction that has completed
 *
 * This called from the GSI layer to notify the IPA layer that a
 * transaction has completed.
 */
void ipa_gsi_trans_complete(struct gsi_trans *trans);

/**
 * ipa_gsi_trans_release() - GSI transaction release callback
 * @trans:	Transaction whose resources should be freed
 *
 * This called from the GSI layer to notify the IPA layer that a
 * transaction is about to be freed, so any resources associated
 * with it should be released.
 */
void ipa_gsi_trans_release(struct gsi_trans *trans);

/**
 * ipa_gsi_channel_tx_queued() - GSI queued to hardware notification
 * @gsi:	GSI pointer
 * @channel_id:	Channel number
 * @count:	Number of transactions queued
 * @byte_count:	Number of bytes to transfer represented by transactions
 *
 * This called from the GSI layer to notify the IPA layer that some
 * number of transactions have been queued to hardware for execution.
 */
void ipa_gsi_channel_tx_queued(struct gsi *gsi, u32 channel_id, u32 count,
			       u32 byte_count);
/**
 * ipa_gsi_trans_complete() - GSI transaction completion callback
ipa_gsi_channel_tx_completed()
 * @gsi:	GSI pointer
 * @channel_id:	Channel number
 * @count:	Number of transactions completed since last report
 * @byte_count:	Number of bytes transferred represented by transactions
 *
 * This called from the GSI layer to notify the IPA layer that the hardware
 * has reported the completion of some number of transactions.
 */
void ipa_gsi_channel_tx_completed(struct gsi *gsi, u32 channel_id, u32 count,
				  u32 byte_count);

bool ipa_gsi_endpoint_data_empty(const struct ipa_gsi_endpoint_data *data);

#endif /* _IPA_GSI_TRANS_H_ */
