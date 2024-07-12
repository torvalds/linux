/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _FBNIC_FW_H_
#define _FBNIC_FW_H_

#include <linux/types.h>

struct fbnic_dev;
struct fbnic_tlv_msg;

struct fbnic_fw_mbx {
	u8 ready, head, tail;
	struct {
		struct fbnic_tlv_msg	*msg;
		dma_addr_t		addr;
	} buf_info[FBNIC_IPC_MBX_DESC_LEN];
};

void fbnic_mbx_init(struct fbnic_dev *fbd);
void fbnic_mbx_clean(struct fbnic_dev *fbd);
void fbnic_mbx_poll(struct fbnic_dev *fbd);
int fbnic_mbx_poll_tx_ready(struct fbnic_dev *fbd);
void fbnic_mbx_flush_tx(struct fbnic_dev *fbd);

#endif /* _FBNIC_FW_H_ */
