/*
 * Copyright (C) 2001-2004 by David Brownell
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* this file is part of ehci-hcd.c */

/*-------------------------------------------------------------------------*/

/*
 * EHCI hardware queue manipulation ... the core.  QH/QTD manipulation.
 *
 * Control, bulk, and interrupt traffic all use "qh" lists.  They list "qtd"
 * entries describing USB transactions, max 16-20kB/entry (with 4kB-aligned
 * buffers needed for the larger number).  We use one QH per endpoint, queue
 * multiple urbs (all three types) per endpoint.  URBs may need several qtds.
 *
 * ISO traffic uses "ISO TD" (itd, and sitd) records, and (along with
 * interrupts) needs careful scheduling.  Performance improvements can be
 * an ongoing challenge.  That's in "ehci-sched.c".
 *
 * USB 1.1 devices are handled (a) by "companion" OHCI or UHCI root hubs,
 * or otherwise through transaction translators (TTs) in USB 2.0 hubs using
 * (b) special fields in qh entries or (c) split iso entries.  TTs will
 * buffer low/full speed data so the host collects it at high speed.
 */

/*-------------------------------------------------------------------------*/

/* fill a qtd, returning how much of the buffer we were able to queue up */

static int
qtd_fill(struct ehci_hcd *ehci, struct ehci_qtd *qtd, dma_addr_t buf,
		  size_t len, int token, int maxpacket)
{
	int	i, count;
	u64	addr = buf;

	/* one buffer entry per 4K ... first might be short or unaligned */
	qtd->hw_buf[0] = cpu_to_hc32(ehci, (u32)addr);
	qtd->hw_buf_hi[0] = cpu_to_hc32(ehci, (u32)(addr >> 32));
	count = 0x1000 - (buf & 0x0fff);	/* rest of that page */
	if (likely (len < count))		/* ... iff needed */
		count = len;
	else {
		buf +=  0x1000;
		buf &= ~0x0fff;

		/* per-qtd limit: from 16K to 20K (best alignment) */
		for (i = 1; count < len && i < 5; i++) {
			addr = buf;
			qtd->hw_buf[i] = cpu_to_hc32(ehci, (u32)addr);
			qtd->hw_buf_hi[i] = cpu_to_hc32(ehci,
					(u32)(addr >> 32));
			buf += 0x1000;
			if ((count + 0x1000) < len)
				count += 0x1000;
			else
				count = len;
		}

		/* short packets may only terminate transfers */
		if (count != len)
			count -= (count % maxpacket);
	}
	qtd->hw_token = cpu_to_hc32(ehci, (count << 16) | token);
	qtd->length = count;

	return count;
}

/*-------------------------------------------------------------------------*/

static inline void
qh_update (struct ehci_hcd *ehci, struct ehci_qh *qh, struct ehci_qtd *qtd)
{
	struct ehci_qh_hw *hw = qh->hw;

	/* writes to an active overlay are unsafe */
	WARN_ON(qh->qh_state != QH_STATE_IDLE);

	hw->hw_qtd_next = QTD_NEXT(ehci, qtd->qtd_dma);
	hw->hw_alt_next = EHCI_LIST_END(ehci);

	/* Except for control endpoints, we make hardware maintain data
	 * toggle (like OHCI) ... here (re)initialize the toggle in the QH,
	 * and set the pseudo-toggle in udev. Only usb_clear_halt() will
	 * ever clear it.
	 */
	if (!(hw->hw_info1 & cpu_to_hc32(ehci, QH_TOGGLE_CTL))) {
		unsigned	is_out, epnum;

		is_out = qh->is_out;
		epnum = (hc32_to_cpup(ehci, &hw->hw_info1) >> 8) & 0x0f;
		if (unlikely (!usb_gettoggle (qh->dev, epnum, is_out))) {
			hw->hw_token &= ~cpu_to_hc32(ehci, QTD_TOGGLE);
			usb_settoggle (qh->dev, epnum, is_out, 1);
		}
	}

	hw->hw_token &= cpu_to_hc32(ehci, QTD_TOGGLE | QTD_STS_PING);
}

/* if it weren't for a common silicon quirk (writing the dummy into the qh
 * overlay, so qh->hw_token wrongly becomes inactive/halted), only fault
 * recovery (including urb dequeue) would need software changes to a QH...
 */
static void
qh_refresh (struct ehci_hcd *ehci, struct ehci_qh *qh)
{
	struct ehci_qtd *qtd;

	qtd = list_entry(qh->qtd_list.next, struct ehci_qtd, qtd_list);

	/*
	 * first qtd may already be partially processed.
	 * If we come here during unlink, the QH overlay region
	 * might have reference to the just unlinked qtd. The
	 * qtd is updated in qh_completions(). Update the QH
	 * overlay here.
	 */
	if (qh->hw->hw_token & ACTIVE_BIT(ehci))
		qh->hw->hw_qtd_next = qtd->hw_next;
	else
		qh_update(ehci, qh, qtd);
}

/*-------------------------------------------------------------------------*/

static void qh_link_async(struct ehci_hcd *ehci, struct ehci_qh *qh);

static void ehci_clear_tt_buffer_complete(struct usb_hcd *hcd,
		struct usb_host_endpoint *ep)
{
	struct ehci_hcd		*ehci = hcd_to_ehci(hcd);
	struct ehci_qh		*qh = ep->hcpriv;
	unsigned long		flags;

	spin_lock_irqsave(&ehci->lock, flags);
	qh->clearing_tt = 0;
	if (qh->qh_state == QH_STATE_IDLE && !list_empty(&qh->qtd_list)
			&& ehci->rh_state == EHCI_RH_RUNNING)
		qh_link_async(ehci, qh);
	spin_unlock_irqrestore(&ehci->lock, flags);
}

static void ehci_clear_tt_buffer(struct ehci_hcd *ehci, struct ehci_qh *qh,
		struct urb *urb, u32 token)
{

	/* If an async split transaction gets an error or is unlinked,
	 * the TT buffer may be left in an indeterminate state.  We
	 * have to clear the TT buffer.
	 *
	 * Note: this routine is never called for Isochronous transfers.
	 */
	if (urb->dev->tt && !usb_pipeint(urb->pipe) && !qh->clearing_tt) {
#if defined(DEBUG) || defined(CONFIG_DYNAMIC_DEBUG)
		struct usb_device *tt = urb->dev->tt->hub;
		dev_dbg(&tt->dev,
			"clear tt buffer port %d, a%d ep%d t%08x\n",
			urb->dev->ttport, urb->dev->devnum,
			usb_pipeendpoint(urb->pipe), token);
#endif /* DEBUG || CONFIG_DYNAMIC_DEBUG */
		if (!ehci_is_TDI(ehci)
				|| urb->dev->tt->hub !=
				   ehci_to_hcd(ehci)->self.root_hub) {
			if (usb_hub_clear_tt_buffer(urb) == 0)
				qh->clearing_tt = 1;
		} else {

			/* REVISIT ARC-derived cores don't clear the root
			 * hub TT buffer in this way...
			 */
		}
	}
}

static int qtd_copy_status (
	struct ehci_hcd *ehci,
	struct urb *urb,
	size_t length,
	u32 token
)
{
	int	status = -EINPROGRESS;

	/* count IN/OUT bytes, not SETUP (even short packets) */
	if (likely (QTD_PID (token) != 2))
		urb->actual_length += length - QTD_LENGTH (token);

	/* don't modify error codes */
	if (unlikely(urb->unlinked))
		return status;

	/* force cleanup after short read; not always an error */
	if (unlikely (IS_SHORT_READ (token)))
		status = -EREMOTEIO;

	/* serious "can't proceed" faults reported by the hardware */
	if (token & QTD_STS_HALT) {
		if (token & QTD_STS_BABBLE) {
			/* FIXME "must" disable babbling device's port too */
			status = -EOVERFLOW;
		/* CERR nonzero + halt --> stall */
		} else if (QTD_CERR(token)) {
			status = -EPIPE;

		/* In theory, more than one of the following bits can be set
		 * since they are sticky and the transaction is retried.
		 * Which to test first is rather arbitrary.
		 */
		} else if (token & QTD_STS_MMF) {
			/* fs/ls interrupt xfer missed the complete-split */
			status = -EPROTO;
		} else if (token & QTD_STS_DBE) {
			status = (QTD_PID (token) == 1) /* IN ? */
				? -ENOSR  /* hc couldn't read data */
				: -ECOMM; /* hc couldn't write data */
		} else if (token & QTD_STS_XACT) {
			/* timeout, bad CRC, wrong PID, etc */
			ehci_dbg(ehci, "devpath %s ep%d%s 3strikes\n",
				urb->dev->devpath,
				usb_pipeendpoint(urb->pipe),
				usb_pipein(urb->pipe) ? "in" : "out");
			status = -EPROTO;
		} else {	/* unknown */
			status = -EPROTO;
		}
	}

	return status;
}

static void
ehci_urb_done(struct ehci_hcd *ehci, struct urb *urb, int status)
__releases(ehci->lock)
__acquires(ehci->lock)
{
	if (usb_pipetype(urb->pipe) == PIPE_INTERRUPT) {
		/* ... update hc-wide periodic stats */
		ehci_to_hcd(ehci)->self.bandwidth_int_reqs--;
	}

	if (unlikely(urb->unlinked)) {
		COUNT(ehci->stats.unlink);
	} else {
		/* report non-error and short read status as zero */
		if (status == -EINPROGRESS || status == -EREMOTEIO)
			status = 0;
		COUNT(ehci->stats.complete);
	}

#ifdef EHCI_URB_TRACE
	ehci_dbg (ehci,
		"%s %s urb %p ep%d%s status %d len %d/%d\n",
		__func__, urb->dev->devpath, urb,
		usb_pipeendpoint (urb->pipe),
		usb_pipein (urb->pipe) ? "in" : "out",
		status,
		urb->actual_length, urb->transfer_buffer_length);
#endif

	/* complete() can reenter this HCD */
	usb_hcd_unlink_urb_from_ep(ehci_to_hcd(ehci), urb);
	spin_unlock (&ehci->lock);
	usb_hcd_giveback_urb(ehci_to_hcd(ehci), urb, status);
	spin_lock (&ehci->lock);
}

static int qh_schedule (struct ehci_hcd *ehci, struct ehci_qh *qh);

/*
 * Process and free completed qtds for a qh, returning URBs to drivers.
 * Chases up to qh->hw_current.  Returns nonzero if the caller should
 * unlink qh.
 */
static unsigned
qh_completions (struct ehci_hcd *ehci, struct ehci_qh *qh)
{
	struct ehci_qtd		*last, *end = qh->dummy;
	struct list_head	*entry, *tmp;
	int			last_status;
	int			stopped;
	u8			state;
	struct ehci_qh_hw	*hw = qh->hw;

	/* completions (or tasks on other cpus) must never clobber HALT
	 * till we've gone through and cleaned everything up, even when
	 * they add urbs to this qh's queue or mark them for unlinking.
	 *
	 * NOTE:  unlinking expects to be done in queue order.
	 *
	 * It's a bug for qh->qh_state to be anything other than
	 * QH_STATE_IDLE, unless our caller is scan_async() or
	 * scan_intr().
	 */
	state = qh->qh_state;
	qh->qh_state = QH_STATE_COMPLETING;
	stopped = (state == QH_STATE_IDLE);

 rescan:
	last = NULL;
	last_status = -EINPROGRESS;
	qh->dequeue_during_giveback = 0;

	/* remove de-activated QTDs from front of queue.
	 * after faults (including short reads), cleanup this urb
	 * then let the queue advance.
	 * if queue is stopped, handles unlinks.
	 */
	list_for_each_safe (entry, tmp, &qh->qtd_list) {
		struct ehci_qtd	*qtd;
		struct urb	*urb;
		u32		token = 0;

		qtd = list_entry (entry, struct ehci_qtd, qtd_list);
		urb = qtd->urb;

		/* clean up any state from previous QTD ...*/
		if (last) {
			if (likely (last->urb != urb)) {
				ehci_urb_done(ehci, last->urb, last_status);
				last_status = -EINPROGRESS;
			}
			ehci_qtd_free (ehci, last);
			last = NULL;
		}

		/* ignore urbs submitted during completions we reported */
		if (qtd == end)
			break;

		/* hardware copies qtd out of qh overlay */
		rmb ();
		token = hc32_to_cpu(ehci, qtd->hw_token);

		/* always clean up qtds the hc de-activated */
 retry_xacterr:
		if ((token & QTD_STS_ACTIVE) == 0) {

			/* Report Data Buffer Error: non-fatal but useful */
			if (token & QTD_STS_DBE)
				ehci_dbg(ehci,
					"detected DataBufferErr for urb %p ep%d%s len %d, qtd %p [qh %p]\n",
					urb,
					usb_endpoint_num(&urb->ep->desc),
					usb_endpoint_dir_in(&urb->ep->desc) ? "in" : "out",
					urb->transfer_buffer_length,
					qtd,
					qh);

			/* on STALL, error, and short reads this urb must
			 * complete and all its qtds must be recycled.
			 */
			if ((token & QTD_STS_HALT) != 0) {

				/* retry transaction errors until we
				 * reach the software xacterr limit
				 */
				if ((token & QTD_STS_XACT) &&
						QTD_CERR(token) == 0 &&
						++qh->xacterrs < QH_XACTERR_MAX &&
						!urb->unlinked) {
					ehci_dbg(ehci,
	"detected XactErr len %zu/%zu retry %d\n",
	qtd->length - QTD_LENGTH(token), qtd->length, qh->xacterrs);

					/* reset the token in the qtd and the
					 * qh overlay (which still contains
					 * the qtd) so that we pick up from
					 * where we left off
					 */
					token &= ~QTD_STS_HALT;
					token |= QTD_STS_ACTIVE |
							(EHCI_TUNE_CERR << 10);
					qtd->hw_token = cpu_to_hc32(ehci,
							token);
					wmb();
					hw->hw_token = cpu_to_hc32(ehci,
							token);
					goto retry_xacterr;
				}
				stopped = 1;

			/* magic dummy for some short reads; qh won't advance.
			 * that silicon quirk can kick in with this dummy too.
			 *
			 * other short reads won't stop the queue, including
			 * control transfers (status stage handles that) or
			 * most other single-qtd reads ... the queue stops if
			 * URB_SHORT_NOT_OK was set so the driver submitting
			 * the urbs could clean it up.
			 */
			} else if (IS_SHORT_READ (token)
					&& !(qtd->hw_alt_next
						& EHCI_LIST_END(ehci))) {
				stopped = 1;
			}

		/* stop scanning when we reach qtds the hc is using */
		} else if (likely (!stopped
				&& ehci->rh_state >= EHCI_RH_RUNNING)) {
			break;

		/* scan the whole queue for unlinks whenever it stops */
		} else {
			stopped = 1;

			/* cancel everything if we halt, suspend, etc */
			if (ehci->rh_state < EHCI_RH_RUNNING)
				last_status = -ESHUTDOWN;

			/* this qtd is active; skip it unless a previous qtd
			 * for its urb faulted, or its urb was canceled.
			 */
			else if (last_status == -EINPROGRESS && !urb->unlinked)
				continue;

			/*
			 * If this was the active qtd when the qh was unlinked
			 * and the overlay's token is active, then the overlay
			 * hasn't been written back to the qtd yet so use its
			 * token instead of the qtd's.  After the qtd is
			 * processed and removed, the overlay won't be valid
			 * any more.
			 */
			if (state == QH_STATE_IDLE &&
					qh->qtd_list.next == &qtd->qtd_list &&
					(hw->hw_token & ACTIVE_BIT(ehci))) {
				token = hc32_to_cpu(ehci, hw->hw_token);
				hw->hw_token &= ~ACTIVE_BIT(ehci);

				/* An unlink may leave an incomplete
				 * async transaction in the TT buffer.
				 * We have to clear it.
				 */
				ehci_clear_tt_buffer(ehci, qh, urb, token);
			}
		}

		/* unless we already know the urb's status, collect qtd status
		 * and update count of bytes transferred.  in common short read
		 * cases with only one data qtd (including control transfers),
		 * queue processing won't halt.  but with two or more qtds (for
		 * example, with a 32 KB transfer), when the first qtd gets a
		 * short read the second must be removed by hand.
		 */
		if (last_status == -EINPROGRESS) {
			last_status = qtd_copy_status(ehci, urb,
					qtd->length, token);
			if (last_status == -EREMOTEIO
					&& (qtd->hw_alt_next
						& EHCI_LIST_END(ehci)))
				last_status = -EINPROGRESS;

			/* As part of low/full-speed endpoint-halt processing
			 * we must clear the TT buffer (11.17.5).
			 */
			if (unlikely(last_status != -EINPROGRESS &&
					last_status != -EREMOTEIO)) {
				/* The TT's in some hubs malfunction when they
				 * receive this request following a STALL (they
				 * stop sending isochronous packets).  Since a
				 * STALL can't leave the TT buffer in a busy
				 * state (if you believe Figures 11-48 - 11-51
				 * in the USB 2.0 spec), we won't clear the TT
				 * buffer in this case.  Strictly speaking this
				 * is a violation of the spec.
				 */
				if (last_status != -EPIPE)
					ehci_clear_tt_buffer(ehci, qh, urb,
							token);
			}
		}

		/* if we're removing something not at the queue head,
		 * patch the hardware queue pointer.
		 */
		if (stopped && qtd->qtd_list.prev != &qh->qtd_list) {
			last = list_entry (qtd->qtd_list.prev,
					struct ehci_qtd, qtd_list);
			last->hw_next = qtd->hw_next;
		}

		/* remove qtd; it's recycled after possible urb completion */
		list_del (&qtd->qtd_list);
		last = qtd;

		/* reinit the xacterr counter for the next qtd */
		qh->xacterrs = 0;
	}

	/* last urb's completion might still need calling */
	if (likely (last != NULL)) {
		ehci_urb_done(ehci, last->urb, last_status);
		ehci_qtd_free (ehci, last);
	}

	/* Do we need to rescan for URBs dequeued during a giveback? */
	if (unlikely(qh->dequeue_during_giveback)) {
		/* If the QH is already unlinked, do the rescan now. */
		if (state == QH_STATE_IDLE)
			goto rescan;

		/* Otherwise the caller must unlink the QH. */
	}

	/* restore original state; caller must unlink or relink */
	qh->qh_state = state;

	/* be sure the hardware's done with the qh before refreshing
	 * it after fault cleanup, or recovering from silicon wrongly
	 * overlaying the dummy qtd (which reduces DMA chatter).
	 *
	 * We won't refresh a QH that's linked (after the HC
	 * stopped the queue).  That avoids a race:
	 *  - HC reads first part of QH;
	 *  - CPU updates that first part and the token;
	 *  - HC reads rest of that QH, including token
	 * Result:  HC gets an inconsistent image, and then
	 * DMAs to/from the wrong memory (corrupting it).
	 *
	 * That should be rare for interrupt transfers,
	 * except maybe high bandwidth ...
	 */
	if (stopped != 0 || hw->hw_qtd_next == EHCI_LIST_END(ehci))
		qh->exception = 1;

	/* Let the caller know if the QH needs to be unlinked. */
	return qh->exception;
}

/*-------------------------------------------------------------------------*/

// high bandwidth multiplier, as encoded in highspeed endpoint descriptors
#define hb_mult(wMaxPacketSize) (1 + (((wMaxPacketSize) >> 11) & 0x03))
// ... and packet size, for any kind of endpoint descriptor
#define max_packet(wMaxPacketSize) ((wMaxPacketSize) & 0x07ff)

/*
 * reverse of qh_urb_transaction:  free a list of TDs.
 * used for cleanup after errors, before HC sees an URB's TDs.
 */
static void qtd_list_free (
	struct ehci_hcd		*ehci,
	struct urb		*urb,
	struct list_head	*qtd_list
) {
	struct list_head	*entry, *temp;

	list_for_each_safe (entry, temp, qtd_list) {
		struct ehci_qtd	*qtd;

		qtd = list_entry (entry, struct ehci_qtd, qtd_list);
		list_del (&qtd->qtd_list);
		ehci_qtd_free (ehci, qtd);
	}
}

/*
 * create a list of filled qtds for this URB; won't link into qh.
 */
static struct list_head *
qh_urb_transaction (
	struct ehci_hcd		*ehci,
	struct urb		*urb,
	struct list_head	*head,
	gfp_t			flags
) {
	struct ehci_qtd		*qtd, *qtd_prev;
	dma_addr_t		buf;
	int			len, this_sg_len, maxpacket;
	int			is_input;
	u32			token;
	int			i;
	struct scatterlist	*sg;

	/*
	 * URBs map to sequences of QTDs:  one logical transaction
	 */
	qtd = ehci_qtd_alloc (ehci, flags);
	if (unlikely (!qtd))
		return NULL;
	list_add_tail (&qtd->qtd_list, head);
	qtd->urb = urb;

	token = QTD_STS_ACTIVE;
	token |= (EHCI_TUNE_CERR << 10);
	/* for split transactions, SplitXState initialized to zero */

	len = urb->transfer_buffer_length;
	is_input = usb_pipein (urb->pipe);
	if (usb_pipecontrol (urb->pipe)) {
		/* SETUP pid */
		qtd_fill(ehci, qtd, urb->setup_dma,
				sizeof (struct usb_ctrlrequest),
				token | (2 /* "setup" */ << 8), 8);

		/* ... and always at least one more pid */
		token ^= QTD_TOGGLE;
		qtd_prev = qtd;
		qtd = ehci_qtd_alloc (ehci, flags);
		if (unlikely (!qtd))
			goto cleanup;
		qtd->urb = urb;
		qtd_prev->hw_next = QTD_NEXT(ehci, qtd->qtd_dma);
		list_add_tail (&qtd->qtd_list, head);

		/* for zero length DATA stages, STATUS is always IN */
		if (len == 0)
			token |= (1 /* "in" */ << 8);
	}

	/*
	 * data transfer stage:  buffer setup
	 */
	i = urb->num_mapped_sgs;
	if (len > 0 && i > 0) {
		sg = urb->sg;
		buf = sg_dma_address(sg);

		/* urb->transfer_buffer_length may be smaller than the
		 * size of the scatterlist (or vice versa)
		 */
		this_sg_len = min_t(int, sg_dma_len(sg), len);
	} else {
		sg = NULL;
		buf = urb->transfer_dma;
		this_sg_len = len;
	}

	if (is_input)
		token |= (1 /* "in" */ << 8);
	/* else it's already initted to "out" pid (0 << 8) */

	maxpacket = max_packet(usb_maxpacket(urb->dev, urb->pipe, !is_input));

	/*
	 * buffer gets wrapped in one or more qtds;
	 * last one may be "short" (including zero len)
	 * and may serve as a control status ack
	 */
	for (;;) {
		int this_qtd_len;

		this_qtd_len = qtd_fill(ehci, qtd, buf, this_sg_len, token,
				maxpacket);
		this_sg_len -= this_qtd_len;
		len -= this_qtd_len;
		buf += this_qtd_len;

		/*
		 * short reads advance to a "magic" dummy instead of the next
		 * qtd ... that forces the queue to stop, for manual cleanup.
		 * (this will usually be overridden later.)
		 */
		if (is_input)
			qtd->hw_alt_next = ehci->async->hw->hw_alt_next;

		/* qh makes control packets use qtd toggle; maybe switch it */
		if ((maxpacket & (this_qtd_len + (maxpacket - 1))) == 0)
			token ^= QTD_TOGGLE;

		if (likely(this_sg_len <= 0)) {
			if (--i <= 0 || len <= 0)
				break;
			sg = sg_next(sg);
			buf = sg_dma_address(sg);
			this_sg_len = min_t(int, sg_dma_len(sg), len);
		}

		qtd_prev = qtd;
		qtd = ehci_qtd_alloc (ehci, flags);
		if (unlikely (!qtd))
			goto cleanup;
		qtd->urb = urb;
		qtd_prev->hw_next = QTD_NEXT(ehci, qtd->qtd_dma);
		list_add_tail (&qtd->qtd_list, head);
	}

	/*
	 * unless the caller requires manual cleanup after short reads,
	 * have the alt_next mechanism keep the queue running after the
	 * last data qtd (the only one, for control and most other cases).
	 */
	if (likely ((urb->transfer_flags & URB_SHORT_NOT_OK) == 0
				|| usb_pipecontrol (urb->pipe)))
		qtd->hw_alt_next = EHCI_LIST_END(ehci);

	/*
	 * control requests may need a terminating data "status" ack;
	 * other OUT ones may need a terminating short packet
	 * (zero length).
	 */
	if (likely (urb->transfer_buffer_length != 0)) {
		int	one_more = 0;

		if (usb_pipecontrol (urb->pipe)) {
			one_more = 1;
			token ^= 0x0100;	/* "in" <--> "out"  */
			token |= QTD_TOGGLE;	/* force DATA1 */
		} else if (usb_pipeout(urb->pipe)
				&& (urb->transfer_flags & URB_ZERO_PACKET)
				&& !(urb->transfer_buffer_length % maxpacket)) {
			one_more = 1;
		}
		if (one_more) {
			qtd_prev = qtd;
			qtd = ehci_qtd_alloc (ehci, flags);
			if (unlikely (!qtd))
				goto cleanup;
			qtd->urb = urb;
			qtd_prev->hw_next = QTD_NEXT(ehci, qtd->qtd_dma);
			list_add_tail (&qtd->qtd_list, head);

			/* never any data in such packets */
			qtd_fill(ehci, qtd, 0, 0, token, 0);
		}
	}

	/* by default, enable interrupt on urb completion */
	if (likely (!(urb->transfer_flags & URB_NO_INTERRUPT)))
		qtd->hw_token |= cpu_to_hc32(ehci, QTD_IOC);
	return head;

cleanup:
	qtd_list_free (ehci, urb, head);
	return NULL;
}

/*-------------------------------------------------------------------------*/

// Would be best to create all qh's from config descriptors,
// when each interface/altsetting is established.  Unlink
// any previous qh and cancel its urbs first; endpoints are
// implicitly reset then (data toggle too).
// That'd mean updating how usbcore talks to HCDs. (2.7?)


/*
 * Each QH holds a qtd list; a QH is used for everything except iso.
 *
 * For interrupt urbs, the scheduler must set the microframe scheduling
 * mask(s) each time the QH gets scheduled.  For highspeed, that's
 * just one microframe in the s-mask.  For split interrupt transactions
 * there are additional complications: c-mask, maybe FSTNs.
 */
static struct ehci_qh *
qh_make (
	struct ehci_hcd		*ehci,
	struct urb		*urb,
	gfp_t			flags
) {
	struct ehci_qh		*qh = ehci_qh_alloc (ehci, flags);
	u32			info1 = 0, info2 = 0;
	int			is_input, type;
	int			maxp = 0;
	struct usb_tt		*tt = urb->dev->tt;
	struct ehci_qh_hw	*hw;

	if (!qh)
		return qh;

	/*
	 * init endpoint/device data for this QH
	 */
	info1 |= usb_pipeendpoint (urb->pipe) << 8;
	info1 |= usb_pipedevice (urb->pipe) << 0;

	is_input = usb_pipein (urb->pipe);
	type = usb_pipetype (urb->pipe);
	maxp = usb_maxpacket (urb->dev, urb->pipe, !is_input);

	/* 1024 byte maxpacket is a hardware ceiling.  High bandwidth
	 * acts like up to 3KB, but is built from smaller packets.
	 */
	if (max_packet(maxp) > 1024) {
		ehci_dbg(ehci, "bogus qh maxpacket %d\n", max_packet(maxp));
		goto done;
	}

	/* Compute interrupt scheduling parameters just once, and save.
	 * - allowing for high bandwidth, how many nsec/uframe are used?
	 * - split transactions need a second CSPLIT uframe; same question
	 * - splits also need a schedule gap (for full/low speed I/O)
	 * - qh has a polling interval
	 *
	 * For control/bulk requests, the HC or TT handles these.
	 */
	if (type == PIPE_INTERRUPT) {
		qh->usecs = NS_TO_US(usb_calc_bus_time(USB_SPEED_HIGH,
				is_input, 0,
				hb_mult(maxp) * max_packet(maxp)));
		qh->start = NO_FRAME;

		if (urb->dev->speed == USB_SPEED_HIGH) {
			qh->c_usecs = 0;
			qh->gap_uf = 0;

			qh->period = urb->interval >> 3;
			if (qh->period == 0 && urb->interval != 1) {
				/* NOTE interval 2 or 4 uframes could work.
				 * But interval 1 scheduling is simpler, and
				 * includes high bandwidth.
				 */
				urb->interval = 1;
			} else if (qh->period > ehci->periodic_size) {
				qh->period = ehci->periodic_size;
				urb->interval = qh->period << 3;
			}
		} else {
			int		think_time;

			/* gap is f(FS/LS transfer times) */
			qh->gap_uf = 1 + usb_calc_bus_time (urb->dev->speed,
					is_input, 0, maxp) / (125 * 1000);

			/* FIXME this just approximates SPLIT/CSPLIT times */
			if (is_input) {		// SPLIT, gap, CSPLIT+DATA
				qh->c_usecs = qh->usecs + HS_USECS (0);
				qh->usecs = HS_USECS (1);
			} else {		// SPLIT+DATA, gap, CSPLIT
				qh->usecs += HS_USECS (1);
				qh->c_usecs = HS_USECS (0);
			}

			think_time = tt ? tt->think_time : 0;
			qh->tt_usecs = NS_TO_US (think_time +
					usb_calc_bus_time (urb->dev->speed,
					is_input, 0, max_packet (maxp)));
			qh->period = urb->interval;
			if (qh->period > ehci->periodic_size) {
				qh->period = ehci->periodic_size;
				urb->interval = qh->period;
			}
		}
	}

	/* support for tt scheduling, and access to toggles */
	qh->dev = urb->dev;

	/* using TT? */
	switch (urb->dev->speed) {
	case USB_SPEED_LOW:
		info1 |= QH_LOW_SPEED;
		/* FALL THROUGH */

	case USB_SPEED_FULL:
		/* EPS 0 means "full" */
		if (type != PIPE_INTERRUPT)
			info1 |= (EHCI_TUNE_RL_TT << 28);
		if (type == PIPE_CONTROL) {
			info1 |= QH_CONTROL_EP;		/* for TT */
			info1 |= QH_TOGGLE_CTL;		/* toggle from qtd */
		}
		info1 |= maxp << 16;

		info2 |= (EHCI_TUNE_MULT_TT << 30);

		/* Some Freescale processors have an erratum in which the
		 * port number in the queue head was 0..N-1 instead of 1..N.
		 */
		if (ehci_has_fsl_portno_bug(ehci))
			info2 |= (urb->dev->ttport-1) << 23;
		else
			info2 |= urb->dev->ttport << 23;

		/* set the address of the TT; for TDI's integrated
		 * root hub tt, leave it zeroed.
		 */
		if (tt && tt->hub != ehci_to_hcd(ehci)->self.root_hub)
			info2 |= tt->hub->devnum << 16;

		/* NOTE:  if (PIPE_INTERRUPT) { scheduler sets c-mask } */

		break;

	case USB_SPEED_HIGH:		/* no TT involved */
		info1 |= QH_HIGH_SPEED;
		if (type == PIPE_CONTROL) {
			info1 |= (EHCI_TUNE_RL_HS << 28);
			info1 |= 64 << 16;	/* usb2 fixed maxpacket */
			info1 |= QH_TOGGLE_CTL;	/* toggle from qtd */
			info2 |= (EHCI_TUNE_MULT_HS << 30);
		} else if (type == PIPE_BULK) {
			info1 |= (EHCI_TUNE_RL_HS << 28);
			/* The USB spec says that high speed bulk endpoints
			 * always use 512 byte maxpacket.  But some device
			 * vendors decided to ignore that, and MSFT is happy
			 * to help them do so.  So now people expect to use
			 * such nonconformant devices with Linux too; sigh.
			 */
			info1 |= max_packet(maxp) << 16;
			info2 |= (EHCI_TUNE_MULT_HS << 30);
		} else {		/* PIPE_INTERRUPT */
			info1 |= max_packet (maxp) << 16;
			info2 |= hb_mult (maxp) << 30;
		}
		break;
	default:
		ehci_dbg(ehci, "bogus dev %p speed %d\n", urb->dev,
			urb->dev->speed);
done:
		qh_destroy(ehci, qh);
		return NULL;
	}

	/* NOTE:  if (PIPE_INTERRUPT) { scheduler sets s-mask } */

	/* init as live, toggle clear */
	qh->qh_state = QH_STATE_IDLE;
	hw = qh->hw;
	hw->hw_info1 = cpu_to_hc32(ehci, info1);
	hw->hw_info2 = cpu_to_hc32(ehci, info2);
	qh->is_out = !is_input;
	usb_settoggle (urb->dev, usb_pipeendpoint (urb->pipe), !is_input, 1);
	return qh;
}

/*-------------------------------------------------------------------------*/

static void enable_async(struct ehci_hcd *ehci)
{
	if (ehci->async_count++)
		return;

	/* Stop waiting to turn off the async schedule */
	ehci->enabled_hrtimer_events &= ~BIT(EHCI_HRTIMER_DISABLE_ASYNC);

	/* Don't start the schedule until ASS is 0 */
	ehci_poll_ASS(ehci);
	turn_on_io_watchdog(ehci);
}

static void disable_async(struct ehci_hcd *ehci)
{
	if (--ehci->async_count)
		return;

	/* The async schedule and unlink lists are supposed to be empty */
	WARN_ON(ehci->async->qh_next.qh || !list_empty(&ehci->async_unlink) ||
			!list_empty(&ehci->async_idle));

	/* Don't turn off the schedule until ASS is 1 */
	ehci_poll_ASS(ehci);
}

/* move qh (and its qtds) onto async queue; maybe enable queue.  */

static void qh_link_async (struct ehci_hcd *ehci, struct ehci_qh *qh)
{
	__hc32		dma = QH_NEXT(ehci, qh->qh_dma);
	struct ehci_qh	*head;

	/* Don't link a QH if there's a Clear-TT-Buffer pending */
	if (unlikely(qh->clearing_tt))
		return;

	WARN_ON(qh->qh_state != QH_STATE_IDLE);

	/* clear halt and/or toggle; and maybe recover from silicon quirk */
	qh_refresh(ehci, qh);

	/* splice right after start */
	head = ehci->async;
	qh->qh_next = head->qh_next;
	qh->hw->hw_next = head->hw->hw_next;
	wmb ();

	head->qh_next.qh = qh;
	head->hw->hw_next = dma;

	qh->qh_state = QH_STATE_LINKED;
	qh->xacterrs = 0;
	qh->exception = 0;
	/* qtd completions reported later by interrupt */

	enable_async(ehci);
}

/*-------------------------------------------------------------------------*/

/*
 * For control/bulk/interrupt, return QH with these TDs appended.
 * Allocates and initializes the QH if necessary.
 * Returns null if it can't allocate a QH it needs to.
 * If the QH has TDs (urbs) already, that's great.
 */
static struct ehci_qh *qh_append_tds (
	struct ehci_hcd		*ehci,
	struct urb		*urb,
	struct list_head	*qtd_list,
	int			epnum,
	void			**ptr
)
{
	struct ehci_qh		*qh = NULL;
	__hc32			qh_addr_mask = cpu_to_hc32(ehci, 0x7f);

	qh = (struct ehci_qh *) *ptr;
	if (unlikely (qh == NULL)) {
		/* can't sleep here, we have ehci->lock... */
		qh = qh_make (ehci, urb, GFP_ATOMIC);
		*ptr = qh;
	}
	if (likely (qh != NULL)) {
		struct ehci_qtd	*qtd;

		if (unlikely (list_empty (qtd_list)))
			qtd = NULL;
		else
			qtd = list_entry (qtd_list->next, struct ehci_qtd,
					qtd_list);

		/* control qh may need patching ... */
		if (unlikely (epnum == 0)) {

                        /* usb_reset_device() briefly reverts to address 0 */
                        if (usb_pipedevice (urb->pipe) == 0)
				qh->hw->hw_info1 &= ~qh_addr_mask;
		}

		/* just one way to queue requests: swap with the dummy qtd.
		 * only hc or qh_refresh() ever modify the overlay.
		 */
		if (likely (qtd != NULL)) {
			struct ehci_qtd		*dummy;
			dma_addr_t		dma;
			__hc32			token;

			/* to avoid racing the HC, use the dummy td instead of
			 * the first td of our list (becomes new dummy).  both
			 * tds stay deactivated until we're done, when the
			 * HC is allowed to fetch the old dummy (4.10.2).
			 */
			token = qtd->hw_token;
			qtd->hw_token = HALT_BIT(ehci);

			dummy = qh->dummy;

			dma = dummy->qtd_dma;
			*dummy = *qtd;
			dummy->qtd_dma = dma;

			list_del (&qtd->qtd_list);
			list_add (&dummy->qtd_list, qtd_list);
			list_splice_tail(qtd_list, &qh->qtd_list);

			ehci_qtd_init(ehci, qtd, qtd->qtd_dma);
			qh->dummy = qtd;

			/* hc must see the new dummy at list end */
			dma = qtd->qtd_dma;
			qtd = list_entry (qh->qtd_list.prev,
					struct ehci_qtd, qtd_list);
			qtd->hw_next = QTD_NEXT(ehci, dma);

			/* let the hc process these next qtds */
			wmb ();
			dummy->hw_token = token;

			urb->hcpriv = qh;
		}
	}
	return qh;
}

/*-------------------------------------------------------------------------*/

static int
submit_async (
	struct ehci_hcd		*ehci,
	struct urb		*urb,
	struct list_head	*qtd_list,
	gfp_t			mem_flags
) {
	int			epnum;
	unsigned long		flags;
	struct ehci_qh		*qh = NULL;
	int			rc;

	epnum = urb->ep->desc.bEndpointAddress;

#ifdef EHCI_URB_TRACE
	{
		struct ehci_qtd *qtd;
		qtd = list_entry(qtd_list->next, struct ehci_qtd, qtd_list);
		ehci_dbg(ehci,
			 "%s %s urb %p ep%d%s len %d, qtd %p [qh %p]\n",
			 __func__, urb->dev->devpath, urb,
			 epnum & 0x0f, (epnum & USB_DIR_IN) ? "in" : "out",
			 urb->transfer_buffer_length,
			 qtd, urb->ep->hcpriv);
	}
#endif

	spin_lock_irqsave (&ehci->lock, flags);
	if (unlikely(!HCD_HW_ACCESSIBLE(ehci_to_hcd(ehci)))) {
		rc = -ESHUTDOWN;
		goto done;
	}
	rc = usb_hcd_link_urb_to_ep(ehci_to_hcd(ehci), urb);
	if (unlikely(rc))
		goto done;

	qh = qh_append_tds(ehci, urb, qtd_list, epnum, &urb->ep->hcpriv);
	if (unlikely(qh == NULL)) {
		usb_hcd_unlink_urb_from_ep(ehci_to_hcd(ehci), urb);
		rc = -ENOMEM;
		goto done;
	}

	/* Control/bulk operations through TTs don't need scheduling,
	 * the HC and TT handle it when the TT has a buffer ready.
	 */
	if (likely (qh->qh_state == QH_STATE_IDLE))
		qh_link_async(ehci, qh);
 done:
	spin_unlock_irqrestore (&ehci->lock, flags);
	if (unlikely (qh == NULL))
		qtd_list_free (ehci, urb, qtd_list);
	return rc;
}

/*-------------------------------------------------------------------------*/
#ifdef CONFIG_USB_HCD_TEST_MODE
/*
 * This function creates the qtds and submits them for the
 * SINGLE_STEP_SET_FEATURE Test.
 * This is done in two parts: first SETUP req for GetDesc is sent then
 * 15 seconds later, the IN stage for GetDesc starts to req data from dev
 *
 * is_setup : i/p arguement decides which of the two stage needs to be
 * performed; TRUE - SETUP and FALSE - IN+STATUS
 * Returns 0 if success
 */
static int submit_single_step_set_feature(
	struct usb_hcd  *hcd,
	struct urb      *urb,
	int             is_setup
) {
	struct ehci_hcd		*ehci = hcd_to_ehci(hcd);
	struct list_head	qtd_list;
	struct list_head	*head;

	struct ehci_qtd		*qtd, *qtd_prev;
	dma_addr_t		buf;
	int			len, maxpacket;
	u32			token;

	INIT_LIST_HEAD(&qtd_list);
	head = &qtd_list;

	/* URBs map to sequences of QTDs:  one logical transaction */
	qtd = ehci_qtd_alloc(ehci, GFP_KERNEL);
	if (unlikely(!qtd))
		return -1;
	list_add_tail(&qtd->qtd_list, head);
	qtd->urb = urb;

	token = QTD_STS_ACTIVE;
	token |= (EHCI_TUNE_CERR << 10);

	len = urb->transfer_buffer_length;
	/*
	 * Check if the request is to perform just the SETUP stage (getDesc)
	 * as in SINGLE_STEP_SET_FEATURE test, DATA stage (IN) happens
	 * 15 secs after the setup
	 */
	if (is_setup) {
		/* SETUP pid */
		qtd_fill(ehci, qtd, urb->setup_dma,
				sizeof(struct usb_ctrlrequest),
				token | (2 /* "setup" */ << 8), 8);

		submit_async(ehci, urb, &qtd_list, GFP_ATOMIC);
		return 0; /*Return now; we shall come back after 15 seconds*/
	}

	/*
	 * IN: data transfer stage:  buffer setup : start the IN txn phase for
	 * the get_Desc SETUP which was sent 15seconds back
	 */
	token ^= QTD_TOGGLE;   /*We need to start IN with DATA-1 Pid-sequence*/
	buf = urb->transfer_dma;

	token |= (1 /* "in" */ << 8);  /*This is IN stage*/

	maxpacket = max_packet(usb_maxpacket(urb->dev, urb->pipe, 0));

	qtd_fill(ehci, qtd, buf, len, token, maxpacket);

	/*
	 * Our IN phase shall always be a short read; so keep the queue running
	 * and let it advance to the next qtd which zero length OUT status
	 */
	qtd->hw_alt_next = EHCI_LIST_END(ehci);

	/* STATUS stage for GetDesc control request */
	token ^= 0x0100;        /* "in" <--> "out"  */
	token |= QTD_TOGGLE;    /* force DATA1 */

	qtd_prev = qtd;
	qtd = ehci_qtd_alloc(ehci, GFP_ATOMIC);
	if (unlikely(!qtd))
		goto cleanup;
	qtd->urb = urb;
	qtd_prev->hw_next = QTD_NEXT(ehci, qtd->qtd_dma);
	list_add_tail(&qtd->qtd_list, head);

	/* dont fill any data in such packets */
	qtd_fill(ehci, qtd, 0, 0, token, 0);

	/* by default, enable interrupt on urb completion */
	if (likely(!(urb->transfer_flags & URB_NO_INTERRUPT)))
		qtd->hw_token |= cpu_to_hc32(ehci, QTD_IOC);

	submit_async(ehci, urb, &qtd_list, GFP_KERNEL);

	return 0;

cleanup:
	qtd_list_free(ehci, urb, head);
	return -1;
}
#endif /* CONFIG_USB_HCD_TEST_MODE */

/*-------------------------------------------------------------------------*/

static void single_unlink_async(struct ehci_hcd *ehci, struct ehci_qh *qh)
{
	struct ehci_qh		*prev;

	/* Add to the end of the list of QHs waiting for the next IAAD */
	qh->qh_state = QH_STATE_UNLINK_WAIT;
	list_add_tail(&qh->unlink_node, &ehci->async_unlink);

	/* Unlink it from the schedule */
	prev = ehci->async;
	while (prev->qh_next.qh != qh)
		prev = prev->qh_next.qh;

	prev->hw->hw_next = qh->hw->hw_next;
	prev->qh_next = qh->qh_next;
	if (ehci->qh_scan_next == qh)
		ehci->qh_scan_next = qh->qh_next.qh;
}

static void start_iaa_cycle(struct ehci_hcd *ehci)
{
	/* Do nothing if an IAA cycle is already running */
	if (ehci->iaa_in_progress)
		return;
	ehci->iaa_in_progress = true;

	/* If the controller isn't running, we don't have to wait for it */
	if (unlikely(ehci->rh_state < EHCI_RH_RUNNING)) {
		end_unlink_async(ehci);

	/* Otherwise start a new IAA cycle */
	} else if (likely(ehci->rh_state == EHCI_RH_RUNNING)) {

		/* Make sure the unlinks are all visible to the hardware */
		wmb();

		ehci_writel(ehci, ehci->command | CMD_IAAD,
				&ehci->regs->command);
		ehci_readl(ehci, &ehci->regs->command);
		ehci_enable_event(ehci, EHCI_HRTIMER_IAA_WATCHDOG, true);
	}
}

/* the async qh for the qtds being unlinked are now gone from the HC */

static void end_unlink_async(struct ehci_hcd *ehci)
{
	struct ehci_qh		*qh;
	bool			early_exit;

	if (ehci->has_synopsys_hc_bug)
		ehci_writel(ehci, (u32) ehci->async->qh_dma,
			    &ehci->regs->async_next);

	/* The current IAA cycle has ended */
	ehci->iaa_in_progress = false;

	if (list_empty(&ehci->async_unlink))
		return;
	qh = list_first_entry(&ehci->async_unlink, struct ehci_qh,
			unlink_node);	/* QH whose IAA cycle just ended */

	/*
	 * If async_unlinking is set then this routine is already running,
	 * either on the stack or on another CPU.
	 */
	early_exit = ehci->async_unlinking;

	/* If the controller isn't running, process all the waiting QHs */
	if (ehci->rh_state < EHCI_RH_RUNNING)
		list_splice_tail_init(&ehci->async_unlink, &ehci->async_idle);

	/*
	 * Intel (?) bug: The HC can write back the overlay region even
	 * after the IAA interrupt occurs.  In self-defense, always go
	 * through two IAA cycles for each QH.
	 */
	else if (qh->qh_state == QH_STATE_UNLINK_WAIT) {
		qh->qh_state = QH_STATE_UNLINK;
		early_exit = true;
	}

	/* Otherwise process only the first waiting QH (NVIDIA bug?) */
	else
		list_move_tail(&qh->unlink_node, &ehci->async_idle);

	/* Start a new IAA cycle if any QHs are waiting for it */
	if (!list_empty(&ehci->async_unlink))
		start_iaa_cycle(ehci);

	/*
	 * Don't allow nesting or concurrent calls,
	 * or wait for the second IAA cycle for the next QH.
	 */
	if (early_exit)
		return;

	/* Process the idle QHs */
	ehci->async_unlinking = true;
	while (!list_empty(&ehci->async_idle)) {
		qh = list_first_entry(&ehci->async_idle, struct ehci_qh,
				unlink_node);
		list_del(&qh->unlink_node);

		qh->qh_state = QH_STATE_IDLE;
		qh->qh_next.qh = NULL;

		if (!list_empty(&qh->qtd_list))
			qh_completions(ehci, qh);
		if (!list_empty(&qh->qtd_list) &&
				ehci->rh_state == EHCI_RH_RUNNING)
			qh_link_async(ehci, qh);
		disable_async(ehci);
	}
	ehci->async_unlinking = false;
}

static void start_unlink_async(struct ehci_hcd *ehci, struct ehci_qh *qh);

static void unlink_empty_async(struct ehci_hcd *ehci)
{
	struct ehci_qh		*qh;
	struct ehci_qh		*qh_to_unlink = NULL;
	int			count = 0;

	/* Find the last async QH which has been empty for a timer cycle */
	for (qh = ehci->async->qh_next.qh; qh; qh = qh->qh_next.qh) {
		if (list_empty(&qh->qtd_list) &&
				qh->qh_state == QH_STATE_LINKED) {
			++count;
			if (qh->unlink_cycle != ehci->async_unlink_cycle)
				qh_to_unlink = qh;
		}
	}

	/* If nothing else is being unlinked, unlink the last empty QH */
	if (list_empty(&ehci->async_unlink) && qh_to_unlink) {
		start_unlink_async(ehci, qh_to_unlink);
		--count;
	}

	/* Other QHs will be handled later */
	if (count > 0) {
		ehci_enable_event(ehci, EHCI_HRTIMER_ASYNC_UNLINKS, true);
		++ehci->async_unlink_cycle;
	}
}

/* The root hub is suspended; unlink all the async QHs */
static void __maybe_unused unlink_empty_async_suspended(struct ehci_hcd *ehci)
{
	struct ehci_qh		*qh;

	while (ehci->async->qh_next.qh) {
		qh = ehci->async->qh_next.qh;
		WARN_ON(!list_empty(&qh->qtd_list));
		single_unlink_async(ehci, qh);
	}
	start_iaa_cycle(ehci);
}

/* makes sure the async qh will become idle */
/* caller must own ehci->lock */

static void start_unlink_async(struct ehci_hcd *ehci, struct ehci_qh *qh)
{
	/* If the QH isn't linked then there's nothing we can do. */
	if (qh->qh_state != QH_STATE_LINKED)
		return;

	single_unlink_async(ehci, qh);
	start_iaa_cycle(ehci);
}

/*-------------------------------------------------------------------------*/

static void scan_async (struct ehci_hcd *ehci)
{
	struct ehci_qh		*qh;
	bool			check_unlinks_later = false;

	ehci->qh_scan_next = ehci->async->qh_next.qh;
	while (ehci->qh_scan_next) {
		qh = ehci->qh_scan_next;
		ehci->qh_scan_next = qh->qh_next.qh;

		/* clean any finished work for this qh */
		if (!list_empty(&qh->qtd_list)) {
			int temp;

			/*
			 * Unlinks could happen here; completion reporting
			 * drops the lock.  That's why ehci->qh_scan_next
			 * always holds the next qh to scan; if the next qh
			 * gets unlinked then ehci->qh_scan_next is adjusted
			 * in single_unlink_async().
			 */
			temp = qh_completions(ehci, qh);
			if (unlikely(temp)) {
				start_unlink_async(ehci, qh);
			} else if (list_empty(&qh->qtd_list)
					&& qh->qh_state == QH_STATE_LINKED) {
				qh->unlink_cycle = ehci->async_unlink_cycle;
				check_unlinks_later = true;
			}
		}
	}

	/*
	 * Unlink empty entries, reducing DMA usage as well
	 * as HCD schedule-scanning costs.  Delay for any qh
	 * we just scanned, there's a not-unusual case that it
	 * doesn't stay idle for long.
	 */
	if (check_unlinks_later && ehci->rh_state == EHCI_RH_RUNNING &&
			!(ehci->enabled_hrtimer_events &
				BIT(EHCI_HRTIMER_ASYNC_UNLINKS))) {
		ehci_enable_event(ehci, EHCI_HRTIMER_ASYNC_UNLINKS, true);
		++ehci->async_unlink_cycle;
	}
}
