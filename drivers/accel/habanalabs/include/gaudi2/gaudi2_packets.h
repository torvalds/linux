/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GAUDI2_PACKETS_H
#define GAUDI2_PACKETS_H

#include <linux/types.h>

#define PACKET_HEADER_PACKET_ID_SHIFT		56
#define PACKET_HEADER_PACKET_ID_MASK		0x1F00000000000000ull

enum packet_id {
	PACKET_WREG_32 = 0x1,
	PACKET_WREG_BULK = 0x2,
	PACKET_MSG_LONG = 0x3,
	PACKET_MSG_SHORT = 0x4,
	PACKET_CP_DMA = 0x5,
	PACKET_REPEAT = 0x6,
	PACKET_MSG_PROT = 0x7,
	PACKET_FENCE = 0x8,
	PACKET_LIN_DMA = 0x9,
	PACKET_NOP = 0xA,
	PACKET_STOP = 0xB,
	PACKET_ARB_POINT = 0xC,
	PACKET_WAIT = 0xD,
	PACKET_CB_LIST = 0xE,
	PACKET_LOAD_AND_EXE = 0xF,
	PACKET_WRITE_ARC_STREAM = 0x10,
	PACKET_LAST_READ_FROM_ARC = 0x11,
	PACKET_WREG_64_SHORT = 0x12,
	PACKET_WREG_64_LONG = 0x13,
	MAX_PACKET_ID = (PACKET_HEADER_PACKET_ID_MASK >>
				PACKET_HEADER_PACKET_ID_SHIFT) + 1
};

#define GAUDI2_PKT_CTL_OPCODE_SHIFT	24
#define GAUDI2_PKT_CTL_OPCODE_MASK	0x1F000000

#define GAUDI2_PKT_CTL_EB_SHIFT		29
#define GAUDI2_PKT_CTL_EB_MASK		0x20000000

#define GAUDI2_PKT_CTL_RB_SHIFT		30
#define GAUDI2_PKT_CTL_RB_MASK		0x40000000

#define GAUDI2_PKT_CTL_MB_SHIFT		31
#define GAUDI2_PKT_CTL_MB_MASK		0x80000000

/* All packets have, at least, an 8-byte header, which contains
 * the packet type. The kernel driver uses the packet header for packet
 * validation and to perform any necessary required preparation before
 * sending them off to the hardware.
 */
struct gaudi2_packet {
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

#define GAUDI2_PKT_SHORT_VAL_SOB_SYNC_VAL_SHIFT	0
#define GAUDI2_PKT_SHORT_VAL_SOB_SYNC_VAL_MASK	0x00007FFF

#define GAUDI2_PKT_SHORT_VAL_SOB_MOD_SHIFT	31
#define GAUDI2_PKT_SHORT_VAL_SOB_MOD_MASK	0x80000000

#define GAUDI2_PKT_SHORT_VAL_MON_SYNC_GID_SHIFT	0
#define GAUDI2_PKT_SHORT_VAL_MON_SYNC_GID_MASK	0x000000FF

#define GAUDI2_PKT_SHORT_VAL_MON_MASK_SHIFT	8
#define GAUDI2_PKT_SHORT_VAL_MON_MASK_MASK	0x0000FF00

#define GAUDI2_PKT_SHORT_VAL_MON_MODE_SHIFT	16
#define GAUDI2_PKT_SHORT_VAL_MON_MODE_MASK	0x00010000

#define GAUDI2_PKT_SHORT_VAL_MON_SYNC_VAL_SHIFT	17
#define GAUDI2_PKT_SHORT_VAL_MON_SYNC_VAL_MASK	0xFFFE0000

#define GAUDI2_PKT_SHORT_CTL_ADDR_SHIFT		0
#define GAUDI2_PKT_SHORT_CTL_ADDR_MASK		0x0000FFFF

#define GAUDI2_PKT_SHORT_CTL_BASE_SHIFT		22
#define GAUDI2_PKT_SHORT_CTL_BASE_MASK		0x00C00000

struct packet_msg_short {
	__le32 value;
	__le32 ctl;
};

struct packet_msg_prot {
	__le32 value;
	__le32 ctl;
	__le64 addr;
};

#define GAUDI2_PKT_FENCE_CFG_DEC_VAL_SHIFT	0
#define GAUDI2_PKT_FENCE_CFG_DEC_VAL_MASK	0x0000000F

#define GAUDI2_PKT_FENCE_CFG_TARGET_VAL_SHIFT	16
#define GAUDI2_PKT_FENCE_CFG_TARGET_VAL_MASK	0x00FF0000

#define GAUDI2_PKT_FENCE_CFG_ID_SHIFT		30
#define GAUDI2_PKT_FENCE_CFG_ID_MASK		0xC0000000

#define GAUDI2_PKT_FENCE_CTL_PRED_SHIFT		0
#define GAUDI2_PKT_FENCE_CTL_PRED_MASK		0x0000001F

struct packet_fence {
	__le32 cfg;
	__le32 ctl;
};

#define GAUDI2_PKT_LIN_DMA_CTL_WRCOMP_SHIFT	0
#define GAUDI2_PKT_LIN_DMA_CTL_WRCOMP_MASK	0x00000001

#define GAUDI2_PKT_LIN_DMA_CTL_ENDIAN_SHIFT	1
#define GAUDI2_PKT_LIN_DMA_CTL_ENDIAN_MASK	0x00000006

#define GAUDI2_PKT_LIN_DMA_CTL_MEMSET_SHIFT	4
#define GAUDI2_PKT_LIN_DMA_CTL_MEMSET_MASK	0x00000010

#define GAUDI2_PKT_LIN_DMA_CTL_CONTEXT_ID_SHIFT	8
#define GAUDI2_PKT_LIN_DMA_CTL_CONTEXT_ID_MASK	0x00FFFF00

struct packet_lin_dma {
	__le32 tsize;
	__le32 ctl;
	__le64 src_addr;
	__le64 dst_addr;
};

struct packet_arb_point {
	__le32 cfg;
	__le32 ctl;
};

struct packet_repeat {
	__le32 cfg;
	__le32 ctl;
};

struct packet_wait {
	__le32 cfg;
	__le32 ctl;
};

struct packet_cb_list {
	__le32 reserved;
	__le32 ctl;
	__le64 index_addr;
	__le64 table_addr;
};

struct packet_load_and_exe {
	__le32 cfg;
	__le32 ctl;
	__le64 src_addr;
};

struct packet_cp_dma {
	__le32 tsize;
	__le32 ctl;
	__le64 src_addr;
};

#endif /* GAUDI2_PACKETS_H */
