/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics st_h3lis331dl sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2023 STMicroelectronics Inc.
 */

#ifndef ST_H3LIS331DL_H
#define ST_H3LIS331DL_H

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/iio/iio.h>
#include <linux/delay.h>
#include <linux/regmap.h>

#define ST_H3LIS331DL_DEV_NAME			"h3lis331dl"
#define ST_LIS331DLH_DEV_NAME			"lis331dlh"

#define ST_H3LIS331DL_REG_WHO_AM_I_ADDR		0x0f
#define ST_H3LIS331DL_WHO_AM_I_VAL		0x32

#define ST_H3LIS331DL_CTRL_REG1_ADDR		0x20
#define ST_H3LIS331DL_EN_MASK			GENMASK(2, 0)
#define ST_H3LIS331DL_DR_MASK			GENMASK(4, 3)
#define ST_H3LIS331DL_PM_MASK			GENMASK(7, 5)

#define ST_H3LIS331DL_CTRL_REG2_ADDR		0x21
#define ST_H3LIS331DL_BOOT_MASK			BIT(7)

#define ST_H3LIS331DL_CTRL_REG3_ADDR		0x22
#define ST_H3LIS331DL_I1CFG_MASK		GENMASK(1, 0)
#define ST_H3LIS331DL_LIR1_MASK			BIT(2)
#define ST_H3LIS331DL_I2CFG_MASK		GENMASK(4, 3)
#define ST_H3LIS331DL_LIR2_MASK			BIT(5)
#define ST_H3LIS331DL_PP_OD_MASK		BIT(6)
#define ST_H3LIS331DL_IHL_MASK			BIT(7)
#define ST_H3LIS331DL_CFG_DRDY_VAL		0x02

#define ST_H3LIS331DL_CTRL_REG4_ADDR		0x23
#define ST_H3LIS331DL_FS_MASK			GENMASK(5, 4)
#define ST_H3LIS331DL_BLE_MASK			BIT(6)
#define ST_H3LIS331DL_BDU_MASK			BIT(7)

#define ST_H3LIS331DL_STATUS_REG_ADDR		0x27
#define ST_H3LIS331DL_ZYXDA_MASK		BIT(3)

#define ST_H3LIS331DL_REG_OUTX_L_ADDR		0x28
#define ST_H3LIS331DL_REG_OUTY_L_ADDR		0x2a
#define ST_H3LIS331DL_REG_OUTZ_L_ADDR		0x2c

#define ST_H3LIS331DL_SAMPLE_SIZE		6

/* enable reading address with auto increment */
#define ST_H3LIS331DL_AUTO_INCREMENT(_addr)	(0x80 | _addr)

#define ST_H3LIS331DL_DATA_CHANNEL(chan_type, addr, mod, ch2, scan_idx,	\
				rb, sb, sg, ex_info)			\
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
	.ext_info = ex_info,						\
}

#define ST_H3LIS331DL_SHIFT_VAL(val, mask)	(((val) << __ffs(mask)) & (mask))
#define ST_H3LIS331DL_ACC_SENSOR_ID	0
#define ST_H3LIS331DL_ODR_LIST_SIZE	8
#define ST_H3LIS331DL_FS_LIST_SIZE	3

/**
 * struct st_h3lis331dl_reg - Generic sensor register description (addr + mask)
 * @addr: Address of register.
 * @mask: Bitmask register for proper usage.
 */
struct st_h3lis331dl_reg {
	u8 addr;
	u8 mask;
};

/**
 * struct st_h3lis331dl_odr - Single ODR entry
 * @hz: Most significant part of the sensor ODR (Hz).
 * @val: ODR register value.
 */
struct st_h3lis331dl_odr {
	int hz;
	u8 val;
};

/**
 * struct st_h3lis331dl_odr_table_entry - Sensor ODR table
 * @size: Size of ODR table.
 * @reg: ODR register.
 * @odr_avl: Array of supported ODR value.
 */
struct st_h3lis331dl_odr_table_entry {
	u8 size;
	struct st_h3lis331dl_reg reg;
	struct st_h3lis331dl_odr odr_avl[ST_H3LIS331DL_ODR_LIST_SIZE];
};

/**
 * struct st_h3lis331dl_fs - Full Scale sensor table entry
 * @gain: Sensor sensitivity.
 * @val: FS register value.
 */
struct st_h3lis331dl_fs {
	u32 gain;
	u8 val;
};

/**
 * struct st_h3lis331dl_fs_table_entry - Full Scale sensor table
 * @size: Full Scale sensor table size.
 * @reg: Register description for FS settings.
 * @fs_avl: Full Scale list entries.
 */
struct st_h3lis331dl_fs_table_entry {
	u8 size;
	struct st_h3lis331dl_reg reg;
	struct st_h3lis331dl_fs fs_avl[ST_H3LIS331DL_FS_LIST_SIZE];
};

#define ST_H3LIS331DL_ACC_FS_100G_GAIN	IIO_G_TO_M_S_2(784000)
#define ST_H3LIS331DL_ACC_FS_200G_GAIN	IIO_G_TO_M_S_2(1568000)
#define ST_H3LIS331DL_ACC_FS_400G_GAIN	IIO_G_TO_M_S_2(3136000)

/**
 * struct st_h3lis331dl_sensor - ST IMU sensor instance
 * @is: Sensor id.
 * @name: Sensor name.
 * @hw: Pointer to instance of struct st_h3lis331dl_hw.
 * @trig: Trigger used by IIO event sensors.
 * @odr: Sensor odr.
 * @gain: Configured sensor sensitivity.
 * @offset: Sensor data offset.
 * @decimator: Sensor decimator
 * @dec_counter: Sensor decimator counter
 */
struct st_h3lis331dl_sensor {
	int id;
	char name[32];
	struct st_h3lis331dl_hw *hw;
	struct iio_trigger *trig;

	int odr;
	u32 gain;
	u32 offset;
	u8 decimator;
	u8 dec_counter;
};

/**
 * struct st_h3lis331dl_hw - ST IMU MEMS hw instance
 * @dev: Pointer to instance of struct device (I2C or SPI).
 * @irq: Device interrupt line (I2C or SPI).
 * @regmap: Register map of the device.
 * @int_pin: Save interrupt pin used by sensor.
 * @lock: Mutex to protect read and write operations.
 * @enable_mask: Enabled sensor bitmask.
 * @ts: hw timestamp value always monotonic where the most
 *      significant 8byte are incremented at every disable/enable.
 * @iio_devs: Pointers to acc/gyro iio_dev instances.
 * @vdd_supply: Voltage regulator for VDD.
 * @vddio_supply: Voltage regulator for VDDIIO.
 * @orientation: Sensor chip orientation relative to main hardware.
 * @self_test: Self test start  and read result.
 * @selftest_available: Reports self test available for this sensor.
 */
struct st_h3lis331dl_hw {
	struct device *dev;
	int irq;
	struct regmap *regmap;
	int int_pin;

	struct mutex lock;

	u64 enable_mask;
	u64 requested_mask;
	s64 ts;

	struct iio_dev *iio_devs;

	struct regulator *vdd_supply;
	struct regulator *vddio_supply;

	struct iio_mount_matrix orientation;
};

extern const struct dev_pm_ops st_h3lis331dl_pm_ops;

static inline int __st_h3lis331dl_write_with_mask(struct st_h3lis331dl_hw *hw,
						  unsigned int addr,
						  unsigned int mask,
						  unsigned int data)
{
	int err;
	unsigned int val = ST_H3LIS331DL_SHIFT_VAL(data, mask);

	err = regmap_update_bits(hw->regmap, addr, mask, val);

	return err;
}

static inline int
st_h3lis331dl_update_bits_locked(struct st_h3lis331dl_hw *hw, unsigned int addr,
				 unsigned int mask, unsigned int val)
{
	int err;

	mutex_lock(&hw->lock);
	err = __st_h3lis331dl_write_with_mask(hw, addr, mask, val);
	mutex_unlock(&hw->lock);

	return err;
}

/* use when mask is constant */
static inline int
st_h3lis331dl_write_with_mask_locked(struct st_h3lis331dl_hw *hw,
				     unsigned int addr,
				     unsigned int mask,
				     unsigned int data)
{
	int err;
	unsigned int val = ST_H3LIS331DL_SHIFT_VAL(mask, data);

	mutex_lock(&hw->lock);
	err = regmap_update_bits(hw->regmap, addr, mask, val);
	mutex_unlock(&hw->lock);

	return err;
}

static inline s64 st_h3lis331dl_get_time_ns(struct iio_dev *iio_dev)
{
	return iio_get_time_ns(iio_dev);
}

int st_h3lis331dl_probe(struct device *dev, int irq, int hw_id,
			struct regmap *regmap);
int st_h3lis331dl_sensor_set_enable(struct st_h3lis331dl_sensor *sensor,
				    bool enable);
int st_h3lis331dl_trigger_setup(struct st_h3lis331dl_hw *hw);
int st_h3lis331dl_allocate_triggered_buffer(struct st_h3lis331dl_hw *hw);

#endif /* ST_H3LIS331DL_H */
