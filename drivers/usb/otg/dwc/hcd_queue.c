/*
 * DesignWare HS OTG controller driver
 * Copyright (C) 2006 Synopsys, Inc.
 * Portions Copyright (C) 2010 Applied Micro Circuits Corporation.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/licenses
 * or write to the Free Software Foundation, Inc., 51 Franklin Street,
 * Suite 500, Boston, MA 02110-1335 USA.
 *
 * Based on Synopsys driver version 2.60a
 * Modified by Mark Miesfeld <mmiesfeld@apm.com>
 * Modified by Stefan Roese <sr@denx.de>, DENX Software Engineering
 * Modified by Chuck Meade <chuck@theptrgroup.com>
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING BUT NOT LIMITED TO THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL SYNOPSYS, INC. BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES
 * (INCLUDING BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * This file contains the functions to manage Queue Heads and Queue
 * Transfer Descriptors.
 */

#include "hcd.h"

static inline int is_fs_ls(enum usb_device_speed speed)
{
	return speed == USB_SPEED_FULL || speed == USB_SPEED_LOW;
}

/* Allocates memory for a QH structure. */
static inline struct dwc_qh *dwc_otg_hcd_qh_alloc(void)
{
	return kmalloc(sizeof(struct dwc_qh), GFP_ATOMIC);
}

/**
 * Initializes a QH structure to initialize the QH.
 */
#define SCHEDULE_SLOP 10
static void dwc_otg_hcd_qh_init(struct dwc_hcd *hcd, struct dwc_qh *qh,
				struct urb *urb)
{
	memset(qh, 0, sizeof(struct dwc_qh));

	/* Initialize QH */
	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
		qh->ep_type = USB_ENDPOINT_XFER_CONTROL;
		break;
	case PIPE_BULK:
		qh->ep_type = USB_ENDPOINT_XFER_BULK;
		break;
	case PIPE_ISOCHRONOUS:
		qh->ep_type = USB_ENDPOINT_XFER_ISOC;
		break;
	case PIPE_INTERRUPT:
		qh->ep_type = USB_ENDPOINT_XFER_INT;
		break;
	}

	qh->ep_is_in = usb_pipein(urb->pipe) ? 1 : 0;
	qh->data_toggle = DWC_OTG_HC_PID_DATA0;
	qh->maxp = usb_maxpacket(urb->dev, urb->pipe, !(usb_pipein(urb->pipe)));

	INIT_LIST_HEAD(&qh->qtd_list);
	INIT_LIST_HEAD(&qh->qh_list_entry);

	qh->channel = NULL;
	qh->speed = urb->dev->speed;

	/*
	 * FS/LS Enpoint on HS Hub NOT virtual root hub
	 */
	qh->do_split = 0;
	if (is_fs_ls(urb->dev->speed) && urb->dev->tt && urb->dev->tt->hub &&
	    urb->dev->tt->hub->devnum != 1)
		qh->do_split = 1;

	if (qh->ep_type == USB_ENDPOINT_XFER_INT ||
	    qh->ep_type == USB_ENDPOINT_XFER_ISOC) {
		/* Compute scheduling parameters once and save them. */
		u32 hprt;
		int bytecount = dwc_hb_mult(qh->maxp) *
		    dwc_max_packet(qh->maxp);

		qh->usecs = NS_TO_US(usb_calc_bus_time(urb->dev->speed,
						       usb_pipein(urb->pipe),
						       (qh->ep_type ==
							USB_ENDPOINT_XFER_ISOC),
						       bytecount));

		/* Start in a slightly future (micro)frame. */
		qh->sched_frame = dwc_frame_num_inc(hcd->frame_number,
						    SCHEDULE_SLOP);
		qh->interval = urb->interval;

		hprt = dwc_reg_read(hcd->core_if->host_if->hprt0, 0);
		if (DWC_HPRT0_PRT_SPD_RD(hprt) == DWC_HPRT0_PRTSPD_HIGH_SPEED &&
		    is_fs_ls(urb->dev->speed)) {
			qh->interval *= 8;
			qh->sched_frame |= 0x7;
			qh->start_split_frame = qh->sched_frame;
		}
	}
}

/**
 * This function allocates and initializes a QH.
 */
static struct dwc_qh *dwc_otg_hcd_qh_create(struct dwc_hcd *hcd,
					    struct urb *urb)
{
	struct dwc_qh *qh;

	/* Allocate memory */
	qh = dwc_otg_hcd_qh_alloc();
	if (qh == NULL)
		return NULL;

	dwc_otg_hcd_qh_init(hcd, qh, urb);
	return qh;
}

/**
 * Free each QTD in the QH's QTD-list then free the QH.  QH should already be
 * removed from a list.  QTD list should already be empty if called from URB
 * Dequeue.
 */
void dwc_otg_hcd_qh_free(struct dwc_qh *qh)
{
	struct dwc_qtd *qtd;
	struct list_head *pos, *temp;

	/* Free each QTD in the QTD list */
	list_for_each_safe(pos, temp, &qh->qtd_list) {
		list_del(pos);
		qtd = dwc_list_to_qtd(pos);
		dwc_otg_hcd_qtd_free(qtd);
	}
	kfree(qh);
}

/**
 * Microframe scheduler
 * track the total use in hcd->frame_usecs
 * keep each qh use in qh->frame_usecs
 * when surrendering the qh then donate the time back
 */
static const u16 max_uframe_usecs[] = { 100, 100, 100, 100, 100, 100, 30, 0 };

/*
 * called from dwc_otg_hcd.c:dwc_otg_hcd_init
 */
int init_hcd_usecs(struct dwc_hcd *hcd)
{
	int i;

	for (i = 0; i < 8; i++)
		hcd->frame_usecs[i] = max_uframe_usecs[i];

	return 0;
}

static int find_single_uframe(struct dwc_hcd *hcd, struct dwc_qh *qh)
{
	int i;
	u16 utime;
	int t_left;
	int ret;
	int done;

	ret = -1;
	utime = qh->usecs;
	t_left = utime;
	i = 0;
	done = 0;
	while (done == 0) {
		/* At the start hcd->frame_usecs[i] = max_uframe_usecs[i]; */
		if (utime <= hcd->frame_usecs[i]) {
			hcd->frame_usecs[i] -= utime;
			qh->frame_usecs[i] += utime;
			t_left -= utime;
			ret = i;
			done = 1;
			return ret;
		} else {
			i++;
			if (i == 8) {
				done = 1;
				ret = -1;
			}
		}
	}
	return ret;
}

/*
 * use this for FS apps that can span multiple uframes
 */
static int find_multi_uframe(struct dwc_hcd *hcd, struct dwc_qh *qh)
{
	int i;
	int j;
	u16 utime;
	int t_left;
	int ret;
	int done;
	u16 xtime;

	ret = -1;
	utime = qh->usecs;
	t_left = utime;
	i = 0;
	done = 0;
loop:
	while (done == 0) {
		if (hcd->frame_usecs[i] <= 0) {
			i++;
			if (i == 8) {
				done = 1;
				ret = -1;
			}
			goto loop;
		}

		/*
		 * We need n consequtive slots so use j as a start slot.
		 * j plus j+1 must be enough time (for now)
		 */
		xtime = hcd->frame_usecs[i];
		for (j = i + 1; j < 8; j++) {
			/*
			 * if we add this frame remaining time to xtime we may
			 * be OK, if not we need to test j for a complete frame.
			 */
			if ((xtime + hcd->frame_usecs[j]) < utime) {
				if (hcd->frame_usecs[j] < max_uframe_usecs[j]) {
					j = 8;
					ret = -1;
					continue;
				}
			}
			if (xtime >= utime) {
				ret = i;
				j = 8;	/* stop loop with a good value ret */
				continue;
			}
			/* add the frame time to x time */
			xtime += hcd->frame_usecs[j];
			/* we must have a fully available next frame or break */
			if ((xtime < utime) &&
			    (hcd->frame_usecs[j] == max_uframe_usecs[j])) {
				ret = -1;
				j = 8;	/* stop loop with a bad value ret */
				continue;
			}
		}
		if (ret >= 0) {
			t_left = utime;
			for (j = i; (t_left > 0) && (j < 8); j++) {
				t_left -= hcd->frame_usecs[j];
				if (t_left <= 0) {
					qh->frame_usecs[j] +=
					    hcd->frame_usecs[j] + t_left;
					hcd->frame_usecs[j] = -t_left;
					ret = i;
					done = 1;
				} else {
					qh->frame_usecs[j] +=
					    hcd->frame_usecs[j];
					hcd->frame_usecs[j] = 0;
				}
			}
		} else {
			i++;
			if (i == 8) {
				done = 1;
				ret = -1;
			}
		}
	}
	return ret;
}

static int find_uframe(struct dwc_hcd *hcd, struct dwc_qh *qh)
{
	int ret = -1;

	if (qh->speed == USB_SPEED_HIGH)
		/* if this is a hs transaction we need a full frame */
		ret = find_single_uframe(hcd, qh);
	else
		/* FS transaction may need a sequence of frames */
		ret = find_multi_uframe(hcd, qh);

	return ret;
}

/**
 * Checks that the max transfer size allowed in a host channel is large enough
 * to handle the maximum data transfer in a single (micro)frame for a periodic
 * transfer.
 */
static int check_max_xfer_size(struct dwc_hcd *hcd, struct dwc_qh *qh)
{
	int status = 0;
	u32 max_xfer_size;
	u32 max_channel_xfer_size;

	max_xfer_size = dwc_max_packet(qh->maxp) * dwc_hb_mult(qh->maxp);
	max_channel_xfer_size = hcd->core_if->core_params->max_transfer_size;

	if (max_xfer_size > max_channel_xfer_size) {
		pr_notice("%s: Periodic xfer length %d > max xfer "
			  "length for channel %d\n", __func__, max_xfer_size,
			  max_channel_xfer_size);
		status = -ENOSPC;
	}

	return status;
}

/**
 * Schedules an interrupt or isochronous transfer in the periodic schedule.
 */
static int schedule_periodic(struct dwc_hcd *hcd, struct dwc_qh *qh)
{
	int status;
	struct usb_bus *bus = hcd_to_bus(dwc_otg_hcd_to_hcd(hcd));
	int frame;

	status = find_uframe(hcd, qh);
	frame = -1;
	if (status == 0) {
		frame = 7;
	} else {
		if (status > 0)
			frame = status - 1;
	}
	/* Set the new frame up */
	if (frame > -1) {
		qh->sched_frame &= ~0x7;
		qh->sched_frame |= (frame & 7);
	}
	if (status != -1)
		status = 0;
	if (status) {
		pr_notice("%s: Insufficient periodic bandwidth for "
			  "periodic transfer.\n", __func__);
		return status;
	}
	status = check_max_xfer_size(hcd, qh);
	if (status) {
		pr_notice("%s: Channel max transfer size too small "
			  "for periodic transfer.\n", __func__);
		return status;
	}
	/* Always start in the inactive schedule. */
	list_add_tail(&qh->qh_list_entry, &hcd->periodic_sched_inactive);

	/* Update claimed usecs per (micro)frame. */
	hcd->periodic_usecs += qh->usecs;

	/*
	 * Update average periodic bandwidth claimed and # periodic reqs for
	 * usbfs.
	 */
	bus->bandwidth_allocated += qh->usecs / qh->interval;

	if (qh->ep_type == USB_ENDPOINT_XFER_INT)
		bus->bandwidth_int_reqs++;
	else
		bus->bandwidth_isoc_reqs++;

	return status;
}

/**
 * This function adds a QH to either the non periodic or periodic schedule if
 * it is not already in the schedule. If the QH is already in the schedule, no
 * action is taken.
 */
static int dwc_otg_hcd_qh_add(struct dwc_hcd *hcd, struct dwc_qh *qh)
{
	int status = 0;

	/* QH may already be in a schedule. */
	if (!list_empty(&qh->qh_list_entry))
		goto done;
	/*
	 * Add the new QH to the appropriate schedule. For non-periodic, always
	 * start in the inactive schedule.
	 */
	if (dwc_qh_is_non_per(qh))
		list_add_tail(&qh->qh_list_entry,
			      &hcd->non_periodic_sched_inactive);
	else
		status = schedule_periodic(hcd, qh);

done:
	return status;
}

/**
 * This function adds a QH to the non periodic deferred schedule.
 *
 * @return 0 if successful, negative error code otherwise.
 */
static int dwc_otg_hcd_qh_add_deferred(struct dwc_hcd *hcd, struct dwc_qh *qh)
{
	if (!list_empty(&qh->qh_list_entry))
		/* QH already in a schedule. */
		goto done;

	/* Add the new QH to the non periodic deferred schedule */
	if (dwc_qh_is_non_per(qh))
		list_add_tail(&qh->qh_list_entry,
			      &hcd->non_periodic_sched_deferred);
done:
	return 0;
}

/**
 * Removes an interrupt or isochronous transfer from the periodic schedule.
 */
static void deschedule_periodic(struct dwc_hcd *hcd, struct dwc_qh *qh)
{
	struct usb_bus *bus = hcd_to_bus(dwc_otg_hcd_to_hcd(hcd));
	int i;

	list_del_init(&qh->qh_list_entry);
	/* Update claimed usecs per (micro)frame. */
	hcd->periodic_usecs -= qh->usecs;
	for (i = 0; i < 8; i++) {
		hcd->frame_usecs[i] += qh->frame_usecs[i];
		qh->frame_usecs[i] = 0;
	}
	/*
	 * Update average periodic bandwidth claimed and # periodic reqs for
	 * usbfs.
	 */
	bus->bandwidth_allocated -= qh->usecs / qh->interval;

	if (qh->ep_type == USB_ENDPOINT_XFER_INT)
		bus->bandwidth_int_reqs--;
	else
		bus->bandwidth_isoc_reqs--;
}

/**
 * Removes a QH from either the non-periodic or periodic schedule.  Memory is
 * not freed.
 */
void dwc_otg_hcd_qh_remove(struct dwc_hcd *hcd, struct dwc_qh *qh)
{
	/* Do nothing if QH is not in a schedule */
	if (list_empty(&qh->qh_list_entry))
		return;

	if (dwc_qh_is_non_per(qh)) {
		if (hcd->non_periodic_qh_ptr == &qh->qh_list_entry)
			hcd->non_periodic_qh_ptr =
			    hcd->non_periodic_qh_ptr->next;
		list_del_init(&qh->qh_list_entry);
	} else {
		deschedule_periodic(hcd, qh);
	}
}

/**
 * Defers a QH. For non-periodic QHs, removes the QH from the active
 * non-periodic schedule. The QH is added to the deferred non-periodic
 * schedule if any QTDs are still attached to the QH.
 */
int dwc_otg_hcd_qh_deferr(struct dwc_hcd *hcd, struct dwc_qh *qh, int delay)
{
	int deact = 1;

	if (dwc_qh_is_non_per(qh)) {
		qh->sched_frame = dwc_frame_num_inc(hcd->frame_number, delay);
		qh->channel = NULL;
		qh->qtd_in_process = NULL;
		deact = 0;
		dwc_otg_hcd_qh_remove(hcd, qh);
		if (!list_empty(&qh->qtd_list))
			/* Add back to deferred non-periodic schedule. */
			dwc_otg_hcd_qh_add_deferred(hcd, qh);
	}
	return deact;
}

/**
 *  Schedule the next continuing periodic split transfer
 */
static void sched_next_per_split_xfr(struct dwc_qh *qh, u16 fr_num,
				     int sched_split)
{
	if (sched_split) {
		qh->sched_frame = fr_num;
		if (dwc_frame_num_le(fr_num,
				     dwc_frame_num_inc(qh->start_split_frame,
						       1))) {
			/*
			 * Allow one frame to elapse after start split
			 * microframe before scheduling complete split, but DONT
			 * if we are doing the next start split in the
			 * same frame for an ISOC out.
			 */
			if (qh->ep_type != USB_ENDPOINT_XFER_ISOC ||
			    qh->ep_is_in)
				qh->sched_frame =
				    dwc_frame_num_inc(qh->sched_frame, 1);
		}
	} else {
		qh->sched_frame = dwc_frame_num_inc(qh->start_split_frame,
						    qh->interval);

		if (dwc_frame_num_le(qh->sched_frame, fr_num))
			qh->sched_frame = fr_num;
		qh->sched_frame |= 0x7;
		qh->start_split_frame = qh->sched_frame;
	}
}

/**
 * Deactivates a periodic QH.  The QH is removed from the periodic queued
 * schedule. If there are any QTDs still attached to the QH, the QH is added to
 * either the periodic inactive schedule or the periodic ready schedule and its
 * next scheduled frame is calculated. The QH is placed in the ready schedule if
 * the scheduled frame has been reached already. Otherwise it's placed in the
 * inactive schedule. If there are no QTDs attached to the QH, the QH is
 * completely removed from the periodic schedule.
 */
static void deactivate_periodic_qh(struct dwc_hcd *hcd, struct dwc_qh *qh,
				   int sched_next_split)
{
	/* unsigned long flags; */
	u16 fr_num = dwc_otg_hcd_get_frame_number(dwc_otg_hcd_to_hcd(hcd));

	if (qh->do_split) {
		sched_next_per_split_xfr(qh, fr_num, sched_next_split);
	} else {
		qh->sched_frame = dwc_frame_num_inc(qh->sched_frame,
						    qh->interval);
		if (dwc_frame_num_le(qh->sched_frame, fr_num))
			qh->sched_frame = fr_num;
	}

	if (list_empty(&qh->qtd_list)) {
		dwc_otg_hcd_qh_remove(hcd, qh);
	} else {
		/*
		 * Remove from periodic_sched_queued and move to appropriate
		 * queue.
		 */
		if (qh->sched_frame == fr_num)
			list_move(&qh->qh_list_entry,
				  &hcd->periodic_sched_ready);
		else
			list_move(&qh->qh_list_entry,
				  &hcd->periodic_sched_inactive);
	}
}

/**
 * Deactivates a non-periodic QH.  Removes the QH from the active non-periodic
 * schedule. The QH is added to the inactive non-periodic schedule if any QTDs
 * are still attached to the QH.
 */
static void deactivate_non_periodic_qh(struct dwc_hcd *hcd, struct dwc_qh *qh)
{
	dwc_otg_hcd_qh_remove(hcd, qh);
	if (!list_empty(&qh->qtd_list))
		dwc_otg_hcd_qh_add(hcd, qh);
}

/**
 * Deactivates a QH.  Determines if the QH is periodic or non-periodic and takes
 * the appropriate action.
 */
void dwc_otg_hcd_qh_deactivate(struct dwc_hcd *hcd, struct dwc_qh *qh,
			       int sched_next_periodic_split)
{
	if (dwc_qh_is_non_per(qh))
		deactivate_non_periodic_qh(hcd, qh);
	else
		deactivate_periodic_qh(hcd, qh, sched_next_periodic_split);
}

/**
 * Initializes a QTD structure.
 */
static void dwc_otg_hcd_qtd_init(struct dwc_qtd *qtd, struct urb *urb)
{
	memset(qtd, 0, sizeof(struct dwc_qtd));
	qtd->urb = urb;

	if (usb_pipecontrol(urb->pipe)) {
		/*
		 * The only time the QTD data toggle is used is on the data
		 * phase of control transfers. This phase always starts with
		 * DATA1.
		 */
		qtd->data_toggle = DWC_OTG_HC_PID_DATA1;
		qtd->control_phase = DWC_OTG_CONTROL_SETUP;
	}

	/* start split */
	qtd->complete_split = 0;
	qtd->isoc_split_pos = DWC_HCSPLIT_XACTPOS_ALL;
	qtd->isoc_split_offset = 0;

	/* Store the qtd ptr in the urb to reference what QTD. */
	urb->hcpriv = qtd;

	INIT_LIST_HEAD(&qtd->qtd_list_entry);
	return;
}

/* Allocates memory for a QTD structure. */
static inline struct dwc_qtd *dwc_otg_hcd_qtd_alloc(gfp_t _mem_flags)
{
	return kmalloc(sizeof(struct dwc_qtd), _mem_flags);
}

/**
 * This function allocates and initializes a QTD.
 */
struct dwc_qtd *dwc_otg_hcd_qtd_create(struct urb *urb, gfp_t _mem_flags)
{
	struct dwc_qtd *qtd = dwc_otg_hcd_qtd_alloc(_mem_flags);

	if (!qtd)
		return NULL;

	dwc_otg_hcd_qtd_init(qtd, urb);
	return qtd;
}

/**
 * This function adds a QTD to the QTD-list of a QH.  It will find the correct
 * QH to place the QTD into.  If it does not find a QH, then it will create a
 * new QH. If the QH to which the QTD is added is not currently scheduled, it
 * is placed into the proper schedule based on its EP type.
 *
 */
int dwc_otg_hcd_qtd_add(struct dwc_qtd *qtd, struct dwc_hcd *hcd)
{
	struct usb_host_endpoint *ep;
	struct dwc_qh *qh;
	int retval = 0;
	struct urb *urb = qtd->urb;

	/*
	 * Get the QH which holds the QTD-list to insert to. Create QH if it
	 * doesn't exist.
	 */
	ep = dwc_urb_to_endpoint(urb);

	qh = (struct dwc_qh *)ep->hcpriv;
	if (!qh) {
		qh = dwc_otg_hcd_qh_create(hcd, urb);
		if (!qh) {
			retval = -1;
			goto done;
		}
		ep->hcpriv = qh;
	}
	qtd->qtd_qh_ptr = qh;
	retval = dwc_otg_hcd_qh_add(hcd, qh);
	if (!retval)
		list_add_tail(&qtd->qtd_list_entry, &qh->qtd_list);

done:
	return retval;
}
