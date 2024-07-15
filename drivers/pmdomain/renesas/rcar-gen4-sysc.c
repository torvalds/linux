// SPDX-License-Identifier: GPL-2.0
/*
 * R-Car Gen4 SYSC Power management support
 *
 * Copyright (C) 2021 Renesas Electronics Corp.
 */

#include <linux/bits.h>
#include <linux/clk/renesas.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/of_address.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "rcar-gen4-sysc.h"

/* SYSC Common */
#define SYSCSR		0x000	/* SYSC Status Register */
#define SYSCPONSR(x)	(0x800 + ((x) * 0x4)) /* Power-ON Status Register 0 */
#define SYSCPOFFSR(x)	(0x808 + ((x) * 0x4)) /* Power-OFF Status Register */
#define SYSCISCR(x)	(0x810 + ((x) * 0x4)) /* Interrupt Status/Clear Register */
#define SYSCIER(x)	(0x820 + ((x) * 0x4)) /* Interrupt Enable Register */
#define SYSCIMR(x)	(0x830 + ((x) * 0x4)) /* Interrupt Mask Register */

/* Power Domain Registers */
#define PDRSR(n)	(0x1000 + ((n) * 0x40))
#define PDRONCR(n)	(0x1004 + ((n) * 0x40))
#define PDROFFCR(n)	(0x1008 + ((n) * 0x40))
#define PDRESR(n)	(0x100C + ((n) * 0x40))

/* PWRON/PWROFF */
#define PWRON_PWROFF		BIT(0)	/* Power-ON/OFF request */

/* PDRESR */
#define PDRESR_ERR		BIT(0)

/* PDRSR */
#define PDRSR_OFF		BIT(0)	/* Power-OFF state */
#define PDRSR_ON		BIT(4)	/* Power-ON state */
#define PDRSR_OFF_STATE		BIT(8)  /* Processing Power-OFF sequence */
#define PDRSR_ON_STATE		BIT(12) /* Processing Power-ON sequence */

#define SYSCSR_BUSY		GENMASK(1, 0)	/* All bit sets is not busy */

#define SYSCSR_TIMEOUT		10000
#define SYSCSR_DELAY_US		1

#define PDRESR_RETRIES		10000
#define PDRESR_DELAY_US		1

#define SYSCISCR_TIMEOUT	10000
#define SYSCISCR_DELAY_US	1

#define RCAR_GEN4_PD_ALWAYS_ON	64
#define NUM_DOMAINS_EACH_REG	BITS_PER_TYPE(u32)

static void __iomem *rcar_gen4_sysc_base;
static DEFINE_SPINLOCK(rcar_gen4_sysc_lock); /* SMP CPUs + I/O devices */

static int rcar_gen4_sysc_pwr_on_off(u8 pdr, bool on)
{
	unsigned int reg_offs;
	u32 val;
	int ret;

	if (on)
		reg_offs = PDRONCR(pdr);
	else
		reg_offs = PDROFFCR(pdr);

	/* Wait until SYSC is ready to accept a power request */
	ret = readl_poll_timeout_atomic(rcar_gen4_sysc_base + SYSCSR, val,
					(val & SYSCSR_BUSY) == SYSCSR_BUSY,
					SYSCSR_DELAY_US, SYSCSR_TIMEOUT);
	if (ret < 0)
		return -EAGAIN;

	/* Submit power shutoff or power resume request */
	iowrite32(PWRON_PWROFF, rcar_gen4_sysc_base + reg_offs);

	return 0;
}

static int clear_irq_flags(unsigned int reg_idx, unsigned int isr_mask)
{
	u32 val;
	int ret;

	iowrite32(isr_mask, rcar_gen4_sysc_base + SYSCISCR(reg_idx));

	ret = readl_poll_timeout_atomic(rcar_gen4_sysc_base + SYSCISCR(reg_idx),
					val, !(val & isr_mask),
					SYSCISCR_DELAY_US, SYSCISCR_TIMEOUT);
	if (ret < 0) {
		pr_err("\n %s : Can not clear IRQ flags in SYSCISCR", __func__);
		return -EIO;
	}

	return 0;
}

static int rcar_gen4_sysc_power(u8 pdr, bool on)
{
	unsigned int isr_mask;
	unsigned int reg_idx, bit_idx;
	unsigned int status;
	unsigned long flags;
	int ret = 0;
	u32 val;
	int k;

	spin_lock_irqsave(&rcar_gen4_sysc_lock, flags);

	reg_idx = pdr / NUM_DOMAINS_EACH_REG;
	bit_idx = pdr % NUM_DOMAINS_EACH_REG;

	isr_mask = BIT(bit_idx);

	/*
	 * The interrupt source needs to be enabled, but masked, to prevent the
	 * CPU from receiving it.
	 */
	iowrite32(ioread32(rcar_gen4_sysc_base + SYSCIER(reg_idx)) | isr_mask,
		  rcar_gen4_sysc_base + SYSCIER(reg_idx));
	iowrite32(ioread32(rcar_gen4_sysc_base + SYSCIMR(reg_idx)) | isr_mask,
		  rcar_gen4_sysc_base + SYSCIMR(reg_idx));

	ret = clear_irq_flags(reg_idx, isr_mask);
	if (ret)
		goto out;

	/* Submit power shutoff or resume request until it was accepted */
	for (k = 0; k < PDRESR_RETRIES; k++) {
		ret = rcar_gen4_sysc_pwr_on_off(pdr, on);
		if (ret)
			goto out;

		status = ioread32(rcar_gen4_sysc_base + PDRESR(pdr));
		if (!(status & PDRESR_ERR))
			break;

		udelay(PDRESR_DELAY_US);
	}

	if (k == PDRESR_RETRIES) {
		ret = -EIO;
		goto out;
	}

	/* Wait until the power shutoff or resume request has completed * */
	ret = readl_poll_timeout_atomic(rcar_gen4_sysc_base + SYSCISCR(reg_idx),
					val, (val & isr_mask),
					SYSCISCR_DELAY_US, SYSCISCR_TIMEOUT);
	if (ret < 0) {
		ret = -EIO;
		goto out;
	}

	/* Clear interrupt flags */
	ret = clear_irq_flags(reg_idx, isr_mask);
	if (ret)
		goto out;

 out:
	spin_unlock_irqrestore(&rcar_gen4_sysc_lock, flags);

	pr_debug("sysc power %s domain %d: %08x -> %d\n", on ? "on" : "off",
		 pdr, ioread32(rcar_gen4_sysc_base + SYSCISCR(reg_idx)), ret);
	return ret;
}

static bool rcar_gen4_sysc_power_is_off(u8 pdr)
{
	unsigned int st;

	st = ioread32(rcar_gen4_sysc_base + PDRSR(pdr));

	if (st & PDRSR_OFF)
		return true;

	return false;
}

struct rcar_gen4_sysc_pd {
	struct generic_pm_domain genpd;
	u8 pdr;
	unsigned int flags;
	char name[];
};

static inline struct rcar_gen4_sysc_pd *to_rcar_gen4_pd(struct generic_pm_domain *d)
{
	return container_of(d, struct rcar_gen4_sysc_pd, genpd);
}

static int rcar_gen4_sysc_pd_power_off(struct generic_pm_domain *genpd)
{
	struct rcar_gen4_sysc_pd *pd = to_rcar_gen4_pd(genpd);

	pr_debug("%s: %s\n", __func__, genpd->name);
	return rcar_gen4_sysc_power(pd->pdr, false);
}

static int rcar_gen4_sysc_pd_power_on(struct generic_pm_domain *genpd)
{
	struct rcar_gen4_sysc_pd *pd = to_rcar_gen4_pd(genpd);

	pr_debug("%s: %s\n", __func__, genpd->name);
	return rcar_gen4_sysc_power(pd->pdr, true);
}

static int __init rcar_gen4_sysc_pd_setup(struct rcar_gen4_sysc_pd *pd)
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
		genpd->attach_dev = cpg_mssr_attach_dev;
		genpd->detach_dev = cpg_mssr_detach_dev;
	}

	genpd->power_off = rcar_gen4_sysc_pd_power_off;
	genpd->power_on = rcar_gen4_sysc_pd_power_on;

	if (pd->flags & (PD_CPU | PD_NO_CR)) {
		/* Skip CPUs (handled by SMP code) and areas without control */
		pr_debug("%s: Not touching %s\n", __func__, genpd->name);
		goto finalize;
	}

	if (!rcar_gen4_sysc_power_is_off(pd->pdr)) {
		pr_debug("%s: %s is already powered\n", __func__, genpd->name);
		goto finalize;
	}

	rcar_gen4_sysc_power(pd->pdr, true);

finalize:
	error = pm_genpd_init(genpd, &simple_qos_governor, false);
	if (error)
		pr_err("Failed to init PM domain %s: %d\n", name, error);

	return error;
}

static const struct of_device_id rcar_gen4_sysc_matches[] __initconst = {
#ifdef CONFIG_SYSC_R8A779A0
	{ .compatible = "renesas,r8a779a0-sysc", .data = &r8a779a0_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A779F0
	{ .compatible = "renesas,r8a779f0-sysc", .data = &r8a779f0_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A779G0
	{ .compatible = "renesas,r8a779g0-sysc", .data = &r8a779g0_sysc_info },
#endif
#ifdef CONFIG_SYSC_R8A779H0
	{ .compatible = "renesas,r8a779h0-sysc", .data = &r8a779h0_sysc_info },
#endif
	{ /* sentinel */ }
};

struct rcar_gen4_pm_domains {
	struct genpd_onecell_data onecell_data;
	struct generic_pm_domain *domains[RCAR_GEN4_PD_ALWAYS_ON + 1];
};

static struct genpd_onecell_data *rcar_gen4_sysc_onecell_data;

static int __init rcar_gen4_sysc_pd_init(void)
{
	const struct rcar_gen4_sysc_info *info;
	const struct of_device_id *match;
	struct rcar_gen4_pm_domains *domains;
	struct device_node *np;
	void __iomem *base;
	unsigned int i;
	int error;

	np = of_find_matching_node_and_match(NULL, rcar_gen4_sysc_matches, &match);
	if (!np)
		return -ENODEV;

	info = match->data;

	base = of_iomap(np, 0);
	if (!base) {
		pr_warn("%pOF: Cannot map regs\n", np);
		error = -ENOMEM;
		goto out_put;
	}

	rcar_gen4_sysc_base = base;

	domains = kzalloc(sizeof(*domains), GFP_KERNEL);
	if (!domains) {
		error = -ENOMEM;
		goto out_put;
	}

	domains->onecell_data.domains = domains->domains;
	domains->onecell_data.num_domains = ARRAY_SIZE(domains->domains);
	rcar_gen4_sysc_onecell_data = &domains->onecell_data;

	for (i = 0; i < info->num_areas; i++) {
		const struct rcar_gen4_sysc_area *area = &info->areas[i];
		struct rcar_gen4_sysc_pd *pd;
		size_t n;

		if (!area->name) {
			/* Skip NULLified area */
			continue;
		}

		n = strlen(area->name) + 1;
		pd = kzalloc(sizeof(*pd) + n, GFP_KERNEL);
		if (!pd) {
			error = -ENOMEM;
			goto out_put;
		}

		memcpy(pd->name, area->name, n);
		pd->genpd.name = pd->name;
		pd->pdr = area->pdr;
		pd->flags = area->flags;

		error = rcar_gen4_sysc_pd_setup(pd);
		if (error)
			goto out_put;

		domains->domains[area->pdr] = &pd->genpd;

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
early_initcall(rcar_gen4_sysc_pd_init);
