// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * arch/arm/mach-at91/pm.c
 * AT91 Power Management
 *
 * Copyright (C) 2005 David Brownell
 */

#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/parser.h>
#include <linux/suspend.h>

#include <linux/clk.h>
#include <linux/clk/at91_pmc.h>
#include <linux/platform_data/atmel.h>

#include <asm/cacheflush.h>
#include <asm/fncpy.h>
#include <asm/system_misc.h>
#include <asm/suspend.h>

#include "generic.h"
#include "pm.h"
#include "sam_secure.h"

#define BACKUP_DDR_PHY_CALIBRATION	(9)

/**
 * struct at91_pm_bu - AT91 power management backup unit data structure
 * @suspended: true if suspended to backup mode
 * @reserved: reserved
 * @canary: canary data for memory checking after exit from backup mode
 * @resume: resume API
 * @ddr_phy_calibration: DDR PHY calibration data: ZQ0CR0, first 8 words
 * of the memory
 */
struct at91_pm_bu {
	int suspended;
	unsigned long reserved;
	phys_addr_t canary;
	phys_addr_t resume;
	unsigned long ddr_phy_calibration[BACKUP_DDR_PHY_CALIBRATION];
};

/**
 * struct at91_pm_sfrbu_regs - registers mapping for SFRBU
 * @pswbu: power switch BU control registers
 */
struct at91_pm_sfrbu_regs {
	struct {
		u32 key;
		u32 ctrl;
		u32 state;
		u32 softsw;
	} pswbu;
};

/**
 * enum at91_pm_eth_clk - Ethernet clock indexes
 * @AT91_PM_ETH_PCLK: pclk index
 * @AT91_PM_ETH_HCLK: hclk index
 * @AT91_PM_ETH_MAX_CLK: max index
 */
enum at91_pm_eth_clk {
	AT91_PM_ETH_PCLK,
	AT91_PM_ETH_HCLK,
	AT91_PM_ETH_MAX_CLK,
};

/**
 * enum at91_pm_eth - Ethernet controller indexes
 * @AT91_PM_G_ETH: gigabit Ethernet controller index
 * @AT91_PM_E_ETH: megabit Ethernet controller index
 * @AT91_PM_MAX_ETH: max index
 */
enum at91_pm_eth {
	AT91_PM_G_ETH,
	AT91_PM_E_ETH,
	AT91_PM_MAX_ETH,
};

/**
 * struct at91_pm_quirk_eth - AT91 PM Ethernet quirks
 * @dev: Ethernet device
 * @np: Ethernet device node
 * @clks: Ethernet clocks
 * @modes: power management mode that this quirk applies to
 * @dns_modes: do not suspend modes: stop suspending if Ethernet is configured
 *	       as wakeup source but buggy and no other wakeup source is
 *	       available
 */
struct at91_pm_quirk_eth {
	struct device *dev;
	struct device_node *np;
	struct clk_bulk_data clks[AT91_PM_ETH_MAX_CLK];
	u32 modes;
	u32 dns_modes;
};

/**
 * struct at91_pm_quirks - AT91 PM quirks
 * @eth: Ethernet quirks
 */
struct at91_pm_quirks {
	struct at91_pm_quirk_eth eth[AT91_PM_MAX_ETH];
};

/**
 * struct at91_soc_pm - AT91 SoC power management data structure
 * @config_shdwc_ws: wakeup sources configuration function for SHDWC
 * @config_pmc_ws: wakeup srouces configuration function for PMC
 * @ws_ids: wakup sources of_device_id array
 * @bu: backup unit mapped data (for backup mode)
 * @quirks: PM quirks
 * @data: PM data to be used on last phase of suspend
 * @sfrbu_regs: SFRBU registers mapping
 * @memcs: memory chip select
 */
struct at91_soc_pm {
	int (*config_shdwc_ws)(void __iomem *shdwc, u32 *mode, u32 *polarity);
	int (*config_pmc_ws)(void __iomem *pmc, u32 mode, u32 polarity);
	const struct of_device_id *ws_ids;
	struct at91_pm_bu *bu;
	struct at91_pm_quirks quirks;
	struct at91_pm_data data;
	struct at91_pm_sfrbu_regs sfrbu_regs;
	void *memcs;
};

/**
 * enum at91_pm_iomaps - IOs that needs to be mapped for different PM modes
 * @AT91_PM_IOMAP_SHDWC:	SHDWC controller
 * @AT91_PM_IOMAP_SFRBU:	SFRBU controller
 * @AT91_PM_IOMAP_ETHC:		Ethernet controller
 */
enum at91_pm_iomaps {
	AT91_PM_IOMAP_SHDWC,
	AT91_PM_IOMAP_SFRBU,
	AT91_PM_IOMAP_ETHC,
};

#define AT91_PM_IOMAP(name)	BIT(AT91_PM_IOMAP_##name)

static struct at91_soc_pm soc_pm = {
	.data = {
		.standby_mode = AT91_PM_STANDBY,
		.suspend_mode = AT91_PM_ULP0,
	},
};

static const match_table_t pm_modes __initconst = {
	{ AT91_PM_STANDBY,	"standby" },
	{ AT91_PM_ULP0,		"ulp0" },
	{ AT91_PM_ULP0_FAST,    "ulp0-fast" },
	{ AT91_PM_ULP1,		"ulp1" },
	{ AT91_PM_BACKUP,	"backup" },
	{ -1, NULL },
};

#define at91_ramc_read(id, field) \
	__raw_readl(soc_pm.data.ramc[id] + field)

#define at91_ramc_write(id, field, value) \
	__raw_writel(value, soc_pm.data.ramc[id] + field)

static int at91_pm_valid_state(suspend_state_t state)
{
	switch (state) {
		case PM_SUSPEND_ON:
		case PM_SUSPEND_STANDBY:
		case PM_SUSPEND_MEM:
			return 1;

		default:
			return 0;
	}
}

static int canary = 0xA5A5A5A5;

struct wakeup_source_info {
	unsigned int pmc_fsmr_bit;
	unsigned int shdwc_mr_bit;
	bool set_polarity;
};

static const struct wakeup_source_info ws_info[] = {
	{ .pmc_fsmr_bit = AT91_PMC_FSTT(10),	.set_polarity = true },
	{ .pmc_fsmr_bit = AT91_PMC_RTCAL,	.shdwc_mr_bit = BIT(17) },
	{ .pmc_fsmr_bit = AT91_PMC_USBAL },
	{ .pmc_fsmr_bit = AT91_PMC_SDMMC_CD },
	{ .pmc_fsmr_bit = AT91_PMC_RTTAL },
	{ .pmc_fsmr_bit = AT91_PMC_RXLP_MCE },
};

static const struct of_device_id sama5d2_ws_ids[] = {
	{ .compatible = "atmel,sama5d2-gem",		.data = &ws_info[0] },
	{ .compatible = "atmel,sama5d2-rtc",		.data = &ws_info[1] },
	{ .compatible = "atmel,sama5d3-udc",		.data = &ws_info[2] },
	{ .compatible = "atmel,at91rm9200-ohci",	.data = &ws_info[2] },
	{ .compatible = "usb-ohci",			.data = &ws_info[2] },
	{ .compatible = "atmel,at91sam9g45-ehci",	.data = &ws_info[2] },
	{ .compatible = "usb-ehci",			.data = &ws_info[2] },
	{ .compatible = "atmel,sama5d2-sdhci",		.data = &ws_info[3] },
	{ /* sentinel */ }
};

static const struct of_device_id sam9x60_ws_ids[] = {
	{ .compatible = "microchip,sam9x60-rtc",	.data = &ws_info[1] },
	{ .compatible = "atmel,at91rm9200-ohci",	.data = &ws_info[2] },
	{ .compatible = "usb-ohci",			.data = &ws_info[2] },
	{ .compatible = "atmel,at91sam9g45-ehci",	.data = &ws_info[2] },
	{ .compatible = "usb-ehci",			.data = &ws_info[2] },
	{ .compatible = "microchip,sam9x60-rtt",	.data = &ws_info[4] },
	{ .compatible = "cdns,sam9x60-macb",		.data = &ws_info[5] },
	{ /* sentinel */ }
};

static const struct of_device_id sama7g5_ws_ids[] = {
	{ .compatible = "microchip,sama7g5-rtc",	.data = &ws_info[1] },
	{ .compatible = "microchip,sama7g5-ohci",	.data = &ws_info[2] },
	{ .compatible = "usb-ohci",			.data = &ws_info[2] },
	{ .compatible = "atmel,at91sam9g45-ehci",	.data = &ws_info[2] },
	{ .compatible = "usb-ehci",			.data = &ws_info[2] },
	{ .compatible = "microchip,sama7g5-sdhci",	.data = &ws_info[3] },
	{ .compatible = "microchip,sama7g5-rtt",	.data = &ws_info[4] },
	{ /* sentinel */ }
};

static int at91_pm_config_ws(unsigned int pm_mode, bool set)
{
	const struct wakeup_source_info *wsi;
	const struct of_device_id *match;
	struct platform_device *pdev;
	struct device_node *np;
	unsigned int mode = 0, polarity = 0, val = 0;

	if (pm_mode != AT91_PM_ULP1)
		return 0;

	if (!soc_pm.data.pmc || !soc_pm.data.shdwc || !soc_pm.ws_ids)
		return -EPERM;

	if (!set) {
		writel(mode, soc_pm.data.pmc + AT91_PMC_FSMR);
		return 0;
	}

	if (soc_pm.config_shdwc_ws)
		soc_pm.config_shdwc_ws(soc_pm.data.shdwc, &mode, &polarity);

	/* SHDWC.MR */
	val = readl(soc_pm.data.shdwc + 0x04);

	/* Loop through defined wakeup sources. */
	for_each_matching_node_and_match(np, soc_pm.ws_ids, &match) {
		pdev = of_find_device_by_node(np);
		if (!pdev)
			continue;

		if (device_may_wakeup(&pdev->dev)) {
			wsi = match->data;

			/* Check if enabled on SHDWC. */
			if (wsi->shdwc_mr_bit && !(val & wsi->shdwc_mr_bit))
				goto put_device;

			mode |= wsi->pmc_fsmr_bit;
			if (wsi->set_polarity)
				polarity |= wsi->pmc_fsmr_bit;
		}

put_device:
		put_device(&pdev->dev);
	}

	if (mode) {
		if (soc_pm.config_pmc_ws)
			soc_pm.config_pmc_ws(soc_pm.data.pmc, mode, polarity);
	} else {
		pr_err("AT91: PM: no ULP1 wakeup sources found!");
	}

	return mode ? 0 : -EPERM;
}

static int at91_sama5d2_config_shdwc_ws(void __iomem *shdwc, u32 *mode,
					u32 *polarity)
{
	u32 val;

	/* SHDWC.WUIR */
	val = readl(shdwc + 0x0c);
	*mode |= (val & 0x3ff);
	*polarity |= ((val >> 16) & 0x3ff);

	return 0;
}

static int at91_sama5d2_config_pmc_ws(void __iomem *pmc, u32 mode, u32 polarity)
{
	writel(mode, pmc + AT91_PMC_FSMR);
	writel(polarity, pmc + AT91_PMC_FSPR);

	return 0;
}

static int at91_sam9x60_config_pmc_ws(void __iomem *pmc, u32 mode, u32 polarity)
{
	writel(mode, pmc + AT91_PMC_FSMR);

	return 0;
}

static bool at91_pm_eth_quirk_is_valid(struct at91_pm_quirk_eth *eth)
{
	struct platform_device *pdev;

	/* Interface NA in DT. */
	if (!eth->np)
		return false;

	/* No quirks for this interface and current suspend mode. */
	if (!(eth->modes & BIT(soc_pm.data.mode)))
		return false;

	if (!eth->dev) {
		/* Driver not probed. */
		pdev = of_find_device_by_node(eth->np);
		if (!pdev)
			return false;
		/* put_device(eth->dev) is called at the end of suspend. */
		eth->dev = &pdev->dev;
	}

	/* No quirks if device isn't a wakeup source. */
	if (!device_may_wakeup(eth->dev))
		return false;

	return true;
}

static int at91_pm_config_quirks(bool suspend)
{
	struct at91_pm_quirk_eth *eth;
	int i, j, ret, tmp;

	/*
	 * Ethernet IPs who's device_node pointers are stored into
	 * soc_pm.quirks.eth[].np cannot handle WoL packets while in ULP0, ULP1
	 * or both due to a hardware bug. If they receive WoL packets while in
	 * ULP0 or ULP1 IPs could stop working or the whole system could stop
	 * working. We cannot handle this scenario in the ethernet driver itself
	 * as the driver is common to multiple vendors and also we only know
	 * here, in this file, if we suspend to ULP0 or ULP1 mode. Thus handle
	 * these scenarios here, as quirks.
	 */
	for (i = 0; i < AT91_PM_MAX_ETH; i++) {
		eth = &soc_pm.quirks.eth[i];

		if (!at91_pm_eth_quirk_is_valid(eth))
			continue;

		/*
		 * For modes in dns_modes mask the system blocks if quirk is not
		 * applied but if applied the interface doesn't act at WoL
		 * events. Thus take care to avoid suspending if this interface
		 * is the only configured wakeup source.
		 */
		if (suspend && eth->dns_modes & BIT(soc_pm.data.mode)) {
			int ws_count = 0;
#ifdef CONFIG_PM_SLEEP
			struct wakeup_source *ws;

			for_each_wakeup_source(ws) {
				if (ws->dev == eth->dev)
					continue;

				ws_count++;
				break;
			}
#endif

			/*
			 * Checking !ws is good for all platforms with issues
			 * even when both G_ETH and E_ETH are available as dns_modes
			 * is populated only on G_ETH interface.
			 */
			if (!ws_count) {
				pr_err("AT91: PM: Ethernet cannot resume from WoL!");
				ret = -EPERM;
				put_device(eth->dev);
				eth->dev = NULL;
				/* No need to revert clock settings for this eth. */
				i--;
				goto clk_unconfigure;
			}
		}

		if (suspend) {
			clk_bulk_disable_unprepare(AT91_PM_ETH_MAX_CLK, eth->clks);
		} else {
			ret = clk_bulk_prepare_enable(AT91_PM_ETH_MAX_CLK,
						      eth->clks);
			if (ret)
				goto clk_unconfigure;
			/*
			 * Release the reference to eth->dev taken in
			 * at91_pm_eth_quirk_is_valid().
			 */
			put_device(eth->dev);
			eth->dev = NULL;
		}
	}

	return 0;

clk_unconfigure:
	/*
	 * In case of resume we reach this point if clk_prepare_enable() failed.
	 * we don't want to revert the previous clk_prepare_enable() for the
	 * other IP.
	 */
	for (j = i; j >= 0; j--) {
		eth = &soc_pm.quirks.eth[j];
		if (suspend) {
			if (!at91_pm_eth_quirk_is_valid(eth))
				continue;

			tmp = clk_bulk_prepare_enable(AT91_PM_ETH_MAX_CLK, eth->clks);
			if (tmp) {
				pr_err("AT91: PM: failed to enable %s clocks\n",
				       j == AT91_PM_G_ETH ? "geth" : "eth");
			}
		}

		/*
		 * Release the reference to eth->dev taken in
		 * at91_pm_eth_quirk_is_valid().
		 */
		put_device(eth->dev);
		eth->dev = NULL;
	}

	return ret;
}

/*
 * Called after processes are frozen, but before we shutdown devices.
 */
static int at91_pm_begin(suspend_state_t state)
{
	int ret;

	switch (state) {
	case PM_SUSPEND_MEM:
		soc_pm.data.mode = soc_pm.data.suspend_mode;
		break;

	case PM_SUSPEND_STANDBY:
		soc_pm.data.mode = soc_pm.data.standby_mode;
		break;

	default:
		soc_pm.data.mode = -1;
	}

	ret = at91_pm_config_ws(soc_pm.data.mode, true);
	if (ret)
		return ret;

	if (soc_pm.data.mode == AT91_PM_BACKUP)
		soc_pm.bu->suspended = 1;
	else if (soc_pm.bu)
		soc_pm.bu->suspended = 0;

	return 0;
}

/*
 * Verify that all the clocks are correct before entering
 * slow-clock mode.
 */
static int at91_pm_verify_clocks(void)
{
	unsigned long scsr;
	int i;

	scsr = readl(soc_pm.data.pmc + AT91_PMC_SCSR);

	/* USB must not be using PLLB */
	if ((scsr & soc_pm.data.uhp_udp_mask) != 0) {
		pr_err("AT91: PM - Suspend-to-RAM with USB still active\n");
		return 0;
	}

	/* PCK0..PCK3 must be disabled, or configured to use clk32k */
	for (i = 0; i < 4; i++) {
		u32 css;

		if ((scsr & (AT91_PMC_PCK0 << i)) == 0)
			continue;
		css = readl(soc_pm.data.pmc + AT91_PMC_PCKR(i)) & AT91_PMC_CSS;
		if (css != AT91_PMC_CSS_SLOW) {
			pr_err("AT91: PM - Suspend-to-RAM with PCK%d src %d\n", i, css);
			return 0;
		}
	}

	return 1;
}

/*
 * Call this from platform driver suspend() to see how deeply to suspend.
 * For example, some controllers (like OHCI) need one of the PLL clocks
 * in order to act as a wakeup source, and those are not available when
 * going into slow clock mode.
 *
 * REVISIT: generalize as clk_will_be_available(clk)?  Other platforms have
 * the very same problem (but not using at91 main_clk), and it'd be better
 * to add one generic API rather than lots of platform-specific ones.
 */
int at91_suspend_entering_slow_clock(void)
{
	return (soc_pm.data.mode >= AT91_PM_ULP0);
}
EXPORT_SYMBOL(at91_suspend_entering_slow_clock);

static void (*at91_suspend_sram_fn)(struct at91_pm_data *);
extern void at91_pm_suspend_in_sram(struct at91_pm_data *pm_data);
extern u32 at91_pm_suspend_in_sram_sz;

static int at91_suspend_finish(unsigned long val)
{
	unsigned char modified_gray_code[] = {
		0x00, 0x01, 0x02, 0x03, 0x06, 0x07, 0x04, 0x05, 0x0c, 0x0d,
		0x0e, 0x0f, 0x0a, 0x0b, 0x08, 0x09, 0x18, 0x19, 0x1a, 0x1b,
		0x1e, 0x1f, 0x1c, 0x1d, 0x14, 0x15, 0x16, 0x17, 0x12, 0x13,
		0x10, 0x11,
	};
	unsigned int tmp, index;
	int i;

	if (soc_pm.data.mode == AT91_PM_BACKUP && soc_pm.data.ramc_phy) {
		/*
		 * Bootloader will perform DDR recalibration and will try to
		 * restore the ZQ0SR0 with the value saved here. But the
		 * calibration is buggy and restoring some values from ZQ0SR0
		 * is forbidden and risky thus we need to provide processed
		 * values for these (modified gray code values).
		 */
		tmp = readl(soc_pm.data.ramc_phy + DDR3PHY_ZQ0SR0);

		/* Store pull-down output impedance select. */
		index = (tmp >> DDR3PHY_ZQ0SR0_PDO_OFF) & 0x1f;
		soc_pm.bu->ddr_phy_calibration[0] = modified_gray_code[index];

		/* Store pull-up output impedance select. */
		index = (tmp >> DDR3PHY_ZQ0SR0_PUO_OFF) & 0x1f;
		soc_pm.bu->ddr_phy_calibration[0] |= modified_gray_code[index];

		/* Store pull-down on-die termination impedance select. */
		index = (tmp >> DDR3PHY_ZQ0SR0_PDODT_OFF) & 0x1f;
		soc_pm.bu->ddr_phy_calibration[0] |= modified_gray_code[index];

		/* Store pull-up on-die termination impedance select. */
		index = (tmp >> DDR3PHY_ZQ0SRO_PUODT_OFF) & 0x1f;
		soc_pm.bu->ddr_phy_calibration[0] |= modified_gray_code[index];

		/*
		 * The 1st 8 words of memory might get corrupted in the process
		 * of DDR PHY recalibration; it is saved here in securam and it
		 * will be restored later, after recalibration, by bootloader
		 */
		for (i = 1; i < BACKUP_DDR_PHY_CALIBRATION; i++)
			soc_pm.bu->ddr_phy_calibration[i] =
				*((unsigned int *)soc_pm.memcs + (i - 1));
	}

	flush_cache_all();
	outer_disable();

	at91_suspend_sram_fn(&soc_pm.data);

	return 0;
}

static void at91_pm_switch_ba_to_vbat(void)
{
	unsigned int offset = offsetof(struct at91_pm_sfrbu_regs, pswbu);
	unsigned int val;

	/* Just for safety. */
	if (!soc_pm.data.sfrbu)
		return;

	val = readl(soc_pm.data.sfrbu + offset);

	/* Already on VBAT. */
	if (!(val & soc_pm.sfrbu_regs.pswbu.state))
		return;

	val &= ~soc_pm.sfrbu_regs.pswbu.softsw;
	val |= soc_pm.sfrbu_regs.pswbu.key | soc_pm.sfrbu_regs.pswbu.ctrl;
	writel(val, soc_pm.data.sfrbu + offset);

	/* Wait for update. */
	val = readl(soc_pm.data.sfrbu + offset);
	while (val & soc_pm.sfrbu_regs.pswbu.state)
		val = readl(soc_pm.data.sfrbu + offset);
}

static void at91_pm_suspend(suspend_state_t state)
{
	if (soc_pm.data.mode == AT91_PM_BACKUP) {
		at91_pm_switch_ba_to_vbat();

		cpu_suspend(0, at91_suspend_finish);

		/* The SRAM is lost between suspend cycles */
		at91_suspend_sram_fn = fncpy(at91_suspend_sram_fn,
					     &at91_pm_suspend_in_sram,
					     at91_pm_suspend_in_sram_sz);
	} else {
		at91_suspend_finish(0);
	}

	outer_resume();
}

/*
 * STANDBY mode has *all* drivers suspended; ignores irqs not marked as 'wakeup'
 * event sources; and reduces DRAM power.  But otherwise it's identical to
 * PM_SUSPEND_ON: cpu idle, and nothing fancy done with main or cpu clocks.
 *
 * AT91_PM_ULP0 is like STANDBY plus slow clock mode, so drivers must
 * suspend more deeply, the master clock switches to the clk32k and turns off
 * the main oscillator
 *
 * AT91_PM_BACKUP turns off the whole SoC after placing the DDR in self refresh
 */
static int at91_pm_enter(suspend_state_t state)
{
	int ret;

	ret = at91_pm_config_quirks(true);
	if (ret)
		return ret;

	switch (state) {
	case PM_SUSPEND_MEM:
	case PM_SUSPEND_STANDBY:
		/*
		 * Ensure that clocks are in a valid state.
		 */
		if (soc_pm.data.mode >= AT91_PM_ULP0 &&
		    !at91_pm_verify_clocks())
			goto error;

		at91_pm_suspend(state);

		break;

	case PM_SUSPEND_ON:
		cpu_do_idle();
		break;

	default:
		pr_debug("AT91: PM - bogus suspend state %d\n", state);
		goto error;
	}

error:
	at91_pm_config_quirks(false);
	return 0;
}

/*
 * Called right prior to thawing processes.
 */
static void at91_pm_end(void)
{
	at91_pm_config_ws(soc_pm.data.mode, false);
}


static const struct platform_suspend_ops at91_pm_ops = {
	.valid	= at91_pm_valid_state,
	.begin	= at91_pm_begin,
	.enter	= at91_pm_enter,
	.end	= at91_pm_end,
};

static struct platform_device at91_cpuidle_device = {
	.name = "cpuidle-at91",
};

/*
 * The AT91RM9200 goes into self-refresh mode with this command, and will
 * terminate self-refresh automatically on the next SDRAM access.
 *
 * Self-refresh mode is exited as soon as a memory access is made, but we don't
 * know for sure when that happens. However, we need to restore the low-power
 * mode if it was enabled before going idle. Restoring low-power mode while
 * still in self-refresh is "not recommended", but seems to work.
 */
static void at91rm9200_standby(void)
{
	asm volatile(
		"b    1f\n\t"
		".align    5\n\t"
		"1:  mcr    p15, 0, %0, c7, c10, 4\n\t"
		"    str    %2, [%1, %3]\n\t"
		"    mcr    p15, 0, %0, c7, c0, 4\n\t"
		:
		: "r" (0), "r" (soc_pm.data.ramc[0]),
		  "r" (1), "r" (AT91_MC_SDRAMC_SRR));
}

/* We manage both DDRAM/SDRAM controllers, we need more than one value to
 * remember.
 */
static void at91_ddr_standby(void)
{
	/* Those two values allow us to delay self-refresh activation
	 * to the maximum. */
	u32 lpr0, lpr1 = 0;
	u32 mdr, saved_mdr0, saved_mdr1 = 0;
	u32 saved_lpr0, saved_lpr1 = 0;

	/* LPDDR1 --> force DDR2 mode during self-refresh */
	saved_mdr0 = at91_ramc_read(0, AT91_DDRSDRC_MDR);
	if ((saved_mdr0 & AT91_DDRSDRC_MD) == AT91_DDRSDRC_MD_LOW_POWER_DDR) {
		mdr = saved_mdr0 & ~AT91_DDRSDRC_MD;
		mdr |= AT91_DDRSDRC_MD_DDR2;
		at91_ramc_write(0, AT91_DDRSDRC_MDR, mdr);
	}

	if (soc_pm.data.ramc[1]) {
		saved_lpr1 = at91_ramc_read(1, AT91_DDRSDRC_LPR);
		lpr1 = saved_lpr1 & ~AT91_DDRSDRC_LPCB;
		lpr1 |= AT91_DDRSDRC_LPCB_SELF_REFRESH;
		saved_mdr1 = at91_ramc_read(1, AT91_DDRSDRC_MDR);
		if ((saved_mdr1 & AT91_DDRSDRC_MD) == AT91_DDRSDRC_MD_LOW_POWER_DDR) {
			mdr = saved_mdr1 & ~AT91_DDRSDRC_MD;
			mdr |= AT91_DDRSDRC_MD_DDR2;
			at91_ramc_write(1, AT91_DDRSDRC_MDR, mdr);
		}
	}

	saved_lpr0 = at91_ramc_read(0, AT91_DDRSDRC_LPR);
	lpr0 = saved_lpr0 & ~AT91_DDRSDRC_LPCB;
	lpr0 |= AT91_DDRSDRC_LPCB_SELF_REFRESH;

	/* self-refresh mode now */
	at91_ramc_write(0, AT91_DDRSDRC_LPR, lpr0);
	if (soc_pm.data.ramc[1])
		at91_ramc_write(1, AT91_DDRSDRC_LPR, lpr1);

	cpu_do_idle();

	at91_ramc_write(0, AT91_DDRSDRC_MDR, saved_mdr0);
	at91_ramc_write(0, AT91_DDRSDRC_LPR, saved_lpr0);
	if (soc_pm.data.ramc[1]) {
		at91_ramc_write(0, AT91_DDRSDRC_MDR, saved_mdr1);
		at91_ramc_write(1, AT91_DDRSDRC_LPR, saved_lpr1);
	}
}

static void sama5d3_ddr_standby(void)
{
	u32 lpr0;
	u32 saved_lpr0;

	saved_lpr0 = at91_ramc_read(0, AT91_DDRSDRC_LPR);
	lpr0 = saved_lpr0 & ~AT91_DDRSDRC_LPCB;
	lpr0 |= AT91_DDRSDRC_LPCB_POWER_DOWN;

	at91_ramc_write(0, AT91_DDRSDRC_LPR, lpr0);

	cpu_do_idle();

	at91_ramc_write(0, AT91_DDRSDRC_LPR, saved_lpr0);
}

/* We manage both DDRAM/SDRAM controllers, we need more than one value to
 * remember.
 */
static void at91sam9_sdram_standby(void)
{
	u32 lpr0, lpr1 = 0;
	u32 saved_lpr0, saved_lpr1 = 0;

	if (soc_pm.data.ramc[1]) {
		saved_lpr1 = at91_ramc_read(1, AT91_SDRAMC_LPR);
		lpr1 = saved_lpr1 & ~AT91_SDRAMC_LPCB;
		lpr1 |= AT91_SDRAMC_LPCB_SELF_REFRESH;
	}

	saved_lpr0 = at91_ramc_read(0, AT91_SDRAMC_LPR);
	lpr0 = saved_lpr0 & ~AT91_SDRAMC_LPCB;
	lpr0 |= AT91_SDRAMC_LPCB_SELF_REFRESH;

	/* self-refresh mode now */
	at91_ramc_write(0, AT91_SDRAMC_LPR, lpr0);
	if (soc_pm.data.ramc[1])
		at91_ramc_write(1, AT91_SDRAMC_LPR, lpr1);

	cpu_do_idle();

	at91_ramc_write(0, AT91_SDRAMC_LPR, saved_lpr0);
	if (soc_pm.data.ramc[1])
		at91_ramc_write(1, AT91_SDRAMC_LPR, saved_lpr1);
}

static void sama7g5_standby(void)
{
	int pwrtmg, ratio;

	pwrtmg = readl(soc_pm.data.ramc[0] + UDDRC_PWRCTL);
	ratio = readl(soc_pm.data.pmc + AT91_PMC_RATIO);

	/*
	 * Place RAM into self-refresh after a maximum idle clocks. The maximum
	 * idle clocks is configured by bootloader in
	 * UDDRC_PWRMGT.SELFREF_TO_X32.
	 */
	writel(pwrtmg | UDDRC_PWRCTL_SELFREF_EN,
	       soc_pm.data.ramc[0] + UDDRC_PWRCTL);
	/* Divide CPU clock by 16. */
	writel(ratio & ~AT91_PMC_RATIO_RATIO, soc_pm.data.pmc + AT91_PMC_RATIO);

	cpu_do_idle();

	/* Restore previous configuration. */
	writel(ratio, soc_pm.data.pmc + AT91_PMC_RATIO);
	writel(pwrtmg, soc_pm.data.ramc[0] + UDDRC_PWRCTL);
}

struct ramc_info {
	void (*idle)(void);
	unsigned int memctrl;
};

static const struct ramc_info ramc_infos[] __initconst = {
	{ .idle = at91rm9200_standby, .memctrl = AT91_MEMCTRL_MC},
	{ .idle = at91sam9_sdram_standby, .memctrl = AT91_MEMCTRL_SDRAMC},
	{ .idle = at91_ddr_standby, .memctrl = AT91_MEMCTRL_DDRSDR},
	{ .idle = sama5d3_ddr_standby, .memctrl = AT91_MEMCTRL_DDRSDR},
	{ .idle = sama7g5_standby, },
};

static const struct of_device_id ramc_ids[] __initconst = {
	{ .compatible = "atmel,at91rm9200-sdramc", .data = &ramc_infos[0] },
	{ .compatible = "atmel,at91sam9260-sdramc", .data = &ramc_infos[1] },
	{ .compatible = "atmel,at91sam9g45-ddramc", .data = &ramc_infos[2] },
	{ .compatible = "atmel,sama5d3-ddramc", .data = &ramc_infos[3] },
	{ .compatible = "microchip,sama7g5-uddrc", .data = &ramc_infos[4], },
	{ /*sentinel*/ }
};

static const struct of_device_id ramc_phy_ids[] __initconst = {
	{ .compatible = "microchip,sama7g5-ddr3phy", },
	{ /* Sentinel. */ },
};

static __init int at91_dt_ramc(bool phy_mandatory)
{
	struct device_node *np;
	const struct of_device_id *of_id;
	int idx = 0;
	void *standby = NULL;
	const struct ramc_info *ramc;
	int ret;

	for_each_matching_node_and_match(np, ramc_ids, &of_id) {
		soc_pm.data.ramc[idx] = of_iomap(np, 0);
		if (!soc_pm.data.ramc[idx]) {
			pr_err("unable to map ramc[%d] cpu registers\n", idx);
			ret = -ENOMEM;
			of_node_put(np);
			goto unmap_ramc;
		}

		ramc = of_id->data;
		if (ramc) {
			if (!standby)
				standby = ramc->idle;
			soc_pm.data.memctrl = ramc->memctrl;
		}

		idx++;
	}

	if (!idx) {
		pr_err("unable to find compatible ram controller node in dtb\n");
		ret = -ENODEV;
		goto unmap_ramc;
	}

	/* Lookup for DDR PHY node, if any. */
	for_each_matching_node_and_match(np, ramc_phy_ids, &of_id) {
		soc_pm.data.ramc_phy = of_iomap(np, 0);
		if (!soc_pm.data.ramc_phy) {
			pr_err("unable to map ramc phy cpu registers\n");
			ret = -ENOMEM;
			of_node_put(np);
			goto unmap_ramc;
		}
	}

	if (phy_mandatory && !soc_pm.data.ramc_phy) {
		pr_err("DDR PHY is mandatory!\n");
		ret = -ENODEV;
		goto unmap_ramc;
	}

	if (!standby) {
		pr_warn("ramc no standby function available\n");
		return 0;
	}

	at91_cpuidle_device.dev.platform_data = standby;

	return 0;

unmap_ramc:
	while (idx)
		iounmap(soc_pm.data.ramc[--idx]);

	return ret;
}

static void at91rm9200_idle(void)
{
	/*
	 * Disable the processor clock.  The processor will be automatically
	 * re-enabled by an interrupt or by a reset.
	 */
	writel(AT91_PMC_PCK, soc_pm.data.pmc + AT91_PMC_SCDR);
}

static void at91sam9_idle(void)
{
	writel(AT91_PMC_PCK, soc_pm.data.pmc + AT91_PMC_SCDR);
	cpu_do_idle();
}

static void __init at91_pm_sram_init(void)
{
	struct gen_pool *sram_pool;
	phys_addr_t sram_pbase;
	unsigned long sram_base;
	struct device_node *node;
	struct platform_device *pdev = NULL;

	for_each_compatible_node(node, NULL, "mmio-sram") {
		pdev = of_find_device_by_node(node);
		if (pdev) {
			of_node_put(node);
			break;
		}
	}

	if (!pdev) {
		pr_warn("%s: failed to find sram device!\n", __func__);
		return;
	}

	sram_pool = gen_pool_get(&pdev->dev, NULL);
	if (!sram_pool) {
		pr_warn("%s: sram pool unavailable!\n", __func__);
		goto out_put_device;
	}

	sram_base = gen_pool_alloc(sram_pool, at91_pm_suspend_in_sram_sz);
	if (!sram_base) {
		pr_warn("%s: unable to alloc sram!\n", __func__);
		goto out_put_device;
	}

	sram_pbase = gen_pool_virt_to_phys(sram_pool, sram_base);
	at91_suspend_sram_fn = __arm_ioremap_exec(sram_pbase,
					at91_pm_suspend_in_sram_sz, false);
	if (!at91_suspend_sram_fn) {
		pr_warn("SRAM: Could not map\n");
		goto out_put_device;
	}

	/* Copy the pm suspend handler to SRAM */
	at91_suspend_sram_fn = fncpy(at91_suspend_sram_fn,
			&at91_pm_suspend_in_sram, at91_pm_suspend_in_sram_sz);
	return;

out_put_device:
	put_device(&pdev->dev);
	return;
}

static bool __init at91_is_pm_mode_active(int pm_mode)
{
	return (soc_pm.data.standby_mode == pm_mode ||
		soc_pm.data.suspend_mode == pm_mode);
}

static int __init at91_pm_backup_scan_memcs(unsigned long node,
					    const char *uname, int depth,
					    void *data)
{
	const char *type;
	const __be32 *reg;
	int *located = data;
	int size;

	/* Memory node already located. */
	if (*located)
		return 0;

	type = of_get_flat_dt_prop(node, "device_type", NULL);

	/* We are scanning "memory" nodes only. */
	if (!type || strcmp(type, "memory"))
		return 0;

	reg = of_get_flat_dt_prop(node, "reg", &size);
	if (reg) {
		soc_pm.memcs = __va((phys_addr_t)be32_to_cpu(*reg));
		*located = 1;
	}

	return 0;
}

static int __init at91_pm_backup_init(void)
{
	struct gen_pool *sram_pool;
	struct device_node *np;
	struct platform_device *pdev;
	int ret = -ENODEV, located = 0;

	if (!IS_ENABLED(CONFIG_SOC_SAMA5D2) &&
	    !IS_ENABLED(CONFIG_SOC_SAMA7G5))
		return -EPERM;

	if (!at91_is_pm_mode_active(AT91_PM_BACKUP))
		return 0;

	np = of_find_compatible_node(NULL, NULL, "atmel,sama5d2-securam");
	if (!np)
		return ret;

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev) {
		pr_warn("%s: failed to find securam device!\n", __func__);
		return ret;
	}

	sram_pool = gen_pool_get(&pdev->dev, NULL);
	if (!sram_pool) {
		pr_warn("%s: securam pool unavailable!\n", __func__);
		goto securam_fail;
	}

	soc_pm.bu = (void *)gen_pool_alloc(sram_pool, sizeof(struct at91_pm_bu));
	if (!soc_pm.bu) {
		pr_warn("%s: unable to alloc securam!\n", __func__);
		ret = -ENOMEM;
		goto securam_fail;
	}

	soc_pm.bu->suspended = 0;
	soc_pm.bu->canary = __pa_symbol(&canary);
	soc_pm.bu->resume = __pa_symbol(cpu_resume);
	if (soc_pm.data.ramc_phy) {
		of_scan_flat_dt(at91_pm_backup_scan_memcs, &located);
		if (!located)
			goto securam_fail;
	}

	return 0;

securam_fail:
	put_device(&pdev->dev);
	return ret;
}

static void __init at91_pm_secure_init(void)
{
	int suspend_mode;
	struct arm_smccc_res res;

	suspend_mode = soc_pm.data.suspend_mode;

	res = sam_smccc_call(SAMA5_SMC_SIP_SET_SUSPEND_MODE,
			     suspend_mode, 0);
	if (res.a0 == 0) {
		pr_info("AT91: Secure PM: suspend mode set to %s\n",
			pm_modes[suspend_mode].pattern);
		return;
	}

	pr_warn("AT91: Secure PM: %s mode not supported !\n",
		pm_modes[suspend_mode].pattern);

	res = sam_smccc_call(SAMA5_SMC_SIP_GET_SUSPEND_MODE, 0, 0);
	if (res.a0 == 0) {
		pr_warn("AT91: Secure PM: failed to get default mode\n");
		return;
	}

	pr_info("AT91: Secure PM: using default suspend mode %s\n",
		pm_modes[suspend_mode].pattern);

	soc_pm.data.suspend_mode = res.a1;
}
static const struct of_device_id atmel_shdwc_ids[] = {
	{ .compatible = "atmel,sama5d2-shdwc" },
	{ .compatible = "microchip,sam9x60-shdwc" },
	{ .compatible = "microchip,sama7g5-shdwc" },
	{ /* sentinel. */ }
};

static const struct of_device_id gmac_ids[] __initconst = {
	{ .compatible = "atmel,sama5d3-gem" },
	{ .compatible = "atmel,sama5d2-gem" },
	{ .compatible = "atmel,sama5d29-gem" },
	{ .compatible = "microchip,sama7g5-gem" },
	{ },
};

static const struct of_device_id emac_ids[] __initconst = {
	{ .compatible = "atmel,sama5d3-macb" },
	{ .compatible = "microchip,sama7g5-emac" },
	{ },
};

/*
 * Replaces _mode_to_replace with a supported mode that doesn't depend
 * on controller pointed by _map_bitmask
 * @_maps: u32 array containing AT91_PM_IOMAP() flags and indexed by AT91
 * PM mode
 * @_map_bitmask: AT91_PM_IOMAP() bitmask; if _mode_to_replace depends on
 * controller represented by _map_bitmask, _mode_to_replace needs to be
 * updated
 * @_mode_to_replace: standby_mode or suspend_mode that need to be
 * updated
 * @_mode_to_check: standby_mode or suspend_mode; this is needed here
 * to avoid having standby_mode and suspend_mode set with the same AT91
 * PM mode
 */
#define AT91_PM_REPLACE_MODE(_maps, _map_bitmask, _mode_to_replace,	\
			     _mode_to_check)				\
	do {								\
		if (((_maps)[(_mode_to_replace)]) & (_map_bitmask)) {	\
			int _mode_to_use, _mode_complementary;		\
			/* Use ULP0 if it doesn't need _map_bitmask. */	\
			if (!((_maps)[AT91_PM_ULP0] & (_map_bitmask))) {\
				_mode_to_use = AT91_PM_ULP0;		\
				_mode_complementary = AT91_PM_STANDBY;	\
			} else {					\
				_mode_to_use = AT91_PM_STANDBY;		\
				_mode_complementary = AT91_PM_STANDBY;	\
			}						\
									\
			if ((_mode_to_check) != _mode_to_use)		\
				(_mode_to_replace) = _mode_to_use;	\
			else						\
				(_mode_to_replace) = _mode_complementary;\
		}							\
	} while (0)

/*
 * Replaces standby and suspend modes with default supported modes:
 * ULP0 and STANDBY.
 * @_maps: u32 array indexed by AT91 PM mode containing AT91_PM_IOMAP()
 * flags
 * @_map: controller specific name; standby and suspend mode need to be
 * replaced in order to not depend on this controller
 */
#define AT91_PM_REPLACE_MODES(_maps, _map)				\
	do {								\
		AT91_PM_REPLACE_MODE((_maps), BIT(AT91_PM_IOMAP_##_map),\
				     (soc_pm.data.standby_mode),	\
				     (soc_pm.data.suspend_mode));	\
		AT91_PM_REPLACE_MODE((_maps), BIT(AT91_PM_IOMAP_##_map),\
				     (soc_pm.data.suspend_mode),	\
				     (soc_pm.data.standby_mode));	\
	} while (0)

static int __init at91_pm_get_eth_clks(struct device_node *np,
				       struct clk_bulk_data *clks)
{
	clks[AT91_PM_ETH_PCLK].clk = of_clk_get_by_name(np, "pclk");
	if (IS_ERR(clks[AT91_PM_ETH_PCLK].clk))
		return PTR_ERR(clks[AT91_PM_ETH_PCLK].clk);

	clks[AT91_PM_ETH_HCLK].clk = of_clk_get_by_name(np, "hclk");
	if (IS_ERR(clks[AT91_PM_ETH_HCLK].clk))
		return PTR_ERR(clks[AT91_PM_ETH_HCLK].clk);

	return 0;
}

static int __init at91_pm_eth_clks_empty(struct clk_bulk_data *clks)
{
	return IS_ERR(clks[AT91_PM_ETH_PCLK].clk) ||
	       IS_ERR(clks[AT91_PM_ETH_HCLK].clk);
}

static void __init at91_pm_modes_init(const u32 *maps, int len)
{
	struct at91_pm_quirk_eth *gmac = &soc_pm.quirks.eth[AT91_PM_G_ETH];
	struct at91_pm_quirk_eth *emac = &soc_pm.quirks.eth[AT91_PM_E_ETH];
	struct device_node *np;
	int ret;

	ret = at91_pm_backup_init();
	if (ret) {
		if (soc_pm.data.standby_mode == AT91_PM_BACKUP)
			soc_pm.data.standby_mode = AT91_PM_ULP0;
		if (soc_pm.data.suspend_mode == AT91_PM_BACKUP)
			soc_pm.data.suspend_mode = AT91_PM_ULP0;
	}

	if (maps[soc_pm.data.standby_mode] & AT91_PM_IOMAP(SHDWC) ||
	    maps[soc_pm.data.suspend_mode] & AT91_PM_IOMAP(SHDWC)) {
		np = of_find_matching_node(NULL, atmel_shdwc_ids);
		if (!np) {
			pr_warn("%s: failed to find shdwc!\n", __func__);
			AT91_PM_REPLACE_MODES(maps, SHDWC);
		} else {
			soc_pm.data.shdwc = of_iomap(np, 0);
			of_node_put(np);
		}
	}

	if (maps[soc_pm.data.standby_mode] & AT91_PM_IOMAP(SFRBU) ||
	    maps[soc_pm.data.suspend_mode] & AT91_PM_IOMAP(SFRBU)) {
		np = of_find_compatible_node(NULL, NULL, "atmel,sama5d2-sfrbu");
		if (!np) {
			pr_warn("%s: failed to find sfrbu!\n", __func__);
			AT91_PM_REPLACE_MODES(maps, SFRBU);
		} else {
			soc_pm.data.sfrbu = of_iomap(np, 0);
			of_node_put(np);
		}
	}

	if ((at91_is_pm_mode_active(AT91_PM_ULP1) ||
	     at91_is_pm_mode_active(AT91_PM_ULP0) ||
	     at91_is_pm_mode_active(AT91_PM_ULP0_FAST)) &&
	    (maps[soc_pm.data.standby_mode] & AT91_PM_IOMAP(ETHC) ||
	     maps[soc_pm.data.suspend_mode] & AT91_PM_IOMAP(ETHC))) {
		np = of_find_matching_node(NULL, gmac_ids);
		if (!np) {
			np = of_find_matching_node(NULL, emac_ids);
			if (np)
				goto get_emac_clks;
			AT91_PM_REPLACE_MODES(maps, ETHC);
			goto unmap_unused_nodes;
		} else {
			gmac->np = np;
			at91_pm_get_eth_clks(np, gmac->clks);
		}

		np = of_find_matching_node(NULL, emac_ids);
		if (!np) {
			if (at91_pm_eth_clks_empty(gmac->clks))
				AT91_PM_REPLACE_MODES(maps, ETHC);
		} else {
get_emac_clks:
			emac->np = np;
			ret = at91_pm_get_eth_clks(np, emac->clks);
			if (ret && at91_pm_eth_clks_empty(gmac->clks)) {
				of_node_put(gmac->np);
				of_node_put(emac->np);
				gmac->np = NULL;
				emac->np = NULL;
			}
		}
	}

unmap_unused_nodes:
	/* Unmap all unnecessary. */
	if (soc_pm.data.shdwc &&
	    !(maps[soc_pm.data.standby_mode] & AT91_PM_IOMAP(SHDWC) ||
	      maps[soc_pm.data.suspend_mode] & AT91_PM_IOMAP(SHDWC))) {
		iounmap(soc_pm.data.shdwc);
		soc_pm.data.shdwc = NULL;
	}

	if (soc_pm.data.sfrbu &&
	    !(maps[soc_pm.data.standby_mode] & AT91_PM_IOMAP(SFRBU) ||
	      maps[soc_pm.data.suspend_mode] & AT91_PM_IOMAP(SFRBU))) {
		iounmap(soc_pm.data.sfrbu);
		soc_pm.data.sfrbu = NULL;
	}

	return;
}

struct pmc_info {
	unsigned long uhp_udp_mask;
	unsigned long mckr;
	unsigned long version;
};

static const struct pmc_info pmc_infos[] __initconst = {
	{
		.uhp_udp_mask = AT91RM9200_PMC_UHP | AT91RM9200_PMC_UDP,
		.mckr = 0x30,
		.version = AT91_PMC_V1,
	},

	{
		.uhp_udp_mask = AT91SAM926x_PMC_UHP | AT91SAM926x_PMC_UDP,
		.mckr = 0x30,
		.version = AT91_PMC_V1,
	},
	{
		.uhp_udp_mask = AT91SAM926x_PMC_UHP,
		.mckr = 0x30,
		.version = AT91_PMC_V1,
	},
	{	.uhp_udp_mask = 0,
		.mckr = 0x30,
		.version = AT91_PMC_V1,
	},
	{
		.uhp_udp_mask = AT91SAM926x_PMC_UHP | AT91SAM926x_PMC_UDP,
		.mckr = 0x28,
		.version = AT91_PMC_V2,
	},
	{
		.mckr = 0x28,
		.version = AT91_PMC_V2,
	},

};

static const struct of_device_id atmel_pmc_ids[] __initconst = {
	{ .compatible = "atmel,at91rm9200-pmc", .data = &pmc_infos[0] },
	{ .compatible = "atmel,at91sam9260-pmc", .data = &pmc_infos[1] },
	{ .compatible = "atmel,at91sam9261-pmc", .data = &pmc_infos[1] },
	{ .compatible = "atmel,at91sam9263-pmc", .data = &pmc_infos[1] },
	{ .compatible = "atmel,at91sam9g45-pmc", .data = &pmc_infos[2] },
	{ .compatible = "atmel,at91sam9n12-pmc", .data = &pmc_infos[1] },
	{ .compatible = "atmel,at91sam9rl-pmc", .data = &pmc_infos[3] },
	{ .compatible = "atmel,at91sam9x5-pmc", .data = &pmc_infos[1] },
	{ .compatible = "atmel,sama5d3-pmc", .data = &pmc_infos[1] },
	{ .compatible = "atmel,sama5d4-pmc", .data = &pmc_infos[1] },
	{ .compatible = "atmel,sama5d2-pmc", .data = &pmc_infos[1] },
	{ .compatible = "microchip,sam9x60-pmc", .data = &pmc_infos[4] },
	{ .compatible = "microchip,sama7g5-pmc", .data = &pmc_infos[5] },
	{ /* sentinel */ },
};

static void __init at91_pm_modes_validate(const int *modes, int len)
{
	u8 i, standby = 0, suspend = 0;
	int mode;

	for (i = 0; i < len; i++) {
		if (standby && suspend)
			break;

		if (modes[i] == soc_pm.data.standby_mode && !standby) {
			standby = 1;
			continue;
		}

		if (modes[i] == soc_pm.data.suspend_mode && !suspend) {
			suspend = 1;
			continue;
		}
	}

	if (!standby) {
		if (soc_pm.data.suspend_mode == AT91_PM_STANDBY)
			mode = AT91_PM_ULP0;
		else
			mode = AT91_PM_STANDBY;

		pr_warn("AT91: PM: %s mode not supported! Using %s.\n",
			pm_modes[soc_pm.data.standby_mode].pattern,
			pm_modes[mode].pattern);
		soc_pm.data.standby_mode = mode;
	}

	if (!suspend) {
		if (soc_pm.data.standby_mode == AT91_PM_ULP0)
			mode = AT91_PM_STANDBY;
		else
			mode = AT91_PM_ULP0;

		pr_warn("AT91: PM: %s mode not supported! Using %s.\n",
			pm_modes[soc_pm.data.suspend_mode].pattern,
			pm_modes[mode].pattern);
		soc_pm.data.suspend_mode = mode;
	}
}

static void __init at91_pm_init(void (*pm_idle)(void))
{
	struct device_node *pmc_np;
	const struct of_device_id *of_id;
	const struct pmc_info *pmc;

	if (at91_cpuidle_device.dev.platform_data)
		platform_device_register(&at91_cpuidle_device);

	pmc_np = of_find_matching_node_and_match(NULL, atmel_pmc_ids, &of_id);
	soc_pm.data.pmc = of_iomap(pmc_np, 0);
	of_node_put(pmc_np);
	if (!soc_pm.data.pmc) {
		pr_err("AT91: PM not supported, PMC not found\n");
		return;
	}

	pmc = of_id->data;
	soc_pm.data.uhp_udp_mask = pmc->uhp_udp_mask;
	soc_pm.data.pmc_mckr_offset = pmc->mckr;
	soc_pm.data.pmc_version = pmc->version;

	if (pm_idle)
		arm_pm_idle = pm_idle;

	at91_pm_sram_init();

	if (at91_suspend_sram_fn) {
		suspend_set_ops(&at91_pm_ops);
		pr_info("AT91: PM: standby: %s, suspend: %s\n",
			pm_modes[soc_pm.data.standby_mode].pattern,
			pm_modes[soc_pm.data.suspend_mode].pattern);
	} else {
		pr_info("AT91: PM not supported, due to no SRAM allocated\n");
	}
}

void __init at91rm9200_pm_init(void)
{
	int ret;

	if (!IS_ENABLED(CONFIG_SOC_AT91RM9200))
		return;

	/*
	 * Force STANDBY and ULP0 mode to avoid calling
	 * at91_pm_modes_validate() which may increase booting time.
	 * Platform supports anyway only STANDBY and ULP0 modes.
	 */
	soc_pm.data.standby_mode = AT91_PM_STANDBY;
	soc_pm.data.suspend_mode = AT91_PM_ULP0;

	ret = at91_dt_ramc(false);
	if (ret)
		return;

	/*
	 * AT91RM9200 SDRAM low-power mode cannot be used with self-refresh.
	 */
	at91_ramc_write(0, AT91_MC_SDRAMC_LPR, 0);

	at91_pm_init(at91rm9200_idle);
}

void __init sam9x60_pm_init(void)
{
	static const int modes[] __initconst = {
		AT91_PM_STANDBY, AT91_PM_ULP0, AT91_PM_ULP0_FAST, AT91_PM_ULP1,
	};
	static const int iomaps[] __initconst = {
		[AT91_PM_ULP1]		= AT91_PM_IOMAP(SHDWC),
	};
	int ret;

	if (!IS_ENABLED(CONFIG_SOC_SAM9X60))
		return;

	at91_pm_modes_validate(modes, ARRAY_SIZE(modes));
	at91_pm_modes_init(iomaps, ARRAY_SIZE(iomaps));
	ret = at91_dt_ramc(false);
	if (ret)
		return;

	at91_pm_init(NULL);

	soc_pm.ws_ids = sam9x60_ws_ids;
	soc_pm.config_pmc_ws = at91_sam9x60_config_pmc_ws;
}

void __init at91sam9_pm_init(void)
{
	int ret;

	if (!IS_ENABLED(CONFIG_SOC_AT91SAM9))
		return;

	/*
	 * Force STANDBY and ULP0 mode to avoid calling
	 * at91_pm_modes_validate() which may increase booting time.
	 * Platform supports anyway only STANDBY and ULP0 modes.
	 */
	soc_pm.data.standby_mode = AT91_PM_STANDBY;
	soc_pm.data.suspend_mode = AT91_PM_ULP0;

	ret = at91_dt_ramc(false);
	if (ret)
		return;

	at91_pm_init(at91sam9_idle);
}

void __init sama5_pm_init(void)
{
	static const int modes[] __initconst = {
		AT91_PM_STANDBY, AT91_PM_ULP0, AT91_PM_ULP0_FAST,
	};
	static const u32 iomaps[] __initconst = {
		[AT91_PM_ULP0]		= AT91_PM_IOMAP(ETHC),
		[AT91_PM_ULP0_FAST]	= AT91_PM_IOMAP(ETHC),
	};
	int ret;

	if (!IS_ENABLED(CONFIG_SOC_SAMA5))
		return;

	at91_pm_modes_validate(modes, ARRAY_SIZE(modes));
	at91_pm_modes_init(iomaps, ARRAY_SIZE(iomaps));
	ret = at91_dt_ramc(false);
	if (ret)
		return;

	at91_pm_init(NULL);

	/* Quirks applies to ULP0, ULP0 fast and ULP1 modes. */
	soc_pm.quirks.eth[AT91_PM_G_ETH].modes = BIT(AT91_PM_ULP0) |
						 BIT(AT91_PM_ULP0_FAST) |
						 BIT(AT91_PM_ULP1);
	/* Do not suspend in ULP0, ULP0 fast if GETH is the only wakeup source. */
	soc_pm.quirks.eth[AT91_PM_G_ETH].dns_modes = BIT(AT91_PM_ULP0) |
						     BIT(AT91_PM_ULP0_FAST);
}

void __init sama5d2_pm_init(void)
{
	static const int modes[] __initconst = {
		AT91_PM_STANDBY, AT91_PM_ULP0, AT91_PM_ULP0_FAST, AT91_PM_ULP1,
		AT91_PM_BACKUP,
	};
	static const u32 iomaps[] __initconst = {
		[AT91_PM_ULP0]		= AT91_PM_IOMAP(ETHC),
		[AT91_PM_ULP0_FAST]	= AT91_PM_IOMAP(ETHC),
		[AT91_PM_ULP1]		= AT91_PM_IOMAP(SHDWC) |
					  AT91_PM_IOMAP(ETHC),
		[AT91_PM_BACKUP]	= AT91_PM_IOMAP(SHDWC) |
					  AT91_PM_IOMAP(SFRBU),
	};
	int ret;

	if (!IS_ENABLED(CONFIG_SOC_SAMA5D2))
		return;

	if (IS_ENABLED(CONFIG_ATMEL_SECURE_PM)) {
		pr_warn("AT91: Secure PM: ignoring standby mode\n");
		at91_pm_secure_init();
		return;
	}

	at91_pm_modes_validate(modes, ARRAY_SIZE(modes));
	at91_pm_modes_init(iomaps, ARRAY_SIZE(iomaps));
	ret = at91_dt_ramc(false);
	if (ret)
		return;

	at91_pm_init(NULL);

	soc_pm.ws_ids = sama5d2_ws_ids;
	soc_pm.config_shdwc_ws = at91_sama5d2_config_shdwc_ws;
	soc_pm.config_pmc_ws = at91_sama5d2_config_pmc_ws;

	soc_pm.sfrbu_regs.pswbu.key = (0x4BD20C << 8);
	soc_pm.sfrbu_regs.pswbu.ctrl = BIT(0);
	soc_pm.sfrbu_regs.pswbu.softsw = BIT(1);
	soc_pm.sfrbu_regs.pswbu.state = BIT(3);

	/* Quirk applies to ULP0, ULP0 fast and ULP1 modes. */
	soc_pm.quirks.eth[AT91_PM_G_ETH].modes = BIT(AT91_PM_ULP0) |
						 BIT(AT91_PM_ULP0_FAST) |
						 BIT(AT91_PM_ULP1);
	/*
	 * Do not suspend in ULP0, ULP0 fast if GETH is the only wakeup
	 * source.
	 */
	soc_pm.quirks.eth[AT91_PM_G_ETH].dns_modes = BIT(AT91_PM_ULP0) |
						     BIT(AT91_PM_ULP0_FAST);
}

void __init sama7_pm_init(void)
{
	static const int modes[] __initconst = {
		AT91_PM_STANDBY, AT91_PM_ULP0, AT91_PM_ULP1, AT91_PM_BACKUP,
	};
	static const u32 iomaps[] __initconst = {
		[AT91_PM_ULP0]		= AT91_PM_IOMAP(SFRBU),
		[AT91_PM_ULP1]		= AT91_PM_IOMAP(SFRBU) |
					  AT91_PM_IOMAP(SHDWC) |
					  AT91_PM_IOMAP(ETHC),
		[AT91_PM_BACKUP]	= AT91_PM_IOMAP(SFRBU) |
					  AT91_PM_IOMAP(SHDWC),
	};
	int ret;

	if (!IS_ENABLED(CONFIG_SOC_SAMA7))
		return;

	at91_pm_modes_validate(modes, ARRAY_SIZE(modes));

	ret = at91_dt_ramc(true);
	if (ret)
		return;

	at91_pm_modes_init(iomaps, ARRAY_SIZE(iomaps));
	at91_pm_init(NULL);

	soc_pm.ws_ids = sama7g5_ws_ids;
	soc_pm.config_pmc_ws = at91_sam9x60_config_pmc_ws;

	soc_pm.sfrbu_regs.pswbu.key = (0x4BD20C << 8);
	soc_pm.sfrbu_regs.pswbu.ctrl = BIT(0);
	soc_pm.sfrbu_regs.pswbu.softsw = BIT(1);
	soc_pm.sfrbu_regs.pswbu.state = BIT(2);

	/* Quirks applies to ULP1 for both Ethernet interfaces. */
	soc_pm.quirks.eth[AT91_PM_E_ETH].modes = BIT(AT91_PM_ULP1);
	soc_pm.quirks.eth[AT91_PM_G_ETH].modes = BIT(AT91_PM_ULP1);
}

static int __init at91_pm_modes_select(char *str)
{
	char *s;
	substring_t args[MAX_OPT_ARGS];
	int standby, suspend;

	if (!str)
		return 0;

	s = strsep(&str, ",");
	standby = match_token(s, pm_modes, args);
	if (standby < 0)
		return 0;

	suspend = match_token(str, pm_modes, args);
	if (suspend < 0)
		return 0;

	soc_pm.data.standby_mode = standby;
	soc_pm.data.suspend_mode = suspend;

	return 0;
}
early_param("atmel.pm_modes", at91_pm_modes_select);
