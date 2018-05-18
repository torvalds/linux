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
#include <linux/platform_data/ti-aemif.h>

#include <asm/mach/arch.h>

#include <mach/common.h>
#include "cp_intc.h"
#include <mach/da8xx.h>

static struct of_dev_auxdata da850_aemif_auxdata_lookup[] = {
	OF_DEV_AUXDATA("ti,davinci-nand", 0x62000000, "davinci-nand.0", NULL),
	{}
};

static struct aemif_platform_data aemif_data = {
	.dev_lookup = da850_aemif_auxdata_lookup,
};

static struct of_dev_auxdata da850_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("ti,davinci-i2c", 0x01c22000, "i2c_davinci.1", NULL),
	OF_DEV_AUXDATA("ti,davinci-i2c", 0x01e28000, "i2c_davinci.2", NULL),
	OF_DEV_AUXDATA("ti,davinci-wdt", 0x01c21000, "davinci-wdt", NULL),
	OF_DEV_AUXDATA("ti,da830-mmc", 0x01c40000, "da830-mmc.0", NULL),
	OF_DEV_AUXDATA("ti,da850-ehrpwm", 0x01f00000, "ehrpwm.0", NULL),
	OF_DEV_AUXDATA("ti,da850-ehrpwm", 0x01f02000, "ehrpwm.1", NULL),
	OF_DEV_AUXDATA("ti,da850-ecap", 0x01f06000, "ecap.0", NULL),
	OF_DEV_AUXDATA("ti,da850-ecap", 0x01f07000, "ecap.1", NULL),
	OF_DEV_AUXDATA("ti,da850-ecap", 0x01f08000, "ecap.2", NULL),
	OF_DEV_AUXDATA("ti,da830-spi", 0x01c41000, "spi_davinci.0", NULL),
	OF_DEV_AUXDATA("ti,da830-spi", 0x01f0e000, "spi_davinci.1", NULL),
	OF_DEV_AUXDATA("ns16550a", 0x01c42000, "serial8250.0", NULL),
	OF_DEV_AUXDATA("ns16550a", 0x01d0c000, "serial8250.1", NULL),
	OF_DEV_AUXDATA("ns16550a", 0x01d0d000, "serial8250.2", NULL),
	OF_DEV_AUXDATA("ti,davinci_mdio", 0x01e24000, "davinci_mdio.0", NULL),
	OF_DEV_AUXDATA("ti,davinci-dm6467-emac", 0x01e20000, "davinci_emac.1",
		       NULL),
	OF_DEV_AUXDATA("ti,da830-mcasp-audio", 0x01d00000, "davinci-mcasp.0", NULL),
	OF_DEV_AUXDATA("ti,da850-aemif", 0x68000000, "ti-aemif", &aemif_data),
	OF_DEV_AUXDATA("ti,da850-tilcdc", 0x01e13000, "da8xx_lcdc.0", NULL),
	OF_DEV_AUXDATA("ti,da830-ohci", 0x01e25000, "ohci-da8xx", NULL),
	OF_DEV_AUXDATA("ti,da830-musb", 0x01e00000, "musb-da8xx", NULL),
	OF_DEV_AUXDATA("ti,da830-usb-phy", 0x01c1417c, "da8xx-usb-phy", NULL),
	OF_DEV_AUXDATA("ti,da850-ahci", 0x01e18000, "ahci_da850", NULL),
	OF_DEV_AUXDATA("ti,da850-vpif", 0x01e17000, "vpif", NULL),
	OF_DEV_AUXDATA("ti,da850-dsp", 0x11800000, "davinci-rproc.0", NULL),
	{}
};

#ifdef CONFIG_ARCH_DAVINCI_DA850

static void __init da850_init_machine(void)
{
	/* All existing boards use 100MHz SATA refclkpn */
	static const unsigned long sata_refclkpn = 100 * 1000 * 1000;

	int ret;

	da850_register_clocks();

	ret = da8xx_register_usb20_phy_clk(false);
	if (ret)
		pr_warn("%s: registering USB 2.0 PHY clock failed: %d",
			__func__, ret);
	ret = da8xx_register_usb11_phy_clk(false);
	if (ret)
		pr_warn("%s: registering USB 1.1 PHY clock failed: %d",
			__func__, ret);

	ret = da850_register_sata_refclk(sata_refclkpn);
	if (ret)
		pr_warn("%s: registering SATA REFCLK failed: %d",
			__func__, ret);

	of_platform_default_populate(NULL, da850_auxdata_lookup, NULL);
	davinci_pm_init();
	pdata_quirks_init();
}

static const char *const da850_boards_compat[] __initconst = {
	"enbw,cmc",
	"ti,da850-lcdk",
	"ti,da850-evm",
	"ti,da850",
	NULL,
};

DT_MACHINE_START(DA850_DT, "Generic DA850/OMAP-L138/AM18x")
	.map_io		= da850_init,
	.init_time	= da850_init_time,
	.init_machine	= da850_init_machine,
	.dt_compat	= da850_boards_compat,
	.init_late	= davinci_init_late,
MACHINE_END

#endif
