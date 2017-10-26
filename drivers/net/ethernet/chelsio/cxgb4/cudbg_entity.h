/*
 *  Copyright (C) 2017 Chelsio Communications.  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms and conditions of the GNU General Public License,
 *  version 2, as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  The full GNU General Public License is included in this distribution in
 *  the file called "COPYING".
 *
 */

#ifndef __CUDBG_ENTITY_H__
#define __CUDBG_ENTITY_H__

#define EDC0_FLAG 3
#define EDC1_FLAG 4

#define CUDBG_ENTITY_SIGNATURE 0xCCEDB001

struct card_mem {
	u16 size_edc0;
	u16 size_edc1;
	u16 mem_flag;
};

struct cudbg_mbox_log {
	struct mbox_cmd entry;
	u32 hi[MBOX_LEN / 8];
	u32 lo[MBOX_LEN / 8];
};

struct cudbg_cim_qcfg {
	u8 chip;
	u16 base[CIM_NUM_IBQ + CIM_NUM_OBQ_T5];
	u16 size[CIM_NUM_IBQ + CIM_NUM_OBQ_T5];
	u16 thres[CIM_NUM_IBQ];
	u32 obq_wr[2 * CIM_NUM_OBQ_T5];
	u32 stat[4 * (CIM_NUM_IBQ + CIM_NUM_OBQ_T5)];
};

struct cudbg_rss_vf_conf {
	u32 rss_vf_vfl;
	u32 rss_vf_vfh;
};

struct ireg_field {
	u32 ireg_addr;
	u32 ireg_data;
	u32 ireg_local_offset;
	u32 ireg_offset_range;
};

struct ireg_buf {
	struct ireg_field tp_pio;
	u32 outbuf[32];
};

struct cudbg_ulprx_la {
	u32 data[ULPRX_LA_SIZE * 8];
	u32 size;
};

struct cudbg_tp_la {
	u32 size;
	u32 mode;
	u8 data[0];
};

struct cudbg_cim_pif_la {
	int size;
	u8 data[0];
};

struct cudbg_tid_info_region {
	u32 ntids;
	u32 nstids;
	u32 stid_base;
	u32 hash_base;

	u32 natids;
	u32 nftids;
	u32 ftid_base;
	u32 aftid_base;
	u32 aftid_end;

	u32 sftid_base;
	u32 nsftids;

	u32 uotid_base;
	u32 nuotids;

	u32 sb;
	u32 flags;
	u32 le_db_conf;
	u32 ip_users;
	u32 ipv6_users;

	u32 hpftid_base;
	u32 nhpftids;
};

#define CUDBG_TID_INFO_REV 1

struct cudbg_tid_info_region_rev1 {
	struct cudbg_ver_hdr ver_hdr;
	struct cudbg_tid_info_region tid;
	u32 tid_start;
	u32 reserved[16];
};

#define CUDBG_NUM_ULPTX 11
#define CUDBG_NUM_ULPTX_READ 512

struct cudbg_ulptx_la {
	u32 rdptr[CUDBG_NUM_ULPTX];
	u32 wrptr[CUDBG_NUM_ULPTX];
	u32 rddata[CUDBG_NUM_ULPTX];
	u32 rd_data[CUDBG_NUM_ULPTX][CUDBG_NUM_ULPTX_READ];
};

#define IREG_NUM_ELEM 4

static const u32 t6_tp_pio_array[][IREG_NUM_ELEM] = {
	{0x7e40, 0x7e44, 0x020, 28}, /* t6_tp_pio_regs_20_to_3b */
	{0x7e40, 0x7e44, 0x040, 10}, /* t6_tp_pio_regs_40_to_49 */
	{0x7e40, 0x7e44, 0x050, 10}, /* t6_tp_pio_regs_50_to_59 */
	{0x7e40, 0x7e44, 0x060, 14}, /* t6_tp_pio_regs_60_to_6d */
	{0x7e40, 0x7e44, 0x06F, 1}, /* t6_tp_pio_regs_6f */
	{0x7e40, 0x7e44, 0x070, 6}, /* t6_tp_pio_regs_70_to_75 */
	{0x7e40, 0x7e44, 0x130, 18}, /* t6_tp_pio_regs_130_to_141 */
	{0x7e40, 0x7e44, 0x145, 19}, /* t6_tp_pio_regs_145_to_157 */
	{0x7e40, 0x7e44, 0x160, 1}, /* t6_tp_pio_regs_160 */
	{0x7e40, 0x7e44, 0x230, 25}, /* t6_tp_pio_regs_230_to_248 */
	{0x7e40, 0x7e44, 0x24a, 3}, /* t6_tp_pio_regs_24c */
	{0x7e40, 0x7e44, 0x8C0, 1} /* t6_tp_pio_regs_8c0 */
};

static const u32 t5_tp_pio_array[][IREG_NUM_ELEM] = {
	{0x7e40, 0x7e44, 0x020, 28}, /* t5_tp_pio_regs_20_to_3b */
	{0x7e40, 0x7e44, 0x040, 19}, /* t5_tp_pio_regs_40_to_52 */
	{0x7e40, 0x7e44, 0x054, 2}, /* t5_tp_pio_regs_54_to_55 */
	{0x7e40, 0x7e44, 0x060, 13}, /* t5_tp_pio_regs_60_to_6c */
	{0x7e40, 0x7e44, 0x06F, 1}, /* t5_tp_pio_regs_6f */
	{0x7e40, 0x7e44, 0x120, 4}, /* t5_tp_pio_regs_120_to_123 */
	{0x7e40, 0x7e44, 0x12b, 2}, /* t5_tp_pio_regs_12b_to_12c */
	{0x7e40, 0x7e44, 0x12f, 21}, /* t5_tp_pio_regs_12f_to_143 */
	{0x7e40, 0x7e44, 0x145, 19}, /* t5_tp_pio_regs_145_to_157 */
	{0x7e40, 0x7e44, 0x230, 25}, /* t5_tp_pio_regs_230_to_248 */
	{0x7e40, 0x7e44, 0x8C0, 1} /* t5_tp_pio_regs_8c0 */
};

static const u32 t6_tp_tm_pio_array[][IREG_NUM_ELEM] = {
	{0x7e18, 0x7e1c, 0x0, 12}
};

static const u32 t5_tp_tm_pio_array[][IREG_NUM_ELEM] = {
	{0x7e18, 0x7e1c, 0x0, 12}
};

static const u32 t6_tp_mib_index_array[6][IREG_NUM_ELEM] = {
	{0x7e50, 0x7e54, 0x0, 13},
	{0x7e50, 0x7e54, 0x10, 6},
	{0x7e50, 0x7e54, 0x18, 21},
	{0x7e50, 0x7e54, 0x30, 32},
	{0x7e50, 0x7e54, 0x50, 22},
	{0x7e50, 0x7e54, 0x68, 12}
};

static const u32 t5_tp_mib_index_array[9][IREG_NUM_ELEM] = {
	{0x7e50, 0x7e54, 0x0, 13},
	{0x7e50, 0x7e54, 0x10, 6},
	{0x7e50, 0x7e54, 0x18, 8},
	{0x7e50, 0x7e54, 0x20, 13},
	{0x7e50, 0x7e54, 0x30, 16},
	{0x7e50, 0x7e54, 0x40, 16},
	{0x7e50, 0x7e54, 0x50, 16},
	{0x7e50, 0x7e54, 0x60, 6},
	{0x7e50, 0x7e54, 0x68, 4}
};

static const u32 t5_sge_dbg_index_array[2][IREG_NUM_ELEM] = {
	{0x10cc, 0x10d0, 0x0, 16},
	{0x10cc, 0x10d4, 0x0, 16},
};

static const u32 t5_pcie_pdbg_array[][IREG_NUM_ELEM] = {
	{0x5a04, 0x5a0c, 0x00, 0x20}, /* t5_pcie_pdbg_regs_00_to_20 */
	{0x5a04, 0x5a0c, 0x21, 0x20}, /* t5_pcie_pdbg_regs_21_to_40 */
	{0x5a04, 0x5a0c, 0x41, 0x10}, /* t5_pcie_pdbg_regs_41_to_50 */
};

static const u32 t5_pcie_cdbg_array[][IREG_NUM_ELEM] = {
	{0x5a10, 0x5a18, 0x00, 0x20}, /* t5_pcie_cdbg_regs_00_to_20 */
	{0x5a10, 0x5a18, 0x21, 0x18}, /* t5_pcie_cdbg_regs_21_to_37 */
};

static const u32 t5_pm_rx_array[][IREG_NUM_ELEM] = {
	{0x8FD0, 0x8FD4, 0x10000, 0x20}, /* t5_pm_rx_regs_10000_to_10020 */
	{0x8FD0, 0x8FD4, 0x10021, 0x0D}, /* t5_pm_rx_regs_10021_to_1002c */
};

static const u32 t5_pm_tx_array[][IREG_NUM_ELEM] = {
	{0x8FF0, 0x8FF4, 0x10000, 0x20}, /* t5_pm_tx_regs_10000_to_10020 */
	{0x8FF0, 0x8FF4, 0x10021, 0x1D}, /* t5_pm_tx_regs_10021_to_1003c */
};

static const u32 t6_ma_ireg_array[][IREG_NUM_ELEM] = {
	{0x78f8, 0x78fc, 0xa000, 23}, /* t6_ma_regs_a000_to_a016 */
	{0x78f8, 0x78fc, 0xa400, 30}, /* t6_ma_regs_a400_to_a41e */
	{0x78f8, 0x78fc, 0xa800, 20} /* t6_ma_regs_a800_to_a813 */
};

static const u32 t6_ma_ireg_array2[][IREG_NUM_ELEM] = {
	{0x78f8, 0x78fc, 0xe400, 17}, /* t6_ma_regs_e400_to_e600 */
	{0x78f8, 0x78fc, 0xe640, 13} /* t6_ma_regs_e640_to_e7c0 */
};

static const u32 t6_up_cim_reg_array[][IREG_NUM_ELEM] = {
	{0x7b50, 0x7b54, 0x2000, 0x20}, /* up_cim_2000_to_207c */
	{0x7b50, 0x7b54, 0x2080, 0x1d}, /* up_cim_2080_to_20fc */
	{0x7b50, 0x7b54, 0x00, 0x20}, /* up_cim_00_to_7c */
	{0x7b50, 0x7b54, 0x80, 0x20}, /* up_cim_80_to_fc */
	{0x7b50, 0x7b54, 0x100, 0x11}, /* up_cim_100_to_14c */
	{0x7b50, 0x7b54, 0x200, 0x10}, /* up_cim_200_to_23c */
	{0x7b50, 0x7b54, 0x240, 0x2}, /* up_cim_240_to_244 */
	{0x7b50, 0x7b54, 0x250, 0x2}, /* up_cim_250_to_254 */
	{0x7b50, 0x7b54, 0x260, 0x2}, /* up_cim_260_to_264 */
	{0x7b50, 0x7b54, 0x270, 0x2}, /* up_cim_270_to_274 */
	{0x7b50, 0x7b54, 0x280, 0x20}, /* up_cim_280_to_2fc */
	{0x7b50, 0x7b54, 0x300, 0x20}, /* up_cim_300_to_37c */
	{0x7b50, 0x7b54, 0x380, 0x14}, /* up_cim_380_to_3cc */

};

static const u32 t5_up_cim_reg_array[][IREG_NUM_ELEM] = {
	{0x7b50, 0x7b54, 0x2000, 0x20}, /* up_cim_2000_to_207c */
	{0x7b50, 0x7b54, 0x2080, 0x19}, /* up_cim_2080_to_20ec */
	{0x7b50, 0x7b54, 0x00, 0x20}, /* up_cim_00_to_7c */
	{0x7b50, 0x7b54, 0x80, 0x20}, /* up_cim_80_to_fc */
	{0x7b50, 0x7b54, 0x100, 0x11}, /* up_cim_100_to_14c */
	{0x7b50, 0x7b54, 0x200, 0x10}, /* up_cim_200_to_23c */
	{0x7b50, 0x7b54, 0x240, 0x2}, /* up_cim_240_to_244 */
	{0x7b50, 0x7b54, 0x250, 0x2}, /* up_cim_250_to_254 */
	{0x7b50, 0x7b54, 0x260, 0x2}, /* up_cim_260_to_264 */
	{0x7b50, 0x7b54, 0x270, 0x2}, /* up_cim_270_to_274 */
	{0x7b50, 0x7b54, 0x280, 0x20}, /* up_cim_280_to_2fc */
	{0x7b50, 0x7b54, 0x300, 0x20}, /* up_cim_300_to_37c */
	{0x7b50, 0x7b54, 0x380, 0x14}, /* up_cim_380_to_3cc */
};

static const u32 t6_hma_ireg_array[][IREG_NUM_ELEM] = {
	{0x51320, 0x51324, 0xa000, 32} /* t6_hma_regs_a000_to_a01f */
};
#endif /* __CUDBG_ENTITY_H__ */
