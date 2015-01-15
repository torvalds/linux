/*
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Modified from mach-omap/omap2/board-generic.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/io.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/irqdomain.h>

#include <asm/mach/arch.h>

#include <mach/common.h>
#include <mach/cp_intc.h>
#include <mach/da8xx.h>

#define DA8XX_NUM_UARTS	3

static struct of_device_id da8xx_irq_match[] __initdata = {
	{ .compatible = "ti,cp-intc", .data = cp_intc_of_init, },
	{ }
};

static void __init da8xx_init_irq(void)
{
	of_irq_init(da8xx_irq_match);
}

static struct of_dev_auxdata da850_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("ti,davinci-i2c", 0x01c22000, "i2c_davinci.1", NULL),
	OF_DEV_AUXDATA("ti,davinci-wdt", 0x01c21000, "davinci-wdt", NULL),
	OF_DEV_AUXDATA("ti,da830-mmc", 0x01c40000, "da830-mmc.0", NULL),
	OF_DEV_AUXDATA("ti,da850-ehrpwm", 0x01f00000, "ehrpwm", NULL),
	OF_DEV_AUXDATA("ti,da850-ehrpwm", 0x01f02000, "ehrpwm", NULL),
	OF_DEV_AUXDATA("ti,da850-ecap", 0x01f06000, "ecap", NULL),
	OF_DEV_AUXDATA("ti,da850-ecap", 0x01f07000, "ecap", NULL),
	OF_DEV_AUXDATA("ti,da850-ecap", 0x01f08000, "ecap", NULL),
	OF_DEV_AUXDATA("ti,da830-spi", 0x01f0e000, "spi_davinci.1", NULL),
	OF_DEV_AUXDATA("ns16550a", 0x01c42000, "serial8250.0", NULL),
	OF_DEV_AUXDATA("ns16550a", 0x01d0c000, "serial8250.1", NULL),
	OF_DEV_AUXDATA("ns16550a", 0x01d0d000, "serial8250.2", NULL),
	OF_DEV_AUXDATA("ti,davinci_mdio", 0x01e24000, "davinci_mdio.0", NULL),
	OF_DEV_AUXDATA("ti,davinci-dm6467-emac", 0x01e20000, "davinci_emac.1",
		       NULL),
	OF_DEV_AUXDATA("ti,da830-mcasp-audio", 0x01d00000, "davinci-mcasp.0", NULL),
	{}
};

#ifdef CONFIG_ARCH_DAVINCI_DA850

static void __init da850_init_machine(void)
{
	of_platform_populate(NULL, of_default_bus_match_table,
			     da850_auxdata_lookup, NULL);

}

static const char *da850_boards_compat[] __initdata = {
	"enbw,cmc",
	"ti,da850-evm",
	"ti,da850",
	NULL,
};

DT_MACHINE_START(DA850_DT, "Generic DA850/OMAP-L138/AM18x")
	.map_io		= da850_init,
	.init_irq	= da8xx_init_irq,
	.init_time	= davinci_timer_init,
	.init_machine	= da850_init_machine,
	.dt_compat	= da850_boards_compat,
	.init_late	= davinci_init_late,
	.restart	= da8xx_restart,
MACHINE_END

#endif
