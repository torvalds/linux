/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023-2024, Advanced Micro Devices, Inc.
 */

#ifndef _AMDXDNA_MAILBOX_HELPER_H
#define _AMDXDNA_MAILBOX_HELPER_H

#define TX_TIMEOUT 2000 /* milliseconds */
#define RX_TIMEOUT 5000 /* milliseconds */

struct amdxdna_dev;

struct xdna_notify {
	struct completion       comp;
	u32			*data;
	size_t			size;
	int			error;
};

#define DECLARE_XDNA_MSG_COMMON(name, op, status)			\
	struct name##_req	req = { 0 };				\
	struct name##_resp	resp = { status	};			\
	struct xdna_notify	hdl = {					\
		.error = 0,						\
		.data = (u32 *)&resp,					\
		.size = sizeof(resp),					\
		.comp = COMPLETION_INITIALIZER_ONSTACK(hdl.comp),	\
	};								\
	struct xdna_mailbox_msg msg = {					\
		.send_data = (u8 *)&req,				\
		.send_size = sizeof(req),				\
		.handle = &hdl,						\
		.opcode = op,						\
		.notify_cb = xdna_msg_cb,				\
	}

int xdna_msg_cb(void *handle, const u32 *data, size_t size);
int xdna_send_msg_wait(struct amdxdna_dev *xdna, struct mailbox_channel *chann,
		       struct xdna_mailbox_msg *msg);

#endif /* _AMDXDNA_MAILBOX_HELPER_H */
