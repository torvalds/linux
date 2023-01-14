/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AD714X CapTouch Programmable Controller driver (bus interfaces)
 *
 * Copyright 2009-2011 Analog Devices Inc.
 */

#ifndef _AD714X_H_
#define _AD714X_H_

#include <linux/pm.h>
#include <linux/types.h>

#define STAGE_NUM              12

struct device;
struct ad714x_platform_data;
struct ad714x_driver_data;
struct ad714x_chip;

typedef int (*ad714x_read_t)(struct ad714x_chip *, unsigned short, unsigned short *, size_t);
typedef int (*ad714x_write_t)(struct ad714x_chip *, unsigned short, unsigned short);

struct ad714x_chip {
	unsigned short l_state;
	unsigned short h_state;
	unsigned short c_state;
	unsigned short adc_reg[STAGE_NUM];
	unsigned short amb_reg[STAGE_NUM];
	unsigned short sensor_val[STAGE_NUM];

	struct ad714x_platform_data *hw;
	struct ad714x_driver_data *sw;

	int irq;
	struct device *dev;
	ad714x_read_t read;
	ad714x_write_t write;

	struct mutex mutex;

	unsigned product;
	unsigned version;

	__be16 xfer_buf[16] ____cacheline_aligned;

};

extern const struct dev_pm_ops ad714x_pm;
struct ad714x_chip *ad714x_probe(struct device *dev, u16 bus_type, int irq,
				 ad714x_read_t read, ad714x_write_t write);

#endif
