/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * PTP 1588 clock using the IXP46X
 *
 * Copyright (C) 2010 OMICRON electronics GmbH
 */

#ifndef _IXP46X_TS_H_
#define _IXP46X_TS_H_

#define DEFAULT_ADDEND 0xF0000029
#define TICKS_NS_SHIFT 4

struct ixp46x_channel_ctl {
	u32 ch_control;  /* 0x40 Time Synchronization Channel Control */
	u32 ch_event;    /* 0x44 Time Synchronization Channel Event */
	u32 tx_snap_lo;  /* 0x48 Transmit Snapshot Low Register */
	u32 tx_snap_hi;  /* 0x4C Transmit Snapshot High Register */
	u32 rx_snap_lo;  /* 0x50 Receive Snapshot Low Register */
	u32 rx_snap_hi;  /* 0x54 Receive Snapshot High Register */
	u32 src_uuid_lo; /* 0x58 Source UUID0 Low Register */
	u32 src_uuid_hi; /* 0x5C Sequence Identifier/Source UUID0 High */
};

struct ixp46x_ts_regs {
	u32 control;     /* 0x00 Time Sync Control Register */
	u32 event;       /* 0x04 Time Sync Event Register */
	u32 addend;      /* 0x08 Time Sync Addend Register */
	u32 accum;       /* 0x0C Time Sync Accumulator Register */
	u32 test;        /* 0x10 Time Sync Test Register */
	u32 unused;      /* 0x14 */
	u32 rsystime_lo; /* 0x18 RawSystemTime_Low Register */
	u32 rsystime_hi; /* 0x1C RawSystemTime_High Register */
	u32 systime_lo;  /* 0x20 SystemTime_Low Register */
	u32 systime_hi;  /* 0x24 SystemTime_High Register */
	u32 trgt_lo;     /* 0x28 TargetTime_Low Register */
	u32 trgt_hi;     /* 0x2C TargetTime_High Register */
	u32 asms_lo;     /* 0x30 Auxiliary Slave Mode Snapshot Low  */
	u32 asms_hi;     /* 0x34 Auxiliary Slave Mode Snapshot High */
	u32 amms_lo;     /* 0x38 Auxiliary Master Mode Snapshot Low */
	u32 amms_hi;     /* 0x3C Auxiliary Master Mode Snapshot High */

	struct ixp46x_channel_ctl channel[3];
};

/* 0x00 Time Sync Control Register Bits */
#define TSCR_AMM (1<<3)
#define TSCR_ASM (1<<2)
#define TSCR_TTM (1<<1)
#define TSCR_RST (1<<0)

/* 0x04 Time Sync Event Register Bits */
#define TSER_SNM (1<<3)
#define TSER_SNS (1<<2)
#define TTIPEND  (1<<1)

/* 0x40 Time Synchronization Channel Control Register Bits */
#define MASTER_MODE   (1<<0)
#define TIMESTAMP_ALL (1<<1)

/* 0x44 Time Synchronization Channel Event Register Bits */
#define TX_SNAPSHOT_LOCKED (1<<0)
#define RX_SNAPSHOT_LOCKED (1<<1)

#if IS_ENABLED(CONFIG_PTP_1588_CLOCK_IXP46X)
int ixp46x_ptp_find(struct ixp46x_ts_regs *__iomem *regs, int *phc_index);
#else
static inline int ixp46x_ptp_find(struct ixp46x_ts_regs *__iomem *regs, int *phc_index)
{
	*regs = NULL;
	*phc_index = -1;

	return -ENODEV;
}
#endif

#endif
