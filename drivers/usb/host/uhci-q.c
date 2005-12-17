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
 * (C) Copyright 2004-2005 Alan Stern, stern@rowland.harvard.edu
 */

static void uhci_free_pending_tds(struct uhci_hcd *uhci);

/*
 * Technically, updating td->status here is a race, but it's not really a
 * problem. The worst that can happen is that we set the IOC bit again
 * generating a spurious interrupt. We could fix this by creating another
 * QH and leaving the IOC bit always set, but then we would have to play
 * games with the FSBR code to make sure we get the correct order in all
 * the cases. I don't think it's worth the effort
 */
static void uhci_set_next_interrupt(struct uhci_hcd *uhci)
{
	if (uhci->is_stopped)
		mod_timer(&uhci_to_hcd(uhci)->rh_timer, jiffies);
	uhci->term_td->status |= cpu_to_le32(TD_CTRL_IOC); 
}

static inline void uhci_clear_next_interrupt(struct uhci_hcd *uhci)
{
	uhci->term_td->status &= ~cpu_to_le32(TD_CTRL_IOC);
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

static inline void uhci_fill_td(struct uhci_td *td, u32 status,
		u32 token, u32 buffer)
{
	td->status = cpu_to_le32(status);
	td->token = cpu_to_le32(token);
	td->buffer = cpu_to_le32(buffer);
}

/*
 * We insert Isochronous URBs directly into the frame list at the beginning
 */
static inline void uhci_insert_td_in_frame_list(struct uhci_hcd *uhci,
		struct uhci_td *td, unsigned framenum)
{
	framenum &= (UHCI_NUMFRAMES - 1);

	td->frame = framenum;

	/* Is there a TD already mapped there? */
	if (uhci->frame_cpu[framenum]) {
		struct uhci_td *ftd, *ltd;

		ftd = uhci->frame_cpu[framenum];
		ltd = list_entry(ftd->fl_list.prev, struct uhci_td, fl_list);

		list_add_tail(&td->fl_list, &ftd->fl_list);

		td->link = ltd->link;
		wmb();
		ltd->link = cpu_to_le32(td->dma_handle);
	} else {
		td->link = uhci->frame[framenum];
		wmb();
		uhci->frame[framenum] = cpu_to_le32(td->dma_handle);
		uhci->frame_cpu[framenum] = td;
	}
}

static inline void uhci_remove_td_from_frame_list(struct uhci_hcd *uhci,
		struct uhci_td *td)
{
	/* If it's not inserted, don't remove it */
	if (td->frame == -1) {
		WARN_ON(!list_empty(&td->fl_list));
		return;
	}

	if (uhci->frame_cpu[td->frame] == td) {
		if (list_empty(&td->fl_list)) {
			uhci->frame[td->frame] = td->link;
			uhci->frame_cpu[td->frame] = NULL;
		} else {
			struct uhci_td *ntd;

			ntd = list_entry(td->fl_list.next, struct uhci_td, fl_list);
			uhci->frame[td->frame] = cpu_to_le32(ntd->dma_handle);
			uhci->frame_cpu[td->frame] = ntd;
		}
	} else {
		struct uhci_td *ptd;

		ptd = list_entry(td->fl_list.prev, struct uhci_td, fl_list);
		ptd->link = td->link;
	}

	list_del_init(&td->fl_list);
	td->frame = -1;
}

/*
 * Remove all the TDs for an Isochronous URB from the frame list
 */
static void uhci_unlink_isochronous_tds(struct uhci_hcd *uhci, struct urb *urb)
{
	struct urb_priv *urbp = (struct urb_priv *) urb->hcpriv;
	struct uhci_td *td;

	list_for_each_entry(td, &urbp->td_list, list)
		uhci_remove_td_from_frame_list(uhci, td);
	wmb();
}

/*
 * Remove an URB's TDs from the hardware schedule
 */
static void uhci_remove_tds_from_schedule(struct uhci_hcd *uhci,
		struct urb *urb, int status)
{
	struct urb_priv *urbp = (struct urb_priv *) urb->hcpriv;

	/* Isochronous TDs get unlinked directly from the frame list */
	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
		uhci_unlink_isochronous_tds(uhci, urb);
		return;
	}

	/* If the URB isn't first on its queue, adjust the link pointer
	 * of the last TD in the previous URB. */
	if (urbp->node.prev != &urbp->qh->queue) {
		struct urb_priv *purbp;
		struct uhci_td *ptd, *ltd;

		if (status == -EINPROGRESS)
			status = 0;
		purbp = list_entry(urbp->node.prev, struct urb_priv, node);
		ptd = list_entry(purbp->td_list.prev, struct uhci_td,
				list);
		ltd = list_entry(urbp->td_list.prev, struct uhci_td,
				list);
		ptd->link = ltd->link;
	}

	/* If the URB completed with an error, then the QH element certainly
	 * points to one of the URB's TDs.  If it completed normally then
	 * the QH element has certainly moved on to the next URB.  And if
	 * the URB is still in progress then it must have been dequeued.
	 * The QH element either hasn't reached it yet or is somewhere in
	 * the middle.  If the URB wasn't first we can assume that it
	 * hasn't started yet (see above): Otherwise all the preceding URBs
	 * would have completed and been removed from the queue, so this one
	 * _would_ be first.
	 *
	 * If the QH element is inside this URB, clear it.  It will be
	 * set properly when the QH is activated.
	 */
	if (status < 0)
		urbp->qh->element = UHCI_PTR_TERM;
}

static struct uhci_qh *uhci_alloc_qh(struct uhci_hcd *uhci,
		struct usb_device *udev, struct usb_host_endpoint *hep)
{
	dma_addr_t dma_handle;
	struct uhci_qh *qh;

	qh = dma_pool_alloc(uhci->qh_pool, GFP_ATOMIC, &dma_handle);
	if (!qh)
		return NULL;

	qh->dma_handle = dma_handle;

	qh->element = UHCI_PTR_TERM;
	qh->link = UHCI_PTR_TERM;

	INIT_LIST_HEAD(&qh->queue);
	INIT_LIST_HEAD(&qh->node);

	if (udev) {		/* Normal QH */
		qh->state = QH_STATE_IDLE;
		qh->hep = hep;
		qh->udev = udev;
		hep->hcpriv = qh;
		usb_get_dev(udev);

	} else {		/* Skeleton QH */
		qh->state = QH_STATE_ACTIVE;
		qh->udev = NULL;
	}
	return qh;
}

static void uhci_free_qh(struct uhci_hcd *uhci, struct uhci_qh *qh)
{
	WARN_ON(qh->state != QH_STATE_IDLE && qh->udev);
	if (!list_empty(&qh->queue))
		dev_warn(uhci_dev(uhci), "qh %p list not empty!\n", qh);

	list_del(&qh->node);
	if (qh->udev) {
		qh->hep->hcpriv = NULL;
		usb_put_dev(qh->udev);
	}
	dma_pool_free(uhci->qh_pool, qh, qh->dma_handle);
}

/*
 * Put a QH on the schedule in both hardware and software
 */
static void uhci_activate_qh(struct uhci_hcd *uhci, struct uhci_qh *qh)
{
	struct uhci_qh *pqh;

	WARN_ON(list_empty(&qh->queue));

	/* Set the element pointer if it isn't set already.
	 * This isn't needed for Isochronous queues, but it doesn't hurt. */
	if (qh_element(qh) == UHCI_PTR_TERM) {
		struct urb_priv *urbp = list_entry(qh->queue.next,
				struct urb_priv, node);
		struct uhci_td *td = list_entry(urbp->td_list.next,
				struct uhci_td, list);

		qh->element = cpu_to_le32(td->dma_handle);
	}

	if (qh->state == QH_STATE_ACTIVE)
		return;
	qh->state = QH_STATE_ACTIVE;

	/* Move the QH from its old list to the end of the appropriate
	 * skeleton's list */
	list_move_tail(&qh->node, &qh->skel->node);

	/* Link it into the schedule */
	pqh = list_entry(qh->node.prev, struct uhci_qh, node);
	qh->link = pqh->link;
	wmb();
	pqh->link = UHCI_PTR_QH | cpu_to_le32(qh->dma_handle);
}

/*
 * Take a QH off the hardware schedule
 */
static void uhci_unlink_qh(struct uhci_hcd *uhci, struct uhci_qh *qh)
{
	struct uhci_qh *pqh;

	if (qh->state == QH_STATE_UNLINKING)
		return;
	WARN_ON(qh->state != QH_STATE_ACTIVE || !qh->udev);
	qh->state = QH_STATE_UNLINKING;

	/* Unlink the QH from the schedule and record when we did it */
	pqh = list_entry(qh->node.prev, struct uhci_qh, node);
	pqh->link = qh->link;
	mb();

	uhci_get_current_frame_number(uhci);
	qh->unlink_frame = uhci->frame_number;

	/* Force an interrupt so we know when the QH is fully unlinked */
	if (list_empty(&uhci->skel_unlink_qh->node))
		uhci_set_next_interrupt(uhci);

	/* Move the QH from its old list to the end of the unlinking list */
	list_move_tail(&qh->node, &uhci->skel_unlink_qh->node);
}

/*
 * When we and the controller are through with a QH, it becomes IDLE.
 * This happens when a QH has been off the schedule (on the unlinking
 * list) for more than one frame, or when an error occurs while adding
 * the first URB onto a new QH.
 */
static void uhci_make_qh_idle(struct uhci_hcd *uhci, struct uhci_qh *qh)
{
	WARN_ON(qh->state == QH_STATE_ACTIVE);

	list_move(&qh->node, &uhci->idle_qh_list);
	qh->state = QH_STATE_IDLE;

	/* If anyone is waiting for a QH to become idle, wake them up */
	if (uhci->num_waiting)
		wake_up_all(&uhci->waitqh);
}

static inline struct urb_priv *uhci_alloc_urb_priv(struct uhci_hcd *uhci,
		struct urb *urb)
{
	struct urb_priv *urbp;

	urbp = kmem_cache_alloc(uhci_up_cachep, SLAB_ATOMIC);
	if (!urbp)
		return NULL;

	memset((void *)urbp, 0, sizeof(*urbp));

	urbp->urb = urb;
	urb->hcpriv = urbp;
	urbp->fsbrtime = jiffies;
	
	INIT_LIST_HEAD(&urbp->node);
	INIT_LIST_HEAD(&urbp->td_list);
	INIT_LIST_HEAD(&urbp->urb_list);

	return urbp;
}

static void uhci_add_td_to_urb(struct urb *urb, struct uhci_td *td)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;

	list_add_tail(&td->list, &urbp->td_list);
}

static void uhci_remove_td_from_urb(struct uhci_td *td)
{
	if (list_empty(&td->list))
		return;

	list_del_init(&td->list);
}

static void uhci_free_urb_priv(struct uhci_hcd *uhci,
		struct urb_priv *urbp)
{
	struct uhci_td *td, *tmp;

	if (!list_empty(&urbp->urb_list))
		dev_warn(uhci_dev(uhci), "urb %p still on uhci->urb_list!\n",
				urbp->urb);
	if (!list_empty(&urbp->node))
		dev_warn(uhci_dev(uhci), "urb %p still on QH's list!\n",
				urbp->urb);

	uhci_get_current_frame_number(uhci);
	if (uhci->frame_number + uhci->is_stopped != uhci->td_remove_age) {
		uhci_free_pending_tds(uhci);
		uhci->td_remove_age = uhci->frame_number;
	}

	/* Check to see if the remove list is empty. Set the IOC bit */
	/* to force an interrupt so we can remove the TDs. */
	if (list_empty(&uhci->td_remove_list))
		uhci_set_next_interrupt(uhci);

	list_for_each_entry_safe(td, tmp, &urbp->td_list, list) {
		uhci_remove_td_from_urb(td);
		list_add(&td->remove_list, &uhci->td_remove_list);
	}

	urbp->urb->hcpriv = NULL;
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
 * Fix up the data toggles for URBs in a queue, when one of them
 * terminates early (short transfer, error, or dequeued).
 */
static void uhci_fixup_toggles(struct urb *urb)
{
	struct list_head *head;
	struct uhci_td *td;
	struct urb_priv *urbp = (struct urb_priv *) urb->hcpriv;
	int prevactive = 0;
	unsigned int toggle = 0;
	struct urb_priv *turbp, *list_end;

	/*
	 * We need to find out what the last successful toggle was so
	 * we can update the data toggles for the following transfers.
	 *
	 * There are 2 ways the last successful completed TD is found:
	 *
	 * 1) The TD is NOT active and the actual length < expected length
	 * 2) The TD is NOT active and it's the last TD in the chain
	 *
	 * and a third way the first uncompleted TD is found:
	 *
	 * 3) The TD is active and the previous TD is NOT active
	 */
	head = &urbp->td_list;
	list_for_each_entry(td, head, list) {
		unsigned int ctrlstat = td_status(td);

		if (!(ctrlstat & TD_CTRL_ACTIVE) &&
				(uhci_actual_length(ctrlstat) <
				 uhci_expected_length(td_token(td)) ||
				td->list.next == head))
			toggle = uhci_toggle(td_token(td)) ^ 1;
		else if ((ctrlstat & TD_CTRL_ACTIVE) && !prevactive)
			toggle = uhci_toggle(td_token(td));

		prevactive = ctrlstat & TD_CTRL_ACTIVE;
	}

	/*
	 * Fix up the toggle for the following URBs in the queue.
	 *
	 * We can stop as soon as we find an URB with toggles set correctly,
	 * because then all the following URBs will be correct also.
	 */
	list_end = list_entry(&urbp->qh->queue, struct urb_priv, node);
	turbp = urbp;
	while ((turbp = list_entry(turbp->node.next, struct urb_priv, node))
			!= list_end) {
		td = list_entry(turbp->td_list.next, struct uhci_td, list);
		if (uhci_toggle(td_token(td)) == toggle)
			return;

		list_for_each_entry(td, &turbp->td_list, list) {
			td->token ^= __constant_cpu_to_le32(TD_TOKEN_TOGGLE);
			toggle ^= 1;
		}
	}

	usb_settoggle(urb->dev, usb_pipeendpoint(urb->pipe),
			usb_pipeout(urb->pipe), toggle);
}

/*
 * Control transfers
 */
static int uhci_submit_control(struct uhci_hcd *uhci, struct urb *urb,
		struct uhci_qh *qh)
{
	struct uhci_td *td;
	unsigned long destination, status;
	int maxsze = le16_to_cpu(qh->hep->desc.wMaxPacketSize);
	int len = urb->transfer_buffer_length;
	dma_addr_t data = urb->transfer_dma;
	__le32 *plink;

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
	uhci_fill_td(td, status, destination | uhci_explen(8),
			urb->setup_dma);
	plink = &td->link;

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
	 * Build the DATA TDs
	 */
	while (len > 0) {
		int pktsze = min(len, maxsze);

		td = uhci_alloc_td(uhci);
		if (!td)
			return -ENOMEM;
		*plink = cpu_to_le32(td->dma_handle);

		/* Alternate Data0/1 (start with Data1) */
		destination ^= TD_TOKEN_TOGGLE;
	
		uhci_add_td_to_urb(urb, td);
		uhci_fill_td(td, status, destination | uhci_explen(pktsze),
				data);
		plink = &td->link;

		data += pktsze;
		len -= pktsze;
	}

	/*
	 * Build the final TD for control status 
	 */
	td = uhci_alloc_td(uhci);
	if (!td)
		return -ENOMEM;
	*plink = cpu_to_le32(td->dma_handle);

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
			destination | uhci_explen(0), 0);

	/* Low-speed transfers get a different queue, and won't hog the bus.
	 * Also, some devices enumerate better without FSBR; the easiest way
	 * to do that is to put URBs on the low-speed queue while the device
	 * isn't in the CONFIGURED state. */
	if (urb->dev->speed == USB_SPEED_LOW ||
			urb->dev->state != USB_STATE_CONFIGURED)
		qh->skel = uhci->skel_ls_control_qh;
	else {
		qh->skel = uhci->skel_fs_control_qh;
		uhci_inc_fsbr(uhci, urb);
	}

	return 0;
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

	urbp->short_transfer = 1;

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

	head = &urbp->td_list;
	if (urbp->short_transfer) {
		tmp = head->prev;
		goto status_stage;
	}

	urb->actual_length = 0;

	tmp = head->next;
	td = list_entry(tmp, struct uhci_td, list);

	/* The first TD is the SETUP stage, check the status, but skip */
	/*  the count */
	status = uhci_status_bits(td_status(td));
	if (status & TD_CTRL_ACTIVE)
		return -EINPROGRESS;

	if (status)
		goto td_error;

	/* The rest of the TDs (but the last) are data */
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

			return usb_control_retrigger_status(uhci, urb);
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
static int uhci_submit_common(struct uhci_hcd *uhci, struct urb *urb,
		struct uhci_qh *qh)
{
	struct uhci_td *td;
	unsigned long destination, status;
	int maxsze = le16_to_cpu(qh->hep->desc.wMaxPacketSize);
	int len = urb->transfer_buffer_length;
	dma_addr_t data = urb->transfer_dma;
	__le32 *plink, fake_link;

	if (len < 0)
		return -EINVAL;

	/* The "pipe" thing contains the destination in bits 8--18 */
	destination = (urb->pipe & PIPE_DEVEP_MASK) | usb_packetid(urb->pipe);

	/* 3 errors */
	status = TD_CTRL_ACTIVE | uhci_maxerr(3);
	if (urb->dev->speed == USB_SPEED_LOW)
		status |= TD_CTRL_LS;
	if (usb_pipein(urb->pipe))
		status |= TD_CTRL_SPD;

	/*
	 * Build the DATA TDs
	 */
	plink = &fake_link;
	do {	/* Allow zero length packets */
		int pktsze = maxsze;

		if (len <= pktsze) {		/* The last packet */
			pktsze = len;
			if (!(urb->transfer_flags & URB_SHORT_NOT_OK))
				status &= ~TD_CTRL_SPD;
		}

		td = uhci_alloc_td(uhci);
		if (!td)
			return -ENOMEM;
		*plink = cpu_to_le32(td->dma_handle);

		uhci_add_td_to_urb(urb, td);
		uhci_fill_td(td, status,
			destination | uhci_explen(pktsze) |
			(usb_gettoggle(urb->dev, usb_pipeendpoint(urb->pipe),
			 usb_pipeout(urb->pipe)) << TD_TOKEN_TOGGLE_SHIFT),
			data);
		plink = &td->link;

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
	if ((urb->transfer_flags & URB_ZERO_PACKET) &&
			usb_pipeout(urb->pipe) && len == 0 &&
			urb->transfer_buffer_length > 0) {
		td = uhci_alloc_td(uhci);
		if (!td)
			return -ENOMEM;
		*plink = cpu_to_le32(td->dma_handle);

		uhci_add_td_to_urb(urb, td);
		uhci_fill_td(td, status, destination | uhci_explen(0) |
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
	td->status |= __constant_cpu_to_le32(TD_CTRL_IOC);

	return 0;
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
			}

			/*
			 * This URB stopped short of its end.  We have to
			 * fix up the toggles of the following URBs on the
			 * queue and restart the queue.
			 *
			 * Do this only the first time we encounter the
			 * short URB.
			 */
			if (!urbp->short_transfer) {
				urbp->short_transfer = 1;
				uhci_fixup_toggles(urb);
				td = list_entry(urbp->td_list.prev,
						struct uhci_td, list);
				urbp->qh->element = td->link;
			}
			break;
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

static inline int uhci_submit_bulk(struct uhci_hcd *uhci, struct urb *urb,
		struct uhci_qh *qh)
{
	int ret;

	/* Can't have low-speed bulk transfers */
	if (urb->dev->speed == USB_SPEED_LOW)
		return -EINVAL;

	qh->skel = uhci->skel_bulk_qh;
	ret = uhci_submit_common(uhci, urb, qh);
	if (ret == 0)
		uhci_inc_fsbr(uhci, urb);
	return ret;
}

static inline int uhci_submit_interrupt(struct uhci_hcd *uhci, struct urb *urb,
		struct uhci_qh *qh)
{
	/* USB 1.1 interrupt transfers only involve one packet per interval.
	 * Drivers can submit URBs of any length, but longer ones will need
	 * multiple intervals to complete.
	 */
	qh->skel = uhci->skelqh[__interval_to_skel(urb->interval)];
	return uhci_submit_common(uhci, urb, qh);
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

		/* look for pending URBs with identical pipe handle */
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
static int uhci_submit_isochronous(struct uhci_hcd *uhci, struct urb *urb,
		struct uhci_qh *qh)
{
	struct uhci_td *td = NULL;	/* Since urb->number_of_packets > 0 */
	int i, ret, frame;
	unsigned long destination, status;
	struct urb_priv *urbp = (struct urb_priv *) urb->hcpriv;

	status = TD_CTRL_ACTIVE | TD_CTRL_IOS;
	destination = (urb->pipe & PIPE_DEVEP_MASK) | usb_packetid(urb->pipe);

	ret = isochronous_find_start(uhci, urb);
	if (ret)
		return ret;

	for (i = 0; i < urb->number_of_packets; i++) {
		td = uhci_alloc_td(uhci);
		if (!td)
			return -ENOMEM;

		uhci_add_td_to_urb(urb, td);
		uhci_fill_td(td, status, destination |
				uhci_explen(urb->iso_frame_desc[i].length),
				urb->transfer_dma +
					urb->iso_frame_desc[i].offset);
	}

	/* Set the interrupt-on-completion flag on the last packet. */
	td->status |= __constant_cpu_to_le32(TD_CTRL_IOC);

	qh->skel = uhci->skel_iso_qh;

	/* Add the TDs to the frame list */
	frame = urb->start_frame;
	list_for_each_entry(td, &urbp->td_list, list) {
		uhci_insert_td_in_frame_list(uhci, td, frame);
		frame += urb->interval;
	}

	return 0;
}

static int uhci_result_isochronous(struct uhci_hcd *uhci, struct urb *urb)
{
	struct uhci_td *td;
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	int status;
	int i, ret = 0;

	urb->actual_length = urb->error_count = 0;

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

static int uhci_urb_enqueue(struct usb_hcd *hcd,
		struct usb_host_endpoint *hep,
		struct urb *urb, gfp_t mem_flags)
{
	int ret;
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);
	unsigned long flags;
	struct urb_priv *urbp;
	struct uhci_qh *qh;
	int bustime;

	spin_lock_irqsave(&uhci->lock, flags);

	ret = urb->status;
	if (ret != -EINPROGRESS)		/* URB already unlinked! */
		goto done;

	ret = -ENOMEM;
	urbp = uhci_alloc_urb_priv(uhci, urb);
	if (!urbp)
		goto done;

	if (hep->hcpriv)
		qh = (struct uhci_qh *) hep->hcpriv;
	else {
		qh = uhci_alloc_qh(uhci, urb->dev, hep);
		if (!qh)
			goto err_no_qh;
	}
	urbp->qh = qh;

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
		ret = uhci_submit_control(uhci, urb, qh);
		break;
	case PIPE_BULK:
		ret = uhci_submit_bulk(uhci, urb, qh);
		break;
	case PIPE_INTERRUPT:
		if (list_empty(&qh->queue)) {
			bustime = usb_check_bandwidth(urb->dev, urb);
			if (bustime < 0)
				ret = bustime;
			else {
				ret = uhci_submit_interrupt(uhci, urb, qh);
				if (ret == 0)
					usb_claim_bandwidth(urb->dev, urb, bustime, 0);
			}
		} else {	/* inherit from parent */
			struct urb_priv *eurbp;

			eurbp = list_entry(qh->queue.prev, struct urb_priv,
					node);
			urb->bandwidth = eurbp->urb->bandwidth;
			ret = uhci_submit_interrupt(uhci, urb, qh);
		}
		break;
	case PIPE_ISOCHRONOUS:
		bustime = usb_check_bandwidth(urb->dev, urb);
		if (bustime < 0) {
			ret = bustime;
			break;
		}

		ret = uhci_submit_isochronous(uhci, urb, qh);
		if (ret == 0)
			usb_claim_bandwidth(urb->dev, urb, bustime, 1);
		break;
	}
	if (ret != 0)
		goto err_submit_failed;

	/* Add this URB to the QH */
	urbp->qh = qh;
	list_add_tail(&urbp->node, &qh->queue);
	list_add_tail(&urbp->urb_list, &uhci->urb_list);

	/* If the new URB is the first and only one on this QH then either
	 * the QH is new and idle or else it's unlinked and waiting to
	 * become idle, so we can activate it right away. */
	if (qh->queue.next == &urbp->node)
		uhci_activate_qh(uhci, qh);

	/* If the QH is already active, we have a race with the hardware.
	 * This won't get fixed until dummy TDs are added. */
	else if (qh->state == QH_STATE_ACTIVE) {

		/* If the URB isn't first on its queue, adjust the link pointer
		 * of the last TD in the previous URB. */
		if (urbp->node.prev != &urbp->qh->queue) {
			struct urb_priv *purbp = list_entry(urbp->node.prev,
					struct urb_priv, node);
			struct uhci_td *ptd = list_entry(purbp->td_list.prev,
					struct uhci_td, list);
			struct uhci_td *td = list_entry(urbp->td_list.next,
					struct uhci_td, list);

			ptd->link = cpu_to_le32(td->dma_handle);

		}
		if (qh_element(qh) == UHCI_PTR_TERM) {
			struct uhci_td *td = list_entry(urbp->td_list.next,
					struct uhci_td, list);

			qh->element = cpu_to_le32(td->dma_handle);
		}
	}
	goto done;

err_submit_failed:
	if (qh->state == QH_STATE_IDLE)
		uhci_make_qh_idle(uhci, qh);	/* Reclaim unused QH */

err_no_qh:
	uhci_free_urb_priv(uhci, urbp);

done:
	spin_unlock_irqrestore(&uhci->lock, flags);
	return ret;
}

/*
 * Return the result of a transfer
 */
static void uhci_transfer_result(struct uhci_hcd *uhci, struct urb *urb)
{
	int status;
	int okay_to_giveback = 0;
	struct urb_priv *urbp = (struct urb_priv *) urb->hcpriv;

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
		status = uhci_result_control(uhci, urb);
		break;
	case PIPE_ISOCHRONOUS:
		status = uhci_result_isochronous(uhci, urb);
		break;
	default:	/* PIPE_BULK or PIPE_INTERRUPT */
		status = uhci_result_common(uhci, urb);
		break;
	}

	spin_lock(&urb->lock);
	if (urb->status == -EINPROGRESS) {	/* Not yet dequeued */
		if (status != -EINPROGRESS) {	/* URB has completed */
			urb->status = status;

			/* If the URB got a real error (as opposed to
			 * simply being dequeued), we don't have to
			 * unlink the QH.  Fix this later... */
			if (status < 0)
				uhci_unlink_qh(uhci, urbp->qh);
			else
				okay_to_giveback = 1;
		}
	} else {				/* Already dequeued */
		if (urbp->qh->state == QH_STATE_UNLINKING &&
				uhci->frame_number + uhci->is_stopped !=
				urbp->qh->unlink_frame)
			okay_to_giveback = 1;
	}
	spin_unlock(&urb->lock);
	if (!okay_to_giveback)
		return;

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_ISOCHRONOUS:
		/* Release bandwidth for Interrupt or Isoc. transfers */
		if (urb->bandwidth)
			usb_release_bandwidth(urb->dev, urb, 1);
		break;
	case PIPE_INTERRUPT:
		/* Release bandwidth for Interrupt or Isoc. transfers */
		/* Make sure we don't release if we have a queued URB */
		if (list_empty(&urbp->qh->queue) && urb->bandwidth)
			usb_release_bandwidth(urb->dev, urb, 0);
		else
			/* bandwidth was passed on to queued URB, */
			/* so don't let usb_unlink_urb() release it */
			urb->bandwidth = 0;
		/* Falls through */
	case PIPE_BULK:
		if (status < 0)
			uhci_fixup_toggles(urb);
		break;
	default:	/* PIPE_CONTROL */
		break;
	}

	/* Take the URB's TDs off the hardware schedule */
	uhci_remove_tds_from_schedule(uhci, urb, status);

	/* Take the URB off the QH's queue and see if the QH is now unused */
	list_del_init(&urbp->node);
	if (list_empty(&urbp->qh->queue))
		uhci_unlink_qh(uhci, urbp->qh);

	uhci_dec_fsbr(uhci, urb);	/* Safe since it checks */

	/* Queue it for giving back */
	list_move_tail(&urbp->urb_list, &uhci->complete_list);
}

/*
 * Check out the QHs waiting to be fully unlinked
 */
static void uhci_scan_unlinking_qhs(struct uhci_hcd *uhci)
{
	struct uhci_qh *qh, *tmp;

	list_for_each_entry_safe(qh, tmp, &uhci->skel_unlink_qh->node, node) {

		/* If the queue is empty and the QH is fully unlinked then
		 * it can become IDLE. */
		if (list_empty(&qh->queue)) {
			if (uhci->frame_number + uhci->is_stopped !=
					qh->unlink_frame)
				uhci_make_qh_idle(uhci, qh);

		/* If none of the QH's URBs have been dequeued then the QH
		 * should be re-activated. */
		} else {
			struct urb_priv *urbp;
			int any_dequeued = 0;

			list_for_each_entry(urbp, &qh->queue, node) {
				if (urbp->urb->status != -EINPROGRESS) {
					any_dequeued = 1;
					break;
				}
			}
			if (!any_dequeued)
				uhci_activate_qh(uhci, qh);
		}
	}
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

	/* Remove Isochronous TDs from the frame list ASAP */
	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS)
		uhci_unlink_isochronous_tds(uhci, urb);
	uhci_unlink_qh(uhci, urbp->qh);

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
		 * TERM bit set) as well as we skip every so many TDs to
		 * make sure it doesn't hog the bandwidth
		 */
		if (td->list.next != head && (count % DEPTH_INTERVAL) ==
				(DEPTH_INTERVAL - 1))
			td->link |= UHCI_PTR_DEPTH;

		count++;
	}

	return 0;
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

	uhci_free_urb_priv(uhci, (struct urb_priv *) (urb->hcpriv));

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

	if (uhci->frame_number + uhci->is_stopped != uhci->td_remove_age)
		uhci_free_pending_tds(uhci);

	/* Walk the list of pending URBs to see which ones completed
	 * (must be _safe because uhci_transfer_result() dequeues URBs) */
	list_for_each_entry_safe(urbp, tmp, &uhci->urb_list, urb_list) {
		struct urb *urb = urbp->urb;

		/* Checks the status and does all of the magic necessary */
		uhci_transfer_result(uhci, urb);
	}
	uhci_finish_completion(uhci, regs);

	/* If the controller is stopped, we can finish these off right now */
	if (uhci->is_stopped)
		uhci_free_pending_tds(uhci);

	if (uhci->need_rescan)
		goto rescan;
	uhci->scan_in_progress = 0;

	/* Check out the QHs waiting for unlinking */
	uhci_scan_unlinking_qhs(uhci);

	if (list_empty(&uhci->td_remove_list) &&
			list_empty(&uhci->skel_unlink_qh->node))
		uhci_clear_next_interrupt(uhci);
	else
		uhci_set_next_interrupt(uhci);
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
