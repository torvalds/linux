/*
 * ADT7316 digital temperature sensor driver supporting ADT7316/7/8 ADT7516/7/9
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _ADT7316_H_
#define _ADT7316_H_

#include <linux/types.h>

#define ADT7316_REG_MAX_ADDR		0x3F

struct adt7316_bus {
	void *client;
	int irq;
	int irq_flags;
	int (*read) (void *client, u8 reg, u8 *data);
	int (*write) (void *client, u8 reg, u8 val);
	int (*multi_read) (void *client, u8 first_reg, u8 count, u8 *data);
	int (*multi_write) (void *client, u8 first_reg, u8 count, u8 *data);
};

#ifdef CONFIG_PM
int adt7316_disable(struct device *dev);
int adt7316_enable(struct device *dev);
#endif
int adt7316_probe(struct device *dev, struct adt7316_bus *bus, const char *name);
int adt7316_remove(struct device *dev);

#endif
