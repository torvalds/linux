/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics st_lsm6dsv16bx sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2023 STMicroelectronics Inc.
 */

#ifndef ST_LSM6DSV16BX_H
#define ST_LSM6DSV16BX_H

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/iio/iio.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "../../common/stm_iio_types.h"

#define ST_LSM6DSV16BX_ODR_LIST_SIZE			9
#define ST_LSM6DSV16BX_ODR_EXPAND(odr, uodr)		((odr * 1000000) + uodr)

#define ST_LSM6DSV16BX_DEV_NAME				"lsm6dsv16bx"
#define ST_LSM6DSV16B_DEV_NAME				"lsm6dsv16b"

#define ST_LSM6DSV16BX_SAMPLE_SIZE			6
#define ST_LSM6DSV16BX_TS_SAMPLE_SIZE			4
#define ST_LSM6DSV16BX_TAG_SIZE				1
#define ST_LSM6DSV16BX_FIFO_SAMPLE_SIZE			(ST_LSM6DSV16BX_SAMPLE_SIZE + \
							 ST_LSM6DSV16BX_TAG_SIZE)
#define ST_LSM6DSV16BX_MAX_FIFO_DEPTH			208

/* register map */
#define ST_LSM6DSV16BX_REG_FUNC_CFG_ACCESS_ADDR		0x01
#define ST_LSM6DSV16BX_EMB_FUNC_REG_ACCESS_MASK		BIT(7)

#define ST_LSM6DSV16BX_REG_IF_CFG_ADDR			0x03
#define ST_LSM6DSV16BX_PP_OD_MASK			BIT(3)
#define ST_LSM6DSV16BX_H_LACTIVE_MASK			BIT(4)
#define ST_LSM6DSV16BX_TDM_OUT_PU_EN_MASK		BIT(6)

#define ST_LSM6DSV16BX_REG_FIFO_CTRL1_ADDR		0x07
#define ST_LSM6DSV16BX_WTM_MASK				GENMASK(7, 0)

#define ST_LSM6DSV16BX_REG_FIFO_CTRL3_ADDR		0x09
#define ST_LSM6DSV16BX_BDR_XL_MASK			GENMASK(3, 0)
#define ST_LSM6DSV16BX_BDR_GY_MASK			GENMASK(7, 4)

#define ST_LSM6DSV16BX_REG_FIFO_CTRL4_ADDR		0x0a
#define ST_LSM6DSV16BX_FIFO_MODE_MASK			GENMASK(2, 0)
#define ST_LSM6DSV16BX_ODR_T_BATCH_MASK			GENMASK(5, 4)
#define ST_LSM6DSV16BX_DEC_TS_BATCH_MASK		GENMASK(7, 6)

#define ST_LSM6DSV16BX_COUNTER_BDR_REG1_ADDR		0x0b
#define ST_LSM6DSV16BX_AH_QVAR_BATCH_EN_MASK		BIT(2)

#define ST_LSM6DSV16BX_REG_INT1_CTRL_ADDR		0x0d
#define ST_LSM6DSV16BX_REG_INT2_CTRL_ADDR		0x0e
#define ST_LSM6DSV16BX_INT_FIFO_TH_MASK			BIT(3)

#define ST_LSM6DSV16BX_REG_WHOAMI_ADDR			0x0f
#define ST_LSM6DSV16BX_WHOAMI_VAL			0x71

#define ST_LSM6DSV16BX_REG_CTRL1_ADDR			0x10
#define ST_LSM6DSV16BX_REG_CTRL2_ADDR			0x11
#define ST_LSM6DSV16BX_ODR_MASK				GENMASK(3, 0)
#define ST_LSM6DSV16BX_OP_MODE_MASK			GENMASK(6, 4)

#define ST_LSM6DSV16BX_REG_CTRL3_ADDR			0x12
#define ST_LSM6DSV16BX_SW_RESET_MASK			BIT(0)
#define ST_LSM6DSV16BX_BDU_MASK				BIT(6)
#define ST_LSM6DSV16BX_BOOT_MASK			BIT(7)

#define ST_LSM6DSV16BX_REG_CTRL4_ADDR			0x13
#define ST_LSM6DSV16BX_DRDY_MASK			BIT(3)

#define ST_LSM6DSV16BX_REG_CTRL6_ADDR			0x15

#define ST_LSM6DSV16BX_REG_CTRL7_ADDR			0x16
#define ST_LSM6DSV16BX_AH_QVARx_EN_MASK			GENMASK(3, 2)
#define ST_LSM6DSV16BX_AH_QVAR_C_ZIN_MASK		GENMASK(5, 4)
#define ST_LSM6DSV16BX_AH_QVAR_EN_MASK			BIT(7)

#define ST_LSM6DSV16BX_REG_CTRL8_ADDR			0x17

#define ST_LSM6DSV16BX_REG_CTRL10_ADDR			0x19
#define ST_LSM6DSV16BX_ST_XL_MASK			GENMASK(1, 0)
#define ST_LSM6DSV16BX_ST_G_MASK			GENMASK(3, 2)

#define ST_LSM6DSV16BX_REG_FIFO_STATUS1_ADDR		0x1b
#define ST_LSM6DSV16BX_FIFO_DIFF_MASK			GENMASK(8, 0)

#define ST_LSM6DSV16BX_REG_ALL_INT_SRC_ADDR		0x1d
#define ST_LSM6DSV16BX_FF_IA_MASK			BIT(0)
#define ST_LSM6DSV16BX_WU_IA_MASK			BIT(1)
#define ST_LSM6DSV16BX_TAP_IA_MASK			BIT(2)
#define ST_LSM6DSV16BX_D6D_IA_MASK			BIT(4)
#define ST_LSM6DSV16BX_SLEEP_CHANGE_MASK		BIT(5)

#define ST_LSM6DSV16BX_REG_D6D_SRC_ADDR			0x1d
#define ST_LSM6DSV16BX_D6D_EVENT_MASK			GENMASK(5, 0)

#define ST_LSM6DSV16BX_REG_STATUS_REG_ADDR		0x1e
#define ST_LSM6DSV16BX_XLDA_MASK			BIT(0)
#define ST_LSM6DSV16BX_GDA_MASK				BIT(1)
#define ST_LSM6DSV16BX_TDA_MASK				BIT(2)

#define ST_LSM6DSV16BX_REG_OUT_TEMP_L_ADDR		0x20

#define ST_LSM6DSV16BX_REG_OUTZ_L_G_ADDR		0x22
#define ST_LSM6DSV16BX_REG_OUTY_L_G_ADDR		0x24
#define ST_LSM6DSV16BX_REG_OUTX_L_G_ADDR		0x26
#define ST_LSM6DSV16BX_REG_OUTZ_L_A_ADDR		0x28
#define ST_LSM6DSV16BX_REG_OUTY_L_A_ADDR		0x2a
#define ST_LSM6DSV16BX_REG_OUTX_L_A_ADDR		0x2c

#define ST_LSM6DSV16BX_REG_OUT_QVAR_ADDR		0x3a

#define ST_LSM6DSV16BX_REG_TIMESTAMP0_ADDR		0x40
#define ST_LSM6DSV16BX_REG_TIMESTAMP2_ADDR		0x42

#define ST_LSM6DSV16BX_REG_WAKE_UP_SRC_ADDR		0x45
#define ST_LSM6DSV16BX_WAKE_UP_EVENT_MASK		GENMASK(3, 0)

#define ST_LSM6DSV16BX_REG_EMB_FUNC_STATUS_MAINPAGE_ADDR	0x49
#define ST_LSM6DSV16BX_IS_STEP_DET_MASK			BIT(3)
#define ST_LSM6DSV16BX_IS_TILT_MASK			BIT(4)
#define ST_LSM6DSV16BX_IS_SIGMOT_MASK			BIT(5)

#define ST_LSM6DSV16BX_REG_FSM_STATUS_MAINPAGE_ADDR	0x4a
#define ST_LSM6DSV16BX_REG_MLC_STATUS_MAINPAGE_ADDR	0x4b

#define ST_LSM6DSV16BX_REG_INTERNAL_FREQ_FINE		0x4f

#define ST_LSM6DSV16BX_REG_FUNCTIONS_ENABLE_ADDR	0x50
#define ST_LSM6DSV16BX_TIMESTAMP_EN_MASK		BIT(6)
#define ST_LSM6DSV16BX_INTERRUPTS_ENABLE_MASK		BIT(7)

#define ST_LSM6DSV16BX_REG_TAP_CFG0_ADDR		0x56
#define ST_LSM6DSV16BX_LIR_MASK				BIT(0)
#define ST_LSM6DSV16BX_REG_TAP_Z_EN_MASK		BIT(1)
#define ST_LSM6DSV16BX_REG_TAP_Y_EN_MASK		BIT(2)
#define ST_LSM6DSV16BX_REG_TAP_X_EN_MASK		BIT(3)
#define ST_LSM6DSV16BX_REG_TAP_EN_MASK			GENMASK(3, 1)

#define ST_LSM6DSV16BX_REG_TAP_CFG1_ADDR		0x57
#define ST_LSM6DSV16BX_TAP_THS_X_MASK			GENMASK(4, 0)
#define ST_LSM6DSV16BX_TAP_PRIORITY_MASK		GENMASK(7, 5)

#define ST_LSM6DSV16BX_REG_TAP_CFG2_ADDR		0x58
#define ST_LSM6DSV16BX_TAP_THS_Y_MASK			GENMASK(4, 0)

#define ST_LSM6DSV16BX_REG_TAP_THS_6D_ADDR		0x59
#define ST_LSM6DSV16BX_TAP_THS_Z_MASK			GENMASK(4, 0)
#define ST_LSM6DSV16BX_SIXD_THS_MASK			GENMASK(6, 5)

#define ST_LSM6DSV16BX_REG_TAP_DUR_ADDR			0x5a
#define ST_LSM6DSV16BX_SHOCK_MASK			GENMASK(1, 0)
#define ST_LSM6DSV16BX_QUIET_MASK			GENMASK(3, 2)
#define ST_LSM6DSV16BX_DUR_MASK				GENMASK(7, 4)

#define ST_LSM6DSV16BX_REG_WAKE_UP_THS_ADDR		0x5b
#define ST_LSM6DSV16BX_WK_THS_MASK			GENMASK(5, 0)
#define ST_LSM6DSV16BX_SINGLE_DOUBLE_TAP_MASK		BIT(7)

#define ST_LSM6DSV16BX_REG_WAKE_UP_DUR_ADDR		0x5c
#define ST_LSM6DSV16BX_WAKE_DUR_MASK			GENMASK(6, 5)

#define ST_LSM6DSV16BX_REG_FREE_FALL_ADDR		0x5d
#define ST_LSM6DSV16BX_FF_THS_MASK			GENMASK(2, 0)

#define ST_LSM6DSV16BX_REG_MD1_CFG_ADDR			0x5e
#define ST_LSM6DSV16BX_REG_MD2_CFG_ADDR			0x5f
#define ST_LSM6DSV16BX_REG_INT2_TIMESTAMP_MASK		BIT(0)
#define ST_LSM6DSV16BX_REG_INT_EMB_FUNC_MASK		BIT(1)
#define ST_LSM6DSV16BX_INT_6D_MASK			BIT(2)
#define ST_LSM6DSV16BX_INT_DOUBLE_TAP_MASK		BIT(3)
#define ST_LSM6DSV16BX_INT_FF_MASK			BIT(4)
#define ST_LSM6DSV16BX_INT_WU_MASK			BIT(5)
#define ST_LSM6DSV16BX_INT_SINGLE_TAP_MASK		BIT(6)
#define ST_LSM6DSV16BX_INT_SLEEP_CHANGE_MASK		BIT(7)

#define ST_LSM6DSV16BX_REG_TDM_CFG0_ADDR		0x6c
#define ST_LSM6DSV16BX_REG_TDM_SLOT_SEL_MASK		BIT(4)
#define ST_LSM6DSV16BX_REG_TDM_WCLK_MASK		GENMASK(2, 1)
#define ST_LSM6DSV16BX_REG_TDM_WCLK_BCLK_SEL_MASK	BIT(0)

#define ST_LSM6DSV16BX_REG_TDM_CFG1_ADDR		0x6d
#define ST_LSM6DSV16BX_REG_TDM_AXES_ORD_SEL_MASK	GENMASK(4, 3)

#define ST_LSM6DSV16BX_REG_TDM_CFG2_ADDR		0x6e
#define ST_LSM6DSV16BX_REG_TDM_FS_XL_MASK		GENMASK(1, 0)

#define ST_LSM6DSV16BX_REG_FIFO_DATA_OUT_TAG_ADDR	0x78

/* embedded function registers */
#define ST_LSM6DSV16BX_REG_PAGE_SEL_ADDR		0x02
#define ST_LSM6DSV16BX_PAGE_SEL_MASK			GENMASK(6, 4)

#define ST_LSM6DSV16BX_REG_EMB_FUNC_EN_A_ADDR		0x04
#define ST_LSM6DSV16BX_SFLP_GAME_EN_MASK		BIT(1)
#define ST_LSM6DSV16BX_REG_PEDO_EN_MASK			BIT(3)
#define ST_LSM6DSV16BX_REG_TILT_EN_MASK			BIT(4)
#define ST_LSM6DSV16BX_REG_SIGN_MOTION_EN_MASK		BIT(5)

#define ST_LSM6DSV16BX_REG_EMB_FUNC_EN_B_ADDR		0x05
#define ST_LSM6DSV16BX_FSM_EN_MASK			BIT(0)
#define ST_LSM6DSV16BX_MLC_EN_MASK			BIT(4)

#define ST_LSM6DSV16BX_REG_PAGE_ADDRESS_ADDR		0x08
#define ST_LSM6DSV16BX_REG_PAGE_VALUE_ADDR		0x09

#define ST_LSM6DSV16BX_REG_EMB_FUNC_INT1_ADDR		0x0a
#define ST_LSM6DSV16BX_INT_STEP_DETECTOR_MASK		BIT(3)
#define ST_LSM6DSV16BX_INT_TILT_MASK			BIT(4)
#define ST_LSM6DSV16BX_INT_SIG_MOT_MASK			BIT(5)

#define ST_LSM6DSV16BX_REG_FSM_INT1_ADDR		0x0b
#define ST_LSM6DSV16BX_REG_MLC_INT1_ADDR		0x0d
#define ST_LSM6DSV16BX_REG_EMB_FUNC_INT2_ADDR		0x0e
#define ST_LSM6DSV16BX_REG_FSM_INT2_ADDR		0x0f
#define ST_LSM6DSV16BX_REG_MLC_INT2_ADDR		0x11

#define ST_LSM6DSV16BX_REG_FSM_STATUS_ADDR		0x13
#define ST_LSM6DSV16BX_REG_MLC_STATUS_ADDR		0x15

#define ST_LSM6DSV16BX_REG_PAGE_RW_ADDR			0x17
#define ST_LSM6DSV16BX_PAGE_READ_MASK			BIT(5)
#define ST_LSM6DSV16BX_PAGE_WRITE_MASK			BIT(6)
#define ST_LSM6DSV16BX_EMB_FUNC_LIR_MASK		BIT(7)

#define ST_LSM6DSV16BX_REG_EMB_FUNC_FIFO_EN_A_ADDR	0x44
#define ST_LSM6DSV16BX_SFLP_GAME_FIFO_EN		BIT(1)
#define ST_LSM6DSV16BX_SFLP_GRAVITY_FIFO_EN		BIT(4)
#define ST_LSM6DSV16BX_SFLP_GBIAS_FIFO_EN_MASK		BIT(5)
#define ST_LSM6DSV16BX_STEP_COUNTER_FIFO_EN_MASK	BIT(6)

#define ST_LSM6DSV16BX_REG_FSM_ENABLE_ADDR		0x46

#define ST_LSM6DSV16BX_REG_FSM_OUTS1_ADDR		0x4c

#define ST_LSM6DSV16BX_REG_SFLP_ODR_ADDR		0x5e
#define ST_LSM6DSV16BX_SFLP_GAME_ODR_MASK		GENMASK(5, 3)

#define ST_LSM6DSV16BX_REG_FSM_ODR_ADDR			0x5f
#define ST_LSM6DSV16BX_FSM_ODR_MASK			GENMASK(5, 3)

#define ST_LSM6DSV16BX_REG_MLC_ODR_ADDR			0x60
#define ST_LSM6DSV16BX_MLC_ODR_MASK			GENMASK(6, 4)

#define ST_LSM6DSV16BX_REG_STEP_COUNTER_L_ADDR		0x62

#define ST_LSM6DSV16BX_REG_EMB_FUNC_SRC_ADDR		0x64
#define ST_LSM6DSV16BX_STEPCOUNTER_BIT_SET_MASK		BIT(2)
#define ST_LSM6DSV16BX_STEP_OVERFLOW_MASK		BIT(3)
#define ST_LSM6DSV16BX_STEP_COUNT_DELTA_IA_MASK		BIT(4)
#define ST_LSM6DSV16BX_STEP_DETECTED_MASK		BIT(5)
#define ST_LSM6DSV16BX_PEDO_RST_STEP_MASK		BIT(7)

#define ST_LSM6DSV16BX_REG_EMB_FUNC_INIT_A_ADDR		0x66
#define ST_LSM6DSV16BX_SFLP_GAME_INIT_MASK		BIT(1)

#define ST_LSM6DSV16BX_REG_EMB_FUNC_INIT_B_ADDR		0x67
#define ST_LSM6DSV16BX_FSM_INIT_MASK			BIT(0)
#define ST_LSM6DSV16BX_MLC_INIT_MASK			BIT(4)

#define ST_LSM6DSV16BX_REG_MLC1_SRC_ADDR		0x70

#define ST_LSM6DSV16BX_TS_DELTA_NS			21700ULL

/* temperature in uC */
#define ST_LSM6DSV16BX_TEMP_GAIN			256
#define ST_LSM6DSV16BX_TEMP_OFFSET			6400

/* self test values */
#define ST_LSM6DSV16BX_SELFTEST_ACCEL_MIN		410
#define ST_LSM6DSV16BX_SELFTEST_ACCEL_MAX		13935
#define ST_LSM6DSV16BX_SELFTEST_GYRO_MIN		2143
#define ST_LSM6DSV16BX_SELFTEST_GYRO_MAX		10000

#define ST_LSM6DSV16BX_SELF_TEST_NORMAL_MODE_VAL	0
#define ST_LSM6DSV16BX_SELF_TEST_POS_SIGN_VAL		1
#define ST_LSM6DSV16BX_SELF_TEST_NEG_SIGN_VAL		2

#define ST_LSM6DSV16BX_DEFAULT_KTIME			(200000000)
#define ST_LSM6DSV16BX_FAST_KTIME			(5000000)

#define ST_LSM6DSV16BX_DATA_CHANNEL(chan_type, addr, mod, ch2, scan_idx, \
				 rb, sb, sg, ext_inf)			 \
{									 \
	.type = chan_type,						 \
	.address = addr,						 \
	.modified = mod,						 \
	.channel2 = ch2,						 \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |			 \
			      BIT(IIO_CHAN_INFO_SCALE),			 \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),	 \
	.scan_index = scan_idx,						 \
	.scan_type = {							 \
		.sign = sg,						 \
		.realbits = rb,						 \
		.storagebits = sb,					 \
		.endianness = IIO_LE,					 \
	},								 \
	.ext_info = ext_inf,						 \
}

#define ST_LSM6DSV16BX_SFLP_DATA_CHANNEL(chan_type, mod, ch2, scan_idx,  \
				      rb, sb, sg)			 \
{									 \
	.type = chan_type,						 \
	.modified = mod,						 \
	.channel2 = ch2,						 \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),	 \
	.scan_index = scan_idx,						 \
	.scan_type = {							 \
		.sign = sg,						 \
		.realbits = rb,						 \
		.storagebits = sb,					 \
		.endianness = IIO_LE,					 \
	},								 \
}

static const struct iio_event_spec st_lsm6dsv16bx_flush_event = {
	.type = STM_IIO_EV_TYPE_FIFO_FLUSH,
	.dir = IIO_EV_DIR_EITHER,
};

static const struct iio_event_spec st_lsm6dsv16bx_thr_event = {
	.type = IIO_EV_TYPE_THRESH,
	.dir = IIO_EV_DIR_RISING,
	.mask_separate = BIT(IIO_EV_INFO_ENABLE),
};

#define ST_LSM6DSV16BX_EVENT_CHANNEL(ctype, etype)	\
{							\
	.type = ctype,					\
	.modified = 0,					\
	.scan_index = -1,				\
	.indexed = -1,					\
	.event_spec = &st_lsm6dsv16bx_##etype##_event,	\
	.num_event_specs = 1,				\
}

#define ST_LSM6DSV16BX_SHIFT_VAL(val, mask)	(((val) << __ffs(mask)) & (mask))

struct st_lsm6dsv16bx_reg {
	u8 addr;
	u8 mask;
};

struct st_lsm6dsv16bx_odr {
	u16 hz;
	int uhz;
	u8 val;
	u8 batch_val;
};

struct st_lsm6dsv16bx_odr_table_entry {
	u8 size;
	struct st_lsm6dsv16bx_reg reg;
	struct st_lsm6dsv16bx_odr odr_avl[ST_LSM6DSV16BX_ODR_LIST_SIZE];
};

struct st_lsm6dsv16bx_fs {
	u32 gain;
	u8 val;
};

#define ST_LSM6DSV16BX_FS_LIST_SIZE		6
#define ST_LSM6DSV16BX_FS_ACC_LIST_SIZE		4
#define ST_LSM6DSV16BX_FS_GYRO_LIST_SIZE	6
struct st_lsm6dsv16bx_fs_table_entry {
	u8 size;
	struct st_lsm6dsv16bx_reg reg;
	struct st_lsm6dsv16bx_fs fs_avl[ST_LSM6DSV16BX_FS_LIST_SIZE];
};

#define ST_LSM6DSV16BX_ACC_FS_2G_GAIN		IIO_G_TO_M_S_2(61000)
#define ST_LSM6DSV16BX_ACC_FS_4G_GAIN		IIO_G_TO_M_S_2(122000)
#define ST_LSM6DSV16BX_ACC_FS_8G_GAIN		IIO_G_TO_M_S_2(244000)
#define ST_LSM6DSV16BX_ACC_FS_16G_GAIN		IIO_G_TO_M_S_2(488000)

#define ST_LSM6DSV16BX_GYRO_FS_125_GAIN		IIO_DEGREE_TO_RAD(4375000)
#define ST_LSM6DSV16BX_GYRO_FS_250_GAIN		IIO_DEGREE_TO_RAD(8750000)
#define ST_LSM6DSV16BX_GYRO_FS_500_GAIN		IIO_DEGREE_TO_RAD(17500000)
#define ST_LSM6DSV16BX_GYRO_FS_1000_GAIN	IIO_DEGREE_TO_RAD(35000000)
#define ST_LSM6DSV16BX_GYRO_FS_2000_GAIN	IIO_DEGREE_TO_RAD(70000000)
#define ST_LSM6DSV16BX_GYRO_FS_4000_GAIN	IIO_DEGREE_TO_RAD(140000000)

/**
 * enum st_lsm6dsv16bx_hw_id - list of HW device id supported by the
 *                          lsm6dsv16bx driver
 */
enum st_lsm6dsv16bx_hw_id {
	ST_LSM6DSV16BX_ID,
	ST_LSM6DSV16B_ID,
	ST_LSM6DSV16BX_MAX_ID,
};

enum st_lsm6dsv16bx_fsm_mlc_enable_id {
	ST_LSM6DSV16BX_MLC_FSM_DISABLED = 0,
	ST_LSM6DSV16BX_MLC_ENABLED = BIT(0),
	ST_LSM6DSV16BX_FSM_ENABLED = BIT(1),
};

/**
 * struct mlc_config_t - MLC/FSM configuration report struct
 * @mlc_int_addr: interrupt register address.
 * @mlc_int_mask: interrupt register mask.
 * @fsm_int_addr: interrupt register address.
 * @fsm_int_mask: interrupt register mask.
 * @mlc_configured: number of mlc configured.
 * @fsm_configured: number of fsm configured.
 * @bin_len: fw binary size.
 * @requested_odr: Min ODR requested to works properly.
 * @status: MLC / FSM enabled status.
 */
struct st_lsm6dsv16bx_mlc_config_t {
	uint8_t mlc_int_addr;
	uint8_t mlc_int_mask;
	uint8_t fsm_int_addr;
	uint8_t fsm_int_mask;
	uint8_t mlc_configured;
	uint8_t fsm_configured;
	uint16_t bin_len;
	uint16_t requested_odr;
	enum st_lsm6dsv16bx_fsm_mlc_enable_id status;
};

/**
 * struct st_lsm6dsv16bx_settings - ST IMU sensor settings
 * @hw_id: Hw id supported by the driver configuration.
 * @name: Device name supported by the driver configuration.
 * @st_qvar_probe: QVAR probe flag, indicate if QVAR feature is supported.
 * @st_mlc_probe: MLC probe flag, indicate if MLC feature is supported.
 * @st_fsm_probe: FSM probe flag, indicate if FSM feature is supported.
 * @st_sflp_probe: SFLP probe flag, indicate if SFLP feature is supported.
 * @fs_table: full scale table for main sensors.
 *
 * main sensors are ST_LSM6DSV16BX_ID_GYRO, ST_LSM6DSV16BX_ID_ACC and
 * ST_LSM6DSV16BX_ID_TEMP
 */
#define ST_LSM6DSV16BX_MAIN_SENSOR_NUM	3

struct st_lsm6dsv16bx_settings {
	struct {
		enum st_lsm6dsv16bx_hw_id hw_id;
		const char *name;
	} id;

	bool st_qvar_probe;
	bool st_mlc_probe;
	bool st_fsm_probe;
	bool st_sflp_probe;
	bool st_tdm_probe;
	struct st_lsm6dsv16bx_fs_table_entry fs_table[ST_LSM6DSV16BX_MAIN_SENSOR_NUM];
};

enum st_lsm6dsv16bx_sensor_id {
	ST_LSM6DSV16BX_ID_GYRO,
	ST_LSM6DSV16BX_ID_ACC,
	ST_LSM6DSV16BX_ID_TEMP,
	ST_LSM6DSV16BX_ID_6X_GAME,
	ST_LSM6DSV16BX_ID_QVAR,
	ST_LSM6DSV16BX_ID_MLC,
	ST_LSM6DSV16BX_ID_MLC_0,
	ST_LSM6DSV16BX_ID_MLC_1,
	ST_LSM6DSV16BX_ID_MLC_2,
	ST_LSM6DSV16BX_ID_MLC_3,
	ST_LSM6DSV16BX_ID_FSM_0,
	ST_LSM6DSV16BX_ID_FSM_1,
	ST_LSM6DSV16BX_ID_FSM_2,
	ST_LSM6DSV16BX_ID_FSM_3,
	ST_LSM6DSV16BX_ID_FSM_4,
	ST_LSM6DSV16BX_ID_FSM_5,
	ST_LSM6DSV16BX_ID_FSM_6,
	ST_LSM6DSV16BX_ID_FSM_7,
	ST_LSM6DSV16BX_ID_STEP_COUNTER,
	ST_LSM6DSV16BX_ID_STEP_DETECTOR,
	ST_LSM6DSV16BX_ID_SIGN_MOTION,
	ST_LSM6DSV16BX_ID_TILT,
	ST_LSM6DSV16BX_ID_TAP,
	ST_LSM6DSV16BX_ID_DTAP,
	ST_LSM6DSV16BX_ID_FF,
	ST_LSM6DSV16BX_ID_SLPCHG,
	ST_LSM6DSV16BX_ID_WK,
	ST_LSM6DSV16BX_ID_6D,
	ST_LSM6DSV16BX_ID_MAX,
};

static const enum st_lsm6dsv16bx_sensor_id st_lsm6dsv16bx_main_sensor_list[] = {
	[0] = ST_LSM6DSV16BX_ID_GYRO,
	[1] = ST_LSM6DSV16BX_ID_ACC,
	[2] = ST_LSM6DSV16BX_ID_6X_GAME,
	[3] = ST_LSM6DSV16BX_ID_TEMP,
};

static const enum st_lsm6dsv16bx_sensor_id st_lsm6dsv16bx_gyro_dep_sensor_list[] = {
	[0] = ST_LSM6DSV16BX_ID_GYRO,
	[1] = ST_LSM6DSV16BX_ID_6X_GAME,
	[4] = ST_LSM6DSV16BX_ID_MLC,
	[5] = ST_LSM6DSV16BX_ID_MLC_0,
	[6] = ST_LSM6DSV16BX_ID_MLC_1,
	[7] = ST_LSM6DSV16BX_ID_MLC_2,
	[8] = ST_LSM6DSV16BX_ID_MLC_3,
	[9] = ST_LSM6DSV16BX_ID_FSM_0,
	[10] = ST_LSM6DSV16BX_ID_FSM_1,
	[11] = ST_LSM6DSV16BX_ID_FSM_2,
	[12] = ST_LSM6DSV16BX_ID_FSM_3,
	[13] = ST_LSM6DSV16BX_ID_FSM_4,
	[14] = ST_LSM6DSV16BX_ID_FSM_5,
	[15] = ST_LSM6DSV16BX_ID_FSM_6,
	[16] = ST_LSM6DSV16BX_ID_FSM_7,
};

static const enum st_lsm6dsv16bx_sensor_id st_lsm6dsv16bx_acc_dep_sensor_list[] = {
	[0] = ST_LSM6DSV16BX_ID_ACC,
	[1] = ST_LSM6DSV16BX_ID_TEMP,
	[2] = ST_LSM6DSV16BX_ID_6X_GAME,
	[3] = ST_LSM6DSV16BX_ID_QVAR,
	[6] = ST_LSM6DSV16BX_ID_MLC,
	[7] = ST_LSM6DSV16BX_ID_MLC_0,
	[8] = ST_LSM6DSV16BX_ID_MLC_1,
	[9] = ST_LSM6DSV16BX_ID_MLC_2,
	[10] = ST_LSM6DSV16BX_ID_MLC_3,
	[11] = ST_LSM6DSV16BX_ID_FSM_0,
	[12] = ST_LSM6DSV16BX_ID_FSM_1,
	[13] = ST_LSM6DSV16BX_ID_FSM_2,
	[14] = ST_LSM6DSV16BX_ID_FSM_3,
	[15] = ST_LSM6DSV16BX_ID_FSM_4,
	[16] = ST_LSM6DSV16BX_ID_FSM_5,
	[17] = ST_LSM6DSV16BX_ID_FSM_6,
	[18] = ST_LSM6DSV16BX_ID_FSM_7,
	[19] = ST_LSM6DSV16BX_ID_STEP_COUNTER,
	[20] = ST_LSM6DSV16BX_ID_STEP_DETECTOR,
	[21] = ST_LSM6DSV16BX_ID_SIGN_MOTION,
	[22] = ST_LSM6DSV16BX_ID_TILT,
	[23] = ST_LSM6DSV16BX_ID_TAP,
	[24] = ST_LSM6DSV16BX_ID_DTAP,
	[25] = ST_LSM6DSV16BX_ID_FF,
	[26] = ST_LSM6DSV16BX_ID_SLPCHG,
	[27] = ST_LSM6DSV16BX_ID_WK,
	[28] = ST_LSM6DSV16BX_ID_6D,
};

static const enum st_lsm6dsv16bx_sensor_id st_lsm6dsv16bx_buffered_sensor_list[] = {
	[0] = ST_LSM6DSV16BX_ID_GYRO,
	[1] = ST_LSM6DSV16BX_ID_ACC,
	[2] = ST_LSM6DSV16BX_ID_TEMP,
	[3] = ST_LSM6DSV16BX_ID_6X_GAME,
	[4] = ST_LSM6DSV16BX_ID_QVAR,
	[7] = ST_LSM6DSV16BX_ID_STEP_COUNTER,
};

/**
 * The mlc only sensor list used by mlc loader
 */
static const enum st_lsm6dsv16bx_sensor_id st_lsm6dsv16bx_mlc_sensor_list[] = {
	[0] = ST_LSM6DSV16BX_ID_MLC_0,
	[1] = ST_LSM6DSV16BX_ID_MLC_1,
	[2] = ST_LSM6DSV16BX_ID_MLC_2,
	[3] = ST_LSM6DSV16BX_ID_MLC_3,
};

/**
 * The fsm only sensor list used by mlc loader
 */
static const enum st_lsm6dsv16bx_sensor_id st_lsm6dsv16bx_fsm_sensor_list[] = {
	[0] = ST_LSM6DSV16BX_ID_FSM_0,
	[1] = ST_LSM6DSV16BX_ID_FSM_1,
	[2] = ST_LSM6DSV16BX_ID_FSM_2,
	[3] = ST_LSM6DSV16BX_ID_FSM_3,
	[4] = ST_LSM6DSV16BX_ID_FSM_4,
	[5] = ST_LSM6DSV16BX_ID_FSM_5,
	[6] = ST_LSM6DSV16BX_ID_FSM_6,
	[7] = ST_LSM6DSV16BX_ID_FSM_7,
};

/**
 * The low power embedded function only sensor list
 */
static const enum st_lsm6dsv16bx_sensor_id st_lsm6dsv16bx_embfunc_sensor_list[] = {
	[0] = ST_LSM6DSV16BX_ID_STEP_COUNTER,
	[1] = ST_LSM6DSV16BX_ID_STEP_DETECTOR,
	[2] = ST_LSM6DSV16BX_ID_SIGN_MOTION,
	[3] = ST_LSM6DSV16BX_ID_TILT,
};

/**
 * The low power event only sensor list
 */
static const enum st_lsm6dsv16bx_sensor_id st_lsm6dsv16bx_event_sensor_list[] = {
	[0] = ST_LSM6DSV16BX_ID_TAP,
	[1] = ST_LSM6DSV16BX_ID_DTAP,
	[2] = ST_LSM6DSV16BX_ID_FF,
	[3] = ST_LSM6DSV16BX_ID_SLPCHG,
	[4] = ST_LSM6DSV16BX_ID_WK,
	[5] = ST_LSM6DSV16BX_ID_6D,
};

/**
 * The low power event triggered only sensor list
 */
static const enum st_lsm6dsv16bx_sensor_id
st_lsm6dsv16bx_event_trigger_sensor_list[] = {
	[0] = ST_LSM6DSV16BX_ID_WK,
	[1] = ST_LSM6DSV16BX_ID_6D,
};

#define ST_LSM6DSV16BX_ID_ALL_FSM_MLC (BIT(ST_LSM6DSV16BX_ID_MLC_0) | \
				       BIT(ST_LSM6DSV16BX_ID_MLC_1) | \
				       BIT(ST_LSM6DSV16BX_ID_MLC_2) | \
				       BIT(ST_LSM6DSV16BX_ID_MLC_3) | \
				       BIT(ST_LSM6DSV16BX_ID_FSM_0) | \
				       BIT(ST_LSM6DSV16BX_ID_FSM_1) | \
				       BIT(ST_LSM6DSV16BX_ID_FSM_2) | \
				       BIT(ST_LSM6DSV16BX_ID_FSM_3) | \
				       BIT(ST_LSM6DSV16BX_ID_FSM_4) | \
				       BIT(ST_LSM6DSV16BX_ID_FSM_5) | \
				       BIT(ST_LSM6DSV16BX_ID_FSM_6) | \
				       BIT(ST_LSM6DSV16BX_ID_FSM_7))
enum st_lsm6dsv16bx_fifo_mode {
	ST_LSM6DSV16BX_FIFO_BYPASS = 0x0,
	ST_LSM6DSV16BX_FIFO_CONT = 0x6,
};

enum {
	ST_LSM6DSV16BX_HW_FLUSH,
	ST_LSM6DSV16BX_HW_OPERATIONAL,
};

/* sensor devices that can wake-up the target */
#define  ST_LSM6DSV16BX_WAKE_UP_SENSORS (BIT(ST_LSM6DSV16BX_ID_GYRO) | \
					 BIT(ST_LSM6DSV16BX_ID_ACC)  | \
					 ST_LSM6DSV16BX_ID_ALL_FSM_MLC)

/**
 * struct st_lsm6dsv16bx_sensor - ST IMU sensor instance
 * @name: Sensor name.
 * @id: Sensor identifier.
 * @hw: Pointer to instance of struct st_lsm6dsv16bx_hw.
 * @trig: Trigger used by IIO event sensors.
 * @odr: Output data rate of the sensor [Hz].
 * @uodr: Output data rate of the sensor [uHz].
 * @gain: Configured sensor sensitivity.
 * @offset: Sensor data offset.
 * @std_samples: Counter of samples to discard during sensor bootstrap.
 * @std_level: Samples to discard threshold.
 * @decimator: Samples decimate counter.
 * @dec_counter: Samples decimate value.
 * @last_fifo_timestamp: Timestamp related to last sample in FIFO.
 * @max_watermark: Max supported watermark level.
 * @watermark: Sensor watermark level.
 * @selftest_status: Report last self test status.
 * @min_st: Min self test raw data value.
 * @max_st: Max self test raw data value.
 * @batch_reg: Batching register info (addr + mask).
 * @status_reg: MLC/FSM IIO event sensor status register.
 * @outreg_addr: MLC/FSM IIO event sensor output register.
 * @status: MLC/FSM enabled IIO event sensor status.
 * @conf: Used in case of IIO sensor event to store configuration.
 * @scan: Scan buffer for triggered sensors event.
 */
struct st_lsm6dsv16bx_sensor {
	char name[32];
	enum st_lsm6dsv16bx_sensor_id id;
	struct st_lsm6dsv16bx_hw *hw;
	struct iio_trigger *trig;

	int odr;
	int uodr;

	union {
		struct {
			/* data sensors */
			u32 gain;
			u32 offset;

			u8 std_samples;
			u8 std_level;

			u8 decimator;
			u8 dec_counter;

			s64 last_fifo_timestamp;
			u16 max_watermark;
			u16 watermark;

			/* self test */
			int8_t selftest_status;
			int min_st;
			int max_st;

			struct st_lsm6dsv16bx_reg batch_reg;
		};

		struct {
			/* mlc or fsm sensor */
			uint8_t status_reg;
			uint8_t outreg_addr;
			enum st_lsm6dsv16bx_fsm_mlc_enable_id status;
		};

		struct {
			/* event sensor, data configuration */
			u32 conf[6];

			/* ensure natural alignment of timestamp */
			struct {
				u8 event;
				s64 ts __aligned(8);
			} scan;
		};
	};
};

/**
 * struct st_lsm6dsv16bx_hw - ST IMU MEMS hw instance
 * @dev: Pointer to instance of device struct (I2C / SPI / I3C).
 * @irq: Device interrupt line (I2C / SPI / I3C).
 * @regmap: regmap structure pointer.
 * @lock: Mutex to protect read and write operations.
 * @fifo_lock: Mutex to prevent concurrent access to the hw FIFO.
 * @page_lock: Mutex to prevent concurrent memory page configuration.
 * @fifo_mode: FIFO operating mode supported by the device.
 * @state: hw operational state.
 * @enable_mask: Enabled sensor bitmask.
 * @hw_timestamp_global: hw timestamp value always monotonic where the most
 *                       significant 8byte are incremented at every disable/enable.
 * @timesync_workqueue: runs the async task in private workqueue.
 * @timesync_work: actual work to be done in the async task workqueue.
 * @timesync_timer: hrtimer used to schedule period read for the async task.
 * @hwtimestamp_lock: spinlock for the 64bit timestamp value.
 * @timesync_ktime: interval value used by the hrtimer.
 * @timestamp_c: counter used for counting number of timesync updates.
 * @int_pin: selected interrupt pin from configuration.
 * @ext_data_len: Number of i2c slave devices connected to I2C master.
 * @ts_offset: Hw timestamp offset.
 * @ts_delta_ns: Calibrated delta timestamp.
 * @hw_ts: Latest hw timestamp from the sensor.
 * @val_ts_old: Last sample timestamp for rollover check.
 * @hw_ts_high: Manage timestamp rollover.
 * @tsample: Sample timestamp.
 * @delta_ts: Estimated delta time between two consecutive interrupts.
 * @ts: Latest timestamp from irq handler.
 * @module_id: identify iio devices of the same sensor module.
 * @orientation: Sensor orientation matrix.
 * @vdd_supply: Voltage regulator for VDD.
 * @vddio_supply: Voltage regulator for VDDIIO.
 * @mlc_config: MLC/FSM data register structure.
 * @preload_mlc: MLC/FSM preload flag.
 * @qvar_workqueue: QVAR workqueue (if enabled in Kconfig).
 * @iio_devs: Pointers to acc/gyro iio_dev instances.
 * @settings: ST IMU sensor settings.
 * @fs_table: ST IMU full scale table.
 * @odr_table: ST IMU output data rate table.
 * @en_tdm: TDM enable flag.
 */
struct st_lsm6dsv16bx_hw {
	struct device *dev;
	int irq;
	struct regmap *regmap;
	struct mutex lock;
	struct mutex fifo_lock;
	struct mutex page_lock;

	enum st_lsm6dsv16bx_fifo_mode fifo_mode;
	unsigned long state;
	u32 enable_mask;
	s64 hw_timestamp_global;

#if defined(CONFIG_IIO_ST_LSM6DSV16BX_ASYNC_HW_TIMESTAMP)
	struct workqueue_struct *timesync_workqueue;
	struct work_struct timesync_work;
	struct hrtimer timesync_timer;
	spinlock_t hwtimestamp_lock;
	ktime_t timesync_ktime;
	int timesync_c;
#endif /* CONFIG_IIO_ST_LSM6DSV16BX_ASYNC_HW_TIMESTAMP */

	u8 int_pin;

	u8 ext_data_len;

	s64 ts_offset;
	u64 ts_delta_ns;
	s64 hw_ts;
	u32 val_ts_old;
	u32 hw_ts_high;
	s64 tsample;
	s64 delta_ts;
	s64 ts;

	u32 module_id;
	struct iio_mount_matrix orientation;
	struct regulator *vdd_supply;
	struct regulator *vddio_supply;
	struct st_lsm6dsv16bx_mlc_config_t *mlc_config;
	bool preload_mlc;
	struct workqueue_struct *qvar_workqueue;
	struct iio_dev *iio_devs[ST_LSM6DSV16BX_ID_MAX];

	const struct st_lsm6dsv16bx_settings *settings;
	const struct st_lsm6dsv16bx_fs_table_entry *fs_table;
	const struct st_lsm6dsv16bx_odr_table_entry *odr_table;

	bool en_tdm;
};

/**
 * struct st_lsm6dsv16bx_ff_th - Free Fall threshold table
 * @mg: Threshold in mg.
 * @val: Register value.
 */
struct st_lsm6dsv16bx_ff_th {
	u32 mg;
	u8 val;
};

/**
 * struct st_lsm6dsv16bx_6D_th - 6D threshold table
 * @deg: Threshold in degrees.
 * @val: Register value.
 */
struct st_lsm6dsv16bx_6D_th {
	u8 deg;
	u8 val;
};

extern const struct dev_pm_ops st_lsm6dsv16bx_pm_ops;

static inline int
__st_lsm6dsv16bx_write_with_mask(struct st_lsm6dsv16bx_hw *hw,
				 unsigned int addr, unsigned int mask,
				 unsigned int data)
{
	int err;
	unsigned int val = ST_LSM6DSV16BX_SHIFT_VAL(data, mask);

	err = regmap_update_bits(hw->regmap, addr, mask, val);

	return err;
}

static inline int st_lsm6dsv16bx_write_with_mask(struct st_lsm6dsv16bx_hw *hw,
						 unsigned int addr,
						 unsigned int mask,
						 unsigned int data)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = __st_lsm6dsv16bx_write_with_mask(hw, addr, mask, data);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_lsm6dsv16bx_read_locked(struct st_lsm6dsv16bx_hw *hw, unsigned int addr,
			   void *data, unsigned int len)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = regmap_bulk_read(hw->regmap, addr, data, len);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_lsm6dsv16bx_write_locked(struct st_lsm6dsv16bx_hw *hw, unsigned int addr,
			    unsigned int val)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = regmap_write(hw->regmap, addr, val);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_lsm6dsv16bx_read_with_mask(struct st_lsm6dsv16bx_hw *hw, u8 addr, u8 mask,
			      u8 *val)
{
	u8 data;
	int err;

	mutex_lock(&hw->page_lock);
	err = regmap_bulk_read(hw->regmap, addr, &data, sizeof(data));
	if (err < 0)
		goto out;

	*val = (data & mask) >> __ffs(mask);

out:
	mutex_unlock(&hw->page_lock);

	return (err < 0) ? err : 0;
}
static inline int st_lsm6dsv16bx_set_page_access(struct st_lsm6dsv16bx_hw *hw,
						 unsigned int mask,
						 unsigned int data)
{
	return regmap_update_bits(hw->regmap,
				  ST_LSM6DSV16BX_REG_FUNC_CFG_ACCESS_ADDR,
				  mask,
				  ST_LSM6DSV16BX_SHIFT_VAL(data, mask));
}

static inline bool st_lsm6dsv16bx_run_mlc_task(struct st_lsm6dsv16bx_hw *hw)
{
	return hw->settings->st_mlc_probe || hw->settings->st_fsm_probe;
}

static inline bool
st_lsm6dsv16bx_is_fifo_enabled(struct st_lsm6dsv16bx_hw *hw)
{
	return hw->enable_mask & (BIT(ST_LSM6DSV16BX_ID_GYRO) |
				  BIT(ST_LSM6DSV16BX_ID_ACC)  |
				  BIT(ST_LSM6DSV16BX_ID_QVAR));
}

int st_lsm6dsv16bx_probe(struct device *dev, int irq, int hw_id,
			 struct regmap *regmap);
int st_lsm6dsv16bx_sensor_set_enable(struct st_lsm6dsv16bx_sensor *sensor,
				     bool enable);
int st_lsm6dsv16bx_sflp_set_enable(struct st_lsm6dsv16bx_sensor *sensor,
				   bool enable);
int st_lsm6dsv16bx_buffers_setup(struct st_lsm6dsv16bx_hw *hw);
ssize_t st_lsm6dsv16bx_flush_fifo(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size);
ssize_t st_lsm6dsv16bx_get_max_watermark(struct device *dev,
					 struct device_attribute *attr,
					 char *buf);
ssize_t st_lsm6dsv16bx_get_watermark(struct device *dev,
				     struct device_attribute *attr,
				     char *buf);
ssize_t st_lsm6dsv16bx_set_watermark(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size);
ssize_t st_lsm6dsv16bx_get_module_id(struct device *dev,
				     struct device_attribute *attr,
				     char *buf);
int st_lsm6dsv16bx_suspend_fifo(struct st_lsm6dsv16bx_hw *hw);
int st_lsm6dsv16bx_set_fifo_mode(struct st_lsm6dsv16bx_hw *hw,
				 enum st_lsm6dsv16bx_fifo_mode fifo_mode);
int
__st_lsm6dsv16bx_set_sensor_batching_odr(struct st_lsm6dsv16bx_sensor *sensor,
					 bool enable);
int st_lsm6dsv16bx_fsm_init(struct st_lsm6dsv16bx_hw *hw);
int st_lsm6dsv16bx_fsm_get_orientation(struct st_lsm6dsv16bx_hw *hw, u8 *data);
int st_lsm6dsv16bx_update_batching(struct iio_dev *iio_dev, bool enable);
int st_lsm6dsv16bx_get_batch_val(struct st_lsm6dsv16bx_sensor *sensor,
				 int odr, int uodr, u8 *val);

int st_lsm6dsv16bx_qvar_probe(struct st_lsm6dsv16bx_hw *hw);
int
st_lsm6dsv16bx_qvar_sensor_set_enable(struct st_lsm6dsv16bx_sensor *sensor,
				      bool enable);
#if defined(CONFIG_IIO_ST_LSM6DSV16BX_ASYNC_HW_TIMESTAMP)
int st_lsm6dsv16bx_hwtimesync_init(struct st_lsm6dsv16bx_hw *hw);
#else /* CONFIG_IIO_ST_LSM6DSV16BX_ASYNC_HW_TIMESTAMP */
static inline int
st_lsm6dsv16bx_hwtimesync_init(struct st_lsm6dsv16bx_hw *hw)
{
	return 0;
}
#endif /* CONFIG_IIO_ST_LSM6DSV16BX_ASYNC_HW_TIMESTAMP */

int st_lsm6dsv16bx_mlc_probe(struct st_lsm6dsv16bx_hw *hw);
int st_lsm6dsv16bx_mlc_remove(struct device *dev);
int st_lsm6dsv16bx_mlc_check_status(struct st_lsm6dsv16bx_hw *hw);
int st_lsm6dsv16bx_mlc_init_preload(struct st_lsm6dsv16bx_hw *hw);

int st_lsm6dsv16bx_probe_event(struct st_lsm6dsv16bx_hw *hw);
int st_lsm6dsv16bx_event_handler(struct st_lsm6dsv16bx_hw *hw);

int st_lsm6dsv16bx_probe_embfunc(struct st_lsm6dsv16bx_hw *hw);
int st_lsm6dsv16bx_embfunc_handler_thread(struct st_lsm6dsv16bx_hw *hw);
int st_lsm6dsv16bx_step_counter_set_enable(struct st_lsm6dsv16bx_sensor *sensor,
					   bool enable);
#endif /* ST_LSM6DSV16BX_H */
