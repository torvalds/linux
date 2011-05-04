/* bnx2x_init.h: Broadcom Everest network driver.
 *               Structures and macroes needed during the initialization.
 *
 * Copyright (c) 2007-2011 Broadcom Corporation
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

/****************************************************************************
* Parity configuration
****************************************************************************/
#define BLOCK_PRTY_INFO(block, en_mask, m1, m1h, m2) \
{ \
	block##_REG_##block##_PRTY_MASK, \
	block##_REG_##block##_PRTY_STS_CLR, \
	en_mask, {m1, m1h, m2}, #block \
}

#define BLOCK_PRTY_INFO_0(block, en_mask, m1, m1h, m2) \
{ \
	block##_REG_##block##_PRTY_MASK_0, \
	block##_REG_##block##_PRTY_STS_CLR_0, \
	en_mask, {m1, m1h, m2}, #block"_0" \
}

#define BLOCK_PRTY_INFO_1(block, en_mask, m1, m1h, m2) \
{ \
	block##_REG_##block##_PRTY_MASK_1, \
	block##_REG_##block##_PRTY_STS_CLR_1, \
	en_mask, {m1, m1h, m2}, #block"_1" \
}

static const struct {
	u32 mask_addr;
	u32 sts_clr_addr;
	u32 en_mask;		/* Mask to enable parity attentions */
	struct {
		u32 e1;		/* 57710 */
		u32 e1h;	/* 57711 */
		u32 e2;		/* 57712 */
	} reg_mask;		/* Register mask (all valid bits) */
	char name[7];		/* Block's longest name is 6 characters long
				 * (name + suffix)
				 */
} bnx2x_blocks_parity_data[] = {
	/* bit 19 masked */
	/* REG_WR(bp, PXP_REG_PXP_PRTY_MASK, 0x80000); */
	/* bit 5,18,20-31 */
	/* REG_WR(bp, PXP2_REG_PXP2_PRTY_MASK_0, 0xfff40020); */
	/* bit 5 */
	/* REG_WR(bp, PXP2_REG_PXP2_PRTY_MASK_1, 0x20);	*/
	/* REG_WR(bp, HC_REG_HC_PRTY_MASK, 0x0); */
	/* REG_WR(bp, MISC_REG_MISC_PRTY_MASK, 0x0); */

	/* Block IGU, MISC, PXP and PXP2 parity errors as long as we don't
	 * want to handle "system kill" flow at the moment.
	 */
	BLOCK_PRTY_INFO(PXP, 0x7ffffff, 0x3ffffff, 0x3ffffff, 0x7ffffff),
	BLOCK_PRTY_INFO_0(PXP2,	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff),
	BLOCK_PRTY_INFO_1(PXP2,	0x7ff, 0x7f, 0x7f, 0x7ff),
	BLOCK_PRTY_INFO(HC, 0x7, 0x7, 0x7, 0),
	BLOCK_PRTY_INFO(IGU, 0x7ff, 0, 0, 0x7ff),
	BLOCK_PRTY_INFO(MISC, 0x1, 0x1, 0x1, 0x1),
	BLOCK_PRTY_INFO(QM, 0, 0x1ff, 0xfff, 0xfff),
	BLOCK_PRTY_INFO(DORQ, 0, 0x3, 0x3, 0x3),
	{GRCBASE_UPB + PB_REG_PB_PRTY_MASK,
		GRCBASE_UPB + PB_REG_PB_PRTY_STS_CLR, 0,
		{0xf, 0xf, 0xf}, "UPB"},
	{GRCBASE_XPB + PB_REG_PB_PRTY_MASK,
		GRCBASE_XPB + PB_REG_PB_PRTY_STS_CLR, 0,
		{0xf, 0xf, 0xf}, "XPB"},
	BLOCK_PRTY_INFO(SRC, 0x4, 0x7, 0x7, 0x7),
	BLOCK_PRTY_INFO(CDU, 0, 0x1f, 0x1f, 0x1f),
	BLOCK_PRTY_INFO(CFC, 0, 0xf, 0xf, 0xf),
	BLOCK_PRTY_INFO(DBG, 0, 0x1, 0x1, 0x1),
	BLOCK_PRTY_INFO(DMAE, 0, 0xf, 0xf, 0xf),
	BLOCK_PRTY_INFO(BRB1, 0, 0xf, 0xf, 0xf),
	BLOCK_PRTY_INFO(PRS, (1<<6), 0xff, 0xff, 0xff),
	BLOCK_PRTY_INFO(TSDM, 0x18, 0x7ff, 0x7ff, 0x7ff),
	BLOCK_PRTY_INFO(CSDM, 0x8, 0x7ff, 0x7ff, 0x7ff),
	BLOCK_PRTY_INFO(USDM, 0x38, 0x7ff, 0x7ff, 0x7ff),
	BLOCK_PRTY_INFO(XSDM, 0x8, 0x7ff, 0x7ff, 0x7ff),
	BLOCK_PRTY_INFO_0(TSEM, 0, 0xffffffff, 0xffffffff, 0xffffffff),
	BLOCK_PRTY_INFO_1(TSEM, 0, 0x3, 0x1f, 0x3f),
	BLOCK_PRTY_INFO_0(USEM, 0, 0xffffffff, 0xffffffff, 0xffffffff),
	BLOCK_PRTY_INFO_1(USEM, 0, 0x3, 0x1f, 0x1f),
	BLOCK_PRTY_INFO_0(CSEM, 0, 0xffffffff, 0xffffffff, 0xffffffff),
	BLOCK_PRTY_INFO_1(CSEM, 0, 0x3, 0x1f, 0x1f),
	BLOCK_PRTY_INFO_0(XSEM, 0, 0xffffffff, 0xffffffff, 0xffffffff),
	BLOCK_PRTY_INFO_1(XSEM, 0, 0x3, 0x1f, 0x3f),
};


/* [28] MCP Latched rom_parity
 * [29] MCP Latched ump_rx_parity
 * [30] MCP Latched ump_tx_parity
 * [31] MCP Latched scpad_parity
 */
#define MISC_AEU_ENABLE_MCP_PRTY_BITS	\
	(AEU_INPUTS_ATTN_BITS_MCP_LATCHED_ROM_PARITY | \
	 AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_RX_PARITY | \
	 AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_TX_PARITY | \
	 AEU_INPUTS_ATTN_BITS_MCP_LATCHED_SCPAD_PARITY)

/* Below registers control the MCP parity attention output. When
 * MISC_AEU_ENABLE_MCP_PRTY_BITS are set - attentions are
 * enabled, when cleared - disabled.
 */
static const u32 mcp_attn_ctl_regs[] = {
	MISC_REG_AEU_ENABLE4_FUNC_0_OUT_0,
	MISC_REG_AEU_ENABLE4_NIG_0,
	MISC_REG_AEU_ENABLE4_PXP_0,
	MISC_REG_AEU_ENABLE4_FUNC_1_OUT_0,
	MISC_REG_AEU_ENABLE4_NIG_1,
	MISC_REG_AEU_ENABLE4_PXP_1
};

static inline void bnx2x_set_mcp_parity(struct bnx2x *bp, u8 enable)
{
	int i;
	u32 reg_val;

	for (i = 0; i < ARRAY_SIZE(mcp_attn_ctl_regs); i++) {
		reg_val = REG_RD(bp, mcp_attn_ctl_regs[i]);

		if (enable)
			reg_val |= MISC_AEU_ENABLE_MCP_PRTY_BITS;
		else
			reg_val &= ~MISC_AEU_ENABLE_MCP_PRTY_BITS;

		REG_WR(bp, mcp_attn_ctl_regs[i], reg_val);
	}
}

static inline u32 bnx2x_parity_reg_mask(struct bnx2x *bp, int idx)
{
	if (CHIP_IS_E1(bp))
		return bnx2x_blocks_parity_data[idx].reg_mask.e1;
	else if (CHIP_IS_E1H(bp))
		return bnx2x_blocks_parity_data[idx].reg_mask.e1h;
	else
		return bnx2x_blocks_parity_data[idx].reg_mask.e2;
}

static inline void bnx2x_disable_blocks_parity(struct bnx2x *bp)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(bnx2x_blocks_parity_data); i++) {
		u32 dis_mask = bnx2x_parity_reg_mask(bp, i);

		if (dis_mask) {
			REG_WR(bp, bnx2x_blocks_parity_data[i].mask_addr,
			       dis_mask);
			DP(NETIF_MSG_HW, "Setting parity mask "
						 "for %s to\t\t0x%x\n",
				    bnx2x_blocks_parity_data[i].name, dis_mask);
		}
	}

	/* Disable MCP parity attentions */
	bnx2x_set_mcp_parity(bp, false);
}

/**
 * Clear the parity error status registers.
 */
static inline void bnx2x_clear_blocks_parity(struct bnx2x *bp)
{
	int i;
	u32 reg_val, mcp_aeu_bits =
		AEU_INPUTS_ATTN_BITS_MCP_LATCHED_ROM_PARITY |
		AEU_INPUTS_ATTN_BITS_MCP_LATCHED_SCPAD_PARITY |
		AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_RX_PARITY |
		AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_TX_PARITY;

	/* Clear SEM_FAST parities */
	REG_WR(bp, XSEM_REG_FAST_MEMORY + SEM_FAST_REG_PARITY_RST, 0x1);
	REG_WR(bp, TSEM_REG_FAST_MEMORY + SEM_FAST_REG_PARITY_RST, 0x1);
	REG_WR(bp, USEM_REG_FAST_MEMORY + SEM_FAST_REG_PARITY_RST, 0x1);
	REG_WR(bp, CSEM_REG_FAST_MEMORY + SEM_FAST_REG_PARITY_RST, 0x1);

	for (i = 0; i < ARRAY_SIZE(bnx2x_blocks_parity_data); i++) {
		u32 reg_mask = bnx2x_parity_reg_mask(bp, i);

		if (reg_mask) {
			reg_val = REG_RD(bp, bnx2x_blocks_parity_data[i].
					 sts_clr_addr);
			if (reg_val & reg_mask)
				DP(NETIF_MSG_HW,
					    "Parity errors in %s: 0x%x\n",
					    bnx2x_blocks_parity_data[i].name,
					    reg_val & reg_mask);
		}
	}

	/* Check if there were parity attentions in MCP */
	reg_val = REG_RD(bp, MISC_REG_AEU_AFTER_INVERT_4_MCP);
	if (reg_val & mcp_aeu_bits)
		DP(NETIF_MSG_HW, "Parity error in MCP: 0x%x\n",
		   reg_val & mcp_aeu_bits);

	/* Clear parity attentions in MCP:
	 * [7]  clears Latched rom_parity
	 * [8]  clears Latched ump_rx_parity
	 * [9]  clears Latched ump_tx_parity
	 * [10] clears Latched scpad_parity (both ports)
	 */
	REG_WR(bp, MISC_REG_AEU_CLR_LATCH_SIGNAL, 0x780);
}

static inline void bnx2x_enable_blocks_parity(struct bnx2x *bp)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(bnx2x_blocks_parity_data); i++) {
		u32 reg_mask = bnx2x_parity_reg_mask(bp, i);

		if (reg_mask)
			REG_WR(bp, bnx2x_blocks_parity_data[i].mask_addr,
				bnx2x_blocks_parity_data[i].en_mask & reg_mask);
	}

	/* Enable MCP parity attentions */
	bnx2x_set_mcp_parity(bp, true);
}


#endif /* BNX2X_INIT_H */

