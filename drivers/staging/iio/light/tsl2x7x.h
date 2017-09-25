/*
 * Device driver for monitoring ambient light intensity (lux)
 * and proximity (prox) within the TAOS TSL2X7X family of devices.
 *
 * Copyright (c) 2012, TAOS Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA	02110-1301, USA.
 */

#ifndef __TSL2X7X_H
#define __TSL2X7X_H
#include <linux/pm.h>

struct iio_dev;

struct tsl2x7x_lux {
	unsigned int ratio;
	unsigned int ch0;
	unsigned int ch1;
};

/* Max number of segments allowable in LUX table */
#define TSL2X7X_MAX_LUX_TABLE_SIZE		9
/* The default LUX tables all have 3 elements.  */
#define TSL2X7X_DEF_LUX_TABLE_SZ		3
#define TSL2X7X_DEFAULT_TABLE_BYTES (sizeof(struct tsl2x7x_lux) * \
				     TSL2X7X_DEF_LUX_TABLE_SZ)

/**
 * struct tsl2x7x_default_settings - power on defaults unless
 *                                   overridden by platform data.
 *  @als_time:              ALS Integration time - multiple of 50mS
 *  @als_gain:              Index into the ALS gain table.
 *  @als_gain_trim:         default gain trim to account for
 *                          aperture effects.
 *  @wait_time:             Time between PRX and ALS cycles
 *                          in 2.7 periods
 *  @prx_time:              5.2ms prox integration time -
 *                          decrease in 2.7ms periods
 *  @prx_gain:              Proximity gain index
 *  @prox_config:           Prox configuration filters.
 *  @als_cal_target:        Known external ALS reading for
 *                          calibration.
 *  @interrupts_en:         Enable/Disable - 0x00 = none, 0x10 = als,
 *                                           0x20 = prx,  0x30 = bth
 *  @persistence:           H/W Filters, Number of 'out of limits'
 *                          ADC readings PRX/ALS.
 *  @als_thresh_low:        CH0 'low' count to trigger interrupt.
 *  @als_thresh_high:       CH0 'high' count to trigger interrupt.
 *  @prox_thres_low:        Low threshold proximity detection.
 *  @prox_thres_high:       High threshold proximity detection
 *  @prox_pulse_count:      Number if proximity emitter pulses
 *  @prox_max_samples_cal:  Used for prox cal.
 */
struct tsl2x7x_settings {
	int als_time;
	int als_gain;
	int als_gain_trim;
	int wait_time;
	int prx_time;
	int prox_gain;
	int prox_config;
	int als_cal_target;
	u8  interrupts_en;
	u8  persistence;
	int als_thresh_low;
	int als_thresh_high;
	int prox_thres_low;
	int prox_thres_high;
	int prox_pulse_count;
	int prox_max_samples_cal;
};

/**
 * struct tsl2X7X_platform_data - Platform callback, glass and defaults
 * @platform_power:            Suspend/resume platform callback
 * @power_on:                  Power on callback
 * @power_off:                 Power off callback
 * @platform_lux_table:        Device specific glass coefficents
 * @platform_default_settings: Device specific power on defaults
 *
 */
struct tsl2X7X_platform_data {
	int (*platform_power)(struct device *dev, pm_message_t);
	int (*power_on)(struct iio_dev *indio_dev);
	int (*power_off)(struct i2c_client *dev);
	struct tsl2x7x_lux platform_lux_table[TSL2X7X_MAX_LUX_TABLE_SIZE];
	struct tsl2x7x_settings *platform_default_settings;
};

#endif /* __TSL2X7X_H */
