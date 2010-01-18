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
typedef struct _FBR_DESC_t {
	u32 addr_lo;
	u32 addr_hi;
	u32 word2;		/* Bits 10-31 reserved, 0-9 descriptor */
} FBR_DESC_t, *PFBR_DESC_t;

/* Typedefs for Packet Status Ring Descriptors */
typedef union _PKT_STAT_DESC_WORD0_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		/* top 16 bits are from the Alcatel Status Word as enumerated in */
		/* PE-MCXMAC Data Sheet IPD DS54 0210-1 (also IPD-DS80 0205-2) */
#if 0
		u32 asw_trunc:1;		/* bit 31(Rx frame truncated) */
#endif
		u32 asw_long_evt:1;	/* bit 31(Rx long event) */
		u32 asw_VLAN_tag:1;	/* bit 30(VLAN tag detected) */
		u32 asw_unsupported_op:1;	/* bit 29(unsupported OP code) */
		u32 asw_pause_frame:1;	/* bit 28(is a pause frame) */
		u32 asw_control_frame:1;	/* bit 27(is a control frame) */
		u32 asw_dribble_nibble:1;	/* bit 26(spurious bits after EOP) */
		u32 asw_broadcast:1;	/* bit 25(has a broadcast address) */
		u32 asw_multicast:1;	/* bit 24(has a multicast address) */
		u32 asw_OK:1;		/* bit 23(valid CRC + no code error) */
		u32 asw_too_long:1;	/* bit 22(frame length > 1518 bytes) */
		u32 asw_len_chk_err:1;	/* bit 21(frame length field incorrect) */
		u32 asw_CRC_err:1;		/* bit 20(CRC error) */
		u32 asw_code_err:1;	/* bit 19(one or more nibbles signalled as errors) */
		u32 asw_false_carrier_event:1;	/* bit 18(bad carrier since last good packet) */
		u32 asw_RX_DV_event:1;	/* bit 17(short receive event detected) */
		u32 asw_prev_pkt_dropped:1;/* bit 16(e.g. IFG too small on previous) */
		u32 unused:5;		/* bits 11-15 */
		u32 vp:1;			/* bit 10(VLAN Packet) */
		u32 jp:1;			/* bit 9(Jumbo Packet) */
		u32 ft:1;			/* bit 8(Frame Truncated) */
		u32 drop:1;		/* bit 7(Drop packet) */
		u32 rxmac_error:1;		/* bit 6(RXMAC Error Indicator) */
		u32 wol:1;			/* bit 5(WOL Event) */
		u32 tcpp:1;		/* bit 4(TCP checksum pass) */
		u32 tcpa:1;		/* bit 3(TCP checksum assist) */
		u32 ipp:1;			/* bit 2(IP checksum pass) */
		u32 ipa:1;			/* bit 1(IP checksum assist) */
		u32 hp:1;			/* bit 0(hash pass) */
#else
		u32 hp:1;			/* bit 0(hash pass) */
		u32 ipa:1;			/* bit 1(IP checksum assist) */
		u32 ipp:1;			/* bit 2(IP checksum pass) */
		u32 tcpa:1;		/* bit 3(TCP checksum assist) */
		u32 tcpp:1;		/* bit 4(TCP checksum pass) */
		u32 wol:1;			/* bit 5(WOL Event) */
		u32 rxmac_error:1;		/* bit 6(RXMAC Error Indicator) */
		u32 drop:1;		/* bit 7(Drop packet) */
		u32 ft:1;			/* bit 8(Frame Truncated) */
		u32 jp:1;			/* bit 9(Jumbo Packet) */
		u32 vp:1;			/* bit 10(VLAN Packet) */
		u32 unused:5;		/* bits 11-15 */
		u32 asw_prev_pkt_dropped:1;/* bit 16(e.g. IFG too small on previous) */
		u32 asw_RX_DV_event:1;	/* bit 17(short receive event detected) */
		u32 asw_false_carrier_event:1;	/* bit 18(bad carrier since last good packet) */
		u32 asw_code_err:1;	/* bit 19(one or more nibbles signalled as errors) */
		u32 asw_CRC_err:1;		/* bit 20(CRC error) */
		u32 asw_len_chk_err:1;	/* bit 21(frame length field incorrect) */
		u32 asw_too_long:1;	/* bit 22(frame length > 1518 bytes) */
		u32 asw_OK:1;		/* bit 23(valid CRC + no code error) */
		u32 asw_multicast:1;	/* bit 24(has a multicast address) */
		u32 asw_broadcast:1;	/* bit 25(has a broadcast address) */
		u32 asw_dribble_nibble:1;	/* bit 26(spurious bits after EOP) */
		u32 asw_control_frame:1;	/* bit 27(is a control frame) */
		u32 asw_pause_frame:1;	/* bit 28(is a pause frame) */
		u32 asw_unsupported_op:1;	/* bit 29(unsupported OP code) */
		u32 asw_VLAN_tag:1;	/* bit 30(VLAN tag detected) */
		u32 asw_long_evt:1;	/* bit 31(Rx long event) */
#if 0
		u32 asw_trunc:1;		/* bit 31(Rx frame truncated) */
#endif
#endif
	} bits;
} PKT_STAT_DESC_WORD0_t, *PPKT_STAT_WORD0_t;

typedef union _PKT_STAT_DESC_WORD1_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 unused:4;	/* bits 28-31 */
		u32 ri:2;		/* bits 26-27(Ring Index) */
		u32 bi:10;		/* bits 16-25(Buffer Index) */
		u32 length:16;	/* bit 0-15(length in bytes) */
#else
		u32 length:16;	/* bit 0-15(length in bytes) */
		u32 bi:10;		/* bits 16-25(Buffer Index) */
		u32 ri:2;		/* bits 26-27(Ring Index) */
		u32 unused:4;	/* bits 28-31 */
#endif
	} bits;
} PKT_STAT_DESC_WORD1_t, *PPKT_STAT_WORD1_t;

typedef struct _PKT_STAT_DESC_t {
	PKT_STAT_DESC_WORD0_t word0;
	PKT_STAT_DESC_WORD1_t word1;
} PKT_STAT_DESC_t, *PPKT_STAT_DESC_t;

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
 */

#define RXSTAT1_OFFSET	16
#define RXSTAT1_MASK	0xFFF
#define RXSTAT1_WRAP	0x10000000

typedef union _rxstat_word1_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 PSRunused:3;	/* bits 29-31 */
		u32 PSRwrap:1;	/* bit 28 */
		u32 PSRoffset:12;	/* bits 16-27 */
		u32 reserved:16;	/* bits 0-15 */
#else
		u32 reserved:16;	/* bits 0-15 */
		u32 PSRoffset:12;	/* bits 16-27 */
		u32 PSRwrap:1;	/* bit 28 */
		u32 PSRunused:3;	/* bits 29-31 */
#endif
	} bits;
} RXSTAT_WORD1_t, *PRXSTAT_WORD1_t;

/*
 * RX_STATUS_BLOCK_t is sructure representing the status of the Rx DMA engine
 * it sits in free memory, and is pointed to by 0x101c / 0x1020
 */
typedef struct _rx_status_block_t {
	u32 Word0;
	RXSTAT_WORD1_t Word1;
} RX_STATUS_BLOCK_t, *PRX_STATUS_BLOCK_t;

/*
 * Structure for look-up table holding free buffer ring pointers
 */
typedef struct _FbrLookupTable {
	void *Va[MAX_DESC_PER_RING_RX];
	void *Buffer1[MAX_DESC_PER_RING_RX];
	void *Buffer2[MAX_DESC_PER_RING_RX];
	u32 PAHigh[MAX_DESC_PER_RING_RX];
	u32 PALow[MAX_DESC_PER_RING_RX];
} FBRLOOKUPTABLE, *PFBRLOOKUPTABLE;

typedef enum {
	ONE_PACKET_INTERRUPT,
	FOUR_PACKET_INTERRUPT
} eRX_INTERRUPT_STATE_t, *PeRX_INTERRUPT_STATE_t;

/*
 * RX_RING_t is sructure representing the adaptor's local reference(s) to the
 * rings
 */
typedef struct _rx_ring_t {
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
	FBRLOOKUPTABLE *Fbr[2];
	u32 local_Fbr1_full;
	u32 Fbr1NumEntries;
	u32 Fbr1BufferSize;

	void *pPSRingVa;
	dma_addr_t pPSRingPa;
	u32 local_psr_full;
	u32 PsrNumEntries;

	void *pRxStatusVa;
	dma_addr_t pRxStatusPa;

	struct list_head RecvBufferPool;

	/* RECV */
	struct list_head RecvList;
	u32 nReadyRecv;

	u32 NumRfd;

	bool UnfinishedReceives;

	struct list_head RecvPacketPool;

	/* lookaside lists */
	struct kmem_cache *RecvLookaside;
} RX_RING_t, *PRX_RING_t;

/* Forward reference of RFD */
struct _MP_RFD;

/* Forward declaration of the private adapter structure */
struct et131x_adapter;

/* PROTOTYPES for Initialization */
int et131x_rx_dma_memory_alloc(struct et131x_adapter *adapter);
void et131x_rx_dma_memory_free(struct et131x_adapter *adapter);
int et131x_rfd_resources_alloc(struct et131x_adapter *adapter,
			       struct _MP_RFD *pMpRfd);
void et131x_rfd_resources_free(struct et131x_adapter *adapter,
			       struct _MP_RFD *pMpRfd);
int et131x_init_recv(struct et131x_adapter *adapter);

void ConfigRxDmaRegs(struct et131x_adapter *adapter);
void SetRxDmaTimer(struct et131x_adapter *adapter);
void et131x_rx_dma_disable(struct et131x_adapter *adapter);
void et131x_rx_dma_enable(struct et131x_adapter *adapter);

void et131x_reset_recv(struct et131x_adapter *adapter);

void et131x_handle_recv_interrupt(struct et131x_adapter *adapter);

#endif /* __ET1310_RX_H__ */
