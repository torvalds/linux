/*
 * cxgb3i_ulp2.h: Chelsio S3xx iSCSI driver.
 *
 * Copyright (c) 2008 Chelsio Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Karen Xie (kxie@chelsio.com)
 */

#ifndef __CXGB3I_ULP2_PDU_H__
#define __CXGB3I_ULP2_PDU_H__

struct cpl_iscsi_hdr_norss {
	union opcode_tid ot;
	u16 pdu_len_ddp;
	u16 len;
	u32 seq;
	u16 urg;
	u8 rsvd;
	u8 status;
};

struct cpl_rx_data_ddp_norss {
	union opcode_tid ot;
	u16 urg;
	u16 len;
	u32 seq;
	u32 nxt_seq;
	u32 ulp_crc;
	u32 ddp_status;
};

#define RX_DDP_STATUS_IPP_SHIFT		27	/* invalid pagepod */
#define RX_DDP_STATUS_TID_SHIFT		26	/* tid mismatch */
#define RX_DDP_STATUS_COLOR_SHIFT	25	/* color mismatch */
#define RX_DDP_STATUS_OFFSET_SHIFT	24	/* offset mismatch */
#define RX_DDP_STATUS_ULIMIT_SHIFT	23	/* ulimit error */
#define RX_DDP_STATUS_TAG_SHIFT		22	/* tag mismatch */
#define RX_DDP_STATUS_DCRC_SHIFT	21	/* dcrc error */
#define RX_DDP_STATUS_HCRC_SHIFT	20	/* hcrc error */
#define RX_DDP_STATUS_PAD_SHIFT		19	/* pad error */
#define RX_DDP_STATUS_PPP_SHIFT		18	/* pagepod parity error */
#define RX_DDP_STATUS_LLIMIT_SHIFT	17	/* llimit error */
#define RX_DDP_STATUS_DDP_SHIFT		16	/* ddp'able */
#define RX_DDP_STATUS_PMM_SHIFT		15	/* pagepod mismatch */

#define ULP2_FLAG_DATA_READY		0x1
#define ULP2_FLAG_DATA_DDPED		0x2
#define ULP2_FLAG_HCRC_ERROR		0x10
#define ULP2_FLAG_DCRC_ERROR		0x20
#define ULP2_FLAG_PAD_ERROR		0x40

void cxgb3i_conn_closing(struct s3_conn *c3cn);
void cxgb3i_conn_pdu_ready(struct s3_conn *c3cn);
void cxgb3i_conn_tx_open(struct s3_conn *c3cn);
#endif
