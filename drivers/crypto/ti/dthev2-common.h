/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * K3 DTHE V2 crypto accelerator driver
 *
 * Copyright (C) Texas Instruments 2025 - https://www.ti.com
 * Author: T Pratham <t-pratham@ti.com>
 */

#ifndef __TI_DTHEV2_H__
#define __TI_DTHEV2_H__

#include <crypto/aead.h>
#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <crypto/engine.h>
#include <crypto/hash.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>

#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dmapool.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/scatterlist.h>

#define DTHE_REG_SIZE		4
#define DTHE_DMA_TIMEOUT_MS	2000
/*
 * Size of largest possible key (of all algorithms) to be stored in dthe_tfm_ctx
 * This is currently the keysize of XTS-AES-256 which is 512 bits (64 bytes)
 */
#define DTHE_MAX_KEYSIZE	(AES_MAX_KEY_SIZE * 2)

enum dthe_aes_mode {
	DTHE_AES_ECB = 0,
	DTHE_AES_CBC,
	DTHE_AES_XTS,
};

/* Driver specific struct definitions */

/**
 * struct dthe_data - DTHE_V2 driver instance data
 * @dev: Device pointer
 * @regs: Base address of the register space
 * @list: list node for dev
 * @engine: Crypto engine instance
 * @dma_aes_rx: AES Rx DMA Channel
 * @dma_aes_tx: AES Tx DMA Channel
 * @dma_sha_tx: SHA Tx DMA Channel
 */
struct dthe_data {
	struct device *dev;
	void __iomem *regs;
	struct list_head list;
	struct crypto_engine *engine;

	struct dma_chan *dma_aes_rx;
	struct dma_chan *dma_aes_tx;

	struct dma_chan *dma_sha_tx;
};

/**
 * struct dthe_list - device data list head
 * @dev_list: linked list head
 * @lock: Spinlock protecting accesses to the list
 */
struct dthe_list {
	struct list_head dev_list;
	spinlock_t lock;
};

/**
 * struct dthe_tfm_ctx - Transform ctx struct containing ctx for all sub-components of DTHE V2
 * @dev_data: Device data struct pointer
 * @keylen: AES key length
 * @key: AES key
 * @aes_mode: AES mode
 * @skcipher_fb: Fallback crypto skcipher handle for AES-XTS mode
 */
struct dthe_tfm_ctx {
	struct dthe_data *dev_data;
	unsigned int keylen;
	u32 key[DTHE_MAX_KEYSIZE / sizeof(u32)];
	enum dthe_aes_mode aes_mode;
	struct crypto_sync_skcipher *skcipher_fb;
};

/**
 * struct dthe_aes_req_ctx - AES engine req ctx struct
 * @enc: flag indicating encryption or decryption operation
 * @aes_compl: Completion variable for use in manual completion in case of DMA callback failure
 */
struct dthe_aes_req_ctx {
	int enc;
	struct completion aes_compl;
};

/* Struct definitions end */

struct dthe_data *dthe_get_dev(struct dthe_tfm_ctx *ctx);

int dthe_register_aes_algs(void);
void dthe_unregister_aes_algs(void);

#endif
