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
#include <linux/iio/iio.h>
#include <linux/regulator/consumer.h>

#define ST_LSM6DS3_DEV_NAME	"lsm6ds3"
#define ST_LSM6DS3H_DEV_NAME	"lsm6ds3h"
#define ST_LSM6DSL_DEV_NAME	"lsm6dsl"
#define ST_LSM6DSM_DEV_NAME	"lsm6dsm"
#define ST_ISM330DLC_DEV_NAME	"ism330dlc"
#define ST_LSM6DSO_DEV_NAME	"lsm6dso"
#define ST_ASM330LHH_DEV_NAME	"asm330lhh"
#define ST_LSM6DSOX_DEV_NAME	"lsm6dsox"
#define ST_LSM6DSR_DEV_NAME	"lsm6dsr"
#define ST_LSM6DS3TRC_DEV_NAME	"lsm6ds3tr-c"
#define ST_ISM330DHCX_DEV_NAME	"ism330dhcx"
#define ST_LSM9DS1_DEV_NAME	"lsm9ds1-imu"
#define ST_LSM6DS0_DEV_NAME	"lsm6ds0"
#define ST_LSM6DSRX_DEV_NAME	"lsm6dsrx"
#define ST_LSM6DST_DEV_NAME	"lsm6dst"
#define ST_LSM6DSOP_DEV_NAME	"lsm6dsop"
#define ST_ASM330LHHX_DEV_NAME	"asm330lhhx"
#define ST_LSM6DSTX_DEV_NAME	"lsm6dstx"
#define ST_LSM6DSV_DEV_NAME	"lsm6dsv"
#define ST_LSM6DSV16X_DEV_NAME	"lsm6dsv16x"
#define ST_LSM6DSO16IS_DEV_NAME	"lsm6dso16is"
#define ST_ISM330IS_DEV_NAME	"ism330is"
#define ST_ASM330LHB_DEV_NAME	"asm330lhb"
#define ST_ASM330LHHXG1_DEV_NAME	"asm330lhhxg1"

enum st_lsm6dsx_hw_id {
	ST_LSM6DS3_ID = 1,
	ST_LSM6DS3H_ID,
	ST_LSM6DSL_ID,
	ST_LSM6DSM_ID,
	ST_ISM330DLC_ID,
	ST_LSM6DSO_ID,
	ST_ASM330LHH_ID,
	ST_LSM6DSOX_ID,
	ST_LSM6DSR_ID,
	ST_LSM6DS3TRC_ID,
	ST_ISM330DHCX_ID,
	ST_LSM9DS1_ID,
	ST_LSM6DS0_ID,
	ST_LSM6DSRX_ID,
	ST_LSM6DST_ID,
	ST_LSM6DSOP_ID,
	ST_ASM330LHHX_ID,
	ST_LSM6DSTX_ID,
	ST_LSM6DSV_ID,
	ST_LSM6DSV16X_ID,
	ST_LSM6DSO16IS_ID,
	ST_ISM330IS_ID,
	ST_ASM330LHB_ID,
	ST_ASM330LHHXG1_ID,
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

#define ST_LSM6DSX_CHANNEL_ACC(chan_type, addr, mod, scan_idx)		\
{									\
	.type = chan_type,						\
	.address = addr,						\
	.modified = 1,							\
	.channel2 = mod,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),		\
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.scan_index = scan_idx,						\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 16,						\
		.storagebits = 16,					\
		.endianness = IIO_LE,					\
	},								\
	.event_spec = &st_lsm6dsx_event,				\
	.ext_info = st_lsm6dsx_ext_info,				\
	.num_event_specs = 1,						\
}

#define ST_LSM6DSX_CHANNEL(chan_type, addr, mod, scan_idx)		\
{									\
	.type = chan_type,						\
	.address = addr,						\
	.modified = 1,							\
	.channel2 = mod,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),		\
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.scan_index = scan_idx,						\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 16,						\
		.storagebits = 16,					\
		.endianness = IIO_LE,					\
	},								\
	.ext_info = st_lsm6dsx_ext_info,				\
}

struct st_lsm6dsx_reg {
	u8 addr;
	u8 mask;
};

struct st_lsm6dsx_sensor;
struct st_lsm6dsx_hw;

struct st_lsm6dsx_odr {
	u32 milli_hz;
	u8 val;
};

#define ST_LSM6DSX_ODR_LIST_SIZE	8
struct st_lsm6dsx_odr_table_entry {
	struct st_lsm6dsx_reg reg;

	struct st_lsm6dsx_odr odr_avl[ST_LSM6DSX_ODR_LIST_SIZE];
	int odr_len;
};

struct st_lsm6dsx_samples_to_discard {
	struct {
		u32 milli_hz;
		u16 samples;
	} val[ST_LSM6DSX_ODR_LIST_SIZE];
};

struct st_lsm6dsx_fs {
	u32 gain;
	u8 val;
};

#define ST_LSM6DSX_FS_LIST_SIZE		4
struct st_lsm6dsx_fs_table_entry {
	struct st_lsm6dsx_reg reg;

	struct st_lsm6dsx_fs fs_avl[ST_LSM6DSX_FS_LIST_SIZE];
	int fs_len;
};

/**
 * struct st_lsm6dsx_fifo_ops - ST IMU FIFO settings
 * @update_fifo: Update FIFO configuration callback.
 * @read_fifo: Read FIFO callback.
 * @fifo_th: FIFO threshold register info (addr + mask).
 * @fifo_diff: FIFO diff status register info (addr + mask).
 * @max_size: Sensor max fifo length in FIFO words.
 * @th_wl: FIFO threshold word length.
 */
struct st_lsm6dsx_fifo_ops {
	int (*update_fifo)(struct st_lsm6dsx_sensor *sensor, bool enable);
	int (*read_fifo)(struct st_lsm6dsx_hw *hw);
	struct {
		u8 addr;
		u16 mask;
	} fifo_th;
	struct {
		u8 addr;
		u16 mask;
	} fifo_diff;
	u16 max_size;
	u8 th_wl;
};

/**
 * struct st_lsm6dsx_hw_ts_settings - ST IMU hw timer settings
 * @timer_en: Hw timer enable register info (addr + mask).
 * @hr_timer: Hw timer resolution register info (addr + mask).
 * @fifo_en: Hw timer FIFO enable register info (addr + mask).
 * @decimator: Hw timer FIFO decimator register info (addr + mask).
 * @freq_fine: Difference in % of ODR with respect to the typical.
 */
struct st_lsm6dsx_hw_ts_settings {
	struct st_lsm6dsx_reg timer_en;
	struct st_lsm6dsx_reg hr_timer;
	struct st_lsm6dsx_reg fifo_en;
	struct st_lsm6dsx_reg decimator;
	u8 freq_fine;
};

/**
 * struct st_lsm6dsx_shub_settings - ST IMU hw i2c controller settings
 * @page_mux: register page mux info (addr + mask).
 * @master_en: master config register info (addr + mask).
 * @pullup_en: i2c controller pull-up register info (addr + mask).
 * @aux_sens: aux sensor register info (addr + mask).
 * @wr_once: write_once register info (addr + mask).
 * @emb_func:  embedded function register info (addr + mask).
 * @num_ext_dev: max number of slave devices.
 * @shub_out: sensor hub first output register info.
 * @slv0_addr: slave0 address in secondary page.
 * @dw_slv0_addr: slave0 write register address in secondary page.
 * @batch_en: Enable/disable FIFO batching.
 * @pause: controller pause value.
 */
struct st_lsm6dsx_shub_settings {
	struct st_lsm6dsx_reg page_mux;
	struct {
		bool sec_page;
		u8 addr;
		u8 mask;
	} master_en;
	struct {
		bool sec_page;
		u8 addr;
		u8 mask;
	} pullup_en;
	struct st_lsm6dsx_reg aux_sens;
	struct st_lsm6dsx_reg wr_once;
	struct st_lsm6dsx_reg emb_func;
	u8 num_ext_dev;
	struct {
		bool sec_page;
		u8 addr;
	} shub_out;
	u8 slv0_addr;
	u8 dw_slv0_addr;
	u8 batch_en;
	u8 pause;
};

struct st_lsm6dsx_event_settings {
	struct st_lsm6dsx_reg enable_reg;
	struct st_lsm6dsx_reg wakeup_reg;
	u8 wakeup_src_reg;
	u8 wakeup_src_status_mask;
	u8 wakeup_src_z_mask;
	u8 wakeup_src_y_mask;
	u8 wakeup_src_x_mask;
};

enum st_lsm6dsx_ext_sensor_id {
	ST_LSM6DSX_ID_MAGN,
};

/**
 * struct st_lsm6dsx_ext_dev_settings - i2c controller slave settings
 * @i2c_addr: I2c slave address list.
 * @wai: Wai address info.
 * @id: external sensor id.
 * @odr_table: Output data rate of the sensor [Hz].
 * @fs_table: Configured sensor sensitivity table depending on full scale.
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
 * @reset: register address for reset.
 * @boot: register address for boot.
 * @bdu: register address for Block Data Update.
 * @id: List of hw id/device name supported by the driver configuration.
 * @channels: IIO channels supported by the device.
 * @irq_config: interrupts related registers.
 * @drdy_mask: register info for data-ready mask (addr + mask).
 * @odr_table: Hw sensors odr table (Hz + val).
 * @samples_to_discard: Number of samples to discard for filters settling time.
 * @fs_table: Hw sensors gain table (gain + val).
 * @decimator: List of decimator register info (addr + mask).
 * @batch: List of FIFO batching register info (addr + mask).
 * @fifo_ops: Sensor hw FIFO parameters.
 * @ts_settings: Hw timer related settings.
 * @shub_settings: i2c controller related settings.
 */
struct st_lsm6dsx_settings {
	struct st_lsm6dsx_reg reset;
	struct st_lsm6dsx_reg boot;
	struct st_lsm6dsx_reg bdu;
	struct {
		enum st_lsm6dsx_hw_id hw_id;
		const char *name;
		u8 wai;
	} id[ST_LSM6DSX_MAX_ID];
	struct {
		const struct iio_chan_spec *chan;
		int len;
	} channels[2];
	struct {
		struct st_lsm6dsx_reg irq1;
		struct st_lsm6dsx_reg irq2;
		struct st_lsm6dsx_reg irq1_func;
		struct st_lsm6dsx_reg irq2_func;
		struct st_lsm6dsx_reg lir;
		struct st_lsm6dsx_reg clear_on_read;
		struct st_lsm6dsx_reg hla;
		struct st_lsm6dsx_reg od;
	} irq_config;
	struct st_lsm6dsx_reg drdy_mask;
	struct st_lsm6dsx_odr_table_entry odr_table[2];
	struct st_lsm6dsx_samples_to_discard samples_to_discard[2];
	struct st_lsm6dsx_fs_table_entry fs_table[2];
	struct st_lsm6dsx_reg decimator[ST_LSM6DSX_MAX_ID];
	struct st_lsm6dsx_reg batch[ST_LSM6DSX_MAX_ID];
	struct st_lsm6dsx_fifo_ops fifo_ops;
	struct st_lsm6dsx_hw_ts_settings ts_settings;
	struct st_lsm6dsx_shub_settings shub_settings;
	struct st_lsm6dsx_event_settings event_settings;
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
 * @samples_to_discard: Number of samples to discard for filters settling time.
 * @watermark: Sensor watermark level.
 * @decimator: Sensor decimation factor.
 * @sip: Number of samples in a given pattern.
 * @ts_ref: Sensor timestamp reference for hw one.
 * @ext_info: Sensor settings if it is connected to i2c controller
 */
struct st_lsm6dsx_sensor {
	char name[32];
	enum st_lsm6dsx_sensor_id id;
	struct st_lsm6dsx_hw *hw;

	u32 gain;
	u32 odr;

	u16 samples_to_discard;
	u16 watermark;
	u8 decimator;
	u8 sip;
	s64 ts_ref;

	struct {
		const struct st_lsm6dsx_ext_dev_settings *settings;
		u32 slv_odr;
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
 * @suspend_mask: Suspended sensor bitmask.
 * @enable_mask: Enabled sensor bitmask.
 * @fifo_mask: Enabled hw FIFO bitmask.
 * @ts_gain: Hw timestamp rate after internal calibration.
 * @ts_sip: Total number of timestamp samples in a given pattern.
 * @sip: Total number of samples (acc/gyro/ts) in a given pattern.
 * @buff: Device read buffer.
 * @irq_routing: pointer to interrupt routing configuration.
 * @event_threshold: wakeup event threshold.
 * @enable_event: enabled event bitmask.
 * @iio_devs: Pointers to acc/gyro iio_dev instances.
 * @settings: Pointer to the specific sensor settings in use.
 * @orientation: sensor chip orientation relative to main hardware.
 * @scan: Temporary buffers used to align data before iio_push_to_buffers()
 */
struct st_lsm6dsx_hw {
	struct device *dev;
	struct regmap *regmap;
	int irq;

	struct mutex fifo_lock;
	struct mutex conf_lock;
	struct mutex page_lock;

	u8 suspend_mask;
	u8 enable_mask;
	u8 fifo_mask;
	s64 ts_gain;
	u8 ts_sip;
	u8 sip;

	const struct st_lsm6dsx_reg *irq_routing;
	u8 event_threshold;
	u8 enable_event;

	u8 *buff;

	struct iio_dev *iio_devs[ST_LSM6DSX_ID_MAX];

	const struct st_lsm6dsx_settings *settings;

	struct iio_mount_matrix orientation;
	/* Ensure natural alignment of buffer elements */
	struct {
		__le16 channels[3];
		aligned_s64 ts;
	} scan[ST_LSM6DSX_ID_MAX];
};

static __maybe_unused const struct iio_event_spec st_lsm6dsx_event = {
	.type = IIO_EV_TYPE_THRESH,
	.dir = IIO_EV_DIR_EITHER,
	.mask_separate = BIT(IIO_EV_INFO_VALUE) |
			 BIT(IIO_EV_INFO_ENABLE)
};

static __maybe_unused const unsigned long st_lsm6dsx_available_scan_masks[] = {
	0x7, 0x0,
};

extern const struct dev_pm_ops st_lsm6dsx_pm_ops;

int st_lsm6dsx_probe(struct device *dev, int irq, int hw_id,
		     struct regmap *regmap);
int st_lsm6dsx_sensor_set_enable(struct st_lsm6dsx_sensor *sensor,
				 bool enable);
int st_lsm6dsx_fifo_setup(struct st_lsm6dsx_hw *hw);
int st_lsm6dsx_set_watermark(struct iio_dev *iio_dev, unsigned int val);
int st_lsm6dsx_update_watermark(struct st_lsm6dsx_sensor *sensor,
				u16 watermark);
int st_lsm6dsx_update_fifo(struct st_lsm6dsx_sensor *sensor, bool enable);
int st_lsm6dsx_flush_fifo(struct st_lsm6dsx_hw *hw);
int st_lsm6dsx_resume_fifo(struct st_lsm6dsx_hw *hw);
int st_lsm6dsx_read_fifo(struct st_lsm6dsx_hw *hw);
int st_lsm6dsx_read_tagged_fifo(struct st_lsm6dsx_hw *hw);
int st_lsm6dsx_check_odr(struct st_lsm6dsx_sensor *sensor, u32 odr, u8 *val);
int st_lsm6dsx_shub_probe(struct st_lsm6dsx_hw *hw, const char *name);
int st_lsm6dsx_shub_set_enable(struct st_lsm6dsx_sensor *sensor, bool enable);
int st_lsm6dsx_shub_read_output(struct st_lsm6dsx_hw *hw, u8 *data, int len);
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

static inline const struct iio_mount_matrix *
st_lsm6dsx_get_mount_matrix(const struct iio_dev *iio_dev,
			    const struct iio_chan_spec *chan)
{
	struct st_lsm6dsx_sensor *sensor = iio_priv(iio_dev);
	struct st_lsm6dsx_hw *hw = sensor->hw;

	return &hw->orientation;
}

static inline int
st_lsm6dsx_device_set_enable(struct st_lsm6dsx_sensor *sensor, bool enable)
{
	if (sensor->id == ST_LSM6DSX_ID_EXT0 ||
	    sensor->id == ST_LSM6DSX_ID_EXT1 ||
	    sensor->id == ST_LSM6DSX_ID_EXT2)
		return st_lsm6dsx_shub_set_enable(sensor, enable);

	return st_lsm6dsx_sensor_set_enable(sensor, enable);
}

static const
struct iio_chan_spec_ext_info __maybe_unused st_lsm6dsx_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_ALL, st_lsm6dsx_get_mount_matrix),
	{ }
};

#endif /* ST_LSM6DSX_H */
