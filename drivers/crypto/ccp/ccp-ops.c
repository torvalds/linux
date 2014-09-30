/*
 * AMD Cryptographic Coprocessor (CCP) driver
 *
 * Copyright (C) 2013 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/ccp.h>
#include <linux/scatterlist.h>
#include <crypto/scatterwalk.h>
#include <crypto/sha.h>

#include "ccp-dev.h"


enum ccp_memtype {
	CCP_MEMTYPE_SYSTEM = 0,
	CCP_MEMTYPE_KSB,
	CCP_MEMTYPE_LOCAL,
	CCP_MEMTYPE__LAST,
};

struct ccp_dma_info {
	dma_addr_t address;
	unsigned int offset;
	unsigned int length;
	enum dma_data_direction dir;
};

struct ccp_dm_workarea {
	struct device *dev;
	struct dma_pool *dma_pool;
	unsigned int length;

	u8 *address;
	struct ccp_dma_info dma;
};

struct ccp_sg_workarea {
	struct scatterlist *sg;
	unsigned int nents;
	unsigned int length;

	struct scatterlist *dma_sg;
	struct device *dma_dev;
	unsigned int dma_count;
	enum dma_data_direction dma_dir;

	unsigned int sg_used;

	u64 bytes_left;
};

struct ccp_data {
	struct ccp_sg_workarea sg_wa;
	struct ccp_dm_workarea dm_wa;
};

struct ccp_mem {
	enum ccp_memtype type;
	union {
		struct ccp_dma_info dma;
		u32 ksb;
	} u;
};

struct ccp_aes_op {
	enum ccp_aes_type type;
	enum ccp_aes_mode mode;
	enum ccp_aes_action action;
};

struct ccp_xts_aes_op {
	enum ccp_aes_action action;
	enum ccp_xts_aes_unit_size unit_size;
};

struct ccp_sha_op {
	enum ccp_sha_type type;
	u64 msg_bits;
};

struct ccp_rsa_op {
	u32 mod_size;
	u32 input_len;
};

struct ccp_passthru_op {
	enum ccp_passthru_bitwise bit_mod;
	enum ccp_passthru_byteswap byte_swap;
};

struct ccp_ecc_op {
	enum ccp_ecc_function function;
};

struct ccp_op {
	struct ccp_cmd_queue *cmd_q;

	u32 jobid;
	u32 ioc;
	u32 soc;
	u32 ksb_key;
	u32 ksb_ctx;
	u32 init;
	u32 eom;

	struct ccp_mem src;
	struct ccp_mem dst;

	union {
		struct ccp_aes_op aes;
		struct ccp_xts_aes_op xts;
		struct ccp_sha_op sha;
		struct ccp_rsa_op rsa;
		struct ccp_passthru_op passthru;
		struct ccp_ecc_op ecc;
	} u;
};

/* SHA initial context values */
static const __be32 ccp_sha1_init[CCP_SHA_CTXSIZE / sizeof(__be32)] = {
	cpu_to_be32(SHA1_H0), cpu_to_be32(SHA1_H1),
	cpu_to_be32(SHA1_H2), cpu_to_be32(SHA1_H3),
	cpu_to_be32(SHA1_H4), 0, 0, 0,
};

static const __be32 ccp_sha224_init[CCP_SHA_CTXSIZE / sizeof(__be32)] = {
	cpu_to_be32(SHA224_H0), cpu_to_be32(SHA224_H1),
	cpu_to_be32(SHA224_H2), cpu_to_be32(SHA224_H3),
	cpu_to_be32(SHA224_H4), cpu_to_be32(SHA224_H5),
	cpu_to_be32(SHA224_H6), cpu_to_be32(SHA224_H7),
};

static const __be32 ccp_sha256_init[CCP_SHA_CTXSIZE / sizeof(__be32)] = {
	cpu_to_be32(SHA256_H0), cpu_to_be32(SHA256_H1),
	cpu_to_be32(SHA256_H2), cpu_to_be32(SHA256_H3),
	cpu_to_be32(SHA256_H4), cpu_to_be32(SHA256_H5),
	cpu_to_be32(SHA256_H6), cpu_to_be32(SHA256_H7),
};

/* The CCP cannot perform zero-length sha operations so the caller
 * is required to buffer data for the final operation.  However, a
 * sha operation for a message with a total length of zero is valid
 * so known values are required to supply the result.
 */
static const u8 ccp_sha1_zero[CCP_SHA_CTXSIZE] = {
	0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d,
	0x32, 0x55, 0xbf, 0xef, 0x95, 0x60, 0x18, 0x90,
	0xaf, 0xd8, 0x07, 0x09, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const u8 ccp_sha224_zero[CCP_SHA_CTXSIZE] = {
	0xd1, 0x4a, 0x02, 0x8c, 0x2a, 0x3a, 0x2b, 0xc9,
	0x47, 0x61, 0x02, 0xbb, 0x28, 0x82, 0x34, 0xc4,
	0x15, 0xa2, 0xb0, 0x1f, 0x82, 0x8e, 0xa6, 0x2a,
	0xc5, 0xb3, 0xe4, 0x2f, 0x00, 0x00, 0x00, 0x00,
};

static const u8 ccp_sha256_zero[CCP_SHA_CTXSIZE] = {
	0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
	0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
	0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
	0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55,
};

static u32 ccp_addr_lo(struct ccp_dma_info *info)
{
	return lower_32_bits(info->address + info->offset);
}

static u32 ccp_addr_hi(struct ccp_dma_info *info)
{
	return upper_32_bits(info->address + info->offset) & 0x0000ffff;
}

static int ccp_do_cmd(struct ccp_op *op, u32 *cr, unsigned int cr_count)
{
	struct ccp_cmd_queue *cmd_q = op->cmd_q;
	struct ccp_device *ccp = cmd_q->ccp;
	void __iomem *cr_addr;
	u32 cr0, cmd;
	unsigned int i;
	int ret = 0;

	/* We could read a status register to see how many free slots
	 * are actually available, but reading that register resets it
	 * and you could lose some error information.
	 */
	cmd_q->free_slots--;

	cr0 = (cmd_q->id << REQ0_CMD_Q_SHIFT)
	      | (op->jobid << REQ0_JOBID_SHIFT)
	      | REQ0_WAIT_FOR_WRITE;

	if (op->soc)
		cr0 |= REQ0_STOP_ON_COMPLETE
		       | REQ0_INT_ON_COMPLETE;

	if (op->ioc || !cmd_q->free_slots)
		cr0 |= REQ0_INT_ON_COMPLETE;

	/* Start at CMD_REQ1 */
	cr_addr = ccp->io_regs + CMD_REQ0 + CMD_REQ_INCR;

	mutex_lock(&ccp->req_mutex);

	/* Write CMD_REQ1 through CMD_REQx first */
	for (i = 0; i < cr_count; i++, cr_addr += CMD_REQ_INCR)
		iowrite32(*(cr + i), cr_addr);

	/* Tell the CCP to start */
	wmb();
	iowrite32(cr0, ccp->io_regs + CMD_REQ0);

	mutex_unlock(&ccp->req_mutex);

	if (cr0 & REQ0_INT_ON_COMPLETE) {
		/* Wait for the job to complete */
		ret = wait_event_interruptible(cmd_q->int_queue,
					       cmd_q->int_rcvd);
		if (ret || cmd_q->cmd_error) {
			/* On error delete all related jobs from the queue */
			cmd = (cmd_q->id << DEL_Q_ID_SHIFT)
			      | op->jobid;

			iowrite32(cmd, ccp->io_regs + DEL_CMD_Q_JOB);

			if (!ret)
				ret = -EIO;
		} else if (op->soc) {
			/* Delete just head job from the queue on SoC */
			cmd = DEL_Q_ACTIVE
			      | (cmd_q->id << DEL_Q_ID_SHIFT)
			      | op->jobid;

			iowrite32(cmd, ccp->io_regs + DEL_CMD_Q_JOB);
		}

		cmd_q->free_slots = CMD_Q_DEPTH(cmd_q->q_status);

		cmd_q->int_rcvd = 0;
	}

	return ret;
}

static int ccp_perform_aes(struct ccp_op *op)
{
	u32 cr[6];

	/* Fill out the register contents for REQ1 through REQ6 */
	cr[0] = (CCP_ENGINE_AES << REQ1_ENGINE_SHIFT)
		| (op->u.aes.type << REQ1_AES_TYPE_SHIFT)
		| (op->u.aes.mode << REQ1_AES_MODE_SHIFT)
		| (op->u.aes.action << REQ1_AES_ACTION_SHIFT)
		| (op->ksb_key << REQ1_KEY_KSB_SHIFT);
	cr[1] = op->src.u.dma.length - 1;
	cr[2] = ccp_addr_lo(&op->src.u.dma);
	cr[3] = (op->ksb_ctx << REQ4_KSB_SHIFT)
		| (CCP_MEMTYPE_SYSTEM << REQ4_MEMTYPE_SHIFT)
		| ccp_addr_hi(&op->src.u.dma);
	cr[4] = ccp_addr_lo(&op->dst.u.dma);
	cr[5] = (CCP_MEMTYPE_SYSTEM << REQ6_MEMTYPE_SHIFT)
		| ccp_addr_hi(&op->dst.u.dma);

	if (op->u.aes.mode == CCP_AES_MODE_CFB)
		cr[0] |= ((0x7f) << REQ1_AES_CFB_SIZE_SHIFT);

	if (op->eom)
		cr[0] |= REQ1_EOM;

	if (op->init)
		cr[0] |= REQ1_INIT;

	return ccp_do_cmd(op, cr, ARRAY_SIZE(cr));
}

static int ccp_perform_xts_aes(struct ccp_op *op)
{
	u32 cr[6];

	/* Fill out the register contents for REQ1 through REQ6 */
	cr[0] = (CCP_ENGINE_XTS_AES_128 << REQ1_ENGINE_SHIFT)
		| (op->u.xts.action << REQ1_AES_ACTION_SHIFT)
		| (op->u.xts.unit_size << REQ1_XTS_AES_SIZE_SHIFT)
		| (op->ksb_key << REQ1_KEY_KSB_SHIFT);
	cr[1] = op->src.u.dma.length - 1;
	cr[2] = ccp_addr_lo(&op->src.u.dma);
	cr[3] = (op->ksb_ctx << REQ4_KSB_SHIFT)
		| (CCP_MEMTYPE_SYSTEM << REQ4_MEMTYPE_SHIFT)
		| ccp_addr_hi(&op->src.u.dma);
	cr[4] = ccp_addr_lo(&op->dst.u.dma);
	cr[5] = (CCP_MEMTYPE_SYSTEM << REQ6_MEMTYPE_SHIFT)
		| ccp_addr_hi(&op->dst.u.dma);

	if (op->eom)
		cr[0] |= REQ1_EOM;

	if (op->init)
		cr[0] |= REQ1_INIT;

	return ccp_do_cmd(op, cr, ARRAY_SIZE(cr));
}

static int ccp_perform_sha(struct ccp_op *op)
{
	u32 cr[6];

	/* Fill out the register contents for REQ1 through REQ6 */
	cr[0] = (CCP_ENGINE_SHA << REQ1_ENGINE_SHIFT)
		| (op->u.sha.type << REQ1_SHA_TYPE_SHIFT)
		| REQ1_INIT;
	cr[1] = op->src.u.dma.length - 1;
	cr[2] = ccp_addr_lo(&op->src.u.dma);
	cr[3] = (op->ksb_ctx << REQ4_KSB_SHIFT)
		| (CCP_MEMTYPE_SYSTEM << REQ4_MEMTYPE_SHIFT)
		| ccp_addr_hi(&op->src.u.dma);

	if (op->eom) {
		cr[0] |= REQ1_EOM;
		cr[4] = lower_32_bits(op->u.sha.msg_bits);
		cr[5] = upper_32_bits(op->u.sha.msg_bits);
	} else {
		cr[4] = 0;
		cr[5] = 0;
	}

	return ccp_do_cmd(op, cr, ARRAY_SIZE(cr));
}

static int ccp_perform_rsa(struct ccp_op *op)
{
	u32 cr[6];

	/* Fill out the register contents for REQ1 through REQ6 */
	cr[0] = (CCP_ENGINE_RSA << REQ1_ENGINE_SHIFT)
		| (op->u.rsa.mod_size << REQ1_RSA_MOD_SIZE_SHIFT)
		| (op->ksb_key << REQ1_KEY_KSB_SHIFT)
		| REQ1_EOM;
	cr[1] = op->u.rsa.input_len - 1;
	cr[2] = ccp_addr_lo(&op->src.u.dma);
	cr[3] = (op->ksb_ctx << REQ4_KSB_SHIFT)
		| (CCP_MEMTYPE_SYSTEM << REQ4_MEMTYPE_SHIFT)
		| ccp_addr_hi(&op->src.u.dma);
	cr[4] = ccp_addr_lo(&op->dst.u.dma);
	cr[5] = (CCP_MEMTYPE_SYSTEM << REQ6_MEMTYPE_SHIFT)
		| ccp_addr_hi(&op->dst.u.dma);

	return ccp_do_cmd(op, cr, ARRAY_SIZE(cr));
}

static int ccp_perform_passthru(struct ccp_op *op)
{
	u32 cr[6];

	/* Fill out the register contents for REQ1 through REQ6 */
	cr[0] = (CCP_ENGINE_PASSTHRU << REQ1_ENGINE_SHIFT)
		| (op->u.passthru.bit_mod << REQ1_PT_BW_SHIFT)
		| (op->u.passthru.byte_swap << REQ1_PT_BS_SHIFT);

	if (op->src.type == CCP_MEMTYPE_SYSTEM)
		cr[1] = op->src.u.dma.length - 1;
	else
		cr[1] = op->dst.u.dma.length - 1;

	if (op->src.type == CCP_MEMTYPE_SYSTEM) {
		cr[2] = ccp_addr_lo(&op->src.u.dma);
		cr[3] = (CCP_MEMTYPE_SYSTEM << REQ4_MEMTYPE_SHIFT)
			| ccp_addr_hi(&op->src.u.dma);

		if (op->u.passthru.bit_mod != CCP_PASSTHRU_BITWISE_NOOP)
			cr[3] |= (op->ksb_key << REQ4_KSB_SHIFT);
	} else {
		cr[2] = op->src.u.ksb * CCP_KSB_BYTES;
		cr[3] = (CCP_MEMTYPE_KSB << REQ4_MEMTYPE_SHIFT);
	}

	if (op->dst.type == CCP_MEMTYPE_SYSTEM) {
		cr[4] = ccp_addr_lo(&op->dst.u.dma);
		cr[5] = (CCP_MEMTYPE_SYSTEM << REQ6_MEMTYPE_SHIFT)
			| ccp_addr_hi(&op->dst.u.dma);
	} else {
		cr[4] = op->dst.u.ksb * CCP_KSB_BYTES;
		cr[5] = (CCP_MEMTYPE_KSB << REQ6_MEMTYPE_SHIFT);
	}

	if (op->eom)
		cr[0] |= REQ1_EOM;

	return ccp_do_cmd(op, cr, ARRAY_SIZE(cr));
}

static int ccp_perform_ecc(struct ccp_op *op)
{
	u32 cr[6];

	/* Fill out the register contents for REQ1 through REQ6 */
	cr[0] = REQ1_ECC_AFFINE_CONVERT
		| (CCP_ENGINE_ECC << REQ1_ENGINE_SHIFT)
		| (op->u.ecc.function << REQ1_ECC_FUNCTION_SHIFT)
		| REQ1_EOM;
	cr[1] = op->src.u.dma.length - 1;
	cr[2] = ccp_addr_lo(&op->src.u.dma);
	cr[3] = (CCP_MEMTYPE_SYSTEM << REQ4_MEMTYPE_SHIFT)
		| ccp_addr_hi(&op->src.u.dma);
	cr[4] = ccp_addr_lo(&op->dst.u.dma);
	cr[5] = (CCP_MEMTYPE_SYSTEM << REQ6_MEMTYPE_SHIFT)
		| ccp_addr_hi(&op->dst.u.dma);

	return ccp_do_cmd(op, cr, ARRAY_SIZE(cr));
}

static u32 ccp_alloc_ksb(struct ccp_device *ccp, unsigned int count)
{
	int start;

	for (;;) {
		mutex_lock(&ccp->ksb_mutex);

		start = (u32)bitmap_find_next_zero_area(ccp->ksb,
							ccp->ksb_count,
							ccp->ksb_start,
							count, 0);
		if (start <= ccp->ksb_count) {
			bitmap_set(ccp->ksb, start, count);

			mutex_unlock(&ccp->ksb_mutex);
			break;
		}

		ccp->ksb_avail = 0;

		mutex_unlock(&ccp->ksb_mutex);

		/* Wait for KSB entries to become available */
		if (wait_event_interruptible(ccp->ksb_queue, ccp->ksb_avail))
			return 0;
	}

	return KSB_START + start;
}

static void ccp_free_ksb(struct ccp_device *ccp, unsigned int start,
			 unsigned int count)
{
	if (!start)
		return;

	mutex_lock(&ccp->ksb_mutex);

	bitmap_clear(ccp->ksb, start - KSB_START, count);

	ccp->ksb_avail = 1;

	mutex_unlock(&ccp->ksb_mutex);

	wake_up_interruptible_all(&ccp->ksb_queue);
}

static u32 ccp_gen_jobid(struct ccp_device *ccp)
{
	return atomic_inc_return(&ccp->current_id) & CCP_JOBID_MASK;
}

static void ccp_sg_free(struct ccp_sg_workarea *wa)
{
	if (wa->dma_count)
		dma_unmap_sg(wa->dma_dev, wa->dma_sg, wa->nents, wa->dma_dir);

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

	wa->nents = sg_nents(sg);
	wa->length = sg->length;
	wa->bytes_left = len;
	wa->sg_used = 0;

	if (len == 0)
		return 0;

	if (dma_dir == DMA_NONE)
		return 0;

	wa->dma_sg = sg;
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

	if (!wa->sg)
		return;

	wa->sg_used += nbytes;
	wa->bytes_left -= nbytes;
	if (wa->sg_used == wa->sg->length) {
		wa->sg = sg_next(wa->sg);
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
		if (!wa->dma.address)
			return -ENOMEM;

		wa->dma.length = len;
	}
	wa->dma.dir = dir;

	return 0;
}

static void ccp_set_dm_area(struct ccp_dm_workarea *wa, unsigned int wa_offset,
			    struct scatterlist *sg, unsigned int sg_offset,
			    unsigned int len)
{
	WARN_ON(!wa->address);

	scatterwalk_map_and_copy(wa->address + wa_offset, sg, sg_offset, len,
				 0);
}

static void ccp_get_dm_area(struct ccp_dm_workarea *wa, unsigned int wa_offset,
			    struct scatterlist *sg, unsigned int sg_offset,
			    unsigned int len)
{
	WARN_ON(!wa->address);

	scatterwalk_map_and_copy(wa->address + wa_offset, sg, sg_offset, len,
				 1);
}

static void ccp_reverse_set_dm_area(struct ccp_dm_workarea *wa,
				    struct scatterlist *sg,
				    unsigned int len, unsigned int se_len,
				    bool sign_extend)
{
	unsigned int nbytes, sg_offset, dm_offset, ksb_len, i;
	u8 buffer[CCP_REVERSE_BUF_SIZE];

	BUG_ON(se_len > sizeof(buffer));

	sg_offset = len;
	dm_offset = 0;
	nbytes = len;
	while (nbytes) {
		ksb_len = min_t(unsigned int, nbytes, se_len);
		sg_offset -= ksb_len;

		scatterwalk_map_and_copy(buffer, sg, sg_offset, ksb_len, 0);
		for (i = 0; i < ksb_len; i++)
			wa->address[dm_offset + i] = buffer[ksb_len - i - 1];

		dm_offset += ksb_len;
		nbytes -= ksb_len;

		if ((ksb_len != se_len) && sign_extend) {
			/* Must sign-extend to nearest sign-extend length */
			if (wa->address[dm_offset - 1] & 0x80)
				memset(wa->address + dm_offset, 0xff,
				       se_len - ksb_len);
		}
	}
}

static void ccp_reverse_get_dm_area(struct ccp_dm_workarea *wa,
				    struct scatterlist *sg,
				    unsigned int len)
{
	unsigned int nbytes, sg_offset, dm_offset, ksb_len, i;
	u8 buffer[CCP_REVERSE_BUF_SIZE];

	sg_offset = 0;
	dm_offset = len;
	nbytes = len;
	while (nbytes) {
		ksb_len = min_t(unsigned int, nbytes, sizeof(buffer));
		dm_offset -= ksb_len;

		for (i = 0; i < ksb_len; i++)
			buffer[ksb_len - i - 1] = wa->address[dm_offset + i];
		scatterwalk_map_and_copy(buffer, sg, sg_offset, ksb_len, 1);

		sg_offset += ksb_len;
		nbytes -= ksb_len;
	}
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
		nbytes = min(sg_wa->sg->length - sg_wa->sg_used,
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
	sg_src_len = sg_dma_len(src->sg_wa.sg) - src->sg_wa.sg_used;
	sg_src_len = min_t(u64, src->sg_wa.bytes_left, sg_src_len);

	if (dst) {
		sg_dst_len = sg_dma_len(dst->sg_wa.sg) - dst->sg_wa.sg_used;
		sg_dst_len = min_t(u64, src->sg_wa.bytes_left, sg_dst_len);
		op_len = min(sg_src_len, sg_dst_len);
	} else
		op_len = sg_src_len;

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
		op->src.u.dma.address = sg_dma_address(src->sg_wa.sg);
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
			op->dst.u.dma.address = sg_dma_address(dst->sg_wa.sg);
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

static int ccp_copy_to_from_ksb(struct ccp_cmd_queue *cmd_q,
				struct ccp_dm_workarea *wa, u32 jobid, u32 ksb,
				u32 byte_swap, bool from)
{
	struct ccp_op op;

	memset(&op, 0, sizeof(op));

	op.cmd_q = cmd_q;
	op.jobid = jobid;
	op.eom = 1;

	if (from) {
		op.soc = 1;
		op.src.type = CCP_MEMTYPE_KSB;
		op.src.u.ksb = ksb;
		op.dst.type = CCP_MEMTYPE_SYSTEM;
		op.dst.u.dma.address = wa->dma.address;
		op.dst.u.dma.length = wa->length;
	} else {
		op.src.type = CCP_MEMTYPE_SYSTEM;
		op.src.u.dma.address = wa->dma.address;
		op.src.u.dma.length = wa->length;
		op.dst.type = CCP_MEMTYPE_KSB;
		op.dst.u.ksb = ksb;
	}

	op.u.passthru.byte_swap = byte_swap;

	return ccp_perform_passthru(&op);
}

static int ccp_copy_to_ksb(struct ccp_cmd_queue *cmd_q,
			   struct ccp_dm_workarea *wa, u32 jobid, u32 ksb,
			   u32 byte_swap)
{
	return ccp_copy_to_from_ksb(cmd_q, wa, jobid, ksb, byte_swap, false);
}

static int ccp_copy_from_ksb(struct ccp_cmd_queue *cmd_q,
			     struct ccp_dm_workarea *wa, u32 jobid, u32 ksb,
			     u32 byte_swap)
{
	return ccp_copy_to_from_ksb(cmd_q, wa, jobid, ksb, byte_swap, true);
}

static int ccp_run_aes_cmac_cmd(struct ccp_cmd_queue *cmd_q,
				struct ccp_cmd *cmd)
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

	BUILD_BUG_ON(CCP_AES_KEY_KSB_COUNT != 1);
	BUILD_BUG_ON(CCP_AES_CTX_KSB_COUNT != 1);

	ret = -EIO;
	memset(&op, 0, sizeof(op));
	op.cmd_q = cmd_q;
	op.jobid = ccp_gen_jobid(cmd_q->ccp);
	op.ksb_key = cmd_q->ksb_key;
	op.ksb_ctx = cmd_q->ksb_ctx;
	op.init = 1;
	op.u.aes.type = aes->type;
	op.u.aes.mode = aes->mode;
	op.u.aes.action = aes->action;

	/* All supported key sizes fit in a single (32-byte) KSB entry
	 * and must be in little endian format. Use the 256-bit byte
	 * swap passthru option to convert from big endian to little
	 * endian.
	 */
	ret = ccp_init_dm_workarea(&key, cmd_q,
				   CCP_AES_KEY_KSB_COUNT * CCP_KSB_BYTES,
				   DMA_TO_DEVICE);
	if (ret)
		return ret;

	dm_offset = CCP_KSB_BYTES - aes->key_len;
	ccp_set_dm_area(&key, dm_offset, aes->key, 0, aes->key_len);
	ret = ccp_copy_to_ksb(cmd_q, &key, op.jobid, op.ksb_key,
			      CCP_PASSTHRU_BYTESWAP_256BIT);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_key;
	}

	/* The AES context fits in a single (32-byte) KSB entry and
	 * must be in little endian format. Use the 256-bit byte swap
	 * passthru option to convert from big endian to little endian.
	 */
	ret = ccp_init_dm_workarea(&ctx, cmd_q,
				   CCP_AES_CTX_KSB_COUNT * CCP_KSB_BYTES,
				   DMA_BIDIRECTIONAL);
	if (ret)
		goto e_key;

	dm_offset = CCP_KSB_BYTES - AES_BLOCK_SIZE;
	ccp_set_dm_area(&ctx, dm_offset, aes->iv, 0, aes->iv_len);
	ret = ccp_copy_to_ksb(cmd_q, &ctx, op.jobid, op.ksb_ctx,
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
			ret = ccp_copy_from_ksb(cmd_q, &ctx, op.jobid,
						op.ksb_ctx,
						CCP_PASSTHRU_BYTESWAP_256BIT);
			if (ret) {
				cmd->engine_error = cmd_q->cmd_error;
				goto e_src;
			}

			ccp_set_dm_area(&ctx, 0, aes->cmac_key, 0,
					aes->cmac_key_len);
			ret = ccp_copy_to_ksb(cmd_q, &ctx, op.jobid, op.ksb_ctx,
					      CCP_PASSTHRU_BYTESWAP_256BIT);
			if (ret) {
				cmd->engine_error = cmd_q->cmd_error;
				goto e_src;
			}
		}

		ret = ccp_perform_aes(&op);
		if (ret) {
			cmd->engine_error = cmd_q->cmd_error;
			goto e_src;
		}

		ccp_process_data(&src, NULL, &op);
	}

	/* Retrieve the AES context - convert from LE to BE using
	 * 32-byte (256-bit) byteswapping
	 */
	ret = ccp_copy_from_ksb(cmd_q, &ctx, op.jobid, op.ksb_ctx,
				CCP_PASSTHRU_BYTESWAP_256BIT);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_src;
	}

	/* ...but we only need AES_BLOCK_SIZE bytes */
	dm_offset = CCP_KSB_BYTES - AES_BLOCK_SIZE;
	ccp_get_dm_area(&ctx, dm_offset, aes->iv, 0, aes->iv_len);

e_src:
	ccp_free_data(&src, cmd_q);

e_ctx:
	ccp_dm_free(&ctx);

e_key:
	ccp_dm_free(&key);

	return ret;
}

static int ccp_run_aes_cmd(struct ccp_cmd_queue *cmd_q, struct ccp_cmd *cmd)
{
	struct ccp_aes_engine *aes = &cmd->u.aes;
	struct ccp_dm_workarea key, ctx;
	struct ccp_data src, dst;
	struct ccp_op op;
	unsigned int dm_offset;
	bool in_place = false;
	int ret;

	if (aes->mode == CCP_AES_MODE_CMAC)
		return ccp_run_aes_cmac_cmd(cmd_q, cmd);

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

	BUILD_BUG_ON(CCP_AES_KEY_KSB_COUNT != 1);
	BUILD_BUG_ON(CCP_AES_CTX_KSB_COUNT != 1);

	ret = -EIO;
	memset(&op, 0, sizeof(op));
	op.cmd_q = cmd_q;
	op.jobid = ccp_gen_jobid(cmd_q->ccp);
	op.ksb_key = cmd_q->ksb_key;
	op.ksb_ctx = cmd_q->ksb_ctx;
	op.init = (aes->mode == CCP_AES_MODE_ECB) ? 0 : 1;
	op.u.aes.type = aes->type;
	op.u.aes.mode = aes->mode;
	op.u.aes.action = aes->action;

	/* All supported key sizes fit in a single (32-byte) KSB entry
	 * and must be in little endian format. Use the 256-bit byte
	 * swap passthru option to convert from big endian to little
	 * endian.
	 */
	ret = ccp_init_dm_workarea(&key, cmd_q,
				   CCP_AES_KEY_KSB_COUNT * CCP_KSB_BYTES,
				   DMA_TO_DEVICE);
	if (ret)
		return ret;

	dm_offset = CCP_KSB_BYTES - aes->key_len;
	ccp_set_dm_area(&key, dm_offset, aes->key, 0, aes->key_len);
	ret = ccp_copy_to_ksb(cmd_q, &key, op.jobid, op.ksb_key,
			      CCP_PASSTHRU_BYTESWAP_256BIT);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_key;
	}

	/* The AES context fits in a single (32-byte) KSB entry and
	 * must be in little endian format. Use the 256-bit byte swap
	 * passthru option to convert from big endian to little endian.
	 */
	ret = ccp_init_dm_workarea(&ctx, cmd_q,
				   CCP_AES_CTX_KSB_COUNT * CCP_KSB_BYTES,
				   DMA_BIDIRECTIONAL);
	if (ret)
		goto e_key;

	if (aes->mode != CCP_AES_MODE_ECB) {
		/* Load the AES context - conver to LE */
		dm_offset = CCP_KSB_BYTES - AES_BLOCK_SIZE;
		ccp_set_dm_area(&ctx, dm_offset, aes->iv, 0, aes->iv_len);
		ret = ccp_copy_to_ksb(cmd_q, &ctx, op.jobid, op.ksb_ctx,
				      CCP_PASSTHRU_BYTESWAP_256BIT);
		if (ret) {
			cmd->engine_error = cmd_q->cmd_error;
			goto e_ctx;
		}
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

	if (in_place)
		dst = src;
	else {
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

		ret = ccp_perform_aes(&op);
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
		ret = ccp_copy_from_ksb(cmd_q, &ctx, op.jobid, op.ksb_ctx,
					CCP_PASSTHRU_BYTESWAP_256BIT);
		if (ret) {
			cmd->engine_error = cmd_q->cmd_error;
			goto e_dst;
		}

		/* ...but we only need AES_BLOCK_SIZE bytes */
		dm_offset = CCP_KSB_BYTES - AES_BLOCK_SIZE;
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

static int ccp_run_xts_aes_cmd(struct ccp_cmd_queue *cmd_q,
			       struct ccp_cmd *cmd)
{
	struct ccp_xts_aes_engine *xts = &cmd->u.xts;
	struct ccp_dm_workarea key, ctx;
	struct ccp_data src, dst;
	struct ccp_op op;
	unsigned int unit_size, dm_offset;
	bool in_place = false;
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

	if (xts->key_len != AES_KEYSIZE_128)
		return -EINVAL;

	if (!xts->final && (xts->src_len & (AES_BLOCK_SIZE - 1)))
		return -EINVAL;

	if (xts->iv_len != AES_BLOCK_SIZE)
		return -EINVAL;

	if (!xts->key || !xts->iv || !xts->src || !xts->dst)
		return -EINVAL;

	BUILD_BUG_ON(CCP_XTS_AES_KEY_KSB_COUNT != 1);
	BUILD_BUG_ON(CCP_XTS_AES_CTX_KSB_COUNT != 1);

	ret = -EIO;
	memset(&op, 0, sizeof(op));
	op.cmd_q = cmd_q;
	op.jobid = ccp_gen_jobid(cmd_q->ccp);
	op.ksb_key = cmd_q->ksb_key;
	op.ksb_ctx = cmd_q->ksb_ctx;
	op.init = 1;
	op.u.xts.action = xts->action;
	op.u.xts.unit_size = xts->unit_size;

	/* All supported key sizes fit in a single (32-byte) KSB entry
	 * and must be in little endian format. Use the 256-bit byte
	 * swap passthru option to convert from big endian to little
	 * endian.
	 */
	ret = ccp_init_dm_workarea(&key, cmd_q,
				   CCP_XTS_AES_KEY_KSB_COUNT * CCP_KSB_BYTES,
				   DMA_TO_DEVICE);
	if (ret)
		return ret;

	dm_offset = CCP_KSB_BYTES - AES_KEYSIZE_128;
	ccp_set_dm_area(&key, dm_offset, xts->key, 0, xts->key_len);
	ccp_set_dm_area(&key, 0, xts->key, dm_offset, xts->key_len);
	ret = ccp_copy_to_ksb(cmd_q, &key, op.jobid, op.ksb_key,
			      CCP_PASSTHRU_BYTESWAP_256BIT);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_key;
	}

	/* The AES context fits in a single (32-byte) KSB entry and
	 * for XTS is already in little endian format so no byte swapping
	 * is needed.
	 */
	ret = ccp_init_dm_workarea(&ctx, cmd_q,
				   CCP_XTS_AES_CTX_KSB_COUNT * CCP_KSB_BYTES,
				   DMA_BIDIRECTIONAL);
	if (ret)
		goto e_key;

	ccp_set_dm_area(&ctx, 0, xts->iv, 0, xts->iv_len);
	ret = ccp_copy_to_ksb(cmd_q, &ctx, op.jobid, op.ksb_ctx,
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

	if (in_place)
		dst = src;
	else {
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

		ret = ccp_perform_xts_aes(&op);
		if (ret) {
			cmd->engine_error = cmd_q->cmd_error;
			goto e_dst;
		}

		ccp_process_data(&src, &dst, &op);
	}

	/* Retrieve the AES context - convert from LE to BE using
	 * 32-byte (256-bit) byteswapping
	 */
	ret = ccp_copy_from_ksb(cmd_q, &ctx, op.jobid, op.ksb_ctx,
				CCP_PASSTHRU_BYTESWAP_256BIT);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_dst;
	}

	/* ...but we only need AES_BLOCK_SIZE bytes */
	dm_offset = CCP_KSB_BYTES - AES_BLOCK_SIZE;
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

static int ccp_run_sha_cmd(struct ccp_cmd_queue *cmd_q, struct ccp_cmd *cmd)
{
	struct ccp_sha_engine *sha = &cmd->u.sha;
	struct ccp_dm_workarea ctx;
	struct ccp_data src;
	struct ccp_op op;
	int ret;

	if (sha->ctx_len != CCP_SHA_CTXSIZE)
		return -EINVAL;

	if (!sha->ctx)
		return -EINVAL;

	if (!sha->final && (sha->src_len & (CCP_SHA_BLOCKSIZE - 1)))
		return -EINVAL;

	if (!sha->src_len) {
		const u8 *sha_zero;

		/* Not final, just return */
		if (!sha->final)
			return 0;

		/* CCP can't do a zero length sha operation so the caller
		 * must buffer the data.
		 */
		if (sha->msg_bits)
			return -EINVAL;

		/* A sha operation for a message with a total length of zero,
		 * return known result.
		 */
		switch (sha->type) {
		case CCP_SHA_TYPE_1:
			sha_zero = ccp_sha1_zero;
			break;
		case CCP_SHA_TYPE_224:
			sha_zero = ccp_sha224_zero;
			break;
		case CCP_SHA_TYPE_256:
			sha_zero = ccp_sha256_zero;
			break;
		default:
			return -EINVAL;
		}

		scatterwalk_map_and_copy((void *)sha_zero, sha->ctx, 0,
					 sha->ctx_len, 1);

		return 0;
	}

	if (!sha->src)
		return -EINVAL;

	BUILD_BUG_ON(CCP_SHA_KSB_COUNT != 1);

	memset(&op, 0, sizeof(op));
	op.cmd_q = cmd_q;
	op.jobid = ccp_gen_jobid(cmd_q->ccp);
	op.ksb_ctx = cmd_q->ksb_ctx;
	op.u.sha.type = sha->type;
	op.u.sha.msg_bits = sha->msg_bits;

	/* The SHA context fits in a single (32-byte) KSB entry and
	 * must be in little endian format. Use the 256-bit byte swap
	 * passthru option to convert from big endian to little endian.
	 */
	ret = ccp_init_dm_workarea(&ctx, cmd_q,
				   CCP_SHA_KSB_COUNT * CCP_KSB_BYTES,
				   DMA_BIDIRECTIONAL);
	if (ret)
		return ret;

	if (sha->first) {
		const __be32 *init;

		switch (sha->type) {
		case CCP_SHA_TYPE_1:
			init = ccp_sha1_init;
			break;
		case CCP_SHA_TYPE_224:
			init = ccp_sha224_init;
			break;
		case CCP_SHA_TYPE_256:
			init = ccp_sha256_init;
			break;
		default:
			ret = -EINVAL;
			goto e_ctx;
		}
		memcpy(ctx.address, init, CCP_SHA_CTXSIZE);
	} else
		ccp_set_dm_area(&ctx, 0, sha->ctx, 0, sha->ctx_len);

	ret = ccp_copy_to_ksb(cmd_q, &ctx, op.jobid, op.ksb_ctx,
			      CCP_PASSTHRU_BYTESWAP_256BIT);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_ctx;
	}

	/* Send data to the CCP SHA engine */
	ret = ccp_init_data(&src, cmd_q, sha->src, sha->src_len,
			    CCP_SHA_BLOCKSIZE, DMA_TO_DEVICE);
	if (ret)
		goto e_ctx;

	while (src.sg_wa.bytes_left) {
		ccp_prepare_data(&src, NULL, &op, CCP_SHA_BLOCKSIZE, false);
		if (sha->final && !src.sg_wa.bytes_left)
			op.eom = 1;

		ret = ccp_perform_sha(&op);
		if (ret) {
			cmd->engine_error = cmd_q->cmd_error;
			goto e_data;
		}

		ccp_process_data(&src, NULL, &op);
	}

	/* Retrieve the SHA context - convert from LE to BE using
	 * 32-byte (256-bit) byteswapping to BE
	 */
	ret = ccp_copy_from_ksb(cmd_q, &ctx, op.jobid, op.ksb_ctx,
				CCP_PASSTHRU_BYTESWAP_256BIT);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_data;
	}

	ccp_get_dm_area(&ctx, 0, sha->ctx, 0, sha->ctx_len);

	if (sha->final && sha->opad) {
		/* HMAC operation, recursively perform final SHA */
		struct ccp_cmd hmac_cmd;
		struct scatterlist sg;
		u64 block_size, digest_size;
		u8 *hmac_buf;

		switch (sha->type) {
		case CCP_SHA_TYPE_1:
			block_size = SHA1_BLOCK_SIZE;
			digest_size = SHA1_DIGEST_SIZE;
			break;
		case CCP_SHA_TYPE_224:
			block_size = SHA224_BLOCK_SIZE;
			digest_size = SHA224_DIGEST_SIZE;
			break;
		case CCP_SHA_TYPE_256:
			block_size = SHA256_BLOCK_SIZE;
			digest_size = SHA256_DIGEST_SIZE;
			break;
		default:
			ret = -EINVAL;
			goto e_data;
		}

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
		memcpy(hmac_buf + block_size, ctx.address, digest_size);

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
	ccp_free_data(&src, cmd_q);

e_ctx:
	ccp_dm_free(&ctx);

	return ret;
}

static int ccp_run_rsa_cmd(struct ccp_cmd_queue *cmd_q, struct ccp_cmd *cmd)
{
	struct ccp_rsa_engine *rsa = &cmd->u.rsa;
	struct ccp_dm_workarea exp, src;
	struct ccp_data dst;
	struct ccp_op op;
	unsigned int ksb_count, i_len, o_len;
	int ret;

	if (rsa->key_size > CCP_RSA_MAX_WIDTH)
		return -EINVAL;

	if (!rsa->exp || !rsa->mod || !rsa->src || !rsa->dst)
		return -EINVAL;

	/* The RSA modulus must precede the message being acted upon, so
	 * it must be copied to a DMA area where the message and the
	 * modulus can be concatenated.  Therefore the input buffer
	 * length required is twice the output buffer length (which
	 * must be a multiple of 256-bits).
	 */
	o_len = ((rsa->key_size + 255) / 256) * 32;
	i_len = o_len * 2;

	ksb_count = o_len / CCP_KSB_BYTES;

	memset(&op, 0, sizeof(op));
	op.cmd_q = cmd_q;
	op.jobid = ccp_gen_jobid(cmd_q->ccp);
	op.ksb_key = ccp_alloc_ksb(cmd_q->ccp, ksb_count);
	if (!op.ksb_key)
		return -EIO;

	/* The RSA exponent may span multiple (32-byte) KSB entries and must
	 * be in little endian format. Reverse copy each 32-byte chunk
	 * of the exponent (En chunk to E0 chunk, E(n-1) chunk to E1 chunk)
	 * and each byte within that chunk and do not perform any byte swap
	 * operations on the passthru operation.
	 */
	ret = ccp_init_dm_workarea(&exp, cmd_q, o_len, DMA_TO_DEVICE);
	if (ret)
		goto e_ksb;

	ccp_reverse_set_dm_area(&exp, rsa->exp, rsa->exp_len, CCP_KSB_BYTES,
				false);
	ret = ccp_copy_to_ksb(cmd_q, &exp, op.jobid, op.ksb_key,
			      CCP_PASSTHRU_BYTESWAP_NOOP);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_exp;
	}

	/* Concatenate the modulus and the message. Both the modulus and
	 * the operands must be in little endian format.  Since the input
	 * is in big endian format it must be converted.
	 */
	ret = ccp_init_dm_workarea(&src, cmd_q, i_len, DMA_TO_DEVICE);
	if (ret)
		goto e_exp;

	ccp_reverse_set_dm_area(&src, rsa->mod, rsa->mod_len, CCP_KSB_BYTES,
				false);
	src.address += o_len;	/* Adjust the address for the copy operation */
	ccp_reverse_set_dm_area(&src, rsa->src, rsa->src_len, CCP_KSB_BYTES,
				false);
	src.address -= o_len;	/* Reset the address to original value */

	/* Prepare the output area for the operation */
	ret = ccp_init_data(&dst, cmd_q, rsa->dst, rsa->mod_len,
			    o_len, DMA_FROM_DEVICE);
	if (ret)
		goto e_src;

	op.soc = 1;
	op.src.u.dma.address = src.dma.address;
	op.src.u.dma.offset = 0;
	op.src.u.dma.length = i_len;
	op.dst.u.dma.address = dst.dm_wa.dma.address;
	op.dst.u.dma.offset = 0;
	op.dst.u.dma.length = o_len;

	op.u.rsa.mod_size = rsa->key_size;
	op.u.rsa.input_len = i_len;

	ret = ccp_perform_rsa(&op);
	if (ret) {
		cmd->engine_error = cmd_q->cmd_error;
		goto e_dst;
	}

	ccp_reverse_get_dm_area(&dst.dm_wa, rsa->dst, rsa->mod_len);

e_dst:
	ccp_free_data(&dst, cmd_q);

e_src:
	ccp_dm_free(&src);

e_exp:
	ccp_dm_free(&exp);

e_ksb:
	ccp_free_ksb(cmd_q->ccp, op.ksb_key, ksb_count);

	return ret;
}

static int ccp_run_passthru_cmd(struct ccp_cmd_queue *cmd_q,
				struct ccp_cmd *cmd)
{
	struct ccp_passthru_engine *pt = &cmd->u.passthru;
	struct ccp_dm_workarea mask;
	struct ccp_data src, dst;
	struct ccp_op op;
	bool in_place = false;
	unsigned int i;
	int ret;

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

	BUILD_BUG_ON(CCP_PASSTHRU_KSB_COUNT != 1);

	memset(&op, 0, sizeof(op));
	op.cmd_q = cmd_q;
	op.jobid = ccp_gen_jobid(cmd_q->ccp);

	if (pt->bit_mod != CCP_PASSTHRU_BITWISE_NOOP) {
		/* Load the mask */
		op.ksb_key = cmd_q->ksb_key;

		ret = ccp_init_dm_workarea(&mask, cmd_q,
					   CCP_PASSTHRU_KSB_COUNT *
					   CCP_KSB_BYTES,
					   DMA_TO_DEVICE);
		if (ret)
			return ret;

		ccp_set_dm_area(&mask, 0, pt->mask, 0, pt->mask_len);
		ret = ccp_copy_to_ksb(cmd_q, &mask, op.jobid, op.ksb_key,
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

	if (in_place)
		dst = src;
	else {
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
		    (dst.sg_wa.sg->length < src.sg_wa.sg->length)) {
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

		ret = ccp_perform_passthru(&op);
		if (ret) {
			cmd->engine_error = cmd_q->cmd_error;
			goto e_dst;
		}

		dst.sg_wa.sg_used += src.sg_wa.sg->length;
		if (dst.sg_wa.sg_used == dst.sg_wa.sg->length) {
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
	op.jobid = ccp_gen_jobid(cmd_q->ccp);

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
	ccp_reverse_set_dm_area(&src, ecc->mod, ecc->mod_len,
				CCP_ECC_OPERAND_SIZE, false);
	src.address += CCP_ECC_OPERAND_SIZE;

	/* Copy the first operand */
	ccp_reverse_set_dm_area(&src, ecc->u.mm.operand_1,
				ecc->u.mm.operand_1_len,
				CCP_ECC_OPERAND_SIZE, false);
	src.address += CCP_ECC_OPERAND_SIZE;

	if (ecc->function != CCP_ECC_FUNCTION_MINV_384BIT) {
		/* Copy the second operand */
		ccp_reverse_set_dm_area(&src, ecc->u.mm.operand_2,
					ecc->u.mm.operand_2_len,
					CCP_ECC_OPERAND_SIZE, false);
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

	ret = ccp_perform_ecc(&op);
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
	ccp_reverse_get_dm_area(&dst, ecc->u.mm.result, CCP_ECC_MODULUS_BYTES);

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
	op.jobid = ccp_gen_jobid(cmd_q->ccp);

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
	ccp_reverse_set_dm_area(&src, ecc->mod, ecc->mod_len,
				CCP_ECC_OPERAND_SIZE, false);
	src.address += CCP_ECC_OPERAND_SIZE;

	/* Copy the first point X and Y coordinate */
	ccp_reverse_set_dm_area(&src, ecc->u.pm.point_1.x,
				ecc->u.pm.point_1.x_len,
				CCP_ECC_OPERAND_SIZE, false);
	src.address += CCP_ECC_OPERAND_SIZE;
	ccp_reverse_set_dm_area(&src, ecc->u.pm.point_1.y,
				ecc->u.pm.point_1.y_len,
				CCP_ECC_OPERAND_SIZE, false);
	src.address += CCP_ECC_OPERAND_SIZE;

	/* Set the first point Z coordianate to 1 */
	*(src.address) = 0x01;
	src.address += CCP_ECC_OPERAND_SIZE;

	if (ecc->function == CCP_ECC_FUNCTION_PADD_384BIT) {
		/* Copy the second point X and Y coordinate */
		ccp_reverse_set_dm_area(&src, ecc->u.pm.point_2.x,
					ecc->u.pm.point_2.x_len,
					CCP_ECC_OPERAND_SIZE, false);
		src.address += CCP_ECC_OPERAND_SIZE;
		ccp_reverse_set_dm_area(&src, ecc->u.pm.point_2.y,
					ecc->u.pm.point_2.y_len,
					CCP_ECC_OPERAND_SIZE, false);
		src.address += CCP_ECC_OPERAND_SIZE;

		/* Set the second point Z coordianate to 1 */
		*(src.address) = 0x01;
		src.address += CCP_ECC_OPERAND_SIZE;
	} else {
		/* Copy the Domain "a" parameter */
		ccp_reverse_set_dm_area(&src, ecc->u.pm.domain_a,
					ecc->u.pm.domain_a_len,
					CCP_ECC_OPERAND_SIZE, false);
		src.address += CCP_ECC_OPERAND_SIZE;

		if (ecc->function == CCP_ECC_FUNCTION_PMUL_384BIT) {
			/* Copy the scalar value */
			ccp_reverse_set_dm_area(&src, ecc->u.pm.scalar,
						ecc->u.pm.scalar_len,
						CCP_ECC_OPERAND_SIZE, false);
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

	ret = ccp_perform_ecc(&op);
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
	ccp_reverse_get_dm_area(&dst, ecc->u.pm.result.x,
				CCP_ECC_MODULUS_BYTES);
	dst.address += CCP_ECC_OUTPUT_SIZE;
	ccp_reverse_get_dm_area(&dst, ecc->u.pm.result.y,
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

static int ccp_run_ecc_cmd(struct ccp_cmd_queue *cmd_q, struct ccp_cmd *cmd)
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
	cmd_q->free_slots = CMD_Q_DEPTH(ioread32(cmd_q->reg_status));

	switch (cmd->engine) {
	case CCP_ENGINE_AES:
		ret = ccp_run_aes_cmd(cmd_q, cmd);
		break;
	case CCP_ENGINE_XTS_AES_128:
		ret = ccp_run_xts_aes_cmd(cmd_q, cmd);
		break;
	case CCP_ENGINE_SHA:
		ret = ccp_run_sha_cmd(cmd_q, cmd);
		break;
	case CCP_ENGINE_RSA:
		ret = ccp_run_rsa_cmd(cmd_q, cmd);
		break;
	case CCP_ENGINE_PASSTHRU:
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
