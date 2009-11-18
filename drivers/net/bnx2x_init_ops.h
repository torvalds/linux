/* bnx2x_init_ops.h: Broadcom Everest network driver.
 *               Static functions needed during the initialization.
 *               This file is "included" in bnx2x_main.c.
 *
 * Copyright (c) 2007-2009 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Maintained by: Eilon Greenstein <eilong@broadcom.com>
 * Written by: Vladislav Zolotarov <vladz@broadcom.com>
 */

#ifndef BNX2X_INIT_OPS_H
#define BNX2X_INIT_OPS_H

static int bnx2x_gunzip(struct bnx2x *bp, const u8 *zbuf, int len);


static void bnx2x_init_str_wr(struct bnx2x *bp, u32 addr, const u32 *data,
			      u32 len)
{
	u32 i;

	for (i = 0; i < len; i++)
		REG_WR(bp, addr + i*4, data[i]);
}

static void bnx2x_init_ind_wr(struct bnx2x *bp, u32 addr, const u32 *data,
			      u32 len)
{
	u32 i;

	for (i = 0; i < len; i++)
		REG_WR_IND(bp, addr + i*4, data[i]);
}

static void bnx2x_write_big_buf(struct bnx2x *bp, u32 addr, u32 len)
{
	if (bp->dmae_ready)
		bnx2x_write_dmae_phys_len(bp, GUNZIP_PHYS(bp), addr, len);
	else
		bnx2x_init_str_wr(bp, addr, GUNZIP_BUF(bp), len);
}

static void bnx2x_init_fill(struct bnx2x *bp, u32 addr, int fill, u32 len)
{
	u32 buf_len = (((len*4) > FW_BUF_SIZE) ? FW_BUF_SIZE : (len*4));
	u32 buf_len32 = buf_len/4;
	u32 i;

	memset(GUNZIP_BUF(bp), (u8)fill, buf_len);

	for (i = 0; i < len; i += buf_len32) {
		u32 cur_len = min(buf_len32, len - i);

		bnx2x_write_big_buf(bp, addr + i*4, cur_len);
	}
}

static void bnx2x_init_wr_64(struct bnx2x *bp, u32 addr, const u32 *data,
			     u32 len64)
{
	u32 buf_len32 = FW_BUF_SIZE/4;
	u32 len = len64*2;
	u64 data64 = 0;
	u32 i;

	/* 64 bit value is in a blob: first low DWORD, then high DWORD */
	data64 = HILO_U64((*(data + 1)), (*data));

	len64 = min((u32)(FW_BUF_SIZE/8), len64);
	for (i = 0; i < len64; i++) {
		u64 *pdata = ((u64 *)(GUNZIP_BUF(bp))) + i;

		*pdata = data64;
	}

	for (i = 0; i < len; i += buf_len32) {
		u32 cur_len = min(buf_len32, len - i);

		bnx2x_write_big_buf(bp, addr + i*4, cur_len);
	}
}

/*********************************************************
   There are different blobs for each PRAM section.
   In addition, each blob write operation is divided into a few operations
   in order to decrease the amount of phys. contiguous buffer needed.
   Thus, when we select a blob the address may be with some offset
   from the beginning of PRAM section.
   The same holds for the INT_TABLE sections.
**********************************************************/
#define IF_IS_INT_TABLE_ADDR(base, addr) \
			if (((base) <= (addr)) && ((base) + 0x400 >= (addr)))

#define IF_IS_PRAM_ADDR(base, addr) \
			if (((base) <= (addr)) && ((base) + 0x40000 >= (addr)))

static const u8 *bnx2x_sel_blob(struct bnx2x *bp, u32 addr, const u8 *data)
{
	IF_IS_INT_TABLE_ADDR(TSEM_REG_INT_TABLE, addr)
		data = INIT_TSEM_INT_TABLE_DATA(bp);
	else
		IF_IS_INT_TABLE_ADDR(CSEM_REG_INT_TABLE, addr)
			data = INIT_CSEM_INT_TABLE_DATA(bp);
	else
		IF_IS_INT_TABLE_ADDR(USEM_REG_INT_TABLE, addr)
			data = INIT_USEM_INT_TABLE_DATA(bp);
	else
		IF_IS_INT_TABLE_ADDR(XSEM_REG_INT_TABLE, addr)
			data = INIT_XSEM_INT_TABLE_DATA(bp);
	else
		IF_IS_PRAM_ADDR(TSEM_REG_PRAM, addr)
			data = INIT_TSEM_PRAM_DATA(bp);
	else
		IF_IS_PRAM_ADDR(CSEM_REG_PRAM, addr)
			data = INIT_CSEM_PRAM_DATA(bp);
	else
		IF_IS_PRAM_ADDR(USEM_REG_PRAM, addr)
			data = INIT_USEM_PRAM_DATA(bp);
	else
		IF_IS_PRAM_ADDR(XSEM_REG_PRAM, addr)
			data = INIT_XSEM_PRAM_DATA(bp);

	return data;
}

static void bnx2x_write_big_buf_wb(struct bnx2x *bp, u32 addr, u32 len)
{
	if (bp->dmae_ready)
		bnx2x_write_dmae_phys_len(bp, GUNZIP_PHYS(bp), addr, len);
	else
		bnx2x_init_ind_wr(bp, addr, GUNZIP_BUF(bp), len);
}

static void bnx2x_init_wr_wb(struct bnx2x *bp, u32 addr, const u32 *data,
			     u32 len)
{
	data = (const u32 *)bnx2x_sel_blob(bp, addr, (const u8 *)data);

	if (bp->dmae_ready)
		VIRT_WR_DMAE_LEN(bp, data, addr, len);
	else
		bnx2x_init_ind_wr(bp, addr, data, len);
}

static void bnx2x_init_wr_zp(struct bnx2x *bp, u32 addr, u32 len, u32 blob_off)
{
	const u8 *data = NULL;
	int rc;
	u32 i;

	data = bnx2x_sel_blob(bp, addr, data) + blob_off*4;

	rc = bnx2x_gunzip(bp, data, len);
	if (rc)
		return;

	/* gunzip_outlen is in dwords */
	len = GUNZIP_OUTLEN(bp);
	for (i = 0; i < len; i++)
		((u32 *)GUNZIP_BUF(bp))[i] =
				cpu_to_le32(((u32 *)GUNZIP_BUF(bp))[i]);

	bnx2x_write_big_buf_wb(bp, addr, len);
}

static void bnx2x_init_block(struct bnx2x *bp, u32 block, u32 stage)
{
	u16 op_start =
		INIT_OPS_OFFSETS(bp)[BLOCK_OPS_IDX(block, stage, STAGE_START)];
	u16 op_end =
		INIT_OPS_OFFSETS(bp)[BLOCK_OPS_IDX(block, stage, STAGE_END)];
	union init_op *op;
	int hw_wr;
	u32 i, op_type, addr, len;
	const u32 *data, *data_base;

	/* If empty block */
	if (op_start == op_end)
		return;

	if (CHIP_REV_IS_FPGA(bp))
		hw_wr = OP_WR_FPGA;
	else if (CHIP_REV_IS_EMUL(bp))
		hw_wr = OP_WR_EMUL;
	else
		hw_wr = OP_WR_ASIC;

	data_base = INIT_DATA(bp);

	for (i = op_start; i < op_end; i++) {

		op = (union init_op *)&(INIT_OPS(bp)[i]);

		op_type = op->str_wr.op;
		addr = op->str_wr.offset;
		len = op->str_wr.data_len;
		data = data_base + op->str_wr.data_off;

		/* HW/EMUL specific */
		if ((op_type > OP_WB) && (op_type == hw_wr))
			op_type = OP_WR;

		switch (op_type) {
		case OP_RD:
			REG_RD(bp, addr);
			break;
		case OP_WR:
			REG_WR(bp, addr, op->write.val);
			break;
		case OP_SW:
			bnx2x_init_str_wr(bp, addr, data, len);
			break;
		case OP_WB:
			bnx2x_init_wr_wb(bp, addr, data, len);
			break;
		case OP_SI:
			bnx2x_init_ind_wr(bp, addr, data, len);
			break;
		case OP_ZR:
			bnx2x_init_fill(bp, addr, 0, op->zero.len);
			break;
		case OP_ZP:
			bnx2x_init_wr_zp(bp, addr, len,
					 op->str_wr.data_off);
			break;
		case OP_WR_64:
			bnx2x_init_wr_64(bp, addr, data, len);
			break;
		default:
			/* happens whenever an op is of a diff HW */
			break;
		}
	}
}


/****************************************************************************
* PXP Arbiter
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
	{ {8, 64, 6},  {16, 64, 11}, {32, 64, 21}, {32, 64, 21} },
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

static void bnx2x_init_pxp_arb(struct bnx2x *bp, int r_order, int w_order)
{
	u32 val, i;

	if (r_order > MAX_RD_ORD) {
		DP(NETIF_MSG_HW, "read order of %d  order adjusted to %d\n",
		   r_order, MAX_RD_ORD);
		r_order = MAX_RD_ORD;
	}
	if (w_order > MAX_WR_ORD) {
		DP(NETIF_MSG_HW, "write order of %d  order adjusted to %d\n",
		   w_order, MAX_WR_ORD);
		w_order = MAX_WR_ORD;
	}
	if (CHIP_REV_IS_FPGA(bp)) {
		DP(NETIF_MSG_HW, "write order adjusted to 1 for FPGA\n");
		w_order = 0;
	}
	DP(NETIF_MSG_HW, "read order %d  write order %d\n", r_order, w_order);

	for (i = 0; i < NUM_RD_Q-1; i++) {
		REG_WR(bp, read_arb_addr[i].l, read_arb_data[i][r_order].l);
		REG_WR(bp, read_arb_addr[i].add,
		       read_arb_data[i][r_order].add);
		REG_WR(bp, read_arb_addr[i].ubound,
		       read_arb_data[i][r_order].ubound);
	}

	for (i = 0; i < NUM_WR_Q-1; i++) {
		if ((write_arb_addr[i].l == PXP2_REG_RQ_BW_WR_L29) ||
		    (write_arb_addr[i].l == PXP2_REG_RQ_BW_WR_L30)) {

			REG_WR(bp, write_arb_addr[i].l,
			       write_arb_data[i][w_order].l);

			REG_WR(bp, write_arb_addr[i].add,
			       write_arb_data[i][w_order].add);

			REG_WR(bp, write_arb_addr[i].ubound,
			       write_arb_data[i][w_order].ubound);
		} else {

			val = REG_RD(bp, write_arb_addr[i].l);
			REG_WR(bp, write_arb_addr[i].l,
			       val | (write_arb_data[i][w_order].l << 10));

			val = REG_RD(bp, write_arb_addr[i].add);
			REG_WR(bp, write_arb_addr[i].add,
			       val | (write_arb_data[i][w_order].add << 10));

			val = REG_RD(bp, write_arb_addr[i].ubound);
			REG_WR(bp, write_arb_addr[i].ubound,
			       val | (write_arb_data[i][w_order].ubound << 7));
		}
	}

	val =  write_arb_data[NUM_WR_Q-1][w_order].add;
	val += write_arb_data[NUM_WR_Q-1][w_order].ubound << 10;
	val += write_arb_data[NUM_WR_Q-1][w_order].l << 17;
	REG_WR(bp, PXP2_REG_PSWRQ_BW_RD, val);

	val =  read_arb_data[NUM_RD_Q-1][r_order].add;
	val += read_arb_data[NUM_RD_Q-1][r_order].ubound << 10;
	val += read_arb_data[NUM_RD_Q-1][r_order].l << 17;
	REG_WR(bp, PXP2_REG_PSWRQ_BW_WR, val);

	REG_WR(bp, PXP2_REG_RQ_WR_MBS0, w_order);
	REG_WR(bp, PXP2_REG_RQ_WR_MBS1, w_order);
	REG_WR(bp, PXP2_REG_RQ_RD_MBS0, r_order);
	REG_WR(bp, PXP2_REG_RQ_RD_MBS1, r_order);

	if (r_order == MAX_RD_ORD)
		REG_WR(bp, PXP2_REG_RQ_PDR_LIMIT, 0xe00);

	REG_WR(bp, PXP2_REG_WR_USDMDP_TH, (0x18 << w_order));

	if (CHIP_IS_E1H(bp)) {
		/*    MPS      w_order     optimal TH      presently TH
		 *    128         0             0               2
		 *    256         1             1               3
		 *    >=512       2             2               3
		 */
		val = ((w_order == 0) ? 2 : 3);
		REG_WR(bp, PXP2_REG_WR_HC_MPS, val);
		REG_WR(bp, PXP2_REG_WR_USDM_MPS, val);
		REG_WR(bp, PXP2_REG_WR_CSDM_MPS, val);
		REG_WR(bp, PXP2_REG_WR_TSDM_MPS, val);
		REG_WR(bp, PXP2_REG_WR_XSDM_MPS, val);
		REG_WR(bp, PXP2_REG_WR_QM_MPS, val);
		REG_WR(bp, PXP2_REG_WR_TM_MPS, val);
		REG_WR(bp, PXP2_REG_WR_SRC_MPS, val);
		REG_WR(bp, PXP2_REG_WR_DBG_MPS, val);
		REG_WR(bp, PXP2_REG_WR_DMAE_MPS, 2); /* DMAE is special */
		REG_WR(bp, PXP2_REG_WR_CDU_MPS, val);
	}
}

#endif /* BNX2X_INIT_OPS_H */
