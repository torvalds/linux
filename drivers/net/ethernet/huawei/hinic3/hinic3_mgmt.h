/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved. */

#ifndef _HINIC3_MGMT_H_
#define _HINIC3_MGMT_H_

#include <linux/types.h>

#include "hinic3_mbox.h"
#include "hinic3_hw_intf.h"

struct hinic3_hwdev;

struct hinic3_recv_msg {
	/* Preallocated buffer of size MAX_PF_MGMT_BUF_SIZE that accumulates
	 * receive message, segment-by-segment.
	 */
	void                 *msg;
	/* Message id for which segments are accumulated. */
	u8                   msg_id;
	/* Sequence id of last received segment of current message. */
	u8                   seq_id;
	u16                  msg_len;
	int                  async_mgmt_to_pf;
	enum mgmt_mod_type   mod;
	u16                  cmd;
	struct completion    recv_done;
};

enum comm_pf_to_mgmt_event_state {
	COMM_SEND_EVENT_UNINIT,
	COMM_SEND_EVENT_START,
	COMM_SEND_EVENT_SUCCESS,
	COMM_SEND_EVENT_TIMEOUT,
};

struct hinic3_msg_pf_to_mgmt {
	struct hinic3_hwdev              *hwdev;
	struct workqueue_struct          *workq;
	void                             *mgmt_ack_buf;
	struct hinic3_recv_msg           recv_msg_from_mgmt;
	struct hinic3_recv_msg           recv_resp_msg_from_mgmt;
	u16                              async_msg_id;
	u16                              sync_msg_id;
	void                             *async_msg_cb_data[MGMT_MOD_HW_MAX];
	/* synchronizes message send with message receives via event queue */
	spinlock_t                       sync_event_lock;
	enum comm_pf_to_mgmt_event_state event_flag;
};

struct mgmt_msg_handle_work {
	struct work_struct           work;
	struct hinic3_msg_pf_to_mgmt *pf_to_mgmt;
	void                         *msg;
	u16                          msg_len;
	enum mgmt_mod_type           mod;
	u16                          cmd;
	u16                          msg_id;
	int                          async_mgmt_to_pf;
};

int hinic3_pf_to_mgmt_init(struct hinic3_hwdev *hwdev);
void hinic3_pf_to_mgmt_free(struct hinic3_hwdev *hwdev);
void hinic3_flush_mgmt_workq(struct hinic3_hwdev *hwdev);
void hinic3_mgmt_msg_aeqe_handler(struct hinic3_hwdev *hwdev,
				  u8 *header, u8 size);

#endif
