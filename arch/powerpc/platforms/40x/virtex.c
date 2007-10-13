/*
 * Xilinx Virtex (IIpro & 4FX) based board support
 *
 * Copyright 2007 Secret Lab Technologies Ltd.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/of_platform.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/time.h>
#include <asm/xilinx_intc.h>

static int __init virtex_device_probe(void)
{
	if (!machine_is(virtex))
		return 0;

	of_platform_bus_probe(NULL, NULL, NULL);

	return 0;
}
device_initcall(virtex_device_probe);

static int __init virtex_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (!of_flat_dt_is_compatible(root, "xilinx,virtex"))
		return 0;

	return 1;
}

define_machine(virtex) {
	.name			= "Xilinx Virtex",
	.probe			= virtex_probe,
	.init_IRQ		= xilinx_intc_init_tree,
	.get_irq		= xilinx_intc_get_irq,
	.calibrate_decr		= generic_calibrate_decr,
};
