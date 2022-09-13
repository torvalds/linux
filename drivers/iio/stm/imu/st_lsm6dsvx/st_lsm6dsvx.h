/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics st_lsm6dsvx sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2022 STMicroelectronics Inc.
 */

#ifndef ST_LSM6DSVX_H
#define ST_LSM6DSVX_H

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iio/iio.h>
#include <linux/regmap.h>

#define ST_LSM6DSVX_ODR_LIST_SIZE		9
#define ST_LSM6DSVX_ODR_EXPAND(odr, uodr)	((odr * 1000000) + uodr)

#define ST_LSM6DSV16X_DEV_NAME			"lsm6dsv16x"

#define ST_LSM6DSVX_SAMPLE_SIZE			6
#define ST_LSM6DSVX_TS_SAMPLE_SIZE		4
#define ST_LSM6DSVX_TAG_SIZE			1
#define ST_LSM6DSVX_FIFO_SAMPLE_SIZE		(ST_LSM6DSVX_SAMPLE_SIZE + \
						 ST_LSM6DSVX_TAG_SIZE)
#define ST_LSM6DSVX_MAX_FIFO_DEPTH		208

/* register map */
#define ST_LSM6DSVX_REG_FUNC_CFG_ACCESS_ADDR	0x01
#define ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK	BIT(7)
#define ST_LSM6DSVX_SHUB_REG_ACCESS_MASK	BIT(6)

#define ST_LSM6DSVX_REG_IF_CFG_ADDR		0x03
#define ST_LSM6DSVX_SHUB_PU_EN_MASK		BIT(6)
#define ST_LSM6DSVX_H_LACTIVE_MASK		BIT(4)
#define ST_LSM6DSVX_PP_OD_MASK			BIT(3)

#define ST_LSM6DSVX_REG_FIFO_CTRL1_ADDR		0x07
#define ST_LSM6DSVX_WTM_MASK			GENMASK(7, 0)

#define ST_LSM6DSVX_REG_FIFO_CTRL3_ADDR		0x09
#define ST_LSM6DSVX_BDR_GY_MASK			GENMASK(7, 4)
#define ST_LSM6DSVX_BDR_XL_MASK			GENMASK(3, 0)

#define ST_LSM6DSVX_REG_FIFO_CTRL4_ADDR		0x0a
#define ST_LSM6DSVX_DEC_TS_BATCH_MASK		GENMASK(7, 6)
#define ST_LSM6DSVX_FIFO_MODE_MASK		GENMASK(2, 0)

#define ST_LSM6DSVX_REG_INT1_CTRL_ADDR		0x0d
#define ST_LSM6DSVX_REG_INT2_CTRL_ADDR		0x0e
#define ST_LSM6DSVX_INT_FIFO_TH_MASK		BIT(3)

#define ST_LSM6DSVX_REG_WHOAMI_ADDR		0x0f
#define ST_LSM6DSVX_WHOAMI_VAL			0x70

#define ST_LSM6DSVX_REG_CTRL1_ADDR		0x10
#define ST_LSM6DSVX_REG_CTRL2_ADDR		0x11

#define ST_LSM6DSVX_REG_CTRL3_ADDR		0x12
#define ST_LSM6DSVX_BOOT_MASK			BIT(7)
#define ST_LSM6DSVX_BDU_MASK			BIT(6)
#define ST_LSM6DSVX_SW_RESET_MASK		BIT(0)

#define ST_LSM6DSVX_REG_CTRL4_ADDR		0x13
#define ST_LSM6DSVX_DRDY_MASK			BIT(3)

#define ST_LSM6DSVX_REG_CTRL6_ADDR		0x15

#define ST_LSM6DSVX_REG_CTRL7_ADDR		0x16
#define ST_LSM6DSVX_AH_QVAR_EN_MASK		BIT(7)
#define ST_LSM6DSVX_AH_QVAR_C_ZIN_MASK		GENMASK(5, 4)

#define ST_LSM6DSVX_REG_CTRL8_ADDR		0x17

#define ST_LSM6DSVX_REG_FIFO_STATUS1_ADDR	0x1b
#define ST_LSM6DSVX_FIFO_DIFF_MASK		GENMASK(8, 0)

#define ST_LSM6DSVX_REG_OUTX_L_G_ADDR		0x22
#define ST_LSM6DSVX_REG_OUTY_L_G_ADDR		0x24
#define ST_LSM6DSVX_REG_OUTZ_L_G_ADDR		0x26
#define ST_LSM6DSVX_REG_OUTX_L_A_ADDR		0x28
#define ST_LSM6DSVX_REG_OUTY_L_A_ADDR		0x2a
#define ST_LSM6DSVX_REG_OUTZ_L_A_ADDR		0x2c

#define ST_LSM6DSVX_REG_OUT_QVAR_ADDR		0x3a

#define ST_LSM6DSVX_REG_INTERNAL_FREQ_FINE	0x4f

#define ST_LSM6DSVX_REG_FUNCTIONS_ENABLE_ADDR	0x50
#define ST_LSM6DSVX_TIMESTAMP_EN_MASK		BIT(6)

#define ST_LSM6DSVX_REG_TAP_CFG0_ADDR		0x56
#define ST_LSM6DSVX_LIR_MASK			BIT(0)

#define ST_LSM6DSVX_REG_TIMESTAMP2_ADDR		0x42

#define ST_LSM6DSVX_REG_FIFO_DATA_OUT_TAG_ADDR	0x78

/* SHUB */
#define ST_LSM6DSVX_REG_SENSOR_HUB_1_ADDR	0x02

#define ST_LSM6DSVX_REG_MASTER_CONFIG_ADDR	0x14
#define ST_LSM6DSVX_WRITE_ONCE_MASK		BIT(6)
#define ST_LSM6DSVX_MASTER_ON_MASK		BIT(2)

#define ST_LSM6DSVX_REG_SLV0_ADDR		0x15

#define ST_LSM6DSVX_REG_SLV0_CONFIG_ADDR	0x17
#define ST_LSM6DSVX_SHUB_ODR_MASK		GENMASK(7, 5)
#define ST_LSM6DSVX_REG_SHUB_ODR_120HZ_VAL	0x04
#define ST_LSM6DSVX_REG_BATCH_EXT_SENS_EN_MASK	BIT(3)

#define ST_LSM6DSVX_REG_SLV1_ADDR		0x18
#define ST_LSM6DSVX_REG_SLV2_ADDR		0x1b
#define ST_LSM6DSVX_REG_SLV3_ADDR		0x1e

#define ST_LSM6DSVX_REG_DATAWRITE_SLV0_ADDR	0x21
#define ST_LSM6DSVX_REG_SLAVE_NUMOP_MASK	GENMASK(2, 0)

#define ST_LSM6DSVX_TS_DELTA_NS			21700ULL

#define ST_LSM6DSVX_DATA_CHANNEL(chan_type, addr, mod, ch2, scan_idx, \
				 rb, sb, sg, ext_inf)		      \
{								      \
	.type = chan_type,					      \
	.address = addr,					      \
	.modified = mod,					      \
	.channel2 = ch2,					      \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		      \
			      BIT(IIO_CHAN_INFO_SCALE),		      \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),      \
	.scan_index = scan_idx,					      \
	.scan_type = {						      \
		.sign = sg,					      \
		.realbits = rb,					      \
		.storagebits = sb,				      \
		.endianness = IIO_LE,				      \
	},							      \
	.ext_info = ext_inf,					      \
}

static const struct iio_event_spec st_lsm6dsvx_flush_event = {
	.type = IIO_EV_TYPE_FIFO_FLUSH,
	.dir = IIO_EV_DIR_EITHER,
};

static const struct iio_event_spec st_lsm6dsvx_thr_event = {
	.type = IIO_EV_TYPE_THRESH,
	.dir = IIO_EV_DIR_RISING,
	.mask_separate = BIT(IIO_EV_INFO_ENABLE),
};

#define ST_LSM6DSVX_EVENT_CHANNEL(ctype, etype)		\
{							\
	.type = ctype,					\
	.modified = 0,					\
	.scan_index = -1,				\
	.indexed = -1,					\
	.event_spec = &st_lsm6dsvx_##etype##_event,	\
	.num_event_specs = 1,				\
}

#define ST_LSM6DSVX_SHIFT_VAL(val, mask)	(((val) << __ffs(mask)) & (mask))

struct st_lsm6dsvx_reg {
	u8 addr;
	u8 mask;
};

struct st_lsm6dsvx_odr {
	u16 hz;
	int uhz;
	u8 val;
	u8 batch_val;
};

struct st_lsm6dsvx_odr_table_entry {
	u8 size;
	struct st_lsm6dsvx_reg reg;
	struct st_lsm6dsvx_odr odr_avl[ST_LSM6DSVX_ODR_LIST_SIZE];
};

struct st_lsm6dsvx_fs {
	struct st_lsm6dsvx_reg reg;
	u32 gain;
	u8 val;
};

#define ST_LSM6DSVX_FS_LIST_SIZE		6
#define ST_LSM6DSVX_FS_ACC_LIST_SIZE		4
#define ST_LSM6DSVX_FS_GYRO_LIST_SIZE		6
struct st_lsm6dsvx_fs_table_entry {
	u8 size;
	struct st_lsm6dsvx_fs fs_avl[ST_LSM6DSVX_FS_LIST_SIZE];
};

#define ST_LSM6DSVX_ACC_FS_2G_GAIN	IIO_G_TO_M_S_2(61)
#define ST_LSM6DSVX_ACC_FS_4G_GAIN	IIO_G_TO_M_S_2(122)
#define ST_LSM6DSVX_ACC_FS_8G_GAIN	IIO_G_TO_M_S_2(244)
#define ST_LSM6DSVX_ACC_FS_16G_GAIN	IIO_G_TO_M_S_2(488)

#define ST_LSM6DSVX_GYRO_FS_125_GAIN	IIO_DEGREE_TO_RAD(4375)
#define ST_LSM6DSVX_GYRO_FS_250_GAIN	IIO_DEGREE_TO_RAD(8750)
#define ST_LSM6DSVX_GYRO_FS_500_GAIN	IIO_DEGREE_TO_RAD(17500)
#define ST_LSM6DSVX_GYRO_FS_1000_GAIN	IIO_DEGREE_TO_RAD(35000)
#define ST_LSM6DSVX_GYRO_FS_2000_GAIN	IIO_DEGREE_TO_RAD(70000)
#define ST_LSM6DSVX_GYRO_FS_4000_GAIN	IIO_DEGREE_TO_RAD(140000)

struct st_lsm6dsvx_ext_dev_info {
	const struct st_lsm6dsvx_ext_dev_settings *ext_dev_settings;
	u8 ext_dev_i2c_addr;
};

enum st_lsm6dsvx_sensor_id {
	ST_LSM6DSVX_ID_GYRO,
	ST_LSM6DSVX_ID_ACC,

#ifdef CONFIG_IIO_ST_LSM6DSVX_QVAR
	ST_LSM6DSVX_ID_QVAR,
#endif /* CONFIG_IIO_ST_LSM6DSVX_QVAR */

	ST_LSM6DSVX_ID_EXT0,
	ST_LSM6DSVX_ID_EXT1,
	ST_LSM6DSVX_ID_MAX,
};

static const enum st_lsm6dsvx_sensor_id
st_lsm6dsvx_main_sensor_list[] = {
	[0] = ST_LSM6DSVX_ID_GYRO,
	[1] = ST_LSM6DSVX_ID_ACC,
};

enum st_lsm6dsvx_fifo_mode {
	ST_LSM6DSVX_FIFO_BYPASS = 0x0,
	ST_LSM6DSVX_FIFO_CONT = 0x6,
};

enum {
	ST_LSM6DSVX_HW_FLUSH,
	ST_LSM6DSVX_HW_OPERATIONAL,
};

/* sensor devices that can wakeup the target */
#define  ST_LSM6DSVX_WAKE_UP_SENSORS (BIT(ST_LSM6DSVX_ID_GYRO) | \
				      BIT(ST_LSM6DSVX_ID_ACC))

/**
 * struct st_lsm6dsvx_sensor - ST IMU sensor instance
 * @id: Sensor identifier.
 * @hw: Pointer to instance of struct st_lsm6dsvx_hw.
 * @ext_dev_info: Sensor hub i2c slave settings.
 * @gain: Configured sensor sensitivity.
 * @odr: Output data rate of the sensor [Hz].
 * @uodr: Output data rate of the sensor [uHz].
 * @hr_timer: hr timer for qvar.
 * @iio_work: iio work for qvar.
 * @oldktime: hr timeout for qvar.
 * @timestamp: qvar timestamp (when in polling mode).
 * @std_samples: Counter of samples to discard during sensor bootstrap.
 * @std_level: Samples to discard threshold.
 * @decimator: Samples decimate counter.
 * @dec_counter: Samples decimate value.
 * @max_watermark: Max supported watermark level.
 * @watermark: Sensor watermark level.
 * @batch_reg: Batching register info (addr + mask).
 */
struct st_lsm6dsvx_sensor {
	enum st_lsm6dsvx_sensor_id id;
	struct st_lsm6dsvx_hw *hw;

	struct st_lsm6dsvx_ext_dev_info ext_dev_info;

	u32 gain;
	int odr;
	int uodr;

#ifdef CONFIG_IIO_ST_LSM6DSVX_QVAR
	struct hrtimer hr_timer;
	struct work_struct iio_work;
	ktime_t oldktime;
	s64 timestamp;
#endif /* CONFIG_IIO_ST_LSM6DSVX_QVAR */

	u8 std_samples;
	u8 std_level;

	u8 decimator;
	u8 dec_counter;

	u16 max_watermark;
	u16 watermark;

	struct st_lsm6dsvx_reg batch_reg;
};

/**
 * struct st_lsm6dsvx_hw - ST IMU MEMS hw instance
 * @dev: Pointer to instance of struct device (I2C / SPI / I3C).
 * @irq: Device interrupt line (I2C / SPI / I3C).
 * @regmap: regmap structure pointer.
 * @lock: Mutex to protect read and write operations.
 * @fifo_lock: Mutex to prevent concurrent access to the hw FIFO.
 * @page_lock: Mutex to prevent concurrent memory page configuration.
 * @fifo_mode: FIFO operating mode supported by the device.
 * @state: hw operational state.
 * @enable_mask: Enabled sensor bitmask.
 * @ext_data_len: Number of i2c slave devices connected to I2C master.
 * @ts_offset: Hw timestamp offset.
 * @ts_delta_ns: Calibrated delta timestamp.
 * @hw_ts: Latest hw timestamp from the sensor.
 * @val_ts_old: Last sample timestamp for rollover check.
 * @hw_ts_high: Manage timestamp rollover.
 * @tsample: Sample timestamp.
 * @delta_ts: Estimated delta time between two consecutive interrupts.
 * @ts: Latest timestamp from irq handler.
 * @last_fifo_timestamp: Last timestamp in FIFO, used by flush event.
 * @orientation: Sensor orientation matrix.
 * @vdd_supply: Voltage regulator for VDD.
 * @vddio_supply: Voltage regulator for VDDIIO.
 * @qvar_workqueue: QVAR workqueue (if enabled in Kconfig).
 * @iio_devs: Pointers to acc/gyro iio_dev instances.
 */
struct st_lsm6dsvx_hw {
	struct device *dev;
	int irq;
	struct regmap *regmap;
	struct mutex lock;
	struct mutex fifo_lock;
	struct mutex page_lock;

	enum st_lsm6dsvx_fifo_mode fifo_mode;
	unsigned long state;
	u32 enable_mask;

	u8 ext_data_len;

	s64 ts_offset;
	u64 ts_delta_ns;
	s64 hw_ts;
	u32 val_ts_old;
	u32 hw_ts_high;
	s64 tsample;
	s64 delta_ts;
	s64 ts;
	s64 last_fifo_timestamp;

	struct iio_mount_matrix orientation;
	struct regulator *vdd_supply;
	struct regulator *vddio_supply;

#ifdef CONFIG_IIO_ST_LSM6DSVX_QVAR
	struct workqueue_struct *qvar_workqueue;
#endif /* CONFIG_IIO_ST_LSM6DSVX_QVAR */

	struct iio_dev *iio_devs[ST_LSM6DSVX_ID_MAX];
};

extern const struct dev_pm_ops st_lsm6dsvx_pm_ops;

static inline int
__st_lsm6dsvx_write_with_mask(struct st_lsm6dsvx_hw *hw,
			      unsigned int addr, unsigned int mask,
			      unsigned int data)
{
	int err;
	unsigned int val = ST_LSM6DSVX_SHIFT_VAL(data, mask);

	err = regmap_update_bits(hw->regmap, addr, mask, val);

	return err;
}

static inline int st_lsm6dsvx_write_with_mask(struct st_lsm6dsvx_hw *hw,
					      unsigned int addr,
					      unsigned int mask,
					      unsigned int data)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = __st_lsm6dsvx_write_with_mask(hw, addr, mask, data);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_lsm6dsvx_read_locked(struct st_lsm6dsvx_hw *hw, unsigned int addr,
			void *data, unsigned int len)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = regmap_bulk_read(hw->regmap, addr, data, len);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_lsm6dsvx_write_locked(struct st_lsm6dsvx_hw *hw, unsigned int addr,
			 unsigned int val)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = regmap_write(hw->regmap, addr, val);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int st_lsm6dsvx_set_page_access(struct st_lsm6dsvx_hw *hw,
					      unsigned int mask,
					      unsigned int data)
{
	return regmap_update_bits(hw->regmap,
				  ST_LSM6DSVX_REG_FUNC_CFG_ACCESS_ADDR,
				  mask,
				  ST_LSM6DSVX_SHIFT_VAL(data, mask));
}

static inline bool
st_lsm6dsvx_is_fifo_enabled(struct st_lsm6dsvx_hw *hw)
{
	return hw->enable_mask & (BIT(ST_LSM6DSVX_ID_GYRO)
				| BIT(ST_LSM6DSVX_ID_ACC)
				| BIT(ST_LSM6DSVX_ID_EXT0)
				| BIT(ST_LSM6DSVX_ID_EXT1)

#ifdef CONFIG_IIO_ST_LSM6DSVX_QVAR
				| BIT(ST_LSM6DSVX_ID_QVAR)
#endif /* CONFIG_IIO_ST_LSM6DSVX_QVAR */

				);
}

int st_lsm6dsvx_probe(struct device *dev, int irq,
		      struct regmap *regmap);
int st_lsm6dsvx_sensor_set_enable(struct st_lsm6dsvx_sensor *sensor,
				 bool enable);
int st_lsm6dsvx_shub_set_enable(struct st_lsm6dsvx_sensor *sensor,
				bool enable);
int st_lsm6dsvx_buffers_setup(struct st_lsm6dsvx_hw *hw);
ssize_t st_lsm6dsvx_flush_fifo(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size);
ssize_t st_lsm6dsvx_get_max_watermark(struct device *dev,
				      struct device_attribute *attr,
				      char *buf);
ssize_t st_lsm6dsvx_get_watermark(struct device *dev,
				  struct device_attribute *attr,
				  char *buf);
ssize_t st_lsm6dsvx_set_watermark(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size);
int st_lsm6dsvx_suspend_fifo(struct st_lsm6dsvx_hw *hw);
int st_lsm6dsvx_set_fifo_mode(struct st_lsm6dsvx_hw *hw,
			      enum st_lsm6dsvx_fifo_mode fifo_mode);
int
__st_lsm6dsvx_set_sensor_batching_odr(struct st_lsm6dsvx_sensor *sensor,
				      bool enable);
int st_lsm6dsvx_fsm_init(struct st_lsm6dsvx_hw *hw);
int st_lsm6dsvx_fsm_get_orientation(struct st_lsm6dsvx_hw *hw,
				    u8 *data);
int st_lsm6dsvx_update_batching(struct iio_dev *iio_dev, bool enable);
int st_lsm6dsvx_get_batch_val(struct st_lsm6dsvx_sensor *sensor,
			      int odr, int uodr, u8 *val);
int st_lsm6dsvx_remove(struct device *dev);

int st_lsm6dsvx_shub_probe(struct st_lsm6dsvx_hw *hw);

#ifdef CONFIG_IIO_ST_LSM6DSVX_QVAR
int st_lsm6dsvx_qvar_probe(struct st_lsm6dsvx_hw *hw);
int
st_lsm6dsvx_qvar_sensor_set_enable(struct st_lsm6dsvx_sensor *sensor,
				   bool enable);
int st_lsm6dsvx_qvar_remove(struct device *dev);
#endif /* CONFIG_IIO_ST_LSM6DSVX_QVAR */

#endif /* ST_LSM6DSVX_H */
