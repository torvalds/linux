/* SPDX-License-Identifier: GPL-2.0 */
/*
 * System Control and Management Interface (SCMI) Message Protocol
 * Raw mode support header.
 *
 * Copyright (C) 2022 ARM Ltd.
 */
#ifndef _SCMI_RAW_MODE_H
#define _SCMI_RAW_MODE_H

#include "common.h"

enum {
	SCMI_RAW_REPLY_QUEUE,
	SCMI_RAW_NOTIF_QUEUE,
	SCMI_RAW_ERRS_QUEUE,
	SCMI_RAW_MAX_QUEUE
};

void *scmi_raw_mode_init(const struct scmi_handle *handle,
			 struct dentry *top_dentry, int instance_id,
			 u8 *channels, int num_chans,
			 const struct scmi_desc *desc, int tx_max_msg);
void scmi_raw_mode_cleanup(void *raw);

void scmi_raw_message_report(void *raw, struct scmi_xfer *xfer,
			     unsigned int idx, unsigned int chan_id);
void scmi_raw_error_report(void *raw, struct scmi_chan_info *cinfo,
			   u32 msg_hdr, void *priv);

#endif /* _SCMI_RAW_MODE_H */
