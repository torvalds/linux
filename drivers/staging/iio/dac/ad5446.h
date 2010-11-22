/*
 * AD5446 SPI DAC driver
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */
#ifndef IIO_ADC_AD5446_H_
#define IIO_ADC_AD5446_H_

/* DAC Control Bits */

#define AD5446_LOAD		(0x0 << 14) /* Load and update */
#define AD5446_SDO_DIS		(0x1 << 14) /* Disable SDO */
#define AD5446_NOP		(0x2 << 14) /* No operation */
#define AD5446_CLK_RISING	(0x3 << 14) /* Clock data on rising edge */

#define RES_MASK(bits)	((1 << (bits)) - 1)

struct ad5446_chip_info {
	u8				bits;		/* number of DAC bits */
	u8				storagebits;	/* number of bits written to the DAC */
	u8				left_shift;	/* number of bits the datum must be shifted */
	char				sign;		/* [s]igned or [u]nsigned */
};

struct ad5446_state {
	struct iio_dev			*indio_dev;
	struct spi_device		*spi;
	const struct ad5446_chip_info	*chip_info;
	struct regulator		*reg;
	struct work_struct		poll_work;
	unsigned short			vref_mv;
	struct spi_transfer		xfer;
	struct spi_message		msg;
	unsigned short			data;
};

enum ad5446_supported_device_ids {
	ID_AD5444,
	ID_AD5446,
	ID_AD5542A,
	ID_AD5512A,
};

#endif /* IIO_ADC_AD5446_H_ */
