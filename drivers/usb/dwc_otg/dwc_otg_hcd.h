/* ==========================================================================
 * $File: //dwh/usb_iip/dev/software/otg_ipmate/linux/drivers/dwc_otg_hcd.h $
 * $Revision: #6 $
 * $Date: 2006/12/05 $
 * $Change: 762293 $
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
#if !defined(__DWC_HCD_H__)
#define __DWC_HCD_H__

#include <linux/list.h>
#include <linux/usb.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
#include <linux/usb/hcd.h>
#else
#include <../drivers/usb/core/hcd.h>
#endif

struct dwc_otg_device;

#include "dwc_otg_cil.h"

/**
 * @file
 *
 * This file contains the structures, constants, and interfaces for
 * the Host Contoller Driver (HCD).
 *
 * The Host Controller Driver (HCD) is responsible for translating requests
 * from the USB Driver into the appropriate actions on the DWC_otg controller.
 * It isolates the USBD from the specifics of the controller by providing an
 * API to the USBD.
 */

/**
 * Phases for control transfers.
 */
typedef enum dwc_otg_control_phase {
	DWC_OTG_CONTROL_SETUP,
	DWC_OTG_CONTROL_DATA,
	DWC_OTG_CONTROL_STATUS
} dwc_otg_control_phase_e;

/** Transaction types. */
typedef enum dwc_otg_transaction_type {
	DWC_OTG_TRANSACTION_NONE,
	DWC_OTG_TRANSACTION_PERIODIC,
	DWC_OTG_TRANSACTION_NON_PERIODIC,
	DWC_OTG_TRANSACTION_ALL
} dwc_otg_transaction_type_e;

/** Transaction types. */
typedef enum qh_status_type {
	QH_INACTIVE,
	QH_ACTIVE,
	QH_READY,
	QH_ASSIGNED,
	QH_QUEUED
} qh_status_type_e;

/**
 * A Queue Transfer Descriptor (QTD) holds the state of a bulk, control,
 * interrupt, or isochronous transfer. A single QTD is created for each URB
 * (of one of these types) submitted to the HCD. The transfer associated with
 * a QTD may require one or multiple transactions.
 *
 * A QTD is linked to a Queue Head, which is entered in either the
 * non-periodic or periodic schedule for execution. When a QTD is chosen for
 * execution, some or all of its transactions may be executed. After
 * execution, the state of the QTD is updated. The QTD may be retired if all
 * its transactions are complete or if an error occurred. Otherwise, it
 * remains in the schedule so more transactions can be executed later.
 */
typedef struct dwc_otg_qtd {
	/**
	 * Determines the PID of the next data packet for the data phase of
	 * control transfers. Ignored for other transfer types.<br>
	 * One of the following values:
	 *	- DWC_OTG_HC_PID_DATA0
	 *	- DWC_OTG_HC_PID_DATA1
	 */
	uint8_t			data_toggle;

	/** Current phase for control transfers (Setup, Data, or Status). */
	dwc_otg_control_phase_e	control_phase;

	/** Keep track of the current split type
	 * for FS/LS endpoints on a HS Hub */
	uint8_t                 complete_split;

	/** How many bytes transferred during SSPLIT OUT */
	uint32_t                ssplit_out_xfer_count;

	/**
	 * Holds the number of bus errors that have occurred for a transaction
	 * within this transfer.
	 */
	uint8_t 		error_count;

	/**
	 * Index of the next frame descriptor for an isochronous transfer. A
	 * frame descriptor describes the buffer position and length of the
	 * data to be transferred in the next scheduled (micro)frame of an
	 * isochronous transfer. It also holds status for that transaction.
	 * The frame index starts at 0.
	 */
	int			isoc_frame_index;

	/** Position of the ISOC split on full/low speed */
	uint8_t                 isoc_split_pos;

	/** Position of the ISOC split in the buffer for the current frame */
	uint16_t                isoc_split_offset;

	/** URB for this transfer */
	struct urb 		*urb;

	/** This list of QTDs */
	struct list_head  	qtd_list_entry;

} dwc_otg_qtd_t;

/**
 * A Queue Head (QH) holds the static characteristics of an endpoint and
 * maintains a list of transfers (QTDs) for that endpoint. A QH structure may
 * be entered in either the non-periodic or periodic schedule.
 */
typedef struct dwc_otg_qh {
	/**
	 * Endpoint type.
	 * One of the following values:
	 * 	- USB_ENDPOINT_XFER_CONTROL
	 *	- USB_ENDPOINT_XFER_ISOC
	 *	- USB_ENDPOINT_XFER_BULK
	 *	- USB_ENDPOINT_XFER_INT
	 */
	uint8_t 		ep_type;
	uint8_t 		ep_is_in;

	/** wMaxPacketSize Field of Endpoint Descriptor. */
	uint16_t		maxp;

	/**
	 * Determines the PID of the next data packet for non-control
	 * transfers. Ignored for control transfers.<br>
	 * One of the following values:
	 *	- DWC_OTG_HC_PID_DATA0
	 * 	- DWC_OTG_HC_PID_DATA1
	 */
	uint8_t			data_toggle;

	/** Ping state if 1. */
	uint8_t 		ping_state;
	
	/**
	 * 
	****/
    uint16_t        qh_state;
	/**
	 * List of QTDs for this QH.
	 */
	struct list_head 	qtd_list;

	/** Host channel currently processing transfers for this QH. */
	dwc_hc_t		*channel;

	/** QTD currently assigned to a host channel for this QH. */
	dwc_otg_qtd_t		*qtd_in_process;

	/** Full/low speed endpoint on high-speed hub requires split. */
	uint8_t                 do_split;

	/** @name Periodic schedule information */
	/** @{ */

	/** Bandwidth in microseconds per (micro)frame. */
	uint8_t			usecs;

	/** Interval between transfers in (micro)frames. */
	uint16_t		interval;

	/**
	 * (micro)frame to initialize a periodic transfer. The transfer
	 * executes in the following (micro)frame.
	 */
	uint16_t		sched_frame;

	/** (micro)frame at which last start split was initialized. */
	uint16_t		start_split_frame;

	/** @} */

	/** Entry for QH in either the periodic or non-periodic schedule. */
	struct list_head        qh_list_entry;
} dwc_otg_qh_t;

/**
 * This structure holds the state of the HCD, including the non-periodic and
 * periodic schedules.
 */
typedef struct dwc_otg_hcd {

	spinlock_t		lock;

	/** DWC OTG Core Interface Layer */
	dwc_otg_core_if_t       *core_if;

	/** Internal DWC HCD Flags */	
	volatile union dwc_otg_hcd_internal_flags {
		uint32_t d32;
		struct {
			unsigned port_connect_status_change : 1;
			unsigned port_connect_status : 1;
			unsigned port_reset_change : 1;
			unsigned port_enable_change : 1;
			unsigned port_suspend_change : 1;
			unsigned port_over_current_change : 1;
			unsigned reserved : 27;
		} b;
	} flags;

	/**
	 * Inactive items in the non-periodic schedule. This is a list of
	 * Queue Heads. Transfers associated with these Queue Heads are not
	 * currently assigned to a host channel.
	 */
	struct list_head 	non_periodic_sched_inactive;

	/**
	 * Active items in the non-periodic schedule. This is a list of
	 * Queue Heads. Transfers associated with these Queue Heads are
	 * currently assigned to a host channel.
	 */
	struct list_head 	non_periodic_sched_active;

	/**
	 * Pointer to the next Queue Head to process in the active
	 * non-periodic schedule.
	 */
	struct list_head 	*non_periodic_qh_ptr;

	/**
	 * Inactive items in the periodic schedule. This is a list of QHs for
	 * periodic transfers that are _not_ scheduled for the next frame.
	 * Each QH in the list has an interval counter that determines when it
	 * needs to be scheduled for execution. This scheduling mechanism
	 * allows only a simple calculation for periodic bandwidth used (i.e.
	 * must assume that all periodic transfers may need to execute in the
	 * same frame). However, it greatly simplifies scheduling and should
	 * be sufficient for the vast majority of OTG hosts, which need to
	 * connect to a small number of peripherals at one time.
	 *
	 * Items move from this list to periodic_sched_ready when the QH
	 * interval counter is 0 at SOF.
	 */
	struct list_head	periodic_sched_inactive;

	/**
	 * List of periodic QHs that are ready for execution in the next
	 * frame, but have not yet been assigned to host channels.
	 *
	 * Items move from this list to periodic_sched_assigned as host
	 * channels become available during the current frame.
	 */
	//struct list_head	periodic_sched_ready;

	/**
	 * List of periodic QHs to be executed in the next frame that are
	 * assigned to host channels.
	 *
	 * Items move from this list to periodic_sched_queued as the
	 * transactions for the QH are queued to the DWC_otg controller.
	 */
	//struct list_head	periodic_sched_assigned;

	/**
	 * List of periodic QHs that have been queued for execution.
	 *
	 * Items move from this list to either periodic_sched_inactive or
	 * periodic_sched_ready when the channel associated with the transfer
	 * is released. If the interval for the QH is 1, the item moves to
	 * periodic_sched_ready because it must be rescheduled for the next
	 * frame. Otherwise, the item moves to periodic_sched_inactive.
	 */
	//struct list_head	periodic_sched_queued;

	/**
	 * Total bandwidth claimed so far for periodic transfers. This value
	 * is in microseconds per (micro)frame. The assumption is that all
	 * periodic transfers may occur in the same (micro)frame.
	 */
	uint16_t		periodic_usecs;

	/**
	 * Frame number read from the core at SOF. The value ranges from 0 to
	 * DWC_HFNUM_MAX_FRNUM.
	 */
	uint16_t		frame_number;

	/**
	 * Free host channels in the controller. This is a list of
	 * dwc_hc_t items.
	 */
	struct list_head 	free_hc_list;

	/**
	 * Number of host channels assigned to periodic transfers. Currently
	 * assuming that there is a dedicated host channel for each periodic
	 * transaction and at least one host channel available for
	 * non-periodic transactions.
	 */
	int			periodic_channels;

	/**
	 * Number of host channels assigned to non-periodic transfers.
	 */
	int			non_periodic_channels;

	/**
	 * Array of pointers to the host channel descriptors. Allows accessing
	 * a host channel descriptor given the host channel number. This is
	 * useful in interrupt handlers.
	 */
	dwc_hc_t		*hc_ptr_array[MAX_EPS_CHANNELS];

	/**
	 * Buffer to use for any data received during the status phase of a
	 * control transfer. Normally no data is transferred during the status
	 * phase. This buffer is used as a bit bucket. 
	 */
	uint8_t			*status_buf;

	/**
	 * DMA address for status_buf.
	 */
	dma_addr_t		status_buf_dma;
#define DWC_OTG_HCD_STATUS_BUF_SIZE 64	

	/**
	 * Structure to allow starting the HCD in a non-interrupt context
	 * during an OTG role change.
	 */
	struct work_struct	start_work;

	/**
	 * Connection timer. An OTG host must display a message if the device
	 * does not connect. Started when the VBus power is turned on via
	 * sysfs attribute "buspower".
	 */
        struct timer_list 	conn_timer;

	/* Tasket to do a reset */
	struct tasklet_struct   *reset_tasklet;

	spinlock_t global_lock;

#ifdef DEBUG
	uint32_t 		frrem_samples;
	uint64_t 		frrem_accum;

	uint32_t		hfnum_7_samples_a;
	uint64_t		hfnum_7_frrem_accum_a;
	uint32_t		hfnum_0_samples_a;
	uint64_t		hfnum_0_frrem_accum_a;
	uint32_t		hfnum_other_samples_a;
	uint64_t		hfnum_other_frrem_accum_a;

	uint32_t		hfnum_7_samples_b;
	uint64_t		hfnum_7_frrem_accum_b;
	uint32_t		hfnum_0_samples_b;
	uint64_t		hfnum_0_frrem_accum_b;
	uint32_t		hfnum_other_samples_b;
	uint64_t		hfnum_other_frrem_accum_b;
#endif	

    /** Flag to indicate whether host controller is enabled. */
    uint8_t host_enabled;

} dwc_otg_hcd_t;

/** Gets the dwc_otg_hcd from a struct usb_hcd */
static inline dwc_otg_hcd_t *hcd_to_dwc_otg_hcd(struct usb_hcd *hcd)
{
	return (dwc_otg_hcd_t *)(hcd->hcd_priv);
}

/** Gets the struct usb_hcd that contains a dwc_otg_hcd_t. */
static inline struct usb_hcd *dwc_otg_hcd_to_hcd(dwc_otg_hcd_t *dwc_otg_hcd)
{
	return container_of((void *)dwc_otg_hcd, struct usb_hcd, hcd_priv);
}

/** @name HCD Create/Destroy Functions */
/** @{ */
extern int __init dwc_otg_hcd_init(struct device *_dev);
extern void dwc_otg_hcd_remove(struct device *_dev);
/** @} */

/** @name Linux HC Driver API Functions */
/** @{ */

extern int dwc_otg_hcd_start(struct usb_hcd *hcd);
extern void dwc_otg_hcd_stop(struct usb_hcd *hcd);
extern int dwc_otg_hcd_get_frame_number(struct usb_hcd *hcd);
extern void dwc_otg_hcd_free(struct usb_hcd *hcd);
extern int dwc_otg_hcd_urb_enqueue(struct usb_hcd *_hcd,
                            struct urb *_urb,
                            gfp_t _mem_flags);
extern int dwc_otg_hcd_urb_dequeue(struct usb_hcd *hcd, 
				   struct urb *urb,
				   int status);
extern void dwc_otg_hcd_endpoint_disable(struct usb_hcd *hcd,
					 struct usb_host_endpoint *ep);
extern irqreturn_t dwc_otg_hcd_irq(struct usb_hcd *hcd);
extern int dwc_otg_hcd_hub_status_data(struct usb_hcd *hcd, 
				       char *buf);
extern int dwc_otg_hcd_hub_control(struct usb_hcd *hcd, 
				   u16 typeReq, 
				   u16 wValue, 
				   u16 wIndex, 
				   char *buf, 
				   u16 wLength);

/** @} */

/** @name Transaction Execution Functions */
/** @{ */
extern dwc_otg_transaction_type_e dwc_otg_hcd_select_transactions(dwc_otg_hcd_t *_hcd);
extern void dwc_otg_hcd_queue_transactions(dwc_otg_hcd_t *_hcd,
					   dwc_otg_transaction_type_e _tr_type);
extern void dwc_otg_hcd_complete_urb(dwc_otg_hcd_t *_hcd, struct urb *_urb,
				     int _status);
/** @} */

/** @name Interrupt Handler Functions */
/** @{ */
extern int32_t dwc_otg_hcd_handle_intr (dwc_otg_hcd_t *_dwc_otg_hcd);
extern int32_t dwc_otg_hcd_handle_sof_intr (dwc_otg_hcd_t *_dwc_otg_hcd);
extern int32_t dwc_otg_hcd_handle_rx_status_q_level_intr (dwc_otg_hcd_t *_dwc_otg_hcd);
extern int32_t dwc_otg_hcd_handle_np_tx_fifo_empty_intr (dwc_otg_hcd_t *_dwc_otg_hcd);
extern int32_t dwc_otg_hcd_handle_perio_tx_fifo_empty_intr (dwc_otg_hcd_t *_dwc_otg_hcd);
extern int32_t dwc_otg_hcd_handle_incomplete_periodic_intr(dwc_otg_hcd_t *_dwc_otg_hcd);
extern int32_t dwc_otg_hcd_handle_port_intr (dwc_otg_hcd_t *_dwc_otg_hcd);
extern int32_t dwc_otg_hcd_handle_conn_id_status_change_intr (dwc_otg_hcd_t *_dwc_otg_hcd);
extern int32_t dwc_otg_hcd_handle_disconnect_intr (dwc_otg_hcd_t *_dwc_otg_hcd);
extern int32_t dwc_otg_hcd_handle_hc_intr (dwc_otg_hcd_t *_dwc_otg_hcd);
extern int32_t dwc_otg_hcd_handle_hc_n_intr (dwc_otg_hcd_t *_dwc_otg_hcd, uint32_t _num);
extern int32_t dwc_otg_hcd_handle_session_req_intr (dwc_otg_hcd_t *_dwc_otg_hcd);
extern int32_t dwc_otg_hcd_handle_wakeup_detected_intr (dwc_otg_hcd_t *_dwc_otg_hcd);
/** @} */


/** @name Schedule Queue Functions */
/** @{ */

/* Implemented in dwc_otg_hcd_queue.c */
extern dwc_otg_qh_t *dwc_otg_hcd_qh_create (dwc_otg_hcd_t *_hcd, struct urb *_urb);
extern void dwc_otg_hcd_qh_init (dwc_otg_hcd_t *_hcd, dwc_otg_qh_t *_qh, struct urb *_urb);
extern void dwc_otg_hcd_qh_free (dwc_otg_qh_t *_qh);
extern int dwc_otg_hcd_qh_add (dwc_otg_hcd_t *_hcd, dwc_otg_qh_t *_qh);
extern void dwc_otg_hcd_qh_remove (dwc_otg_hcd_t *_hcd, dwc_otg_qh_t *_qh);
extern void dwc_otg_hcd_qh_deactivate (dwc_otg_hcd_t *_hcd, dwc_otg_qh_t *_qh, int sched_csplit);

/** Remove and free a QH */
static inline void dwc_otg_hcd_qh_remove_and_free (dwc_otg_hcd_t *_hcd,
						   dwc_otg_qh_t *_qh)
{
	dwc_otg_hcd_qh_remove (_hcd, _qh);
	dwc_otg_hcd_qh_free (_qh);
}

/** Allocates memory for a QH structure.
 * @return Returns the memory allocate or NULL on error. */
static inline dwc_otg_qh_t *dwc_otg_hcd_qh_alloc (void)
{
	return (dwc_otg_qh_t *) kmalloc (sizeof(dwc_otg_qh_t), GFP_ATOMIC);//GFP_KERNEL
}

extern dwc_otg_qtd_t *dwc_otg_hcd_qtd_create (struct urb *urb);
extern void dwc_otg_hcd_qtd_init (dwc_otg_qtd_t *qtd, struct urb *urb);
extern int dwc_otg_hcd_qtd_add (dwc_otg_qtd_t *qtd, dwc_otg_hcd_t *dwc_otg_hcd);

/** Allocates memory for a QTD structure.
 * @return Returns the memory allocate or NULL on error. */
static inline dwc_otg_qtd_t *dwc_otg_hcd_qtd_alloc (void)
{
	return (dwc_otg_qtd_t *) kmalloc (sizeof(dwc_otg_qtd_t), GFP_ATOMIC);//GFP_KERNEL
}

/** Frees the memory for a QTD structure.  QTD should already be removed from
 * list.
 * @param[in] _qtd QTD to free.*/
static inline void dwc_otg_hcd_qtd_free (dwc_otg_qtd_t *_qtd)
{
	kfree (_qtd);
}

/** Removes a QTD from list.
 * @param[in] _qtd QTD to remove from list. */
static inline void dwc_otg_hcd_qtd_remove (dwc_otg_qtd_t *_qtd)
{
	unsigned long flags;
	local_irq_save (flags);
	list_del (&_qtd->qtd_list_entry);
	local_irq_restore (flags);
}

/** Remove and free a QTD */
static inline void dwc_otg_hcd_qtd_remove_and_free (dwc_otg_qtd_t *_qtd)
{
	dwc_otg_hcd_qtd_remove (_qtd);
	dwc_otg_hcd_qtd_free (_qtd);
}

/** @} */


/** @name Internal Functions */
/** @{ */
dwc_otg_qh_t *dwc_urb_to_qh(struct urb *_urb);
void dwc_otg_hcd_dump_frrem(dwc_otg_hcd_t *_hcd);
void dwc_otg_hcd_dump_state(dwc_otg_hcd_t *_hcd);
/** @} */

/** Gets the usb_host_endpoint associated with an URB. */
static inline struct usb_host_endpoint *dwc_urb_to_endpoint(struct urb *_urb)
{
	struct usb_device *dev = _urb->dev;
	int ep_num = usb_pipeendpoint(_urb->pipe);
	
	if (usb_pipein(_urb->pipe))
		return dev->ep_in[ep_num ];
	else
		return dev->ep_out[ep_num ];
}

/**
 * Gets the endpoint number from a _bEndpointAddress argument. The endpoint is
 * qualified with its direction (possible 32 endpoints per device).
 */
#define dwc_ep_addr_to_endpoint(_bEndpointAddress_) ((_bEndpointAddress_ & USB_ENDPOINT_NUMBER_MASK) | \
                                                     ((_bEndpointAddress_ & USB_DIR_IN) != 0) << 4)

/** Gets the QH that contains the list_head */
#define dwc_list_to_qh(_list_head_ptr_) (container_of(_list_head_ptr_,dwc_otg_qh_t,qh_list_entry))

/** Gets the QTD that contains the list_head */
#define dwc_list_to_qtd(_list_head_ptr_) (container_of(_list_head_ptr_,dwc_otg_qtd_t,qtd_list_entry))

/** Check if QH is non-periodic  */
#define dwc_qh_is_non_per(_qh_ptr_) ((_qh_ptr_->ep_type == USB_ENDPOINT_XFER_BULK) || \
                                     (_qh_ptr_->ep_type == USB_ENDPOINT_XFER_CONTROL))

/** High bandwidth multiplier as encoded in highspeed endpoint descriptors */
#define dwc_hb_mult(wMaxPacketSize) (1 + (((wMaxPacketSize) >> 11) & 0x03))

/** Packet size for any kind of endpoint descriptor */
#define dwc_max_packet(wMaxPacketSize) ((wMaxPacketSize) & 0x07ff)

/**
 * Returns true if _frame1 is less than or equal to _frame2. The comparison is
 * done modulo DWC_HFNUM_MAX_FRNUM. This accounts for the rollover of the
 * frame number when the max frame number is reached.
 */
static inline int dwc_frame_num_le(uint16_t _frame1, uint16_t _frame2)
{
	return ((_frame2 - _frame1) & DWC_HFNUM_MAX_FRNUM) <=
		(DWC_HFNUM_MAX_FRNUM >> 1);
}

/**
 * Returns true if _frame1 is greater than _frame2. The comparison is done
 * modulo DWC_HFNUM_MAX_FRNUM. This accounts for the rollover of the frame
 * number when the max frame number is reached.
 */
static inline int dwc_frame_num_gt(uint16_t _frame1, uint16_t _frame2)
{
	return (_frame1 != _frame2) &&
		(((_frame1 - _frame2) & DWC_HFNUM_MAX_FRNUM) <
		 (DWC_HFNUM_MAX_FRNUM >> 1));
}

/**
 * Increments _frame by the amount specified by _inc. The addition is done
 * modulo DWC_HFNUM_MAX_FRNUM. Returns the incremented value.
 */
static inline uint16_t dwc_frame_num_inc(uint16_t _frame, uint16_t _inc)
{
	return (_frame + _inc) & DWC_HFNUM_MAX_FRNUM;
}

static inline uint16_t dwc_full_frame_num (uint16_t _frame)
{
	return ((_frame) & DWC_HFNUM_MAX_FRNUM) >> 3;
}

static inline uint16_t dwc_micro_frame_num (uint16_t _frame)
{
	return (_frame) & 0x7;
}

#ifdef DEBUG
/**
 * Macro to sample the remaining PHY clocks left in the current frame. This
 * may be used during debugging to determine the average time it takes to
 * execute sections of code. There are two possible sample points, "a" and
 * "b", so the _letter argument must be one of these values.
 *
 * To dump the average sample times, read the "hcd_frrem" sysfs attribute. For
 * example, "cat /sys/devices/lm0/hcd_frrem".
 */
#define dwc_sample_frrem(_hcd, _qh, _letter) \
{ \
	hfnum_data_t hfnum; \
	dwc_otg_qtd_t *qtd; \
	qtd = list_entry(_qh->qtd_list.next, dwc_otg_qtd_t, qtd_list_entry); \
	if (usb_pipeint(qtd->urb->pipe) && _qh->start_split_frame != 0 && !qtd->complete_split) { \
		hfnum.d32 = dwc_read_reg32(&_hcd->core_if->host_if->host_global_regs->hfnum); \
		switch (hfnum.b.frnum & 0x7) { \
		case 7: \
			_hcd->hfnum_7_samples_##_letter++; \
			_hcd->hfnum_7_frrem_accum_##_letter += hfnum.b.frrem; \
			break; \
		case 0: \
			_hcd->hfnum_0_samples_##_letter++; \
			_hcd->hfnum_0_frrem_accum_##_letter += hfnum.b.frrem; \
			break; \
		default: \
			_hcd->hfnum_other_samples_##_letter++; \
			_hcd->hfnum_other_frrem_accum_##_letter += hfnum.b.frrem; \
			break; \
		} \
	} \
}
#else
#define dwc_sample_frrem(_hcd, _qh, _letter) 
#endif		
#endif
#endif /* DWC_DEVICE_ONLY */
