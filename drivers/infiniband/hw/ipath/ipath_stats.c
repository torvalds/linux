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

#include "ipath_kernel.h"

struct infinipath_stats ipath_stats;

/**
 * ipath_snap_cntr - snapshot a chip counter
 * @dd: the infinipath device
 * @creg: the counter to snapshot
 *
 * called from add_timer and user counter read calls, to deal with
 * counters that wrap in "human time".  The words sent and received, and
 * the packets sent and received are all that we worry about.  For now,
 * at least, we don't worry about error counters, because if they wrap
 * that quickly, we probably don't care.  We may eventually just make this
 * handle all the counters.  word counters can wrap in about 20 seconds
 * of full bandwidth traffic, packet counters in a few hours.
 */

u64 ipath_snap_cntr(struct ipath_devdata *dd, ipath_creg creg)
{
	u32 val, reg64 = 0;
	u64 val64;
	unsigned long t0, t1;
	u64 ret;

	t0 = jiffies;
	/* If fast increment counters are only 32 bits, snapshot them,
	 * and maintain them as 64bit values in the driver */
	if (!(dd->ipath_flags & IPATH_32BITCOUNTERS) &&
	    (creg == dd->ipath_cregs->cr_wordsendcnt ||
	     creg == dd->ipath_cregs->cr_wordrcvcnt ||
	     creg == dd->ipath_cregs->cr_pktsendcnt ||
	     creg == dd->ipath_cregs->cr_pktrcvcnt)) {
		val64 = ipath_read_creg(dd, creg);
		val = val64 == ~0ULL ? ~0U : 0;
		reg64 = 1;
	} else			/* val64 just to keep gcc quiet... */
		val64 = val = ipath_read_creg32(dd, creg);
	/*
	 * See if a second has passed.  This is just a way to detect things
	 * that are quite broken.  Normally this should take just a few
	 * cycles (the check is for long enough that we don't care if we get
	 * pre-empted.)  An Opteron HT O read timeout is 4 seconds with
	 * normal NB values
	 */
	t1 = jiffies;
	if (time_before(t0 + HZ, t1) && val == -1) {
		ipath_dev_err(dd, "Error!  Read counter 0x%x timed out\n",
			      creg);
		ret = 0ULL;
		goto bail;
	}
	if (reg64) {
		ret = val64;
		goto bail;
	}

	if (creg == dd->ipath_cregs->cr_wordsendcnt) {
		if (val != dd->ipath_lastsword) {
			dd->ipath_sword += val - dd->ipath_lastsword;
			dd->ipath_lastsword = val;
		}
		val64 = dd->ipath_sword;
	} else if (creg == dd->ipath_cregs->cr_wordrcvcnt) {
		if (val != dd->ipath_lastrword) {
			dd->ipath_rword += val - dd->ipath_lastrword;
			dd->ipath_lastrword = val;
		}
		val64 = dd->ipath_rword;
	} else if (creg == dd->ipath_cregs->cr_pktsendcnt) {
		if (val != dd->ipath_lastspkts) {
			dd->ipath_spkts += val - dd->ipath_lastspkts;
			dd->ipath_lastspkts = val;
		}
		val64 = dd->ipath_spkts;
	} else if (creg == dd->ipath_cregs->cr_pktrcvcnt) {
		if (val != dd->ipath_lastrpkts) {
			dd->ipath_rpkts += val - dd->ipath_lastrpkts;
			dd->ipath_lastrpkts = val;
		}
		val64 = dd->ipath_rpkts;
	} else
		val64 = (u64) val;

	ret = val64;

bail:
	return ret;
}

/**
 * ipath_qcheck - print delta of egrfull/hdrqfull errors for kernel ports
 * @dd: the infinipath device
 *
 * print the delta of egrfull/hdrqfull errors for kernel ports no more than
 * every 5 seconds.  User processes are printed at close, but kernel doesn't
 * close, so...  Separate routine so may call from other places someday, and
 * so function name when printed by _IPATH_INFO is meaningfull
 */
static void ipath_qcheck(struct ipath_devdata *dd)
{
	static u64 last_tot_hdrqfull;
	struct ipath_portdata *pd = dd->ipath_pd[0];
	size_t blen = 0;
	char buf[128];
	u32 hdrqtail;

	*buf = 0;
	if (pd->port_hdrqfull != dd->ipath_p0_hdrqfull) {
		blen = snprintf(buf, sizeof buf, "port 0 hdrqfull %u",
				pd->port_hdrqfull -
				dd->ipath_p0_hdrqfull);
		dd->ipath_p0_hdrqfull = pd->port_hdrqfull;
	}
	if (ipath_stats.sps_etidfull != dd->ipath_last_tidfull) {
		blen += snprintf(buf + blen, sizeof buf - blen,
				 "%srcvegrfull %llu",
				 blen ? ", " : "",
				 (unsigned long long)
				 (ipath_stats.sps_etidfull -
				  dd->ipath_last_tidfull));
		dd->ipath_last_tidfull = ipath_stats.sps_etidfull;
	}

	/*
	 * this is actually the number of hdrq full interrupts, not actual
	 * events, but at the moment that's mostly what I'm interested in.
	 * Actual count, etc. is in the counters, if needed.  For production
	 * users this won't ordinarily be printed.
	 */

	if ((ipath_debug & (__IPATH_PKTDBG | __IPATH_DBG)) &&
	    ipath_stats.sps_hdrqfull != last_tot_hdrqfull) {
		blen += snprintf(buf + blen, sizeof buf - blen,
				 "%shdrqfull %llu (all ports)",
				 blen ? ", " : "",
				 (unsigned long long)
				 (ipath_stats.sps_hdrqfull -
				  last_tot_hdrqfull));
		last_tot_hdrqfull = ipath_stats.sps_hdrqfull;
	}
	if (blen)
		ipath_dbg("%s\n", buf);

	hdrqtail = ipath_get_hdrqtail(pd);
	if (pd->port_head != hdrqtail) {
		if (dd->ipath_lastport0rcv_cnt ==
		    ipath_stats.sps_port0pkts) {
			ipath_cdbg(PKT, "missing rcv interrupts? "
				   "port0 hd=%x tl=%x; port0pkts %llx; write"
				   " hd (w/intr)\n",
				   pd->port_head, hdrqtail,
				   (unsigned long long)
				   ipath_stats.sps_port0pkts);
			ipath_write_ureg(dd, ur_rcvhdrhead, hdrqtail |
				dd->ipath_rhdrhead_intr_off, pd->port_port);
		}
		dd->ipath_lastport0rcv_cnt = ipath_stats.sps_port0pkts;
	}
}

static void ipath_chk_errormask(struct ipath_devdata *dd)
{
	static u32 fixed;
	u32 ctrl;
	unsigned long errormask;
	unsigned long hwerrs;

	if (!dd->ipath_errormask || !(dd->ipath_flags & IPATH_INITTED))
		return;

	errormask = ipath_read_kreg64(dd, dd->ipath_kregs->kr_errormask);

	if (errormask == dd->ipath_errormask)
		return;
	fixed++;

	hwerrs = ipath_read_kreg64(dd, dd->ipath_kregs->kr_hwerrstatus);
	ctrl = ipath_read_kreg32(dd, dd->ipath_kregs->kr_control);

	ipath_write_kreg(dd, dd->ipath_kregs->kr_errormask,
		dd->ipath_errormask);

	if ((hwerrs & dd->ipath_hwerrmask) ||
		(ctrl & INFINIPATH_C_FREEZEMODE)) {
		/* force re-interrupt of pending events, just in case */
		ipath_write_kreg(dd, dd->ipath_kregs->kr_hwerrclear, 0ULL);
		ipath_write_kreg(dd, dd->ipath_kregs->kr_errorclear, 0ULL);
		ipath_write_kreg(dd, dd->ipath_kregs->kr_intclear, 0ULL);
		dev_info(&dd->pcidev->dev,
			"errormask fixed(%u) %lx -> %lx, ctrl %x hwerr %lx\n",
			fixed, errormask, (unsigned long)dd->ipath_errormask,
			ctrl, hwerrs);
	} else
		ipath_dbg("errormask fixed(%u) %lx -> %lx, no freeze\n",
			fixed, errormask,
			(unsigned long)dd->ipath_errormask);
}


/**
 * ipath_get_faststats - get word counters from chip before they overflow
 * @opaque - contains a pointer to the infinipath device ipath_devdata
 *
 * called from add_timer
 */
void ipath_get_faststats(unsigned long opaque)
{
	struct ipath_devdata *dd = (struct ipath_devdata *) opaque;
	int i;
	static unsigned cnt;
	unsigned long flags;
	u64 traffic_wds;

	/*
	 * don't access the chip while running diags, or memory diags can
	 * fail
	 */
	if (!dd->ipath_kregbase || !(dd->ipath_flags & IPATH_INITTED) ||
	    ipath_diag_inuse)
		/* but re-arm the timer, for diags case; won't hurt other */
		goto done;

	/*
	 * We now try to maintain a "active timer", based on traffic
	 * exceeding a threshold, so we need to check the word-counts
	 * even if they are 64-bit.
	 */
	traffic_wds = ipath_snap_cntr(dd, dd->ipath_cregs->cr_wordsendcnt) +
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_wordrcvcnt);
	spin_lock_irqsave(&dd->ipath_eep_st_lock, flags);
	traffic_wds -= dd->ipath_traffic_wds;
	dd->ipath_traffic_wds += traffic_wds;
	if (traffic_wds  >= IPATH_TRAFFIC_ACTIVE_THRESHOLD)
		atomic_add(5, &dd->ipath_active_time); /* S/B #define */
	spin_unlock_irqrestore(&dd->ipath_eep_st_lock, flags);

	if (dd->ipath_flags & IPATH_32BITCOUNTERS) {
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_pktsendcnt);
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_pktrcvcnt);
	}

	ipath_qcheck(dd);

	/*
	 * deal with repeat error suppression.  Doesn't really matter if
	 * last error was almost a full interval ago, or just a few usecs
	 * ago; still won't get more than 2 per interval.  We may want
	 * longer intervals for this eventually, could do with mod, counter
	 * or separate timer.  Also see code in ipath_handle_errors() and
	 * ipath_handle_hwerrors().
	 */

	if (dd->ipath_lasterror)
		dd->ipath_lasterror = 0;
	if (dd->ipath_lasthwerror)
		dd->ipath_lasthwerror = 0;
	if (dd->ipath_maskederrs
	    && time_after(jiffies, dd->ipath_unmasktime)) {
		char ebuf[256];
		int iserr;
		iserr = ipath_decode_err(dd, ebuf, sizeof ebuf,
					 dd->ipath_maskederrs);
		if (dd->ipath_maskederrs &
		    ~(INFINIPATH_E_RRCVEGRFULL | INFINIPATH_E_RRCVHDRFULL |
		      INFINIPATH_E_PKTERRS))
			ipath_dev_err(dd, "Re-enabling masked errors "
				      "(%s)\n", ebuf);
		else {
			/*
			 * rcvegrfull and rcvhdrqfull are "normal", for some
			 * types of processes (mostly benchmarks) that send
			 * huge numbers of messages, while not processing
			 * them.  So only complain about these at debug
			 * level.
			 */
			if (iserr)
				ipath_dbg(
					"Re-enabling queue full errors (%s)\n",
					ebuf);
			else
				ipath_cdbg(ERRPKT, "Re-enabling packet"
					" problem interrupt (%s)\n", ebuf);
		}

		/* re-enable masked errors */
		dd->ipath_errormask |= dd->ipath_maskederrs;
		ipath_write_kreg(dd, dd->ipath_kregs->kr_errormask,
				 dd->ipath_errormask);
		dd->ipath_maskederrs = 0;
	}

	/* limit qfull messages to ~one per minute per port */
	if ((++cnt & 0x10)) {
		for (i = (int) dd->ipath_cfgports; --i >= 0; ) {
			struct ipath_portdata *pd = dd->ipath_pd[i];

			if (pd && pd->port_lastrcvhdrqtail != -1)
				pd->port_lastrcvhdrqtail = -1;
		}
	}

	ipath_chk_errormask(dd);
done:
	mod_timer(&dd->ipath_stats_timer, jiffies + HZ * 5);
}
