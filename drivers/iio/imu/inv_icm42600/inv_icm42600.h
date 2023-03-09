/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020 Invensense, Inc.
 */

#ifndef INV_ICM42600_H_
#define INV_ICM42600_H_

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/regmap.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <linux/pm.h>
#include <linux/iio/iio.h>

#include "inv_icm42600_buffer.h"

enum inv_icm42600_chip {
	INV_CHIP_INVALID,
	INV_CHIP_ICM42600,
	INV_CHIP_ICM42602,
	INV_CHIP_ICM42605,
	INV_CHIP_ICM42622,
	INV_CHIP_ICM42631,
	INV_CHIP_NB,
};

/* serial bus slew rates */
enum inv_icm42600_slew_rate {
	INV_ICM42600_SLEW_RATE_20_60NS,
	INV_ICM42600_SLEW_RATE_12_36NS,
	INV_ICM42600_SLEW_RATE_6_18NS,
	INV_ICM42600_SLEW_RATE_4_12NS,
	INV_ICM42600_SLEW_RATE_2_6NS,
	INV_ICM42600_SLEW_RATE_INF_2NS,
};

enum inv_icm42600_sensor_mode {
	INV_ICM42600_SENSOR_MODE_OFF,
	INV_ICM42600_SENSOR_MODE_STANDBY,
	INV_ICM42600_SENSOR_MODE_LOW_POWER,
	INV_ICM42600_SENSOR_MODE_LOW_NOISE,
	INV_ICM42600_SENSOR_MODE_NB,
};

/* gyroscope fullscale values */
enum inv_icm42600_gyro_fs {
	INV_ICM42600_GYRO_FS_2000DPS,
	INV_ICM42600_GYRO_FS_1000DPS,
	INV_ICM42600_GYRO_FS_500DPS,
	INV_ICM42600_GYRO_FS_250DPS,
	INV_ICM42600_GYRO_FS_125DPS,
	INV_ICM42600_GYRO_FS_62_5DPS,
	INV_ICM42600_GYRO_FS_31_25DPS,
	INV_ICM42600_GYRO_FS_15_625DPS,
	INV_ICM42600_GYRO_FS_NB,
};

/* accelerometer fullscale values */
enum inv_icm42600_accel_fs {
	INV_ICM42600_ACCEL_FS_16G,
	INV_ICM42600_ACCEL_FS_8G,
	INV_ICM42600_ACCEL_FS_4G,
	INV_ICM42600_ACCEL_FS_2G,
	INV_ICM42600_ACCEL_FS_NB,
};

/* ODR suffixed by LN or LP are Low-Noise or Low-Power mode only */
enum inv_icm42600_odr {
	INV_ICM42600_ODR_8KHZ_LN = 3,
	INV_ICM42600_ODR_4KHZ_LN,
	INV_ICM42600_ODR_2KHZ_LN,
	INV_ICM42600_ODR_1KHZ_LN,
	INV_ICM42600_ODR_200HZ,
	INV_ICM42600_ODR_100HZ,
	INV_ICM42600_ODR_50HZ,
	INV_ICM42600_ODR_25HZ,
	INV_ICM42600_ODR_12_5HZ,
	INV_ICM42600_ODR_6_25HZ_LP,
	INV_ICM42600_ODR_3_125HZ_LP,
	INV_ICM42600_ODR_1_5625HZ_LP,
	INV_ICM42600_ODR_500HZ,
	INV_ICM42600_ODR_NB,
};

enum inv_icm42600_filter {
	/* Low-Noise mode sensor data filter (3rd order filter by default) */
	INV_ICM42600_FILTER_BW_ODR_DIV_2,

	/* Low-Power mode sensor data filter (averaging) */
	INV_ICM42600_FILTER_AVG_1X = 1,
	INV_ICM42600_FILTER_AVG_16X = 6,
};

struct inv_icm42600_sensor_conf {
	int mode;
	int fs;
	int odr;
	int filter;
};
#define INV_ICM42600_SENSOR_CONF_INIT		{-1, -1, -1, -1}

struct inv_icm42600_conf {
	struct inv_icm42600_sensor_conf gyro;
	struct inv_icm42600_sensor_conf accel;
	bool temp_en;
};

struct inv_icm42600_suspended {
	enum inv_icm42600_sensor_mode gyro;
	enum inv_icm42600_sensor_mode accel;
	bool temp;
};

/**
 *  struct inv_icm42600_state - driver state variables
 *  @lock:		lock for serializing multiple registers access.
 *  @chip:		chip identifier.
 *  @name:		chip name.
 *  @map:		regmap pointer.
 *  @vdd_supply:	VDD voltage regulator for the chip.
 *  @vddio_supply:	I/O voltage regulator for the chip.
 *  @orientation:	sensor chip orientation relative to main hardware.
 *  @conf:		chip sensors configurations.
 *  @suspended:		suspended sensors configuration.
 *  @indio_gyro:	gyroscope IIO device.
 *  @indio_accel:	accelerometer IIO device.
 *  @buffer:		data transfer buffer aligned for DMA.
 *  @fifo:		FIFO management structure.
 *  @timestamp:		interrupt timestamps.
 */
struct inv_icm42600_state {
	struct mutex lock;
	enum inv_icm42600_chip chip;
	const char *name;
	struct regmap *map;
	struct regulator *vdd_supply;
	struct regulator *vddio_supply;
	struct iio_mount_matrix orientation;
	struct inv_icm42600_conf conf;
	struct inv_icm42600_suspended suspended;
	struct iio_dev *indio_gyro;
	struct iio_dev *indio_accel;
	uint8_t buffer[2] __aligned(IIO_DMA_MINALIGN);
	struct inv_icm42600_fifo fifo;
	struct {
		int64_t gyro;
		int64_t accel;
	} timestamp;
};

/* Virtual register addresses: @bank on MSB (4 upper bits), @address on LSB */

/* Bank selection register, available in all banks */
#define INV_ICM42600_REG_BANK_SEL			0x76
#define INV_ICM42600_BANK_SEL_MASK			GENMASK(2, 0)

/* User bank 0 (MSB 0x00) */
#define INV_ICM42600_REG_DEVICE_CONFIG			0x0011
#define INV_ICM42600_DEVICE_CONFIG_SOFT_RESET		BIT(0)

#define INV_ICM42600_REG_DRIVE_CONFIG			0x0013
#define INV_ICM42600_DRIVE_CONFIG_I2C_MASK		GENMASK(5, 3)
#define INV_ICM42600_DRIVE_CONFIG_I2C(_rate)		\
		FIELD_PREP(INV_ICM42600_DRIVE_CONFIG_I2C_MASK, (_rate))
#define INV_ICM42600_DRIVE_CONFIG_SPI_MASK		GENMASK(2, 0)
#define INV_ICM42600_DRIVE_CONFIG_SPI(_rate)		\
		FIELD_PREP(INV_ICM42600_DRIVE_CONFIG_SPI_MASK, (_rate))

#define INV_ICM42600_REG_INT_CONFIG			0x0014
#define INV_ICM42600_INT_CONFIG_INT2_LATCHED		BIT(5)
#define INV_ICM42600_INT_CONFIG_INT2_PUSH_PULL		BIT(4)
#define INV_ICM42600_INT_CONFIG_INT2_ACTIVE_HIGH	BIT(3)
#define INV_ICM42600_INT_CONFIG_INT2_ACTIVE_LOW		0x00
#define INV_ICM42600_INT_CONFIG_INT1_LATCHED		BIT(2)
#define INV_ICM42600_INT_CONFIG_INT1_PUSH_PULL		BIT(1)
#define INV_ICM42600_INT_CONFIG_INT1_ACTIVE_HIGH	BIT(0)
#define INV_ICM42600_INT_CONFIG_INT1_ACTIVE_LOW		0x00

#define INV_ICM42600_REG_FIFO_CONFIG			0x0016
#define INV_ICM42600_FIFO_CONFIG_MASK			GENMASK(7, 6)
#define INV_ICM42600_FIFO_CONFIG_BYPASS			\
		FIELD_PREP(INV_ICM42600_FIFO_CONFIG_MASK, 0)
#define INV_ICM42600_FIFO_CONFIG_STREAM			\
		FIELD_PREP(INV_ICM42600_FIFO_CONFIG_MASK, 1)
#define INV_ICM42600_FIFO_CONFIG_STOP_ON_FULL		\
		FIELD_PREP(INV_ICM42600_FIFO_CONFIG_MASK, 2)

/* all sensor data are 16 bits (2 registers wide) in big-endian */
#define INV_ICM42600_REG_TEMP_DATA			0x001D
#define INV_ICM42600_REG_ACCEL_DATA_X			0x001F
#define INV_ICM42600_REG_ACCEL_DATA_Y			0x0021
#define INV_ICM42600_REG_ACCEL_DATA_Z			0x0023
#define INV_ICM42600_REG_GYRO_DATA_X			0x0025
#define INV_ICM42600_REG_GYRO_DATA_Y			0x0027
#define INV_ICM42600_REG_GYRO_DATA_Z			0x0029
#define INV_ICM42600_DATA_INVALID			-32768

#define INV_ICM42600_REG_INT_STATUS			0x002D
#define INV_ICM42600_INT_STATUS_UI_FSYNC		BIT(6)
#define INV_ICM42600_INT_STATUS_PLL_RDY			BIT(5)
#define INV_ICM42600_INT_STATUS_RESET_DONE		BIT(4)
#define INV_ICM42600_INT_STATUS_DATA_RDY		BIT(3)
#define INV_ICM42600_INT_STATUS_FIFO_THS		BIT(2)
#define INV_ICM42600_INT_STATUS_FIFO_FULL		BIT(1)
#define INV_ICM42600_INT_STATUS_AGC_RDY			BIT(0)

/*
 * FIFO access registers
 * FIFO count is 16 bits (2 registers) big-endian
 * FIFO data is a continuous read register to read FIFO content
 */
#define INV_ICM42600_REG_FIFO_COUNT			0x002E
#define INV_ICM42600_REG_FIFO_DATA			0x0030

#define INV_ICM42600_REG_SIGNAL_PATH_RESET		0x004B
#define INV_ICM42600_SIGNAL_PATH_RESET_DMP_INIT_EN	BIT(6)
#define INV_ICM42600_SIGNAL_PATH_RESET_DMP_MEM_RESET	BIT(5)
#define INV_ICM42600_SIGNAL_PATH_RESET_RESET		BIT(3)
#define INV_ICM42600_SIGNAL_PATH_RESET_TMST_STROBE	BIT(2)
#define INV_ICM42600_SIGNAL_PATH_RESET_FIFO_FLUSH	BIT(1)

/* default configuration: all data big-endian and fifo count in bytes */
#define INV_ICM42600_REG_INTF_CONFIG0			0x004C
#define INV_ICM42600_INTF_CONFIG0_FIFO_HOLD_LAST_DATA	BIT(7)
#define INV_ICM42600_INTF_CONFIG0_FIFO_COUNT_REC	BIT(6)
#define INV_ICM42600_INTF_CONFIG0_FIFO_COUNT_ENDIAN	BIT(5)
#define INV_ICM42600_INTF_CONFIG0_SENSOR_DATA_ENDIAN	BIT(4)
#define INV_ICM42600_INTF_CONFIG0_UI_SIFS_CFG_MASK	GENMASK(1, 0)
#define INV_ICM42600_INTF_CONFIG0_UI_SIFS_CFG_SPI_DIS	\
		FIELD_PREP(INV_ICM42600_INTF_CONFIG0_UI_SIFS_CFG_MASK, 2)
#define INV_ICM42600_INTF_CONFIG0_UI_SIFS_CFG_I2C_DIS	\
		FIELD_PREP(INV_ICM42600_INTF_CONFIG0_UI_SIFS_CFG_MASK, 3)

#define INV_ICM42600_REG_INTF_CONFIG1			0x004D
#define INV_ICM42600_INTF_CONFIG1_ACCEL_LP_CLK_RC	BIT(3)

#define INV_ICM42600_REG_PWR_MGMT0			0x004E
#define INV_ICM42600_PWR_MGMT0_TEMP_DIS			BIT(5)
#define INV_ICM42600_PWR_MGMT0_IDLE			BIT(4)
#define INV_ICM42600_PWR_MGMT0_GYRO(_mode)		\
		FIELD_PREP(GENMASK(3, 2), (_mode))
#define INV_ICM42600_PWR_MGMT0_ACCEL(_mode)		\
		FIELD_PREP(GENMASK(1, 0), (_mode))

#define INV_ICM42600_REG_GYRO_CONFIG0			0x004F
#define INV_ICM42600_GYRO_CONFIG0_FS(_fs)		\
		FIELD_PREP(GENMASK(7, 5), (_fs))
#define INV_ICM42600_GYRO_CONFIG0_ODR(_odr)		\
		FIELD_PREP(GENMASK(3, 0), (_odr))

#define INV_ICM42600_REG_ACCEL_CONFIG0			0x0050
#define INV_ICM42600_ACCEL_CONFIG0_FS(_fs)		\
		FIELD_PREP(GENMASK(7, 5), (_fs))
#define INV_ICM42600_ACCEL_CONFIG0_ODR(_odr)		\
		FIELD_PREP(GENMASK(3, 0), (_odr))

#define INV_ICM42600_REG_GYRO_ACCEL_CONFIG0		0x0052
#define INV_ICM42600_GYRO_ACCEL_CONFIG0_ACCEL_FILT(_f)	\
		FIELD_PREP(GENMASK(7, 4), (_f))
#define INV_ICM42600_GYRO_ACCEL_CONFIG0_GYRO_FILT(_f)	\
		FIELD_PREP(GENMASK(3, 0), (_f))

#define INV_ICM42600_REG_TMST_CONFIG			0x0054
#define INV_ICM42600_TMST_CONFIG_MASK			GENMASK(4, 0)
#define INV_ICM42600_TMST_CONFIG_TMST_TO_REGS_EN	BIT(4)
#define INV_ICM42600_TMST_CONFIG_TMST_RES_16US		BIT(3)
#define INV_ICM42600_TMST_CONFIG_TMST_DELTA_EN		BIT(2)
#define INV_ICM42600_TMST_CONFIG_TMST_FSYNC_EN		BIT(1)
#define INV_ICM42600_TMST_CONFIG_TMST_EN		BIT(0)

#define INV_ICM42600_REG_FIFO_CONFIG1			0x005F
#define INV_ICM42600_FIFO_CONFIG1_RESUME_PARTIAL_RD	BIT(6)
#define INV_ICM42600_FIFO_CONFIG1_WM_GT_TH		BIT(5)
#define INV_ICM42600_FIFO_CONFIG1_TMST_FSYNC_EN		BIT(3)
#define INV_ICM42600_FIFO_CONFIG1_TEMP_EN		BIT(2)
#define INV_ICM42600_FIFO_CONFIG1_GYRO_EN		BIT(1)
#define INV_ICM42600_FIFO_CONFIG1_ACCEL_EN		BIT(0)

/* FIFO watermark is 16 bits (2 registers wide) in little-endian */
#define INV_ICM42600_REG_FIFO_WATERMARK			0x0060
#define INV_ICM42600_FIFO_WATERMARK_VAL(_wm)		\
		cpu_to_le16((_wm) & GENMASK(11, 0))
/* FIFO is 2048 bytes, let 12 samples for reading latency */
#define INV_ICM42600_FIFO_WATERMARK_MAX			(2048 - 12 * 16)

#define INV_ICM42600_REG_INT_CONFIG1			0x0064
#define INV_ICM42600_INT_CONFIG1_TPULSE_DURATION	BIT(6)
#define INV_ICM42600_INT_CONFIG1_TDEASSERT_DISABLE	BIT(5)
#define INV_ICM42600_INT_CONFIG1_ASYNC_RESET		BIT(4)

#define INV_ICM42600_REG_INT_SOURCE0			0x0065
#define INV_ICM42600_INT_SOURCE0_UI_FSYNC_INT1_EN	BIT(6)
#define INV_ICM42600_INT_SOURCE0_PLL_RDY_INT1_EN	BIT(5)
#define INV_ICM42600_INT_SOURCE0_RESET_DONE_INT1_EN	BIT(4)
#define INV_ICM42600_INT_SOURCE0_UI_DRDY_INT1_EN	BIT(3)
#define INV_ICM42600_INT_SOURCE0_FIFO_THS_INT1_EN	BIT(2)
#define INV_ICM42600_INT_SOURCE0_FIFO_FULL_INT1_EN	BIT(1)
#define INV_ICM42600_INT_SOURCE0_UI_AGC_RDY_INT1_EN	BIT(0)

#define INV_ICM42600_REG_WHOAMI				0x0075
#define INV_ICM42600_WHOAMI_ICM42600			0x40
#define INV_ICM42600_WHOAMI_ICM42602			0x41
#define INV_ICM42600_WHOAMI_ICM42605			0x42
#define INV_ICM42600_WHOAMI_ICM42622			0x46
#define INV_ICM42600_WHOAMI_ICM42631			0x5C

/* User bank 1 (MSB 0x10) */
#define INV_ICM42600_REG_SENSOR_CONFIG0			0x1003
#define INV_ICM42600_SENSOR_CONFIG0_ZG_DISABLE		BIT(5)
#define INV_ICM42600_SENSOR_CONFIG0_YG_DISABLE		BIT(4)
#define INV_ICM42600_SENSOR_CONFIG0_XG_DISABLE		BIT(3)
#define INV_ICM42600_SENSOR_CONFIG0_ZA_DISABLE		BIT(2)
#define INV_ICM42600_SENSOR_CONFIG0_YA_DISABLE		BIT(1)
#define INV_ICM42600_SENSOR_CONFIG0_XA_DISABLE		BIT(0)

/* Timestamp value is 20 bits (3 registers) in little-endian */
#define INV_ICM42600_REG_TMSTVAL			0x1062
#define INV_ICM42600_TMSTVAL_MASK			GENMASK(19, 0)

#define INV_ICM42600_REG_INTF_CONFIG4			0x107A
#define INV_ICM42600_INTF_CONFIG4_I3C_BUS_ONLY		BIT(6)
#define INV_ICM42600_INTF_CONFIG4_SPI_AP_4WIRE		BIT(1)

#define INV_ICM42600_REG_INTF_CONFIG6			0x107C
#define INV_ICM42600_INTF_CONFIG6_MASK			GENMASK(4, 0)
#define INV_ICM42600_INTF_CONFIG6_I3C_EN		BIT(4)
#define INV_ICM42600_INTF_CONFIG6_I3C_IBI_BYTE_EN	BIT(3)
#define INV_ICM42600_INTF_CONFIG6_I3C_IBI_EN		BIT(2)
#define INV_ICM42600_INTF_CONFIG6_I3C_DDR_EN		BIT(1)
#define INV_ICM42600_INTF_CONFIG6_I3C_SDR_EN		BIT(0)

/* User bank 4 (MSB 0x40) */
#define INV_ICM42600_REG_INT_SOURCE8			0x404F
#define INV_ICM42600_INT_SOURCE8_FSYNC_IBI_EN		BIT(5)
#define INV_ICM42600_INT_SOURCE8_PLL_RDY_IBI_EN		BIT(4)
#define INV_ICM42600_INT_SOURCE8_UI_DRDY_IBI_EN		BIT(3)
#define INV_ICM42600_INT_SOURCE8_FIFO_THS_IBI_EN	BIT(2)
#define INV_ICM42600_INT_SOURCE8_FIFO_FULL_IBI_EN	BIT(1)
#define INV_ICM42600_INT_SOURCE8_AGC_RDY_IBI_EN		BIT(0)

#define INV_ICM42600_REG_OFFSET_USER0			0x4077
#define INV_ICM42600_REG_OFFSET_USER1			0x4078
#define INV_ICM42600_REG_OFFSET_USER2			0x4079
#define INV_ICM42600_REG_OFFSET_USER3			0x407A
#define INV_ICM42600_REG_OFFSET_USER4			0x407B
#define INV_ICM42600_REG_OFFSET_USER5			0x407C
#define INV_ICM42600_REG_OFFSET_USER6			0x407D
#define INV_ICM42600_REG_OFFSET_USER7			0x407E
#define INV_ICM42600_REG_OFFSET_USER8			0x407F

/* Sleep times required by the driver */
#define INV_ICM42600_POWER_UP_TIME_MS		100
#define INV_ICM42600_RESET_TIME_MS		1
#define INV_ICM42600_ACCEL_STARTUP_TIME_MS	20
#define INV_ICM42600_GYRO_STARTUP_TIME_MS	60
#define INV_ICM42600_GYRO_STOP_TIME_MS		150
#define INV_ICM42600_TEMP_STARTUP_TIME_MS	14
#define INV_ICM42600_SUSPEND_DELAY_MS		2000

typedef int (*inv_icm42600_bus_setup)(struct inv_icm42600_state *);

extern const struct regmap_config inv_icm42600_regmap_config;
extern const struct dev_pm_ops inv_icm42600_pm_ops;

const struct iio_mount_matrix *
inv_icm42600_get_mount_matrix(const struct iio_dev *indio_dev,
			      const struct iio_chan_spec *chan);

uint32_t inv_icm42600_odr_to_period(enum inv_icm42600_odr odr);

int inv_icm42600_set_accel_conf(struct inv_icm42600_state *st,
				struct inv_icm42600_sensor_conf *conf,
				unsigned int *sleep_ms);

int inv_icm42600_set_gyro_conf(struct inv_icm42600_state *st,
			       struct inv_icm42600_sensor_conf *conf,
			       unsigned int *sleep_ms);

int inv_icm42600_set_temp_conf(struct inv_icm42600_state *st, bool enable,
			       unsigned int *sleep_ms);

int inv_icm42600_debugfs_reg(struct iio_dev *indio_dev, unsigned int reg,
			     unsigned int writeval, unsigned int *readval);

int inv_icm42600_core_probe(struct regmap *regmap, int chip, int irq,
			    inv_icm42600_bus_setup bus_setup);

struct iio_dev *inv_icm42600_gyro_init(struct inv_icm42600_state *st);

int inv_icm42600_gyro_parse_fifo(struct iio_dev *indio_dev);

struct iio_dev *inv_icm42600_accel_init(struct inv_icm42600_state *st);

int inv_icm42600_accel_parse_fifo(struct iio_dev *indio_dev);

#endif
