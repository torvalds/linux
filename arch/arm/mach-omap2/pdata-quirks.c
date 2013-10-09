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
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/wl12xx.h>

#include "common.h"
#include "common-board-devices.h"
#include "dss-common.h"
#include "control.h"

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

#if IS_ENABLED(CONFIG_WL12XX)

static struct wl12xx_platform_data wl12xx __initdata;

static void __init __used legacy_init_wl12xx(unsigned ref_clock,
					     unsigned tcxo_clock,
					     int gpio)
{
	int res;

	wl12xx.board_ref_clock = ref_clock;
	wl12xx.board_tcxo_clock = tcxo_clock;
	wl12xx.irq = gpio_to_irq(gpio);

	res = wl12xx_set_platform_data(&wl12xx);
	if (res) {
		pr_err("error setting wl12xx data: %d\n", res);
		return;
	}
}
#else
static inline void legacy_init_wl12xx(unsigned ref_clock,
				      unsigned tcxo_clock,
				      int gpio)
{
}
#endif

#ifdef CONFIG_ARCH_OMAP3
static void __init hsmmc2_internal_input_clk(void)
{
	u32 reg;

	reg = omap_ctrl_readl(OMAP343X_CONTROL_DEVCONF1);
	reg |= OMAP2_MMCSDIO2ADPCLKISEL;
	omap_ctrl_writel(reg, OMAP343X_CONTROL_DEVCONF1);
}

static void __init omap3_igep0020_legacy_init(void)
{
	omap3_igep2_display_init_of();
}
#endif /* CONFIG_ARCH_OMAP3 */

#ifdef CONFIG_ARCH_OMAP4
static void __init omap4_sdp_legacy_init(void)
{
	omap_4430sdp_display_init_of();
	legacy_init_wl12xx(WL12XX_REFCLOCK_26,
			   WL12XX_TCXOCLOCK_26, 53);
}

static void __init omap4_panda_legacy_init(void)
{
	omap4_panda_display_init_of();
	legacy_init_ehci_clk("auxclk3_ck");
	legacy_init_wl12xx(WL12XX_REFCLOCK_38, 0, 53);
}
#endif

#ifdef CONFIG_SOC_OMAP5
static void __init omap5_uevm_legacy_init(void)
{
	legacy_init_ehci_clk("auxclk1_ck");
}
#endif

static struct pdata_init pdata_quirks[] __initdata = {
#ifdef CONFIG_ARCH_OMAP3
	{ "nokia,omap3-n9", hsmmc2_internal_input_clk, },
	{ "nokia,omap3-n950", hsmmc2_internal_input_clk, },
	{ "isee,omap3-igep0020", omap3_igep0020_legacy_init, },
#endif
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
