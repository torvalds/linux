/*
 * hcd.c - DesignWare HS OTG Controller host-mode routines
 *
 * Copyright (C) 2004-2013 Synopsys, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This file contains the core HCD code, and implements the Linux hc_driver
 * API
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include <linux/usb/hcd.h>
#include <linux/usb/ch11.h>

#include "core.h"
#include "hcd.h"

/**
 * dwc2_dump_channel_info() - Prints the state of a host channel
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Pointer to the channel to dump
 *
 * Must be called with interrupt disabled and spinlock held
 *
 * NOTE: This function will be removed once the peripheral controller code
 * is integrated and the driver is stable
 */
static void dwc2_dump_channel_info(struct dwc2_hsotg *hsotg,
				   struct dwc2_host_chan *chan)
{
#ifdef VERBOSE_DEBUG
	int num_channels = hsotg->core_params->host_channels;
	struct dwc2_qh *qh;
	u32 hcchar;
	u32 hcsplt;
	u32 hctsiz;
	u32 hc_dma;
	int i;

	if (chan == NULL)
		return;

	hcchar = readl(hsotg->regs + HCCHAR(chan->hc_num));
	hcsplt = readl(hsotg->regs + HCSPLT(chan->hc_num));
	hctsiz = readl(hsotg->regs + HCTSIZ(chan->hc_num));
	hc_dma = readl(hsotg->regs + HCDMA(chan->hc_num));

	dev_dbg(hsotg->dev, "  Assigned to channel %p:\n", chan);
	dev_dbg(hsotg->dev, "    hcchar 0x%08x, hcsplt 0x%08x\n",
		hcchar, hcsplt);
	dev_dbg(hsotg->dev, "    hctsiz 0x%08x, hc_dma 0x%08x\n",
		hctsiz, hc_dma);
	dev_dbg(hsotg->dev, "    dev_addr: %d, ep_num: %d, ep_is_in: %d\n",
		chan->dev_addr, chan->ep_num, chan->ep_is_in);
	dev_dbg(hsotg->dev, "    ep_type: %d\n", chan->ep_type);
	dev_dbg(hsotg->dev, "    max_packet: %d\n", chan->max_packet);
	dev_dbg(hsotg->dev, "    data_pid_start: %d\n", chan->data_pid_start);
	dev_dbg(hsotg->dev, "    xfer_started: %d\n", chan->xfer_started);
	dev_dbg(hsotg->dev, "    halt_status: %d\n", chan->halt_status);
	dev_dbg(hsotg->dev, "    xfer_buf: %p\n", chan->xfer_buf);
	dev_dbg(hsotg->dev, "    xfer_dma: %08lx\n",
		(unsigned long)chan->xfer_dma);
	dev_dbg(hsotg->dev, "    xfer_len: %d\n", chan->xfer_len);
	dev_dbg(hsotg->dev, "    qh: %p\n", chan->qh);
	dev_dbg(hsotg->dev, "  NP inactive sched:\n");
	list_for_each_entry(qh, &hsotg->non_periodic_sched_inactive,
			    qh_list_entry)
		dev_dbg(hsotg->dev, "    %p\n", qh);
	dev_dbg(hsotg->dev, "  NP active sched:\n");
	list_for_each_entry(qh, &hsotg->non_periodic_sched_active,
			    qh_list_entry)
		dev_dbg(hsotg->dev, "    %p\n", qh);
	dev_dbg(hsotg->dev, "  Channels:\n");
	for (i = 0; i < num_channels; i++) {
		struct dwc2_host_chan *chan = hsotg->hc_ptr_array[i];

		dev_dbg(hsotg->dev, "    %2d: %p\n", i, chan);
	}
#endif /* VERBOSE_DEBUG */
}

/*
 * Processes all the URBs in a single list of QHs. Completes them with
 * -ETIMEDOUT and frees the QTD.
 *
 * Must be called with interrupt disabled and spinlock held
 */
static void dwc2_kill_urbs_in_qh_list(struct dwc2_hsotg *hsotg,
				      struct list_head *qh_list)
{
	struct dwc2_qh *qh, *qh_tmp;
	struct dwc2_qtd *qtd, *qtd_tmp;

	list_for_each_entry_safe(qh, qh_tmp, qh_list, qh_list_entry) {
		list_for_each_entry_safe(qtd, qtd_tmp, &qh->qtd_list,
					 qtd_list_entry) {
			if (qtd->urb != NULL) {
				dwc2_host_complete(hsotg, qtd->urb->priv,
						   qtd->urb, -ETIMEDOUT);
				dwc2_hcd_qtd_unlink_and_free(hsotg, qtd, qh);
			}
		}
	}
}

static void dwc2_qh_list_free(struct dwc2_hsotg *hsotg,
			      struct list_head *qh_list)
{
	struct dwc2_qtd *qtd, *qtd_tmp;
	struct dwc2_qh *qh, *qh_tmp;
	unsigned long flags;

	if (!qh_list->next)
		/* The list hasn't been initialized yet */
		return;

	spin_lock_irqsave(&hsotg->lock, flags);

	/* Ensure there are no QTDs or URBs left */
	dwc2_kill_urbs_in_qh_list(hsotg, qh_list);

	list_for_each_entry_safe(qh, qh_tmp, qh_list, qh_list_entry) {
		dwc2_hcd_qh_unlink(hsotg, qh);

		/* Free each QTD in the QH's QTD list */
		list_for_each_entry_safe(qtd, qtd_tmp, &qh->qtd_list,
					 qtd_list_entry)
			dwc2_hcd_qtd_unlink_and_free(hsotg, qtd, qh);

		spin_unlock_irqrestore(&hsotg->lock, flags);
		dwc2_hcd_qh_free(hsotg, qh);
		spin_lock_irqsave(&hsotg->lock, flags);
	}

	spin_unlock_irqrestore(&hsotg->lock, flags);
}

/*
 * Responds with an error status of -ETIMEDOUT to all URBs in the non-periodic
 * and periodic schedules. The QTD associated with each URB is removed from
 * the schedule and freed. This function may be called when a disconnect is
 * detected or when the HCD is being stopped.
 *
 * Must be called with interrupt disabled and spinlock held
 */
static void dwc2_kill_all_urbs(struct dwc2_hsotg *hsotg)
{
	dwc2_kill_urbs_in_qh_list(hsotg, &hsotg->non_periodic_sched_inactive);
	dwc2_kill_urbs_in_qh_list(hsotg, &hsotg->non_periodic_sched_active);
	dwc2_kill_urbs_in_qh_list(hsotg, &hsotg->periodic_sched_inactive);
	dwc2_kill_urbs_in_qh_list(hsotg, &hsotg->periodic_sched_ready);
	dwc2_kill_urbs_in_qh_list(hsotg, &hsotg->periodic_sched_assigned);
	dwc2_kill_urbs_in_qh_list(hsotg, &hsotg->periodic_sched_queued);
}

/**
 * dwc2_hcd_start() - Starts the HCD when switching to Host mode
 *
 * @hsotg: Pointer to struct dwc2_hsotg
 */
void dwc2_hcd_start(struct dwc2_hsotg *hsotg)
{
	u32 hprt0;

	if (hsotg->op_state == OTG_STATE_B_HOST) {
		/*
		 * Reset the port. During a HNP mode switch the reset
		 * needs to occur within 1ms and have a duration of at
		 * least 50ms.
		 */
		hprt0 = dwc2_read_hprt0(hsotg);
		hprt0 |= HPRT0_RST;
		writel(hprt0, hsotg->regs + HPRT0);
	}

	queue_delayed_work(hsotg->wq_otg, &hsotg->start_work,
			   msecs_to_jiffies(50));
}

/* Must be called with interrupt disabled and spinlock held */
static void dwc2_hcd_cleanup_channels(struct dwc2_hsotg *hsotg)
{
	int num_channels = hsotg->core_params->host_channels;
	struct dwc2_host_chan *channel;
	u32 hcchar;
	int i;

	if (hsotg->core_params->dma_enable <= 0) {
		/* Flush out any channel requests in slave mode */
		for (i = 0; i < num_channels; i++) {
			channel = hsotg->hc_ptr_array[i];
			if (!list_empty(&channel->hc_list_entry))
				continue;
			hcchar = readl(hsotg->regs + HCCHAR(i));
			if (hcchar & HCCHAR_CHENA) {
				hcchar &= ~(HCCHAR_CHENA | HCCHAR_EPDIR);
				hcchar |= HCCHAR_CHDIS;
				writel(hcchar, hsotg->regs + HCCHAR(i));
			}
		}
	}

	for (i = 0; i < num_channels; i++) {
		channel = hsotg->hc_ptr_array[i];
		if (!list_empty(&channel->hc_list_entry))
			continue;
		hcchar = readl(hsotg->regs + HCCHAR(i));
		if (hcchar & HCCHAR_CHENA) {
			/* Halt the channel */
			hcchar |= HCCHAR_CHDIS;
			writel(hcchar, hsotg->regs + HCCHAR(i));
		}

		dwc2_hc_cleanup(hsotg, channel);
		list_add_tail(&channel->hc_list_entry, &hsotg->free_hc_list);
		/*
		 * Added for Descriptor DMA to prevent channel double cleanup in
		 * release_channel_ddma(), which is called from ep_disable when
		 * device disconnects
		 */
		channel->qh = NULL;
	}
}

/**
 * dwc2_hcd_disconnect() - Handles disconnect of the HCD
 *
 * @hsotg: Pointer to struct dwc2_hsotg
 *
 * Must be called with interrupt disabled and spinlock held
 */
void dwc2_hcd_disconnect(struct dwc2_hsotg *hsotg)
{
	u32 intr;

	/* Set status flags for the hub driver */
	hsotg->flags.b.port_connect_status_change = 1;
	hsotg->flags.b.port_connect_status = 0;

	/*
	 * Shutdown any transfers in process by clearing the Tx FIFO Empty
	 * interrupt mask and status bits and disabling subsequent host
	 * channel interrupts.
	 */
	intr = readl(hsotg->regs + GINTMSK);
	intr &= ~(GINTSTS_NPTXFEMP | GINTSTS_PTXFEMP | GINTSTS_HCHINT);
	writel(intr, hsotg->regs + GINTMSK);
	intr = GINTSTS_NPTXFEMP | GINTSTS_PTXFEMP | GINTSTS_HCHINT;
	writel(intr, hsotg->regs + GINTSTS);

	/*
	 * Turn off the vbus power only if the core has transitioned to device
	 * mode. If still in host mode, need to keep power on to detect a
	 * reconnection.
	 */
	if (dwc2_is_device_mode(hsotg)) {
		if (hsotg->op_state != OTG_STATE_A_SUSPEND) {
			dev_dbg(hsotg->dev, "Disconnect: PortPower off\n");
			writel(0, hsotg->regs + HPRT0);
		}

		dwc2_disable_host_interrupts(hsotg);
	}

	/* Respond with an error status to all URBs in the schedule */
	dwc2_kill_all_urbs(hsotg);

	if (dwc2_is_host_mode(hsotg))
		/* Clean up any host channels that were in use */
		dwc2_hcd_cleanup_channels(hsotg);

	dwc2_host_disconnect(hsotg);
}

/**
 * dwc2_hcd_rem_wakeup() - Handles Remote Wakeup
 *
 * @hsotg: Pointer to struct dwc2_hsotg
 */
static void dwc2_hcd_rem_wakeup(struct dwc2_hsotg *hsotg)
{
	if (hsotg->lx_state == DWC2_L2)
		hsotg->flags.b.port_suspend_change = 1;
	else
		hsotg->flags.b.port_l1_change = 1;
}

/**
 * dwc2_hcd_stop() - Halts the DWC_otg host mode operations in a clean manner
 *
 * @hsotg: Pointer to struct dwc2_hsotg
 *
 * Must be called with interrupt disabled and spinlock held
 */
void dwc2_hcd_stop(struct dwc2_hsotg *hsotg)
{
	dev_dbg(hsotg->dev, "DWC OTG HCD STOP\n");

	/*
	 * The root hub should be disconnected before this function is called.
	 * The disconnect will clear the QTD lists (via ..._hcd_urb_dequeue)
	 * and the QH lists (via ..._hcd_endpoint_disable).
	 */

	/* Turn off all host-specific interrupts */
	dwc2_disable_host_interrupts(hsotg);

	/* Turn off the vbus power */
	dev_dbg(hsotg->dev, "PortPower off\n");
	writel(0, hsotg->regs + HPRT0);
}

static int dwc2_hcd_urb_enqueue(struct dwc2_hsotg *hsotg,
				struct dwc2_hcd_urb *urb, void **ep_handle,
				gfp_t mem_flags)
{
	struct dwc2_qtd *qtd;
	unsigned long flags;
	u32 intr_mask;
	int retval;

	if (!hsotg->flags.b.port_connect_status) {
		/* No longer connected */
		dev_err(hsotg->dev, "Not connected\n");
		return -ENODEV;
	}

	qtd = kzalloc(sizeof(*qtd), mem_flags);
	if (!qtd)
		return -ENOMEM;

	dwc2_hcd_qtd_init(qtd, urb);
	retval = dwc2_hcd_qtd_add(hsotg, qtd, (struct dwc2_qh **)ep_handle,
				  mem_flags);
	if (retval < 0) {
		dev_err(hsotg->dev,
			"DWC OTG HCD URB Enqueue failed adding QTD. Error status %d\n",
			retval);
		kfree(qtd);
		return retval;
	}

	intr_mask = readl(hsotg->regs + GINTMSK);
	if (!(intr_mask & GINTSTS_SOF) && retval == 0) {
		enum dwc2_transaction_type tr_type;

		if (qtd->qh->ep_type == USB_ENDPOINT_XFER_BULK &&
		    !(qtd->urb->flags & URB_GIVEBACK_ASAP))
			/*
			 * Do not schedule SG transactions until qtd has
			 * URB_GIVEBACK_ASAP set
			 */
			return 0;

		spin_lock_irqsave(&hsotg->lock, flags);
		tr_type = dwc2_hcd_select_transactions(hsotg);
		if (tr_type != DWC2_TRANSACTION_NONE)
			dwc2_hcd_queue_transactions(hsotg, tr_type);
		spin_unlock_irqrestore(&hsotg->lock, flags);
	}

	return retval;
}

/* Must be called with interrupt disabled and spinlock held */
static int dwc2_hcd_urb_dequeue(struct dwc2_hsotg *hsotg,
				struct dwc2_hcd_urb *urb)
{
	struct dwc2_qh *qh;
	struct dwc2_qtd *urb_qtd;

	urb_qtd = urb->qtd;
	if (!urb_qtd) {
		dev_dbg(hsotg->dev, "## Urb QTD is NULL ##\n");
		return -EINVAL;
	}

	qh = urb_qtd->qh;
	if (!qh) {
		dev_dbg(hsotg->dev, "## Urb QTD QH is NULL ##\n");
		return -EINVAL;
	}

	if (urb_qtd->in_process && qh->channel) {
		dwc2_dump_channel_info(hsotg, qh->channel);

		/* The QTD is in process (it has been assigned to a channel) */
		if (hsotg->flags.b.port_connect_status)
			/*
			 * If still connected (i.e. in host mode), halt the
			 * channel so it can be used for other transfers. If
			 * no longer connected, the host registers can't be
			 * written to halt the channel since the core is in
			 * device mode.
			 */
			dwc2_hc_halt(hsotg, qh->channel,
				     DWC2_HC_XFER_URB_DEQUEUE);
	}

	/*
	 * Free the QTD and clean up the associated QH. Leave the QH in the
	 * schedule if it has any remaining QTDs.
	 */
	if (hsotg->core_params->dma_desc_enable <= 0) {
		u8 in_process = urb_qtd->in_process;

		dwc2_hcd_qtd_unlink_and_free(hsotg, urb_qtd, qh);
		if (in_process) {
			dwc2_hcd_qh_deactivate(hsotg, qh, 0);
			qh->channel = NULL;
		} else if (list_empty(&qh->qtd_list)) {
			dwc2_hcd_qh_unlink(hsotg, qh);
		}
	} else {
		dwc2_hcd_qtd_unlink_and_free(hsotg, urb_qtd, qh);
	}

	return 0;
}

/* Must NOT be called with interrupt disabled or spinlock held */
static int dwc2_hcd_endpoint_disable(struct dwc2_hsotg *hsotg,
				     struct usb_host_endpoint *ep, int retry)
{
	struct dwc2_qtd *qtd, *qtd_tmp;
	struct dwc2_qh *qh;
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&hsotg->lock, flags);

	qh = ep->hcpriv;
	if (!qh) {
		rc = -EINVAL;
		goto err;
	}

	while (!list_empty(&qh->qtd_list) && retry--) {
		if (retry == 0) {
			dev_err(hsotg->dev,
				"## timeout in dwc2_hcd_endpoint_disable() ##\n");
			rc = -EBUSY;
			goto err;
		}

		spin_unlock_irqrestore(&hsotg->lock, flags);
		usleep_range(20000, 40000);
		spin_lock_irqsave(&hsotg->lock, flags);
		qh = ep->hcpriv;
		if (!qh) {
			rc = -EINVAL;
			goto err;
		}
	}

	dwc2_hcd_qh_unlink(hsotg, qh);

	/* Free each QTD in the QH's QTD list */
	list_for_each_entry_safe(qtd, qtd_tmp, &qh->qtd_list, qtd_list_entry)
		dwc2_hcd_qtd_unlink_and_free(hsotg, qtd, qh);

	ep->hcpriv = NULL;
	spin_unlock_irqrestore(&hsotg->lock, flags);
	dwc2_hcd_qh_free(hsotg, qh);

	return 0;

err:
	ep->hcpriv = NULL;
	spin_unlock_irqrestore(&hsotg->lock, flags);

	return rc;
}

/* Must be called with interrupt disabled and spinlock held */
static int dwc2_hcd_endpoint_reset(struct dwc2_hsotg *hsotg,
				   struct usb_host_endpoint *ep)
{
	struct dwc2_qh *qh = ep->hcpriv;

	if (!qh)
		return -EINVAL;

	qh->data_toggle = DWC2_HC_PID_DATA0;

	return 0;
}

/*
 * Initializes dynamic portions of the DWC_otg HCD state
 *
 * Must be called with interrupt disabled and spinlock held
 */
static void dwc2_hcd_reinit(struct dwc2_hsotg *hsotg)
{
	struct dwc2_host_chan *chan, *chan_tmp;
	int num_channels;
	int i;

	hsotg->flags.d32 = 0;

	hsotg->non_periodic_qh_ptr = &hsotg->non_periodic_sched_active;
	hsotg->non_periodic_channels = 0;
	hsotg->periodic_channels = 0;

	/*
	 * Put all channels in the free channel list and clean up channel
	 * states
	 */
	list_for_each_entry_safe(chan, chan_tmp, &hsotg->free_hc_list,
				 hc_list_entry)
		list_del_init(&chan->hc_list_entry);

	num_channels = hsotg->core_params->host_channels;
	for (i = 0; i < num_channels; i++) {
		chan = hsotg->hc_ptr_array[i];
		list_add_tail(&chan->hc_list_entry, &hsotg->free_hc_list);
		dwc2_hc_cleanup(hsotg, chan);
	}

	/* Initialize the DWC core for host mode operation */
	dwc2_core_host_init(hsotg);
}

static void dwc2_hc_init_split(struct dwc2_hsotg *hsotg,
			       struct dwc2_host_chan *chan,
			       struct dwc2_qtd *qtd, struct dwc2_hcd_urb *urb)
{
	int hub_addr, hub_port;

	chan->do_split = 1;
	chan->xact_pos = qtd->isoc_split_pos;
	chan->complete_split = qtd->complete_split;
	dwc2_host_hub_info(hsotg, urb->priv, &hub_addr, &hub_port);
	chan->hub_addr = (u8)hub_addr;
	chan->hub_port = (u8)hub_port;
}

static void *dwc2_hc_init_xfer(struct dwc2_hsotg *hsotg,
			       struct dwc2_host_chan *chan,
			       struct dwc2_qtd *qtd, void *bufptr)
{
	struct dwc2_hcd_urb *urb = qtd->urb;
	struct dwc2_hcd_iso_packet_desc *frame_desc;

	switch (dwc2_hcd_get_pipe_type(&urb->pipe_info)) {
	case USB_ENDPOINT_XFER_CONTROL:
		chan->ep_type = USB_ENDPOINT_XFER_CONTROL;

		switch (qtd->control_phase) {
		case DWC2_CONTROL_SETUP:
			dev_vdbg(hsotg->dev, "  Control setup transaction\n");
			chan->do_ping = 0;
			chan->ep_is_in = 0;
			chan->data_pid_start = DWC2_HC_PID_SETUP;
			if (hsotg->core_params->dma_enable > 0)
				chan->xfer_dma = urb->setup_dma;
			else
				chan->xfer_buf = urb->setup_packet;
			chan->xfer_len = 8;
			bufptr = NULL;
			break;

		case DWC2_CONTROL_DATA:
			dev_vdbg(hsotg->dev, "  Control data transaction\n");
			chan->data_pid_start = qtd->data_toggle;
			break;

		case DWC2_CONTROL_STATUS:
			/*
			 * Direction is opposite of data direction or IN if no
			 * data
			 */
			dev_vdbg(hsotg->dev, "  Control status transaction\n");
			if (urb->length == 0)
				chan->ep_is_in = 1;
			else
				chan->ep_is_in =
					dwc2_hcd_is_pipe_out(&urb->pipe_info);
			if (chan->ep_is_in)
				chan->do_ping = 0;
			chan->data_pid_start = DWC2_HC_PID_DATA1;
			chan->xfer_len = 0;
			if (hsotg->core_params->dma_enable > 0)
				chan->xfer_dma = hsotg->status_buf_dma;
			else
				chan->xfer_buf = hsotg->status_buf;
			bufptr = NULL;
			break;
		}
		break;

	case USB_ENDPOINT_XFER_BULK:
		chan->ep_type = USB_ENDPOINT_XFER_BULK;
		break;

	case USB_ENDPOINT_XFER_INT:
		chan->ep_type = USB_ENDPOINT_XFER_INT;
		break;

	case USB_ENDPOINT_XFER_ISOC:
		chan->ep_type = USB_ENDPOINT_XFER_ISOC;
		if (hsotg->core_params->dma_desc_enable > 0)
			break;

		frame_desc = &urb->iso_descs[qtd->isoc_frame_index];
		frame_desc->status = 0;

		if (hsotg->core_params->dma_enable > 0) {
			chan->xfer_dma = urb->dma;
			chan->xfer_dma += frame_desc->offset +
					qtd->isoc_split_offset;
		} else {
			chan->xfer_buf = urb->buf;
			chan->xfer_buf += frame_desc->offset +
					qtd->isoc_split_offset;
		}

		chan->xfer_len = frame_desc->length - qtd->isoc_split_offset;

		/* For non-dword aligned buffers */
		if (hsotg->core_params->dma_enable > 0 &&
		    (chan->xfer_dma & 0x3))
			bufptr = (u8 *)urb->buf + frame_desc->offset +
					qtd->isoc_split_offset;
		else
			bufptr = NULL;

		if (chan->xact_pos == DWC2_HCSPLT_XACTPOS_ALL) {
			if (chan->xfer_len <= 188)
				chan->xact_pos = DWC2_HCSPLT_XACTPOS_ALL;
			else
				chan->xact_pos = DWC2_HCSPLT_XACTPOS_BEGIN;
		}
		break;
	}

	return bufptr;
}

static int dwc2_hc_setup_align_buf(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh,
				   struct dwc2_host_chan *chan, void *bufptr)
{
	u32 buf_size;

	if (chan->ep_type != USB_ENDPOINT_XFER_ISOC)
		buf_size = hsotg->core_params->max_transfer_size;
	else
		buf_size = 4096;

	if (!qh->dw_align_buf) {
		qh->dw_align_buf = dma_alloc_coherent(hsotg->dev, buf_size,
						      &qh->dw_align_buf_dma,
						      GFP_ATOMIC);
		if (!qh->dw_align_buf)
			return -ENOMEM;
	}

	if (!chan->ep_is_in && chan->xfer_len) {
		dma_sync_single_for_cpu(hsotg->dev, chan->xfer_dma, buf_size,
					DMA_TO_DEVICE);
		memcpy(qh->dw_align_buf, bufptr, chan->xfer_len);
		dma_sync_single_for_device(hsotg->dev, chan->xfer_dma, buf_size,
					   DMA_TO_DEVICE);
	}

	chan->align_buf = qh->dw_align_buf_dma;
	return 0;
}

/**
 * dwc2_assign_and_init_hc() - Assigns transactions from a QTD to a free host
 * channel and initializes the host channel to perform the transactions. The
 * host channel is removed from the free list.
 *
 * @hsotg: The HCD state structure
 * @qh:    Transactions from the first QTD for this QH are selected and assigned
 *         to a free host channel
 */
static void dwc2_assign_and_init_hc(struct dwc2_hsotg *hsotg,
				    struct dwc2_qh *qh)
{
	struct dwc2_host_chan *chan;
	struct dwc2_hcd_urb *urb;
	struct dwc2_qtd *qtd;
	void *bufptr = NULL;

	if (dbg_qh(qh))
		dev_vdbg(hsotg->dev, "%s(%p,%p)\n", __func__, hsotg, qh);

	if (list_empty(&qh->qtd_list)) {
		dev_dbg(hsotg->dev, "No QTDs in QH list\n");
		return;
	}

	if (list_empty(&hsotg->free_hc_list)) {
		dev_dbg(hsotg->dev, "No free channel to assign\n");
		return;
	}

	chan = list_first_entry(&hsotg->free_hc_list, struct dwc2_host_chan,
				hc_list_entry);

	/* Remove the host channel from the free list */
	list_del_init(&chan->hc_list_entry);

	qtd = list_first_entry(&qh->qtd_list, struct dwc2_qtd, qtd_list_entry);
	urb = qtd->urb;
	qh->channel = chan;
	qtd->in_process = 1;

	/*
	 * Use usb_pipedevice to determine device address. This address is
	 * 0 before the SET_ADDRESS command and the correct address afterward.
	 */
	chan->dev_addr = dwc2_hcd_get_dev_addr(&urb->pipe_info);
	chan->ep_num = dwc2_hcd_get_ep_num(&urb->pipe_info);
	chan->speed = qh->dev_speed;
	chan->max_packet = dwc2_max_packet(qh->maxp);

	chan->xfer_started = 0;
	chan->halt_status = DWC2_HC_XFER_NO_HALT_STATUS;
	chan->error_state = (qtd->error_count > 0);
	chan->halt_on_queue = 0;
	chan->halt_pending = 0;
	chan->requests = 0;

	/*
	 * The following values may be modified in the transfer type section
	 * below. The xfer_len value may be reduced when the transfer is
	 * started to accommodate the max widths of the XferSize and PktCnt
	 * fields in the HCTSIZn register.
	 */

	chan->ep_is_in = (dwc2_hcd_is_pipe_in(&urb->pipe_info) != 0);
	if (chan->ep_is_in)
		chan->do_ping = 0;
	else
		chan->do_ping = qh->ping_state;

	chan->data_pid_start = qh->data_toggle;
	chan->multi_count = 1;

	if (hsotg->core_params->dma_enable > 0) {
		chan->xfer_dma = urb->dma + urb->actual_length;

		/* For non-dword aligned case */
		if (hsotg->core_params->dma_desc_enable <= 0 &&
		    (chan->xfer_dma & 0x3))
			bufptr = (u8 *)urb->buf + urb->actual_length;
	} else {
		chan->xfer_buf = (u8 *)urb->buf + urb->actual_length;
	}

	chan->xfer_len = urb->length - urb->actual_length;
	chan->xfer_count = 0;

	/* Set the split attributes if required */
	if (qh->do_split)
		dwc2_hc_init_split(hsotg, chan, qtd, urb);
	else
		chan->do_split = 0;

	/* Set the transfer attributes */
	bufptr = dwc2_hc_init_xfer(hsotg, chan, qtd, bufptr);

	/* Non DWORD-aligned buffer case */
	if (bufptr) {
		dev_vdbg(hsotg->dev, "Non-aligned buffer\n");
		if (dwc2_hc_setup_align_buf(hsotg, qh, chan, bufptr)) {
			dev_err(hsotg->dev,
				"%s: Failed to allocate memory to handle non-dword aligned buffer\n",
				__func__);
			/* Add channel back to free list */
			chan->align_buf = 0;
			chan->multi_count = 0;
			list_add_tail(&chan->hc_list_entry,
				      &hsotg->free_hc_list);
			qtd->in_process = 0;
			qh->channel = NULL;
			return;
		}
	} else {
		chan->align_buf = 0;
	}

	if (chan->ep_type == USB_ENDPOINT_XFER_INT ||
	    chan->ep_type == USB_ENDPOINT_XFER_ISOC)
		/*
		 * This value may be modified when the transfer is started
		 * to reflect the actual transfer length
		 */
		chan->multi_count = dwc2_hb_mult(qh->maxp);

	if (hsotg->core_params->dma_desc_enable > 0)
		chan->desc_list_addr = qh->desc_list_dma;

	dwc2_hc_init(hsotg, chan);
	chan->qh = qh;
}

/**
 * dwc2_hcd_select_transactions() - Selects transactions from the HCD transfer
 * schedule and assigns them to available host channels. Called from the HCD
 * interrupt handler functions.
 *
 * @hsotg: The HCD state structure
 *
 * Return: The types of new transactions that were assigned to host channels
 */
enum dwc2_transaction_type dwc2_hcd_select_transactions(
		struct dwc2_hsotg *hsotg)
{
	enum dwc2_transaction_type ret_val = DWC2_TRANSACTION_NONE;
	struct list_head *qh_ptr;
	struct dwc2_qh *qh;
	int num_channels;

#ifdef DWC2_DEBUG_SOF
	dev_vdbg(hsotg->dev, "  Select Transactions\n");
#endif

	/* Process entries in the periodic ready list */
	qh_ptr = hsotg->periodic_sched_ready.next;
	while (qh_ptr != &hsotg->periodic_sched_ready) {
		if (list_empty(&hsotg->free_hc_list))
			break;
		qh = list_entry(qh_ptr, struct dwc2_qh, qh_list_entry);
		dwc2_assign_and_init_hc(hsotg, qh);

		/*
		 * Move the QH from the periodic ready schedule to the
		 * periodic assigned schedule
		 */
		qh_ptr = qh_ptr->next;
		list_move(&qh->qh_list_entry, &hsotg->periodic_sched_assigned);
		ret_val = DWC2_TRANSACTION_PERIODIC;
	}

	/*
	 * Process entries in the inactive portion of the non-periodic
	 * schedule. Some free host channels may not be used if they are
	 * reserved for periodic transfers.
	 */
	num_channels = hsotg->core_params->host_channels;
	qh_ptr = hsotg->non_periodic_sched_inactive.next;
	while (qh_ptr != &hsotg->non_periodic_sched_inactive) {
		if (hsotg->non_periodic_channels >= num_channels -
						hsotg->periodic_channels)
			break;
		if (list_empty(&hsotg->free_hc_list))
			break;
		qh = list_entry(qh_ptr, struct dwc2_qh, qh_list_entry);
		dwc2_assign_and_init_hc(hsotg, qh);

		/*
		 * Move the QH from the non-periodic inactive schedule to the
		 * non-periodic active schedule
		 */
		qh_ptr = qh_ptr->next;
		list_move(&qh->qh_list_entry,
			  &hsotg->non_periodic_sched_active);

		if (ret_val == DWC2_TRANSACTION_NONE)
			ret_val = DWC2_TRANSACTION_NON_PERIODIC;
		else
			ret_val = DWC2_TRANSACTION_ALL;

		hsotg->non_periodic_channels++;
	}

	return ret_val;
}

/**
 * dwc2_queue_transaction() - Attempts to queue a single transaction request for
 * a host channel associated with either a periodic or non-periodic transfer
 *
 * @hsotg: The HCD state structure
 * @chan:  Host channel descriptor associated with either a periodic or
 *         non-periodic transfer
 * @fifo_dwords_avail: Number of DWORDs available in the periodic Tx FIFO
 *                     for periodic transfers or the non-periodic Tx FIFO
 *                     for non-periodic transfers
 *
 * Return: 1 if a request is queued and more requests may be needed to
 * complete the transfer, 0 if no more requests are required for this
 * transfer, -1 if there is insufficient space in the Tx FIFO
 *
 * This function assumes that there is space available in the appropriate
 * request queue. For an OUT transfer or SETUP transaction in Slave mode,
 * it checks whether space is available in the appropriate Tx FIFO.
 *
 * Must be called with interrupt disabled and spinlock held
 */
static int dwc2_queue_transaction(struct dwc2_hsotg *hsotg,
				  struct dwc2_host_chan *chan,
				  u16 fifo_dwords_avail)
{
	int retval = 0;

	if (hsotg->core_params->dma_enable > 0) {
		if (hsotg->core_params->dma_desc_enable > 0) {
			if (!chan->xfer_started ||
			    chan->ep_type == USB_ENDPOINT_XFER_ISOC) {
				dwc2_hcd_start_xfer_ddma(hsotg, chan->qh);
				chan->qh->ping_state = 0;
			}
		} else if (!chan->xfer_started) {
			dwc2_hc_start_transfer(hsotg, chan);
			chan->qh->ping_state = 0;
		}
	} else if (chan->halt_pending) {
		/* Don't queue a request if the channel has been halted */
	} else if (chan->halt_on_queue) {
		dwc2_hc_halt(hsotg, chan, chan->halt_status);
	} else if (chan->do_ping) {
		if (!chan->xfer_started)
			dwc2_hc_start_transfer(hsotg, chan);
	} else if (!chan->ep_is_in ||
		   chan->data_pid_start == DWC2_HC_PID_SETUP) {
		if ((fifo_dwords_avail * 4) >= chan->max_packet) {
			if (!chan->xfer_started) {
				dwc2_hc_start_transfer(hsotg, chan);
				retval = 1;
			} else {
				retval = dwc2_hc_continue_transfer(hsotg, chan);
			}
		} else {
			retval = -1;
		}
	} else {
		if (!chan->xfer_started) {
			dwc2_hc_start_transfer(hsotg, chan);
			retval = 1;
		} else {
			retval = dwc2_hc_continue_transfer(hsotg, chan);
		}
	}

	return retval;
}

/*
 * Processes periodic channels for the next frame and queues transactions for
 * these channels to the DWC_otg controller. After queueing transactions, the
 * Periodic Tx FIFO Empty interrupt is enabled if there are more transactions
 * to queue as Periodic Tx FIFO or request queue space becomes available.
 * Otherwise, the Periodic Tx FIFO Empty interrupt is disabled.
 *
 * Must be called with interrupt disabled and spinlock held
 */
static void dwc2_process_periodic_channels(struct dwc2_hsotg *hsotg)
{
	struct list_head *qh_ptr;
	struct dwc2_qh *qh;
	u32 tx_status;
	u32 fspcavail;
	u32 gintmsk;
	int status;
	int no_queue_space = 0;
	int no_fifo_space = 0;
	u32 qspcavail;

	if (dbg_perio())
		dev_vdbg(hsotg->dev, "Queue periodic transactions\n");

	tx_status = readl(hsotg->regs + HPTXSTS);
	qspcavail = tx_status >> TXSTS_QSPCAVAIL_SHIFT &
		    TXSTS_QSPCAVAIL_MASK >> TXSTS_QSPCAVAIL_SHIFT;
	fspcavail = tx_status >> TXSTS_FSPCAVAIL_SHIFT &
		    TXSTS_FSPCAVAIL_MASK >> TXSTS_FSPCAVAIL_SHIFT;

	if (dbg_perio()) {
		dev_vdbg(hsotg->dev, "  P Tx Req Queue Space Avail (before queue): %d\n",
			 qspcavail);
		dev_vdbg(hsotg->dev, "  P Tx FIFO Space Avail (before queue): %d\n",
			 fspcavail);
	}

	qh_ptr = hsotg->periodic_sched_assigned.next;
	while (qh_ptr != &hsotg->periodic_sched_assigned) {
		tx_status = readl(hsotg->regs + HPTXSTS);
		if ((tx_status & TXSTS_QSPCAVAIL_MASK) == 0) {
			no_queue_space = 1;
			break;
		}

		qh = list_entry(qh_ptr, struct dwc2_qh, qh_list_entry);
		if (!qh->channel) {
			qh_ptr = qh_ptr->next;
			continue;
		}

		/* Make sure EP's TT buffer is clean before queueing qtds */
		if (qh->tt_buffer_dirty) {
			qh_ptr = qh_ptr->next;
			continue;
		}

		/*
		 * Set a flag if we're queuing high-bandwidth in slave mode.
		 * The flag prevents any halts to get into the request queue in
		 * the middle of multiple high-bandwidth packets getting queued.
		 */
		if (hsotg->core_params->dma_enable <= 0 &&
				qh->channel->multi_count > 1)
			hsotg->queuing_high_bandwidth = 1;

		fspcavail = tx_status >> TXSTS_FSPCAVAIL_SHIFT &
			    TXSTS_FSPCAVAIL_MASK >> TXSTS_FSPCAVAIL_SHIFT;
		status = dwc2_queue_transaction(hsotg, qh->channel, fspcavail);
		if (status < 0) {
			no_fifo_space = 1;
			break;
		}

		/*
		 * In Slave mode, stay on the current transfer until there is
		 * nothing more to do or the high-bandwidth request count is
		 * reached. In DMA mode, only need to queue one request. The
		 * controller automatically handles multiple packets for
		 * high-bandwidth transfers.
		 */
		if (hsotg->core_params->dma_enable > 0 || status == 0 ||
		    qh->channel->requests == qh->channel->multi_count) {
			qh_ptr = qh_ptr->next;
			/*
			 * Move the QH from the periodic assigned schedule to
			 * the periodic queued schedule
			 */
			list_move(&qh->qh_list_entry,
				  &hsotg->periodic_sched_queued);

			/* done queuing high bandwidth */
			hsotg->queuing_high_bandwidth = 0;
		}
	}

	if (hsotg->core_params->dma_enable <= 0) {
		tx_status = readl(hsotg->regs + HPTXSTS);
		qspcavail = tx_status >> TXSTS_QSPCAVAIL_SHIFT &
			    TXSTS_QSPCAVAIL_MASK >> TXSTS_QSPCAVAIL_SHIFT;
		fspcavail = tx_status >> TXSTS_FSPCAVAIL_SHIFT &
			    TXSTS_FSPCAVAIL_MASK >> TXSTS_FSPCAVAIL_SHIFT;
		if (dbg_perio()) {
			dev_vdbg(hsotg->dev,
				 "  P Tx Req Queue Space Avail (after queue): %d\n",
				 qspcavail);
			dev_vdbg(hsotg->dev,
				 "  P Tx FIFO Space Avail (after queue): %d\n",
				 fspcavail);
		}

		if (!list_empty(&hsotg->periodic_sched_assigned) ||
		    no_queue_space || no_fifo_space) {
			/*
			 * May need to queue more transactions as the request
			 * queue or Tx FIFO empties. Enable the periodic Tx
			 * FIFO empty interrupt. (Always use the half-empty
			 * level to ensure that new requests are loaded as
			 * soon as possible.)
			 */
			gintmsk = readl(hsotg->regs + GINTMSK);
			gintmsk |= GINTSTS_PTXFEMP;
			writel(gintmsk, hsotg->regs + GINTMSK);
		} else {
			/*
			 * Disable the Tx FIFO empty interrupt since there are
			 * no more transactions that need to be queued right
			 * now. This function is called from interrupt
			 * handlers to queue more transactions as transfer
			 * states change.
			 */
			gintmsk = readl(hsotg->regs + GINTMSK);
			gintmsk &= ~GINTSTS_PTXFEMP;
			writel(gintmsk, hsotg->regs + GINTMSK);
		}
	}
}

/*
 * Processes active non-periodic channels and queues transactions for these
 * channels to the DWC_otg controller. After queueing transactions, the NP Tx
 * FIFO Empty interrupt is enabled if there are more transactions to queue as
 * NP Tx FIFO or request queue space becomes available. Otherwise, the NP Tx
 * FIFO Empty interrupt is disabled.
 *
 * Must be called with interrupt disabled and spinlock held
 */
static void dwc2_process_non_periodic_channels(struct dwc2_hsotg *hsotg)
{
	struct list_head *orig_qh_ptr;
	struct dwc2_qh *qh;
	u32 tx_status;
	u32 qspcavail;
	u32 fspcavail;
	u32 gintmsk;
	int status;
	int no_queue_space = 0;
	int no_fifo_space = 0;
	int more_to_do = 0;

	dev_vdbg(hsotg->dev, "Queue non-periodic transactions\n");

	tx_status = readl(hsotg->regs + GNPTXSTS);
	qspcavail = tx_status >> TXSTS_QSPCAVAIL_SHIFT &
		    TXSTS_QSPCAVAIL_MASK >> TXSTS_QSPCAVAIL_SHIFT;
	fspcavail = tx_status >> TXSTS_FSPCAVAIL_SHIFT &
		    TXSTS_FSPCAVAIL_MASK >> TXSTS_FSPCAVAIL_SHIFT;
	dev_vdbg(hsotg->dev, "  NP Tx Req Queue Space Avail (before queue): %d\n",
		 qspcavail);
	dev_vdbg(hsotg->dev, "  NP Tx FIFO Space Avail (before queue): %d\n",
		 fspcavail);

	/*
	 * Keep track of the starting point. Skip over the start-of-list
	 * entry.
	 */
	if (hsotg->non_periodic_qh_ptr == &hsotg->non_periodic_sched_active)
		hsotg->non_periodic_qh_ptr = hsotg->non_periodic_qh_ptr->next;
	orig_qh_ptr = hsotg->non_periodic_qh_ptr;

	/*
	 * Process once through the active list or until no more space is
	 * available in the request queue or the Tx FIFO
	 */
	do {
		tx_status = readl(hsotg->regs + GNPTXSTS);
		qspcavail = tx_status >> TXSTS_QSPCAVAIL_SHIFT &
			    TXSTS_QSPCAVAIL_MASK >> TXSTS_QSPCAVAIL_SHIFT;
		if (hsotg->core_params->dma_enable <= 0 && qspcavail == 0) {
			no_queue_space = 1;
			break;
		}

		qh = list_entry(hsotg->non_periodic_qh_ptr, struct dwc2_qh,
				qh_list_entry);
		if (!qh->channel)
			goto next;

		/* Make sure EP's TT buffer is clean before queueing qtds */
		if (qh->tt_buffer_dirty)
			goto next;

		fspcavail = tx_status >> TXSTS_FSPCAVAIL_SHIFT &
			    TXSTS_FSPCAVAIL_MASK >> TXSTS_FSPCAVAIL_SHIFT;
		status = dwc2_queue_transaction(hsotg, qh->channel, fspcavail);

		if (status > 0) {
			more_to_do = 1;
		} else if (status < 0) {
			no_fifo_space = 1;
			break;
		}
next:
		/* Advance to next QH, skipping start-of-list entry */
		hsotg->non_periodic_qh_ptr = hsotg->non_periodic_qh_ptr->next;
		if (hsotg->non_periodic_qh_ptr ==
				&hsotg->non_periodic_sched_active)
			hsotg->non_periodic_qh_ptr =
					hsotg->non_periodic_qh_ptr->next;
	} while (hsotg->non_periodic_qh_ptr != orig_qh_ptr);

	if (hsotg->core_params->dma_enable <= 0) {
		tx_status = readl(hsotg->regs + GNPTXSTS);
		qspcavail = tx_status >> TXSTS_QSPCAVAIL_SHIFT &
			    TXSTS_QSPCAVAIL_MASK >> TXSTS_QSPCAVAIL_SHIFT;
		fspcavail = tx_status >> TXSTS_FSPCAVAIL_SHIFT &
			    TXSTS_FSPCAVAIL_MASK >> TXSTS_FSPCAVAIL_SHIFT;
		dev_vdbg(hsotg->dev,
			 "  NP Tx Req Queue Space Avail (after queue): %d\n",
			 qspcavail);
		dev_vdbg(hsotg->dev,
			 "  NP Tx FIFO Space Avail (after queue): %d\n",
			 fspcavail);

		if (more_to_do || no_queue_space || no_fifo_space) {
			/*
			 * May need to queue more transactions as the request
			 * queue or Tx FIFO empties. Enable the non-periodic
			 * Tx FIFO empty interrupt. (Always use the half-empty
			 * level to ensure that new requests are loaded as
			 * soon as possible.)
			 */
			gintmsk = readl(hsotg->regs + GINTMSK);
			gintmsk |= GINTSTS_NPTXFEMP;
			writel(gintmsk, hsotg->regs + GINTMSK);
		} else {
			/*
			 * Disable the Tx FIFO empty interrupt since there are
			 * no more transactions that need to be queued right
			 * now. This function is called from interrupt
			 * handlers to queue more transactions as transfer
			 * states change.
			 */
			gintmsk = readl(hsotg->regs + GINTMSK);
			gintmsk &= ~GINTSTS_NPTXFEMP;
			writel(gintmsk, hsotg->regs + GINTMSK);
		}
	}
}

/**
 * dwc2_hcd_queue_transactions() - Processes the currently active host channels
 * and queues transactions for these channels to the DWC_otg controller. Called
 * from the HCD interrupt handler functions.
 *
 * @hsotg:   The HCD state structure
 * @tr_type: The type(s) of transactions to queue (non-periodic, periodic,
 *           or both)
 *
 * Must be called with interrupt disabled and spinlock held
 */
void dwc2_hcd_queue_transactions(struct dwc2_hsotg *hsotg,
				 enum dwc2_transaction_type tr_type)
{
#ifdef DWC2_DEBUG_SOF
	dev_vdbg(hsotg->dev, "Queue Transactions\n");
#endif
	/* Process host channels associated with periodic transfers */
	if ((tr_type == DWC2_TRANSACTION_PERIODIC ||
	     tr_type == DWC2_TRANSACTION_ALL) &&
	    !list_empty(&hsotg->periodic_sched_assigned))
		dwc2_process_periodic_channels(hsotg);

	/* Process host channels associated with non-periodic transfers */
	if (tr_type == DWC2_TRANSACTION_NON_PERIODIC ||
	    tr_type == DWC2_TRANSACTION_ALL) {
		if (!list_empty(&hsotg->non_periodic_sched_active)) {
			dwc2_process_non_periodic_channels(hsotg);
		} else {
			/*
			 * Ensure NP Tx FIFO empty interrupt is disabled when
			 * there are no non-periodic transfers to process
			 */
			u32 gintmsk = readl(hsotg->regs + GINTMSK);

			gintmsk &= ~GINTSTS_NPTXFEMP;
			writel(gintmsk, hsotg->regs + GINTMSK);
		}
	}
}

static void dwc2_conn_id_status_change(struct work_struct *work)
{
	struct dwc2_hsotg *hsotg = container_of(work, struct dwc2_hsotg,
						wf_otg);
	u32 count = 0;
	u32 gotgctl;

	dev_dbg(hsotg->dev, "%s()\n", __func__);

	gotgctl = readl(hsotg->regs + GOTGCTL);
	dev_dbg(hsotg->dev, "gotgctl=%0x\n", gotgctl);
	dev_dbg(hsotg->dev, "gotgctl.b.conidsts=%d\n",
		!!(gotgctl & GOTGCTL_CONID_B));

	/* B-Device connector (Device Mode) */
	if (gotgctl & GOTGCTL_CONID_B) {
		/* Wait for switch to device mode */
		dev_dbg(hsotg->dev, "connId B\n");
		while (!dwc2_is_device_mode(hsotg)) {
			dev_info(hsotg->dev,
				 "Waiting for Peripheral Mode, Mode=%s\n",
				 dwc2_is_host_mode(hsotg) ? "Host" :
				 "Peripheral");
			usleep_range(20000, 40000);
			if (++count > 250)
				break;
		}
		if (count > 250)
			dev_err(hsotg->dev,
				"Connection id status change timed out\n");
		hsotg->op_state = OTG_STATE_B_PERIPHERAL;
		dwc2_core_init(hsotg, false, -1);
		dwc2_enable_global_interrupts(hsotg);
	} else {
		/* A-Device connector (Host Mode) */
		dev_dbg(hsotg->dev, "connId A\n");
		while (!dwc2_is_host_mode(hsotg)) {
			dev_info(hsotg->dev, "Waiting for Host Mode, Mode=%s\n",
				 dwc2_is_host_mode(hsotg) ?
				 "Host" : "Peripheral");
			usleep_range(20000, 40000);
			if (++count > 250)
				break;
		}
		if (count > 250)
			dev_err(hsotg->dev,
				"Connection id status change timed out\n");
		hsotg->op_state = OTG_STATE_A_HOST;

		/* Initialize the Core for Host mode */
		dwc2_core_init(hsotg, false, -1);
		dwc2_enable_global_interrupts(hsotg);
		dwc2_hcd_start(hsotg);
	}
}

static void dwc2_wakeup_detected(unsigned long data)
{
	struct dwc2_hsotg *hsotg = (struct dwc2_hsotg *)data;
	u32 hprt0;

	dev_dbg(hsotg->dev, "%s()\n", __func__);

	/*
	 * Clear the Resume after 70ms. (Need 20 ms minimum. Use 70 ms
	 * so that OPT tests pass with all PHYs.)
	 */
	hprt0 = dwc2_read_hprt0(hsotg);
	dev_dbg(hsotg->dev, "Resume: HPRT0=%0x\n", hprt0);
	hprt0 &= ~HPRT0_RES;
	writel(hprt0, hsotg->regs + HPRT0);
	dev_dbg(hsotg->dev, "Clear Resume: HPRT0=%0x\n",
		readl(hsotg->regs + HPRT0));

	dwc2_hcd_rem_wakeup(hsotg);

	/* Change to L0 state */
	hsotg->lx_state = DWC2_L0;
}

static int dwc2_host_is_b_hnp_enabled(struct dwc2_hsotg *hsotg)
{
	struct usb_hcd *hcd = dwc2_hsotg_to_hcd(hsotg);

	return hcd->self.b_hnp_enable;
}

/* Must NOT be called with interrupt disabled or spinlock held */
static void dwc2_port_suspend(struct dwc2_hsotg *hsotg, u16 windex)
{
	unsigned long flags;
	u32 hprt0;
	u32 pcgctl;
	u32 gotgctl;

	dev_dbg(hsotg->dev, "%s()\n", __func__);

	spin_lock_irqsave(&hsotg->lock, flags);

	if (windex == hsotg->otg_port && dwc2_host_is_b_hnp_enabled(hsotg)) {
		gotgctl = readl(hsotg->regs + GOTGCTL);
		gotgctl |= GOTGCTL_HSTSETHNPEN;
		writel(gotgctl, hsotg->regs + GOTGCTL);
		hsotg->op_state = OTG_STATE_A_SUSPEND;
	}

	hprt0 = dwc2_read_hprt0(hsotg);
	hprt0 |= HPRT0_SUSP;
	writel(hprt0, hsotg->regs + HPRT0);

	/* Update lx_state */
	hsotg->lx_state = DWC2_L2;

	/* Suspend the Phy Clock */
	pcgctl = readl(hsotg->regs + PCGCTL);
	pcgctl |= PCGCTL_STOPPCLK;
	writel(pcgctl, hsotg->regs + PCGCTL);
	udelay(10);

	/* For HNP the bus must be suspended for at least 200ms */
	if (dwc2_host_is_b_hnp_enabled(hsotg)) {
		pcgctl = readl(hsotg->regs + PCGCTL);
		pcgctl &= ~PCGCTL_STOPPCLK;
		writel(pcgctl, hsotg->regs + PCGCTL);

		spin_unlock_irqrestore(&hsotg->lock, flags);

		usleep_range(200000, 250000);
	} else {
		spin_unlock_irqrestore(&hsotg->lock, flags);
	}
}

/* Handles hub class-specific requests */
static int dwc2_hcd_hub_control(struct dwc2_hsotg *hsotg, u16 typereq,
				u16 wvalue, u16 windex, char *buf, u16 wlength)
{
	struct usb_hub_descriptor *hub_desc;
	int retval = 0;
	u32 hprt0;
	u32 port_status;
	u32 speed;
	u32 pcgctl;

	switch (typereq) {
	case ClearHubFeature:
		dev_dbg(hsotg->dev, "ClearHubFeature %1xh\n", wvalue);

		switch (wvalue) {
		case C_HUB_LOCAL_POWER:
		case C_HUB_OVER_CURRENT:
			/* Nothing required here */
			break;

		default:
			retval = -EINVAL;
			dev_err(hsotg->dev,
				"ClearHubFeature request %1xh unknown\n",
				wvalue);
		}
		break;

	case ClearPortFeature:
		if (wvalue != USB_PORT_FEAT_L1)
			if (!windex || windex > 1)
				goto error;
		switch (wvalue) {
		case USB_PORT_FEAT_ENABLE:
			dev_dbg(hsotg->dev,
				"ClearPortFeature USB_PORT_FEAT_ENABLE\n");
			hprt0 = dwc2_read_hprt0(hsotg);
			hprt0 |= HPRT0_ENA;
			writel(hprt0, hsotg->regs + HPRT0);
			break;

		case USB_PORT_FEAT_SUSPEND:
			dev_dbg(hsotg->dev,
				"ClearPortFeature USB_PORT_FEAT_SUSPEND\n");
			writel(0, hsotg->regs + PCGCTL);
			usleep_range(20000, 40000);

			hprt0 = dwc2_read_hprt0(hsotg);
			hprt0 |= HPRT0_RES;
			writel(hprt0, hsotg->regs + HPRT0);
			hprt0 &= ~HPRT0_SUSP;
			usleep_range(100000, 150000);

			hprt0 &= ~HPRT0_RES;
			writel(hprt0, hsotg->regs + HPRT0);
			break;

		case USB_PORT_FEAT_POWER:
			dev_dbg(hsotg->dev,
				"ClearPortFeature USB_PORT_FEAT_POWER\n");
			hprt0 = dwc2_read_hprt0(hsotg);
			hprt0 &= ~HPRT0_PWR;
			writel(hprt0, hsotg->regs + HPRT0);
			break;

		case USB_PORT_FEAT_INDICATOR:
			dev_dbg(hsotg->dev,
				"ClearPortFeature USB_PORT_FEAT_INDICATOR\n");
			/* Port indicator not supported */
			break;

		case USB_PORT_FEAT_C_CONNECTION:
			/*
			 * Clears driver's internal Connect Status Change flag
			 */
			dev_dbg(hsotg->dev,
				"ClearPortFeature USB_PORT_FEAT_C_CONNECTION\n");
			hsotg->flags.b.port_connect_status_change = 0;
			break;

		case USB_PORT_FEAT_C_RESET:
			/* Clears driver's internal Port Reset Change flag */
			dev_dbg(hsotg->dev,
				"ClearPortFeature USB_PORT_FEAT_C_RESET\n");
			hsotg->flags.b.port_reset_change = 0;
			break;

		case USB_PORT_FEAT_C_ENABLE:
			/*
			 * Clears the driver's internal Port Enable/Disable
			 * Change flag
			 */
			dev_dbg(hsotg->dev,
				"ClearPortFeature USB_PORT_FEAT_C_ENABLE\n");
			hsotg->flags.b.port_enable_change = 0;
			break;

		case USB_PORT_FEAT_C_SUSPEND:
			/*
			 * Clears the driver's internal Port Suspend Change
			 * flag, which is set when resume signaling on the host
			 * port is complete
			 */
			dev_dbg(hsotg->dev,
				"ClearPortFeature USB_PORT_FEAT_C_SUSPEND\n");
			hsotg->flags.b.port_suspend_change = 0;
			break;

		case USB_PORT_FEAT_C_PORT_L1:
			dev_dbg(hsotg->dev,
				"ClearPortFeature USB_PORT_FEAT_C_PORT_L1\n");
			hsotg->flags.b.port_l1_change = 0;
			break;

		case USB_PORT_FEAT_C_OVER_CURRENT:
			dev_dbg(hsotg->dev,
				"ClearPortFeature USB_PORT_FEAT_C_OVER_CURRENT\n");
			hsotg->flags.b.port_over_current_change = 0;
			break;

		default:
			retval = -EINVAL;
			dev_err(hsotg->dev,
				"ClearPortFeature request %1xh unknown or unsupported\n",
				wvalue);
		}
		break;

	case GetHubDescriptor:
		dev_dbg(hsotg->dev, "GetHubDescriptor\n");
		hub_desc = (struct usb_hub_descriptor *)buf;
		hub_desc->bDescLength = 9;
		hub_desc->bDescriptorType = 0x29;
		hub_desc->bNbrPorts = 1;
		hub_desc->wHubCharacteristics = cpu_to_le16(0x08);
		hub_desc->bPwrOn2PwrGood = 1;
		hub_desc->bHubContrCurrent = 0;
		hub_desc->u.hs.DeviceRemovable[0] = 0;
		hub_desc->u.hs.DeviceRemovable[1] = 0xff;
		break;

	case GetHubStatus:
		dev_dbg(hsotg->dev, "GetHubStatus\n");
		memset(buf, 0, 4);
		break;

	case GetPortStatus:
		dev_vdbg(hsotg->dev,
			 "GetPortStatus wIndex=0x%04x flags=0x%08x\n", windex,
			 hsotg->flags.d32);
		if (!windex || windex > 1)
			goto error;

		port_status = 0;
		if (hsotg->flags.b.port_connect_status_change)
			port_status |= USB_PORT_STAT_C_CONNECTION << 16;
		if (hsotg->flags.b.port_enable_change)
			port_status |= USB_PORT_STAT_C_ENABLE << 16;
		if (hsotg->flags.b.port_suspend_change)
			port_status |= USB_PORT_STAT_C_SUSPEND << 16;
		if (hsotg->flags.b.port_l1_change)
			port_status |= USB_PORT_STAT_C_L1 << 16;
		if (hsotg->flags.b.port_reset_change)
			port_status |= USB_PORT_STAT_C_RESET << 16;
		if (hsotg->flags.b.port_over_current_change) {
			dev_warn(hsotg->dev, "Overcurrent change detected\n");
			port_status |= USB_PORT_STAT_C_OVERCURRENT << 16;
		}

		if (!hsotg->flags.b.port_connect_status) {
			/*
			 * The port is disconnected, which means the core is
			 * either in device mode or it soon will be. Just
			 * return 0's for the remainder of the port status
			 * since the port register can't be read if the core
			 * is in device mode.
			 */
			*(__le32 *)buf = cpu_to_le32(port_status);
			break;
		}

		hprt0 = readl(hsotg->regs + HPRT0);
		dev_vdbg(hsotg->dev, "  HPRT0: 0x%08x\n", hprt0);

		if (hprt0 & HPRT0_CONNSTS)
			port_status |= USB_PORT_STAT_CONNECTION;
		if (hprt0 & HPRT0_ENA)
			port_status |= USB_PORT_STAT_ENABLE;
		if (hprt0 & HPRT0_SUSP)
			port_status |= USB_PORT_STAT_SUSPEND;
		if (hprt0 & HPRT0_OVRCURRACT)
			port_status |= USB_PORT_STAT_OVERCURRENT;
		if (hprt0 & HPRT0_RST)
			port_status |= USB_PORT_STAT_RESET;
		if (hprt0 & HPRT0_PWR)
			port_status |= USB_PORT_STAT_POWER;

		speed = hprt0 & HPRT0_SPD_MASK;
		if (speed == HPRT0_SPD_HIGH_SPEED)
			port_status |= USB_PORT_STAT_HIGH_SPEED;
		else if (speed == HPRT0_SPD_LOW_SPEED)
			port_status |= USB_PORT_STAT_LOW_SPEED;

		if (hprt0 & HPRT0_TSTCTL_MASK)
			port_status |= USB_PORT_STAT_TEST;
		/* USB_PORT_FEAT_INDICATOR unsupported always 0 */

		dev_vdbg(hsotg->dev, "port_status=%08x\n", port_status);
		*(__le32 *)buf = cpu_to_le32(port_status);
		break;

	case SetHubFeature:
		dev_dbg(hsotg->dev, "SetHubFeature\n");
		/* No HUB features supported */
		break;

	case SetPortFeature:
		dev_dbg(hsotg->dev, "SetPortFeature\n");
		if (wvalue != USB_PORT_FEAT_TEST && (!windex || windex > 1))
			goto error;

		if (!hsotg->flags.b.port_connect_status) {
			/*
			 * The port is disconnected, which means the core is
			 * either in device mode or it soon will be. Just
			 * return without doing anything since the port
			 * register can't be written if the core is in device
			 * mode.
			 */
			break;
		}

		switch (wvalue) {
		case USB_PORT_FEAT_SUSPEND:
			dev_dbg(hsotg->dev,
				"SetPortFeature - USB_PORT_FEAT_SUSPEND\n");
			if (windex != hsotg->otg_port)
				goto error;
			dwc2_port_suspend(hsotg, windex);
			break;

		case USB_PORT_FEAT_POWER:
			dev_dbg(hsotg->dev,
				"SetPortFeature - USB_PORT_FEAT_POWER\n");
			hprt0 = dwc2_read_hprt0(hsotg);
			hprt0 |= HPRT0_PWR;
			writel(hprt0, hsotg->regs + HPRT0);
			break;

		case USB_PORT_FEAT_RESET:
			hprt0 = dwc2_read_hprt0(hsotg);
			dev_dbg(hsotg->dev,
				"SetPortFeature - USB_PORT_FEAT_RESET\n");
			pcgctl = readl(hsotg->regs + PCGCTL);
			pcgctl &= ~(PCGCTL_ENBL_SLEEP_GATING | PCGCTL_STOPPCLK);
			writel(pcgctl, hsotg->regs + PCGCTL);
			/* ??? Original driver does this */
			writel(0, hsotg->regs + PCGCTL);

			hprt0 = dwc2_read_hprt0(hsotg);
			/* Clear suspend bit if resetting from suspend state */
			hprt0 &= ~HPRT0_SUSP;

			/*
			 * When B-Host the Port reset bit is set in the Start
			 * HCD Callback function, so that the reset is started
			 * within 1ms of the HNP success interrupt
			 */
			if (!dwc2_hcd_is_b_host(hsotg)) {
				hprt0 |= HPRT0_PWR | HPRT0_RST;
				dev_dbg(hsotg->dev,
					"In host mode, hprt0=%08x\n", hprt0);
				writel(hprt0, hsotg->regs + HPRT0);
			}

			/* Clear reset bit in 10ms (FS/LS) or 50ms (HS) */
			usleep_range(50000, 70000);
			hprt0 &= ~HPRT0_RST;
			writel(hprt0, hsotg->regs + HPRT0);
			hsotg->lx_state = DWC2_L0; /* Now back to On state */
			break;

		case USB_PORT_FEAT_INDICATOR:
			dev_dbg(hsotg->dev,
				"SetPortFeature - USB_PORT_FEAT_INDICATOR\n");
			/* Not supported */
			break;

		default:
			retval = -EINVAL;
			dev_err(hsotg->dev,
				"SetPortFeature %1xh unknown or unsupported\n",
				wvalue);
			break;
		}
		break;

	default:
error:
		retval = -EINVAL;
		dev_dbg(hsotg->dev,
			"Unknown hub control request: %1xh wIndex: %1xh wValue: %1xh\n",
			typereq, windex, wvalue);
		break;
	}

	return retval;
}

static int dwc2_hcd_is_status_changed(struct dwc2_hsotg *hsotg, int port)
{
	int retval;

	if (port != 1)
		return -EINVAL;

	retval = (hsotg->flags.b.port_connect_status_change ||
		  hsotg->flags.b.port_reset_change ||
		  hsotg->flags.b.port_enable_change ||
		  hsotg->flags.b.port_suspend_change ||
		  hsotg->flags.b.port_over_current_change);

	if (retval) {
		dev_dbg(hsotg->dev,
			"DWC OTG HCD HUB STATUS DATA: Root port status changed\n");
		dev_dbg(hsotg->dev, "  port_connect_status_change: %d\n",
			hsotg->flags.b.port_connect_status_change);
		dev_dbg(hsotg->dev, "  port_reset_change: %d\n",
			hsotg->flags.b.port_reset_change);
		dev_dbg(hsotg->dev, "  port_enable_change: %d\n",
			hsotg->flags.b.port_enable_change);
		dev_dbg(hsotg->dev, "  port_suspend_change: %d\n",
			hsotg->flags.b.port_suspend_change);
		dev_dbg(hsotg->dev, "  port_over_current_change: %d\n",
			hsotg->flags.b.port_over_current_change);
	}

	return retval;
}

int dwc2_hcd_get_frame_number(struct dwc2_hsotg *hsotg)
{
	u32 hfnum = readl(hsotg->regs + HFNUM);

#ifdef DWC2_DEBUG_SOF
	dev_vdbg(hsotg->dev, "DWC OTG HCD GET FRAME NUMBER %d\n",
		 hfnum >> HFNUM_FRNUM_SHIFT &
		 HFNUM_FRNUM_MASK >> HFNUM_FRNUM_SHIFT);
#endif
	return hfnum >> HFNUM_FRNUM_SHIFT &
	       HFNUM_FRNUM_MASK >> HFNUM_FRNUM_SHIFT;
}

int dwc2_hcd_is_b_host(struct dwc2_hsotg *hsotg)
{
	return (hsotg->op_state == OTG_STATE_B_HOST);
}

static struct dwc2_hcd_urb *dwc2_hcd_urb_alloc(struct dwc2_hsotg *hsotg,
					       int iso_desc_count,
					       gfp_t mem_flags)
{
	struct dwc2_hcd_urb *urb;
	u32 size = sizeof(*urb) + iso_desc_count *
		   sizeof(struct dwc2_hcd_iso_packet_desc);

	urb = kzalloc(size, mem_flags);
	if (urb)
		urb->packet_count = iso_desc_count;
	return urb;
}

static void dwc2_hcd_urb_set_pipeinfo(struct dwc2_hsotg *hsotg,
				      struct dwc2_hcd_urb *urb, u8 dev_addr,
				      u8 ep_num, u8 ep_type, u8 ep_dir, u16 mps)
{
	if (dbg_perio() ||
	    ep_type == USB_ENDPOINT_XFER_BULK ||
	    ep_type == USB_ENDPOINT_XFER_CONTROL)
		dev_vdbg(hsotg->dev,
			 "addr=%d, ep_num=%d, ep_dir=%1x, ep_type=%1x, mps=%d\n",
			 dev_addr, ep_num, ep_dir, ep_type, mps);
	urb->pipe_info.dev_addr = dev_addr;
	urb->pipe_info.ep_num = ep_num;
	urb->pipe_info.pipe_type = ep_type;
	urb->pipe_info.pipe_dir = ep_dir;
	urb->pipe_info.mps = mps;
}

/*
 * NOTE: This function will be removed once the peripheral controller code
 * is integrated and the driver is stable
 */
void dwc2_hcd_dump_state(struct dwc2_hsotg *hsotg)
{
#ifdef DEBUG
	struct dwc2_host_chan *chan;
	struct dwc2_hcd_urb *urb;
	struct dwc2_qtd *qtd;
	int num_channels;
	u32 np_tx_status;
	u32 p_tx_status;
	int i;

	num_channels = hsotg->core_params->host_channels;
	dev_dbg(hsotg->dev, "\n");
	dev_dbg(hsotg->dev,
		"************************************************************\n");
	dev_dbg(hsotg->dev, "HCD State:\n");
	dev_dbg(hsotg->dev, "  Num channels: %d\n", num_channels);

	for (i = 0; i < num_channels; i++) {
		chan = hsotg->hc_ptr_array[i];
		dev_dbg(hsotg->dev, "  Channel %d:\n", i);
		dev_dbg(hsotg->dev,
			"    dev_addr: %d, ep_num: %d, ep_is_in: %d\n",
			chan->dev_addr, chan->ep_num, chan->ep_is_in);
		dev_dbg(hsotg->dev, "    speed: %d\n", chan->speed);
		dev_dbg(hsotg->dev, "    ep_type: %d\n", chan->ep_type);
		dev_dbg(hsotg->dev, "    max_packet: %d\n", chan->max_packet);
		dev_dbg(hsotg->dev, "    data_pid_start: %d\n",
			chan->data_pid_start);
		dev_dbg(hsotg->dev, "    multi_count: %d\n", chan->multi_count);
		dev_dbg(hsotg->dev, "    xfer_started: %d\n",
			chan->xfer_started);
		dev_dbg(hsotg->dev, "    xfer_buf: %p\n", chan->xfer_buf);
		dev_dbg(hsotg->dev, "    xfer_dma: %08lx\n",
			(unsigned long)chan->xfer_dma);
		dev_dbg(hsotg->dev, "    xfer_len: %d\n", chan->xfer_len);
		dev_dbg(hsotg->dev, "    xfer_count: %d\n", chan->xfer_count);
		dev_dbg(hsotg->dev, "    halt_on_queue: %d\n",
			chan->halt_on_queue);
		dev_dbg(hsotg->dev, "    halt_pending: %d\n",
			chan->halt_pending);
		dev_dbg(hsotg->dev, "    halt_status: %d\n", chan->halt_status);
		dev_dbg(hsotg->dev, "    do_split: %d\n", chan->do_split);
		dev_dbg(hsotg->dev, "    complete_split: %d\n",
			chan->complete_split);
		dev_dbg(hsotg->dev, "    hub_addr: %d\n", chan->hub_addr);
		dev_dbg(hsotg->dev, "    hub_port: %d\n", chan->hub_port);
		dev_dbg(hsotg->dev, "    xact_pos: %d\n", chan->xact_pos);
		dev_dbg(hsotg->dev, "    requests: %d\n", chan->requests);
		dev_dbg(hsotg->dev, "    qh: %p\n", chan->qh);

		if (chan->xfer_started) {
			u32 hfnum, hcchar, hctsiz, hcint, hcintmsk;

			hfnum = readl(hsotg->regs + HFNUM);
			hcchar = readl(hsotg->regs + HCCHAR(i));
			hctsiz = readl(hsotg->regs + HCTSIZ(i));
			hcint = readl(hsotg->regs + HCINT(i));
			hcintmsk = readl(hsotg->regs + HCINTMSK(i));
			dev_dbg(hsotg->dev, "    hfnum: 0x%08x\n", hfnum);
			dev_dbg(hsotg->dev, "    hcchar: 0x%08x\n", hcchar);
			dev_dbg(hsotg->dev, "    hctsiz: 0x%08x\n", hctsiz);
			dev_dbg(hsotg->dev, "    hcint: 0x%08x\n", hcint);
			dev_dbg(hsotg->dev, "    hcintmsk: 0x%08x\n", hcintmsk);
		}

		if (!(chan->xfer_started && chan->qh))
			continue;

		list_for_each_entry(qtd, &chan->qh->qtd_list, qtd_list_entry) {
			if (!qtd->in_process)
				break;
			urb = qtd->urb;
			dev_dbg(hsotg->dev, "    URB Info:\n");
			dev_dbg(hsotg->dev, "      qtd: %p, urb: %p\n",
				qtd, urb);
			if (urb) {
				dev_dbg(hsotg->dev,
					"      Dev: %d, EP: %d %s\n",
					dwc2_hcd_get_dev_addr(&urb->pipe_info),
					dwc2_hcd_get_ep_num(&urb->pipe_info),
					dwc2_hcd_is_pipe_in(&urb->pipe_info) ?
					"IN" : "OUT");
				dev_dbg(hsotg->dev,
					"      Max packet size: %d\n",
					dwc2_hcd_get_mps(&urb->pipe_info));
				dev_dbg(hsotg->dev,
					"      transfer_buffer: %p\n",
					urb->buf);
				dev_dbg(hsotg->dev,
					"      transfer_dma: %08lx\n",
					(unsigned long)urb->dma);
				dev_dbg(hsotg->dev,
					"      transfer_buffer_length: %d\n",
					urb->length);
				dev_dbg(hsotg->dev, "      actual_length: %d\n",
					urb->actual_length);
			}
		}
	}

	dev_dbg(hsotg->dev, "  non_periodic_channels: %d\n",
		hsotg->non_periodic_channels);
	dev_dbg(hsotg->dev, "  periodic_channels: %d\n",
		hsotg->periodic_channels);
	dev_dbg(hsotg->dev, "  periodic_usecs: %d\n", hsotg->periodic_usecs);
	np_tx_status = readl(hsotg->regs + GNPTXSTS);
	dev_dbg(hsotg->dev, "  NP Tx Req Queue Space Avail: %d\n",
		np_tx_status >> TXSTS_QSPCAVAIL_SHIFT &
		TXSTS_QSPCAVAIL_MASK >> TXSTS_QSPCAVAIL_SHIFT);
	dev_dbg(hsotg->dev, "  NP Tx FIFO Space Avail: %d\n",
		np_tx_status >> TXSTS_FSPCAVAIL_SHIFT &
		TXSTS_FSPCAVAIL_MASK >> TXSTS_FSPCAVAIL_SHIFT);
	p_tx_status = readl(hsotg->regs + HPTXSTS);
	dev_dbg(hsotg->dev, "  P Tx Req Queue Space Avail: %d\n",
		p_tx_status >> TXSTS_QSPCAVAIL_SHIFT &
		TXSTS_QSPCAVAIL_MASK >> TXSTS_QSPCAVAIL_SHIFT);
	dev_dbg(hsotg->dev, "  P Tx FIFO Space Avail: %d\n",
		p_tx_status >> TXSTS_FSPCAVAIL_SHIFT &
		TXSTS_FSPCAVAIL_MASK >> TXSTS_FSPCAVAIL_SHIFT);
	dwc2_hcd_dump_frrem(hsotg);
	dwc2_dump_global_registers(hsotg);
	dwc2_dump_host_registers(hsotg);
	dev_dbg(hsotg->dev,
		"************************************************************\n");
	dev_dbg(hsotg->dev, "\n");
#endif
}

/*
 * NOTE: This function will be removed once the peripheral controller code
 * is integrated and the driver is stable
 */
void dwc2_hcd_dump_frrem(struct dwc2_hsotg *hsotg)
{
#ifdef DWC2_DUMP_FRREM
	dev_dbg(hsotg->dev, "Frame remaining at SOF:\n");
	dev_dbg(hsotg->dev, "  samples %u, accum %llu, avg %llu\n",
		hsotg->frrem_samples, hsotg->frrem_accum,
		hsotg->frrem_samples > 0 ?
		hsotg->frrem_accum / hsotg->frrem_samples : 0);
	dev_dbg(hsotg->dev, "\n");
	dev_dbg(hsotg->dev, "Frame remaining at start_transfer (uframe 7):\n");
	dev_dbg(hsotg->dev, "  samples %u, accum %llu, avg %llu\n",
		hsotg->hfnum_7_samples,
		hsotg->hfnum_7_frrem_accum,
		hsotg->hfnum_7_samples > 0 ?
		hsotg->hfnum_7_frrem_accum / hsotg->hfnum_7_samples : 0);
	dev_dbg(hsotg->dev, "Frame remaining at start_transfer (uframe 0):\n");
	dev_dbg(hsotg->dev, "  samples %u, accum %llu, avg %llu\n",
		hsotg->hfnum_0_samples,
		hsotg->hfnum_0_frrem_accum,
		hsotg->hfnum_0_samples > 0 ?
		hsotg->hfnum_0_frrem_accum / hsotg->hfnum_0_samples : 0);
	dev_dbg(hsotg->dev, "Frame remaining at start_transfer (uframe 1-6):\n");
	dev_dbg(hsotg->dev, "  samples %u, accum %llu, avg %llu\n",
		hsotg->hfnum_other_samples,
		hsotg->hfnum_other_frrem_accum,
		hsotg->hfnum_other_samples > 0 ?
		hsotg->hfnum_other_frrem_accum / hsotg->hfnum_other_samples :
		0);
	dev_dbg(hsotg->dev, "\n");
	dev_dbg(hsotg->dev, "Frame remaining at sample point A (uframe 7):\n");
	dev_dbg(hsotg->dev, "  samples %u, accum %llu, avg %llu\n",
		hsotg->hfnum_7_samples_a, hsotg->hfnum_7_frrem_accum_a,
		hsotg->hfnum_7_samples_a > 0 ?
		hsotg->hfnum_7_frrem_accum_a / hsotg->hfnum_7_samples_a : 0);
	dev_dbg(hsotg->dev, "Frame remaining at sample point A (uframe 0):\n");
	dev_dbg(hsotg->dev, "  samples %u, accum %llu, avg %llu\n",
		hsotg->hfnum_0_samples_a, hsotg->hfnum_0_frrem_accum_a,
		hsotg->hfnum_0_samples_a > 0 ?
		hsotg->hfnum_0_frrem_accum_a / hsotg->hfnum_0_samples_a : 0);
	dev_dbg(hsotg->dev, "Frame remaining at sample point A (uframe 1-6):\n");
	dev_dbg(hsotg->dev, "  samples %u, accum %llu, avg %llu\n",
		hsotg->hfnum_other_samples_a, hsotg->hfnum_other_frrem_accum_a,
		hsotg->hfnum_other_samples_a > 0 ?
		hsotg->hfnum_other_frrem_accum_a / hsotg->hfnum_other_samples_a
		: 0);
	dev_dbg(hsotg->dev, "\n");
	dev_dbg(hsotg->dev, "Frame remaining at sample point B (uframe 7):\n");
	dev_dbg(hsotg->dev, "  samples %u, accum %llu, avg %llu\n",
		hsotg->hfnum_7_samples_b, hsotg->hfnum_7_frrem_accum_b,
		hsotg->hfnum_7_samples_b > 0 ?
		hsotg->hfnum_7_frrem_accum_b / hsotg->hfnum_7_samples_b : 0);
	dev_dbg(hsotg->dev, "Frame remaining at sample point B (uframe 0):\n");
	dev_dbg(hsotg->dev, "  samples %u, accum %llu, avg %llu\n",
		hsotg->hfnum_0_samples_b, hsotg->hfnum_0_frrem_accum_b,
		(hsotg->hfnum_0_samples_b > 0) ?
		hsotg->hfnum_0_frrem_accum_b / hsotg->hfnum_0_samples_b : 0);
	dev_dbg(hsotg->dev, "Frame remaining at sample point B (uframe 1-6):\n");
	dev_dbg(hsotg->dev, "  samples %u, accum %llu, avg %llu\n",
		hsotg->hfnum_other_samples_b, hsotg->hfnum_other_frrem_accum_b,
		(hsotg->hfnum_other_samples_b > 0) ?
		hsotg->hfnum_other_frrem_accum_b / hsotg->hfnum_other_samples_b
		: 0);
#endif
}

struct wrapper_priv_data {
	struct dwc2_hsotg *hsotg;
};

/* Gets the dwc2_hsotg from a usb_hcd */
static struct dwc2_hsotg *dwc2_hcd_to_hsotg(struct usb_hcd *hcd)
{
	struct wrapper_priv_data *p;

	p = (struct wrapper_priv_data *) &hcd->hcd_priv;
	return p->hsotg;
}

static int _dwc2_hcd_start(struct usb_hcd *hcd);

void dwc2_host_start(struct dwc2_hsotg *hsotg)
{
	struct usb_hcd *hcd = dwc2_hsotg_to_hcd(hsotg);

	hcd->self.is_b_host = dwc2_hcd_is_b_host(hsotg);
	_dwc2_hcd_start(hcd);
}

void dwc2_host_disconnect(struct dwc2_hsotg *hsotg)
{
	struct usb_hcd *hcd = dwc2_hsotg_to_hcd(hsotg);

	hcd->self.is_b_host = 0;
}

void dwc2_host_hub_info(struct dwc2_hsotg *hsotg, void *context, int *hub_addr,
			int *hub_port)
{
	struct urb *urb = context;

	if (urb->dev->tt)
		*hub_addr = urb->dev->tt->hub->devnum;
	else
		*hub_addr = 0;
	*hub_port = urb->dev->ttport;
}

int dwc2_host_get_speed(struct dwc2_hsotg *hsotg, void *context)
{
	struct urb *urb = context;

	return urb->dev->speed;
}

static void dwc2_allocate_bus_bandwidth(struct usb_hcd *hcd, u16 bw,
					struct urb *urb)
{
	struct usb_bus *bus = hcd_to_bus(hcd);

	if (urb->interval)
		bus->bandwidth_allocated += bw / urb->interval;
	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS)
		bus->bandwidth_isoc_reqs++;
	else
		bus->bandwidth_int_reqs++;
}

static void dwc2_free_bus_bandwidth(struct usb_hcd *hcd, u16 bw,
				    struct urb *urb)
{
	struct usb_bus *bus = hcd_to_bus(hcd);

	if (urb->interval)
		bus->bandwidth_allocated -= bw / urb->interval;
	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS)
		bus->bandwidth_isoc_reqs--;
	else
		bus->bandwidth_int_reqs--;
}

/*
 * Sets the final status of an URB and returns it to the upper layer. Any
 * required cleanup of the URB is performed.
 *
 * Must be called with interrupt disabled and spinlock held
 */
void dwc2_host_complete(struct dwc2_hsotg *hsotg, void *context,
			struct dwc2_hcd_urb *dwc2_urb, int status)
{
	struct urb *urb = context;
	int i;

	if (!urb) {
		dev_dbg(hsotg->dev, "## %s: context is NULL ##\n", __func__);
		return;
	}

	if (!dwc2_urb) {
		dev_dbg(hsotg->dev, "## %s: dwc2_urb is NULL ##\n", __func__);
		return;
	}

	urb->actual_length = dwc2_hcd_urb_get_actual_length(dwc2_urb);

	if (dbg_urb(urb))
		dev_vdbg(hsotg->dev,
			 "%s: urb %p device %d ep %d-%s status %d actual %d\n",
			 __func__, urb, usb_pipedevice(urb->pipe),
			 usb_pipeendpoint(urb->pipe),
			 usb_pipein(urb->pipe) ? "IN" : "OUT", status,
			 urb->actual_length);

	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS && dbg_perio()) {
		for (i = 0; i < urb->number_of_packets; i++)
			dev_vdbg(hsotg->dev, " ISO Desc %d status %d\n",
				 i, urb->iso_frame_desc[i].status);
	}

	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
		urb->error_count = dwc2_hcd_urb_get_error_count(dwc2_urb);
		for (i = 0; i < urb->number_of_packets; ++i) {
			urb->iso_frame_desc[i].actual_length =
				dwc2_hcd_urb_get_iso_desc_actual_length(
						dwc2_urb, i);
			urb->iso_frame_desc[i].status =
				dwc2_hcd_urb_get_iso_desc_status(dwc2_urb, i);
		}
	}

	urb->status = status;
	urb->hcpriv = NULL;
	if (!status) {
		if ((urb->transfer_flags & URB_SHORT_NOT_OK) &&
		    urb->actual_length < urb->transfer_buffer_length)
			urb->status = -EREMOTEIO;
	}

	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS ||
	    usb_pipetype(urb->pipe) == PIPE_INTERRUPT) {
		struct usb_host_endpoint *ep = urb->ep;

		if (ep)
			dwc2_free_bus_bandwidth(dwc2_hsotg_to_hcd(hsotg),
					dwc2_hcd_get_ep_bandwidth(hsotg, ep),
					urb);
	}

	kfree(dwc2_urb);

	spin_unlock(&hsotg->lock);
	usb_hcd_giveback_urb(dwc2_hsotg_to_hcd(hsotg), urb, status);
	spin_lock(&hsotg->lock);
}

/*
 * Work queue function for starting the HCD when A-Cable is connected
 */
static void dwc2_hcd_start_func(struct work_struct *work)
{
	struct dwc2_hsotg *hsotg = container_of(work, struct dwc2_hsotg,
						start_work.work);

	dev_dbg(hsotg->dev, "%s() %p\n", __func__, hsotg);
	dwc2_host_start(hsotg);
}

/*
 * Reset work queue function
 */
static void dwc2_hcd_reset_func(struct work_struct *work)
{
	struct dwc2_hsotg *hsotg = container_of(work, struct dwc2_hsotg,
						reset_work.work);
	u32 hprt0;

	dev_dbg(hsotg->dev, "USB RESET function called\n");
	hprt0 = dwc2_read_hprt0(hsotg);
	hprt0 &= ~HPRT0_RST;
	writel(hprt0, hsotg->regs + HPRT0);
	hsotg->flags.b.port_reset_change = 1;
}

/*
 * =========================================================================
 *  Linux HC Driver Functions
 * =========================================================================
 */

/*
 * Initializes the DWC_otg controller and its root hub and prepares it for host
 * mode operation. Activates the root port. Returns 0 on success and a negative
 * error code on failure.
 */
static int _dwc2_hcd_start(struct usb_hcd *hcd)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	struct usb_bus *bus = hcd_to_bus(hcd);
	unsigned long flags;

	dev_dbg(hsotg->dev, "DWC OTG HCD START\n");

	spin_lock_irqsave(&hsotg->lock, flags);

	hcd->state = HC_STATE_RUNNING;

	if (dwc2_is_device_mode(hsotg)) {
		spin_unlock_irqrestore(&hsotg->lock, flags);
		return 0;	/* why 0 ?? */
	}

	dwc2_hcd_reinit(hsotg);

	/* Initialize and connect root hub if one is not already attached */
	if (bus->root_hub) {
		dev_dbg(hsotg->dev, "DWC OTG HCD Has Root Hub\n");
		/* Inform the HUB driver to resume */
		usb_hcd_resume_root_hub(hcd);
	}

	spin_unlock_irqrestore(&hsotg->lock, flags);
	return 0;
}

/*
 * Halts the DWC_otg host mode operations in a clean manner. USB transfers are
 * stopped.
 */
static void _dwc2_hcd_stop(struct usb_hcd *hcd)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	unsigned long flags;

	spin_lock_irqsave(&hsotg->lock, flags);
	dwc2_hcd_stop(hsotg);
	spin_unlock_irqrestore(&hsotg->lock, flags);

	usleep_range(1000, 3000);
}

/* Returns the current frame number */
static int _dwc2_hcd_get_frame_number(struct usb_hcd *hcd)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);

	return dwc2_hcd_get_frame_number(hsotg);
}

static void dwc2_dump_urb_info(struct usb_hcd *hcd, struct urb *urb,
			       char *fn_name)
{
#ifdef VERBOSE_DEBUG
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	char *pipetype;
	char *speed;

	dev_vdbg(hsotg->dev, "%s, urb %p\n", fn_name, urb);
	dev_vdbg(hsotg->dev, "  Device address: %d\n",
		 usb_pipedevice(urb->pipe));
	dev_vdbg(hsotg->dev, "  Endpoint: %d, %s\n",
		 usb_pipeendpoint(urb->pipe),
		 usb_pipein(urb->pipe) ? "IN" : "OUT");

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
		pipetype = "CONTROL";
		break;
	case PIPE_BULK:
		pipetype = "BULK";
		break;
	case PIPE_INTERRUPT:
		pipetype = "INTERRUPT";
		break;
	case PIPE_ISOCHRONOUS:
		pipetype = "ISOCHRONOUS";
		break;
	default:
		pipetype = "UNKNOWN";
		break;
	}

	dev_vdbg(hsotg->dev, "  Endpoint type: %s %s (%s)\n", pipetype,
		 usb_urb_dir_in(urb) ? "IN" : "OUT", usb_pipein(urb->pipe) ?
		 "IN" : "OUT");

	switch (urb->dev->speed) {
	case USB_SPEED_HIGH:
		speed = "HIGH";
		break;
	case USB_SPEED_FULL:
		speed = "FULL";
		break;
	case USB_SPEED_LOW:
		speed = "LOW";
		break;
	default:
		speed = "UNKNOWN";
		break;
	}

	dev_vdbg(hsotg->dev, "  Speed: %s\n", speed);
	dev_vdbg(hsotg->dev, "  Max packet size: %d\n",
		 usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe)));
	dev_vdbg(hsotg->dev, "  Data buffer length: %d\n",
		 urb->transfer_buffer_length);
	dev_vdbg(hsotg->dev, "  Transfer buffer: %p, Transfer DMA: %08lx\n",
		 urb->transfer_buffer, (unsigned long)urb->transfer_dma);
	dev_vdbg(hsotg->dev, "  Setup buffer: %p, Setup DMA: %08lx\n",
		 urb->setup_packet, (unsigned long)urb->setup_dma);
	dev_vdbg(hsotg->dev, "  Interval: %d\n", urb->interval);

	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
		int i;

		for (i = 0; i < urb->number_of_packets; i++) {
			dev_vdbg(hsotg->dev, "  ISO Desc %d:\n", i);
			dev_vdbg(hsotg->dev, "    offset: %d, length %d\n",
				 urb->iso_frame_desc[i].offset,
				 urb->iso_frame_desc[i].length);
		}
	}
#endif
}

/*
 * Starts processing a USB transfer request specified by a USB Request Block
 * (URB). mem_flags indicates the type of memory allocation to use while
 * processing this URB.
 */
static int _dwc2_hcd_urb_enqueue(struct usb_hcd *hcd, struct urb *urb,
				 gfp_t mem_flags)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	struct usb_host_endpoint *ep = urb->ep;
	struct dwc2_hcd_urb *dwc2_urb;
	int i;
	int alloc_bandwidth = 0;
	int retval = 0;
	u8 ep_type = 0;
	u32 tflags = 0;
	void *buf;
	unsigned long flags;

	if (dbg_urb(urb)) {
		dev_vdbg(hsotg->dev, "DWC OTG HCD URB Enqueue\n");
		dwc2_dump_urb_info(hcd, urb, "urb_enqueue");
	}

	if (ep == NULL)
		return -EINVAL;

	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS ||
	    usb_pipetype(urb->pipe) == PIPE_INTERRUPT) {
		spin_lock_irqsave(&hsotg->lock, flags);
		if (!dwc2_hcd_is_bandwidth_allocated(hsotg, ep))
			alloc_bandwidth = 1;
		spin_unlock_irqrestore(&hsotg->lock, flags);
	}

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
		ep_type = USB_ENDPOINT_XFER_CONTROL;
		break;
	case PIPE_ISOCHRONOUS:
		ep_type = USB_ENDPOINT_XFER_ISOC;
		break;
	case PIPE_BULK:
		ep_type = USB_ENDPOINT_XFER_BULK;
		break;
	case PIPE_INTERRUPT:
		ep_type = USB_ENDPOINT_XFER_INT;
		break;
	default:
		dev_warn(hsotg->dev, "Wrong ep type\n");
	}

	dwc2_urb = dwc2_hcd_urb_alloc(hsotg, urb->number_of_packets,
				      mem_flags);
	if (!dwc2_urb)
		return -ENOMEM;

	dwc2_hcd_urb_set_pipeinfo(hsotg, dwc2_urb, usb_pipedevice(urb->pipe),
				  usb_pipeendpoint(urb->pipe), ep_type,
				  usb_pipein(urb->pipe),
				  usb_maxpacket(urb->dev, urb->pipe,
						!(usb_pipein(urb->pipe))));

	buf = urb->transfer_buffer;
	if (hcd->self.uses_dma) {
		/*
		 * Calculate virtual address from physical address, because
		 * some class driver may not fill transfer_buffer.
		 * In Buffer DMA mode virtual address is used, when handling
		 * non-DWORD aligned buffers.
		 */
		buf = bus_to_virt(urb->transfer_dma);
	}

	if (!(urb->transfer_flags & URB_NO_INTERRUPT))
		tflags |= URB_GIVEBACK_ASAP;
	if (urb->transfer_flags & URB_ZERO_PACKET)
		tflags |= URB_SEND_ZERO_PACKET;

	dwc2_urb->priv = urb;
	dwc2_urb->buf = buf;
	dwc2_urb->dma = urb->transfer_dma;
	dwc2_urb->length = urb->transfer_buffer_length;
	dwc2_urb->setup_packet = urb->setup_packet;
	dwc2_urb->setup_dma = urb->setup_dma;
	dwc2_urb->flags = tflags;
	dwc2_urb->interval = urb->interval;
	dwc2_urb->status = -EINPROGRESS;

	for (i = 0; i < urb->number_of_packets; ++i)
		dwc2_hcd_urb_set_iso_desc_params(dwc2_urb, i,
						 urb->iso_frame_desc[i].offset,
						 urb->iso_frame_desc[i].length);

	urb->hcpriv = dwc2_urb;
	retval = dwc2_hcd_urb_enqueue(hsotg, dwc2_urb, &ep->hcpriv,
				      mem_flags);
	if (retval) {
		urb->hcpriv = NULL;
		kfree(dwc2_urb);
	} else {
		if (alloc_bandwidth) {
			spin_lock_irqsave(&hsotg->lock, flags);
			dwc2_allocate_bus_bandwidth(hcd,
					dwc2_hcd_get_ep_bandwidth(hsotg, ep),
					urb);
			spin_unlock_irqrestore(&hsotg->lock, flags);
		}
	}

	return retval;
}

/*
 * Aborts/cancels a USB transfer request. Always returns 0 to indicate success.
 */
static int _dwc2_hcd_urb_dequeue(struct usb_hcd *hcd, struct urb *urb,
				 int status)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	int rc = 0;
	unsigned long flags;

	dev_dbg(hsotg->dev, "DWC OTG HCD URB Dequeue\n");
	dwc2_dump_urb_info(hcd, urb, "urb_dequeue");

	spin_lock_irqsave(&hsotg->lock, flags);

	if (!urb->hcpriv) {
		dev_dbg(hsotg->dev, "## urb->hcpriv is NULL ##\n");
		goto out;
	}

	rc = dwc2_hcd_urb_dequeue(hsotg, urb->hcpriv);

	kfree(urb->hcpriv);
	urb->hcpriv = NULL;

	/* Higher layer software sets URB status */
	spin_unlock(&hsotg->lock);
	usb_hcd_giveback_urb(hcd, urb, status);
	spin_lock(&hsotg->lock);

	dev_dbg(hsotg->dev, "Called usb_hcd_giveback_urb()\n");
	dev_dbg(hsotg->dev, "  urb->status = %d\n", urb->status);
out:
	spin_unlock_irqrestore(&hsotg->lock, flags);

	return rc;
}

/*
 * Frees resources in the DWC_otg controller related to a given endpoint. Also
 * clears state in the HCD related to the endpoint. Any URBs for the endpoint
 * must already be dequeued.
 */
static void _dwc2_hcd_endpoint_disable(struct usb_hcd *hcd,
				       struct usb_host_endpoint *ep)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);

	dev_dbg(hsotg->dev,
		"DWC OTG HCD EP DISABLE: bEndpointAddress=0x%02x, ep->hcpriv=%p\n",
		ep->desc.bEndpointAddress, ep->hcpriv);
	dwc2_hcd_endpoint_disable(hsotg, ep, 250);
}

/*
 * Resets endpoint specific parameter values, in current version used to reset
 * the data toggle (as a WA). This function can be called from usb_clear_halt
 * routine.
 */
static void _dwc2_hcd_endpoint_reset(struct usb_hcd *hcd,
				     struct usb_host_endpoint *ep)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	int is_control = usb_endpoint_xfer_control(&ep->desc);
	int is_out = usb_endpoint_dir_out(&ep->desc);
	int epnum = usb_endpoint_num(&ep->desc);
	struct usb_device *udev;
	unsigned long flags;

	dev_dbg(hsotg->dev,
		"DWC OTG HCD EP RESET: bEndpointAddress=0x%02x\n",
		ep->desc.bEndpointAddress);

	udev = to_usb_device(hsotg->dev);

	spin_lock_irqsave(&hsotg->lock, flags);

	usb_settoggle(udev, epnum, is_out, 0);
	if (is_control)
		usb_settoggle(udev, epnum, !is_out, 0);
	dwc2_hcd_endpoint_reset(hsotg, ep);

	spin_unlock_irqrestore(&hsotg->lock, flags);
}

/*
 * Handles host mode interrupts for the DWC_otg controller. Returns IRQ_NONE if
 * there was no interrupt to handle. Returns IRQ_HANDLED if there was a valid
 * interrupt.
 *
 * This function is called by the USB core when an interrupt occurs
 */
static irqreturn_t _dwc2_hcd_irq(struct usb_hcd *hcd)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);

	return dwc2_handle_hcd_intr(hsotg);
}

/*
 * Creates Status Change bitmap for the root hub and root port. The bitmap is
 * returned in buf. Bit 0 is the status change indicator for the root hub. Bit 1
 * is the status change indicator for the single root port. Returns 1 if either
 * change indicator is 1, otherwise returns 0.
 */
static int _dwc2_hcd_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);

	buf[0] = dwc2_hcd_is_status_changed(hsotg, 1) << 1;
	return buf[0] != 0;
}

/* Handles hub class-specific requests */
static int _dwc2_hcd_hub_control(struct usb_hcd *hcd, u16 typereq, u16 wvalue,
				 u16 windex, char *buf, u16 wlength)
{
	int retval = dwc2_hcd_hub_control(dwc2_hcd_to_hsotg(hcd), typereq,
					  wvalue, windex, buf, wlength);
	return retval;
}

/* Handles hub TT buffer clear completions */
static void _dwc2_hcd_clear_tt_buffer_complete(struct usb_hcd *hcd,
					       struct usb_host_endpoint *ep)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	struct dwc2_qh *qh;
	unsigned long flags;

	qh = ep->hcpriv;
	if (!qh)
		return;

	spin_lock_irqsave(&hsotg->lock, flags);
	qh->tt_buffer_dirty = 0;

	if (hsotg->flags.b.port_connect_status)
		dwc2_hcd_queue_transactions(hsotg, DWC2_TRANSACTION_ALL);

	spin_unlock_irqrestore(&hsotg->lock, flags);
}

static struct hc_driver dwc2_hc_driver = {
	.description = "dwc2_hsotg",
	.product_desc = "DWC OTG Controller",
	.hcd_priv_size = sizeof(struct wrapper_priv_data),

	.irq = _dwc2_hcd_irq,
	.flags = HCD_MEMORY | HCD_USB2,

	.start = _dwc2_hcd_start,
	.stop = _dwc2_hcd_stop,
	.urb_enqueue = _dwc2_hcd_urb_enqueue,
	.urb_dequeue = _dwc2_hcd_urb_dequeue,
	.endpoint_disable = _dwc2_hcd_endpoint_disable,
	.endpoint_reset = _dwc2_hcd_endpoint_reset,
	.get_frame_number = _dwc2_hcd_get_frame_number,

	.hub_status_data = _dwc2_hcd_hub_status_data,
	.hub_control = _dwc2_hcd_hub_control,
	.clear_tt_buffer_complete = _dwc2_hcd_clear_tt_buffer_complete,
};

/*
 * Frees secondary storage associated with the dwc2_hsotg structure contained
 * in the struct usb_hcd field
 */
static void dwc2_hcd_free(struct dwc2_hsotg *hsotg)
{
	u32 ahbcfg;
	u32 dctl;
	int i;

	dev_dbg(hsotg->dev, "DWC OTG HCD FREE\n");

	/* Free memory for QH/QTD lists */
	dwc2_qh_list_free(hsotg, &hsotg->non_periodic_sched_inactive);
	dwc2_qh_list_free(hsotg, &hsotg->non_periodic_sched_active);
	dwc2_qh_list_free(hsotg, &hsotg->periodic_sched_inactive);
	dwc2_qh_list_free(hsotg, &hsotg->periodic_sched_ready);
	dwc2_qh_list_free(hsotg, &hsotg->periodic_sched_assigned);
	dwc2_qh_list_free(hsotg, &hsotg->periodic_sched_queued);

	/* Free memory for the host channels */
	for (i = 0; i < MAX_EPS_CHANNELS; i++) {
		struct dwc2_host_chan *chan = hsotg->hc_ptr_array[i];

		if (chan != NULL) {
			dev_dbg(hsotg->dev, "HCD Free channel #%i, chan=%p\n",
				i, chan);
			hsotg->hc_ptr_array[i] = NULL;
			kfree(chan);
		}
	}

	if (hsotg->core_params->dma_enable > 0) {
		if (hsotg->status_buf) {
			dma_free_coherent(hsotg->dev, DWC2_HCD_STATUS_BUF_SIZE,
					  hsotg->status_buf,
					  hsotg->status_buf_dma);
			hsotg->status_buf = NULL;
		}
	} else {
		kfree(hsotg->status_buf);
		hsotg->status_buf = NULL;
	}

	ahbcfg = readl(hsotg->regs + GAHBCFG);

	/* Disable all interrupts */
	ahbcfg &= ~GAHBCFG_GLBL_INTR_EN;
	writel(ahbcfg, hsotg->regs + GAHBCFG);
	writel(0, hsotg->regs + GINTMSK);

	if (hsotg->snpsid >= DWC2_CORE_REV_3_00a) {
		dctl = readl(hsotg->regs + DCTL);
		dctl |= DCTL_SFTDISCON;
		writel(dctl, hsotg->regs + DCTL);
	}

	if (hsotg->wq_otg) {
		if (!cancel_work_sync(&hsotg->wf_otg))
			flush_workqueue(hsotg->wq_otg);
		destroy_workqueue(hsotg->wq_otg);
	}

	kfree(hsotg->core_params);
	hsotg->core_params = NULL;
	del_timer(&hsotg->wkp_timer);
}

static void dwc2_hcd_release(struct dwc2_hsotg *hsotg)
{
	/* Turn off all host-specific interrupts */
	dwc2_disable_host_interrupts(hsotg);

	dwc2_hcd_free(hsotg);
}

/*
 * Sets all parameters to the given value.
 *
 * Assumes that the dwc2_core_params struct contains only integers.
 */
void dwc2_set_all_params(struct dwc2_core_params *params, int value)
{
	int *p = (int *)params;
	size_t size = sizeof(*params) / sizeof(*p);
	int i;

	for (i = 0; i < size; i++)
		p[i] = -1;
}
EXPORT_SYMBOL_GPL(dwc2_set_all_params);

/*
 * Initializes the HCD. This function allocates memory for and initializes the
 * static parts of the usb_hcd and dwc2_hsotg structures. It also registers the
 * USB bus with the core and calls the hc_driver->start() function. It returns
 * a negative error on failure.
 */
int dwc2_hcd_init(struct dwc2_hsotg *hsotg, int irq,
		  const struct dwc2_core_params *params)
{
	struct usb_hcd *hcd;
	struct dwc2_host_chan *channel;
	u32 snpsid, gusbcfg, hcfg;
	int i, num_channels;
	int retval = -ENOMEM;

	dev_dbg(hsotg->dev, "DWC OTG HCD INIT\n");

	/*
	 * Attempt to ensure this device is really a DWC_otg Controller.
	 * Read and verify the GSNPSID register contents. The value should be
	 * 0x45f42xxx or 0x45f43xxx, which corresponds to either "OT2" or "OT3",
	 * as in "OTG version 2.xx" or "OTG version 3.xx".
	 */
	snpsid = readl(hsotg->regs + GSNPSID);
	if ((snpsid & 0xfffff000) != 0x4f542000 &&
	    (snpsid & 0xfffff000) != 0x4f543000) {
		dev_err(hsotg->dev, "Bad value for GSNPSID: 0x%08x\n", snpsid);
		retval = -ENODEV;
		goto error1;
	}

	/*
	 * Store the contents of the hardware configuration registers here for
	 * easy access later
	 */
	hsotg->hwcfg1 = readl(hsotg->regs + GHWCFG1);
	hsotg->hwcfg2 = readl(hsotg->regs + GHWCFG2);
	hsotg->hwcfg3 = readl(hsotg->regs + GHWCFG3);
	hsotg->hwcfg4 = readl(hsotg->regs + GHWCFG4);

	dev_dbg(hsotg->dev, "hwcfg1=%08x\n", hsotg->hwcfg1);
	dev_dbg(hsotg->dev, "hwcfg2=%08x\n", hsotg->hwcfg2);
	dev_dbg(hsotg->dev, "hwcfg3=%08x\n", hsotg->hwcfg3);
	dev_dbg(hsotg->dev, "hwcfg4=%08x\n", hsotg->hwcfg4);

	/* Force host mode to get HPTXFSIZ exact power on value */
	gusbcfg = readl(hsotg->regs + GUSBCFG);
	gusbcfg |= GUSBCFG_FORCEHOSTMODE;
	writel(gusbcfg, hsotg->regs + GUSBCFG);
	usleep_range(100000, 150000);

	hsotg->hptxfsiz = readl(hsotg->regs + HPTXFSIZ);
	dev_dbg(hsotg->dev, "hptxfsiz=%08x\n", hsotg->hptxfsiz);
	gusbcfg = readl(hsotg->regs + GUSBCFG);
	gusbcfg &= ~GUSBCFG_FORCEHOSTMODE;
	writel(gusbcfg, hsotg->regs + GUSBCFG);
	usleep_range(100000, 150000);

	hcfg = readl(hsotg->regs + HCFG);
	dev_dbg(hsotg->dev, "hcfg=%08x\n", hcfg);
	dev_dbg(hsotg->dev, "op_mode=%0x\n",
		hsotg->hwcfg2 >> GHWCFG2_OP_MODE_SHIFT &
		GHWCFG2_OP_MODE_MASK >> GHWCFG2_OP_MODE_SHIFT);
	dev_dbg(hsotg->dev, "arch=%0x\n",
		hsotg->hwcfg2 >> GHWCFG2_ARCHITECTURE_SHIFT &
		GHWCFG2_ARCHITECTURE_MASK >> GHWCFG2_ARCHITECTURE_SHIFT);
	dev_dbg(hsotg->dev, "num_dev_ep=%d\n",
		hsotg->hwcfg2 >> GHWCFG2_NUM_DEV_EP_SHIFT &
		GHWCFG2_NUM_DEV_EP_MASK >> GHWCFG2_NUM_DEV_EP_SHIFT);
	dev_dbg(hsotg->dev, "max_host_chan=%d\n",
		hsotg->hwcfg2 >> GHWCFG2_NUM_HOST_CHAN_SHIFT &
		GHWCFG2_NUM_HOST_CHAN_MASK >> GHWCFG2_NUM_HOST_CHAN_SHIFT);
	dev_dbg(hsotg->dev, "nonperio_tx_q_depth=0x%0x\n",
		hsotg->hwcfg2 >> GHWCFG2_NONPERIO_TX_Q_DEPTH_SHIFT &
		GHWCFG2_NONPERIO_TX_Q_DEPTH_MASK >>
				GHWCFG2_NONPERIO_TX_Q_DEPTH_SHIFT);
	dev_dbg(hsotg->dev, "host_perio_tx_q_depth=0x%0x\n",
		hsotg->hwcfg2 >> GHWCFG2_HOST_PERIO_TX_Q_DEPTH_SHIFT &
		GHWCFG2_HOST_PERIO_TX_Q_DEPTH_MASK >>
				GHWCFG2_HOST_PERIO_TX_Q_DEPTH_SHIFT);
	dev_dbg(hsotg->dev, "dev_token_q_depth=0x%0x\n",
		hsotg->hwcfg2 >> GHWCFG2_DEV_TOKEN_Q_DEPTH_SHIFT &
		GHWCFG3_XFER_SIZE_CNTR_WIDTH_MASK >>
				GHWCFG3_XFER_SIZE_CNTR_WIDTH_SHIFT);

#ifdef CONFIG_USB_DWC2_TRACK_MISSED_SOFS
	hsotg->frame_num_array = kzalloc(sizeof(*hsotg->frame_num_array) *
					 FRAME_NUM_ARRAY_SIZE, GFP_KERNEL);
	if (!hsotg->frame_num_array)
		goto error1;
	hsotg->last_frame_num_array = kzalloc(
			sizeof(*hsotg->last_frame_num_array) *
			FRAME_NUM_ARRAY_SIZE, GFP_KERNEL);
	if (!hsotg->last_frame_num_array)
		goto error1;
	hsotg->last_frame_num = HFNUM_MAX_FRNUM;
#endif

	hsotg->core_params = kzalloc(sizeof(*hsotg->core_params), GFP_KERNEL);
	if (!hsotg->core_params)
		goto error1;

	dwc2_set_all_params(hsotg->core_params, -1);

	/* Validate parameter values */
	dwc2_set_parameters(hsotg, params);

	/* Set device flags indicating whether the HCD supports DMA */
	if (hsotg->core_params->dma_enable > 0) {
		if (dma_set_mask(hsotg->dev, DMA_BIT_MASK(32)) < 0)
			dev_warn(hsotg->dev, "can't set DMA mask\n");
		if (dma_set_coherent_mask(hsotg->dev, DMA_BIT_MASK(31)) < 0)
			dev_warn(hsotg->dev,
				 "can't enable workaround for >2GB RAM\n");
	} else {
		dma_set_mask(hsotg->dev, 0);
		dma_set_coherent_mask(hsotg->dev, 0);
	}

	hcd = usb_create_hcd(&dwc2_hc_driver, hsotg->dev, dev_name(hsotg->dev));
	if (!hcd)
		goto error1;

	hcd->has_tt = 1;

	spin_lock_init(&hsotg->lock);
	((struct wrapper_priv_data *) &hcd->hcd_priv)->hsotg = hsotg;
	hsotg->priv = hcd;

	/*
	 * Disable the global interrupt until all the interrupt handlers are
	 * installed
	 */
	dwc2_disable_global_interrupts(hsotg);

	/* Initialize the DWC_otg core, and select the Phy type */
	retval = dwc2_core_init(hsotg, true, irq);
	if (retval)
		goto error2;

	/* Create new workqueue and init work */
	retval = -ENOMEM;
	hsotg->wq_otg = create_singlethread_workqueue("dwc2");
	if (!hsotg->wq_otg) {
		dev_err(hsotg->dev, "Failed to create workqueue\n");
		goto error2;
	}
	INIT_WORK(&hsotg->wf_otg, dwc2_conn_id_status_change);

	hsotg->snpsid = readl(hsotg->regs + GSNPSID);
	dev_dbg(hsotg->dev, "Core Release: %1x.%1x%1x%1x\n",
		hsotg->snpsid >> 12 & 0xf, hsotg->snpsid >> 8 & 0xf,
		hsotg->snpsid >> 4 & 0xf, hsotg->snpsid & 0xf);

	setup_timer(&hsotg->wkp_timer, dwc2_wakeup_detected,
		    (unsigned long)hsotg);

	/* Initialize the non-periodic schedule */
	INIT_LIST_HEAD(&hsotg->non_periodic_sched_inactive);
	INIT_LIST_HEAD(&hsotg->non_periodic_sched_active);

	/* Initialize the periodic schedule */
	INIT_LIST_HEAD(&hsotg->periodic_sched_inactive);
	INIT_LIST_HEAD(&hsotg->periodic_sched_ready);
	INIT_LIST_HEAD(&hsotg->periodic_sched_assigned);
	INIT_LIST_HEAD(&hsotg->periodic_sched_queued);

	/*
	 * Create a host channel descriptor for each host channel implemented
	 * in the controller. Initialize the channel descriptor array.
	 */
	INIT_LIST_HEAD(&hsotg->free_hc_list);
	num_channels = hsotg->core_params->host_channels;
	memset(&hsotg->hc_ptr_array[0], 0, sizeof(hsotg->hc_ptr_array));

	for (i = 0; i < num_channels; i++) {
		channel = kzalloc(sizeof(*channel), GFP_KERNEL);
		if (channel == NULL)
			goto error3;
		channel->hc_num = i;
		hsotg->hc_ptr_array[i] = channel;
	}

	/* Initialize hsotg start work */
	INIT_DELAYED_WORK(&hsotg->start_work, dwc2_hcd_start_func);

	/* Initialize port reset work */
	INIT_DELAYED_WORK(&hsotg->reset_work, dwc2_hcd_reset_func);

	/*
	 * Allocate space for storing data on status transactions. Normally no
	 * data is sent, but this space acts as a bit bucket. This must be
	 * done after usb_add_hcd since that function allocates the DMA buffer
	 * pool.
	 */
	if (hsotg->core_params->dma_enable > 0)
		hsotg->status_buf = dma_alloc_coherent(hsotg->dev,
					DWC2_HCD_STATUS_BUF_SIZE,
					&hsotg->status_buf_dma, GFP_KERNEL);
	else
		hsotg->status_buf = kzalloc(DWC2_HCD_STATUS_BUF_SIZE,
					  GFP_KERNEL);

	if (!hsotg->status_buf)
		goto error3;

	hsotg->otg_port = 1;
	hsotg->frame_list = NULL;
	hsotg->frame_list_dma = 0;
	hsotg->periodic_qh_count = 0;

	/* Initiate lx_state to L3 disconnected state */
	hsotg->lx_state = DWC2_L3;

	hcd->self.otg_port = hsotg->otg_port;

	/* Don't support SG list at this point */
	hcd->self.sg_tablesize = 0;

	/*
	 * Finish generic HCD initialization and start the HCD. This function
	 * allocates the DMA buffer pool, registers the USB bus, requests the
	 * IRQ line, and calls hcd_start method.
	 */
	retval = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (retval < 0)
		goto error3;

	dwc2_dump_global_registers(hsotg);
	dwc2_dump_host_registers(hsotg);
	dwc2_hcd_dump_state(hsotg);

	dwc2_enable_global_interrupts(hsotg);

	return 0;

error3:
	dwc2_hcd_release(hsotg);
error2:
	usb_put_hcd(hcd);
error1:
	kfree(hsotg->core_params);

#ifdef CONFIG_USB_DWC2_TRACK_MISSED_SOFS
	kfree(hsotg->last_frame_num_array);
	kfree(hsotg->frame_num_array);
#endif

	dev_err(hsotg->dev, "%s() FAILED, returning %d\n", __func__, retval);
	return retval;
}
EXPORT_SYMBOL_GPL(dwc2_hcd_init);

/*
 * Removes the HCD.
 * Frees memory and resources associated with the HCD and deregisters the bus.
 */
void dwc2_hcd_remove(struct dwc2_hsotg *hsotg)
{
	struct usb_hcd *hcd;

	dev_dbg(hsotg->dev, "DWC OTG HCD REMOVE\n");

	hcd = dwc2_hsotg_to_hcd(hsotg);
	dev_dbg(hsotg->dev, "hsotg->hcd = %p\n", hcd);

	if (!hcd) {
		dev_dbg(hsotg->dev, "%s: dwc2_hsotg_to_hcd(hsotg) NULL!\n",
			__func__);
		return;
	}

	usb_remove_hcd(hcd);
	hsotg->priv = NULL;
	dwc2_hcd_release(hsotg);
	usb_put_hcd(hcd);

#ifdef CONFIG_USB_DWC2_TRACK_MISSED_SOFS
	kfree(hsotg->last_frame_num_array);
	kfree(hsotg->frame_num_array);
#endif
}
EXPORT_SYMBOL_GPL(dwc2_hcd_remove);
