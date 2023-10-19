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
	u32 ch_control1;				/* 0x0000 */
	u32 ch_control2;				/* 0x0004 */
	u32 transfer_size;				/* 0x0008 */
	union {
		u64 reg;				/* 0x000c..0x0010 */
		struct {
			u32 lsb;			/* 0x000c */
			u32 msb;			/* 0x0010 */
		};
	} sar;
	union {
		u64 reg;				/* 0x0014..0x0018 */
		struct {
			u32 lsb;			/* 0x0014 */
			u32 msb;			/* 0x0018 */
		};
	} dar;
	union {
		u64 reg;				/* 0x001c..0x0020 */
		struct {
			u32 lsb;			/* 0x001c */
			u32 msb;			/* 0x0020 */
		};
	} llp;
} __packed;

struct dw_edma_v0_ch {
	struct dw_edma_v0_ch_regs wr;			/* 0x0200 */
	u32 padding_1[55];				/* 0x0224..0x02fc */
	struct dw_edma_v0_ch_regs rd;			/* 0x0300 */
	u32 padding_2[55];				/* 0x0324..0x03fc */
} __packed;

struct dw_edma_v0_unroll {
	u32 padding_1;					/* 0x00f8 */
	u32 wr_engine_chgroup;				/* 0x0100 */
	u32 rd_engine_chgroup;				/* 0x0104 */
	union {
		u64 reg;				/* 0x0108..0x010c */
		struct {
			u32 lsb;			/* 0x0108 */
			u32 msb;			/* 0x010c */
		};
	} wr_engine_hshake_cnt;
	u32 padding_2[2];				/* 0x0110..0x0114 */
	union {
		u64 reg;				/* 0x0120..0x0124 */
		struct {
			u32 lsb;			/* 0x0120 */
			u32 msb;			/* 0x0124 */
		};
	} rd_engine_hshake_cnt;
	u32 padding_3[2];				/* 0x0120..0x0124 */
	u32 wr_ch0_pwr_en;				/* 0x0128 */
	u32 wr_ch1_pwr_en;				/* 0x012c */
	u32 wr_ch2_pwr_en;				/* 0x0130 */
	u32 wr_ch3_pwr_en;				/* 0x0134 */
	u32 wr_ch4_pwr_en;				/* 0x0138 */
	u32 wr_ch5_pwr_en;				/* 0x013c */
	u32 wr_ch6_pwr_en;				/* 0x0140 */
	u32 wr_ch7_pwr_en;				/* 0x0144 */
	u32 padding_4[8];				/* 0x0148..0x0164 */
	u32 rd_ch0_pwr_en;				/* 0x0168 */
	u32 rd_ch1_pwr_en;				/* 0x016c */
	u32 rd_ch2_pwr_en;				/* 0x0170 */
	u32 rd_ch3_pwr_en;				/* 0x0174 */
	u32 rd_ch4_pwr_en;				/* 0x0178 */
	u32 rd_ch5_pwr_en;				/* 0x018c */
	u32 rd_ch6_pwr_en;				/* 0x0180 */
	u32 rd_ch7_pwr_en;				/* 0x0184 */
	u32 padding_5[30];				/* 0x0188..0x01fc */
	struct dw_edma_v0_ch ch[EDMA_V0_MAX_NR_CH];	/* 0x0200..0x1120 */
} __packed;

struct dw_edma_v0_legacy {
	u32 viewport_sel;				/* 0x00f8 */
	struct dw_edma_v0_ch_regs ch;			/* 0x0100..0x0120 */
} __packed;

struct dw_edma_v0_regs {
	/* eDMA global registers */
	u32 ctrl_data_arb_prior;			/* 0x0000 */
	u32 padding_1;					/* 0x0004 */
	u32 ctrl;					/* 0x0008 */
	u32 wr_engine_en;				/* 0x000c */
	u32 wr_doorbell;				/* 0x0010 */
	u32 padding_2;					/* 0x0014 */
	union {
		u64 reg;				/* 0x0018..0x001c */
		struct {
			u32 lsb;			/* 0x0018 */
			u32 msb;			/* 0x001c */
		};
	} wr_ch_arb_weight;
	u32 padding_3[3];				/* 0x0020..0x0028 */
	u32 rd_engine_en;				/* 0x002c */
	u32 rd_doorbell;				/* 0x0030 */
	u32 padding_4;					/* 0x0034 */
	union {
		u64 reg;				/* 0x0038..0x003c */
		struct {
			u32 lsb;			/* 0x0038 */
			u32 msb;			/* 0x003c */
		};
	} rd_ch_arb_weight;
	u32 padding_5[3];				/* 0x0040..0x0048 */
	/* eDMA interrupts registers */
	u32 wr_int_status;				/* 0x004c */
	u32 padding_6;					/* 0x0050 */
	u32 wr_int_mask;				/* 0x0054 */
	u32 wr_int_clear;				/* 0x0058 */
	u32 wr_err_status;				/* 0x005c */
	union {
		u64 reg;				/* 0x0060..0x0064 */
		struct {
			u32 lsb;			/* 0x0060 */
			u32 msb;			/* 0x0064 */
		};
	} wr_done_imwr;
	union {
		u64 reg;				/* 0x0068..0x006c */
		struct {
			u32 lsb;			/* 0x0068 */
			u32 msb;			/* 0x006c */
		};
	} wr_abort_imwr;
	u32 wr_ch01_imwr_data;				/* 0x0070 */
	u32 wr_ch23_imwr_data;				/* 0x0074 */
	u32 wr_ch45_imwr_data;				/* 0x0078 */
	u32 wr_ch67_imwr_data;				/* 0x007c */
	u32 padding_7[4];				/* 0x0080..0x008c */
	u32 wr_linked_list_err_en;			/* 0x0090 */
	u32 padding_8[3];				/* 0x0094..0x009c */
	u32 rd_int_status;				/* 0x00a0 */
	u32 padding_9;					/* 0x00a4 */
	u32 rd_int_mask;				/* 0x00a8 */
	u32 rd_int_clear;				/* 0x00ac */
	u32 padding_10;					/* 0x00b0 */
	union {
		u64 reg;				/* 0x00b4..0x00b8 */
		struct {
			u32 lsb;			/* 0x00b4 */
			u32 msb;			/* 0x00b8 */
		};
	} rd_err_status;
	u32 padding_11[2];				/* 0x00bc..0x00c0 */
	u32 rd_linked_list_err_en;			/* 0x00c4 */
	u32 padding_12;					/* 0x00c8 */
	union {
		u64 reg;				/* 0x00cc..0x00d0 */
		struct {
			u32 lsb;			/* 0x00cc */
			u32 msb;			/* 0x00d0 */
		};
	} rd_done_imwr;
	union {
		u64 reg;				/* 0x00d4..0x00d8 */
		struct {
			u32 lsb;			/* 0x00d4 */
			u32 msb;			/* 0x00d8 */
		};
	} rd_abort_imwr;
	u32 rd_ch01_imwr_data;				/* 0x00dc */
	u32 rd_ch23_imwr_data;				/* 0x00e0 */
	u32 rd_ch45_imwr_data;				/* 0x00e4 */
	u32 rd_ch67_imwr_data;				/* 0x00e8 */
	u32 padding_13[4];				/* 0x00ec..0x00f8 */
	/* eDMA channel context grouping */
	union dw_edma_v0_type {
		struct dw_edma_v0_legacy legacy;	/* 0x00f8..0x0120 */
		struct dw_edma_v0_unroll unroll;	/* 0x00f8..0x1120 */
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
