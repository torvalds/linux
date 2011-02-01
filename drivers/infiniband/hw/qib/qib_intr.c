/*
 * Copyright (c) 2006, 2007, 2008, 2009, 2010 QLogic Corporation.
 * All rights reserved.
 * Copyright (c) 2003, 2004, 2005, 2006 PathScale, Inc. All rights reserved.
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

#include <linux/pci.h>
#include <linux/delay.h>

#include "qib.h"
#include "qib_common.h"

/**
 * qib_format_hwmsg - format a single hwerror message
 * @msg message buffer
 * @msgl length of message buffer
 * @hwmsg message to add to message buffer
 */
static void qib_format_hwmsg(char *msg, size_t msgl, const char *hwmsg)
{
	strlcat(msg, "[", msgl);
	strlcat(msg, hwmsg, msgl);
	strlcat(msg, "]", msgl);
}

/**
 * qib_format_hwerrors - format hardware error messages for display
 * @hwerrs hardware errors bit vector
 * @hwerrmsgs hardware error descriptions
 * @nhwerrmsgs number of hwerrmsgs
 * @msg message buffer
 * @msgl message buffer length
 */
void qib_format_hwerrors(u64 hwerrs, const struct qib_hwerror_msgs *hwerrmsgs,
			 size_t nhwerrmsgs, char *msg, size_t msgl)
{
	int i;

	for (i = 0; i < nhwerrmsgs; i++)
		if (hwerrs & hwerrmsgs[i].mask)
			qib_format_hwmsg(msg, msgl, hwerrmsgs[i].msg);
}

static void signal_ib_event(struct qib_pportdata *ppd, enum ib_event_type ev)
{
	struct ib_event event;
	struct qib_devdata *dd = ppd->dd;

	event.device = &dd->verbs_dev.ibdev;
	event.element.port_num = ppd->port;
	event.event = ev;
	ib_dispatch_event(&event);
}

void qib_handle_e_ibstatuschanged(struct qib_pportdata *ppd, u64 ibcs)
{
	struct qib_devdata *dd = ppd->dd;
	unsigned long flags;
	u32 lstate;
	u8 ltstate;
	enum ib_event_type ev = 0;

	lstate = dd->f_iblink_state(ibcs); /* linkstate */
	ltstate = dd->f_ibphys_portstate(ibcs);

	/*
	 * If linkstate transitions into INIT from any of the various down
	 * states, or if it transitions from any of the up (INIT or better)
	 * states into any of the down states (except link recovery), then
	 * call the chip-specific code to take appropriate actions.
	 */
	if (lstate >= IB_PORT_INIT && (ppd->lflags & QIBL_LINKDOWN) &&
	    ltstate == IB_PHYSPORTSTATE_LINKUP) {
		/* transitioned to UP */
		if (dd->f_ib_updown(ppd, 1, ibcs))
			goto skip_ibchange; /* chip-code handled */
	} else if (ppd->lflags & (QIBL_LINKINIT | QIBL_LINKARMED |
		   QIBL_LINKACTIVE | QIBL_IB_FORCE_NOTIFY)) {
		if (ltstate != IB_PHYSPORTSTATE_LINKUP &&
		    ltstate <= IB_PHYSPORTSTATE_CFG_TRAIN &&
		    dd->f_ib_updown(ppd, 0, ibcs))
			goto skip_ibchange; /* chip-code handled */
		qib_set_uevent_bits(ppd, _QIB_EVENT_LINKDOWN_BIT);
	}

	if (lstate != IB_PORT_DOWN) {
		/* lstate is INIT, ARMED, or ACTIVE */
		if (lstate != IB_PORT_ACTIVE) {
			*ppd->statusp &= ~QIB_STATUS_IB_READY;
			if (ppd->lflags & QIBL_LINKACTIVE)
				ev = IB_EVENT_PORT_ERR;
			spin_lock_irqsave(&ppd->lflags_lock, flags);
			if (lstate == IB_PORT_ARMED) {
				ppd->lflags |= QIBL_LINKARMED | QIBL_LINKV;
				ppd->lflags &= ~(QIBL_LINKINIT |
					QIBL_LINKDOWN | QIBL_LINKACTIVE);
			} else {
				ppd->lflags |= QIBL_LINKINIT | QIBL_LINKV;
				ppd->lflags &= ~(QIBL_LINKARMED |
					QIBL_LINKDOWN | QIBL_LINKACTIVE);
			}
			spin_unlock_irqrestore(&ppd->lflags_lock, flags);
			/* start a 75msec timer to clear symbol errors */
			mod_timer(&ppd->symerr_clear_timer,
				  msecs_to_jiffies(75));
		} else if (ltstate == IB_PHYSPORTSTATE_LINKUP &&
			   !(ppd->lflags & QIBL_LINKACTIVE)) {
			/* active, but not active defered */
			qib_hol_up(ppd); /* useful only for 6120 now */
			*ppd->statusp |=
				QIB_STATUS_IB_READY | QIB_STATUS_IB_CONF;
			qib_clear_symerror_on_linkup((unsigned long)ppd);
			spin_lock_irqsave(&ppd->lflags_lock, flags);
			ppd->lflags |= QIBL_LINKACTIVE | QIBL_LINKV;
			ppd->lflags &= ~(QIBL_LINKINIT |
				QIBL_LINKDOWN | QIBL_LINKARMED);
			spin_unlock_irqrestore(&ppd->lflags_lock, flags);
			if (dd->flags & QIB_HAS_SEND_DMA)
				qib_sdma_process_event(ppd,
					qib_sdma_event_e30_go_running);
			ev = IB_EVENT_PORT_ACTIVE;
			dd->f_setextled(ppd, 1);
		}
	} else { /* down */
		if (ppd->lflags & QIBL_LINKACTIVE)
			ev = IB_EVENT_PORT_ERR;
		spin_lock_irqsave(&ppd->lflags_lock, flags);
		ppd->lflags |= QIBL_LINKDOWN | QIBL_LINKV;
		ppd->lflags &= ~(QIBL_LINKINIT |
				 QIBL_LINKACTIVE | QIBL_LINKARMED);
		spin_unlock_irqrestore(&ppd->lflags_lock, flags);
		*ppd->statusp &= ~QIB_STATUS_IB_READY;
	}

skip_ibchange:
	ppd->lastibcstat = ibcs;
	if (ev)
		signal_ib_event(ppd, ev);
	return;
}

void qib_clear_symerror_on_linkup(unsigned long opaque)
{
	struct qib_pportdata *ppd = (struct qib_pportdata *)opaque;

	if (ppd->lflags & QIBL_LINKACTIVE)
		return;

	ppd->ibport_data.z_symbol_error_counter =
		ppd->dd->f_portcntr(ppd, QIBPORTCNTR_IBSYMBOLERR);
}

/*
 * Handle receive interrupts for user ctxts; this means a user
 * process was waiting for a packet to arrive, and didn't want
 * to poll.
 */
void qib_handle_urcv(struct qib_devdata *dd, u64 ctxtr)
{
	struct qib_ctxtdata *rcd;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&dd->uctxt_lock, flags);
	for (i = dd->first_user_ctxt; dd->rcd && i < dd->cfgctxts; i++) {
		if (!(ctxtr & (1ULL << i)))
			continue;
		rcd = dd->rcd[i];
		if (!rcd || !rcd->cnt)
			continue;

		if (test_and_clear_bit(QIB_CTXT_WAITING_RCV, &rcd->flag)) {
			wake_up_interruptible(&rcd->wait);
			dd->f_rcvctrl(rcd->ppd, QIB_RCVCTRL_INTRAVAIL_DIS,
				      rcd->ctxt);
		} else if (test_and_clear_bit(QIB_CTXT_WAITING_URG,
					      &rcd->flag)) {
			rcd->urgent++;
			wake_up_interruptible(&rcd->wait);
		}
	}
	spin_unlock_irqrestore(&dd->uctxt_lock, flags);
}

void qib_bad_intrstatus(struct qib_devdata *dd)
{
	static int allbits;

	/* separate routine, for better optimization of qib_intr() */

	/*
	 * We print the message and disable interrupts, in hope of
	 * having a better chance of debugging the problem.
	 */
	qib_dev_err(dd, "Read of chip interrupt status failed"
		    " disabling interrupts\n");
	if (allbits++) {
		/* disable interrupt delivery, something is very wrong */
		if (allbits == 2)
			dd->f_set_intr_state(dd, 0);
		if (allbits == 3) {
			qib_dev_err(dd, "2nd bad interrupt status, "
				    "unregistering interrupts\n");
			dd->flags |= QIB_BADINTR;
			dd->flags &= ~QIB_INITTED;
			dd->f_free_irq(dd);
		}
	}
}
