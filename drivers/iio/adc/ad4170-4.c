// SPDX-License-Identifier: GPL-2.0+
/*
 * Analog Devices AD4170-4 ADC driver
 *
 * Copyright (C) 2025 Analog Devices, Inc.
 * Author: Ana-Maria Cusco <ana-maria.cusco@analog.com>
 * Author: Marcelo Schmitt <marcelo.schmitt@analog.com>
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/math64.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/unaligned.h>
#include <linux/units.h>
#include <linux/util_macros.h>

/*
 * AD4170 registers
 * Multibyte register addresses point to the most significant byte which is the
 * address to use to get the most significant byte first (address accessed is
 * decremented by one for each data byte)
 *
 * Each register address define follows the AD4170_<REG_NAME>_REG format.
 * Each mask follows the AD4170_<REG_NAME>_<FIELD_NAME> format.
 * E.g. AD4170_PIN_MUXING_DIG_AUX1_CTRL_MSK is for accessing DIG_AUX1_CTRL field
 * of PIN_MUXING_REG.
 * Each constant follows the AD4170_<REG_NAME>_<FIELD_NAME>_<FUNCTION> format.
 * E.g. AD4170_PIN_MUXING_DIG_AUX1_DISABLED is the value written to
 * DIG_AUX1_CTRL field of PIN_MUXING register to disable DIG_AUX1 pin.
 * Some register names and register field names are shortened versions of
 * their datasheet counterpart names to provide better code readability.
 */
#define AD4170_CONFIG_A_REG				0x00
#define AD4170_DATA_24B_REG				0x1E
#define AD4170_PIN_MUXING_REG				0x69
#define AD4170_CLOCK_CTRL_REG				0x6B
#define AD4170_ADC_CTRL_REG				0x71
#define AD4170_CHAN_EN_REG				0x79
#define AD4170_CHAN_SETUP_REG(x)			(0x81 + 4 * (x))
#define AD4170_CHAN_MAP_REG(x)				(0x83 + 4 * (x))
#define AD4170_MISC_REG(x)				(0xC1 + 14 * (x))
#define AD4170_AFE_REG(x)				(0xC3 + 14 * (x))
#define AD4170_FILTER_REG(x)				(0xC5 + 14 * (x))
#define AD4170_FILTER_FS_REG(x)				(0xC7 + 14 * (x))
#define AD4170_OFFSET_REG(x)				(0xCA + 14 * (x))
#define AD4170_GAIN_REG(x)				(0xCD + 14 * (x))
#define AD4170_V_BIAS_REG				0x135
#define AD4170_CURRENT_SRC_REG(x)			(0x139 + 2 * (x))
#define AD4170_GPIO_MODE_REG				0x191
#define AD4170_GPIO_OUTPUT_REG				0x193
#define AD4170_GPIO_INPUT_REG				0x195
#define AD4170_ADC_CTRL_CONT_READ_EXIT_REG		0x200 /* virtual reg */

#define AD4170_REG_READ_MASK				BIT(14)

/* AD4170_CONFIG_A_REG - INTERFACE_CONFIG_A REGISTER */
#define AD4170_SW_RESET_MSK				(BIT(7) | BIT(0))

/* AD4170_PIN_MUXING_REG */
#define AD4170_PIN_MUXING_DIG_AUX1_CTRL_MSK		GENMASK(5, 4)

/* AD4170_CLOCK_CTRL_REG */
#define AD4170_CLOCK_CTRL_CLOCKSEL_MSK			GENMASK(1, 0)

/* AD4170_ADC_CTRL_REG */
#define AD4170_ADC_CTRL_MULTI_DATA_REG_SEL_MSK		BIT(7)
#define AD4170_ADC_CTRL_CONT_READ_MSK			GENMASK(5, 4)
#define AD4170_ADC_CTRL_MODE_MSK			GENMASK(3, 0)

/* AD4170_CHAN_EN_REG */
#define AD4170_CHAN_EN(ch)				BIT(ch)

/* AD4170_CHAN_SETUP_REG */
#define AD4170_CHAN_SETUP_SETUP_MSK			GENMASK(2, 0)

/* AD4170_CHAN_MAP_REG */
#define AD4170_CHAN_MAP_AINP_MSK			GENMASK(12, 8)
#define AD4170_CHAN_MAP_AINM_MSK			GENMASK(4, 0)

/* AD4170_MISC_REG */
#define AD4170_MISC_CHOP_IEXC_MSK			GENMASK(15, 14)
#define AD4170_MISC_CHOP_ADC_MSK			GENMASK(9, 8)

/* AD4170_AFE_REG */
#define AD4170_AFE_REF_BUF_M_MSK			GENMASK(11, 10)
#define AD4170_AFE_REF_BUF_P_MSK			GENMASK(9, 8)
#define AD4170_AFE_REF_SELECT_MSK			GENMASK(6, 5)
#define AD4170_AFE_BIPOLAR_MSK				BIT(4)
#define AD4170_AFE_PGA_GAIN_MSK				GENMASK(3, 0)

/* AD4170_FILTER_REG */
#define AD4170_FILTER_FILTER_TYPE_MSK			GENMASK(3, 0)

/* AD4170_CURRENT_SRC_REG */
#define AD4170_CURRENT_SRC_I_OUT_PIN_MSK		GENMASK(12, 8)
#define AD4170_CURRENT_SRC_I_OUT_VAL_MSK		GENMASK(2, 0)

/* AD4170_GPIO_MODE_REG */
#define AD4170_GPIO_MODE_GPIO0_MSK			GENMASK(1, 0)
#define AD4170_GPIO_MODE_GPIO1_MSK			GENMASK(3, 2)
#define AD4170_GPIO_MODE_GPIO2_MSK			GENMASK(5, 4)
#define AD4170_GPIO_MODE_GPIO3_MSK			GENMASK(7, 6)

/* AD4170_GPIO_OUTPUT_REG */
#define AD4170_GPIO_OUTPUT_GPIO_MSK(x)			BIT(x)

/* AD4170 register constants */

/* AD4170_CLOCK_CTRL_REG constants */
#define AD4170_CLOCK_CTRL_CLOCKSEL_INT			0x0
#define AD4170_CLOCK_CTRL_CLOCKSEL_INT_OUT		0x1
#define AD4170_CLOCK_CTRL_CLOCKSEL_EXT			0x2
#define AD4170_CLOCK_CTRL_CLOCKSEL_EXT_XTAL		0x3

/* AD4170_CHAN_MAP_REG constants */
#define AD4170_CHAN_MAP_AIN(x)			(x)
#define AD4170_CHAN_MAP_TEMP_SENSOR		17
#define AD4170_CHAN_MAP_AVDD_AVSS_P		18
#define AD4170_CHAN_MAP_AVDD_AVSS_N		18
#define AD4170_CHAN_MAP_IOVDD_DGND_P		19
#define AD4170_CHAN_MAP_IOVDD_DGND_N		19
#define AD4170_CHAN_MAP_AVSS			23
#define AD4170_CHAN_MAP_DGND			24
#define AD4170_CHAN_MAP_REFIN1_P		25
#define AD4170_CHAN_MAP_REFIN1_N		26
#define AD4170_CHAN_MAP_REFIN2_P		27
#define AD4170_CHAN_MAP_REFIN2_N		28
#define AD4170_CHAN_MAP_REFOUT			29

/* AD4170_MISC_REG constants */
#define AD4170_MISC_CHOP_IEXC_PAIR1			0x1
#define AD4170_MISC_CHOP_IEXC_PAIR2			0x2
#define AD4170_MISC_CHOP_IEXC_BOTH			0x3

/* AD4170_PIN_MUXING_REG constants */
#define AD4170_PIN_MUXING_DIG_AUX1_DISABLED		0x0
#define AD4170_PIN_MUXING_DIG_AUX1_RDY			0x1

/* AD4170_ADC_CTRL_REG constants */
#define AD4170_ADC_CTRL_MODE_CONT			0x0
#define AD4170_ADC_CTRL_MODE_SINGLE			0x4
#define AD4170_ADC_CTRL_MODE_IDLE			0x7

#define AD4170_ADC_CTRL_CONT_READ_DISABLE		0x0
#define AD4170_ADC_CTRL_CONT_READ_ENABLE		0x1

/* AD4170_FILTER_REG constants */
#define AD4170_FILTER_FILTER_TYPE_SINC5_AVG		0x0
#define AD4170_FILTER_FILTER_TYPE_SINC5			0x4
#define AD4170_FILTER_FILTER_TYPE_SINC3			0x6

/* AD4170_CURRENT_SRC_REG constants */
#define AD4170_CURRENT_SRC_I_OUT_PIN_AIN(x)		(x)
#define AD4170_CURRENT_SRC_I_OUT_PIN_GPIO(x)		((x) + 17)

/* AD4170_GPIO_MODE_REG constants */
#define AD4170_GPIO_MODE_GPIO_INPUT			1
#define AD4170_GPIO_MODE_GPIO_OUTPUT			2

/* Device properties and auxiliary constants */

#define AD4170_NUM_ANALOG_PINS				9
#define AD4170_NUM_GPIO_PINS				4
#define AD4170_MAX_ADC_CHANNELS				16
#define AD4170_MAX_IIO_CHANNELS				(AD4170_MAX_ADC_CHANNELS + 1)
#define AD4170_MAX_ANALOG_PINS				8
#define AD4170_MAX_SETUPS				8
#define AD4170_INVALID_SETUP				9
#define AD4170_SPI_INST_PHASE_LEN			2
#define AD4170_SPI_MAX_XFER_LEN				6
#define AD4170_NUM_CURRENT_SRC				4
#define AD4170_DEFAULT_SAMP_RATE			(125 * HZ_PER_KHZ)

#define AD4170_INT_REF_2_5V				2500000

/* Internal and external clock properties */
#define AD4170_INT_CLOCK_16MHZ				(16 * HZ_PER_MHZ)
#define AD4170_EXT_CLOCK_MHZ_MIN			(1 * HZ_PER_MHZ)
#define AD4170_EXT_CLOCK_MHZ_MAX			(17 * HZ_PER_MHZ)

#define AD4170_NUM_PGA_OPTIONS				10

/* Digital filter properties */
#define AD4170_SINC3_MIN_FS				4
#define AD4170_SINC3_MAX_FS				65532
#define AD4170_SINC5_MIN_FS				1
#define AD4170_SINC5_MAX_FS				256

#define AD4170_GAIN_REG_DEFAULT				0x555555

#define AD4170_ADC_CTRL_CONT_READ_EXIT			0xA5

/* Analog pin functions  */
#define AD4170_PIN_UNASSIGNED				0x00
#define AD4170_PIN_ANALOG_IN				0x01
#define AD4170_PIN_CURRENT_OUT				0x02
#define AD4170_PIN_VBIAS				0x04

/* GPIO pin functions  */
#define AD4170_GPIO_UNASSIGNED				0x00
#define AD4170_GPIO_AC_EXCITATION			0x02
#define AD4170_GPIO_OUTPUT				0x04

/* Current source */
#define AD4170_CURRENT_SRC_DISABLED			0xFF

static const unsigned int ad4170_reg_size[] = {
	[AD4170_CONFIG_A_REG] = 1,
	[AD4170_DATA_24B_REG] = 3,
	[AD4170_PIN_MUXING_REG] = 2,
	[AD4170_CLOCK_CTRL_REG] = 2,
	[AD4170_ADC_CTRL_REG] = 2,
	[AD4170_CHAN_EN_REG] = 2,
	/*
	 * CHANNEL_SETUP and CHANNEL_MAP register are all 2 byte size each and
	 * their addresses are interleaved such that we have CHANNEL_SETUP0
	 * address followed by CHANNEL_MAP0 address, followed by CHANNEL_SETUP1,
	 * and so on until CHANNEL_MAP15.
	 * Thus, initialize the register size for them only once.
	 */
	[AD4170_CHAN_SETUP_REG(0) ... AD4170_CHAN_MAP_REG(AD4170_MAX_ADC_CHANNELS - 1)] = 2,
	/*
	 * MISC, AFE, FILTER, FILTER_FS, OFFSET, and GAIN register addresses are
	 * also interleaved but MISC, AFE, FILTER, FILTER_FS, OFFSET are 16-bit
	 * while OFFSET, GAIN are 24-bit registers so we can't init them all to
	 * the same size.
	 */
	[AD4170_MISC_REG(0) ... AD4170_FILTER_FS_REG(0)] = 2,
	[AD4170_MISC_REG(1) ... AD4170_FILTER_FS_REG(1)] = 2,
	[AD4170_MISC_REG(2) ... AD4170_FILTER_FS_REG(2)] = 2,
	[AD4170_MISC_REG(3) ... AD4170_FILTER_FS_REG(3)] = 2,
	[AD4170_MISC_REG(4) ... AD4170_FILTER_FS_REG(4)] = 2,
	[AD4170_MISC_REG(5) ... AD4170_FILTER_FS_REG(5)] = 2,
	[AD4170_MISC_REG(6) ... AD4170_FILTER_FS_REG(6)] = 2,
	[AD4170_MISC_REG(7) ... AD4170_FILTER_FS_REG(7)] = 2,
	[AD4170_OFFSET_REG(0) ... AD4170_GAIN_REG(0)] = 3,
	[AD4170_OFFSET_REG(1) ... AD4170_GAIN_REG(1)] = 3,
	[AD4170_OFFSET_REG(2) ... AD4170_GAIN_REG(2)] = 3,
	[AD4170_OFFSET_REG(3) ... AD4170_GAIN_REG(3)] = 3,
	[AD4170_OFFSET_REG(4) ... AD4170_GAIN_REG(4)] = 3,
	[AD4170_OFFSET_REG(5) ... AD4170_GAIN_REG(5)] = 3,
	[AD4170_OFFSET_REG(6) ... AD4170_GAIN_REG(6)] = 3,
	[AD4170_OFFSET_REG(7) ... AD4170_GAIN_REG(7)] = 3,
	[AD4170_V_BIAS_REG] = 2,
	[AD4170_CURRENT_SRC_REG(0) ... AD4170_CURRENT_SRC_REG(3)] = 2,
	[AD4170_GPIO_MODE_REG] = 2,
	[AD4170_GPIO_OUTPUT_REG] = 2,
	[AD4170_GPIO_INPUT_REG] = 2,
	[AD4170_ADC_CTRL_CONT_READ_EXIT_REG] = 0,
};

enum ad4170_ref_buf {
	AD4170_REF_BUF_PRE,	/* Pre-charge referrence buffer */
	AD4170_REF_BUF_FULL,	/* Full referrence buffering */
	AD4170_REF_BUF_BYPASS,	/* Bypass referrence buffering */
};

/* maps adi,positive/negative-reference-buffer property values to enum */
static const char * const ad4170_ref_buf_str[] = {
	[AD4170_REF_BUF_PRE] = "precharge",
	[AD4170_REF_BUF_FULL] = "full",
	[AD4170_REF_BUF_BYPASS] = "disabled",
};

enum ad4170_ref_select {
	AD4170_REF_REFIN1,
	AD4170_REF_REFIN2,
	AD4170_REF_REFOUT,
	AD4170_REF_AVDD,
};

enum ad4170_filter_type {
	AD4170_SINC5_AVG,
	AD4170_SINC5,
	AD4170_SINC3,
};

enum ad4170_regulator {
	AD4170_AVDD_SUP,
	AD4170_AVSS_SUP,
	AD4170_IOVDD_SUP,
	AD4170_REFIN1P_SUP,
	AD4170_REFIN1N_SUP,
	AD4170_REFIN2P_SUP,
	AD4170_REFIN2N_SUP,
	AD4170_MAX_SUP,
};

static const char *const ad4170_clk_sel[] = {
	"ext-clk", "xtal",
};

enum ad4170_int_pin_sel {
	AD4170_INT_PIN_SDO,
	AD4170_INT_PIN_DIG_AUX1,
};

static const char * const ad4170_int_pin_names[] = {
	[AD4170_INT_PIN_SDO] = "sdo",
	[AD4170_INT_PIN_DIG_AUX1] = "dig_aux1",
};

static const unsigned int ad4170_sinc3_filt_fs_tbl[] = {
	4, 8, 12, 16, 20, 40, 48, 80,			/*  0 -  7 */
	100, 256, 500, 1000, 5000, 8332, 10000, 25000,	/*  8 - 15 */
	50000, 65532,					/* 16 - 17 */
};

#define AD4170_MAX_FS_TBL_SIZE		ARRAY_SIZE(ad4170_sinc3_filt_fs_tbl)

static const unsigned int ad4170_sinc5_filt_fs_tbl[] = {
	1, 2, 4, 8, 12, 16, 20, 40, 48, 80, 100, 256,
};

static const unsigned int ad4170_iout_pin_tbl[] = {
	AD4170_CURRENT_SRC_I_OUT_PIN_AIN(0),
	AD4170_CURRENT_SRC_I_OUT_PIN_AIN(1),
	AD4170_CURRENT_SRC_I_OUT_PIN_AIN(2),
	AD4170_CURRENT_SRC_I_OUT_PIN_AIN(3),
	AD4170_CURRENT_SRC_I_OUT_PIN_AIN(4),
	AD4170_CURRENT_SRC_I_OUT_PIN_AIN(5),
	AD4170_CURRENT_SRC_I_OUT_PIN_AIN(6),
	AD4170_CURRENT_SRC_I_OUT_PIN_AIN(7),
	AD4170_CURRENT_SRC_I_OUT_PIN_AIN(8),
	AD4170_CURRENT_SRC_I_OUT_PIN_GPIO(0),
	AD4170_CURRENT_SRC_I_OUT_PIN_GPIO(1),
	AD4170_CURRENT_SRC_I_OUT_PIN_GPIO(2),
	AD4170_CURRENT_SRC_I_OUT_PIN_GPIO(3),
};

static const unsigned int ad4170_iout_current_ua_tbl[] = {
	0, 10, 50, 100, 250, 500, 1000, 1500,
};

enum ad4170_sensor_enum {
	AD4170_ADC_SENSOR = 0,
	AD4170_WEIGH_SCALE_SENSOR = 1,
	AD4170_RTD_SENSOR = 2,
	AD4170_THERMOCOUPLE_SENSOR = 3,
};

/* maps adi,sensor-type property value to enum */
static const char * const ad4170_sensor_type[] = {
	[AD4170_ADC_SENSOR] = "adc",
	[AD4170_WEIGH_SCALE_SENSOR] = "weighscale",
	[AD4170_RTD_SENSOR] = "rtd",
	[AD4170_THERMOCOUPLE_SENSOR] = "thermocouple",
};

struct ad4170_chip_info {
	const char *name;
};

static const struct ad4170_chip_info ad4170_chip_info = {
	.name = "ad4170-4",
};

static const struct ad4170_chip_info ad4190_chip_info = {
	.name = "ad4190-4",
};

static const struct ad4170_chip_info ad4195_chip_info = {
	.name = "ad4195-4",
};

/*
 * There are 8 of each MISC, AFE, FILTER, FILTER_FS, OFFSET, and GAIN
 * configuration registers. That is, there are 8 miscellaneous registers, MISC0
 * to MISC7. Each MISC register is associated with a setup; MISCN is associated
 * with setup number N. The other 5 above mentioned types of registers have
 * analogous structure. A setup is a set of those registers. For example,
 * setup 1 comprises of MISC1, AFE1, FILTER1, FILTER_FS1, OFFSET1, and GAIN1
 * registers. Also, there are 16 CHANNEL_SETUP registers (CHANNEL_SETUP0 to
 * CHANNEL_SETUP15). Each channel setup is associated with one of the 8 possible
 * setups. Thus, AD4170 can support up to 16 channels but, since there are only
 * 8 available setups, channels must share settings if more than 8 channels are
 * configured.
 *
 * If this struct is modified, ad4170_setup_eq() will probably need to be
 * updated too.
 */
struct ad4170_setup {
	u16 misc;
	u16 afe;
	u16 filter;
	u16 filter_fs;
	u32 offset; /* For calibration purposes */
	u32 gain; /* For calibration purposes */
};

struct ad4170_setup_info {
	struct ad4170_setup setup;
	unsigned int enabled_channels;
	unsigned int channels;
};

struct ad4170_chan_info {
	unsigned int input_range_uv;
	unsigned int setup_num; /* Index to access state setup_infos array */
	struct ad4170_setup setup; /* cached setup */
	int offset_tbl[10];
	u32 scale_tbl[10][2];
	bool initialized;
	bool enabled;
};

static const char * const ad4170_filt_names[] = {
	[AD4170_SINC5_AVG] = "sinc5+avg",
	[AD4170_SINC5] = "sinc5",
	[AD4170_SINC3] = "sinc3",
};

struct ad4170_state {
	struct mutex lock; /* Protect read-modify-write and multi write sequences */
	int vrefs_uv[AD4170_MAX_SUP];
	u32 mclk_hz;
	struct ad4170_setup_info setup_infos[AD4170_MAX_SETUPS];
	struct ad4170_chan_info chan_infos[AD4170_MAX_ADC_CHANNELS];
	struct completion completion;
	struct iio_chan_spec chans[AD4170_MAX_IIO_CHANNELS];
	struct spi_device *spi;
	struct regmap *regmap;
	int sps_tbl[ARRAY_SIZE(ad4170_filt_names)][AD4170_MAX_FS_TBL_SIZE][2];
	__be32 bounce_buffer[AD4170_MAX_ADC_CHANNELS];
	struct spi_message msg;
	struct spi_transfer xfer;
	struct iio_trigger *trig;
	struct clk_hw int_clk_hw;
	unsigned int clock_ctrl;
	unsigned int pins_fn[AD4170_NUM_ANALOG_PINS];
	int gpio_fn[AD4170_NUM_GPIO_PINS];
	unsigned int cur_src_pins[AD4170_NUM_CURRENT_SRC];
	struct gpio_chip gpiochip;
	/*
	 * DMA (thus cache coherency maintenance) requires the transfer buffers
	 * to live in their own cache lines.
	 */
	u8 rx_buf[4] __aligned(IIO_DMA_MINALIGN);
};

static void ad4170_fill_sps_tbl(struct ad4170_state *st)
{
	unsigned int tmp0, tmp1, i;

	/*
	 * The ODR can be calculated the same way for sinc5+avg, sinc5, and
	 * sinc3 filter types with the exception that sinc5 filter has a
	 * narrowed range of allowed FILTER_FS values.
	 */
	for (i = 0; i < ARRAY_SIZE(ad4170_sinc3_filt_fs_tbl); i++) {
		tmp0 = div_u64_rem(st->mclk_hz, 32 * ad4170_sinc3_filt_fs_tbl[i],
				   &tmp1);
		tmp1 = mult_frac(tmp1, MICRO, 32 * ad4170_sinc3_filt_fs_tbl[i]);
		/* Fill sinc5+avg filter SPS table */
		st->sps_tbl[AD4170_SINC5_AVG][i][0] = tmp0; /* Integer part */
		st->sps_tbl[AD4170_SINC5_AVG][i][1] = tmp1; /* Fractional part */

		/* Fill sinc3 filter SPS table */
		st->sps_tbl[AD4170_SINC3][i][0] = tmp0; /* Integer part */
		st->sps_tbl[AD4170_SINC3][i][1] = tmp1; /* Fractional part */
	}
	/* Sinc5 filter ODR doesn't use all FILTER_FS bits */
	for (i = 0; i < ARRAY_SIZE(ad4170_sinc5_filt_fs_tbl); i++) {
		tmp0 = div_u64_rem(st->mclk_hz, 32 * ad4170_sinc5_filt_fs_tbl[i],
				   &tmp1);
		tmp1 = mult_frac(tmp1, MICRO, 32 * ad4170_sinc5_filt_fs_tbl[i]);
		/* Fill sinc5 filter SPS table */
		st->sps_tbl[AD4170_SINC5][i][0] = tmp0; /* Integer part */
		st->sps_tbl[AD4170_SINC5][i][1] = tmp1; /* Fractional part */
	}
}

static int ad4170_debugfs_reg_access(struct iio_dev *indio_dev,
				     unsigned int reg, unsigned int writeval,
				     unsigned int *readval)
{
	struct ad4170_state *st = iio_priv(indio_dev);

	if (readval)
		return regmap_read(st->regmap, reg, readval);

	return regmap_write(st->regmap, reg, writeval);
}

static int ad4170_get_reg_size(struct ad4170_state *st, unsigned int reg,
			       unsigned int *size)
{
	if (reg >= ARRAY_SIZE(ad4170_reg_size))
		return -EINVAL;

	*size = ad4170_reg_size[reg];

	return 0;
}

static int ad4170_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct ad4170_state *st = context;
	u8 tx_buf[AD4170_SPI_MAX_XFER_LEN];
	unsigned int size;
	int ret;

	ret = ad4170_get_reg_size(st, reg, &size);
	if (ret)
		return ret;

	put_unaligned_be16(reg, tx_buf);
	switch (size) {
	case 3:
		put_unaligned_be24(val, &tx_buf[AD4170_SPI_INST_PHASE_LEN]);
		break;
	case 2:
		put_unaligned_be16(val, &tx_buf[AD4170_SPI_INST_PHASE_LEN]);
		break;
	case 1:
		tx_buf[AD4170_SPI_INST_PHASE_LEN] = val;
		break;
	case 0:
		/* Write continuous read exit code */
		tx_buf[0] = AD4170_ADC_CTRL_CONT_READ_EXIT;
		return spi_write_then_read(st->spi, tx_buf, 1, NULL, 0);
	default:
		return -EINVAL;
	}

	return spi_write_then_read(st->spi, tx_buf,
				   AD4170_SPI_INST_PHASE_LEN + size, NULL, 0);
}

static int ad4170_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct ad4170_state *st = context;
	u8 tx_buf[AD4170_SPI_INST_PHASE_LEN];
	unsigned int size;
	int ret;

	put_unaligned_be16(AD4170_REG_READ_MASK | reg, tx_buf);

	ret = ad4170_get_reg_size(st, reg, &size);
	if (ret)
		return ret;

	ret = spi_write_then_read(st->spi, tx_buf, ARRAY_SIZE(tx_buf),
				  st->rx_buf, size);
	if (ret)
		return ret;

	switch (size) {
	case 3:
		*val = get_unaligned_be24(st->rx_buf);
		return 0;
	case 2:
		*val = get_unaligned_be16(st->rx_buf);
		return 0;
	case 1:
		*val = st->rx_buf[0];
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct regmap_config ad4170_regmap_config = {
	.reg_read = ad4170_reg_read,
	.reg_write = ad4170_reg_write,
};

static bool ad4170_setup_eq(struct ad4170_setup *a, struct ad4170_setup *b)
{
	if (a->misc != b->misc ||
	    a->afe != b->afe ||
	    a->filter != b->filter ||
	    a->filter_fs != b->filter_fs ||
	    a->offset != b->offset ||
	    a->gain != b->gain)
		return false;

	return true;
}

static int ad4170_find_setup(struct ad4170_state *st,
			     struct ad4170_setup *target_setup,
			     unsigned int *setup_num, bool *overwrite)
{
	unsigned int i;

	*setup_num = AD4170_INVALID_SETUP;
	*overwrite = false;

	for (i = 0; i < AD4170_MAX_SETUPS; i++) {
		struct ad4170_setup_info *setup_info = &st->setup_infos[i];

		/* Immediately accept a matching setup. */
		if (ad4170_setup_eq(target_setup, &setup_info->setup)) {
			*setup_num = i;
			return 0;
		}

		/* Ignore all setups which are used by enabled channels. */
		if (setup_info->enabled_channels)
			continue;

		/* Find the least used slot. */
		if (*setup_num == AD4170_INVALID_SETUP ||
		    setup_info->channels < st->setup_infos[*setup_num].channels)
			*setup_num = i;
	}

	if (*setup_num == AD4170_INVALID_SETUP)
		return -EINVAL;

	*overwrite = true;
	return 0;
}

static void ad4170_unlink_channel(struct ad4170_state *st, unsigned int channel)
{
	struct ad4170_chan_info *chan_info = &st->chan_infos[channel];
	struct ad4170_setup_info *setup_info = &st->setup_infos[chan_info->setup_num];

	chan_info->setup_num = AD4170_INVALID_SETUP;
	setup_info->channels--;
}

static int ad4170_unlink_setup(struct ad4170_state *st, unsigned int setup_num)
{
	unsigned int i;

	for (i = 0; i < AD4170_MAX_ADC_CHANNELS; i++) {
		struct ad4170_chan_info *chan_info = &st->chan_infos[i];

		if (!chan_info->initialized || chan_info->setup_num != setup_num)
			continue;

		ad4170_unlink_channel(st, i);
	}
	return 0;
}

static int ad4170_link_channel_setup(struct ad4170_state *st,
				     unsigned int chan_addr,
				     unsigned int setup_num)
{
	struct ad4170_setup_info *setup_info = &st->setup_infos[setup_num];
	struct ad4170_chan_info *chan_info = &st->chan_infos[chan_addr];
	int ret;

	ret = regmap_update_bits(st->regmap, AD4170_CHAN_SETUP_REG(chan_addr),
				 AD4170_CHAN_SETUP_SETUP_MSK,
				 FIELD_PREP(AD4170_CHAN_SETUP_SETUP_MSK, setup_num));
	if (ret)
		return ret;

	chan_info->setup_num = setup_num;
	setup_info->channels++;
	return 0;
}

static int ad4170_write_setup(struct ad4170_state *st, unsigned int setup_num,
			      struct ad4170_setup *setup)
{
	int ret;

	/*
	 * It is recommended to place the ADC in standby mode or idle mode to
	 * write to OFFSET and GAIN registers.
	 */
	ret = regmap_update_bits(st->regmap, AD4170_ADC_CTRL_REG,
				 AD4170_ADC_CTRL_MODE_MSK,
				 FIELD_PREP(AD4170_ADC_CTRL_MODE_MSK,
					    AD4170_ADC_CTRL_MODE_IDLE));
	if (ret)
		return ret;

	ret = regmap_write(st->regmap, AD4170_MISC_REG(setup_num), setup->misc);
	if (ret)
		return ret;

	ret = regmap_write(st->regmap, AD4170_AFE_REG(setup_num), setup->afe);
	if (ret)
		return ret;

	ret = regmap_write(st->regmap, AD4170_FILTER_REG(setup_num),
			   setup->filter);
	if (ret)
		return ret;

	ret = regmap_write(st->regmap, AD4170_FILTER_FS_REG(setup_num),
			   setup->filter_fs);
	if (ret)
		return ret;

	ret = regmap_write(st->regmap, AD4170_OFFSET_REG(setup_num),
			   setup->offset);
	if (ret)
		return ret;

	ret = regmap_write(st->regmap, AD4170_GAIN_REG(setup_num), setup->gain);
	if (ret)
		return ret;

	memcpy(&st->setup_infos[setup_num].setup, setup, sizeof(*setup));
	return 0;
}

static int ad4170_write_channel_setup(struct ad4170_state *st,
				      unsigned int chan_addr, bool on_enable)
{
	struct ad4170_chan_info *chan_info = &st->chan_infos[chan_addr];
	bool overwrite;
	int setup_num;
	int ret;

	/*
	 * Similar to AD4130 driver, the following cases need to be handled.
	 *
	 * 1. Enabled and linked channel with setup changes:
	 *    - Find a setup. If not possible, return error.
	 *    - Unlink channel from current setup.
	 *    - If the setup found has only disabled channels linked to it,
	 *      unlink all channels, and write the new setup to it.
	 *    - Link channel to new setup.
	 *
	 * 2. Soon to be enabled and unlinked channel:
	 *    - Find a setup. If not possible, return error.
	 *    - If the setup found has only disabled channels linked to it,
	 *      unlink all channels, and write the new setup to it.
	 *    - Link channel to the setup.
	 *
	 * 3. Disabled and linked channel with setup changes:
	 *    - Unlink channel from current setup.
	 *
	 * 4. Soon to be enabled and linked channel:
	 * 5. Disabled and unlinked channel with setup changes:
	 *    - Do nothing.
	 */

	/* Cases 3, 4, and 5 */
	if (chan_info->setup_num != AD4170_INVALID_SETUP) {
		/* Case 4 */
		if (on_enable)
			return 0;

		/* Case 3 */
		if (!chan_info->enabled) {
			ad4170_unlink_channel(st, chan_addr);
			return 0;
		}
	} else if (!on_enable && !chan_info->enabled) {
		/* Case 5 */
		return 0;
	}

	/* Cases 1 & 2 */
	ret = ad4170_find_setup(st, &chan_info->setup, &setup_num, &overwrite);
	if (ret)
		return ret;

	if (chan_info->setup_num != AD4170_INVALID_SETUP)
		/* Case 1 */
		ad4170_unlink_channel(st, chan_addr);

	if (overwrite) {
		ret = ad4170_unlink_setup(st, setup_num);
		if (ret)
			return ret;

		ret = ad4170_write_setup(st, setup_num, &chan_info->setup);
		if (ret)
			return ret;
	}

	return ad4170_link_channel_setup(st, chan_addr, setup_num);
}

static int ad4170_set_channel_enable(struct ad4170_state *st,
				     unsigned int chan_addr, bool status)
{
	struct ad4170_chan_info *chan_info = &st->chan_infos[chan_addr];
	struct ad4170_setup_info *setup_info;
	int ret;

	if (chan_info->enabled == status)
		return 0;

	if (status) {
		ret = ad4170_write_channel_setup(st, chan_addr, true);
		if (ret)
			return ret;
	}

	setup_info = &st->setup_infos[chan_info->setup_num];

	ret = regmap_update_bits(st->regmap, AD4170_CHAN_EN_REG,
				 AD4170_CHAN_EN(chan_addr),
				 status ? AD4170_CHAN_EN(chan_addr) : 0);
	if (ret)
		return ret;

	setup_info->enabled_channels += status ? 1 : -1;
	chan_info->enabled = status;
	return 0;
}

static int __ad4170_get_filter_type(unsigned int filter)
{
	u16 f_conf = FIELD_GET(AD4170_FILTER_FILTER_TYPE_MSK, filter);

	switch (f_conf) {
	case AD4170_FILTER_FILTER_TYPE_SINC5_AVG:
		return AD4170_SINC5_AVG;
	case AD4170_FILTER_FILTER_TYPE_SINC5:
		return AD4170_SINC5;
	case AD4170_FILTER_FILTER_TYPE_SINC3:
		return AD4170_SINC3;
	default:
		return -EINVAL;
	}
}

static int ad4170_set_filter_type(struct iio_dev *indio_dev,
				  struct iio_chan_spec const *chan,
				  unsigned int val)
{
	struct ad4170_state *st = iio_priv(indio_dev);
	struct ad4170_chan_info *chan_info = &st->chan_infos[chan->address];
	struct ad4170_setup *setup = &chan_info->setup;
	unsigned int filter_type_conf;
	int ret;

	switch (val) {
	case AD4170_SINC5_AVG:
		filter_type_conf = AD4170_FILTER_FILTER_TYPE_SINC5_AVG;
		break;
	case AD4170_SINC5:
		filter_type_conf = AD4170_FILTER_FILTER_TYPE_SINC5;
		break;
	case AD4170_SINC3:
		filter_type_conf = AD4170_FILTER_FILTER_TYPE_SINC3;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * The filters provide the same ODR for a given filter_fs value but
	 * there are different minimum and maximum filter_fs limits for each
	 * filter. The filter_fs value will be adjusted if the current filter_fs
	 * is out of the limits of the just requested filter. Since the
	 * filter_fs value affects the ODR (sampling_frequency), changing the
	 * filter may lead to a change in the sampling frequency.
	 */
	scoped_guard(mutex, &st->lock) {
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		if (val == AD4170_SINC5_AVG || val == AD4170_SINC3)
			setup->filter_fs = clamp(val, AD4170_SINC3_MIN_FS,
						 AD4170_SINC3_MAX_FS);
		else
			setup->filter_fs = clamp(val, AD4170_SINC5_MIN_FS,
						 AD4170_SINC5_MAX_FS);

		setup->filter &= ~AD4170_FILTER_FILTER_TYPE_MSK;
		setup->filter |= FIELD_PREP(AD4170_FILTER_FILTER_TYPE_MSK,
					    filter_type_conf);

		ret = ad4170_write_channel_setup(st, chan->address, false);
		iio_device_release_direct(indio_dev);
	}

	return ret;
}

static int ad4170_get_filter_type(struct iio_dev *indio_dev,
				  struct iio_chan_spec const *chan)
{
	struct ad4170_state *st = iio_priv(indio_dev);
	struct ad4170_chan_info *chan_info = &st->chan_infos[chan->address];
	struct ad4170_setup *setup = &chan_info->setup;

	return __ad4170_get_filter_type(setup->filter);
}

static const struct iio_enum ad4170_filter_type_enum = {
	.items = ad4170_filt_names,
	.num_items = ARRAY_SIZE(ad4170_filt_names),
	.get = ad4170_get_filter_type,
	.set = ad4170_set_filter_type,
};

static const struct iio_chan_spec_ext_info ad4170_filter_type_ext_info[] = {
	IIO_ENUM("filter_type", IIO_SEPARATE, &ad4170_filter_type_enum),
	IIO_ENUM_AVAILABLE("filter_type", IIO_SHARED_BY_TYPE,
			   &ad4170_filter_type_enum),
	{ }
};

static const struct iio_chan_spec ad4170_channel_template = {
	.type = IIO_VOLTAGE,
	.indexed = 1,
	.differential = 1,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			      BIT(IIO_CHAN_INFO_SCALE) |
			      BIT(IIO_CHAN_INFO_CALIBBIAS) |
			      BIT(IIO_CHAN_INFO_CALIBSCALE) |
			      BIT(IIO_CHAN_INFO_SAMP_FREQ) |
			      BIT(IIO_CHAN_INFO_OFFSET),
	.info_mask_separate_available = BIT(IIO_CHAN_INFO_SCALE) |
					BIT(IIO_CHAN_INFO_SAMP_FREQ),
	.ext_info = ad4170_filter_type_ext_info,
	.scan_type = {
		.realbits = 24,
		.storagebits = 32,
		.shift = 8,
		.endianness = IIO_BE,
	},
};

static const struct iio_chan_spec ad4170_temp_channel_template = {
	.type = IIO_TEMP,
	.indexed = 0,
	.channel = 17,
	.channel2 = 17,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			      BIT(IIO_CHAN_INFO_SCALE) |
			      BIT(IIO_CHAN_INFO_OFFSET) |
			      BIT(IIO_CHAN_INFO_CALIBSCALE) |
			      BIT(IIO_CHAN_INFO_CALIBBIAS) |
			      BIT(IIO_CHAN_INFO_SAMP_FREQ),
	.info_mask_separate_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	.scan_type = {
		.sign = 's',
		.realbits = 24,
		.storagebits = 32,
		.shift = 8,
		.endianness = IIO_BE,
	},
};

/*
 * Receives the number of a multiplexed AD4170 input (ain_n), and stores the
 * voltage (in µV) of the specified input into ain_voltage. If the input number
 * is a ordinary analog input (AIN0 to AIN8), stores zero into ain_voltage.
 * If a voltage regulator required by a special input is unavailable, return
 * error code. Return 0 on success.
 */
static int ad4170_get_ain_voltage_uv(struct ad4170_state *st, int ain_n,
				     int *ain_voltage)
{
	struct device *dev = &st->spi->dev;
	int v_diff;

	*ain_voltage = 0;
	/*
	 * The voltage bias (vbias) sets the common-mode voltage of the channel
	 * to (AVDD + AVSS)/2. If provided, AVSS supply provides the magnitude
	 * (absolute value) of the negative voltage supplied to the AVSS pin.
	 * So, we do AVDD - AVSS to compute the DC voltage generated by the bias
	 * voltage generator.
	 */
	if (st->pins_fn[ain_n] & AD4170_PIN_VBIAS) {
		int v_diff = st->vrefs_uv[AD4170_AVDD_SUP] - st->vrefs_uv[AD4170_AVSS_SUP];
		*ain_voltage = v_diff / 2;
		return 0;
	}

	if (ain_n <= AD4170_CHAN_MAP_TEMP_SENSOR)
		return 0;

	switch (ain_n) {
	case AD4170_CHAN_MAP_AVDD_AVSS_N:
		v_diff = st->vrefs_uv[AD4170_AVDD_SUP] - st->vrefs_uv[AD4170_AVSS_SUP];
		*ain_voltage = v_diff / 5;
		return 0;
	case AD4170_CHAN_MAP_IOVDD_DGND_N:
		*ain_voltage = st->vrefs_uv[AD4170_IOVDD_SUP] / 5;
		return 0;
	case AD4170_CHAN_MAP_AVSS:
		*ain_voltage = st->vrefs_uv[AD4170_AVSS_SUP];
		return 0;
	case AD4170_CHAN_MAP_DGND:
		*ain_voltage = 0;
		return 0;
	case AD4170_CHAN_MAP_REFIN1_P:
		if (st->vrefs_uv[AD4170_REFIN1P_SUP] == -ENODEV)
			return dev_err_probe(dev, -ENODEV,
					     "input set to REFIN+ but ref not provided\n");

		*ain_voltage = st->vrefs_uv[AD4170_REFIN1P_SUP];
		return 0;
	case AD4170_CHAN_MAP_REFIN1_N:
		if (st->vrefs_uv[AD4170_REFIN1N_SUP] == -ENODEV)
			return dev_err_probe(dev, -ENODEV,
					     "input set to REFIN- but ref not provided\n");

		*ain_voltage = st->vrefs_uv[AD4170_REFIN1N_SUP];
		return 0;
	case AD4170_CHAN_MAP_REFIN2_P:
		if (st->vrefs_uv[AD4170_REFIN2P_SUP] == -ENODEV)
			return dev_err_probe(dev, -ENODEV,
					     "input set to REFIN2+ but ref not provided\n");

		*ain_voltage = st->vrefs_uv[AD4170_REFIN2P_SUP];
		return 0;
	case AD4170_CHAN_MAP_REFIN2_N:
		if (st->vrefs_uv[AD4170_REFIN2N_SUP] == -ENODEV)
			return dev_err_probe(dev, -ENODEV,
					     "input set to REFIN2- but ref not provided\n");

		*ain_voltage = st->vrefs_uv[AD4170_REFIN2N_SUP];
		return 0;
	case AD4170_CHAN_MAP_REFOUT:
		/* REFOUT is 2.5V relative to AVSS so take that into account */
		*ain_voltage = st->vrefs_uv[AD4170_AVSS_SUP] + AD4170_INT_REF_2_5V;
		return 0;
	default:
		return -EINVAL;
	}
}

static int ad4170_validate_analog_input(struct ad4170_state *st, int pin)
{
	if (pin <= AD4170_MAX_ANALOG_PINS) {
		if (st->pins_fn[pin] & AD4170_PIN_CURRENT_OUT)
			return dev_err_probe(&st->spi->dev, -EINVAL,
					     "Pin %d already used with fn %u.\n",
					     pin, st->pins_fn[pin]);

		st->pins_fn[pin] |= AD4170_PIN_ANALOG_IN;
	}
	return 0;
}

static int ad4170_validate_channel_input(struct ad4170_state *st, int pin, bool com)
{
	/* Check common-mode input pin is mapped to a special input. */
	if (com && (pin < AD4170_CHAN_MAP_AVDD_AVSS_P || pin > AD4170_CHAN_MAP_REFOUT))
		return dev_err_probe(&st->spi->dev, -EINVAL,
				     "Invalid common-mode input pin number. %d\n",
				     pin);

	/* Check differential input pin is mapped to a analog input pin. */
	if (!com && pin > AD4170_MAX_ANALOG_PINS)
		return dev_err_probe(&st->spi->dev, -EINVAL,
				     "Invalid analog input pin number. %d\n",
				     pin);

	return ad4170_validate_analog_input(st, pin);
}

/*
 * Verifies whether the channel input configuration is valid by checking the
 * input numbers.
 * Returns 0 on valid channel input configuration. -EINVAL otherwise.
 */
static int ad4170_validate_channel(struct ad4170_state *st,
				   struct iio_chan_spec const *chan)
{
	int ret;

	ret = ad4170_validate_channel_input(st, chan->channel, false);
	if (ret)
		return ret;

	return ad4170_validate_channel_input(st, chan->channel2,
					     !chan->differential);
}

/*
 * Verifies whether the channel configuration is valid by checking the provided
 * input type, polarity, and voltage references result in a sane input range.
 * Returns negative error code on failure.
 */
static int ad4170_get_input_range(struct ad4170_state *st,
				  struct iio_chan_spec const *chan,
				  unsigned int ch_reg, unsigned int ref_sel)
{
	bool bipolar = chan->scan_type.sign == 's';
	struct device *dev = &st->spi->dev;
	int refp, refn, ain_voltage, ret;

	switch (ref_sel) {
	case AD4170_REF_REFIN1:
		if (st->vrefs_uv[AD4170_REFIN1P_SUP] == -ENODEV ||
		    st->vrefs_uv[AD4170_REFIN1N_SUP] == -ENODEV)
			return dev_err_probe(dev, -ENODEV,
					     "REFIN± selected but not provided\n");

		refp = st->vrefs_uv[AD4170_REFIN1P_SUP];
		refn = st->vrefs_uv[AD4170_REFIN1N_SUP];
		break;
	case AD4170_REF_REFIN2:
		if (st->vrefs_uv[AD4170_REFIN2P_SUP] == -ENODEV ||
		    st->vrefs_uv[AD4170_REFIN2N_SUP] == -ENODEV)
			return dev_err_probe(dev, -ENODEV,
					     "REFIN2± selected but not provided\n");

		refp = st->vrefs_uv[AD4170_REFIN2P_SUP];
		refn = st->vrefs_uv[AD4170_REFIN2N_SUP];
		break;
	case AD4170_REF_AVDD:
		refp = st->vrefs_uv[AD4170_AVDD_SUP];
		refn = st->vrefs_uv[AD4170_AVSS_SUP];
		break;
	case AD4170_REF_REFOUT:
		/* REFOUT is 2.5 V relative to AVSS */
		refp = st->vrefs_uv[AD4170_AVSS_SUP] + AD4170_INT_REF_2_5V;
		refn = st->vrefs_uv[AD4170_AVSS_SUP];
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Find out the analog input range from the channel type, polarity, and
	 * voltage reference selection.
	 * AD4170 channels are either differential or pseudo-differential.
	 * Diff input voltage range: −VREF/gain to +VREF/gain (datasheet page 6)
	 * Pseudo-diff input voltage range: 0 to VREF/gain (datasheet page 6)
	 */
	if (chan->differential) {
		if (!bipolar)
			return dev_err_probe(dev, -EINVAL,
					     "Channel %u differential unipolar\n",
					     ch_reg);

		/*
		 * Differential bipolar channel.
		 * avss-supply is never above 0V.
		 * Assuming refin1n-supply not above 0V.
		 * Assuming refin2n-supply not above 0V.
		 */
		return refp + abs(refn);
	}
	/*
	 * Some configurations can lead to invalid setups.
	 * For example, if AVSS = -2.5V, REF_SELECT set to REFOUT (REFOUT/AVSS),
	 * and pseudo-diff channel configuration set, then the input range
	 * should go from 0V to +VREF (single-ended - datasheet pg 10), but
	 * REFOUT/AVSS range would be -2.5V to 0V.
	 * Check the positive reference is higher than 0V for pseudo-diff
	 * channels.
	 * Note that at this point in the code, refp can only be >= 0 since all
	 * error codes from reading the regulator voltage have been checked
	 * either at ad4170_regulator_setup() or above in this function.
	 */
	if (refp == 0)
		return dev_err_probe(dev, -EINVAL,
				     "REF+ == GND for pseudo-diff chan %u\n",
				     ch_reg);

	if (bipolar)
		return refp;

	/*
	 * Pseudo-differential unipolar channel.
	 * Input expected to swing from IN- to +VREF.
	 */
	ret = ad4170_get_ain_voltage_uv(st, chan->channel2, &ain_voltage);
	if (ret)
		return ret;

	if (refp - ain_voltage <= 0)
		return dev_err_probe(dev, -EINVAL,
				     "Negative input >= REF+ for pseudo-diff chan %u\n",
				     ch_reg);

	return refp - ain_voltage;
}

static int __ad4170_read_sample(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan, int *val)
{
	struct ad4170_state *st = iio_priv(indio_dev);
	unsigned long settling_time_ms;
	int ret;

	reinit_completion(&st->completion);
	ret = regmap_update_bits(st->regmap, AD4170_ADC_CTRL_REG,
				 AD4170_ADC_CTRL_MODE_MSK,
				 FIELD_PREP(AD4170_ADC_CTRL_MODE_MSK,
					    AD4170_ADC_CTRL_MODE_SINGLE));
	if (ret)
		return ret;

	/*
	 * When a channel is manually selected by the user, the ADC needs an
	 * extra time to provide the first stable conversion. The ADC settling
	 * time depends on the filter type, filter frequency, and ADC clock
	 * frequency (see datasheet page 53). The maximum settling time among
	 * all filter configurations is 6291164 / fCLK. Use that formula to wait
	 * for sufficient time whatever the filter configuration may be.
	 */
	settling_time_ms = DIV_ROUND_UP(6291164 * MILLI, st->mclk_hz);
	ret = wait_for_completion_timeout(&st->completion,
					  msecs_to_jiffies(settling_time_ms));
	if (!ret)
		dev_dbg(&st->spi->dev,
			"No Data Ready signal. Reading after delay.\n");

	ret = regmap_read(st->regmap, AD4170_DATA_24B_REG, val);
	if (ret)
		return ret;

	if (chan->scan_type.sign == 's')
		*val = sign_extend32(*val, chan->scan_type.realbits - 1);

	return 0;
}

static int ad4170_read_sample(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan, int *val)
{
	struct ad4170_state *st = iio_priv(indio_dev);
	struct device *dev = &st->spi->dev;
	int ret, ret2;

	/*
	 * The ADC sequences through all enabled channels. That can lead to
	 * incorrect channel being sampled if a previous read would have left a
	 * different channel enabled. Thus, always enable and disable the
	 * channel on single-shot read.
	 */
	ret = ad4170_set_channel_enable(st, chan->address, true);
	if (ret)
		return ret;

	ret = __ad4170_read_sample(indio_dev, chan, val);
	if (ret) {
		dev_err(dev, "failed to read sample: %d\n", ret);

		ret2 = ad4170_set_channel_enable(st, chan->address, false);
		if (ret2)
			dev_err(dev, "failed to disable channel: %d\n", ret2);

		return ret;
	}

	ret = ad4170_set_channel_enable(st, chan->address, false);
	if (ret)
		return ret;

	return IIO_VAL_INT;
}

static int ad4170_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long info)
{
	struct ad4170_state *st = iio_priv(indio_dev);
	struct ad4170_chan_info *chan_info = &st->chan_infos[chan->address];
	struct ad4170_setup *setup = &chan_info->setup;
	enum ad4170_filter_type f_type;
	unsigned int pga, fs_idx;
	int ret;

	guard(mutex)(&st->lock);
	switch (info) {
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		ret = ad4170_read_sample(indio_dev, chan, val);
		iio_device_release_direct(indio_dev);
		return ret;
	case IIO_CHAN_INFO_SCALE:
		pga = FIELD_GET(AD4170_AFE_PGA_GAIN_MSK, setup->afe);
		switch (chan->type) {
		case IIO_VOLTAGE:
			*val = chan_info->scale_tbl[pga][0];
			*val2 = chan_info->scale_tbl[pga][1];
			return IIO_VAL_INT_PLUS_NANO;
		case IIO_TEMP:
			/*
			 * The scale_tbl converts output codes to mV units so
			 * multiply by MILLI to make the factor convert to µV.
			 * Then, apply the temperature sensor change sensitivity
			 * of 477 μV/K. Finally, multiply the result by MILLI
			 * again to comply with milli degrees Celsius IIO ABI.
			 */
			*val = 0;
			*val2 = DIV_ROUND_CLOSEST(chan_info->scale_tbl[pga][1] * MILLI, 477) *
						  MILLI;
			return IIO_VAL_INT_PLUS_NANO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		pga = FIELD_GET(AD4170_AFE_PGA_GAIN_MSK, setup->afe);
		*val = chan_info->offset_tbl[pga];
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SAMP_FREQ:
		f_type = __ad4170_get_filter_type(setup->filter);
		switch (f_type) {
		case AD4170_SINC5_AVG:
		case AD4170_SINC3:
			fs_idx = find_closest(setup->filter_fs,
					      ad4170_sinc3_filt_fs_tbl,
					      ARRAY_SIZE(ad4170_sinc3_filt_fs_tbl));
			*val = st->sps_tbl[f_type][fs_idx][0];
			*val2 = st->sps_tbl[f_type][fs_idx][1];
			return IIO_VAL_INT_PLUS_MICRO;
		case AD4170_SINC5:
			fs_idx = find_closest(setup->filter_fs,
					      ad4170_sinc5_filt_fs_tbl,
					      ARRAY_SIZE(ad4170_sinc5_filt_fs_tbl));
			*val = st->sps_tbl[f_type][fs_idx][0];
			*val2 = st->sps_tbl[f_type][fs_idx][1];
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_CALIBBIAS:
		*val = setup->offset;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBSCALE:
		*val = setup->gain;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ad4170_fill_scale_tbl(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan)
{
	struct ad4170_state *st = iio_priv(indio_dev);
	struct ad4170_chan_info *chan_info = &st->chan_infos[chan->address];
	struct device *dev = &st->spi->dev;
	int bipolar = chan->scan_type.sign == 's' ? 1 : 0;
	int precision_bits = chan->scan_type.realbits;
	int pga, ainm_voltage, ret;
	unsigned long long offset;

	ainm_voltage = 0;
	ret = ad4170_get_ain_voltage_uv(st, chan->channel2, &ainm_voltage);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to fill scale table\n");

	for (pga = 0; pga < AD4170_NUM_PGA_OPTIONS; pga++) {
		u64 nv;
		unsigned int lshift, rshift;

		/*
		 * The PGA options are numbered from 0 to 9, with option 0 being
		 * a gain of 2^0 (no actual gain), and 7 meaning a gain of 2^7.
		 * Option 8, though, sets a gain of 0.5, so the input signal can
		 * be attenuated by 2 rather than amplified. Option 9, allows
		 * the signal to bypass the PGA circuitry (no gain).
		 *
		 * The scale factor to get ADC output codes to values in mV
		 * units is given by:
		 * _scale = (input_range / gain) / 2^precision
		 * AD4170 gain is a power of 2 so the above can be written as
		 * _scale = input_range / 2^(precision + gain)
		 * Keep the input range in µV to avoid truncating the less
		 * significant bits when right shifting it so to preserve scale
		 * precision.
		 */
		nv = (u64)chan_info->input_range_uv * NANO;
		lshift = !!(pga & BIT(3)); /* handle PGA options 8 and 9 */
		rshift = precision_bits - bipolar + (pga & GENMASK(2, 0)) - lshift;
		chan_info->scale_tbl[pga][0] = 0;
		chan_info->scale_tbl[pga][1] = div_u64(nv >> rshift, MILLI);

		/*
		 * If the negative input is not at GND, the conversion result
		 * (which is relative to IN-) will be offset by the level at IN-.
		 * Use the scale factor the other way around to go from a known
		 * voltage to the corresponding ADC output code.
		 * With that, we are able to get to what would be the output
		 * code for the voltage at the negative input.
		 * If the negative input is not fixed, there is no offset.
		 */
		offset = ((unsigned long long)abs(ainm_voltage)) * MICRO;
		offset = DIV_ROUND_CLOSEST_ULL(offset, chan_info->scale_tbl[pga][1]);

		/*
		 * After divided by the scale, offset will always fit into 31
		 * bits. For _raw + _offset to be relative to GND, the value
		 * provided as _offset is of opposite sign than the real offset.
		 */
		if (ainm_voltage > 0)
			chan_info->offset_tbl[pga] = -(int)(offset);
		else
			chan_info->offset_tbl[pga] = (int)(offset);
	}
	return 0;
}

static int ad4170_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long info)
{
	struct ad4170_state *st = iio_priv(indio_dev);
	struct ad4170_chan_info *chan_info = &st->chan_infos[chan->address];
	enum ad4170_filter_type f_type;

	switch (info) {
	case IIO_CHAN_INFO_SCALE:
		*vals = (int *)chan_info->scale_tbl;
		*length = ARRAY_SIZE(chan_info->scale_tbl) * 2;
		*type = IIO_VAL_INT_PLUS_NANO;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*type = IIO_VAL_INT_PLUS_MICRO;
		f_type = ad4170_get_filter_type(indio_dev, chan);
		switch (f_type) {
		case AD4170_SINC5_AVG:
		case AD4170_SINC3:
			/* Read sps_tbl here to ensure in bounds array access */
			*vals = (int *)st->sps_tbl[f_type];
			*length = ARRAY_SIZE(ad4170_sinc3_filt_fs_tbl) * 2;
			return IIO_AVAIL_LIST;
		case AD4170_SINC5:
			/* Read sps_tbl here to ensure in bounds array access */
			*vals = (int *)st->sps_tbl[f_type];
			*length = ARRAY_SIZE(ad4170_sinc5_filt_fs_tbl) * 2;
			return IIO_AVAIL_LIST;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int ad4170_set_pga(struct ad4170_state *st,
			  struct iio_chan_spec const *chan, int val, int val2)
{
	struct ad4170_chan_info *chan_info = &st->chan_infos[chan->address];
	struct ad4170_setup *setup = &chan_info->setup;
	unsigned int pga;

	for (pga = 0; pga < AD4170_NUM_PGA_OPTIONS; pga++) {
		if (val == chan_info->scale_tbl[pga][0] &&
		    val2 == chan_info->scale_tbl[pga][1])
			break;
	}

	if (pga == AD4170_NUM_PGA_OPTIONS)
		return -EINVAL;

	guard(mutex)(&st->lock);
	setup->afe &= ~AD4170_AFE_PGA_GAIN_MSK;
	setup->afe |= FIELD_PREP(AD4170_AFE_PGA_GAIN_MSK, pga);

	return ad4170_write_channel_setup(st, chan->address, false);
}

static int ad4170_set_channel_freq(struct ad4170_state *st,
				   struct iio_chan_spec const *chan, int val,
				   int val2)
{
	struct ad4170_chan_info *chan_info = &st->chan_infos[chan->address];
	struct ad4170_setup *setup = &chan_info->setup;
	enum ad4170_filter_type f_type = __ad4170_get_filter_type(setup->filter);
	unsigned int filt_fs_tbl_size, i;

	switch (f_type) {
	case AD4170_SINC5_AVG:
	case AD4170_SINC3:
		filt_fs_tbl_size = ARRAY_SIZE(ad4170_sinc3_filt_fs_tbl);
		break;
	case AD4170_SINC5:
		filt_fs_tbl_size = ARRAY_SIZE(ad4170_sinc5_filt_fs_tbl);
		break;
	}

	for (i = 0; i < filt_fs_tbl_size; i++) {
		if (st->sps_tbl[f_type][i][0] == val &&
		    st->sps_tbl[f_type][i][1] == val2)
			break;
	}
	if (i == filt_fs_tbl_size)
		return -EINVAL;

	guard(mutex)(&st->lock);
	if (f_type == AD4170_SINC5)
		setup->filter_fs = ad4170_sinc5_filt_fs_tbl[i];
	else
		setup->filter_fs = ad4170_sinc3_filt_fs_tbl[i];

	return ad4170_write_channel_setup(st, chan->address, false);
}

static int ad4170_set_calib_offset(struct ad4170_state *st,
				   struct iio_chan_spec const *chan, int val)
{
	struct ad4170_chan_info *chan_info = &st->chan_infos[chan->address];
	struct ad4170_setup *setup = &chan_info->setup;

	guard(mutex)(&st->lock);
	setup->offset = val;

	return ad4170_write_channel_setup(st, chan->address, false);
}

static int ad4170_set_calib_gain(struct ad4170_state *st,
				 struct iio_chan_spec const *chan, int val)
{
	struct ad4170_chan_info *chan_info = &st->chan_infos[chan->address];
	struct ad4170_setup *setup = &chan_info->setup;

	guard(mutex)(&st->lock);
	setup->gain = val;

	return ad4170_write_channel_setup(st, chan->address, false);
}

static int __ad4170_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan, int val,
			      int val2, long info)
{
	struct ad4170_state *st = iio_priv(indio_dev);

	switch (info) {
	case IIO_CHAN_INFO_SCALE:
		return ad4170_set_pga(st, chan, val, val2);
	case IIO_CHAN_INFO_SAMP_FREQ:
		return ad4170_set_channel_freq(st, chan, val, val2);
	case IIO_CHAN_INFO_CALIBBIAS:
		return ad4170_set_calib_offset(st, chan, val);
	case IIO_CHAN_INFO_CALIBSCALE:
		return ad4170_set_calib_gain(st, chan, val);
	default:
		return -EINVAL;
	}
}

static int ad4170_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int val,
			    int val2, long info)
{
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = __ad4170_write_raw(indio_dev, chan, val, val2, info);
	iio_device_release_direct(indio_dev);
	return ret;
}

static int ad4170_write_raw_get_fmt(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan,
				    long info)
{
	switch (info) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_CALIBBIAS:
	case IIO_CHAN_INFO_CALIBSCALE:
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ad4170_update_scan_mode(struct iio_dev *indio_dev,
				   const unsigned long *active_scan_mask)
{
	struct ad4170_state *st = iio_priv(indio_dev);
	unsigned int chan_index;
	int ret;

	iio_for_each_active_channel(indio_dev, chan_index) {
		ret = ad4170_set_channel_enable(st, chan_index, true);
		if (ret)
			return ret;
	}
	return 0;
}

static const struct iio_info ad4170_info = {
	.read_raw = ad4170_read_raw,
	.read_avail = ad4170_read_avail,
	.write_raw = ad4170_write_raw,
	.write_raw_get_fmt = ad4170_write_raw_get_fmt,
	.update_scan_mode = ad4170_update_scan_mode,
	.debugfs_reg_access = ad4170_debugfs_reg_access,
};

static int ad4170_soft_reset(struct ad4170_state *st)
{
	int ret;

	ret = regmap_write(st->regmap, AD4170_CONFIG_A_REG,
			   AD4170_SW_RESET_MSK);
	if (ret)
		return ret;

	/* AD4170-4 requires 1 ms between reset and any register access. */
	fsleep(1 * USEC_PER_MSEC);

	return 0;
}

static int ad4170_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct iio_dev *indio_dev = gpiochip_get_data(gc);
	struct ad4170_state *st = iio_priv(indio_dev);
	unsigned int val;
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = regmap_read(st->regmap, AD4170_GPIO_MODE_REG, &val);
	if (ret)
		goto err_release;

	/*
	 * If the GPIO is configured as an input, read the current value from
	 * AD4170_GPIO_INPUT_REG. Otherwise, read the input value from
	 * AD4170_GPIO_OUTPUT_REG.
	 */
	if (val & BIT(offset * 2))
		ret = regmap_read(st->regmap, AD4170_GPIO_INPUT_REG, &val);
	else
		ret = regmap_read(st->regmap, AD4170_GPIO_OUTPUT_REG, &val);
	if (ret)
		goto err_release;

	ret = !!(val & BIT(offset));
err_release:
	iio_device_release_direct(indio_dev);

	return ret;
}

static int ad4170_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct iio_dev *indio_dev = gpiochip_get_data(gc);
	struct ad4170_state *st = iio_priv(indio_dev);
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = regmap_assign_bits(st->regmap, AD4170_GPIO_OUTPUT_REG,
				 BIT(offset), !!value);

	iio_device_release_direct(indio_dev);
	return ret;
}

static int ad4170_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct iio_dev *indio_dev = gpiochip_get_data(gc);
	struct ad4170_state *st = iio_priv(indio_dev);
	unsigned int val;
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = regmap_read(st->regmap, AD4170_GPIO_MODE_REG, &val);
	if (ret)
		goto err_release;

	if (val & BIT(offset * 2 + 1))
		ret = GPIO_LINE_DIRECTION_OUT;
	else
		ret = GPIO_LINE_DIRECTION_IN;

err_release:
	iio_device_release_direct(indio_dev);

	return ret;
}

static int ad4170_gpio_direction_input(struct gpio_chip *gc, unsigned int offset)
{
	struct iio_dev *indio_dev = gpiochip_get_data(gc);
	struct ad4170_state *st = iio_priv(indio_dev);
	unsigned long gpio_mask;
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	switch (offset) {
	case 0:
		gpio_mask = AD4170_GPIO_MODE_GPIO0_MSK;
		break;
	case 1:
		gpio_mask = AD4170_GPIO_MODE_GPIO1_MSK;
		break;
	case 2:
		gpio_mask = AD4170_GPIO_MODE_GPIO2_MSK;
		break;
	case 3:
		gpio_mask = AD4170_GPIO_MODE_GPIO3_MSK;
		break;
	default:
		ret = -EINVAL;
		goto err_release;
	}
	ret = regmap_update_bits(st->regmap, AD4170_GPIO_MODE_REG, gpio_mask,
				 AD4170_GPIO_MODE_GPIO_INPUT << (2 * offset));

err_release:
	iio_device_release_direct(indio_dev);

	return ret;
}

static int ad4170_gpio_direction_output(struct gpio_chip *gc,
					unsigned int offset, int value)
{
	struct iio_dev *indio_dev = gpiochip_get_data(gc);
	struct ad4170_state *st = iio_priv(indio_dev);
	unsigned long gpio_mask;
	int ret;

	ret = ad4170_gpio_set(gc, offset, value);
	if (ret)
		return ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	switch (offset) {
	case 0:
		gpio_mask = AD4170_GPIO_MODE_GPIO0_MSK;
		break;
	case 1:
		gpio_mask = AD4170_GPIO_MODE_GPIO1_MSK;
		break;
	case 2:
		gpio_mask = AD4170_GPIO_MODE_GPIO2_MSK;
		break;
	case 3:
		gpio_mask = AD4170_GPIO_MODE_GPIO3_MSK;
		break;
	default:
		ret = -EINVAL;
		goto err_release;
	}
	ret = regmap_update_bits(st->regmap, AD4170_GPIO_MODE_REG, gpio_mask,
				 AD4170_GPIO_MODE_GPIO_OUTPUT << (2 * offset));

err_release:
	iio_device_release_direct(indio_dev);

	return ret;
}

static int ad4170_gpio_init_valid_mask(struct gpio_chip *gc,
				       unsigned long *valid_mask,
				       unsigned int ngpios)
{
	struct ad4170_state *st = gpiochip_get_data(gc);
	unsigned int i;

	/* Only expose GPIOs that were not assigned any other function. */
	for (i = 0; i < ngpios; i++) {
		bool valid = st->gpio_fn[i] == AD4170_GPIO_UNASSIGNED;

		__assign_bit(i, valid_mask, valid);
	}

	return 0;
}

static int ad4170_gpio_init(struct iio_dev *indio_dev)
{
	struct ad4170_state *st = iio_priv(indio_dev);

	st->gpiochip.label = "ad4170_gpios";
	st->gpiochip.base = -1;
	st->gpiochip.ngpio = AD4170_NUM_GPIO_PINS;
	st->gpiochip.parent = &st->spi->dev;
	st->gpiochip.can_sleep = true;
	st->gpiochip.init_valid_mask = ad4170_gpio_init_valid_mask;
	st->gpiochip.get_direction = ad4170_gpio_get_direction;
	st->gpiochip.direction_input = ad4170_gpio_direction_input;
	st->gpiochip.direction_output = ad4170_gpio_direction_output;
	st->gpiochip.get = ad4170_gpio_get;
	st->gpiochip.set = ad4170_gpio_set;
	st->gpiochip.owner = THIS_MODULE;

	return devm_gpiochip_add_data(&st->spi->dev, &st->gpiochip, indio_dev);
}

static int ad4170_validate_excitation_pin(struct ad4170_state *st, u32 pin)
{
	struct device *dev = &st->spi->dev;
	unsigned int i;

	/* Check the pin number is valid */
	for (i = 0; i < ARRAY_SIZE(ad4170_iout_pin_tbl); i++)
		if (ad4170_iout_pin_tbl[i] == pin)
			break;

	if (i == ARRAY_SIZE(ad4170_iout_pin_tbl))
		return dev_err_probe(dev, -EINVAL,
				     "Invalid excitation pin: %u\n",
				     pin);

	/* Check the pin is available */
	if (pin <= AD4170_MAX_ANALOG_PINS) {
		if (st->pins_fn[pin] != AD4170_PIN_UNASSIGNED)
			return dev_err_probe(dev, -EINVAL,
					     "Pin %u already used with fn %u\n",
					     pin, st->pins_fn[pin]);

		st->pins_fn[pin] |= AD4170_PIN_CURRENT_OUT;
	} else {
		unsigned int gpio = pin - AD4170_CURRENT_SRC_I_OUT_PIN_GPIO(0);

		if (st->gpio_fn[gpio] != AD4170_GPIO_UNASSIGNED)
			return dev_err_probe(dev, -EINVAL,
					     "GPIO %u already used with fn %u\n",
					     gpio, st->gpio_fn[gpio]);

		st->gpio_fn[gpio] |= AD4170_GPIO_AC_EXCITATION;
	}

	return 0;
}

static int ad4170_validate_excitation_pins(struct ad4170_state *st,
					   u32 *exc_pins, int num_exc_pins)
{
	unsigned int i;
	int ret;

	for (i = 0; i < num_exc_pins; i++) {
		ret = ad4170_validate_excitation_pin(st, exc_pins[i]);
		if (ret)
			return ret;
	}
	return 0;
}

static const char *const ad4170_i_out_pin_dt_props[] = {
	"adi,excitation-pin-0",
	"adi,excitation-pin-1",
	"adi,excitation-pin-2",
	"adi,excitation-pin-3",
};

static const char *const ad4170_i_out_val_dt_props[] = {
	"adi,excitation-current-0-microamp",
	"adi,excitation-current-1-microamp",
	"adi,excitation-current-2-microamp",
	"adi,excitation-current-3-microamp",
};

/*
 * Parses firmware data describing output current source setup. There are 4
 * excitation currents (IOUT0 to IOUT3) that can be configured independently.
 * Excitation currents are added if they are output on the same pin.
 */
static int ad4170_parse_exc_current(struct ad4170_state *st,
				    struct fwnode_handle *child,
				    unsigned int *exc_pins,
				    unsigned int *exc_curs,
				    unsigned int *num_exc_pins)
{
	struct device *dev = &st->spi->dev;
	unsigned int num_pins, i, j;
	u32 pin, val;
	int ret;

	num_pins = 0;
	for (i = 0; i < AD4170_NUM_CURRENT_SRC; i++) {
		/* Parse excitation current output pin properties. */
		pin = AD4170_CURRENT_SRC_I_OUT_PIN_AIN(0);
		ret = fwnode_property_read_u32(child, ad4170_i_out_pin_dt_props[i],
					       &pin);
		if (ret)
			continue;

		exc_pins[num_pins] = pin;

		/* Parse excitation current value properties. */
		val = ad4170_iout_current_ua_tbl[0];
		fwnode_property_read_u32(child,
					 ad4170_i_out_val_dt_props[i], &val);

		for (j = 0; j < ARRAY_SIZE(ad4170_iout_current_ua_tbl); j++)
			if (ad4170_iout_current_ua_tbl[j] == val)
				break;

		if (j == ARRAY_SIZE(ad4170_iout_current_ua_tbl))
			return dev_err_probe(dev, -EINVAL, "Invalid %s: %uuA\n",
					     ad4170_i_out_val_dt_props[i], val);

		exc_curs[num_pins] = j;
		num_pins++;
	}
	*num_exc_pins = num_pins;

	return 0;
}

static int ad4170_setup_current_src(struct ad4170_state *st,
				    struct fwnode_handle *child,
				    struct ad4170_setup *setup, u32 *exc_pins,
				    unsigned int *exc_curs, int num_exc_pins,
				    bool ac_excited)
{
	unsigned int exc_cur_pair, i, j;
	int ret;

	for (i = 0; i < num_exc_pins; i++) {
		unsigned int exc_cur = exc_curs[i];
		unsigned int pin = exc_pins[i];
		unsigned int current_src = 0;

		for (j = 0; j < AD4170_NUM_CURRENT_SRC; j++)
			if (st->cur_src_pins[j] == AD4170_CURRENT_SRC_DISABLED)
				break;

		if (j == AD4170_NUM_CURRENT_SRC)
			return dev_err_probe(&st->spi->dev, -EINVAL,
					     "Too many excitation current sources\n");

		current_src |= FIELD_PREP(AD4170_CURRENT_SRC_I_OUT_PIN_MSK, pin);
		current_src |= FIELD_PREP(AD4170_CURRENT_SRC_I_OUT_VAL_MSK, exc_cur);
		st->cur_src_pins[j] = pin;
		ret = regmap_write(st->regmap, AD4170_CURRENT_SRC_REG(j),
				   current_src);
		if (ret)
			return ret;
	}

	if (!ac_excited)
		return 0;

	if (num_exc_pins < 2)
		return dev_err_probe(&st->spi->dev, -EINVAL,
			"Current chopping requested but only one pin provided: %u\n",
			exc_pins[0]);

	/*
	 * Two use cases to handle here:
	 * - 2 pairs of excitation currents;
	 * - 1 pair of excitation currents.
	 */
	if (num_exc_pins == 4) {
		for (i = 0; i < AD4170_NUM_CURRENT_SRC; i++)
			if (st->cur_src_pins[i] != exc_pins[i])
				return dev_err_probe(&st->spi->dev, -EINVAL,
						     "Unable to use 4 exc pins\n");
	} else {
		/*
		 * Excitation current chopping is configured in pairs. Current
		 * sources IOUT0 and IOUT1 form pair 1, IOUT2 and IOUT3 make up
		 * pair 2. So, if current chopping was requested, check if the
		 * first end of the first pair of excitation currents is
		 * available. Try the next pair if IOUT0 has already been
		 * configured for another channel.
		 */
		i = st->cur_src_pins[0] == exc_pins[0] ? 0 : 2;

		if (st->cur_src_pins[i] != exc_pins[0] ||
		    st->cur_src_pins[i + 1] != exc_pins[1])
			return dev_err_probe(&st->spi->dev, -EINVAL,
					     "Failed to setup current chopping\n");

		st->cur_src_pins[i] = exc_pins[0];
		st->cur_src_pins[i + 1] = exc_pins[1];

		if (i == 0)
			exc_cur_pair = AD4170_MISC_CHOP_IEXC_PAIR1;
		else
			exc_cur_pair = AD4170_MISC_CHOP_IEXC_PAIR2;
	}

	/*
	 * Configure excitation current chopping.
	 * Chop both pairs if using four excitation pins.
	 */
	setup->misc |= FIELD_PREP(AD4170_MISC_CHOP_IEXC_MSK,
				  num_exc_pins == 2 ?
				  exc_cur_pair :
				  AD4170_MISC_CHOP_IEXC_BOTH);

	return 0;
}

static int ad4170_setup_bridge(struct ad4170_state *st,
			       struct fwnode_handle *child,
			       struct ad4170_setup *setup, u32 *exc_pins,
			       unsigned int *exc_curs, int num_exc_pins,
			       bool ac_excited)
{
	unsigned long gpio_mask;
	unsigned int i;
	int ret;

	/*
	 * If a specific current is provided through
	 * adi,excitation-current-n-microamp, set excitation pins provided
	 * through adi,excitation-pin-n to excite the bridge circuit.
	 */
	for (i = 0; i < num_exc_pins; i++)
		if (exc_curs[i] > 0)
			return ad4170_setup_current_src(st, child, setup, exc_pins,
							exc_curs, num_exc_pins,
							ac_excited);

	/*
	 * Else, use predefined ACX1, ACX1 negated, ACX2, ACX2 negated signals
	 * to AC excite the bridge. Those signals are output on GPIO2, GPIO0,
	 * GPIO3, and GPIO1, respectively. If only two pins are specified for AC
	 * excitation, use ACX1 and ACX2 (GPIO2 and GPIO3).
	 *
	 * Also, to avoid any short-circuit condition when more than one channel
	 * is enabled, set GPIO2 and GPIO0 high, and set GPIO1 and GPIO3 low to
	 * DC excite the bridge whenever a channel without AC excitation is
	 * selected. That is needed because GPIO pins are controlled by the next
	 * highest priority GPIO function when a channel doesn't enable AC
	 * excitation. See datasheet Figure 113 Weigh Scale (AC Excitation) for
	 * the reference circuit diagram.
	 */
	if (num_exc_pins == 2) {
		setup->misc |= FIELD_PREP(AD4170_MISC_CHOP_ADC_MSK, 0x3);

		gpio_mask = AD4170_GPIO_MODE_GPIO3_MSK | AD4170_GPIO_MODE_GPIO2_MSK;
		ret = regmap_update_bits(st->regmap, AD4170_GPIO_MODE_REG, gpio_mask,
					 FIELD_PREP(AD4170_GPIO_MODE_GPIO3_MSK,
						    AD4170_GPIO_MODE_GPIO_OUTPUT) |
					 FIELD_PREP(AD4170_GPIO_MODE_GPIO2_MSK,
						    AD4170_GPIO_MODE_GPIO_OUTPUT));
		if (ret)
			return ret;

		/*
		 * Set GPIO2 high and GPIO3 low to DC excite the bridge when
		 * a different channel is selected.
		 */
		gpio_mask = AD4170_GPIO_OUTPUT_GPIO_MSK(3) |
			    AD4170_GPIO_OUTPUT_GPIO_MSK(2);
		ret = regmap_update_bits(st->regmap, AD4170_GPIO_OUTPUT_REG, gpio_mask,
					 FIELD_PREP(AD4170_GPIO_OUTPUT_GPIO_MSK(3), 0) |
					 FIELD_PREP(AD4170_GPIO_OUTPUT_GPIO_MSK(2), 1));
		if (ret)
			return ret;

		st->gpio_fn[3] |= AD4170_GPIO_OUTPUT;
		st->gpio_fn[2] |= AD4170_GPIO_OUTPUT;
	} else {
		setup->misc |= FIELD_PREP(AD4170_MISC_CHOP_ADC_MSK, 0x2);

		gpio_mask = AD4170_GPIO_MODE_GPIO3_MSK | AD4170_GPIO_MODE_GPIO2_MSK |
			    AD4170_GPIO_MODE_GPIO1_MSK | AD4170_GPIO_MODE_GPIO0_MSK;
		ret = regmap_update_bits(st->regmap, AD4170_GPIO_MODE_REG, gpio_mask,
					 FIELD_PREP(AD4170_GPIO_MODE_GPIO3_MSK,
						    AD4170_GPIO_MODE_GPIO_OUTPUT) |
					 FIELD_PREP(AD4170_GPIO_MODE_GPIO2_MSK,
						    AD4170_GPIO_MODE_GPIO_OUTPUT) |
					 FIELD_PREP(AD4170_GPIO_MODE_GPIO1_MSK,
						    AD4170_GPIO_MODE_GPIO_OUTPUT) |
					 FIELD_PREP(AD4170_GPIO_MODE_GPIO0_MSK,
						    AD4170_GPIO_MODE_GPIO_OUTPUT));
		if (ret)
			return ret;

		/*
		 * Set GPIO2 and GPIO0 high, and set GPIO1 and GPIO3 low to DC
		 * excite the bridge when a different channel is selected.
		 */
		gpio_mask = AD4170_GPIO_OUTPUT_GPIO_MSK(3) |
			    AD4170_GPIO_OUTPUT_GPIO_MSK(2) |
			    AD4170_GPIO_OUTPUT_GPIO_MSK(1) |
			    AD4170_GPIO_OUTPUT_GPIO_MSK(0);
		ret = regmap_update_bits(st->regmap, AD4170_GPIO_OUTPUT_REG, gpio_mask,
					 FIELD_PREP(AD4170_GPIO_OUTPUT_GPIO_MSK(3), 0) |
					 FIELD_PREP(AD4170_GPIO_OUTPUT_GPIO_MSK(2), 1) |
					 FIELD_PREP(AD4170_GPIO_OUTPUT_GPIO_MSK(1), 0) |
					 FIELD_PREP(AD4170_GPIO_OUTPUT_GPIO_MSK(0), 1));
		if (ret)
			return ret;

		st->gpio_fn[3] |= AD4170_GPIO_OUTPUT;
		st->gpio_fn[2] |= AD4170_GPIO_OUTPUT;
		st->gpio_fn[1] |= AD4170_GPIO_OUTPUT;
		st->gpio_fn[0] |= AD4170_GPIO_OUTPUT;
	}

	return 0;
}

static int ad4170_setup_rtd(struct ad4170_state *st,
			    struct fwnode_handle *child,
			    struct ad4170_setup *setup, u32 *exc_pins,
			    unsigned int *exc_curs, int num_exc_pins, bool ac_excited)
{
	return ad4170_setup_current_src(st, child, setup, exc_pins,
					exc_curs, num_exc_pins, ac_excited);
}

static int ad4170_parse_external_sensor(struct ad4170_state *st,
					struct fwnode_handle *child,
					struct ad4170_setup *setup,
					struct iio_chan_spec *chan,
					unsigned int s_type)
{
	unsigned int num_exc_pins, reg_val;
	struct device *dev = &st->spi->dev;
	u32 pins[2], exc_pins[4], exc_curs[4];
	bool ac_excited;
	int ret;

	ret = fwnode_property_read_u32_array(child, "diff-channels", pins,
					     ARRAY_SIZE(pins));
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to read sensor diff-channels\n");

	chan->differential = true;
	chan->channel = pins[0];
	chan->channel2 = pins[1];

	ret = ad4170_parse_exc_current(st, child, exc_pins, exc_curs, &num_exc_pins);
	if (ret)
		return ret;

	/* The external sensor may not need excitation from the ADC chip. */
	if (num_exc_pins == 0)
		return 0;

	ret = ad4170_validate_excitation_pins(st, exc_pins, num_exc_pins);
	if (ret)
		return ret;

	ac_excited = fwnode_property_read_bool(child, "adi,excitation-ac");

	if (s_type == AD4170_THERMOCOUPLE_SENSOR) {
		if (st->pins_fn[chan->channel2] & AD4170_PIN_VBIAS) {
			reg_val = BIT(chan->channel2);
			ret = regmap_write(st->regmap, AD4170_V_BIAS_REG, reg_val);
			if (ret)
				dev_err_probe(dev, ret, "Failed to set vbias\n");
		}
	}
	if (s_type == AD4170_WEIGH_SCALE_SENSOR)
		ret = ad4170_setup_bridge(st, child, setup, exc_pins, exc_curs,
					  num_exc_pins, ac_excited);
	else
		ret = ad4170_setup_rtd(st, child, setup, exc_pins, exc_curs,
				       num_exc_pins, ac_excited);

	return ret;
}

static int ad4170_parse_reference(struct ad4170_state *st,
				  struct fwnode_handle *child,
				  struct ad4170_setup *setup)
{
	struct device *dev = &st->spi->dev;
	const char *propname;
	u32 aux;
	int ret;

	/* Optional positive reference buffering */
	propname = "adi,positive-reference-buffer";
	ret = device_property_match_property_string(dev, propname,
						    ad4170_ref_buf_str,
						    ARRAY_SIZE(ad4170_ref_buf_str));

	/* Default to full precharge buffer enabled. */
	setup->afe |= FIELD_PREP(AD4170_AFE_REF_BUF_P_MSK,
				 ret >= 0 ? ret : AD4170_REF_BUF_FULL);

	/* Optional negative reference buffering */
	propname = "adi,negative-reference-buffer";
	ret = device_property_match_property_string(dev, propname,
						    ad4170_ref_buf_str,
						    ARRAY_SIZE(ad4170_ref_buf_str));

	/* Default to full precharge buffer enabled. */
	setup->afe |= FIELD_PREP(AD4170_AFE_REF_BUF_M_MSK,
				 ret >= 0 ? ret : AD4170_REF_BUF_FULL);

	/* Optional voltage reference selection */
	propname = "adi,reference-select";
	aux = AD4170_REF_REFOUT; /* Default reference selection. */
	fwnode_property_read_u32(child, propname, &aux);
	if (aux > AD4170_REF_AVDD)
		return dev_err_probe(dev, -EINVAL, "Invalid %s: %u\n",
				     propname, aux);

	setup->afe |= FIELD_PREP(AD4170_AFE_REF_SELECT_MSK, aux);

	return 0;
}

static int ad4170_parse_adc_channel_type(struct device *dev,
					 struct fwnode_handle *child,
					 struct iio_chan_spec *chan)
{
	const char *propname, *propname2;
	int ret, ret2;
	u32 pins[2];

	propname = "single-channel";
	propname2 = "diff-channels";
	if (!fwnode_property_present(child, propname) &&
	    !fwnode_property_present(child, propname2))
		return dev_err_probe(dev, -EINVAL,
				     "Channel must define one of %s or %s.\n",
				     propname, propname2);

	/* Parse differential channel configuration */
	ret = fwnode_property_read_u32_array(child, propname2, pins,
					     ARRAY_SIZE(pins));
	if (!ret) {
		chan->differential = true;
		chan->channel = pins[0];
		chan->channel2 = pins[1];
		return 0;
	}
	/* Failed to parse diff chan so try pseudo-diff chan props */

	propname2 = "common-mode-channel";
	if (fwnode_property_present(child, propname) &&
	    !fwnode_property_present(child, propname2))
		return dev_err_probe(dev, -EINVAL,
				     "When %s is defined, %s must be defined too\n",
				     propname, propname2);

	/* Parse pseudo-differential channel configuration */
	ret = fwnode_property_read_u32(child, propname, &pins[0]);
	ret2 = fwnode_property_read_u32(child, propname2, &pins[1]);

	if (!ret && !ret2) {
		chan->differential = false;
		chan->channel = pins[0];
		chan->channel2 = pins[1];
		return 0;
	}
	return dev_err_probe(dev, -EINVAL,
			     "Failed to parse channel %lu input. %d, %d\n",
			     chan->address, ret, ret2);
}

static int ad4170_parse_channel_node(struct iio_dev *indio_dev,
				     struct fwnode_handle *child,
				     unsigned int chan_num)
{
	struct ad4170_state *st = iio_priv(indio_dev);
	unsigned int s_type = AD4170_ADC_SENSOR;
	struct device *dev = &st->spi->dev;
	struct ad4170_chan_info *chan_info;
	struct ad4170_setup *setup;
	struct iio_chan_spec *chan;
	unsigned int ref_select;
	unsigned int ch_reg;
	bool bipolar;
	int ret;

	ret = fwnode_property_read_u32(child, "reg", &ch_reg);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read channel reg\n");

	if (ch_reg >= AD4170_MAX_ADC_CHANNELS)
		return dev_err_probe(dev, -EINVAL,
				     "Channel idx greater than no of channels\n");

	chan = &st->chans[chan_num];
	*chan = ad4170_channel_template;

	chan->address = ch_reg;
	chan->scan_index = ch_reg;
	chan_info = &st->chan_infos[chan->address];

	chan_info->setup_num = AD4170_INVALID_SETUP;
	chan_info->initialized = true;

	setup = &chan_info->setup;
	ret = ad4170_parse_reference(st, child, setup);
	if (ret)
		return ret;

	ret = fwnode_property_match_property_string(child, "adi,sensor-type",
						    ad4170_sensor_type,
						    ARRAY_SIZE(ad4170_sensor_type));

	/* Default to conventional ADC channel if sensor type not present */
	s_type = ret < 0 ? AD4170_ADC_SENSOR : ret;
	switch (s_type) {
	case AD4170_ADC_SENSOR:
		ret = ad4170_parse_adc_channel_type(dev, child, chan);
		if (ret)
			return ret;

		break;
	case AD4170_WEIGH_SCALE_SENSOR:
	case AD4170_THERMOCOUPLE_SENSOR:
	case AD4170_RTD_SENSOR:
		ret = ad4170_parse_external_sensor(st, child, setup, chan, s_type);
		if (ret)
			return ret;

		break;
	default:
		return -EINVAL;
	}

	bipolar = fwnode_property_read_bool(child, "bipolar");
	setup->afe |= FIELD_PREP(AD4170_AFE_BIPOLAR_MSK, bipolar);
	if (bipolar)
		chan->scan_type.sign = 's';
	else
		chan->scan_type.sign = 'u';

	ret = ad4170_validate_channel(st, chan);
	if (ret)
		return ret;

	ref_select = FIELD_GET(AD4170_AFE_REF_SELECT_MSK, setup->afe);
	ret = ad4170_get_input_range(st, chan, ch_reg, ref_select);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Invalid input config\n");

	chan_info->input_range_uv = ret;
	return 0;
}

static int ad4170_parse_channels(struct iio_dev *indio_dev)
{
	struct ad4170_state *st = iio_priv(indio_dev);
	struct device *dev = &st->spi->dev;
	unsigned int num_channels;
	unsigned int chan_num;
	int ret;

	num_channels = device_get_child_node_count(dev);

	if (num_channels > AD4170_MAX_ADC_CHANNELS)
		return dev_err_probe(dev, -EINVAL, "Too many channels\n");

	/* Add one for temperature */
	num_channels = min(num_channels + 1, AD4170_MAX_ADC_CHANNELS);

	chan_num = 0;
	device_for_each_child_node_scoped(dev, child) {
		ret = ad4170_parse_channel_node(indio_dev, child, chan_num++);
		if (ret)
			return ret;
	}

	/*
	 * Add internal temperature sensor channel if the maximum number of
	 * channels has not been reached.
	 */
	if (num_channels < AD4170_MAX_ADC_CHANNELS) {
		struct ad4170_setup *setup = &st->chan_infos[chan_num].setup;

		st->chans[chan_num] = ad4170_temp_channel_template;
		st->chans[chan_num].address = chan_num;
		st->chans[chan_num].scan_index = chan_num;

		st->chan_infos[chan_num].setup_num = AD4170_INVALID_SETUP;
		st->chan_infos[chan_num].initialized = true;

		setup->afe |= FIELD_PREP(AD4170_AFE_REF_SELECT_MSK,
					 AD4170_REF_AVDD);

		ret = ad4170_get_input_range(st, &st->chans[chan_num], chan_num,
					     AD4170_REF_AVDD);
		if (ret < 0)
			return dev_err_probe(dev, ret, "Invalid input config\n");

		st->chan_infos[chan_num].input_range_uv = ret;
		chan_num++;
	}

	/* Add timestamp channel */
	struct iio_chan_spec ts_chan = IIO_CHAN_SOFT_TIMESTAMP(chan_num);

	st->chans[chan_num] = ts_chan;
	num_channels = num_channels + 1;

	indio_dev->num_channels = num_channels;
	indio_dev->channels = st->chans;

	return 0;
}

static struct ad4170_state *clk_hw_to_ad4170(struct clk_hw *hw)
{
	return container_of(hw, struct ad4170_state, int_clk_hw);
}

static unsigned long ad4170_sel_clk(struct ad4170_state *st,
				    unsigned int clk_sel)
{
	st->clock_ctrl &= ~AD4170_CLOCK_CTRL_CLOCKSEL_MSK;
	st->clock_ctrl |= FIELD_PREP(AD4170_CLOCK_CTRL_CLOCKSEL_MSK, clk_sel);
	return regmap_write(st->regmap, AD4170_CLOCK_CTRL_REG, st->clock_ctrl);
}

static unsigned long ad4170_clk_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	return AD4170_INT_CLOCK_16MHZ;
}

static int ad4170_clk_output_is_enabled(struct clk_hw *hw)
{
	struct ad4170_state *st = clk_hw_to_ad4170(hw);
	u32 clk_sel;

	clk_sel = FIELD_GET(AD4170_CLOCK_CTRL_CLOCKSEL_MSK, st->clock_ctrl);
	return clk_sel == AD4170_CLOCK_CTRL_CLOCKSEL_INT_OUT;
}

static int ad4170_clk_output_prepare(struct clk_hw *hw)
{
	struct ad4170_state *st = clk_hw_to_ad4170(hw);

	return ad4170_sel_clk(st, AD4170_CLOCK_CTRL_CLOCKSEL_INT_OUT);
}

static void ad4170_clk_output_unprepare(struct clk_hw *hw)
{
	struct ad4170_state *st = clk_hw_to_ad4170(hw);

	ad4170_sel_clk(st, AD4170_CLOCK_CTRL_CLOCKSEL_INT);
}

static const struct clk_ops ad4170_int_clk_ops = {
	.recalc_rate = ad4170_clk_recalc_rate,
	.is_enabled = ad4170_clk_output_is_enabled,
	.prepare = ad4170_clk_output_prepare,
	.unprepare = ad4170_clk_output_unprepare,
};

static int ad4170_register_clk_provider(struct iio_dev *indio_dev)
{
	struct ad4170_state *st = iio_priv(indio_dev);
	struct device *dev = indio_dev->dev.parent;
	struct clk_init_data init = {};
	int ret;

	if (device_property_read_string(dev, "clock-output-names", &init.name)) {
		init.name = devm_kasprintf(dev, GFP_KERNEL, "%pfw",
					   dev_fwnode(dev));
		if (!init.name)
			return -ENOMEM;
	}

	init.ops = &ad4170_int_clk_ops;

	st->int_clk_hw.init = &init;
	ret = devm_clk_hw_register(dev, &st->int_clk_hw);
	if (ret)
		return ret;

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
					   &st->int_clk_hw);
}

static int ad4170_clock_select(struct iio_dev *indio_dev)
{
	struct ad4170_state *st = iio_priv(indio_dev);
	struct device *dev = &st->spi->dev;
	struct clk *ext_clk;
	int ret;

	ext_clk = devm_clk_get_optional_enabled(dev, NULL);
	if (IS_ERR(ext_clk))
		return dev_err_probe(dev, PTR_ERR(ext_clk),
				     "Failed to get external clock\n");

	if (!ext_clk) {
		/* Use internal clock reference */
		st->mclk_hz = AD4170_INT_CLOCK_16MHZ;
		st->clock_ctrl |= FIELD_PREP(AD4170_CLOCK_CTRL_CLOCKSEL_MSK,
					     AD4170_CLOCK_CTRL_CLOCKSEL_INT_OUT);

		if (!device_property_present(&st->spi->dev, "#clock-cells"))
			return 0;

		return ad4170_register_clk_provider(indio_dev);
	}

	/* Read optional clock-names prop to specify the external clock type */
	ret = device_property_match_property_string(dev, "clock-names",
						    ad4170_clk_sel,
						    ARRAY_SIZE(ad4170_clk_sel));

	ret = ret < 0 ? 0 : ret; /* Default to external clock if no clock-names */
	st->clock_ctrl |= FIELD_PREP(AD4170_CLOCK_CTRL_CLOCKSEL_MSK,
				     AD4170_CLOCK_CTRL_CLOCKSEL_EXT + ret);

	st->mclk_hz = clk_get_rate(ext_clk);
	if (st->mclk_hz < AD4170_EXT_CLOCK_MHZ_MIN ||
	    st->mclk_hz > AD4170_EXT_CLOCK_MHZ_MAX) {
		return dev_err_probe(dev, -EINVAL,
				     "Invalid external clock frequency %u\n",
				     st->mclk_hz);
	}

	return 0;
}

static int ad4170_parse_firmware(struct iio_dev *indio_dev)
{
	unsigned int vbias_pins[AD4170_MAX_ANALOG_PINS];
	struct ad4170_state *st = iio_priv(indio_dev);
	struct device *dev = &st->spi->dev;
	unsigned int num_vbias_pins;
	int reg_data, ret;
	u32 int_pin_sel;
	unsigned int i;

	ret = ad4170_clock_select(indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to setup device clock\n");

	ret = regmap_write(st->regmap, AD4170_CLOCK_CTRL_REG, st->clock_ctrl);
	if (ret)
		return ret;

	for (i = 0; i < AD4170_NUM_CURRENT_SRC; i++)
		st->cur_src_pins[i] = AD4170_CURRENT_SRC_DISABLED;

	/* On power on, device defaults to using SDO pin for data ready signal */
	int_pin_sel = AD4170_INT_PIN_SDO;
	ret = device_property_match_property_string(dev, "interrupt-names",
						    ad4170_int_pin_names,
						    ARRAY_SIZE(ad4170_int_pin_names));
	if (ret >= 0)
		int_pin_sel = ret;

	reg_data = FIELD_PREP(AD4170_PIN_MUXING_DIG_AUX1_CTRL_MSK,
			      int_pin_sel == AD4170_INT_PIN_DIG_AUX1 ?
			      AD4170_PIN_MUXING_DIG_AUX1_RDY :
			      AD4170_PIN_MUXING_DIG_AUX1_DISABLED);

	ret = regmap_update_bits(st->regmap, AD4170_PIN_MUXING_REG,
				 AD4170_PIN_MUXING_DIG_AUX1_CTRL_MSK, reg_data);
	if (ret)
		return ret;

	ret = device_property_count_u32(dev, "adi,vbias-pins");
	if (ret > 0) {
		if (ret > AD4170_MAX_ANALOG_PINS)
			return dev_err_probe(dev, -EINVAL,
					     "Too many vbias pins %u\n", ret);

		num_vbias_pins = ret;

		ret = device_property_read_u32_array(dev, "adi,vbias-pins",
						     vbias_pins,
						     num_vbias_pins);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to read vbias pins\n");

		for (i = 0; i < num_vbias_pins; i++)
			st->pins_fn[vbias_pins[i]] |= AD4170_PIN_VBIAS;
	}

	ret = ad4170_parse_channels(indio_dev);
	if (ret)
		return ret;

	/* Only create a GPIO chip if flagged for it */
	if (device_property_read_bool(dev, "gpio-controller")) {
		ret = ad4170_gpio_init(indio_dev);
		if (ret)
			return ret;
	}

	return 0;
}

static int ad4170_initial_config(struct iio_dev *indio_dev)
{
	struct ad4170_state *st = iio_priv(indio_dev);
	struct device *dev = &st->spi->dev;
	unsigned int i;
	int ret;

	ad4170_fill_sps_tbl(st);

	ret = regmap_update_bits(st->regmap, AD4170_ADC_CTRL_REG,
				 AD4170_ADC_CTRL_MODE_MSK,
				 FIELD_PREP(AD4170_ADC_CTRL_MODE_MSK,
					    AD4170_ADC_CTRL_MODE_IDLE));
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to set ADC mode to idle\n");

	for (i = 0; i < indio_dev->num_channels; i++) {
		struct ad4170_chan_info *chan_info;
		struct iio_chan_spec const *chan;
		struct ad4170_setup *setup;
		unsigned int val;

		chan = &indio_dev->channels[i];
		if (chan->type == IIO_TIMESTAMP)
			continue;

		chan_info = &st->chan_infos[chan->address];

		setup = &chan_info->setup;
		setup->gain = AD4170_GAIN_REG_DEFAULT;
		ret = ad4170_write_channel_setup(st, chan->address, false);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to write channel setup\n");

		val = FIELD_PREP(AD4170_CHAN_MAP_AINP_MSK, chan->channel) |
		      FIELD_PREP(AD4170_CHAN_MAP_AINM_MSK, chan->channel2);

		ret = regmap_write(st->regmap, AD4170_CHAN_MAP_REG(i), val);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to write CHAN_MAP_REG\n");

		ret = ad4170_set_channel_freq(st, chan,
					      AD4170_DEFAULT_SAMP_RATE, 0);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to set channel freq\n");

		ret = ad4170_fill_scale_tbl(indio_dev, chan);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to fill scale tbl\n");
	}

	/* Disable all channels to avoid reading from unexpected channel */
	ret = regmap_write(st->regmap, AD4170_CHAN_EN_REG, 0);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to disable channels\n");

	/*
	 * Configure channels to share the same data output register, i.e. data
	 * can be read from the same register address regardless of channel
	 * number.
	 */
	return regmap_update_bits(st->regmap, AD4170_ADC_CTRL_REG,
				 AD4170_ADC_CTRL_MULTI_DATA_REG_SEL_MSK,
				 AD4170_ADC_CTRL_MULTI_DATA_REG_SEL_MSK);
}

static int ad4170_prepare_spi_message(struct ad4170_state *st)
{
	/*
	 * Continuous data register read is enabled on buffer postenable so
	 * no instruction phase is needed meaning we don't need to send the
	 * register address to read data. Transfer only needs the read buffer.
	 */
	st->xfer.rx_buf = &st->rx_buf;
	st->xfer.len = BITS_TO_BYTES(ad4170_channel_template.scan_type.realbits);

	spi_message_init_with_transfers(&st->msg, &st->xfer, 1);

	return devm_spi_optimize_message(&st->spi->dev, st->spi, &st->msg);
}

static int ad4170_buffer_postenable(struct iio_dev *indio_dev)
{
	struct ad4170_state *st = iio_priv(indio_dev);
	int ret;

	ret = regmap_update_bits(st->regmap, AD4170_ADC_CTRL_REG,
				 AD4170_ADC_CTRL_MODE_MSK,
				 FIELD_PREP(AD4170_ADC_CTRL_MODE_MSK,
					    AD4170_ADC_CTRL_MODE_CONT));
	if (ret)
		return ret;

	/*
	 * This enables continuous read of the ADC data register. The ADC must
	 * be in continuous conversion mode.
	 */
	return regmap_update_bits(st->regmap, AD4170_ADC_CTRL_REG,
				  AD4170_ADC_CTRL_CONT_READ_MSK,
				  FIELD_PREP(AD4170_ADC_CTRL_CONT_READ_MSK,
					     AD4170_ADC_CTRL_CONT_READ_ENABLE));
}

static int ad4170_buffer_predisable(struct iio_dev *indio_dev)
{
	struct ad4170_state *st = iio_priv(indio_dev);
	unsigned int i;
	int ret;

	/*
	 * Use a high register address (virtual register) to request a write of
	 * 0xA5 to the ADC during the first 8 SCLKs of the ADC data read cycle,
	 * thus exiting continuous read.
	 */
	ret = regmap_write(st->regmap, AD4170_ADC_CTRL_CONT_READ_EXIT_REG, 0);
	if (ret)
		return ret;

	ret = regmap_update_bits(st->regmap, AD4170_ADC_CTRL_REG,
				 AD4170_ADC_CTRL_CONT_READ_MSK,
				 FIELD_PREP(AD4170_ADC_CTRL_CONT_READ_MSK,
					    AD4170_ADC_CTRL_CONT_READ_DISABLE));
	if (ret)
		return ret;

	ret = regmap_update_bits(st->regmap, AD4170_ADC_CTRL_REG,
				 AD4170_ADC_CTRL_MODE_MSK,
				 FIELD_PREP(AD4170_ADC_CTRL_MODE_MSK,
					    AD4170_ADC_CTRL_MODE_IDLE));
	if (ret)
		return ret;

	/*
	 * The ADC sequences through all the enabled channels (see datasheet
	 * page 95). That can lead to incorrect channel being read if a
	 * single-shot read (or buffered read with different active_scan_mask)
	 * is done after buffer disable. Disable all channels so only requested
	 * channels will be read.
	 */
	for (i = 0; i < indio_dev->num_channels; i++) {
		if (indio_dev->channels[i].type == IIO_TIMESTAMP)
			continue;

		ret = ad4170_set_channel_enable(st, i, false);
		if (ret)
			return ret;
	}

	return 0;
}

static bool ad4170_validate_scan_mask(struct iio_dev *indio_dev,
				      const unsigned long *scan_mask)
{
	unsigned int masklength = iio_get_masklength(indio_dev);
	unsigned int enabled;

	/*
	 * The channel sequencer cycles through the enabled channels in
	 * sequential order, from channel 0 to channel 15, bypassing disabled
	 * channels. When more than one channel is enabled, channel 0 must
	 * always be enabled. See datasheet channel_en register description at
	 * page 95.
	 */
	enabled = bitmap_weight(scan_mask, masklength);
	if (enabled > 1)
		return test_bit(0, scan_mask);

	return enabled == 1;
}

static const struct iio_buffer_setup_ops ad4170_buffer_ops = {
	.postenable = ad4170_buffer_postenable,
	.predisable = ad4170_buffer_predisable,
	.validate_scan_mask = ad4170_validate_scan_mask,
};

static irqreturn_t ad4170_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad4170_state *st = iio_priv(indio_dev);
	unsigned int chan_index;
	unsigned int i = 0;
	int ret;

	iio_for_each_active_channel(indio_dev, chan_index) {
		ret = spi_sync(st->spi, &st->msg);
		if (ret)
			goto err_out;

		memcpy(&st->bounce_buffer[i++], st->rx_buf, ARRAY_SIZE(st->rx_buf));
	}

	iio_push_to_buffers_with_ts(indio_dev, st->bounce_buffer,
				    sizeof(st->bounce_buffer),
				    iio_get_time_ns(indio_dev));
err_out:
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static const struct iio_trigger_ops ad4170_trigger_ops = {
	.validate_device = iio_trigger_validate_own_device,
};

static irqreturn_t ad4170_irq_handler(int irq, void *dev_id)
{
	struct iio_dev *indio_dev = dev_id;
	struct ad4170_state *st = iio_priv(indio_dev);

	if (iio_buffer_enabled(indio_dev))
		iio_trigger_poll(st->trig);
	else
		complete(&st->completion);

	return IRQ_HANDLED;
};

static int ad4170_trigger_setup(struct iio_dev *indio_dev)
{
	struct ad4170_state *st = iio_priv(indio_dev);
	struct device *dev = &st->spi->dev;
	int ret;

	st->trig = devm_iio_trigger_alloc(dev, "%s-trig%d",
					  indio_dev->name,
					  iio_device_id(indio_dev));
	if (!st->trig)
		return -ENOMEM;

	st->trig->ops = &ad4170_trigger_ops;

	iio_trigger_set_drvdata(st->trig, indio_dev);
	ret = devm_iio_trigger_register(dev, st->trig);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register trigger\n");

	indio_dev->trig = iio_trigger_get(st->trig);

	return 0;
}

static int ad4170_regulator_setup(struct ad4170_state *st)
{
	struct device *dev = &st->spi->dev;
	int ret;

	/* Required regulators */
	ret = devm_regulator_get_enable_read_voltage(dev, "avdd");
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get AVDD voltage.\n");

	st->vrefs_uv[AD4170_AVDD_SUP] = ret;

	ret = devm_regulator_get_enable_read_voltage(dev, "iovdd");
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get IOVDD voltage.\n");

	st->vrefs_uv[AD4170_IOVDD_SUP] = ret;

	/* Optional regulators */
	ret = devm_regulator_get_enable_read_voltage(dev, "avss");
	if (ret < 0 && ret != -ENODEV)
		return dev_err_probe(dev, ret, "Failed to get AVSS voltage.\n");

	/*
	 * Assume AVSS at GND (0V) if not provided.
	 * REVISIT: AVSS is never above system ground level (i.e. AVSS is either
	 * GND or a negative voltage). But we currently don't have support for
	 * reading negative voltages with the regulator framework. So, the
	 * current AD4170 support reads a positive value from the regulator,
	 * then inverts sign to make that negative.
	 */
	st->vrefs_uv[AD4170_AVSS_SUP] = ret == -ENODEV ? 0 : -ret;

	ret = devm_regulator_get_enable_read_voltage(dev, "refin1p");
	if (ret < 0 && ret != -ENODEV)
		return dev_err_probe(dev, ret, "Failed to get REFIN+ voltage.\n");

	st->vrefs_uv[AD4170_REFIN1P_SUP] = ret;

	ret = devm_regulator_get_enable_read_voltage(dev, "refin1n");
	if (ret < 0 && ret != -ENODEV)
		return dev_err_probe(dev, ret, "Failed to get REFIN- voltage.\n");

	/*
	 * Negative supplies are assumed to provide negative voltage.
	 * REVISIT when support for negative regulator voltage read be available
	 * in the regulator framework.
	 */
	st->vrefs_uv[AD4170_REFIN1N_SUP] = ret == -ENODEV ? -ENODEV : -ret;

	ret = devm_regulator_get_enable_read_voltage(dev, "refin2p");
	if (ret < 0 && ret != -ENODEV)
		return dev_err_probe(dev, ret, "Failed to get REFIN2+ voltage.\n");

	st->vrefs_uv[AD4170_REFIN2P_SUP] = ret;

	ret = devm_regulator_get_enable_read_voltage(dev, "refin2n");
	if (ret < 0 && ret != -ENODEV)
		return dev_err_probe(dev, ret, "Failed to get REFIN2- voltage.\n");

	/*
	 * Negative supplies are assumed to provide negative voltage.
	 * REVISIT when support for negative regulator voltage read be available
	 * in the regulator framework.
	 */
	st->vrefs_uv[AD4170_REFIN2N_SUP] = ret == -ENODEV ? -ENODEV : -ret;

	return 0;
}

static int ad4170_probe(struct spi_device *spi)
{
	const struct ad4170_chip_info *chip;
	struct device *dev = &spi->dev;
	struct iio_dev *indio_dev;
	struct ad4170_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->spi = spi;

	ret = devm_mutex_init(dev, &st->lock);
	if (ret)
		return ret;

	chip = spi_get_device_match_data(spi);
	if (!chip)
		return -EINVAL;

	indio_dev->name = chip->name;
	indio_dev->info = &ad4170_info;

	st->regmap = devm_regmap_init(dev, NULL, st, &ad4170_regmap_config);
	if (IS_ERR(st->regmap))
		return dev_err_probe(dev, PTR_ERR(st->regmap),
				     "Failed to initialize regmap\n");

	ret = ad4170_regulator_setup(st);
	if (ret)
		return ret;

	ret = ad4170_soft_reset(st);
	if (ret)
		return ret;

	ret = ad4170_parse_firmware(indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to parse firmware\n");

	ret = ad4170_initial_config(indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to setup device\n");

	init_completion(&st->completion);

	if (spi->irq) {
		ret = devm_request_irq(dev, spi->irq, &ad4170_irq_handler,
				       IRQF_ONESHOT, indio_dev->name, indio_dev);
		if (ret)
			return ret;

		ret = ad4170_trigger_setup(indio_dev);
		if (ret)
			return ret;
	}

	ret = ad4170_prepare_spi_message(st);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to prepare SPI message\n");

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL,
					      &ad4170_trigger_handler,
					      &ad4170_buffer_ops);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to setup read buffer\n");

	return devm_iio_device_register(dev, indio_dev);
}

static const struct spi_device_id ad4170_id_table[] = {
	{ "ad4170-4", (kernel_ulong_t)&ad4170_chip_info },
	{ "ad4190-4", (kernel_ulong_t)&ad4190_chip_info },
	{ "ad4195-4", (kernel_ulong_t)&ad4195_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad4170_id_table);

static const struct of_device_id ad4170_of_match[] = {
	{ .compatible = "adi,ad4170-4", .data = &ad4170_chip_info },
	{ .compatible = "adi,ad4190-4", .data = &ad4190_chip_info },
	{ .compatible = "adi,ad4195-4", .data = &ad4195_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ad4170_of_match);

static struct spi_driver ad4170_driver = {
	.driver = {
		.name = "ad4170-4",
		.of_match_table = ad4170_of_match,
	},
	.probe = ad4170_probe,
	.id_table = ad4170_id_table,
};
module_spi_driver(ad4170_driver);

MODULE_AUTHOR("Ana-Maria Cusco <ana-maria.cusco@analog.com>");
MODULE_AUTHOR("Marcelo Schmitt <marcelo.schmitt@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD4170 SPI driver");
MODULE_LICENSE("GPL");
