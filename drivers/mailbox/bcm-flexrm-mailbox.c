/*
 * Copyright (C) 2017 Broadcom
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Broadcom FlexRM Mailbox Driver
 *
 * Each Broadcom FlexSparx4 offload engine is implemented as an
 * extension to Broadcom FlexRM ring manager. The FlexRM ring
 * manager provides a set of rings which can be used to submit
 * work to a FlexSparx4 offload engine.
 *
 * This driver creates a mailbox controller using a set of FlexRM
 * rings where each mailbox channel represents a separate FlexRM ring.
 */

#include <asm/barrier.h>
#include <asm/byteorder.h>
#include <linux/atomic.h>
#include <linux/bitmap.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox/brcm-message.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

/* ====== FlexRM register defines ===== */

/* FlexRM configuration */
#define RING_REGS_SIZE					0x10000
#define RING_DESC_SIZE					8
#define RING_DESC_INDEX(offset)				\
			((offset) / RING_DESC_SIZE)
#define RING_DESC_OFFSET(index)				\
			((index) * RING_DESC_SIZE)
#define RING_MAX_REQ_COUNT				1024
#define RING_BD_ALIGN_ORDER				12
#define RING_BD_ALIGN_CHECK(addr)			\
			(!((addr) & ((0x1 << RING_BD_ALIGN_ORDER) - 1)))
#define RING_BD_TOGGLE_INVALID(offset)			\
			(((offset) >> RING_BD_ALIGN_ORDER) & 0x1)
#define RING_BD_TOGGLE_VALID(offset)			\
			(!RING_BD_TOGGLE_INVALID(offset))
#define RING_BD_DESC_PER_REQ				32
#define RING_BD_DESC_COUNT				\
			(RING_MAX_REQ_COUNT * RING_BD_DESC_PER_REQ)
#define RING_BD_SIZE					\
			(RING_BD_DESC_COUNT * RING_DESC_SIZE)
#define RING_CMPL_ALIGN_ORDER				13
#define RING_CMPL_DESC_COUNT				RING_MAX_REQ_COUNT
#define RING_CMPL_SIZE					\
			(RING_CMPL_DESC_COUNT * RING_DESC_SIZE)
#define RING_VER_MAGIC					0x76303031

/* Per-Ring register offsets */
#define RING_VER					0x000
#define RING_BD_START_ADDR				0x004
#define RING_BD_READ_PTR				0x008
#define RING_BD_WRITE_PTR				0x00c
#define RING_BD_READ_PTR_DDR_LS				0x010
#define RING_BD_READ_PTR_DDR_MS				0x014
#define RING_CMPL_START_ADDR				0x018
#define RING_CMPL_WRITE_PTR				0x01c
#define RING_NUM_REQ_RECV_LS				0x020
#define RING_NUM_REQ_RECV_MS				0x024
#define RING_NUM_REQ_TRANS_LS				0x028
#define RING_NUM_REQ_TRANS_MS				0x02c
#define RING_NUM_REQ_OUTSTAND				0x030
#define RING_CONTROL					0x034
#define RING_FLUSH_DONE					0x038
#define RING_MSI_ADDR_LS				0x03c
#define RING_MSI_ADDR_MS				0x040
#define RING_MSI_CONTROL				0x048
#define RING_BD_READ_PTR_DDR_CONTROL			0x04c
#define RING_MSI_DATA_VALUE				0x064

/* Register RING_BD_START_ADDR fields */
#define BD_LAST_UPDATE_HW_SHIFT				28
#define BD_LAST_UPDATE_HW_MASK				0x1
#define BD_START_ADDR_VALUE(pa)				\
	((u32)((((dma_addr_t)(pa)) >> RING_BD_ALIGN_ORDER) & 0x0fffffff))
#define BD_START_ADDR_DECODE(val)			\
	((dma_addr_t)((val) & 0x0fffffff) << RING_BD_ALIGN_ORDER)

/* Register RING_CMPL_START_ADDR fields */
#define CMPL_START_ADDR_VALUE(pa)			\
	((u32)((((u64)(pa)) >> RING_CMPL_ALIGN_ORDER) & 0x07ffffff))

/* Register RING_CONTROL fields */
#define CONTROL_MASK_DISABLE_CONTROL			12
#define CONTROL_FLUSH_SHIFT				5
#define CONTROL_ACTIVE_SHIFT				4
#define CONTROL_RATE_ADAPT_MASK				0xf
#define CONTROL_RATE_DYNAMIC				0x0
#define CONTROL_RATE_FAST				0x8
#define CONTROL_RATE_MEDIUM				0x9
#define CONTROL_RATE_SLOW				0xa
#define CONTROL_RATE_IDLE				0xb

/* Register RING_FLUSH_DONE fields */
#define FLUSH_DONE_MASK					0x1

/* Register RING_MSI_CONTROL fields */
#define MSI_TIMER_VAL_SHIFT				16
#define MSI_TIMER_VAL_MASK				0xffff
#define MSI_ENABLE_SHIFT				15
#define MSI_ENABLE_MASK					0x1
#define MSI_COUNT_SHIFT					0
#define MSI_COUNT_MASK					0x3ff

/* Register RING_BD_READ_PTR_DDR_CONTROL fields */
#define BD_READ_PTR_DDR_TIMER_VAL_SHIFT			16
#define BD_READ_PTR_DDR_TIMER_VAL_MASK			0xffff
#define BD_READ_PTR_DDR_ENABLE_SHIFT			15
#define BD_READ_PTR_DDR_ENABLE_MASK			0x1

/* ====== FlexRM ring descriptor defines ===== */

/* Completion descriptor format */
#define CMPL_OPAQUE_SHIFT			0
#define CMPL_OPAQUE_MASK			0xffff
#define CMPL_ENGINE_STATUS_SHIFT		16
#define CMPL_ENGINE_STATUS_MASK			0xffff
#define CMPL_DME_STATUS_SHIFT			32
#define CMPL_DME_STATUS_MASK			0xffff
#define CMPL_RM_STATUS_SHIFT			48
#define CMPL_RM_STATUS_MASK			0xffff

/* Completion DME status code */
#define DME_STATUS_MEM_COR_ERR			BIT(0)
#define DME_STATUS_MEM_UCOR_ERR			BIT(1)
#define DME_STATUS_FIFO_UNDERFLOW		BIT(2)
#define DME_STATUS_FIFO_OVERFLOW		BIT(3)
#define DME_STATUS_RRESP_ERR			BIT(4)
#define DME_STATUS_BRESP_ERR			BIT(5)
#define DME_STATUS_ERROR_MASK			(DME_STATUS_MEM_COR_ERR | \
						 DME_STATUS_MEM_UCOR_ERR | \
						 DME_STATUS_FIFO_UNDERFLOW | \
						 DME_STATUS_FIFO_OVERFLOW | \
						 DME_STATUS_RRESP_ERR | \
						 DME_STATUS_BRESP_ERR)

/* Completion RM status code */
#define RM_STATUS_CODE_SHIFT			0
#define RM_STATUS_CODE_MASK			0x3ff
#define RM_STATUS_CODE_GOOD			0x0
#define RM_STATUS_CODE_AE_TIMEOUT		0x3ff

/* General descriptor format */
#define DESC_TYPE_SHIFT				60
#define DESC_TYPE_MASK				0xf
#define DESC_PAYLOAD_SHIFT			0
#define DESC_PAYLOAD_MASK			0x0fffffffffffffff

/* Null descriptor format  */
#define NULL_TYPE				0
#define NULL_TOGGLE_SHIFT			58
#define NULL_TOGGLE_MASK			0x1

/* Header descriptor format */
#define HEADER_TYPE				1
#define HEADER_TOGGLE_SHIFT			58
#define HEADER_TOGGLE_MASK			0x1
#define HEADER_ENDPKT_SHIFT			57
#define HEADER_ENDPKT_MASK			0x1
#define HEADER_STARTPKT_SHIFT			56
#define HEADER_STARTPKT_MASK			0x1
#define HEADER_BDCOUNT_SHIFT			36
#define HEADER_BDCOUNT_MASK			0x1f
#define HEADER_BDCOUNT_MAX			HEADER_BDCOUNT_MASK
#define HEADER_FLAGS_SHIFT			16
#define HEADER_FLAGS_MASK			0xffff
#define HEADER_OPAQUE_SHIFT			0
#define HEADER_OPAQUE_MASK			0xffff

/* Source (SRC) descriptor format */
#define SRC_TYPE				2
#define SRC_LENGTH_SHIFT			44
#define SRC_LENGTH_MASK				0xffff
#define SRC_ADDR_SHIFT				0
#define SRC_ADDR_MASK				0x00000fffffffffff

/* Destination (DST) descriptor format */
#define DST_TYPE				3
#define DST_LENGTH_SHIFT			44
#define DST_LENGTH_MASK				0xffff
#define DST_ADDR_SHIFT				0
#define DST_ADDR_MASK				0x00000fffffffffff

/* Immediate (IMM) descriptor format */
#define IMM_TYPE				4
#define IMM_DATA_SHIFT				0
#define IMM_DATA_MASK				0x0fffffffffffffff

/* Next pointer (NPTR) descriptor format */
#define NPTR_TYPE				5
#define NPTR_TOGGLE_SHIFT			58
#define NPTR_TOGGLE_MASK			0x1
#define NPTR_ADDR_SHIFT				0
#define NPTR_ADDR_MASK				0x00000fffffffffff

/* Mega source (MSRC) descriptor format */
#define MSRC_TYPE				6
#define MSRC_LENGTH_SHIFT			44
#define MSRC_LENGTH_MASK			0xffff
#define MSRC_ADDR_SHIFT				0
#define MSRC_ADDR_MASK				0x00000fffffffffff

/* Mega destination (MDST) descriptor format */
#define MDST_TYPE				7
#define MDST_LENGTH_SHIFT			44
#define MDST_LENGTH_MASK			0xffff
#define MDST_ADDR_SHIFT				0
#define MDST_ADDR_MASK				0x00000fffffffffff

/* Source with tlast (SRCT) descriptor format */
#define SRCT_TYPE				8
#define SRCT_LENGTH_SHIFT			44
#define SRCT_LENGTH_MASK			0xffff
#define SRCT_ADDR_SHIFT				0
#define SRCT_ADDR_MASK				0x00000fffffffffff

/* Destination with tlast (DSTT) descriptor format */
#define DSTT_TYPE				9
#define DSTT_LENGTH_SHIFT			44
#define DSTT_LENGTH_MASK			0xffff
#define DSTT_ADDR_SHIFT				0
#define DSTT_ADDR_MASK				0x00000fffffffffff

/* Immediate with tlast (IMMT) descriptor format */
#define IMMT_TYPE				10
#define IMMT_DATA_SHIFT				0
#define IMMT_DATA_MASK				0x0fffffffffffffff

/* Descriptor helper macros */
#define DESC_DEC(_d, _s, _m)			(((_d) >> (_s)) & (_m))
#define DESC_ENC(_d, _v, _s, _m)		\
			do { \
				(_d) &= ~((u64)(_m) << (_s)); \
				(_d) |= (((u64)(_v) & (_m)) << (_s)); \
			} while (0)

/* ====== FlexRM data structures ===== */

struct flexrm_ring {
	/* Unprotected members */
	int num;
	struct flexrm_mbox *mbox;
	void __iomem *regs;
	bool irq_requested;
	unsigned int irq;
	cpumask_t irq_aff_hint;
	unsigned int msi_timer_val;
	unsigned int msi_count_threshold;
	struct brcm_message *requests[RING_MAX_REQ_COUNT];
	void *bd_base;
	dma_addr_t bd_dma_base;
	u32 bd_write_offset;
	void *cmpl_base;
	dma_addr_t cmpl_dma_base;
	/* Atomic stats */
	atomic_t msg_send_count;
	atomic_t msg_cmpl_count;
	/* Protected members */
	spinlock_t lock;
	DECLARE_BITMAP(requests_bmap, RING_MAX_REQ_COUNT);
	u32 cmpl_read_offset;
};

struct flexrm_mbox {
	struct device *dev;
	void __iomem *regs;
	u32 num_rings;
	struct flexrm_ring *rings;
	struct dma_pool *bd_pool;
	struct dma_pool *cmpl_pool;
	struct dentry *root;
	struct mbox_controller controller;
};

/* ====== FlexRM ring descriptor helper routines ===== */

static u64 flexrm_read_desc(void *desc_ptr)
{
	return le64_to_cpu(*((u64 *)desc_ptr));
}

static void flexrm_write_desc(void *desc_ptr, u64 desc)
{
	*((u64 *)desc_ptr) = cpu_to_le64(desc);
}

static u32 flexrm_cmpl_desc_to_reqid(u64 cmpl_desc)
{
	return (u32)(cmpl_desc & CMPL_OPAQUE_MASK);
}

static int flexrm_cmpl_desc_to_error(u64 cmpl_desc)
{
	u32 status;

	status = DESC_DEC(cmpl_desc, CMPL_DME_STATUS_SHIFT,
			  CMPL_DME_STATUS_MASK);
	if (status & DME_STATUS_ERROR_MASK)
		return -EIO;

	status = DESC_DEC(cmpl_desc, CMPL_RM_STATUS_SHIFT,
			  CMPL_RM_STATUS_MASK);
	status &= RM_STATUS_CODE_MASK;
	if (status == RM_STATUS_CODE_AE_TIMEOUT)
		return -ETIMEDOUT;

	return 0;
}

static bool flexrm_is_next_table_desc(void *desc_ptr)
{
	u64 desc = flexrm_read_desc(desc_ptr);
	u32 type = DESC_DEC(desc, DESC_TYPE_SHIFT, DESC_TYPE_MASK);

	return (type == NPTR_TYPE) ? true : false;
}

static u64 flexrm_next_table_desc(u32 toggle, dma_addr_t next_addr)
{
	u64 desc = 0;

	DESC_ENC(desc, NPTR_TYPE, DESC_TYPE_SHIFT, DESC_TYPE_MASK);
	DESC_ENC(desc, toggle, NPTR_TOGGLE_SHIFT, NPTR_TOGGLE_MASK);
	DESC_ENC(desc, next_addr, NPTR_ADDR_SHIFT, NPTR_ADDR_MASK);

	return desc;
}

static u64 flexrm_null_desc(u32 toggle)
{
	u64 desc = 0;

	DESC_ENC(desc, NULL_TYPE, DESC_TYPE_SHIFT, DESC_TYPE_MASK);
	DESC_ENC(desc, toggle, NULL_TOGGLE_SHIFT, NULL_TOGGLE_MASK);

	return desc;
}

static u32 flexrm_estimate_header_desc_count(u32 nhcnt)
{
	u32 hcnt = nhcnt / HEADER_BDCOUNT_MAX;

	if (!(nhcnt % HEADER_BDCOUNT_MAX))
		hcnt += 1;

	return hcnt;
}

static void flexrm_flip_header_toggle(void *desc_ptr)
{
	u64 desc = flexrm_read_desc(desc_ptr);

	if (desc & ((u64)0x1 << HEADER_TOGGLE_SHIFT))
		desc &= ~((u64)0x1 << HEADER_TOGGLE_SHIFT);
	else
		desc |= ((u64)0x1 << HEADER_TOGGLE_SHIFT);

	flexrm_write_desc(desc_ptr, desc);
}

static u64 flexrm_header_desc(u32 toggle, u32 startpkt, u32 endpkt,
			       u32 bdcount, u32 flags, u32 opaque)
{
	u64 desc = 0;

	DESC_ENC(desc, HEADER_TYPE, DESC_TYPE_SHIFT, DESC_TYPE_MASK);
	DESC_ENC(desc, toggle, HEADER_TOGGLE_SHIFT, HEADER_TOGGLE_MASK);
	DESC_ENC(desc, startpkt, HEADER_STARTPKT_SHIFT, HEADER_STARTPKT_MASK);
	DESC_ENC(desc, endpkt, HEADER_ENDPKT_SHIFT, HEADER_ENDPKT_MASK);
	DESC_ENC(desc, bdcount, HEADER_BDCOUNT_SHIFT, HEADER_BDCOUNT_MASK);
	DESC_ENC(desc, flags, HEADER_FLAGS_SHIFT, HEADER_FLAGS_MASK);
	DESC_ENC(desc, opaque, HEADER_OPAQUE_SHIFT, HEADER_OPAQUE_MASK);

	return desc;
}

static void flexrm_enqueue_desc(u32 nhpos, u32 nhcnt, u32 reqid,
				 u64 desc, void **desc_ptr, u32 *toggle,
				 void *start_desc, void *end_desc)
{
	u64 d;
	u32 nhavail, _toggle, _startpkt, _endpkt, _bdcount;

	/* Sanity check */
	if (nhcnt <= nhpos)
		return;

	/*
	 * Each request or packet start with a HEADER descriptor followed
	 * by one or more non-HEADER descriptors (SRC, SRCT, MSRC, DST,
	 * DSTT, MDST, IMM, and IMMT). The number of non-HEADER descriptors
	 * following a HEADER descriptor is represented by BDCOUNT field
	 * of HEADER descriptor. The max value of BDCOUNT field is 31 which
	 * means we can only have 31 non-HEADER descriptors following one
	 * HEADER descriptor.
	 *
	 * In general use, number of non-HEADER descriptors can easily go
	 * beyond 31. To tackle this situation, we have packet (or request)
	 * extension bits (STARTPKT and ENDPKT) in the HEADER descriptor.
	 *
	 * To use packet extension, the first HEADER descriptor of request
	 * (or packet) will have STARTPKT=1 and ENDPKT=0. The intermediate
	 * HEADER descriptors will have STARTPKT=0 and ENDPKT=0. The last
	 * HEADER descriptor will have STARTPKT=0 and ENDPKT=1. Also, the
	 * TOGGLE bit of the first HEADER will be set to invalid state to
	 * ensure that FlexRM does not start fetching descriptors till all
	 * descriptors are enqueued. The user of this function will flip
	 * the TOGGLE bit of first HEADER after all descriptors are
	 * enqueued.
	 */

	if ((nhpos % HEADER_BDCOUNT_MAX == 0) && (nhcnt - nhpos)) {
		/* Prepare the header descriptor */
		nhavail = (nhcnt - nhpos);
		_toggle = (nhpos == 0) ? !(*toggle) : (*toggle);
		_startpkt = (nhpos == 0) ? 0x1 : 0x0;
		_endpkt = (nhavail <= HEADER_BDCOUNT_MAX) ? 0x1 : 0x0;
		_bdcount = (nhavail <= HEADER_BDCOUNT_MAX) ?
				nhavail : HEADER_BDCOUNT_MAX;
		if (nhavail <= HEADER_BDCOUNT_MAX)
			_bdcount = nhavail;
		else
			_bdcount = HEADER_BDCOUNT_MAX;
		d = flexrm_header_desc(_toggle, _startpkt, _endpkt,
					_bdcount, 0x0, reqid);

		/* Write header descriptor */
		flexrm_write_desc(*desc_ptr, d);

		/* Point to next descriptor */
		*desc_ptr += sizeof(desc);
		if (*desc_ptr == end_desc)
			*desc_ptr = start_desc;

		/* Skip next pointer descriptors */
		while (flexrm_is_next_table_desc(*desc_ptr)) {
			*toggle = (*toggle) ? 0 : 1;
			*desc_ptr += sizeof(desc);
			if (*desc_ptr == end_desc)
				*desc_ptr = start_desc;
		}
	}

	/* Write desired descriptor */
	flexrm_write_desc(*desc_ptr, desc);

	/* Point to next descriptor */
	*desc_ptr += sizeof(desc);
	if (*desc_ptr == end_desc)
		*desc_ptr = start_desc;

	/* Skip next pointer descriptors */
	while (flexrm_is_next_table_desc(*desc_ptr)) {
		*toggle = (*toggle) ? 0 : 1;
		*desc_ptr += sizeof(desc);
		if (*desc_ptr == end_desc)
			*desc_ptr = start_desc;
	}
}

static u64 flexrm_src_desc(dma_addr_t addr, unsigned int length)
{
	u64 desc = 0;

	DESC_ENC(desc, SRC_TYPE, DESC_TYPE_SHIFT, DESC_TYPE_MASK);
	DESC_ENC(desc, length, SRC_LENGTH_SHIFT, SRC_LENGTH_MASK);
	DESC_ENC(desc, addr, SRC_ADDR_SHIFT, SRC_ADDR_MASK);

	return desc;
}

static u64 flexrm_msrc_desc(dma_addr_t addr, unsigned int length_div_16)
{
	u64 desc = 0;

	DESC_ENC(desc, MSRC_TYPE, DESC_TYPE_SHIFT, DESC_TYPE_MASK);
	DESC_ENC(desc, length_div_16, MSRC_LENGTH_SHIFT, MSRC_LENGTH_MASK);
	DESC_ENC(desc, addr, MSRC_ADDR_SHIFT, MSRC_ADDR_MASK);

	return desc;
}

static u64 flexrm_dst_desc(dma_addr_t addr, unsigned int length)
{
	u64 desc = 0;

	DESC_ENC(desc, DST_TYPE, DESC_TYPE_SHIFT, DESC_TYPE_MASK);
	DESC_ENC(desc, length, DST_LENGTH_SHIFT, DST_LENGTH_MASK);
	DESC_ENC(desc, addr, DST_ADDR_SHIFT, DST_ADDR_MASK);

	return desc;
}

static u64 flexrm_mdst_desc(dma_addr_t addr, unsigned int length_div_16)
{
	u64 desc = 0;

	DESC_ENC(desc, MDST_TYPE, DESC_TYPE_SHIFT, DESC_TYPE_MASK);
	DESC_ENC(desc, length_div_16, MDST_LENGTH_SHIFT, MDST_LENGTH_MASK);
	DESC_ENC(desc, addr, MDST_ADDR_SHIFT, MDST_ADDR_MASK);

	return desc;
}

static u64 flexrm_imm_desc(u64 data)
{
	u64 desc = 0;

	DESC_ENC(desc, IMM_TYPE, DESC_TYPE_SHIFT, DESC_TYPE_MASK);
	DESC_ENC(desc, data, IMM_DATA_SHIFT, IMM_DATA_MASK);

	return desc;
}

static u64 flexrm_srct_desc(dma_addr_t addr, unsigned int length)
{
	u64 desc = 0;

	DESC_ENC(desc, SRCT_TYPE, DESC_TYPE_SHIFT, DESC_TYPE_MASK);
	DESC_ENC(desc, length, SRCT_LENGTH_SHIFT, SRCT_LENGTH_MASK);
	DESC_ENC(desc, addr, SRCT_ADDR_SHIFT, SRCT_ADDR_MASK);

	return desc;
}

static u64 flexrm_dstt_desc(dma_addr_t addr, unsigned int length)
{
	u64 desc = 0;

	DESC_ENC(desc, DSTT_TYPE, DESC_TYPE_SHIFT, DESC_TYPE_MASK);
	DESC_ENC(desc, length, DSTT_LENGTH_SHIFT, DSTT_LENGTH_MASK);
	DESC_ENC(desc, addr, DSTT_ADDR_SHIFT, DSTT_ADDR_MASK);

	return desc;
}

static u64 flexrm_immt_desc(u64 data)
{
	u64 desc = 0;

	DESC_ENC(desc, IMMT_TYPE, DESC_TYPE_SHIFT, DESC_TYPE_MASK);
	DESC_ENC(desc, data, IMMT_DATA_SHIFT, IMMT_DATA_MASK);

	return desc;
}

static bool flexrm_spu_sanity_check(struct brcm_message *msg)
{
	struct scatterlist *sg;

	if (!msg->spu.src || !msg->spu.dst)
		return false;
	for (sg = msg->spu.src; sg; sg = sg_next(sg)) {
		if (sg->length & 0xf) {
			if (sg->length > SRC_LENGTH_MASK)
				return false;
		} else {
			if (sg->length > (MSRC_LENGTH_MASK * 16))
				return false;
		}
	}
	for (sg = msg->spu.dst; sg; sg = sg_next(sg)) {
		if (sg->length & 0xf) {
			if (sg->length > DST_LENGTH_MASK)
				return false;
		} else {
			if (sg->length > (MDST_LENGTH_MASK * 16))
				return false;
		}
	}

	return true;
}

static u32 flexrm_spu_estimate_nonheader_desc_count(struct brcm_message *msg)
{
	u32 cnt = 0;
	unsigned int dst_target = 0;
	struct scatterlist *src_sg = msg->spu.src, *dst_sg = msg->spu.dst;

	while (src_sg || dst_sg) {
		if (src_sg) {
			cnt++;
			dst_target = src_sg->length;
			src_sg = sg_next(src_sg);
		} else
			dst_target = UINT_MAX;

		while (dst_target && dst_sg) {
			cnt++;
			if (dst_sg->length < dst_target)
				dst_target -= dst_sg->length;
			else
				dst_target = 0;
			dst_sg = sg_next(dst_sg);
		}
	}

	return cnt;
}

static int flexrm_spu_dma_map(struct device *dev, struct brcm_message *msg)
{
	int rc;

	rc = dma_map_sg(dev, msg->spu.src, sg_nents(msg->spu.src),
			DMA_TO_DEVICE);
	if (rc < 0)
		return rc;

	rc = dma_map_sg(dev, msg->spu.dst, sg_nents(msg->spu.dst),
			DMA_FROM_DEVICE);
	if (rc < 0) {
		dma_unmap_sg(dev, msg->spu.src, sg_nents(msg->spu.src),
			     DMA_TO_DEVICE);
		return rc;
	}

	return 0;
}

static void flexrm_spu_dma_unmap(struct device *dev, struct brcm_message *msg)
{
	dma_unmap_sg(dev, msg->spu.dst, sg_nents(msg->spu.dst),
		     DMA_FROM_DEVICE);
	dma_unmap_sg(dev, msg->spu.src, sg_nents(msg->spu.src),
		     DMA_TO_DEVICE);
}

static void *flexrm_spu_write_descs(struct brcm_message *msg, u32 nhcnt,
				     u32 reqid, void *desc_ptr, u32 toggle,
				     void *start_desc, void *end_desc)
{
	u64 d;
	u32 nhpos = 0;
	void *orig_desc_ptr = desc_ptr;
	unsigned int dst_target = 0;
	struct scatterlist *src_sg = msg->spu.src, *dst_sg = msg->spu.dst;

	while (src_sg || dst_sg) {
		if (src_sg) {
			if (sg_dma_len(src_sg) & 0xf)
				d = flexrm_src_desc(sg_dma_address(src_sg),
						     sg_dma_len(src_sg));
			else
				d = flexrm_msrc_desc(sg_dma_address(src_sg),
						      sg_dma_len(src_sg)/16);
			flexrm_enqueue_desc(nhpos, nhcnt, reqid,
					     d, &desc_ptr, &toggle,
					     start_desc, end_desc);
			nhpos++;
			dst_target = sg_dma_len(src_sg);
			src_sg = sg_next(src_sg);
		} else
			dst_target = UINT_MAX;

		while (dst_target && dst_sg) {
			if (sg_dma_len(dst_sg) & 0xf)
				d = flexrm_dst_desc(sg_dma_address(dst_sg),
						     sg_dma_len(dst_sg));
			else
				d = flexrm_mdst_desc(sg_dma_address(dst_sg),
						      sg_dma_len(dst_sg)/16);
			flexrm_enqueue_desc(nhpos, nhcnt, reqid,
					     d, &desc_ptr, &toggle,
					     start_desc, end_desc);
			nhpos++;
			if (sg_dma_len(dst_sg) < dst_target)
				dst_target -= sg_dma_len(dst_sg);
			else
				dst_target = 0;
			dst_sg = sg_next(dst_sg);
		}
	}

	/* Null descriptor with invalid toggle bit */
	flexrm_write_desc(desc_ptr, flexrm_null_desc(!toggle));

	/* Ensure that descriptors have been written to memory */
	wmb();

	/* Flip toggle bit in header */
	flexrm_flip_header_toggle(orig_desc_ptr);

	return desc_ptr;
}

static bool flexrm_sba_sanity_check(struct brcm_message *msg)
{
	u32 i;

	if (!msg->sba.cmds || !msg->sba.cmds_count)
		return false;

	for (i = 0; i < msg->sba.cmds_count; i++) {
		if (((msg->sba.cmds[i].flags & BRCM_SBA_CMD_TYPE_B) ||
		     (msg->sba.cmds[i].flags & BRCM_SBA_CMD_TYPE_C)) &&
		    (msg->sba.cmds[i].flags & BRCM_SBA_CMD_HAS_OUTPUT))
			return false;
		if ((msg->sba.cmds[i].flags & BRCM_SBA_CMD_TYPE_B) &&
		    (msg->sba.cmds[i].data_len > SRCT_LENGTH_MASK))
			return false;
		if ((msg->sba.cmds[i].flags & BRCM_SBA_CMD_TYPE_C) &&
		    (msg->sba.cmds[i].data_len > SRCT_LENGTH_MASK))
			return false;
		if ((msg->sba.cmds[i].flags & BRCM_SBA_CMD_HAS_RESP) &&
		    (msg->sba.cmds[i].resp_len > DSTT_LENGTH_MASK))
			return false;
		if ((msg->sba.cmds[i].flags & BRCM_SBA_CMD_HAS_OUTPUT) &&
		    (msg->sba.cmds[i].data_len > DSTT_LENGTH_MASK))
			return false;
	}

	return true;
}

static u32 flexrm_sba_estimate_nonheader_desc_count(struct brcm_message *msg)
{
	u32 i, cnt;

	cnt = 0;
	for (i = 0; i < msg->sba.cmds_count; i++) {
		cnt++;

		if ((msg->sba.cmds[i].flags & BRCM_SBA_CMD_TYPE_B) ||
		    (msg->sba.cmds[i].flags & BRCM_SBA_CMD_TYPE_C))
			cnt++;

		if (msg->sba.cmds[i].flags & BRCM_SBA_CMD_HAS_RESP)
			cnt++;

		if (msg->sba.cmds[i].flags & BRCM_SBA_CMD_HAS_OUTPUT)
			cnt++;
	}

	return cnt;
}

static void *flexrm_sba_write_descs(struct brcm_message *msg, u32 nhcnt,
				     u32 reqid, void *desc_ptr, u32 toggle,
				     void *start_desc, void *end_desc)
{
	u64 d;
	u32 i, nhpos = 0;
	struct brcm_sba_command *c;
	void *orig_desc_ptr = desc_ptr;

	/* Convert SBA commands into descriptors */
	for (i = 0; i < msg->sba.cmds_count; i++) {
		c = &msg->sba.cmds[i];

		if ((c->flags & BRCM_SBA_CMD_HAS_RESP) &&
		    (c->flags & BRCM_SBA_CMD_HAS_OUTPUT)) {
			/* Destination response descriptor */
			d = flexrm_dst_desc(c->resp, c->resp_len);
			flexrm_enqueue_desc(nhpos, nhcnt, reqid,
					     d, &desc_ptr, &toggle,
					     start_desc, end_desc);
			nhpos++;
		} else if (c->flags & BRCM_SBA_CMD_HAS_RESP) {
			/* Destination response with tlast descriptor */
			d = flexrm_dstt_desc(c->resp, c->resp_len);
			flexrm_enqueue_desc(nhpos, nhcnt, reqid,
					     d, &desc_ptr, &toggle,
					     start_desc, end_desc);
			nhpos++;
		}

		if (c->flags & BRCM_SBA_CMD_HAS_OUTPUT) {
			/* Destination with tlast descriptor */
			d = flexrm_dstt_desc(c->data, c->data_len);
			flexrm_enqueue_desc(nhpos, nhcnt, reqid,
					     d, &desc_ptr, &toggle,
					     start_desc, end_desc);
			nhpos++;
		}

		if (c->flags & BRCM_SBA_CMD_TYPE_B) {
			/* Command as immediate descriptor */
			d = flexrm_imm_desc(c->cmd);
			flexrm_enqueue_desc(nhpos, nhcnt, reqid,
					     d, &desc_ptr, &toggle,
					     start_desc, end_desc);
			nhpos++;
		} else {
			/* Command as immediate descriptor with tlast */
			d = flexrm_immt_desc(c->cmd);
			flexrm_enqueue_desc(nhpos, nhcnt, reqid,
					     d, &desc_ptr, &toggle,
					     start_desc, end_desc);
			nhpos++;
		}

		if ((c->flags & BRCM_SBA_CMD_TYPE_B) ||
		    (c->flags & BRCM_SBA_CMD_TYPE_C)) {
			/* Source with tlast descriptor */
			d = flexrm_srct_desc(c->data, c->data_len);
			flexrm_enqueue_desc(nhpos, nhcnt, reqid,
					     d, &desc_ptr, &toggle,
					     start_desc, end_desc);
			nhpos++;
		}
	}

	/* Null descriptor with invalid toggle bit */
	flexrm_write_desc(desc_ptr, flexrm_null_desc(!toggle));

	/* Ensure that descriptors have been written to memory */
	wmb();

	/* Flip toggle bit in header */
	flexrm_flip_header_toggle(orig_desc_ptr);

	return desc_ptr;
}

static bool flexrm_sanity_check(struct brcm_message *msg)
{
	if (!msg)
		return false;

	switch (msg->type) {
	case BRCM_MESSAGE_SPU:
		return flexrm_spu_sanity_check(msg);
	case BRCM_MESSAGE_SBA:
		return flexrm_sba_sanity_check(msg);
	default:
		return false;
	};
}

static u32 flexrm_estimate_nonheader_desc_count(struct brcm_message *msg)
{
	if (!msg)
		return 0;

	switch (msg->type) {
	case BRCM_MESSAGE_SPU:
		return flexrm_spu_estimate_nonheader_desc_count(msg);
	case BRCM_MESSAGE_SBA:
		return flexrm_sba_estimate_nonheader_desc_count(msg);
	default:
		return 0;
	};
}

static int flexrm_dma_map(struct device *dev, struct brcm_message *msg)
{
	if (!dev || !msg)
		return -EINVAL;

	switch (msg->type) {
	case BRCM_MESSAGE_SPU:
		return flexrm_spu_dma_map(dev, msg);
	default:
		break;
	}

	return 0;
}

static void flexrm_dma_unmap(struct device *dev, struct brcm_message *msg)
{
	if (!dev || !msg)
		return;

	switch (msg->type) {
	case BRCM_MESSAGE_SPU:
		flexrm_spu_dma_unmap(dev, msg);
		break;
	default:
		break;
	}
}

static void *flexrm_write_descs(struct brcm_message *msg, u32 nhcnt,
				u32 reqid, void *desc_ptr, u32 toggle,
				void *start_desc, void *end_desc)
{
	if (!msg || !desc_ptr || !start_desc || !end_desc)
		return ERR_PTR(-ENOTSUPP);

	if ((desc_ptr < start_desc) || (end_desc <= desc_ptr))
		return ERR_PTR(-ERANGE);

	switch (msg->type) {
	case BRCM_MESSAGE_SPU:
		return flexrm_spu_write_descs(msg, nhcnt, reqid,
					       desc_ptr, toggle,
					       start_desc, end_desc);
	case BRCM_MESSAGE_SBA:
		return flexrm_sba_write_descs(msg, nhcnt, reqid,
					       desc_ptr, toggle,
					       start_desc, end_desc);
	default:
		return ERR_PTR(-ENOTSUPP);
	};
}

/* ====== FlexRM driver helper routines ===== */

static void flexrm_write_config_in_seqfile(struct flexrm_mbox *mbox,
					   struct seq_file *file)
{
	int i;
	const char *state;
	struct flexrm_ring *ring;

	seq_printf(file, "%-5s %-9s %-18s %-10s %-18s %-10s\n",
		   "Ring#", "State", "BD_Addr", "BD_Size",
		   "Cmpl_Addr", "Cmpl_Size");

	for (i = 0; i < mbox->num_rings; i++) {
		ring = &mbox->rings[i];
		if (readl(ring->regs + RING_CONTROL) &
		    BIT(CONTROL_ACTIVE_SHIFT))
			state = "active";
		else
			state = "inactive";
		seq_printf(file,
			   "%-5d %-9s 0x%016llx 0x%08x 0x%016llx 0x%08x\n",
			   ring->num, state,
			   (unsigned long long)ring->bd_dma_base,
			   (u32)RING_BD_SIZE,
			   (unsigned long long)ring->cmpl_dma_base,
			   (u32)RING_CMPL_SIZE);
	}
}

static void flexrm_write_stats_in_seqfile(struct flexrm_mbox *mbox,
					  struct seq_file *file)
{
	int i;
	u32 val, bd_read_offset;
	struct flexrm_ring *ring;

	seq_printf(file, "%-5s %-10s %-10s %-10s %-11s %-11s\n",
		   "Ring#", "BD_Read", "BD_Write",
		   "Cmpl_Read", "Submitted", "Completed");

	for (i = 0; i < mbox->num_rings; i++) {
		ring = &mbox->rings[i];
		bd_read_offset = readl_relaxed(ring->regs + RING_BD_READ_PTR);
		val = readl_relaxed(ring->regs + RING_BD_START_ADDR);
		bd_read_offset *= RING_DESC_SIZE;
		bd_read_offset += (u32)(BD_START_ADDR_DECODE(val) -
					ring->bd_dma_base);
		seq_printf(file, "%-5d 0x%08x 0x%08x 0x%08x %-11d %-11d\n",
			   ring->num,
			   (u32)bd_read_offset,
			   (u32)ring->bd_write_offset,
			   (u32)ring->cmpl_read_offset,
			   (u32)atomic_read(&ring->msg_send_count),
			   (u32)atomic_read(&ring->msg_cmpl_count));
	}
}

static int flexrm_new_request(struct flexrm_ring *ring,
				struct brcm_message *batch_msg,
				struct brcm_message *msg)
{
	void *next;
	unsigned long flags;
	u32 val, count, nhcnt;
	u32 read_offset, write_offset;
	bool exit_cleanup = false;
	int ret = 0, reqid;

	/* Do sanity check on message */
	if (!flexrm_sanity_check(msg))
		return -EIO;
	msg->error = 0;

	/* If no requests possible then save data pointer and goto done. */
	spin_lock_irqsave(&ring->lock, flags);
	reqid = bitmap_find_free_region(ring->requests_bmap,
					RING_MAX_REQ_COUNT, 0);
	spin_unlock_irqrestore(&ring->lock, flags);
	if (reqid < 0)
		return -ENOSPC;
	ring->requests[reqid] = msg;

	/* Do DMA mappings for the message */
	ret = flexrm_dma_map(ring->mbox->dev, msg);
	if (ret < 0) {
		ring->requests[reqid] = NULL;
		spin_lock_irqsave(&ring->lock, flags);
		bitmap_release_region(ring->requests_bmap, reqid, 0);
		spin_unlock_irqrestore(&ring->lock, flags);
		return ret;
	}

	/* Determine current HW BD read offset */
	read_offset = readl_relaxed(ring->regs + RING_BD_READ_PTR);
	val = readl_relaxed(ring->regs + RING_BD_START_ADDR);
	read_offset *= RING_DESC_SIZE;
	read_offset += (u32)(BD_START_ADDR_DECODE(val) - ring->bd_dma_base);

	/*
	 * Number required descriptors = number of non-header descriptors +
	 *				 number of header descriptors +
	 *				 1x null descriptor
	 */
	nhcnt = flexrm_estimate_nonheader_desc_count(msg);
	count = flexrm_estimate_header_desc_count(nhcnt) + nhcnt + 1;

	/* Check for available descriptor space. */
	write_offset = ring->bd_write_offset;
	while (count) {
		if (!flexrm_is_next_table_desc(ring->bd_base + write_offset))
			count--;
		write_offset += RING_DESC_SIZE;
		if (write_offset == RING_BD_SIZE)
			write_offset = 0x0;
		if (write_offset == read_offset)
			break;
	}
	if (count) {
		ret = -ENOSPC;
		exit_cleanup = true;
		goto exit;
	}

	/* Write descriptors to ring */
	next = flexrm_write_descs(msg, nhcnt, reqid,
			ring->bd_base + ring->bd_write_offset,
			RING_BD_TOGGLE_VALID(ring->bd_write_offset),
			ring->bd_base, ring->bd_base + RING_BD_SIZE);
	if (IS_ERR(next)) {
		ret = PTR_ERR(next);
		exit_cleanup = true;
		goto exit;
	}

	/* Save ring BD write offset */
	ring->bd_write_offset = (unsigned long)(next - ring->bd_base);

	/* Increment number of messages sent */
	atomic_inc_return(&ring->msg_send_count);

exit:
	/* Update error status in message */
	msg->error = ret;

	/* Cleanup if we failed */
	if (exit_cleanup) {
		flexrm_dma_unmap(ring->mbox->dev, msg);
		ring->requests[reqid] = NULL;
		spin_lock_irqsave(&ring->lock, flags);
		bitmap_release_region(ring->requests_bmap, reqid, 0);
		spin_unlock_irqrestore(&ring->lock, flags);
	}

	return ret;
}

static int flexrm_process_completions(struct flexrm_ring *ring)
{
	u64 desc;
	int err, count = 0;
	unsigned long flags;
	struct brcm_message *msg = NULL;
	u32 reqid, cmpl_read_offset, cmpl_write_offset;
	struct mbox_chan *chan = &ring->mbox->controller.chans[ring->num];

	spin_lock_irqsave(&ring->lock, flags);

	/*
	 * Get current completion read and write offset
	 *
	 * Note: We should read completion write pointer at least once
	 * after we get a MSI interrupt because HW maintains internal
	 * MSI status which will allow next MSI interrupt only after
	 * completion write pointer is read.
	 */
	cmpl_write_offset = readl_relaxed(ring->regs + RING_CMPL_WRITE_PTR);
	cmpl_write_offset *= RING_DESC_SIZE;
	cmpl_read_offset = ring->cmpl_read_offset;
	ring->cmpl_read_offset = cmpl_write_offset;

	spin_unlock_irqrestore(&ring->lock, flags);

	/* For each completed request notify mailbox clients */
	reqid = 0;
	while (cmpl_read_offset != cmpl_write_offset) {
		/* Dequeue next completion descriptor */
		desc = *((u64 *)(ring->cmpl_base + cmpl_read_offset));

		/* Next read offset */
		cmpl_read_offset += RING_DESC_SIZE;
		if (cmpl_read_offset == RING_CMPL_SIZE)
			cmpl_read_offset = 0;

		/* Decode error from completion descriptor */
		err = flexrm_cmpl_desc_to_error(desc);
		if (err < 0) {
			dev_warn(ring->mbox->dev,
			"ring%d got completion desc=0x%lx with error %d\n",
			ring->num, (unsigned long)desc, err);
		}

		/* Determine request id from completion descriptor */
		reqid = flexrm_cmpl_desc_to_reqid(desc);

		/* Determine message pointer based on reqid */
		msg = ring->requests[reqid];
		if (!msg) {
			dev_warn(ring->mbox->dev,
			"ring%d null msg pointer for completion desc=0x%lx\n",
			ring->num, (unsigned long)desc);
			continue;
		}

		/* Release reqid for recycling */
		ring->requests[reqid] = NULL;
		spin_lock_irqsave(&ring->lock, flags);
		bitmap_release_region(ring->requests_bmap, reqid, 0);
		spin_unlock_irqrestore(&ring->lock, flags);

		/* Unmap DMA mappings */
		flexrm_dma_unmap(ring->mbox->dev, msg);

		/* Give-back message to mailbox client */
		msg->error = err;
		mbox_chan_received_data(chan, msg);

		/* Increment number of completions processed */
		atomic_inc_return(&ring->msg_cmpl_count);
		count++;
	}

	return count;
}

/* ====== FlexRM Debugfs callbacks ====== */

static int flexrm_debugfs_conf_show(struct seq_file *file, void *offset)
{
	struct flexrm_mbox *mbox = dev_get_drvdata(file->private);

	/* Write config in file */
	flexrm_write_config_in_seqfile(mbox, file);

	return 0;
}

static int flexrm_debugfs_stats_show(struct seq_file *file, void *offset)
{
	struct flexrm_mbox *mbox = dev_get_drvdata(file->private);

	/* Write stats in file */
	flexrm_write_stats_in_seqfile(mbox, file);

	return 0;
}

/* ====== FlexRM interrupt handler ===== */

static irqreturn_t flexrm_irq_event(int irq, void *dev_id)
{
	/* We only have MSI for completions so just wakeup IRQ thread */
	/* Ring related errors will be informed via completion descriptors */

	return IRQ_WAKE_THREAD;
}

static irqreturn_t flexrm_irq_thread(int irq, void *dev_id)
{
	flexrm_process_completions(dev_id);

	return IRQ_HANDLED;
}

/* ====== FlexRM mailbox callbacks ===== */

static int flexrm_send_data(struct mbox_chan *chan, void *data)
{
	int i, rc;
	struct flexrm_ring *ring = chan->con_priv;
	struct brcm_message *msg = data;

	if (msg->type == BRCM_MESSAGE_BATCH) {
		for (i = msg->batch.msgs_queued;
		     i < msg->batch.msgs_count; i++) {
			rc = flexrm_new_request(ring, msg,
						 &msg->batch.msgs[i]);
			if (rc) {
				msg->error = rc;
				return rc;
			}
			msg->batch.msgs_queued++;
		}
		return 0;
	}

	return flexrm_new_request(ring, NULL, data);
}

static bool flexrm_peek_data(struct mbox_chan *chan)
{
	int cnt = flexrm_process_completions(chan->con_priv);

	return (cnt > 0) ? true : false;
}

static int flexrm_startup(struct mbox_chan *chan)
{
	u64 d;
	u32 val, off;
	int ret = 0;
	dma_addr_t next_addr;
	struct flexrm_ring *ring = chan->con_priv;

	/* Allocate BD memory */
	ring->bd_base = dma_pool_alloc(ring->mbox->bd_pool,
				       GFP_KERNEL, &ring->bd_dma_base);
	if (!ring->bd_base) {
		dev_err(ring->mbox->dev,
			"can't allocate BD memory for ring%d\n",
			ring->num);
		ret = -ENOMEM;
		goto fail;
	}

	/* Configure next table pointer entries in BD memory */
	for (off = 0; off < RING_BD_SIZE; off += RING_DESC_SIZE) {
		next_addr = off + RING_DESC_SIZE;
		if (next_addr == RING_BD_SIZE)
			next_addr = 0;
		next_addr += ring->bd_dma_base;
		if (RING_BD_ALIGN_CHECK(next_addr))
			d = flexrm_next_table_desc(RING_BD_TOGGLE_VALID(off),
						    next_addr);
		else
			d = flexrm_null_desc(RING_BD_TOGGLE_INVALID(off));
		flexrm_write_desc(ring->bd_base + off, d);
	}

	/* Allocate completion memory */
	ring->cmpl_base = dma_pool_zalloc(ring->mbox->cmpl_pool,
					 GFP_KERNEL, &ring->cmpl_dma_base);
	if (!ring->cmpl_base) {
		dev_err(ring->mbox->dev,
			"can't allocate completion memory for ring%d\n",
			ring->num);
		ret = -ENOMEM;
		goto fail_free_bd_memory;
	}

	/* Request IRQ */
	if (ring->irq == UINT_MAX) {
		dev_err(ring->mbox->dev,
			"ring%d IRQ not available\n", ring->num);
		ret = -ENODEV;
		goto fail_free_cmpl_memory;
	}
	ret = request_threaded_irq(ring->irq,
				   flexrm_irq_event,
				   flexrm_irq_thread,
				   0, dev_name(ring->mbox->dev), ring);
	if (ret) {
		dev_err(ring->mbox->dev,
			"failed to request ring%d IRQ\n", ring->num);
		goto fail_free_cmpl_memory;
	}
	ring->irq_requested = true;

	/* Set IRQ affinity hint */
	ring->irq_aff_hint = CPU_MASK_NONE;
	val = ring->mbox->num_rings;
	val = (num_online_cpus() < val) ? val / num_online_cpus() : 1;
	cpumask_set_cpu((ring->num / val) % num_online_cpus(),
			&ring->irq_aff_hint);
	ret = irq_update_affinity_hint(ring->irq, &ring->irq_aff_hint);
	if (ret) {
		dev_err(ring->mbox->dev,
			"failed to set IRQ affinity hint for ring%d\n",
			ring->num);
		goto fail_free_irq;
	}

	/* Disable/inactivate ring */
	writel_relaxed(0x0, ring->regs + RING_CONTROL);

	/* Program BD start address */
	val = BD_START_ADDR_VALUE(ring->bd_dma_base);
	writel_relaxed(val, ring->regs + RING_BD_START_ADDR);

	/* BD write pointer will be same as HW write pointer */
	ring->bd_write_offset =
			readl_relaxed(ring->regs + RING_BD_WRITE_PTR);
	ring->bd_write_offset *= RING_DESC_SIZE;

	/* Program completion start address */
	val = CMPL_START_ADDR_VALUE(ring->cmpl_dma_base);
	writel_relaxed(val, ring->regs + RING_CMPL_START_ADDR);

	/* Completion read pointer will be same as HW write pointer */
	ring->cmpl_read_offset =
			readl_relaxed(ring->regs + RING_CMPL_WRITE_PTR);
	ring->cmpl_read_offset *= RING_DESC_SIZE;

	/* Read ring Tx, Rx, and Outstanding counts to clear */
	readl_relaxed(ring->regs + RING_NUM_REQ_RECV_LS);
	readl_relaxed(ring->regs + RING_NUM_REQ_RECV_MS);
	readl_relaxed(ring->regs + RING_NUM_REQ_TRANS_LS);
	readl_relaxed(ring->regs + RING_NUM_REQ_TRANS_MS);
	readl_relaxed(ring->regs + RING_NUM_REQ_OUTSTAND);

	/* Configure RING_MSI_CONTROL */
	val = 0;
	val |= (ring->msi_timer_val << MSI_TIMER_VAL_SHIFT);
	val |= BIT(MSI_ENABLE_SHIFT);
	val |= (ring->msi_count_threshold & MSI_COUNT_MASK) << MSI_COUNT_SHIFT;
	writel_relaxed(val, ring->regs + RING_MSI_CONTROL);

	/* Enable/activate ring */
	val = BIT(CONTROL_ACTIVE_SHIFT);
	writel_relaxed(val, ring->regs + RING_CONTROL);

	/* Reset stats to zero */
	atomic_set(&ring->msg_send_count, 0);
	atomic_set(&ring->msg_cmpl_count, 0);

	return 0;

fail_free_irq:
	free_irq(ring->irq, ring);
	ring->irq_requested = false;
fail_free_cmpl_memory:
	dma_pool_free(ring->mbox->cmpl_pool,
		      ring->cmpl_base, ring->cmpl_dma_base);
	ring->cmpl_base = NULL;
fail_free_bd_memory:
	dma_pool_free(ring->mbox->bd_pool,
		      ring->bd_base, ring->bd_dma_base);
	ring->bd_base = NULL;
fail:
	return ret;
}

static void flexrm_shutdown(struct mbox_chan *chan)
{
	u32 reqid;
	unsigned int timeout;
	struct brcm_message *msg;
	struct flexrm_ring *ring = chan->con_priv;

	/* Disable/inactivate ring */
	writel_relaxed(0x0, ring->regs + RING_CONTROL);

	/* Set ring flush state */
	timeout = 1000; /* timeout of 1s */
	writel_relaxed(BIT(CONTROL_FLUSH_SHIFT),
			ring->regs + RING_CONTROL);
	do {
		if (readl_relaxed(ring->regs + RING_FLUSH_DONE) &
		    FLUSH_DONE_MASK)
			break;
		mdelay(1);
	} while (--timeout);
	if (!timeout)
		dev_err(ring->mbox->dev,
			"setting ring%d flush state timedout\n", ring->num);

	/* Clear ring flush state */
	timeout = 1000; /* timeout of 1s */
	writel_relaxed(0x0, ring->regs + RING_CONTROL);
	do {
		if (!(readl_relaxed(ring->regs + RING_FLUSH_DONE) &
		      FLUSH_DONE_MASK))
			break;
		mdelay(1);
	} while (--timeout);
	if (!timeout)
		dev_err(ring->mbox->dev,
			"clearing ring%d flush state timedout\n", ring->num);

	/* Abort all in-flight requests */
	for (reqid = 0; reqid < RING_MAX_REQ_COUNT; reqid++) {
		msg = ring->requests[reqid];
		if (!msg)
			continue;

		/* Release reqid for recycling */
		ring->requests[reqid] = NULL;

		/* Unmap DMA mappings */
		flexrm_dma_unmap(ring->mbox->dev, msg);

		/* Give-back message to mailbox client */
		msg->error = -EIO;
		mbox_chan_received_data(chan, msg);
	}

	/* Clear requests bitmap */
	bitmap_zero(ring->requests_bmap, RING_MAX_REQ_COUNT);

	/* Release IRQ */
	if (ring->irq_requested) {
		irq_update_affinity_hint(ring->irq, NULL);
		free_irq(ring->irq, ring);
		ring->irq_requested = false;
	}

	/* Free-up completion descriptor ring */
	if (ring->cmpl_base) {
		dma_pool_free(ring->mbox->cmpl_pool,
			      ring->cmpl_base, ring->cmpl_dma_base);
		ring->cmpl_base = NULL;
	}

	/* Free-up BD descriptor ring */
	if (ring->bd_base) {
		dma_pool_free(ring->mbox->bd_pool,
			      ring->bd_base, ring->bd_dma_base);
		ring->bd_base = NULL;
	}
}

static const struct mbox_chan_ops flexrm_mbox_chan_ops = {
	.send_data	= flexrm_send_data,
	.startup	= flexrm_startup,
	.shutdown	= flexrm_shutdown,
	.peek_data	= flexrm_peek_data,
};

static struct mbox_chan *flexrm_mbox_of_xlate(struct mbox_controller *cntlr,
					const struct of_phandle_args *pa)
{
	struct mbox_chan *chan;
	struct flexrm_ring *ring;

	if (pa->args_count < 3)
		return ERR_PTR(-EINVAL);

	if (pa->args[0] >= cntlr->num_chans)
		return ERR_PTR(-ENOENT);

	if (pa->args[1] > MSI_COUNT_MASK)
		return ERR_PTR(-EINVAL);

	if (pa->args[2] > MSI_TIMER_VAL_MASK)
		return ERR_PTR(-EINVAL);

	chan = &cntlr->chans[pa->args[0]];
	ring = chan->con_priv;
	ring->msi_count_threshold = pa->args[1];
	ring->msi_timer_val = pa->args[2];

	return chan;
}

/* ====== FlexRM platform driver ===== */

static void flexrm_mbox_msi_write(struct msi_desc *desc, struct msi_msg *msg)
{
	struct device *dev = msi_desc_to_dev(desc);
	struct flexrm_mbox *mbox = dev_get_drvdata(dev);
	struct flexrm_ring *ring = &mbox->rings[desc->msi_index];

	/* Configure per-Ring MSI registers */
	writel_relaxed(msg->address_lo, ring->regs + RING_MSI_ADDR_LS);
	writel_relaxed(msg->address_hi, ring->regs + RING_MSI_ADDR_MS);
	writel_relaxed(msg->data, ring->regs + RING_MSI_DATA_VALUE);
}

static int flexrm_mbox_probe(struct platform_device *pdev)
{
	int index, ret = 0;
	void __iomem *regs;
	void __iomem *regs_end;
	struct resource *iomem;
	struct flexrm_ring *ring;
	struct flexrm_mbox *mbox;
	struct device *dev = &pdev->dev;

	/* Allocate driver mailbox struct */
	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox) {
		ret = -ENOMEM;
		goto fail;
	}
	mbox->dev = dev;
	platform_set_drvdata(pdev, mbox);

	/* Get resource for registers */
	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iomem || (resource_size(iomem) < RING_REGS_SIZE)) {
		ret = -ENODEV;
		goto fail;
	}

	/* Map registers of all rings */
	mbox->regs = devm_ioremap_resource(&pdev->dev, iomem);
	if (IS_ERR(mbox->regs)) {
		ret = PTR_ERR(mbox->regs);
		goto fail;
	}
	regs_end = mbox->regs + resource_size(iomem);

	/* Scan and count available rings */
	mbox->num_rings = 0;
	for (regs = mbox->regs; regs < regs_end; regs += RING_REGS_SIZE) {
		if (readl_relaxed(regs + RING_VER) == RING_VER_MAGIC)
			mbox->num_rings++;
	}
	if (!mbox->num_rings) {
		ret = -ENODEV;
		goto fail;
	}

	/* Allocate driver ring structs */
	ring = devm_kcalloc(dev, mbox->num_rings, sizeof(*ring), GFP_KERNEL);
	if (!ring) {
		ret = -ENOMEM;
		goto fail;
	}
	mbox->rings = ring;

	/* Initialize members of driver ring structs */
	regs = mbox->regs;
	for (index = 0; index < mbox->num_rings; index++) {
		ring = &mbox->rings[index];
		ring->num = index;
		ring->mbox = mbox;
		while ((regs < regs_end) &&
		       (readl_relaxed(regs + RING_VER) != RING_VER_MAGIC))
			regs += RING_REGS_SIZE;
		if (regs_end <= regs) {
			ret = -ENODEV;
			goto fail;
		}
		ring->regs = regs;
		regs += RING_REGS_SIZE;
		ring->irq = UINT_MAX;
		ring->irq_requested = false;
		ring->msi_timer_val = MSI_TIMER_VAL_MASK;
		ring->msi_count_threshold = 0x1;
		memset(ring->requests, 0, sizeof(ring->requests));
		ring->bd_base = NULL;
		ring->bd_dma_base = 0;
		ring->cmpl_base = NULL;
		ring->cmpl_dma_base = 0;
		atomic_set(&ring->msg_send_count, 0);
		atomic_set(&ring->msg_cmpl_count, 0);
		spin_lock_init(&ring->lock);
		bitmap_zero(ring->requests_bmap, RING_MAX_REQ_COUNT);
		ring->cmpl_read_offset = 0;
	}

	/* FlexRM is capable of 40-bit physical addresses only */
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(40));
	if (ret) {
		ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
		if (ret)
			goto fail;
	}

	/* Create DMA pool for ring BD memory */
	mbox->bd_pool = dma_pool_create("bd", dev, RING_BD_SIZE,
					1 << RING_BD_ALIGN_ORDER, 0);
	if (!mbox->bd_pool) {
		ret = -ENOMEM;
		goto fail;
	}

	/* Create DMA pool for ring completion memory */
	mbox->cmpl_pool = dma_pool_create("cmpl", dev, RING_CMPL_SIZE,
					  1 << RING_CMPL_ALIGN_ORDER, 0);
	if (!mbox->cmpl_pool) {
		ret = -ENOMEM;
		goto fail_destroy_bd_pool;
	}

	/* Allocate platform MSIs for each ring */
	ret = platform_msi_domain_alloc_irqs(dev, mbox->num_rings,
						flexrm_mbox_msi_write);
	if (ret)
		goto fail_destroy_cmpl_pool;

	/* Save alloced IRQ numbers for each ring */
	for (index = 0; index < mbox->num_rings; index++)
		mbox->rings[index].irq = msi_get_virq(dev, index);

	/* Check availability of debugfs */
	if (!debugfs_initialized())
		goto skip_debugfs;

	/* Create debugfs root entry */
	mbox->root = debugfs_create_dir(dev_name(mbox->dev), NULL);

	/* Create debugfs config entry */
	debugfs_create_devm_seqfile(mbox->dev, "config", mbox->root,
				    flexrm_debugfs_conf_show);

	/* Create debugfs stats entry */
	debugfs_create_devm_seqfile(mbox->dev, "stats", mbox->root,
				    flexrm_debugfs_stats_show);

skip_debugfs:

	/* Initialize mailbox controller */
	mbox->controller.txdone_irq = false;
	mbox->controller.txdone_poll = false;
	mbox->controller.ops = &flexrm_mbox_chan_ops;
	mbox->controller.dev = dev;
	mbox->controller.num_chans = mbox->num_rings;
	mbox->controller.of_xlate = flexrm_mbox_of_xlate;
	mbox->controller.chans = devm_kcalloc(dev, mbox->num_rings,
				sizeof(*mbox->controller.chans), GFP_KERNEL);
	if (!mbox->controller.chans) {
		ret = -ENOMEM;
		goto fail_free_debugfs_root;
	}
	for (index = 0; index < mbox->num_rings; index++)
		mbox->controller.chans[index].con_priv = &mbox->rings[index];

	/* Register mailbox controller */
	ret = devm_mbox_controller_register(dev, &mbox->controller);
	if (ret)
		goto fail_free_debugfs_root;

	dev_info(dev, "registered flexrm mailbox with %d channels\n",
			mbox->controller.num_chans);

	return 0;

fail_free_debugfs_root:
	debugfs_remove_recursive(mbox->root);
	platform_msi_domain_free_irqs(dev);
fail_destroy_cmpl_pool:
	dma_pool_destroy(mbox->cmpl_pool);
fail_destroy_bd_pool:
	dma_pool_destroy(mbox->bd_pool);
fail:
	return ret;
}

static int flexrm_mbox_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct flexrm_mbox *mbox = platform_get_drvdata(pdev);

	debugfs_remove_recursive(mbox->root);

	platform_msi_domain_free_irqs(dev);

	dma_pool_destroy(mbox->cmpl_pool);
	dma_pool_destroy(mbox->bd_pool);

	return 0;
}

static const struct of_device_id flexrm_mbox_of_match[] = {
	{ .compatible = "brcm,iproc-flexrm-mbox", },
	{},
};
MODULE_DEVICE_TABLE(of, flexrm_mbox_of_match);

static struct platform_driver flexrm_mbox_driver = {
	.driver = {
		.name = "brcm-flexrm-mbox",
		.of_match_table = flexrm_mbox_of_match,
	},
	.probe		= flexrm_mbox_probe,
	.remove		= flexrm_mbox_remove,
};
module_platform_driver(flexrm_mbox_driver);

MODULE_AUTHOR("Anup Patel <anup.patel@broadcom.com>");
MODULE_DESCRIPTION("Broadcom FlexRM mailbox driver");
MODULE_LICENSE("GPL v2");
