/*
 * Universal Host Controller Interface driver for USB.
 *
 * Maintainer: Alan Stern <stern@rowland.harvard.edu>
 *
 * (C) Copyright 1999 Linus Torvalds
 * (C) Copyright 1999-2002 Johannes Erdfelt, johannes@erdfelt.com
 * (C) Copyright 1999 Randy Dunlap
 * (C) Copyright 1999 Georg Acher, acher@in.tum.de
 * (C) Copyright 1999 Deti Fliegl, deti@fliegl.de
 * (C) Copyright 1999 Thomas Sailer, sailer@ife.ee.ethz.ch
 * (C) Copyright 1999 Roman Weissgaerber, weissg@vienna.at
 * (C) Copyright 2000 Yggdrasil Computing, Inc. (port of new PCI interface
 *               support from usb-ohci.c by Adam Richter, adam@yggdrasil.com).
 * (C) Copyright 1999 Gregory P. Smith (from usb-ohci.c)
 * (C) Copyright 2004 Alan Stern, stern@rowland.harvard.edu
 */

static int uhci_urb_dequeue(struct usb_hcd *hcd, struct urb *urb);
static void uhci_unlink_generic(struct uhci_hcd *uhci, struct urb *urb);
static void uhci_remove_pending_urbps(struct uhci_hcd *uhci);
static void uhci_free_pending_qhs(struct uhci_hcd *uhci);
static void uhci_free_pending_tds(struct uhci_hcd *uhci);

/*
 * Technically, updating td->status here is a race, but it's not really a
 * problem. The worst that can happen is that we set the IOC bit again
 * generating a spurious interrupt. We could fix this by creating another
 * QH and leaving the IOC bit always set, but then we would have to play
 * games with the FSBR code to make sure we get the correct order in all
 * the cases. I don't think it's worth the effort
 */
static inline void uhci_set_next_interrupt(struct uhci_hcd *uhci)
{
	if (uhci->is_stopped)
		mod_timer(&uhci_to_hcd(uhci)->rh_timer, jiffies);
	uhci->term_td->status |= cpu_to_le32(TD_CTRL_IOC); 
}

static inline void uhci_clear_next_interrupt(struct uhci_hcd *uhci)
{
	uhci->term_td->status &= ~cpu_to_le32(TD_CTRL_IOC);
}

static inline void uhci_moveto_complete(struct uhci_hcd *uhci, 
					struct urb_priv *urbp)
{
	list_move_tail(&urbp->urb_list, &uhci->complete_list);
}

static struct uhci_td *uhci_alloc_td(struct uhci_hcd *uhci)
{
	dma_addr_t dma_handle;
	struct uhci_td *td;

	td = dma_pool_alloc(uhci->td_pool, GFP_ATOMIC, &dma_handle);
	if (!td)
		return NULL;

	td->dma_handle = dma_handle;

	td->link = UHCI_PTR_TERM;
	td->buffer = 0;

	td->frame = -1;

	INIT_LIST_HEAD(&td->list);
	INIT_LIST_HEAD(&td->remove_list);
	INIT_LIST_HEAD(&td->fl_list);

	return td;
}

static inline void uhci_fill_td(struct uhci_td *td, u32 status,
		u32 token, u32 buffer)
{
	td->status = cpu_to_le32(status);
	td->token = cpu_to_le32(token);
	td->buffer = cpu_to_le32(buffer);
}

/*
 * We insert Isochronous URB's directly into the frame list at the beginning
 */
static void uhci_insert_td_frame_list(struct uhci_hcd *uhci, struct uhci_td *td, unsigned framenum)
{
	framenum &= (UHCI_NUMFRAMES - 1);

	td->frame = framenum;

	/* Is there a TD already mapped there? */
	if (uhci->fl->frame_cpu[framenum]) {
		struct uhci_td *ftd, *ltd;

		ftd = uhci->fl->frame_cpu[framenum];
		ltd = list_entry(ftd->fl_list.prev, struct uhci_td, fl_list);

		list_add_tail(&td->fl_list, &ftd->fl_list);

		td->link = ltd->link;
		wmb();
		ltd->link = cpu_to_le32(td->dma_handle);
	} else {
		td->link = uhci->fl->frame[framenum];
		wmb();
		uhci->fl->frame[framenum] = cpu_to_le32(td->dma_handle);
		uhci->fl->frame_cpu[framenum] = td;
	}
}

static void uhci_remove_td(struct uhci_hcd *uhci, struct uhci_td *td)
{
	/* If it's not inserted, don't remove it */
	if (td->frame == -1 && list_empty(&td->fl_list))
		return;

	if (td->frame != -1 && uhci->fl->frame_cpu[td->frame] == td) {
		if (list_empty(&td->fl_list)) {
			uhci->fl->frame[td->frame] = td->link;
			uhci->fl->frame_cpu[td->frame] = NULL;
		} else {
			struct uhci_td *ntd;

			ntd = list_entry(td->fl_list.next, struct uhci_td, fl_list);
			uhci->fl->frame[td->frame] = cpu_to_le32(ntd->dma_handle);
			uhci->fl->frame_cpu[td->frame] = ntd;
		}
	} else {
		struct uhci_td *ptd;

		ptd = list_entry(td->fl_list.prev, struct uhci_td, fl_list);
		ptd->link = td->link;
	}

	wmb();
	td->link = UHCI_PTR_TERM;

	list_del_init(&td->fl_list);
	td->frame = -1;
}

/*
 * Inserts a td list into qh.
 */
static void uhci_insert_tds_in_qh(struct uhci_qh *qh, struct urb *urb, __le32 breadth)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	struct uhci_td *td;
	__le32 *plink;

	/* Ordering isn't important here yet since the QH hasn't been */
	/* inserted into the schedule yet */
	plink = &qh->element;
	list_for_each_entry(td, &urbp->td_list, list) {
		*plink = cpu_to_le32(td->dma_handle) | breadth;
		plink = &td->link;
	}
	*plink = UHCI_PTR_TERM;
}

static void uhci_free_td(struct uhci_hcd *uhci, struct uhci_td *td)
{
	if (!list_empty(&td->list))
		dev_warn(uhci_dev(uhci), "td %p still in list!\n", td);
	if (!list_empty(&td->remove_list))
		dev_warn(uhci_dev(uhci), "td %p still in remove_list!\n", td);
	if (!list_empty(&td->fl_list))
		dev_warn(uhci_dev(uhci), "td %p still in fl_list!\n", td);

	dma_pool_free(uhci->td_pool, td, td->dma_handle);
}

static struct uhci_qh *uhci_alloc_qh(struct uhci_hcd *uhci)
{
	dma_addr_t dma_handle;
	struct uhci_qh *qh;

	qh = dma_pool_alloc(uhci->qh_pool, GFP_ATOMIC, &dma_handle);
	if (!qh)
		return NULL;

	qh->dma_handle = dma_handle;

	qh->element = UHCI_PTR_TERM;
	qh->link = UHCI_PTR_TERM;

	qh->urbp = NULL;

	INIT_LIST_HEAD(&qh->list);
	INIT_LIST_HEAD(&qh->remove_list);

	return qh;
}

static void uhci_free_qh(struct uhci_hcd *uhci, struct uhci_qh *qh)
{
	if (!list_empty(&qh->list))
		dev_warn(uhci_dev(uhci), "qh %p list not empty!\n", qh);
	if (!list_empty(&qh->remove_list))
		dev_warn(uhci_dev(uhci), "qh %p still in remove_list!\n", qh);

	dma_pool_free(uhci->qh_pool, qh, qh->dma_handle);
}

/*
 * Append this urb's qh after the last qh in skelqh->list
 *
 * Note that urb_priv.queue_list doesn't have a separate queue head;
 * it's a ring with every element "live".
 */
static void uhci_insert_qh(struct uhci_hcd *uhci, struct uhci_qh *skelqh, struct urb *urb)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	struct urb_priv *turbp;
	struct uhci_qh *lqh;

	/* Grab the last QH */
	lqh = list_entry(skelqh->list.prev, struct uhci_qh, list);

	/* Point to the next skelqh */
	urbp->qh->link = lqh->link;
	wmb();				/* Ordering is important */

	/*
	 * Patch QHs for previous endpoint's queued URBs?  HC goes
	 * here next, not to the next skelqh it now points to.
	 *
	 *    lqh --> td ... --> qh ... --> td --> qh ... --> td
	 *     |                 |                 |
	 *     v                 v                 v
	 *     +<----------------+-----------------+
	 *     v
	 *    newqh --> td ... --> td
	 *     |
	 *     v
	 *    ...
	 *
	 * The HC could see (and use!) any of these as we write them.
	 */
	lqh->link = cpu_to_le32(urbp->qh->dma_handle) | UHCI_PTR_QH;
	if (lqh->urbp) {
		list_for_each_entry(turbp, &lqh->urbp->queue_list, queue_list)
			turbp->qh->link = lqh->link;
	}

	list_add_tail(&urbp->qh->list, &skelqh->list);
}

/*
 * Start removal of QH from schedule; it finishes next frame.
 * TDs should be unlinked before this is called.
 */
static void uhci_remove_qh(struct uhci_hcd *uhci, struct uhci_qh *qh)
{
	struct uhci_qh *pqh;
	__le32 newlink;

	if (!qh)
		return;

	/*
	 * Only go through the hoops if it's actually linked in
	 */
	if (!list_empty(&qh->list)) {

		/* If our queue is nonempty, make the next URB the head */
		if (!list_empty(&qh->urbp->queue_list)) {
			struct urb_priv *nurbp;

			nurbp = list_entry(qh->urbp->queue_list.next,
					struct urb_priv, queue_list);
			nurbp->queued = 0;
			list_add(&nurbp->qh->list, &qh->list);
			newlink = cpu_to_le32(nurbp->qh->dma_handle) | UHCI_PTR_QH;
		} else
			newlink = qh->link;

		/* Fix up the previous QH's queue to link to either
		 * the new head of this queue or the start of the
		 * next endpoint's queue. */
		pqh = list_entry(qh->list.prev, struct uhci_qh, list);
		pqh->link = newlink;
		if (pqh->urbp) {
			struct urb_priv *turbp;

			list_for_each_entry(turbp, &pqh->urbp->queue_list,
					queue_list)
				turbp->qh->link = newlink;
		}
		wmb();

		/* Leave qh->link in case the HC is on the QH now, it will */
		/* continue the rest of the schedule */
		qh->element = UHCI_PTR_TERM;

		list_del_init(&qh->list);
	}

	list_del_init(&qh->urbp->queue_list);
	qh->urbp = NULL;

	uhci_get_current_frame_number(uhci);
	if (uhci->frame_number + uhci->is_stopped != uhci->qh_remove_age) {
		uhci_free_pending_qhs(uhci);
		uhci->qh_remove_age = uhci->frame_number;
	}

	/* Check to see if the remove list is empty. Set the IOC bit */
	/* to force an interrupt so we can remove the QH */
	if (list_empty(&uhci->qh_remove_list))
		uhci_set_next_interrupt(uhci);

	list_add(&qh->remove_list, &uhci->qh_remove_list);
}

static int uhci_fixup_toggle(struct urb *urb, unsigned int toggle)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	struct uhci_td *td;

	list_for_each_entry(td, &urbp->td_list, list) {
		if (toggle)
			td->token |= cpu_to_le32(TD_TOKEN_TOGGLE);
		else
			td->token &= ~cpu_to_le32(TD_TOKEN_TOGGLE);

		toggle ^= 1;
	}

	return toggle;
}

/* This function will append one URB's QH to another URB's QH. This is for */
/* queuing interrupt, control or bulk transfers */
static void uhci_append_queued_urb(struct uhci_hcd *uhci, struct urb *eurb, struct urb *urb)
{
	struct urb_priv *eurbp, *urbp, *furbp, *lurbp;
	struct uhci_td *lltd;

	eurbp = eurb->hcpriv;
	urbp = urb->hcpriv;

	/* Find the first URB in the queue */
	furbp = eurbp;
	if (eurbp->queued) {
		list_for_each_entry(furbp, &eurbp->queue_list, queue_list)
			if (!furbp->queued)
				break;
	}

	lurbp = list_entry(furbp->queue_list.prev, struct urb_priv, queue_list);

	lltd = list_entry(lurbp->td_list.prev, struct uhci_td, list);

	/* Control transfers always start with toggle 0 */
	if (!usb_pipecontrol(urb->pipe))
		usb_settoggle(urb->dev, usb_pipeendpoint(urb->pipe),
				usb_pipeout(urb->pipe),
				uhci_fixup_toggle(urb,
					uhci_toggle(td_token(lltd)) ^ 1));

	/* All qh's in the queue need to link to the next queue */
	urbp->qh->link = eurbp->qh->link;

	wmb();			/* Make sure we flush everything */

	lltd->link = cpu_to_le32(urbp->qh->dma_handle) | UHCI_PTR_QH;

	list_add_tail(&urbp->queue_list, &furbp->queue_list);

	urbp->queued = 1;
}

static void uhci_delete_queued_urb(struct uhci_hcd *uhci, struct urb *urb)
{
	struct urb_priv *urbp, *nurbp, *purbp, *turbp;
	struct uhci_td *pltd;
	unsigned int toggle;

	urbp = urb->hcpriv;

	if (list_empty(&urbp->queue_list))
		return;

	nurbp = list_entry(urbp->queue_list.next, struct urb_priv, queue_list);

	/*
	 * Fix up the toggle for the following URBs in the queue.
	 * Only needed for bulk and interrupt: control and isochronous
	 * endpoints don't propagate toggles between messages.
	 */
	if (usb_pipebulk(urb->pipe) || usb_pipeint(urb->pipe)) {
		if (!urbp->queued)
			/* We just set the toggle in uhci_unlink_generic */
			toggle = usb_gettoggle(urb->dev,
					usb_pipeendpoint(urb->pipe),
					usb_pipeout(urb->pipe));
		else {
			/* If we're in the middle of the queue, grab the */
			/* toggle from the TD previous to us */
			purbp = list_entry(urbp->queue_list.prev,
					struct urb_priv, queue_list);
			pltd = list_entry(purbp->td_list.prev,
					struct uhci_td, list);
			toggle = uhci_toggle(td_token(pltd)) ^ 1;
		}

		list_for_each_entry(turbp, &urbp->queue_list, queue_list) {
			if (!turbp->queued)
				break;
			toggle = uhci_fixup_toggle(turbp->urb, toggle);
		}

		usb_settoggle(urb->dev, usb_pipeendpoint(urb->pipe),
				usb_pipeout(urb->pipe), toggle);
	}

	if (urbp->queued) {
		/* We're somewhere in the middle (or end).  The case where
		 * we're at the head is handled in uhci_remove_qh(). */
		purbp = list_entry(urbp->queue_list.prev, struct urb_priv,
				queue_list);

		pltd = list_entry(purbp->td_list.prev, struct uhci_td, list);
		if (nurbp->queued)
			pltd->link = cpu_to_le32(nurbp->qh->dma_handle) | UHCI_PTR_QH;
		else
			/* The next URB happens to be the beginning, so */
			/*  we're the last, end the chain */
			pltd->link = UHCI_PTR_TERM;
	}

	/* urbp->queue_list is handled in uhci_remove_qh() */
}

static struct urb_priv *uhci_alloc_urb_priv(struct uhci_hcd *uhci, struct urb *urb)
{
	struct urb_priv *urbp;

	urbp = kmem_cache_alloc(uhci_up_cachep, SLAB_ATOMIC);
	if (!urbp)
		return NULL;

	memset((void *)urbp, 0, sizeof(*urbp));

	urbp->inserttime = jiffies;
	urbp->fsbrtime = jiffies;
	urbp->urb = urb;
	
	INIT_LIST_HEAD(&urbp->td_list);
	INIT_LIST_HEAD(&urbp->queue_list);
	INIT_LIST_HEAD(&urbp->urb_list);

	list_add_tail(&urbp->urb_list, &uhci->urb_list);

	urb->hcpriv = urbp;

	return urbp;
}

static void uhci_add_td_to_urb(struct urb *urb, struct uhci_td *td)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;

	td->urb = urb;

	list_add_tail(&td->list, &urbp->td_list);
}

static void uhci_remove_td_from_urb(struct uhci_td *td)
{
	if (list_empty(&td->list))
		return;

	list_del_init(&td->list);

	td->urb = NULL;
}

static void uhci_destroy_urb_priv(struct uhci_hcd *uhci, struct urb *urb)
{
	struct uhci_td *td, *tmp;
	struct urb_priv *urbp;

	urbp = (struct urb_priv *)urb->hcpriv;
	if (!urbp)
		return;

	if (!list_empty(&urbp->urb_list))
		dev_warn(uhci_dev(uhci), "urb %p still on uhci->urb_list "
				"or uhci->remove_list!\n", urb);

	uhci_get_current_frame_number(uhci);
	if (uhci->frame_number + uhci->is_stopped != uhci->td_remove_age) {
		uhci_free_pending_tds(uhci);
		uhci->td_remove_age = uhci->frame_number;
	}

	/* Check to see if the remove list is empty. Set the IOC bit */
	/* to force an interrupt so we can remove the TD's*/
	if (list_empty(&uhci->td_remove_list))
		uhci_set_next_interrupt(uhci);

	list_for_each_entry_safe(td, tmp, &urbp->td_list, list) {
		uhci_remove_td_from_urb(td);
		uhci_remove_td(uhci, td);
		list_add(&td->remove_list, &uhci->td_remove_list);
	}

	urb->hcpriv = NULL;
	kmem_cache_free(uhci_up_cachep, urbp);
}

static void uhci_inc_fsbr(struct uhci_hcd *uhci, struct urb *urb)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;

	if ((!(urb->transfer_flags & URB_NO_FSBR)) && !urbp->fsbr) {
		urbp->fsbr = 1;
		if (!uhci->fsbr++ && !uhci->fsbrtimeout)
			uhci->skel_term_qh->link = cpu_to_le32(uhci->skel_fs_control_qh->dma_handle) | UHCI_PTR_QH;
	}
}

static void uhci_dec_fsbr(struct uhci_hcd *uhci, struct urb *urb)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;

	if ((!(urb->transfer_flags & URB_NO_FSBR)) && urbp->fsbr) {
		urbp->fsbr = 0;
		if (!--uhci->fsbr)
			uhci->fsbrtimeout = jiffies + FSBR_DELAY;
	}
}

/*
 * Map status to standard result codes
 *
 * <status> is (td_status(td) & 0xF60000), a.k.a.
 * uhci_status_bits(td_status(td)).
 * Note: <status> does not include the TD_CTRL_NAK bit.
 * <dir_out> is True for output TDs and False for input TDs.
 */
static int uhci_map_status(int status, int dir_out)
{
	if (!status)
		return 0;
	if (status & TD_CTRL_BITSTUFF)			/* Bitstuff error */
		return -EPROTO;
	if (status & TD_CTRL_CRCTIMEO) {		/* CRC/Timeout */
		if (dir_out)
			return -EPROTO;
		else
			return -EILSEQ;
	}
	if (status & TD_CTRL_BABBLE)			/* Babble */
		return -EOVERFLOW;
	if (status & TD_CTRL_DBUFERR)			/* Buffer error */
		return -ENOSR;
	if (status & TD_CTRL_STALLED)			/* Stalled */
		return -EPIPE;
	WARN_ON(status & TD_CTRL_ACTIVE);		/* Active */
	return 0;
}

/*
 * Control transfers
 */
static int uhci_submit_control(struct uhci_hcd *uhci, struct urb *urb, struct urb *eurb)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	struct uhci_td *td;
	struct uhci_qh *qh, *skelqh;
	unsigned long destination, status;
	int maxsze = usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe));
	int len = urb->transfer_buffer_length;
	dma_addr_t data = urb->transfer_dma;

	/* The "pipe" thing contains the destination in bits 8--18 */
	destination = (urb->pipe & PIPE_DEVEP_MASK) | USB_PID_SETUP;

	/* 3 errors */
	status = TD_CTRL_ACTIVE | uhci_maxerr(3);
	if (urb->dev->speed == USB_SPEED_LOW)
		status |= TD_CTRL_LS;

	/*
	 * Build the TD for the control request setup packet
	 */
	td = uhci_alloc_td(uhci);
	if (!td)
		return -ENOMEM;

	uhci_add_td_to_urb(urb, td);
	uhci_fill_td(td, status, destination | uhci_explen(7),
		urb->setup_dma);

	/*
	 * If direction is "send", change the packet ID from SETUP (0x2D)
	 * to OUT (0xE1).  Else change it from SETUP to IN (0x69) and
	 * set Short Packet Detect (SPD) for all data packets.
	 */
	if (usb_pipeout(urb->pipe))
		destination ^= (USB_PID_SETUP ^ USB_PID_OUT);
	else {
		destination ^= (USB_PID_SETUP ^ USB_PID_IN);
		status |= TD_CTRL_SPD;
	}

	/*
	 * Build the DATA TD's
	 */
	while (len > 0) {
		int pktsze = len;

		if (pktsze > maxsze)
			pktsze = maxsze;

		td = uhci_alloc_td(uhci);
		if (!td)
			return -ENOMEM;

		/* Alternate Data0/1 (start with Data1) */
		destination ^= TD_TOKEN_TOGGLE;
	
		uhci_add_td_to_urb(urb, td);
		uhci_fill_td(td, status, destination | uhci_explen(pktsze - 1),
			data);

		data += pktsze;
		len -= pktsze;
	}

	/*
	 * Build the final TD for control status 
	 */
	td = uhci_alloc_td(uhci);
	if (!td)
		return -ENOMEM;

	/*
	 * It's IN if the pipe is an output pipe or we're not expecting
	 * data back.
	 */
	destination &= ~TD_TOKEN_PID_MASK;
	if (usb_pipeout(urb->pipe) || !urb->transfer_buffer_length)
		destination |= USB_PID_IN;
	else
		destination |= USB_PID_OUT;

	destination |= TD_TOKEN_TOGGLE;		/* End in Data1 */

	status &= ~TD_CTRL_SPD;

	uhci_add_td_to_urb(urb, td);
	uhci_fill_td(td, status | TD_CTRL_IOC,
		destination | uhci_explen(UHCI_NULL_DATA_SIZE), 0);

	qh = uhci_alloc_qh(uhci);
	if (!qh)
		return -ENOMEM;

	urbp->qh = qh;
	qh->urbp = urbp;

	uhci_insert_tds_in_qh(qh, urb, UHCI_PTR_BREADTH);

	/* Low-speed transfers get a different queue, and won't hog the bus.
	 * Also, some devices enumerate better without FSBR; the easiest way
	 * to do that is to put URBs on the low-speed queue while the device
	 * is in the DEFAULT state. */
	if (urb->dev->speed == USB_SPEED_LOW ||
			urb->dev->state == USB_STATE_DEFAULT)
		skelqh = uhci->skel_ls_control_qh;
	else {
		skelqh = uhci->skel_fs_control_qh;
		uhci_inc_fsbr(uhci, urb);
	}

	if (eurb)
		uhci_append_queued_urb(uhci, eurb, urb);
	else
		uhci_insert_qh(uhci, skelqh, urb);

	return -EINPROGRESS;
}

/*
 * If control-IN transfer was short, the status packet wasn't sent.
 * This routine changes the element pointer in the QH to point at the
 * status TD.  It's safe to do this even while the QH is live, because
 * the hardware only updates the element pointer following a successful
 * transfer.  The inactive TD for the short packet won't cause an update,
 * so the pointer won't get overwritten.  The next time the controller
 * sees this QH, it will send the status packet.
 */
static int usb_control_retrigger_status(struct uhci_hcd *uhci, struct urb *urb)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	struct uhci_td *td;

	urbp->short_control_packet = 1;

	td = list_entry(urbp->td_list.prev, struct uhci_td, list);
	urbp->qh->element = cpu_to_le32(td->dma_handle);

	return -EINPROGRESS;
}


static int uhci_result_control(struct uhci_hcd *uhci, struct urb *urb)
{
	struct list_head *tmp, *head;
	struct urb_priv *urbp = urb->hcpriv;
	struct uhci_td *td;
	unsigned int status;
	int ret = 0;

	if (list_empty(&urbp->td_list))
		return -EINVAL;

	head = &urbp->td_list;

	if (urbp->short_control_packet) {
		tmp = head->prev;
		goto status_stage;
	}

	tmp = head->next;
	td = list_entry(tmp, struct uhci_td, list);

	/* The first TD is the SETUP stage, check the status, but skip */
	/*  the count */
	status = uhci_status_bits(td_status(td));
	if (status & TD_CTRL_ACTIVE)
		return -EINPROGRESS;

	if (status)
		goto td_error;

	urb->actual_length = 0;

	/* The rest of the TD's (but the last) are data */
	tmp = tmp->next;
	while (tmp != head && tmp->next != head) {
		unsigned int ctrlstat;

		td = list_entry(tmp, struct uhci_td, list);
		tmp = tmp->next;

		ctrlstat = td_status(td);
		status = uhci_status_bits(ctrlstat);
		if (status & TD_CTRL_ACTIVE)
			return -EINPROGRESS;

		urb->actual_length += uhci_actual_length(ctrlstat);

		if (status)
			goto td_error;

		/* Check to see if we received a short packet */
		if (uhci_actual_length(ctrlstat) <
				uhci_expected_length(td_token(td))) {
			if (urb->transfer_flags & URB_SHORT_NOT_OK) {
				ret = -EREMOTEIO;
				goto err;
			}

			if (uhci_packetid(td_token(td)) == USB_PID_IN)
				return usb_control_retrigger_status(uhci, urb);
			else
				return 0;
		}
	}

status_stage:
	td = list_entry(tmp, struct uhci_td, list);

	/* Control status stage */
	status = td_status(td);

#ifdef I_HAVE_BUGGY_APC_BACKUPS
	/* APC BackUPS Pro kludge */
	/* It tries to send all of the descriptor instead of the amount */
	/*  we requested */
	if (status & TD_CTRL_IOC &&	/* IOC is masked out by uhci_status_bits */
	    status & TD_CTRL_ACTIVE &&
	    status & TD_CTRL_NAK)
		return 0;
#endif

	status = uhci_status_bits(status);
	if (status & TD_CTRL_ACTIVE)
		return -EINPROGRESS;

	if (status)
		goto td_error;

	return 0;

td_error:
	ret = uhci_map_status(status, uhci_packetout(td_token(td)));

err:
	if ((debug == 1 && ret != -EPIPE) || debug > 1) {
		/* Some debugging code */
		dev_dbg(uhci_dev(uhci), "%s: failed with status %x\n",
				__FUNCTION__, status);

		if (errbuf) {
			/* Print the chain for debugging purposes */
			uhci_show_qh(urbp->qh, errbuf, ERRBUF_LEN, 0);

			lprintk(errbuf);
		}
	}

	return ret;
}

/*
 * Common submit for bulk and interrupt
 */
static int uhci_submit_common(struct uhci_hcd *uhci, struct urb *urb, struct urb *eurb, struct uhci_qh *skelqh)
{
	struct uhci_td *td;
	struct uhci_qh *qh;
	unsigned long destination, status;
	int maxsze = usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe));
	int len = urb->transfer_buffer_length;
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	dma_addr_t data = urb->transfer_dma;

	if (len < 0)
		return -EINVAL;

	/* The "pipe" thing contains the destination in bits 8--18 */
	destination = (urb->pipe & PIPE_DEVEP_MASK) | usb_packetid(urb->pipe);

	status = uhci_maxerr(3) | TD_CTRL_ACTIVE;
	if (urb->dev->speed == USB_SPEED_LOW)
		status |= TD_CTRL_LS;
	if (usb_pipein(urb->pipe))
		status |= TD_CTRL_SPD;

	/*
	 * Build the DATA TD's
	 */
	do {	/* Allow zero length packets */
		int pktsze = maxsze;

		if (pktsze >= len) {
			pktsze = len;
			if (!(urb->transfer_flags & URB_SHORT_NOT_OK))
				status &= ~TD_CTRL_SPD;
		}

		td = uhci_alloc_td(uhci);
		if (!td)
			return -ENOMEM;

		uhci_add_td_to_urb(urb, td);
		uhci_fill_td(td, status, destination | uhci_explen(pktsze - 1) |
			(usb_gettoggle(urb->dev, usb_pipeendpoint(urb->pipe),
			 usb_pipeout(urb->pipe)) << TD_TOKEN_TOGGLE_SHIFT),
			data);

		data += pktsze;
		len -= maxsze;

		usb_dotoggle(urb->dev, usb_pipeendpoint(urb->pipe),
			usb_pipeout(urb->pipe));
	} while (len > 0);

	/*
	 * URB_ZERO_PACKET means adding a 0-length packet, if direction
	 * is OUT and the transfer_length was an exact multiple of maxsze,
	 * hence (len = transfer_length - N * maxsze) == 0
	 * however, if transfer_length == 0, the zero packet was already
	 * prepared above.
	 */
	if (usb_pipeout(urb->pipe) && (urb->transfer_flags & URB_ZERO_PACKET) &&
	    !len && urb->transfer_buffer_length) {
		td = uhci_alloc_td(uhci);
		if (!td)
			return -ENOMEM;

		uhci_add_td_to_urb(urb, td);
		uhci_fill_td(td, status, destination | uhci_explen(UHCI_NULL_DATA_SIZE) |
			(usb_gettoggle(urb->dev, usb_pipeendpoint(urb->pipe),
			 usb_pipeout(urb->pipe)) << TD_TOKEN_TOGGLE_SHIFT),
			data);

		usb_dotoggle(urb->dev, usb_pipeendpoint(urb->pipe),
			usb_pipeout(urb->pipe));
	}

	/* Set the interrupt-on-completion flag on the last packet.
	 * A more-or-less typical 4 KB URB (= size of one memory page)
	 * will require about 3 ms to transfer; that's a little on the
	 * fast side but not enough to justify delaying an interrupt
	 * more than 2 or 3 URBs, so we will ignore the URB_NO_INTERRUPT
	 * flag setting. */
	td->status |= cpu_to_le32(TD_CTRL_IOC);

	qh = uhci_alloc_qh(uhci);
	if (!qh)
		return -ENOMEM;

	urbp->qh = qh;
	qh->urbp = urbp;

	/* Always breadth first */
	uhci_insert_tds_in_qh(qh, urb, UHCI_PTR_BREADTH);

	if (eurb)
		uhci_append_queued_urb(uhci, eurb, urb);
	else
		uhci_insert_qh(uhci, skelqh, urb);

	return -EINPROGRESS;
}

/*
 * Common result for bulk and interrupt
 */
static int uhci_result_common(struct uhci_hcd *uhci, struct urb *urb)
{
	struct urb_priv *urbp = urb->hcpriv;
	struct uhci_td *td;
	unsigned int status = 0;
	int ret = 0;

	urb->actual_length = 0;

	list_for_each_entry(td, &urbp->td_list, list) {
		unsigned int ctrlstat = td_status(td);

		status = uhci_status_bits(ctrlstat);
		if (status & TD_CTRL_ACTIVE)
			return -EINPROGRESS;

		urb->actual_length += uhci_actual_length(ctrlstat);

		if (status)
			goto td_error;

		if (uhci_actual_length(ctrlstat) <
				uhci_expected_length(td_token(td))) {
			if (urb->transfer_flags & URB_SHORT_NOT_OK) {
				ret = -EREMOTEIO;
				goto err;
			} else
				return 0;
		}
	}

	return 0;

td_error:
	ret = uhci_map_status(status, uhci_packetout(td_token(td)));

err:
	/* 
	 * Enable this chunk of code if you want to see some more debugging.
	 * But be careful, it has the tendancy to starve out khubd and prevent
	 * disconnects from happening successfully if you have a slow debug
	 * log interface (like a serial console.
	 */
#if 0
	if ((debug == 1 && ret != -EPIPE) || debug > 1) {
		/* Some debugging code */
		dev_dbg(uhci_dev(uhci), "%s: failed with status %x\n",
				__FUNCTION__, status);

		if (errbuf) {
			/* Print the chain for debugging purposes */
			uhci_show_qh(urbp->qh, errbuf, ERRBUF_LEN, 0);

			lprintk(errbuf);
		}
	}
#endif
	return ret;
}

static inline int uhci_submit_bulk(struct uhci_hcd *uhci, struct urb *urb, struct urb *eurb)
{
	int ret;

	/* Can't have low-speed bulk transfers */
	if (urb->dev->speed == USB_SPEED_LOW)
		return -EINVAL;

	ret = uhci_submit_common(uhci, urb, eurb, uhci->skel_bulk_qh);
	if (ret == -EINPROGRESS)
		uhci_inc_fsbr(uhci, urb);

	return ret;
}

static inline int uhci_submit_interrupt(struct uhci_hcd *uhci, struct urb *urb, struct urb *eurb)
{
	/* USB 1.1 interrupt transfers only involve one packet per interval;
	 * that's the uhci_submit_common() "breadth first" policy.  Drivers
	 * can submit urbs of any length, but longer ones might need many
	 * intervals to complete.
	 */
	return uhci_submit_common(uhci, urb, eurb, uhci->skelqh[__interval_to_skel(urb->interval)]);
}

/*
 * Isochronous transfers
 */
static int isochronous_find_limits(struct uhci_hcd *uhci, struct urb *urb, unsigned int *start, unsigned int *end)
{
	struct urb *last_urb = NULL;
	struct urb_priv *up;
	int ret = 0;

	list_for_each_entry(up, &uhci->urb_list, urb_list) {
		struct urb *u = up->urb;

		/* look for pending URB's with identical pipe handle */
		if ((urb->pipe == u->pipe) && (urb->dev == u->dev) &&
		    (u->status == -EINPROGRESS) && (u != urb)) {
			if (!last_urb)
				*start = u->start_frame;
			last_urb = u;
		}
	}

	if (last_urb) {
		*end = (last_urb->start_frame + last_urb->number_of_packets *
				last_urb->interval) & (UHCI_NUMFRAMES-1);
		ret = 0;
	} else
		ret = -1;	/* no previous urb found */

	return ret;
}

static int isochronous_find_start(struct uhci_hcd *uhci, struct urb *urb)
{
	int limits;
	unsigned int start = 0, end = 0;

	if (urb->number_of_packets > 900)	/* 900? Why? */
		return -EFBIG;

	limits = isochronous_find_limits(uhci, urb, &start, &end);

	if (urb->transfer_flags & URB_ISO_ASAP) {
		if (limits) {
			uhci_get_current_frame_number(uhci);
			urb->start_frame = (uhci->frame_number + 10)
					& (UHCI_NUMFRAMES - 1);
		} else
			urb->start_frame = end;
	} else {
		urb->start_frame &= (UHCI_NUMFRAMES - 1);
		/* FIXME: Sanity check */
	}

	return 0;
}

/*
 * Isochronous transfers
 */
static int uhci_submit_isochronous(struct uhci_hcd *uhci, struct urb *urb)
{
	struct uhci_td *td;
	int i, ret, frame;
	int status, destination;

	status = TD_CTRL_ACTIVE | TD_CTRL_IOS;
	destination = (urb->pipe & PIPE_DEVEP_MASK) | usb_packetid(urb->pipe);

	ret = isochronous_find_start(uhci, urb);
	if (ret)
		return ret;

	frame = urb->start_frame;
	for (i = 0; i < urb->number_of_packets; i++, frame += urb->interval) {
		if (!urb->iso_frame_desc[i].length)
			continue;

		td = uhci_alloc_td(uhci);
		if (!td)
			return -ENOMEM;

		uhci_add_td_to_urb(urb, td);
		uhci_fill_td(td, status, destination | uhci_explen(urb->iso_frame_desc[i].length - 1),
			urb->transfer_dma + urb->iso_frame_desc[i].offset);

		if (i + 1 >= urb->number_of_packets)
			td->status |= cpu_to_le32(TD_CTRL_IOC);

		uhci_insert_td_frame_list(uhci, td, frame);
	}

	return -EINPROGRESS;
}

static int uhci_result_isochronous(struct uhci_hcd *uhci, struct urb *urb)
{
	struct uhci_td *td;
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	int status;
	int i, ret = 0;

	urb->actual_length = 0;

	i = 0;
	list_for_each_entry(td, &urbp->td_list, list) {
		int actlength;
		unsigned int ctrlstat = td_status(td);

		if (ctrlstat & TD_CTRL_ACTIVE)
			return -EINPROGRESS;

		actlength = uhci_actual_length(ctrlstat);
		urb->iso_frame_desc[i].actual_length = actlength;
		urb->actual_length += actlength;

		status = uhci_map_status(uhci_status_bits(ctrlstat),
				usb_pipeout(urb->pipe));
		urb->iso_frame_desc[i].status = status;
		if (status) {
			urb->error_count++;
			ret = status;
		}

		i++;
	}

	return ret;
}

static struct urb *uhci_find_urb_ep(struct uhci_hcd *uhci, struct urb *urb)
{
	struct urb_priv *up;

	/* We don't match Isoc transfers since they are special */
	if (usb_pipeisoc(urb->pipe))
		return NULL;

	list_for_each_entry(up, &uhci->urb_list, urb_list) {
		struct urb *u = up->urb;

		if (u->dev == urb->dev && u->status == -EINPROGRESS) {
			/* For control, ignore the direction */
			if (usb_pipecontrol(urb->pipe) &&
			    (u->pipe & ~USB_DIR_IN) == (urb->pipe & ~USB_DIR_IN))
				return u;
			else if (u->pipe == urb->pipe)
				return u;
		}
	}

	return NULL;
}

static int uhci_urb_enqueue(struct usb_hcd *hcd,
		struct usb_host_endpoint *ep,
		struct urb *urb, gfp_t mem_flags)
{
	int ret;
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);
	unsigned long flags;
	struct urb *eurb;
	int bustime;

	spin_lock_irqsave(&uhci->lock, flags);

	ret = urb->status;
	if (ret != -EINPROGRESS)		/* URB already unlinked! */
		goto out;

	eurb = uhci_find_urb_ep(uhci, urb);

	if (!uhci_alloc_urb_priv(uhci, urb)) {
		ret = -ENOMEM;
		goto out;
	}

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
		ret = uhci_submit_control(uhci, urb, eurb);
		break;
	case PIPE_INTERRUPT:
		if (!eurb) {
			bustime = usb_check_bandwidth(urb->dev, urb);
			if (bustime < 0)
				ret = bustime;
			else {
				ret = uhci_submit_interrupt(uhci, urb, eurb);
				if (ret == -EINPROGRESS)
					usb_claim_bandwidth(urb->dev, urb, bustime, 0);
			}
		} else {	/* inherit from parent */
			urb->bandwidth = eurb->bandwidth;
			ret = uhci_submit_interrupt(uhci, urb, eurb);
		}
		break;
	case PIPE_BULK:
		ret = uhci_submit_bulk(uhci, urb, eurb);
		break;
	case PIPE_ISOCHRONOUS:
		bustime = usb_check_bandwidth(urb->dev, urb);
		if (bustime < 0) {
			ret = bustime;
			break;
		}

		ret = uhci_submit_isochronous(uhci, urb);
		if (ret == -EINPROGRESS)
			usb_claim_bandwidth(urb->dev, urb, bustime, 1);
		break;
	}

	if (ret != -EINPROGRESS) {
		/* Submit failed, so delete it from the urb_list */
		struct urb_priv *urbp = urb->hcpriv;

		list_del_init(&urbp->urb_list);
		uhci_destroy_urb_priv(uhci, urb);
	} else
		ret = 0;

out:
	spin_unlock_irqrestore(&uhci->lock, flags);
	return ret;
}

/*
 * Return the result of a transfer
 */
static void uhci_transfer_result(struct uhci_hcd *uhci, struct urb *urb)
{
	int ret = -EINPROGRESS;
	struct urb_priv *urbp;

	spin_lock(&urb->lock);

	urbp = (struct urb_priv *)urb->hcpriv;

	if (urb->status != -EINPROGRESS)	/* URB already dequeued */
		goto out;

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
		ret = uhci_result_control(uhci, urb);
		break;
	case PIPE_BULK:
	case PIPE_INTERRUPT:
		ret = uhci_result_common(uhci, urb);
		break;
	case PIPE_ISOCHRONOUS:
		ret = uhci_result_isochronous(uhci, urb);
		break;
	}

	if (ret == -EINPROGRESS)
		goto out;
	urb->status = ret;

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
	case PIPE_BULK:
	case PIPE_ISOCHRONOUS:
		/* Release bandwidth for Interrupt or Isoc. transfers */
		if (urb->bandwidth)
			usb_release_bandwidth(urb->dev, urb, 1);
		uhci_unlink_generic(uhci, urb);
		break;
	case PIPE_INTERRUPT:
		/* Release bandwidth for Interrupt or Isoc. transfers */
		/* Make sure we don't release if we have a queued URB */
		if (list_empty(&urbp->queue_list) && urb->bandwidth)
			usb_release_bandwidth(urb->dev, urb, 0);
		else
			/* bandwidth was passed on to queued URB, */
			/* so don't let usb_unlink_urb() release it */
			urb->bandwidth = 0;
		uhci_unlink_generic(uhci, urb);
		break;
	default:
		dev_info(uhci_dev(uhci), "%s: unknown pipe type %d "
				"for urb %p\n",
				__FUNCTION__, usb_pipetype(urb->pipe), urb);
	}

	/* Move it from uhci->urb_list to uhci->complete_list */
	uhci_moveto_complete(uhci, urbp);

out:
	spin_unlock(&urb->lock);
}

static void uhci_unlink_generic(struct uhci_hcd *uhci, struct urb *urb)
{
	struct list_head *head;
	struct uhci_td *td;
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	int prevactive = 0;

	uhci_dec_fsbr(uhci, urb);	/* Safe since it checks */

	/*
	 * Now we need to find out what the last successful toggle was
	 * so we can update the local data toggle for the next transfer
	 *
	 * There are 2 ways the last successful completed TD is found:
	 *
	 * 1) The TD is NOT active and the actual length < expected length
	 * 2) The TD is NOT active and it's the last TD in the chain
	 *
	 * and a third way the first uncompleted TD is found:
	 *
	 * 3) The TD is active and the previous TD is NOT active
	 *
	 * Control and Isochronous ignore the toggle, so this is safe
	 * for all types
	 *
	 * FIXME: The toggle fixups won't be 100% reliable until we
	 * change over to using a single queue for each endpoint and
	 * stop the queue before unlinking.
	 */
	head = &urbp->td_list;
	list_for_each_entry(td, head, list) {
		unsigned int ctrlstat = td_status(td);

		if (!(ctrlstat & TD_CTRL_ACTIVE) &&
				(uhci_actual_length(ctrlstat) <
				 uhci_expected_length(td_token(td)) ||
				td->list.next == head))
			usb_settoggle(urb->dev, uhci_endpoint(td_token(td)),
				uhci_packetout(td_token(td)),
				uhci_toggle(td_token(td)) ^ 1);
		else if ((ctrlstat & TD_CTRL_ACTIVE) && !prevactive)
			usb_settoggle(urb->dev, uhci_endpoint(td_token(td)),
				uhci_packetout(td_token(td)),
				uhci_toggle(td_token(td)));

		prevactive = ctrlstat & TD_CTRL_ACTIVE;
	}

	uhci_delete_queued_urb(uhci, urb);

	/* The interrupt loop will reclaim the QH's */
	uhci_remove_qh(uhci, urbp->qh);
	urbp->qh = NULL;
}

static int uhci_urb_dequeue(struct usb_hcd *hcd, struct urb *urb)
{
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);
	unsigned long flags;
	struct urb_priv *urbp;

	spin_lock_irqsave(&uhci->lock, flags);
	urbp = urb->hcpriv;
	if (!urbp)			/* URB was never linked! */
		goto done;
	list_del_init(&urbp->urb_list);

	uhci_unlink_generic(uhci, urb);

	uhci_get_current_frame_number(uhci);
	if (uhci->frame_number + uhci->is_stopped != uhci->urb_remove_age) {
		uhci_remove_pending_urbps(uhci);
		uhci->urb_remove_age = uhci->frame_number;
	}

	/* If we're the first, set the next interrupt bit */
	if (list_empty(&uhci->urb_remove_list))
		uhci_set_next_interrupt(uhci);
	list_add_tail(&urbp->urb_list, &uhci->urb_remove_list);

done:
	spin_unlock_irqrestore(&uhci->lock, flags);
	return 0;
}

static int uhci_fsbr_timeout(struct uhci_hcd *uhci, struct urb *urb)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	struct list_head *head;
	struct uhci_td *td;
	int count = 0;

	uhci_dec_fsbr(uhci, urb);

	urbp->fsbr_timeout = 1;

	/*
	 * Ideally we would want to fix qh->element as well, but it's
	 * read/write by the HC, so that can introduce a race. It's not
	 * really worth the hassle
	 */

	head = &urbp->td_list;
	list_for_each_entry(td, head, list) {
		/*
		 * Make sure we don't do the last one (since it'll have the
		 * TERM bit set) as well as we skip every so many TD's to
		 * make sure it doesn't hog the bandwidth
		 */
		if (td->list.next != head && (count % DEPTH_INTERVAL) ==
				(DEPTH_INTERVAL - 1))
			td->link |= UHCI_PTR_DEPTH;

		count++;
	}

	return 0;
}

static void uhci_free_pending_qhs(struct uhci_hcd *uhci)
{
	struct uhci_qh *qh, *tmp;

	list_for_each_entry_safe(qh, tmp, &uhci->qh_remove_list, remove_list) {
		list_del_init(&qh->remove_list);

		uhci_free_qh(uhci, qh);
	}
}

static void uhci_free_pending_tds(struct uhci_hcd *uhci)
{
	struct uhci_td *td, *tmp;

	list_for_each_entry_safe(td, tmp, &uhci->td_remove_list, remove_list) {
		list_del_init(&td->remove_list);

		uhci_free_td(uhci, td);
	}
}

static void
uhci_finish_urb(struct usb_hcd *hcd, struct urb *urb, struct pt_regs *regs)
__releases(uhci->lock)
__acquires(uhci->lock)
{
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);

	uhci_destroy_urb_priv(uhci, urb);

	spin_unlock(&uhci->lock);
	usb_hcd_giveback_urb(hcd, urb, regs);
	spin_lock(&uhci->lock);
}

static void uhci_finish_completion(struct uhci_hcd *uhci, struct pt_regs *regs)
{
	struct urb_priv *urbp, *tmp;

	list_for_each_entry_safe(urbp, tmp, &uhci->complete_list, urb_list) {
		struct urb *urb = urbp->urb;

		list_del_init(&urbp->urb_list);
		uhci_finish_urb(uhci_to_hcd(uhci), urb, regs);
	}
}

static void uhci_remove_pending_urbps(struct uhci_hcd *uhci)
{

	/* Splice the urb_remove_list onto the end of the complete_list */
	list_splice_init(&uhci->urb_remove_list, uhci->complete_list.prev);
}

/* Process events in the schedule, but only in one thread at a time */
static void uhci_scan_schedule(struct uhci_hcd *uhci, struct pt_regs *regs)
{
	struct urb_priv *urbp, *tmp;

	/* Don't allow re-entrant calls */
	if (uhci->scan_in_progress) {
		uhci->need_rescan = 1;
		return;
	}
	uhci->scan_in_progress = 1;
 rescan:
	uhci->need_rescan = 0;

	uhci_clear_next_interrupt(uhci);
	uhci_get_current_frame_number(uhci);

	if (uhci->frame_number + uhci->is_stopped != uhci->qh_remove_age)
		uhci_free_pending_qhs(uhci);
	if (uhci->frame_number + uhci->is_stopped != uhci->td_remove_age)
		uhci_free_pending_tds(uhci);
	if (uhci->frame_number + uhci->is_stopped != uhci->urb_remove_age)
		uhci_remove_pending_urbps(uhci);

	/* Walk the list of pending URBs to see which ones completed
	 * (must be _safe because uhci_transfer_result() dequeues URBs) */
	list_for_each_entry_safe(urbp, tmp, &uhci->urb_list, urb_list) {
		struct urb *urb = urbp->urb;

		/* Checks the status and does all of the magic necessary */
		uhci_transfer_result(uhci, urb);
	}
	uhci_finish_completion(uhci, regs);

	/* If the controller is stopped, we can finish these off right now */
	if (uhci->is_stopped) {
		uhci_free_pending_qhs(uhci);
		uhci_free_pending_tds(uhci);
		uhci_remove_pending_urbps(uhci);
	}

	if (uhci->need_rescan)
		goto rescan;
	uhci->scan_in_progress = 0;

	if (list_empty(&uhci->urb_remove_list) &&
	    list_empty(&uhci->td_remove_list) &&
	    list_empty(&uhci->qh_remove_list))
		uhci_clear_next_interrupt(uhci);
	else
		uhci_set_next_interrupt(uhci);

	/* Wake up anyone waiting for an URB to complete */
	wake_up_all(&uhci->waitqh);
}

static void check_fsbr(struct uhci_hcd *uhci)
{
	struct urb_priv *up;

	list_for_each_entry(up, &uhci->urb_list, urb_list) {
		struct urb *u = up->urb;

		spin_lock(&u->lock);

		/* Check if the FSBR timed out */
		if (up->fsbr && !up->fsbr_timeout && time_after_eq(jiffies, up->fsbrtime + IDLE_TIMEOUT))
			uhci_fsbr_timeout(uhci, u);

		spin_unlock(&u->lock);
	}

	/* Really disable FSBR */
	if (!uhci->fsbr && uhci->fsbrtimeout && time_after_eq(jiffies, uhci->fsbrtimeout)) {
		uhci->fsbrtimeout = 0;
		uhci->skel_term_qh->link = UHCI_PTR_TERM;
	}
}
