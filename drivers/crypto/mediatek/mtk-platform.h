/*
 * Driver for EIP97 cryptographic accelerator.
 *
 * Copyright (c) 2016 Ryder Lee <ryder.lee@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __MTK_PLATFORM_H_
#define __MTK_PLATFORM_H_

#include <crypto/algapi.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/hash.h>
#include <crypto/scatterwalk.h>
#include <crypto/skcipher.h>
#include <linux/crypto.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/scatterlist.h>
#include "mtk-regs.h"

#define MTK_RDR_PROC_THRESH	BIT(0)
#define MTK_RDR_PROC_MODE	BIT(23)
#define MTK_CNT_RST		BIT(31)
#define MTK_IRQ_RDR0		BIT(1)
#define MTK_IRQ_RDR1		BIT(3)
#define MTK_IRQ_RDR2		BIT(5)
#define MTK_IRQ_RDR3		BIT(7)

#define SIZE_IN_WORDS(x)	((x) >> 2)

/**
 * Ring 0/1 are used by AES encrypt and decrypt.
 * Ring 2/3 are used by SHA.
 */
enum {
	RING0 = 0,
	RING1,
	RING2,
	RING3,
	RING_MAX,
};

#define MTK_REC_NUM		(RING_MAX / 2)
#define MTK_IRQ_NUM		5

/**
 * struct mtk_desc - DMA descriptor
 * @hdr:	the descriptor control header
 * @buf:	DMA address of input buffer segment
 * @ct:		DMA address of command token that control operation flow
 * @ct_hdr:	the command token control header
 * @tag:	the user-defined field
 * @tfm:	DMA address of transform state
 * @bound:	align descriptors offset boundary
 *
 * Structure passed to the crypto engine to describe where source
 * data needs to be fetched and how it needs to be processed.
 */
struct mtk_desc {
	__le32 hdr;
	__le32 buf;
	__le32 ct;
	__le32 ct_hdr;
	__le32 tag;
	__le32 tfm;
	__le32 bound[2];
};

#define MTK_DESC_NUM		512
#define MTK_DESC_OFF		SIZE_IN_WORDS(sizeof(struct mtk_desc))
#define MTK_DESC_SZ		(MTK_DESC_OFF - 2)
#define MTK_DESC_RING_SZ	((sizeof(struct mtk_desc) * MTK_DESC_NUM))
#define MTK_DESC_CNT(x)		((MTK_DESC_OFF * (x)) << 2)
#define MTK_DESC_LAST		cpu_to_le32(BIT(22))
#define MTK_DESC_FIRST		cpu_to_le32(BIT(23))
#define MTK_DESC_BUF_LEN(x)	cpu_to_le32(x)
#define MTK_DESC_CT_LEN(x)	cpu_to_le32((x) << 24)

/**
 * struct mtk_ring - Descriptor ring
 * @cmd_base:	pointer to command descriptor ring base
 * @cmd_dma:	DMA address of command descriptor ring
 * @cmd_pos:	current position in the command descriptor ring
 * @res_base:	pointer to result descriptor ring base
 * @res_dma:	DMA address of result descriptor ring
 * @res_pos:	current position in the result descriptor ring
 *
 * A descriptor ring is a circular buffer that is used to manage
 * one or more descriptors. There are two type of descriptor rings;
 * the command descriptor ring and result descriptor ring.
 */
struct mtk_ring {
	struct mtk_desc *cmd_base;
	dma_addr_t cmd_dma;
	u32 cmd_pos;
	struct mtk_desc *res_base;
	dma_addr_t res_dma;
	u32 res_pos;
};

/**
 * struct mtk_aes_dma - Structure that holds sg list info
 * @sg:		pointer to scatter-gather list
 * @nents:	number of entries in the sg list
 * @remainder:	remainder of sg list
 * @sg_len:	number of entries in the sg mapped list
 */
struct mtk_aes_dma {
	struct scatterlist *sg;
	int nents;
	u32 remainder;
	u32 sg_len;
};

struct mtk_aes_base_ctx;
struct mtk_aes_rec;
struct mtk_cryp;

typedef int (*mtk_aes_fn)(struct mtk_cryp *cryp, struct mtk_aes_rec *aes);

/**
 * struct mtk_aes_rec - AES operation record
 * @queue:	crypto request queue
 * @areq:	pointer to async request
 * @task:	the tasklet is use in AES interrupt
 * @ctx:	pointer to current context
 * @src:	the structure that holds source sg list info
 * @dst:	the structure that holds destination sg list info
 * @aligned_sg:	the scatter list is use to alignment
 * @real_dst:	pointer to the destination sg list
 * @resume:	pointer to resume function
 * @total:	request buffer length
 * @buf:	pointer to page buffer
 * @id:		record identification
 * @flags:	it's describing AES operation state
 * @lock:	the async queue lock
 *
 * Structure used to record AES execution state.
 */
struct mtk_aes_rec {
	struct crypto_queue queue;
	struct crypto_async_request *areq;
	struct tasklet_struct task;
	struct mtk_aes_base_ctx *ctx;
	struct mtk_aes_dma src;
	struct mtk_aes_dma dst;

	struct scatterlist aligned_sg;
	struct scatterlist *real_dst;

	mtk_aes_fn resume;

	size_t total;
	void *buf;

	u8 id;
	unsigned long flags;
	/* queue lock */
	spinlock_t lock;
};

/**
 * struct mtk_sha_rec - SHA operation record
 * @queue:	crypto request queue
 * @req:	pointer to ahash request
 * @task:	the tasklet is use in SHA interrupt
 * @id:		record identification
 * @flags:	it's describing SHA operation state
 * @lock:	the ablkcipher queue lock
 *
 * Structure used to record SHA execution state.
 */
struct mtk_sha_rec {
	struct crypto_queue queue;
	struct ahash_request *req;
	struct tasklet_struct task;

	u8 id;
	unsigned long flags;
	/* queue lock */
	spinlock_t lock;
};

/**
 * struct mtk_cryp - Cryptographic device
 * @base:	pointer to mapped register I/O base
 * @dev:	pointer to device
 * @clk_ethif:	pointer to ethif clock
 * @clk_cryp:	pointer to crypto clock
 * @irq:	global system and rings IRQ
 * @ring:	pointer to execution state of AES
 * @aes:	pointer to execution state of SHA
 * @sha:	each execution record map to a ring
 * @aes_list:	device list of AES
 * @sha_list:	device list of SHA
 * @tmp:	pointer to temporary buffer for internal use
 * @tmp_dma:	DMA address of temporary buffer
 * @rec:	it's used to select SHA record for tfm
 *
 * Structure storing cryptographic device information.
 */
struct mtk_cryp {
	void __iomem *base;
	struct device *dev;
	struct clk *clk_ethif;
	struct clk *clk_cryp;
	int irq[MTK_IRQ_NUM];

	struct mtk_ring *ring[RING_MAX];
	struct mtk_aes_rec *aes[MTK_REC_NUM];
	struct mtk_sha_rec *sha[MTK_REC_NUM];

	struct list_head aes_list;
	struct list_head sha_list;

	void *tmp;
	dma_addr_t tmp_dma;
	bool rec;
};

int mtk_cipher_alg_register(struct mtk_cryp *cryp);
void mtk_cipher_alg_release(struct mtk_cryp *cryp);
int mtk_hash_alg_register(struct mtk_cryp *cryp);
void mtk_hash_alg_release(struct mtk_cryp *cryp);

#endif /* __MTK_PLATFORM_H_ */
