/* SPDX-License-Identifier: (GPL-2.0 OR MIT)
 * Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2019 Google, Inc.
 */

/* GVE Transmit Descriptor formats */

#ifndef _GVE_DESC_H_
#define _GVE_DESC_H_

#include <linux/build_bug.h>

/* A note on seg_addrs
 *
 * Base addresses encoded in seg_addr are not assumed to be physical
 * addresses. The ring format assumes these come from some linear address
 * space. This could be physical memory, kernel virtual memory, user virtual
 * memory. gVNIC uses lists of registered pages. Each queue is assumed
 * to be associated with a single such linear address space to ensure a
 * consistent meaning for seg_addrs posted to its rings.
 */

struct gve_tx_pkt_desc {
	u8	type_flags;  /* desc type is lower 4 bits, flags upper */
	u8	l4_csum_offset;  /* relative offset of L4 csum word */
	u8	l4_hdr_offset;  /* Offset of start of L4 headers in packet */
	u8	desc_cnt;  /* Total descriptors for this packet */
	__be16	len;  /* Total length of this packet (in bytes) */
	__be16	seg_len;  /* Length of this descriptor's segment */
	__be64	seg_addr;  /* Base address (see note) of this segment */
} __packed;

struct gve_tx_seg_desc {
	u8	type_flags;	/* type is lower 4 bits, flags upper	*/
	u8	l3_offset;	/* TSO: 2 byte units to start of IPH	*/
	__be16	reserved;
	__be16	mss;		/* TSO MSS				*/
	__be16	seg_len;
	__be64	seg_addr;
} __packed;

/* GVE Transmit Descriptor Types */
#define	GVE_TXD_STD		(0x0 << 4) /* Std with Host Address	*/
#define	GVE_TXD_TSO		(0x1 << 4) /* TSO with Host Address	*/
#define	GVE_TXD_SEG		(0x2 << 4) /* Seg with Host Address	*/

/* GVE Transmit Descriptor Flags for Std Pkts */
#define	GVE_TXF_L4CSUM	BIT(0)	/* Need csum offload */
#define	GVE_TXF_TSTAMP	BIT(2)	/* Timestamp required */

/* GVE Transmit Descriptor Flags for TSO Segs */
#define	GVE_TXSF_IPV6	BIT(1)	/* IPv6 TSO */

/* GVE Receive Packet Descriptor */
/* The start of an ethernet packet comes 2 bytes into the rx buffer.
 * gVNIC adds this padding so that both the DMA and the L3/4 protocol header
 * access is aligned.
 */
#define GVE_RX_PAD 2

struct gve_rx_desc {
	u8	padding[48];
	__be32	rss_hash;  /* Receive-side scaling hash (Toeplitz for gVNIC) */
	__be16	mss;
	__be16	reserved;  /* Reserved to zero */
	u8	hdr_len;  /* Header length (L2-L4) including padding */
	u8	hdr_off;  /* 64-byte-scaled offset into RX_DATA entry */
	__sum16	csum;  /* 1's-complement partial checksum of L3+ bytes */
	__be16	len;  /* Length of the received packet */
	__be16	flags_seq;  /* Flags [15:3] and sequence number [2:0] (1-7) */
} __packed;
static_assert(sizeof(struct gve_rx_desc) == 64);

/* As with the Tx ring format, the qpl_offset entries below are offsets into an
 * ordered list of registered pages.
 */
struct gve_rx_data_slot {
	/* byte offset into the rx registered segment of this slot */
	__be64 qpl_offset;
};

/* GVE Recive Packet Descriptor Seq No */
#define GVE_SEQNO(x) (be16_to_cpu(x) & 0x7)

/* GVE Recive Packet Descriptor Flags */
#define GVE_RXFLG(x)	cpu_to_be16(1 << (3 + (x)))
#define	GVE_RXF_FRAG	GVE_RXFLG(3)	/* IP Fragment			*/
#define	GVE_RXF_IPV4	GVE_RXFLG(4)	/* IPv4				*/
#define	GVE_RXF_IPV6	GVE_RXFLG(5)	/* IPv6				*/
#define	GVE_RXF_TCP	GVE_RXFLG(6)	/* TCP Packet			*/
#define	GVE_RXF_UDP	GVE_RXFLG(7)	/* UDP Packet			*/
#define	GVE_RXF_ERR	GVE_RXFLG(8)	/* Packet Error Detected	*/

/* GVE IRQ */
#define GVE_IRQ_ACK	BIT(31)
#define GVE_IRQ_MASK	BIT(30)
#define GVE_IRQ_EVENT	BIT(29)

static inline bool gve_needs_rss(__be16 flag)
{
	if (flag & GVE_RXF_FRAG)
		return false;
	if (flag & (GVE_RXF_IPV4 | GVE_RXF_IPV6))
		return true;
	return false;
}

static inline u8 gve_next_seqno(u8 seq)
{
	return (seq + 1) == 8 ? 1 : seq + 1;
}
#endif /* _GVE_DESC_H_ */
