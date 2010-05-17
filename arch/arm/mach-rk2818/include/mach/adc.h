/* arch/arm/mach-rk28418/include/mach/adc.h
 *
 * Copyright (c) 2010 luowei <lw@rock-chips.com>
 *
 * This program is free software; yosu can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#ifndef __ASM_ARCH_ADC_H
#define __ASM_ARCH_ADC_H

#define RK28_ADCREG(x) (x)
#define RK28_ADCDAT    RK28_ADCREG(0x00)
#define RK28_ADCSTAS   RK28_ADCREG(0x04)
#define RK28_ADCCON	   RK28_ADCREG(0x08)

//ADC_DATA
#define RK28_ADC_DATA_MASK	(0x3ff)

//ADC_STAS
#define RK28_ADC_STAS_STOP         (0)

//ADC_CTRL
#define RK28_ADC_INT_END		(1<<6)
#define RK28_ADC_INT_CLEAR		(~(1<<6))
#define RK28_ADC_INT_ENABLE 	(1<<5) 
#define RK28_ADC_INT_DISABLE 	(~(1<<5))
#define RK28_ADC_CONV_START      (1<<4)
#define RK28_ADC_CONV_STOP       (~(1<<4))
#define RK28_ADC_POWER_UP    (1<<3)
#define RK28_ADC_POWER_DOWN   (~(1<<3))
#define RK28_ADC_SEL_CH(x)    ((x)&0x03)
#define RK28_ADC_MASK_CH    (~(0x03))

struct rk28_adc_client;

extern volatile int gAdcValue[4];	//start adc in rk2818_adckey.c
extern int rk28_adc_start(struct rk28_adc_client *client,
			 unsigned int channel, unsigned int nr_samples);

extern int rk28_adc_read(struct rk28_adc_client *client, unsigned int ch);

extern struct rk28_adc_client *
	rk28_adc_register(struct platform_device *pdev,
			 void (*select)(struct rk28_adc_client *client,
					unsigned selected),
			 void (*conv)(struct rk28_adc_client *client,
				      unsigned d0, unsigned d1,
				      unsigned *samples_left),
			 unsigned int is_ts);

extern void rk28_adc_release(struct rk28_adc_client *client);

#endif

