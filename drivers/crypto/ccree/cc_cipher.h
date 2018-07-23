/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2012-2018 ARM Limited or its affiliates. */

/* \file cc_cipher.h
 * ARM CryptoCell Cipher Crypto API
 */

#ifndef __CC_CIPHER_H__
#define __CC_CIPHER_H__

#include <linux/kernel.h>
#include <crypto/algapi.h>
#include "cc_driver.h"
#include "cc_buffer_mgr.h"

struct cipher_req_ctx {
	struct async_gen_req_ctx gen_ctx;
	enum cc_req_dma_buf_type dma_buf_type;
	u32 in_nents;
	u32 in_mlli_nents;
	u32 out_nents;
	u32 out_mlli_nents;
	u8 *backup_info; /*store iv for generated IV flow*/
	u8 *iv;
	bool is_giv;
	struct mlli_params mlli_params;
};

int cc_cipher_alloc(struct cc_drvdata *drvdata);

int cc_cipher_free(struct cc_drvdata *drvdata);

struct cc_hkey_info {
	u16 keylen;
	u8 hw_key1;
	u8 hw_key2;
} __packed;

#define CC_HW_KEY_SIZE sizeof(struct cc_hkey_info)

#endif /*__CC_CIPHER_H__*/
