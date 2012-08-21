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
	struct device		*dev;
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


#endif /* IIO_DAC_AD5446_H_ */
