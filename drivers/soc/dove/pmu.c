// SPDX-License-Identifier: GPL-2.0
/*
 * Marvell Dove PMU support
 */
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/reset.h>
#include <linux/reset-controller.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/soc/dove/pmu.h>
#include <linux/spinlock.h>

#define NR_PMU_IRQS		7

#define PMC_SW_RST		0x30
#define PMC_IRQ_CAUSE		0x50
#define PMC_IRQ_MASK		0x54

#define PMU_PWR			0x10
#define PMU_ISO			0x58

struct pmu_data {
	spinlock_t lock;
	struct device_node *of_node;
	void __iomem *pmc_base;
	void __iomem *pmu_base;
	struct irq_chip_generic *irq_gc;
	struct irq_domain *irq_domain;
#ifdef CONFIG_RESET_CONTROLLER
	struct reset_controller_dev reset;
#endif
};

/*
 * The PMU contains a register to reset various subsystems within the
 * SoC.  Export this as a reset controller.
 */
#ifdef CONFIG_RESET_CONTROLLER
#define rcdev_to_pmu(rcdev) container_of(rcdev, struct pmu_data, reset)

static int pmu_reset_reset(struct reset_controller_dev *rc, unsigned long id)
{
	struct pmu_data *pmu = rcdev_to_pmu(rc);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&pmu->lock, flags);
	val = readl_relaxed(pmu->pmc_base + PMC_SW_RST);
	writel_relaxed(val & ~BIT(id), pmu->pmc_base + PMC_SW_RST);
	writel_relaxed(val | BIT(id), pmu->pmc_base + PMC_SW_RST);
	spin_unlock_irqrestore(&pmu->lock, flags);

	return 0;
}

static int pmu_reset_assert(struct reset_controller_dev *rc, unsigned long id)
{
	struct pmu_data *pmu = rcdev_to_pmu(rc);
	unsigned long flags;
	u32 val = ~BIT(id);

	spin_lock_irqsave(&pmu->lock, flags);
	val &= readl_relaxed(pmu->pmc_base + PMC_SW_RST);
	writel_relaxed(val, pmu->pmc_base + PMC_SW_RST);
	spin_unlock_irqrestore(&pmu->lock, flags);

	return 0;
}

static int pmu_reset_deassert(struct reset_controller_dev *rc, unsigned long id)
{
	struct pmu_data *pmu = rcdev_to_pmu(rc);
	unsigned long flags;
	u32 val = BIT(id);

	spin_lock_irqsave(&pmu->lock, flags);
	val |= readl_relaxed(pmu->pmc_base + PMC_SW_RST);
	writel_relaxed(val, pmu->pmc_base + PMC_SW_RST);
	spin_unlock_irqrestore(&pmu->lock, flags);

	return 0;
}

static const struct reset_control_ops pmu_reset_ops = {
	.reset = pmu_reset_reset,
	.assert = pmu_reset_assert,
	.deassert = pmu_reset_deassert,
};

static struct reset_controller_dev pmu_reset __initdata = {
	.ops = &pmu_reset_ops,
	.owner = THIS_MODULE,
	.nr_resets = 32,
};

static void __init pmu_reset_init(struct pmu_data *pmu)
{
	int ret;

	pmu->reset = pmu_reset;
	pmu->reset.of_node = pmu->of_node;

	ret = reset_controller_register(&pmu->reset);
	if (ret)
		pr_err("pmu: %s failed: %d\n", "reset_controller_register", ret);
}
#else
static void __init pmu_reset_init(struct pmu_data *pmu)
{
}
#endif

struct pmu_domain {
	struct pmu_data *pmu;
	u32 pwr_mask;
	u32 rst_mask;
	u32 iso_mask;
	struct generic_pm_domain base;
};

#define to_pmu_domain(dom) container_of(dom, struct pmu_domain, base)

/*
 * This deals with the "old" Marvell sequence of bringing a power domain
 * down/up, which is: apply power, release reset, disable isolators.
 *
 * Later devices apparantly use a different sequence: power up, disable
 * isolators, assert repair signal, enable SRMA clock, enable AXI clock,
 * enable module clock, deassert reset.
 *
 * Note: reading the assembly, it seems that the IO accessors have an
 * unfortunate side-effect - they cause memory already read into registers
 * for the if () to be re-read for the bit-set or bit-clear operation.
 * The code is written to avoid this.
 */
static int pmu_domain_power_off(struct generic_pm_domain *domain)
{
	struct pmu_domain *pmu_dom = to_pmu_domain(domain);
	struct pmu_data *pmu = pmu_dom->pmu;
	unsigned long flags;
	unsigned int val;
	void __iomem *pmu_base = pmu->pmu_base;
	void __iomem *pmc_base = pmu->pmc_base;

	spin_lock_irqsave(&pmu->lock, flags);

	/* Enable isolators */
	if (pmu_dom->iso_mask) {
		val = ~pmu_dom->iso_mask;
		val &= readl_relaxed(pmu_base + PMU_ISO);
		writel_relaxed(val, pmu_base + PMU_ISO);
	}

	/* Reset unit */
	if (pmu_dom->rst_mask) {
		val = ~pmu_dom->rst_mask;
		val &= readl_relaxed(pmc_base + PMC_SW_RST);
		writel_relaxed(val, pmc_base + PMC_SW_RST);
	}

	/* Power down */
	val = readl_relaxed(pmu_base + PMU_PWR) | pmu_dom->pwr_mask;
	writel_relaxed(val, pmu_base + PMU_PWR);

	spin_unlock_irqrestore(&pmu->lock, flags);

	return 0;
}

static int pmu_domain_power_on(struct generic_pm_domain *domain)
{
	struct pmu_domain *pmu_dom = to_pmu_domain(domain);
	struct pmu_data *pmu = pmu_dom->pmu;
	unsigned long flags;
	unsigned int val;
	void __iomem *pmu_base = pmu->pmu_base;
	void __iomem *pmc_base = pmu->pmc_base;

	spin_lock_irqsave(&pmu->lock, flags);

	/* Power on */
	val = ~pmu_dom->pwr_mask & readl_relaxed(pmu_base + PMU_PWR);
	writel_relaxed(val, pmu_base + PMU_PWR);

	/* Release reset */
	if (pmu_dom->rst_mask) {
		val = pmu_dom->rst_mask;
		val |= readl_relaxed(pmc_base + PMC_SW_RST);
		writel_relaxed(val, pmc_base + PMC_SW_RST);
	}

	/* Disable isolators */
	if (pmu_dom->iso_mask) {
		val = pmu_dom->iso_mask;
		val |= readl_relaxed(pmu_base + PMU_ISO);
		writel_relaxed(val, pmu_base + PMU_ISO);
	}

	spin_unlock_irqrestore(&pmu->lock, flags);

	return 0;
}

static void __pmu_domain_register(struct pmu_domain *domain,
	struct device_node *np)
{
	unsigned int val = readl_relaxed(domain->pmu->pmu_base + PMU_PWR);

	domain->base.power_off = pmu_domain_power_off;
	domain->base.power_on = pmu_domain_power_on;

	pm_genpd_init(&domain->base, NULL, !(val & domain->pwr_mask));

	if (np)
		of_genpd_add_provider_simple(np, &domain->base);
}

/* PMU IRQ controller */
static void pmu_irq_handler(struct irq_desc *desc)
{
	struct pmu_data *pmu = irq_desc_get_handler_data(desc);
	struct irq_chip_generic *gc = pmu->irq_gc;
	struct irq_domain *domain = pmu->irq_domain;
	void __iomem *base = gc->reg_base;
	u32 stat = readl_relaxed(base + PMC_IRQ_CAUSE) & gc->mask_cache;
	u32 done = ~0;

	if (stat == 0) {
		handle_bad_irq(desc);
		return;
	}

	while (stat) {
		u32 hwirq = fls(stat) - 1;

		stat &= ~(1 << hwirq);
		done &= ~(1 << hwirq);

		generic_handle_irq(irq_find_mapping(domain, hwirq));
	}

	/*
	 * The PMU mask register is not RW0C: it is RW.  This means that
	 * the bits take whatever value is written to them; if you write
	 * a '1', you will set the interrupt.
	 *
	 * Unfortunately this means there is NO race free way to clear
	 * these interrupts.
	 *
	 * So, let's structure the code so that the window is as small as
	 * possible.
	 */
	irq_gc_lock(gc);
	done &= readl_relaxed(base + PMC_IRQ_CAUSE);
	writel_relaxed(done, base + PMC_IRQ_CAUSE);
	irq_gc_unlock(gc);
}

static int __init dove_init_pmu_irq(struct pmu_data *pmu, int irq)
{
	const char *name = "pmu_irq";
	struct irq_chip_generic *gc;
	struct irq_domain *domain;
	int ret;

	/* mask and clear all interrupts */
	writel(0, pmu->pmc_base + PMC_IRQ_MASK);
	writel(0, pmu->pmc_base + PMC_IRQ_CAUSE);

	domain = irq_domain_add_linear(pmu->of_node, NR_PMU_IRQS,
				       &irq_generic_chip_ops, NULL);
	if (!domain) {
		pr_err("%s: unable to add irq domain\n", name);
		return -ENOMEM;
	}

	ret = irq_alloc_domain_generic_chips(domain, NR_PMU_IRQS, 1, name,
					     handle_level_irq,
					     IRQ_NOREQUEST | IRQ_NOPROBE, 0,
					     IRQ_GC_INIT_MASK_CACHE);
	if (ret) {
		pr_err("%s: unable to alloc irq domain gc: %d\n", name, ret);
		irq_domain_remove(domain);
		return ret;
	}

	gc = irq_get_domain_generic_chip(domain, 0);
	gc->reg_base = pmu->pmc_base;
	gc->chip_types[0].regs.mask = PMC_IRQ_MASK;
	gc->chip_types[0].chip.irq_mask = irq_gc_mask_clr_bit;
	gc->chip_types[0].chip.irq_unmask = irq_gc_mask_set_bit;

	pmu->irq_domain = domain;
	pmu->irq_gc = gc;

	irq_set_handler_data(irq, pmu);
	irq_set_chained_handler(irq, pmu_irq_handler);

	return 0;
}

int __init dove_init_pmu_legacy(const struct dove_pmu_initdata *initdata)
{
	const struct dove_pmu_domain_initdata *domain_initdata;
	struct pmu_data *pmu;
	int ret;

	pmu = kzalloc(sizeof(*pmu), GFP_KERNEL);
	if (!pmu)
		return -ENOMEM;

	spin_lock_init(&pmu->lock);
	pmu->pmc_base = initdata->pmc_base;
	pmu->pmu_base = initdata->pmu_base;

	pmu_reset_init(pmu);
	for (domain_initdata = initdata->domains; domain_initdata->name;
	     domain_initdata++) {
		struct pmu_domain *domain;

		domain = kzalloc(sizeof(*domain), GFP_KERNEL);
		if (domain) {
			domain->pmu = pmu;
			domain->pwr_mask = domain_initdata->pwr_mask;
			domain->rst_mask = domain_initdata->rst_mask;
			domain->iso_mask = domain_initdata->iso_mask;
			domain->base.name = domain_initdata->name;

			__pmu_domain_register(domain, NULL);
		}
	}

	ret = dove_init_pmu_irq(pmu, initdata->irq);
	if (ret)
		pr_err("dove_init_pmu_irq() failed: %d\n", ret);

	if (pmu->irq_domain)
		irq_domain_associate_many(pmu->irq_domain,
					  initdata->irq_domain_start,
					  0, NR_PMU_IRQS);

	return 0;
}

/*
 * pmu: power-manager@d0000 {
 *	compatible = "marvell,dove-pmu";
 *	reg = <0xd0000 0x8000> <0xd8000 0x8000>;
 *	interrupts = <33>;
 *	interrupt-controller;
 *	#reset-cells = 1;
 *	vpu_domain: vpu-domain {
 *		#power-domain-cells = <0>;
 *		marvell,pmu_pwr_mask = <0x00000008>;
 *		marvell,pmu_iso_mask = <0x00000001>;
 *		resets = <&pmu 16>;
 *	};
 *	gpu_domain: gpu-domain {
 *		#power-domain-cells = <0>;
 *		marvell,pmu_pwr_mask = <0x00000004>;
 *		marvell,pmu_iso_mask = <0x00000002>;
 *		resets = <&pmu 18>;
 *	};
 * };
 */
int __init dove_init_pmu(void)
{
	struct device_node *np_pmu, *domains_node, *np;
	struct pmu_data *pmu;
	int ret, parent_irq;

	/* Lookup the PMU node */
	np_pmu = of_find_compatible_node(NULL, NULL, "marvell,dove-pmu");
	if (!np_pmu)
		return 0;

	domains_node = of_get_child_by_name(np_pmu, "domains");
	if (!domains_node) {
		pr_err("%s: failed to find domains sub-node\n", np_pmu->name);
		return 0;
	}

	pmu = kzalloc(sizeof(*pmu), GFP_KERNEL);
	if (!pmu)
		return -ENOMEM;

	spin_lock_init(&pmu->lock);
	pmu->of_node = np_pmu;
	pmu->pmc_base = of_iomap(pmu->of_node, 0);
	pmu->pmu_base = of_iomap(pmu->of_node, 1);
	if (!pmu->pmc_base || !pmu->pmu_base) {
		pr_err("%s: failed to map PMU\n", np_pmu->name);
		iounmap(pmu->pmu_base);
		iounmap(pmu->pmc_base);
		kfree(pmu);
		return -ENOMEM;
	}

	pmu_reset_init(pmu);

	for_each_available_child_of_node(domains_node, np) {
		struct of_phandle_args args;
		struct pmu_domain *domain;

		domain = kzalloc(sizeof(*domain), GFP_KERNEL);
		if (!domain)
			break;

		domain->pmu = pmu;
		domain->base.name = kstrdup(np->name, GFP_KERNEL);
		if (!domain->base.name) {
			kfree(domain);
			break;
		}

		of_property_read_u32(np, "marvell,pmu_pwr_mask",
				     &domain->pwr_mask);
		of_property_read_u32(np, "marvell,pmu_iso_mask",
				     &domain->iso_mask);

		/*
		 * We parse the reset controller property directly here
		 * to ensure that we can operate when the reset controller
		 * support is not configured into the kernel.
		 */
		ret = of_parse_phandle_with_args(np, "resets", "#reset-cells",
						 0, &args);
		if (ret == 0) {
			if (args.np == pmu->of_node)
				domain->rst_mask = BIT(args.args[0]);
			of_node_put(args.np);
		}

		__pmu_domain_register(domain, np);
	}

	/* Loss of the interrupt controller is not a fatal error. */
	parent_irq = irq_of_parse_and_map(pmu->of_node, 0);
	if (!parent_irq) {
		pr_err("%s: no interrupt specified\n", np_pmu->name);
	} else {
		ret = dove_init_pmu_irq(pmu, parent_irq);
		if (ret)
			pr_err("dove_init_pmu_irq() failed: %d\n", ret);
	}

	return 0;
}
