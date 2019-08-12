/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics st_lsm6dsx sensor driver
 *
 * Copyright 2016 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi@st.com>
 * Denis Ciocca <denis.ciocca@st.com>
 */

#ifndef ST_LSM6DSX_H
#define ST_LSM6DSX_H

#include <linux/device.h>

#define ST_LSM6DS3_DEV_NAME	"lsm6ds3"
#define ST_LSM6DS3H_DEV_NAME	"lsm6ds3h"
#define ST_LSM6DSL_DEV_NAME	"lsm6dsl"
#define ST_LSM6DSM_DEV_NAME	"lsm6dsm"
#define ST_ISM330DLC_DEV_NAME	"ism330dlc"
#define ST_LSM6DSO_DEV_NAME	"lsm6dso"
#define ST_ASM330LHH_DEV_NAME	"asm330lhh"
#define ST_LSM6DSOX_DEV_NAME	"lsm6dsox"
#define ST_LSM6DSR_DEV_NAME	"lsm6dsr"

enum st_lsm6dsx_hw_id {
	ST_LSM6DS3_ID,
	ST_LSM6DS3H_ID,
	ST_LSM6DSL_ID,
	ST_LSM6DSM_ID,
	ST_ISM330DLC_ID,
	ST_LSM6DSO_ID,
	ST_ASM330LHH_ID,
	ST_LSM6DSOX_ID,
	ST_LSM6DSR_ID,
	ST_LSM6DSX_MAX_ID,
};

#define ST_LSM6DSX_BUFF_SIZE		512
#define ST_LSM6DSX_CHAN_SIZE		2
#define ST_LSM6DSX_SAMPLE_SIZE		6
#define ST_LSM6DSX_TAG_SIZE		1
#define ST_LSM6DSX_TAGGED_SAMPLE_SIZE	(ST_LSM6DSX_SAMPLE_SIZE + \
					 ST_LSM6DSX_TAG_SIZE)
#define ST_LSM6DSX_MAX_WORD_LEN		((32 / ST_LSM6DSX_SAMPLE_SIZE) * \
					 ST_LSM6DSX_SAMPLE_SIZE)
#define ST_LSM6DSX_MAX_TAGGED_WORD_LEN	((32 / ST_LSM6DSX_TAGGED_SAMPLE_SIZE) \
					 * ST_LSM6DSX_TAGGED_SAMPLE_SIZE)
#define ST_LSM6DSX_SHIFT_VAL(val, mask)	(((val) << __ffs(mask)) & (mask))

#define ST_LSM6DSX_CHANNEL(chan_type, addr, mod, scan_idx)		\
{									\
	.type = chan_type,						\
	.address = addr,						\
	.modified = 1,							\
	.channel2 = mod,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |			\
			      BIT(IIO_CHAN_INFO_SCALE),			\
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.scan_index = scan_idx,						\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 16,						\
		.storagebits = 16,					\
		.endianness = IIO_LE,					\
	},								\
}

struct st_lsm6dsx_reg {
	u8 addr;
	u8 mask;
};

struct st_lsm6dsx_hw;

struct st_lsm6dsx_odr {
	u16 hz;
	u8 val;
};

#define ST_LSM6DSX_ODR_LIST_SIZE	6
struct st_lsm6dsx_odr_table_entry {
	struct st_lsm6dsx_reg reg;
	struct st_lsm6dsx_odr odr_avl[ST_LSM6DSX_ODR_LIST_SIZE];
};

struct st_lsm6dsx_fs {
	u32 gain;
	u8 val;
};

#define ST_LSM6DSX_FS_LIST_SIZE		4
struct st_lsm6dsx_fs_table_entry {
	struct st_lsm6dsx_reg reg;
	struct st_lsm6dsx_fs fs_avl[ST_LSM6DSX_FS_LIST_SIZE];
};

/**
 * struct st_lsm6dsx_fifo_ops - ST IMU FIFO settings
 * @read_fifo: Read FIFO callback.
 * @fifo_th: FIFO threshold register info (addr + mask).
 * @fifo_diff: FIFO diff status register info (addr + mask).
 * @th_wl: FIFO threshold word length.
 */
struct st_lsm6dsx_fifo_ops {
	int (*read_fifo)(struct st_lsm6dsx_hw *hw);
	struct {
		u8 addr;
		u16 mask;
	} fifo_th;
	struct {
		u8 addr;
		u16 mask;
	} fifo_diff;
	u8 th_wl;
};

/**
 * struct st_lsm6dsx_hw_ts_settings - ST IMU hw timer settings
 * @timer_en: Hw timer enable register info (addr + mask).
 * @hr_timer: Hw timer resolution register info (addr + mask).
 * @fifo_en: Hw timer FIFO enable register info (addr + mask).
 * @decimator: Hw timer FIFO decimator register info (addr + mask).
 */
struct st_lsm6dsx_hw_ts_settings {
	struct st_lsm6dsx_reg timer_en;
	struct st_lsm6dsx_reg hr_timer;
	struct st_lsm6dsx_reg fifo_en;
	struct st_lsm6dsx_reg decimator;
};

/**
 * struct st_lsm6dsx_shub_settings - ST IMU hw i2c controller settings
 * @page_mux: register page mux info (addr + mask).
 * @master_en: master config register info (addr + mask).
 * @pullup_en: i2c controller pull-up register info (addr + mask).
 * @aux_sens: aux sensor register info (addr + mask).
 * @wr_once: write_once register info (addr + mask).
 * @shub_out: sensor hub first output register info.
 * @slv0_addr: slave0 address in secondary page.
 * @dw_slv0_addr: slave0 write register address in secondary page.
 * @batch_en: Enable/disable FIFO batching.
 */
struct st_lsm6dsx_shub_settings {
	struct st_lsm6dsx_reg page_mux;
	struct st_lsm6dsx_reg master_en;
	struct st_lsm6dsx_reg pullup_en;
	struct st_lsm6dsx_reg aux_sens;
	struct st_lsm6dsx_reg wr_once;
	u8 shub_out;
	u8 slv0_addr;
	u8 dw_slv0_addr;
	u8 batch_en;
};

enum st_lsm6dsx_ext_sensor_id {
	ST_LSM6DSX_ID_MAGN,
};

/**
 * struct st_lsm6dsx_ext_dev_settings - i2c controller slave settings
 * @i2c_addr: I2c slave address list.
 * @wai: Wai address info.
 * @id: external sensor id.
 * @odr: Output data rate of the sensor [Hz].
 * @gain: Configured sensor sensitivity.
 * @temp_comp: Temperature compensation register info (addr + mask).
 * @pwr_table: Power on register info (addr + mask).
 * @off_canc: Offset cancellation register info (addr + mask).
 * @bdu: Block data update register info (addr + mask).
 * @out: Output register info.
 */
struct st_lsm6dsx_ext_dev_settings {
	u8 i2c_addr[2];
	struct {
		u8 addr;
		u8 val;
	} wai;
	enum st_lsm6dsx_ext_sensor_id id;
	struct st_lsm6dsx_odr_table_entry odr_table;
	struct st_lsm6dsx_fs_table_entry fs_table;
	struct st_lsm6dsx_reg temp_comp;
	struct {
		struct st_lsm6dsx_reg reg;
		u8 off_val;
		u8 on_val;
	} pwr_table;
	struct st_lsm6dsx_reg off_canc;
	struct st_lsm6dsx_reg bdu;
	struct {
		u8 addr;
		u8 len;
	} out;
};

/**
 * struct st_lsm6dsx_settings - ST IMU sensor settings
 * @wai: Sensor WhoAmI default value.
 * @max_fifo_size: Sensor max fifo length in FIFO words.
 * @id: List of hw id/device name supported by the driver configuration.
 * @decimator: List of decimator register info (addr + mask).
 * @batch: List of FIFO batching register info (addr + mask).
 * @fifo_ops: Sensor hw FIFO parameters.
 * @ts_settings: Hw timer related settings.
 * @shub_settings: i2c controller related settings.
 */
struct st_lsm6dsx_settings {
	u8 wai;
	u16 max_fifo_size;
	struct {
		enum st_lsm6dsx_hw_id hw_id;
		const char *name;
	} id[ST_LSM6DSX_MAX_ID];
	struct st_lsm6dsx_reg decimator[ST_LSM6DSX_MAX_ID];
	struct st_lsm6dsx_reg batch[ST_LSM6DSX_MAX_ID];
	struct st_lsm6dsx_fifo_ops fifo_ops;
	struct st_lsm6dsx_hw_ts_settings ts_settings;
	struct st_lsm6dsx_shub_settings shub_settings;
};

enum st_lsm6dsx_sensor_id {
	ST_LSM6DSX_ID_GYRO,
	ST_LSM6DSX_ID_ACC,
	ST_LSM6DSX_ID_EXT0,
	ST_LSM6DSX_ID_EXT1,
	ST_LSM6DSX_ID_EXT2,
	ST_LSM6DSX_ID_MAX,
};

enum st_lsm6dsx_fifo_mode {
	ST_LSM6DSX_FIFO_BYPASS = 0x0,
	ST_LSM6DSX_FIFO_CONT = 0x6,
};

/**
 * struct st_lsm6dsx_sensor - ST IMU sensor instance
 * @name: Sensor name.
 * @id: Sensor identifier.
 * @hw: Pointer to instance of struct st_lsm6dsx_hw.
 * @gain: Configured sensor sensitivity.
 * @odr: Output data rate of the sensor [Hz].
 * @watermark: Sensor watermark level.
 * @sip: Number of samples in a given pattern.
 * @decimator: FIFO decimation factor.
 * @ts_ref: Sensor timestamp reference for hw one.
 * @ext_info: Sensor settings if it is connected to i2c controller
 */
struct st_lsm6dsx_sensor {
	char name[32];
	enum st_lsm6dsx_sensor_id id;
	struct st_lsm6dsx_hw *hw;

	u32 gain;
	u16 odr;

	u16 watermark;
	u8 sip;
	u8 decimator;
	s64 ts_ref;

	struct {
		const struct st_lsm6dsx_ext_dev_settings *settings;
		u8 addr;
	} ext_info;
};

/**
 * struct st_lsm6dsx_hw - ST IMU MEMS hw instance
 * @dev: Pointer to instance of struct device (I2C or SPI).
 * @regmap: Register map of the device.
 * @irq: Device interrupt line (I2C or SPI).
 * @fifo_lock: Mutex to prevent concurrent access to the hw FIFO.
 * @conf_lock: Mutex to prevent concurrent FIFO configuration update.
 * @page_lock: Mutex to prevent concurrent memory page configuration.
 * @fifo_mode: FIFO operating mode supported by the device.
 * @suspend_mask: Suspended sensor bitmask.
 * @enable_mask: Enabled sensor bitmask.
 * @ts_sip: Total number of timestamp samples in a given pattern.
 * @sip: Total number of samples (acc/gyro/ts) in a given pattern.
 * @buff: Device read buffer.
 * @iio_devs: Pointers to acc/gyro iio_dev instances.
 * @settings: Pointer to the specific sensor settings in use.
 */
struct st_lsm6dsx_hw {
	struct device *dev;
	struct regmap *regmap;
	int irq;

	struct mutex fifo_lock;
	struct mutex conf_lock;
	struct mutex page_lock;

	enum st_lsm6dsx_fifo_mode fifo_mode;
	u8 suspend_mask;
	u8 enable_mask;
	u8 ts_sip;
	u8 sip;

	u8 *buff;

	struct iio_dev *iio_devs[ST_LSM6DSX_ID_MAX];

	const struct st_lsm6dsx_settings *settings;
};

static const unsigned long st_lsm6dsx_available_scan_masks[] = {0x7, 0x0};
extern const struct dev_pm_ops st_lsm6dsx_pm_ops;

int st_lsm6dsx_probe(struct device *dev, int irq, int hw_id,
		     struct regmap *regmap);
int st_lsm6dsx_sensor_set_enable(struct st_lsm6dsx_sensor *sensor,
				 bool enable);
int st_lsm6dsx_fifo_setup(struct st_lsm6dsx_hw *hw);
int st_lsm6dsx_set_watermark(struct iio_dev *iio_dev, unsigned int val);
int st_lsm6dsx_update_watermark(struct st_lsm6dsx_sensor *sensor,
				u16 watermark);
int st_lsm6dsx_flush_fifo(struct st_lsm6dsx_hw *hw);
int st_lsm6dsx_set_fifo_mode(struct st_lsm6dsx_hw *hw,
			     enum st_lsm6dsx_fifo_mode fifo_mode);
int st_lsm6dsx_read_fifo(struct st_lsm6dsx_hw *hw);
int st_lsm6dsx_read_tagged_fifo(struct st_lsm6dsx_hw *hw);
int st_lsm6dsx_check_odr(struct st_lsm6dsx_sensor *sensor, u16 odr, u8 *val);
int st_lsm6dsx_shub_probe(struct st_lsm6dsx_hw *hw, const char *name);
int st_lsm6dsx_shub_set_enable(struct st_lsm6dsx_sensor *sensor, bool enable);
int st_lsm6dsx_set_page(struct st_lsm6dsx_hw *hw, bool enable);

static inline int
st_lsm6dsx_update_bits_locked(struct st_lsm6dsx_hw *hw, unsigned int addr,
			      unsigned int mask, unsigned int val)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = regmap_update_bits(hw->regmap, addr, mask, val);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_lsm6dsx_read_locked(struct st_lsm6dsx_hw *hw, unsigned int addr,
		       void *val, unsigned int len)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = regmap_bulk_read(hw->regmap, addr, val, len);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_lsm6dsx_write_locked(struct st_lsm6dsx_hw *hw, unsigned int addr,
			unsigned int val)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = regmap_write(hw->regmap, addr, val);
	mutex_unlock(&hw->page_lock);

	return err;
}

#endif /* ST_LSM6DSX_H */
