/*
 * Copyright (c) 2006, 2007, 2008 QLogic Corporation. All rights reserved.
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
#include <linux/sched.h>

#include "ipath_kernel.h"
#include "ipath_verbs.h"
#include "ipath_common.h"


/*
 * Called when we might have an error that is specific to a particular
 * PIO buffer, and may need to cancel that buffer, so it can be re-used.
 */
void ipath_disarm_senderrbufs(struct ipath_devdata *dd)
{
	u32 piobcnt;
	unsigned long sbuf[4];
	/*
	 * it's possible that sendbuffererror could have bits set; might
	 * have already done this as a result of hardware error handling
	 */
	piobcnt = dd->ipath_piobcnt2k + dd->ipath_piobcnt4k;
	/* read these before writing errorclear */
	sbuf[0] = ipath_read_kreg64(
		dd, dd->ipath_kregs->kr_sendbuffererror);
	sbuf[1] = ipath_read_kreg64(
		dd, dd->ipath_kregs->kr_sendbuffererror + 1);
	if (piobcnt > 128)
		sbuf[2] = ipath_read_kreg64(
			dd, dd->ipath_kregs->kr_sendbuffererror + 2);
	if (piobcnt > 192)
		sbuf[3] = ipath_read_kreg64(
			dd, dd->ipath_kregs->kr_sendbuffererror + 3);
	else
		sbuf[3] = 0;

	if (sbuf[0] || sbuf[1] || (piobcnt > 128 && (sbuf[2] || sbuf[3]))) {
		int i;
		if (ipath_debug & (__IPATH_PKTDBG|__IPATH_DBG) &&
			time_after(dd->ipath_lastcancel, jiffies)) {
			__IPATH_DBG_WHICH(__IPATH_PKTDBG|__IPATH_DBG,
					  "SendbufErrs %lx %lx", sbuf[0],
					  sbuf[1]);
			if (ipath_debug & __IPATH_PKTDBG && piobcnt > 128)
				printk(" %lx %lx ", sbuf[2], sbuf[3]);
			printk("\n");
		}

		for (i = 0; i < piobcnt; i++)
			if (test_bit(i, sbuf))
				ipath_disarm_piobufs(dd, i, 1);
		/* ignore armlaunch errs for a bit */
		dd->ipath_lastcancel = jiffies+3;
	}
}


/* These are all rcv-related errors which we want to count for stats */
#define E_SUM_PKTERRS \
	(INFINIPATH_E_RHDRLEN | INFINIPATH_E_RBADTID | \
	 INFINIPATH_E_RBADVERSION | INFINIPATH_E_RHDR | \
	 INFINIPATH_E_RLONGPKTLEN | INFINIPATH_E_RSHORTPKTLEN | \
	 INFINIPATH_E_RMAXPKTLEN | INFINIPATH_E_RMINPKTLEN | \
	 INFINIPATH_E_RFORMATERR | INFINIPATH_E_RUNSUPVL | \
	 INFINIPATH_E_RUNEXPCHAR | INFINIPATH_E_REBP)

/* These are all send-related errors which we want to count for stats */
#define E_SUM_ERRS \
	(INFINIPATH_E_SPIOARMLAUNCH | INFINIPATH_E_SUNEXPERRPKTNUM | \
	 INFINIPATH_E_SDROPPEDDATAPKT | INFINIPATH_E_SDROPPEDSMPPKT | \
	 INFINIPATH_E_SMAXPKTLEN | INFINIPATH_E_SUNSUPVL | \
	 INFINIPATH_E_SMINPKTLEN | INFINIPATH_E_SPKTLEN | \
	 INFINIPATH_E_INVALIDADDR)

/*
 * this is similar to E_SUM_ERRS, but can't ignore armlaunch, don't ignore
 * errors not related to freeze and cancelling buffers.  Can't ignore
 * armlaunch because could get more while still cleaning up, and need
 * to cancel those as they happen.
 */
#define E_SPKT_ERRS_IGNORE \
	 (INFINIPATH_E_SDROPPEDDATAPKT | INFINIPATH_E_SDROPPEDSMPPKT | \
	 INFINIPATH_E_SMAXPKTLEN | INFINIPATH_E_SMINPKTLEN | \
	 INFINIPATH_E_SPKTLEN)

/*
 * these are errors that can occur when the link changes state while
 * a packet is being sent or received.  This doesn't cover things
 * like EBP or VCRC that can be the result of a sending having the
 * link change state, so we receive a "known bad" packet.
 */
#define E_SUM_LINK_PKTERRS \
	(INFINIPATH_E_SDROPPEDDATAPKT | INFINIPATH_E_SDROPPEDSMPPKT | \
	 INFINIPATH_E_SMINPKTLEN | INFINIPATH_E_SPKTLEN | \
	 INFINIPATH_E_RSHORTPKTLEN | INFINIPATH_E_RMINPKTLEN | \
	 INFINIPATH_E_RUNEXPCHAR)

static u64 handle_e_sum_errs(struct ipath_devdata *dd, ipath_err_t errs)
{
	u64 ignore_this_time = 0;

	ipath_disarm_senderrbufs(dd);
	if ((errs & E_SUM_LINK_PKTERRS) &&
	    !(dd->ipath_flags & IPATH_LINKACTIVE)) {
		/*
		 * This can happen when SMA is trying to bring the link
		 * up, but the IB link changes state at the "wrong" time.
		 * The IB logic then complains that the packet isn't
		 * valid.  We don't want to confuse people, so we just
		 * don't print them, except at debug
		 */
		ipath_dbg("Ignoring packet errors %llx, because link not "
			  "ACTIVE\n", (unsigned long long) errs);
		ignore_this_time = errs & E_SUM_LINK_PKTERRS;
	}

	return ignore_this_time;
}

/* generic hw error messages... */
#define INFINIPATH_HWE_TXEMEMPARITYERR_MSG(a) \
	{ \
		.mask = ( INFINIPATH_HWE_TXEMEMPARITYERR_##a <<    \
			  INFINIPATH_HWE_TXEMEMPARITYERR_SHIFT ),   \
		.msg = "TXE " #a " Memory Parity"	     \
	}
#define INFINIPATH_HWE_RXEMEMPARITYERR_MSG(a) \
	{ \
		.mask = ( INFINIPATH_HWE_RXEMEMPARITYERR_##a <<    \
			  INFINIPATH_HWE_RXEMEMPARITYERR_SHIFT ),   \
		.msg = "RXE " #a " Memory Parity"	     \
	}

static const struct ipath_hwerror_msgs ipath_generic_hwerror_msgs[] = {
	INFINIPATH_HWE_MSG(IBCBUSFRSPCPARITYERR, "IPATH2IB Parity"),
	INFINIPATH_HWE_MSG(IBCBUSTOSPCPARITYERR, "IB2IPATH Parity"),

	INFINIPATH_HWE_TXEMEMPARITYERR_MSG(PIOBUF),
	INFINIPATH_HWE_TXEMEMPARITYERR_MSG(PIOPBC),
	INFINIPATH_HWE_TXEMEMPARITYERR_MSG(PIOLAUNCHFIFO),

	INFINIPATH_HWE_RXEMEMPARITYERR_MSG(RCVBUF),
	INFINIPATH_HWE_RXEMEMPARITYERR_MSG(LOOKUPQ),
	INFINIPATH_HWE_RXEMEMPARITYERR_MSG(EAGERTID),
	INFINIPATH_HWE_RXEMEMPARITYERR_MSG(EXPTID),
	INFINIPATH_HWE_RXEMEMPARITYERR_MSG(FLAGBUF),
	INFINIPATH_HWE_RXEMEMPARITYERR_MSG(DATAINFO),
	INFINIPATH_HWE_RXEMEMPARITYERR_MSG(HDRINFO),
};

/**
 * ipath_format_hwmsg - format a single hwerror message
 * @msg message buffer
 * @msgl length of message buffer
 * @hwmsg message to add to message buffer
 */
static void ipath_format_hwmsg(char *msg, size_t msgl, const char *hwmsg)
{
	strlcat(msg, "[", msgl);
	strlcat(msg, hwmsg, msgl);
	strlcat(msg, "]", msgl);
}

/**
 * ipath_format_hwerrors - format hardware error messages for display
 * @hwerrs hardware errors bit vector
 * @hwerrmsgs hardware error descriptions
 * @nhwerrmsgs number of hwerrmsgs
 * @msg message buffer
 * @msgl message buffer length
 */
void ipath_format_hwerrors(u64 hwerrs,
			   const struct ipath_hwerror_msgs *hwerrmsgs,
			   size_t nhwerrmsgs,
			   char *msg, size_t msgl)
{
	int i;
	const int glen =
	    ARRAY_SIZE(ipath_generic_hwerror_msgs);

	for (i=0; i<glen; i++) {
		if (hwerrs & ipath_generic_hwerror_msgs[i].mask) {
			ipath_format_hwmsg(msg, msgl,
					   ipath_generic_hwerror_msgs[i].msg);
		}
	}

	for (i=0; i<nhwerrmsgs; i++) {
		if (hwerrs & hwerrmsgs[i].mask) {
			ipath_format_hwmsg(msg, msgl, hwerrmsgs[i].msg);
		}
	}
}

/* return the strings for the most common link states */
static char *ib_linkstate(struct ipath_devdata *dd, u64 ibcs)
{
	char *ret;
	u32 state;

	state = ipath_ib_state(dd, ibcs);
	if (state == dd->ib_init)
		ret = "Init";
	else if (state == dd->ib_arm)
		ret = "Arm";
	else if (state == dd->ib_active)
		ret = "Active";
	else
		ret = "Down";
	return ret;
}

void signal_ib_event(struct ipath_devdata *dd, enum ib_event_type ev)
{
	struct ib_event event;

	event.device = &dd->verbs_dev->ibdev;
	event.element.port_num = 1;
	event.event = ev;
	ib_dispatch_event(&event);
}

static void handle_e_ibstatuschanged(struct ipath_devdata *dd,
				     ipath_err_t errs)
{
	u32 ltstate, lstate, ibstate, lastlstate;
	u32 init = dd->ib_init;
	u32 arm = dd->ib_arm;
	u32 active = dd->ib_active;
	const u64 ibcs = ipath_read_kreg64(dd, dd->ipath_kregs->kr_ibcstatus);

	lstate = ipath_ib_linkstate(dd, ibcs); /* linkstate */
	ibstate = ipath_ib_state(dd, ibcs);
	/* linkstate at last interrupt */
	lastlstate = ipath_ib_linkstate(dd, dd->ipath_lastibcstat);
	ltstate = ipath_ib_linktrstate(dd, ibcs); /* linktrainingtate */

	/*
	 * Since going into a recovery state causes the link state to go
	 * down and since recovery is transitory, it is better if we "miss"
	 * ever seeing the link training state go into recovery (i.e.,
	 * ignore this transition for link state special handling purposes)
	 * without even updating ipath_lastibcstat.
	 */
	if ((ltstate == INFINIPATH_IBCS_LT_STATE_RECOVERRETRAIN) ||
	    (ltstate == INFINIPATH_IBCS_LT_STATE_RECOVERWAITRMT) ||
	    (ltstate == INFINIPATH_IBCS_LT_STATE_RECOVERIDLE))
		goto done;

	/*
	 * if linkstate transitions into INIT from any of the various down
	 * states, or if it transitions from any of the up (INIT or better)
	 * states into any of the down states (except link recovery), then
	 * call the chip-specific code to take appropriate actions.
	 */
	if (lstate >= INFINIPATH_IBCS_L_STATE_INIT &&
		lastlstate == INFINIPATH_IBCS_L_STATE_DOWN) {
		/* transitioned to UP */
		if (dd->ipath_f_ib_updown(dd, 1, ibcs)) {
			/* link came up, so we must no longer be disabled */
			dd->ipath_flags &= ~IPATH_IB_LINK_DISABLED;
			ipath_cdbg(LINKVERB, "LinkUp handled, skipped\n");
			goto skip_ibchange; /* chip-code handled */
		}
	} else if ((lastlstate >= INFINIPATH_IBCS_L_STATE_INIT ||
		(dd->ipath_flags & IPATH_IB_FORCE_NOTIFY)) &&
		ltstate <= INFINIPATH_IBCS_LT_STATE_CFGWAITRMT &&
		ltstate != INFINIPATH_IBCS_LT_STATE_LINKUP) {
		int handled;
		handled = dd->ipath_f_ib_updown(dd, 0, ibcs);
		dd->ipath_flags &= ~IPATH_IB_FORCE_NOTIFY;
		if (handled) {
			ipath_cdbg(LINKVERB, "LinkDown handled, skipped\n");
			goto skip_ibchange; /* chip-code handled */
		}
	}

	/*
	 * Significant enough to always print and get into logs, if it was
	 * unexpected.  If it was a requested state change, we'll have
	 * already cleared the flags, so we won't print this warning
	 */
	if ((ibstate != arm && ibstate != active) &&
	    (dd->ipath_flags & (IPATH_LINKARMED | IPATH_LINKACTIVE))) {
		dev_info(&dd->pcidev->dev, "Link state changed from %s "
			 "to %s\n", (dd->ipath_flags & IPATH_LINKARMED) ?
			 "ARM" : "ACTIVE", ib_linkstate(dd, ibcs));
	}

	if (ltstate == INFINIPATH_IBCS_LT_STATE_POLLACTIVE ||
	    ltstate == INFINIPATH_IBCS_LT_STATE_POLLQUIET) {
		u32 lastlts;
		lastlts = ipath_ib_linktrstate(dd, dd->ipath_lastibcstat);
		/*
		 * Ignore cycling back and forth from Polling.Active to
		 * Polling.Quiet while waiting for the other end of the link
		 * to come up, except to try and decide if we are connected
		 * to a live IB device or not.  We will cycle back and
		 * forth between them if no cable is plugged in, the other
		 * device is powered off or disabled, etc.
		 */
		if (lastlts == INFINIPATH_IBCS_LT_STATE_POLLACTIVE ||
		    lastlts == INFINIPATH_IBCS_LT_STATE_POLLQUIET) {
			if (!(dd->ipath_flags & IPATH_IB_AUTONEG_INPROG) &&
			     (++dd->ipath_ibpollcnt == 40)) {
				dd->ipath_flags |= IPATH_NOCABLE;
				*dd->ipath_statusp |=
					IPATH_STATUS_IB_NOCABLE;
				ipath_cdbg(LINKVERB, "Set NOCABLE\n");
			}
			ipath_cdbg(LINKVERB, "POLL change to %s (%x)\n",
				ipath_ibcstatus_str[ltstate], ibstate);
			goto skip_ibchange;
		}
	}

	dd->ipath_ibpollcnt = 0; /* not poll*, now */
	ipath_stats.sps_iblink++;

	if (ibstate != init && dd->ipath_lastlinkrecov && ipath_linkrecovery) {
		u64 linkrecov;
		linkrecov = ipath_snap_cntr(dd,
			dd->ipath_cregs->cr_iblinkerrrecovcnt);
		if (linkrecov != dd->ipath_lastlinkrecov) {
			ipath_dbg("IB linkrecov up %Lx (%s %s) recov %Lu\n",
				(unsigned long long) ibcs,
				ib_linkstate(dd, ibcs),
				ipath_ibcstatus_str[ltstate],
				(unsigned long long) linkrecov);
			/* and no more until active again */
			dd->ipath_lastlinkrecov = 0;
			ipath_set_linkstate(dd, IPATH_IB_LINKDOWN);
			goto skip_ibchange;
		}
	}

	if (ibstate == init || ibstate == arm || ibstate == active) {
		*dd->ipath_statusp &= ~IPATH_STATUS_IB_NOCABLE;
		if (ibstate == init || ibstate == arm) {
			*dd->ipath_statusp &= ~IPATH_STATUS_IB_READY;
			if (dd->ipath_flags & IPATH_LINKACTIVE)
				signal_ib_event(dd, IB_EVENT_PORT_ERR);
		}
		if (ibstate == arm) {
			dd->ipath_flags |= IPATH_LINKARMED;
			dd->ipath_flags &= ~(IPATH_LINKUNK |
				IPATH_LINKINIT | IPATH_LINKDOWN |
				IPATH_LINKACTIVE | IPATH_NOCABLE);
			ipath_hol_down(dd);
		} else  if (ibstate == init) {
			/*
			 * set INIT and DOWN.  Down is checked by
			 * most of the other code, but INIT is
			 * useful to know in a few places.
			 */
			dd->ipath_flags |= IPATH_LINKINIT |
				IPATH_LINKDOWN;
			dd->ipath_flags &= ~(IPATH_LINKUNK |
				IPATH_LINKARMED | IPATH_LINKACTIVE |
				IPATH_NOCABLE);
			ipath_hol_down(dd);
		} else {  /* active */
			dd->ipath_lastlinkrecov = ipath_snap_cntr(dd,
				dd->ipath_cregs->cr_iblinkerrrecovcnt);
			*dd->ipath_statusp |=
				IPATH_STATUS_IB_READY | IPATH_STATUS_IB_CONF;
			dd->ipath_flags |= IPATH_LINKACTIVE;
			dd->ipath_flags &= ~(IPATH_LINKUNK | IPATH_LINKINIT
				| IPATH_LINKDOWN | IPATH_LINKARMED |
				IPATH_NOCABLE);
			if (dd->ipath_flags & IPATH_HAS_SEND_DMA)
				ipath_restart_sdma(dd);
			signal_ib_event(dd, IB_EVENT_PORT_ACTIVE);
			/* LED active not handled in chip _f_updown */
			dd->ipath_f_setextled(dd, lstate, ltstate);
			ipath_hol_up(dd);
		}

		/*
		 * print after we've already done the work, so as not to
		 * delay the state changes and notifications, for debugging
		 */
		if (lstate == lastlstate)
			ipath_cdbg(LINKVERB, "Unchanged from last: %s "
				"(%x)\n", ib_linkstate(dd, ibcs), ibstate);
		else
			ipath_cdbg(VERBOSE, "Unit %u: link up to %s %s (%x)\n",
				  dd->ipath_unit, ib_linkstate(dd, ibcs),
				  ipath_ibcstatus_str[ltstate],  ibstate);
	} else { /* down */
		if (dd->ipath_flags & IPATH_LINKACTIVE)
			signal_ib_event(dd, IB_EVENT_PORT_ERR);
		dd->ipath_flags |= IPATH_LINKDOWN;
		dd->ipath_flags &= ~(IPATH_LINKUNK | IPATH_LINKINIT
				     | IPATH_LINKACTIVE |
				     IPATH_LINKARMED);
		*dd->ipath_statusp &= ~IPATH_STATUS_IB_READY;
		dd->ipath_lli_counter = 0;

		if (lastlstate != INFINIPATH_IBCS_L_STATE_DOWN)
			ipath_cdbg(VERBOSE, "Unit %u link state down "
				   "(state 0x%x), from %s\n",
				   dd->ipath_unit, lstate,
				   ib_linkstate(dd, dd->ipath_lastibcstat));
		else
			ipath_cdbg(LINKVERB, "Unit %u link state changed "
				   "to %s (0x%x) from down (%x)\n",
				   dd->ipath_unit,
				   ipath_ibcstatus_str[ltstate],
				   ibstate, lastlstate);
	}

skip_ibchange:
	dd->ipath_lastibcstat = ibcs;
done:
	return;
}

static void handle_supp_msgs(struct ipath_devdata *dd,
			     unsigned supp_msgs, char *msg, u32 msgsz)
{
	/*
	 * Print the message unless it's ibc status change only, which
	 * happens so often we never want to count it.
	 */
	if (dd->ipath_lasterror & ~INFINIPATH_E_IBSTATUSCHANGED) {
		int iserr;
		ipath_err_t mask;
		iserr = ipath_decode_err(dd, msg, msgsz,
					 dd->ipath_lasterror &
					 ~INFINIPATH_E_IBSTATUSCHANGED);

		mask = INFINIPATH_E_RRCVEGRFULL | INFINIPATH_E_RRCVHDRFULL |
			INFINIPATH_E_PKTERRS | INFINIPATH_E_SDMADISABLED;

		/* if we're in debug, then don't mask SDMADISABLED msgs */
		if (ipath_debug & __IPATH_DBG)
			mask &= ~INFINIPATH_E_SDMADISABLED;

		if (dd->ipath_lasterror & ~mask)
			ipath_dev_err(dd, "Suppressed %u messages for "
				      "fast-repeating errors (%s) (%llx)\n",
				      supp_msgs, msg,
				      (unsigned long long)
				      dd->ipath_lasterror);
		else {
			/*
			 * rcvegrfull and rcvhdrqfull are "normal", for some
			 * types of processes (mostly benchmarks) that send
			 * huge numbers of messages, while not processing
			 * them. So only complain about these at debug
			 * level.
			 */
			if (iserr)
				ipath_dbg("Suppressed %u messages for %s\n",
					  supp_msgs, msg);
			else
				ipath_cdbg(ERRPKT,
					"Suppressed %u messages for %s\n",
					  supp_msgs, msg);
		}
	}
}

static unsigned handle_frequent_errors(struct ipath_devdata *dd,
				       ipath_err_t errs, char *msg,
				       u32 msgsz, int *noprint)
{
	unsigned long nc;
	static unsigned long nextmsg_time;
	static unsigned nmsgs, supp_msgs;

	/*
	 * Throttle back "fast" messages to no more than 10 per 5 seconds.
	 * This isn't perfect, but it's a reasonable heuristic. If we get
	 * more than 10, give a 6x longer delay.
	 */
	nc = jiffies;
	if (nmsgs > 10) {
		if (time_before(nc, nextmsg_time)) {
			*noprint = 1;
			if (!supp_msgs++)
				nextmsg_time = nc + HZ * 3;
		}
		else if (supp_msgs) {
			handle_supp_msgs(dd, supp_msgs, msg, msgsz);
			supp_msgs = 0;
			nmsgs = 0;
		}
	}
	else if (!nmsgs++ || time_after(nc, nextmsg_time))
		nextmsg_time = nc + HZ / 2;

	return supp_msgs;
}

static void handle_sdma_errors(struct ipath_devdata *dd, ipath_err_t errs)
{
	unsigned long flags;
	int expected;

	if (ipath_debug & __IPATH_DBG) {
		char msg[128];
		ipath_decode_err(dd, msg, sizeof msg, errs &
			INFINIPATH_E_SDMAERRS);
		ipath_dbg("errors %lx (%s)\n", (unsigned long)errs, msg);
	}
	if (ipath_debug & __IPATH_VERBDBG) {
		unsigned long tl, hd, status, lengen;
		tl = ipath_read_kreg64(dd, dd->ipath_kregs->kr_senddmatail);
		hd = ipath_read_kreg64(dd, dd->ipath_kregs->kr_senddmahead);
		status = ipath_read_kreg64(dd
			, dd->ipath_kregs->kr_senddmastatus);
		lengen = ipath_read_kreg64(dd,
			dd->ipath_kregs->kr_senddmalengen);
		ipath_cdbg(VERBOSE, "sdma tl 0x%lx hd 0x%lx status 0x%lx "
			"lengen 0x%lx\n", tl, hd, status, lengen);
	}

	spin_lock_irqsave(&dd->ipath_sdma_lock, flags);
	__set_bit(IPATH_SDMA_DISABLED, &dd->ipath_sdma_status);
	expected = test_bit(IPATH_SDMA_ABORTING, &dd->ipath_sdma_status);
	spin_unlock_irqrestore(&dd->ipath_sdma_lock, flags);
	if (!expected)
		ipath_cancel_sends(dd, 1);
}

static void handle_sdma_intr(struct ipath_devdata *dd, u64 istat)
{
	unsigned long flags;
	int expected;

	if ((istat & INFINIPATH_I_SDMAINT) &&
	    !test_bit(IPATH_SDMA_SHUTDOWN, &dd->ipath_sdma_status))
		ipath_sdma_intr(dd);

	if (istat & INFINIPATH_I_SDMADISABLED) {
		expected = test_bit(IPATH_SDMA_ABORTING,
			&dd->ipath_sdma_status);
		ipath_dbg("%s SDmaDisabled intr\n",
			expected ? "expected" : "unexpected");
		spin_lock_irqsave(&dd->ipath_sdma_lock, flags);
		__set_bit(IPATH_SDMA_DISABLED, &dd->ipath_sdma_status);
		spin_unlock_irqrestore(&dd->ipath_sdma_lock, flags);
		if (!expected)
			ipath_cancel_sends(dd, 1);
		if (!test_bit(IPATH_SDMA_SHUTDOWN, &dd->ipath_sdma_status))
			tasklet_hi_schedule(&dd->ipath_sdma_abort_task);
	}
}

static int handle_hdrq_full(struct ipath_devdata *dd)
{
	int chkerrpkts = 0;
	u32 hd, tl;
	u32 i;

	ipath_stats.sps_hdrqfull++;
	for (i = 0; i < dd->ipath_cfgports; i++) {
		struct ipath_portdata *pd = dd->ipath_pd[i];

		if (i == 0) {
			/*
			 * For kernel receive queues, we just want to know
			 * if there are packets in the queue that we can
			 * process.
			 */
			if (pd->port_head != ipath_get_hdrqtail(pd))
				chkerrpkts |= 1 << i;
			continue;
		}

		/* Skip if user context is not open */
		if (!pd || !pd->port_cnt)
			continue;

		/* Don't report the same point multiple times. */
		if (dd->ipath_flags & IPATH_NODMA_RTAIL)
			tl = ipath_read_ureg32(dd, ur_rcvhdrtail, i);
		else
			tl = ipath_get_rcvhdrtail(pd);
		if (tl == pd->port_lastrcvhdrqtail)
			continue;

		hd = ipath_read_ureg32(dd, ur_rcvhdrhead, i);
		if (hd == (tl + 1) || (!hd && tl == dd->ipath_hdrqlast)) {
			pd->port_lastrcvhdrqtail = tl;
			pd->port_hdrqfull++;
			/* flush hdrqfull so that poll() sees it */
			wmb();
			wake_up_interruptible(&pd->port_wait);
		}
	}

	return chkerrpkts;
}

static int handle_errors(struct ipath_devdata *dd, ipath_err_t errs)
{
	char msg[128];
	u64 ignore_this_time = 0;
	u64 iserr = 0;
	int chkerrpkts = 0, noprint = 0;
	unsigned supp_msgs;
	int log_idx;

	/*
	 * don't report errors that are masked, either at init
	 * (not set in ipath_errormask), or temporarily (set in
	 * ipath_maskederrs)
	 */
	errs &= dd->ipath_errormask & ~dd->ipath_maskederrs;

	supp_msgs = handle_frequent_errors(dd, errs, msg, (u32)sizeof msg,
		&noprint);

	/* do these first, they are most important */
	if (errs & INFINIPATH_E_HARDWARE) {
		/* reuse same msg buf */
		dd->ipath_f_handle_hwerrors(dd, msg, sizeof msg);
	} else {
		u64 mask;
		for (log_idx = 0; log_idx < IPATH_EEP_LOG_CNT; ++log_idx) {
			mask = dd->ipath_eep_st_masks[log_idx].errs_to_log;
			if (errs & mask)
				ipath_inc_eeprom_err(dd, log_idx, 1);
		}
	}

	if (errs & INFINIPATH_E_SDMAERRS)
		handle_sdma_errors(dd, errs);

	if (!noprint && (errs & ~dd->ipath_e_bitsextant))
		ipath_dev_err(dd, "error interrupt with unknown errors "
			      "%llx set\n", (unsigned long long)
			      (errs & ~dd->ipath_e_bitsextant));

	if (errs & E_SUM_ERRS)
		ignore_this_time = handle_e_sum_errs(dd, errs);
	else if ((errs & E_SUM_LINK_PKTERRS) &&
	    !(dd->ipath_flags & IPATH_LINKACTIVE)) {
		/*
		 * This can happen when SMA is trying to bring the link
		 * up, but the IB link changes state at the "wrong" time.
		 * The IB logic then complains that the packet isn't
		 * valid.  We don't want to confuse people, so we just
		 * don't print them, except at debug
		 */
		ipath_dbg("Ignoring packet errors %llx, because link not "
			  "ACTIVE\n", (unsigned long long) errs);
		ignore_this_time = errs & E_SUM_LINK_PKTERRS;
	}

	if (supp_msgs == 250000) {
		int s_iserr;
		/*
		 * It's not entirely reasonable assuming that the errors set
		 * in the last clear period are all responsible for the
		 * problem, but the alternative is to assume it's the only
		 * ones on this particular interrupt, which also isn't great
		 */
		dd->ipath_maskederrs |= dd->ipath_lasterror | errs;

		dd->ipath_errormask &= ~dd->ipath_maskederrs;
		ipath_write_kreg(dd, dd->ipath_kregs->kr_errormask,
				 dd->ipath_errormask);
		s_iserr = ipath_decode_err(dd, msg, sizeof msg,
					   dd->ipath_maskederrs);

		if (dd->ipath_maskederrs &
		    ~(INFINIPATH_E_RRCVEGRFULL |
		      INFINIPATH_E_RRCVHDRFULL | INFINIPATH_E_PKTERRS))
			ipath_dev_err(dd, "Temporarily disabling "
			    "error(s) %llx reporting; too frequent (%s)\n",
				(unsigned long long) dd->ipath_maskederrs,
				msg);
		else {
			/*
			 * rcvegrfull and rcvhdrqfull are "normal",
			 * for some types of processes (mostly benchmarks)
			 * that send huge numbers of messages, while not
			 * processing them.  So only complain about
			 * these at debug level.
			 */
			if (s_iserr)
				ipath_dbg("Temporarily disabling reporting "
				    "too frequent queue full errors (%s)\n",
				    msg);
			else
				ipath_cdbg(ERRPKT,
				    "Temporarily disabling reporting too"
				    " frequent packet errors (%s)\n",
				    msg);
		}

		/*
		 * Re-enable the masked errors after around 3 minutes.  in
		 * ipath_get_faststats().  If we have a series of fast
		 * repeating but different errors, the interval will keep
		 * stretching out, but that's OK, as that's pretty
		 * catastrophic.
		 */
		dd->ipath_unmasktime = jiffies + HZ * 180;
	}

	ipath_write_kreg(dd, dd->ipath_kregs->kr_errorclear, errs);
	if (ignore_this_time)
		errs &= ~ignore_this_time;
	if (errs & ~dd->ipath_lasterror) {
		errs &= ~dd->ipath_lasterror;
		/* never suppress duplicate hwerrors or ibstatuschange */
		dd->ipath_lasterror |= errs &
			~(INFINIPATH_E_HARDWARE |
			  INFINIPATH_E_IBSTATUSCHANGED);
	}

	if (errs & INFINIPATH_E_SENDSPECIALTRIGGER) {
		dd->ipath_spectriggerhit++;
		ipath_dbg("%lu special trigger hits\n",
			dd->ipath_spectriggerhit);
	}

	/* likely due to cancel; so suppress message unless verbose */
	if ((errs & (INFINIPATH_E_SPKTLEN | INFINIPATH_E_SPIOARMLAUNCH)) &&
		time_after(dd->ipath_lastcancel, jiffies)) {
		/* armlaunch takes precedence; it often causes both. */
		ipath_cdbg(VERBOSE,
			"Suppressed %s error (%llx) after sendbuf cancel\n",
			(errs &  INFINIPATH_E_SPIOARMLAUNCH) ?
			"armlaunch" : "sendpktlen", (unsigned long long)errs);
		errs &= ~(INFINIPATH_E_SPIOARMLAUNCH | INFINIPATH_E_SPKTLEN);
	}

	if (!errs)
		return 0;

	if (!noprint) {
		ipath_err_t mask;
		/*
		 * The ones we mask off are handled specially below
		 * or above.  Also mask SDMADISABLED by default as it
		 * is too chatty.
		 */
		mask = INFINIPATH_E_IBSTATUSCHANGED |
			INFINIPATH_E_RRCVEGRFULL | INFINIPATH_E_RRCVHDRFULL |
			INFINIPATH_E_HARDWARE | INFINIPATH_E_SDMADISABLED;

		/* if we're in debug, then don't mask SDMADISABLED msgs */
		if (ipath_debug & __IPATH_DBG)
			mask &= ~INFINIPATH_E_SDMADISABLED;

		ipath_decode_err(dd, msg, sizeof msg, errs & ~mask);
	} else
		/* so we don't need if (!noprint) at strlcat's below */
		*msg = 0;

	if (errs & E_SUM_PKTERRS) {
		ipath_stats.sps_pkterrs++;
		chkerrpkts = 1;
	}
	if (errs & E_SUM_ERRS)
		ipath_stats.sps_errs++;

	if (errs & (INFINIPATH_E_RICRC | INFINIPATH_E_RVCRC)) {
		ipath_stats.sps_crcerrs++;
		chkerrpkts = 1;
	}
	iserr = errs & ~(E_SUM_PKTERRS | INFINIPATH_E_PKTERRS);


	/*
	 * We don't want to print these two as they happen, or we can make
	 * the situation even worse, because it takes so long to print
	 * messages to serial consoles.  Kernel ports get printed from
	 * fast_stats, no more than every 5 seconds, user ports get printed
	 * on close
	 */
	if (errs & INFINIPATH_E_RRCVHDRFULL)
		chkerrpkts |= handle_hdrq_full(dd);
	if (errs & INFINIPATH_E_RRCVEGRFULL) {
		struct ipath_portdata *pd = dd->ipath_pd[0];

		/*
		 * since this is of less importance and not likely to
		 * happen without also getting hdrfull, only count
		 * occurrences; don't check each port (or even the kernel
		 * vs user)
		 */
		ipath_stats.sps_etidfull++;
		if (pd->port_head != ipath_get_hdrqtail(pd))
			chkerrpkts |= 1;
	}

	/*
	 * do this before IBSTATUSCHANGED, in case both bits set in a single
	 * interrupt; we want the STATUSCHANGE to "win", so we do our
	 * internal copy of state machine correctly
	 */
	if (errs & INFINIPATH_E_RIBLOSTLINK) {
		/*
		 * force through block below
		 */
		errs |= INFINIPATH_E_IBSTATUSCHANGED;
		ipath_stats.sps_iblink++;
		dd->ipath_flags |= IPATH_LINKDOWN;
		dd->ipath_flags &= ~(IPATH_LINKUNK | IPATH_LINKINIT
				     | IPATH_LINKARMED | IPATH_LINKACTIVE);
		*dd->ipath_statusp &= ~IPATH_STATUS_IB_READY;

		ipath_dbg("Lost link, link now down (%s)\n",
			ipath_ibcstatus_str[ipath_read_kreg64(dd,
			dd->ipath_kregs->kr_ibcstatus) & 0xf]);
	}
	if (errs & INFINIPATH_E_IBSTATUSCHANGED)
		handle_e_ibstatuschanged(dd, errs);

	if (errs & INFINIPATH_E_RESET) {
		if (!noprint)
			ipath_dev_err(dd, "Got reset, requires re-init "
				      "(unload and reload driver)\n");
		dd->ipath_flags &= ~IPATH_INITTED;	/* needs re-init */
		/* mark as having had error */
		*dd->ipath_statusp |= IPATH_STATUS_HWERROR;
		*dd->ipath_statusp &= ~IPATH_STATUS_IB_CONF;
	}

	if (!noprint && *msg) {
		if (iserr)
			ipath_dev_err(dd, "%s error\n", msg);
	}
	if (dd->ipath_state_wanted & dd->ipath_flags) {
		ipath_cdbg(VERBOSE, "driver wanted state %x, iflags now %x, "
			   "waking\n", dd->ipath_state_wanted,
			   dd->ipath_flags);
		wake_up_interruptible(&ipath_state_wait);
	}

	return chkerrpkts;
}

/*
 * try to cleanup as much as possible for anything that might have gone
 * wrong while in freeze mode, such as pio buffers being written by user
 * processes (causing armlaunch), send errors due to going into freeze mode,
 * etc., and try to avoid causing extra interrupts while doing so.
 * Forcibly update the in-memory pioavail register copies after cleanup
 * because the chip won't do it while in freeze mode (the register values
 * themselves are kept correct).
 * Make sure that we don't lose any important interrupts by using the chip
 * feature that says that writing 0 to a bit in *clear that is set in
 * *status will cause an interrupt to be generated again (if allowed by
 * the *mask value).
 */
void ipath_clear_freeze(struct ipath_devdata *dd)
{
	/* disable error interrupts, to avoid confusion */
	ipath_write_kreg(dd, dd->ipath_kregs->kr_errormask, 0ULL);

	/* also disable interrupts; errormask is sometimes overwriten */
	ipath_write_kreg(dd, dd->ipath_kregs->kr_intmask, 0ULL);

	ipath_cancel_sends(dd, 1);

	/* clear the freeze, and be sure chip saw it */
	ipath_write_kreg(dd, dd->ipath_kregs->kr_control,
			 dd->ipath_control);
	ipath_read_kreg64(dd, dd->ipath_kregs->kr_scratch);

	/* force in-memory update now we are out of freeze */
	ipath_force_pio_avail_update(dd);

	/*
	 * force new interrupt if any hwerr, error or interrupt bits are
	 * still set, and clear "safe" send packet errors related to freeze
	 * and cancelling sends.  Re-enable error interrupts before possible
	 * force of re-interrupt on pending interrupts.
	 */
	ipath_write_kreg(dd, dd->ipath_kregs->kr_hwerrclear, 0ULL);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_errorclear,
		E_SPKT_ERRS_IGNORE);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_errormask,
		dd->ipath_errormask);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_intmask, -1LL);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_intclear, 0ULL);
}


/* this is separate to allow for better optimization of ipath_intr() */

static noinline void ipath_bad_intr(struct ipath_devdata *dd, u32 *unexpectp)
{
	/*
	 * sometimes happen during driver init and unload, don't want
	 * to process any interrupts at that point
	 */

	/* this is just a bandaid, not a fix, if something goes badly
	 * wrong */
	if (++*unexpectp > 100) {
		if (++*unexpectp > 105) {
			/*
			 * ok, we must be taking somebody else's interrupts,
			 * due to a messed up mptable and/or PIRQ table, so
			 * unregister the interrupt.  We've seen this during
			 * linuxbios development work, and it may happen in
			 * the future again.
			 */
			if (dd->pcidev && dd->ipath_irq) {
				ipath_dev_err(dd, "Now %u unexpected "
					      "interrupts, unregistering "
					      "interrupt handler\n",
					      *unexpectp);
				ipath_dbg("free_irq of irq %d\n",
					  dd->ipath_irq);
				dd->ipath_f_free_irq(dd);
			}
		}
		if (ipath_read_ireg(dd, dd->ipath_kregs->kr_intmask)) {
			ipath_dev_err(dd, "%u unexpected interrupts, "
				      "disabling interrupts completely\n",
				      *unexpectp);
			/*
			 * disable all interrupts, something is very wrong
			 */
			ipath_write_kreg(dd, dd->ipath_kregs->kr_intmask,
					 0ULL);
		}
	} else if (*unexpectp > 1)
		ipath_dbg("Interrupt when not ready, should not happen, "
			  "ignoring\n");
}

static noinline void ipath_bad_regread(struct ipath_devdata *dd)
{
	static int allbits;

	/* separate routine, for better optimization of ipath_intr() */

	/*
	 * We print the message and disable interrupts, in hope of
	 * having a better chance of debugging the problem.
	 */
	ipath_dev_err(dd,
		      "Read of interrupt status failed (all bits set)\n");
	if (allbits++) {
		/* disable all interrupts, something is very wrong */
		ipath_write_kreg(dd, dd->ipath_kregs->kr_intmask, 0ULL);
		if (allbits == 2) {
			ipath_dev_err(dd, "Still bad interrupt status, "
				      "unregistering interrupt\n");
			dd->ipath_f_free_irq(dd);
		} else if (allbits > 2) {
			if ((allbits % 10000) == 0)
				printk(".");
		} else
			ipath_dev_err(dd, "Disabling interrupts, "
				      "multiple errors\n");
	}
}

static void handle_layer_pioavail(struct ipath_devdata *dd)
{
	unsigned long flags;
	int ret;

	ret = ipath_ib_piobufavail(dd->verbs_dev);
	if (ret > 0)
		goto set;

	return;
set:
	spin_lock_irqsave(&dd->ipath_sendctrl_lock, flags);
	dd->ipath_sendctrl |= INFINIPATH_S_PIOINTBUFAVAIL;
	ipath_write_kreg(dd, dd->ipath_kregs->kr_sendctrl,
			 dd->ipath_sendctrl);
	ipath_read_kreg64(dd, dd->ipath_kregs->kr_scratch);
	spin_unlock_irqrestore(&dd->ipath_sendctrl_lock, flags);
}

/*
 * Handle receive interrupts for user ports; this means a user
 * process was waiting for a packet to arrive, and didn't want
 * to poll
 */
static void handle_urcv(struct ipath_devdata *dd, u64 istat)
{
	u64 portr;
	int i;
	int rcvdint = 0;

	/*
	 * test_and_clear_bit(IPATH_PORT_WAITING_RCV) and
	 * test_and_clear_bit(IPATH_PORT_WAITING_URG) below
	 * would both like timely updates of the bits so that
	 * we don't pass them by unnecessarily.  the rmb()
	 * here ensures that we see them promptly -- the
	 * corresponding wmb()'s are in ipath_poll_urgent()
	 * and ipath_poll_next()...
	 */
	rmb();
	portr = ((istat >> dd->ipath_i_rcvavail_shift) &
		 dd->ipath_i_rcvavail_mask) |
		((istat >> dd->ipath_i_rcvurg_shift) &
		 dd->ipath_i_rcvurg_mask);
	for (i = 1; i < dd->ipath_cfgports; i++) {
		struct ipath_portdata *pd = dd->ipath_pd[i];

		if (portr & (1 << i) && pd && pd->port_cnt) {
			if (test_and_clear_bit(IPATH_PORT_WAITING_RCV,
					       &pd->port_flag)) {
				clear_bit(i + dd->ipath_r_intravail_shift,
					  &dd->ipath_rcvctrl);
				wake_up_interruptible(&pd->port_wait);
				rcvdint = 1;
			} else if (test_and_clear_bit(IPATH_PORT_WAITING_URG,
						      &pd->port_flag)) {
				pd->port_urgent++;
				wake_up_interruptible(&pd->port_wait);
			}
		}
	}
	if (rcvdint) {
		/* only want to take one interrupt, so turn off the rcv
		 * interrupt for all the ports that we set the rcv_waiting
		 * (but never for kernel port)
		 */
		ipath_write_kreg(dd, dd->ipath_kregs->kr_rcvctrl,
				 dd->ipath_rcvctrl);
	}
}

irqreturn_t ipath_intr(int irq, void *data)
{
	struct ipath_devdata *dd = data;
	u64 istat, chk0rcv = 0;
	ipath_err_t estat = 0;
	irqreturn_t ret;
	static unsigned unexpected = 0;
	u64 kportrbits;

	ipath_stats.sps_ints++;

	if (dd->ipath_int_counter != (u32) -1)
		dd->ipath_int_counter++;

	if (!(dd->ipath_flags & IPATH_PRESENT)) {
		/*
		 * This return value is not great, but we do not want the
		 * interrupt core code to remove our interrupt handler
		 * because we don't appear to be handling an interrupt
		 * during a chip reset.
		 */
		return IRQ_HANDLED;
	}

	/*
	 * this needs to be flags&initted, not statusp, so we keep
	 * taking interrupts even after link goes down, etc.
	 * Also, we *must* clear the interrupt at some point, or we won't
	 * take it again, which can be real bad for errors, etc...
	 */

	if (!(dd->ipath_flags & IPATH_INITTED)) {
		ipath_bad_intr(dd, &unexpected);
		ret = IRQ_NONE;
		goto bail;
	}

	istat = ipath_read_ireg(dd, dd->ipath_kregs->kr_intstatus);

	if (unlikely(!istat)) {
		ipath_stats.sps_nullintr++;
		ret = IRQ_NONE; /* not our interrupt, or already handled */
		goto bail;
	}
	if (unlikely(istat == -1)) {
		ipath_bad_regread(dd);
		/* don't know if it was our interrupt or not */
		ret = IRQ_NONE;
		goto bail;
	}

	if (unexpected)
		unexpected = 0;

	if (unlikely(istat & ~dd->ipath_i_bitsextant))
		ipath_dev_err(dd,
			      "interrupt with unknown interrupts %Lx set\n",
			      (unsigned long long)
			      istat & ~dd->ipath_i_bitsextant);
	else if (istat & ~INFINIPATH_I_ERROR) /* errors do own printing */
		ipath_cdbg(VERBOSE, "intr stat=0x%Lx\n",
			(unsigned long long) istat);

	if (istat & INFINIPATH_I_ERROR) {
		ipath_stats.sps_errints++;
		estat = ipath_read_kreg64(dd,
					  dd->ipath_kregs->kr_errorstatus);
		if (!estat)
			dev_info(&dd->pcidev->dev, "error interrupt (%Lx), "
				 "but no error bits set!\n",
				 (unsigned long long) istat);
		else if (estat == -1LL)
			/*
			 * should we try clearing all, or hope next read
			 * works?
			 */
			ipath_dev_err(dd, "Read of error status failed "
				      "(all bits set); ignoring\n");
		else
			chk0rcv |= handle_errors(dd, estat);
	}

	if (istat & INFINIPATH_I_GPIO) {
		/*
		 * GPIO interrupts fall in two broad classes:
		 * GPIO_2 indicates (on some HT4xx boards) that a packet
		 *        has arrived for Port 0. Checking for this
		 *        is controlled by flag IPATH_GPIO_INTR.
		 * GPIO_3..5 on IBA6120 Rev2 and IBA6110 Rev4 chips indicate
		 *        errors that we need to count. Checking for this
		 *        is controlled by flag IPATH_GPIO_ERRINTRS.
		 */
		u32 gpiostatus;
		u32 to_clear = 0;

		gpiostatus = ipath_read_kreg32(
			dd, dd->ipath_kregs->kr_gpio_status);
		/* First the error-counter case. */
		if ((gpiostatus & IPATH_GPIO_ERRINTR_MASK) &&
		    (dd->ipath_flags & IPATH_GPIO_ERRINTRS)) {
			/* want to clear the bits we see asserted. */
			to_clear |= (gpiostatus & IPATH_GPIO_ERRINTR_MASK);

			/*
			 * Count appropriately, clear bits out of our copy,
			 * as they have been "handled".
			 */
			if (gpiostatus & (1 << IPATH_GPIO_RXUVL_BIT)) {
				ipath_dbg("FlowCtl on UnsupVL\n");
				dd->ipath_rxfc_unsupvl_errs++;
			}
			if (gpiostatus & (1 << IPATH_GPIO_OVRUN_BIT)) {
				ipath_dbg("Overrun Threshold exceeded\n");
				dd->ipath_overrun_thresh_errs++;
			}
			if (gpiostatus & (1 << IPATH_GPIO_LLI_BIT)) {
				ipath_dbg("Local Link Integrity error\n");
				dd->ipath_lli_errs++;
			}
			gpiostatus &= ~IPATH_GPIO_ERRINTR_MASK;
		}
		/* Now the Port0 Receive case */
		if ((gpiostatus & (1 << IPATH_GPIO_PORT0_BIT)) &&
		    (dd->ipath_flags & IPATH_GPIO_INTR)) {
			/*
			 * GPIO status bit 2 is set, and we expected it.
			 * clear it and indicate in p0bits.
			 * This probably only happens if a Port0 pkt
			 * arrives at _just_ the wrong time, and we
			 * handle that by seting chk0rcv;
			 */
			to_clear |= (1 << IPATH_GPIO_PORT0_BIT);
			gpiostatus &= ~(1 << IPATH_GPIO_PORT0_BIT);
			chk0rcv = 1;
		}
		if (gpiostatus) {
			/*
			 * Some unexpected bits remain. If they could have
			 * caused the interrupt, complain and clear.
			 * To avoid repetition of this condition, also clear
			 * the mask. It is almost certainly due to error.
			 */
			const u32 mask = (u32) dd->ipath_gpio_mask;

			if (mask & gpiostatus) {
				ipath_dbg("Unexpected GPIO IRQ bits %x\n",
				  gpiostatus & mask);
				to_clear |= (gpiostatus & mask);
				dd->ipath_gpio_mask &= ~(gpiostatus & mask);
				ipath_write_kreg(dd,
					dd->ipath_kregs->kr_gpio_mask,
					dd->ipath_gpio_mask);
			}
		}
		if (to_clear) {
			ipath_write_kreg(dd, dd->ipath_kregs->kr_gpio_clear,
					(u64) to_clear);
		}
	}

	/*
	 * Clear the interrupt bits we found set, unless they are receive
	 * related, in which case we already cleared them above, and don't
	 * want to clear them again, because we might lose an interrupt.
	 * Clear it early, so we "know" know the chip will have seen this by
	 * the time we process the queue, and will re-interrupt if necessary.
	 * The processor itself won't take the interrupt again until we return.
	 */
	ipath_write_kreg(dd, dd->ipath_kregs->kr_intclear, istat);

	/*
	 * Handle kernel receive queues before checking for pio buffers
	 * available since receives can overflow; piobuf waiters can afford
	 * a few extra cycles, since they were waiting anyway, and user's
	 * waiting for receive are at the bottom.
	 */
	kportrbits = (1ULL << dd->ipath_i_rcvavail_shift) |
		(1ULL << dd->ipath_i_rcvurg_shift);
	if (chk0rcv || (istat & kportrbits)) {
		istat &= ~kportrbits;
		ipath_kreceive(dd->ipath_pd[0]);
	}

	if (istat & ((dd->ipath_i_rcvavail_mask << dd->ipath_i_rcvavail_shift) |
		     (dd->ipath_i_rcvurg_mask << dd->ipath_i_rcvurg_shift)))
		handle_urcv(dd, istat);

	if (istat & (INFINIPATH_I_SDMAINT | INFINIPATH_I_SDMADISABLED))
		handle_sdma_intr(dd, istat);

	if (istat & INFINIPATH_I_SPIOBUFAVAIL) {
		unsigned long flags;

		spin_lock_irqsave(&dd->ipath_sendctrl_lock, flags);
		dd->ipath_sendctrl &= ~INFINIPATH_S_PIOINTBUFAVAIL;
		ipath_write_kreg(dd, dd->ipath_kregs->kr_sendctrl,
				 dd->ipath_sendctrl);
		ipath_read_kreg64(dd, dd->ipath_kregs->kr_scratch);
		spin_unlock_irqrestore(&dd->ipath_sendctrl_lock, flags);

		/* always process; sdma verbs uses PIO for acks and VL15  */
		handle_layer_pioavail(dd);
	}

	ret = IRQ_HANDLED;

bail:
	return ret;
}
