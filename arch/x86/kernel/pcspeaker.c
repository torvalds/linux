// SPDX-License-Identifier: GPL-2.0
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/init.h>

static __init int add_pcspkr(void)
{
	struct platform_device *pd;

	pd = platform_device_register_simple("pcspkr", -1, NULL, 0);

	return IS_ERR(pd) ? PTR_ERR(pd) : 0;
}
device_initcall(add_pcspkr);
