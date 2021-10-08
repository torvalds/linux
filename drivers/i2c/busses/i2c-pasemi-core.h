/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/atomic.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/i2c-smbus.h>
#include <linux/io.h>
#include <linux/kernel.h>

struct pasemi_smbus {
	struct device		*dev;
	struct i2c_adapter	 adapter;
	void __iomem		*ioaddr;
	unsigned long		 base;
	int			 size;
};

int pasemi_i2c_common_probe(struct pasemi_smbus *smbus);
