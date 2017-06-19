/*
 * arch/arm/mach-at91/pm.c
 * AT91 Power Management
 *
 * Copyright (C) 2005 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/suspend.h>

#include <linux/clk/at91_pmc.h>

#include <asm/cacheflush.h>
#include <asm/fncpy.h>
#include <asm/system_misc.h>

#include "generic.h"
#include "pm.h"

/*
 * FIXME: this is needed to communicate between the pinctrl driver and
 * the PM implementation in the machine. Possibly part of the PM
 * implementation should be moved down into the pinctrl driver and get
 * called as part of the generic suspend/resume path.
 */
#ifdef CONFIG_PINCTRL_AT91
extern void at91_pinctrl_gpio_suspend(void);
extern void at91_pinctrl_gpio_resume(void);
#endif

static struct at91_pm_data pm_data;

#define at91_ramc_read(id, field) \
	__raw_readl(pm_data.ramc[id] + field)

#define at91_ramc_write(id, field, value) \
	__raw_writel(value, pm_data.ramc[id] + field)

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


static suspend_state_t target_state;

/*
 * Called after processes are frozen, but before we shutdown devices.
 */
static int at91_pm_begin(suspend_state_t state)
{
	target_state = state;
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

	scsr = readl(pm_data.pmc + AT91_PMC_SCSR);

	/* USB must not be using PLLB */
	if ((scsr & pm_data.uhp_udp_mask) != 0) {
		pr_err("AT91: PM - Suspend-to-RAM with USB still active\n");
		return 0;
	}

	/* PCK0..PCK3 must be disabled, or configured to use clk32k */
	for (i = 0; i < 4; i++) {
		u32 css;

		if ((scsr & (AT91_PMC_PCK0 << i)) == 0)
			continue;
		css = readl(pm_data.pmc + AT91_PMC_PCKR(i)) & AT91_PMC_CSS;
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
	return (target_state == PM_SUSPEND_MEM);
}
EXPORT_SYMBOL(at91_suspend_entering_slow_clock);

static void (*at91_suspend_sram_fn)(struct at91_pm_data *);
extern void at91_pm_suspend_in_sram(struct at91_pm_data *pm_data);
extern u32 at91_pm_suspend_in_sram_sz;

static void at91_pm_suspend(suspend_state_t state)
{
	pm_data.mode = (state == PM_SUSPEND_MEM) ? AT91_PM_SLOW_CLOCK : 0;

	flush_cache_all();
	outer_disable();

	at91_suspend_sram_fn(&pm_data);

	outer_resume();
}

static int at91_pm_enter(suspend_state_t state)
{
#ifdef CONFIG_PINCTRL_AT91
	at91_pinctrl_gpio_suspend();
#endif
	switch (state) {
	/*
	 * Suspend-to-RAM is like STANDBY plus slow clock mode, so
	 * drivers must suspend more deeply, the master clock switches
	 * to the clk32k and turns off the main oscillator
	 */
	case PM_SUSPEND_MEM:
		/*
		 * Ensure that clocks are in a valid state.
		 */
		if (!at91_pm_verify_clocks())
			goto error;

		at91_pm_suspend(state);

		break;

	/*
	 * STANDBY mode has *all* drivers suspended; ignores irqs not
	 * marked as 'wakeup' event sources; and reduces DRAM power.
	 * But otherwise it's identical to PM_SUSPEND_ON: cpu idle, and
	 * nothing fancy done with main or cpu clocks.
	 */
	case PM_SUSPEND_STANDBY:
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
	target_state = PM_SUSPEND_ON;

#ifdef CONFIG_PINCTRL_AT91
	at91_pinctrl_gpio_resume();
#endif
	return 0;
}

/*
 * Called right prior to thawing processes.
 */
static void at91_pm_end(void)
{
	target_state = PM_SUSPEND_ON;
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
		: "r" (0), "r" (pm_data.ramc[0]),
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

	if (pm_data.ramc[1]) {
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
	if (pm_data.ramc[1])
		at91_ramc_write(1, AT91_DDRSDRC_LPR, lpr1);

	cpu_do_idle();

	at91_ramc_write(0, AT91_DDRSDRC_MDR, saved_mdr0);
	at91_ramc_write(0, AT91_DDRSDRC_LPR, saved_lpr0);
	if (pm_data.ramc[1]) {
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

	if (pm_data.ramc[1]) {
		saved_lpr1 = at91_ramc_read(1, AT91_SDRAMC_LPR);
		lpr1 = saved_lpr1 & ~AT91_SDRAMC_LPCB;
		lpr1 |= AT91_SDRAMC_LPCB_SELF_REFRESH;
	}

	saved_lpr0 = at91_ramc_read(0, AT91_SDRAMC_LPR);
	lpr0 = saved_lpr0 & ~AT91_SDRAMC_LPCB;
	lpr0 |= AT91_SDRAMC_LPCB_SELF_REFRESH;

	/* self-refresh mode now */
	at91_ramc_write(0, AT91_SDRAMC_LPR, lpr0);
	if (pm_data.ramc[1])
		at91_ramc_write(1, AT91_SDRAMC_LPR, lpr1);

	cpu_do_idle();

	at91_ramc_write(0, AT91_SDRAMC_LPR, saved_lpr0);
	if (pm_data.ramc[1])
		at91_ramc_write(1, AT91_SDRAMC_LPR, saved_lpr1);
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
};

static const struct of_device_id ramc_ids[] __initconst = {
	{ .compatible = "atmel,at91rm9200-sdramc", .data = &ramc_infos[0] },
	{ .compatible = "atmel,at91sam9260-sdramc", .data = &ramc_infos[1] },
	{ .compatible = "atmel,at91sam9g45-ddramc", .data = &ramc_infos[2] },
	{ .compatible = "atmel,sama5d3-ddramc", .data = &ramc_infos[3] },
	{ /*sentinel*/ }
};

static __init void at91_dt_ramc(void)
{
	struct device_node *np;
	const struct of_device_id *of_id;
	int idx = 0;
	void *standby = NULL;
	const struct ramc_info *ramc;

	for_each_matching_node_and_match(np, ramc_ids, &of_id) {
		pm_data.ramc[idx] = of_iomap(np, 0);
		if (!pm_data.ramc[idx])
			panic(pr_fmt("unable to map ramc[%d] cpu registers\n"), idx);

		ramc = of_id->data;
		if (!standby)
			standby = ramc->idle;
		pm_data.memctrl = ramc->memctrl;

		idx++;
	}

	if (!idx)
		panic(pr_fmt("unable to find compatible ram controller node in dtb\n"));

	if (!standby) {
		pr_warn("ramc no standby function available\n");
		return;
	}

	at91_cpuidle_device.dev.platform_data = standby;
}

static void at91rm9200_idle(void)
{
	/*
	 * Disable the processor clock.  The processor will be automatically
	 * re-enabled by an interrupt or by a reset.
	 */
	writel(AT91_PMC_PCK, pm_data.pmc + AT91_PMC_SCDR);
}

static void at91sam9_idle(void)
{
	writel(AT91_PMC_PCK, pm_data.pmc + AT91_PMC_SCDR);
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
		return;
	}

	sram_base = gen_pool_alloc(sram_pool, at91_pm_suspend_in_sram_sz);
	if (!sram_base) {
		pr_warn("%s: unable to alloc sram!\n", __func__);
		return;
	}

	sram_pbase = gen_pool_virt_to_phys(sram_pool, sram_base);
	at91_suspend_sram_fn = __arm_ioremap_exec(sram_pbase,
					at91_pm_suspend_in_sram_sz, false);
	if (!at91_suspend_sram_fn) {
		pr_warn("SRAM: Could not map\n");
		return;
	}

	/* Copy the pm suspend handler to SRAM */
	at91_suspend_sram_fn = fncpy(at91_suspend_sram_fn,
			&at91_pm_suspend_in_sram, at91_pm_suspend_in_sram_sz);
}

struct pmc_info {
	unsigned long uhp_udp_mask;
};

static const struct pmc_info pmc_infos[] __initconst = {
	{ .uhp_udp_mask = AT91RM9200_PMC_UHP | AT91RM9200_PMC_UDP },
	{ .uhp_udp_mask = AT91SAM926x_PMC_UHP | AT91SAM926x_PMC_UDP },
	{ .uhp_udp_mask = AT91SAM926x_PMC_UHP },
};

static const struct of_device_id atmel_pmc_ids[] __initconst = {
	{ .compatible = "atmel,at91rm9200-pmc", .data = &pmc_infos[0] },
	{ .compatible = "atmel,at91sam9260-pmc", .data = &pmc_infos[1] },
	{ .compatible = "atmel,at91sam9g45-pmc", .data = &pmc_infos[2] },
	{ .compatible = "atmel,at91sam9n12-pmc", .data = &pmc_infos[1] },
	{ .compatible = "atmel,at91sam9x5-pmc", .data = &pmc_infos[1] },
	{ .compatible = "atmel,sama5d3-pmc", .data = &pmc_infos[1] },
	{ .compatible = "atmel,sama5d2-pmc", .data = &pmc_infos[1] },
	{ /* sentinel */ },
};

static void __init at91_pm_init(void (*pm_idle)(void))
{
	struct device_node *pmc_np;
	const struct of_device_id *of_id;
	const struct pmc_info *pmc;

	if (at91_cpuidle_device.dev.platform_data)
		platform_device_register(&at91_cpuidle_device);

	pmc_np = of_find_matching_node_and_match(NULL, atmel_pmc_ids, &of_id);
	pm_data.pmc = of_iomap(pmc_np, 0);
	if (!pm_data.pmc) {
		pr_err("AT91: PM not supported, PMC not found\n");
		return;
	}

	pmc = of_id->data;
	pm_data.uhp_udp_mask = pmc->uhp_udp_mask;

	if (pm_idle)
		arm_pm_idle = pm_idle;

	at91_pm_sram_init();

	if (at91_suspend_sram_fn)
		suspend_set_ops(&at91_pm_ops);
	else
		pr_info("AT91: PM not supported, due to no SRAM allocated\n");
}

void __init at91rm9200_pm_init(void)
{
	at91_dt_ramc();

	/*
	 * AT91RM9200 SDRAM low-power mode cannot be used with self-refresh.
	 */
	at91_ramc_write(0, AT91_MC_SDRAMC_LPR, 0);

	at91_pm_init(at91rm9200_idle);
}

void __init at91sam9_pm_init(void)
{
	at91_dt_ramc();
	at91_pm_init(at91sam9_idle);
}

void __init sama5_pm_init(void)
{
	at91_dt_ramc();
	at91_pm_init(NULL);
}
