/*
 * AD7887 SPI ADC driver
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */
#ifndef IIO_ADC_AD7887_H_
#define IIO_ADC_AD7887_H_

/*
 * TODO: struct ad7887_platform_data needs to go into include/linux/iio
 */

struct ad7887_platform_data {
	/*
	 * AD7887:
	 * In single channel mode en_dual = flase, AIN1/Vref pins assumes its
	 * Vref function. In dual channel mode en_dual = true, AIN1 becomes the
	 * second input channel, and Vref is internally connected to Vdd.
	 */
	bool				en_dual;
	/*
	 * AD7887:
	 * use_onchip_ref = true, the Vref is internally connected to the 2.500V
	 * Voltage reference. If use_onchip_ref = false, the reference voltage
	 * is supplied by AIN1/Vref
	 */
	bool				use_onchip_ref;
};

#endif /* IIO_ADC_AD7887_H_ */
