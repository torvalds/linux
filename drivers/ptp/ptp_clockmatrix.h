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
#define MAX_TOD		(4)
#define MAX_PLL		(8)

#define MAX_ABS_WRITE_PHASE_PICOSECONDS (107374182350LL)

#define TOD_MASK_ADDR		(0xFFA5)
#define DEFAULT_TOD_MASK	(0x04)

#define SET_U16_LSB(orig, val8) (orig = (0xff00 & (orig)) | (val8))
#define SET_U16_MSB(orig, val8) (orig = (0x00ff & (orig)) | (val8 << 8))

#define TOD0_PTP_PLL_ADDR		(0xFFA8)
#define TOD1_PTP_PLL_ADDR		(0xFFA9)
#define TOD2_PTP_PLL_ADDR		(0xFFAA)
#define TOD3_PTP_PLL_ADDR		(0xFFAB)

#define TOD0_OUT_ALIGN_MASK_ADDR	(0xFFB0)
#define TOD1_OUT_ALIGN_MASK_ADDR	(0xFFB2)
#define TOD2_OUT_ALIGN_MASK_ADDR	(0xFFB4)
#define TOD3_OUT_ALIGN_MASK_ADDR	(0xFFB6)

#define DEFAULT_OUTPUT_MASK_PLL0	(0x003)
#define DEFAULT_OUTPUT_MASK_PLL1	(0x00c)
#define DEFAULT_OUTPUT_MASK_PLL2	(0x030)
#define DEFAULT_OUTPUT_MASK_PLL3	(0x0c0)

#define DEFAULT_TOD0_PTP_PLL		(0)
#define DEFAULT_TOD1_PTP_PLL		(1)
#define DEFAULT_TOD2_PTP_PLL		(2)
#define DEFAULT_TOD3_PTP_PLL		(3)

#define POST_SM_RESET_DELAY_MS			(3000)
#define PHASE_PULL_IN_THRESHOLD_NS_DEPRECATED	(150000)
#define PHASE_PULL_IN_THRESHOLD_NS		(15000)
#define TOD_WRITE_OVERHEAD_COUNT_MAX		(2)
#define TOD_BYTE_COUNT				(11)

#define LOCK_TIMEOUT_MS			(2000)
#define LOCK_POLL_INTERVAL_MS		(10)

#define PEROUT_ENABLE_OUTPUT_MASK	(0xdeadbeef)

#define IDTCM_MAX_WRITE_COUNT		(512)

#define PHASE_PULL_IN_MAX_PPB		(144000)
#define PHASE_PULL_IN_MIN_THRESHOLD_NS	(2)

/*
 * Return register address based on passed in firmware version
 */
#define IDTCM_FW_REG(FW, VER, REG)	(((FW) < (VER)) ? (REG) : (REG##_##VER))

/* PTP PLL Mode */
enum ptp_pll_mode {
	PTP_PLL_MODE_MIN = 0,
	PTP_PLL_MODE_WRITE_FREQUENCY = PTP_PLL_MODE_MIN,
	PTP_PLL_MODE_WRITE_PHASE,
	PTP_PLL_MODE_UNSUPPORTED,
	PTP_PLL_MODE_MAX = PTP_PLL_MODE_UNSUPPORTED,
};

/* Values of DPLL_N.DPLL_MODE.PLL_MODE */
enum pll_mode {
	PLL_MODE_MIN = 0,
	PLL_MODE_PLL = PLL_MODE_MIN,
	PLL_MODE_WRITE_PHASE = 1,
	PLL_MODE_WRITE_FREQUENCY = 2,
	PLL_MODE_GPIO_INC_DEC = 3,
	PLL_MODE_SYNTHESIS = 4,
	PLL_MODE_PHASE_MEASUREMENT = 5,
	PLL_MODE_DISABLED = 6,
	PLL_MODE_MAX = PLL_MODE_DISABLED,
};

/* Values of DPLL_CTRL_n.DPLL_MANU_REF_CFG.MANUAL_REFERENCE */
enum manual_reference {
	MANU_REF_MIN = 0,
	MANU_REF_CLK0 = MANU_REF_MIN,
	MANU_REF_CLK1,
	MANU_REF_CLK2,
	MANU_REF_CLK3,
	MANU_REF_CLK4,
	MANU_REF_CLK5,
	MANU_REF_CLK6,
	MANU_REF_CLK7,
	MANU_REF_CLK8,
	MANU_REF_CLK9,
	MANU_REF_CLK10,
	MANU_REF_CLK11,
	MANU_REF_CLK12,
	MANU_REF_CLK13,
	MANU_REF_CLK14,
	MANU_REF_CLK15,
	MANU_REF_WRITE_PHASE,
	MANU_REF_WRITE_FREQUENCY,
	MANU_REF_XO_DPLL,
	MANU_REF_MAX = MANU_REF_XO_DPLL,
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

/* 4.8.7 only */
enum scsr_tod_write_trig_sel {
	SCSR_TOD_WR_TRIG_SEL_DISABLE = 0,
	SCSR_TOD_WR_TRIG_SEL_IMMEDIATE = 1,
	SCSR_TOD_WR_TRIG_SEL_REFCLK = 2,
	SCSR_TOD_WR_TRIG_SEL_PWMPPS = 3,
	SCSR_TOD_WR_TRIG_SEL_TODPPS = 4,
	SCSR_TOD_WR_TRIG_SEL_SYNCFOD = 5,
	SCSR_TOD_WR_TRIG_SEL_GPIO = 6,
	SCSR_TOD_WR_TRIG_SEL_MAX = SCSR_TOD_WR_TRIG_SEL_GPIO,
};

/* 4.8.7 only */
enum scsr_tod_write_type_sel {
	SCSR_TOD_WR_TYPE_SEL_ABSOLUTE = 0,
	SCSR_TOD_WR_TYPE_SEL_DELTA_PLUS = 1,
	SCSR_TOD_WR_TYPE_SEL_DELTA_MINUS = 2,
	SCSR_TOD_WR_TYPE_SEL_MAX = SCSR_TOD_WR_TYPE_SEL_DELTA_MINUS,
};

/* Values STATUS.DPLL_SYS_STATUS.DPLL_SYS_STATE */
enum dpll_state {
	DPLL_STATE_MIN = 0,
	DPLL_STATE_FREERUN = DPLL_STATE_MIN,
	DPLL_STATE_LOCKACQ = 1,
	DPLL_STATE_LOCKREC = 2,
	DPLL_STATE_LOCKED = 3,
	DPLL_STATE_HOLDOVER = 4,
	DPLL_STATE_OPEN_LOOP = 5,
	DPLL_STATE_MAX = DPLL_STATE_OPEN_LOOP,
};

enum fw_version {
	V_DEFAULT = 0,
	V487 = 1,
	V520 = 2,
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
	u8			sync_src;
	enum ptp_pll_mode	mode;
	int			(*configure_write_frequency)(struct idtcm_channel *channel);
	int			(*configure_write_phase)(struct idtcm_channel *channel);
	int			(*do_phase_pull_in)(struct idtcm_channel *channel,
						    s32 offset_ns, u32 max_ffo_ppb);
	s32			current_freq_scaled_ppm;
	bool			phase_pull_in;
	u8			pll;
	u16			output_mask;
};

struct idtcm {
	struct idtcm_channel	channel[MAX_TOD];
	struct i2c_client	*client;
	u8			page_offset;
	u8			tod_mask;
	char			version[16];
	enum fw_version		fw_ver;

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
