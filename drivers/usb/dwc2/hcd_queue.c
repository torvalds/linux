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
#include <linux/gcd.h>
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

/* Wait this long before releasing periodic reservation */
#define DWC2_UNRESERVE_DELAY (msecs_to_jiffies(5))

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
		max_claimed_usecs = 100 - qh->host_us;
	} else {
		/*
		 * Full speed mode
		 * Max periodic usecs is 90% x 1000 usec = 900 usec
		 */
		max_claimed_usecs = 900 - qh->host_us;
	}

	if (hsotg->periodic_usecs > max_claimed_usecs) {
		dev_err(hsotg->dev,
			"%s: already claimed usecs %d, required usecs %d\n",
			__func__, hsotg->periodic_usecs, qh->host_us);
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
	unsigned short utime = qh->host_us;
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
	unsigned short utime = qh->host_us;
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
 * dwc2_pick_first_frame() - Choose 1st frame for qh that's already scheduled
 *
 * Takes a qh that has already been scheduled (which means we know we have the
 * bandwdith reserved for us) and set the next_active_frame and the
 * start_active_frame.
 *
 * This is expected to be called on qh's that weren't previously actively
 * running.  It just picks the next frame that we can fit into without any
 * thought about the past.
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:    QH for a periodic endpoint
 *
 */
static void dwc2_pick_first_frame(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	u16 frame_number;
	u16 earliest_frame;
	u16 next_active_frame;
	u16 interval;

	/*
	 * Use the real frame number rather than the cached value as of the
	 * last SOF to give us a little extra slop.
	 */
	frame_number = dwc2_hcd_get_frame_number(hsotg);

	/*
	 * We wouldn't want to start any earlier than the next frame just in
	 * case the frame number ticks as we're doing this calculation.
	 *
	 * NOTE: if we could quantify how long till we actually get scheduled
	 * we might be able to avoid the "+ 1" by looking at the upper part of
	 * HFNUM (the FRREM field).  For now we'll just use the + 1 though.
	 */
	earliest_frame = dwc2_frame_num_inc(frame_number, 1);
	next_active_frame = earliest_frame;

	/* Get the "no microframe schduler" out of the way... */
	if (hsotg->core_params->uframe_sched <= 0) {
		if (qh->do_split)
			/* Splits are active at microframe 0 minus 1 */
			next_active_frame |= 0x7;
		goto exit;
	}

	/* Adjust interval as per high speed schedule which has 8 uFrame */
	interval = gcd(qh->host_interval, 8);

	/*
	 * We know interval must divide (HFNUM_MAX_FRNUM + 1) now that we've
	 * done the gcd(), so it's safe to move to the beginning of the current
	 * interval like this.
	 *
	 * After this we might be before earliest_frame, but don't worry,
	 * we'll fix it...
	 */
	next_active_frame = (next_active_frame / interval) * interval;

	/*
	 * Actually choose to start at the frame number we've been
	 * scheduled for.
	 */
	next_active_frame = dwc2_frame_num_inc(next_active_frame,
					       qh->assigned_uframe);

	/*
	 * We actually need 1 frame before since the next_active_frame is
	 * the frame number we'll be put on the ready list and we won't be on
	 * the bus until 1 frame later.
	 */
	next_active_frame = dwc2_frame_num_dec(next_active_frame, 1);

	/*
	 * By now we might actually be before the earliest_frame.  Let's move
	 * up intervals until we're not.
	 */
	while (dwc2_frame_num_gt(earliest_frame, next_active_frame))
		next_active_frame = dwc2_frame_num_inc(next_active_frame,
						       interval);

exit:
	qh->next_active_frame = next_active_frame;
	qh->start_active_frame = next_active_frame;

	dwc2_sch_vdbg(hsotg, "QH=%p First fn=%04x nxt=%04x\n",
		     qh, frame_number, qh->next_active_frame);
}

/**
 * dwc2_do_reserve() - Make a periodic reservation
 *
 * Try to allocate space in the periodic schedule.  Depending on parameters
 * this might use the microframe scheduler or the dumb scheduler.
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:    QH for the periodic transfer.
 *
 * Returns: 0 upon success; error upon failure.
 */
static int dwc2_do_reserve(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	int status;

	if (hsotg->core_params->uframe_sched > 0) {
		status = dwc2_find_uframe(hsotg, qh);
		if (status >= 0)
			qh->assigned_uframe = status;
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

	if (hsotg->core_params->uframe_sched <= 0)
		/* Reserve periodic channel */
		hsotg->periodic_channels++;

	/* Update claimed usecs per (micro)frame */
	hsotg->periodic_usecs += qh->host_us;

	dwc2_pick_first_frame(hsotg, qh);

	return 0;
}

/**
 * dwc2_do_unreserve() - Actually release the periodic reservation
 *
 * This function actually releases the periodic bandwidth that was reserved
 * by the given qh.
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:    QH for the periodic transfer.
 */
static void dwc2_do_unreserve(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	assert_spin_locked(&hsotg->lock);

	WARN_ON(!qh->unreserve_pending);

	/* No more unreserve pending--we're doing it */
	qh->unreserve_pending = false;

	if (WARN_ON(!list_empty(&qh->qh_list_entry)))
		list_del_init(&qh->qh_list_entry);

	/* Update claimed usecs per (micro)frame */
	hsotg->periodic_usecs -= qh->host_us;

	if (hsotg->core_params->uframe_sched > 0) {
		int i;

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
 * dwc2_unreserve_timer_fn() - Timer function to release periodic reservation
 *
 * According to the kernel doc for usb_submit_urb() (specifically the part about
 * "Reserved Bandwidth Transfers"), we need to keep a reservation active as
 * long as a device driver keeps submitting.  Since we're using HCD_BH to give
 * back the URB we need to give the driver a little bit of time before we
 * release the reservation.  This worker is called after the appropriate
 * delay.
 *
 * @work: Pointer to a qh unreserve_work.
 */
static void dwc2_unreserve_timer_fn(unsigned long data)
{
	struct dwc2_qh *qh = (struct dwc2_qh *)data;
	struct dwc2_hsotg *hsotg = qh->hsotg;
	unsigned long flags;

	/*
	 * Wait for the lock, or for us to be scheduled again.  We
	 * could be scheduled again if:
	 * - We started executing but didn't get the lock yet.
	 * - A new reservation came in, but cancel didn't take effect
	 *   because we already started executing.
	 * - The timer has been kicked again.
	 * In that case cancel and wait for the next call.
	 */
	while (!spin_trylock_irqsave(&hsotg->lock, flags)) {
		if (timer_pending(&qh->unreserve_timer))
			return;
	}

	/*
	 * Might be no more unreserve pending if:
	 * - We started executing but didn't get the lock yet.
	 * - A new reservation came in, but cancel didn't take effect
	 *   because we already started executing.
	 *
	 * We can't put this in the loop above because unreserve_pending needs
	 * to be accessed under lock, so we can only check it once we got the
	 * lock.
	 */
	if (qh->unreserve_pending)
		dwc2_do_unreserve(hsotg, qh);

	spin_unlock_irqrestore(&hsotg->lock, flags);
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

	status = dwc2_check_max_xfer_size(hsotg, qh);
	if (status) {
		dev_dbg(hsotg->dev,
			"%s: Channel max transfer size too small for periodic transfer\n",
			__func__);
		return status;
	}

	/* Cancel pending unreserve; if canceled OK, unreserve was pending */
	if (del_timer(&qh->unreserve_timer))
		WARN_ON(!qh->unreserve_pending);

	/*
	 * Only need to reserve if there's not an unreserve pending, since if an
	 * unreserve is pending then by definition our old reservation is still
	 * valid.  Unreserve might still be pending even if we didn't cancel if
	 * dwc2_unreserve_timer_fn() already started.  Code in the timer handles
	 * that case.
	 */
	if (!qh->unreserve_pending) {
		status = dwc2_do_reserve(hsotg, qh);
		if (status)
			return status;
	} else {
		/*
		 * It might have been a while, so make sure that frame_number
		 * is still good.  Note: we could also try to use the similar
		 * dwc2_next_periodic_start() but that schedules much more
		 * tightly and we might need to hurry and queue things up.
		 */
		if (dwc2_frame_num_le(qh->next_active_frame,
				      hsotg->frame_number))
			dwc2_pick_first_frame(hsotg, qh);
	}

	qh->unreserve_pending = 0;

	if (hsotg->core_params->dma_desc_enable > 0)
		/* Don't rely on SOF and start in ready schedule */
		list_add_tail(&qh->qh_list_entry, &hsotg->periodic_sched_ready);
	else
		/* Always start in inactive schedule */
		list_add_tail(&qh->qh_list_entry,
			      &hsotg->periodic_sched_inactive);

	return 0;
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
	bool did_modify;

	assert_spin_locked(&hsotg->lock);

	/*
	 * Schedule the unreserve to happen in a little bit.  Cases here:
	 * - Unreserve worker might be sitting there waiting to grab the lock.
	 *   In this case it will notice it's been schedule again and will
	 *   quit.
	 * - Unreserve worker might not be scheduled.
	 *
	 * We should never already be scheduled since dwc2_schedule_periodic()
	 * should have canceled the scheduled unreserve timer (hence the
	 * warning on did_modify).
	 *
	 * We add + 1 to the timer to guarantee that at least 1 jiffy has
	 * passed (otherwise if the jiffy counter might tick right after we
	 * read it and we'll get no delay).
	 */
	did_modify = mod_timer(&qh->unreserve_timer,
			       jiffies + DWC2_UNRESERVE_DELAY + 1);
	WARN_ON(did_modify);
	qh->unreserve_pending = 1;

	list_del_init(&qh->qh_list_entry);
}

/**
 * dwc2_qh_init() - Initializes a QH structure
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:    The QH to init
 * @urb:   Holds the information about the device/endpoint needed to initialize
 *         the QH
 */
static void dwc2_qh_init(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh,
			 struct dwc2_hcd_urb *urb)
{
	int dev_speed, hub_addr, hub_port;
	char *speed, *type;

	dev_vdbg(hsotg->dev, "%s()\n", __func__);

	/* Initialize QH */
	qh->hsotg = hsotg;
	setup_timer(&qh->unreserve_timer, dwc2_unreserve_timer_fn,
		    (unsigned long)qh);
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

		qh->host_us = NS_TO_US(usb_calc_bus_time(qh->do_split ?
			      USB_SPEED_HIGH : dev_speed, qh->ep_is_in,
			      qh->ep_type == USB_ENDPOINT_XFER_ISOC,
			      bytecount));

		qh->host_interval = urb->interval;
		dwc2_sch_dbg(hsotg, "QH=%p init nxt=%04x, fn=%04x, int=%#x\n",
			     qh, qh->next_active_frame, hsotg->frame_number,
			     qh->host_interval);
#if 0
		/* Increase interrupt polling rate for debugging */
		if (qh->ep_type == USB_ENDPOINT_XFER_INT)
			qh->host_interval = 8;
#endif
		hprt = dwc2_readl(hsotg->regs + HPRT0);
		prtspd = (hprt & HPRT0_SPD_MASK) >> HPRT0_SPD_SHIFT;
		if (prtspd == HPRT0_SPD_HIGH_SPEED &&
		    (dev_speed == USB_SPEED_LOW ||
		     dev_speed == USB_SPEED_FULL)) {
			qh->host_interval *= 8;
			dwc2_sch_dbg(hsotg,
				     "QH=%p init*8 nxt=%04x, fn=%04x, int=%#x\n",
				     qh, qh->next_active_frame,
				     hsotg->frame_number, qh->host_interval);

		}
		dev_dbg(hsotg->dev, "interval=%d\n", qh->host_interval);
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
			 qh->host_us);
		dev_vdbg(hsotg->dev, "DWC OTG HCD QH - interval = %d\n",
			 qh->host_interval);
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
	/* Make sure any unreserve work is finished. */
	if (del_timer_sync(&qh->unreserve_timer)) {
		unsigned long flags;

		spin_lock_irqsave(&hsotg->lock, flags);
		dwc2_do_unreserve(hsotg, qh);
		spin_unlock_irqrestore(&hsotg->lock, flags);
	}

	if (qh->desc_list)
		dwc2_hcd_qh_free_ddma(hsotg, qh);
	kfree(qh);
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
		/* Schedule right away */
		qh->start_active_frame = hsotg->frame_number;
		qh->next_active_frame = qh->start_active_frame;

		/* Always start in inactive schedule */
		list_add_tail(&qh->qh_list_entry,
			      &hsotg->non_periodic_sched_inactive);
		return 0;
	}

	status = dwc2_schedule_periodic(hsotg, qh);
	if (status)
		return status;
	if (!hsotg->periodic_qh_count) {
		intr_mask = dwc2_readl(hsotg->regs + GINTMSK);
		intr_mask |= GINTSTS_SOF;
		dwc2_writel(intr_mask, hsotg->regs + GINTMSK);
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
		intr_mask = dwc2_readl(hsotg->regs + GINTMSK);
		intr_mask &= ~GINTSTS_SOF;
		dwc2_writel(intr_mask, hsotg->regs + GINTMSK);
	}
}

/**
 * dwc2_next_for_periodic_split() - Set next_active_frame midway thru a split.
 *
 * This is called for setting next_active_frame for periodic splits for all but
 * the first packet of the split.  Confusing?  I thought so...
 *
 * Periodic splits are single low/full speed transfers that we end up splitting
 * up into several high speed transfers.  They always fit into one full (1 ms)
 * frame but might be split over several microframes (125 us each).  We to put
 * each of the parts on a very specific high speed frame.
 *
 * This function figures out where the next active uFrame needs to be.
 *
 * @hsotg:        The HCD state structure
 * @qh:           QH for the periodic transfer.
 * @frame_number: The current frame number.
 *
 * Return: number missed by (or 0 if we didn't miss).
 */
static int dwc2_next_for_periodic_split(struct dwc2_hsotg *hsotg,
					 struct dwc2_qh *qh, u16 frame_number)
{
	u16 old_frame = qh->next_active_frame;
	u16 prev_frame_number = dwc2_frame_num_dec(frame_number, 1);
	int missed = 0;
	u16 incr;

	/*
	 * Basically: increment 1 normally, but 2 right after the start split
	 * (except for ISOC out).
	 */
	if (old_frame == qh->start_active_frame &&
	    !(qh->ep_type == USB_ENDPOINT_XFER_ISOC && !qh->ep_is_in))
		incr = 2;
	else
		incr = 1;

	qh->next_active_frame = dwc2_frame_num_inc(old_frame, incr);

	/*
	 * Note that it's OK for frame_number to be 1 frame past
	 * next_active_frame.  Remember that next_active_frame is supposed to
	 * be 1 frame _before_ when we want to be scheduled.  If we're 1 frame
	 * past it just means schedule ASAP.
	 *
	 * It's _not_ OK, however, if we're more than one frame past.
	 */
	if (dwc2_frame_num_gt(prev_frame_number, qh->next_active_frame)) {
		/*
		 * OOPS, we missed.  That's actually pretty bad since
		 * the hub will be unhappy; try ASAP I guess.
		 */
		missed = dwc2_frame_num_dec(prev_frame_number,
					    qh->next_active_frame);
		qh->next_active_frame = frame_number;
	}

	return missed;
}

/**
 * dwc2_next_periodic_start() - Set next_active_frame for next transfer start
 *
 * This is called for setting next_active_frame for a periodic transfer for
 * all cases other than midway through a periodic split.  This will also update
 * start_active_frame.
 *
 * Since we _always_ keep start_active_frame as the start of the previous
 * transfer this is normally pretty easy: we just add our interval to
 * start_active_frame and we've got our answer.
 *
 * The tricks come into play if we miss.  In that case we'll look for the next
 * slot we can fit into.
 *
 * @hsotg:        The HCD state structure
 * @qh:           QH for the periodic transfer.
 * @frame_number: The current frame number.
 *
 * Return: number missed by (or 0 if we didn't miss).
 */
static int dwc2_next_periodic_start(struct dwc2_hsotg *hsotg,
				     struct dwc2_qh *qh, u16 frame_number)
{
	int missed = 0;
	u16 interval = qh->host_interval;
	u16 prev_frame_number = dwc2_frame_num_dec(frame_number, 1);

	qh->start_active_frame = dwc2_frame_num_inc(qh->start_active_frame,
						    interval);

	/*
	 * The dwc2_frame_num_gt() function used below won't work terribly well
	 * with if we just incremented by a really large intervals since the
	 * frame counter only goes to 0x3fff.  It's terribly unlikely that we
	 * will have missed in this case anyway.  Just go to exit.  If we want
	 * to try to do better we'll need to keep track of a bigger counter
	 * somewhere in the driver and handle overflows.
	 */
	if (interval >= 0x1000)
		goto exit;

	/*
	 * Test for misses, which is when it's too late to schedule.
	 *
	 * A few things to note:
	 * - We compare against prev_frame_number since start_active_frame
	 *   and next_active_frame are always 1 frame before we want things
	 *   to be active and we assume we can still get scheduled in the
	 *   current frame number.
	 * - It's possible for start_active_frame (now incremented) to be
	 *   next_active_frame if we got an EO MISS (even_odd miss) which
	 *   basically means that we detected there wasn't enough time for
	 *   the last packet and dwc2_hc_set_even_odd_frame() rescheduled us
	 *   at the last second.  We want to make sure we don't schedule
	 *   another transfer for the same frame.  My test webcam doesn't seem
	 *   terribly upset by missing a transfer but really doesn't like when
	 *   we do two transfers in the same frame.
	 * - Some misses are expected.  Specifically, in order to work
	 *   perfectly dwc2 really needs quite spectacular interrupt latency
	 *   requirements.  It needs to be able to handle its interrupts
	 *   completely within 125 us of them being asserted. That not only
	 *   means that the dwc2 interrupt handler needs to be fast but it
	 *   means that nothing else in the system has to block dwc2 for a long
	 *   time.  We can help with the dwc2 parts of this, but it's hard to
	 *   guarantee that a system will have interrupt latency < 125 us, so
	 *   we have to be robust to some misses.
	 */
	if (qh->start_active_frame == qh->next_active_frame ||
	    dwc2_frame_num_gt(prev_frame_number, qh->start_active_frame)) {
		u16 ideal_start = qh->start_active_frame;

		/* Adjust interval as per gcd with plan length. */
		interval = gcd(interval, 8);

		do {
			qh->start_active_frame = dwc2_frame_num_inc(
				qh->start_active_frame, interval);
		} while (dwc2_frame_num_gt(prev_frame_number,
					   qh->start_active_frame));

		missed = dwc2_frame_num_dec(qh->start_active_frame,
					    ideal_start);
	}

exit:
	qh->next_active_frame = qh->start_active_frame;

	return missed;
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
	u16 old_frame = qh->next_active_frame;
	u16 frame_number;
	int missed;

	if (dbg_qh(qh))
		dev_vdbg(hsotg->dev, "%s()\n", __func__);

	if (dwc2_qh_is_non_per(qh)) {
		dwc2_hcd_qh_unlink(hsotg, qh);
		if (!list_empty(&qh->qtd_list))
			/* Add back to inactive non-periodic schedule */
			dwc2_hcd_qh_add(hsotg, qh);
		return;
	}

	/*
	 * Use the real frame number rather than the cached value as of the
	 * last SOF just to get us a little closer to reality.  Note that
	 * means we don't actually know if we've already handled the SOF
	 * interrupt for this frame.
	 */
	frame_number = dwc2_hcd_get_frame_number(hsotg);

	if (sched_next_periodic_split)
		missed = dwc2_next_for_periodic_split(hsotg, qh, frame_number);
	else
		missed = dwc2_next_periodic_start(hsotg, qh, frame_number);

	dwc2_sch_vdbg(hsotg,
		     "QH=%p next(%d) fn=%04x, sch=%04x=>%04x (%+d) miss=%d %s\n",
		     qh, sched_next_periodic_split, frame_number, old_frame,
		     qh->next_active_frame,
		     dwc2_frame_num_dec(qh->next_active_frame, old_frame),
		missed, missed ? "MISS" : "");

	if (list_empty(&qh->qtd_list)) {
		dwc2_hcd_qh_unlink(hsotg, qh);
		return;
	}

	/*
	 * Remove from periodic_sched_queued and move to
	 * appropriate queue
	 *
	 * Note: we purposely use the frame_number from the "hsotg" structure
	 * since we know SOF interrupt will handle future frames.
	 */
	if (dwc2_frame_num_le(qh->next_active_frame, hsotg->frame_number))
		list_move_tail(&qh->qh_list_entry,
			       &hsotg->periodic_sched_ready);
	else
		list_move_tail(&qh->qh_list_entry,
			       &hsotg->periodic_sched_inactive);
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
