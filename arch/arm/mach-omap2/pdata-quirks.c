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
#include <linux/davinci_emac.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/wl12xx.h>

#include <linux/platform_data/pinctrl-single.h>

#include "am35xx.h"
#include "common.h"
#include "common-board-devices.h"
#include "dss-common.h"
#include "control.h"
#include "omap-secure.h"
#include "soc.h"

struct pdata_init {
	const char *compatible;
	void (*fn)(void);
};

struct of_dev_auxdata omap_auxdata_lookup[];
static struct twl4030_gpio_platform_data twl_gpio_auxdata;

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

#ifdef CONFIG_MACH_NOKIA_N8X0
static void __init omap2420_n8x0_legacy_init(void)
{
	omap_auxdata_lookup[0].platform_data = n8x0_legacy_init();
}
#else
#define omap2420_n8x0_legacy_init	NULL
#endif

#ifdef CONFIG_ARCH_OMAP3
static void __init hsmmc2_internal_input_clk(void)
{
	u32 reg;

	reg = omap_ctrl_readl(OMAP343X_CONTROL_DEVCONF1);
	reg |= OMAP2_MMCSDIO2ADPCLKISEL;
	omap_ctrl_writel(reg, OMAP343X_CONTROL_DEVCONF1);
}

static int omap3_sbc_t3730_twl_callback(struct device *dev,
					   unsigned gpio,
					   unsigned ngpio)
{
	int res;

	res = gpio_request_one(gpio + 2, GPIOF_OUT_INIT_HIGH,
			       "wlan rst");
	if (res)
		return res;

	gpio_export(gpio, 0);

	return 0;
}

static void __init omap3_sbc_t3730_twl_init(void)
{
	twl_gpio_auxdata.setup = omap3_sbc_t3730_twl_callback;
}

static void __init omap3_sbc_t3730_legacy_init(void)
{
	legacy_init_wl12xx(WL12XX_REFCLOCK_38, 0, 136);
	omap_ads7846_init(1, 57, 0, NULL);
}

static void __init omap3_igep0020_legacy_init(void)
{
	omap3_igep2_display_init_of();
}

static void __init omap3_evm_legacy_init(void)
{
	legacy_init_wl12xx(WL12XX_REFCLOCK_38, 0, 149);
}

static void __init omap3_zoom_legacy_init(void)
{
	legacy_init_wl12xx(WL12XX_REFCLOCK_26, 0, 162);
}

static void am35xx_enable_emac_int(void)
{
	u32 v;

	v = omap_ctrl_readl(AM35XX_CONTROL_LVL_INTR_CLEAR);
	v |= (AM35XX_CPGMAC_C0_RX_PULSE_CLR | AM35XX_CPGMAC_C0_TX_PULSE_CLR |
	      AM35XX_CPGMAC_C0_MISC_PULSE_CLR | AM35XX_CPGMAC_C0_RX_THRESH_CLR);
	omap_ctrl_writel(v, AM35XX_CONTROL_LVL_INTR_CLEAR);
	omap_ctrl_readl(AM35XX_CONTROL_LVL_INTR_CLEAR); /* OCP barrier */
}

static void am35xx_disable_emac_int(void)
{
	u32 v;

	v = omap_ctrl_readl(AM35XX_CONTROL_LVL_INTR_CLEAR);
	v |= (AM35XX_CPGMAC_C0_RX_PULSE_CLR | AM35XX_CPGMAC_C0_TX_PULSE_CLR);
	omap_ctrl_writel(v, AM35XX_CONTROL_LVL_INTR_CLEAR);
	omap_ctrl_readl(AM35XX_CONTROL_LVL_INTR_CLEAR); /* OCP barrier */
}

static struct emac_platform_data am35xx_emac_pdata = {
	.interrupt_enable	= am35xx_enable_emac_int,
	.interrupt_disable	= am35xx_disable_emac_int,
};

static void __init am3517_evm_legacy_init(void)
{
	u32 v;

	v = omap_ctrl_readl(AM35XX_CONTROL_IP_SW_RESET);
	v &= ~AM35XX_CPGMACSS_SW_RST;
	omap_ctrl_writel(v, AM35XX_CONTROL_IP_SW_RESET);
	omap_ctrl_readl(AM35XX_CONTROL_IP_SW_RESET); /* OCP barrier */
}

static void __init nokia_n900_legacy_init(void)
{
	hsmmc2_internal_input_clk();

	if (omap_type() == OMAP2_DEVICE_TYPE_SEC) {
		if (IS_ENABLED(CONFIG_ARM_ERRATA_430973)) {
			pr_info("RX-51: Enabling ARM errata 430973 workaround\n");
			/* set IBE to 1 */
			rx51_secure_update_aux_cr(BIT(6), 0);
		} else {
			pr_warning("RX-51: Not enabling ARM errata 430973 workaround\n");
			pr_warning("Thumb binaries may crash randomly without this workaround\n");
		}
	}
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

static struct pcs_pdata pcs_pdata;

void omap_pcs_legacy_init(int irq, void (*rearm)(void))
{
	pcs_pdata.irq = irq;
	pcs_pdata.rearm = rearm;
}

/*
 * GPIOs for TWL are initialized by the I2C bus and need custom
 * handing until DSS has device tree bindings.
 */
void omap_auxdata_legacy_init(struct device *dev)
{
	if (dev->platform_data)
		return;

	if (strcmp("twl4030-gpio", dev_name(dev)))
		return;

	dev->platform_data = &twl_gpio_auxdata;
}

/*
 * Few boards still need auxdata populated before we populate
 * the dev entries in of_platform_populate().
 */
static struct pdata_init auxdata_quirks[] __initdata = {
#ifdef CONFIG_SOC_OMAP2420
	{ "nokia,n800", omap2420_n8x0_legacy_init, },
	{ "nokia,n810", omap2420_n8x0_legacy_init, },
	{ "nokia,n810-wimax", omap2420_n8x0_legacy_init, },
#endif
#ifdef CONFIG_ARCH_OMAP3
	{ "compulab,omap3-sbc-t3730", omap3_sbc_t3730_twl_init, },
#endif
	{ /* sentinel */ },
};

struct of_dev_auxdata omap_auxdata_lookup[] __initdata = {
#ifdef CONFIG_MACH_NOKIA_N8X0
	OF_DEV_AUXDATA("ti,omap2420-mmc", 0x4809c000, "mmci-omap.0", NULL),
#endif
#ifdef CONFIG_ARCH_OMAP3
	OF_DEV_AUXDATA("ti,omap3-padconf", 0x48002030, "48002030.pinmux", &pcs_pdata),
	OF_DEV_AUXDATA("ti,omap3-padconf", 0x480025a0, "480025a0.pinmux", &pcs_pdata),
	OF_DEV_AUXDATA("ti,omap3-padconf", 0x48002a00, "48002a00.pinmux", &pcs_pdata),
	/* Only on am3517 */
	OF_DEV_AUXDATA("ti,davinci_mdio", 0x5c030000, "davinci_mdio.0", NULL),
	OF_DEV_AUXDATA("ti,am3517-emac", 0x5c000000, "davinci_emac.0",
		       &am35xx_emac_pdata),
#endif
#ifdef CONFIG_ARCH_OMAP4
	OF_DEV_AUXDATA("ti,omap4-padconf", 0x4a100040, "4a100040.pinmux", &pcs_pdata),
	OF_DEV_AUXDATA("ti,omap4-padconf", 0x4a31e040, "4a31e040.pinmux", &pcs_pdata),
#endif
	{ /* sentinel */ },
};

/*
 * Few boards still need to initialize some legacy devices with
 * platform data until the drivers support device tree.
 */
static struct pdata_init pdata_quirks[] __initdata = {
#ifdef CONFIG_ARCH_OMAP3
	{ "compulab,omap3-sbc-t3730", omap3_sbc_t3730_legacy_init, },
	{ "nokia,omap3-n900", nokia_n900_legacy_init, },
	{ "nokia,omap3-n9", hsmmc2_internal_input_clk, },
	{ "nokia,omap3-n950", hsmmc2_internal_input_clk, },
	{ "isee,omap3-igep0020", omap3_igep0020_legacy_init, },
	{ "ti,omap3-evm-37xx", omap3_evm_legacy_init, },
	{ "ti,omap3-zoom3", omap3_zoom_legacy_init, },
	{ "ti,am3517-evm", am3517_evm_legacy_init, },
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

static void pdata_quirks_check(struct pdata_init *quirks)
{
	while (quirks->compatible) {
		if (of_machine_is_compatible(quirks->compatible)) {
			if (quirks->fn)
				quirks->fn();
			break;
		}
		quirks++;
	}
}

void __init pdata_quirks_init(struct of_device_id *omap_dt_match_table)
{
	omap_sdrc_init(NULL, NULL);
	pdata_quirks_check(auxdata_quirks);
	of_platform_populate(NULL, omap_dt_match_table,
			     omap_auxdata_lookup, NULL);
	pdata_quirks_check(pdata_quirks);
}
