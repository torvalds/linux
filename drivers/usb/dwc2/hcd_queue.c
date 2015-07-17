/*
 * hcd_queue.c - DesignWare HS OTG Controller host queuing routines
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
 * This file contains the functions to manage Queue Heads and Queue
 * Transfer Descriptors for Host mode
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include <linux/usb/hcd.h>
#include <linux/usb/ch11.h>

#include "core.h"
#include "hcd.h"

/**
 * dwc2_qh_init() - Initializes a QH structure
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:    The QH to init
 * @urb:   Holds the information about the device/endpoint needed to initialize
 *         the QH
 */
#define SCHEDULE_SLOP 10
static void dwc2_qh_init(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh,
			 struct dwc2_hcd_urb *urb)
{
	int dev_speed, hub_addr, hub_port;
	char *speed, *type;

	dev_vdbg(hsotg->dev, "%s()\n", __func__);

	/* Initialize QH */
	qh->ep_type = dwc2_hcd_get_pipe_type(&urb->pipe_info);
	qh->ep_is_in = dwc2_hcd_is_pipe_in(&urb->pipe_info) ? 1 : 0;

	qh->data_toggle = DWC2_HC_PID_DATA0;
	qh->maxp = dwc2_hcd_get_mps(&urb->pipe_info);
	INIT_LIST_HEAD(&qh->qtd_list);
	INIT_LIST_HEAD(&qh->qh_list_entry);

	/* FS/LS Endpoint on HS Hub, NOT virtual root hub */
	dev_speed = dwc2_host_get_speed(hsotg, urb->priv);

	dwc2_host_hub_info(hsotg, urb->priv, &hub_addr, &hub_port);

	if ((dev_speed == USB_SPEED_LOW || dev_speed == USB_SPEED_FULL) &&
	    hub_addr != 0 && hub_addr != 1) {
		dev_vdbg(hsotg->dev,
			 "QH init: EP %d: TT found at hub addr %d, for port %d\n",
			 dwc2_hcd_get_ep_num(&urb->pipe_info), hub_addr,
			 hub_port);
		qh->do_split = 1;
	}

	if (qh->ep_type == USB_ENDPOINT_XFER_INT ||
	    qh->ep_type == USB_ENDPOINT_XFER_ISOC) {
		/* Compute scheduling parameters once and save them */
		u32 hprt, prtspd;

		/* Todo: Account for split transfers in the bus time */
		int bytecount =
			dwc2_hb_mult(qh->maxp) * dwc2_max_packet(qh->maxp);

		qh->usecs = NS_TO_US(usb_calc_bus_time(qh->do_split ?
				USB_SPEED_HIGH : dev_speed, qh->ep_is_in,
				qh->ep_type == USB_ENDPOINT_XFER_ISOC,
				bytecount));
		/* Start in a slightly future (micro)frame */
		qh->sched_frame = dwc2_frame_num_inc(hsotg->frame_number,
						     SCHEDULE_SLOP);
		qh->interval = urb->interval;
#if 0
		/* Increase interrupt polling rate for debugging */
		if (qh->ep_type == USB_ENDPOINT_XFER_INT)
			qh->interval = 8;
#endif
		hprt = readl(hsotg->regs + HPRT0);
		prtspd = (hprt & HPRT0_SPD_MASK) >> HPRT0_SPD_SHIFT;
		if (prtspd == HPRT0_SPD_HIGH_SPEED &&
		    (dev_speed == USB_SPEED_LOW ||
		     dev_speed == USB_SPEED_FULL)) {
			qh->interval *= 8;
			qh->sched_frame |= 0x7;
			qh->start_split_frame = qh->sched_frame;
		}
		dev_dbg(hsotg->dev, "interval=%d\n", qh->interval);
	}

	dev_vdbg(hsotg->dev, "DWC OTG HCD QH Initialized\n");
	dev_vdbg(hsotg->dev, "DWC OTG HCD QH - qh = %p\n", qh);
	dev_vdbg(hsotg->dev, "DWC OTG HCD QH - Device Address = %d\n",
		 dwc2_hcd_get_dev_addr(&urb->pipe_info));
	dev_vdbg(hsotg->dev, "DWC OTG HCD QH - Endpoint %d, %s\n",
		 dwc2_hcd_get_ep_num(&urb->pipe_info),
		 dwc2_hcd_is_pipe_in(&urb->pipe_info) ? "IN" : "OUT");

	qh->dev_speed = dev_speed;

	switch (dev_speed) {
	case USB_SPEED_LOW:
		speed = "low";
		break;
	case USB_SPEED_FULL:
		speed = "full";
		break;
	case USB_SPEED_HIGH:
		speed = "high";
		break;
	default:
		speed = "?";
		break;
	}
	dev_vdbg(hsotg->dev, "DWC OTG HCD QH - Speed = %s\n", speed);

	switch (qh->ep_type) {
	case USB_ENDPOINT_XFER_ISOC:
		type = "isochronous";
		break;
	case USB_ENDPOINT_XFER_INT:
		type = "interrupt";
		break;
	case USB_ENDPOINT_XFER_CONTROL:
		type = "control";
		break;
	case USB_ENDPOINT_XFER_BULK:
		type = "bulk";
		break;
	default:
		type = "?";
		break;
	}

	dev_vdbg(hsotg->dev, "DWC OTG HCD QH - Type = %s\n", type);

	if (qh->ep_type == USB_ENDPOINT_XFER_INT) {
		dev_vdbg(hsotg->dev, "DWC OTG HCD QH - usecs = %d\n",
			 qh->usecs);
		dev_vdbg(hsotg->dev, "DWC OTG HCD QH - interval = %d\n",
			 qh->interval);
	}
}

/**
 * dwc2_hcd_qh_create() - Allocates and initializes a QH
 *
 * @hsotg:        The HCD state structure for the DWC OTG controller
 * @urb:          Holds the information about the device/endpoint needed
 *                to initialize the QH
 * @atomic_alloc: Flag to do atomic allocation if needed
 *
 * Return: Pointer to the newly allocated QH, or NULL on error
 */
struct dwc2_qh *dwc2_hcd_qh_create(struct dwc2_hsotg *hsotg,
					  struct dwc2_hcd_urb *urb,
					  gfp_t mem_flags)
{
	struct dwc2_qh *qh;

	if (!urb->priv)
		return NULL;

	/* Allocate memory */
	qh = kzalloc(sizeof(*qh), mem_flags);
	if (!qh)
		return NULL;

	dwc2_qh_init(hsotg, qh, urb);

	if (hsotg->core_params->dma_desc_enable > 0 &&
	    dwc2_hcd_qh_init_ddma(hsotg, qh, mem_flags) < 0) {
		dwc2_hcd_qh_free(hsotg, qh);
		return NULL;
	}

	return qh;
}

/**
 * dwc2_hcd_qh_free() - Frees the QH
 *
 * @hsotg: HCD instance
 * @qh:    The QH to free
 *
 * QH should already be removed from the list. QTD list should already be empty
 * if called from URB Dequeue.
 *
 * Must NOT be called with interrupt disabled or spinlock held
 */
void dwc2_hcd_qh_free(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	if (hsotg->core_params->dma_desc_enable > 0) {
		dwc2_hcd_qh_free_ddma(hsotg, qh);
	} else {
		/* kfree(NULL) is safe */
		kfree(qh->dw_align_buf);
		qh->dw_align_buf_dma = (dma_addr_t)0;
	}
	kfree(qh);
}

/**
 * dwc2_periodic_channel_available() - Checks that a channel is available for a
 * periodic transfer
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 *
 * Return: 0 if successful, negative error code otherwise
 */
static int dwc2_periodic_channel_available(struct dwc2_hsotg *hsotg)
{
	/*
	 * Currently assuming that there is a dedicated host channel for
	 * each periodic transaction plus at least one host channel for
	 * non-periodic transactions
	 */
	int status;
	int num_channels;

	num_channels = hsotg->core_params->host_channels;
	if (hsotg->periodic_channels + hsotg->non_periodic_channels <
								num_channels
	    && hsotg->periodic_channels < num_channels - 1) {
		status = 0;
	} else {
		dev_dbg(hsotg->dev,
			"%s: Total channels: %d, Periodic: %d, "
			"Non-periodic: %d\n", __func__, num_channels,
			hsotg->periodic_channels, hsotg->non_periodic_channels);
		status = -ENOSPC;
	}

	return status;
}

/**
 * dwc2_check_periodic_bandwidth() - Checks that there is sufficient bandwidth
 * for the specified QH in the periodic schedule
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:    QH containing periodic bandwidth required
 *
 * Return: 0 if successful, negative error code otherwise
 *
 * For simplicity, this calculation assumes that all the transfers in the
 * periodic schedule may occur in the same (micro)frame
 */
static int dwc2_check_periodic_bandwidth(struct dwc2_hsotg *hsotg,
					 struct dwc2_qh *qh)
{
	int status;
	s16 max_claimed_usecs;

	status = 0;

	if (qh->dev_speed == USB_SPEED_HIGH || qh->do_split) {
		/*
		 * High speed mode
		 * Max periodic usecs is 80% x 125 usec = 100 usec
		 */
		max_claimed_usecs = 100 - qh->usecs;
	} else {
		/*
		 * Full speed mode
		 * Max periodic usecs is 90% x 1000 usec = 900 usec
		 */
		max_claimed_usecs = 900 - qh->usecs;
	}

	if (hsotg->periodic_usecs > max_claimed_usecs) {
		dev_err(hsotg->dev,
			"%s: already claimed usecs %d, required usecs %d\n",
			__func__, hsotg->periodic_usecs, qh->usecs);
		status = -ENOSPC;
	}

	return status;
}

/**
 * Microframe scheduler
 * track the total use in hsotg->frame_usecs
 * keep each qh use in qh->frame_usecs
 * when surrendering the qh then donate the time back
 */
static const unsigned short max_uframe_usecs[] = {
	100, 100, 100, 100, 100, 100, 30, 0
};

void dwc2_hcd_init_usecs(struct dwc2_hsotg *hsotg)
{
	int i;

	for (i = 0; i < 8; i++)
		hsotg->frame_usecs[i] = max_uframe_usecs[i];
}

static int dwc2_find_single_uframe(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	unsigned short utime = qh->usecs;
	int i;

	for (i = 0; i < 8; i++) {
		/* At the start hsotg->frame_usecs[i] = max_uframe_usecs[i] */
		if (utime <= hsotg->frame_usecs[i]) {
			hsotg->frame_usecs[i] -= utime;
			qh->frame_usecs[i] += utime;
			return i;
		}
	}
	return -ENOSPC;
}

/*
 * use this for FS apps that can span multiple uframes
 */
static int dwc2_find_multi_uframe(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	unsigned short utime = qh->usecs;
	unsigned short xtime;
	int t_left;
	int i;
	int j;
	int k;

	for (i = 0; i < 8; i++) {
		if (hsotg->frame_usecs[i] <= 0)
			continue;

		/*
		 * we need n consecutive slots so use j as a start slot
		 * j plus j+1 must be enough time (for now)
		 */
		xtime = hsotg->frame_usecs[i];
		for (j = i + 1; j < 8; j++) {
			/*
			 * if we add this frame remaining time to xtime we may
			 * be OK, if not we need to test j for a complete frame
			 */
			if (xtime + hsotg->frame_usecs[j] < utime) {
				if (hsotg->frame_usecs[j] <
							max_uframe_usecs[j])
					continue;
			}
			if (xtime >= utime) {
				t_left = utime;
				for (k = i; k < 8; k++) {
					t_left -= hsotg->frame_usecs[k];
					if (t_left <= 0) {
						qh->frame_usecs[k] +=
							hsotg->frame_usecs[k]
								+ t_left;
						hsotg->frame_usecs[k] = -t_left;
						return i;
					} else {
						qh->frame_usecs[k] +=
							hsotg->frame_usecs[k];
						hsotg->frame_usecs[k] = 0;
					}
				}
			}
			/* add the frame time to x time */
			xtime += hsotg->frame_usecs[j];
			/* we must have a fully available next frame or break */
			if (xtime < utime &&
			   hsotg->frame_usecs[j] == max_uframe_usecs[j])
				continue;
		}
	}
	return -ENOSPC;
}

static int dwc2_find_uframe(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	int ret;

	if (qh->dev_speed == USB_SPEED_HIGH) {
		/* if this is a hs transaction we need a full frame */
		ret = dwc2_find_single_uframe(hsotg, qh);
	} else {
		/*
		 * if this is a fs transaction we may need a sequence
		 * of frames
		 */
		ret = dwc2_find_multi_uframe(hsotg, qh);
	}
	return ret;
}

/**
 * dwc2_check_max_xfer_size() - Checks that the max transfer size allowed in a
 * host channel is large enough to handle the maximum data transfer in a single
 * (micro)frame for a periodic transfer
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:    QH for a periodic endpoint
 *
 * Return: 0 if successful, negative error code otherwise
 */
static int dwc2_check_max_xfer_size(struct dwc2_hsotg *hsotg,
				    struct dwc2_qh *qh)
{
	u32 max_xfer_size;
	u32 max_channel_xfer_size;
	int status = 0;

	max_xfer_size = dwc2_max_packet(qh->maxp) * dwc2_hb_mult(qh->maxp);
	max_channel_xfer_size = hsotg->core_params->max_transfer_size;

	if (max_xfer_size > max_channel_xfer_size) {
		dev_err(hsotg->dev,
			"%s: Periodic xfer length %d > max xfer length for channel %d\n",
			__func__, max_xfer_size, max_channel_xfer_size);
		status = -ENOSPC;
	}

	return status;
}

/**
 * dwc2_schedule_periodic() - Schedules an interrupt or isochronous transfer in
 * the periodic schedule
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:    QH for the periodic transfer. The QH should already contain the
 *         scheduling information.
 *
 * Return: 0 if successful, negative error code otherwise
 */
static int dwc2_schedule_periodic(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	int status;

	if (hsotg->core_params->uframe_sched > 0) {
		int frame = -1;

		status = dwc2_find_uframe(hsotg, qh);
		if (status == 0)
			frame = 7;
		else if (status > 0)
			frame = status - 1;

		/* Set the new frame up */
		if (frame >= 0) {
			qh->sched_frame &= ~0x7;
			qh->sched_frame |= (frame & 7);
		}

		if (status > 0)
			status = 0;
	} else {
		status = dwc2_periodic_channel_available(hsotg);
		if (status) {
			dev_info(hsotg->dev,
				 "%s: No host channel available for periodic transfer\n",
				 __func__);
			return status;
		}

		status = dwc2_check_periodic_bandwidth(hsotg, qh);
	}

	if (status) {
		dev_dbg(hsotg->dev,
			"%s: Insufficient periodic bandwidth for periodic transfer\n",
			__func__);
		return status;
	}

	status = dwc2_check_max_xfer_size(hsotg, qh);
	if (status) {
		dev_dbg(hsotg->dev,
			"%s: Channel max transfer size too small for periodic transfer\n",
			__func__);
		return status;
	}

	if (hsotg->core_params->dma_desc_enable > 0)
		/* Don't rely on SOF and start in ready schedule */
		list_add_tail(&qh->qh_list_entry, &hsotg->periodic_sched_ready);
	else
		/* Always start in inactive schedule */
		list_add_tail(&qh->qh_list_entry,
			      &hsotg->periodic_sched_inactive);

	if (hsotg->core_params->uframe_sched <= 0)
		/* Reserve periodic channel */
		hsotg->periodic_channels++;

	/* Update claimed usecs per (micro)frame */
	hsotg->periodic_usecs += qh->usecs;

	return status;
}

/**
 * dwc2_deschedule_periodic() - Removes an interrupt or isochronous transfer
 * from the periodic schedule
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:	   QH for the periodic transfer
 */
static void dwc2_deschedule_periodic(struct dwc2_hsotg *hsotg,
				     struct dwc2_qh *qh)
{
	int i;

	list_del_init(&qh->qh_list_entry);

	/* Update claimed usecs per (micro)frame */
	hsotg->periodic_usecs -= qh->usecs;

	if (hsotg->core_params->uframe_sched > 0) {
		for (i = 0; i < 8; i++) {
			hsotg->frame_usecs[i] += qh->frame_usecs[i];
			qh->frame_usecs[i] = 0;
		}
	} else {
		/* Release periodic channel reservation */
		hsotg->periodic_channels--;
	}
}

/**
 * dwc2_hcd_qh_add() - Adds a QH to either the non periodic or periodic
 * schedule if it is not already in the schedule. If the QH is already in
 * the schedule, no action is taken.
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:    The QH to add
 *
 * Return: 0 if successful, negative error code otherwise
 */
int dwc2_hcd_qh_add(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	int status;
	u32 intr_mask;

	if (dbg_qh(qh))
		dev_vdbg(hsotg->dev, "%s()\n", __func__);

	if (!list_empty(&qh->qh_list_entry))
		/* QH already in a schedule */
		return 0;

	/* Add the new QH to the appropriate schedule */
	if (dwc2_qh_is_non_per(qh)) {
		/* Always start in inactive schedule */
		list_add_tail(&qh->qh_list_entry,
			      &hsotg->non_periodic_sched_inactive);
		return 0;
	}

	status = dwc2_schedule_periodic(hsotg, qh);
	if (status)
		return status;
	if (!hsotg->periodic_qh_count) {
		intr_mask = readl(hsotg->regs + GINTMSK);
		intr_mask |= GINTSTS_SOF;
		writel(intr_mask, hsotg->regs + GINTMSK);
	}
	hsotg->periodic_qh_count++;

	return 0;
}

/**
 * dwc2_hcd_qh_unlink() - Removes a QH from either the non-periodic or periodic
 * schedule. Memory is not freed.
 *
 * @hsotg: The HCD state structure
 * @qh:    QH to remove from schedule
 */
void dwc2_hcd_qh_unlink(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	u32 intr_mask;

	dev_vdbg(hsotg->dev, "%s()\n", __func__);

	if (list_empty(&qh->qh_list_entry))
		/* QH is not in a schedule */
		return;

	if (dwc2_qh_is_non_per(qh)) {
		if (hsotg->non_periodic_qh_ptr == &qh->qh_list_entry)
			hsotg->non_periodic_qh_ptr =
					hsotg->non_periodic_qh_ptr->next;
		list_del_init(&qh->qh_list_entry);
		return;
	}

	dwc2_deschedule_periodic(hsotg, qh);
	hsotg->periodic_qh_count--;
	if (!hsotg->periodic_qh_count) {
		intr_mask = readl(hsotg->regs + GINTMSK);
		intr_mask &= ~GINTSTS_SOF;
		writel(intr_mask, hsotg->regs + GINTMSK);
	}
}

/*
 * Schedule the next continuing periodic split transfer
 */
static void dwc2_sched_periodic_split(struct dwc2_hsotg *hsotg,
				      struct dwc2_qh *qh, u16 frame_number,
				      int sched_next_periodic_split)
{
	u16 incr;

	if (sched_next_periodic_split) {
		qh->sched_frame = frame_number;
		incr = dwc2_frame_num_inc(qh->start_split_frame, 1);
		if (dwc2_frame_num_le(frame_number, incr)) {
			/*
			 * Allow one frame to elapse after start split
			 * microframe before scheduling complete split, but
			 * DON'T if we are doing the next start split in the
			 * same frame for an ISOC out
			 */
			if (qh->ep_type != USB_ENDPOINT_XFER_ISOC ||
			    qh->ep_is_in != 0) {
				qh->sched_frame =
					dwc2_frame_num_inc(qh->sched_frame, 1);
			}
		}
	} else {
		qh->sched_frame = dwc2_frame_num_inc(qh->start_split_frame,
						     qh->interval);
		if (dwc2_frame_num_le(qh->sched_frame, frame_number))
			qh->sched_frame = frame_number;
		qh->sched_frame |= 0x7;
		qh->start_split_frame = qh->sched_frame;
	}
}

/*
 * Deactivates a QH. For non-periodic QHs, removes the QH from the active
 * non-periodic schedule. The QH is added to the inactive non-periodic
 * schedule if any QTDs are still attached to the QH.
 *
 * For periodic QHs, the QH is removed from the periodic queued schedule. If
 * there are any QTDs still attached to the QH, the QH is added to either the
 * periodic inactive schedule or the periodic ready schedule and its next
 * scheduled frame is calculated. The QH is placed in the ready schedule if
 * the scheduled frame has been reached already. Otherwise it's placed in the
 * inactive schedule. If there are no QTDs attached to the QH, the QH is
 * completely removed from the periodic schedule.
 */
void dwc2_hcd_qh_deactivate(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh,
			    int sched_next_periodic_split)
{
	u16 frame_number;

	if (dbg_qh(qh))
		dev_vdbg(hsotg->dev, "%s()\n", __func__);

	if (dwc2_qh_is_non_per(qh)) {
		dwc2_hcd_qh_unlink(hsotg, qh);
		if (!list_empty(&qh->qtd_list))
			/* Add back to inactive non-periodic schedule */
			dwc2_hcd_qh_add(hsotg, qh);
		return;
	}

	frame_number = dwc2_hcd_get_frame_number(hsotg);

	if (qh->do_split) {
		dwc2_sched_periodic_split(hsotg, qh, frame_number,
					  sched_next_periodic_split);
	} else {
		qh->sched_frame = dwc2_frame_num_inc(qh->sched_frame,
						     qh->interval);
		if (dwc2_frame_num_le(qh->sched_frame, frame_number))
			qh->sched_frame = frame_number;
	}

	if (list_empty(&qh->qtd_list)) {
		dwc2_hcd_qh_unlink(hsotg, qh);
		return;
	}
	/*
	 * Remove from periodic_sched_queued and move to
	 * appropriate queue
	 */
	if ((hsotg->core_params->uframe_sched > 0 &&
	     dwc2_frame_num_le(qh->sched_frame, frame_number)) ||
	    (hsotg->core_params->uframe_sched <= 0 &&
	     qh->sched_frame == frame_number))
		list_move(&qh->qh_list_entry, &hsotg->periodic_sched_ready);
	else
		list_move(&qh->qh_list_entry, &hsotg->periodic_sched_inactive);
}

/**
 * dwc2_hcd_qtd_init() - Initializes a QTD structure
 *
 * @qtd: The QTD to initialize
 * @urb: The associated URB
 */
void dwc2_hcd_qtd_init(struct dwc2_qtd *qtd, struct dwc2_hcd_urb *urb)
{
	qtd->urb = urb;
	if (dwc2_hcd_get_pipe_type(&urb->pipe_info) ==
			USB_ENDPOINT_XFER_CONTROL) {
		/*
		 * The only time the QTD data toggle is used is on the data
		 * phase of control transfers. This phase always starts with
		 * DATA1.
		 */
		qtd->data_toggle = DWC2_HC_PID_DATA1;
		qtd->control_phase = DWC2_CONTROL_SETUP;
	}

	/* Start split */
	qtd->complete_split = 0;
	qtd->isoc_split_pos = DWC2_HCSPLT_XACTPOS_ALL;
	qtd->isoc_split_offset = 0;
	qtd->in_process = 0;

	/* Store the qtd ptr in the urb to reference the QTD */
	urb->qtd = qtd;
}

/**
 * dwc2_hcd_qtd_add() - Adds a QTD to the QTD-list of a QH
 *			Caller must hold driver lock.
 *
 * @hsotg:        The DWC HCD structure
 * @qtd:          The QTD to add
 * @qh:           Queue head to add qtd to
 *
 * Return: 0 if successful, negative error code otherwise
 *
 * If the QH to which the QTD is added is not currently scheduled, it is placed
 * into the proper schedule based on its EP type.
 */
int dwc2_hcd_qtd_add(struct dwc2_hsotg *hsotg, struct dwc2_qtd *qtd,
		     struct dwc2_qh *qh)
{
	int retval;

	if (unlikely(!qh)) {
		dev_err(hsotg->dev, "%s: Invalid QH\n", __func__);
		retval = -EINVAL;
		goto fail;
	}

	retval = dwc2_hcd_qh_add(hsotg, qh);
	if (retval)
		goto fail;

	qtd->qh = qh;
	list_add_tail(&qtd->qtd_list_entry, &qh->qtd_list);

	return 0;
fail:
	return retval;
}
