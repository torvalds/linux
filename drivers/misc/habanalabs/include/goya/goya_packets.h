/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2017-2018 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GOYA_PACKETS_H
#define GOYA_PACKETS_H

#include <linux/types.h>

#define PACKET_HEADER_PACKET_ID_SHIFT		56
#define PACKET_HEADER_PACKET_ID_MASK		0x1F00000000000000ull

enum packet_id {
	PACKET_WREG_32 = 0x1,
	PACKET_WREG_BULK = 0x2,
	PACKET_MSG_LONG = 0x3,
	PACKET_MSG_SHORT = 0x4,
	PACKET_CP_DMA = 0x5,
	PACKET_MSG_PROT = 0x7,
	PACKET_FENCE = 0x8,
	PACKET_LIN_DMA = 0x9,
	PACKET_NOP = 0xA,
	PACKET_STOP = 0xB,
	MAX_PACKET_ID = (PACKET_HEADER_PACKET_ID_MASK >>
				PACKET_HEADER_PACKET_ID_SHIFT) + 1
};

#define GOYA_PKT_CTL_OPCODE_SHIFT	24
#define GOYA_PKT_CTL_OPCODE_MASK	0x1F000000

#define GOYA_PKT_CTL_EB_SHIFT		29
#define GOYA_PKT_CTL_EB_MASK		0x20000000

#define GOYA_PKT_CTL_RB_SHIFT		30
#define GOYA_PKT_CTL_RB_MASK		0x40000000

#define GOYA_PKT_CTL_MB_SHIFT		31
#define GOYA_PKT_CTL_MB_MASK		0x80000000

/* All packets have, at least, an 8-byte header, which contains
 * the packet type. The kernel driver uses the packet header for packet
 * validation and to perform any necessary required preparation before
 * sending them off to the hardware.
 */
struct goya_packet {
	__le64 header;
	/* The rest of the packet data follows. Use the corresponding
	 * packet_XXX struct to deference the data, based on packet type
	 */
	u8 contents[];
};

struct packet_nop {
	__le32 reserved;
	__le32 ctl;
};

struct packet_stop {
	__le32 reserved;
	__le32 ctl;
};

#define GOYA_PKT_WREG32_CTL_REG_OFFSET_SHIFT	0
#define GOYA_PKT_WREG32_CTL_REG_OFFSET_MASK	0x0000FFFF

struct packet_wreg32 {
	__le32 value;
	__le32 ctl;
};

struct packet_wreg_bulk {
	__le32 size64;
	__le32 ctl;
	__le64 values[]; /* data starts here */
};

struct packet_msg_long {
	__le32 value;
	__le32 ctl;
	__le64 addr;
};

struct packet_msg_short {
	__le32 value;
	__le32 ctl;
};

struct packet_msg_prot {
	__le32 value;
	__le32 ctl;
	__le64 addr;
};

struct packet_fence {
	__le32 cfg;
	__le32 ctl;
};

#define GOYA_PKT_LIN_DMA_CTL_WO_SHIFT		0
#define GOYA_PKT_LIN_DMA_CTL_WO_MASK		0x00000001

#define GOYA_PKT_LIN_DMA_CTL_RDCOMP_SHIFT	1
#define GOYA_PKT_LIN_DMA_CTL_RDCOMP_MASK	0x00000002

#define GOYA_PKT_LIN_DMA_CTL_WRCOMP_SHIFT	2
#define GOYA_PKT_LIN_DMA_CTL_WRCOMP_MASK	0x00000004

#define GOYA_PKT_LIN_DMA_CTL_MEMSET_SHIFT	6
#define GOYA_PKT_LIN_DMA_CTL_MEMSET_MASK	0x00000040

#define GOYA_PKT_LIN_DMA_CTL_DMA_DIR_SHIFT	20
#define GOYA_PKT_LIN_DMA_CTL_DMA_DIR_MASK	0x00700000

struct packet_lin_dma {
	__le32 tsize;
	__le32 ctl;
	__le64 src_addr;
	__le64 dst_addr;
};

struct packet_cp_dma {
	__le32 tsize;
	__le32 ctl;
	__le64 src_addr;
};

#endif /* GOYA_PACKETS_H */
