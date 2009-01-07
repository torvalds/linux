/**************************************************************************
 *
 * Copyright © 2000-2008 Alacritech, Inc.  All rights reserved.
 *
 * $Id: sxg.h,v 1.3 2008/07/24 17:25:08 chris Exp $
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY ALACRITECH, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ALACRITECH, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of Alacritech, Inc.
 *
 **************************************************************************/

/*
 * FILENAME: sxg.h
 *
 * This is the base set of header definitions for the SXG driver.
 */
#ifndef __SXG_DRIVER_H__
#define __SXG_DRIVER_H__

#define p_net_device struct net_device *
// SXG_STATS - Probably move these to someplace where
// the slicstat (sxgstat?) program can get them.
struct SXG_STATS {
	// Xmt
	u32				XmtNBL;				// Offload send NBL count
	u64				DumbXmtBytes;		// Dumbnic send bytes
	u64				SlowXmtBytes;		// Slowpath send bytes
	u64				FastXmtBytes;		// Fastpath send bytes
	u64				DumbXmtPkts;		// Dumbnic send packets
	u64				SlowXmtPkts;		// Slowpath send packets
	u64				FastXmtPkts;		// Fastpath send packets
    u64				DumbXmtUcastPkts;	// directed packets
    u64				DumbXmtMcastPkts;	// Multicast packets
    u64				DumbXmtBcastPkts;	// OID_GEN_BROADCAST_FRAMES_RCV
    u64				DumbXmtUcastBytes;	// OID_GEN_DIRECTED_BYTES_XMIT
    u64				DumbXmtMcastBytes;	// OID_GEN_MULTICAST_BYTES_XMIT
    u64				DumbXmtBcastBytes;	// OID_GEN_BROADCAST_BYTES_XMIT
    u64				XmtErrors;			// OID_GEN_XMIT_ERROR
    u64				XmtDiscards;		// OID_GEN_XMIT_DISCARDS
	u64				XmtOk;				// OID_GEN_XMIT_OK
	u64				XmtQLen;			// OID_GEN_TRANSMIT_QUEUE_LENGTH
	u64				XmtZeroFull;		// Transmit ring zero full
	// Rcv
	u32				RcvNBL;				// Offload recieve NBL count
	u64				DumbRcvBytes;		// dumbnic recv bytes
    u64             DumbRcvUcastBytes;	// OID_GEN_DIRECTED_BYTES_RCV
    u64             DumbRcvMcastBytes;	// OID_GEN_MULTICAST_BYTES_RCV
    u64             DumbRcvBcastBytes;	// OID_GEN_BROADCAST_BYTES_RCV
	u64				SlowRcvBytes;		// Slowpath recv bytes
	u64				FastRcvBytes;		// Fastpath recv bytes
    u64				DumbRcvPkts;		// OID_GEN_DIRECTED_FRAMES_RCV
	u64				DumbRcvTcpPkts;		// See SxgCollectStats
    u64				DumbRcvUcastPkts;	// directed packets
    u64				DumbRcvMcastPkts;	// Multicast packets
    u64				DumbRcvBcastPkts;	// OID_GEN_BROADCAST_FRAMES_RCV
	u64				SlowRcvPkts;		// OID_GEN_DIRECTED_FRAMES_RCV
    u64				RcvErrors;			// OID_GEN_RCV_ERROR
    u64				RcvDiscards;		// OID_GEN_RCV_DISCARDS
	u64				RcvNoBuffer;		// OID_GEN_RCV_NO_BUFFER
    u64 			PdqFull;			// Processed Data Queue Full
	u64				EventRingFull;		// Event ring full
	// Verbose stats
	u64				MaxSends;			// Max sends outstanding
	u64				NoSglBuf;			// SGL buffer allocation failure
	u64				SglFail;			// NDIS SGL failure
	u64				SglAsync;			// NDIS SGL failure
	u64				NoMem;				// Memory allocation failure
	u64				NumInts;			// Interrupts
	u64				FalseInts;			// Interrupt with ISR == 0
	u64				XmtDrops;			// No sahara DRAM buffer for xmt
	// Sahara receive status
	u64				TransportCsum;		// SXG_RCV_STATUS_TRANSPORT_CSUM
	u64				TransportUflow;		// SXG_RCV_STATUS_TRANSPORT_UFLOW
	u64				TransportHdrLen;	// SXG_RCV_STATUS_TRANSPORT_HDRLEN
	u64				NetworkCsum;		// SXG_RCV_STATUS_NETWORK_CSUM:
	u64				NetworkUflow;		// SXG_RCV_STATUS_NETWORK_UFLOW:
	u64				NetworkHdrLen;		// SXG_RCV_STATUS_NETWORK_HDRLEN:
	u64				Parity;				// SXG_RCV_STATUS_PARITY
	u64				LinkParity;			// SXG_RCV_STATUS_LINK_PARITY:
	u64				LinkEarly;			// SXG_RCV_STATUS_LINK_EARLY:
	u64				LinkBufOflow;		// SXG_RCV_STATUS_LINK_BUFOFLOW:
	u64				LinkCode;			// SXG_RCV_STATUS_LINK_CODE:
	u64				LinkDribble;		// SXG_RCV_STATUS_LINK_DRIBBLE:
	u64				LinkCrc;			// SXG_RCV_STATUS_LINK_CRC:
	u64				LinkOflow;			// SXG_RCV_STATUS_LINK_OFLOW:
	u64				LinkUflow;			// SXG_RCV_STATUS_LINK_UFLOW:
};


/****************************************************************************
 * DUMB-NIC Send path definitions
 ****************************************************************************/

#define SXG_COMPLETE_DUMB_SEND(_pAdapt, _skb) {                     		    	\
	ASSERT(_skb);													    			\
    dev_kfree_skb_irq(_skb);                                                        \
}

#define SXG_DROP_DUMB_SEND(_pAdapt, _skb) {                           		    	\
	ASSERT(_skb);													    			\
    dev_kfree_skb(_skb);                                                            \
}

// Locate current receive header buffer location.  Use this
// instead of RcvDataHdr->VirtualAddress since the data
// may have been offset by SXG_ADVANCE_MDL_OFFSET
#define SXG_RECEIVE_DATA_LOCATION(_RcvDataHdr)        (_RcvDataHdr)->skb->data

/************************************************************************
 * Dumb-NIC receive processing
 ************************************************************************/
// Define an SXG_PACKET as an NDIS_PACKET
#define PSXG_PACKET       struct sk_buff *
// Indications array size
#define SXG_RCV_ARRAYSIZE	64

#define SXG_ALLOCATE_RCV_PACKET(_pAdapt, _RcvDataBufferHdr) {				\
	struct sk_buff * skb;												    \
    skb = alloc_skb(2048, GFP_ATOMIC);                                      \
    if (skb) {                                                              \
    	(_RcvDataBufferHdr)->skb = skb;                                     \
        skb->next = NULL;                                                   \
    } else {                                                                \
    	(_RcvDataBufferHdr)->skb = NULL;                                    \
    }                                                                       \
}

#define SXG_FREE_RCV_PACKET(_RcvDataBufferHdr) {							\
	if((_RcvDataBufferHdr)->skb) {											\
		dev_kfree_skb((_RcvDataBufferHdr)->skb);						    \
    }                                                                       \
}

// Macro to add a NDIS_PACKET to an indication array
// If we fill up our array of packet pointers, then indicate this
// block up now and start on a new one.
#define	SXG_ADD_RCV_PACKET(_pAdapt, _Packet, _PrevPacket, _IndicationList, _NumPackets) { \
	(_IndicationList)[_NumPackets] = (_Packet);										\
	(_NumPackets)++;																\
	if((_NumPackets) == SXG_RCV_ARRAYSIZE) {										\
		SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "IndicRcv",				\
				   (_NumPackets), 0, 0, 0);											\
        netif_rx((_IndicationList),(_NumPackets));                                  \
		(_NumPackets) = 0;															\
	}																				\
}

#define SXG_INDICATE_PACKETS(_pAdapt, _IndicationList, _NumPackets) {			\
	if(_NumPackets) {															\
		SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "IndicRcv",			\
				   (_NumPackets), 0, 0, 0);										\
        netif_rx((_IndicationList),(_NumPackets));                              \
		(_NumPackets) = 0;														\
	}																			\
}

#define SXG_REINIATIALIZE_PACKET(_Packet)										\
	{}  /*_NdisReinitializePacket(_Packet)*/  /*  this is not necessary with an skb */

// Definitions to initialize Dumb-nic Receive NBLs
#define SXG_RCV_PACKET_BUFFER_HDR(_Packet) (((PSXG_RCV_NBL_RESERVED)((_Packet)->MiniportReservedEx))->RcvDataBufferHdr)

#define SXG_RCV_SET_CHECKSUM_INFO(_Packet, _Cpi)	\
	NDIS_PER_PACKET_INFO_FROM_PACKET((_Packet), TcpIpChecksumPacketInfo) = (PVOID)(_Cpi)

#define SXG_RCV_SET_TOEPLITZ(_Packet, _Toeplitz, _Type, _Function) {		\
	NDIS_PACKET_SET_HASH_VALUE((_Packet), (_Toeplitz));						\
	NDIS_PACKET_SET_HASH_TYPE((_Packet), (_Type));							\
	NDIS_PACKET_SET_HASH_FUNCTION((_Packet), (_Function));					\
}

#define SXG_RCV_SET_VLAN_INFO(_Packet, _VlanId, _Priority) {					\
	NDIS_PACKET_8021Q_INFO	_Packet8021qInfo;									\
	_Packet8021qInfo.TagHeader.VlanId = (_VlanId);								\
	_Packet8021qInfo.TagHeader.UserPriority = (_Priority);						\
	NDIS_PER_PACKET_INFO_FROM_PACKET((_Packet), Ieee8021QNetBufferListInfo) = 	\
		_Packet8021qInfo.Value;													\
}

#define SXG_ADJUST_RCV_PACKET(_Packet, _RcvDataBufferHdr, _Event) {			\
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "DumbRcv",			\
			   (_RcvDataBufferHdr), (_Packet),								\
			   (_Event)->Status, 0);	                    				\
	ASSERT((_Event)->Length <= (_RcvDataBufferHdr)->Size);					\
    Packet->len = (_Event)->Length;                                         \
}

///////////////////////////////////////////////////////////////////////////////
// Macros to free a receive data buffer and receive data descriptor block
///////////////////////////////////////////////////////////////////////////////
// NOTE - Lock must be held with RCV macros
#define SXG_GET_RCV_DATA_BUFFER(_pAdapt, _Hdr) {								\
	struct LIST_ENTRY     				*_ple;										\
	_Hdr = NULL;																\
	if((_pAdapt)->FreeRcvBufferCount) {											\
		ASSERT(!(IsListEmpty(&(_pAdapt)->FreeRcvBuffers)));						\
		_ple = RemoveHeadList(&(_pAdapt)->FreeRcvBuffers);	    				\
		(_Hdr) = container_of(_ple, struct SXG_RCV_DATA_BUFFER_HDR, FreeList);	        \
		(_pAdapt)->FreeRcvBufferCount--;										\
		ASSERT((_Hdr)->State == SXG_BUFFER_FREE);								\
	}																			\
}

#define SXG_FREE_RCV_DATA_BUFFER(_pAdapt, _Hdr) {							\
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "RtnDHdr",			\
			   (_Hdr), (_pAdapt)->FreeRcvBufferCount,						\
			   (_Hdr)->State, (_Hdr)->VirtualAddress);						\
/*	SXG_RESTORE_MDL_OFFSET(_Hdr);	*/										\
	(_pAdapt)->FreeRcvBufferCount++;										\
	ASSERT(((_pAdapt)->AllRcvBlockCount * SXG_RCV_DESCRIPTORS_PER_BLOCK) >= (_pAdapt)->FreeRcvBufferCount); \
	ASSERT((_Hdr)->State != SXG_BUFFER_FREE);								\
	(_Hdr)->State = SXG_BUFFER_FREE;										\
	InsertTailList(&(_pAdapt)->FreeRcvBuffers, &((_Hdr)->FreeList));		\
}

#define SXG_FREE_RCV_DESCRIPTOR_BLOCK(_pAdapt, _Hdr) {						\
	ASSERT((_Hdr)->State != SXG_BUFFER_FREE);								\
	(_Hdr)->State = SXG_BUFFER_FREE;										\
	(_pAdapt)->FreeRcvBlockCount++;											\
	ASSERT((_pAdapt)->AllRcvBlockCount >= (_pAdapt)->FreeRcvBlockCount);	\
	InsertTailList(&(_pAdapt)->FreeRcvBlocks, &(_Hdr)->FreeList);			\
}

// SGL macros
#define SXG_FREE_SGL_BUFFER(_pAdapt, _Sgl, _NB) {	\
	spin_lock(&(_pAdapt)->SglQLock);		\
	(_pAdapt)->FreeSglBufferCount++;		\
	ASSERT((_pAdapt)->AllSglBufferCount >= (_pAdapt)->FreeSglBufferCount);\
	ASSERT(!((_Sgl)->State & SXG_BUFFER_FREE));	\
	(_Sgl)->State = SXG_BUFFER_FREE;		\
	InsertTailList(&(_pAdapt)->FreeSglBuffers, &(_Sgl)->FreeList);	\
	spin_unlock(&(_pAdapt)->SglQLock);		\
}

// Get an SGL buffer from the free queue.  The first part of this macro
// attempts to keep ahead of buffer depletion by allocating more when
// we hit a minimum threshold.  Note that we don't grab the lock
// until after that.  We're dealing with round numbers here, so we don't need to,
// and not grabbing it avoids a possible double-trip.
#define SXG_GET_SGL_BUFFER(_pAdapt, _Sgl) {				\
	struct LIST_ENTRY *_ple;						\
	if ((_pAdapt->FreeSglBufferCount < SXG_MIN_SGL_BUFFERS) &&	\
	   (_pAdapt->AllSglBufferCount < SXG_MAX_SGL_BUFFERS) &&	\
	   (_pAdapt->AllocationsPending == 0)) {			\
		sxg_allocate_buffer_memory(_pAdapt,			\
			(sizeof(struct SXG_SCATTER_GATHER) + SXG_SGL_BUF_SIZE),\
			SXG_BUFFER_TYPE_SGL);				\
	}								\
	_Sgl = NULL;							\
	spin_lock(&(_pAdapt)->SglQLock);				\
	if((_pAdapt)->FreeSglBufferCount) {				\
		ASSERT(!(IsListEmpty(&(_pAdapt)->FreeSglBuffers)));	\
		_ple = RemoveHeadList(&(_pAdapt)->FreeSglBuffers);	\
		(_Sgl) = container_of(_ple, struct SXG_SCATTER_GATHER, FreeList); \
            (_pAdapt)->FreeSglBufferCount--;				\
		ASSERT((_Sgl)->State == SXG_BUFFER_FREE);		\
		(_Sgl)->State = SXG_BUFFER_BUSY;			\
		(_Sgl)->pSgl = NULL;					\
	}								\
	spin_unlock(&(_pAdapt)->SglQLock);				\
}

//
// SXG_MULTICAST_ADDRESS
//
// Linked list of multicast addresses.
struct SXG_MULTICAST_ADDRESS {
	unsigned char							Address[6];
	struct SXG_MULTICAST_ADDRESS	*Next;
};

// Structure to maintain chimney send and receive buffer queues.
// This structure maintains NET_BUFFER_LIST queues that are
// given to us via the Chimney MiniportTcpOffloadSend and
// MiniportTcpOffloadReceive routines.  This structure DOES NOT
// manage our data buffer queue
struct SXG_BUFFER_QUEUE {
	u32						Type;			// Slow or fast - See below
	u32						Direction;		// Xmt or Rcv
	u32						Bytes;			// Byte count
	u32 *        			Head;			// Send queue head
	u32 *        			Tail;			// Send queue tail
//	PNET_BUFFER_LIST			NextNBL;		// Short cut - next NBL
//	PNET_BUFFER					NextNB;			// Short cut - next NB
};

#define		SXG_SLOW_SEND_BUFFER	0
#define		SXG_FAST_SEND_BUFFER	1
#define 	SXG_RECEIVE_BUFFER		2

#define SXG_INIT_BUFFER(_Buffer, _Type) { 						\
	(_Buffer)->Type = (_Type);									\
	if((_Type) == SXG_RECEIVE_BUFFER) {							\
		(_Buffer)->Direction = 0;								\
	} else {													\
		(_Buffer)->Direction = NDIS_SG_LIST_WRITE_TO_DEVICE;	\
	}															\
	(_Buffer)->Bytes = 0;										\
	(_Buffer)->Head = NULL;										\
	(_Buffer)->Tail = NULL;										\
}


#define SXG_RSS_CPU_COUNT(_pAdapt) 								\
	((_pAdapt)->RssEnabled 	?  NR_CPUS : 1)

/****************************************************************************
 * DRIVER and ADAPTER structures
 ****************************************************************************/

// Adapter states - These states closely match the adapter states
// documented in the DDK (with a few exceptions).
enum SXG_STATE {
	SXG_STATE_INITIALIZING,			// Initializing
	SXG_STATE_BOOTDIAG,				// Boot-Diagnostic mode
	SXG_STATE_PAUSING,				// Pausing
	SXG_STATE_PAUSED,				// Paused
	SXG_STATE_RUNNING,				// Running
	SXG_STATE_RESETTING,			// Reset in progress
	SXG_STATE_SLEEP,				// Sleeping
	SXG_STATE_DIAG,					// Diagnostic mode
	SXG_STATE_HALTING,				// Halting
	SXG_STATE_HALTED,				// Down or not-initialized
	SXG_STATE_SHUTDOWN				// shutdown
};

// Link state
enum SXG_LINK_STATE {
	SXG_LINK_DOWN,
	SXG_LINK_UP
};

// Link initialization timeout in 100us units
#define SXG_LINK_TIMEOUT	100000		// 10 Seconds - REDUCE!


// Microcode file selection codes
enum SXG_UCODE_SEL {
	SXG_UCODE_SAHARA,				// Sahara ucode
	SXG_UCODE_SDIAGCPU,				// Sahara CPU diagnostic ucode
	SXG_UCODE_SDIAGSYS				// Sahara system diagnostic ucode
};


#define SXG_DISABLE_ALL_INTERRUPTS(_padapt) sxg_disable_interrupt(_padapt)
#define SXG_ENABLE_ALL_INTERRUPTS(_padapt) sxg_enable_interrupt(_padapt)

// This probably lives in a proto.h file.  Move later
#define SXG_MULTICAST_PACKET(_pether) ((_pether)->ether_dhost[0] & 0x01)
#define SXG_BROADCAST_PACKET(_pether) ((*(u32 *)(_pether)->ether_dhost == 0xFFFFFFFF) && \
				(*(u16 *)&(_pether)->ether_dhost[4] == 0xFFFF))

// For DbgPrints
#define SXG_ID      DPFLTR_IHVNETWORK_ID
#define SXG_ERROR   DPFLTR_ERROR_LEVEL

//
// SXG_DRIVER structure -
//
// contains information about the sxg driver.  There is only
// one of these, and it is defined as a global.
struct SXG_DRIVER {
	struct adapter_t	*Adapters;		// Linked list of adapters
	ushort				AdapterID;		// Maintain unique adapter ID
};

#ifdef STATUS_SUCCESS
#undef STATUS_SUCCESS
#endif

#define STATUS_SUCCESS              0
#define STATUS_PENDING              0
#define STATUS_FAILURE             -1
#define STATUS_ERROR               -2
#define STATUS_NOT_SUPPORTED       -3
#define STATUS_BUFFER_TOO_SHORT    -4
#define STATUS_RESOURCES           -5

#define SLIC_MAX_CARDS              32
#define SLIC_MAX_PORTS              4        /* Max # of ports per card   */
#if SLIC_DUMP_ENABLED
// Dump buffer size
//
// This cannot be bigger than the max DMA size the card supports,
// given the current code structure in the host and ucode.
// Mojave supports 16K, Oasis supports 16K-1, so
// just set this at 15K, shouldnt make that much of a diff.
#define DUMP_BUF_SIZE               0x3C00
#endif

#define MIN(a, b) ((u32)(a) < (u32)(b) ? (a) : (b))
#define MAX(a, b) ((u32)(a) > (u32)(b) ? (a) : (b))

struct mcast_address_t {
    unsigned char                     address[6];
    struct mcast_address_t   *next;
};

#define CARD_DOWN        0x00000000
#define CARD_UP          0x00000001
#define CARD_FAIL        0x00000002
#define CARD_DIAG        0x00000003
#define CARD_SLEEP       0x00000004

#define ADAPT_DOWN             0x00
#define ADAPT_UP               0x01
#define ADAPT_FAIL             0x02
#define ADAPT_RESET            0x03
#define ADAPT_SLEEP            0x04

#define ADAPT_FLAGS_BOOTTIME            0x0001
#define ADAPT_FLAGS_IS64BIT             0x0002
#define ADAPT_FLAGS_PENDINGLINKDOWN     0x0004
#define ADAPT_FLAGS_FIBERMEDIA          0x0008
#define ADAPT_FLAGS_LOCKS_ALLOCED       0x0010
#define ADAPT_FLAGS_INT_REGISTERED      0x0020
#define ADAPT_FLAGS_LOAD_TIMER_SET      0x0040
#define ADAPT_FLAGS_STATS_TIMER_SET     0x0080
#define ADAPT_FLAGS_RESET_TIMER_SET     0x0100

#define LINK_DOWN              0x00
#define LINK_CONFIG            0x01
#define LINK_UP                0x02

#define LINK_10MB              0x00
#define LINK_100MB             0x01
#define LINK_AUTOSPEED         0x02
#define LINK_1000MB            0x03
#define LINK_10000MB           0x04

#define LINK_HALFD             0x00
#define LINK_FULLD             0x01
#define LINK_AUTOD             0x02

#define MAC_DIRECTED     0x00000001
#define MAC_BCAST        0x00000002
#define MAC_MCAST        0x00000004
#define MAC_PROMISC      0x00000008
#define MAC_LOOPBACK     0x00000010
#define MAC_ALLMCAST     0x00000020

#define SLIC_DUPLEX(x)    ((x==LINK_FULLD) ? "FDX" : "HDX")
#define SLIC_SPEED(x)     ((x==LINK_100MB) ? "100Mb" : ((x==LINK_1000MB) ? "1000Mb" : " 10Mb"))
#define SLIC_LINKSTATE(x) ((x==LINK_DOWN) ? "Down" : "Up  ")
#define SLIC_ADAPTER_STATE(x) ((x==ADAPT_UP) ? "UP" : "Down")
#define SLIC_CARD_STATE(x)    ((x==CARD_UP) ? "UP" : "Down")


struct ether_header {
    unsigned char    ether_dhost[6];
    unsigned char    ether_shost[6];
    ushort   ether_type;
};


#define NUM_CFG_SPACES      2
#define NUM_CFG_REGS        64

struct physcard_t {
    struct adapter_t  *adapter[SLIC_MAX_PORTS];
    struct physcard_t *next;
    unsigned int                adapters_allocd;
};

struct sxgbase_driver_t {
	spinlock_t	driver_lock;
	unsigned long	flags;	/* irqsave for spinlock */
	u32		num_sxg_cards;
	u32		num_sxg_ports;
	u32		num_sxg_ports_active;
	u32		dynamic_intagg;
	struct physcard_t	*phys_card;
};


struct adapter_t {
	void *               ifp;
	unsigned int                port;
	struct physcard_t        *physcard;
	unsigned int                physport;
	unsigned int                cardindex;
	unsigned int                card_size;
	unsigned int                chipid;
	unsigned int                busnumber;
	unsigned int                slotnumber;
	unsigned int                functionnumber;
	ushort              vendid;
	ushort              devid;
	ushort              subsysid;
	u32             irq;

	void *               sxg_adapter;
	u32             nBusySend;

	void __iomem *	base_addr;
	u32             memorylength;
	u32             drambase;
	u32             dramlength;
	unsigned int                queues_initialized;
	unsigned int                allocated;
	unsigned int                activated;
	u32             intrregistered;
	unsigned int                isp_initialized;
	unsigned int                gennumber;
	u32             curaddrupper;
	u32             isrcopy;
	unsigned char               state;
	unsigned char               linkstate;
	unsigned char               linkspeed;
	unsigned char               linkduplex;
	unsigned int                flags;
	unsigned char               macaddr[6];
	unsigned char               currmacaddr[6];
	u32             macopts;
	ushort              devflags_prev;
	u64             mcastmask;
	struct mcast_address_t   *mcastaddrs;
	struct timer_list   pingtimer;
	u32             pingtimerset;
	struct timer_list   statstimer;
	u32             statstimerset;
	struct timer_list   vpci_timer;
	u32             vpci_timerset;
	struct timer_list   loadtimer;
	u32             loadtimerset;

	u32             xmitq_full;
	u32             all_reg_writes;
	u32             icr_reg_writes;
	u32             isr_reg_writes;
	u32             error_interrupts;
	u32             error_rmiss_interrupts;
	u32             rx_errors;
	u32             rcv_drops;
	u32             rcv_interrupts;
	u32             xmit_interrupts;
	u32             linkevent_interrupts;
	u32             upr_interrupts;
	u32             num_isrs;
	u32             false_interrupts;
	u32             tx_packets;
	u32             xmit_completes;
	u32             tx_drops;
	u32             rcv_broadcasts;
	u32             rcv_multicasts;
	u32             rcv_unicasts;
	u32             max_isr_rcvs;
	u32             max_isr_xmits;
	u32             rcv_interrupt_yields;
	u32             intagg_period;
	struct net_device_stats stats;
	u32 *					MiniportHandle;		// Our miniport handle
	enum SXG_STATE					State;				// Adapter state
	enum SXG_LINK_STATE				LinkState;			// Link state
	u64						LinkSpeed;			// Link Speed
	u32						PowerState;			// NDIS power state
	struct adapter_t   		*Next;				// Linked list
	ushort						AdapterID;			// 1..n
	unsigned char						MacAddr[6];			// Our permanent HW mac address
	unsigned char						CurrMacAddr[6];		// Our Current mac address
	p_net_device                netdev;
	p_net_device                next_netdevice;
	struct pci_dev            * pcidev;

	struct SXG_MULTICAST_ADDRESS		*MulticastAddrs;		// Multicast list
	u64     				MulticastMask;		// Multicast mask
	u32 *					InterruptHandle;	// Register Interrupt handle
	u32						InterruptLevel;		// From Resource list
	u32						InterruptVector;	// From Resource list
	spinlock_t	AdapterLock;	/* Serialize access adapter routines */
	spinlock_t	Bit64RegLock;	/* For writing 64-bit addresses */
	struct SXG_HW_REGS			*HwRegs;				// Sahara HW Register Memory (BAR0/1)
	struct SXG_UCODE_REGS			*UcodeRegs;			// Microcode Register Memory (BAR2/3)
	struct SXG_TCB_REGS			*TcbRegs;			// Same as Ucode regs - See sxghw.h
	ushort						ResetDpcCount;		// For timeout
	ushort						RssDpcCount;		// For timeout
	ushort						VendorID;			// Vendor ID
	ushort						DeviceID;			// Device ID
	ushort						SubSystemID;		// Sub-System ID
	ushort						FrameSize;			// Maximum frame size
	u32 *					DmaHandle;			// NDIS DMA handle
	u32 *					PacketPoolHandle;	// Used with NDIS 5.2 only.  Don't ifdef out
	u32 *					BufferPoolHandle;	// Used with NDIS 5.2 only.  Don't ifdef out
	u32						MacFilter;			// NDIS MAC Filter
	ushort						IpId;				// For slowpath
	struct SXG_EVENT_RING			*EventRings;			// Host event rings.  1/CPU to 16 max
	dma_addr_t              	PEventRings;		// Physical address
	u32						NextEvent[SXG_MAX_RSS];	// Current location in ring
	dma_addr_t          		PTcbBuffers;		// TCB Buffers - physical address
	dma_addr_t	            	PTcbCompBuffers;	// TCB Composite Buffers - phys addr
	struct SXG_XMT_RING				*XmtRings;			// Transmit rings
	dma_addr_t		            PXmtRings;			// Transmit rings - physical address
	struct SXG_RING_INFO				XmtRingZeroInfo;	// Transmit ring 0 info
	spinlock_t	XmtZeroLock;	/* Transmit ring 0 lock */
	u32 *					XmtRingZeroIndex;	// Shared XMT ring 0 index
	dma_addr_t          		PXmtRingZeroIndex;	// Shared XMT ring 0 index - physical
	struct LIST_ENTRY					FreeProtocolHeaders;// Free protocol headers
	u32						FreeProtoHdrCount;	// Count
	void *						ProtocolHeaders;	// Block of protocol header
	dma_addr_t	            	PProtocolHeaders;	// Block of protocol headers - phys

	struct SXG_RCV_RING		*RcvRings;			// Receive rings
	dma_addr_t	            	PRcvRings;			// Receive rings - physical address
	struct SXG_RING_INFO				RcvRingZeroInfo;	// Receive ring 0 info

	u32 *					Isr;				// Interrupt status register
	dma_addr_t	            	PIsr;				// ISR - physical address
	u32						IsrCopy[SXG_MAX_RSS];	// Copy of ISR
	ushort						InterruptsEnabled;	// Bitmask of enabled vectors
	unsigned char *						IndirectionTable;	// RSS indirection table
	dma_addr_t	            	PIndirectionTable;	// Physical address
	ushort						RssTableSize;		// From NDIS_RECEIVE_SCALE_PARAMETERS
	ushort						HashKeySize;		// From NDIS_RECEIVE_SCALE_PARAMETERS
	unsigned char						HashSecretKey[40];	// rss key
	u32						HashInformation;
	// Receive buffer queues
	spinlock_t	RcvQLock;	/* Receive Queue Lock */
	struct LIST_ENTRY					FreeRcvBuffers;		// Free SXG_DATA_BUFFER queue
	struct LIST_ENTRY					FreeRcvBlocks;		// Free SXG_RCV_DESCRIPTOR_BLOCK Q
	struct LIST_ENTRY					AllRcvBlocks;		// All SXG_RCV_BLOCKs
	ushort						FreeRcvBufferCount;	// Number of free rcv data buffers
	ushort						FreeRcvBlockCount;	// # of free rcv descriptor blocks
	ushort						AllRcvBlockCount;	// Number of total receive blocks
	ushort						ReceiveBufferSize;	// SXG_RCV_DATA/JUMBO_BUFFER_SIZE only
	u32						AllocationsPending;	// Receive allocation pending
	u32						RcvBuffersOnCard;	// SXG_DATA_BUFFERS owned by card
	// SGL buffers
	spinlock_t	SglQLock;	/* SGL Queue Lock */
	struct LIST_ENTRY					FreeSglBuffers;		// Free SXG_SCATTER_GATHER
	struct LIST_ENTRY					AllSglBuffers;		// All SXG_SCATTER_GATHER
	ushort						FreeSglBufferCount;	// Number of free SGL buffers
	ushort						AllSglBufferCount;	// Number of total SGL buffers
	u32						CurrentTime;		// Tick count
	u32						FastpathConnections;// # of fastpath connections
	// Various single-bit flags:
	u32						BasicAllocations:1;	// Locks and listheads
	u32						IntRegistered:1;	// Interrupt registered
	u32						PingOutstanding:1;	// Ping outstanding to card
	u32						Dead:1;				// Card dead
	u32						DumpDriver:1;		// OID_SLIC_DRIVER_DUMP request
	u32						DumpCard:1;			// OID_SLIC_CARD_DUMP request
	u32						DumpCmdRunning:1;	// Dump command in progress
	u32						DebugRunning:1;		// AGDB debug in progress
	u32						JumboEnabled:1;		// Jumbo frames enabled
	u32						MsiEnabled:1;		// MSI interrupt enabled
	u32						RssEnabled:1;		// RSS Enabled
	u32						FailOnBadEeprom:1;	// Fail on Bad Eeprom
	u32						DiagStart:1;		// Init adapter for diagnostic start
	// Stats
	u32						PendingRcvCount;	// Outstanding rcv indications
	u32						PendingXmtCount;	// Outstanding send requests
	struct SXG_STATS				Stats;				// Statistics
	u32						ReassBufs;			// Number of reassembly buffers
	// Card Crash Info
	ushort						CrashLocation;		// Microcode crash location
	unsigned char						CrashCpu;			// Sahara CPU ID
	// Diagnostics
	//	PDIAG_CMD					DiagCmds;			// List of free diagnostic commands
	//	PDIAG_BUFFER				DiagBuffers;		// List of free diagnostic buffers
	//	PDIAG_REQ					DiagReqQ;			// List of outstanding (asynchronous) diag requests
	//	u32						DiagCmdTimeout;		// Time out for diag cmds (seconds) XXXTODO - replace with SXG_PARAM var?
	//	unsigned char						DiagDmaDesc[DMA_CPU_CTXS];		// Free DMA descriptors bit field (32 CPU ctx * 8 DMA ctx)

	/////////////////////////////////////////////////////////////////////
	// Put preprocessor-conditional fields at the end so we don't
	// have to recompile sxgdbg everytime we reconfigure the driver
	/////////////////////////////////////////////////////////////////////
	void *						PendingSetRss;		// Pending RSS parameter change
	u32						IPv4HdrSize;		// Shared 5.2/6.0 encap param
	unsigned char *          			InterruptInfo;		// Allocated by us during AddDevice
#if defined(CONFIG_X86)
	u32						AddrUpper;			// Upper 32 bits of 64-bit register
#endif
	//#if SXG_FAILURE_DUMP
	//	NDIS_EVENT					DumpThreadEvent;	// syncronize dump thread
	//	BOOLEAN						DumpThreadRunning;	// termination flag
	//	PSXG_DUMP_CMD				DumpBuffer;			// 68k - Cmd and Buffer
	//	dma_addr_t		PDumpBuffer;		// Physical address
	//#endif // SXG_FAILURE_DUMP

};

#if SLIC_DUMP_ENABLED
#define SLIC_DUMP_REQUESTED      1
#define SLIC_DUMP_IN_PROGRESS    2
#define SLIC_DUMP_DONE           3

/****************************************************************************
 *
 * Microcode crash information structure.  This
 * structure is written out to the card's SRAM when the microcode panic's.
 *
 ****************************************************************************/
struct slic_crash_info {
    ushort  cpu_id;
    ushort  crash_pc;
};

#define CRASH_INFO_OFFSET   0x155C

#endif

#define UPDATE_STATS(largestat, newstat, oldstat)                        \
{                                                                        \
    if ((newstat) < (oldstat))                                           \
        (largestat) += ((newstat) + (0xFFFFFFFF - oldstat + 1));         \
    else                                                                 \
        (largestat) += ((newstat) - (oldstat));                          \
}

#define UPDATE_STATS_GB(largestat, newstat, oldstat)                     \
{                                                                        \
    (largestat) += ((newstat) - (oldstat));                              \
}

#define ETHER_EQ_ADDR(_AddrA, _AddrB, _Result)                           \
{                                                                        \
    _Result = TRUE;                                                      \
    if (*(u32 *)(_AddrA) != *(u32 *)(_AddrB))                      \
        _Result = FALSE;                                                 \
    if (*(u16 *)(&((_AddrA)[4])) != *(u16 *)(&((_AddrB)[4])))        \
        _Result = FALSE;                                                 \
}

#define ETHERMAXFRAME   1514
#define JUMBOMAXFRAME   9014

#if defined(CONFIG_X86_64) || defined(CONFIG_IA64)
#define   SXG_GET_ADDR_LOW(_addr)  (u32)((u64)(_addr) & 0x00000000FFFFFFFF)
#define   SXG_GET_ADDR_HIGH(_addr)  (u32)(((u64)(_addr) >> 32) & 0x00000000FFFFFFFF)
#else
#define   SXG_GET_ADDR_LOW(_addr)   (u32)_addr
#define   SXG_GET_ADDR_HIGH(_addr)  (u32)0
#endif

#define FLUSH       TRUE
#define DONT_FLUSH  FALSE

#define SIOCSLICDUMPCARD         SIOCDEVPRIVATE+9
#define SIOCSLICSETINTAGG        SIOCDEVPRIVATE+10
#define SIOCSLICTRACEDUMP        SIOCDEVPRIVATE+11

#endif /*  __SXG_DRIVER_H__ */
