/* bnx2x_init.h: Broadcom Everest network driver.
 *               Structures and macroes needed during the initialization.
 *
 * Copyright (c) 2007-2009 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Maintained by: Eilon Greenstein <eilong@broadcom.com>
 * Written by: Eliezer Tamir
 * Modified by: Vladislav Zolotarov <vladz@broadcom.com>
 */

#ifndef BNX2X_INIT_H
#define BNX2X_INIT_H

#define COMMON				0x1
#define PORT0				0x2
#define PORT1				0x4

#define INIT_EMULATION			0x1
#define INIT_FPGA			0x2
#define INIT_ASIC			0x4
#define INIT_HARDWARE			0x7

#define TSTORM_INTMEM_ADDR		TSEM_REG_FAST_MEMORY
#define CSTORM_INTMEM_ADDR		CSEM_REG_FAST_MEMORY
#define XSTORM_INTMEM_ADDR		XSEM_REG_FAST_MEMORY
#define USTORM_INTMEM_ADDR		USEM_REG_FAST_MEMORY
/* RAM0 size in bytes */
#define STORM_INTMEM_SIZE_E1		0x5800
#define STORM_INTMEM_SIZE_E1H		0x10000
#define STORM_INTMEM_SIZE(bp)	((CHIP_IS_E1H(bp) ? STORM_INTMEM_SIZE_E1H : \
						    STORM_INTMEM_SIZE_E1) / 4)


/* Init operation types and structures */
/* Common for both E1 and E1H */
#define OP_RD			0x1 /* read single register */
#define OP_WR			0x2 /* write single register */
#define OP_IW			0x3 /* write single register using mailbox */
#define OP_SW			0x4 /* copy a string to the device */
#define OP_SI			0x5 /* copy a string using mailbox */
#define OP_ZR			0x6 /* clear memory */
#define OP_ZP			0x7 /* unzip then copy with DMAE */
#define OP_WR_64		0x8 /* write 64 bit pattern */
#define OP_WB			0x9 /* copy a string using DMAE */

/* FPGA and EMUL specific operations */
#define OP_WR_EMUL		0xa /* write single register on Emulation */
#define OP_WR_FPGA		0xb /* write single register on FPGA */
#define OP_WR_ASIC		0xc /* write single register on ASIC */

/* Init stages */
#define COMMON_STAGE            0
#define PORT0_STAGE     	1
#define PORT1_STAGE     	2
/* Never reorder FUNCx stages !!! */
#define FUNC0_STAGE     	3
#define FUNC1_STAGE     	4
#define FUNC2_STAGE     	5
#define FUNC3_STAGE     	6
#define FUNC4_STAGE     	7
#define FUNC5_STAGE     	8
#define FUNC6_STAGE     	9
#define FUNC7_STAGE     	10
#define STAGE_IDX_MAX   	11

#define STAGE_START     	0
#define STAGE_END       	1


/* Indices of blocks */
#define PRS_BLOCK               0
#define SRCH_BLOCK              1
#define TSDM_BLOCK              2
#define TCM_BLOCK               3
#define BRB1_BLOCK              4
#define TSEM_BLOCK              5
#define PXPCS_BLOCK             6
#define EMAC0_BLOCK             7
#define EMAC1_BLOCK             8
#define DBU_BLOCK               9
#define MISC_BLOCK              10
#define DBG_BLOCK               11
#define NIG_BLOCK               12
#define MCP_BLOCK               13
#define UPB_BLOCK               14
#define CSDM_BLOCK              15
#define USDM_BLOCK              16
#define CCM_BLOCK               17
#define UCM_BLOCK               18
#define USEM_BLOCK              19
#define CSEM_BLOCK              20
#define XPB_BLOCK               21
#define DQ_BLOCK                22
#define TIMERS_BLOCK            23
#define XSDM_BLOCK              24
#define QM_BLOCK                25
#define PBF_BLOCK               26
#define XCM_BLOCK               27
#define XSEM_BLOCK              28
#define CDU_BLOCK               29
#define DMAE_BLOCK              30
#define PXP_BLOCK               31
#define CFC_BLOCK               32
#define HC_BLOCK                33
#define PXP2_BLOCK              34
#define MISC_AEU_BLOCK          35

/* Returns the index of start or end of a specific block stage in ops array*/
#define BLOCK_OPS_IDX(block, stage, end) \
       (2*(((block)*STAGE_IDX_MAX) + (stage)) + (end))


struct raw_op {
	u32 op:8;
	u32 offset:24;
	u32 raw_data;
};

struct op_read {
	u32 op:8;
	u32 offset:24;
	u32 pad;
};

struct op_write {
	u32 op:8;
	u32 offset:24;
	u32 val;
};

struct op_string_write {
	u32 op:8;
	u32 offset:24;
#ifdef __LITTLE_ENDIAN
	u16 data_off;
	u16 data_len;
#else /* __BIG_ENDIAN */
	u16 data_len;
	u16 data_off;
#endif
};

struct op_zero {
	u32 op:8;
	u32 offset:24;
	u32 len;
};

union init_op {
	struct op_read		read;
	struct op_write		write;
	struct op_string_write	str_wr;
	struct op_zero		zero;
	struct raw_op		raw;
};

/****************************************************************************
* PXP
****************************************************************************/
/*
 * This code configures the PCI read/write arbiter
 * which implements a weighted round robin
 * between the virtual queues in the chip.
 *
 * The values were derived for each PCI max payload and max request size.
 * since max payload and max request size are only known at run time,
 * this is done as a separate init stage.
 */

#define NUM_WR_Q			13
#define NUM_RD_Q			29
#define MAX_RD_ORD			3
#define MAX_WR_ORD			2

/* configuration for one arbiter queue */
struct arb_line {
	int l;
	int add;
	int ubound;
};

/* derived configuration for each read queue for each max request size */
static const struct arb_line read_arb_data[NUM_RD_Q][MAX_RD_ORD + 1] = {
/* 1 */	{ {8, 64, 25}, {16, 64, 25}, {32, 64, 25}, {64, 64, 41} },
	{ {4, 8,  4},  {4,  8,  4},  {4,  8,  4},  {4,  8,  4}  },
	{ {4, 3,  3},  {4,  3,  3},  {4,  3,  3},  {4,  3,  3}  },
	{ {8, 3,  6},  {16, 3,  11}, {16, 3,  11}, {16, 3,  11} },
	{ {8, 64, 25}, {16, 64, 25}, {32, 64, 25}, {64, 64, 41} },
	{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {64, 3,  41} },
	{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {64, 3,  41} },
	{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {64, 3,  41} },
	{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {64, 3,  41} },
/* 10 */{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {32, 3,  21} },
	{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {32, 3,  21} },
	{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {32, 3,  21} },
	{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {32, 3,  21} },
	{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {32, 3,  21} },
	{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {32, 3,  21} },
	{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {32, 3,  21} },
	{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {32, 3,  21} },
	{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {32, 3,  21} },
	{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {32, 3,  21} },
/* 20 */{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {32, 3,  21} },
	{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {32, 3,  21} },
	{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {32, 3,  21} },
	{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {32, 3,  21} },
	{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {32, 3,  21} },
	{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {32, 3,  21} },
	{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {32, 3,  21} },
	{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {32, 3,  21} },
	{ {8, 3,  6},  {16, 3,  11}, {32, 3,  21}, {32, 3,  21} },
	{ {8, 64, 25}, {16, 64, 41}, {32, 64, 81}, {64, 64, 120} }
};

/* derived configuration for each write queue for each max request size */
static const struct arb_line write_arb_data[NUM_WR_Q][MAX_WR_ORD + 1] = {
/* 1 */	{ {4, 6,  3},  {4,  6,  3},  {4,  6,  3} },
	{ {4, 2,  3},  {4,  2,  3},  {4,  2,  3} },
	{ {8, 2,  6},  {16, 2,  11}, {16, 2,  11} },
	{ {8, 2,  6},  {16, 2,  11}, {32, 2,  21} },
	{ {8, 2,  6},  {16, 2,  11}, {32, 2,  21} },
	{ {8, 2,  6},  {16, 2,  11}, {32, 2,  21} },
	{ {8, 64, 25}, {16, 64, 25}, {32, 64, 25} },
	{ {8, 2,  6},  {16, 2,  11}, {16, 2,  11} },
	{ {8, 2,  6},  {16, 2,  11}, {16, 2,  11} },
/* 10 */{ {8, 9,  6},  {16, 9,  11}, {32, 9,  21} },
	{ {8, 47, 19}, {16, 47, 19}, {32, 47, 21} },
	{ {8, 9,  6},  {16, 9,  11}, {16, 9,  11} },
	{ {8, 64, 25}, {16, 64, 41}, {32, 64, 81} }
};

/* register addresses for read queues */
static const struct arb_line read_arb_addr[NUM_RD_Q-1] = {
/* 1 */	{PXP2_REG_RQ_BW_RD_L0, PXP2_REG_RQ_BW_RD_ADD0,
		PXP2_REG_RQ_BW_RD_UBOUND0},
	{PXP2_REG_PSWRQ_BW_L1, PXP2_REG_PSWRQ_BW_ADD1,
		PXP2_REG_PSWRQ_BW_UB1},
	{PXP2_REG_PSWRQ_BW_L2, PXP2_REG_PSWRQ_BW_ADD2,
		PXP2_REG_PSWRQ_BW_UB2},
	{PXP2_REG_PSWRQ_BW_L3, PXP2_REG_PSWRQ_BW_ADD3,
		PXP2_REG_PSWRQ_BW_UB3},
	{PXP2_REG_RQ_BW_RD_L4, PXP2_REG_RQ_BW_RD_ADD4,
		PXP2_REG_RQ_BW_RD_UBOUND4},
	{PXP2_REG_RQ_BW_RD_L5, PXP2_REG_RQ_BW_RD_ADD5,
		PXP2_REG_RQ_BW_RD_UBOUND5},
	{PXP2_REG_PSWRQ_BW_L6, PXP2_REG_PSWRQ_BW_ADD6,
		PXP2_REG_PSWRQ_BW_UB6},
	{PXP2_REG_PSWRQ_BW_L7, PXP2_REG_PSWRQ_BW_ADD7,
		PXP2_REG_PSWRQ_BW_UB7},
	{PXP2_REG_PSWRQ_BW_L8, PXP2_REG_PSWRQ_BW_ADD8,
		PXP2_REG_PSWRQ_BW_UB8},
/* 10 */{PXP2_REG_PSWRQ_BW_L9, PXP2_REG_PSWRQ_BW_ADD9,
		PXP2_REG_PSWRQ_BW_UB9},
	{PXP2_REG_PSWRQ_BW_L10, PXP2_REG_PSWRQ_BW_ADD10,
		PXP2_REG_PSWRQ_BW_UB10},
	{PXP2_REG_PSWRQ_BW_L11, PXP2_REG_PSWRQ_BW_ADD11,
		PXP2_REG_PSWRQ_BW_UB11},
	{PXP2_REG_RQ_BW_RD_L12, PXP2_REG_RQ_BW_RD_ADD12,
		PXP2_REG_RQ_BW_RD_UBOUND12},
	{PXP2_REG_RQ_BW_RD_L13, PXP2_REG_RQ_BW_RD_ADD13,
		PXP2_REG_RQ_BW_RD_UBOUND13},
	{PXP2_REG_RQ_BW_RD_L14, PXP2_REG_RQ_BW_RD_ADD14,
		PXP2_REG_RQ_BW_RD_UBOUND14},
	{PXP2_REG_RQ_BW_RD_L15, PXP2_REG_RQ_BW_RD_ADD15,
		PXP2_REG_RQ_BW_RD_UBOUND15},
	{PXP2_REG_RQ_BW_RD_L16, PXP2_REG_RQ_BW_RD_ADD16,
		PXP2_REG_RQ_BW_RD_UBOUND16},
	{PXP2_REG_RQ_BW_RD_L17, PXP2_REG_RQ_BW_RD_ADD17,
		PXP2_REG_RQ_BW_RD_UBOUND17},
	{PXP2_REG_RQ_BW_RD_L18, PXP2_REG_RQ_BW_RD_ADD18,
		PXP2_REG_RQ_BW_RD_UBOUND18},
/* 20 */{PXP2_REG_RQ_BW_RD_L19, PXP2_REG_RQ_BW_RD_ADD19,
		PXP2_REG_RQ_BW_RD_UBOUND19},
	{PXP2_REG_RQ_BW_RD_L20, PXP2_REG_RQ_BW_RD_ADD20,
		PXP2_REG_RQ_BW_RD_UBOUND20},
	{PXP2_REG_RQ_BW_RD_L22, PXP2_REG_RQ_BW_RD_ADD22,
		PXP2_REG_RQ_BW_RD_UBOUND22},
	{PXP2_REG_RQ_BW_RD_L23, PXP2_REG_RQ_BW_RD_ADD23,
		PXP2_REG_RQ_BW_RD_UBOUND23},
	{PXP2_REG_RQ_BW_RD_L24, PXP2_REG_RQ_BW_RD_ADD24,
		PXP2_REG_RQ_BW_RD_UBOUND24},
	{PXP2_REG_RQ_BW_RD_L25, PXP2_REG_RQ_BW_RD_ADD25,
		PXP2_REG_RQ_BW_RD_UBOUND25},
	{PXP2_REG_RQ_BW_RD_L26, PXP2_REG_RQ_BW_RD_ADD26,
		PXP2_REG_RQ_BW_RD_UBOUND26},
	{PXP2_REG_RQ_BW_RD_L27, PXP2_REG_RQ_BW_RD_ADD27,
		PXP2_REG_RQ_BW_RD_UBOUND27},
	{PXP2_REG_PSWRQ_BW_L28, PXP2_REG_PSWRQ_BW_ADD28,
		PXP2_REG_PSWRQ_BW_UB28}
};

/* register addresses for write queues */
static const struct arb_line write_arb_addr[NUM_WR_Q-1] = {
/* 1 */	{PXP2_REG_PSWRQ_BW_L1, PXP2_REG_PSWRQ_BW_ADD1,
		PXP2_REG_PSWRQ_BW_UB1},
	{PXP2_REG_PSWRQ_BW_L2, PXP2_REG_PSWRQ_BW_ADD2,
		PXP2_REG_PSWRQ_BW_UB2},
	{PXP2_REG_PSWRQ_BW_L3, PXP2_REG_PSWRQ_BW_ADD3,
		PXP2_REG_PSWRQ_BW_UB3},
	{PXP2_REG_PSWRQ_BW_L6, PXP2_REG_PSWRQ_BW_ADD6,
		PXP2_REG_PSWRQ_BW_UB6},
	{PXP2_REG_PSWRQ_BW_L7, PXP2_REG_PSWRQ_BW_ADD7,
		PXP2_REG_PSWRQ_BW_UB7},
	{PXP2_REG_PSWRQ_BW_L8, PXP2_REG_PSWRQ_BW_ADD8,
		PXP2_REG_PSWRQ_BW_UB8},
	{PXP2_REG_PSWRQ_BW_L9, PXP2_REG_PSWRQ_BW_ADD9,
		PXP2_REG_PSWRQ_BW_UB9},
	{PXP2_REG_PSWRQ_BW_L10, PXP2_REG_PSWRQ_BW_ADD10,
		PXP2_REG_PSWRQ_BW_UB10},
	{PXP2_REG_PSWRQ_BW_L11, PXP2_REG_PSWRQ_BW_ADD11,
		PXP2_REG_PSWRQ_BW_UB11},
/* 10 */{PXP2_REG_PSWRQ_BW_L28, PXP2_REG_PSWRQ_BW_ADD28,
		PXP2_REG_PSWRQ_BW_UB28},
	{PXP2_REG_RQ_BW_WR_L29, PXP2_REG_RQ_BW_WR_ADD29,
		PXP2_REG_RQ_BW_WR_UBOUND29},
	{PXP2_REG_RQ_BW_WR_L30, PXP2_REG_RQ_BW_WR_ADD30,
		PXP2_REG_RQ_BW_WR_UBOUND30}
};


/****************************************************************************
* CDU
****************************************************************************/

#define CDU_REGION_NUMBER_XCM_AG	2
#define CDU_REGION_NUMBER_UCM_AG	4

/**
 * String-to-compress [31:8] = CID (all 24 bits)
 * String-to-compress [7:4] = Region
 * String-to-compress [3:0] = Type
 */
#define CDU_VALID_DATA(_cid, _region, _type) \
		(((_cid) << 8) | (((_region) & 0xf) << 4) | (((_type) & 0xf)))
#define CDU_CRC8(_cid, _region, _type) \
			calc_crc8(CDU_VALID_DATA(_cid, _region, _type), 0xff)
#define CDU_RSRVD_VALUE_TYPE_A(_cid, _region, _type) \
			(0x80 | (CDU_CRC8(_cid, _region, _type) & 0x7f))
#define CDU_RSRVD_VALUE_TYPE_B(_crc, _type) \
	(0x80 | ((_type) & 0xf << 3) | (CDU_CRC8(_cid, _region, _type) & 0x7))
#define CDU_RSRVD_INVALIDATE_CONTEXT_VALUE(_val)	((_val) & ~0x80)


/* registers addresses are not in order
   so these arrays help simplify the code */
static const int cm_blocks[9] = {
	MISC_BLOCK, TCM_BLOCK,  UCM_BLOCK,  CCM_BLOCK, XCM_BLOCK,
	TSEM_BLOCK, USEM_BLOCK, CSEM_BLOCK, XSEM_BLOCK
};

#endif /* BNX2X_INIT_H */

