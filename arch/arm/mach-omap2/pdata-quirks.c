/*
 * Legacy platform_data quirks
 *
 * Copyright (C) 2013 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include "common.h"
#include "common-board-devices.h"
#include "dss-common.h"

struct pdata_init {
	const char *compatible;
	void (*fn)(void);
};

/*
 * Create alias for USB host PHY clock.
 * Remove this when clock phandle can be provided via DT
 */
static void __init __used legacy_init_ehci_clk(char *clkname)
{
	int ret;

	ret = clk_add_alias("main_clk", NULL, clkname, NULL);
	if (ret)
		pr_err("%s:Failed to add main_clk alias to %s :%d\n",
		       __func__, clkname, ret);
}

#ifdef CONFIG_ARCH_OMAP4
static void __init omap4_sdp_legacy_init(void)
{
	omap_4430sdp_display_init_of();
}

static void __init omap4_panda_legacy_init(void)
{
	omap4_panda_display_init_of();
	legacy_init_ehci_clk("auxclk3_ck");
}
#endif

#ifdef CONFIG_SOC_OMAP5
static void __init omap5_uevm_legacy_init(void)
{
	legacy_init_ehci_clk("auxclk1_ck");
}
#endif

static struct pdata_init pdata_quirks[] __initdata = {
#ifdef CONFIG_ARCH_OMAP4
	{ "ti,omap4-sdp", omap4_sdp_legacy_init, },
	{ "ti,omap4-panda", omap4_panda_legacy_init, },
#endif
#ifdef CONFIG_SOC_OMAP5
	{ "ti,omap5-uevm", omap5_uevm_legacy_init, },
#endif
	{ /* sentinel */ },
};

void __init pdata_quirks_init(void)
{
	struct pdata_init *quirks = pdata_quirks;

	while (quirks->compatible) {
		if (of_machine_is_compatible(quirks->compatible)) {
			if (quirks->fn)
				quirks->fn();
			break;
		}
		quirks++;
	}
}
