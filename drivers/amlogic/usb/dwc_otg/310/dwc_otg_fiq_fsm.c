/*
 * dwc_otg_fiq_fsm.c - The finite state machine FIQ
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
 * The FIQ functionality is glued into the Synopsys driver via the entry point
 * in the FSM enqueue function, and at the exit point in handling a HC interrupt
 * for a FSM-enabled channel.
 *
 * NB: Large parts of this implementation have architecture-specific code.
 * For porting this functionality to other ARM machines, the minimum is required:
 * - An interrupt controller allowing the top-level dwc USB interrupt to be routed
 *   to the FIQ
 * - A method of forcing a software generated interrupt from FIQ mode that then
 *   triggers an IRQ entry (with the dwc USB handler called by this IRQ number)
 * - Guaranteed interrupt routing such that both the FIQ and SGI occur on the same
 *   processor core - there is no locking between the FIQ and IRQ (aside from
 *   local_fiq_disable)
 *
 */

#include "dwc_otg_fiq_fsm.h"
#include "dwc_otg_driver.h"
#include "mach/register.h"
#include <linux/io.h>
#include <mach/am_regs.h>
#include <asm/fiq.h>

char buffer[1000*16];
int wptr;
void notrace _fiq_print(enum fiq_debug_level dbg_lvl, volatile struct fiq_state *state, char *fmt, ...)
{
	enum fiq_debug_level dbg_lvl_req = FIQDBG_ERR;
	va_list args;
	char text[17];
	hfnum_data_t hfnum = { .d32 = FIQ_READ(state->dwc_regs_base + 0x408) };

	if((dbg_lvl & dbg_lvl_req) || dbg_lvl == FIQDBG_ERR)
	{
		snprintf(text, 9, " %4d:%1u  ", hfnum.b.frnum/8, hfnum.b.frnum & 7);
		va_start(args, fmt);
		vsnprintf(text+8, 9, fmt, args);
		va_end(args);

		memcpy(buffer + wptr, text, 16);
		wptr = (wptr + 16) % sizeof(buffer);
	}
}

/**
 * fiq_fsm_restart_channel() - Poke channel enable bit for a split transaction
 * @channel: channel to re-enable
 */
static void fiq_fsm_restart_channel(struct fiq_state *st, int n, int force)
{
	hcchar_data_t hcchar = { .d32 = FIQ_READ(st->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCCHAR) };
	
	hcchar.b.chen = 0;
	if (st->channel[n].hcchar_copy.b.eptype & 0x1) {
		hfnum_data_t hfnum = { .d32 = FIQ_READ(st->dwc_regs_base + HFNUM) };
		/* Hardware bug workaround: update the ssplit index */
		if (st->channel[n].hcsplt_copy.b.spltena)
			st->channel[n].expected_uframe = (hfnum.b.frnum + 1) & 0x3FFF;
		
		hcchar.b.oddfrm = (hfnum.b.frnum & 0x1) ? 0	: 1;
	}
	
	FIQ_WRITE(st->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCCHAR, hcchar.d32);
	hcchar.d32 = FIQ_READ(st->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCCHAR);
	hcchar.b.chen = 1;

	FIQ_WRITE(st->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCCHAR, hcchar.d32);
	fiq_print(FIQDBG_INT, st, "HCGO %01d %01d", n, force);
}

/**
 * fiq_fsm_setup_csplit() - Prepare a host channel for a CSplit transaction stage
 * @st: Pointer to the channel's state
 * @n : channel number
 *
 * Change host channel registers to perform a complete-split transaction. Being mindful of the
 * endpoint direction, set control regs up correctly.
 */
static void notrace fiq_fsm_setup_csplit(struct fiq_state *st, int n)
{
	hcsplt_data_t hcsplt = { .d32 = FIQ_READ(st->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCSPLT) };
	hctsiz_data_t hctsiz = { .d32 = FIQ_READ(st->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCTSIZ) };
	
	hcsplt.b.compsplt = 1;
	if (st->channel[n].hcchar_copy.b.epdir == 1) {
		// If IN, the CSPLIT result contains the data or a hub handshake. hctsiz = maxpacket.
		hctsiz.b.xfersize = st->channel[n].hctsiz_copy.b.xfersize;
	} else {
		// If OUT, the CSPLIT result contains handshake only.
		hctsiz.b.xfersize = 0;
	}
	FIQ_WRITE(st->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCSPLT, hcsplt.d32);
	FIQ_WRITE(st->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCTSIZ, hctsiz.d32);
	//mb();
}

static inline int notrace fiq_get_xfer_len(struct fiq_state *st, int n)
{
	/* The xfersize register is a bit wonky. For IN transfers, it decrements by the packet size. */
	hctsiz_data_t hctsiz = { .d32 = FIQ_READ(st->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCTSIZ) };
	
	if (st->channel[n].hcchar_copy.b.epdir == 0) {
		return st->channel[n].hctsiz_copy.b.xfersize;
	} else {
		return st->channel[n].hctsiz_copy.b.xfersize - hctsiz.b.xfersize;
	}

}


/**
 * fiq_increment_dma_buf() - update DMA address for bounce buffers after a CSPLIT
 *
 * Of use only for IN periodic transfers.
 */
static int notrace fiq_increment_dma_buf(struct fiq_state *st, int num_channels, int n)
{
	hcdma_data_t hcdma;
	int i = st->channel[n].dma_info.index;
	int len;
	struct fiq_dma_blob *blob = (struct fiq_dma_blob *) st->dma_base;

	len = fiq_get_xfer_len(st, n);
	fiq_print(FIQDBG_INT, st, "LEN: %03d", len);
	st->channel[n].dma_info.slot_len[i] = len;
	i++;
	if (i > 6)
		BUG();

	hcdma.d32 = (dma_addr_t) &blob->channel[n].index[i].buf[0];
	FIQ_WRITE(st->dwc_regs_base + HC_DMA + (HC_OFFSET * n), hcdma.d32);
	st->channel[n].dma_info.index = i;
	return 0;
}

/**
 * fiq_reload_hctsiz() - for IN transactions, reset HCTSIZ
 */
static void notrace fiq_fsm_reload_hctsiz(struct fiq_state *st, int n)
{
	hctsiz_data_t hctsiz = { .d32 = FIQ_READ(st->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCTSIZ) };
	hctsiz.b.xfersize = st->channel[n].hctsiz_copy.b.xfersize;
	hctsiz.b.pktcnt = 1;
	FIQ_WRITE(st->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCTSIZ, hctsiz.d32);
}

/**
 * fiq_iso_out_advance() - update DMA address and split position bits
 * for isochronous OUT transactions.
 *
 * Returns 1 if this is the last packet queued, 0 otherwise. Split-ALL and
 * Split-BEGIN states are not handled - this is done when the transaction was queued.
 *
 * This function must only be called from the FIQ_ISO_OUT_ACTIVE state.
 */
static int notrace fiq_iso_out_advance(struct fiq_state *st, int num_channels, int n)
{
	hcsplt_data_t hcsplt;
	hctsiz_data_t hctsiz;
	hcdma_data_t hcdma;
	struct fiq_dma_blob *blob = (struct fiq_dma_blob *) st->dma_base;
	int last = 0;
	int i = st->channel[n].dma_info.index;
	
	fiq_print(FIQDBG_INT, st, "ADV %01d %01d ", n, i);
	i++;
	if (i == 4)
		last = 1;
	if (st->channel[n].dma_info.slot_len[i+1] == 255)
		last = 1;

	/* New DMA address - address of bounce buffer referred to in index */
	hcdma.d32 = (uint32_t) &blob->channel[n].index[i].buf[0];
	//hcdma.d32 = FIQ_READ(st->dwc_regs_base + HC_DMA + (HC_OFFSET * n));
	//hcdma.d32 += st->channel[n].dma_info.slot_len[i];
	fiq_print(FIQDBG_INT, st, "LAST: %01d ", last);
	fiq_print(FIQDBG_INT, st, "LEN: %03d", st->channel[n].dma_info.slot_len[i]);
	hcsplt.d32 = FIQ_READ(st->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCSPLT);
	hctsiz.d32 = FIQ_READ(st->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCTSIZ);
	hcsplt.b.xactpos = (last) ? ISOC_XACTPOS_END : ISOC_XACTPOS_MID;
	/* Set up new packet length */
	hctsiz.b.pktcnt = 1;
	hctsiz.b.xfersize = st->channel[n].dma_info.slot_len[i];
	fiq_print(FIQDBG_INT, st, "%08x", hctsiz.d32);
	
	st->channel[n].dma_info.index++;
	FIQ_WRITE(st->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCSPLT, hcsplt.d32);
	FIQ_WRITE(st->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCTSIZ, hctsiz.d32);
	FIQ_WRITE(st->dwc_regs_base + HC_DMA + (HC_OFFSET * n), hcdma.d32);
	return last;
}

/**
 * fiq_fsm_tt_next_isoc() - queue next pending isochronous out start-split on a TT
 * 
 * Despite the limitations of the DWC core, we can force a microframe pipeline of
 * isochronous OUT start-split transactions while waiting for a corresponding other-type
 * of endpoint to finish its CSPLITs. TTs have big periodic buffers therefore it
 * is very unlikely that filling the start-split FIFO will cause data loss.
 * This allows much better interleaving of transactions in an order-independent way-
 * there is no requirement to prioritise isochronous, just a state-space search has
 * to be performed on each periodic start-split complete interrupt.
 */
static int notrace fiq_fsm_tt_next_isoc(struct fiq_state *st, int num_channels, int n)
{
	int hub_addr = st->channel[n].hub_addr;
	int port_addr = st->channel[n].port_addr;
	int i, poked = 0;
	for (i = 0; i < num_channels; i++) {
		if (i == n || st->channel[i].fsm == FIQ_PASSTHROUGH)
			continue;
		if (st->channel[i].hub_addr == hub_addr &&
			st->channel[i].port_addr == port_addr) {
			switch (st->channel[i].fsm) {
			case FIQ_PER_ISO_OUT_PENDING:
				if (st->channel[i].nrpackets == 1) {
					st->channel[i].fsm = FIQ_PER_ISO_OUT_LAST;
				} else {
					st->channel[i].fsm = FIQ_PER_ISO_OUT_ACTIVE;
				}
				fiq_fsm_restart_channel(st, i, 0);
				poked = 1;
				break;

			default:
				break;
			}
		}
		if (poked)
			break;
	}
	return poked;
}

/**
 * fiq_fsm_tt_in_use() - search for host channels using this TT
 * @n: Channel to use as reference
 *
 */
int notrace noinline fiq_fsm_tt_in_use(struct fiq_state *st, int num_channels, int n)
{
	int hub_addr = st->channel[n].hub_addr;
	int port_addr = st->channel[n].port_addr;
	int i, in_use = 0;
	for (i = 0; i < num_channels; i++) {
		if (i == n || st->channel[i].fsm == FIQ_PASSTHROUGH)
			continue;
		switch (st->channel[i].fsm) {
		/* TT is reserved for channels that are in the middle of a periodic
		 * split transaction.
		 */
		case FIQ_PER_SSPLIT_STARTED:
		case FIQ_PER_CSPLIT_WAIT:
		case FIQ_PER_CSPLIT_NYET1:
		//case FIQ_PER_CSPLIT_POLL:
		case FIQ_PER_ISO_OUT_ACTIVE:
		case FIQ_PER_ISO_OUT_LAST:
			if (st->channel[i].hub_addr == hub_addr &&
				st->channel[i].port_addr == port_addr) {
				in_use = 1;
			}
			break;
		default:
			break;
		}
		if (in_use)
			break;
	}
	return in_use;
}

/**
 * fiq_fsm_more_csplits() - determine whether additional CSPLITs need
 * 			to be issued for this IN transaction.
 *
 * We cannot tell the inbound PID of a data packet due to hardware limitations.
 * we need to make an educated guess as to whether we need to queue another CSPLIT
 * or not. A no-brainer is when we have received enough data to fill the endpoint
 * size, but for endpoints that give variable-length data then we have to resort
 * to heuristics.
 *
 * We also return whether this is the last CSPLIT to be queued, again based on
 * heuristics. This is to allow a 1-uframe overlap of periodic split transactions.
 * Note: requires at least 1 CSPLIT to have been performed prior to being called.
 */

/*
 * We need some way of guaranteeing if a returned periodic packet of size X
 * has a DATA0 PID.
 * The heuristic value of 144 bytes assumes that the received data has maximal
 * bit-stuffing and the clock frequency of the transmitting device is at the lowest
 * permissible limit. If the transfer length results in a final packet size
 * 144 < p <= 188, then an erroneous CSPLIT will be issued.
 * Also used to ensure that an endpoint will nominally only return a single
 * complete-split worth of data.
 */
#define DATA0_PID_HEURISTIC 144

static int notrace noinline fiq_fsm_more_csplits(struct fiq_state *state, int n, int *probably_last)
{

	int i;
	int total_len = 0;
	int more_needed = 1;
	struct fiq_channel_state *st = &state->channel[n];

	for (i = 0; i < st->dma_info.index; i++) {
			total_len += st->dma_info.slot_len[i];
	}

	*probably_last = 0;

	if (st->hcchar_copy.b.eptype == 0x3) {
		/*
		 * An interrupt endpoint will take max 2 CSPLITs. if we are receiving data
		 * then this is definitely the last CSPLIT.
		 */
		*probably_last = 1;
	} else {
		/* Isoc IN. This is a bit risky if we are the first transaction:
		 * we may have been held off slightly. */
		if (i > 1 && st->dma_info.slot_len[st->dma_info.index-1] <= DATA0_PID_HEURISTIC) {
			more_needed = 0;
		}
		/* If in the next uframe we will receive enough data to fill the endpoint,
		 * then only issue 1 more csplit.
		 */
		if (st->hctsiz_copy.b.xfersize - total_len <= DATA0_PID_HEURISTIC)
			*probably_last = 1;
	}

	if (total_len >= st->hctsiz_copy.b.xfersize || 
		i == 6 || total_len == 0)
		/* Note: due to bit stuffing it is possible to have > 6 CSPLITs for
		 * a single endpoint. Accepting more would completely break our scheduling mechanism though
		 * - in these extreme cases we will pass through a truncated packet.
		 */
		more_needed = 0;	
	
	return more_needed;
}

/**
 * fiq_fsm_too_late() - Test transaction for lateness
 * 
 * If a SSPLIT for a large IN transaction is issued too late in a frame,
 * the hub will disable the port to the device and respond with ERR handshakes.
 * The hub status endpoint will not reflect this change.
 * Returns 1 if we will issue a SSPLIT that will result in a device babble.
 */
int notrace fiq_fsm_too_late(struct fiq_state *st, int n)
{
	int uframe;
	hfnum_data_t hfnum = { .d32 = FIQ_READ(st->dwc_regs_base + HFNUM) };
	uframe = hfnum.b.frnum & 0x7;
	if ((uframe < 6) && (st->channel[n].nrpackets + 1 + uframe > 7)) {
		return 1;
	} else {
		return 0;
	}
}


/**
 * fiq_fsm_start_next_periodic() - A half-arsed attempt at a microframe pipeline
 *
 * Search pending transactions in the start-split pending state and queue them.
 * Don't queue packets in uframe .5 (comes out in .6) (USB2.0 11.18.4).
 * Note: we specifically don't do isochronous OUT transactions first because better
 * use of the TT's start-split fifo can be achieved by pipelining an IN before an OUT.
 */
static void notrace noinline fiq_fsm_start_next_periodic(struct fiq_state *st, int num_channels)
{
	int n;
	hfnum_data_t hfnum = { .d32 = FIQ_READ(st->dwc_regs_base + HFNUM) };
	if ((hfnum.b.frnum & 0x7) == 5)
		return;
	for (n = 0; n < num_channels; n++) {
		if (st->channel[n].fsm == FIQ_PER_SSPLIT_QUEUED) {
			/* Check to see if any other transactions are using this TT */
			if(!fiq_fsm_tt_in_use(st, num_channels, n)) {
				if (!fiq_fsm_too_late(st, n)) {
					st->channel[n].fsm = FIQ_PER_SSPLIT_STARTED;
					fiq_print(FIQDBG_INT, st, "NEXTPER ");
					fiq_fsm_restart_channel(st, n, 0);
				} else {
					st->channel[n].fsm = FIQ_PER_SPLIT_TIMEOUT;
				}
				break;
			}
		}
	}
	for (n = 0; n < num_channels; n++) {
		if (st->channel[n].fsm == FIQ_PER_ISO_OUT_PENDING) {
			if (!fiq_fsm_tt_in_use(st, num_channels, n)) {
				fiq_print(FIQDBG_INT, st, "NEXTISO ");	
				st->channel[n].fsm = FIQ_PER_ISO_OUT_ACTIVE;
				fiq_fsm_restart_channel(st, n, 0);
				break;
			}
		}
	}
}

/**
 * fiq_fsm_update_hs_isoc() - update isochronous frame and transfer data
 * @state:	Pointer to fiq_state
 * @n:		Channel transaction is active on
 * @hcint:	Copy of host channel interrupt register
 *
 * Returns 0 if there are no more transactions for this HC to do, 1
 * otherwise.
 */
static int notrace noinline fiq_fsm_update_hs_isoc(struct fiq_state *state, int n, hcint_data_t hcint)
{
	struct fiq_channel_state *st = &state->channel[n];
	int xfer_len = 0, nrpackets = 0;
	hcdma_data_t hcdma;
	fiq_print(FIQDBG_INT, state, "HSISO %02d", n);

	xfer_len = fiq_get_xfer_len(state, n);
	st->hs_isoc_info.iso_desc[st->hs_isoc_info.index].actual_length = xfer_len;

	st->hs_isoc_info.iso_desc[st->hs_isoc_info.index].status = hcint.d32;

	st->hs_isoc_info.index++;
	if (st->hs_isoc_info.index == st->hs_isoc_info.nrframes) {
		return 0;
	}

	/* grab the next DMA address offset from the array */
	hcdma.d32 = st->hcdma_copy.d32 + st->hs_isoc_info.iso_desc[st->hs_isoc_info.index].offset;
	FIQ_WRITE(state->dwc_regs_base + HC_DMA + (HC_OFFSET * n), hcdma.d32);

	/* We need to set multi_count. This is a bit tricky - has to be set per-transaction as
	 * the core needs to be told to send the correct number. Caution: for IN transfers,
	 * this is always set to the maximum size of the endpoint. */
	xfer_len = st->hs_isoc_info.iso_desc[st->hs_isoc_info.index].length;
	/* Integer divide in a FIQ: fun. FIXME: make this not suck */
	nrpackets = (xfer_len + st->hcchar_copy.b.mps - 1) / st->hcchar_copy.b.mps;
	if (nrpackets == 0)
		nrpackets = 1;
	st->hcchar_copy.b.multicnt = nrpackets;
	st->hctsiz_copy.b.pktcnt = nrpackets;

	/* Initial PID also needs to be set */
	if (st->hcchar_copy.b.epdir == 0) {
		st->hctsiz_copy.b.xfersize = xfer_len;
		switch (st->hcchar_copy.b.multicnt) {
		case 1:
			st->hctsiz_copy.b.pid = DWC_PID_DATA0;
			break;
		case 2:
		case 3:
			st->hctsiz_copy.b.pid = DWC_PID_MDATA;
			break;
		}

	} else {
		switch (st->hcchar_copy.b.multicnt) {
		st->hctsiz_copy.b.xfersize = nrpackets * st->hcchar_copy.b.mps;
		case 1:
			st->hctsiz_copy.b.pid = DWC_PID_DATA0;
			break;
		case 2:
			st->hctsiz_copy.b.pid = DWC_PID_DATA1;
			break;
		case 3:
			st->hctsiz_copy.b.pid = DWC_PID_DATA2;
			break;
		}
	}
	FIQ_WRITE(state->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCTSIZ, st->hctsiz_copy.d32);
	FIQ_WRITE(state->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCCHAR, st->hcchar_copy.d32);
	/* Channel is enabled on hcint handler exit */
	fiq_print(FIQDBG_INT, state, "HSISOOUT");
	return 1;
}

/**
 * fiq_fsm_do_sof() - FSM start-of-frame interrupt handler
 * @state:	Pointer to the state struct passed from banked FIQ mode registers.
 * @num_channels:	set according to the DWC hardware configuration
 *
 * The SOF handler in FSM mode has two functions
 * 1. Hold off SOF from causing schedule advancement in IRQ context if there's
 *    nothing to do
 * 2. Advance certain FSM states that require either a microframe delay, or a microframe
 *    of holdoff.
 *
 * The second part is architecture-specific to mach-bcm2835 -
 * a sane interrupt controller would have a mask register for ARM interrupt sources
 * to be promoted to the nFIQ line, but it doesn't. Instead a single interrupt
 * number (USB) can be enabled. This means that certain parts of the USB specification
 * that require "wait a little while, then issue another packet" cannot be fulfilled with
 * the timing granularity required to achieve optimal throughout. The workaround is to use
 * the SOF "timer" (125uS) to perform this task.
 */
static int notrace noinline fiq_fsm_do_sof(struct fiq_state *state, int num_channels)
{
	hfnum_data_t hfnum = { .d32 = FIQ_READ(state->dwc_regs_base + HFNUM) };
	int n;
	int kick_irq = 0;
	   
	if ((hfnum.b.frnum & 0x7) == 1) {
		/* We cannot issue csplits for transactions in the last frame past (n+1).1
		 * Check to see if there are any transactions that are stale.
		 * Boot them out.
		 */
		for (n = 0; n < num_channels; n++) {
			switch (state->channel[n].fsm) {
			case FIQ_PER_CSPLIT_WAIT:
			case FIQ_PER_CSPLIT_NYET1:
			case FIQ_PER_CSPLIT_POLL:
			case FIQ_PER_CSPLIT_LAST:
				/* Check if we are no longer in the same full-speed frame. */				
				if (((state->channel[n].expected_uframe & 0x3FFF) & ~0x7) <
						(hfnum.b.frnum & ~0x7))
					state->channel[n].fsm = FIQ_PER_SPLIT_TIMEOUT;
				break;
			default:
				break;
			}
		}
	}

	for (n = 0; n < num_channels; n++) {
		switch (state->channel[n].fsm) {
		
		case FIQ_NP_SSPLIT_RETRY:
		case FIQ_NP_IN_CSPLIT_RETRY:
		case FIQ_NP_OUT_CSPLIT_RETRY:
			fiq_fsm_restart_channel(state, n, 0);
			break;
			
		case FIQ_HS_ISOC_SLEEPING:
			state->channel[n].fsm = FIQ_HS_ISOC_TURBO;
			fiq_fsm_restart_channel(state, n, 0);
			break;
			
		case FIQ_PER_SSPLIT_QUEUED:
			if ((hfnum.b.frnum & 0x7) == 5)
				break;
			if(!fiq_fsm_tt_in_use(state, num_channels, n)) {
				if (!fiq_fsm_too_late(state, n)) {
					//fiq_print(FIQDBG_INT, st, "SOF GO %01d", n);
					fiq_fsm_restart_channel(state, n, 0);
					state->channel[n].fsm = FIQ_PER_SSPLIT_STARTED;
				} else {
					/* Transaction cannot be started without risking a device babble error */
					state->channel[n].fsm = FIQ_PER_SPLIT_TIMEOUT;
					state->haintmsk_saved.b2.chint &= ~(1 << n);
					FIQ_WRITE(state->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCINTMSK, 0);
					kick_irq |= 1;
				}
			}
			break;
			
		case FIQ_PER_ISO_OUT_PENDING:
			/* Ordinarily, this should be poked after the SSPLIT
			 * complete interrupt for a competing transfer on the same
			 * TT. Doesn't happen for aborted transactions though.
			 */
			if ((hfnum.b.frnum & 0x7) >= 5)
				break;
			if (!fiq_fsm_tt_in_use(state, num_channels, n)) {
				/* Hardware bug. SOF can sometimes occur after the channel halt interrupt
				 * that caused this.
				 */
					fiq_fsm_restart_channel(state, n, 0);
					fiq_print(FIQDBG_INT, state, "SOF ISOC");
					if (state->channel[n].nrpackets == 1) {
						state->channel[n].fsm = FIQ_PER_ISO_OUT_LAST;
					} else {
						state->channel[n].fsm = FIQ_PER_ISO_OUT_ACTIVE;
					}
			}
			break;

		case FIQ_PER_CSPLIT_WAIT:
			/* we are guaranteed to be in this state if and only if the SSPLIT interrupt
			 * occurred when the bus transaction occurred. The SOF interrupt reversal bug
			 * will utterly bugger this up though.
			 */
			if (hfnum.b.frnum != state->channel[n].expected_uframe) {
				fiq_print(FIQDBG_INT, state, "SOFCS %d ", n);
				state->channel[n].fsm = FIQ_PER_CSPLIT_POLL;
				fiq_fsm_restart_channel(state, n, 0);
				fiq_fsm_start_next_periodic(state, num_channels);
				
			}
			break;
		
		case FIQ_PER_SPLIT_TIMEOUT:
		case FIQ_DEQUEUE_ISSUED:
			/* Ugly: we have to force a HCD interrupt.
			 * Poke the mask for the channel in question.
			 * We will take a fake SOF because of this, but
			 * that's OK.
			 */
			state->haintmsk_saved.b2.chint &= ~(1 << n);
			FIQ_WRITE(state->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCINTMSK, 0);
			kick_irq |= 1;
			break;
		
		default:
			break;
		}
	}

	if (state->kick_np_queues ||
			dwc_frame_num_le(state->next_sched_frame, hfnum.b.frnum))
		kick_irq |= 1;

	return !kick_irq;
}


/**
 * fiq_fsm_do_hcintr() - FSM host channel interrupt handler
 * @state: Pointer to the FIQ state struct
 * @num_channels: Number of channels as per hardware config
 * @n: channel for which HAINT(i) was raised
 * 
 * An important property is that only the CHHLT interrupt is unmasked. Unfortunately, AHBerr is as well.
 */
static int notrace noinline fiq_fsm_do_hcintr(struct fiq_state *state, int num_channels, int n)
{
	hcint_data_t hcint;
	hcintmsk_data_t hcintmsk;
	hcint_data_t hcint_probe;
	hcchar_data_t hcchar;
	int handled = 0;
	int restart = 0;
	int last_csplit = 0;
	int start_next_periodic = 0;
	struct fiq_channel_state *st = &state->channel[n];
	hfnum_data_t hfnum;

	hcint.d32 = FIQ_READ(state->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCINT);
	hcintmsk.d32 = FIQ_READ(state->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCINTMSK);
	hcint_probe.d32 = hcint.d32 & hcintmsk.d32;

	if (st->fsm != FIQ_PASSTHROUGH) {
		fiq_print(FIQDBG_INT, state, "HC%01d ST%02d", n, st->fsm);
		fiq_print(FIQDBG_INT, state, "%08x", hcint.d32);
	}

	switch (st->fsm) {
	
	case FIQ_PASSTHROUGH:
	case FIQ_DEQUEUE_ISSUED:
		/* doesn't belong to us, kick it upstairs */
		break;

	case FIQ_PASSTHROUGH_ERRORSTATE:
		/* We are here to emulate the error recovery mechanism of the dwc HCD.
		 * Several interrupts are unmasked if a previous transaction failed - it's
		 * death for the FIQ to attempt to handle them as the channel isn't halted.
		 * Emulate what the HCD does in this situation: mask and continue.
		 * The FSM has no other state setup so this has to be handled out-of-band.
		 */
		fiq_print(FIQDBG_ERR, state, "ERRST %02d", n);
		if (hcint_probe.b.nak || hcint_probe.b.ack || hcint_probe.b.datatglerr) {
			fiq_print(FIQDBG_ERR, state, "RESET %02d", n);
			/* In some random cases we can get a NAK interrupt coincident with a Xacterr
			 * interrupt, after the device has disappeared.
			 */
			if (!hcint.b.xacterr)
				st->nr_errors = 0;
			hcintmsk.b.nak = 0;
			hcintmsk.b.ack = 0;
			hcintmsk.b.datatglerr = 0;
			FIQ_WRITE(state->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCINTMSK, hcintmsk.d32);
			return 1;
		}
		if (hcint_probe.b.chhltd) {
			fiq_print(FIQDBG_ERR, state, "CHHLT %02d", n);
			fiq_print(FIQDBG_ERR, state, "%08x", hcint.d32);
			return 0;
		}
		break;

	/* Non-periodic state groups */
	case FIQ_NP_SSPLIT_STARTED:
	case FIQ_NP_SSPLIT_RETRY:
		/* Got a HCINT for a NP SSPLIT. Expected ACK / NAK / fail */
		if (hcint.b.ack) {
			/* SSPLIT complete. For OUT, the data has been sent. For IN, the LS transaction
			 * will start shortly. SOF needs to kick the transaction to prevent a NYET flood. 
			 */
			if(st->hcchar_copy.b.epdir == 1)
				st->fsm = FIQ_NP_IN_CSPLIT_RETRY;
			else
				st->fsm = FIQ_NP_OUT_CSPLIT_RETRY;
			st->nr_errors = 0;
			handled = 1;
			fiq_fsm_setup_csplit(state, n);
		} else if (hcint.b.nak) {
			// No buffer space in TT. Retry on a uframe boundary.
			st->fsm = FIQ_NP_SSPLIT_RETRY;
			handled = 1;
		} else if (hcint.b.xacterr) {
			// The only other one we care about is xacterr. This implies HS bus error - retry.
			st->nr_errors++;
			st->fsm = FIQ_NP_SSPLIT_RETRY;
			if (st->nr_errors >= 3) {
				st->fsm = FIQ_NP_SPLIT_HS_ABORTED;
			} else {
				handled = 1;
				restart = 1;
			}
		} else {
			st->fsm = FIQ_NP_SPLIT_LS_ABORTED;
			handled = 0;
			restart = 0;
		}
		break;
		
	case FIQ_NP_IN_CSPLIT_RETRY:
		/* Received a CSPLIT done interrupt.
		 * Expected Data/NAK/STALL/NYET for IN. 
		 */
		if (hcint.b.xfercomp) {
			/* For IN, data is present. */
			st->fsm = FIQ_NP_SPLIT_DONE;
		} else if (hcint.b.nak) {
			/* no endpoint data. Punt it upstairs */
			st->fsm = FIQ_NP_SPLIT_DONE;
		} else if (hcint.b.nyet) {
			/* CSPLIT NYET - retry on a uframe boundary. */
			handled = 1;
			st->nr_errors = 0;
		} else if (hcint.b.datatglerr) {
			/* data toggle errors do not set the xfercomp bit. */
			st->fsm = FIQ_NP_SPLIT_LS_ABORTED;
		} else if (hcint.b.xacterr) {
			/* HS error. Retry immediate */
			st->fsm = FIQ_NP_IN_CSPLIT_RETRY;
			st->nr_errors++;
			if (st->nr_errors >= 3) {
				st->fsm = FIQ_NP_SPLIT_HS_ABORTED;
			} else {
				handled = 1;
				restart = 1;
			}
		} else if (hcint.b.stall || hcint.b.bblerr) {
			/* A STALL implies either a LS bus error or a genuine STALL. */
			st->fsm = FIQ_NP_SPLIT_LS_ABORTED;
		} else {
			/*  Hardware bug. It's possible in some cases to
			 *  get a channel halt with nothing else set when
			 *  the response was a NYET. Treat as local 3-strikes retry. 
			 */
			hcint_data_t hcint_test = hcint;
			hcint_test.b.chhltd = 0;
			if (!hcint_test.d32) {
				st->nr_errors++;
				if (st->nr_errors >= 3) {
					st->fsm = FIQ_NP_SPLIT_HS_ABORTED;
				} else {
					handled = 1;
				}
			} else {
				/* Bail out if something unexpected happened */
				st->fsm = FIQ_NP_SPLIT_HS_ABORTED;
			}
		}
		break;
	
	case FIQ_NP_OUT_CSPLIT_RETRY:
		/* Received a CSPLIT done interrupt.
		 * Expected ACK/NAK/STALL/NYET/XFERCOMP for OUT.*/
		if (hcint.b.xfercomp) {
			st->fsm = FIQ_NP_SPLIT_DONE;
		} else if (hcint.b.nak) {
			// The HCD will implement the holdoff on frame boundaries.
			st->fsm = FIQ_NP_SPLIT_DONE;
		} else if (hcint.b.nyet) {
			// Hub still processing.
			st->fsm = FIQ_NP_OUT_CSPLIT_RETRY;
			handled = 1;
			st->nr_errors = 0;
			//restart = 1;
		} else if (hcint.b.xacterr) { 
			/* HS error. retry immediate */
			st->fsm = FIQ_NP_OUT_CSPLIT_RETRY;
			st->nr_errors++;
			if (st->nr_errors >= 3) {
				st->fsm = FIQ_NP_SPLIT_HS_ABORTED;
			} else {
				handled = 1;
				restart = 1;
			}
		} else if (hcint.b.stall) { 
			/* LS bus error or genuine stall */
			st->fsm = FIQ_NP_SPLIT_LS_ABORTED;
		} else {
			/* 
			 * Hardware bug. It's possible in some cases to get a
			 * channel halt with nothing else set when the response was a NYET.
			 * Treat as local 3-strikes retry.
			 */
			hcint_data_t hcint_test = hcint;
			hcint_test.b.chhltd = 0;
			if (!hcint_test.d32) {
				st->nr_errors++;
				if (st->nr_errors >= 3) {
					st->fsm = FIQ_NP_SPLIT_HS_ABORTED;
				} else {
					handled = 1;
				}
			} else {
				// Something unexpected happened. AHBerror or babble perhaps. Let the IRQ deal with it.
				st->fsm = FIQ_NP_SPLIT_HS_ABORTED;
			}
		}
		break;
	
	/* Periodic split states (except isoc out) */
	case FIQ_PER_SSPLIT_STARTED:
		/* Expect an ACK or failure for SSPLIT */
		if (hcint.b.ack) {
			/*
			 * SSPLIT transfer complete interrupt - the generation of this interrupt is fraught with bugs.
			 * For a packet queued in microframe n-3 to appear in n-2, if the channel is enabled near the EOF1
			 * point for microframe n-3, the packet will not appear on the bus until microframe n.
			 * Additionally, the generation of the actual interrupt is dodgy. For a packet appearing on the bus
			 * in microframe n, sometimes the interrupt is generated immediately. Sometimes, it appears in n+1
			 * coincident with SOF for n+1.
			 * SOF is also buggy. It can sometimes be raised AFTER the first bus transaction has taken place.
			 * These appear to be caused by timing/clock crossing bugs within the core itself.
			 * State machine workaround.
			 */
			hfnum.d32 = FIQ_READ(state->dwc_regs_base + HFNUM);
			hcchar.d32 = FIQ_READ(state->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCCHAR);
			fiq_fsm_setup_csplit(state, n);
			/* Poke the oddfrm bit. If we are equivalent, we received the interrupt at the correct
			 * time. If not, then we're in the next SOF.
			 */
			if ((hfnum.b.frnum & 0x1) == hcchar.b.oddfrm) {
				fiq_print(FIQDBG_INT, state, "CSWAIT %01d", n);
				st->expected_uframe = hfnum.b.frnum;
				st->fsm = FIQ_PER_CSPLIT_WAIT;
			} else {
				fiq_print(FIQDBG_INT, state, "CSPOL  %01d", n);
				/* For isochronous IN endpoints,
				 * we need to hold off if we are expecting a lot of data */
				if (st->hcchar_copy.b.mps < DATA0_PID_HEURISTIC) {
					start_next_periodic = 1;
				}
				/* Danger will robinson: we are in a broken state. If our first interrupt after
				 * this is a NYET, it will be delayed by 1 uframe and result in an unrecoverable
				 * lag. Unmask the NYET interrupt. 
				 */
				st->expected_uframe = (hfnum.b.frnum + 1) & 0x3FFF;
				st->fsm = FIQ_PER_CSPLIT_BROKEN_NYET1;
				restart = 1;
			}
			handled = 1;
		} else if (hcint.b.xacterr) {
			/* 3-strikes retry is enabled, we have hit our max nr_errors */
			st->fsm = FIQ_PER_SPLIT_HS_ABORTED;
			start_next_periodic = 1;
		} else {
			st->fsm = FIQ_PER_SPLIT_HS_ABORTED;
			start_next_periodic = 1;
		}
		/* We can now queue the next isochronous OUT transaction, if one is pending. */
		if(fiq_fsm_tt_next_isoc(state, num_channels, n)) {
			fiq_print(FIQDBG_INT, state, "NEXTISO ");
		}
		break;
		
	case FIQ_PER_CSPLIT_NYET1:
		/* First CSPLIT attempt was a NYET. If we get a subsequent NYET,
		 * we are too late and the TT has dropped its CSPLIT fifo.
		 */
		hfnum.d32 = FIQ_READ(state->dwc_regs_base + HFNUM);
		hcchar.d32 = FIQ_READ(state->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCCHAR);
		start_next_periodic = 1;
		if (hcint.b.nak) {
			st->fsm = FIQ_PER_SPLIT_DONE;
		} else if (hcint.b.xfercomp) {
			fiq_increment_dma_buf(state, num_channels, n);
			st->fsm = FIQ_PER_CSPLIT_POLL;
			st->nr_errors = 0;
			if (fiq_fsm_more_csplits(state, n, &last_csplit)) {
				handled = 1;
				restart = 1;
				if (!last_csplit)
					start_next_periodic = 0;
			} else {
				st->fsm = FIQ_PER_SPLIT_DONE;
			}
		} else if (hcint.b.nyet) {
			/* Doh. Data lost. */
			st->fsm = FIQ_PER_SPLIT_NYET_ABORTED;
		} else if (hcint.b.xacterr || hcint.b.stall || hcint.b.bblerr) {
			st->fsm = FIQ_PER_SPLIT_LS_ABORTED;
		} else {
			st->fsm = FIQ_PER_SPLIT_HS_ABORTED;
		}
		break;
		
	case FIQ_PER_CSPLIT_BROKEN_NYET1:
		/* 
		 * we got here because our host channel is in the delayed-interrupt
		 * state and we cannot take a NYET interrupt any later than when it
		 * occurred. Disable then re-enable the channel if this happens to force
		 * CSPLITs to occur at the right time. 
		 */
		hfnum.d32 = FIQ_READ(state->dwc_regs_base + HFNUM);
		hcchar.d32 = FIQ_READ(state->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCCHAR);
		fiq_print(FIQDBG_INT, state, "BROK: %01d ", n);
		if (hcint.b.nak) {
			st->fsm = FIQ_PER_SPLIT_DONE;
			start_next_periodic = 1;
		} else if (hcint.b.xfercomp) {
			fiq_increment_dma_buf(state, num_channels, n);
			if (fiq_fsm_more_csplits(state, n, &last_csplit)) {
				st->fsm = FIQ_PER_CSPLIT_POLL;
				handled = 1;
				restart = 1;
				start_next_periodic = 1;
				/* Reload HCTSIZ for the next transfer */
				fiq_fsm_reload_hctsiz(state, n);
				if (!last_csplit)
					start_next_periodic = 0;
			} else {
				st->fsm = FIQ_PER_SPLIT_DONE;
			}
		} else if (hcint.b.nyet) {
			st->fsm = FIQ_PER_SPLIT_NYET_ABORTED;
			start_next_periodic = 1;
		} else if (hcint.b.xacterr || hcint.b.stall || hcint.b.bblerr) {
			/* Local 3-strikes retry is handled by the core. This is a ERR response.*/
			st->fsm = FIQ_PER_SPLIT_LS_ABORTED;
		} else {
			st->fsm = FIQ_PER_SPLIT_HS_ABORTED;
		}
		break;
	
	case FIQ_PER_CSPLIT_POLL:
		hfnum.d32 = FIQ_READ(state->dwc_regs_base + HFNUM);
		hcchar.d32 = FIQ_READ(state->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCCHAR);
		start_next_periodic = 1;
		if (hcint.b.nak) {
			st->fsm = FIQ_PER_SPLIT_DONE;
		} else if (hcint.b.xfercomp) {
			fiq_increment_dma_buf(state, num_channels, n);
			if (fiq_fsm_more_csplits(state, n, &last_csplit)) {
				handled = 1;
				restart = 1;
				/* Reload HCTSIZ for the next transfer */
				fiq_fsm_reload_hctsiz(state, n);
				if (!last_csplit)
					start_next_periodic = 0;
			} else {
				st->fsm = FIQ_PER_SPLIT_DONE;
			}
		} else if (hcint.b.nyet) {
			/* Are we a NYET after the first data packet? */
			if (st->nrpackets == 0) {
				st->fsm = FIQ_PER_CSPLIT_NYET1;
				handled = 1;
				restart = 1;
			} else {
				/* We got a NYET when polling CSPLITs. Can happen
				 * if our heuristic fails, or if someone disables us
				 * for any significant length of time.
				 */
				if (st->nr_errors >= 3) {
					st->fsm = FIQ_PER_SPLIT_NYET_ABORTED;
				} else {
					st->fsm = FIQ_PER_SPLIT_DONE;
				}
			}
		} else if (hcint.b.xacterr || hcint.b.stall || hcint.b.bblerr) {
			/* For xacterr, Local 3-strikes retry is handled by the core. This is a ERR response.*/
			st->fsm = FIQ_PER_SPLIT_LS_ABORTED;
		} else {
			st->fsm = FIQ_PER_SPLIT_HS_ABORTED;
		}
		break;
	
	case FIQ_HS_ISOC_TURBO:
		if (fiq_fsm_update_hs_isoc(state, n, hcint)) {
			/* more transactions to come */
			handled = 1;
			restart = 1;
			fiq_print(FIQDBG_INT, state, "HSISO M ");
		} else {
			st->fsm = FIQ_HS_ISOC_DONE;
			fiq_print(FIQDBG_INT, state, "HSISO F ");
		}
		break;

	case FIQ_HS_ISOC_ABORTED:
		/* This abort is called by the driver rewriting the state mid-transaction
		 * which allows the dequeue mechanism to work more effectively.
		 */
		break;

	case FIQ_PER_ISO_OUT_ACTIVE:
		if (hcint.b.ack) {
			if(fiq_iso_out_advance(state, num_channels, n)) {
				/* last OUT transfer */
				st->fsm = FIQ_PER_ISO_OUT_LAST;
				/*
				 * Assuming the periodic FIFO in the dwc core
				 * actually does its job properly, we can queue
				 * the next ssplit now and in theory, the wire
				 * transactions will be in-order.
				 */
				// No it doesn't. It appears to process requests in host channel order.
				//start_next_periodic = 1;
			}
			handled = 1;
			restart = 1;
		} else {
			/*
			 * Isochronous transactions carry on regardless. Log the error
			 * and continue.
			 */
			//explode += 1;
			st->nr_errors++;
			if(fiq_iso_out_advance(state, num_channels, n)) {
				st->fsm = FIQ_PER_ISO_OUT_LAST;
				//start_next_periodic = 1;
			}
			handled = 1;
			restart = 1;
		}
		break;
	
	case FIQ_PER_ISO_OUT_LAST:
		if (hcint.b.ack) {
			/* All done here */
			st->fsm = FIQ_PER_ISO_OUT_DONE;
		} else {
			st->fsm = FIQ_PER_ISO_OUT_DONE;
			st->nr_errors++;
		}
		start_next_periodic = 1;
		break;

	case FIQ_PER_SPLIT_TIMEOUT:
		/* SOF kicked us because we overran. */
		start_next_periodic = 1;
		break;

	default:
		break;
	}

	if (handled) {
		FIQ_WRITE(state->dwc_regs_base + HC_START + (HC_OFFSET * n) + HCINT, hcint.d32);
	} else {
		/* Copy the regs into the state so the IRQ knows what to do */
		st->hcint_copy.d32 = hcint.d32;
	}

	if (restart) {
		/* Restart always implies handled. */
		if (restart == 2) {
			/* For complete-split INs, the show must go on.
			 * Force a channel restart */
			fiq_fsm_restart_channel(state, n, 1);
		} else {
			fiq_fsm_restart_channel(state, n, 0);
		}
	}
	if (start_next_periodic) {
		fiq_fsm_start_next_periodic(state, num_channels);
	}
	if (st->fsm != FIQ_PASSTHROUGH)
		fiq_print(FIQDBG_INT, state, "FSMOUT%02d", st->fsm);

	return handled;
}

/**
 * dwc_otg_fiq_fsm() - Flying State Machine (monster) FIQ
 * @state:		pointer to state struct passed from the banked FIQ mode registers.
 * @num_channels:	set according to the DWC hardware configuration
 * @dma:		pointer to DMA bounce buffers for split transaction slots
 *
 * The FSM FIQ performs the low-level tasks that normally would be performed by the microcode
 * inside an EHCI or similar host controller regarding split transactions. The DWC core
 * interrupts each and every time a split transaction packet is received or sent successfully.
 * This results in either an interrupt storm when everything is working "properly", or
 * the interrupt latency of the system in general breaks time-sensitive periodic split
 * transactions. Pushing the low-level, but relatively easy state machine work into the FIQ
 * solves these problems.
 *
 * Return: void
 */
void notrace dwc_otg_fiq_fsm(struct fiq_state *state, int num_channels)
{
	gintsts_data_t gintsts, gintsts_handled;
	gintmsk_data_t gintmsk;
	//hfnum_data_t hfnum;
	haint_data_t haint, haint_handled;
	haintmsk_data_t haintmsk;
	int kick_irq = 0;

	gintsts_handled.d32 = 0;
	haint_handled.d32 = 0;

	gintsts.d32 = FIQ_READ(state->dwc_regs_base + GINTSTS);
	gintmsk.d32 = FIQ_READ(state->dwc_regs_base + GINTMSK);
	gintsts.d32 &= gintmsk.d32;

	if (gintsts.b.sofintr) {
		/* For FSM mode, SOF is required to keep the state machine advance for
		 * certain stages of the periodic pipeline. It's death to mask this
		 * interrupt in that case.
		 */

		if (!fiq_fsm_do_sof(state, num_channels)) {
			/* Kick IRQ once. Queue advancement means that all pending transactions
			 * will get serviced when the IRQ finally executes.
			 */
			if (state->gintmsk_saved.b.sofintr == 1)
				kick_irq |= 1;
			state->gintmsk_saved.b.sofintr = 0;	
		}
		gintsts_handled.b.sofintr = 1;
	}

	if (gintsts.b.hcintr) {
		int i;
		haint.d32 = FIQ_READ(state->dwc_regs_base + HAINT);
		haintmsk.d32 = FIQ_READ(state->dwc_regs_base + HAINTMSK);
		haint.d32 &= haintmsk.d32;
		haint_handled.d32 = 0;
		for (i=0; i<num_channels; i++) {
			if (haint.b2.chint & (1 << i)) {
				if(!fiq_fsm_do_hcintr(state, num_channels, i)) {
					/* HCINT was not handled in FIQ
					 * HAINT is level-sensitive, leading to level-sensitive ginststs.b.hcint bit.
					 * Mask HAINT(i) but keep top-level hcint unmasked.
					 */
					state->haintmsk_saved.b2.chint &= ~(1 << i);
				} else {
					/* do_hcintr cleaned up after itself, but clear haint */
					haint_handled.b2.chint |= (1 << i);
				}
			}
		}
	
		if (haint_handled.b2.chint) {
			FIQ_WRITE(state->dwc_regs_base + HAINT, haint_handled.d32);
		}

		if (haintmsk.d32 != (haintmsk.d32 & state->haintmsk_saved.d32)) {
			/*
			 * This is necessary to avoid multiple retriggers of the MPHI in the case
			 * where interrupts are held off and HCINTs start to pile up.
			 * Only wake up the IRQ if a new interrupt came in, was not handled and was
			 * masked.
			 */
			haintmsk.d32 &= state->haintmsk_saved.d32;
			FIQ_WRITE(state->dwc_regs_base + HAINTMSK, haintmsk.d32);
			kick_irq |= 1;
		}
		/* Top-Level interrupt - always handled because it's level-sensitive */
		gintsts_handled.b.hcintr = 1;
	}

	/* Clear the bits in the saved register that were not handled but were triggered. */
	state->gintmsk_saved.d32 &= ~(gintsts.d32 & ~gintsts_handled.d32);

	/* FIQ didn't handle something - mask has changed - write new mask */
	if (gintmsk.d32 != (gintmsk.d32 & state->gintmsk_saved.d32)) {
		gintmsk.d32 &= state->gintmsk_saved.d32;
		gintmsk.b.sofintr = 1;
		FIQ_WRITE(state->dwc_regs_base + GINTMSK, gintmsk.d32);
//		fiq_print(FIQDBG_INT, state, "KICKGINT");
//		fiq_print(FIQDBG_INT, state, "%08x", gintmsk.d32);
//		fiq_print(FIQDBG_INT, state, "%08x", state->gintmsk_saved.d32);
		kick_irq |= 1;
	}

	if (gintsts_handled.d32) {
		/* Only applies to edge-sensitive bits in GINTSTS */
		FIQ_WRITE(state->dwc_regs_base + GINTSTS, gintsts_handled.d32);
	}

	/* We got an interrupt, didn't handle it. */
	//if (kick_irq) {
	//	state->mphi_int_count++;
	//	FIQ_WRITE(state->mphi_regs.outdda, (int) state->dummy_send);
	//	FIQ_WRITE(state->mphi_regs.outddb, (1<<29));

	//}
	state->fiq_done++;
	if (kick_irq)
		WRITE_CBUS_REG(ISA_TIMERD, 1);
	//mb();
}

/**
 * dwc_otg_fiq_nop() - FIQ "lite"
 * @state:	pointer to state struct passed from the banked FIQ mode registers.
 *
 * The "nop" handler does not intervene on any interrupts other than SOF.
 * It is limited in scope to deciding at each SOF if the IRQ SOF handler (which deals
 * with non-periodic/periodic queues) needs to be kicked.
 *
 * This is done to hold off the SOF interrupt, which occurs at a rate of 8000 per second.
 *
 * Return: void
 */
void notrace dwc_otg_fiq_nop(struct fiq_state *state)
{
	gintsts_data_t gintsts, gintsts_handled;
	gintmsk_data_t gintmsk;
	hfnum_data_t hfnum;

	hfnum.d32 = FIQ_READ(state->dwc_regs_base + HFNUM);
	gintsts.d32 = FIQ_READ(state->dwc_regs_base + GINTSTS);
	gintmsk.d32 = FIQ_READ(state->dwc_regs_base + GINTMSK);
	gintsts.d32 &= gintmsk.d32;
	gintsts_handled.d32 = 0;

	if (gintsts.b.sofintr) {
		if (!state->kick_np_queues &&
				dwc_frame_num_gt(state->next_sched_frame, hfnum.b.frnum)) {
			/* SOF handled, no work to do, just ACK interrupt */
			gintsts_handled.b.sofintr = 1;
		} else {
			/* Kick IRQ */
			state->gintmsk_saved.b.sofintr = 0;
		}
	}

	/* Reset handled interrupts */
	if(gintsts_handled.d32) {
		FIQ_WRITE(state->dwc_regs_base + GINTSTS, gintsts_handled.d32);
	}

	/* Clear the bits in the saved register that were not handled but were triggered. */
	state->gintmsk_saved.d32 &= ~(gintsts.d32 & ~gintsts_handled.d32);

	/* We got an interrupt, didn't handle it and want to mask it */
	if (~(state->gintmsk_saved.d32)) {
		state->mphi_int_count++;
		gintmsk.d32 &= state->gintmsk_saved.d32;
		FIQ_WRITE(state->dwc_regs_base + GINTMSK, gintmsk.d32);
		/* Force a clear before another dummy send */
		FIQ_WRITE(state->mphi_regs.intstat, (1<<29));
		FIQ_WRITE(state->mphi_regs.outdda, (int) state->dummy_send);
		FIQ_WRITE(state->mphi_regs.outddb, (1<<29));

	}
	state->fiq_done++;
	WRITE_CBUS_REG(ISA_TIMERD, 1);
	//mb();
}

/****************************************************************
  * work around here
  * Sometimes GIC FIQ trigger into IRQ mode
*****************************************************************/
long fiq_parament[2];

void set_fiq_parament(unsigned int fiq,long data)
{
	fiq_parament[fiq-INT_USB_A] = data;
}

long get_fiq_parament(unsigned int fiq)
{
	return fiq_parament[fiq-INT_USB_A];
}

void fiq_isr_fake(unsigned int fiq)
{
	struct fiq_state *state;
	int num_channels;
	dwc_otg_hcd_t *dwc_otg_hcd;

	dwc_otg_hcd = (dwc_otg_hcd_t *)get_fiq_parament(fiq);

	state = dwc_otg_hcd->fiq_state;
	if (fiq_fsm_enable) {
		num_channels = dwc_otg_hcd->core_if->core_params->host_channels;
		dwc_otg_fiq_fsm(state,num_channels);
	} else {
		dwc_otg_fiq_nop(state);
	}
}

void handle_fasteoi_irq_fake(unsigned int irq, struct irq_desc *desc)
{
	raw_spin_lock(&desc->lock);
	fiq_isr_fake(irq);
	desc->irq_data.chip->irq_eoi(&desc->irq_data);
	raw_spin_unlock(&desc->lock);
	return;
}

extern void  gic_set_fiq_fake(unsigned fiq,irq_flow_handler_t handle);
void set_fiq_init(unsigned int fiq,long data)
{
	// Enable FIQ interrupt from USB peripheral
	gic_set_fiq_fake(fiq, handle_fasteoi_irq_fake);
	set_fiq_parament(fiq,data);
	enable_fiq(fiq);
}
