// SPDX-License-Identifier: GPL-2.0+
/*
 * IIO driver for MCP356X/MCP356XR and MCP346X/MCP346XR series ADC chip family
 *
 * Copyright (C) 2022-2023 Microchip Technology Inc. and its subsidiaries
 *
 * Author: Marius Cristea <marius.cristea@microchip.com>
 *
 * Datasheet for MCP3561, MCP3562, MCP3564 can be found here:
 * https://ww1.microchip.com/downloads/aemDocuments/documents/MSLD/ProductDocuments/DataSheets/MCP3561-2-4-Family-Data-Sheet-DS20006181C.pdf
 * Datasheet for MCP3561R, MCP3562R, MCP3564R can be found here:
 * https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP3561_2_4R-Data-Sheet-DS200006391C.pdf
 * Datasheet for MCP3461, MCP3462, MCP3464 can be found here:
 * https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP3461-2-4-Two-Four-Eight-Channel-153.6-ksps-Low-Noise-16-Bit-Delta-Sigma-ADC-Data-Sheet-20006180D.pdf
 * Datasheet for MCP3461R, MCP3462R, MCP3464R can be found here:
 * https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP3461-2-4R-Family-Data-Sheet-DS20006404C.pdf
 */

#include <linux/bitfield.h>
#include <linux/iopoll.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/units.h>
#include <linux/util_macros.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define MCP3564_ADCDATA_REG		0x00

#define MCP3564_CONFIG0_REG		0x01
#define MCP3564_CONFIG0_ADC_MODE_MASK		GENMASK(1, 0)
/* Current Source/Sink Selection Bits for Sensor Bias */
#define MCP3564_CONFIG0_CS_SEL_MASK		GENMASK(3, 2)
/* Internal clock is selected and AMCLK is present on the analog master clock output pin */
#define MCP3564_CONFIG0_USE_INT_CLK_OUTPUT_EN	0x03
/* Internal clock is selected and no clock output is present on the CLK pin */
#define MCP3564_CONFIG0_USE_INT_CLK		0x02
/* External digital clock */
#define MCP3564_CONFIG0_USE_EXT_CLK		0x01
/* External digital clock (default) */
#define MCP3564_CONFIG0_USE_EXT_CLK_DEFAULT	0x00
#define MCP3564_CONFIG0_CLK_SEL_MASK		GENMASK(5, 4)
#define MCP3456_CONFIG0_BIT6_DEFAULT		BIT(6)
#define MCP3456_CONFIG0_VREF_MASK		BIT(7)

#define MCP3564_CONFIG1_REG		0x02
#define MCP3564_CONFIG1_OVERSPL_RATIO_MASK	GENMASK(5, 2)

#define MCP3564_CONFIG2_REG		0x03
#define MCP3564_CONFIG2_AZ_REF_MASK		BIT(1)
#define MCP3564_CONFIG2_AZ_MUX_MASK		BIT(2)

#define MCP3564_CONFIG2_HARDWARE_GAIN_MASK	GENMASK(5, 3)
#define MCP3564_DEFAULT_HARDWARE_GAIN		0x01
#define MCP3564_CONFIG2_BOOST_CURRENT_MASK	GENMASK(7, 6)

#define MCP3564_CONFIG3_REG		0x04
#define MCP3464_CONFIG3_EN_GAINCAL_MASK		BIT(0)
#define MCP3464_CONFIG3_EN_OFFCAL_MASK		BIT(1)
#define MCP3464_CONFIG3_EN_CRCCOM_MASK		BIT(2)
#define MCP3464_CONFIG3_CRC_FORMAT_MASK		BIT(3)
/*
 * ADC Output Data Format 32-bit (25-bit right justified data + Channel ID):
 *                CHID[3:0] + SGN extension (4 bits) + 24-bit ADC data.
 *        It allows overrange with the SGN extension.
 */
#define MCP3464_CONFIG3_DATA_FMT_32B_WITH_CH_ID		3
/*
 * ADC Output Data Format 32-bit (25-bit right justified data):
 *                SGN extension (8-bit) + 24-bit ADC data.
 *        It allows overrange with the SGN extension.
 */
#define MCP3464_CONFIG3_DATA_FMT_32B_SGN_EXT		2
/*
 * ADC Output Data Format 32-bit (24-bit left justified data):
 *                24-bit ADC data + 0x00 (8-bit).
 *        It does not allow overrange (ADC code locked to 0xFFFFFF or 0x800000).
 */
#define MCP3464_CONFIG3_DATA_FMT_32B_LEFT_JUSTIFIED	1
/*
 * ADC Output Data Format 24-bit (default ADC coding):
 *                24-bit ADC data.
 *        It does not allow overrange (ADC code locked to 0xFFFFFF or 0x800000).
 */
#define MCP3464_CONFIG3_DATA_FMT_24B			0
#define MCP3464_CONFIG3_DATA_FORMAT_MASK	GENMASK(5, 4)

/* Continuous Conversion mode or continuous conversion cycle in SCAN mode. */
#define MCP3464_CONFIG3_CONV_MODE_CONTINUOUS		3
/*
 * One-shot conversion or one-shot cycle in SCAN mode. It sets ADC_MODE[1:0] to ‘10’
 * (standby) at the end of the conversion or at the end of the conversion cycle in SCAN mode.
 */
#define MCP3464_CONFIG3_CONV_MODE_ONE_SHOT_STANDBY	2
/*
 * One-shot conversion or one-shot cycle in SCAN mode. It sets ADC_MODE[1:0] to ‘0x’ (ADC
 * Shutdown) at the end of the conversion or at the end of the conversion cycle in SCAN
 * mode (default).
 */
#define MCP3464_CONFIG3_CONV_MODE_ONE_SHOT_SHUTDOWN	0
#define MCP3464_CONFIG3_CONV_MODE_MASK		GENMASK(7, 6)

#define MCP3564_IRQ_REG			0x05
#define MCP3464_EN_STP_MASK			BIT(0)
#define MCP3464_EN_FASTCMD_MASK			BIT(1)
#define MCP3464_IRQ_MODE_0_MASK			BIT(2)
#define MCP3464_IRQ_MODE_1_MASK			BIT(3)
#define MCP3564_POR_STATUS_MASK			BIT(4)
#define MCP3564_CRCCFG_STATUS_MASK		BIT(5)
#define MCP3564_DATA_READY_MASK			BIT(6)

#define MCP3564_MUX_REG			0x06
#define MCP3564_MUX_VIN_P_MASK			GENMASK(7, 4)
#define MCP3564_MUX_VIN_N_MASK			GENMASK(3, 0)
#define MCP3564_MUX_SET(x, y)			(FIELD_PREP(MCP3564_MUX_VIN_P_MASK, (x)) |	\
						FIELD_PREP(MCP3564_MUX_VIN_N_MASK, (y)))

#define MCP3564_SCAN_REG		0x07
#define MCP3564_SCAN_CH_SEL_MASK		GENMASK(15, 0)
#define MCP3564_SCAN_CH_SEL_SET(x)		FIELD_PREP(MCP3564_SCAN_CH_SEL_MASK, (x))
#define MCP3564_SCAN_DELAY_TIME_MASK		GENMASK(23, 21)
#define MCP3564_SCAN_DELAY_TIME_SET(x)		FIELD_PREP(MCP3564_SCAN_DELAY_TIME_MASK, (x))
#define MCP3564_SCAN_DEFAULT_VALUE		0

#define MCP3564_TIMER_REG		0x08
#define MCP3564_TIMER_DEFAULT_VALUE		0

#define MCP3564_OFFSETCAL_REG		0x09
#define MCP3564_DEFAULT_OFFSETCAL		0

#define MCP3564_GAINCAL_REG		0x0A
#define MCP3564_DEFAULT_GAINCAL			0x00800000

#define MCP3564_RESERVED_B_REG		0x0B

#define MCP3564_RESERVED_C_REG		0x0C
#define MCP3564_C_REG_DEFAULT			0x50
#define MCP3564R_C_REG_DEFAULT			0x30

#define MCP3564_LOCK_REG		0x0D
#define MCP3564_LOCK_WRITE_ACCESS_PASSWORD	0xA5
#define MCP3564_RESERVED_E_REG		0x0E
#define MCP3564_CRCCFG_REG		0x0F

#define MCP3564_CMD_HW_ADDR_MASK	GENMASK(7, 6)
#define MCP3564_CMD_ADDR_MASK		GENMASK(5, 2)

#define MCP3564_HW_ADDR_MASK		GENMASK(1, 0)

#define MCP3564_FASTCMD_START	0x0A
#define MCP3564_FASTCMD_RESET	0x0E

#define MCP3461_HW_ID		0x0008
#define MCP3462_HW_ID		0x0009
#define MCP3464_HW_ID		0x000B

#define MCP3561_HW_ID		0x000C
#define MCP3562_HW_ID		0x000D
#define MCP3564_HW_ID		0x000F
#define MCP3564_HW_ID_MASK	GENMASK(3, 0)

#define MCP3564R_INT_VREF_MV	2400

#define MCP3564_DATA_READY_TIMEOUT_MS	2000

#define MCP3564_MAX_PGA				8
#define MCP3564_MAX_BURNOUT_IDX			4
#define MCP3564_MAX_CHANNELS			66

enum mcp3564_ids {
	mcp3461,
	mcp3462,
	mcp3464,
	mcp3561,
	mcp3562,
	mcp3564,
	mcp3461r,
	mcp3462r,
	mcp3464r,
	mcp3561r,
	mcp3562r,
	mcp3564r,
};

enum mcp3564_delay_time {
	MCP3564_NO_DELAY,
	MCP3564_DELAY_8_DMCLK,
	MCP3564_DELAY_16_DMCLK,
	MCP3564_DELAY_32_DMCLK,
	MCP3564_DELAY_64_DMCLK,
	MCP3564_DELAY_128_DMCLK,
	MCP3564_DELAY_256_DMCLK,
	MCP3564_DELAY_512_DMCLK
};

enum mcp3564_adc_conversion_mode {
	MCP3564_ADC_MODE_DEFAULT,
	MCP3564_ADC_MODE_SHUTDOWN,
	MCP3564_ADC_MODE_STANDBY,
	MCP3564_ADC_MODE_CONVERSION
};

enum mcp3564_adc_bias_current {
	MCP3564_BOOST_CURRENT_x0_50,
	MCP3564_BOOST_CURRENT_x0_66,
	MCP3564_BOOST_CURRENT_x1_00,
	MCP3564_BOOST_CURRENT_x2_00
};

enum mcp3564_burnout {
	MCP3564_CONFIG0_CS_SEL_0_0_uA,
	MCP3564_CONFIG0_CS_SEL_0_9_uA,
	MCP3564_CONFIG0_CS_SEL_3_7_uA,
	MCP3564_CONFIG0_CS_SEL_15_uA
};

enum mcp3564_channel_names {
	MCP3564_CH0,
	MCP3564_CH1,
	MCP3564_CH2,
	MCP3564_CH3,
	MCP3564_CH4,
	MCP3564_CH5,
	MCP3564_CH6,
	MCP3564_CH7,
	MCP3564_AGND,
	MCP3564_AVDD,
	MCP3564_RESERVED, /* do not use */
	MCP3564_REFIN_POZ,
	MCP3564_REFIN_NEG,
	MCP3564_TEMP_DIODE_P,
	MCP3564_TEMP_DIODE_M,
	MCP3564_INTERNAL_VCM,
};

enum mcp3564_oversampling {
	MCP3564_OVERSAMPLING_RATIO_32,
	MCP3564_OVERSAMPLING_RATIO_64,
	MCP3564_OVERSAMPLING_RATIO_128,
	MCP3564_OVERSAMPLING_RATIO_256,
	MCP3564_OVERSAMPLING_RATIO_512,
	MCP3564_OVERSAMPLING_RATIO_1024,
	MCP3564_OVERSAMPLING_RATIO_2048,
	MCP3564_OVERSAMPLING_RATIO_4096,
	MCP3564_OVERSAMPLING_RATIO_8192,
	MCP3564_OVERSAMPLING_RATIO_16384,
	MCP3564_OVERSAMPLING_RATIO_20480,
	MCP3564_OVERSAMPLING_RATIO_24576,
	MCP3564_OVERSAMPLING_RATIO_40960,
	MCP3564_OVERSAMPLING_RATIO_49152,
	MCP3564_OVERSAMPLING_RATIO_81920,
	MCP3564_OVERSAMPLING_RATIO_98304
};

static const unsigned int mcp3564_oversampling_avail[] = {
	[MCP3564_OVERSAMPLING_RATIO_32] = 32,
	[MCP3564_OVERSAMPLING_RATIO_64] = 64,
	[MCP3564_OVERSAMPLING_RATIO_128] = 128,
	[MCP3564_OVERSAMPLING_RATIO_256] = 256,
	[MCP3564_OVERSAMPLING_RATIO_512] = 512,
	[MCP3564_OVERSAMPLING_RATIO_1024] = 1024,
	[MCP3564_OVERSAMPLING_RATIO_2048] = 2048,
	[MCP3564_OVERSAMPLING_RATIO_4096] = 4096,
	[MCP3564_OVERSAMPLING_RATIO_8192] = 8192,
	[MCP3564_OVERSAMPLING_RATIO_16384] = 16384,
	[MCP3564_OVERSAMPLING_RATIO_20480] = 20480,
	[MCP3564_OVERSAMPLING_RATIO_24576] = 24576,
	[MCP3564_OVERSAMPLING_RATIO_40960] = 40960,
	[MCP3564_OVERSAMPLING_RATIO_49152] = 49152,
	[MCP3564_OVERSAMPLING_RATIO_81920] = 81920,
	[MCP3564_OVERSAMPLING_RATIO_98304] = 98304
};

/*
 * Current Source/Sink Selection Bits for Sensor Bias (source on VIN+/sink on VIN-)
 */
static const int mcp3564_burnout_avail[][2] = {
	[MCP3564_CONFIG0_CS_SEL_0_0_uA] = { 0, 0 },
	[MCP3564_CONFIG0_CS_SEL_0_9_uA] = { 0, 900 },
	[MCP3564_CONFIG0_CS_SEL_3_7_uA] = { 0, 3700 },
	[MCP3564_CONFIG0_CS_SEL_15_uA] = { 0, 15000 }
};

/*
 * BOOST[1:0]: ADC Bias Current Selection
 */
static const char * const mcp3564_boost_current_avail[] = {
	[MCP3564_BOOST_CURRENT_x0_50] = "0.5",
	[MCP3564_BOOST_CURRENT_x0_66] = "0.66",
	[MCP3564_BOOST_CURRENT_x1_00] = "1",
	[MCP3564_BOOST_CURRENT_x2_00] = "2",
};

/*
 * Calibration bias values
 */
static const int mcp3564_calib_bias[] = {
	-8388608,	/* min: -2^23		*/
	 1,		/* step: 1		*/
	 8388607	/* max:  2^23 - 1	*/
};

/*
 * Calibration scale values
 * The Gain Error Calibration register (GAINCAL) is an
 * unsigned 24-bit register that holds the digital gain error
 * calibration value, GAINCAL which could be calculated by
 * GAINCAL (V/V) = (GAINCAL[23:0])/8388608
 * The gain error calibration value range in equivalent voltage is [0; 2-2^(-23)]
 */
static const unsigned int mcp3564_calib_scale[] = {
	0,		/* min:  0		*/
	1,		/* step: 1/8388608	*/
	16777215	/* max:  2 - 2^(-23)	*/
};

/* Programmable hardware gain x1/3, x1, x2, x4, x8, x16, x32, x64 */
static const int mcp3564_hwgain_frac[] = {
	3, 10,
	1, 1,
	2, 1,
	4, 1,
	8, 1,
	16, 1,
	32, 1,
	64, 1
};

static const char *mcp3564_channel_labels[2] = {
	"burnout_current", "temperature",
};

/**
 * struct mcp3564_chip_info - chip specific data
 * @name:		device name
 * @num_channels:	number of channels
 * @resolution:		ADC resolution
 * @have_vref:		does the hardware have an internal voltage reference?
 */
struct mcp3564_chip_info {
	const char	*name;
	unsigned int	num_channels;
	unsigned int	resolution;
	bool		have_vref;
};

/**
 * struct mcp3564_state - working data for a ADC device
 * @chip_info:		chip specific data
 * @spi:		SPI device structure
 * @vref:		the regulator device used as a voltage reference in case
 *			external voltage reference is used
 * @vref_mv:		voltage reference value in miliVolts
 * @lock:		synchronize access to driver's state members
 * @dev_addr:		hardware device address
 * @oversampling:	the index inside oversampling list of the ADC
 * @hwgain:		the index inside hardware gain list of the ADC
 * @scale_tbls:		table with precalculated scale
 * @calib_bias:		calibration bias value
 * @calib_scale:	calibration scale value
 * @current_boost_mode:	the index inside current boost list of the ADC
 * @burnout_mode:	the index inside current bias list of the ADC
 * @auto_zeroing_mux:	set if ADC auto-zeroing algorithm is enabled
 * @auto_zeroing_ref:	set if ADC auto-Zeroing Reference Buffer Setting is enabled
 * @have_vref:		does the ADC have an internal voltage reference?
 * @labels:		table with channels labels
 */
struct mcp3564_state {
	const struct mcp3564_chip_info	*chip_info;
	struct spi_device		*spi;
	struct regulator		*vref;
	unsigned short			vref_mv;
	struct mutex			lock; /* Synchronize access to driver's state members */
	u8				dev_addr;
	enum mcp3564_oversampling	oversampling;
	unsigned int			hwgain;
	unsigned int			scale_tbls[MCP3564_MAX_PGA][2];
	int				calib_bias;
	int				calib_scale;
	unsigned int			current_boost_mode;
	enum mcp3564_burnout		burnout_mode;
	bool				auto_zeroing_mux;
	bool				auto_zeroing_ref;
	bool				have_vref;
	const char			*labels[MCP3564_MAX_CHANNELS];
};

static inline u8 mcp3564_cmd_write(u8 chip_addr, u8 reg)
{
	return FIELD_PREP(MCP3564_CMD_HW_ADDR_MASK, chip_addr) |
	       FIELD_PREP(MCP3564_CMD_ADDR_MASK, reg) |
	       BIT(1);
}

static inline u8 mcp3564_cmd_read(u8 chip_addr, u8 reg)
{
	return FIELD_PREP(MCP3564_CMD_HW_ADDR_MASK, chip_addr) |
	       FIELD_PREP(MCP3564_CMD_ADDR_MASK, reg) |
	       BIT(0);
}

static int mcp3564_read_8bits(struct mcp3564_state *adc, u8 reg, u8 *val)
{
	int ret;
	u8 tx_buf;
	u8 rx_buf;

	tx_buf = mcp3564_cmd_read(adc->dev_addr, reg);

	ret = spi_write_then_read(adc->spi, &tx_buf, sizeof(tx_buf),
				  &rx_buf, sizeof(rx_buf));
	*val = rx_buf;

	return ret;
}

static int mcp3564_read_16bits(struct mcp3564_state *adc, u8 reg, u16 *val)
{
	int ret;
	u8 tx_buf;
	__be16 rx_buf;

	tx_buf = mcp3564_cmd_read(adc->dev_addr, reg);

	ret = spi_write_then_read(adc->spi, &tx_buf, sizeof(tx_buf),
				  &rx_buf, sizeof(rx_buf));
	*val = be16_to_cpu(rx_buf);

	return ret;
}

static int mcp3564_read_32bits(struct mcp3564_state *adc, u8 reg, u32 *val)
{
	int ret;
	u8 tx_buf;
	__be32 rx_buf;

	tx_buf = mcp3564_cmd_read(adc->dev_addr, reg);

	ret = spi_write_then_read(adc->spi, &tx_buf, sizeof(tx_buf),
				  &rx_buf, sizeof(rx_buf));
	*val = be32_to_cpu(rx_buf);

	return ret;
}

static int mcp3564_write_8bits(struct mcp3564_state *adc, u8 reg, u8 val)
{
	u8 tx_buf[2];

	tx_buf[0] = mcp3564_cmd_write(adc->dev_addr, reg);
	tx_buf[1] = val;

	return spi_write_then_read(adc->spi, tx_buf, sizeof(tx_buf), NULL, 0);
}

static int mcp3564_write_24bits(struct mcp3564_state *adc, u8 reg, u32 val)
{
	__be32 val_be;

	val |= (mcp3564_cmd_write(adc->dev_addr, reg) << 24);
	val_be = cpu_to_be32(val);

	return spi_write_then_read(adc->spi, &val_be, sizeof(val_be), NULL, 0);
}

static int mcp3564_fast_cmd(struct mcp3564_state *adc, u8 fast_cmd)
{
	u8 val;

	val = FIELD_PREP(MCP3564_CMD_HW_ADDR_MASK, adc->dev_addr) |
			 FIELD_PREP(MCP3564_CMD_ADDR_MASK, fast_cmd);

	return spi_write_then_read(adc->spi, &val, 1, NULL, 0);
}

static int mcp3564_update_8bits(struct mcp3564_state *adc, u8 reg, u32 mask, u8 val)
{
	u8 tmp;
	int ret;

	val &= mask;

	ret = mcp3564_read_8bits(adc, reg, &tmp);
	if (ret < 0)
		return ret;

	tmp &= ~mask;
	tmp |= val;

	return mcp3564_write_8bits(adc, reg, tmp);
}

static int mcp3564_set_current_boost_mode(struct iio_dev *indio_dev,
					  const struct iio_chan_spec *chan,
					  unsigned int mode)
{
	struct mcp3564_state *adc = iio_priv(indio_dev);
	int ret;

	dev_dbg(&indio_dev->dev, "%s: %d\n", __func__, mode);

	mutex_lock(&adc->lock);
	ret = mcp3564_update_8bits(adc, MCP3564_CONFIG2_REG, MCP3564_CONFIG2_BOOST_CURRENT_MASK,
				   FIELD_PREP(MCP3564_CONFIG2_BOOST_CURRENT_MASK, mode));

	if (ret)
		dev_err(&indio_dev->dev, "Failed to configure CONFIG2 register\n");
	else
		adc->current_boost_mode = mode;

	mutex_unlock(&adc->lock);

	return ret;
}

static int mcp3564_get_current_boost_mode(struct iio_dev *indio_dev,
					  const struct iio_chan_spec *chan)
{
	struct mcp3564_state *adc = iio_priv(indio_dev);

	return adc->current_boost_mode;
}

static const struct iio_enum mcp3564_current_boost_mode_enum = {
	.items = mcp3564_boost_current_avail,
	.num_items = ARRAY_SIZE(mcp3564_boost_current_avail),
	.set = mcp3564_set_current_boost_mode,
	.get = mcp3564_get_current_boost_mode,
};

static const struct iio_chan_spec_ext_info mcp3564_ext_info[] = {
	IIO_ENUM("boost_current_gain", IIO_SHARED_BY_ALL, &mcp3564_current_boost_mode_enum),
	{
		.name = "boost_current_gain_available",
		.shared = IIO_SHARED_BY_ALL,
		.read = iio_enum_available_read,
		.private = (uintptr_t)&mcp3564_current_boost_mode_enum,
	},
	{ }
};

static ssize_t mcp3564_auto_zeroing_mux_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct mcp3564_state *adc = iio_priv(indio_dev);

	return sysfs_emit(buf, "%d\n", adc->auto_zeroing_mux);
}

static ssize_t mcp3564_auto_zeroing_mux_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct mcp3564_state *adc = iio_priv(indio_dev);
	bool auto_zero;
	int ret;

	ret = kstrtobool(buf, &auto_zero);
	if (ret)
		return ret;

	mutex_lock(&adc->lock);
	ret = mcp3564_update_8bits(adc, MCP3564_CONFIG2_REG, MCP3564_CONFIG2_AZ_MUX_MASK,
				   FIELD_PREP(MCP3564_CONFIG2_AZ_MUX_MASK, auto_zero));

	if (ret)
		dev_err(&indio_dev->dev, "Failed to update CONFIG2 register\n");
	else
		adc->auto_zeroing_mux = auto_zero;

	mutex_unlock(&adc->lock);

	return ret ? ret : len;
}

static ssize_t mcp3564_auto_zeroing_ref_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct mcp3564_state *adc = iio_priv(indio_dev);

	return sysfs_emit(buf, "%d\n", adc->auto_zeroing_ref);
}

static ssize_t mcp3564_auto_zeroing_ref_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct mcp3564_state *adc = iio_priv(indio_dev);
	bool auto_zero;
	int ret;

	ret = kstrtobool(buf, &auto_zero);
	if (ret)
		return ret;

	mutex_lock(&adc->lock);
	ret = mcp3564_update_8bits(adc, MCP3564_CONFIG2_REG, MCP3564_CONFIG2_AZ_REF_MASK,
				   FIELD_PREP(MCP3564_CONFIG2_AZ_REF_MASK, auto_zero));

	if (ret)
		dev_err(&indio_dev->dev, "Failed to update CONFIG2 register\n");
	else
		adc->auto_zeroing_ref = auto_zero;

	mutex_unlock(&adc->lock);

	return ret ? ret : len;
}

static const struct iio_chan_spec mcp3564_channel_template = {
	.type = IIO_VOLTAGE,
	.indexed = 1,
	.differential = 1,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	.info_mask_shared_by_all  = BIT(IIO_CHAN_INFO_SCALE)		|
				BIT(IIO_CHAN_INFO_CALIBSCALE)		|
				BIT(IIO_CHAN_INFO_CALIBBIAS)		|
				BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
	.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_SCALE)	|
				BIT(IIO_CHAN_INFO_CALIBSCALE) |
				BIT(IIO_CHAN_INFO_CALIBBIAS)		|
				BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
	.ext_info = mcp3564_ext_info,
};

static const struct iio_chan_spec mcp3564_temp_channel_template = {
	.type = IIO_TEMP,
	.channel = 0,
	.address = ((MCP3564_TEMP_DIODE_P << 4) | MCP3564_TEMP_DIODE_M),
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	.info_mask_shared_by_all  = BIT(IIO_CHAN_INFO_SCALE)		|
			BIT(IIO_CHAN_INFO_CALIBSCALE)			|
			BIT(IIO_CHAN_INFO_CALIBBIAS)			|
			BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
	.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_SCALE)	|
			BIT(IIO_CHAN_INFO_CALIBSCALE) |
			BIT(IIO_CHAN_INFO_CALIBBIAS)			|
			BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
};

static const struct iio_chan_spec mcp3564_burnout_channel_template = {
	.type = IIO_CURRENT,
	.output = true,
	.channel = 0,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	.info_mask_separate_available = BIT(IIO_CHAN_INFO_RAW),
};

/*
 * Number of channels could be calculated:
 * num_channels = single_ended_input + differential_input + temperature + burnout
 * Eg. for MCP3561 (only 2 channels available: CH0 and CH1)
 * single_ended_input = (CH0 - GND), (CH1 -  GND) = 2
 * differential_input = (CH0 - CH1), (CH0 -  CH0) = 2
 * num_channels = 2 + 2 + 2
 * Generic formula is:
 * num_channels = P^R(Number_of_single_ended_channels, 2) + 2 (temperature + burnout channels)
 * P^R(Number_of_single_ended_channels, 2) is Permutations with Replacement of
 *     Number_of_single_ended_channels taken by 2
 */
static const struct mcp3564_chip_info mcp3564_chip_infos_tbl[] = {
	[mcp3461] = {
		.name = "mcp3461",
		.num_channels = 6,
		.resolution = 16,
		.have_vref = false,
	},
	[mcp3462] = {
		.name = "mcp3462",
		.num_channels = 18,
		.resolution = 16,
		.have_vref = false,
	},
	[mcp3464] = {
		.name = "mcp3464",
		.num_channels = 66,
		.resolution = 16,
		.have_vref = false,
	},
	[mcp3561] = {
		.name = "mcp3561",
		.num_channels = 6,
		.resolution = 24,
		.have_vref = false,
	},
	[mcp3562] = {
		.name = "mcp3562",
		.num_channels = 18,
		.resolution = 24,
		.have_vref = false,
	},
	[mcp3564] = {
		.name = "mcp3564",
		.num_channels = 66,
		.resolution = 24,
		.have_vref = false,
	},
	[mcp3461r] = {
		.name = "mcp3461r",
		.num_channels = 6,
		.resolution = 16,
		.have_vref = false,
	},
	[mcp3462r] = {
		.name = "mcp3462r",
		.num_channels = 18,
		.resolution = 16,
		.have_vref = true,
	},
	[mcp3464r] = {
		.name = "mcp3464r",
		.num_channels = 66,
		.resolution = 16,
		.have_vref = true,
	},
	[mcp3561r] = {
		.name = "mcp3561r",
		.num_channels = 6,
		.resolution = 24,
		.have_vref = true,
	},
	[mcp3562r] = {
		.name = "mcp3562r",
		.num_channels = 18,
		.resolution = 24,
		.have_vref = true,
	},
	[mcp3564r] = {
		.name = "mcp3564r",
		.num_channels = 66,
		.resolution = 24,
		.have_vref = true,
	},
};

static int mcp3564_read_single_value(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *channel,
				     int *val)
{
	struct mcp3564_state *adc = iio_priv(indio_dev);
	int ret;
	u8 tmp;
	int ret_read = 0;

	ret = mcp3564_write_8bits(adc, MCP3564_MUX_REG, channel->address);
	if (ret)
		return ret;

	/* Start ADC Conversion using fast command (overwrites ADC_MODE[1:0] = 11) */
	ret = mcp3564_fast_cmd(adc, MCP3564_FASTCMD_START);
	if (ret)
		return ret;

	/*
	 * Check if the conversion is ready. If not, wait a little bit, and
	 * in case of timeout exit with an error.
	 */
	ret = read_poll_timeout(mcp3564_read_8bits, ret_read,
				ret_read || !(tmp & MCP3564_DATA_READY_MASK),
				20000, MCP3564_DATA_READY_TIMEOUT_MS * 1000, true,
				adc, MCP3564_IRQ_REG, &tmp);

	/* failed to read status register */
	if (ret_read)
		return ret_read;

	if (ret)
		return ret;

	if (tmp & MCP3564_DATA_READY_MASK)
		/* failing to finish conversion */
		return -EBUSY;

	return mcp3564_read_32bits(adc, MCP3564_ADCDATA_REG, val);
}

static int mcp3564_read_avail(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *channel,
			      const int **vals, int *type,
			      int *length, long mask)
{
	struct mcp3564_state *adc = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (!channel->output)
			return -EINVAL;

		*vals = mcp3564_burnout_avail[0];
		*length = ARRAY_SIZE(mcp3564_burnout_avail) * 2;
		*type = IIO_VAL_INT_PLUS_MICRO;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*vals = mcp3564_oversampling_avail;
		*length = ARRAY_SIZE(mcp3564_oversampling_avail);
		*type = IIO_VAL_INT;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SCALE:
		*vals = (int *)adc->scale_tbls;
		*length = ARRAY_SIZE(adc->scale_tbls) * 2;
		*type = IIO_VAL_INT_PLUS_NANO;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_CALIBBIAS:
		*vals = mcp3564_calib_bias;
		*type = IIO_VAL_INT;
		return IIO_AVAIL_RANGE;
	case IIO_CHAN_INFO_CALIBSCALE:
		*vals = mcp3564_calib_scale;
		*type = IIO_VAL_INT;
		return IIO_AVAIL_RANGE;
	default:
		return -EINVAL;
	}
}

static int mcp3564_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *channel,
			    int *val, int *val2, long mask)
{
	struct mcp3564_state *adc = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (channel->output) {
			mutex_lock(&adc->lock);
			*val = mcp3564_burnout_avail[adc->burnout_mode][0];
			*val2 = mcp3564_burnout_avail[adc->burnout_mode][1];
			mutex_unlock(&adc->lock);
			return IIO_VAL_INT_PLUS_MICRO;
		}

		ret = mcp3564_read_single_value(indio_dev, channel, val);
		if (ret)
			return -EINVAL;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		mutex_lock(&adc->lock);
		*val = adc->scale_tbls[adc->hwgain][0];
		*val2 = adc->scale_tbls[adc->hwgain][1];
		mutex_unlock(&adc->lock);
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*val = mcp3564_oversampling_avail[adc->oversampling];
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBBIAS:
		*val = adc->calib_bias;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBSCALE:
		*val = adc->calib_scale;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int mcp3564_write_raw_get_fmt(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan,
				     long info)
{
	switch (info) {
	case IIO_CHAN_INFO_RAW:
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_CALIBBIAS:
	case IIO_CHAN_INFO_CALIBSCALE:
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return -EINVAL;
	}
}

static int mcp3564_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *channel, int val,
			     int val2, long mask)
{
	struct mcp3564_state *adc = iio_priv(indio_dev);
	int tmp;
	unsigned int hwgain;
	enum mcp3564_burnout burnout;
	int ret = 0;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (!channel->output)
			return -EINVAL;

		for (burnout = 0; burnout < MCP3564_MAX_BURNOUT_IDX; burnout++)
			if (val == mcp3564_burnout_avail[burnout][0] &&
			    val2 == mcp3564_burnout_avail[burnout][1])
				break;

		if (burnout == MCP3564_MAX_BURNOUT_IDX)
			return -EINVAL;

		if (burnout == adc->burnout_mode)
			return ret;

		mutex_lock(&adc->lock);
		ret = mcp3564_update_8bits(adc, MCP3564_CONFIG0_REG,
					   MCP3564_CONFIG0_CS_SEL_MASK,
					   FIELD_PREP(MCP3564_CONFIG0_CS_SEL_MASK, burnout));

		if (ret)
			dev_err(&indio_dev->dev, "Failed to configure burnout current\n");
		else
			adc->burnout_mode = burnout;
		mutex_unlock(&adc->lock);
		return ret;
	case IIO_CHAN_INFO_CALIBBIAS:
		if (val < mcp3564_calib_bias[0] || val > mcp3564_calib_bias[2])
			return -EINVAL;

		mutex_lock(&adc->lock);
		ret = mcp3564_write_24bits(adc, MCP3564_OFFSETCAL_REG, val);
		if (!ret)
			adc->calib_bias = val;
		mutex_unlock(&adc->lock);
		return ret;
	case IIO_CHAN_INFO_CALIBSCALE:
		if (val < mcp3564_calib_scale[0] || val > mcp3564_calib_scale[2])
			return -EINVAL;

		if (adc->calib_scale == val)
			return ret;

		mutex_lock(&adc->lock);
		ret = mcp3564_write_24bits(adc, MCP3564_GAINCAL_REG, val);
		if (!ret)
			adc->calib_scale = val;
		mutex_unlock(&adc->lock);
		return ret;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		if (val < 0)
			return -EINVAL;

		tmp = find_closest(val, mcp3564_oversampling_avail,
				   ARRAY_SIZE(mcp3564_oversampling_avail));

		if (adc->oversampling == tmp)
			return ret;

		mutex_lock(&adc->lock);
		ret = mcp3564_update_8bits(adc, MCP3564_CONFIG1_REG,
					   MCP3564_CONFIG1_OVERSPL_RATIO_MASK,
					   FIELD_PREP(MCP3564_CONFIG1_OVERSPL_RATIO_MASK,
						      adc->oversampling));
		if (!ret)
			adc->oversampling = tmp;
		mutex_unlock(&adc->lock);
		return ret;
	case IIO_CHAN_INFO_SCALE:
		for (hwgain = 0; hwgain < MCP3564_MAX_PGA; hwgain++)
			if (val == adc->scale_tbls[hwgain][0] &&
			    val2 == adc->scale_tbls[hwgain][1])
				break;

		if (hwgain == MCP3564_MAX_PGA)
			return -EINVAL;

		if (hwgain == adc->hwgain)
			return ret;

		mutex_lock(&adc->lock);
		ret = mcp3564_update_8bits(adc, MCP3564_CONFIG2_REG,
					   MCP3564_CONFIG2_HARDWARE_GAIN_MASK,
					   FIELD_PREP(MCP3564_CONFIG2_HARDWARE_GAIN_MASK, hwgain));
		if (!ret)
			adc->hwgain = hwgain;

		mutex_unlock(&adc->lock);
		return ret;
	default:
		return -EINVAL;
	}
}

static int mcp3564_read_label(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan, char *label)
{
	struct mcp3564_state *adc = iio_priv(indio_dev);

	return sprintf(label, "%s\n", adc->labels[chan->scan_index]);
}

static int mcp3564_parse_fw_children(struct iio_dev *indio_dev)
{
	struct mcp3564_state *adc = iio_priv(indio_dev);
	struct device *dev = &adc->spi->dev;
	struct iio_chan_spec *channels;
	struct fwnode_handle *child;
	struct iio_chan_spec chanspec = mcp3564_channel_template;
	struct iio_chan_spec temp_chanspec = mcp3564_temp_channel_template;
	struct iio_chan_spec burnout_chanspec = mcp3564_burnout_channel_template;
	int chan_idx = 0;
	unsigned int num_ch;
	u32 inputs[2];
	const char *node_name;
	const char *label;
	int ret;

	num_ch = device_get_child_node_count(dev);
	if (num_ch == 0)
		return dev_err_probe(&indio_dev->dev, -ENODEV,
				     "FW has no channels defined\n");

	/* Reserve space for burnout and temperature channel */
	num_ch += 2;

	if (num_ch > adc->chip_info->num_channels)
		return dev_err_probe(dev, -EINVAL, "Too many channels %d > %d\n",
				     num_ch, adc->chip_info->num_channels);

	channels = devm_kcalloc(dev, num_ch, sizeof(*channels), GFP_KERNEL);
	if (!channels)
		return dev_err_probe(dev, -ENOMEM, "Can't allocate memory\n");

	device_for_each_child_node(dev, child) {
		node_name = fwnode_get_name(child);

		if (fwnode_property_present(child, "diff-channels")) {
			ret = fwnode_property_read_u32_array(child,
							     "diff-channels",
							     inputs,
							     ARRAY_SIZE(inputs));
			chanspec.differential = 1;
		} else {
			ret = fwnode_property_read_u32(child, "reg", &inputs[0]);

			chanspec.differential = 0;
			inputs[1] = MCP3564_AGND;
		}
		if (ret) {
			fwnode_handle_put(child);
			return ret;
		}

		if (inputs[0] > MCP3564_INTERNAL_VCM ||
		    inputs[1] > MCP3564_INTERNAL_VCM) {
			fwnode_handle_put(child);
			return dev_err_probe(&indio_dev->dev, -EINVAL,
					     "Channel index > %d, for %s\n",
					     MCP3564_INTERNAL_VCM + 1,
					     node_name);
		}

		chanspec.address = (inputs[0] << 4) | inputs[1];
		chanspec.channel = inputs[0];
		chanspec.channel2 = inputs[1];
		chanspec.scan_index = chan_idx;

		if (fwnode_property_present(child, "label")) {
			fwnode_property_read_string(child, "label", &label);
			adc->labels[chan_idx] = label;
		}

		channels[chan_idx] = chanspec;
		chan_idx++;
	}

	/* Add burnout current channel */
	burnout_chanspec.scan_index = chan_idx;
	channels[chan_idx] = burnout_chanspec;
	adc->labels[chan_idx] = mcp3564_channel_labels[0];
	chanspec.scan_index = chan_idx;
	chan_idx++;

	/* Add temperature channel */
	temp_chanspec.scan_index = chan_idx;
	channels[chan_idx] = temp_chanspec;
	adc->labels[chan_idx] = mcp3564_channel_labels[1];
	chan_idx++;

	indio_dev->num_channels = chan_idx;
	indio_dev->channels = channels;

	return 0;
}

static void mcp3564_disable_reg(void *reg)
{
	regulator_disable(reg);
}

static void mcp3564_fill_scale_tbls(struct mcp3564_state *adc)
{
	unsigned int pow = adc->chip_info->resolution - 1;
	int ref;
	unsigned int i;
	int tmp0;
	u64 tmp1;

	for (i = 0; i < MCP3564_MAX_PGA; i++) {
		ref = adc->vref_mv;
		tmp1 = ((u64)ref * NANO) >> pow;
		div_u64_rem(tmp1, NANO, &tmp0);

		tmp1 = tmp1 * mcp3564_hwgain_frac[(2 * i) + 1];
		tmp0 = (int)div_u64(tmp1, mcp3564_hwgain_frac[2 * i]);

		adc->scale_tbls[i][1] = tmp0;
	}
}

static int mcp3564_config(struct iio_dev *indio_dev)
{
	struct mcp3564_state *adc = iio_priv(indio_dev);
	struct device *dev = &adc->spi->dev;
	const struct spi_device_id *dev_id;
	u8 tmp_reg;
	u16 tmp_u16;
	enum mcp3564_ids ids;
	int ret = 0;
	unsigned int tmp = 0x01;
	bool err = false;

	/*
	 * The address is set on a per-device basis by fuses in the factory,
	 * configured on request. If not requested, the fuses are set for 0x1.
	 * The device address is part of the device markings to avoid
	 * potential confusion. This address is coded on two bits, so four possible
	 * addresses are available when multiple devices are present on the same
	 * SPI bus with only one Chip Select line for all devices.
	 */
	device_property_read_u32(dev, "microchip,hw-device-address", &tmp);

	if (tmp > 3) {
		dev_err_probe(dev, tmp,
			      "invalid device address. Must be in range 0-3.\n");
		return -EINVAL;
	}

	adc->dev_addr = FIELD_GET(MCP3564_HW_ADDR_MASK, tmp);

	dev_dbg(dev, "use HW device address %i\n", adc->dev_addr);

	ret = mcp3564_read_8bits(adc, MCP3564_RESERVED_C_REG, &tmp_reg);
	if (ret < 0)
		return ret;

	switch (tmp_reg) {
	case MCP3564_C_REG_DEFAULT:
		adc->have_vref = false;
		break;
	case MCP3564R_C_REG_DEFAULT:
		adc->have_vref = true;
		break;
	default:
		dev_info(dev, "Unknown chip found: %d\n", tmp_reg);
		err = true;
	}

	if (!err) {
		ret = mcp3564_read_16bits(adc, MCP3564_RESERVED_E_REG, &tmp_u16);
		if (ret < 0)
			return ret;

		switch (tmp_u16 & MCP3564_HW_ID_MASK) {
		case MCP3461_HW_ID:
			if (adc->have_vref)
				ids = mcp3461r;
			else
				ids = mcp3461;
			break;
		case MCP3462_HW_ID:
			if (adc->have_vref)
				ids = mcp3462r;
			else
				ids = mcp3462;
			break;
		case MCP3464_HW_ID:
			if (adc->have_vref)
				ids = mcp3464r;
			else
				ids = mcp3464;
			break;
		case MCP3561_HW_ID:
			if (adc->have_vref)
				ids = mcp3561r;
			else
				ids = mcp3561;
			break;
		case MCP3562_HW_ID:
			if (adc->have_vref)
				ids = mcp3562r;
			else
				ids = mcp3562;
			break;
		case MCP3564_HW_ID:
			if (adc->have_vref)
				ids = mcp3564r;
			else
				ids = mcp3564;
			break;
		default:
			dev_info(dev, "Unknown chip found: %d\n", tmp_u16);
			err = true;
		}
	}

	if (err) {
		/*
		 * If failed to identify the hardware based on internal registers,
		 * try using fallback compatible in device tree to deal with some newer part number.
		 */
		adc->chip_info = spi_get_device_match_data(adc->spi);
		if (!adc->chip_info) {
			dev_id = spi_get_device_id(adc->spi);
			adc->chip_info = (const struct mcp3564_chip_info *)dev_id->driver_data;
		}

		adc->have_vref = adc->chip_info->have_vref;
	} else {
		adc->chip_info = &mcp3564_chip_infos_tbl[ids];
	}

	dev_dbg(dev, "Found %s chip\n", adc->chip_info->name);

	adc->vref = devm_regulator_get_optional(dev, "vref");
	if (IS_ERR(adc->vref)) {
		if (PTR_ERR(adc->vref) != -ENODEV)
			return dev_err_probe(dev, PTR_ERR(adc->vref),
					     "failed to get regulator\n");

		/* Check if chip has internal vref */
		if (!adc->have_vref)
			return dev_err_probe(dev, PTR_ERR(adc->vref),
					     "Unknown Vref\n");
		adc->vref = NULL;
		dev_dbg(dev, "%s: Using internal Vref\n", __func__);
	} else {
		ret = regulator_enable(adc->vref);
		if (ret)
			return ret;

		ret = devm_add_action_or_reset(dev, mcp3564_disable_reg,
					       adc->vref);
		if (ret)
			return ret;

		dev_dbg(dev, "%s: Using External Vref\n", __func__);

		ret = regulator_get_voltage(adc->vref);
		if (ret < 0)
			return dev_err_probe(dev, ret,
					     "Failed to read vref regulator\n");

		adc->vref_mv = ret / MILLI;
	}

	ret = mcp3564_parse_fw_children(indio_dev);
	if (ret)
		return ret;

	/*
	 * Command sequence that ensures a recovery with the desired settings
	 * in any cases of loss-of-power scenario (Full Chip Reset):
	 *  - Write LOCK register to 0xA5
	 *  - Write IRQ register to 0x03
	 *  - Send "Device Full Reset" fast command
	 *  - Wait 1ms for "Full Reset" to complete
	 */
	ret = mcp3564_write_8bits(adc, MCP3564_LOCK_REG, MCP3564_LOCK_WRITE_ACCESS_PASSWORD);
	if (ret)
		return ret;

	ret = mcp3564_write_8bits(adc, MCP3564_IRQ_REG, 0x03);
	if (ret)
		return ret;

	ret = mcp3564_fast_cmd(adc, MCP3564_FASTCMD_RESET);
	if (ret)
		return ret;

	/*
	 * After Full reset wait some time to be able to fully reset the part and place
	 * it back in a default configuration.
	 * From datasheet: POR (Power On Reset Time) is ~1us
	 * 1ms should be enough.
	 */
	mdelay(1);

	/* set a gain of 1x for GAINCAL */
	ret = mcp3564_write_24bits(adc, MCP3564_GAINCAL_REG, MCP3564_DEFAULT_GAINCAL);
	if (ret)
		return ret;

	adc->calib_scale = MCP3564_DEFAULT_GAINCAL;

	ret = mcp3564_write_24bits(adc, MCP3564_OFFSETCAL_REG, MCP3564_DEFAULT_OFFSETCAL);
	if (ret)
		return ret;

	ret = mcp3564_write_24bits(adc, MCP3564_TIMER_REG, MCP3564_TIMER_DEFAULT_VALUE);
	if (ret)
		return ret;

	ret = mcp3564_write_24bits(adc, MCP3564_SCAN_REG,
				   MCP3564_SCAN_DELAY_TIME_SET(MCP3564_NO_DELAY) |
				   MCP3564_SCAN_CH_SEL_SET(MCP3564_SCAN_DEFAULT_VALUE));
	if (ret)
		return ret;

	ret = mcp3564_write_8bits(adc, MCP3564_MUX_REG, MCP3564_MUX_SET(MCP3564_CH0, MCP3564_CH1));
	if (ret)
		return ret;

	ret = mcp3564_write_8bits(adc, MCP3564_IRQ_REG,
				  FIELD_PREP(MCP3464_EN_FASTCMD_MASK, 1) |
				  FIELD_PREP(MCP3464_EN_STP_MASK, 1));
	if (ret)
		return ret;

	tmp_reg = FIELD_PREP(MCP3464_CONFIG3_CONV_MODE_MASK,
			     MCP3464_CONFIG3_CONV_MODE_ONE_SHOT_STANDBY);
	tmp_reg |= FIELD_PREP(MCP3464_CONFIG3_DATA_FORMAT_MASK,
			      MCP3464_CONFIG3_DATA_FMT_32B_SGN_EXT);
	tmp_reg |= MCP3464_CONFIG3_EN_OFFCAL_MASK;
	tmp_reg |= MCP3464_CONFIG3_EN_GAINCAL_MASK;

	ret = mcp3564_write_8bits(adc, MCP3564_CONFIG3_REG, tmp_reg);
	if (ret)
		return ret;

	tmp_reg = FIELD_PREP(MCP3564_CONFIG2_BOOST_CURRENT_MASK, MCP3564_BOOST_CURRENT_x1_00);
	tmp_reg |= FIELD_PREP(MCP3564_CONFIG2_HARDWARE_GAIN_MASK, 0x01);
	tmp_reg |= FIELD_PREP(MCP3564_CONFIG2_AZ_MUX_MASK, 1);

	ret = mcp3564_write_8bits(adc, MCP3564_CONFIG2_REG, tmp_reg);
	if (ret)
		return ret;

	adc->hwgain = 0x01;
	adc->auto_zeroing_mux = true;
	adc->auto_zeroing_ref = false;
	adc->current_boost_mode = MCP3564_BOOST_CURRENT_x1_00;

	tmp_reg = FIELD_PREP(MCP3564_CONFIG1_OVERSPL_RATIO_MASK, MCP3564_OVERSAMPLING_RATIO_98304);

	ret = mcp3564_write_8bits(adc, MCP3564_CONFIG1_REG, tmp_reg);
	if (ret)
		return ret;

	adc->oversampling = MCP3564_OVERSAMPLING_RATIO_98304;

	tmp_reg = FIELD_PREP(MCP3564_CONFIG0_ADC_MODE_MASK, MCP3564_ADC_MODE_STANDBY);
	tmp_reg |= FIELD_PREP(MCP3564_CONFIG0_CS_SEL_MASK, MCP3564_CONFIG0_CS_SEL_0_0_uA);
	tmp_reg |= FIELD_PREP(MCP3564_CONFIG0_CLK_SEL_MASK, MCP3564_CONFIG0_USE_INT_CLK);
	tmp_reg |= MCP3456_CONFIG0_BIT6_DEFAULT;

	if (!adc->vref) {
		tmp_reg |= FIELD_PREP(MCP3456_CONFIG0_VREF_MASK, 1);
		adc->vref_mv = MCP3564R_INT_VREF_MV;
	}

	ret = mcp3564_write_8bits(adc, MCP3564_CONFIG0_REG, tmp_reg);

	adc->burnout_mode = MCP3564_CONFIG0_CS_SEL_0_0_uA;

	return ret;
}

static IIO_DEVICE_ATTR(auto_zeroing_ref_enable, 0644,
		       mcp3564_auto_zeroing_ref_show,
		       mcp3564_auto_zeroing_ref_store, 0);

static IIO_DEVICE_ATTR(auto_zeroing_mux_enable, 0644,
		       mcp3564_auto_zeroing_mux_show,
		       mcp3564_auto_zeroing_mux_store, 0);

static struct attribute *mcp3564_attributes[] = {
	&iio_dev_attr_auto_zeroing_mux_enable.dev_attr.attr,
	NULL
};

static struct attribute *mcp3564r_attributes[] = {
	&iio_dev_attr_auto_zeroing_mux_enable.dev_attr.attr,
	&iio_dev_attr_auto_zeroing_ref_enable.dev_attr.attr,
	NULL
};

static struct attribute_group mcp3564_attribute_group = {
	.attrs = mcp3564_attributes,
};

static struct attribute_group mcp3564r_attribute_group = {
	.attrs = mcp3564r_attributes,
};

static const struct iio_info mcp3564_info = {
	.read_raw = mcp3564_read_raw,
	.read_avail = mcp3564_read_avail,
	.write_raw = mcp3564_write_raw,
	.write_raw_get_fmt = mcp3564_write_raw_get_fmt,
	.read_label = mcp3564_read_label,
	.attrs = &mcp3564_attribute_group,
};

static const struct iio_info mcp3564r_info = {
	.read_raw = mcp3564_read_raw,
	.read_avail = mcp3564_read_avail,
	.write_raw = mcp3564_write_raw,
	.write_raw_get_fmt = mcp3564_write_raw_get_fmt,
	.read_label = mcp3564_read_label,
	.attrs = &mcp3564r_attribute_group,
};

static int mcp3564_probe(struct spi_device *spi)
{
	int ret;
	struct iio_dev *indio_dev;
	struct mcp3564_state *adc;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->spi = spi;

	dev_dbg(&spi->dev, "%s: probe(spi = 0x%p)\n", __func__, spi);

	/*
	 * Do any chip specific initialization, e.g:
	 * read/write some registers
	 * enable/disable certain channels
	 * change the sampling rate to the requested value
	 */
	ret = mcp3564_config(indio_dev);
	if (ret)
		return dev_err_probe(&spi->dev, ret,
				     "Can't configure MCP356X device\n");

	dev_dbg(&spi->dev, "%s: Vref (mV): %d\n", __func__, adc->vref_mv);

	mcp3564_fill_scale_tbls(adc);

	indio_dev->name = adc->chip_info->name;
	indio_dev->modes = INDIO_DIRECT_MODE;

	if (!adc->vref)
		indio_dev->info = &mcp3564r_info;
	else
		indio_dev->info = &mcp3564_info;

	mutex_init(&adc->lock);

	ret = devm_iio_device_register(&spi->dev, indio_dev);
	if (ret)
		return dev_err_probe(&spi->dev, ret,
				     "Can't register IIO device\n");

	return 0;
}

static const struct of_device_id mcp3564_dt_ids[] = {
	{ .compatible = "microchip,mcp3461", .data = &mcp3564_chip_infos_tbl[mcp3461] },
	{ .compatible = "microchip,mcp3462", .data = &mcp3564_chip_infos_tbl[mcp3462] },
	{ .compatible = "microchip,mcp3464", .data = &mcp3564_chip_infos_tbl[mcp3464] },
	{ .compatible = "microchip,mcp3561", .data = &mcp3564_chip_infos_tbl[mcp3561] },
	{ .compatible = "microchip,mcp3562", .data = &mcp3564_chip_infos_tbl[mcp3562] },
	{ .compatible = "microchip,mcp3564", .data = &mcp3564_chip_infos_tbl[mcp3564] },
	{ .compatible = "microchip,mcp3461r", .data = &mcp3564_chip_infos_tbl[mcp3461r] },
	{ .compatible = "microchip,mcp3462r", .data = &mcp3564_chip_infos_tbl[mcp3462r] },
	{ .compatible = "microchip,mcp3464r", .data = &mcp3564_chip_infos_tbl[mcp3464r] },
	{ .compatible = "microchip,mcp3561r", .data = &mcp3564_chip_infos_tbl[mcp3561r] },
	{ .compatible = "microchip,mcp3562r", .data = &mcp3564_chip_infos_tbl[mcp3562r] },
	{ .compatible = "microchip,mcp3564r", .data = &mcp3564_chip_infos_tbl[mcp3564r] },
	{ }
};
MODULE_DEVICE_TABLE(of, mcp3564_dt_ids);

static const struct spi_device_id mcp3564_id[] = {
	{ "mcp3461", (kernel_ulong_t)&mcp3564_chip_infos_tbl[mcp3461] },
	{ "mcp3462", (kernel_ulong_t)&mcp3564_chip_infos_tbl[mcp3462] },
	{ "mcp3464", (kernel_ulong_t)&mcp3564_chip_infos_tbl[mcp3464] },
	{ "mcp3561", (kernel_ulong_t)&mcp3564_chip_infos_tbl[mcp3561] },
	{ "mcp3562", (kernel_ulong_t)&mcp3564_chip_infos_tbl[mcp3562] },
	{ "mcp3564", (kernel_ulong_t)&mcp3564_chip_infos_tbl[mcp3564] },
	{ "mcp3461r", (kernel_ulong_t)&mcp3564_chip_infos_tbl[mcp3461r] },
	{ "mcp3462r", (kernel_ulong_t)&mcp3564_chip_infos_tbl[mcp3462r] },
	{ "mcp3464r", (kernel_ulong_t)&mcp3564_chip_infos_tbl[mcp3464r] },
	{ "mcp3561r", (kernel_ulong_t)&mcp3564_chip_infos_tbl[mcp3561r] },
	{ "mcp3562r", (kernel_ulong_t)&mcp3564_chip_infos_tbl[mcp3562r] },
	{ "mcp3564r", (kernel_ulong_t)&mcp3564_chip_infos_tbl[mcp3564r] },
	{ }
};
MODULE_DEVICE_TABLE(spi, mcp3564_id);

static struct spi_driver mcp3564_driver = {
	.driver = {
		.name = "mcp3564",
		.of_match_table = mcp3564_dt_ids,
	},
	.probe = mcp3564_probe,
	.id_table = mcp3564_id,
};

module_spi_driver(mcp3564_driver);

MODULE_AUTHOR("Marius Cristea <marius.cristea@microchip.com>");
MODULE_DESCRIPTION("Microchip MCP346x/MCP346xR and MCP356x/MCP356xR ADCs");
MODULE_LICENSE("GPL v2");
