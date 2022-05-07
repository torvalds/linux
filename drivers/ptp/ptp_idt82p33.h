/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * PTP hardware clock driver for the IDT 82P33XXX family of clocks.
 *
 * Copyright (C) 2019 Integrated Device Technology, Inc., a Renesas Company.
 */
#ifndef PTP_IDT82P33_H
#define PTP_IDT82P33_H

#include <linux/ktime.h>
#include <linux/workqueue.h>


/* Register Map - AN888_SMUforIEEE_SynchEther_82P33xxx_RevH.pdf */
#define PAGE_NUM (8)
#define _ADDR(page, offset) (((page) << 0x7) | ((offset) & 0x7f))
#define _PAGE(addr) (((addr) >> 0x7) & 0x7)
#define _OFFSET(addr)  ((addr) & 0x7f)

#define DPLL1_TOD_CNFG 0x134
#define DPLL2_TOD_CNFG 0x1B4

#define DPLL1_TOD_STS 0x10B
#define DPLL2_TOD_STS 0x18B

#define DPLL1_TOD_TRIGGER 0x115
#define DPLL2_TOD_TRIGGER 0x195

#define DPLL1_OPERATING_MODE_CNFG 0x120
#define DPLL2_OPERATING_MODE_CNFG 0x1A0

#define DPLL1_HOLDOVER_FREQ_CNFG 0x12C
#define DPLL2_HOLDOVER_FREQ_CNFG 0x1AC

#define DPLL1_PHASE_OFFSET_CNFG 0x143
#define DPLL2_PHASE_OFFSET_CNFG 0x1C3

#define DPLL1_SYNC_EDGE_CNFG 0X140
#define DPLL2_SYNC_EDGE_CNFG 0X1C0

#define DPLL1_INPUT_MODE_CNFG 0X116
#define DPLL2_INPUT_MODE_CNFG 0X196

#define OUT_MUX_CNFG(outn) _ADDR(0x6, (0xC * (outn)))

#define PAGE_ADDR 0x7F
/* Register Map end */

/* Register definitions - AN888_SMUforIEEE_SynchEther_82P33xxx_RevH.pdf*/
#define TOD_TRIGGER(wr_trig, rd_trig) ((wr_trig & 0xf) << 4 | (rd_trig & 0xf))
#define SYNC_TOD BIT(1)
#define PH_OFFSET_EN BIT(7)
#define SQUELCH_ENABLE BIT(5)

/* Bit definitions for the DPLL_MODE register */
#define PLL_MODE_SHIFT                    (0)
#define PLL_MODE_MASK                     (0x1F)

enum pll_mode {
	PLL_MODE_MIN = 0,
	PLL_MODE_AUTOMATIC = PLL_MODE_MIN,
	PLL_MODE_FORCE_FREERUN = 1,
	PLL_MODE_FORCE_HOLDOVER = 2,
	PLL_MODE_FORCE_LOCKED = 4,
	PLL_MODE_FORCE_PRE_LOCKED2 = 5,
	PLL_MODE_FORCE_PRE_LOCKED = 6,
	PLL_MODE_FORCE_LOST_PHASE = 7,
	PLL_MODE_DCO = 10,
	PLL_MODE_WPH = 18,
	PLL_MODE_MAX = PLL_MODE_WPH,
};

enum hw_tod_trig_sel {
	HW_TOD_TRIG_SEL_MIN = 0,
	HW_TOD_TRIG_SEL_NO_WRITE = HW_TOD_TRIG_SEL_MIN,
	HW_TOD_TRIG_SEL_SYNC_SEL = 1,
	HW_TOD_TRIG_SEL_IN12 = 2,
	HW_TOD_TRIG_SEL_IN13 = 3,
	HW_TOD_TRIG_SEL_IN14 = 4,
	HW_TOD_TRIG_SEL_TOD_PPS = 5,
	HW_TOD_TRIG_SEL_TIMER_INTERVAL = 6,
	HW_TOD_TRIG_SEL_MSB_PHASE_OFFSET_CNFG = 7,
	HW_TOD_TRIG_SEL_MSB_HOLDOVER_FREQ_CNFG = 8,
	HW_TOD_WR_TRIG_SEL_MSB_TOD_CNFG = 9,
	HW_TOD_RD_TRIG_SEL_LSB_TOD_STS = HW_TOD_WR_TRIG_SEL_MSB_TOD_CNFG,
	WR_TRIG_SEL_MAX = HW_TOD_WR_TRIG_SEL_MSB_TOD_CNFG,
};

/* Register bit definitions end */
#define FW_FILENAME	"idt82p33xxx.bin"
#define MAX_PHC_PLL (2)
#define TOD_BYTE_COUNT (10)
#define MAX_MEASURMENT_COUNT (5)
#define SNAP_THRESHOLD_NS (150000)
#define SYNC_TOD_TIMEOUT_SEC (5)

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

/* PTP Hardware Clock interface */
struct idt82p33_channel {
	struct ptp_clock_info	caps;
	struct ptp_clock	*ptp_clock;
	struct idt82p33	*idt82p33;
	enum pll_mode	pll_mode;
	/* task to turn off SYNC_TOD bit after pps sync */
	struct delayed_work	sync_tod_work;
	bool			sync_tod_on;
	s32			current_freq_ppb;
	u8			output_mask;
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
	struct idt82p33_channel channel[MAX_PHC_PLL];
	struct i2c_client	*client;
	u8	page_offset;
	u8	pll_mask;
	ktime_t start_time;
	int calculate_overhead_flag;
	s64 tod_write_overhead_ns;
	/* Protects I2C read/modify/write registers from concurrent access */
	struct mutex	reg_lock;
};

/* firmware interface */
struct idt82p33_fwrc {
	u8 hiaddr;
	u8 loaddr;
	u8 value;
	u8 reserved;
} __packed;

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


#endif /* PTP_IDT82P33_H */
