/*
 * Copyright (c) 2006 QLogic, Inc. All rights reserved.
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
	if (piobcnt > 128) {
		sbuf[2] = ipath_read_kreg64(
			dd, dd->ipath_kregs->kr_sendbuffererror + 2);
		sbuf[3] = ipath_read_kreg64(
			dd, dd->ipath_kregs->kr_sendbuffererror + 3);
	}

	if (sbuf[0] || sbuf[1] || (piobcnt > 128 && (sbuf[2] || sbuf[3]))) {
		int i;
		if (ipath_debug & (__IPATH_PKTDBG|__IPATH_DBG)) {
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
		dd->ipath_lastcancel = jiffies+3; /* no armlaunch for a bit */
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
	    sizeof(ipath_generic_hwerror_msgs) /
	    sizeof(ipath_generic_hwerror_msgs[0]);

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
static char *ib_linkstate(u32 linkstate)
{
	char *ret;

	switch (linkstate) {
	case IPATH_IBSTATE_INIT:
		ret = "Init";
		break;
	case IPATH_IBSTATE_ARM:
		ret = "Arm";
		break;
	case IPATH_IBSTATE_ACTIVE:
		ret = "Active";
		break;
	default:
		ret = "Down";
	}

	return ret;
}

static void handle_e_ibstatuschanged(struct ipath_devdata *dd,
				     ipath_err_t errs, int noprint)
{
	u64 val;
	u32 ltstate, lstate;

	/*
	 * even if diags are enabled, we want to notice LINKINIT, etc.
	 * We just don't want to change the LED state, or
	 * dd->ipath_kregs->kr_ibcctrl
	 */
	val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_ibcstatus);
	lstate = val & IPATH_IBSTATE_MASK;

	/*
	 * this is confusing enough when it happens that I want to always put it
	 * on the console and in the logs.  If it was a requested state change,
	 * we'll have already cleared the flags, so we won't print this warning
	 */
	if ((lstate != IPATH_IBSTATE_ARM && lstate != IPATH_IBSTATE_ACTIVE)
		&& (dd->ipath_flags & (IPATH_LINKARMED | IPATH_LINKACTIVE))) {
		dev_info(&dd->pcidev->dev, "Link state changed from %s to %s\n",
				 (dd->ipath_flags & IPATH_LINKARMED) ? "ARM" : "ACTIVE",
				 ib_linkstate(lstate));
		/*
		 * Flush all queued sends when link went to DOWN or INIT,
		 * to be sure that they don't block SMA and other MAD packets
		 */
		ipath_write_kreg(dd, dd->ipath_kregs->kr_sendctrl,
				 INFINIPATH_S_ABORT);
		ipath_disarm_piobufs(dd, dd->ipath_lastport_piobuf,
							(unsigned)(dd->ipath_piobcnt2k +
					dd->ipath_piobcnt4k) -
					dd->ipath_lastport_piobuf);
	}
	else if (lstate == IPATH_IBSTATE_INIT || lstate == IPATH_IBSTATE_ARM ||
	    lstate == IPATH_IBSTATE_ACTIVE) {
		/*
		 * only print at SMA if there is a change, debug if not
		 * (sometimes we want to know that, usually not).
		 */
		if (lstate == ((unsigned) dd->ipath_lastibcstat
			       & IPATH_IBSTATE_MASK)) {
			ipath_dbg("Status change intr but no change (%s)\n",
				  ib_linkstate(lstate));
		}
		else
			ipath_cdbg(VERBOSE, "Unit %u link state %s, last "
				   "was %s\n", dd->ipath_unit,
				   ib_linkstate(lstate),
				   ib_linkstate((unsigned)
						dd->ipath_lastibcstat
						& IPATH_IBSTATE_MASK));
	}
	else {
		lstate = dd->ipath_lastibcstat & IPATH_IBSTATE_MASK;
		if (lstate == IPATH_IBSTATE_INIT ||
		    lstate == IPATH_IBSTATE_ARM ||
		    lstate == IPATH_IBSTATE_ACTIVE)
			ipath_cdbg(VERBOSE, "Unit %u link state down"
				   " (state 0x%x), from %s\n",
				   dd->ipath_unit,
				   (u32)val & IPATH_IBSTATE_MASK,
				   ib_linkstate(lstate));
		else
			ipath_cdbg(VERBOSE, "Unit %u link state changed "
				   "to 0x%x from down (%x)\n",
				   dd->ipath_unit, (u32) val, lstate);
	}
	ltstate = (val >> INFINIPATH_IBCS_LINKTRAININGSTATE_SHIFT) &
		INFINIPATH_IBCS_LINKTRAININGSTATE_MASK;
	lstate = (val >> INFINIPATH_IBCS_LINKSTATE_SHIFT) &
		INFINIPATH_IBCS_LINKSTATE_MASK;

	if (ltstate == INFINIPATH_IBCS_LT_STATE_POLLACTIVE ||
	    ltstate == INFINIPATH_IBCS_LT_STATE_POLLQUIET) {
		u32 last_ltstate;

		/*
		 * Ignore cycling back and forth from Polling.Active
		 * to Polling.Quiet while waiting for the other end of
		 * the link to come up. We will cycle back and forth
		 * between them if no cable is plugged in,
		 * the other device is powered off or disabled, etc.
		 */
		last_ltstate = (dd->ipath_lastibcstat >>
				INFINIPATH_IBCS_LINKTRAININGSTATE_SHIFT)
			& INFINIPATH_IBCS_LINKTRAININGSTATE_MASK;
		if (last_ltstate == INFINIPATH_IBCS_LT_STATE_POLLACTIVE
		    || last_ltstate ==
		    INFINIPATH_IBCS_LT_STATE_POLLQUIET) {
			if (dd->ipath_ibpollcnt > 40) {
				dd->ipath_flags |= IPATH_NOCABLE;
				*dd->ipath_statusp |=
					IPATH_STATUS_IB_NOCABLE;
			} else
				dd->ipath_ibpollcnt++;
			goto skip_ibchange;
		}
	}
	dd->ipath_ibpollcnt = 0;	/* some state other than 2 or 3 */
	ipath_stats.sps_iblink++;
	if (ltstate != INFINIPATH_IBCS_LT_STATE_LINKUP) {
		dd->ipath_flags |= IPATH_LINKDOWN;
		dd->ipath_flags &= ~(IPATH_LINKUNK | IPATH_LINKINIT
				     | IPATH_LINKACTIVE |
				     IPATH_LINKARMED);
		*dd->ipath_statusp &= ~IPATH_STATUS_IB_READY;
		dd->ipath_lli_counter = 0;
		if (!noprint) {
			if (((dd->ipath_lastibcstat >>
			      INFINIPATH_IBCS_LINKSTATE_SHIFT) &
			     INFINIPATH_IBCS_LINKSTATE_MASK)
			    == INFINIPATH_IBCS_L_STATE_ACTIVE)
				/* if from up to down be more vocal */
				ipath_cdbg(VERBOSE,
					   "Unit %u link now down (%s)\n",
					   dd->ipath_unit,
					   ipath_ibcstatus_str[ltstate]);
			else
				ipath_cdbg(VERBOSE, "Unit %u link is "
					   "down (%s)\n", dd->ipath_unit,
					   ipath_ibcstatus_str[ltstate]);
		}

		dd->ipath_f_setextled(dd, lstate, ltstate);
	} else if ((val & IPATH_IBSTATE_MASK) == IPATH_IBSTATE_ACTIVE) {
		dd->ipath_flags |= IPATH_LINKACTIVE;
		dd->ipath_flags &=
			~(IPATH_LINKUNK | IPATH_LINKINIT | IPATH_LINKDOWN |
			  IPATH_LINKARMED | IPATH_NOCABLE);
		*dd->ipath_statusp &= ~IPATH_STATUS_IB_NOCABLE;
		*dd->ipath_statusp |=
			IPATH_STATUS_IB_READY | IPATH_STATUS_IB_CONF;
		dd->ipath_f_setextled(dd, lstate, ltstate);
	} else if ((val & IPATH_IBSTATE_MASK) == IPATH_IBSTATE_INIT) {
		/*
		 * set INIT and DOWN.  Down is checked by most of the other
		 * code, but INIT is useful to know in a few places.
		 */
		dd->ipath_flags |= IPATH_LINKINIT | IPATH_LINKDOWN;
		dd->ipath_flags &=
			~(IPATH_LINKUNK | IPATH_LINKACTIVE | IPATH_LINKARMED
			  | IPATH_NOCABLE);
		*dd->ipath_statusp &= ~(IPATH_STATUS_IB_NOCABLE
					| IPATH_STATUS_IB_READY);
		dd->ipath_f_setextled(dd, lstate, ltstate);
	} else if ((val & IPATH_IBSTATE_MASK) == IPATH_IBSTATE_ARM) {
		dd->ipath_flags |= IPATH_LINKARMED;
		dd->ipath_flags &=
			~(IPATH_LINKUNK | IPATH_LINKDOWN | IPATH_LINKINIT |
			  IPATH_LINKACTIVE | IPATH_NOCABLE);
		*dd->ipath_statusp &= ~(IPATH_STATUS_IB_NOCABLE
					| IPATH_STATUS_IB_READY);
		dd->ipath_f_setextled(dd, lstate, ltstate);
	} else {
		if (!noprint)
			ipath_dbg("IBstatuschange unit %u: %s (%x)\n",
				  dd->ipath_unit,
				  ipath_ibcstatus_str[ltstate], ltstate);
	}
skip_ibchange:
	dd->ipath_lastibcstat = val;
}

static void handle_supp_msgs(struct ipath_devdata *dd,
			     unsigned supp_msgs, char msg[512])
{
	/*
	 * Print the message unless it's ibc status change only, which
	 * happens so often we never want to count it.
	 */
	if (dd->ipath_lasterror & ~INFINIPATH_E_IBSTATUSCHANGED) {
		ipath_decode_err(msg, sizeof msg, dd->ipath_lasterror &
				 ~INFINIPATH_E_IBSTATUSCHANGED);
		if (dd->ipath_lasterror &
		    ~(INFINIPATH_E_RRCVEGRFULL | INFINIPATH_E_RRCVHDRFULL))
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
			ipath_dbg("Suppressed %u messages for %s\n",
				  supp_msgs, msg);
		}
	}
}

static unsigned handle_frequent_errors(struct ipath_devdata *dd,
				       ipath_err_t errs, char msg[512],
				       int *noprint)
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
			handle_supp_msgs(dd, supp_msgs, msg);
			supp_msgs = 0;
			nmsgs = 0;
		}
	}
	else if (!nmsgs++ || time_after(nc, nextmsg_time))
		nextmsg_time = nc + HZ / 2;

	return supp_msgs;
}

static int handle_errors(struct ipath_devdata *dd, ipath_err_t errs)
{
	char msg[512];
	u64 ignore_this_time = 0;
	int i;
	int chkerrpkts = 0, noprint = 0;
	unsigned supp_msgs;

	supp_msgs = handle_frequent_errors(dd, errs, msg, &noprint);

	/*
	 * don't report errors that are masked (includes those always
	 * ignored)
	 */
	errs &= ~dd->ipath_maskederrs;

	/* do these first, they are most important */
	if (errs & INFINIPATH_E_HARDWARE) {
		/* reuse same msg buf */
		dd->ipath_f_handle_hwerrors(dd, msg, sizeof msg);
	}

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
		/*
		 * It's not entirely reasonable assuming that the errors set
		 * in the last clear period are all responsible for the
		 * problem, but the alternative is to assume it's the only
		 * ones on this particular interrupt, which also isn't great
		 */
		dd->ipath_maskederrs |= dd->ipath_lasterror | errs;
		ipath_write_kreg(dd, dd->ipath_kregs->kr_errormask,
				 ~dd->ipath_maskederrs);
		ipath_decode_err(msg, sizeof msg,
				 (dd->ipath_maskederrs & ~dd->
				  ipath_ignorederrs));

		if ((dd->ipath_maskederrs & ~dd->ipath_ignorederrs) &
		    ~(INFINIPATH_E_RRCVEGRFULL | INFINIPATH_E_RRCVHDRFULL))
			ipath_dev_err(dd, "Disabling error(s) %llx because "
				      "occurring too frequently (%s)\n",
				      (unsigned long long)
				      (dd->ipath_maskederrs &
				       ~dd->ipath_ignorederrs), msg);
		else {
			/*
			 * rcvegrfull and rcvhdrqfull are "normal",
			 * for some types of processes (mostly benchmarks)
			 * that send huge numbers of messages, while not
			 * processing them.  So only complain about
			 * these at debug level.
			 */
			ipath_dbg("Disabling frequent queue full errors "
				  "(%s)\n", msg);
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

	/* likely due to cancel, so suppress */
	if ((errs & (INFINIPATH_E_SPKTLEN | INFINIPATH_E_SPIOARMLAUNCH)) &&
		dd->ipath_lastcancel > jiffies) {
		ipath_dbg("Suppressed armlaunch/spktlen after error send cancel\n");
		errs &= ~(INFINIPATH_E_SPIOARMLAUNCH | INFINIPATH_E_SPKTLEN);
	}

	if (!errs)
		return 0;

	if (!noprint)
		/*
		 * the ones we mask off are handled specially below or above
		 */
		ipath_decode_err(msg, sizeof msg,
				 errs & ~(INFINIPATH_E_IBSTATUSCHANGED |
					  INFINIPATH_E_RRCVEGRFULL |
					  INFINIPATH_E_RRCVHDRFULL |
					  INFINIPATH_E_HARDWARE));
	else
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

	/*
	 * We don't want to print these two as they happen, or we can make
	 * the situation even worse, because it takes so long to print
	 * messages to serial consoles.  Kernel ports get printed from
	 * fast_stats, no more than every 5 seconds, user ports get printed
	 * on close
	 */
	if (errs & INFINIPATH_E_RRCVHDRFULL) {
		int any;
		u32 hd, tl;
		ipath_stats.sps_hdrqfull++;
		for (any = i = 0; i < dd->ipath_cfgports; i++) {
			struct ipath_portdata *pd = dd->ipath_pd[i];
			if (i == 0) {
				hd = dd->ipath_port0head;
				tl = (u32) le64_to_cpu(
					*dd->ipath_hdrqtailptr);
			} else if (pd && pd->port_cnt &&
				   pd->port_rcvhdrtail_kvaddr) {
				/*
				 * don't report same point multiple times,
				 * except kernel
				 */
				tl = *(u64 *) pd->port_rcvhdrtail_kvaddr;
				if (tl == dd->ipath_lastrcvhdrqtails[i])
					continue;
				hd = ipath_read_ureg32(dd, ur_rcvhdrhead,
						       i);
			} else
				continue;
			if (hd == (tl + 1) ||
			    (!hd && tl == dd->ipath_hdrqlast)) {
				if (i == 0)
					chkerrpkts = 1;
				dd->ipath_lastrcvhdrqtails[i] = tl;
				pd->port_hdrqfull++;
			}
		}
	}
	if (errs & INFINIPATH_E_RRCVEGRFULL) {
		/*
		 * since this is of less importance and not likely to
		 * happen without also getting hdrfull, only count
		 * occurrences; don't check each port (or even the kernel
		 * vs user)
		 */
		ipath_stats.sps_etidfull++;
		if (dd->ipath_port0head !=
		    (u32) le64_to_cpu(*dd->ipath_hdrqtailptr))
			chkerrpkts = 1;
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
		if (!noprint) {
			u64 st = ipath_read_kreg64(
				dd, dd->ipath_kregs->kr_ibcstatus);

			ipath_dbg("Lost link, link now down (%s)\n",
				  ipath_ibcstatus_str[st & 0xf]);
		}
	}
	if (errs & INFINIPATH_E_IBSTATUSCHANGED)
		handle_e_ibstatuschanged(dd, errs, noprint);

	if (errs & INFINIPATH_E_RESET) {
		if (!noprint)
			ipath_dev_err(dd, "Got reset, requires re-init "
				      "(unload and reload driver)\n");
		dd->ipath_flags &= ~IPATH_INITTED;	/* needs re-init */
		/* mark as having had error */
		*dd->ipath_statusp |= IPATH_STATUS_HWERROR;
		*dd->ipath_statusp &= ~IPATH_STATUS_IB_CONF;
	}

	if (!noprint && *msg)
		ipath_dev_err(dd, "%s error\n", msg);
	if (dd->ipath_state_wanted & dd->ipath_flags) {
		ipath_cdbg(VERBOSE, "driver wanted state %x, iflags now %x, "
			   "waking\n", dd->ipath_state_wanted,
			   dd->ipath_flags);
		wake_up_interruptible(&ipath_state_wait);
	}

	return chkerrpkts;
}

/* this is separate to allow for better optimization of ipath_intr() */

static void ipath_bad_intr(struct ipath_devdata *dd, u32 * unexpectp)
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
		if (ipath_read_kreg32(dd, dd->ipath_kregs->kr_intmask)) {
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

static void ipath_bad_regread(struct ipath_devdata *dd)
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

static void handle_port_pioavail(struct ipath_devdata *dd)
{
	u32 i;
	/*
	 * start from port 1, since for now port 0  is never using
	 * wait_event for PIO
	 */
	for (i = 1; dd->ipath_portpiowait && i < dd->ipath_cfgports; i++) {
		struct ipath_portdata *pd = dd->ipath_pd[i];

		if (pd && pd->port_cnt &&
		    dd->ipath_portpiowait & (1U << i)) {
			clear_bit(i, &dd->ipath_portpiowait);
			if (test_bit(IPATH_PORT_WAITING_PIO,
				     &pd->port_flag)) {
				clear_bit(IPATH_PORT_WAITING_PIO,
					  &pd->port_flag);
				wake_up_interruptible(&pd->port_wait);
			}
		}
	}
}

static void handle_layer_pioavail(struct ipath_devdata *dd)
{
	int ret;

	ret = ipath_ib_piobufavail(dd->verbs_dev);
	if (ret > 0)
		goto set;

	return;
set:
	set_bit(IPATH_S_PIOINTBUFAVAIL, &dd->ipath_sendctrl);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_sendctrl,
			 dd->ipath_sendctrl);
}

/*
 * Handle receive interrupts for user ports; this means a user
 * process was waiting for a packet to arrive, and didn't want
 * to poll
 */
static void handle_urcv(struct ipath_devdata *dd, u32 istat)
{
	u64 portr;
	int i;
	int rcvdint = 0;

	portr = ((istat >> INFINIPATH_I_RCVAVAIL_SHIFT) &
		 dd->ipath_i_rcvavail_mask)
		| ((istat >> INFINIPATH_I_RCVURG_SHIFT) &
		   dd->ipath_i_rcvurg_mask);
	for (i = 1; i < dd->ipath_cfgports; i++) {
		struct ipath_portdata *pd = dd->ipath_pd[i];
		if (portr & (1 << i) && pd && pd->port_cnt &&
			test_bit(IPATH_PORT_WAITING_RCV, &pd->port_flag)) {
			int rcbit;
			clear_bit(IPATH_PORT_WAITING_RCV,
				  &pd->port_flag);
			rcbit = i + INFINIPATH_R_INTRAVAIL_SHIFT;
			clear_bit(1UL << rcbit, &dd->ipath_rcvctrl);
			wake_up_interruptible(&pd->port_wait);
			rcvdint = 1;
		}
	}
	if (rcvdint) {
		/* only want to take one interrupt, so turn off the rcv
		 * interrupt for all the ports that we did the wakeup on
		 * (but never for kernel port)
		 */
		ipath_write_kreg(dd, dd->ipath_kregs->kr_rcvctrl,
				 dd->ipath_rcvctrl);
	}
}

irqreturn_t ipath_intr(int irq, void *data)
{
	struct ipath_devdata *dd = data;
	u32 istat, chk0rcv = 0;
	ipath_err_t estat = 0;
	irqreturn_t ret;
	u32 oldhead, curtail;
	static unsigned unexpected = 0;
	static const u32 port0rbits = (1U<<INFINIPATH_I_RCVAVAIL_SHIFT) |
		 (1U<<INFINIPATH_I_RCVURG_SHIFT);

	ipath_stats.sps_ints++;

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

	/*
	 * We try to avoid reading the interrupt status register, since
	 * that's a PIO read, and stalls the processor for up to about
	 * ~0.25 usec. The idea is that if we processed a port0 packet,
	 * we blindly clear the  port 0 receive interrupt bits, and nothing
	 * else, then return.  If other interrupts are pending, the chip
	 * will re-interrupt us as soon as we write the intclear register.
	 * We then won't process any more kernel packets (if not the 2nd
	 * time, then the 3rd or 4th) and we'll then handle the other
	 * interrupts.   We clear the interrupts first so that we don't
	 * lose intr for later packets that arrive while we are processing.
	 */
	oldhead = dd->ipath_port0head;
	curtail = (u32)le64_to_cpu(*dd->ipath_hdrqtailptr);
	if (oldhead != curtail) {
		if (dd->ipath_flags & IPATH_GPIO_INTR) {
			ipath_write_kreg(dd, dd->ipath_kregs->kr_gpio_clear,
					 (u64) (1 << IPATH_GPIO_PORT0_BIT));
			istat = port0rbits | INFINIPATH_I_GPIO;
		}
		else
			istat = port0rbits;
		ipath_write_kreg(dd, dd->ipath_kregs->kr_intclear, istat);
		ipath_kreceive(dd);
		if (oldhead != dd->ipath_port0head) {
			ipath_stats.sps_fastrcvint++;
			goto done;
		}
	}

	istat = ipath_read_kreg32(dd, dd->ipath_kregs->kr_intstatus);

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
			      "interrupt with unknown interrupts %x set\n",
			      istat & (u32) ~ dd->ipath_i_bitsextant);
	else
		ipath_cdbg(VERBOSE, "intr stat=0x%x\n", istat);

	if (unlikely(istat & INFINIPATH_I_ERROR)) {
		ipath_stats.sps_errints++;
		estat = ipath_read_kreg64(dd,
					  dd->ipath_kregs->kr_errorstatus);
		if (!estat)
			dev_info(&dd->pcidev->dev, "error interrupt (%x), "
				 "but no error bits set!\n", istat);
		else if (estat == -1LL)
			/*
			 * should we try clearing all, or hope next read
			 * works?
			 */
			ipath_dev_err(dd, "Read of error status failed "
				      "(all bits set); ignoring\n");
		else
			if (handle_errors(dd, estat))
				/* force calling ipath_kreceive() */
				chk0rcv = 1;
	}

	if (istat & INFINIPATH_I_GPIO) {
		/*
		 * GPIO interrupts fall in two broad classes:
		 * GPIO_2 indicates (on some HT4xx boards) that a packet
		 *        has arrived for Port 0. Checking for this
		 *        is controlled by flag IPATH_GPIO_INTR.
		 * GPIO_3..5 on IBA6120 Rev2 chips indicate errors
		 *        that we need to count. Checking for this
		 *        is controlled by flag IPATH_GPIO_ERRINTRS.
		 */
		u32 gpiostatus;
		u32 to_clear = 0;

		gpiostatus = ipath_read_kreg32(
			dd, dd->ipath_kregs->kr_gpio_status);
		/* First the error-counter case.
		 */
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
		if (unlikely(gpiostatus)) {
			/*
			 * Some unexpected bits remain. If they could have
			 * caused the interrupt, complain and clear.
			 * MEA: this is almost certainly non-ideal.
			 * we should look into auto-disable of unexpected
			 * GPIO interrupts, possibly on a "three strikes"
			 * basis.
			 */
			u32 mask;
			mask = ipath_read_kreg32(
				dd, dd->ipath_kregs->kr_gpio_mask);
			if (mask & gpiostatus) {
				ipath_dbg("Unexpected GPIO IRQ bits %x\n",
				  gpiostatus & mask);
				to_clear |= (gpiostatus & mask);
			}
		}
		if (to_clear) {
			ipath_write_kreg(dd, dd->ipath_kregs->kr_gpio_clear,
					(u64) to_clear);
		}
	}
	chk0rcv |= istat & port0rbits;

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
	 * handle port0 receive  before checking for pio buffers available,
	 * since receives can overflow; piobuf waiters can afford a few
	 * extra cycles, since they were waiting anyway, and user's waiting
	 * for receive are at the bottom.
	 */
	if (chk0rcv) {
		ipath_kreceive(dd);
		istat &= ~port0rbits;
	}

	if (istat & ((dd->ipath_i_rcvavail_mask <<
		      INFINIPATH_I_RCVAVAIL_SHIFT)
		     | (dd->ipath_i_rcvurg_mask <<
			INFINIPATH_I_RCVURG_SHIFT)))
		handle_urcv(dd, istat);

	if (istat & INFINIPATH_I_SPIOBUFAVAIL) {
		clear_bit(IPATH_S_PIOINTBUFAVAIL, &dd->ipath_sendctrl);
		ipath_write_kreg(dd, dd->ipath_kregs->kr_sendctrl,
				 dd->ipath_sendctrl);

		if (dd->ipath_portpiowait)
			handle_port_pioavail(dd);

		handle_layer_pioavail(dd);
	}

done:
	ret = IRQ_HANDLED;

bail:
	return ret;
}
