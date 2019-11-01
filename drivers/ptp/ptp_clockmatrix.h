/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * PTP hardware clock driver for the IDT ClockMatrix(TM) family of timing and
 * synchronization devices.
 *
 * Copyright (C) 2019 Integrated Device Technology, Inc., a Renesas Company.
 */
#ifndef PTP_IDTCLOCKMATRIX_H
#define PTP_IDTCLOCKMATRIX_H

#include <linux/ktime.h>

#include "idt8a340_reg.h"

#define FW_FILENAME	"idtcm.bin"
#define MAX_PHC_PLL	4

#define PLL_MASK_ADDR		(0xFFA5)
#define DEFAULT_PLL_MASK	(0x04)

#define SET_U16_LSB(orig, val8) (orig = (0xff00 & (orig)) | (val8))
#define SET_U16_MSB(orig, val8) (orig = (0x00ff & (orig)) | (val8 << 8))

#define OUTPUT_MASK_PLL0_ADDR		(0xFFB0)
#define OUTPUT_MASK_PLL1_ADDR		(0xFFB2)
#define OUTPUT_MASK_PLL2_ADDR		(0xFFB4)
#define OUTPUT_MASK_PLL3_ADDR		(0xFFB6)

#define DEFAULT_OUTPUT_MASK_PLL0	(0x003)
#define DEFAULT_OUTPUT_MASK_PLL1	(0x00c)
#define DEFAULT_OUTPUT_MASK_PLL2	(0x030)
#define DEFAULT_OUTPUT_MASK_PLL3	(0x0c0)

#define POST_SM_RESET_DELAY_MS		(3000)
#define PHASE_PULL_IN_THRESHOLD_NS	(150000)
#define TOD_WRITE_OVERHEAD_COUNT_MAX    (5)
#define TOD_BYTE_COUNT                  (11)

/* Values of DPLL_N.DPLL_MODE.PLL_MODE */
enum pll_mode {
	PLL_MODE_MIN = 0,
	PLL_MODE_NORMAL = PLL_MODE_MIN,
	PLL_MODE_WRITE_PHASE = 1,
	PLL_MODE_WRITE_FREQUENCY = 2,
	PLL_MODE_GPIO_INC_DEC = 3,
	PLL_MODE_SYNTHESIS = 4,
	PLL_MODE_PHASE_MEASUREMENT = 5,
	PLL_MODE_MAX = PLL_MODE_PHASE_MEASUREMENT,
};

enum hw_tod_write_trig_sel {
	HW_TOD_WR_TRIG_SEL_MIN = 0,
	HW_TOD_WR_TRIG_SEL_MSB = HW_TOD_WR_TRIG_SEL_MIN,
	HW_TOD_WR_TRIG_SEL_RESERVED = 1,
	HW_TOD_WR_TRIG_SEL_TOD_PPS = 2,
	HW_TOD_WR_TRIG_SEL_IRIGB_PPS = 3,
	HW_TOD_WR_TRIG_SEL_PWM_PPS = 4,
	HW_TOD_WR_TRIG_SEL_GPIO = 5,
	HW_TOD_WR_TRIG_SEL_FOD_SYNC = 6,
	WR_TRIG_SEL_MAX = HW_TOD_WR_TRIG_SEL_FOD_SYNC,
};

struct idtcm;

struct idtcm_channel {
	struct ptp_clock_info	caps;
	struct ptp_clock	*ptp_clock;
	struct idtcm		*idtcm;
	u16			dpll_phase;
	u16			dpll_freq;
	u16			dpll_n;
	u16			dpll_ctrl_n;
	u16			dpll_phase_pull_in;
	u16			tod_read_primary;
	u16			tod_write;
	u16			tod_n;
	u16			hw_dpll_n;
	enum pll_mode		pll_mode;
	u16			output_mask;
};

struct idtcm {
	struct idtcm_channel	channel[MAX_PHC_PLL];
	struct i2c_client	*client;
	u8			page_offset;
	u8			pll_mask;

	/* Overhead calculation for adjtime */
	u8			calculate_overhead_flag;
	s64			tod_write_overhead_ns;
	ktime_t			start_time;

	/* Protects I2C read/modify/write registers from concurrent access */
	struct mutex		reg_lock;
};

struct idtcm_fwrc {
	u8 hiaddr;
	u8 loaddr;
	u8 value;
	u8 reserved;
} __packed;

#endif /* PTP_IDTCLOCKMATRIX_H */
