/* drivers/adc/chips/rk29_adc.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#ifndef __ASM_RK30_ADC_H
#define __ASM_RK30_ADC_H

#define ADC_DATA		0x00
#define ADC_DATA_MASK		0x3ff

#define ADC_STAS		0x04
#define ADC_STAS_BUSY		(1<<0)

#define ADC_CTRL		0x08
#define ADC_CTRL_CH(ch)		(0x07 - ((ch)<<0))
#define ADC_CTRL_POWER_UP	(1<<3)
#define ADC_CTRL_START		(1<<4)
#define ADC_CTRL_IRQ_ENABLE	(1<<5)
#define ADC_CTRL_IRQ_STATUS	(1<<6)

#define ADC_DLY_PU_SOC		0x0C 

#define ADC_CLK_RATE		1  //1M
#define SAMPLE_RATE		(20/ADC_CLK_RATE)  //20 CLK

struct tsadc_table
{
	int code;
	int temp;
};

static struct tsadc_table table_code_to_temp[] =
{
	{3800, -40},
	{3792, -35},
	{3783, -30},
	{3774, -25},
	{3765, -20},
	{3756, -15},
	{3747, -10},
	{3737, -5},
	{3728, 0},
	{3718, 5},
	
	{3708, 10},
	{3698, 15},
	{3688, 20},
	{3678, 25},
	{3667, 30},
	{3656, 35},
	{3645, 40},
	{3634, 45},
	{3623, 50},
	{3611, 55},
	
	{3600, 60},
	{3588, 65},
	{3575, 70},
	{3563, 75},
	{3550, 80},
	{3537, 85},
	{3524, 90},
	{3510, 95},
	{3496, 100},
	{3482, 105},
	
	{3467, 110},
	{3452, 115},
	{3437, 120},
	{3421, 125},

};



#endif /* __ASM_RK30_ADC_H */
