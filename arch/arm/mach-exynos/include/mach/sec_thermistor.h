/*
 * Copyright (C) 2011 Samsung Electronics, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __MACH_SEC_THERMISTOR_H
#define __MACH_SEC_THERMISTOR_H __FILE__


/**
 * struct sec_therm_adc_table - adc to temperature table for sec thermistor
 * driver
 * @adc: adc value
 * @temperature: temperature(C) * 10
 */
struct sec_therm_adc_table {
	int adc;
	int temperature;
};

/**
 * struct sec_bat_plaform_data - init data for sec batter driver
 * @adc_channel: adc channel that connected to thermistor
 * @adc_table: array of adc to temperature data
 * @adc_arr_size: size of adc_table
 * @polling_interval: interval for polling thermistor (msecs)
 */
struct sec_therm_platform_data {
	unsigned int adc_channel;
	unsigned int adc_arr_size;
	struct sec_therm_adc_table *adc_table;
	unsigned int polling_interval;
	int (*get_siop_level)(int);
};

#endif /* __MACH_SEC_THERMISTOR_H */
