// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2010-2011 Calxeda, Inc.
 */
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clocksource.h>
#include <linux/dma-mapping.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/pl320-ipc.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/reboot.h>
#include <linux/amba/bus.h>
#include <linux/platform_device.h>
#include <linux/psci.h>

#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "core.h"
#include "sysregs.h"

void __iomem *sregs_base;
void __iomem *scu_base_addr;

static void __init highbank_scu_map_io(void)
{
	unsigned long base;

	/* Get SCU base */
	asm("mrc p15, 4, %0, c15, c0, 0" : "=r" (base));

	scu_base_addr = ioremap(base, SZ_4K);
}


static void highbank_l2c310_write_sec(unsigned long val, unsigned reg)
{
	if (reg == L2X0_CTRL)
		highbank_smc1(0x102, val);
	else
		WARN_ONCE(1, "Highbank L2C310: ignoring write to reg 0x%x\n",
			  reg);
}

static void __init highbank_init_irq(void)
{
	irqchip_init();

	if (of_find_compatible_node(NULL, NULL, "arm,cortex-a9"))
		highbank_scu_map_io();
}

static void highbank_power_off(void)
{
	highbank_set_pwr_shutdown();

	while (1)
		cpu_do_idle();
}

static int highbank_platform_notifier(struct notifier_block *nb,
				  unsigned long event, void *__dev)
{
	struct resource *res;
	int reg = -1;
	u32 val;
	struct device *dev = __dev;

	if (event != BUS_NOTIFY_ADD_DEVICE)
		return NOTIFY_DONE;

	if (of_device_is_compatible(dev->of_node, "calxeda,hb-ahci"))
		reg = 0xc;
	else if (of_device_is_compatible(dev->of_node, "calxeda,hb-sdhci"))
		reg = 0x18;
	else if (of_device_is_compatible(dev->of_node, "arm,pl330"))
		reg = 0x20;
	else if (of_device_is_compatible(dev->of_node, "calxeda,hb-xgmac")) {
		res = platform_get_resource(to_platform_device(dev),
					    IORESOURCE_MEM, 0);
		if (res) {
			if (res->start == 0xfff50000)
				reg = 0;
			else if (res->start == 0xfff51000)
				reg = 4;
		}
	}

	if (reg < 0)
		return NOTIFY_DONE;

	if (of_property_read_bool(dev->of_node, "dma-coherent")) {
		val = readl(sregs_base + reg);
		writel(val | 0xff01, sregs_base + reg);
		set_dma_ops(dev, &arm_coherent_dma_ops);
	}

	return NOTIFY_OK;
}

static struct notifier_block highbank_amba_nb = {
	.notifier_call = highbank_platform_notifier,
};

static struct notifier_block highbank_platform_nb = {
	.notifier_call = highbank_platform_notifier,
};

static struct platform_device highbank_cpuidle_device = {
	.name = "cpuidle-calxeda",
};

static int hb_keys_notifier(struct notifier_block *nb, unsigned long event, void *data)
{
	u32 key = *(u32 *)data;

	if (event != 0x1000)
		return 0;

	if (key == KEY_POWER)
		orderly_poweroff(false);
	else if (key == 0xffff)
		ctrl_alt_del();

	return 0;
}
static struct notifier_block hb_keys_nb = {
	.notifier_call = hb_keys_notifier,
};

static void __init highbank_init(void)
{
	struct device_node *np;

	/* Map system registers */
	np = of_find_compatible_node(NULL, NULL, "calxeda,hb-sregs");
	sregs_base = of_iomap(np, 0);
	WARN_ON(!sregs_base);

	pm_power_off = highbank_power_off;
	highbank_pm_init();

	bus_register_notifier(&platform_bus_type, &highbank_platform_nb);
	bus_register_notifier(&amba_bustype, &highbank_amba_nb);

	pl320_ipc_register_notifier(&hb_keys_nb);

	if (psci_ops.cpu_suspend)
		platform_device_register(&highbank_cpuidle_device);
}

static const char *const highbank_match[] __initconst = {
	"calxeda,highbank",
	"calxeda,ecx-2000",
	NULL,
};

DT_MACHINE_START(HIGHBANK, "Highbank")
#if defined(CONFIG_ZONE_DMA) && defined(CONFIG_ARM_LPAE)
	.dma_zone_size	= (4ULL * SZ_1G),
#endif
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
	.l2c_write_sec	= highbank_l2c310_write_sec,
	.init_irq	= highbank_init_irq,
	.init_machine	= highbank_init,
	.dt_compat	= highbank_match,
	.restart	= highbank_restart,
MACHINE_END
