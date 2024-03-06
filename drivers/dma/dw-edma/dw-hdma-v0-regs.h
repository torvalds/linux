/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Cai Huoqing
 * Synopsys DesignWare HDMA v0 reg
 *
 * Author: Cai Huoqing <cai.huoqing@linux.dev>
 */

#ifndef _DW_HDMA_V0_REGS_H
#define _DW_HDMA_V0_REGS_H

#include <linux/dmaengine.h>

#define HDMA_V0_MAX_NR_CH			8
#define HDMA_V0_LOCAL_ABORT_INT_EN		BIT(6)
#define HDMA_V0_REMOTE_ABORT_INT_EN		BIT(5)
#define HDMA_V0_LOCAL_STOP_INT_EN		BIT(4)
#define HDMA_V0_REMOTE_STOP_INT_EN		BIT(3)
#define HDMA_V0_ABORT_INT_MASK			BIT(2)
#define HDMA_V0_STOP_INT_MASK			BIT(0)
#define HDMA_V0_LINKLIST_EN			BIT(0)
#define HDMA_V0_CONSUMER_CYCLE_STAT		BIT(1)
#define HDMA_V0_CONSUMER_CYCLE_BIT		BIT(0)
#define HDMA_V0_DOORBELL_START			BIT(0)
#define HDMA_V0_CH_STATUS_MASK			GENMASK(1, 0)

struct dw_hdma_v0_ch_regs {
	u32 ch_en;				/* 0x0000 */
	u32 doorbell;				/* 0x0004 */
	u32 prefetch;				/* 0x0008 */
	u32 handshake;				/* 0x000c */
	union {
		u64 reg;			/* 0x0010..0x0014 */
		struct {
			u32 lsb;		/* 0x0010 */
			u32 msb;		/* 0x0014 */
		};
	} llp;
	u32 cycle_sync;				/* 0x0018 */
	u32 transfer_size;			/* 0x001c */
	union {
		u64 reg;			/* 0x0020..0x0024 */
		struct {
			u32 lsb;		/* 0x0020 */
			u32 msb;		/* 0x0024 */
		};
	} sar;
	union {
		u64 reg;			/* 0x0028..0x002c */
		struct {
			u32 lsb;		/* 0x0028 */
			u32 msb;		/* 0x002c */
		};
	} dar;
	u32 watermark_en;			/* 0x0030 */
	u32 control1;				/* 0x0034 */
	u32 func_num;				/* 0x0038 */
	u32 qos;				/* 0x003c */
	u32 padding_1[16];			/* 0x0040..0x007c */
	u32 ch_stat;				/* 0x0080 */
	u32 int_stat;				/* 0x0084 */
	u32 int_setup;				/* 0x0088 */
	u32 int_clear;				/* 0x008c */
	union {
		u64 reg;			/* 0x0090..0x0094 */
		struct {
			u32 lsb;		/* 0x0090 */
			u32 msb;		/* 0x0094 */
		};
	} msi_stop;
	union {
		u64 reg;			/* 0x0098..0x009c */
		struct {
			u32 lsb;		/* 0x0098 */
			u32 msb;		/* 0x009c */
		};
	} msi_watermark;
	union {
		u64 reg;			/* 0x00a0..0x00a4 */
		struct {
			u32 lsb;		/* 0x00a0 */
			u32 msb;		/* 0x00a4 */
		};
	} msi_abort;
	u32 msi_msgdata;			/* 0x00a8 */
	u32 padding_2[21];			/* 0x00ac..0x00fc */
} __packed;

struct dw_hdma_v0_ch {
	struct dw_hdma_v0_ch_regs wr;		/* 0x0000 */
	struct dw_hdma_v0_ch_regs rd;		/* 0x0100 */
} __packed;

struct dw_hdma_v0_regs {
	struct dw_hdma_v0_ch ch[HDMA_V0_MAX_NR_CH];	/* 0x0000..0x0fa8 */
} __packed;

struct dw_hdma_v0_lli {
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

struct dw_hdma_v0_llp {
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

#endif /* _DW_HDMA_V0_REGS_H */
