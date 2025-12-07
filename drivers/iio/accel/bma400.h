/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Register constants and other forward declarations needed by the bma400
 * sources.
 *
 * Copyright 2019 Dan Robertson <dan@dlrobertson.com>
 */

#ifndef _BMA400_H_
#define _BMA400_H_

#include <linux/bits.h>
#include <linux/regmap.h>

/*
 * Read-Only Registers
 */

/* Chip ID of BMA 400 devices found in the chip ID register. */
#define BMA400_ID_REG_VAL           0x90

/* Status and ID registers */
#define BMA400_CHIP_ID_REG          0x00
#define BMA400_ERR_REG              0x02
#define BMA400_STATUS_REG           0x03

/* Acceleration registers */
#define BMA400_ACC_X_LSB_REG		0x04
#define BMA400_ACC_X_MSB_REG		0x05
#define BMA400_ACC_Y_LSB_REG		0x06
#define BMA400_ACC_Y_MSB_REG		0x07
#define BMA400_ACC_Z_LSB_REG		0x08
#define BMA400_ACC_Z_MSB_REG		0x09

/* Sensor time registers */
#define BMA400_SENSOR_TIME0_REG         0x0a
#define BMA400_SENSOR_TIME1_REG         0x0b
#define BMA400_SENSOR_TIME2_REG         0x0c

/* Event and interrupt registers */
#define BMA400_EVENT_REG            0x0d

#define BMA400_INT_STAT0_REG        0x0e
#define BMA400_INT_STAT0_GEN1_MASK		BIT(2)
#define BMA400_INT_STAT0_GEN2_MASK		BIT(3)
#define BMA400_INT_STAT0_DRDY_MASK		BIT(7)

#define BMA400_INT_STAT1_REG        0x0f
#define BMA400_INT_STAT1_STEP_INT_MASK		GENMASK(9, 8)
#define BMA400_INT_STAT1_S_TAP_MASK		BIT(10)
#define BMA400_INT_STAT1_D_TAP_MASK		BIT(11)

#define BMA400_INT_STAT2_REG        0x10

/* Bit present in all INT_STAT registers */
#define BMA400_INT_STAT_ENG_OVRRUN_MASK		BIT(4)

/* Temperature register */
#define BMA400_TEMP_DATA_REG        0x11

/* FIFO length and data registers */
#define BMA400_FIFO_LENGTH0_REG     0x12
#define BMA400_FIFO_LENGTH1_REG     0x13
#define BMA400_FIFO_DATA_REG        0x14

/* Step count registers */
#define BMA400_STEP_CNT0_REG        0x15
#define BMA400_STEP_CNT1_REG        0x16
#define BMA400_STEP_CNT3_REG        0x17
#define BMA400_STEP_STAT_REG        0x18
#define BMA400_STEP_RAW_LEN         0x03

/*
 * Read-write configuration registers
 */
#define BMA400_ACC_CONFIG0_REG		0x19
#define BMA400_ACC_CONFIG0_LP_OSR_MASK		GENMASK(6, 5)

#define BMA400_ACC_CONFIG1_REG		0x1a
#define BMA400_ACC_CONFIG1_ODR_MASK		GENMASK(3, 0)
#define BMA400_ACC_CONFIG1_ODR_MIN_RAW		0x05
#define BMA400_ACC_CONFIG1_ODR_LP_RAW		0x06
#define BMA400_ACC_CONFIG1_ODR_MAX_RAW		0x0b
#define BMA400_ACC_CONFIG1_ODR_MAX_HZ		800
#define BMA400_ACC_CONFIG1_ODR_MIN_WHOLE_HZ	25
#define BMA400_ACC_CONFIG1_ODR_MIN_HZ		12
#define BMA400_ACC_CONFIG1_NP_OSR_MASK		GENMASK(5, 4)
#define BMA400_ACC_CONFIG1_ACC_RANGE_MASK	GENMASK(7, 6)

#define BMA400_ACC_CONFIG2_REG      0x1b

/* Interrupt registers */
#define BMA400_INT_CONFIG0_REG	    0x1f
#define BMA400_INT_CONFIG0_GEN1_MASK		BIT(2)
#define BMA400_INT_CONFIG0_GEN2_MASK		BIT(3)
#define BMA400_INT_CONFIG0_DRDY_MASK		BIT(7)

enum bma400_generic_intr {
	BMA400_GEN1_INTR = 0x1,
	BMA400_GEN2_INTR = 0x2,
};

#define BMA400_INT_CONFIG1_REG	    0x20
#define BMA400_INT_CONFIG1_STEP_INT_MASK	BIT(0)
#define BMA400_INT_CONFIG1_S_TAP_MASK		BIT(2)
#define BMA400_INT_CONFIG1_D_TAP_MASK		BIT(3)

#define BMA400_INT1_MAP_REG	    0x21
#define BMA400_INT12_MAP_REG        0x23
#define BMA400_INT_IO_CTRL_REG	    0x24

#define BMA400_TWO_BITS_MASK        GENMASK(1, 0)

/* Generic interrupts register */
#define BMA400_GENINT_CONFIG_REG_BASE		0x3f
#define BMA400_NUM_GENINT_CONFIG_REGS		11
#define BMA400_GENINT_CONFIG_REG(gen_intr, config_idx)		\
	(BMA400_GENINT_CONFIG_REG_BASE +			\
	(gen_intr - 1) * BMA400_NUM_GENINT_CONFIG_REGS +	\
	(config_idx))
#define BMA400_GENINT_CONFIG0_HYST_MASK		GENMASK(1, 0)
#define BMA400_GENINT_CONFIG0_REF_UPD_MODE_MASK	GENMASK(3, 2)
#define BMA400_GENINT_CONFIG0_DATA_SRC_MASK	BIT(4)
#define BMA400_GENINT_CONFIG0_X_EN_MASK		BIT(5)
#define BMA400_GENINT_CONFIG0_Y_EN_MASK		BIT(6)
#define BMA400_GENINT_CONFIG0_Z_EN_MASK		BIT(7)

enum bma400_accel_data_src {
	ACCEL_FILT1 = 0x0,
	ACCEL_FILT2 = 0x1,
};

enum bma400_ref_updt_mode {
	BMA400_REF_MANUAL_UPDT_MODE = 0x0,
	BMA400_REF_ONETIME_UPDT_MODE = 0x1,
	BMA400_REF_EVERYTIME_UPDT_MODE = 0x2,
	BMA400_REF_EVERYTIME_LP_UPDT_MODE = 0x3,
};

#define BMA400_GEN_CONFIG1_OFF      0x01
#define BMA400_GENINT_CONFIG1_AXES_COMB_MASK	BIT(0)
#define BMA400_GENINT_CONFIG1_DETCT_CRIT_MASK	BIT(1)

enum bma400_genintr_acceleval_axescomb {
	BMA400_EVAL_X_OR_Y_OR_Z = 0x0,
	BMA400_EVAL_X_AND_Y_AND_Z = 0x1,
};

enum bma400_detect_criterion {
	BMA400_DETECT_INACTIVITY = 0x0,
	BMA400_DETECT_ACTIVITY = 0x1,
};

/* TAP config registers */
#define BMA400_TAP_CONFIG_REG		0x57
#define BMA400_TAP_CONFIG_SEN_MASK	GENMASK(2, 0)

#define BMA400_TAP_CONFIG1_REG          0x58
#define BMA400_TAP_CONFIG1_TICSTH_MASK		GENMASK(1, 0)
#define BMA400_TAP_CONFIG1_QUIET_MASK		GENMASK(3, 2)
#define BMA400_TAP_CONFIG1_QUIETDT_MASK		GENMASK(5, 4)
#define BMA400_TAP_TIM_LIST_LEN     4

#define BMA400_CMD_REG              0x7e
/*
 * BMA400_SCALE_MIN macro value represents m/s^2 for 1 LSB before
 * converting to micro values for +-2g range.
 *
 * For +-2g - 1 LSB = 0.976562 milli g = 0.009576 m/s^2
 * For +-4g - 1 LSB = 1.953125 milli g = 0.019153 m/s^2
 * For +-16g - 1 LSB = 7.8125 milli g = 0.076614 m/s^2
 *
 * The raw value which is used to select the different ranges is determined
 * by the first bit set position from the scale value, so BMA400_SCALE_MIN
 * should be odd.
 *
 * Scale values for +-2g, +-4g, +-8g and +-16g are populated into bma400_scales
 * array by left shifting BMA400_SCALE_MIN.
 * e.g.:
 * To select +-2g = 9577 << 0 = raw value to write is 0.
 * To select +-8g = 9577 << 2 = raw value to write is 2.
 * To select +-16g = 9577 << 3 = raw value to write is 3.
 */
#define BMA400_ACC_SCALE_MIN            9577
#define BMA400_ACC_SCALE_MAX            76617

extern const struct regmap_config bma400_regmap_config;

int bma400_probe(struct device *dev, struct regmap *regmap, int irq,
		 const char *name);

#endif
