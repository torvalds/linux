// SPDX-License-Identifier: GPL-2.0-only
/*
 * Legacy platform_data quirks
 *
 * Copyright (C) 2013 Texas Instruments
 */
#include <linux/clk.h>
#include <linux/davinci_emac.h>
#include <linux/gpio/machine.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/power/smartreflex.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>

#include <linux/platform_data/pinctrl-single.h>
#include <linux/platform_data/hsmmc-omap.h>
#include <linux/platform_data/iommu-omap.h>
#include <linux/platform_data/ti-sysc.h>
#include <linux/platform_data/wkup_m3.h>
#include <linux/platform_data/asoc-ti-mcbsp.h>
#include <linux/platform_data/ti-prm.h>

#include "clockdomain.h"
#include "common.h"
#include "common-board-devices.h"
#include "control.h"
#include "omap_device.h"
#include "omap-secure.h"
#include "soc.h"

static struct omap_hsmmc_platform_data __maybe_unused mmc_pdata[2];

struct pdata_init {
	const char *compatible;
	void (*fn)(void);
};

static struct of_dev_auxdata omap_auxdata_lookup[];

#ifdef CONFIG_MACH_NOKIA_N8X0
static void __init omap2420_n8x0_legacy_init(void)
{
	omap_auxdata_lookup[0].platform_data = n8x0_legacy_init();
}
#else
#define omap2420_n8x0_legacy_init	NULL
#endif

#ifdef CONFIG_ARCH_OMAP3
/*
 * Configures GPIOs 126, 127 and 129 to 1.8V mode instead of 3.0V
 * mode for MMC1 in case bootloader did not configure things.
 * Note that if the pins are used for MMC1, pbias-regulator
 * manages the IO voltage.
 */
static void __init omap3_gpio126_127_129(void)
{
	u32 reg;

	reg = omap_ctrl_readl(OMAP343X_CONTROL_PBIAS_LITE);
	reg &= ~OMAP343X_PBIASLITEVMODE1;
	reg |= OMAP343X_PBIASLITEPWRDNZ1;
	omap_ctrl_writel(reg, OMAP343X_CONTROL_PBIAS_LITE);
	if (cpu_is_omap3630()) {
		reg = omap_ctrl_readl(OMAP34XX_CONTROL_WKUP_CTRL);
		reg |= OMAP36XX_GPIO_IO_PWRDNZ;
		omap_ctrl_writel(reg, OMAP34XX_CONTROL_WKUP_CTRL);
	}
}

static void __init hsmmc2_internal_input_clk(void)
{
	u32 reg;

	reg = omap_ctrl_readl(OMAP343X_CONTROL_DEVCONF1);
	reg |= OMAP2_MMCSDIO2ADPCLKISEL;
	omap_ctrl_writel(reg, OMAP343X_CONTROL_DEVCONF1);
}

#ifdef CONFIG_OMAP_HWMOD
static struct iommu_platform_data omap3_iommu_pdata = {
	.reset_name = "mmu",
	.assert_reset = omap_device_assert_hardreset,
	.deassert_reset = omap_device_deassert_hardreset,
	.device_enable = omap_device_enable,
	.device_idle = omap_device_idle,
};

static struct iommu_platform_data omap3_iommu_isp_pdata = {
	.device_enable = omap_device_enable,
	.device_idle = omap_device_idle,
};
#endif

static void __init omap3_sbc_t3x_usb_hub_init(char *hub_name, int idx)
{
	struct gpio_desc *d;

	/* This asserts the RESET line (reverse polarity) */
	d = gpiod_get_index(NULL, "reset", idx, GPIOD_OUT_HIGH);
	if (IS_ERR(d)) {
		pr_err("Unable to get T3x USB reset GPIO descriptor\n");
		return;
	}
	gpiod_set_consumer_name(d, hub_name);
	gpiod_export(d, 0);
	udelay(10);
	/* De-assert RESET */
	gpiod_set_value(d, 0);
	msleep(1);
}

static struct gpiod_lookup_table omap3_sbc_t3x_usb_gpio_table = {
	.dev_id = NULL,
	.table = {
		GPIO_LOOKUP_IDX("gpio-160-175", 7, "reset", 0,
				GPIO_ACTIVE_LOW),
		{ }
	},
};

static void __init omap3_sbc_t3730_legacy_init(void)
{
	gpiod_add_lookup_table(&omap3_sbc_t3x_usb_gpio_table);
	omap3_sbc_t3x_usb_hub_init("sb-t35 usb hub", 0);
}

static void __init omap3_sbc_t3530_legacy_init(void)
{
	gpiod_add_lookup_table(&omap3_sbc_t3x_usb_gpio_table);
	omap3_sbc_t3x_usb_hub_init("sb-t35 usb hub", 0);
}

static void __init omap3_evm_legacy_init(void)
{
	hsmmc2_internal_input_clk();
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

static void __init am35xx_emac_reset(void)
{
	u32 v;

	v = omap_ctrl_readl(AM35XX_CONTROL_IP_SW_RESET);
	v &= ~AM35XX_CPGMACSS_SW_RST;
	omap_ctrl_writel(v, AM35XX_CONTROL_IP_SW_RESET);
	omap_ctrl_readl(AM35XX_CONTROL_IP_SW_RESET); /* OCP barrier */
}

static struct gpiod_lookup_table cm_t3517_wlan_gpio_table = {
	.dev_id = NULL,
	.table = {
		GPIO_LOOKUP("gpio-48-53", 8, "power",
			    GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("gpio-0-15", 4, "noe",
			    GPIO_ACTIVE_HIGH),
		{ }
	},
};

static void __init omap3_sbc_t3517_wifi_init(void)
{
	struct gpio_desc *d;

	gpiod_add_lookup_table(&cm_t3517_wlan_gpio_table);

	/* This asserts the RESET line (reverse polarity) */
	d = gpiod_get(NULL, "power", GPIOD_OUT_HIGH);
	if (IS_ERR(d)) {
		pr_err("Unable to get CM T3517 WLAN power GPIO descriptor\n");
	} else {
		gpiod_set_consumer_name(d, "wlan pwr");
		gpiod_export(d, 0);
	}

	d = gpiod_get(NULL, "noe", GPIOD_OUT_HIGH);
	if (IS_ERR(d)) {
		pr_err("Unable to get CM T3517 WLAN XCVR NOE GPIO descriptor\n");
	} else {
		gpiod_set_consumer_name(d, "xcvr noe");
		gpiod_export(d, 0);
	}
	msleep(100);
	gpiod_set_value(d, 0);
}

static struct gpiod_lookup_table omap3_sbc_t3517_usb_gpio_table = {
	.dev_id = NULL,
	.table = {
		GPIO_LOOKUP_IDX("gpio-144-159", 8, "reset", 0,
				GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("gpio-96-111", 2, "reset", 1,
				GPIO_ACTIVE_LOW),
		{ }
	},
};

static void __init omap3_sbc_t3517_legacy_init(void)
{
	gpiod_add_lookup_table(&omap3_sbc_t3517_usb_gpio_table);
	omap3_sbc_t3x_usb_hub_init("cm-t3517 usb hub", 0);
	omap3_sbc_t3x_usb_hub_init("sb-t35 usb hub", 1);
	am35xx_emac_reset();
	hsmmc2_internal_input_clk();
	omap3_sbc_t3517_wifi_init();
}

static void __init am3517_evm_legacy_init(void)
{
	am35xx_emac_reset();
}

static void __init nokia_n900_legacy_init(void)
{
	hsmmc2_internal_input_clk();
	mmc_pdata[0].name = "external";
	mmc_pdata[1].name = "internal";

	if (omap_type() != OMAP2_DEVICE_TYPE_GP) {
		if (IS_ENABLED(CONFIG_ARM_ERRATA_430973)) {
			pr_info("RX-51: Enabling ARM errata 430973 workaround\n");
			/* set IBE to 1 */
			rx51_secure_update_aux_cr(BIT(6), 0);
		} else {
			pr_warn("RX-51: Not enabling ARM errata 430973 workaround\n");
			pr_warn("Thumb binaries may crash randomly without this workaround\n");
		}
	}
}

static void __init omap3_tao3530_legacy_init(void)
{
	hsmmc2_internal_input_clk();
}

static void __init omap3_logicpd_torpedo_init(void)
{
	omap3_gpio126_127_129();
}

/* omap3pandora legacy devices */

static struct platform_device pandora_backlight = {
	.name	= "pandora-backlight",
	.id	= -1,
};

static struct gpiod_lookup_table pandora_soc_audio_gpios = {
	.dev_id = "soc-audio",
	.table = {
		GPIO_LOOKUP("gpio-112-127", 6, "dac", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("gpio-0-15", 14, "amp", GPIO_ACTIVE_HIGH),
		{ }
	},
};

static void __init omap3_pandora_legacy_init(void)
{
	platform_device_register(&pandora_backlight);
	gpiod_add_lookup_table(&pandora_soc_audio_gpios);
}
#endif /* CONFIG_ARCH_OMAP3 */

#ifdef CONFIG_SOC_DRA7XX
static struct iommu_platform_data dra7_ipu1_dsp_iommu_pdata = {
	.set_pwrdm_constraint = omap_iommu_set_pwrdm_constraint,
};
#endif

static struct clockdomain *ti_sysc_find_one_clockdomain(struct clk *clk)
{
	struct clk_hw *hw = __clk_get_hw(clk);
	struct clockdomain *clkdm = NULL;
	struct clk_hw_omap *hwclk;

	hwclk = to_clk_hw_omap(hw);
	if (!omap2_clk_is_hw_omap(hw))
		return NULL;

	if (hwclk && hwclk->clkdm_name)
		clkdm = clkdm_lookup(hwclk->clkdm_name);

	return clkdm;
}

/**
 * ti_sysc_clkdm_init - find clockdomain based on clock
 * @fck: device functional clock
 * @ick: device interface clock
 * @dev: struct device
 *
 * Populate clockdomain based on clock. It is needed for
 * clkdm_deny_idle() and clkdm_allow_idle() for blocking clockdomain
 * clockdomain idle during reset, enable and idle.
 *
 * Note that we assume interconnect driver manages the clocks
 * and do not need to populate oh->_clk for dynamically
 * allocated modules.
 */
static int ti_sysc_clkdm_init(struct device *dev,
			      struct clk *fck, struct clk *ick,
			      struct ti_sysc_cookie *cookie)
{
	if (!IS_ERR(fck))
		cookie->clkdm = ti_sysc_find_one_clockdomain(fck);
	if (cookie->clkdm)
		return 0;
	if (!IS_ERR(ick))
		cookie->clkdm = ti_sysc_find_one_clockdomain(ick);
	if (cookie->clkdm)
		return 0;

	return -ENODEV;
}

static void ti_sysc_clkdm_deny_idle(struct device *dev,
				    const struct ti_sysc_cookie *cookie)
{
	if (cookie->clkdm)
		clkdm_deny_idle(cookie->clkdm);
}

static void ti_sysc_clkdm_allow_idle(struct device *dev,
				     const struct ti_sysc_cookie *cookie)
{
	if (cookie->clkdm)
		clkdm_allow_idle(cookie->clkdm);
}

#ifdef CONFIG_OMAP_HWMOD
static int ti_sysc_enable_module(struct device *dev,
				 const struct ti_sysc_cookie *cookie)
{
	if (!cookie->data)
		return -EINVAL;

	return omap_hwmod_enable(cookie->data);
}

static int ti_sysc_idle_module(struct device *dev,
			       const struct ti_sysc_cookie *cookie)
{
	if (!cookie->data)
		return -EINVAL;

	return omap_hwmod_idle(cookie->data);
}

static int ti_sysc_shutdown_module(struct device *dev,
				   const struct ti_sysc_cookie *cookie)
{
	if (!cookie->data)
		return -EINVAL;

	return omap_hwmod_shutdown(cookie->data);
}
#endif	/* CONFIG_OMAP_HWMOD */

static bool ti_sysc_soc_type_gp(void)
{
	return omap_type() == OMAP2_DEVICE_TYPE_GP;
}

static struct of_dev_auxdata omap_auxdata_lookup[];

static struct ti_sysc_platform_data ti_sysc_pdata = {
	.auxdata = omap_auxdata_lookup,
	.soc_type_gp = ti_sysc_soc_type_gp,
	.init_clockdomain = ti_sysc_clkdm_init,
	.clkdm_deny_idle = ti_sysc_clkdm_deny_idle,
	.clkdm_allow_idle = ti_sysc_clkdm_allow_idle,
#ifdef CONFIG_OMAP_HWMOD
	.init_module = omap_hwmod_init_module,
	.enable_module = ti_sysc_enable_module,
	.idle_module = ti_sysc_idle_module,
	.shutdown_module = ti_sysc_shutdown_module,
#endif
};

static struct pcs_pdata pcs_pdata;

void omap_pcs_legacy_init(int irq, void (*rearm)(void))
{
	pcs_pdata.irq = irq;
	pcs_pdata.rearm = rearm;
}

static struct ti_prm_platform_data ti_prm_pdata = {
	.clkdm_deny_idle = clkdm_deny_idle,
	.clkdm_allow_idle = clkdm_allow_idle,
	.clkdm_lookup = clkdm_lookup,
};

#if defined(CONFIG_ARCH_OMAP3) && IS_ENABLED(CONFIG_SND_SOC_OMAP_MCBSP)
static struct omap_mcbsp_platform_data mcbsp_pdata;
static void __init omap3_mcbsp_init(void)
{
	omap3_mcbsp_init_pdata_callback(&mcbsp_pdata);
}
#else
static void __init omap3_mcbsp_init(void) {}
#endif

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
	{ /* sentinel */ },
};

struct omap_sr_data __maybe_unused omap_sr_pdata[OMAP_SR_NR];

static struct of_dev_auxdata omap_auxdata_lookup[] = {
#ifdef CONFIG_MACH_NOKIA_N8X0
	OF_DEV_AUXDATA("ti,omap2420-mmc", 0x4809c000, "mmci-omap.0", NULL),
	OF_DEV_AUXDATA("menelaus", 0x72, "1-0072", &n8x0_menelaus_platform_data),
#endif
#ifdef CONFIG_ARCH_OMAP3
	OF_DEV_AUXDATA("ti,omap2-iommu", 0x5d000000, "5d000000.mmu",
		       &omap3_iommu_pdata),
	OF_DEV_AUXDATA("ti,omap2-iommu", 0x480bd400, "480bd400.mmu",
		       &omap3_iommu_isp_pdata),
	OF_DEV_AUXDATA("ti,omap3-smartreflex-core", 0x480cb000,
		       "480cb000.smartreflex", &omap_sr_pdata[OMAP_SR_CORE]),
	OF_DEV_AUXDATA("ti,omap3-smartreflex-mpu-iva", 0x480c9000,
		       "480c9000.smartreflex", &omap_sr_pdata[OMAP_SR_MPU]),
	OF_DEV_AUXDATA("ti,omap3-hsmmc", 0x4809c000, "4809c000.mmc", &mmc_pdata[0]),
	OF_DEV_AUXDATA("ti,omap3-hsmmc", 0x480b4000, "480b4000.mmc", &mmc_pdata[1]),
	/* Only on am3517 */
	OF_DEV_AUXDATA("ti,davinci_mdio", 0x5c030000, "davinci_mdio.0", NULL),
	OF_DEV_AUXDATA("ti,am3517-emac", 0x5c000000, "davinci_emac.0",
		       &am35xx_emac_pdata),
	OF_DEV_AUXDATA("nokia,n900-rom-rng", 0, NULL, rx51_secure_rng_call),
	/* McBSP modules with sidetone core */
#if IS_ENABLED(CONFIG_SND_SOC_OMAP_MCBSP)
	OF_DEV_AUXDATA("ti,omap3-mcbsp", 0x49022000, "49022000.mcbsp", &mcbsp_pdata),
	OF_DEV_AUXDATA("ti,omap3-mcbsp", 0x49024000, "49024000.mcbsp", &mcbsp_pdata),
#endif
#endif
#if defined(CONFIG_ARCH_OMAP4) || defined(CONFIG_SOC_OMAP5)
	OF_DEV_AUXDATA("ti,omap4-smartreflex-iva", 0x4a0db000,
		       "4a0db000.smartreflex", &omap_sr_pdata[OMAP_SR_IVA]),
	OF_DEV_AUXDATA("ti,omap4-smartreflex-core", 0x4a0dd000,
		       "4a0dd000.smartreflex", &omap_sr_pdata[OMAP_SR_CORE]),
	OF_DEV_AUXDATA("ti,omap4-smartreflex-mpu", 0x4a0d9000,
		       "4a0d9000.smartreflex", &omap_sr_pdata[OMAP_SR_MPU]),
#endif
#ifdef CONFIG_SOC_DRA7XX
	OF_DEV_AUXDATA("ti,dra7-dsp-iommu", 0x40d01000, "40d01000.mmu",
		       &dra7_ipu1_dsp_iommu_pdata),
	OF_DEV_AUXDATA("ti,dra7-dsp-iommu", 0x41501000, "41501000.mmu",
		       &dra7_ipu1_dsp_iommu_pdata),
	OF_DEV_AUXDATA("ti,dra7-iommu", 0x58882000, "58882000.mmu",
		       &dra7_ipu1_dsp_iommu_pdata),
#endif
	/* Common auxdata */
	OF_DEV_AUXDATA("simple-pm-bus", 0, NULL, omap_auxdata_lookup),
	OF_DEV_AUXDATA("ti,sysc", 0, NULL, &ti_sysc_pdata),
	OF_DEV_AUXDATA("pinctrl-single", 0, NULL, &pcs_pdata),
	OF_DEV_AUXDATA("ti,omap-prm-inst", 0, NULL, &ti_prm_pdata),
	OF_DEV_AUXDATA("ti,omap-sdma", 0, NULL, &dma_plat_info),
	{ /* sentinel */ },
};

/*
 * Few boards still need to initialize some legacy devices with
 * platform data until the drivers support device tree.
 */
static struct pdata_init pdata_quirks[] __initdata = {
#ifdef CONFIG_ARCH_OMAP3
	{ "compulab,omap3-sbc-t3517", omap3_sbc_t3517_legacy_init, },
	{ "compulab,omap3-sbc-t3530", omap3_sbc_t3530_legacy_init, },
	{ "compulab,omap3-sbc-t3730", omap3_sbc_t3730_legacy_init, },
	{ "nokia,omap3-n900", nokia_n900_legacy_init, },
	{ "nokia,omap3-n9", hsmmc2_internal_input_clk, },
	{ "nokia,omap3-n950", hsmmc2_internal_input_clk, },
	{ "logicpd,dm3730-torpedo-devkit", omap3_logicpd_torpedo_init, },
	{ "ti,omap3-evm-37xx", omap3_evm_legacy_init, },
	{ "ti,am3517-evm", am3517_evm_legacy_init, },
	{ "technexion,omap3-tao3530", omap3_tao3530_legacy_init, },
	{ "openpandora,omap3-pandora-600mhz", omap3_pandora_legacy_init, },
	{ "openpandora,omap3-pandora-1ghz", omap3_pandora_legacy_init, },
#endif
	{ /* sentinel */ },
};

static void pdata_quirks_check(struct pdata_init *quirks)
{
	while (quirks->compatible) {
		if (of_machine_is_compatible(quirks->compatible)) {
			if (quirks->fn)
				quirks->fn();
		}
		quirks++;
	}
}

static const char * const pdata_quirks_init_nodes[] = {
	"prcm",
	"prm",
};

static void __init
pdata_quirks_init_clocks(const struct of_device_id *omap_dt_match_table)
{
	struct device_node *np;
	int i;

	for (i = 0; i < ARRAY_SIZE(pdata_quirks_init_nodes); i++) {
		np = of_find_node_by_name(NULL, pdata_quirks_init_nodes[i]);
		if (!np)
			continue;

		of_platform_populate(np, omap_dt_match_table,
				     omap_auxdata_lookup, NULL);

		of_node_put(np);
	}
}

void __init pdata_quirks_init(const struct of_device_id *omap_dt_match_table)
{
	/*
	 * We still need this for omap2420 and omap3 PM to work, others are
	 * using drivers/misc/sram.c already.
	 */
	if (of_machine_is_compatible("ti,omap2420") ||
	    of_machine_is_compatible("ti,omap3"))
		omap_sdrc_init(NULL, NULL);

	if (of_machine_is_compatible("ti,omap3"))
		omap3_mcbsp_init();
	pdata_quirks_check(auxdata_quirks);

	pdata_quirks_init_clocks(omap_dt_match_table);

	of_platform_populate(NULL, omap_dt_match_table,
			     omap_auxdata_lookup, NULL);
	pdata_quirks_check(pdata_quirks);
}
