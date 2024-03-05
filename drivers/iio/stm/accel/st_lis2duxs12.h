/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics st_lis2duxs12 sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2022 STMicroelectronics Inc.
 */

#ifndef ST_LIS2DUXS12_H
#define ST_LIS2DUXS12_H

#include <linux/device.h>
#include <linux/iio/iio.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/bitfield.h>

#include "../common/stm_iio_types.h"

#define ST_LIS2DUXS12_ODR_EXPAND(odr, uodr)	(((odr) * 1000000) + (uodr))

#define ST_LIS2DUX12_DEV_NAME			"lis2dux12"
#define ST_LIS2DUXS12_DEV_NAME			"lis2duxs12"

/* default configuration values */
#define ST_LIS2DUXS12_DEFAULT_WK_TH		154000

#define ST_LIS2DUXS12_PIN_CTRL_ADDR		0x0c
#define ST_LIS2DUXS12_PP_OD_MASK		BIT(1)
#define ST_LIS2DUXS12_CS_PU_DIS_MASK		BIT(2)
#define ST_LIS2DUXS12_H_LACTIVE_MASK		BIT(3)
#define ST_LIS2DUXS12_PD_DIS_INT1_MASK		BIT(4)
#define ST_LIS2DUXS12_PD_DIS_INT2_MASK		BIT(5)
#define ST_LIS2DUXS12_SDA_PU_EN_MASK		BIT(6)
#define ST_LIS2DUXS12_SDO_PU_EN_MASK		BIT(7)

#define ST_LIS2DUXS12_WHOAMI_ADDR		0x0f
#define ST_LIS2DUXS12_WHOAMI_VAL		0x47

#define ST_LIS2DUXS12_CTRL1_ADDR		0x10
#define ST_LIS2DUXS12_WU_EN_MASK		GENMASK(2, 0)
#define ST_LIS2DUXS12_DRDY_PULSED_MASK		BIT(3)
#define ST_LIS2DUXS12_IF_ADD_INC_MASK		BIT(4)
#define ST_LIS2DUXS12_SW_RESET_MASK		BIT(5)
#define ST_LIS2DUXS12_INT1_ON_RES_MASK		BIT(6)

#define ST_LIS2DUXS12_CTRL2_ADDR		0x11
#define ST_LIS2DUXS12_CTRL3_ADDR		0x12
#define ST_LIS2DUXS12_HP_EN_MASK		BIT(2)
#define ST_LIS2DUXS12_INT_FIFO_TH_MASK		BIT(5)

#define ST_LIS2DUXS12_CTRL4_ADDR		0x13
#define ST_LIS2DUXS12_BOOT_MASK			BIT(0)
#define ST_LIS2DUXS12_SOC_MASK			BIT(1)
#define ST_LIS2DUXS12_FIFO_EN_MASK		BIT(3)
#define ST_LIS2DUXS12_EMB_FUNC_EN_MASK		BIT(4)
#define ST_LIS2DUXS12_BDU_MASK			BIT(5)
#define ST_LIS2DUXS12_INACT_ODR_MASK		GENMASK(7, 6)

#define ST_LIS2DUXS12_CTRL5_ADDR		0x14
#define ST_LIS2DUXS12_FS_MASK			GENMASK(1, 0)
#define ST_LIS2DUXS12_ODR_MASK			GENMASK(7, 4)

#define ST_LIS2DUXS12_FIFO_CTRL_ADDR		0x15
#define ST_LIS2DUXS12_FIFO_MODE_MASK		GENMASK(2, 0)

#define ST_LIS2DUXS12_FIFO_WTM_ADDR		0x16
#define ST_LIS2DUXS12_FIFO_WTM_MASK		GENMASK(6, 0)
#define ST_LIS2DUXS12_XL_ONLY_FIFO_MASK		BIT(7)

#define ST_LIS2DUXS12_INTERRUPT_CFG_ADDR	0x17
#define ST_LIS2DUXS12_INTERRUPTS_ENABLE_MASK	BIT(0)
#define ST_LIS2DUXS12_LIR_MASK			BIT(1)
#define ST_LIS2DUXS12_TIMESTAMP_EN_MASK		BIT(7)

#define ST_LIS2DUXS12_SIXD_ADDR			0x18
#define ST_LIS2DUXS12_D6D_THS_MASK		GENMASK(6, 5)

#define ST_LIS2DUXS12_WAKE_UP_THS_ADDR		0x1c
#define ST_LIS2DUXS12_WK_THS_MASK		GENMASK(5, 0)
#define ST_LIS2DUXS12_SLEEP_ON_MASK		BIT(6)

#define ST_LIS2DUXS12_WAKE_UP_DUR_ADDR		0x1d
#define ST_LIS2DUXS12_SLEEP_DUR_MASK		GENMASK(3, 0)
#define ST_LIS2DUXS12_WAKE_DUR_MASK		GENMASK(6, 5)
#define ST_LIS2DUXS12_FF_DUR5_MASK		BIT(7)

#define ST_LIS2DUXS12_FREE_FALL_ADDR		0x1e
#define ST_LIS2DUXS12_FF_THS_MASK		GENMASK(2, 0)
#define ST_LIS2DUXS12_FF_DUR_MASK		GENMASK(7, 3)
#define ST_LIS2DUXS12_FF_DUR5_MASK		BIT(7)

#define ST_LIS2DUXS12_MD1_CFG_ADDR		0x1f
#define ST_LIS2DUXS12_MD2_CFG_ADDR		0x20
#define ST_LIS2DUXS12_INT_SLEEP_CHANGE_MASK	BIT(7)
#define ST_LIS2DUXS12_INT_WU_MASK		BIT(5)
#define ST_LIS2DUXS12_INT_FF_MASK		BIT(4)
#define ST_LIS2DUXS12_INT_TAP_MASK		BIT(3)
#define ST_LIS2DUXS12_INT_6D_MASK		BIT(2)
#define ST_LIS2DUXS12_INT_TIMESTAMP_MASK	BIT(1)
#define ST_LIS2DUXS12_INT_EMB_FUNC_MASK		BIT(0)

#define ST_LIS2DUXS12_WAKE_UP_SRC_ADDR		0x21
#define ST_LIS2DUXS12_WK_MASK			GENMASK(2, 0)
#define ST_LIS2DUXS12_WU_IA_MASK		BIT(3)
#define ST_LIS2DUXS12_SLEEP_STATE_MASK		BIT(4)
#define ST_LIS2DUXS12_FF_IA_MASK		BIT(5)
#define ST_LIS2DUXS12_SLEEP_CHANGE_IA_MASK	BIT(6)

#define ST_LIS2DUXS12_TAP_SRC_ADDR		0x22
#define ST_LIS2DUXS12_TRIPLE_TAP_IA_MASK	BIT(4)
#define ST_LIS2DUXS12_DOUBLE_TAP_IA_MASK	BIT(5)
#define ST_LIS2DUXS12_SINGLE_TAP_IA_MASK	BIT(6)
#define ST_LIS2DUXS12_TAP_IA_MASK		BIT(7)

#define ST_LIS2DUXS12_SIXD_SRC_ADDR		0x23
#define ST_LIS2DUXS12_X_Y_Z_MASK		GENMASK(5, 0)
#define ST_LIS2DUXS12_D6D_IA_MASK		BIT(6)

#define ST_LIS2DUXS12_ALL_INT_SRC_ADDR		0x24
#define ST_LIS2DUXS12_FF_IA_ALL_MASK		BIT(0)
#define ST_LIS2DUXS12_WU_IA_ALL_MASK		BIT(1)
#define ST_LIS2DUXS12_SINGLE_TAP_ALL_MASK	BIT(2)
#define ST_LIS2DUXS12_DOUBLE_TAP_ALL_MASK	BIT(3)
#define ST_LIS2DUXS12_TRIPLE_TAP_ALL_MASK	BIT(4)
#define ST_LIS2DUXS12_D6D_IA_ALL_MASK		BIT(5)
#define ST_LIS2DUXS12_SLEEP_CHANGE_ALL_MASK	BIT(6)

#define ST_LIS2DUXS12_STATUS_ADDR		0x25
#define ST_LIS2DUXS12_DRDY_MASK			BIT(0)
#define ST_LIS2DUXS12_INT_GLOBAL_MASK		BIT(5)

#define ST_LIS2DUXS12_FIFO_STATUS1_ADDR		0x26
#define ST_LIS2DUXS12_FIFO_WTM_IA_MASK		BIT(7)

#define ST_LIS2DUXS12_FIFO_STATUS2_ADDR		0x27
#define ST_LIS2DUXS12_FIFO_FSS_MASK		GENMASK(7, 0)

#define ST_LIS2DUXS12_OUT_X_L_ADDR		0x28
#define ST_LIS2DUXS12_OUT_Y_L_ADDR		0x2a
#define ST_LIS2DUXS12_OUT_Z_L_ADDR		0x2c
#define ST_LIS2DUXS12_OUT_T_AH_QVAR_L_ADDR	0x2e

#define ST_LIS2DUXS12_AH_QVAR_CFG_ADDR		0x31
#define ST_LIS2DUXS12_AH_QVAR_EN_MASK		BIT(7)
#define ST_LIS2DUXS12_AH_QVAR_NOTCH_EN_MASK	BIT(6)
#define ST_LIS2DUXS12_AH_QVAR_NOTCH_CUTOFF_MASK	BIT(5)
#define ST_LIS2DUXS12_AH_QVAR_C_ZIN_MASK	GENMASK(4, 3)
#define ST_LIS2DUXS12_AH_QVAR_GAIN_MASK		GENMASK(2, 1)

#define ST_LIS2DUXS12_SELF_TEST_ADDR		0x32
#define ST_LIS2DUXS12_ST_MASK			GENMASK(5, 4)

#define ST_LIS2DUXS12_EMB_FUNC_STATUS_MAINPAGE_ADDR	0x34
#define ST_LIS2DUXS12_IS_STEP_DET_MASK		BIT(3)
#define ST_LIS2DUXS12_IS_TILT_MASK		BIT(4)
#define ST_LIS2DUXS12_IS_SIGMOT_MASK		BIT(5)

#define ST_LIS2DUXS12_FSM_STATUS_MAINPAGE_ADDR	0x35
#define ST_LIS2DUXS12_MLC_STATUS_MAINPAGE_ADDR	0x36

#define ST_LIS2DUXS12_FUNC_CFG_ACCESS_ADDR	0x3f
#define ST_LIS2DUXS12_EMB_FUNC_REG_ACCESS_MASK	BIT(7)
#define ST_LIS2DUXS12_FSM_WR_CTRL_EN_MASK	BIT(0)

#define ST_LIS2DUXS12_FIFO_DATA_OUT_TAG_ADDR	0x40

#define ST_LIS2DUXS12_FIFO_BATCH_DEC_ADDR	0x47
#define ST_LIS2DUXS12_BDR_XL_MASK		GENMASK(2, 0)
#define ST_LIS2DUXS12_DEC_TS_MASK		GENMASK(4, 3)

#define ST_LIS2DUXS12_TAP_CFG0_ADDR		0x6f
#define ST_LIS2DUXS12_AXIS_MASK			GENMASK(7, 6)
#define ST_LIS2DUXS12_INVERT_T_MASK		GENMASK(5, 1)

#define ST_LIS2DUXS12_TAP_CFG1_ADDR		0x70
#define ST_LIS2DUXS12_POST_STILL_T_MASK		GENMASK(3, 0)
#define ST_LIS2DUXS12_PRE_STILL_THS_MASK	GENMASK(7, 4)

#define ST_LIS2DUXS12_TAP_CFG2_ADDR		0x71
#define ST_LIS2DUXS12_WAIT_T_MASK		GENMASK(5, 0)
#define ST_LIS2DUXS12_POST_STILL_TH_MASK	GENMASK(7, 6)

#define ST_LIS2DUXS12_TAP_CFG3_ADDR		0x72
#define ST_LIS2DUXS12_LATENCY_T_MASK		GENMASK(3, 0)
#define ST_LIS2DUXS12_POST_STILL_THS_MASK	GENMASK(7, 4)

#define ST_LIS2DUXS12_TAP_CFG4_ADDR		0x73
#define ST_LIS2DUXS12_PEAK_THS_MASK		GENMASK(5, 0)
#define ST_LIS2DUXS12_WAIT_END_LATENCY_MASK	BIT(7)

#define ST_LIS2DUXS12_TAP_CFG5_ADDR		0x74
#define ST_LIS2DUXS12_REBOUND_T_MASK		GENMASK(4, 0)
#define ST_LIS2DUXS12_SINGLE_TAP_EN_MASK	BIT(5)
#define ST_LIS2DUXS12_DOUBLE_TAP_EN_MASK	BIT(6)
#define ST_LIS2DUXS12_TRIPLE_TAP_EN_MASK	BIT(7)

#define ST_LIS2DUXS12_TAP_CFG6_ADDR		0x75
#define ST_LIS2DUXS12_PRE_STILL_N_MASK		GENMASK(3, 0)
#define ST_LIS2DUXS12_PRE_STILL_ST_MASK		GENMASK(7, 4)

#define ST_LIS2DUXS12_TIMESTAMP2_ADDR		0x7c

#define ST_LIS2DUXS12_SELFTEST_ACCEL_MIN	204
#define ST_LIS2DUXS12_SELFTEST_ACCEL_MAX	4918

/* embedded function registers */
#define ST_LIS2DUXS12_PAGE_SEL_ADDR		0x02
#define ST_LIS2DUXS12_EMB_FUNC_EN_A_ADDR	0x04
#define ST_LIS2DUXS12_PEDO_EN_MASK		BIT(3)
#define ST_LIS2DUXS12_TILT_EN_MASK		BIT(4)
#define ST_LIS2DUXS12_SIGN_MOTION_EN_MASK	BIT(5)
#define ST_LIS2DUXS12_MLC_BEFORE_FSM_EN_MASK	BIT(7)

#define ST_LIS2DUXS12_EMB_FUNC_EN_B_ADDR	0x05
#define ST_LIS2DUXS12_FSM_EN_MASK		BIT(0)
#define ST_LIS2DUXS12_MLC_EN_MASK		BIT(4)

#define ST_LIS2DUXS12_EMB_FUNC_EXEC_STATUS_ADDR	0x07
#define ST_LIS2DUXS12_FUNC_ENDOP_MASK		BIT(0)
#define ST_LIS2DUXS12_FUNC_EXEC_OVR_MASK	BIT(1)

#define ST_LIS2DUXS12_PAGE_ADDRESS_ADDR		0x08
#define ST_LIS2DUXS12_PAGE_VALUE_ADDR		0x09

#define ST_LIS2DUXS12_EMB_FUNC_INT1_ADDR	0x0a
#define ST_LIS2DUXS12_INT_STEP_DETECTOR_MASK	BIT(3)
#define ST_LIS2DUXS12_INT_TILT_MASK		BIT(4)
#define ST_LIS2DUXS12_INT_SIG_MOT_MASK		BIT(5)

#define ST_LIS2DUXS12_FSM_INT1_ADDR		0x0b
#define ST_LIS2DUXS12_MLC_INT1_ADDR		0x0d
#define ST_LIS2DUXS12_EMB_FUNC_INT2_ADDR	0x0e
#define ST_LIS2DUXS12_FSM_INT2_ADDR		0x0f
#define ST_LIS2DUXS12_MLC_INT2_ADDR		0x11

#define ST_LIS2DUXS12_EMB_FUNC_STATUS_ADDR	0x12
#define ST_LIS2DUXS12_EMB_IS_STEP_DET_MASK	BIT(3)
#define ST_LIS2DUXS12_EMB_IS_TILT_MASK		BIT(4)
#define ST_LIS2DUXS12_EMB_IS_SIGMOT_MASK	BIT(5)

#define ST_LIS2DUXS12_FSM_STATUS_ADDR		0x13
#define ST_LIS2DUXS12_MLC_STATUS_ADDR		0x15

#define ST_LIS2DUXS12_PAGE_RW_ADDR		0x17
#define ST_LIS2DUXS12_EMB_FUNC_LIR_MASK		BIT(7)

#define ST_LIS2DUXS12_EMB_FUNC_FIFO_EN_ADDR	0x18
#define ST_LIS2DUXS12_STEP_COUNTER_FIFO_EN_MASK	BIT(0)
#define ST_LIS2DUXS12_MLC_FIFO_EN_MASK		BIT(1)
#define ST_LIS2DUXS12_MLC_FILTER_FEATURE_FIFO_EN_MASK BIT(2)
#define ST_LIS2DUXS12_FSM_FIFO_EN_MASK		BIT(3)

#define ST_LIS2DUXS12_FSM_ENABLE_ADDR		0x1a
#define ST_LIS2DUXS12_FSM_OUTS1_ADDR		0x20

#define ST_LIS2DUXS12_STEP_COUNTER_L_ADDR	0x28
#define ST_LIS2DUXS12_STEP_COUNTER_H_ADDR	0x29

#define ST_LIS2DUXS12_EMB_FUNC_SRC_ADDR		0x2a
#define ST_LIS2DUXS12_STEP_DETECTED_MASK	BIT(5)
#define ST_LIS2DUXS12_PEDO_RST_STEP_MASK	BIT(7)

#define ST_LIS2DUXS12_EMB_FUNC_INIT_A_ADDR	0x2c
#define ST_LIS2DUXS12_STEP_DET_INIT_MASK	BIT(3)
#define ST_LIS2DUXS12_TILT_INIT_MASK		BIT(4)
#define ST_LIS2DUXS12_SIG_MOT_INIT_MASK		BIT(5)
#define ST_LIS2DUXS12_MLC_BEFORE_FSM_INIT_MASK	BIT(7)

#define ST_LIS2DUXS12_EMB_FUNC_INIT_B_ADDR	0x2d
#define ST_LIS2DUXS12_FSM_INIT_MASK		BIT(0)
#define ST_LIS2DUXS12_MLC_INIT_MASK		BIT(4)

#define ST_LIS2DUXS12_MLC1_SRC_ADDR		0x34

#define ST_LIS2DUXS12_FSM_ODR_ADDR		0x39
#define ST_LIS2DUXS12_FSM_ODR_MASK		GENMASK(5, 3)

#define ST_LIS2DUXS12_MLC_ODR_ADDR		0x3a
#define ST_LIS2DUXS12_MLC_ODR_MASK		GENMASK(6, 4)

/* Timestamp Tick 10us/LSB */
#define ST_LIS2DUXS12_TS_DELTA_NS		10000ULL

/* Temperature in uC */
#define ST_LIS2DUXS12_TEMP_GAIN			256
#define ST_LIS2DUXS12_TEMP_OFFSET		6400

/* FIFO simple size and depth */
#define ST_LIS2DUXS12_SAMPLE_SIZE		6
#define ST_LIS2DUXS12_TS_SAMPLE_SIZE		4
#define ST_LIS2DUXS12_TAG_SIZE			1
#define ST_LIS2DUXS12_FIFO_SAMPLE_SIZE		(ST_LIS2DUXS12_SAMPLE_SIZE + \
						 ST_LIS2DUXS12_TAG_SIZE)
#define ST_LIS2DUXS12_MAX_FIFO_DEPTH		127

struct __packed raw_data_compact_t {
	__le16 x:12;
	__le16 y:12;
	__le16 z:12;
	__le16 t:12;
};

struct __packed raw_data_t {
	__le16 x;
	__le16 y;
	__le16 z;
};

#define ST_LIS2DUXS12_DATA_CHANNEL(chan_type, addr, mod, ch2, scan_idx,	\
				rb, sb, sg, ext_inf)			\
{									\
	.type = chan_type,						\
	.address = addr,						\
	.modified = mod,						\
	.channel2 = ch2,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |			\
			      BIT(IIO_CHAN_INFO_SCALE),			\
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.scan_index = scan_idx,						\
	.scan_type = {							\
		.sign = sg,						\
		.realbits = rb,						\
		.storagebits = sb,					\
		.shift = sb - rb,					\
		.endianness = IIO_LE,					\
	},								\
	.ext_info = ext_inf,						\
}

static const struct iio_event_spec st_lis2duxs12_flush_event = {
	.type = STM_IIO_EV_TYPE_FIFO_FLUSH,
	.dir = IIO_EV_DIR_EITHER,
};

static const struct iio_event_spec st_lis2duxs12_thr_event = {
	.type = IIO_EV_TYPE_THRESH,
	.dir = IIO_EV_DIR_RISING,
	.mask_separate = BIT(IIO_EV_INFO_ENABLE),
};

#define ST_LIS2DUXS12_EVENT_CHANNEL(ctype, etype)	\
{							\
	.type = ctype,					\
	.modified = 0,					\
	.scan_index = -1,				\
	.indexed = -1,					\
	.event_spec = &st_lis2duxs12_##etype##_event,	\
	.num_event_specs = 1,				\
}

#define ST_LIS2DUXS12_SHIFT_VAL(val, mask)	(((val) << __ffs(mask)) & (mask))
#define ST_LIS2DUXS12_DESHIFT_VAL(val, mask)	(((val) & (mask)) >> __ffs(mask))

enum st_lis2duxs12_pm_t {
	ST_LIS2DUXS12_LP_MODE = 0,
	ST_LIS2DUXS12_HP_MODE,
	ST_LIS2DUXS12_NO_MODE,
};

enum st_lis2duxs12_fsm_mlc_enable_id {
	ST_LIS2DUXS12_MLC_FSM_DISABLED = 0,
	ST_LIS2DUXS12_MLC_ENABLED = BIT(0),
	ST_LIS2DUXS12_FSM_ENABLED = BIT(1),
};

/**
 * struct mlc_config_t -
 * @mlc_int_addr: interrupt register address.
 * @mlc_int_mask: interrupt register mask.
 * @fsm_int_addr: interrupt register address.
 * @fsm_int_mask: interrupt register mask.
 * @mlc_configured: number of mlc configured.
 * @fsm_configured: number of fsm configured.
 * @bin_len: fw binary size.
 * @requested_odr: Min ODR requested to works properly.
 * @requested_device: Device bitmask requested by firmware.
 * @status: MLC / FSM enabled status.
 */
struct st_lis2duxs12_mlc_config_t {
	uint8_t mlc_int_addr;
	uint8_t mlc_int_mask;
	uint8_t fsm_int_addr;
	uint8_t fsm_int_mask;
	uint8_t mlc_configured;
	uint8_t fsm_configured;
	uint16_t bin_len;
	uint16_t requested_odr;
	uint32_t requested_device;
	enum st_lis2duxs12_fsm_mlc_enable_id status;
};

/**
 * struct st_lis2duxs12_ff_th - Free Fall threshold table
 * @mg: Threshold in mg.
 * @val: Register value.
 */
struct st_lis2duxs12_ff_th {
	u32 mg;
	u8 val;
};

/**
 * struct st_lis2duxs12_6D_th - 6D threshold table
 * @deg: Threshold in degrees.
 * @val: Register value.
 */
struct st_lis2duxs12_6D_th {
	u8 deg;
	u8 val;
};

/**
 * struct st_lis2duxs12_reg - Generic sensor register description addr +
 *                            mask
 * @addr: Address of register.
 * @mask: Bitmask register for proper usage.
 */
struct st_lis2duxs12_reg {
	u8 addr;
	u8 mask;
};

/**
 * Define embedded functions register access
 *
 * FUNC_CFG_ACCESS_0 is default bank
 * FUNC_CFG_ACCESS_FUNC_CFG Enable access to the embedded functions
 *                          configuration registers.
 */
enum st_lis2duxs12_page_sel_register {
	FUNC_CFG_ACCESS_0 = 0,
	FUNC_CFG_ACCESS_FUNC_CFG,
};

/**
 * struct st_lis2duxs12_odr - Single ODR entry
 * @hz: Most significant part of the sensor ODR (Hz).
 * @uhz: Less significant part of the sensor ODR (micro Hz).
 * @val: ODR register value.
 */
struct st_lis2duxs12_odr {
	u16 hz;
	u32 uhz;
	u8 val;
};

/**
 * struct st_lis2duxs12_odr_table_entry - Sensor ODR table
 * @size: Size of ODR table.
 * @reg: ODR register.
 * @pm: Power mode register.
 * @batching_reg: ODR register for batching on fifo.
 * @odr_avl: Array of supported ODR value.
 */
struct st_lis2duxs12_odr_table_entry {
	u8 size;
	struct st_lis2duxs12_reg reg;
	struct st_lis2duxs12_reg pm;
	struct st_lis2duxs12_odr odr_avl[10];
};

/**
 * struct st_lis2duxs12_fs - Full Scale sensor table entry
 * @gain: Sensor sensitivity (mdps/LSB, mg/LSB and uC/LSB).
 * @val: FS register value.
 */
struct st_lis2duxs12_fs {
	u32 gain;
	u8 val;
};

/**
 * struct st_lis2duxs12_fs_table_entry - Full Scale sensor table
 * @size: Full Scale sensor table size.
 * @reg: Register description for FS settings.
 * @fs_avl: Full Scale list entries.
 */
struct st_lis2duxs12_fs_table_entry {
	u8 size;
	struct st_lis2duxs12_reg reg;
	struct st_lis2duxs12_fs fs_avl[4];
};

enum st_lis2duxs12_sensor_id {
	ST_LIS2DUXS12_ID_ACC = 0,
	ST_LIS2DUXS12_ID_TEMP,
	ST_LIS2DUXS12_ID_STEP_COUNTER,
	ST_LIS2DUXS12_ID_STEP_DETECTOR,
	ST_LIS2DUXS12_ID_SIGN_MOTION,
	ST_LIS2DUXS12_ID_TILT,
	ST_LIS2DUXS12_ID_QVAR,
	ST_LIS2DUXS12_ID_FF,
	ST_LIS2DUXS12_ID_SC,
	ST_LIS2DUXS12_ID_WK,
	ST_LIS2DUXS12_ID_6D,
	ST_LIS2DUXS12_ID_TAP,
	ST_LIS2DUXS12_ID_DTAP,
	ST_LIS2DUXS12_ID_TTAP,
	ST_LIS2DUXS12_ID_MLC,
	ST_LIS2DUXS12_ID_MLC_0,
	ST_LIS2DUXS12_ID_MLC_1,
	ST_LIS2DUXS12_ID_MLC_2,
	ST_LIS2DUXS12_ID_MLC_3,
	ST_LIS2DUXS12_ID_FSM_0,
	ST_LIS2DUXS12_ID_FSM_1,
	ST_LIS2DUXS12_ID_FSM_2,
	ST_LIS2DUXS12_ID_FSM_3,
	ST_LIS2DUXS12_ID_FSM_4,
	ST_LIS2DUXS12_ID_FSM_5,
	ST_LIS2DUXS12_ID_FSM_6,
	ST_LIS2DUXS12_ID_FSM_7,
	ST_LIS2DUXS12_ID_MAX,
};

static const enum
st_lis2duxs12_sensor_id st_lis2duxs12_buffered_sensor_list[] = {
	[0] = ST_LIS2DUXS12_ID_ACC,
	[1] = ST_LIS2DUXS12_ID_TEMP,
	[2] = ST_LIS2DUXS12_ID_STEP_COUNTER,
	[3] = ST_LIS2DUXS12_ID_QVAR,
};

#define ST_LIS2DUXS12_BUFFERED_ENABLED (BIT(ST_LIS2DUXS12_ID_ACC) | \
					BIT(ST_LIS2DUXS12_ID_TEMP) | \
					BIT(ST_LIS2DUXS12_ID_STEP_COUNTER) | \
					BIT(ST_LIS2DUXS12_ID_QVAR))

static const enum
st_lis2duxs12_sensor_id st_lis2duxs12_mlc_sensor_list[] = {
	[0] = ST_LIS2DUXS12_ID_MLC_0,
	[1] = ST_LIS2DUXS12_ID_MLC_1,
	[2] = ST_LIS2DUXS12_ID_MLC_2,
	[3] = ST_LIS2DUXS12_ID_MLC_3,
};

static const enum
st_lis2duxs12_sensor_id st_lis2duxs12_fsm_sensor_list[] = {
	[0] = ST_LIS2DUXS12_ID_FSM_0,
	[1] = ST_LIS2DUXS12_ID_FSM_1,
	[2] = ST_LIS2DUXS12_ID_FSM_2,
	[3] = ST_LIS2DUXS12_ID_FSM_3,
	[4] = ST_LIS2DUXS12_ID_FSM_4,
	[5] = ST_LIS2DUXS12_ID_FSM_5,
	[6] = ST_LIS2DUXS12_ID_FSM_6,
	[7] = ST_LIS2DUXS12_ID_FSM_7,
};

#define ST_LIS2DUXS12_EMB_FUNC_ENABLED (BIT(ST_LIS2DUXS12_ID_STEP_DETECTOR) | \
					BIT(ST_LIS2DUXS12_ID_SIGN_MOTION)   | \
					BIT(ST_LIS2DUXS12_ID_TILT))

#define ST_LIS2DUXS12_BASIC_FUNC_ENABLED (GENMASK(ST_LIS2DUXS12_ID_TTAP, \
						  ST_LIS2DUXS12_ID_FF))

/* HW devices that can wakeup the target */
#define ST_LIS2DUXS12_WAKE_UP_SENSORS (BIT(ST_LIS2DUXS12_ID_ACC)    | \
				       BIT(ST_LIS2DUXS12_ID_MLC_0)  | \
				       BIT(ST_LIS2DUXS12_ID_MLC_1)  | \
				       BIT(ST_LIS2DUXS12_ID_MLC_2)  | \
				       BIT(ST_LIS2DUXS12_ID_MLC_3)  | \
				       BIT(ST_LIS2DUXS12_ID_FSM_0)  | \
				       BIT(ST_LIS2DUXS12_ID_FSM_1)  | \
				       BIT(ST_LIS2DUXS12_ID_FSM_2)  | \
				       BIT(ST_LIS2DUXS12_ID_FSM_3)  | \
				       BIT(ST_LIS2DUXS12_ID_FSM_4)  | \
				       BIT(ST_LIS2DUXS12_ID_FSM_5)  | \
				       BIT(ST_LIS2DUXS12_ID_FSM_6)  | \
				       BIT(ST_LIS2DUXS12_ID_FSM_7))

/* this is the minimal ODR for wake-up sensors and dependencies */
#define ST_LIS2DUXS12_MIN_ODR_IN_WAKEUP		25

enum st_lis2duxs12_fifo_mode {
	ST_LIS2DUXS12_FIFO_BYPASS = 0x0,
	ST_LIS2DUXS12_FIFO_CONT = 0x6,
};

enum {
	ST_LIS2DUXS12_HW_FLUSH,
	ST_LIS2DUXS12_HW_OPERATIONAL,
};

enum st_lis2duxs12_hw_id {
	ST_LIS2DUX12_ID,
	ST_LIS2DUXS12_ID,
	ST_LIS2DUXS12_MAX_ID,
};

/**
 * struct st_lis2duxs12_settings - ST IMU sensor settings
 *
 * @hw_id: Hw id supported by the driver configuration.
 * @name: Device name supported by the driver configuration.
 * @st_qvar_support: QVAR supported flag.
 */
struct st_lis2duxs12_settings {
	struct {
		enum st_lis2duxs12_hw_id hw_id;
		const char *name;
	} id;
	bool st_qvar_support;
};
/**
 * struct st_lis2duxs12_sensor - ST ACC sensor instance
 */
struct st_lis2duxs12_sensor {
	char name[32];
	enum st_lis2duxs12_sensor_id id;
	struct st_lis2duxs12_hw *hw;
	struct iio_trigger *trig;

	int odr;
	int uodr;

	union {
		/* sensor with odrs, gain and offset */
		struct {
			u32 gain;
			u32 offset;
			u8 decimator;
			u8 dec_counter;
			__le16 old_data;
			u8 max_watermark;
			u8 watermark;
			enum st_lis2duxs12_pm_t pm;

			/* self test */
			int8_t selftest_status;
			int min_st;
			int max_st;
		};
		/* mlc/fsm event sensors */
		struct {
			uint8_t status_reg;
			uint8_t outreg_addr;
			enum st_lis2duxs12_fsm_mlc_enable_id status;
		};
		/* sensor specific data configuration */
		struct {
			u32 conf[6];
			/* Ensure natural alignment of timestamp */
			struct {
				u8 event;
				s64 ts __aligned(8);
			} scan;
		};
	};
};

/**
 * struct st_lis2duxs12_hw - ST ACC MEMS hw instance
 */
struct st_lis2duxs12_hw {
	struct device *dev;
	int irq;
	struct regmap *regmap;
	struct mutex page_lock;
	struct mutex fifo_lock;
	enum st_lis2duxs12_fifo_mode fifo_mode;
	unsigned long state;
	bool xl_only;
	bool timestamp;

	u8 std_level;
	u64 samples;

	u32 enable_mask;
	u32 requested_mask;

	s64 ts_offset;
	s64 hw_ts;
	s64 tsample;
	s64 delta_ts;
	s64 ts;
	s64 last_fifo_timestamp;

	struct iio_mount_matrix orientation;
	struct regulator *vdd_supply;
	struct regulator *vddio_supply;

	struct st_lis2duxs12_mlc_config_t *mlc_config;
	const struct st_lis2duxs12_odr_table_entry *odr_table_entry;
	const struct st_lis2duxs12_fs_table_entry *fs_table_entry;

	bool preload_mlc;

	u8 int_pin;
	u8 ft_int_reg;
	u8 md_int_reg;
	u8 emb_int_reg;

	struct iio_dev *iio_devs[ST_LIS2DUXS12_ID_MAX];
	const struct st_lis2duxs12_settings *settings;
};

extern const struct dev_pm_ops st_lis2duxs12_pm_ops;

static inline bool
st_lis2duxs12_is_fifo_enabled(struct st_lis2duxs12_hw *hw)
{
	return hw->enable_mask & (BIT(ST_LIS2DUXS12_ID_ACC) |
				  BIT(ST_LIS2DUXS12_ID_TEMP));
}

static inline int
__st_lis2duxs12_write_with_mask(struct st_lis2duxs12_hw *hw,
				unsigned int addr,
				unsigned int mask,
				unsigned int data)
{
	int err;
	unsigned int val = ST_LIS2DUXS12_SHIFT_VAL(data, mask);

	err = regmap_update_bits(hw->regmap, addr, mask, val);

	return err;
}

static inline int
st_lis2duxs12_update_bits_locked(struct st_lis2duxs12_hw *hw,
				 unsigned int addr, unsigned int mask,
				 unsigned int val)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = __st_lis2duxs12_write_with_mask(hw, addr, mask, val);
	mutex_unlock(&hw->page_lock);

	return err;
}

/* use when mask is constant */
static inline int
st_lis2duxs12_write_with_mask_locked(struct st_lis2duxs12_hw *hw,
				     unsigned int addr,
				     unsigned int mask,
				     unsigned int data)
{
	int err;
	unsigned int val = FIELD_PREP(mask, data);

	mutex_lock(&hw->page_lock);
	err = regmap_update_bits(hw->regmap, addr, mask, val);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_lis2duxs12_read_locked(struct st_lis2duxs12_hw *hw,
			  unsigned int addr, void *val,
			  unsigned int len)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = regmap_bulk_read(hw->regmap, addr, val, len);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_lis2duxs12_read_with_mask(struct st_lis2duxs12_hw *hw,
			     unsigned int addr, unsigned int mask,
			     u8 *val)
{
	unsigned int data;
	int err;

	err = regmap_read(hw->regmap, addr, &data);
	*val = (u8)ST_LIS2DUXS12_DESHIFT_VAL(data, mask);

	return err;
}

static inline int
st_lis2duxs12_read_with_mask_locked(struct st_lis2duxs12_hw *hw,
				    unsigned int addr,
				    unsigned int mask, u8 *val)
{
	unsigned int data;
	int err;

	mutex_lock(&hw->page_lock);
	err = regmap_read(hw->regmap, addr, &data);
	mutex_unlock(&hw->page_lock);
	*val = (u8)ST_LIS2DUXS12_DESHIFT_VAL(data, mask);

	return err;
}

static inline int
st_lis2duxs12_write_locked(struct st_lis2duxs12_hw *hw,
			   unsigned int addr, unsigned int val)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = regmap_write(hw->regmap, addr, val);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_lis2duxs12_set_emb_access(struct st_lis2duxs12_hw *hw,
			     unsigned int val)
{
	return regmap_write(hw->regmap,
			    ST_LIS2DUXS12_FUNC_CFG_ACCESS_ADDR,
			    val ? ST_LIS2DUXS12_EMB_FUNC_REG_ACCESS_MASK : 0);
}

static inline int
st_lis2duxs12_read_page_locked(struct st_lis2duxs12_hw *hw,
			       unsigned int addr, void *val,
			       unsigned int len)
{
	int err;

	mutex_lock(&hw->page_lock);
	st_lis2duxs12_set_emb_access(hw, 1);
	err = regmap_bulk_read(hw->regmap, addr, val, len);
	st_lis2duxs12_set_emb_access(hw, 0);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_lis2duxs12_write_page_locked(struct st_lis2duxs12_hw *hw,
				unsigned int addr, unsigned int *val,
				unsigned int len)
{
	int err;

	mutex_lock(&hw->page_lock);
	st_lis2duxs12_set_emb_access(hw, 1);
	err = regmap_bulk_write(hw->regmap, addr, val, len);
	st_lis2duxs12_set_emb_access(hw, 0);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_lis2duxs12_update_page_bits_locked(struct st_lis2duxs12_hw *hw,
				      unsigned int addr,
				      unsigned int mask,
				      unsigned int val)
{
	int err;

	mutex_lock(&hw->page_lock);
	st_lis2duxs12_set_emb_access(hw, 1);
	err = regmap_update_bits(hw->regmap, addr, mask, val);
	st_lis2duxs12_set_emb_access(hw, 0);
	mutex_unlock(&hw->page_lock);

	return err;
}

int st_lis2duxs12_probe(struct device *dev, int irq,
			enum st_lis2duxs12_hw_id hw_id, struct regmap *regmap);
int st_lis2duxs12_remove(struct device *dev);
int st_lis2duxs12_sensor_set_enable(struct st_lis2duxs12_sensor *sensor,
				    bool enable);
int st_lis2duxs12_buffers_setup(struct st_lis2duxs12_hw *hw);
ssize_t st_lis2duxs12_flush_fifo(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size);
ssize_t st_lis2duxs12_get_max_watermark(struct device *dev,
					struct device_attribute *attr,
					char *buf);
ssize_t st_lis2duxs12_get_watermark(struct device *dev,
				    struct device_attribute *attr,
				    char *buf);
ssize_t st_lis2duxs12_set_watermark(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size);
int st_lis2duxs12_suspend_fifo(struct st_lis2duxs12_hw *hw);
int st_lis2duxs12_set_fifo_mode(struct st_lis2duxs12_hw *hw,
				enum st_lis2duxs12_fifo_mode fifo_mode);
int st_lis2duxs12_update_batching(struct iio_dev *iio_dev, bool enable);

/* mlc / fsm */
int st_lis2duxs12_mlc_probe(struct st_lis2duxs12_hw *hw);
int st_lis2duxs12_mlc_remove(struct device *dev);
int st_lis2duxs12_mlc_check_status(struct st_lis2duxs12_hw *hw);
int st_lis2duxs12_mlc_init_preload(struct st_lis2duxs12_hw *hw);

int st_lis2duxs12_reset_step_counter(struct iio_dev *iio_dev);
int st_lis2duxs12_embedded_function_init(struct st_lis2duxs12_hw *hw);
int st_lis2duxs12_step_counter_set_enable(struct st_lis2duxs12_sensor *sensor,
					  bool enable);
int st_lis2duxs12_embfunc_sensor_set_enable(struct st_lis2duxs12_sensor *sensor,
					    bool enable);
int st_lis2duxs12_probe_basicfunc(struct st_lis2duxs12_hw *hw);
int st_lis2duxs12_event_handler(struct st_lis2duxs12_hw *hw);

/* qvar */
int st_lis2duxs12_qvar_probe(struct st_lis2duxs12_hw *hw);
int st_lis2duxs12_qvar_set_enable(struct st_lis2duxs12_sensor *sensor,
				  bool enable);
#endif /* ST_LIS2DUXS12_H */
