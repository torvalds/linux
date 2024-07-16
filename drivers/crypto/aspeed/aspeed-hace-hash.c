// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2021 Aspeed Technology Inc.
 */

#include "aspeed-hace.h"

#ifdef CONFIG_CRYPTO_DEV_ASPEED_DEBUG
#define AHASH_DBG(h, fmt, ...)	\
	dev_info((h)->dev, "%s() " fmt, __func__, ##__VA_ARGS__)
#else
#define AHASH_DBG(h, fmt, ...)	\
	dev_dbg((h)->dev, "%s() " fmt, __func__, ##__VA_ARGS__)
#endif

/* Initialization Vectors for SHA-family */
static const __be32 sha1_iv[8] = {
	cpu_to_be32(SHA1_H0), cpu_to_be32(SHA1_H1),
	cpu_to_be32(SHA1_H2), cpu_to_be32(SHA1_H3),
	cpu_to_be32(SHA1_H4), 0, 0, 0
};

static const __be32 sha224_iv[8] = {
	cpu_to_be32(SHA224_H0), cpu_to_be32(SHA224_H1),
	cpu_to_be32(SHA224_H2), cpu_to_be32(SHA224_H3),
	cpu_to_be32(SHA224_H4), cpu_to_be32(SHA224_H5),
	cpu_to_be32(SHA224_H6), cpu_to_be32(SHA224_H7),
};

static const __be32 sha256_iv[8] = {
	cpu_to_be32(SHA256_H0), cpu_to_be32(SHA256_H1),
	cpu_to_be32(SHA256_H2), cpu_to_be32(SHA256_H3),
	cpu_to_be32(SHA256_H4), cpu_to_be32(SHA256_H5),
	cpu_to_be32(SHA256_H6), cpu_to_be32(SHA256_H7),
};

static const __be64 sha384_iv[8] = {
	cpu_to_be64(SHA384_H0), cpu_to_be64(SHA384_H1),
	cpu_to_be64(SHA384_H2), cpu_to_be64(SHA384_H3),
	cpu_to_be64(SHA384_H4), cpu_to_be64(SHA384_H5),
	cpu_to_be64(SHA384_H6), cpu_to_be64(SHA384_H7)
};

static const __be64 sha512_iv[8] = {
	cpu_to_be64(SHA512_H0), cpu_to_be64(SHA512_H1),
	cpu_to_be64(SHA512_H2), cpu_to_be64(SHA512_H3),
	cpu_to_be64(SHA512_H4), cpu_to_be64(SHA512_H5),
	cpu_to_be64(SHA512_H6), cpu_to_be64(SHA512_H7)
};

static const __be32 sha512_224_iv[16] = {
	cpu_to_be32(0xC8373D8CUL), cpu_to_be32(0xA24D5419UL),
	cpu_to_be32(0x6699E173UL), cpu_to_be32(0xD6D4DC89UL),
	cpu_to_be32(0xAEB7FA1DUL), cpu_to_be32(0x829CFF32UL),
	cpu_to_be32(0x14D59D67UL), cpu_to_be32(0xCF9F2F58UL),
	cpu_to_be32(0x692B6D0FUL), cpu_to_be32(0xA84DD47BUL),
	cpu_to_be32(0x736FE377UL), cpu_to_be32(0x4289C404UL),
	cpu_to_be32(0xA8859D3FUL), cpu_to_be32(0xC8361D6AUL),
	cpu_to_be32(0xADE61211UL), cpu_to_be32(0xA192D691UL)
};

static const __be32 sha512_256_iv[16] = {
	cpu_to_be32(0x94213122UL), cpu_to_be32(0x2CF72BFCUL),
	cpu_to_be32(0xA35F559FUL), cpu_to_be32(0xC2644CC8UL),
	cpu_to_be32(0x6BB89323UL), cpu_to_be32(0x51B1536FUL),
	cpu_to_be32(0x19773896UL), cpu_to_be32(0xBDEA4059UL),
	cpu_to_be32(0xE23E2896UL), cpu_to_be32(0xE3FF8EA8UL),
	cpu_to_be32(0x251E5EBEUL), cpu_to_be32(0x92398653UL),
	cpu_to_be32(0xFC99012BUL), cpu_to_be32(0xAAB8852CUL),
	cpu_to_be32(0xDC2DB70EUL), cpu_to_be32(0xA22CC581UL)
};

/* The purpose of this padding is to ensure that the padded message is a
 * multiple of 512 bits (SHA1/SHA224/SHA256) or 1024 bits (SHA384/SHA512).
 * The bit "1" is appended at the end of the message followed by
 * "padlen-1" zero bits. Then a 64 bits block (SHA1/SHA224/SHA256) or
 * 128 bits block (SHA384/SHA512) equals to the message length in bits
 * is appended.
 *
 * For SHA1/SHA224/SHA256, padlen is calculated as followed:
 *  - if message length < 56 bytes then padlen = 56 - message length
 *  - else padlen = 64 + 56 - message length
 *
 * For SHA384/SHA512, padlen is calculated as followed:
 *  - if message length < 112 bytes then padlen = 112 - message length
 *  - else padlen = 128 + 112 - message length
 */
static void aspeed_ahash_fill_padding(struct aspeed_hace_dev *hace_dev,
				      struct aspeed_sham_reqctx *rctx)
{
	unsigned int index, padlen;
	__be64 bits[2];

	AHASH_DBG(hace_dev, "rctx flags:0x%x\n", (u32)rctx->flags);

	switch (rctx->flags & SHA_FLAGS_MASK) {
	case SHA_FLAGS_SHA1:
	case SHA_FLAGS_SHA224:
	case SHA_FLAGS_SHA256:
		bits[0] = cpu_to_be64(rctx->digcnt[0] << 3);
		index = rctx->bufcnt & 0x3f;
		padlen = (index < 56) ? (56 - index) : ((64 + 56) - index);
		*(rctx->buffer + rctx->bufcnt) = 0x80;
		memset(rctx->buffer + rctx->bufcnt + 1, 0, padlen - 1);
		memcpy(rctx->buffer + rctx->bufcnt + padlen, bits, 8);
		rctx->bufcnt += padlen + 8;
		break;
	default:
		bits[1] = cpu_to_be64(rctx->digcnt[0] << 3);
		bits[0] = cpu_to_be64(rctx->digcnt[1] << 3 |
				      rctx->digcnt[0] >> 61);
		index = rctx->bufcnt & 0x7f;
		padlen = (index < 112) ? (112 - index) : ((128 + 112) - index);
		*(rctx->buffer + rctx->bufcnt) = 0x80;
		memset(rctx->buffer + rctx->bufcnt + 1, 0, padlen - 1);
		memcpy(rctx->buffer + rctx->bufcnt + padlen, bits, 16);
		rctx->bufcnt += padlen + 16;
		break;
	}
}

/*
 * Prepare DMA buffer before hardware engine
 * processing.
 */
static int aspeed_ahash_dma_prepare(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_hash *hash_engine = &hace_dev->hash_engine;
	struct ahash_request *req = hash_engine->req;
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);
	int length, remain;

	length = rctx->total + rctx->bufcnt;
	remain = length % rctx->block_size;

	AHASH_DBG(hace_dev, "length:0x%x, remain:0x%x\n", length, remain);

	if (rctx->bufcnt)
		memcpy(hash_engine->ahash_src_addr, rctx->buffer, rctx->bufcnt);

	if (rctx->total + rctx->bufcnt < ASPEED_CRYPTO_SRC_DMA_BUF_LEN) {
		scatterwalk_map_and_copy(hash_engine->ahash_src_addr +
					 rctx->bufcnt, rctx->src_sg,
					 rctx->offset, rctx->total - remain, 0);
		rctx->offset += rctx->total - remain;

	} else {
		dev_warn(hace_dev->dev, "Hash data length is too large\n");
		return -EINVAL;
	}

	scatterwalk_map_and_copy(rctx->buffer, rctx->src_sg,
				 rctx->offset, remain, 0);

	rctx->bufcnt = remain;
	rctx->digest_dma_addr = dma_map_single(hace_dev->dev, rctx->digest,
					       SHA512_DIGEST_SIZE,
					       DMA_BIDIRECTIONAL);
	if (dma_mapping_error(hace_dev->dev, rctx->digest_dma_addr)) {
		dev_warn(hace_dev->dev, "dma_map() rctx digest error\n");
		return -ENOMEM;
	}

	hash_engine->src_length = length - remain;
	hash_engine->src_dma = hash_engine->ahash_src_dma_addr;
	hash_engine->digest_dma = rctx->digest_dma_addr;

	return 0;
}

/*
 * Prepare DMA buffer as SG list buffer before
 * hardware engine processing.
 */
static int aspeed_ahash_dma_prepare_sg(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_hash *hash_engine = &hace_dev->hash_engine;
	struct ahash_request *req = hash_engine->req;
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);
	struct aspeed_sg_list *src_list;
	struct scatterlist *s;
	int length, remain, sg_len, i;
	int rc = 0;

	remain = (rctx->total + rctx->bufcnt) % rctx->block_size;
	length = rctx->total + rctx->bufcnt - remain;

	AHASH_DBG(hace_dev, "%s:0x%x, %s:%zu, %s:0x%x, %s:0x%x\n",
		  "rctx total", rctx->total, "bufcnt", rctx->bufcnt,
		  "length", length, "remain", remain);

	sg_len = dma_map_sg(hace_dev->dev, rctx->src_sg, rctx->src_nents,
			    DMA_TO_DEVICE);
	if (!sg_len) {
		dev_warn(hace_dev->dev, "dma_map_sg() src error\n");
		rc = -ENOMEM;
		goto end;
	}

	src_list = (struct aspeed_sg_list *)hash_engine->ahash_src_addr;
	rctx->digest_dma_addr = dma_map_single(hace_dev->dev, rctx->digest,
					       SHA512_DIGEST_SIZE,
					       DMA_BIDIRECTIONAL);
	if (dma_mapping_error(hace_dev->dev, rctx->digest_dma_addr)) {
		dev_warn(hace_dev->dev, "dma_map() rctx digest error\n");
		rc = -ENOMEM;
		goto free_src_sg;
	}

	if (rctx->bufcnt != 0) {
		u32 phy_addr;
		u32 len;

		rctx->buffer_dma_addr = dma_map_single(hace_dev->dev,
						       rctx->buffer,
						       rctx->block_size * 2,
						       DMA_TO_DEVICE);
		if (dma_mapping_error(hace_dev->dev, rctx->buffer_dma_addr)) {
			dev_warn(hace_dev->dev, "dma_map() rctx buffer error\n");
			rc = -ENOMEM;
			goto free_rctx_digest;
		}

		phy_addr = rctx->buffer_dma_addr;
		len = rctx->bufcnt;
		length -= len;

		/* Last sg list */
		if (length == 0)
			len |= HASH_SG_LAST_LIST;

		src_list[0].phy_addr = cpu_to_le32(phy_addr);
		src_list[0].len = cpu_to_le32(len);
		src_list++;
	}

	if (length != 0) {
		for_each_sg(rctx->src_sg, s, sg_len, i) {
			u32 phy_addr = sg_dma_address(s);
			u32 len = sg_dma_len(s);

			if (length > len)
				length -= len;
			else {
				/* Last sg list */
				len = length;
				len |= HASH_SG_LAST_LIST;
				length = 0;
			}

			src_list[i].phy_addr = cpu_to_le32(phy_addr);
			src_list[i].len = cpu_to_le32(len);
		}
	}

	if (length != 0) {
		rc = -EINVAL;
		goto free_rctx_buffer;
	}

	rctx->offset = rctx->total - remain;
	hash_engine->src_length = rctx->total + rctx->bufcnt - remain;
	hash_engine->src_dma = hash_engine->ahash_src_dma_addr;
	hash_engine->digest_dma = rctx->digest_dma_addr;

	return 0;

free_rctx_buffer:
	if (rctx->bufcnt != 0)
		dma_unmap_single(hace_dev->dev, rctx->buffer_dma_addr,
				 rctx->block_size * 2, DMA_TO_DEVICE);
free_rctx_digest:
	dma_unmap_single(hace_dev->dev, rctx->digest_dma_addr,
			 SHA512_DIGEST_SIZE, DMA_BIDIRECTIONAL);
free_src_sg:
	dma_unmap_sg(hace_dev->dev, rctx->src_sg, rctx->src_nents,
		     DMA_TO_DEVICE);
end:
	return rc;
}

static int aspeed_ahash_complete(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_hash *hash_engine = &hace_dev->hash_engine;
	struct ahash_request *req = hash_engine->req;

	AHASH_DBG(hace_dev, "\n");

	hash_engine->flags &= ~CRYPTO_FLAGS_BUSY;

	crypto_finalize_hash_request(hace_dev->crypt_engine_hash, req, 0);

	return 0;
}

/*
 * Copy digest to the corresponding request result.
 * This function will be called at final() stage.
 */
static int aspeed_ahash_transfer(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_hash *hash_engine = &hace_dev->hash_engine;
	struct ahash_request *req = hash_engine->req;
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);

	AHASH_DBG(hace_dev, "\n");

	dma_unmap_single(hace_dev->dev, rctx->digest_dma_addr,
			 SHA512_DIGEST_SIZE, DMA_BIDIRECTIONAL);

	dma_unmap_single(hace_dev->dev, rctx->buffer_dma_addr,
			 rctx->block_size * 2, DMA_TO_DEVICE);

	memcpy(req->result, rctx->digest, rctx->digsize);

	return aspeed_ahash_complete(hace_dev);
}

/*
 * Trigger hardware engines to do the math.
 */
static int aspeed_hace_ahash_trigger(struct aspeed_hace_dev *hace_dev,
				     aspeed_hace_fn_t resume)
{
	struct aspeed_engine_hash *hash_engine = &hace_dev->hash_engine;
	struct ahash_request *req = hash_engine->req;
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);

	AHASH_DBG(hace_dev, "src_dma:%pad, digest_dma:%pad, length:%zu\n",
		  &hash_engine->src_dma, &hash_engine->digest_dma,
		  hash_engine->src_length);

	rctx->cmd |= HASH_CMD_INT_ENABLE;
	hash_engine->resume = resume;

	ast_hace_write(hace_dev, hash_engine->src_dma, ASPEED_HACE_HASH_SRC);
	ast_hace_write(hace_dev, hash_engine->digest_dma,
		       ASPEED_HACE_HASH_DIGEST_BUFF);
	ast_hace_write(hace_dev, hash_engine->digest_dma,
		       ASPEED_HACE_HASH_KEY_BUFF);
	ast_hace_write(hace_dev, hash_engine->src_length,
		       ASPEED_HACE_HASH_DATA_LEN);

	/* Memory barrier to ensure all data setup before engine starts */
	mb();

	ast_hace_write(hace_dev, rctx->cmd, ASPEED_HACE_HASH_CMD);

	return -EINPROGRESS;
}

/*
 * HMAC resume aims to do the second pass produces
 * the final HMAC code derived from the inner hash
 * result and the outer key.
 */
static int aspeed_ahash_hmac_resume(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_hash *hash_engine = &hace_dev->hash_engine;
	struct ahash_request *req = hash_engine->req;
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct aspeed_sham_ctx *tctx = crypto_ahash_ctx(tfm);
	struct aspeed_sha_hmac_ctx *bctx = tctx->base;
	int rc = 0;

	AHASH_DBG(hace_dev, "\n");

	dma_unmap_single(hace_dev->dev, rctx->digest_dma_addr,
			 SHA512_DIGEST_SIZE, DMA_BIDIRECTIONAL);

	dma_unmap_single(hace_dev->dev, rctx->buffer_dma_addr,
			 rctx->block_size * 2, DMA_TO_DEVICE);

	/* o key pad + hash sum 1 */
	memcpy(rctx->buffer, bctx->opad, rctx->block_size);
	memcpy(rctx->buffer + rctx->block_size, rctx->digest, rctx->digsize);

	rctx->bufcnt = rctx->block_size + rctx->digsize;
	rctx->digcnt[0] = rctx->block_size + rctx->digsize;

	aspeed_ahash_fill_padding(hace_dev, rctx);
	memcpy(rctx->digest, rctx->sha_iv, rctx->ivsize);

	rctx->digest_dma_addr = dma_map_single(hace_dev->dev, rctx->digest,
					       SHA512_DIGEST_SIZE,
					       DMA_BIDIRECTIONAL);
	if (dma_mapping_error(hace_dev->dev, rctx->digest_dma_addr)) {
		dev_warn(hace_dev->dev, "dma_map() rctx digest error\n");
		rc = -ENOMEM;
		goto end;
	}

	rctx->buffer_dma_addr = dma_map_single(hace_dev->dev, rctx->buffer,
					       rctx->block_size * 2,
					       DMA_TO_DEVICE);
	if (dma_mapping_error(hace_dev->dev, rctx->buffer_dma_addr)) {
		dev_warn(hace_dev->dev, "dma_map() rctx buffer error\n");
		rc = -ENOMEM;
		goto free_rctx_digest;
	}

	hash_engine->src_dma = rctx->buffer_dma_addr;
	hash_engine->src_length = rctx->bufcnt;
	hash_engine->digest_dma = rctx->digest_dma_addr;

	return aspeed_hace_ahash_trigger(hace_dev, aspeed_ahash_transfer);

free_rctx_digest:
	dma_unmap_single(hace_dev->dev, rctx->digest_dma_addr,
			 SHA512_DIGEST_SIZE, DMA_BIDIRECTIONAL);
end:
	return rc;
}

static int aspeed_ahash_req_final(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_hash *hash_engine = &hace_dev->hash_engine;
	struct ahash_request *req = hash_engine->req;
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);
	int rc = 0;

	AHASH_DBG(hace_dev, "\n");

	aspeed_ahash_fill_padding(hace_dev, rctx);

	rctx->digest_dma_addr = dma_map_single(hace_dev->dev,
					       rctx->digest,
					       SHA512_DIGEST_SIZE,
					       DMA_BIDIRECTIONAL);
	if (dma_mapping_error(hace_dev->dev, rctx->digest_dma_addr)) {
		dev_warn(hace_dev->dev, "dma_map() rctx digest error\n");
		rc = -ENOMEM;
		goto end;
	}

	rctx->buffer_dma_addr = dma_map_single(hace_dev->dev,
					       rctx->buffer,
					       rctx->block_size * 2,
					       DMA_TO_DEVICE);
	if (dma_mapping_error(hace_dev->dev, rctx->buffer_dma_addr)) {
		dev_warn(hace_dev->dev, "dma_map() rctx buffer error\n");
		rc = -ENOMEM;
		goto free_rctx_digest;
	}

	hash_engine->src_dma = rctx->buffer_dma_addr;
	hash_engine->src_length = rctx->bufcnt;
	hash_engine->digest_dma = rctx->digest_dma_addr;

	if (rctx->flags & SHA_FLAGS_HMAC)
		return aspeed_hace_ahash_trigger(hace_dev,
						 aspeed_ahash_hmac_resume);

	return aspeed_hace_ahash_trigger(hace_dev, aspeed_ahash_transfer);

free_rctx_digest:
	dma_unmap_single(hace_dev->dev, rctx->digest_dma_addr,
			 SHA512_DIGEST_SIZE, DMA_BIDIRECTIONAL);
end:
	return rc;
}

static int aspeed_ahash_update_resume_sg(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_hash *hash_engine = &hace_dev->hash_engine;
	struct ahash_request *req = hash_engine->req;
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);

	AHASH_DBG(hace_dev, "\n");

	dma_unmap_sg(hace_dev->dev, rctx->src_sg, rctx->src_nents,
		     DMA_TO_DEVICE);

	if (rctx->bufcnt != 0)
		dma_unmap_single(hace_dev->dev, rctx->buffer_dma_addr,
				 rctx->block_size * 2,
				 DMA_TO_DEVICE);

	dma_unmap_single(hace_dev->dev, rctx->digest_dma_addr,
			 SHA512_DIGEST_SIZE, DMA_BIDIRECTIONAL);

	scatterwalk_map_and_copy(rctx->buffer, rctx->src_sg, rctx->offset,
				 rctx->total - rctx->offset, 0);

	rctx->bufcnt = rctx->total - rctx->offset;
	rctx->cmd &= ~HASH_CMD_HASH_SRC_SG_CTRL;

	if (rctx->flags & SHA_FLAGS_FINUP)
		return aspeed_ahash_req_final(hace_dev);

	return aspeed_ahash_complete(hace_dev);
}

static int aspeed_ahash_update_resume(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_hash *hash_engine = &hace_dev->hash_engine;
	struct ahash_request *req = hash_engine->req;
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);

	AHASH_DBG(hace_dev, "\n");

	dma_unmap_single(hace_dev->dev, rctx->digest_dma_addr,
			 SHA512_DIGEST_SIZE, DMA_BIDIRECTIONAL);

	if (rctx->flags & SHA_FLAGS_FINUP)
		return aspeed_ahash_req_final(hace_dev);

	return aspeed_ahash_complete(hace_dev);
}

static int aspeed_ahash_req_update(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_hash *hash_engine = &hace_dev->hash_engine;
	struct ahash_request *req = hash_engine->req;
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);
	aspeed_hace_fn_t resume;
	int ret;

	AHASH_DBG(hace_dev, "\n");

	if (hace_dev->version == AST2600_VERSION) {
		rctx->cmd |= HASH_CMD_HASH_SRC_SG_CTRL;
		resume = aspeed_ahash_update_resume_sg;

	} else {
		resume = aspeed_ahash_update_resume;
	}

	ret = hash_engine->dma_prepare(hace_dev);
	if (ret)
		return ret;

	return aspeed_hace_ahash_trigger(hace_dev, resume);
}

static int aspeed_hace_hash_handle_queue(struct aspeed_hace_dev *hace_dev,
				  struct ahash_request *req)
{
	return crypto_transfer_hash_request_to_engine(
			hace_dev->crypt_engine_hash, req);
}

static int aspeed_ahash_do_request(struct crypto_engine *engine, void *areq)
{
	struct ahash_request *req = ahash_request_cast(areq);
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct aspeed_sham_ctx *tctx = crypto_ahash_ctx(tfm);
	struct aspeed_hace_dev *hace_dev = tctx->hace_dev;
	struct aspeed_engine_hash *hash_engine;
	int ret = 0;

	hash_engine = &hace_dev->hash_engine;
	hash_engine->flags |= CRYPTO_FLAGS_BUSY;

	if (rctx->op == SHA_OP_UPDATE)
		ret = aspeed_ahash_req_update(hace_dev);
	else if (rctx->op == SHA_OP_FINAL)
		ret = aspeed_ahash_req_final(hace_dev);

	if (ret != -EINPROGRESS)
		return ret;

	return 0;
}

static int aspeed_ahash_prepare_request(struct crypto_engine *engine,
					void *areq)
{
	struct ahash_request *req = ahash_request_cast(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct aspeed_sham_ctx *tctx = crypto_ahash_ctx(tfm);
	struct aspeed_hace_dev *hace_dev = tctx->hace_dev;
	struct aspeed_engine_hash *hash_engine;

	hash_engine = &hace_dev->hash_engine;
	hash_engine->req = req;

	if (hace_dev->version == AST2600_VERSION)
		hash_engine->dma_prepare = aspeed_ahash_dma_prepare_sg;
	else
		hash_engine->dma_prepare = aspeed_ahash_dma_prepare;

	return 0;
}

static int aspeed_sham_update(struct ahash_request *req)
{
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct aspeed_sham_ctx *tctx = crypto_ahash_ctx(tfm);
	struct aspeed_hace_dev *hace_dev = tctx->hace_dev;

	AHASH_DBG(hace_dev, "req->nbytes: %d\n", req->nbytes);

	rctx->total = req->nbytes;
	rctx->src_sg = req->src;
	rctx->offset = 0;
	rctx->src_nents = sg_nents(req->src);
	rctx->op = SHA_OP_UPDATE;

	rctx->digcnt[0] += rctx->total;
	if (rctx->digcnt[0] < rctx->total)
		rctx->digcnt[1]++;

	if (rctx->bufcnt + rctx->total < rctx->block_size) {
		scatterwalk_map_and_copy(rctx->buffer + rctx->bufcnt,
					 rctx->src_sg, rctx->offset,
					 rctx->total, 0);
		rctx->bufcnt += rctx->total;

		return 0;
	}

	return aspeed_hace_hash_handle_queue(hace_dev, req);
}

static int aspeed_sham_shash_digest(struct crypto_shash *tfm, u32 flags,
				    const u8 *data, unsigned int len, u8 *out)
{
	SHASH_DESC_ON_STACK(shash, tfm);

	shash->tfm = tfm;

	return crypto_shash_digest(shash, data, len, out);
}

static int aspeed_sham_final(struct ahash_request *req)
{
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct aspeed_sham_ctx *tctx = crypto_ahash_ctx(tfm);
	struct aspeed_hace_dev *hace_dev = tctx->hace_dev;

	AHASH_DBG(hace_dev, "req->nbytes:%d, rctx->total:%d\n",
		  req->nbytes, rctx->total);
	rctx->op = SHA_OP_FINAL;

	return aspeed_hace_hash_handle_queue(hace_dev, req);
}

static int aspeed_sham_finup(struct ahash_request *req)
{
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct aspeed_sham_ctx *tctx = crypto_ahash_ctx(tfm);
	struct aspeed_hace_dev *hace_dev = tctx->hace_dev;
	int rc1, rc2;

	AHASH_DBG(hace_dev, "req->nbytes: %d\n", req->nbytes);

	rctx->flags |= SHA_FLAGS_FINUP;

	rc1 = aspeed_sham_update(req);
	if (rc1 == -EINPROGRESS || rc1 == -EBUSY)
		return rc1;

	/*
	 * final() has to be always called to cleanup resources
	 * even if update() failed, except EINPROGRESS
	 */
	rc2 = aspeed_sham_final(req);

	return rc1 ? : rc2;
}

static int aspeed_sham_init(struct ahash_request *req)
{
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct aspeed_sham_ctx *tctx = crypto_ahash_ctx(tfm);
	struct aspeed_hace_dev *hace_dev = tctx->hace_dev;
	struct aspeed_sha_hmac_ctx *bctx = tctx->base;

	AHASH_DBG(hace_dev, "%s: digest size:%d\n",
		  crypto_tfm_alg_name(&tfm->base),
		  crypto_ahash_digestsize(tfm));

	rctx->cmd = HASH_CMD_ACC_MODE;
	rctx->flags = 0;

	switch (crypto_ahash_digestsize(tfm)) {
	case SHA1_DIGEST_SIZE:
		rctx->cmd |= HASH_CMD_SHA1 | HASH_CMD_SHA_SWAP;
		rctx->flags |= SHA_FLAGS_SHA1;
		rctx->digsize = SHA1_DIGEST_SIZE;
		rctx->block_size = SHA1_BLOCK_SIZE;
		rctx->sha_iv = sha1_iv;
		rctx->ivsize = 32;
		memcpy(rctx->digest, sha1_iv, rctx->ivsize);
		break;
	case SHA224_DIGEST_SIZE:
		rctx->cmd |= HASH_CMD_SHA224 | HASH_CMD_SHA_SWAP;
		rctx->flags |= SHA_FLAGS_SHA224;
		rctx->digsize = SHA224_DIGEST_SIZE;
		rctx->block_size = SHA224_BLOCK_SIZE;
		rctx->sha_iv = sha224_iv;
		rctx->ivsize = 32;
		memcpy(rctx->digest, sha224_iv, rctx->ivsize);
		break;
	case SHA256_DIGEST_SIZE:
		rctx->cmd |= HASH_CMD_SHA256 | HASH_CMD_SHA_SWAP;
		rctx->flags |= SHA_FLAGS_SHA256;
		rctx->digsize = SHA256_DIGEST_SIZE;
		rctx->block_size = SHA256_BLOCK_SIZE;
		rctx->sha_iv = sha256_iv;
		rctx->ivsize = 32;
		memcpy(rctx->digest, sha256_iv, rctx->ivsize);
		break;
	case SHA384_DIGEST_SIZE:
		rctx->cmd |= HASH_CMD_SHA512_SER | HASH_CMD_SHA384 |
			     HASH_CMD_SHA_SWAP;
		rctx->flags |= SHA_FLAGS_SHA384;
		rctx->digsize = SHA384_DIGEST_SIZE;
		rctx->block_size = SHA384_BLOCK_SIZE;
		rctx->sha_iv = (const __be32 *)sha384_iv;
		rctx->ivsize = 64;
		memcpy(rctx->digest, sha384_iv, rctx->ivsize);
		break;
	case SHA512_DIGEST_SIZE:
		rctx->cmd |= HASH_CMD_SHA512_SER | HASH_CMD_SHA512 |
			     HASH_CMD_SHA_SWAP;
		rctx->flags |= SHA_FLAGS_SHA512;
		rctx->digsize = SHA512_DIGEST_SIZE;
		rctx->block_size = SHA512_BLOCK_SIZE;
		rctx->sha_iv = (const __be32 *)sha512_iv;
		rctx->ivsize = 64;
		memcpy(rctx->digest, sha512_iv, rctx->ivsize);
		break;
	default:
		dev_warn(tctx->hace_dev->dev, "digest size %d not support\n",
			 crypto_ahash_digestsize(tfm));
		return -EINVAL;
	}

	rctx->bufcnt = 0;
	rctx->total = 0;
	rctx->digcnt[0] = 0;
	rctx->digcnt[1] = 0;

	/* HMAC init */
	if (tctx->flags & SHA_FLAGS_HMAC) {
		rctx->digcnt[0] = rctx->block_size;
		rctx->bufcnt = rctx->block_size;
		memcpy(rctx->buffer, bctx->ipad, rctx->block_size);
		rctx->flags |= SHA_FLAGS_HMAC;
	}

	return 0;
}

static int aspeed_sha512s_init(struct ahash_request *req)
{
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct aspeed_sham_ctx *tctx = crypto_ahash_ctx(tfm);
	struct aspeed_hace_dev *hace_dev = tctx->hace_dev;
	struct aspeed_sha_hmac_ctx *bctx = tctx->base;

	AHASH_DBG(hace_dev, "digest size: %d\n", crypto_ahash_digestsize(tfm));

	rctx->cmd = HASH_CMD_ACC_MODE;
	rctx->flags = 0;

	switch (crypto_ahash_digestsize(tfm)) {
	case SHA224_DIGEST_SIZE:
		rctx->cmd |= HASH_CMD_SHA512_SER | HASH_CMD_SHA512_224 |
			     HASH_CMD_SHA_SWAP;
		rctx->flags |= SHA_FLAGS_SHA512_224;
		rctx->digsize = SHA224_DIGEST_SIZE;
		rctx->block_size = SHA512_BLOCK_SIZE;
		rctx->sha_iv = sha512_224_iv;
		rctx->ivsize = 64;
		memcpy(rctx->digest, sha512_224_iv, rctx->ivsize);
		break;
	case SHA256_DIGEST_SIZE:
		rctx->cmd |= HASH_CMD_SHA512_SER | HASH_CMD_SHA512_256 |
			     HASH_CMD_SHA_SWAP;
		rctx->flags |= SHA_FLAGS_SHA512_256;
		rctx->digsize = SHA256_DIGEST_SIZE;
		rctx->block_size = SHA512_BLOCK_SIZE;
		rctx->sha_iv = sha512_256_iv;
		rctx->ivsize = 64;
		memcpy(rctx->digest, sha512_256_iv, rctx->ivsize);
		break;
	default:
		dev_warn(tctx->hace_dev->dev, "digest size %d not support\n",
			 crypto_ahash_digestsize(tfm));
		return -EINVAL;
	}

	rctx->bufcnt = 0;
	rctx->total = 0;
	rctx->digcnt[0] = 0;
	rctx->digcnt[1] = 0;

	/* HMAC init */
	if (tctx->flags & SHA_FLAGS_HMAC) {
		rctx->digcnt[0] = rctx->block_size;
		rctx->bufcnt = rctx->block_size;
		memcpy(rctx->buffer, bctx->ipad, rctx->block_size);
		rctx->flags |= SHA_FLAGS_HMAC;
	}

	return 0;
}

static int aspeed_sham_digest(struct ahash_request *req)
{
	return aspeed_sham_init(req) ? : aspeed_sham_finup(req);
}

static int aspeed_sham_setkey(struct crypto_ahash *tfm, const u8 *key,
			      unsigned int keylen)
{
	struct aspeed_sham_ctx *tctx = crypto_ahash_ctx(tfm);
	struct aspeed_hace_dev *hace_dev = tctx->hace_dev;
	struct aspeed_sha_hmac_ctx *bctx = tctx->base;
	int ds = crypto_shash_digestsize(bctx->shash);
	int bs = crypto_shash_blocksize(bctx->shash);
	int err = 0;
	int i;

	AHASH_DBG(hace_dev, "%s: keylen:%d\n", crypto_tfm_alg_name(&tfm->base),
		  keylen);

	if (keylen > bs) {
		err = aspeed_sham_shash_digest(bctx->shash,
					       crypto_shash_get_flags(bctx->shash),
					       key, keylen, bctx->ipad);
		if (err)
			return err;
		keylen = ds;

	} else {
		memcpy(bctx->ipad, key, keylen);
	}

	memset(bctx->ipad + keylen, 0, bs - keylen);
	memcpy(bctx->opad, bctx->ipad, bs);

	for (i = 0; i < bs; i++) {
		bctx->ipad[i] ^= HMAC_IPAD_VALUE;
		bctx->opad[i] ^= HMAC_OPAD_VALUE;
	}

	return err;
}

static int aspeed_sham_cra_init(struct crypto_tfm *tfm)
{
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->__crt_alg);
	struct aspeed_sham_ctx *tctx = crypto_tfm_ctx(tfm);
	struct aspeed_hace_alg *ast_alg;

	ast_alg = container_of(alg, struct aspeed_hace_alg, alg.ahash);
	tctx->hace_dev = ast_alg->hace_dev;
	tctx->flags = 0;

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct aspeed_sham_reqctx));

	if (ast_alg->alg_base) {
		/* hmac related */
		struct aspeed_sha_hmac_ctx *bctx = tctx->base;

		tctx->flags |= SHA_FLAGS_HMAC;
		bctx->shash = crypto_alloc_shash(ast_alg->alg_base, 0,
						 CRYPTO_ALG_NEED_FALLBACK);
		if (IS_ERR(bctx->shash)) {
			dev_warn(ast_alg->hace_dev->dev,
				 "base driver '%s' could not be loaded.\n",
				 ast_alg->alg_base);
			return PTR_ERR(bctx->shash);
		}
	}

	tctx->enginectx.op.do_one_request = aspeed_ahash_do_request;
	tctx->enginectx.op.prepare_request = aspeed_ahash_prepare_request;
	tctx->enginectx.op.unprepare_request = NULL;

	return 0;
}

static void aspeed_sham_cra_exit(struct crypto_tfm *tfm)
{
	struct aspeed_sham_ctx *tctx = crypto_tfm_ctx(tfm);
	struct aspeed_hace_dev *hace_dev = tctx->hace_dev;

	AHASH_DBG(hace_dev, "%s\n", crypto_tfm_alg_name(tfm));

	if (tctx->flags & SHA_FLAGS_HMAC) {
		struct aspeed_sha_hmac_ctx *bctx = tctx->base;

		crypto_free_shash(bctx->shash);
	}
}

static int aspeed_sham_export(struct ahash_request *req, void *out)
{
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);

	memcpy(out, rctx, sizeof(*rctx));

	return 0;
}

static int aspeed_sham_import(struct ahash_request *req, const void *in)
{
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);

	memcpy(rctx, in, sizeof(*rctx));

	return 0;
}

static struct aspeed_hace_alg aspeed_ahash_algs[] = {
	{
		.alg.ahash = {
			.init	= aspeed_sham_init,
			.update	= aspeed_sham_update,
			.final	= aspeed_sham_final,
			.finup	= aspeed_sham_finup,
			.digest	= aspeed_sham_digest,
			.export	= aspeed_sham_export,
			.import	= aspeed_sham_import,
			.halg = {
				.digestsize = SHA1_DIGEST_SIZE,
				.statesize = sizeof(struct aspeed_sham_reqctx),
				.base = {
					.cra_name		= "sha1",
					.cra_driver_name	= "aspeed-sha1",
					.cra_priority		= 300,
					.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
								  CRYPTO_ALG_ASYNC |
								  CRYPTO_ALG_KERN_DRIVER_ONLY,
					.cra_blocksize		= SHA1_BLOCK_SIZE,
					.cra_ctxsize		= sizeof(struct aspeed_sham_ctx),
					.cra_alignmask		= 0,
					.cra_module		= THIS_MODULE,
					.cra_init		= aspeed_sham_cra_init,
					.cra_exit		= aspeed_sham_cra_exit,
				}
			}
		},
	},
	{
		.alg.ahash = {
			.init	= aspeed_sham_init,
			.update	= aspeed_sham_update,
			.final	= aspeed_sham_final,
			.finup	= aspeed_sham_finup,
			.digest	= aspeed_sham_digest,
			.export	= aspeed_sham_export,
			.import	= aspeed_sham_import,
			.halg = {
				.digestsize = SHA256_DIGEST_SIZE,
				.statesize = sizeof(struct aspeed_sham_reqctx),
				.base = {
					.cra_name		= "sha256",
					.cra_driver_name	= "aspeed-sha256",
					.cra_priority		= 300,
					.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
								  CRYPTO_ALG_ASYNC |
								  CRYPTO_ALG_KERN_DRIVER_ONLY,
					.cra_blocksize		= SHA256_BLOCK_SIZE,
					.cra_ctxsize		= sizeof(struct aspeed_sham_ctx),
					.cra_alignmask		= 0,
					.cra_module		= THIS_MODULE,
					.cra_init		= aspeed_sham_cra_init,
					.cra_exit		= aspeed_sham_cra_exit,
				}
			}
		},
	},
	{
		.alg.ahash = {
			.init	= aspeed_sham_init,
			.update	= aspeed_sham_update,
			.final	= aspeed_sham_final,
			.finup	= aspeed_sham_finup,
			.digest	= aspeed_sham_digest,
			.export	= aspeed_sham_export,
			.import	= aspeed_sham_import,
			.halg = {
				.digestsize = SHA224_DIGEST_SIZE,
				.statesize = sizeof(struct aspeed_sham_reqctx),
				.base = {
					.cra_name		= "sha224",
					.cra_driver_name	= "aspeed-sha224",
					.cra_priority		= 300,
					.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
								  CRYPTO_ALG_ASYNC |
								  CRYPTO_ALG_KERN_DRIVER_ONLY,
					.cra_blocksize		= SHA224_BLOCK_SIZE,
					.cra_ctxsize		= sizeof(struct aspeed_sham_ctx),
					.cra_alignmask		= 0,
					.cra_module		= THIS_MODULE,
					.cra_init		= aspeed_sham_cra_init,
					.cra_exit		= aspeed_sham_cra_exit,
				}
			}
		},
	},
	{
		.alg_base = "sha1",
		.alg.ahash = {
			.init	= aspeed_sham_init,
			.update	= aspeed_sham_update,
			.final	= aspeed_sham_final,
			.finup	= aspeed_sham_finup,
			.digest	= aspeed_sham_digest,
			.setkey	= aspeed_sham_setkey,
			.export	= aspeed_sham_export,
			.import	= aspeed_sham_import,
			.halg = {
				.digestsize = SHA1_DIGEST_SIZE,
				.statesize = sizeof(struct aspeed_sham_reqctx),
				.base = {
					.cra_name		= "hmac(sha1)",
					.cra_driver_name	= "aspeed-hmac-sha1",
					.cra_priority		= 300,
					.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
								  CRYPTO_ALG_ASYNC |
								  CRYPTO_ALG_KERN_DRIVER_ONLY,
					.cra_blocksize		= SHA1_BLOCK_SIZE,
					.cra_ctxsize		= sizeof(struct aspeed_sham_ctx) +
								sizeof(struct aspeed_sha_hmac_ctx),
					.cra_alignmask		= 0,
					.cra_module		= THIS_MODULE,
					.cra_init		= aspeed_sham_cra_init,
					.cra_exit		= aspeed_sham_cra_exit,
				}
			}
		},
	},
	{
		.alg_base = "sha224",
		.alg.ahash = {
			.init	= aspeed_sham_init,
			.update	= aspeed_sham_update,
			.final	= aspeed_sham_final,
			.finup	= aspeed_sham_finup,
			.digest	= aspeed_sham_digest,
			.setkey	= aspeed_sham_setkey,
			.export	= aspeed_sham_export,
			.import	= aspeed_sham_import,
			.halg = {
				.digestsize = SHA224_DIGEST_SIZE,
				.statesize = sizeof(struct aspeed_sham_reqctx),
				.base = {
					.cra_name		= "hmac(sha224)",
					.cra_driver_name	= "aspeed-hmac-sha224",
					.cra_priority		= 300,
					.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
								  CRYPTO_ALG_ASYNC |
								  CRYPTO_ALG_KERN_DRIVER_ONLY,
					.cra_blocksize		= SHA224_BLOCK_SIZE,
					.cra_ctxsize		= sizeof(struct aspeed_sham_ctx) +
								sizeof(struct aspeed_sha_hmac_ctx),
					.cra_alignmask		= 0,
					.cra_module		= THIS_MODULE,
					.cra_init		= aspeed_sham_cra_init,
					.cra_exit		= aspeed_sham_cra_exit,
				}
			}
		},
	},
	{
		.alg_base = "sha256",
		.alg.ahash = {
			.init	= aspeed_sham_init,
			.update	= aspeed_sham_update,
			.final	= aspeed_sham_final,
			.finup	= aspeed_sham_finup,
			.digest	= aspeed_sham_digest,
			.setkey	= aspeed_sham_setkey,
			.export	= aspeed_sham_export,
			.import	= aspeed_sham_import,
			.halg = {
				.digestsize = SHA256_DIGEST_SIZE,
				.statesize = sizeof(struct aspeed_sham_reqctx),
				.base = {
					.cra_name		= "hmac(sha256)",
					.cra_driver_name	= "aspeed-hmac-sha256",
					.cra_priority		= 300,
					.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
								  CRYPTO_ALG_ASYNC |
								  CRYPTO_ALG_KERN_DRIVER_ONLY,
					.cra_blocksize		= SHA256_BLOCK_SIZE,
					.cra_ctxsize		= sizeof(struct aspeed_sham_ctx) +
								sizeof(struct aspeed_sha_hmac_ctx),
					.cra_alignmask		= 0,
					.cra_module		= THIS_MODULE,
					.cra_init		= aspeed_sham_cra_init,
					.cra_exit		= aspeed_sham_cra_exit,
				}
			}
		},
	},
};

static struct aspeed_hace_alg aspeed_ahash_algs_g6[] = {
	{
		.alg.ahash = {
			.init	= aspeed_sham_init,
			.update	= aspeed_sham_update,
			.final	= aspeed_sham_final,
			.finup	= aspeed_sham_finup,
			.digest	= aspeed_sham_digest,
			.export	= aspeed_sham_export,
			.import	= aspeed_sham_import,
			.halg = {
				.digestsize = SHA384_DIGEST_SIZE,
				.statesize = sizeof(struct aspeed_sham_reqctx),
				.base = {
					.cra_name		= "sha384",
					.cra_driver_name	= "aspeed-sha384",
					.cra_priority		= 300,
					.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
								  CRYPTO_ALG_ASYNC |
								  CRYPTO_ALG_KERN_DRIVER_ONLY,
					.cra_blocksize		= SHA384_BLOCK_SIZE,
					.cra_ctxsize		= sizeof(struct aspeed_sham_ctx),
					.cra_alignmask		= 0,
					.cra_module		= THIS_MODULE,
					.cra_init		= aspeed_sham_cra_init,
					.cra_exit		= aspeed_sham_cra_exit,
				}
			}
		},
	},
	{
		.alg.ahash = {
			.init	= aspeed_sham_init,
			.update	= aspeed_sham_update,
			.final	= aspeed_sham_final,
			.finup	= aspeed_sham_finup,
			.digest	= aspeed_sham_digest,
			.export	= aspeed_sham_export,
			.import	= aspeed_sham_import,
			.halg = {
				.digestsize = SHA512_DIGEST_SIZE,
				.statesize = sizeof(struct aspeed_sham_reqctx),
				.base = {
					.cra_name		= "sha512",
					.cra_driver_name	= "aspeed-sha512",
					.cra_priority		= 300,
					.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
								  CRYPTO_ALG_ASYNC |
								  CRYPTO_ALG_KERN_DRIVER_ONLY,
					.cra_blocksize		= SHA512_BLOCK_SIZE,
					.cra_ctxsize		= sizeof(struct aspeed_sham_ctx),
					.cra_alignmask		= 0,
					.cra_module		= THIS_MODULE,
					.cra_init		= aspeed_sham_cra_init,
					.cra_exit		= aspeed_sham_cra_exit,
				}
			}
		},
	},
	{
		.alg.ahash = {
			.init	= aspeed_sha512s_init,
			.update	= aspeed_sham_update,
			.final	= aspeed_sham_final,
			.finup	= aspeed_sham_finup,
			.digest	= aspeed_sham_digest,
			.export	= aspeed_sham_export,
			.import	= aspeed_sham_import,
			.halg = {
				.digestsize = SHA224_DIGEST_SIZE,
				.statesize = sizeof(struct aspeed_sham_reqctx),
				.base = {
					.cra_name		= "sha512_224",
					.cra_driver_name	= "aspeed-sha512_224",
					.cra_priority		= 300,
					.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
								  CRYPTO_ALG_ASYNC |
								  CRYPTO_ALG_KERN_DRIVER_ONLY,
					.cra_blocksize		= SHA512_BLOCK_SIZE,
					.cra_ctxsize		= sizeof(struct aspeed_sham_ctx),
					.cra_alignmask		= 0,
					.cra_module		= THIS_MODULE,
					.cra_init		= aspeed_sham_cra_init,
					.cra_exit		= aspeed_sham_cra_exit,
				}
			}
		},
	},
	{
		.alg.ahash = {
			.init	= aspeed_sha512s_init,
			.update	= aspeed_sham_update,
			.final	= aspeed_sham_final,
			.finup	= aspeed_sham_finup,
			.digest	= aspeed_sham_digest,
			.export	= aspeed_sham_export,
			.import	= aspeed_sham_import,
			.halg = {
				.digestsize = SHA256_DIGEST_SIZE,
				.statesize = sizeof(struct aspeed_sham_reqctx),
				.base = {
					.cra_name		= "sha512_256",
					.cra_driver_name	= "aspeed-sha512_256",
					.cra_priority		= 300,
					.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
								  CRYPTO_ALG_ASYNC |
								  CRYPTO_ALG_KERN_DRIVER_ONLY,
					.cra_blocksize		= SHA512_BLOCK_SIZE,
					.cra_ctxsize		= sizeof(struct aspeed_sham_ctx),
					.cra_alignmask		= 0,
					.cra_module		= THIS_MODULE,
					.cra_init		= aspeed_sham_cra_init,
					.cra_exit		= aspeed_sham_cra_exit,
				}
			}
		},
	},
	{
		.alg_base = "sha384",
		.alg.ahash = {
			.init	= aspeed_sham_init,
			.update	= aspeed_sham_update,
			.final	= aspeed_sham_final,
			.finup	= aspeed_sham_finup,
			.digest	= aspeed_sham_digest,
			.setkey	= aspeed_sham_setkey,
			.export	= aspeed_sham_export,
			.import	= aspeed_sham_import,
			.halg = {
				.digestsize = SHA384_DIGEST_SIZE,
				.statesize = sizeof(struct aspeed_sham_reqctx),
				.base = {
					.cra_name		= "hmac(sha384)",
					.cra_driver_name	= "aspeed-hmac-sha384",
					.cra_priority		= 300,
					.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
								  CRYPTO_ALG_ASYNC |
								  CRYPTO_ALG_KERN_DRIVER_ONLY,
					.cra_blocksize		= SHA384_BLOCK_SIZE,
					.cra_ctxsize		= sizeof(struct aspeed_sham_ctx) +
								sizeof(struct aspeed_sha_hmac_ctx),
					.cra_alignmask		= 0,
					.cra_module		= THIS_MODULE,
					.cra_init		= aspeed_sham_cra_init,
					.cra_exit		= aspeed_sham_cra_exit,
				}
			}
		},
	},
	{
		.alg_base = "sha512",
		.alg.ahash = {
			.init	= aspeed_sham_init,
			.update	= aspeed_sham_update,
			.final	= aspeed_sham_final,
			.finup	= aspeed_sham_finup,
			.digest	= aspeed_sham_digest,
			.setkey	= aspeed_sham_setkey,
			.export	= aspeed_sham_export,
			.import	= aspeed_sham_import,
			.halg = {
				.digestsize = SHA512_DIGEST_SIZE,
				.statesize = sizeof(struct aspeed_sham_reqctx),
				.base = {
					.cra_name		= "hmac(sha512)",
					.cra_driver_name	= "aspeed-hmac-sha512",
					.cra_priority		= 300,
					.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
								  CRYPTO_ALG_ASYNC |
								  CRYPTO_ALG_KERN_DRIVER_ONLY,
					.cra_blocksize		= SHA512_BLOCK_SIZE,
					.cra_ctxsize		= sizeof(struct aspeed_sham_ctx) +
								sizeof(struct aspeed_sha_hmac_ctx),
					.cra_alignmask		= 0,
					.cra_module		= THIS_MODULE,
					.cra_init		= aspeed_sham_cra_init,
					.cra_exit		= aspeed_sham_cra_exit,
				}
			}
		},
	},
	{
		.alg_base = "sha512_224",
		.alg.ahash = {
			.init	= aspeed_sha512s_init,
			.update	= aspeed_sham_update,
			.final	= aspeed_sham_final,
			.finup	= aspeed_sham_finup,
			.digest	= aspeed_sham_digest,
			.setkey	= aspeed_sham_setkey,
			.export	= aspeed_sham_export,
			.import	= aspeed_sham_import,
			.halg = {
				.digestsize = SHA224_DIGEST_SIZE,
				.statesize = sizeof(struct aspeed_sham_reqctx),
				.base = {
					.cra_name		= "hmac(sha512_224)",
					.cra_driver_name	= "aspeed-hmac-sha512_224",
					.cra_priority		= 300,
					.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
								  CRYPTO_ALG_ASYNC |
								  CRYPTO_ALG_KERN_DRIVER_ONLY,
					.cra_blocksize		= SHA512_BLOCK_SIZE,
					.cra_ctxsize		= sizeof(struct aspeed_sham_ctx) +
								sizeof(struct aspeed_sha_hmac_ctx),
					.cra_alignmask		= 0,
					.cra_module		= THIS_MODULE,
					.cra_init		= aspeed_sham_cra_init,
					.cra_exit		= aspeed_sham_cra_exit,
				}
			}
		},
	},
	{
		.alg_base = "sha512_256",
		.alg.ahash = {
			.init	= aspeed_sha512s_init,
			.update	= aspeed_sham_update,
			.final	= aspeed_sham_final,
			.finup	= aspeed_sham_finup,
			.digest	= aspeed_sham_digest,
			.setkey	= aspeed_sham_setkey,
			.export	= aspeed_sham_export,
			.import	= aspeed_sham_import,
			.halg = {
				.digestsize = SHA256_DIGEST_SIZE,
				.statesize = sizeof(struct aspeed_sham_reqctx),
				.base = {
					.cra_name		= "hmac(sha512_256)",
					.cra_driver_name	= "aspeed-hmac-sha512_256",
					.cra_priority		= 300,
					.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
								  CRYPTO_ALG_ASYNC |
								  CRYPTO_ALG_KERN_DRIVER_ONLY,
					.cra_blocksize		= SHA512_BLOCK_SIZE,
					.cra_ctxsize		= sizeof(struct aspeed_sham_ctx) +
								sizeof(struct aspeed_sha_hmac_ctx),
					.cra_alignmask		= 0,
					.cra_module		= THIS_MODULE,
					.cra_init		= aspeed_sham_cra_init,
					.cra_exit		= aspeed_sham_cra_exit,
				}
			}
		},
	},
};

void aspeed_unregister_hace_hash_algs(struct aspeed_hace_dev *hace_dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(aspeed_ahash_algs); i++)
		crypto_unregister_ahash(&aspeed_ahash_algs[i].alg.ahash);

	if (hace_dev->version != AST2600_VERSION)
		return;

	for (i = 0; i < ARRAY_SIZE(aspeed_ahash_algs_g6); i++)
		crypto_unregister_ahash(&aspeed_ahash_algs_g6[i].alg.ahash);
}

void aspeed_register_hace_hash_algs(struct aspeed_hace_dev *hace_dev)
{
	int rc, i;

	AHASH_DBG(hace_dev, "\n");

	for (i = 0; i < ARRAY_SIZE(aspeed_ahash_algs); i++) {
		aspeed_ahash_algs[i].hace_dev = hace_dev;
		rc = crypto_register_ahash(&aspeed_ahash_algs[i].alg.ahash);
		if (rc) {
			AHASH_DBG(hace_dev, "Failed to register %s\n",
				  aspeed_ahash_algs[i].alg.ahash.halg.base.cra_name);
		}
	}

	if (hace_dev->version != AST2600_VERSION)
		return;

	for (i = 0; i < ARRAY_SIZE(aspeed_ahash_algs_g6); i++) {
		aspeed_ahash_algs_g6[i].hace_dev = hace_dev;
		rc = crypto_register_ahash(&aspeed_ahash_algs_g6[i].alg.ahash);
		if (rc) {
			AHASH_DBG(hace_dev, "Failed to register %s\n",
				  aspeed_ahash_algs_g6[i].alg.ahash.halg.base.cra_name);
		}
	}
}
