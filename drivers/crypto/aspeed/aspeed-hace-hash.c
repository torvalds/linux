// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2021 Aspeed Technology Inc.
 */

#include "aspeed-hace.h"
#include <crypto/engine.h>
#include <crypto/internal/hash.h>
#include <crypto/scatterwalk.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/scatterlist.h>
#include <linux/string.h>

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

static int aspeed_sham_init(struct ahash_request *req);
static int aspeed_ahash_req_update(struct aspeed_hace_dev *hace_dev);

static int aspeed_sham_export(struct ahash_request *req, void *out)
{
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);
	union {
		u8 *u8;
		u64 *u64;
	} p = { .u8 = out };

	memcpy(out, rctx->digest, rctx->ivsize);
	p.u8 += rctx->ivsize;
	put_unaligned(rctx->digcnt[0], p.u64++);
	if (rctx->ivsize == 64)
		put_unaligned(rctx->digcnt[1], p.u64);
	return 0;
}

static int aspeed_sham_import(struct ahash_request *req, const void *in)
{
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);
	union {
		const u8 *u8;
		const u64 *u64;
	} p = { .u8 = in };
	int err;

	err = aspeed_sham_init(req);
	if (err)
		return err;

	memcpy(rctx->digest, in, rctx->ivsize);
	p.u8 += rctx->ivsize;
	rctx->digcnt[0] = get_unaligned(p.u64++);
	if (rctx->ivsize == 64)
		rctx->digcnt[1] = get_unaligned(p.u64);
	return 0;
}

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
static int aspeed_ahash_fill_padding(struct aspeed_hace_dev *hace_dev,
				     struct aspeed_sham_reqctx *rctx, u8 *buf)
{
	unsigned int index, padlen, bitslen;
	__be64 bits[2];

	AHASH_DBG(hace_dev, "rctx flags:0x%x\n", (u32)rctx->flags);

	switch (rctx->flags & SHA_FLAGS_MASK) {
	case SHA_FLAGS_SHA1:
	case SHA_FLAGS_SHA224:
	case SHA_FLAGS_SHA256:
		bits[0] = cpu_to_be64(rctx->digcnt[0] << 3);
		index = rctx->digcnt[0] & 0x3f;
		padlen = (index < 56) ? (56 - index) : ((64 + 56) - index);
		bitslen = 8;
		break;
	default:
		bits[1] = cpu_to_be64(rctx->digcnt[0] << 3);
		bits[0] = cpu_to_be64(rctx->digcnt[1] << 3 |
				      rctx->digcnt[0] >> 61);
		index = rctx->digcnt[0] & 0x7f;
		padlen = (index < 112) ? (112 - index) : ((128 + 112) - index);
		bitslen = 16;
		break;
	}
	buf[0] = 0x80;
	memset(buf + 1, 0, padlen - 1);
	memcpy(buf + padlen, bits, bitslen);
	return padlen + bitslen;
}

static void aspeed_ahash_update_counter(struct aspeed_sham_reqctx *rctx,
					unsigned int len)
{
	rctx->offset += len;
	rctx->digcnt[0] += len;
	if (rctx->digcnt[0] < len)
		rctx->digcnt[1]++;
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
	unsigned int length, remain;
	bool final = false;

	length = rctx->total - rctx->offset;
	remain = length - round_down(length, rctx->block_size);

	AHASH_DBG(hace_dev, "length:0x%x, remain:0x%x\n", length, remain);

	if (length > ASPEED_HASH_SRC_DMA_BUF_LEN)
		length = ASPEED_HASH_SRC_DMA_BUF_LEN;
	else if (rctx->flags & SHA_FLAGS_FINUP) {
		if (round_up(length, rctx->block_size) + rctx->block_size >
		    ASPEED_CRYPTO_SRC_DMA_BUF_LEN)
			length = round_down(length - 1, rctx->block_size);
		else
			final = true;
	} else
		length -= remain;
	scatterwalk_map_and_copy(hash_engine->ahash_src_addr, rctx->src_sg,
				 rctx->offset, length, 0);
	aspeed_ahash_update_counter(rctx, length);
	if (final)
		length += aspeed_ahash_fill_padding(
			hace_dev, rctx, hash_engine->ahash_src_addr + length);

	rctx->digest_dma_addr = dma_map_single(hace_dev->dev, rctx->digest,
					       SHA512_DIGEST_SIZE,
					       DMA_BIDIRECTIONAL);
	if (dma_mapping_error(hace_dev->dev, rctx->digest_dma_addr)) {
		dev_warn(hace_dev->dev, "dma_map() rctx digest error\n");
		return -ENOMEM;
	}

	hash_engine->src_length = length;
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
	bool final = rctx->flags & SHA_FLAGS_FINUP;
	int remain, sg_len, i, max_sg_nents;
	unsigned int length, offset, total;
	struct aspeed_sg_list *src_list;
	struct scatterlist *s;
	int rc = 0;

	offset = rctx->offset;
	length = rctx->total - offset;
	remain = final ? 0 : length - round_down(length, rctx->block_size);
	length -= remain;

	AHASH_DBG(hace_dev, "%s:0x%x, %s:0x%x, %s:0x%x\n",
		  "rctx total", rctx->total,
		  "length", length, "remain", remain);

	sg_len = dma_map_sg(hace_dev->dev, rctx->src_sg, rctx->src_nents,
			    DMA_TO_DEVICE);
	if (!sg_len) {
		dev_warn(hace_dev->dev, "dma_map_sg() src error\n");
		rc = -ENOMEM;
		goto end;
	}

	max_sg_nents = ASPEED_HASH_SRC_DMA_BUF_LEN / sizeof(*src_list) - final;
	sg_len = min(sg_len, max_sg_nents);
	src_list = (struct aspeed_sg_list *)hash_engine->ahash_src_addr;
	rctx->digest_dma_addr = dma_map_single(hace_dev->dev, rctx->digest,
					       SHA512_DIGEST_SIZE,
					       DMA_BIDIRECTIONAL);
	if (dma_mapping_error(hace_dev->dev, rctx->digest_dma_addr)) {
		dev_warn(hace_dev->dev, "dma_map() rctx digest error\n");
		rc = -ENOMEM;
		goto free_src_sg;
	}

	total = 0;
	for_each_sg(rctx->src_sg, s, sg_len, i) {
		u32 phy_addr = sg_dma_address(s);
		u32 len = sg_dma_len(s);

		if (len <= offset) {
			offset -= len;
			continue;
		}

		len -= offset;
		phy_addr += offset;
		offset = 0;

		if (length > len)
			length -= len;
		else {
			/* Last sg list */
			len = length;
			length = 0;
		}

		total += len;
		src_list[i].phy_addr = cpu_to_le32(phy_addr);
		src_list[i].len = cpu_to_le32(len);
	}

	if (length != 0) {
		total = round_down(total, rctx->block_size);
		final = false;
	}

	aspeed_ahash_update_counter(rctx, total);
	if (final) {
		int len = aspeed_ahash_fill_padding(hace_dev, rctx,
						    rctx->buffer);

		total += len;
		rctx->buffer_dma_addr = dma_map_single(hace_dev->dev,
						       rctx->buffer,
						       sizeof(rctx->buffer),
						       DMA_TO_DEVICE);
		if (dma_mapping_error(hace_dev->dev, rctx->buffer_dma_addr)) {
			dev_warn(hace_dev->dev, "dma_map() rctx buffer error\n");
			rc = -ENOMEM;
			goto free_rctx_digest;
		}

		src_list[i].phy_addr = cpu_to_le32(rctx->buffer_dma_addr);
		src_list[i].len = cpu_to_le32(len);
		i++;
	}
	src_list[i - 1].len |= cpu_to_le32(HASH_SG_LAST_LIST);

	hash_engine->src_length = total;
	hash_engine->src_dma = hash_engine->ahash_src_dma_addr;
	hash_engine->digest_dma = rctx->digest_dma_addr;

	return 0;

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
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);

	AHASH_DBG(hace_dev, "\n");

	dma_unmap_single(hace_dev->dev, rctx->digest_dma_addr,
			 SHA512_DIGEST_SIZE, DMA_BIDIRECTIONAL);

	if (rctx->total - rctx->offset >= rctx->block_size ||
	    (rctx->total != rctx->offset && rctx->flags & SHA_FLAGS_FINUP))
		return aspeed_ahash_req_update(hace_dev);

	hash_engine->flags &= ~CRYPTO_FLAGS_BUSY;

	if (rctx->flags & SHA_FLAGS_FINUP)
		memcpy(req->result, rctx->digest, rctx->digsize);

	crypto_finalize_hash_request(hace_dev->crypt_engine_hash, req,
				     rctx->total - rctx->offset);

	return 0;
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

static int aspeed_ahash_update_resume_sg(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_hash *hash_engine = &hace_dev->hash_engine;
	struct ahash_request *req = hash_engine->req;
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);

	AHASH_DBG(hace_dev, "\n");

	dma_unmap_sg(hace_dev->dev, rctx->src_sg, rctx->src_nents,
		     DMA_TO_DEVICE);

	if (rctx->flags & SHA_FLAGS_FINUP && rctx->total == rctx->offset)
		dma_unmap_single(hace_dev->dev, rctx->buffer_dma_addr,
				 sizeof(rctx->buffer), DMA_TO_DEVICE);

	rctx->cmd &= ~HASH_CMD_HASH_SRC_SG_CTRL;

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
		resume = aspeed_ahash_complete;
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

static noinline int aspeed_ahash_fallback(struct ahash_request *req)
{
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);
	HASH_FBREQ_ON_STACK(fbreq, req);
	u8 *state = rctx->buffer;
	struct scatterlist sg[2];
	struct scatterlist *ssg;
	int ret;

	ssg = scatterwalk_ffwd(sg, req->src, rctx->offset);
	ahash_request_set_crypt(fbreq, ssg, req->result,
				rctx->total - rctx->offset);

	ret = aspeed_sham_export(req, state) ?:
	      crypto_ahash_import_core(fbreq, state);

	if (rctx->flags & SHA_FLAGS_FINUP)
		ret = ret ?: crypto_ahash_finup(fbreq);
	else
		ret = ret ?: crypto_ahash_update(fbreq) ?:
			     crypto_ahash_export_core(fbreq, state) ?:
			     aspeed_sham_import(req, state);
	HASH_REQUEST_ZERO(fbreq);
	return ret;
}

static int aspeed_ahash_do_request(struct crypto_engine *engine, void *areq)
{
	struct ahash_request *req = ahash_request_cast(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct aspeed_sham_ctx *tctx = crypto_ahash_ctx(tfm);
	struct aspeed_hace_dev *hace_dev = tctx->hace_dev;
	struct aspeed_engine_hash *hash_engine;
	int ret;

	hash_engine = &hace_dev->hash_engine;
	hash_engine->flags |= CRYPTO_FLAGS_BUSY;

	ret = aspeed_ahash_req_update(hace_dev);
	if (ret != -EINPROGRESS)
		return aspeed_ahash_fallback(req);

	return 0;
}

static void aspeed_ahash_prepare_request(struct crypto_engine *engine,
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
}

static int aspeed_ahash_do_one(struct crypto_engine *engine, void *areq)
{
	aspeed_ahash_prepare_request(engine, areq);
	return aspeed_ahash_do_request(engine, areq);
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
	rctx->src_nents = sg_nents_for_len(req->src, req->nbytes);

	return aspeed_hace_hash_handle_queue(hace_dev, req);
}

static int aspeed_sham_finup(struct ahash_request *req)
{
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct aspeed_sham_ctx *tctx = crypto_ahash_ctx(tfm);
	struct aspeed_hace_dev *hace_dev = tctx->hace_dev;

	AHASH_DBG(hace_dev, "req->nbytes: %d\n", req->nbytes);

	rctx->flags |= SHA_FLAGS_FINUP;

	return aspeed_sham_update(req);
}

static int aspeed_sham_init(struct ahash_request *req)
{
	struct aspeed_sham_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct aspeed_sham_ctx *tctx = crypto_ahash_ctx(tfm);
	struct aspeed_hace_dev *hace_dev = tctx->hace_dev;

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
		rctx->ivsize = 32;
		memcpy(rctx->digest, sha1_iv, rctx->ivsize);
		break;
	case SHA224_DIGEST_SIZE:
		rctx->cmd |= HASH_CMD_SHA224 | HASH_CMD_SHA_SWAP;
		rctx->flags |= SHA_FLAGS_SHA224;
		rctx->digsize = SHA224_DIGEST_SIZE;
		rctx->block_size = SHA224_BLOCK_SIZE;
		rctx->ivsize = 32;
		memcpy(rctx->digest, sha224_iv, rctx->ivsize);
		break;
	case SHA256_DIGEST_SIZE:
		rctx->cmd |= HASH_CMD_SHA256 | HASH_CMD_SHA_SWAP;
		rctx->flags |= SHA_FLAGS_SHA256;
		rctx->digsize = SHA256_DIGEST_SIZE;
		rctx->block_size = SHA256_BLOCK_SIZE;
		rctx->ivsize = 32;
		memcpy(rctx->digest, sha256_iv, rctx->ivsize);
		break;
	case SHA384_DIGEST_SIZE:
		rctx->cmd |= HASH_CMD_SHA512_SER | HASH_CMD_SHA384 |
			     HASH_CMD_SHA_SWAP;
		rctx->flags |= SHA_FLAGS_SHA384;
		rctx->digsize = SHA384_DIGEST_SIZE;
		rctx->block_size = SHA384_BLOCK_SIZE;
		rctx->ivsize = 64;
		memcpy(rctx->digest, sha384_iv, rctx->ivsize);
		break;
	case SHA512_DIGEST_SIZE:
		rctx->cmd |= HASH_CMD_SHA512_SER | HASH_CMD_SHA512 |
			     HASH_CMD_SHA_SWAP;
		rctx->flags |= SHA_FLAGS_SHA512;
		rctx->digsize = SHA512_DIGEST_SIZE;
		rctx->block_size = SHA512_BLOCK_SIZE;
		rctx->ivsize = 64;
		memcpy(rctx->digest, sha512_iv, rctx->ivsize);
		break;
	default:
		dev_warn(tctx->hace_dev->dev, "digest size %d not support\n",
			 crypto_ahash_digestsize(tfm));
		return -EINVAL;
	}

	rctx->total = 0;
	rctx->digcnt[0] = 0;
	rctx->digcnt[1] = 0;

	return 0;
}

static int aspeed_sham_digest(struct ahash_request *req)
{
	return aspeed_sham_init(req) ? : aspeed_sham_finup(req);
}

static int aspeed_sham_cra_init(struct crypto_ahash *tfm)
{
	struct ahash_alg *alg = crypto_ahash_alg(tfm);
	struct aspeed_sham_ctx *tctx = crypto_ahash_ctx(tfm);
	struct aspeed_hace_alg *ast_alg;

	ast_alg = container_of(alg, struct aspeed_hace_alg, alg.ahash.base);
	tctx->hace_dev = ast_alg->hace_dev;

	return 0;
}

static struct aspeed_hace_alg aspeed_ahash_algs[] = {
	{
		.alg.ahash.base = {
			.init	= aspeed_sham_init,
			.update	= aspeed_sham_update,
			.finup	= aspeed_sham_finup,
			.digest	= aspeed_sham_digest,
			.export	= aspeed_sham_export,
			.import	= aspeed_sham_import,
			.init_tfm = aspeed_sham_cra_init,
			.halg = {
				.digestsize = SHA1_DIGEST_SIZE,
				.statesize = sizeof(struct aspeed_sham_reqctx),
				.base = {
					.cra_name		= "sha1",
					.cra_driver_name	= "aspeed-sha1",
					.cra_priority		= 300,
					.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
								  CRYPTO_ALG_ASYNC |
								  CRYPTO_AHASH_ALG_BLOCK_ONLY |
								  CRYPTO_ALG_KERN_DRIVER_ONLY,
					.cra_blocksize		= SHA1_BLOCK_SIZE,
					.cra_ctxsize		= sizeof(struct aspeed_sham_ctx),
					.cra_reqsize		= sizeof(struct aspeed_sham_reqctx),
					.cra_alignmask		= 0,
					.cra_module		= THIS_MODULE,
				}
			}
		},
		.alg.ahash.op = {
			.do_one_request = aspeed_ahash_do_one,
		},
	},
	{
		.alg.ahash.base = {
			.init	= aspeed_sham_init,
			.update	= aspeed_sham_update,
			.finup	= aspeed_sham_finup,
			.digest	= aspeed_sham_digest,
			.export	= aspeed_sham_export,
			.import	= aspeed_sham_import,
			.init_tfm = aspeed_sham_cra_init,
			.halg = {
				.digestsize = SHA256_DIGEST_SIZE,
				.statesize = sizeof(struct aspeed_sham_reqctx),
				.base = {
					.cra_name		= "sha256",
					.cra_driver_name	= "aspeed-sha256",
					.cra_priority		= 300,
					.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
								  CRYPTO_ALG_ASYNC |
								  CRYPTO_AHASH_ALG_BLOCK_ONLY |
								  CRYPTO_ALG_KERN_DRIVER_ONLY,
					.cra_blocksize		= SHA256_BLOCK_SIZE,
					.cra_ctxsize		= sizeof(struct aspeed_sham_ctx),
					.cra_reqsize		= sizeof(struct aspeed_sham_reqctx),
					.cra_alignmask		= 0,
					.cra_module		= THIS_MODULE,
				}
			}
		},
		.alg.ahash.op = {
			.do_one_request = aspeed_ahash_do_one,
		},
	},
	{
		.alg.ahash.base = {
			.init	= aspeed_sham_init,
			.update	= aspeed_sham_update,
			.finup	= aspeed_sham_finup,
			.digest	= aspeed_sham_digest,
			.export	= aspeed_sham_export,
			.import	= aspeed_sham_import,
			.init_tfm = aspeed_sham_cra_init,
			.halg = {
				.digestsize = SHA224_DIGEST_SIZE,
				.statesize = sizeof(struct aspeed_sham_reqctx),
				.base = {
					.cra_name		= "sha224",
					.cra_driver_name	= "aspeed-sha224",
					.cra_priority		= 300,
					.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
								  CRYPTO_ALG_ASYNC |
								  CRYPTO_AHASH_ALG_BLOCK_ONLY |
								  CRYPTO_ALG_KERN_DRIVER_ONLY,
					.cra_blocksize		= SHA224_BLOCK_SIZE,
					.cra_ctxsize		= sizeof(struct aspeed_sham_ctx),
					.cra_reqsize		= sizeof(struct aspeed_sham_reqctx),
					.cra_alignmask		= 0,
					.cra_module		= THIS_MODULE,
				}
			}
		},
		.alg.ahash.op = {
			.do_one_request = aspeed_ahash_do_one,
		},
	},
};

static struct aspeed_hace_alg aspeed_ahash_algs_g6[] = {
	{
		.alg.ahash.base = {
			.init	= aspeed_sham_init,
			.update	= aspeed_sham_update,
			.finup	= aspeed_sham_finup,
			.digest	= aspeed_sham_digest,
			.export	= aspeed_sham_export,
			.import	= aspeed_sham_import,
			.init_tfm = aspeed_sham_cra_init,
			.halg = {
				.digestsize = SHA384_DIGEST_SIZE,
				.statesize = sizeof(struct aspeed_sham_reqctx),
				.base = {
					.cra_name		= "sha384",
					.cra_driver_name	= "aspeed-sha384",
					.cra_priority		= 300,
					.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
								  CRYPTO_ALG_ASYNC |
								  CRYPTO_AHASH_ALG_BLOCK_ONLY |
								  CRYPTO_ALG_KERN_DRIVER_ONLY,
					.cra_blocksize		= SHA384_BLOCK_SIZE,
					.cra_ctxsize		= sizeof(struct aspeed_sham_ctx),
					.cra_reqsize		= sizeof(struct aspeed_sham_reqctx),
					.cra_alignmask		= 0,
					.cra_module		= THIS_MODULE,
				}
			}
		},
		.alg.ahash.op = {
			.do_one_request = aspeed_ahash_do_one,
		},
	},
	{
		.alg.ahash.base = {
			.init	= aspeed_sham_init,
			.update	= aspeed_sham_update,
			.finup	= aspeed_sham_finup,
			.digest	= aspeed_sham_digest,
			.export	= aspeed_sham_export,
			.import	= aspeed_sham_import,
			.init_tfm = aspeed_sham_cra_init,
			.halg = {
				.digestsize = SHA512_DIGEST_SIZE,
				.statesize = sizeof(struct aspeed_sham_reqctx),
				.base = {
					.cra_name		= "sha512",
					.cra_driver_name	= "aspeed-sha512",
					.cra_priority		= 300,
					.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
								  CRYPTO_ALG_ASYNC |
								  CRYPTO_AHASH_ALG_BLOCK_ONLY |
								  CRYPTO_ALG_KERN_DRIVER_ONLY,
					.cra_blocksize		= SHA512_BLOCK_SIZE,
					.cra_ctxsize		= sizeof(struct aspeed_sham_ctx),
					.cra_reqsize		= sizeof(struct aspeed_sham_reqctx),
					.cra_alignmask		= 0,
					.cra_module		= THIS_MODULE,
				}
			}
		},
		.alg.ahash.op = {
			.do_one_request = aspeed_ahash_do_one,
		},
	},
};

void aspeed_unregister_hace_hash_algs(struct aspeed_hace_dev *hace_dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(aspeed_ahash_algs); i++)
		crypto_engine_unregister_ahash(&aspeed_ahash_algs[i].alg.ahash);

	if (hace_dev->version != AST2600_VERSION)
		return;

	for (i = 0; i < ARRAY_SIZE(aspeed_ahash_algs_g6); i++)
		crypto_engine_unregister_ahash(&aspeed_ahash_algs_g6[i].alg.ahash);
}

void aspeed_register_hace_hash_algs(struct aspeed_hace_dev *hace_dev)
{
	int rc, i;

	AHASH_DBG(hace_dev, "\n");

	for (i = 0; i < ARRAY_SIZE(aspeed_ahash_algs); i++) {
		aspeed_ahash_algs[i].hace_dev = hace_dev;
		rc = crypto_engine_register_ahash(&aspeed_ahash_algs[i].alg.ahash);
		if (rc) {
			AHASH_DBG(hace_dev, "Failed to register %s\n",
				  aspeed_ahash_algs[i].alg.ahash.base.halg.base.cra_name);
		}
	}

	if (hace_dev->version != AST2600_VERSION)
		return;

	for (i = 0; i < ARRAY_SIZE(aspeed_ahash_algs_g6); i++) {
		aspeed_ahash_algs_g6[i].hace_dev = hace_dev;
		rc = crypto_engine_register_ahash(&aspeed_ahash_algs_g6[i].alg.ahash);
		if (rc) {
			AHASH_DBG(hace_dev, "Failed to register %s\n",
				  aspeed_ahash_algs_g6[i].alg.ahash.base.halg.base.cra_name);
		}
	}
}
