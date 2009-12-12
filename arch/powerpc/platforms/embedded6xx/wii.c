/*
 * arch/powerpc/platforms/embedded6xx/wii.c
 *
 * Nintendo Wii board-specific support
 * Copyright (C) 2008-2009 The GameCube Linux Team
 * Copyright (C) 2008,2009 Albert Herranz
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 */
#define DRV_MODULE_NAME "wii"
#define pr_fmt(fmt) DRV_MODULE_NAME ": " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/kexec.h>
#include <linux/of_platform.h>

#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/time.h>
#include <asm/udbg.h>

#include "flipper-pic.h"
#include "hlwd-pic.h"
#include "usbgecko_udbg.h"

/* control block */
#define HW_CTRL_COMPATIBLE	"nintendo,hollywood-control"

#define HW_CTRL_RESETS		0x94
#define HW_CTRL_RESETS_SYS	(1<<0)

/* gpio */
#define HW_GPIO_COMPATIBLE	"nintendo,hollywood-gpio"

#define HW_GPIO_BASE(idx)	(idx * 0x20)
#define HW_GPIO_OUT(idx)	(HW_GPIO_BASE(idx) + 0)
#define HW_GPIO_DIR(idx)	(HW_GPIO_BASE(idx) + 4)

#define HW_GPIO_SHUTDOWN	(1<<1)
#define HW_GPIO_SLOT_LED	(1<<5)
#define HW_GPIO_SENSOR_BAR	(1<<8)


static void __iomem *hw_ctrl;
static void __iomem *hw_gpio;

static void wii_spin(void)
{
	local_irq_disable();
	for (;;)
		cpu_relax();
}

static void __iomem *wii_ioremap_hw_regs(char *name, char *compatible)
{
	void __iomem *hw_regs = NULL;
	struct device_node *np;
	struct resource res;
	int error = -ENODEV;

	np = of_find_compatible_node(NULL, NULL, compatible);
	if (!np) {
		pr_err("no compatible node found for %s\n", compatible);
		goto out;
	}
	error = of_address_to_resource(np, 0, &res);
	if (error) {
		pr_err("no valid reg found for %s\n", np->name);
		goto out_put;
	}

	hw_regs = ioremap(res.start, resource_size(&res));
	if (hw_regs) {
		pr_info("%s at 0x%08x mapped to 0x%p\n", name,
			res.start, hw_regs);
	}

out_put:
	of_node_put(np);
out:
	return hw_regs;
}

static void __init wii_setup_arch(void)
{
	hw_ctrl = wii_ioremap_hw_regs("hw_ctrl", HW_CTRL_COMPATIBLE);
	hw_gpio = wii_ioremap_hw_regs("hw_gpio", HW_GPIO_COMPATIBLE);
	if (hw_gpio) {
		/* turn off the front blue led and IR light */
		clrbits32(hw_gpio + HW_GPIO_OUT(0),
			  HW_GPIO_SLOT_LED | HW_GPIO_SENSOR_BAR);
	}
}

static void wii_restart(char *cmd)
{
	local_irq_disable();

	if (hw_ctrl) {
		/* clear the system reset pin to cause a reset */
		clrbits32(hw_ctrl + HW_CTRL_RESETS, HW_CTRL_RESETS_SYS);
	}
	wii_spin();
}

static void wii_power_off(void)
{
	local_irq_disable();

	if (hw_gpio) {
		/* make sure that the poweroff GPIO is configured as output */
		setbits32(hw_gpio + HW_GPIO_DIR(1), HW_GPIO_SHUTDOWN);

		/* drive the poweroff GPIO high */
		setbits32(hw_gpio + HW_GPIO_OUT(1), HW_GPIO_SHUTDOWN);
	}
	wii_spin();
}

static void wii_halt(void)
{
	if (ppc_md.restart)
		ppc_md.restart(NULL);
	wii_spin();
}

static void __init wii_init_early(void)
{
	ug_udbg_init();
}

static void __init wii_pic_probe(void)
{
	flipper_pic_probe();
	hlwd_pic_probe();
}

static int __init wii_probe(void)
{
	unsigned long dt_root;

	dt_root = of_get_flat_dt_root();
	if (!of_flat_dt_is_compatible(dt_root, "nintendo,wii"))
		return 0;

	return 1;
}

static void wii_shutdown(void)
{
	hlwd_quiesce();
	flipper_quiesce();
}

#ifdef CONFIG_KEXEC
static int wii_machine_kexec_prepare(struct kimage *image)
{
	return 0;
}
#endif /* CONFIG_KEXEC */

define_machine(wii) {
	.name			= "wii",
	.probe			= wii_probe,
	.init_early		= wii_init_early,
	.setup_arch		= wii_setup_arch,
	.restart		= wii_restart,
	.power_off		= wii_power_off,
	.halt			= wii_halt,
	.init_IRQ		= wii_pic_probe,
	.get_irq		= flipper_pic_get_irq,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
	.machine_shutdown	= wii_shutdown,
#ifdef CONFIG_KEXEC
	.machine_kexec_prepare	= wii_machine_kexec_prepare,
#endif
};

static struct of_device_id wii_of_bus[] = {
	{ .compatible = "nintendo,hollywood", },
	{ },
};

static int __init wii_device_probe(void)
{
	if (!machine_is(wii))
		return 0;

	of_platform_bus_probe(NULL, wii_of_bus, NULL);
	return 0;
}
device_initcall(wii_device_probe);

