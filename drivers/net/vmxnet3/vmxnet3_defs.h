/*
 * Linux driver for VMware's vmxnet3 ethernet NIC.
 *
 * Copyright (C) 2008-2021, VMware, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Maintained by: pv-drivers@vmware.com
 *
 */

#ifndef _VMXNET3_DEFS_H_
#define _VMXNET3_DEFS_H_

#include "upt1_defs.h"

/* all registers are 32 bit wide */
/* BAR 1 */
enum {
	VMXNET3_REG_VRRS	= 0x0,	/* Vmxnet3 Revision Report Selection */
	VMXNET3_REG_UVRS	= 0x8,	/* UPT Version Report Selection */
	VMXNET3_REG_DSAL	= 0x10,	/* Driver Shared Address Low */
	VMXNET3_REG_DSAH	= 0x18,	/* Driver Shared Address High */
	VMXNET3_REG_CMD		= 0x20,	/* Command */
	VMXNET3_REG_MACL	= 0x28,	/* MAC Address Low */
	VMXNET3_REG_MACH	= 0x30,	/* MAC Address High */
	VMXNET3_REG_ICR		= 0x38,	/* Interrupt Cause Register */
	VMXNET3_REG_ECR		= 0x40	/* Event Cause Register */
};

/* BAR 0 */
enum {
	VMXNET3_REG_IMR		= 0x0,	 /* Interrupt Mask Register */
	VMXNET3_REG_TXPROD	= 0x600, /* Tx Producer Index */
	VMXNET3_REG_RXPROD	= 0x800, /* Rx Producer Index for ring 1 */
	VMXNET3_REG_RXPROD2	= 0xA00	 /* Rx Producer Index for ring 2 */
};

#define VMXNET3_PT_REG_SIZE     4096	/* BAR 0 */
#define VMXNET3_VD_REG_SIZE     4096	/* BAR 1 */

#define VMXNET3_REG_ALIGN       8	/* All registers are 8-byte aligned. */
#define VMXNET3_REG_ALIGN_MASK  0x7

/* I/O Mapped access to registers */
#define VMXNET3_IO_TYPE_PT              0
#define VMXNET3_IO_TYPE_VD              1
#define VMXNET3_IO_ADDR(type, reg)      (((type) << 24) | ((reg) & 0xFFFFFF))
#define VMXNET3_IO_TYPE(addr)           ((addr) >> 24)
#define VMXNET3_IO_REG(addr)            ((addr) & 0xFFFFFF)

enum {
	VMXNET3_CMD_FIRST_SET = 0xCAFE0000,
	VMXNET3_CMD_ACTIVATE_DEV = VMXNET3_CMD_FIRST_SET,
	VMXNET3_CMD_QUIESCE_DEV,
	VMXNET3_CMD_RESET_DEV,
	VMXNET3_CMD_UPDATE_RX_MODE,
	VMXNET3_CMD_UPDATE_MAC_FILTERS,
	VMXNET3_CMD_UPDATE_VLAN_FILTERS,
	VMXNET3_CMD_UPDATE_RSSIDT,
	VMXNET3_CMD_UPDATE_IML,
	VMXNET3_CMD_UPDATE_PMCFG,
	VMXNET3_CMD_UPDATE_FEATURE,
	VMXNET3_CMD_RESERVED1,
	VMXNET3_CMD_LOAD_PLUGIN,
	VMXNET3_CMD_RESERVED2,
	VMXNET3_CMD_RESERVED3,
	VMXNET3_CMD_SET_COALESCE,
	VMXNET3_CMD_REGISTER_MEMREGS,
	VMXNET3_CMD_SET_RSS_FIELDS,

	VMXNET3_CMD_FIRST_GET = 0xF00D0000,
	VMXNET3_CMD_GET_QUEUE_STATUS = VMXNET3_CMD_FIRST_GET,
	VMXNET3_CMD_GET_STATS,
	VMXNET3_CMD_GET_LINK,
	VMXNET3_CMD_GET_PERM_MAC_LO,
	VMXNET3_CMD_GET_PERM_MAC_HI,
	VMXNET3_CMD_GET_DID_LO,
	VMXNET3_CMD_GET_DID_HI,
	VMXNET3_CMD_GET_DEV_EXTRA_INFO,
	VMXNET3_CMD_GET_CONF_INTR,
	VMXNET3_CMD_GET_RESERVED1,
	VMXNET3_CMD_GET_TXDATA_DESC_SIZE,
	VMXNET3_CMD_GET_COALESCE,
	VMXNET3_CMD_GET_RSS_FIELDS,
	VMXNET3_CMD_GET_RESERVED2,
	VMXNET3_CMD_GET_RESERVED3,
	VMXNET3_CMD_GET_MAX_QUEUES_CONF,
};

/*
 *	Little Endian layout of bitfields -
 *	Byte 0 :	7.....len.....0
 *	Byte 1 :	oco gen 13.len.8
 *	Byte 2 : 	5.msscof.0 ext1  dtype
 *	Byte 3 : 	13...msscof...6
 *
 *	Big Endian layout of bitfields -
 *	Byte 0:		13...msscof...6
 *	Byte 1 : 	5.msscof.0 ext1  dtype
 *	Byte 2 :	oco gen 13.len.8
 *	Byte 3 :	7.....len.....0
 *
 *	Thus, le32_to_cpu on the dword will allow the big endian driver to read
 *	the bit fields correctly. And cpu_to_le32 will convert bitfields
 *	bit fields written by big endian driver to format required by device.
 */

struct Vmxnet3_TxDesc {
	__le64 addr;

#ifdef __BIG_ENDIAN_BITFIELD
	u32 msscof:14;  /* MSS, checksum offset, flags */
	u32 ext1:1;
	u32 dtype:1;    /* descriptor type */
	u32 oco:1;
	u32 gen:1;      /* generation bit */
	u32 len:14;
#else
	u32 len:14;
	u32 gen:1;      /* generation bit */
	u32 oco:1;
	u32 dtype:1;    /* descriptor type */
	u32 ext1:1;
	u32 msscof:14;  /* MSS, checksum offset, flags */
#endif  /* __BIG_ENDIAN_BITFIELD */

#ifdef __BIG_ENDIAN_BITFIELD
	u32 tci:16;     /* Tag to Insert */
	u32 ti:1;       /* VLAN Tag Insertion */
	u32 ext2:1;
	u32 cq:1;       /* completion request */
	u32 eop:1;      /* End Of Packet */
	u32 om:2;       /* offload mode */
	u32 hlen:10;    /* header len */
#else
	u32 hlen:10;    /* header len */
	u32 om:2;       /* offload mode */
	u32 eop:1;      /* End Of Packet */
	u32 cq:1;       /* completion request */
	u32 ext2:1;
	u32 ti:1;       /* VLAN Tag Insertion */
	u32 tci:16;     /* Tag to Insert */
#endif  /* __BIG_ENDIAN_BITFIELD */
};

/* TxDesc.OM values */
#define VMXNET3_OM_NONE         0
#define VMXNET3_OM_ENCAP        1
#define VMXNET3_OM_CSUM         2
#define VMXNET3_OM_TSO          3

/* fields in TxDesc we access w/o using bit fields */
#define VMXNET3_TXD_EOP_SHIFT	12
#define VMXNET3_TXD_CQ_SHIFT	13
#define VMXNET3_TXD_GEN_SHIFT	14
#define VMXNET3_TXD_EOP_DWORD_SHIFT 3
#define VMXNET3_TXD_GEN_DWORD_SHIFT 2

#define VMXNET3_TXD_CQ		(1 << VMXNET3_TXD_CQ_SHIFT)
#define VMXNET3_TXD_EOP		(1 << VMXNET3_TXD_EOP_SHIFT)
#define VMXNET3_TXD_GEN		(1 << VMXNET3_TXD_GEN_SHIFT)

#define VMXNET3_HDR_COPY_SIZE   128


struct Vmxnet3_TxDataDesc {
	u8		data[VMXNET3_HDR_COPY_SIZE];
};

typedef u8 Vmxnet3_RxDataDesc;

#define VMXNET3_TCD_GEN_SHIFT	31
#define VMXNET3_TCD_GEN_SIZE	1
#define VMXNET3_TCD_TXIDX_SHIFT	0
#define VMXNET3_TCD_TXIDX_SIZE	12
#define VMXNET3_TCD_GEN_DWORD_SHIFT	3

struct Vmxnet3_TxCompDesc {
	u32		txdIdx:12;    /* Index of the EOP TxDesc */
	u32		ext1:20;

	__le32		ext2;
	__le32		ext3;

	u32		rsvd:24;
	u32		type:7;       /* completion type */
	u32		gen:1;        /* generation bit */
};

struct Vmxnet3_RxDesc {
	__le64		addr;

#ifdef __BIG_ENDIAN_BITFIELD
	u32		gen:1;        /* Generation bit */
	u32		rsvd:15;
	u32		dtype:1;      /* Descriptor type */
	u32		btype:1;      /* Buffer Type */
	u32		len:14;
#else
	u32		len:14;
	u32		btype:1;      /* Buffer Type */
	u32		dtype:1;      /* Descriptor type */
	u32		rsvd:15;
	u32		gen:1;        /* Generation bit */
#endif
	u32		ext1;
};

/* values of RXD.BTYPE */
#define VMXNET3_RXD_BTYPE_HEAD   0    /* head only */
#define VMXNET3_RXD_BTYPE_BODY   1    /* body only */

/* fields in RxDesc we access w/o using bit fields */
#define VMXNET3_RXD_BTYPE_SHIFT  14
#define VMXNET3_RXD_GEN_SHIFT    31

#define VMXNET3_RCD_HDR_INNER_SHIFT  13

struct Vmxnet3_RxCompDesc {
#ifdef __BIG_ENDIAN_BITFIELD
	u32		ext2:1;
	u32		cnc:1;        /* Checksum Not Calculated */
	u32		rssType:4;    /* RSS hash type used */
	u32		rqID:10;      /* rx queue/ring ID */
	u32		sop:1;        /* Start of Packet */
	u32		eop:1;        /* End of Packet */
	u32		ext1:2;
	u32		rxdIdx:12;    /* Index of the RxDesc */
#else
	u32		rxdIdx:12;    /* Index of the RxDesc */
	u32		ext1:2;
	u32		eop:1;        /* End of Packet */
	u32		sop:1;        /* Start of Packet */
	u32		rqID:10;      /* rx queue/ring ID */
	u32		rssType:4;    /* RSS hash type used */
	u32		cnc:1;        /* Checksum Not Calculated */
	u32		ext2:1;
#endif  /* __BIG_ENDIAN_BITFIELD */

	__le32		rssHash;      /* RSS hash value */

#ifdef __BIG_ENDIAN_BITFIELD
	u32		tci:16;       /* Tag stripped */
	u32		ts:1;         /* Tag is stripped */
	u32		err:1;        /* Error */
	u32		len:14;       /* data length */
#else
	u32		len:14;       /* data length */
	u32		err:1;        /* Error */
	u32		ts:1;         /* Tag is stripped */
	u32		tci:16;       /* Tag stripped */
#endif  /* __BIG_ENDIAN_BITFIELD */


#ifdef __BIG_ENDIAN_BITFIELD
	u32		gen:1;        /* generation bit */
	u32		type:7;       /* completion type */
	u32		fcs:1;        /* Frame CRC correct */
	u32		frg:1;        /* IP Fragment */
	u32		v4:1;         /* IPv4 */
	u32		v6:1;         /* IPv6 */
	u32		ipc:1;        /* IP Checksum Correct */
	u32		tcp:1;        /* TCP packet */
	u32		udp:1;        /* UDP packet */
	u32		tuc:1;        /* TCP/UDP Checksum Correct */
	u32		csum:16;
#else
	u32		csum:16;
	u32		tuc:1;        /* TCP/UDP Checksum Correct */
	u32		udp:1;        /* UDP packet */
	u32		tcp:1;        /* TCP packet */
	u32		ipc:1;        /* IP Checksum Correct */
	u32		v6:1;         /* IPv6 */
	u32		v4:1;         /* IPv4 */
	u32		frg:1;        /* IP Fragment */
	u32		fcs:1;        /* Frame CRC correct */
	u32		type:7;       /* completion type */
	u32		gen:1;        /* generation bit */
#endif  /* __BIG_ENDIAN_BITFIELD */
};

struct Vmxnet3_RxCompDescExt {
	__le32		dword1;
	u8		segCnt;       /* Number of aggregated packets */
	u8		dupAckCnt;    /* Number of duplicate Acks */
	__le16		tsDelta;      /* TCP timestamp difference */
	__le32		dword2;
#ifdef __BIG_ENDIAN_BITFIELD
	u32		gen:1;        /* generation bit */
	u32		type:7;       /* completion type */
	u32		fcs:1;        /* Frame CRC correct */
	u32		frg:1;        /* IP Fragment */
	u32		v4:1;         /* IPv4 */
	u32		v6:1;         /* IPv6 */
	u32		ipc:1;        /* IP Checksum Correct */
	u32		tcp:1;        /* TCP packet */
	u32		udp:1;        /* UDP packet */
	u32		tuc:1;        /* TCP/UDP Checksum Correct */
	u32		mss:16;
#else
	u32		mss:16;
	u32		tuc:1;        /* TCP/UDP Checksum Correct */
	u32		udp:1;        /* UDP packet */
	u32		tcp:1;        /* TCP packet */
	u32		ipc:1;        /* IP Checksum Correct */
	u32		v6:1;         /* IPv6 */
	u32		v4:1;         /* IPv4 */
	u32		frg:1;        /* IP Fragment */
	u32		fcs:1;        /* Frame CRC correct */
	u32		type:7;       /* completion type */
	u32		gen:1;        /* generation bit */
#endif  /* __BIG_ENDIAN_BITFIELD */
};


/* fields in RxCompDesc we access via Vmxnet3_GenericDesc.dword[3] */
#define VMXNET3_RCD_TUC_SHIFT	16
#define VMXNET3_RCD_IPC_SHIFT	19

/* fields in RxCompDesc we access via Vmxnet3_GenericDesc.qword[1] */
#define VMXNET3_RCD_TYPE_SHIFT	56
#define VMXNET3_RCD_GEN_SHIFT	63

/* csum OK for TCP/UDP pkts over IP */
#define VMXNET3_RCD_CSUM_OK (1 << VMXNET3_RCD_TUC_SHIFT | \
			     1 << VMXNET3_RCD_IPC_SHIFT)
#define VMXNET3_TXD_GEN_SIZE 1
#define VMXNET3_TXD_EOP_SIZE 1

/* value of RxCompDesc.rssType */
#define VMXNET3_RCD_RSS_TYPE_NONE     0
#define VMXNET3_RCD_RSS_TYPE_IPV4     1
#define VMXNET3_RCD_RSS_TYPE_TCPIPV4  2
#define VMXNET3_RCD_RSS_TYPE_IPV6     3
#define VMXNET3_RCD_RSS_TYPE_TCPIPV6  4
#define VMXNET3_RCD_RSS_TYPE_UDPIPV4  5
#define VMXNET3_RCD_RSS_TYPE_UDPIPV6  6
#define VMXNET3_RCD_RSS_TYPE_ESPIPV4  7
#define VMXNET3_RCD_RSS_TYPE_ESPIPV6  8


/* a union for accessing all cmd/completion descriptors */
union Vmxnet3_GenericDesc {
	__le64				qword[2];
	__le32				dword[4];
	__le16				word[8];
	struct Vmxnet3_TxDesc		txd;
	struct Vmxnet3_RxDesc		rxd;
	struct Vmxnet3_TxCompDesc	tcd;
	struct Vmxnet3_RxCompDesc	rcd;
	struct Vmxnet3_RxCompDescExt 	rcdExt;
};

#define VMXNET3_INIT_GEN       1

/* Max size of a single tx buffer */
#define VMXNET3_MAX_TX_BUF_SIZE  (1 << 14)

/* # of tx desc needed for a tx buffer size */
#define VMXNET3_TXD_NEEDED(size) (((size) + VMXNET3_MAX_TX_BUF_SIZE - 1) / \
				  VMXNET3_MAX_TX_BUF_SIZE)

/* max # of tx descs for a non-tso pkt */
#define VMXNET3_MAX_TXD_PER_PKT 16

/* Max size of a single rx buffer */
#define VMXNET3_MAX_RX_BUF_SIZE  ((1 << 14) - 1)
/* Minimum size of a type 0 buffer */
#define VMXNET3_MIN_T0_BUF_SIZE  128
#define VMXNET3_MAX_CSUM_OFFSET  1024

/* Ring base address alignment */
#define VMXNET3_RING_BA_ALIGN   512
#define VMXNET3_RING_BA_MASK    (VMXNET3_RING_BA_ALIGN - 1)

/* Ring size must be a multiple of 32 */
#define VMXNET3_RING_SIZE_ALIGN 32
#define VMXNET3_RING_SIZE_MASK  (VMXNET3_RING_SIZE_ALIGN - 1)

/* Tx Data Ring buffer size must be a multiple of 64 */
#define VMXNET3_TXDATA_DESC_SIZE_ALIGN 64
#define VMXNET3_TXDATA_DESC_SIZE_MASK  (VMXNET3_TXDATA_DESC_SIZE_ALIGN - 1)

/* Rx Data Ring buffer size must be a multiple of 64 */
#define VMXNET3_RXDATA_DESC_SIZE_ALIGN 64
#define VMXNET3_RXDATA_DESC_SIZE_MASK  (VMXNET3_RXDATA_DESC_SIZE_ALIGN - 1)

/* Max ring size */
#define VMXNET3_TX_RING_MAX_SIZE   4096
#define VMXNET3_TC_RING_MAX_SIZE   4096
#define VMXNET3_RX_RING_MAX_SIZE   4096
#define VMXNET3_RX_RING2_MAX_SIZE  4096
#define VMXNET3_RC_RING_MAX_SIZE   8192

#define VMXNET3_TXDATA_DESC_MIN_SIZE 128
#define VMXNET3_TXDATA_DESC_MAX_SIZE 2048

#define VMXNET3_RXDATA_DESC_MAX_SIZE 2048

/* a list of reasons for queue stop */

enum {
 VMXNET3_ERR_NOEOP        = 0x80000000,  /* cannot find the EOP desc of a pkt */
 VMXNET3_ERR_TXD_REUSE    = 0x80000001,  /* reuse TxDesc before tx completion */
 VMXNET3_ERR_BIG_PKT      = 0x80000002,  /* too many TxDesc for a pkt */
 VMXNET3_ERR_DESC_NOT_SPT = 0x80000003,  /* descriptor type not supported */
 VMXNET3_ERR_SMALL_BUF    = 0x80000004,  /* type 0 buffer too small */
 VMXNET3_ERR_STRESS       = 0x80000005,  /* stress option firing in vmkernel */
 VMXNET3_ERR_SWITCH       = 0x80000006,  /* mode switch failure */
 VMXNET3_ERR_TXD_INVALID  = 0x80000007,  /* invalid TxDesc */
};

/* completion descriptor types */
#define VMXNET3_CDTYPE_TXCOMP      0    /* Tx Completion Descriptor */
#define VMXNET3_CDTYPE_RXCOMP      3    /* Rx Completion Descriptor */
#define VMXNET3_CDTYPE_RXCOMP_LRO  4    /* Rx Completion Descriptor for LRO */

enum {
	VMXNET3_GOS_BITS_UNK    = 0,   /* unknown */
	VMXNET3_GOS_BITS_32     = 1,
	VMXNET3_GOS_BITS_64     = 2,
};

#define VMXNET3_GOS_TYPE_LINUX	1


struct Vmxnet3_GOSInfo {
#ifdef __BIG_ENDIAN_BITFIELD
	u32		gosMisc:10;    /* other info about gos */
	u32		gosVer:16;     /* gos version */
	u32		gosType:4;     /* which guest */
	u32		gosBits:2;    /* 32-bit or 64-bit? */
#else
	u32		gosBits:2;     /* 32-bit or 64-bit? */
	u32		gosType:4;     /* which guest */
	u32		gosVer:16;     /* gos version */
	u32		gosMisc:10;    /* other info about gos */
#endif  /* __BIG_ENDIAN_BITFIELD */
};

struct Vmxnet3_DriverInfo {
	__le32				version;
	struct Vmxnet3_GOSInfo		gos;
	__le32				vmxnet3RevSpt;
	__le32				uptVerSpt;
};


#define VMXNET3_REV1_MAGIC  3133079265u

/*
 * QueueDescPA must be 128 bytes aligned. It points to an array of
 * Vmxnet3_TxQueueDesc followed by an array of Vmxnet3_RxQueueDesc.
 * The number of Vmxnet3_TxQueueDesc/Vmxnet3_RxQueueDesc are specified by
 * Vmxnet3_MiscConf.numTxQueues/numRxQueues, respectively.
 */
#define VMXNET3_QUEUE_DESC_ALIGN  128


struct Vmxnet3_MiscConf {
	struct Vmxnet3_DriverInfo driverInfo;
	__le64		uptFeatures;
	__le64		ddPA;         /* driver data PA */
	__le64		queueDescPA;  /* queue descriptor table PA */
	__le32		ddLen;        /* driver data len */
	__le32		queueDescLen; /* queue desc. table len in bytes */
	__le32		mtu;
	__le16		maxNumRxSG;
	u8		numTxQueues;
	u8		numRxQueues;
	__le32		reserved[4];
};


struct Vmxnet3_TxQueueConf {
	__le64		txRingBasePA;
	__le64		dataRingBasePA;
	__le64		compRingBasePA;
	__le64		ddPA;         /* driver data */
	__le64		reserved;
	__le32		txRingSize;   /* # of tx desc */
	__le32		dataRingSize; /* # of data desc */
	__le32		compRingSize; /* # of comp desc */
	__le32		ddLen;        /* size of driver data */
	u8		intrIdx;
	u8		_pad1[1];
	__le16		txDataRingDescSize;
	u8		_pad2[4];
};


struct Vmxnet3_RxQueueConf {
	__le64		rxRingBasePA[2];
	__le64		compRingBasePA;
	__le64		ddPA;            /* driver data */
	__le64		rxDataRingBasePA;
	__le32		rxRingSize[2];   /* # of rx desc */
	__le32		compRingSize;    /* # of rx comp desc */
	__le32		ddLen;           /* size of driver data */
	u8		intrIdx;
	u8		_pad1[1];
	__le16		rxDataRingDescSize;  /* size of rx data ring buffer */
	u8		_pad2[4];
};


enum vmxnet3_intr_mask_mode {
	VMXNET3_IMM_AUTO   = 0,
	VMXNET3_IMM_ACTIVE = 1,
	VMXNET3_IMM_LAZY   = 2
};

enum vmxnet3_intr_type {
	VMXNET3_IT_AUTO = 0,
	VMXNET3_IT_INTX = 1,
	VMXNET3_IT_MSI  = 2,
	VMXNET3_IT_MSIX = 3
};

#define VMXNET3_MAX_TX_QUEUES  8
#define VMXNET3_MAX_RX_QUEUES  16
/* addition 1 for events */
#define VMXNET3_MAX_INTRS      25

/* Version 6 and later will use below macros */
#define VMXNET3_EXT_MAX_TX_QUEUES  32
#define VMXNET3_EXT_MAX_RX_QUEUES  32
/* addition 1 for events */
#define VMXNET3_EXT_MAX_INTRS      65
#define VMXNET3_FIRST_SET_INTRS    64

/* value of intrCtrl */
#define VMXNET3_IC_DISABLE_ALL  0x1   /* bit 0 */


struct Vmxnet3_IntrConf {
	bool		autoMask;
	u8		numIntrs;      /* # of interrupts */
	u8		eventIntrIdx;
	u8		modLevels[VMXNET3_MAX_INTRS];	/* moderation level for
							 * each intr */
	__le32		intrCtrl;
	__le32		reserved[2];
};

struct Vmxnet3_IntrConfExt {
	u8              autoMask;
	u8              numIntrs;      /* # of interrupts */
	u8              eventIntrIdx;
	u8              reserved;
	__le32          intrCtrl;
	__le32          reserved1;
	u8              modLevels[VMXNET3_EXT_MAX_INTRS]; /* moderation level for
							   * each intr
							   */
	u8              reserved2[3];
};

/* one bit per VLAN ID, the size is in the units of u32	*/
#define VMXNET3_VFT_SIZE  (4096 / (sizeof(u32) * 8))


struct Vmxnet3_QueueStatus {
	bool		stopped;
	u8		_pad[3];
	__le32		error;
};


struct Vmxnet3_TxQueueCtrl {
	__le32		txNumDeferred;
	__le32		txThreshold;
	__le64		reserved;
};


struct Vmxnet3_RxQueueCtrl {
	bool		updateRxProd;
	u8		_pad[7];
	__le64		reserved;
};

enum {
	VMXNET3_RXM_UCAST     = 0x01,  /* unicast only */
	VMXNET3_RXM_MCAST     = 0x02,  /* multicast passing the filters */
	VMXNET3_RXM_BCAST     = 0x04,  /* broadcast only */
	VMXNET3_RXM_ALL_MULTI = 0x08,  /* all multicast */
	VMXNET3_RXM_PROMISC   = 0x10  /* promiscuous */
};

struct Vmxnet3_RxFilterConf {
	__le32		rxMode;       /* VMXNET3_RXM_xxx */
	__le16		mfTableLen;   /* size of the multicast filter table */
	__le16		_pad1;
	__le64		mfTablePA;    /* PA of the multicast filters table */
	__le32		vfTable[VMXNET3_VFT_SIZE]; /* vlan filter */
};


#define VMXNET3_PM_MAX_FILTERS        6
#define VMXNET3_PM_MAX_PATTERN_SIZE   128
#define VMXNET3_PM_MAX_MASK_SIZE      (VMXNET3_PM_MAX_PATTERN_SIZE / 8)

#define VMXNET3_PM_WAKEUP_MAGIC       cpu_to_le16(0x01)  /* wake up on magic pkts */
#define VMXNET3_PM_WAKEUP_FILTER      cpu_to_le16(0x02)  /* wake up on pkts matching
							  * filters */


struct Vmxnet3_PM_PktFilter {
	u8		maskSize;
	u8		patternSize;
	u8		mask[VMXNET3_PM_MAX_MASK_SIZE];
	u8		pattern[VMXNET3_PM_MAX_PATTERN_SIZE];
	u8		pad[6];
};


struct Vmxnet3_PMConf {
	__le16		wakeUpEvents;  /* VMXNET3_PM_WAKEUP_xxx */
	u8		numFilters;
	u8		pad[5];
	struct Vmxnet3_PM_PktFilter filters[VMXNET3_PM_MAX_FILTERS];
};


struct Vmxnet3_VariableLenConfDesc {
	__le32		confVer;
	__le32		confLen;
	__le64		confPA;
};


struct Vmxnet3_TxQueueDesc {
	struct Vmxnet3_TxQueueCtrl		ctrl;
	struct Vmxnet3_TxQueueConf		conf;

	/* Driver read after a GET command */
	struct Vmxnet3_QueueStatus		status;
	struct UPT1_TxStats			stats;
	u8					_pad[88]; /* 128 aligned */
};


struct Vmxnet3_RxQueueDesc {
	struct Vmxnet3_RxQueueCtrl		ctrl;
	struct Vmxnet3_RxQueueConf		conf;
	/* Driver read after a GET commad */
	struct Vmxnet3_QueueStatus		status;
	struct UPT1_RxStats			stats;
	u8				      __pad[88]; /* 128 aligned */
};

struct Vmxnet3_SetPolling {
	u8					enablePolling;
};

#define VMXNET3_COAL_STATIC_MAX_DEPTH		128
#define VMXNET3_COAL_RBC_MIN_RATE		100
#define VMXNET3_COAL_RBC_MAX_RATE		100000

enum Vmxnet3_CoalesceMode {
	VMXNET3_COALESCE_DISABLED   = 0,
	VMXNET3_COALESCE_ADAPT      = 1,
	VMXNET3_COALESCE_STATIC     = 2,
	VMXNET3_COALESCE_RBC        = 3
};

struct Vmxnet3_CoalesceRbc {
	u32					rbc_rate;
};

struct Vmxnet3_CoalesceStatic {
	u32					tx_depth;
	u32					tx_comp_depth;
	u32					rx_depth;
};

struct Vmxnet3_CoalesceScheme {
	enum Vmxnet3_CoalesceMode		coalMode;
	union {
		struct Vmxnet3_CoalesceRbc	coalRbc;
		struct Vmxnet3_CoalesceStatic	coalStatic;
	} coalPara;
};

struct Vmxnet3_MemoryRegion {
	__le64					startPA;
	__le32					length;
	__le16					txQueueBits;
	__le16					rxQueueBits;
};

#define MAX_MEMORY_REGION_PER_QUEUE 16
#define MAX_MEMORY_REGION_PER_DEVICE 256

struct Vmxnet3_MemRegs {
	__le16					numRegs;
	__le16					pad[3];
	struct Vmxnet3_MemoryRegion		memRegs[1];
};

enum Vmxnet3_RSSField {
	VMXNET3_RSS_FIELDS_TCPIP4 = 0x0001,
	VMXNET3_RSS_FIELDS_TCPIP6 = 0x0002,
	VMXNET3_RSS_FIELDS_UDPIP4 = 0x0004,
	VMXNET3_RSS_FIELDS_UDPIP6 = 0x0008,
	VMXNET3_RSS_FIELDS_ESPIP4 = 0x0010,
	VMXNET3_RSS_FIELDS_ESPIP6 = 0x0020,
};

/* If the command data <= 16 bytes, use the shared memory directly.
 * otherwise, use variable length configuration descriptor.
 */
union Vmxnet3_CmdInfo {
	struct Vmxnet3_VariableLenConfDesc	varConf;
	struct Vmxnet3_SetPolling		setPolling;
	enum   Vmxnet3_RSSField                 setRssFields;
	__le64					data[2];
};

struct Vmxnet3_DSDevRead {
	/* read-only region for device, read by dev in response to a SET cmd */
	struct Vmxnet3_MiscConf			misc;
	struct Vmxnet3_IntrConf			intrConf;
	struct Vmxnet3_RxFilterConf		rxFilterConf;
	struct Vmxnet3_VariableLenConfDesc	rssConfDesc;
	struct Vmxnet3_VariableLenConfDesc	pmConfDesc;
	struct Vmxnet3_VariableLenConfDesc	pluginConfDesc;
};

struct Vmxnet3_DSDevReadExt {
	/* read-only region for device, read by dev in response to a SET cmd */
	struct Vmxnet3_IntrConfExt              intrConfExt;
};

/* All structures in DriverShared are padded to multiples of 8 bytes */
struct Vmxnet3_DriverShared {
	__le32				magic;
	/* make devRead start at 64bit boundaries */
	__le32                          size; /* size of DriverShared */
	struct Vmxnet3_DSDevRead	devRead;
	__le32				ecr;
	__le32				reserved;
	union {
		__le32			reserved1[4];
		union Vmxnet3_CmdInfo	cmdInfo; /* only valid in the context of
						  * executing the relevant
						  * command
						  */
	} cu;
	struct Vmxnet3_DSDevReadExt     devReadExt;
};


#define VMXNET3_ECR_RQERR       (1 << 0)
#define VMXNET3_ECR_TQERR       (1 << 1)
#define VMXNET3_ECR_LINK        (1 << 2)
#define VMXNET3_ECR_DIC         (1 << 3)
#define VMXNET3_ECR_DEBUG       (1 << 4)

/* flip the gen bit of a ring */
#define VMXNET3_FLIP_RING_GEN(gen) ((gen) = (gen) ^ 0x1)

/* only use this if moving the idx won't affect the gen bit */
#define VMXNET3_INC_RING_IDX_ONLY(idx, ring_size) \
	do {\
		(idx)++;\
		if (unlikely((idx) == (ring_size))) {\
			(idx) = 0;\
		} \
	} while (0)

#define VMXNET3_SET_VFTABLE_ENTRY(vfTable, vid) \
	(vfTable[vid >> 5] |= (1 << (vid & 31)))
#define VMXNET3_CLEAR_VFTABLE_ENTRY(vfTable, vid) \
	(vfTable[vid >> 5] &= ~(1 << (vid & 31)))

#define VMXNET3_VFTABLE_ENTRY_IS_SET(vfTable, vid) \
	((vfTable[vid >> 5] & (1 << (vid & 31))) != 0)

#define VMXNET3_MAX_MTU     9000
#define VMXNET3_MIN_MTU     60

#define VMXNET3_LINK_UP         (10000 << 16 | 1)    /* 10 Gbps, up */
#define VMXNET3_LINK_DOWN       0

#endif /* _VMXNET3_DEFS_H_ */
