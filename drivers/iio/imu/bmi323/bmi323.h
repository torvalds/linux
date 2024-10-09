/* SPDX-License-Identifier: GPL-2.0 */
/*
 * IIO driver for Bosch BMI323 6-Axis IMU
 *
 * Copyright (C) 2023, Jagath Jog J <jagathjog1996@gmail.com>
 */

#ifndef _BMI323_H_
#define _BMI323_H_

#include <linux/bits.h>
#include <linux/regmap.h>
#include <linux/units.h>

#define BMI323_I2C_DUMMY			2
#define BMI323_SPI_DUMMY			1

/* Register map */

#define BMI323_CHIP_ID_REG			0x00
#define BMI323_CHIP_ID_VAL			0x0043
#define BMI323_CHIP_ID_MSK			GENMASK(7, 0)
#define BMI323_ERR_REG				0x01
#define BMI323_STATUS_REG			0x02
#define BMI323_STATUS_POR_MSK			BIT(0)

/* Accelero/Gyro/Temp data registers */
#define BMI323_ACCEL_X_REG			0x03
#define BMI323_GYRO_X_REG			0x06
#define BMI323_TEMP_REG				0x09
#define BMI323_ALL_CHAN_MSK			GENMASK(5, 0)

/* Status registers */
#define BMI323_STATUS_INT1_REG			0x0D
#define BMI323_STATUS_INT2_REG			0x0E
#define BMI323_STATUS_NOMOTION_MSK		BIT(0)
#define BMI323_STATUS_MOTION_MSK		BIT(1)
#define BMI323_STATUS_STP_WTR_MSK		BIT(5)
#define BMI323_STATUS_TAP_MSK			BIT(8)
#define BMI323_STATUS_ERROR_MSK			BIT(10)
#define BMI323_STATUS_TMP_DRDY_MSK		BIT(11)
#define BMI323_STATUS_GYR_DRDY_MSK		BIT(12)
#define BMI323_STATUS_ACC_DRDY_MSK		BIT(13)
#define BMI323_STATUS_ACC_GYR_DRDY_MSK		GENMASK(13, 12)
#define BMI323_STATUS_FIFO_WTRMRK_MSK		BIT(14)
#define BMI323_STATUS_FIFO_FULL_MSK		BIT(15)

/* Feature registers */
#define BMI323_FEAT_IO0_REG			0x10
#define BMI323_FEAT_IO0_XYZ_NOMOTION_MSK	GENMASK(2, 0)
#define BMI323_FEAT_IO0_XYZ_MOTION_MSK		GENMASK(5, 3)
#define BMI323_FEAT_XYZ_MSK			GENMASK(2, 0)
#define BMI323_FEAT_IO0_STP_CNT_MSK		BIT(9)
#define BMI323_FEAT_IO0_S_TAP_MSK		BIT(12)
#define BMI323_FEAT_IO0_D_TAP_MSK		BIT(13)
#define BMI323_FEAT_IO1_REG			0x11
#define BMI323_FEAT_IO1_ERR_MSK			GENMASK(3, 0)
#define BMI323_FEAT_IO2_REG			0x12
#define BMI323_FEAT_IO_STATUS_REG		0x14
#define BMI323_FEAT_IO_STATUS_MSK		BIT(0)
#define BMI323_FEAT_ENG_POLL			2000
#define BMI323_FEAT_ENG_TIMEOUT			10000

/* FIFO registers */
#define BMI323_FIFO_FILL_LEVEL_REG		0x15
#define BMI323_FIFO_DATA_REG			0x16

/* Accelero/Gyro config registers */
#define BMI323_ACC_CONF_REG			0x20
#define BMI323_GYRO_CONF_REG			0x21
#define BMI323_ACC_GYRO_CONF_MODE_MSK		GENMASK(14, 12)
#define BMI323_ACC_GYRO_CONF_ODR_MSK		GENMASK(3, 0)
#define BMI323_ACC_GYRO_CONF_SCL_MSK		GENMASK(6, 4)
#define BMI323_ACC_GYRO_CONF_BW_MSK		BIT(7)
#define BMI323_ACC_GYRO_CONF_AVG_MSK		GENMASK(10, 8)

/* FIFO registers */
#define BMI323_FIFO_WTRMRK_REG			0x35
#define BMI323_FIFO_CONF_REG			0x36
#define BMI323_FIFO_CONF_STP_FUL_MSK		BIT(0)
#define BMI323_FIFO_CONF_ACC_GYR_EN_MSK		GENMASK(10, 9)
#define BMI323_FIFO_ACC_GYR_MSK			GENMASK(1, 0)
#define BMI323_FIFO_CTRL_REG			0x37
#define BMI323_FIFO_FLUSH_MSK			BIT(0)

/* Interrupt pin config registers */
#define BMI323_IO_INT_CTR_REG			0x38
#define BMI323_IO_INT1_LVL_MSK			BIT(0)
#define BMI323_IO_INT1_OD_MSK			BIT(1)
#define BMI323_IO_INT1_OP_EN_MSK		BIT(2)
#define BMI323_IO_INT1_LVL_OD_OP_MSK		GENMASK(2, 0)
#define BMI323_IO_INT2_LVL_MSK			BIT(8)
#define BMI323_IO_INT2_OD_MSK			BIT(9)
#define BMI323_IO_INT2_OP_EN_MSK		BIT(10)
#define BMI323_IO_INT2_LVL_OD_OP_MSK		GENMASK(10, 8)
#define BMI323_IO_INT_CONF_REG			0x39
#define BMI323_IO_INT_LTCH_MSK			BIT(0)
#define BMI323_INT_MAP1_REG			0x3A
#define BMI323_INT_MAP2_REG			0x3B
#define BMI323_NOMOTION_MSK			GENMASK(1, 0)
#define BMI323_MOTION_MSK			GENMASK(3, 2)
#define BMI323_STEP_CNT_MSK			GENMASK(11, 10)
#define BMI323_TAP_MSK				GENMASK(1, 0)
#define BMI323_TMP_DRDY_MSK			GENMASK(7, 6)
#define BMI323_GYR_DRDY_MSK			GENMASK(9, 8)
#define BMI323_ACC_DRDY_MSK			GENMASK(11, 10)
#define BMI323_FIFO_WTRMRK_MSK			GENMASK(13, 12)
#define BMI323_FIFO_FULL_MSK			GENMASK(15, 14)

/* Feature registers */
#define BMI323_FEAT_CTRL_REG			0x40
#define BMI323_FEAT_ENG_EN_MSK			BIT(0)
#define BMI323_FEAT_DATA_ADDR			0x41
#define BMI323_FEAT_DATA_TX			0x42
#define BMI323_FEAT_DATA_STATUS			0x43
#define BMI323_FEAT_DATA_TX_RDY_MSK		BIT(1)
#define BMI323_FEAT_EVNT_EXT_REG		0x47
#define BMI323_FEAT_EVNT_EXT_S_MSK		BIT(3)
#define BMI323_FEAT_EVNT_EXT_D_MSK		BIT(4)

#define BMI323_CMD_REG				0x7E
#define BMI323_RST_VAL				0xDEAF
#define BMI323_CFG_RES_REG			0x7F

/* Extended registers */
#define BMI323_GEN_SET1_REG			0x02
#define BMI323_GEN_SET1_MODE_MSK		BIT(0)
#define BMI323_GEN_HOLD_DUR_MSK			GENMASK(4, 1)

/* Any Motion/No Motion config registers */
#define BMI323_ANYMO1_REG			0x05
#define BMI323_NOMO1_REG			0x08
#define BMI323_MO2_OFFSET			0x01
#define BMI323_MO3_OFFSET			0x02
#define BMI323_MO1_REF_UP_MSK			BIT(12)
#define BMI323_MO1_SLOPE_TH_MSK			GENMASK(11, 0)
#define BMI323_MO2_HYSTR_MSK			GENMASK(9, 0)
#define BMI323_MO3_DURA_MSK			GENMASK(12, 0)

/* Step counter config registers */
#define BMI323_STEP_SC1_REG			0x10
#define BMI323_STEP_SC1_WTRMRK_MSK		GENMASK(9, 0)
#define BMI323_STEP_SC1_RST_CNT_MSK		BIT(10)
#define BMI323_STEP_SC1_REG			0x10
#define BMI323_STEP_LEN				2

/* Tap gesture config registers */
#define BMI323_TAP1_REG				0x1E
#define BMI323_TAP1_AXIS_SEL_MSK		GENMASK(1, 0)
#define BMI323_AXIS_XYZ_MSK			GENMASK(1, 0)
#define BMI323_TAP1_TIMOUT_MSK			BIT(2)
#define BMI323_TAP1_MAX_PEAKS_MSK		GENMASK(5, 3)
#define BMI323_TAP1_MODE_MSK			GENMASK(7, 6)
#define BMI323_TAP2_REG				0x1F
#define BMI323_TAP2_THRES_MSK			GENMASK(9, 0)
#define BMI323_TAP2_MAX_DUR_MSK			GENMASK(15, 10)
#define BMI323_TAP3_REG				0x20
#define BMI323_TAP3_QUIET_TIM_MSK		GENMASK(15, 12)
#define BMI323_TAP3_QT_BW_TAP_MSK		GENMASK(11, 8)
#define BMI323_TAP3_QT_AFT_GES_MSK		GENMASK(15, 12)

#define BMI323_MOTION_THRES_SCALE		512
#define BMI323_MOTION_HYSTR_SCALE		512
#define BMI323_MOTION_DURAT_SCALE		50
#define BMI323_TAP_THRES_SCALE			512
#define BMI323_DUR_BW_TAP_SCALE			200
#define BMI323_QUITE_TIM_GES_SCALE		25
#define BMI323_MAX_GES_DUR_SCALE		25

/*
 * The formula to calculate temperature in C.
 * See datasheet section 6.1.1, Register Map Overview
 *
 * T_C = (temp_raw / 512) + 23
 */
#define BMI323_TEMP_OFFSET			11776
#define BMI323_TEMP_SCALE			1953125

/*
 * The BMI323 features a FIFO with a capacity of 2048 bytes. Each frame
 * consists of accelerometer (X, Y, Z) data and gyroscope (X, Y, Z) data,
 * totaling 6 words or 12 bytes. The FIFO buffer can hold a total of
 * 170 frames.
 *
 * If a watermark interrupt is configured for 170 frames, the interrupt will
 * trigger when the FIFO reaches 169 frames, so limit the maximum watermark
 * level to 169 frames. In terms of data, 169 frames would equal 1014 bytes,
 * which is approximately 2 frames before the FIFO reaches its full capacity.
 * See datasheet section 5.7.3 FIFO Buffer Interrupts
 */
#define BMI323_BYTES_PER_SAMPLE			2
#define BMI323_FIFO_LENGTH_IN_BYTES		2048
#define BMI323_FIFO_FRAME_LENGTH		6
#define BMI323_FIFO_FULL_IN_FRAMES		\
	((BMI323_FIFO_LENGTH_IN_BYTES /		\
	(BMI323_BYTES_PER_SAMPLE * BMI323_FIFO_FRAME_LENGTH)) - 1)
#define BMI323_FIFO_FULL_IN_WORDS		\
	(BMI323_FIFO_FULL_IN_FRAMES * BMI323_FIFO_FRAME_LENGTH)

#define BMI323_INT_MICRO_TO_RAW(val, val2, scale) ((val) * (scale) + \
						  ((val2) * (scale)) / MEGA)

#define BMI323_RAW_TO_MICRO(raw, scale) ((((raw) % (scale)) * MEGA) / scale)

struct device;
int bmi323_core_probe(struct device *dev);
extern const struct regmap_config bmi323_regmap_config;
extern const struct dev_pm_ops bmi323_core_pm_ops;

#endif
