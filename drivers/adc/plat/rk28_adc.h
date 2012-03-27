/* drivers/adc/chips/rk28_adc.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#ifndef __ASM_RK29_ADC_H
#define __ASM_RK29_ADC_H

#define ADC_DATA			0x00
#define ADC_DATA_MASK		0x3ff

#define ADC_STAS			0x04
#define ADC_STAS_BUSY		(1<<0)

#define ADC_CTRL			0x08
#define ADC_CTRL_CH(ch)		(ch >> SARADC_CHN_SHIFT)
#define ADC_CTRL_POWER_UP	(1<<3)
#define ADC_CTRL_START		(1<<4)
#define ADC_CTRL_IRQ_ENABLE	(1<<5)
#define ADC_CTRL_IRQ_STATUS	(1<<6)

#define ADC_CLK_RATE		1  //1M
/* maximum conversion rate of 100KSPS with 1MHZ ADC converter clock.
 * SET: real conversion rate is half of maximum conversion rate
 */
#define SAMPLE_RATE			((1000/100) * 2 /(ADC_CLK_RATE))


#endif /* __ASM_RK29_ADC_H */
