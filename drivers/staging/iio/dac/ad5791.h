/*
 * AD5791 SPI DAC driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#ifndef SPI_AD5791_H_
#define SPI_AD5791_H_

#define AD5791_RES_MASK(x)		((1 << (x)) - 1)
#define AD5791_DAC_MASK			AD5791_RES_MASK(20)
#define AD5791_DAC_MSB			(1 << 19)

#define AD5791_CMD_READ			(1 << 23)
#define AD5791_CMD_WRITE		(0 << 23)
#define AD5791_ADDR(addr)		((addr) << 20)

/* Registers */
#define AD5791_ADDR_NOOP		0
#define AD5791_ADDR_DAC0		1
#define AD5791_ADDR_CTRL		2
#define AD5791_ADDR_CLRCODE		3
#define AD5791_ADDR_SW_CTRL		4

/* Control Register */
#define AD5791_CTRL_RBUF		(1 << 1)
#define AD5791_CTRL_OPGND		(1 << 2)
#define AD5791_CTRL_DACTRI		(1 << 3)
#define AD5791_CTRL_BIN2SC		(1 << 4)
#define AD5791_CTRL_SDODIS		(1 << 5)
#define AD5761_CTRL_LINCOMP(x)		((x) << 6)

#define AD5791_LINCOMP_0_10		0
#define AD5791_LINCOMP_10_12		1
#define AD5791_LINCOMP_12_16		2
#define AD5791_LINCOMP_16_19		3
#define AD5791_LINCOMP_19_20		12

#define AD5780_LINCOMP_0_10		0
#define AD5780_LINCOMP_10_20		12

/* Software Control Register */
#define AD5791_SWCTRL_LDAC		(1 << 0)
#define AD5791_SWCTRL_CLR		(1 << 1)
#define AD5791_SWCTRL_RESET		(1 << 2)

#define AD5791_DAC_PWRDN_6K		0
#define AD5791_DAC_PWRDN_3STATE		1

/*
 * TODO: struct ad5791_platform_data needs to go into include/linux/iio
 */

/**
 * struct ad5791_platform_data - platform specific information
 * @vref_pos_mv:	Vdd Positive Analog Supply Volatge (mV)
 * @vref_neg_mv:	Vdd Negative Analog Supply Volatge (mV)
 * @use_rbuf_gain2:	ext. amplifier connected in gain of two configuration
 */

struct ad5791_platform_data {
	u16				vref_pos_mv;
	u16				vref_neg_mv;
	bool				use_rbuf_gain2;
};

/**
 * struct ad5791_chip_info - chip specific information
 * @bits:		accuracy of the DAC in bits
 * @left_shift:		number of bits the datum must be shifted
 * @get_lin_comp:	function pointer to the device specific function
 */

struct ad5791_chip_info {
	u8			bits;
	u8			left_shift;
	int (*get_lin_comp)	(unsigned int span);
};

/**
 * struct ad5791_state - driver instance specific data
 * @us:			spi_device
 * @reg_vdd:		positive supply regulator
 * @reg_vss:		negative supply regulator
 * @chip_info:		chip model specific constants
 * @vref_mv:		actual reference voltage used
 * @pwr_down_mode	current power down mode
 */

struct ad5791_state {
	struct spi_device		*spi;
	struct regulator		*reg_vdd;
	struct regulator		*reg_vss;
	const struct ad5791_chip_info	*chip_info;
	unsigned short			vref_mv;
	unsigned			ctrl;
	unsigned			pwr_down_mode;
	bool				pwr_down;
};

/**
 * ad5791_supported_device_ids:
 */

enum ad5791_supported_device_ids {
	ID_AD5760,
	ID_AD5780,
	ID_AD5781,
	ID_AD5791,
};

#endif /* SPI_AD5791_H_ */
