// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Texas Instruments ADS131M02 family ADC chips.
 *
 * Copyright (C) 2024 Protonic Holland
 * Copyright (C) 2025 Oleksij Rempel <kernel@pengutronix.de>, Pengutronix
 *
 * Primary Datasheet Reference (used for citations):
 * ADS131M08 8-Channel, Simultaneously-Sampling, 24-Bit, Delta-Sigma ADC
 * Document SBAS950B, Revised February 2021
 * https://www.ti.com/lit/ds/symlink/ads131m08.pdf
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/crc-itu-t.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/device/devres.h>
#include <linux/err.h>
#include <linux/iio/iio.h>
#include <linux/lockdep.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/spi/spi.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/unaligned.h>

/* Max channels supported by the largest variant in the family (ADS131M08) */
#define ADS131M_MAX_CHANNELS		8

/* Section 6.7, t_REGACQ (min time after reset) is 5us */
#define ADS131M_RESET_DELAY_US		5

#define ADS131M_WORD_SIZE_BYTES		3
#define ADS131M_RESPONSE_WORDS		1
#define ADS131M_CRC_WORDS		1

/*
 * SPI Frame word count calculation.
 * Frame = N channel words + 1 response word + 1 CRC word.
 * Word size depends on WLENGTH bits in MODE register (Default 24-bit).
 */
#define ADS131M_FRAME_WORDS(nch) \
	((nch) + ADS131M_RESPONSE_WORDS + ADS131M_CRC_WORDS)

/*
 * SPI Frame byte size calculation.
 * Assumes default word size of 24 bits (3 bytes).
 */
#define ADS131M_FRAME_BYTES(nch) \
	(ADS131M_FRAME_WORDS(nch) * ADS131M_WORD_SIZE_BYTES)

/*
 * Index calculation for the start byte of channel 'x' data within the RX buffer.
 * Assumes 24-bit words (3 bytes per word).
 * The received frame starts with the response word (e.g., STATUS register
 * content when NULL command was sent), followed by data for channels 0 to N-1,
 * and finally the output CRC word.
 * Response = index 0..2, Chan0 = index 3..5, Chan1 = index 6..8, ...
 * Index for ChanX = 3 (response) + x * 3 (channel data size).
 */
#define ADS131M_CHANNEL_INDEX(x) \
	((x) * ADS131M_WORD_SIZE_BYTES + ADS131M_WORD_SIZE_BYTES)

#define ADS131M_CMD_NULL		0x0000
#define ADS131M_CMD_RESET		0x0011

#define ADS131M_CMD_ADDR_MASK		GENMASK(11, 7)
#define ADS131M_CMD_NUM_MASK		GENMASK(6, 0)

#define ADS131M_CMD_RREG_OP		0xa000
#define ADS131M_CMD_WREG_OP		0x6000

#define ADS131M_CMD_RREG(a, n) \
	(ADS131M_CMD_RREG_OP | \
	 FIELD_PREP(ADS131M_CMD_ADDR_MASK, a) | \
	 FIELD_PREP(ADS131M_CMD_NUM_MASK, n))
#define ADS131M_CMD_WREG(a, n) \
	(ADS131M_CMD_WREG_OP | \
	 FIELD_PREP(ADS131M_CMD_ADDR_MASK, a) | \
	 FIELD_PREP(ADS131M_CMD_NUM_MASK, n))

/*  STATUS Register (0x01h) bit definitions */
#define ADS131M_STATUS_CRC_ERR		BIT(12) /* Input CRC error */

#define ADS131M_REG_MODE		0x02
#define ADS131M_MODE_RX_CRC_EN		BIT(12) /* Enable Input CRC */
#define ADS131M_MODE_CRC_TYPE_ANSI	BIT(11) /* 0 = CCITT, 1 = ANSI */
#define ADS131M_MODE_RESET_FLAG		BIT(10)

#define ADS131M_REG_CLOCK		0x03
#define ADS131M_CLOCK_XTAL_DIS		BIT(7)
#define ADS131M_CLOCK_EXTREF_EN		BIT(6)

/* 1.2V internal reference, in millivolts, for IIO_VAL_FRACTIONAL_LOG2 */
#define ADS131M_VREF_INTERNAL_mV	1200
/* 24-bit resolution */
#define ADS131M_RESOLUTION_BITS		24
/* Signed data uses (RESOLUTION_BITS - 1) magnitude bits */
#define ADS131M_CODE_BITS              (ADS131M_RESOLUTION_BITS - 1)

/* External ref FSR = Vref * 0.96 */
#define ADS131M_EXTREF_SCALE_NUM	96
#define ADS131M_EXTREF_SCALE_DEN	100

struct ads131m_configuration {
	const struct iio_chan_spec *channels;
	const char *name;
	u16 reset_ack;
	u8 num_channels;
	u8 supports_extref:1;
	u8 supports_xtal:1;
};

struct ads131m_priv {
	struct iio_dev *indio_dev;
	struct spi_device *spi;
	const struct ads131m_configuration *config;

	bool use_external_ref;
	int scale_val;
	int scale_val2;

	struct spi_transfer xfer;
	struct spi_message msg;

	/*
	 * Protects the shared tx_buffer and rx_buffer. More importantly,
	 * this serializes all SPI communication to ensure the atomicity
	 * of multi-cycle command sequences (like WREG, RREG, or RESET).
	 */
	struct mutex lock;

	/* DMA-safe buffers should be placed at the end of the struct. */
	u8 tx_buffer[ADS131M_FRAME_BYTES(ADS131M_MAX_CHANNELS)]
		__aligned(IIO_DMA_MINALIGN);
	u8 rx_buffer[ADS131M_FRAME_BYTES(ADS131M_MAX_CHANNELS)];
};

/**
 * ads131m_tx_frame_unlocked - Sends a command frame with Input CRC
 * @priv: Device private data structure.
 * @command: The 16-bit command to send (e.g., NULL, RREG, RESET).
 *
 * This function sends a command in Word 0, and its calculated 16-bit
 * CRC in Word 1, as required when Input CRC is enabled.
 *
 * Return: 0 on success, or a negative error code.
 */
static int ads131m_tx_frame_unlocked(struct ads131m_priv *priv, u32 command)
{
	struct iio_dev *indio_dev = priv->indio_dev;
	u16 crc;

	lockdep_assert_held(&priv->lock);

	memset(priv->tx_buffer, 0, ADS131M_FRAME_BYTES(indio_dev->num_channels));

	/* Word 0: 16-bit command, MSB-aligned in 24-bit word */
	put_unaligned_be16(command, &priv->tx_buffer[0]);

	/* Word 1: Input CRC. Calculated over the 3 bytes of Word 0. */
	crc = crc_itu_t(0xffff, priv->tx_buffer, 3);
	put_unaligned_be16(crc, &priv->tx_buffer[3]);

	return spi_sync(priv->spi, &priv->msg);
}

/**
 * ads131m_rx_frame_unlocked - Receives a full SPI data frame.
 * @priv: Device private data structure.
 *
 * This function sends a NULL command (with its CRC) to clock out a
 * full SPI frame from the device (e.g., response + channel data + CRC).
 *
 * Return: 0 on success, or a negative error code.
 */
static int ads131m_rx_frame_unlocked(struct ads131m_priv *priv)
{
	return ads131m_tx_frame_unlocked(priv, ADS131M_CMD_NULL);
}

/**
 * ads131m_check_status_crc_err - Checks for an Input CRC error.
 * @priv: Device private data structure.
 *
 * Sends a NULL command to fetch the STATUS register and checks the
 * CRC_ERR bit. This is used to verify the integrity of the previous
 * command (like RREG or WREG).
 *
 * Return: 0 on success, -EIO if CRC_ERR bit is set.
 */
static int ads131m_check_status_crc_err(struct ads131m_priv *priv)
{
	struct device *dev = &priv->spi->dev;
	u16 status;
	int ret;

	lockdep_assert_held(&priv->lock);

	ret = ads131m_rx_frame_unlocked(priv);
	if (ret < 0) {
		dev_err_ratelimited(dev,
				    "SPI error on STATUS read for CRC check\n");
		return ret;
	}

	status = get_unaligned_be16(&priv->rx_buffer[0]);
	if (status & ADS131M_STATUS_CRC_ERR) {
		dev_err_ratelimited(dev,
				    "Input CRC error reported in STATUS = 0x%04x\n",
				    status);
		return -EIO;
	}

	return 0;
}

/**
 * ads131m_write_reg_unlocked - Writes a single register and verifies the ACK.
 * @priv: Device private data structure.
 * @reg: The 8-bit register address.
 * @val: The 16-bit value to write.
 *
 * This function performs the full 3-cycle WREG operation with Input CRC:
 * 1. (Cycle 1) Sends WREG command, data, and its calculated CRC.
 * 2. (Cycle 2) Sends NULL+CRC to retrieve the response from Cycle 1.
 * 3. Verifies the response is the correct ACK for the WREG.
 * 4. (Cycle 3) Sends NULL+CRC to retrieve STATUS and check for CRC_ERR.
 *
 * Return: 0 on success, or a negative error code.
 */
static int ads131m_write_reg_unlocked(struct ads131m_priv *priv, u8 reg, u16 val)
{
	struct iio_dev *indio_dev = priv->indio_dev;
	u16 command, expected_ack, response, crc;
	struct device *dev = &priv->spi->dev;
	int ret_crc_err = 0;
	int ret;

	lockdep_assert_held(&priv->lock);

	command = ADS131M_CMD_WREG(reg, 0); /* n = 0 for 1 register */
	/*
	 * Per Table 8-11, WREG response is: 010a aaaa ammm mmmm
	 * For 1 reg (n = 0 -> m = 0): 010a aaaa a000 0000 = 0x4000 | (reg << 7)
	 */
	expected_ack = 0x4000 | (reg << 7);

	/* Cycle 1: Send WREG Command + Data + Input CRC */

	memset(priv->tx_buffer, 0, ADS131M_FRAME_BYTES(indio_dev->num_channels));

	/* Word 0: WREG command, 1 reg (n = 0), MSB-aligned */
	put_unaligned_be16(command, &priv->tx_buffer[0]);

	/* Word 1: Data, MSB-aligned */
	put_unaligned_be16(val, &priv->tx_buffer[3]);

	/* Word 2: Input CRC. Calculated over Word 0 (Cmd) and Word 1 (Data). */
	crc = crc_itu_t(0xffff, priv->tx_buffer, 6);
	put_unaligned_be16(crc, &priv->tx_buffer[6]);

	/* Ignore the RX buffer (it's from the previous command) */
	ret = spi_sync(priv->spi, &priv->msg);
	if (ret < 0) {
		dev_err_ratelimited(dev, "SPI error on WREG (cycle 1)\n");
		return ret;
	}

	/* Cycle 2: Send NULL Command to get the WREG response */
	ret = ads131m_rx_frame_unlocked(priv);
	if (ret < 0) {
		dev_err_ratelimited(dev, "SPI error on WREG ACK (cycle 2)\n");
		return ret;
	}

	/*
	 * Response is in the first 2 bytes of the RX buffer
	 * (MSB-aligned 16-bit response)
	 */
	response = get_unaligned_be16(&priv->rx_buffer[0]);
	if (response != expected_ack) {
		dev_err_ratelimited(dev, "WREG(0x%02x) failed, expected ACK 0x%04x, got 0x%04x\n",
				    reg, expected_ack, response);
		ret_crc_err = -EIO;
		/*
		 * Don't return yet, still need to do Cycle 3 to clear
		 * any potential CRC_ERR flag from this failed command.
		 */
	}

	/*
	 * Cycle 3: Check STATUS for Input CRC error.
	 * This is necessary even if ACK was wrong, to clear the CRC_ERR flag.
	 */
	ret = ads131m_check_status_crc_err(priv);
	if (ret < 0)
		return ret;

	return ret_crc_err;
}

/**
 * ads131m_read_reg_unlocked - Reads a single register from the device.
 * @priv: Device private data structure.
 * @reg: The 8-bit register address.
 * @val: Pointer to store the 16-bit register value.
 *
 * This function performs the full 3-cycle RREG operation with Input CRC:
 * 1. (Cycle 1) Sends the RREG command + Input CRC.
 * 2. (Cycle 2) Sends NULL+CRC to retrieve the register data.
 * 3. (Cycle 3) Sends NULL+CRC to retrieve STATUS and check for CRC_ERR.
 *
 * Return: 0 on success, or a negative error code.
 */
static int ads131m_read_reg_unlocked(struct ads131m_priv *priv, u8 reg, u16 *val)
{
	struct device *dev = &priv->spi->dev;
	u16 command;
	int ret;

	lockdep_assert_held(&priv->lock);

	command = ADS131M_CMD_RREG(reg, 0); /* n=0 for 1 register */

	/*
	 * Cycle 1: Send RREG Command + Input CRC
	 * Ignore the RX buffer (it's from the previous command)
	 */
	ret = ads131m_tx_frame_unlocked(priv, command);
	if (ret < 0) {
		dev_err_ratelimited(dev, "SPI error on RREG (cycle 1)\n");
		return ret;
	}

	/* Cycle 2: Send NULL Command to get the register data */
	ret = ads131m_rx_frame_unlocked(priv);
	if (ret < 0) {
		dev_err_ratelimited(dev, "SPI error on RREG data (cycle 2)\n");
		return ret;
	}

	/*
	 * Per datasheet, for a single reg read, the response is the data.
	 * It's in the first 2 bytes of the RX buffer (MSB-aligned 16-bit).
	 */
	*val = get_unaligned_be16(&priv->rx_buffer[0]);

	/*
	 * Cycle 3: Check STATUS for Input CRC error.
	 * The RREG command does not execute if CRC is bad, but we read
	 * STATUS anyway to clear the flag in case it was set.
	 */
	return ads131m_check_status_crc_err(priv);
}

/**
 * ads131m_rmw_reg - Reads, modifies, and writes a single register.
 * @priv: Device private data structure.
 * @reg: The 8-bit register address.
 * @clear: Bitmask of bits to clear.
 * @set: Bitmask of bits to set.
 *
 * This function performs an atomic read-modify-write operation on a register.
 * It reads the register, applies the clear and set masks, and writes
 * the new value back if it has changed.
 *
 * Return: 0 on success, or a negative error code.
 */
static int ads131m_rmw_reg(struct ads131m_priv *priv, u8 reg, u16 clear, u16 set)
{
	u16 old_val, new_val;
	int ret;

	guard(mutex)(&priv->lock);

	ret = ads131m_read_reg_unlocked(priv, reg, &old_val);
	if (ret < 0)
		return ret;

	new_val = (old_val & ~clear) | set;
	if (new_val == old_val)
		return 0;

	return ads131m_write_reg_unlocked(priv, reg, new_val);
}

/**
 * ads131m_verify_output_crc - Verifies the CRC of the received SPI frame.
 * @priv: Device private data structure.
 *
 * This function calculates the CRC-16-CCITT (Poly 0x1021, Seed 0xFFFF) over
 * the received response and channel data, and compares it to the CRC word
 * received at the end of the SPI frame.
 *
 * Return: 0 on success, -EIO on CRC mismatch.
 */
static int ads131m_verify_output_crc(struct ads131m_priv *priv)
{
	struct iio_dev *indio_dev = priv->indio_dev;
	struct device *dev = &priv->spi->dev;
	u16 calculated_crc, received_crc;
	size_t data_len;

	lockdep_assert_held(&priv->lock);

	/*
	 * Frame: [Response][Chan 0]...[Chan N-1][CRC Word]
	 * Data for CRC: [Response][Chan 0]...[Chan N-1]
	 * Data length = (N_channels + 1) * 3 bytes (at 24-bit word size)
	 */
	data_len = ADS131M_FRAME_BYTES(indio_dev->num_channels) - 3;
	calculated_crc = crc_itu_t(0xffff, priv->rx_buffer, data_len);

	/*
	 * The received 16-bit CRC is MSB-aligned in the last 24-bit word.
	 * We extract it from the first 2 bytes (BE) of that word.
	 */
	received_crc = get_unaligned_be16(&priv->rx_buffer[data_len]);
	if (calculated_crc != received_crc) {
		dev_err_ratelimited(dev, "Output CRC error. Got %04x, expected %04x\n",
				    received_crc, calculated_crc);
		return -EIO;
	}

	return 0;
}

/**
 * ads131m_adc_read - Reads channel data, checks input and output CRCs.
 * @priv: Device private data structure.
 * @channel: The channel number to read.
 * @val: Pointer to store the raw 24-bit value.
 *
 * This function sends a NULL command (with Input CRC) to retrieve data.
 * It checks the received STATUS word for any Input CRC errors from the
 * previous command, and then verifies the Output CRC of the current
 * data frame.
 *
 * Return: 0 on success, or a negative error code.
 */
static int ads131m_adc_read(struct ads131m_priv *priv, u8 channel, s32 *val)
{
	struct device *dev = &priv->spi->dev;
	u16 status;
	int ret;
	u8 *buf;

	guard(mutex)(&priv->lock);

	/* Send NULL command + Input CRC, and receive data frame */
	ret = ads131m_rx_frame_unlocked(priv);
	if (ret < 0)
		return ret;

	/*
	 * Check STATUS for Input CRC error from the previous command frame.
	 * Note: the STATUS word belongs to the frame before this NULL command.
	 */
	status = get_unaligned_be16(&priv->rx_buffer[0]);
	if (status & ADS131M_STATUS_CRC_ERR) {
		dev_err_ratelimited(dev,
				    "Previous input CRC error reported in STATUS (0x%04x)\n",
				    status);
	}

	ret = ads131m_verify_output_crc(priv);
	if (ret < 0)
		return ret;

	buf = &priv->rx_buffer[ADS131M_CHANNEL_INDEX(channel)];
	*val = sign_extend32(get_unaligned_be24(buf), ADS131M_CODE_BITS);

	return 0;
}

static int ads131m_read_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *channel,
			    int *val, int *val2, long mask)
{
	struct ads131m_priv *priv = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = ads131m_adc_read(priv, channel->channel, val);
		if (ret)
			return ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = priv->scale_val;
		*val2 = priv->scale_val2;

		return IIO_VAL_FRACTIONAL;
	default:
		return -EINVAL;
	}
}

#define ADS131M_VOLTAGE_CHANNEL(num)	\
	{ \
		.type = IIO_VOLTAGE, \
		.differential = 1, \
		.indexed = 1, \
		.channel = (num), \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	}

static const struct iio_chan_spec ads131m02_channels[] = {
	ADS131M_VOLTAGE_CHANNEL(0),
	ADS131M_VOLTAGE_CHANNEL(1),
};

static const struct iio_chan_spec ads131m03_channels[] = {
	ADS131M_VOLTAGE_CHANNEL(0),
	ADS131M_VOLTAGE_CHANNEL(1),
	ADS131M_VOLTAGE_CHANNEL(2),
};

static const struct iio_chan_spec ads131m04_channels[] = {
	ADS131M_VOLTAGE_CHANNEL(0),
	ADS131M_VOLTAGE_CHANNEL(1),
	ADS131M_VOLTAGE_CHANNEL(2),
	ADS131M_VOLTAGE_CHANNEL(3),
};

static const struct iio_chan_spec ads131m06_channels[] = {
	ADS131M_VOLTAGE_CHANNEL(0),
	ADS131M_VOLTAGE_CHANNEL(1),
	ADS131M_VOLTAGE_CHANNEL(2),
	ADS131M_VOLTAGE_CHANNEL(3),
	ADS131M_VOLTAGE_CHANNEL(4),
	ADS131M_VOLTAGE_CHANNEL(5),
};

static const struct iio_chan_spec ads131m08_channels[] = {
	ADS131M_VOLTAGE_CHANNEL(0),
	ADS131M_VOLTAGE_CHANNEL(1),
	ADS131M_VOLTAGE_CHANNEL(2),
	ADS131M_VOLTAGE_CHANNEL(3),
	ADS131M_VOLTAGE_CHANNEL(4),
	ADS131M_VOLTAGE_CHANNEL(5),
	ADS131M_VOLTAGE_CHANNEL(6),
	ADS131M_VOLTAGE_CHANNEL(7),
};

static const struct ads131m_configuration ads131m02_config = {
	.channels = ads131m02_channels,
	.num_channels = ARRAY_SIZE(ads131m02_channels),
	.reset_ack = 0xff22,
	.name = "ads131m02",
};

static const struct ads131m_configuration ads131m03_config = {
	.channels = ads131m03_channels,
	.num_channels = ARRAY_SIZE(ads131m03_channels),
	.reset_ack = 0xff23,
	.name = "ads131m03",
};

static const struct ads131m_configuration ads131m04_config = {
	.channels = ads131m04_channels,
	.num_channels = ARRAY_SIZE(ads131m04_channels),
	.reset_ack = 0xff24,
	.name = "ads131m04",
};

static const struct ads131m_configuration ads131m06_config = {
	.channels = ads131m06_channels,
	.num_channels = ARRAY_SIZE(ads131m06_channels),
	.reset_ack = 0xff26,
	.supports_extref = true,
	.supports_xtal = true,
	.name = "ads131m06",
};

static const struct ads131m_configuration ads131m08_config = {
	.channels = ads131m08_channels,
	.num_channels = ARRAY_SIZE(ads131m08_channels),
	.reset_ack = 0xff28,
	.supports_extref = true,
	.supports_xtal = true,
	.name = "ads131m08",
};

static const struct iio_info ads131m_info = {
	.read_raw = ads131m_read_raw,
};

/*
 * Prepares the reusable SPI message structure for a full-duplex transfer.
 * The ADS131M requires sending a command frame while simultaneously
 * receiving the response/data frame from the previous command cycle.
 *
 * This message is optimized for the primary data acquisition workflow:
 * sending a single-word command (like NULL) and receiving a full data
 * frame (Response + N*Channels + CRC).
 *
 * This message is sized for a full data frame and is reused for all
 * command/data cycles. The driver does not implement variable-length SPI
 * messages.
 *
 * Return: 0 on success, or a negative error code.
 */
static int ads131m_prepare_message(struct ads131m_priv *priv)
{
	struct iio_dev *indio_dev = priv->indio_dev;
	struct device *dev = &priv->spi->dev;
	int ret;

	priv->xfer.tx_buf = priv->tx_buffer;
	priv->xfer.rx_buf = priv->rx_buffer;
	priv->xfer.len = ADS131M_FRAME_BYTES(indio_dev->num_channels);
	spi_message_init_with_transfers(&priv->msg, &priv->xfer, 1);

	ret = devm_spi_optimize_message(dev, priv->spi, &priv->msg);
	if (ret)
		return dev_err_probe(dev, ret, "failed to optimize SPI message\n");

	return 0;
}

/**
 * ads131m_hw_reset - Pulses the optional hardware reset.
 * @priv: Device private data structure.
 * @rstc: Reset control for the /RESET line.
 *
 * Pulses the /RESET line to perform a hardware reset and waits the
 * required t_REGACQ time for the device to be ready.
 *
 * Return: 0 on success, or a negative error code.
 */
static int ads131m_hw_reset(struct ads131m_priv *priv,
			    struct reset_control *rstc)
{
	struct device *dev = &priv->spi->dev;
	int ret;

	/*
	 * Manually pulse the reset line using the framework.
	 * The reset-gpio provider does not implement the .reset op,
	 * so we must use .assert and .deassert.
	 */
	ret = reset_control_assert(rstc);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to assert reset\n");

	/* Datasheet: Hold /RESET low for > 2 f_CLKIN cycles. 1us is ample. */
	fsleep(1);

	ret = reset_control_deassert(rstc);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to deassert reset\n");

	/* Wait t_REGACQ (5us) for registers to be accessible */
	fsleep(ADS131M_RESET_DELAY_US);

	return 0;
}

/**
 * ads131m_sw_reset - Issues a software RESET and verifies ACK.
 * @priv: Device private data structure.
 *
 * This function sends a RESET command (with Input CRC), waits t_REGACQ,
 * reads back the RESET ACK, and then sends a final NULL to check for
 * any input CRC errors.
 *
 * Return: 0 on success, or a negative error code.
 */
static int ads131m_sw_reset(struct ads131m_priv *priv)
{
	u16 expected_ack = priv->config->reset_ack;
	struct device *dev = &priv->spi->dev;
	u16 response;
	int ret;

	guard(mutex)(&priv->lock);

	ret = ads131m_tx_frame_unlocked(priv, ADS131M_CMD_RESET);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to send RESET command\n");

	/* Wait t_REGACQ (5us) for device to be ready after reset */
	fsleep(ADS131M_RESET_DELAY_US);

	/* Cycle 2: Send NULL + CRC to retrieve the response to the RESET */
	ret = ads131m_rx_frame_unlocked(priv);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to read RESET ACK\n");

	response = get_unaligned_be16(&priv->rx_buffer[0]);

	/* Check against the device-specific ACK value */
	if (response != expected_ack)
		return dev_err_probe(dev, -EIO,
				     "RESET ACK mismatch, got 0x%04x, expected 0x%04x\n",
				     response, expected_ack);

	/* Cycle 3: Check STATUS for Input CRC error on the RESET command. */
	return ads131m_check_status_crc_err(priv);
}

/**
 * ads131m_reset - Resets the device using hardware or software.
 * @priv: Device private data structure.
 * @rstc: Optional reset control, or NULL for software reset.
 *
 * This function performs a hardware reset if supported (rstc provided),
 * otherwise it issues a software RESET command via SPI.
 *
 * Note: The software reset path also validates the device's reset
 * acknowledgment against the expected ID for the compatible string.
 * The hardware reset path bypasses this ID check.
 *
 * Return: 0 on success, or a negative error code.
 */
static int ads131m_reset(struct ads131m_priv *priv, struct reset_control *rstc)
{
	if (rstc)
		return ads131m_hw_reset(priv, rstc);

	return ads131m_sw_reset(priv);
}

static int ads131m_power_init(struct ads131m_priv *priv)
{
	static const char * const supply_ids[] = { "avdd", "dvdd" };
	struct device *dev = &priv->spi->dev;
	int vref_uV;
	int ret;

	ret = devm_regulator_bulk_get_enable(dev, ARRAY_SIZE(supply_ids), supply_ids);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to enable regulators\n");

	/* Default to Internal 1.2V reference: 1200mV / 2^23 */
	priv->scale_val = ADS131M_VREF_INTERNAL_mV;
	priv->scale_val2 = BIT(ADS131M_CODE_BITS);

	if (!priv->config->supports_extref)
		return 0;

	ret = devm_regulator_get_enable_read_voltage(dev, "refin");
	if (ret < 0 && ret != -ENODEV)
		return dev_err_probe(dev, ret, "failed to get refin supply\n");

	if (ret == 0)
		return dev_err_probe(dev, -EINVAL, "refin supply reports 0V\n");

	if (ret == -ENODEV)
		return 0;

	vref_uV = ret;

	/*
	 * External reference found: Scale(mV) = (vref_uV * 0.96) / 1000
	 * The denominator is 100 * 2^23 because of the 0.96 factor (96/100).
	 */
	priv->scale_val = div_s64((s64)vref_uV * ADS131M_EXTREF_SCALE_NUM, 1000);
	priv->scale_val2 = ADS131M_EXTREF_SCALE_DEN * BIT(ADS131M_CODE_BITS);
	priv->use_external_ref = true;

	return 0;
}

/**
 * ads131m_hw_init - Initialize the ADC hardware.
 * @priv: Device private data structure.
 * @rstc: Optional reset control, or NULL for software reset.
 * @is_xtal: True if 'clock-names' is "xtal", false if "clkin".
 *
 * Return: 0 on success, or a negative error code.
 */
static int ads131m_hw_init(struct ads131m_priv *priv,
			   struct reset_control *rstc, bool is_xtal)
{
	struct device *dev = &priv->spi->dev;
	u16 mode_clear, mode_set;
	int ret;

	ret = ads131m_reset(priv, rstc);
	if (ret < 0)
		return ret;

	/*
	 * Configure CLOCK register (0x03) based on DT properties.
	 * This register only needs configuration for 32-pin (M06/M08)
	 * variants, as the configurable bits (XTAL_DIS, EXTREF_EN)
	 * are reserved on 20-pin (M02/M03/M04) variants.
	 */
	if (priv->config->supports_xtal || priv->config->supports_extref) {
		u16 clk_set = 0;

		if (priv->config->supports_xtal && !is_xtal)
			clk_set |= ADS131M_CLOCK_XTAL_DIS;

		if (priv->config->supports_extref && priv->use_external_ref)
			clk_set |= ADS131M_CLOCK_EXTREF_EN;

		ret = ads131m_rmw_reg(priv, ADS131M_REG_CLOCK,
				      ADS131M_CLOCK_EXTREF_EN | ADS131M_CLOCK_XTAL_DIS,
				      clk_set);
		if (ret < 0)
			return dev_err_probe(dev, ret, "Failed to configure CLOCK register\n");
	}

	/*
	 * The RESET command sets all registers to default, which means:
	 * 1. The RESET bit (Bit 10) in MODE is set to '1'.
	 * 2. The CRC_TYPE bit (Bit 11) in MODE is '0' (CCITT).
	 * 3. The RX_CRC_EN bit (Bit 12) in MODE is '0' (Disabled).
	 *
	 * We must:
	 * 1. Clear the RESET bit.
	 * 2. Enable Input CRC (RX_CRC_EN).
	 * 3. Explicitly clear the ANSI CRC bit (for certainty).
	 */
	mode_clear = ADS131M_MODE_CRC_TYPE_ANSI | ADS131M_MODE_RESET_FLAG;
	mode_set = ADS131M_MODE_RX_CRC_EN;

	ret = ads131m_rmw_reg(priv, ADS131M_REG_MODE, mode_clear, mode_set);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to configure MODE register\n");

	return 0;
}

/**
 * ads131m_parse_clock - enable clock and detect "xtal" selection
 * @priv: Device private data structure.
 * @is_xtal: result flag (true if "xtal", false if default "clkin")
 *
 * Return: 0 on success, or a negative error code.
 */
static int ads131m_parse_clock(struct ads131m_priv *priv, bool *is_xtal)
{
	struct device *dev = &priv->spi->dev;
	struct clk *clk;
	int ret;

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR_OR_NULL(clk)) {
		if (IS_ERR(clk))
			ret = PTR_ERR(clk);
		else
			ret = -ENODEV;

		return dev_err_probe(dev, ret, "clk get enabled failed\n");
	}

	ret = device_property_match_string(dev, "clock-names", "xtal");
	if (ret > 0)
		return dev_err_probe(dev, -EINVAL,
				     "'xtal' must be the only or first clock name");

	if (ret < 0 && ret != -ENODATA)
		return dev_err_probe(dev, ret,
				     "failed to read 'clock-names' property");

	if (ret == 0 && !priv->config->supports_xtal)
		return dev_err_probe(dev, -EINVAL,
				     "'xtal' clock not supported on this device");

	*is_xtal = !ret;

	return 0;
}

static int ads131m_probe(struct spi_device *spi)
{
	const struct ads131m_configuration *config;
	struct device *dev = &spi->dev;
	struct reset_control *rstc;
	struct iio_dev *indio_dev;
	struct ads131m_priv *priv;
	bool is_xtal;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*priv));
	if (!indio_dev)
		return -ENOMEM;

	priv = iio_priv(indio_dev);
	priv->indio_dev = indio_dev;
	priv->spi = spi;

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &ads131m_info;

	config = spi_get_device_match_data(spi);

	priv->config = config;
	indio_dev->name = config->name;
	indio_dev->channels = config->channels;
	indio_dev->num_channels = config->num_channels;

	rstc = devm_reset_control_get_optional_exclusive(dev, NULL);
	if (IS_ERR(rstc))
		return dev_err_probe(dev, PTR_ERR(rstc),
				     "Failed to get reset controller\n");

	ret = devm_mutex_init(dev, &priv->lock);
	if (ret < 0)
		return ret;

	ret = ads131m_prepare_message(priv);
	if (ret < 0)
		return ret;

	ret = ads131m_power_init(priv);
	if (ret < 0)
		return ret;

	/* Power must be applied and stable before the clock is enabled. */
	ret = ads131m_parse_clock(priv, &is_xtal);
	if (ret < 0)
		return ret;

	ret = ads131m_hw_init(priv, rstc, is_xtal);
	if (ret < 0)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id ads131m_of_match[] = {
	{ .compatible = "ti,ads131m02", .data = &ads131m02_config },
	{ .compatible = "ti,ads131m03", .data = &ads131m03_config },
	{ .compatible = "ti,ads131m04", .data = &ads131m04_config },
	{ .compatible = "ti,ads131m06", .data = &ads131m06_config },
	{ .compatible = "ti,ads131m08", .data = &ads131m08_config },
	{ }
};
MODULE_DEVICE_TABLE(of, ads131m_of_match);

static const struct spi_device_id ads131m_id[] = {
	{ "ads131m02", (kernel_ulong_t)&ads131m02_config },
	{ "ads131m03", (kernel_ulong_t)&ads131m03_config },
	{ "ads131m04", (kernel_ulong_t)&ads131m04_config },
	{ "ads131m06", (kernel_ulong_t)&ads131m06_config },
	{ "ads131m08", (kernel_ulong_t)&ads131m08_config },
	{ }
};
MODULE_DEVICE_TABLE(spi, ads131m_id);

static struct spi_driver ads131m_driver = {
	.driver = {
		.name = "ads131m02",
		.of_match_table = ads131m_of_match,
	},
	.probe = ads131m_probe,
	.id_table = ads131m_id,
};
module_spi_driver(ads131m_driver);

MODULE_AUTHOR("David Jander <david@protonic.nl>");
MODULE_DESCRIPTION("Texas Instruments ADS131M02 ADC driver");
MODULE_LICENSE("GPL");
