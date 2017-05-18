/*
 * AMD Cryptographic Coprocessor (CCP) driver
 *
 * Copyright (C) 2016 Advanced Micro Devices, Inc.
 *
 * Author: Gary R Hook <gary.hook@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kthread.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/compiler.h>
#include <linux/ccp.h>

#include "ccp-dev.h"

/* Allocate the requested number of contiguous LSB slots
 * from the LSB bitmap. Look in the private range for this
 * queue first; failing that, check the public area.
 * If no space is available, wait around.
 * Return: first slot number
 */
static u32 ccp_lsb_alloc(struct ccp_cmd_queue *cmd_q, unsigned int count)
{
	struct ccp_device *ccp;
	int start;

	/* First look at the map for the queue */
	if (cmd_q->lsb >= 0) {
		start = (u32)bitmap_find_next_zero_area(cmd_q->lsbmap,
							LSB_SIZE,
							0, count, 0);
		if (start < LSB_SIZE) {
			bitmap_set(cmd_q->lsbmap, start, count);
			return start + cmd_q->lsb * LSB_SIZE;
		}
	}

	/* No joy; try to get an entry from the shared blocks */
	ccp = cmd_q->ccp;
	for (;;) {
		mutex_lock(&ccp->sb_mutex);

		start = (u32)bitmap_find_next_zero_area(ccp->lsbmap,
							MAX_LSB_CNT * LSB_SIZE,
							0,
							count, 0);
		if (start <= MAX_LSB_CNT * LSB_SIZE) {
			bitmap_set(ccp->lsbmap, start, count);

			mutex_unlock(&ccp->sb_mutex);
			return start;
		}

		ccp->sb_avail = 0;

		mutex_unlock(&ccp->sb_mutex);

		/* Wait for KSB entries to become available */
		if (wait_event_interruptible(ccp->sb_queue, ccp->sb_avail))
			return 0;
	}
}

/* Free a number of LSB slots from the bitmap, starting at
 * the indicated starting slot number.
 */
static void ccp_lsb_free(struct ccp_cmd_queue *cmd_q, unsigned int start,
			 unsigned int count)
{
	if (!start)
		return;

	if (cmd_q->lsb == start) {
		/* An entry from the private LSB */
		bitmap_clear(cmd_q->lsbmap, start, count);
	} else {
		/* From the shared LSBs */
		struct ccp_device *ccp = cmd_q->ccp;

		mutex_lock(&ccp->sb_mutex);
		bitmap_clear(ccp->lsbmap, start, count);
		ccp->sb_avail = 1;
		mutex_unlock(&ccp->sb_mutex);
		wake_up_interruptible_all(&ccp->sb_queue);
	}
}

/* CCP version 5: Union to define the function field (cmd_reg1/dword0) */
union ccp_function {
	struct {
		u16 size:7;
		u16 encrypt:1;
		u16 mode:5;
		u16 type:2;
	} aes;
	struct {
		u16 size:7;
		u16 encrypt:1;
		u16 rsvd:5;
		u16 type:2;
	} aes_xts;
	struct {
		u16 size:7;
		u16 encrypt:1;
		u16 mode:5;
		u16 type:2;
	} des3;
	struct {
		u16 rsvd1:10;
		u16 type:4;
		u16 rsvd2:1;
	} sha;
	struct {
		u16 mode:3;
		u16 size:12;
	} rsa;
	struct {
		u16 byteswap:2;
		u16 bitwise:3;
		u16 reflect:2;
		u16 rsvd:8;
	} pt;
	struct  {
		u16 rsvd:13;
	} zlib;
	struct {
		u16 size:10;
		u16 type:2;
		u16 mode:3;
	} ecc;
	u16 raw;
};

#define	CCP_AES_SIZE(p)		((p)->aes.size)
#define	CCP_AES_ENCRYPT(p)	((p)->aes.encrypt)
#define	CCP_AES_MODE(p)		((p)->aes.mode)
#define	CCP_AES_TYPE(p)		((p)->aes.type)
#define	CCP_XTS_SIZE(p)		((p)->aes_xts.size)
#define	CCP_XTS_ENCRYPT(p)	((p)->aes_xts.encrypt)
#define	CCP_DES3_SIZE(p)	((p)->des3.size)
#define	CCP_DES3_ENCRYPT(p)	((p)->des3.encrypt)
#define	CCP_DES3_MODE(p)	((p)->des3.mode)
#define	CCP_DES3_TYPE(p)	((p)->des3.type)
#define	CCP_SHA_TYPE(p)		((p)->sha.type)
#define	CCP_RSA_SIZE(p)		((p)->rsa.size)
#define	CCP_PT_BYTESWAP(p)	((p)->pt.byteswap)
#define	CCP_PT_BITWISE(p)	((p)->pt.bitwise)
#define	CCP_ECC_MODE(p)		((p)->ecc.mode)
#define	CCP_ECC_AFFINE(p)	((p)->ecc.one)

/* Word 0 */
#define CCP5_CMD_DW0(p)		((p)->dw0)
#define CCP5_CMD_SOC(p)		(CCP5_CMD_DW0(p).soc)
#define CCP5_CMD_IOC(p)		(CCP5_CMD_DW0(p).ioc)
#define CCP5_CMD_INIT(p)	(CCP5_CMD_DW0(p).init)
#define CCP5_CMD_EOM(p)		(CCP5_CMD_DW0(p).eom)
#define CCP5_CMD_FUNCTION(p)	(CCP5_CMD_DW0(p).function)
#define CCP5_CMD_ENGINE(p)	(CCP5_CMD_DW0(p).engine)
#define CCP5_CMD_PROT(p)	(CCP5_CMD_DW0(p).prot)

/* Word 1 */
#define CCP5_CMD_DW1(p)		((p)->length)
#define CCP5_CMD_LEN(p)		(CCP5_CMD_DW1(p))

/* Word 2 */
#define CCP5_CMD_DW2(p)		((p)->src_lo)
#define CCP5_CMD_SRC_LO(p)	(CCP5_CMD_DW2(p))

/* Word 3 */
#define CCP5_CMD_DW3(p)		((p)->dw3)
#define CCP5_CMD_SRC_MEM(p)	((p)->dw3.src_mem)
#define CCP5_CMD_SRC_HI(p)	((p)->dw3.src_hi)
#define CCP5_CMD_LSB_ID(p)	((p)->dw3.lsb_cxt_id)
#define CCP5_CMD_FIX_SRC(p)	((p)->dw3.fixed)

/* Words 4/5 */
#define CCP5_CMD_DW4(p)		((p)->dw4)
#define CCP5_CMD_DST_LO(p)	(CCP5_CMD_DW4(p).dst_lo)
#define CCP5_CMD_DW5(p)		((p)->dw5.fields.dst_hi)
#define CCP5_CMD_DST_HI(p)	(CCP5_CMD_DW5(p))
#define CCP5_CMD_DST_MEM(p)	((p)->dw5.fields.dst_mem)
#define CCP5_CMD_FIX_DST(p)	((p)->dw5.fields.fixed)
#define CCP5_CMD_SHA_LO(p)	((p)->dw4.sha_len_lo)
#define CCP5_CMD_SHA_HI(p)	((p)->dw5.sha_len_hi)

/* Word 6/7 */
#define CCP5_CMD_DW6(p)		((p)->key_lo)
#define CCP5_CMD_KEY_LO(p)	(CCP5_CMD_DW6(p))
#define CCP5_CMD_DW7(p)		((p)->dw7)
#define CCP5_CMD_KEY_HI(p)	((p)->dw7.key_hi)
#define CCP5_CMD_KEY_MEM(p)	((p)->dw7.key_mem)

static inline u32 low_address(unsigned long addr)
{
	return (u64)addr & 0x0ffffffff;
}

static inline u32 high_address(unsigned long addr)
{
	return ((u64)addr >> 32) & 0x00000ffff;
}

static unsigned int ccp5_get_free_slots(struct ccp_cmd_queue *cmd_q)
{
	unsigned int head_idx, n;
	u32 head_lo, queue_start;

	queue_start = low_address(cmd_q->qdma_tail);
	head_lo = ioread32(cmd_q->reg_head_lo);
	head_idx = (head_lo - queue_start) / sizeof(struct ccp5_desc);

	n = head_idx + COMMANDS_PER_QUEUE - cmd_q->qidx - 1;

	return n % COMMANDS_PER_QUEUE; /* Always one unused spot */
}

static int ccp5_do_cmd(struct ccp5_desc *desc,
		       struct ccp_cmd_queue *cmd_q)
{
	u32 *mP;
	__le32 *dP;
	u32 tail;
	int	i;
	int ret = 0;

	if (CCP5_CMD_SOC(desc)) {
		CCP5_CMD_IOC(desc) = 1;
		CCP5_CMD_SOC(desc) = 0;
	}
	mutex_lock(&cmd_q->q_mutex);

	mP = (u32 *) &cmd_q->qbase[cmd_q->qidx];
	dP = (__le32 *) desc;
	for (i = 0; i < 8; i++)
		mP[i] = cpu_to_le32(dP[i]); /* handle endianness */

	cmd_q->qidx = (cmd_q->qidx + 1) % COMMANDS_PER_QUEUE;

	/* The data used by this command must be flushed to memory */
	wmb();

	/* Write the new tail address back to the queue register */
	tail = low_address(cmd_q->qdma_tail + cmd_q->qidx * Q_DESC_SIZE);
	iowrite32(tail, cmd_q->reg_tail_lo);

	/* Turn the queue back on using our cached control register */
	iowrite32(cmd_q->qcontrol | CMD5_Q_RUN, cmd_q->reg_control);
	mutex_unlock(&cmd_q->q_mutex);

	if (CCP5_CMD_IOC(desc)) {
		/* Wait for the job to complete */
		ret = wait_event_interruptible(cmd_q->int_queue,
					       cmd_q->int_rcvd);
		if (ret || cmd_q->cmd_error) {
			/* Log the error and flush the queue by
			 * moving the head pointer
			 */
			if (cmd_q->cmd_error)
				ccp_log_error(cmd_q->ccp,
					      cmd_q->cmd_error);
			iowrite32(tail, cmd_q->reg_head_lo);
			if (!ret)
				ret = -EIO;
		}
		cmd_q->int_rcvd = 0;
	}

	return ret;
}

static int ccp5_perform_aes(struct ccp_op *op)
{
	struct ccp5_desc desc;
	union ccp_function function;
	u32 key_addr = op->sb_key * LSB_ITEM_SIZE;

	/* Zero out all the fields of the command desc */
	memset(&desc, 0, Q_DESC_SIZE);

	CCP5_CMD_ENGINE(&desc) = CCP_ENGINE_AES;

	CCP5_CMD_SOC(&desc) = op->soc;
	CCP5_CMD_IOC(&desc) = 1;
	CCP5_CMD_INIT(&desc) = op->init;
	CCP5_CMD_EOM(&desc) = op->eom;
	CCP5_CMD_PROT(&desc) = 0;

	function.raw = 0;
	CCP_AES_ENCRYPT(&function) = op->u.aes.action;
	CCP_AES_MODE(&function) = op->u.aes.mode;
	CCP_AES_TYPE(&function) = op->u.aes.type;
	CCP_AES_SIZE(&function) = op->u.aes.size;

	CCP5_CMD_FUNCTION(&desc) = function.raw;

	CCP5_CMD_LEN(&desc) = op->src.u.dma.length;

	CCP5_CMD_SRC_LO(&desc) = ccp_addr_lo(&op->src.u.dma);
	CCP5_CMD_SRC_HI(&desc) = ccp_addr_hi(&op->src.u.dma);
	CCP5_CMD_SRC_MEM(&desc) = CCP_MEMTYPE_SYSTEM;

	CCP5_CMD_DST_LO(&desc) = ccp_addr_lo(&op->dst.u.dma);
	CCP5_CMD_DST_HI(&desc) = ccp_addr_hi(&op->dst.u.dma);
	CCP5_CMD_DST_MEM(&desc) = CCP_MEMTYPE_SYSTEM;

	CCP5_CMD_KEY_LO(&desc) = lower_32_bits(key_addr);
	CCP5_CMD_KEY_HI(&desc) = 0;
	CCP5_CMD_KEY_MEM(&desc) = CCP_MEMTYPE_SB;
	CCP5_CMD_LSB_ID(&desc) = op->sb_ctx;

	return ccp5_do_cmd(&desc, op->cmd_q);
}

static int ccp5_perform_xts_aes(struct ccp_op *op)
{
	struct ccp5_desc desc;
	union ccp_function function;
	u32 key_addr = op->sb_key * LSB_ITEM_SIZE;

	/* Zero out all the fields of the command desc */
	memset(&desc, 0, Q_DESC_SIZE);

	CCP5_CMD_ENGINE(&desc) = CCP_ENGINE_XTS_AES_128;

	CCP5_CMD_SOC(&desc) = op->soc;
	CCP5_CMD_IOC(&desc) = 1;
	CCP5_CMD_INIT(&desc) = op->init;
	CCP5_CMD_EOM(&desc) = op->eom;
	CCP5_CMD_PROT(&desc) = 0;

	function.raw = 0;
	CCP_XTS_ENCRYPT(&function) = op->u.xts.action;
	CCP_XTS_SIZE(&function) = op->u.xts.unit_size;
	CCP5_CMD_FUNCTION(&desc) = function.raw;

	CCP5_CMD_LEN(&desc) = op->src.u.dma.length;

	CCP5_CMD_SRC_LO(&desc) = ccp_addr_lo(&op->src.u.dma);
	CCP5_CMD_SRC_HI(&desc) = ccp_addr_hi(&op->src.u.dma);
	CCP5_CMD_SRC_MEM(&desc) = CCP_MEMTYPE_SYSTEM;

	CCP5_CMD_DST_LO(&desc) = ccp_addr_lo(&op->dst.u.dma);
	CCP5_CMD_DST_HI(&desc) = ccp_addr_hi(&op->dst.u.dma);
	CCP5_CMD_DST_MEM(&desc) = CCP_MEMTYPE_SYSTEM;

	CCP5_CMD_KEY_LO(&desc) = lower_32_bits(key_addr);
	CCP5_CMD_KEY_HI(&desc) =  0;
	CCP5_CMD_KEY_MEM(&desc) = CCP_MEMTYPE_SB;
	CCP5_CMD_LSB_ID(&desc) = op->sb_ctx;

	return ccp5_do_cmd(&desc, op->cmd_q);
}

static int ccp5_perform_sha(struct ccp_op *op)
{
	struct ccp5_desc desc;
	union ccp_function function;

	/* Zero out all the fields of the command desc */
	memset(&desc, 0, Q_DESC_SIZE);

	CCP5_CMD_ENGINE(&desc) = CCP_ENGINE_SHA;

	CCP5_CMD_SOC(&desc) = op->soc;
	CCP5_CMD_IOC(&desc) = 1;
	CCP5_CMD_INIT(&desc) = 1;
	CCP5_CMD_EOM(&desc) = op->eom;
	CCP5_CMD_PROT(&desc) = 0;

	function.raw = 0;
	CCP_SHA_TYPE(&function) = op->u.sha.type;
	CCP5_CMD_FUNCTION(&desc) = function.raw;

	CCP5_CMD_LEN(&desc) = op->src.u.dma.length;

	CCP5_CMD_SRC_LO(&desc) = ccp_addr_lo(&op->src.u.dma);
	CCP5_CMD_SRC_HI(&desc) = ccp_addr_hi(&op->src.u.dma);
	CCP5_CMD_SRC_MEM(&desc) = CCP_MEMTYPE_SYSTEM;

	CCP5_CMD_LSB_ID(&desc) = op->sb_ctx;

	if (op->eom) {
		CCP5_CMD_SHA_LO(&desc) = lower_32_bits(op->u.sha.msg_bits);
		CCP5_CMD_SHA_HI(&desc) = upper_32_bits(op->u.sha.msg_bits);
	} else {
		CCP5_CMD_SHA_LO(&desc) = 0;
		CCP5_CMD_SHA_HI(&desc) = 0;
	}

	return ccp5_do_cmd(&desc, op->cmd_q);
}

static int ccp5_perform_des3(struct ccp_op *op)
{
	struct ccp5_desc desc;
	union ccp_function function;
	u32 key_addr = op->sb_key * LSB_ITEM_SIZE;

	/* Zero out all the fields of the command desc */
	memset(&desc, 0, sizeof(struct ccp5_desc));

	CCP5_CMD_ENGINE(&desc) = CCP_ENGINE_DES3;

	CCP5_CMD_SOC(&desc) = op->soc;
	CCP5_CMD_IOC(&desc) = 1;
	CCP5_CMD_INIT(&desc) = op->init;
	CCP5_CMD_EOM(&desc) = op->eom;
	CCP5_CMD_PROT(&desc) = 0;

	function.raw = 0;
	CCP_DES3_ENCRYPT(&function) = op->u.des3.action;
	CCP_DES3_MODE(&function) = op->u.des3.mode;
	CCP_DES3_TYPE(&function) = op->u.des3.type;
	CCP5_CMD_FUNCTION(&desc) = function.raw;

	CCP5_CMD_LEN(&desc) = op->src.u.dma.length;

	CCP5_CMD_SRC_LO(&desc) = ccp_addr_lo(&op->src.u.dma);
	CCP5_CMD_SRC_HI(&desc) = ccp_addr_hi(&op->src.u.dma);
	CCP5_CMD_SRC_MEM(&desc) = CCP_MEMTYPE_SYSTEM;

	CCP5_CMD_DST_LO(&desc) = ccp_addr_lo(&op->dst.u.dma);
	CCP5_CMD_DST_HI(&desc) = ccp_addr_hi(&op->dst.u.dma);
	CCP5_CMD_DST_MEM(&desc) = CCP_MEMTYPE_SYSTEM;

	CCP5_CMD_KEY_LO(&desc) = lower_32_bits(key_addr);
	CCP5_CMD_KEY_HI(&desc) = 0;
	CCP5_CMD_KEY_MEM(&desc) = CCP_MEMTYPE_SB;
	CCP5_CMD_LSB_ID(&desc) = op->sb_ctx;

	return ccp5_do_cmd(&desc, op->cmd_q);
}

static int ccp5_perform_rsa(struct ccp_op *op)
{
	struct ccp5_desc desc;
	union ccp_function function;

	/* Zero out all the fields of the command desc */
	memset(&desc, 0, Q_DESC_SIZE);

	CCP5_CMD_ENGINE(&desc) = CCP_ENGINE_RSA;

	CCP5_CMD_SOC(&desc) = op->soc;
	CCP5_CMD_IOC(&desc) = 1;
	CCP5_CMD_INIT(&desc) = 0;
	CCP5_CMD_EOM(&desc) = 1;
	CCP5_CMD_PROT(&desc) = 0;

	function.raw = 0;
	CCP_RSA_SIZE(&function) = op->u.rsa.mod_size >> 3;
	CCP5_CMD_FUNCTION(&desc) = function.raw;

	CCP5_CMD_LEN(&desc) = op->u.rsa.input_len;

	/* Source is from external memory */
	CCP5_CMD_SRC_LO(&desc) = ccp_addr_lo(&op->src.u.dma);
	CCP5_CMD_SRC_HI(&desc) = ccp_addr_hi(&op->src.u.dma);
	CCP5_CMD_SRC_MEM(&desc) = CCP_MEMTYPE_SYSTEM;

	/* Destination is in external memory */
	CCP5_CMD_DST_LO(&desc) = ccp_addr_lo(&op->dst.u.dma);
	CCP5_CMD_DST_HI(&desc) = ccp_addr_hi(&op->dst.u.dma);
	CCP5_CMD_DST_MEM(&desc) = CCP_MEMTYPE_SYSTEM;

	/* Exponent is in LSB memory */
	CCP5_CMD_KEY_LO(&desc) = op->sb_key * LSB_ITEM_SIZE;
	CCP5_CMD_KEY_HI(&desc) = 0;
	CCP5_CMD_KEY_MEM(&desc) = CCP_MEMTYPE_SB;

	return ccp5_do_cmd(&desc, op->cmd_q);
}

static int ccp5_perform_passthru(struct ccp_op *op)
{
	struct ccp5_desc desc;
	union ccp_function function;
	struct ccp_dma_info *saddr = &op->src.u.dma;
	struct ccp_dma_info *daddr = &op->dst.u.dma;


	memset(&desc, 0, Q_DESC_SIZE);

	CCP5_CMD_ENGINE(&desc) = CCP_ENGINE_PASSTHRU;

	CCP5_CMD_SOC(&desc) = 0;
	CCP5_CMD_IOC(&desc) = 1;
	CCP5_CMD_INIT(&desc) = 0;
	CCP5_CMD_EOM(&desc) = op->eom;
	CCP5_CMD_PROT(&desc) = 0;

	function.raw = 0;
	CCP_PT_BYTESWAP(&function) = op->u.passthru.byte_swap;
	CCP_PT_BITWISE(&function) = op->u.passthru.bit_mod;
	CCP5_CMD_FUNCTION(&desc) = function.raw;

	/* Length of source data is always 256 bytes */
	if (op->src.type == CCP_MEMTYPE_SYSTEM)
		CCP5_CMD_LEN(&desc) = saddr->length;
	else
		CCP5_CMD_LEN(&desc) = daddr->length;

	if (op->src.type == CCP_MEMTYPE_SYSTEM) {
		CCP5_CMD_SRC_LO(&desc) = ccp_addr_lo(&op->src.u.dma);
		CCP5_CMD_SRC_HI(&desc) = ccp_addr_hi(&op->src.u.dma);
		CCP5_CMD_SRC_MEM(&desc) = CCP_MEMTYPE_SYSTEM;

		if (op->u.passthru.bit_mod != CCP_PASSTHRU_BITWISE_NOOP)
			CCP5_CMD_LSB_ID(&desc) = op->sb_key;
	} else {
		u32 key_addr = op->src.u.sb * CCP_SB_BYTES;

		CCP5_CMD_SRC_LO(&desc) = lower_32_bits(key_addr);
		CCP5_CMD_SRC_HI(&desc) = 0;
		CCP5_CMD_SRC_MEM(&desc) = CCP_MEMTYPE_SB;
	}

	if (op->dst.type == CCP_MEMTYPE_SYSTEM) {
		CCP5_CMD_DST_LO(&desc) = ccp_addr_lo(&op->dst.u.dma);
		CCP5_CMD_DST_HI(&desc) = ccp_addr_hi(&op->dst.u.dma);
		CCP5_CMD_DST_MEM(&desc) = CCP_MEMTYPE_SYSTEM;
	} else {
		u32 key_addr = op->dst.u.sb * CCP_SB_BYTES;

		CCP5_CMD_DST_LO(&desc) = lower_32_bits(key_addr);
		CCP5_CMD_DST_HI(&desc) = 0;
		CCP5_CMD_DST_MEM(&desc) = CCP_MEMTYPE_SB;
	}

	return ccp5_do_cmd(&desc, op->cmd_q);
}

static int ccp5_perform_ecc(struct ccp_op *op)
{
	struct ccp5_desc desc;
	union ccp_function function;

	/* Zero out all the fields of the command desc */
	memset(&desc, 0, Q_DESC_SIZE);

	CCP5_CMD_ENGINE(&desc) = CCP_ENGINE_ECC;

	CCP5_CMD_SOC(&desc) = 0;
	CCP5_CMD_IOC(&desc) = 1;
	CCP5_CMD_INIT(&desc) = 0;
	CCP5_CMD_EOM(&desc) = 1;
	CCP5_CMD_PROT(&desc) = 0;

	function.raw = 0;
	function.ecc.mode = op->u.ecc.function;
	CCP5_CMD_FUNCTION(&desc) = function.raw;

	CCP5_CMD_LEN(&desc) = op->src.u.dma.length;

	CCP5_CMD_SRC_LO(&desc) = ccp_addr_lo(&op->src.u.dma);
	CCP5_CMD_SRC_HI(&desc) = ccp_addr_hi(&op->src.u.dma);
	CCP5_CMD_SRC_MEM(&desc) = CCP_MEMTYPE_SYSTEM;

	CCP5_CMD_DST_LO(&desc) = ccp_addr_lo(&op->dst.u.dma);
	CCP5_CMD_DST_HI(&desc) = ccp_addr_hi(&op->dst.u.dma);
	CCP5_CMD_DST_MEM(&desc) = CCP_MEMTYPE_SYSTEM;

	return ccp5_do_cmd(&desc, op->cmd_q);
}

static int ccp_find_lsb_regions(struct ccp_cmd_queue *cmd_q, u64 status)
{
	int q_mask = 1 << cmd_q->id;
	int queues = 0;
	int j;

	/* Build a bit mask to know which LSBs this queue has access to.
	 * Don't bother with segment 0 as it has special privileges.
	 */
	for (j = 1; j < MAX_LSB_CNT; j++) {
		if (status & q_mask)
			bitmap_set(cmd_q->lsbmask, j, 1);
		status >>= LSB_REGION_WIDTH;
	}
	queues = bitmap_weight(cmd_q->lsbmask, MAX_LSB_CNT);
	dev_dbg(cmd_q->ccp->dev, "Queue %d can access %d LSB regions\n",
		 cmd_q->id, queues);

	return queues ? 0 : -EINVAL;
}


static int ccp_find_and_assign_lsb_to_q(struct ccp_device *ccp,
					int lsb_cnt, int n_lsbs,
					unsigned long *lsb_pub)
{
	DECLARE_BITMAP(qlsb, MAX_LSB_CNT);
	int bitno;
	int qlsb_wgt;
	int i;

	/* For each queue:
	 * If the count of potential LSBs available to a queue matches the
	 * ordinal given to us in lsb_cnt:
	 * Copy the mask of possible LSBs for this queue into "qlsb";
	 * For each bit in qlsb, see if the corresponding bit in the
	 * aggregation mask is set; if so, we have a match.
	 *     If we have a match, clear the bit in the aggregation to
	 *     mark it as no longer available.
	 *     If there is no match, clear the bit in qlsb and keep looking.
	 */
	for (i = 0; i < ccp->cmd_q_count; i++) {
		struct ccp_cmd_queue *cmd_q = &ccp->cmd_q[i];

		qlsb_wgt = bitmap_weight(cmd_q->lsbmask, MAX_LSB_CNT);

		if (qlsb_wgt == lsb_cnt) {
			bitmap_copy(qlsb, cmd_q->lsbmask, MAX_LSB_CNT);

			bitno = find_first_bit(qlsb, MAX_LSB_CNT);
			while (bitno < MAX_LSB_CNT) {
				if (test_bit(bitno, lsb_pub)) {
					/* We found an available LSB
					 * that this queue can access
					 */
					cmd_q->lsb = bitno;
					bitmap_clear(lsb_pub, bitno, 1);
					dev_dbg(ccp->dev,
						 "Queue %d gets LSB %d\n",
						 i, bitno);
					break;
				}
				bitmap_clear(qlsb, bitno, 1);
				bitno = find_first_bit(qlsb, MAX_LSB_CNT);
			}
			if (bitno >= MAX_LSB_CNT)
				return -EINVAL;
			n_lsbs--;
		}
	}
	return n_lsbs;
}

/* For each queue, from the most- to least-constrained:
 * find an LSB that can be assigned to the queue. If there are N queues that
 * can only use M LSBs, where N > M, fail; otherwise, every queue will get a
 * dedicated LSB. Remaining LSB regions become a shared resource.
 * If we have fewer LSBs than queues, all LSB regions become shared resources.
 */
static int ccp_assign_lsbs(struct ccp_device *ccp)
{
	DECLARE_BITMAP(lsb_pub, MAX_LSB_CNT);
	DECLARE_BITMAP(qlsb, MAX_LSB_CNT);
	int n_lsbs = 0;
	int bitno;
	int i, lsb_cnt;
	int rc = 0;

	bitmap_zero(lsb_pub, MAX_LSB_CNT);

	/* Create an aggregate bitmap to get a total count of available LSBs */
	for (i = 0; i < ccp->cmd_q_count; i++)
		bitmap_or(lsb_pub,
			  lsb_pub, ccp->cmd_q[i].lsbmask,
			  MAX_LSB_CNT);

	n_lsbs = bitmap_weight(lsb_pub, MAX_LSB_CNT);

	if (n_lsbs >= ccp->cmd_q_count) {
		/* We have enough LSBS to give every queue a private LSB.
		 * Brute force search to start with the queues that are more
		 * constrained in LSB choice. When an LSB is privately
		 * assigned, it is removed from the public mask.
		 * This is an ugly N squared algorithm with some optimization.
		 */
		for (lsb_cnt = 1;
		     n_lsbs && (lsb_cnt <= MAX_LSB_CNT);
		     lsb_cnt++) {
			rc = ccp_find_and_assign_lsb_to_q(ccp, lsb_cnt, n_lsbs,
							  lsb_pub);
			if (rc < 0)
				return -EINVAL;
			n_lsbs = rc;
		}
	}

	rc = 0;
	/* What's left of the LSBs, according to the public mask, now become
	 * shared. Any zero bits in the lsb_pub mask represent an LSB region
	 * that can't be used as a shared resource, so mark the LSB slots for
	 * them as "in use".
	 */
	bitmap_copy(qlsb, lsb_pub, MAX_LSB_CNT);

	bitno = find_first_zero_bit(qlsb, MAX_LSB_CNT);
	while (bitno < MAX_LSB_CNT) {
		bitmap_set(ccp->lsbmap, bitno * LSB_SIZE, LSB_SIZE);
		bitmap_set(qlsb, bitno, 1);
		bitno = find_first_zero_bit(qlsb, MAX_LSB_CNT);
	}

	return rc;
}

static void ccp5_disable_queue_interrupts(struct ccp_device *ccp)
{
	unsigned int i;

	for (i = 0; i < ccp->cmd_q_count; i++)
		iowrite32(0x0, ccp->cmd_q[i].reg_int_enable);
}

static void ccp5_enable_queue_interrupts(struct ccp_device *ccp)
{
	unsigned int i;

	for (i = 0; i < ccp->cmd_q_count; i++)
		iowrite32(SUPPORTED_INTERRUPTS, ccp->cmd_q[i].reg_int_enable);
}

static void ccp5_irq_bh(unsigned long data)
{
	struct ccp_device *ccp = (struct ccp_device *)data;
	u32 status;
	unsigned int i;

	for (i = 0; i < ccp->cmd_q_count; i++) {
		struct ccp_cmd_queue *cmd_q = &ccp->cmd_q[i];

		status = ioread32(cmd_q->reg_interrupt_status);

		if (status) {
			cmd_q->int_status = status;
			cmd_q->q_status = ioread32(cmd_q->reg_status);
			cmd_q->q_int_status = ioread32(cmd_q->reg_int_status);

			/* On error, only save the first error value */
			if ((status & INT_ERROR) && !cmd_q->cmd_error)
				cmd_q->cmd_error = CMD_Q_ERROR(cmd_q->q_status);

			cmd_q->int_rcvd = 1;

			/* Acknowledge the interrupt and wake the kthread */
			iowrite32(status, cmd_q->reg_interrupt_status);
			wake_up_interruptible(&cmd_q->int_queue);
		}
	}
	ccp5_enable_queue_interrupts(ccp);
}

static irqreturn_t ccp5_irq_handler(int irq, void *data)
{
	struct device *dev = data;
	struct ccp_device *ccp = dev_get_drvdata(dev);

	ccp5_disable_queue_interrupts(ccp);
	if (ccp->use_tasklet)
		tasklet_schedule(&ccp->irq_tasklet);
	else
		ccp5_irq_bh((unsigned long)ccp);
	return IRQ_HANDLED;
}

static int ccp5_init(struct ccp_device *ccp)
{
	struct device *dev = ccp->dev;
	struct ccp_cmd_queue *cmd_q;
	struct dma_pool *dma_pool;
	char dma_pool_name[MAX_DMAPOOL_NAME_LEN];
	unsigned int qmr, qim, i;
	u64 status;
	u32 status_lo, status_hi;
	int ret;

	/* Find available queues */
	qim = 0;
	qmr = ioread32(ccp->io_regs + Q_MASK_REG);
	for (i = 0; i < MAX_HW_QUEUES; i++) {

		if (!(qmr & (1 << i)))
			continue;

		/* Allocate a dma pool for this queue */
		snprintf(dma_pool_name, sizeof(dma_pool_name), "%s_q%d",
			 ccp->name, i);
		dma_pool = dma_pool_create(dma_pool_name, dev,
					   CCP_DMAPOOL_MAX_SIZE,
					   CCP_DMAPOOL_ALIGN, 0);
		if (!dma_pool) {
			dev_err(dev, "unable to allocate dma pool\n");
			ret = -ENOMEM;
		}

		cmd_q = &ccp->cmd_q[ccp->cmd_q_count];
		ccp->cmd_q_count++;

		cmd_q->ccp = ccp;
		cmd_q->id = i;
		cmd_q->dma_pool = dma_pool;
		mutex_init(&cmd_q->q_mutex);

		/* Page alignment satisfies our needs for N <= 128 */
		BUILD_BUG_ON(COMMANDS_PER_QUEUE > 128);
		cmd_q->qsize = Q_SIZE(Q_DESC_SIZE);
		cmd_q->qbase = dma_zalloc_coherent(dev, cmd_q->qsize,
						   &cmd_q->qbase_dma,
						   GFP_KERNEL);
		if (!cmd_q->qbase) {
			dev_err(dev, "unable to allocate command queue\n");
			ret = -ENOMEM;
			goto e_pool;
		}

		cmd_q->qidx = 0;
		/* Preset some register values and masks that are queue
		 * number dependent
		 */
		cmd_q->reg_control = ccp->io_regs +
				     CMD5_Q_STATUS_INCR * (i + 1);
		cmd_q->reg_tail_lo = cmd_q->reg_control + CMD5_Q_TAIL_LO_BASE;
		cmd_q->reg_head_lo = cmd_q->reg_control + CMD5_Q_HEAD_LO_BASE;
		cmd_q->reg_int_enable = cmd_q->reg_control +
					CMD5_Q_INT_ENABLE_BASE;
		cmd_q->reg_interrupt_status = cmd_q->reg_control +
					      CMD5_Q_INTERRUPT_STATUS_BASE;
		cmd_q->reg_status = cmd_q->reg_control + CMD5_Q_STATUS_BASE;
		cmd_q->reg_int_status = cmd_q->reg_control +
					CMD5_Q_INT_STATUS_BASE;
		cmd_q->reg_dma_status = cmd_q->reg_control +
					CMD5_Q_DMA_STATUS_BASE;
		cmd_q->reg_dma_read_status = cmd_q->reg_control +
					     CMD5_Q_DMA_READ_STATUS_BASE;
		cmd_q->reg_dma_write_status = cmd_q->reg_control +
					      CMD5_Q_DMA_WRITE_STATUS_BASE;

		init_waitqueue_head(&cmd_q->int_queue);

		dev_dbg(dev, "queue #%u available\n", i);
	}

	if (ccp->cmd_q_count == 0) {
		dev_notice(dev, "no command queues available\n");
		ret = -EIO;
		goto e_pool;
	}

	/* Turn off the queues and disable interrupts until ready */
	ccp5_disable_queue_interrupts(ccp);
	for (i = 0; i < ccp->cmd_q_count; i++) {
		cmd_q = &ccp->cmd_q[i];

		cmd_q->qcontrol = 0; /* Start with nothing */
		iowrite32(cmd_q->qcontrol, cmd_q->reg_control);

		ioread32(cmd_q->reg_int_status);
		ioread32(cmd_q->reg_status);

		/* Clear the interrupt status */
		iowrite32(SUPPORTED_INTERRUPTS, cmd_q->reg_interrupt_status);
	}

	dev_dbg(dev, "Requesting an IRQ...\n");
	/* Request an irq */
	ret = ccp->get_irq(ccp);
	if (ret) {
		dev_err(dev, "unable to allocate an IRQ\n");
		goto e_pool;
	}
	/* Initialize the ISR tasklet */
	if (ccp->use_tasklet)
		tasklet_init(&ccp->irq_tasklet, ccp5_irq_bh,
			     (unsigned long)ccp);

	dev_dbg(dev, "Loading LSB map...\n");
	/* Copy the private LSB mask to the public registers */
	status_lo = ioread32(ccp->io_regs + LSB_PRIVATE_MASK_LO_OFFSET);
	status_hi = ioread32(ccp->io_regs + LSB_PRIVATE_MASK_HI_OFFSET);
	iowrite32(status_lo, ccp->io_regs + LSB_PUBLIC_MASK_LO_OFFSET);
	iowrite32(status_hi, ccp->io_regs + LSB_PUBLIC_MASK_HI_OFFSET);
	status = ((u64)status_hi<<30) | (u64)status_lo;

	dev_dbg(dev, "Configuring virtual queues...\n");
	/* Configure size of each virtual queue accessible to host */
	for (i = 0; i < ccp->cmd_q_count; i++) {
		u32 dma_addr_lo;
		u32 dma_addr_hi;

		cmd_q = &ccp->cmd_q[i];

		cmd_q->qcontrol &= ~(CMD5_Q_SIZE << CMD5_Q_SHIFT);
		cmd_q->qcontrol |= QUEUE_SIZE_VAL << CMD5_Q_SHIFT;

		cmd_q->qdma_tail = cmd_q->qbase_dma;
		dma_addr_lo = low_address(cmd_q->qdma_tail);
		iowrite32((u32)dma_addr_lo, cmd_q->reg_tail_lo);
		iowrite32((u32)dma_addr_lo, cmd_q->reg_head_lo);

		dma_addr_hi = high_address(cmd_q->qdma_tail);
		cmd_q->qcontrol |= (dma_addr_hi << 16);
		iowrite32(cmd_q->qcontrol, cmd_q->reg_control);

		/* Find the LSB regions accessible to the queue */
		ccp_find_lsb_regions(cmd_q, status);
		cmd_q->lsb = -1; /* Unassigned value */
	}

	dev_dbg(dev, "Assigning LSBs...\n");
	ret = ccp_assign_lsbs(ccp);
	if (ret) {
		dev_err(dev, "Unable to assign LSBs (%d)\n", ret);
		goto e_irq;
	}

	/* Optimization: pre-allocate LSB slots for each queue */
	for (i = 0; i < ccp->cmd_q_count; i++) {
		ccp->cmd_q[i].sb_key = ccp_lsb_alloc(&ccp->cmd_q[i], 2);
		ccp->cmd_q[i].sb_ctx = ccp_lsb_alloc(&ccp->cmd_q[i], 2);
	}

	dev_dbg(dev, "Starting threads...\n");
	/* Create a kthread for each queue */
	for (i = 0; i < ccp->cmd_q_count; i++) {
		struct task_struct *kthread;

		cmd_q = &ccp->cmd_q[i];

		kthread = kthread_create(ccp_cmd_queue_thread, cmd_q,
					 "%s-q%u", ccp->name, cmd_q->id);
		if (IS_ERR(kthread)) {
			dev_err(dev, "error creating queue thread (%ld)\n",
				PTR_ERR(kthread));
			ret = PTR_ERR(kthread);
			goto e_kthread;
		}

		cmd_q->kthread = kthread;
		wake_up_process(kthread);
	}

	dev_dbg(dev, "Enabling interrupts...\n");
	ccp5_enable_queue_interrupts(ccp);

	dev_dbg(dev, "Registering device...\n");
	/* Put this on the unit list to make it available */
	ccp_add_device(ccp);

	ret = ccp_register_rng(ccp);
	if (ret)
		goto e_kthread;

	/* Register the DMA engine support */
	ret = ccp_dmaengine_register(ccp);
	if (ret)
		goto e_hwrng;

	return 0;

e_hwrng:
	ccp_unregister_rng(ccp);

e_kthread:
	for (i = 0; i < ccp->cmd_q_count; i++)
		if (ccp->cmd_q[i].kthread)
			kthread_stop(ccp->cmd_q[i].kthread);

e_irq:
	ccp->free_irq(ccp);

e_pool:
	for (i = 0; i < ccp->cmd_q_count; i++)
		dma_pool_destroy(ccp->cmd_q[i].dma_pool);

	return ret;
}

static void ccp5_destroy(struct ccp_device *ccp)
{
	struct device *dev = ccp->dev;
	struct ccp_cmd_queue *cmd_q;
	struct ccp_cmd *cmd;
	unsigned int i;

	/* Unregister the DMA engine */
	ccp_dmaengine_unregister(ccp);

	/* Unregister the RNG */
	ccp_unregister_rng(ccp);

	/* Remove this device from the list of available units first */
	ccp_del_device(ccp);

	/* Disable and clear interrupts */
	ccp5_disable_queue_interrupts(ccp);
	for (i = 0; i < ccp->cmd_q_count; i++) {
		cmd_q = &ccp->cmd_q[i];

		/* Turn off the run bit */
		iowrite32(cmd_q->qcontrol & ~CMD5_Q_RUN, cmd_q->reg_control);

		/* Clear the interrupt status */
		iowrite32(SUPPORTED_INTERRUPTS, cmd_q->reg_interrupt_status);
		ioread32(cmd_q->reg_int_status);
		ioread32(cmd_q->reg_status);
	}

	/* Stop the queue kthreads */
	for (i = 0; i < ccp->cmd_q_count; i++)
		if (ccp->cmd_q[i].kthread)
			kthread_stop(ccp->cmd_q[i].kthread);

	ccp->free_irq(ccp);

	for (i = 0; i < ccp->cmd_q_count; i++) {
		cmd_q = &ccp->cmd_q[i];
		dma_free_coherent(dev, cmd_q->qsize, cmd_q->qbase,
				  cmd_q->qbase_dma);
	}

	/* Flush the cmd and backlog queue */
	while (!list_empty(&ccp->cmd)) {
		/* Invoke the callback directly with an error code */
		cmd = list_first_entry(&ccp->cmd, struct ccp_cmd, entry);
		list_del(&cmd->entry);
		cmd->callback(cmd->data, -ENODEV);
	}
	while (!list_empty(&ccp->backlog)) {
		/* Invoke the callback directly with an error code */
		cmd = list_first_entry(&ccp->backlog, struct ccp_cmd, entry);
		list_del(&cmd->entry);
		cmd->callback(cmd->data, -ENODEV);
	}
}

static void ccp5_config(struct ccp_device *ccp)
{
	/* Public side */
	iowrite32(0x0, ccp->io_regs + CMD5_REQID_CONFIG_OFFSET);
}

static void ccp5other_config(struct ccp_device *ccp)
{
	int i;
	u32 rnd;

	/* We own all of the queues on the NTB CCP */

	iowrite32(0x00012D57, ccp->io_regs + CMD5_TRNG_CTL_OFFSET);
	iowrite32(0x00000003, ccp->io_regs + CMD5_CONFIG_0_OFFSET);
	for (i = 0; i < 12; i++) {
		rnd = ioread32(ccp->io_regs + TRNG_OUT_REG);
		iowrite32(rnd, ccp->io_regs + CMD5_AES_MASK_OFFSET);
	}

	iowrite32(0x0000001F, ccp->io_regs + CMD5_QUEUE_MASK_OFFSET);
	iowrite32(0x00005B6D, ccp->io_regs + CMD5_QUEUE_PRIO_OFFSET);
	iowrite32(0x00000000, ccp->io_regs + CMD5_CMD_TIMEOUT_OFFSET);

	iowrite32(0x3FFFFFFF, ccp->io_regs + LSB_PRIVATE_MASK_LO_OFFSET);
	iowrite32(0x000003FF, ccp->io_regs + LSB_PRIVATE_MASK_HI_OFFSET);

	iowrite32(0x00108823, ccp->io_regs + CMD5_CLK_GATE_CTL_OFFSET);

	ccp5_config(ccp);
}

/* Version 5 adds some function, but is essentially the same as v5 */
static const struct ccp_actions ccp5_actions = {
	.aes = ccp5_perform_aes,
	.xts_aes = ccp5_perform_xts_aes,
	.sha = ccp5_perform_sha,
	.des3 = ccp5_perform_des3,
	.rsa = ccp5_perform_rsa,
	.passthru = ccp5_perform_passthru,
	.ecc = ccp5_perform_ecc,
	.sballoc = ccp_lsb_alloc,
	.sbfree = ccp_lsb_free,
	.init = ccp5_init,
	.destroy = ccp5_destroy,
	.get_free_slots = ccp5_get_free_slots,
	.irqhandler = ccp5_irq_handler,
};

const struct ccp_vdata ccpv5a = {
	.version = CCP_VERSION(5, 0),
	.setup = ccp5_config,
	.perform = &ccp5_actions,
	.bar = 2,
	.offset = 0x0,
};

const struct ccp_vdata ccpv5b = {
	.version = CCP_VERSION(5, 0),
	.dma_chan_attr = DMA_PRIVATE,
	.setup = ccp5other_config,
	.perform = &ccp5_actions,
	.bar = 2,
	.offset = 0x0,
};
