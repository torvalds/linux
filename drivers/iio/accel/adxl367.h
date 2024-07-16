/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Cosmin Tanislav <cosmin.tanislav@analog.com>
 */

#ifndef _ADXL367_H_
#define _ADXL367_H_

#include <linux/types.h>

struct device;
struct regmap;

struct adxl367_ops {
	int (*read_fifo)(void *context, __be16 *fifo_buf,
			 unsigned int fifo_entries);
};

int adxl367_probe(struct device *dev, const struct adxl367_ops *ops,
		  void *context, struct regmap *regmap, int irq);

#endif /* _ADXL367_H_ */
