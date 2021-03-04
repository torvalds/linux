/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 HiSilicon Limited. */
#ifndef HISI_ZIP_H
#define HISI_ZIP_H

#undef pr_fmt
#define pr_fmt(fmt)	"hisi_zip: " fmt

#include <linux/list.h>
#include "../qm.h"

enum hisi_zip_error_type {
	/* negative compression */
	HZIP_NC_ERR = 0x0d,
};

struct hisi_zip_dfx {
	atomic64_t send_cnt;
	atomic64_t recv_cnt;
	atomic64_t send_busy_cnt;
	atomic64_t err_bd_cnt;
};

struct hisi_zip_ctrl;

struct hisi_zip {
	struct hisi_qm qm;
	struct hisi_zip_ctrl *ctrl;
	struct hisi_zip_dfx dfx;
};

struct hisi_zip_sqe {
	u32 consumed;
	u32 produced;
	u32 comp_data_length;
	u32 dw3;
	u32 input_data_length;
	u32 lba_l;
	u32 lba_h;
	u32 dw7;
	u32 dw8;
	u32 dw9;
	u32 dw10;
	u32 priv_info;
	u32 dw12;
	u32 tag;
	u32 dest_avail_out;
	u32 rsvd0;
	u32 comp_head_addr_l;
	u32 comp_head_addr_h;
	u32 source_addr_l;
	u32 source_addr_h;
	u32 dest_addr_l;
	u32 dest_addr_h;
	u32 stream_ctx_addr_l;
	u32 stream_ctx_addr_h;
	u32 cipher_key1_addr_l;
	u32 cipher_key1_addr_h;
	u32 cipher_key2_addr_l;
	u32 cipher_key2_addr_h;
	u32 rsvd1[4];
};

int zip_create_qps(struct hisi_qp **qps, int ctx_num, int node);
int hisi_zip_register_to_crypto(struct hisi_qm *qm);
void hisi_zip_unregister_from_crypto(struct hisi_qm *qm);
#endif
