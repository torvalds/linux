/*
 * Freescale MPL115A pressure/temperature sensor
 *
 * Copyright (c) 2014 Peter Meerwald <pmeerw@pmeerw.net>
 * Copyright (c) 2016 Akinobu Mita <akinobu.mita@gmail.com>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 */

#ifndef _MPL115_H_
#define _MPL115_H_

struct mpl115_ops {
	int (*init)(struct device *);
	int (*read)(struct device *, u8);
	int (*write)(struct device *, u8, u8);
};

int mpl115_probe(struct device *dev, const char *name,
			const struct mpl115_ops *ops);

#endif
