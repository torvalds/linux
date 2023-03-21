/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics st_ism330is sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2023 STMicroelectronics Inc.
 */

#ifndef ST_ISM330IS_H
#define ST_ISM330IS_H

#include <linux/device.h>
#include <linux/iio/iio.h>
#include <linux/delay.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/bitfield.h>

#define ST_ISM330IS_DEV_NAME			"ism330is"

#define ST_ISM330IS_REG_FUNC_CFG_ACCESS_ADDR	0x01
#define ST_ISM330IS_SHUB_REG_MASK		BIT(6)

#define ST_ISM330IS_REG_PIN_CTRL_ADDR		0x02
#define ST_ISM330IS_SDO_PU_EN_MASK		BIT(6)

#define ST_ISM330IS_REG_WHOAMI_ADDR		0x0f
#define ST_ISM330IS_WHOAMI_VAL			0x22

#define ST_ISM330IS_REG_CTRL1_XL_ADDR		0x10
#define ST_ISM330IS_ODR_XL_MASK			GENMASK(7, 4)
#define ST_ISM330IS_FS_XL_MASK			GENMASK(3, 2)

#define ST_ISM330IS_REG_CTRL2_G_ADDR		0x11
#define ST_ISM330IS_ODR_G_MASK			GENMASK(7, 4)
#define ST_ISM330IS_FS_G_MASK			GENMASK(3, 1)

#define ST_ISM330IS_REG_CTRL3_C_ADDR		0x12
#define ST_ISM330IS_BOOT_MASK			BIT(7)
#define ST_ISM330IS_BDU_MASK			BIT(6)
#define ST_ISM330IS_H_LACTIVE_MASK		BIT(5)
#define ST_ISM330IS_PP_OD_MASK			BIT(4)
#define ST_ISM330IS_SW_RESET_MASK		BIT(0)

#define ST_ISM330IS_REG_CTRL5_C_ADDR		0x14
#define ST_ISM330IS_ST_G_MASK			GENMASK(3, 2)
#define ST_ISM330IS_ST_XL_MASK			GENMASK(1, 0)

#define ST_ISM330IS_REG_STATUS_ADDR		0x1e
#define ST_ISM330IS_STATUS_TDA			BIT(2)
#define ST_ISM330IS_STATUS_XLDA			BIT(0)
#define ST_ISM330IS_STATUS_GDA			BIT(1)

#define ST_ISM330IS_REG_OUT_TEMP_L_ADDR		0x20
#define ST_ISM330IS_REG_OUTX_L_G_ADDR		0x22
#define ST_ISM330IS_REG_OUTY_L_G_ADDR		0x24
#define ST_ISM330IS_REG_OUTZ_L_G_ADDR		0x26
#define ST_ISM330IS_REG_OUTX_L_A_ADDR		0x28
#define ST_ISM330IS_REG_OUTY_L_A_ADDR		0x2a
#define ST_ISM330IS_REG_OUTZ_L_A_ADDR		0x2c

#define ST_ISM330IS_ST_ACCEL_MIN		737
#define ST_ISM330IS_ST_ACCEL_MAX		13934
#define ST_ISM330IS_ST_GYRO_MIN			2142
#define ST_ISM330IS_ST_GYRO_MAX			10000

#define ST_ISM330IS_ST_DISABLED_VAL		0
#define ST_ISM330IS_ST_POS_SIGN_VAL		1
#define ST_ISM330IS_ST_NEG_ACCEL_SIGN_VAL	2
#define ST_ISM330IS_ST_NEG_GYRO_SIGN_VAL	3

/* shub registers */
#define ST_ISM330IS_REG_SENSOR_HUB_1_ADDR	0x02

#define ST_ISM330IS_REG_MASTER_CONFIG_ADDR	0x14
#define ST_ISM330IS_WRITE_ONCE_MASK		BIT(6)
#define ST_ISM330IS_SHUB_PU_EN_MASK		BIT(3)
#define ST_ISM330IS_MASTER_ON_MASK		BIT(2)
#define ST_ISM330IS_AUX_SENS_ON_MASK		GENMASK(1, 0)

#define ST_ISM330IS_REG_SLV0_ADDR		0x15
#define ST_ISM330IS_REG_SLV0_CFG		0x17
#define ST_ISM330IS_REG_SLV1_ADDR		0x18
#define ST_ISM330IS_REG_SLV2_ADDR		0x1b
#define ST_ISM330IS_REG_SLV3_ADDR		0x1e

#define ST_ISM330IS_REG_DATAWRITE_SLV0_ADDR	0x21
#define ST_ISM330IS_SLAVE_NUMOP_MASK		GENMASK(2, 0)

#define ST_ISM330IS_REG_STATUS_MASTER_ADDR	0x22
#define ST_ISM330IS_SENS_HUB_ENDOP_MASK	BIT(0)

/* Timestamp Tick 25us/LSB */
#define ST_ISM330IS_TS_DELTA_NS			25000ULL

/* Temperature in uC */
#define ST_ISM330IS_TEMP_GAIN			256
#define ST_ISM330IS_TEMP_OFFSET			6400

#define ST_ISM330IS_DATA_CHANNEL(chan_type, addr, mod, ch2, scan_idx,    \
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

#define ST_ISM330IS_SHIFT_VAL(val, mask)	(((val) << __ffs(mask)) & (mask))

extern const struct dev_pm_ops st_ism330is_pm_ops;

/**
 * struct st_ism330is_reg - Generic sensor register
 * description (addr + mask)
 *
 * @addr: Address of register.
 * @mask: Bitmask register for proper usage.
 */
struct st_ism330is_reg {
	u8 addr;
	u8 mask;
};

/**
 * struct st_ism330is_odr - Single ODR entry
 * @mhz: Sensor ODR (milli Hz).
 * @val: ODR register value.
 */
struct st_ism330is_odr {
	u32 mhz;
	u8 val;
};

/**
 * struct st_ism330is_odr_table_entry - Sensor ODR table
 * @size: Size of ODR table.
 * @reg: ODR register.
 * @odr_avl: Array of supported ODR value.
 */
struct st_ism330is_odr_table_entry {
	u8 size;
	struct st_ism330is_reg reg;
	struct st_ism330is_odr odr_avl[8];
};

/**
 * struct st_ism330is_fs
 * brief Full scale entry
 *
 * @gain: The gain to obtain data value from raw data (LSB).
 * @val: Register value.
 */
struct st_ism330is_fs {
	u32 gain;
	u8 val;
};

/**
 * struct st_ism330is_fs_table_entry - Full Scale sensor table
 * @reg: st_ism330is_reg struct.
 * @fs_avl: Full Scale list entries.
 * @fs_len: Real size of fs_avl array.
 */
struct st_ism330is_fs_table_entry {
	int fs_len;
	struct st_ism330is_reg reg;
	struct st_ism330is_fs fs_avl[4];
};

enum st_ism330is_sensor_id {
	ST_ISM330IS_ID_GYRO = 0,
	ST_ISM330IS_ID_ACC,
	ST_ISM330IS_ID_TEMP,
	ST_ISM330IS_ID_EXT0,
	ST_ISM330IS_ID_EXT1,
	ST_ISM330IS_ID_MAX,
};

/**
 * @enum st_ism330is_sensor_id
 * @brief Sensor Table Identifier
 */
static const enum st_ism330is_sensor_id st_ism330is_main_sensor_list[] = {
	[0] = ST_ISM330IS_ID_GYRO,
	[1] = ST_ISM330IS_ID_ACC,
	[2] = ST_ISM330IS_ID_TEMP,
	[3] = ST_ISM330IS_ID_EXT0,
	[4] = ST_ISM330IS_ID_EXT1,
};

static const enum st_ism330is_sensor_id
st_ism330is_triggered_main_sensor_list[] = {
	[0] = ST_ISM330IS_ID_GYRO,
	[1] = ST_ISM330IS_ID_ACC,
	[2] = ST_ISM330IS_ID_TEMP,
	[3] = ST_ISM330IS_ID_EXT0,
	[4] = ST_ISM330IS_ID_EXT1,
};

struct st_ism330is_ext_dev_info {
	const struct st_ism330is_ext_dev_settings *ext_dev_settings;
	u8 ext_dev_i2c_addr;
};

/**
 * struct st_ism330is_sensor - ST IMU sensor instance
 * @ext_dev_info: For sensor hub indicate device info struct.
 * @id: Sensor identifier.
 * @hw: Pointer to instance of struct st_ism330is_hw.
 * @name: Sensor name.
 * @offset: Sensor data offset.
 * @gain: Configured sensor sensitivity.
 * @mhz: Output data rate of the sensor [milli Hz].
 * @selftest_status: Report last self test status.
 * @min_st: Min self test raw data value.
 * @max_st: Max self test raw data value.
 */
struct st_ism330is_sensor {
	struct st_ism330is_ext_dev_info ext_dev_info;
	enum st_ism330is_sensor_id id;
	struct st_ism330is_hw *hw;
	char name[32];

	u32 offset;
	u32 gain;
	u32 mhz;

	/* self test */
	int8_t selftest_status;
	int min_st;
	int max_st;
};

/**
 * struct st_ism330is_hw - ST IMU MEMS hw instance
 * @iio_devs: Pointers to acc/gyro iio_dev instances.
 * @orientation: Sensor orientation matrix.
 * @vddio_supply: Voltage regulator for VDDIIO.
 * @vdd_supply: Voltage regulator for VDD.
 * @page_lock: Mutex to prevent concurrent access to the page selector.
 * @regmap: Register map of the device.
 * @dev: Pointer to instance of struct device (I2C or SPI).
 * @i2c_master_pu: I2C master line Pull Up configuration.
 * @enable_mask: Enabled sensor bitmask.
 * @ext_data_len: Number of i2c slave devices connected to I2C master.
 * @irq: Device interrupt line (I2C or SPI).
 */
struct st_ism330is_hw {
	struct iio_dev *iio_devs[ST_ISM330IS_ID_MAX];
	struct iio_mount_matrix orientation;
	struct regulator *vddio_supply;
	struct regulator *vdd_supply;
	struct mutex page_lock;
	struct regmap *regmap;
	struct device *dev;
	u8 i2c_master_pu;
	u32 enable_mask;
	u8 ext_data_len;
	int irq;
};

static inline int
__st_ism330is_write_with_mask(struct st_ism330is_hw *hw,
			      unsigned int addr,
			      unsigned int mask,
			      unsigned int val)
{
	return regmap_update_bits(hw->regmap, addr, mask,
				  ST_ISM330IS_SHIFT_VAL(val, mask));
}

static inline int
st_ism330is_update_bits_locked(struct st_ism330is_hw *hw,
			       unsigned int addr,
			       unsigned int mask,
			       unsigned int val)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = __st_ism330is_write_with_mask(hw, addr, mask, val);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_ism330is_read_locked(struct st_ism330is_hw *hw, unsigned int addr,
			void *val, unsigned int len)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = regmap_bulk_read(hw->regmap, addr, val, len);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_ism330is_write_locked(struct st_ism330is_hw *hw, unsigned int addr,
			 unsigned int val)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = regmap_write(hw->regmap, addr, val);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_ism330is_set_page_access(struct st_ism330is_hw *hw, unsigned int mask,
			    unsigned int val)
{
	return __st_ism330is_write_with_mask(hw,
					   ST_ISM330IS_REG_FUNC_CFG_ACCESS_ADDR,
					   mask, val);
}

int st_ism330is_probe(struct device *dev, int irq, struct regmap *regmap);
int st_ism330is_sensor_set_enable(struct st_ism330is_sensor *sensor,
				  bool enable);
int st_ism330is_shub_probe(struct st_ism330is_hw *hw);
int st_ism330is_shub_set_enable(struct st_ism330is_sensor *sensor,
				bool enable);
int st_ism330is_shub_read(struct st_ism330is_sensor *sensor,
			  u8 addr, u8 *data, int len);
int st_ism330is_allocate_buffers(struct st_ism330is_hw *hw);
#endif /* ST_ISM330IS_H */
