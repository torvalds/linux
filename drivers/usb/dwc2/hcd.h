// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * hcd.h - DesignWare HS OTG Controller host-mode declarations
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
#ifndef __DWC2_HCD_H__
#define __DWC2_HCD_H__

/*
 * This file contains the structures, constants, and interfaces for the
 * Host Contoller Driver (HCD)
 *
 * The Host Controller Driver (HCD) is responsible for translating requests
 * from the USB Driver into the appropriate actions on the DWC_otg controller.
 * It isolates the USBD from the specifics of the controller by providing an
 * API to the USBD.
 */

struct dwc2_qh;

/**
 * struct dwc2_host_chan - Software host channel descriptor
 *
 * @hc_num:             Host channel number, used for register address lookup
 * @dev_addr:           Address of the device
 * @ep_num:             Endpoint of the device
 * @ep_is_in:           Endpoint direction
 * @speed:              Device speed. One of the following values:
 *                       - USB_SPEED_LOW
 *                       - USB_SPEED_FULL
 *                       - USB_SPEED_HIGH
 * @ep_type:            Endpoint type. One of the following values:
 *                       - USB_ENDPOINT_XFER_CONTROL: 0
 *                       - USB_ENDPOINT_XFER_ISOC:    1
 *                       - USB_ENDPOINT_XFER_BULK:    2
 *                       - USB_ENDPOINT_XFER_INTR:    3
 * @max_packet:         Max packet size in bytes
 * @data_pid_start:     PID for initial transaction.
 *                       0: DATA0
 *                       1: DATA2
 *                       2: DATA1
 *                       3: MDATA (non-Control EP),
 *                          SETUP (Control EP)
 * @multi_count:        Number of additional periodic transactions per
 *                      (micro)frame
 * @xfer_buf:           Pointer to current transfer buffer position
 * @xfer_dma:           DMA address of xfer_buf
 * @xfer_len:           Total number of bytes to transfer
 * @xfer_count:         Number of bytes transferred so far
 * @start_pkt_count:    Packet count at start of transfer
 * @xfer_started:       True if the transfer has been started
 * @ping:               True if a PING request should be issued on this channel
 * @error_state:        True if the error count for this transaction is non-zero
 * @halt_on_queue:      True if this channel should be halted the next time a
 *                      request is queued for the channel. This is necessary in
 *                      slave mode if no request queue space is available when
 *                      an attempt is made to halt the channel.
 * @halt_pending:       True if the host channel has been halted, but the core
 *                      is not finished flushing queued requests
 * @do_split:           Enable split for the channel
 * @complete_split:     Enable complete split
 * @hub_addr:           Address of high speed hub for the split
 * @hub_port:           Port of the low/full speed device for the split
 * @xact_pos:           Split transaction position. One of the following values:
 *                       - DWC2_HCSPLT_XACTPOS_MID
 *                       - DWC2_HCSPLT_XACTPOS_BEGIN
 *                       - DWC2_HCSPLT_XACTPOS_END
 *                       - DWC2_HCSPLT_XACTPOS_ALL
 * @requests:           Number of requests issued for this channel since it was
 *                      assigned to the current transfer (not counting PINGs)
 * @schinfo:            Scheduling micro-frame bitmap
 * @ntd:                Number of transfer descriptors for the transfer
 * @halt_status:        Reason for halting the host channel
 * @hcint               Contents of the HCINT register when the interrupt came
 * @qh:                 QH for the transfer being processed by this channel
 * @hc_list_entry:      For linking to list of host channels
 * @desc_list_addr:     Current QH's descriptor list DMA address
 * @desc_list_sz:       Current QH's descriptor list size
 * @split_order_list_entry: List entry for keeping track of the order of splits
 *
 * This structure represents the state of a single host channel when acting in
 * host mode. It contains the data items needed to transfer packets to an
 * endpoint via a host channel.
 */
struct dwc2_host_chan {
	u8 hc_num;

	unsigned dev_addr:7;
	unsigned ep_num:4;
	unsigned ep_is_in:1;
	unsigned speed:4;
	unsigned ep_type:2;
	unsigned max_packet:11;
	unsigned data_pid_start:2;
#define DWC2_HC_PID_DATA0	TSIZ_SC_MC_PID_DATA0
#define DWC2_HC_PID_DATA2	TSIZ_SC_MC_PID_DATA2
#define DWC2_HC_PID_DATA1	TSIZ_SC_MC_PID_DATA1
#define DWC2_HC_PID_MDATA	TSIZ_SC_MC_PID_MDATA
#define DWC2_HC_PID_SETUP	TSIZ_SC_MC_PID_SETUP

	unsigned multi_count:2;

	u8 *xfer_buf;
	dma_addr_t xfer_dma;
	u32 xfer_len;
	u32 xfer_count;
	u16 start_pkt_count;
	u8 xfer_started;
	u8 do_ping;
	u8 error_state;
	u8 halt_on_queue;
	u8 halt_pending;
	u8 do_split;
	u8 complete_split;
	u8 hub_addr;
	u8 hub_port;
	u8 xact_pos;
#define DWC2_HCSPLT_XACTPOS_MID	HCSPLT_XACTPOS_MID
#define DWC2_HCSPLT_XACTPOS_END	HCSPLT_XACTPOS_END
#define DWC2_HCSPLT_XACTPOS_BEGIN HCSPLT_XACTPOS_BEGIN
#define DWC2_HCSPLT_XACTPOS_ALL	HCSPLT_XACTPOS_ALL

	u8 requests;
	u8 schinfo;
	u16 ntd;
	enum dwc2_halt_status halt_status;
	u32 hcint;
	struct dwc2_qh *qh;
	struct list_head hc_list_entry;
	dma_addr_t desc_list_addr;
	u32 desc_list_sz;
	struct list_head split_order_list_entry;
};

struct dwc2_hcd_pipe_info {
	u8 dev_addr;
	u8 ep_num;
	u8 pipe_type;
	u8 pipe_dir;
	u16 mps;
};

struct dwc2_hcd_iso_packet_desc {
	u32 offset;
	u32 length;
	u32 actual_length;
	u32 status;
};

struct dwc2_qtd;

struct dwc2_hcd_urb {
	void *priv;
	struct dwc2_qtd *qtd;
	void *buf;
	dma_addr_t dma;
	void *setup_packet;
	dma_addr_t setup_dma;
	u32 length;
	u32 actual_length;
	u32 status;
	u32 error_count;
	u32 packet_count;
	u32 flags;
	u16 interval;
	struct dwc2_hcd_pipe_info pipe_info;
	struct dwc2_hcd_iso_packet_desc iso_descs[0];
};

/* Phases for control transfers */
enum dwc2_control_phase {
	DWC2_CONTROL_SETUP,
	DWC2_CONTROL_DATA,
	DWC2_CONTROL_STATUS,
};

/* Transaction types */
enum dwc2_transaction_type {
	DWC2_TRANSACTION_NONE,
	DWC2_TRANSACTION_PERIODIC,
	DWC2_TRANSACTION_NON_PERIODIC,
	DWC2_TRANSACTION_ALL,
};

/* The number of elements per LS bitmap (per port on multi_tt) */
#define DWC2_ELEMENTS_PER_LS_BITMAP	DIV_ROUND_UP(DWC2_LS_SCHEDULE_SLICES, \
						     BITS_PER_LONG)

/**
 * struct dwc2_tt - dwc2 data associated with a usb_tt
 *
 * @refcount:           Number of Queue Heads (QHs) holding a reference.
 * @usb_tt:             Pointer back to the official usb_tt.
 * @periodic_bitmaps:   Bitmap for which parts of the 1ms frame are accounted
 *                      for already.  Each is DWC2_ELEMENTS_PER_LS_BITMAP
 *			elements (so sizeof(long) times that in bytes).
 *
 * This structure is stored in the hcpriv of the official usb_tt.
 */
struct dwc2_tt {
	int refcount;
	struct usb_tt *usb_tt;
	unsigned long periodic_bitmaps[];
};

/**
 * struct dwc2_hs_transfer_time - Info about a transfer on the high speed bus.
 *
 * @start_schedule_usecs:  The start time on the main bus schedule.  Note that
 *                         the main bus schedule is tightly packed and this
 *			   time should be interpreted as tightly packed (so
 *			   uFrame 0 starts at 0 us, uFrame 1 starts at 100 us
 *			   instead of 125 us).
 * @duration_us:           How long this transfer goes.
 */

struct dwc2_hs_transfer_time {
	u32 start_schedule_us;
	u16 duration_us;
};

/**
 * struct dwc2_qh - Software queue head structure
 *
 * @hsotg:              The HCD state structure for the DWC OTG controller
 * @ep_type:            Endpoint type. One of the following values:
 *                       - USB_ENDPOINT_XFER_CONTROL
 *                       - USB_ENDPOINT_XFER_BULK
 *                       - USB_ENDPOINT_XFER_INT
 *                       - USB_ENDPOINT_XFER_ISOC
 * @ep_is_in:           Endpoint direction
 * @maxp:               Value from wMaxPacketSize field of Endpoint Descriptor
 * @dev_speed:          Device speed. One of the following values:
 *                       - USB_SPEED_LOW
 *                       - USB_SPEED_FULL
 *                       - USB_SPEED_HIGH
 * @data_toggle:        Determines the PID of the next data packet for
 *                      non-controltransfers. Ignored for control transfers.
 *                      One of the following values:
 *                       - DWC2_HC_PID_DATA0
 *                       - DWC2_HC_PID_DATA1
 * @ping_state:         Ping state
 * @do_split:           Full/low speed endpoint on high-speed hub requires split
 * @td_first:           Index of first activated isochronous transfer descriptor
 * @td_last:            Index of last activated isochronous transfer descriptor
 * @host_us:            Bandwidth in microseconds per transfer as seen by host
 * @device_us:          Bandwidth in microseconds per transfer as seen by device
 * @host_interval:      Interval between transfers as seen by the host.  If
 *                      the host is high speed and the device is low speed this
 *                      will be 8 times device interval.
 * @device_interval:    Interval between transfers as seen by the device.
 *                      interval.
 * @next_active_frame:  (Micro)frame _before_ we next need to put something on
 *                      the bus.  We'll move the qh to active here.  If the
 *                      host is in high speed mode this will be a uframe.  If
 *                      the host is in low speed mode this will be a full frame.
 * @start_active_frame: If we are partway through a split transfer, this will be
 *			what next_active_frame was when we started.  Otherwise
 *			it should always be the same as next_active_frame.
 * @num_hs_transfers:   Number of transfers in hs_transfers.
 *                      Normally this is 1 but can be more than one for splits.
 *                      Always >= 1 unless the host is in low/full speed mode.
 * @hs_transfers:       Transfers that are scheduled as seen by the high speed
 *                      bus.  Not used if host is in low or full speed mode (but
 *                      note that it IS USED if the device is low or full speed
 *                      as long as the HOST is in high speed mode).
 * @ls_start_schedule_slice: Start time (in slices) on the low speed bus
 *                           schedule that's being used by this device.  This
 *			     will be on the periodic_bitmap in a
 *                           "struct dwc2_tt".  Not used if this device is high
 *                           speed.  Note that this is in "schedule slice" which
 *                           is tightly packed.
 * @ls_duration_us:     Duration on the low speed bus schedule.
 * @ntd:                Actual number of transfer descriptors in a list
 * @qtd_list:           List of QTDs for this QH
 * @channel:            Host channel currently processing transfers for this QH
 * @qh_list_entry:      Entry for QH in either the periodic or non-periodic
 *                      schedule
 * @desc_list:          List of transfer descriptors
 * @desc_list_dma:      Physical address of desc_list
 * @desc_list_sz:       Size of descriptors list
 * @n_bytes:            Xfer Bytes array. Each element corresponds to a transfer
 *                      descriptor and indicates original XferSize value for the
 *                      descriptor
 * @unreserve_timer:    Timer for releasing periodic reservation.
 * @dwc2_tt:            Pointer to our tt info (or NULL if no tt).
 * @ttport:             Port number within our tt.
 * @tt_buffer_dirty     True if clear_tt_buffer_complete is pending
 * @unreserve_pending:  True if we planned to unreserve but haven't yet.
 * @schedule_low_speed: True if we have a low/full speed component (either the
 *			host is in low/full speed mode or do_split).
 *
 * A Queue Head (QH) holds the static characteristics of an endpoint and
 * maintains a list of transfers (QTDs) for that endpoint. A QH structure may
 * be entered in either the non-periodic or periodic schedule.
 */
struct dwc2_qh {
	struct dwc2_hsotg *hsotg;
	u8 ep_type;
	u8 ep_is_in;
	u16 maxp;
	u8 dev_speed;
	u8 data_toggle;
	u8 ping_state;
	u8 do_split;
	u8 td_first;
	u8 td_last;
	u16 host_us;
	u16 device_us;
	u16 host_interval;
	u16 device_interval;
	u16 next_active_frame;
	u16 start_active_frame;
	s16 num_hs_transfers;
	struct dwc2_hs_transfer_time hs_transfers[DWC2_HS_SCHEDULE_UFRAMES];
	u32 ls_start_schedule_slice;
	u16 ntd;
	struct list_head qtd_list;
	struct dwc2_host_chan *channel;
	struct list_head qh_list_entry;
	struct dwc2_dma_desc *desc_list;
	dma_addr_t desc_list_dma;
	u32 desc_list_sz;
	u32 *n_bytes;
	struct timer_list unreserve_timer;
	struct dwc2_tt *dwc_tt;
	int ttport;
	unsigned tt_buffer_dirty:1;
	unsigned unreserve_pending:1;
	unsigned schedule_low_speed:1;
};

/**
 * struct dwc2_qtd - Software queue transfer descriptor (QTD)
 *
 * @control_phase:      Current phase for control transfers (Setup, Data, or
 *                      Status)
 * @in_process:         Indicates if this QTD is currently processed by HW
 * @data_toggle:        Determines the PID of the next data packet for the
 *                      data phase of control transfers. Ignored for other
 *                      transfer types. One of the following values:
 *                       - DWC2_HC_PID_DATA0
 *                       - DWC2_HC_PID_DATA1
 * @complete_split:     Keeps track of the current split type for FS/LS
 *                      endpoints on a HS Hub
 * @isoc_split_pos:     Position of the ISOC split in full/low speed
 * @isoc_frame_index:   Index of the next frame descriptor for an isochronous
 *                      transfer. A frame descriptor describes the buffer
 *                      position and length of the data to be transferred in the
 *                      next scheduled (micro)frame of an isochronous transfer.
 *                      It also holds status for that transaction. The frame
 *                      index starts at 0.
 * @isoc_split_offset:  Position of the ISOC split in the buffer for the
 *                      current frame
 * @ssplit_out_xfer_count: How many bytes transferred during SSPLIT OUT
 * @error_count:        Holds the number of bus errors that have occurred for
 *                      a transaction within this transfer
 * @n_desc:             Number of DMA descriptors for this QTD
 * @isoc_frame_index_last: Last activated frame (packet) index, used in
 *                      descriptor DMA mode only
 * @urb:                URB for this transfer
 * @qh:                 Queue head for this QTD
 * @qtd_list_entry:     For linking to the QH's list of QTDs
 *
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
struct dwc2_qtd {
	enum dwc2_control_phase control_phase;
	u8 in_process;
	u8 data_toggle;
	u8 complete_split;
	u8 isoc_split_pos;
	u16 isoc_frame_index;
	u16 isoc_split_offset;
	u16 isoc_td_last;
	u16 isoc_td_first;
	u32 ssplit_out_xfer_count;
	u8 error_count;
	u8 n_desc;
	u16 isoc_frame_index_last;
	struct dwc2_hcd_urb *urb;
	struct dwc2_qh *qh;
	struct list_head qtd_list_entry;
};

#ifdef DEBUG
struct hc_xfer_info {
	struct dwc2_hsotg *hsotg;
	struct dwc2_host_chan *chan;
};
#endif

u32 dwc2_calc_frame_interval(struct dwc2_hsotg *hsotg);

/* Gets the struct usb_hcd that contains a struct dwc2_hsotg */
static inline struct usb_hcd *dwc2_hsotg_to_hcd(struct dwc2_hsotg *hsotg)
{
	return (struct usb_hcd *)hsotg->priv;
}

/*
 * Inline used to disable one channel interrupt. Channel interrupts are
 * disabled when the channel is halted or released by the interrupt handler.
 * There is no need to handle further interrupts of that type until the
 * channel is re-assigned. In fact, subsequent handling may cause crashes
 * because the channel structures are cleaned up when the channel is released.
 */
static inline void disable_hc_int(struct dwc2_hsotg *hsotg, int chnum, u32 intr)
{
	u32 mask = dwc2_readl(hsotg->regs + HCINTMSK(chnum));

	mask &= ~intr;
	dwc2_writel(mask, hsotg->regs + HCINTMSK(chnum));
}

void dwc2_hc_cleanup(struct dwc2_hsotg *hsotg, struct dwc2_host_chan *chan);
void dwc2_hc_halt(struct dwc2_hsotg *hsotg, struct dwc2_host_chan *chan,
		  enum dwc2_halt_status halt_status);
void dwc2_hc_start_transfer_ddma(struct dwc2_hsotg *hsotg,
				 struct dwc2_host_chan *chan);

/*
 * Reads HPRT0 in preparation to modify. It keeps the WC bits 0 so that if they
 * are read as 1, they won't clear when written back.
 */
static inline u32 dwc2_read_hprt0(struct dwc2_hsotg *hsotg)
{
	u32 hprt0 = dwc2_readl(hsotg->regs + HPRT0);

	hprt0 &= ~(HPRT0_ENA | HPRT0_CONNDET | HPRT0_ENACHG | HPRT0_OVRCURRCHG);
	return hprt0;
}

static inline u8 dwc2_hcd_get_ep_num(struct dwc2_hcd_pipe_info *pipe)
{
	return pipe->ep_num;
}

static inline u8 dwc2_hcd_get_pipe_type(struct dwc2_hcd_pipe_info *pipe)
{
	return pipe->pipe_type;
}

static inline u16 dwc2_hcd_get_mps(struct dwc2_hcd_pipe_info *pipe)
{
	return pipe->mps;
}

static inline u8 dwc2_hcd_get_dev_addr(struct dwc2_hcd_pipe_info *pipe)
{
	return pipe->dev_addr;
}

static inline u8 dwc2_hcd_is_pipe_isoc(struct dwc2_hcd_pipe_info *pipe)
{
	return pipe->pipe_type == USB_ENDPOINT_XFER_ISOC;
}

static inline u8 dwc2_hcd_is_pipe_int(struct dwc2_hcd_pipe_info *pipe)
{
	return pipe->pipe_type == USB_ENDPOINT_XFER_INT;
}

static inline u8 dwc2_hcd_is_pipe_bulk(struct dwc2_hcd_pipe_info *pipe)
{
	return pipe->pipe_type == USB_ENDPOINT_XFER_BULK;
}

static inline u8 dwc2_hcd_is_pipe_control(struct dwc2_hcd_pipe_info *pipe)
{
	return pipe->pipe_type == USB_ENDPOINT_XFER_CONTROL;
}

static inline u8 dwc2_hcd_is_pipe_in(struct dwc2_hcd_pipe_info *pipe)
{
	return pipe->pipe_dir == USB_DIR_IN;
}

static inline u8 dwc2_hcd_is_pipe_out(struct dwc2_hcd_pipe_info *pipe)
{
	return !dwc2_hcd_is_pipe_in(pipe);
}

int dwc2_hcd_init(struct dwc2_hsotg *hsotg);
void dwc2_hcd_remove(struct dwc2_hsotg *hsotg);

/* Transaction Execution Functions */
enum dwc2_transaction_type dwc2_hcd_select_transactions(
						struct dwc2_hsotg *hsotg);
void dwc2_hcd_queue_transactions(struct dwc2_hsotg *hsotg,
				 enum dwc2_transaction_type tr_type);

/* Schedule Queue Functions */
/* Implemented in hcd_queue.c */
struct dwc2_qh *dwc2_hcd_qh_create(struct dwc2_hsotg *hsotg,
				   struct dwc2_hcd_urb *urb,
					  gfp_t mem_flags);
void dwc2_hcd_qh_free(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh);
int dwc2_hcd_qh_add(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh);
void dwc2_hcd_qh_unlink(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh);
void dwc2_hcd_qh_deactivate(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh,
			    int sched_csplit);

void dwc2_hcd_qtd_init(struct dwc2_qtd *qtd, struct dwc2_hcd_urb *urb);
int dwc2_hcd_qtd_add(struct dwc2_hsotg *hsotg, struct dwc2_qtd *qtd,
		     struct dwc2_qh *qh);

/* Unlinks and frees a QTD */
static inline void dwc2_hcd_qtd_unlink_and_free(struct dwc2_hsotg *hsotg,
						struct dwc2_qtd *qtd,
						struct dwc2_qh *qh)
{
	list_del(&qtd->qtd_list_entry);
	kfree(qtd);
	qtd = NULL;
}

/* Descriptor DMA support functions */
void dwc2_hcd_start_xfer_ddma(struct dwc2_hsotg *hsotg,
			      struct dwc2_qh *qh);
void dwc2_hcd_complete_xfer_ddma(struct dwc2_hsotg *hsotg,
				 struct dwc2_host_chan *chan, int chnum,
					enum dwc2_halt_status halt_status);

int dwc2_hcd_qh_init_ddma(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh,
			  gfp_t mem_flags);
void dwc2_hcd_qh_free_ddma(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh);

/* Check if QH is non-periodic */
#define dwc2_qh_is_non_per(_qh_ptr_) \
	((_qh_ptr_)->ep_type == USB_ENDPOINT_XFER_BULK || \
	 (_qh_ptr_)->ep_type == USB_ENDPOINT_XFER_CONTROL)

#ifdef CONFIG_USB_DWC2_DEBUG_PERIODIC
static inline bool dbg_hc(struct dwc2_host_chan *hc) { return true; }
static inline bool dbg_qh(struct dwc2_qh *qh) { return true; }
static inline bool dbg_urb(struct urb *urb) { return true; }
static inline bool dbg_perio(void) { return true; }
#else /* !CONFIG_USB_DWC2_DEBUG_PERIODIC */
static inline bool dbg_hc(struct dwc2_host_chan *hc)
{
	return hc->ep_type == USB_ENDPOINT_XFER_BULK ||
	       hc->ep_type == USB_ENDPOINT_XFER_CONTROL;
}

static inline bool dbg_qh(struct dwc2_qh *qh)
{
	return qh->ep_type == USB_ENDPOINT_XFER_BULK ||
	       qh->ep_type == USB_ENDPOINT_XFER_CONTROL;
}

static inline bool dbg_urb(struct urb *urb)
{
	return usb_pipetype(urb->pipe) == PIPE_BULK ||
	       usb_pipetype(urb->pipe) == PIPE_CONTROL;
}

static inline bool dbg_perio(void) { return false; }
#endif

/* High bandwidth multiplier as encoded in highspeed endpoint descriptors */
#define dwc2_hb_mult(wmaxpacketsize) (1 + (((wmaxpacketsize) >> 11) & 0x03))

/* Packet size for any kind of endpoint descriptor */
#define dwc2_max_packet(wmaxpacketsize) ((wmaxpacketsize) & 0x07ff)

/*
 * Returns true if frame1 index is greater than frame2 index. The comparison
 * is done modulo FRLISTEN_64_SIZE. This accounts for the rollover of the
 * frame number when the max index frame number is reached.
 */
static inline bool dwc2_frame_idx_num_gt(u16 fr_idx1, u16 fr_idx2)
{
	u16 diff = fr_idx1 - fr_idx2;
	u16 sign = diff & (FRLISTEN_64_SIZE >> 1);

	return diff && !sign;
}

/*
 * Returns true if frame1 is less than or equal to frame2. The comparison is
 * done modulo HFNUM_MAX_FRNUM. This accounts for the rollover of the
 * frame number when the max frame number is reached.
 */
static inline int dwc2_frame_num_le(u16 frame1, u16 frame2)
{
	return ((frame2 - frame1) & HFNUM_MAX_FRNUM) <= (HFNUM_MAX_FRNUM >> 1);
}

/*
 * Returns true if frame1 is greater than frame2. The comparison is done
 * modulo HFNUM_MAX_FRNUM. This accounts for the rollover of the frame
 * number when the max frame number is reached.
 */
static inline int dwc2_frame_num_gt(u16 frame1, u16 frame2)
{
	return (frame1 != frame2) &&
	       ((frame1 - frame2) & HFNUM_MAX_FRNUM) < (HFNUM_MAX_FRNUM >> 1);
}

/*
 * Increments frame by the amount specified by inc. The addition is done
 * modulo HFNUM_MAX_FRNUM. Returns the incremented value.
 */
static inline u16 dwc2_frame_num_inc(u16 frame, u16 inc)
{
	return (frame + inc) & HFNUM_MAX_FRNUM;
}

static inline u16 dwc2_frame_num_dec(u16 frame, u16 dec)
{
	return (frame + HFNUM_MAX_FRNUM + 1 - dec) & HFNUM_MAX_FRNUM;
}

static inline u16 dwc2_full_frame_num(u16 frame)
{
	return (frame & HFNUM_MAX_FRNUM) >> 3;
}

static inline u16 dwc2_micro_frame_num(u16 frame)
{
	return frame & 0x7;
}

/*
 * Returns the Core Interrupt Status register contents, ANDed with the Core
 * Interrupt Mask register contents
 */
static inline u32 dwc2_read_core_intr(struct dwc2_hsotg *hsotg)
{
	return dwc2_readl(hsotg->regs + GINTSTS) &
	       dwc2_readl(hsotg->regs + GINTMSK);
}

static inline u32 dwc2_hcd_urb_get_status(struct dwc2_hcd_urb *dwc2_urb)
{
	return dwc2_urb->status;
}

static inline u32 dwc2_hcd_urb_get_actual_length(
		struct dwc2_hcd_urb *dwc2_urb)
{
	return dwc2_urb->actual_length;
}

static inline u32 dwc2_hcd_urb_get_error_count(struct dwc2_hcd_urb *dwc2_urb)
{
	return dwc2_urb->error_count;
}

static inline void dwc2_hcd_urb_set_iso_desc_params(
		struct dwc2_hcd_urb *dwc2_urb, int desc_num, u32 offset,
		u32 length)
{
	dwc2_urb->iso_descs[desc_num].offset = offset;
	dwc2_urb->iso_descs[desc_num].length = length;
}

static inline u32 dwc2_hcd_urb_get_iso_desc_status(
		struct dwc2_hcd_urb *dwc2_urb, int desc_num)
{
	return dwc2_urb->iso_descs[desc_num].status;
}

static inline u32 dwc2_hcd_urb_get_iso_desc_actual_length(
		struct dwc2_hcd_urb *dwc2_urb, int desc_num)
{
	return dwc2_urb->iso_descs[desc_num].actual_length;
}

static inline int dwc2_hcd_is_bandwidth_allocated(struct dwc2_hsotg *hsotg,
						  struct usb_host_endpoint *ep)
{
	struct dwc2_qh *qh = ep->hcpriv;

	if (qh && !list_empty(&qh->qh_list_entry))
		return 1;

	return 0;
}

static inline u16 dwc2_hcd_get_ep_bandwidth(struct dwc2_hsotg *hsotg,
					    struct usb_host_endpoint *ep)
{
	struct dwc2_qh *qh = ep->hcpriv;

	if (!qh) {
		WARN_ON(1);
		return 0;
	}

	return qh->host_us;
}

void dwc2_hcd_save_data_toggle(struct dwc2_hsotg *hsotg,
			       struct dwc2_host_chan *chan, int chnum,
				      struct dwc2_qtd *qtd);

/* HCD Core API */

/**
 * dwc2_handle_hcd_intr() - Called on every hardware interrupt
 *
 * @hsotg: The DWC2 HCD
 *
 * Returns IRQ_HANDLED if interrupt is handled
 * Return IRQ_NONE if interrupt is not handled
 */
irqreturn_t dwc2_handle_hcd_intr(struct dwc2_hsotg *hsotg);

/**
 * dwc2_hcd_stop() - Halts the DWC_otg host mode operation
 *
 * @hsotg: The DWC2 HCD
 */
void dwc2_hcd_stop(struct dwc2_hsotg *hsotg);

/**
 * dwc2_hcd_is_b_host() - Returns 1 if core currently is acting as B host,
 * and 0 otherwise
 *
 * @hsotg: The DWC2 HCD
 */
int dwc2_hcd_is_b_host(struct dwc2_hsotg *hsotg);

/**
 * dwc2_hcd_dump_state() - Dumps hsotg state
 *
 * @hsotg: The DWC2 HCD
 *
 * NOTE: This function will be removed once the peripheral controller code
 * is integrated and the driver is stable
 */
void dwc2_hcd_dump_state(struct dwc2_hsotg *hsotg);

/**
 * dwc2_hcd_dump_frrem() - Dumps the average frame remaining at SOF
 *
 * @hsotg: The DWC2 HCD
 *
 * This can be used to determine average interrupt latency. Frame remaining is
 * also shown for start transfer and two additional sample points.
 *
 * NOTE: This function will be removed once the peripheral controller code
 * is integrated and the driver is stable
 */
void dwc2_hcd_dump_frrem(struct dwc2_hsotg *hsotg);

/* URB interface */

/* Transfer flags */
#define URB_GIVEBACK_ASAP	0x1
#define URB_SEND_ZERO_PACKET	0x2

/* Host driver callbacks */
struct dwc2_tt *dwc2_host_get_tt_info(struct dwc2_hsotg *hsotg,
				      void *context, gfp_t mem_flags,
				      int *ttport);

void dwc2_host_put_tt_info(struct dwc2_hsotg *hsotg,
			   struct dwc2_tt *dwc_tt);
int dwc2_host_get_speed(struct dwc2_hsotg *hsotg, void *context);
void dwc2_host_complete(struct dwc2_hsotg *hsotg, struct dwc2_qtd *qtd,
			int status);

#ifdef DEBUG
/*
 * Macro to sample the remaining PHY clocks left in the current frame. This
 * may be used during debugging to determine the average time it takes to
 * execute sections of code. There are two possible sample points, "a" and
 * "b", so the _letter_ argument must be one of these values.
 *
 * To dump the average sample times, read the "hcd_frrem" sysfs attribute. For
 * example, "cat /sys/devices/lm0/hcd_frrem".
 */
#define dwc2_sample_frrem(_hcd_, _qh_, _letter_)			\
do {									\
	struct hfnum_data _hfnum_;					\
	struct dwc2_qtd *_qtd_;						\
									\
	_qtd_ = list_entry((_qh_)->qtd_list.next, struct dwc2_qtd,	\
			   qtd_list_entry);				\
	if (usb_pipeint(_qtd_->urb->pipe) &&				\
	    (_qh_)->start_active_frame != 0 && !_qtd_->complete_split) { \
		_hfnum_.d32 = dwc2_readl((_hcd_)->regs + HFNUM);	\
		switch (_hfnum_.b.frnum & 0x7) {			\
		case 7:							\
			(_hcd_)->hfnum_7_samples_##_letter_++;		\
			(_hcd_)->hfnum_7_frrem_accum_##_letter_ +=	\
				_hfnum_.b.frrem;			\
			break;						\
		case 0:							\
			(_hcd_)->hfnum_0_samples_##_letter_++;		\
			(_hcd_)->hfnum_0_frrem_accum_##_letter_ +=	\
				_hfnum_.b.frrem;			\
			break;						\
		default:						\
			(_hcd_)->hfnum_other_samples_##_letter_++;	\
			(_hcd_)->hfnum_other_frrem_accum_##_letter_ +=	\
				_hfnum_.b.frrem;			\
			break;						\
		}							\
	}								\
} while (0)
#else
#define dwc2_sample_frrem(_hcd_, _qh_, _letter_)	do {} while (0)
#endif

#endif /* __DWC2_HCD_H__ */
