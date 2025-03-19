// SPDX-License-Identifier: GPL-2.0-only
/*
 * Marvell Armada 370 and Armada XP SoC IRQ handling
 *
 * Copyright (C) 2012 Marvell
 *
 * Lior Amsalem <alior@marvell.com>
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 * Ben Dooks <ben.dooks@codethink.co.uk>
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/cpu.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/irqdomain.h>
#include <linux/slab.h>
#include <linux/syscore_ops.h>
#include <linux/msi.h>
#include <linux/types.h>
#include <asm/mach/arch.h>
#include <asm/exception.h>
#include <asm/smp_plat.h>
#include <asm/mach/irq.h>

/*
 * Overall diagram of the Armada XP interrupt controller:
 *
 *    To CPU 0                 To CPU 1
 *
 *       /\                       /\
 *       ||                       ||
 * +---------------+     +---------------+
 * |               |	 |               |
 * |    per-CPU    |	 |    per-CPU    |
 * |  mask/unmask  |	 |  mask/unmask  |
 * |     CPU0      |	 |     CPU1      |
 * |               |	 |               |
 * +---------------+	 +---------------+
 *        /\                       /\
 *        ||                       ||
 *        \\_______________________//
 *                     ||
 *            +-------------------+
 *            |                   |
 *            | Global interrupt  |
 *            |    mask/unmask    |
 *            |                   |
 *            +-------------------+
 *                     /\
 *                     ||
 *               interrupt from
 *                   device
 *
 * The "global interrupt mask/unmask" is modified using the
 * MPIC_INT_SET_ENABLE and MPIC_INT_CLEAR_ENABLE
 * registers, which are relative to "mpic->base".
 *
 * The "per-CPU mask/unmask" is modified using the MPIC_INT_SET_MASK
 * and MPIC_INT_CLEAR_MASK registers, which are relative to
 * "mpic->per_cpu". This base address points to a special address,
 * which automatically accesses the registers of the current CPU.
 *
 * The per-CPU mask/unmask can also be adjusted using the global
 * per-interrupt MPIC_INT_SOURCE_CTL register, which we use to
 * configure interrupt affinity.
 *
 * Due to this model, all interrupts need to be mask/unmasked at two
 * different levels: at the global level and at the per-CPU level.
 *
 * This driver takes the following approach to deal with this:
 *
 *  - For global interrupts:
 *
 *    At ->map() time, a global interrupt is unmasked at the per-CPU
 *    mask/unmask level. It is therefore unmasked at this level for
 *    the current CPU, running the ->map() code. This allows to have
 *    the interrupt unmasked at this level in non-SMP
 *    configurations. In SMP configurations, the ->set_affinity()
 *    callback is called, which using the MPIC_INT_SOURCE_CTL()
 *    readjusts the per-CPU mask/unmask for the interrupt.
 *
 *    The ->mask() and ->unmask() operations only mask/unmask the
 *    interrupt at the "global" level.
 *
 *    So, a global interrupt is enabled at the per-CPU level as soon
 *    as it is mapped. At run time, the masking/unmasking takes place
 *    at the global level.
 *
 *  - For per-CPU interrupts
 *
 *    At ->map() time, a per-CPU interrupt is unmasked at the global
 *    mask/unmask level.
 *
 *    The ->mask() and ->unmask() operations mask/unmask the interrupt
 *    at the per-CPU level.
 *
 *    So, a per-CPU interrupt is enabled at the global level as soon
 *    as it is mapped. At run time, the masking/unmasking takes place
 *    at the per-CPU level.
 */

/* Registers relative to mpic->base */
#define MPIC_INT_CONTROL			0x00
#define MPIC_INT_CONTROL_NUMINT_MASK		GENMASK(12, 2)
#define MPIC_SW_TRIG_INT			0x04
#define MPIC_INT_SET_ENABLE			0x30
#define MPIC_INT_CLEAR_ENABLE			0x34
#define MPIC_INT_SOURCE_CTL(hwirq)		(0x100 + (hwirq) * 4)
#define MPIC_INT_SOURCE_CPU_MASK		GENMASK(3, 0)
#define MPIC_INT_IRQ_FIQ_MASK(cpuid)		((BIT(0) | BIT(8)) << (cpuid))

/* Registers relative to mpic->per_cpu */
#define MPIC_IN_DRBEL_CAUSE			0x08
#define MPIC_IN_DRBEL_MASK			0x0c
#define MPIC_PPI_CAUSE				0x10
#define MPIC_CPU_INTACK				0x44
#define MPIC_CPU_INTACK_IID_MASK		GENMASK(9, 0)
#define MPIC_INT_SET_MASK			0x48
#define MPIC_INT_CLEAR_MASK			0x4C
#define MPIC_INT_FABRIC_MASK			0x54
#define MPIC_INT_CAUSE_PERF(cpu)		BIT(cpu)

#define MPIC_PER_CPU_IRQS_NR			29

/* IPI and MSI interrupt definitions for IPI platforms */
#define IPI_DOORBELL_NR				8
#define IPI_DOORBELL_MASK			GENMASK(7, 0)
#define PCI_MSI_DOORBELL_START			16
#define PCI_MSI_DOORBELL_NR			16
#define PCI_MSI_DOORBELL_MASK			GENMASK(31, 16)

/* MSI interrupt definitions for non-IPI platforms */
#define PCI_MSI_FULL_DOORBELL_START		0
#define PCI_MSI_FULL_DOORBELL_NR		32
#define PCI_MSI_FULL_DOORBELL_MASK		GENMASK(31, 0)
#define PCI_MSI_FULL_DOORBELL_SRC0_MASK		GENMASK(15, 0)
#define PCI_MSI_FULL_DOORBELL_SRC1_MASK		GENMASK(31, 16)

/**
 * struct mpic - MPIC private data structure
 * @base:		MPIC registers base address
 * @per_cpu:		per-CPU registers base address
 * @parent_irq:		parent IRQ if MPIC is not top-level interrupt controller
 * @domain:		MPIC main interrupt domain
 * @ipi_domain:		IPI domain
 * @msi_domain:		MSI domain
 * @msi_inner_domain:	MSI inner domain
 * @msi_used:		bitmap of used MSI numbers
 * @msi_lock:		mutex serializing access to @msi_used
 * @msi_doorbell_addr:	physical address of MSI doorbell register
 * @msi_doorbell_mask:	mask of available doorbell bits for MSIs (either PCI_MSI_DOORBELL_MASK or
 *			PCI_MSI_FULL_DOORBELL_MASK)
 * @msi_doorbell_start:	first set bit in @msi_doorbell_mask
 * @msi_doorbell_size:	number of set bits in @msi_doorbell_mask
 * @doorbell_mask:	doorbell mask of MSIs and IPIs, stored on suspend, restored on resume
 */
struct mpic {
	void __iomem *base;
	void __iomem *per_cpu;
	int parent_irq;
	struct irq_domain *domain;
#ifdef CONFIG_SMP
	struct irq_domain *ipi_domain;
#endif
#ifdef CONFIG_PCI_MSI
	struct irq_domain *msi_domain;
	struct irq_domain *msi_inner_domain;
	DECLARE_BITMAP(msi_used, PCI_MSI_FULL_DOORBELL_NR);
	struct mutex msi_lock;
	phys_addr_t msi_doorbell_addr;
	u32 msi_doorbell_mask;
	unsigned int msi_doorbell_start, msi_doorbell_size;
#endif
	u32 doorbell_mask;
};

static struct mpic *mpic_data __ro_after_init;

static inline bool mpic_is_ipi_available(struct mpic *mpic)
{
	/*
	 * We distinguish IPI availability in the IC by the IC not having a
	 * parent irq defined. If a parent irq is defined, there is a parent
	 * interrupt controller (e.g. GIC) that takes care of inter-processor
	 * interrupts.
	 */
	return mpic->parent_irq <= 0;
}

static inline bool mpic_is_percpu_irq(irq_hw_number_t hwirq)
{
	return hwirq < MPIC_PER_CPU_IRQS_NR;
}

/*
 * In SMP mode:
 * For shared global interrupts, mask/unmask global enable bit
 * For CPU interrupts, mask/unmask the calling CPU's bit
 */
static void mpic_irq_mask(struct irq_data *d)
{
	struct mpic *mpic = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	if (!mpic_is_percpu_irq(hwirq))
		writel(hwirq, mpic->base + MPIC_INT_CLEAR_ENABLE);
	else
		writel(hwirq, mpic->per_cpu + MPIC_INT_SET_MASK);
}

static void mpic_irq_unmask(struct irq_data *d)
{
	struct mpic *mpic = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	if (!mpic_is_percpu_irq(hwirq))
		writel(hwirq, mpic->base + MPIC_INT_SET_ENABLE);
	else
		writel(hwirq, mpic->per_cpu + MPIC_INT_CLEAR_MASK);
}

#ifdef CONFIG_PCI_MSI

static struct irq_chip mpic_msi_irq_chip = {
	.name		= "MPIC MSI",
	.irq_mask	= pci_msi_mask_irq,
	.irq_unmask	= pci_msi_unmask_irq,
};

static struct msi_domain_info mpic_msi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		   MSI_FLAG_MULTI_PCI_MSI | MSI_FLAG_PCI_MSIX),
	.chip	= &mpic_msi_irq_chip,
};

static void mpic_compose_msi_msg(struct irq_data *d, struct msi_msg *msg)
{
	unsigned int cpu = cpumask_first(irq_data_get_effective_affinity_mask(d));
	struct mpic *mpic = irq_data_get_irq_chip_data(d);

	msg->address_lo = lower_32_bits(mpic->msi_doorbell_addr);
	msg->address_hi = upper_32_bits(mpic->msi_doorbell_addr);
	msg->data = BIT(cpu + 8) | (d->hwirq + mpic->msi_doorbell_start);
}

static int mpic_msi_set_affinity(struct irq_data *d, const struct cpumask *mask, bool force)
{
	unsigned int cpu;

	if (!force)
		cpu = cpumask_any_and(mask, cpu_online_mask);
	else
		cpu = cpumask_first(mask);

	if (cpu >= nr_cpu_ids)
		return -EINVAL;

	irq_data_update_effective_affinity(d, cpumask_of(cpu));

	return IRQ_SET_MASK_OK;
}

static struct irq_chip mpic_msi_bottom_irq_chip = {
	.name			= "MPIC MSI",
	.irq_compose_msi_msg	= mpic_compose_msi_msg,
	.irq_set_affinity	= mpic_msi_set_affinity,
};

static int mpic_msi_alloc(struct irq_domain *domain, unsigned int virq, unsigned int nr_irqs,
			  void *args)
{
	struct mpic *mpic = domain->host_data;
	int hwirq;

	mutex_lock(&mpic->msi_lock);
	hwirq = bitmap_find_free_region(mpic->msi_used, mpic->msi_doorbell_size,
					order_base_2(nr_irqs));
	mutex_unlock(&mpic->msi_lock);

	if (hwirq < 0)
		return -ENOSPC;

	for (unsigned int i = 0; i < nr_irqs; i++) {
		irq_domain_set_info(domain, virq + i, hwirq + i,
				    &mpic_msi_bottom_irq_chip,
				    domain->host_data, handle_simple_irq,
				    NULL, NULL);
	}

	return 0;
}

static void mpic_msi_free(struct irq_domain *domain, unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct mpic *mpic = domain->host_data;

	mutex_lock(&mpic->msi_lock);
	bitmap_release_region(mpic->msi_used, d->hwirq, order_base_2(nr_irqs));
	mutex_unlock(&mpic->msi_lock);
}

static const struct irq_domain_ops mpic_msi_domain_ops = {
	.alloc	= mpic_msi_alloc,
	.free	= mpic_msi_free,
};

static void mpic_msi_reenable_percpu(struct mpic *mpic)
{
	u32 reg;

	/* Enable MSI doorbell mask and combined cpu local interrupt */
	reg = readl(mpic->per_cpu + MPIC_IN_DRBEL_MASK);
	reg |= mpic->msi_doorbell_mask;
	writel(reg, mpic->per_cpu + MPIC_IN_DRBEL_MASK);

	/* Unmask local doorbell interrupt */
	writel(1, mpic->per_cpu + MPIC_INT_CLEAR_MASK);
}

static int __init mpic_msi_init(struct mpic *mpic, struct device_node *node,
				phys_addr_t main_int_phys_base)
{
	mpic->msi_doorbell_addr = main_int_phys_base + MPIC_SW_TRIG_INT;

	mutex_init(&mpic->msi_lock);

	if (mpic_is_ipi_available(mpic)) {
		mpic->msi_doorbell_start = PCI_MSI_DOORBELL_START;
		mpic->msi_doorbell_size = PCI_MSI_DOORBELL_NR;
		mpic->msi_doorbell_mask = PCI_MSI_DOORBELL_MASK;
	} else {
		mpic->msi_doorbell_start = PCI_MSI_FULL_DOORBELL_START;
		mpic->msi_doorbell_size = PCI_MSI_FULL_DOORBELL_NR;
		mpic->msi_doorbell_mask = PCI_MSI_FULL_DOORBELL_MASK;
	}

	mpic->msi_inner_domain = irq_domain_add_linear(NULL, mpic->msi_doorbell_size,
						       &mpic_msi_domain_ops, mpic);
	if (!mpic->msi_inner_domain)
		return -ENOMEM;

	mpic->msi_domain = pci_msi_create_irq_domain(of_node_to_fwnode(node), &mpic_msi_domain_info,
						     mpic->msi_inner_domain);
	if (!mpic->msi_domain) {
		irq_domain_remove(mpic->msi_inner_domain);
		return -ENOMEM;
	}

	mpic_msi_reenable_percpu(mpic);

	/* Unmask low 16 MSI irqs on non-IPI platforms */
	if (!mpic_is_ipi_available(mpic))
		writel(0, mpic->per_cpu + MPIC_INT_CLEAR_MASK);

	return 0;
}
#else
static __maybe_unused void mpic_msi_reenable_percpu(struct mpic *mpic) {}

static inline int mpic_msi_init(struct mpic *mpic, struct device_node *node,
				phys_addr_t main_int_phys_base)
{
	return 0;
}
#endif

static void mpic_perf_init(struct mpic *mpic)
{
	u32 cpuid;

	/*
	 * This Performance Counter Overflow interrupt is specific for
	 * Armada 370 and XP. It is not available on Armada 375, 38x and 39x.
	 */
	if (!of_machine_is_compatible("marvell,armada-370-xp"))
		return;

	cpuid = cpu_logical_map(smp_processor_id());

	/* Enable Performance Counter Overflow interrupts */
	writel(MPIC_INT_CAUSE_PERF(cpuid), mpic->per_cpu + MPIC_INT_FABRIC_MASK);
}

#ifdef CONFIG_SMP
static void mpic_ipi_mask(struct irq_data *d)
{
	struct mpic *mpic = irq_data_get_irq_chip_data(d);
	u32 reg;

	reg = readl(mpic->per_cpu + MPIC_IN_DRBEL_MASK);
	reg &= ~BIT(d->hwirq);
	writel(reg, mpic->per_cpu + MPIC_IN_DRBEL_MASK);
}

static void mpic_ipi_unmask(struct irq_data *d)
{
	struct mpic *mpic = irq_data_get_irq_chip_data(d);
	u32 reg;

	reg = readl(mpic->per_cpu + MPIC_IN_DRBEL_MASK);
	reg |= BIT(d->hwirq);
	writel(reg, mpic->per_cpu + MPIC_IN_DRBEL_MASK);
}

static void mpic_ipi_send_mask(struct irq_data *d, const struct cpumask *mask)
{
	struct mpic *mpic = irq_data_get_irq_chip_data(d);
	unsigned int cpu;
	u32 map = 0;

	/* Convert our logical CPU mask into a physical one. */
	for_each_cpu(cpu, mask)
		map |= BIT(cpu_logical_map(cpu));

	/*
	 * Ensure that stores to Normal memory are visible to the
	 * other CPUs before issuing the IPI.
	 */
	dsb();

	/* submit softirq */
	writel((map << 8) | d->hwirq, mpic->base + MPIC_SW_TRIG_INT);
}

static void mpic_ipi_ack(struct irq_data *d)
{
	struct mpic *mpic = irq_data_get_irq_chip_data(d);

	writel(~BIT(d->hwirq), mpic->per_cpu + MPIC_IN_DRBEL_CAUSE);
}

static struct irq_chip mpic_ipi_irqchip = {
	.name		= "IPI",
	.irq_ack	= mpic_ipi_ack,
	.irq_mask	= mpic_ipi_mask,
	.irq_unmask	= mpic_ipi_unmask,
	.ipi_send_mask	= mpic_ipi_send_mask,
};

static int mpic_ipi_alloc(struct irq_domain *d, unsigned int virq,
			  unsigned int nr_irqs, void *args)
{
	for (unsigned int i = 0; i < nr_irqs; i++) {
		irq_set_percpu_devid(virq + i);
		irq_domain_set_info(d, virq + i, i, &mpic_ipi_irqchip, d->host_data,
				    handle_percpu_devid_irq, NULL, NULL);
	}

	return 0;
}

static void mpic_ipi_free(struct irq_domain *d, unsigned int virq,
			  unsigned int nr_irqs)
{
	/* Not freeing IPIs */
}

static const struct irq_domain_ops mpic_ipi_domain_ops = {
	.alloc	= mpic_ipi_alloc,
	.free	= mpic_ipi_free,
};

static void mpic_ipi_resume(struct mpic *mpic)
{
	for (irq_hw_number_t i = 0; i < IPI_DOORBELL_NR; i++) {
		unsigned int virq = irq_find_mapping(mpic->ipi_domain, i);
		struct irq_data *d;

		if (!virq || !irq_percpu_is_enabled(virq))
			continue;

		d = irq_domain_get_irq_data(mpic->ipi_domain, virq);
		mpic_ipi_unmask(d);
	}
}

static int __init mpic_ipi_init(struct mpic *mpic, struct device_node *node)
{
	int base_ipi;

	mpic->ipi_domain = irq_domain_create_linear(of_node_to_fwnode(node), IPI_DOORBELL_NR,
						    &mpic_ipi_domain_ops, mpic);
	if (WARN_ON(!mpic->ipi_domain))
		return -ENOMEM;

	irq_domain_update_bus_token(mpic->ipi_domain, DOMAIN_BUS_IPI);
	base_ipi = irq_domain_alloc_irqs(mpic->ipi_domain, IPI_DOORBELL_NR, NUMA_NO_NODE, NULL);
	if (WARN_ON(!base_ipi))
		return -ENOMEM;

	set_smp_ipi_range(base_ipi, IPI_DOORBELL_NR);

	return 0;
}

static int mpic_set_affinity(struct irq_data *d, const struct cpumask *mask_val, bool force)
{
	struct mpic *mpic = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	unsigned int cpu;

	/* Select a single core from the affinity mask which is online */
	cpu = cpumask_any_and(mask_val, cpu_online_mask);

	atomic_io_modify(mpic->base + MPIC_INT_SOURCE_CTL(hwirq),
			 MPIC_INT_SOURCE_CPU_MASK, BIT(cpu_logical_map(cpu)));

	irq_data_update_effective_affinity(d, cpumask_of(cpu));

	return IRQ_SET_MASK_OK;
}

static void mpic_smp_cpu_init(struct mpic *mpic)
{
	for (irq_hw_number_t i = 0; i < mpic->domain->hwirq_max; i++)
		writel(i, mpic->per_cpu + MPIC_INT_SET_MASK);

	if (!mpic_is_ipi_available(mpic))
		return;

	/* Disable all IPIs */
	writel(0, mpic->per_cpu + MPIC_IN_DRBEL_MASK);

	/* Clear pending IPIs */
	writel(0, mpic->per_cpu + MPIC_IN_DRBEL_CAUSE);

	/* Unmask IPI interrupt */
	writel(0, mpic->per_cpu + MPIC_INT_CLEAR_MASK);
}

static void mpic_reenable_percpu(struct mpic *mpic)
{
	/* Re-enable per-CPU interrupts that were enabled before suspend */
	for (irq_hw_number_t i = 0; i < MPIC_PER_CPU_IRQS_NR; i++) {
		unsigned int virq = irq_linear_revmap(mpic->domain, i);
		struct irq_data *d;

		if (!virq || !irq_percpu_is_enabled(virq))
			continue;

		d = irq_get_irq_data(virq);
		mpic_irq_unmask(d);
	}

	if (mpic_is_ipi_available(mpic))
		mpic_ipi_resume(mpic);

	mpic_msi_reenable_percpu(mpic);
}

static int mpic_starting_cpu(unsigned int cpu)
{
	struct mpic *mpic = irq_get_default_domain()->host_data;

	mpic_perf_init(mpic);
	mpic_smp_cpu_init(mpic);
	mpic_reenable_percpu(mpic);

	return 0;
}

static int mpic_cascaded_starting_cpu(unsigned int cpu)
{
	struct mpic *mpic = mpic_data;

	mpic_perf_init(mpic);
	mpic_reenable_percpu(mpic);
	enable_percpu_irq(mpic->parent_irq, IRQ_TYPE_NONE);

	return 0;
}
#else
static void mpic_smp_cpu_init(struct mpic *mpic) {}
static void mpic_ipi_resume(struct mpic *mpic) {}
#endif

static struct irq_chip mpic_irq_chip = {
	.name		= "MPIC",
	.irq_mask	= mpic_irq_mask,
	.irq_mask_ack	= mpic_irq_mask,
	.irq_unmask	= mpic_irq_unmask,
#ifdef CONFIG_SMP
	.irq_set_affinity = mpic_set_affinity,
#endif
	.flags		= IRQCHIP_SKIP_SET_WAKE | IRQCHIP_MASK_ON_SUSPEND,
};

static int mpic_irq_map(struct irq_domain *domain, unsigned int virq, irq_hw_number_t hwirq)
{
	struct mpic *mpic = domain->host_data;

	/* IRQs 0 and 1 cannot be mapped, they are handled internally */
	if (hwirq <= 1)
		return -EINVAL;

	irq_set_chip_data(virq, mpic);

	mpic_irq_mask(irq_get_irq_data(virq));
	if (!mpic_is_percpu_irq(hwirq))
		writel(hwirq, mpic->per_cpu + MPIC_INT_CLEAR_MASK);
	else
		writel(hwirq, mpic->base + MPIC_INT_SET_ENABLE);
	irq_set_status_flags(virq, IRQ_LEVEL);

	if (mpic_is_percpu_irq(hwirq)) {
		irq_set_percpu_devid(virq);
		irq_set_chip_and_handler(virq, &mpic_irq_chip, handle_percpu_devid_irq);
	} else {
		irq_set_chip_and_handler(virq, &mpic_irq_chip, handle_level_irq);
		irqd_set_single_target(irq_desc_get_irq_data(irq_to_desc(virq)));
	}
	irq_set_probe(virq);
	return 0;
}

static const struct irq_domain_ops mpic_irq_ops = {
	.map	= mpic_irq_map,
	.xlate	= irq_domain_xlate_onecell,
};

#ifdef CONFIG_PCI_MSI
static void mpic_handle_msi_irq(struct mpic *mpic)
{
	unsigned long cause;
	unsigned int i;

	cause = readl_relaxed(mpic->per_cpu + MPIC_IN_DRBEL_CAUSE);
	cause &= mpic->msi_doorbell_mask;
	writel(~cause, mpic->per_cpu + MPIC_IN_DRBEL_CAUSE);

	for_each_set_bit(i, &cause, BITS_PER_LONG)
		generic_handle_domain_irq(mpic->msi_inner_domain, i - mpic->msi_doorbell_start);
}
#else
static void mpic_handle_msi_irq(struct mpic *mpic) {}
#endif

#ifdef CONFIG_SMP
static void mpic_handle_ipi_irq(struct mpic *mpic)
{
	unsigned long cause;
	irq_hw_number_t i;

	cause = readl_relaxed(mpic->per_cpu + MPIC_IN_DRBEL_CAUSE);
	cause &= IPI_DOORBELL_MASK;

	for_each_set_bit(i, &cause, IPI_DOORBELL_NR)
		generic_handle_domain_irq(mpic->ipi_domain, i);
}
#else
static inline void mpic_handle_ipi_irq(struct mpic *mpic) {}
#endif

static void mpic_handle_cascade_irq(struct irq_desc *desc)
{
	struct mpic *mpic = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned long cause;
	u32 irqsrc, cpuid;
	irq_hw_number_t i;

	chained_irq_enter(chip, desc);

	cause = readl_relaxed(mpic->per_cpu + MPIC_PPI_CAUSE);
	cpuid = cpu_logical_map(smp_processor_id());

	for_each_set_bit(i, &cause, MPIC_PER_CPU_IRQS_NR) {
		irqsrc = readl_relaxed(mpic->base + MPIC_INT_SOURCE_CTL(i));

		/* Check if the interrupt is not masked on current CPU.
		 * Test IRQ (0-1) and FIQ (8-9) mask bits.
		 */
		if (!(irqsrc & MPIC_INT_IRQ_FIQ_MASK(cpuid)))
			continue;

		if (i == 0 || i == 1) {
			mpic_handle_msi_irq(mpic);
			continue;
		}

		generic_handle_domain_irq(mpic->domain, i);
	}

	chained_irq_exit(chip, desc);
}

static void __exception_irq_entry mpic_handle_irq(struct pt_regs *regs)
{
	struct mpic *mpic = irq_get_default_domain()->host_data;
	irq_hw_number_t i;
	u32 irqstat;

	do {
		irqstat = readl_relaxed(mpic->per_cpu + MPIC_CPU_INTACK);
		i = FIELD_GET(MPIC_CPU_INTACK_IID_MASK, irqstat);

		if (i > 1022)
			break;

		if (i > 1)
			generic_handle_domain_irq(mpic->domain, i);

		/* MSI handling */
		if (i == 1)
			mpic_handle_msi_irq(mpic);

		/* IPI Handling */
		if (i == 0)
			mpic_handle_ipi_irq(mpic);
	} while (1);
}

static int mpic_suspend(void)
{
	struct mpic *mpic = mpic_data;

	mpic->doorbell_mask = readl(mpic->per_cpu + MPIC_IN_DRBEL_MASK);

	return 0;
}

static void mpic_resume(void)
{
	struct mpic *mpic = mpic_data;
	bool src0, src1;

	/* Re-enable interrupts */
	for (irq_hw_number_t i = 0; i < mpic->domain->hwirq_max; i++) {
		unsigned int virq = irq_linear_revmap(mpic->domain, i);
		struct irq_data *d;

		if (!virq)
			continue;

		d = irq_get_irq_data(virq);

		if (!mpic_is_percpu_irq(i)) {
			/* Non per-CPU interrupts */
			writel(i, mpic->per_cpu + MPIC_INT_CLEAR_MASK);
			if (!irqd_irq_disabled(d))
				mpic_irq_unmask(d);
		} else {
			/* Per-CPU interrupts */
			writel(i, mpic->base + MPIC_INT_SET_ENABLE);

			/*
			 * Re-enable on the current CPU, mpic_reenable_percpu()
			 * will take care of secondary CPUs when they come up.
			 */
			if (irq_percpu_is_enabled(virq))
				mpic_irq_unmask(d);
		}
	}

	/* Reconfigure doorbells for IPIs and MSIs */
	writel(mpic->doorbell_mask, mpic->per_cpu + MPIC_IN_DRBEL_MASK);

	if (mpic_is_ipi_available(mpic)) {
		src0 = mpic->doorbell_mask & IPI_DOORBELL_MASK;
		src1 = mpic->doorbell_mask & PCI_MSI_DOORBELL_MASK;
	} else {
		src0 = mpic->doorbell_mask & PCI_MSI_FULL_DOORBELL_SRC0_MASK;
		src1 = mpic->doorbell_mask & PCI_MSI_FULL_DOORBELL_SRC1_MASK;
	}

	if (src0)
		writel(0, mpic->per_cpu + MPIC_INT_CLEAR_MASK);
	if (src1)
		writel(1, mpic->per_cpu + MPIC_INT_CLEAR_MASK);

	if (mpic_is_ipi_available(mpic))
		mpic_ipi_resume(mpic);
}

static struct syscore_ops mpic_syscore_ops = {
	.suspend	= mpic_suspend,
	.resume		= mpic_resume,
};

static int __init mpic_map_region(struct device_node *np, int index,
				  void __iomem **base, phys_addr_t *phys_base)
{
	struct resource res;
	int err;

	err = of_address_to_resource(np, index, &res);
	if (WARN_ON(err))
		goto fail;

	if (WARN_ON(!request_mem_region(res.start, resource_size(&res), np->full_name))) {
		err = -EBUSY;
		goto fail;
	}

	*base = ioremap(res.start, resource_size(&res));
	if (WARN_ON(!*base)) {
		err = -ENOMEM;
		goto fail;
	}

	if (phys_base)
		*phys_base = res.start;

	return 0;

fail:
	pr_err("%pOF: Unable to map resource %d: %pE\n", np, index, ERR_PTR(err));
	return err;
}

static int __init mpic_of_init(struct device_node *node, struct device_node *parent)
{
	phys_addr_t phys_base;
	unsigned int nr_irqs;
	struct mpic *mpic;
	int err;

	mpic = kzalloc(sizeof(*mpic), GFP_KERNEL);
	if (WARN_ON(!mpic))
		return -ENOMEM;

	mpic_data = mpic;

	err = mpic_map_region(node, 0, &mpic->base, &phys_base);
	if (err)
		return err;

	err = mpic_map_region(node, 1, &mpic->per_cpu, NULL);
	if (err)
		return err;

	nr_irqs = FIELD_GET(MPIC_INT_CONTROL_NUMINT_MASK, readl(mpic->base + MPIC_INT_CONTROL));

	for (irq_hw_number_t i = 0; i < nr_irqs; i++)
		writel(i, mpic->base + MPIC_INT_CLEAR_ENABLE);

	/*
	 * Initialize mpic->parent_irq before calling any other functions, since
	 * it is used to distinguish between IPI and non-IPI platforms.
	 */
	mpic->parent_irq = irq_of_parse_and_map(node, 0);

	/*
	 * On non-IPI platforms the driver currently supports only the per-CPU
	 * interrupts (the first 29 interrupts). See mpic_handle_cascade_irq().
	 */
	if (!mpic_is_ipi_available(mpic))
		nr_irqs = MPIC_PER_CPU_IRQS_NR;

	mpic->domain = irq_domain_add_linear(node, nr_irqs, &mpic_irq_ops, mpic);
	if (!mpic->domain) {
		pr_err("%pOF: Unable to add IRQ domain\n", node);
		return -ENOMEM;
	}

	irq_domain_update_bus_token(mpic->domain, DOMAIN_BUS_WIRED);

	/* Setup for the boot CPU */
	mpic_perf_init(mpic);
	mpic_smp_cpu_init(mpic);

	err = mpic_msi_init(mpic, node, phys_base);
	if (err) {
		pr_err("%pOF: Unable to initialize MSI domain\n", node);
		return err;
	}

	if (mpic_is_ipi_available(mpic)) {
		irq_set_default_domain(mpic->domain);
		set_handle_irq(mpic_handle_irq);
#ifdef CONFIG_SMP
		err = mpic_ipi_init(mpic, node);
		if (err) {
			pr_err("%pOF: Unable to initialize IPI domain\n", node);
			return err;
		}

		cpuhp_setup_state_nocalls(CPUHP_AP_IRQ_ARMADA_XP_STARTING,
					  "irqchip/armada/ipi:starting",
					  mpic_starting_cpu, NULL);
#endif
	} else {
#ifdef CONFIG_SMP
		cpuhp_setup_state_nocalls(CPUHP_AP_IRQ_ARMADA_XP_STARTING,
					  "irqchip/armada/cascade:starting",
					  mpic_cascaded_starting_cpu, NULL);
#endif
		irq_set_chained_handler_and_data(mpic->parent_irq,
						 mpic_handle_cascade_irq, mpic);
	}

	register_syscore_ops(&mpic_syscore_ops);

	return 0;
}

IRQCHIP_DECLARE(marvell_mpic, "marvell,mpic", mpic_of_init);
