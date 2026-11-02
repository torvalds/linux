// SPDX-License-Identifier: GPL-2.0-only
/*
 * IIO driver for Texas Instruments ADS112C14 and similar ADCs.
 *
 * Copyright (C) 2026 Texas Instruments Incorporated - https://www.ti.com/
 * Copyright (C) 2026 Baylibre Inc.
 *
 * Datasheet: https://www.ti.com/lit/ds/symlink/ads122c14.pdf
 */

#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/crc8.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/device/devres.h>
#include <linux/i2c.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/interrupt.h>
#include <linux/math64.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/time64.h>
#include <linux/types.h>
#include <linux/unaligned.h>
#include <linux/units.h>

/* Arbitrary limit since channels are dynamic. */
#define ADS112C14_MAX_MEASUREMENT_CHANNELS 16

/* Datasheet t_d(RST) - time to wait after reset before next I2C use. */
#define ADS112C14_DELAY_RESET_US 500

#define ADS112C14_CMD_RDATA	0x00
#define ADS112C14_CMD_RREG	0x40
#define ADS112C14_CMD_WREG	0x80

#define ADS112C14_REG_DEVICE_ID				0x00
#define   ADS112C14_DEVICE_ID_BITS			GENMASK(3, 0)

#define ADS112C14_REG_REVISION_ID			0x01

#define ADS112C14_REG_STATUS_MSB			0x02
#define   ADS112C14_STATUS_MSB_RESETN			BIT(7)
#define   ADS112C14_STATUS_MSB_AVDD_UVN			BIT(6)
#define   ADS112C14_STATUS_MSB_REF_UVN			BIT(5)
#define   ADS112C14_STATUS_MSB_REG_MAP_CRC_FAULTN	BIT(3)
#define   ADS112C14_STATUS_MSB_MEM_FAULTN		BIT(2)
#define   ADS112C14_STATUS_MSB_REG_WRITE_FAULTN		BIT(1)
#define   ADS112C14_STATUS_MSB_DRDY			BIT(0)

#define ADS112C14_REG_STATUS_LSB			0x03
#define   ADS112C14_STATUS_LSB_CONV_COUNT		GENMASK(7, 4)
#define   ADS112C14_STATUS_LSB_GPIO3_DAT_IN		BIT(3)
#define   ADS112C14_STATUS_LSB_GPIO2_DAT_IN		BIT(2)
#define   ADS112C14_STATUS_LSB_GPIO1_DAT_IN		BIT(1)
#define   ADS112C14_STATUS_LSB_GPIO0_DAT_IN		BIT(0)

#define ADS112C14_REG_CONVERSION_CTRL			0x04
#define   ADS112C14_CONVERSION_CTRL_RESET		GENMASK(7, 2)
#define   ADS112C14_CONVERSION_CTRL_START		BIT(1)
#define   ADS112C14_CONVERSION_CTRL_STOP		BIT(0)

#define ADS112C14_REG_DEVICE_CFG			0x05
#define   ADS112C14_DEVICE_CFG_PWDN			BIT(7)
#define   ADS112C14_DEVICE_CFG_STBY_MODE		BIT(6)
#define   ADS112C14_DEVICE_CFG_BOCS			GENMASK(5, 4)
#define     ADS112C14_DEVICE_CFG_BOCS_DISABLED		  0
#define     ADS112C14_DEVICE_CFG_BOCS_200_nA		  1
#define     ADS112C14_DEVICE_CFG_BOCS_1_uA		  2
#define     ADS112C14_DEVICE_CFG_BOCS_10_uA		  3

#define   ADS112C14_DEVICE_CFG_CLK_SEL			BIT(3)
#define     ADS112C14_DEVICE_CFG_CLK_SEL_INTERNAL	  0
#define     ADS112C14_DEVICE_CFG_CLK_SEL_EXTERNAL	  1
#define   ADS112C14_DEVICE_CFG_CONV_MODE		BIT(2)
#define     ADS112C14_DEVICE_CFG_CONV_MODE_CONTINUOUS	  0
#define     ADS112C14_DEVICE_CFG_CONV_MODE_SINGLE_SHOT	  1
#define   ADS112C14_DEVICE_CFG_SPEED_MODE		GENMASK(1, 0)

#define ADS112C14_REG_DATA_RATE_CFG			0x06
#define   ADS112C14_DATA_RATE_CFG_DELAY			GENMASK(7, 4)
#define   ADS112C14_DATA_RATE_CFG_GC_EN			BIT(3)
#define   ADS112C14_DATA_RATE_CFG_FLTR_OSR		GENMASK(2, 0)
#define     ADS112C14_DATA_RATE_CFG_FLTR_OSR_16		  0
#define     ADS112C14_DATA_RATE_CFG_FLTR_OSR_32		  1
#define     ADS112C14_DATA_RATE_CFG_FLTR_OSR_128	  2
#define     ADS112C14_DATA_RATE_CFG_FLTR_OSR_256	  3
#define     ADS112C14_DATA_RATE_CFG_FLTR_OSR_512	  4
#define     ADS112C14_DATA_RATE_CFG_FLTR_OSR_1024	  5
#define     ADS112C14_DATA_RATE_CFG_FLTR_OSR_25SPS	  6
#define     ADS112C14_DATA_RATE_CFG_FLTR_OSR_20SPS	  7

#define ADS112C14_REG_MUX_CFG				0x07
#define   ADS112C14_MUX_CFG_AINP			GENMASK(7, 4)
#define   ADS112C14_MUX_CFG_AINN			GENMASK(3, 0)
#define     ADS112C14_MUX_CFG_AIN_GND			  8

#define ADS112C14_REG_GAIN_CFG				0x08
#define   ADS112C14_GAIN_CFG_SPARE			BIT(7)
#define   ADS112C14_GAIN_CFG_SYS_MON			GENMASK(6, 4)
#define   ADS112C14_GAIN_CFG_GAIN			GENMASK(3, 0)

#define ADS112C14_REG_REFERENCE_CFG			0x09
#define   ADS112C14_REFERENCE_CFG_REF_UV_EN		BIT(7)
#define   ADS112C14_REFERENCE_CFG_REFP_BUF_EN		BIT(5)
#define   ADS112C14_REFERENCE_CFG_REFN_BUF_EN		BIT(4)
#define   ADS112C14_REFERENCE_CFG_REF_VAL		BIT(2)
#define     ADS112C14_REFERENCE_CFG_REF_VAL_1_25V	  0
#define     ADS112C14_REFERENCE_CFG_REF_VAL_2_5V	  1
#define   ADS112C14_REFERENCE_CFG_REF_SEL		GENMASK(1, 0)
#define     ADS112C14_REFERENCE_CFG_REF_SEL_INTERNAL	  0
#define     ADS112C14_REFERENCE_CFG_REF_SEL_EXTERNAL	  1
#define     ADS112C14_REFERENCE_CFG_REF_SEL_AVDD	  2

#define ADS112C14_REG_DIGITAL_CFG			0x0A
#define   ADS112C14_DIGITAL_CFG_REG_MAP_CRC_EN		BIT(6)
#define   ADS112C14_DIGITAL_CFG_I2C_CRC_EN		BIT(5)
#define   ADS112C14_DIGITAL_CFG_STATUS_EN		BIT(4)
#define   ADS112C14_DIGITAL_CFG_FAULT_PIN_BEHAVIOR	BIT(3)
#define   ADS112C14_DIGITAL_CFG_CODING			BIT(1)

#define ADS112C14_REG_GPIO_CFG				0x0B
#define   ADS112C14_GPIO_CFG_GPIO3_CFG			GENMASK(7, 6)
#define   ADS112C14_GPIO_CFG_GPIO2_CFG			GENMASK(5, 4)
#define   ADS112C14_GPIO_CFG_GPIO1_CFG			GENMASK(3, 2)
#define   ADS112C14_GPIO_CFG_GPIO0_CFG			GENMASK(1, 0)
#define     ADS112C14_GPIO_CFG_GPIO_CFG_DISABLED	  0
#define     ADS112C14_GPIO_CFG_GPIO_CFG_INPUT		  1
#define     ADS112C14_GPIO_CFG_GPIO_CFG_OUTPUT_PUSH_PULL  2
#define     ADS112C14_GPIO_CFG_GPIO_CFG_OUTPUT_OPEN_DRAIN 3

#define ADS112C14_REG_GPIO_DATA_OUTPUT			0x0C
#define   ADS112C14_GPIO_DATA_OUTPUT_GPIO3_SRC		BIT(7)
#define     ADS112C14_GPIO_DATA_OUTPUT_GPIO3_SRC_DAT_OUT  0
#define     ADS112C14_GPIO_DATA_OUTPUT_GPIO3_SRC_DRDY	  1
#define   ADS112C14_GPIO_DATA_OUTPUT_GPIO2_SRC		BIT(6)
#define   ADS112C14_GPIO_DATA_OUTPUT_GPIO3_DAT_OUT	BIT(3)
#define   ADS112C14_GPIO_DATA_OUTPUT_GPIO2_DAT_OUT	BIT(2)
#define   ADS112C14_GPIO_DATA_OUTPUT_GPIO1_DAT_OUT	BIT(1)
#define   ADS112C14_GPIO_DATA_OUTPUT_GPIO0_DAT_OUT	BIT(0)

#define ADS112C14_REG_IDAC_MAG_CFG			0x0D
#define   ADS112C14_IDAC_MAG_CFG_I2MAG			GENMASK(7, 4)
#define   ADS112C14_IDAC_MAG_CFG_I1MAG			GENMASK(3, 0)

#define ADS112C14_REG_IDAC_MUX_CFG			0x0E
#define   ADS112C14_IDAC_MUX_CFG_IUNIT			BIT(7)
#define   ADS112C14_IDAC_MUX_CFG_I2MUX			GENMASK(6, 4)
#define   ADS112C14_IDAC_MUX_CFG_I1MUX			GENMASK(2, 0)

#define ADS112C14_REG_REG_MAP_CRC			0x0F

#define ADS112C14_INT_REF0_mV				1250
#define ADS112C14_INT_REF1_mV				2500

enum {
	ADS112C14_VREF_SOURCE_INTERNAL_2_5V,
	ADS112C14_VREF_SOURCE_INTERNAL_1_25V,
	ADS112C14_VREF_SOURCE_EXTERNAL,
	ADS112C14_VREF_SOURCE_AVDD,
};

static const char * const ads112c14_vref_source_names[] = {
	[ADS112C14_VREF_SOURCE_INTERNAL_2_5V] = "internal-2.5v",
	[ADS112C14_VREF_SOURCE_INTERNAL_1_25V] = "internal-1.25v",
	[ADS112C14_VREF_SOURCE_EXTERNAL] = "external",
	[ADS112C14_VREF_SOURCE_AVDD] = "avdd",
};

/*
 * Available gains as tenths (e.g. value 5 == 0.5 gain). Indexes correspond to
 * ADS112C14_GAIN_CFG_GAIN values.
 */
static const u32 ads112c14_pga_gains_x10[] = {
	5, 10, 20, 40, 50, 80, 100, 160,		/* 0 -  7 */
	200, 320, 500, 640, 1000, 1280, 2000, 2560,	/* 8 - 15 */
};

#define ADS112C14_INTERNAL_CLK_Hz (4096 * HZ_PER_KHZ)

/* Index corresponds to first 2 ADS112C14_DATA_RATE_CFG_FLTR_OSR values. */
static const int ads112c14_sinc4_osr_available[] = {
	16, 32
};

/* Index corresponds to next 4 ADS112C14_DATA_RATE_CFG_FLTR_OSR values. */
static const int ads112c14_sinc4_sinc1_osr_available[] = {
	128, 256, 512, 1024
};

/* Index corresponds to ADS112C14_DEVICE_CFG_SPEED_MODE value. */
static const int ads112c14_sinc4_sinc1_pf1_20sps_osr_available[] = {
	1600, 12800, 25600, 51200
};

/* Index corresponds to ADS112C14_DEVICE_CFG_SPEED_MODE value. */
static const int ads112c14_sinc4_sinc1_pf1_25sps_osr_available[] = {
	1280, 10240, 20480, 40960
};

/* Index corresponds to ADS112C14_DEVICE_CFG_SPEED_MODE value. */
static const int ads112c14_fmod_div[] = {
	128, 16, 8, 4
};

#define ADS112C14_DELAY_MAX FIELD_MAX(ADS112C14_DATA_RATE_CFG_DELAY)

/* Table 7-6 latency in t_MOD for OSR [16, 32, 128, 256, 512, 1024]. */
static const int ads112c14_sinc_latency_tmod[][ARRAY_SIZE(ads112c14_fmod_div)] = {
	{ 80, 88, 88, 104 },
	{ 144, 152, 152, 168 },
	{ 240, 248, 248, 264 },
	{ 368, 376, 376, 392 },
	{ 624, 632, 632, 648 },
	{ 1136, 1144, 1144, 1160 },
};

/* Table 7-7 latency in t_MOD for output data rates [25SPS, 20SPS]. */
static const int ads112c14_fir_latency_tmod[][ARRAY_SIZE(ads112c14_fmod_div)] = {
	{ 1416, 10384, 20624, 41120 },
	{ 1736, 12944, 25744, 51360 },
};

enum ads112c14_filter_type {
	ADS112C14_FILTER_TYPE_SINC4,
	ADS112C14_FILTER_TYPE_SINC4_SINC1,
	ADS112C14_FILTER_TYPE_SINC4_SINC1_PF1,
};

static const char * const ads112c14_filter_type_names[] = {
	[ADS112C14_FILTER_TYPE_SINC4] = "sinc4",
	[ADS112C14_FILTER_TYPE_SINC4_SINC1] = "sinc4+sinc1",
	[ADS112C14_FILTER_TYPE_SINC4_SINC1_PF1] = "sinc4+sinc1+pf1",
};

#define ADS112C14_I2C_CRC8_POLYNOMIAL 0x07
DECLARE_CRC8_TABLE(ads112c14_crc8_table);

struct ads112c14_chip_info {
	const char *name;
	u8 device_id;
	u32 resolution_bits;
};

/* Fixed channels for system monitor measurements. */

#define ADS112C14_SYS_MON_CHANNEL_BASE 100

enum {
	ADS112C14_SYS_MON_CHANNEL_TEMP = ADS112C14_SYS_MON_CHANNEL_BASE,
	ADS112C14_SYS_MON_CHANNEL_EXT_REF,
	ADS112C14_SYS_MON_CHANNEL_AVDD,
	ADS112C14_SYS_MON_CHANNEL_DVDD,
	ADS112C14_SYS_MON_CHANNEL_SHORT,
};

static const struct iio_chan_spec_ext_info ads112c14_ext_info[];

static const struct iio_chan_spec ads112c14_sys_mon_channels[] = {
	{
		.type = IIO_TEMP,
		.indexed = 1,
		.channel = ADS112C14_SYS_MON_CHANNEL_TEMP,
		.address = 2,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW)
				    | BIT(IIO_CHAN_INFO_SCALE)
				    | BIT(IIO_CHAN_INFO_OFFSET)
				    | BIT(IIO_CHAN_INFO_SAMP_FREQ)
				    | BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_SAMP_FREQ)
				    | BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.ext_info = ads112c14_ext_info,
	},
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = ADS112C14_SYS_MON_CHANNEL_EXT_REF,
		.address = 3,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW)
				    | BIT(IIO_CHAN_INFO_SCALE)
				    | BIT(IIO_CHAN_INFO_SAMP_FREQ)
				    | BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_SAMP_FREQ)
				    | BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.ext_info = ads112c14_ext_info,
	},
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = ADS112C14_SYS_MON_CHANNEL_AVDD,
		.address = 4,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW)
				    | BIT(IIO_CHAN_INFO_SCALE)
				    | BIT(IIO_CHAN_INFO_SAMP_FREQ)
				    | BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_SAMP_FREQ)
				    | BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.ext_info = ads112c14_ext_info,
	},
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = ADS112C14_SYS_MON_CHANNEL_DVDD,
		.address = 5,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW)
				    | BIT(IIO_CHAN_INFO_SCALE)
				    | BIT(IIO_CHAN_INFO_SAMP_FREQ)
				    | BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_SAMP_FREQ)
				    | BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.ext_info = ads112c14_ext_info,
	},
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = ADS112C14_SYS_MON_CHANNEL_SHORT,
		.channel2 = ADS112C14_SYS_MON_CHANNEL_SHORT,
		.differential = 1,
		.address = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW)
				    | BIT(IIO_CHAN_INFO_SCALE)
				    | BIT(IIO_CHAN_INFO_SAMP_FREQ)
				    | BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_SCALE)
				    | BIT(IIO_CHAN_INFO_SAMP_FREQ)
				    | BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.ext_info = ads112c14_ext_info,
	},
};

struct ads112c14_measurement {
	const char *label;
	u32 vref_source;
	u8 iunit;
	u8 idac1_mag;
	u8 idac2_mag;
	u8 idac1_mux;
	u8 idac2_mux;
	u8 iadc_count;
	u8 gain_val;
	u8 burnout;
	bool global_chop;
	bool bipolar;
	int scale_available[ARRAY_SIZE(ads112c14_pga_gains_x10)][2];
};

struct ads112c14_channel_state {
	u8 speed_mode;
	u8 filter_osr;
	u8 delay;
};

struct ads112c14_data {
	const struct ads112c14_chip_info *chip_info;
	struct regmap *regmap;
	struct iio_trigger *drdy_trig;
	/* Synchronizes access to register value fields. */
	struct mutex lock;
	long fclk_Hz;
	int drdy_irq;
	struct completion drdy_completion;
	bool continuous_mode;
	bool i2c_crc_enabled;
	u32 avdd_uV;
	u32 ext_ref_uV;
	bool refp_is_avdd;
	bool refn_is_gnd;
	u32 ext_ref_ohms;
	struct ads112c14_measurement *measurements;
	struct ads112c14_channel_state *channel_states;
	u32 num_measurements;
	u8 sys_mon_chan_short_gain_val;
	int sys_mon_chan_short_scale_available[ARRAY_SIZE(ads112c14_pga_gains_x10)][2];
	int sinc4_sample_rate_available[ARRAY_SIZE(ads112c14_sinc4_osr_available)][ARRAY_SIZE(ads112c14_fmod_div)][2];
	int sinc4_sinc1_sample_rate_available[ARRAY_SIZE(ads112c14_sinc4_sinc1_osr_available)][ARRAY_SIZE(ads112c14_fmod_div)][2];
	int sinc4_sinc1_pf1_sample_rate_available[2][2];
	int sinc_settling_time_range_available[ARRAY_SIZE(ads112c14_sinc_latency_tmod)][ARRAY_SIZE(ads112c14_fmod_div)][3][2];
	int fir_settling_time_range_available[ARRAY_SIZE(ads112c14_fir_latency_tmod)][ARRAY_SIZE(ads112c14_fmod_div)][3][2];
	IIO_DECLARE_BUFFER_WITH_TS(__be32, scan, ADS112C14_MAX_MEASUREMENT_CHANNELS +
						 ARRAY_SIZE(ads112c14_sys_mon_channels));
};

static irqreturn_t ads112c14_drdy_irq_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct ads112c14_data *data = iio_priv(indio_dev);

	if (READ_ONCE(data->continuous_mode))
		iio_trigger_poll(data->drdy_trig);
	else
		complete(&data->drdy_completion);

	return IRQ_HANDLED;
}

static const struct iio_trigger_ops ads112c14_trigger_ops = {
	.validate_device = iio_trigger_validate_own_device,
};

static bool ads112c14_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ADS112C14_REG_DEVICE_ID:
	case ADS112C14_REG_REVISION_ID:
	case ADS112C14_REG_STATUS_LSB:
		return false;
	default:
		return true;
	}
}

static bool ads112c14_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ADS112C14_REG_STATUS_MSB:
	case ADS112C14_REG_STATUS_LSB:
	case ADS112C14_REG_CONVERSION_CTRL:
		return true;
	default:
		return false;
	}
}

static const struct reg_default ads112c14_reg_defaults[] = {
	{ ADS112C14_REG_DEVICE_CFG, 0 },
	{ ADS112C14_REG_DATA_RATE_CFG, 0 },
	{ ADS112C14_REG_MUX_CFG, 0 },
	{ ADS112C14_REG_GAIN_CFG, FIELD_PREP_CONST(ADS112C14_GAIN_CFG_GAIN, 1) },
	{ ADS112C14_REG_REFERENCE_CFG, 0 },
	{ ADS112C14_REG_DIGITAL_CFG, 0 },
	{ ADS112C14_REG_GPIO_CFG, 0 },
	{ ADS112C14_REG_GPIO_DATA_OUTPUT, 0 },
	{ ADS112C14_REG_IDAC_MAG_CFG, 0 },
	{ ADS112C14_REG_IDAC_MUX_CFG, FIELD_PREP_CONST(ADS112C14_IDAC_MUX_CFG_I2MUX, 1) },
};

/**
 * ads112c14_i2c_read_bytes() - Read bytes from the device over I2C
 * @client: I2C client for the device
 * @cmd: Command to send to the device before reading
 * @buf: Buffer to store the read bytes
 * @len: Number of bytes to read
 * @use_crc: Whether to use CRC8 for data integrity check
 *
 * If I2C_CRC is enabled, @use_crc may be set to true to perform a CRC8 check
 * on the received data.
 */
static int ads112c14_i2c_read_bytes(struct i2c_client *client, u8 cmd,
				    u8 *buf, u8 len, bool use_crc)
{
	u8 rx_buf[4]; /* Up to 3 data bytes + 1 CRC byte. */
	u8 rx_len;
	int ret;

	rx_len = len + (use_crc ? 1 : 0);

	if (rx_len > sizeof(rx_buf))
		return -EINVAL;

	ret = i2c_smbus_read_i2c_block_data(client, cmd, rx_len, rx_buf);
	if (ret < 0)
		return ret;

	if (use_crc) {
		u8 crc = crc8(ads112c14_crc8_table, rx_buf, len, CRC8_INIT_VALUE);

		if (crc != rx_buf[len])
			return -EBADMSG;
	}

	memcpy(buf, rx_buf, len);

	return 0;
}

/**
 * ads112c14_regmap_bus_read() - Read a register from the device
 * @context: Pointer to the device context
 * @reg_buf: Register address to read
 * @reg_size: Size of the register address (should be 1)
 * @val_buf: Buffer to store the read value
 * @val_size: Size of the value to read
 *
 * Custom regmap read function that also does CRC check when enabled.
 */
static int ads112c14_regmap_bus_read(void *context, const void *reg_buf,
				     size_t reg_size, void *val_buf,
				     size_t val_size)
{
	struct ads112c14_data *data = context;
	struct device *dev = regmap_get_device(data->regmap);
	struct i2c_client *client = to_i2c_client(dev);
	const u8 *cmd = reg_buf;

	if (reg_size != 1)
		return -EINVAL;

	return ads112c14_i2c_read_bytes(client, cmd[0], val_buf, val_size,
					data->i2c_crc_enabled);
}

/**
 * ads112c14_regmap_bus_write() - Write a register to the device
 * @context: Pointer to the device context
 * @data_buf: Buffer containing the register address and value to write
 * @count: Number of bytes to write
 *
 * Custom regmap write function that also does readback with CRC check of
 * nonvolatile registers when CRC is enabled.
 */
static int ads112c14_regmap_bus_write(void *context, const void *data_buf,
				      size_t count)
{
	struct ads112c14_data *data = context;
	struct device *dev = regmap_get_device(data->regmap);
	struct i2c_client *client = to_i2c_client(dev);
	const u8 *tx = data_buf;
	u8 reg, readback;
	int ret;

	if (count != 2)
		return -EINVAL;

	ret = i2c_smbus_write_byte_data(client, tx[0], tx[1]);
	if (ret)
		return ret;

	reg = tx[0] & ~ADS112C14_CMD_WREG;

	if (!data->i2c_crc_enabled || ads112c14_volatile_reg(dev, reg))
		return 0;

	ret = ads112c14_i2c_read_bytes(client, reg | ADS112C14_CMD_RREG,
				       &readback, sizeof(readback), true);
	if (ret)
		return ret;

	if (readback != tx[1])
		return -EIO;

	return 0;
}

static const struct regmap_bus ads112c14_regmap_bus = {
	.read = ads112c14_regmap_bus_read,
	.write = ads112c14_regmap_bus_write,
	.reg_format_endian_default = REGMAP_ENDIAN_BIG,
	.val_format_endian_default = REGMAP_ENDIAN_BIG,
};

static const struct regmap_config ads112c14_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.read_flag_mask = ADS112C14_CMD_RREG,
	.write_flag_mask = ADS112C14_CMD_WREG,
	.max_register = ADS112C14_REG_REG_MAP_CRC,
	.writeable_reg = ads112c14_writeable_reg,
	.volatile_reg = ads112c14_volatile_reg,
	.reg_defaults = ads112c14_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(ads112c14_reg_defaults),
	.cache_type = REGCACHE_MAPLE,
};

static int ads112c14_get_osr(struct ads112c14_channel_state *channel_state)
{
	u8 i;

	switch (channel_state->filter_osr) {
	case ADS112C14_DATA_RATE_CFG_FLTR_OSR_16...ADS112C14_DATA_RATE_CFG_FLTR_OSR_32:
		i = channel_state->filter_osr;
		return ads112c14_sinc4_osr_available[i];
	case ADS112C14_DATA_RATE_CFG_FLTR_OSR_128...ADS112C14_DATA_RATE_CFG_FLTR_OSR_1024:
		i = channel_state->filter_osr - ADS112C14_DATA_RATE_CFG_FLTR_OSR_128;
		return ads112c14_sinc4_sinc1_osr_available[i];
	case ADS112C14_DATA_RATE_CFG_FLTR_OSR_25SPS:
		i = channel_state->speed_mode;
		return ads112c14_sinc4_sinc1_pf1_25sps_osr_available[i];
	case ADS112C14_DATA_RATE_CFG_FLTR_OSR_20SPS:
		i = channel_state->speed_mode;
		return ads112c14_sinc4_sinc1_pf1_20sps_osr_available[i];
	default:
		return -EINVAL;
	}
}

static int ads112c14_get_fmod_Hz(struct ads112c14_data *data,
				 struct ads112c14_channel_state *channel_state)
{
	return data->fclk_Hz / ads112c14_fmod_div[channel_state->speed_mode];
}

static int ads112c14_delay_to_tmod(u8 delay)
{
	if (!delay)
		return 0;

	return BIT(delay - 1);
}

static int ads112c14_get_latency_tmod(struct ads112c14_channel_state *channel_state)
{
	u8 speed_mode = channel_state->speed_mode;
	u8 filter_osr = channel_state->filter_osr;

	if (speed_mode >= ARRAY_SIZE(ads112c14_fmod_div))
		return -EINVAL;

	if (filter_osr <= ADS112C14_DATA_RATE_CFG_FLTR_OSR_1024)
		return ads112c14_sinc_latency_tmod[filter_osr][speed_mode];

	if (filter_osr == ADS112C14_DATA_RATE_CFG_FLTR_OSR_25SPS)
		return ads112c14_fir_latency_tmod[0][speed_mode];

	if (filter_osr == ADS112C14_DATA_RATE_CFG_FLTR_OSR_20SPS)
		return ads112c14_fir_latency_tmod[1][speed_mode];

	return -EINVAL;
}

static int ads112c14_get_settling_time_us(struct ads112c14_data *data,
					  struct ads112c14_channel_state *channel_state,
					  u8 delay, u32 *settling_time_us)
{
	int fmod_Hz, latency_tmod;
	u64 total_tmod;

	fmod_Hz = ads112c14_get_fmod_Hz(data, channel_state);
	if (fmod_Hz <= 0)
		return -EINVAL;

	latency_tmod = ads112c14_get_latency_tmod(channel_state);
	if (latency_tmod < 0)
		return latency_tmod;

	total_tmod = latency_tmod + ads112c14_delay_to_tmod(delay);
	*settling_time_us = div64_u64(total_tmod * USEC_PER_SEC, fmod_Hz);

	return 0;
}

static int ads112c14_find_delay_for_settling_time_us(struct ads112c14_data *data,
						     struct ads112c14_channel_state *channel_state,
						     s64 settling_time_us, u8 *delay)
{
	u64 delay_us, delay_tmod_needed;
	u32 fixed_latency_us;
	int ret, fmod_Hz;
	u8 i;

	ret = ads112c14_get_settling_time_us(data, channel_state, 0, &fixed_latency_us);
	if (ret)
		return ret;

	if (settling_time_us <= fixed_latency_us) {
		*delay = 0;
		return 0;
	}

	fmod_Hz = ads112c14_get_fmod_Hz(data, channel_state);
	if (fmod_Hz <= 0)
		return -EINVAL;

	delay_us = settling_time_us - fixed_latency_us;
	delay_tmod_needed = DIV_ROUND_UP_ULL(delay_us * fmod_Hz, USEC_PER_SEC);

	for (i = 1; i < ADS112C14_DELAY_MAX; i++) {
		if (ads112c14_delay_to_tmod(i) >= delay_tmod_needed)
			break;
	}

	*delay = i;

	return 0;
}

static ssize_t ads112c14_read_settling_time(struct iio_dev *indio_dev,
					    uintptr_t private,
					    const struct iio_chan_spec *chan,
					    char *buf)
{
	struct ads112c14_data *data = iio_priv(indio_dev);
	struct ads112c14_channel_state *channel_state;
	u32 settling_time_us;
	int vals[2];
	int ret;

	guard(mutex)(&data->lock);

	channel_state = &data->channel_states[chan->scan_index];

	ret = ads112c14_get_settling_time_us(data, channel_state,
					     channel_state->delay,
					     &settling_time_us);
	if (ret)
		return ret;

	iio_val_s64_decompose(settling_time_us, &vals[0], &vals[1]);

	return iio_format_value(buf, IIO_VAL_DECIMAL64_MICRO, ARRAY_SIZE(vals), vals);
}

static ssize_t ads112c14_write_settling_time(struct iio_dev *indio_dev,
					     uintptr_t private,
					     const struct iio_chan_spec *chan,
					     const char *buf, size_t len)
{
	struct ads112c14_data *data = iio_priv(indio_dev);
	struct ads112c14_channel_state *channel_state;
	s64 settling_time_us;
	int integer;
	int fract;
	u8 delay;
	int ret;

	ret = iio_str_to_fixpoint(buf, 100000, &integer, &fract);
	if (ret)
		return ret;

	settling_time_us = integer * MICRO + fract;
	if (settling_time_us < 0)
		return -EINVAL;

	IIO_DEV_ACQUIRE_DIRECT_MODE(indio_dev, claim);
	if (IIO_DEV_ACQUIRE_FAILED(claim))
		return -EBUSY;

	guard(mutex)(&data->lock);

	channel_state = &data->channel_states[chan->scan_index];

	ret = ads112c14_find_delay_for_settling_time_us(data, channel_state,
							settling_time_us, &delay);
	if (ret)
		return ret;

	channel_state->delay = delay;

	return len;
}

static ssize_t ads112c14_read_settling_time_available(struct iio_dev *indio_dev,
						      uintptr_t private,
						      const struct iio_chan_spec *chan,
						      char *buf)
{
	struct ads112c14_data *data = iio_priv(indio_dev);
	struct ads112c14_channel_state *channel_state;
	u8 filter_osr, speed_mode;
	const int (*range)[2];
	size_t len = 0;
	int i;

	guard(mutex)(&data->lock);

	channel_state = &data->channel_states[chan->scan_index];

	filter_osr = channel_state->filter_osr;
	speed_mode = channel_state->speed_mode;

	if (speed_mode >= ARRAY_SIZE(ads112c14_fmod_div))
		return -EINVAL;

	if (filter_osr <= ADS112C14_DATA_RATE_CFG_FLTR_OSR_1024)
		range = data->sinc_settling_time_range_available[filter_osr][speed_mode];
	else if (filter_osr == ADS112C14_DATA_RATE_CFG_FLTR_OSR_25SPS)
		range = data->fir_settling_time_range_available[0][speed_mode];
	else if (filter_osr == ADS112C14_DATA_RATE_CFG_FLTR_OSR_20SPS)
		range = data->fir_settling_time_range_available[1][speed_mode];
	else
		return -EINVAL;

	len += sysfs_emit_at(buf, len, "[");
	for (i = 0; i < 3; i++) {
		s64 range_val;
		s32 int_val, rem;

		range_val = iio_val_s64_compose(range[i][0], range[i][1]);
		int_val = div_s64_rem(range_val, MICRO, &rem);

		if (i)
			len += sysfs_emit_at(buf, len, " ");

		len += sysfs_emit_at(buf, len, "%d.%06d", int_val, rem);
	}
	len += sysfs_emit_at(buf, len, "]\n");

	return len;
}

static int ads112c14_prepare_measurement_channel(struct ads112c14_data *data,
						 const struct iio_chan_spec *chan,
						 bool en_burnout)
{
	struct ads112c14_measurement *measurement = &data->measurements[chan->scan_index];
	struct ads112c14_channel_state *channel_state;
	u32 refp_buf_en, refn_buf_en, ref_val, ref_sel;
	int ret;

	channel_state = &data->channel_states[chan->scan_index];

	ret = regmap_update_bits(data->regmap, ADS112C14_REG_DEVICE_CFG,
				 ADS112C14_DEVICE_CFG_SPEED_MODE,
				 FIELD_PREP(ADS112C14_DEVICE_CFG_SPEED_MODE,
					    channel_state->speed_mode));
	if (ret)
		return ret;

	ret = regmap_update_bits(data->regmap, ADS112C14_REG_MUX_CFG,
				 ADS112C14_MUX_CFG_AINP | ADS112C14_MUX_CFG_AINN,
				 FIELD_PREP(ADS112C14_MUX_CFG_AINP, chan->channel) |
				 FIELD_PREP(ADS112C14_MUX_CFG_AINN, chan->channel2));
	if (ret)
		return ret;

	ret = regmap_assign_bits(data->regmap, ADS112C14_REG_DIGITAL_CFG,
				 ADS112C14_DIGITAL_CFG_CODING,
				 !measurement->bipolar);
	if (ret)
		return ret;

	ret = regmap_update_bits(data->regmap, ADS112C14_REG_GAIN_CFG,
				 ADS112C14_GAIN_CFG_SYS_MON |
				 ADS112C14_GAIN_CFG_GAIN,
				 FIELD_PREP(ADS112C14_GAIN_CFG_SYS_MON, 0) |
				 FIELD_PREP(ADS112C14_GAIN_CFG_GAIN,
					    measurement->gain_val));
	if (ret)
		return ret;

	ret = regmap_update_bits(data->regmap, ADS112C14_REG_IDAC_MAG_CFG,
				 ADS112C14_IDAC_MAG_CFG_I2MAG |
				 ADS112C14_IDAC_MAG_CFG_I1MAG,
				 FIELD_PREP(ADS112C14_IDAC_MAG_CFG_I2MAG,
					    measurement->idac2_mag) |
				 FIELD_PREP(ADS112C14_IDAC_MAG_CFG_I1MAG,
					    measurement->idac1_mag));
	if (ret)
		return ret;

	ret = regmap_update_bits(data->regmap, ADS112C14_REG_IDAC_MUX_CFG,
				 ADS112C14_IDAC_MUX_CFG_IUNIT |
				 ADS112C14_IDAC_MUX_CFG_I2MUX |
				 ADS112C14_IDAC_MUX_CFG_I1MUX,
				 FIELD_PREP(ADS112C14_IDAC_MUX_CFG_IUNIT,
					    measurement->iunit) |
				 FIELD_PREP(ADS112C14_IDAC_MUX_CFG_I2MUX,
					    measurement->idac2_mux) |
				 FIELD_PREP(ADS112C14_IDAC_MUX_CFG_I1MUX,
					    measurement->idac1_mux));
	if (ret)
		return ret;

	ret = regmap_update_bits(data->regmap, ADS112C14_REG_DATA_RATE_CFG,
				 ADS112C14_DATA_RATE_CFG_DELAY |
				 ADS112C14_DATA_RATE_CFG_GC_EN |
				 ADS112C14_DATA_RATE_CFG_FLTR_OSR,
				 FIELD_PREP(ADS112C14_DATA_RATE_CFG_DELAY,
					    channel_state->delay) |
				 FIELD_PREP(ADS112C14_DATA_RATE_CFG_GC_EN,
					    (measurement->global_chop &&
					     !en_burnout) ? 1 : 0) |
				 FIELD_PREP(ADS112C14_DATA_RATE_CFG_FLTR_OSR,
					    channel_state->filter_osr));
	if (ret)
		return ret;

	refp_buf_en = !data->refp_is_avdd &&
		      measurement->vref_source == ADS112C14_VREF_SOURCE_EXTERNAL;
	refn_buf_en = !data->refn_is_gnd &&
		      measurement->vref_source == ADS112C14_VREF_SOURCE_EXTERNAL;

	ref_val = measurement->vref_source == ADS112C14_VREF_SOURCE_INTERNAL_2_5V ?
		ADS112C14_REFERENCE_CFG_REF_VAL_2_5V :
		ADS112C14_REFERENCE_CFG_REF_VAL_1_25V;

	switch (measurement->vref_source) {
	case ADS112C14_VREF_SOURCE_AVDD:
		ref_sel = ADS112C14_REFERENCE_CFG_REF_SEL_AVDD;
		break;
	case ADS112C14_VREF_SOURCE_EXTERNAL:
		ref_sel = ADS112C14_REFERENCE_CFG_REF_SEL_EXTERNAL;
		break;
	default:
		ref_sel = ADS112C14_REFERENCE_CFG_REF_SEL_INTERNAL;
		break;
	}

	return regmap_update_bits(data->regmap, ADS112C14_REG_REFERENCE_CFG,
				  ADS112C14_REFERENCE_CFG_REFP_BUF_EN |
				  ADS112C14_REFERENCE_CFG_REFN_BUF_EN |
				  ADS112C14_REFERENCE_CFG_REF_VAL |
				  ADS112C14_REFERENCE_CFG_REF_SEL,
				  FIELD_PREP(ADS112C14_REFERENCE_CFG_REFP_BUF_EN,
					     refp_buf_en) |
				  FIELD_PREP(ADS112C14_REFERENCE_CFG_REFN_BUF_EN,
					     refn_buf_en) |
				  FIELD_PREP(ADS112C14_REFERENCE_CFG_REF_VAL,
					     ref_val) |
				  FIELD_PREP(ADS112C14_REFERENCE_CFG_REF_SEL,
					     ref_sel));
}

static int ads112c14_prepare_sys_mon_channel(struct ads112c14_data *data,
					     const struct iio_chan_spec *chan)
{
	struct ads112c14_channel_state *channel_state;
	u32 gain_val;
	int ret;

	channel_state = &data->channel_states[chan->scan_index];

	/*
	 * NB: IDAC registers are left as-is in case they are generating current
	 * needed for the external reference measurement.
	 */

	/*
	 * All SYS_MON channels use GAIN of 1 to keep it simple. Other than
	 * the internal short channel, where it is useful in practice.
	 */
	gain_val = chan->channel == ADS112C14_SYS_MON_CHANNEL_SHORT ?
		   data->sys_mon_chan_short_gain_val : 1;

	ret = regmap_update_bits(data->regmap, ADS112C14_REG_GAIN_CFG,
				 ADS112C14_GAIN_CFG_SYS_MON |
				 ADS112C14_GAIN_CFG_GAIN,
				 FIELD_PREP(ADS112C14_GAIN_CFG_SYS_MON, chan->address) |
				 FIELD_PREP(ADS112C14_GAIN_CFG_GAIN, gain_val));
	if (ret)
		return ret;

	/* All SYS_MON channels use signed data to keep it simple. */
	ret = regmap_clear_bits(data->regmap, ADS112C14_REG_DIGITAL_CFG,
				ADS112C14_DIGITAL_CFG_CODING);
	if (ret)
		return ret;

	ret = regmap_update_bits(data->regmap, ADS112C14_REG_DEVICE_CFG,
				 ADS112C14_DEVICE_CFG_SPEED_MODE,
				 FIELD_PREP(ADS112C14_DEVICE_CFG_SPEED_MODE,
					    channel_state->speed_mode));
	if (ret)
		return ret;

	ret = regmap_update_bits(data->regmap, ADS112C14_REG_DATA_RATE_CFG,
				 ADS112C14_DATA_RATE_CFG_DELAY |
				 ADS112C14_DATA_RATE_CFG_GC_EN |
				 ADS112C14_DATA_RATE_CFG_FLTR_OSR,
				 FIELD_PREP(ADS112C14_DATA_RATE_CFG_DELAY,
					    channel_state->delay) |
				 FIELD_PREP(ADS112C14_DATA_RATE_CFG_GC_EN, 0) |
				 FIELD_PREP(ADS112C14_DATA_RATE_CFG_FLTR_OSR,
					    channel_state->filter_osr));
	if (ret)
		return ret;

	/*
	 * REVISIT: if we implement regulator support for the REFOUT pin, we
	 * might need to make this voltage match what is required by that. In
	 * that case, we could also adjust GAIN so that we still get the same
	 * range.
	 */
	/*
	 * NB: SYS_MON channels ignore REF_SEL except for the shorted input
	 * channel, so we set it here to internal reference to be consistent.
	 * If we ever need to make a measurement of shorted input with other
	 * reference source, we could add additional channels for that.
	 */
	ret = regmap_update_bits(data->regmap, ADS112C14_REG_REFERENCE_CFG,
				 ADS112C14_REFERENCE_CFG_REF_VAL |
				 ADS112C14_REFERENCE_CFG_REF_SEL,
				 FIELD_PREP(ADS112C14_REFERENCE_CFG_REF_VAL,
					    ADS112C14_REFERENCE_CFG_REF_VAL_2_5V) |
				 FIELD_PREP(ADS112C14_REFERENCE_CFG_REF_SEL,
					    ADS112C14_REFERENCE_CFG_REF_SEL_INTERNAL));
	if (ret)
		return ret;

	return 0;
}

static int ads112c14_prepare_channel(struct ads112c14_data *data,
				     const struct iio_chan_spec *chan,
				     bool en_burnout)
{
	if (chan->channel < ADS112C14_SYS_MON_CHANNEL_BASE)
		return ads112c14_prepare_measurement_channel(data, chan, en_burnout);

	return ads112c14_prepare_sys_mon_channel(data, chan);
}

static int ads112c14_scan_read(struct ads112c14_data *data, u8 *buf)
{
	struct i2c_client *client = to_i2c_client(regmap_get_device(data->regmap));
	int ret;
	u8 len;

	len = BITS_TO_BYTES(data->chip_info->resolution_bits);
	if (data->i2c_crc_enabled)
		len += 1;

	ret = i2c_smbus_read_i2c_block_data(client, ADS112C14_CMD_RDATA, len, buf);
	if (ret < 0)
		return ret;

	return 0;
}

static int ads112c14_wait_for_conversion_irq(struct ads112c14_data *data,
					     u32 settle_time_us)
{
	unsigned long remaining;
	int ret;

	reinit_completion(&data->drdy_completion);

	ret = regmap_write(data->regmap, ADS112C14_REG_CONVERSION_CTRL,
			   ADS112C14_CONVERSION_CTRL_START);
	if (ret)
		return ret;

	/* Give it 1ms more than calculated settling time. */
	remaining = wait_for_completion_timeout(&data->drdy_completion,
						usecs_to_jiffies(settle_time_us +
								 1 * USEC_PER_MSEC));

	return remaining ? 0 : -ETIMEDOUT;
}

static int ads112c14_wait_for_conversion_poll(struct ads112c14_data *data,
					      u32 settle_time_us)
{
	u32 reg_val;
	int ret;

	ret = regmap_write(data->regmap, ADS112C14_REG_CONVERSION_CTRL,
			   ADS112C14_CONVERSION_CTRL_START);
	if (ret)
		return ret;

	/* Give it 1ms more than calculated settling time. */
	return regmap_read_poll_timeout(data->regmap,
					ADS112C14_REG_STATUS_MSB, reg_val,
					FIELD_GET(ADS112C14_STATUS_MSB_DRDY, reg_val),
					1 * USEC_PER_MSEC,
					settle_time_us + 1 * USEC_PER_MSEC);
}

static int ads112c14_single_conversion(struct ads112c14_data *data,
				       const struct iio_chan_spec *chan,
				       u8 *buf, bool en_burnout, bool for_scan)
{
	struct i2c_client *client = to_i2c_client(regmap_get_device(data->regmap));
	struct ads112c14_channel_state *channel_state;
	u32 settle_time_us;
	int ret;

	guard(mutex)(&data->lock);

	ret = ads112c14_prepare_channel(data, chan, en_burnout);
	if (ret)
		return ret;

	channel_state = &data->channel_states[chan->scan_index];
	ret = ads112c14_get_settling_time_us(data, channel_state,
					     channel_state->delay,
					     &settle_time_us);
	if (ret)
		return ret;

	ret = regmap_test_bits(data->regmap, ADS112C14_REG_DATA_RATE_CFG,
			       ADS112C14_DATA_RATE_CFG_GC_EN);
	if (ret < 0)
		return ret;

	/* Input chopping doubles the settling time. */
	if (ret)
		settle_time_us *= 2;

	if (data->drdy_irq)
		ret = ads112c14_wait_for_conversion_irq(data, settle_time_us);
	else
		ret = ads112c14_wait_for_conversion_poll(data, settle_time_us);
	if (ret)
		return ret;

	/*
	 * When doing buffered read, we don't check the CRC, but rather pass it
	 * along with the raw data. This way, we don't silently drop samples
	 * with CRC errors, but rather leave it to userspace to decide what to
	 * do.
	 */
	if (for_scan)
		return ads112c14_scan_read(data, buf);

	return ads112c14_i2c_read_bytes(client, ADS112C14_CMD_RDATA, buf,
					BITS_TO_BYTES(data->chip_info->resolution_bits),
					data->i2c_crc_enabled);
}

static bool ads112c14_using_drdy_trigger(struct iio_dev *indio_dev)
{
	struct ads112c14_data *data = iio_priv(indio_dev);

	return data->drdy_trig && indio_dev->trig == data->drdy_trig;
}

static int ads112c14_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2, long mask)
{
	struct ads112c14_data *data = iio_priv(indio_dev);
	struct ads112c14_measurement *measurement = NULL;
	const int *scale_avail;
	u32 vref_uV, fsr_bits;

	/* Selecting V_REF source is not implemented yet. */
	vref_uV = ADS112C14_INT_REF1_mV * (MICRO / MILLI);

	if (chan->channel < ADS112C14_SYS_MON_CHANNEL_BASE) {
		measurement = &data->measurements[chan->scan_index];
		fsr_bits = data->chip_info->resolution_bits - measurement->bipolar;
	} else {
		/* All SYS_MON channels are using signed coding. */
		fsr_bits = data->chip_info->resolution_bits - 1;
	}

	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
		u8 buf[3];
		int ret;

		IIO_DEV_ACQUIRE_DIRECT_MODE(indio_dev, claim);
		if (IIO_DEV_ACQUIRE_FAILED(claim))
			return -EBUSY;

		ret = ads112c14_single_conversion(data, chan, buf, false, false);
		if (ret)
			return ret;

		switch (data->chip_info->resolution_bits) {
		case 16:
			*val = get_unaligned_be16(buf);
			break;
		case 24:
			*val = get_unaligned_be24(buf);
			break;
		default:
			return -EINVAL;
		}

		if (!measurement || measurement->bipolar)
			*val = sign_extend32(*val, fsr_bits);

		return IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_TEMP) {
			/* TS_TC (typical) = 405 uV/°C */
			*val = MILLI * vref_uV / 405;
			*val2 = fsr_bits;
			return IIO_VAL_FRACTIONAL_LOG2;
		}

		if (chan->channel < ADS112C14_SYS_MON_CHANNEL_BASE) {
			guard(mutex)(&data->lock);

			scale_avail = measurement->scale_available[measurement->gain_val];
			*val = scale_avail[0];
			*val2 = scale_avail[1];

			return IIO_VAL_DECIMAL64_PICO;
		}

		if (chan->channel == ADS112C14_SYS_MON_CHANNEL_SHORT) {
			u8 idx;

			guard(mutex)(&data->lock);

			idx = data->sys_mon_chan_short_gain_val;
			scale_avail = data->sys_mon_chan_short_scale_available[idx];
			*val = scale_avail[0];
			*val2 = scale_avail[1];

			return IIO_VAL_DECIMAL64_PICO;
		}

		*val = vref_uV / (MICRO / MILLI);

		/*
		 * Some SYS_MON channels (ext ref, AVDD, DVDD) need to be
		 * multiplied by 8 to account for internal attenuation of / 8.
		 */
		switch (chan->address) {
		case 3 ... 5:
			*val2 = fsr_bits - 3;
			break;
		default:
			*val2 = fsr_bits;
			break;
		}

		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_OFFSET:
		/* Only the temperature channel has an offset. */
		if (chan->type != IIO_TEMP)
			return -EINVAL;
		/*
		 * Die temperature [°C] = 25°C + (Measured voltage – TS_Offset) / TS_TC
		 * TS_TC (typical) = 405 uV/°C
		 * TS_Offset (typical) = 119.5 mV
		 */
		*val = div_s64((s64)(25 * 405 - 119500) * BIT(fsr_bits), vref_uV);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SAMP_FREQ: {
		struct ads112c14_channel_state *channel_state;
		const int (*available)[2];
		u8 i, j;

		guard(mutex)(&data->lock);

		channel_state = &data->channel_states[chan->scan_index];

		switch (channel_state->filter_osr) {
		case ADS112C14_DATA_RATE_CFG_FLTR_OSR_16...ADS112C14_DATA_RATE_CFG_FLTR_OSR_32:
			j = channel_state->filter_osr;
			available = data->sinc4_sample_rate_available[j];
			i = channel_state->speed_mode;
			break;
		case ADS112C14_DATA_RATE_CFG_FLTR_OSR_128...ADS112C14_DATA_RATE_CFG_FLTR_OSR_1024:
			j = channel_state->filter_osr - ADS112C14_DATA_RATE_CFG_FLTR_OSR_128;
			available = data->sinc4_sinc1_sample_rate_available[j];
			i = channel_state->speed_mode;
			break;
		case ADS112C14_DATA_RATE_CFG_FLTR_OSR_25SPS:
		case ADS112C14_DATA_RATE_CFG_FLTR_OSR_20SPS:
			available = data->sinc4_sinc1_pf1_sample_rate_available;
			i = channel_state->filter_osr - ADS112C14_DATA_RATE_CFG_FLTR_OSR_25SPS;
			break;
		default:
			return -EINVAL;
		}

		*val = available[i][0];
		*val2 = available[i][1];
		return IIO_VAL_INT_PLUS_MICRO;
	}
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO: {
		guard(mutex)(&data->lock);

		*val = ads112c14_get_osr(&data->channel_states[chan->scan_index]);
		return IIO_VAL_INT;
	}
	default:
		return -EINVAL;
	}
}

static int ads112c14_read_avail(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan, const int **vals,
				int *type, int *length, long mask)
{
	struct ads112c14_data *data = iio_priv(indio_dev);
	struct ads112c14_channel_state *channel_state;

	channel_state = &data->channel_states[chan->scan_index];

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		if (chan->channel < ADS112C14_SYS_MON_CHANNEL_BASE) {
			struct ads112c14_measurement *measurement;

			guard(mutex)(&data->lock);

			measurement = &data->measurements[chan->scan_index];
			*vals = (const int *)measurement->scale_available;
			*length = 2 * ARRAY_SIZE(measurement->scale_available);
			*type = IIO_VAL_DECIMAL64_PICO;
			return IIO_AVAIL_LIST;
		}

		if (chan->channel == ADS112C14_SYS_MON_CHANNEL_SHORT) {
			guard(mutex)(&data->lock);

			*vals = (const int *)data->sys_mon_chan_short_scale_available;
			*length = 2 * ARRAY_SIZE(data->sys_mon_chan_short_scale_available);
			*type = IIO_VAL_DECIMAL64_PICO;
			return IIO_AVAIL_LIST;
		}

		return -EINVAL;

	case IIO_CHAN_INFO_SAMP_FREQ: {
		guard(mutex)(&data->lock);

		switch (channel_state->filter_osr) {
		case ADS112C14_DATA_RATE_CFG_FLTR_OSR_16...ADS112C14_DATA_RATE_CFG_FLTR_OSR_32:
			*vals = (const int *)data->sinc4_sample_rate_available[channel_state->filter_osr];
			*length = 2 * ARRAY_SIZE(data->sinc4_sample_rate_available[0]);
			*type = IIO_VAL_INT_PLUS_MICRO;
			return IIO_AVAIL_LIST;
		case ADS112C14_DATA_RATE_CFG_FLTR_OSR_128...ADS112C14_DATA_RATE_CFG_FLTR_OSR_1024:
			*vals = (const int *)data->sinc4_sinc1_sample_rate_available[channel_state->filter_osr - ADS112C14_DATA_RATE_CFG_FLTR_OSR_128];
			*length = 2 * ARRAY_SIZE(data->sinc4_sinc1_sample_rate_available[0]);
			*type = IIO_VAL_INT_PLUS_MICRO;
			return IIO_AVAIL_LIST;
		case ADS112C14_DATA_RATE_CFG_FLTR_OSR_25SPS...ADS112C14_DATA_RATE_CFG_FLTR_OSR_20SPS:
			*vals = (const int *)data->sinc4_sinc1_pf1_sample_rate_available;
			*length = 2 * ARRAY_SIZE(data->sinc4_sinc1_pf1_sample_rate_available);
			*type = IIO_VAL_INT_PLUS_MICRO;
			return IIO_AVAIL_LIST;
		default:
			return -EINVAL;
		}
	}
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO: {
		guard(mutex)(&data->lock);

		switch (channel_state->filter_osr) {
		case ADS112C14_DATA_RATE_CFG_FLTR_OSR_16...ADS112C14_DATA_RATE_CFG_FLTR_OSR_32:
			*vals = ads112c14_sinc4_osr_available;
			*length = ARRAY_SIZE(ads112c14_sinc4_osr_available);
			*type = IIO_VAL_INT;
			return IIO_AVAIL_LIST;
		case ADS112C14_DATA_RATE_CFG_FLTR_OSR_128...ADS112C14_DATA_RATE_CFG_FLTR_OSR_1024:
			*vals = ads112c14_sinc4_sinc1_osr_available;
			*length = ARRAY_SIZE(ads112c14_sinc4_sinc1_osr_available);
			*type = IIO_VAL_INT;
			return IIO_AVAIL_LIST;
		case ADS112C14_DATA_RATE_CFG_FLTR_OSR_25SPS:
			*vals = ads112c14_sinc4_sinc1_pf1_25sps_osr_available;
			*length = ARRAY_SIZE(ads112c14_sinc4_sinc1_pf1_25sps_osr_available);
			*type = IIO_VAL_INT;
			return IIO_AVAIL_LIST;
		case ADS112C14_DATA_RATE_CFG_FLTR_OSR_20SPS:
			*vals = ads112c14_sinc4_sinc1_pf1_20sps_osr_available;
			*length = ARRAY_SIZE(ads112c14_sinc4_sinc1_pf1_20sps_osr_available);
			*type = IIO_VAL_INT;
			return IIO_AVAIL_LIST;
		default:
			return -EINVAL;
		}
	}
	default:
		return -EINVAL;
	}
}

static int ads112c14_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan, int val,
			       int val2, long mask)
{
	struct ads112c14_data *data = iio_priv(indio_dev);
	const int (*scale_avail)[2];
	u8 *gain_val;

	IIO_DEV_ACQUIRE_DIRECT_MODE(indio_dev, claim);
	if (IIO_DEV_ACQUIRE_FAILED(claim))
		return -EBUSY;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE: {
		guard(mutex)(&data->lock);

		if (chan->channel < ADS112C14_SYS_MON_CHANNEL_BASE) {
			struct ads112c14_measurement *measurement;

			measurement = &data->measurements[chan->scan_index];
			scale_avail = measurement->scale_available;
			gain_val = &measurement->gain_val;
		} else if (chan->channel == ADS112C14_SYS_MON_CHANNEL_SHORT) {
			scale_avail = data->sys_mon_chan_short_scale_available;
			gain_val = &data->sys_mon_chan_short_gain_val;
		} else {
			return -EINVAL;
		}

		for (u32 i = 0; i < ARRAY_SIZE(ads112c14_pga_gains_x10); i++) {
			if (iio_val_s64_compose(val, val2) ==
			    iio_val_s64_compose(scale_avail[i][0], scale_avail[i][1])) {
				*gain_val = i;
				return 0;
			}
		}

		return -EINVAL;
	}
	case IIO_CHAN_INFO_SAMP_FREQ: {
		struct ads112c14_channel_state *channel_state;
		const int (*available)[2];

		guard(mutex)(&data->lock);

		channel_state = &data->channel_states[chan->scan_index];

		switch (channel_state->filter_osr) {
		case ADS112C14_DATA_RATE_CFG_FLTR_OSR_16...ADS112C14_DATA_RATE_CFG_FLTR_OSR_1024:
			if (channel_state->filter_osr < ADS112C14_DATA_RATE_CFG_FLTR_OSR_128) {
				u8 idx = channel_state->filter_osr;

				available = data->sinc4_sample_rate_available[idx];
			} else {
				u8 idx = channel_state->filter_osr - ADS112C14_DATA_RATE_CFG_FLTR_OSR_128;

				available = data->sinc4_sinc1_sample_rate_available[idx];
			}

			for (unsigned int i = 0; i < ARRAY_SIZE(ads112c14_fmod_div); i++) {
				if (val == available[i][0] && val2 == available[i][1]) {
					channel_state->speed_mode = i;
					return 0;
				}
			}

			return -EINVAL;
		case ADS112C14_DATA_RATE_CFG_FLTR_OSR_25SPS:
		case ADS112C14_DATA_RATE_CFG_FLTR_OSR_20SPS: {
			available = data->sinc4_sinc1_pf1_sample_rate_available;

			for (unsigned int i = 0; i < ARRAY_SIZE(data->sinc4_sinc1_pf1_sample_rate_available); i++) {
				if (val == available[i][0] && val2 == available[i][1]) {
					channel_state->filter_osr = i + ADS112C14_DATA_RATE_CFG_FLTR_OSR_25SPS;
					return 0;
				}
			}

			return -EINVAL;
		}
		default:
			return -EINVAL;
		}
	}
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO: {
		struct ads112c14_channel_state *channel_state;

		guard(mutex)(&data->lock);

		channel_state = &data->channel_states[chan->scan_index];

		switch (channel_state->filter_osr) {
		case ADS112C14_DATA_RATE_CFG_FLTR_OSR_16...ADS112C14_DATA_RATE_CFG_FLTR_OSR_32:
			for (unsigned int i = 0; i < ARRAY_SIZE(ads112c14_sinc4_osr_available); i++) {
				if (val == ads112c14_sinc4_osr_available[i]) {
					channel_state->filter_osr = i;
					return 0;
				}
			}

			return -EINVAL;
		case ADS112C14_DATA_RATE_CFG_FLTR_OSR_128...ADS112C14_DATA_RATE_CFG_FLTR_OSR_1024:
			for (unsigned int i = 0; i < ARRAY_SIZE(ads112c14_sinc4_sinc1_osr_available); i++) {
				if (val == ads112c14_sinc4_sinc1_osr_available[i]) {
					channel_state->filter_osr = i + ADS112C14_DATA_RATE_CFG_FLTR_OSR_128;
					return 0;
				}
			}

			return -EINVAL;
		case ADS112C14_DATA_RATE_CFG_FLTR_OSR_25SPS:
			for (unsigned int i = 0; i < ARRAY_SIZE(ads112c14_sinc4_sinc1_pf1_25sps_osr_available); i++) {
				if (val == ads112c14_sinc4_sinc1_pf1_25sps_osr_available[i]) {
					channel_state->speed_mode = i;
					return 0;
				}
			}

			return -EINVAL;
		case ADS112C14_DATA_RATE_CFG_FLTR_OSR_20SPS:
			for (unsigned int i = 0; i < ARRAY_SIZE(ads112c14_sinc4_sinc1_pf1_20sps_osr_available); i++) {
				if (val == ads112c14_sinc4_sinc1_pf1_20sps_osr_available[i]) {
					channel_state->speed_mode = i;
					return 0;
				}
			}

			return -EINVAL;
		default:
			return -EINVAL;
		}
	}
	default:
		return -EINVAL;
	}
}

static int ads112c14_write_raw_get_fmt(struct iio_dev *indio_dev,
				       struct iio_chan_spec const *chan,
				       long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_DECIMAL64_PICO;
	default:
		return IIO_VAL_INT_PLUS_MICRO;
	}
}

static int ads112c14_update_scan_mode(struct iio_dev *indio_dev,
				      const unsigned long *scan_mask)
{
	/* Only continuous mode is limited to a single channel. */
	if (!ads112c14_using_drdy_trigger(indio_dev))
		return 0;

	if (!iio_validate_scan_mask_onehot(indio_dev, scan_mask))
		return -EINVAL;

	return 0;
}

static int ads112c14_debugfs_reg_access(struct iio_dev *indio_dev,
					unsigned int reg,
					unsigned int writeval,
					unsigned int *readval)
{
	struct ads112c14_data *data = iio_priv(indio_dev);

	if (readval)
		return regmap_read(data->regmap, reg, readval);

	return regmap_write(data->regmap, reg, writeval);
}

static int ads112c14_read_label(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan, char *label)
{
	struct ads112c14_data *data = iio_priv(indio_dev);
	const char *label_source;

	/* measurement channels */
	if (chan->channel < ADS112C14_SYS_MON_CHANNEL_BASE) {
		struct ads112c14_measurement *measurement;

		measurement = &data->measurements[chan->scan_index];
		if (!measurement->label)
			return -EINVAL;

		return sysfs_emit(label, "%s\n", measurement->label);
	}

	/* System monitor channels. */
	switch (chan->channel) {
	case ADS112C14_SYS_MON_CHANNEL_TEMP:
		label_source = "Internal temperature sensor";
		break;
	case ADS112C14_SYS_MON_CHANNEL_EXT_REF:
		label_source = "External reference";
		break;
	case ADS112C14_SYS_MON_CHANNEL_AVDD:
		label_source = "AVDD";
		break;
	case ADS112C14_SYS_MON_CHANNEL_DVDD:
		label_source = "DVDD";
		break;
	case ADS112C14_SYS_MON_CHANNEL_SHORT:
		label_source = "Internal short (internal reference source)";
		break;
	default:
		return -EINVAL;
	}

	return sysfs_emit(label, "%s\n", label_source);
}

static const struct iio_chan_spec *
ads112c14_first_active_channel(struct iio_dev *indio_dev)
{
	unsigned int scan_mask_len = iio_get_masklength(indio_dev);
	unsigned int i;

	i = find_first_bit(indio_dev->active_scan_mask, scan_mask_len);
	if (i == scan_mask_len)
		return NULL;

	return &indio_dev->channels[i];
}

static irqreturn_t ads112c14_trigger_handler(int irq, void *private)
{
	struct iio_poll_func *pf = private;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ads112c14_data *data = iio_priv(indio_dev);
	u32 offset = 0;
	u32 i;
	int ret;

	if (ads112c14_using_drdy_trigger(indio_dev)) {
		const struct iio_chan_spec *chan;

		chan = ads112c14_first_active_channel(indio_dev);
		if (!chan)
			goto out;

		ret = ads112c14_scan_read(data, (u8 *)&data->scan[0]);
		if (ret) {
			dev_err_once(indio_dev->dev.parent,
				     "failed to read channel %d: %pe; additional errors will be suppressed\n",
				     chan->channel, ERR_PTR(ret));
			goto out;
		}

		iio_push_to_buffers_with_ts(indio_dev, data->scan,
					    sizeof(data->scan), pf->timestamp);
		goto out;
	}

	iio_for_each_active_channel(indio_dev, i) {
		const struct iio_chan_spec *chan = &indio_dev->channels[i];

		ret = ads112c14_single_conversion(data, chan,
						  (u8 *)&data->scan[offset++],
						  false, true);
		if (ret) {
			dev_err_once(indio_dev->dev.parent,
				     "failed to read channel %d: %pe; additional errors will be suppressed\n",
				     chan->channel, ERR_PTR(ret));
			goto out;
		}
	}

	iio_push_to_buffers_with_ts(indio_dev, data->scan,
				    sizeof(data->scan), pf->timestamp);
out:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const struct iio_info ads112c14_info = {
	.read_raw = ads112c14_read_raw,
	.read_avail = ads112c14_read_avail,
	.write_raw = ads112c14_write_raw,
	.write_raw_get_fmt = ads112c14_write_raw_get_fmt,
	.update_scan_mode = ads112c14_update_scan_mode,
	.debugfs_reg_access = ads112c14_debugfs_reg_access,
	.read_label = ads112c14_read_label,
};

static int ads112c14_buffer_postenable(struct iio_dev *indio_dev)
{
	struct ads112c14_data *data = iio_priv(indio_dev);
	const struct iio_chan_spec *chan;
	int ret;

	if (!ads112c14_using_drdy_trigger(indio_dev))
		return 0;

	chan = ads112c14_first_active_channel(indio_dev);
	if (!chan)
		return -EINVAL;

	guard(mutex)(&data->lock);

	ret = ads112c14_prepare_channel(data, chan, false);
	if (ret)
		return ret;

	ret = regmap_update_bits(data->regmap, ADS112C14_REG_DEVICE_CFG,
				 ADS112C14_DEVICE_CFG_CONV_MODE,
				 FIELD_PREP(ADS112C14_DEVICE_CFG_CONV_MODE,
					    ADS112C14_DEVICE_CFG_CONV_MODE_CONTINUOUS));
	if (ret)
		return ret;

	WRITE_ONCE(data->continuous_mode, true);

	ret = regmap_write(data->regmap, ADS112C14_REG_CONVERSION_CTRL,
			   ADS112C14_CONVERSION_CTRL_START);
	if (ret) {
		WRITE_ONCE(data->continuous_mode, false);
		regmap_update_bits(data->regmap, ADS112C14_REG_DEVICE_CFG,
				   ADS112C14_DEVICE_CFG_CONV_MODE,
				   FIELD_PREP(ADS112C14_DEVICE_CFG_CONV_MODE,
					      ADS112C14_DEVICE_CFG_CONV_MODE_SINGLE_SHOT));
		return ret;
	}

	return 0;
}

static int ads112c14_buffer_predisable(struct iio_dev *indio_dev)
{
	struct ads112c14_data *data = iio_priv(indio_dev);
	int ret;

	if (!ads112c14_using_drdy_trigger(indio_dev))
		return 0;

	guard(mutex)(&data->lock);

	WRITE_ONCE(data->continuous_mode, false);

	ret = regmap_write(data->regmap, ADS112C14_REG_CONVERSION_CTRL,
			   ADS112C14_CONVERSION_CTRL_STOP);
	if (ret)
		return ret;

	return regmap_update_bits(data->regmap, ADS112C14_REG_DEVICE_CFG,
				  ADS112C14_DEVICE_CFG_CONV_MODE,
				  FIELD_PREP(ADS112C14_DEVICE_CFG_CONV_MODE,
					     ADS112C14_DEVICE_CFG_CONV_MODE_SINGLE_SHOT));
}

static const struct iio_buffer_setup_ops ads112c14_buffer_setup_ops = {
	.postenable = ads112c14_buffer_postenable,
	.predisable = ads112c14_buffer_predisable,
};

static ssize_t ads112c14_read_burnout_raw(struct iio_dev *indio_dev,
					  uintptr_t private,
					  struct iio_chan_spec const *chan,
					  char *buf)
{
	struct ads112c14_data *data = iio_priv(indio_dev);
	struct ads112c14_measurement *measurement;
	int ret, ret2, val;
	u8 raw_buf[3];

	if (chan->channel >= ADS112C14_SYS_MON_CHANNEL_BASE)
		return -EINVAL;

	measurement = &data->measurements[chan->scan_index];
	if (!measurement->burnout)
		return -EINVAL;

	IIO_DEV_ACQUIRE_DIRECT_MODE(indio_dev, claim);
	if (IIO_DEV_ACQUIRE_FAILED(claim))
		return -EBUSY;

	ret = regmap_update_bits(data->regmap, ADS112C14_REG_DEVICE_CFG,
				 ADS112C14_DEVICE_CFG_BOCS,
				 FIELD_PREP(ADS112C14_DEVICE_CFG_BOCS,
					    measurement->burnout));
	if (ret)
		return ret;

	ret = ads112c14_single_conversion(data, chan, raw_buf, true, false);

	/*
	 * Important to always turn off burnout current even if the conversion
	 * fails so that it does not affect subsequent measurements.
	 */
	ret2 = regmap_update_bits(data->regmap, ADS112C14_REG_DEVICE_CFG,
				  ADS112C14_DEVICE_CFG_BOCS,
				  FIELD_PREP(ADS112C14_DEVICE_CFG_BOCS,
					     ADS112C14_DEVICE_CFG_BOCS_DISABLED));
	if (ret < 0)
		return ret;
	if (ret2)
		return ret2;

	switch (data->chip_info->resolution_bits) {
	case 16:
		val = get_unaligned_be16(raw_buf);
		break;
	case 24:
		val = get_unaligned_be24(raw_buf);
		break;
	default:
		return -EINVAL;
	}

	if (measurement->bipolar)
		val = sign_extend32(val, data->chip_info->resolution_bits - 1);

	return sysfs_emit(buf, "%d\n", val);
}

static int ads112c14_get_filter_type_from_state(struct ads112c14_channel_state *channel_state)
{
	switch (channel_state->filter_osr) {
	case ADS112C14_DATA_RATE_CFG_FLTR_OSR_16...ADS112C14_DATA_RATE_CFG_FLTR_OSR_32:
		return ADS112C14_FILTER_TYPE_SINC4;
	case ADS112C14_DATA_RATE_CFG_FLTR_OSR_128...ADS112C14_DATA_RATE_CFG_FLTR_OSR_1024:
		return ADS112C14_FILTER_TYPE_SINC4_SINC1;
	case ADS112C14_DATA_RATE_CFG_FLTR_OSR_25SPS...ADS112C14_DATA_RATE_CFG_FLTR_OSR_20SPS:
		return ADS112C14_FILTER_TYPE_SINC4_SINC1_PF1;
	default:
		return -EINVAL;
	}
}

static int ads112c14_set_filter_type(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan,
				     unsigned int val)
{
	struct ads112c14_data *data = iio_priv(indio_dev);
	struct ads112c14_channel_state *channel_state;
	int ret;

	IIO_DEV_ACQUIRE_DIRECT_MODE(indio_dev, claim);
	if (IIO_DEV_ACQUIRE_FAILED(claim))
		return -EBUSY;

	guard(mutex)(&data->lock);

	channel_state = &data->channel_states[chan->scan_index];

	ret = ads112c14_get_filter_type_from_state(channel_state);
	if (ret < 0)
		return ret;

	/*
	 * channel_state->filter_osr affects multiple attributes, so don't modify
	 * it if the filter type is already set to the requested value.
	 */
	if (ret == val)
		return 0;

	/* Otherwise, pick an arbitrary default for each type. */
	switch (val) {
	case ADS112C14_FILTER_TYPE_SINC4:
		channel_state->filter_osr = ADS112C14_DATA_RATE_CFG_FLTR_OSR_16;
		break;
	case ADS112C14_FILTER_TYPE_SINC4_SINC1:
		channel_state->filter_osr = ADS112C14_DATA_RATE_CFG_FLTR_OSR_128;
		break;
	case ADS112C14_FILTER_TYPE_SINC4_SINC1_PF1:
		channel_state->filter_osr = ADS112C14_DATA_RATE_CFG_FLTR_OSR_25SPS;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ads112c14_get_filter_type(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan)
{
	struct ads112c14_data *data = iio_priv(indio_dev);
	struct ads112c14_channel_state *channel_state;

	guard(mutex)(&data->lock);

	channel_state = &data->channel_states[chan->scan_index];

	return ads112c14_get_filter_type_from_state(channel_state);
}

static const struct iio_enum ads112c14_filter_type_enum = {
	.items = ads112c14_filter_type_names,
	.num_items = ARRAY_SIZE(ads112c14_filter_type_names),
	.set = ads112c14_set_filter_type,
	.get = ads112c14_get_filter_type,
};

static const struct iio_chan_spec_ext_info ads112c14_ext_info[] = {
	{
		.name = "settlingtime",
		.read = ads112c14_read_settling_time,
		.write = ads112c14_write_settling_time,
		.shared = IIO_SEPARATE,
	},
	{
		.name = "settlingtime_available",
		.read = ads112c14_read_settling_time_available,
		.shared = IIO_SEPARATE,
	},
	IIO_ENUM("filter_type", IIO_SEPARATE, &ads112c14_filter_type_enum),
	IIO_ENUM_AVAILABLE("filter_type", IIO_SEPARATE, &ads112c14_filter_type_enum),
	{ }
};

static const struct iio_chan_spec_ext_info ads112c14_ext_info_burnout[] = {
	{
		.name = "burnoutraw",
		.read = ads112c14_read_burnout_raw,
	},
	{
		.name = "settlingtime",
		.read = ads112c14_read_settling_time,
		.write = ads112c14_write_settling_time,
		.shared = IIO_SEPARATE,
	},
	{
		.name = "settlingtime_available",
		.read = ads112c14_read_settling_time_available,
		.shared = IIO_SEPARATE,
	},
	IIO_ENUM("filter_type", IIO_SEPARATE, &ads112c14_filter_type_enum),
	IIO_ENUM_AVAILABLE("filter_type", IIO_SEPARATE, &ads112c14_filter_type_enum),
	{ }
};

static int ads112c14_populate_idac_mag(u32 current_nA, u8 *idac_mag)
{
	u32 current_uA = current_nA / (NANO / MICRO);

	/* Convert microamps to IMAG bits */
	if (current_uA == 1)
		*idac_mag = 1;
	else if (in_range(current_uA, 10, 100) && current_uA % 10 == 0)
		*idac_mag = current_uA / 10 + 1;
	else
		return dev_err_probe(NULL, -EINVAL,
				     "invalid excitation-current-nanoamp value\n");

	return 0;
}

static int ads112c14_parse_channels(struct iio_dev *indio_dev,
				    bool *need_avdd_ref, bool *need_ext_ref)
{
	struct ads112c14_data *data = iio_priv(indio_dev);
	struct device *dev = indio_dev->dev.parent;
	struct iio_chan_spec *channels;
	u32 num_child_nodes, num_data_chans, i, pair[2];
	int ret;

	*need_avdd_ref = false;
	*need_ext_ref = false;

	num_child_nodes = device_get_named_child_node_count(dev, "channel");

	data->measurements = devm_kcalloc(dev, num_child_nodes,
					  sizeof(*data->measurements), GFP_KERNEL);
	if (!data->measurements)
		return -ENOMEM;

	num_data_chans = num_child_nodes + ARRAY_SIZE(ads112c14_sys_mon_channels);

	data->channel_states = devm_kcalloc(dev, num_data_chans,
					    sizeof(*data->channel_states),
					    GFP_KERNEL);
	if (!data->channel_states)
		return -ENOMEM;

	channels = devm_kcalloc(dev, num_data_chans + 1,
				sizeof(*channels), GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	i = 0;
	device_for_each_named_child_node_scoped(dev, child, "channel") {
		struct ads112c14_measurement *measurement = &data->measurements[i];
		struct iio_chan_spec *spec = &channels[i];
		const char *propname;

		spec->indexed = 1;
		spec->scan_index = i;
		spec->ext_info = ads112c14_ext_info;
		measurement->gain_val = 1;
		data->channel_states[i].filter_osr = ADS112C14_DATA_RATE_CFG_FLTR_OSR_16;

		if (fwnode_property_present(child, "label")) {
			ret = fwnode_property_read_string(child, "label", &measurement->label);
			if (ret)
				return dev_err_probe(dev, ret,
						     "failed to read label property\n");
		}

		if (fwnode_property_present(child, "single-channel")) {
			ret = fwnode_property_read_u32(child, "single-channel",
						       &pair[0]);
			if (ret)
				return dev_err_probe(dev, ret,
						     "failed to read single-channel property\n");

			if (pair[0] >= 8)
				return dev_err_probe(dev, -EINVAL,
						     "single-channel value must be between 0 and 7\n");

			spec->channel = pair[0];
			/*
			 * NB: channel2 is unused by iio core code in this case.
			 * Let's us avoid special case for negative input mux
			 * for single-ended channels when taking measurements.
			 */
			spec->channel2 = ADS112C14_MUX_CFG_AIN_GND;
		} else if (fwnode_property_present(child, "diff-channels")) {
			ret = fwnode_property_read_u32_array(child, "diff-channels",
							     pair, ARRAY_SIZE(pair));
			if (ret)
				return dev_err_probe(dev, ret,
						     "failed to read diff-channels property\n");

			if (pair[0] >= 8 || pair[1] >= 8)
				return dev_err_probe(dev, -EINVAL,
						     "diff-channels values must be between 0 and 7\n");

			spec->differential = 1;
			spec->channel = pair[0];
			spec->channel2 = pair[1];
		} else {
			return dev_err_probe(dev, -EINVAL,
					     "channel node missing channel type property\n");
		}

		if (fwnode_property_present(child, "excitation-channels")) {
			ret = fwnode_property_count_u32(child, "excitation-channels");
			if (ret < 0)
				return dev_err_probe(dev, ret,
						     "failed to read excitation-channels property\n");

			if (ret < 1 || ret > 2)
				return dev_err_probe(dev, -EINVAL,
						     "excitation-channels property must have 1 or 2 values\n");

			measurement->iadc_count = ret;
			pair[1] = 0;

			ret = fwnode_property_read_u32_array(child, "excitation-channels",
							     pair, measurement->iadc_count);
			if (ret)
				return dev_err_probe(dev, ret,
						     "failed to read excitation-channels property\n");

			if (pair[0] >= 8 || pair[1] >= 8)
				return dev_err_probe(dev, -EINVAL,
						     "excitation-channels values must be between 0 and 7\n");

			measurement->idac1_mux = pair[0];
			measurement->idac2_mux = measurement->iadc_count > 1 ? pair[1] : 0;

			ret = fwnode_property_read_u32_array(child, "excitation-current-nanoamp",
							     pair, measurement->iadc_count);
			if (ret)
				return dev_err_probe(dev, ret,
						     "failed to read excitation-current-nanoamp property\n");

			if (pair[0] <= 100 * (NANO / MICRO) &&
			    (measurement->iadc_count == 1 || pair[1] <= 100 * (NANO / MICRO))) {
				/*
				 * If both values are 100µA or less, then we can
				 * use IUNIT = 1µA for better precision.
				 */
				ret = ads112c14_populate_idac_mag(pair[0],
								  &measurement->idac1_mag);
				if (ret)
					return ret;

				if (measurement->iadc_count > 1) {
					ret = ads112c14_populate_idac_mag(pair[1],
									  &measurement->idac2_mag);
					if (ret)
						return ret;
				}
			} else {
				/*
				 * Otherwise, IUINT is 10µA (flag set) and so
				 * IxMAG is 1/10 of the actual current.
				 */
				measurement->iunit = 1;

				ret = ads112c14_populate_idac_mag(pair[0] / 10,
								  &measurement->idac1_mag);
				if (ret)
					return ret;

				if (measurement->iadc_count > 1) {
					ret = ads112c14_populate_idac_mag(pair[1] / 10,
									  &measurement->idac2_mag);
					if (ret)
						return ret;
				}
			}
		}

		measurement->bipolar = fwnode_property_read_bool(child, "bipolar");
		measurement->global_chop = fwnode_property_read_bool(child,
								     "input-chopping");

		propname = "burn-out-current-nanoamp";
		if (fwnode_property_present(child, propname)) {
			u32 burnout_nA;

			ret = fwnode_property_read_u32(child, propname, &burnout_nA);
			if (ret)
				return dev_err_probe(dev, ret,
						     "failed to read %s property\n",
						     propname);

			switch (burnout_nA) {
			case 200:
				measurement->burnout = ADS112C14_DEVICE_CFG_BOCS_200_nA;
				break;
			case 1000:
				measurement->burnout = ADS112C14_DEVICE_CFG_BOCS_1_uA;
				break;
			case 10000:
				measurement->burnout = ADS112C14_DEVICE_CFG_BOCS_10_uA;
				break;
			default:
				return dev_err_probe(dev, -EINVAL,
						     "invalid %s value\n", propname);
			}

			if (measurement->burnout != ADS112C14_DEVICE_CFG_BOCS_DISABLED)
				spec->ext_info = ads112c14_ext_info_burnout;
		}

		if (fwnode_property_present(child, "reference-sources")) {
			ret = fwnode_property_match_property_string(child,
				"reference-sources", ads112c14_vref_source_names,
				ARRAY_SIZE(ads112c14_vref_source_names));
			if (ret < 0)
				return dev_err_probe(dev, ret,
						     "invalid reference-sources value\n");

			measurement->vref_source = ret;
		}

		if (measurement->vref_source == ADS112C14_VREF_SOURCE_AVDD)
			*need_avdd_ref = true;
		if (measurement->vref_source == ADS112C14_VREF_SOURCE_EXTERNAL)
			*need_ext_ref = true;

		spec->info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
					   BIT(IIO_CHAN_INFO_SCALE) |
					   BIT(IIO_CHAN_INFO_SAMP_FREQ) |
					   BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO);
		spec->info_mask_separate_available = BIT(IIO_CHAN_INFO_SCALE) |
						     BIT(IIO_CHAN_INFO_SAMP_FREQ) |
						     BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO);

		/*
		 * If reference source is resistor rather than voltage supply,
		 * then the measurement is effectively a resistance measurement.
		 */
		spec->type = (measurement->vref_source == ADS112C14_VREF_SOURCE_EXTERNAL &&
			      data->ext_ref_ohms) ? IIO_RESISTANCE : IIO_VOLTAGE;

		if (spec->type == IIO_RESISTANCE)
			spec->differential = 0;

		spec->scan_type = (struct iio_scan_type){
			.format = measurement->bipolar ?
				  IIO_SCAN_FORMAT_SIGNED_INT :
				  IIO_SCAN_FORMAT_UNSIGNED_INT,
			.realbits = data->chip_info->resolution_bits,
			.storagebits = 32,
			.shift = 32 - data->chip_info->resolution_bits,
			.endianness = IIO_BE,
		};

		i++;
	}

	data->num_measurements = i;
	if (data->num_measurements > ADS112C14_MAX_MEASUREMENT_CHANNELS)
		return dev_err_probe(dev, -EINVAL,
				     "too many measurement channels defined\n");

	memcpy(channels + i, ads112c14_sys_mon_channels, sizeof(ads112c14_sys_mon_channels));

	for (u32 j = 0; j < ARRAY_SIZE(ads112c14_sys_mon_channels); j++) {
		struct iio_chan_spec *spec = &channels[i];
		struct ads112c14_channel_state *channel_state;

		channel_state = &data->channel_states[i];
		channel_state->filter_osr = ADS112C14_DATA_RATE_CFG_FLTR_OSR_16;

		/* Update the template that was already copied with dynamic values. */
		spec->scan_index = i;
		spec->scan_type = (struct iio_scan_type){
			.format = IIO_SCAN_FORMAT_SIGNED_INT,
			.realbits = data->chip_info->resolution_bits,
			.storagebits = 32,
			.shift = 32 - data->chip_info->resolution_bits,
			.endianness = IIO_BE,
		};

		i++;
	}

	channels[i] = IIO_CHAN_SOFT_TIMESTAMP(i);
	i++;

	indio_dev->channels = channels;
	indio_dev->num_channels = i;

	return 0;
}

static void ads112c14_populate_scale_available(int (*scale_avail)[2],
					       u32 full_scale, u32 fsr_bits)
{
	for (u32 i = 0; i < ARRAY_SIZE(ads112c14_pga_gains_x10); i++) {
		u64 gain_x10 = ads112c14_pga_gains_x10[i];
		s64 scale;

		scale = div64_u64((u64)PICO * 10U * full_scale,
				  gain_x10 * BIT(fsr_bits));

		iio_val_s64_decompose(scale, &scale_avail[i][0],
				      &scale_avail[i][1]);
	}
}

static void ads112c14_populate_odr_tables(struct ads112c14_data *data)
{
	int *available;
	u32 osr, fmod_Hz;
	u64 odr_uHz;
	u32 rem;

	for (unsigned int i = 0; i < ARRAY_SIZE(ads112c14_sinc4_osr_available); i++) {
		osr = ads112c14_sinc4_osr_available[i];

		for (unsigned int j = 0; j < ARRAY_SIZE(ads112c14_fmod_div); j++) {
			fmod_Hz = data->fclk_Hz / ads112c14_fmod_div[j];
			odr_uHz = div_u64((u64)fmod_Hz * MICRO, osr);
			available = data->sinc4_sample_rate_available[i][j];
			available[0] = div_u64_rem(odr_uHz, MICRO, &rem);
			available[1] = rem;
		}
	}

	for (unsigned int i = 0; i < ARRAY_SIZE(ads112c14_sinc4_sinc1_osr_available); i++) {
		osr = ads112c14_sinc4_sinc1_osr_available[i];

		for (unsigned int j = 0; j < ARRAY_SIZE(ads112c14_fmod_div); j++) {
			fmod_Hz = data->fclk_Hz / ads112c14_fmod_div[j];
			odr_uHz = div_u64((u64)fmod_Hz * MICRO, osr);
			available = data->sinc4_sinc1_sample_rate_available[i][j];
			available[0] = div_u64_rem(odr_uHz, MICRO, &rem);
			available[1] = rem;
		}
	}

	odr_uHz = div_u64((u64)25 * data->fclk_Hz * MICRO, ADS112C14_INTERNAL_CLK_Hz);
	available = data->sinc4_sinc1_pf1_sample_rate_available[0];
	available[0] = div_u64_rem(odr_uHz, MICRO, &rem);
	available[1] = rem;

	odr_uHz = div_u64((u64)20 * data->fclk_Hz * MICRO, ADS112C14_INTERNAL_CLK_Hz);
	available = data->sinc4_sinc1_pf1_sample_rate_available[1];
	available[0] = div_u64_rem(odr_uHz, MICRO, &rem);
	available[1] = rem;
}

static void ads112c14_populate_settling_range_tables(struct ads112c14_data *data)
{
	s32 (*avail)[2];
	u32 i, j;

	for (i = 0; i < ARRAY_SIZE(ads112c14_sinc_latency_tmod); i++) {
		for (j = 0; j < ARRAY_SIZE(ads112c14_fmod_div); j++) {
			u64 fmod_Hz = data->fclk_Hz / ads112c14_fmod_div[j];
			u64 start_tmod = ads112c14_sinc_latency_tmod[i][j];
			u64 step_tmod = ads112c14_delay_to_tmod(1);
			u64 stop_tmod = start_tmod + ads112c14_delay_to_tmod(ADS112C14_DELAY_MAX);
			s64 start_us, step_us, stop_us;

			start_us = DIV_ROUND_CLOSEST_ULL(start_tmod * USEC_PER_SEC, fmod_Hz);
			step_us = DIV_ROUND_CLOSEST_ULL(step_tmod * USEC_PER_SEC, fmod_Hz);
			stop_us = DIV_ROUND_CLOSEST_ULL(stop_tmod * USEC_PER_SEC, fmod_Hz);

			avail = data->sinc_settling_time_range_available[i][j];

			iio_val_s64_decompose(start_us, &avail[0][0], &avail[0][1]);
			iio_val_s64_decompose(step_us, &avail[1][0], &avail[1][1]);
			iio_val_s64_decompose(stop_us, &avail[2][0], &avail[2][1]);
		}
	}

	for (i = 0; i < ARRAY_SIZE(ads112c14_fir_latency_tmod); i++) {
		for (j = 0; j < ARRAY_SIZE(ads112c14_fmod_div); j++) {
			u64 fmod_Hz = data->fclk_Hz / ads112c14_fmod_div[j];
			u64 start_tmod = ads112c14_fir_latency_tmod[i][j];
			u64 step_tmod = ads112c14_delay_to_tmod(1);
			u64 stop_tmod = start_tmod + ads112c14_delay_to_tmod(ADS112C14_DELAY_MAX);
			s64 start_us, step_us, stop_us;

			start_us = DIV_ROUND_CLOSEST_ULL(start_tmod * USEC_PER_SEC, fmod_Hz);
			step_us = DIV_ROUND_CLOSEST_ULL(step_tmod * USEC_PER_SEC, fmod_Hz);
			stop_us = DIV_ROUND_CLOSEST_ULL(stop_tmod * USEC_PER_SEC, fmod_Hz);

			avail = data->fir_settling_time_range_available[i][j];

			iio_val_s64_decompose(start_us, &avail[0][0], &avail[0][1]);
			iio_val_s64_decompose(step_us, &avail[1][0], &avail[1][1]);
			iio_val_s64_decompose(stop_us, &avail[2][0], &avail[2][1]);
		}
	}
}

static void ads112c14_populate_tables(struct ads112c14_data *data)
{
	u32 full_scale, fsr_bits;

	for (u32 i = 0; i < data->num_measurements; i++) {
		struct ads112c14_measurement *measurement = &data->measurements[i];

		switch (measurement->vref_source) {
		case ADS112C14_VREF_SOURCE_EXTERNAL:
			if (data->ext_ref_ohms)
				full_scale = data->ext_ref_ohms;
			else
				full_scale = data->ext_ref_uV / (MICRO / MILLI);
			break;
		case ADS112C14_VREF_SOURCE_AVDD:
			full_scale = data->avdd_uV / (MICRO / MILLI);
			break;
		case ADS112C14_VREF_SOURCE_INTERNAL_1_25V:
			full_scale = ADS112C14_INT_REF0_mV;
			break;
		default:
			full_scale = ADS112C14_INT_REF1_mV;
			break;
		}

		fsr_bits = data->chip_info->resolution_bits - measurement->bipolar;

		ads112c14_populate_scale_available(measurement->scale_available,
						   full_scale, fsr_bits);
	}

	/* For now, assuming all sys_mon channels are using 2.5V reference. */
	full_scale = ADS112C14_INT_REF1_mV;
	fsr_bits = data->chip_info->resolution_bits - 1;

	ads112c14_populate_scale_available(data->sys_mon_chan_short_scale_available,
					   full_scale, fsr_bits);
	ads112c14_populate_odr_tables(data);
	ads112c14_populate_settling_range_tables(data);
}

static int ads112c14_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	const struct ads112c14_chip_info *info;
	struct iio_dev *indio_dev;
	struct ads112c14_data *data;
	struct clk *clk;
	bool need_avdd_ref, need_ext_ref;
	u32 refp_uV = 0;
	u32 refn_uV = 0;
	u32 reg_val;
	int ret;

	info = i2c_get_match_data(client);
	if (!info)
		return dev_err_probe(dev, -ENODEV, "missing match data\n");

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->chip_info = info;

	ret = devm_mutex_init(dev, &data->lock);
	if (ret)
		return ret;

	if (device_property_present(dev, "ti,refp-refn-resistor-ohms")) {
		ret = device_property_read_u32(dev, "ti,refp-refn-resistor-ohms",
					       &data->ext_ref_ohms);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to read ti,refp-refn-resistor-ohms property\n");
	}

	ret = ads112c14_parse_channels(indio_dev, &need_avdd_ref, &need_ext_ref);
	if (ret)
		return ret;

	ret = devm_regulator_get_enable(dev, "dvdd");
	if (ret)
		return dev_err_probe(dev, ret, "failed to get dvdd regulator\n");

	if (need_avdd_ref) {
		ret = devm_regulator_get_enable_read_voltage(dev, "avdd");
		if (ret < 0)
			return dev_err_probe(dev, ret, "failed to get avdd voltage\n");

		data->avdd_uV = ret;
	} else {
		ret = devm_regulator_get_enable(dev, "avdd");
		if (ret)
			return dev_err_probe(dev, ret, "failed to get avdd regulator\n");
	}

	if (device_property_present(dev, "refp-supply")) {
		ret = devm_regulator_get_enable_read_voltage(dev, "refp");
		if (ret < 0)
			return dev_err_probe(dev, ret, "failed to get refp voltage\n");

		refp_uV = ret;

		struct fwnode_handle *refp_fwnode __free(fwnode_handle) =
			fwnode_find_reference(dev->fwnode, "refp-supply", 0);
		if (IS_ERR(refp_fwnode))
			return dev_err_probe(dev, PTR_ERR(refp_fwnode),
					     "failed to get refp fwnode\n");

		struct fwnode_handle *avdd_fwnode __free(fwnode_handle) =
			fwnode_find_reference(dev->fwnode, "avdd-supply", 0);
		if (IS_ERR(avdd_fwnode))
			return dev_err_probe(dev, PTR_ERR(avdd_fwnode),
					     "failed to get avdd fwnode\n");

		/* REFP buffer should not be enabled when connected to AVDD */
		data->refp_is_avdd = refp_fwnode == avdd_fwnode;
	}

	if (device_property_present(dev, "refn-supply")) {
		ret = devm_regulator_get_enable_read_voltage(dev, "refn");
		if (ret < 0)
			return dev_err_probe(dev, ret, "failed to get refn voltage\n");

		refn_uV = ret;
	} else {
		data->refn_is_gnd = true;
	}

	data->ext_ref_uV = refp_uV - refn_uV;

	if (data->ext_ref_uV && data->ext_ref_ohms)
		return dev_err_probe(dev, -EINVAL,
				     "ti,refp-refn-resistor-ohms property should not be present when refp-supply or refn-supply is present\n");

	if (need_ext_ref && !data->ext_ref_uV && !data->ext_ref_ohms)
		return dev_err_probe(dev, -EINVAL,
				     "external reference measurements require either refp-supply or ti,refp-refn-resistor-ohms property\n");

	clk = devm_clk_get_optional_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "failed to get clk\n");

	if (clk) {
		data->fclk_Hz = clk_get_rate(clk);
		if (!data->fclk_Hz)
			return dev_err_probe(dev, -EINVAL, "clk rate is 0\n");
	} else {
		data->fclk_Hz = ADS112C14_INTERNAL_CLK_Hz;
	}

	/* It takes some time for the internal reference to stabilize. */
	fsleep(10 * USEC_PER_MSEC);

	data->regmap = devm_regmap_init(dev, &ads112c14_regmap_bus, data,
					&ads112c14_regmap_config);
	if (IS_ERR(data->regmap))
		return dev_err_probe(dev, PTR_ERR(data->regmap),
				     "failed to init regmap\n");

	/*
	 * Write magic reset value (0x16) to ensure known state. The reset may
	 * cause an error because of failing to get the I2C ACK at the end of
	 * the message. The device still gets reset so it is safe to ignore the
	 * return value here. If something else is wrong, later read/write will
	 * likely have the same error.
	 */
	regmap_write(data->regmap, ADS112C14_REG_CONVERSION_CTRL,
		     FIELD_PREP(ADS112C14_CONVERSION_CTRL_RESET, 0x16));

	fsleep(ADS112C14_DELAY_RESET_US);

	ret = regmap_read(data->regmap, ADS112C14_REG_STATUS_MSB, &reg_val);
	if (ret)
		return ret;

	if (FIELD_GET(ADS112C14_STATUS_MSB_RESETN, reg_val))
		return dev_err_probe(dev, -EIO, "reset failed\n");

	/* Default gain after reset is 1. */
	data->sys_mon_chan_short_gain_val = 1;

	/*
	 * Clear reset bit to prepare for next probe. And clear AVDD fault since
	 * that happens on every reset.
	 */
	ret = regmap_write(data->regmap, ADS112C14_REG_STATUS_MSB,
			   ADS112C14_STATUS_MSB_RESETN |
			   ADS112C14_STATUS_MSB_AVDD_UVN);
	if (ret)
		return ret;

	ret = regmap_set_bits(data->regmap, ADS112C14_REG_DIGITAL_CFG,
			      ADS112C14_DIGITAL_CFG_I2C_CRC_EN);
	if (ret)
		return ret;

	data->i2c_crc_enabled = true;

	ret = regmap_read(data->regmap, ADS112C14_REG_DEVICE_ID, &reg_val);
	if (ret)
		return ret;

	if (FIELD_GET(ADS112C14_DEVICE_ID_BITS, reg_val) != info->device_id)
		dev_info(dev, "device ID mismatch, expected 0x%X, got 0x%lX\n",
			 info->device_id,
			 FIELD_GET(ADS112C14_DEVICE_ID_BITS, reg_val));

	ret = regmap_update_bits(data->regmap, ADS112C14_REG_DEVICE_CFG,
				 ADS112C14_DEVICE_CFG_CONV_MODE,
				 FIELD_PREP(ADS112C14_DEVICE_CFG_CONV_MODE,
					    ADS112C14_DEVICE_CFG_CONV_MODE_SINGLE_SHOT));
	if (ret)
		return ret;

	if (fwnode_property_match_string(dev_fwnode(dev), "interrupt-names", "drdy") >= 0) {
		data->drdy_irq = fwnode_irq_get_byname(dev_fwnode(dev), "drdy");
		if (data->drdy_irq < 0)
			return dev_err_probe(dev, data->drdy_irq,
					     "failed to get drdy interrupt\n");

		if (clk)
			return dev_err_probe(dev, -EINVAL,
					     "cannot use both DRDY and CLK - they share the same pin\n");

		/*
		 * REVISIT: would probably need to implement a pin controller in
		 * order to support open drain option here.
		 */
		ret = regmap_update_bits(data->regmap, ADS112C14_REG_GPIO_CFG,
					 ADS112C14_GPIO_CFG_GPIO3_CFG,
					 FIELD_PREP(ADS112C14_GPIO_CFG_GPIO3_CFG,
						    ADS112C14_GPIO_CFG_GPIO_CFG_OUTPUT_PUSH_PULL));
		if (ret)
			return ret;

		ret = regmap_update_bits(data->regmap, ADS112C14_REG_GPIO_DATA_OUTPUT,
					 ADS112C14_GPIO_DATA_OUTPUT_GPIO3_SRC,
					 FIELD_PREP(ADS112C14_GPIO_DATA_OUTPUT_GPIO3_SRC,
						    ADS112C14_GPIO_DATA_OUTPUT_GPIO3_SRC_DRDY));
		if (ret)
			return ret;

		init_completion(&data->drdy_completion);

		data->drdy_trig = devm_iio_trigger_alloc(dev, "%s-dev%d-drdy",
							 info->name,
							 iio_device_id(indio_dev));
		if (!data->drdy_trig)
			return -ENOMEM;

		data->drdy_trig->ops = &ads112c14_trigger_ops;
		iio_trigger_set_drvdata(data->drdy_trig, indio_dev);

		ret = devm_iio_trigger_register(dev, data->drdy_trig);
		if (ret)
			return ret;

		ret = devm_request_irq(dev, data->drdy_irq, ads112c14_drdy_irq_handler,
				       0, dev_name(dev), indio_dev);
		if (ret)
			return ret;
	}

	if (clk) {
		ret = regmap_update_bits(data->regmap, ADS112C14_REG_GPIO_CFG,
					 ADS112C14_GPIO_CFG_GPIO3_CFG,
					 FIELD_PREP(ADS112C14_GPIO_CFG_GPIO3_CFG,
						    ADS112C14_GPIO_CFG_GPIO_CFG_INPUT));
		if (ret)
			return ret;

		ret = regmap_update_bits(data->regmap, ADS112C14_REG_DEVICE_CFG,
					 ADS112C14_DEVICE_CFG_CLK_SEL,
					 FIELD_PREP(ADS112C14_DEVICE_CFG_CLK_SEL,
						    ADS112C14_DEVICE_CFG_CLK_SEL_EXTERNAL));
		if (ret)
			return ret;
	}

	ads112c14_populate_tables(data);

	indio_dev->name = info->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &ads112c14_info;

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
					      iio_pollfunc_store_time,
					      ads112c14_trigger_handler,
					      &ads112c14_buffer_setup_ops);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct ads112c14_chip_info ads112c14_chip_info = {
	.name = "ads112c14",
	.device_id = 0xE,
	.resolution_bits = 16,
};

static const struct ads112c14_chip_info ads122c14_chip_info = {
	.name = "ads122c14",
	.device_id = 0xF,
	.resolution_bits = 24,
};

static const struct of_device_id ads112c14_of_match[] = {
	{ .compatible = "ti,ads112c14", .data = &ads112c14_chip_info },
	{ .compatible = "ti,ads122c14", .data = &ads122c14_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ads112c14_of_match);

static const struct i2c_device_id ads112c14_id[] = {
	{ .name = "ads112c14", .driver_data = (kernel_ulong_t)&ads112c14_chip_info },
	{ .name = "ads122c14", .driver_data = (kernel_ulong_t)&ads122c14_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ads112c14_id);

static int ads112c14_i2c_add_driver(struct i2c_driver *driver)
{
	crc8_populate_msb(ads112c14_crc8_table, ADS112C14_I2C_CRC8_POLYNOMIAL);

	return i2c_add_driver(driver);
}

static struct i2c_driver ads112c14_driver = {
	.driver = {
		.name = "ads112c14",
		.of_match_table = ads112c14_of_match,
	},
	.probe = ads112c14_probe,
	.id_table = ads112c14_id,
};
module_driver(ads112c14_driver, ads112c14_i2c_add_driver, i2c_del_driver);

MODULE_AUTHOR("David Lechner (TI) <dlechner@baylibre.com>");
MODULE_DESCRIPTION("TI ADS112C14 I2C ADC driver");
MODULE_LICENSE("GPL");
