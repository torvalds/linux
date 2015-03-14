/*
 * AD9833/AD9834/AD9837/AD9838 SPI DDS driver
 *
 * Copyright 2010-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */
#ifndef IIO_DDS_AD9834_H_
#define IIO_DDS_AD9834_H_

/* Registers */

#define AD9834_REG_CMD		0
#define AD9834_REG_FREQ0	BIT(14)
#define AD9834_REG_FREQ1	BIT(15)
#define AD9834_REG_PHASE0	(BIT(15) | BIT(14))
#define AD9834_REG_PHASE1	(BIT(15) | BIT(14) | BIT(13))

/* Command Control Bits */

#define AD9834_B28		BIT(13)
#define AD9834_HLB		BIT(12)
#define AD9834_FSEL		BIT(11)
#define AD9834_PSEL		BIT(10)
#define AD9834_PIN_SW		BIT(9)
#define AD9834_RESET		BIT(8)
#define AD9834_SLEEP1		BIT(7)
#define AD9834_SLEEP12		BIT(6)
#define AD9834_OPBITEN		BIT(5)
#define AD9834_SIGN_PIB		BIT(4)
#define AD9834_DIV2		BIT(3)
#define AD9834_MODE		BIT(1)

#define AD9834_FREQ_BITS	28
#define AD9834_PHASE_BITS	12

#define RES_MASK(bits)	(BIT(bits) - 1)

/**
 * struct ad9834_state - driver instance specific data
 * @spi:		spi_device
 * @reg:		supply regulator
 * @mclk:		external master clock
 * @control:		cached control word
 * @xfer:		default spi transfer
 * @msg:		default spi message
 * @freq_xfer:		tuning word spi transfer
 * @freq_msg:		tuning word spi message
 * @data:		spi transmit buffer
 * @freq_data:		tuning word spi transmit buffer
 */

struct ad9834_state {
	struct spi_device		*spi;
	struct regulator		*reg;
	unsigned int			mclk;
	unsigned short			control;
	unsigned short			devid;
	struct spi_transfer		xfer;
	struct spi_message		msg;
	struct spi_transfer		freq_xfer[2];
	struct spi_message		freq_msg;

	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	__be16				data ____cacheline_aligned;
	__be16				freq_data[2];
};


/*
 * TODO: struct ad7887_platform_data needs to go into include/linux/iio
 */

/**
 * struct ad9834_platform_data - platform specific information
 * @mclk:		master clock in Hz
 * @freq0:		power up freq0 tuning word in Hz
 * @freq1:		power up freq1 tuning word in Hz
 * @phase0:		power up phase0 value [0..4095] correlates with 0..2PI
 * @phase1:		power up phase1 value [0..4095] correlates with 0..2PI
 * @en_div2:		digital output/2 is passed to the SIGN BIT OUT pin
 * @en_signbit_msb_out:	the MSB (or MSB/2) of the DAC data is connected to the
 *			SIGN BIT OUT pin. en_div2 controls whether it is the MSB
 *			or MSB/2 that is output. if en_signbit_msb_out=false,
 *			the on-board comparator is connected to SIGN BIT OUT
 */

struct ad9834_platform_data {
	unsigned int		mclk;
	unsigned int		freq0;
	unsigned int		freq1;
	unsigned short		phase0;
	unsigned short		phase1;
	bool			en_div2;
	bool			en_signbit_msb_out;
};

/**
 * ad9834_supported_device_ids:
 */

enum ad9834_supported_device_ids {
	ID_AD9833,
	ID_AD9834,
	ID_AD9837,
	ID_AD9838,
};

#endif /* IIO_DDS_AD9834_H_ */
