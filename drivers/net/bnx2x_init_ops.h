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

static void bnx2x_reg_wr_ind(struct bnx2x *bp, u32 addr, u32 val);
static int bnx2x_gunzip(struct bnx2x *bp, const u8 *zbuf, int len);

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

static const u8 *bnx2x_sel_blob(struct bnx2x *bp, u32 addr, const u8 *data)
{
	IF_IS_INT_TABLE_ADDR(TSEM_REG_INT_TABLE, addr)
		data = bp->tsem_int_table_data;
	else IF_IS_INT_TABLE_ADDR(CSEM_REG_INT_TABLE, addr)
		data = bp->csem_int_table_data;
	else IF_IS_INT_TABLE_ADDR(USEM_REG_INT_TABLE, addr)
		data = bp->usem_int_table_data;
	else IF_IS_INT_TABLE_ADDR(XSEM_REG_INT_TABLE, addr)
		data = bp->xsem_int_table_data;
	else IF_IS_PRAM_ADDR(TSEM_REG_PRAM, addr)
		data = bp->tsem_pram_data;
	else IF_IS_PRAM_ADDR(CSEM_REG_PRAM, addr)
		data = bp->csem_pram_data;
	else IF_IS_PRAM_ADDR(USEM_REG_PRAM, addr)
		data = bp->usem_pram_data;
	else IF_IS_PRAM_ADDR(XSEM_REG_PRAM, addr)
		data = bp->xsem_pram_data;

	return data;
}

static void bnx2x_write_big_buf_wb(struct bnx2x *bp, u32 addr, u32 len)
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
		bnx2x_init_ind_wr(bp, addr, bp->gunzip_buf, len);
}

static void bnx2x_init_wr_wb(struct bnx2x *bp, u32 addr, const u32 *data,
			     u32 len)
{
	/* This is needed for NO_ZIP mode, currently supported
	   in little endian mode only */
	data = (const u32*)bnx2x_sel_blob(bp, addr, (const u8*)data);

	if ((len * 4) > FW_BUF_SIZE) {
		BNX2X_ERR("LARGE DMAE OPERATION ! "
			  "addr 0x%x  len 0x%x\n", addr, len*4);
		return;
	}
	memcpy(bp->gunzip_buf, data, len * 4);

	bnx2x_write_big_buf_wb(bp, addr, len);
}

static void bnx2x_init_wr_zp(struct bnx2x *bp, u32 addr,
			     u32 len, u32 blob_off)
{
	int rc, i;
        const u8 *data = NULL;

	data = bnx2x_sel_blob(bp, addr, data) + 4*blob_off;

	if (data == NULL) {
		panic("Blob not found for addr 0x%x\n", addr);
		return;
	}

	rc = bnx2x_gunzip(bp, data, len);
	if (rc) {
		BNX2X_ERR("gunzip failed ! addr 0x%x rc %d\n", addr, rc);
		BNX2X_ERR("blob_offset=0x%x\n", blob_off);
		return;
	}

	/* gunzip_outlen is in dwords */
	len = bp->gunzip_outlen;
	for (i = 0; i < len; i++)
		((u32 *)bp->gunzip_buf)[i] =
			cpu_to_le32(((u32 *)bp->gunzip_buf)[i]);

	bnx2x_write_big_buf_wb(bp, addr, len);
}

static void bnx2x_init_block(struct bnx2x *bp, u32 block, u32 stage)
{
	int hw_wr, i;
	u16 op_start =
		bp->init_ops_offsets[BLOCK_OPS_IDX(block,stage,STAGE_START)];
	u16 op_end =
		bp->init_ops_offsets[BLOCK_OPS_IDX(block,stage,STAGE_END)];
	union init_op *op;
	u32 op_type, addr, len;
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

	data_base = bp->init_data;

	for (i = op_start; i < op_end; i++) {

		op = (union init_op *)&(bp->init_ops[i]);

		op_type = op->str_wr.op;
		addr = op->str_wr.offset;
		len = op->str_wr.data_len;
		data = data_base + op->str_wr.data_off;

		/* HW/EMUL specific */
		if (unlikely((op_type > OP_WB) && (op_type == hw_wr)))
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

/* PXP */
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

#endif /* BNX2X_INIT_OPS_H */
