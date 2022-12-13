/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * PTP hardware clock driver for the IDT 82P33XXX family of clocks.
 *
 * Copyright (C) 2019 Integrated Device Technology, Inc., a Renesas Company.
 */
#ifndef PTP_IDT82P33_H
#define PTP_IDT82P33_H

#include <linux/ktime.h>
#include <linux/mfd/idt82p33_reg.h>
#include <linux/regmap.h>

#define FW_FILENAME	"idt82p33xxx.bin"
#define MAX_PHC_PLL	(2)
#define MAX_TRIG_CLK	(3)
#define MAX_PER_OUT	(11)
#define TOD_BYTE_COUNT	(10)
#define DCO_MAX_PPB     (92000)
#define MAX_MEASURMENT_COUNT	(5)
#define SNAP_THRESHOLD_NS	(10000)
#define IMMEDIATE_SNAP_THRESHOLD_NS (50000)
#define DDCO_THRESHOLD_NS	(5)
#define IDT82P33_MAX_WRITE_COUNT	(512)

#define PLLMASK_ADDR_HI	0xFF
#define PLLMASK_ADDR_LO	0xA5

#define PLL0_OUTMASK_ADDR_HI	0xFF
#define PLL0_OUTMASK_ADDR_LO	0xB0

#define PLL1_OUTMASK_ADDR_HI	0xFF
#define PLL1_OUTMASK_ADDR_LO	0xB2

#define PLL2_OUTMASK_ADDR_HI	0xFF
#define PLL2_OUTMASK_ADDR_LO	0xB4

#define PLL3_OUTMASK_ADDR_HI	0xFF
#define PLL3_OUTMASK_ADDR_LO	0xB6

#define DEFAULT_PLL_MASK	(0x01)
#define DEFAULT_OUTPUT_MASK_PLL0	(0xc0)
#define DEFAULT_OUTPUT_MASK_PLL1	DEFAULT_OUTPUT_MASK_PLL0

/**
 * @brief Maximum absolute value for write phase offset in femtoseconds
 */
#define WRITE_PHASE_OFFSET_LIMIT (20000052084ll)

/** @brief Phase offset resolution
 *
 *  DPLL phase offset = 10^15 fs / ( System Clock  * 2^13)
 *                    = 10^15 fs / ( 1638400000 * 2^23)
 *                    = 74.5058059692382 fs
 */
#define IDT_T0DPLL_PHASE_RESOL 74506

/* PTP Hardware Clock interface */
struct idt82p33_channel {
	struct ptp_clock_info	caps;
	struct ptp_clock	*ptp_clock;
	struct idt82p33		*idt82p33;
	enum pll_mode		pll_mode;
	/* Workaround for TOD-to-output alignment issue */
	struct delayed_work	adjtime_work;
	s32			current_freq;
	/* double dco mode */
	bool			ddco;
	u8			output_mask;
	/* last input trigger for extts */
	u8			tod_trigger;
	bool			discard_next_extts;
	u8			plln;
	/* remember last tod_sts for extts */
	u8			extts_tod_sts[TOD_BYTE_COUNT];
	u16			dpll_tod_cnfg;
	u16			dpll_tod_trigger;
	u16			dpll_tod_sts;
	u16			dpll_mode_cnfg;
	u16			dpll_freq_cnfg;
	u16			dpll_phase_cnfg;
	u16			dpll_sync_cnfg;
	u16			dpll_input_mode_cnfg;
};

struct idt82p33 {
	struct idt82p33_channel	channel[MAX_PHC_PLL];
	struct device		*dev;
	u8			pll_mask;
	/* Polls for external time stamps */
	u8			extts_mask;
	bool			extts_single_shot;
	struct delayed_work	extts_work;
	/* Remember the ptp channel to report extts */
	struct idt82p33_channel	*event_channel[MAX_PHC_PLL];
	/* Mutex to protect operations from being interrupted */
	struct mutex		*lock;
	struct regmap		*regmap;
	struct device		*mfd;
	/* Overhead calculation for adjtime */
	ktime_t			start_time;
	int			calculate_overhead_flag;
	s64			tod_write_overhead_ns;
};

/* firmware interface */
struct idt82p33_fwrc {
	u8 hiaddr;
	u8 loaddr;
	u8 value;
	u8 reserved;
} __packed;

#endif /* PTP_IDT82P33_H */
