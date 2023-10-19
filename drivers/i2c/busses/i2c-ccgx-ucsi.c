// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Instantiate UCSI device for Cypress CCGx Type-C controller.
 * Derived from i2c-designware-pcidrv.c and i2c-nvidia-gpu.c.
 */

#include <linux/i2c.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/string.h>

#include "i2c-ccgx-ucsi.h"

struct software_node;

struct i2c_client *i2c_new_ccgx_ucsi(struct i2c_adapter *adapter, int irq,
				     const struct software_node *swnode)
{
	struct i2c_board_info info = {};

	strscpy(info.type, "ccgx-ucsi", sizeof(info.type));
	info.addr = 0x08;
	info.irq = irq;
	info.swnode = swnode;

	return i2c_new_client_device(adapter, &info);
}
EXPORT_SYMBOL_GPL(i2c_new_ccgx_ucsi);

MODULE_LICENSE("GPL");
