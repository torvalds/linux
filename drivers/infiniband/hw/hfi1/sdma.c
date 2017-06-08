/*
 * Copyright(c) 2015, 2016 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/spinlock.h>
#include <linux/seqlock.h>
#include <linux/netdevice.h>
#include <linux/moduleparam.h>
#include <linux/bitops.h>
#include <linux/timer.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>

#include "hfi.h"
#include "common.h"
#include "qp.h"
#include "sdma.h"
#include "iowait.h"
#include "trace.h"

/* must be a power of 2 >= 64 <= 32768 */
#define SDMA_DESCQ_CNT 2048
#define SDMA_DESC_INTR 64
#define INVALID_TAIL 0xffff

static uint sdma_descq_cnt = SDMA_DESCQ_CNT;
module_param(sdma_descq_cnt, uint, S_IRUGO);
MODULE_PARM_DESC(sdma_descq_cnt, "Number of SDMA descq entries");

static uint sdma_idle_cnt = 250;
module_param(sdma_idle_cnt, uint, S_IRUGO);
MODULE_PARM_DESC(sdma_idle_cnt, "sdma interrupt idle delay (ns,default 250)");

uint mod_num_sdma;
module_param_named(num_sdma, mod_num_sdma, uint, S_IRUGO);
MODULE_PARM_DESC(num_sdma, "Set max number SDMA engines to use");

static uint sdma_desct_intr = SDMA_DESC_INTR;
module_param_named(desct_intr, sdma_desct_intr, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(desct_intr, "Number of SDMA descriptor before interrupt");

#define SDMA_WAIT_BATCH_SIZE 20
/* max wait time for a SDMA engine to indicate it has halted */
#define SDMA_ERR_HALT_TIMEOUT 10 /* ms */
/* all SDMA engine errors that cause a halt */

#define SD(name) SEND_DMA_##name
#define ALL_SDMA_ENG_HALT_ERRS \
	(SD(ENG_ERR_STATUS_SDMA_WRONG_DW_ERR_SMASK) \
	| SD(ENG_ERR_STATUS_SDMA_GEN_MISMATCH_ERR_SMASK) \
	| SD(ENG_ERR_STATUS_SDMA_TOO_LONG_ERR_SMASK) \
	| SD(ENG_ERR_STATUS_SDMA_TAIL_OUT_OF_BOUNDS_ERR_SMASK) \
	| SD(ENG_ERR_STATUS_SDMA_FIRST_DESC_ERR_SMASK) \
	| SD(ENG_ERR_STATUS_SDMA_MEM_READ_ERR_SMASK) \
	| SD(ENG_ERR_STATUS_SDMA_HALT_ERR_SMASK) \
	| SD(ENG_ERR_STATUS_SDMA_LENGTH_MISMATCH_ERR_SMASK) \
	| SD(ENG_ERR_STATUS_SDMA_PACKET_DESC_OVERFLOW_ERR_SMASK) \
	| SD(ENG_ERR_STATUS_SDMA_HEADER_SELECT_ERR_SMASK) \
	| SD(ENG_ERR_STATUS_SDMA_HEADER_ADDRESS_ERR_SMASK) \
	| SD(ENG_ERR_STATUS_SDMA_HEADER_LENGTH_ERR_SMASK) \
	| SD(ENG_ERR_STATUS_SDMA_TIMEOUT_ERR_SMASK) \
	| SD(ENG_ERR_STATUS_SDMA_DESC_TABLE_UNC_ERR_SMASK) \
	| SD(ENG_ERR_STATUS_SDMA_ASSEMBLY_UNC_ERR_SMASK) \
	| SD(ENG_ERR_STATUS_SDMA_PACKET_TRACKING_UNC_ERR_SMASK) \
	| SD(ENG_ERR_STATUS_SDMA_HEADER_STORAGE_UNC_ERR_SMASK) \
	| SD(ENG_ERR_STATUS_SDMA_HEADER_REQUEST_FIFO_UNC_ERR_SMASK))

/* sdma_sendctrl operations */
#define SDMA_SENDCTRL_OP_ENABLE    BIT(0)
#define SDMA_SENDCTRL_OP_INTENABLE BIT(1)
#define SDMA_SENDCTRL_OP_HALT      BIT(2)
#define SDMA_SENDCTRL_OP_CLEANUP   BIT(3)

/* handle long defines */
#define SDMA_EGRESS_PACKET_OCCUPANCY_SMASK \
SEND_EGRESS_SEND_DMA_STATUS_SDMA_EGRESS_PACKET_OCCUPANCY_SMASK
#define SDMA_EGRESS_PACKET_OCCUPANCY_SHIFT \
SEND_EGRESS_SEND_DMA_STATUS_SDMA_EGRESS_PACKET_OCCUPANCY_SHIFT

static const char * const sdma_state_names[] = {
	[sdma_state_s00_hw_down]                = "s00_HwDown",
	[sdma_state_s10_hw_start_up_halt_wait]  = "s10_HwStartUpHaltWait",
	[sdma_state_s15_hw_start_up_clean_wait] = "s15_HwStartUpCleanWait",
	[sdma_state_s20_idle]                   = "s20_Idle",
	[sdma_state_s30_sw_clean_up_wait]       = "s30_SwCleanUpWait",
	[sdma_state_s40_hw_clean_up_wait]       = "s40_HwCleanUpWait",
	[sdma_state_s50_hw_halt_wait]           = "s50_HwHaltWait",
	[sdma_state_s60_idle_halt_wait]         = "s60_IdleHaltWait",
	[sdma_state_s80_hw_freeze]		= "s80_HwFreeze",
	[sdma_state_s82_freeze_sw_clean]	= "s82_FreezeSwClean",
	[sdma_state_s99_running]                = "s99_Running",
};

#ifdef CONFIG_SDMA_VERBOSITY
static const char * const sdma_event_names[] = {
	[sdma_event_e00_go_hw_down]   = "e00_GoHwDown",
	[sdma_event_e10_go_hw_start]  = "e10_GoHwStart",
	[sdma_event_e15_hw_halt_done] = "e15_HwHaltDone",
	[sdma_event_e25_hw_clean_up_done] = "e25_HwCleanUpDone",
	[sdma_event_e30_go_running]   = "e30_GoRunning",
	[sdma_event_e40_sw_cleaned]   = "e40_SwCleaned",
	[sdma_event_e50_hw_cleaned]   = "e50_HwCleaned",
	[sdma_event_e60_hw_halted]    = "e60_HwHalted",
	[sdma_event_e70_go_idle]      = "e70_GoIdle",
	[sdma_event_e80_hw_freeze]    = "e80_HwFreeze",
	[sdma_event_e81_hw_frozen]    = "e81_HwFrozen",
	[sdma_event_e82_hw_unfreeze]  = "e82_HwUnfreeze",
	[sdma_event_e85_link_down]    = "e85_LinkDown",
	[sdma_event_e90_sw_halted]    = "e90_SwHalted",
};
#endif

static const struct sdma_set_state_action sdma_action_table[] = {
	[sdma_state_s00_hw_down] = {
		.go_s99_running_tofalse = 1,
		.op_enable = 0,
		.op_intenable = 0,
		.op_halt = 0,
		.op_cleanup = 0,
	},
	[sdma_state_s10_hw_start_up_halt_wait] = {
		.op_enable = 0,
		.op_intenable = 0,
		.op_halt = 1,
		.op_cleanup = 0,
	},
	[sdma_state_s15_hw_start_up_clean_wait] = {
		.op_enable = 0,
		.op_intenable = 1,
		.op_halt = 0,
		.op_cleanup = 1,
	},
	[sdma_state_s20_idle] = {
		.op_enable = 0,
		.op_intenable = 1,
		.op_halt = 0,
		.op_cleanup = 0,
	},
	[sdma_state_s30_sw_clean_up_wait] = {
		.op_enable = 0,
		.op_intenable = 0,
		.op_halt = 0,
		.op_cleanup = 0,
	},
	[sdma_state_s40_hw_clean_up_wait] = {
		.op_enable = 0,
		.op_intenable = 0,
		.op_halt = 0,
		.op_cleanup = 1,
	},
	[sdma_state_s50_hw_halt_wait] = {
		.op_enable = 0,
		.op_intenable = 0,
		.op_halt = 0,
		.op_cleanup = 0,
	},
	[sdma_state_s60_idle_halt_wait] = {
		.go_s99_running_tofalse = 1,
		.op_enable = 0,
		.op_intenable = 0,
		.op_halt = 1,
		.op_cleanup = 0,
	},
	[sdma_state_s80_hw_freeze] = {
		.op_enable = 0,
		.op_intenable = 0,
		.op_halt = 0,
		.op_cleanup = 0,
	},
	[sdma_state_s82_freeze_sw_clean] = {
		.op_enable = 0,
		.op_intenable = 0,
		.op_halt = 0,
		.op_cleanup = 0,
	},
	[sdma_state_s99_running] = {
		.op_enable = 1,
		.op_intenable = 1,
		.op_halt = 0,
		.op_cleanup = 0,
		.go_s99_running_totrue = 1,
	},
};

#define SDMA_TAIL_UPDATE_THRESH 0x1F

/* declare all statics here rather than keep sorting */
static void sdma_complete(struct kref *);
static void sdma_finalput(struct sdma_state *);
static void sdma_get(struct sdma_state *);
static void sdma_hw_clean_up_task(unsigned long);
static void sdma_put(struct sdma_state *);
static void sdma_set_state(struct sdma_engine *, enum sdma_states);
static void sdma_start_hw_clean_up(struct sdma_engine *);
static void sdma_sw_clean_up_task(unsigned long);
static void sdma_sendctrl(struct sdma_engine *, unsigned);
static void init_sdma_regs(struct sdma_engine *, u32, uint);
static void sdma_process_event(
	struct sdma_engine *sde,
	enum sdma_events event);
static void __sdma_process_event(
	struct sdma_engine *sde,
	enum sdma_events event);
static void dump_sdma_state(struct sdma_engine *sde);
static void sdma_make_progress(struct sdma_engine *sde, u64 status);
static void sdma_desc_avail(struct sdma_engine *sde, unsigned avail);
static void sdma_flush_descq(struct sdma_engine *sde);

/**
 * sdma_state_name() - return state string from enum
 * @state: state
 */
static const char *sdma_state_name(enum sdma_states state)
{
	return sdma_state_names[state];
}

static void sdma_get(struct sdma_state *ss)
{
	kref_get(&ss->kref);
}

static void sdma_complete(struct kref *kref)
{
	struct sdma_state *ss =
		container_of(kref, struct sdma_state, kref);

	complete(&ss->comp);
}

static void sdma_put(struct sdma_state *ss)
{
	kref_put(&ss->kref, sdma_complete);
}

static void sdma_finalput(struct sdma_state *ss)
{
	sdma_put(ss);
	wait_for_completion(&ss->comp);
}

static inline void write_sde_csr(
	struct sdma_engine *sde,
	u32 offset0,
	u64 value)
{
	write_kctxt_csr(sde->dd, sde->this_idx, offset0, value);
}

static inline u64 read_sde_csr(
	struct sdma_engine *sde,
	u32 offset0)
{
	return read_kctxt_csr(sde->dd, sde->this_idx, offset0);
}

/*
 * sdma_wait_for_packet_egress() - wait for the VL FIFO occupancy for
 * sdma engine 'sde' to drop to 0.
 */
static void sdma_wait_for_packet_egress(struct sdma_engine *sde,
					int pause)
{
	u64 off = 8 * sde->this_idx;
	struct hfi1_devdata *dd = sde->dd;
	int lcnt = 0;
	u64 reg_prev;
	u64 reg = 0;

	while (1) {
		reg_prev = reg;
		reg = read_csr(dd, off + SEND_EGRESS_SEND_DMA_STATUS);

		reg &= SDMA_EGRESS_PACKET_OCCUPANCY_SMASK;
		reg >>= SDMA_EGRESS_PACKET_OCCUPANCY_SHIFT;
		if (reg == 0)
			break;
		/* counter is reest if accupancy count changes */
		if (reg != reg_prev)
			lcnt = 0;
		if (lcnt++ > 500) {
			/* timed out - bounce the link */
			dd_dev_err(dd, "%s: engine %u timeout waiting for packets to egress, remaining count %u, bouncing link\n",
				   __func__, sde->this_idx, (u32)reg);
			queue_work(dd->pport->hfi1_wq,
				   &dd->pport->link_bounce_work);
			break;
		}
		udelay(1);
	}
}

/*
 * sdma_wait() - wait for packet egress to complete for all SDMA engines,
 * and pause for credit return.
 */
void sdma_wait(struct hfi1_devdata *dd)
{
	int i;

	for (i = 0; i < dd->num_sdma; i++) {
		struct sdma_engine *sde = &dd->per_sdma[i];

		sdma_wait_for_packet_egress(sde, 0);
	}
}

static inline void sdma_set_desc_cnt(struct sdma_engine *sde, unsigned cnt)
{
	u64 reg;

	if (!(sde->dd->flags & HFI1_HAS_SDMA_TIMEOUT))
		return;
	reg = cnt;
	reg &= SD(DESC_CNT_CNT_MASK);
	reg <<= SD(DESC_CNT_CNT_SHIFT);
	write_sde_csr(sde, SD(DESC_CNT), reg);
}

static inline void complete_tx(struct sdma_engine *sde,
			       struct sdma_txreq *tx,
			       int res)
{
	/* protect against complete modifying */
	struct iowait *wait = tx->wait;
	callback_t complete = tx->complete;

#ifdef CONFIG_HFI1_DEBUG_SDMA_ORDER
	trace_hfi1_sdma_out_sn(sde, tx->sn);
	if (WARN_ON_ONCE(sde->head_sn != tx->sn))
		dd_dev_err(sde->dd, "expected %llu got %llu\n",
			   sde->head_sn, tx->sn);
	sde->head_sn++;
#endif
	__sdma_txclean(sde->dd, tx);
	if (complete)
		(*complete)(tx, res);
	if (wait && iowait_sdma_dec(wait))
		iowait_drain_wakeup(wait);
}

/*
 * Complete all the sdma requests with a SDMA_TXREQ_S_ABORTED status
 *
 * Depending on timing there can be txreqs in two places:
 * - in the descq ring
 * - in the flush list
 *
 * To avoid ordering issues the descq ring needs to be flushed
 * first followed by the flush list.
 *
 * This routine is called from two places
 * - From a work queue item
 * - Directly from the state machine just before setting the
 *   state to running
 *
 * Must be called with head_lock held
 *
 */
static void sdma_flush(struct sdma_engine *sde)
{
	struct sdma_txreq *txp, *txp_next;
	LIST_HEAD(flushlist);
	unsigned long flags;

	/* flush from head to tail */
	sdma_flush_descq(sde);
	spin_lock_irqsave(&sde->flushlist_lock, flags);
	/* copy flush list */
	list_for_each_entry_safe(txp, txp_next, &sde->flushlist, list) {
		list_del_init(&txp->list);
		list_add_tail(&txp->list, &flushlist);
	}
	spin_unlock_irqrestore(&sde->flushlist_lock, flags);
	/* flush from flush list */
	list_for_each_entry_safe(txp, txp_next, &flushlist, list)
		complete_tx(sde, txp, SDMA_TXREQ_S_ABORTED);
}

/*
 * Fields a work request for flushing the descq ring
 * and the flush list
 *
 * If the engine has been brought to running during
 * the scheduling delay, the flush is ignored, assuming
 * that the process of bringing the engine to running
 * would have done this flush prior to going to running.
 *
 */
static void sdma_field_flush(struct work_struct *work)
{
	unsigned long flags;
	struct sdma_engine *sde =
		container_of(work, struct sdma_engine, flush_worker);

	write_seqlock_irqsave(&sde->head_lock, flags);
	if (!__sdma_running(sde))
		sdma_flush(sde);
	write_sequnlock_irqrestore(&sde->head_lock, flags);
}

static void sdma_err_halt_wait(struct work_struct *work)
{
	struct sdma_engine *sde = container_of(work, struct sdma_engine,
						err_halt_worker);
	u64 statuscsr;
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(SDMA_ERR_HALT_TIMEOUT);
	while (1) {
		statuscsr = read_sde_csr(sde, SD(STATUS));
		statuscsr &= SD(STATUS_ENG_HALTED_SMASK);
		if (statuscsr)
			break;
		if (time_after(jiffies, timeout)) {
			dd_dev_err(sde->dd,
				   "SDMA engine %d - timeout waiting for engine to halt\n",
				   sde->this_idx);
			/*
			 * Continue anyway.  This could happen if there was
			 * an uncorrectable error in the wrong spot.
			 */
			break;
		}
		usleep_range(80, 120);
	}

	sdma_process_event(sde, sdma_event_e15_hw_halt_done);
}

static void sdma_err_progress_check_schedule(struct sdma_engine *sde)
{
	if (!is_bx(sde->dd) && HFI1_CAP_IS_KSET(SDMA_AHG)) {
		unsigned index;
		struct hfi1_devdata *dd = sde->dd;

		for (index = 0; index < dd->num_sdma; index++) {
			struct sdma_engine *curr_sdma = &dd->per_sdma[index];

			if (curr_sdma != sde)
				curr_sdma->progress_check_head =
							curr_sdma->descq_head;
		}
		dd_dev_err(sde->dd,
			   "SDMA engine %d - check scheduled\n",
				sde->this_idx);
		mod_timer(&sde->err_progress_check_timer, jiffies + 10);
	}
}

static void sdma_err_progress_check(unsigned long data)
{
	unsigned index;
	struct sdma_engine *sde = (struct sdma_engine *)data;

	dd_dev_err(sde->dd, "SDE progress check event\n");
	for (index = 0; index < sde->dd->num_sdma; index++) {
		struct sdma_engine *curr_sde = &sde->dd->per_sdma[index];
		unsigned long flags;

		/* check progress on each engine except the current one */
		if (curr_sde == sde)
			continue;
		/*
		 * We must lock interrupts when acquiring sde->lock,
		 * to avoid a deadlock if interrupt triggers and spins on
		 * the same lock on same CPU
		 */
		spin_lock_irqsave(&curr_sde->tail_lock, flags);
		write_seqlock(&curr_sde->head_lock);

		/* skip non-running queues */
		if (curr_sde->state.current_state != sdma_state_s99_running) {
			write_sequnlock(&curr_sde->head_lock);
			spin_unlock_irqrestore(&curr_sde->tail_lock, flags);
			continue;
		}

		if ((curr_sde->descq_head != curr_sde->descq_tail) &&
		    (curr_sde->descq_head ==
				curr_sde->progress_check_head))
			__sdma_process_event(curr_sde,
					     sdma_event_e90_sw_halted);
		write_sequnlock(&curr_sde->head_lock);
		spin_unlock_irqrestore(&curr_sde->tail_lock, flags);
	}
	schedule_work(&sde->err_halt_worker);
}

static void sdma_hw_clean_up_task(unsigned long opaque)
{
	struct sdma_engine *sde = (struct sdma_engine *)opaque;
	u64 statuscsr;

	while (1) {
#ifdef CONFIG_SDMA_VERBOSITY
		dd_dev_err(sde->dd, "CONFIG SDMA(%u) %s:%d %s()\n",
			   sde->this_idx, slashstrip(__FILE__), __LINE__,
			__func__);
#endif
		statuscsr = read_sde_csr(sde, SD(STATUS));
		statuscsr &= SD(STATUS_ENG_CLEANED_UP_SMASK);
		if (statuscsr)
			break;
		udelay(10);
	}

	sdma_process_event(sde, sdma_event_e25_hw_clean_up_done);
}

static inline struct sdma_txreq *get_txhead(struct sdma_engine *sde)
{
	smp_read_barrier_depends(); /* see sdma_update_tail() */
	return sde->tx_ring[sde->tx_head & sde->sdma_mask];
}

/*
 * flush ring for recovery
 */
static void sdma_flush_descq(struct sdma_engine *sde)
{
	u16 head, tail;
	int progress = 0;
	struct sdma_txreq *txp = get_txhead(sde);

	/* The reason for some of the complexity of this code is that
	 * not all descriptors have corresponding txps.  So, we have to
	 * be able to skip over descs until we wander into the range of
	 * the next txp on the list.
	 */
	head = sde->descq_head & sde->sdma_mask;
	tail = sde->descq_tail & sde->sdma_mask;
	while (head != tail) {
		/* advance head, wrap if needed */
		head = ++sde->descq_head & sde->sdma_mask;
		/* if now past this txp's descs, do the callback */
		if (txp && txp->next_descq_idx == head) {
			/* remove from list */
			sde->tx_ring[sde->tx_head++ & sde->sdma_mask] = NULL;
			complete_tx(sde, txp, SDMA_TXREQ_S_ABORTED);
			trace_hfi1_sdma_progress(sde, head, tail, txp);
			txp = get_txhead(sde);
		}
		progress++;
	}
	if (progress)
		sdma_desc_avail(sde, sdma_descq_freecnt(sde));
}

static void sdma_sw_clean_up_task(unsigned long opaque)
{
	struct sdma_engine *sde = (struct sdma_engine *)opaque;
	unsigned long flags;

	spin_lock_irqsave(&sde->tail_lock, flags);
	write_seqlock(&sde->head_lock);

	/*
	 * At this point, the following should always be true:
	 * - We are halted, so no more descriptors are getting retired.
	 * - We are not running, so no one is submitting new work.
	 * - Only we can send the e40_sw_cleaned, so we can't start
	 *   running again until we say so.  So, the active list and
	 *   descq are ours to play with.
	 */

	/*
	 * In the error clean up sequence, software clean must be called
	 * before the hardware clean so we can use the hardware head in
	 * the progress routine.  A hardware clean or SPC unfreeze will
	 * reset the hardware head.
	 *
	 * Process all retired requests. The progress routine will use the
	 * latest physical hardware head - we are not running so speed does
	 * not matter.
	 */
	sdma_make_progress(sde, 0);

	sdma_flush(sde);

	/*
	 * Reset our notion of head and tail.
	 * Note that the HW registers have been reset via an earlier
	 * clean up.
	 */
	sde->descq_tail = 0;
	sde->descq_head = 0;
	sde->desc_avail = sdma_descq_freecnt(sde);
	*sde->head_dma = 0;

	__sdma_process_event(sde, sdma_event_e40_sw_cleaned);

	write_sequnlock(&sde->head_lock);
	spin_unlock_irqrestore(&sde->tail_lock, flags);
}

static void sdma_sw_tear_down(struct sdma_engine *sde)
{
	struct sdma_state *ss = &sde->state;

	/* Releasing this reference means the state machine has stopped. */
	sdma_put(ss);

	/* stop waiting for all unfreeze events to complete */
	atomic_set(&sde->dd->sdma_unfreeze_count, -1);
	wake_up_interruptible(&sde->dd->sdma_unfreeze_wq);
}

static void sdma_start_hw_clean_up(struct sdma_engine *sde)
{
	tasklet_hi_schedule(&sde->sdma_hw_clean_up_task);
}

static void sdma_set_state(struct sdma_engine *sde,
			   enum sdma_states next_state)
{
	struct sdma_state *ss = &sde->state;
	const struct sdma_set_state_action *action = sdma_action_table;
	unsigned op = 0;

	trace_hfi1_sdma_state(
		sde,
		sdma_state_names[ss->current_state],
		sdma_state_names[next_state]);

	/* debugging bookkeeping */
	ss->previous_state = ss->current_state;
	ss->previous_op = ss->current_op;
	ss->current_state = next_state;

	if (ss->previous_state != sdma_state_s99_running &&
	    next_state == sdma_state_s99_running)
		sdma_flush(sde);

	if (action[next_state].op_enable)
		op |= SDMA_SENDCTRL_OP_ENABLE;

	if (action[next_state].op_intenable)
		op |= SDMA_SENDCTRL_OP_INTENABLE;

	if (action[next_state].op_halt)
		op |= SDMA_SENDCTRL_OP_HALT;

	if (action[next_state].op_cleanup)
		op |= SDMA_SENDCTRL_OP_CLEANUP;

	if (action[next_state].go_s99_running_tofalse)
		ss->go_s99_running = 0;

	if (action[next_state].go_s99_running_totrue)
		ss->go_s99_running = 1;

	ss->current_op = op;
	sdma_sendctrl(sde, ss->current_op);
}

/**
 * sdma_get_descq_cnt() - called when device probed
 *
 * Return a validated descq count.
 *
 * This is currently only used in the verbs initialization to build the tx
 * list.
 *
 * This will probably be deleted in favor of a more scalable approach to
 * alloc tx's.
 *
 */
u16 sdma_get_descq_cnt(void)
{
	u16 count = sdma_descq_cnt;

	if (!count)
		return SDMA_DESCQ_CNT;
	/* count must be a power of 2 greater than 64 and less than
	 * 32768.   Otherwise return default.
	 */
	if (!is_power_of_2(count))
		return SDMA_DESCQ_CNT;
	if (count < 64 || count > 32768)
		return SDMA_DESCQ_CNT;
	return count;
}

/**
 * sdma_engine_get_vl() - return vl for a given sdma engine
 * @sde: sdma engine
 *
 * This function returns the vl mapped to a given engine, or an error if
 * the mapping can't be found. The mapping fields are protected by RCU.
 */
int sdma_engine_get_vl(struct sdma_engine *sde)
{
	struct hfi1_devdata *dd = sde->dd;
	struct sdma_vl_map *m;
	u8 vl;

	if (sde->this_idx >= TXE_NUM_SDMA_ENGINES)
		return -EINVAL;

	rcu_read_lock();
	m = rcu_dereference(dd->sdma_map);
	if (unlikely(!m)) {
		rcu_read_unlock();
		return -EINVAL;
	}
	vl = m->engine_to_vl[sde->this_idx];
	rcu_read_unlock();

	return vl;
}

/**
 * sdma_select_engine_vl() - select sdma engine
 * @dd: devdata
 * @selector: a spreading factor
 * @vl: this vl
 *
 *
 * This function returns an engine based on the selector and a vl.  The
 * mapping fields are protected by RCU.
 */
struct sdma_engine *sdma_select_engine_vl(
	struct hfi1_devdata *dd,
	u32 selector,
	u8 vl)
{
	struct sdma_vl_map *m;
	struct sdma_map_elem *e;
	struct sdma_engine *rval;

	/* NOTE This should only happen if SC->VL changed after the initial
	 *      checks on the QP/AH
	 *      Default will return engine 0 below
	 */
	if (vl >= num_vls) {
		rval = NULL;
		goto done;
	}

	rcu_read_lock();
	m = rcu_dereference(dd->sdma_map);
	if (unlikely(!m)) {
		rcu_read_unlock();
		return &dd->per_sdma[0];
	}
	e = m->map[vl & m->mask];
	rval = e->sde[selector & e->mask];
	rcu_read_unlock();

done:
	rval =  !rval ? &dd->per_sdma[0] : rval;
	trace_hfi1_sdma_engine_select(dd, selector, vl, rval->this_idx);
	return rval;
}

/**
 * sdma_select_engine_sc() - select sdma engine
 * @dd: devdata
 * @selector: a spreading factor
 * @sc5: the 5 bit sc
 *
 *
 * This function returns an engine based on the selector and an sc.
 */
struct sdma_engine *sdma_select_engine_sc(
	struct hfi1_devdata *dd,
	u32 selector,
	u8 sc5)
{
	u8 vl = sc_to_vlt(dd, sc5);

	return sdma_select_engine_vl(dd, selector, vl);
}

struct sdma_rht_map_elem {
	u32 mask;
	u8 ctr;
	struct sdma_engine *sde[0];
};

struct sdma_rht_node {
	unsigned long cpu_id;
	struct sdma_rht_map_elem *map[HFI1_MAX_VLS_SUPPORTED];
	struct rhash_head node;
};

#define NR_CPUS_HINT 192

static const struct rhashtable_params sdma_rht_params = {
	.nelem_hint = NR_CPUS_HINT,
	.head_offset = offsetof(struct sdma_rht_node, node),
	.key_offset = offsetof(struct sdma_rht_node, cpu_id),
	.key_len = FIELD_SIZEOF(struct sdma_rht_node, cpu_id),
	.max_size = NR_CPUS,
	.min_size = 8,
	.automatic_shrinking = true,
};

/*
 * sdma_select_user_engine() - select sdma engine based on user setup
 * @dd: devdata
 * @selector: a spreading factor
 * @vl: this vl
 *
 * This function returns an sdma engine for a user sdma request.
 * User defined sdma engine affinity setting is honored when applicable,
 * otherwise system default sdma engine mapping is used. To ensure correct
 * ordering, the mapping from <selector, vl> to sde must remain unchanged.
 */
struct sdma_engine *sdma_select_user_engine(struct hfi1_devdata *dd,
					    u32 selector, u8 vl)
{
	struct sdma_rht_node *rht_node;
	struct sdma_engine *sde = NULL;
	const struct cpumask *current_mask = &current->cpus_allowed;
	unsigned long cpu_id;

	/*
	 * To ensure that always the same sdma engine(s) will be
	 * selected make sure the process is pinned to this CPU only.
	 */
	if (cpumask_weight(current_mask) != 1)
		goto out;

	cpu_id = smp_processor_id();
	rcu_read_lock();
	rht_node = rhashtable_lookup_fast(dd->sdma_rht, &cpu_id,
					  sdma_rht_params);

	if (rht_node && rht_node->map[vl]) {
		struct sdma_rht_map_elem *map = rht_node->map[vl];

		sde = map->sde[selector & map->mask];
	}
	rcu_read_unlock();

	if (sde)
		return sde;

out:
	return sdma_select_engine_vl(dd, selector, vl);
}

static void sdma_populate_sde_map(struct sdma_rht_map_elem *map)
{
	int i;

	for (i = 0; i < roundup_pow_of_two(map->ctr ? : 1) - map->ctr; i++)
		map->sde[map->ctr + i] = map->sde[i];
}

static void sdma_cleanup_sde_map(struct sdma_rht_map_elem *map,
				 struct sdma_engine *sde)
{
	unsigned int i, pow;

	/* only need to check the first ctr entries for a match */
	for (i = 0; i < map->ctr; i++) {
		if (map->sde[i] == sde) {
			memmove(&map->sde[i], &map->sde[i + 1],
				(map->ctr - i - 1) * sizeof(map->sde[0]));
			map->ctr--;
			pow = roundup_pow_of_two(map->ctr ? : 1);
			map->mask = pow - 1;
			sdma_populate_sde_map(map);
			break;
		}
	}
}

/*
 * Prevents concurrent reads and writes of the sdma engine cpu_mask
 */
static DEFINE_MUTEX(process_to_sde_mutex);

ssize_t sdma_set_cpu_to_sde_map(struct sdma_engine *sde, const char *buf,
				size_t count)
{
	struct hfi1_devdata *dd = sde->dd;
	cpumask_var_t mask, new_mask;
	unsigned long cpu;
	int ret, vl, sz;

	vl = sdma_engine_get_vl(sde);
	if (unlikely(vl < 0))
		return -EINVAL;

	ret = zalloc_cpumask_var(&mask, GFP_KERNEL);
	if (!ret)
		return -ENOMEM;

	ret = zalloc_cpumask_var(&new_mask, GFP_KERNEL);
	if (!ret) {
		free_cpumask_var(mask);
		return -ENOMEM;
	}
	ret = cpulist_parse(buf, mask);
	if (ret)
		goto out_free;

	if (!cpumask_subset(mask, cpu_online_mask)) {
		dd_dev_warn(sde->dd, "Invalid CPU mask\n");
		ret = -EINVAL;
		goto out_free;
	}

	sz = sizeof(struct sdma_rht_map_elem) +
			(TXE_NUM_SDMA_ENGINES * sizeof(struct sdma_engine *));

	mutex_lock(&process_to_sde_mutex);

	for_each_cpu(cpu, mask) {
		struct sdma_rht_node *rht_node;

		/* Check if we have this already mapped */
		if (cpumask_test_cpu(cpu, &sde->cpu_mask)) {
			cpumask_set_cpu(cpu, new_mask);
			continue;
		}

		if (vl >= ARRAY_SIZE(rht_node->map)) {
			ret = -EINVAL;
			goto out;
		}

		rht_node = rhashtable_lookup_fast(dd->sdma_rht, &cpu,
						  sdma_rht_params);
		if (!rht_node) {
			rht_node = kzalloc(sizeof(*rht_node), GFP_KERNEL);
			if (!rht_node) {
				ret = -ENOMEM;
				goto out;
			}

			rht_node->map[vl] = kzalloc(sz, GFP_KERNEL);
			if (!rht_node->map[vl]) {
				kfree(rht_node);
				ret = -ENOMEM;
				goto out;
			}
			rht_node->cpu_id = cpu;
			rht_node->map[vl]->mask = 0;
			rht_node->map[vl]->ctr = 1;
			rht_node->map[vl]->sde[0] = sde;

			ret = rhashtable_insert_fast(dd->sdma_rht,
						     &rht_node->node,
						     sdma_rht_params);
			if (ret) {
				kfree(rht_node->map[vl]);
				kfree(rht_node);
				dd_dev_err(sde->dd, "Failed to set process to sde affinity for cpu %lu\n",
					   cpu);
				goto out;
			}

		} else {
			int ctr, pow;

			/* Add new user mappings */
			if (!rht_node->map[vl])
				rht_node->map[vl] = kzalloc(sz, GFP_KERNEL);

			if (!rht_node->map[vl]) {
				ret = -ENOMEM;
				goto out;
			}

			rht_node->map[vl]->ctr++;
			ctr = rht_node->map[vl]->ctr;
			rht_node->map[vl]->sde[ctr - 1] = sde;
			pow = roundup_pow_of_two(ctr);
			rht_node->map[vl]->mask = pow - 1;

			/* Populate the sde map table */
			sdma_populate_sde_map(rht_node->map[vl]);
		}
		cpumask_set_cpu(cpu, new_mask);
	}

	/* Clean up old mappings */
	for_each_cpu(cpu, cpu_online_mask) {
		struct sdma_rht_node *rht_node;

		/* Don't cleanup sdes that are set in the new mask */
		if (cpumask_test_cpu(cpu, mask))
			continue;

		rht_node = rhashtable_lookup_fast(dd->sdma_rht, &cpu,
						  sdma_rht_params);
		if (rht_node) {
			bool empty = true;
			int i;

			/* Remove mappings for old sde */
			for (i = 0; i < HFI1_MAX_VLS_SUPPORTED; i++)
				if (rht_node->map[i])
					sdma_cleanup_sde_map(rht_node->map[i],
							     sde);

			/* Free empty hash table entries */
			for (i = 0; i < HFI1_MAX_VLS_SUPPORTED; i++) {
				if (!rht_node->map[i])
					continue;

				if (rht_node->map[i]->ctr) {
					empty = false;
					break;
				}
			}

			if (empty) {
				ret = rhashtable_remove_fast(dd->sdma_rht,
							     &rht_node->node,
							     sdma_rht_params);
				WARN_ON(ret);

				for (i = 0; i < HFI1_MAX_VLS_SUPPORTED; i++)
					kfree(rht_node->map[i]);

				kfree(rht_node);
			}
		}
	}

	cpumask_copy(&sde->cpu_mask, new_mask);
out:
	mutex_unlock(&process_to_sde_mutex);
out_free:
	free_cpumask_var(mask);
	free_cpumask_var(new_mask);
	return ret ? : strnlen(buf, PAGE_SIZE);
}

ssize_t sdma_get_cpu_to_sde_map(struct sdma_engine *sde, char *buf)
{
	mutex_lock(&process_to_sde_mutex);
	if (cpumask_empty(&sde->cpu_mask))
		snprintf(buf, PAGE_SIZE, "%s\n", "empty");
	else
		cpumap_print_to_pagebuf(true, buf, &sde->cpu_mask);
	mutex_unlock(&process_to_sde_mutex);
	return strnlen(buf, PAGE_SIZE);
}

static void sdma_rht_free(void *ptr, void *arg)
{
	struct sdma_rht_node *rht_node = ptr;
	int i;

	for (i = 0; i < HFI1_MAX_VLS_SUPPORTED; i++)
		kfree(rht_node->map[i]);

	kfree(rht_node);
}

/**
 * sdma_seqfile_dump_cpu_list() - debugfs dump the cpu to sdma mappings
 * @s: seq file
 * @dd: hfi1_devdata
 * @cpuid: cpu id
 *
 * This routine dumps the process to sde mappings per cpu
 */
void sdma_seqfile_dump_cpu_list(struct seq_file *s,
				struct hfi1_devdata *dd,
				unsigned long cpuid)
{
	struct sdma_rht_node *rht_node;
	int i, j;

	rht_node = rhashtable_lookup_fast(dd->sdma_rht, &cpuid,
					  sdma_rht_params);
	if (!rht_node)
		return;

	seq_printf(s, "cpu%3lu: ", cpuid);
	for (i = 0; i < HFI1_MAX_VLS_SUPPORTED; i++) {
		if (!rht_node->map[i] || !rht_node->map[i]->ctr)
			continue;

		seq_printf(s, " vl%d: [", i);

		for (j = 0; j < rht_node->map[i]->ctr; j++) {
			if (!rht_node->map[i]->sde[j])
				continue;

			if (j > 0)
				seq_puts(s, ",");

			seq_printf(s, " sdma%2d",
				   rht_node->map[i]->sde[j]->this_idx);
		}
		seq_puts(s, " ]");
	}

	seq_puts(s, "\n");
}

/*
 * Free the indicated map struct
 */
static void sdma_map_free(struct sdma_vl_map *m)
{
	int i;

	for (i = 0; m && i < m->actual_vls; i++)
		kfree(m->map[i]);
	kfree(m);
}

/*
 * Handle RCU callback
 */
static void sdma_map_rcu_callback(struct rcu_head *list)
{
	struct sdma_vl_map *m = container_of(list, struct sdma_vl_map, list);

	sdma_map_free(m);
}

/**
 * sdma_map_init - called when # vls change
 * @dd: hfi1_devdata
 * @port: port number
 * @num_vls: number of vls
 * @vl_engines: per vl engine mapping (optional)
 *
 * This routine changes the mapping based on the number of vls.
 *
 * vl_engines is used to specify a non-uniform vl/engine loading. NULL
 * implies auto computing the loading and giving each VLs a uniform
 * distribution of engines per VL.
 *
 * The auto algorithm computes the sde_per_vl and the number of extra
 * engines.  Any extra engines are added from the last VL on down.
 *
 * rcu locking is used here to control access to the mapping fields.
 *
 * If either the num_vls or num_sdma are non-power of 2, the array sizes
 * in the struct sdma_vl_map and the struct sdma_map_elem are rounded
 * up to the next highest power of 2 and the first entry is reused
 * in a round robin fashion.
 *
 * If an error occurs the map change is not done and the mapping is
 * not changed.
 *
 */
int sdma_map_init(struct hfi1_devdata *dd, u8 port, u8 num_vls, u8 *vl_engines)
{
	int i, j;
	int extra, sde_per_vl;
	int engine = 0;
	u8 lvl_engines[OPA_MAX_VLS];
	struct sdma_vl_map *oldmap, *newmap;

	if (!(dd->flags & HFI1_HAS_SEND_DMA))
		return 0;

	if (!vl_engines) {
		/* truncate divide */
		sde_per_vl = dd->num_sdma / num_vls;
		/* extras */
		extra = dd->num_sdma % num_vls;
		vl_engines = lvl_engines;
		/* add extras from last vl down */
		for (i = num_vls - 1; i >= 0; i--, extra--)
			vl_engines[i] = sde_per_vl + (extra > 0 ? 1 : 0);
	}
	/* build new map */
	newmap = kzalloc(
		sizeof(struct sdma_vl_map) +
			roundup_pow_of_two(num_vls) *
			sizeof(struct sdma_map_elem *),
		GFP_KERNEL);
	if (!newmap)
		goto bail;
	newmap->actual_vls = num_vls;
	newmap->vls = roundup_pow_of_two(num_vls);
	newmap->mask = (1 << ilog2(newmap->vls)) - 1;
	/* initialize back-map */
	for (i = 0; i < TXE_NUM_SDMA_ENGINES; i++)
		newmap->engine_to_vl[i] = -1;
	for (i = 0; i < newmap->vls; i++) {
		/* save for wrap around */
		int first_engine = engine;

		if (i < newmap->actual_vls) {
			int sz = roundup_pow_of_two(vl_engines[i]);

			/* only allocate once */
			newmap->map[i] = kzalloc(
				sizeof(struct sdma_map_elem) +
					sz * sizeof(struct sdma_engine *),
				GFP_KERNEL);
			if (!newmap->map[i])
				goto bail;
			newmap->map[i]->mask = (1 << ilog2(sz)) - 1;
			/* assign engines */
			for (j = 0; j < sz; j++) {
				newmap->map[i]->sde[j] =
					&dd->per_sdma[engine];
				if (++engine >= first_engine + vl_engines[i])
					/* wrap back to first engine */
					engine = first_engine;
			}
			/* assign back-map */
			for (j = 0; j < vl_engines[i]; j++)
				newmap->engine_to_vl[first_engine + j] = i;
		} else {
			/* just re-use entry without allocating */
			newmap->map[i] = newmap->map[i % num_vls];
		}
		engine = first_engine + vl_engines[i];
	}
	/* newmap in hand, save old map */
	spin_lock_irq(&dd->sde_map_lock);
	oldmap = rcu_dereference_protected(dd->sdma_map,
					   lockdep_is_held(&dd->sde_map_lock));

	/* publish newmap */
	rcu_assign_pointer(dd->sdma_map, newmap);

	spin_unlock_irq(&dd->sde_map_lock);
	/* success, free any old map after grace period */
	if (oldmap)
		call_rcu(&oldmap->list, sdma_map_rcu_callback);
	return 0;
bail:
	/* free any partial allocation */
	sdma_map_free(newmap);
	return -ENOMEM;
}

/*
 * Clean up allocated memory.
 *
 * This routine is can be called regardless of the success of sdma_init()
 *
 */
static void sdma_clean(struct hfi1_devdata *dd, size_t num_engines)
{
	size_t i;
	struct sdma_engine *sde;

	if (dd->sdma_pad_dma) {
		dma_free_coherent(&dd->pcidev->dev, 4,
				  (void *)dd->sdma_pad_dma,
				  dd->sdma_pad_phys);
		dd->sdma_pad_dma = NULL;
		dd->sdma_pad_phys = 0;
	}
	if (dd->sdma_heads_dma) {
		dma_free_coherent(&dd->pcidev->dev, dd->sdma_heads_size,
				  (void *)dd->sdma_heads_dma,
				  dd->sdma_heads_phys);
		dd->sdma_heads_dma = NULL;
		dd->sdma_heads_phys = 0;
	}
	for (i = 0; dd->per_sdma && i < num_engines; ++i) {
		sde = &dd->per_sdma[i];

		sde->head_dma = NULL;
		sde->head_phys = 0;

		if (sde->descq) {
			dma_free_coherent(
				&dd->pcidev->dev,
				sde->descq_cnt * sizeof(u64[2]),
				sde->descq,
				sde->descq_phys
			);
			sde->descq = NULL;
			sde->descq_phys = 0;
		}
		kvfree(sde->tx_ring);
		sde->tx_ring = NULL;
	}
	spin_lock_irq(&dd->sde_map_lock);
	sdma_map_free(rcu_access_pointer(dd->sdma_map));
	RCU_INIT_POINTER(dd->sdma_map, NULL);
	spin_unlock_irq(&dd->sde_map_lock);
	synchronize_rcu();
	kfree(dd->per_sdma);
	dd->per_sdma = NULL;

	if (dd->sdma_rht) {
		rhashtable_free_and_destroy(dd->sdma_rht, sdma_rht_free, NULL);
		kfree(dd->sdma_rht);
		dd->sdma_rht = NULL;
	}
}

/**
 * sdma_init() - called when device probed
 * @dd: hfi1_devdata
 * @port: port number (currently only zero)
 *
 * sdma_init initializes the specified number of engines.
 *
 * The code initializes each sde, its csrs.  Interrupts
 * are not required to be enabled.
 *
 * Returns:
 * 0 - success, -errno on failure
 */
int sdma_init(struct hfi1_devdata *dd, u8 port)
{
	unsigned this_idx;
	struct sdma_engine *sde;
	struct rhashtable *tmp_sdma_rht;
	u16 descq_cnt;
	void *curr_head;
	struct hfi1_pportdata *ppd = dd->pport + port;
	u32 per_sdma_credits;
	uint idle_cnt = sdma_idle_cnt;
	size_t num_engines = dd->chip_sdma_engines;
	int ret = -ENOMEM;

	if (!HFI1_CAP_IS_KSET(SDMA)) {
		HFI1_CAP_CLEAR(SDMA_AHG);
		return 0;
	}
	if (mod_num_sdma &&
	    /* can't exceed chip support */
	    mod_num_sdma <= dd->chip_sdma_engines &&
	    /* count must be >= vls */
	    mod_num_sdma >= num_vls)
		num_engines = mod_num_sdma;

	dd_dev_info(dd, "SDMA mod_num_sdma: %u\n", mod_num_sdma);
	dd_dev_info(dd, "SDMA chip_sdma_engines: %u\n", dd->chip_sdma_engines);
	dd_dev_info(dd, "SDMA chip_sdma_mem_size: %u\n",
		    dd->chip_sdma_mem_size);

	per_sdma_credits =
		dd->chip_sdma_mem_size / (num_engines * SDMA_BLOCK_SIZE);

	/* set up freeze waitqueue */
	init_waitqueue_head(&dd->sdma_unfreeze_wq);
	atomic_set(&dd->sdma_unfreeze_count, 0);

	descq_cnt = sdma_get_descq_cnt();
	dd_dev_info(dd, "SDMA engines %zu descq_cnt %u\n",
		    num_engines, descq_cnt);

	/* alloc memory for array of send engines */
	dd->per_sdma = kcalloc(num_engines, sizeof(*dd->per_sdma), GFP_KERNEL);
	if (!dd->per_sdma)
		return ret;

	idle_cnt = ns_to_cclock(dd, idle_cnt);
	if (!sdma_desct_intr)
		sdma_desct_intr = SDMA_DESC_INTR;

	/* Allocate memory for SendDMA descriptor FIFOs */
	for (this_idx = 0; this_idx < num_engines; ++this_idx) {
		sde = &dd->per_sdma[this_idx];
		sde->dd = dd;
		sde->ppd = ppd;
		sde->this_idx = this_idx;
		sde->descq_cnt = descq_cnt;
		sde->desc_avail = sdma_descq_freecnt(sde);
		sde->sdma_shift = ilog2(descq_cnt);
		sde->sdma_mask = (1 << sde->sdma_shift) - 1;

		/* Create a mask specifically for each interrupt source */
		sde->int_mask = (u64)1 << (0 * TXE_NUM_SDMA_ENGINES +
					   this_idx);
		sde->progress_mask = (u64)1 << (1 * TXE_NUM_SDMA_ENGINES +
						this_idx);
		sde->idle_mask = (u64)1 << (2 * TXE_NUM_SDMA_ENGINES +
					    this_idx);
		/* Create a combined mask to cover all 3 interrupt sources */
		sde->imask = sde->int_mask | sde->progress_mask |
			     sde->idle_mask;

		spin_lock_init(&sde->tail_lock);
		seqlock_init(&sde->head_lock);
		spin_lock_init(&sde->senddmactrl_lock);
		spin_lock_init(&sde->flushlist_lock);
		/* insure there is always a zero bit */
		sde->ahg_bits = 0xfffffffe00000000ULL;

		sdma_set_state(sde, sdma_state_s00_hw_down);

		/* set up reference counting */
		kref_init(&sde->state.kref);
		init_completion(&sde->state.comp);

		INIT_LIST_HEAD(&sde->flushlist);
		INIT_LIST_HEAD(&sde->dmawait);

		sde->tail_csr =
			get_kctxt_csr_addr(dd, this_idx, SD(TAIL));

		if (idle_cnt)
			dd->default_desc1 =
				SDMA_DESC1_HEAD_TO_HOST_FLAG;
		else
			dd->default_desc1 =
				SDMA_DESC1_INT_REQ_FLAG;

		tasklet_init(&sde->sdma_hw_clean_up_task, sdma_hw_clean_up_task,
			     (unsigned long)sde);

		tasklet_init(&sde->sdma_sw_clean_up_task, sdma_sw_clean_up_task,
			     (unsigned long)sde);
		INIT_WORK(&sde->err_halt_worker, sdma_err_halt_wait);
		INIT_WORK(&sde->flush_worker, sdma_field_flush);

		sde->progress_check_head = 0;

		setup_timer(&sde->err_progress_check_timer,
			    sdma_err_progress_check, (unsigned long)sde);

		sde->descq = dma_zalloc_coherent(
			&dd->pcidev->dev,
			descq_cnt * sizeof(u64[2]),
			&sde->descq_phys,
			GFP_KERNEL
		);
		if (!sde->descq)
			goto bail;
		sde->tx_ring =
			kcalloc(descq_cnt, sizeof(struct sdma_txreq *),
				GFP_KERNEL);
		if (!sde->tx_ring)
			sde->tx_ring =
				vzalloc(
					sizeof(struct sdma_txreq *) *
					descq_cnt);
		if (!sde->tx_ring)
			goto bail;
	}

	dd->sdma_heads_size = L1_CACHE_BYTES * num_engines;
	/* Allocate memory for DMA of head registers to memory */
	dd->sdma_heads_dma = dma_zalloc_coherent(
		&dd->pcidev->dev,
		dd->sdma_heads_size,
		&dd->sdma_heads_phys,
		GFP_KERNEL
	);
	if (!dd->sdma_heads_dma) {
		dd_dev_err(dd, "failed to allocate SendDMA head memory\n");
		goto bail;
	}

	/* Allocate memory for pad */
	dd->sdma_pad_dma = dma_zalloc_coherent(
		&dd->pcidev->dev,
		sizeof(u32),
		&dd->sdma_pad_phys,
		GFP_KERNEL
	);
	if (!dd->sdma_pad_dma) {
		dd_dev_err(dd, "failed to allocate SendDMA pad memory\n");
		goto bail;
	}

	/* assign each engine to different cacheline and init registers */
	curr_head = (void *)dd->sdma_heads_dma;
	for (this_idx = 0; this_idx < num_engines; ++this_idx) {
		unsigned long phys_offset;

		sde = &dd->per_sdma[this_idx];

		sde->head_dma = curr_head;
		curr_head += L1_CACHE_BYTES;
		phys_offset = (unsigned long)sde->head_dma -
			      (unsigned long)dd->sdma_heads_dma;
		sde->head_phys = dd->sdma_heads_phys + phys_offset;
		init_sdma_regs(sde, per_sdma_credits, idle_cnt);
	}
	dd->flags |= HFI1_HAS_SEND_DMA;
	dd->flags |= idle_cnt ? HFI1_HAS_SDMA_TIMEOUT : 0;
	dd->num_sdma = num_engines;
	ret = sdma_map_init(dd, port, ppd->vls_operational, NULL);
	if (ret < 0)
		goto bail;

	tmp_sdma_rht = kzalloc(sizeof(*tmp_sdma_rht), GFP_KERNEL);
	if (!tmp_sdma_rht) {
		ret = -ENOMEM;
		goto bail;
	}

	ret = rhashtable_init(tmp_sdma_rht, &sdma_rht_params);
	if (ret < 0)
		goto bail;
	dd->sdma_rht = tmp_sdma_rht;

	dd_dev_info(dd, "SDMA num_sdma: %u\n", dd->num_sdma);
	return 0;

bail:
	sdma_clean(dd, num_engines);
	return ret;
}

/**
 * sdma_all_running() - called when the link goes up
 * @dd: hfi1_devdata
 *
 * This routine moves all engines to the running state.
 */
void sdma_all_running(struct hfi1_devdata *dd)
{
	struct sdma_engine *sde;
	unsigned int i;

	/* move all engines to running */
	for (i = 0; i < dd->num_sdma; ++i) {
		sde = &dd->per_sdma[i];
		sdma_process_event(sde, sdma_event_e30_go_running);
	}
}

/**
 * sdma_all_idle() - called when the link goes down
 * @dd: hfi1_devdata
 *
 * This routine moves all engines to the idle state.
 */
void sdma_all_idle(struct hfi1_devdata *dd)
{
	struct sdma_engine *sde;
	unsigned int i;

	/* idle all engines */
	for (i = 0; i < dd->num_sdma; ++i) {
		sde = &dd->per_sdma[i];
		sdma_process_event(sde, sdma_event_e70_go_idle);
	}
}

/**
 * sdma_start() - called to kick off state processing for all engines
 * @dd: hfi1_devdata
 *
 * This routine is for kicking off the state processing for all required
 * sdma engines.  Interrupts need to be working at this point.
 *
 */
void sdma_start(struct hfi1_devdata *dd)
{
	unsigned i;
	struct sdma_engine *sde;

	/* kick off the engines state processing */
	for (i = 0; i < dd->num_sdma; ++i) {
		sde = &dd->per_sdma[i];
		sdma_process_event(sde, sdma_event_e10_go_hw_start);
	}
}

/**
 * sdma_exit() - used when module is removed
 * @dd: hfi1_devdata
 */
void sdma_exit(struct hfi1_devdata *dd)
{
	unsigned this_idx;
	struct sdma_engine *sde;

	for (this_idx = 0; dd->per_sdma && this_idx < dd->num_sdma;
			++this_idx) {
		sde = &dd->per_sdma[this_idx];
		if (!list_empty(&sde->dmawait))
			dd_dev_err(dd, "sde %u: dmawait list not empty!\n",
				   sde->this_idx);
		sdma_process_event(sde, sdma_event_e00_go_hw_down);

		del_timer_sync(&sde->err_progress_check_timer);

		/*
		 * This waits for the state machine to exit so it is not
		 * necessary to kill the sdma_sw_clean_up_task to make sure
		 * it is not running.
		 */
		sdma_finalput(&sde->state);
	}
	sdma_clean(dd, dd->num_sdma);
}

/*
 * unmap the indicated descriptor
 */
static inline void sdma_unmap_desc(
	struct hfi1_devdata *dd,
	struct sdma_desc *descp)
{
	switch (sdma_mapping_type(descp)) {
	case SDMA_MAP_SINGLE:
		dma_unmap_single(
			&dd->pcidev->dev,
			sdma_mapping_addr(descp),
			sdma_mapping_len(descp),
			DMA_TO_DEVICE);
		break;
	case SDMA_MAP_PAGE:
		dma_unmap_page(
			&dd->pcidev->dev,
			sdma_mapping_addr(descp),
			sdma_mapping_len(descp),
			DMA_TO_DEVICE);
		break;
	}
}

/*
 * return the mode as indicated by the first
 * descriptor in the tx.
 */
static inline u8 ahg_mode(struct sdma_txreq *tx)
{
	return (tx->descp[0].qw[1] & SDMA_DESC1_HEADER_MODE_SMASK)
		>> SDMA_DESC1_HEADER_MODE_SHIFT;
}

/**
 * __sdma_txclean() - clean tx of mappings, descp *kmalloc's
 * @dd: hfi1_devdata for unmapping
 * @tx: tx request to clean
 *
 * This is used in the progress routine to clean the tx or
 * by the ULP to toss an in-process tx build.
 *
 * The code can be called multiple times without issue.
 *
 */
void __sdma_txclean(
	struct hfi1_devdata *dd,
	struct sdma_txreq *tx)
{
	u16 i;

	if (tx->num_desc) {
		u8 skip = 0, mode = ahg_mode(tx);

		/* unmap first */
		sdma_unmap_desc(dd, &tx->descp[0]);
		/* determine number of AHG descriptors to skip */
		if (mode > SDMA_AHG_APPLY_UPDATE1)
			skip = mode >> 1;
		for (i = 1 + skip; i < tx->num_desc; i++)
			sdma_unmap_desc(dd, &tx->descp[i]);
		tx->num_desc = 0;
	}
	kfree(tx->coalesce_buf);
	tx->coalesce_buf = NULL;
	/* kmalloc'ed descp */
	if (unlikely(tx->desc_limit > ARRAY_SIZE(tx->descs))) {
		tx->desc_limit = ARRAY_SIZE(tx->descs);
		kfree(tx->descp);
	}
}

static inline u16 sdma_gethead(struct sdma_engine *sde)
{
	struct hfi1_devdata *dd = sde->dd;
	int use_dmahead;
	u16 hwhead;

#ifdef CONFIG_SDMA_VERBOSITY
	dd_dev_err(sde->dd, "CONFIG SDMA(%u) %s:%d %s()\n",
		   sde->this_idx, slashstrip(__FILE__), __LINE__, __func__);
#endif

retry:
	use_dmahead = HFI1_CAP_IS_KSET(USE_SDMA_HEAD) && __sdma_running(sde) &&
					(dd->flags & HFI1_HAS_SDMA_TIMEOUT);
	hwhead = use_dmahead ?
		(u16)le64_to_cpu(*sde->head_dma) :
		(u16)read_sde_csr(sde, SD(HEAD));

	if (unlikely(HFI1_CAP_IS_KSET(SDMA_HEAD_CHECK))) {
		u16 cnt;
		u16 swtail;
		u16 swhead;
		int sane;

		swhead = sde->descq_head & sde->sdma_mask;
		/* this code is really bad for cache line trading */
		swtail = ACCESS_ONCE(sde->descq_tail) & sde->sdma_mask;
		cnt = sde->descq_cnt;

		if (swhead < swtail)
			/* not wrapped */
			sane = (hwhead >= swhead) & (hwhead <= swtail);
		else if (swhead > swtail)
			/* wrapped around */
			sane = ((hwhead >= swhead) && (hwhead < cnt)) ||
				(hwhead <= swtail);
		else
			/* empty */
			sane = (hwhead == swhead);

		if (unlikely(!sane)) {
			dd_dev_err(dd, "SDMA(%u) bad head (%s) hwhd=%hu swhd=%hu swtl=%hu cnt=%hu\n",
				   sde->this_idx,
				   use_dmahead ? "dma" : "kreg",
				   hwhead, swhead, swtail, cnt);
			if (use_dmahead) {
				/* try one more time, using csr */
				use_dmahead = 0;
				goto retry;
			}
			/* proceed as if no progress */
			hwhead = swhead;
		}
	}
	return hwhead;
}

/*
 * This is called when there are send DMA descriptors that might be
 * available.
 *
 * This is called with head_lock held.
 */
static void sdma_desc_avail(struct sdma_engine *sde, unsigned avail)
{
	struct iowait *wait, *nw;
	struct iowait *waits[SDMA_WAIT_BATCH_SIZE];
	unsigned i, n = 0, seq;
	struct sdma_txreq *stx;
	struct hfi1_ibdev *dev = &sde->dd->verbs_dev;

#ifdef CONFIG_SDMA_VERBOSITY
	dd_dev_err(sde->dd, "CONFIG SDMA(%u) %s:%d %s()\n", sde->this_idx,
		   slashstrip(__FILE__), __LINE__, __func__);
	dd_dev_err(sde->dd, "avail: %u\n", avail);
#endif

	do {
		seq = read_seqbegin(&dev->iowait_lock);
		if (!list_empty(&sde->dmawait)) {
			/* at least one item */
			write_seqlock(&dev->iowait_lock);
			/* Harvest waiters wanting DMA descriptors */
			list_for_each_entry_safe(
					wait,
					nw,
					&sde->dmawait,
					list) {
				u16 num_desc = 0;

				if (!wait->wakeup)
					continue;
				if (n == ARRAY_SIZE(waits))
					break;
				if (!list_empty(&wait->tx_head)) {
					stx = list_first_entry(
						&wait->tx_head,
						struct sdma_txreq,
						list);
					num_desc = stx->num_desc;
				}
				if (num_desc > avail)
					break;
				avail -= num_desc;
				list_del_init(&wait->list);
				waits[n++] = wait;
			}
			write_sequnlock(&dev->iowait_lock);
			break;
		}
	} while (read_seqretry(&dev->iowait_lock, seq));

	for (i = 0; i < n; i++)
		waits[i]->wakeup(waits[i], SDMA_AVAIL_REASON);
}

/* head_lock must be held */
static void sdma_make_progress(struct sdma_engine *sde, u64 status)
{
	struct sdma_txreq *txp = NULL;
	int progress = 0;
	u16 hwhead, swhead;
	int idle_check_done = 0;

	hwhead = sdma_gethead(sde);

	/* The reason for some of the complexity of this code is that
	 * not all descriptors have corresponding txps.  So, we have to
	 * be able to skip over descs until we wander into the range of
	 * the next txp on the list.
	 */

retry:
	txp = get_txhead(sde);
	swhead = sde->descq_head & sde->sdma_mask;
	trace_hfi1_sdma_progress(sde, hwhead, swhead, txp);
	while (swhead != hwhead) {
		/* advance head, wrap if needed */
		swhead = ++sde->descq_head & sde->sdma_mask;

		/* if now past this txp's descs, do the callback */
		if (txp && txp->next_descq_idx == swhead) {
			/* remove from list */
			sde->tx_ring[sde->tx_head++ & sde->sdma_mask] = NULL;
			complete_tx(sde, txp, SDMA_TXREQ_S_OK);
			/* see if there is another txp */
			txp = get_txhead(sde);
		}
		trace_hfi1_sdma_progress(sde, hwhead, swhead, txp);
		progress++;
	}

	/*
	 * The SDMA idle interrupt is not guaranteed to be ordered with respect
	 * to updates to the the dma_head location in host memory. The head
	 * value read might not be fully up to date. If there are pending
	 * descriptors and the SDMA idle interrupt fired then read from the
	 * CSR SDMA head instead to get the latest value from the hardware.
	 * The hardware SDMA head should be read at most once in this invocation
	 * of sdma_make_progress(..) which is ensured by idle_check_done flag
	 */
	if ((status & sde->idle_mask) && !idle_check_done) {
		u16 swtail;

		swtail = ACCESS_ONCE(sde->descq_tail) & sde->sdma_mask;
		if (swtail != hwhead) {
			hwhead = (u16)read_sde_csr(sde, SD(HEAD));
			idle_check_done = 1;
			goto retry;
		}
	}

	sde->last_status = status;
	if (progress)
		sdma_desc_avail(sde, sdma_descq_freecnt(sde));
}

/*
 * sdma_engine_interrupt() - interrupt handler for engine
 * @sde: sdma engine
 * @status: sdma interrupt reason
 *
 * Status is a mask of the 3 possible interrupts for this engine.  It will
 * contain bits _only_ for this SDMA engine.  It will contain at least one
 * bit, it may contain more.
 */
void sdma_engine_interrupt(struct sdma_engine *sde, u64 status)
{
	trace_hfi1_sdma_engine_interrupt(sde, status);
	write_seqlock(&sde->head_lock);
	sdma_set_desc_cnt(sde, sdma_desct_intr);
	if (status & sde->idle_mask)
		sde->idle_int_cnt++;
	else if (status & sde->progress_mask)
		sde->progress_int_cnt++;
	else if (status & sde->int_mask)
		sde->sdma_int_cnt++;
	sdma_make_progress(sde, status);
	write_sequnlock(&sde->head_lock);
}

/**
 * sdma_engine_error() - error handler for engine
 * @sde: sdma engine
 * @status: sdma interrupt reason
 */
void sdma_engine_error(struct sdma_engine *sde, u64 status)
{
	unsigned long flags;

#ifdef CONFIG_SDMA_VERBOSITY
	dd_dev_err(sde->dd, "CONFIG SDMA(%u) error status 0x%llx state %s\n",
		   sde->this_idx,
		   (unsigned long long)status,
		   sdma_state_names[sde->state.current_state]);
#endif
	spin_lock_irqsave(&sde->tail_lock, flags);
	write_seqlock(&sde->head_lock);
	if (status & ALL_SDMA_ENG_HALT_ERRS)
		__sdma_process_event(sde, sdma_event_e60_hw_halted);
	if (status & ~SD(ENG_ERR_STATUS_SDMA_HALT_ERR_SMASK)) {
		dd_dev_err(sde->dd,
			   "SDMA (%u) engine error: 0x%llx state %s\n",
			   sde->this_idx,
			   (unsigned long long)status,
			   sdma_state_names[sde->state.current_state]);
		dump_sdma_state(sde);
	}
	write_sequnlock(&sde->head_lock);
	spin_unlock_irqrestore(&sde->tail_lock, flags);
}

static void sdma_sendctrl(struct sdma_engine *sde, unsigned op)
{
	u64 set_senddmactrl = 0;
	u64 clr_senddmactrl = 0;
	unsigned long flags;

#ifdef CONFIG_SDMA_VERBOSITY
	dd_dev_err(sde->dd, "CONFIG SDMA(%u) senddmactrl E=%d I=%d H=%d C=%d\n",
		   sde->this_idx,
		   (op & SDMA_SENDCTRL_OP_ENABLE) ? 1 : 0,
		   (op & SDMA_SENDCTRL_OP_INTENABLE) ? 1 : 0,
		   (op & SDMA_SENDCTRL_OP_HALT) ? 1 : 0,
		   (op & SDMA_SENDCTRL_OP_CLEANUP) ? 1 : 0);
#endif

	if (op & SDMA_SENDCTRL_OP_ENABLE)
		set_senddmactrl |= SD(CTRL_SDMA_ENABLE_SMASK);
	else
		clr_senddmactrl |= SD(CTRL_SDMA_ENABLE_SMASK);

	if (op & SDMA_SENDCTRL_OP_INTENABLE)
		set_senddmactrl |= SD(CTRL_SDMA_INT_ENABLE_SMASK);
	else
		clr_senddmactrl |= SD(CTRL_SDMA_INT_ENABLE_SMASK);

	if (op & SDMA_SENDCTRL_OP_HALT)
		set_senddmactrl |= SD(CTRL_SDMA_HALT_SMASK);
	else
		clr_senddmactrl |= SD(CTRL_SDMA_HALT_SMASK);

	spin_lock_irqsave(&sde->senddmactrl_lock, flags);

	sde->p_senddmactrl |= set_senddmactrl;
	sde->p_senddmactrl &= ~clr_senddmactrl;

	if (op & SDMA_SENDCTRL_OP_CLEANUP)
		write_sde_csr(sde, SD(CTRL),
			      sde->p_senddmactrl |
			      SD(CTRL_SDMA_CLEANUP_SMASK));
	else
		write_sde_csr(sde, SD(CTRL), sde->p_senddmactrl);

	spin_unlock_irqrestore(&sde->senddmactrl_lock, flags);

#ifdef CONFIG_SDMA_VERBOSITY
	sdma_dumpstate(sde);
#endif
}

static void sdma_setlengen(struct sdma_engine *sde)
{
#ifdef CONFIG_SDMA_VERBOSITY
	dd_dev_err(sde->dd, "CONFIG SDMA(%u) %s:%d %s()\n",
		   sde->this_idx, slashstrip(__FILE__), __LINE__, __func__);
#endif

	/*
	 * Set SendDmaLenGen and clear-then-set the MSB of the generation
	 * count to enable generation checking and load the internal
	 * generation counter.
	 */
	write_sde_csr(sde, SD(LEN_GEN),
		      (sde->descq_cnt / 64) << SD(LEN_GEN_LENGTH_SHIFT));
	write_sde_csr(sde, SD(LEN_GEN),
		      ((sde->descq_cnt / 64) << SD(LEN_GEN_LENGTH_SHIFT)) |
		      (4ULL << SD(LEN_GEN_GENERATION_SHIFT)));
}

static inline void sdma_update_tail(struct sdma_engine *sde, u16 tail)
{
	/* Commit writes to memory and advance the tail on the chip */
	smp_wmb(); /* see get_txhead() */
	writeq(tail, sde->tail_csr);
}

/*
 * This is called when changing to state s10_hw_start_up_halt_wait as
 * a result of send buffer errors or send DMA descriptor errors.
 */
static void sdma_hw_start_up(struct sdma_engine *sde)
{
	u64 reg;

#ifdef CONFIG_SDMA_VERBOSITY
	dd_dev_err(sde->dd, "CONFIG SDMA(%u) %s:%d %s()\n",
		   sde->this_idx, slashstrip(__FILE__), __LINE__, __func__);
#endif

	sdma_setlengen(sde);
	sdma_update_tail(sde, 0); /* Set SendDmaTail */
	*sde->head_dma = 0;

	reg = SD(ENG_ERR_CLEAR_SDMA_HEADER_REQUEST_FIFO_UNC_ERR_MASK) <<
	      SD(ENG_ERR_CLEAR_SDMA_HEADER_REQUEST_FIFO_UNC_ERR_SHIFT);
	write_sde_csr(sde, SD(ENG_ERR_CLEAR), reg);
}

/*
 * set_sdma_integrity
 *
 * Set the SEND_DMA_CHECK_ENABLE register for send DMA engine 'sde'.
 */
static void set_sdma_integrity(struct sdma_engine *sde)
{
	struct hfi1_devdata *dd = sde->dd;

	write_sde_csr(sde, SD(CHECK_ENABLE),
		      hfi1_pkt_base_sdma_integrity(dd));
}

static void init_sdma_regs(
	struct sdma_engine *sde,
	u32 credits,
	uint idle_cnt)
{
	u8 opval, opmask;
#ifdef CONFIG_SDMA_VERBOSITY
	struct hfi1_devdata *dd = sde->dd;

	dd_dev_err(dd, "CONFIG SDMA(%u) %s:%d %s()\n",
		   sde->this_idx, slashstrip(__FILE__), __LINE__, __func__);
#endif

	write_sde_csr(sde, SD(BASE_ADDR), sde->descq_phys);
	sdma_setlengen(sde);
	sdma_update_tail(sde, 0); /* Set SendDmaTail */
	write_sde_csr(sde, SD(RELOAD_CNT), idle_cnt);
	write_sde_csr(sde, SD(DESC_CNT), 0);
	write_sde_csr(sde, SD(HEAD_ADDR), sde->head_phys);
	write_sde_csr(sde, SD(MEMORY),
		      ((u64)credits << SD(MEMORY_SDMA_MEMORY_CNT_SHIFT)) |
		      ((u64)(credits * sde->this_idx) <<
		       SD(MEMORY_SDMA_MEMORY_INDEX_SHIFT)));
	write_sde_csr(sde, SD(ENG_ERR_MASK), ~0ull);
	set_sdma_integrity(sde);
	opmask = OPCODE_CHECK_MASK_DISABLED;
	opval = OPCODE_CHECK_VAL_DISABLED;
	write_sde_csr(sde, SD(CHECK_OPCODE),
		      (opmask << SEND_CTXT_CHECK_OPCODE_MASK_SHIFT) |
		      (opval << SEND_CTXT_CHECK_OPCODE_VALUE_SHIFT));
}

#ifdef CONFIG_SDMA_VERBOSITY

#define sdma_dumpstate_helper0(reg) do { \
		csr = read_csr(sde->dd, reg); \
		dd_dev_err(sde->dd, "%36s     0x%016llx\n", #reg, csr); \
	} while (0)

#define sdma_dumpstate_helper(reg) do { \
		csr = read_sde_csr(sde, reg); \
		dd_dev_err(sde->dd, "%36s[%02u] 0x%016llx\n", \
			#reg, sde->this_idx, csr); \
	} while (0)

#define sdma_dumpstate_helper2(reg) do { \
		csr = read_csr(sde->dd, reg + (8 * i)); \
		dd_dev_err(sde->dd, "%33s_%02u     0x%016llx\n", \
				#reg, i, csr); \
	} while (0)

void sdma_dumpstate(struct sdma_engine *sde)
{
	u64 csr;
	unsigned i;

	sdma_dumpstate_helper(SD(CTRL));
	sdma_dumpstate_helper(SD(STATUS));
	sdma_dumpstate_helper0(SD(ERR_STATUS));
	sdma_dumpstate_helper0(SD(ERR_MASK));
	sdma_dumpstate_helper(SD(ENG_ERR_STATUS));
	sdma_dumpstate_helper(SD(ENG_ERR_MASK));

	for (i = 0; i < CCE_NUM_INT_CSRS; ++i) {
		sdma_dumpstate_helper2(CCE_INT_STATUS);
		sdma_dumpstate_helper2(CCE_INT_MASK);
		sdma_dumpstate_helper2(CCE_INT_BLOCKED);
	}

	sdma_dumpstate_helper(SD(TAIL));
	sdma_dumpstate_helper(SD(HEAD));
	sdma_dumpstate_helper(SD(PRIORITY_THLD));
	sdma_dumpstate_helper(SD(IDLE_CNT));
	sdma_dumpstate_helper(SD(RELOAD_CNT));
	sdma_dumpstate_helper(SD(DESC_CNT));
	sdma_dumpstate_helper(SD(DESC_FETCHED_CNT));
	sdma_dumpstate_helper(SD(MEMORY));
	sdma_dumpstate_helper0(SD(ENGINES));
	sdma_dumpstate_helper0(SD(MEM_SIZE));
	/* sdma_dumpstate_helper(SEND_EGRESS_SEND_DMA_STATUS);  */
	sdma_dumpstate_helper(SD(BASE_ADDR));
	sdma_dumpstate_helper(SD(LEN_GEN));
	sdma_dumpstate_helper(SD(HEAD_ADDR));
	sdma_dumpstate_helper(SD(CHECK_ENABLE));
	sdma_dumpstate_helper(SD(CHECK_VL));
	sdma_dumpstate_helper(SD(CHECK_JOB_KEY));
	sdma_dumpstate_helper(SD(CHECK_PARTITION_KEY));
	sdma_dumpstate_helper(SD(CHECK_SLID));
	sdma_dumpstate_helper(SD(CHECK_OPCODE));
}
#endif

static void dump_sdma_state(struct sdma_engine *sde)
{
	struct hw_sdma_desc *descq;
	struct hw_sdma_desc *descqp;
	u64 desc[2];
	u64 addr;
	u8 gen;
	u16 len;
	u16 head, tail, cnt;

	head = sde->descq_head & sde->sdma_mask;
	tail = sde->descq_tail & sde->sdma_mask;
	cnt = sdma_descq_freecnt(sde);
	descq = sde->descq;

	dd_dev_err(sde->dd,
		   "SDMA (%u) descq_head: %u descq_tail: %u freecnt: %u FLE %d\n",
		   sde->this_idx, head, tail, cnt,
		   !list_empty(&sde->flushlist));

	/* print info for each entry in the descriptor queue */
	while (head != tail) {
		char flags[6] = { 'x', 'x', 'x', 'x', 0 };

		descqp = &sde->descq[head];
		desc[0] = le64_to_cpu(descqp->qw[0]);
		desc[1] = le64_to_cpu(descqp->qw[1]);
		flags[0] = (desc[1] & SDMA_DESC1_INT_REQ_FLAG) ? 'I' : '-';
		flags[1] = (desc[1] & SDMA_DESC1_HEAD_TO_HOST_FLAG) ?
				'H' : '-';
		flags[2] = (desc[0] & SDMA_DESC0_FIRST_DESC_FLAG) ? 'F' : '-';
		flags[3] = (desc[0] & SDMA_DESC0_LAST_DESC_FLAG) ? 'L' : '-';
		addr = (desc[0] >> SDMA_DESC0_PHY_ADDR_SHIFT)
			& SDMA_DESC0_PHY_ADDR_MASK;
		gen = (desc[1] >> SDMA_DESC1_GENERATION_SHIFT)
			& SDMA_DESC1_GENERATION_MASK;
		len = (desc[0] >> SDMA_DESC0_BYTE_COUNT_SHIFT)
			& SDMA_DESC0_BYTE_COUNT_MASK;
		dd_dev_err(sde->dd,
			   "SDMA sdmadesc[%u]: flags:%s addr:0x%016llx gen:%u len:%u bytes\n",
			   head, flags, addr, gen, len);
		dd_dev_err(sde->dd,
			   "\tdesc0:0x%016llx desc1 0x%016llx\n",
			   desc[0], desc[1]);
		if (desc[0] & SDMA_DESC0_FIRST_DESC_FLAG)
			dd_dev_err(sde->dd,
				   "\taidx: %u amode: %u alen: %u\n",
				   (u8)((desc[1] &
					 SDMA_DESC1_HEADER_INDEX_SMASK) >>
					SDMA_DESC1_HEADER_INDEX_SHIFT),
				   (u8)((desc[1] &
					 SDMA_DESC1_HEADER_MODE_SMASK) >>
					SDMA_DESC1_HEADER_MODE_SHIFT),
				   (u8)((desc[1] &
					 SDMA_DESC1_HEADER_DWS_SMASK) >>
					SDMA_DESC1_HEADER_DWS_SHIFT));
		head++;
		head &= sde->sdma_mask;
	}
}

#define SDE_FMT \
	"SDE %u CPU %d STE %s C 0x%llx S 0x%016llx E 0x%llx T(HW) 0x%llx T(SW) 0x%x H(HW) 0x%llx H(SW) 0x%x H(D) 0x%llx DM 0x%llx GL 0x%llx R 0x%llx LIS 0x%llx AHGI 0x%llx TXT %u TXH %u DT %u DH %u FLNE %d DQF %u SLC 0x%llx\n"
/**
 * sdma_seqfile_dump_sde() - debugfs dump of sde
 * @s: seq file
 * @sde: send dma engine to dump
 *
 * This routine dumps the sde to the indicated seq file.
 */
void sdma_seqfile_dump_sde(struct seq_file *s, struct sdma_engine *sde)
{
	u16 head, tail;
	struct hw_sdma_desc *descqp;
	u64 desc[2];
	u64 addr;
	u8 gen;
	u16 len;

	head = sde->descq_head & sde->sdma_mask;
	tail = ACCESS_ONCE(sde->descq_tail) & sde->sdma_mask;
	seq_printf(s, SDE_FMT, sde->this_idx,
		   sde->cpu,
		   sdma_state_name(sde->state.current_state),
		   (unsigned long long)read_sde_csr(sde, SD(CTRL)),
		   (unsigned long long)read_sde_csr(sde, SD(STATUS)),
		   (unsigned long long)read_sde_csr(sde, SD(ENG_ERR_STATUS)),
		   (unsigned long long)read_sde_csr(sde, SD(TAIL)), tail,
		   (unsigned long long)read_sde_csr(sde, SD(HEAD)), head,
		   (unsigned long long)le64_to_cpu(*sde->head_dma),
		   (unsigned long long)read_sde_csr(sde, SD(MEMORY)),
		   (unsigned long long)read_sde_csr(sde, SD(LEN_GEN)),
		   (unsigned long long)read_sde_csr(sde, SD(RELOAD_CNT)),
		   (unsigned long long)sde->last_status,
		   (unsigned long long)sde->ahg_bits,
		   sde->tx_tail,
		   sde->tx_head,
		   sde->descq_tail,
		   sde->descq_head,
		   !list_empty(&sde->flushlist),
		   sde->descq_full_count,
		   (unsigned long long)read_sde_csr(sde, SEND_DMA_CHECK_SLID));

	/* print info for each entry in the descriptor queue */
	while (head != tail) {
		char flags[6] = { 'x', 'x', 'x', 'x', 0 };

		descqp = &sde->descq[head];
		desc[0] = le64_to_cpu(descqp->qw[0]);
		desc[1] = le64_to_cpu(descqp->qw[1]);
		flags[0] = (desc[1] & SDMA_DESC1_INT_REQ_FLAG) ? 'I' : '-';
		flags[1] = (desc[1] & SDMA_DESC1_HEAD_TO_HOST_FLAG) ?
				'H' : '-';
		flags[2] = (desc[0] & SDMA_DESC0_FIRST_DESC_FLAG) ? 'F' : '-';
		flags[3] = (desc[0] & SDMA_DESC0_LAST_DESC_FLAG) ? 'L' : '-';
		addr = (desc[0] >> SDMA_DESC0_PHY_ADDR_SHIFT)
			& SDMA_DESC0_PHY_ADDR_MASK;
		gen = (desc[1] >> SDMA_DESC1_GENERATION_SHIFT)
			& SDMA_DESC1_GENERATION_MASK;
		len = (desc[0] >> SDMA_DESC0_BYTE_COUNT_SHIFT)
			& SDMA_DESC0_BYTE_COUNT_MASK;
		seq_printf(s,
			   "\tdesc[%u]: flags:%s addr:0x%016llx gen:%u len:%u bytes\n",
			   head, flags, addr, gen, len);
		if (desc[0] & SDMA_DESC0_FIRST_DESC_FLAG)
			seq_printf(s, "\t\tahgidx: %u ahgmode: %u\n",
				   (u8)((desc[1] &
					 SDMA_DESC1_HEADER_INDEX_SMASK) >>
					SDMA_DESC1_HEADER_INDEX_SHIFT),
				   (u8)((desc[1] &
					 SDMA_DESC1_HEADER_MODE_SMASK) >>
					SDMA_DESC1_HEADER_MODE_SHIFT));
		head = (head + 1) & sde->sdma_mask;
	}
}

/*
 * add the generation number into
 * the qw1 and return
 */
static inline u64 add_gen(struct sdma_engine *sde, u64 qw1)
{
	u8 generation = (sde->descq_tail >> sde->sdma_shift) & 3;

	qw1 &= ~SDMA_DESC1_GENERATION_SMASK;
	qw1 |= ((u64)generation & SDMA_DESC1_GENERATION_MASK)
			<< SDMA_DESC1_GENERATION_SHIFT;
	return qw1;
}

/*
 * This routine submits the indicated tx
 *
 * Space has already been guaranteed and
 * tail side of ring is locked.
 *
 * The hardware tail update is done
 * in the caller and that is facilitated
 * by returning the new tail.
 *
 * There is special case logic for ahg
 * to not add the generation number for
 * up to 2 descriptors that follow the
 * first descriptor.
 *
 */
static inline u16 submit_tx(struct sdma_engine *sde, struct sdma_txreq *tx)
{
	int i;
	u16 tail;
	struct sdma_desc *descp = tx->descp;
	u8 skip = 0, mode = ahg_mode(tx);

	tail = sde->descq_tail & sde->sdma_mask;
	sde->descq[tail].qw[0] = cpu_to_le64(descp->qw[0]);
	sde->descq[tail].qw[1] = cpu_to_le64(add_gen(sde, descp->qw[1]));
	trace_hfi1_sdma_descriptor(sde, descp->qw[0], descp->qw[1],
				   tail, &sde->descq[tail]);
	tail = ++sde->descq_tail & sde->sdma_mask;
	descp++;
	if (mode > SDMA_AHG_APPLY_UPDATE1)
		skip = mode >> 1;
	for (i = 1; i < tx->num_desc; i++, descp++) {
		u64 qw1;

		sde->descq[tail].qw[0] = cpu_to_le64(descp->qw[0]);
		if (skip) {
			/* edits don't have generation */
			qw1 = descp->qw[1];
			skip--;
		} else {
			/* replace generation with real one for non-edits */
			qw1 = add_gen(sde, descp->qw[1]);
		}
		sde->descq[tail].qw[1] = cpu_to_le64(qw1);
		trace_hfi1_sdma_descriptor(sde, descp->qw[0], qw1,
					   tail, &sde->descq[tail]);
		tail = ++sde->descq_tail & sde->sdma_mask;
	}
	tx->next_descq_idx = tail;
#ifdef CONFIG_HFI1_DEBUG_SDMA_ORDER
	tx->sn = sde->tail_sn++;
	trace_hfi1_sdma_in_sn(sde, tx->sn);
	WARN_ON_ONCE(sde->tx_ring[sde->tx_tail & sde->sdma_mask]);
#endif
	sde->tx_ring[sde->tx_tail++ & sde->sdma_mask] = tx;
	sde->desc_avail -= tx->num_desc;
	return tail;
}

/*
 * Check for progress
 */
static int sdma_check_progress(
	struct sdma_engine *sde,
	struct iowait *wait,
	struct sdma_txreq *tx)
{
	int ret;

	sde->desc_avail = sdma_descq_freecnt(sde);
	if (tx->num_desc <= sde->desc_avail)
		return -EAGAIN;
	/* pulse the head_lock */
	if (wait && wait->sleep) {
		unsigned seq;

		seq = raw_seqcount_begin(
			(const seqcount_t *)&sde->head_lock.seqcount);
		ret = wait->sleep(sde, wait, tx, seq);
		if (ret == -EAGAIN)
			sde->desc_avail = sdma_descq_freecnt(sde);
	} else {
		ret = -EBUSY;
	}
	return ret;
}

/**
 * sdma_send_txreq() - submit a tx req to ring
 * @sde: sdma engine to use
 * @wait: wait structure to use when full (may be NULL)
 * @tx: sdma_txreq to submit
 *
 * The call submits the tx into the ring.  If a iowait structure is non-NULL
 * the packet will be queued to the list in wait.
 *
 * Return:
 * 0 - Success, -EINVAL - sdma_txreq incomplete, -EBUSY - no space in
 * ring (wait == NULL)
 * -EIOCBQUEUED - tx queued to iowait, -ECOMM bad sdma state
 */
int sdma_send_txreq(struct sdma_engine *sde,
		    struct iowait *wait,
		    struct sdma_txreq *tx)
{
	int ret = 0;
	u16 tail;
	unsigned long flags;

	/* user should have supplied entire packet */
	if (unlikely(tx->tlen))
		return -EINVAL;
	tx->wait = wait;
	spin_lock_irqsave(&sde->tail_lock, flags);
retry:
	if (unlikely(!__sdma_running(sde)))
		goto unlock_noconn;
	if (unlikely(tx->num_desc > sde->desc_avail))
		goto nodesc;
	tail = submit_tx(sde, tx);
	if (wait)
		iowait_sdma_inc(wait);
	sdma_update_tail(sde, tail);
unlock:
	spin_unlock_irqrestore(&sde->tail_lock, flags);
	return ret;
unlock_noconn:
	if (wait)
		iowait_sdma_inc(wait);
	tx->next_descq_idx = 0;
#ifdef CONFIG_HFI1_DEBUG_SDMA_ORDER
	tx->sn = sde->tail_sn++;
	trace_hfi1_sdma_in_sn(sde, tx->sn);
#endif
	spin_lock(&sde->flushlist_lock);
	list_add_tail(&tx->list, &sde->flushlist);
	spin_unlock(&sde->flushlist_lock);
	if (wait) {
		wait->tx_count++;
		wait->count += tx->num_desc;
	}
	schedule_work(&sde->flush_worker);
	ret = -ECOMM;
	goto unlock;
nodesc:
	ret = sdma_check_progress(sde, wait, tx);
	if (ret == -EAGAIN) {
		ret = 0;
		goto retry;
	}
	sde->descq_full_count++;
	goto unlock;
}

/**
 * sdma_send_txlist() - submit a list of tx req to ring
 * @sde: sdma engine to use
 * @wait: wait structure to use when full (may be NULL)
 * @tx_list: list of sdma_txreqs to submit
 * @count: pointer to a u32 which, after return will contain the total number of
 *         sdma_txreqs removed from the tx_list. This will include sdma_txreqs
 *         whose SDMA descriptors are submitted to the ring and the sdma_txreqs
 *         which are added to SDMA engine flush list if the SDMA engine state is
 *         not running.
 *
 * The call submits the list into the ring.
 *
 * If the iowait structure is non-NULL and not equal to the iowait list
 * the unprocessed part of the list  will be appended to the list in wait.
 *
 * In all cases, the tx_list will be updated so the head of the tx_list is
 * the list of descriptors that have yet to be transmitted.
 *
 * The intent of this call is to provide a more efficient
 * way of submitting multiple packets to SDMA while holding the tail
 * side locking.
 *
 * Return:
 * 0 - Success,
 * -EINVAL - sdma_txreq incomplete, -EBUSY - no space in ring (wait == NULL)
 * -EIOCBQUEUED - tx queued to iowait, -ECOMM bad sdma state
 */
int sdma_send_txlist(struct sdma_engine *sde, struct iowait *wait,
		     struct list_head *tx_list, u32 *count_out)
{
	struct sdma_txreq *tx, *tx_next;
	int ret = 0;
	unsigned long flags;
	u16 tail = INVALID_TAIL;
	u32 submit_count = 0, flush_count = 0, total_count;

	spin_lock_irqsave(&sde->tail_lock, flags);
retry:
	list_for_each_entry_safe(tx, tx_next, tx_list, list) {
		tx->wait = wait;
		if (unlikely(!__sdma_running(sde)))
			goto unlock_noconn;
		if (unlikely(tx->num_desc > sde->desc_avail))
			goto nodesc;
		if (unlikely(tx->tlen)) {
			ret = -EINVAL;
			goto update_tail;
		}
		list_del_init(&tx->list);
		tail = submit_tx(sde, tx);
		submit_count++;
		if (tail != INVALID_TAIL &&
		    (submit_count & SDMA_TAIL_UPDATE_THRESH) == 0) {
			sdma_update_tail(sde, tail);
			tail = INVALID_TAIL;
		}
	}
update_tail:
	total_count = submit_count + flush_count;
	if (wait)
		iowait_sdma_add(wait, total_count);
	if (tail != INVALID_TAIL)
		sdma_update_tail(sde, tail);
	spin_unlock_irqrestore(&sde->tail_lock, flags);
	*count_out = total_count;
	return ret;
unlock_noconn:
	spin_lock(&sde->flushlist_lock);
	list_for_each_entry_safe(tx, tx_next, tx_list, list) {
		tx->wait = wait;
		list_del_init(&tx->list);
		tx->next_descq_idx = 0;
#ifdef CONFIG_HFI1_DEBUG_SDMA_ORDER
		tx->sn = sde->tail_sn++;
		trace_hfi1_sdma_in_sn(sde, tx->sn);
#endif
		list_add_tail(&tx->list, &sde->flushlist);
		flush_count++;
		if (wait) {
			wait->tx_count++;
			wait->count += tx->num_desc;
		}
	}
	spin_unlock(&sde->flushlist_lock);
	schedule_work(&sde->flush_worker);
	ret = -ECOMM;
	goto update_tail;
nodesc:
	ret = sdma_check_progress(sde, wait, tx);
	if (ret == -EAGAIN) {
		ret = 0;
		goto retry;
	}
	sde->descq_full_count++;
	goto update_tail;
}

static void sdma_process_event(struct sdma_engine *sde, enum sdma_events event)
{
	unsigned long flags;

	spin_lock_irqsave(&sde->tail_lock, flags);
	write_seqlock(&sde->head_lock);

	__sdma_process_event(sde, event);

	if (sde->state.current_state == sdma_state_s99_running)
		sdma_desc_avail(sde, sdma_descq_freecnt(sde));

	write_sequnlock(&sde->head_lock);
	spin_unlock_irqrestore(&sde->tail_lock, flags);
}

static void __sdma_process_event(struct sdma_engine *sde,
				 enum sdma_events event)
{
	struct sdma_state *ss = &sde->state;
	int need_progress = 0;

	/* CONFIG SDMA temporary */
#ifdef CONFIG_SDMA_VERBOSITY
	dd_dev_err(sde->dd, "CONFIG SDMA(%u) [%s] %s\n", sde->this_idx,
		   sdma_state_names[ss->current_state],
		   sdma_event_names[event]);
#endif

	switch (ss->current_state) {
	case sdma_state_s00_hw_down:
		switch (event) {
		case sdma_event_e00_go_hw_down:
			break;
		case sdma_event_e30_go_running:
			/*
			 * If down, but running requested (usually result
			 * of link up, then we need to start up.
			 * This can happen when hw down is requested while
			 * bringing the link up with traffic active on
			 * 7220, e.g.
			 */
			ss->go_s99_running = 1;
			/* fall through and start dma engine */
		case sdma_event_e10_go_hw_start:
			/* This reference means the state machine is started */
			sdma_get(&sde->state);
			sdma_set_state(sde,
				       sdma_state_s10_hw_start_up_halt_wait);
			break;
		case sdma_event_e15_hw_halt_done:
			break;
		case sdma_event_e25_hw_clean_up_done:
			break;
		case sdma_event_e40_sw_cleaned:
			sdma_sw_tear_down(sde);
			break;
		case sdma_event_e50_hw_cleaned:
			break;
		case sdma_event_e60_hw_halted:
			break;
		case sdma_event_e70_go_idle:
			break;
		case sdma_event_e80_hw_freeze:
			break;
		case sdma_event_e81_hw_frozen:
			break;
		case sdma_event_e82_hw_unfreeze:
			break;
		case sdma_event_e85_link_down:
			break;
		case sdma_event_e90_sw_halted:
			break;
		}
		break;

	case sdma_state_s10_hw_start_up_halt_wait:
		switch (event) {
		case sdma_event_e00_go_hw_down:
			sdma_set_state(sde, sdma_state_s00_hw_down);
			sdma_sw_tear_down(sde);
			break;
		case sdma_event_e10_go_hw_start:
			break;
		case sdma_event_e15_hw_halt_done:
			sdma_set_state(sde,
				       sdma_state_s15_hw_start_up_clean_wait);
			sdma_start_hw_clean_up(sde);
			break;
		case sdma_event_e25_hw_clean_up_done:
			break;
		case sdma_event_e30_go_running:
			ss->go_s99_running = 1;
			break;
		case sdma_event_e40_sw_cleaned:
			break;
		case sdma_event_e50_hw_cleaned:
			break;
		case sdma_event_e60_hw_halted:
			schedule_work(&sde->err_halt_worker);
			break;
		case sdma_event_e70_go_idle:
			ss->go_s99_running = 0;
			break;
		case sdma_event_e80_hw_freeze:
			break;
		case sdma_event_e81_hw_frozen:
			break;
		case sdma_event_e82_hw_unfreeze:
			break;
		case sdma_event_e85_link_down:
			break;
		case sdma_event_e90_sw_halted:
			break;
		}
		break;

	case sdma_state_s15_hw_start_up_clean_wait:
		switch (event) {
		case sdma_event_e00_go_hw_down:
			sdma_set_state(sde, sdma_state_s00_hw_down);
			sdma_sw_tear_down(sde);
			break;
		case sdma_event_e10_go_hw_start:
			break;
		case sdma_event_e15_hw_halt_done:
			break;
		case sdma_event_e25_hw_clean_up_done:
			sdma_hw_start_up(sde);
			sdma_set_state(sde, ss->go_s99_running ?
				       sdma_state_s99_running :
				       sdma_state_s20_idle);
			break;
		case sdma_event_e30_go_running:
			ss->go_s99_running = 1;
			break;
		case sdma_event_e40_sw_cleaned:
			break;
		case sdma_event_e50_hw_cleaned:
			break;
		case sdma_event_e60_hw_halted:
			break;
		case sdma_event_e70_go_idle:
			ss->go_s99_running = 0;
			break;
		case sdma_event_e80_hw_freeze:
			break;
		case sdma_event_e81_hw_frozen:
			break;
		case sdma_event_e82_hw_unfreeze:
			break;
		case sdma_event_e85_link_down:
			break;
		case sdma_event_e90_sw_halted:
			break;
		}
		break;

	case sdma_state_s20_idle:
		switch (event) {
		case sdma_event_e00_go_hw_down:
			sdma_set_state(sde, sdma_state_s00_hw_down);
			sdma_sw_tear_down(sde);
			break;
		case sdma_event_e10_go_hw_start:
			break;
		case sdma_event_e15_hw_halt_done:
			break;
		case sdma_event_e25_hw_clean_up_done:
			break;
		case sdma_event_e30_go_running:
			sdma_set_state(sde, sdma_state_s99_running);
			ss->go_s99_running = 1;
			break;
		case sdma_event_e40_sw_cleaned:
			break;
		case sdma_event_e50_hw_cleaned:
			break;
		case sdma_event_e60_hw_halted:
			sdma_set_state(sde, sdma_state_s50_hw_halt_wait);
			schedule_work(&sde->err_halt_worker);
			break;
		case sdma_event_e70_go_idle:
			break;
		case sdma_event_e85_link_down:
			/* fall through */
		case sdma_event_e80_hw_freeze:
			sdma_set_state(sde, sdma_state_s80_hw_freeze);
			atomic_dec(&sde->dd->sdma_unfreeze_count);
			wake_up_interruptible(&sde->dd->sdma_unfreeze_wq);
			break;
		case sdma_event_e81_hw_frozen:
			break;
		case sdma_event_e82_hw_unfreeze:
			break;
		case sdma_event_e90_sw_halted:
			break;
		}
		break;

	case sdma_state_s30_sw_clean_up_wait:
		switch (event) {
		case sdma_event_e00_go_hw_down:
			sdma_set_state(sde, sdma_state_s00_hw_down);
			break;
		case sdma_event_e10_go_hw_start:
			break;
		case sdma_event_e15_hw_halt_done:
			break;
		case sdma_event_e25_hw_clean_up_done:
			break;
		case sdma_event_e30_go_running:
			ss->go_s99_running = 1;
			break;
		case sdma_event_e40_sw_cleaned:
			sdma_set_state(sde, sdma_state_s40_hw_clean_up_wait);
			sdma_start_hw_clean_up(sde);
			break;
		case sdma_event_e50_hw_cleaned:
			break;
		case sdma_event_e60_hw_halted:
			break;
		case sdma_event_e70_go_idle:
			ss->go_s99_running = 0;
			break;
		case sdma_event_e80_hw_freeze:
			break;
		case sdma_event_e81_hw_frozen:
			break;
		case sdma_event_e82_hw_unfreeze:
			break;
		case sdma_event_e85_link_down:
			ss->go_s99_running = 0;
			break;
		case sdma_event_e90_sw_halted:
			break;
		}
		break;

	case sdma_state_s40_hw_clean_up_wait:
		switch (event) {
		case sdma_event_e00_go_hw_down:
			sdma_set_state(sde, sdma_state_s00_hw_down);
			tasklet_hi_schedule(&sde->sdma_sw_clean_up_task);
			break;
		case sdma_event_e10_go_hw_start:
			break;
		case sdma_event_e15_hw_halt_done:
			break;
		case sdma_event_e25_hw_clean_up_done:
			sdma_hw_start_up(sde);
			sdma_set_state(sde, ss->go_s99_running ?
				       sdma_state_s99_running :
				       sdma_state_s20_idle);
			break;
		case sdma_event_e30_go_running:
			ss->go_s99_running = 1;
			break;
		case sdma_event_e40_sw_cleaned:
			break;
		case sdma_event_e50_hw_cleaned:
			break;
		case sdma_event_e60_hw_halted:
			break;
		case sdma_event_e70_go_idle:
			ss->go_s99_running = 0;
			break;
		case sdma_event_e80_hw_freeze:
			break;
		case sdma_event_e81_hw_frozen:
			break;
		case sdma_event_e82_hw_unfreeze:
			break;
		case sdma_event_e85_link_down:
			ss->go_s99_running = 0;
			break;
		case sdma_event_e90_sw_halted:
			break;
		}
		break;

	case sdma_state_s50_hw_halt_wait:
		switch (event) {
		case sdma_event_e00_go_hw_down:
			sdma_set_state(sde, sdma_state_s00_hw_down);
			tasklet_hi_schedule(&sde->sdma_sw_clean_up_task);
			break;
		case sdma_event_e10_go_hw_start:
			break;
		case sdma_event_e15_hw_halt_done:
			sdma_set_state(sde, sdma_state_s30_sw_clean_up_wait);
			tasklet_hi_schedule(&sde->sdma_sw_clean_up_task);
			break;
		case sdma_event_e25_hw_clean_up_done:
			break;
		case sdma_event_e30_go_running:
			ss->go_s99_running = 1;
			break;
		case sdma_event_e40_sw_cleaned:
			break;
		case sdma_event_e50_hw_cleaned:
			break;
		case sdma_event_e60_hw_halted:
			schedule_work(&sde->err_halt_worker);
			break;
		case sdma_event_e70_go_idle:
			ss->go_s99_running = 0;
			break;
		case sdma_event_e80_hw_freeze:
			break;
		case sdma_event_e81_hw_frozen:
			break;
		case sdma_event_e82_hw_unfreeze:
			break;
		case sdma_event_e85_link_down:
			ss->go_s99_running = 0;
			break;
		case sdma_event_e90_sw_halted:
			break;
		}
		break;

	case sdma_state_s60_idle_halt_wait:
		switch (event) {
		case sdma_event_e00_go_hw_down:
			sdma_set_state(sde, sdma_state_s00_hw_down);
			tasklet_hi_schedule(&sde->sdma_sw_clean_up_task);
			break;
		case sdma_event_e10_go_hw_start:
			break;
		case sdma_event_e15_hw_halt_done:
			sdma_set_state(sde, sdma_state_s30_sw_clean_up_wait);
			tasklet_hi_schedule(&sde->sdma_sw_clean_up_task);
			break;
		case sdma_event_e25_hw_clean_up_done:
			break;
		case sdma_event_e30_go_running:
			ss->go_s99_running = 1;
			break;
		case sdma_event_e40_sw_cleaned:
			break;
		case sdma_event_e50_hw_cleaned:
			break;
		case sdma_event_e60_hw_halted:
			schedule_work(&sde->err_halt_worker);
			break;
		case sdma_event_e70_go_idle:
			ss->go_s99_running = 0;
			break;
		case sdma_event_e80_hw_freeze:
			break;
		case sdma_event_e81_hw_frozen:
			break;
		case sdma_event_e82_hw_unfreeze:
			break;
		case sdma_event_e85_link_down:
			break;
		case sdma_event_e90_sw_halted:
			break;
		}
		break;

	case sdma_state_s80_hw_freeze:
		switch (event) {
		case sdma_event_e00_go_hw_down:
			sdma_set_state(sde, sdma_state_s00_hw_down);
			tasklet_hi_schedule(&sde->sdma_sw_clean_up_task);
			break;
		case sdma_event_e10_go_hw_start:
			break;
		case sdma_event_e15_hw_halt_done:
			break;
		case sdma_event_e25_hw_clean_up_done:
			break;
		case sdma_event_e30_go_running:
			ss->go_s99_running = 1;
			break;
		case sdma_event_e40_sw_cleaned:
			break;
		case sdma_event_e50_hw_cleaned:
			break;
		case sdma_event_e60_hw_halted:
			break;
		case sdma_event_e70_go_idle:
			ss->go_s99_running = 0;
			break;
		case sdma_event_e80_hw_freeze:
			break;
		case sdma_event_e81_hw_frozen:
			sdma_set_state(sde, sdma_state_s82_freeze_sw_clean);
			tasklet_hi_schedule(&sde->sdma_sw_clean_up_task);
			break;
		case sdma_event_e82_hw_unfreeze:
			break;
		case sdma_event_e85_link_down:
			break;
		case sdma_event_e90_sw_halted:
			break;
		}
		break;

	case sdma_state_s82_freeze_sw_clean:
		switch (event) {
		case sdma_event_e00_go_hw_down:
			sdma_set_state(sde, sdma_state_s00_hw_down);
			tasklet_hi_schedule(&sde->sdma_sw_clean_up_task);
			break;
		case sdma_event_e10_go_hw_start:
			break;
		case sdma_event_e15_hw_halt_done:
			break;
		case sdma_event_e25_hw_clean_up_done:
			break;
		case sdma_event_e30_go_running:
			ss->go_s99_running = 1;
			break;
		case sdma_event_e40_sw_cleaned:
			/* notify caller this engine is done cleaning */
			atomic_dec(&sde->dd->sdma_unfreeze_count);
			wake_up_interruptible(&sde->dd->sdma_unfreeze_wq);
			break;
		case sdma_event_e50_hw_cleaned:
			break;
		case sdma_event_e60_hw_halted:
			break;
		case sdma_event_e70_go_idle:
			ss->go_s99_running = 0;
			break;
		case sdma_event_e80_hw_freeze:
			break;
		case sdma_event_e81_hw_frozen:
			break;
		case sdma_event_e82_hw_unfreeze:
			sdma_hw_start_up(sde);
			sdma_set_state(sde, ss->go_s99_running ?
				       sdma_state_s99_running :
				       sdma_state_s20_idle);
			break;
		case sdma_event_e85_link_down:
			break;
		case sdma_event_e90_sw_halted:
			break;
		}
		break;

	case sdma_state_s99_running:
		switch (event) {
		case sdma_event_e00_go_hw_down:
			sdma_set_state(sde, sdma_state_s00_hw_down);
			tasklet_hi_schedule(&sde->sdma_sw_clean_up_task);
			break;
		case sdma_event_e10_go_hw_start:
			break;
		case sdma_event_e15_hw_halt_done:
			break;
		case sdma_event_e25_hw_clean_up_done:
			break;
		case sdma_event_e30_go_running:
			break;
		case sdma_event_e40_sw_cleaned:
			break;
		case sdma_event_e50_hw_cleaned:
			break;
		case sdma_event_e60_hw_halted:
			need_progress = 1;
			sdma_err_progress_check_schedule(sde);
		case sdma_event_e90_sw_halted:
			/*
			* SW initiated halt does not perform engines
			* progress check
			*/
			sdma_set_state(sde, sdma_state_s50_hw_halt_wait);
			schedule_work(&sde->err_halt_worker);
			break;
		case sdma_event_e70_go_idle:
			sdma_set_state(sde, sdma_state_s60_idle_halt_wait);
			break;
		case sdma_event_e85_link_down:
			ss->go_s99_running = 0;
			/* fall through */
		case sdma_event_e80_hw_freeze:
			sdma_set_state(sde, sdma_state_s80_hw_freeze);
			atomic_dec(&sde->dd->sdma_unfreeze_count);
			wake_up_interruptible(&sde->dd->sdma_unfreeze_wq);
			break;
		case sdma_event_e81_hw_frozen:
			break;
		case sdma_event_e82_hw_unfreeze:
			break;
		}
		break;
	}

	ss->last_event = event;
	if (need_progress)
		sdma_make_progress(sde, 0);
}

/*
 * _extend_sdma_tx_descs() - helper to extend txreq
 *
 * This is called once the initial nominal allocation
 * of descriptors in the sdma_txreq is exhausted.
 *
 * The code will bump the allocation up to the max
 * of MAX_DESC (64) descriptors. There doesn't seem
 * much point in an interim step. The last descriptor
 * is reserved for coalesce buffer in order to support
 * cases where input packet has >MAX_DESC iovecs.
 *
 */
static int _extend_sdma_tx_descs(struct hfi1_devdata *dd, struct sdma_txreq *tx)
{
	int i;

	/* Handle last descriptor */
	if (unlikely((tx->num_desc == (MAX_DESC - 1)))) {
		/* if tlen is 0, it is for padding, release last descriptor */
		if (!tx->tlen) {
			tx->desc_limit = MAX_DESC;
		} else if (!tx->coalesce_buf) {
			/* allocate coalesce buffer with space for padding */
			tx->coalesce_buf = kmalloc(tx->tlen + sizeof(u32),
						   GFP_ATOMIC);
			if (!tx->coalesce_buf)
				goto enomem;
			tx->coalesce_idx = 0;
		}
		return 0;
	}

	if (unlikely(tx->num_desc == MAX_DESC))
		goto enomem;

	tx->descp = kmalloc_array(
			MAX_DESC,
			sizeof(struct sdma_desc),
			GFP_ATOMIC);
	if (!tx->descp)
		goto enomem;

	/* reserve last descriptor for coalescing */
	tx->desc_limit = MAX_DESC - 1;
	/* copy ones already built */
	for (i = 0; i < tx->num_desc; i++)
		tx->descp[i] = tx->descs[i];
	return 0;
enomem:
	__sdma_txclean(dd, tx);
	return -ENOMEM;
}

/*
 * ext_coal_sdma_tx_descs() - extend or coalesce sdma tx descriptors
 *
 * This is called once the initial nominal allocation of descriptors
 * in the sdma_txreq is exhausted.
 *
 * This function calls _extend_sdma_tx_descs to extend or allocate
 * coalesce buffer. If there is a allocated coalesce buffer, it will
 * copy the input packet data into the coalesce buffer. It also adds
 * coalesce buffer descriptor once when whole packet is received.
 *
 * Return:
 * <0 - error
 * 0 - coalescing, don't populate descriptor
 * 1 - continue with populating descriptor
 */
int ext_coal_sdma_tx_descs(struct hfi1_devdata *dd, struct sdma_txreq *tx,
			   int type, void *kvaddr, struct page *page,
			   unsigned long offset, u16 len)
{
	int pad_len, rval;
	dma_addr_t addr;

	rval = _extend_sdma_tx_descs(dd, tx);
	if (rval) {
		__sdma_txclean(dd, tx);
		return rval;
	}

	/* If coalesce buffer is allocated, copy data into it */
	if (tx->coalesce_buf) {
		if (type == SDMA_MAP_NONE) {
			__sdma_txclean(dd, tx);
			return -EINVAL;
		}

		if (type == SDMA_MAP_PAGE) {
			kvaddr = kmap(page);
			kvaddr += offset;
		} else if (WARN_ON(!kvaddr)) {
			__sdma_txclean(dd, tx);
			return -EINVAL;
		}

		memcpy(tx->coalesce_buf + tx->coalesce_idx, kvaddr, len);
		tx->coalesce_idx += len;
		if (type == SDMA_MAP_PAGE)
			kunmap(page);

		/* If there is more data, return */
		if (tx->tlen - tx->coalesce_idx)
			return 0;

		/* Whole packet is received; add any padding */
		pad_len = tx->packet_len & (sizeof(u32) - 1);
		if (pad_len) {
			pad_len = sizeof(u32) - pad_len;
			memset(tx->coalesce_buf + tx->coalesce_idx, 0, pad_len);
			/* padding is taken care of for coalescing case */
			tx->packet_len += pad_len;
			tx->tlen += pad_len;
		}

		/* dma map the coalesce buffer */
		addr = dma_map_single(&dd->pcidev->dev,
				      tx->coalesce_buf,
				      tx->tlen,
				      DMA_TO_DEVICE);

		if (unlikely(dma_mapping_error(&dd->pcidev->dev, addr))) {
			__sdma_txclean(dd, tx);
			return -ENOSPC;
		}

		/* Add descriptor for coalesce buffer */
		tx->desc_limit = MAX_DESC;
		return _sdma_txadd_daddr(dd, SDMA_MAP_SINGLE, tx,
					 addr, tx->tlen);
	}

	return 1;
}

/* Update sdes when the lmc changes */
void sdma_update_lmc(struct hfi1_devdata *dd, u64 mask, u32 lid)
{
	struct sdma_engine *sde;
	int i;
	u64 sreg;

	sreg = ((mask & SD(CHECK_SLID_MASK_MASK)) <<
		SD(CHECK_SLID_MASK_SHIFT)) |
		(((lid & mask) & SD(CHECK_SLID_VALUE_MASK)) <<
		SD(CHECK_SLID_VALUE_SHIFT));

	for (i = 0; i < dd->num_sdma; i++) {
		hfi1_cdbg(LINKVERB, "SendDmaEngine[%d].SLID_CHECK = 0x%x",
			  i, (u32)sreg);
		sde = &dd->per_sdma[i];
		write_sde_csr(sde, SD(CHECK_SLID), sreg);
	}
}

/* tx not dword sized - pad */
int _pad_sdma_tx_descs(struct hfi1_devdata *dd, struct sdma_txreq *tx)
{
	int rval = 0;

	tx->num_desc++;
	if ((unlikely(tx->num_desc == tx->desc_limit))) {
		rval = _extend_sdma_tx_descs(dd, tx);
		if (rval) {
			__sdma_txclean(dd, tx);
			return rval;
		}
	}
	/* finish the one just added */
	make_tx_sdma_desc(
		tx,
		SDMA_MAP_NONE,
		dd->sdma_pad_phys,
		sizeof(u32) - (tx->packet_len & (sizeof(u32) - 1)));
	_sdma_close_tx(dd, tx);
	return rval;
}

/*
 * Add ahg to the sdma_txreq
 *
 * The logic will consume up to 3
 * descriptors at the beginning of
 * sdma_txreq.
 */
void _sdma_txreq_ahgadd(
	struct sdma_txreq *tx,
	u8 num_ahg,
	u8 ahg_entry,
	u32 *ahg,
	u8 ahg_hlen)
{
	u32 i, shift = 0, desc = 0;
	u8 mode;

	WARN_ON_ONCE(num_ahg > 9 || (ahg_hlen & 3) || ahg_hlen == 4);
	/* compute mode */
	if (num_ahg == 1)
		mode = SDMA_AHG_APPLY_UPDATE1;
	else if (num_ahg <= 5)
		mode = SDMA_AHG_APPLY_UPDATE2;
	else
		mode = SDMA_AHG_APPLY_UPDATE3;
	tx->num_desc++;
	/* initialize to consumed descriptors to zero */
	switch (mode) {
	case SDMA_AHG_APPLY_UPDATE3:
		tx->num_desc++;
		tx->descs[2].qw[0] = 0;
		tx->descs[2].qw[1] = 0;
		/* FALLTHROUGH */
	case SDMA_AHG_APPLY_UPDATE2:
		tx->num_desc++;
		tx->descs[1].qw[0] = 0;
		tx->descs[1].qw[1] = 0;
		break;
	}
	ahg_hlen >>= 2;
	tx->descs[0].qw[1] |=
		(((u64)ahg_entry & SDMA_DESC1_HEADER_INDEX_MASK)
			<< SDMA_DESC1_HEADER_INDEX_SHIFT) |
		(((u64)ahg_hlen & SDMA_DESC1_HEADER_DWS_MASK)
			<< SDMA_DESC1_HEADER_DWS_SHIFT) |
		(((u64)mode & SDMA_DESC1_HEADER_MODE_MASK)
			<< SDMA_DESC1_HEADER_MODE_SHIFT) |
		(((u64)ahg[0] & SDMA_DESC1_HEADER_UPDATE1_MASK)
			<< SDMA_DESC1_HEADER_UPDATE1_SHIFT);
	for (i = 0; i < (num_ahg - 1); i++) {
		if (!shift && !(i & 2))
			desc++;
		tx->descs[desc].qw[!!(i & 2)] |=
			(((u64)ahg[i + 1])
				<< shift);
		shift = (shift + 32) & 63;
	}
}

/**
 * sdma_ahg_alloc - allocate an AHG entry
 * @sde: engine to allocate from
 *
 * Return:
 * 0-31 when successful, -EOPNOTSUPP if AHG is not enabled,
 * -ENOSPC if an entry is not available
 */
int sdma_ahg_alloc(struct sdma_engine *sde)
{
	int nr;
	int oldbit;

	if (!sde) {
		trace_hfi1_ahg_allocate(sde, -EINVAL);
		return -EINVAL;
	}
	while (1) {
		nr = ffz(ACCESS_ONCE(sde->ahg_bits));
		if (nr > 31) {
			trace_hfi1_ahg_allocate(sde, -ENOSPC);
			return -ENOSPC;
		}
		oldbit = test_and_set_bit(nr, &sde->ahg_bits);
		if (!oldbit)
			break;
		cpu_relax();
	}
	trace_hfi1_ahg_allocate(sde, nr);
	return nr;
}

/**
 * sdma_ahg_free - free an AHG entry
 * @sde: engine to return AHG entry
 * @ahg_index: index to free
 *
 * This routine frees the indicate AHG entry.
 */
void sdma_ahg_free(struct sdma_engine *sde, int ahg_index)
{
	if (!sde)
		return;
	trace_hfi1_ahg_deallocate(sde, ahg_index);
	if (ahg_index < 0 || ahg_index > 31)
		return;
	clear_bit(ahg_index, &sde->ahg_bits);
}

/*
 * SPC freeze handling for SDMA engines.  Called when the driver knows
 * the SPC is going into a freeze but before the freeze is fully
 * settled.  Generally an error interrupt.
 *
 * This event will pull the engine out of running so no more entries can be
 * added to the engine's queue.
 */
void sdma_freeze_notify(struct hfi1_devdata *dd, int link_down)
{
	int i;
	enum sdma_events event = link_down ? sdma_event_e85_link_down :
					     sdma_event_e80_hw_freeze;

	/* set up the wait but do not wait here */
	atomic_set(&dd->sdma_unfreeze_count, dd->num_sdma);

	/* tell all engines to stop running and wait */
	for (i = 0; i < dd->num_sdma; i++)
		sdma_process_event(&dd->per_sdma[i], event);

	/* sdma_freeze() will wait for all engines to have stopped */
}

/*
 * SPC freeze handling for SDMA engines.  Called when the driver knows
 * the SPC is fully frozen.
 */
void sdma_freeze(struct hfi1_devdata *dd)
{
	int i;
	int ret;

	/*
	 * Make sure all engines have moved out of the running state before
	 * continuing.
	 */
	ret = wait_event_interruptible(dd->sdma_unfreeze_wq,
				       atomic_read(&dd->sdma_unfreeze_count) <=
				       0);
	/* interrupted or count is negative, then unloading - just exit */
	if (ret || atomic_read(&dd->sdma_unfreeze_count) < 0)
		return;

	/* set up the count for the next wait */
	atomic_set(&dd->sdma_unfreeze_count, dd->num_sdma);

	/* tell all engines that the SPC is frozen, they can start cleaning */
	for (i = 0; i < dd->num_sdma; i++)
		sdma_process_event(&dd->per_sdma[i], sdma_event_e81_hw_frozen);

	/*
	 * Wait for everyone to finish software clean before exiting.  The
	 * software clean will read engine CSRs, so must be completed before
	 * the next step, which will clear the engine CSRs.
	 */
	(void)wait_event_interruptible(dd->sdma_unfreeze_wq,
				atomic_read(&dd->sdma_unfreeze_count) <= 0);
	/* no need to check results - done no matter what */
}

/*
 * SPC freeze handling for the SDMA engines.  Called after the SPC is unfrozen.
 *
 * The SPC freeze acts like a SDMA halt and a hardware clean combined.  All
 * that is left is a software clean.  We could do it after the SPC is fully
 * frozen, but then we'd have to add another state to wait for the unfreeze.
 * Instead, just defer the software clean until the unfreeze step.
 */
void sdma_unfreeze(struct hfi1_devdata *dd)
{
	int i;

	/* tell all engines start freeze clean up */
	for (i = 0; i < dd->num_sdma; i++)
		sdma_process_event(&dd->per_sdma[i],
				   sdma_event_e82_hw_unfreeze);
}

/**
 * _sdma_engine_progress_schedule() - schedule progress on engine
 * @sde: sdma_engine to schedule progress
 *
 */
void _sdma_engine_progress_schedule(
	struct sdma_engine *sde)
{
	trace_hfi1_sdma_engine_progress(sde, sde->progress_mask);
	/* assume we have selected a good cpu */
	write_csr(sde->dd,
		  CCE_INT_FORCE + (8 * (IS_SDMA_START / 64)),
		  sde->progress_mask);
}
