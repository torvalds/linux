/* bnx2x_init.h: Broadcom Everest network driver.
 *
 * Copyright (c) 2007-2009 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Maintained by: Eilon Greenstein <eilong@broadcom.com>
 * Written by: Eliezer Tamir
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

/* Operation specific for E1 */
#define OP_RD_E1		0xa /* read single register */
#define OP_WR_E1		0xb /* write single register */
#define OP_IW_E1		0xc /* write single register using mailbox */
#define OP_SW_E1		0xd /* copy a string to the device */
#define OP_SI_E1		0xe /* copy a string using mailbox */
#define OP_ZR_E1		0xf /* clear memory */
#define OP_ZP_E1		0x10 /* unzip then copy with DMAE */
#define OP_WR_64_E1		0x11 /* write 64 bit pattern on E1 */
#define OP_WB_E1		0x12 /* copy a string using DMAE */

/* Operation specific for E1H */
#define OP_RD_E1H		0x13 /* read single register */
#define OP_WR_E1H		0x14 /* write single register */
#define OP_IW_E1H		0x15 /* write single register using mailbox */
#define OP_SW_E1H		0x16 /* copy a string to the device */
#define OP_SI_E1H		0x17 /* copy a string using mailbox */
#define OP_ZR_E1H		0x18 /* clear memory */
#define OP_ZP_E1H		0x19 /* unzip then copy with DMAE */
#define OP_WR_64_E1H		0x1a /* write 64 bit pattern on E1H */
#define OP_WB_E1H		0x1b /* copy a string using DMAE */

/* FPGA and EMUL specific operations */
#define OP_WR_EMUL_E1H		0x1c /* write single register on E1H Emul */
#define OP_WR_EMUL		0x1d /* write single register on Emulation */
#define OP_WR_FPGA		0x1e /* write single register on FPGA */
#define OP_WR_ASIC		0x1f /* write single register on ASIC */


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

#include "bnx2x_init_values.h"

static void bnx2x_reg_wr_ind(struct bnx2x *bp, u32 addr, u32 val);
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

static void bnx2x_write_big_buf(struct bnx2x *bp, u32 addr, u32 len)
{
	int offset = 0;

	if (bp->dmae_ready) {
		while (len > DMAE_LEN32_WR_MAX) {
			bnx2x_write_dmae(bp, bp->gunzip_mapping + offset,
					 addr + offset, DMAE_LEN32_WR_MAX);
			offset += DMAE_LEN32_WR_MAX * 4;
			len -= DMAE_LEN32_WR_MAX;
		}
		bnx2x_write_dmae(bp, bp->gunzip_mapping + offset,
				 addr + offset, len);
	} else
		bnx2x_init_str_wr(bp, addr, bp->gunzip_buf, len);
}

static void bnx2x_init_fill(struct bnx2x *bp, u32 addr, int fill, u32 len)
{
	u32 buf_len = (((len * 4) > FW_BUF_SIZE) ? FW_BUF_SIZE : (len * 4));
	u32 buf_len32 = buf_len / 4;
	int i;

	memset(bp->gunzip_buf, fill, buf_len);

	for (i = 0; i < len; i += buf_len32) {
		u32 cur_len = min(buf_len32, len - i);

		bnx2x_write_big_buf(bp, addr + i * 4, cur_len);
	}
}

static void bnx2x_init_wr_64(struct bnx2x *bp, u32 addr, const u32 *data,
			     u32 len64)
{
	u32 buf_len32 = FW_BUF_SIZE / 4;
	u32 len = len64 * 2;
	u64 data64 = 0;
	int i;

	/* 64 bit value is in a blob: first low DWORD, then high DWORD */
	data64 = HILO_U64((*(data + 1)), (*data));
	len64 = min((u32)(FW_BUF_SIZE/8), len64);
	for (i = 0; i < len64; i++) {
		u64 *pdata = ((u64 *)(bp->gunzip_buf)) + i;

		*pdata = data64;
	}

	for (i = 0; i < len; i += buf_len32) {
		u32 cur_len = min(buf_len32, len - i);

		bnx2x_write_big_buf(bp, addr + i * 4, cur_len);
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

static const u32 *bnx2x_sel_blob(u32 addr, const u32 *data, int is_e1)
{
	IF_IS_INT_TABLE_ADDR(TSEM_REG_INT_TABLE, addr)
		data = is_e1 ? tsem_int_table_data_e1 :
			       tsem_int_table_data_e1h;
	else
		IF_IS_INT_TABLE_ADDR(CSEM_REG_INT_TABLE, addr)
			data = is_e1 ? csem_int_table_data_e1 :
				       csem_int_table_data_e1h;
	else
		IF_IS_INT_TABLE_ADDR(USEM_REG_INT_TABLE, addr)
			data = is_e1 ? usem_int_table_data_e1 :
				       usem_int_table_data_e1h;
	else
		IF_IS_INT_TABLE_ADDR(XSEM_REG_INT_TABLE, addr)
			data = is_e1 ? xsem_int_table_data_e1 :
				       xsem_int_table_data_e1h;
	else
		IF_IS_PRAM_ADDR(TSEM_REG_PRAM, addr)
			data = is_e1 ? tsem_pram_data_e1 : tsem_pram_data_e1h;
	else
		IF_IS_PRAM_ADDR(CSEM_REG_PRAM, addr)
			data = is_e1 ? csem_pram_data_e1 : csem_pram_data_e1h;
	else
		IF_IS_PRAM_ADDR(USEM_REG_PRAM, addr)
			data = is_e1 ? usem_pram_data_e1 : usem_pram_data_e1h;
	else
		IF_IS_PRAM_ADDR(XSEM_REG_PRAM, addr)
			data = is_e1 ? xsem_pram_data_e1 : xsem_pram_data_e1h;

	return data;
}

static void bnx2x_init_wr_wb(struct bnx2x *bp, u32 addr, const u32 *data,
			     u32 len, int gunzip, int is_e1, u32 blob_off)
{
	int offset = 0;

	data = bnx2x_sel_blob(addr, data, is_e1) + blob_off;

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
			BNX2X_ERR("gunzip failed ! rc %d\n", rc);
#ifdef __BIG_ENDIAN
			kfree(temp);
#endif
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
			BNX2X_ERR("LARGE DMAE OPERATION ! "
				  "addr 0x%x  len 0x%x\n", addr, len*4);
			return;
		}
		memcpy(bp->gunzip_buf, data, len * 4);
	}

	if (bp->dmae_ready) {
		while (len > DMAE_LEN32_WR_MAX) {
			bnx2x_write_dmae(bp, bp->gunzip_mapping + offset,
					 addr + offset, DMAE_LEN32_WR_MAX);
			offset += DMAE_LEN32_WR_MAX * 4;
			len -= DMAE_LEN32_WR_MAX;
		}
		bnx2x_write_dmae(bp, bp->gunzip_mapping + offset,
				 addr + offset, len);
	} else
		bnx2x_init_ind_wr(bp, addr, bp->gunzip_buf, len);
}

static void bnx2x_init_block(struct bnx2x *bp, u32 op_start, u32 op_end)
{
	int is_e1       = CHIP_IS_E1(bp);
	int is_e1h      = CHIP_IS_E1H(bp);
	int is_emul_e1h = (CHIP_REV_IS_EMUL(bp) && is_e1h);
	int hw_wr, i;
	union init_op *op;
	u32 op_type, addr, len;
	const u32 *data, *data_base;

	if (CHIP_REV_IS_FPGA(bp))
		hw_wr = OP_WR_FPGA;
	else if (CHIP_REV_IS_EMUL(bp))
		hw_wr = OP_WR_EMUL;
	else
		hw_wr = OP_WR_ASIC;

	if (is_e1)
		data_base = init_data_e1;
	else /* CHIP_IS_E1H(bp) */
		data_base = init_data_e1h;

	for (i = op_start; i < op_end; i++) {

		op = (union init_op *)&(init_ops[i]);

		op_type = op->str_wr.op;
		addr = op->str_wr.offset;
		len = op->str_wr.data_len;
		data = data_base + op->str_wr.data_off;

		/* careful! it must be in order */
		if (unlikely(op_type > OP_WB)) {

			/* If E1 only */
			if (op_type <= OP_WB_E1) {
				if (is_e1)
					op_type -= (OP_RD_E1 - OP_RD);

			/* If E1H only */
			} else if (op_type <= OP_WB_E1H) {
				if (is_e1h)
					op_type -= (OP_RD_E1H - OP_RD);
			}

			/* HW/EMUL specific */
			if (op_type == hw_wr)
				op_type = OP_WR;

			/* EMUL on E1H is special */
			if ((op_type == OP_WR_EMUL_E1H) && is_emul_e1h)
				op_type = OP_WR;
		}

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
			bnx2x_init_wr_wb(bp, addr, data, len, 0, is_e1, 0);
			break;
		case OP_SI:
			bnx2x_init_ind_wr(bp, addr, data, len);
			break;
		case OP_ZR:
			bnx2x_init_fill(bp, addr, 0, op->zero.len);
			break;
		case OP_ZP:
			bnx2x_init_wr_wb(bp, addr, data, len, 1, is_e1,
					 op->str_wr.data_off);
			break;
		case OP_WR_64:
			bnx2x_init_wr_64(bp, addr, data, len);
			break;
		default:
			/* happens whenever an op is of a diff HW */
#if 0
			DP(NETIF_MSG_HW, "skipping init operation  "
			   "index %d[%d:%d]: type %d  addr 0x%x  "
			   "len %d(0x%x)\n",
			   i, op_start, op_end, op_type, addr, len, len);
#endif
			break;
		}
	}
}


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

static void bnx2x_init_pxp(struct bnx2x *bp)
{
	u16 devctl;
	int r_order, w_order;
	u32 val, i;

	pci_read_config_word(bp->pdev,
			     bp->pcie_cap + PCI_EXP_DEVCTL, &devctl);
	DP(NETIF_MSG_HW, "read 0x%x from devctl\n", devctl);
	w_order = ((devctl & PCI_EXP_DEVCTL_PAYLOAD) >> 5);
	if (bp->mrrs == -1)
		r_order = ((devctl & PCI_EXP_DEVCTL_READRQ) >> 12);
	else {
		DP(NETIF_MSG_HW, "force read order to %d\n", bp->mrrs);
		r_order = bp->mrrs;
	}

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

/* registers addresses are not in order
   so these arrays help simplify the code */
static const int cm_start[E1H_FUNC_MAX][9] = {
	{MISC_FUNC0_START, TCM_FUNC0_START, UCM_FUNC0_START, CCM_FUNC0_START,
	 XCM_FUNC0_START, TSEM_FUNC0_START, USEM_FUNC0_START, CSEM_FUNC0_START,
	 XSEM_FUNC0_START},
	{MISC_FUNC1_START, TCM_FUNC1_START, UCM_FUNC1_START, CCM_FUNC1_START,
	 XCM_FUNC1_START, TSEM_FUNC1_START, USEM_FUNC1_START, CSEM_FUNC1_START,
	 XSEM_FUNC1_START},
	{MISC_FUNC2_START, TCM_FUNC2_START, UCM_FUNC2_START, CCM_FUNC2_START,
	 XCM_FUNC2_START, TSEM_FUNC2_START, USEM_FUNC2_START, CSEM_FUNC2_START,
	 XSEM_FUNC2_START},
	{MISC_FUNC3_START, TCM_FUNC3_START, UCM_FUNC3_START, CCM_FUNC3_START,
	 XCM_FUNC3_START, TSEM_FUNC3_START, USEM_FUNC3_START, CSEM_FUNC3_START,
	 XSEM_FUNC3_START},
	{MISC_FUNC4_START, TCM_FUNC4_START, UCM_FUNC4_START, CCM_FUNC4_START,
	 XCM_FUNC4_START, TSEM_FUNC4_START, USEM_FUNC4_START, CSEM_FUNC4_START,
	 XSEM_FUNC4_START},
	{MISC_FUNC5_START, TCM_FUNC5_START, UCM_FUNC5_START, CCM_FUNC5_START,
	 XCM_FUNC5_START, TSEM_FUNC5_START, USEM_FUNC5_START, CSEM_FUNC5_START,
	 XSEM_FUNC5_START},
	{MISC_FUNC6_START, TCM_FUNC6_START, UCM_FUNC6_START, CCM_FUNC6_START,
	 XCM_FUNC6_START, TSEM_FUNC6_START, USEM_FUNC6_START, CSEM_FUNC6_START,
	 XSEM_FUNC6_START},
	{MISC_FUNC7_START, TCM_FUNC7_START, UCM_FUNC7_START, CCM_FUNC7_START,
	 XCM_FUNC7_START, TSEM_FUNC7_START, USEM_FUNC7_START, CSEM_FUNC7_START,
	 XSEM_FUNC7_START}
};

static const int cm_end[E1H_FUNC_MAX][9] = {
	{MISC_FUNC0_END, TCM_FUNC0_END, UCM_FUNC0_END, CCM_FUNC0_END,
	 XCM_FUNC0_END, TSEM_FUNC0_END, USEM_FUNC0_END, CSEM_FUNC0_END,
	 XSEM_FUNC0_END},
	{MISC_FUNC1_END, TCM_FUNC1_END, UCM_FUNC1_END, CCM_FUNC1_END,
	 XCM_FUNC1_END, TSEM_FUNC1_END, USEM_FUNC1_END, CSEM_FUNC1_END,
	 XSEM_FUNC1_END},
	{MISC_FUNC2_END, TCM_FUNC2_END, UCM_FUNC2_END, CCM_FUNC2_END,
	 XCM_FUNC2_END, TSEM_FUNC2_END, USEM_FUNC2_END, CSEM_FUNC2_END,
	 XSEM_FUNC2_END},
	{MISC_FUNC3_END, TCM_FUNC3_END, UCM_FUNC3_END, CCM_FUNC3_END,
	 XCM_FUNC3_END, TSEM_FUNC3_END, USEM_FUNC3_END, CSEM_FUNC3_END,
	 XSEM_FUNC3_END},
	{MISC_FUNC4_END, TCM_FUNC4_END, UCM_FUNC4_END, CCM_FUNC4_END,
	 XCM_FUNC4_END, TSEM_FUNC4_END, USEM_FUNC4_END, CSEM_FUNC4_END,
	 XSEM_FUNC4_END},
	{MISC_FUNC5_END, TCM_FUNC5_END, UCM_FUNC5_END, CCM_FUNC5_END,
	 XCM_FUNC5_END, TSEM_FUNC5_END, USEM_FUNC5_END, CSEM_FUNC5_END,
	 XSEM_FUNC5_END},
	{MISC_FUNC6_END, TCM_FUNC6_END, UCM_FUNC6_END, CCM_FUNC6_END,
	 XCM_FUNC6_END, TSEM_FUNC6_END, USEM_FUNC6_END, CSEM_FUNC6_END,
	 XSEM_FUNC6_END},
	{MISC_FUNC7_END, TCM_FUNC7_END, UCM_FUNC7_END, CCM_FUNC7_END,
	 XCM_FUNC7_END, TSEM_FUNC7_END, USEM_FUNC7_END, CSEM_FUNC7_END,
	 XSEM_FUNC7_END},
};

static const int hc_limits[E1H_FUNC_MAX][2] = {
	{HC_FUNC0_START, HC_FUNC0_END},
	{HC_FUNC1_START, HC_FUNC1_END},
	{HC_FUNC2_START, HC_FUNC2_END},
	{HC_FUNC3_START, HC_FUNC3_END},
	{HC_FUNC4_START, HC_FUNC4_END},
	{HC_FUNC5_START, HC_FUNC5_END},
	{HC_FUNC6_START, HC_FUNC6_END},
	{HC_FUNC7_START, HC_FUNC7_END}
};

#endif /* BNX2X_INIT_H */

