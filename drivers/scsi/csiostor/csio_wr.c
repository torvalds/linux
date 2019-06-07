/*
 * This file is part of the Chelsio FCoE driver for Linux.
 *
 * Copyright (c) 2008-2012 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <asm/page.h>
#include <linux/cache.h>

#include "t4_values.h"
#include "csio_hw.h"
#include "csio_wr.h"
#include "csio_mb.h"
#include "csio_defs.h"

int csio_intr_coalesce_cnt;		/* value:SGE_INGRESS_RX_THRESHOLD[0] */
static int csio_sge_thresh_reg;		/* SGE_INGRESS_RX_THRESHOLD[0] */

int csio_intr_coalesce_time = 10;	/* value:SGE_TIMER_VALUE_1 */
static int csio_sge_timer_reg = 1;

#define CSIO_SET_FLBUF_SIZE(_hw, _reg, _val)				\
	csio_wr_reg32((_hw), (_val), SGE_FL_BUFFER_SIZE##_reg##_A)

static void
csio_get_flbuf_size(struct csio_hw *hw, struct csio_sge *sge, uint32_t reg)
{
	sge->sge_fl_buf_size[reg] = csio_rd_reg32(hw, SGE_FL_BUFFER_SIZE0_A +
							reg * sizeof(uint32_t));
}

/* Free list buffer size */
static inline uint32_t
csio_wr_fl_bufsz(struct csio_sge *sge, struct csio_dma_buf *buf)
{
	return sge->sge_fl_buf_size[buf->paddr & 0xF];
}

/* Size of the egress queue status page */
static inline uint32_t
csio_wr_qstat_pgsz(struct csio_hw *hw)
{
	return (hw->wrm.sge.sge_control & EGRSTATUSPAGESIZE_F) ?  128 : 64;
}

/* Ring freelist doorbell */
static inline void
csio_wr_ring_fldb(struct csio_hw *hw, struct csio_q *flq)
{
	/*
	 * Ring the doorbell only when we have atleast CSIO_QCREDIT_SZ
	 * number of bytes in the freelist queue. This translates to atleast
	 * 8 freelist buffer pointers (since each pointer is 8 bytes).
	 */
	if (flq->inc_idx >= 8) {
		csio_wr_reg32(hw, DBPRIO_F | QID_V(flq->un.fl.flid) |
				  PIDX_T5_V(flq->inc_idx / 8) | DBTYPE_F,
				  MYPF_REG(SGE_PF_KDOORBELL_A));
		flq->inc_idx &= 7;
	}
}

/* Write a 0 cidx increment value to enable SGE interrupts for this queue */
static void
csio_wr_sge_intr_enable(struct csio_hw *hw, uint16_t iqid)
{
	csio_wr_reg32(hw, CIDXINC_V(0)		|
			  INGRESSQID_V(iqid)	|
			  TIMERREG_V(X_TIMERREG_RESTART_COUNTER),
			  MYPF_REG(SGE_PF_GTS_A));
}

/*
 * csio_wr_fill_fl - Populate the FL buffers of a FL queue.
 * @hw: HW module.
 * @flq: Freelist queue.
 *
 * Fill up freelist buffer entries with buffers of size specified
 * in the size register.
 *
 */
static int
csio_wr_fill_fl(struct csio_hw *hw, struct csio_q *flq)
{
	struct csio_wrm *wrm = csio_hw_to_wrm(hw);
	struct csio_sge *sge = &wrm->sge;
	__be64 *d = (__be64 *)(flq->vstart);
	struct csio_dma_buf *buf = &flq->un.fl.bufs[0];
	uint64_t paddr;
	int sreg = flq->un.fl.sreg;
	int n = flq->credits;

	while (n--) {
		buf->len = sge->sge_fl_buf_size[sreg];
		buf->vaddr = dma_alloc_coherent(&hw->pdev->dev, buf->len,
						&buf->paddr, GFP_KERNEL);
		if (!buf->vaddr) {
			csio_err(hw, "Could only fill %d buffers!\n", n + 1);
			return -ENOMEM;
		}

		paddr = buf->paddr | (sreg & 0xF);

		*d++ = cpu_to_be64(paddr);
		buf++;
	}

	return 0;
}

/*
 * csio_wr_update_fl -
 * @hw: HW module.
 * @flq: Freelist queue.
 *
 *
 */
static inline void
csio_wr_update_fl(struct csio_hw *hw, struct csio_q *flq, uint16_t n)
{

	flq->inc_idx += n;
	flq->pidx += n;
	if (unlikely(flq->pidx >= flq->credits))
		flq->pidx -= (uint16_t)flq->credits;

	CSIO_INC_STATS(flq, n_flq_refill);
}

/*
 * csio_wr_alloc_q - Allocate a WR queue and initialize it.
 * @hw: HW module
 * @qsize: Size of the queue in bytes
 * @wrsize: Since of WR in this queue, if fixed.
 * @type: Type of queue (Ingress/Egress/Freelist)
 * @owner: Module that owns this queue.
 * @nflb: Number of freelist buffers for FL.
 * @sreg: What is the FL buffer size register?
 * @iq_int_handler: Ingress queue handler in INTx mode.
 *
 * This function allocates and sets up a queue for the caller
 * of size qsize, aligned at the required boundary. This is subject to
 * be free entries being available in the queue array. If one is found,
 * it is initialized with the allocated queue, marked as being used (owner),
 * and a handle returned to the caller in form of the queue's index
 * into the q_arr array.
 * If user has indicated a freelist (by specifying nflb > 0), create
 * another queue (with its own index into q_arr) for the freelist. Allocate
 * memory for DMA buffer metadata (vaddr, len etc). Save off the freelist
 * idx in the ingress queue's flq.idx. This is how a Freelist is associated
 * with its owning ingress queue.
 */
int
csio_wr_alloc_q(struct csio_hw *hw, uint32_t qsize, uint32_t wrsize,
		uint16_t type, void *owner, uint32_t nflb, int sreg,
		iq_handler_t iq_intx_handler)
{
	struct csio_wrm *wrm = csio_hw_to_wrm(hw);
	struct csio_q	*q, *flq;
	int		free_idx = wrm->free_qidx;
	int		ret_idx = free_idx;
	uint32_t	qsz;
	int flq_idx;

	if (free_idx >= wrm->num_q) {
		csio_err(hw, "No more free queues.\n");
		return -1;
	}

	switch (type) {
	case CSIO_EGRESS:
		qsz = ALIGN(qsize, CSIO_QCREDIT_SZ) + csio_wr_qstat_pgsz(hw);
		break;
	case CSIO_INGRESS:
		switch (wrsize) {
		case 16:
		case 32:
		case 64:
		case 128:
			break;
		default:
			csio_err(hw, "Invalid Ingress queue WR size:%d\n",
				    wrsize);
			return -1;
		}

		/*
		 * Number of elements must be a multiple of 16
		 * So this includes status page size
		 */
		qsz = ALIGN(qsize/wrsize, 16) * wrsize;

		break;
	case CSIO_FREELIST:
		qsz = ALIGN(qsize/wrsize, 8) * wrsize + csio_wr_qstat_pgsz(hw);
		break;
	default:
		csio_err(hw, "Invalid queue type: 0x%x\n", type);
		return -1;
	}

	q = wrm->q_arr[free_idx];

	q->vstart = dma_alloc_coherent(&hw->pdev->dev, qsz, &q->pstart,
				       GFP_KERNEL);
	if (!q->vstart) {
		csio_err(hw,
			 "Failed to allocate DMA memory for "
			 "queue at id: %d size: %d\n", free_idx, qsize);
		return -1;
	}

	q->type		= type;
	q->owner	= owner;
	q->pidx		= q->cidx = q->inc_idx = 0;
	q->size		= qsz;
	q->wr_sz	= wrsize;	/* If using fixed size WRs */

	wrm->free_qidx++;

	if (type == CSIO_INGRESS) {
		/* Since queue area is set to zero */
		q->un.iq.genbit	= 1;

		/*
		 * Ingress queue status page size is always the size of
		 * the ingress queue entry.
		 */
		q->credits	= (qsz - q->wr_sz) / q->wr_sz;
		q->vwrap	= (void *)((uintptr_t)(q->vstart) + qsz
							- q->wr_sz);

		/* Allocate memory for FL if requested */
		if (nflb > 0) {
			flq_idx = csio_wr_alloc_q(hw, nflb * sizeof(__be64),
						  sizeof(__be64), CSIO_FREELIST,
						  owner, 0, sreg, NULL);
			if (flq_idx == -1) {
				csio_err(hw,
					 "Failed to allocate FL queue"
					 " for IQ idx:%d\n", free_idx);
				return -1;
			}

			/* Associate the new FL with the Ingress quue */
			q->un.iq.flq_idx = flq_idx;

			flq = wrm->q_arr[q->un.iq.flq_idx];
			flq->un.fl.bufs = kcalloc(flq->credits,
						  sizeof(struct csio_dma_buf),
						  GFP_KERNEL);
			if (!flq->un.fl.bufs) {
				csio_err(hw,
					 "Failed to allocate FL queue bufs"
					 " for IQ idx:%d\n", free_idx);
				return -1;
			}

			flq->un.fl.packen = 0;
			flq->un.fl.offset = 0;
			flq->un.fl.sreg = sreg;

			/* Fill up the free list buffers */
			if (csio_wr_fill_fl(hw, flq))
				return -1;

			/*
			 * Make sure in a FLQ, atleast 1 credit (8 FL buffers)
			 * remains unpopulated,otherwise HW thinks
			 * FLQ is empty.
			 */
			flq->pidx = flq->inc_idx = flq->credits - 8;
		} else {
			q->un.iq.flq_idx = -1;
		}

		/* Associate the IQ INTx handler. */
		q->un.iq.iq_intx_handler = iq_intx_handler;

		csio_q_iqid(hw, ret_idx) = CSIO_MAX_QID;

	} else if (type == CSIO_EGRESS) {
		q->credits = (qsz - csio_wr_qstat_pgsz(hw)) / CSIO_QCREDIT_SZ;
		q->vwrap   = (void *)((uintptr_t)(q->vstart) + qsz
						- csio_wr_qstat_pgsz(hw));
		csio_q_eqid(hw, ret_idx) = CSIO_MAX_QID;
	} else { /* Freelist */
		q->credits = (qsz - csio_wr_qstat_pgsz(hw)) / sizeof(__be64);
		q->vwrap   = (void *)((uintptr_t)(q->vstart) + qsz
						- csio_wr_qstat_pgsz(hw));
		csio_q_flid(hw, ret_idx) = CSIO_MAX_QID;
	}

	return ret_idx;
}

/*
 * csio_wr_iq_create_rsp - Response handler for IQ creation.
 * @hw: The HW module.
 * @mbp: Mailbox.
 * @iq_idx: Ingress queue that got created.
 *
 * Handle FW_IQ_CMD mailbox completion. Save off the assigned IQ/FL ids.
 */
static int
csio_wr_iq_create_rsp(struct csio_hw *hw, struct csio_mb *mbp, int iq_idx)
{
	struct csio_iq_params iqp;
	enum fw_retval retval;
	uint32_t iq_id;
	int flq_idx;

	memset(&iqp, 0, sizeof(struct csio_iq_params));

	csio_mb_iq_alloc_write_rsp(hw, mbp, &retval, &iqp);

	if (retval != FW_SUCCESS) {
		csio_err(hw, "IQ cmd returned 0x%x!\n", retval);
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	csio_q_iqid(hw, iq_idx)		= iqp.iqid;
	csio_q_physiqid(hw, iq_idx)	= iqp.physiqid;
	csio_q_pidx(hw, iq_idx)		= csio_q_cidx(hw, iq_idx) = 0;
	csio_q_inc_idx(hw, iq_idx)	= 0;

	/* Actual iq-id. */
	iq_id = iqp.iqid - hw->wrm.fw_iq_start;

	/* Set the iq-id to iq map table. */
	if (iq_id >= CSIO_MAX_IQ) {
		csio_err(hw,
			 "Exceeding MAX_IQ(%d) supported!"
			 " iqid:%d rel_iqid:%d FW iq_start:%d\n",
			 CSIO_MAX_IQ, iq_id, iqp.iqid, hw->wrm.fw_iq_start);
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}
	csio_q_set_intr_map(hw, iq_idx, iq_id);

	/*
	 * During FW_IQ_CMD, FW sets interrupt_sent bit to 1 in the SGE
	 * ingress context of this queue. This will block interrupts to
	 * this queue until the next GTS write. Therefore, we do a
	 * 0-cidx increment GTS write for this queue just to clear the
	 * interrupt_sent bit. This will re-enable interrupts to this
	 * queue.
	 */
	csio_wr_sge_intr_enable(hw, iqp.physiqid);

	flq_idx = csio_q_iq_flq_idx(hw, iq_idx);
	if (flq_idx != -1) {
		struct csio_q *flq = hw->wrm.q_arr[flq_idx];

		csio_q_flid(hw, flq_idx) = iqp.fl0id;
		csio_q_cidx(hw, flq_idx) = 0;
		csio_q_pidx(hw, flq_idx)    = csio_q_credits(hw, flq_idx) - 8;
		csio_q_inc_idx(hw, flq_idx) = csio_q_credits(hw, flq_idx) - 8;

		/* Now update SGE about the buffers allocated during init */
		csio_wr_ring_fldb(hw, flq);
	}

	mempool_free(mbp, hw->mb_mempool);

	return 0;
}

/*
 * csio_wr_iq_create - Configure an Ingress queue with FW.
 * @hw: The HW module.
 * @priv: Private data object.
 * @iq_idx: Ingress queue index in the WR module.
 * @vec: MSIX vector.
 * @portid: PCIE Channel to be associated with this queue.
 * @async: Is this a FW asynchronous message handling queue?
 * @cbfn: Completion callback.
 *
 * This API configures an ingress queue with FW by issuing a FW_IQ_CMD mailbox
 * with alloc/write bits set.
 */
int
csio_wr_iq_create(struct csio_hw *hw, void *priv, int iq_idx,
		  uint32_t vec, uint8_t portid, bool async,
		  void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct csio_mb  *mbp;
	struct csio_iq_params iqp;
	int flq_idx;

	memset(&iqp, 0, sizeof(struct csio_iq_params));
	csio_q_portid(hw, iq_idx) = portid;

	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp) {
		csio_err(hw, "IQ command out of memory!\n");
		return -ENOMEM;
	}

	switch (hw->intr_mode) {
	case CSIO_IM_INTX:
	case CSIO_IM_MSI:
		/* For interrupt forwarding queue only */
		if (hw->intr_iq_idx == iq_idx)
			iqp.iqandst	= X_INTERRUPTDESTINATION_PCIE;
		else
			iqp.iqandst	= X_INTERRUPTDESTINATION_IQ;
		iqp.iqandstindex	=
			csio_q_physiqid(hw, hw->intr_iq_idx);
		break;
	case CSIO_IM_MSIX:
		iqp.iqandst		= X_INTERRUPTDESTINATION_PCIE;
		iqp.iqandstindex	= (uint16_t)vec;
		break;
	case CSIO_IM_NONE:
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	/* Pass in the ingress queue cmd parameters */
	iqp.pfn			= hw->pfn;
	iqp.vfn			= 0;
	iqp.iq_start		= 1;
	iqp.viid		= 0;
	iqp.type		= FW_IQ_TYPE_FL_INT_CAP;
	iqp.iqasynch		= async;
	if (csio_intr_coalesce_cnt)
		iqp.iqanus	= X_UPDATESCHEDULING_COUNTER_OPTTIMER;
	else
		iqp.iqanus	= X_UPDATESCHEDULING_TIMER;
	iqp.iqanud		= X_UPDATEDELIVERY_INTERRUPT;
	iqp.iqpciech		= portid;
	iqp.iqintcntthresh	= (uint8_t)csio_sge_thresh_reg;

	switch (csio_q_wr_sz(hw, iq_idx)) {
	case 16:
		iqp.iqesize = 0; break;
	case 32:
		iqp.iqesize = 1; break;
	case 64:
		iqp.iqesize = 2; break;
	case 128:
		iqp.iqesize = 3; break;
	}

	iqp.iqsize		= csio_q_size(hw, iq_idx) /
						csio_q_wr_sz(hw, iq_idx);
	iqp.iqaddr		= csio_q_pstart(hw, iq_idx);

	flq_idx = csio_q_iq_flq_idx(hw, iq_idx);
	if (flq_idx != -1) {
		enum chip_type chip = CHELSIO_CHIP_VERSION(hw->chip_id);
		struct csio_q *flq = hw->wrm.q_arr[flq_idx];

		iqp.fl0paden	= 1;
		iqp.fl0packen	= flq->un.fl.packen ? 1 : 0;
		iqp.fl0fbmin	= X_FETCHBURSTMIN_64B;
		iqp.fl0fbmax	= ((chip == CHELSIO_T5) ?
				  X_FETCHBURSTMAX_512B : X_FETCHBURSTMAX_256B);
		iqp.fl0size	= csio_q_size(hw, flq_idx) / CSIO_QCREDIT_SZ;
		iqp.fl0addr	= csio_q_pstart(hw, flq_idx);
	}

	csio_mb_iq_alloc_write(hw, mbp, priv, CSIO_MB_DEFAULT_TMO, &iqp, cbfn);

	if (csio_mb_issue(hw, mbp)) {
		csio_err(hw, "Issue of IQ cmd failed!\n");
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	if (cbfn != NULL)
		return 0;

	return csio_wr_iq_create_rsp(hw, mbp, iq_idx);
}

/*
 * csio_wr_eq_create_rsp - Response handler for EQ creation.
 * @hw: The HW module.
 * @mbp: Mailbox.
 * @eq_idx: Egress queue that got created.
 *
 * Handle FW_EQ_OFLD_CMD mailbox completion. Save off the assigned EQ ids.
 */
static int
csio_wr_eq_cfg_rsp(struct csio_hw *hw, struct csio_mb *mbp, int eq_idx)
{
	struct csio_eq_params eqp;
	enum fw_retval retval;

	memset(&eqp, 0, sizeof(struct csio_eq_params));

	csio_mb_eq_ofld_alloc_write_rsp(hw, mbp, &retval, &eqp);

	if (retval != FW_SUCCESS) {
		csio_err(hw, "EQ OFLD cmd returned 0x%x!\n", retval);
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	csio_q_eqid(hw, eq_idx)	= (uint16_t)eqp.eqid;
	csio_q_physeqid(hw, eq_idx) = (uint16_t)eqp.physeqid;
	csio_q_pidx(hw, eq_idx)	= csio_q_cidx(hw, eq_idx) = 0;
	csio_q_inc_idx(hw, eq_idx) = 0;

	mempool_free(mbp, hw->mb_mempool);

	return 0;
}

/*
 * csio_wr_eq_create - Configure an Egress queue with FW.
 * @hw: HW module.
 * @priv: Private data.
 * @eq_idx: Egress queue index in the WR module.
 * @iq_idx: Associated ingress queue index.
 * @cbfn: Completion callback.
 *
 * This API configures a offload egress queue with FW by issuing a
 * FW_EQ_OFLD_CMD  (with alloc + write ) mailbox.
 */
int
csio_wr_eq_create(struct csio_hw *hw, void *priv, int eq_idx,
		  int iq_idx, uint8_t portid,
		  void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct csio_mb  *mbp;
	struct csio_eq_params eqp;

	memset(&eqp, 0, sizeof(struct csio_eq_params));

	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp) {
		csio_err(hw, "EQ command out of memory!\n");
		return -ENOMEM;
	}

	eqp.pfn			= hw->pfn;
	eqp.vfn			= 0;
	eqp.eqstart		= 1;
	eqp.hostfcmode		= X_HOSTFCMODE_STATUS_PAGE;
	eqp.iqid		= csio_q_iqid(hw, iq_idx);
	eqp.fbmin		= X_FETCHBURSTMIN_64B;
	eqp.fbmax		= X_FETCHBURSTMAX_512B;
	eqp.cidxfthresh		= 0;
	eqp.pciechn		= portid;
	eqp.eqsize		= csio_q_size(hw, eq_idx) / CSIO_QCREDIT_SZ;
	eqp.eqaddr		= csio_q_pstart(hw, eq_idx);

	csio_mb_eq_ofld_alloc_write(hw, mbp, priv, CSIO_MB_DEFAULT_TMO,
				    &eqp, cbfn);

	if (csio_mb_issue(hw, mbp)) {
		csio_err(hw, "Issue of EQ OFLD cmd failed!\n");
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	if (cbfn != NULL)
		return 0;

	return csio_wr_eq_cfg_rsp(hw, mbp, eq_idx);
}

/*
 * csio_wr_iq_destroy_rsp - Response handler for IQ removal.
 * @hw: The HW module.
 * @mbp: Mailbox.
 * @iq_idx: Ingress queue that was freed.
 *
 * Handle FW_IQ_CMD (free) mailbox completion.
 */
static int
csio_wr_iq_destroy_rsp(struct csio_hw *hw, struct csio_mb *mbp, int iq_idx)
{
	enum fw_retval retval = csio_mb_fw_retval(mbp);
	int rv = 0;

	if (retval != FW_SUCCESS)
		rv = -EINVAL;

	mempool_free(mbp, hw->mb_mempool);

	return rv;
}

/*
 * csio_wr_iq_destroy - Free an ingress queue.
 * @hw: The HW module.
 * @priv: Private data object.
 * @iq_idx: Ingress queue index to destroy
 * @cbfn: Completion callback.
 *
 * This API frees an ingress queue by issuing the FW_IQ_CMD
 * with the free bit set.
 */
static int
csio_wr_iq_destroy(struct csio_hw *hw, void *priv, int iq_idx,
		   void (*cbfn)(struct csio_hw *, struct csio_mb *))
{
	int rv = 0;
	struct csio_mb  *mbp;
	struct csio_iq_params iqp;
	int flq_idx;

	memset(&iqp, 0, sizeof(struct csio_iq_params));

	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp)
		return -ENOMEM;

	iqp.pfn		= hw->pfn;
	iqp.vfn		= 0;
	iqp.iqid	= csio_q_iqid(hw, iq_idx);
	iqp.type	= FW_IQ_TYPE_FL_INT_CAP;

	flq_idx = csio_q_iq_flq_idx(hw, iq_idx);
	if (flq_idx != -1)
		iqp.fl0id = csio_q_flid(hw, flq_idx);
	else
		iqp.fl0id = 0xFFFF;

	iqp.fl1id = 0xFFFF;

	csio_mb_iq_free(hw, mbp, priv, CSIO_MB_DEFAULT_TMO, &iqp, cbfn);

	rv = csio_mb_issue(hw, mbp);
	if (rv != 0) {
		mempool_free(mbp, hw->mb_mempool);
		return rv;
	}

	if (cbfn != NULL)
		return 0;

	return csio_wr_iq_destroy_rsp(hw, mbp, iq_idx);
}

/*
 * csio_wr_eq_destroy_rsp - Response handler for OFLD EQ creation.
 * @hw: The HW module.
 * @mbp: Mailbox.
 * @eq_idx: Egress queue that was freed.
 *
 * Handle FW_OFLD_EQ_CMD (free) mailbox completion.
 */
static int
csio_wr_eq_destroy_rsp(struct csio_hw *hw, struct csio_mb *mbp, int eq_idx)
{
	enum fw_retval retval = csio_mb_fw_retval(mbp);
	int rv = 0;

	if (retval != FW_SUCCESS)
		rv = -EINVAL;

	mempool_free(mbp, hw->mb_mempool);

	return rv;
}

/*
 * csio_wr_eq_destroy - Free an Egress queue.
 * @hw: The HW module.
 * @priv: Private data object.
 * @eq_idx: Egress queue index to destroy
 * @cbfn: Completion callback.
 *
 * This API frees an Egress queue by issuing the FW_EQ_OFLD_CMD
 * with the free bit set.
 */
static int
csio_wr_eq_destroy(struct csio_hw *hw, void *priv, int eq_idx,
		   void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	int rv = 0;
	struct csio_mb  *mbp;
	struct csio_eq_params eqp;

	memset(&eqp, 0, sizeof(struct csio_eq_params));

	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp)
		return -ENOMEM;

	eqp.pfn		= hw->pfn;
	eqp.vfn		= 0;
	eqp.eqid	= csio_q_eqid(hw, eq_idx);

	csio_mb_eq_ofld_free(hw, mbp, priv, CSIO_MB_DEFAULT_TMO, &eqp, cbfn);

	rv = csio_mb_issue(hw, mbp);
	if (rv != 0) {
		mempool_free(mbp, hw->mb_mempool);
		return rv;
	}

	if (cbfn != NULL)
		return 0;

	return csio_wr_eq_destroy_rsp(hw, mbp, eq_idx);
}

/*
 * csio_wr_cleanup_eq_stpg - Cleanup Egress queue status page
 * @hw: HW module
 * @qidx: Egress queue index
 *
 * Cleanup the Egress queue status page.
 */
static void
csio_wr_cleanup_eq_stpg(struct csio_hw *hw, int qidx)
{
	struct csio_q	*q = csio_hw_to_wrm(hw)->q_arr[qidx];
	struct csio_qstatus_page *stp = (struct csio_qstatus_page *)q->vwrap;

	memset(stp, 0, sizeof(*stp));
}

/*
 * csio_wr_cleanup_iq_ftr - Cleanup Footer entries in IQ
 * @hw: HW module
 * @qidx: Ingress queue index
 *
 * Cleanup the footer entries in the given ingress queue,
 * set to 1 the internal copy of genbit.
 */
static void
csio_wr_cleanup_iq_ftr(struct csio_hw *hw, int qidx)
{
	struct csio_wrm *wrm	= csio_hw_to_wrm(hw);
	struct csio_q	*q	= wrm->q_arr[qidx];
	void *wr;
	struct csio_iqwr_footer *ftr;
	uint32_t i = 0;

	/* set to 1 since we are just about zero out genbit */
	q->un.iq.genbit = 1;

	for (i = 0; i < q->credits; i++) {
		/* Get the WR */
		wr = (void *)((uintptr_t)q->vstart +
					   (i * q->wr_sz));
		/* Get the footer */
		ftr = (struct csio_iqwr_footer *)((uintptr_t)wr +
					  (q->wr_sz - sizeof(*ftr)));
		/* Zero out footer */
		memset(ftr, 0, sizeof(*ftr));
	}
}

int
csio_wr_destroy_queues(struct csio_hw *hw, bool cmd)
{
	int i, flq_idx;
	struct csio_q *q;
	struct csio_wrm *wrm = csio_hw_to_wrm(hw);
	int rv;

	for (i = 0; i < wrm->free_qidx; i++) {
		q = wrm->q_arr[i];

		switch (q->type) {
		case CSIO_EGRESS:
			if (csio_q_eqid(hw, i) != CSIO_MAX_QID) {
				csio_wr_cleanup_eq_stpg(hw, i);
				if (!cmd) {
					csio_q_eqid(hw, i) = CSIO_MAX_QID;
					continue;
				}

				rv = csio_wr_eq_destroy(hw, NULL, i, NULL);
				if ((rv == -EBUSY) || (rv == -ETIMEDOUT))
					cmd = false;

				csio_q_eqid(hw, i) = CSIO_MAX_QID;
			}
			/* fall through */
		case CSIO_INGRESS:
			if (csio_q_iqid(hw, i) != CSIO_MAX_QID) {
				csio_wr_cleanup_iq_ftr(hw, i);
				if (!cmd) {
					csio_q_iqid(hw, i) = CSIO_MAX_QID;
					flq_idx = csio_q_iq_flq_idx(hw, i);
					if (flq_idx != -1)
						csio_q_flid(hw, flq_idx) =
								CSIO_MAX_QID;
					continue;
				}

				rv = csio_wr_iq_destroy(hw, NULL, i, NULL);
				if ((rv == -EBUSY) || (rv == -ETIMEDOUT))
					cmd = false;

				csio_q_iqid(hw, i) = CSIO_MAX_QID;
				flq_idx = csio_q_iq_flq_idx(hw, i);
				if (flq_idx != -1)
					csio_q_flid(hw, flq_idx) = CSIO_MAX_QID;
			}
		default:
			break;
		}
	}

	hw->flags &= ~CSIO_HWF_Q_FW_ALLOCED;

	return 0;
}

/*
 * csio_wr_get - Get requested size of WR entry/entries from queue.
 * @hw: HW module.
 * @qidx: Index of queue.
 * @size: Cumulative size of Work request(s).
 * @wrp: Work request pair.
 *
 * If requested credits are available, return the start address of the
 * work request in the work request pair. Set pidx accordingly and
 * return.
 *
 * NOTE about WR pair:
 * ==================
 * A WR can start towards the end of a queue, and then continue at the
 * beginning, since the queue is considered to be circular. This will
 * require a pair of address/size to be passed back to the caller -
 * hence Work request pair format.
 */
int
csio_wr_get(struct csio_hw *hw, int qidx, uint32_t size,
	    struct csio_wr_pair *wrp)
{
	struct csio_wrm *wrm = csio_hw_to_wrm(hw);
	struct csio_q *q = wrm->q_arr[qidx];
	void *cwr = (void *)((uintptr_t)(q->vstart) +
						(q->pidx * CSIO_QCREDIT_SZ));
	struct csio_qstatus_page *stp = (struct csio_qstatus_page *)q->vwrap;
	uint16_t cidx = q->cidx = ntohs(stp->cidx);
	uint16_t pidx = q->pidx;
	uint32_t req_sz	= ALIGN(size, CSIO_QCREDIT_SZ);
	int req_credits	= req_sz / CSIO_QCREDIT_SZ;
	int credits;

	CSIO_DB_ASSERT(q->owner != NULL);
	CSIO_DB_ASSERT((qidx >= 0) && (qidx < wrm->free_qidx));
	CSIO_DB_ASSERT(cidx <= q->credits);

	/* Calculate credits */
	if (pidx > cidx) {
		credits = q->credits - (pidx - cidx) - 1;
	} else if (cidx > pidx) {
		credits = cidx - pidx - 1;
	} else {
		/* cidx == pidx, empty queue */
		credits = q->credits;
		CSIO_INC_STATS(q, n_qempty);
	}

	/*
	 * Check if we have enough credits.
	 * credits = 1 implies queue is full.
	 */
	if (!credits || (req_credits > credits)) {
		CSIO_INC_STATS(q, n_qfull);
		return -EBUSY;
	}

	/*
	 * If we are here, we have enough credits to satisfy the
	 * request. Check if we are near the end of q, and if WR spills over.
	 * If it does, use the first addr/size to cover the queue until
	 * the end. Fit the remainder portion of the request at the top
	 * of queue and return it in the second addr/len. Set pidx
	 * accordingly.
	 */
	if (unlikely(((uintptr_t)cwr + req_sz) > (uintptr_t)(q->vwrap))) {
		wrp->addr1 = cwr;
		wrp->size1 = (uint32_t)((uintptr_t)q->vwrap - (uintptr_t)cwr);
		wrp->addr2 = q->vstart;
		wrp->size2 = req_sz - wrp->size1;
		q->pidx	= (uint16_t)(ALIGN(wrp->size2, CSIO_QCREDIT_SZ) /
							CSIO_QCREDIT_SZ);
		CSIO_INC_STATS(q, n_qwrap);
		CSIO_INC_STATS(q, n_eq_wr_split);
	} else {
		wrp->addr1 = cwr;
		wrp->size1 = req_sz;
		wrp->addr2 = NULL;
		wrp->size2 = 0;
		q->pidx	+= (uint16_t)req_credits;

		/* We are the end of queue, roll back pidx to top of queue */
		if (unlikely(q->pidx == q->credits)) {
			q->pidx = 0;
			CSIO_INC_STATS(q, n_qwrap);
		}
	}

	q->inc_idx = (uint16_t)req_credits;

	CSIO_INC_STATS(q, n_tot_reqs);

	return 0;
}

/*
 * csio_wr_copy_to_wrp - Copies given data into WR.
 * @data_buf - Data buffer
 * @wrp - Work request pair.
 * @wr_off - Work request offset.
 * @data_len - Data length.
 *
 * Copies the given data in Work Request. Work request pair(wrp) specifies
 * address information of Work request.
 * Returns: none
 */
void
csio_wr_copy_to_wrp(void *data_buf, struct csio_wr_pair *wrp,
		   uint32_t wr_off, uint32_t data_len)
{
	uint32_t nbytes;

	/* Number of space available in buffer addr1 of WRP */
	nbytes = ((wrp->size1 - wr_off) >= data_len) ?
					data_len : (wrp->size1 - wr_off);

	memcpy((uint8_t *) wrp->addr1 + wr_off, data_buf, nbytes);
	data_len -= nbytes;

	/* Write the remaining data from the begining of circular buffer */
	if (data_len) {
		CSIO_DB_ASSERT(data_len <= wrp->size2);
		CSIO_DB_ASSERT(wrp->addr2 != NULL);
		memcpy(wrp->addr2, (uint8_t *) data_buf + nbytes, data_len);
	}
}

/*
 * csio_wr_issue - Notify chip of Work request.
 * @hw: HW module.
 * @qidx: Index of queue.
 * @prio: 0: Low priority, 1: High priority
 *
 * Rings the SGE Doorbell by writing the current producer index of the passed
 * in queue into the register.
 *
 */
int
csio_wr_issue(struct csio_hw *hw, int qidx, bool prio)
{
	struct csio_wrm *wrm = csio_hw_to_wrm(hw);
	struct csio_q *q = wrm->q_arr[qidx];

	CSIO_DB_ASSERT((qidx >= 0) && (qidx < wrm->free_qidx));

	wmb();
	/* Ring SGE Doorbell writing q->pidx into it */
	csio_wr_reg32(hw, DBPRIO_V(prio) | QID_V(q->un.eq.physeqid) |
			  PIDX_T5_V(q->inc_idx) | DBTYPE_F,
			  MYPF_REG(SGE_PF_KDOORBELL_A));
	q->inc_idx = 0;

	return 0;
}

static inline uint32_t
csio_wr_avail_qcredits(struct csio_q *q)
{
	if (q->pidx > q->cidx)
		return q->pidx - q->cidx;
	else if (q->cidx > q->pidx)
		return q->credits - (q->cidx - q->pidx);
	else
		return 0;	/* cidx == pidx, empty queue */
}

/*
 * csio_wr_inval_flq_buf - Invalidate a free list buffer entry.
 * @hw: HW module.
 * @flq: The freelist queue.
 *
 * Invalidate the driver's version of a freelist buffer entry,
 * without freeing the associated the DMA memory. The entry
 * to be invalidated is picked up from the current Free list
 * queue cidx.
 *
 */
static inline void
csio_wr_inval_flq_buf(struct csio_hw *hw, struct csio_q *flq)
{
	flq->cidx++;
	if (flq->cidx == flq->credits) {
		flq->cidx = 0;
		CSIO_INC_STATS(flq, n_qwrap);
	}
}

/*
 * csio_wr_process_fl - Process a freelist completion.
 * @hw: HW module.
 * @q: The ingress queue attached to the Freelist.
 * @wr: The freelist completion WR in the ingress queue.
 * @len_to_qid: The lower 32-bits of the first flit of the RSP footer
 * @iq_handler: Caller's handler for this completion.
 * @priv: Private pointer of caller
 *
 */
static inline void
csio_wr_process_fl(struct csio_hw *hw, struct csio_q *q,
		   void *wr, uint32_t len_to_qid,
		   void (*iq_handler)(struct csio_hw *, void *,
				      uint32_t, struct csio_fl_dma_buf *,
				      void *),
		   void *priv)
{
	struct csio_wrm *wrm = csio_hw_to_wrm(hw);
	struct csio_sge *sge = &wrm->sge;
	struct csio_fl_dma_buf flb;
	struct csio_dma_buf *buf, *fbuf;
	uint32_t bufsz, len, lastlen = 0;
	int n;
	struct csio_q *flq = hw->wrm.q_arr[q->un.iq.flq_idx];

	CSIO_DB_ASSERT(flq != NULL);

	len = len_to_qid;

	if (len & IQWRF_NEWBUF) {
		if (flq->un.fl.offset > 0) {
			csio_wr_inval_flq_buf(hw, flq);
			flq->un.fl.offset = 0;
		}
		len = IQWRF_LEN_GET(len);
	}

	CSIO_DB_ASSERT(len != 0);

	flb.totlen = len;

	/* Consume all freelist buffers used for len bytes */
	for (n = 0, fbuf = flb.flbufs; ; n++, fbuf++) {
		buf = &flq->un.fl.bufs[flq->cidx];
		bufsz = csio_wr_fl_bufsz(sge, buf);

		fbuf->paddr	= buf->paddr;
		fbuf->vaddr	= buf->vaddr;

		flb.offset	= flq->un.fl.offset;
		lastlen		= min(bufsz, len);
		fbuf->len	= lastlen;

		len -= lastlen;
		if (!len)
			break;
		csio_wr_inval_flq_buf(hw, flq);
	}

	flb.defer_free = flq->un.fl.packen ? 0 : 1;

	iq_handler(hw, wr, q->wr_sz - sizeof(struct csio_iqwr_footer),
		   &flb, priv);

	if (flq->un.fl.packen)
		flq->un.fl.offset += ALIGN(lastlen, sge->csio_fl_align);
	else
		csio_wr_inval_flq_buf(hw, flq);

}

/*
 * csio_is_new_iqwr - Is this a new Ingress queue entry ?
 * @q: Ingress quueue.
 * @ftr: Ingress queue WR SGE footer.
 *
 * The entry is new if our generation bit matches the corresponding
 * bit in the footer of the current WR.
 */
static inline bool
csio_is_new_iqwr(struct csio_q *q, struct csio_iqwr_footer *ftr)
{
	return (q->un.iq.genbit == (ftr->u.type_gen >> IQWRF_GEN_SHIFT));
}

/*
 * csio_wr_process_iq - Process elements in Ingress queue.
 * @hw:  HW pointer
 * @qidx: Index of queue
 * @iq_handler: Handler for this queue
 * @priv: Caller's private pointer
 *
 * This routine walks through every entry of the ingress queue, calling
 * the provided iq_handler with the entry, until the generation bit
 * flips.
 */
int
csio_wr_process_iq(struct csio_hw *hw, struct csio_q *q,
		   void (*iq_handler)(struct csio_hw *, void *,
				      uint32_t, struct csio_fl_dma_buf *,
				      void *),
		   void *priv)
{
	struct csio_wrm *wrm = csio_hw_to_wrm(hw);
	void *wr = (void *)((uintptr_t)q->vstart + (q->cidx * q->wr_sz));
	struct csio_iqwr_footer *ftr;
	uint32_t wr_type, fw_qid, qid;
	struct csio_q *q_completed;
	struct csio_q *flq = csio_iq_has_fl(q) ?
					wrm->q_arr[q->un.iq.flq_idx] : NULL;
	int rv = 0;

	/* Get the footer */
	ftr = (struct csio_iqwr_footer *)((uintptr_t)wr +
					  (q->wr_sz - sizeof(*ftr)));

	/*
	 * When q wrapped around last time, driver should have inverted
	 * ic.genbit as well.
	 */
	while (csio_is_new_iqwr(q, ftr)) {

		CSIO_DB_ASSERT(((uintptr_t)wr + q->wr_sz) <=
						(uintptr_t)q->vwrap);
		rmb();
		wr_type = IQWRF_TYPE_GET(ftr->u.type_gen);

		switch (wr_type) {
		case X_RSPD_TYPE_CPL:
			/* Subtract footer from WR len */
			iq_handler(hw, wr, q->wr_sz - sizeof(*ftr), NULL, priv);
			break;
		case X_RSPD_TYPE_FLBUF:
			csio_wr_process_fl(hw, q, wr,
					   ntohl(ftr->pldbuflen_qid),
					   iq_handler, priv);
			break;
		case X_RSPD_TYPE_INTR:
			fw_qid = ntohl(ftr->pldbuflen_qid);
			qid = fw_qid - wrm->fw_iq_start;
			q_completed = hw->wrm.intr_map[qid];

			if (unlikely(qid ==
					csio_q_physiqid(hw, hw->intr_iq_idx))) {
				/*
				 * We are already in the Forward Interrupt
				 * Interrupt Queue Service! Do-not service
				 * again!
				 *
				 */
			} else {
				CSIO_DB_ASSERT(q_completed);
				CSIO_DB_ASSERT(
					q_completed->un.iq.iq_intx_handler);

				/* Call the queue handler. */
				q_completed->un.iq.iq_intx_handler(hw, NULL,
						0, NULL, (void *)q_completed);
			}
			break;
		default:
			csio_warn(hw, "Unknown resp type 0x%x received\n",
				 wr_type);
			CSIO_INC_STATS(q, n_rsp_unknown);
			break;
		}

		/*
		 * Ingress *always* has fixed size WR entries. Therefore,
		 * there should always be complete WRs towards the end of
		 * queue.
		 */
		if (((uintptr_t)wr + q->wr_sz) == (uintptr_t)q->vwrap) {

			/* Roll over to start of queue */
			q->cidx = 0;
			wr	= q->vstart;

			/* Toggle genbit */
			q->un.iq.genbit ^= 0x1;

			CSIO_INC_STATS(q, n_qwrap);
		} else {
			q->cidx++;
			wr	= (void *)((uintptr_t)(q->vstart) +
					   (q->cidx * q->wr_sz));
		}

		ftr = (struct csio_iqwr_footer *)((uintptr_t)wr +
						  (q->wr_sz - sizeof(*ftr)));
		q->inc_idx++;

	} /* while (q->un.iq.genbit == hdr->genbit) */

	/*
	 * We need to re-arm SGE interrupts in case we got a stray interrupt,
	 * especially in msix mode. With INTx, this may be a common occurence.
	 */
	if (unlikely(!q->inc_idx)) {
		CSIO_INC_STATS(q, n_stray_comp);
		rv = -EINVAL;
		goto restart;
	}

	/* Replenish free list buffers if pending falls below low water mark */
	if (flq) {
		uint32_t avail  = csio_wr_avail_qcredits(flq);
		if (avail <= 16) {
			/* Make sure in FLQ, atleast 1 credit (8 FL buffers)
			 * remains unpopulated otherwise HW thinks
			 * FLQ is empty.
			 */
			csio_wr_update_fl(hw, flq, (flq->credits - 8) - avail);
			csio_wr_ring_fldb(hw, flq);
		}
	}

restart:
	/* Now inform SGE about our incremental index value */
	csio_wr_reg32(hw, CIDXINC_V(q->inc_idx)		|
			  INGRESSQID_V(q->un.iq.physiqid)	|
			  TIMERREG_V(csio_sge_timer_reg),
			  MYPF_REG(SGE_PF_GTS_A));
	q->stats.n_tot_rsps += q->inc_idx;

	q->inc_idx = 0;

	return rv;
}

int
csio_wr_process_iq_idx(struct csio_hw *hw, int qidx,
		   void (*iq_handler)(struct csio_hw *, void *,
				      uint32_t, struct csio_fl_dma_buf *,
				      void *),
		   void *priv)
{
	struct csio_wrm *wrm	= csio_hw_to_wrm(hw);
	struct csio_q	*iq	= wrm->q_arr[qidx];

	return csio_wr_process_iq(hw, iq, iq_handler, priv);
}

static int
csio_closest_timer(struct csio_sge *s, int time)
{
	int i, delta, match = 0, min_delta = INT_MAX;

	for (i = 0; i < ARRAY_SIZE(s->timer_val); i++) {
		delta = time - s->timer_val[i];
		if (delta < 0)
			delta = -delta;
		if (delta < min_delta) {
			min_delta = delta;
			match = i;
		}
	}
	return match;
}

static int
csio_closest_thresh(struct csio_sge *s, int cnt)
{
	int i, delta, match = 0, min_delta = INT_MAX;

	for (i = 0; i < ARRAY_SIZE(s->counter_val); i++) {
		delta = cnt - s->counter_val[i];
		if (delta < 0)
			delta = -delta;
		if (delta < min_delta) {
			min_delta = delta;
			match = i;
		}
	}
	return match;
}

static void
csio_wr_fixup_host_params(struct csio_hw *hw)
{
	struct csio_wrm *wrm = csio_hw_to_wrm(hw);
	struct csio_sge *sge = &wrm->sge;
	uint32_t clsz = L1_CACHE_BYTES;
	uint32_t s_hps = PAGE_SHIFT - 10;
	uint32_t stat_len = clsz > 64 ? 128 : 64;
	u32 fl_align = clsz < 32 ? 32 : clsz;
	u32 pack_align;
	u32 ingpad, ingpack;
	int pcie_cap;

	csio_wr_reg32(hw, HOSTPAGESIZEPF0_V(s_hps) | HOSTPAGESIZEPF1_V(s_hps) |
		      HOSTPAGESIZEPF2_V(s_hps) | HOSTPAGESIZEPF3_V(s_hps) |
		      HOSTPAGESIZEPF4_V(s_hps) | HOSTPAGESIZEPF5_V(s_hps) |
		      HOSTPAGESIZEPF6_V(s_hps) | HOSTPAGESIZEPF7_V(s_hps),
		      SGE_HOST_PAGE_SIZE_A);

	/* T5 introduced the separation of the Free List Padding and
	 * Packing Boundaries.  Thus, we can select a smaller Padding
	 * Boundary to avoid uselessly chewing up PCIe Link and Memory
	 * Bandwidth, and use a Packing Boundary which is large enough
	 * to avoid false sharing between CPUs, etc.
	 *
	 * For the PCI Link, the smaller the Padding Boundary the
	 * better.  For the Memory Controller, a smaller Padding
	 * Boundary is better until we cross under the Memory Line
	 * Size (the minimum unit of transfer to/from Memory).  If we
	 * have a Padding Boundary which is smaller than the Memory
	 * Line Size, that'll involve a Read-Modify-Write cycle on the
	 * Memory Controller which is never good.
	 */

	/* We want the Packing Boundary to be based on the Cache Line
	 * Size in order to help avoid False Sharing performance
	 * issues between CPUs, etc.  We also want the Packing
	 * Boundary to incorporate the PCI-E Maximum Payload Size.  We
	 * get best performance when the Packing Boundary is a
	 * multiple of the Maximum Payload Size.
	 */
	pack_align = fl_align;
	pcie_cap = pci_find_capability(hw->pdev, PCI_CAP_ID_EXP);
	if (pcie_cap) {
		u32 mps, mps_log;
		u16 devctl;

		/* The PCIe Device Control Maximum Payload Size field
		 * [bits 7:5] encodes sizes as powers of 2 starting at
		 * 128 bytes.
		 */
		pci_read_config_word(hw->pdev,
				     pcie_cap + PCI_EXP_DEVCTL,
				     &devctl);
		mps_log = ((devctl & PCI_EXP_DEVCTL_PAYLOAD) >> 5) + 7;
		mps = 1 << mps_log;
		if (mps > pack_align)
			pack_align = mps;
	}

	/* T5/T6 have a special interpretation of the "0"
	 * value for the Packing Boundary.  This corresponds to 16
	 * bytes instead of the expected 32 bytes.
	 */
	if (pack_align <= 16) {
		ingpack = INGPACKBOUNDARY_16B_X;
		fl_align = 16;
	} else if (pack_align == 32) {
		ingpack = INGPACKBOUNDARY_64B_X;
		fl_align = 64;
	} else {
		u32 pack_align_log = fls(pack_align) - 1;

		ingpack = pack_align_log - INGPACKBOUNDARY_SHIFT_X;
		fl_align = pack_align;
	}

	/* Use the smallest Ingress Padding which isn't smaller than
	 * the Memory Controller Read/Write Size.  We'll take that as
	 * being 8 bytes since we don't know of any system with a
	 * wider Memory Controller Bus Width.
	 */
	if (csio_is_t5(hw->pdev->device & CSIO_HW_CHIP_MASK))
		ingpad = INGPADBOUNDARY_32B_X;
	else
		ingpad = T6_INGPADBOUNDARY_8B_X;

	csio_set_reg_field(hw, SGE_CONTROL_A,
			   INGPADBOUNDARY_V(INGPADBOUNDARY_M) |
			   EGRSTATUSPAGESIZE_F,
			   INGPADBOUNDARY_V(ingpad) |
			   EGRSTATUSPAGESIZE_V(stat_len != 64));
	csio_set_reg_field(hw, SGE_CONTROL2_A,
			   INGPACKBOUNDARY_V(INGPACKBOUNDARY_M),
			   INGPACKBOUNDARY_V(ingpack));

	/* FL BUFFER SIZE#0 is Page size i,e already aligned to cache line */
	csio_wr_reg32(hw, PAGE_SIZE, SGE_FL_BUFFER_SIZE0_A);

	/*
	 * If using hard params, the following will get set correctly
	 * in csio_wr_set_sge().
	 */
	if (hw->flags & CSIO_HWF_USING_SOFT_PARAMS) {
		csio_wr_reg32(hw,
			(csio_rd_reg32(hw, SGE_FL_BUFFER_SIZE2_A) +
			fl_align - 1) & ~(fl_align - 1),
			SGE_FL_BUFFER_SIZE2_A);
		csio_wr_reg32(hw,
			(csio_rd_reg32(hw, SGE_FL_BUFFER_SIZE3_A) +
			fl_align - 1) & ~(fl_align - 1),
			SGE_FL_BUFFER_SIZE3_A);
	}

	sge->csio_fl_align = fl_align;

	csio_wr_reg32(hw, HPZ0_V(PAGE_SHIFT - 12), ULP_RX_TDDP_PSZ_A);

	/* default value of rx_dma_offset of the NIC driver */
	csio_set_reg_field(hw, SGE_CONTROL_A,
			   PKTSHIFT_V(PKTSHIFT_M),
			   PKTSHIFT_V(CSIO_SGE_RX_DMA_OFFSET));

	csio_hw_tp_wr_bits_indirect(hw, TP_INGRESS_CONFIG_A,
				    CSUM_HAS_PSEUDO_HDR_F, 0);
}

static void
csio_init_intr_coalesce_parms(struct csio_hw *hw)
{
	struct csio_wrm *wrm = csio_hw_to_wrm(hw);
	struct csio_sge *sge = &wrm->sge;

	csio_sge_thresh_reg = csio_closest_thresh(sge, csio_intr_coalesce_cnt);
	if (csio_intr_coalesce_cnt) {
		csio_sge_thresh_reg = 0;
		csio_sge_timer_reg = X_TIMERREG_RESTART_COUNTER;
		return;
	}

	csio_sge_timer_reg = csio_closest_timer(sge, csio_intr_coalesce_time);
}

/*
 * csio_wr_get_sge - Get SGE register values.
 * @hw: HW module.
 *
 * Used by non-master functions and by master-functions relying on config file.
 */
static void
csio_wr_get_sge(struct csio_hw *hw)
{
	struct csio_wrm *wrm = csio_hw_to_wrm(hw);
	struct csio_sge *sge = &wrm->sge;
	uint32_t ingpad;
	int i;
	u32 timer_value_0_and_1, timer_value_2_and_3, timer_value_4_and_5;
	u32 ingress_rx_threshold;

	sge->sge_control = csio_rd_reg32(hw, SGE_CONTROL_A);

	ingpad = INGPADBOUNDARY_G(sge->sge_control);

	switch (ingpad) {
	case X_INGPCIEBOUNDARY_32B:
		sge->csio_fl_align = 32; break;
	case X_INGPCIEBOUNDARY_64B:
		sge->csio_fl_align = 64; break;
	case X_INGPCIEBOUNDARY_128B:
		sge->csio_fl_align = 128; break;
	case X_INGPCIEBOUNDARY_256B:
		sge->csio_fl_align = 256; break;
	case X_INGPCIEBOUNDARY_512B:
		sge->csio_fl_align = 512; break;
	case X_INGPCIEBOUNDARY_1024B:
		sge->csio_fl_align = 1024; break;
	case X_INGPCIEBOUNDARY_2048B:
		sge->csio_fl_align = 2048; break;
	case X_INGPCIEBOUNDARY_4096B:
		sge->csio_fl_align = 4096; break;
	}

	for (i = 0; i < CSIO_SGE_FL_SIZE_REGS; i++)
		csio_get_flbuf_size(hw, sge, i);

	timer_value_0_and_1 = csio_rd_reg32(hw, SGE_TIMER_VALUE_0_AND_1_A);
	timer_value_2_and_3 = csio_rd_reg32(hw, SGE_TIMER_VALUE_2_AND_3_A);
	timer_value_4_and_5 = csio_rd_reg32(hw, SGE_TIMER_VALUE_4_AND_5_A);

	sge->timer_val[0] = (uint16_t)csio_core_ticks_to_us(hw,
					TIMERVALUE0_G(timer_value_0_and_1));
	sge->timer_val[1] = (uint16_t)csio_core_ticks_to_us(hw,
					TIMERVALUE1_G(timer_value_0_and_1));
	sge->timer_val[2] = (uint16_t)csio_core_ticks_to_us(hw,
					TIMERVALUE2_G(timer_value_2_and_3));
	sge->timer_val[3] = (uint16_t)csio_core_ticks_to_us(hw,
					TIMERVALUE3_G(timer_value_2_and_3));
	sge->timer_val[4] = (uint16_t)csio_core_ticks_to_us(hw,
					TIMERVALUE4_G(timer_value_4_and_5));
	sge->timer_val[5] = (uint16_t)csio_core_ticks_to_us(hw,
					TIMERVALUE5_G(timer_value_4_and_5));

	ingress_rx_threshold = csio_rd_reg32(hw, SGE_INGRESS_RX_THRESHOLD_A);
	sge->counter_val[0] = THRESHOLD_0_G(ingress_rx_threshold);
	sge->counter_val[1] = THRESHOLD_1_G(ingress_rx_threshold);
	sge->counter_val[2] = THRESHOLD_2_G(ingress_rx_threshold);
	sge->counter_val[3] = THRESHOLD_3_G(ingress_rx_threshold);

	csio_init_intr_coalesce_parms(hw);
}

/*
 * csio_wr_set_sge - Initialize SGE registers
 * @hw: HW module.
 *
 * Used by Master function to initialize SGE registers in the absence
 * of a config file.
 */
static void
csio_wr_set_sge(struct csio_hw *hw)
{
	struct csio_wrm *wrm = csio_hw_to_wrm(hw);
	struct csio_sge *sge = &wrm->sge;
	int i;

	/*
	 * Set up our basic SGE mode to deliver CPL messages to our Ingress
	 * Queue and Packet Date to the Free List.
	 */
	csio_set_reg_field(hw, SGE_CONTROL_A, RXPKTCPLMODE_F, RXPKTCPLMODE_F);

	sge->sge_control = csio_rd_reg32(hw, SGE_CONTROL_A);

	/* sge->csio_fl_align is set up by csio_wr_fixup_host_params(). */

	/*
	 * Set up to drop DOORBELL writes when the DOORBELL FIFO overflows
	 * and generate an interrupt when this occurs so we can recover.
	 */
	csio_set_reg_field(hw, SGE_DBFIFO_STATUS_A,
			   LP_INT_THRESH_T5_V(LP_INT_THRESH_T5_M),
			   LP_INT_THRESH_T5_V(CSIO_SGE_DBFIFO_INT_THRESH));
	csio_set_reg_field(hw, SGE_DBFIFO_STATUS2_A,
			   HP_INT_THRESH_T5_V(LP_INT_THRESH_T5_M),
			   HP_INT_THRESH_T5_V(CSIO_SGE_DBFIFO_INT_THRESH));

	csio_set_reg_field(hw, SGE_DOORBELL_CONTROL_A, ENABLE_DROP_F,
			   ENABLE_DROP_F);

	/* SGE_FL_BUFFER_SIZE0 is set up by csio_wr_fixup_host_params(). */

	CSIO_SET_FLBUF_SIZE(hw, 1, CSIO_SGE_FLBUF_SIZE1);
	csio_wr_reg32(hw, (CSIO_SGE_FLBUF_SIZE2 + sge->csio_fl_align - 1)
		      & ~(sge->csio_fl_align - 1), SGE_FL_BUFFER_SIZE2_A);
	csio_wr_reg32(hw, (CSIO_SGE_FLBUF_SIZE3 + sge->csio_fl_align - 1)
		      & ~(sge->csio_fl_align - 1), SGE_FL_BUFFER_SIZE3_A);
	CSIO_SET_FLBUF_SIZE(hw, 4, CSIO_SGE_FLBUF_SIZE4);
	CSIO_SET_FLBUF_SIZE(hw, 5, CSIO_SGE_FLBUF_SIZE5);
	CSIO_SET_FLBUF_SIZE(hw, 6, CSIO_SGE_FLBUF_SIZE6);
	CSIO_SET_FLBUF_SIZE(hw, 7, CSIO_SGE_FLBUF_SIZE7);
	CSIO_SET_FLBUF_SIZE(hw, 8, CSIO_SGE_FLBUF_SIZE8);

	for (i = 0; i < CSIO_SGE_FL_SIZE_REGS; i++)
		csio_get_flbuf_size(hw, sge, i);

	/* Initialize interrupt coalescing attributes */
	sge->timer_val[0] = CSIO_SGE_TIMER_VAL_0;
	sge->timer_val[1] = CSIO_SGE_TIMER_VAL_1;
	sge->timer_val[2] = CSIO_SGE_TIMER_VAL_2;
	sge->timer_val[3] = CSIO_SGE_TIMER_VAL_3;
	sge->timer_val[4] = CSIO_SGE_TIMER_VAL_4;
	sge->timer_val[5] = CSIO_SGE_TIMER_VAL_5;

	sge->counter_val[0] = CSIO_SGE_INT_CNT_VAL_0;
	sge->counter_val[1] = CSIO_SGE_INT_CNT_VAL_1;
	sge->counter_val[2] = CSIO_SGE_INT_CNT_VAL_2;
	sge->counter_val[3] = CSIO_SGE_INT_CNT_VAL_3;

	csio_wr_reg32(hw, THRESHOLD_0_V(sge->counter_val[0]) |
		      THRESHOLD_1_V(sge->counter_val[1]) |
		      THRESHOLD_2_V(sge->counter_val[2]) |
		      THRESHOLD_3_V(sge->counter_val[3]),
		      SGE_INGRESS_RX_THRESHOLD_A);

	csio_wr_reg32(hw,
		   TIMERVALUE0_V(csio_us_to_core_ticks(hw, sge->timer_val[0])) |
		   TIMERVALUE1_V(csio_us_to_core_ticks(hw, sge->timer_val[1])),
		   SGE_TIMER_VALUE_0_AND_1_A);

	csio_wr_reg32(hw,
		   TIMERVALUE2_V(csio_us_to_core_ticks(hw, sge->timer_val[2])) |
		   TIMERVALUE3_V(csio_us_to_core_ticks(hw, sge->timer_val[3])),
		   SGE_TIMER_VALUE_2_AND_3_A);

	csio_wr_reg32(hw,
		   TIMERVALUE4_V(csio_us_to_core_ticks(hw, sge->timer_val[4])) |
		   TIMERVALUE5_V(csio_us_to_core_ticks(hw, sge->timer_val[5])),
		   SGE_TIMER_VALUE_4_AND_5_A);

	csio_init_intr_coalesce_parms(hw);
}

void
csio_wr_sge_init(struct csio_hw *hw)
{
	/*
	 * If we are master and chip is not initialized:
	 *    - If we plan to use the config file, we need to fixup some
	 *      host specific registers, and read the rest of the SGE
	 *      configuration.
	 *    - If we dont plan to use the config file, we need to initialize
	 *      SGE entirely, including fixing the host specific registers.
	 * If we are master and chip is initialized, just read and work off of
	 *	the already initialized SGE values.
	 * If we arent the master, we are only allowed to read and work off of
	 *      the already initialized SGE values.
	 *
	 * Therefore, before calling this function, we assume that the master-
	 * ship of the card, state and whether to use config file or not, have
	 * already been decided.
	 */
	if (csio_is_hw_master(hw)) {
		if (hw->fw_state != CSIO_DEV_STATE_INIT)
			csio_wr_fixup_host_params(hw);

		if (hw->flags & CSIO_HWF_USING_SOFT_PARAMS)
			csio_wr_get_sge(hw);
		else
			csio_wr_set_sge(hw);
	} else
		csio_wr_get_sge(hw);
}

/*
 * csio_wrm_init - Initialize Work request module.
 * @wrm: WR module
 * @hw: HW pointer
 *
 * Allocates memory for an array of queue pointers starting at q_arr.
 */
int
csio_wrm_init(struct csio_wrm *wrm, struct csio_hw *hw)
{
	int i;

	if (!wrm->num_q) {
		csio_err(hw, "Num queues is not set\n");
		return -EINVAL;
	}

	wrm->q_arr = kcalloc(wrm->num_q, sizeof(struct csio_q *), GFP_KERNEL);
	if (!wrm->q_arr)
		goto err;

	for (i = 0; i < wrm->num_q; i++) {
		wrm->q_arr[i] = kzalloc(sizeof(struct csio_q), GFP_KERNEL);
		if (!wrm->q_arr[i]) {
			while (--i >= 0)
				kfree(wrm->q_arr[i]);
			goto err_free_arr;
		}
	}
	wrm->free_qidx	= 0;

	return 0;

err_free_arr:
	kfree(wrm->q_arr);
err:
	return -ENOMEM;
}

/*
 * csio_wrm_exit - Initialize Work request module.
 * @wrm: WR module
 * @hw: HW module
 *
 * Uninitialize WR module. Free q_arr and pointers in it.
 * We have the additional job of freeing the DMA memory associated
 * with the queues.
 */
void
csio_wrm_exit(struct csio_wrm *wrm, struct csio_hw *hw)
{
	int i;
	uint32_t j;
	struct csio_q *q;
	struct csio_dma_buf *buf;

	for (i = 0; i < wrm->num_q; i++) {
		q = wrm->q_arr[i];

		if (wrm->free_qidx && (i < wrm->free_qidx)) {
			if (q->type == CSIO_FREELIST) {
				if (!q->un.fl.bufs)
					continue;
				for (j = 0; j < q->credits; j++) {
					buf = &q->un.fl.bufs[j];
					if (!buf->vaddr)
						continue;
					dma_free_coherent(&hw->pdev->dev,
							buf->len, buf->vaddr,
							buf->paddr);
				}
				kfree(q->un.fl.bufs);
			}
			dma_free_coherent(&hw->pdev->dev, q->size,
					q->vstart, q->pstart);
		}
		kfree(q);
	}

	hw->flags &= ~CSIO_HWF_Q_MEM_ALLOCED;

	kfree(wrm->q_arr);
}
