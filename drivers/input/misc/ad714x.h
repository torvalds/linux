/*
 * AD714X CapTouch Programmable Controller driver (bus interfaces)
 *
 * Copyright 2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _AD714X_H_
#define _AD714X_H_

#include <linux/types.h>

struct device;
struct ad714x_chip;

typedef int (*ad714x_read_t)(struct device *, unsigned short, unsigned short *);
typedef int (*ad714x_write_t)(struct device *, unsigned short, unsigned short);

int ad714x_disable(struct ad714x_chip *ad714x);
int ad714x_enable(struct ad714x_chip *ad714x);
struct ad714x_chip *ad714x_probe(struct device *dev, u16 bus_type, int irq,
				 ad714x_read_t read, ad714x_write_t write);
void ad714x_remove(struct ad714x_chip *ad714x);

#endif
