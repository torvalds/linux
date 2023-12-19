/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2018 Chelsio Communications, Inc. */

#ifndef __CHCR_IPSEC_H__
#define __CHCR_IPSEC_H__

#include "t4_hw.h"
#include "cxgb4.h"
#include "t4_msg.h"
#include "cxgb4_uld.h"

#include "chcr_core.h"
#include "chcr_algo.h"
#include "chcr_crypto.h"

#define CHIPSEC_DRV_MODULE_NAME "ch_ipsec"
#define CHIPSEC_DRV_VERSION "1.0.0.0-ko"
#define CHIPSEC_DRV_DESC "Chelsio T6 Crypto Ipsec offload Driver"

struct ipsec_uld_ctx {
	struct list_head entry;
	struct cxgb4_lld_info lldi;
};

struct chcr_ipsec_req {
	struct ulp_txpkt ulptx;
	struct ulptx_idata sc_imm;
	struct cpl_tx_sec_pdu sec_cpl;
	struct _key_ctx key_ctx;
};

struct chcr_ipsec_wr {
	struct fw_ulptx_wr wreq;
	struct chcr_ipsec_req req;
};

#define ESN_IV_INSERT_OFFSET 12
struct chcr_ipsec_aadiv {
	__be32 spi;
	u8 seq_no[8];
	u8 iv[8];
};

struct ipsec_sa_entry {
	int hmac_ctrl;
	u16 esn;
	u16 resv;
	unsigned int enckey_len;
	unsigned int kctx_len;
	unsigned int authsize;
	__be32 key_ctx_hdr;
	char salt[MAX_SALT];
	char key[2 * AES_MAX_KEY_SIZE];
};

#endif /* __CHCR_IPSEC_H__ */

