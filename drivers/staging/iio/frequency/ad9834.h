/*
 * AD9833/AD9834/AD9837/AD9838 SPI DDS driver
 *
 * Copyright 2010-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */
#ifndef IIO_DDS_AD9834_H_
#define IIO_DDS_AD9834_H_

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

#endif /* IIO_DDS_AD9834_H_ */
