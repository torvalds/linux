/*
 * AD5446 SPI DAC driver
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */
#ifndef IIO_DAC_AD5446_H_
#define IIO_DAC_AD5446_H_

/* DAC Control Bits */

#define AD5446_LOAD		(0x0 << 14) /* Load and update */
#define AD5446_SDO_DIS		(0x1 << 14) /* Disable SDO */
#define AD5446_NOP		(0x2 << 14) /* No operation */
#define AD5446_CLK_RISING	(0x3 << 14) /* Clock data on rising edge */

#define AD5620_LOAD		(0x0 << 14) /* Load and update Norm Operation*/
#define AD5620_PWRDWN_1k	(0x1 << 14) /* Power-down: 1kOhm to GND */
#define AD5620_PWRDWN_100k	(0x2 << 14) /* Power-down: 100kOhm to GND */
#define AD5620_PWRDWN_TRISTATE	(0x3 << 14) /* Power-down: Three-state */

#define AD5660_LOAD		(0x0 << 16) /* Load and update Norm Operation*/
#define AD5660_PWRDWN_1k	(0x1 << 16) /* Power-down: 1kOhm to GND */
#define AD5660_PWRDWN_100k	(0x2 << 16) /* Power-down: 100kOhm to GND */
#define AD5660_PWRDWN_TRISTATE	(0x3 << 16) /* Power-down: Three-state */

#define MODE_PWRDWN_1k		0x1
#define MODE_PWRDWN_100k	0x2
#define MODE_PWRDWN_TRISTATE	0x3

/**
 * struct ad5446_state - driver instance specific data
 * @spi:		spi_device
 * @chip_info:		chip model specific constants, available modes etc
 * @reg:		supply regulator
 * @vref_mv:		actual reference voltage used
 */

struct ad5446_state {
	struct spi_device		*spi;
	const struct ad5446_chip_info	*chip_info;
	struct regulator		*reg;
	unsigned short			vref_mv;
	unsigned			cached_val;
	unsigned			pwr_down_mode;
	unsigned			pwr_down;
};

/**
 * struct ad5446_chip_info - chip specific information
 * @channel:		channel spec for the DAC
 * @int_vref_mv:	AD5620/40/60: the internal reference voltage
 * @write:		chip specific helper function to write to the register
 */

struct ad5446_chip_info {
	struct iio_chan_spec	channel;
	u16			int_vref_mv;
	int			(*write)(struct ad5446_state *st, unsigned val);
};

/**
 * ad5446_supported_device_ids:
 * The AD5620/40/60 parts are available in different fixed internal reference
 * voltage options. The actual part numbers may look differently
 * (and a bit cryptic), however this style is used to make clear which
 * parts are supported here.
 */

enum ad5446_supported_device_ids {
	ID_AD5444,
	ID_AD5446,
	ID_AD5541A,
	ID_AD5512A,
	ID_AD5553,
	ID_AD5601,
	ID_AD5611,
	ID_AD5621,
	ID_AD5620_2500,
	ID_AD5620_1250,
	ID_AD5640_2500,
	ID_AD5640_1250,
	ID_AD5660_2500,
	ID_AD5660_1250,
	ID_AD5662,
};

#endif /* IIO_DAC_AD5446_H_ */
