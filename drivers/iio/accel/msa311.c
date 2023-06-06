// SPDX-License-Identifier: GPL-2.0
/*
 * MEMSensing digital 3-Axis accelerometer
 *
 * MSA311 is a tri-axial, low-g accelerometer with I2C digital output for
 * sensitivity consumer applications. It has dynamic user-selectable full
 * scales range of +-2g/+-4g/+-8g/+-16g and allows acceleration measurements
 * with output data rates from 1Hz to 1000Hz.
 *
 * MSA311 is available in an ultra small (2mm x 2mm, height 0.95mm) LGA package
 * and is guaranteed to operate over -40C to +85C.
 *
 * This driver supports following MSA311 features:
 *     - IIO interface
 *     - Different power modes: NORMAL, SUSPEND
 *     - ODR (Output Data Rate) selection
 *     - Scale selection
 *     - IIO triggered buffer
 *     - NEW_DATA interrupt + trigger
 *
 * Below features to be done:
 *     - Motion Events: ACTIVE, TAP, ORIENT, FREEFALL
 *     - Low Power mode
 *
 * Copyright (c) 2022, SberDevices. All Rights Reserved.
 *
 * Author: Dmitry Rokosov <ddrokosov@sberdevices.ru>
 */

#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/string_helpers.h>
#include <linux/units.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define MSA311_SOFT_RESET_REG     0x00
#define MSA311_PARTID_REG         0x01
#define MSA311_ACC_X_REG          0x02
#define MSA311_ACC_Y_REG          0x04
#define MSA311_ACC_Z_REG          0x06
#define MSA311_MOTION_INT_REG     0x09
#define MSA311_DATA_INT_REG       0x0A
#define MSA311_TAP_ACTIVE_STS_REG 0x0B
#define MSA311_ORIENT_STS_REG     0x0C
#define MSA311_RANGE_REG          0x0F
#define MSA311_ODR_REG            0x10
#define MSA311_PWR_MODE_REG       0x11
#define MSA311_SWAP_POLARITY_REG  0x12
#define MSA311_INT_SET_0_REG      0x16
#define MSA311_INT_SET_1_REG      0x17
#define MSA311_INT_MAP_0_REG      0x19
#define MSA311_INT_MAP_1_REG      0x1A
#define MSA311_INT_CONFIG_REG     0x20
#define MSA311_INT_LATCH_REG      0x21
#define MSA311_FREEFALL_DUR_REG   0x22
#define MSA311_FREEFALL_TH_REG    0x23
#define MSA311_FREEFALL_HY_REG    0x24
#define MSA311_ACTIVE_DUR_REG     0x27
#define MSA311_ACTIVE_TH_REG      0x28
#define MSA311_TAP_DUR_REG        0x2A
#define MSA311_TAP_TH_REG         0x2B
#define MSA311_ORIENT_HY_REG      0x2C
#define MSA311_Z_BLOCK_REG        0x2D
#define MSA311_OFFSET_X_REG       0x38
#define MSA311_OFFSET_Y_REG       0x39
#define MSA311_OFFSET_Z_REG       0x3A

enum msa311_fields {
	/* Soft_Reset */
	F_SOFT_RESET_I2C, F_SOFT_RESET_SPI,
	/* Motion_Interrupt */
	F_ORIENT_INT, F_S_TAP_INT, F_D_TAP_INT, F_ACTIVE_INT, F_FREEFALL_INT,
	/* Data_Interrupt */
	F_NEW_DATA_INT,
	/* Tap_Active_Status */
	F_TAP_SIGN, F_TAP_FIRST_X, F_TAP_FIRST_Y, F_TAP_FIRST_Z, F_ACTV_SIGN,
	F_ACTV_FIRST_X, F_ACTV_FIRST_Y, F_ACTV_FIRST_Z,
	/* Orientation_Status */
	F_ORIENT_Z, F_ORIENT_X_Y,
	/* Range */
	F_FS,
	/* ODR */
	F_X_AXIS_DIS, F_Y_AXIS_DIS, F_Z_AXIS_DIS, F_ODR,
	/* Power Mode/Bandwidth */
	F_PWR_MODE, F_LOW_POWER_BW,
	/* Swap_Polarity */
	F_X_POLARITY, F_Y_POLARITY, F_Z_POLARITY, F_X_Y_SWAP,
	/* Int_Set_0 */
	F_ORIENT_INT_EN, F_S_TAP_INT_EN, F_D_TAP_INT_EN, F_ACTIVE_INT_EN_Z,
	F_ACTIVE_INT_EN_Y, F_ACTIVE_INT_EN_X,
	/* Int_Set_1 */
	F_NEW_DATA_INT_EN, F_FREEFALL_INT_EN,
	/* Int_Map_0 */
	F_INT1_ORIENT, F_INT1_S_TAP, F_INT1_D_TAP, F_INT1_ACTIVE,
	F_INT1_FREEFALL,
	/* Int_Map_1 */
	F_INT1_NEW_DATA,
	/* Int_Config */
	F_INT1_OD, F_INT1_LVL,
	/* Int_Latch */
	F_RESET_INT, F_LATCH_INT,
	/* Freefall_Hy */
	F_FREEFALL_MODE, F_FREEFALL_HY,
	/* Active_Dur */
	F_ACTIVE_DUR,
	/* Tap_Dur */
	F_TAP_QUIET, F_TAP_SHOCK, F_TAP_DUR,
	/* Tap_Th */
	F_TAP_TH,
	/* Orient_Hy */
	F_ORIENT_HYST, F_ORIENT_BLOCKING, F_ORIENT_MODE,
	/* Z_Block */
	F_Z_BLOCKING,
	/* End of register map */
	F_MAX_FIELDS,
};

static const struct reg_field msa311_reg_fields[] = {
	/* Soft_Reset */
	[F_SOFT_RESET_I2C] = REG_FIELD(MSA311_SOFT_RESET_REG, 2, 2),
	[F_SOFT_RESET_SPI] = REG_FIELD(MSA311_SOFT_RESET_REG, 5, 5),
	/* Motion_Interrupt */
	[F_ORIENT_INT] = REG_FIELD(MSA311_MOTION_INT_REG, 6, 6),
	[F_S_TAP_INT] = REG_FIELD(MSA311_MOTION_INT_REG, 5, 5),
	[F_D_TAP_INT] = REG_FIELD(MSA311_MOTION_INT_REG, 4, 4),
	[F_ACTIVE_INT] = REG_FIELD(MSA311_MOTION_INT_REG, 2, 2),
	[F_FREEFALL_INT] = REG_FIELD(MSA311_MOTION_INT_REG, 0, 0),
	/* Data_Interrupt */
	[F_NEW_DATA_INT] = REG_FIELD(MSA311_DATA_INT_REG, 0, 0),
	/* Tap_Active_Status */
	[F_TAP_SIGN] = REG_FIELD(MSA311_TAP_ACTIVE_STS_REG, 7, 7),
	[F_TAP_FIRST_X] = REG_FIELD(MSA311_TAP_ACTIVE_STS_REG, 6, 6),
	[F_TAP_FIRST_Y] = REG_FIELD(MSA311_TAP_ACTIVE_STS_REG, 5, 5),
	[F_TAP_FIRST_Z] = REG_FIELD(MSA311_TAP_ACTIVE_STS_REG, 4, 4),
	[F_ACTV_SIGN] = REG_FIELD(MSA311_TAP_ACTIVE_STS_REG, 3, 3),
	[F_ACTV_FIRST_X] = REG_FIELD(MSA311_TAP_ACTIVE_STS_REG, 2, 2),
	[F_ACTV_FIRST_Y] = REG_FIELD(MSA311_TAP_ACTIVE_STS_REG, 1, 1),
	[F_ACTV_FIRST_Z] = REG_FIELD(MSA311_TAP_ACTIVE_STS_REG, 0, 0),
	/* Orientation_Status */
	[F_ORIENT_Z] = REG_FIELD(MSA311_ORIENT_STS_REG, 6, 6),
	[F_ORIENT_X_Y] = REG_FIELD(MSA311_ORIENT_STS_REG, 4, 5),
	/* Range */
	[F_FS] = REG_FIELD(MSA311_RANGE_REG, 0, 1),
	/* ODR */
	[F_X_AXIS_DIS] = REG_FIELD(MSA311_ODR_REG, 7, 7),
	[F_Y_AXIS_DIS] = REG_FIELD(MSA311_ODR_REG, 6, 6),
	[F_Z_AXIS_DIS] = REG_FIELD(MSA311_ODR_REG, 5, 5),
	[F_ODR] = REG_FIELD(MSA311_ODR_REG, 0, 3),
	/* Power Mode/Bandwidth */
	[F_PWR_MODE] = REG_FIELD(MSA311_PWR_MODE_REG, 6, 7),
	[F_LOW_POWER_BW] = REG_FIELD(MSA311_PWR_MODE_REG, 1, 4),
	/* Swap_Polarity */
	[F_X_POLARITY] = REG_FIELD(MSA311_SWAP_POLARITY_REG, 3, 3),
	[F_Y_POLARITY] = REG_FIELD(MSA311_SWAP_POLARITY_REG, 2, 2),
	[F_Z_POLARITY] = REG_FIELD(MSA311_SWAP_POLARITY_REG, 1, 1),
	[F_X_Y_SWAP] = REG_FIELD(MSA311_SWAP_POLARITY_REG, 0, 0),
	/* Int_Set_0 */
	[F_ORIENT_INT_EN] = REG_FIELD(MSA311_INT_SET_0_REG, 6, 6),
	[F_S_TAP_INT_EN] = REG_FIELD(MSA311_INT_SET_0_REG, 5, 5),
	[F_D_TAP_INT_EN] = REG_FIELD(MSA311_INT_SET_0_REG, 4, 4),
	[F_ACTIVE_INT_EN_Z] = REG_FIELD(MSA311_INT_SET_0_REG, 2, 2),
	[F_ACTIVE_INT_EN_Y] = REG_FIELD(MSA311_INT_SET_0_REG, 1, 1),
	[F_ACTIVE_INT_EN_X] = REG_FIELD(MSA311_INT_SET_0_REG, 0, 0),
	/* Int_Set_1 */
	[F_NEW_DATA_INT_EN] = REG_FIELD(MSA311_INT_SET_1_REG, 4, 4),
	[F_FREEFALL_INT_EN] = REG_FIELD(MSA311_INT_SET_1_REG, 3, 3),
	/* Int_Map_0 */
	[F_INT1_ORIENT] = REG_FIELD(MSA311_INT_MAP_0_REG, 6, 6),
	[F_INT1_S_TAP] = REG_FIELD(MSA311_INT_MAP_0_REG, 5, 5),
	[F_INT1_D_TAP] = REG_FIELD(MSA311_INT_MAP_0_REG, 4, 4),
	[F_INT1_ACTIVE] = REG_FIELD(MSA311_INT_MAP_0_REG, 2, 2),
	[F_INT1_FREEFALL] = REG_FIELD(MSA311_INT_MAP_0_REG, 0, 0),
	/* Int_Map_1 */
	[F_INT1_NEW_DATA] = REG_FIELD(MSA311_INT_MAP_1_REG, 0, 0),
	/* Int_Config */
	[F_INT1_OD] = REG_FIELD(MSA311_INT_CONFIG_REG, 1, 1),
	[F_INT1_LVL] = REG_FIELD(MSA311_INT_CONFIG_REG, 0, 0),
	/* Int_Latch */
	[F_RESET_INT] = REG_FIELD(MSA311_INT_LATCH_REG, 7, 7),
	[F_LATCH_INT] = REG_FIELD(MSA311_INT_LATCH_REG, 0, 3),
	/* Freefall_Hy */
	[F_FREEFALL_MODE] = REG_FIELD(MSA311_FREEFALL_HY_REG, 2, 2),
	[F_FREEFALL_HY] = REG_FIELD(MSA311_FREEFALL_HY_REG, 0, 1),
	/* Active_Dur */
	[F_ACTIVE_DUR] = REG_FIELD(MSA311_ACTIVE_DUR_REG, 0, 1),
	/* Tap_Dur */
	[F_TAP_QUIET] = REG_FIELD(MSA311_TAP_DUR_REG, 7, 7),
	[F_TAP_SHOCK] = REG_FIELD(MSA311_TAP_DUR_REG, 6, 6),
	[F_TAP_DUR] = REG_FIELD(MSA311_TAP_DUR_REG, 0, 2),
	/* Tap_Th */
	[F_TAP_TH] = REG_FIELD(MSA311_TAP_TH_REG, 0, 4),
	/* Orient_Hy */
	[F_ORIENT_HYST] = REG_FIELD(MSA311_ORIENT_HY_REG, 4, 6),
	[F_ORIENT_BLOCKING] = REG_FIELD(MSA311_ORIENT_HY_REG, 2, 3),
	[F_ORIENT_MODE] = REG_FIELD(MSA311_ORIENT_HY_REG, 0, 1),
	/* Z_Block */
	[F_Z_BLOCKING] = REG_FIELD(MSA311_Z_BLOCK_REG, 0, 3),
};

#define MSA311_WHO_AM_I 0x13

/*
 * Possible Full Scale ranges
 *
 * Axis data is 12-bit signed value, so
 *
 * fs0 = (2 + 2) * 9.81 / (2^11) = 0.009580
 * fs1 = (4 + 4) * 9.81 / (2^11) = 0.019160
 * fs2 = (8 + 8) * 9.81 / (2^11) = 0.038320
 * fs3 = (16 + 16) * 9.81 / (2^11) = 0.076641
 */
enum {
	MSA311_FS_2G,
	MSA311_FS_4G,
	MSA311_FS_8G,
	MSA311_FS_16G,
};

struct iio_decimal_fract {
	int integral;
	int microfract;
};

static const struct iio_decimal_fract msa311_fs_table[] = {
	{0, 9580}, {0, 19160}, {0, 38320}, {0, 76641},
};

/* Possible Output Data Rate values */
enum {
	MSA311_ODR_1_HZ,
	MSA311_ODR_1_95_HZ,
	MSA311_ODR_3_9_HZ,
	MSA311_ODR_7_81_HZ,
	MSA311_ODR_15_63_HZ,
	MSA311_ODR_31_25_HZ,
	MSA311_ODR_62_5_HZ,
	MSA311_ODR_125_HZ,
	MSA311_ODR_250_HZ,
	MSA311_ODR_500_HZ,
	MSA311_ODR_1000_HZ,
};

static const struct iio_decimal_fract msa311_odr_table[] = {
	{1, 0}, {1, 950000}, {3, 900000}, {7, 810000}, {15, 630000},
	{31, 250000}, {62, 500000}, {125, 0}, {250, 0}, {500, 0}, {1000, 0},
};

/* All supported power modes */
#define MSA311_PWR_MODE_NORMAL  0b00
#define MSA311_PWR_MODE_LOW     0b01
#define MSA311_PWR_MODE_UNKNOWN 0b10
#define MSA311_PWR_MODE_SUSPEND 0b11
static const char * const msa311_pwr_modes[] = {
	[MSA311_PWR_MODE_NORMAL] = "normal",
	[MSA311_PWR_MODE_LOW] = "low",
	[MSA311_PWR_MODE_UNKNOWN] = "unknown",
	[MSA311_PWR_MODE_SUSPEND] = "suspend",
};

/* Autosuspend delay */
#define MSA311_PWR_SLEEP_DELAY_MS 2000

/* Possible INT1 types and levels */
enum {
	MSA311_INT1_OD_PUSH_PULL,
	MSA311_INT1_OD_OPEN_DRAIN,
};

enum {
	MSA311_INT1_LVL_LOW,
	MSA311_INT1_LVL_HIGH,
};

/* Latch INT modes */
#define MSA311_LATCH_INT_NOT_LATCHED 0b0000
#define MSA311_LATCH_INT_250MS       0b0001
#define MSA311_LATCH_INT_500MS       0b0010
#define MSA311_LATCH_INT_1S          0b0011
#define MSA311_LATCH_INT_2S          0b0100
#define MSA311_LATCH_INT_4S          0b0101
#define MSA311_LATCH_INT_8S          0b0110
#define MSA311_LATCH_INT_1MS         0b1010
#define MSA311_LATCH_INT_2MS         0b1011
#define MSA311_LATCH_INT_25MS        0b1100
#define MSA311_LATCH_INT_50MS        0b1101
#define MSA311_LATCH_INT_100MS       0b1110
#define MSA311_LATCH_INT_LATCHED     0b0111

static const struct regmap_range msa311_readonly_registers[] = {
	regmap_reg_range(MSA311_PARTID_REG, MSA311_ORIENT_STS_REG),
};

static const struct regmap_access_table msa311_writeable_table = {
	.no_ranges = msa311_readonly_registers,
	.n_no_ranges = ARRAY_SIZE(msa311_readonly_registers),
};

static const struct regmap_range msa311_writeonly_registers[] = {
	regmap_reg_range(MSA311_SOFT_RESET_REG, MSA311_SOFT_RESET_REG),
};

static const struct regmap_access_table msa311_readable_table = {
	.no_ranges = msa311_writeonly_registers,
	.n_no_ranges = ARRAY_SIZE(msa311_writeonly_registers),
};

static const struct regmap_range msa311_volatile_registers[] = {
	regmap_reg_range(MSA311_ACC_X_REG, MSA311_ORIENT_STS_REG),
};

static const struct regmap_access_table msa311_volatile_table = {
	.yes_ranges = msa311_volatile_registers,
	.n_yes_ranges = ARRAY_SIZE(msa311_volatile_registers),
};

static const struct regmap_config msa311_regmap_config = {
	.name = "msa311",
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MSA311_OFFSET_Z_REG,
	.wr_table = &msa311_writeable_table,
	.rd_table = &msa311_readable_table,
	.volatile_table = &msa311_volatile_table,
	.cache_type = REGCACHE_RBTREE,
};

#define MSA311_GENMASK(field) ({                \
	typeof(&(msa311_reg_fields)[0]) _field; \
	_field = &msa311_reg_fields[(field)];   \
	GENMASK(_field->msb, _field->lsb);      \
})

/**
 * struct msa311_priv - MSA311 internal private state
 * @regs: Underlying I2C bus adapter used to abstract slave
 *        register accesses
 * @fields: Abstract objects for each registers fields access
 * @dev: Device handler associated with appropriate bus client
 * @lock: Protects msa311 device state between setup and data access routines
 *        (power transitions, samp_freq/scale tune, retrieving axes data, etc)
 * @chip_name: Chip name in the format "msa311-%02x" % partid
 * @new_data_trig: Optional NEW_DATA interrupt driven trigger used
 *                 to notify external consumers a new sample is ready
 */
struct msa311_priv {
	struct regmap *regs;
	struct regmap_field *fields[F_MAX_FIELDS];

	struct device *dev;
	struct mutex lock;
	char *chip_name;

	struct iio_trigger *new_data_trig;
};

enum msa311_si {
	MSA311_SI_X,
	MSA311_SI_Y,
	MSA311_SI_Z,
	MSA311_SI_TIMESTAMP,
};

#define MSA311_ACCEL_CHANNEL(axis) {                                        \
	.type = IIO_ACCEL,                                                  \
	.modified = 1,                                                      \
	.channel2 = IIO_MOD_##axis,                                         \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),                       \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |              \
				    BIT(IIO_CHAN_INFO_SAMP_FREQ),           \
	.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SCALE) |    \
					      BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.scan_index = MSA311_SI_##axis,                                     \
	.scan_type = {                                                      \
		.sign = 's',                                                \
		.realbits = 12,                                             \
		.storagebits = 16,                                          \
		.shift = 4,                                                 \
		.endianness = IIO_LE,                                       \
	},                                                                  \
	.datasheet_name = "ACC_"#axis,                                      \
}

static const struct iio_chan_spec msa311_channels[] = {
	MSA311_ACCEL_CHANNEL(X),
	MSA311_ACCEL_CHANNEL(Y),
	MSA311_ACCEL_CHANNEL(Z),
	IIO_CHAN_SOFT_TIMESTAMP(MSA311_SI_TIMESTAMP),
};

/**
 * msa311_get_odr() - Read Output Data Rate (ODR) value from MSA311 accel
 * @msa311: MSA311 internal private state
 * @odr: output ODR value
 *
 * This function should be called under msa311->lock.
 *
 * Return: 0 on success, -ERRNO in other failures
 */
static int msa311_get_odr(struct msa311_priv *msa311, unsigned int *odr)
{
	int err;

	err = regmap_field_read(msa311->fields[F_ODR], odr);
	if (err)
		return err;

	/*
	 * Filter the same 1000Hz ODR register values based on datasheet info.
	 * ODR can be equal to 1010-1111 for 1000Hz, but function returns 1010
	 * all the time.
	 */
	if (*odr > MSA311_ODR_1000_HZ)
		*odr = MSA311_ODR_1000_HZ;

	return 0;
}

/**
 * msa311_set_odr() - Setup Output Data Rate (ODR) value for MSA311 accel
 * @msa311: MSA311 internal private state
 * @odr: requested ODR value
 *
 * This function should be called under msa311->lock. Possible ODR values:
 *     - 1Hz (not available in normal mode)
 *     - 1.95Hz (not available in normal mode)
 *     - 3.9Hz
 *     - 7.81Hz
 *     - 15.63Hz
 *     - 31.25Hz
 *     - 62.5Hz
 *     - 125Hz
 *     - 250Hz
 *     - 500Hz
 *     - 1000Hz
 *
 * Return: 0 on success, -EINVAL for bad ODR value in the certain power mode,
 *         -ERRNO in other failures
 */
static int msa311_set_odr(struct msa311_priv *msa311, unsigned int odr)
{
	struct device *dev = msa311->dev;
	unsigned int pwr_mode;
	bool good_odr;
	int err;

	err = regmap_field_read(msa311->fields[F_PWR_MODE], &pwr_mode);
	if (err)
		return err;

	/* Filter bad ODR values */
	if (pwr_mode == MSA311_PWR_MODE_NORMAL)
		good_odr = (odr > MSA311_ODR_1_95_HZ);
	else
		good_odr = false;

	if (!good_odr) {
		dev_err(dev,
			"can't set odr %u.%06uHz, not available in %s mode\n",
			msa311_odr_table[odr].integral,
			msa311_odr_table[odr].microfract,
			msa311_pwr_modes[pwr_mode]);
		return -EINVAL;
	}

	return regmap_field_write(msa311->fields[F_ODR], odr);
}

/**
 * msa311_wait_for_next_data() - Wait next accel data available after resume
 * @msa311: MSA311 internal private state
 *
 * Return: 0 on success, -EINTR if msleep() was interrupted,
 *         -ERRNO in other failures
 */
static int msa311_wait_for_next_data(struct msa311_priv *msa311)
{
	static const unsigned int unintr_thresh_ms = 20;
	struct device *dev = msa311->dev;
	unsigned long freq_uhz;
	unsigned long wait_ms;
	unsigned int odr;
	int err;

	err = msa311_get_odr(msa311, &odr);
	if (err) {
		dev_err(dev, "can't get actual frequency (%pe)\n",
			ERR_PTR(err));
		return err;
	}

	/*
	 * After msa311 resuming is done, we need to wait for data
	 * to be refreshed by accel logic.
	 * A certain timeout is calculated based on the current ODR value.
	 * If requested timeout isn't so long (let's assume 20ms),
	 * we can wait for next data in uninterruptible sleep.
	 */
	freq_uhz = msa311_odr_table[odr].integral * MICROHZ_PER_HZ +
		   msa311_odr_table[odr].microfract;
	wait_ms = (MICROHZ_PER_HZ / freq_uhz) * MSEC_PER_SEC;

	if (wait_ms < unintr_thresh_ms)
		usleep_range(wait_ms * USEC_PER_MSEC,
			     unintr_thresh_ms * USEC_PER_MSEC);
	else if (msleep_interruptible(wait_ms))
		return -EINTR;

	return 0;
}

/**
 * msa311_set_pwr_mode() - Install certain MSA311 power mode
 * @msa311: MSA311 internal private state
 * @mode: Power mode can be equal to NORMAL or SUSPEND
 *
 * This function should be called under msa311->lock.
 *
 * Return: 0 on success, -ERRNO on failure
 */
static int msa311_set_pwr_mode(struct msa311_priv *msa311, unsigned int mode)
{
	struct device *dev = msa311->dev;
	unsigned int prev_mode;
	int err;

	if (mode >= ARRAY_SIZE(msa311_pwr_modes))
		return -EINVAL;

	dev_dbg(dev, "transition to %s mode\n", msa311_pwr_modes[mode]);

	err = regmap_field_read(msa311->fields[F_PWR_MODE], &prev_mode);
	if (err)
		return err;

	err = regmap_field_write(msa311->fields[F_PWR_MODE], mode);
	if (err)
		return err;

	/* Wait actual data if we wake up */
	if (prev_mode == MSA311_PWR_MODE_SUSPEND &&
	    mode == MSA311_PWR_MODE_NORMAL)
		return msa311_wait_for_next_data(msa311);

	return 0;
}

/**
 * msa311_get_axis() - Read MSA311 accel data for certain IIO channel axis spec
 * @msa311: MSA311 internal private state
 * @chan: IIO channel specification
 * @axis: Output accel axis data for requested IIO channel spec
 *
 * This function should be called under msa311->lock.
 *
 * Return: 0 on success, -EINVAL for unknown IIO channel specification,
 *         -ERRNO in other failures
 */
static int msa311_get_axis(struct msa311_priv *msa311,
			   const struct iio_chan_spec * const chan,
			   __le16 *axis)
{
	struct device *dev = msa311->dev;
	unsigned int axis_reg;

	if (chan->scan_index < MSA311_SI_X || chan->scan_index > MSA311_SI_Z) {
		dev_err(dev, "invalid scan_index value [%d]\n",
			chan->scan_index);
		return -EINVAL;
	}

	/* Axes data layout has 2 byte gap for each axis starting from X axis */
	axis_reg = MSA311_ACC_X_REG + (chan->scan_index << 1);

	return regmap_bulk_read(msa311->regs, axis_reg, axis, sizeof(*axis));
}

static int msa311_read_raw_data(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2)
{
	struct msa311_priv *msa311 = iio_priv(indio_dev);
	struct device *dev = msa311->dev;
	__le16 axis;
	int err;

	err = pm_runtime_resume_and_get(dev);
	if (err)
		return err;

	err = iio_device_claim_direct_mode(indio_dev);
	if (err)
		return err;

	mutex_lock(&msa311->lock);
	err = msa311_get_axis(msa311, chan, &axis);
	mutex_unlock(&msa311->lock);

	iio_device_release_direct_mode(indio_dev);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	if (err) {
		dev_err(dev, "can't get axis %s (%pe)\n",
			chan->datasheet_name, ERR_PTR(err));
		return err;
	}

	/*
	 * Axis data format is:
	 * ACC_X = (ACC_X_MSB[7:0] << 4) | ACC_X_LSB[7:4]
	 */
	*val = sign_extend32(le16_to_cpu(axis) >> chan->scan_type.shift,
			     chan->scan_type.realbits - 1);

	return IIO_VAL_INT;
}

static int msa311_read_scale(struct iio_dev *indio_dev, int *val, int *val2)
{
	struct msa311_priv *msa311 = iio_priv(indio_dev);
	struct device *dev = msa311->dev;
	unsigned int fs;
	int err;

	mutex_lock(&msa311->lock);
	err = regmap_field_read(msa311->fields[F_FS], &fs);
	mutex_unlock(&msa311->lock);
	if (err) {
		dev_err(dev, "can't get actual scale (%pe)\n", ERR_PTR(err));
		return err;
	}

	*val = msa311_fs_table[fs].integral;
	*val2 = msa311_fs_table[fs].microfract;

	return IIO_VAL_INT_PLUS_MICRO;
}

static int msa311_read_samp_freq(struct iio_dev *indio_dev,
				 int *val, int *val2)
{
	struct msa311_priv *msa311 = iio_priv(indio_dev);
	struct device *dev = msa311->dev;
	unsigned int odr;
	int err;

	mutex_lock(&msa311->lock);
	err = msa311_get_odr(msa311, &odr);
	mutex_unlock(&msa311->lock);
	if (err) {
		dev_err(dev, "can't get actual frequency (%pe)\n",
			ERR_PTR(err));
		return err;
	}

	*val = msa311_odr_table[odr].integral;
	*val2 = msa311_odr_table[odr].microfract;

	return IIO_VAL_INT_PLUS_MICRO;
}

static int msa311_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return msa311_read_raw_data(indio_dev, chan, val, val2);

	case IIO_CHAN_INFO_SCALE:
		return msa311_read_scale(indio_dev, val, val2);

	case IIO_CHAN_INFO_SAMP_FREQ:
		return msa311_read_samp_freq(indio_dev, val, val2);

	default:
		return -EINVAL;
	}
}

static int msa311_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type,
			     int *length, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = (int *)msa311_odr_table;
		*type = IIO_VAL_INT_PLUS_MICRO;
		/* ODR value has 2 ints (integer and fractional parts) */
		*length = ARRAY_SIZE(msa311_odr_table) * 2;
		return IIO_AVAIL_LIST;

	case IIO_CHAN_INFO_SCALE:
		*vals = (int *)msa311_fs_table;
		*type = IIO_VAL_INT_PLUS_MICRO;
		/* FS value has 2 ints (integer and fractional parts) */
		*length = ARRAY_SIZE(msa311_fs_table) * 2;
		return IIO_AVAIL_LIST;

	default:
		return -EINVAL;
	}
}

static int msa311_write_scale(struct iio_dev *indio_dev, int val, int val2)
{
	struct msa311_priv *msa311 = iio_priv(indio_dev);
	struct device *dev = msa311->dev;
	unsigned int fs;
	int err;

	/* We do not have fs >= 1, so skip such values */
	if (val)
		return 0;

	err = pm_runtime_resume_and_get(dev);
	if (err)
		return err;

	err = -EINVAL;
	for (fs = 0; fs < ARRAY_SIZE(msa311_fs_table); fs++)
		/* Do not check msa311_fs_table[fs].integral, it's always 0 */
		if (val2 == msa311_fs_table[fs].microfract) {
			mutex_lock(&msa311->lock);
			err = regmap_field_write(msa311->fields[F_FS], fs);
			mutex_unlock(&msa311->lock);
			break;
		}

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	if (err)
		dev_err(dev, "can't update scale (%pe)\n", ERR_PTR(err));

	return err;
}

static int msa311_write_samp_freq(struct iio_dev *indio_dev, int val, int val2)
{
	struct msa311_priv *msa311 = iio_priv(indio_dev);
	struct device *dev = msa311->dev;
	unsigned int odr;
	int err;

	err = pm_runtime_resume_and_get(dev);
	if (err)
		return err;

	/*
	 * Sampling frequency changing is prohibited when buffer mode is
	 * enabled, because sometimes MSA311 chip returns outliers during
	 * frequency values growing up in the read operation moment.
	 */
	err = iio_device_claim_direct_mode(indio_dev);
	if (err)
		return err;

	err = -EINVAL;
	for (odr = 0; odr < ARRAY_SIZE(msa311_odr_table); odr++)
		if (val == msa311_odr_table[odr].integral &&
		    val2 == msa311_odr_table[odr].microfract) {
			mutex_lock(&msa311->lock);
			err = msa311_set_odr(msa311, odr);
			mutex_unlock(&msa311->lock);
			break;
		}

	iio_device_release_direct_mode(indio_dev);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	if (err)
		dev_err(dev, "can't update frequency (%pe)\n", ERR_PTR(err));

	return err;
}

static int msa311_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return msa311_write_scale(indio_dev, val, val2);

	case IIO_CHAN_INFO_SAMP_FREQ:
		return msa311_write_samp_freq(indio_dev, val, val2);

	default:
		return -EINVAL;
	}
}

static int msa311_debugfs_reg_access(struct iio_dev *indio_dev,
				     unsigned int reg, unsigned int writeval,
				     unsigned int *readval)
{
	struct msa311_priv *msa311 = iio_priv(indio_dev);
	struct device *dev = msa311->dev;
	int err;

	if (reg > regmap_get_max_register(msa311->regs))
		return -EINVAL;

	err = pm_runtime_resume_and_get(dev);
	if (err)
		return err;

	mutex_lock(&msa311->lock);

	if (readval)
		err = regmap_read(msa311->regs, reg, readval);
	else
		err = regmap_write(msa311->regs, reg, writeval);

	mutex_unlock(&msa311->lock);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	if (err)
		dev_err(dev, "can't %s register %u from debugfs (%pe)\n",
			str_read_write(readval), reg, ERR_PTR(err));

	return err;
}

static int msa311_buffer_preenable(struct iio_dev *indio_dev)
{
	struct msa311_priv *msa311 = iio_priv(indio_dev);
	struct device *dev = msa311->dev;

	return pm_runtime_resume_and_get(dev);
}

static int msa311_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct msa311_priv *msa311 = iio_priv(indio_dev);
	struct device *dev = msa311->dev;

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;
}

static int msa311_set_new_data_trig_state(struct iio_trigger *trig, bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct msa311_priv *msa311 = iio_priv(indio_dev);
	struct device *dev = msa311->dev;
	int err;

	mutex_lock(&msa311->lock);
	err = regmap_field_write(msa311->fields[F_NEW_DATA_INT_EN], state);
	mutex_unlock(&msa311->lock);
	if (err)
		dev_err(dev,
			"can't %s buffer due to new_data_int failure (%pe)\n",
			str_enable_disable(state), ERR_PTR(err));

	return err;
}

static int msa311_validate_device(struct iio_trigger *trig,
				  struct iio_dev *indio_dev)
{
	return iio_trigger_get_drvdata(trig) == indio_dev ? 0 : -EINVAL;
}

static irqreturn_t msa311_buffer_thread(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct msa311_priv *msa311 = iio_priv(pf->indio_dev);
	struct iio_dev *indio_dev = pf->indio_dev;
	const struct iio_chan_spec *chan;
	struct device *dev = msa311->dev;
	int bit, err, i = 0;
	__le16 axis;
	struct {
		__le16 channels[MSA311_SI_Z + 1];
		s64 ts __aligned(8);
	} buf;

	memset(&buf, 0, sizeof(buf));

	mutex_lock(&msa311->lock);

	for_each_set_bit(bit, indio_dev->active_scan_mask,
			 indio_dev->masklength) {
		chan = &msa311_channels[bit];

		err = msa311_get_axis(msa311, chan, &axis);
		if (err) {
			mutex_unlock(&msa311->lock);
			dev_err(dev, "can't get axis %s (%pe)\n",
				chan->datasheet_name, ERR_PTR(err));
			goto notify_done;
		}

		buf.channels[i++] = axis;
	}

	mutex_unlock(&msa311->lock);

	iio_push_to_buffers_with_timestamp(indio_dev, &buf,
					   iio_get_time_ns(indio_dev));

notify_done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static irqreturn_t msa311_irq_thread(int irq, void *p)
{
	struct msa311_priv *msa311 = iio_priv(p);
	unsigned int new_data_int_enabled;
	struct device *dev = msa311->dev;
	int err;

	mutex_lock(&msa311->lock);

	/*
	 * We do not check NEW_DATA int status, because based on the
	 * specification it's cleared automatically after a fixed time.
	 * So just check that is enabled by driver logic.
	 */
	err = regmap_field_read(msa311->fields[F_NEW_DATA_INT_EN],
				&new_data_int_enabled);

	mutex_unlock(&msa311->lock);
	if (err) {
		dev_err(dev, "can't read new_data interrupt state (%pe)\n",
			ERR_PTR(err));
		return IRQ_NONE;
	}

	if (new_data_int_enabled)
		iio_trigger_poll_nested(msa311->new_data_trig);

	return IRQ_HANDLED;
}

static const struct iio_info msa311_info = {
	.read_raw = msa311_read_raw,
	.read_avail = msa311_read_avail,
	.write_raw = msa311_write_raw,
	.debugfs_reg_access = msa311_debugfs_reg_access,
};

static const struct iio_buffer_setup_ops msa311_buffer_setup_ops = {
	.preenable = msa311_buffer_preenable,
	.postdisable = msa311_buffer_postdisable,
};

static const struct iio_trigger_ops msa311_new_data_trig_ops = {
	.set_trigger_state = msa311_set_new_data_trig_state,
	.validate_device = msa311_validate_device,
};

static int msa311_check_partid(struct msa311_priv *msa311)
{
	struct device *dev = msa311->dev;
	unsigned int partid;
	int err;

	err = regmap_read(msa311->regs, MSA311_PARTID_REG, &partid);
	if (err)
		return dev_err_probe(dev, err, "failed to read partid\n");

	if (partid != MSA311_WHO_AM_I)
		dev_warn(dev, "invalid partid (%#x), expected (%#x)\n",
			 partid, MSA311_WHO_AM_I);

	msa311->chip_name = devm_kasprintf(dev, GFP_KERNEL,
					   "msa311-%02x", partid);
	if (!msa311->chip_name)
		return dev_err_probe(dev, -ENOMEM, "can't alloc chip name\n");

	return 0;
}

static int msa311_soft_reset(struct msa311_priv *msa311)
{
	struct device *dev = msa311->dev;
	int err;

	err = regmap_write(msa311->regs, MSA311_SOFT_RESET_REG,
			   MSA311_GENMASK(F_SOFT_RESET_I2C) |
			   MSA311_GENMASK(F_SOFT_RESET_SPI));
	if (err)
		return dev_err_probe(dev, err, "can't soft reset all logic\n");

	return 0;
}

static int msa311_chip_init(struct msa311_priv *msa311)
{
	struct device *dev = msa311->dev;
	const char zero_bulk[2] = { };
	int err;

	err = regmap_write(msa311->regs, MSA311_RANGE_REG, MSA311_FS_16G);
	if (err)
		return dev_err_probe(dev, err, "failed to setup accel range\n");

	/* Disable all interrupts by default */
	err = regmap_bulk_write(msa311->regs, MSA311_INT_SET_0_REG,
				zero_bulk, sizeof(zero_bulk));
	if (err)
		return dev_err_probe(dev, err,
				     "can't disable set0/set1 interrupts\n");

	/* Unmap all INT1 interrupts by default */
	err = regmap_bulk_write(msa311->regs, MSA311_INT_MAP_0_REG,
				zero_bulk, sizeof(zero_bulk));
	if (err)
		return dev_err_probe(dev, err,
				     "failed to unmap map0/map1 interrupts\n");

	/* Disable all axes by default */
	err = regmap_update_bits(msa311->regs, MSA311_ODR_REG,
				 MSA311_GENMASK(F_X_AXIS_DIS) |
				 MSA311_GENMASK(F_Y_AXIS_DIS) |
				 MSA311_GENMASK(F_Z_AXIS_DIS), 0);
	if (err)
		return dev_err_probe(dev, err, "can't enable all axes\n");

	err = msa311_set_odr(msa311, MSA311_ODR_125_HZ);
	if (err)
		return dev_err_probe(dev, err,
				     "failed to set accel frequency\n");

	return 0;
}

static int msa311_setup_interrupts(struct msa311_priv *msa311)
{
	struct device *dev = msa311->dev;
	struct i2c_client *i2c = to_i2c_client(dev);
	struct iio_dev *indio_dev = i2c_get_clientdata(i2c);
	struct iio_trigger *trig;
	int err;

	/* Keep going without interrupts if no initialized I2C IRQ */
	if (i2c->irq <= 0)
		return 0;

	err = devm_request_threaded_irq(&i2c->dev, i2c->irq, NULL,
					msa311_irq_thread, IRQF_ONESHOT,
					msa311->chip_name, indio_dev);
	if (err)
		return dev_err_probe(dev, err, "failed to request IRQ\n");

	trig = devm_iio_trigger_alloc(dev, "%s-new-data", msa311->chip_name);
	if (!trig)
		return dev_err_probe(dev, -ENOMEM,
				     "can't allocate newdata trigger\n");

	msa311->new_data_trig = trig;
	msa311->new_data_trig->ops = &msa311_new_data_trig_ops;
	iio_trigger_set_drvdata(msa311->new_data_trig, indio_dev);

	err = devm_iio_trigger_register(dev, msa311->new_data_trig);
	if (err)
		return dev_err_probe(dev, err,
				     "can't register newdata trigger\n");

	err = regmap_field_write(msa311->fields[F_INT1_OD],
				 MSA311_INT1_OD_PUSH_PULL);
	if (err)
		return dev_err_probe(dev, err,
				     "can't enable push-pull interrupt\n");

	err = regmap_field_write(msa311->fields[F_INT1_LVL],
				 MSA311_INT1_LVL_HIGH);
	if (err)
		return dev_err_probe(dev, err,
				     "can't set active interrupt level\n");

	err = regmap_field_write(msa311->fields[F_LATCH_INT],
				 MSA311_LATCH_INT_LATCHED);
	if (err)
		return dev_err_probe(dev, err,
				     "can't latch interrupt\n");

	err = regmap_field_write(msa311->fields[F_RESET_INT], 1);
	if (err)
		return dev_err_probe(dev, err,
				     "can't reset interrupt\n");

	err = regmap_field_write(msa311->fields[F_INT1_NEW_DATA], 1);
	if (err)
		return dev_err_probe(dev, err,
				     "can't map new data interrupt\n");

	return 0;
}

static int msa311_regmap_init(struct msa311_priv *msa311)
{
	struct regmap_field **fields = msa311->fields;
	struct device *dev = msa311->dev;
	struct i2c_client *i2c = to_i2c_client(dev);
	struct regmap *regmap;
	int i;

	regmap = devm_regmap_init_i2c(i2c, &msa311_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "failed to register i2c regmap\n");

	msa311->regs = regmap;

	for (i = 0; i < F_MAX_FIELDS; i++) {
		fields[i] = devm_regmap_field_alloc(dev,
						    msa311->regs,
						    msa311_reg_fields[i]);
		if (IS_ERR(msa311->fields[i]))
			return dev_err_probe(dev, PTR_ERR(msa311->fields[i]),
					     "can't alloc field[%d]\n", i);
	}

	return 0;
}

static void msa311_powerdown(void *msa311)
{
	msa311_set_pwr_mode(msa311, MSA311_PWR_MODE_SUSPEND);
}

static int msa311_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct msa311_priv *msa311;
	struct iio_dev *indio_dev;
	int err;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*msa311));
	if (!indio_dev)
		return dev_err_probe(dev, -ENOMEM,
				     "IIO device allocation failed\n");

	msa311 = iio_priv(indio_dev);
	msa311->dev = dev;
	i2c_set_clientdata(i2c, indio_dev);

	err = msa311_regmap_init(msa311);
	if (err)
		return err;

	mutex_init(&msa311->lock);

	err = devm_regulator_get_enable(dev, "vdd");
	if (err)
		return dev_err_probe(dev, err, "can't get vdd supply\n");

	err = msa311_check_partid(msa311);
	if (err)
		return err;

	err = msa311_soft_reset(msa311);
	if (err)
		return err;

	err = msa311_set_pwr_mode(msa311, MSA311_PWR_MODE_NORMAL);
	if (err)
		return dev_err_probe(dev, err, "failed to power on device\n");

	/*
	 * Register powerdown deferred callback which suspends the chip
	 * after module unloaded.
	 *
	 * MSA311 should be in SUSPEND mode in the two cases:
	 * 1) When driver is loaded, but we do not have any data or
	 *    configuration requests to it (we are solving it using
	 *    autosuspend feature).
	 * 2) When driver is unloaded and device is not used (devm action is
	 *    used in this case).
	 */
	err = devm_add_action_or_reset(dev, msa311_powerdown, msa311);
	if (err)
		return dev_err_probe(dev, err, "can't add powerdown action\n");

	err = pm_runtime_set_active(dev);
	if (err)
		return err;

	err = devm_pm_runtime_enable(dev);
	if (err)
		return err;

	pm_runtime_get_noresume(dev);
	pm_runtime_set_autosuspend_delay(dev, MSA311_PWR_SLEEP_DELAY_MS);
	pm_runtime_use_autosuspend(dev);

	err = msa311_chip_init(msa311);
	if (err)
		return err;

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = msa311_channels;
	indio_dev->num_channels = ARRAY_SIZE(msa311_channels);
	indio_dev->name = msa311->chip_name;
	indio_dev->info = &msa311_info;

	err = devm_iio_triggered_buffer_setup(dev, indio_dev,
					      iio_pollfunc_store_time,
					      msa311_buffer_thread,
					      &msa311_buffer_setup_ops);
	if (err)
		return dev_err_probe(dev, err,
				     "can't setup IIO trigger buffer\n");

	err = msa311_setup_interrupts(msa311);
	if (err)
		return err;

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	err = devm_iio_device_register(dev, indio_dev);
	if (err)
		return dev_err_probe(dev, err, "IIO device register failed\n");

	return 0;
}

static int msa311_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct msa311_priv *msa311 = iio_priv(indio_dev);
	int err;

	mutex_lock(&msa311->lock);
	err = msa311_set_pwr_mode(msa311, MSA311_PWR_MODE_SUSPEND);
	mutex_unlock(&msa311->lock);
	if (err)
		dev_err(dev, "failed to power off device (%pe)\n",
			ERR_PTR(err));

	return err;
}

static int msa311_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct msa311_priv *msa311 = iio_priv(indio_dev);
	int err;

	mutex_lock(&msa311->lock);
	err = msa311_set_pwr_mode(msa311, MSA311_PWR_MODE_NORMAL);
	mutex_unlock(&msa311->lock);
	if (err)
		dev_err(dev, "failed to power on device (%pe)\n",
			ERR_PTR(err));

	return err;
}

static DEFINE_RUNTIME_DEV_PM_OPS(msa311_pm_ops, msa311_runtime_suspend,
				 msa311_runtime_resume, NULL);

static const struct i2c_device_id msa311_i2c_id[] = {
	{ .name = "msa311" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, msa311_i2c_id);

static const struct of_device_id msa311_of_match[] = {
	{ .compatible = "memsensing,msa311" },
	{ }
};
MODULE_DEVICE_TABLE(of, msa311_of_match);

static struct i2c_driver msa311_driver = {
	.driver = {
		.name = "msa311",
		.of_match_table = msa311_of_match,
		.pm = pm_ptr(&msa311_pm_ops),
	},
	.probe_new	= msa311_probe,
	.id_table	= msa311_i2c_id,
};
module_i2c_driver(msa311_driver);

MODULE_AUTHOR("Dmitry Rokosov <ddrokosov@sberdevices.ru>");
MODULE_DESCRIPTION("MEMSensing MSA311 3-axis accelerometer driver");
MODULE_LICENSE("GPL");
