// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPI ADC driver for Analog Devices Inc. AD4695 and similar chips
 *
 * https://www.analog.com/en/products/ad4695.html
 * https://www.analog.com/en/products/ad4696.html
 * https://www.analog.com/en/products/ad4697.html
 * https://www.analog.com/en/products/ad4698.html
 *
 * Copyright 2024 Analog Devices Inc.
 * Copyright 2024 BayLibre, SAS
 */

#include <linux/align.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/buffer-dmaengine.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/minmax.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/offload/consumer.h>
#include <linux/spi/offload/provider.h>
#include <linux/spi/spi.h>
#include <linux/units.h>

#include <dt-bindings/iio/adc/adi,ad4695.h>

/* AD4695 registers */
#define AD4695_REG_SPI_CONFIG_A				0x0000
#define   AD4695_REG_SPI_CONFIG_A_SW_RST		  (BIT(7) | BIT(0))
#define   AD4695_REG_SPI_CONFIG_A_ADDR_DIR		  BIT(5)
#define AD4695_REG_SPI_CONFIG_B				0x0001
#define   AD4695_REG_SPI_CONFIG_B_INST_MODE		  BIT(7)
#define AD4695_REG_DEVICE_TYPE				0x0003
#define AD4695_REG_SCRATCH_PAD				0x000A
#define AD4695_REG_VENDOR_L				0x000C
#define AD4695_REG_VENDOR_H				0x000D
#define AD4695_REG_LOOP_MODE				0x000E
#define AD4695_REG_SPI_CONFIG_C				0x0010
#define   AD4695_REG_SPI_CONFIG_C_MB_STRICT		  BIT(7)
#define AD4695_REG_SPI_STATUS				0x0011
#define AD4695_REG_STATUS				0x0014
#define AD4695_REG_ALERT_STATUS1			0x0015
#define AD4695_REG_ALERT_STATUS2			0x0016
#define AD4695_REG_CLAMP_STATUS				0x001A
#define AD4695_REG_SETUP				0x0020
#define   AD4695_REG_SETUP_LDO_EN			  BIT(4)
#define   AD4695_REG_SETUP_SPI_MODE			  BIT(2)
#define   AD4695_REG_SETUP_SPI_CYC_CTRL			  BIT(1)
#define AD4695_REG_REF_CTRL				0x0021
#define   AD4695_REG_REF_CTRL_OV_MODE			  BIT(7)
#define   AD4695_REG_REF_CTRL_VREF_SET			  GENMASK(4, 2)
#define   AD4695_REG_REF_CTRL_REFHIZ_EN			  BIT(1)
#define   AD4695_REG_REF_CTRL_REFBUF_EN			  BIT(0)
#define AD4695_REG_SEQ_CTRL				0x0022
#define   AD4695_REG_SEQ_CTRL_STD_SEQ_EN		  BIT(7)
#define   AD4695_REG_SEQ_CTRL_NUM_SLOTS_AS		  GENMASK(6, 0)
#define AD4695_REG_AC_CTRL				0x0023
#define AD4695_REG_STD_SEQ_CONFIG			0x0024
#define AD4695_REG_GPIO_CTRL				0x0026
#define AD4695_REG_GP_MODE				0x0027
#define   AD4695_REG_GP_MODE_BUSY_GP_SEL		  BIT(5)
#define   AD4695_REG_GP_MODE_BUSY_GP_EN			  BIT(1)
#define AD4695_REG_TEMP_CTRL				0x0029
#define   AD4695_REG_TEMP_CTRL_TEMP_EN			  BIT(0)
#define AD4695_REG_CONFIG_IN(n)				(0x0030 | (n))
#define   AD4695_REG_CONFIG_IN_MODE			  BIT(6)
#define   AD4695_REG_CONFIG_IN_PAIR			  GENMASK(5, 4)
#define   AD4695_REG_CONFIG_IN_AINHIGHZ_EN		  BIT(3)
#define   AD4695_REG_CONFIG_IN_OSR_SET			  GENMASK(1, 0)
#define AD4695_REG_UPPER_IN(n)				(0x0040 | (2 * (n)))
#define AD4695_REG_LOWER_IN(n)				(0x0060 | (2 * (n)))
#define AD4695_REG_HYST_IN(n)				(0x0080 | (2 * (n)))
#define AD4695_REG_OFFSET_IN(n)				(0x00A0 | (2 * (n)))
#define AD4695_REG_GAIN_IN(n)				(0x00C0 | (2 * (n)))
#define AD4695_REG_AS_SLOT(n)				(0x0100 | (n))
#define   AD4695_REG_AS_SLOT_INX			  GENMASK(3, 0)

/* Conversion mode commands */
#define AD4695_CMD_EXIT_CNV_MODE	0x0A
#define AD4695_CMD_TEMP_CHAN		0x0F
#define AD4695_CMD_VOLTAGE_CHAN(n)	(0x10 | (n))

/* timing specs */
#define AD4695_T_CONVERT_NS		415
#define AD4695_T_WAKEUP_HW_MS		3
#define AD4695_T_WAKEUP_SW_MS		3
#define AD4695_T_REFBUF_MS		100
#define AD4695_T_REGCONFIG_NS		20
#define AD4695_T_SCK_CNV_DELAY_NS	80
#define AD4695_T_CNVL_NS		80
#define AD4695_T_CNVH_NS		10
#define AD4695_REG_ACCESS_SCLK_HZ	(10 * MEGA)

/* Max number of voltage input channels. */
#define AD4695_MAX_CHANNELS		16
/* Max size of 1 raw sample in bytes. */
#define AD4695_MAX_CHANNEL_SIZE		2

enum ad4695_in_pair {
	AD4695_IN_PAIR_REFGND,
	AD4695_IN_PAIR_COM,
	AD4695_IN_PAIR_EVEN_ODD,
};

struct ad4695_chip_info {
	const char *name;
	int max_sample_rate;
	u32 t_acq_ns;
	u8 num_voltage_inputs;
};

struct ad4695_channel_config {
	unsigned int channel;
	bool highz_en;
	bool bipolar;
	enum ad4695_in_pair pin_pairing;
	unsigned int common_mode_mv;
	unsigned int oversampling_ratio;
};

struct ad4695_state {
	struct spi_device *spi;
	struct spi_offload *offload;
	struct spi_offload_trigger *offload_trigger;
	struct regmap *regmap;
	struct regmap *regmap16;
	struct gpio_desc *reset_gpio;
	/* currently PWM CNV only supported with SPI offload use */
	struct pwm_device *cnv_pwm;
	/* protects against concurrent use of cnv_pwm */
	struct mutex cnv_pwm_lock;
	/* offload also requires separate gpio to manually control CNV */
	struct gpio_desc *cnv_gpio;
	/* voltages channels plus temperature and timestamp */
	struct iio_chan_spec iio_chan[AD4695_MAX_CHANNELS + 2];
	struct ad4695_channel_config channels_cfg[AD4695_MAX_CHANNELS];
	const struct ad4695_chip_info *chip_info;
	int sample_freq_range[3];
	/* Reference voltage. */
	unsigned int vref_mv;
	/* Common mode input pin voltage. */
	unsigned int com_mv;
	/*
	 * 2 per voltage and temperature chan plus 1 xfer to trigger 1st
	 * CNV. Excluding the trigger xfer, every 2nd xfer only serves
	 * to control CS and add a delay between the last SCLK and next
	 * CNV rising edges.
	 */
	struct spi_transfer buf_read_xfer[AD4695_MAX_CHANNELS * 2 + 3];
	struct spi_message buf_read_msg;
	/* Raw conversion data received. */
	u8 buf[ALIGN((AD4695_MAX_CHANNELS + 2) * AD4695_MAX_CHANNEL_SIZE,
		     sizeof(s64)) + sizeof(s64)] __aligned(IIO_DMA_MINALIGN);
	u16 raw_data;
	/* Commands to send for single conversion. */
	u16 cnv_cmd;
	u8 cnv_cmd2;
	/* Buffer for storing data from regmap bus reads/writes */
	u8 regmap_bus_data[4];
};

static const struct regmap_range ad4695_regmap_rd_ranges[] = {
	regmap_reg_range(AD4695_REG_SPI_CONFIG_A, AD4695_REG_SPI_CONFIG_B),
	regmap_reg_range(AD4695_REG_DEVICE_TYPE, AD4695_REG_DEVICE_TYPE),
	regmap_reg_range(AD4695_REG_SCRATCH_PAD, AD4695_REG_SCRATCH_PAD),
	regmap_reg_range(AD4695_REG_VENDOR_L, AD4695_REG_LOOP_MODE),
	regmap_reg_range(AD4695_REG_SPI_CONFIG_C, AD4695_REG_SPI_STATUS),
	regmap_reg_range(AD4695_REG_STATUS, AD4695_REG_ALERT_STATUS2),
	regmap_reg_range(AD4695_REG_CLAMP_STATUS, AD4695_REG_CLAMP_STATUS),
	regmap_reg_range(AD4695_REG_SETUP, AD4695_REG_AC_CTRL),
	regmap_reg_range(AD4695_REG_GPIO_CTRL, AD4695_REG_TEMP_CTRL),
	regmap_reg_range(AD4695_REG_CONFIG_IN(0), AD4695_REG_CONFIG_IN(15)),
	regmap_reg_range(AD4695_REG_AS_SLOT(0), AD4695_REG_AS_SLOT(127)),
};

static const struct regmap_access_table ad4695_regmap_rd_table = {
	.yes_ranges = ad4695_regmap_rd_ranges,
	.n_yes_ranges = ARRAY_SIZE(ad4695_regmap_rd_ranges),
};

static const struct regmap_range ad4695_regmap_wr_ranges[] = {
	regmap_reg_range(AD4695_REG_SPI_CONFIG_A, AD4695_REG_SPI_CONFIG_B),
	regmap_reg_range(AD4695_REG_SCRATCH_PAD, AD4695_REG_SCRATCH_PAD),
	regmap_reg_range(AD4695_REG_LOOP_MODE, AD4695_REG_LOOP_MODE),
	regmap_reg_range(AD4695_REG_SPI_CONFIG_C, AD4695_REG_SPI_STATUS),
	regmap_reg_range(AD4695_REG_SETUP, AD4695_REG_AC_CTRL),
	regmap_reg_range(AD4695_REG_GPIO_CTRL, AD4695_REG_TEMP_CTRL),
	regmap_reg_range(AD4695_REG_CONFIG_IN(0), AD4695_REG_CONFIG_IN(15)),
	regmap_reg_range(AD4695_REG_AS_SLOT(0), AD4695_REG_AS_SLOT(127)),
};

static const struct regmap_access_table ad4695_regmap_wr_table = {
	.yes_ranges = ad4695_regmap_wr_ranges,
	.n_yes_ranges = ARRAY_SIZE(ad4695_regmap_wr_ranges),
};

static const struct regmap_config ad4695_regmap_config = {
	.name = "ad4695-8",
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = AD4695_REG_AS_SLOT(127),
	.rd_table = &ad4695_regmap_rd_table,
	.wr_table = &ad4695_regmap_wr_table,
};

static const struct regmap_range ad4695_regmap16_rd_ranges[] = {
	regmap_reg_range(AD4695_REG_STD_SEQ_CONFIG, AD4695_REG_STD_SEQ_CONFIG),
	regmap_reg_range(AD4695_REG_UPPER_IN(0), AD4695_REG_GAIN_IN(15)),
};

static const struct regmap_access_table ad4695_regmap16_rd_table = {
	.yes_ranges = ad4695_regmap16_rd_ranges,
	.n_yes_ranges = ARRAY_SIZE(ad4695_regmap16_rd_ranges),
};

static const struct regmap_range ad4695_regmap16_wr_ranges[] = {
	regmap_reg_range(AD4695_REG_STD_SEQ_CONFIG, AD4695_REG_STD_SEQ_CONFIG),
	regmap_reg_range(AD4695_REG_UPPER_IN(0), AD4695_REG_GAIN_IN(15)),
};

static const struct regmap_access_table ad4695_regmap16_wr_table = {
	.yes_ranges = ad4695_regmap16_wr_ranges,
	.n_yes_ranges = ARRAY_SIZE(ad4695_regmap16_wr_ranges),
};

static const struct regmap_config ad4695_regmap16_config = {
	.name = "ad4695-16",
	.reg_bits = 16,
	.reg_stride = 2,
	.val_bits = 16,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.max_register = AD4695_REG_GAIN_IN(15),
	.rd_table = &ad4695_regmap16_rd_table,
	.wr_table = &ad4695_regmap16_wr_table,
};

static int ad4695_regmap_bus_reg_write(void *context, const void *data,
				       size_t count)
{
	struct ad4695_state *st = context;
	struct spi_transfer xfer = {
			.speed_hz = AD4695_REG_ACCESS_SCLK_HZ,
			.len = count,
			.tx_buf = st->regmap_bus_data,
	};

	if (count > ARRAY_SIZE(st->regmap_bus_data))
		return -EINVAL;

	memcpy(st->regmap_bus_data, data, count);

	return spi_sync_transfer(st->spi, &xfer, 1);
}

static int ad4695_regmap_bus_reg_read(void *context, const void *reg,
				      size_t reg_size, void *val,
				      size_t val_size)
{
	struct ad4695_state *st = context;
	struct spi_transfer xfers[] = {
		{
			.speed_hz = AD4695_REG_ACCESS_SCLK_HZ,
			.len = reg_size,
			.tx_buf = &st->regmap_bus_data[0],
		}, {
			.speed_hz = AD4695_REG_ACCESS_SCLK_HZ,
			.len = val_size,
			.rx_buf = &st->regmap_bus_data[2],
		},
	};
	int ret;

	if (reg_size > 2)
		return -EINVAL;

	if (val_size > 2)
		return -EINVAL;

	memcpy(&st->regmap_bus_data[0], reg, reg_size);

	ret = spi_sync_transfer(st->spi, xfers, ARRAY_SIZE(xfers));
	if (ret)
		return ret;

	memcpy(val, &st->regmap_bus_data[2], val_size);

	return 0;
}

static const struct regmap_bus ad4695_regmap_bus = {
	.write = ad4695_regmap_bus_reg_write,
	.read = ad4695_regmap_bus_reg_read,
	.read_flag_mask = 0x80,
	.reg_format_endian_default = REGMAP_ENDIAN_BIG,
	.val_format_endian_default = REGMAP_ENDIAN_BIG,
};

enum {
	AD4695_SCAN_TYPE_OSR_1,
	AD4695_SCAN_TYPE_OSR_4,
	AD4695_SCAN_TYPE_OSR_16,
	AD4695_SCAN_TYPE_OSR_64,
};

static const struct iio_scan_type ad4695_scan_type_offload_u[] = {
	[AD4695_SCAN_TYPE_OSR_1] = {
		.sign = 'u',
		.realbits = 16,
		.shift = 3,
		.storagebits = 32,
	},
	[AD4695_SCAN_TYPE_OSR_4] = {
		.sign = 'u',
		.realbits = 17,
		.shift = 2,
		.storagebits = 32,
	},
	[AD4695_SCAN_TYPE_OSR_16] = {
		.sign = 'u',
		.realbits = 18,
		.shift = 1,
		.storagebits = 32,
	},
	[AD4695_SCAN_TYPE_OSR_64] = {
		.sign = 'u',
		.realbits = 19,
		.storagebits = 32,
	},
};

static const struct iio_scan_type ad4695_scan_type_offload_s[] = {
	[AD4695_SCAN_TYPE_OSR_1] = {
		.sign = 's',
		.realbits = 16,
		.shift = 3,
		.storagebits = 32,
	},
	[AD4695_SCAN_TYPE_OSR_4] = {
		.sign = 's',
		.realbits = 17,
		.shift = 2,
		.storagebits = 32,
	},
	[AD4695_SCAN_TYPE_OSR_16] = {
		.sign = 's',
		.realbits = 18,
		.shift = 1,
		.storagebits = 32,
	},
	[AD4695_SCAN_TYPE_OSR_64] = {
		.sign = 's',
		.realbits = 19,
		.storagebits = 32,
	},
};

static const struct iio_chan_spec ad4695_channel_template = {
	.type = IIO_VOLTAGE,
	.indexed = 1,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			      BIT(IIO_CHAN_INFO_SCALE) |
			      BIT(IIO_CHAN_INFO_OFFSET) |
			      BIT(IIO_CHAN_INFO_CALIBSCALE) |
			      BIT(IIO_CHAN_INFO_CALIBBIAS),
	.info_mask_separate_available = BIT(IIO_CHAN_INFO_CALIBSCALE) |
					BIT(IIO_CHAN_INFO_CALIBBIAS),
	.scan_type = {
		.sign = 'u',
		.realbits = 16,
		.storagebits = 16,
	},
};

static const struct iio_chan_spec ad4695_temp_channel_template = {
	.address = AD4695_CMD_TEMP_CHAN,
	.type = IIO_TEMP,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			      BIT(IIO_CHAN_INFO_SCALE) |
			      BIT(IIO_CHAN_INFO_OFFSET),
	.scan_type = {
		.sign = 's',
		.realbits = 16,
		.storagebits = 16,
	},
};

static const struct iio_chan_spec ad4695_soft_timestamp_channel_template =
	IIO_CHAN_SOFT_TIMESTAMP(0);

static const char * const ad4695_power_supplies[] = {
	"avdd", "vio"
};

static const int ad4695_oversampling_ratios[] = {
	1, 4, 16, 64,
};

static const struct ad4695_chip_info ad4695_chip_info = {
	.name = "ad4695",
	.max_sample_rate = 500 * KILO,
	.t_acq_ns = 1715,
	.num_voltage_inputs = 16,
};

static const struct ad4695_chip_info ad4696_chip_info = {
	.name = "ad4696",
	.max_sample_rate = 1 * MEGA,
	.t_acq_ns = 715,
	.num_voltage_inputs = 16,
};

static const struct ad4695_chip_info ad4697_chip_info = {
	.name = "ad4697",
	.max_sample_rate = 500 * KILO,
	.t_acq_ns = 1715,
	.num_voltage_inputs = 8,
};

static const struct ad4695_chip_info ad4698_chip_info = {
	.name = "ad4698",
	.max_sample_rate = 1 * MEGA,
	.t_acq_ns = 715,
	.num_voltage_inputs = 8,
};

static void ad4695_cnv_manual_trigger(struct ad4695_state *st)
{
	gpiod_set_value_cansleep(st->cnv_gpio, 1);
	ndelay(10);
	gpiod_set_value_cansleep(st->cnv_gpio, 0);
}

/**
 * ad4695_set_single_cycle_mode - Set the device in single cycle mode
 * @st: The AD4695 state
 * @channel: The first channel to read
 *
 * As per the datasheet, to enable single cycle mode, we need to set
 * STD_SEQ_EN=0, NUM_SLOTS_AS=0 and CYC_CTRL=1 (Table 15). Setting SPI_MODE=1
 * triggers the first conversion using the channel in AS_SLOT0.
 *
 * Context: can sleep, must be called with iio_device_claim_direct held
 * Return: 0 on success, a negative error code on failure
 */
static int ad4695_set_single_cycle_mode(struct ad4695_state *st,
					unsigned int channel)
{
	int ret;

	ret = regmap_clear_bits(st->regmap, AD4695_REG_SEQ_CTRL,
				AD4695_REG_SEQ_CTRL_STD_SEQ_EN |
				AD4695_REG_SEQ_CTRL_NUM_SLOTS_AS);
	if (ret)
		return ret;

	ret = regmap_write(st->regmap, AD4695_REG_AS_SLOT(0),
			   FIELD_PREP(AD4695_REG_AS_SLOT_INX, channel));
	if (ret)
		return ret;

	return regmap_set_bits(st->regmap, AD4695_REG_SETUP,
			       AD4695_REG_SETUP_SPI_MODE |
			       AD4695_REG_SETUP_SPI_CYC_CTRL);
}

/**
 * ad4695_enter_advanced_sequencer_mode - Put the ADC in advanced sequencer mode
 * @st: The driver state
 * @n: The number of slots to use - must be >= 2, <= 128
 *
 * As per the datasheet, to enable advanced sequencer, we need to set
 * STD_SEQ_EN=0, NUM_SLOTS_AS=n-1 and CYC_CTRL=0 (Table 15). Setting SPI_MODE=1
 * triggers the first conversion using the channel in AS_SLOT0.
 *
 * Return: 0 on success, a negative error code on failure
 */
static int ad4695_enter_advanced_sequencer_mode(struct ad4695_state *st, u32 n)
{
	int ret;

	ret = regmap_update_bits(st->regmap, AD4695_REG_SEQ_CTRL,
		AD4695_REG_SEQ_CTRL_STD_SEQ_EN |
		AD4695_REG_SEQ_CTRL_NUM_SLOTS_AS,
		FIELD_PREP(AD4695_REG_SEQ_CTRL_STD_SEQ_EN, 0) |
		FIELD_PREP(AD4695_REG_SEQ_CTRL_NUM_SLOTS_AS, n - 1));
	if (ret)
		return ret;

	return regmap_update_bits(st->regmap, AD4695_REG_SETUP,
		AD4695_REG_SETUP_SPI_MODE | AD4695_REG_SETUP_SPI_CYC_CTRL,
		FIELD_PREP(AD4695_REG_SETUP_SPI_MODE, 1) |
		FIELD_PREP(AD4695_REG_SETUP_SPI_CYC_CTRL, 0));
}

/**
 * ad4695_exit_conversion_mode - Exit conversion mode
 * @st: The AD4695 state
 *
 * Sends SPI command to exit conversion mode.
 *
 * Return: 0 on success, a negative error code on failure
 */
static int ad4695_exit_conversion_mode(struct ad4695_state *st)
{
	/*
	 * An extra transfer is needed to trigger a conversion here so
	 * that we can be 100% sure the command will be processed by the
	 * ADC, rather than relying on it to be in the correct state
	 * when this function is called (this chip has a quirk where the
	 * command only works when reading a conversion, and if the
	 * previous conversion was already read then it won't work). The
	 * actual conversion command is then run at the slower
	 * AD4695_REG_ACCESS_SCLK_HZ speed to guarantee this works.
	 */
	struct spi_transfer xfers[] = {
		{
			.delay.value = AD4695_T_CNVL_NS,
			.delay.unit = SPI_DELAY_UNIT_NSECS,
			.cs_change = 1,
			.cs_change_delay.value = AD4695_T_CNVH_NS,
			.cs_change_delay.unit = SPI_DELAY_UNIT_NSECS,
		},
		{
			.speed_hz = AD4695_REG_ACCESS_SCLK_HZ,
			.tx_buf = &st->cnv_cmd2,
			.len = 1,
			.delay.value = AD4695_T_REGCONFIG_NS,
			.delay.unit = SPI_DELAY_UNIT_NSECS,
		},
	};

	/*
	 * Technically, could do a 5-bit transfer, but shifting to start of
	 * 8 bits instead for better SPI controller support.
	 */
	st->cnv_cmd2 = AD4695_CMD_EXIT_CNV_MODE << 3;

	if (st->cnv_gpio) {
		ad4695_cnv_manual_trigger(st);

		/*
		 * In this case, CNV is not connected to CS, so we don't need
		 * the extra CS toggle to trigger the conversion and toggling
		 * CS would have no effect.
		 */
		return spi_sync_transfer(st->spi, &xfers[1], 1);
	}

	return spi_sync_transfer(st->spi, xfers, ARRAY_SIZE(xfers));
}

static int ad4695_set_ref_voltage(struct ad4695_state *st, int vref_mv)
{
	u8 val;

	if (vref_mv >= 2400 && vref_mv <= 2750)
		val = 0;
	else if (vref_mv > 2750 && vref_mv <= 3250)
		val = 1;
	else if (vref_mv > 3250 && vref_mv <= 3750)
		val = 2;
	else if (vref_mv > 3750 && vref_mv <= 4500)
		val = 3;
	else if (vref_mv > 4500 && vref_mv <= 5100)
		val = 4;
	else
		return -EINVAL;

	return regmap_update_bits(st->regmap, AD4695_REG_REF_CTRL,
				  AD4695_REG_REF_CTRL_VREF_SET,
				  FIELD_PREP(AD4695_REG_REF_CTRL_VREF_SET, val));
}

/**
 * ad4695_osr_to_regval - convert ratio to OSR register value
 * @ratio: ratio to check
 *
 * Check if ratio is present in the list of available ratios and return
 * the corresponding value that needs to be written to the register to
 * select that ratio.
 *
 * Returns: register value (0 to 3) or -EINVAL if there is not an exact
 * match
 */
static int ad4695_osr_to_regval(int ratio)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ad4695_oversampling_ratios); i++) {
		if (ratio == ad4695_oversampling_ratios[i])
			return i;
	}

	return -EINVAL;
}

static int ad4695_write_chn_cfg(struct ad4695_state *st,
				struct ad4695_channel_config *cfg)
{
	u32 mask, val;

	mask = AD4695_REG_CONFIG_IN_MODE;
	val = FIELD_PREP(AD4695_REG_CONFIG_IN_MODE, cfg->bipolar ? 1 : 0);

	mask |= AD4695_REG_CONFIG_IN_PAIR;
	val |= FIELD_PREP(AD4695_REG_CONFIG_IN_PAIR, cfg->pin_pairing);

	mask |= AD4695_REG_CONFIG_IN_AINHIGHZ_EN;
	val |= FIELD_PREP(AD4695_REG_CONFIG_IN_AINHIGHZ_EN,
			  cfg->highz_en ? 1 : 0);

	return regmap_update_bits(st->regmap,
				  AD4695_REG_CONFIG_IN(cfg->channel),
				  mask, val);
}

static int ad4695_buffer_preenable(struct iio_dev *indio_dev)
{
	struct ad4695_state *st = iio_priv(indio_dev);
	struct spi_transfer *xfer;
	u8 temp_chan_bit = st->chip_info->num_voltage_inputs;
	u32 bit, num_xfer, num_slots;
	u32 temp_en = 0;
	int ret, rx_buf_offset = 0;

	/*
	 * We are using the advanced sequencer since it is the only way to read
	 * multiple channels that allows individual configuration of each
	 * voltage input channel. Slot 0 in the advanced sequencer is used to
	 * account for the gap between trigger polls - we don't read data from
	 * this slot. Each enabled voltage channel is assigned a slot starting
	 * with slot 1.
	 */
	num_slots = 1;

	memset(st->buf_read_xfer, 0, sizeof(st->buf_read_xfer));

	/* First xfer is only to trigger conversion of slot 1, so no rx. */
	xfer = &st->buf_read_xfer[0];
	xfer->cs_change = 1;
	xfer->delay.value = st->chip_info->t_acq_ns;
	xfer->delay.unit = SPI_DELAY_UNIT_NSECS;
	xfer->cs_change_delay.value = AD4695_T_CONVERT_NS;
	xfer->cs_change_delay.unit = SPI_DELAY_UNIT_NSECS;
	num_xfer = 1;

	iio_for_each_active_channel(indio_dev, bit) {
		xfer = &st->buf_read_xfer[num_xfer];
		xfer->bits_per_word = 16;
		xfer->rx_buf = &st->buf[rx_buf_offset];
		xfer->len = 2;
		rx_buf_offset += xfer->len;

		if (bit == temp_chan_bit) {
			temp_en = 1;
		} else {
			ret = regmap_write(st->regmap,
				AD4695_REG_AS_SLOT(num_slots),
				FIELD_PREP(AD4695_REG_AS_SLOT_INX, bit));
			if (ret)
				return ret;

			num_slots++;
		}

		num_xfer++;

		/*
		 * We need to add a blank xfer in data reads, to meet the timing
		 * requirement of a minimum delay between the last SCLK rising
		 * edge and the CS deassert.
		 */
		xfer = &st->buf_read_xfer[num_xfer];
		xfer->delay.value = AD4695_T_SCK_CNV_DELAY_NS;
		xfer->delay.unit = SPI_DELAY_UNIT_NSECS;
		xfer->cs_change = 1;
		xfer->cs_change_delay.value = AD4695_T_CONVERT_NS;
		xfer->cs_change_delay.unit = SPI_DELAY_UNIT_NSECS;

		num_xfer++;
	}

	/*
	 * The advanced sequencer requires that at least 2 slots are enabled.
	 * Since slot 0 is always used for other purposes, we need only 1
	 * enabled voltage channel to meet this requirement.  If the temperature
	 * channel is the only enabled channel, we need to add one more slot in
	 * the sequence but not read from it. This is because the temperature
	 * sensor is sampled at the end of the channel sequence in advanced
	 * sequencer mode (see datasheet page 38).
	 *
	 * From the iio_for_each_active_channel() block above, we now have an
	 * xfer with data followed by a blank xfer to allow us to meet the
	 * timing spec, so move both of those up before adding an extra to
	 * handle the temperature-only case.
	 */
	if (num_slots < 2) {
		/* Move last two xfers */
		st->buf_read_xfer[num_xfer] = st->buf_read_xfer[num_xfer - 1];
		st->buf_read_xfer[num_xfer - 1] = st->buf_read_xfer[num_xfer - 2];
		num_xfer++;

		/* Modify inserted xfer for extra slot. */
		xfer = &st->buf_read_xfer[num_xfer - 3];
		memset(xfer, 0, sizeof(*xfer));
		xfer->cs_change = 1;
		xfer->delay.value = st->chip_info->t_acq_ns;
		xfer->delay.unit = SPI_DELAY_UNIT_NSECS;
		xfer->cs_change_delay.value = AD4695_T_CONVERT_NS;
		xfer->cs_change_delay.unit = SPI_DELAY_UNIT_NSECS;
		xfer++;

		/* and add the extra slot in the sequencer */
		ret = regmap_write(st->regmap,
				   AD4695_REG_AS_SLOT(num_slots),
				   FIELD_PREP(AD4695_REG_AS_SLOT_INX, 0));
		if (ret)
			return ret;

		num_slots++;

		/*
		 * We still want to point at the last xfer when finished, so
		 * update the pointer.
		 */
		xfer = &st->buf_read_xfer[num_xfer - 1];
	}

	/*
	 * Don't keep CS asserted after last xfer. Also triggers conversion of
	 * slot 0.
	 */
	xfer->cs_change = 0;

	/*
	 * Temperature channel isn't included in the sequence, but rather
	 * controlled by setting a bit in the TEMP_CTRL register.
	 */

	ret = regmap_update_bits(st->regmap, AD4695_REG_TEMP_CTRL,
		AD4695_REG_TEMP_CTRL_TEMP_EN,
		FIELD_PREP(AD4695_REG_TEMP_CTRL_TEMP_EN, temp_en));
	if (ret)
		return ret;

	spi_message_init_with_transfers(&st->buf_read_msg, st->buf_read_xfer,
					num_xfer);

	ret = spi_optimize_message(st->spi, &st->buf_read_msg);
	if (ret)
		return ret;

	/* This triggers conversion of slot 0. */
	ret = ad4695_enter_advanced_sequencer_mode(st, num_slots);
	if (ret)
		spi_unoptimize_message(&st->buf_read_msg);

	return ret;
}

static int ad4695_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct ad4695_state *st = iio_priv(indio_dev);
	int ret;

	ret = ad4695_exit_conversion_mode(st);
	if (ret)
		return ret;

	spi_unoptimize_message(&st->buf_read_msg);

	return 0;
}

static const struct iio_buffer_setup_ops ad4695_buffer_setup_ops = {
	.preenable = ad4695_buffer_preenable,
	.postdisable = ad4695_buffer_postdisable,
};

static irqreturn_t ad4695_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad4695_state *st = iio_priv(indio_dev);
	int ret;

	ret = spi_sync(st->spi, &st->buf_read_msg);
	if (ret)
		goto out;

	iio_push_to_buffers_with_timestamp(indio_dev, st->buf, pf->timestamp);

out:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int ad4695_offload_buffer_postenable(struct iio_dev *indio_dev)
{
	struct ad4695_state *st = iio_priv(indio_dev);
	struct spi_offload_trigger_config config = {
		.type = SPI_OFFLOAD_TRIGGER_DATA_READY,
	};
	struct spi_transfer *xfer = &st->buf_read_xfer[0];
	struct pwm_state state;
	u8 temp_chan_bit = st->chip_info->num_voltage_inputs;
	u8 num_slots = 0;
	u8 temp_en = 0;
	unsigned int bit;
	int ret;

	iio_for_each_active_channel(indio_dev, bit) {
		if (bit == temp_chan_bit) {
			temp_en = 1;
			continue;
		}

		ret = regmap_write(st->regmap, AD4695_REG_AS_SLOT(num_slots),
				   FIELD_PREP(AD4695_REG_AS_SLOT_INX, bit));
		if (ret)
			return ret;

		num_slots++;
	}

	/*
	 * For non-offload, we could discard data to work around this
	 * restriction, but with offload, that is not possible.
	 */
	if (num_slots < 2) {
		dev_err(&st->spi->dev,
			"At least two voltage channels must be enabled.\n");
		return -EINVAL;
	}

	ret = regmap_update_bits(st->regmap, AD4695_REG_TEMP_CTRL,
				 AD4695_REG_TEMP_CTRL_TEMP_EN,
				 FIELD_PREP(AD4695_REG_TEMP_CTRL_TEMP_EN,
					    temp_en));
	if (ret)
		return ret;

	/* Each BUSY event means just one sample for one channel is ready. */
	memset(xfer, 0, sizeof(*xfer));
	xfer->offload_flags = SPI_OFFLOAD_XFER_RX_STREAM;
	/* Using 19 bits per word to allow for possible oversampling */
	xfer->bits_per_word = 19;
	xfer->len = 4;

	spi_message_init_with_transfers(&st->buf_read_msg, xfer, 1);
	st->buf_read_msg.offload = st->offload;

	ret = spi_optimize_message(st->spi, &st->buf_read_msg);
	if (ret)
		return ret;

	/*
	 * NB: technically, this is part the SPI offload trigger enable, but it
	 * doesn't work to call it from the offload trigger enable callback
	 * because it requires accessing the SPI bus. Calling it from the
	 * trigger enable callback could cause a deadlock.
	 */
	ret = regmap_set_bits(st->regmap, AD4695_REG_GP_MODE,
			      AD4695_REG_GP_MODE_BUSY_GP_EN);
	if (ret)
		goto err_unoptimize_message;

	ret = spi_offload_trigger_enable(st->offload, st->offload_trigger,
					 &config);
	if (ret)
		goto err_disable_busy_output;

	ret = ad4695_enter_advanced_sequencer_mode(st, num_slots);
	if (ret)
		goto err_offload_trigger_disable;

	mutex_lock(&st->cnv_pwm_lock);
	pwm_get_state(st->cnv_pwm, &state);
	/*
	 * PWM subsystem generally rounds down, so requesting 2x minimum high
	 * time ensures that we meet the minimum high time in any case.
	 */
	state.duty_cycle = AD4695_T_CNVH_NS * 2;
	ret = pwm_apply_might_sleep(st->cnv_pwm, &state);
	mutex_unlock(&st->cnv_pwm_lock);
	if (ret)
		goto err_offload_exit_conversion_mode;

	return 0;

err_offload_exit_conversion_mode:
	/*
	 * We have to unwind in a different order to avoid triggering offload.
	 * ad4695_exit_conversion_mode() triggers a conversion, so it has to be
	 * done after spi_offload_trigger_disable().
	 */
	spi_offload_trigger_disable(st->offload, st->offload_trigger);
	ad4695_exit_conversion_mode(st);
	goto err_disable_busy_output;

err_offload_trigger_disable:
	spi_offload_trigger_disable(st->offload, st->offload_trigger);

err_disable_busy_output:
	regmap_clear_bits(st->regmap, AD4695_REG_GP_MODE,
			  AD4695_REG_GP_MODE_BUSY_GP_EN);

err_unoptimize_message:
	spi_unoptimize_message(&st->buf_read_msg);

	return ret;
}

static int ad4695_offload_buffer_predisable(struct iio_dev *indio_dev)
{
	struct ad4695_state *st = iio_priv(indio_dev);
	struct pwm_state state;
	int ret;

	scoped_guard(mutex, &st->cnv_pwm_lock) {
		pwm_get_state(st->cnv_pwm, &state);
		state.duty_cycle = 0;
		ret = pwm_apply_might_sleep(st->cnv_pwm, &state);
		if (ret)
			return ret;
	}

	spi_offload_trigger_disable(st->offload, st->offload_trigger);

	/*
	 * ad4695_exit_conversion_mode() triggers a conversion, so it has to be
	 * done after spi_offload_trigger_disable().
	 */
	ret = ad4695_exit_conversion_mode(st);
	if (ret)
		return ret;

	ret = regmap_clear_bits(st->regmap, AD4695_REG_GP_MODE,
				AD4695_REG_GP_MODE_BUSY_GP_EN);
	if (ret)
		return ret;

	spi_unoptimize_message(&st->buf_read_msg);

	return 0;
}

static const struct iio_buffer_setup_ops ad4695_offload_buffer_setup_ops = {
	.postenable = ad4695_offload_buffer_postenable,
	.predisable = ad4695_offload_buffer_predisable,
};

/**
 * ad4695_read_one_sample - Read a single sample using single-cycle mode
 * @st: The AD4695 state
 * @address: The address of the channel to read
 *
 * Upon successful return, the sample will be stored in `st->raw_data`.
 *
 * Context: can sleep, must be called with iio_device_claim_direct held
 * Return: 0 on success, a negative error code on failure
 */
static int ad4695_read_one_sample(struct ad4695_state *st, unsigned int address)
{
	struct spi_transfer xfers[2] = {
		{
			.speed_hz = AD4695_REG_ACCESS_SCLK_HZ,
			.bits_per_word = 16,
			.tx_buf = &st->cnv_cmd,
			.len = 2,
		},
		{
			/* Required delay between last SCLK and CNV/CS */
			.delay.value = AD4695_T_SCK_CNV_DELAY_NS,
			.delay.unit = SPI_DELAY_UNIT_NSECS,
		}
	};
	int ret;

	ret = ad4695_set_single_cycle_mode(st, address);
	if (ret)
		return ret;

	/*
	 * If CNV is connected to CS, the previous function will have triggered
	 * the conversion, otherwise, we do it manually.
	 */
	if (st->cnv_gpio)
		ad4695_cnv_manual_trigger(st);

	/*
	 * Setting the first channel to the temperature channel isn't supported
	 * in single-cycle mode, so we have to do an extra conversion to read
	 * the temperature.
	 */
	if (address == AD4695_CMD_TEMP_CHAN) {
		st->cnv_cmd = AD4695_CMD_TEMP_CHAN << 11;

		ret = spi_sync_transfer(st->spi, xfers, ARRAY_SIZE(xfers));
		if (ret)
			return ret;

		/*
		 * If CNV is connected to CS, the previous function will have
		 * triggered the conversion, otherwise, we do it manually.
		 */
		if (st->cnv_gpio)
			ad4695_cnv_manual_trigger(st);
	}

	/* Then read the result and exit conversion mode. */
	st->cnv_cmd = AD4695_CMD_EXIT_CNV_MODE << 11;
	xfers[0].rx_buf = &st->raw_data;

	return spi_sync_transfer(st->spi, xfers, ARRAY_SIZE(xfers));
}

static int __ad4695_read_info_raw(struct ad4695_state *st,
				  struct iio_chan_spec const *chan,
				  int *val)
{
	u8 realbits = chan->scan_type.realbits;
	int ret;

	ret = ad4695_read_one_sample(st, chan->address);
	if (ret)
		return ret;

	if (chan->scan_type.sign == 's')
		*val = sign_extend32(st->raw_data, realbits - 1);
	else
		*val = st->raw_data;

	return IIO_VAL_INT;
}

static int ad4695_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct ad4695_state *st = iio_priv(indio_dev);
	const struct iio_scan_type *scan_type;
	struct ad4695_channel_config *cfg;
	unsigned int reg_val;
	int ret, tmp;
	u8 realbits;

	if (chan->type == IIO_VOLTAGE)
		cfg = &st->channels_cfg[chan->scan_index];

	scan_type = iio_get_current_scan_type(indio_dev, chan);
	if (IS_ERR(scan_type))
		return PTR_ERR(scan_type);

	realbits = scan_type->realbits;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		ret = __ad4695_read_info_raw(st, chan, val);
		iio_device_release_direct(indio_dev);
		return ret;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			*val = st->vref_mv;
			*val2 = realbits;
			return IIO_VAL_FRACTIONAL_LOG2;
		case IIO_TEMP:
			/* T_scale (°C) = raw * V_REF (mV) / (-1.8 mV/°C * 2^16) */
			*val = st->vref_mv * -556;
			*val2 = 16;
			return IIO_VAL_FRACTIONAL_LOG2;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		switch (chan->type) {
		case IIO_VOLTAGE:
			if (cfg->pin_pairing == AD4695_IN_PAIR_COM)
				*val = st->com_mv * (1 << realbits) / st->vref_mv;
			else if (cfg->pin_pairing == AD4695_IN_PAIR_EVEN_ODD)
				*val = cfg->common_mode_mv * (1 << realbits) / st->vref_mv;
			else
				*val = 0;

			return IIO_VAL_INT;
		case IIO_TEMP:
			/* T_offset (°C) = -725 mV / (-1.8 mV/°C) */
			/* T_offset (raw) = T_offset (°C) * (-1.8 mV/°C) * 2^16 / V_REF (mV) */
			*val = -47513600;
			*val2 = st->vref_mv;
			return IIO_VAL_FRACTIONAL;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_CALIBSCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			if (!iio_device_claim_direct(indio_dev))
				return -EBUSY;
			ret = regmap_read(st->regmap16,
					  AD4695_REG_GAIN_IN(chan->scan_index),
					  &reg_val);
			iio_device_release_direct(indio_dev);
			if (ret)
				return ret;
			*val = reg_val;
			*val2 = 15;

			return IIO_VAL_FRACTIONAL_LOG2;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_CALIBBIAS:
		switch (chan->type)
		case IIO_VOLTAGE: {
			if (!iio_device_claim_direct(indio_dev))
				return -EBUSY;
			ret = regmap_read(st->regmap16,
					  AD4695_REG_OFFSET_IN(chan->scan_index),
					  &reg_val);
			iio_device_release_direct(indio_dev);
			if (ret)
				return ret;

			tmp = sign_extend32(reg_val, 15);

			switch (cfg->oversampling_ratio) {
			case 1:
				*val = tmp / 4;
				*val2 = abs(tmp) % 4 * MICRO / 4;
				break;
			case 4:
				*val = tmp / 2;
				*val2 = abs(tmp) % 2 * MICRO / 2;
				break;
			case 16:
				*val = tmp;
				*val2 = 0;
				break;
			case 64:
				*val = tmp * 2;
				*val2 = 0;
				break;
			default:
				return -EINVAL;
			}

			if (tmp < 0 && *val2) {
				*val *= -1;
				*val2 *= -1;
			}

			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		switch (chan->type) {
		case IIO_VOLTAGE:
			*val = cfg->oversampling_ratio;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ: {
		struct pwm_state state;
		unsigned int osr = 1;

		if (chan->type == IIO_VOLTAGE)
			osr = cfg->oversampling_ratio;

		ret = pwm_get_state_hw(st->cnv_pwm, &state);
		if (ret)
			return ret;

		/*
		 * The effective sampling frequency for a channel is the input
		 * frequency divided by the channel's OSR value.
		 */
		*val = DIV_ROUND_UP_ULL(NSEC_PER_SEC, state.period * osr);

		return IIO_VAL_INT;
	}
	default:
		return -EINVAL;
	}
}

static int ad4695_write_raw_get_fmt(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan,
				    long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return IIO_VAL_INT;
	default:
		return IIO_VAL_INT_PLUS_MICRO;
	}
}

static int ad4695_set_osr_val(struct ad4695_state *st,
			      struct iio_chan_spec const *chan,
			      int val)
{
	int osr = ad4695_osr_to_regval(val);

	if (osr < 0)
		return osr;

	switch (chan->type) {
	case IIO_VOLTAGE:
		st->channels_cfg[chan->scan_index].oversampling_ratio = val;
		return regmap_update_bits(st->regmap,
				AD4695_REG_CONFIG_IN(chan->scan_index),
				AD4695_REG_CONFIG_IN_OSR_SET,
				FIELD_PREP(AD4695_REG_CONFIG_IN_OSR_SET, osr));
	default:
		return -EINVAL;
	}
}

static unsigned int ad4695_get_calibbias(int val, int val2, int osr)
{
	int val_calc, scale;

	switch (osr) {
	case 4:
		scale = 4;
		break;
	case 16:
		scale = 2;
		break;
	case 64:
		scale = 1;
		break;
	default:
		scale = 8;
		break;
	}

	val = clamp_t(int, val, S32_MIN / 8, S32_MAX / 8);

	/* val2 range is (-MICRO, MICRO) if val == 0, otherwise [0, MICRO) */
	if (val < 0)
		val_calc = val * scale - val2 * scale / MICRO;
	else if (val2 < 0)
		/* if val2 < 0 then val == 0 */
		val_calc = val2 * scale / (int)MICRO;
	else
		val_calc = val * scale + val2 * scale / MICRO;

	val_calc /= 2;

	return clamp_t(int, val_calc, S16_MIN, S16_MAX);
}

static int __ad4695_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct ad4695_state *st = iio_priv(indio_dev);
	unsigned int reg_val;
	unsigned int osr = 1;

	if (chan->type == IIO_VOLTAGE)
		osr = st->channels_cfg[chan->scan_index].oversampling_ratio;

	switch (mask) {
	case IIO_CHAN_INFO_CALIBSCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			if (val < 0 || val2 < 0)
				reg_val = 0;
			else if (val > 1)
				reg_val = U16_MAX;
			else
				reg_val = (val * (1 << 16) +
					   mul_u64_u32_div(val2, 1 << 16,
							   MICRO)) / 2;

			return regmap_write(st->regmap16,
					    AD4695_REG_GAIN_IN(chan->scan_index),
					    reg_val);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_CALIBBIAS:
		switch (chan->type) {
		case IIO_VOLTAGE:
			reg_val = ad4695_get_calibbias(val, val2, osr);
			return regmap_write(st->regmap16,
					    AD4695_REG_OFFSET_IN(chan->scan_index),
					    reg_val);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ: {
		struct pwm_state state;
		/*
		 * Limit the maximum acceptable sample rate according to
		 * the channel's oversampling ratio.
		 */
		u64 max_osr_rate = DIV_ROUND_UP_ULL(st->chip_info->max_sample_rate,
						    osr);

		if (val <= 0 || val > max_osr_rate)
			return -EINVAL;

		guard(mutex)(&st->cnv_pwm_lock);
		pwm_get_state(st->cnv_pwm, &state);
		/*
		 * The required sample frequency for a given OSR is the
		 * input frequency multiplied by it.
		 */
		state.period = DIV_ROUND_UP_ULL(NSEC_PER_SEC, val * osr);
		return pwm_apply_might_sleep(st->cnv_pwm, &state);
	}
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		return ad4695_set_osr_val(st, chan, val);
	default:
		return -EINVAL;
	}
}

static int ad4695_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;
	ret = __ad4695_write_raw(indio_dev, chan, val, val2, mask);
	iio_device_release_direct(indio_dev);

	return ret;
}

static int ad4695_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long mask)
{
	int ret;
	static const int ad4695_calibscale_available[6] = {
		/* Range of 0 (inclusive) to 2 (exclusive) */
		0, 15, 1, 15, U16_MAX, 15
	};
	static const int ad4695_calibbias_available[4][6] = {
		/*
		 * Datasheet says FSR/8 which translates to signed/4. The step
		 * depends on oversampling ratio, so we need four different
		 * ranges to select from.
		 */
		{
			S16_MIN / 4, 0,
			0, MICRO / 4,
			S16_MAX / 4, S16_MAX % 4 * MICRO / 4
		},
		{
			S16_MIN / 2, 0,
			0, MICRO / 2,
			S16_MAX / 2, S16_MAX % 2 * MICRO / 2,
		},
		{
			S16_MIN, 0,
			1, 0,
			S16_MAX, 0,
		},
		{
			S16_MIN * 2, 0,
			2, 0,
			S16_MAX * 2, 0,
		},
	};
	struct ad4695_state *st = iio_priv(indio_dev);
	unsigned int osr = 1;

	if (chan->type == IIO_VOLTAGE)
		osr = st->channels_cfg[chan->scan_index].oversampling_ratio;

	switch (mask) {
	case IIO_CHAN_INFO_CALIBSCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			*vals = ad4695_calibscale_available;
			*type = IIO_VAL_FRACTIONAL_LOG2;
			return IIO_AVAIL_RANGE;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_CALIBBIAS:
		switch (chan->type) {
		case IIO_VOLTAGE:
			ret = ad4695_osr_to_regval(osr);
			if (ret < 0)
				return ret;
			/*
			 * Select the appropriate calibbias array based on the
			 * OSR value in the register.
			 */
			*vals = ad4695_calibbias_available[ret];
			*type = IIO_VAL_INT_PLUS_MICRO;
			return IIO_AVAIL_RANGE;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		/* Max sample rate for the channel depends on OSR */
		st->sample_freq_range[2] =
			DIV_ROUND_UP_ULL(st->chip_info->max_sample_rate, osr);
		*vals = st->sample_freq_range;
		*type = IIO_VAL_INT;
		return IIO_AVAIL_RANGE;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		switch (chan->type) {
		case IIO_VOLTAGE:
			*vals = ad4695_oversampling_ratios;
			*length = ARRAY_SIZE(ad4695_oversampling_ratios);
			*type = IIO_VAL_INT;
			return IIO_AVAIL_LIST;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int ad4695_debugfs_reg_access(struct iio_dev *indio_dev,
				     unsigned int reg,
				     unsigned int writeval,
				     unsigned int *readval)
{
	struct ad4695_state *st = iio_priv(indio_dev);
	int ret = -EINVAL;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	if (readval) {
		if (regmap_check_range_table(st->regmap, reg,
					     &ad4695_regmap_rd_table))
			ret = regmap_read(st->regmap, reg, readval);
		if (regmap_check_range_table(st->regmap16, reg,
					     &ad4695_regmap16_rd_table))
			ret = regmap_read(st->regmap16, reg, readval);
	} else {
		if (regmap_check_range_table(st->regmap, reg,
					     &ad4695_regmap_wr_table))
			ret = regmap_write(st->regmap, reg, writeval);
		if (regmap_check_range_table(st->regmap16, reg,
					     &ad4695_regmap16_wr_table))
			ret = regmap_write(st->regmap16, reg, writeval);
	}
	iio_device_release_direct(indio_dev);

	return ret;
}

static int ad4695_get_current_scan_type(const struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan)
{
	struct ad4695_state *st = iio_priv(indio_dev);
	unsigned int osr = st->channels_cfg[chan->scan_index].oversampling_ratio;

	switch (osr) {
	case 1:
		return AD4695_SCAN_TYPE_OSR_1;
	case 4:
		return AD4695_SCAN_TYPE_OSR_4;
	case 16:
		return AD4695_SCAN_TYPE_OSR_16;
	case 64:
		return AD4695_SCAN_TYPE_OSR_64;
	default:
		return -EINVAL;
	}
}

static const struct iio_info ad4695_info = {
	.read_raw = &ad4695_read_raw,
	.write_raw_get_fmt = &ad4695_write_raw_get_fmt,
	.write_raw = &ad4695_write_raw,
	.read_avail = &ad4695_read_avail,
	.debugfs_reg_access = &ad4695_debugfs_reg_access,
};

static const struct iio_info ad4695_offload_info = {
	.read_raw = &ad4695_read_raw,
	.write_raw_get_fmt = &ad4695_write_raw_get_fmt,
	.write_raw = &ad4695_write_raw,
	.get_current_scan_type = &ad4695_get_current_scan_type,
	.read_avail = &ad4695_read_avail,
	.debugfs_reg_access = &ad4695_debugfs_reg_access,
};

static int ad4695_parse_channel_cfg(struct ad4695_state *st)
{
	struct device *dev = &st->spi->dev;
	struct ad4695_channel_config *chan_cfg;
	struct iio_chan_spec *iio_chan;
	int ret, i;

	/* populate defaults */
	for (i = 0; i < st->chip_info->num_voltage_inputs; i++) {
		chan_cfg = &st->channels_cfg[i];
		iio_chan = &st->iio_chan[i];

		chan_cfg->highz_en = true;
		chan_cfg->channel = i;

		/* This is the default OSR after reset */
		chan_cfg->oversampling_ratio = 1;

		*iio_chan = ad4695_channel_template;
		iio_chan->channel = i;
		iio_chan->scan_index = i;
		iio_chan->address = AD4695_CMD_VOLTAGE_CHAN(i);
	}

	/* modify based on firmware description */
	device_for_each_child_node_scoped(dev, child) {
		u32 reg, val;

		ret = fwnode_property_read_u32(child, "reg", &reg);
		if (ret)
			return dev_err_probe(dev, ret,
				"failed to read reg property (%s)\n",
				fwnode_get_name(child));

		if (reg >= st->chip_info->num_voltage_inputs)
			return dev_err_probe(dev, -EINVAL,
				"reg out of range (%s)\n",
				fwnode_get_name(child));

		iio_chan = &st->iio_chan[reg];
		chan_cfg = &st->channels_cfg[reg];

		chan_cfg->highz_en =
			!fwnode_property_read_bool(child, "adi,no-high-z");
		chan_cfg->bipolar = fwnode_property_read_bool(child, "bipolar");

		ret = fwnode_property_read_u32(child, "common-mode-channel",
					       &val);
		if (ret && ret != -EINVAL)
			return dev_err_probe(dev, ret,
				"failed to read common-mode-channel (%s)\n",
				fwnode_get_name(child));

		if (ret == -EINVAL || val == AD4695_COMMON_MODE_REFGND)
			chan_cfg->pin_pairing = AD4695_IN_PAIR_REFGND;
		else if (val == AD4695_COMMON_MODE_COM)
			chan_cfg->pin_pairing = AD4695_IN_PAIR_COM;
		else
			chan_cfg->pin_pairing = AD4695_IN_PAIR_EVEN_ODD;

		if (chan_cfg->pin_pairing == AD4695_IN_PAIR_EVEN_ODD &&
		    val % 2 == 0)
			return dev_err_probe(dev, -EINVAL,
				"common-mode-channel must be odd number (%s)\n",
				fwnode_get_name(child));

		if (chan_cfg->pin_pairing == AD4695_IN_PAIR_EVEN_ODD &&
		    val != reg + 1)
			return dev_err_probe(dev, -EINVAL,
				"common-mode-channel must be next consecutive channel (%s)\n",
				fwnode_get_name(child));

		if (chan_cfg->pin_pairing == AD4695_IN_PAIR_EVEN_ODD) {
			char name[5];

			snprintf(name, sizeof(name), "in%d", reg + 1);

			ret = devm_regulator_get_enable_read_voltage(dev, name);
			if (ret < 0)
				return dev_err_probe(dev, ret,
					"failed to get %s voltage (%s)\n",
					name, fwnode_get_name(child));

			chan_cfg->common_mode_mv = ret / 1000;
		}

		if (chan_cfg->bipolar &&
		    chan_cfg->pin_pairing == AD4695_IN_PAIR_REFGND)
			return dev_err_probe(dev, -EINVAL,
				"bipolar mode is not available for inputs paired with REFGND (%s).\n",
				fwnode_get_name(child));

		if (chan_cfg->bipolar)
			iio_chan->scan_type.sign = 's';

		ret = ad4695_write_chn_cfg(st, chan_cfg);
		if (ret)
			return ret;
	}

	/* Temperature channel must be next scan index after voltage channels. */
	st->iio_chan[i] = ad4695_temp_channel_template;
	st->iio_chan[i].scan_index = i;
	i++;

	st->iio_chan[i] = ad4695_soft_timestamp_channel_template;
	st->iio_chan[i].scan_index = i;

	return 0;
}

static bool ad4695_offload_trigger_match(struct spi_offload_trigger *trigger,
					 enum spi_offload_trigger_type type,
					 u64 *args, u32 nargs)
{
	if (type != SPI_OFFLOAD_TRIGGER_DATA_READY)
		return false;

	/*
	 * Requires 2 args:
	 * args[0] is the trigger event.
	 * args[1] is the GPIO pin number.
	 */
	if (nargs != 2 || args[0] != AD4695_TRIGGER_EVENT_BUSY)
		return false;

	return true;
}

static int ad4695_offload_trigger_request(struct spi_offload_trigger *trigger,
					  enum spi_offload_trigger_type type,
					  u64 *args, u32 nargs)
{
	struct ad4695_state *st = spi_offload_trigger_get_priv(trigger);

	/* Should already be validated by match, but just in case. */
	if (nargs != 2)
		return -EINVAL;

	/* DT tells us if BUSY event uses GP0 or GP3. */
	if (args[1] == AD4695_TRIGGER_PIN_GP3)
		return regmap_set_bits(st->regmap, AD4695_REG_GP_MODE,
				       AD4695_REG_GP_MODE_BUSY_GP_SEL);

	return regmap_clear_bits(st->regmap, AD4695_REG_GP_MODE,
				 AD4695_REG_GP_MODE_BUSY_GP_SEL);
}

static int
ad4695_offload_trigger_validate(struct spi_offload_trigger *trigger,
				struct spi_offload_trigger_config *config)
{
	if (config->type != SPI_OFFLOAD_TRIGGER_DATA_READY)
		return -EINVAL;

	return 0;
}

/*
 * NB: There are no enable/disable callbacks here due to requiring a SPI
 * message to enable or disable the BUSY output on the ADC.
 */
static const struct spi_offload_trigger_ops ad4695_offload_trigger_ops = {
	.match = ad4695_offload_trigger_match,
	.request = ad4695_offload_trigger_request,
	.validate = ad4695_offload_trigger_validate,
};

static void ad4695_pwm_disable(void *pwm)
{
	pwm_disable(pwm);
}

static int ad4695_probe_spi_offload(struct iio_dev *indio_dev,
				    struct ad4695_state *st)
{
	struct device *dev = &st->spi->dev;
	struct spi_offload_trigger_info trigger_info = {
		.fwnode = dev_fwnode(dev),
		.ops = &ad4695_offload_trigger_ops,
		.priv = st,
	};
	struct pwm_state pwm_state;
	struct dma_chan *rx_dma;
	int ret, i;

	indio_dev->info = &ad4695_offload_info;
	indio_dev->num_channels = st->chip_info->num_voltage_inputs + 1;
	indio_dev->setup_ops = &ad4695_offload_buffer_setup_ops;

	if (!st->cnv_gpio)
		return dev_err_probe(dev, -ENODEV,
				     "CNV GPIO is required for SPI offload\n");

	ret = devm_spi_offload_trigger_register(dev, &trigger_info);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register offload trigger\n");

	st->offload_trigger = devm_spi_offload_trigger_get(dev, st->offload,
		SPI_OFFLOAD_TRIGGER_DATA_READY);
	if (IS_ERR(st->offload_trigger))
		return dev_err_probe(dev, PTR_ERR(st->offload_trigger),
				     "failed to get offload trigger\n");

	ret = devm_mutex_init(dev, &st->cnv_pwm_lock);
	if (ret)
		return ret;

	st->cnv_pwm = devm_pwm_get(dev, NULL);
	if (IS_ERR(st->cnv_pwm))
		return dev_err_probe(dev, PTR_ERR(st->cnv_pwm),
				     "failed to get CNV PWM\n");

	pwm_init_state(st->cnv_pwm, &pwm_state);

	/* If firmware didn't provide default rate, use 10kHz (arbitrary). */
	if (pwm_state.period == 0)
		pwm_state.period = 100 * MILLI;

	pwm_state.enabled = true;

	ret = pwm_apply_might_sleep(st->cnv_pwm, &pwm_state);
	if (ret)
		return dev_err_probe(dev, ret, "failed to apply CNV PWM\n");

	ret = devm_add_action_or_reset(dev, ad4695_pwm_disable, st->cnv_pwm);
	if (ret)
		return ret;

	rx_dma = devm_spi_offload_rx_stream_request_dma_chan(dev, st->offload);
	if (IS_ERR(rx_dma))
		return dev_err_probe(dev, PTR_ERR(rx_dma),
				     "failed to get offload RX DMA\n");

	for (i = 0; i < indio_dev->num_channels; i++) {
		struct iio_chan_spec *chan = &st->iio_chan[i];
		struct ad4695_channel_config *cfg;

		/*
		 * NB: When using offload support, all channels need to have the
		 * same bits_per_word because they all use the same SPI message
		 * for reading one sample. In order to prevent breaking
		 * userspace in the future when oversampling support is added,
		 * all channels are set read 19 bits with a shift of 3 to mask
		 * out the extra bits even though we currently only support 16
		 * bit samples (oversampling ratio == 1).
		 */
		chan->scan_type.shift = 3;
		chan->scan_type.storagebits = 32;
		/* add sample frequency for PWM CNV trigger */
		chan->info_mask_separate |= BIT(IIO_CHAN_INFO_SAMP_FREQ);
		chan->info_mask_separate_available |= BIT(IIO_CHAN_INFO_SAMP_FREQ);

		/* Add the oversampling properties only for voltage channels */
		if (chan->type != IIO_VOLTAGE)
			continue;

		cfg = &st->channels_cfg[i];

		chan->info_mask_separate |= BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO);
		chan->info_mask_separate_available |=
			BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO);
		chan->has_ext_scan_type = 1;
		if (cfg->bipolar) {
			chan->ext_scan_type = ad4695_scan_type_offload_s;
			chan->num_ext_scan_type =
				ARRAY_SIZE(ad4695_scan_type_offload_s);
		} else {
			chan->ext_scan_type = ad4695_scan_type_offload_u;
			chan->num_ext_scan_type =
				ARRAY_SIZE(ad4695_scan_type_offload_u);
		}
	}

	return devm_iio_dmaengine_buffer_setup_with_handle(dev, indio_dev,
		rx_dma, IIO_BUFFER_DIRECTION_IN);
}

static const struct spi_offload_config ad4695_spi_offload_config = {
	.capability_flags = SPI_OFFLOAD_CAP_TRIGGER |
			    SPI_OFFLOAD_CAP_RX_STREAM_DMA,
};

static int ad4695_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct ad4695_state *st;
	struct iio_dev *indio_dev;
	bool use_internal_ldo_supply;
	bool use_internal_ref_buffer;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->spi = spi;

	st->chip_info = spi_get_device_match_data(spi);
	if (!st->chip_info)
		return -EINVAL;

	st->sample_freq_range[0] = 1; /* min */
	st->sample_freq_range[1] = 1; /* step */
	st->sample_freq_range[2] = st->chip_info->max_sample_rate; /* max */

	st->regmap = devm_regmap_init(dev, &ad4695_regmap_bus, st,
				      &ad4695_regmap_config);
	if (IS_ERR(st->regmap))
		return dev_err_probe(dev, PTR_ERR(st->regmap),
				     "Failed to initialize regmap\n");

	st->regmap16 = devm_regmap_init(dev, &ad4695_regmap_bus, st,
					&ad4695_regmap16_config);
	if (IS_ERR(st->regmap16))
		return dev_err_probe(dev, PTR_ERR(st->regmap16),
				     "Failed to initialize regmap16\n");

	st->cnv_gpio = devm_gpiod_get_optional(dev, "cnv", GPIOD_OUT_LOW);
	if (IS_ERR(st->cnv_gpio))
		return dev_err_probe(dev, PTR_ERR(st->cnv_gpio),
				     "Failed to get CNV GPIO\n");

	ret = devm_regulator_bulk_get_enable(dev,
					     ARRAY_SIZE(ad4695_power_supplies),
					     ad4695_power_supplies);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to enable power supplies\n");

	/* If LDO_IN supply is present, then we are using internal LDO. */
	ret = devm_regulator_get_enable_optional(dev, "ldo-in");
	if (ret < 0 && ret != -ENODEV)
		return dev_err_probe(dev, ret,
				     "Failed to enable LDO_IN supply\n");

	use_internal_ldo_supply = ret == 0;

	if (!use_internal_ldo_supply) {
		/* Otherwise we need an external VDD supply. */
		ret = devm_regulator_get_enable(dev, "vdd");
		if (ret < 0)
			return dev_err_probe(dev, ret,
					     "Failed to enable VDD supply\n");
	}

	/* If REFIN supply is given, then we are using internal buffer */
	ret = devm_regulator_get_enable_read_voltage(dev, "refin");
	if (ret < 0 && ret != -ENODEV)
		return dev_err_probe(dev, ret, "Failed to get REFIN voltage\n");

	if (ret != -ENODEV) {
		st->vref_mv = ret / 1000;
		use_internal_ref_buffer = true;
	} else {
		/* Otherwise, we need an external reference. */
		ret = devm_regulator_get_enable_read_voltage(dev, "ref");
		if (ret < 0)
			return dev_err_probe(dev, ret,
					     "Failed to get REF voltage\n");

		st->vref_mv = ret / 1000;
		use_internal_ref_buffer = false;
	}

	ret = devm_regulator_get_enable_read_voltage(dev, "com");
	if (ret < 0 && ret != -ENODEV)
		return dev_err_probe(dev, ret, "Failed to get COM voltage\n");

	st->com_mv = ret == -ENODEV ? 0 : ret / 1000;

	/*
	 * Reset the device using hardware reset if available or fall back to
	 * software reset.
	 */

	st->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(st->reset_gpio))
		return PTR_ERR(st->reset_gpio);

	if (st->reset_gpio) {
		gpiod_set_value(st->reset_gpio, 0);
		msleep(AD4695_T_WAKEUP_HW_MS);
	} else {
		ret = regmap_write(st->regmap, AD4695_REG_SPI_CONFIG_A,
				   AD4695_REG_SPI_CONFIG_A_SW_RST);
		if (ret)
			return ret;

		msleep(AD4695_T_WAKEUP_SW_MS);
	}

	/* Needed for regmap16 to be able to work correctly. */
	ret = regmap_set_bits(st->regmap, AD4695_REG_SPI_CONFIG_A,
			      AD4695_REG_SPI_CONFIG_A_ADDR_DIR);
	if (ret)
		return ret;

	/* Disable internal LDO if it isn't needed. */
	ret = regmap_update_bits(st->regmap, AD4695_REG_SETUP,
				 AD4695_REG_SETUP_LDO_EN,
				 FIELD_PREP(AD4695_REG_SETUP_LDO_EN,
					    use_internal_ldo_supply ? 1 : 0));
	if (ret)
		return ret;

	/* configure reference supply */

	if (device_property_present(dev, "adi,no-ref-current-limit")) {
		ret = regmap_set_bits(st->regmap, AD4695_REG_REF_CTRL,
				      AD4695_REG_REF_CTRL_OV_MODE);
		if (ret)
			return ret;
	}

	if (device_property_present(dev, "adi,no-ref-high-z")) {
		if (use_internal_ref_buffer)
			return dev_err_probe(dev, -EINVAL,
				"Cannot disable high-Z mode for internal reference buffer\n");

		ret = regmap_clear_bits(st->regmap, AD4695_REG_REF_CTRL,
					AD4695_REG_REF_CTRL_REFHIZ_EN);
		if (ret)
			return ret;
	}

	ret = ad4695_set_ref_voltage(st, st->vref_mv);
	if (ret)
		return ret;

	if (use_internal_ref_buffer) {
		ret = regmap_set_bits(st->regmap, AD4695_REG_REF_CTRL,
				      AD4695_REG_REF_CTRL_REFBUF_EN);
		if (ret)
			return ret;

		/* Give the capacitor some time to charge up. */
		msleep(AD4695_T_REFBUF_MS);
	}

	ret = ad4695_parse_channel_cfg(st);
	if (ret)
		return ret;

	indio_dev->name = st->chip_info->name;
	indio_dev->info = &ad4695_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = st->iio_chan;
	indio_dev->num_channels = st->chip_info->num_voltage_inputs + 2;

	st->offload = devm_spi_offload_get(dev, spi, &ad4695_spi_offload_config);
	ret = PTR_ERR_OR_ZERO(st->offload);
	if (ret && ret != -ENODEV)
		return dev_err_probe(dev, ret, "failed to get SPI offload\n");

	/* If no SPI offload, fall back to low speed usage. */
	if (ret == -ENODEV) {
		/* Driver currently requires CNV pin to be connected to SPI CS */
		if (st->cnv_gpio)
			return dev_err_probe(dev, -EINVAL,
					     "CNV GPIO is not supported\n");

		indio_dev->num_channels = st->chip_info->num_voltage_inputs + 2;

		ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
						      iio_pollfunc_store_time,
						      ad4695_trigger_handler,
						      &ad4695_buffer_setup_ops);
		if (ret)
			return ret;
	} else {
		ret = ad4695_probe_spi_offload(indio_dev, st);
		if (ret)
			return ret;
	}

	return devm_iio_device_register(dev, indio_dev);
}

static const struct spi_device_id ad4695_spi_id_table[] = {
	{ .name = "ad4695", .driver_data = (kernel_ulong_t)&ad4695_chip_info },
	{ .name = "ad4696", .driver_data = (kernel_ulong_t)&ad4696_chip_info },
	{ .name = "ad4697", .driver_data = (kernel_ulong_t)&ad4697_chip_info },
	{ .name = "ad4698", .driver_data = (kernel_ulong_t)&ad4698_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad4695_spi_id_table);

static const struct of_device_id ad4695_of_match_table[] = {
	{ .compatible = "adi,ad4695", .data = &ad4695_chip_info, },
	{ .compatible = "adi,ad4696", .data = &ad4696_chip_info, },
	{ .compatible = "adi,ad4697", .data = &ad4697_chip_info, },
	{ .compatible = "adi,ad4698", .data = &ad4698_chip_info, },
	{ }
};
MODULE_DEVICE_TABLE(of, ad4695_of_match_table);

static struct spi_driver ad4695_driver = {
	.driver = {
		.name = "ad4695",
		.of_match_table = ad4695_of_match_table,
	},
	.probe = ad4695_probe,
	.id_table = ad4695_spi_id_table,
};
module_spi_driver(ad4695_driver);

MODULE_AUTHOR("Ramona Gradinariu <ramona.gradinariu@analog.com>");
MODULE_AUTHOR("David Lechner <dlechner@baylibre.com>");
MODULE_DESCRIPTION("Analog Devices AD4695 ADC driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_DMAENGINE_BUFFER");
