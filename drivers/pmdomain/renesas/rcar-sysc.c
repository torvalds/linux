// SPDX-License-Identifier: GPL-2.0
/*
 * R-Car SYSC Power management support
 *
 * Copyright (C) 2014  Magnus Damm
 * Copyright (C) 2015-2017 Glider bvba
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
#include <linux/iopoll.h>
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


#define SYSCSR_TIMEOUT		1000
#define SYSCSR_DELAY_US		1

#define PWRER_RETRIES		1000
#define PWRER_DELAY_US		1

#define SYSCISR_TIMEOUT		1000
#define SYSCISR_DELAY_US	1

#define RCAR_PD_ALWAYS_ON	32	/* Always-on power area */

struct rcar_sysc_pd {
	struct generic_pm_domain genpd;
	u16 chan_offs;
	u8 chan_bit;
	u8 isr_bit;
	unsigned int flags;
	char name[];
};

static void __iomem *rcar_sysc_base;
static DEFINE_SPINLOCK(rcar_sysc_lock); /* SMP CPUs + I/O devices */
static u32 rcar_sysc_extmask_offs, rcar_sysc_extmask_val;

static int rcar_sysc_pwr_on_off(const struct rcar_sysc_pd *pd, bool on)
{
	unsigned int sr_bit, reg_offs;
	u32 val;
	int ret;

	if (on) {
		sr_bit = SYSCSR_PONENB;
		reg_offs = PWRONCR_OFFS;
	} else {
		sr_bit = SYSCSR_POFFENB;
		reg_offs = PWROFFCR_OFFS;
	}

	/* Wait until SYSC is ready to accept a power request */
	ret = readl_poll_timeout_atomic(rcar_sysc_base + SYSCSR, val,
					val & BIT(sr_bit), SYSCSR_DELAY_US,
					SYSCSR_TIMEOUT);
	if (ret)
		return -EAGAIN;

	/* Power-off delay quirk */
	if (!on && (pd->flags & PD_OFF_DELAY))
		udelay(1);

	/* Submit power shutoff or power resume request */
	iowrite32(BIT(pd->chan_bit), rcar_sysc_base + pd->chan_offs + reg_offs);

	return 0;
}

static int rcar_sysc_power(const struct rcar_sysc_pd *pd, bool on)
{
	unsigned int isr_mask = BIT(pd->isr_bit);
	unsigned int chan_mask = BIT(pd->chan_bit);
	unsigned int status, k;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&rcar_sysc_lock, flags);

	/*
	 * Mask external power requests for CPU or 3DG domains
	 */
	if (rcar_sysc_extmask_val) {
		iowrite32(rcar_sysc_extmask_val,
			  rcar_sysc_base + rcar_sysc_extmask_offs);
	}

	/*
	 * The interrupt source needs to be enabled, but masked, to prevent the
	 * CPU from receiving it.
	 */
	iowrite32(ioread32(rcar_sysc_base + SYSCIMR) | isr_mask,
		  rcar_sysc_base + SYSCIMR);
	iowrite32(ioread32(rcar_sysc_base + SYSCIER) | isr_mask,
		  rcar_sysc_base + SYSCIER);

	iowrite32(isr_mask, rcar_sysc_base + SYSCISCR);

	/* Submit power shutoff or resume request until it was accepted */
	for (k = 0; k < PWRER_RETRIES; k++) {
		ret = rcar_sysc_pwr_on_off(pd, on);
		if (ret)
			goto out;

		status = ioread32(rcar_sysc_base + pd->chan_offs + PWRER_OFFS);
		if (!(status & chan_mask))
			break;

		udelay(PWRER_DELAY_US);
	}

	if (k == PWRER_RETRIES) {
		ret = -EIO;
		goto out;
	}

	/* Wait until the power shutoff or resume request has completed * */
	ret = readl_poll_timeout_atomic(rcar_sysc_base + SYSCISR, status,
					status & isr_mask, SYSCISR_DELAY_US,
					SYSCISR_TIMEOUT);
	if (ret)
		ret = -EIO;

	iowrite32(isr_mask, rcar_sysc_base + SYSCISCR);

 out:
	if (rcar_sysc_extmask_val)
		iowrite32(0, rcar_sysc_base + rcar_sysc_extmask_offs);

	spin_unlock_irqrestore(&rcar_sysc_lock, flags);

	pr_debug("sysc power %s domain %d: %08x -> %d\n", on ? "on" : "off",
		 pd->isr_bit, ioread32(rcar_sysc_base + SYSCISR), ret);
	return ret;
}

static bool rcar_sysc_power_is_off(const struct rcar_sysc_pd *pd)
{
	unsigned int st;

	st = ioread32(rcar_sysc_base + pd->chan_offs + PWRSR_OFFS);
	if (st & BIT(pd->chan_bit))
		return true;

	return false;
}

static inline struct rcar_sysc_pd *to_rcar_pd(struct generic_pm_domain *d)
{
	return container_of(d, struct rcar_sysc_pd, genpd);
}

static int rcar_sysc_pd_power_off(struct generic_pm_domain *genpd)
{
	struct rcar_sysc_pd *pd = to_rcar_pd(genpd);

	pr_debug("%s: %s\n", __func__, genpd->name);
	return rcar_sysc_power(pd, false);
}

static int rcar_sysc_pd_power_on(struct generic_pm_domain *genpd)
{
	struct rcar_sysc_pd *pd = to_rcar_pd(genpd);

	pr_debug("%s: %s\n", __func__, genpd->name);
	return rcar_sysc_power(pd, true);
}

static bool has_cpg_mstp;

static int __init rcar_sysc_pd_setup(struct rcar_sysc_pd *pd)
{
	struct generic_pm_domain *genpd = &pd->genpd;
	const char *name = pd->genpd.name;
	int error;

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

	if (!rcar_sysc_power_is_off(pd)) {
		pr_debug("%s: %s is already powered\n", __func__, genpd->name);
		goto finalize;
	}

	rcar_sysc_power(pd, true);

finalize:
	error = pm_genpd_init(genpd, &simple_qos_governor, false);
	if (error)
		pr_err("Failed to init PM domain %s: %d\n", name, error);

	return error;
}

static const struct of_device_id rcar_sysc_matches[] __initconst = {
#ifdef CONFIG_SYSC_R8A7742
	{ .compatible = "renesas,r8a7742-sysc", .data = &r8a7742_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A7743
	{ .compatible = "renesas,r8a7743-sysc", .data = &r8a7743_sysc_info },
	/* RZ/G1N is identical to RZ/G2M w.r.t. power domains. */
	{ .compatible = "renesas,r8a7744-sysc", .data = &r8a7743_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A7745
	{ .compatible = "renesas,r8a7745-sysc", .data = &r8a7745_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A77470
	{ .compatible = "renesas,r8a77470-sysc", .data = &r8a77470_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A774A1
	{ .compatible = "renesas,r8a774a1-sysc", .data = &r8a774a1_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A774B1
	{ .compatible = "renesas,r8a774b1-sysc", .data = &r8a774b1_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A774C0
	{ .compatible = "renesas,r8a774c0-sysc", .data = &r8a774c0_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A774E1
	{ .compatible = "renesas,r8a774e1-sysc", .data = &r8a774e1_sysc_info },
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
#ifdef CONFIG_SYSC_R8A77960
	{ .compatible = "renesas,r8a7796-sysc", .data = &r8a77960_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A77961
	{ .compatible = "renesas,r8a77961-sysc", .data = &r8a77961_sysc_info },
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
#ifdef CONFIG_SYSC_R8A77990
	{ .compatible = "renesas,r8a77990-sysc", .data = &r8a77990_sysc_info },
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

static struct genpd_onecell_data *rcar_sysc_onecell_data;

static int __init rcar_sysc_pd_init(void)
{
	const struct rcar_sysc_info *info;
	const struct of_device_id *match;
	struct rcar_pm_domains *domains;
	struct device_node *np;
	void __iomem *base;
	unsigned int i;
	int error;

	np = of_find_matching_node_and_match(NULL, rcar_sysc_matches, &match);
	if (!np)
		return -ENODEV;

	info = match->data;

	if (info->init) {
		error = info->init();
		if (error)
			goto out_put;
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

	/* Optional External Request Mask Register */
	rcar_sysc_extmask_offs = info->extmask_offs;
	rcar_sysc_extmask_val = info->extmask_val;

	domains = kzalloc(sizeof(*domains), GFP_KERNEL);
	if (!domains) {
		error = -ENOMEM;
		goto out_put;
	}

	domains->onecell_data.domains = domains->domains;
	domains->onecell_data.num_domains = ARRAY_SIZE(domains->domains);
	rcar_sysc_onecell_data = &domains->onecell_data;

	for (i = 0; i < info->num_areas; i++) {
		const struct rcar_sysc_area *area = &info->areas[i];
		struct rcar_sysc_pd *pd;
		size_t n;

		n = strlen(area->name) + 1;
		pd = kzalloc(sizeof(*pd) + n, GFP_KERNEL);
		if (!pd) {
			error = -ENOMEM;
			goto out_put;
		}

		memcpy(pd->name, area->name, n);
		pd->genpd.name = pd->name;
		pd->chan_offs = area->chan_offs;
		pd->chan_bit = area->chan_bit;
		pd->isr_bit = area->isr_bit;
		pd->flags = area->flags;

		error = rcar_sysc_pd_setup(pd);
		if (error)
			goto out_put;

		domains->domains[area->isr_bit] = &pd->genpd;

		if (area->parent < 0)
			continue;

		error = pm_genpd_add_subdomain(domains->domains[area->parent],
					       &pd->genpd);
		if (error) {
			pr_warn("Failed to add PM subdomain %s to parent %u\n",
				area->name, area->parent);
			goto out_put;
		}
	}

	error = of_genpd_add_provider_onecell(np, &domains->onecell_data);

out_put:
	of_node_put(np);
	return error;
}
early_initcall(rcar_sysc_pd_init);

#ifdef CONFIG_ARCH_R8A7779
static int rcar_sysc_power_cpu(unsigned int idx, bool on)
{
	struct generic_pm_domain *genpd;
	struct rcar_sysc_pd *pd;
	unsigned int i;

	if (!rcar_sysc_onecell_data)
		return -ENODEV;

	for (i = 0; i < rcar_sysc_onecell_data->num_domains; i++) {
		genpd = rcar_sysc_onecell_data->domains[i];
		if (!genpd)
			continue;

		pd = to_rcar_pd(genpd);
		if (!(pd->flags & PD_CPU) || pd->chan_bit != idx)
			continue;

		return rcar_sysc_power(pd, on);
	}

	return -ENOENT;
}

int rcar_sysc_power_down_cpu(unsigned int cpu)
{
	return rcar_sysc_power_cpu(cpu, false);
}

int rcar_sysc_power_up_cpu(unsigned int cpu)
{
	return rcar_sysc_power_cpu(cpu, true);
}
#endif /* CONFIG_ARCH_R8A7779 */
