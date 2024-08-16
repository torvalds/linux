// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Keem Bay OCS HCU Crypto Driver.
 *
 * Copyright (C) 2018-2020 Intel Corporation
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/module.h>

#include <crypto/sha2.h>

#include "ocs-hcu.h"

/* Registers. */
#define OCS_HCU_MODE			0x00
#define OCS_HCU_CHAIN			0x04
#define OCS_HCU_OPERATION		0x08
#define OCS_HCU_KEY_0			0x0C
#define OCS_HCU_ISR			0x50
#define OCS_HCU_IER			0x54
#define OCS_HCU_STATUS			0x58
#define OCS_HCU_MSG_LEN_LO		0x60
#define OCS_HCU_MSG_LEN_HI		0x64
#define OCS_HCU_KEY_BYTE_ORDER_CFG	0x80
#define OCS_HCU_DMA_SRC_ADDR		0x400
#define OCS_HCU_DMA_SRC_SIZE		0x408
#define OCS_HCU_DMA_DST_SIZE		0x40C
#define OCS_HCU_DMA_DMA_MODE		0x410
#define OCS_HCU_DMA_NEXT_SRC_DESCR	0x418
#define OCS_HCU_DMA_MSI_ISR		0x480
#define OCS_HCU_DMA_MSI_IER		0x484
#define OCS_HCU_DMA_MSI_MASK		0x488

/* Register bit definitions. */
#define HCU_MODE_ALGO_SHIFT		16
#define HCU_MODE_HMAC_SHIFT		22

#define HCU_STATUS_BUSY			BIT(0)

#define HCU_BYTE_ORDER_SWAP		BIT(0)

#define HCU_IRQ_HASH_DONE		BIT(2)
#define HCU_IRQ_HASH_ERR_MASK		(BIT(3) | BIT(1) | BIT(0))

#define HCU_DMA_IRQ_SRC_DONE		BIT(0)
#define HCU_DMA_IRQ_SAI_ERR		BIT(2)
#define HCU_DMA_IRQ_BAD_COMP_ERR	BIT(3)
#define HCU_DMA_IRQ_INBUF_RD_ERR	BIT(4)
#define HCU_DMA_IRQ_INBUF_WD_ERR	BIT(5)
#define HCU_DMA_IRQ_OUTBUF_WR_ERR	BIT(6)
#define HCU_DMA_IRQ_OUTBUF_RD_ERR	BIT(7)
#define HCU_DMA_IRQ_CRD_ERR		BIT(8)
#define HCU_DMA_IRQ_ERR_MASK		(HCU_DMA_IRQ_SAI_ERR | \
					 HCU_DMA_IRQ_BAD_COMP_ERR | \
					 HCU_DMA_IRQ_INBUF_RD_ERR | \
					 HCU_DMA_IRQ_INBUF_WD_ERR | \
					 HCU_DMA_IRQ_OUTBUF_WR_ERR | \
					 HCU_DMA_IRQ_OUTBUF_RD_ERR | \
					 HCU_DMA_IRQ_CRD_ERR)

#define HCU_DMA_SNOOP_MASK		(0x7 << 28)
#define HCU_DMA_SRC_LL_EN		BIT(25)
#define HCU_DMA_EN			BIT(31)

#define OCS_HCU_ENDIANNESS_VALUE	0x2A

#define HCU_DMA_MSI_UNMASK		BIT(0)
#define HCU_DMA_MSI_DISABLE		0
#define HCU_IRQ_DISABLE			0

#define OCS_HCU_START			BIT(0)
#define OCS_HCU_TERMINATE		BIT(1)

#define OCS_LL_DMA_FLAG_TERMINATE	BIT(31)

#define OCS_HCU_HW_KEY_LEN_U32		(OCS_HCU_HW_KEY_LEN / sizeof(u32))

#define HCU_DATA_WRITE_ENDIANNESS_OFFSET	26

#define OCS_HCU_NUM_CHAINS_SHA256_224_SM3	(SHA256_DIGEST_SIZE / sizeof(u32))
#define OCS_HCU_NUM_CHAINS_SHA384_512		(SHA512_DIGEST_SIZE / sizeof(u32))

/*
 * While polling on a busy HCU, wait maximum 200us between one check and the
 * other.
 */
#define OCS_HCU_WAIT_BUSY_RETRY_DELAY_US	200
/* Wait on a busy HCU for maximum 1 second. */
#define OCS_HCU_WAIT_BUSY_TIMEOUT_US		1000000

/**
 * struct ocs_hcu_dma_entry - An entry in an OCS DMA linked list.
 * @src_addr:  Source address of the data.
 * @src_len:   Length of data to be fetched.
 * @nxt_desc:  Next descriptor to fetch.
 * @ll_flags:  Flags (Freeze @ terminate) for the DMA engine.
 */
struct ocs_hcu_dma_entry {
	u32 src_addr;
	u32 src_len;
	u32 nxt_desc;
	u32 ll_flags;
};

/**
 * struct ocs_hcu_dma_list - OCS-specific DMA linked list.
 * @head:	The head of the list (points to the array backing the list).
 * @tail:	The current tail of the list; NULL if the list is empty.
 * @dma_addr:	The DMA address of @head (i.e., the DMA address of the backing
 *		array).
 * @max_nents:	Maximum number of entries in the list (i.e., number of elements
 *		in the backing array).
 *
 * The OCS DMA list is an array-backed list of OCS DMA descriptors. The array
 * backing the list is allocated with dma_alloc_coherent() and pointed by
 * @head.
 */
struct ocs_hcu_dma_list {
	struct ocs_hcu_dma_entry	*head;
	struct ocs_hcu_dma_entry	*tail;
	dma_addr_t			dma_addr;
	size_t				max_nents;
};

static inline u32 ocs_hcu_num_chains(enum ocs_hcu_algo algo)
{
	switch (algo) {
	case OCS_HCU_ALGO_SHA224:
	case OCS_HCU_ALGO_SHA256:
	case OCS_HCU_ALGO_SM3:
		return OCS_HCU_NUM_CHAINS_SHA256_224_SM3;
	case OCS_HCU_ALGO_SHA384:
	case OCS_HCU_ALGO_SHA512:
		return OCS_HCU_NUM_CHAINS_SHA384_512;
	default:
		return 0;
	};
}

static inline u32 ocs_hcu_digest_size(enum ocs_hcu_algo algo)
{
	switch (algo) {
	case OCS_HCU_ALGO_SHA224:
		return SHA224_DIGEST_SIZE;
	case OCS_HCU_ALGO_SHA256:
	case OCS_HCU_ALGO_SM3:
		/* SM3 shares the same block size. */
		return SHA256_DIGEST_SIZE;
	case OCS_HCU_ALGO_SHA384:
		return SHA384_DIGEST_SIZE;
	case OCS_HCU_ALGO_SHA512:
		return SHA512_DIGEST_SIZE;
	default:
		return 0;
	}
}

/**
 * ocs_hcu_wait_busy() - Wait for HCU OCS hardware to became usable.
 * @hcu_dev:	OCS HCU device to wait for.
 *
 * Return: 0 if device free, -ETIMEOUT if device busy and internal timeout has
 *	   expired.
 */
static int ocs_hcu_wait_busy(struct ocs_hcu_dev *hcu_dev)
{
	long val;

	return readl_poll_timeout(hcu_dev->io_base + OCS_HCU_STATUS, val,
				  !(val & HCU_STATUS_BUSY),
				  OCS_HCU_WAIT_BUSY_RETRY_DELAY_US,
				  OCS_HCU_WAIT_BUSY_TIMEOUT_US);
}

static void ocs_hcu_done_irq_en(struct ocs_hcu_dev *hcu_dev)
{
	/* Clear any pending interrupts. */
	writel(0xFFFFFFFF, hcu_dev->io_base + OCS_HCU_ISR);
	hcu_dev->irq_err = false;
	/* Enable error and HCU done interrupts. */
	writel(HCU_IRQ_HASH_DONE | HCU_IRQ_HASH_ERR_MASK,
	       hcu_dev->io_base + OCS_HCU_IER);
}

static void ocs_hcu_dma_irq_en(struct ocs_hcu_dev *hcu_dev)
{
	/* Clear any pending interrupts. */
	writel(0xFFFFFFFF, hcu_dev->io_base + OCS_HCU_DMA_MSI_ISR);
	hcu_dev->irq_err = false;
	/* Only operating on DMA source completion and error interrupts. */
	writel(HCU_DMA_IRQ_ERR_MASK | HCU_DMA_IRQ_SRC_DONE,
	       hcu_dev->io_base + OCS_HCU_DMA_MSI_IER);
	/* Unmask */
	writel(HCU_DMA_MSI_UNMASK, hcu_dev->io_base + OCS_HCU_DMA_MSI_MASK);
}

static void ocs_hcu_irq_dis(struct ocs_hcu_dev *hcu_dev)
{
	writel(HCU_IRQ_DISABLE, hcu_dev->io_base + OCS_HCU_IER);
	writel(HCU_DMA_MSI_DISABLE, hcu_dev->io_base + OCS_HCU_DMA_MSI_IER);
}

static int ocs_hcu_wait_and_disable_irq(struct ocs_hcu_dev *hcu_dev)
{
	int rc;

	rc = wait_for_completion_interruptible(&hcu_dev->irq_done);
	if (rc)
		goto exit;

	if (hcu_dev->irq_err) {
		/* Unset flag and return error. */
		hcu_dev->irq_err = false;
		rc = -EIO;
		goto exit;
	}

exit:
	ocs_hcu_irq_dis(hcu_dev);

	return rc;
}

/**
 * ocs_hcu_get_intermediate_data() - Get intermediate data.
 * @hcu_dev:	The target HCU device.
 * @data:	Where to store the intermediate.
 * @algo:	The algorithm being used.
 *
 * This function is used to save the current hashing process state in order to
 * continue it in the future.
 *
 * Note: once all data has been processed, the intermediate data actually
 * contains the hashing result. So this function is also used to retrieve the
 * final result of a hashing process.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int ocs_hcu_get_intermediate_data(struct ocs_hcu_dev *hcu_dev,
					 struct ocs_hcu_idata *data,
					 enum ocs_hcu_algo algo)
{
	const int n = ocs_hcu_num_chains(algo);
	u32 *chain;
	int rc;
	int i;

	/* Data not requested. */
	if (!data)
		return -EINVAL;

	chain = (u32 *)data->digest;

	/* Ensure that the OCS is no longer busy before reading the chains. */
	rc = ocs_hcu_wait_busy(hcu_dev);
	if (rc)
		return rc;

	/*
	 * This loops is safe because data->digest is an array of
	 * SHA512_DIGEST_SIZE bytes and the maximum value returned by
	 * ocs_hcu_num_chains() is OCS_HCU_NUM_CHAINS_SHA384_512 which is equal
	 * to SHA512_DIGEST_SIZE / sizeof(u32).
	 */
	for (i = 0; i < n; i++)
		chain[i] = readl(hcu_dev->io_base + OCS_HCU_CHAIN);

	data->msg_len_lo = readl(hcu_dev->io_base + OCS_HCU_MSG_LEN_LO);
	data->msg_len_hi = readl(hcu_dev->io_base + OCS_HCU_MSG_LEN_HI);

	return 0;
}

/**
 * ocs_hcu_set_intermediate_data() - Set intermediate data.
 * @hcu_dev:	The target HCU device.
 * @data:	The intermediate data to be set.
 * @algo:	The algorithm being used.
 *
 * This function is used to continue a previous hashing process.
 */
static void ocs_hcu_set_intermediate_data(struct ocs_hcu_dev *hcu_dev,
					  const struct ocs_hcu_idata *data,
					  enum ocs_hcu_algo algo)
{
	const int n = ocs_hcu_num_chains(algo);
	u32 *chain = (u32 *)data->digest;
	int i;

	/*
	 * This loops is safe because data->digest is an array of
	 * SHA512_DIGEST_SIZE bytes and the maximum value returned by
	 * ocs_hcu_num_chains() is OCS_HCU_NUM_CHAINS_SHA384_512 which is equal
	 * to SHA512_DIGEST_SIZE / sizeof(u32).
	 */
	for (i = 0; i < n; i++)
		writel(chain[i], hcu_dev->io_base + OCS_HCU_CHAIN);

	writel(data->msg_len_lo, hcu_dev->io_base + OCS_HCU_MSG_LEN_LO);
	writel(data->msg_len_hi, hcu_dev->io_base + OCS_HCU_MSG_LEN_HI);
}

static int ocs_hcu_get_digest(struct ocs_hcu_dev *hcu_dev,
			      enum ocs_hcu_algo algo, u8 *dgst, size_t dgst_len)
{
	u32 *chain;
	int rc;
	int i;

	if (!dgst)
		return -EINVAL;

	/* Length of the output buffer must match the algo digest size. */
	if (dgst_len != ocs_hcu_digest_size(algo))
		return -EINVAL;

	/* Ensure that the OCS is no longer busy before reading the chains. */
	rc = ocs_hcu_wait_busy(hcu_dev);
	if (rc)
		return rc;

	chain = (u32 *)dgst;
	for (i = 0; i < dgst_len / sizeof(u32); i++)
		chain[i] = readl(hcu_dev->io_base + OCS_HCU_CHAIN);

	return 0;
}

/**
 * ocs_hcu_hw_cfg() - Configure the HCU hardware.
 * @hcu_dev:	The HCU device to configure.
 * @algo:	The algorithm to be used by the HCU device.
 * @use_hmac:	Whether or not HW HMAC should be used.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int ocs_hcu_hw_cfg(struct ocs_hcu_dev *hcu_dev, enum ocs_hcu_algo algo,
			  bool use_hmac)
{
	u32 cfg;
	int rc;

	if (algo != OCS_HCU_ALGO_SHA256 && algo != OCS_HCU_ALGO_SHA224 &&
	    algo != OCS_HCU_ALGO_SHA384 && algo != OCS_HCU_ALGO_SHA512 &&
	    algo != OCS_HCU_ALGO_SM3)
		return -EINVAL;

	rc = ocs_hcu_wait_busy(hcu_dev);
	if (rc)
		return rc;

	/* Ensure interrupts are disabled. */
	ocs_hcu_irq_dis(hcu_dev);

	/* Configure endianness, hashing algorithm and HW HMAC (if needed) */
	cfg = OCS_HCU_ENDIANNESS_VALUE << HCU_DATA_WRITE_ENDIANNESS_OFFSET;
	cfg |= algo << HCU_MODE_ALGO_SHIFT;
	if (use_hmac)
		cfg |= BIT(HCU_MODE_HMAC_SHIFT);

	writel(cfg, hcu_dev->io_base + OCS_HCU_MODE);

	return 0;
}

/**
 * ocs_hcu_clear_key() - Clear key stored in OCS HMAC KEY registers.
 * @hcu_dev:	The OCS HCU device whose key registers should be cleared.
 */
static void ocs_hcu_clear_key(struct ocs_hcu_dev *hcu_dev)
{
	int reg_off;

	/* Clear OCS_HCU_KEY_[0..15] */
	for (reg_off = 0; reg_off < OCS_HCU_HW_KEY_LEN; reg_off += sizeof(u32))
		writel(0, hcu_dev->io_base + OCS_HCU_KEY_0 + reg_off);
}

/**
 * ocs_hcu_write_key() - Write key to OCS HMAC KEY registers.
 * @hcu_dev:	The OCS HCU device the key should be written to.
 * @key:	The key to be written.
 * @len:	The size of the key to write. It must be OCS_HCU_HW_KEY_LEN.
 *
 * Return:	0 on success, negative error code otherwise.
 */
static int ocs_hcu_write_key(struct ocs_hcu_dev *hcu_dev, const u8 *key, size_t len)
{
	u32 key_u32[OCS_HCU_HW_KEY_LEN_U32];
	int i;

	if (len > OCS_HCU_HW_KEY_LEN)
		return -EINVAL;

	/* Copy key into temporary u32 array. */
	memcpy(key_u32, key, len);

	/*
	 * Hardware requires all the bytes of the HW Key vector to be
	 * written. So pad with zero until we reach OCS_HCU_HW_KEY_LEN.
	 */
	memzero_explicit((u8 *)key_u32 + len, OCS_HCU_HW_KEY_LEN - len);

	/*
	 * OCS hardware expects the MSB of the key to be written at the highest
	 * address of the HCU Key vector; in other word, the key must be
	 * written in reverse order.
	 *
	 * Therefore, we first enable byte swapping for the HCU key vector;
	 * so that bytes of 32-bit word written to OCS_HCU_KEY_[0..15] will be
	 * swapped:
	 * 3 <---> 0, 2 <---> 1.
	 */
	writel(HCU_BYTE_ORDER_SWAP,
	       hcu_dev->io_base + OCS_HCU_KEY_BYTE_ORDER_CFG);
	/*
	 * And then we write the 32-bit words composing the key starting from
	 * the end of the key.
	 */
	for (i = 0; i < OCS_HCU_HW_KEY_LEN_U32; i++)
		writel(key_u32[OCS_HCU_HW_KEY_LEN_U32 - 1 - i],
		       hcu_dev->io_base + OCS_HCU_KEY_0 + (sizeof(u32) * i));

	memzero_explicit(key_u32, OCS_HCU_HW_KEY_LEN);

	return 0;
}

/**
 * ocs_hcu_ll_dma_start() - Start OCS HCU hashing via DMA
 * @hcu_dev:	The OCS HCU device to use.
 * @dma_list:	The OCS DMA list mapping the data to hash.
 * @finalize:	Whether or not this is the last hashing operation and therefore
 *		the final hash should be compute even if data is not
 *		block-aligned.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int ocs_hcu_ll_dma_start(struct ocs_hcu_dev *hcu_dev,
				const struct ocs_hcu_dma_list *dma_list,
				bool finalize)
{
	u32 cfg = HCU_DMA_SNOOP_MASK | HCU_DMA_SRC_LL_EN | HCU_DMA_EN;
	int rc;

	if (!dma_list)
		return -EINVAL;

	/*
	 * For final requests we use HCU_DONE IRQ to be notified when all input
	 * data has been processed by the HCU; however, we cannot do so for
	 * non-final requests, because we don't get a HCU_DONE IRQ when we
	 * don't terminate the operation.
	 *
	 * Therefore, for non-final requests, we use the DMA IRQ, which
	 * triggers when DMA has finishing feeding all the input data to the
	 * HCU, but the HCU may still be processing it. This is fine, since we
	 * will wait for the HCU processing to be completed when we try to read
	 * intermediate results, in ocs_hcu_get_intermediate_data().
	 */
	if (finalize)
		ocs_hcu_done_irq_en(hcu_dev);
	else
		ocs_hcu_dma_irq_en(hcu_dev);

	reinit_completion(&hcu_dev->irq_done);
	writel(dma_list->dma_addr, hcu_dev->io_base + OCS_HCU_DMA_NEXT_SRC_DESCR);
	writel(0, hcu_dev->io_base + OCS_HCU_DMA_SRC_SIZE);
	writel(0, hcu_dev->io_base + OCS_HCU_DMA_DST_SIZE);

	writel(OCS_HCU_START, hcu_dev->io_base + OCS_HCU_OPERATION);

	writel(cfg, hcu_dev->io_base + OCS_HCU_DMA_DMA_MODE);

	if (finalize)
		writel(OCS_HCU_TERMINATE, hcu_dev->io_base + OCS_HCU_OPERATION);

	rc = ocs_hcu_wait_and_disable_irq(hcu_dev);
	if (rc)
		return rc;

	return 0;
}

struct ocs_hcu_dma_list *ocs_hcu_dma_list_alloc(struct ocs_hcu_dev *hcu_dev,
						int max_nents)
{
	struct ocs_hcu_dma_list *dma_list;

	dma_list = kmalloc(sizeof(*dma_list), GFP_KERNEL);
	if (!dma_list)
		return NULL;

	/* Total size of the DMA list to allocate. */
	dma_list->head = dma_alloc_coherent(hcu_dev->dev,
					    sizeof(*dma_list->head) * max_nents,
					    &dma_list->dma_addr, GFP_KERNEL);
	if (!dma_list->head) {
		kfree(dma_list);
		return NULL;
	}
	dma_list->max_nents = max_nents;
	dma_list->tail = NULL;

	return dma_list;
}

void ocs_hcu_dma_list_free(struct ocs_hcu_dev *hcu_dev,
			   struct ocs_hcu_dma_list *dma_list)
{
	if (!dma_list)
		return;

	dma_free_coherent(hcu_dev->dev,
			  sizeof(*dma_list->head) * dma_list->max_nents,
			  dma_list->head, dma_list->dma_addr);

	kfree(dma_list);
}

/* Add a new DMA entry at the end of the OCS DMA list. */
int ocs_hcu_dma_list_add_tail(struct ocs_hcu_dev *hcu_dev,
			      struct ocs_hcu_dma_list *dma_list,
			      dma_addr_t addr, u32 len)
{
	struct device *dev = hcu_dev->dev;
	struct ocs_hcu_dma_entry *old_tail;
	struct ocs_hcu_dma_entry *new_tail;

	if (!len)
		return 0;

	if (!dma_list)
		return -EINVAL;

	if (addr & ~OCS_HCU_DMA_BIT_MASK) {
		dev_err(dev,
			"Unexpected error: Invalid DMA address for OCS HCU\n");
		return -EINVAL;
	}

	old_tail = dma_list->tail;
	new_tail = old_tail ? old_tail + 1 : dma_list->head;

	/* Check if list is full. */
	if (new_tail - dma_list->head >= dma_list->max_nents)
		return -ENOMEM;

	/*
	 * If there was an old tail (i.e., this is not the first element we are
	 * adding), un-terminate the old tail and make it point to the new one.
	 */
	if (old_tail) {
		old_tail->ll_flags &= ~OCS_LL_DMA_FLAG_TERMINATE;
		/*
		 * The old tail 'nxt_desc' must point to the DMA address of the
		 * new tail.
		 */
		old_tail->nxt_desc = dma_list->dma_addr +
				     sizeof(*dma_list->tail) * (new_tail -
								dma_list->head);
	}

	new_tail->src_addr = (u32)addr;
	new_tail->src_len = (u32)len;
	new_tail->ll_flags = OCS_LL_DMA_FLAG_TERMINATE;
	new_tail->nxt_desc = 0;

	/* Update list tail with new tail. */
	dma_list->tail = new_tail;

	return 0;
}

/**
 * ocs_hcu_hash_init() - Initialize hash operation context.
 * @ctx:	The context to initialize.
 * @algo:	The hashing algorithm to use.
 *
 * Return:	0 on success, negative error code otherwise.
 */
int ocs_hcu_hash_init(struct ocs_hcu_hash_ctx *ctx, enum ocs_hcu_algo algo)
{
	if (!ctx)
		return -EINVAL;

	ctx->algo = algo;
	ctx->idata.msg_len_lo = 0;
	ctx->idata.msg_len_hi = 0;
	/* No need to set idata.digest to 0. */

	return 0;
}

/**
 * ocs_hcu_hash_update() - Perform a hashing iteration.
 * @hcu_dev:	The OCS HCU device to use.
 * @ctx:	The OCS HCU hashing context.
 * @dma_list:	The OCS DMA list mapping the input data to process.
 *
 * Return: 0 on success; negative error code otherwise.
 */
int ocs_hcu_hash_update(struct ocs_hcu_dev *hcu_dev,
			struct ocs_hcu_hash_ctx *ctx,
			const struct ocs_hcu_dma_list *dma_list)
{
	int rc;

	if (!hcu_dev || !ctx)
		return -EINVAL;

	/* Configure the hardware for the current request. */
	rc = ocs_hcu_hw_cfg(hcu_dev, ctx->algo, false);
	if (rc)
		return rc;

	/* If we already processed some data, idata needs to be set. */
	if (ctx->idata.msg_len_lo || ctx->idata.msg_len_hi)
		ocs_hcu_set_intermediate_data(hcu_dev, &ctx->idata, ctx->algo);

	/* Start linked-list DMA hashing. */
	rc = ocs_hcu_ll_dma_start(hcu_dev, dma_list, false);
	if (rc)
		return rc;

	/* Update idata and return. */
	return ocs_hcu_get_intermediate_data(hcu_dev, &ctx->idata, ctx->algo);
}

/**
 * ocs_hcu_hash_finup() - Update and finalize hash computation.
 * @hcu_dev:	The OCS HCU device to use.
 * @ctx:	The OCS HCU hashing context.
 * @dma_list:	The OCS DMA list mapping the input data to process.
 * @dgst:	The buffer where to save the computed digest.
 * @dgst_len:	The length of @dgst.
 *
 * Return: 0 on success; negative error code otherwise.
 */
int ocs_hcu_hash_finup(struct ocs_hcu_dev *hcu_dev,
		       const struct ocs_hcu_hash_ctx *ctx,
		       const struct ocs_hcu_dma_list *dma_list,
		       u8 *dgst, size_t dgst_len)
{
	int rc;

	if (!hcu_dev || !ctx)
		return -EINVAL;

	/* Configure the hardware for the current request. */
	rc = ocs_hcu_hw_cfg(hcu_dev, ctx->algo, false);
	if (rc)
		return rc;

	/* If we already processed some data, idata needs to be set. */
	if (ctx->idata.msg_len_lo || ctx->idata.msg_len_hi)
		ocs_hcu_set_intermediate_data(hcu_dev, &ctx->idata, ctx->algo);

	/* Start linked-list DMA hashing. */
	rc = ocs_hcu_ll_dma_start(hcu_dev, dma_list, true);
	if (rc)
		return rc;

	/* Get digest and return. */
	return ocs_hcu_get_digest(hcu_dev, ctx->algo, dgst, dgst_len);
}

/**
 * ocs_hcu_hash_final() - Finalize hash computation.
 * @hcu_dev:		The OCS HCU device to use.
 * @ctx:		The OCS HCU hashing context.
 * @dgst:		The buffer where to save the computed digest.
 * @dgst_len:		The length of @dgst.
 *
 * Return: 0 on success; negative error code otherwise.
 */
int ocs_hcu_hash_final(struct ocs_hcu_dev *hcu_dev,
		       const struct ocs_hcu_hash_ctx *ctx, u8 *dgst,
		       size_t dgst_len)
{
	int rc;

	if (!hcu_dev || !ctx)
		return -EINVAL;

	/* Configure the hardware for the current request. */
	rc = ocs_hcu_hw_cfg(hcu_dev, ctx->algo, false);
	if (rc)
		return rc;

	/* If we already processed some data, idata needs to be set. */
	if (ctx->idata.msg_len_lo || ctx->idata.msg_len_hi)
		ocs_hcu_set_intermediate_data(hcu_dev, &ctx->idata, ctx->algo);

	/*
	 * Enable HCU interrupts, so that HCU_DONE will be triggered once the
	 * final hash is computed.
	 */
	ocs_hcu_done_irq_en(hcu_dev);
	reinit_completion(&hcu_dev->irq_done);
	writel(OCS_HCU_TERMINATE, hcu_dev->io_base + OCS_HCU_OPERATION);

	rc = ocs_hcu_wait_and_disable_irq(hcu_dev);
	if (rc)
		return rc;

	/* Get digest and return. */
	return ocs_hcu_get_digest(hcu_dev, ctx->algo, dgst, dgst_len);
}

/**
 * ocs_hcu_digest() - Compute hash digest.
 * @hcu_dev:		The OCS HCU device to use.
 * @algo:		The hash algorithm to use.
 * @data:		The input data to process.
 * @data_len:		The length of @data.
 * @dgst:		The buffer where to save the computed digest.
 * @dgst_len:		The length of @dgst.
 *
 * Return: 0 on success; negative error code otherwise.
 */
int ocs_hcu_digest(struct ocs_hcu_dev *hcu_dev, enum ocs_hcu_algo algo,
		   void *data, size_t data_len, u8 *dgst, size_t dgst_len)
{
	struct device *dev = hcu_dev->dev;
	dma_addr_t dma_handle;
	u32 reg;
	int rc;

	/* Configure the hardware for the current request. */
	rc = ocs_hcu_hw_cfg(hcu_dev, algo, false);
	if (rc)
		return rc;

	dma_handle = dma_map_single(dev, data, data_len, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma_handle))
		return -EIO;

	reg = HCU_DMA_SNOOP_MASK | HCU_DMA_EN;

	ocs_hcu_done_irq_en(hcu_dev);

	reinit_completion(&hcu_dev->irq_done);

	writel(dma_handle, hcu_dev->io_base + OCS_HCU_DMA_SRC_ADDR);
	writel(data_len, hcu_dev->io_base + OCS_HCU_DMA_SRC_SIZE);
	writel(OCS_HCU_START, hcu_dev->io_base + OCS_HCU_OPERATION);
	writel(reg, hcu_dev->io_base + OCS_HCU_DMA_DMA_MODE);

	writel(OCS_HCU_TERMINATE, hcu_dev->io_base + OCS_HCU_OPERATION);

	rc = ocs_hcu_wait_and_disable_irq(hcu_dev);
	if (rc)
		return rc;

	dma_unmap_single(dev, dma_handle, data_len, DMA_TO_DEVICE);

	return ocs_hcu_get_digest(hcu_dev, algo, dgst, dgst_len);
}

/**
 * ocs_hcu_hmac() - Compute HMAC.
 * @hcu_dev:		The OCS HCU device to use.
 * @algo:		The hash algorithm to use with HMAC.
 * @key:		The key to use.
 * @dma_list:	The OCS DMA list mapping the input data to process.
 * @key_len:		The length of @key.
 * @dgst:		The buffer where to save the computed HMAC.
 * @dgst_len:		The length of @dgst.
 *
 * Return: 0 on success; negative error code otherwise.
 */
int ocs_hcu_hmac(struct ocs_hcu_dev *hcu_dev, enum ocs_hcu_algo algo,
		 const u8 *key, size_t key_len,
		 const struct ocs_hcu_dma_list *dma_list,
		 u8 *dgst, size_t dgst_len)
{
	int rc;

	/* Ensure 'key' is not NULL. */
	if (!key || key_len == 0)
		return -EINVAL;

	/* Configure the hardware for the current request. */
	rc = ocs_hcu_hw_cfg(hcu_dev, algo, true);
	if (rc)
		return rc;

	rc = ocs_hcu_write_key(hcu_dev, key, key_len);
	if (rc)
		return rc;

	rc = ocs_hcu_ll_dma_start(hcu_dev, dma_list, true);

	/* Clear HW key before processing return code. */
	ocs_hcu_clear_key(hcu_dev);

	if (rc)
		return rc;

	return ocs_hcu_get_digest(hcu_dev, algo, dgst, dgst_len);
}

irqreturn_t ocs_hcu_irq_handler(int irq, void *dev_id)
{
	struct ocs_hcu_dev *hcu_dev = dev_id;
	u32 hcu_irq;
	u32 dma_irq;

	/* Read and clear the HCU interrupt. */
	hcu_irq = readl(hcu_dev->io_base + OCS_HCU_ISR);
	writel(hcu_irq, hcu_dev->io_base + OCS_HCU_ISR);

	/* Read and clear the HCU DMA interrupt. */
	dma_irq = readl(hcu_dev->io_base + OCS_HCU_DMA_MSI_ISR);
	writel(dma_irq, hcu_dev->io_base + OCS_HCU_DMA_MSI_ISR);

	/* Check for errors. */
	if (hcu_irq & HCU_IRQ_HASH_ERR_MASK || dma_irq & HCU_DMA_IRQ_ERR_MASK) {
		hcu_dev->irq_err = true;
		goto complete;
	}

	/* Check for DONE IRQs. */
	if (hcu_irq & HCU_IRQ_HASH_DONE || dma_irq & HCU_DMA_IRQ_SRC_DONE)
		goto complete;

	return IRQ_NONE;

complete:
	complete(&hcu_dev->irq_done);

	return IRQ_HANDLED;
}

MODULE_DESCRIPTION("Intel Keem Bay OCS HCU Crypto Driver");
MODULE_LICENSE("GPL");
