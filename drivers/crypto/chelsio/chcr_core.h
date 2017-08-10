/*
 * This file is part of the Chelsio T6 Crypto driver for Linux.
 *
 * Copyright (c) 2003-2016 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef __CHCR_CORE_H__
#define __CHCR_CORE_H__

#include <crypto/algapi.h>
#include "t4_hw.h"
#include "cxgb4.h"
#include "cxgb4_uld.h"

#define DRV_MODULE_NAME "chcr"
#define DRV_VERSION "1.0.0.0"

#define MAX_PENDING_REQ_TO_HW 20
#define CHCR_TEST_RESPONSE_TIMEOUT 1000

#define PAD_ERROR_BIT		1
#define CHK_PAD_ERR_BIT(x)	(((x) >> PAD_ERROR_BIT) & 1)

#define MAC_ERROR_BIT		0
#define CHK_MAC_ERR_BIT(x)	(((x) >> MAC_ERROR_BIT) & 1)
#define MAX_SALT                4
#define WR_MIN_LEN (sizeof(struct chcr_wr) + \
		    sizeof(struct cpl_rx_phys_dsgl) + \
		    sizeof(struct ulptx_sgl))

#define padap(dev) pci_get_drvdata(dev->u_ctx->lldi.pdev)

struct uld_ctx;

struct _key_ctx {
	__be32 ctx_hdr;
	u8 salt[MAX_SALT];
	__be64 reserverd;
	unsigned char key[0];
};

struct chcr_wr {
	struct fw_crypto_lookaside_wr wreq;
	struct ulp_txpkt ulptx;
	struct ulptx_idata sc_imm;
	struct cpl_tx_sec_pdu sec_cpl;
	struct _key_ctx key_ctx;
};

struct chcr_dev {
	spinlock_t lock_chcr_dev;
	struct uld_ctx *u_ctx;
	unsigned char tx_channel_id;
	unsigned char rx_channel_id;
};

struct uld_ctx {
	struct list_head entry;
	struct cxgb4_lld_info lldi;
	struct chcr_dev *dev;
};

struct uld_ctx * assign_chcr_device(void);
int chcr_send_wr(struct sk_buff *skb);
int start_crypto(void);
int stop_crypto(void);
int chcr_uld_rx_handler(void *handle, const __be64 *rsp,
			const struct pkt_gl *pgl);
int chcr_handle_resp(struct crypto_async_request *req, unsigned char *input,
		     int err);
#endif /* __CHCR_CORE_H__ */
