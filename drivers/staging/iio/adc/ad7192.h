/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AD7190 AD7192 AD7195 SPI ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
 */
#ifndef IIO_ADC_AD7192_H_
#define IIO_ADC_AD7192_H_

/*
 * TODO: struct ad7192_platform_data needs to go into include/linux/iio
 */

/**
 * struct ad7192_platform_data - platform/board specific information
 * @vref_mv:		the external reference voltage in millivolt
 * @clock_source_sel:	[0..3]
 *			0 External 4.92 MHz clock connected from MCLK1 to MCLK2
 *			1 External Clock applied to MCLK2
 *			2 Internal 4.92 MHz Clock not available at the MCLK2 pin
 *			3 Internal 4.92 MHz Clock available at the MCLK2 pin
 * @ext_clk_Hz:		the external clock frequency in Hz, if not set
 *			the driver uses the internal clock (16.776 MHz)
 * @refin2_en:		REFIN1/REFIN2 Reference Select (AD7190/2 only)
 * @rej60_en:		50/60Hz notch filter enable
 * @sinc3_en:		SINC3 filter enable (default SINC4)
 * @chop_en:		CHOP mode enable
 * @buf_en:		buffered input mode enable
 * @unipolar_en:	unipolar mode enable
 * @burnout_curr_en:	constant current generators on AIN(+|-) enable
 */

struct ad7192_platform_data {
	u16		vref_mv;
	u8		clock_source_sel;
	u32		ext_clk_hz;
	bool		refin2_en;
	bool		rej60_en;
	bool		sinc3_en;
	bool		chop_en;
	bool		buf_en;
	bool		unipolar_en;
	bool		burnout_curr_en;
};

#endif /* IIO_ADC_AD7192_H_ */
