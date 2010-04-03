/*
 * Agere Systems Inc.
 * 10/100/1000 Base-T Ethernet Driver for the ET1301 and ET131x series MACs
 *
 * Copyright © 2005 Agere Systems Inc.
 * All rights reserved.
 *   http://www.agere.com
 *
 *------------------------------------------------------------------------------
 *
 * et1310_rx.h - Defines, structs, enums, prototypes, etc. pertaining to data
 *               reception.
 *
 *------------------------------------------------------------------------------
 *
 * SOFTWARE LICENSE
 *
 * This software is provided subject to the following terms and conditions,
 * which you should read carefully before using the software.  Using this
 * software indicates your acceptance of these terms and conditions.  If you do
 * not agree with these terms and conditions, do not use the software.
 *
 * Copyright © 2005 Agere Systems Inc.
 * All rights reserved.
 *
 * Redistribution and use in source or binary forms, with or without
 * modifications, are permitted provided that the following conditions are met:
 *
 * . Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following Disclaimer as comments in the code as
 *    well as in the documentation and/or other materials provided with the
 *    distribution.
 *
 * . Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following Disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * . Neither the name of Agere Systems Inc. nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Disclaimer
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, INFRINGEMENT AND THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  ANY
 * USE, MODIFICATION OR DISTRIBUTION OF THIS SOFTWARE IS SOLELY AT THE USERS OWN
 * RISK. IN NO EVENT SHALL AGERE SYSTEMS INC. OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, INCLUDING, BUT NOT LIMITED TO, CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 */

#ifndef __ET1310_RX_H__
#define __ET1310_RX_H__

#include "et1310_address_map.h"

#define USE_FBR0 true

#ifdef USE_FBR0
/* #define FBR0_BUFFER_SIZE 256 */
#endif

/* #define FBR1_BUFFER_SIZE 2048 */

#define FBR_CHUNKS 32

#define MAX_DESC_PER_RING_RX         1024

/* number of RFDs - default and min */
#ifdef USE_FBR0
#define RFD_LOW_WATER_MARK	40
#define NIC_MIN_NUM_RFD		64
#define NIC_DEFAULT_NUM_RFD	1024
#else
#define RFD_LOW_WATER_MARK	20
#define NIC_MIN_NUM_RFD		64
#define NIC_DEFAULT_NUM_RFD	256
#endif

#define NUM_PACKETS_HANDLED	256

#define ALCATEL_BAD_STATUS	0xe47f0000
#define ALCATEL_MULTICAST_PKT	0x01000000
#define ALCATEL_BROADCAST_PKT	0x02000000

/* typedefs for Free Buffer Descriptors */
struct fbr_desc
{
	u32 addr_lo;
	u32 addr_hi;
	u32 word2;		/* Bits 10-31 reserved, 0-9 descriptor */
};

/* Packet Status Ring Descriptors
 *
 * Word 0:
 *
 * top 16 bits are from the Alcatel Status Word as enumerated in
 * PE-MCXMAC Data Sheet IPD DS54 0210-1 (also IPD-DS80 0205-2)
 *
 * 0: hp			hash pass
 * 1: ipa			IP checksum assist
 * 2: ipp			IP checksum pass
 * 3: tcpa			TCP checksum assist
 * 4: tcpp			TCP checksum pass
 * 5: wol			WOL Event
 * 6: rxmac_error		RXMAC Error Indicator
 * 7: drop			Drop packet
 * 8: ft			Frame Truncated
 * 9: jp			Jumbo Packet
 * 10: vp			VLAN Packet
 * 11-15: unused
 * 16: asw_prev_pkt_dropped 	e.g. IFG too small on previous
 * 17: asw_RX_DV_event		short receive event detected
 * 18: asw_false_carrier_event	bad carrier since last good packet
 * 19: asw_code_err		one or more nibbles signalled as errors
 * 20: asw_CRC_err		CRC error
 * 21: asw_len_chk_err		frame length field incorrect
 * 22: asw_too_long		frame length > 1518 bytes
 * 23: asw_OK			valid CRC + no code error
 * 24: asw_multicast		has a multicast address
 * 25: asw_broadcast		has a broadcast address
 * 26: asw_dribble_nibble	spurious bits after EOP
 * 27: asw_control_frame	is a control frame
 * 28: asw_pause_frame		is a pause frame
 * 29: asw_unsupported_op	unsupported OP code
 * 30: asw_VLAN_tag		VLAN tag detected
 * 31: asw_long_evt		Rx long event
 *
 * Word 1:
 * 0-15: length			length in bytes
 * 16-25: bi			Buffer Index
 * 26-27: ri			Ring Index
 * 28-31: reserved
 */

struct pkt_stat_desc {
	u32 word0;
	u32 word1;
};

/* Typedefs for the RX DMA status word */

/*
 * rx status word 0 holds part of the status bits of the Rx DMA engine
 * that get copied out to memory by the ET-1310.  Word 0 is a 32 bit word
 * which contains the Free Buffer ring 0 and 1 available offset.
 *
 * bit 0-9 FBR1 offset
 * bit 10 Wrap flag for FBR1
 * bit 16-25 FBR0 offset
 * bit 26 Wrap flag for FBR0
 */

/*
 * RXSTAT_WORD1_t structure holds part of the status bits of the Rx DMA engine
 * that get copied out to memory by the ET-1310.  Word 3 is a 32 bit word
 * which contains the Packet Status Ring available offset.
 *
 * bit 0-15 reserved
 * bit 16-27 PSRoffset
 * bit 28 PSRwrap
 * bit 29-31 unused
 */

/*
 * struct rx_status_block is a structure representing the status of the Rx
 * DMA engine it sits in free memory, and is pointed to by 0x101c / 0x1020
 */
struct rx_status_block {
	u32 Word0;
	u32 Word1;
};

/*
 * Structure for look-up table holding free buffer ring pointers
 */
struct fbr_lookup {
	void *virt[MAX_DESC_PER_RING_RX];
	void *buffer1[MAX_DESC_PER_RING_RX];
	void *buffer2[MAX_DESC_PER_RING_RX];
	u32 bus_high[MAX_DESC_PER_RING_RX];
	u32 bus_low[MAX_DESC_PER_RING_RX];
};

/*
 * struct rx_ring is the ssructure representing the adaptor's local
 * reference(s) to the rings
 */
struct rx_ring {
#ifdef USE_FBR0
	void *pFbr0RingVa;
	dma_addr_t pFbr0RingPa;
	void *Fbr0MemVa[MAX_DESC_PER_RING_RX / FBR_CHUNKS];
	dma_addr_t Fbr0MemPa[MAX_DESC_PER_RING_RX / FBR_CHUNKS];
	uint64_t Fbr0Realpa;
	uint64_t Fbr0offset;
	u32 local_Fbr0_full;
	u32 Fbr0NumEntries;
	u32 Fbr0BufferSize;
#endif
	void *pFbr1RingVa;
	dma_addr_t pFbr1RingPa;
	void *Fbr1MemVa[MAX_DESC_PER_RING_RX / FBR_CHUNKS];
	dma_addr_t Fbr1MemPa[MAX_DESC_PER_RING_RX / FBR_CHUNKS];
	uint64_t Fbr1Realpa;
	uint64_t Fbr1offset;
	struct fbr_lookup *fbr[2];	/* One per ring */
	u32 local_Fbr1_full;
	u32 Fbr1NumEntries;
	u32 Fbr1BufferSize;

	void *pPSRingVa;
	dma_addr_t pPSRingPa;
	u32 local_psr_full;
	u32 PsrNumEntries;

	struct rx_status_block *rx_status_block;
	dma_addr_t rx_status_bus;

	struct list_head RecvBufferPool;

	/* RECV */
	struct list_head RecvList;
	u32 nReadyRecv;

	u32 NumRfd;

	bool UnfinishedReceives;

	struct list_head RecvPacketPool;

	/* lookaside lists */
	struct kmem_cache *RecvLookaside;
};

#endif /* __ET1310_RX_H__ */
