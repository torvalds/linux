/* bnx2x_init.h: Broadcom Everest network driver.
 *
 * Copyright (c) 2007-2008 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Eliezer Tamir <eliezert@broadcom.com>
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

#define STORM_INTMEM_SIZE		(0x5800 / 4)
#define TSTORM_INTMEM_ADDR		0x1a0000
#define CSTORM_INTMEM_ADDR		0x220000
#define XSTORM_INTMEM_ADDR		0x2a0000
#define USTORM_INTMEM_ADDR		0x320000


/* Init operation types and structures */

#define OP_RD			0x1 /* read single register */
#define OP_WR			0x2 /* write single register */
#define OP_IW			0x3 /* write single register using mailbox */
#define OP_SW			0x4 /* copy a string to the device */
#define OP_SI			0x5 /* copy a string using mailbox */
#define OP_ZR			0x6 /* clear memory */
#define OP_ZP			0x7 /* unzip then copy with DMAE */
#define OP_WB			0x8 /* copy a string using DMAE */

struct raw_op {
	u32 op		:8;
	u32 offset	:24;
	u32 raw_data;
};

struct op_read {
	u32 op		:8;
	u32 offset	:24;
	u32 pad;
};

struct op_write {
	u32 op		:8;
	u32 offset	:24;
	u32 val;
};

struct op_string_write {
	u32 op		:8;
	u32 offset	:24;
#ifdef __LITTLE_ENDIAN
	u16 data_off;
	u16 data_len;
#else /* __BIG_ENDIAN */
	u16 data_len;
	u16 data_off;
#endif
};

struct op_zero {
	u32 op		:8;
	u32 offset	:24;
	u32 len;
};

union init_op {
	struct op_read		read;
	struct op_write		write;
	struct op_string_write	str_wr;
	struct op_zero		zero;
	struct raw_op		raw;
};

#include "bnx2x_init_values.h"

static void bnx2x_reg_wr_ind(struct bnx2x *bp, u32 addr, u32 val);

static void bnx2x_write_dmae(struct bnx2x *bp, dma_addr_t dma_addr,
			     u32 dst_addr, u32 len32);

static int bnx2x_gunzip(struct bnx2x *bp, u8 *zbuf, int len);

static void bnx2x_init_str_wr(struct bnx2x *bp, u32 addr, const u32 *data,
			      u32 len)
{
	int i;

	for (i = 0; i < len; i++) {
		REG_WR(bp, addr + i*4, data[i]);
		if (!(i % 10000)) {
			touch_softlockup_watchdog();
			cpu_relax();
		}
	}
}

#define INIT_MEM_WR(reg, data, reg_off, len) \
	bnx2x_init_str_wr(bp, reg + reg_off*4, data, len)

static void bnx2x_init_ind_wr(struct bnx2x *bp, u32 addr, const u32 *data,
			      u16 len)
{
	int i;

	for (i = 0; i < len; i++) {
		REG_WR_IND(bp, addr + i*4, data[i]);
		if (!(i % 10000)) {
			touch_softlockup_watchdog();
			cpu_relax();
		}
	}
}

static void bnx2x_init_wr_wb(struct bnx2x *bp, u32 addr, const u32 *data,
			     u32 len, int gunzip)
{
	int offset = 0;

	if (gunzip) {
		int rc;
#ifdef __BIG_ENDIAN
		int i, size;
		u32 *temp;

		temp = kmalloc(len, GFP_KERNEL);
		size = (len / 4) + ((len % 4) ? 1 : 0);
		for (i = 0; i < size; i++)
			temp[i] = swab32(data[i]);
		data = temp;
#endif
		rc = bnx2x_gunzip(bp, (u8 *)data, len);
		if (rc) {
			DP(NETIF_MSG_HW, "gunzip failed ! rc %d\n", rc);
			return;
		}
		len = bp->gunzip_outlen;
#ifdef __BIG_ENDIAN
		kfree(temp);
		for (i = 0; i < len; i++)
			 ((u32 *)bp->gunzip_buf)[i] =
					swab32(((u32 *)bp->gunzip_buf)[i]);
#endif
	} else {
		if ((len * 4) > FW_BUF_SIZE) {
			BNX2X_ERR("LARGE DMAE OPERATION ! len 0x%x\n", len*4);
			return;
		}
		memcpy(bp->gunzip_buf, data, len * 4);
	}

	while (len > DMAE_LEN32_MAX) {
		bnx2x_write_dmae(bp, bp->gunzip_mapping + offset,
				 addr + offset, DMAE_LEN32_MAX);
		offset += DMAE_LEN32_MAX * 4;
		len -= DMAE_LEN32_MAX;
	}
	bnx2x_write_dmae(bp, bp->gunzip_mapping + offset, addr + offset, len);
}

#define INIT_MEM_WB(reg, data, reg_off, len) \
	bnx2x_init_wr_wb(bp, reg + reg_off*4, data, len, 0)

#define INIT_GUNZIP_DMAE(reg, data, reg_off, len) \
	bnx2x_init_wr_wb(bp, reg + reg_off*4, data, len, 1)

static void bnx2x_init_fill(struct bnx2x *bp, u32 addr, int fill, u32 len)
{
	int offset = 0;

	if ((len * 4) > FW_BUF_SIZE) {
		BNX2X_ERR("LARGE DMAE OPERATION ! len 0x%x\n", len * 4);
		return;
	}
	memset(bp->gunzip_buf, fill, len * 4);

	while (len > DMAE_LEN32_MAX) {
		bnx2x_write_dmae(bp, bp->gunzip_mapping + offset,
				 addr + offset, DMAE_LEN32_MAX);
		offset += DMAE_LEN32_MAX * 4;
		len -= DMAE_LEN32_MAX;
	}
	bnx2x_write_dmae(bp, bp->gunzip_mapping + offset, addr + offset, len);
}

static void bnx2x_init_block(struct bnx2x *bp, u32 op_start, u32 op_end)
{
	int i;
	union init_op *op;
	u32 op_type, addr, len;
	const u32 *data;

	for (i = op_start; i < op_end; i++) {

		op = (union init_op *)&(init_ops[i]);

		op_type = op->str_wr.op;
		addr = op->str_wr.offset;
		len = op->str_wr.data_len;
		data = init_data + op->str_wr.data_off;

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
			bnx2x_init_wr_wb(bp, addr, data, len, 0);
			break;
		case OP_SI:
			bnx2x_init_ind_wr(bp, addr, data, len);
			break;
		case OP_ZR:
			bnx2x_init_fill(bp, addr, 0, op->zero.len);
			break;
		case OP_ZP:
			bnx2x_init_wr_wb(bp, addr, data, len, 1);
			break;
		default:
			BNX2X_ERR("BAD init operation!\n");
		}
	}
}


/****************************************************************************
* PXP
****************************************************************************/
/*
 * This code configures the PCI read/write arbiter
 * which implements a wighted round robin
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
	{{8 , 64 , 25}, {16 , 64 , 25}, {32 , 64 , 25}, {64 , 64 , 41} },
	{{4 , 8 , 4},   {4 , 8 , 4},    {4 , 8 , 4},    {4 , 8 , 4} },
	{{4 , 3 , 3},   {4 , 3 , 3},    {4 , 3 , 3},    {4 , 3 , 3} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {16 , 3 , 11},  {16 , 3 , 11} },
	{{8 , 64 , 25}, {16 , 64 , 25}, {32 , 64 , 25}, {64 , 64 , 41} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {64 , 3 , 41} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {64 , 3 , 41} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {64 , 3 , 41} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {64 , 3 , 41} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {32 , 3 , 21} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {32 , 3 , 21} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {32 , 3 , 21} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {32 , 3 , 21} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {32 , 3 , 21} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {32 , 3 , 21} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {32 , 3 , 21} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {32 , 3 , 21} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {32 , 3 , 21} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {32 , 3 , 21} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {32 , 3 , 21} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {32 , 3 , 21} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {32 , 3 , 21} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {32 , 3 , 21} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {32 , 3 , 21} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {32 , 3 , 21} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {32 , 3 , 21} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {32 , 3 , 21} },
	{{8 , 3 , 6},   {16 , 3 , 11},  {32 , 3 , 21},  {32 , 3 , 21} },
	{{8 , 64 , 25}, {16 , 64 , 41}, {32 , 64 , 81}, {64 , 64 , 120} }
};

/* derived configuration for each write queue for each max request size */
static const struct arb_line write_arb_data[NUM_WR_Q][MAX_WR_ORD + 1] = {
	{{4 , 6 , 3},   {4 , 6 , 3},    {4 , 6 , 3} },
	{{4 , 2 , 3},   {4 , 2 , 3},    {4 , 2 , 3} },
	{{8 , 2 , 6},   {16 , 2 , 11},  {16 , 2 , 11} },
	{{8 , 2 , 6},   {16 , 2 , 11},  {32 , 2 , 21} },
	{{8 , 2 , 6},   {16 , 2 , 11},  {32 , 2 , 21} },
	{{8 , 2 , 6},   {16 , 2 , 11},  {32 , 2 , 21} },
	{{8 , 64 , 25}, {16 , 64 , 25}, {32 , 64 , 25} },
	{{8 , 2 , 6},   {16 , 2 , 11},  {16 , 2 , 11} },
	{{8 , 2 , 6},   {16 , 2 , 11},  {16 , 2 , 11} },
	{{8 , 9 , 6},   {16 , 9 , 11},  {32 , 9 , 21} },
	{{8 , 47 , 19}, {16 , 47 , 19}, {32 , 47 , 21} },
	{{8 , 9 , 6},   {16 , 9 , 11},  {16 , 9 , 11} },
	{{8 , 64 , 25}, {16 , 64 , 41}, {32 , 64 , 81} }
};

/* register adresses for read queues */
static const struct arb_line read_arb_addr[NUM_RD_Q-1] = {
	{PXP2_REG_RQ_BW_RD_L0, PXP2_REG_RQ_BW_RD_ADD0,
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
	{PXP2_REG_PSWRQ_BW_L9, PXP2_REG_PSWRQ_BW_ADD9,
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
	{PXP2_REG_RQ_BW_RD_L19, PXP2_REG_RQ_BW_RD_ADD19,
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

/* register adresses for wrtie queues */
static const struct arb_line write_arb_addr[NUM_WR_Q-1] = {
	{PXP2_REG_PSWRQ_BW_L1, PXP2_REG_PSWRQ_BW_ADD1,
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
	{PXP2_REG_PSWRQ_BW_L28, PXP2_REG_PSWRQ_BW_ADD28,
		PXP2_REG_PSWRQ_BW_UB28},
	{PXP2_REG_RQ_BW_WR_L29, PXP2_REG_RQ_BW_WR_ADD29,
		PXP2_REG_RQ_BW_WR_UBOUND29},
	{PXP2_REG_RQ_BW_WR_L30, PXP2_REG_RQ_BW_WR_ADD30,
		PXP2_REG_RQ_BW_WR_UBOUND30}
};

static void bnx2x_init_pxp(struct bnx2x *bp)
{
	int r_order, w_order;
	u32 val, i;

	pci_read_config_word(bp->pdev,
			     bp->pcie_cap + PCI_EXP_DEVCTL, (u16 *)&val);
	DP(NETIF_MSG_HW, "read 0x%x from devctl\n", (u16)val);
	w_order = ((val & PCI_EXP_DEVCTL_PAYLOAD) >> 5);
	r_order = ((val & PCI_EXP_DEVCTL_READRQ) >> 12);

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
	REG_WR(bp, PXP2_REG_WR_DMAE_TH, (128 << w_order)/16);
}


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

/*****************************************************************************
 * Description:
 *         Calculates crc 8 on a word value: polynomial 0-1-2-8
 *         Code was translated from Verilog.
 ****************************************************************************/
static u8 calc_crc8(u32 data, u8 crc)
{
	u8 D[32];
	u8 NewCRC[8];
	u8 C[8];
	u8 crc_res;
	u8 i;

	/* split the data into 31 bits */
	for (i = 0; i < 32; i++) {
		D[i] = data & 1;
		data = data >> 1;
	}

	/* split the crc into 8 bits */
	for (i = 0; i < 8; i++) {
		C[i] = crc & 1;
		crc = crc >> 1;
	}

	NewCRC[0] = D[31] ^ D[30] ^ D[28] ^ D[23] ^ D[21] ^ D[19] ^ D[18] ^
		D[16] ^ D[14] ^ D[12] ^ D[8] ^ D[7] ^ D[6] ^ D[0] ^ C[4] ^
		C[6] ^ C[7];
	NewCRC[1] = D[30] ^ D[29] ^ D[28] ^ D[24] ^ D[23] ^ D[22] ^ D[21] ^
		D[20] ^ D[18] ^ D[17] ^ D[16] ^ D[15] ^ D[14] ^ D[13] ^
		D[12] ^ D[9] ^ D[6] ^ D[1] ^ D[0] ^ C[0] ^ C[4] ^ C[5] ^ C[6];
	NewCRC[2] = D[29] ^ D[28] ^ D[25] ^ D[24] ^ D[22] ^ D[17] ^ D[15] ^
		D[13] ^ D[12] ^ D[10] ^ D[8] ^ D[6] ^ D[2] ^ D[1] ^ D[0] ^
		C[0] ^ C[1] ^ C[4] ^ C[5];
	NewCRC[3] = D[30] ^ D[29] ^ D[26] ^ D[25] ^ D[23] ^ D[18] ^ D[16] ^
		D[14] ^ D[13] ^ D[11] ^ D[9] ^ D[7] ^ D[3] ^ D[2] ^ D[1] ^
		C[1] ^ C[2] ^ C[5] ^ C[6];
	NewCRC[4] = D[31] ^ D[30] ^ D[27] ^ D[26] ^ D[24] ^ D[19] ^ D[17] ^
		D[15] ^ D[14] ^ D[12] ^ D[10] ^ D[8] ^ D[4] ^ D[3] ^ D[2] ^
		C[0] ^ C[2] ^ C[3] ^ C[6] ^ C[7];
	NewCRC[5] = D[31] ^ D[28] ^ D[27] ^ D[25] ^ D[20] ^ D[18] ^ D[16] ^
		D[15] ^ D[13] ^ D[11] ^ D[9] ^ D[5] ^ D[4] ^ D[3] ^ C[1] ^
		C[3] ^ C[4] ^ C[7];
	NewCRC[6] = D[29] ^ D[28] ^ D[26] ^ D[21] ^ D[19] ^ D[17] ^ D[16] ^
		D[14] ^ D[12] ^ D[10] ^ D[6] ^ D[5] ^ D[4] ^ C[2] ^ C[4] ^
		C[5];
	NewCRC[7] = D[30] ^ D[29] ^ D[27] ^ D[22] ^ D[20] ^ D[18] ^ D[17] ^
		D[15] ^ D[13] ^ D[11] ^ D[7] ^ D[6] ^ D[5] ^ C[3] ^ C[5] ^
		C[6];

	crc_res = 0;
	for (i = 0; i < 8; i++)
		crc_res |= (NewCRC[i] << i);

	return crc_res;
}


#endif /* BNX2X_INIT_H */

