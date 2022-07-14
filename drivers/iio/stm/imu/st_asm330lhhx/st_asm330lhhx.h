/*
 * STMicroelectronics st_asm330lhhx sensor driver
 *
 * Copyright 2019 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi@st.com>
 *
 * Licensed under the GPL-2.
 */

#ifndef ST_ASM330LHHX_H
#define ST_ASM330LHHX_H

#include <linux/device.h>
#include <linux/iio/iio.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/spinlock.h>

#define ST_ASM330LHHX_DRV_VERSION		"1.1"
#define ST_ASM330LHHX_DEBUG_DISCHARGE

#define ST_ASM330LHHX_MAX_ODR			833
#define ST_ASM330LHHX_ODR_LIST_SIZE		8
#define ST_ASM330LHHX_ODR_EXPAND(odr, uodr)	((odr * 1000000) + uodr)

#define ST_ASM330LHH_DEV_NAME			"asm330lhh"
#define ST_ASM330LHHX_DEV_NAME			"asm330lhhx"

#define ST_ASM330LHHX_DEFAULT_XL_FS_INDEX	2
#define ST_ASM330LHHX_DEFAULT_XL_ODR_INDEX	1
#define ST_ASM330LHHX_DEFAULT_G_FS_INDEX	2
#define ST_ASM330LHHX_DEFAULT_G_ODR_INDEX	1
#define ST_ASM330LHHX_DEFAULT_T_FS_INDEX	0
#define ST_ASM330LHHX_DEFAULT_T_ODR_INDEX	1

#define ST_ASM330LHHX_REG_FIFO_CTRL1_ADDR	0x07
#define ST_ASM330LHHX_REG_FIFO_CTRL2_ADDR	0x08
#define ST_ASM330LHHX_REG_FIFO_WTM_MASK		GENMASK(8, 0)
#define ST_ASM330LHHX_REG_FIFO_WTM8_MASK	BIT(0)
#define ST_ASM330LHHX_REG_FIFO_STATUS_DIFF	GENMASK(9, 0)

#define ST_ASM330LHHX_REG_FIFO_CTRL3_ADDR	0x09
#define ST_ASM330LHHX_REG_BDR_XL_MASK		GENMASK(3, 0)
#define ST_ASM330LHHX_REG_BDR_GY_MASK		GENMASK(7, 4)

#define ST_ASM330LHHX_REG_FIFO_CTRL4_ADDR	0x0a
#define ST_ASM330LHHX_REG_FIFO_MODE_MASK	GENMASK(2, 0)
#define ST_ASM330LHHX_REG_DEC_TS_MASK		GENMASK(7, 6)
#define ST_ASM330LHHX_REG_ODR_T_BATCH_MASK	GENMASK(5, 4)

#define ST_ASM330LHHX_REG_INT1_CTRL_ADDR	0x0d
#define ST_ASM330LHHX_REG_INT2_CTRL_ADDR	0x0e
#define ST_ASM330LHHX_REG_INT_FIFO_TH_MASK	BIT(3)

#define ST_ASM330LHHX_REG_WHOAMI_ADDR		0x0f
#define ST_ASM330LHHX_WHOAMI_VAL		0x6b

#define ST_ASM330LHHX_CTRL1_XL_ADDR		0x10
#define ST_ASM330LHHX_CTRL2_G_ADDR		0x11

#define ST_ASM330LHHX_REG_CTRL3_C_ADDR		0x12
#define ST_ASM330LHHX_REG_SW_RESET_MASK		BIT(0)
#define ST_ASM330LHHX_REG_PP_OD_MASK		BIT(4)
#define ST_ASM330LHHX_REG_H_LACTIVE_MASK	BIT(5)
#define ST_ASM330LHHX_REG_BDU_MASK		BIT(6)
#define ST_ASM330LHHX_REG_BOOT_MASK		BIT(7)

#define ST_ASM330LHHX_REG_CTRL4_C_ADDR		0x13
#define ST_ASM330LHHX_REG_DRDY_MASK		BIT(3)

#define ST_ASM330LHHX_REG_CTRL5_C_ADDR		0x14
#define ST_ASM330LHHX_REG_ROUNDING_MASK		GENMASK(6, 5)
#define ST_ASM330LHHX_REG_ST_G_MASK		GENMASK(3, 2)
#define ST_ASM330LHHX_REG_ST_XL_MASK		GENMASK(1, 0)
#define ST_ASM330LHHX_SELFTEST_ACCEL_MIN	737
#define ST_ASM330LHHX_SELFTEST_ACCEL_MAX	13934
#define ST_ASM330LHHX_SELFTEST_GYRO_MIN		2142
#define ST_ASM330LHHX_SELFTEST_GYRO_MAX		10000

#define ST_ASM330LHHX_SELF_TEST_DISABLED_VAL	0
#define ST_ASM330LHHX_SELF_TEST_POS_SIGN_VAL	1
#define ST_ASM330LHHX_SELF_TEST_NEG_ACCEL_SIGN_VAL	2
#define ST_ASM330LHHX_SELF_TEST_NEG_GYRO_SIGN_VAL	3

#define ST_ASM330LHHX_REG_CTRL9_XL_ADDR		0x18
#define ST_ASM330LHHX_REG_DEVICE_CONF_MASK	BIT(1)

#define ST_ASM330LHHX_REG_CTRL10_C_ADDR		0x19
#define ST_ASM330LHHX_REG_TIMESTAMP_EN_MASK	BIT(5)

#define ST_ASM330LHHX_REG_STATUS_ADDR		0x1e
#define ST_ASM330LHHX_REG_STATUS_XLDA		BIT(0)
#define ST_ASM330LHHX_REG_STATUS_GDA		BIT(1)
#define ST_ASM330LHHX_REG_STATUS_TDA		BIT(2)

#define ST_ASM330LHHX_REG_OUT_TEMP_L_ADDR	0x20

#define ST_ASM330LHHX_REG_OUTX_L_A_ADDR		0x28
#define ST_ASM330LHHX_REG_OUTY_L_A_ADDR		0x2a
#define ST_ASM330LHHX_REG_OUTZ_L_A_ADDR		0x2c

#define ST_ASM330LHHX_REG_OUTX_L_G_ADDR		0x22
#define ST_ASM330LHHX_REG_OUTY_L_G_ADDR		0x24
#define ST_ASM330LHHX_REG_OUTZ_L_G_ADDR		0x26

#define ST_ASM330LHHX_REG_TIMESTAMP0_ADDR	0x40

#define ST_ASM330LHHX_REG_TAP_CFG0_ADDR		0x56
#define ST_ASM330LHHX_REG_LIR_MASK		BIT(0)

#define ST_ASM330LHHX_REG_THS_6D_ADDR		0x59
#define ST_ASM330LHHX_SIXD_THS_MASK		GENMASK(6, 5)

#define ST_ASM330LHHX_REG_WAKE_UP_THS_ADDR	0x5b
#define ST_ASM330LHHX_WAKE_UP_THS_MASK		GENMASK(5, 0)

#define ST_ASM330LHHX_REG_WAKE_UP_DUR_ADDR	0x5c
#define ST_ASM330LHHX_WAKE_UP_DUR_MASK		GENMASK(6, 5)

#define ST_ASM330LHHX_REG_FREE_FALL_ADDR	0x5d
#define ST_ASM330LHHX_FF_THS_MASK		GENMASK(2, 0)

#define ST_ASM330LHHX_INTERNAL_FREQ_FINE	0x63

/* Timestamp Tick 25us/LSB */
#define ST_ASM330LHHX_TS_DELTA_NS		25000ULL

/* Temperature in uC */
#define ST_ASM330LHHX_TEMP_GAIN			256
#define ST_ASM330LHHX_TEMP_FS_GAIN		1000000 / ST_ASM330LHHX_TEMP_GAIN
#define ST_ASM330LHHX_TEMP_OFFSET		6400

/* FIFO simple size and depth */
#define ST_ASM330LHHX_SAMPLE_SIZE		6
#define ST_ASM330LHHX_TS_SAMPLE_SIZE		4
#define ST_ASM330LHHX_TAG_SIZE			1
#define ST_ASM330LHHX_FIFO_SAMPLE_SIZE		(ST_ASM330LHHX_SAMPLE_SIZE + \
					 	 ST_ASM330LHHX_TAG_SIZE)
#define ST_ASM330LHHX_MAX_FIFO_DEPTH		416

#define ST_ASM330LHHX_DEFAULT_KTIME		(200000000)
#define ST_ASM330LHHX_FAST_KTIME		(5000000)

#define ST_ASM330LHHX_DATA_CHANNEL(chan_type, addr, mod, ch2, scan_idx,	\
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
	.ext_info = st_asm330lhhx_ext_info,				\
}

static const struct iio_event_spec st_asm330lhhx_flush_event = {
	.type = IIO_EV_TYPE_FIFO_FLUSH,
	.dir = IIO_EV_DIR_EITHER,
};

static const struct iio_event_spec st_asm330lhhx_thr_event = {
	.type = IIO_EV_TYPE_THRESH,
	.dir = IIO_EV_DIR_RISING,
	.mask_separate = BIT(IIO_EV_INFO_ENABLE),
};

#define ST_ASM330LHHX_EVENT_CHANNEL(ctype, etype)	\
{							\
	.type = ctype,					\
	.modified = 0,					\
	.scan_index = -1,				\
	.indexed = -1,					\
	.event_spec = &st_asm330lhhx_##etype##_event,	\
	.num_event_specs = 1,				\
}

#define ST_ASM330LHHX_RX_MAX_LENGTH	64
#define ST_ASM330LHHX_TX_MAX_LENGTH	16

struct st_asm330lhhx_transfer_buffer {
	u8 rx_buf[ST_ASM330LHHX_RX_MAX_LENGTH];
	u8 tx_buf[ST_ASM330LHHX_TX_MAX_LENGTH] ____cacheline_aligned;
};

struct st_asm330lhhx_transfer_function {
	int (*read)(struct device *dev, u8 addr, int len, u8 *data);
	int (*write)(struct device *dev, u8 addr, int len, const u8 *data);
};

/**
 * struct st_asm330lhhx_reg - Generic sensor register description (addr + mask)
 * @addr: Address of register.
 * @mask: Bitmask register for proper usage.
 */
struct st_asm330lhhx_reg {
	u8 addr;
	u8 mask;
};

enum st_asm330lhhx_suspend_resume_register {
	ST_ASM330LHHX_CTRL1_XL_REG = 0,
	ST_ASM330LHHX_CTRL2_G_REG,
	ST_ASM330LHHX_REG_CTRL3_C_REG,
	ST_ASM330LHHX_REG_CTRL4_C_REG,
	ST_ASM330LHHX_REG_CTRL5_C_REG,
	ST_ASM330LHHX_REG_CTRL10_C_REG,
	ST_ASM330LHHX_REG_TAP_CFG0_REG,
	ST_ASM330LHHX_REG_INT1_CTRL_REG,
	ST_ASM330LHHX_REG_INT2_CTRL_REG,
	ST_ASM330LHHX_REG_FIFO_CTRL1_REG,
	ST_ASM330LHHX_REG_FIFO_CTRL2_REG,
	ST_ASM330LHHX_REG_FIFO_CTRL3_REG,
	ST_ASM330LHHX_REG_FIFO_CTRL4_REG,
	ST_ASM330LHHX_SUSPEND_RESUME_REGS,
};

struct st_asm330lhhx_suspend_resume_entry {
	u8 addr;
	u8 val;
	u8 mask;
};

/**
 * struct st_asm330lhhx_odr - Single ODR entry
 * @hz: Most significant part of the sensor ODR (Hz).
 * @uhz: Less significant part of the sensor ODR (micro Hz).
 * @val: ODR register value.
 * @batch_val: Batching ODR register value.
 */
struct st_asm330lhhx_odr {
	u16 hz;
	u32 uhz;
	u8 val;
	u8 batch_val;
};

/**
 * struct st_asm330lhhx_odr_table_entry - Sensor ODR table
 * @size: Size of ODR table.
 * @reg: ODR register.
 * @batching_reg: ODR register for batching on fifo.
 * @odr_avl: Array of supported ODR value.
 */
struct st_asm330lhhx_odr_table_entry {
	u8 size;
	struct st_asm330lhhx_reg reg;
	struct st_asm330lhhx_reg batching_reg;
	struct st_asm330lhhx_odr odr_avl[ST_ASM330LHHX_ODR_LIST_SIZE];
};

/**
 * struct st_asm330lhhx_fs - Full Scale sensor table entry
 * @reg: Register description for FS settings.
 * @gain: Sensor sensitivity (mdps/LSB, mg/LSB and uC/LSB).
 * @val: FS register value.
 */
struct st_asm330lhhx_fs {
	struct st_asm330lhhx_reg reg;
	u32 gain;
	u8 val;
};

#define ST_ASM330LHHX_FS_LIST_SIZE		5
#define ST_ASM330LHHX_FS_ACC_LIST_SIZE		4
#define ST_ASM330LHHX_FS_GYRO_LIST_SIZE		5
#ifdef CONFIG_IIO_ST_ASM330LHHX_EN_TEMPERATURE
#define ST_ASM330LHHX_FS_TEMP_LIST_SIZE		1
#endif /* CONFIG_IIO_ST_ASM330LHHX_EN_TEMPERATURE */

/**
 * struct st_asm330lhhx_fs_table_entry - Full Scale sensor table
 * @size: Full Scale sensor table size.
 * @fs_avl: Full Scale list entries.
 */
struct st_asm330lhhx_fs_table_entry {
	u8 size;
	struct st_asm330lhhx_fs fs_avl[ST_ASM330LHHX_FS_LIST_SIZE];
};

#define ST_ASM330LHHX_ACC_FS_2G_GAIN	IIO_G_TO_M_S_2(61000)
#define ST_ASM330LHHX_ACC_FS_4G_GAIN	IIO_G_TO_M_S_2(122000)
#define ST_ASM330LHHX_ACC_FS_8G_GAIN	IIO_G_TO_M_S_2(244000)
#define ST_ASM330LHHX_ACC_FS_16G_GAIN	IIO_G_TO_M_S_2(488000)

#define ST_ASM330LHHX_GYRO_FS_250_GAIN	IIO_DEGREE_TO_RAD(8750000)
#define ST_ASM330LHHX_GYRO_FS_500_GAIN	IIO_DEGREE_TO_RAD(17500000)
#define ST_ASM330LHHX_GYRO_FS_1000_GAIN	IIO_DEGREE_TO_RAD(35000000)
#define ST_ASM330LHHX_GYRO_FS_2000_GAIN	IIO_DEGREE_TO_RAD(70000000)
#define ST_ASM330LHHX_GYRO_FS_4000_GAIN	IIO_DEGREE_TO_RAD(140000000)

enum st_asm330lhhx_sensor_id {
	ST_ASM330LHHX_ID_GYRO = 0,
	ST_ASM330LHHX_ID_ACC,
#ifdef CONFIG_IIO_ST_ASM330LHHX_EN_TEMPERATURE
	ST_ASM330LHHX_ID_TEMP,
#endif /* CONFIG_IIO_ST_ASM330LHHX_EN_TEMPERATURE */
	ST_ASM330LHHX_ID_EVENT,
	ST_ASM330LHHX_ID_FF = ST_ASM330LHHX_ID_EVENT,
	ST_ASM330LHHX_ID_SC,
	ST_ASM330LHHX_ID_TRIGGER,
	ST_ASM330LHHX_ID_WK = ST_ASM330LHHX_ID_TRIGGER,
	ST_ASM330LHHX_ID_6D,
	ST_ASM330LHHX_ID_MAX,
};

enum st_asm330lhhx_fifo_mode {
	ST_ASM330LHHX_FIFO_BYPASS = 0x0,
	ST_ASM330LHHX_FIFO_CONT = 0x6,
};

enum {
	ST_ASM330LHHX_HW_FLUSH,
	ST_ASM330LHHX_HW_OPERATIONAL,
};

/**
 * struct st_asm330lhhx_sensor - ST IMU sensor instance
 * @id: Sensor identifier.
 * @hw: Pointer to instance of struct st_asm330lhhx_hw.
 * @gain: Configured sensor sensitivity.
 * @offset: Sensor data offset.
 * @conf: Used in case of sensor event to manage configuration.
 * @odr: Output data rate of the sensor [Hz].
 * @uodr: Output data rate of the sensor [uHz].
 * @max_watermark: Max supported watermark level.
 * @watermark: Sensor watermark level.
 * @last_fifo_timestamp: Store last sample timestamp in FIFO, used by flush
 * @selftest_status: Last status of self test output
 * @min_st, @max_st: Min/Max acc/gyro data values during self test procedure
 */
struct st_asm330lhhx_sensor {
	enum st_asm330lhhx_sensor_id id;
	struct st_asm330lhhx_hw *hw;
	struct iio_trigger *trig;

	union {
		/* sensor with odrs, gain and offset */
		struct {
			u32 gain;
			u32 offset;
			int odr;
			int uodr;

#ifdef ST_ASM330LHHX_DEBUG_DISCHARGE
			u32 discharged_samples;
#endif /* ST_ASM330LHHX_DEBUG_DISCHARGE */

			u16 max_watermark;
			u16 watermark;
			s64 last_fifo_timestamp;

			/* self test */
			int8_t selftest_status;
			int min_st;
			int max_st;
		};

		/* sensor specific data configuration */
		struct {
			u32 conf[6];
		};
	};
};

/**
 * struct st_asm330lhhx_hw - ST IMU MEMS hw instance
 * @dev: Pointer to instance of struct device (I2C or SPI).
 * @irq: Device interrupt line (I2C or SPI).
 * @int_pin: Save interrupt pin used by sensor.
 * @lock: Mutex to protect read and write operations.
 * @fifo_lock: Mutex to prevent concurrent access to the hw FIFO.
 * @page_lock: Mutex to prevent concurrent memory page configuration.
 * @fifo_mode: FIFO operating mode supported by the device.
 * @state: hw operational state.
 * @enable_mask: Enabled sensor bitmask.
 * @hw_timestamp_global: hw timestamp value always monotonic where the most
 *                       significant 8byte are incremented at every disable/enable.
 * @timesync_workqueue: runs the async task in private workqueue.
 * @timesync_work: actual work to be done in the async task workqueue.
 * @timesync_timer: hrtimer used to schedule period read for the async task.
 * @hwtimestamp_lock: spinlock for the 64bit timestamp value.
 * @timesync_ktime: interval value used by the hrtimer.
 * @timestamp_c: counter used for counting number of timesync updates.
 * @ts_offset: Hw timestamp offset.
 * @ts_delta_ns: Calibrate delta time tick.
 * @hw_ts: Latest hw timestamp from the sensor.
 * @val_ts_old: Hold hw timestamp for timer rollover.
 * @hw_ts_high: Save MSB hw timestamp.
 * @tsample: Timestamp for each sensor sample.
 * @delta_ts: Delta time between two consecutive interrupts.
 * @ts: Latest timestamp from irq handler.
 * @odr_table_entry: Sensors ODR table.
 * @iio_devs: Pointers to acc/gyro iio_dev instances.
 * @tf: Transfer function structure used by I/O operations.
 * @tb: Transfer buffers used by SPI I/O operations.
 * @orientation: sensor chip orientation relative to main hardware.
 */
struct st_asm330lhhx_hw {
	struct device *dev;
	int irq;
	int int_pin;

	struct mutex lock;
	struct mutex fifo_lock;
	struct mutex page_lock;

	enum st_asm330lhhx_fifo_mode fifo_mode;
	unsigned long state;
	u32 enable_mask;

	s64 hw_timestamp_global;

#if defined (CONFIG_IIO_ST_ASM330LHHX_ASYNC_HW_TIMESTAMP)
	struct workqueue_struct *timesync_workqueue;
	struct work_struct timesync_work;
	struct hrtimer timesync_timer;
	spinlock_t hwtimestamp_lock;
	ktime_t timesync_ktime;
	int timesync_c;
#endif /* CONFIG_IIO_ST_ASM330LHHX_ASYNC_HW_TIMESTAMP */

	s64 ts_offset;
	u64 ts_delta_ns;
	s64 hw_ts;
	u32 val_ts_old;
	u32 hw_ts_high;
	s64 tsample;
	s64 delta_ts;
	s64 ts;

	const struct st_asm330lhhx_odr_table_entry *odr_table_entry;
	struct iio_dev *iio_devs[ST_ASM330LHHX_ID_MAX];

	struct regulator *vdd_supply;
	struct regulator *vddio_supply;

	const struct st_asm330lhhx_transfer_function *tf;
	struct st_asm330lhhx_transfer_buffer tb;

	struct iio_mount_matrix orientation;
};

/**
 * struct st_asm330lhhx_ff_th - Free Fall threshold table
 * @mg: Threshold in mg.
 * @val: Register value.
 */
struct st_asm330lhhx_ff_th {
	u32 mg;
	u8 val;
};

/**
 * struct st_asm330lhhx_6D_th - 6D threshold table
 * @deg: Threshold in degrees.
 * @val: Register value.
 */
struct st_asm330lhhx_6D_th {
	u8 deg;
	u8 val;
};

extern const struct dev_pm_ops st_asm330lhhx_pm_ops;

static inline int st_asm330lhhx_read_atomic(struct st_asm330lhhx_hw *hw,
					    u8 addr, int len, u8 *data)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = hw->tf->read(hw->dev, addr, len, data);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_asm330lhhx_write_atomic(struct st_asm330lhhx_hw *hw,
			   u8 addr, int len, u8 *data)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = hw->tf->write(hw->dev, addr, len, data);
	mutex_unlock(&hw->page_lock);

	return err;
}

int __st_asm330lhhx_write_with_mask(struct st_asm330lhhx_hw *hw, u8 addr,
				    u8 mask, u8 val);
static inline int
st_asm330lhhx_write_with_mask(struct st_asm330lhhx_hw *hw, u8 addr,
			      u8 mask, u8 val)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = __st_asm330lhhx_write_with_mask(hw, addr, mask, val);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline bool
st_asm330lhhx_is_fifo_enabled(struct st_asm330lhhx_hw *hw)
{
	return hw->enable_mask & (BIT(ST_ASM330LHHX_ID_GYRO) |
				  BIT(ST_ASM330LHHX_ID_ACC));
}

static inline s64 st_asm330lhhx_get_time_ns(struct iio_dev *iio_dev)
{
        return iio_get_time_ns(iio_dev);
}

int st_asm330lhhx_probe(struct device *dev, int irq,
		  const struct st_asm330lhhx_transfer_function *tf_ops);
int st_asm330lhhx_sensor_set_enable(struct st_asm330lhhx_sensor *sensor,
				    bool enable);
int st_asm330lhhx_buffers_setup(struct st_asm330lhhx_hw *hw);
int st_asm330lhhx_get_batch_val(struct st_asm330lhhx_sensor *sensor,
				int odr, int uodr, u8 *val);
int st_asm330lhhx_update_watermark(struct st_asm330lhhx_sensor *sensor,
				   u16 watermark);
ssize_t st_asm330lhhx_flush_fifo(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size);
ssize_t st_asm330lhhx_get_max_watermark(struct device *dev,
					struct device_attribute *attr,
					char *buf);
ssize_t st_asm330lhhx_get_watermark(struct device *dev,
				    struct device_attribute *attr,
				    char *buf);
ssize_t st_asm330lhhx_set_watermark(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size);
int st_asm330lhhx_suspend_fifo(struct st_asm330lhhx_hw *hw);
int st_asm330lhhx_set_fifo_mode(struct st_asm330lhhx_hw *hw,
			     enum st_asm330lhhx_fifo_mode fifo_mode);
int __st_asm330lhhx_set_sensor_batching_odr(struct st_asm330lhhx_sensor *sensor,
					    bool enable);
int st_asm330lhhx_update_batching(struct iio_dev *iio_dev, bool enable);
int st_asm330lhhx_reset_hwts(struct st_asm330lhhx_hw *hw);
#ifdef CONFIG_IIO_ST_ASM330LHHX_EN_BASIC_FEATURES
int st_asm330lhhx_event_handler(struct st_asm330lhhx_hw *hw);
int st_asm330lhhx_probe_event(struct st_asm330lhhx_hw *hw);
int st_asm330lhhx_set_wake_up_thershold(struct st_asm330lhhx_hw *hw,
					int th_ug);
int st_asm330lhhx_set_wake_up_duration(struct st_asm330lhhx_hw *hw,
				       int dur_ms);
int st_asm330lhhx_set_freefall_threshold(struct st_asm330lhhx_hw *hw,
					 int th_mg);
int st_asm330lhhx_set_6D_threshold(struct st_asm330lhhx_hw *hw,
				   int deg);
int st_asm330lhhx_read_with_mask(struct st_asm330lhhx_hw *hw, u8 addr,
				 u8 mask, u8 *val);
#endif /* CONFIG_IIO_ST_ASM330LHHX_EN_BASIC_FEATURES */

#if defined (CONFIG_IIO_ST_ASM330LHHX_ASYNC_HW_TIMESTAMP)
int st_asm330lhhx_hwtimesync_init(struct st_asm330lhhx_hw *hw);
#else /* CONFIG_IIO_ST_ASM330LHHX_ASYNC_HW_TIMESTAMP */
static inline int
st_asm330lhhx_hwtimesync_init(struct st_asm330lhhx_hw *hw)
{
	return 0;
}
#endif /* CONFIG_IIO_ST_ASM330LHHX_ASYNC_HW_TIMESTAMP */
#endif /* ST_ASM330LHHX_H */
