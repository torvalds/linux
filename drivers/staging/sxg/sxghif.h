/*******************************************************************
 * Copyright © 1997-2007 Alacritech, Inc. All rights reserved
 *
 * $Id: sxghif.h,v 1.5 2008/07/24 19:18:22 chris Exp $
 *
 * sxghif.h:
 *
 * This file contains structures and definitions for the
 * Alacritech Sahara host interface
 ******************************************************************/

#define DBG				1

/* UCODE Registers */
struct sxg_ucode_regs {
	/* Address 0 - 0x3F = Command codes 0-15 for TCB 0.  Excode 0 */
	u32	Icr;		/* Code = 0 (extended), ExCode = 0 - Int control */
	u32	RsvdReg1;	/* Code = 1 - TOE -NA */
	u32	RsvdReg2;	/* Code = 2 - TOE -NA */
	u32	RsvdReg3;	/* Code = 3 - TOE -NA */
	u32	RsvdReg4;	/* Code = 4 - TOE -NA */
	u32	RsvdReg5;	/* Code = 5 - TOE -NA */
	u32	CardUp;		/* Code = 6 - Microcode initialized when 1 */
	u32	RsvdReg7;	/* Code = 7 - TOE -NA */
	u32	ConfigStat;     /* Code = 8 - Configuration data load status */
	u32  	RsvdReg9;	/* Code = 9 - TOE -NA */
	u32	CodeNotUsed[6];	/* Codes 10-15 not used.  ExCode = 0 */
	/* This brings us to ExCode 1 at address 0x40 = Interrupt status pointer */
	u32	Isp;		/* Code = 0 (extended), ExCode = 1 */
	u32	PadEx1[15];	/* Codes 1-15 not used with extended codes */
	/* ExCode 2 = Interrupt Status Register */
	u32	Isr;		/* Code = 0 (extended), ExCode = 2 */
	u32 PadEx2[15];
	/* ExCode 3 = Event base register.  Location of event rings */
	u32	EventBase;	/* Code = 0 (extended), ExCode = 3 */
	u32 PadEx3[15];
	/* ExCode 4 = Event ring size */
	u32	EventSize;	/* Code = 0 (extended), ExCode = 4 */
	u32 PadEx4[15];
	/* ExCode 5 = TCB Buffers base address */
	u32	TcbBase;	/* Code = 0 (extended), ExCode = 5 */
	u32 PadEx5[15];
	/* ExCode 6 = TCB Composite Buffers base address */
	u32	TcbCompBase;	/* Code = 0 (extended), ExCode = 6 */
	u32 PadEx6[15];
	/* ExCode 7 = Transmit ring base address */
	u32	XmtBase;	/* Code = 0 (extended), ExCode = 7 */
	u32 PadEx7[15];
	/* ExCode 8 = Transmit ring size */
	u32	XmtSize;	/* Code = 0 (extended), ExCode = 8 */
	u32 PadEx8[15];
	/* ExCode 9 = Receive ring base address */
	u32	RcvBase;	/* Code = 0 (extended), ExCode = 9 */
	u32 PadEx9[15];
	/* ExCode 10 = Receive ring size */
	u32	RcvSize;	/* Code = 0 (extended), ExCode = 10 */
	u32 PadEx10[15];
	/* ExCode 11 = Read EEPROM/Flash Config */
	u32	Config;		/* Code = 0 (extended), ExCode = 11 */
	u32 PadEx11[15];
	/* ExCode 12 = Multicast bits 31:0 */
	u32	McastLow;	/* Code = 0 (extended), ExCode = 12 */
	u32 PadEx12[15];
	/* ExCode 13 = Multicast bits 63:32 */
	u32	McastHigh;	/* Code = 0 (extended), ExCode = 13 */
	u32 PadEx13[15];
	/* ExCode 14 = Ping */
	u32	Ping;		/* Code = 0 (extended), ExCode = 14 */
	u32 PadEx14[15];
	/* ExCode 15 = Link MTU */
	u32	LinkMtu;	/* Code = 0 (extended), ExCode = 15 */
	u32 PadEx15[15];
	/* ExCode 16 = Download synchronization */
	u32	LoadSync;	/* Code = 0 (extended), ExCode = 16 */
	u32 PadEx16[15];
	/* ExCode 17 = Upper DRAM address bits on 32-bit systems */
	u32	Upper;		/* Code = 0 (extended), ExCode = 17 */
	u32 PadEx17[15];
	/* ExCode 18 = Slowpath Send Index Address */
	u32	SPSendIndex;	/* Code = 0 (extended), ExCode = 18 */
	u32 PadEx18[15];
	/* ExCode 19 = Get ucode statistics */
	u32	GetUcodeStats;	/* Code = 0 (extended), ExCode = 19 */
	u32 PadEx19[15];
	/* ExCode 20 = Aggregation - See sxgmisc.c:SxgSetInterruptAggregation */
	u32	Aggregation;	/* Code = 0 (extended), ExCode = 20 */
	u32 PadEx20[15];
	/* ExCode 21 = Receive MDL push timer */
	u32	PushTicks;	/* Code = 0 (extended), ExCode = 21 */
	u32 PadEx21[15];
	/* ExCode 22 = ACK Frequency */
	u32	AckFrequency;	/* Code = 0 (extended), ExCode = 22 */
	u32 PadEx22[15];
	/* ExCode 23 = TOE NA */
	u32 RsvdReg23;
	u32 PadEx23[15];
	/* ExCode 24 = TOE NA */
	u32 RsvdReg24;
	u32 PadEx24[15];
	/* ExCode 25 = TOE NA */
	u32	RsvdReg25;	/* Code = 0 (extended), ExCode = 25 */
	u32 PadEx25[15];
	/* ExCode 26 = Receive checksum requirements */
	u32	ReceiveChecksum;	/* Code = 0 (extended), ExCode = 26 */
	u32 PadEx26[15];
	/* ExCode 27 = RSS Requirements */
	u32	Rss;		/* Code = 0 (extended), ExCode = 27 */
	u32 PadEx27[15];
	/* ExCode 28 = RSS Table */
	u32	RssTable;	/* Code = 0 (extended), ExCode = 28 */
	u32 PadEx28[15];
	/* ExCode 29 = Event ring release entries */
	u32	EventRelease;	/* Code = 0 (extended), ExCode = 29 */
	u32 PadEx29[15];
	/* ExCode 30 = Number of receive bufferlist commands on ring 0 */
	u32	RcvCmd;		/* Code = 0 (extended), ExCode = 30 */
	u32 PadEx30[15];
	/* ExCode 31 = slowpath transmit command - Data[31:0] = 1 */
	u32	XmtCmd;		/* Code = 0 (extended), ExCode = 31 */
	u32 PadEx31[15];
	/* ExCode 32 = Dump command */
	u32	DumpCmd;	/* Code = 0 (extended), ExCode = 32 */
	u32 PadEx32[15];
	/* ExCode 33 = Debug command */
	u32	DebugCmd;	/* Code = 0 (extended), ExCode = 33 */
	u32 PadEx33[15];
	/*
	 * There are 128 possible extended commands - each of account for 16
	 * words (including the non-relevent base command codes 1-15).
	 * Pad for the remainder of these here to bring us to the next CPU
	 * base.  As extended codes are added, reduce the first array value in
	 * the following field
	 */
	u32 PadToNextCpu[94][16];	/* 94 = 128 - 34 (34 = Excodes 0 - 33)*/
};

/* Interrupt control register (0) values */
#define SXG_ICR_DISABLE					0x00000000
#define SXG_ICR_ENABLE					0x00000001
#define SXG_ICR_MASK					0x00000002
#define SXG_ICR_MSGID_MASK				0xFFFF0000
#define SXG_ICR_MSGID_SHIFT			16
#define SXG_ICR(_MessageId, _Data)	\
	((((_MessageId) << SXG_ICR_MSGID_SHIFT) &	\
	  SXG_ICR_MSGID_MASK) | (_Data))

#define SXG_MIN_AGG_DEFAULT	0x0010	/* Minimum aggregation default */
#define SXG_MAX_AGG_DEFAULT	0x0040	/* Maximum aggregation default */
#define SXG_MAX_AGG_SHIFT	16	/* Maximum in top 16 bits of register */
/* Disable interrupt aggregation on xmt */
#define SXG_AGG_XMT_DISABLE	0x80000000

/* The Microcode supports up to 16 RSS queues (RevB) */
#define SXG_MAX_RSS			16
#define SXG_MAX_RSS_REVA		8

#define SXG_MAX_RSS_TABLE_SIZE	256		/* 256-byte max */

#define SXG_RSS_REVA_TCP6	0x00000001	/* RSS TCP over IPv6 */
#define SXG_RSS_REVA_TCP4	0x00000002	/* RSS TCP over IPv4 */
#define SXG_RSS_IP		0x00000001	/* RSS TCP over IPv6 */
#define SXG_RSS_TCP		0x00000002	/* RSS TCP over IPv4 */
#define SXG_RSS_LEGACY		0x00000004	/* Line-base interrupts */
#define SXG_RSS_TABLE_SIZE	0x0000FF00	/* Table size mask */

#define SXG_RSS_TABLE_SHIFT			8
#define	SXG_RSS_BASE_CPU	0x00FF0000	/* Base CPU (not used) */
#define SXG_RSS_BASE_SHIFT			16

#define SXG_RCV_IP_CSUM_ENABLED		0x00000001	/* ExCode 26 (ReceiveChecksum) */
#define SXG_RCV_TCP_CSUM_ENABLED	0x00000002	/* ExCode 26 (ReceiveChecksum) */

#define SXG_XMT_CPUID_SHIFT			16

/*
 * Status returned by ucode in the ConfigStat reg (see above) when attempted
 * to load configuration data from the EEPROM/Flash.
 */
#define	SXG_CFG_TIMEOUT		1	/* init value - timeout if unchanged */
#define	SXG_CFG_LOAD_EEPROM	2	/* config data loaded from EEPROM */
#define	SXG_CFG_LOAD_FLASH	3	/* config data loaded from flash */
#define	SXG_CFG_LOAD_INVALID	4	/* no valid config data found */
#define	SXG_CFG_LOAD_ERROR	5	/* hardware error */

#define SXG_CHECK_FOR_HANG_TIME		5

/*
 * TCB registers - This is really the same register memory area as UCODE_REGS
 * above, but defined differently.  Bits 17:06 of the address define the TCB,
 * which means each TCB area occupies 0x40 (64) bytes, or 16 u32S.  What really
 * is happening is that these registers occupy the "PadEx[15]" areas in the
 * struct sxg_ucode_regs definition above
 */
struct sxg_tcb_regs {
	u32 ExCode;	/* Extended codes - see SXG_UCODE_REGS */
	u32 Xmt;	/* Code = 1 - # of Xmt descriptors added to ring */
	u32 Rcv;	/* Code = 2 - # of Rcv descriptors added to ring */
	u32 Rsvd1;	/* Code = 3 - TOE NA */
	u32 Rsvd2;	/* Code = 4 - TOE NA */
	u32 Rsvd3;	/* Code = 5 - TOE NA */
	u32 Invalid1;	/* Code = 6 - Reserved for "CardUp" see above */
	u32 Rsvd4;	/* Code = 7 - TOE NA */
	u32 Invalid2;	/* Code = 8 - Reserved for "ConfigStat" see above */
	u32 Rsvd5;	/* Code = 9 - TOE NA */
	u32 Pad[6];	/* Codes 10-15 - Not used. */
};

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
#define SXG_ISR_ERR		0x80000000	/* Error */
#define SXG_ISR_EVENT		0x40000000	/* Event ring event */
#define SXG_ISR_NONE1		0x20000000	/* Not used */
#define SXG_ISR_UPC		0x10000000	/* Dump/debug command complete*/
#define SXG_ISR_LINK		0x08000000	/* Link event */
#define SXG_ISR_PDQF		0x04000000	/* Processed data queue full */
#define SXG_ISR_RMISS		0x02000000	/* Drop - no host buf */
#define SXG_ISR_BREAK		0x01000000	/* Breakpoint hit */
#define SXG_ISR_PING		0x00800000	/* Heartbeat response */
#define SXG_ISR_DEAD		0x00400000	/* Card crash */
#define SXG_ISR_ERFULL		0x00200000	/* Event ring full */
#define SXG_ISR_XDROP		0x00100000	/* XMT Drop - no DRAM bufs or XMT err */
#define SXG_ISR_SPSEND		0x00080000	/* Slow send complete */
#define SXG_ISR_CPU		0x00070000	/* Dead CPU mask */
#define SXG_ISR_CPU_SHIFT	16		/* Dead CPU shift */
#define SXG_ISR_CRASH		0x0000FFFF	/* Crash address mask */

/***************************************************************************
 * Event Ring entry
 *
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
 ************************************************************************/
#pragma pack(push, 1)
struct sxg_event {
	u32			Pad[1];		/* not used */
	u32			SndUna;		/* SndUna value */
	u32			Resid;		/* receive MDL resid */
	union {
		void *		HostHandle;	/* Receive host handle */
		u32		Rsvd1;		/* TOE NA */
		struct {
			u32 NotUsed;
			u32	Rsvd2;		/* TOE NA */
		} Flush;
	};
	u32			Toeplitz;	/* RSS Toeplitz hash */
	union {
		ushort		Rsvd3;		/* TOE NA */
		ushort		HdrOffset;	/* Slowpath */
	};
	ushort			Length;
	unsigned char 	Rsvd4;		/* TOE NA */
	unsigned char 	Code;		/* Event code */
	unsigned char	CommandIndex;	/* New ring index */
	unsigned char	Status;		/* Event status */
};
#pragma pack(pop)

/* Event code definitions */
#define EVENT_CODE_BUFFERS	0x01	/* Receive buffer list command (ring 0) */
#define EVENT_CODE_SLOWRCV	0x02	/* Slowpath receive */
#define EVENT_CODE_UNUSED	0x04	/* Was slowpath commands complete */

/* Status values */
#define EVENT_STATUS_VALID	0x80	/* Entry valid */

/* Slowpath status */
#define EVENT_STATUS_ERROR	0x40	/* Completed with error. Index in next byte */
#define EVENT_STATUS_TCPIP4	0x20	/* TCPIPv4 frame */
#define EVENT_STATUS_TCPBAD	0x10	/* Bad TCP checksum */
#define EVENT_STATUS_IPBAD	0x08	/* Bad IP checksum */
#define EVENT_STATUS_RCVERR	0x04	/* Slowpath receive error */
#define EVENT_STATUS_IPONLY	0x02	/* IP frame */
#define EVENT_STATUS_TCPIP6	0x01	/* TCPIPv6 frame */
#define EVENT_STATUS_TCPIP	0x21	/* Combination of v4 and v6 */

/*
 * Event ring
 * Size must be power of 2, between 128 and 16k
 */
#define EVENT_RING_SIZE		4096
#define EVENT_RING_BATCH	16	/* Hand entries back 16 at a time. */
/* Stop processing events after 4096 (256 * 16) */
#define EVENT_BATCH_LIMIT	256

struct sxg_event_ring {
	struct sxg_event Ring[EVENT_RING_SIZE];
};

/* TCB Buffers */
/* Maximum number of TCBS supported by hardware/microcode */
#define SXG_MAX_TCB		4096
/* Minimum TCBs before we fail initialization */
#define SXG_MIN_TCB		512
/*
 * TCB Hash
 * The bucket is determined by bits 11:4 of the toeplitz if we support 4k
 * offloaded connections, 10:4 if we support 2k and so on.
 */
#define SXG_TCB_BUCKET_SHIFT	4
#define SXG_TCB_PER_BUCKET	16
#define SXG_TCB_BUCKET_MASK	0xFF0	/* Bucket portion of TCB ID */
#define SXG_TCB_ELEMENT_MASK	0x00F	/* Element within bucket */
#define SXG_TCB_BUCKETS		256		/* 256 * 16 = 4k */

#define SXG_TCB_BUFFER_SIZE	512	/* ASSERT format is correct */

#define SXG_TCB_RCVQ_SIZE	736

#define SXG_TCB_COMPOSITE_BUFFER_SIZE	1024

#define SXG_LOCATE_TCP_FRAME_HDR(_TcpObject, _IPv6)			\
	(((_TcpObject)->VlanId) ?					\
	 ((_IPv6) ?		/* Vlan frame header = yes */		\
	  &(_TcpObject)->CompBuffer->Frame.HasVlan.TcpIp6.SxgTcp:	\
	  &(_TcpObject)->CompBuffer->Frame.HasVlan.TcpIp.SxgTcp): 	\
	 ((_IPv6) ?		/* Vlan frame header = No */		\
	  &(_TcpObject)->CompBuffer->Frame.NoVlan.TcpIp6.SxgTcp	: 	\
	  &(_TcpObject)->CompBuffer->Frame.NoVlan.TcpIp.SxgTcp))

#define SXG_LOCATE_IP_FRAME_HDR(_TcpObject)				\
	(_TcpObject)->VlanId ?						\
	&(_TcpObject)->CompBuffer->Frame.HasVlan.TcpIp.Ip: 		\
	&(_TcpObject)->CompBuffer->Frame.NoVlan.TcpIp.Ip

#define SXG_LOCATE_IP6_FRAME_HDR(TcpObject)				\
	(_TcpObject)->VlanId ?						\
	&(_TcpObject)->CompBuffer->Frame.HasVlan.TcpIp6.Ip:		\
	&(_TcpObject)->CompBuffer->Frame.NoVlan.TcpIp6.Ip

#if DBG
/*
 * Horrible kludge to distinguish dumb-nic, slowpath, and
 * fastpath traffic.  Decrement the HopLimit by one
 * for slowpath, two for fastpath.  This assumes the limit is measurably
 * greater than two, which I think is reasonable.
 * Obviously this is DBG only.  Maybe remove later, or #if 0 so we
 * can set it when needed
 */
#define SXG_DBG_HOP_LIMIT(_TcpObject, _FastPath) {			\
	PIPV6_HDR		_Ip6FrameHdr;				\
	if ((_TcpObject)->IPv6) {					\
		_Ip6FrameHdr = SXG_LOCATE_IP6_FRAME_HDR((_TcpObject));	\
		if (_FastPath) {					\
			_Ip6FrameHdr->HopLimit = 			\
				(_TcpObject)->Cached.TtlOrHopLimit - 2;	\
		} else {						\
			_Ip6FrameHdr->HopLimit = 			\
				(_TcpObject)->Cached.TtlOrHopLimit - 1;	\
		}							\
	}								\
}
#else
/* Do nothing with free build */
#define SXG_DBG_HOP_LIMIT(_TcpObject, _FastPath)
#endif

/* Receive and transmit rings */
#define SXG_MAX_RING_SIZE	256
#define SXG_XMT_RING_SIZE	128		/* Start with 128 */
#define SXG_RCV_RING_SIZE	128		/* Start with 128 */
#define SXG_MAX_ENTRIES     4096
#define SXG_JUMBO_RCV_RING_SIZE       32

/* Structure and macros to manage a ring */
struct sxg_ring_info {
	/* Where we add entries - Note unsigned char:RING_SIZE */
	unsigned char Head;
	unsigned char Tail;	/* Where we pull off completed entries */
	ushort	Size;		/* Ring size - Must be multiple of 2 */
	void *	Context[SXG_MAX_RING_SIZE];	/* Shadow ring */
};

#define SXG_INITIALIZE_RING(_ring, _size) {				\
	(_ring).Head = 0;						\
	(_ring).Tail = 0;						\
	(_ring).Size = (_size);						\
}

#define SXG_ADVANCE_INDEX(_index, _size) 				\
			((_index) = ((_index) + 1) & ((_size) - 1))
#define SXG_PREVIOUS_INDEX(_index, _size) 				\
			(((_index) - 1) &((_size) - 1))
#define SXG_RING_EMPTY(_ring) ((_ring)->Head == (_ring)->Tail)
#define SXG_RING_FULL(_ring) 						\
		((((_ring)->Head + 1) & ((_ring)->Size - 1)) == (_ring)->Tail)
#define SXG_RING_ADVANCE_HEAD(_ring) 					\
		SXG_ADVANCE_INDEX((_ring)->Head, ((_ring)->Size))
#define SXG_RING_RETREAT_HEAD(_ring) ((_ring)->Head =			\
		SXG_PREVIOUS_INDEX((_ring)->Head, (_ring)->Size))
#define SXG_RING_ADVANCE_TAIL(_ring) {					\
	ASSERT((_ring)->Tail != (_ring)->Head);				\
	SXG_ADVANCE_INDEX((_ring)->Tail, ((_ring)->Size));		\
}
/*
 * Set cmd to the next available ring entry, set the shadow context
 * entry and advance the ring.
 * The appropriate lock must be held when calling this macro
 */
#define SXG_GET_CMD(_ring, _ringinfo, _cmd, _context) {			\
	if(SXG_RING_FULL(_ringinfo)) {					\
		(_cmd) = NULL;						\
	} else {							\
		(_cmd) = &(_ring)->Descriptors[(_ringinfo)->Head];	\
		(_ringinfo)->Context[(_ringinfo)->Head] = (void *)(_context);\
		SXG_RING_ADVANCE_HEAD(_ringinfo);			\
	}								\
}

/*
 * Abort the previously allocated command by retreating the head.
 * NOTE - The appopriate lock MUST NOT BE DROPPED between the SXG_GET_CMD
 * and SXG_ABORT_CMD calls.
 */
#define SXG_ABORT_CMD(_ringinfo) {					\
	ASSERT(!(SXG_RING_EMPTY(_ringinfo)));				\
	SXG_RING_RETREAT_HEAD(_ringinfo);				\
	(_ringinfo)->Context[(_ringinfo)->Head] = NULL;			\
}

/*
 * For the given ring, return a pointer to the tail cmd and context,
 * clear the context and advance the tail
 */
#define SXG_RETURN_CMD(_ring, _ringinfo, _cmd, _context) {		\
	(_cmd) = &(_ring)->Descriptors[(_ringinfo)->Tail];		\
	(_context) = (_ringinfo)->Context[(_ringinfo)->Tail];       	\
	(_ringinfo)->Context[(_ringinfo)->Tail] = NULL;			\
	SXG_RING_ADVANCE_TAIL(_ringinfo);				\
}

/*
 * For a given ring find out how much the first pointer is ahead of
 * the second pointer. "ahead" recognises the fact that the ring can wrap
 */
static inline int sxg_ring_get_forward_diff (struct sxg_ring_info *ringinfo,
						int a, int b) {
	if ((a < 0 || a > ringinfo->Size ) || (b < 0 || b > ringinfo->Size))
		return -1;
	if (a > b)	/* _a is lagging _b and _b has not wrapped around */
		return (a - b);
	else
		return ((ringinfo->Size - (b - a)));
}

/***************************************************************
 * Host Command Buffer - commands to INIC via the Cmd Rings
 *
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
 ****************************************************************/
#pragma pack(push, 1)
struct sxg_cmd {
	dma64_addr_t	Sgl;		/* Physical address of SGL */
	union {
		struct {
			dma64_addr_t FirstSgeAddress; /* Address of first SGE */
			u32 	     FirstSgeLength;  /* Length of first SGE */
			union {
				u32  Rsvd1;	   /* TOE NA */
				u32  SgeOffset; /* Slowpath - 2nd SGE offset */
				/* MDL completion - clobbers update */
				u32  Resid;
			};
			union {
				u32  TotalLength; /* Total transfer length */
				u32  Mss;	  /* LSO MSS */
			};
		} Buffer;
	};
	union {
		struct {
			unsigned char Flags:4;	/* slowpath flags */
			unsigned char IpHl:4;	/* Ip header length (>>2) */
			unsigned char MacLen;	/* Mac header len */
		} CsumFlags;
		struct {
			ushort	Flags:4;	/* slowpath flags */
			ushort	TcpHdrOff:7;	/* TCP */
			ushort	MacLen:5;	/* Mac header len */
		} LsoFlags;
		ushort		Flags;		/* flags */
	};
	union {
		ushort	SgEntries;	/* SG entry count including first sge */
		struct {
			unsigned char Status;	/* Copied from event status */
			unsigned char NotUsed;
		} Status;
	};
};
#pragma pack(pop)

#pragma pack(push, 1)
struct vlan_hdr {
	ushort VlanTci;
	ushort VlanTpid;
};
#pragma pack(pop)

/********************************************************************
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
 *****************************************************************/
/* Slowpath CMD flags */
#define SXG_SLOWCMD_CSUM_IP		0x01	/* Checksum IP */
#define SXG_SLOWCMD_CSUM_TCP		0x02	/* Checksum TCP */
#define SXG_SLOWCMD_LSO			0x04	/* Large segment send */

struct sxg_xmt_ring {
	struct sxg_cmd Descriptors[SXG_XMT_RING_SIZE];
};

struct sxg_rcv_ring {
	struct sxg_cmd Descriptors[SXG_RCV_RING_SIZE];
};

/*
 * Share memory buffer types - Used to identify asynchronous
 * shared memory allocation
 */
enum sxg_buffer_type {
	SXG_BUFFER_TYPE_RCV,		/* Receive buffer */
	SXG_BUFFER_TYPE_SGL		/* SGL buffer */
};

/* State for SXG buffers */
#define SXG_BUFFER_FREE		0x01
#define SXG_BUFFER_BUSY		0x02
#define SXG_BUFFER_ONCARD	0x04
#define SXG_BUFFER_UPSTREAM	0x08

/*
 * Receive data buffers
 *
 * Receive data buffers are given to the Sahara card 128 at a time.
 * This is accomplished by filling in a "receive descriptor block"
 * with 128 "receive descriptors".  Each descriptor consists of
 * a physical address, which the card uses as the address to
 * DMA data into, and a virtual address, which is given back
 * to the host in the "HostHandle" portion of an event.
 * The receive descriptor data structure is defined below
 * as sxg_rcv_data_descriptor, and the corresponding block
 * is defined as sxg_rcv_descriptor_block.
 *
 * This receive descriptor block is given to the card by filling
 * in the Sgl field of a sxg_cmd entry from pAdapt->RcvRings[0]
 * with the physical address of the receive descriptor block.
 *
 * Both the receive buffers and the receive descriptor blocks
 * require additional data structures to maintain them
 * on a free queue and contain other information associated with them.
 * Those data structures are defined as the sxg_rcv_data_buffer_hdr
 * and sxg_rcv_descriptor_block_hdr respectively.
 *
 * Since both the receive buffers and the receive descriptor block
 * must be accessible by the card, both must be allocated out of
 * shared memory.  To ensure that we always have a descriptor
 * block available for every 128 buffers, we allocate all of
 * these resources together in a single block.  This entire
 * block is managed by a struct sxg_rcv_block_hdr, who's sole purpose
 * is to maintain address information so that the entire block
 * can be free later.
 *
 * Further complicating matters is the fact that the receive
 * buffers must be variable in length in order to accomodate
 * jumbo frame configurations.  We configure the buffer
 * length so that the buffer and it's corresponding struct
 * sxg_rcv_data_buffer_hdr structure add up to an even
 * boundary.  Then we place the remaining data structures after 128
 *  of them as shown in the following diagram:
 *
 *  _________________________________________
 * |                                         |
 * |    Variable length receive buffer #1    |
 * |_________________________________________|
 * |                                         |
 * |       sxg_rcv_data_buffer_hdr #1        |
 * |_________________________________________| <== Even 2k or 10k boundary
 * |                                         |
 * |         ... repeat 2-128 ..             |
 * |_________________________________________|
 * |                                         |
 * |      struct sxg_rcv_descriptor_block    |
 * |  Contains sxg_rcv_data_descriptor * 128 |
 * |_________________________________________|
 * |                                         |
 * |   struct sxg_rcv_descriptor_block_hdr   |
 * |_________________________________________|
 * |                                         |
 * |      struct sxg_rcv_block_hdr           |
 * |_________________________________________|
 *
 * Memory consumption:
 *	  Non-jumbo:
 *      Buffers and sxg_rcv_data_buffer_hdr = 2k * 128 = 256k
 *    + struct sxg_rcv_descriptor_block = 2k
 *    + struct sxg_rcv_descriptor_block_hdr = ~32
 *    + struct sxg_rcv_block_hdr = ~32
 *    => Total = ~258k/block
 *
 *	  Jumbo:
 *      Buffers and sxg_rcv_data_buffer_hdr = 10k * 128 = 1280k
 *    + struct sxg_rcv_descriptor_block = 2k
 *    + struct sxg_rcv_descriptor_block_hdr = ~32
 *    + struct sxg_rcv_block_hdr = ~32
 *    => Total = ~1282k/block
 *
 */
#define SXG_RCV_DATA_BUFFERS		8192	/* Amount to give to the card */
#define SXG_INITIAL_RCV_DATA_BUFFERS	16384	/* Initial pool of buffers */
/* Minimum amount and when to get more */
#define SXG_MIN_RCV_DATA_BUFFERS	4096
#define SXG_MAX_RCV_BLOCKS		256	/* = 32k receive buffers */
/* Amount to give to the card in case of jumbo frames */
#define SXG_JUMBO_RCV_DATA_BUFFERS		2048
/* Initial pool of buffers in case of jumbo buffers */
#define SXG_INITIAL_JUMBO_RCV_DATA_BUFFERS	4096
#define SXG_MIN_JUMBO_RCV_DATA_BUFFERS		1024

/* Receive buffer header */
struct sxg_rcv_data_buffer_hdr {
	dma64_addr_t	PhysicalAddress;	/* Buffer physical address */
	/*
	 * Note - DO NOT USE the VirtualAddress field to locate data.
	 * Use the sxg.h:SXG_RECEIVE_DATA_LOCATION macro instead.
	 */
	struct list_entry	FreeList;	/* Free queue of buffers */
	unsigned char		State;		/* See SXG_BUFFER state above */
	struct sk_buff          * skb;		/* Double mapped (nbl and pkt)*/
};

/*
 * SxgSlowReceive uses the PACKET (skb) contained
 * in the struct sxg_rcv_data_buffer_hdr when indicating dumb-nic data
 */
#define SxgDumbRcvPacket	        skb

/* Space for struct sxg_rcv_data_buffer_hdr */
#define SXG_RCV_DATA_HDR_SIZE		sizeof(struct sxg_rcv_data_buffer_hdr)
/* Non jumbo = 2k including HDR */
#define SXG_RCV_DATA_BUFFER_SIZE	2048
/* jumbo = 10k including HDR */
#define SXG_RCV_JUMBO_BUFFER_SIZE	10240

/* Receive data descriptor */
struct sxg_rcv_data_descriptor {
	union {
		struct sk_buff *VirtualAddress;	/* Host handle */
		u64		ForceTo8Bytes;	/*Force x86 to 8-byte boundary*/
	};
	dma64_addr_t PhysicalAddress;
};

/* Receive descriptor block */
#define SXG_RCV_DESCRIPTORS_PER_BLOCK		128
#define SXG_RCV_DESCRIPTOR_BLOCK_SIZE		2048	/* For sanity check */

struct sxg_rcv_descriptor_block {
	struct sxg_rcv_data_descriptor Descriptors[SXG_RCV_DESCRIPTORS_PER_BLOCK];
};

/* Receive descriptor block header */
struct sxg_rcv_descriptor_block_hdr {
	void 		*VirtualAddress;	/* start of 2k buffer */
	dma64_addr_t		PhysicalAddress;/* and it's physical address */
	struct list_entry	FreeList;/* free queue of descriptor blocks */
	unsigned char	State;	/* see sxg_buffer state above */
};

/* Receive block header */
struct sxg_rcv_block_hdr {
	void		*VirtualAddress;	/* Start of virtual memory */
	dma64_addr_t		PhysicalAddress;/* ..and it's physical address*/
	struct list_entry	AllList;	/* Queue of all SXG_RCV_BLOCKS*/
};

/* Macros to determine data structure offsets into receive block */
#define SXG_RCV_BLOCK_SIZE(_Buffersize) 				\
	(((_Buffersize) * SXG_RCV_DESCRIPTORS_PER_BLOCK) +		\
	 (sizeof(struct sxg_rcv_descriptor_block))              +	\
	 (sizeof(struct sxg_rcv_descriptor_block_hdr))          +	\
	 (sizeof(struct sxg_rcv_block_hdr)))
#define SXG_RCV_BUFFER_DATA_SIZE(_Buffersize)				\
	((_Buffersize) - SXG_RCV_DATA_HDR_SIZE)
#define SXG_RCV_DATA_BUFFER_HDR_OFFSET(_Buffersize)			\
	((_Buffersize) - SXG_RCV_DATA_HDR_SIZE)
#define SXG_RCV_DESCRIPTOR_BLOCK_OFFSET(_Buffersize)			\
	((_Buffersize) * SXG_RCV_DESCRIPTORS_PER_BLOCK)
#define SXG_RCV_DESCRIPTOR_BLOCK_HDR_OFFSET(_Buffersize)		\
	(((_Buffersize) * SXG_RCV_DESCRIPTORS_PER_BLOCK) +		\
	 (sizeof(struct sxg_rcv_descriptor_block)))
#define SXG_RCV_BLOCK_HDR_OFFSET(_Buffersize)				\
	(((_Buffersize) * SXG_RCV_DESCRIPTORS_PER_BLOCK) +		\
	 (sizeof(struct sxg_rcv_descriptor_block))              +	\
	 (sizeof(struct sxg_rcv_descriptor_block_hdr)))

/* Scatter gather list buffer */
#define SXG_INITIAL_SGL_BUFFERS	8192	/* Initial pool of SGL buffers */
#define SXG_MIN_SGL_BUFFERS	2048	/* Minimum amount and when to get more*/
/* Maximum to allocate (note ADAPT:ushort) */
#define SXG_MAX_SGL_BUFFERS	16384

/*
 * SXG_SGL_POOL_PROPERTIES - This structure is used to define a pool of SGL
 * buffers. These buffers are allocated out of shared memory and used to
 * contain a physical scatter gather list structure that is shared
 * with the card.
 *
 * We split our SGL buffers into multiple pools based on size.  The motivation
 * is that some applications perform very large I/Os (1MB for example), so
 * we need to be able to allocate an SGL to accommodate such a request.
 * But such an SGL would require 256 24-byte SG entries - ~6k.
 * Given that the vast majority of I/Os are much smaller than 1M, allocating
 * a single pool of SGL buffers would be a horribly inefficient use of
 * memory.
 *
 * The following structure includes two fields relating to its size.
 * The NBSize field specifies the largest NET_BUFFER that can be handled
 * by the particular pool.  The SGEntries field defines the size, in
 * entries, of the SGL for that pool.  The SGEntries is determined by
 * dividing the NBSize by the expected page size (4k), and then padding
 * it by some appropriate amount as insurance (20% or so..??).
 */
struct sxg_sgl_pool_properties {
	u32	NBSize;		/* Largest NET_BUFFER size for this pool */
	ushort	SGEntries;	/* Number of entries in SGL */
	ushort	InitialBuffers;	/* Number to allocate at initializationtime */
	ushort	MinBuffers;	/* When to get more */
	ushort	MaxBuffers;	/* When to stop */
	ushort	PerCpuThreshold;/* See sxgh.h:SXG_RESOURCES */
};

/*
 * At the moment I'm going to statically initialize 4 pools:
 *	100k buffer pool: The vast majority of the expected buffers are expected
 *			to be less than or equal to 100k.  At 30 entries per and
 * 			8k initial buffers amounts to ~4MB of memory
 *                 NOTE - This used to be 64K with 20 entries, but during
 *                        WHQL NDIS 6.0 Testing (2c_mini6stress) MS does their
 *                        best to send absurd NBL's with ridiculous SGLs, we
 *                        have received 400byte sends contained in SGL's that
 *                        have 28 entries
 *	  1M buffer pool: Buffers between 64k and 1M.  Allocate 256 initial
 *	  		  buffers with 300 entries each => ~2MB of memory
 *	  5M buffer pool: Not expected often, if at all.  32 initial buffers
 *			  at 1500 entries each => ~1MB of memory
 * 10M buffer pool: Not expected at all, except under pathelogical conditions.
 * 		    Allocate one at initialization time.
 * 		  Note - 10M is the current limit of what we can realistically
 * 		  	 support due to the sahara SGL bug described in the
 * 		  	 SAHARA SGL WORKAROUND below. We will likely adjust the
 * 		  	 number of pools and/or pool properties over time.
 */
#define SXG_NUM_SGL_POOLS	4
#define INITIALIZE_SGL_POOL_PROPERTIES					\
struct sxg_sgl_pool_properties SxgSglPoolProperties[SXG_NUM_SGL_POOLS] =\
{ 									\
	{  102400,   30, 8192, 2048, 16384, 256},			\
	{ 1048576,  300,  256,  128,  1024, 16},			\
	{ 5252880, 1500,   32,   16,   512, 0},				\
	{10485760, 2700,    2,    4,    32, 0},				\
};

extern struct sxg_sgl_pool_properties SxgSglPoolProperties[];

#define SXG_MAX_SGL_BUFFER_SIZE						\
	SxgSglPoolProperties[SXG_NUM_SGL_POOLS - 1].NBSize

/*
 * SAHARA SGL WORKAROUND!!
 * The current Sahara card uses a 16-bit counter when advancing
 * SGL address locations.  This means that if an SGL crosses
 * a 64k boundary, the hardware will actually skip back to
 * the start of the previous 64k boundary, with obviously
 * undesirable results.
 *
 * We currently workaround this issue by allocating SGL buffers
 * in 64k blocks and skipping over buffers that straddle the boundary.
 */
#define SXG_INVALID_SGL(phys_addr,len) \
	(((phys_addr >> 16) != ( (phys_addr + len) >> 16 )))

/*
 * Allocate SGLs in blocks so we can skip over invalid entries.
 * We allocation 64k worth of SGL buffers, including the
 * struct sxg_sgl_block_hdr, plus one for padding
 */
#define SXG_SGL_BLOCK_SIZE				65536
#define SXG_SGL_ALLOCATION_SIZE(_Pool)					\
	SXG_SGL_BLOCK_SIZE + SXG_SGL_SIZE(_Pool)

struct sxg_sgl_block_hdr {
	ushort			Pool;		/* Associated SGL pool */
	/* struct sxg_scatter_gather blocks */
	struct list_entry	List;
	dma64_addr_t        	PhysicalAddress;/* physical address */
};

/*
 * The following definition denotes the maximum block of memory that the
 * card can DMA to.It is specified in the call to NdisMRegisterScatterGatherDma.
 * For now, use the same value as used in the Slic/Oasis driver, which
 * is 128M.  That should cover any expected MDL that I can think of.
 */
#define SXG_MAX_PHYS_MAP	(1024 * 1024 * 128)

/* Self identifying structure type */
enum SXG_SGL_TYPE {
	SXG_SGL_DUMB,		/* Dumb NIC SGL */
	SXG_SGL_SLOW,		/* Slowpath protocol header - see below */
	SXG_SGL_CHIMNEY		/* Chimney offload SGL */
};

/*
 * The ucode expects an NDIS SGL structure that
 * is formatted for an x64 system.  When running
 * on an x64 system, we can simply hand the NDIS SGL
 * to the card directly.  For x86 systems we must reconstruct
 * the SGL.  The following structure defines an x64
 * formatted SGL entry
 */
struct sxg_x64_sge {
	dma64_addr_t    Address;	/* same as wdm.h */
	u32		Length;		/* same as wdm.h */
	u32		CompilerPad;	/* The compiler pads to 8-bytes */
	u64		Reserved;	/* u32 * in wdm.h.  Force to 8 bytes */
};

/*
 * Our SGL structure - Essentially the same as
 * wdm.h:SCATTER_GATHER_LIST.  Note the variable number of
 * elements based on the pool specified above
 */
struct sxg_x64_sgl {
	u32 NumberOfElements;
	u32 *Reserved;
	struct sxg_x64_sge		Elements[1];   /* Variable */
};

struct sxg_scatter_gather {
	enum SXG_SGL_TYPE	Type;		/* FIRST! Dumb-nic or offload */
	ushort			Pool;		/* Associated SGL pool */
	ushort			Entries;	/* SGL total entries */
	void *   		adapter;	/* Back pointer to adapter */
	/* Free struct sxg_scatter_gather blocks */
	struct list_entry	FreeList;
	/* All struct sxg_scatter_gather blocks */
	struct list_entry	AllList;
	dma64_addr_t		PhysicalAddress;/* physical address */
	unsigned char		State;		/* See SXG_BUFFER state above */
	unsigned char		CmdIndex;	/* Command ring index */
	struct sk_buff         	*DumbPacket;	/* Associated Packet */
	/* For asynchronous completions */
	u32			Direction;
	u32			CurOffset;	/* Current SGL offset */
	u32			SglRef;		/* SGL reference count */
	struct vlan_hdr		VlanTag;	/* VLAN tag to be inserted into SGL */
	struct sxg_x64_sgl     	*pSgl;		/* SGL Addr. Possibly &Sgl */
	struct sxg_x64_sgl	Sgl;		/* SGL handed to card */
};

/*
 * Note - the "- 1" is because struct sxg_scatter_gather=>struct sxg_x64_sgl
 * includes 1 SGE..
 */
#define SXG_SGL_SIZE(_Pool) 						\
	(sizeof(struct sxg_scatter_gather) +				\
	 ((SxgSglPoolProperties[_Pool].SGEntries - 1) * 		\
				sizeof(struct sxg_x64_sge)))

/* Force NDIS to give us it's own buffer so we can reformat to our own */
#define SXG_SGL_BUFFER(_SxgSgl)                 NULL
#define SXG_SGL_BUFFER_LENGTH(_SxgSgl)          0
#define SXG_SGL_BUF_SIZE                        0

/*
#if defined(CONFIG_X86_64)
#define SXG_SGL_BUFFER(_SxgSgl)		    (&_SxgSgl->Sgl)
#define SXG_SGL_BUFFER_LENGTH(_SxgSgl)	((_SxgSgl)->Entries * 		\
					sizeof(struct sxg_x64_sge))
#define SXG_SGL_BUF_SIZE			    sizeof(struct sxg_x64_sgl)
#elif defined(CONFIG_X86)
// Force NDIS to give us it's own buffer so we can reformat to our own
#define SXG_SGL_BUFFER(_SxgSgl)		        NULL
#define SXG_SGL_BUFFER_LENGTH(_SxgSgl)		0
#define SXG_SGL_BUF_SIZE			0
#else
#error staging: sxg: driver is for X86 only!
#endif
*/
/* Microcode statistics */
struct sxg_ucode_stats {
	u32  RPDQOflow;		/* PDQ overflow (unframed ie dq & drop 1st) */
	u32  XDrops;		/* Xmt drops due to no xmt buffer */
	u32  ERDrops;		/* Rcv drops due to ER full */
	u32  NBDrops;		/* Rcv drops due to out of host buffers */
	u32  PQDrops;		/* Rcv drops due to PDQ full */
	/* Rcv drops due to bad frame: no link addr match, frlen > max */
	u32  BFDrops;
	u32  UPDrops;		/* Rcv drops due to UPFq full */
	u32  XNoBufs;		/* Xmt drop due to no DRAM Xmit buffer or PxyBuf */
};

/*
 * Macros for handling the Offload engine values
 */
/* Number of positions to shift Network Header Length before passing to card */
#define SXG_NW_HDR_LEN_SHIFT		2
