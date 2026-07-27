/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2026 Morse Micro
 */

#ifndef _MM81X_YAPS_HW_H_
#define _MM81X_YAPS_HW_H_

#include <linux/types.h>
#include <linux/crc7.h>

#define MM81X_INT_YAPS_FC_PKT_WAITING_IRQN 0
#define MM81X_INT_YAPS_FC_PACKET_FREED_UP_IRQN 1

struct mm81x_yaps_hw_table {
	/* NOTE: We need these padding bytes for yaps to work */
	u8 padding[4];
	__le32 ysl_addr;
	__le32 yds_addr;
	__le32 status_regs_addr;

	/* Alloc pool sizes */
	__le16 tc_tx_pool_size;
	__le16 fc_rx_pool_size;
	u8 tc_cmd_pool_size;
	u8 tc_beacon_pool_size;
	u8 tc_mgmt_pool_size;
	u8 fc_resp_pool_size;
	u8 fc_tx_sts_pool_size;
	u8 fc_aux_pool_size;

	/* To chip/from chip queue sizes */
	u8 tc_tx_q_size;
	u8 tc_cmd_q_size;
	u8 tc_beacon_q_size;
	u8 tc_mgmt_q_size;
	u8 fc_q_size;
	u8 fc_done_q_size;

	__le16 yaps_reserved_page_size;
	__le16 reserved_unused;
} __packed;

struct mm81x;

void mm81x_yaps_hw_enable_irqs(struct mm81x *mors, bool enable);
int mm81x_yaps_hw_init(struct mm81x *mors);
void mm81x_yaps_hw_finish(struct mm81x *mors);
void mm81x_yaps_hw_read_table(struct mm81x *mors,
			      struct mm81x_yaps_hw_table *tbl_ptr);

#endif /* !_MM81X_YAPS_HW_H_ */
