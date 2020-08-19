/*
 * AMD Cryptographic Coprocessor (CCP) driver
 *
 * Copyright (C) 2013,2017 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 * Author: Gary R Hook <gary.hook@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <crypto/scatterwalk.h>
#include <crypto/des.h>
#include <linux/ccp.h>

#include "ccp-dev.h"

/* SHA initial context values */
static const __be32 ccp_sha1_init[SHA1_DIGEST_SIZE / sizeof(__be32)] = {
	cpu_to_be32(SHA1_H0), cpu_to_be32(SHA1_H1),
	cpu_to_be32(SHA1_H2), cpu_to_be32(SHA1_H3),
	cpu_to_be32(SHA1_H4),
};

static const __be32 ccp_sha224_init[SHA256_DIGEST_SIZE / sizeof(__be32)] = {
	cpu_to_be32(SHA224_H0), cpu_to_be32(SHA224_H1),
	cpu_to_be32(SHA224_H2), cpu_to_be32(SHA224_H3),
	cpu_to_be32(SHA224_H4), cpu_to_be32(SHA224_H5),
	cpu_to_be32(SHA224_H6), cpu_to_be32(SHA224_H7),
};

static const __be32 ccp_sha256_init[SHA256_DIGEST_SIZE / sizeof(__be32)] = {
	cpu_to_be32(SHA256_H0), cpu_to_be32(SHA256_H1),
	cpu_to_be32(SHA256_H2), cpu_to_be32(SHA256_H3),
	cpu_to_be32(SHA256_H4), cpu_to_be32(SHA256_H5),
	cpu_to_be32(SHA256_H6), cpu_to_be32(SHA256_H7),
};

static const __be64 ccp_sha384_init[SHA512_DIGEST_SIZE / sizeof(__be64)] = {
	cpu_to_be64(SHA384_H0), cpu_to_be64(SHA384_H1),
	cpu_to_be64(SHA384_H2), cpu_to_be64(SHA384_H3),
	cpu_to_be64(SHA384_H4), cpu_to_be64(SHA384_H5),
	cpu_to_be64(SHA384_H6), cpu_to_be64(SHA384_H7),
};

static const __be64 ccp_sha512_init[SHA512_DIGEST_SIZE / sizeof(__be64)] = {
	cpu_to_be64(SHA512_H0), cpu_to_be64(SHA512_H1),
	cpu_to_be64(SHA512_H2), cpu_to_be64(SHA512_H3),
	cpu_to_be64(SHA512_H4), cpu_to_be64(SHA512_H5),
	cpu_to_be64(SHA512_H6), cpu_to_be64(SHA512_H7),
};

#define	CCP_NEW_JOBID(ccp)	((ccp->vdata->version == CCP_VERSION(3, 0)) ? \
					ccp_gen_jobid(ccp) : 0)

static u32 ccp_gen_jobid(struct ccp_device *ccp)
{
	return atomic_inc_return(&ccp->current_id) & CCP_JOBID_MASK;
}

static void ccp_sg_free(struct ccp_sg_workarea *wa)
{
	if (wa->dma_count)
		dma_unmap_sg(wa->dma_dev, wa->dma_sg_head, wa->nents, wa->dma_dir);

	wa->dma_count = 0;
}

static int ccp_init_sg_workarea(struct ccp_sg_workarea *wa, struct device *dev,
				struct scatterlist *sg, u64 len,
				enum dma_data_direction dma_dir)
{
	memset(wa, 0, sizeof(*wa));

	wa->sg = sg;
	if (!sg)
		return 0;

	wa->nents = sg_nents_for_len(sg, len);
	if (wa->nents < 0)
		return wa->nents;

	wa->bytes_left = len;
	wa->sg_used = 0;

	if (len == 0)
		return 0;

	if (dma_dir == DMA_NONE)
		return 0;

	wa->dma_sg = sg;
	wa->dma_sg_head = sg;
	wa->dma_dev = dev;
	wa->dma_dir = dma_dir;
	wa->dma_count = dma_map_sg(dev, sg, wa->nents, dma_dir);
	if (!wa->dma_count)
		return -ENOMEM;

	return 0;
}

static void ccp_update_sg_workarea(struct ccp_sg_workarea *wa, unsigned int len)
{
	unsigned int nbytes = min_t(u64, len, wa->bytes_left);
	unsigned int sg_combined_len = 0;

	if (!wa->sg)
		return;

	wa->sg_used += nbytes;
	wa->bytes_left -= nbytes;
	if (wa->sg_used == sg_dma_len(wa->dma_sg)) {
		/* Advance to the next DMA scatterlist entry */
		wa->dma_sg = sg_next(wa->dma_sg);

		/* In the case that the DMA mapped scatterlist has entries
		 * that have been merged, the non-DMA mapped scatterlist
		 * must be advanced multiple times for each merged entry.
		 * This ensures that the current non-DMA mapped entry
		 * corresponds to the current DMA mapped entry.
		 */
		do {
			sg_combined_len += wa->sg->length;
			wa->sg = sg_next(wa->sg);
		} while (wa->sg_used > sg_combined_len);

		wa->sg_used = 0;
	}
}

static void ccp_dm_free(struct ccp_dm_workarea *wa)
{
	if (wa->length <= CCP_DMAPOOL_MAX_SIZE) {
		if (wa->address)
			dma_pool_free(wa->dma_pool, wa->address,
				      wa->dma.address);
	} else {
		if (wa->dma.address)
			dma_unmap_single(wa->dev, wa->dma.address, wa->length,
					 wa->dma.dir);
		kfree(wa->address);
	}

	wa->address = NULL;
	wa->dma.address = 0;
}

static int ccp_init_dm_workarea(struct ccp_dm_workarea *wa,
				struct ccp_cmd_queue *cmd_q,
				unsigned int len,
				enum dma_data_direction dir)
{
	memset(wa, 0, sizeof(*wa));

	if (!len)
		return 0;

	wa->dev = cmd_q->ccp->dev;
	wa->length = len;

	if (len <= CCP_DMAPOOL_MAX_SIZE) {
		wa->dma_pool = cmd_q->dma_pool;

		wa->address = dma_pool_alloc(wa->dma_pool, GFP_KERNEL,
					     &wa->dma.address);
		if (!wa->address)
			return -ENOMEM;

		wa->dma.length = CCP_DMAPOOL_MAX_SIZE;

		memset(wa->address, 0, CCP_DMAPOOL_MAX_SIZE);
	} else {
		wa->address = kzalloc(len, GFP_KERNEL);
		if (!wa->address)
			return -ENOMEM;

		wa->dma.address = dma_map_single(wa->dev, wa->address, len,
						 dir);
		if (dma_mapping_error(wa->dev, wa->dma.address))
			return -ENOMEM;

		wa->dma.length = len;
	}
	wa->dma.dir = dir;

	return 0;
}

static int ccp_set_dm_area(struct ccp_dm_workarea *wa, unsigned int wa_offset,
			   struct scatterlist *sg, unsigned int sg_offset,
			   unsigned int len)
{
	WARN_ON(!wa->address);

	if (len > (wa->length - wa_offset))
		return -EINVAL;

	scatterwalk_map_and_copy(wa->address + wa_offset, sg, sg_offset, len,
				 0);
	return 0;
}

static void ccp_get_dm_area(struct ccp_dm_workarea *wa, unsigned int wa_offset,
			    struct scatterlist *sg, unsigned int sg_offset,
			    unsigned int len)
{
	WARN_ON(!wa->address);

	scatterwalk_map_and_copy(wa->address + wa_offset, sg, sg_offset, len,
				 1);
}

static int ccp_reverse_set_dm_area(struct ccp_dm_workarea *wa,
				   unsigned int wa_offset,
				   struct scatterlist *sg,
				   unsigned int sg_offset,
				   unsigned int len)
{
	u8 *p, *q;
	int	rc;

	rc = ccp_set_dm_area(wa, wa_offset, sg, sg_offset, len);
	if (rc)
		return rc;

	p = wa->address + wa_offset;
	q = p + len - 1;
	while (p < q) {
		*p = *p ^ *q;
		*q = *p ^ *q;
		*p = *p ^ *q;
		p++;
		q--;
	}
	return 0;
}

static void ccp_reverse_get_dm_area(struct ccp_dm_workarea *wa,
				    unsigned int wa_offset,
				    struct scatterlist *sg,
				    unsigned int sg_offset,
				    unsigned int len)
{
	u8 *p, *q;

	p = wa->address + wa_offset;
	q = p + len - 1;
	while (p < q) {
		*p = *p ^ *q;
		*q = *p ^ *q;
		*p = *p ^ *q;
		p++;
		q--;
	}

	ccp_get_dm_area(wa, wa_offset, sg, sg_offset, len);
}

static void ccp_free_data(struct ccp_data *data, struct ccp_cmd_queue *cmd_q)
{
	ccp_dm_free(&data->dm_wa);
	ccp_sg_free(&data->sg_wa);
}

static int ccp_init_data(struct ccp_data *data, struct ccp_cmd_queue *cmd_q,
			 struct scatterlist *sg, u64 sg_len,
			 unsigned int dm_len,
			 enum dma_data_direction dir)
{
	int ret;

	memset(data, 0, sizeof(*data));

	ret = ccp_init_sg_workarea(&data->sg_wa, cmd_q->ccp->dev, sg, sg_len,
				   dir);
	if (ret)
		goto e_err;

	ret = ccp_init_dm_workarea(&data->dm_wa, cmd_q, dm_len, dir);
	if (ret)
		goto e_err;

	return 0;

e_err:
	ccp_free_data(data, cmd_q);

	return ret;
}

static unsigned int ccp_queue_buf(struct ccp_data *data, unsigned int from)
{
	struct ccp_sg_workarea *sg_wa = &data->sg_wa;
	struct ccp_dm_workarea *dm_wa = &data->dm_wa;
	unsigned int buf_count, nbytes;

	/* Clear the buffer if setting it */
	if (!from)
		memset(dm_wa->address, 0, dm_wa->length);

	if (!sg_wa->sg)
		return 0;

	/* Perform the copy operation
	 *   nbytes will always be <= UINT_MAX because dm_wa->length is
	 *   an unsigned int
	 */
	nbytes = min_t(u64, sg_wa->bytes_left, dm_wa->length);
	scatterwalk_map_and_copy(dm_wa->address, sg_wa->sg, sg_wa->sg_used,
				 nbytes, from);

	/* Update the structures and generate the count */
	buf_count = 0;
	while (sg_wa->bytes_left && (buf_count < dm_wa->length)) {
		nbytes = min(sg_dma_len(sg_wa->dma_sg) - sg_wa->sg_used,
			     dm_wa->length - buf_count);
		nbytes = min_t(u64, sg_wa->bytes_left, nbytes);

		buf_count += nbytes;
		ccp_update_sg_workarea(sg_wa, nbytes);
	}

	return buf_count;
}

static unsigned int ccp_fill_queue_buf(struct ccp_data *data)
{
	return ccp_queue_buf(data, 0);
}

static unsigned int ccp_empty_queue_buf(struct ccp_data *data)
{
	return ccp_queue_buf(data, 1);
}

static void ccp_prepare_data(struct ccp_data *src, struct ccp_data *dst,
			     struct ccp_op *op, unsigned int block_size,
			     bool blocksize_op)
{
	unsigned int sg_src_len, sg_dst_len, op_len;

	/* The CCP can only DMA from/to one address each per operation. This
	 * requires that we find the smallest DMA area between the source
	 * and destination. The resulting len values will always be <= UINT_MAX
	 * because the dma length is an unsigned int.
	 */
	sg_src_len = sg_dma_len(src->sg_wa.dma_sg) - src->sg_wa.sg_used;
	sg_src_len = min_t(u64, src->sg_wa.bytes_left, sg_src_len);

	if (dst) {
		sg_dst_len = sg_dma_len(dst->sg_wa.dma_sg) - dst->sg_wa.sg_used;
		sg_dst_len = min_t(u64, src->sg_wa.bytes_left, sg_dst_len);
		op_len = min(sg_src_len, sg_dst_len);
	} else {
		op_len = sg_src_len;
	}

	/* The data operation length will be at least block_size in length
	 * or the smaller of available sg room remaining for the source or
	 * the destination
	 */
	op_len = max(op_len, block_size);

	/* Unless we have to buffer data, there's no reason to wait */
	op->soc = 0;

	if (sg_src_len < block_size) {
		/* Not enough data in the sg element, so it
		 * needs to be buffered into a blocksize chunk
		 */
		int cp_len = ccp_fill_queue_buf(src);

		op->soc = 1;
		op->src.u.dma.address = src->dm_wa.dma.address;
		op->src.u.dma.offset = 0;
		op->src.u.dma.length = (blocksize_op) ? block_size : cp_len;
	} else {
		/* Enough data in the sg element, but we need to
		 * adjust for any previously copied data
		 */
		op->src.u.dma.address = sg_dma_address(src->sg_wa.dma_sg);
		op->src.u.dma.offset = src->sg_wa.sg_used;
		op->src.u.dma.length = op_len & ~(block_size - 1);

		ccp_update_sg_workarea(&src->sg_wa, op->src.u.dma.length);
	}

	if (dst) {
		if (sg_dst_len < block_size) {
			/* Not enough room in the sg element or we're on the
			 * last piece of data (when using padding), so the
			 * output needs to be buffered into a blocksize chunk
			 */
			op->soc = 1;
			op->dst.u.dma.address = dst->dm_wa.dma.address;
			op->dst.u.dma.offset = 0;
			op->dst.u.dma.length = op->src.u.dma.length;
		} else {
			/* Enough room in the sg element, but we need to
			 * adjust for any previously used area
			 */
			op->dst.u.dma.address = sg_dma_address(dst->sg_wa.dma_sg);
			op->dst.u.dma.offset = dst->sg_wa.sg_used;
			op->dst.u.dma.length = op->src.u.dma.length;
		}
	}
}

static void ccp_process_data(struct ccp_data *src, struct ccp_data *dst,
			     struct ccp_op *op)
{
	op->init = 0;

	if (dst) {
		if (op->dst.u.dma.address == dst->dm_wa.dma.address)
			ccp_empty_queue_buf(dst);
		else
			ccp_update_sg_workarea(&dst->sg_wa,
					       op->dst.u.dma.length);
	}
}

static int ccp_copy_to_from_sb(struct ccp_cmd_queue *cmd_q,
			       struct ccp_dm_workarea *wa, u32 jobid, u32 sb,
			       u32 byte_swap, bool from)
{
	struct ccp_op op;

	memset(&op, 0, sizeof(op));

	op.cmd_q = cmd_q;
	op.jobid = jobid;
	op.eom = 1;

	if (from) {
		op.soc = 1;
		op.src.type = CCP_MEMTYPE_SB;
		op.src.u.sb = sb;
		op.dst.type = CCP_MEMTYPE_SYSTEM;
		op.dst.u.dma.address = wa->dma.address;
		op.dst.u.dma.length = wa->length;
	} else {
		op.src.type = CCP_MEMTYPE_SYSTEM;
		op.src.u.dma.address = wa->dma.address;
		op.src.u.dma.length = wa->length;
		op.dst.type = CCP_MEMTYPE_SB;
		op.dst.u.sb = sb;
	}

	op.u.passthru.byte_swap = byte_swap;

	return cmd_q->ccp->vdata->perform->passthru(&op);
}

static int ccp_copy_to_sb(struct ccp_cmd_queue *cmd_q,
			  struct ccp_dm_workarea *wa, u32 jobid, u32 sb,
			  u32 byte_swap)
{
	return ccp_copy_to_from_sb(cmd_q, wa, jobid, sb, byte_swap, false);
}

static int ccp_copy_from_sb(struct ccp_cmd_queue *cmd_q,
			    struct ccp_dm_workarea *wa, u32 jobid, u32 sb,
			    u32 byte_swap)
{
	return ccp_copy_to_from_sb(cmd_q, wa, jobid, sb, byte_swap, true);
}

static noinline_for_stack int
ccp_run_aes_cmac_cmd(struct ccp_cmd_queue *cmd_q, struct ccp_cmd *cmd)
{
	struct ccp_aes_engine *aes = &cmd->u.aes;
	struct ccp_dm_workarea key, ctx;
	struct ccp_data src;
	struct ccp_op op;
	unsigned int dm_offset;
	int ret;

	if (!((aes->key_len == AES_KEYSIZE_128) ||
	      (aes->key_len == AES_KEYSIZE_192) ||
	      (aes->key_len == AES_KEYSIZE_256)))
		return -EINVAL;

	if (aes->src_len & (AES_BLOCK_SIZE - 1))
		return -EINVAL;

	if (aes->iv_len != AES_BLOCK_SIZE)
		return -EINVAL;

	if (!aes->key || !aes->iv || !aes->src)
		return -EINVAL;

	if (aes->cmac_final) {
		if (aes->cmac_key_len != AES_BLOCK_SIZE)
			return -EINVAL;

		if (!aes->cmac_key)
			return -EINVAL;
	}

	BUILD_BUG_ON(CCP_AES_KEY_SB_COUNT != 1);
	BUILD_BUG_ON(CCP_AES_CTX_SB_COUNT != 1);

	ret = -EIO;
	memset(&op, 0, sizeof(op));
	op.cmd_q = cmd_q;
	op.jobid = CCP_NEW_JOBID(cmd_q->ccp);
	op.sb_key = cmd_q->sb_key;
	op.sb_ctx = cmd_q->sb_ctx;
	op.init = 1;
	op.u.aes.type = aes->type;
	op.u.aes.mode = aes->mode;
	op.u.aes.action = aes->action;

	/* All supported key sizes fit in a single (32-byte) SB entry
	 * and must be in little endian format. Use the 256-bit byte
	 * swap passthru option to convert from big endian to little
	 * endian.
	 */
	ret = ccp_init_dm_workarea(&key, cmd_q,
				   CCP_AES_KEY_SB_COUNT * CCP_SB_BYTES,
				   DMA_TO_DEVICE);
	if (ret)
		return ret;

	dm_offset = CCP_SB_BYTES - aes->key_len;
	ret = ccp_set_dm_area(&key, dm_offset, aes->key, 0, aes->key_len);
	if (ret)
		goto e_key;
	ret = ccp_copy_to_sb(cmd_q, &key, op.jobid, op.sb_key,
			     CCP_PASSTHRU_BYTESWAP_256BIT);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_key;
	}

	/* The AES context fits in a single (32-byte) SB entry and
	 * must be in little endian format. Use the 256-bit byte swap
	 * passthru option to convert from big endian to little endian.
	 */
	ret = ccp_init_dm_workarea(&ctx, cmd_q,
				   CCP_AES_CTX_SB_COUNT * CCP_SB_BYTES,
				   DMA_BIDIRECTIONAL);
	if (ret)
		goto e_key;

	dm_offset = CCP_SB_BYTES - AES_BLOCK_SIZE;
	ret = ccp_set_dm_area(&ctx, dm_offset, aes->iv, 0, aes->iv_len);
	if (ret)
		goto e_ctx;
	ret = ccp_copy_to_sb(cmd_q, &ctx, op.jobid, op.sb_ctx,
			     CCP_PASSTHRU_BYTESWAP_256BIT);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_ctx;
	}

	/* Send data to the CCP AES engine */
	ret = ccp_init_data(&src, cmd_q, aes->src, aes->src_len,
			    AES_BLOCK_SIZE, DMA_TO_DEVICE);
	if (ret)
		goto e_ctx;

	while (src.sg_wa.bytes_left) {
		ccp_prepare_data(&src, NULL, &op, AES_BLOCK_SIZE, true);
		if (aes->cmac_final && !src.sg_wa.bytes_left) {
			op.eom = 1;

			/* Push the K1/K2 key to the CCP now */
			ret = ccp_copy_from_sb(cmd_q, &ctx, op.jobid,
					       op.sb_ctx,
					       CCP_PASSTHRU_BYTESWAP_256BIT);
			if (ret) {
				cmd->engine_error = cmd_q->cmd_error;
				goto e_src;
			}

			ret = ccp_set_dm_area(&ctx, 0, aes->cmac_key, 0,
					      aes->cmac_key_len);
			if (ret)
				goto e_src;
			ret = ccp_copy_to_sb(cmd_q, &ctx, op.jobid, op.sb_ctx,
					     CCP_PASSTHRU_BYTESWAP_256BIT);
			if (ret) {
				cmd->engine_error = cmd_q->cmd_error;
				goto e_src;
			}
		}

		ret = cmd_q->ccp->vdata->perform->aes(&op);
		if (ret) {
			cmd->engine_error = cmd_q->cmd_error;
			goto e_src;
		}

		ccp_process_data(&src, NULL, &op);
	}

	/* Retrieve the AES context - convert from LE to BE using
	 * 32-byte (256-bit) byteswapping
	 */
	ret = ccp_copy_from_sb(cmd_q, &ctx, op.jobid, op.sb_ctx,
			       CCP_PASSTHRU_BYTESWAP_256BIT);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_src;
	}

	/* ...but we only need AES_BLOCK_SIZE bytes */
	dm_offset = CCP_SB_BYTES - AES_BLOCK_SIZE;
	ccp_get_dm_area(&ctx, dm_offset, aes->iv, 0, aes->iv_len);

e_src:
	ccp_free_data(&src, cmd_q);

e_ctx:
	ccp_dm_free(&ctx);

e_key:
	ccp_dm_free(&key);

	return ret;
}

static noinline_for_stack int
ccp_run_aes_gcm_cmd(struct ccp_cmd_queue *cmd_q, struct ccp_cmd *cmd)
{
	struct ccp_aes_engine *aes = &cmd->u.aes;
	struct ccp_dm_workarea key, ctx, final_wa, tag;
	struct ccp_data src, dst;
	struct ccp_data aad;
	struct ccp_op op;

	unsigned long long *final;
	unsigned int dm_offset;
	unsigned int authsize;
	unsigned int jobid;
	unsigned int ilen;
	bool in_place = true; /* Default value */
	int ret;

	struct scatterlist *p_inp, sg_inp[2];
	struct scatterlist *p_tag, sg_tag[2];
	struct scatterlist *p_outp, sg_outp[2];
	struct scatterlist *p_aad;

	if (!aes->iv)
		return -EINVAL;

	if (!((aes->key_len == AES_KEYSIZE_128) ||
		(aes->key_len == AES_KEYSIZE_192) ||
		(aes->key_len == AES_KEYSIZE_256)))
		return -EINVAL;

	if (!aes->key) /* Gotta have a key SGL */
		return -EINVAL;

	/* Zero defaults to 16 bytes, the maximum size */
	authsize = aes->authsize ? aes->authsize : AES_BLOCK_SIZE;
	switch (authsize) {
	case 16:
	case 15:
	case 14:
	case 13:
	case 12:
	case 8:
	case 4:
		break;
	default:
		return -EINVAL;
	}

	/* First, decompose the source buffer into AAD & PT,
	 * and the destination buffer into AAD, CT & tag, or
	 * the input into CT & tag.
	 * It is expected that the input and output SGs will
	 * be valid, even if the AAD and input lengths are 0.
	 */
	p_aad = aes->src;
	p_inp = scatterwalk_ffwd(sg_inp, aes->src, aes->aad_len);
	p_outp = scatterwalk_ffwd(sg_outp, aes->dst, aes->aad_len);
	if (aes->action == CCP_AES_ACTION_ENCRYPT) {
		ilen = aes->src_len;
		p_tag = scatterwalk_ffwd(sg_tag, p_outp, ilen);
	} else {
		/* Input length for decryption includes tag */
		ilen = aes->src_len - authsize;
		p_tag = scatterwalk_ffwd(sg_tag, p_inp, ilen);
	}

	jobid = CCP_NEW_JOBID(cmd_q->ccp);

	memset(&op, 0, sizeof(op));
	op.cmd_q = cmd_q;
	op.jobid = jobid;
	op.sb_key = cmd_q->sb_key; /* Pre-allocated */
	op.sb_ctx = cmd_q->sb_ctx; /* Pre-allocated */
	op.init = 1;
	op.u.aes.type = aes->type;

	/* Copy the key to the LSB */
	ret = ccp_init_dm_workarea(&key, cmd_q,
				   CCP_AES_CTX_SB_COUNT * CCP_SB_BYTES,
				   DMA_TO_DEVICE);
	if (ret)
		return ret;

	dm_offset = CCP_SB_BYTES - aes->key_len;
	ret = ccp_set_dm_area(&key, dm_offset, aes->key, 0, aes->key_len);
	if (ret)
		goto e_key;
	ret = ccp_copy_to_sb(cmd_q, &key, op.jobid, op.sb_key,
			     CCP_PASSTHRU_BYTESWAP_256BIT);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_key;
	}

	/* Copy the context (IV) to the LSB.
	 * There is an assumption here that the IV is 96 bits in length, plus
	 * a nonce of 32 bits. If no IV is present, use a zeroed buffer.
	 */
	ret = ccp_init_dm_workarea(&ctx, cmd_q,
				   CCP_AES_CTX_SB_COUNT * CCP_SB_BYTES,
				   DMA_BIDIRECTIONAL);
	if (ret)
		goto e_key;

	dm_offset = CCP_AES_CTX_SB_COUNT * CCP_SB_BYTES - aes->iv_len;
	ret = ccp_set_dm_area(&ctx, dm_offset, aes->iv, 0, aes->iv_len);
	if (ret)
		goto e_ctx;

	ret = ccp_copy_to_sb(cmd_q, &ctx, op.jobid, op.sb_ctx,
			     CCP_PASSTHRU_BYTESWAP_256BIT);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_ctx;
	}

	op.init = 1;
	if (aes->aad_len > 0) {
		/* Step 1: Run a GHASH over the Additional Authenticated Data */
		ret = ccp_init_data(&aad, cmd_q, p_aad, aes->aad_len,
				    AES_BLOCK_SIZE,
				    DMA_TO_DEVICE);
		if (ret)
			goto e_ctx;

		op.u.aes.mode = CCP_AES_MODE_GHASH;
		op.u.aes.action = CCP_AES_GHASHAAD;

		while (aad.sg_wa.bytes_left) {
			ccp_prepare_data(&aad, NULL, &op, AES_BLOCK_SIZE, true);

			ret = cmd_q->ccp->vdata->perform->aes(&op);
			if (ret) {
				cmd->engine_error = cmd_q->cmd_error;
				goto e_aad;
			}

			ccp_process_data(&aad, NULL, &op);
			op.init = 0;
		}
	}

	op.u.aes.mode = CCP_AES_MODE_GCTR;
	op.u.aes.action = aes->action;

	if (ilen > 0) {
		/* Step 2: Run a GCTR over the plaintext */
		in_place = (sg_virt(p_inp) == sg_virt(p_outp)) ? true : false;

		ret = ccp_init_data(&src, cmd_q, p_inp, ilen,
				    AES_BLOCK_SIZE,
				    in_place ? DMA_BIDIRECTIONAL
					     : DMA_TO_DEVICE);
		if (ret)
			goto e_ctx;

		if (in_place) {
			dst = src;
		} else {
			ret = ccp_init_data(&dst, cmd_q, p_outp, ilen,
					    AES_BLOCK_SIZE, DMA_FROM_DEVICE);
			if (ret)
				goto e_src;
		}

		op.soc = 0;
		op.eom = 0;
		op.init = 1;
		while (src.sg_wa.bytes_left) {
			ccp_prepare_data(&src, &dst, &op, AES_BLOCK_SIZE, true);
			if (!src.sg_wa.bytes_left) {
				unsigned int nbytes = ilen % AES_BLOCK_SIZE;

				if (nbytes) {
					op.eom = 1;
					op.u.aes.size = (nbytes * 8) - 1;
				}
			}

			ret = cmd_q->ccp->vdata->perform->aes(&op);
			if (ret) {
				cmd->engine_error = cmd_q->cmd_error;
				goto e_dst;
			}

			ccp_process_data(&src, &dst, &op);
			op.init = 0;
		}
	}

	/* Step 3: Update the IV portion of the context with the original IV */
	ret = ccp_copy_from_sb(cmd_q, &ctx, op.jobid, op.sb_ctx,
			       CCP_PASSTHRU_BYTESWAP_256BIT);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_dst;
	}

	ret = ccp_set_dm_area(&ctx, dm_offset, aes->iv, 0, aes->iv_len);
	if (ret)
		goto e_dst;

	ret = ccp_copy_to_sb(cmd_q, &ctx, op.jobid, op.sb_ctx,
			     CCP_PASSTHRU_BYTESWAP_256BIT);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_dst;
	}

	/* Step 4: Concatenate the lengths of the AAD and source, and
	 * hash that 16 byte buffer.
	 */
	ret = ccp_init_dm_workarea(&final_wa, cmd_q, AES_BLOCK_SIZE,
				   DMA_BIDIRECTIONAL);
	if (ret)
		goto e_dst;
	final = (unsigned long long *) final_wa.address;
	final[0] = cpu_to_be64(aes->aad_len * 8);
	final[1] = cpu_to_be64(ilen * 8);

	memset(&op, 0, sizeof(op));
	op.cmd_q = cmd_q;
	op.jobid = jobid;
	op.sb_key = cmd_q->sb_key; /* Pre-allocated */
	op.sb_ctx = cmd_q->sb_ctx; /* Pre-allocated */
	op.init = 1;
	op.u.aes.type = aes->type;
	op.u.aes.mode = CCP_AES_MODE_GHASH;
	op.u.aes.action = CCP_AES_GHASHFINAL;
	op.src.type = CCP_MEMTYPE_SYSTEM;
	op.src.u.dma.address = final_wa.dma.address;
	op.src.u.dma.length = AES_BLOCK_SIZE;
	op.dst.type = CCP_MEMTYPE_SYSTEM;
	op.dst.u.dma.address = final_wa.dma.address;
	op.dst.u.dma.length = AES_BLOCK_SIZE;
	op.eom = 1;
	op.u.aes.size = 0;
	ret = cmd_q->ccp->vdata->perform->aes(&op);
	if (ret)
		goto e_dst;

	if (aes->action == CCP_AES_ACTION_ENCRYPT) {
		/* Put the ciphered tag after the ciphertext. */
		ccp_get_dm_area(&final_wa, 0, p_tag, 0, authsize);
	} else {
		/* Does this ciphered tag match the input? */
		ret = ccp_init_dm_workarea(&tag, cmd_q, authsize,
					   DMA_BIDIRECTIONAL);
		if (ret)
			goto e_tag;
		ret = ccp_set_dm_area(&tag, 0, p_tag, 0, authsize);
		if (ret)
			goto e_tag;

		ret = crypto_memneq(tag.address, final_wa.address,
				    authsize) ? -EBADMSG : 0;
		ccp_dm_free(&tag);
	}

e_tag:
	ccp_dm_free(&final_wa);

e_dst:
	if (ilen > 0 && !in_place)
		ccp_free_data(&dst, cmd_q);

e_src:
	if (ilen > 0)
		ccp_free_data(&src, cmd_q);

e_aad:
	if (aes->aad_len)
		ccp_free_data(&aad, cmd_q);

e_ctx:
	ccp_dm_free(&ctx);

e_key:
	ccp_dm_free(&key);

	return ret;
}

static noinline_for_stack int
ccp_run_aes_cmd(struct ccp_cmd_queue *cmd_q, struct ccp_cmd *cmd)
{
	struct ccp_aes_engine *aes = &cmd->u.aes;
	struct ccp_dm_workarea key, ctx;
	struct ccp_data src, dst;
	struct ccp_op op;
	unsigned int dm_offset;
	bool in_place = false;
	int ret;

	if (!((aes->key_len == AES_KEYSIZE_128) ||
	      (aes->key_len == AES_KEYSIZE_192) ||
	      (aes->key_len == AES_KEYSIZE_256)))
		return -EINVAL;

	if (((aes->mode == CCP_AES_MODE_ECB) ||
	     (aes->mode == CCP_AES_MODE_CBC) ||
	     (aes->mode == CCP_AES_MODE_CFB)) &&
	    (aes->src_len & (AES_BLOCK_SIZE - 1)))
		return -EINVAL;

	if (!aes->key || !aes->src || !aes->dst)
		return -EINVAL;

	if (aes->mode != CCP_AES_MODE_ECB) {
		if (aes->iv_len != AES_BLOCK_SIZE)
			return -EINVAL;

		if (!aes->iv)
			return -EINVAL;
	}

	BUILD_BUG_ON(CCP_AES_KEY_SB_COUNT != 1);
	BUILD_BUG_ON(CCP_AES_CTX_SB_COUNT != 1);

	ret = -EIO;
	memset(&op, 0, sizeof(op));
	op.cmd_q = cmd_q;
	op.jobid = CCP_NEW_JOBID(cmd_q->ccp);
	op.sb_key = cmd_q->sb_key;
	op.sb_ctx = cmd_q->sb_ctx;
	op.init = (aes->mode == CCP_AES_MODE_ECB) ? 0 : 1;
	op.u.aes.type = aes->type;
	op.u.aes.mode = aes->mode;
	op.u.aes.action = aes->action;

	/* All supported key sizes fit in a single (32-byte) SB entry
	 * and must be in little endian format. Use the 256-bit byte
	 * swap passthru option to convert from big endian to little
	 * endian.
	 */
	ret = ccp_init_dm_workarea(&key, cmd_q,
				   CCP_AES_KEY_SB_COUNT * CCP_SB_BYTES,
				   DMA_TO_DEVICE);
	if (ret)
		return ret;

	dm_offset = CCP_SB_BYTES - aes->key_len;
	ret = ccp_set_dm_area(&key, dm_offset, aes->key, 0, aes->key_len);
	if (ret)
		goto e_key;
	ret = ccp_copy_to_sb(cmd_q, &key, op.jobid, op.sb_key,
			     CCP_PASSTHRU_BYTESWAP_256BIT);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_key;
	}

	/* The AES context fits in a single (32-byte) SB entry and
	 * must be in little endian format. Use the 256-bit byte swap
	 * passthru option to convert from big endian to little endian.
	 */
	ret = ccp_init_dm_workarea(&ctx, cmd_q,
				   CCP_AES_CTX_SB_COUNT * CCP_SB_BYTES,
				   DMA_BIDIRECTIONAL);
	if (ret)
		goto e_key;

	if (aes->mode != CCP_AES_MODE_ECB) {
		/* Load the AES context - convert to LE */
		dm_offset = CCP_SB_BYTES - AES_BLOCK_SIZE;
		ret = ccp_set_dm_area(&ctx, dm_offset, aes->iv, 0, aes->iv_len);
		if (ret)
			goto e_ctx;
		ret = ccp_copy_to_sb(cmd_q, &ctx, op.jobid, op.sb_ctx,
				     CCP_PASSTHRU_BYTESWAP_256BIT);
		if (ret) {
			cmd->engine_error = cmd_q->cmd_error;
			goto e_ctx;
		}
	}
	switch (aes->mode) {
	case CCP_AES_MODE_CFB: /* CFB128 only */
	case CCP_AES_MODE_CTR:
		op.u.aes.size = AES_BLOCK_SIZE * BITS_PER_BYTE - 1;
		break;
	default:
		op.u.aes.size = 0;
	}

	/* Prepare the input and output data workareas. For in-place
	 * operations we need to set the dma direction to BIDIRECTIONAL
	 * and copy the src workarea to the dst workarea.
	 */
	if (sg_virt(aes->src) == sg_virt(aes->dst))
		in_place = true;

	ret = ccp_init_data(&src, cmd_q, aes->src, aes->src_len,
			    AES_BLOCK_SIZE,
			    in_place ? DMA_BIDIRECTIONAL : DMA_TO_DEVICE);
	if (ret)
		goto e_ctx;

	if (in_place) {
		dst = src;
	} else {
		ret = ccp_init_data(&dst, cmd_q, aes->dst, aes->src_len,
				    AES_BLOCK_SIZE, DMA_FROM_DEVICE);
		if (ret)
			goto e_src;
	}

	/* Send data to the CCP AES engine */
	while (src.sg_wa.bytes_left) {
		ccp_prepare_data(&src, &dst, &op, AES_BLOCK_SIZE, true);
		if (!src.sg_wa.bytes_left) {
			op.eom = 1;

			/* Since we don't retrieve the AES context in ECB
			 * mode we have to wait for the operation to complete
			 * on the last piece of data
			 */
			if (aes->mode == CCP_AES_MODE_ECB)
				op.soc = 1;
		}

		ret = cmd_q->ccp->vdata->perform->aes(&op);
		if (ret) {
			cmd->engine_error = cmd_q->cmd_error;
			goto e_dst;
		}

		ccp_process_data(&src, &dst, &op);
	}

	if (aes->mode != CCP_AES_MODE_ECB) {
		/* Retrieve the AES context - convert from LE to BE using
		 * 32-byte (256-bit) byteswapping
		 */
		ret = ccp_copy_from_sb(cmd_q, &ctx, op.jobid, op.sb_ctx,
				       CCP_PASSTHRU_BYTESWAP_256BIT);
		if (ret) {
			cmd->engine_error = cmd_q->cmd_error;
			goto e_dst;
		}

		/* ...but we only need AES_BLOCK_SIZE bytes */
		dm_offset = CCP_SB_BYTES - AES_BLOCK_SIZE;
		ccp_get_dm_area(&ctx, dm_offset, aes->iv, 0, aes->iv_len);
	}

e_dst:
	if (!in_place)
		ccp_free_data(&dst, cmd_q);

e_src:
	ccp_free_data(&src, cmd_q);

e_ctx:
	ccp_dm_free(&ctx);

e_key:
	ccp_dm_free(&key);

	return ret;
}

static noinline_for_stack int
ccp_run_xts_aes_cmd(struct ccp_cmd_queue *cmd_q, struct ccp_cmd *cmd)
{
	struct ccp_xts_aes_engine *xts = &cmd->u.xts;
	struct ccp_dm_workarea key, ctx;
	struct ccp_data src, dst;
	struct ccp_op op;
	unsigned int unit_size, dm_offset;
	bool in_place = false;
	unsigned int sb_count;
	enum ccp_aes_type aestype;
	int ret;

	switch (xts->unit_size) {
	case CCP_XTS_AES_UNIT_SIZE_16:
		unit_size = 16;
		break;
	case CCP_XTS_AES_UNIT_SIZE_512:
		unit_size = 512;
		break;
	case CCP_XTS_AES_UNIT_SIZE_1024:
		unit_size = 1024;
		break;
	case CCP_XTS_AES_UNIT_SIZE_2048:
		unit_size = 2048;
		break;
	case CCP_XTS_AES_UNIT_SIZE_4096:
		unit_size = 4096;
		break;

	default:
		return -EINVAL;
	}

	if (xts->key_len == AES_KEYSIZE_128)
		aestype = CCP_AES_TYPE_128;
	else if (xts->key_len == AES_KEYSIZE_256)
		aestype = CCP_AES_TYPE_256;
	else
		return -EINVAL;

	if (!xts->final && (xts->src_len & (AES_BLOCK_SIZE - 1)))
		return -EINVAL;

	if (xts->iv_len != AES_BLOCK_SIZE)
		return -EINVAL;

	if (!xts->key || !xts->iv || !xts->src || !xts->dst)
		return -EINVAL;

	BUILD_BUG_ON(CCP_XTS_AES_KEY_SB_COUNT != 1);
	BUILD_BUG_ON(CCP_XTS_AES_CTX_SB_COUNT != 1);

	ret = -EIO;
	memset(&op, 0, sizeof(op));
	op.cmd_q = cmd_q;
	op.jobid = CCP_NEW_JOBID(cmd_q->ccp);
	op.sb_key = cmd_q->sb_key;
	op.sb_ctx = cmd_q->sb_ctx;
	op.init = 1;
	op.u.xts.type = aestype;
	op.u.xts.action = xts->action;
	op.u.xts.unit_size = xts->unit_size;

	/* A version 3 device only supports 128-bit keys, which fits into a
	 * single SB entry. A version 5 device uses a 512-bit vector, so two
	 * SB entries.
	 */
	if (cmd_q->ccp->vdata->version == CCP_VERSION(3, 0))
		sb_count = CCP_XTS_AES_KEY_SB_COUNT;
	else
		sb_count = CCP5_XTS_AES_KEY_SB_COUNT;
	ret = ccp_init_dm_workarea(&key, cmd_q,
				   sb_count * CCP_SB_BYTES,
				   DMA_TO_DEVICE);
	if (ret)
		return ret;

	if (cmd_q->ccp->vdata->version == CCP_VERSION(3, 0)) {
		/* All supported key sizes must be in little endian format.
		 * Use the 256-bit byte swap passthru option to convert from
		 * big endian to little endian.
		 */
		dm_offset = CCP_SB_BYTES - AES_KEYSIZE_128;
		ret = ccp_set_dm_area(&key, dm_offset, xts->key, 0, xts->key_len);
		if (ret)
			goto e_key;
		ret = ccp_set_dm_area(&key, 0, xts->key, xts->key_len, xts->key_len);
		if (ret)
			goto e_key;
	} else {
		/* Version 5 CCPs use a 512-bit space for the key: each portion
		 * occupies 256 bits, or one entire slot, and is zero-padded.
		 */
		unsigned int pad;

		dm_offset = CCP_SB_BYTES;
		pad = dm_offset - xts->key_len;
		ret = ccp_set_dm_area(&key, pad, xts->key, 0, xts->key_len);
		if (ret)
			goto e_key;
		ret = ccp_set_dm_area(&key, dm_offset + pad, xts->key,
				      xts->key_len, xts->key_len);
		if (ret)
			goto e_key;
	}
	ret = ccp_copy_to_sb(cmd_q, &key, op.jobid, op.sb_key,
			     CCP_PASSTHRU_BYTESWAP_256BIT);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_key;
	}

	/* The AES context fits in a single (32-byte) SB entry and
	 * for XTS is already in little endian format so no byte swapping
	 * is needed.
	 */
	ret = ccp_init_dm_workarea(&ctx, cmd_q,
				   CCP_XTS_AES_CTX_SB_COUNT * CCP_SB_BYTES,
				   DMA_BIDIRECTIONAL);
	if (ret)
		goto e_key;

	ret = ccp_set_dm_area(&ctx, 0, xts->iv, 0, xts->iv_len);
	if (ret)
		goto e_ctx;
	ret = ccp_copy_to_sb(cmd_q, &ctx, op.jobid, op.sb_ctx,
			     CCP_PASSTHRU_BYTESWAP_NOOP);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_ctx;
	}

	/* Prepare the input and output data workareas. For in-place
	 * operations we need to set the dma direction to BIDIRECTIONAL
	 * and copy the src workarea to the dst workarea.
	 */
	if (sg_virt(xts->src) == sg_virt(xts->dst))
		in_place = true;

	ret = ccp_init_data(&src, cmd_q, xts->src, xts->src_len,
			    unit_size,
			    in_place ? DMA_BIDIRECTIONAL : DMA_TO_DEVICE);
	if (ret)
		goto e_ctx;

	if (in_place) {
		dst = src;
	} else {
		ret = ccp_init_data(&dst, cmd_q, xts->dst, xts->src_len,
				    unit_size, DMA_FROM_DEVICE);
		if (ret)
			goto e_src;
	}

	/* Send data to the CCP AES engine */
	while (src.sg_wa.bytes_left) {
		ccp_prepare_data(&src, &dst, &op, unit_size, true);
		if (!src.sg_wa.bytes_left)
			op.eom = 1;

		ret = cmd_q->ccp->vdata->perform->xts_aes(&op);
		if (ret) {
			cmd->engine_error = cmd_q->cmd_error;
			goto e_dst;
		}

		ccp_process_data(&src, &dst, &op);
	}

	/* Retrieve the AES context - convert from LE to BE using
	 * 32-byte (256-bit) byteswapping
	 */
	ret = ccp_copy_from_sb(cmd_q, &ctx, op.jobid, op.sb_ctx,
			       CCP_PASSTHRU_BYTESWAP_256BIT);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_dst;
	}

	/* ...but we only need AES_BLOCK_SIZE bytes */
	dm_offset = CCP_SB_BYTES - AES_BLOCK_SIZE;
	ccp_get_dm_area(&ctx, dm_offset, xts->iv, 0, xts->iv_len);

e_dst:
	if (!in_place)
		ccp_free_data(&dst, cmd_q);

e_src:
	ccp_free_data(&src, cmd_q);

e_ctx:
	ccp_dm_free(&ctx);

e_key:
	ccp_dm_free(&key);

	return ret;
}

static noinline_for_stack int
ccp_run_des3_cmd(struct ccp_cmd_queue *cmd_q, struct ccp_cmd *cmd)
{
	struct ccp_des3_engine *des3 = &cmd->u.des3;

	struct ccp_dm_workarea key, ctx;
	struct ccp_data src, dst;
	struct ccp_op op;
	unsigned int dm_offset;
	unsigned int len_singlekey;
	bool in_place = false;
	int ret;

	/* Error checks */
	if (cmd_q->ccp->vdata->version < CCP_VERSION(5, 0))
		return -EINVAL;

	if (!cmd_q->ccp->vdata->perform->des3)
		return -EINVAL;

	if (des3->key_len != DES3_EDE_KEY_SIZE)
		return -EINVAL;

	if (((des3->mode == CCP_DES3_MODE_ECB) ||
		(des3->mode == CCP_DES3_MODE_CBC)) &&
		(des3->src_len & (DES3_EDE_BLOCK_SIZE - 1)))
		return -EINVAL;

	if (!des3->key || !des3->src || !des3->dst)
		return -EINVAL;

	if (des3->mode != CCP_DES3_MODE_ECB) {
		if (des3->iv_len != DES3_EDE_BLOCK_SIZE)
			return -EINVAL;

		if (!des3->iv)
			return -EINVAL;
	}

	ret = -EIO;
	/* Zero out all the fields of the command desc */
	memset(&op, 0, sizeof(op));

	/* Set up the Function field */
	op.cmd_q = cmd_q;
	op.jobid = CCP_NEW_JOBID(cmd_q->ccp);
	op.sb_key = cmd_q->sb_key;

	op.init = (des3->mode == CCP_DES3_MODE_ECB) ? 0 : 1;
	op.u.des3.type = des3->type;
	op.u.des3.mode = des3->mode;
	op.u.des3.action = des3->action;

	/*
	 * All supported key sizes fit in a single (32-byte) KSB entry and
	 * (like AES) must be in little endian format. Use the 256-bit byte
	 * swap passthru option to convert from big endian to little endian.
	 */
	ret = ccp_init_dm_workarea(&key, cmd_q,
				   CCP_DES3_KEY_SB_COUNT * CCP_SB_BYTES,
				   DMA_TO_DEVICE);
	if (ret)
		return ret;

	/*
	 * The contents of the key triplet are in the reverse order of what
	 * is required by the engine. Copy the 3 pieces individually to put
	 * them where they belong.
	 */
	dm_offset = CCP_SB_BYTES - des3->key_len; /* Basic offset */

	len_singlekey = des3->key_len / 3;
	ret = ccp_set_dm_area(&key, dm_offset + 2 * len_singlekey,
			      des3->key, 0, len_singlekey);
	if (ret)
		goto e_key;
	ret = ccp_set_dm_area(&key, dm_offset + len_singlekey,
			      des3->key, len_singlekey, len_singlekey);
	if (ret)
		goto e_key;
	ret = ccp_set_dm_area(&key, dm_offset,
			      des3->key, 2 * len_singlekey, len_singlekey);
	if (ret)
		goto e_key;

	/* Copy the key to the SB */
	ret = ccp_copy_to_sb(cmd_q, &key, op.jobid, op.sb_key,
			     CCP_PASSTHRU_BYTESWAP_256BIT);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_key;
	}

	/*
	 * The DES3 context fits in a single (32-byte) KSB entry and
	 * must be in little endian format. Use the 256-bit byte swap
	 * passthru option to convert from big endian to little endian.
	 */
	if (des3->mode != CCP_DES3_MODE_ECB) {
		op.sb_ctx = cmd_q->sb_ctx;

		ret = ccp_init_dm_workarea(&ctx, cmd_q,
					   CCP_DES3_CTX_SB_COUNT * CCP_SB_BYTES,
					   DMA_BIDIRECTIONAL);
		if (ret)
			goto e_key;

		/* Load the context into the LSB */
		dm_offset = CCP_SB_BYTES - des3->iv_len;
		ret = ccp_set_dm_area(&ctx, dm_offset, des3->iv, 0,
				      des3->iv_len);
		if (ret)
			goto e_ctx;

		ret = ccp_copy_to_sb(cmd_q, &ctx, op.jobid, op.sb_ctx,
				     CCP_PASSTHRU_BYTESWAP_256BIT);
		if (ret) {
			cmd->engine_error = cmd_q->cmd_error;
			goto e_ctx;
		}
	}

	/*
	 * Prepare the input and output data workareas. For in-place
	 * operations we need to set the dma direction to BIDIRECTIONAL
	 * and copy the src workarea to the dst workarea.
	 */
	if (sg_virt(des3->src) == sg_virt(des3->dst))
		in_place = true;

	ret = ccp_init_data(&src, cmd_q, des3->src, des3->src_len,
			DES3_EDE_BLOCK_SIZE,
			in_place ? DMA_BIDIRECTIONAL : DMA_TO_DEVICE);
	if (ret)
		goto e_ctx;

	if (in_place)
		dst = src;
	else {
		ret = ccp_init_data(&dst, cmd_q, des3->dst, des3->src_len,
				DES3_EDE_BLOCK_SIZE, DMA_FROM_DEVICE);
		if (ret)
			goto e_src;
	}

	/* Send data to the CCP DES3 engine */
	while (src.sg_wa.bytes_left) {
		ccp_prepare_data(&src, &dst, &op, DES3_EDE_BLOCK_SIZE, true);
		if (!src.sg_wa.bytes_left) {
			op.eom = 1;

			/* Since we don't retrieve the context in ECB mode
			 * we have to wait for the operation to complete
			 * on the last piece of data
			 */
			op.soc = 0;
		}

		ret = cmd_q->ccp->vdata->perform->des3(&op);
		if (ret) {
			cmd->engine_error = cmd_q->cmd_error;
			goto e_dst;
		}

		ccp_process_data(&src, &dst, &op);
	}

	if (des3->mode != CCP_DES3_MODE_ECB) {
		/* Retrieve the context and make BE */
		ret = ccp_copy_from_sb(cmd_q, &ctx, op.jobid, op.sb_ctx,
				       CCP_PASSTHRU_BYTESWAP_256BIT);
		if (ret) {
			cmd->engine_error = cmd_q->cmd_error;
			goto e_dst;
		}

		/* ...but we only need the last DES3_EDE_BLOCK_SIZE bytes */
		ccp_get_dm_area(&ctx, dm_offset, des3->iv, 0,
				DES3_EDE_BLOCK_SIZE);
	}
e_dst:
	if (!in_place)
		ccp_free_data(&dst, cmd_q);

e_src:
	ccp_free_data(&src, cmd_q);

e_ctx:
	if (des3->mode != CCP_DES3_MODE_ECB)
		ccp_dm_free(&ctx);

e_key:
	ccp_dm_free(&key);

	return ret;
}

static noinline_for_stack int
ccp_run_sha_cmd(struct ccp_cmd_queue *cmd_q, struct ccp_cmd *cmd)
{
	struct ccp_sha_engine *sha = &cmd->u.sha;
	struct ccp_dm_workarea ctx;
	struct ccp_data src;
	struct ccp_op op;
	unsigned int ioffset, ooffset;
	unsigned int digest_size;
	int sb_count;
	const void *init;
	u64 block_size;
	int ctx_size;
	int ret;

	switch (sha->type) {
	case CCP_SHA_TYPE_1:
		if (sha->ctx_len < SHA1_DIGEST_SIZE)
			return -EINVAL;
		block_size = SHA1_BLOCK_SIZE;
		break;
	case CCP_SHA_TYPE_224:
		if (sha->ctx_len < SHA224_DIGEST_SIZE)
			return -EINVAL;
		block_size = SHA224_BLOCK_SIZE;
		break;
	case CCP_SHA_TYPE_256:
		if (sha->ctx_len < SHA256_DIGEST_SIZE)
			return -EINVAL;
		block_size = SHA256_BLOCK_SIZE;
		break;
	case CCP_SHA_TYPE_384:
		if (cmd_q->ccp->vdata->version < CCP_VERSION(4, 0)
		    || sha->ctx_len < SHA384_DIGEST_SIZE)
			return -EINVAL;
		block_size = SHA384_BLOCK_SIZE;
		break;
	case CCP_SHA_TYPE_512:
		if (cmd_q->ccp->vdata->version < CCP_VERSION(4, 0)
		    || sha->ctx_len < SHA512_DIGEST_SIZE)
			return -EINVAL;
		block_size = SHA512_BLOCK_SIZE;
		break;
	default:
		return -EINVAL;
	}

	if (!sha->ctx)
		return -EINVAL;

	if (!sha->final && (sha->src_len & (block_size - 1)))
		return -EINVAL;

	/* The version 3 device can't handle zero-length input */
	if (cmd_q->ccp->vdata->version == CCP_VERSION(3, 0)) {

		if (!sha->src_len) {
			unsigned int digest_len;
			const u8 *sha_zero;

			/* Not final, just return */
			if (!sha->final)
				return 0;

			/* CCP can't do a zero length sha operation so the
			 * caller must buffer the data.
			 */
			if (sha->msg_bits)
				return -EINVAL;

			/* The CCP cannot perform zero-length sha operations
			 * so the caller is required to buffer data for the
			 * final operation. However, a sha operation for a
			 * message with a total length of zero is valid so
			 * known values are required to supply the result.
			 */
			switch (sha->type) {
			case CCP_SHA_TYPE_1:
				sha_zero = sha1_zero_message_hash;
				digest_len = SHA1_DIGEST_SIZE;
				break;
			case CCP_SHA_TYPE_224:
				sha_zero = sha224_zero_message_hash;
				digest_len = SHA224_DIGEST_SIZE;
				break;
			case CCP_SHA_TYPE_256:
				sha_zero = sha256_zero_message_hash;
				digest_len = SHA256_DIGEST_SIZE;
				break;
			default:
				return -EINVAL;
			}

			scatterwalk_map_and_copy((void *)sha_zero, sha->ctx, 0,
						 digest_len, 1);

			return 0;
		}
	}

	/* Set variables used throughout */
	switch (sha->type) {
	case CCP_SHA_TYPE_1:
		digest_size = SHA1_DIGEST_SIZE;
		init = (void *) ccp_sha1_init;
		ctx_size = SHA1_DIGEST_SIZE;
		sb_count = 1;
		if (cmd_q->ccp->vdata->version != CCP_VERSION(3, 0))
			ooffset = ioffset = CCP_SB_BYTES - SHA1_DIGEST_SIZE;
		else
			ooffset = ioffset = 0;
		break;
	case CCP_SHA_TYPE_224:
		digest_size = SHA224_DIGEST_SIZE;
		init = (void *) ccp_sha224_init;
		ctx_size = SHA256_DIGEST_SIZE;
		sb_count = 1;
		ioffset = 0;
		if (cmd_q->ccp->vdata->version != CCP_VERSION(3, 0))
			ooffset = CCP_SB_BYTES - SHA224_DIGEST_SIZE;
		else
			ooffset = 0;
		break;
	case CCP_SHA_TYPE_256:
		digest_size = SHA256_DIGEST_SIZE;
		init = (void *) ccp_sha256_init;
		ctx_size = SHA256_DIGEST_SIZE;
		sb_count = 1;
		ooffset = ioffset = 0;
		break;
	case CCP_SHA_TYPE_384:
		digest_size = SHA384_DIGEST_SIZE;
		init = (void *) ccp_sha384_init;
		ctx_size = SHA512_DIGEST_SIZE;
		sb_count = 2;
		ioffset = 0;
		ooffset = 2 * CCP_SB_BYTES - SHA384_DIGEST_SIZE;
		break;
	case CCP_SHA_TYPE_512:
		digest_size = SHA512_DIGEST_SIZE;
		init = (void *) ccp_sha512_init;
		ctx_size = SHA512_DIGEST_SIZE;
		sb_count = 2;
		ooffset = ioffset = 0;
		break;
	default:
		ret = -EINVAL;
		goto e_data;
	}

	/* For zero-length plaintext the src pointer is ignored;
	 * otherwise both parts must be valid
	 */
	if (sha->src_len && !sha->src)
		return -EINVAL;

	memset(&op, 0, sizeof(op));
	op.cmd_q = cmd_q;
	op.jobid = CCP_NEW_JOBID(cmd_q->ccp);
	op.sb_ctx = cmd_q->sb_ctx; /* Pre-allocated */
	op.u.sha.type = sha->type;
	op.u.sha.msg_bits = sha->msg_bits;

	/* For SHA1/224/256 the context fits in a single (32-byte) SB entry;
	 * SHA384/512 require 2 adjacent SB slots, with the right half in the
	 * first slot, and the left half in the second. Each portion must then
	 * be in little endian format: use the 256-bit byte swap option.
	 */
	ret = ccp_init_dm_workarea(&ctx, cmd_q, sb_count * CCP_SB_BYTES,
				   DMA_BIDIRECTIONAL);
	if (ret)
		return ret;
	if (sha->first) {
		switch (sha->type) {
		case CCP_SHA_TYPE_1:
		case CCP_SHA_TYPE_224:
		case CCP_SHA_TYPE_256:
			memcpy(ctx.address + ioffset, init, ctx_size);
			break;
		case CCP_SHA_TYPE_384:
		case CCP_SHA_TYPE_512:
			memcpy(ctx.address + ctx_size / 2, init,
			       ctx_size / 2);
			memcpy(ctx.address, init + ctx_size / 2,
			       ctx_size / 2);
			break;
		default:
			ret = -EINVAL;
			goto e_ctx;
		}
	} else {
		/* Restore the context */
		ret = ccp_set_dm_area(&ctx, 0, sha->ctx, 0,
				      sb_count * CCP_SB_BYTES);
		if (ret)
			goto e_ctx;
	}

	ret = ccp_copy_to_sb(cmd_q, &ctx, op.jobid, op.sb_ctx,
			     CCP_PASSTHRU_BYTESWAP_256BIT);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_ctx;
	}

	if (sha->src) {
		/* Send data to the CCP SHA engine; block_size is set above */
		ret = ccp_init_data(&src, cmd_q, sha->src, sha->src_len,
				    block_size, DMA_TO_DEVICE);
		if (ret)
			goto e_ctx;

		while (src.sg_wa.bytes_left) {
			ccp_prepare_data(&src, NULL, &op, block_size, false);
			if (sha->final && !src.sg_wa.bytes_left)
				op.eom = 1;

			ret = cmd_q->ccp->vdata->perform->sha(&op);
			if (ret) {
				cmd->engine_error = cmd_q->cmd_error;
				goto e_data;
			}

			ccp_process_data(&src, NULL, &op);
		}
	} else {
		op.eom = 1;
		ret = cmd_q->ccp->vdata->perform->sha(&op);
		if (ret) {
			cmd->engine_error = cmd_q->cmd_error;
			goto e_data;
		}
	}

	/* Retrieve the SHA context - convert from LE to BE using
	 * 32-byte (256-bit) byteswapping to BE
	 */
	ret = ccp_copy_from_sb(cmd_q, &ctx, op.jobid, op.sb_ctx,
			       CCP_PASSTHRU_BYTESWAP_256BIT);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_data;
	}

	if (sha->final) {
		/* Finishing up, so get the digest */
		switch (sha->type) {
		case CCP_SHA_TYPE_1:
		case CCP_SHA_TYPE_224:
		case CCP_SHA_TYPE_256:
			ccp_get_dm_area(&ctx, ooffset,
					sha->ctx, 0,
					digest_size);
			break;
		case CCP_SHA_TYPE_384:
		case CCP_SHA_TYPE_512:
			ccp_get_dm_area(&ctx, 0,
					sha->ctx, LSB_ITEM_SIZE - ooffset,
					LSB_ITEM_SIZE);
			ccp_get_dm_area(&ctx, LSB_ITEM_SIZE + ooffset,
					sha->ctx, 0,
					LSB_ITEM_SIZE - ooffset);
			break;
		default:
			ret = -EINVAL;
			goto e_ctx;
		}
	} else {
		/* Stash the context */
		ccp_get_dm_area(&ctx, 0, sha->ctx, 0,
				sb_count * CCP_SB_BYTES);
	}

	if (sha->final && sha->opad) {
		/* HMAC operation, recursively perform final SHA */
		struct ccp_cmd hmac_cmd;
		struct scatterlist sg;
		u8 *hmac_buf;

		if (sha->opad_len != block_size) {
			ret = -EINVAL;
			goto e_data;
		}

		hmac_buf = kmalloc(block_size + digest_size, GFP_KERNEL);
		if (!hmac_buf) {
			ret = -ENOMEM;
			goto e_data;
		}
		sg_init_one(&sg, hmac_buf, block_size + digest_size);

		scatterwalk_map_and_copy(hmac_buf, sha->opad, 0, block_size, 0);
		switch (sha->type) {
		case CCP_SHA_TYPE_1:
		case CCP_SHA_TYPE_224:
		case CCP_SHA_TYPE_256:
			memcpy(hmac_buf + block_size,
			       ctx.address + ooffset,
			       digest_size);
			break;
		case CCP_SHA_TYPE_384:
		case CCP_SHA_TYPE_512:
			memcpy(hmac_buf + block_size,
			       ctx.address + LSB_ITEM_SIZE + ooffset,
			       LSB_ITEM_SIZE);
			memcpy(hmac_buf + block_size +
			       (LSB_ITEM_SIZE - ooffset),
			       ctx.address,
			       LSB_ITEM_SIZE);
			break;
		default:
			kfree(hmac_buf);
			ret = -EINVAL;
			goto e_data;
		}

		memset(&hmac_cmd, 0, sizeof(hmac_cmd));
		hmac_cmd.engine = CCP_ENGINE_SHA;
		hmac_cmd.u.sha.type = sha->type;
		hmac_cmd.u.sha.ctx = sha->ctx;
		hmac_cmd.u.sha.ctx_len = sha->ctx_len;
		hmac_cmd.u.sha.src = &sg;
		hmac_cmd.u.sha.src_len = block_size + digest_size;
		hmac_cmd.u.sha.opad = NULL;
		hmac_cmd.u.sha.opad_len = 0;
		hmac_cmd.u.sha.first = 1;
		hmac_cmd.u.sha.final = 1;
		hmac_cmd.u.sha.msg_bits = (block_size + digest_size) << 3;

		ret = ccp_run_sha_cmd(cmd_q, &hmac_cmd);
		if (ret)
			cmd->engine_error = hmac_cmd.engine_error;

		kfree(hmac_buf);
	}

e_data:
	if (sha->src)
		ccp_free_data(&src, cmd_q);

e_ctx:
	ccp_dm_free(&ctx);

	return ret;
}

static noinline_for_stack int
ccp_run_rsa_cmd(struct ccp_cmd_queue *cmd_q, struct ccp_cmd *cmd)
{
	struct ccp_rsa_engine *rsa = &cmd->u.rsa;
	struct ccp_dm_workarea exp, src, dst;
	struct ccp_op op;
	unsigned int sb_count, i_len, o_len;
	int ret;

	/* Check against the maximum allowable size, in bits */
	if (rsa->key_size > cmd_q->ccp->vdata->rsamax)
		return -EINVAL;

	if (!rsa->exp || !rsa->mod || !rsa->src || !rsa->dst)
		return -EINVAL;

	memset(&op, 0, sizeof(op));
	op.cmd_q = cmd_q;
	op.jobid = CCP_NEW_JOBID(cmd_q->ccp);

	/* The RSA modulus must precede the message being acted upon, so
	 * it must be copied to a DMA area where the message and the
	 * modulus can be concatenated.  Therefore the input buffer
	 * length required is twice the output buffer length (which
	 * must be a multiple of 256-bits).  Compute o_len, i_len in bytes.
	 * Buffer sizes must be a multiple of 32 bytes; rounding up may be
	 * required.
	 */
	o_len = 32 * ((rsa->key_size + 255) / 256);
	i_len = o_len * 2;

	sb_count = 0;
	if (cmd_q->ccp->vdata->version < CCP_VERSION(5, 0)) {
		/* sb_count is the number of storage block slots required
		 * for the modulus.
		 */
		sb_count = o_len / CCP_SB_BYTES;
		op.sb_key = cmd_q->ccp->vdata->perform->sballoc(cmd_q,
								sb_count);
		if (!op.sb_key)
			return -EIO;
	} else {
		/* A version 5 device allows a modulus size that will not fit
		 * in the LSB, so the command will transfer it from memory.
		 * Set the sb key to the default, even though it's not used.
		 */
		op.sb_key = cmd_q->sb_key;
	}

	/* The RSA exponent must be in little endian format. Reverse its
	 * byte order.
	 */
	ret = ccp_init_dm_workarea(&exp, cmd_q, o_len, DMA_TO_DEVICE);
	if (ret)
		goto e_sb;

	ret = ccp_reverse_set_dm_area(&exp, 0, rsa->exp, 0, rsa->exp_len);
	if (ret)
		goto e_exp;

	if (cmd_q->ccp->vdata->version < CCP_VERSION(5, 0)) {
		/* Copy the exponent to the local storage block, using
		 * as many 32-byte blocks as were allocated above. It's
		 * already little endian, so no further change is required.
		 */
		ret = ccp_copy_to_sb(cmd_q, &exp, op.jobid, op.sb_key,
				     CCP_PASSTHRU_BYTESWAP_NOOP);
		if (ret) {
			cmd->engine_error = cmd_q->cmd_error;
			goto e_exp;
		}
	} else {
		/* The exponent can be retrieved from memory via DMA. */
		op.exp.u.dma.address = exp.dma.address;
		op.exp.u.dma.offset = 0;
	}

	/* Concatenate the modulus and the message. Both the modulus and
	 * the operands must be in little endian format.  Since the input
	 * is in big endian format it must be converted.
	 */
	ret = ccp_init_dm_workarea(&src, cmd_q, i_len, DMA_TO_DEVICE);
	if (ret)
		goto e_exp;

	ret = ccp_reverse_set_dm_area(&src, 0, rsa->mod, 0, rsa->mod_len);
	if (ret)
		goto e_src;
	ret = ccp_reverse_set_dm_area(&src, o_len, rsa->src, 0, rsa->src_len);
	if (ret)
		goto e_src;

	/* Prepare the output area for the operation */
	ret = ccp_init_dm_workarea(&dst, cmd_q, o_len, DMA_FROM_DEVICE);
	if (ret)
		goto e_src;

	op.soc = 1;
	op.src.u.dma.address = src.dma.address;
	op.src.u.dma.offset = 0;
	op.src.u.dma.length = i_len;
	op.dst.u.dma.address = dst.dma.address;
	op.dst.u.dma.offset = 0;
	op.dst.u.dma.length = o_len;

	op.u.rsa.mod_size = rsa->key_size;
	op.u.rsa.input_len = i_len;

	ret = cmd_q->ccp->vdata->perform->rsa(&op);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_dst;
	}

	ccp_reverse_get_dm_area(&dst, 0, rsa->dst, 0, rsa->mod_len);

e_dst:
	ccp_dm_free(&dst);

e_src:
	ccp_dm_free(&src);

e_exp:
	ccp_dm_free(&exp);

e_sb:
	if (sb_count)
		cmd_q->ccp->vdata->perform->sbfree(cmd_q, op.sb_key, sb_count);

	return ret;
}

static noinline_for_stack int
ccp_run_passthru_cmd(struct ccp_cmd_queue *cmd_q, struct ccp_cmd *cmd)
{
	struct ccp_passthru_engine *pt = &cmd->u.passthru;
	struct ccp_dm_workarea mask;
	struct ccp_data src, dst;
	struct ccp_op op;
	bool in_place = false;
	unsigned int i;
	int ret = 0;

	if (!pt->final && (pt->src_len & (CCP_PASSTHRU_BLOCKSIZE - 1)))
		return -EINVAL;

	if (!pt->src || !pt->dst)
		return -EINVAL;

	if (pt->bit_mod != CCP_PASSTHRU_BITWISE_NOOP) {
		if (pt->mask_len != CCP_PASSTHRU_MASKSIZE)
			return -EINVAL;
		if (!pt->mask)
			return -EINVAL;
	}

	BUILD_BUG_ON(CCP_PASSTHRU_SB_COUNT != 1);

	memset(&op, 0, sizeof(op));
	op.cmd_q = cmd_q;
	op.jobid = CCP_NEW_JOBID(cmd_q->ccp);

	if (pt->bit_mod != CCP_PASSTHRU_BITWISE_NOOP) {
		/* Load the mask */
		op.sb_key = cmd_q->sb_key;

		ret = ccp_init_dm_workarea(&mask, cmd_q,
					   CCP_PASSTHRU_SB_COUNT *
					   CCP_SB_BYTES,
					   DMA_TO_DEVICE);
		if (ret)
			return ret;

		ret = ccp_set_dm_area(&mask, 0, pt->mask, 0, pt->mask_len);
		if (ret)
			goto e_mask;
		ret = ccp_copy_to_sb(cmd_q, &mask, op.jobid, op.sb_key,
				     CCP_PASSTHRU_BYTESWAP_NOOP);
		if (ret) {
			cmd->engine_error = cmd_q->cmd_error;
			goto e_mask;
		}
	}

	/* Prepare the input and output data workareas. For in-place
	 * operations we need to set the dma direction to BIDIRECTIONAL
	 * and copy the src workarea to the dst workarea.
	 */
	if (sg_virt(pt->src) == sg_virt(pt->dst))
		in_place = true;

	ret = ccp_init_data(&src, cmd_q, pt->src, pt->src_len,
			    CCP_PASSTHRU_MASKSIZE,
			    in_place ? DMA_BIDIRECTIONAL : DMA_TO_DEVICE);
	if (ret)
		goto e_mask;

	if (in_place) {
		dst = src;
	} else {
		ret = ccp_init_data(&dst, cmd_q, pt->dst, pt->src_len,
				    CCP_PASSTHRU_MASKSIZE, DMA_FROM_DEVICE);
		if (ret)
			goto e_src;
	}

	/* Send data to the CCP Passthru engine
	 *   Because the CCP engine works on a single source and destination
	 *   dma address at a time, each entry in the source scatterlist
	 *   (after the dma_map_sg call) must be less than or equal to the
	 *   (remaining) length in the destination scatterlist entry and the
	 *   length must be a multiple of CCP_PASSTHRU_BLOCKSIZE
	 */
	dst.sg_wa.sg_used = 0;
	for (i = 1; i <= src.sg_wa.dma_count; i++) {
		if (!dst.sg_wa.sg ||
		    (sg_dma_len(dst.sg_wa.sg) < sg_dma_len(src.sg_wa.sg))) {
			ret = -EINVAL;
			goto e_dst;
		}

		if (i == src.sg_wa.dma_count) {
			op.eom = 1;
			op.soc = 1;
		}

		op.src.type = CCP_MEMTYPE_SYSTEM;
		op.src.u.dma.address = sg_dma_address(src.sg_wa.sg);
		op.src.u.dma.offset = 0;
		op.src.u.dma.length = sg_dma_len(src.sg_wa.sg);

		op.dst.type = CCP_MEMTYPE_SYSTEM;
		op.dst.u.dma.address = sg_dma_address(dst.sg_wa.sg);
		op.dst.u.dma.offset = dst.sg_wa.sg_used;
		op.dst.u.dma.length = op.src.u.dma.length;

		ret = cmd_q->ccp->vdata->perform->passthru(&op);
		if (ret) {
			cmd->engine_error = cmd_q->cmd_error;
			goto e_dst;
		}

		dst.sg_wa.sg_used += sg_dma_len(src.sg_wa.sg);
		if (dst.sg_wa.sg_used == sg_dma_len(dst.sg_wa.sg)) {
			dst.sg_wa.sg = sg_next(dst.sg_wa.sg);
			dst.sg_wa.sg_used = 0;
		}
		src.sg_wa.sg = sg_next(src.sg_wa.sg);
	}

e_dst:
	if (!in_place)
		ccp_free_data(&dst, cmd_q);

e_src:
	ccp_free_data(&src, cmd_q);

e_mask:
	if (pt->bit_mod != CCP_PASSTHRU_BITWISE_NOOP)
		ccp_dm_free(&mask);

	return ret;
}

static noinline_for_stack int
ccp_run_passthru_nomap_cmd(struct ccp_cmd_queue *cmd_q,
				      struct ccp_cmd *cmd)
{
	struct ccp_passthru_nomap_engine *pt = &cmd->u.passthru_nomap;
	struct ccp_dm_workarea mask;
	struct ccp_op op;
	int ret;

	if (!pt->final && (pt->src_len & (CCP_PASSTHRU_BLOCKSIZE - 1)))
		return -EINVAL;

	if (!pt->src_dma || !pt->dst_dma)
		return -EINVAL;

	if (pt->bit_mod != CCP_PASSTHRU_BITWISE_NOOP) {
		if (pt->mask_len != CCP_PASSTHRU_MASKSIZE)
			return -EINVAL;
		if (!pt->mask)
			return -EINVAL;
	}

	BUILD_BUG_ON(CCP_PASSTHRU_SB_COUNT != 1);

	memset(&op, 0, sizeof(op));
	op.cmd_q = cmd_q;
	op.jobid = CCP_NEW_JOBID(cmd_q->ccp);

	if (pt->bit_mod != CCP_PASSTHRU_BITWISE_NOOP) {
		/* Load the mask */
		op.sb_key = cmd_q->sb_key;

		mask.length = pt->mask_len;
		mask.dma.address = pt->mask;
		mask.dma.length = pt->mask_len;

		ret = ccp_copy_to_sb(cmd_q, &mask, op.jobid, op.sb_key,
				     CCP_PASSTHRU_BYTESWAP_NOOP);
		if (ret) {
			cmd->engine_error = cmd_q->cmd_error;
			return ret;
		}
	}

	/* Send data to the CCP Passthru engine */
	op.eom = 1;
	op.soc = 1;

	op.src.type = CCP_MEMTYPE_SYSTEM;
	op.src.u.dma.address = pt->src_dma;
	op.src.u.dma.offset = 0;
	op.src.u.dma.length = pt->src_len;

	op.dst.type = CCP_MEMTYPE_SYSTEM;
	op.dst.u.dma.address = pt->dst_dma;
	op.dst.u.dma.offset = 0;
	op.dst.u.dma.length = pt->src_len;

	ret = cmd_q->ccp->vdata->perform->passthru(&op);
	if (ret)
		cmd->engine_error = cmd_q->cmd_error;

	return ret;
}

static int ccp_run_ecc_mm_cmd(struct ccp_cmd_queue *cmd_q, struct ccp_cmd *cmd)
{
	struct ccp_ecc_engine *ecc = &cmd->u.ecc;
	struct ccp_dm_workarea src, dst;
	struct ccp_op op;
	int ret;
	u8 *save;

	if (!ecc->u.mm.operand_1 ||
	    (ecc->u.mm.operand_1_len > CCP_ECC_MODULUS_BYTES))
		return -EINVAL;

	if (ecc->function != CCP_ECC_FUNCTION_MINV_384BIT)
		if (!ecc->u.mm.operand_2 ||
		    (ecc->u.mm.operand_2_len > CCP_ECC_MODULUS_BYTES))
			return -EINVAL;

	if (!ecc->u.mm.result ||
	    (ecc->u.mm.result_len < CCP_ECC_MODULUS_BYTES))
		return -EINVAL;

	memset(&op, 0, sizeof(op));
	op.cmd_q = cmd_q;
	op.jobid = CCP_NEW_JOBID(cmd_q->ccp);

	/* Concatenate the modulus and the operands. Both the modulus and
	 * the operands must be in little endian format.  Since the input
	 * is in big endian format it must be converted and placed in a
	 * fixed length buffer.
	 */
	ret = ccp_init_dm_workarea(&src, cmd_q, CCP_ECC_SRC_BUF_SIZE,
				   DMA_TO_DEVICE);
	if (ret)
		return ret;

	/* Save the workarea address since it is updated in order to perform
	 * the concatenation
	 */
	save = src.address;

	/* Copy the ECC modulus */
	ret = ccp_reverse_set_dm_area(&src, 0, ecc->mod, 0, ecc->mod_len);
	if (ret)
		goto e_src;
	src.address += CCP_ECC_OPERAND_SIZE;

	/* Copy the first operand */
	ret = ccp_reverse_set_dm_area(&src, 0, ecc->u.mm.operand_1, 0,
				      ecc->u.mm.operand_1_len);
	if (ret)
		goto e_src;
	src.address += CCP_ECC_OPERAND_SIZE;

	if (ecc->function != CCP_ECC_FUNCTION_MINV_384BIT) {
		/* Copy the second operand */
		ret = ccp_reverse_set_dm_area(&src, 0, ecc->u.mm.operand_2, 0,
					      ecc->u.mm.operand_2_len);
		if (ret)
			goto e_src;
		src.address += CCP_ECC_OPERAND_SIZE;
	}

	/* Restore the workarea address */
	src.address = save;

	/* Prepare the output area for the operation */
	ret = ccp_init_dm_workarea(&dst, cmd_q, CCP_ECC_DST_BUF_SIZE,
				   DMA_FROM_DEVICE);
	if (ret)
		goto e_src;

	op.soc = 1;
	op.src.u.dma.address = src.dma.address;
	op.src.u.dma.offset = 0;
	op.src.u.dma.length = src.length;
	op.dst.u.dma.address = dst.dma.address;
	op.dst.u.dma.offset = 0;
	op.dst.u.dma.length = dst.length;

	op.u.ecc.function = cmd->u.ecc.function;

	ret = cmd_q->ccp->vdata->perform->ecc(&op);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_dst;
	}

	ecc->ecc_result = le16_to_cpup(
		(const __le16 *)(dst.address + CCP_ECC_RESULT_OFFSET));
	if (!(ecc->ecc_result & CCP_ECC_RESULT_SUCCESS)) {
		ret = -EIO;
		goto e_dst;
	}

	/* Save the ECC result */
	ccp_reverse_get_dm_area(&dst, 0, ecc->u.mm.result, 0,
				CCP_ECC_MODULUS_BYTES);

e_dst:
	ccp_dm_free(&dst);

e_src:
	ccp_dm_free(&src);

	return ret;
}

static int ccp_run_ecc_pm_cmd(struct ccp_cmd_queue *cmd_q, struct ccp_cmd *cmd)
{
	struct ccp_ecc_engine *ecc = &cmd->u.ecc;
	struct ccp_dm_workarea src, dst;
	struct ccp_op op;
	int ret;
	u8 *save;

	if (!ecc->u.pm.point_1.x ||
	    (ecc->u.pm.point_1.x_len > CCP_ECC_MODULUS_BYTES) ||
	    !ecc->u.pm.point_1.y ||
	    (ecc->u.pm.point_1.y_len > CCP_ECC_MODULUS_BYTES))
		return -EINVAL;

	if (ecc->function == CCP_ECC_FUNCTION_PADD_384BIT) {
		if (!ecc->u.pm.point_2.x ||
		    (ecc->u.pm.point_2.x_len > CCP_ECC_MODULUS_BYTES) ||
		    !ecc->u.pm.point_2.y ||
		    (ecc->u.pm.point_2.y_len > CCP_ECC_MODULUS_BYTES))
			return -EINVAL;
	} else {
		if (!ecc->u.pm.domain_a ||
		    (ecc->u.pm.domain_a_len > CCP_ECC_MODULUS_BYTES))
			return -EINVAL;

		if (ecc->function == CCP_ECC_FUNCTION_PMUL_384BIT)
			if (!ecc->u.pm.scalar ||
			    (ecc->u.pm.scalar_len > CCP_ECC_MODULUS_BYTES))
				return -EINVAL;
	}

	if (!ecc->u.pm.result.x ||
	    (ecc->u.pm.result.x_len < CCP_ECC_MODULUS_BYTES) ||
	    !ecc->u.pm.result.y ||
	    (ecc->u.pm.result.y_len < CCP_ECC_MODULUS_BYTES))
		return -EINVAL;

	memset(&op, 0, sizeof(op));
	op.cmd_q = cmd_q;
	op.jobid = CCP_NEW_JOBID(cmd_q->ccp);

	/* Concatenate the modulus and the operands. Both the modulus and
	 * the operands must be in little endian format.  Since the input
	 * is in big endian format it must be converted and placed in a
	 * fixed length buffer.
	 */
	ret = ccp_init_dm_workarea(&src, cmd_q, CCP_ECC_SRC_BUF_SIZE,
				   DMA_TO_DEVICE);
	if (ret)
		return ret;

	/* Save the workarea address since it is updated in order to perform
	 * the concatenation
	 */
	save = src.address;

	/* Copy the ECC modulus */
	ret = ccp_reverse_set_dm_area(&src, 0, ecc->mod, 0, ecc->mod_len);
	if (ret)
		goto e_src;
	src.address += CCP_ECC_OPERAND_SIZE;

	/* Copy the first point X and Y coordinate */
	ret = ccp_reverse_set_dm_area(&src, 0, ecc->u.pm.point_1.x, 0,
				      ecc->u.pm.point_1.x_len);
	if (ret)
		goto e_src;
	src.address += CCP_ECC_OPERAND_SIZE;
	ret = ccp_reverse_set_dm_area(&src, 0, ecc->u.pm.point_1.y, 0,
				      ecc->u.pm.point_1.y_len);
	if (ret)
		goto e_src;
	src.address += CCP_ECC_OPERAND_SIZE;

	/* Set the first point Z coordinate to 1 */
	*src.address = 0x01;
	src.address += CCP_ECC_OPERAND_SIZE;

	if (ecc->function == CCP_ECC_FUNCTION_PADD_384BIT) {
		/* Copy the second point X and Y coordinate */
		ret = ccp_reverse_set_dm_area(&src, 0, ecc->u.pm.point_2.x, 0,
					      ecc->u.pm.point_2.x_len);
		if (ret)
			goto e_src;
		src.address += CCP_ECC_OPERAND_SIZE;
		ret = ccp_reverse_set_dm_area(&src, 0, ecc->u.pm.point_2.y, 0,
					      ecc->u.pm.point_2.y_len);
		if (ret)
			goto e_src;
		src.address += CCP_ECC_OPERAND_SIZE;

		/* Set the second point Z coordinate to 1 */
		*src.address = 0x01;
		src.address += CCP_ECC_OPERAND_SIZE;
	} else {
		/* Copy the Domain "a" parameter */
		ret = ccp_reverse_set_dm_area(&src, 0, ecc->u.pm.domain_a, 0,
					      ecc->u.pm.domain_a_len);
		if (ret)
			goto e_src;
		src.address += CCP_ECC_OPERAND_SIZE;

		if (ecc->function == CCP_ECC_FUNCTION_PMUL_384BIT) {
			/* Copy the scalar value */
			ret = ccp_reverse_set_dm_area(&src, 0,
						      ecc->u.pm.scalar, 0,
						      ecc->u.pm.scalar_len);
			if (ret)
				goto e_src;
			src.address += CCP_ECC_OPERAND_SIZE;
		}
	}

	/* Restore the workarea address */
	src.address = save;

	/* Prepare the output area for the operation */
	ret = ccp_init_dm_workarea(&dst, cmd_q, CCP_ECC_DST_BUF_SIZE,
				   DMA_FROM_DEVICE);
	if (ret)
		goto e_src;

	op.soc = 1;
	op.src.u.dma.address = src.dma.address;
	op.src.u.dma.offset = 0;
	op.src.u.dma.length = src.length;
	op.dst.u.dma.address = dst.dma.address;
	op.dst.u.dma.offset = 0;
	op.dst.u.dma.length = dst.length;

	op.u.ecc.function = cmd->u.ecc.function;

	ret = cmd_q->ccp->vdata->perform->ecc(&op);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_dst;
	}

	ecc->ecc_result = le16_to_cpup(
		(const __le16 *)(dst.address + CCP_ECC_RESULT_OFFSET));
	if (!(ecc->ecc_result & CCP_ECC_RESULT_SUCCESS)) {
		ret = -EIO;
		goto e_dst;
	}

	/* Save the workarea address since it is updated as we walk through
	 * to copy the point math result
	 */
	save = dst.address;

	/* Save the ECC result X and Y coordinates */
	ccp_reverse_get_dm_area(&dst, 0, ecc->u.pm.result.x, 0,
				CCP_ECC_MODULUS_BYTES);
	dst.address += CCP_ECC_OUTPUT_SIZE;
	ccp_reverse_get_dm_area(&dst, 0, ecc->u.pm.result.y, 0,
				CCP_ECC_MODULUS_BYTES);
	dst.address += CCP_ECC_OUTPUT_SIZE;

	/* Restore the workarea address */
	dst.address = save;

e_dst:
	ccp_dm_free(&dst);

e_src:
	ccp_dm_free(&src);

	return ret;
}

static noinline_for_stack int
ccp_run_ecc_cmd(struct ccp_cmd_queue *cmd_q, struct ccp_cmd *cmd)
{
	struct ccp_ecc_engine *ecc = &cmd->u.ecc;

	ecc->ecc_result = 0;

	if (!ecc->mod ||
	    (ecc->mod_len > CCP_ECC_MODULUS_BYTES))
		return -EINVAL;

	switch (ecc->function) {
	case CCP_ECC_FUNCTION_MMUL_384BIT:
	case CCP_ECC_FUNCTION_MADD_384BIT:
	case CCP_ECC_FUNCTION_MINV_384BIT:
		return ccp_run_ecc_mm_cmd(cmd_q, cmd);

	case CCP_ECC_FUNCTION_PADD_384BIT:
	case CCP_ECC_FUNCTION_PMUL_384BIT:
	case CCP_ECC_FUNCTION_PDBL_384BIT:
		return ccp_run_ecc_pm_cmd(cmd_q, cmd);

	default:
		return -EINVAL;
	}
}

int ccp_run_cmd(struct ccp_cmd_queue *cmd_q, struct ccp_cmd *cmd)
{
	int ret;

	cmd->engine_error = 0;
	cmd_q->cmd_error = 0;
	cmd_q->int_rcvd = 0;
	cmd_q->free_slots = cmd_q->ccp->vdata->perform->get_free_slots(cmd_q);

	switch (cmd->engine) {
	case CCP_ENGINE_AES:
		switch (cmd->u.aes.mode) {
		case CCP_AES_MODE_CMAC:
			ret = ccp_run_aes_cmac_cmd(cmd_q, cmd);
			break;
		case CCP_AES_MODE_GCM:
			ret = ccp_run_aes_gcm_cmd(cmd_q, cmd);
			break;
		default:
			ret = ccp_run_aes_cmd(cmd_q, cmd);
			break;
		}
		break;
	case CCP_ENGINE_XTS_AES_128:
		ret = ccp_run_xts_aes_cmd(cmd_q, cmd);
		break;
	case CCP_ENGINE_DES3:
		ret = ccp_run_des3_cmd(cmd_q, cmd);
		break;
	case CCP_ENGINE_SHA:
		ret = ccp_run_sha_cmd(cmd_q, cmd);
		break;
	case CCP_ENGINE_RSA:
		ret = ccp_run_rsa_cmd(cmd_q, cmd);
		break;
	case CCP_ENGINE_PASSTHRU:
		if (cmd->flags & CCP_CMD_PASSTHRU_NO_DMA_MAP)
			ret = ccp_run_passthru_nomap_cmd(cmd_q, cmd);
		else
			ret = ccp_run_passthru_cmd(cmd_q, cmd);
		break;
	case CCP_ENGINE_ECC:
		ret = ccp_run_ecc_cmd(cmd_q, cmd);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}
