/* ==========================================================================
 * $File: //dwh/usb_iip/dev/software/otg_ipmate/linux/drivers/dwc_otg_hcd_queue.c $
 * $Revision: #4 $
 * $Date: 2005/09/15 $
 * $Change: 537387 $
 *
 * Synopsys HS OTG Linux Software Driver and documentation (hereinafter,
 * "Software") is an Unsupported proprietary work of Synopsys, Inc. unless
 * otherwise expressly agreed to in writing between Synopsys and you.
 * 
 * The Software IS NOT an item of Licensed Software or Licensed Product under
 * any End User Software License Agreement or Agreement for Licensed Product
 * with Synopsys or any supplement thereto. You are permitted to use and
 * redistribute this Software in source and binary forms, with or without
 * modification, provided that redistributions of source code must retain this
 * notice. You may not view, use, disclose, copy or distribute this file or
 * any information contained herein except pursuant to this license grant from
 * Synopsys. If you do not agree with this notice, including the disclaimer
 * below, then you are not authorized to use the Software.
 * 
 * THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS" BASIS
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * ========================================================================== */
#ifndef DWC_DEVICE_ONLY

/**
 * @file
 *
 * This file contains the functions to manage Queue Heads and Queue
 * Transfer Descriptors.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/irq.h>

#include "dwc_otg_driver.h"
#include "dwc_otg_hcd.h"
#include "dwc_otg_regs.h"

/**
 * This function allocates and initializes a QH.
 *
 * @param _hcd The HCD state structure for the DWC OTG controller.
 * @param[in] _urb Holds the information about the device/endpoint that we need
 * to initialize the QH.
 *
 * @return Returns pointer to the newly allocated QH, or NULL on error. */
dwc_otg_qh_t *dwc_otg_hcd_qh_create (dwc_otg_hcd_t *_hcd, struct urb *_urb)
{
	dwc_otg_qh_t *qh;

	/* Allocate memory */
	/** @todo add memflags argument */
	qh = dwc_otg_hcd_qh_alloc ();
	if (qh == NULL) {
		return NULL;
	}

	dwc_otg_hcd_qh_init (_hcd, qh, _urb);
	return qh;
}

/** Free each QTD in the QH's QTD-list then free the QH.  QH should already be
 * removed from a list.  QTD list should already be empty if called from URB
 * Dequeue.
 *
 * @param[in] _qh The QH to free.
 */
void dwc_otg_hcd_qh_free (dwc_otg_qh_t *_qh)
{
	dwc_otg_qtd_t *qtd;
	struct list_head *pos;

	unsigned long flags;

	local_irq_save(flags);
	
	/* Free each QTD in the QTD list */
	for (pos = _qh->qtd_list.next;
	     pos != &_qh->qtd_list;
	     pos = _qh->qtd_list.next)
	{
		list_del (pos);
		qtd = dwc_list_to_qtd (pos);
		if(qtd->urb)
		{
		    qtd->urb->hcpriv =NULL;
	        qtd->urb->ep->hcpriv = NULL;
	    }
		dwc_otg_hcd_qtd_free (qtd);
		qtd=NULL;
	}
    
	kfree (_qh);
	_qh = NULL;
	local_irq_restore(flags);
	return;
}

/** Initializes a QH structure.
 *
 * @param[in] _hcd The HCD state structure for the DWC OTG controller.
 * @param[in] _qh The QH to init.
 * @param[in] _urb Holds the information about the device/endpoint that we need
 * to initialize the QH. */
#define SCHEDULE_SLOP 10
void dwc_otg_hcd_qh_init(dwc_otg_hcd_t *_hcd, dwc_otg_qh_t *_qh, struct urb *_urb)
{
	memset (_qh, 0, sizeof (dwc_otg_qh_t));
	/* Initialize QH */
	switch (usb_pipetype(_urb->pipe)) {
	case PIPE_CONTROL:
		_qh->ep_type = USB_ENDPOINT_XFER_CONTROL;
		break;
	case PIPE_BULK:
		_qh->ep_type = USB_ENDPOINT_XFER_BULK;
		break;
	case PIPE_ISOCHRONOUS:
		_qh->ep_type = USB_ENDPOINT_XFER_ISOC;
		break;
	case PIPE_INTERRUPT: 
		_qh->ep_type = USB_ENDPOINT_XFER_INT;
		break;
	}

	_qh->ep_is_in = usb_pipein(_urb->pipe) ? 1 : 0;

	_qh->data_toggle = DWC_OTG_HC_PID_DATA0;
	_qh->maxp = usb_maxpacket(_urb->dev, _urb->pipe, !(usb_pipein(_urb->pipe)));
	INIT_LIST_HEAD(&_qh->qtd_list);
	INIT_LIST_HEAD(&_qh->qh_list_entry);
	_qh->channel = NULL;

	/* FS/LS Enpoint on HS Hub 
	 * NOT virtual root hub */
	_qh->do_split = 0;

	if (((_urb->dev->speed == USB_SPEED_LOW) || 
	     (_urb->dev->speed == USB_SPEED_FULL)) &&
	    (_urb->dev->tt) && (_urb->dev->tt->hub)&&
	    (_urb->dev->tt->hub->devnum != 1)) 
	{
		DWC_DEBUGPL(DBG_HCD, "QH init: EP %d: TT found at hub addr %d, for port %d\n", 
			   usb_pipeendpoint(_urb->pipe), _urb->dev->tt->hub->devnum, 
			   _urb->dev->ttport);
		_qh->do_split = 1;
	}

	if (_qh->ep_type == USB_ENDPOINT_XFER_INT ||
	    _qh->ep_type == USB_ENDPOINT_XFER_ISOC) {
		/* Compute scheduling parameters once and save them. */
		hprt0_data_t hprt;

		/** @todo Account for split transfers in the bus time. */
		int bytecount = dwc_hb_mult(_qh->maxp) * dwc_max_packet(_qh->maxp);
		/*
		 * The results from usb_calc_bus_time are in nanosecs,
		 * so divide the result by 1000 to convert to
		 * microsecs expected by this driver
		 */
		_qh->usecs = usb_calc_bus_time(_urb->dev->speed,
					       usb_pipein(_urb->pipe),
					       (_qh->ep_type == USB_ENDPOINT_XFER_ISOC),
					       bytecount) / 1000;

		/* Start in a slightly future (micro)frame. */
		_qh->sched_frame = dwc_frame_num_inc(_hcd->frame_number,
						     SCHEDULE_SLOP);
		_qh->interval = _urb->interval;
#if 0
		/* Increase interrupt polling rate for debugging. */
		if (_qh->ep_type == USB_ENDPOINT_XFER_INT) {
			_qh->interval = 8;
		}
#endif		
		hprt.d32 = dwc_read_reg32(_hcd->core_if->host_if->hprt0);
		if ((hprt.b.prtspd == DWC_HPRT0_PRTSPD_HIGH_SPEED) && 
		    ((_urb->dev->speed == USB_SPEED_LOW) || 
		     (_urb->dev->speed == USB_SPEED_FULL)))
		{
			_qh->interval *= 8;
			_qh->sched_frame |= 0x7;
			_qh->start_split_frame = _qh->sched_frame;
		}

	}

	DWC_DEBUGPL(DBG_HCD, "DWC OTG HCD QH Initialized\n");
	DWC_DEBUGPL(DBG_HCDV, "DWC OTG HCD QH  - qh = %p\n", _qh);
	DWC_DEBUGPL(DBG_HCDV, "DWC OTG HCD QH  - Device Address = %d\n",
		    _urb->dev->devnum);
	DWC_DEBUGPL(DBG_HCDV, "DWC OTG HCD QH  - Endpoint %d, %s\n",
		    usb_pipeendpoint(_urb->pipe),
		    usb_pipein(_urb->pipe) == USB_DIR_IN ? "IN" : "OUT");
	DWC_DEBUGPL(DBG_HCDV, "DWC OTG HCD QH  - Speed = %s\n", 
		    ({ char *speed; switch (_urb->dev->speed) {
		    case USB_SPEED_LOW: speed = "low";	break;
		    case USB_SPEED_FULL: speed = "full";	break;
		    case USB_SPEED_HIGH: speed = "high";	break;
		    default: speed = "?";	break;
		    }; speed;}));
	DWC_DEBUGPL(DBG_HCDV, "DWC OTG HCD QH  - Type = %s\n",
		    ({ char *type; switch (_qh->ep_type) {
		    case USB_ENDPOINT_XFER_ISOC: type = "isochronous";	break;
		    case USB_ENDPOINT_XFER_INT: type = "interrupt";	break;
		    case USB_ENDPOINT_XFER_CONTROL: type = "control";	break;
		    case USB_ENDPOINT_XFER_BULK: type = "bulk";	break;
		    default: type = "?";	break;
		    }; type;}));
#ifdef DEBUG
	if (_qh->ep_type == USB_ENDPOINT_XFER_INT) {
		DWC_DEBUGPL(DBG_HCDV, "DWC OTG HCD QH - usecs = %d\n",
			    _qh->usecs);
		DWC_DEBUGPL(DBG_HCDV, "DWC OTG HCD QH - interval = %d\n",
			    _qh->interval);
	}
#endif	
	
	return;
}

/**
 * Checks that a channel is available for a periodic transfer.
 *
 * @return 0 if successful, negative error code otherise.
 */
static int periodic_channel_available(dwc_otg_hcd_t *_hcd)
{
	/*
	 * Currently assuming that there is a dedicated host channnel for each
	 * periodic transaction plus at least one host channel for
	 * non-periodic transactions.
	 */
	int status;
#ifdef CONFIG_ARCH_RK30
	int num_channels;

	num_channels = _hcd->core_if->core_params->host_channels;
	if ((_hcd->periodic_channels + _hcd->non_periodic_channels < num_channels) &&
	    (_hcd->periodic_channels < num_channels - 1)) {
		status = 0;
	}
	else {
		DWC_NOTICE("%s: Total channels: %d, Periodic: %d, Non-periodic: %d\n",
			   __func__, num_channels, _hcd->periodic_channels,
			   _hcd->non_periodic_channels);
		status = -ENOSPC;
	}
#else
	status = 0;
#endif
	return status;
}

/**
 * Checks that there is sufficient bandwidth for the specified QH in the
 * periodic schedule. For simplicity, this calculation assumes that all the
 * transfers in the periodic schedule may occur in the same (micro)frame.
 *
 * @param _hcd The HCD state structure for the DWC OTG controller.
 * @param _qh QH containing periodic bandwidth required.
 *
 * @return 0 if successful, negative error code otherwise.
 */
static int check_periodic_bandwidth(dwc_otg_hcd_t *_hcd, dwc_otg_qh_t *_qh)
{
	int 		status;
	uint16_t 	max_claimed_usecs;

	status = 0;

	if (_hcd->core_if->core_params->speed == DWC_SPEED_PARAM_HIGH) {
		/*
		 * High speed mode.
		 * Max periodic usecs is 80% x 125 usec = 100 usec.
		 */
		max_claimed_usecs = 100 - _qh->usecs;
	} else {
		/*
		 * Full speed mode.
		 * Max periodic usecs is 90% x 1000 usec = 900 usec.
		 */
		max_claimed_usecs = 900 - _qh->usecs;
	}

	if (_hcd->periodic_usecs > max_claimed_usecs) {
		DWC_NOTICE("%s: already claimed usecs %d, required usecs %d\n",
			   __func__, _hcd->periodic_usecs, _qh->usecs);
		status = -ENOSPC;
	}

	return status;
}
			
/**
 * Checks that the max transfer size allowed in a host channel is large enough
 * to handle the maximum data transfer in a single (micro)frame for a periodic
 * transfer.
 *
 * @param _hcd The HCD state structure for the DWC OTG controller.
 * @param _qh QH for a periodic endpoint.
 *
 * @return 0 if successful, negative error code otherwise.
 */
static int check_max_xfer_size(dwc_otg_hcd_t *_hcd, dwc_otg_qh_t *_qh)
{
	int		status;
	uint32_t 	max_xfer_size;
	uint32_t	max_channel_xfer_size;

	status = 0;

	max_xfer_size = dwc_max_packet(_qh->maxp) * dwc_hb_mult(_qh->maxp);
	max_channel_xfer_size = _hcd->core_if->core_params->max_transfer_size;

	if (max_xfer_size > max_channel_xfer_size) {
		DWC_NOTICE("%s: Periodic xfer length %d > "
			    "max xfer length for channel %d\n",
			    __func__, max_xfer_size, max_channel_xfer_size);
		status = -ENOSPC;
	}

	return status;
}

/**
 * Schedules an interrupt or isochronous transfer in the periodic schedule.
 *
 * @param _hcd The HCD state structure for the DWC OTG controller.
 * @param _qh QH for the periodic transfer. The QH should already contain the
 * scheduling information.
 *
 * @return 0 if successful, negative error code otherwise.
 */
static int schedule_periodic(dwc_otg_hcd_t *_hcd, dwc_otg_qh_t *_qh)
{
	int status = 0;

	status = periodic_channel_available(_hcd);
	if (status) {
		DWC_NOTICE("%s: No host channel available for periodic "
			   "transfer.\n", __func__);
		return status;
	}

	status = check_periodic_bandwidth(_hcd, _qh);
	if (status) {
		DWC_NOTICE("%s: Insufficient periodic bandwidth for "
			   "periodic transfer.\n", __func__);
		return status;
	}

	status = check_max_xfer_size(_hcd, _qh);
	if (status) {
		DWC_NOTICE("%s: Channel max transfer size too small "
			    "for periodic transfer.\n", __func__);
		return status;
	}

	/* Always start in the inactive schedule. */
	list_add_tail(&_qh->qh_list_entry, &_hcd->periodic_sched_inactive);
	_qh->qh_state = QH_INACTIVE;

	/* Reserve the periodic channel. */
	_hcd->periodic_channels++;

	/* Update claimed usecs per (micro)frame. */
	_hcd->periodic_usecs += _qh->usecs;

	/* Update average periodic bandwidth claimed and # periodic reqs for usbfs. */
	hcd_to_bus(dwc_otg_hcd_to_hcd(_hcd))->bandwidth_allocated += _qh->usecs / _qh->interval;
	if (_qh->ep_type == USB_ENDPOINT_XFER_INT) {
		hcd_to_bus(dwc_otg_hcd_to_hcd(_hcd))->bandwidth_int_reqs++;
		DWC_DEBUGPL(DBG_HCD, "Scheduled intr: qh %p, usecs %d, period %d\n",
			    _qh, _qh->usecs, _qh->interval);
	} else {
		hcd_to_bus(dwc_otg_hcd_to_hcd(_hcd))->bandwidth_isoc_reqs++;
		DWC_DEBUGPL(DBG_HCD, "Scheduled isoc: qh %p, usecs %d, period %d\n",
			    _qh, _qh->usecs, _qh->interval);
	}
		
	return status;
}

/**
 * This function adds a QH to either the non periodic or periodic schedule if
 * it is not already in the schedule. If the QH is already in the schedule, no
 * action is taken.
 *
 * @return 0 if successful, negative error code otherwise.
 */
int dwc_otg_hcd_qh_add (dwc_otg_hcd_t *_hcd, dwc_otg_qh_t *_qh)
{
	unsigned long flags;
	int status = 0;

	local_irq_save(flags);

	if (!list_empty(&_qh->qh_list_entry)) {
		/* QH already in a schedule. */
		goto done;
	}

	/* Add the new QH to the appropriate schedule */
	if (dwc_qh_is_non_per(_qh)) {
		/* Always start in the inactive schedule. */
		list_add_tail(&_qh->qh_list_entry, &_hcd->non_periodic_sched_inactive);
	} else {
		status = schedule_periodic(_hcd, _qh);
	}

 done:
	local_irq_restore(flags);

	return status;
}

/**
 * Removes an interrupt or isochronous transfer from the periodic schedule.
 *
 * @param _hcd The HCD state structure for the DWC OTG controller.
 * @param _qh QH for the periodic transfer.
 */
static void deschedule_periodic(dwc_otg_hcd_t *_hcd, dwc_otg_qh_t *_qh)
{
	list_del_init(&_qh->qh_list_entry);

	/* Release the periodic channel reservation. */
	_hcd->periodic_channels--;

	/* Update claimed usecs per (micro)frame. */
	_hcd->periodic_usecs -= _qh->usecs;

	/* Update average periodic bandwidth claimed and # periodic reqs for usbfs. */
	hcd_to_bus(dwc_otg_hcd_to_hcd(_hcd))->bandwidth_allocated -= _qh->usecs / _qh->interval;

	if (_qh->ep_type == USB_ENDPOINT_XFER_INT) {
		hcd_to_bus(dwc_otg_hcd_to_hcd(_hcd))->bandwidth_int_reqs--;
		DWC_DEBUGPL(DBG_HCD, "Descheduled intr: qh %p, usecs %d, period %d\n",
			    _qh, _qh->usecs, _qh->interval);
	} else {
		hcd_to_bus(dwc_otg_hcd_to_hcd(_hcd))->bandwidth_isoc_reqs--;
		DWC_DEBUGPL(DBG_HCD, "Descheduled isoc: qh %p, usecs %d, period %d\n",
			    _qh, _qh->usecs, _qh->interval);
	}
}

/** 
 * Removes a QH from either the non-periodic or periodic schedule.  Memory is
 * not freed.
 *
 * @param[in] _hcd The HCD state structure.
 * @param[in] _qh QH to remove from schedule. */
void dwc_otg_hcd_qh_remove (dwc_otg_hcd_t *_hcd, dwc_otg_qh_t *_qh)
{
	unsigned long flags;

	local_irq_save(flags);

	if (list_empty(&_qh->qh_list_entry)) {
		/* QH is not in a schedule. */
		goto done;
	}

	if (dwc_qh_is_non_per(_qh)) {
		if (_hcd->non_periodic_qh_ptr == &_qh->qh_list_entry) {
			_hcd->non_periodic_qh_ptr = _hcd->non_periodic_qh_ptr->next;
		}
		list_del_init(&_qh->qh_list_entry);
	} else {
		deschedule_periodic(_hcd, _qh);
	}

 done:
	local_irq_restore(flags);
}

/**
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
void dwc_otg_hcd_qh_deactivate(dwc_otg_hcd_t *_hcd, dwc_otg_qh_t *_qh, int sched_next_periodic_split)
{
	unsigned long flags;
	local_irq_save(flags);

	if (dwc_qh_is_non_per(_qh)) {
		dwc_otg_hcd_qh_remove(_hcd, _qh);
		if (!list_empty(&_qh->qtd_list)) {
			/* Add back to inactive non-periodic schedule. */
			dwc_otg_hcd_qh_add(_hcd, _qh);
		return;
		}
	} else {
		uint16_t frame_number =	dwc_otg_hcd_get_frame_number(dwc_otg_hcd_to_hcd(_hcd));

		if (_qh->do_split) {
			/* Schedule the next continuing periodic split transfer */
			if (sched_next_periodic_split) {

				_qh->sched_frame = frame_number;
				if (dwc_frame_num_le(frame_number,
						     dwc_frame_num_inc(_qh->start_split_frame, 1))) {
					/*
					 * Allow one frame to elapse after start
					 * split microframe before scheduling
					 * complete split, but DONT if we are
					 * doing the next start split in the
					 * same frame for an ISOC out.
					 */
					if ((_qh->ep_type != USB_ENDPOINT_XFER_ISOC) || (_qh->ep_is_in != 0)) {
						_qh->sched_frame = dwc_frame_num_inc(_qh->sched_frame, 1);
					}
				}
			} else {
				_qh->sched_frame = dwc_frame_num_inc(_qh->start_split_frame,
								     _qh->interval);
				if (dwc_frame_num_le(_qh->sched_frame, frame_number)) {
					_qh->sched_frame = frame_number;
				}
				_qh->sched_frame |= 0x7;
				_qh->start_split_frame = _qh->sched_frame;
			}
		} else {
			_qh->sched_frame = dwc_frame_num_inc(_qh->sched_frame, _qh->interval);
			if (dwc_frame_num_le(_qh->sched_frame, frame_number)) {
				_qh->sched_frame = frame_number;
			}
		}

		if (list_empty(&_qh->qtd_list)) {
			dwc_otg_hcd_qh_remove(_hcd, _qh);
		} else {
			/*
			 * Remove from periodic_sched_queued and move to
			 * appropriate queue.
			 */
			if (_qh->sched_frame == frame_number) {
				//list_move_tail(&_qh->qh_list_entry,
				//	  &_hcd->periodic_sched_ready);
	            _qh->qh_state = QH_READY;
			} else {
				//list_move_tail(&_qh->qh_list_entry,
				//	  &_hcd->periodic_sched_inactive);
	            _qh->qh_state = QH_INACTIVE;
			}
		}
	}

	local_irq_restore(flags);
}

/** 
 * This function allocates and initializes a QTD. 
 *
 * @param[in] _urb The URB to create a QTD from.  Each URB-QTD pair will end up
 * pointing to each other so each pair should have a unique correlation.
 *
 * @return Returns pointer to the newly allocated QTD, or NULL on error. */
dwc_otg_qtd_t *dwc_otg_hcd_qtd_create (struct urb *_urb)
{
	dwc_otg_qtd_t *qtd;

	qtd = dwc_otg_hcd_qtd_alloc ();
	if (qtd == NULL) {
		return NULL;
	}

	dwc_otg_hcd_qtd_init (qtd, _urb);
	return qtd;
}

/** 
 * Initializes a QTD structure.
 *
 * @param[in] _qtd The QTD to initialize.
 * @param[in] _urb The URB to use for initialization.  */
void dwc_otg_hcd_qtd_init (dwc_otg_qtd_t *_qtd, struct urb *_urb)
{
	memset (_qtd, 0, sizeof (dwc_otg_qtd_t));
	_qtd->urb = _urb;
	if (usb_pipecontrol(_urb->pipe)) {
		/*
		 * The only time the QTD data toggle is used is on the data
		 * phase of control transfers. This phase always starts with
		 * DATA1.
		 */
		_qtd->data_toggle = DWC_OTG_HC_PID_DATA1;
		_qtd->control_phase = DWC_OTG_CONTROL_SETUP;
	}

	/* start split */
	_qtd->complete_split = 0;
	_qtd->isoc_split_pos = DWC_HCSPLIT_XACTPOS_ALL;
	_qtd->isoc_split_offset = 0;

	/* Store the qtd ptr in the urb to reference what QTD. */
	_urb->hcpriv = _qtd;
	return;
}

/**
 * This function adds a QTD to the QTD-list of a QH.  It will find the correct
 * QH to place the QTD into.  If it does not find a QH, then it will create a
 * new QH. If the QH to which the QTD is added is not currently scheduled, it
 * is placed into the proper schedule based on its EP type.
 *
 * @param[in] _qtd The QTD to add
 * @param[in] _dwc_otg_hcd The DWC HCD structure
 *
 * @return 0 if successful, negative error code otherwise.
 */
int dwc_otg_hcd_qtd_add (dwc_otg_qtd_t *_qtd,
			 dwc_otg_hcd_t *_dwc_otg_hcd)
{
	struct usb_host_endpoint *ep;
	dwc_otg_qh_t *qh;
	unsigned long flags;
	int retval = 0;

	struct urb *urb = _qtd->urb;


	/*
	 * Get the QH which holds the QTD-list to insert to. Create QH if it
	 * doesn't exist.
	 */ 
	ep = dwc_urb_to_endpoint(urb);
	qh = (dwc_otg_qh_t *) ep->hcpriv;
	if (qh == NULL) {
		qh = dwc_otg_hcd_qh_create (_dwc_otg_hcd, urb);
		if (qh == NULL) {
			retval = -ENOMEM;
			goto done;
		}
		ep->hcpriv = qh;
	}

	local_irq_save(flags);	
	retval = dwc_otg_hcd_qh_add(_dwc_otg_hcd, qh);
	if (retval == 0) {
		list_add_tail(&_qtd->qtd_list_entry, &qh->qtd_list);
	}

 done:
	local_irq_restore(flags);
	return retval;
}

#endif /* DWC_DEVICE_ONLY */
