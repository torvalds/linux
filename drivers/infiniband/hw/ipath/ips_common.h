#ifndef IPS_COMMON_H
#define IPS_COMMON_H
/*
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

#include "ipath_common.h"

struct ipath_header {
	/*
	 * Version - 4 bits, Port - 4 bits, TID - 10 bits and Offset -
	 * 14 bits before ECO change ~28 Dec 03.  After that, Vers 4,
	 * Port 3, TID 11, offset 14.
	 */
	__le32 ver_port_tid_offset;
	__le16 chksum;
	__le16 pkt_flags;
};

struct ips_message_header {
	__be16 lrh[4];
	__be32 bth[3];
	/* fields below this point are in host byte order */
	struct ipath_header iph;
	__u8 sub_opcode;
	__u8 flags;
	__u16 src_rank;
	/* 24 bits. The upper 8 bit is available for other use */
	union {
		struct {
			unsigned ack_seq_num:24;
			unsigned port:4;
			unsigned unused:4;
		};
		__u32 ack_seq_num_org;
	};
	__u8 expected_tid_session_id;
	__u8 tinylen;		/* to aid MPI */
	union {
	    __u16 tag;		/* to aid MPI */
	    __u16 mqhdr;	/* for PSM MQ */
	};
	union {
		__u32 mpi[4];	/* to aid MPI */
		__u32 data[4];
		__u64 mq[2];	/* for PSM MQ */
		struct {
			__u16 mtu;
			__u8 major_ver;
			__u8 minor_ver;
			__u32 not_used;	//free
			__u32 run_id;
			__u32 client_ver;
		};
	};
};

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
	__u32 csum;
	__le16 csum_offset;
	__le16 flags;
	__u16 first_2_bytes;
	__u8 unused[2];		/* currently unused */
};

/*
 * The PIO buffer used for sending infinipath messages must only be written
 * in 32-bit words, all the data must be written, and no writes can occur
 * after the last word is written (which transfers "ownership" of the buffer
 * to the chip and triggers the message to be sent).
 * Since the Linux sk_buff structure can be recursive, non-aligned, and
 * any number of bytes in each segment, we use the following structure
 * to keep information about the overall state of the copy operation.
 * This is used to save the information needed to store the checksum
 * in the right place before sending the last word to the hardware and
 * to buffer the last 0-3 bytes of non-word sized segments.
 */
struct copy_data_s {
	struct ether_header *hdr;
	/* addr of PIO buf to write csum to */
	__u32 __iomem *csum_pio;
	__u32 __iomem *to;	/* addr of PIO buf to write data to */
	__u32 device;		/* which device to allocate PIO bufs from */
	__s32 error;		/* set if there is an error. */
	__s32 extra;		/* amount of data saved in u.buf below */
	__u32 len;		/* total length to send in bytes */
	__u32 flen;		/* frament length in words */
	__u32 csum;		/* partial IP checksum */
	__u32 pos;		/* position for partial checksum */
	__u32 offset;		/* offset to where data currently starts */
	__s32 checksum_calc;	/* set to 1 when csum has been calculated */
	struct sk_buff *skb;
	union {
		__u32 w;
		__u8 buf[4];
	} u;
};

/* IB - LRH header consts */
#define IPS_LRH_GRH 0x0003	/* 1. word of IB LRH - next header: GRH */
#define IPS_LRH_BTH 0x0002	/* 1. word of IB LRH - next header: BTH */

#define IPS_OFFSET  0

/*
 * defines the cut-off point between the header queue and eager/expected
 * TID queue
 */
#define NUM_OF_EXTRA_WORDS_IN_HEADER_QUEUE \
	((sizeof(struct ips_message_header) - \
	  offsetof(struct ips_message_header, iph)) >> 2)

/* OpCodes  */
#define OPCODE_IPS 0xC0
#define OPCODE_ITH4X 0xC1

/* OpCode 30 is use by stand-alone test programs  */
#define OPCODE_RAW_DATA 0xDE
/* last OpCode (31) is reserved for test  */
#define OPCODE_TEST 0xDF

/* sub OpCodes - ips  */
#define OPCODE_SEQ_DATA 0x01
#define OPCODE_SEQ_CTRL 0x02

#define OPCODE_SEQ_MQ_DATA 0x03
#define OPCODE_SEQ_MQ_CTRL 0x04

#define OPCODE_ACK 0x10
#define OPCODE_NAK 0x11

#define OPCODE_ERR_CHK 0x20
#define OPCODE_ERR_CHK_PLS 0x21

#define OPCODE_STARTUP 0x30
#define OPCODE_STARTUP_ACK 0x31
#define OPCODE_STARTUP_NAK 0x32

#define OPCODE_STARTUP_EXT 0x34
#define OPCODE_STARTUP_ACK_EXT 0x35
#define OPCODE_STARTUP_NAK_EXT 0x36

#define OPCODE_TIDS_RELEASE 0x40
#define OPCODE_TIDS_RELEASE_CONFIRM 0x41

#define OPCODE_CLOSE 0x50
#define OPCODE_CLOSE_ACK 0x51
/*
 * like OPCODE_CLOSE, but no complaint if other side has already closed.
 * Used when doing abort(), MPI_Abort(), etc.
 */
#define OPCODE_ABORT 0x52

/* sub OpCodes - ith4x  */
#define OPCODE_ENCAP 0x81
#define OPCODE_LID_ARP 0x82

/* Receive Header Queue: receive type (from infinipath) */
#define RCVHQ_RCV_TYPE_EXPECTED  0
#define RCVHQ_RCV_TYPE_EAGER     1
#define RCVHQ_RCV_TYPE_NON_KD    2
#define RCVHQ_RCV_TYPE_ERROR     3

/* misc. */
#define SIZE_OF_CRC 1

#define EAGER_TID_ID INFINIPATH_I_TID_MASK

#define IPS_DEFAULT_P_KEY 0xFFFF

#define IPS_PERMISSIVE_LID 0xFFFF
#define IPS_MULTICAST_LID_BASE 0xC000

#define IPS_AETH_CREDIT_SHIFT 24
#define IPS_AETH_CREDIT_MASK 0x1F
#define IPS_AETH_CREDIT_INVAL 0x1F

#define IPS_PSN_MASK 0xFFFFFF
#define IPS_MSN_MASK 0xFFFFFF
#define IPS_QPN_MASK 0xFFFFFF
#define IPS_MULTICAST_QPN 0xFFFFFF

/* functions for extracting fields from rcvhdrq entries */
static inline __u32 ips_get_hdr_err_flags(const __le32 * rbuf)
{
	return __le32_to_cpu(rbuf[1]);
}

static inline __u32 ips_get_index(const __le32 * rbuf)
{
	return (__le32_to_cpu(rbuf[0]) >> INFINIPATH_RHF_EGRINDEX_SHIFT)
	    & INFINIPATH_RHF_EGRINDEX_MASK;
}

static inline __u32 ips_get_rcv_type(const __le32 * rbuf)
{
	return (__le32_to_cpu(rbuf[0]) >> INFINIPATH_RHF_RCVTYPE_SHIFT)
	    & INFINIPATH_RHF_RCVTYPE_MASK;
}

static inline __u32 ips_get_length_in_bytes(const __le32 * rbuf)
{
	return ((__le32_to_cpu(rbuf[0]) >> INFINIPATH_RHF_LENGTH_SHIFT)
		& INFINIPATH_RHF_LENGTH_MASK) << 2;
}

static inline void *ips_get_first_protocol_header(const __u32 * rbuf)
{
	return (void *)&rbuf[2];
}

static inline struct ips_message_header *ips_get_ips_header(const __u32 *
							    rbuf)
{
	return (struct ips_message_header *)&rbuf[2];
}

static inline __u32 ips_get_ipath_ver(__le32 hdrword)
{
	return (__le32_to_cpu(hdrword) >> INFINIPATH_I_VERS_SHIFT)
	    & INFINIPATH_I_VERS_MASK;
}

#endif				/* IPS_COMMON_H */
