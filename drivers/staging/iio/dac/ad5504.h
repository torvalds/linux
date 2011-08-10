/*
 * AD5504 SPI DAC driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#ifndef SPI_AD5504_H_
#define SPI_AD5504_H_

#define AD5505_BITS			12
#define AD5504_RES_MASK			((1 << (AD5505_BITS)) - 1)

#define AD5504_CMD_READ			(1 << 15)
#define AD5504_CMD_WRITE		(0 << 15)
#define AD5504_ADDR(addr)		((addr) << 12)

/* Registers */
#define AD5504_ADDR_NOOP		0
#define AD5504_ADDR_DAC0		1
#define AD5504_ADDR_DAC1		2
#define AD5504_ADDR_DAC2		3
#define AD5504_ADDR_DAC3		4
#define AD5504_ADDR_ALL_DAC		5
#define AD5504_ADDR_CTRL		7

/* Control Register */
#define AD5504_DAC_PWR(ch)		((ch) << 2)
#define AD5504_DAC_PWRDWN_MODE(mode)	((mode) << 6)
#define AD5504_DAC_PWRDN_20K		0
#define AD5504_DAC_PWRDN_3STATE		1

/*
 * TODO: struct ad5504_platform_data needs to go into include/linux/iio
 */

struct ad5504_platform_data {
	u16				vref_mv;
};

/**
 * struct ad5446_state - driver instance specific data
 * @us:			spi_device
 * @reg:		supply regulator
 * @vref_mv:		actual reference voltage used
 * @pwr_down_mask	power down mask
 * @pwr_down_mode	current power down mode
 */

struct ad5504_state {
	struct spi_device		*spi;
	struct regulator		*reg;
	unsigned short			vref_mv;
	unsigned			pwr_down_mask;
	unsigned			pwr_down_mode;
};

/**
 * ad5504_supported_device_ids:
 */

enum ad5504_supported_device_ids {
	ID_AD5504,
	ID_AD5501,
};

#endif /* SPI_AD5504_H_ */
