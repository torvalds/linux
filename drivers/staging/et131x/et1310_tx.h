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
 * et1310_tx.h - Defines, structs, enums, prototypes, etc. pertaining to data
 *               transmission.
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

#ifndef __ET1310_TX_H__
#define __ET1310_TX_H__


/* Typedefs for Tx Descriptor Ring */

/*
 * word 2 of the control bits in the Tx Descriptor ring for the ET-1310
 *
 * 0-15: length of packet
 * 16-27: VLAN tag
 * 28: VLAN CFI
 * 29-31: VLAN priority
 *
 * word 3 of the control bits in the Tx Descriptor ring for the ET-1310
 *
 * 0: last packet in the sequence
 * 1: first packet in the sequence
 * 2: interrupt the processor when this pkt sent
 * 3: Control word - no packet data
 * 4: Issue half-duplex backpressure : XON/XOFF
 * 5: send pause frame
 * 6: Tx frame has error
 * 7: append CRC
 * 8: MAC override
 * 9: pad packet
 * 10: Packet is a Huge packet
 * 11: append VLAN tag
 * 12: IP checksum assist
 * 13: TCP checksum assist
 * 14: UDP checksum assist
 */

/* struct tx_desc represents each descriptor on the ring */
struct tx_desc {
	u32 addr_hi;
	u32 addr_lo;
	u32 len_vlan;	/* control words how to xmit the */
	u32 flags;	/* data (detailed above) */
};

/* Typedefs for Tx DMA engine status writeback */

/*
 * TX_STATUS_BLOCK_t is sructure representing the status of the Tx DMA engine
 * it sits in free memory, and is pointed to by 0x101c / 0x1020
 */
typedef union _tx_status_block_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 unused:21;		/* bits 11-31 */
		u32 serv_cpl_wrap:1;	/* bit 10 */
		u32 serv_cpl:10;		/* bits 0-9 */
#else
		u32 serv_cpl:10;		/* bits 0-9 */
		u32 serv_cpl_wrap:1;	/* bit 10 */
		u32 unused:21;		/* bits 11-31 */
#endif
	} bits;
} TX_STATUS_BLOCK_t, *PTX_STATUS_BLOCK_t;

/* TCB (Transmit Control Block) */
struct tcb {
	struct tcb *Next;
	u32 Flags;
	u32 Count;
	u32 PacketStaleCount;
	struct sk_buff *Packet;
	u32 PacketLength;
	u32 WrIndex;
	u32 WrIndexStart;
};

/* Structure to hold the skb's in a list */
typedef struct tx_skb_list_elem {
	struct list_head skb_list_elem;
	struct sk_buff *skb;
} TX_SKB_LIST_ELEM, *PTX_SKB_LIST_ELEM;

/* Structure representing our local reference(s) to the ring */
struct tx_ring {
	/* TCB (Transmit Control Block) memory and lists */
	struct tcb *MpTcbMem;

	/* List of TCBs that are ready to be used */
	struct tcb *TCBReadyQueueHead;
	struct tcb *TCBReadyQueueTail;

	/* list of TCBs that are currently being sent.  NOTE that access to all
	 * three of these (including nBusySend) are controlled via the
	 * TCBSendQLock.  This lock should be secured prior to incementing /
	 * decrementing nBusySend, or any queue manipulation on CurrSendHead /
	 * Tail
	 */
	struct tcb *CurrSendHead;
	struct tcb *CurrSendTail;
	int nBusySend;

	/* List of packets (not TCBs) that were queued for lack of resources */
	struct list_head SendWaitQueue;
	int nWaitSend;

	/* The actual descriptor ring */
	struct tx_desc *tx_desc_ring;
	dma_addr_t tx_desc_ring_pa;
	u64 pTxDescRingAdjustedPa;
	u64 TxDescOffset;

	/* ReadyToSend indicates where we last wrote to in the descriptor ring. */
	u32 txDmaReadyToSend;

	/* The location of the write-back status block */
	PTX_STATUS_BLOCK_t pTxStatusVa;
	dma_addr_t pTxStatusPa;

	/* A Block of zeroes used to pad packets that are less than 60 bytes */
	void *pTxDummyBlkVa;
	dma_addr_t pTxDummyBlkPa;

	TXMAC_ERR_t TxMacErr;

	/* Variables to track the Tx interrupt coalescing features */
	int TxPacketsSinceLastinterrupt;
};

/* Forward declaration of the frag-list for the following prototypes */
typedef struct _MP_FRAG_LIST MP_FRAG_LIST, *PMP_FRAG_LIST;

/* Forward declaration of the private adapter structure */
struct et131x_adapter;

/* PROTOTYPES for et1310_tx.c */
int et131x_tx_dma_memory_alloc(struct et131x_adapter *adapter);
void et131x_tx_dma_memory_free(struct et131x_adapter *adapter);
void ConfigTxDmaRegs(struct et131x_adapter *pAdapter);
void et131x_init_send(struct et131x_adapter *adapter);
void et131x_tx_dma_disable(struct et131x_adapter *pAdapter);
void et131x_tx_dma_enable(struct et131x_adapter *pAdapter);
void et131x_handle_send_interrupt(struct et131x_adapter *pAdapter);
void et131x_free_busy_send_packets(struct et131x_adapter *pAdapter);
int et131x_send_packets(struct sk_buff *skb, struct net_device *netdev);

#endif /* __ET1310_TX_H__ */
