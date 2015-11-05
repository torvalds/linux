/*
 * dwc_otg_fiq_fsm.h - Finite state machine FIQ header definitions
 *
 * Copyright (c) 2013 Raspberry Pi Foundation
 *
 * Author: Jonathan Bell <jonathan@raspberrypi.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *	* Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *	* Redistributions in binary form must reproduce the above copyright
 *	  notice, this list of conditions and the following disclaimer in the
 *	  documentation and/or other materials provided with the distribution.
 *	* Neither the name of Raspberry Pi nor the
 *	  names of its contributors may be used to endorse or promote products
 *	  derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This FIQ implements functionality that performs split transactions on
 * the dwc_otg hardware without any outside intervention. A split transaction
 * is "queued" by nominating a specific host channel to perform the entirety
 * of a split transaction. This FIQ will then perform the microframe-precise
 * scheduling required in each phase of the transaction until completion.
 *
 * The FIQ functionality has been surgically implanted into the Synopsys
 * vendor-provided driver.
 *
 */

#ifndef DWC_OTG_FIQ_FSM_H_
#define DWC_OTG_FIQ_FSM_H_

#include "dwc_otg_regs.h"
#include "dwc_otg_cil.h"
#include "dwc_otg_hcd.h"
#include <linux/kernel.h>
#include <linux/irqflags.h>
#include <linux/string.h>
#include <asm/barrier.h>

#if 0
#define FLAME_ON(x)					\
do {							\
	int gpioreg;                                    \
							\
	gpioreg = readl(__io_address(0x20200000+0x8));	\
	gpioreg &= ~(7 << (x-20)*3);			\
	gpioreg |= 0x1 << (x-20)*3;			\
	writel(gpioreg, __io_address(0x20200000+0x8));	\
							\
	writel(1<<x, __io_address(0x20200000+(0x1C)));	\
} while (0)

#define FLAME_OFF(x)					\
do {							\
	writel(1<<x, __io_address(0x20200000+(0x28)));	\
} while (0)
#else 
#define FLAME_ON(x) do { } while (0)
#define FLAME_OFF(X) do { } while (0)
#endif 

/* This is a quick-and-dirty arch-specific register read/write. We know that
 * writes to a peripheral on BCM2835 will always arrive in-order, also that
 * reads and writes are executed in-order therefore the need for memory barriers 
 * is obviated if we're only talking to USB.
 */
#define FIQ_WRITE(_addr_,_data_) (*(volatile unsigned int *) (_addr_) = (_data_))
#define FIQ_READ(_addr_) (*(volatile unsigned int *) (_addr_))

/* FIQ-ified register definitions. Offsets are from dwc_regs_base. */
#define GINTSTS		0x014
#define GINTMSK		0x018
/* Debug register. Poll the top of the received packets FIFO. */
#define GRXSTSR		0x01C
#define HFNUM		0x408
#define HAINT		0x414
#define HAINTMSK	0x418
#define HPRT0		0x440

/* HC_regs start from an offset of 0x500 */
#define HC_START	0x500
#define HC_OFFSET	0x020

#define HC_DMA		0x514

#define HCCHAR		0x00
#define HCSPLT		0x04
#define HCINT		0x08
#define HCINTMSK	0x0C
#define HCTSIZ		0x10

#define ISOC_XACTPOS_ALL 	0b11
#define ISOC_XACTPOS_BEGIN	0b10
#define ISOC_XACTPOS_MID	0b00
#define ISOC_XACTPOS_END	0b01

#define DWC_PID_DATA2	0b01
#define DWC_PID_MDATA	0b11
#define DWC_PID_DATA1	0b10
#define DWC_PID_DATA0	0b00

typedef struct {
	volatile void* base;
	volatile void* ctrl;
	volatile void* outdda;
	volatile void* outddb;
	volatile void* intstat;
} mphi_regs_t;


enum fiq_debug_level {
	FIQDBG_SCHED = (1 << 0),
	FIQDBG_INT   = (1 << 1),
	FIQDBG_ERR   = (1 << 2),
	FIQDBG_PORTHUB = (1 << 3),
};

struct fiq_state;

extern void _fiq_print (enum fiq_debug_level dbg_lvl, volatile struct fiq_state *state, char *fmt, ...);
#if 0
#define fiq_print _fiq_print
#else
#define fiq_print(x, y, ...)
#endif

extern bool fiq_enable, fiq_fsm_enable;
extern ushort nak_holdoff;

/**
 * enum fiq_fsm_state - The FIQ FSM states.
 *
 * This is the "core" of the FIQ FSM. Broadly, the FSM states follow the
 * USB2.0 specification for host responses to various transaction states.
 * There are modifications to this host state machine because of a variety of
 * quirks and limitations in the dwc_otg hardware.
 *
 * The fsm state is also used to communicate back to the driver on completion of
 * a split transaction. The end states are used in conjunction with the interrupts
 * raised by the final transaction.
 */
enum fiq_fsm_state {
	/* FIQ isn't enabled for this host channel */
	FIQ_PASSTHROUGH = 0,
	/* For the first interrupt received for this channel,
	 * the FIQ has to ack any interrupts indicating success. */
	FIQ_PASSTHROUGH_ERRORSTATE = 31,
	/* Nonperiodic state groups */
	FIQ_NP_SSPLIT_STARTED = 1,
	FIQ_NP_SSPLIT_RETRY = 2,
	FIQ_NP_OUT_CSPLIT_RETRY = 3,
	FIQ_NP_IN_CSPLIT_RETRY = 4,
	FIQ_NP_SPLIT_DONE = 5,
	FIQ_NP_SPLIT_LS_ABORTED = 6,
	/* This differentiates a HS transaction error from a LS one
	 * (handling the hub state is different) */
	FIQ_NP_SPLIT_HS_ABORTED = 7,

	/* Periodic state groups */
	/* Periodic transactions are either started directly by the IRQ handler
	 * or deferred if the TT is already in use.
	 */
	FIQ_PER_SSPLIT_QUEUED = 8,
	FIQ_PER_SSPLIT_STARTED = 9,
	FIQ_PER_SSPLIT_LAST = 10,
	
	
	FIQ_PER_ISO_OUT_PENDING = 11,
	FIQ_PER_ISO_OUT_ACTIVE = 12,
	FIQ_PER_ISO_OUT_LAST = 13,
	FIQ_PER_ISO_OUT_DONE = 27,

	FIQ_PER_CSPLIT_WAIT = 14,
	FIQ_PER_CSPLIT_NYET1 = 15,
	FIQ_PER_CSPLIT_BROKEN_NYET1 = 28,
	FIQ_PER_CSPLIT_NYET_FAFF = 29,
	/* For multiple CSPLITs (large isoc IN, or delayed interrupt) */
	FIQ_PER_CSPLIT_POLL = 16,
	/* The last CSPLIT for a transaction has been issued, differentiates
	 * for the state machine to queue the next packet.
	 */
	FIQ_PER_CSPLIT_LAST = 17,

	FIQ_PER_SPLIT_DONE = 18,
	FIQ_PER_SPLIT_LS_ABORTED = 19,
	FIQ_PER_SPLIT_HS_ABORTED = 20,
	FIQ_PER_SPLIT_NYET_ABORTED = 21,
	/* Frame rollover has occurred without the transaction finishing. */
	FIQ_PER_SPLIT_TIMEOUT = 22,
	
	/* FIQ-accelerated HS Isochronous state groups */
	FIQ_HS_ISOC_TURBO = 23,
	/* For interval > 1, SOF wakes up the isochronous FSM */
	FIQ_HS_ISOC_SLEEPING = 24,
	FIQ_HS_ISOC_DONE = 25,
	FIQ_HS_ISOC_ABORTED = 26,
	FIQ_DEQUEUE_ISSUED = 30,
	FIQ_TEST = 32,
};

struct fiq_stack {
	int magic1;
	uint8_t stack[2048];
	int magic2;
};


/**
 * struct fiq_dma_info - DMA bounce buffer utilisation information (per-channel)
 * @index:	Number of slots reported used for IN transactions / number of slots
 *			transmitted for an OUT transaction
 * @slot_len[6]: Number of actual transfer bytes in each slot (255 if unused)
 *
 * Split transaction transfers can have variable length depending on other bus
 * traffic. The OTG core DMA engine requires 4-byte aligned addresses therefore
 * each transaction needs a guaranteed aligned address. A maximum of 6 split transfers
 * can happen per-frame.
 */
struct fiq_dma_info {
	u8 index;
	u8 slot_len[6];
};

struct __attribute__((packed)) fiq_split_dma_slot {
	u8 buf[188];
};

struct fiq_dma_channel {
	struct __attribute__((packed)) fiq_split_dma_slot index[6];
};

struct fiq_dma_blob {
	struct __attribute__((packed)) fiq_dma_channel channel[0];
};

/**
 * struct fiq_hs_isoc_info - USB2.0 isochronous data
 * @iso_frame:	Pointer to the array of OTG URB iso_frame_descs.
 * @nrframes:	Total length of iso_frame_desc array
 * @index:	Current index (FIQ-maintained)
 *
 */
struct fiq_hs_isoc_info {
	struct dwc_otg_hcd_iso_packet_desc *iso_desc;
	unsigned int nrframes;
	unsigned int index;
};

/**
 * struct fiq_channel_state - FIQ state machine storage
 * @fsm:	Current state of the channel as understood by the FIQ
 * @nr_errors:	Number of transaction errors on this split-transaction
 * @hub_addr:   SSPLIT/CSPLIT destination hub
 * @port_addr:  SSPLIT/CSPLIT destination port - always 1 if single TT hub
 * @nrpackets:  For isoc OUT, the number of split-OUT packets to transmit. For
 * 		split-IN, number of CSPLIT data packets that were received.
 * @hcchar_copy:
 * @hcsplt_copy:
 * @hcintmsk_copy:
 * @hctsiz_copy:	Copies of the host channel registers.
 * 			For use as scratch, or for returning state.
 *
 * The fiq_channel_state is state storage between interrupts for a host channel. The
 * FSM state is stored here. Members of this structure must only be set up by the
 * driver prior to enabling the FIQ for this host channel, and not touched until the FIQ
 * has updated the state to either a COMPLETE state group or ABORT state group.
 */

struct fiq_channel_state {
	enum fiq_fsm_state fsm;
	unsigned int nr_errors;
	unsigned int hub_addr;
	unsigned int port_addr;
	/* Hardware bug workaround: sometimes channel halt interrupts are
	 * delayed until the next SOF. Keep track of when we expected to get interrupted. */
	unsigned int expected_uframe; 
	/* in/out for communicating number of dma buffers used, or number of ISOC to do */
	unsigned int nrpackets;
	struct fiq_dma_info dma_info;
	struct fiq_hs_isoc_info hs_isoc_info;
	/* Copies of HC registers - in/out communication from/to IRQ handler
	 * and for ease of channel setup. A bit of mungeing is performed - for
	 * example the hctsiz.b.maxp is _always_ the max packet size of the endpoint. 
	 */
	hcchar_data_t hcchar_copy;
	hcsplt_data_t hcsplt_copy;
	hcint_data_t hcint_copy;
	hcintmsk_data_t hcintmsk_copy;
	hctsiz_data_t hctsiz_copy;
	hcdma_data_t hcdma_copy;
};

/**
 * struct fiq_state - top-level FIQ state machine storage
 * @mphi_regs:		virtual address of the MPHI peripheral register file
 * @dwc_regs_base:	virtual address of the base of the DWC core register file
 * @dma_base:		physical address for the base of the DMA bounce buffers
 * @dummy_send:		Scratch area for sending a fake message to the MPHI peripheral
 * @gintmsk_saved:	Top-level mask of interrupts that the FIQ has not handled.
 * 			Used for determining which interrupts fired to set off the IRQ handler.
 * @haintmsk_saved:	Mask of interrupts from host channels that the FIQ did not handle internally.
 * @np_count:		Non-periodic transactions in the active queue
 * @np_sent:		Count of non-periodic transactions that have completed
 * @next_sched_frame:	For periodic transactions handled by the driver's SOF-driven queuing mechanism,
 * 			this is the next frame on which a SOF interrupt is required. Used to hold off
 * 			passing SOF through to the driver until necessary.
 * @channel[n]:		Per-channel FIQ state. Allocated during init depending on the number of host
 * 			channels configured into the core logic.
 *
 * This is passed as the first argument to the dwc_otg_fiq_fsm top-level FIQ handler from the asm stub.
 * It contains top-level state information.
 */
struct fiq_state {
	mphi_regs_t mphi_regs;
	void *dwc_regs_base;
	dma_addr_t dma_base;
	struct fiq_dma_blob *fiq_dmab;
	void *dummy_send;
	gintmsk_data_t gintmsk_saved;
	haintmsk_data_t haintmsk_saved;
	int mphi_int_count;
	unsigned int fiq_done;
	unsigned int kick_np_queues;
	unsigned int next_sched_frame;
#ifdef FIQ_DEBUG
	char * buffer;
	unsigned int bufsiz;
#endif
	struct fiq_channel_state channel[0];
};

extern int fiq_fsm_too_late(struct fiq_state *st, int n);

extern int fiq_fsm_tt_in_use(struct fiq_state *st, int num_channels, int n);

extern void dwc_otg_fiq_fsm(struct fiq_state *state, int num_channels);

extern void dwc_otg_fiq_nop(struct fiq_state *state);
extern void set_fiq_init(unsigned int fiq,long data);

#endif /* DWC_OTG_FIQ_FSM_H_ */
