/*
 * Copyright (c) 2008, 2009, 2010 QLogic Corporation. All rights reserved.
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
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>
#include <linux/moduleparam.h>

#include "qib.h"

static unsigned qib_hol_timeout_ms = 3000;
module_param_named(hol_timeout_ms, qib_hol_timeout_ms, uint, S_IRUGO);
MODULE_PARM_DESC(hol_timeout_ms,
		 "duration of user app suspension after link failure");

unsigned qib_sdma_fetch_arb = 1;
module_param_named(fetch_arb, qib_sdma_fetch_arb, uint, S_IRUGO);
MODULE_PARM_DESC(fetch_arb, "IBA7220: change SDMA descriptor arbitration");

/**
 * qib_disarm_piobufs - cancel a range of PIO buffers
 * @dd: the qlogic_ib device
 * @first: the first PIO buffer to cancel
 * @cnt: the number of PIO buffers to cancel
 *
 * Cancel a range of PIO buffers. Used at user process close,
 * in case it died while writing to a PIO buffer.
 */
void qib_disarm_piobufs(struct qib_devdata *dd, unsigned first, unsigned cnt)
{
	unsigned long flags;
	unsigned i;
	unsigned last;

	last = first + cnt;
	spin_lock_irqsave(&dd->pioavail_lock, flags);
	for (i = first; i < last; i++) {
		__clear_bit(i, dd->pio_need_disarm);
		dd->f_sendctrl(dd->pport, QIB_SENDCTRL_DISARM_BUF(i));
	}
	spin_unlock_irqrestore(&dd->pioavail_lock, flags);
}

/*
 * This is called by a user process when it sees the DISARM_BUFS event
 * bit is set.
 */
int qib_disarm_piobufs_ifneeded(struct qib_ctxtdata *rcd)
{
	struct qib_devdata *dd = rcd->dd;
	unsigned i;
	unsigned last;
	unsigned n = 0;

	last = rcd->pio_base + rcd->piocnt;
	/*
	 * Don't need uctxt_lock here, since user has called in to us.
	 * Clear at start in case more interrupts set bits while we
	 * are disarming
	 */
	if (rcd->user_event_mask) {
		/*
		 * subctxt_cnt is 0 if not shared, so do base
		 * separately, first, then remaining subctxt, if any
		 */
		clear_bit(_QIB_EVENT_DISARM_BUFS_BIT, &rcd->user_event_mask[0]);
		for (i = 1; i < rcd->subctxt_cnt; i++)
			clear_bit(_QIB_EVENT_DISARM_BUFS_BIT,
				  &rcd->user_event_mask[i]);
	}
	spin_lock_irq(&dd->pioavail_lock);
	for (i = rcd->pio_base; i < last; i++) {
		if (__test_and_clear_bit(i, dd->pio_need_disarm)) {
			n++;
			dd->f_sendctrl(rcd->ppd, QIB_SENDCTRL_DISARM_BUF(i));
		}
	}
	spin_unlock_irq(&dd->pioavail_lock);
	return 0;
}

static struct qib_pportdata *is_sdma_buf(struct qib_devdata *dd, unsigned i)
{
	struct qib_pportdata *ppd;
	unsigned pidx;

	for (pidx = 0; pidx < dd->num_pports; pidx++) {
		ppd = dd->pport + pidx;
		if (i >= ppd->sdma_state.first_sendbuf &&
		    i < ppd->sdma_state.last_sendbuf)
			return ppd;
	}
	return NULL;
}

/*
 * Return true if send buffer is being used by a user context.
 * Sets  _QIB_EVENT_DISARM_BUFS_BIT in user_event_mask as a side effect
 */
static int find_ctxt(struct qib_devdata *dd, unsigned bufn)
{
	struct qib_ctxtdata *rcd;
	unsigned ctxt;
	int ret = 0;

	spin_lock(&dd->uctxt_lock);
	for (ctxt = dd->first_user_ctxt; ctxt < dd->cfgctxts; ctxt++) {
		rcd = dd->rcd[ctxt];
		if (!rcd || bufn < rcd->pio_base ||
		    bufn >= rcd->pio_base + rcd->piocnt)
			continue;
		if (rcd->user_event_mask) {
			int i;
			/*
			 * subctxt_cnt is 0 if not shared, so do base
			 * separately, first, then remaining subctxt, if any
			 */
			set_bit(_QIB_EVENT_DISARM_BUFS_BIT,
				&rcd->user_event_mask[0]);
			for (i = 1; i < rcd->subctxt_cnt; i++)
				set_bit(_QIB_EVENT_DISARM_BUFS_BIT,
					&rcd->user_event_mask[i]);
		}
		ret = 1;
		break;
	}
	spin_unlock(&dd->uctxt_lock);

	return ret;
}

/*
 * Disarm a set of send buffers.  If the buffer might be actively being
 * written to, mark the buffer to be disarmed later when it is not being
 * written to.
 *
 * This should only be called from the IRQ error handler.
 */
void qib_disarm_piobufs_set(struct qib_devdata *dd, unsigned long *mask,
			    unsigned cnt)
{
	struct qib_pportdata *ppd, *pppd[QIB_MAX_IB_PORTS];
	unsigned i;
	unsigned long flags;

	for (i = 0; i < dd->num_pports; i++)
		pppd[i] = NULL;

	for (i = 0; i < cnt; i++) {
		int which;
		if (!test_bit(i, mask))
			continue;
		/*
		 * If the buffer is owned by the DMA hardware,
		 * reset the DMA engine.
		 */
		ppd = is_sdma_buf(dd, i);
		if (ppd) {
			pppd[ppd->port] = ppd;
			continue;
		}
		/*
		 * If the kernel is writing the buffer or the buffer is
		 * owned by a user process, we can't clear it yet.
		 */
		spin_lock_irqsave(&dd->pioavail_lock, flags);
		if (test_bit(i, dd->pio_writing) ||
		    (!test_bit(i << 1, dd->pioavailkernel) &&
		     find_ctxt(dd, i))) {
			__set_bit(i, dd->pio_need_disarm);
			which = 0;
		} else {
			which = 1;
			dd->f_sendctrl(dd->pport, QIB_SENDCTRL_DISARM_BUF(i));
		}
		spin_unlock_irqrestore(&dd->pioavail_lock, flags);
	}

	/* do cancel_sends once per port that had sdma piobufs in error */
	for (i = 0; i < dd->num_pports; i++)
		if (pppd[i])
			qib_cancel_sends(pppd[i]);
}

/**
 * update_send_bufs - update shadow copy of the PIO availability map
 * @dd: the qlogic_ib device
 *
 * called whenever our local copy indicates we have run out of send buffers
 */
static void update_send_bufs(struct qib_devdata *dd)
{
	unsigned long flags;
	unsigned i;
	const unsigned piobregs = dd->pioavregs;

	/*
	 * If the generation (check) bits have changed, then we update the
	 * busy bit for the corresponding PIO buffer.  This algorithm will
	 * modify positions to the value they already have in some cases
	 * (i.e., no change), but it's faster than changing only the bits
	 * that have changed.
	 *
	 * We would like to do this atomicly, to avoid spinlocks in the
	 * critical send path, but that's not really possible, given the
	 * type of changes, and that this routine could be called on
	 * multiple cpu's simultaneously, so we lock in this routine only,
	 * to avoid conflicting updates; all we change is the shadow, and
	 * it's a single 64 bit memory location, so by definition the update
	 * is atomic in terms of what other cpu's can see in testing the
	 * bits.  The spin_lock overhead isn't too bad, since it only
	 * happens when all buffers are in use, so only cpu overhead, not
	 * latency or bandwidth is affected.
	 */
	if (!dd->pioavailregs_dma)
		return;
	spin_lock_irqsave(&dd->pioavail_lock, flags);
	for (i = 0; i < piobregs; i++) {
		u64 pchbusy, pchg, piov, pnew;

		piov = le64_to_cpu(dd->pioavailregs_dma[i]);
		pchg = dd->pioavailkernel[i] &
			~(dd->pioavailshadow[i] ^ piov);
		pchbusy = pchg << QLOGIC_IB_SENDPIOAVAIL_BUSY_SHIFT;
		if (pchg && (pchbusy & dd->pioavailshadow[i])) {
			pnew = dd->pioavailshadow[i] & ~pchbusy;
			pnew |= piov & pchbusy;
			dd->pioavailshadow[i] = pnew;
		}
	}
	spin_unlock_irqrestore(&dd->pioavail_lock, flags);
}

/*
 * Debugging code and stats updates if no pio buffers available.
 */
static noinline void no_send_bufs(struct qib_devdata *dd)
{
	dd->upd_pio_shadow = 1;

	/* not atomic, but if we lose a stat count in a while, that's OK */
	qib_stats.sps_nopiobufs++;
}

/*
 * Common code for normal driver send buffer allocation, and reserved
 * allocation.
 *
 * Do appropriate marking as busy, etc.
 * Returns buffer pointer if one is found, otherwise NULL.
 */
u32 __iomem *qib_getsendbuf_range(struct qib_devdata *dd, u32 *pbufnum,
				  u32 first, u32 last)
{
	unsigned i, j, updated = 0;
	unsigned nbufs;
	unsigned long flags;
	unsigned long *shadow = dd->pioavailshadow;
	u32 __iomem *buf;

	if (!(dd->flags & QIB_PRESENT))
		return NULL;

	nbufs = last - first + 1; /* number in range to check */
	if (dd->upd_pio_shadow) {
		/*
		 * Minor optimization.  If we had no buffers on last call,
		 * start out by doing the update; continue and do scan even
		 * if no buffers were updated, to be paranoid.
		 */
		update_send_bufs(dd);
		updated++;
	}
	i = first;
rescan:
	/*
	 * While test_and_set_bit() is atomic, we do that and then the
	 * change_bit(), and the pair is not.  See if this is the cause
	 * of the remaining armlaunch errors.
	 */
	spin_lock_irqsave(&dd->pioavail_lock, flags);
	for (j = 0; j < nbufs; j++, i++) {
		if (i > last)
			i = first;
		if (__test_and_set_bit((2 * i) + 1, shadow))
			continue;
		/* flip generation bit */
		__change_bit(2 * i, shadow);
		/* remember that the buffer can be written to now */
		__set_bit(i, dd->pio_writing);
		break;
	}
	spin_unlock_irqrestore(&dd->pioavail_lock, flags);

	if (j == nbufs) {
		if (!updated) {
			/*
			 * First time through; shadow exhausted, but may be
			 * buffers available, try an update and then rescan.
			 */
			update_send_bufs(dd);
			updated++;
			i = first;
			goto rescan;
		}
		no_send_bufs(dd);
		buf = NULL;
	} else {
		if (i < dd->piobcnt2k)
			buf = (u32 __iomem *)(dd->pio2kbase +
				i * dd->palign);
		else if (i < dd->piobcnt2k + dd->piobcnt4k || !dd->piovl15base)
			buf = (u32 __iomem *)(dd->pio4kbase +
				(i - dd->piobcnt2k) * dd->align4k);
		else
			buf = (u32 __iomem *)(dd->piovl15base +
				(i - (dd->piobcnt2k + dd->piobcnt4k)) *
				dd->align4k);
		if (pbufnum)
			*pbufnum = i;
		dd->upd_pio_shadow = 0;
	}

	return buf;
}

/*
 * Record that the caller is finished writing to the buffer so we don't
 * disarm it while it is being written and disarm it now if needed.
 */
void qib_sendbuf_done(struct qib_devdata *dd, unsigned n)
{
	unsigned long flags;

	spin_lock_irqsave(&dd->pioavail_lock, flags);
	__clear_bit(n, dd->pio_writing);
	if (__test_and_clear_bit(n, dd->pio_need_disarm))
		dd->f_sendctrl(dd->pport, QIB_SENDCTRL_DISARM_BUF(n));
	spin_unlock_irqrestore(&dd->pioavail_lock, flags);
}

/**
 * qib_chg_pioavailkernel - change which send buffers are available for kernel
 * @dd: the qlogic_ib device
 * @start: the starting send buffer number
 * @len: the number of send buffers
 * @avail: true if the buffers are available for kernel use, false otherwise
 */
void qib_chg_pioavailkernel(struct qib_devdata *dd, unsigned start,
	unsigned len, u32 avail, struct qib_ctxtdata *rcd)
{
	unsigned long flags;
	unsigned end;
	unsigned ostart = start;

	/* There are two bits per send buffer (busy and generation) */
	start *= 2;
	end = start + len * 2;

	spin_lock_irqsave(&dd->pioavail_lock, flags);
	/* Set or clear the busy bit in the shadow. */
	while (start < end) {
		if (avail) {
			unsigned long dma;
			int i;

			/*
			 * The BUSY bit will never be set, because we disarm
			 * the user buffers before we hand them back to the
			 * kernel.  We do have to make sure the generation
			 * bit is set correctly in shadow, since it could
			 * have changed many times while allocated to user.
			 * We can't use the bitmap functions on the full
			 * dma array because it is always little-endian, so
			 * we have to flip to host-order first.
			 * BITS_PER_LONG is slightly wrong, since it's
			 * always 64 bits per register in chip...
			 * We only work on 64 bit kernels, so that's OK.
			 */
			i = start / BITS_PER_LONG;
			__clear_bit(QLOGIC_IB_SENDPIOAVAIL_BUSY_SHIFT + start,
				    dd->pioavailshadow);
			dma = (unsigned long)
				le64_to_cpu(dd->pioavailregs_dma[i]);
			if (test_bit((QLOGIC_IB_SENDPIOAVAIL_CHECK_SHIFT +
				      start) % BITS_PER_LONG, &dma))
				__set_bit(QLOGIC_IB_SENDPIOAVAIL_CHECK_SHIFT +
					  start, dd->pioavailshadow);
			else
				__clear_bit(QLOGIC_IB_SENDPIOAVAIL_CHECK_SHIFT
					    + start, dd->pioavailshadow);
			__set_bit(start, dd->pioavailkernel);
		} else {
			__set_bit(start + QLOGIC_IB_SENDPIOAVAIL_BUSY_SHIFT,
				  dd->pioavailshadow);
			__clear_bit(start, dd->pioavailkernel);
		}
		start += 2;
	}

	spin_unlock_irqrestore(&dd->pioavail_lock, flags);

	dd->f_txchk_change(dd, ostart, len, avail, rcd);
}

/*
 * Flush all sends that might be in the ready to send state, as well as any
 * that are in the process of being sent.  Used whenever we need to be
 * sure the send side is idle.  Cleans up all buffer state by canceling
 * all pio buffers, and issuing an abort, which cleans up anything in the
 * launch fifo.  The cancel is superfluous on some chip versions, but
 * it's safer to always do it.
 * PIOAvail bits are updated by the chip as if a normal send had happened.
 */
void qib_cancel_sends(struct qib_pportdata *ppd)
{
	struct qib_devdata *dd = ppd->dd;
	struct qib_ctxtdata *rcd;
	unsigned long flags;
	unsigned ctxt;
	unsigned i;
	unsigned last;

	/*
	 * Tell PSM to disarm buffers again before trying to reuse them.
	 * We need to be sure the rcd doesn't change out from under us
	 * while we do so.  We hold the two locks sequentially.  We might
	 * needlessly set some need_disarm bits as a result, if the
	 * context is closed after we release the uctxt_lock, but that's
	 * fairly benign, and safer than nesting the locks.
	 */
	for (ctxt = dd->first_user_ctxt; ctxt < dd->cfgctxts; ctxt++) {
		spin_lock_irqsave(&dd->uctxt_lock, flags);
		rcd = dd->rcd[ctxt];
		if (rcd && rcd->ppd == ppd) {
			last = rcd->pio_base + rcd->piocnt;
			if (rcd->user_event_mask) {
				/*
				 * subctxt_cnt is 0 if not shared, so do base
				 * separately, first, then remaining subctxt,
				 * if any
				 */
				set_bit(_QIB_EVENT_DISARM_BUFS_BIT,
					&rcd->user_event_mask[0]);
				for (i = 1; i < rcd->subctxt_cnt; i++)
					set_bit(_QIB_EVENT_DISARM_BUFS_BIT,
						&rcd->user_event_mask[i]);
			}
			i = rcd->pio_base;
			spin_unlock_irqrestore(&dd->uctxt_lock, flags);
			spin_lock_irqsave(&dd->pioavail_lock, flags);
			for (; i < last; i++)
				__set_bit(i, dd->pio_need_disarm);
			spin_unlock_irqrestore(&dd->pioavail_lock, flags);
		} else
			spin_unlock_irqrestore(&dd->uctxt_lock, flags);
	}

	if (!(dd->flags & QIB_HAS_SEND_DMA))
		dd->f_sendctrl(ppd, QIB_SENDCTRL_DISARM_ALL |
				    QIB_SENDCTRL_FLUSH);
}

/*
 * Force an update of in-memory copy of the pioavail registers, when
 * needed for any of a variety of reasons.
 * If already off, this routine is a nop, on the assumption that the
 * caller (or set of callers) will "do the right thing".
 * This is a per-device operation, so just the first port.
 */
void qib_force_pio_avail_update(struct qib_devdata *dd)
{
	dd->f_sendctrl(dd->pport, QIB_SENDCTRL_AVAIL_BLIP);
}

void qib_hol_down(struct qib_pportdata *ppd)
{
	/*
	 * Cancel sends when the link goes DOWN so that we aren't doing it
	 * at INIT when we might be trying to send SMI packets.
	 */
	if (!(ppd->lflags & QIBL_IB_AUTONEG_INPROG))
		qib_cancel_sends(ppd);
}

/*
 * Link is at INIT.
 * We start the HoL timer so we can detect stuck packets blocking SMP replies.
 * Timer may already be running, so use mod_timer, not add_timer.
 */
void qib_hol_init(struct qib_pportdata *ppd)
{
	if (ppd->hol_state != QIB_HOL_INIT) {
		ppd->hol_state = QIB_HOL_INIT;
		mod_timer(&ppd->hol_timer,
			  jiffies + msecs_to_jiffies(qib_hol_timeout_ms));
	}
}

/*
 * Link is up, continue any user processes, and ensure timer
 * is a nop, if running.  Let timer keep running, if set; it
 * will nop when it sees the link is up.
 */
void qib_hol_up(struct qib_pportdata *ppd)
{
	ppd->hol_state = QIB_HOL_UP;
}

/*
 * This is only called via the timer.
 */
void qib_hol_event(unsigned long opaque)
{
	struct qib_pportdata *ppd = (struct qib_pportdata *)opaque;

	/* If hardware error, etc, skip. */
	if (!(ppd->dd->flags & QIB_INITTED))
		return;

	if (ppd->hol_state != QIB_HOL_UP) {
		/*
		 * Try to flush sends in case a stuck packet is blocking
		 * SMP replies.
		 */
		qib_hol_down(ppd);
		mod_timer(&ppd->hol_timer,
			  jiffies + msecs_to_jiffies(qib_hol_timeout_ms));
	}
}
