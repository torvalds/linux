/*
 * midas-thermistor.h - thermistor of MIDAS Project
 *
 * Copyright (C) 2011 Samsung Electrnoics
 * SangYoung Son <hello.son@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __MIDAS_THERMISTOR_H
#define __MIDAS_THERMISTOR_H __FILE__

#include <linux/platform_device.h>
#include <plat/gpio-cfg.h>
#include <mach/gpio-midas.h>
#ifdef CONFIG_STMPE811_ADC
#include <linux/stmpe811-adc.h>
#endif

/* class for factory mode */
extern struct class *sec_class;

/*
 * struct sec_bat_adc_table_data - adc to temperature table for sec battery
 * driver
 * @adc: adc value
 * @value: value
 */
struct adc_table_data {
	int adc;
	int value;
};

#ifdef CONFIG_S3C_ADC
int convert_adc(int adc_data, int channel);
#endif

#ifdef CONFIG_STMPE811_ADC
extern struct stmpe811_platform_data stmpe811_pdata;
#endif

#ifdef CONFIG_SEC_THERMISTOR
extern struct platform_device sec_device_thermistor;
#endif
#ifdef CONFIG_SEC_SUBTHERMISTOR
extern struct platform_device sec_device_subthermistor;
#endif

#endif /* __MIDAS_THERMISTOR_H */

