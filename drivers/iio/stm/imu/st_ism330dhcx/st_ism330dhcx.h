/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics st_ism330dhcx sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2020 STMicroelectronics Inc.
 */

#ifndef ST_ISM330DHCX_H
#define ST_ISM330DHCX_H

#include <linux/device.h>
#include <linux/iio/iio.h>
#include <linux/of_device.h>
#include <linux/delay.h>

#include "../../common/stm_iio_types.h"

#define ST_ISM330DHCX_MAX_ODR			833
#define ST_ISM330DHCX_ODR_LIST_SIZE		8
#define ST_ISM330DHCX_ODR_EXPAND(odr, uodr)	((odr * 1000000) + uodr)

#define ST_ISM330DHCX_DEV_NAME			"ism330dhcx"

#define ST_ISM330DHCX_REG_FUNC_CFG_ACCESS_ADDR	0x01
#define ST_ISM330DHCX_REG_SHUB_REG_MASK		BIT(6)
#define ST_ISM330DHCX_REG_FUNC_CFG_MASK		BIT(7)

#define ST_ISM330DHCX_REG_FIFO_CTRL1_ADDR		0x07
#define ST_ISM330DHCX_REG_FIFO_CTRL2_ADDR		0x08
#define ST_ISM330DHCX_REG_FIFO_WTM_MASK		GENMASK(8, 0)
#define ST_ISM330DHCX_REG_FIFO_WTM8_MASK		BIT(0)

#define ST_ISM330DHCX_REG_FIFO_CTRL3_ADDR		0x09
#define ST_ISM330DHCX_REG_BDR_XL_MASK		GENMASK(3, 0)
#define ST_ISM330DHCX_REG_BDR_GY_MASK		GENMASK(7, 4)

#define ST_ISM330DHCX_REG_FIFO_CTRL4_ADDR		0x0a
#define ST_ISM330DHCX_REG_FIFO_MODE_MASK		GENMASK(2, 0)
#define ST_ISM330DHCX_REG_ODR_T_BATCH_MASK		GENMASK(5, 4)
#define ST_ISM330DHCX_REG_DEC_TS_MASK		GENMASK(7, 6)

#define ST_ISM330DHCX_REG_INT1_CTRL_ADDR		0x0d
#define ST_ISM330DHCX_REG_INT2_CTRL_ADDR		0x0e
#define ST_ISM330DHCX_REG_INT_FIFO_TH_MASK		BIT(3)

#define ST_ISM330DHCX_REG_WHOAMI_ADDR		0x0f
#define ST_ISM330DHCX_WHOAMI_VAL			0x6b

#define ST_ISM330DHCX_CTRL1_XL_ADDR		0x10
#define ST_ISM330DHCX_CTRL2_G_ADDR			0x11
#define ST_ISM330DHCX_REG_CTRL3_C_ADDR		0x12
#define ST_ISM330DHCX_REG_SW_RESET_MASK		BIT(0)
#define ST_ISM330DHCX_REG_PP_OD_MASK		BIT(4)
#define ST_ISM330DHCX_REG_H_LACTIVE_MASK		BIT(5)
#define ST_ISM330DHCX_REG_BDU_MASK			BIT(6)
#define ST_ISM330DHCX_REG_BOOT_MASK		BIT(7)

#define ST_ISM330DHCX_REG_CTRL4_C_ADDR		0x13
#define ST_ISM330DHCX_REG_DRDY_MASK		BIT(3)

#define ST_ISM330DHCX_REG_CTRL5_C_ADDR		0x14
#define ST_ISM330DHCX_REG_ROUNDING_MASK		GENMASK(6, 5)
#define ST_ISM330DHCX_REG_ST_G_MASK		GENMASK(3, 2)
#define ST_ISM330DHCX_REG_ST_XL_MASK		GENMASK(1, 0)

#define ST_ISM330DHCX_SELFTEST_ACCEL_MIN	737
#define ST_ISM330DHCX_SELFTEST_ACCEL_MAX	13934
#define ST_ISM330DHCX_SELFTEST_GYRO_MIN		2142
#define ST_ISM330DHCX_SELFTEST_GYRO_MAX		10000

#define ST_ISM330DHCX_SELF_TEST_DISABLED_VAL	0
#define ST_ISM330DHCX_SELF_TEST_POS_SIGN_VAL	1
#define ST_ISM330DHCX_SELF_TEST_NEG_ACCEL_SIGN_VAL	2
#define ST_ISM330DHCX_SELF_TEST_NEG_GYRO_SIGN_VAL	3

#define ST_ISM330DHCX_REG_CTRL9_XL_ADDR		0x18
#define ST_ISM330DHCX_REG_I3C_DISABLE_MASK		BIT(1)

#define ST_ISM330DHCX_REG_CTRL10_C_ADDR		0x19
#define ST_ISM330DHCX_REG_TIMESTAMP_EN_MASK	BIT(5)

#define ST_ISM330DHCX_REG_STATUS_ADDR		0x1e
#define ST_ISM330DHCX_REG_STATUS_XLDA		BIT(0)
#define ST_ISM330DHCX_REG_STATUS_GDA		BIT(1)
#define ST_ISM330DHCX_REG_STATUS_TDA		BIT(2)

#define ST_ISM330DHCX_REG_OUT_TEMP_L_ADDR		0x20

#define ST_ISM330DHCX_REG_OUTX_L_G_ADDR		0x22
#define ST_ISM330DHCX_REG_OUTY_L_G_ADDR		0x24
#define ST_ISM330DHCX_REG_OUTZ_L_G_ADDR		0x26

#define ST_ISM330DHCX_REG_OUTX_L_A_ADDR		0x28
#define ST_ISM330DHCX_REG_OUTY_L_A_ADDR		0x2a
#define ST_ISM330DHCX_REG_OUTZ_L_A_ADDR		0x2c

#define ST_ISM330DHCX_REG_FIFO_STATUS1_ADDR	0x3a
#define ST_ISM330DHCX_REG_FIFO_STATUS_DIFF		GENMASK(9, 0)

#define ST_ISM330DHCX_REG_TIMESTAMP0_ADDR		0x40
#define ST_ISM330DHCX_REG_TIMESTAMP2_ADDR		0x42

#define ST_ISM330DHCX_REG_TAP_CFG0_ADDR		0x56
#define ST_ISM330DHCX_REG_TAP_X_EN_MASK		BIT(3)
#define ST_ISM330DHCX_REG_TAP_Y_EN_MASK		BIT(2)
#define ST_ISM330DHCX_REG_TAP_Z_EN_MASK		BIT(1)
#define ST_ISM330DHCX_REG_LIR_MASK			BIT(0)

#define ST_ISM330DHCX_REG_MD1_CFG_ADDR		0x5e
#define ST_ISM330DHCX_REG_MD2_CFG_ADDR		0x5f
#define ST_ISM330DHCX_REG_INT2_TIMESTAMP_MASK	BIT(0)
#define ST_ISM330DHCX_REG_INT_EMB_FUNC_MASK	BIT(1)

#define ST_ISM330DHCX_INTERNAL_FREQ_FINE		0x63

#define ST_ISM330DHCX_REG_FIFO_DATA_OUT_TAG_ADDR	0x78

/* embedded registers */
#define ST_ISM330DHCX_REG_EMB_FUNC_INT1_ADDR	0x0a
#define ST_ISM330DHCX_REG_EMB_FUNC_INT2_ADDR	0x0e

/* Timestamp Tick 25us/LSB */
#define ST_ISM330DHCX_TS_DELTA_NS			25000ULL

#define ST_ISM330DHCX_TEMP_GAIN			256
#define ST_ISM330DHCX_TEMP_FS_GAIN			(1000000 / ST_ISM330DHCX_TEMP_GAIN)
#define ST_ISM330DHCX_TEMP_OFFSET			6400

#define ST_ISM330DHCX_SAMPLE_SIZE			6
#define ST_ISM330DHCX_TS_SAMPLE_SIZE		4
#define ST_ISM330DHCX_TAG_SIZE			1
#define ST_ISM330DHCX_FIFO_SAMPLE_SIZE		(ST_ISM330DHCX_SAMPLE_SIZE + \
						 ST_ISM330DHCX_TAG_SIZE)
#define ST_ISM330DHCX_MAX_FIFO_DEPTH		416

#define ST_ISM330DHCX_DATA_CHANNEL(chan_type, addr, mod, ch2, scan_idx,	\
				rb, sb, sg)				\
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
		.endianness = IIO_LE,					\
	},								\
}

static const struct iio_event_spec st_ism330dhcx_flush_event = {
	.type = STM_IIO_EV_TYPE_FIFO_FLUSH,
	.dir = IIO_EV_DIR_EITHER,
};

static const struct iio_event_spec st_ism330dhcx_thr_event = {
	.type = IIO_EV_TYPE_THRESH,
	.dir = IIO_EV_DIR_RISING,
	.mask_separate = BIT(IIO_EV_INFO_ENABLE),
};

#define ST_ISM330DHCX_EVENT_CHANNEL(ctype, etype)		\
{							\
	.type = ctype,					\
	.modified = 0,					\
	.scan_index = -1,				\
	.indexed = -1,					\
	.event_spec = &st_ism330dhcx_##etype##_event,	\
	.num_event_specs = 1,				\
}

#define ST_ISM330DHCX_RX_MAX_LENGTH		64
#define ST_ISM330DHCX_TX_MAX_LENGTH		16

/**
 * @struct st_ism330dhcx_transfer_buffer
 * @brief Buffer support for data transfer
 *
 * rx_buf: Data receive buffer.
 * tx_buf: Data transmit buffer.
 */
struct st_ism330dhcx_transfer_buffer {
	u8 rx_buf[ST_ISM330DHCX_RX_MAX_LENGTH];
	u8 tx_buf[ST_ISM330DHCX_TX_MAX_LENGTH] ____cacheline_aligned;
};

/**
 * @struct st_ism330dhcx_transfer_function
 * @brief Bus Transfer Function
 *
 * read: Bus read funtion to get register value from sensor.
 * write: Bus write funtion to set register value to sensor.
 */
struct st_ism330dhcx_transfer_function {
	int (*read)(struct device *dev, u8 addr, int len, u8 *data);
	int (*write)(struct device *dev, u8 addr, int len, const u8 *data);
};

/**
 * @struct st_ism330dhcx_reg
 * @brief Generic sensor register description
 *
 * addr: Register arress value.
 * mask: Register bitmask.
 */
struct st_ism330dhcx_reg {
	u8 addr;
	u8 mask;
};

enum st_ism330dhcx_suspend_resume_register {
	ST_ISM330DHCX_CTRL1_XL_REG = 0,
	ST_ISM330DHCX_CTRL2_G_REG,
	ST_ISM330DHCX_REG_CTRL3_C_REG,
	ST_ISM330DHCX_REG_CTRL4_C_REG,
	ST_ISM330DHCX_REG_CTRL5_C_REG,
	ST_ISM330DHCX_REG_CTRL10_C_REG,
	ST_ISM330DHCX_REG_TAP_CFG0_REG,
	ST_ISM330DHCX_REG_INT1_CTRL_REG,
	ST_ISM330DHCX_REG_INT2_CTRL_REG,
	ST_ISM330DHCX_REG_FIFO_CTRL1_REG,
	ST_ISM330DHCX_REG_FIFO_CTRL2_REG,
	ST_ISM330DHCX_REG_FIFO_CTRL3_REG,
	ST_ISM330DHCX_REG_FIFO_CTRL4_REG,
	ST_ISM330DHCX_SUSPEND_RESUME_REGS,
};

struct st_ism330dhcx_suspend_resume_entry {
	u8 addr;
	u8 val;
	u8 mask;
};

/**
 * @struct st_ism330dhcx_odr
 * @brief ODR sensor table entry
 *
 * In the ODR table the possible ODR supported by sensor can be defined in the
 * following format:
 *    .odr_avl[0] = {   0, 0,       0x00 },
 *    .odr_avl[1] = {  12, 500000,  0x01 }, ..... it means 12.5 Hz
 *    .odr_avl[2] = {  26, 0,       0x02 }, ..... it means 26.0 Hz
 *
 * hz: Most significant part of ODR value (in Hz).
 * uhz: Least significant part of ODR value (in micro Hz).
 * val: Register value tu set ODR.
 */
struct st_ism330dhcx_odr {
	int hz;
	int uhz;
	u8 val;
};

/**
 * @struct st_ism330dhcx_odr_table_entry
 * @brief ODR sensor table
 *
 * odr_size: ODR table size.
 * reg: Sensor register description for ODR (address and mask).
 * odr_avl: All supported ODR values.
 */
struct st_ism330dhcx_odr_table_entry {
	u8 odr_size;
	struct st_ism330dhcx_reg reg;
	struct st_ism330dhcx_odr odr_avl[ST_ISM330DHCX_ODR_LIST_SIZE];
};

/**
 * @struct st_ism330dhcx_fs
 * @brief Full scale entry
 *
 * reg: Sensor register description for FS (address and mask).
 * gain: The gain to obtain data value from raw data (LSB).
 * val: Register value.
 */
struct st_ism330dhcx_fs {
	struct st_ism330dhcx_reg reg;
	u32 gain;
	u8 val;
};

/**
 * @struct st_ism330dhcx_fs_table_entry
 * @brief Full scale table
 *
 * size: Full scale number of entry.
 * fs_avl: Full scale entry.
 */
#define ST_ISM330DHCX_FS_LIST_SIZE			5
#define ST_ISM330DHCX_FS_ACC_LIST_SIZE		4
#define ST_ISM330DHCX_FS_GYRO_LIST_SIZE		5
#define ST_ISM330DHCX_FS_TEMP_LIST_SIZE		1
struct st_ism330dhcx_fs_table_entry {
	u8 size;
	struct st_ism330dhcx_fs fs_avl[ST_ISM330DHCX_FS_LIST_SIZE];
};

#define ST_ISM330DHCX_ACC_FS_2G_GAIN	IIO_G_TO_M_S_2(61000)
#define ST_ISM330DHCX_ACC_FS_4G_GAIN	IIO_G_TO_M_S_2(122000)
#define ST_ISM330DHCX_ACC_FS_8G_GAIN	IIO_G_TO_M_S_2(244000)
#define ST_ISM330DHCX_ACC_FS_16G_GAIN	IIO_G_TO_M_S_2(488000)

#define ST_ISM330DHCX_GYRO_FS_250_GAIN	IIO_DEGREE_TO_RAD(8750000)
#define ST_ISM330DHCX_GYRO_FS_500_GAIN	IIO_DEGREE_TO_RAD(17500000)
#define ST_ISM330DHCX_GYRO_FS_1000_GAIN	IIO_DEGREE_TO_RAD(35000000)
#define ST_ISM330DHCX_GYRO_FS_2000_GAIN	IIO_DEGREE_TO_RAD(70000000)
#define ST_ISM330DHCX_GYRO_FS_4000_GAIN	IIO_DEGREE_TO_RAD(140000000)

struct st_ism330dhcx_ext_dev_info {
	const struct st_ism330dhcx_ext_dev_settings *ext_dev_settings;
	u8 ext_dev_i2c_addr;
};

/**
 * @enum st_ism330dhcx_sensor_id
 * @brief Sensor Identifier
 */
enum st_ism330dhcx_sensor_id {
	ST_ISM330DHCX_ID_GYRO,
	ST_ISM330DHCX_ID_ACC,
	ST_ISM330DHCX_ID_TEMP,
	ST_ISM330DHCX_ID_EXT0,
	ST_ISM330DHCX_ID_EXT1,
	ST_ISM330DHCX_ID_STEP_COUNTER,
	ST_ISM330DHCX_ID_STEP_DETECTOR,
	ST_ISM330DHCX_ID_SIGN_MOTION,
	ST_ISM330DHCX_ID_GLANCE,
	ST_ISM330DHCX_ID_MOTION,
	ST_ISM330DHCX_ID_NO_MOTION,
	ST_ISM330DHCX_ID_WAKEUP,
	ST_ISM330DHCX_ID_PICKUP,
	ST_ISM330DHCX_ID_ORIENTATION,
	ST_ISM330DHCX_ID_WRIST_TILT,
	ST_ISM330DHCX_ID_TILT,
	ST_ISM330DHCX_ID_MAX,
};

/**
 * @enum st_ism330dhcx_sensor_id
 * @brief Sensor Table Identifier
 */
static const enum st_ism330dhcx_sensor_id st_ism330dhcx_main_sensor_list[] = {
	 [0] = ST_ISM330DHCX_ID_GYRO,
	 [1] = ST_ISM330DHCX_ID_ACC,
	 [2] = ST_ISM330DHCX_ID_TEMP,
	 [3] = ST_ISM330DHCX_ID_STEP_COUNTER,
	 [4] = ST_ISM330DHCX_ID_STEP_DETECTOR,
	 [5] = ST_ISM330DHCX_ID_SIGN_MOTION,
	 [6] = ST_ISM330DHCX_ID_GLANCE,
	 [7] = ST_ISM330DHCX_ID_MOTION,
	 [8] = ST_ISM330DHCX_ID_NO_MOTION,
	 [9] = ST_ISM330DHCX_ID_WAKEUP,
	[10] = ST_ISM330DHCX_ID_PICKUP,
	[11] = ST_ISM330DHCX_ID_ORIENTATION,
	[12] = ST_ISM330DHCX_ID_WRIST_TILT,
	[13] = ST_ISM330DHCX_ID_TILT,
};

/**
 * @enum st_ism330dhcx_fifo_mode
 * @brief FIFO Modes
 */
enum st_ism330dhcx_fifo_mode {
	ST_ISM330DHCX_FIFO_BYPASS = 0x0,
	ST_ISM330DHCX_FIFO_CONT = 0x6,
};

/**
 * @enum st_ism330dhcx_fifo_mode - FIFO Buffer Status
 */
enum st_ism330dhcx_fifo_status {
	ST_ISM330DHCX_HW_FLUSH,
	ST_ISM330DHCX_HW_OPERATIONAL,
};

/**
 * @struct st_ism330dhcx_sensor
 * @brief ST IMU sensor instance
 *
 * id: Sensor identifier
 * hw: Pointer to instance of struct st_ism330dhcx_hw
 * ext_dev_info: Sensor hub i2c slave settings.
 * trig: Sensor iio trigger.
 * gain: Configured sensor sensitivity
 * odr: Output data rate of the sensor [Hz]
 * uodr: Output data rate of the sensor [uHz]
 * offset: Sensor data offset
 * decimator: Sensor decimator
 * dec_counter: Sensor decimator counter
 * old_data: Saved sensor data
 * max_watermark: Max supported watermark level
 * watermark: Sensor watermark level
 * batch_reg: Sensor reg/mask for FIFO batching register
 * last_fifo_timestamp: Store last sample timestamp in FIFO, used by flush
 * selftest_status: Last status of self test output
 * min_st, max_st: Min/Max acc/gyro data values during self test procedure
 */
struct st_ism330dhcx_sensor {
	enum st_ism330dhcx_sensor_id id;
	struct st_ism330dhcx_hw *hw;

	struct st_ism330dhcx_ext_dev_info ext_dev_info;

	struct iio_trigger *trig;

	u32 gain;
	int odr;
	int uodr;

	u32 offset;
	u8 decimator;
	u8 dec_counter;

	u16 max_watermark;
	u16 watermark;

	struct st_ism330dhcx_reg batch_reg;
	s64 last_fifo_timestamp;

	/* self test */
	int8_t selftest_status;
	int min_st;
	int max_st;
};

/**
 * @struct st_ism330dhcx_hw
 * @brief ST IMU MEMS hw instance
 *
 * dev: Pointer to instance of struct device (I2C or SPI).
 * irq: Device interrupt line (I2C or SPI).
 * lock: Mutex to protect read and write operations.
 * fifo_lock: Mutex to prevent concurrent access to the hw FIFO.
 * page_lock: Mutex to prevent concurrent memory page configuration.
 * fifo_mode: FIFO operating mode supported by the device.
 * state: hw operational state.
 * enable_mask: Enabled sensor bitmask.
 * fsm_enable_mask: FSM Enabled sensor bitmask.
 * embfunc_pg0_irq_reg: Embedded function irq configutation register (page 0).
 * embfunc_irq_reg: Embedded function irq configutation register (other).
 * ext_data_len: Number of i2c slave devices connected to I2C master.
 * odr: Timestamp sample ODR [Hz]
 * uodr: Timestamp sample ODR [uHz]
 * ts_offset: Hw timestamp offset.
 * hw_ts: Latest hw timestamp from the sensor.
 * hw_ts_high: Manage timestamp rollover
 * tsample:
 * hw_ts_old:
 * delta_ts: Delta time between two consecutive interrupts.
 * delta_hw_ts:
 * ts: Latest timestamp from irq handler.
 * @module_id: identify iio devices of the same sensor module.
 * iio_devs: Pointers to acc/gyro iio_dev instances.
 * tf: Transfer function structure used by I/O operations.
 * tb: Transfer buffers used by SPI I/O operations.
 */
struct st_ism330dhcx_hw {
	struct device *dev;
	int irq;

	struct mutex lock;
	struct mutex fifo_lock;
	struct mutex page_lock;

	enum st_ism330dhcx_fifo_mode fifo_mode;
	unsigned long state;
	u32 enable_mask;
	u32 requested_mask;

	u16 fsm_enable_mask;
	u8 embfunc_irq_reg;
	u8 embfunc_pg0_irq_reg;

	u8 ext_data_len;

	int odr;
	int uodr;

	s64 ts_offset;
	u64 ts_delta_ns;
	s64 hw_ts;
	u32 val_ts_old;
	u32 hw_ts_high;
	s64 tsample;
	s64 delta_ts;
	s64 ts;
	u32 module_id;

	struct iio_dev *iio_devs[ST_ISM330DHCX_ID_MAX];

	const struct st_ism330dhcx_transfer_function *tf;
	struct st_ism330dhcx_transfer_buffer tb;
};

/**
 * @struct dev_pm_ops
 * @brief Power mamagement callback function structure
 */
extern const struct dev_pm_ops st_ism330dhcx_pm_ops;

static inline int st_ism330dhcx_read_atomic(struct st_ism330dhcx_hw *hw,
					    u8 addr, int len, u8 *data)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = hw->tf->read(hw->dev, addr, len, data);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int st_ism330dhcx_write_atomic(struct st_ism330dhcx_hw *hw, u8 addr,
					  int len, u8 *data)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = hw->tf->write(hw->dev, addr, len, data);
	mutex_unlock(&hw->page_lock);

	return err;
}

int __st_ism330dhcx_write_with_mask(struct st_ism330dhcx_hw *hw, u8 addr, u8 mask,
				 u8 val);
static inline int st_ism330dhcx_write_with_mask(struct st_ism330dhcx_hw *hw, u8 addr,
					     u8 mask, u8 val)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = __st_ism330dhcx_write_with_mask(hw, addr, mask, val);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_ism330dhcx_update_bits_locked(struct st_ism330dhcx_hw *hw, unsigned int addr,
			     unsigned int mask, unsigned int val)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = __st_ism330dhcx_write_with_mask(hw, addr, mask, val);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int st_ism330dhcx_set_page_access(struct st_ism330dhcx_hw *hw,
					     u8 mask, u8 data)
{
	int err;

	err = __st_ism330dhcx_write_with_mask(hw,
					   ST_ISM330DHCX_REG_FUNC_CFG_ACCESS_ADDR,
					   mask, data);
	usleep_range(100, 150);

	return err;
}

static inline bool st_ism330dhcx_is_fifo_enabled(struct st_ism330dhcx_hw *hw)
{
	return hw->enable_mask & (BIT(ST_ISM330DHCX_ID_STEP_COUNTER) |
				  BIT(ST_ISM330DHCX_ID_GYRO)	  |
				  BIT(ST_ISM330DHCX_ID_ACC)	  |
				  BIT(ST_ISM330DHCX_ID_EXT0)	  |
				  BIT(ST_ISM330DHCX_ID_EXT1));
}

int st_ism330dhcx_probe(struct device *dev, int irq,
		     const struct st_ism330dhcx_transfer_function *tf_ops);
int st_ism330dhcx_shub_set_enable(struct st_ism330dhcx_sensor *sensor, bool enable);
int st_ism330dhcx_shub_probe(struct st_ism330dhcx_hw *hw);
int st_ism330dhcx_sensor_set_enable(struct st_ism330dhcx_sensor *sensor,
				 bool enable);
int st_ism330dhcx_buffers_setup(struct st_ism330dhcx_hw *hw);
int st_ism330dhcx_get_odr_val(enum st_ism330dhcx_sensor_id id, int odr, int uodr,
			   int *podr, int *puodr, u8 *val);
int st_ism330dhcx_update_watermark(struct st_ism330dhcx_sensor *sensor,
				u16 watermark);
ssize_t st_ism330dhcx_flush_fifo(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size);
ssize_t st_ism330dhcx_get_max_watermark(struct device *dev,
				     struct device_attribute *attr,
				     char *buf);
ssize_t st_ism330dhcx_get_watermark(struct device *dev,
				 struct device_attribute *attr,
				 char *buf);
ssize_t st_ism330dhcx_set_watermark(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size);
ssize_t st_ism330dhcx_get_module_id(struct device *dev,
				    struct device_attribute *attr,
				    char *buf);

int st_ism330dhcx_set_page_access(struct st_ism330dhcx_hw *hw, u8 mask, u8 data);
int st_ism330dhcx_suspend_fifo(struct st_ism330dhcx_hw *hw);
int st_ism330dhcx_set_fifo_mode(struct st_ism330dhcx_hw *hw,
			     enum st_ism330dhcx_fifo_mode fifo_mode);
int __st_ism330dhcx_set_sensor_batching_odr(struct st_ism330dhcx_sensor *sensor,
					 bool enable);
int st_ism330dhcx_fsm_init(struct st_ism330dhcx_hw *hw);
int st_ism330dhcx_fsm_get_orientation(struct st_ism330dhcx_hw *hw, u8 *data);
int st_ism330dhcx_embfunc_sensor_set_enable(struct st_ism330dhcx_sensor *sensor,
					 bool enable);
int st_ism330dhcx_step_counter_set_enable(struct st_ism330dhcx_sensor *sensor,
				       bool enable);
int st_ism330dhcx_reset_step_counter(struct iio_dev *iio_dev);
int st_ism330dhcx_update_batching(struct iio_dev *iio_dev, bool enable);
int st_ism330dhcx_reset_hwts(struct st_ism330dhcx_hw *hw);
#endif /* ST_ISM330DHCX_H */
