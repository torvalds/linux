// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright The Asahi Linux Contributors
 *
 * Based on irq-lpc32xx:
 *   Copyright 2015-2016 Vladimir Zapolskiy <vz@mleia.com>
 * Based on irq-bcm2836:
 *   Copyright 2015 Broadcom
 */

/*
 * AIC is a fairly simple interrupt controller with the following features:
 *
 * - 896 level-triggered hardware IRQs
 *   - Single mask bit per IRQ
 *   - Per-IRQ affinity setting
 *   - Automatic masking on event delivery (auto-ack)
 *   - Software triggering (ORed with hw line)
 * - 2 per-CPU IPIs (meant as "self" and "other", but they are
 *   interchangeable if not symmetric)
 * - Automatic prioritization (single event/ack register per CPU, lower IRQs =
 *   higher priority)
 * - Automatic masking on ack
 * - Default "this CPU" register view and explicit per-CPU views
 *
 * In addition, this driver also handles FIQs, as these are routed to the same
 * IRQ vector. These are used for Fast IPIs (TODO), the ARMv8 timer IRQs, and
 * performance counters (TODO).
 *
 * Implementation notes:
 *
 * - This driver creates two IRQ domains, one for HW IRQs and internal FIQs,
 *   and one for IPIs.
 * - Since Linux needs more than 2 IPIs, we implement a software IRQ controller
 *   and funnel all IPIs into one per-CPU IPI (the second "self" IPI is unused).
 * - FIQ hwirq numbers are assigned after true hwirqs, and are per-cpu.
 * - DT bindings use 3-cell form (like GIC):
 *   - <0 nr flags> - hwirq #nr
 *   - <1 nr flags> - FIQ #nr
 *     - nr=0  Physical HV timer
 *     - nr=1  Virtual HV timer
 *     - nr=2  Physical guest timer
 *     - nr=3  Virtual guest timer
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/cpuhotplug.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/irqchip/arm-vgic-info.h>
#include <linux/irqdomain.h>
#include <linux/limits.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <asm/exception.h>
#include <asm/sysreg.h>
#include <asm/virt.h>

#include <dt-bindings/interrupt-controller/apple-aic.h>

/*
 * AIC registers (MMIO)
 */

#define AIC_INFO		0x0004
#define AIC_INFO_NR_HW		GENMASK(15, 0)

#define AIC_CONFIG		0x0010

#define AIC_WHOAMI		0x2000
#define AIC_EVENT		0x2004
#define AIC_EVENT_TYPE		GENMASK(31, 16)
#define AIC_EVENT_NUM		GENMASK(15, 0)

#define AIC_EVENT_TYPE_HW	1
#define AIC_EVENT_TYPE_IPI	4
#define AIC_EVENT_IPI_OTHER	1
#define AIC_EVENT_IPI_SELF	2

#define AIC_IPI_SEND		0x2008
#define AIC_IPI_ACK		0x200c
#define AIC_IPI_MASK_SET	0x2024
#define AIC_IPI_MASK_CLR	0x2028

#define AIC_IPI_SEND_CPU(cpu)	BIT(cpu)

#define AIC_IPI_OTHER		BIT(0)
#define AIC_IPI_SELF		BIT(31)

#define AIC_TARGET_CPU		0x3000
#define AIC_SW_SET		0x4000
#define AIC_SW_CLR		0x4080
#define AIC_MASK_SET		0x4100
#define AIC_MASK_CLR		0x4180

#define AIC_CPU_IPI_SET(cpu)	(0x5008 + ((cpu) << 7))
#define AIC_CPU_IPI_CLR(cpu)	(0x500c + ((cpu) << 7))
#define AIC_CPU_IPI_MASK_SET(cpu) (0x5024 + ((cpu) << 7))
#define AIC_CPU_IPI_MASK_CLR(cpu) (0x5028 + ((cpu) << 7))

#define MASK_REG(x)		(4 * ((x) >> 5))
#define MASK_BIT(x)		BIT((x) & GENMASK(4, 0))

/*
 * IMP-DEF sysregs that control FIQ sources
 * Note: sysreg-based IPIs are not supported yet.
 */

/* Core PMC control register */
#define SYS_IMP_APL_PMCR0_EL1		sys_reg(3, 1, 15, 0, 0)
#define PMCR0_IMODE			GENMASK(10, 8)
#define PMCR0_IMODE_OFF			0
#define PMCR0_IMODE_PMI			1
#define PMCR0_IMODE_AIC			2
#define PMCR0_IMODE_HALT		3
#define PMCR0_IMODE_FIQ			4
#define PMCR0_IACT			BIT(11)

/* IPI request registers */
#define SYS_IMP_APL_IPI_RR_LOCAL_EL1	sys_reg(3, 5, 15, 0, 0)
#define SYS_IMP_APL_IPI_RR_GLOBAL_EL1	sys_reg(3, 5, 15, 0, 1)
#define IPI_RR_CPU			GENMASK(7, 0)
/* Cluster only used for the GLOBAL register */
#define IPI_RR_CLUSTER			GENMASK(23, 16)
#define IPI_RR_TYPE			GENMASK(29, 28)
#define IPI_RR_IMMEDIATE		0
#define IPI_RR_RETRACT			1
#define IPI_RR_DEFERRED			2
#define IPI_RR_NOWAKE			3

/* IPI status register */
#define SYS_IMP_APL_IPI_SR_EL1		sys_reg(3, 5, 15, 1, 1)
#define IPI_SR_PENDING			BIT(0)

/* Guest timer FIQ enable register */
#define SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2	sys_reg(3, 5, 15, 1, 3)
#define VM_TMR_FIQ_ENABLE_V		BIT(0)
#define VM_TMR_FIQ_ENABLE_P		BIT(1)

/* Deferred IPI countdown register */
#define SYS_IMP_APL_IPI_CR_EL1		sys_reg(3, 5, 15, 3, 1)

/* Uncore PMC control register */
#define SYS_IMP_APL_UPMCR0_EL1		sys_reg(3, 7, 15, 0, 4)
#define UPMCR0_IMODE			GENMASK(18, 16)
#define UPMCR0_IMODE_OFF		0
#define UPMCR0_IMODE_AIC		2
#define UPMCR0_IMODE_HALT		3
#define UPMCR0_IMODE_FIQ		4

/* Uncore PMC status register */
#define SYS_IMP_APL_UPMSR_EL1		sys_reg(3, 7, 15, 6, 4)
#define UPMSR_IACT			BIT(0)

#define AIC_NR_FIQ		6
#define AIC_NR_SWIPI		32

/*
 * FIQ hwirq index definitions: FIQ sources use the DT binding defines
 * directly, except that timers are special. At the irqchip level, the
 * two timer types are represented by their access method: _EL0 registers
 * or _EL02 registers. In the DT binding, the timers are represented
 * by their purpose (HV or guest). This mapping is for when the kernel is
 * running at EL2 (with VHE). When the kernel is running at EL1, the
 * mapping differs and aic_irq_domain_translate() performs the remapping.
 */

#define AIC_TMR_EL0_PHYS	AIC_TMR_HV_PHYS
#define AIC_TMR_EL0_VIRT	AIC_TMR_HV_VIRT
#define AIC_TMR_EL02_PHYS	AIC_TMR_GUEST_PHYS
#define AIC_TMR_EL02_VIRT	AIC_TMR_GUEST_VIRT

struct aic_irq_chip {
	void __iomem *base;
	struct irq_domain *hw_domain;
	struct irq_domain *ipi_domain;
	struct {
		cpumask_t aff;
	} *fiq_aff[AIC_NR_FIQ];
	int nr_hw;
};

static DEFINE_PER_CPU(uint32_t, aic_fiq_unmasked);

static DEFINE_PER_CPU(atomic_t, aic_vipi_flag);
static DEFINE_PER_CPU(atomic_t, aic_vipi_enable);

static struct aic_irq_chip *aic_irqc;

static void aic_handle_ipi(struct pt_regs *regs);

static u32 aic_ic_read(struct aic_irq_chip *ic, u32 reg)
{
	return readl_relaxed(ic->base + reg);
}

static void aic_ic_write(struct aic_irq_chip *ic, u32 reg, u32 val)
{
	writel_relaxed(val, ic->base + reg);
}

/*
 * IRQ irqchip
 */

static void aic_irq_mask(struct irq_data *d)
{
	struct aic_irq_chip *ic = irq_data_get_irq_chip_data(d);

	aic_ic_write(ic, AIC_MASK_SET + MASK_REG(irqd_to_hwirq(d)),
		     MASK_BIT(irqd_to_hwirq(d)));
}

static void aic_irq_unmask(struct irq_data *d)
{
	struct aic_irq_chip *ic = irq_data_get_irq_chip_data(d);

	aic_ic_write(ic, AIC_MASK_CLR + MASK_REG(d->hwirq),
		     MASK_BIT(irqd_to_hwirq(d)));
}

static void aic_irq_eoi(struct irq_data *d)
{
	/*
	 * Reading the interrupt reason automatically acknowledges and masks
	 * the IRQ, so we just unmask it here if needed.
	 */
	if (!irqd_irq_masked(d))
		aic_irq_unmask(d);
}

static void __exception_irq_entry aic_handle_irq(struct pt_regs *regs)
{
	struct aic_irq_chip *ic = aic_irqc;
	u32 event, type, irq;

	do {
		/*
		 * We cannot use a relaxed read here, as reads from DMA buffers
		 * need to be ordered after the IRQ fires.
		 */
		event = readl(ic->base + AIC_EVENT);
		type = FIELD_GET(AIC_EVENT_TYPE, event);
		irq = FIELD_GET(AIC_EVENT_NUM, event);

		if (type == AIC_EVENT_TYPE_HW)
			generic_handle_domain_irq(aic_irqc->hw_domain, irq);
		else if (type == AIC_EVENT_TYPE_IPI && irq == 1)
			aic_handle_ipi(regs);
		else if (event != 0)
			pr_err_ratelimited("Unknown IRQ event %d, %d\n", type, irq);
	} while (event);

	/*
	 * vGIC maintenance interrupts end up here too, so we need to check
	 * for them separately. This should never trigger if KVM is working
	 * properly, because it will have already taken care of clearing it
	 * on guest exit before this handler runs.
	 */
	if (is_kernel_in_hyp_mode() && (read_sysreg_s(SYS_ICH_HCR_EL2) & ICH_HCR_EN) &&
		read_sysreg_s(SYS_ICH_MISR_EL2) != 0) {
		pr_err_ratelimited("vGIC IRQ fired and not handled by KVM, disabling.\n");
		sysreg_clear_set_s(SYS_ICH_HCR_EL2, ICH_HCR_EN, 0);
	}
}

static int aic_irq_set_affinity(struct irq_data *d,
				const struct cpumask *mask_val, bool force)
{
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	struct aic_irq_chip *ic = irq_data_get_irq_chip_data(d);
	int cpu;

	if (force)
		cpu = cpumask_first(mask_val);
	else
		cpu = cpumask_any_and(mask_val, cpu_online_mask);

	aic_ic_write(ic, AIC_TARGET_CPU + hwirq * 4, BIT(cpu));
	irq_data_update_effective_affinity(d, cpumask_of(cpu));

	return IRQ_SET_MASK_OK;
}

static int aic_irq_set_type(struct irq_data *d, unsigned int type)
{
	/*
	 * Some IRQs (e.g. MSIs) implicitly have edge semantics, and we don't
	 * have a way to find out the type of any given IRQ, so just allow both.
	 */
	return (type == IRQ_TYPE_LEVEL_HIGH || type == IRQ_TYPE_EDGE_RISING) ? 0 : -EINVAL;
}

static struct irq_chip aic_chip = {
	.name = "AIC",
	.irq_mask = aic_irq_mask,
	.irq_unmask = aic_irq_unmask,
	.irq_eoi = aic_irq_eoi,
	.irq_set_affinity = aic_irq_set_affinity,
	.irq_set_type = aic_irq_set_type,
};

/*
 * FIQ irqchip
 */

static unsigned long aic_fiq_get_idx(struct irq_data *d)
{
	struct aic_irq_chip *ic = irq_data_get_irq_chip_data(d);

	return irqd_to_hwirq(d) - ic->nr_hw;
}

static void aic_fiq_set_mask(struct irq_data *d)
{
	/* Only the guest timers have real mask bits, unfortunately. */
	switch (aic_fiq_get_idx(d)) {
	case AIC_TMR_EL02_PHYS:
		sysreg_clear_set_s(SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2, VM_TMR_FIQ_ENABLE_P, 0);
		isb();
		break;
	case AIC_TMR_EL02_VIRT:
		sysreg_clear_set_s(SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2, VM_TMR_FIQ_ENABLE_V, 0);
		isb();
		break;
	default:
		break;
	}
}

static void aic_fiq_clear_mask(struct irq_data *d)
{
	switch (aic_fiq_get_idx(d)) {
	case AIC_TMR_EL02_PHYS:
		sysreg_clear_set_s(SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2, 0, VM_TMR_FIQ_ENABLE_P);
		isb();
		break;
	case AIC_TMR_EL02_VIRT:
		sysreg_clear_set_s(SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2, 0, VM_TMR_FIQ_ENABLE_V);
		isb();
		break;
	default:
		break;
	}
}

static void aic_fiq_mask(struct irq_data *d)
{
	aic_fiq_set_mask(d);
	__this_cpu_and(aic_fiq_unmasked, ~BIT(aic_fiq_get_idx(d)));
}

static void aic_fiq_unmask(struct irq_data *d)
{
	aic_fiq_clear_mask(d);
	__this_cpu_or(aic_fiq_unmasked, BIT(aic_fiq_get_idx(d)));
}

static void aic_fiq_eoi(struct irq_data *d)
{
	/* We mask to ack (where we can), so we need to unmask at EOI. */
	if (__this_cpu_read(aic_fiq_unmasked) & BIT(aic_fiq_get_idx(d)))
		aic_fiq_clear_mask(d);
}

#define TIMER_FIRING(x)                                                        \
	(((x) & (ARCH_TIMER_CTRL_ENABLE | ARCH_TIMER_CTRL_IT_MASK |            \
		 ARCH_TIMER_CTRL_IT_STAT)) ==                                  \
	 (ARCH_TIMER_CTRL_ENABLE | ARCH_TIMER_CTRL_IT_STAT))

static void __exception_irq_entry aic_handle_fiq(struct pt_regs *regs)
{
	/*
	 * It would be really nice if we had a system register that lets us get
	 * the FIQ source state without having to peek down into sources...
	 * but such a register does not seem to exist.
	 *
	 * So, we have these potential sources to test for:
	 *  - Fast IPIs (not yet used)
	 *  - The 4 timers (CNTP, CNTV for each of HV and guest)
	 *  - Per-core PMCs (not yet supported)
	 *  - Per-cluster uncore PMCs (not yet supported)
	 *
	 * Since not dealing with any of these results in a FIQ storm,
	 * we check for everything here, even things we don't support yet.
	 */

	if (read_sysreg_s(SYS_IMP_APL_IPI_SR_EL1) & IPI_SR_PENDING) {
		pr_err_ratelimited("Fast IPI fired. Acking.\n");
		write_sysreg_s(IPI_SR_PENDING, SYS_IMP_APL_IPI_SR_EL1);
	}

	if (TIMER_FIRING(read_sysreg(cntp_ctl_el0)))
		generic_handle_domain_irq(aic_irqc->hw_domain,
					  aic_irqc->nr_hw + AIC_TMR_EL0_PHYS);

	if (TIMER_FIRING(read_sysreg(cntv_ctl_el0)))
		generic_handle_domain_irq(aic_irqc->hw_domain,
					  aic_irqc->nr_hw + AIC_TMR_EL0_VIRT);

	if (is_kernel_in_hyp_mode()) {
		uint64_t enabled = read_sysreg_s(SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2);

		if ((enabled & VM_TMR_FIQ_ENABLE_P) &&
		    TIMER_FIRING(read_sysreg_s(SYS_CNTP_CTL_EL02)))
			generic_handle_domain_irq(aic_irqc->hw_domain,
						  aic_irqc->nr_hw + AIC_TMR_EL02_PHYS);

		if ((enabled & VM_TMR_FIQ_ENABLE_V) &&
		    TIMER_FIRING(read_sysreg_s(SYS_CNTV_CTL_EL02)))
			generic_handle_domain_irq(aic_irqc->hw_domain,
						  aic_irqc->nr_hw + AIC_TMR_EL02_VIRT);
	}

	if (read_sysreg_s(SYS_IMP_APL_PMCR0_EL1) & PMCR0_IACT) {
		int irq;
		if (cpumask_test_cpu(smp_processor_id(),
				     &aic_irqc->fiq_aff[AIC_CPU_PMU_P]->aff))
			irq = AIC_CPU_PMU_P;
		else
			irq = AIC_CPU_PMU_E;
		generic_handle_domain_irq(aic_irqc->hw_domain,
					  aic_irqc->nr_hw + irq);
	}

	if (FIELD_GET(UPMCR0_IMODE, read_sysreg_s(SYS_IMP_APL_UPMCR0_EL1)) == UPMCR0_IMODE_FIQ &&
			(read_sysreg_s(SYS_IMP_APL_UPMSR_EL1) & UPMSR_IACT)) {
		/* Same story with uncore PMCs */
		pr_err_ratelimited("Uncore PMC FIQ fired. Masking.\n");
		sysreg_clear_set_s(SYS_IMP_APL_UPMCR0_EL1, UPMCR0_IMODE,
				   FIELD_PREP(UPMCR0_IMODE, UPMCR0_IMODE_OFF));
	}
}

static int aic_fiq_set_type(struct irq_data *d, unsigned int type)
{
	return (type == IRQ_TYPE_LEVEL_HIGH) ? 0 : -EINVAL;
}

static struct irq_chip fiq_chip = {
	.name = "AIC-FIQ",
	.irq_mask = aic_fiq_mask,
	.irq_unmask = aic_fiq_unmask,
	.irq_ack = aic_fiq_set_mask,
	.irq_eoi = aic_fiq_eoi,
	.irq_set_type = aic_fiq_set_type,
};

/*
 * Main IRQ domain
 */

static int aic_irq_domain_map(struct irq_domain *id, unsigned int irq,
			      irq_hw_number_t hw)
{
	struct aic_irq_chip *ic = id->host_data;

	if (hw < ic->nr_hw) {
		irq_domain_set_info(id, irq, hw, &aic_chip, id->host_data,
				    handle_fasteoi_irq, NULL, NULL);
		irqd_set_single_target(irq_desc_get_irq_data(irq_to_desc(irq)));
	} else {
		int fiq = hw - ic->nr_hw;

		switch (fiq) {
		case AIC_CPU_PMU_P:
		case AIC_CPU_PMU_E:
			irq_set_percpu_devid_partition(irq, &ic->fiq_aff[fiq]->aff);
			break;
		default:
			irq_set_percpu_devid(irq);
			break;
		}

		irq_domain_set_info(id, irq, hw, &fiq_chip, id->host_data,
				    handle_percpu_devid_irq, NULL, NULL);
	}

	return 0;
}

static int aic_irq_domain_translate(struct irq_domain *id,
				    struct irq_fwspec *fwspec,
				    unsigned long *hwirq,
				    unsigned int *type)
{
	struct aic_irq_chip *ic = id->host_data;

	if (fwspec->param_count != 3 || !is_of_node(fwspec->fwnode))
		return -EINVAL;

	switch (fwspec->param[0]) {
	case AIC_IRQ:
		if (fwspec->param[1] >= ic->nr_hw)
			return -EINVAL;
		*hwirq = fwspec->param[1];
		break;
	case AIC_FIQ:
		if (fwspec->param[1] >= AIC_NR_FIQ)
			return -EINVAL;
		*hwirq = ic->nr_hw + fwspec->param[1];

		/*
		 * In EL1 the non-redirected registers are the guest's,
		 * not EL2's, so remap the hwirqs to match.
		 */
		if (!is_kernel_in_hyp_mode()) {
			switch (fwspec->param[1]) {
			case AIC_TMR_GUEST_PHYS:
				*hwirq = ic->nr_hw + AIC_TMR_EL0_PHYS;
				break;
			case AIC_TMR_GUEST_VIRT:
				*hwirq = ic->nr_hw + AIC_TMR_EL0_VIRT;
				break;
			case AIC_TMR_HV_PHYS:
			case AIC_TMR_HV_VIRT:
				return -ENOENT;
			default:
				break;
			}
		}
		break;
	default:
		return -EINVAL;
	}

	*type = fwspec->param[2] & IRQ_TYPE_SENSE_MASK;

	return 0;
}

static int aic_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs, void *arg)
{
	unsigned int type = IRQ_TYPE_NONE;
	struct irq_fwspec *fwspec = arg;
	irq_hw_number_t hwirq;
	int i, ret;

	ret = aic_irq_domain_translate(domain, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	for (i = 0; i < nr_irqs; i++) {
		ret = aic_irq_domain_map(domain, virq + i, hwirq + i);
		if (ret)
			return ret;
	}

	return 0;
}

static void aic_irq_domain_free(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs)
{
	int i;

	for (i = 0; i < nr_irqs; i++) {
		struct irq_data *d = irq_domain_get_irq_data(domain, virq + i);

		irq_set_handler(virq + i, NULL);
		irq_domain_reset_irq_data(d);
	}
}

static const struct irq_domain_ops aic_irq_domain_ops = {
	.translate	= aic_irq_domain_translate,
	.alloc		= aic_irq_domain_alloc,
	.free		= aic_irq_domain_free,
};

/*
 * IPI irqchip
 */

static void aic_ipi_mask(struct irq_data *d)
{
	u32 irq_bit = BIT(irqd_to_hwirq(d));

	/* No specific ordering requirements needed here. */
	atomic_andnot(irq_bit, this_cpu_ptr(&aic_vipi_enable));
}

static void aic_ipi_unmask(struct irq_data *d)
{
	struct aic_irq_chip *ic = irq_data_get_irq_chip_data(d);
	u32 irq_bit = BIT(irqd_to_hwirq(d));

	atomic_or(irq_bit, this_cpu_ptr(&aic_vipi_enable));

	/*
	 * The atomic_or() above must complete before the atomic_read()
	 * below to avoid racing aic_ipi_send_mask().
	 */
	smp_mb__after_atomic();

	/*
	 * If a pending vIPI was unmasked, raise a HW IPI to ourselves.
	 * No barriers needed here since this is a self-IPI.
	 */
	if (atomic_read(this_cpu_ptr(&aic_vipi_flag)) & irq_bit)
		aic_ic_write(ic, AIC_IPI_SEND, AIC_IPI_SEND_CPU(smp_processor_id()));
}

static void aic_ipi_send_mask(struct irq_data *d, const struct cpumask *mask)
{
	struct aic_irq_chip *ic = irq_data_get_irq_chip_data(d);
	u32 irq_bit = BIT(irqd_to_hwirq(d));
	u32 send = 0;
	int cpu;
	unsigned long pending;

	for_each_cpu(cpu, mask) {
		/*
		 * This sequence is the mirror of the one in aic_ipi_unmask();
		 * see the comment there. Additionally, release semantics
		 * ensure that the vIPI flag set is ordered after any shared
		 * memory accesses that precede it. This therefore also pairs
		 * with the atomic_fetch_andnot in aic_handle_ipi().
		 */
		pending = atomic_fetch_or_release(irq_bit, per_cpu_ptr(&aic_vipi_flag, cpu));

		/*
		 * The atomic_fetch_or_release() above must complete before the
		 * atomic_read() below to avoid racing aic_ipi_unmask().
		 */
		smp_mb__after_atomic();

		if (!(pending & irq_bit) &&
		    (atomic_read(per_cpu_ptr(&aic_vipi_enable, cpu)) & irq_bit))
			send |= AIC_IPI_SEND_CPU(cpu);
	}

	/*
	 * The flag writes must complete before the physical IPI is issued
	 * to another CPU. This is implied by the control dependency on
	 * the result of atomic_read_acquire() above, which is itself
	 * already ordered after the vIPI flag write.
	 */
	if (send)
		aic_ic_write(ic, AIC_IPI_SEND, send);
}

static struct irq_chip ipi_chip = {
	.name = "AIC-IPI",
	.irq_mask = aic_ipi_mask,
	.irq_unmask = aic_ipi_unmask,
	.ipi_send_mask = aic_ipi_send_mask,
};

/*
 * IPI IRQ domain
 */

static void aic_handle_ipi(struct pt_regs *regs)
{
	int i;
	unsigned long enabled, firing;

	/*
	 * Ack the IPI. We need to order this after the AIC event read, but
	 * that is enforced by normal MMIO ordering guarantees.
	 */
	aic_ic_write(aic_irqc, AIC_IPI_ACK, AIC_IPI_OTHER);

	/*
	 * The mask read does not need to be ordered. Only we can change
	 * our own mask anyway, so no races are possible here, as long as
	 * we are properly in the interrupt handler (which is covered by
	 * the barrier that is part of the top-level AIC handler's readl()).
	 */
	enabled = atomic_read(this_cpu_ptr(&aic_vipi_enable));

	/*
	 * Clear the IPIs we are about to handle. This pairs with the
	 * atomic_fetch_or_release() in aic_ipi_send_mask(), and needs to be
	 * ordered after the aic_ic_write() above (to avoid dropping vIPIs) and
	 * before IPI handling code (to avoid races handling vIPIs before they
	 * are signaled). The former is taken care of by the release semantics
	 * of the write portion, while the latter is taken care of by the
	 * acquire semantics of the read portion.
	 */
	firing = atomic_fetch_andnot(enabled, this_cpu_ptr(&aic_vipi_flag)) & enabled;

	for_each_set_bit(i, &firing, AIC_NR_SWIPI)
		generic_handle_domain_irq(aic_irqc->ipi_domain, i);

	/*
	 * No ordering needed here; at worst this just changes the timing of
	 * when the next IPI will be delivered.
	 */
	aic_ic_write(aic_irqc, AIC_IPI_MASK_CLR, AIC_IPI_OTHER);
}

static int aic_ipi_alloc(struct irq_domain *d, unsigned int virq,
			 unsigned int nr_irqs, void *args)
{
	int i;

	for (i = 0; i < nr_irqs; i++) {
		irq_set_percpu_devid(virq + i);
		irq_domain_set_info(d, virq + i, i, &ipi_chip, d->host_data,
				    handle_percpu_devid_irq, NULL, NULL);
	}

	return 0;
}

static void aic_ipi_free(struct irq_domain *d, unsigned int virq, unsigned int nr_irqs)
{
	/* Not freeing IPIs */
}

static const struct irq_domain_ops aic_ipi_domain_ops = {
	.alloc = aic_ipi_alloc,
	.free = aic_ipi_free,
};

static int __init aic_init_smp(struct aic_irq_chip *irqc, struct device_node *node)
{
	struct irq_domain *ipi_domain;
	int base_ipi;

	ipi_domain = irq_domain_create_linear(irqc->hw_domain->fwnode, AIC_NR_SWIPI,
					      &aic_ipi_domain_ops, irqc);
	if (WARN_ON(!ipi_domain))
		return -ENODEV;

	ipi_domain->flags |= IRQ_DOMAIN_FLAG_IPI_SINGLE;
	irq_domain_update_bus_token(ipi_domain, DOMAIN_BUS_IPI);

	base_ipi = __irq_domain_alloc_irqs(ipi_domain, -1, AIC_NR_SWIPI,
					   NUMA_NO_NODE, NULL, false, NULL);

	if (WARN_ON(!base_ipi)) {
		irq_domain_remove(ipi_domain);
		return -ENODEV;
	}

	set_smp_ipi_range(base_ipi, AIC_NR_SWIPI);

	irqc->ipi_domain = ipi_domain;

	return 0;
}

static int aic_init_cpu(unsigned int cpu)
{
	/* Mask all hard-wired per-CPU IRQ/FIQ sources */

	/* Pending Fast IPI FIQs */
	write_sysreg_s(IPI_SR_PENDING, SYS_IMP_APL_IPI_SR_EL1);

	/* Timer FIQs */
	sysreg_clear_set(cntp_ctl_el0, 0, ARCH_TIMER_CTRL_IT_MASK);
	sysreg_clear_set(cntv_ctl_el0, 0, ARCH_TIMER_CTRL_IT_MASK);

	/* EL2-only (VHE mode) IRQ sources */
	if (is_kernel_in_hyp_mode()) {
		/* Guest timers */
		sysreg_clear_set_s(SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2,
				   VM_TMR_FIQ_ENABLE_V | VM_TMR_FIQ_ENABLE_P, 0);

		/* vGIC maintenance IRQ */
		sysreg_clear_set_s(SYS_ICH_HCR_EL2, ICH_HCR_EN, 0);
	}

	/* PMC FIQ */
	sysreg_clear_set_s(SYS_IMP_APL_PMCR0_EL1, PMCR0_IMODE | PMCR0_IACT,
			   FIELD_PREP(PMCR0_IMODE, PMCR0_IMODE_OFF));

	/* Uncore PMC FIQ */
	sysreg_clear_set_s(SYS_IMP_APL_UPMCR0_EL1, UPMCR0_IMODE,
			   FIELD_PREP(UPMCR0_IMODE, UPMCR0_IMODE_OFF));

	/* Commit all of the above */
	isb();

	/*
	 * Make sure the kernel's idea of logical CPU order is the same as AIC's
	 * If we ever end up with a mismatch here, we will have to introduce
	 * a mapping table similar to what other irqchip drivers do.
	 */
	WARN_ON(aic_ic_read(aic_irqc, AIC_WHOAMI) != smp_processor_id());

	/*
	 * Always keep IPIs unmasked at the hardware level (except auto-masking
	 * by AIC during processing). We manage masks at the vIPI level.
	 */
	aic_ic_write(aic_irqc, AIC_IPI_ACK, AIC_IPI_SELF | AIC_IPI_OTHER);
	aic_ic_write(aic_irqc, AIC_IPI_MASK_SET, AIC_IPI_SELF);
	aic_ic_write(aic_irqc, AIC_IPI_MASK_CLR, AIC_IPI_OTHER);

	/* Initialize the local mask state */
	__this_cpu_write(aic_fiq_unmasked, 0);

	return 0;
}

static struct gic_kvm_info vgic_info __initdata = {
	.type			= GIC_V3,
	.no_maint_irq_mask	= true,
	.no_hw_deactivation	= true,
};

static void build_fiq_affinity(struct aic_irq_chip *ic, struct device_node *aff)
{
	int i, n;
	u32 fiq;

	if (of_property_read_u32(aff, "apple,fiq-index", &fiq) ||
	    WARN_ON(fiq >= AIC_NR_FIQ) || ic->fiq_aff[fiq])
		return;

	n = of_property_count_elems_of_size(aff, "cpus", sizeof(u32));
	if (WARN_ON(n < 0))
		return;

	ic->fiq_aff[fiq] = kzalloc(sizeof(ic->fiq_aff[fiq]), GFP_KERNEL);
	if (!ic->fiq_aff[fiq])
		return;

	for (i = 0; i < n; i++) {
		struct device_node *cpu_node;
		u32 cpu_phandle;
		int cpu;

		if (of_property_read_u32_index(aff, "cpus", i, &cpu_phandle))
			continue;

		cpu_node = of_find_node_by_phandle(cpu_phandle);
		if (WARN_ON(!cpu_node))
			continue;

		cpu = of_cpu_node_to_id(cpu_node);
		if (WARN_ON(cpu < 0))
			continue;

		cpumask_set_cpu(cpu, &ic->fiq_aff[fiq]->aff);
	}
}

static int __init aic_of_ic_init(struct device_node *node, struct device_node *parent)
{
	int i;
	void __iomem *regs;
	u32 info;
	struct aic_irq_chip *irqc;
	struct device_node *affs;

	regs = of_iomap(node, 0);
	if (WARN_ON(!regs))
		return -EIO;

	irqc = kzalloc(sizeof(*irqc), GFP_KERNEL);
	if (!irqc)
		return -ENOMEM;

	aic_irqc = irqc;
	irqc->base = regs;

	info = aic_ic_read(irqc, AIC_INFO);
	irqc->nr_hw = FIELD_GET(AIC_INFO_NR_HW, info);

	irqc->hw_domain = irq_domain_create_linear(of_node_to_fwnode(node),
						   irqc->nr_hw + AIC_NR_FIQ,
						   &aic_irq_domain_ops, irqc);
	if (WARN_ON(!irqc->hw_domain)) {
		iounmap(irqc->base);
		kfree(irqc);
		return -ENODEV;
	}

	irq_domain_update_bus_token(irqc->hw_domain, DOMAIN_BUS_WIRED);

	if (aic_init_smp(irqc, node)) {
		irq_domain_remove(irqc->hw_domain);
		iounmap(irqc->base);
		kfree(irqc);
		return -ENODEV;
	}

	affs = of_get_child_by_name(node, "affinities");
	if (affs) {
		struct device_node *chld;

		for_each_child_of_node(affs, chld)
			build_fiq_affinity(irqc, chld);
	}

	set_handle_irq(aic_handle_irq);
	set_handle_fiq(aic_handle_fiq);

	for (i = 0; i < BITS_TO_U32(irqc->nr_hw); i++)
		aic_ic_write(irqc, AIC_MASK_SET + i * 4, U32_MAX);
	for (i = 0; i < BITS_TO_U32(irqc->nr_hw); i++)
		aic_ic_write(irqc, AIC_SW_CLR + i * 4, U32_MAX);
	for (i = 0; i < irqc->nr_hw; i++)
		aic_ic_write(irqc, AIC_TARGET_CPU + i * 4, 1);

	if (!is_kernel_in_hyp_mode())
		pr_info("Kernel running in EL1, mapping interrupts");

	cpuhp_setup_state(CPUHP_AP_IRQ_APPLE_AIC_STARTING,
			  "irqchip/apple-aic/ipi:starting",
			  aic_init_cpu, NULL);

	vgic_set_kvm_info(&vgic_info);

	pr_info("Initialized with %d IRQs, %d FIQs, %d vIPIs\n",
		irqc->nr_hw, AIC_NR_FIQ, AIC_NR_SWIPI);

	return 0;
}

IRQCHIP_DECLARE(apple_m1_aic, "apple,aic", aic_of_ic_init);
