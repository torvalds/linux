/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Freescale MPL115A pressure/temperature sensor
 *
 * Copyright (c) 2014 Peter Meerwald <pmeerw@pmeerw.net>
 * Copyright (c) 2016 Akinobu Mita <akinobu.mita@gmail.com>
 */

#include <linux/pm_runtime.h>

#ifndef _MPL115_H_
#define _MPL115_H_

struct mpl115_ops {
	int (*init)(struct device *);
	int (*read)(struct device *, u8);
	int (*write)(struct device *, u8, u8);
};

int mpl115_probe(struct device *dev, const char *name,
			const struct mpl115_ops *ops);

/*PM ops */
extern const struct dev_pm_ops mpl115_dev_pm_ops;

#endif
