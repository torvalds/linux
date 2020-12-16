// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Keem Bay OCS HCU Crypto Driver.
 *
 * Copyright (C) 2018-2020 Intel Corporation
 */

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_device.h>

#include <crypto/engine.h>
#include <crypto/scatterwalk.h>
#include <crypto/sha2.h>
#include <crypto/sm3.h>
#include <crypto/internal/hash.h>

#include "ocs-hcu.h"

#define DRV_NAME	"keembay-ocs-hcu"

/* Flag marking a final request. */
#define REQ_FINAL			BIT(0)

/**
 * struct ocs_hcu_ctx: OCS HCU Transform context.
 * @engine_ctx:	 Crypto Engine context.
 * @hcu_dev:	 The OCS HCU device used by the transformation.
 * @is_sm3_tfm:  Whether or not this is an SM3 transformation.
 */
struct ocs_hcu_ctx {
	struct crypto_engine_ctx engine_ctx;
	struct ocs_hcu_dev *hcu_dev;
	bool is_sm3_tfm;
};

/**
 * struct ocs_hcu_rctx - Context for the request.
 * @hcu_dev:	    OCS HCU device to be used to service the request.
 * @flags:	    Flags tracking request status.
 * @algo:	    Algorithm to use for the request.
 * @blk_sz:	    Block size of the transformation / request.
 * @dig_sz:	    Digest size of the transformation / request.
 * @dma_list:	    OCS DMA linked list.
 * @hash_ctx:	    OCS HCU hashing context.
 * @buffer:	    Buffer to store partial block of data.
 * @buf_cnt:	    Number of bytes currently stored in the buffer.
 * @buf_dma_addr:   The DMA address of @buffer (when mapped).
 * @buf_dma_count:  The number of bytes in @buffer currently DMA-mapped.
 * @sg:		    Head of the scatterlist entries containing data.
 * @sg_data_total:  Total data in the SG list at any time.
 * @sg_data_offset: Offset into the data of the current individual SG node.
 * @sg_dma_nents:   Number of sg entries mapped in dma_list.
 */
struct ocs_hcu_rctx {
	struct ocs_hcu_dev	*hcu_dev;
	u32			flags;
	enum ocs_hcu_algo	algo;
	size_t			blk_sz;
	size_t			dig_sz;
	struct ocs_hcu_dma_list	*dma_list;
	struct ocs_hcu_hash_ctx	hash_ctx;
	u8			buffer[SHA512_BLOCK_SIZE];
	size_t			buf_cnt;
	dma_addr_t		buf_dma_addr;
	size_t			buf_dma_count;
	struct scatterlist	*sg;
	unsigned int		sg_data_total;
	unsigned int		sg_data_offset;
	unsigned int		sg_dma_nents;
};

/**
 * struct ocs_hcu_drv - Driver data
 * @dev_list:	The list of HCU devices.
 * @lock:	The lock protecting dev_list.
 */
struct ocs_hcu_drv {
	struct list_head dev_list;
	spinlock_t lock; /* Protects dev_list. */
};

static struct ocs_hcu_drv ocs_hcu = {
	.dev_list = LIST_HEAD_INIT(ocs_hcu.dev_list),
	.lock = __SPIN_LOCK_UNLOCKED(ocs_hcu.lock),
};

/*
 * Return the total amount of data in the request; that is: the data in the
 * request buffer + the data in the sg list.
 */
static inline unsigned int kmb_get_total_data(struct ocs_hcu_rctx *rctx)
{
	return rctx->sg_data_total + rctx->buf_cnt;
}

/* Move remaining content of scatter-gather list to context buffer. */
static int flush_sg_to_ocs_buffer(struct ocs_hcu_rctx *rctx)
{
	size_t count;

	if (rctx->sg_data_total > (sizeof(rctx->buffer) - rctx->buf_cnt)) {
		WARN(1, "%s: sg data does not fit in buffer\n", __func__);
		return -EINVAL;
	}

	while (rctx->sg_data_total) {
		if (!rctx->sg) {
			WARN(1, "%s: unexpected NULL sg\n", __func__);
			return -EINVAL;
		}
		/*
		 * If current sg has been fully processed, skip to the next
		 * one.
		 */
		if (rctx->sg_data_offset == rctx->sg->length) {
			rctx->sg = sg_next(rctx->sg);
			rctx->sg_data_offset = 0;
			continue;
		}
		/*
		 * Determine the maximum data available to copy from the node.
		 * Minimum of the length left in the sg node, or the total data
		 * in the request.
		 */
		count = min(rctx->sg->length - rctx->sg_data_offset,
			    rctx->sg_data_total);
		/* Copy from scatter-list entry to context buffer. */
		scatterwalk_map_and_copy(&rctx->buffer[rctx->buf_cnt],
					 rctx->sg, rctx->sg_data_offset,
					 count, 0);

		rctx->sg_data_offset += count;
		rctx->sg_data_total -= count;
		rctx->buf_cnt += count;
	}

	return 0;
}

static struct ocs_hcu_dev *kmb_ocs_hcu_find_dev(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ocs_hcu_ctx *tctx = crypto_ahash_ctx(tfm);

	/* If the HCU device for the request was previously set, return it. */
	if (tctx->hcu_dev)
		return tctx->hcu_dev;

	/*
	 * Otherwise, get the first HCU device available (there should be one
	 * and only one device).
	 */
	spin_lock_bh(&ocs_hcu.lock);
	tctx->hcu_dev = list_first_entry_or_null(&ocs_hcu.dev_list,
						 struct ocs_hcu_dev,
						 list);
	spin_unlock_bh(&ocs_hcu.lock);

	return tctx->hcu_dev;
}

/* Free OCS DMA linked list and DMA-able context buffer. */
static void kmb_ocs_hcu_dma_cleanup(struct ahash_request *req,
				    struct ocs_hcu_rctx *rctx)
{
	struct ocs_hcu_dev *hcu_dev = rctx->hcu_dev;
	struct device *dev = hcu_dev->dev;

	/* Unmap rctx->buffer (if mapped). */
	if (rctx->buf_dma_count) {
		dma_unmap_single(dev, rctx->buf_dma_addr, rctx->buf_dma_count,
				 DMA_TO_DEVICE);
		rctx->buf_dma_count = 0;
	}

	/* Unmap req->src (if mapped). */
	if (rctx->sg_dma_nents) {
		dma_unmap_sg(dev, req->src, rctx->sg_dma_nents, DMA_TO_DEVICE);
		rctx->sg_dma_nents = 0;
	}

	/* Free dma_list (if allocated). */
	if (rctx->dma_list) {
		ocs_hcu_dma_list_free(hcu_dev, rctx->dma_list);
		rctx->dma_list = NULL;
	}
}

/*
 * Prepare for DMA operation:
 * - DMA-map request context buffer (if needed)
 * - DMA-map SG list (only the entries to be processed, see note below)
 * - Allocate OCS HCU DMA linked list (number of elements =  SG entries to
 *   process + context buffer (if not empty)).
 * - Add DMA-mapped request context buffer to OCS HCU DMA list.
 * - Add SG entries to DMA list.
 *
 * Note: if this is a final request, we process all the data in the SG list,
 * otherwise we can only process up to the maximum amount of block-aligned data
 * (the remainder will be put into the context buffer and processed in the next
 * request).
 */
static int kmb_ocs_dma_prepare(struct ahash_request *req)
{
	struct ocs_hcu_rctx *rctx = ahash_request_ctx(req);
	struct device *dev = rctx->hcu_dev->dev;
	unsigned int remainder = 0;
	unsigned int total;
	size_t nents;
	size_t count;
	int rc;
	int i;

	/* This function should be called only when there is data to process. */
	total = kmb_get_total_data(rctx);
	if (!total)
		return -EINVAL;

	/*
	 * If this is not a final DMA (terminated DMA), the data passed to the
	 * HCU must be aligned to the block size; compute the remainder data to
	 * be processed in the next request.
	 */
	if (!(rctx->flags & REQ_FINAL))
		remainder = total % rctx->blk_sz;

	/* Determine the number of scatter gather list entries to process. */
	nents = sg_nents_for_len(req->src, rctx->sg_data_total - remainder);

	/* If there are entries to process, map them. */
	if (nents) {
		rctx->sg_dma_nents = dma_map_sg(dev, req->src, nents,
						DMA_TO_DEVICE);
		if (!rctx->sg_dma_nents) {
			dev_err(dev, "Failed to MAP SG\n");
			rc = -ENOMEM;
			goto cleanup;
		}
		/*
		 * The value returned by dma_map_sg() can be < nents; so update
		 * nents accordingly.
		 */
		nents = rctx->sg_dma_nents;
	}

	/*
	 * If context buffer is not empty, map it and add extra DMA entry for
	 * it.
	 */
	if (rctx->buf_cnt) {
		rctx->buf_dma_addr = dma_map_single(dev, rctx->buffer,
						    rctx->buf_cnt,
						    DMA_TO_DEVICE);
		if (dma_mapping_error(dev, rctx->buf_dma_addr)) {
			dev_err(dev, "Failed to map request context buffer\n");
			rc = -ENOMEM;
			goto cleanup;
		}
		rctx->buf_dma_count = rctx->buf_cnt;
		/* Increase number of dma entries. */
		nents++;
	}

	/* Allocate OCS HCU DMA list. */
	rctx->dma_list = ocs_hcu_dma_list_alloc(rctx->hcu_dev, nents);
	if (!rctx->dma_list) {
		rc = -ENOMEM;
		goto cleanup;
	}

	/* Add request context buffer (if previously DMA-mapped) */
	if (rctx->buf_dma_count) {
		rc = ocs_hcu_dma_list_add_tail(rctx->hcu_dev, rctx->dma_list,
					       rctx->buf_dma_addr,
					       rctx->buf_dma_count);
		if (rc)
			goto cleanup;
	}

	/* Add the SG nodes to be processed to the DMA linked list. */
	for_each_sg(req->src, rctx->sg, rctx->sg_dma_nents, i) {
		/*
		 * The number of bytes to add to the list entry is the minimum
		 * between:
		 * - The DMA length of the SG entry.
		 * - The data left to be processed.
		 */
		count = min(rctx->sg_data_total - remainder,
			    sg_dma_len(rctx->sg) - rctx->sg_data_offset);
		/*
		 * Do not create a zero length DMA descriptor. Check in case of
		 * zero length SG node.
		 */
		if (count == 0)
			continue;
		/* Add sg to HCU DMA list. */
		rc = ocs_hcu_dma_list_add_tail(rctx->hcu_dev,
					       rctx->dma_list,
					       rctx->sg->dma_address,
					       count);
		if (rc)
			goto cleanup;

		/* Update amount of data remaining in SG list. */
		rctx->sg_data_total -= count;

		/*
		 * If  remaining data is equal to remainder (note: 'less than'
		 * case should never happen in practice), we are done: update
		 * offset and exit the loop.
		 */
		if (rctx->sg_data_total <= remainder) {
			WARN_ON(rctx->sg_data_total < remainder);
			rctx->sg_data_offset += count;
			break;
		}

		/*
		 * If we get here is because we need to process the next sg in
		 * the list; set offset within the sg to 0.
		 */
		rctx->sg_data_offset = 0;
	}

	return 0;
cleanup:
	dev_err(dev, "Failed to prepare DMA.\n");
	kmb_ocs_hcu_dma_cleanup(req, rctx);

	return rc;
}

static void kmb_ocs_hcu_secure_cleanup(struct ahash_request *req)
{
	struct ocs_hcu_rctx *rctx = ahash_request_ctx(req);

	/* Clear buffer of any data. */
	memzero_explicit(rctx->buffer, sizeof(rctx->buffer));
}

static int kmb_ocs_hcu_handle_queue(struct ahash_request *req)
{
	struct ocs_hcu_dev *hcu_dev = kmb_ocs_hcu_find_dev(req);

	if (!hcu_dev)
		return -ENOENT;

	return crypto_transfer_hash_request_to_engine(hcu_dev->engine, req);
}

static int kmb_ocs_hcu_do_one_request(struct crypto_engine *engine, void *areq)
{
	struct ahash_request *req = container_of(areq, struct ahash_request,
						 base);
	struct ocs_hcu_dev *hcu_dev = kmb_ocs_hcu_find_dev(req);
	struct ocs_hcu_rctx *rctx = ahash_request_ctx(req);
	int rc;

	if (!hcu_dev) {
		rc = -ENOENT;
		goto error;
	}

	/* Handle update request case. */
	if (!(rctx->flags & REQ_FINAL)) {
		/* Update should always have input data. */
		if (!kmb_get_total_data(rctx))
			return -EINVAL;

		/* Map input data into the HCU DMA linked list. */
		rc = kmb_ocs_dma_prepare(req);
		if (rc)
			goto error;

		/* Do hashing step. */
		rc = ocs_hcu_hash_update(hcu_dev, &rctx->hash_ctx,
					 rctx->dma_list);

		/* Unmap data and free DMA list regardless of return code. */
		kmb_ocs_hcu_dma_cleanup(req, rctx);

		/* Process previous return code. */
		if (rc)
			goto error;

		/*
		 * Reset request buffer count (data in the buffer was just
		 * processed).
		 */
		rctx->buf_cnt = 0;
		/*
		 * Move remaining sg data into the request buffer, so that it
		 * will be processed during the next request.
		 *
		 * NOTE: we have remaining data if kmb_get_total_data() was not
		 * a multiple of block size.
		 */
		rc = flush_sg_to_ocs_buffer(rctx);
		if (rc)
			goto error;

		goto done;
	}

	/* If we get here, this is a final request. */

	/* If there is data to process, use finup. */
	if (kmb_get_total_data(rctx)) {
		/* Map input data into the HCU DMA linked list. */
		rc = kmb_ocs_dma_prepare(req);
		if (rc)
			goto error;

		/* Do hashing step. */
		rc = ocs_hcu_hash_finup(hcu_dev, &rctx->hash_ctx,
					rctx->dma_list,
					req->result, rctx->dig_sz);
		/* Free DMA list regardless of return code. */
		kmb_ocs_hcu_dma_cleanup(req, rctx);

		/* Process previous return code. */
		if (rc)
			goto error;

	} else {  /* Otherwise (if we have no data), use final. */
		rc = ocs_hcu_hash_final(hcu_dev, &rctx->hash_ctx, req->result,
					rctx->dig_sz);
		if (rc)
			goto error;
	}

	/* Perform secure clean-up. */
	kmb_ocs_hcu_secure_cleanup(req);
done:
	crypto_finalize_hash_request(hcu_dev->engine, req, 0);

	return 0;

error:
	kmb_ocs_hcu_secure_cleanup(req);
	return rc;
}

static int kmb_ocs_hcu_init(struct ahash_request *req)
{
	struct ocs_hcu_dev *hcu_dev = kmb_ocs_hcu_find_dev(req);
	struct ocs_hcu_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ocs_hcu_ctx *ctx = crypto_ahash_ctx(tfm);

	if (!hcu_dev)
		return -ENOENT;

	/* Initialize entire request context to zero. */
	memset(rctx, 0, sizeof(*rctx));

	rctx->hcu_dev = hcu_dev;
	rctx->dig_sz = crypto_ahash_digestsize(tfm);

	switch (rctx->dig_sz) {
	case SHA256_DIGEST_SIZE:
		rctx->blk_sz = SHA256_BLOCK_SIZE;
		/*
		 * SHA256 and SM3 have the same digest size: use info from tfm
		 * context to find out which one we should use.
		 */
		rctx->algo = ctx->is_sm3_tfm ? OCS_HCU_ALGO_SM3 :
					       OCS_HCU_ALGO_SHA256;
		break;
	case SHA384_DIGEST_SIZE:
		rctx->blk_sz = SHA384_BLOCK_SIZE;
		rctx->algo = OCS_HCU_ALGO_SHA384;
		break;
	case SHA512_DIGEST_SIZE:
		rctx->blk_sz = SHA512_BLOCK_SIZE;
		rctx->algo = OCS_HCU_ALGO_SHA512;
		break;
	default:
		return -EINVAL;
	}

	/* Initialize intermediate data. */
	ocs_hcu_hash_init(&rctx->hash_ctx, rctx->algo);

	return 0;
}

static int kmb_ocs_hcu_update(struct ahash_request *req)
{
	struct ocs_hcu_rctx *rctx = ahash_request_ctx(req);

	if (!req->nbytes)
		return 0;

	rctx->sg_data_total = req->nbytes;
	rctx->sg_data_offset = 0;
	rctx->sg = req->src;

	/*
	 * If remaining sg_data fits into ctx buffer, just copy it there; we'll
	 * process it at the next update() or final().
	 */
	if (rctx->sg_data_total <= (sizeof(rctx->buffer) - rctx->buf_cnt))
		return flush_sg_to_ocs_buffer(rctx);

	return kmb_ocs_hcu_handle_queue(req);
}

static int kmb_ocs_hcu_final(struct ahash_request *req)
{
	struct ocs_hcu_rctx *rctx = ahash_request_ctx(req);

	rctx->sg_data_total = 0;
	rctx->sg_data_offset = 0;
	rctx->sg = NULL;

	rctx->flags |= REQ_FINAL;

	return kmb_ocs_hcu_handle_queue(req);
}

static int kmb_ocs_hcu_finup(struct ahash_request *req)
{
	struct ocs_hcu_rctx *rctx = ahash_request_ctx(req);

	rctx->sg_data_total = req->nbytes;
	rctx->sg_data_offset = 0;
	rctx->sg = req->src;

	rctx->flags |= REQ_FINAL;

	return kmb_ocs_hcu_handle_queue(req);
}

static int kmb_ocs_hcu_digest(struct ahash_request *req)
{
	int rc = 0;
	struct ocs_hcu_dev *hcu_dev = kmb_ocs_hcu_find_dev(req);

	if (!hcu_dev)
		return -ENOENT;

	rc = kmb_ocs_hcu_init(req);
	if (rc)
		return rc;

	rc = kmb_ocs_hcu_finup(req);

	return rc;
}

static int kmb_ocs_hcu_export(struct ahash_request *req, void *out)
{
	struct ocs_hcu_rctx *rctx = ahash_request_ctx(req);

	/* Intermediate data is always stored and applied per request. */
	memcpy(out, rctx, sizeof(*rctx));

	return 0;
}

static int kmb_ocs_hcu_import(struct ahash_request *req, const void *in)
{
	struct ocs_hcu_rctx *rctx = ahash_request_ctx(req);

	/* Intermediate data is always stored and applied per request. */
	memcpy(rctx, in, sizeof(*rctx));

	return 0;
}

/* Set request size and initialize tfm context. */
static void __cra_init(struct crypto_tfm *tfm, struct ocs_hcu_ctx *ctx)
{
	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct ocs_hcu_rctx));

	/* Init context to 0. */
	memzero_explicit(ctx, sizeof(*ctx));
	/* Set engine ops. */
	ctx->engine_ctx.op.do_one_request = kmb_ocs_hcu_do_one_request;
}

static int kmb_ocs_hcu_sha_cra_init(struct crypto_tfm *tfm)
{
	struct ocs_hcu_ctx *ctx = crypto_tfm_ctx(tfm);

	__cra_init(tfm, ctx);

	return 0;
}

static int kmb_ocs_hcu_sm3_cra_init(struct crypto_tfm *tfm)
{
	struct ocs_hcu_ctx *ctx = crypto_tfm_ctx(tfm);

	__cra_init(tfm, ctx);

	ctx->is_sm3_tfm = true;

	return 0;
}

static struct ahash_alg ocs_hcu_algs[] = {
{
	.init		= kmb_ocs_hcu_init,
	.update		= kmb_ocs_hcu_update,
	.final		= kmb_ocs_hcu_final,
	.finup		= kmb_ocs_hcu_finup,
	.digest		= kmb_ocs_hcu_digest,
	.export		= kmb_ocs_hcu_export,
	.import		= kmb_ocs_hcu_import,
	.halg = {
		.digestsize	= SHA256_DIGEST_SIZE,
		.statesize	= sizeof(struct ocs_hcu_rctx),
		.base	= {
			.cra_name		= "sha256",
			.cra_driver_name	= "sha256-keembay-ocs",
			.cra_priority		= 255,
			.cra_flags		= CRYPTO_ALG_ASYNC,
			.cra_blocksize		= SHA256_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct ocs_hcu_ctx),
			.cra_alignmask		= 0,
			.cra_module		= THIS_MODULE,
			.cra_init		= kmb_ocs_hcu_sha_cra_init,
		}
	}
},
{
	.init		= kmb_ocs_hcu_init,
	.update		= kmb_ocs_hcu_update,
	.final		= kmb_ocs_hcu_final,
	.finup		= kmb_ocs_hcu_finup,
	.digest		= kmb_ocs_hcu_digest,
	.export		= kmb_ocs_hcu_export,
	.import		= kmb_ocs_hcu_import,
	.halg = {
		.digestsize	= SM3_DIGEST_SIZE,
		.statesize	= sizeof(struct ocs_hcu_rctx),
		.base	= {
			.cra_name		= "sm3",
			.cra_driver_name	= "sm3-keembay-ocs",
			.cra_priority		= 255,
			.cra_flags		= CRYPTO_ALG_ASYNC,
			.cra_blocksize		= SM3_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct ocs_hcu_ctx),
			.cra_alignmask		= 0,
			.cra_module		= THIS_MODULE,
			.cra_init		= kmb_ocs_hcu_sm3_cra_init,
		}
	}
},
{
	.init		= kmb_ocs_hcu_init,
	.update		= kmb_ocs_hcu_update,
	.final		= kmb_ocs_hcu_final,
	.finup		= kmb_ocs_hcu_finup,
	.digest		= kmb_ocs_hcu_digest,
	.export		= kmb_ocs_hcu_export,
	.import		= kmb_ocs_hcu_import,
	.halg = {
		.digestsize	= SHA384_DIGEST_SIZE,
		.statesize	= sizeof(struct ocs_hcu_rctx),
		.base	= {
			.cra_name		= "sha384",
			.cra_driver_name	= "sha384-keembay-ocs",
			.cra_priority		= 255,
			.cra_flags		= CRYPTO_ALG_ASYNC,
			.cra_blocksize		= SHA384_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct ocs_hcu_ctx),
			.cra_alignmask		= 0,
			.cra_module		= THIS_MODULE,
			.cra_init		= kmb_ocs_hcu_sha_cra_init,
		}
	}
},
{
	.init		= kmb_ocs_hcu_init,
	.update		= kmb_ocs_hcu_update,
	.final		= kmb_ocs_hcu_final,
	.finup		= kmb_ocs_hcu_finup,
	.digest		= kmb_ocs_hcu_digest,
	.export		= kmb_ocs_hcu_export,
	.import		= kmb_ocs_hcu_import,
	.halg = {
		.digestsize	= SHA512_DIGEST_SIZE,
		.statesize	= sizeof(struct ocs_hcu_rctx),
		.base	= {
			.cra_name		= "sha512",
			.cra_driver_name	= "sha512-keembay-ocs",
			.cra_priority		= 255,
			.cra_flags		= CRYPTO_ALG_ASYNC,
			.cra_blocksize		= SHA512_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct ocs_hcu_ctx),
			.cra_alignmask		= 0,
			.cra_module		= THIS_MODULE,
			.cra_init		= kmb_ocs_hcu_sha_cra_init,
		}
	}
},
};

/* Device tree driver match. */
static const struct of_device_id kmb_ocs_hcu_of_match[] = {
	{
		.compatible = "intel,keembay-ocs-hcu",
	},
	{}
};

static int kmb_ocs_hcu_remove(struct platform_device *pdev)
{
	struct ocs_hcu_dev *hcu_dev;
	int rc;

	hcu_dev = platform_get_drvdata(pdev);
	if (!hcu_dev)
		return -ENODEV;

	crypto_unregister_ahashes(ocs_hcu_algs, ARRAY_SIZE(ocs_hcu_algs));

	rc = crypto_engine_exit(hcu_dev->engine);

	spin_lock_bh(&ocs_hcu.lock);
	list_del(&hcu_dev->list);
	spin_unlock_bh(&ocs_hcu.lock);

	return rc;
}

static int kmb_ocs_hcu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ocs_hcu_dev *hcu_dev;
	struct resource *hcu_mem;
	int rc;

	hcu_dev = devm_kzalloc(dev, sizeof(*hcu_dev), GFP_KERNEL);
	if (!hcu_dev)
		return -ENOMEM;

	hcu_dev->dev = dev;

	platform_set_drvdata(pdev, hcu_dev);
	rc = dma_set_mask_and_coherent(&pdev->dev, OCS_HCU_DMA_BIT_MASK);
	if (rc)
		return rc;

	/* Get the memory address and remap. */
	hcu_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!hcu_mem) {
		dev_err(dev, "Could not retrieve io mem resource.\n");
		return -ENODEV;
	}

	hcu_dev->io_base = devm_ioremap_resource(dev, hcu_mem);
	if (IS_ERR(hcu_dev->io_base)) {
		dev_err(dev, "Could not io-remap mem resource.\n");
		return PTR_ERR(hcu_dev->io_base);
	}

	init_completion(&hcu_dev->irq_done);

	/* Get and request IRQ. */
	hcu_dev->irq = platform_get_irq(pdev, 0);
	if (hcu_dev->irq < 0)
		return hcu_dev->irq;

	rc = devm_request_threaded_irq(&pdev->dev, hcu_dev->irq,
				       ocs_hcu_irq_handler, NULL, 0,
				       "keembay-ocs-hcu", hcu_dev);
	if (rc < 0) {
		dev_err(dev, "Could not request IRQ.\n");
		return rc;
	}

	INIT_LIST_HEAD(&hcu_dev->list);

	spin_lock_bh(&ocs_hcu.lock);
	list_add_tail(&hcu_dev->list, &ocs_hcu.dev_list);
	spin_unlock_bh(&ocs_hcu.lock);

	/* Initialize crypto engine */
	hcu_dev->engine = crypto_engine_alloc_init(dev, 1);
	if (!hcu_dev->engine)
		goto list_del;

	rc = crypto_engine_start(hcu_dev->engine);
	if (rc) {
		dev_err(dev, "Could not start engine.\n");
		goto cleanup;
	}

	/* Security infrastructure guarantees OCS clock is enabled. */

	rc = crypto_register_ahashes(ocs_hcu_algs, ARRAY_SIZE(ocs_hcu_algs));
	if (rc) {
		dev_err(dev, "Could not register algorithms.\n");
		goto cleanup;
	}

	return 0;

cleanup:
	crypto_engine_exit(hcu_dev->engine);
list_del:
	spin_lock_bh(&ocs_hcu.lock);
	list_del(&hcu_dev->list);
	spin_unlock_bh(&ocs_hcu.lock);

	return rc;
}

/* The OCS driver is a platform device. */
static struct platform_driver kmb_ocs_hcu_driver = {
	.probe = kmb_ocs_hcu_probe,
	.remove = kmb_ocs_hcu_remove,
	.driver = {
			.name = DRV_NAME,
			.of_match_table = kmb_ocs_hcu_of_match,
		},
};

module_platform_driver(kmb_ocs_hcu_driver);

MODULE_LICENSE("GPL");
