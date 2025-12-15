/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2025 Invensense, Inc. */

#ifndef INV_ICM45600_H_
#define INV_ICM45600_H_

#include <linux/bits.h>
#include <linux/limits.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/sizes.h>
#include <linux/types.h>

#include <linux/iio/common/inv_sensors_timestamp.h>
#include <linux/iio/iio.h>

#include "inv_icm45600_buffer.h"

#define INV_ICM45600_REG_BANK_MASK	GENMASK(15, 8)
#define INV_ICM45600_REG_ADDR_MASK	GENMASK(7, 0)

enum inv_icm45600_sensor_mode {
	INV_ICM45600_SENSOR_MODE_OFF,
	INV_ICM45600_SENSOR_MODE_STANDBY,
	INV_ICM45600_SENSOR_MODE_LOW_POWER,
	INV_ICM45600_SENSOR_MODE_LOW_NOISE,
	INV_ICM45600_SENSOR_MODE_MAX
};

/* gyroscope fullscale values */
enum inv_icm45600_gyro_fs {
	INV_ICM45600_GYRO_FS_2000DPS,
	INV_ICM45600_GYRO_FS_1000DPS,
	INV_ICM45600_GYRO_FS_500DPS,
	INV_ICM45600_GYRO_FS_250DPS,
	INV_ICM45600_GYRO_FS_125DPS,
	INV_ICM45600_GYRO_FS_62_5DPS,
	INV_ICM45600_GYRO_FS_31_25DPS,
	INV_ICM45600_GYRO_FS_15_625DPS,
	INV_ICM45600_GYRO_FS_MAX
};

enum inv_icm45686_gyro_fs {
	INV_ICM45686_GYRO_FS_4000DPS,
	INV_ICM45686_GYRO_FS_2000DPS,
	INV_ICM45686_GYRO_FS_1000DPS,
	INV_ICM45686_GYRO_FS_500DPS,
	INV_ICM45686_GYRO_FS_250DPS,
	INV_ICM45686_GYRO_FS_125DPS,
	INV_ICM45686_GYRO_FS_62_5DPS,
	INV_ICM45686_GYRO_FS_31_25DPS,
	INV_ICM45686_GYRO_FS_15_625DPS,
	INV_ICM45686_GYRO_FS_MAX
};

/* accelerometer fullscale values */
enum inv_icm45600_accel_fs {
	INV_ICM45600_ACCEL_FS_16G,
	INV_ICM45600_ACCEL_FS_8G,
	INV_ICM45600_ACCEL_FS_4G,
	INV_ICM45600_ACCEL_FS_2G,
	INV_ICM45600_ACCEL_FS_MAX
};

enum inv_icm45686_accel_fs {
	INV_ICM45686_ACCEL_FS_32G,
	INV_ICM45686_ACCEL_FS_16G,
	INV_ICM45686_ACCEL_FS_8G,
	INV_ICM45686_ACCEL_FS_4G,
	INV_ICM45686_ACCEL_FS_2G,
	INV_ICM45686_ACCEL_FS_MAX
};

/* ODR suffixed by LN or LP are Low-Noise or Low-Power mode only */
enum inv_icm45600_odr {
	INV_ICM45600_ODR_6400HZ_LN = 0x03,
	INV_ICM45600_ODR_3200HZ_LN,
	INV_ICM45600_ODR_1600HZ_LN,
	INV_ICM45600_ODR_800HZ_LN,
	INV_ICM45600_ODR_400HZ,
	INV_ICM45600_ODR_200HZ,
	INV_ICM45600_ODR_100HZ,
	INV_ICM45600_ODR_50HZ,
	INV_ICM45600_ODR_25HZ,
	INV_ICM45600_ODR_12_5HZ,
	INV_ICM45600_ODR_6_25HZ_LP,
	INV_ICM45600_ODR_3_125HZ_LP,
	INV_ICM45600_ODR_1_5625HZ_LP,
	INV_ICM45600_ODR_MAX
};

struct inv_icm45600_sensor_conf {
	u8 mode;
	u8 fs;
	u8 odr;
	u8 filter;
};

#define INV_ICM45600_SENSOR_CONF_KEEP_VALUES { U8_MAX, U8_MAX, U8_MAX, U8_MAX }

struct inv_icm45600_conf {
	struct inv_icm45600_sensor_conf gyro;
	struct inv_icm45600_sensor_conf accel;
};

struct inv_icm45600_suspended {
	enum inv_icm45600_sensor_mode gyro;
	enum inv_icm45600_sensor_mode accel;
};

struct inv_icm45600_chip_info {
	u8 whoami;
	const char *name;
	const struct inv_icm45600_conf *conf;
	const int *accel_scales;
	const int accel_scales_len;
	const int *gyro_scales;
	const int gyro_scales_len;
};

extern const struct inv_icm45600_chip_info inv_icm45605_chip_info;
extern const struct inv_icm45600_chip_info inv_icm45606_chip_info;
extern const struct inv_icm45600_chip_info inv_icm45608_chip_info;
extern const struct inv_icm45600_chip_info inv_icm45634_chip_info;
extern const struct inv_icm45600_chip_info inv_icm45686_chip_info;
extern const struct inv_icm45600_chip_info inv_icm45687_chip_info;
extern const struct inv_icm45600_chip_info inv_icm45688p_chip_info;
extern const struct inv_icm45600_chip_info inv_icm45689_chip_info;

extern const int inv_icm45600_accel_scale[][2];
extern const int inv_icm45686_accel_scale[][2];
extern const int inv_icm45600_gyro_scale[][2];
extern const int inv_icm45686_gyro_scale[][2];

/**
 *  struct inv_icm45600_state - driver state variables
 *  @lock:		lock for serializing multiple registers access.
 *  @map:		regmap pointer.
 *  @vddio_supply:	I/O voltage regulator for the chip.
 *  @orientation:	sensor chip orientation relative to main hardware.
 *  @conf:		chip sensors configurations.
 *  @suspended:		suspended sensors configuration.
 *  @indio_gyro:	gyroscope IIO device.
 *  @indio_accel:	accelerometer IIO device.
 *  @chip_info:		chip driver data.
 *  @timestamp:		interrupt timestamps.
 *  @fifo:		FIFO management structure.
 *  @buffer:		data transfer buffer aligned for DMA.
 */
struct inv_icm45600_state {
	struct mutex lock;
	struct regmap *map;
	struct regulator *vddio_supply;
	struct iio_mount_matrix orientation;
	struct inv_icm45600_conf conf;
	struct inv_icm45600_suspended suspended;
	struct iio_dev *indio_gyro;
	struct iio_dev *indio_accel;
	const struct inv_icm45600_chip_info *chip_info;
	struct {
		s64 gyro;
		s64 accel;
	} timestamp;
	struct inv_icm45600_fifo fifo;
	union {
		u8 buff[2];
		__le16 u16;
		u8 ireg[3];
	} buffer __aligned(IIO_DMA_MINALIGN);
};

/**
 * struct inv_icm45600_sensor_state - sensor state variables
 * @scales:		table of scales.
 * @scales_len:		length (nb of items) of the scales table.
 * @power_mode:		sensor requested power mode (for common frequencies)
 * @ts:			timestamp module states.
 */
struct inv_icm45600_sensor_state {
	const int *scales;
	size_t scales_len;
	enum inv_icm45600_sensor_mode power_mode;
	struct inv_sensors_timestamp ts;
};

/* Virtual register addresses: @bank on MSB (16 bits), @address on LSB */

/* Indirect register access */
#define INV_ICM45600_REG_IREG_ADDR			0x7C
#define INV_ICM45600_REG_IREG_DATA			0x7E

/* Direct acces registers */
#define INV_ICM45600_REG_MISC2				0x007F
#define INV_ICM45600_MISC2_SOFT_RESET			BIT(1)

#define INV_ICM45600_REG_DRIVE_CONFIG0			0x0032
#define INV_ICM45600_DRIVE_CONFIG0_SPI_MASK		GENMASK(3, 1)
#define INV_ICM45600_SPI_SLEW_RATE_0_5NS		6
#define INV_ICM45600_SPI_SLEW_RATE_4NS			5
#define INV_ICM45600_SPI_SLEW_RATE_5NS			4
#define INV_ICM45600_SPI_SLEW_RATE_7NS			3
#define INV_ICM45600_SPI_SLEW_RATE_10NS			2
#define INV_ICM45600_SPI_SLEW_RATE_14NS			1
#define INV_ICM45600_SPI_SLEW_RATE_38NS			0

#define INV_ICM45600_REG_INT1_CONFIG2			0x0018
#define INV_ICM45600_INT1_CONFIG2_PUSH_PULL		BIT(2)
#define INV_ICM45600_INT1_CONFIG2_LATCHED		BIT(1)
#define INV_ICM45600_INT1_CONFIG2_ACTIVE_HIGH		BIT(0)
#define INV_ICM45600_INT1_CONFIG2_ACTIVE_LOW		0x00

#define INV_ICM45600_REG_FIFO_CONFIG0			0x001D
#define INV_ICM45600_FIFO_CONFIG0_MODE_MASK		GENMASK(7, 6)
#define INV_ICM45600_FIFO_CONFIG0_MODE_BYPASS		0
#define INV_ICM45600_FIFO_CONFIG0_MODE_STREAM		1
#define INV_ICM45600_FIFO_CONFIG0_MODE_STOP_ON_FULL	2
#define INV_ICM45600_FIFO_CONFIG0_FIFO_DEPTH_MASK	GENMASK(5, 0)
#define INV_ICM45600_FIFO_CONFIG0_FIFO_DEPTH_MAX	0x1F

#define INV_ICM45600_REG_FIFO_CONFIG2			0x0020
#define INV_ICM45600_REG_FIFO_CONFIG2_FIFO_FLUSH	BIT(7)
#define INV_ICM45600_REG_FIFO_CONFIG2_WM_GT_TH		BIT(3)

#define INV_ICM45600_REG_FIFO_CONFIG3			0x0021
#define INV_ICM45600_FIFO_CONFIG3_ES1_EN		BIT(5)
#define INV_ICM45600_FIFO_CONFIG3_ES0_EN		BIT(4)
#define INV_ICM45600_FIFO_CONFIG3_HIRES_EN		BIT(3)
#define INV_ICM45600_FIFO_CONFIG3_GYRO_EN		BIT(2)
#define INV_ICM45600_FIFO_CONFIG3_ACCEL_EN		BIT(1)
#define INV_ICM45600_FIFO_CONFIG3_IF_EN			BIT(0)

#define INV_ICM45600_REG_FIFO_CONFIG4			0x0022
#define INV_ICM45600_FIFO_CONFIG4_COMP_EN		BIT(2)
#define INV_ICM45600_FIFO_CONFIG4_TMST_FSYNC_EN		BIT(1)
#define INV_ICM45600_FIFO_CONFIG4_ES0_9B		BIT(0)

/* all sensor data are 16 bits (2 registers wide) in big-endian */
#define INV_ICM45600_REG_TEMP_DATA			0x000C
#define INV_ICM45600_REG_ACCEL_DATA_X			0x0000
#define INV_ICM45600_REG_ACCEL_DATA_Y			0x0002
#define INV_ICM45600_REG_ACCEL_DATA_Z			0x0004
#define INV_ICM45600_REG_GYRO_DATA_X			0x0006
#define INV_ICM45600_REG_GYRO_DATA_Y			0x0008
#define INV_ICM45600_REG_GYRO_DATA_Z			0x000A

#define INV_ICM45600_REG_INT_STATUS			0x0019
#define INV_ICM45600_INT_STATUS_RESET_DONE		BIT(7)
#define INV_ICM45600_INT_STATUS_AUX1_AGC_RDY		BIT(6)
#define INV_ICM45600_INT_STATUS_AP_AGC_RDY		BIT(5)
#define INV_ICM45600_INT_STATUS_AP_FSYNC		BIT(4)
#define INV_ICM45600_INT_STATUS_AUX1_DRDY		BIT(3)
#define INV_ICM45600_INT_STATUS_DATA_RDY		BIT(2)
#define INV_ICM45600_INT_STATUS_FIFO_THS		BIT(1)
#define INV_ICM45600_INT_STATUS_FIFO_FULL		BIT(0)

/*
 * FIFO access registers
 * FIFO count is 16 bits (2 registers)
 * FIFO data is a continuous read register to read FIFO content
 */
#define INV_ICM45600_REG_FIFO_COUNT			0x0012
#define INV_ICM45600_REG_FIFO_DATA			0x0014

#define INV_ICM45600_REG_PWR_MGMT0			0x0010
#define INV_ICM45600_PWR_MGMT0_GYRO_MODE_MASK		GENMASK(3, 2)
#define INV_ICM45600_PWR_MGMT0_ACCEL_MODE_MASK		GENMASK(1, 0)

#define INV_ICM45600_REG_ACCEL_CONFIG0			0x001B
#define INV_ICM45600_ACCEL_CONFIG0_FS_MASK		GENMASK(6, 4)
#define INV_ICM45600_ACCEL_CONFIG0_ODR_MASK		GENMASK(3, 0)
#define INV_ICM45600_REG_GYRO_CONFIG0			0x001C
#define INV_ICM45600_GYRO_CONFIG0_FS_MASK		GENMASK(7, 4)
#define INV_ICM45600_GYRO_CONFIG0_ODR_MASK		GENMASK(3, 0)

#define INV_ICM45600_REG_SMC_CONTROL_0			0xA258
#define INV_ICM45600_SMC_CONTROL_0_ACCEL_LP_CLK_SEL	BIT(4)
#define INV_ICM45600_SMC_CONTROL_0_TMST_EN		BIT(0)

/* FIFO watermark is 16 bits (2 registers wide) in little-endian */
#define INV_ICM45600_REG_FIFO_WATERMARK			0x001E

/* FIFO is configured for 8kb */
#define INV_ICM45600_FIFO_SIZE_MAX			SZ_8K

#define INV_ICM45600_REG_INT1_CONFIG0			0x0016
#define INV_ICM45600_INT1_CONFIG0_RESET_DONE_EN		BIT(7)
#define INV_ICM45600_INT1_CONFIG0_AUX1_AGC_RDY_EN	BIT(6)
#define INV_ICM45600_INT1_CONFIG0_AP_AGC_RDY_EN		BIT(5)
#define INV_ICM45600_INT1_CONFIG0_AP_FSYNC_EN		BIT(4)
#define INV_ICM45600_INT1_CONFIG0_AUX1_DRDY_EN		BIT(3)
#define INV_ICM45600_INT1_CONFIG0_DRDY_EN		BIT(2)
#define INV_ICM45600_INT1_CONFIG0_FIFO_THS_EN		BIT(1)
#define INV_ICM45600_INT1_CONFIG0_FIFO_FULL_EN		BIT(0)

#define INV_ICM45600_REG_WHOAMI				0x0072
#define INV_ICM45600_WHOAMI_ICM45605			0xE5
#define INV_ICM45600_WHOAMI_ICM45686			0xE9
#define INV_ICM45600_WHOAMI_ICM45688P			0xE7
#define INV_ICM45600_WHOAMI_ICM45608			0x81
#define INV_ICM45600_WHOAMI_ICM45634			0x82
#define INV_ICM45600_WHOAMI_ICM45689			0x83
#define INV_ICM45600_WHOAMI_ICM45606			0x84
#define INV_ICM45600_WHOAMI_ICM45687			0x85

/* Gyro USER offset */
#define INV_ICM45600_IPREG_SYS1_REG_42			0xA42A
#define INV_ICM45600_IPREG_SYS1_REG_56			0xA438
#define INV_ICM45600_IPREG_SYS1_REG_70			0xA446
#define INV_ICM45600_GYRO_OFFUSER_MASK			GENMASK(13, 0)
/* Gyro Averaging filter */
#define INV_ICM45600_IPREG_SYS1_REG_170			0xA4AA
#define INV_ICM45600_IPREG_SYS1_170_GYRO_LP_AVG_MASK	GENMASK(4, 1)
#define INV_ICM45600_GYRO_LP_AVG_SEL_8X			5
#define INV_ICM45600_GYRO_LP_AVG_SEL_2X			1
/* Accel USER offset */
#define INV_ICM45600_IPREG_SYS2_REG_24			0xA518
#define INV_ICM45600_IPREG_SYS2_REG_32			0xA520
#define INV_ICM45600_IPREG_SYS2_REG_40			0xA528
#define INV_ICM45600_ACCEL_OFFUSER_MASK			GENMASK(13, 0)
/* Accel averaging filter */
#define INV_ICM45600_IPREG_SYS2_REG_129			0xA581
#define INV_ICM45600_ACCEL_LP_AVG_SEL_1X		0x0000
#define INV_ICM45600_ACCEL_LP_AVG_SEL_4X		0x0002

/* Sleep times required by the driver */
#define INV_ICM45600_ACCEL_STARTUP_TIME_MS	60
#define INV_ICM45600_GYRO_STARTUP_TIME_MS	60
#define INV_ICM45600_GYRO_STOP_TIME_MS		150
#define INV_ICM45600_IREG_DELAY_US		4

typedef int (*inv_icm45600_bus_setup)(struct inv_icm45600_state *);

extern const struct dev_pm_ops inv_icm45600_pm_ops;

const struct iio_mount_matrix *
inv_icm45600_get_mount_matrix(const struct iio_dev *indio_dev,
			      const struct iio_chan_spec *chan);

#define INV_ICM45600_TEMP_CHAN(_index)					\
	{								\
		.type = IIO_TEMP,					\
		.info_mask_separate =					\
			BIT(IIO_CHAN_INFO_RAW) |			\
			BIT(IIO_CHAN_INFO_OFFSET) |			\
			BIT(IIO_CHAN_INFO_SCALE),			\
		.scan_index = _index,					\
		.scan_type = {						\
			.sign = 's',					\
			.realbits = 16,					\
			.storagebits = 16,				\
			.endianness = IIO_LE,				\
		},							\
	}

int inv_icm45600_temp_read_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int *val, int *val2, long mask);

u32 inv_icm45600_odr_to_period(enum inv_icm45600_odr odr);

int inv_icm45600_set_accel_conf(struct inv_icm45600_state *st,
				struct inv_icm45600_sensor_conf *conf,
				unsigned int *sleep_ms);

int inv_icm45600_set_gyro_conf(struct inv_icm45600_state *st,
			       struct inv_icm45600_sensor_conf *conf,
			       unsigned int *sleep_ms);

int inv_icm45600_debugfs_reg(struct iio_dev *indio_dev, unsigned int reg,
			     unsigned int writeval, unsigned int *readval);

int inv_icm45600_core_probe(struct regmap *regmap,
				const struct inv_icm45600_chip_info *chip_info,
				bool reset, inv_icm45600_bus_setup bus_setup);

struct iio_dev *inv_icm45600_gyro_init(struct inv_icm45600_state *st);

int inv_icm45600_gyro_parse_fifo(struct iio_dev *indio_dev);

struct iio_dev *inv_icm45600_accel_init(struct inv_icm45600_state *st);

int inv_icm45600_accel_parse_fifo(struct iio_dev *indio_dev);

#endif
