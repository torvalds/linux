/*
 * AD7746 capacitive sensor driver supporting AD7745, AD7746 and AD7747
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#ifndef IIO_CDC_AD7746_H_
#define IIO_CDC_AD7746_H_

/*
 * TODO: struct ad7746_platform_data needs to go into include/linux/iio
 */

#define AD7466_EXCLVL_0		0 /* +-VDD/8 */
#define AD7466_EXCLVL_1		1 /* +-VDD/4 */
#define AD7466_EXCLVL_2		2 /* +-VDD * 3/8 */
#define AD7466_EXCLVL_3		3 /* +-VDD/2 */

struct ad7746_platform_data {
	unsigned char exclvl;	/*Excitation Voltage Level */
	bool exca_en;		/* enables EXCA pin as the excitation output */
	bool exca_inv_en;	/* enables /EXCA pin as the excitation output */
	bool excb_en;		/* enables EXCB pin as the excitation output */
	bool excb_inv_en;	/* enables /EXCB pin as the excitation output */
};

#endif /* IIO_CDC_AD7746_H_ */
