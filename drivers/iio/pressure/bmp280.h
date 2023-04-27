/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/iio/iio.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>


/* BMP580 specific registers */
#define BMP580_REG_CMD			0x7E
#define BMP580_REG_EFF_OSR		0x38
#define BMP580_REG_ODR_CONFIG		0x37
#define BMP580_REG_OSR_CONFIG		0x36
#define BMP580_REG_IF_CONFIG		0x13
#define BMP580_REG_REV_ID		0x02
#define BMP580_REG_CHIP_ID		0x01
/* OOR allows to configure a pressure alarm */
#define BMP580_REG_OOR_CONFIG		0x35
#define BMP580_REG_OOR_RANGE		0x34
#define BMP580_REG_OOR_THR_MSB		0x33
#define BMP580_REG_OOR_THR_LSB		0x32
/* DSP registers (IIR filters) */
#define BMP580_REG_DSP_IIR		0x31
#define BMP580_REG_DSP_CONFIG		0x30
/* NVM access registers */
#define BMP580_REG_NVM_DATA_MSB		0x2D
#define BMP580_REG_NVM_DATA_LSB		0x2C
#define BMP580_REG_NVM_ADDR		0x2B
/* Status registers */
#define BMP580_REG_STATUS		0x28
#define BMP580_REG_INT_STATUS		0x27
#define BMP580_REG_CHIP_STATUS		0x11
/* Data registers */
#define BMP580_REG_FIFO_DATA		0x29
#define BMP580_REG_PRESS_MSB		0x22
#define BMP580_REG_PRESS_LSB		0x21
#define BMP580_REG_PRESS_XLSB		0x20
#define BMP580_REG_TEMP_MSB		0x1F
#define BMP580_REG_TEMP_LSB		0x1E
#define BMP580_REG_TEMP_XLSB		0x1D
/* FIFO config registers */
#define BMP580_REG_FIFO_SEL		0x18
#define BMP580_REG_FIFO_COUNT		0x17
#define BMP580_REG_FIFO_CONFIG		0x16
/* Interruptions config registers */
#define BMP580_REG_INT_SOURCE		0x15
#define BMP580_REG_INT_CONFIG		0x14

#define BMP580_CMD_NOOP			0x00
#define BMP580_CMD_EXTMODE_SEQ_0	0x73
#define BMP580_CMD_EXTMODE_SEQ_1	0xB4
#define BMP580_CMD_EXTMODE_SEQ_2	0x69
#define BMP580_CMD_NVM_OP_SEQ_0		0x5D
#define BMP580_CMD_NVM_READ_SEQ_1	0xA5
#define BMP580_CMD_NVM_WRITE_SEQ_1	0xA0
#define BMP580_CMD_SOFT_RESET		0xB6

#define BMP580_INT_STATUS_POR_MASK	BIT(4)

#define BMP580_STATUS_CORE_RDY_MASK	BIT(0)
#define BMP580_STATUS_NVM_RDY_MASK	BIT(1)
#define BMP580_STATUS_NVM_ERR_MASK	BIT(2)
#define BMP580_STATUS_NVM_CMD_ERR_MASK	BIT(3)

#define BMP580_OSR_PRESS_MASK		GENMASK(5, 3)
#define BMP580_OSR_TEMP_MASK		GENMASK(2, 0)
#define BMP580_OSR_PRESS_EN		BIT(6)
#define BMP580_EFF_OSR_PRESS_MASK	GENMASK(5, 3)
#define BMP580_EFF_OSR_TEMP_MASK	GENMASK(2, 0)
#define BMP580_EFF_OSR_VALID_ODR	BIT(7)

#define BMP580_ODR_MASK			GENMASK(6, 2)
#define BMP580_MODE_MASK		GENMASK(1, 0)
#define BMP580_MODE_SLEEP		0
#define BMP580_MODE_NORMAL		1
#define BMP580_MODE_FORCED		2
#define BMP580_MODE_CONTINOUS		3
#define BMP580_ODR_DEEPSLEEP_DIS	BIT(7)

#define BMP580_DSP_COMP_MASK		GENMASK(1, 0)
#define BMP580_DSP_COMP_DIS		0
#define BMP580_DSP_TEMP_COMP_EN		1
/*
 * In section 7.27 of datasheet, modes 2 and 3 are technically the same.
 * Pressure compensation means also enabling temperature compensation
 */
#define BMP580_DSP_PRESS_COMP_EN	2
#define BMP580_DSP_PRESS_TEMP_COMP_EN	3
#define BMP580_DSP_IIR_FORCED_FLUSH	BIT(2)
#define BMP580_DSP_SHDW_IIR_TEMP_EN	BIT(3)
#define BMP580_DSP_FIFO_IIR_TEMP_EN	BIT(4)
#define BMP580_DSP_SHDW_IIR_PRESS_EN	BIT(5)
#define BMP580_DSP_FIFO_IIR_PRESS_EN	BIT(6)
#define BMP580_DSP_OOR_IIR_PRESS_EN	BIT(7)

#define BMP580_DSP_IIR_PRESS_MASK	GENMASK(5, 3)
#define BMP580_DSP_IIR_TEMP_MASK	GENMASK(2, 0)
#define BMP580_FILTER_OFF		0
#define BMP580_FILTER_1X		1
#define BMP580_FILTER_3X		2
#define BMP580_FILTER_7X		3
#define BMP580_FILTER_15X		4
#define BMP580_FILTER_31X		5
#define BMP580_FILTER_63X		6
#define BMP580_FILTER_127X		7

#define BMP580_NVM_ROW_ADDR_MASK	GENMASK(5, 0)
#define BMP580_NVM_PROG_EN		BIT(6)

#define BMP580_TEMP_SKIPPED		0x7f7f7f
#define BMP580_PRESS_SKIPPED		0x7f7f7f

/* BMP380 specific registers */
#define BMP380_REG_CMD			0x7E
#define BMP380_REG_CONFIG		0x1F
#define BMP380_REG_ODR			0x1D
#define BMP380_REG_OSR			0x1C
#define BMP380_REG_POWER_CONTROL	0x1B
#define BMP380_REG_IF_CONFIG		0x1A
#define BMP380_REG_INT_CONTROL		0x19
#define BMP380_REG_INT_STATUS		0x11
#define BMP380_REG_EVENT		0x10
#define BMP380_REG_STATUS		0x03
#define BMP380_REG_ERROR		0x02
#define BMP380_REG_ID			0x00

#define BMP380_REG_FIFO_CONFIG_1	0x18
#define BMP380_REG_FIFO_CONFIG_2	0x17
#define BMP380_REG_FIFO_WATERMARK_MSB	0x16
#define BMP380_REG_FIFO_WATERMARK_LSB	0x15
#define BMP380_REG_FIFO_DATA		0x14
#define BMP380_REG_FIFO_LENGTH_MSB	0x13
#define BMP380_REG_FIFO_LENGTH_LSB	0x12

#define BMP380_REG_SENSOR_TIME_MSB	0x0E
#define BMP380_REG_SENSOR_TIME_LSB	0x0D
#define BMP380_REG_SENSOR_TIME_XLSB	0x0C

#define BMP380_REG_TEMP_MSB		0x09
#define BMP380_REG_TEMP_LSB		0x08
#define BMP380_REG_TEMP_XLSB		0x07

#define BMP380_REG_PRESS_MSB		0x06
#define BMP380_REG_PRESS_LSB		0x05
#define BMP380_REG_PRESS_XLSB		0x04

#define BMP380_REG_CALIB_TEMP_START	0x31
#define BMP380_CALIB_REG_COUNT		21

#define BMP380_FILTER_MASK		GENMASK(3, 1)
#define BMP380_FILTER_OFF		0
#define BMP380_FILTER_1X		1
#define BMP380_FILTER_3X		2
#define BMP380_FILTER_7X		3
#define BMP380_FILTER_15X		4
#define BMP380_FILTER_31X		5
#define BMP380_FILTER_63X		6
#define BMP380_FILTER_127X		7

#define BMP380_OSRS_TEMP_MASK		GENMASK(5, 3)
#define BMP380_OSRS_PRESS_MASK		GENMASK(2, 0)

#define BMP380_ODRS_MASK		GENMASK(4, 0)

#define BMP380_CTRL_SENSORS_MASK	GENMASK(1, 0)
#define BMP380_CTRL_SENSORS_PRESS_EN	BIT(0)
#define BMP380_CTRL_SENSORS_TEMP_EN	BIT(1)
#define BMP380_MODE_MASK		GENMASK(5, 4)
#define BMP380_MODE_SLEEP		0
#define BMP380_MODE_FORCED		1
#define BMP380_MODE_NORMAL		3

#define BMP380_MIN_TEMP			-4000
#define BMP380_MAX_TEMP			8500
#define BMP380_MIN_PRES			3000000
#define BMP380_MAX_PRES			12500000

#define BMP380_CMD_NOOP			0x00
#define BMP380_CMD_EXTMODE_EN_MID	0x34
#define BMP380_CMD_FIFO_FLUSH		0xB0
#define BMP380_CMD_SOFT_RESET		0xB6

#define BMP380_STATUS_CMD_RDY_MASK	BIT(4)
#define BMP380_STATUS_DRDY_PRESS_MASK	BIT(5)
#define BMP380_STATUS_DRDY_TEMP_MASK	BIT(6)

#define BMP380_ERR_FATAL_MASK		BIT(0)
#define BMP380_ERR_CMD_MASK		BIT(1)
#define BMP380_ERR_CONF_MASK		BIT(2)

#define BMP380_TEMP_SKIPPED		0x800000
#define BMP380_PRESS_SKIPPED		0x800000

/* BMP280 specific registers */
#define BMP280_REG_HUMIDITY_LSB		0xFE
#define BMP280_REG_HUMIDITY_MSB		0xFD
#define BMP280_REG_TEMP_XLSB		0xFC
#define BMP280_REG_TEMP_LSB		0xFB
#define BMP280_REG_TEMP_MSB		0xFA
#define BMP280_REG_PRESS_XLSB		0xF9
#define BMP280_REG_PRESS_LSB		0xF8
#define BMP280_REG_PRESS_MSB		0xF7

/* Helper mask to truncate excess 4 bits on pressure and temp readings */
#define BMP280_MEAS_TRIM_MASK		GENMASK(24, 4)

#define BMP280_REG_CONFIG		0xF5
#define BMP280_REG_CTRL_MEAS		0xF4
#define BMP280_REG_STATUS		0xF3
#define BMP280_REG_CTRL_HUMIDITY	0xF2

/* Due to non linear mapping, and data sizes we can't do a bulk read */
#define BMP280_REG_COMP_H1		0xA1
#define BMP280_REG_COMP_H2		0xE1
#define BMP280_REG_COMP_H3		0xE3
#define BMP280_REG_COMP_H4		0xE4
#define BMP280_REG_COMP_H5		0xE5
#define BMP280_REG_COMP_H6		0xE7

#define BMP280_REG_COMP_TEMP_START	0x88
#define BMP280_COMP_TEMP_REG_COUNT	6

#define BMP280_REG_COMP_PRESS_START	0x8E
#define BMP280_COMP_PRESS_REG_COUNT	18

#define BMP280_COMP_H5_MASK		GENMASK(15, 4)

#define BMP280_CONTIGUOUS_CALIB_REGS	(BMP280_COMP_TEMP_REG_COUNT + \
					 BMP280_COMP_PRESS_REG_COUNT)

#define BMP280_FILTER_MASK		GENMASK(4, 2)
#define BMP280_FILTER_OFF		0
#define BMP280_FILTER_2X		1
#define BMP280_FILTER_4X		2
#define BMP280_FILTER_8X		3
#define BMP280_FILTER_16X		4

#define BMP280_OSRS_HUMIDITY_MASK	GENMASK(2, 0)
#define BMP280_OSRS_HUMIDITY_SKIP	0
#define BMP280_OSRS_HUMIDITY_1X		1
#define BMP280_OSRS_HUMIDITY_2X		2
#define BMP280_OSRS_HUMIDITY_4X		3
#define BMP280_OSRS_HUMIDITY_8X		4
#define BMP280_OSRS_HUMIDITY_16X	5

#define BMP280_OSRS_TEMP_MASK		GENMASK(7, 5)
#define BMP280_OSRS_TEMP_SKIP		0
#define BMP280_OSRS_TEMP_1X		1
#define BMP280_OSRS_TEMP_2X		2
#define BMP280_OSRS_TEMP_4X		3
#define BMP280_OSRS_TEMP_8X		4
#define BMP280_OSRS_TEMP_16X		5

#define BMP280_OSRS_PRESS_MASK		GENMASK(4, 2)
#define BMP280_OSRS_PRESS_SKIP		0
#define BMP280_OSRS_PRESS_1X		1
#define BMP280_OSRS_PRESS_2X		2
#define BMP280_OSRS_PRESS_4X		3
#define BMP280_OSRS_PRESS_8X		4
#define BMP280_OSRS_PRESS_16X		5

#define BMP280_MODE_MASK		GENMASK(1, 0)
#define BMP280_MODE_SLEEP		0
#define BMP280_MODE_FORCED		1
#define BMP280_MODE_NORMAL		3

/* BMP180 specific registers */
#define BMP180_REG_OUT_XLSB		0xF8
#define BMP180_REG_OUT_LSB		0xF7
#define BMP180_REG_OUT_MSB		0xF6

#define BMP180_REG_CALIB_START		0xAA
#define BMP180_REG_CALIB_COUNT		22

#define BMP180_MEAS_CTRL_MASK		GENMASK(4, 0)
#define BMP180_MEAS_TEMP		0x0E
#define BMP180_MEAS_PRESS		0x14
#define BMP180_MEAS_SCO			BIT(5)
#define BMP180_OSRS_PRESS_MASK		GENMASK(7, 6)
#define BMP180_MEAS_PRESS_1X		0
#define BMP180_MEAS_PRESS_2X		1
#define BMP180_MEAS_PRESS_4X		2
#define BMP180_MEAS_PRESS_8X		3

/* BMP180 and BMP280 common registers */
#define BMP280_REG_CTRL_MEAS		0xF4
#define BMP280_REG_RESET		0xE0
#define BMP280_REG_ID			0xD0

#define BMP380_CHIP_ID			0x50
#define BMP580_CHIP_ID			0x50
#define BMP580_CHIP_ID_ALT		0x51
#define BMP180_CHIP_ID			0x55
#define BMP280_CHIP_ID			0x58
#define BME280_CHIP_ID			0x60
#define BMP280_SOFT_RESET_VAL		0xB6

/* BMP280 register skipped special values */
#define BMP280_TEMP_SKIPPED		0x80000
#define BMP280_PRESS_SKIPPED		0x80000
#define BMP280_HUMIDITY_SKIPPED		0x8000

/* Core exported structs */

static const char *const bmp280_supply_names[] = {
	"vddd", "vdda"
};

#define BMP280_NUM_SUPPLIES ARRAY_SIZE(bmp280_supply_names)

struct bmp180_calib {
	s16 AC1;
	s16 AC2;
	s16 AC3;
	u16 AC4;
	u16 AC5;
	u16 AC6;
	s16 B1;
	s16 B2;
	s16 MB;
	s16 MC;
	s16 MD;
};

/* See datasheet Section 4.2.2. */
struct bmp280_calib {
	u16 T1;
	s16 T2;
	s16 T3;
	u16 P1;
	s16 P2;
	s16 P3;
	s16 P4;
	s16 P5;
	s16 P6;
	s16 P7;
	s16 P8;
	s16 P9;
	u8  H1;
	s16 H2;
	u8  H3;
	s16 H4;
	s16 H5;
	s8  H6;
};

/* See datasheet Section 3.11.1. */
struct bmp380_calib {
	u16 T1;
	u16 T2;
	s8  T3;
	s16 P1;
	s16 P2;
	s8  P3;
	s8  P4;
	u16 P5;
	u16 P6;
	s8  P7;
	s8  P8;
	s16 P9;
	s8  P10;
	s8  P11;
};

struct bmp280_data {
	struct device *dev;
	struct mutex lock;
	struct regmap *regmap;
	struct completion done;
	bool use_eoc;
	const struct bmp280_chip_info *chip_info;
	union {
		struct bmp180_calib bmp180;
		struct bmp280_calib bmp280;
		struct bmp380_calib bmp380;
	} calib;
	struct regulator_bulk_data supplies[BMP280_NUM_SUPPLIES];
	unsigned int start_up_time; /* in microseconds */

	/* log of base 2 of oversampling rate */
	u8 oversampling_press;
	u8 oversampling_temp;
	u8 oversampling_humid;
	u8 iir_filter_coeff;

	/*
	 * BMP380 devices introduce sampling frequency configuration. See
	 * datasheet sections 3.3.3. and 4.3.19 for more details.
	 *
	 * BMx280 devices allowed indirect configuration of sampling frequency
	 * changing the t_standby duration between measurements, as detailed on
	 * section 3.6.3 of the datasheet.
	 */
	int sampling_freq;

	/*
	 * Carryover value from temperature conversion, used in pressure
	 * calculation.
	 */
	s32 t_fine;

	/*
	 * DMA (thus cache coherency maintenance) may require the
	 * transfer buffers to live in their own cache lines.
	 */
	union {
		/* Sensor data buffer */
		u8 buf[3];
		/* Calibration data buffers */
		__le16 bmp280_cal_buf[BMP280_CONTIGUOUS_CALIB_REGS / 2];
		__be16 bmp180_cal_buf[BMP180_REG_CALIB_COUNT / 2];
		u8 bmp380_cal_buf[BMP380_CALIB_REG_COUNT];
		/* Miscellaneous, endianess-aware data buffers */
		__le16 le16;
		__be16 be16;
	} __aligned(IIO_DMA_MINALIGN);
};

struct bmp280_chip_info {
	unsigned int id_reg;
	const unsigned int chip_id;

	const struct regmap_config *regmap_config;

	const struct iio_chan_spec *channels;
	int num_channels;
	unsigned int start_up_time;

	const int *oversampling_temp_avail;
	int num_oversampling_temp_avail;
	int oversampling_temp_default;

	const int *oversampling_press_avail;
	int num_oversampling_press_avail;
	int oversampling_press_default;

	const int *oversampling_humid_avail;
	int num_oversampling_humid_avail;
	int oversampling_humid_default;

	const int *iir_filter_coeffs_avail;
	int num_iir_filter_coeffs_avail;
	int iir_filter_coeff_default;

	const int (*sampling_freq_avail)[2];
	int num_sampling_freq_avail;
	int sampling_freq_default;

	int (*chip_config)(struct bmp280_data *);
	int (*read_temp)(struct bmp280_data *, int *, int *);
	int (*read_press)(struct bmp280_data *, int *, int *);
	int (*read_humid)(struct bmp280_data *, int *, int *);
	int (*read_calib)(struct bmp280_data *);
	int (*preinit)(struct bmp280_data *);
};

/* Chip infos for each variant */
extern const struct bmp280_chip_info bmp180_chip_info;
extern const struct bmp280_chip_info bmp280_chip_info;
extern const struct bmp280_chip_info bme280_chip_info;
extern const struct bmp280_chip_info bmp380_chip_info;
extern const struct bmp280_chip_info bmp580_chip_info;

/* Regmap configurations */
extern const struct regmap_config bmp180_regmap_config;
extern const struct regmap_config bmp280_regmap_config;
extern const struct regmap_config bmp380_regmap_config;
extern const struct regmap_config bmp580_regmap_config;

/* Probe called from different transports */
int bmp280_common_probe(struct device *dev,
			struct regmap *regmap,
			const struct bmp280_chip_info *,
			const char *name,
			int irq);

/* PM ops */
extern const struct dev_pm_ops bmp280_dev_pm_ops;
