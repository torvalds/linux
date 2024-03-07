/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics lsm6ds3 driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2014-2016 STMicroelectronics Inc.
 */

#ifndef ST_LSM6DS3_H
#define ST_LSM6DS3_H

#include <linux/types.h>
#include <linux/iio/trigger.h>
#include <linux/version.h>

#include "../../common/stm_iio_types.h"

#if KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE
#include <linux/iio/iio-opaque.h>
#endif /* LINUX_VERSION_CODE */

#ifdef CONFIG_ST_LSM6DS3_IIO_MASTER_SUPPORT
#include <linux/i2c.h>
#endif /* CONFIG_ST_LSM6DS3_IIO_MASTER_SUPPORT */

#define LSM6DS3_DEV_NAME			"lsm6ds3"
#define LSM6DS33_DEV_NAME			"lsm6ds33"

enum st_mask_id {
	ST_MASK_ID_ACCEL = 0,
	ST_MASK_ID_GYRO,
	ST_MASK_ID_SIGN_MOTION,
	ST_MASK_ID_STEP_COUNTER,
	ST_MASK_ID_STEP_DETECTOR,
	ST_MASK_ID_TILT,
	ST_MASK_ID_EXT0,
	ST_MASK_ID_HW_PEDOMETER,
	ST_MASK_ID_SENSOR_HUB,
	ST_MASK_ID_DIGITAL_FUNC,
	ST_MASK_ID_SENSOR_HUB_ASYNC_OP,
};

#define ST_INDIO_DEV_NUM			6

#define ST_LSM6DS3_TX_MAX_LENGTH		12
#define ST_LSM6DS3_RX_MAX_LENGTH		8193

#define ST_LSM6DS3_BYTE_FOR_CHANNEL		2
#define ST_LSM6DS3_FIFO_ELEMENT_LEN_BYTE	6

#define ST_LSM6DS3_MAX_FIFO_SIZE		8192
#define ST_LSM6DS3_MAX_FIFO_THRESHOLD		1092
#define ST_LSM6DS3_MAX_FIFO_LENGHT		(ST_LSM6DS3_MAX_FIFO_SIZE / \
					ST_LSM6DS3_FIFO_ELEMENT_LEN_BYTE)

#define ST_LSM6DS3_SELFTEST_NA_MS		"na"
#define ST_LSM6DS3_SELFTEST_FAIL_MS		"fail"
#define ST_LSM6DS3_SELFTEST_PASS_MS		"pass"

#define ST_LSM6DS3_WAKE_UP_SENSORS	(BIT(ST_MASK_ID_SIGN_MOTION) | \
					BIT(ST_MASK_ID_TILT))

#ifdef CONFIG_ST_LSM6DS3_IIO_MASTER_SUPPORT
#define ST_LSM6DS3_NUM_CLIENTS			1
#else /* CONFIG_ST_LSM6DS3_IIO_MASTER_SUPPORT */
#define ST_LSM6DS3_NUM_CLIENTS			0
#endif /* CONFIG_ST_LSM6DS3_IIO_MASTER_SUPPORT */

#define ST_LSM6DS3_LSM_CHANNELS(device_type, modif, index, mod, \
						endian, sbits, rbits, addr, s) \
{ \
	.type = device_type, \
	.modified = modif, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			BIT(IIO_CHAN_INFO_SCALE), \
	.scan_index = index, \
	.channel2 = mod, \
	.address = addr, \
	.scan_type = { \
		.sign = s, \
		.realbits = rbits, \
		.shift = sbits - rbits, \
		.storagebits = sbits, \
		.endianness = endian, \
	}, \
}

extern const struct iio_event_spec lsm6ds3_fifo_flush_event;

#define ST_LSM6DS3_FLUSH_CHANNEL(device_type) \
{ \
	.type = device_type, \
	.modified = 0, \
	.scan_index = -1, \
	.indexed = -1, \
	.event_spec = &lsm6ds3_fifo_flush_event,\
	.num_event_specs = 1, \
}

#define ST_LSM6DS3_HWFIFO_ENABLED() \
	IIO_DEVICE_ATTR(hwfifo_enabled, S_IWUSR | S_IRUGO, \
			st_lsm6ds3_sysfs_get_hwfifo_enabled,\
			st_lsm6ds3_sysfs_set_hwfifo_enabled, 0);

#define ST_LSM6DS3_HWFIFO_WATERMARK() \
	IIO_DEVICE_ATTR(hwfifo_watermark, S_IWUSR | S_IRUGO, \
			st_lsm6ds3_sysfs_get_hwfifo_watermark,\
			st_lsm6ds3_sysfs_set_hwfifo_watermark, 0);

#define ST_LSM6DS3_HWFIFO_WATERMARK_MIN() \
	IIO_DEVICE_ATTR(hwfifo_watermark_min, S_IRUGO, \
			st_lsm6ds3_sysfs_get_hwfifo_watermark_min, NULL, 0);

#define ST_LSM6DS3_HWFIFO_WATERMARK_MAX() \
	IIO_DEVICE_ATTR(hwfifo_watermark_max, S_IRUGO, \
			st_lsm6ds3_sysfs_get_hwfifo_watermark_max, NULL, 0);

#define ST_LSM6DS3_HWFIFO_FLUSH() \
	IIO_DEVICE_ATTR(hwfifo_flush, S_IWUSR, NULL, \
			st_lsm6ds3_sysfs_flush_fifo, 0);

enum fifo_mode {
	BYPASS = 0,
	CONTINUOS,
};

struct st_lsm6ds3_transfer_buffer {
	struct mutex buf_lock;
	u8 rx_buf[ST_LSM6DS3_RX_MAX_LENGTH];
	u8 tx_buf[ST_LSM6DS3_TX_MAX_LENGTH] ____cacheline_aligned;
};

struct lsm6ds3_out_decimation {
	short decimator;
	short num_samples;
};

struct lsm6ds3_fifo_output {
	u8 sip;
	int64_t deltatime;
	int64_t deltatime_default;
	int64_t timestamp;
	int64_t timestamp_p;
	short decimator;
	short num_samples;
	bool initialized;
};

/* struct lsm6ds3_data - common data for i2c or spi driver instance
 * @name: pointer to the device name (i2c name or spi modalias).
 * @spi_connection: set if driver probed by i2c or spi.
 * @enable_digfunc_mask: mask used to enable/disable hw digital functions.
 * @enable_pedometer_mask: mask used to enable/disable hw pedometer function.
 * @enable_sensorhub_mask: mask used to enable/disable sensor-hub feature.
 * @irq_enable_fifo_mask: mask used to enable/disable fifo irq.
 * @irq_enable_accel_ext_mask: mask used to enable/disable accel irq.
 * @hw_odr: physical sensor odr expressed in Hz.
 * @v_odr: requested sensor odr by userspace expressed in Hz.
 * @hwfifo_enabled: is hwfifo enabled?
 * @hwfifo_decimator: hwfifo decimator factor.
 * @hwfifo_watermark: hwfifo watermark value.
 * @samples_to_discard: samples to discard due to ODR switch.
 * @nofifo_decimation: output status when fifo is disabled.
 * @fifo_output: output status when fifo is enabled.
 * @sensors_enabled: sensors enabled mask.
 * @sensors_use_fifo: sensors use fifo mask.
 * @accel_odr_dependency: odr dependency: accel, sensor-hub, dig-func.
 * @accel_on: accel is going to be enabled during fifo odr switch?
 * @magn_on: magn is going to be enabled during fifo odr switch?
 * @odr_lock: mutex to avoid race condition during odr switch.
 * @reset_steps: do I need to reset number of steps?
 * @sign_motion_event_ready: significan motion event is ready to be pushed.
 * @fifo_data: fifo data.
 * @gyro_selftest_status: gyroscope selftest result.
 * @accel_selftest_status: accelerometer selftest result.
 * @irq: irq number.
 * @timestamp: timestamp value from boot process.
 * @module_id: identify iio devices of the same sensor module.
 */
struct lsm6ds3_data {
	const char *name;

	bool spi_connection;

	u16 enable_digfunc_mask;
	u16 enable_pedometer_mask;
#ifdef CONFIG_ST_LSM6DS3_IIO_MASTER_SUPPORT
	u16 enable_sensorhub_mask;
#endif /* CONFIG_ST_LSM6DS3_IIO_MASTER_SUPPORT */

	u16 irq_enable_fifo_mask;
	u16 irq_enable_accel_ext_mask;

	unsigned int hw_odr[ST_INDIO_DEV_NUM + 1];
	unsigned int v_odr[ST_INDIO_DEV_NUM + 1];
	unsigned int trigger_odr;

	bool hwfifo_enabled[ST_INDIO_DEV_NUM + 1];
	u8 hwfifo_decimator[ST_INDIO_DEV_NUM + 1];
	u16 hwfifo_watermark[ST_INDIO_DEV_NUM + 1];
	u16 fifo_watermark;

	u8 samples_to_discard[ST_INDIO_DEV_NUM + 1];
	u8 samples_to_discard_2[ST_INDIO_DEV_NUM + 1];
	struct lsm6ds3_out_decimation nofifo_decimation[ST_INDIO_DEV_NUM + 1];
	struct lsm6ds3_fifo_output fifo_output[ST_INDIO_DEV_NUM + 1];

	u16 sensors_enabled;
	u16 sensors_use_fifo;
	u64 num_steps;

	int accel_odr_dependency[3];

	bool accel_on;
	bool magn_on;
	enum fifo_mode fifo_status;

	struct mutex odr_lock;

	bool reset_steps;
	bool sign_motion_event_ready;

	u8 *fifo_data;
	u8 accel_last_push[6];
	u8 gyro_last_push[6];
	u8 ext0_last_push[6];
	int8_t gyro_selftest_status;
	int8_t accel_selftest_status;

	int irq;

	s64 timestamp;
	int64_t fifo_enable_timestamp;
	int64_t slower_counter;
	uint8_t slower_id;

#ifdef CONFIG_ST_LSM6DS3_XL_DATA_INJECTION
	bool injection_mode;
	s64 last_injection_timestamp;
	struct hrtimer injection_timer;
	struct work_struct injection_work;
	spinlock_t injection_spinlock;
	u8 injection_data[30];
	u8 injection_samples;
#endif /* CONFIG_ST_LSM6DS3_XL_DATA_INJECTION */

	struct work_struct data_work;

	struct device *dev;
	struct iio_dev *indio_dev[ST_INDIO_DEV_NUM + 1];
	struct iio_trigger *trig[ST_INDIO_DEV_NUM + 1];
	struct mutex bank_registers_lock;
	struct mutex fifo_lock;
	u32 module_id;

#ifdef CONFIG_ST_LSM6DS3_IIO_MASTER_SUPPORT
	bool ext0_available;
	int8_t ext0_selftest_status;
	struct mutex i2c_transfer_lock;
#endif /* CONFIG_ST_LSM6DS3_IIO_MASTER_SUPPORT */

	const struct st_lsm6ds3_transfer_function *tf;
	struct st_lsm6ds3_transfer_buffer tb;
};

struct st_lsm6ds3_transfer_function {
	int (*write)(struct lsm6ds3_data *cdata,
				u8 reg_addr, int len, u8 *data, bool b_lock);
	int (*read)(struct lsm6ds3_data *cdata,
				u8 reg_addr, int len, u8 *data, bool b_lock);
};

struct lsm6ds3_sensor_data {
	struct lsm6ds3_data *cdata;

	unsigned int c_gain[3];

	u8 num_data_channels;
	u8 sindex;
	u8 data_out_reg;
	u8 *buffer_data;
};

int st_lsm6ds3_write_data_with_mask(struct lsm6ds3_data *cdata,
			u8 reg_addr, u8 mask, u8 data, bool b_lock);

int st_lsm6ds3_push_data_with_timestamp(struct lsm6ds3_data *cdata,
					u8 index, u8 *data, int64_t timestamp);

int st_lsm6ds3_common_probe(struct lsm6ds3_data *cdata, int irq);
void st_lsm6ds3_common_remove(struct lsm6ds3_data *cdata, int irq);

int st_lsm6ds3_set_enable(struct lsm6ds3_sensor_data *sdata, bool enable, bool buffer);
int st_lsm6ds3_set_fifo_mode(struct lsm6ds3_data *cdata, enum fifo_mode fm);
int st_lsm6ds3_enable_sensor_hub(struct lsm6ds3_data *cdata, bool enable,
							enum st_mask_id id);
int lsm6ds3_read_output_data(struct lsm6ds3_data *cdata, int sindex, bool push);
int st_lsm6ds3_set_drdy_irq(struct lsm6ds3_sensor_data *sdata, bool state);

ssize_t st_lsm6ds3_sysfs_get_hwfifo_enabled(struct device *dev,
				struct device_attribute *attr, char *buf);
ssize_t st_lsm6ds3_sysfs_set_hwfifo_enabled(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size);
ssize_t st_lsm6ds3_sysfs_get_hwfifo_watermark(struct device *dev,
				struct device_attribute *attr, char *buf);
ssize_t st_lsm6ds3_sysfs_set_hwfifo_watermark(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size);
ssize_t st_lsm6ds3_sysfs_get_hwfifo_watermark_max(struct device *dev,
				struct device_attribute *attr, char *buf);
ssize_t st_lsm6ds3_sysfs_get_hwfifo_watermark_min(struct device *dev,
				struct device_attribute *attr, char *buf);
ssize_t st_lsm6ds3_sysfs_flush_fifo(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size);
ssize_t st_lsm6ds3_get_module_id(struct device *dev,
				 struct device_attribute *attr,
				 char *buf);

#ifdef CONFIG_IIO_BUFFER
int st_lsm6ds3_allocate_rings(struct lsm6ds3_data *cdata);
void st_lsm6ds3_deallocate_rings(struct lsm6ds3_data *cdata);
int st_lsm6ds3_trig_set_state(struct iio_trigger *trig, bool state);
int st_lsm6ds3_read_fifo(struct lsm6ds3_data *cdata, bool async);
#define ST_LSM6DS3_TRIGGER_SET_STATE (&st_lsm6ds3_trig_set_state)
#else /* CONFIG_IIO_BUFFER */
static inline int st_lsm6ds3_allocate_rings(struct lsm6ds3_data *cdata)
{
	return 0;
}
static inline void st_lsm6ds3_deallocate_rings(struct lsm6ds3_data *cdata)
{
}
static inline int st_lsm6ds3_read_fifo(struct lsm6ds3_data *cdata, bool async)
{
	return 0;
}
#define ST_LSM6DS3_TRIGGER_SET_STATE NULL
#endif /* CONFIG_IIO_BUFFER */

#ifdef CONFIG_IIO_TRIGGER
int st_lsm6ds3_allocate_triggers(struct lsm6ds3_data *cdata,
				const struct iio_trigger_ops *trigger_ops);
void st_lsm6ds3_deallocate_triggers(struct lsm6ds3_data *cdata);
void st_lsm6ds3_flush_works(void);
#else /* CONFIG_IIO_TRIGGER */
static inline int st_lsm6ds3_allocate_triggers(struct lsm6ds3_data *cdata,
			const struct iio_trigger_ops *trigger_ops, int irq)
{
	return 0;
}
static inline void st_lsm6ds3_deallocate_triggers(struct lsm6ds3_data *cdata,
								int irq)
{
	return;
}
static inline void st_lsm6ds3_flush_works(void)
{
	return;
}
#endif /* CONFIG_IIO_TRIGGER */

#ifdef CONFIG_PM
int st_lsm6ds3_common_suspend(struct lsm6ds3_data *cdata);
int st_lsm6ds3_common_resume(struct lsm6ds3_data *cdata);
#endif /* CONFIG_PM */

#ifdef CONFIG_ST_LSM6DS3_IIO_MASTER_SUPPORT
int st_lsm6ds3_write_embedded_registers(struct lsm6ds3_data *cdata,
						u8 reg_addr, u8 *data, int len);
int st_lsm6ds3_i2c_master_probe(struct lsm6ds3_data *cdata);
int st_lsm6ds3_i2c_master_exit(struct lsm6ds3_data *cdata);
#else /* CONFIG_ST_LSM6DS3_IIO_MASTER_SUPPORT */
static inline int st_lsm6ds3_i2c_master_probe(struct lsm6ds3_data *cdata)
{
	return 0;
}
static inline int st_lsm6ds3_i2c_master_exit(struct lsm6ds3_data *cdata)
{
	return 0;
}
#endif /* CONFIG_ST_LSM6DS3_IIO_MASTER_SUPPORT */

static inline int st_lsm6ds3_iio_dev_currentmode(struct iio_dev *indio_dev)
{

#if KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE
	struct iio_dev_opaque *iio_opq = to_iio_dev_opaque(indio_dev);

	return iio_opq->currentmode;
#else /* LINUX_VERSION_CODE */
	return indio_dev->currentmode;
#endif /* LINUX_VERSION_CODE */

}

#endif /* ST_LSM6DS3_H */
