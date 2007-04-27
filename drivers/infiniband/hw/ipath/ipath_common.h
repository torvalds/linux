/*
 * Copyright (c) 2006, 2007 QLogic Corporation. All rights reserved.
 * Copyright (c) 2003, 2004, 2005, 2006 PathScale, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _IPATH_COMMON_H
#define _IPATH_COMMON_H

/*
 * This file contains defines, structures, etc. that are used
 * to communicate between kernel and user code.
 */


/* This is the IEEE-assigned OUI for QLogic Inc. InfiniPath */
#define IPATH_SRC_OUI_1 0x00
#define IPATH_SRC_OUI_2 0x11
#define IPATH_SRC_OUI_3 0x75

/* version of protocol header (known to chip also). In the long run,
 * we should be able to generate and accept a range of version numbers;
 * for now we only accept one, and it's compiled in.
 */
#define IPS_PROTO_VERSION 2

/*
 * These are compile time constants that you may want to enable or disable
 * if you are trying to debug problems with code or performance.
 * IPATH_VERBOSE_TRACING define as 1 if you want additional tracing in
 * fastpath code
 * IPATH_TRACE_REGWRITES define as 1 if you want register writes to be
 * traced in faspath code
 * _IPATH_TRACING define as 0 if you want to remove all tracing in a
 * compilation unit
 * _IPATH_DEBUGGING define as 0 if you want to remove debug prints
 */

/*
 * The value in the BTH QP field that InfiniPath uses to differentiate
 * an infinipath protocol IB packet vs standard IB transport
 */
#define IPATH_KD_QP 0x656b79

/*
 * valid states passed to ipath_set_linkstate() user call
 */
#define IPATH_IB_LINKDOWN		0
#define IPATH_IB_LINKARM		1
#define IPATH_IB_LINKACTIVE		2
#define IPATH_IB_LINKINIT		3
#define IPATH_IB_LINKDOWN_SLEEP		4
#define IPATH_IB_LINKDOWN_DISABLE	5
#define IPATH_IB_LINK_LOOPBACK	6 /* enable local loopback */
#define IPATH_IB_LINK_EXTERNAL	7 /* normal, disable local loopback */

/*
 * stats maintained by the driver.  For now, at least, this is global
 * to all minor devices.
 */
struct infinipath_stats {
	/* number of interrupts taken */
	__u64 sps_ints;
	/* number of interrupts for errors */
	__u64 sps_errints;
	/* number of errors from chip (not incl. packet errors or CRC) */
	__u64 sps_errs;
	/* number of packet errors from chip other than CRC */
	__u64 sps_pkterrs;
	/* number of packets with CRC errors (ICRC and VCRC) */
	__u64 sps_crcerrs;
	/* number of hardware errors reported (parity, etc.) */
	__u64 sps_hwerrs;
	/* number of times IB link changed state unexpectedly */
	__u64 sps_iblink;
	/* kernel receive interrupts that didn't read intstat */
	__u64 sps_fastrcvint;
	/* number of kernel (port0) packets received */
	__u64 sps_port0pkts;
	/* number of "ethernet" packets sent by driver */
	__u64 sps_ether_spkts;
	/* number of "ethernet" packets received by driver */
	__u64 sps_ether_rpkts;
	/* number of SMA packets sent by driver. Obsolete. */
	__u64 sps_sma_spkts;
	/* number of SMA packets received by driver. Obsolete. */
	__u64 sps_sma_rpkts;
	/* number of times all ports rcvhdrq was full and packet dropped */
	__u64 sps_hdrqfull;
	/* number of times all ports egrtid was full and packet dropped */
	__u64 sps_etidfull;
	/*
	 * number of times we tried to send from driver, but no pio buffers
	 * avail
	 */
	__u64 sps_nopiobufs;
	/* number of ports currently open */
	__u64 sps_ports;
	/* list of pkeys (other than default) accepted (0 means not set) */
	__u16 sps_pkeys[4];
	__u16 sps_unused16[4]; /* available; maintaining compatible layout */
	/* number of user ports per chip (not IB ports) */
	__u32 sps_nports;
	/* not our interrupt, or already handled */
	__u32 sps_nullintr;
	/* max number of packets handled per receive call */
	__u32 sps_maxpkts_call;
	/* avg number of packets handled per receive call */
	__u32 sps_avgpkts_call;
	/* total number of pages locked */
	__u64 sps_pagelocks;
	/* total number of pages unlocked */
	__u64 sps_pageunlocks;
	/*
	 * Number of packets dropped in kernel other than errors (ether
	 * packets if ipath not configured, etc.)
	 */
	__u64 sps_krdrops;
	__u64 sps_txeparity; /* PIO buffer parity error, recovered */
	/* pad for future growth */
	__u64 __sps_pad[45];
};

/*
 * These are the status bits readable (in ascii form, 64bit value)
 * from the "status" sysfs file.
 */
#define IPATH_STATUS_INITTED       0x1	/* basic initialization done */
#define IPATH_STATUS_DISABLED      0x2	/* hardware disabled */
/* Device has been disabled via admin request */
#define IPATH_STATUS_ADMIN_DISABLED    0x4
/* Chip has been found and initted */
#define IPATH_STATUS_CHIP_PRESENT 0x20
/* IB link is at ACTIVE, usable for data traffic */
#define IPATH_STATUS_IB_READY     0x40
/* link is configured, LID, MTU, etc. have been set */
#define IPATH_STATUS_IB_CONF      0x80
/* no link established, probably no cable */
#define IPATH_STATUS_IB_NOCABLE  0x100
/* A Fatal hardware error has occurred. */
#define IPATH_STATUS_HWERROR     0x200

/*
 * The list of usermode accessible registers.  Also see Reg_* later in file.
 */
typedef enum _ipath_ureg {
	/* (RO)  DMA RcvHdr to be used next. */
	ur_rcvhdrtail = 0,
	/* (RW)  RcvHdr entry to be processed next by host. */
	ur_rcvhdrhead = 1,
	/* (RO)  Index of next Eager index to use. */
	ur_rcvegrindextail = 2,
	/* (RW)  Eager TID to be processed next */
	ur_rcvegrindexhead = 3,
	/* For internal use only; max register number. */
	_IPATH_UregMax
} ipath_ureg;

/* bit values for spi_runtime_flags */
#define IPATH_RUNTIME_HT	0x1
#define IPATH_RUNTIME_PCIE	0x2
#define IPATH_RUNTIME_FORCE_WC_ORDER	0x4
#define IPATH_RUNTIME_RCVHDR_COPY	0x8
#define IPATH_RUNTIME_MASTER	0x10
#define IPATH_RUNTIME_PBC_REWRITE 0x20
#define IPATH_RUNTIME_LOOSE_DMA_ALIGN 0x40

/*
 * This structure is returned by ipath_userinit() immediately after
 * open to get implementation-specific info, and info specific to this
 * instance.
 *
 * This struct must have explict pad fields where type sizes
 * may result in different alignments between 32 and 64 bit
 * programs, since the 64 bit * bit kernel requires the user code
 * to have matching offsets
 */
struct ipath_base_info {
	/* version of hardware, for feature checking. */
	__u32 spi_hw_version;
	/* version of software, for feature checking. */
	__u32 spi_sw_version;
	/* InfiniPath port assigned, goes into sent packets */
	__u16 spi_port;
	__u16 spi_subport;
	/*
	 * IB MTU, packets IB data must be less than this.
	 * The MTU is in bytes, and will be a multiple of 4 bytes.
	 */
	__u32 spi_mtu;
	/*
	 * Size of a PIO buffer.  Any given packet's total size must be less
	 * than this (in words).  Included is the starting control word, so
	 * if 513 is returned, then total pkt size is 512 words or less.
	 */
	__u32 spi_piosize;
	/* size of the TID cache in infinipath, in entries */
	__u32 spi_tidcnt;
	/* size of the TID Eager list in infinipath, in entries */
	__u32 spi_tidegrcnt;
	/* size of a single receive header queue entry in words. */
	__u32 spi_rcvhdrent_size;
	/*
	 * Count of receive header queue entries allocated.
	 * This may be less than the spu_rcvhdrcnt passed in!.
	 */
	__u32 spi_rcvhdr_cnt;

	/* per-chip and other runtime features bitmap (IPATH_RUNTIME_*) */
	__u32 spi_runtime_flags;

	/* address where receive buffer queue is mapped into */
	__u64 spi_rcvhdr_base;

	/* user program. */

	/* base address of eager TID receive buffers. */
	__u64 spi_rcv_egrbufs;

	/* Allocated by initialization code, not by protocol. */

	/*
	 * Size of each TID buffer in host memory, starting at
	 * spi_rcv_egrbufs.  The buffers are virtually contiguous.
	 */
	__u32 spi_rcv_egrbufsize;
	/*
	 * The special QP (queue pair) value that identifies an infinipath
	 * protocol packet from standard IB packets.  More, probably much
	 * more, to be added.
	 */
	__u32 spi_qpair;

	/*
	 * User register base for init code, not to be used directly by
	 * protocol or applications.
	 */
	__u64 __spi_uregbase;
	/*
	 * Maximum buffer size in bytes that can be used in a single TID
	 * entry (assuming the buffer is aligned to this boundary).  This is
	 * the minimum of what the hardware and software support Guaranteed
	 * to be a power of 2.
	 */
	__u32 spi_tid_maxsize;
	/*
	 * alignment of each pio send buffer (byte count
	 * to add to spi_piobufbase to get to second buffer)
	 */
	__u32 spi_pioalign;
	/*
	 * The index of the first pio buffer available to this process;
	 * needed to do lookup in spi_pioavailaddr; not added to
	 * spi_piobufbase.
	 */
	__u32 spi_pioindex;
	 /* number of buffers mapped for this process */
	__u32 spi_piocnt;

	/*
	 * Base address of writeonly pio buffers for this process.
	 * Each buffer has spi_piosize words, and is aligned on spi_pioalign
	 * boundaries.  spi_piocnt buffers are mapped from this address
	 */
	__u64 spi_piobufbase;

	/*
	 * Base address of readonly memory copy of the pioavail registers.
	 * There are 2 bits for each buffer.
	 */
	__u64 spi_pioavailaddr;

	/*
	 * Address where driver updates a copy of the interface and driver
	 * status (IPATH_STATUS_*) as a 64 bit value.  It's followed by a
	 * string indicating hardware error, if there was one.
	 */
	__u64 spi_status;

	/* number of chip ports available to user processes */
	__u32 spi_nports;
	/* unit number of chip we are using */
	__u32 spi_unit;
	/* num bufs in each contiguous set */
	__u32 spi_rcv_egrperchunk;
	/* size in bytes of each contiguous set */
	__u32 spi_rcv_egrchunksize;
	/* total size of mmap to cover full rcvegrbuffers */
	__u32 spi_rcv_egrbuftotlen;
	__u32 spi_filler_for_align;
	/* address of readonly memory copy of the rcvhdrq tail register. */
	__u64 spi_rcvhdr_tailaddr;

	/* shared memory pages for subports if port is shared */
	__u64 spi_subport_uregbase;
	__u64 spi_subport_rcvegrbuf;
	__u64 spi_subport_rcvhdr_base;

	/* shared memory page for hardware port if it is shared */
	__u64 spi_port_uregbase;
	__u64 spi_port_rcvegrbuf;
	__u64 spi_port_rcvhdr_base;
	__u64 spi_port_rcvhdr_tailaddr;

} __attribute__ ((aligned(8)));


/*
 * This version number is given to the driver by the user code during
 * initialization in the spu_userversion field of ipath_user_info, so
 * the driver can check for compatibility with user code.
 *
 * The major version changes when data structures
 * change in an incompatible way.  The driver must be the same or higher
 * for initialization to succeed.  In some cases, a higher version
 * driver will not interoperate with older software, and initialization
 * will return an error.
 */
#define IPATH_USER_SWMAJOR 1

/*
 * Minor version differences are always compatible
 * a within a major version, however if user software is larger
 * than driver software, some new features and/or structure fields
 * may not be implemented; the user code must deal with this if it
 * cares, or it must abort after initialization reports the difference.
 */
#define IPATH_USER_SWMINOR 5

#define IPATH_USER_SWVERSION ((IPATH_USER_SWMAJOR<<16) | IPATH_USER_SWMINOR)

#define IPATH_KERN_TYPE 0

/*
 * Similarly, this is the kernel version going back to the user.  It's
 * slightly different, in that we want to tell if the driver was built as
 * part of a QLogic release, or from the driver from openfabrics.org,
 * kernel.org, or a standard distribution, for support reasons.
 * The high bit is 0 for non-QLogic and 1 for QLogic-built/supplied.
 *
 * It's returned by the driver to the user code during initialization in the
 * spi_sw_version field of ipath_base_info, so the user code can in turn
 * check for compatibility with the kernel.
*/
#define IPATH_KERN_SWVERSION ((IPATH_KERN_TYPE<<31) | IPATH_USER_SWVERSION)

/*
 * This structure is passed to ipath_userinit() to tell the driver where
 * user code buffers are, sizes, etc.   The offsets and sizes of the
 * fields must remain unchanged, for binary compatibility.  It can
 * be extended, if userversion is changed so user code can tell, if needed
 */
struct ipath_user_info {
	/*
	 * version of user software, to detect compatibility issues.
	 * Should be set to IPATH_USER_SWVERSION.
	 */
	__u32 spu_userversion;

	/* desired number of receive header queue entries */
	__u32 spu_rcvhdrcnt;

	/* size of struct base_info to write to */
	__u32 spu_base_info_size;

	/*
	 * number of words in KD protocol header
	 * This tells InfiniPath how many words to copy to rcvhdrq.  If 0,
	 * kernel uses a default.  Once set, attempts to set any other value
	 * are an error (EAGAIN) until driver is reloaded.
	 */
	__u32 spu_rcvhdrsize;

	/*
	 * If two or more processes wish to share a port, each process
	 * must set the spu_subport_cnt and spu_subport_id to the same
	 * values.  The only restriction on the spu_subport_id is that
	 * it be unique for a given node.
	 */
	__u16 spu_subport_cnt;
	__u16 spu_subport_id;

	__u32 spu_unused; /* kept for compatible layout */

	/*
	 * address of struct base_info to write to
	 */
	__u64 spu_base_info;

} __attribute__ ((aligned(8)));

/* User commands. */

#define IPATH_CMD_MIN		16

#define __IPATH_CMD_USER_INIT	16	/* old set up userspace (for old user code) */
#define IPATH_CMD_PORT_INFO	17	/* find out what resources we got */
#define IPATH_CMD_RECV_CTRL	18	/* control receipt of packets */
#define IPATH_CMD_TID_UPDATE	19	/* update expected TID entries */
#define IPATH_CMD_TID_FREE	20	/* free expected TID entries */
#define IPATH_CMD_SET_PART_KEY	21	/* add partition key */
#define __IPATH_CMD_SLAVE_INFO	22	/* return info on slave processes (for old user code) */
#define IPATH_CMD_ASSIGN_PORT	23	/* allocate HCA and port */
#define IPATH_CMD_USER_INIT 	24	/* set up userspace */
#define IPATH_CMD_UNUSED_1	25
#define IPATH_CMD_UNUSED_2	26
#define IPATH_CMD_PIOAVAILUPD	27	/* force an update of PIOAvail reg */

#define IPATH_CMD_MAX		27

struct ipath_port_info {
	__u32 num_active;	/* number of active units */
	__u32 unit;		/* unit (chip) assigned to caller */
	__u16 port;		/* port on unit assigned to caller */
	__u16 subport;		/* subport on unit assigned to caller */
	__u16 num_ports;	/* number of ports available on unit */
	__u16 num_subports;	/* number of subports opened on port */
};

struct ipath_tid_info {
	__u32 tidcnt;
	/* make structure same size in 32 and 64 bit */
	__u32 tid__unused;
	/* virtual address of first page in transfer */
	__u64 tidvaddr;
	/* pointer (same size 32/64 bit) to __u16 tid array */
	__u64 tidlist;

	/*
	 * pointer (same size 32/64 bit) to bitmap of TIDs used
	 * for this call; checked for being large enough at open
	 */
	__u64 tidmap;
};

struct ipath_cmd {
	__u32 type;			/* command type */
	union {
		struct ipath_tid_info tid_info;
		struct ipath_user_info user_info;
		/* address in userspace of struct ipath_port_info to
		   write result to */
		__u64 port_info;
		/* enable/disable receipt of packets */
		__u32 recv_ctrl;
		/* partition key to set */
		__u16 part_key;
		/* user address of __u32 bitmask of active slaves */
		__u64 slave_mask_addr;
	} cmd;
};

struct ipath_iovec {
	/* Pointer to data, but same size 32 and 64 bit */
	__u64 iov_base;

	/*
	 * Length of data; don't need 64 bits, but want
	 * ipath_sendpkt to remain same size as before 32 bit changes, so...
	 */
	__u64 iov_len;
};

/*
 * Describes a single packet for send.  Each packet can have one or more
 * buffers, but the total length (exclusive of IB headers) must be less
 * than the MTU, and if using the PIO method, entire packet length,
 * including IB headers, must be less than the ipath_piosize value (words).
 * Use of this necessitates including sys/uio.h
 */
struct __ipath_sendpkt {
	__u32 sps_flags;	/* flags for packet (TBD) */
	__u32 sps_cnt;		/* number of entries to use in sps_iov */
	/* array of iov's describing packet. TEMPORARY */
	struct ipath_iovec sps_iov[4];
};

/* Passed into diag data special file's ->write method. */
struct ipath_diag_pkt {
	__u32 unit;
	__u64 data;
	__u32 len;
};

/*
 * Data layout in I2C flash (for GUID, etc.)
 * All fields are little-endian binary unless otherwise stated
 */
#define IPATH_FLASH_VERSION 2
struct ipath_flash {
	/* flash layout version (IPATH_FLASH_VERSION) */
	__u8 if_fversion;
	/* checksum protecting if_length bytes */
	__u8 if_csum;
	/*
	 * valid length (in use, protected by if_csum), including
	 * if_fversion and if_csum themselves)
	 */
	__u8 if_length;
	/* the GUID, in network order */
	__u8 if_guid[8];
	/* number of GUIDs to use, starting from if_guid */
	__u8 if_numguid;
	/* the (last 10 characters of) board serial number, in ASCII */
	char if_serial[12];
	/* board mfg date (YYYYMMDD ASCII) */
	char if_mfgdate[8];
	/* last board rework/test date (YYYYMMDD ASCII) */
	char if_testdate[8];
	/* logging of error counts, TBD */
	__u8 if_errcntp[4];
	/* powered on hours, updated at driver unload */
	__u8 if_powerhour[2];
	/* ASCII free-form comment field */
	char if_comment[32];
	/* Backwards compatible prefix for longer QLogic Serial Numbers */
	char if_sprefix[4];
	/* 82 bytes used, min flash size is 128 bytes */
	__u8 if_future[46];
};

/*
 * These are the counters implemented in the chip, and are listed in order.
 * The InterCaps naming is taken straight from the chip spec.
 */
struct infinipath_counters {
	__u64 LBIntCnt;
	__u64 LBFlowStallCnt;
	__u64 Reserved1;
	__u64 TxUnsupVLErrCnt;
	__u64 TxDataPktCnt;
	__u64 TxFlowPktCnt;
	__u64 TxDwordCnt;
	__u64 TxLenErrCnt;
	__u64 TxMaxMinLenErrCnt;
	__u64 TxUnderrunCnt;
	__u64 TxFlowStallCnt;
	__u64 TxDroppedPktCnt;
	__u64 RxDroppedPktCnt;
	__u64 RxDataPktCnt;
	__u64 RxFlowPktCnt;
	__u64 RxDwordCnt;
	__u64 RxLenErrCnt;
	__u64 RxMaxMinLenErrCnt;
	__u64 RxICRCErrCnt;
	__u64 RxVCRCErrCnt;
	__u64 RxFlowCtrlErrCnt;
	__u64 RxBadFormatCnt;
	__u64 RxLinkProblemCnt;
	__u64 RxEBPCnt;
	__u64 RxLPCRCErrCnt;
	__u64 RxBufOvflCnt;
	__u64 RxTIDFullErrCnt;
	__u64 RxTIDValidErrCnt;
	__u64 RxPKeyMismatchCnt;
	__u64 RxP0HdrEgrOvflCnt;
	__u64 RxP1HdrEgrOvflCnt;
	__u64 RxP2HdrEgrOvflCnt;
	__u64 RxP3HdrEgrOvflCnt;
	__u64 RxP4HdrEgrOvflCnt;
	__u64 RxP5HdrEgrOvflCnt;
	__u64 RxP6HdrEgrOvflCnt;
	__u64 RxP7HdrEgrOvflCnt;
	__u64 RxP8HdrEgrOvflCnt;
	__u64 Reserved6;
	__u64 Reserved7;
	__u64 IBStatusChangeCnt;
	__u64 IBLinkErrRecoveryCnt;
	__u64 IBLinkDownedCnt;
	__u64 IBSymbolErrCnt;
};

/*
 * The next set of defines are for packet headers, and chip register
 * and memory bits that are visible to and/or used by user-mode software
 * The other bits that are used only by the driver or diags are in
 * ipath_registers.h
 */

/* RcvHdrFlags bits */
#define INFINIPATH_RHF_LENGTH_MASK 0x7FF
#define INFINIPATH_RHF_LENGTH_SHIFT 0
#define INFINIPATH_RHF_RCVTYPE_MASK 0x7
#define INFINIPATH_RHF_RCVTYPE_SHIFT 11
#define INFINIPATH_RHF_EGRINDEX_MASK 0x7FF
#define INFINIPATH_RHF_EGRINDEX_SHIFT 16
#define INFINIPATH_RHF_H_ICRCERR   0x80000000
#define INFINIPATH_RHF_H_VCRCERR   0x40000000
#define INFINIPATH_RHF_H_PARITYERR 0x20000000
#define INFINIPATH_RHF_H_LENERR    0x10000000
#define INFINIPATH_RHF_H_MTUERR    0x08000000
#define INFINIPATH_RHF_H_IHDRERR   0x04000000
#define INFINIPATH_RHF_H_TIDERR    0x02000000
#define INFINIPATH_RHF_H_MKERR     0x01000000
#define INFINIPATH_RHF_H_IBERR     0x00800000
#define INFINIPATH_RHF_L_SWA       0x00008000
#define INFINIPATH_RHF_L_SWB       0x00004000

/* infinipath header fields */
#define INFINIPATH_I_VERS_MASK 0xF
#define INFINIPATH_I_VERS_SHIFT 28
#define INFINIPATH_I_PORT_MASK 0xF
#define INFINIPATH_I_PORT_SHIFT 24
#define INFINIPATH_I_TID_MASK 0x7FF
#define INFINIPATH_I_TID_SHIFT 13
#define INFINIPATH_I_OFFSET_MASK 0x1FFF
#define INFINIPATH_I_OFFSET_SHIFT 0

/* K_PktFlags bits */
#define INFINIPATH_KPF_INTR 0x1
#define INFINIPATH_KPF_SUBPORT_MASK 0x3
#define INFINIPATH_KPF_SUBPORT_SHIFT 1

#define INFINIPATH_MAX_SUBPORT	4

/* SendPIO per-buffer control */
#define INFINIPATH_SP_TEST    0x40
#define INFINIPATH_SP_TESTEBP 0x20

/* SendPIOAvail bits */
#define INFINIPATH_SENDPIOAVAIL_BUSY_SHIFT 1
#define INFINIPATH_SENDPIOAVAIL_CHECK_SHIFT 0

/* infinipath header format */
struct ipath_header {
	/*
	 * Version - 4 bits, Port - 4 bits, TID - 10 bits and Offset -
	 * 14 bits before ECO change ~28 Dec 03.  After that, Vers 4,
	 * Port 4, TID 11, offset 13.
	 */
	__le32 ver_port_tid_offset;
	__le16 chksum;
	__le16 pkt_flags;
};

/* infinipath user message header format.
 * This structure contains the first 4 fields common to all protocols
 * that employ infinipath.
 */
struct ipath_message_header {
	__be16 lrh[4];
	__be32 bth[3];
	/* fields below this point are in host byte order */
	struct ipath_header iph;
	__u8 sub_opcode;
};

/* infinipath ethernet header format */
struct ether_header {
	__be16 lrh[4];
	__be32 bth[3];
	struct ipath_header iph;
	__u8 sub_opcode;
	__u8 cmd;
	__be16 lid;
	__u16 mac[3];
	__u8 frag_num;
	__u8 seq_num;
	__le32 len;
	/* MUST be of word size due to PIO write requirements */
	__le32 csum;
	__le16 csum_offset;
	__le16 flags;
	__u16 first_2_bytes;
	__u8 unused[2];		/* currently unused */
};


/* IB - LRH header consts */
#define IPATH_LRH_GRH 0x0003	/* 1. word of IB LRH - next header: GRH */
#define IPATH_LRH_BTH 0x0002	/* 1. word of IB LRH - next header: BTH */

/* misc. */
#define SIZE_OF_CRC 1

#define IPATH_DEFAULT_P_KEY 0xFFFF
#define IPATH_PERMISSIVE_LID 0xFFFF
#define IPATH_AETH_CREDIT_SHIFT 24
#define IPATH_AETH_CREDIT_MASK 0x1F
#define IPATH_AETH_CREDIT_INVAL 0x1F
#define IPATH_PSN_MASK 0xFFFFFF
#define IPATH_MSN_MASK 0xFFFFFF
#define IPATH_QPN_MASK 0xFFFFFF
#define IPATH_MULTICAST_LID_BASE 0xC000
#define IPATH_MULTICAST_QPN 0xFFFFFF

/* Receive Header Queue: receive type (from infinipath) */
#define RCVHQ_RCV_TYPE_EXPECTED  0
#define RCVHQ_RCV_TYPE_EAGER     1
#define RCVHQ_RCV_TYPE_NON_KD    2
#define RCVHQ_RCV_TYPE_ERROR     3


/* sub OpCodes - ith4x  */
#define IPATH_ITH4X_OPCODE_ENCAP 0x81
#define IPATH_ITH4X_OPCODE_LID_ARP 0x82

#define IPATH_HEADER_QUEUE_WORDS 9

/* functions for extracting fields from rcvhdrq entries for the driver.
 */
static inline __u32 ipath_hdrget_err_flags(const __le32 * rbuf)
{
	return __le32_to_cpu(rbuf[1]);
}

static inline __u32 ipath_hdrget_rcv_type(const __le32 * rbuf)
{
	return (__le32_to_cpu(rbuf[0]) >> INFINIPATH_RHF_RCVTYPE_SHIFT)
	    & INFINIPATH_RHF_RCVTYPE_MASK;
}

static inline __u32 ipath_hdrget_length_in_bytes(const __le32 * rbuf)
{
	return ((__le32_to_cpu(rbuf[0]) >> INFINIPATH_RHF_LENGTH_SHIFT)
		& INFINIPATH_RHF_LENGTH_MASK) << 2;
}

static inline __u32 ipath_hdrget_index(const __le32 * rbuf)
{
	return (__le32_to_cpu(rbuf[0]) >> INFINIPATH_RHF_EGRINDEX_SHIFT)
	    & INFINIPATH_RHF_EGRINDEX_MASK;
}

static inline __u32 ipath_hdrget_ipath_ver(__le32 hdrword)
{
	return (__le32_to_cpu(hdrword) >> INFINIPATH_I_VERS_SHIFT)
	    & INFINIPATH_I_VERS_MASK;
}

#endif				/* _IPATH_COMMON_H */
