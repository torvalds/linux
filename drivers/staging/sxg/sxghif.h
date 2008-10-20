/*
 * Copyright © 1997-2007 Alacritech, Inc. All rights reserved
 *
 * $Id: sxghif.h,v 1.5 2008/07/24 19:18:22 chris Exp $
 *
 * sxghif.h:
 *
 * This file contains structures and definitions for the
 * Alacritech Sahara host interface
 */

/*******************************************************************************
 * UCODE Registers
 *******************************************************************************/
typedef struct _SXG_UCODE_REGS {
	// Address 0 - 0x3F = Command codes 0-15 for TCB 0.  Excode 0
	u32		Icr;			// Code = 0 (extended), ExCode = 0 - Int control
	u32		RsvdReg1;		// Code = 1 - TOE -NA
	u32		RsvdReg2;		// Code = 2 - TOE -NA
	u32		RsvdReg3;		// Code = 3 - TOE -NA
	u32		RsvdReg4;		// Code = 4 - TOE -NA
	u32		RsvdReg5;		// Code = 5 - TOE -NA
	u32		CardUp;			// Code = 6 - Microcode initialized when 1
	u32		RsvdReg7;		// Code = 7 - TOE -NA
	u32		CodeNotUsed[8];		// Codes 8-15 not used.  ExCode = 0
	// This brings us to ExCode 1 at address 0x40 = Interrupt status pointer
	u32		Isp;			// Code = 0 (extended), ExCode = 1
	u32		PadEx1[15];		// Codes 1-15 not used with extended codes
	// ExCode 2 = Interrupt Status Register
	u32		Isr;			// Code = 0 (extended), ExCode = 2
	u32		PadEx2[15];
	// ExCode 3 = Event base register.  Location of event rings
	u32		EventBase;		// Code = 0 (extended), ExCode = 3
	u32		PadEx3[15];
	// ExCode 4 = Event ring size
	u32		EventSize;		// Code = 0 (extended), ExCode = 4
	u32		PadEx4[15];
	// ExCode 5 = TCB Buffers base address
	u32		TcbBase;		// Code = 0 (extended), ExCode = 5
	u32		PadEx5[15];
	// ExCode 6 = TCB Composite Buffers base address
	u32		TcbCompBase;		// Code = 0 (extended), ExCode = 6
	u32		PadEx6[15];
	// ExCode 7 = Transmit ring base address
	u32		XmtBase;		// Code = 0 (extended), ExCode = 7
	u32		PadEx7[15];
	// ExCode 8 = Transmit ring size
	u32		XmtSize;		// Code = 0 (extended), ExCode = 8
	u32		PadEx8[15];
	// ExCode 9 = Receive ring base address
	u32		RcvBase;		// Code = 0 (extended), ExCode = 9
	u32		PadEx9[15];
	// ExCode 10 = Receive ring size
	u32		RcvSize;		// Code = 0 (extended), ExCode = 10
	u32		PadEx10[15];
	// ExCode 11 = Read EEPROM Config
	u32		Config;			// Code = 0 (extended), ExCode = 11
	u32		PadEx11[15];
	// ExCode 12 = Multicast bits 31:0
	u32		McastLow;		// Code = 0 (extended), ExCode = 12
	u32		PadEx12[15];
	// ExCode 13 = Multicast bits 63:32
	u32		McastHigh;		// Code = 0 (extended), ExCode = 13
	u32		PadEx13[15];
	// ExCode 14 = Ping
	u32		Ping;			// Code = 0 (extended), ExCode = 14
	u32		PadEx14[15];
	// ExCode 15 = Link MTU
	u32		LinkMtu;		// Code = 0 (extended), ExCode = 15
	u32		PadEx15[15];
	// ExCode 16 = Download synchronization
	u32		LoadSync;		// Code = 0 (extended), ExCode = 16
	u32		PadEx16[15];
	// ExCode 17 = Upper DRAM address bits on 32-bit systems
	u32		Upper;			// Code = 0 (extended), ExCode = 17
	u32		PadEx17[15];
	// ExCode 18 = Slowpath Send Index Address
	u32		SPSendIndex;		// Code = 0 (extended), ExCode = 18
	u32		PadEx18[15];
	u32		RsvdXF;			// Code = 0 (extended), ExCode = 19
	u32		PadEx19[15];
	// ExCode 20 = Aggregation
	u32		Aggregation;		// Code = 0 (extended), ExCode = 20
	u32		PadEx20[15];
	// ExCode 21 = Receive MDL push timer
	u32		PushTicks;		// Code = 0 (extended), ExCode = 21
	u32		PadEx21[15];
	// ExCode 22 = TOE NA
	u32		AckFrequency;		// Code = 0 (extended), ExCode = 22
	u32		PadEx22[15];
	// ExCode 23 = TOE NA
	u32		RsvdReg23;
	u32		PadEx23[15];
	// ExCode 24 = TOE NA
	u32		RsvdReg24;
	u32		PadEx24[15];
	// ExCode 25 = TOE NA
	u32		RsvdReg25;		// Code = 0 (extended), ExCode = 25
	u32		PadEx25[15];
	// ExCode 26 = Receive checksum requirements
	u32		ReceiveChecksum;	// Code = 0 (extended), ExCode = 26
	u32		PadEx26[15];
	// ExCode 27 = RSS Requirements
	u32		Rss;			// Code = 0 (extended), ExCode = 27
	u32		PadEx27[15];
	// ExCode 28 = RSS Table
	u32		RssTable;		// Code = 0 (extended), ExCode = 28
	u32		PadEx28[15];
	// ExCode 29 = Event ring release entries
	u32		EventRelease;		// Code = 0 (extended), ExCode = 29
	u32		PadEx29[15];
	// ExCode 30 = Number of receive bufferlist commands on ring 0
	u32		RcvCmd;			// Code = 0 (extended), ExCode = 30
	u32		PadEx30[15];
	// ExCode 31 = slowpath transmit command - Data[31:0] = 1
	u32		XmtCmd;			// Code = 0 (extended), ExCode = 31
	u32		PadEx31[15];
	// ExCode 32 = Dump command
	u32		DumpCmd;		// Code = 0 (extended), ExCode = 32
	u32		PadEx32[15];
	// ExCode 33 = Debug command
	u32		DebugCmd;		// Code = 0 (extended), ExCode = 33
	u32		PadEx33[15];
	// There are 128 possible extended commands - each of account for 16
	// words (including the non-relevent base command codes 1-15).
	// Pad for the remainder of these here to bring us to the next CPU
	// base.  As extended codes are added, reduce the first array value in
	// the following field
	u32		PadToNextCpu[94][16];	// 94 = 128 - 34 (34 = Excodes 0 - 33)
} SXG_UCODE_REGS, *PSXG_UCODE_REGS;

// Interrupt control register (0) values
#define SXG_ICR_DISABLE					0x00000000
#define SXG_ICR_ENABLE					0x00000001
#define SXG_ICR_MASK					0x00000002
#define SXG_ICR_MSGID_MASK				0xFFFF0000
#define SXG_ICR_MSGID_SHIFT			16
#define SXG_ICR(_MessageId, _Data)	\
	((((_MessageId) << SXG_ICR_MSGID_SHIFT) &	\
	  SXG_ICR_MSGID_MASK) | (_Data))

// The Microcode supports up to 16 RSS queues
#define SXG_MAX_RSS				16
#define SXG_MAX_RSS_TABLE_SIZE	256		// 256-byte max

#define SXG_RSS_TCP6				0x00000001	// RSS TCP over IPv6
#define SXG_RSS_TCP4				0x00000002	// RSS TCP over IPv4
#define SXG_RSS_LEGACY				0x00000004	// Line-base interrupts
#define SXG_RSS_TABLE_SIZE			0x0000FF00	// Table size mask
#define SXG_RSS_TABLE_SHIFT			8
#define	SXG_RSS_BASE_CPU			0x00FF0000	// Base CPU (not used)
#define SXG_RSS_BASE_SHIFT			16

#define SXG_RCV_IP_CSUM_ENABLED		0x00000001	// ExCode 26 (ReceiveChecksum)
#define SXG_RCV_TCP_CSUM_ENABLED	0x00000002	// ExCode 26 (ReceiveChecksum)

#define SXG_XMT_CPUID_SHIFT			16

#if VPCI
#define SXG_CHECK_FOR_HANG_TIME		3000
#else
#define SXG_CHECK_FOR_HANG_TIME		5
#endif

/*
 * TCB registers - This is really the same register memory area as UCODE_REGS
 * above, but defined differently.  Bits 17:06 of the address define the TCB,
 * which means each TCB area occupies 0x40 (64) bytes, or 16 u32S.  What really
 * is happening is that these registers occupy the "PadEx[15]" areas in the
 * SXG_UCODE_REGS definition above
 */
typedef struct _SXG_TCB_REGS {
	u32		ExCode;		/* Extended codes - see SXG_UCODE_REGS */
	u32		Xmt;		/* Code = 1 - # of Xmt descriptors added to ring */
	u32		Rcv;		/* Code = 2 - # of Rcv descriptors added to ring */
	u32		Rsvd1;		/* Code = 3 - TOE NA */
	u32		Rsvd2;		/* Code = 4 - TOE NA */
	u32		Rsvd3;		/* Code = 5 - TOE NA */
	u32		Invalid;	/* Code = 6 - Reserved for "CardUp" see above */
	u32		Rsvd4;		/* Code = 7 - TOE NA */
	u32		Rsvd5;		/* Code = 8 - TOE NA */
	u32		Pad[7];		/* Codes 8-15 - Not used. */
} SXG_TCB_REGS, *PSXG_TCB_REGS;

/***************************************************************************
 * ISR Format
 *                31                                      0
 *                 _______________________________________
 *                |    |    |    |    |    |    |    |    |
 *                |____|____|____|____|____|____|____|____|
 *                 ^^^^ ^^^^ ^^^^ ^^^^ \                 /
 *           ERR --|||| |||| |||| ||||  -----------------
 *         EVENT ---||| |||| |||| ||||          |
 *               ----|| |||| |||| ||||          |-- Crash Address
 *           UPC -----| |||| |||| ||||
 *        LEVENT -------|||| |||| ||||
 *          PDQF --------||| |||| ||||
 *         RMISS ---------|| |||| ||||
 *         BREAK ----------| |||| ||||
 *       HBEATOK ------------|||| ||||
 *       NOHBEAT -------------||| ||||
 *        ERFULL --------------|| ||||
 *         XDROP ---------------| ||||
 *               -----------------||||
 *               -----------------||||--\
 *                                 ||---|-CpuId of crash
 *                                 |----/
 ***************************************************************************/
#define SXG_ISR_ERR		0x80000000	// Error
#define SXG_ISR_EVENT		0x40000000	// Event ring event
#define SXG_ISR_NONE1		0x20000000	// Not used
#define SXG_ISR_UPC		0x10000000	// Dump/debug command complete
#define SXG_ISR_LINK		0x08000000	// Link event
#define SXG_ISR_PDQF		0x04000000	// Processed data queue full
#define SXG_ISR_RMISS		0x02000000	// Drop - no host buf
#define SXG_ISR_BREAK		0x01000000	// Breakpoint hit
#define SXG_ISR_PING		0x00800000	// Heartbeat response
#define SXG_ISR_DEAD		0x00400000	// Card crash
#define SXG_ISR_ERFULL		0x00200000	// Event ring full
#define SXG_ISR_XDROP		0x00100000	// XMT Drop - no DRAM bufs or XMT err
#define SXG_ISR_SPSEND		0x00080000	// Slow send complete
#define SXG_ISR_CPU		0x00070000	// Dead CPU mask
#define SXG_ISR_CPU_SHIFT		16	// Dead CPU shift
#define SXG_ISR_CRASH		0x0000FFFF	// Crash address mask

/***************************************************************************
 *
 * Event Ring entry
 *
 ***************************************************************************/
/*
 *  31                  15                 0
 *  .___________________.___________________.
 *  |<------------    Pad 0    ------------>|
 *  |_________|_________|_________|_________|0		0x00
 *  |<------------    Pad 1    ------------>|
 *  |_________|_________|_________|_________|4		0x04
 *  |<------------    Pad 2    ------------>|
 *  |_________|_________|_________|_________|8 		0x08
 *  |<----------- Event Word 0 ------------>|
 *  |_________|_________|_________|_________|12		0x0c
 *  |<----------- Event Word 1 ------------>|
 *  |_________|_________|_________|_________|16		0x10
 *  |<------------- Toeplitz   ------------>|
 *  |_________|_________|_________|_________|20		0x14
 *  |<----- Length ---->|<------ TCB Id --->|
 *  |_________|_________|_________|_________|24		0x18
 *  |<----- Status ---->|Evnt Code|Flsh Code|
 *  |_________|_________|_________|_________|28		0x1c
 *   ^                   ^^^^ ^^^^
 *   |- VALID            |||| ||||- RBUFC
 *                       |||| |||-- SLOWR
 *                       |||| ||--- UNUSED
 *                       |||| |---- FASTC
 *                       ||||------ FASTR
 *                       |||-------
 *                       ||--------
 *                       |---------
 *
 * Slowpath status:
 *   _______________________________________
 *  |<----- Status ---->|Evnt Code|Flsh Code|
 *  |_________|Cmd Index|_________|_________|28		0x1c
 *    ^^^ ^^^^
 *    ||| ||||- ISTCPIP6
 *    ||| |||-- IPONLY
 *    ||| ||--- RCVERR
 *    ||| |---- IPCBAD
 *    |||------ TCPCBAD
 *    ||------- ISTCPIP
 *    |-------- SCERR
 *
 */
#pragma pack(push, 1)
typedef struct _SXG_EVENT {
	u32			Pad[1];		// not used
	u32			SndUna;		// SndUna value
	u32			Resid;		// receive MDL resid
	union {
		void *		HostHandle;	// Receive host handle
		u32		Rsvd1;		// TOE NA
		struct {
			u32	NotUsed;
			u32	Rsvd2;		// TOE NA
		} Flush;
	};
	u32			Toeplitz;	// RSS Toeplitz hash
	union {
		ushort		Rsvd3;		// TOE NA
		ushort		HdrOffset;	// Slowpath
	};
	ushort			Length;		//
	unsigned char 		Rsvd4;		// TOE NA
	unsigned char 		Code;		// Event code
	unsigned char		CommandIndex;	// New ring index
	unsigned char		Status;		// Event status
} SXG_EVENT, *PSXG_EVENT;
#pragma pack(pop)

// Event code definitions
#define EVENT_CODE_BUFFERS	0x01	// Receive buffer list command (ring 0)
#define EVENT_CODE_SLOWRCV	0x02	// Slowpath receive
#define EVENT_CODE_UNUSED	0x04	// Was slowpath commands complete

// Status values
#define EVENT_STATUS_VALID	0x80	// Entry valid

// Slowpath status
#define EVENT_STATUS_ERROR	0x40	// Completed with error. Index in next byte
#define EVENT_STATUS_TCPIP4	0x20	// TCPIPv4 frame
#define EVENT_STATUS_TCPBAD	0x10	// Bad TCP checksum
#define EVENT_STATUS_IPBAD	0x08	// Bad IP checksum
#define EVENT_STATUS_RCVERR	0x04	// Slowpath receive error
#define EVENT_STATUS_IPONLY	0x02	// IP frame
#define EVENT_STATUS_TCPIP6	0x01	// TCPIPv6 frame
#define EVENT_STATUS_TCPIP	0x21	// Combination of v4 and v6

// Event ring
// Size must be power of 2, between 128 and 16k
#define EVENT_RING_SIZE		4096	// ??
#define EVENT_RING_BATCH	16		// Hand entries back 16 at a time.
#define EVENT_BATCH_LIMIT	256	    // Stop processing events after 256 (16 * 16)

typedef struct _SXG_EVENT_RING {
	SXG_EVENT	Ring[EVENT_RING_SIZE];
}SXG_EVENT_RING, *PSXG_EVENT_RING;

/***************************************************************************
 *
 * TCB Buffers
 *
 ***************************************************************************/
// Maximum number of TCBS supported by hardware/microcode
#define SXG_MAX_TCB		4096
// Minimum TCBs before we fail initialization
#define SXG_MIN_TCB		512
// TCB Hash
// The bucket is determined by bits 11:4 of the toeplitz if we support 4k
// offloaded connections, 10:4 if we support 2k and so on.
#define SXG_TCB_BUCKET_SHIFT	4
#define SXG_TCB_PER_BUCKET		16
#define SXG_TCB_BUCKET_MASK		0xFF0	// Bucket portion of TCB ID
#define SXG_TCB_ELEMENT_MASK	0x00F	// Element within bucket
#define SXG_TCB_BUCKETS			256		// 256 * 16 = 4k

#define SXG_TCB_BUFFER_SIZE	512	// ASSERT format is correct

#define SXG_TCB_RCVQ_SIZE		736

#define SXG_TCB_COMPOSITE_BUFFER_SIZE	1024

#define SXG_LOCATE_TCP_FRAME_HDR(_TcpObject, _IPv6)							\
	(((_TcpObject)->VlanId) ?												\
	 ((_IPv6) ?		/* Vlan frame header = yes */							\
	  &(_TcpObject)->CompBuffer->Frame.HasVlan.TcpIp6.SxgTcp			:	\
	  &(_TcpObject)->CompBuffer->Frame.HasVlan.TcpIp.SxgTcp)			: 	\
	 ((_IPv6) ?		/* Vlan frame header = No */							\
	  &(_TcpObject)->CompBuffer->Frame.NoVlan.TcpIp6.SxgTcp				: 	\
	  &(_TcpObject)->CompBuffer->Frame.NoVlan.TcpIp.SxgTcp))

#define SXG_LOCATE_IP_FRAME_HDR(_TcpObject)									\
	(_TcpObject)->VlanId ?													\
	&(_TcpObject)->CompBuffer->Frame.HasVlan.TcpIp.Ip				: 		\
	&(_TcpObject)->CompBuffer->Frame.NoVlan.TcpIp.Ip

#define SXG_LOCATE_IP6_FRAME_HDR(_TcpObject)								\
	(_TcpObject)->VlanId ?													\
	&(_TcpObject)->CompBuffer->Frame.HasVlan.TcpIp6.Ip				:		\
	&(_TcpObject)->CompBuffer->Frame.NoVlan.TcpIp6.Ip


#if DBG
// Horrible kludge to distinguish dumb-nic, slowpath, and
// fastpath traffic.  Decrement the HopLimit by one
// for slowpath, two for fastpath.  This assumes the limit is measurably
// greater than two, which I think is reasonable.
// Obviously this is DBG only.  Maybe remove later, or #if 0 so we
// can set it when needed
#define SXG_DBG_HOP_LIMIT(_TcpObject, _FastPath) {								\
	PIPV6_HDR		_Ip6FrameHdr;												\
	if((_TcpObject)->IPv6) {													\
		_Ip6FrameHdr = SXG_LOCATE_IP6_FRAME_HDR((_TcpObject));					\
		if(_FastPath) {															\
			_Ip6FrameHdr->HopLimit = (_TcpObject)->Cached.TtlOrHopLimit - 2;	\
		} else {																\
			_Ip6FrameHdr->HopLimit = (_TcpObject)->Cached.TtlOrHopLimit - 1;	\
		}																		\
	}																			\
}
#else
// Do nothing with free build
#define SXG_DBG_HOP_LIMIT(_TcpObject, _FastPath)
#endif

/***************************************************************************
 * Receive and transmit rings
 ***************************************************************************/
#define SXG_MAX_RING_SIZE	256
#define SXG_XMT_RING_SIZE	128		// Start with 128
#define SXG_RCV_RING_SIZE	128		// Start with 128
#define SXG_MAX_ENTRIES     4096

// Structure and macros to manage a ring
typedef struct _SXG_RING_INFO {
	unsigned char			Head;		// Where we add entries - Note unsigned char:RING_SIZE
	unsigned char			Tail;		// Where we pull off completed entries
	ushort			Size;		// Ring size - Must be multiple of 2
	void *			Context[SXG_MAX_RING_SIZE];	// Shadow ring
} SXG_RING_INFO, *PSXG_RING_INFO;

#define SXG_INITIALIZE_RING(_ring, _size) {							\
	(_ring).Head = 0;												\
	(_ring).Tail = 0;												\
	(_ring).Size = (_size);											\
}
#define SXG_ADVANCE_INDEX(_index, _size) ((_index) = ((_index) + 1) & ((_size) - 1))
#define SXG_PREVIOUS_INDEX(_index, _size) (((_index) - 1) &((_size) - 1))
#define SXG_RING_EMPTY(_ring) ((_ring)->Head == (_ring)->Tail)
#define SXG_RING_FULL(_ring) ((((_ring)->Head + 1) & ((_ring)->Size - 1)) == (_ring)->Tail)
#define SXG_RING_ADVANCE_HEAD(_ring) SXG_ADVANCE_INDEX((_ring)->Head, ((_ring)->Size))
#define SXG_RING_RETREAT_HEAD(_ring) ((_ring)->Head =				\
									  SXG_PREVIOUS_INDEX((_ring)->Head, (_ring)->Size))
#define SXG_RING_ADVANCE_TAIL(_ring) {								\
	ASSERT((_ring)->Tail != (_ring)->Head);							\
	SXG_ADVANCE_INDEX((_ring)->Tail, ((_ring)->Size));				\
}
// Set cmd to the next available ring entry, set the shadow context
// entry and advance the ring.
// The appropriate lock must be held when calling this macro
#define SXG_GET_CMD(_ring, _ringinfo, _cmd, _context) {				\
	if(SXG_RING_FULL(_ringinfo)) {									\
		(_cmd) = NULL;												\
	} else {														\
		(_cmd) = &(_ring)->Descriptors[(_ringinfo)->Head];			\
		(_ringinfo)->Context[(_ringinfo)->Head] = (void *)(_context);\
		SXG_RING_ADVANCE_HEAD(_ringinfo);							\
	}																\
}

// Abort the previously allocated command by retreating the head.
// NOTE - The appopriate lock MUST NOT BE DROPPED between the SXG_GET_CMD
// and SXG_ABORT_CMD calls.
#define SXG_ABORT_CMD(_ringinfo) {									\
	ASSERT(!(SXG_RING_EMPTY(_ringinfo)));							\
	SXG_RING_RETREAT_HEAD(_ringinfo);								\
	(_ringinfo)->Context[(_ringinfo)->Head] = NULL;					\
}

// For the given ring, return a pointer to the tail cmd and context,
// clear the context and advance the tail
#define SXG_RETURN_CMD(_ring, _ringinfo, _cmd, _context) {			\
	(_cmd) = &(_ring)->Descriptors[(_ringinfo)->Tail];				\
	(_context) = (_ringinfo)->Context[(_ringinfo)->Tail];       	\
	(_ringinfo)->Context[(_ringinfo)->Tail] = NULL;					\
	SXG_RING_ADVANCE_TAIL(_ringinfo);								\
}

/***************************************************************************
 *
 * Host Command Buffer - commands to INIC via the Cmd Rings
 *
 ***************************************************************************/
/*
 *  31                  15                 0
 *  .___________________.___________________.
 *  |<-------------- Sgl Low -------------->|
 *  |_________|_________|_________|_________|0		0x00
 *  |<-------------- Sgl High ------------->|
 *  |_________|_________|_________|_________|4		0x04
 *  |<-------------  Sge 0 Low  ----------->|
 *  |_________|_________|_________|_________|8 		0x08
 *  |<-------------  Sge 0 High ----------->|
 *  |_________|_________|_________|_________|12		0x0c
 *  |<------------  Sge 0 Length ---------->|
 *  |_________|_________|_________|_________|16		0x10
 *  |<----------- Window Update ----------->|
 *  |<-------- SP 1st SGE offset ---------->|
 *  |_________|_________|_________|_________|20		0x14
 *  |<----------- Total Length ------------>|
 *  |_________|_________|_________|_________|24		0x18
 *  |<----- LCnt ------>|<----- Flags ----->|
 *  |_________|_________|_________|_________|28		0x1c
 */
#pragma pack(push, 1)
typedef struct _SXG_CMD {
	dma_addr_t             			Sgl;			// Physical address of SGL
	union {
		struct {
			dma64_addr_t          	FirstSgeAddress;// Address of first SGE
			u32					FirstSgeLength;	// Length of first SGE
			union {
				u32				Rsvd1;	        // TOE NA
				u32				SgeOffset;		// Slowpath - 2nd SGE offset
				u32				Resid;			// MDL completion - clobbers update
			};
			union {
				u32				TotalLength;	// Total transfer length
				u32				Mss;			// LSO MSS
			};
		} Buffer;
	};
	union {
		struct {
			unsigned char					Flags:4;		// slowpath flags
			unsigned char					IpHl:4;			// Ip header length (>>2)
			unsigned char					MacLen;			// Mac header len
		} CsumFlags;
		struct {
			ushort					Flags:4;		// slowpath flags
			ushort					TcpHdrOff:7;	// TCP
			ushort					MacLen:5;		// Mac header len
		} LsoFlags;
		ushort						Flags;			// flags
	};
	union {
		ushort						SgEntries;		// SG entry count including first sge
		struct {
			unsigned char					Status;		    // Copied from event status
			unsigned char					NotUsed;
		} Status;
	};
} SXG_CMD, *PSXG_CMD;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _VLAN_HDR {
	ushort	VlanTci;
	ushort	VlanTpid;
} VLAN_HDR, *PVLAN_HDR;
#pragma pack(pop)

/*
 * Slowpath Flags:
 *
 *
 * LSS Flags:
 *                                        .---
 *                                       /.--- TCP Large segment send
 *                                      //.---
 *                                     ///.---
 *  3                   1     1       ////
 *  1                   5     0      ||||
 *  .___________________.____________vvvv.
 *  |                   |MAC |  TCP |    |
 *  |      LCnt         |hlen|hdroff|Flgs|
 *  |___________________|||||||||||||____|
 *
 *
 * Checksum Flags
 *
 *                                           .---
 *                                          /.---
 *                                         //.--- Checksum TCP
 *                                        ///.--- Checksum IP
 *  3                   1                //// No bits - normal send
 *  1                   5          7    ||||
 *  .___________________._______________vvvv.
 *  |                   | Offload | IP |    |
 *  |      LCnt         |MAC hlen |Hlen|Flgs|
 *  |___________________|____|____|____|____|
 *
 */
// Slowpath CMD flags
#define SXG_SLOWCMD_CSUM_IP			0x01		// Checksum IP
#define SXG_SLOWCMD_CSUM_TCP		0x02		// Checksum TCP
#define SXG_SLOWCMD_LSO				0x04		// Large segment send

typedef struct _SXG_XMT_RING {
	SXG_CMD		Descriptors[SXG_XMT_RING_SIZE];
} SXG_XMT_RING, *PSXG_XMT_RING;

typedef struct _SXG_RCV_RING {
	SXG_CMD		Descriptors[SXG_RCV_RING_SIZE];
} SXG_RCV_RING, *PSXG_RCV_RING;

/***************************************************************************
 * Share memory buffer types - Used to identify asynchronous
 * shared memory allocation
 ***************************************************************************/
typedef enum {
	SXG_BUFFER_TYPE_RCV,		// Receive buffer
	SXG_BUFFER_TYPE_SGL			// SGL buffer
} SXG_BUFFER_TYPE;

// State for SXG buffers
#define SXG_BUFFER_FREE		0x01
#define SXG_BUFFER_BUSY		0x02
#define SXG_BUFFER_ONCARD	0x04
#define SXG_BUFFER_UPSTREAM	0x08

/***************************************************************************
 * Receive data buffers
 *
 * Receive data buffers are given to the Sahara card 128 at a time.
 * This is accomplished by filling in a "receive descriptor block"
 * with 128 "receive descriptors".  Each descriptor consists of
 * a physical address, which the card uses as the address to
 * DMA data into, and a virtual address, which is given back
 * to the host in the "HostHandle" portion of an event.
 * The receive descriptor data structure is defined below
 * as SXG_RCV_DATA_DESCRIPTOR, and the corresponding block
 * is defined as SXG_RCV_DESCRIPTOR_BLOCK.
 *
 * This receive descriptor block is given to the card by filling
 * in the Sgl field of a SXG_CMD entry from pAdapt->RcvRings[0]
 * with the physical address of the receive descriptor block.
 *
 * Both the receive buffers and the receive descriptor blocks
 * require additional data structures to maintain them
 * on a free queue and contain other information associated with them.
 * Those data structures are defined as the SXG_RCV_DATA_BUFFER_HDR
 * and SXG_RCV_DESCRIPTOR_BLOCK_HDR respectively.
 *
 * Since both the receive buffers and the receive descriptor block
 * must be accessible by the card, both must be allocated out of
 * shared memory.  To ensure that we always have a descriptor
 * block available for every 128 buffers, we allocate all of
 * these resources together in a single block.  This entire
 * block is managed by a SXG_RCV_BLOCK_HDR, who's sole purpose
 * is to maintain address information so that the entire block
 * can be free later.
 *
 * Further complicating matters is the fact that the receive
 * buffers must be variable in length in order to accomodate
 * jumbo frame configurations.  We configure the buffer
 * length so that the buffer and it's corresponding SXG_RCV_DATA_BUFFER_HDR
 * structure add up to an even boundary.  Then we place the
 * remaining data structures after 128 of them as shown in
 * the following diagram:
 *
 *  _________________________________________
 * |                                         |
 * |    Variable length receive buffer #1    |
 * |_________________________________________|
 * |                                         |
 * |       SXG_RCV_DATA_BUFFER_HDR #1        |
 * |_________________________________________| <== Even 2k or 10k boundary
 * |                                         |
 * |         ... repeat 2-128 ..             |
 * |_________________________________________|
 * |                                         |
 * |      SXG_RCV_DESCRIPTOR_BLOCK           |
 * |  Contains SXG_RCV_DATA_DESCRIPTOR * 128 |
 * |_________________________________________|
 * |                                         |
 * |      SXG_RCV_DESCRIPTOR_BLOCK_HDR       |
 * |_________________________________________|
 * |                                         |
 * |          SXG_RCV_BLOCK_HDR              |
 * |_________________________________________|
 *
 * Memory consumption:
 *	  Non-jumbo:
 *      Buffers and SXG_RCV_DATA_BUFFER_HDR = 2k * 128 = 256k
 *    + SXG_RCV_DESCRIPTOR_BLOCK = 2k
 *    + SXG_RCV_DESCRIPTOR_BLOCK_HDR = ~32
 *    + SXG_RCV_BLOCK_HDR = ~32
 *    => Total = ~258k/block
 *
 *	  Jumbo:
 *      Buffers and SXG_RCV_DATA_BUFFER_HDR = 10k * 128 = 1280k
 *    + SXG_RCV_DESCRIPTOR_BLOCK = 2k
 *    + SXG_RCV_DESCRIPTOR_BLOCK_HDR = ~32
 *    + SXG_RCV_BLOCK_HDR = ~32
 *    => Total = ~1282k/block
 *
 ***************************************************************************/
#define SXG_RCV_DATA_BUFFERS			4096	// Amount to give to the card
#define SXG_INITIAL_RCV_DATA_BUFFERS	8192	// Initial pool of buffers
#define SXG_MIN_RCV_DATA_BUFFERS		2048	// Minimum amount and when to get more
#define SXG_MAX_RCV_BLOCKS				128		// = 16384 receive buffers

// Receive buffer header
typedef struct _SXG_RCV_DATA_BUFFER_HDR {
	dma_addr_t          			PhysicalAddress;	// Buffer physical address
	// Note - DO NOT USE the VirtualAddress field to locate data.
	// Use the sxg.h:SXG_RECEIVE_DATA_LOCATION macro instead.
	void *VirtualAddress;		// Start of buffer
	LIST_ENTRY						FreeList;			// Free queue of buffers
	struct _SXG_RCV_DATA_BUFFER_HDR	*Next;				// Fastpath data buffer queue
	u32							Size;				// Buffer size
	u32							ByteOffset;			// See SXG_RESTORE_MDL_OFFSET
	unsigned char							State;				// See SXG_BUFFER state above
	unsigned char							Status;				// Event status (to log PUSH)
	struct sk_buff                * skb;				// Double mapped (nbl and pkt)
} SXG_RCV_DATA_BUFFER_HDR, *PSXG_RCV_DATA_BUFFER_HDR;

// SxgSlowReceive uses the PACKET (skb) contained
// in the SXG_RCV_DATA_BUFFER_HDR when indicating dumb-nic data
#define SxgDumbRcvPacket	        skb

#define SXG_RCV_DATA_HDR_SIZE			256		// Space for SXG_RCV_DATA_BUFFER_HDR
#define SXG_RCV_DATA_BUFFER_SIZE		2048	// Non jumbo = 2k including HDR
#define SXG_RCV_JUMBO_BUFFER_SIZE		10240	// jumbo = 10k including HDR

// Receive data descriptor
typedef struct _SXG_RCV_DATA_DESCRIPTOR {
	union {
		struct sk_buff    *	VirtualAddress;			// Host handle
		u64 			ForceTo8Bytes;			// Force x86 to 8-byte boundary
	};
	dma_addr_t             	PhysicalAddress;
} SXG_RCV_DATA_DESCRIPTOR, *PSXG_RCV_DATA_DESCRIPTOR;

// Receive descriptor block
#define SXG_RCV_DESCRIPTORS_PER_BLOCK		128
#define SXG_RCV_DESCRIPTOR_BLOCK_SIZE		2048	// For sanity check
typedef struct _SXG_RCV_DESCRIPTOR_BLOCK {
	SXG_RCV_DATA_DESCRIPTOR		Descriptors[SXG_RCV_DESCRIPTORS_PER_BLOCK];
} SXG_RCV_DESCRIPTOR_BLOCK, *PSXG_RCV_DESCRIPTOR_BLOCK;

// Receive descriptor block header
typedef struct _SXG_RCV_DESCRIPTOR_BLOCK_HDR {
	void *					VirtualAddress;			// Start of 2k buffer
	dma_addr_t	            PhysicalAddress;		// ..and it's physical address
	LIST_ENTRY				FreeList;				// Free queue of descriptor blocks
	unsigned char					State;					// See SXG_BUFFER state above
} SXG_RCV_DESCRIPTOR_BLOCK_HDR, *PSXG_RCV_DESCRIPTOR_BLOCK_HDR;

// Receive block header
typedef struct _SXG_RCV_BLOCK_HDR {
	void *					VirtualAddress;			// Start of virtual memory
	dma_addr_t	            PhysicalAddress;		// ..and it's physical address
	LIST_ENTRY				AllList;				// Queue of all SXG_RCV_BLOCKS
} SXG_RCV_BLOCK_HDR, *PSXG_RCV_BLOCK_HDR;

// Macros to determine data structure offsets into receive block
#define SXG_RCV_BLOCK_SIZE(_Buffersize) 					\
	(((_Buffersize) * SXG_RCV_DESCRIPTORS_PER_BLOCK) +		\
	 (sizeof(SXG_RCV_DESCRIPTOR_BLOCK))              +		\
	 (sizeof(SXG_RCV_DESCRIPTOR_BLOCK_HDR))          +		\
	 (sizeof(SXG_RCV_BLOCK_HDR)))
#define SXG_RCV_BUFFER_DATA_SIZE(_Buffersize)				\
	((_Buffersize) - SXG_RCV_DATA_HDR_SIZE)
#define SXG_RCV_DATA_BUFFER_HDR_OFFSET(_Buffersize)			\
	((_Buffersize) - SXG_RCV_DATA_HDR_SIZE)
#define SXG_RCV_DESCRIPTOR_BLOCK_OFFSET(_Buffersize)		\
	((_Buffersize) * SXG_RCV_DESCRIPTORS_PER_BLOCK)
#define SXG_RCV_DESCRIPTOR_BLOCK_HDR_OFFSET(_Buffersize)	\
	(((_Buffersize) * SXG_RCV_DESCRIPTORS_PER_BLOCK) +		\
	 (sizeof(SXG_RCV_DESCRIPTOR_BLOCK)))
#define SXG_RCV_BLOCK_HDR_OFFSET(_Buffersize)				\
	(((_Buffersize) * SXG_RCV_DESCRIPTORS_PER_BLOCK) +		\
	 (sizeof(SXG_RCV_DESCRIPTOR_BLOCK))              +		\
	 (sizeof(SXG_RCV_DESCRIPTOR_BLOCK_HDR)))

// Use the miniport reserved portion of the NBL to locate
// our SXG_RCV_DATA_BUFFER_HDR structure.
typedef struct _SXG_RCV_NBL_RESERVED {
	PSXG_RCV_DATA_BUFFER_HDR	RcvDataBufferHdr;
	void *						Available;
} SXG_RCV_NBL_RESERVED, *PSXG_RCV_NBL_RESERVED;

#define SXG_RCV_NBL_BUFFER_HDR(_NBL) (((PSXG_RCV_NBL_RESERVED)NET_BUFFER_LIST_MINIPORT_RESERVED(_NBL))->RcvDataBufferHdr)

/***************************************************************************
 * Scatter gather list buffer
 ***************************************************************************/
#define SXG_INITIAL_SGL_BUFFERS		8192	// Initial pool of SGL buffers
#define SXG_MIN_SGL_BUFFERS			2048	// Minimum amount and when to get more
#define SXG_MAX_SGL_BUFFERS			16384	// Maximum to allocate (note ADAPT:ushort)


// Self identifying structure type
typedef enum _SXG_SGL_TYPE {
	SXG_SGL_DUMB,				// Dumb NIC SGL
	SXG_SGL_SLOW,				// Slowpath protocol header - see below
	SXG_SGL_CHIMNEY				// Chimney offload SGL
} SXG_SGL_TYPE, PSXG_SGL_TYPE;

// Note - the description below is Microsoft specific
//
// The following definition specifies the amount of shared memory to allocate
// for the SCATTER_GATHER_LIST portion of the SXG_SCATTER_GATHER data structure.
// The following considerations apply when setting this value:
// - First, the Sahara card is designed to read the Microsoft SGL structure
// 	 straight out of host memory.  This means that the SGL must reside in
//	 shared memory.  If the length here is smaller than the SGL for the
//	 NET_BUFFER, then NDIS will allocate its own buffer.  The buffer
//	 that NDIS allocates is not in shared memory, so when this happens,
//	 the SGL will need to be copied to a set of SXG_SCATTER_GATHER buffers.
//	 In other words.. we don't want this value to be too small.
// - On the other hand.. we're allocating up to 16k of these things.  If
//	 we make this too big, we start to consume a ton of memory..
// At the moment, I'm going to limit the number of SG entries to 150.
// If each entry maps roughly 4k, then this should cover roughly 600kB
// NET_BUFFERs.  Furthermore, since each entry is 24 bytes, the total
// SGE portion of the structure consumes 3600 bytes, which should allow
// the entire SXG_SCATTER_GATHER structure to reside comfortably within
// a 4k block, providing the remaining fields stay under 500 bytes.
//
// So with 150 entries, the SXG_SCATTER_GATHER structure becomes roughly
// 4k.  At 16k of them, that amounts to 64M of shared memory.  A ton, but
// manageable.
#define SXG_SGL_ENTRIES		150

// The ucode expects an NDIS SGL structure that
// is formatted for an x64 system.  When running
// on an x64 system, we can simply hand the NDIS SGL
// to the card directly.  For x86 systems we must reconstruct
// the SGL.  The following structure defines an x64
// formatted SGL entry
typedef struct _SXG_X64_SGE {
    dma64_addr_t      	Address;	// same as wdm.h
    u32				Length;		// same as wdm.h
	u32				CompilerPad;// The compiler pads to 8-bytes
    u64 			Reserved;	// u32 * in wdm.h.  Force to 8 bytes
} SXG_X64_SGE, *PSXG_X64_SGE;

typedef struct _SCATTER_GATHER_ELEMENT {
    dma64_addr_t      	Address;	// same as wdm.h
    u32				Length;		// same as wdm.h
	u32				CompilerPad;// The compiler pads to 8-bytes
    u64 			Reserved;	// u32 * in wdm.h.  Force to 8 bytes
} SCATTER_GATHER_ELEMENT, *PSCATTER_GATHER_ELEMENT;


typedef struct _SCATTER_GATHER_LIST {
    u32					NumberOfElements;
    u32 *				Reserved;
    SCATTER_GATHER_ELEMENT	Elements[];
} SCATTER_GATHER_LIST, *PSCATTER_GATHER_LIST;

// The card doesn't care about anything except elements, so
// we can leave the u32 * reserved field alone in the following
// SGL structure.  But redefine from wdm.h:SCATTER_GATHER_LIST so
// we can specify SXG_X64_SGE and define a fixed number of elements
typedef struct _SXG_X64_SGL {
    u32					NumberOfElements;
    u32 *				Reserved;
    SXG_X64_SGE				Elements[SXG_SGL_ENTRIES];
} SXG_X64_SGL, *PSXG_X64_SGL;

typedef struct _SXG_SCATTER_GATHER {
	SXG_SGL_TYPE						Type;			// FIRST! Dumb-nic or offload
	void *   							adapter;		// Back pointer to adapter
	LIST_ENTRY							FreeList;		// Free SXG_SCATTER_GATHER blocks
	LIST_ENTRY							AllList;		// All SXG_SCATTER_GATHER blocks
	dma_addr_t				            PhysicalAddress;// physical address
	unsigned char								State;			// See SXG_BUFFER state above
	unsigned char								CmdIndex;		// Command ring index
	struct sk_buff                    *	DumbPacket;		// Associated Packet
	u32								Direction;		// For asynchronous completions
	u32								CurOffset;		// Current SGL offset
	u32								SglRef;			// SGL reference count
	VLAN_HDR							VlanTag;		// VLAN tag to be inserted into SGL
	PSCATTER_GATHER_LIST   				pSgl;			// SGL Addr. Possibly &Sgl
	SXG_X64_SGL							Sgl;			// SGL handed to card
} SXG_SCATTER_GATHER, *PSXG_SCATTER_GATHER;

#if defined(CONFIG_X86_64)
#define SXG_SGL_BUFFER(_SxgSgl)		(&_SxgSgl->Sgl)
#define SXG_SGL_BUF_SIZE			sizeof(SXG_X64_SGL)
#elif defined(CONFIG_X86)
// Force NDIS to give us it's own buffer so we can reformat to our own
#define SXG_SGL_BUFFER(_SxgSgl)		NULL
#define SXG_SGL_BUF_SIZE			0
#else
    Stop Compilation;
#endif

