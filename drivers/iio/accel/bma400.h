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

/* Status and ID registers */
#define BMA400_CHIP_ID_REG          0x00
#define BMA400_ERR_REG              0x02
#define BMA400_STATUS_REG           0x03

/* Acceleration registers */
#define BMA400_X_AXIS_LSB_REG       0x04
#define BMA400_X_AXIS_MSB_REG       0x05
#define BMA400_Y_AXIS_LSB_REG       0x06
#define BMA400_Y_AXIS_MSB_REG       0x07
#define BMA400_Z_AXIS_LSB_REG       0x08
#define BMA400_Z_AXIS_MSB_REG       0x09

/* Sensor time registers */
#define BMA400_SENSOR_TIME0         0x0a
#define BMA400_SENSOR_TIME1         0x0b
#define BMA400_SENSOR_TIME2         0x0c

/* Event and interrupt registers */
#define BMA400_EVENT_REG            0x0d
#define BMA400_INT_STAT0_REG        0x0e
#define BMA400_INT_STAT1_REG        0x0f
#define BMA400_INT_STAT2_REG        0x10
#define BMA400_INT12_MAP_REG        0x23

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
#define BMA400_STEP_INT_MSK         BIT(0)
#define BMA400_STEP_RAW_LEN         0x03
#define BMA400_STEP_STAT_MASK       GENMASK(9, 8)

/*
 * Read-write configuration registers
 */
#define BMA400_ACC_CONFIG0_REG      0x19
#define BMA400_ACC_CONFIG1_REG      0x1a
#define BMA400_ACC_CONFIG2_REG      0x1b
#define BMA400_CMD_REG              0x7e

/* Interrupt registers */
#define BMA400_INT_CONFIG0_REG	    0x1f
#define BMA400_INT_CONFIG1_REG	    0x20
#define BMA400_INT1_MAP_REG	    0x21
#define BMA400_INT_IO_CTRL_REG	    0x24
#define BMA400_INT_DRDY_MSK	    BIT(7)

/* Chip ID of BMA 400 devices found in the chip ID register. */
#define BMA400_ID_REG_VAL           0x90

#define BMA400_LP_OSR_SHIFT         5
#define BMA400_NP_OSR_SHIFT         4
#define BMA400_SCALE_SHIFT          6

#define BMA400_TWO_BITS_MASK        GENMASK(1, 0)
#define BMA400_LP_OSR_MASK          GENMASK(6, 5)
#define BMA400_NP_OSR_MASK          GENMASK(5, 4)
#define BMA400_ACC_ODR_MASK         GENMASK(3, 0)
#define BMA400_ACC_SCALE_MASK       GENMASK(7, 6)

#define BMA400_ACC_ODR_MIN_RAW      0x05
#define BMA400_ACC_ODR_LP_RAW       0x06
#define BMA400_ACC_ODR_MAX_RAW      0x0b

#define BMA400_ACC_ODR_MAX_HZ       800
#define BMA400_ACC_ODR_MIN_WHOLE_HZ 25
#define BMA400_ACC_ODR_MIN_HZ       12

/* Generic interrupts register */
#define BMA400_GEN1INT_CONFIG0      0x3f
#define BMA400_GEN2INT_CONFIG0      0x4A
#define BMA400_GEN_CONFIG1_OFF      0x01
#define BMA400_GEN_CONFIG2_OFF      0x02
#define BMA400_GEN_CONFIG3_OFF      0x03
#define BMA400_GEN_CONFIG31_OFF     0x04
#define BMA400_INT_GEN1_MSK         BIT(2)
#define BMA400_INT_GEN2_MSK         BIT(3)
#define BMA400_GEN_HYST_MSK         GENMASK(1, 0)

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
#define BMA400_SCALE_MIN            9577
#define BMA400_SCALE_MAX            76617

#define BMA400_NUM_REGULATORS       2
#define BMA400_VDD_REGULATOR        0
#define BMA400_VDDIO_REGULATOR      1

extern const struct regmap_config bma400_regmap_config;

int bma400_probe(struct device *dev, struct regmap *regmap, int irq,
		 const char *name);

#endif
