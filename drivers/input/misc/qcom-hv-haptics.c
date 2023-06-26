// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/atomic.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/qpnp/qpnp-pbs.h>

#include <linux/soc/qcom/battery_charger.h>

#define CREATE_TRACE_POINTS
#include <trace/events/qcom_haptics.h>

/* status register definitions in HAPTICS_CFG module */
#define HAP_CFG_REVISION2_REG			0x01
#define HAP_CFG_V1				0x1
#define HAP_CFG_V2				0x2
#define HAP_CFG_V3				0x3
#define HAP_CFG_V4				0x4

#define HAP_CFG_STATUS_DATA_MSB_REG		0x09
/* STATUS_DATA_MSB definitions while MOD_STATUS_SEL is 0 */
#define AUTO_RES_CAL_DONE_BIT			BIT(5)
#define CAL_TLRA_CL_STS_MSB_MASK		GENMASK(4, 0)
/* STATUS_DATA_MSB definition while MOD_STATUS_SEL is 3 */
#define LAST_GOOD_TLRA_CL_MSB_MASK		GENMASK(4, 0)
/* STATUS_DATA_MSB definition while MOD_STATUS_SEL is 4 */
#define TLRA_CL_ERR_MSB_MASK			GENMASK(4, 0)
/* STATUS_DATA_MSB when MOD_STATUS_SEL is 5 and MOD_STATUS_XT.SEL is 1 */
#define FIFO_REAL_TIME_FILL_STATUS_MSB_MASK_V2	GENMASK(1, 0)
#define FIFO_REAL_TIME_FILL_STATUS_MSB_MASK_V3	GENMASK(2, 0)
#define FIFO_REAL_TIME_FILL_STATUS_MSB_MASK_V4	GENMASK(3, 0)
#define FIFO_EMPTY_FLAG_BIT			BIT(6)
#define FIFO_FULL_FLAG_BIT			BIT(5)

#define HAP_CFG_STATUS_DATA_LSB_REG		0x0A
/* STATUS_DATA_LSB definition while MOD_STATUS_SEL is 0 */
#define CAL_TLRA_CL_STS_LSB_MASK		GENMASK(7, 0)
/* STATUS_DATA_LSB when MOD_STATUS_SEL is 5 and MOD_STATUS_XT.SEL is 1 */
#define FIFO_REAL_TIME_FILL_STATUS_LSB_MASK	GENMASK(7, 0)
/* STATUS_DATA_LSB when MOD_STATUS_SEL is 5 and MOD_STATUS_XT.SEL is 0 */
#define HAP_DRV_PATTERN_SRC_STATUS_MASK		GENMASK(2, 0)

#define HAP_CFG_FAULT_STATUS_REG		0x0C
#define SC_FLAG_BIT				BIT(2)
#define AUTO_RES_ERROR_BIT			BIT(1)
#define HPRW_RDY_FAULT_BIT			BIT(0)

/* Only for HAP525_HV */
#define HAP_CFG_HPWR_INTF_REG                   0x0B
#define HPWR_INTF_STATUS_MASK                   GENMASK(1, 0)
#define HPWR_DISABLED                           0
#define HPWR_READY                              3

#define HAP_CFG_REAL_TIME_LRA_IMPEDANCE_REG	0x0E
#define LRA_IMPEDANCE_MOHMS_LSB			250

#define HAP_CFG_INT_RT_STS_REG			0x10
#define FIFO_EMPTY_BIT				BIT(1)

/* config register definitions in HAPTICS_CFG module */
#define HAP_CFG_EN_CTL_REG			0x46
#define HAPTICS_EN_BIT				BIT(7)

#define HAP_CFG_DRV_CTRL_REG			0x47
#define PSTG_DLY_MASK				GENMASK(7, 6)
#define DRV_SLEW_RATE_MASK			GENMASK(2, 0)

#define HAP_CFG_VMAX_REG			0x48
#define VMAX_HV_STEP_MV				50
#define VMAX_MV_STEP_MV				32
#define MAX_VMAX_MV				11000
#define MAX_HV_VMAX_MV				10000
#define MAX_MV_VMAX_MV				6000
#define CLAMPED_VMAX_MV				5000
#define DEFAULT_VMAX_MV				5000

#define HAP_CFG_DRV_WF_SEL_REG			0x49
#define DRV_WF_FMT_BIT				BIT(4)
#define DRV_WF_SEL_MASK				GENMASK(1, 0)

#define HAP_CFG_AUTO_SHUTDOWN_CFG_REG		0x4A

#define HAP_CFG_TRIG_PRIORITY_REG		0x4B
#define SWR_IGNORE_BIT				BIT(4)

#define HAP_CFG_SPMI_PLAY_REG			0x4C
#define PLAY_EN_BIT				BIT(7)
#define PATX_MEM_SEL_MASK			GENMASK(5, 4) /* This is only for HAP525_HV */
#define BRAKE_EN_BIT				BIT(3)
#define PAT_SRC_MASK				GENMASK(2, 0)

#define HAP_CFG_EXT_TRIG_REG			0x4D

#define HAP_CFG_SWR_ACCESS_REG			0x4E
#define SWR_PAT_CFG_EN_BIT			BIT(7)
#define SWR_PAT_INPUT_EN_BIT			BIT(6)
#define SWR_PAT_RES_N_BIT			BIT(5)

#define HAP_CFG_BRAKE_MODE_CFG_REG		0x50
#define BRAKE_MODE_MASK				GENMASK(7, 6)
#define BRAKE_MODE_SHIFT			6
#define BRAKE_SINE_GAIN_MASK			GENMASK(3, 2)
#define BRAKE_SINE_GAIN_SHIFT			2
#define BRAKE_WF_SEL_MASK			GENMASK(1, 0)

#define HAP_CFG_CL_BRAKE_CFG_REG		0x51
#define HAP_CFG_CL_BRAKE_CAL_PARAM_REG		0x52
#define HAP_CFG_CL_BRAKE_RSET_REG		0x53
#define HAP_CFG_PWM_CFG_REG			0x5A

#define HAP_CFG_TLRA_OL_HIGH_REG		0x5C
#define TLRA_OL_MSB_MASK			GENMASK(3, 0)
#define HAP_CFG_TLRA_OL_LOW_REG			0x5D
#define TLRA_OL_LSB_MASK			GENMASK(7, 0)
#define TLRA_STEP_US				5
#define TLRA_MAX_US				20475

#define HAP_CFG_RC_CLK_CAL_COUNT_MSB_REG	0x5E
#define RC_CLK_CAL_COUNT_MSB_MASK		GENMASK(1, 0)
#define HAP_CFG_RC_CLK_CAL_COUNT_LSB_REG	0x5F
#define RC_CLK_CAL_COUNT_LSB_MASK		GENMASK(7, 0)

#define HAP_CFG_DRV_DUTY_CFG_REG		0x60
#define ADT_DRV_DUTY_EN_BIT			BIT(7)
#define ADT_BRK_DUTY_EN_BIT			BIT(6)
#define DRV_DUTY_MASK				GENMASK(5, 3)
#define DRV_DUTY_62P5_PCT			2
#define DRV_DUTY_SHIFT				3
#define BRK_DUTY_MASK				GENMASK(2, 0)
#define BRK_DUTY_75_PCT			6

#define HAP_CFG_ADT_DRV_DUTY_CFG_REG		0x61
#define HAP_CFG_ZX_WIND_CFG_REG			0x62

#define HAP_CFG_AUTORES_CFG_REG			0x63
#define AUTORES_EN_BIT				BIT(7)
#define AUTORES_EN_DLY_MASK			GENMASK(5, 2)
#define AUTORES_EN_DLY(cycles)			((cycles) * 2)
#define AUTORES_EN_DLY_6_CYCLES			AUTORES_EN_DLY(6)
#define AUTORES_EN_DLY_7_CYCLES			AUTORES_EN_DLY(7)
#define AUTORES_EN_DLY_SHIFT			2
#define AUTORES_ERR_WINDOW_MASK			GENMASK(1, 0)
#define AUTORES_ERR_WINDOW_12P5_PERCENT		0x0
#define AUTORES_ERR_WINDOW_25_PERCENT		0x1
#define AUTORES_ERR_WINDOW_50_PERCENT		0x2
#define AUTORES_ERR_WINDOW_100_PERCENT		0x3

#define HAP_CFG_AUTORES_ERR_RECOVERY_REG	0x64
#define EN_HW_RECOVERY_BIT			BIT(1)
#define SW_ERR_DRV_FREQ_BIT			BIT(0)

#define HAP_CFG_ISC_CFG_REG			0x65
#define ILIM_CC_EN_BIT				BIT(7)
/* Following bits are only for HAP525_HV */
#define EN_SC_DET_P_HAP525_HV_BIT		BIT(6)
#define EN_SC_DET_N_HAP525_HV_BIT		BIT(5)
#define EN_IMP_DET_HAP525_HV_BIT		BIT(4)
#define ILIM_PULSE_DENSITY_MASK			GENMASK(3, 2)
#define ILIM_DENSITY_8_OVER_64_CYCLES		0

#define HAP_CFG_FAULT_CLR_REG			0x66
#define SC_CLR_BIT				BIT(2)
#define AUTO_RES_ERR_CLR_BIT			BIT(1)
#define HPWR_RDY_FAULT_CLR_BIT			BIT(0)

#define HAP_CFG_VMAX_HDRM_REG			0x67
#define VMAX_HDRM_MASK				GENMASK(6, 0)
#define VMAX_HDRM_STEP_MV			50
#define VMAX_HDRM_MAX_MV			6350

#define HAP_CFG_VSET_CFG_REG			0x68
#define FORCE_VSET_ACK_BIT			BIT(1) /* This is only for HAP525_HV */
#define FORCE_VREG_RDY_BIT			BIT(0)

#define HAP_CFG_MOD_STATUS_SEL_REG		0x70
#define HAP_CFG_MOD_STATUS_XT_REG		0x71

#define HAP_CFG_CAL_EN_REG			0x72
#define CAL_RC_CLK_MASK				GENMASK(3, 2)
#define CAL_RC_CLK_SHIFT			2
#define CAL_RC_CLK_DISABLED_VAL			0
#define CAL_RC_CLK_AUTO_VAL			1
#define CAL_RC_CLK_MANUAL_VAL			2

/* For HAP520_MV and HAP525_HV */
#define HAP_CFG_ISC_CFG2_REG			0x77
#define EN_SC_DET_P_HAP520_MV_BIT		BIT(6)
#define EN_SC_DET_N_HAP520_MV_BIT		BIT(5)
#define ISC_THRESH_HAP520_MV_MASK		GENMASK(2, 0)
#define ISC_THRESH_HAP520_MV_140MA		0x01
#define ISC_THRESH_HAP525_HV_MASK		GENMASK(4, 0)
#define ISC_THRESH_HAP525_HV_125MA		0x11
#define ISC_THRESH_HAP525_HV_250MA		0x12

/* These registers are only applicable for HAP520_MV */
#define HAP_CFG_HW_CONFIG_REG			0x0D
#define HV_HAP_DRIVER_BIT			BIT(1)

#define HAP_CFG_HPWR_INTF_CTL_REG		0x80
#define INTF_CTL_MASK				GENMASK(1, 0)
#define INTF_CTL_BOB				1
#define INTF_CTL_BHARGER			2

#define HAP_CFG_VHPWR_REG			0x84
#define VHPWR_STEP_MV				32

/* Only for HAP525_HV */
#define HAP_CFG_RT_LRA_IMPD_MEAS_CFG_REG	0xA4
#define LRA_IMPEDANCE_MEAS_EN_BIT		BIT(7)
#define LRA_IMPEDANCE_MEAS_CURRENT_SEL_BIT	BIT(0)
#define CURRENT_SEL_VAL_125MA			0
#define CURRENT_SEL_VAL_250MA			1

/* version register definitions for HAPTICS_PATTERN module */
#define HAP_PTN_REVISION2_REG			0x01
#define HAP_PTN_V1				0x1
#define HAP_PTN_V2				0x2
#define HAP_PTN_V3				0x3
#define HAP_PTN_V4				0x4

/* status register definition for HAPTICS_PATTERN module */
#define HAP_PTN_FIFO_READY_STS_REG		0x08
#define FIFO_READY_BIT				BIT(0)

#define HAP_PTN_NUM_PAT_REG			0x09

/* config register definition for HAPTICS_PATTERN module */
#define HAP_PTN_FIFO_DIN_0_REG			0x20
#define HAP_PTN_FIFO_DIN_NUM			4

#define HAP_PTN_FIFO_PLAY_RATE_REG		0x24
#define PAT_MEM_PLAY_RATE_MASK			GENMASK(7, 4)
#define FIFO_PLAY_RATE_MASK			GENMASK(3, 0)

#define HAP_PTN_FIFO_EMPTY_CFG_REG		0x2A
#define EMPTY_THRESH_MASK			GENMASK(3, 0)
#define HAP_PTN_FIFO_THRESH_LSB			40

#define HAP_PTN_FIFO_DEPTH_CFG_REG		0x2B
#define HAP_PTN_FIFO_DIN_1B_REG			0x2C

#define HAP_PTN_DIRECT_PLAY_REG			0x26
#define DIRECT_PLAY_MAX_AMPLITUDE		0xFF

#define HAP_PTN_AUTORES_CAL_CFG_REG		0x28

#define HAP_PTN_PTRN1_TLRA_MSB_REG		0x30
#define HAP_PTN_PTRN1_TLRA_LSB_REG		0x31
#define HAP_PTN_PTRN2_TLRA_MSB_REG		0x32
#define HAP_PTN_PTRN2_TLRA_LSB_REG		0x33

#define HAP_PTN_PTRN1_CFG_REG			0x34
#define PTRN_FLRA2X_SHIFT			7
#define PTRN_SAMPLE_PER_MASK			GENMASK(2, 0)

#define PTRN_AMP_MSB_MASK			BIT(0)
#define PTRN_AMP_LSB_MASK			GENMASK(7, 0)

#define HAP_PTN_PTRN2_CFG_REG			0x50

#define HAP_PTN_BRAKE_AMP_REG			0x70

/* HAPTICS_PATTERN registers only present in HAP525_HV */
#define HAP_PTN_MEM_OP_ACCESS_REG		0x2D
#define MEM_PAT_ACCESS_BIT			BIT(7)
#define MEM_PAT_RW_SEL_MASK			GENMASK(5, 4)
#define MEM_FLUSH_RELOAD_BIT			BIT(0)

#define HAP_PTN_MMAP_FIFO_REG			0xA0
#define MMAP_FIFO_EXIST_BIT			BIT(7)
#define MMAP_FIFO_LEN_MASK			GENMASK(3, 0)
#define MMAP_FIFO_LEN_PER_LSB			128

#define HAP_PTN_MMAP_PAT1_REG			0xA1
#define MMAP_PAT_LEN_PER_LSB			32
#define MMAP_PAT1_LEN_MASK			GENMASK(6, 0)
#define MMAP_PAT2_LEN_MASK			GENMASK(5, 0)
#define MMAP_PAT3_PAT4_LEN_MASK			GENMASK(4, 0)

/* register in HBOOST module */
#define HAP_BOOST_REVISION1			0x00
#define HAP_BOOST_REVISION2			0x01
#define HAP_BOOST_V0P0				0x0000
#define HAP_BOOST_V0P1				0x0001
#define HAP_BOOST_V0P2				0x0002

#define HAP_BOOST_STATUS4_REG			0x0B
#define BOOST_DTEST1_STATUS_BIT			BIT(0)

#define HAP_BOOST_HW_CTRL_FOLLOW_REG		0x41
#define FOLLOW_HW_EN_BIT			BIT(7)
#define FOLLOW_HW_CCM_BIT			BIT(6)
#define FOLLOW_HW_VSET_BIT			BIT(5)

#define HAP_BOOST_VREG_EN_REG			0x46
#define VREG_EN_BIT				BIT(7)

#define HAP_BOOST_CLAMP_REG			0x70
#define CLAMP_5V_BIT				BIT(0)

/* haptics SDAM registers offset definition */
#define HAP_STATUS_DATA_MSB_SDAM_OFFSET		0x46
#define HAP_AUTO_BRAKE_CAL_VMAX_OFFSET		0x4B
#define HAP_AUTO_BRAKE_CAL_PTRN1_CFG0_OFFSET	0x4C
#define HAP_AUTO_BRAKE_CAL_PTRN1_LSB0_OFFSET	0x52
#define HAP_LRA_PTRN1_TLRA_LSB_OFFSET		0x64
#define HAP_LRA_PTRN1_TLRA_MSB_OFFSET		0x65
#define HAP_LRA_NOMINAL_OHM_SDAM_OFFSET		0x75
#define HAP_LRA_DETECTED_OHM_SDAM_OFFSET	0x76
#define HAP_AUTO_BRAKE_CAL_DONE_OFFSET		0x7E

#define AUTO_BRAKE_CAL_DONE			0x80

#define PBS_ARG_REG				0x42
#define HAP_VREG_ON_VAL				0x1
#define HAP_VREG_OFF_VAL			0x2
#define HAP_AUTO_BRAKE_CAL_VAL			0x3
#define PBS_TRIG_SET_REG			0xE5
#define PBS_TRIG_CLR_REG			0xE6
#define PBS_TRIG_SET_VAL			0x1
#define PBS_TRIG_CLR_VAL			0x1

/* constant parameters */
#define SAMPLES_PER_PATTERN			8
#define BRAKE_SAMPLE_COUNT			8
#define DEFAULT_ERM_PLAY_RATE_US		5000
#define MAX_EFFECT_COUNT			64
#define FIFO_READY_TIMEOUT_MS			1000
#define CHAR_PER_PATTERN_S			48
#define CHAR_PER_SAMPLE				8
#define CHAR_MSG_HEADER				16
#define CHAR_BRAKE_MODE				24
#define HW_BRAKE_MAX_CYCLES			16
#define F_LRA_VARIATION_HZ			5
#define NON_HBOOST_MAX_VMAX_MV			4000
/* below definitions are only for HAP525_HV */
#define MMAP_NUM_BYTES				2048
#define MMAP_FIFO_MIN_SIZE			640
#define FIFO_PRGM_INIT_SIZE			320

#define is_between(val, min, max)	\
	(((min) <= (max)) && ((min) <= (val)) && ((val) <= (max)))

enum hap_status_sel {
	CAL_TLRA_CL_STS = 0x00,
	T_WIND_STS,
	T_WIND_STS_PREV,
	LAST_GOOD_TLRA_CL_STS,
	TLRA_CL_ERR_STS,
	HAP_DRV_STS,
	RNAT_RCAL_INT,
	BRAKE_CAL_SCALAR = 0x07,
	CLAMPED_DUTY_CYCLE_STS = 0x8003,
	FIFO_REAL_TIME_STS = 0x8005,
};

enum drv_sig_shape {
	WF_SQUARE,
	WF_SINE,
	WF_NO_MODULATION,
	WF_RESERVED,
};

enum brake_mode {
	OL_BRAKE,
	CL_BRAKE,
	PREDICT_BRAKE,
	AUTO_BRAKE,
};

enum brake_sine_gain {
	BRAKE_SINE_GAIN_X1,
	BRAKE_SINE_GAIN_X2,
	BRAKE_SINE_GAIN_X4,
	BRAKE_SINE_GAIN_X8,
};

enum pat_mem_sel {
	PAT1_MEM,
	PAT2_MEM,
	PAT3_MEM,
	PAT4_MEM,
	PAT_MEM_MAX,
};

enum pattern_src {
	FIFO,
	DIRECT_PLAY,
	PATTERN1,
	PATTERN2,
	SWR,
	PATTERN_MEM,
	SRC_RESERVED,
};

enum s_period {
	T_LRA = 0,
	T_LRA_DIV_2,
	T_LRA_DIV_4,
	T_LRA_DIV_8,
	T_LRA_X_2,
	T_LRA_X_4,
	T_LRA_X_8,
	T_RESERVED,
	/* F_xKHZ definitions are for FIFO only */
	F_8KHZ,
	F_16KHZ,
	F_24KHZ,
	F_32KHZ,
	F_44P1KHZ,
	F_48KHZ,
	F_RESERVED,
};

enum custom_effect_param {
	CUSTOM_DATA_EFFECT_IDX,
	CUSTOM_DATA_TIMEOUT_SEC_IDX,
	CUSTOM_DATA_TIMEOUT_MSEC_IDX,
	CUSTOM_DATA_LEN,
};

/*
 * HW type of the haptics module, the type value follows the
 * revision value of the HAPTICS_CFG/HAPTICS_PATTERN modules
 */
enum haptics_hw_type {
	HAP520 = 0x2,  /* PM8350B */
	HAP520_MV = 0x3,  /* PM5100 */
	HAP525_HV = 0x4,  /* PM8550B */
};

enum wa_flags {
	TOGGLE_CAL_RC_CLK = BIT(0),
	SW_CTRL_HBST = BIT(1),
	SLEEP_CLK_32K_SCALE = BIT(2),
	TOGGLE_EN_TO_FLUSH_FIFO = BIT(3),
	RECOVER_SWR_SLAVE = BIT(4),
};

static const char * const src_str[] = {
	"FIFO",
	"DIRECT_PLAY",
	"PATTERN1",
	"PATTERN2",
	"SWR",
	"reserved",
};

static const char * const brake_str[] = {
	"open-loop-brake",
	"close-loop-brake",
	"predictive-brake",
	"auto-brake",
};

static const char * const period_str[] = {
	"T_LRA",
	"T_LRA_DIV_2",
	"T_LRA_DIV_4",
	"T_LRA_DIV_8",
	"T_LRA_X_2",
	"T_LRA_X_4",
	"T_LRA_X_8",
	"reserved_1",
	"F_8KHZ",
	"F_16HKZ",
	"F_24KHZ",
	"F_32KHZ",
	"F_44P1KHZ",
	"F_48KHZ",
	"reserved_2",
};

struct pattern_s {
	u16			amplitude;
	enum s_period		period;
	bool			f_lra_x2;
};

struct pattern_cfg {
	struct pattern_s	samples[SAMPLES_PER_PATTERN];
	u32			play_rate_us;
	u32			play_length_us;
	bool			preload;
};

struct fifo_cfg {
	u8			*samples;
	u32			num_s;
	enum s_period		period_per_s;
	u32			play_length_us;
	bool			preload;
};

struct brake_cfg {
	u8			samples[BRAKE_SAMPLE_COUNT];
	enum brake_mode		mode;
	enum drv_sig_shape	brake_wf;
	enum brake_sine_gain	sine_gain;
	u32			play_length_us;
	bool			disabled;
};

struct haptics_effect {
	struct pattern_cfg	*pattern;
	struct fifo_cfg		*fifo;
	struct brake_cfg	*brake;
	u32			id;
	u32			vmax_mv;
	u32			t_lra_us;
	enum pattern_src	src;
	enum pat_mem_sel	pat_sel;
	bool			auto_res_disable;
};

struct mmap_partition {
	u32	max_size;
	u32	length;
	bool	in_use;
};

struct haptics_mmap {
	struct mmap_partition	fifo_mmap;
	struct mmap_partition	pat_sel_mmap[PAT_MEM_MAX];
	enum s_period		pat_play_rate;
};

/**
 * struct fifo_play_status - Data used for recording the FIFO playing status
 *
 * @samples_written:	The number of the samples that has been written into
 *			FIFO memory.
 * @written_done:	The flag to indicate if all of the FIFO samples has
 *			been written to the FIFO memory.
 * @is_busy:		The flag to indicate if it's in the middle of FIFO
 *			playing.
 * @cancelled:		The flag to indicate if FIFO playing is cancelled due
 *			to a stopping command arrived in the middle of playing.
 */
struct fifo_play_status {
	u32			samples_written;
	atomic_t		written_done;
	atomic_t		is_busy;
	atomic_t		cancelled;
};

struct haptics_play_info {
	struct haptics_effect	*effect;
	struct brake_cfg	*brake;
	struct fifo_play_status	fifo_status;
	struct mutex		lock;
	atomic_t		gain;
	u32			vmax_mv;
	u32			length_us;
	enum pattern_src	pattern_src;
	bool			in_calibration;
};

struct haptics_hw_config {
	struct brake_cfg	brake;
	u32			vmax_mv;
	u32			t_lra_us;
	u32			cl_t_lra_us;
	u32			lra_min_mohms;
	u32			lra_max_mohms;
	u32			lra_open_mohms;
	u32			lra_measured_mohms;
	u32			preload_effect;
	u32			fifo_empty_thresh;
	u16			rc_clk_cal_count;
	enum drv_sig_shape	drv_wf;
	bool			is_erm;
	bool			measure_lra_impedance;
};

struct custom_fifo_data {
	u32	idx;
	u32	length;
	u32	play_rate_hz;
	u8	*data;
};

struct haptics_chip {
	struct device			*dev;
	struct regmap			*regmap;
	struct input_dev		*input_dev;
	struct haptics_hw_config	config;
	struct haptics_effect		*effects;
	struct haptics_effect		*primitives;
	struct haptics_effect		*custom_effect;
	struct haptics_play_info	play;
	struct haptics_mmap		mmap;
	struct dentry			*debugfs_dir;
	struct delayed_work		stop_work;
	struct regulator_dev		*swr_slave_rdev;
	struct nvmem_cell		*cl_brake_nvmem;
	struct nvmem_device		*hap_cfg_nvmem;
	struct device_node		*pbs_node;
	struct class			hap_class;
	struct regulator		*hpwr_vreg;
	struct hrtimer			hbst_off_timer;
	struct notifier_block		hboost_nb;
	struct mutex			vmax_lock;
	struct work_struct		set_gain_work;
	int				fifo_empty_irq;
	u32				hpwr_voltage_mv;
	u32				effects_count;
	u32				primitives_count;
	u32				cfg_addr_base;
	u32				ptn_addr_base;
	u32				hbst_addr_base;
	u32				clamped_vmax_mv;
	u32				wa_flags;
	u32				primitive_duration;
	u8				cfg_revision;
	u8				ptn_revision;
	u8				hpwr_intf_ctl;
	u16				hbst_revision;
	u16				max_vmax_mv;
	enum haptics_hw_type		hw_type;
	bool				fifo_empty_irq_en;
	bool				swr_slave_enabled;
	bool				clamp_at_5v;
	bool				hpwr_vreg_enabled;
	bool				is_hv_haptics;
	bool				hboost_enabled;
};

struct haptics_reg_info {
	u8 addr;
	u8 val;
};

static inline int get_max_fifo_samples(struct haptics_chip *chip)
{
	int val = 0;

	switch (chip->hw_type) {
	case HAP520:
		val = 640;
		break;
	case HAP520_MV:
		val = 1024;
		break;
	case HAP525_HV:
		val = chip->mmap.fifo_mmap.length ?
			chip->mmap.fifo_mmap.length : MMAP_FIFO_MIN_SIZE;
		break;
	default:
		pr_err("Invalid HW type\n");
		break;
	}

	return val;
}

static int get_fifo_empty_threshold(struct haptics_chip *chip)
{
	int val = 0;

	switch (chip->hw_type) {
	case HAP520:
		val = 280;
		break;
	case HAP520_MV:
	case HAP525_HV:
		val = 288;
		break;
	default:
		pr_err("Invalid HW type\n");
		break;
	}

	return val;
}

static int get_fifo_threshold_per_bit(struct haptics_chip *chip)
{
	int val = -EINVAL;

	switch (chip->hw_type) {
	case HAP520:
		val = 40;
		break;
	case HAP520_MV:
	case HAP525_HV:
		val = 32;
		break;
	default:
		pr_err("Invalid HW type\n");
		break;
	}

	return val;
}

static bool is_haptics_external_powered(struct haptics_chip *chip)
{
	/* SW based explicit vote */
	if (chip->hpwr_vreg)
		return true;

	/* Implicit voting by HW */
	if (chip->hw_type == HAP520_MV)
		return true;

	/* Powered by HBOOST */
	return false;
}

static int haptics_read(struct haptics_chip *chip,
		u16 base, u8 offset, u8 *val, u32 length)
{
	int rc = 0;
	u16 addr = base + offset;

	rc = regmap_bulk_read(chip->regmap, addr, val, length);
	if (rc < 0)
		dev_err(chip->dev, "read addr %#x failed, rc=%d\n", addr, rc);

	return rc;
}

static int haptics_write(struct haptics_chip *chip,
		u16 base, u8 offset, u8 *val, u32 length)
{
	int rc = 0;
	u16 addr = base + offset;

	rc = regmap_bulk_write(chip->regmap, addr, val, length);
	if (rc < 0)
		dev_err(chip->dev, "write addr %#x failed, rc=%d\n", addr, rc);

	return rc;
}

static int haptics_masked_write(struct haptics_chip *chip,
		u16 base, u8 offset, u8 mask, u8 val)
{
	int rc = 0;
	u16 addr = base + offset;

	regmap_update_bits(chip->regmap, addr, mask, val);
	if (rc < 0)
		dev_err(chip->dev, "update addr %#x failed, rc=%d\n", addr, rc);

	return rc;
}

static void __dump_effects(struct haptics_chip *chip,
				struct haptics_effect *effects, int effects_count)
{
	struct haptics_effect *effect;
	struct pattern_s *sample;
	char *str;
	u32 size, pos;
	int i, j;

	for (i = 0; i < effects_count; i++) {
		effect = &effects[i];
		if (!effect)
			return;

		dev_dbg(chip->dev, "effect %d\n", effect->id);
		dev_dbg(chip->dev, "vmax_mv = %d\n", effect->vmax_mv);
		if (effect->pattern) {
			for (j = 0; j < SAMPLES_PER_PATTERN; j++) {
				sample = &effect->pattern->samples[j];
				dev_dbg(chip->dev, "pattern = %d, period = %s, f_lra_x2 = %d\n",
						sample->amplitude,
						period_str[sample->period],
						sample->f_lra_x2);
			}

			dev_dbg(chip->dev, "pattern play_rate_us = %d\n",
					effect->pattern->play_rate_us);
			dev_dbg(chip->dev, "pattern play_length_us = %d\n",
					effect->pattern->play_length_us);
			dev_dbg(chip->dev, "pattern preload = %d\n",
					effect->pattern->preload);
		}

		if (effect->fifo) {
			size = effect->fifo->num_s * CHAR_PER_SAMPLE
							+ CHAR_MSG_HEADER;
			str = kzalloc(size, GFP_KERNEL);
			if (str == NULL)
				return;

			pos = 0;
			pos += scnprintf(str, size, "%s", "FIFO data: ");
			for (j = 0; j < effect->fifo->num_s; j++)
				pos += scnprintf(str + pos, size - pos, "%d ",
						(s8)effect->fifo->samples[j]);

			dev_dbg(chip->dev, "%s\n", str);
			kfree(str);
			dev_dbg(chip->dev, "FIFO data preload: %d, play rate: %s, play length: %uus\n",
					effect->fifo->preload,
					period_str[effect->fifo->period_per_s],
					effect->fifo->play_length_us);
		}

		if (effect->brake && !effect->brake->disabled) {
			size = BRAKE_SAMPLE_COUNT * CHAR_PER_SAMPLE
						+ CHAR_MSG_HEADER;
			str = kzalloc(size, GFP_KERNEL);
			if (str == NULL)
				return;

			pos = 0;
			pos += scnprintf(str, size, "%s", "brake pattern: ");
			for (j = 0; j < BRAKE_SAMPLE_COUNT; j++)
				pos += scnprintf(str + pos, size - pos, "%#x ",
						effect->brake->samples[j]);

			dev_dbg(chip->dev, "%s\n", str);
			kfree(str);
			dev_dbg(chip->dev, "brake mode: %s\n",
					brake_str[effect->brake->mode]);
			dev_dbg(chip->dev, "brake play length: %dus\n",
					effect->brake->play_length_us);
		}

		dev_dbg(chip->dev, "pattern src: %s\n", src_str[effect->src]);
		dev_dbg(chip->dev, "auto resonance %s\n",
				effect->auto_res_disable ?
				"disabled" : "enabled");
	}
}

static void verify_brake_samples(struct brake_cfg *brake)
{
	int i;

	if (brake->mode == PREDICT_BRAKE || brake->mode == AUTO_BRAKE)
		return;

	for (i = BRAKE_SAMPLE_COUNT - 1; i > 0; i--) {
		if (brake->samples[i] != 0) {
			brake->disabled = false;
			return;
		}
	}

	brake->disabled = true;
}

static int get_pattern_play_length_us(struct pattern_cfg *pattern)
{
	int i = SAMPLES_PER_PATTERN - 1, j;
	u32 us_per_sample, total_length_us = 0;

	if (!pattern)
		return -EINVAL;

	for (; i >= 0; i--)
		if (pattern->samples[i].amplitude != 0)
			break;

	for (j = 0; j <= i; j++) {
		us_per_sample = pattern->play_rate_us;
		switch (pattern->samples[j].period) {
		case T_LRA:
			break;
		case T_LRA_DIV_2:
			us_per_sample /= 2;
			break;
		case T_LRA_DIV_4:
			us_per_sample /= 4;
			break;
		case T_LRA_DIV_8:
			us_per_sample /= 8;
			break;
		case T_LRA_X_2:
			us_per_sample *= 2;
			break;
		case T_LRA_X_4:
			us_per_sample *= 4;
			break;
		case T_LRA_X_8:
			us_per_sample *= 8;
			break;
		default:
			return -EINVAL;
		}

		if (pattern->samples[j].f_lra_x2)
			us_per_sample /= 2;

		total_length_us += us_per_sample;
	}

	return total_length_us;
}

static int get_fifo_play_length_us(struct fifo_cfg *fifo, u32 t_lra_us)
{
	u32 length_us;
	int i;

	if (!fifo)
		return -EINVAL;

	for (i = fifo->num_s - 1; i > 0; i--)
		if (fifo->samples[i] != 0)
			break;

	length_us = (i + 1) * t_lra_us;
	switch (fifo->period_per_s) {
	case T_LRA:
		break;
	case T_LRA_DIV_2:
		length_us /= 2;
		break;
	case T_LRA_DIV_4:
		length_us /= 4;
		break;
	case T_LRA_DIV_8:
		length_us /= 8;
		break;
	case F_8KHZ:
		length_us = 1000 * fifo->num_s / 8;
		break;
	case F_16KHZ:
		length_us = 1000 * fifo->num_s / 16;
		break;
	case F_24KHZ:
		length_us = 1000 * fifo->num_s / 24;
		break;
	case F_32KHZ:
		length_us = 1000 * fifo->num_s / 32;
		break;
	case F_44P1KHZ:
		length_us = 10000 * fifo->num_s / 441;
		break;
	case F_48KHZ:
		length_us = 1000 * fifo->num_s / 48;
		break;
	default:
		length_us = -EINVAL;
		break;
	}

	return length_us;
}

static int get_brake_play_length_us(struct brake_cfg *brake, u32 t_lra_us)
{
	int i = BRAKE_SAMPLE_COUNT - 1;

	if (!brake || brake->disabled)
		return 0;

	if (brake->mode == PREDICT_BRAKE || brake->mode == AUTO_BRAKE)
		return HW_BRAKE_MAX_CYCLES * t_lra_us / 2;

	for (; i >= 0; i--)
		if (brake->samples[i] != 0)
			break;

	return t_lra_us * (i + 1) / 2;
}

static int haptics_get_status_data(struct haptics_chip *chip,
			enum hap_status_sel sel, u8 data[])
{
	int rc;
	u8 mod_sel_val[2];
	const char *hap_status_name[BRAKE_CAL_SCALAR + 1] = {
		"CAL_TLRA_CL_STS",
		"T_WIND_STS",
		"T_WIND_STS_PREV",
		"LAST_GOOD_TLRA_CL_STS",
		"TLRA_CL_ERR_STS",
		"HAP_DRV_STS",
		"RNAT_RCAL_INT",
		"BRAKE_CAL_SCALAR",
	};
	const char *name;

	mod_sel_val[0] = sel & 0xff;
	mod_sel_val[1] = (sel >> 8) & 0xff;
	rc = haptics_write(chip, chip->cfg_addr_base,
			HAP_CFG_MOD_STATUS_XT_REG, &mod_sel_val[1], 1);
	if (rc < 0)
		return rc;

	rc = haptics_write(chip, chip->cfg_addr_base,
			HAP_CFG_MOD_STATUS_SEL_REG, mod_sel_val, 1);
	if (rc < 0)
		return rc;

	rc = haptics_read(chip, chip->cfg_addr_base,
			HAP_CFG_STATUS_DATA_MSB_REG, data, 2);
	if (rc < 0)
		return rc;

	if (sel <= BRAKE_CAL_SCALAR)
		name = hap_status_name[sel];
	else if (sel == CLAMPED_DUTY_CYCLE_STS)
		name = "CLAMPED_DUTY_CYCLE_STS";
	else if (sel == FIFO_REAL_TIME_STS)
		name = "FIFO_REAL_TIME_STS";

	dev_dbg(chip->dev, "Get status data[%s] = (%#x, %#x)\n", name, data[0], data[1]);
	trace_qcom_haptics_status(name, data[0], data[1]);
	return 0;
}

#define AUTO_CAL_CLK_SCALE_DEN		1000
#define AUTO_CAL_CLK_SCALE_NUM		1024
static int haptics_adjust_lra_period(struct haptics_chip *chip, u32 *t_lra_us)
{
	int rc;
	u8 val;

	if (chip->wa_flags & SLEEP_CLK_32K_SCALE) {
		rc = haptics_read(chip, chip->cfg_addr_base,
				HAP_CFG_CAL_EN_REG, &val, 1);
		if (rc < 0)
			return rc;

		if ((val & CAL_RC_CLK_MASK) ==
				(CAL_RC_CLK_AUTO_VAL << CAL_RC_CLK_SHIFT))
			*t_lra_us = div_u64((u64)(*t_lra_us) * AUTO_CAL_CLK_SCALE_NUM,
					AUTO_CAL_CLK_SCALE_DEN);
	}

	return 0;
}

/* constant definitions for calculating TLRA */
#define TLRA_AUTO_RES_ERR_NO_CAL_STEP_PSEC	1667000
#define TLRA_AUTO_RES_NO_CAL_STEP_PSEC		3333000
#define TLRA_AUTO_RES_ERR_AUTO_CAL_STEP_PSEC	\
	((chip->wa_flags & SLEEP_CLK_32K_SCALE) ? 1627700 : 1666667)
#define TLRA_AUTO_RES_AUTO_CAL_STEP_PSEC	\
	((chip->wa_flags & SLEEP_CLK_32K_SCALE) ? 813850 : 833333)
#define SLEEP_CLK_CAL_DIVIDER			\
	((chip->wa_flags & SLEEP_CLK_32K_SCALE) ? 600 : 586)
#define CL_TLRA_ERROR_RANGE_PCT			20
static int haptics_get_closeloop_lra_period(
		struct haptics_chip *chip, bool in_boot)
{
	struct haptics_hw_config *config = &chip->config;
	u16 cal_tlra_cl_sts, tlra_cl_err_sts, tlra_ol, last_good_tlra_cl_sts;
	u8 val[2], rc_clk_cal;
	bool auto_res_done;
	u64 tmp;
	int rc;

	/* read RC_CLK_CAL enabling mode */
	rc = haptics_read(chip, chip->cfg_addr_base,
			HAP_CFG_CAL_EN_REG, val, 1);
	if (rc < 0)
		return rc;

	rc_clk_cal = ((val[0] & CAL_RC_CLK_MASK) >> CAL_RC_CLK_SHIFT);
	/* read auto resonance calibration result */
	if (in_boot && ((chip->hw_type == HAP520) || (chip->hw_type == HAP520_MV))) {
		if (chip->hap_cfg_nvmem == NULL) {
			dev_dbg(chip->dev, "nvmem device for hap_cfg is not defined\n");
			return -EINVAL;
		}

		rc = nvmem_device_read(chip->hap_cfg_nvmem,
				HAP_STATUS_DATA_MSB_SDAM_OFFSET, 2, val);
		if (rc < 0) {
			dev_err(chip->dev, "read SDAM %#x failed, rc=%d\n",
					HAP_STATUS_DATA_MSB_SDAM_OFFSET, rc);
			return rc;
		}
	} else {
		rc = haptics_get_status_data(chip, CAL_TLRA_CL_STS, val);
		if (rc < 0)
			return rc;
	}

	auto_res_done = !!(val[0] & AUTO_RES_CAL_DONE_BIT);
	cal_tlra_cl_sts =
		((val[0] & CAL_TLRA_CL_STS_MSB_MASK) << 8) | val[1];

	/* read auto resonance calibration error status */
	rc = haptics_get_status_data(chip, TLRA_CL_ERR_STS, val);
	if (rc < 0)
		return rc;

	tlra_cl_err_sts =
		((val[0] & TLRA_CL_ERR_MSB_MASK) << 8) | val[1];

	dev_dbg(chip->dev, "rc_clk_cal = %u, auto_res_done = %d\n",
			rc_clk_cal, auto_res_done);

	if (rc_clk_cal == CAL_RC_CLK_DISABLED_VAL && !auto_res_done) {
		/* TLRA_CL_ERR(us) = TLRA_CL_ERR_STS * 1.667 us */
		tmp = tlra_cl_err_sts * TLRA_AUTO_RES_ERR_NO_CAL_STEP_PSEC;
		dev_dbg(chip->dev, "tlra_cl_err_sts = %#x\n", tlra_cl_err_sts);
		config->cl_t_lra_us = div_u64(tmp, 1000000);
	} else if (rc_clk_cal == CAL_RC_CLK_DISABLED_VAL && auto_res_done) {
		/*
		 * CAL_TLRA_CL_STS_NO_CAL = CAL_TLRA_CL_STS
		 * TLRA_AUTO_RES(us) = CAL_TLRA_CL_STS_NO_CAL * 3.333 us
		 */
		tmp = cal_tlra_cl_sts * TLRA_AUTO_RES_NO_CAL_STEP_PSEC;
		dev_dbg(chip->dev, "cal_tlra_cl_sts = %#x\n", cal_tlra_cl_sts);
		config->cl_t_lra_us = div_u64(tmp, 1000000);
	} else if (rc_clk_cal == CAL_RC_CLK_AUTO_VAL && !auto_res_done) {
		/*
		 * CAL_TLRA_OL = CAL_TLRA_CL_STS;
		 * TLRA_CL_ERR(us) = TLRA_CL_ERR_STS *
		 *     (TLRA_OL / CAL_TLRA_OL) * TLRA_AUTO_RES_ERR_AUTO_CAL_STEP_US
		 */

		/* read the TLRA_OL setting */
		rc = haptics_read(chip, chip->cfg_addr_base,
				HAP_CFG_TLRA_OL_HIGH_REG, val, 2);
		if (rc < 0)
			return rc;

		tlra_ol = (val[0] & TLRA_OL_MSB_MASK) << 8 | val[1];
		dev_dbg(chip->dev, "tlra_ol = %#x, tlra_cl_err_sts = %#x, cal_tlra_cl_sts = %#x\n",
				tlra_ol, tlra_cl_err_sts, cal_tlra_cl_sts);

		tmp = tlra_cl_err_sts * tlra_ol;
		tmp *= TLRA_AUTO_RES_ERR_AUTO_CAL_STEP_PSEC;
		tmp = div_u64(tmp, cal_tlra_cl_sts);
		config->cl_t_lra_us = div_u64(tmp, 1000000);

		/* calculate RC_CLK_CAL_COUNT */
		if (!config->t_lra_us || !config->cl_t_lra_us)
			return -EINVAL;
		/*
		 * RC_CLK_CAL_COUNT = SLEEP_CLK_CAL_DIVIDER * (CAL_TLRA_OL / TLRA_OL)
		 *		* (SLEEP_CLK_CAL_DIVIDER / 586) * (CL_T_TLRA_US / OL_T_LRA_US)
		 */
		tmp = SLEEP_CLK_CAL_DIVIDER * SLEEP_CLK_CAL_DIVIDER;
		tmp *= cal_tlra_cl_sts * config->cl_t_lra_us;
		tmp = div_u64(tmp, tlra_ol);
		tmp = div_u64(tmp, 586);
		config->rc_clk_cal_count = div_u64(tmp, config->t_lra_us);
	} else if (rc_clk_cal == CAL_RC_CLK_AUTO_VAL && auto_res_done) {
		/*
		 * CAL_TLRA_CL_STS_W_CAL = CAL_TLRA_CL_STS;
		 * TLRA_AUTO_RES(us) = LAST_GOOD_TLRA_CL_STS *
		 *      TLRA_AUTO_RES_AUTO_CAL_STEP_US *
		 *     (LAST_GOOD_TLRA_CL_STS / CAL_TLRA_CL_STS_AUTO_CAL)
		 */

		/* read LAST_GOOD_TLRA_CL_STS */
		rc = haptics_get_status_data(chip, LAST_GOOD_TLRA_CL_STS, val);
		if (rc < 0)
			return rc;

		last_good_tlra_cl_sts =
			((val[0] & LAST_GOOD_TLRA_CL_MSB_MASK) << 8) | val[1];

		dev_dbg(chip->dev, "last_good_tlra_cl_sts = %#x, cal_tlra_cl_sts = %#x\n",
				last_good_tlra_cl_sts, cal_tlra_cl_sts);

		tmp = last_good_tlra_cl_sts * last_good_tlra_cl_sts;
		tmp *= TLRA_AUTO_RES_AUTO_CAL_STEP_PSEC;
		tmp = div_u64(tmp, cal_tlra_cl_sts);
		config->cl_t_lra_us = div_u64(tmp, 1000000);

		/* calculate RC_CLK_CAL_COUNT */
		if (!config->t_lra_us || !config->cl_t_lra_us)
			return -EINVAL;

		/*
		 * RC_CLK_CAL_COUNT =
		 *	SLEEP_CLK_CAL_DIVIDER * (CAL_TLRA_CL_STS_AUTO_CAL / LAST_GOOD_TLRA_CL_STS)
		 *		* (SLEEP_CLK_CAL_DIVIDER / 293) * (CL_T_TLRA_US / OL_T_LRA_US)
		 */
		tmp = SLEEP_CLK_CAL_DIVIDER * SLEEP_CLK_CAL_DIVIDER;
		tmp *= cal_tlra_cl_sts * config->cl_t_lra_us;
		tmp = div_u64(tmp, last_good_tlra_cl_sts);
		tmp = div_u64(tmp, 293);
		config->rc_clk_cal_count = div_u64(tmp, config->t_lra_us);
	} else {
		dev_err(chip->dev, "Can't get close-loop LRA period in rc_clk_cal mode %u\n",
				rc_clk_cal);
		return -EINVAL;
	}

	if ((abs(config->t_lra_us - config->cl_t_lra_us) * 100 / config->t_lra_us) >
			CL_TLRA_ERROR_RANGE_PCT) {
		dev_warn(chip->dev, "The calibrated period (%d us) has large variation, use open-loop LRA period (%d us) instead\n",
				config->cl_t_lra_us, config->t_lra_us);
		config->cl_t_lra_us = config->t_lra_us;
		chip->config.rc_clk_cal_count = 0;
	}

	dev_dbg(chip->dev, "OL_TLRA %u us, CL_TLRA %u us, RC_CLK_CAL_COUNT %#x\n",
		chip->config.t_lra_us, chip->config.cl_t_lra_us,
		chip->config.rc_clk_cal_count);
	return 0;
}

static int haptics_module_enable(struct haptics_chip *chip, bool enable)
{
	u8 val;
	int rc;

	val = enable ? HAPTICS_EN_BIT : 0;
	rc = haptics_write(chip, chip->cfg_addr_base,
		HAP_CFG_EN_CTL_REG, &val, 1);
	if (rc < 0)
		return rc;

	dev_dbg(chip->dev, "haptics module %s\n",
		enable ? "enabled" : "disabled");
	return 0;
}

static int haptics_toggle_module_enable(struct haptics_chip *chip)
{
	int rc;

	/*
	 * Updating HAPTICS_EN would vote hBoost enable status. Add 100us
	 * delay before updating HAPTICS_EN for hBoost to have enough time
	 * to handle its power transition.
	 */
	usleep_range(100, 101);
	rc = haptics_module_enable(chip, false);
	if (rc < 0)
		return rc;

	usleep_range(100, 101);
	return haptics_module_enable(chip, true);
}

#define VMAX_SETTLE_COUNT	10
static int haptics_check_hpwr_status(struct haptics_chip *chip)
{
	int i, rc = 0;
	u8 val;

	if (chip->hw_type != HAP525_HV)
		return 0;

	for (i = 0; i < VMAX_SETTLE_COUNT; i++) {
		rc = haptics_read(chip, chip->cfg_addr_base, HAP_CFG_HPWR_INTF_REG, &val, 1);
		if (rc < 0)
			break;

		val &= HPWR_INTF_STATUS_MASK;
		if ((val == HPWR_DISABLED) || (val == HPWR_READY))
			break;

		usleep_range(1000, 1001);
	}

	if (!rc && i == VMAX_SETTLE_COUNT) {
		haptics_toggle_module_enable(chip);
		dev_err(chip->dev, "set Vmax failed, toggle HAPTICS_EN to restore HW status\n");
		rc = -EBUSY;
	}

	return rc;
}

static int haptics_set_vmax_mv(struct haptics_chip *chip, u32 vmax_mv)
{
	int rc = 0;
	u8 val, vmax_step;

	mutex_lock(&chip->vmax_lock);
	if (vmax_mv > chip->max_vmax_mv) {
		dev_dbg(chip->dev, "vmax (%d) exceed the max value: %d\n",
					vmax_mv, chip->max_vmax_mv);
		vmax_mv = chip->max_vmax_mv;
	}

	if (vmax_mv > chip->clamped_vmax_mv)
		vmax_mv = chip->clamped_vmax_mv;

	if (chip->clamp_at_5v && (vmax_mv > CLAMPED_VMAX_MV))
		vmax_mv = CLAMPED_VMAX_MV;

	vmax_step = (chip->is_hv_haptics) ?
				VMAX_HV_STEP_MV : VMAX_MV_STEP_MV;
	val = vmax_mv / vmax_step;
	rc = haptics_write(chip, chip->cfg_addr_base,
			HAP_CFG_VMAX_REG, &val, 1);
	if (rc < 0) {
		dev_err(chip->dev, "config VMAX failed, rc=%d\n", rc);
		mutex_unlock(&chip->vmax_lock);
		return rc;
	}

	dev_dbg(chip->dev, "Set Vmax to %u mV\n", vmax_mv);

	rc = haptics_check_hpwr_status(chip);
	if (rc < 0)
		dev_err(chip->dev, "check hpwr_status failed, rc=%d\n", rc);

	mutex_unlock(&chip->vmax_lock);
	return rc;
}

static int haptics_set_vmax_headroom_mv(struct haptics_chip *chip, u32 hdrm_mv)
{
	int rc = 0;
	u8 val;

	if (hdrm_mv > VMAX_HDRM_MAX_MV) {
		dev_err(chip->dev, "headroom (%d) exceed the max value: %d\n",
					hdrm_mv, VMAX_HDRM_MAX_MV);
		return -EINVAL;
	}

	val = hdrm_mv / VMAX_HDRM_STEP_MV;
	rc = haptics_write(chip, chip->cfg_addr_base,
			HAP_CFG_VMAX_HDRM_REG, &val, 1);
	if (rc < 0)
		dev_err(chip->dev, "config VMAX_HDRM failed, rc=%d\n", rc);

	return rc;
}

static int haptics_get_vmax_headroom_mv(struct haptics_chip *chip, u32 *hdrm_mv)
{
	int rc;
	u8 val;

	rc = haptics_read(chip, chip->cfg_addr_base,
			HAP_CFG_VMAX_HDRM_REG, &val, 1);
	if (rc < 0) {
		dev_err(chip->dev, "Get Vmax HDRM failed, rc=%d\n",
				rc);
		return rc;
	}

	*hdrm_mv = (val & VMAX_HDRM_MASK) * VMAX_HDRM_STEP_MV;
	return 0;
}

static int haptics_enable_autores(struct haptics_chip *chip, bool en)
{
	int rc;

	rc = haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_AUTORES_CFG_REG, AUTORES_EN_BIT,
			en ? AUTORES_EN_BIT : 0);
	if (rc < 0)
		dev_err(chip->dev, "%s auto resonance failed, rc=%d\n",
				en ? "enable" : "disable", rc);

	return rc;
}

static int haptics_set_direct_play(struct haptics_chip *chip, u8 amplitude)
{
	int rc;

	rc = haptics_write(chip, chip->ptn_addr_base,
			HAP_PTN_DIRECT_PLAY_REG, &amplitude, 1);
	if (rc < 0)
		dev_err(chip->dev, "config DIRECT_PLAY failed, rc=%d\n", rc);

	return rc;
}

static bool is_boost_vreg_enabled_in_open_loop(struct haptics_chip *chip)
{
	int rc;
	u8 val;

	if (is_haptics_external_powered(chip))
		return false;

	rc = haptics_read(chip, chip->hbst_addr_base, HAP_BOOST_VREG_EN_REG, &val, 1);
	if (rc < 0)
		return false;

	chip->hboost_enabled = (val & VREG_EN_BIT);
	rc = haptics_read(chip, chip->hbst_addr_base, HAP_BOOST_HW_CTRL_FOLLOW_REG, &val, 1);
	if (rc < 0)
		return false;

	if (!(val & FOLLOW_HW_EN_BIT) && chip->hboost_enabled) {
		dev_dbg(chip->dev, "HBoost is enabled in open loop condition\n");
		return true;
	}

	return false;
}

static int haptics_boost_vreg_enable(struct haptics_chip *chip, bool en)
{
	int rc;
	u8 val;

	if (is_haptics_external_powered(chip))
		return 0;

	if (!(chip->wa_flags & SW_CTRL_HBST))
		return 0;

	if (chip->hap_cfg_nvmem == NULL) {
		dev_dbg(chip->dev, "nvmem device for hap_cfg is not defined\n");
		return 0;
	}

	if (is_boost_vreg_enabled_in_open_loop(chip)) {
		dev_dbg(chip->dev, "Ignore %s hBoost while it's enabled in open-loop mode\n",
				en ? "enabling" : "disabling");
		return 0;
	}

	if (chip->hboost_enabled == en)
		return 0;

	val = en ? HAP_VREG_ON_VAL : HAP_VREG_OFF_VAL;
	rc = nvmem_device_write(chip->hap_cfg_nvmem,
			PBS_ARG_REG, 1, &val);
	if (rc < 0) {
		dev_err(chip->dev, "write SDAM %#x failed, rc=%d\n",
				PBS_ARG_REG, rc);
		return rc;
	}

	val = PBS_TRIG_SET_VAL;
	rc = nvmem_device_write(chip->hap_cfg_nvmem,
			PBS_TRIG_SET_REG, 1, &val);
	if (rc < 0) {
		dev_err(chip->dev, "Write SDAM %#x failed, rc=%d\n",
				PBS_TRIG_SET_REG, rc);
		return rc;
	}

	chip->hboost_enabled = en;
	return 0;
}

static bool is_swr_play_enabled(struct haptics_chip *chip)
{
	int rc;
	u8 val[2];

	rc = haptics_get_status_data(chip, HAP_DRV_STS, val);
	if (rc < 0)
		return false;

	if ((val[1] & HAP_DRV_PATTERN_SRC_STATUS_MASK) == SWR)
		return true;

	return false;
}

#define HBOOST_WAIT_READY_COUNT		100
#define HBOOST_WAIT_READY_INTERVAL_US	200
static int haptics_wait_hboost_ready(struct haptics_chip *chip)
{
	int i, rc;
	u8 val;

	if (is_haptics_external_powered(chip))
		return 0;

	if (!(chip->wa_flags & SW_CTRL_HBST))
		return 0;

	if ((hrtimer_get_remaining(&chip->hbst_off_timer) > 0) ||
			hrtimer_active(&chip->hbst_off_timer)) {
		hrtimer_cancel(&chip->hbst_off_timer);
		dev_dbg(chip->dev, "hboost is still on, ignore\n");
		return 0;
	}

	/*
	 * Wait ~20ms until hBoost is ready, otherwise
	 * bail out and return -EBUSY
	 */
	for (i = 0; i < HBOOST_WAIT_READY_COUNT; i++) {
		/* HBoost is always ready when working in open loop mode */
		if (is_boost_vreg_enabled_in_open_loop(chip))
			return 0;

		/*
		 * If there is already a SWR play in the background, then HBoost
		 * will be kept as on hence no need to wait its ready.
		 */
		mutex_lock(&chip->play.lock);
		if (is_swr_play_enabled(chip)) {
			dev_dbg(chip->dev, "Ignore waiting hBoost when SWR play is in progress\n");
			mutex_unlock(&chip->play.lock);
			return 0;
		}
		mutex_unlock(&chip->play.lock);

		/* Check if HBoost is in standby (disabled) state */
		rc = haptics_read(chip, chip->hbst_addr_base,
				HAP_BOOST_VREG_EN_REG, &val, 1);
		if (!rc && !(val & VREG_EN_BIT)) {
			rc = haptics_read(chip, chip->hbst_addr_base,
					HAP_BOOST_STATUS4_REG, &val, 1);
			if (!rc && !(val & BOOST_DTEST1_STATUS_BIT))
				return 0;
		}

		dev_dbg(chip->dev, "hBoost is busy, wait %d\n", i);
		usleep_range(HBOOST_WAIT_READY_INTERVAL_US,
				HBOOST_WAIT_READY_INTERVAL_US + 1);
	}

	if (i == HBOOST_WAIT_READY_COUNT) {
		dev_err(chip->dev, "hboost is not ready for haptics play\n");
		return -EBUSY;
	}

	return 0;
}

static int haptics_enable_hpwr_vreg(struct haptics_chip *chip, bool en)
{
	int rc;

	if (chip->hpwr_vreg == NULL || chip->hpwr_vreg_enabled == en)
		return 0;

	if (en) {
		rc = regulator_set_voltage(chip->hpwr_vreg,
				chip->hpwr_voltage_mv * 1000, INT_MAX);
		if (rc < 0) {
			dev_err(chip->dev, "Set hpwr voltage failed, rc=%d\n",
					rc);
			return rc;
		}

		rc = regulator_enable(chip->hpwr_vreg);
		if (rc < 0) {
			dev_err(chip->dev, "Enable hpwr failed, rc=%d\n",
					rc);
			regulator_set_voltage(chip->hpwr_vreg, 0, INT_MAX);
			return rc;
		}
	} else {
		rc = regulator_disable(chip->hpwr_vreg);
		if (rc < 0) {
			dev_err(chip->dev, "Disable hpwr failed, rc=%d\n",
					rc);
			return rc;
		}

		rc = regulator_set_voltage(chip->hpwr_vreg, 0, INT_MAX);
		if (rc < 0) {
			dev_err(chip->dev, "Set hpwr voltage failed, rc=%d\n",
					rc);
			return rc;
		}
	}

	dev_dbg(chip->dev, "%s hpwr vreg\n", en ? "enabled" : "disabled");
	chip->hpwr_vreg_enabled = en;
	return 0;
}

static int haptics_force_vreg_ready(struct haptics_chip *chip, bool ready)
{
	u8 mask;

	mask = FORCE_VREG_RDY_BIT;
	if (chip->hw_type == HAP525_HV)
		mask |= FORCE_VSET_ACK_BIT;

	return haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_VSET_CFG_REG, mask, ready ? mask : 0);
}

static int haptics_open_loop_drive_config(struct haptics_chip *chip, bool en)
{
	int rc = 0;
	u8 val;

	if ((is_boost_vreg_enabled_in_open_loop(chip) ||
			chip->hboost_enabled || is_haptics_external_powered(chip)) && en) {
		/*
		 * Only set force-VREG-ready here if hBoost is not used by charger firmware.
		 * In the case of charger firmware enabling hBoost, force-VREG-ready will be
		 * set/reset based on the VMAX_CLAMP notification.
		 */
		if (chip->clamped_vmax_mv == MAX_HV_VMAX_MV) {
			rc = haptics_force_vreg_ready(chip, true);
			if (rc < 0)
				return rc;
		}

		/* Toggle RC_CLK_CAL_EN if it's in auto mode */
		rc = haptics_read(chip, chip->cfg_addr_base,
				HAP_CFG_CAL_EN_REG, &val, 1);
		if (rc < 0)
			return rc;

		if ((chip->wa_flags & TOGGLE_CAL_RC_CLK) &&
		    ((val & CAL_RC_CLK_MASK) ==
		      CAL_RC_CLK_AUTO_VAL << CAL_RC_CLK_SHIFT)) {
			val = CAL_RC_CLK_DISABLED_VAL << CAL_RC_CLK_SHIFT;
			rc = haptics_masked_write(chip, chip->cfg_addr_base,
					HAP_CFG_CAL_EN_REG, CAL_RC_CLK_MASK,
					val);
			if (rc < 0)
				return rc;

			val = CAL_RC_CLK_AUTO_VAL << CAL_RC_CLK_SHIFT;
			rc = haptics_masked_write(chip, chip->cfg_addr_base,
					HAP_CFG_CAL_EN_REG, CAL_RC_CLK_MASK,
					val);
			if (rc < 0)
				return rc;

			dev_dbg(chip->dev, "Toggle CAL_EN in open-loop-VREG playing\n");
		}
	} else if (!is_haptics_external_powered(chip) &&
			(chip->clamped_vmax_mv == MAX_HV_VMAX_MV)) {
		/*
		 * Reset force-VREG-ready only when hBoost is not
		 * used by charger firmware.
		 */
		rc = haptics_force_vreg_ready(chip, false);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int haptics_clear_fault(struct haptics_chip *chip)
{
	u8 val;

	val = SC_CLR_BIT | AUTO_RES_ERR_CLR_BIT |
		HPWR_RDY_FAULT_CLR_BIT;

	return haptics_write(chip, chip->cfg_addr_base,
			HAP_CFG_FAULT_CLR_REG, &val, 1);
}

static int haptics_wait_brake_complete(struct haptics_chip *chip)
{
	struct haptics_play_info *play = &chip->play;
	u32 brake_length_us, t_lra_us, timeout, delay_us;
	int rc;
	u8 val;

	if (chip->hw_type != HAP525_HV)
		return 0;

	t_lra_us = (chip->config.cl_t_lra_us) ?
		chip->config.cl_t_lra_us : chip->config.t_lra_us;

	brake_length_us = get_brake_play_length_us(play->brake, t_lra_us);

	/* add a cycle to give some margin for brake synchronization */
	brake_length_us += t_lra_us;
	delay_us = t_lra_us / 2;
	timeout = brake_length_us / delay_us + 1;
	dev_dbg(chip->dev, "wait %d us for brake pattern to complete\n", brake_length_us);

	/* poll HPWR_DISABLED to guarantee the brake pattern has been played completely */
	do {
		usleep_range(delay_us, delay_us + 1);
		rc = haptics_read(chip, chip->cfg_addr_base, HAP_CFG_HPWR_INTF_REG, &val, 1);
		if (rc < 0) {
			dev_err(chip->dev, "read HPWR_INTF failed, rc=%d\n", rc);
			return rc;
		}

		if ((val & HPWR_INTF_STATUS_MASK) == HPWR_DISABLED) {
			dev_dbg(chip->dev, "stopped play completely");
			break;
		}

		dev_dbg(chip->dev, "polling HPWR_INTF timeout %d, value = %d\n",
				timeout, val);
	} while (--timeout);

	if (timeout == 0) {
		dev_warn(chip->dev, "poll HPWR_DISABLED failed after stopped play\n");
		return haptics_toggle_module_enable(chip);
	}

	return 0;
}

#define BOOST_VREG_OFF_DELAY_SECONDS	2
static int haptics_enable_play(struct haptics_chip *chip, bool en)
{
	struct haptics_play_info *play = &chip->play;
	int rc;
	u8 val;

	if (en) {
		rc = haptics_clear_fault(chip);
		if (rc < 0)
			return rc;
	}

	rc = haptics_open_loop_drive_config(chip, en);
	if (rc < 0)
		return rc;

	val = play->pattern_src;
	if (chip->hw_type == HAP525_HV && play->pattern_src == PATTERN_MEM)
		val |= FIELD_PREP(PATX_MEM_SEL_MASK, play->effect->pat_sel);

	if (play->brake && !play->brake->disabled)
		val |= BRAKE_EN_BIT;

	if (en)
		val |= PLAY_EN_BIT;

	rc = haptics_write(chip, chip->cfg_addr_base,
			HAP_CFG_SPMI_PLAY_REG, &val, 1);
	if (rc < 0) {
		dev_err(chip->dev, "Write SPMI_PLAY failed, rc=%d\n", rc);
		return rc;
	}

	if (!en)
		haptics_wait_brake_complete(chip);

	if (chip->wa_flags & SW_CTRL_HBST) {
		if (en) {
			rc = haptics_boost_vreg_enable(chip, true);
			if (rc < 0) {
				dev_err(chip->dev, "Keep boost vreg on failed, rc=%d\n",
						rc);
				return rc;
			}
		} else {
			hrtimer_start(&chip->hbst_off_timer,
					ktime_set(BOOST_VREG_OFF_DELAY_SECONDS, 0),
					HRTIMER_MODE_REL);
		}
	}

	trace_qcom_haptics_play(en);
	return rc;
}

static int haptics_set_brake(struct haptics_chip *chip, struct brake_cfg *brake)
{
	int rc = 0;
	u8 val;

	if (brake->disabled)
		return 0;

	val = brake->mode << BRAKE_MODE_SHIFT |
		brake->sine_gain << BRAKE_SINE_GAIN_SHIFT;
	rc = haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_BRAKE_MODE_CFG_REG,
			BRAKE_MODE_MASK | BRAKE_SINE_GAIN_MASK, val);
	if (rc < 0) {
		dev_err(chip->dev, "set brake CFG failed, rc=%d\n", rc);
		return rc;
	}

	rc = haptics_write(chip, chip->ptn_addr_base, HAP_PTN_BRAKE_AMP_REG,
			brake->samples, BRAKE_SAMPLE_COUNT);
	if (rc < 0) {
		dev_err(chip->dev, "set brake pattern failed, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static int haptics_set_pattern(struct haptics_chip *chip,
		struct pattern_cfg *pattern,
		enum pattern_src src)
{
	struct pattern_s *sample;
	u8 values[SAMPLES_PER_PATTERN * 3] = { 0 };
	u8 ptn_tlra_addr, ptn_cfg_addr;
	int i, rc, tmp;
	u32 play_rate_us;

	if (src != PATTERN1 && src != PATTERN2) {
		dev_err(chip->dev, "no pattern src specified!\n");
		return -EINVAL;
	}

	ptn_tlra_addr = HAP_PTN_PTRN1_TLRA_MSB_REG;
	ptn_cfg_addr = HAP_PTN_PTRN1_CFG_REG;
	if (src == PATTERN2) {
		ptn_tlra_addr = HAP_PTN_PTRN2_TLRA_MSB_REG;
		ptn_cfg_addr = HAP_PTN_PTRN2_CFG_REG;
	}

	/* Adjust T_LRA before programming it into HW */
	play_rate_us = pattern->play_rate_us;
	rc = haptics_adjust_lra_period(chip, &play_rate_us);
	if (rc < 0)
		return rc;

	/* Configure T_LRA for this pattern */
	tmp = play_rate_us / TLRA_STEP_US;
	values[0] = (tmp >> 8) & TLRA_OL_MSB_MASK;
	values[1] = tmp & TLRA_OL_LSB_MASK;
	rc = haptics_write(chip, chip->ptn_addr_base, ptn_tlra_addr,
			values, 2);
	if (rc < 0) {
		dev_err(chip->dev, "update pattern TLRA failed, rc=%d\n", rc);
		return rc;
	}

	/* Configure pattern registers */
	for (i = 0; i < SAMPLES_PER_PATTERN; i++) {
		sample = &pattern->samples[i];
		values[i * 3] = sample->f_lra_x2 << PTRN_FLRA2X_SHIFT;
		values[i * 3] |= sample->period & PTRN_SAMPLE_PER_MASK;
		values[i * 3 + 1] =
			(sample->amplitude >> 8) & PTRN_AMP_MSB_MASK;
		values[i * 3 + 2] = sample->amplitude & PTRN_AMP_LSB_MASK;
	}

	rc = haptics_write(chip, chip->ptn_addr_base, ptn_cfg_addr,
			values, SAMPLES_PER_PATTERN * 3);
	if (rc < 0) {
		dev_err(chip->dev, "write pattern data failed, rc=%d\n", rc);
		return rc;
	}

	return 0;

}

static int haptics_update_memory_data(struct haptics_chip *chip,
					u8 *data, u32 length)
{
	int rc, count, i;
	u32 left;
	u8 tmp[HAP_PTN_FIFO_DIN_NUM] = {0};

	if (!length)
		return 0;

	count = length / HAP_PTN_FIFO_DIN_NUM;
	for (i = 0; i < count; i++) {
		rc = haptics_write(chip, chip->ptn_addr_base,
				HAP_PTN_FIFO_DIN_0_REG, data,
				HAP_PTN_FIFO_DIN_NUM);
		if (rc < 0) {
			dev_err(chip->dev, "bulk write FIFO_DIN failed, rc=%d\n",
					rc);
			return rc;
		}

		data += HAP_PTN_FIFO_DIN_NUM;
	}

	left = length % HAP_PTN_FIFO_DIN_NUM;
	if (left) {
		/*
		 * In HAP520 module, when 1-byte FIFO write clashes
		 * with the HW FIFO read operation, the HW will only read
		 * 1 valid byte in every 4 bytes FIFO samples. So avoid
		 * this by keeping the samples 4-byte aligned and always
		 * use 4-byte write for HAP520 module.
		 */
		if (chip->hw_type == HAP520) {
			memcpy(tmp, data, left);
			rc = haptics_write(chip, chip->ptn_addr_base,
					HAP_PTN_FIFO_DIN_0_REG, tmp,
					HAP_PTN_FIFO_DIN_NUM);
			if (rc < 0)
				dev_err(chip->dev, "write FIFO_DIN failed, rc=%d\n", rc);

			return rc;
		}

		for (i = 0; i < left; i++) {
			rc = haptics_write(chip, chip->ptn_addr_base,
					HAP_PTN_FIFO_DIN_1B_REG,
					(data + i), 1);
			if (rc < 0) {
				dev_err(chip->dev, "write FIFO_DIN_1B failed, rc=%d\n",
						rc);
				return rc;
			}
		}
	}

	return 0;
}

static int haptics_update_fifo_samples(struct haptics_chip *chip,
					u8 *samples, u32 length, bool refill)
{
	int rc;
	u8 val;

	if (!samples) {
		dev_err(chip->dev, "no data available to update FIFO\n");
		return -EINVAL;
	}

	if (chip->hw_type == HAP525_HV && !refill) {
		val = MEM_FLUSH_RELOAD_BIT;
		rc = haptics_write(chip, chip->ptn_addr_base,
				HAP_PTN_MEM_OP_ACCESS_REG, &val, 1);
		if (rc < 0)
			return rc;

		val = 0;
		rc = haptics_write(chip, chip->ptn_addr_base,
				HAP_PTN_MEM_OP_ACCESS_REG, &val, 1);
		if (rc < 0)
			return rc;
	}

	return haptics_update_memory_data(chip, samples, length);
}

static int haptics_update_pat_mem_samples(struct haptics_chip *chip,
		enum pat_mem_sel pat_sel, u8 *samples, u32 length)
{
	int rc;
	u8 val;

	if (!samples) {
		dev_err(chip->dev, "no data available to update PAT_MEM\n");
		return -EINVAL;
	}

	/* only HAP525_HV supports PATx_MEM pattern source */
	if (chip->hw_type != HAP525_HV) {
		dev_dbg(chip->dev, "HW type %d doesn't support PATx_MEM pattern source\n",
				chip->hw_type);
		return 0;
	}

	val = FIELD_PREP(MEM_PAT_RW_SEL_MASK, pat_sel);
	val |= MEM_PAT_ACCESS_BIT | MEM_FLUSH_RELOAD_BIT;
	rc = haptics_write(chip, chip->ptn_addr_base,
			HAP_PTN_MEM_OP_ACCESS_REG, &val, 1);
	if (rc < 0)
		return rc;

	val &= ~MEM_FLUSH_RELOAD_BIT;
	rc = haptics_write(chip, chip->ptn_addr_base,
			HAP_PTN_MEM_OP_ACCESS_REG, &val, 1);
	if (rc < 0)
		return rc;

	rc = haptics_update_memory_data(chip, samples, length);
	rc |= haptics_masked_write(chip, chip->ptn_addr_base,
			HAP_PTN_MEM_OP_ACCESS_REG, MEM_PAT_ACCESS_BIT, 0);
	return rc;
}

static int haptics_get_fifo_fill_status(struct haptics_chip *chip, u32 *fill)
{
	int rc;
	u8 val[2], fill_status_mask;
	u32 filled, available;
	bool empty = false, full = false;

	rc = haptics_get_status_data(chip, FIFO_REAL_TIME_STS, val);
	if (rc < 0)
		return rc;

	switch (chip->hw_type) {
	case HAP520:
		fill_status_mask = FIFO_REAL_TIME_FILL_STATUS_MSB_MASK_V2;
		break;
	case HAP520_MV:
		fill_status_mask = FIFO_REAL_TIME_FILL_STATUS_MSB_MASK_V3;
		break;
	case HAP525_HV:
		fill_status_mask = FIFO_REAL_TIME_FILL_STATUS_MSB_MASK_V4;
		break;
	default:
		dev_err(chip->dev, "HW type %d is not supported\n",
				chip->hw_type);
		return -EINVAL;
	}

	filled = ((val[0] & fill_status_mask) << 8) | val[1];
	empty = !!(val[0] & FIFO_EMPTY_FLAG_BIT);
	full = !!(val[0] & FIFO_FULL_FLAG_BIT);
	available = get_max_fifo_samples(chip) - filled;

	dev_dbg(chip->dev, "filled=%u, available=%u, full=%d, empty=%d\n",
			filled, available, full, empty);
	trace_qcom_haptics_fifo_hw_status(filled, available, full, empty);
	*fill = filled;
	return 0;
}

static int haptics_get_available_fifo_memory(struct haptics_chip *chip)
{
	int rc;
	u32 fill, available;

	rc = haptics_get_fifo_fill_status(chip, &fill);
	if (rc < 0)
		return rc;

	if (fill > get_max_fifo_samples(chip)) {
		dev_err(chip->dev, "Filled FIFO number %d exceed the max %d\n",
				fill, get_max_fifo_samples(chip));
		return -EINVAL;
	} else if (fill == get_max_fifo_samples(chip)) {
		dev_err(chip->dev, "no FIFO space available\n");
		return -EBUSY;
	}

	available = get_max_fifo_samples(chip) - fill;
	return available;
}

static int haptics_set_manual_rc_clk_cal(struct haptics_chip *chip)
{
	int rc;
	u16 cal_count = chip->config.rc_clk_cal_count;
	u8 val[2];

	if (cal_count == 0) {
		dev_dbg(chip->dev, "Ignore setting RC_CLK_CAL_COUNT\n");
		return 0;
	}

	val[0] = (cal_count >> 8) & RC_CLK_CAL_COUNT_MSB_MASK;
	val[1] = cal_count & RC_CLK_CAL_COUNT_LSB_MASK;
	rc = haptics_write(chip, chip->cfg_addr_base,
			HAP_CFG_RC_CLK_CAL_COUNT_MSB_REG, val, 2);
	if (rc < 0)
		return rc;

	val[0] = CAL_RC_CLK_MANUAL_VAL << CAL_RC_CLK_SHIFT;
	return haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_CAL_EN_REG, CAL_RC_CLK_MASK,
			val[0]);
}

static int haptics_set_fifo_playrate(struct haptics_chip *chip,
				enum s_period period_per_s)
{
	int rc;

	rc = haptics_masked_write(chip, chip->ptn_addr_base,
			HAP_PTN_FIFO_PLAY_RATE_REG,
			FIFO_PLAY_RATE_MASK, period_per_s);
	if (rc < 0)
		dev_err(chip->dev, "Set FIFO play rate failed, rc=%d\n", rc);

	return rc;
}

static int haptics_set_fifo_empty_threshold(struct haptics_chip *chip,
							u32 thresh)
{
	u8 thresh_per_bit;
	int rc;

	rc = get_fifo_threshold_per_bit(chip);
	if (rc < 0)
		return rc;

	thresh_per_bit = rc;
	rc = haptics_masked_write(chip, chip->ptn_addr_base,
			HAP_PTN_FIFO_EMPTY_CFG_REG,
			EMPTY_THRESH_MASK, (thresh / thresh_per_bit));
	if (rc < 0)
		dev_err(chip->dev, "Set FIFO empty threshold failed, rc=%d\n",
				rc);

	return rc;
}

static void haptics_fifo_empty_irq_config(struct haptics_chip *chip,
						bool enable)
{
	if (!chip->fifo_empty_irq_en && enable) {
		enable_irq(chip->fifo_empty_irq);
		chip->fifo_empty_irq_en = true;
	} else if (chip->fifo_empty_irq_en && !enable) {
		disable_irq_nosync(chip->fifo_empty_irq);
		chip->fifo_empty_irq_en = false;
	}
}

static int haptics_set_fifo(struct haptics_chip *chip, struct fifo_cfg *fifo)
{
	struct fifo_play_status *status = &chip->play.fifo_status;
	u32 num, fifo_thresh;
	int rc, available;

	if (atomic_read(&status->is_busy) == 1) {
		dev_err(chip->dev, "FIFO is busy\n");
		return -EBUSY;
	}

	/* Configure FIFO play rate */
	rc = haptics_set_fifo_playrate(chip, fifo->period_per_s);
	if (rc < 0)
		return rc;

	if (fifo->period_per_s >= F_8KHZ) {
		/* Set manual RC CLK CAL when playing FIFO */
		rc = haptics_set_manual_rc_clk_cal(chip);
		if (rc < 0)
			return rc;
	}

	atomic_set(&status->written_done, 0);
	atomic_set(&status->cancelled, 0);
	status->samples_written = 0;

	/*
	 * Write the 1st set of the data into FIFO if there are
	 * more than MAX_FIFO_SAMPLES samples, the rest will be
	 * written if any FIFO memory is available after playing.
	 */
	num = min_t(u32, fifo->num_s, get_max_fifo_samples(chip));
	available = haptics_get_available_fifo_memory(chip);
	if (available < 0)
		return available;

	num = min_t(u32, available, num);
	num = min_t(u32, num, FIFO_PRGM_INIT_SIZE);
	/* Keep the FIFO programming 4-byte aligned if FIFO refilling is needed */
	if ((num < fifo->num_s) && (num % HAP_PTN_FIFO_DIN_NUM))
		num = round_down(num, HAP_PTN_FIFO_DIN_NUM);

	rc = haptics_update_fifo_samples(chip, fifo->samples, num, false);
	if (rc < 0) {
		dev_err(chip->dev, "write FIFO samples failed, rc=%d\n", rc);
		return rc;
	}

	atomic_set(&status->is_busy, 1);
	status->samples_written = num;
	trace_qcom_haptics_fifo_prgm_status(fifo->num_s, status->samples_written, num);
	if (num == fifo->num_s) {
		fifo_thresh = 0;
		atomic_set(&status->written_done, 1);
	} else {
		fifo_thresh = chip->config.fifo_empty_thresh;
	}

	/*
	 * Set FIFO empty threshold here. FIFO empty IRQ will
	 * be enabled after playing FIFO samples so that more
	 * FIFO samples can be written (if available) when
	 * FIFO empty IRQ is triggered.
	 */
	rc = haptics_set_fifo_empty_threshold(chip, fifo_thresh);
	if (rc < 0)
		return rc;

	haptics_fifo_empty_irq_config(chip, true);

	return 0;
}

static int haptics_load_constant_effect(struct haptics_chip *chip, u8 amplitude)
{
	struct haptics_play_info *play = &chip->play;
	u32 hdrm_mv, vmax_mv = chip->config.vmax_mv;
	int rc = 0;

	mutex_lock(&chip->play.lock);
	if (chip->play.in_calibration) {
		dev_err(chip->dev, "calibration in progress, ignore playing constant effect\n");
		rc = -EBUSY;
		goto unlock;
	}

	/* No effect data when playing constant waveform */
	play->effect = NULL;

	/* Fix Vmax to (hpwr_vreg_mv - hdrm_mv) in non-HBOOST regulator case */
	if (is_haptics_external_powered(chip)) {
		rc = haptics_get_vmax_headroom_mv(chip, &hdrm_mv);
		if (rc < 0)
			goto unlock;

		vmax_mv = chip->hpwr_voltage_mv - hdrm_mv;
	}

	/* configure VMAX in case it was changed in previous effect playing */
	rc = haptics_set_vmax_mv(chip, vmax_mv);
	if (rc < 0)
		goto unlock;

	/* Config brake settings if it's necessary */
	play->brake = &chip->config.brake;
	if (play->brake) {
		rc = haptics_set_brake(chip, play->brake);
		if (rc < 0)
			goto unlock;
	}

	rc = haptics_set_direct_play(chip, amplitude);
	if (rc < 0)
		goto unlock;

	/* Always enable LRA auto resonance for DIRECT_PLAY */
	rc = haptics_enable_autores(chip, !chip->config.is_erm);
	if (rc < 0)
		goto unlock;

	play->pattern_src = DIRECT_PLAY;
unlock:
	mutex_unlock(&chip->play.lock);
	return rc;
}

static int haptics_load_predefined_effect(struct haptics_chip *chip,
					struct haptics_effect *effect)
{
	struct haptics_play_info *play = &chip->play;
	int rc;

	if (effect == NULL)
		return -EINVAL;

	play->effect = effect;
	/* Clamp VMAX for different vibration strength */
	rc = haptics_set_vmax_mv(chip, play->vmax_mv);
	if (rc < 0)
		return rc;

	play->pattern_src = play->effect->src;
	if (play->pattern_src == DIRECT_PLAY ||
			play->pattern_src == SWR) {
		dev_err(chip->dev, "pattern src %d can't be used for predefined effect\n",
				play->pattern_src);
		return -EINVAL;
	}

	if ((play->pattern_src == FIFO) && (chip->wa_flags & TOGGLE_EN_TO_FLUSH_FIFO)) {
		/* Toggle HAPTICS_EN for a clear start point of FIFO playing */
		rc = haptics_toggle_module_enable(chip);
		if (rc < 0)
			return rc;
	}

	rc = haptics_enable_autores(chip, !play->effect->auto_res_disable);
	if (rc < 0)
		return rc;

	play->brake = play->effect->brake;
	/* Config brake settings if it's necessary */
	if (play->brake) {
		rc = haptics_set_brake(chip, play->brake);
		if (rc < 0)
			return rc;
	}

	if (play->pattern_src == PATTERN1 || play->pattern_src == PATTERN2) {
		if (play->effect->pattern->preload) {
			dev_dbg(chip->dev, "Ignore preloaded effect: %d\n",
					play->effect->id);
			return 0;
		}

		rc = haptics_set_pattern(chip, play->effect->pattern,
						play->pattern_src);
		if (rc < 0)
			return rc;
	}

	if (play->pattern_src == FIFO) {
		rc = haptics_set_fifo(chip, play->effect->fifo);
		if (rc < 0)
			return rc;
	}

	/*
	 * PATTERN_MEM sources (PATx_MEM) introduced in HAP525_HV haptics
	 * module are used for preload effects. The pattern for the preload
	 * effect should have been programmed during boot up and it will be
	 * retained until device is powered off, so it doesn't need to be
	 * programmed at runtime.
	 */
	if (chip->hw_type == HAP525_HV &&
			play->pattern_src == PATTERN_MEM) {
		if (!play->effect->fifo->preload) {
			dev_err(chip->dev, "effect %d has PAT_MEM src but not preloaded\n",
					play->effect->id);
			return -EINVAL;
		}

		/* disable auto resonance for PATx_MEM mode */
		rc = haptics_enable_autores(chip, false);
		if (rc < 0)
			return rc;

		dev_dbg(chip->dev, "Ignore loading data for preload FIFO effect: %d\n",
				play->effect->id);
	}

	return 0;
}

static int haptics_init_custom_effect(struct haptics_chip *chip)
{
	chip->custom_effect = devm_kzalloc(chip->dev,
			sizeof(*chip->custom_effect), GFP_KERNEL);
	if (!chip->custom_effect)
		return -ENOMEM;

	chip->custom_effect->fifo = devm_kzalloc(chip->dev,
			sizeof(*chip->custom_effect->fifo), GFP_KERNEL);
	if (!chip->custom_effect->fifo)
		return -ENOMEM;

	/* custom effect will be played in FIFO mode without brake */
	chip->custom_effect->pattern = NULL;
	chip->custom_effect->brake = NULL;
	chip->custom_effect->id = UINT_MAX;
	chip->custom_effect->vmax_mv = chip->config.vmax_mv;
	chip->custom_effect->t_lra_us = chip->config.t_lra_us;
	chip->custom_effect->src = FIFO;
	chip->custom_effect->auto_res_disable = true;

	return 0;
}

static int haptics_convert_sample_period(struct haptics_chip *chip,
						u32 play_rate_hz)
{
	enum s_period period;
	u32 f_lra, f_lra_min, f_lra_max;

	if (chip->config.t_lra_us == 0)
		return -EINVAL;

	f_lra = USEC_PER_SEC / chip->config.t_lra_us;
	if (f_lra == 0 || f_lra < F_LRA_VARIATION_HZ)
		return -EINVAL;

	f_lra_min = f_lra - F_LRA_VARIATION_HZ;
	f_lra_max = f_lra + F_LRA_VARIATION_HZ;

	if (play_rate_hz == 8000)
		period = F_8KHZ;
	else if (play_rate_hz == 16000)
		period = F_16KHZ;
	else if (play_rate_hz == 24000)
		period = F_24KHZ;
	else if (play_rate_hz == 32000)
		period = F_32KHZ;
	else if (play_rate_hz == 44100)
		period = F_44P1KHZ;
	else if (play_rate_hz == 48000)
		period = F_48KHZ;
	else if (is_between(play_rate_hz, f_lra_min, f_lra_max))
		period = T_LRA;
	else if (is_between(play_rate_hz / 2, f_lra_min, f_lra_max))
		period = T_LRA_DIV_2;
	else if (is_between(play_rate_hz / 4, f_lra_min, f_lra_max))
		period = T_LRA_DIV_4;
	else if (is_between(play_rate_hz / 8, f_lra_min, f_lra_max))
		period = T_LRA_DIV_8;
	else if (is_between(play_rate_hz * 2, f_lra_min, f_lra_max))
		period = T_LRA_X_2;
	else if (is_between(play_rate_hz * 4, f_lra_min, f_lra_max))
		period = T_LRA_X_4;
	else if (is_between(play_rate_hz * 8, f_lra_min, f_lra_max))
		period = T_LRA_X_8;
	else
		return -EINVAL;

	return period;
}

static int haptics_load_custom_effect(struct haptics_chip *chip,
			s16 __user *data, u32 length, s16 magnitude)
{
	struct haptics_play_info *play = &chip->play;
	struct custom_fifo_data custom_data = {};
	struct fifo_cfg *fifo;
	int rc;

	if (!chip->custom_effect || !chip->custom_effect->fifo)
		return -ENOMEM;

	fifo = chip->custom_effect->fifo;
	if (copy_from_user(&custom_data, data, sizeof(custom_data)))
		return -EFAULT;

	dev_dbg(chip->dev, "custom data length %d with play-rate %d Hz\n",
			custom_data.length, custom_data.play_rate_hz);
	rc = haptics_convert_sample_period(chip, custom_data.play_rate_hz);
	if (rc < 0) {
		dev_err(chip->dev, "Can't support play rate: %d Hz\n",
				custom_data.play_rate_hz);
		return rc;
	}

	mutex_lock(&chip->play.lock);
	fifo->period_per_s = rc;
	/*
	 * Before allocating samples buffer, free the old sample
	 * buffer first if it's not been freed.
	 */
	kvfree(fifo->samples);
	fifo->samples = kcalloc(custom_data.length, sizeof(u8), GFP_KERNEL);
	if (!fifo->samples) {
		fifo->samples = vmalloc(custom_data.length);
		if (!fifo->samples) {
			rc = -ENOMEM;
			goto unlock;
		}
	}

	if (copy_from_user(fifo->samples,
				(u8 __user *)custom_data.data,
				custom_data.length)) {
		rc = -EFAULT;
		goto cleanup;
	}

	dev_dbg(chip->dev, "Copy custom FIFO samples successfully\n");
	fifo->num_s = custom_data.length;
	fifo->play_length_us = get_fifo_play_length_us(fifo,
			chip->custom_effect->t_lra_us);

	if (chip->play.in_calibration) {
		dev_err(chip->dev, "calibration in progress, ignore playing custom effect\n");
		rc = -EBUSY;
		goto cleanup;
	}

	play->effect = chip->custom_effect;
	play->brake = NULL;
	play->vmax_mv = (magnitude * chip->custom_effect->vmax_mv) / 0x7fff;
	rc = haptics_set_vmax_mv(chip, play->vmax_mv);
	if (rc < 0)
		goto cleanup;

	/* Toggle HAPTICS_EN for a clear start point of FIFO playing */
	if (chip->wa_flags & TOGGLE_EN_TO_FLUSH_FIFO) {
		rc = haptics_toggle_module_enable(chip);
		if (rc < 0)
			goto cleanup;
	}

	rc = haptics_enable_autores(chip, !play->effect->auto_res_disable);
	if (rc < 0)
		goto cleanup;

	play->pattern_src = FIFO;
	rc = haptics_set_fifo(chip, play->effect->fifo);
	if (rc < 0)
		goto cleanup;

	mutex_unlock(&chip->play.lock);
	return 0;
cleanup:
	kvfree(fifo->samples);
	fifo->samples = NULL;
unlock:
	mutex_unlock(&chip->play.lock);
	return rc;
}

static u32 get_play_length_effect_us(struct haptics_effect *effect)
{
	u32 length_us = 0;

	if (effect->brake)
		length_us = effect->brake->play_length_us;

	if ((effect->src == PATTERN1 || effect->src == PATTERN2)
			&& effect->pattern)
		length_us += effect->pattern->play_length_us;
	else if (effect->src == FIFO && effect->fifo)
		length_us += effect->fifo->play_length_us;

	return length_us;
}

static inline u32 get_play_length_us(struct haptics_play_info *play)
{
	return get_play_length_effect_us(play->effect);
}

#define PRIMITIVE_EFFECT_ID_BIT		BIT(15)
#define PRIMITIVE_EFFECT_ID_MASK	GENMASK(14, 0)
static int haptics_load_periodic_effect(struct haptics_chip *chip,
			s16 __user *data, u32 length, s16 magnitude)
{
	struct haptics_play_info *play = &chip->play;
	s16 custom_data[CUSTOM_DATA_LEN] = { 0 };
	struct haptics_effect *effects = NULL;
	s16 custom_id = 0;
	int effects_count = 0;
	int rc, i;
	bool primitive;

	if (copy_from_user(custom_data, data, sizeof(custom_data)))
		return -EFAULT;

	primitive = !!(custom_data[CUSTOM_DATA_EFFECT_IDX] & PRIMITIVE_EFFECT_ID_BIT);

	if (primitive) {
		if (chip->primitives_count == 0)
			return -EINVAL;

		custom_id = custom_data[CUSTOM_DATA_EFFECT_IDX] & PRIMITIVE_EFFECT_ID_MASK;
		effects = chip->primitives;
		effects_count = chip->primitives_count;
	} else {
		if (chip->effects_count == 0)
			return -EINVAL;

		custom_id = custom_data[CUSTOM_DATA_EFFECT_IDX];
		effects = chip->effects;
		effects_count = chip->effects_count;
	}

	for (i = 0; i < effects_count; i++)
		if (effects[i].id == custom_id)
			break;

	if (i == effects_count) {
		dev_err(chip->dev, "effect%d is not supported!\n",
				custom_data[CUSTOM_DATA_EFFECT_IDX]);
		return -EINVAL;
	}

	mutex_lock(&chip->play.lock);
	if (chip->play.in_calibration) {
		dev_err(chip->dev, "calibration in progress, ignore playing predefined effect\n");
		rc = -EBUSY;
		goto unlock;
	}

	play->vmax_mv = (magnitude * effects[i].vmax_mv) / 0x7fff;
	dev_dbg(chip->dev, "upload %s effect %d, vmax=%d\n", primitive ? "primitive" : "predefined",
			effects[i].id, play->vmax_mv);

	rc = haptics_load_predefined_effect(chip, &effects[i]);
	if (rc < 0) {
		dev_err(chip->dev, "Play predefined effect%d failed, rc=%d\n",
				effects[i].id, rc);
		goto unlock;
	}
	mutex_unlock(&chip->play.lock);

	play->length_us = get_play_length_us(play);
	custom_data[CUSTOM_DATA_TIMEOUT_SEC_IDX] =
		play->length_us / USEC_PER_SEC;
	custom_data[CUSTOM_DATA_TIMEOUT_MSEC_IDX] =
		(play->length_us % USEC_PER_SEC) / USEC_PER_MSEC;

	if (copy_to_user(data, custom_data, length))
		return -EFAULT;

	return 0;
unlock:
	mutex_unlock(&chip->play.lock);
	return rc;
}

static u8 get_direct_play_max_amplitude(struct haptics_chip *chip)
{
	u32 amplitude = DIRECT_PLAY_MAX_AMPLITUDE, hdrm_mv;
	int rc;

	if (is_haptics_external_powered(chip)) {
		rc = haptics_get_vmax_headroom_mv(chip, &hdrm_mv);
		if (rc < 0)
			return 0;

		amplitude *= chip->config.vmax_mv;
		amplitude /= (chip->hpwr_voltage_mv - hdrm_mv);
		if (amplitude > DIRECT_PLAY_MAX_AMPLITUDE)
			amplitude = DIRECT_PLAY_MAX_AMPLITUDE;
	}

	dev_dbg(chip->dev, "max amplitude for direct play: %#x\n", amplitude);
	return (u8)amplitude;
}

static int haptics_stop_fifo_play(struct haptics_chip *chip)
{
	int rc;
	u8 val;

	if (atomic_read(&chip->play.fifo_status.is_busy) == 0) {
		dev_dbg(chip->dev, "FIFO playing is not in progress\n");
		return 0;
	}

	rc = haptics_enable_play(chip, false);
	if (rc < 0)
		return rc;

	/* restore FIFO play rate back to T_LRA */
	rc = haptics_set_fifo_playrate(chip, T_LRA);
	if (rc < 0)
		return rc;

	haptics_fifo_empty_irq_config(chip, false);
	kvfree(chip->custom_effect->fifo->samples);
	chip->custom_effect->fifo->samples = NULL;

	atomic_set(&chip->play.fifo_status.is_busy, 0);

	/*
	 * All other playing modes would use AUTO mode RC
	 * calibration except FIFO streaming mode, so restore
	 * back to AUTO RC calibration after FIFO playing.
	 */
	val = CAL_RC_CLK_AUTO_VAL << CAL_RC_CLK_SHIFT;
	rc = haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_CAL_EN_REG, CAL_RC_CLK_MASK, val);
	if (rc < 0)
		return rc;

	dev_dbg(chip->dev, "stopped FIFO playing successfully\n");
	return 0;
}

static void haptics_stop_constant_effect_play(struct work_struct *work)
{
	struct haptics_chip *chip = container_of(work, struct haptics_chip, stop_work.work);
	int rc = 0;

	rc = haptics_enable_play(chip, false);
	if (rc < 0)
		dev_err(chip->dev, "stop constant effect play failed\n");

	rc = haptics_enable_hpwr_vreg(chip, false);
	if (rc < 0)
		dev_err(chip->dev, "disable hpwr_vreg failed\n");
}

static int haptics_upload_effect(struct input_dev *dev,
		struct ff_effect *effect, struct ff_effect *old)
{
	struct haptics_chip *chip = input_get_drvdata(dev);
	u32 length_ms, tmp;
	s16 level;
	u8 amplitude;
	int rc = 0;

	switch (effect->type) {
	case FF_CONSTANT:
		length_ms = effect->replay.length;
		level = effect->u.constant.level;
		tmp = get_direct_play_max_amplitude(chip);
		tmp *= level;
		amplitude = tmp / 0x7fff;
		dev_dbg(chip->dev, "upload constant effect, length = %dms, amplitude = %#x\n",
				length_ms, amplitude);
		schedule_delayed_work(&chip->stop_work, msecs_to_jiffies(length_ms));
		haptics_load_constant_effect(chip, amplitude);
		if (rc < 0) {
			dev_err(chip->dev, "set direct play failed, rc=%d\n",
					rc);
			return rc;
		}

		break;
	case FF_PERIODIC:
		if (effect->u.periodic.waveform != FF_CUSTOM) {
			dev_err(chip->dev, "Only support custom waveforms\n");
			return -EINVAL;
		}

		if (effect->u.periodic.custom_len ==
				sizeof(struct custom_fifo_data)) {
			rc = haptics_load_custom_effect(chip,
					effect->u.periodic.custom_data,
					effect->u.periodic.custom_len,
					effect->u.periodic.magnitude);
			if (rc < 0) {
				dev_err(chip->dev, "Upload custom FIFO data failed rc=%d\n",
						rc);
				return rc;
			}
		} else if (effect->u.periodic.custom_len ==
				sizeof(s16) * CUSTOM_DATA_LEN) {
			rc = haptics_load_periodic_effect(chip,
					effect->u.periodic.custom_data,
					effect->u.periodic.custom_len,
					effect->u.periodic.magnitude);
			if (rc < 0) {
				dev_err(chip->dev, "Upload periodic effect failed rc=%d\n",
						rc);
				return rc;
			}
		}

		break;
	default:
		dev_err(chip->dev, "%d effect is not supported\n",
				effect->type);
		return -EINVAL;
	}

	rc = haptics_enable_hpwr_vreg(chip, true);
	if (rc < 0) {
		dev_err(chip->dev, "enable hpwr_vreg failed, rc=%d\n", rc);
		return rc;
	}

	rc = haptics_wait_hboost_ready(chip);
	if (rc < 0 && chip->play.pattern_src == FIFO) {
		/*
		 * Call haptics_stop_fifo_play(chip) explicitly if hBoost is
		 * not ready for the FIFO play. This drops current FIFO play
		 * but it restores the SW back to initial status so that the
		 * following FIFO play requests can still be served.
		 */
		dev_dbg(chip->dev, "stop FIFO play explicitly to restore SW status\n");
		mutex_lock(&chip->play.lock);
		haptics_stop_fifo_play(chip);
		mutex_unlock(&chip->play.lock);
		return rc;
	}

	return 0;
}

static int haptics_playback(struct input_dev *dev, int effect_id, int val)
{
	struct haptics_chip *chip = input_get_drvdata(dev);

	dev_dbg(chip->dev, "playback val = %d\n", val);
	if (!!val)
		return haptics_enable_play(chip, true);

	return 0;
}

static int haptics_erase(struct input_dev *dev, int effect_id)
{
	struct haptics_chip *chip = input_get_drvdata(dev);
	struct haptics_play_info *play = &chip->play;
	int rc;

	dev_dbg(chip->dev, "erase effect, really stop play\n");
	mutex_lock(&play->lock);
	cancel_delayed_work_sync(&chip->stop_work);
	if ((play->pattern_src == FIFO) &&
			atomic_read(&play->fifo_status.is_busy)) {
		if (atomic_read(&play->fifo_status.written_done) == 0) {
			dev_dbg(chip->dev, "cancelling FIFO playing\n");
			atomic_set(&play->fifo_status.cancelled, 1);
		}

		rc = haptics_stop_fifo_play(chip);
		if (rc < 0) {
			dev_err(chip->dev, "stop FIFO playing failed, rc=%d\n",
					rc);
			mutex_unlock(&play->lock);
			return rc;
		}
	} else {
		rc = haptics_enable_play(chip, false);
		if (rc < 0) {
			dev_err(chip->dev, "stop play failed, rc=%d\n", rc);
			mutex_unlock(&play->lock);
			return rc;
		}
	}
	mutex_unlock(&play->lock);

	rc = haptics_enable_hpwr_vreg(chip, false);
	if (rc < 0)
		dev_err(chip->dev, "disable hpwr_vreg failed, rc=%d\n", rc);

	return rc;
}

static void haptics_set_gain_work(struct work_struct *work)
{
	struct haptics_chip *chip =
		container_of(work, struct haptics_chip, set_gain_work);
	struct haptics_hw_config *config = &chip->config;
	struct haptics_play_info *play = &chip->play;
	u32 vmax_mv, amplitude;
	u16 gain;

	mutex_lock(&play->lock);
	gain = atomic_read(&play->gain);
	/* scale amplitude when playing in DIRECT_PLAY mode */
	if (chip->play.pattern_src == DIRECT_PLAY) {
		amplitude = get_direct_play_max_amplitude(chip);
		amplitude *= gain;
		amplitude /= 0x7fff;

		dev_dbg(chip->dev, "Set amplitude: %#x\n", amplitude);
		haptics_set_direct_play(chip, (u8)amplitude);
		mutex_unlock(&play->lock);
		return;
	}

	/* scale Vmax when playing in other modes */
	vmax_mv = config->vmax_mv;
	if (play->effect)
		vmax_mv = play->effect->vmax_mv;

	if (chip->clamp_at_5v && (vmax_mv > CLAMPED_VMAX_MV))
		vmax_mv = CLAMPED_VMAX_MV;

	play->vmax_mv = ((u32)(gain * vmax_mv)) / 0x7fff;
	haptics_set_vmax_mv(chip, play->vmax_mv);
	mutex_unlock(&play->lock);
}

static void haptics_set_gain(struct input_dev *dev, u16 gain)
{
	struct haptics_chip *chip = input_get_drvdata(dev);
	struct haptics_play_info *play = &chip->play;

	if (gain == 0)
		return;

	if (gain > 0x7fff)
		gain = 0x7fff;

	atomic_set(&play->gain, gain);
	schedule_work(&chip->set_gain_work);
	dev_dbg(chip->dev, "Set gain: %#x\n", gain);
}

static int haptics_store_cl_brake_settings(struct haptics_chip *chip)
{
	int rc = 0;
	u8 val[2];

	if (!chip->cl_brake_nvmem)
		return 0;

	rc = haptics_get_status_data(chip, RNAT_RCAL_INT, val);
	if (rc < 0)
		return rc;

	rc = nvmem_cell_write(chip->cl_brake_nvmem, &val[1], 1);
	if (rc < 0)
		dev_err(chip->dev, "store RNAT/RCAL to SDAM failed, rc=%d\n", rc);

	return rc;
}

static int haptics_config_openloop_lra_period(struct haptics_chip *chip,
							u32 t_lra_us)
{
	u32 tmp;
	u8 val[2];
	int rc;

	rc = haptics_adjust_lra_period(chip, &t_lra_us);
	if (rc < 0)
		return rc;

	tmp = t_lra_us / TLRA_STEP_US;
	val[0] = (tmp >> 8) & TLRA_OL_MSB_MASK;
	val[1] = tmp & TLRA_OL_LSB_MASK;

	return haptics_write(chip, chip->cfg_addr_base,
			HAP_CFG_TLRA_OL_HIGH_REG, val, 2);
}

static int haptics_config_preload_fifo_effect(struct haptics_chip *chip,
				struct haptics_effect *effect)
{
	int rc;

	if (!effect->fifo->preload) {
		dev_err(chip->dev, "effect %d doesn't support preload\n",
				effect->id);
		return -EINVAL;
	}

	if (effect->src != PATTERN_MEM) {
		dev_err(chip->dev, "effect %d pattern_src is not PATTERN_MEM\n",
				effect->id);
		return -EINVAL;
	}

	rc = haptics_update_pat_mem_samples(chip, effect->pat_sel,
			effect->fifo->samples, effect->fifo->num_s);
	if (!rc)
		dev_dbg(chip->dev, "effect %d is preloaded in PAT_MEM %d\n",
				effect->id, effect->pat_sel);
	return rc;
}

static int haptics_mmap_config(struct haptics_chip *chip)
{
	int rc, i, left;
	u8 val[4];

	/*
	 * Make the FIFO memory size 128-byte aligned, and append
	 * the leftover memory bytes to PAT1_MEM.
	 */
	left = chip->mmap.fifo_mmap.length % MMAP_FIFO_LEN_PER_LSB;
	if (left) {
		chip->mmap.fifo_mmap.length -= left;
		chip->mmap.pat_sel_mmap[PAT1_MEM].length += left;
	}

	/* config MMAP_FIFO */
	val[0] = chip->mmap.fifo_mmap.length / MMAP_FIFO_LEN_PER_LSB;
	if (val[0]-- == 0) {
		dev_err(chip->dev, "fifo length %d is less than %d\n",
				chip->mmap.fifo_mmap.length,
				MMAP_FIFO_LEN_PER_LSB);
		return -EINVAL;
	}

	val[0] &= MMAP_FIFO_LEN_MASK;
	val[0] |= MMAP_FIFO_EXIST_BIT;
	rc = haptics_write(chip, chip->ptn_addr_base,
			HAP_PTN_MMAP_FIFO_REG, val, 1);
	if (rc < 0)
		return rc;

	/* config MMAP_PAT1/2/3/4 */
	for (i = PAT1_MEM; i <= PAT4_MEM; i++)
		val[i] = min(chip->mmap.pat_sel_mmap[i].length, chip->mmap.pat_sel_mmap[i].max_size)
			/ MMAP_PAT_LEN_PER_LSB;

	rc = haptics_write(chip, chip->ptn_addr_base,
			HAP_PTN_MMAP_PAT1_REG, val, 4);
	if (rc < 0)
		return rc;

	/* config PATx_PLAY_RATE */
	rc = haptics_masked_write(chip, chip->ptn_addr_base,
			HAP_PTN_FIFO_PLAY_RATE_REG, PAT_MEM_PLAY_RATE_MASK,
			FIELD_PREP(PAT_MEM_PLAY_RATE_MASK, chip->mmap.pat_play_rate));
	if (rc < 0) {
		dev_err(chip->dev, "Set pat_mem play rate failed, rc=%d\n", rc);
		return rc;
	}

	dev_dbg(chip->dev, "haptics memory map configured with FIFO: %d, PAT1:%d, PAT2: %d, PAT3: %d, PAT4: %d\n",
			chip->mmap.fifo_mmap.length,
			chip->mmap.pat_sel_mmap[PAT1_MEM].length,
			chip->mmap.pat_sel_mmap[PAT2_MEM].length,
			chip->mmap.pat_sel_mmap[PAT3_MEM].length,
			chip->mmap.pat_sel_mmap[PAT4_MEM].length);
	return 0;
}

static int haptics_mmap_preload_fifo_effect(struct haptics_chip *chip,
				struct haptics_effect *effect)
{
	int i;
	u32 length, fifo_length;

	if (!effect->fifo->preload) {
		dev_err(chip->dev, "effect %d doesn't support preload\n",
			effect->id);
		return -EINVAL;
	}

	if (chip->mmap.pat_play_rate == F_RESERVED) {
		chip->mmap.pat_play_rate = effect->fifo->period_per_s;
	} else if (chip->mmap.pat_play_rate != effect->fifo->period_per_s) {
		dev_warn(chip->dev, "PATx_MEM sources only support one play rate\n");
		goto mmap_failed;
	}

	length = effect->fifo->num_s;
	if (length % MMAP_PAT_LEN_PER_LSB)
		length = (length / MMAP_PAT_LEN_PER_LSB + 1) * MMAP_PAT_LEN_PER_LSB;

	fifo_length = chip->mmap.fifo_mmap.length - length;
	if (fifo_length < MMAP_FIFO_MIN_SIZE) {
		dev_warn(chip->dev, "not enough space for preload FIFO effect\n");
		goto mmap_failed;
	}

	for (i = PAT4_MEM; i >= PAT1_MEM; i--) {
		if (!chip->mmap.pat_sel_mmap[i].in_use &&
				(length <= chip->mmap.pat_sel_mmap[i].max_size))
			break;
	}

	if (i < PAT1_MEM) {
		dev_warn(chip->dev, "no PAT_MEM source available\n");
		effect->fifo->preload = false;
		goto mmap_failed;
	}

	/* update the mmap configuration */
	chip->mmap.pat_sel_mmap[i].in_use = true;
	chip->mmap.pat_sel_mmap[i].length = length;
	chip->mmap.fifo_mmap.length = fifo_length;

	/* Update the effect pattern_src and pat_sel for the preload effect */
	effect->src = PATTERN_MEM;
	effect->pat_sel = i;
	return 0;

	/* if mmap is failed then the effect couldn't be preloaded */
mmap_failed:
	effect->fifo->preload = false;
	return -ENOSPC;
}

static void haptics_mmap_init(struct haptics_chip *chip)
{
	u32 max_pat_mem_size = MMAP_NUM_BYTES - MMAP_FIFO_MIN_SIZE;

	/* Assume that all memory space is used for FIFO mode by default */
	chip->mmap.fifo_mmap.in_use = true;
	chip->mmap.fifo_mmap.length = MMAP_NUM_BYTES;
	chip->mmap.fifo_mmap.max_size = MMAP_NUM_BYTES;

	/* different PATx_MEM partition has different maximum size */
	chip->mmap.pat_sel_mmap[PAT1_MEM].max_size = min(max_pat_mem_size,
			(u32)MMAP_PAT1_LEN_MASK * MMAP_PAT_LEN_PER_LSB);
	chip->mmap.pat_sel_mmap[PAT2_MEM].max_size = min(max_pat_mem_size,
			(u32)MMAP_PAT2_LEN_MASK * MMAP_PAT_LEN_PER_LSB);
	chip->mmap.pat_sel_mmap[PAT3_MEM].max_size = min(max_pat_mem_size,
			(u32)MMAP_PAT3_PAT4_LEN_MASK * MMAP_PAT_LEN_PER_LSB);
	chip->mmap.pat_sel_mmap[PAT4_MEM].max_size = min(max_pat_mem_size,
			(u32)MMAP_PAT3_PAT4_LEN_MASK * MMAP_PAT_LEN_PER_LSB);
	chip->mmap.pat_play_rate = F_RESERVED;
}

static int haptics_init_fifo_memory(struct haptics_chip *chip)
{
	struct haptics_effect *effect;
	int rc, i;

	if (chip->hw_type != HAP525_HV) {
		dev_dbg(chip->dev, "HW type %d doesn't support mmap\n",
				chip->hw_type);
		return 0;
	}

	haptics_mmap_init(chip);

	for (i = 0; i < chip->effects_count; i++) {
		effect = &chip->effects[i];
		if (!effect->fifo || !effect->fifo->preload)
			continue;

		rc = haptics_mmap_preload_fifo_effect(chip, effect);
		if (rc < 0 && rc != -ENOSPC)
			return rc;
	}

	rc = haptics_mmap_config(chip);
	if (rc < 0)
		return rc;

	for (i = 0; i < chip->effects_count; i++) {
		effect = &chip->effects[i];
		if (!effect->fifo || !effect->fifo->preload)
			continue;

		rc = haptics_config_preload_fifo_effect(chip, effect);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int haptics_init_preload_pattern_effect(struct haptics_chip *chip)
{
	struct haptics_hw_config *config = &chip->config;
	struct haptics_effect *effect;
	int i;

	if (config->preload_effect == -EINVAL)
		return 0;

	for (i = 0; i < chip->effects_count; i++)
		if (chip->effects[i].id == config->preload_effect)
			break;

	if (i == chip->effects_count) {
		dev_err(chip->dev, "preload effect %d is not found\n",
				config->preload_effect);
		return -EINVAL;
	}

	effect = &chip->effects[i];
	return haptics_set_pattern(chip, effect->pattern, effect->src);
}

static int haptics_init_lra_period_config(struct haptics_chip *chip)
{
	int rc = 0;
	u8 val;
	u32 t_lra_us;

	/* set AUTO_mode RC CLK calibration by default */
	val = FIELD_PREP(CAL_RC_CLK_MASK, CAL_RC_CLK_AUTO_VAL);
	rc = haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_CAL_EN_REG, CAL_RC_CLK_MASK, val);
	if (rc < 0)
		return rc;

	/* get calibrated close loop period */
	t_lra_us = chip->config.t_lra_us;
	rc = haptics_get_closeloop_lra_period(chip, true);
	if (!rc && chip->config.cl_t_lra_us != 0)
		t_lra_us = chip->config.cl_t_lra_us;
	else
		dev_warn(chip->dev, "get closeloop LRA period failed, rc=%d\n", rc);

	/* Config T_LRA */
	return haptics_config_openloop_lra_period(chip, t_lra_us);
}

static int haptics_init_hpwr_config(struct haptics_chip *chip)
{
	int rc;
	u8 val;

	if ((chip->hw_type == HAP520_MV) && !chip->hpwr_vreg) {
		/* Indicates if HPWR is BOB or Bharger */
		rc = haptics_read(chip, chip->cfg_addr_base,
			HAP_CFG_HPWR_INTF_CTL_REG, &val, 1);
		if (rc < 0)
			return rc;

		chip->hpwr_intf_ctl = val & INTF_CTL_MASK;

		/* Read HPWR voltage to adjust the VMAX */
		if (chip->hpwr_voltage_mv == 0 &&
				chip->hpwr_intf_ctl == INTF_CTL_BOB) {
			rc = haptics_read(chip, chip->cfg_addr_base,
				HAP_CFG_VHPWR_REG, &val, 1);
			if (rc < 0)
				return rc;

			chip->hpwr_voltage_mv = val * VHPWR_STEP_MV;
		}
	}

	/* Force VREG_RDY if non-HBoost is used for powering haptics */
	if (is_haptics_external_powered(chip))
		return haptics_masked_write(chip, chip->cfg_addr_base,
				HAP_CFG_VSET_CFG_REG, FORCE_VREG_RDY_BIT,
				FORCE_VREG_RDY_BIT);

	return 0;
}

static int haptics_init_drive_config(struct haptics_chip *chip)
{
	struct haptics_hw_config *config = &chip->config;
	int rc;
	u8 val;

	/* Config driver waveform shape and use 2's complement data format */
	rc = haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_DRV_WF_SEL_REG,
			DRV_WF_SEL_MASK | DRV_WF_FMT_BIT, config->drv_wf);
	if (rc < 0)
		return rc;

	/* Config brake mode and waveform shape */
	val = FIELD_PREP(BRAKE_MODE_MASK, config->brake.mode);
	val |= FIELD_PREP(BRAKE_SINE_GAIN_MASK, config->brake.sine_gain);
	val |= FIELD_PREP(BRAKE_WF_SEL_MASK, config->brake.brake_wf);

	return haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_BRAKE_MODE_CFG_REG,
			BRAKE_MODE_MASK | BRAKE_SINE_GAIN_MASK
			| BRAKE_WF_SEL_MASK, val);
}

static int haptics_init_vmax_config(struct haptics_chip *chip)
{
	int rc;
	u8 val;

	if (!is_haptics_external_powered(chip)) {
		rc = haptics_read(chip, chip->hbst_addr_base,
				HAP_BOOST_CLAMP_REG, &val, 1);
		if (rc < 0)
			return rc;

		chip->clamp_at_5v = val & CLAMP_5V_BIT;
	}

	chip->is_hv_haptics = true;
	chip->max_vmax_mv = MAX_VMAX_MV;
	if (chip->hw_type == HAP520_MV || chip->hw_type == HAP525_HV) {
		rc = haptics_read(chip, chip->cfg_addr_base,
			HAP_CFG_HW_CONFIG_REG, &val, 1);
		if (rc < 0)
			return rc;

		chip->is_hv_haptics = val & HV_HAP_DRIVER_BIT;
		chip->max_vmax_mv = (chip->is_hv_haptics) ?
					MAX_HV_VMAX_MV : MAX_MV_VMAX_MV;
	}

	/* Set the initial clamped vmax value when hBoost is used by charger firmware */
	chip->clamped_vmax_mv = MAX_HV_VMAX_MV;
	/* Config VMAX */
	return haptics_set_vmax_mv(chip, chip->config.vmax_mv);
}

static int haptics_config_wa(struct haptics_chip *chip)
{
	switch (chip->hw_type) {
	case HAP520:
		chip->wa_flags |= TOGGLE_CAL_RC_CLK | SW_CTRL_HBST | SLEEP_CLK_32K_SCALE |
			TOGGLE_EN_TO_FLUSH_FIFO | RECOVER_SWR_SLAVE;
		break;
	case HAP520_MV:
		break;
	case HAP525_HV:
		if (chip->hbst_revision == HAP_BOOST_V0P1)
			chip->wa_flags |= SW_CTRL_HBST;
		break;
	default:
		dev_err(chip->dev, "HW type %d does not match\n",
			chip->hw_type);
		return -EINVAL;
	}

	return 0;
}

static int haptics_hw_init(struct haptics_chip *chip)
{
	int rc;

	rc = haptics_config_wa(chip);
	if (rc < 0)
		return rc;

	rc = haptics_store_cl_brake_settings(chip);
	if (rc < 0)
		return rc;

	rc = haptics_init_vmax_config(chip);
	if (rc < 0)
		return rc;

	rc = haptics_init_drive_config(chip);
	if (rc < 0)
		return rc;

	rc = haptics_init_hpwr_config(chip);
	if (rc < 0)
		return rc;

	if (chip->config.is_erm)
		return 0;

	rc = haptics_init_lra_period_config(chip);
	if (rc < 0)
		return rc;

	rc = haptics_init_preload_pattern_effect(chip);
	if (rc < 0)
		return rc;

	return haptics_init_fifo_memory(chip);
}

static irqreturn_t fifo_empty_irq_handler(int irq, void *data)
{
	struct haptics_chip *chip = data;
	struct fifo_cfg *fifo;
	struct fifo_play_status *status;
	u32 samples_left;
	u8 *samples, val;
	int rc, num;

	rc = haptics_read(chip, chip->cfg_addr_base,
			HAP_CFG_INT_RT_STS_REG, &val, 1);
	if (rc < 0)
		return IRQ_HANDLED;

	if (!(val & FIFO_EMPTY_BIT))
		return IRQ_HANDLED;

	mutex_lock(&chip->play.lock);
	status = &chip->play.fifo_status;
	if (atomic_read(&status->written_done) == 1) {
		/*
		 * Check the FIFO real time fill status before stopping
		 * play to make sure that all FIFO samples can be played
		 * successfully. If there are still samples left in FIFO
		 * memory, defer the stop into erase() function.
		 */
		num = haptics_get_available_fifo_memory(chip);
		if (num != get_max_fifo_samples(chip)) {
			dev_dbg(chip->dev, "%d FIFO samples still in playing\n",
					get_max_fifo_samples(chip) - num);
			goto unlock;
		}

		rc = haptics_stop_fifo_play(chip);
		if (rc < 0)
			goto unlock;

		dev_dbg(chip->dev, "FIFO playing is done\n");
	} else {
		if (atomic_read(&status->cancelled) == 1) {
			dev_dbg(chip->dev, "FIFO programming got cancelled\n");
			goto unlock;
		}

		if (!chip->play.effect)
			goto unlock;

		fifo = chip->play.effect->fifo;
		if (!fifo || !fifo->samples) {
			dev_err(chip->dev, "no FIFO samples available\n");
			goto unlock;
		}

		samples_left = fifo->num_s - status->samples_written;
		num = haptics_get_available_fifo_memory(chip);
		if (num < 0)
			goto unlock;

		samples = fifo->samples + status->samples_written;

		/*
		 * Always use 4-byte burst write in the middle of FIFO programming to
		 * avoid HW padding zeros during 1-byte write which would cause the HW
		 * stop driving for the unexpected padding zeros.
		 */
		if (num < samples_left)
			num = round_down(num, HAP_PTN_FIFO_DIN_NUM);
		else
			num = samples_left;

		rc = haptics_update_fifo_samples(chip, samples, num, true);
		if (rc < 0) {
			dev_err(chip->dev, "Update FIFO samples failed, rc=%d\n",
					rc);
			goto unlock;
		}

		status->samples_written += num;
		trace_qcom_haptics_fifo_prgm_status(fifo->num_s, status->samples_written, num);
		if (status->samples_written == fifo->num_s) {
			dev_dbg(chip->dev, "FIFO programming is done\n");
			atomic_set(&chip->play.fifo_status.written_done, 1);
			haptics_set_fifo_empty_threshold(chip, 0);
		}
	}

unlock:
	mutex_unlock(&chip->play.lock);
	return IRQ_HANDLED;
}

#ifdef CONFIG_DEBUG_FS
static int vmax_dbgfs_read(void *data, u64 *val)
{
	struct haptics_effect *effect = data;

	*val = effect->vmax_mv;

	return 0;
}

static int vmax_dbgfs_write(void *data, u64 val)
{
	struct haptics_effect *effect = data;

	if (val > MAX_VMAX_MV)
		val = MAX_VMAX_MV;

	effect->vmax_mv = (u32) val;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(vmax_debugfs_ops, vmax_dbgfs_read,
		vmax_dbgfs_write, "%llu\n");

static int auto_res_en_dbgfs_read(void *data, u64 *val)
{
	struct haptics_effect *effect = data;

	*val = !effect->auto_res_disable;

	return 0;
}

static int auto_res_en_dbgfs_write(void *data, u64 val)
{
	struct haptics_effect *effect = data;

	effect->auto_res_disable = !val;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(auto_res_en_debugfs_ops,  auto_res_en_dbgfs_read,
		auto_res_en_dbgfs_write, "%llu\n");

static ssize_t pattern_s_dbgfs_read(struct file *fp, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	u32 pos = 0, size = CHAR_PER_PATTERN_S * SAMPLES_PER_PATTERN;
	char *str;
	int i = 0, rc;

	if (!effect->pattern)
		return 0;

	str = kzalloc(size, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	for (i = 0; i < SAMPLES_PER_PATTERN; i++) {
		pos += scnprintf(str + pos, size - pos, "0x%03x  ",
				effect->pattern->samples[i].amplitude);
		pos += scnprintf(str + pos, size - pos, "%s(0x%02x)  ",
				period_str[effect->pattern->samples[i].period],
				effect->pattern->samples[i].period);
		pos += scnprintf(str + pos, size - pos, "F_LRA_X2(%1d)\n",
				 effect->pattern->samples[i].f_lra_x2);
	}

	rc = simple_read_from_buffer(buf, count, ppos, str, pos);
	kfree(str);

	return rc;
}

static ssize_t pattern_s_dbgfs_write(struct file *fp,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	struct pattern_s patterns[SAMPLES_PER_PATTERN] = {{0, 0, 0},};
	char *str, *kbuf, *token;
	u32 val, tmp[3 * SAMPLES_PER_PATTERN] = {0};
	int rc, i = 0, j = 0;

	if (count > CHAR_PER_PATTERN_S * SAMPLES_PER_PATTERN)
		return -EINVAL;

	kbuf = kzalloc(CHAR_PER_PATTERN_S * SAMPLES_PER_PATTERN + 1,
						GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	str = kbuf;
	rc = copy_from_user(str, buf, count);
	if (rc > 0) {
		rc = -EFAULT;
		goto exit;
	}

	str[count] = '\0';
	*ppos += count;

	while ((token = strsep((char **)&str, " ")) != NULL) {
		rc = kstrtouint(token, 0, &val);
		if (rc < 0) {
			rc = -EINVAL;
			goto exit;
		}

		tmp[i++] = val;
	}

	if (i % 3)
		pr_warn("Tuple should be having 3 elements, discarding tuple %d\n",
				i / 3);

	for (j = 0; j < i / 3; j++) {
		if (tmp[3 * j] > 0x1ff || tmp[3 * j + 1] > T_LRA_X_8 ||
				tmp[3 * j + 2] > 1) {
			pr_err("allowed tuples: [amplitude(<= 0x1ff) period(<=6(T_LRA_X_8)) f_lra_x2(0,1)]\n");
			rc = -EINVAL;
			goto exit;
		}

		patterns[j].amplitude = (u16)tmp[3 * j];
		patterns[j].period = (enum s_period)tmp[3 * j + 1];
		patterns[j].f_lra_x2 = !!tmp[3 * j + 2];
	}

	memcpy(effect->pattern->samples, patterns,
			sizeof(effect->pattern->samples));

	/* recalculate the play length */
	effect->pattern->play_length_us =
		get_pattern_play_length_us(effect->pattern);
	if (effect->pattern->play_length_us == -EINVAL) {
		pr_err("get pattern play length failed\n");
		rc = -EINVAL;
		goto exit;
	}

	rc = count;
exit:
	kfree(kbuf);
	return rc;
}

static const struct file_operations pattern_s_dbgfs_ops = {
	.read = pattern_s_dbgfs_read,
	.write = pattern_s_dbgfs_write,
	.open = simple_open,
};

static int pattern_play_rate_us_dbgfs_read(void *data, u64 *val)
{
	struct haptics_effect *effect = data;

	*val = effect->pattern->play_rate_us;

	return 0;
}

static int pattern_play_rate_us_dbgfs_write(void *data, u64 val)
{
	struct haptics_effect *effect = data;

	if (val > TLRA_MAX_US)
		val = TLRA_MAX_US;

	effect->pattern->play_rate_us = (u32)val;
	/* recalculate the play length */
	effect->pattern->play_length_us =
		get_pattern_play_length_us(effect->pattern);
	if (effect->pattern->play_length_us == -EINVAL) {
		pr_err("get pattern play length failed\n");
		return -EINVAL;
	}

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(pattern_play_rate_dbgfs_ops,
		pattern_play_rate_us_dbgfs_read,
		pattern_play_rate_us_dbgfs_write, "%llu\n");

static ssize_t fifo_s_dbgfs_read(struct file *fp,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	struct fifo_cfg *fifo = effect->fifo;
	char *kbuf;
	int rc, i;
	u32 size, pos = 0;

	size = CHAR_PER_SAMPLE * fifo->num_s + 1;
	kbuf = kzalloc(size, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	for (i = 0; i < fifo->num_s; i++)
		pos += scnprintf(kbuf + pos, size - pos,
				"%d ", (s8)fifo->samples[i]);

	pos += scnprintf(kbuf + pos, size - pos, "%s", "\n");
	rc = simple_read_from_buffer(buf, count, ppos, kbuf, pos);
	kfree(kbuf);

	return rc;
}

static ssize_t fifo_s_dbgfs_write(struct file *fp,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	struct fifo_cfg *fifo = effect->fifo;
	char *str, *kbuf, *token;
	int rc, i = 0;
	int val;
	u8 *samples;

	kbuf = kzalloc(count + 1, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	str = kbuf;
	rc = copy_from_user(str, buf, count);
	if (rc > 0) {
		rc = -EFAULT;
		goto exit;
	}

	str[count] = '\0';
	*ppos += count;

	samples = kcalloc(fifo->num_s, sizeof(*samples), GFP_KERNEL);
	if (!samples) {
		rc = -ENOMEM;
		goto exit;
	}

	while ((token = strsep(&str, " ")) != NULL) {
		rc = kstrtoint(token, 0, &val);
		if (rc < 0) {
			rc = -EINVAL;
			goto exit2;
		}

		if (val > 0xff)
			val = 0xff;

		samples[i++] = (u8)val;
		/* only support fifo pattern no longer than before */
		if (i >= fifo->num_s)
			break;
	}

	memcpy(fifo->samples, samples, fifo->num_s);
	fifo->play_length_us = get_fifo_play_length_us(fifo, effect->t_lra_us);
	if (fifo->play_length_us == -EINVAL) {
		pr_err("get fifo play length failed\n");
		rc = -EINVAL;
		goto exit2;
	}

	rc = count;
exit2:
	kfree(samples);
exit:
	kfree(kbuf);
	return rc;
}

static const struct file_operations fifo_s_dbgfs_ops = {
	.read = fifo_s_dbgfs_read,
	.write = fifo_s_dbgfs_write,
	.owner = THIS_MODULE,
	.open = simple_open,
};

static int fifo_period_dbgfs_read(void *data, u64 *val)
{
	struct haptics_effect *effect = data;

	*val = effect->fifo->period_per_s;

	return 0;
}

static int fifo_period_dbgfs_write(void *data, u64 val)
{
	struct haptics_effect *effect = data;
	struct fifo_cfg *fifo = effect->fifo;

	if (val > F_48KHZ)
		return -EINVAL;

	fifo->period_per_s = (enum s_period)val;
	fifo->play_length_us = get_fifo_play_length_us(fifo, effect->t_lra_us);
	if (fifo->play_length_us == -EINVAL) {
		pr_err("get fifo play length failed\n");
		return -EINVAL;
	}

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fifo_period_dbgfs_ops,
		fifo_period_dbgfs_read,
		fifo_period_dbgfs_write, "%llu\n");

static ssize_t brake_s_dbgfs_read(struct file *fp,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	struct brake_cfg *brake = effect->brake;
	char *str;
	int rc, i;
	u32 size, pos = 0;

	size = CHAR_PER_SAMPLE * BRAKE_SAMPLE_COUNT + 1;
	str = kzalloc(size, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	for (i = 0; i < BRAKE_SAMPLE_COUNT; i++)
		pos += scnprintf(str + pos, size - pos, "0x%02x ",
				brake->samples[i]);

	pos += scnprintf(str + pos, size - pos, "%s", "\n");
	rc = simple_read_from_buffer(buf, count, ppos, str, pos);
	kfree(str);

	return rc;
}

static ssize_t brake_s_dbgfs_write(struct file *fp,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	struct brake_cfg *brake = effect->brake;
	char *str, *kbuf, *token;
	int rc, i = 0;
	u32 val;
	u8 samples[BRAKE_SAMPLE_COUNT] = {0};

	if (count > CHAR_PER_SAMPLE * BRAKE_SAMPLE_COUNT)
		return -EINVAL;

	kbuf = kzalloc(CHAR_PER_SAMPLE * BRAKE_SAMPLE_COUNT + 1, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	str = kbuf;
	rc = copy_from_user(str, buf, count);
	if (rc > 0) {
		rc = -EFAULT;
		goto exit;
	}

	str[count] = '\0';
	*ppos += count;

	while ((token = strsep((char **)&str, " ")) != NULL) {
		rc = kstrtouint(token, 0, &val);
		if (rc < 0) {
			rc = -EINVAL;
			goto exit;
		}

		if (val > 0xff)
			val = 0xff;

		samples[i++] = (u8)val;
		if (i >= BRAKE_SAMPLE_COUNT)
			break;
	}

	memcpy(brake->samples, samples, BRAKE_SAMPLE_COUNT);
	verify_brake_samples(brake);
	brake->play_length_us =
		get_brake_play_length_us(brake, effect->t_lra_us);

	rc = count;
exit:
	kfree(kbuf);
	return rc;
}

static const struct file_operations brake_s_dbgfs_ops = {
	.read = brake_s_dbgfs_read,
	.write = brake_s_dbgfs_write,
	.open = simple_open,
};

static ssize_t brake_mode_dbgfs_read(struct file *fp,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	struct brake_cfg *brake = effect->brake;
	char str[CHAR_BRAKE_MODE] = {0};
	u32 size;
	int rc;

	size = scnprintf(str, ARRAY_SIZE(str), "%s\n", brake_str[brake->mode]);
	rc = simple_read_from_buffer(buf, count, ppos, str, size);

	return rc;
}

static ssize_t brake_mode_dbgfs_write(struct file *fp,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	struct brake_cfg *brake = effect->brake;
	char *kbuf;
	int rc;

	kbuf = kzalloc(count + 1, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	rc = copy_from_user(kbuf, buf, count);
	if (rc > 0) {
		rc = -EFAULT;
		goto exit;
	}

	kbuf[count] = '\0';
	*ppos += count;
	rc = count;
	if (strcmp(kbuf, "open-loop") == 0) {
		brake->mode = OL_BRAKE;
	} else if (strcmp(kbuf, "close-loop") == 0) {
		brake->mode = CL_BRAKE;
	} else if (strcmp(kbuf, "predictive") == 0) {
		brake->mode = PREDICT_BRAKE;
	} else if (strcmp(kbuf, "auto") == 0) {
		brake->mode = AUTO_BRAKE;
	} else {
		pr_err("%s brake mode is not supported\n", kbuf);
		rc = -EINVAL;
	}

exit:
	kfree(kbuf);
	return rc;
}

static const struct file_operations brake_mode_dbgfs_ops = {
	.read = brake_mode_dbgfs_read,
	.write = brake_mode_dbgfs_write,
	.open = simple_open,
};

static int brake_en_dbgfs_read(void *data, u64 *val)
{
	struct haptics_effect *effect = data;

	*val = !effect->brake->disabled;

	return 0;
}

static int brake_en_dbgfs_write(void *data, u64 val)
{
	struct haptics_effect *effect = data;

	effect->brake->disabled = !val;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(brake_en_dbgfs_ops,  brake_en_dbgfs_read,
		brake_en_dbgfs_write, "%llu\n");

static int brake_sine_gain_dbgfs_read(void *data, u64 *val)
{
	struct haptics_effect *effect = data;

	*val = effect->brake->sine_gain;

	return 0;
}

static int brake_sine_gain_dbgfs_write(void *data, u64 val)
{
	struct haptics_effect *effect = data;

	if (val > BRAKE_SINE_GAIN_X8)
		return -EINVAL;

	effect->brake->sine_gain = val;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(brake_sine_gain_dbgfs_ops,
		brake_sine_gain_dbgfs_read,
		brake_sine_gain_dbgfs_write, "%llu\n");

static int preload_effect_idx_dbgfs_read(void *data, u64 *val)
{
	struct haptics_chip *chip = data;

	*val = chip->config.preload_effect;

	return 0;
}

static int preload_effect_idx_dbgfs_write(void *data, u64 val)
{
	struct haptics_chip *chip = data;
	struct haptics_effect *new, *old;
	int rc, i;

	for (i = 0; i < chip->effects_count; i++)
		if (chip->effects[i].id == val)
			break;

	if (i == chip->effects_count)
		return -EINVAL;

	new = &chip->effects[i];

	for (i = 0; i < chip->effects_count; i++)
		if (chip->effects[i].id == chip->config.preload_effect)
			break;

	old = &chip->effects[i];

	chip->config.preload_effect = (u32)val;

	new->pattern->preload = true;
	new->src = PATTERN2;
	rc = haptics_set_pattern(chip, new->pattern, new->src);
	if (rc < 0)
		return rc;

	old->src = PATTERN1;
	old->pattern->preload = false;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(preload_effect_idx_dbgfs_ops,
		preload_effect_idx_dbgfs_read,
		preload_effect_idx_dbgfs_write, "%llu\n");

static int haptics_add_effects_debugfs(struct haptics_effect *effect,
		struct dentry *dir)
{
	struct dentry *file, *pattern_dir, *fifo_dir, *brake_dir;

	file = debugfs_create_file_unsafe("vmax_mv", 0644, dir,
			effect, &vmax_debugfs_ops);
	if (IS_ERR(file))
		return PTR_ERR(file);

	file = debugfs_create_file_unsafe("lra_auto_res_en", 0644, dir,
			effect, &auto_res_en_debugfs_ops);
	if (IS_ERR(file))
		return PTR_ERR(file);

	/* effect can have either pattern or FIFO */
	if (effect->pattern) {
		pattern_dir = debugfs_create_dir("pattern", dir);
		if (IS_ERR(pattern_dir))
			return PTR_ERR(pattern_dir);

		file = debugfs_create_file("samples", 0644, pattern_dir,
				effect, &pattern_s_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);

		file = debugfs_create_file_unsafe("play_rate_us", 0644,
				pattern_dir, effect,
				&pattern_play_rate_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);
	} else if (effect->fifo) {
		fifo_dir = debugfs_create_dir("fifo", dir);
		if (IS_ERR(fifo_dir))
			return PTR_ERR(fifo_dir);

		file = debugfs_create_file("samples", 0644, fifo_dir,
				effect, &fifo_s_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);

		file = debugfs_create_file_unsafe("period", 0644, fifo_dir,
				effect, &fifo_period_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);
	}

	if (effect->brake) {
		brake_dir = debugfs_create_dir("brake", dir);
		if (IS_ERR(brake_dir))
			return PTR_ERR(brake_dir);

		file = debugfs_create_file("samples", 0644, brake_dir,
				effect, &brake_s_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);

		file = debugfs_create_file("mode", 0644, brake_dir,
				effect, &brake_mode_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);

		file = debugfs_create_file_unsafe("enable", 0644, brake_dir,
				effect, &brake_en_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);

		file = debugfs_create_file_unsafe("sine_gain", 0644, brake_dir,
				effect, &brake_sine_gain_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);
	}

	return 0;
}

#define EFFECT_NAME_SIZE		15
static int haptics_add_debugfs(struct dentry *hap_dir, struct haptics_effect *effects,
			int count, char *effect_name)
{
	struct dentry *effect_dir;
	char str[EFFECT_NAME_SIZE] = {0};
	int rc = 0;
	int i = 0;

	for (; i < count; i++) {
		scnprintf(str, ARRAY_SIZE(str), "%s%d", effect_name, effects[i].id);
		effect_dir = debugfs_create_dir(str, hap_dir);
		if (IS_ERR(effect_dir)) {
			rc = PTR_ERR(effect_dir);
			pr_err("create %s debugfs directory failed, rc=%d\n", str, rc);
			return rc;
		}
		rc = haptics_add_effects_debugfs(&effects[i], effect_dir);
		if (rc < 0) {
			pr_err("create debugfs nodes for %s failed, rc=%d\n", str, rc);
			return rc;
		}
	}

	return rc;
}

static int haptics_create_debugfs(struct haptics_chip *chip)
{
	struct dentry *hap_dir, *file;
	int rc = 0;

	hap_dir = debugfs_create_dir("haptics", NULL);
	if (IS_ERR(hap_dir)) {
		rc = PTR_ERR(hap_dir);
		dev_err(chip->dev, "create haptics debugfs directory failed, rc=%d\n",
				rc);
		return rc;
	}

	rc = haptics_add_debugfs(hap_dir, chip->effects, chip->effects_count, "effect");
	if (rc < 0)
		goto exit;

	rc = haptics_add_debugfs(hap_dir, chip->primitives, chip->primitives_count, "primitive");
	if (rc < 0)
		goto exit;

	file = debugfs_create_file_unsafe("preload_effect_idx", 0644, hap_dir,
			chip, &preload_effect_idx_dbgfs_ops);
	if (IS_ERR(file)) {
		rc = PTR_ERR(file);
		dev_err(chip->dev, "create preload_effect_idx debugfs failed, rc=%d\n",
				rc);
		goto exit;
	}

	debugfs_create_u32("fifo_empty_thresh", 0600, hap_dir,
			&chip->config.fifo_empty_thresh);
	chip->debugfs_dir = hap_dir;
	return 0;

exit:
	debugfs_remove_recursive(hap_dir);
	return rc;
}
#endif

static int haptics_parse_effect_pattern_data(struct haptics_chip *chip,
		struct device_node *node, struct haptics_effect *effect)
{
	struct haptics_hw_config *config = &chip->config;
	u32 data[SAMPLES_PER_PATTERN * 3];
	int rc, tmp, i;

	effect->t_lra_us = config->t_lra_us;
	tmp = of_property_count_elems_of_size(node,
			"qcom,wf-pattern-data", sizeof(u32));
	if (tmp <= 0) {
		dev_dbg(chip->dev, "qcom,wf-pattern-data is not defined properly for effect %d\n",
				effect->id);
		return 0;
	}

	if (tmp > SAMPLES_PER_PATTERN * 3) {
		dev_err(chip->dev, "Pattern src can only play 8 samples at max\n");
		return -EINVAL;
	}

	effect->pattern = devm_kzalloc(chip->dev,
			sizeof(*effect->pattern), GFP_KERNEL);
	if (!effect->pattern)
		return -ENOMEM;

	rc = of_property_read_u32_array(node,
			"qcom,wf-pattern-data", data, tmp);
	if (rc < 0) {
		dev_err(chip->dev, "Read wf-pattern-data failed, rc=%d\n",
				rc);
		return rc;
	}

	for (i = 0; i < tmp / 3; i++) {
		if (data[3 * i] > 0x1ff || data[3 * i + 1] > T_LRA_X_8
				|| data[3 * i + 2] > 1) {
			dev_err(chip->dev, "allowed tuples: [amplitude(<= 0x1ff) period(<=6(T_LRA_X_8)) f_lra_x2(0,1)]\n");
			return -EINVAL;
		}

		effect->pattern->samples[i].amplitude =
			(u16)data[3 * i];
		effect->pattern->samples[i].period =
			(enum s_period)data[3 * i + 1];
		effect->pattern->samples[i].f_lra_x2 =
			(bool)data[3 * i + 2];
	}

	effect->pattern->preload = of_property_read_bool(node,
			"qcom,wf-pattern-preload");
	/*
	 * Use PATTERN1 src by default, effect with preloaded
	 * pattern will use PATTERN2 by default and only the
	 * 1st preloaded pattern will be served.
	 */
	effect->src = PATTERN1;
	if (effect->pattern->preload) {
		if (config->preload_effect != -EINVAL) {
			dev_err(chip->dev, "effect %d has been defined as preloaded\n",
					config->preload_effect);
			effect->pattern->preload = false;
		} else {
			config->preload_effect = effect->id;
			effect->src = PATTERN2;
		}
	}

	if (config->is_erm)
		effect->pattern->play_rate_us = DEFAULT_ERM_PLAY_RATE_US;
	else
		effect->pattern->play_rate_us = config->t_lra_us;

	rc = of_property_read_u32(node, "qcom,wf-pattern-period-us", &tmp);
	if (rc < 0)
		dev_dbg(chip->dev, "Read qcom,wf-pattern-period-us failed, rc=%d\n",
				rc);
	else
		effect->pattern->play_rate_us = tmp;

	if (effect->pattern->play_rate_us > TLRA_MAX_US) {
		dev_err(chip->dev, "qcom,wf-pattern-period-us (%d) exceed the max value: %d\n",
				effect->pattern->play_rate_us,
				TLRA_MAX_US);
		return -EINVAL;
	}

	effect->pattern->play_length_us = get_pattern_play_length_us(effect->pattern);
	if (effect->pattern->play_length_us == -EINVAL) {
		dev_err(chip->dev, "get pattern play length failed\n");
		return -EINVAL;
	}

	return 0;
}

static int haptics_parse_effect_fifo_data(struct haptics_chip *chip,
		struct device_node *node, struct haptics_effect *effect)
{
	struct haptics_hw_config *config = &chip->config;
	int rc, tmp;

	if (effect->pattern) {
		dev_dbg(chip->dev, "ignore parsing FIFO effect when pattern effect is present\n");
		return 0;
	}

	tmp = of_property_count_u8_elems(node, "qcom,wf-fifo-data");
	if (tmp <= 0) {
		dev_dbg(chip->dev, "qcom,wf-fifo-data is not defined properly for effect %d\n",
				effect->id);
		return 0;
	}

	effect->fifo = devm_kzalloc(chip->dev,
			sizeof(*effect->fifo), GFP_KERNEL);
	if (!effect->fifo)
		return -ENOMEM;

	effect->fifo->samples = devm_kcalloc(chip->dev,
			tmp, sizeof(u8), GFP_KERNEL);
	if (!effect->fifo->samples)
		return -ENOMEM;

	rc = of_property_read_u8_array(node, "qcom,wf-fifo-data",
			effect->fifo->samples, tmp);
	if (rc < 0) {
		dev_err(chip->dev, "Read wf-fifo-data failed, rc=%d\n",
				rc);
		return rc;
	}

	effect->fifo->num_s = tmp;
	effect->fifo->period_per_s = T_LRA;
	rc = of_property_read_u32(node, "qcom,wf-fifo-period", &tmp);
	if (rc < 0) {
		dev_err(chip->dev, "Get qcom,wf-fifo-period failed, rc=%d\n", rc);
		return rc;
	} else if (tmp > F_48KHZ) {
		dev_err(chip->dev, "FIFO playing period %d is not supported\n",
				tmp);
		return -EINVAL;
	}

	effect->fifo->period_per_s = tmp;
	effect->fifo->play_length_us =
		get_fifo_play_length_us(effect->fifo, config->t_lra_us);
	if (effect->fifo->play_length_us == -EINVAL) {
		dev_err(chip->dev, "get fifo play length failed\n");
		return -EINVAL;
	}

	effect->src = FIFO;
	effect->fifo->preload = of_property_read_bool(node,
					"qcom,wf-fifo-preload");
	return 0;
}

static int haptics_parse_effect_brake_data(struct haptics_chip *chip,
		struct device_node *node, struct haptics_effect *effect)
{
	struct haptics_hw_config *config = &chip->config;
	int rc, tmp;

	effect->brake = devm_kzalloc(chip->dev,
			sizeof(*effect->brake), GFP_KERNEL);
	if (!effect->brake)
		return -ENOMEM;

	memcpy(effect->brake, &config->brake, sizeof(*effect->brake));

	of_property_read_u32(node, "qcom,wf-brake-mode", &effect->brake->mode);
	if (effect->brake->mode > AUTO_BRAKE) {
		dev_err(chip->dev, "can't support brake mode: %d\n",
				effect->brake->mode);
		return -EINVAL;
	}

	if (effect->brake->brake_wf == WF_SINE) {
		of_property_read_u32(node, "qcom,wf-brake-sine-gain",
				&effect->brake->sine_gain);
		if (effect->brake->sine_gain > BRAKE_SINE_GAIN_X8) {
			dev_err(chip->dev, "can't support brake sine gain: %d\n",
					effect->brake->sine_gain);
			return -EINVAL;
		}
	}

	effect->brake->disabled =
		of_property_read_bool(node, "qcom,wf-brake-disable");
	tmp = of_property_count_u8_elems(node, "qcom,wf-brake-pattern");
	if (tmp > BRAKE_SAMPLE_COUNT) {
		dev_err(chip->dev, "more than %d brake samples\n",
				BRAKE_SAMPLE_COUNT);
		return -EINVAL;
	}

	if (tmp > 0) {
		memset(effect->brake->samples, 0,
				sizeof(u8) * BRAKE_SAMPLE_COUNT);
		rc = of_property_read_u8_array(node, "qcom,wf-brake-pattern",
				effect->brake->samples, tmp);
		if (rc < 0) {
			dev_err(chip->dev, "Read wf-brake-pattern failed, rc=%d\n",
					rc);
			return rc;
		}
		verify_brake_samples(effect->brake);
	} else {
		if (effect->brake->mode == OL_BRAKE ||
				effect->brake->mode == CL_BRAKE)
			effect->brake->disabled = true;
	}

	effect->brake->play_length_us =
		get_brake_play_length_us(effect->brake, config->t_lra_us);

	return 0;
}

static int haptics_parse_per_effect_dt(struct haptics_chip *chip,
		struct device_node *node, struct haptics_effect *effect)
{
	struct haptics_hw_config *config = &chip->config;
	int rc, tmp;

	if (!effect)
		return -EINVAL;

	effect->vmax_mv = config->vmax_mv;
	rc = of_property_read_u32(node, "qcom,wf-vmax-mv", &tmp);
	if (rc < 0)
		dev_dbg(chip->dev, "read qcom,wf-vmax-mv failed, rc=%d\n",
				rc);
	else
		effect->vmax_mv = tmp;

	if (effect->vmax_mv > MAX_VMAX_MV) {
		dev_err(chip->dev, "qcom,wf-vmax-mv (%d) exceed the max value: %d\n",
				effect->vmax_mv, MAX_VMAX_MV);
		return -EINVAL;
	}

	rc = haptics_parse_effect_pattern_data(chip, node, effect);
	if (rc < 0) {
		dev_err(chip->dev, "parse effect PATTERN data failed, rc=%d\n", rc);
		return rc;
	}

	rc = haptics_parse_effect_fifo_data(chip, node, effect);
	if (rc < 0) {
		dev_err(chip->dev, "parse effect FIFO data failed, rc=%d\n", rc);
		return rc;
	}

	if (!effect->pattern && !effect->fifo) {
		dev_err(chip->dev, "no pattern specified for effect %d\n",
				effect->id);
		return -EINVAL;
	}

	rc = haptics_parse_effect_brake_data(chip, node, effect);
	if (rc < 0) {
		dev_err(chip->dev, "parse effect brake data failed, rc=%d\n", rc);
		return rc;
	}

	if (!config->is_erm)
		effect->auto_res_disable = of_property_read_bool(node,
				"qcom,wf-auto-res-disable");

	return 0;
}

static int haptics_parse_primitives_dt(struct haptics_chip *chip)
{
	struct device_node *node = chip->dev->of_node;
	struct device_node *child;
	int rc, i = 0, num = 0;

	for_each_available_child_of_node(node, child) {
		if (of_find_property(child, "qcom,primitive-id", NULL))
			num++;
	}
	if (!num)
		return 0;

	chip->primitives = devm_kcalloc(chip->dev, num,
			sizeof(*chip->effects), GFP_KERNEL);
	if (!chip->primitives)
		return -ENOMEM;

	for_each_available_child_of_node(node, child) {
		if (!of_find_property(child, "qcom,primitive-id", NULL))
			continue;

		rc = of_property_read_u32(child, "qcom,primitive-id", &(chip->primitives[i].id));
		if (rc < 0) {
			dev_err(chip->dev, "Read qcom,primitive-id failed, rc=%d\n",
					rc);
			of_node_put(child);
			return rc;
		}

		rc = haptics_parse_per_effect_dt(chip, child,
					&chip->primitives[i]);
		if (rc < 0) {
			dev_err(chip->dev, "parse primitive %d failed, rc=%d\n",
					i, rc);
			of_node_put(child);
			return rc;
		}
		i++;
	}

	chip->primitives_count = i;
	dev_dbg(chip->dev, "Dump primitive effect settings as following\n");
	__dump_effects(chip, chip->primitives, chip->primitives_count);

	return 0;
}

static int haptics_parse_effects_dt(struct haptics_chip *chip)
{
	struct device_node *node = chip->dev->of_node;
	struct device_node *child;
	int rc, i = 0, num = 0;

	for_each_available_child_of_node(node, child) {
		if (of_find_property(child, "qcom,effect-id", NULL))
			num++;
	}
	if (num == 0)
		return 0;

	chip->effects = devm_kcalloc(chip->dev, num,
			sizeof(*chip->effects), GFP_KERNEL);
	if (!chip->effects)
		return -ENOMEM;

	for_each_available_child_of_node(node, child) {
		if (!of_find_property(child, "qcom,effect-id", NULL))
			continue;

		rc = of_property_read_u32(child, "qcom,effect-id", &(chip->effects[i].id));
		if (rc < 0) {
			dev_err(chip->dev, "Read qcom,effect-id failed, rc=%d\n",
					rc);
			of_node_put(child);
			return rc;
		}

		rc = haptics_parse_per_effect_dt(chip, child,
					&chip->effects[i]);
		if (rc < 0) {
			dev_err(chip->dev, "parse effect %d failed, rc=%d\n",
					i, rc);
			of_node_put(child);
			return rc;
		}
		i++;
	}

	chip->effects_count = i;
	dev_dbg(chip->dev, "Dump predefined effect settings as following\n");
	__dump_effects(chip, chip->effects, chip->effects_count);

	return 0;
}

static int haptics_parse_lra_dt(struct haptics_chip *chip)
{
	struct haptics_hw_config *config = &chip->config;
	struct device_node *node = chip->dev->of_node;
	int rc;

	rc = of_property_read_u32(node, "qcom,lra-period-us",
					&config->t_lra_us);
	if (rc < 0) {
		dev_err(chip->dev, "Read T-LRA failed, rc=%d\n", rc);
		return rc;
	}

	if (config->t_lra_us > TLRA_MAX_US) {
		dev_err(chip->dev, "qcom,lra-period-us (%d) exceed the max value: %d\n",
				config->t_lra_us, TLRA_MAX_US);
		return -EINVAL;
	}

	config->drv_wf = WF_SINE;
	of_property_read_u32(node, "qcom,drv-sig-shape", &config->drv_wf);
	if (config->drv_wf >= WF_RESERVED) {
		dev_err(chip->dev, "Can't support drive shape: %d\n",
				config->drv_wf);
		return -EINVAL;
	}

	config->brake.brake_wf = WF_SINE;
	of_property_read_u32(node, "qcom,brake-sig-shape",
			&config->brake.brake_wf);
	if (config->brake.brake_wf >= WF_RESERVED) {
		dev_err(chip->dev, "Can't support brake shape: %d\n",
				config->brake.brake_wf);
		return -EINVAL;
	}

	if (config->brake.brake_wf == WF_SINE) {
		config->brake.sine_gain = BRAKE_SINE_GAIN_X1;
		of_property_read_u32(node, "qcom,brake-sine-gain",
				&config->brake.sine_gain);
		if (config->brake.sine_gain > BRAKE_SINE_GAIN_X8) {
			dev_err(chip->dev, "Can't support brake sine gain: %d\n",
					config->brake.sine_gain);
			return -EINVAL;
		}
	}

	if (chip->hw_type == HAP525_HV)
		config->measure_lra_impedance = of_property_read_bool(node,
				"qcom,rt-imp-detect");

	return 0;
}

static int haptics_get_revision(struct haptics_chip *chip)
{
	int rc;
	u8 val[2];

	rc = haptics_read(chip, chip->cfg_addr_base,
			HAP_CFG_REVISION2_REG, val, 1);
	if (rc < 0)
		return rc;

	chip->cfg_revision = val[0];
	rc = haptics_read(chip, chip->ptn_addr_base,
			HAP_PTN_REVISION2_REG, val, 1);
	if (rc < 0)
		return rc;

	chip->ptn_revision = val[0];

	if (is_haptics_external_powered(chip)) {
		dev_info(chip->dev, "haptics revision: HAP_CFG %#x, HAP_PTN %#x\n",
			chip->cfg_revision, chip->ptn_revision);
	} else {
		rc = haptics_read(chip, chip->hbst_addr_base,
				HAP_BOOST_REVISION1, val, 2);
		if (rc < 0)
			return rc;

		chip->hbst_revision = (val[1] << 8) | val[0];
		dev_info(chip->dev, "haptics revision: HAP_CFG %#x, HAP_PTN %#x, HAP_HBST %#x\n",
			chip->cfg_revision, chip->ptn_revision, chip->hbst_revision);

	}

	if ((chip->cfg_revision == HAP_CFG_V2) &&
			(chip->ptn_revision == HAP_PTN_V2)) {
		chip->hw_type = HAP520;
	} else if ((chip->cfg_revision == HAP_CFG_V3) &&
			(chip->ptn_revision == HAP_PTN_V3)) {
		chip->hw_type = HAP520_MV;
	} else if ((chip->cfg_revision == HAP_CFG_V4) &&
			(chip->ptn_revision == HAP_PTN_V4)) {
		chip->hw_type = HAP525_HV;
	} else {
		dev_err(chip->dev, "haptics revision is not supported\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int haptics_parse_hpwr_vreg_dt(struct haptics_chip *chip)
{
	struct device_node *node = chip->dev->of_node;
	int rc;

	if (!of_find_property(node, "qcom,hpwr-supply", NULL))
		return 0;

	chip->hpwr_vreg = devm_regulator_get(chip->dev, "qcom,hpwr");
	if (IS_ERR(chip->hpwr_vreg)) {
		rc = PTR_ERR(chip->hpwr_vreg);
		if (rc != -EPROBE_DEFER)
			dev_err(chip->dev, "Failed to get qcom,hpwr-supply, rc=%d\n",
					rc);
		return rc;
	}

	rc = of_property_read_u32(node, "qcom,hpwr-voltage-mv",
			&chip->hpwr_voltage_mv);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read qcom,hpwr-voltage-mv, rc=%d\n",
				rc);
		return rc;
	}

	if (chip->hpwr_voltage_mv == 0 ||
			chip->hpwr_voltage_mv > NON_HBOOST_MAX_VMAX_MV)
		return -EINVAL;

	return 0;
}

static int haptics_parse_dt(struct haptics_chip *chip)
{
	struct haptics_hw_config *config = &chip->config;
	struct device_node *node = chip->dev->of_node;
	struct platform_device *pdev = to_platform_device(chip->dev);
	const __be32 *addr;
	int rc = 0, tmp;

	rc = haptics_parse_hpwr_vreg_dt(chip);
	if (rc < 0)
		return rc;

	if (of_find_property(node, "nvmem-cells", NULL)) {
		chip->cl_brake_nvmem = devm_nvmem_cell_get(chip->dev,
						"hap_cl_brake");
		if (IS_ERR(chip->cl_brake_nvmem)) {
			rc = PTR_ERR(chip->cl_brake_nvmem);
			if (rc != -EPROBE_DEFER)
				dev_err(chip->dev, "Failed to get nvmem-cells, rc=%d\n",
							rc);

			return rc;
		}
	}

	if (of_find_property(node, "nvmem", NULL)) {
		chip->hap_cfg_nvmem =
			devm_nvmem_device_get(chip->dev, "hap_cfg_sdam");
		if (IS_ERR(chip->hap_cfg_nvmem)) {
			rc = PTR_ERR(chip->hap_cfg_nvmem);
			if (rc != -EPROBE_DEFER)
				dev_err(chip->dev, "Failed to get hap_cfg nvmem device, rc=%d\n",
						rc);
			return rc;
		}
	}

	if (of_find_property(node, "qcom,pbs-client", NULL)) {
		chip->pbs_node = of_parse_phandle(node, "qcom,pbs-client", 0);
		if (!chip->pbs_node) {
			dev_err(chip->dev, "Failed to get PBS client\n");
			return -ENODEV;
		}
	}

	addr = of_get_address(node, 0, NULL, NULL);
	if (!addr) {
		dev_err(chip->dev, "Read HAPTICS_CFG address failed\n");
		rc = -EINVAL;
		goto free_pbs;
	}

	chip->cfg_addr_base = be32_to_cpu(*addr);
	addr = of_get_address(node, 1, NULL, NULL);
	if (!addr) {
		dev_err(chip->dev, "Read HAPTICS_PATTERN address failed\n");
		rc = -EINVAL;
		goto free_pbs;
	}

	chip->ptn_addr_base = be32_to_cpu(*addr);
	addr = of_get_address(node, 2, NULL, NULL);
	if (!addr && !is_haptics_external_powered(chip)) {
		dev_err(chip->dev, "Read HAPTICS_HBOOST address failed\n");
		rc = -EINVAL;
		goto free_pbs;
	} else if (addr != NULL) {
		chip->hbst_addr_base = be32_to_cpu(*addr);
	}

	rc = haptics_get_revision(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Get revision failed, rc=%d\n", rc);
		goto free_pbs;
	}

	chip->fifo_empty_irq = platform_get_irq_byname(pdev, "fifo-empty");
	if (!chip->fifo_empty_irq) {
		dev_err(chip->dev, "Get fifo-empty IRQ failed\n");
		rc = -EINVAL;
		goto free_pbs;
	}

	config->vmax_mv = DEFAULT_VMAX_MV;
	of_property_read_u32(node, "qcom,vmax-mv", &config->vmax_mv);
	if (config->vmax_mv >= MAX_VMAX_MV) {
		dev_err(chip->dev, "qcom,vmax-mv (%d) exceed the max value: %d\n",
				config->vmax_mv, MAX_VMAX_MV);
		rc = -EINVAL;
		goto free_pbs;
	}

	config->fifo_empty_thresh = get_fifo_empty_threshold(chip);
	of_property_read_u32(node, "qcom,fifo-empty-threshold",
			&config->fifo_empty_thresh);
	if (config->fifo_empty_thresh >= get_max_fifo_samples(chip)) {
		dev_err(chip->dev, "FIFO empty threshold (%d) should be less than %d\n",
			config->fifo_empty_thresh, get_max_fifo_samples(chip));
		rc = -EINVAL;
		goto free_pbs;
	}

	config->brake.mode = AUTO_BRAKE;
	of_property_read_u32(node, "qcom,brake-mode", &config->brake.mode);
	if (config->brake.mode > AUTO_BRAKE) {
		dev_err(chip->dev, "Can't support brake mode: %d\n",
				config->brake.mode);
		rc = -EINVAL;
		goto free_pbs;
	}

	config->brake.disabled =
		of_property_read_bool(node, "qcom,brake-disable");
	tmp = of_property_count_u8_elems(node, "qcom,brake-pattern");
	if (tmp > BRAKE_SAMPLE_COUNT) {
		dev_err(chip->dev, "more than %d brake samples\n",
				BRAKE_SAMPLE_COUNT);
		rc = -EINVAL;
		goto free_pbs;
	}

	if (tmp > 0) {
		rc = of_property_read_u8_array(node, "qcom,brake-pattern",
				config->brake.samples, tmp);
		if (rc < 0) {
			dev_err(chip->dev, "Read brake-pattern failed, rc=%d\n",
					rc);
			goto free_pbs;
		}
		verify_brake_samples(&config->brake);
	} else {
		if (config->brake.mode == OL_BRAKE ||
				config->brake.mode == CL_BRAKE)
			config->brake.disabled = true;
	}

	config->is_erm = of_property_read_bool(node, "qcom,use-erm");
	if (config->is_erm) {
		config->drv_wf = WF_NO_MODULATION;
		config->brake.brake_wf = WF_NO_MODULATION;
	} else {
		rc = haptics_parse_lra_dt(chip);
		if (rc < 0) {
			dev_err(chip->dev, "Parse device-tree for LRA failed, rc=%d\n",
					rc);
			goto free_pbs;
		}
	}

	config->preload_effect = -EINVAL;
	rc = haptics_parse_effects_dt(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Parse device-tree for effects failed, rc=%d\n",
				 rc);
		goto free_pbs;
	}

	rc = haptics_parse_primitives_dt(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Parse device-tree for primitives failed, rc=%d\n",
				rc);
		goto free_pbs;
	}

	return 0;
free_pbs:
	if (chip->pbs_node) {
		of_node_put(chip->pbs_node);
		chip->pbs_node = NULL;
	}

	return rc;
}

static int swr_slave_reg_enable(struct regulator_dev *rdev)
{
	struct haptics_chip *chip = rdev_get_drvdata(rdev);
	int rc;
	u8 mask = SWR_PAT_INPUT_EN_BIT | SWR_PAT_RES_N_BIT | SWR_PAT_CFG_EN_BIT;

	rc = haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_SWR_ACCESS_REG, mask, mask);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to enable SWR_PAT, rc=%d\n",
				rc);
		return rc;
	}

	if (!(chip->wa_flags & RECOVER_SWR_SLAVE))
		goto done;

	/*
	 * If haptics has already been in SWR mode when enabling the SWR
	 * slave, it means that the haptics module was stuck in prevous
	 * SWR play. Then toggle HAPTICS_EN to reset haptics module and
	 * ignore SWR mode until next SWR slave enable request is coming.
	 */
	if (is_swr_play_enabled(chip)) {
		rc = haptics_masked_write(chip, chip->cfg_addr_base,
				HAP_CFG_TRIG_PRIORITY_REG,
				SWR_IGNORE_BIT, SWR_IGNORE_BIT);
		if (rc < 0) {
			dev_err(chip->dev, "Failed to enable SWR_IGNORE, rc=%d\n", rc);
			return rc;
		}

		rc = haptics_toggle_module_enable(chip);
		if (rc < 0)
			return rc;
	} else {
		rc = haptics_masked_write(chip, chip->cfg_addr_base,
				HAP_CFG_TRIG_PRIORITY_REG,
				SWR_IGNORE_BIT, 0);
		if (rc < 0) {
			dev_err(chip->dev, "Failed to disable SWR_IGNORE, rc=%d\n", rc);
			return rc;
		}
	}
done:
	chip->swr_slave_enabled = true;
	return 0;
}

static int swr_slave_reg_disable(struct regulator_dev *rdev)
{
	struct haptics_chip *chip = rdev_get_drvdata(rdev);
	int rc;

	rc = haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_SWR_ACCESS_REG,
			SWR_PAT_INPUT_EN_BIT | SWR_PAT_RES_N_BIT |
			SWR_PAT_CFG_EN_BIT, 0);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to disable SWR_PAT, rc=%d\n", rc);
		return rc;
	}

	chip->swr_slave_enabled = false;
	return 0;
}

static int swr_slave_reg_is_enabled(struct regulator_dev *rdev)
{
	struct haptics_chip *chip = rdev_get_drvdata(rdev);

	return chip->swr_slave_enabled;
}

static const struct regulator_ops swr_slave_reg_ops = {
	.enable		= swr_slave_reg_enable,
	.disable	= swr_slave_reg_disable,
	.is_enabled	= swr_slave_reg_is_enabled,
};

static struct regulator_desc swr_slave_reg_rdesc = {
	.owner		= THIS_MODULE,
	.of_match	= "qcom,hap-swr-slave-reg",
	.name		= "hap-swr-slave-reg",
	.type		= REGULATOR_VOLTAGE,
	.ops		= &swr_slave_reg_ops,
};

static int haptics_init_swr_slave_regulator(struct haptics_chip *chip)
{
	struct regulator_config cfg = {};
	u8 val, mask;
	int rc = 0;

	rc = haptics_read(chip, chip->cfg_addr_base,
			HAP_CFG_SWR_ACCESS_REG, &val, 1);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read SWR_ACCESS, rc=%d\n",
				rc);
		return rc;
	}

	mask = SWR_PAT_INPUT_EN_BIT | SWR_PAT_RES_N_BIT | SWR_PAT_CFG_EN_BIT;
	val &= mask;
	chip->swr_slave_enabled = ((val == mask) ? true : false);

	cfg.dev = chip->dev;
	cfg.driver_data = chip;
	chip->swr_slave_rdev = devm_regulator_register(chip->dev,
			&swr_slave_reg_rdesc, &cfg);
	if (IS_ERR(chip->swr_slave_rdev)) {
		rc = PTR_ERR(chip->swr_slave_rdev);
		chip->swr_slave_rdev = NULL;

		if (rc != -EPROBE_DEFER)
			dev_err(chip->dev, "register swr-slave-reg regulator failed, rc=%d\n",
					rc);
	}

	return rc;
}

static int haptics_pbs_trigger_isc_config(struct haptics_chip *chip)
{
	int rc;

	if (chip->pbs_node == NULL) {
		dev_err(chip->dev, "PBS device is not defined\n");
		return -ENODEV;
	}

	rc = qpnp_pbs_trigger_single_event(chip->pbs_node);
	if (rc < 0)
		dev_err(chip->dev, "Trigger PBS to config ISC failed, rc=%d\n",
				rc);

	return rc;
}

#define MAX_SWEEP_STEPS		5
#define MIN_DUTY_MILLI_PCT	0
#define MAX_DUTY_MILLI_PCT	100000
#define LRA_CONFIG_REGS		3
static u32 get_lra_impedance_capable_max(struct haptics_chip *chip)
{
	u32 mohms;
	u32 max_vmax_mv, min_isc_ma;

	switch (chip->hw_type) {
	case HAP520:
		min_isc_ma = 250;
		if (chip->clamp_at_5v)
			max_vmax_mv = 5000;
		else
			max_vmax_mv = 10000;
		break;
	case HAP520_MV:
		max_vmax_mv = 5000;
		min_isc_ma = 140;
		break;
	case HAP525_HV:
		if (chip->clamp_at_5v) {
			max_vmax_mv = 5000;
			min_isc_ma = 125;
		} else {
			max_vmax_mv = 10000;
			min_isc_ma = 250;
		}
		break;
	default:
		return 0;
	}

	mohms = (max_vmax_mv * 1000) / min_isc_ma;
	if (is_haptics_external_powered(chip))
		mohms = (chip->hpwr_voltage_mv * 1000) / min_isc_ma;

	dev_dbg(chip->dev, "LRA impedance capable max: %u mohms\n", mohms);
	return mohms;
}

#define RT_IMPD_DET_VMAX_DEFAULT_MV		4500
static int haptics_measure_realtime_lra_impedance(struct haptics_chip *chip)
{
	int rc;
	u8 val, current_sel;
	u32 vmax_mv, nominal_ohm, current_ma, vmax_margin_mv;
	struct pattern_cfg pattern = {
		.samples = {
			    {0xff, T_LRA, false},
			    {0, 0, 0},
			    {0, 0, 0},
			    {0, 0, 0},
			    {0, 0, 0},
			    {0, 0, 0},
			    {0, 0, 0},
			    {0, 0, 0},
			   },
		.play_rate_us = chip->config.t_lra_us + 2000, /* drive off resonance of the LRA */
		.play_length_us = chip->config.t_lra_us + 2000, /* drive it at least 1 cycle */
		.preload = false,
	};

	/* calculate Vmax according to nominal resistance */
	vmax_mv = RT_IMPD_DET_VMAX_DEFAULT_MV;
	current_sel = chip->clamp_at_5v ? CURRENT_SEL_VAL_125MA : CURRENT_SEL_VAL_250MA;
	if (chip->hap_cfg_nvmem != NULL) {
		rc = nvmem_device_read(chip->hap_cfg_nvmem,
				HAP_LRA_NOMINAL_OHM_SDAM_OFFSET, 1, &val);
		if (rc > 0 && val) {
			nominal_ohm = val * LRA_IMPEDANCE_MOHMS_LSB / 1000;
			/*
			 * use 250mA current_sel when nominal impedance is lower than 15 Ohms
			 * and use 125mA detection when nominal impedance is higher than 40 Ohms
			 */
			if (nominal_ohm < 15)
				current_sel = CURRENT_SEL_VAL_250MA;
			else if (nominal_ohm > 40)
				current_sel = CURRENT_SEL_VAL_125MA;

			/*
			 * give 8 Ohms margin (1000mV / 125mA, or 2000mV / 250mA) for wider
			 * detectability
			 */
			if (current_sel == CURRENT_SEL_VAL_125MA) {
				current_ma = 125;
				vmax_margin_mv = 1000;
			} else {
				current_ma = 250;
				vmax_margin_mv = 2000;
			}

			vmax_mv = max(vmax_mv, nominal_ohm * current_ma + vmax_margin_mv);
		}
	}

	dev_dbg(chip->dev, "Set %u mV Vmax for impedance detection\n", vmax_mv);
	rc = haptics_set_vmax_mv(chip, vmax_mv);
	if (rc < 0)
		return rc;

	/* set current for imp_det comparator and enable it */
	val = LRA_IMPEDANCE_MEAS_EN_BIT | current_sel;
	rc = haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_RT_LRA_IMPD_MEAS_CFG_REG,
			LRA_IMPEDANCE_MEAS_EN_BIT |
			LRA_IMPEDANCE_MEAS_CURRENT_SEL_BIT, val);
	if (rc < 0)
		return rc;

	/* disconnect imp_det comparator to avoid it impact SC behavior */
	rc = haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_ISC_CFG_REG, EN_IMP_DET_HAP525_HV_BIT, 0);
	if (rc < 0)
		goto restore;

	/* play 1 drive cycle using PATTERN1 source */
	rc = haptics_set_pattern(chip, &pattern, PATTERN1);
	if (rc < 0)
		goto restore;

	chip->play.pattern_src = PATTERN1;
	rc = haptics_enable_play(chip, true);
	if (rc < 0)
		goto restore;

	usleep_range(pattern.play_length_us, pattern.play_length_us + 1);

	rc = haptics_enable_play(chip, false);
	if (rc < 0)
		goto restore;

	/* read the measured LRA impedance */
	rc = haptics_read(chip, chip->cfg_addr_base,
			HAP_CFG_REAL_TIME_LRA_IMPEDANCE_REG, &val, 1);
	if (rc < 0)
		goto restore;

	chip->config.lra_measured_mohms = val * LRA_IMPEDANCE_MOHMS_LSB;
	dev_dbg(chip->dev, "measured LRA impedance: %u mohm",
			chip->config.lra_measured_mohms);
	/* store the detected LRA impedance into SDAM for future usage */
	if (chip->hap_cfg_nvmem != NULL) {
		rc = nvmem_device_write(chip->hap_cfg_nvmem,
				HAP_LRA_DETECTED_OHM_SDAM_OFFSET, 1, &val);
		if (rc < 0)
			dev_err(chip->dev, "write measured impedance value into SDAM failed, rc=%d\n",
					rc);
	}
restore:
	if (rc < 0)
		dev_err(chip->dev, "measure LRA impedance failed, rc=%d\n", rc);

	return haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_RT_LRA_IMPD_MEAS_CFG_REG,
			LRA_IMPEDANCE_MEAS_EN_BIT, 0);
}

static int haptics_detect_lra_impedance(struct haptics_chip *chip)
{
	int rc, i;
	struct haptics_reg_info lra_config[LRA_CONFIG_REGS] = {
		{ HAP_CFG_DRV_WF_SEL_REG, 0x10 },
		{ HAP_CFG_VMAX_REG, 0xC8 },
		{ HAP_CFG_VMAX_HDRM_REG, 0x00 },
	};
	struct haptics_reg_info backup[LRA_CONFIG_REGS];
	u8 val, cfg1, cfg2, reg1, reg2, mask1, mask2, val1, val2;
	u32 duty_milli_pct, low_milli_pct, high_milli_pct;
	u32 amplitude, lra_min_mohms, lra_max_mohms, capability_mohms;

	/* Backup default register values */
	memcpy(backup, lra_config, sizeof(backup));
	for (i = 0; i < LRA_CONFIG_REGS; i++) {
		rc = haptics_read(chip, chip->cfg_addr_base,
				backup[i].addr, &backup[i].val, 1);
		if (rc < 0)
			return rc;
	}

	if (chip->hw_type == HAP520) {
		/* Trigger PBS to config 250mA ISC setting */
		rc = haptics_pbs_trigger_isc_config(chip);
		if (rc < 0)
			return rc;
	} else {
		 /* Config ISC_CFG settings for LRA impedance_detection */
		switch (chip->hw_type) {
		case HAP520_MV:
			reg1 = HAP_CFG_ISC_CFG_REG;
			mask1 = ILIM_CC_EN_BIT;
			val1 = ILIM_CC_EN_BIT;
			reg2 = HAP_CFG_ISC_CFG2_REG;
			mask2 = EN_SC_DET_P_HAP520_MV_BIT |
				EN_SC_DET_N_HAP520_MV_BIT |
				ISC_THRESH_HAP520_MV_MASK;
			val2 = EN_SC_DET_P_HAP520_MV_BIT|
				EN_SC_DET_N_HAP520_MV_BIT|
				ISC_THRESH_HAP520_MV_140MA;
			break;
		case HAP525_HV:
			reg1 = HAP_CFG_ISC_CFG_REG;
			mask1 = EN_SC_DET_P_HAP525_HV_BIT | EN_SC_DET_N_HAP525_HV_BIT |
				EN_IMP_DET_HAP525_HV_BIT | ILIM_PULSE_DENSITY_MASK;
			val1 = EN_IMP_DET_HAP525_HV_BIT |
				FIELD_PREP(ILIM_PULSE_DENSITY_MASK, ILIM_DENSITY_8_OVER_64_CYCLES);
			reg2 = HAP_CFG_RT_LRA_IMPD_MEAS_CFG_REG;
			mask2 = LRA_IMPEDANCE_MEAS_EN_BIT | LRA_IMPEDANCE_MEAS_CURRENT_SEL_BIT;
			val2 = chip->clamp_at_5v ? CURRENT_SEL_VAL_125MA : CURRENT_SEL_VAL_250MA;
			val2 |= LRA_IMPEDANCE_MEAS_EN_BIT;
			break;
		default:
			dev_err(chip->dev, "unsupported HW type: %d\n",
					chip->hw_type);
			return -EOPNOTSUPP;
		}

		 /* save ISC_CFG default settings */
		rc = haptics_read(chip, chip->cfg_addr_base, reg1, &cfg1, 1);
		if (rc < 0)
			return rc;

		rc = haptics_read(chip, chip->cfg_addr_base, reg2, &cfg2, 1);
		if (rc < 0)
			return rc;

		/* update ISC_CFG settings for the detection */
		rc = haptics_masked_write(chip, chip->cfg_addr_base, reg1, mask1, val1);
		if (rc < 0)
			return rc;

		rc = haptics_masked_write(chip, chip->cfg_addr_base, reg2, mask2, val2);
		if (rc < 0)
			goto restore;
	}

	/* Set square drive waveform, 10V Vmax, no HDRM */
	for (i = 0; i < LRA_CONFIG_REGS; i++) {
		rc = haptics_write(chip, chip->cfg_addr_base,
				lra_config[i].addr, &lra_config[i].val, 1);
		if (rc < 0)
			goto restore;
	}

	rc = haptics_enable_hpwr_vreg(chip, true);
	if (rc < 0)
		goto restore;

	low_milli_pct = MIN_DUTY_MILLI_PCT;
	high_milli_pct = MAX_DUTY_MILLI_PCT;
	/* Sweep duty cycle using binary approach */
	for (i = 0; i < MAX_SWEEP_STEPS; i++) {
		/* Set direct play amplitude */
		duty_milli_pct = (low_milli_pct + high_milli_pct) / 2;
		amplitude = (duty_milli_pct * DIRECT_PLAY_MAX_AMPLITUDE)
						/ 100000;
		rc = haptics_set_direct_play(chip, (u8)amplitude);
		if (rc < 0)
			goto restore;

		dev_dbg(chip->dev, "sweeping milli_pct %u, amplitude %#x\n",
				duty_milli_pct, amplitude);
		/* Enable play */
		chip->play.pattern_src = DIRECT_PLAY;
		rc = haptics_enable_play(chip, true);
		if (rc < 0)
			goto restore;

		/* Play a cycle then read SC fault status */
		usleep_range(chip->config.t_lra_us,
				chip->config.t_lra_us + 1000);
		rc = haptics_read(chip, chip->cfg_addr_base,
				HAP_CFG_FAULT_STATUS_REG, &val, 1);
		if (rc < 0)
			goto restore;

		if (val & SC_FLAG_BIT)
			high_milli_pct = duty_milli_pct;
		else
			low_milli_pct = duty_milli_pct;

		/* Disable play */
		rc = haptics_enable_play(chip, false);
		if (rc < 0)
			goto restore;

		/* Sleep 4ms */
		usleep_range(4000, 5000);
	}

	capability_mohms = get_lra_impedance_capable_max(chip);
	lra_min_mohms = low_milli_pct * capability_mohms / 100000;
	lra_max_mohms = high_milli_pct * capability_mohms / 100000;
	if (lra_min_mohms == 0)
		dev_warn(chip->dev, "Short circuit detected!\n");
	else if (lra_max_mohms >= capability_mohms)
		dev_warn(chip->dev, "Open circuit detected!\n");
	else
		dev_dbg(chip->dev, "LRA impedance is between %u - %u mohms\n",
				lra_min_mohms, lra_max_mohms);

	chip->config.lra_min_mohms = lra_min_mohms;
	chip->config.lra_max_mohms = lra_max_mohms;
	chip->config.lra_open_mohms = capability_mohms;
restore:
	/* Disable play in case it's not been disabled */
	haptics_enable_play(chip, false);
	rc = haptics_enable_hpwr_vreg(chip, false);
	if (rc < 0)
		return rc;

	if (chip->hw_type == HAP520) {
		/* Trigger PBS to restore 1500mA ISC setting */
		rc = haptics_pbs_trigger_isc_config(chip);
		if (rc < 0)
			return rc;
	} else {
		/* restore ISC_CFG settings to default */
		rc = haptics_write(chip, chip->cfg_addr_base, reg1, &cfg1, 1);
		if (rc < 0)
			return rc;

		rc = haptics_write(chip, chip->cfg_addr_base, reg2, &cfg2, 1);
		if (rc < 0)
			return rc;
	}

	/* Restore driver waveform, Vmax, HDRM settings */
	for (i = 0; i < LRA_CONFIG_REGS; i++) {
		rc = haptics_write(chip, chip->cfg_addr_base,
				backup[i].addr, &backup[i].val, 1);
		if (rc < 0)
			break;
	}

	return rc;
}

#define LRA_CALIBRATION_VMAX_HDRM_MV	500
static int haptics_detect_lra_frequency(struct haptics_chip *chip)
{
	int rc;
	u8 autores_cfg, drv_duty_cfg, amplitude, mask, val;
	u32 vmax_mv = chip->config.vmax_mv;

	rc = haptics_read(chip, chip->cfg_addr_base,
			HAP_CFG_AUTORES_CFG_REG, &autores_cfg, 1);
	if (rc < 0) {
		dev_err(chip->dev, "Read AUTORES_CFG failed, rc=%d\n", rc);
		return rc;
	}

	rc = haptics_read(chip, chip->cfg_addr_base,
			HAP_CFG_DRV_DUTY_CFG_REG, &drv_duty_cfg, 1);
	if (rc < 0) {
		dev_err(chip->dev, "Read DRV_DUTY_CFG failed, rc=%d\n", rc);
		return rc;
	}

	if (chip->hw_type == HAP525_HV)
		val = AUTORES_EN_DLY_7_CYCLES << AUTORES_EN_DLY_SHIFT|
			AUTORES_ERR_WINDOW_25_PERCENT | AUTORES_EN_BIT;
	else
		val = AUTORES_EN_DLY_6_CYCLES << AUTORES_EN_DLY_SHIFT|
			AUTORES_ERR_WINDOW_50_PERCENT | AUTORES_EN_BIT;

	rc = haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_AUTORES_CFG_REG, AUTORES_EN_BIT |
			AUTORES_EN_DLY_MASK | AUTORES_ERR_WINDOW_MASK,
			val);
	if (rc < 0)
		return rc;

	if (chip->hw_type == HAP525_HV) {
		mask = ADT_DRV_DUTY_EN_BIT | ADT_BRK_DUTY_EN_BIT |
			DRV_DUTY_MASK | BRK_DUTY_MASK;
		val = DRV_DUTY_62P5_PCT << DRV_DUTY_SHIFT | BRK_DUTY_75_PCT;
	} else {
		mask = ADT_DRV_DUTY_EN_BIT;
		val = 0;
	}

	rc = haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_DRV_DUTY_CFG_REG, mask, val);
	if (rc < 0)
		goto restore;

	rc = haptics_config_openloop_lra_period(chip, chip->config.t_lra_us);
	if (rc < 0)
		goto restore;

	rc = haptics_set_vmax_headroom_mv(chip, LRA_CALIBRATION_VMAX_HDRM_MV);
	if (rc < 0)
		goto restore;

	/* Fix Vmax to (hpwr_vreg_mv - hdrm_mv) in non-HBOOST regulator case */
	if (is_haptics_external_powered(chip))
		vmax_mv = chip->hpwr_voltage_mv - LRA_CALIBRATION_VMAX_HDRM_MV;

	rc = haptics_set_vmax_mv(chip, vmax_mv);
	if (rc < 0)
		goto restore;

	amplitude = get_direct_play_max_amplitude(chip);
	rc = haptics_set_direct_play(chip, amplitude);
	if (rc < 0)
		goto restore;

	rc = haptics_enable_hpwr_vreg(chip, true);
	if (rc < 0)
		goto restore;

	chip->play.pattern_src = DIRECT_PLAY;
	rc = haptics_enable_play(chip, true);
	if (rc < 0)
		goto restore;

	/* wait for ~150ms to get the LRA calibration result */
	usleep_range(150000, 155000);

	rc = haptics_get_closeloop_lra_period(chip, false);
	if (rc < 0)
		goto restore;

	rc = haptics_enable_play(chip, false);
	if (rc < 0)
		goto restore;

	haptics_config_openloop_lra_period(chip, chip->config.cl_t_lra_us);

restore:
	/* Disable play in case it's not been disabled */
	haptics_enable_play(chip, false);
	rc = haptics_enable_hpwr_vreg(chip, false);
	if (rc < 0)
		return rc;

	rc = haptics_write(chip, chip->cfg_addr_base,
			HAP_CFG_AUTORES_CFG_REG, &autores_cfg, 1);
	if (rc < 0)
		return rc;

	rc = haptics_write(chip, chip->cfg_addr_base,
			HAP_CFG_DRV_DUTY_CFG_REG, &drv_duty_cfg, 1);

	return rc;
}

#define AUTO_BRAKE_CAL_POLLING_COUNT	10
#define AUTO_BRAKE_CAL_POLLING_STEP_US	20000
#define AUTO_BRAKE_CAL_WAIT_MS		800
#define AUTO_BRAKE_CAL_DRIVE_CYCLES	6
static int haptics_start_auto_brake_calibration(struct haptics_chip *chip)
{
	struct haptics_reg_info lra_config[4] = {
		{ HAP_CFG_DRV_DUTY_CFG_REG, 0x55 },
		{ HAP_CFG_BRAKE_MODE_CFG_REG, 0xC1 },
		{ HAP_CFG_CL_BRAKE_CFG_REG, 0xE1 },
		{ HAP_CFG_ADT_DRV_DUTY_CFG_REG, 0x3B },
	};
	struct haptics_reg_info backup[4];
	u32 retry_count = AUTO_BRAKE_CAL_POLLING_COUNT;
	u32 t_lra_us, tmp;
	u8 val[AUTO_BRAKE_CAL_DRIVE_CYCLES] = {};
	int rc, i;

	/* Ignore calibration if nvmem is not assigned */
	if (!chip->hap_cfg_nvmem || chip->hw_type != HAP525_HV)
		return -EOPNOTSUPP;

	t_lra_us = chip->config.t_lra_us;
	/* Update T_LRA into SDAM */
	if (chip->config.cl_t_lra_us != 0)
		t_lra_us = chip->config.cl_t_lra_us;

	tmp = t_lra_us / TLRA_STEP_US;
	val[0] = tmp & TLRA_OL_LSB_MASK;
	val[1] = (tmp >> 8) & TLRA_OL_MSB_MASK;
	rc = nvmem_device_write(chip->hap_cfg_nvmem, HAP_LRA_PTRN1_TLRA_LSB_OFFSET, 2, val);
	if (rc < 0) {
		dev_err(chip->dev, "set T_LRA for auto brake cal failed,rc=%d\n", rc);
		return rc;
	}

	/* Set Vmax to 4.5V with 50mV per step */
	val[0] = 0x5A;
	rc = nvmem_device_write(chip->hap_cfg_nvmem, HAP_AUTO_BRAKE_CAL_VMAX_OFFSET, 1, val);
	if (rc < 0) {
		dev_err(chip->dev, "set Vmax for auto brake cal failed,rc=%d\n", rc);
		return rc;
	}

	/* Set PTRN1_CFGx for each cycle in the 6-cycle drive calibration */
	rc = nvmem_device_write(chip->hap_cfg_nvmem, HAP_AUTO_BRAKE_CAL_PTRN1_CFG0_OFFSET,
			AUTO_BRAKE_CAL_DRIVE_CYCLES, val);
	if (rc < 0) {
		dev_err(chip->dev, "set PTRN1_CFGx for auto brake cal failed, rc=%d\n", rc);
		return rc;
	}

	/*
	 * Set amplitude for 6-cycle drive calibration
	 *   1 cycle over-drive with 4.5V VMAX + 0.5V VMAX_HDRM
	 *   2 cycles intermediate drive with VMAX close to 3.5V
	 *   3 cycles steady drive with VMAX close to Vrms
	 */
	val[0] = 0xFF;
	val[1] = val[2] = 0xC9;
	val[3] = val[4] = val[5] = chip->config.vmax_mv * 0xFF / 4500;
	rc = nvmem_device_write(chip->hap_cfg_nvmem, HAP_AUTO_BRAKE_CAL_PTRN1_LSB0_OFFSET,
			AUTO_BRAKE_CAL_DRIVE_CYCLES, val);
	if (rc < 0) {
		dev_err(chip->dev, "set PTRN1_LSBx for auto brake cal failed, rc=%d\n", rc);
		return rc;
	}

	/* cache several registers setting before configure them for auto brake calibration */
	memcpy(backup, lra_config, sizeof(backup));
	for (i = 0; i < 4; i++) {
		rc = haptics_read(chip, chip->cfg_addr_base,
				backup[i].addr, &backup[i].val, 1);
		if (rc < 0)
			return rc;
	}

	/* Set 62.5% BRK_DUTY, enable AUTO brake, etc */
	for (i = 0; i < 4; i++) {
		rc = haptics_write(chip, chip->cfg_addr_base,
				lra_config[i].addr, &lra_config[i].val, 1);
		if (rc < 0)
			goto restore;
	}

	/* Clear calibration done flag 1st */
	val[0] = 0;
	rc = nvmem_device_write(chip->hap_cfg_nvmem, HAP_AUTO_BRAKE_CAL_DONE_OFFSET, 1, val);
	if (rc < 0) {
		dev_err(chip->dev, "clear auto brake cal done flag failed, rc=%d\n", rc);
		goto restore;
	}

	rc = haptics_clear_fault(chip);
	if (rc < 0)
		goto restore;

	/* Trigger PBS to start calibration */
	val[0] = HAP_AUTO_BRAKE_CAL_VAL;
	rc = nvmem_device_write(chip->hap_cfg_nvmem, PBS_ARG_REG, 1, val);
	if (rc < 0) {
		dev_err(chip->dev, "set PBS_ARG for auto brake cal failed, rc=%d\n", rc);
		goto restore;
	}

	val[0] = PBS_TRIG_CLR_VAL;
	rc = nvmem_device_write(chip->hap_cfg_nvmem, PBS_TRIG_CLR_REG, 1, val);
	if (rc < 0) {
		dev_err(chip->dev, "clear PBS_TRIG for auto brake cal failed, rc=%d\n", rc);
		goto restore;
	}

	val[0] = PBS_TRIG_SET_VAL;
	rc = nvmem_device_write(chip->hap_cfg_nvmem, PBS_TRIG_SET_REG, 1, val);
	if (rc < 0) {
		dev_err(chip->dev, "set PBS_TRIG for auto brake cal failed, rc=%d\n", rc);
		goto restore;
	}

	/* wait for ~800ms and then poll auto brake cal done flag with ~200ms timeout */
	msleep(AUTO_BRAKE_CAL_WAIT_MS);

	while (retry_count--) {
		rc = nvmem_device_read(chip->hap_cfg_nvmem,
				HAP_AUTO_BRAKE_CAL_DONE_OFFSET, 1, val);
		if (rc < 0) {
			dev_err(chip->dev, "read auto brake cal done flag failed, rc=%d\n", rc);
			goto restore;
		}

		if (val[0] & AUTO_BRAKE_CAL_DONE) {
			dev_info(chip->dev, "auto brake calibration is done\n");
			break;
		}

		usleep_range(AUTO_BRAKE_CAL_POLLING_STEP_US, AUTO_BRAKE_CAL_POLLING_STEP_US + 1);
	}

restore:
	/* restore haptics settings after auto brake calibration */
	for (i = 0; i < 4; i++)
		haptics_write(chip, chip->cfg_addr_base,
				backup[i].addr, &backup[i].val, 1);

	return rc;
}

static int haptics_start_lra_calibrate(struct haptics_chip *chip)
{
	int rc;

	mutex_lock(&chip->play.lock);
	/*
	 * Ignore calibration if it's in FIFO playing to avoid
	 * messing up the FIFO playing status
	 */
	if ((chip->play.pattern_src == FIFO) &&
			atomic_read(&chip->play.fifo_status.is_busy)) {
		dev_err(chip->dev, "In FIFO playing, ignore calibration\n");
		rc = -EBUSY;
		goto unlock;
	}

	/* Stop other mode playing if there is any */
	rc = haptics_enable_play(chip, false);
	if (rc < 0) {
		dev_err(chip->dev, "Stop playing failed, rc=%d\n", rc);
		goto unlock;
	}

	chip->play.in_calibration = true;
	rc = haptics_detect_lra_frequency(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Detect LRA frequency failed, rc=%d\n", rc);
		goto unlock;
	}

	/* Sleep at least 4ms to stabilize the LRA from frequency detection */
	usleep_range(4000, 5000);
	if (chip->config.measure_lra_impedance)
		rc = haptics_measure_realtime_lra_impedance(chip);
	else
		rc = haptics_detect_lra_impedance(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Detect LRA impedance failed, rc=%d\n", rc);
		goto unlock;
	}

	rc = haptics_start_auto_brake_calibration(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Run auto brake calibration failed, rc=%d\n", rc);
		goto unlock;
	}

unlock:
	haptics_clear_fault(chip);
	chip->play.in_calibration = false;
	mutex_unlock(&chip->play.lock);
	return rc;
}

static ssize_t lra_calibration_store(struct class *c,
		struct class_attribute *attr, const char *buf, size_t count)
{
	struct haptics_chip *chip = container_of(c,
			struct haptics_chip, hap_class);
	bool val;
	int rc;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	if (val) {
		rc = haptics_start_lra_calibrate(chip);
		if (rc < 0)
			return rc;
	}

	return count;
}
static CLASS_ATTR_WO(lra_calibration);

static ssize_t lra_frequency_hz_show(struct class *c,
		struct class_attribute *attr, char *buf)
{
	struct haptics_chip *chip = container_of(c,
			struct haptics_chip, hap_class);
	u32 cl_f_lra;

	if (chip->config.cl_t_lra_us == 0)
		return -EINVAL;

	cl_f_lra = USEC_PER_SEC / chip->config.cl_t_lra_us;
	return scnprintf(buf, PAGE_SIZE, "%d Hz\n", cl_f_lra);
}
static CLASS_ATTR_RO(lra_frequency_hz);

static ssize_t lra_impedance_show(struct class *c,
		struct class_attribute *attr, char *buf)
{
	struct haptics_chip *chip = container_of(c,
			struct haptics_chip, hap_class);

	if (chip->config.measure_lra_impedance)
		return scnprintf(buf, PAGE_SIZE, "measured %u mohms\n",
				chip->config.lra_measured_mohms);

	if (chip->config.lra_min_mohms == 0 && chip->config.lra_max_mohms == 0)
		return -EINVAL;
	else if (chip->config.lra_min_mohms == 0)
		return scnprintf(buf, PAGE_SIZE, "%s\n", "Short circuit");
	else if (chip->config.lra_max_mohms >= chip->config.lra_open_mohms)
		return scnprintf(buf, PAGE_SIZE, "%s\n", "Open circuit");
	else
		return scnprintf(buf, PAGE_SIZE, "%u ~ %u mohms\n",
				chip->config.lra_min_mohms,
				chip->config.lra_max_mohms);
}
static CLASS_ATTR_RO(lra_impedance);

static ssize_t primitive_duration_show(struct class *c,
		struct class_attribute *attr, char *buf)
{
	struct haptics_chip *chip = container_of(c,
			struct haptics_chip, hap_class);

	return scnprintf(buf, PAGE_SIZE, "%d\n", chip->primitive_duration);
}

static ssize_t primitive_duration_store(struct class *c,
		struct class_attribute *attr, const char *buf, size_t count)
{
	struct haptics_chip *chip = container_of(c,
			struct haptics_chip, hap_class);
	u16 primitive_id = 0;
	int i = 0;

	if (kstrtou16(buf, 0, &primitive_id))
		return -EINVAL;

	for (i = 0; i < chip->primitives_count; i++) {
		if (chip->primitives[i].id == primitive_id)
			break;
	}

	if (i == chip->primitives_count) {
		pr_err("Primitive id specified is incorrect\n");
		return -EINVAL;
	}

	chip->primitive_duration = get_play_length_effect_us(&chip->primitives[i]);

	return count;
}

static CLASS_ATTR_RW(primitive_duration);

static struct attribute *hap_class_attrs[] = {
	&class_attr_lra_calibration.attr,
	&class_attr_lra_frequency_hz.attr,
	&class_attr_lra_impedance.attr,
	&class_attr_primitive_duration.attr,
	NULL,
};
ATTRIBUTE_GROUPS(hap_class);

static bool is_swr_supported(struct haptics_chip *chip)
{
	/* HAP520_MV does not support soundwire */
	if (chip->hw_type == HAP520_MV)
		return false;

	return true;
}

static enum hrtimer_restart haptics_disable_hbst_timer(struct hrtimer *timer)
{
	struct haptics_chip *chip = container_of(timer,
			struct haptics_chip, hbst_off_timer);
	int rc;

	rc = haptics_boost_vreg_enable(chip, false);
	if (rc < 0)
		dev_err(chip->dev, "disable boost vreg failed, rc=%d\n", rc);
	else
		dev_dbg(chip->dev, "boost vreg is disabled\n");

	return HRTIMER_NORESTART;
}

static int haptics_boost_notifier(struct notifier_block *nb, unsigned long event, void *val)
{
	struct haptics_chip *chip = container_of(nb, struct haptics_chip, hboost_nb);
	u32 vmax_mv;
	int rc;

	switch (event) {
	case VMAX_CLAMP:
		vmax_mv = *(u32 *)val;
		if (vmax_mv > MAX_HV_VMAX_MV) {
			dev_err(chip->dev, "voted Vmax (%u mV) is higher than maximum (%u mV)\n",
					vmax_mv, MAX_HV_VMAX_MV);
			return -EINVAL;
		}

		chip->clamped_vmax_mv = vmax_mv;
		dev_dbg(chip->dev, "Vmax is clamped at %u mV to support hBoost concurrency\n",
				vmax_mv);
		rc = haptics_force_vreg_ready(chip, vmax_mv == MAX_HV_VMAX_MV ? false : true);
		if (rc < 0)
			return rc;
		break;
	default:
		break;
	}

	return 0;
}

static int haptics_probe(struct platform_device *pdev)
{
	struct haptics_chip *chip;
	struct input_dev *input_dev;
	struct ff_device *ff_dev;
	int rc, count;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	input_dev = devm_input_allocate_device(&pdev->dev);
	if (!input_dev)
		return -ENOMEM;

	chip->dev = &pdev->dev;
	chip->regmap = dev_get_regmap(chip->dev->parent, NULL);
	if (!chip->regmap) {
		dev_err(chip->dev, "Get regmap failed\n");
		return -ENXIO;
	}

	rc = haptics_parse_dt(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Parse device-tree failed, rc = %d\n", rc);
		return rc;
	}

	rc = haptics_init_custom_effect(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Init custom effect failed, rc=%d\n", rc);
		return rc;
	}

	mutex_init(&chip->vmax_lock);

	rc = haptics_hw_init(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Initialize HW failed, rc = %d\n", rc);
		return rc;
	}

	if (is_swr_supported(chip)) {
		rc = haptics_init_swr_slave_regulator(chip);
		if (rc < 0) {
			dev_err(chip->dev, "Initialize swr slave regulator failed, rc = %d\n",
					rc);
			return rc;
		}
	}

	rc = devm_request_threaded_irq(chip->dev, chip->fifo_empty_irq,
			NULL, fifo_empty_irq_handler,
			IRQF_ONESHOT, "fifo-empty", chip);
	if (rc < 0) {
		dev_err(chip->dev, "request fifo-empty IRQ failed, rc=%d\n",
				rc);
		return rc;
	}

	mutex_init(&chip->play.lock);
	disable_irq_nosync(chip->fifo_empty_irq);
	chip->fifo_empty_irq_en = false;
	hrtimer_init(&chip->hbst_off_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	chip->hbst_off_timer.function = haptics_disable_hbst_timer;
	INIT_DELAYED_WORK(&chip->stop_work, haptics_stop_constant_effect_play);
	INIT_WORK(&chip->set_gain_work, haptics_set_gain_work);

	atomic_set(&chip->play.fifo_status.is_busy, 0);
	atomic_set(&chip->play.fifo_status.written_done, 0);
	atomic_set(&chip->play.fifo_status.cancelled, 0);
	input_dev->name = "qcom-hv-haptics";
	input_set_drvdata(input_dev, chip);
	chip->input_dev = input_dev;

	input_set_capability(input_dev, EV_FF, FF_CONSTANT);
	input_set_capability(input_dev, EV_FF, FF_GAIN);
	if ((chip->effects_count != 0) || (chip->primitives_count != 0)) {
		input_set_capability(input_dev, EV_FF, FF_PERIODIC);
		input_set_capability(input_dev, EV_FF, FF_CUSTOM);
	}

	if (chip->effects_count + chip->primitives_count > MAX_EFFECT_COUNT)
		dev_err(chip->dev, "Effects count cannot be more than %d\n", MAX_EFFECT_COUNT);

	count = min_t(u32, chip->effects_count + chip->primitives_count + 1, MAX_EFFECT_COUNT);

	rc = input_ff_create(input_dev, count);
	if (rc < 0) {
		dev_err(chip->dev, "create input FF device failed, rc=%d\n",
				rc);
		return rc;
	}

	ff_dev = input_dev->ff;
	ff_dev->upload = haptics_upload_effect;
	ff_dev->playback = haptics_playback;
	ff_dev->erase = haptics_erase;
	ff_dev->set_gain = haptics_set_gain;
	rc = input_register_device(input_dev);
	if (rc < 0) {
		dev_err(chip->dev, "register input device failed, rc=%d\n",
				rc);
		goto destroy_ff;
	}

	dev_set_drvdata(chip->dev, chip);
	chip->hap_class.name = "qcom-haptics";
	chip->hap_class.class_groups = hap_class_groups;
	rc = class_register(&chip->hap_class);
	if (rc < 0) {
		dev_err(chip->dev, "register hap_class failed, rc=%d\n", rc);
		goto destroy_ff;
	}

	chip->hboost_nb.notifier_call = haptics_boost_notifier;
	register_hboost_event_notifier(&chip->hboost_nb);
#ifdef CONFIG_DEBUG_FS
	rc = haptics_create_debugfs(chip);
	if (rc < 0)
		dev_err(chip->dev, "Creating debugfs failed, rc=%d\n", rc);
#endif
	return 0;
destroy_ff:
	input_ff_destroy(chip->input_dev);
	return rc;
}

static int haptics_remove(struct platform_device *pdev)
{
	struct haptics_chip *chip = dev_get_drvdata(&pdev->dev);

	if (chip->pbs_node)
		of_node_put(chip->pbs_node);

	unregister_hboost_event_notifier(&chip->hboost_nb);
	class_unregister(&chip->hap_class);
#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(chip->debugfs_dir);
#endif
	input_unregister_device(chip->input_dev);
	dev_set_drvdata(chip->dev, NULL);

	return 0;
}

static void haptics_ds_suspend_config(struct device *dev)
{
	struct haptics_chip *chip = dev_get_drvdata(dev);

	if (chip->fifo_empty_irq > 0)
		devm_free_irq(dev, chip->fifo_empty_irq, chip);
}

static int haptics_ds_resume_config(struct device *dev)
{
	struct haptics_chip *chip = dev_get_drvdata(dev);
	int rc = 0;

	rc = haptics_hw_init(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Initialize HW failed, rc = %d\n", rc);
		return rc;
	}

	if (chip->fifo_empty_irq > 0) {
		rc = devm_request_threaded_irq(chip->dev, chip->fifo_empty_irq,
					NULL, fifo_empty_irq_handler,
					IRQF_ONESHOT, "fifo-empty", chip);
		if (rc < 0) {
			dev_err(chip->dev, "request fifo-empty IRQ failed, rc=%d\n",
					rc);
			return rc;
		}

		disable_irq_nosync(chip->fifo_empty_irq);
		chip->fifo_empty_irq_en = false;
	}

	return rc;
}

#ifdef CONFIG_PM_SLEEP
static int haptics_suspend_config(struct device *dev)
{
	struct haptics_chip *chip = dev_get_drvdata(dev);
	struct haptics_play_info *play = &chip->play;
	int rc;

	mutex_lock(&play->lock);
	if ((play->pattern_src == FIFO) &&
			atomic_read(&play->fifo_status.is_busy)) {
		if (atomic_read(&play->fifo_status.written_done) == 0) {
			dev_dbg(chip->dev, "cancelling FIFO playing\n");
			atomic_set(&play->fifo_status.cancelled, 1);
		}

		rc = haptics_stop_fifo_play(chip);
		if (rc < 0) {
			dev_err(chip->dev, "stop FIFO playing failed, rc=%d\n", rc);
			mutex_unlock(&play->lock);
			return rc;
		}
	} else {
		rc = haptics_enable_play(chip, false);
		if (rc < 0) {
			mutex_unlock(&play->lock);
			return rc;
		}
	}
	mutex_unlock(&play->lock);

	/*
	 * Cancel the hBoost turning off timer and disable
	 * hBoost if it's still enabled
	 */
	if (chip->wa_flags & SW_CTRL_HBST) {
		hrtimer_cancel(&chip->hbst_off_timer);
		haptics_boost_vreg_enable(chip, false);
	}

	rc = haptics_enable_hpwr_vreg(chip, false);
	if (rc < 0)
		return rc;

	return haptics_module_enable(chip, false);
}

static int haptics_suspend(struct device *dev)
{
	struct haptics_chip *chip = dev_get_drvdata(dev);
	int rc = 0;

	if (chip->cfg_revision == HAP_CFG_V1)
		return 0;

	rc = haptics_suspend_config(dev);
	if (rc < 0)
		return rc;

#ifdef CONFIG_DEEPSLEEP
	if (mem_sleep_current == PM_SUSPEND_MEM)
		haptics_ds_suspend_config(dev);
#endif

	return 0;
}

static int haptics_resume(struct device *dev)
{
	struct haptics_chip *chip = dev_get_drvdata(dev);

#ifdef CONFIG_DEEPSLEEP
	if (mem_sleep_current == PM_SUSPEND_MEM) {
		int rc = 0;

		rc = haptics_ds_resume_config(dev);
		if (rc < 0)
			return rc;
	}
#endif

	return haptics_module_enable(chip, true);
}
#endif

static int haptics_freeze(struct device *dev)
{
	int rc = 0;

	rc = haptics_suspend_config(dev);
	if (rc < 0)
		return rc;

	haptics_ds_suspend_config(dev);

	return 0;
}

static int haptics_restore(struct device *dev)
{
	struct haptics_chip *chip = dev_get_drvdata(dev);
	int rc = 0;

	rc = haptics_ds_resume_config(dev);
	if (rc < 0)
		return rc;

	return haptics_module_enable(chip, true);

}

static void haptics_shutdown(struct platform_device *pdev)
{
	struct haptics_chip *chip = platform_get_drvdata(pdev);

	haptics_suspend_config(chip->dev);

	haptics_ds_suspend_config(chip->dev);
}

static const struct dev_pm_ops haptics_pm_ops = {
	.suspend = haptics_suspend,
	.resume = haptics_resume,
	.freeze = haptics_freeze,
	.restore = haptics_restore,
};

static const struct of_device_id haptics_match_table[] = {
	{
		.compatible = "qcom,hv-haptics",
	},
	{
		.compatible = "qcom,pm8350b-haptics",
	},
	{
		.compatible = "qcom,pm5100-haptics",
	},
	{},
};

static struct platform_driver haptics_driver = {
	.driver		= {
		.name = "qcom-hv-haptics",
		.of_match_table = haptics_match_table,
		.pm		= &haptics_pm_ops,
	},
	.probe		= haptics_probe,
	.shutdown	= haptics_shutdown,
	.remove		= haptics_remove,
};
module_platform_driver(haptics_driver);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. High-Voltage Haptics driver");
MODULE_LICENSE("GPL v2");
