/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics st_mag40 driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#ifndef __ST_MAG40_H
#define __ST_MAG40_H

#include <linux/types.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/version.h>

#define ST_MAG40_DEV_NAME			"st_mag40"
#define LIS2MDL_DEV_NAME			"lis2mdl_magn"
#define LSM303AH_DEV_NAME			"lsm303ah_magn"
#define LSM303AGR_DEV_NAME			"lsm303agr_magn"
#define ISM303DAC_DEV_NAME			"ism303dac_magn"
#define IIS2MDC_DEV_NAME			"iis2mdc_magn"

/* Power Modes */
enum {
	ST_MAG40_LWC_MODE = 0,
	ST_MAG40_NORMAL_MODE,
	ST_MAG40_MODE_COUNT,
};

#define ST_MAG40_WHO_AM_I_ADDR				0x4f
#define ST_MAG40_WHO_AM_I_DEF				0x40

/* Magnetometer control registers */
#define ST_MAG40_CFG_REG_A_ADDR				0x60
#define ST_MAG40_TEMP_COMP_EN				0x80
#define ST_MAG40_CFG_REG_A_ODR_MASK			0x0c
#define ST_MAG40_CFG_REG_A_ODR_10Hz			0x00
#define ST_MAG40_CFG_REG_A_ODR_20Hz			0x01
#define ST_MAG40_CFG_REG_A_ODR_50Hz			0x02
#define ST_MAG40_CFG_REG_A_ODR_100Hz			0x03
#define ST_MAG40_CFG_REG_A_ODR_COUNT			4
#define ST_MAG40_CFG_REG_A_MD_MASK			0x03
#define ST_MAG40_CFG_REG_A_MD_CONT			0x00
#define ST_MAG40_CFG_REG_A_MD_IDLE			0x03

#define ST_MAG40_ODR_ADDR				ST_MAG40_CFG_REG_A_ADDR
#define ST_MAG40_ODR_MASK				ST_MAG40_CFG_REG_A_ODR_MASK

#define ST_MAG40_EN_ADDR				ST_MAG40_CFG_REG_A_ADDR
#define ST_MAG40_EN_MASK				ST_MAG40_CFG_REG_A_MD_MASK

#define ST_MAG40_CFG_REG_B_ADDR				0x61
#define ST_MAG40_CFG_REG_B_OFF_CANC_MASK		0x02

#define ST_MAG40_CFG_REG_C_ADDR				0x62
#define ST_MAG40_CFG_REG_C_BDU_MASK			0x10
#define ST_MAG40_CFG_REG_C_SELFTEST_MASK		0x02
#define ST_MAG40_CFG_REG_C_INT_MASK			0x01

#define ST_MAG40_INT_DRDY_ADDR				ST_MAG40_CFG_REG_C_ADDR
#define ST_MAG40_INT_DRDY_MASK				ST_MAG40_CFG_REG_C_INT_MASK

#define ST_MAG40_STATUS_ADDR				0x67
#define ST_MAG40_AVL_DATA_MASK				0x7
#define ST_MAG40_STATUS_ZYXDA_MASK			BIT(3)

/* Magnetometer output registers */
#define ST_MAG40_OUTX_L_ADDR				0x68
#define ST_MAG40_OUTY_L_ADDR				0x6A
#define ST_MAG40_OUTZ_L_ADDR				0x6C

#define ST_MAG40_BDU_ADDR				ST_MAG40_CTRL1_ADDR
#define ST_MAG40_BDU_MASK				0x02

#define ST_MAG40_TURNON_TIME_SAMPLES_NUM		2

/* 3 axis of 16 bit each */
#define ST_MAG40_OUT_LEN				6

#define ST_MAG40_TX_MAX_LENGTH				16
#define ST_MAG40_RX_MAX_LENGTH				16

#define ST_MAG40_SELFTEST_MIN				15
#define ST_MAG40_SELFTEST_MAX				500
#define ST_MAG40_ST_READ_CYCLES				50

struct st_mag40_transfer_buffer {
	u8 rx_buf[ST_MAG40_RX_MAX_LENGTH];
	u8 tx_buf[ST_MAG40_TX_MAX_LENGTH] ____cacheline_aligned;
};

struct st_mag40_data;

struct st_mag40_transfer_function {
	int (*write)(struct st_mag40_data *cdata, u8 reg_addr, int len, u8 *data);
	int (*read)(struct st_mag40_data *cdata, u8 reg_addr, int len, u8 *data);
};

enum st_mag40_selftest_status {
	ST_MAG40_ST_RESET,
	ST_MAG40_ST_ERROR,
	ST_MAG40_ST_PASS,
	ST_MAG40_ST_FAIL,
};

struct st_mag40_data {
	const char *name;
	struct mutex lock;
	u8 drdy_int_pin;
	int irq;
	s64 ts;
	s64 ts_irq;
	s64 delta_ts;

	enum st_mag40_selftest_status st_status;

	u32 module_id;

	u16 odr;
	u8 samples_to_discard;

	struct device *dev;
	struct iio_trigger *iio_trig;
	const struct st_mag40_transfer_function *tf;
	struct st_mag40_transfer_buffer tb;
};

static inline s64 st_mag40_get_timestamp(struct iio_dev *iio_dev)
{
	return iio_get_time_ns(iio_dev);
}

int st_mag40_common_probe(struct iio_dev *iio_dev);
void st_mag40_common_remove(struct iio_dev *iio_dev);

#ifdef CONFIG_PM
int st_mag40_common_suspend(struct st_mag40_data *cdata);
int st_mag40_common_resume(struct st_mag40_data *cdata);
#endif /* CONFIG_PM */

int st_mag40_allocate_ring(struct iio_dev *iio_dev);
int st_mag40_allocate_trigger(struct iio_dev *iio_dev);
int st_mag40_trig_set_state(struct iio_trigger *trig, bool state);
int st_mag40_set_enable(struct st_mag40_data *cdata, bool enable);
void st_mag40_deallocate_ring(struct iio_dev *iio_dev);
void st_mag40_deallocate_trigger(struct st_mag40_data *cdata);
int st_mag40_write_register(struct st_mag40_data *cdata, u8 reg_addr, u8 mask, u8 data);

#endif /* __ST_MAG40_H */
