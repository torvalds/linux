/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018-2019 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare eDMA v0 core
 *
 * Author: Gustavo Pimentel <gustavo.pimentel@synopsys.com>
 */

#ifndef _DW_EDMA_V0_REGS_H
#define _DW_EDMA_V0_REGS_H

#include <linux/dmaengine.h>

#define EDMA_V0_MAX_NR_CH				8
#define EDMA_V0_VIEWPORT_MASK				GENMASK(2, 0)
#define EDMA_V0_DONE_INT_MASK				GENMASK(7, 0)
#define EDMA_V0_ABORT_INT_MASK				GENMASK(23, 16)
#define EDMA_V0_WRITE_CH_COUNT_MASK			GENMASK(3, 0)
#define EDMA_V0_READ_CH_COUNT_MASK			GENMASK(19, 16)
#define EDMA_V0_CH_STATUS_MASK				GENMASK(6, 5)
#define EDMA_V0_DOORBELL_CH_MASK			GENMASK(2, 0)
#define EDMA_V0_LINKED_LIST_ERR_MASK			GENMASK(7, 0)

#define EDMA_V0_CH_ODD_MSI_DATA_MASK			GENMASK(31, 16)
#define EDMA_V0_CH_EVEN_MSI_DATA_MASK			GENMASK(15, 0)

struct dw_edma_v0_ch_regs {
	u32 ch_control1;				/* 0x000 */
	u32 ch_control2;				/* 0x004 */
	u32 transfer_size;				/* 0x008 */
	union {
		u64 reg;				/* 0x00c..0x010 */
		struct {
			u32 lsb;			/* 0x00c */
			u32 msb;			/* 0x010 */
		};
	} sar;
	union {
		u64 reg;				/* 0x014..0x018 */
		struct {
			u32 lsb;			/* 0x014 */
			u32 msb;			/* 0x018 */
		};
	} dar;
	union {
		u64 reg;				/* 0x01c..0x020 */
		struct {
			u32 lsb;			/* 0x01c */
			u32 msb;			/* 0x020 */
		};
	} llp;
} __packed;

struct dw_edma_v0_ch {
	struct dw_edma_v0_ch_regs wr;			/* 0x200 */
	u32 padding_1[55];				/* [0x224..0x2fc] */
	struct dw_edma_v0_ch_regs rd;			/* 0x300 */
	u32 padding_2[55];				/* [0x324..0x3fc] */
} __packed;

struct dw_edma_v0_unroll {
	u32 padding_1;					/* 0x0f8 */
	u32 wr_engine_chgroup;				/* 0x100 */
	u32 rd_engine_chgroup;				/* 0x104 */
	union {
		u64 reg;				/* 0x108..0x10c */
		struct {
			u32 lsb;			/* 0x108 */
			u32 msb;			/* 0x10c */
		};
	} wr_engine_hshake_cnt;
	u32 padding_2[2];				/* [0x110..0x114] */
	union {
		u64 reg;				/* 0x120..0x124 */
		struct {
			u32 lsb;			/* 0x120 */
			u32 msb;			/* 0x124 */
		};
	} rd_engine_hshake_cnt;
	u32 padding_3[2];				/* [0x120..0x124] */
	u32 wr_ch0_pwr_en;				/* 0x128 */
	u32 wr_ch1_pwr_en;				/* 0x12c */
	u32 wr_ch2_pwr_en;				/* 0x130 */
	u32 wr_ch3_pwr_en;				/* 0x134 */
	u32 wr_ch4_pwr_en;				/* 0x138 */
	u32 wr_ch5_pwr_en;				/* 0x13c */
	u32 wr_ch6_pwr_en;				/* 0x140 */
	u32 wr_ch7_pwr_en;				/* 0x144 */
	u32 padding_4[8];				/* [0x148..0x164] */
	u32 rd_ch0_pwr_en;				/* 0x168 */
	u32 rd_ch1_pwr_en;				/* 0x16c */
	u32 rd_ch2_pwr_en;				/* 0x170 */
	u32 rd_ch3_pwr_en;				/* 0x174 */
	u32 rd_ch4_pwr_en;				/* 0x178 */
	u32 rd_ch5_pwr_en;				/* 0x18c */
	u32 rd_ch6_pwr_en;				/* 0x180 */
	u32 rd_ch7_pwr_en;				/* 0x184 */
	u32 padding_5[30];				/* [0x188..0x1fc] */
	struct dw_edma_v0_ch ch[EDMA_V0_MAX_NR_CH];	/* [0x200..0x1120] */
} __packed;

struct dw_edma_v0_legacy {
	u32 viewport_sel;				/* 0x0f8 */
	struct dw_edma_v0_ch_regs ch;			/* [0x100..0x120] */
} __packed;

struct dw_edma_v0_regs {
	/* eDMA global registers */
	u32 ctrl_data_arb_prior;			/* 0x000 */
	u32 padding_1;					/* 0x004 */
	u32 ctrl;					/* 0x008 */
	u32 wr_engine_en;				/* 0x00c */
	u32 wr_doorbell;				/* 0x010 */
	u32 padding_2;					/* 0x014 */
	union {
		u64 reg;				/* 0x018..0x01c */
		struct {
			u32 lsb;			/* 0x018 */
			u32 msb;			/* 0x01c */
		};
	} wr_ch_arb_weight;
	u32 padding_3[3];				/* [0x020..0x028] */
	u32 rd_engine_en;				/* 0x02c */
	u32 rd_doorbell;				/* 0x030 */
	u32 padding_4;					/* 0x034 */
	union {
		u64 reg;				/* 0x038..0x03c */
		struct {
			u32 lsb;			/* 0x038 */
			u32 msb;			/* 0x03c */
		};
	} rd_ch_arb_weight;
	u32 padding_5[3];				/* [0x040..0x048] */
	/* eDMA interrupts registers */
	u32 wr_int_status;				/* 0x04c */
	u32 padding_6;					/* 0x050 */
	u32 wr_int_mask;				/* 0x054 */
	u32 wr_int_clear;				/* 0x058 */
	u32 wr_err_status;				/* 0x05c */
	union {
		u64 reg;				/* 0x060..0x064 */
		struct {
			u32 lsb;			/* 0x060 */
			u32 msb;			/* 0x064 */
		};
	} wr_done_imwr;
	union {
		u64 reg;				/* 0x068..0x06c */
		struct {
			u32 lsb;			/* 0x068 */
			u32 msb;			/* 0x06c */
		};
	} wr_abort_imwr;
	u32 wr_ch01_imwr_data;				/* 0x070 */
	u32 wr_ch23_imwr_data;				/* 0x074 */
	u32 wr_ch45_imwr_data;				/* 0x078 */
	u32 wr_ch67_imwr_data;				/* 0x07c */
	u32 padding_7[4];				/* [0x080..0x08c] */
	u32 wr_linked_list_err_en;			/* 0x090 */
	u32 padding_8[3];				/* [0x094..0x09c] */
	u32 rd_int_status;				/* 0x0a0 */
	u32 padding_9;					/* 0x0a4 */
	u32 rd_int_mask;				/* 0x0a8 */
	u32 rd_int_clear;				/* 0x0ac */
	u32 padding_10;					/* 0x0b0 */
	union {
		u64 reg;				/* 0x0b4..0x0b8 */
		struct {
			u32 lsb;			/* 0x0b4 */
			u32 msb;			/* 0x0b8 */
		};
	} rd_err_status;
	u32 padding_11[2];				/* [0x0bc..0x0c0] */
	u32 rd_linked_list_err_en;			/* 0x0c4 */
	u32 padding_12;					/* 0x0c8 */
	union {
		u64 reg;				/* 0x0cc..0x0d0 */
		struct {
			u32 lsb;			/* 0x0cc */
			u32 msb;			/* 0x0d0 */
		};
	} rd_done_imwr;
	union {
		u64 reg;				/* 0x0d4..0x0d8 */
		struct {
			u32 lsb;			/* 0x0d4 */
			u32 msb;			/* 0x0d8 */
		};
	} rd_abort_imwr;
	u32 rd_ch01_imwr_data;				/* 0x0dc */
	u32 rd_ch23_imwr_data;				/* 0x0e0 */
	u32 rd_ch45_imwr_data;				/* 0x0e4 */
	u32 rd_ch67_imwr_data;				/* 0x0e8 */
	u32 padding_13[4];				/* [0x0ec..0x0f8] */
	/* eDMA channel context grouping */
	union dw_edma_v0_type {
		struct dw_edma_v0_legacy legacy;	/* [0x0f8..0x120] */
		struct dw_edma_v0_unroll unroll;	/* [0x0f8..0x1120] */
	} type;
} __packed;

struct dw_edma_v0_lli {
	u32 control;
	u32 transfer_size;
	union {
		u64 reg;
		struct {
			u32 lsb;
			u32 msb;
		};
	} sar;
	union {
		u64 reg;
		struct {
			u32 lsb;
			u32 msb;
		};
	} dar;
} __packed;

struct dw_edma_v0_llp {
	u32 control;
	u32 reserved;
	union {
		u64 reg;
		struct {
			u32 lsb;
			u32 msb;
		};
	} llp;
} __packed;

#endif /* _DW_EDMA_V0_REGS_H */
