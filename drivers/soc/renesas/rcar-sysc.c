/*
 * R-Car SYSC Power management support
 *
 * Copyright (C) 2014  Magnus Damm
 * Copyright (C) 2015-2017 Glider bvba
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/clk/renesas.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/of_address.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/soc/renesas/rcar-sysc.h>

#include "rcar-sysc.h"

/* SYSC Common */
#define SYSCSR			0x00	/* SYSC Status Register */
#define SYSCISR			0x04	/* Interrupt Status Register */
#define SYSCISCR		0x08	/* Interrupt Status Clear Register */
#define SYSCIER			0x0c	/* Interrupt Enable Register */
#define SYSCIMR			0x10	/* Interrupt Mask Register */

/* SYSC Status Register */
#define SYSCSR_PONENB		1	/* Ready for power resume requests */
#define SYSCSR_POFFENB		0	/* Ready for power shutoff requests */

/*
 * Power Control Register Offsets inside the register block for each domain
 * Note: The "CR" registers for ARM cores exist on H1 only
 *	 Use WFI to power off, CPG/APMU to resume ARM cores on R-Car Gen2
 *	 Use PSCI on R-Car Gen3
 */
#define PWRSR_OFFS		0x00	/* Power Status Register */
#define PWROFFCR_OFFS		0x04	/* Power Shutoff Control Register */
#define PWROFFSR_OFFS		0x08	/* Power Shutoff Status Register */
#define PWRONCR_OFFS		0x0c	/* Power Resume Control Register */
#define PWRONSR_OFFS		0x10	/* Power Resume Status Register */
#define PWRER_OFFS		0x14	/* Power Shutoff/Resume Error */


#define SYSCSR_RETRIES		100
#define SYSCSR_DELAY_US		1

#define PWRER_RETRIES		100
#define PWRER_DELAY_US		1

#define SYSCISR_RETRIES		1000
#define SYSCISR_DELAY_US	1

#define RCAR_PD_ALWAYS_ON	32	/* Always-on power area */

static void __iomem *rcar_sysc_base;
static DEFINE_SPINLOCK(rcar_sysc_lock); /* SMP CPUs + I/O devices */

static int rcar_sysc_pwr_on_off(const struct rcar_sysc_ch *sysc_ch, bool on)
{
	unsigned int sr_bit, reg_offs;
	int k;

	if (on) {
		sr_bit = SYSCSR_PONENB;
		reg_offs = PWRONCR_OFFS;
	} else {
		sr_bit = SYSCSR_POFFENB;
		reg_offs = PWROFFCR_OFFS;
	}

	/* Wait until SYSC is ready to accept a power request */
	for (k = 0; k < SYSCSR_RETRIES; k++) {
		if (ioread32(rcar_sysc_base + SYSCSR) & BIT(sr_bit))
			break;
		udelay(SYSCSR_DELAY_US);
	}

	if (k == SYSCSR_RETRIES)
		return -EAGAIN;

	/* Submit power shutoff or power resume request */
	iowrite32(BIT(sysc_ch->chan_bit),
		  rcar_sysc_base + sysc_ch->chan_offs + reg_offs);

	return 0;
}

static int rcar_sysc_power(const struct rcar_sysc_ch *sysc_ch, bool on)
{
	unsigned int isr_mask = BIT(sysc_ch->isr_bit);
	unsigned int chan_mask = BIT(sysc_ch->chan_bit);
	unsigned int status;
	unsigned long flags;
	int ret = 0;
	int k;

	spin_lock_irqsave(&rcar_sysc_lock, flags);

	iowrite32(isr_mask, rcar_sysc_base + SYSCISCR);

	/* Submit power shutoff or resume request until it was accepted */
	for (k = 0; k < PWRER_RETRIES; k++) {
		ret = rcar_sysc_pwr_on_off(sysc_ch, on);
		if (ret)
			goto out;

		status = ioread32(rcar_sysc_base +
				  sysc_ch->chan_offs + PWRER_OFFS);
		if (!(status & chan_mask))
			break;

		udelay(PWRER_DELAY_US);
	}

	if (k == PWRER_RETRIES) {
		ret = -EIO;
		goto out;
	}

	/* Wait until the power shutoff or resume request has completed * */
	for (k = 0; k < SYSCISR_RETRIES; k++) {
		if (ioread32(rcar_sysc_base + SYSCISR) & isr_mask)
			break;
		udelay(SYSCISR_DELAY_US);
	}

	if (k == SYSCISR_RETRIES)
		ret = -EIO;

	iowrite32(isr_mask, rcar_sysc_base + SYSCISCR);

 out:
	spin_unlock_irqrestore(&rcar_sysc_lock, flags);

	pr_debug("sysc power %s domain %d: %08x -> %d\n", on ? "on" : "off",
		 sysc_ch->isr_bit, ioread32(rcar_sysc_base + SYSCISR), ret);
	return ret;
}

int rcar_sysc_power_down(const struct rcar_sysc_ch *sysc_ch)
{
	return rcar_sysc_power(sysc_ch, false);
}

int rcar_sysc_power_up(const struct rcar_sysc_ch *sysc_ch)
{
	return rcar_sysc_power(sysc_ch, true);
}

static bool rcar_sysc_power_is_off(const struct rcar_sysc_ch *sysc_ch)
{
	unsigned int st;

	st = ioread32(rcar_sysc_base + sysc_ch->chan_offs + PWRSR_OFFS);
	if (st & BIT(sysc_ch->chan_bit))
		return true;

	return false;
}

struct rcar_sysc_pd {
	struct generic_pm_domain genpd;
	struct rcar_sysc_ch ch;
	unsigned int flags;
	char name[0];
};

static inline struct rcar_sysc_pd *to_rcar_pd(struct generic_pm_domain *d)
{
	return container_of(d, struct rcar_sysc_pd, genpd);
}

static int rcar_sysc_pd_power_off(struct generic_pm_domain *genpd)
{
	struct rcar_sysc_pd *pd = to_rcar_pd(genpd);

	pr_debug("%s: %s\n", __func__, genpd->name);
	return rcar_sysc_power_down(&pd->ch);
}

static int rcar_sysc_pd_power_on(struct generic_pm_domain *genpd)
{
	struct rcar_sysc_pd *pd = to_rcar_pd(genpd);

	pr_debug("%s: %s\n", __func__, genpd->name);
	return rcar_sysc_power_up(&pd->ch);
}

static bool has_cpg_mstp;

static void __init rcar_sysc_pd_setup(struct rcar_sysc_pd *pd)
{
	struct generic_pm_domain *genpd = &pd->genpd;
	const char *name = pd->genpd.name;
	struct dev_power_governor *gov = &simple_qos_governor;

	if (pd->flags & PD_CPU) {
		/*
		 * This domain contains a CPU core and therefore it should
		 * only be turned off if the CPU is not in use.
		 */
		pr_debug("PM domain %s contains %s\n", name, "CPU");
		genpd->flags |= GENPD_FLAG_ALWAYS_ON;
	} else if (pd->flags & PD_SCU) {
		/*
		 * This domain contains an SCU and cache-controller, and
		 * therefore it should only be turned off if the CPU cores are
		 * not in use.
		 */
		pr_debug("PM domain %s contains %s\n", name, "SCU");
		genpd->flags |= GENPD_FLAG_ALWAYS_ON;
	} else if (pd->flags & PD_NO_CR) {
		/*
		 * This domain cannot be turned off.
		 */
		genpd->flags |= GENPD_FLAG_ALWAYS_ON;
	}

	if (!(pd->flags & (PD_CPU | PD_SCU))) {
		/* Enable Clock Domain for I/O devices */
		genpd->flags |= GENPD_FLAG_PM_CLK | GENPD_FLAG_ACTIVE_WAKEUP;
		if (has_cpg_mstp) {
			genpd->attach_dev = cpg_mstp_attach_dev;
			genpd->detach_dev = cpg_mstp_detach_dev;
		} else {
			genpd->attach_dev = cpg_mssr_attach_dev;
			genpd->detach_dev = cpg_mssr_detach_dev;
		}
	}

	genpd->power_off = rcar_sysc_pd_power_off;
	genpd->power_on = rcar_sysc_pd_power_on;

	if (pd->flags & (PD_CPU | PD_NO_CR)) {
		/* Skip CPUs (handled by SMP code) and areas without control */
		pr_debug("%s: Not touching %s\n", __func__, genpd->name);
		goto finalize;
	}

	if (!rcar_sysc_power_is_off(&pd->ch)) {
		pr_debug("%s: %s is already powered\n", __func__, genpd->name);
		goto finalize;
	}

	rcar_sysc_power_up(&pd->ch);

finalize:
	pm_genpd_init(genpd, gov, false);
}

static const struct of_device_id rcar_sysc_matches[] __initconst = {
#ifdef CONFIG_SYSC_R8A7743
	{ .compatible = "renesas,r8a7743-sysc", .data = &r8a7743_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A7745
	{ .compatible = "renesas,r8a7745-sysc", .data = &r8a7745_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A7779
	{ .compatible = "renesas,r8a7779-sysc", .data = &r8a7779_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A7790
	{ .compatible = "renesas,r8a7790-sysc", .data = &r8a7790_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A7791
	{ .compatible = "renesas,r8a7791-sysc", .data = &r8a7791_sysc_info },
	/* R-Car M2-N is identical to R-Car M2-W w.r.t. power domains. */
	{ .compatible = "renesas,r8a7793-sysc", .data = &r8a7791_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A7792
	{ .compatible = "renesas,r8a7792-sysc", .data = &r8a7792_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A7794
	{ .compatible = "renesas,r8a7794-sysc", .data = &r8a7794_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A7795
	{ .compatible = "renesas,r8a7795-sysc", .data = &r8a7795_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A7796
	{ .compatible = "renesas,r8a7796-sysc", .data = &r8a7796_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A77965
	{ .compatible = "renesas,r8a77965-sysc", .data = &r8a77965_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A77970
	{ .compatible = "renesas,r8a77970-sysc", .data = &r8a77970_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A77980
	{ .compatible = "renesas,r8a77980-sysc", .data = &r8a77980_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A77995
	{ .compatible = "renesas,r8a77995-sysc", .data = &r8a77995_sysc_info },
#endif
	{ /* sentinel */ }
};

struct rcar_pm_domains {
	struct genpd_onecell_data onecell_data;
	struct generic_pm_domain *domains[RCAR_PD_ALWAYS_ON + 1];
};

static int __init rcar_sysc_pd_init(void)
{
	const struct rcar_sysc_info *info;
	const struct of_device_id *match;
	struct rcar_pm_domains *domains;
	struct device_node *np;
	u32 syscier, syscimr;
	void __iomem *base;
	unsigned int i;
	int error;

	if (rcar_sysc_base)
		return 0;

	np = of_find_matching_node_and_match(NULL, rcar_sysc_matches, &match);
	if (!np)
		return -ENODEV;

	info = match->data;

	if (info->init) {
		error = info->init();
		if (error)
			return error;
	}

	has_cpg_mstp = of_find_compatible_node(NULL, NULL,
					       "renesas,cpg-mstp-clocks");

	base = of_iomap(np, 0);
	if (!base) {
		pr_warn("%pOF: Cannot map regs\n", np);
		error = -ENOMEM;
		goto out_put;
	}

	rcar_sysc_base = base;

	domains = kzalloc(sizeof(*domains), GFP_KERNEL);
	if (!domains) {
		error = -ENOMEM;
		goto out_put;
	}

	domains->onecell_data.domains = domains->domains;
	domains->onecell_data.num_domains = ARRAY_SIZE(domains->domains);

	for (i = 0, syscier = 0; i < info->num_areas; i++)
		syscier |= BIT(info->areas[i].isr_bit);

	/*
	 * Mask all interrupt sources to prevent the CPU from receiving them.
	 * Make sure not to clear reserved bits that were set before.
	 */
	syscimr = ioread32(base + SYSCIMR);
	syscimr |= syscier;
	pr_debug("%pOF: syscimr = 0x%08x\n", np, syscimr);
	iowrite32(syscimr, base + SYSCIMR);

	/*
	 * SYSC needs all interrupt sources enabled to control power.
	 */
	pr_debug("%pOF: syscier = 0x%08x\n", np, syscier);
	iowrite32(syscier, base + SYSCIER);

	for (i = 0; i < info->num_areas; i++) {
		const struct rcar_sysc_area *area = &info->areas[i];
		struct rcar_sysc_pd *pd;

		if (!area->name) {
			/* Skip NULLified area */
			continue;
		}

		pd = kzalloc(sizeof(*pd) + strlen(area->name) + 1, GFP_KERNEL);
		if (!pd) {
			error = -ENOMEM;
			goto out_put;
		}

		strcpy(pd->name, area->name);
		pd->genpd.name = pd->name;
		pd->ch.chan_offs = area->chan_offs;
		pd->ch.chan_bit = area->chan_bit;
		pd->ch.isr_bit = area->isr_bit;
		pd->flags = area->flags;

		rcar_sysc_pd_setup(pd);
		if (area->parent >= 0)
			pm_genpd_add_subdomain(domains->domains[area->parent],
					       &pd->genpd);

		domains->domains[area->isr_bit] = &pd->genpd;
	}

	error = of_genpd_add_provider_onecell(np, &domains->onecell_data);

out_put:
	of_node_put(np);
	return error;
}
early_initcall(rcar_sysc_pd_init);

void __init rcar_sysc_nullify(struct rcar_sysc_area *areas,
			      unsigned int num_areas, u8 id)
{
	unsigned int i;

	for (i = 0; i < num_areas; i++)
		if (areas[i].isr_bit == id) {
			areas[i].name = NULL;
			return;
		}
}

void __init rcar_sysc_init(phys_addr_t base, u32 syscier)
{
	u32 syscimr;

	if (!rcar_sysc_pd_init())
		return;

	rcar_sysc_base = ioremap_nocache(base, PAGE_SIZE);

	/*
	 * Mask all interrupt sources to prevent the CPU from receiving them.
	 * Make sure not to clear reserved bits that were set before.
	 */
	syscimr = ioread32(rcar_sysc_base + SYSCIMR);
	syscimr |= syscier;
	pr_debug("%s: syscimr = 0x%08x\n", __func__, syscimr);
	iowrite32(syscimr, rcar_sysc_base + SYSCIMR);

	/*
	 * SYSC needs all interrupt sources enabled to control power.
	 */
	pr_debug("%s: syscier = 0x%08x\n", __func__, syscier);
	iowrite32(syscier, rcar_sysc_base + SYSCIER);
}
