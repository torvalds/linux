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

/* RAM0 size in bytes */
#define STORM_INTMEM_SIZE_E1		0x5800
#define STORM_INTMEM_SIZE_E1H		0x10000
#define STORM_INTMEM_SIZE(bp) ((CHIP_IS_E1(bp) ? STORM_INTMEM_SIZE_E1 : \
						    STORM_INTMEM_SIZE_E1H) / 4)


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
/* Never reorder stages !!! */
#define COMMON_STAGE		0
#define PORT0_STAGE		1
#define PORT1_STAGE		2
#define FUNC0_STAGE		3
#define FUNC1_STAGE		4
#define FUNC2_STAGE		5
#define FUNC3_STAGE		6
#define FUNC4_STAGE		7
#define FUNC5_STAGE		8
#define FUNC6_STAGE		9
#define FUNC7_STAGE		10
#define STAGE_IDX_MAX		11

#define STAGE_START		0
#define STAGE_END		1


/* Indices of blocks */
#define PRS_BLOCK		0
#define SRCH_BLOCK		1
#define TSDM_BLOCK		2
#define TCM_BLOCK		3
#define BRB1_BLOCK		4
#define TSEM_BLOCK		5
#define PXPCS_BLOCK		6
#define EMAC0_BLOCK		7
#define EMAC1_BLOCK		8
#define DBU_BLOCK		9
#define MISC_BLOCK		10
#define DBG_BLOCK		11
#define NIG_BLOCK		12
#define MCP_BLOCK		13
#define UPB_BLOCK		14
#define CSDM_BLOCK		15
#define USDM_BLOCK		16
#define CCM_BLOCK		17
#define UCM_BLOCK		18
#define USEM_BLOCK		19
#define CSEM_BLOCK		20
#define XPB_BLOCK		21
#define DQ_BLOCK		22
#define TIMERS_BLOCK		23
#define XSDM_BLOCK		24
#define QM_BLOCK		25
#define PBF_BLOCK		26
#define XCM_BLOCK		27
#define XSEM_BLOCK		28
#define CDU_BLOCK		29
#define DMAE_BLOCK		30
#define PXP_BLOCK		31
#define CFC_BLOCK		32
#define HC_BLOCK		33
#define PXP2_BLOCK		34
#define MISC_AEU_BLOCK		35
#define PGLUE_B_BLOCK		36
#define IGU_BLOCK		37
#define ATC_BLOCK		38
#define QM_4PORT_BLOCK		39
#define XSEM_4PORT_BLOCK		40


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

#define INITOP_SET		0	/* set the HW directly */
#define INITOP_CLEAR		1	/* clear the HW directly */
#define INITOP_INIT		2	/* set the init-value array */

/****************************************************************************
* ILT management
****************************************************************************/
struct ilt_line {
	dma_addr_t page_mapping;
	void *page;
	u32 size;
};

struct ilt_client_info {
	u32 page_size;
	u16 start;
	u16 end;
	u16 client_num;
	u16 flags;
#define ILT_CLIENT_SKIP_INIT	0x1
#define ILT_CLIENT_SKIP_MEM	0x2
};

struct bnx2x_ilt {
	u32 start_line;
	struct ilt_line		*lines;
	struct ilt_client_info	clients[4];
#define ILT_CLIENT_CDU	0
#define ILT_CLIENT_QM	1
#define ILT_CLIENT_SRC	2
#define ILT_CLIENT_TM	3
};

/****************************************************************************
* SRC configuration
****************************************************************************/
struct src_ent {
	u8 opaque[56];
	u64 next;
};

#endif /* BNX2X_INIT_H */

