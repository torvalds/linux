/*
 * Copyright (c) 2007, 2008 QLogic Corporation. All rights reserved.
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

#include <linux/spinlock.h>

#include "ipath_kernel.h"
#include "ipath_verbs.h"
#include "ipath_common.h"

#define SDMA_DESCQ_SZ PAGE_SIZE /* 256 entries per 4KB page */

static void vl15_watchdog_enq(struct ipath_devdata *dd)
{
	/* ipath_sdma_lock must already be held */
	if (atomic_inc_return(&dd->ipath_sdma_vl15_count) == 1) {
		unsigned long interval = (HZ + 19) / 20;
		dd->ipath_sdma_vl15_timer.expires = jiffies + interval;
		add_timer(&dd->ipath_sdma_vl15_timer);
	}
}

static void vl15_watchdog_deq(struct ipath_devdata *dd)
{
	/* ipath_sdma_lock must already be held */
	if (atomic_dec_return(&dd->ipath_sdma_vl15_count) != 0) {
		unsigned long interval = (HZ + 19) / 20;
		mod_timer(&dd->ipath_sdma_vl15_timer, jiffies + interval);
	} else {
		del_timer(&dd->ipath_sdma_vl15_timer);
	}
}

static void vl15_watchdog_timeout(unsigned long opaque)
{
	struct ipath_devdata *dd = (struct ipath_devdata *)opaque;

	if (atomic_read(&dd->ipath_sdma_vl15_count) != 0) {
		ipath_dbg("vl15 watchdog timeout - clearing\n");
		ipath_cancel_sends(dd, 1);
		ipath_hol_down(dd);
	} else {
		ipath_dbg("vl15 watchdog timeout - "
			  "condition already cleared\n");
	}
}

static void unmap_desc(struct ipath_devdata *dd, unsigned head)
{
	__le64 *descqp = &dd->ipath_sdma_descq[head].qw[0];
	u64 desc[2];
	dma_addr_t addr;
	size_t len;

	desc[0] = le64_to_cpu(descqp[0]);
	desc[1] = le64_to_cpu(descqp[1]);

	addr = (desc[1] << 32) | (desc[0] >> 32);
	len = (desc[0] >> 14) & (0x7ffULL << 2);
	dma_unmap_single(&dd->pcidev->dev, addr, len, DMA_TO_DEVICE);
}

/*
 * ipath_sdma_lock should be locked before calling this.
 */
int ipath_sdma_make_progress(struct ipath_devdata *dd)
{
	struct list_head *lp = NULL;
	struct ipath_sdma_txreq *txp = NULL;
	u16 dmahead;
	u16 start_idx = 0;
	int progress = 0;

	if (!list_empty(&dd->ipath_sdma_activelist)) {
		lp = dd->ipath_sdma_activelist.next;
		txp = list_entry(lp, struct ipath_sdma_txreq, list);
		start_idx = txp->start_idx;
	}

	/*
	 * Read the SDMA head register in order to know that the
	 * interrupt clear has been written to the chip.
	 * Otherwise, we may not get an interrupt for the last
	 * descriptor in the queue.
	 */
	dmahead = (u16)ipath_read_kreg32(dd, dd->ipath_kregs->kr_senddmahead);
	/* sanity check return value for error handling (chip reset, etc.) */
	if (dmahead >= dd->ipath_sdma_descq_cnt)
		goto done;

	while (dd->ipath_sdma_descq_head != dmahead) {
		if (txp && txp->flags & IPATH_SDMA_TXREQ_F_FREEDESC &&
		    dd->ipath_sdma_descq_head == start_idx) {
			unmap_desc(dd, dd->ipath_sdma_descq_head);
			start_idx++;
			if (start_idx == dd->ipath_sdma_descq_cnt)
				start_idx = 0;
		}

		/* increment free count and head */
		dd->ipath_sdma_descq_removed++;
		if (++dd->ipath_sdma_descq_head == dd->ipath_sdma_descq_cnt)
			dd->ipath_sdma_descq_head = 0;

		if (txp && txp->next_descq_idx == dd->ipath_sdma_descq_head) {
			/* move to notify list */
			if (txp->flags & IPATH_SDMA_TXREQ_F_VL15)
				vl15_watchdog_deq(dd);
			list_move_tail(lp, &dd->ipath_sdma_notifylist);
			if (!list_empty(&dd->ipath_sdma_activelist)) {
				lp = dd->ipath_sdma_activelist.next;
				txp = list_entry(lp, struct ipath_sdma_txreq,
						 list);
				start_idx = txp->start_idx;
			} else {
				lp = NULL;
				txp = NULL;
			}
		}
		progress = 1;
	}

	if (progress)
		tasklet_hi_schedule(&dd->ipath_sdma_notify_task);

done:
	return progress;
}

static void ipath_sdma_notify(struct ipath_devdata *dd, struct list_head *list)
{
	struct ipath_sdma_txreq *txp, *txp_next;

	list_for_each_entry_safe(txp, txp_next, list, list) {
		list_del_init(&txp->list);

		if (txp->callback)
			(*txp->callback)(txp->callback_cookie,
					 txp->callback_status);
	}
}

static void sdma_notify_taskbody(struct ipath_devdata *dd)
{
	unsigned long flags;
	struct list_head list;

	INIT_LIST_HEAD(&list);

	spin_lock_irqsave(&dd->ipath_sdma_lock, flags);

	list_splice_init(&dd->ipath_sdma_notifylist, &list);

	spin_unlock_irqrestore(&dd->ipath_sdma_lock, flags);

	ipath_sdma_notify(dd, &list);

	/*
	 * The IB verbs layer needs to see the callback before getting
	 * the call to ipath_ib_piobufavail() because the callback
	 * handles releasing resources the next send will need.
	 * Otherwise, we could do these calls in
	 * ipath_sdma_make_progress().
	 */
	ipath_ib_piobufavail(dd->verbs_dev);
}

static void sdma_notify_task(unsigned long opaque)
{
	struct ipath_devdata *dd = (struct ipath_devdata *)opaque;

	if (!test_bit(IPATH_SDMA_SHUTDOWN, &dd->ipath_sdma_status))
		sdma_notify_taskbody(dd);
}

static void dump_sdma_state(struct ipath_devdata *dd)
{
	unsigned long reg;

	reg = ipath_read_kreg64(dd, dd->ipath_kregs->kr_senddmastatus);
	ipath_cdbg(VERBOSE, "kr_senddmastatus: 0x%016lx\n", reg);

	reg = ipath_read_kreg64(dd, dd->ipath_kregs->kr_sendctrl);
	ipath_cdbg(VERBOSE, "kr_sendctrl: 0x%016lx\n", reg);

	reg = ipath_read_kreg64(dd, dd->ipath_kregs->kr_senddmabufmask0);
	ipath_cdbg(VERBOSE, "kr_senddmabufmask0: 0x%016lx\n", reg);

	reg = ipath_read_kreg64(dd, dd->ipath_kregs->kr_senddmabufmask1);
	ipath_cdbg(VERBOSE, "kr_senddmabufmask1: 0x%016lx\n", reg);

	reg = ipath_read_kreg64(dd, dd->ipath_kregs->kr_senddmabufmask2);
	ipath_cdbg(VERBOSE, "kr_senddmabufmask2: 0x%016lx\n", reg);

	reg = ipath_read_kreg64(dd, dd->ipath_kregs->kr_senddmatail);
	ipath_cdbg(VERBOSE, "kr_senddmatail: 0x%016lx\n", reg);

	reg = ipath_read_kreg64(dd, dd->ipath_kregs->kr_senddmahead);
	ipath_cdbg(VERBOSE, "kr_senddmahead: 0x%016lx\n", reg);
}

static void sdma_abort_task(unsigned long opaque)
{
	struct ipath_devdata *dd = (struct ipath_devdata *) opaque;
	u64 status;
	unsigned long flags;

	if (test_bit(IPATH_SDMA_SHUTDOWN, &dd->ipath_sdma_status))
		return;

	spin_lock_irqsave(&dd->ipath_sdma_lock, flags);

	status = dd->ipath_sdma_status & IPATH_SDMA_ABORT_MASK;

	/* nothing to do */
	if (status == IPATH_SDMA_ABORT_NONE)
		goto unlock;

	/* ipath_sdma_abort() is done, waiting for interrupt */
	if (status == IPATH_SDMA_ABORT_DISARMED) {
		if (jiffies < dd->ipath_sdma_abort_intr_timeout)
			goto resched_noprint;
		/* give up, intr got lost somewhere */
		ipath_dbg("give up waiting for SDMADISABLED intr\n");
		__set_bit(IPATH_SDMA_DISABLED, &dd->ipath_sdma_status);
		status = IPATH_SDMA_ABORT_ABORTED;
	}

	/* everything is stopped, time to clean up and restart */
	if (status == IPATH_SDMA_ABORT_ABORTED) {
		struct ipath_sdma_txreq *txp, *txpnext;
		u64 hwstatus;
		int notify = 0;

		hwstatus = ipath_read_kreg64(dd,
				dd->ipath_kregs->kr_senddmastatus);

		if ((hwstatus & (IPATH_SDMA_STATUS_SCORE_BOARD_DRAIN_IN_PROG |
				 IPATH_SDMA_STATUS_ABORT_IN_PROG	     |
				 IPATH_SDMA_STATUS_INTERNAL_SDMA_ENABLE)) ||
		    !(hwstatus & IPATH_SDMA_STATUS_SCB_EMPTY)) {
			if (dd->ipath_sdma_reset_wait > 0) {
				/* not done shutting down sdma */
				--dd->ipath_sdma_reset_wait;
				goto resched;
			}
			ipath_cdbg(VERBOSE, "gave up waiting for quiescent "
				"status after SDMA reset, continuing\n");
			dump_sdma_state(dd);
		}

		/* dequeue all "sent" requests */
		list_for_each_entry_safe(txp, txpnext,
					 &dd->ipath_sdma_activelist, list) {
			txp->callback_status = IPATH_SDMA_TXREQ_S_ABORTED;
			if (txp->flags & IPATH_SDMA_TXREQ_F_VL15)
				vl15_watchdog_deq(dd);
			list_move_tail(&txp->list, &dd->ipath_sdma_notifylist);
			notify = 1;
		}
		if (notify)
			tasklet_hi_schedule(&dd->ipath_sdma_notify_task);

		/* reset our notion of head and tail */
		dd->ipath_sdma_descq_tail = 0;
		dd->ipath_sdma_descq_head = 0;
		dd->ipath_sdma_head_dma[0] = 0;
		dd->ipath_sdma_generation = 0;
		dd->ipath_sdma_descq_removed = dd->ipath_sdma_descq_added;

		/* Reset SendDmaLenGen */
		ipath_write_kreg(dd, dd->ipath_kregs->kr_senddmalengen,
			(u64) dd->ipath_sdma_descq_cnt | (1ULL << 18));

		/* done with sdma state for a bit */
		spin_unlock_irqrestore(&dd->ipath_sdma_lock, flags);

		/*
		 * Don't restart sdma here (with the exception
		 * below). Wait until link is up to ACTIVE.  VL15 MADs
		 * used to bring the link up use PIO, and multiple link
		 * transitions otherwise cause the sdma engine to be
		 * stopped and started multiple times.
		 * The disable is done here, including the shadow,
		 * so the state is kept consistent.
		 * See ipath_restart_sdma() for the actual starting
		 * of sdma.
		 */
		spin_lock_irqsave(&dd->ipath_sendctrl_lock, flags);
		dd->ipath_sendctrl &= ~INFINIPATH_S_SDMAENABLE;
		ipath_write_kreg(dd, dd->ipath_kregs->kr_sendctrl,
				 dd->ipath_sendctrl);
		ipath_read_kreg64(dd, dd->ipath_kregs->kr_scratch);
		spin_unlock_irqrestore(&dd->ipath_sendctrl_lock, flags);

		/* make sure I see next message */
		dd->ipath_sdma_abort_jiffies = 0;

		/*
		 * Not everything that takes SDMA offline is a link
		 * status change.  If the link was up, restart SDMA.
		 */
		if (dd->ipath_flags & IPATH_LINKACTIVE)
			ipath_restart_sdma(dd);

		goto done;
	}

resched:
	/*
	 * for now, keep spinning
	 * JAG - this is bad to just have default be a loop without
	 * state change
	 */
	if (jiffies > dd->ipath_sdma_abort_jiffies) {
		ipath_dbg("looping with status 0x%08lx\n",
			  dd->ipath_sdma_status);
		dd->ipath_sdma_abort_jiffies = jiffies + 5 * HZ;
	}
resched_noprint:
	spin_unlock_irqrestore(&dd->ipath_sdma_lock, flags);
	if (!test_bit(IPATH_SDMA_SHUTDOWN, &dd->ipath_sdma_status))
		tasklet_hi_schedule(&dd->ipath_sdma_abort_task);
	return;

unlock:
	spin_unlock_irqrestore(&dd->ipath_sdma_lock, flags);
done:
	return;
}

/*
 * This is called from interrupt context.
 */
void ipath_sdma_intr(struct ipath_devdata *dd)
{
	unsigned long flags;

	spin_lock_irqsave(&dd->ipath_sdma_lock, flags);

	(void) ipath_sdma_make_progress(dd);

	spin_unlock_irqrestore(&dd->ipath_sdma_lock, flags);
}

static int alloc_sdma(struct ipath_devdata *dd)
{
	int ret = 0;

	/* Allocate memory for SendDMA descriptor FIFO */
	dd->ipath_sdma_descq = dma_alloc_coherent(&dd->pcidev->dev,
		SDMA_DESCQ_SZ, &dd->ipath_sdma_descq_phys, GFP_KERNEL);

	if (!dd->ipath_sdma_descq) {
		ipath_dev_err(dd, "failed to allocate SendDMA descriptor "
			"FIFO memory\n");
		ret = -ENOMEM;
		goto done;
	}

	dd->ipath_sdma_descq_cnt =
		SDMA_DESCQ_SZ / sizeof(struct ipath_sdma_desc);

	/* Allocate memory for DMA of head register to memory */
	dd->ipath_sdma_head_dma = dma_alloc_coherent(&dd->pcidev->dev,
		PAGE_SIZE, &dd->ipath_sdma_head_phys, GFP_KERNEL);
	if (!dd->ipath_sdma_head_dma) {
		ipath_dev_err(dd, "failed to allocate SendDMA head memory\n");
		ret = -ENOMEM;
		goto cleanup_descq;
	}
	dd->ipath_sdma_head_dma[0] = 0;

	init_timer(&dd->ipath_sdma_vl15_timer);
	dd->ipath_sdma_vl15_timer.function = vl15_watchdog_timeout;
	dd->ipath_sdma_vl15_timer.data = (unsigned long)dd;
	atomic_set(&dd->ipath_sdma_vl15_count, 0);

	goto done;

cleanup_descq:
	dma_free_coherent(&dd->pcidev->dev, SDMA_DESCQ_SZ,
		(void *)dd->ipath_sdma_descq, dd->ipath_sdma_descq_phys);
	dd->ipath_sdma_descq = NULL;
	dd->ipath_sdma_descq_phys = 0;
done:
	return ret;
}

int setup_sdma(struct ipath_devdata *dd)
{
	int ret = 0;
	unsigned i, n;
	u64 tmp64;
	u64 senddmabufmask[3] = { 0 };
	unsigned long flags;

	ret = alloc_sdma(dd);
	if (ret)
		goto done;

	if (!dd->ipath_sdma_descq) {
		ipath_dev_err(dd, "SendDMA memory not allocated\n");
		goto done;
	}

	/*
	 * Set initial status as if we had been up, then gone down.
	 * This lets initial start on transition to ACTIVE be the
	 * same as restart after link flap.
	 */
	dd->ipath_sdma_status = IPATH_SDMA_ABORT_ABORTED;
	dd->ipath_sdma_abort_jiffies = 0;
	dd->ipath_sdma_generation = 0;
	dd->ipath_sdma_descq_tail = 0;
	dd->ipath_sdma_descq_head = 0;
	dd->ipath_sdma_descq_removed = 0;
	dd->ipath_sdma_descq_added = 0;

	/* Set SendDmaBase */
	ipath_write_kreg(dd, dd->ipath_kregs->kr_senddmabase,
			 dd->ipath_sdma_descq_phys);
	/* Set SendDmaLenGen */
	tmp64 = dd->ipath_sdma_descq_cnt;
	tmp64 |= 1<<18; /* enable generation checking */
	ipath_write_kreg(dd, dd->ipath_kregs->kr_senddmalengen, tmp64);
	/* Set SendDmaTail */
	ipath_write_kreg(dd, dd->ipath_kregs->kr_senddmatail,
			 dd->ipath_sdma_descq_tail);
	/* Set SendDmaHeadAddr */
	ipath_write_kreg(dd, dd->ipath_kregs->kr_senddmaheadaddr,
			 dd->ipath_sdma_head_phys);

	/*
	 * Reserve all the former "kernel" piobufs, using high number range
	 * so we get as many 4K buffers as possible
	 */
	n = dd->ipath_piobcnt2k + dd->ipath_piobcnt4k;
	i = dd->ipath_lastport_piobuf + dd->ipath_pioreserved;
	ipath_chg_pioavailkernel(dd, i, n - i , 0);
	for (; i < n; ++i) {
		unsigned word = i / 64;
		unsigned bit = i & 63;
		BUG_ON(word >= 3);
		senddmabufmask[word] |= 1ULL << bit;
	}
	ipath_write_kreg(dd, dd->ipath_kregs->kr_senddmabufmask0,
			 senddmabufmask[0]);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_senddmabufmask1,
			 senddmabufmask[1]);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_senddmabufmask2,
			 senddmabufmask[2]);

	INIT_LIST_HEAD(&dd->ipath_sdma_activelist);
	INIT_LIST_HEAD(&dd->ipath_sdma_notifylist);

	tasklet_init(&dd->ipath_sdma_notify_task, sdma_notify_task,
		     (unsigned long) dd);
	tasklet_init(&dd->ipath_sdma_abort_task, sdma_abort_task,
		     (unsigned long) dd);

	/*
	 * No use to turn on SDMA here, as link is probably not ACTIVE
	 * Just mark it RUNNING and enable the interrupt, and let the
	 * ipath_restart_sdma() on link transition to ACTIVE actually
	 * enable it.
	 */
	spin_lock_irqsave(&dd->ipath_sendctrl_lock, flags);
	dd->ipath_sendctrl |= INFINIPATH_S_SDMAINTENABLE;
	ipath_write_kreg(dd, dd->ipath_kregs->kr_sendctrl, dd->ipath_sendctrl);
	ipath_read_kreg64(dd, dd->ipath_kregs->kr_scratch);
	__set_bit(IPATH_SDMA_RUNNING, &dd->ipath_sdma_status);
	spin_unlock_irqrestore(&dd->ipath_sendctrl_lock, flags);

done:
	return ret;
}

void teardown_sdma(struct ipath_devdata *dd)
{
	struct ipath_sdma_txreq *txp, *txpnext;
	unsigned long flags;
	dma_addr_t sdma_head_phys = 0;
	dma_addr_t sdma_descq_phys = 0;
	void *sdma_descq = NULL;
	void *sdma_head_dma = NULL;

	spin_lock_irqsave(&dd->ipath_sdma_lock, flags);
	__clear_bit(IPATH_SDMA_RUNNING, &dd->ipath_sdma_status);
	__set_bit(IPATH_SDMA_ABORTING, &dd->ipath_sdma_status);
	__set_bit(IPATH_SDMA_SHUTDOWN, &dd->ipath_sdma_status);
	spin_unlock_irqrestore(&dd->ipath_sdma_lock, flags);

	tasklet_kill(&dd->ipath_sdma_abort_task);
	tasklet_kill(&dd->ipath_sdma_notify_task);

	/* turn off sdma */
	spin_lock_irqsave(&dd->ipath_sendctrl_lock, flags);
	dd->ipath_sendctrl &= ~INFINIPATH_S_SDMAENABLE;
	ipath_write_kreg(dd, dd->ipath_kregs->kr_sendctrl,
		dd->ipath_sendctrl);
	ipath_read_kreg64(dd, dd->ipath_kregs->kr_scratch);
	spin_unlock_irqrestore(&dd->ipath_sendctrl_lock, flags);

	spin_lock_irqsave(&dd->ipath_sdma_lock, flags);
	/* dequeue all "sent" requests */
	list_for_each_entry_safe(txp, txpnext, &dd->ipath_sdma_activelist,
				 list) {
		txp->callback_status = IPATH_SDMA_TXREQ_S_SHUTDOWN;
		if (txp->flags & IPATH_SDMA_TXREQ_F_VL15)
			vl15_watchdog_deq(dd);
		list_move_tail(&txp->list, &dd->ipath_sdma_notifylist);
	}
	spin_unlock_irqrestore(&dd->ipath_sdma_lock, flags);

	sdma_notify_taskbody(dd);

	del_timer_sync(&dd->ipath_sdma_vl15_timer);

	spin_lock_irqsave(&dd->ipath_sdma_lock, flags);

	dd->ipath_sdma_abort_jiffies = 0;

	ipath_write_kreg(dd, dd->ipath_kregs->kr_senddmabase, 0);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_senddmalengen, 0);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_senddmatail, 0);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_senddmaheadaddr, 0);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_senddmabufmask0, 0);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_senddmabufmask1, 0);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_senddmabufmask2, 0);

	if (dd->ipath_sdma_head_dma) {
		sdma_head_dma = (void *) dd->ipath_sdma_head_dma;
		sdma_head_phys = dd->ipath_sdma_head_phys;
		dd->ipath_sdma_head_dma = NULL;
		dd->ipath_sdma_head_phys = 0;
	}

	if (dd->ipath_sdma_descq) {
		sdma_descq = dd->ipath_sdma_descq;
		sdma_descq_phys = dd->ipath_sdma_descq_phys;
		dd->ipath_sdma_descq = NULL;
		dd->ipath_sdma_descq_phys = 0;
	}

	spin_unlock_irqrestore(&dd->ipath_sdma_lock, flags);

	if (sdma_head_dma)
		dma_free_coherent(&dd->pcidev->dev, PAGE_SIZE,
				  sdma_head_dma, sdma_head_phys);

	if (sdma_descq)
		dma_free_coherent(&dd->pcidev->dev, SDMA_DESCQ_SZ,
				  sdma_descq, sdma_descq_phys);
}

/*
 * [Re]start SDMA, if we use it, and it's not already OK.
 * This is called on transition to link ACTIVE, either the first or
 * subsequent times.
 */
void ipath_restart_sdma(struct ipath_devdata *dd)
{
	unsigned long flags;
	int needed = 1;

	if (!(dd->ipath_flags & IPATH_HAS_SEND_DMA))
		goto bail;

	/*
	 * First, make sure we should, which is to say,
	 * check that we are "RUNNING" (not in teardown)
	 * and not "SHUTDOWN"
	 */
	spin_lock_irqsave(&dd->ipath_sdma_lock, flags);
	if (!test_bit(IPATH_SDMA_RUNNING, &dd->ipath_sdma_status)
		|| test_bit(IPATH_SDMA_SHUTDOWN, &dd->ipath_sdma_status))
			needed = 0;
	else {
		__clear_bit(IPATH_SDMA_DISABLED, &dd->ipath_sdma_status);
		__clear_bit(IPATH_SDMA_DISARMED, &dd->ipath_sdma_status);
		__clear_bit(IPATH_SDMA_ABORTING, &dd->ipath_sdma_status);
	}
	spin_unlock_irqrestore(&dd->ipath_sdma_lock, flags);
	if (!needed) {
		ipath_dbg("invalid attempt to restart SDMA, status 0x%08lx\n",
			dd->ipath_sdma_status);
		goto bail;
	}
	spin_lock_irqsave(&dd->ipath_sendctrl_lock, flags);
	/*
	 * First clear, just to be safe. Enable is only done
	 * in chip on 0->1 transition
	 */
	dd->ipath_sendctrl &= ~INFINIPATH_S_SDMAENABLE;
	ipath_write_kreg(dd, dd->ipath_kregs->kr_sendctrl, dd->ipath_sendctrl);
	ipath_read_kreg64(dd, dd->ipath_kregs->kr_scratch);
	dd->ipath_sendctrl |= INFINIPATH_S_SDMAENABLE;
	ipath_write_kreg(dd, dd->ipath_kregs->kr_sendctrl, dd->ipath_sendctrl);
	ipath_read_kreg64(dd, dd->ipath_kregs->kr_scratch);
	spin_unlock_irqrestore(&dd->ipath_sendctrl_lock, flags);

	/* notify upper layers */
	ipath_ib_piobufavail(dd->verbs_dev);

bail:
	return;
}

static inline void make_sdma_desc(struct ipath_devdata *dd,
	u64 *sdmadesc, u64 addr, u64 dwlen, u64 dwoffset)
{
	WARN_ON(addr & 3);
	/* SDmaPhyAddr[47:32] */
	sdmadesc[1] = addr >> 32;
	/* SDmaPhyAddr[31:0] */
	sdmadesc[0] = (addr & 0xfffffffcULL) << 32;
	/* SDmaGeneration[1:0] */
	sdmadesc[0] |= (dd->ipath_sdma_generation & 3ULL) << 30;
	/* SDmaDwordCount[10:0] */
	sdmadesc[0] |= (dwlen & 0x7ffULL) << 16;
	/* SDmaBufOffset[12:2] */
	sdmadesc[0] |= dwoffset & 0x7ffULL;
}

/*
 * This function queues one IB packet onto the send DMA queue per call.
 * The caller is responsible for checking:
 * 1) The number of send DMA descriptor entries is less than the size of
 *    the descriptor queue.
 * 2) The IB SGE addresses and lengths are 32-bit aligned
 *    (except possibly the last SGE's length)
 * 3) The SGE addresses are suitable for passing to dma_map_single().
 */
int ipath_sdma_verbs_send(struct ipath_devdata *dd,
	struct ipath_sge_state *ss, u32 dwords,
	struct ipath_verbs_txreq *tx)
{

	unsigned long flags;
	struct ipath_sge *sge;
	int ret = 0;
	u16 tail;
	__le64 *descqp;
	u64 sdmadesc[2];
	u32 dwoffset;
	dma_addr_t addr;

	if ((tx->map_len + (dwords<<2)) > dd->ipath_ibmaxlen) {
		ipath_dbg("packet size %X > ibmax %X, fail\n",
			tx->map_len + (dwords<<2), dd->ipath_ibmaxlen);
		ret = -EMSGSIZE;
		goto fail;
	}

	spin_lock_irqsave(&dd->ipath_sdma_lock, flags);

retry:
	if (unlikely(test_bit(IPATH_SDMA_ABORTING, &dd->ipath_sdma_status))) {
		ret = -EBUSY;
		goto unlock;
	}

	if (tx->txreq.sg_count > ipath_sdma_descq_freecnt(dd)) {
		if (ipath_sdma_make_progress(dd))
			goto retry;
		ret = -ENOBUFS;
		goto unlock;
	}

	addr = dma_map_single(&dd->pcidev->dev, tx->txreq.map_addr,
			      tx->map_len, DMA_TO_DEVICE);
	if (dma_mapping_error(&dd->pcidev->dev, addr))
		goto ioerr;

	dwoffset = tx->map_len >> 2;
	make_sdma_desc(dd, sdmadesc, (u64) addr, dwoffset, 0);

	/* SDmaFirstDesc */
	sdmadesc[0] |= 1ULL << 12;
	if (tx->txreq.flags & IPATH_SDMA_TXREQ_F_USELARGEBUF)
		sdmadesc[0] |= 1ULL << 14;	/* SDmaUseLargeBuf */

	/* write to the descq */
	tail = dd->ipath_sdma_descq_tail;
	descqp = &dd->ipath_sdma_descq[tail].qw[0];
	*descqp++ = cpu_to_le64(sdmadesc[0]);
	*descqp++ = cpu_to_le64(sdmadesc[1]);

	if (tx->txreq.flags & IPATH_SDMA_TXREQ_F_FREEDESC)
		tx->txreq.start_idx = tail;

	/* increment the tail */
	if (++tail == dd->ipath_sdma_descq_cnt) {
		tail = 0;
		descqp = &dd->ipath_sdma_descq[0].qw[0];
		++dd->ipath_sdma_generation;
	}

	sge = &ss->sge;
	while (dwords) {
		u32 dw;
		u32 len;

		len = dwords << 2;
		if (len > sge->length)
			len = sge->length;
		if (len > sge->sge_length)
			len = sge->sge_length;
		BUG_ON(len == 0);
		dw = (len + 3) >> 2;
		addr = dma_map_single(&dd->pcidev->dev, sge->vaddr, dw << 2,
				      DMA_TO_DEVICE);
		if (dma_mapping_error(&dd->pcidev->dev, addr))
			goto unmap;
		make_sdma_desc(dd, sdmadesc, (u64) addr, dw, dwoffset);
		/* SDmaUseLargeBuf has to be set in every descriptor */
		if (tx->txreq.flags & IPATH_SDMA_TXREQ_F_USELARGEBUF)
			sdmadesc[0] |= 1ULL << 14;
		/* write to the descq */
		*descqp++ = cpu_to_le64(sdmadesc[0]);
		*descqp++ = cpu_to_le64(sdmadesc[1]);

		/* increment the tail */
		if (++tail == dd->ipath_sdma_descq_cnt) {
			tail = 0;
			descqp = &dd->ipath_sdma_descq[0].qw[0];
			++dd->ipath_sdma_generation;
		}
		sge->vaddr += len;
		sge->length -= len;
		sge->sge_length -= len;
		if (sge->sge_length == 0) {
			if (--ss->num_sge)
				*sge = *ss->sg_list++;
		} else if (sge->length == 0 && sge->mr != NULL) {
			if (++sge->n >= IPATH_SEGSZ) {
				if (++sge->m >= sge->mr->mapsz)
					break;
				sge->n = 0;
			}
			sge->vaddr =
				sge->mr->map[sge->m]->segs[sge->n].vaddr;
			sge->length =
				sge->mr->map[sge->m]->segs[sge->n].length;
		}

		dwoffset += dw;
		dwords -= dw;
	}

	if (!tail)
		descqp = &dd->ipath_sdma_descq[dd->ipath_sdma_descq_cnt].qw[0];
	descqp -= 2;
	/* SDmaLastDesc */
	descqp[0] |= __constant_cpu_to_le64(1ULL << 11);
	if (tx->txreq.flags & IPATH_SDMA_TXREQ_F_INTREQ) {
		/* SDmaIntReq */
		descqp[0] |= __constant_cpu_to_le64(1ULL << 15);
	}

	/* Commit writes to memory and advance the tail on the chip */
	wmb();
	ipath_write_kreg(dd, dd->ipath_kregs->kr_senddmatail, tail);

	tx->txreq.next_descq_idx = tail;
	tx->txreq.callback_status = IPATH_SDMA_TXREQ_S_OK;
	dd->ipath_sdma_descq_tail = tail;
	dd->ipath_sdma_descq_added += tx->txreq.sg_count;
	list_add_tail(&tx->txreq.list, &dd->ipath_sdma_activelist);
	if (tx->txreq.flags & IPATH_SDMA_TXREQ_F_VL15)
		vl15_watchdog_enq(dd);
	goto unlock;

unmap:
	while (tail != dd->ipath_sdma_descq_tail) {
		if (!tail)
			tail = dd->ipath_sdma_descq_cnt - 1;
		else
			tail--;
		unmap_desc(dd, tail);
	}
ioerr:
	ret = -EIO;
unlock:
	spin_unlock_irqrestore(&dd->ipath_sdma_lock, flags);
fail:
	return ret;
}
