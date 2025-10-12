// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2025 ARM Limited, All Rights Reserved.
 */

#define pr_fmt(fmt)	"GICv5: " fmt

#include <linux/cpuhotplug.h>
#include <linux/idr.h>
#include <linux/irqdomain.h>
#include <linux/slab.h>
#include <linux/wordpart.h>

#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic-v5.h>
#include <linux/irqchip/arm-vgic-info.h>

#include <asm/cpufeature.h>
#include <asm/exception.h>

static u8 pri_bits __ro_after_init = 5;

#define GICV5_IRQ_PRI_MASK	0x1f
#define GICV5_IRQ_PRI_MI	(GICV5_IRQ_PRI_MASK & GENMASK(4, 5 - pri_bits))

#define PPI_NR	128

static bool gicv5_cpuif_has_gcie(void)
{
	return this_cpu_has_cap(ARM64_HAS_GICV5_CPUIF);
}

struct gicv5_chip_data gicv5_global_data __read_mostly;

static DEFINE_IDA(lpi_ida);
static u32 num_lpis __ro_after_init;

void __init gicv5_init_lpis(u32 lpis)
{
	num_lpis = lpis;
}

void __init gicv5_deinit_lpis(void)
{
	num_lpis = 0;
}

static int alloc_lpi(void)
{
	if (!num_lpis)
		return -ENOSPC;

	return ida_alloc_max(&lpi_ida, num_lpis - 1, GFP_KERNEL);
}

static void release_lpi(u32 lpi)
{
	ida_free(&lpi_ida, lpi);
}

int gicv5_alloc_lpi(void)
{
	return alloc_lpi();
}

void gicv5_free_lpi(u32 lpi)
{
	release_lpi(lpi);
}

static void gicv5_ppi_priority_init(void)
{
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_PRI_MI), SYS_ICC_PPI_PRIORITYR0_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_PRI_MI), SYS_ICC_PPI_PRIORITYR1_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_PRI_MI), SYS_ICC_PPI_PRIORITYR2_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_PRI_MI), SYS_ICC_PPI_PRIORITYR3_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_PRI_MI), SYS_ICC_PPI_PRIORITYR4_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_PRI_MI), SYS_ICC_PPI_PRIORITYR5_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_PRI_MI), SYS_ICC_PPI_PRIORITYR6_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_PRI_MI), SYS_ICC_PPI_PRIORITYR7_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_PRI_MI), SYS_ICC_PPI_PRIORITYR8_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_PRI_MI), SYS_ICC_PPI_PRIORITYR9_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_PRI_MI), SYS_ICC_PPI_PRIORITYR10_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_PRI_MI), SYS_ICC_PPI_PRIORITYR11_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_PRI_MI), SYS_ICC_PPI_PRIORITYR12_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_PRI_MI), SYS_ICC_PPI_PRIORITYR13_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_PRI_MI), SYS_ICC_PPI_PRIORITYR14_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_PRI_MI), SYS_ICC_PPI_PRIORITYR15_EL1);

	/*
	 * Context syncronization required to make sure system register writes
	 * effects are synchronised.
	 */
	isb();
}

static void gicv5_hwirq_init(irq_hw_number_t hwirq, u8 priority, u8 hwirq_type)
{
	u64 cdpri, cdaff;
	u16 iaffid;
	int ret;

	if (hwirq_type == GICV5_HWIRQ_TYPE_LPI || hwirq_type == GICV5_HWIRQ_TYPE_SPI) {
		cdpri = FIELD_PREP(GICV5_GIC_CDPRI_PRIORITY_MASK, priority)	|
			FIELD_PREP(GICV5_GIC_CDPRI_TYPE_MASK, hwirq_type)	|
			FIELD_PREP(GICV5_GIC_CDPRI_ID_MASK, hwirq);
		gic_insn(cdpri, CDPRI);

		ret = gicv5_irs_cpu_to_iaffid(smp_processor_id(), &iaffid);

		if (WARN_ON_ONCE(ret))
			return;

		cdaff = FIELD_PREP(GICV5_GIC_CDAFF_IAFFID_MASK, iaffid)		|
			FIELD_PREP(GICV5_GIC_CDAFF_TYPE_MASK, hwirq_type)	|
			FIELD_PREP(GICV5_GIC_CDAFF_ID_MASK, hwirq);
		gic_insn(cdaff, CDAFF);
	}
}

static void gicv5_ppi_irq_mask(struct irq_data *d)
{
	u64 hwirq_id_bit = BIT_ULL(d->hwirq % 64);

	if (d->hwirq < 64)
		sysreg_clear_set_s(SYS_ICC_PPI_ENABLER0_EL1, hwirq_id_bit, 0);
	else
		sysreg_clear_set_s(SYS_ICC_PPI_ENABLER1_EL1, hwirq_id_bit, 0);

	/*
	 * We must ensure that the disable takes effect immediately to
	 * guarantee that the lazy-disabled IRQ mechanism works.
	 * A context synchronization event is required to guarantee it.
	 * Reference: I_ZLTKB/R_YRGMH GICv5 specification - section 2.9.1.
	 */
	isb();
}

static void gicv5_iri_irq_mask(struct irq_data *d, u8 hwirq_type)
{
	u64 cddis;

	cddis = FIELD_PREP(GICV5_GIC_CDDIS_ID_MASK, d->hwirq)	|
		FIELD_PREP(GICV5_GIC_CDDIS_TYPE_MASK, hwirq_type);

	gic_insn(cddis, CDDIS);
	/*
	 * We must make sure that GIC CDDIS write effects are propagated
	 * immediately to make sure the disable takes effect to guarantee
	 * that the lazy-disabled IRQ mechanism works.
	 * Rule R_XCLJC states that the effects of a GIC system instruction
	 * complete in finite time.
	 * The GSB ensures completion of the GIC instruction and prevents
	 * loads, stores and GIC instructions from executing part of their
	 * functionality before the GSB SYS.
	 */
	gsb_sys();
}

static void gicv5_spi_irq_mask(struct irq_data *d)
{
	gicv5_iri_irq_mask(d, GICV5_HWIRQ_TYPE_SPI);
}

static void gicv5_lpi_irq_mask(struct irq_data *d)
{
	gicv5_iri_irq_mask(d, GICV5_HWIRQ_TYPE_LPI);
}

static void gicv5_ppi_irq_unmask(struct irq_data *d)
{
	u64 hwirq_id_bit = BIT_ULL(d->hwirq % 64);

	if (d->hwirq < 64)
		sysreg_clear_set_s(SYS_ICC_PPI_ENABLER0_EL1, 0, hwirq_id_bit);
	else
		sysreg_clear_set_s(SYS_ICC_PPI_ENABLER1_EL1, 0, hwirq_id_bit);
	/*
	 * We must ensure that the enable takes effect in finite time - a
	 * context synchronization event is required to guarantee it, we
	 * can not take for granted that would happen (eg a core going straight
	 * into idle after enabling a PPI).
	 * Reference: I_ZLTKB/R_YRGMH GICv5 specification - section 2.9.1.
	 */
	isb();
}

static void gicv5_iri_irq_unmask(struct irq_data *d, u8 hwirq_type)
{
	u64 cden;

	cden = FIELD_PREP(GICV5_GIC_CDEN_ID_MASK, d->hwirq)	|
	       FIELD_PREP(GICV5_GIC_CDEN_TYPE_MASK, hwirq_type);
	/*
	 * Rule R_XCLJC states that the effects of a GIC system instruction
	 * complete in finite time and that's the only requirement when
	 * unmasking an SPI/LPI IRQ.
	 */
	gic_insn(cden, CDEN);
}

static void gicv5_spi_irq_unmask(struct irq_data *d)
{
	gicv5_iri_irq_unmask(d, GICV5_HWIRQ_TYPE_SPI);
}

static void gicv5_lpi_irq_unmask(struct irq_data *d)
{
	gicv5_iri_irq_unmask(d, GICV5_HWIRQ_TYPE_LPI);
}

static void gicv5_hwirq_eoi(u32 hwirq_id, u8 hwirq_type)
{
	u64 cddi;

	cddi = FIELD_PREP(GICV5_GIC_CDDI_ID_MASK, hwirq_id)	|
	       FIELD_PREP(GICV5_GIC_CDDI_TYPE_MASK, hwirq_type);

	gic_insn(cddi, CDDI);

	gic_insn(0, CDEOI);
}

static void gicv5_ppi_irq_eoi(struct irq_data *d)
{
	/* Skip deactivate for forwarded PPI interrupts */
	if (irqd_is_forwarded_to_vcpu(d)) {
		gic_insn(0, CDEOI);
		return;
	}

	gicv5_hwirq_eoi(d->hwirq, GICV5_HWIRQ_TYPE_PPI);
}

static void gicv5_spi_irq_eoi(struct irq_data *d)
{
	gicv5_hwirq_eoi(d->hwirq, GICV5_HWIRQ_TYPE_SPI);
}

static void gicv5_lpi_irq_eoi(struct irq_data *d)
{
	gicv5_hwirq_eoi(d->hwirq, GICV5_HWIRQ_TYPE_LPI);
}

static int gicv5_iri_irq_set_affinity(struct irq_data *d,
				      const struct cpumask *mask_val,
				      bool force, u8 hwirq_type)
{
	int ret, cpuid;
	u16 iaffid;
	u64 cdaff;

	if (force)
		cpuid = cpumask_first(mask_val);
	else
		cpuid = cpumask_any_and(mask_val, cpu_online_mask);

	ret = gicv5_irs_cpu_to_iaffid(cpuid, &iaffid);
	if (ret)
		return ret;

	cdaff = FIELD_PREP(GICV5_GIC_CDAFF_IAFFID_MASK, iaffid)		|
		FIELD_PREP(GICV5_GIC_CDAFF_TYPE_MASK, hwirq_type)	|
		FIELD_PREP(GICV5_GIC_CDAFF_ID_MASK, d->hwirq);
	gic_insn(cdaff, CDAFF);

	irq_data_update_effective_affinity(d, cpumask_of(cpuid));

	return IRQ_SET_MASK_OK_DONE;
}

static int gicv5_spi_irq_set_affinity(struct irq_data *d,
				      const struct cpumask *mask_val,
				      bool force)
{
	return gicv5_iri_irq_set_affinity(d, mask_val, force,
					  GICV5_HWIRQ_TYPE_SPI);
}

static int gicv5_lpi_irq_set_affinity(struct irq_data *d,
				      const struct cpumask *mask_val,
				      bool force)
{
	return gicv5_iri_irq_set_affinity(d, mask_val, force,
					  GICV5_HWIRQ_TYPE_LPI);
}

enum ppi_reg {
	PPI_PENDING,
	PPI_ACTIVE,
	PPI_HM
};

static __always_inline u64 read_ppi_sysreg_s(unsigned int irq,
					     const enum ppi_reg which)
{
	switch (which) {
	case PPI_PENDING:
		return irq < 64	? read_sysreg_s(SYS_ICC_PPI_SPENDR0_EL1) :
				  read_sysreg_s(SYS_ICC_PPI_SPENDR1_EL1);
	case PPI_ACTIVE:
		return irq < 64	? read_sysreg_s(SYS_ICC_PPI_SACTIVER0_EL1) :
				  read_sysreg_s(SYS_ICC_PPI_SACTIVER1_EL1);
	case PPI_HM:
		return irq < 64	? read_sysreg_s(SYS_ICC_PPI_HMR0_EL1) :
				  read_sysreg_s(SYS_ICC_PPI_HMR1_EL1);
	default:
		BUILD_BUG_ON(1);
	}
}

static __always_inline void write_ppi_sysreg_s(unsigned int irq, bool set,
					       const enum ppi_reg which)
{
	u64 bit = BIT_ULL(irq % 64);

	switch (which) {
	case PPI_PENDING:
		if (set) {
			if (irq < 64)
				write_sysreg_s(bit, SYS_ICC_PPI_SPENDR0_EL1);
			else
				write_sysreg_s(bit, SYS_ICC_PPI_SPENDR1_EL1);
		} else {
			if (irq < 64)
				write_sysreg_s(bit, SYS_ICC_PPI_CPENDR0_EL1);
			else
				write_sysreg_s(bit, SYS_ICC_PPI_CPENDR1_EL1);
		}
		return;
	case PPI_ACTIVE:
		if (set) {
			if (irq < 64)
				write_sysreg_s(bit, SYS_ICC_PPI_SACTIVER0_EL1);
			else
				write_sysreg_s(bit, SYS_ICC_PPI_SACTIVER1_EL1);
		} else {
			if (irq < 64)
				write_sysreg_s(bit, SYS_ICC_PPI_CACTIVER0_EL1);
			else
				write_sysreg_s(bit, SYS_ICC_PPI_CACTIVER1_EL1);
		}
		return;
	default:
		BUILD_BUG_ON(1);
	}
}

static int gicv5_ppi_irq_get_irqchip_state(struct irq_data *d,
					   enum irqchip_irq_state which,
					   bool *state)
{
	u64 hwirq_id_bit = BIT_ULL(d->hwirq % 64);

	switch (which) {
	case IRQCHIP_STATE_PENDING:
		*state = !!(read_ppi_sysreg_s(d->hwirq, PPI_PENDING) & hwirq_id_bit);
		return 0;
	case IRQCHIP_STATE_ACTIVE:
		*state = !!(read_ppi_sysreg_s(d->hwirq, PPI_ACTIVE) & hwirq_id_bit);
		return 0;
	default:
		pr_debug("Unexpected PPI irqchip state\n");
		return -EINVAL;
	}
}

static int gicv5_iri_irq_get_irqchip_state(struct irq_data *d,
					   enum irqchip_irq_state which,
					   bool *state, u8 hwirq_type)
{
	u64 icsr, cdrcfg;

	cdrcfg = d->hwirq | FIELD_PREP(GICV5_GIC_CDRCFG_TYPE_MASK, hwirq_type);

	gic_insn(cdrcfg, CDRCFG);
	isb();
	icsr = read_sysreg_s(SYS_ICC_ICSR_EL1);

	if (FIELD_GET(ICC_ICSR_EL1_F, icsr)) {
		pr_err("ICSR_EL1 is invalid\n");
		return -EINVAL;
	}

	switch (which) {
	case IRQCHIP_STATE_PENDING:
		*state = !!(FIELD_GET(ICC_ICSR_EL1_Pending, icsr));
		return 0;

	case IRQCHIP_STATE_ACTIVE:
		*state = !!(FIELD_GET(ICC_ICSR_EL1_Active, icsr));
		return 0;

	default:
		pr_debug("Unexpected irqchip_irq_state\n");
		return -EINVAL;
	}
}

static int gicv5_spi_irq_get_irqchip_state(struct irq_data *d,
					   enum irqchip_irq_state which,
					   bool *state)
{
	return gicv5_iri_irq_get_irqchip_state(d, which, state,
					       GICV5_HWIRQ_TYPE_SPI);
}

static int gicv5_lpi_irq_get_irqchip_state(struct irq_data *d,
					   enum irqchip_irq_state which,
					   bool *state)
{
	return gicv5_iri_irq_get_irqchip_state(d, which, state,
					       GICV5_HWIRQ_TYPE_LPI);
}

static int gicv5_ppi_irq_set_irqchip_state(struct irq_data *d,
					   enum irqchip_irq_state which,
					   bool state)
{
	switch (which) {
	case IRQCHIP_STATE_PENDING:
		write_ppi_sysreg_s(d->hwirq, state, PPI_PENDING);
		return 0;
	case IRQCHIP_STATE_ACTIVE:
		write_ppi_sysreg_s(d->hwirq, state, PPI_ACTIVE);
		return 0;
	default:
		pr_debug("Unexpected PPI irqchip state\n");
		return -EINVAL;
	}
}

static void gicv5_iri_irq_write_pending_state(struct irq_data *d, bool state,
					      u8 hwirq_type)
{
	u64 cdpend;

	cdpend = FIELD_PREP(GICV5_GIC_CDPEND_TYPE_MASK, hwirq_type)	|
		 FIELD_PREP(GICV5_GIC_CDPEND_ID_MASK, d->hwirq)		|
		 FIELD_PREP(GICV5_GIC_CDPEND_PENDING_MASK, state);

	gic_insn(cdpend, CDPEND);
}

static void gicv5_spi_irq_write_pending_state(struct irq_data *d, bool state)
{
	gicv5_iri_irq_write_pending_state(d, state, GICV5_HWIRQ_TYPE_SPI);
}

static void gicv5_lpi_irq_write_pending_state(struct irq_data *d, bool state)
{
	gicv5_iri_irq_write_pending_state(d, state, GICV5_HWIRQ_TYPE_LPI);
}

static int gicv5_spi_irq_set_irqchip_state(struct irq_data *d,
					   enum irqchip_irq_state which,
					   bool state)
{
	switch (which) {
	case IRQCHIP_STATE_PENDING:
		gicv5_spi_irq_write_pending_state(d, state);
		break;
	default:
		pr_debug("Unexpected irqchip_irq_state\n");
		return -EINVAL;
	}

	return 0;
}

static int gicv5_lpi_irq_set_irqchip_state(struct irq_data *d,
					   enum irqchip_irq_state which,
					   bool state)
{
	switch (which) {
	case IRQCHIP_STATE_PENDING:
		gicv5_lpi_irq_write_pending_state(d, state);
		break;

	default:
		pr_debug("Unexpected irqchip_irq_state\n");
		return -EINVAL;
	}

	return 0;
}

static int gicv5_spi_irq_retrigger(struct irq_data *data)
{
	return !gicv5_spi_irq_set_irqchip_state(data, IRQCHIP_STATE_PENDING,
						true);
}

static int gicv5_lpi_irq_retrigger(struct irq_data *data)
{
	return !gicv5_lpi_irq_set_irqchip_state(data, IRQCHIP_STATE_PENDING,
						true);
}

static void gicv5_ipi_send_single(struct irq_data *d, unsigned int cpu)
{
	/* Mark the LPI pending */
	irq_chip_retrigger_hierarchy(d);
}

static bool gicv5_ppi_irq_is_level(irq_hw_number_t hwirq)
{
	u64 bit = BIT_ULL(hwirq % 64);

	return !!(read_ppi_sysreg_s(hwirq, PPI_HM) & bit);
}

static int gicv5_ppi_irq_set_vcpu_affinity(struct irq_data *d, void *vcpu)
{
	if (vcpu)
		irqd_set_forwarded_to_vcpu(d);
	else
		irqd_clr_forwarded_to_vcpu(d);

	return 0;
}

static const struct irq_chip gicv5_ppi_irq_chip = {
	.name			= "GICv5-PPI",
	.irq_mask		= gicv5_ppi_irq_mask,
	.irq_unmask		= gicv5_ppi_irq_unmask,
	.irq_eoi		= gicv5_ppi_irq_eoi,
	.irq_get_irqchip_state	= gicv5_ppi_irq_get_irqchip_state,
	.irq_set_irqchip_state	= gicv5_ppi_irq_set_irqchip_state,
	.irq_set_vcpu_affinity	= gicv5_ppi_irq_set_vcpu_affinity,
	.flags			= IRQCHIP_SKIP_SET_WAKE	  |
				  IRQCHIP_MASK_ON_SUSPEND,
};

static const struct irq_chip gicv5_spi_irq_chip = {
	.name			= "GICv5-SPI",
	.irq_mask		= gicv5_spi_irq_mask,
	.irq_unmask		= gicv5_spi_irq_unmask,
	.irq_eoi		= gicv5_spi_irq_eoi,
	.irq_set_type		= gicv5_spi_irq_set_type,
	.irq_set_affinity	= gicv5_spi_irq_set_affinity,
	.irq_retrigger		= gicv5_spi_irq_retrigger,
	.irq_get_irqchip_state	= gicv5_spi_irq_get_irqchip_state,
	.irq_set_irqchip_state	= gicv5_spi_irq_set_irqchip_state,
	.flags			= IRQCHIP_SET_TYPE_MASKED |
				  IRQCHIP_SKIP_SET_WAKE	  |
				  IRQCHIP_MASK_ON_SUSPEND,
};

static const struct irq_chip gicv5_lpi_irq_chip = {
	.name			= "GICv5-LPI",
	.irq_mask		= gicv5_lpi_irq_mask,
	.irq_unmask		= gicv5_lpi_irq_unmask,
	.irq_eoi		= gicv5_lpi_irq_eoi,
	.irq_set_affinity	= gicv5_lpi_irq_set_affinity,
	.irq_retrigger		= gicv5_lpi_irq_retrigger,
	.irq_get_irqchip_state	= gicv5_lpi_irq_get_irqchip_state,
	.irq_set_irqchip_state	= gicv5_lpi_irq_set_irqchip_state,
	.flags			= IRQCHIP_SKIP_SET_WAKE	  |
				  IRQCHIP_MASK_ON_SUSPEND,
};

static const struct irq_chip gicv5_ipi_irq_chip = {
	.name			= "GICv5-IPI",
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
	.irq_get_irqchip_state	= irq_chip_get_parent_state,
	.irq_set_irqchip_state	= irq_chip_set_parent_state,
	.ipi_send_single	= gicv5_ipi_send_single,
	.flags			= IRQCHIP_SKIP_SET_WAKE	  |
				  IRQCHIP_MASK_ON_SUSPEND,
};

static __always_inline int gicv5_irq_domain_translate(struct irq_domain *d,
						      struct irq_fwspec *fwspec,
						      irq_hw_number_t *hwirq,
						      unsigned int *type,
						      const u8 hwirq_type)
{
	if (!is_of_node(fwspec->fwnode))
		return -EINVAL;

	if (fwspec->param_count < 3)
		return -EINVAL;

	if (fwspec->param[0] != hwirq_type)
		return -EINVAL;

	*hwirq = fwspec->param[1];

	switch (hwirq_type) {
	case GICV5_HWIRQ_TYPE_PPI:
		/*
		 * Handling mode is hardcoded for PPIs, set the type using
		 * HW reported value.
		 */
		*type = gicv5_ppi_irq_is_level(*hwirq) ? IRQ_TYPE_LEVEL_LOW :
							 IRQ_TYPE_EDGE_RISING;
		break;
	case GICV5_HWIRQ_TYPE_SPI:
		*type = fwspec->param[2] & IRQ_TYPE_SENSE_MASK;
		break;
	default:
		BUILD_BUG_ON(1);
	}

	return 0;
}

static int gicv5_irq_ppi_domain_translate(struct irq_domain *d,
					  struct irq_fwspec *fwspec,
					  irq_hw_number_t *hwirq,
					  unsigned int *type)
{
	return gicv5_irq_domain_translate(d, fwspec, hwirq, type,
					  GICV5_HWIRQ_TYPE_PPI);
}

static int gicv5_irq_ppi_domain_alloc(struct irq_domain *domain, unsigned int virq,
				      unsigned int nr_irqs, void *arg)
{
	unsigned int type = IRQ_TYPE_NONE;
	struct irq_fwspec *fwspec = arg;
	irq_hw_number_t hwirq;
	int ret;

	if (WARN_ON_ONCE(nr_irqs != 1))
		return -EINVAL;

	ret = gicv5_irq_ppi_domain_translate(domain, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_status_flags(virq, IRQ_LEVEL);

	irq_set_percpu_devid(virq);
	irq_domain_set_info(domain, virq, hwirq, &gicv5_ppi_irq_chip, NULL,
			    handle_percpu_devid_irq, NULL, NULL);

	return 0;
}

static void gicv5_irq_domain_free(struct irq_domain *domain, unsigned int virq,
				  unsigned int nr_irqs)
{
	struct irq_data *d;

	if (WARN_ON_ONCE(nr_irqs != 1))
		return;

	d = irq_domain_get_irq_data(domain, virq);

	irq_set_handler(virq, NULL);
	irq_domain_reset_irq_data(d);
}

static int gicv5_irq_ppi_domain_select(struct irq_domain *d, struct irq_fwspec *fwspec,
				       enum irq_domain_bus_token bus_token)
{
	if (fwspec->fwnode != d->fwnode)
		return 0;

	if (fwspec->param[0] != GICV5_HWIRQ_TYPE_PPI)
		return 0;

	return (d == gicv5_global_data.ppi_domain);
}

static const struct irq_domain_ops gicv5_irq_ppi_domain_ops = {
	.translate	= gicv5_irq_ppi_domain_translate,
	.alloc		= gicv5_irq_ppi_domain_alloc,
	.free		= gicv5_irq_domain_free,
	.select		= gicv5_irq_ppi_domain_select
};

static int gicv5_irq_spi_domain_translate(struct irq_domain *d,
					  struct irq_fwspec *fwspec,
					  irq_hw_number_t *hwirq,
					  unsigned int *type)
{
	return gicv5_irq_domain_translate(d, fwspec, hwirq, type,
					  GICV5_HWIRQ_TYPE_SPI);
}

static int gicv5_irq_spi_domain_alloc(struct irq_domain *domain, unsigned int virq,
				      unsigned int nr_irqs, void *arg)
{
	struct gicv5_irs_chip_data *chip_data;
	unsigned int type = IRQ_TYPE_NONE;
	struct irq_fwspec *fwspec = arg;
	struct irq_data *irqd;
	irq_hw_number_t hwirq;
	int ret;

	if (WARN_ON_ONCE(nr_irqs != 1))
		return -EINVAL;

	ret = gicv5_irq_spi_domain_translate(domain, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	irqd = irq_desc_get_irq_data(irq_to_desc(virq));
	chip_data = gicv5_irs_lookup_by_spi_id(hwirq);

	irq_domain_set_info(domain, virq, hwirq, &gicv5_spi_irq_chip, chip_data,
			    handle_fasteoi_irq, NULL, NULL);
	irq_set_probe(virq);
	irqd_set_single_target(irqd);

	gicv5_hwirq_init(hwirq, GICV5_IRQ_PRI_MI, GICV5_HWIRQ_TYPE_SPI);

	return 0;
}

static int gicv5_irq_spi_domain_select(struct irq_domain *d, struct irq_fwspec *fwspec,
				       enum irq_domain_bus_token bus_token)
{
	if (fwspec->fwnode != d->fwnode)
		return 0;

	if (fwspec->param[0] != GICV5_HWIRQ_TYPE_SPI)
		return 0;

	return (d == gicv5_global_data.spi_domain);
}

static const struct irq_domain_ops gicv5_irq_spi_domain_ops = {
	.translate	= gicv5_irq_spi_domain_translate,
	.alloc		= gicv5_irq_spi_domain_alloc,
	.free		= gicv5_irq_domain_free,
	.select		= gicv5_irq_spi_domain_select
};

static void gicv5_lpi_config_reset(struct irq_data *d)
{
	u64 cdhm;

	/*
	 * Reset LPIs handling mode to edge by default and clear pending
	 * state to make sure we start the LPI with a clean state from
	 * previous incarnations.
	 */
	cdhm = FIELD_PREP(GICV5_GIC_CDHM_HM_MASK, 0)				|
	       FIELD_PREP(GICV5_GIC_CDHM_TYPE_MASK, GICV5_HWIRQ_TYPE_LPI)	|
	       FIELD_PREP(GICV5_GIC_CDHM_ID_MASK, d->hwirq);
	gic_insn(cdhm, CDHM);

	gicv5_lpi_irq_write_pending_state(d, false);
}

static int gicv5_irq_lpi_domain_alloc(struct irq_domain *domain, unsigned int virq,
				      unsigned int nr_irqs, void *arg)
{
	irq_hw_number_t hwirq;
	struct irq_data *irqd;
	u32 *lpi = arg;
	int ret;

	if (WARN_ON_ONCE(nr_irqs != 1))
		return -EINVAL;

	hwirq = *lpi;

	irqd = irq_domain_get_irq_data(domain, virq);

	irq_domain_set_info(domain, virq, hwirq, &gicv5_lpi_irq_chip, NULL,
			    handle_fasteoi_irq, NULL, NULL);
	irqd_set_single_target(irqd);

	ret = gicv5_irs_iste_alloc(hwirq);
	if (ret < 0)
		return ret;

	gicv5_hwirq_init(hwirq, GICV5_IRQ_PRI_MI, GICV5_HWIRQ_TYPE_LPI);
	gicv5_lpi_config_reset(irqd);

	return 0;
}

static const struct irq_domain_ops gicv5_irq_lpi_domain_ops = {
	.alloc	= gicv5_irq_lpi_domain_alloc,
	.free	= gicv5_irq_domain_free,
};

void __init gicv5_init_lpi_domain(void)
{
	struct irq_domain *d;

	d = irq_domain_create_tree(NULL, &gicv5_irq_lpi_domain_ops, NULL);
	gicv5_global_data.lpi_domain = d;
}

void __init gicv5_free_lpi_domain(void)
{
	irq_domain_remove(gicv5_global_data.lpi_domain);
	gicv5_global_data.lpi_domain = NULL;
}

static int gicv5_irq_ipi_domain_alloc(struct irq_domain *domain, unsigned int virq,
				      unsigned int nr_irqs, void *arg)
{
	struct irq_data *irqd;
	int ret, i;
	u32 lpi;

	for (i = 0; i < nr_irqs; i++) {
		ret = gicv5_alloc_lpi();
		if (ret < 0)
			return ret;

		lpi = ret;

		ret = irq_domain_alloc_irqs_parent(domain, virq + i, 1, &lpi);
		if (ret) {
			gicv5_free_lpi(lpi);
			return ret;
		}

		irqd = irq_domain_get_irq_data(domain, virq + i);

		irq_domain_set_hwirq_and_chip(domain, virq + i, i,
				&gicv5_ipi_irq_chip, NULL);

		irqd_set_single_target(irqd);

		irq_set_handler(virq + i, handle_percpu_irq);
	}

	return 0;
}

static void gicv5_irq_ipi_domain_free(struct irq_domain *domain, unsigned int virq,
				      unsigned int nr_irqs)
{
	struct irq_data *d;
	unsigned int i;

	for (i = 0; i < nr_irqs; i++) {
		d = irq_domain_get_irq_data(domain, virq + i);

		if (!d)
			return;

		gicv5_free_lpi(d->parent_data->hwirq);

		irq_set_handler(virq + i, NULL);
		irq_domain_reset_irq_data(d);
		irq_domain_free_irqs_parent(domain, virq + i, 1);
	}
}

static const struct irq_domain_ops gicv5_irq_ipi_domain_ops = {
	.alloc	= gicv5_irq_ipi_domain_alloc,
	.free	= gicv5_irq_ipi_domain_free,
};

static void handle_irq_per_domain(u32 hwirq)
{
	u8 hwirq_type = FIELD_GET(GICV5_HWIRQ_TYPE, hwirq);
	u32 hwirq_id = FIELD_GET(GICV5_HWIRQ_ID, hwirq);
	struct irq_domain *domain;

	switch (hwirq_type) {
	case GICV5_HWIRQ_TYPE_PPI:
		domain = gicv5_global_data.ppi_domain;
		break;
	case GICV5_HWIRQ_TYPE_SPI:
		domain = gicv5_global_data.spi_domain;
		break;
	case GICV5_HWIRQ_TYPE_LPI:
		domain = gicv5_global_data.lpi_domain;
		break;
	default:
		pr_err_once("Unknown IRQ type, bail out\n");
		return;
	}

	if (generic_handle_domain_irq(domain, hwirq_id)) {
		pr_err_once("Could not handle, hwirq = 0x%x", hwirq_id);
		gicv5_hwirq_eoi(hwirq_id, hwirq_type);
	}
}

static void __exception_irq_entry gicv5_handle_irq(struct pt_regs *regs)
{
	bool valid;
	u32 hwirq;
	u64 ia;

	ia = gicr_insn(CDIA);
	valid = GICV5_GICR_CDIA_VALID(ia);

	if (!valid)
		return;

	/*
	 * Ensure that the CDIA instruction effects (ie IRQ activation) are
	 * completed before handling the interrupt.
	 */
	gsb_ack();

	/*
	 * Ensure instruction ordering between an acknowledgment and subsequent
	 * instructions in the IRQ handler using an ISB.
	 */
	isb();

	hwirq = FIELD_GET(GICV5_HWIRQ_INTID, ia);

	handle_irq_per_domain(hwirq);
}

static void gicv5_cpu_disable_interrupts(void)
{
	u64 cr0;

	cr0 = FIELD_PREP(ICC_CR0_EL1_EN, 0);
	write_sysreg_s(cr0, SYS_ICC_CR0_EL1);
}

static void gicv5_cpu_enable_interrupts(void)
{
	u64 cr0, pcr;

	write_sysreg_s(0, SYS_ICC_PPI_ENABLER0_EL1);
	write_sysreg_s(0, SYS_ICC_PPI_ENABLER1_EL1);

	gicv5_ppi_priority_init();

	pcr = FIELD_PREP(ICC_PCR_EL1_PRIORITY, GICV5_IRQ_PRI_MI);
	write_sysreg_s(pcr, SYS_ICC_PCR_EL1);

	cr0 = FIELD_PREP(ICC_CR0_EL1_EN, 1);
	write_sysreg_s(cr0, SYS_ICC_CR0_EL1);
}

static int base_ipi_virq;

static int gicv5_starting_cpu(unsigned int cpu)
{
	if (WARN(!gicv5_cpuif_has_gcie(),
		 "GICv5 system components present but CPU does not have FEAT_GCIE"))
		return -ENODEV;

	gicv5_cpu_enable_interrupts();

	return gicv5_irs_register_cpu(cpu);
}

static void __init gicv5_smp_init(void)
{
	unsigned int num_ipis = GICV5_IPIS_PER_CPU * nr_cpu_ids;

	cpuhp_setup_state_nocalls(CPUHP_AP_IRQ_GIC_STARTING,
				  "irqchip/arm/gicv5:starting",
				  gicv5_starting_cpu, NULL);

	base_ipi_virq = irq_domain_alloc_irqs(gicv5_global_data.ipi_domain,
					      num_ipis, NUMA_NO_NODE, NULL);
	if (WARN(base_ipi_virq <= 0, "IPI IRQ allocation was not successful"))
		return;

	set_smp_ipi_range_percpu(base_ipi_virq, GICV5_IPIS_PER_CPU, nr_cpu_ids);
}

static void __init gicv5_free_domains(void)
{
	if (gicv5_global_data.ppi_domain)
		irq_domain_remove(gicv5_global_data.ppi_domain);
	if (gicv5_global_data.spi_domain)
		irq_domain_remove(gicv5_global_data.spi_domain);
	if (gicv5_global_data.ipi_domain)
		irq_domain_remove(gicv5_global_data.ipi_domain);

	gicv5_global_data.ppi_domain = NULL;
	gicv5_global_data.spi_domain = NULL;
	gicv5_global_data.ipi_domain = NULL;
}

static int __init gicv5_init_domains(struct fwnode_handle *handle)
{
	u32 spi_count = gicv5_global_data.global_spi_count;
	struct irq_domain *d;

	d = irq_domain_create_linear(handle, PPI_NR, &gicv5_irq_ppi_domain_ops, NULL);
	if (!d)
		return -ENOMEM;

	irq_domain_update_bus_token(d, DOMAIN_BUS_WIRED);
	gicv5_global_data.ppi_domain = d;

	if (spi_count) {
		d = irq_domain_create_linear(handle, spi_count,
					     &gicv5_irq_spi_domain_ops, NULL);

		if (!d) {
			gicv5_free_domains();
			return -ENOMEM;
		}

		gicv5_global_data.spi_domain = d;
		irq_domain_update_bus_token(d, DOMAIN_BUS_WIRED);
	}

	if (!WARN(!gicv5_global_data.lpi_domain,
		  "LPI domain uninitialized, can't set up IPIs")) {
		d = irq_domain_create_hierarchy(gicv5_global_data.lpi_domain,
						0, GICV5_IPIS_PER_CPU * nr_cpu_ids,
						NULL, &gicv5_irq_ipi_domain_ops,
						NULL);

		if (!d) {
			gicv5_free_domains();
			return -ENOMEM;
		}
		gicv5_global_data.ipi_domain = d;
	}
	gicv5_global_data.fwnode = handle;

	return 0;
}

static void gicv5_set_cpuif_pribits(void)
{
	u64 icc_idr0 = read_sysreg_s(SYS_ICC_IDR0_EL1);

	switch (FIELD_GET(ICC_IDR0_EL1_PRI_BITS, icc_idr0)) {
	case ICC_IDR0_EL1_PRI_BITS_4BITS:
		gicv5_global_data.cpuif_pri_bits = 4;
		break;
	case ICC_IDR0_EL1_PRI_BITS_5BITS:
		gicv5_global_data.cpuif_pri_bits = 5;
		break;
	default:
		pr_err("Unexpected ICC_IDR0_EL1_PRI_BITS value, default to 4");
		gicv5_global_data.cpuif_pri_bits = 4;
		break;
	}
}

static void gicv5_set_cpuif_idbits(void)
{
	u32 icc_idr0 = read_sysreg_s(SYS_ICC_IDR0_EL1);

	switch (FIELD_GET(ICC_IDR0_EL1_ID_BITS, icc_idr0)) {
	case ICC_IDR0_EL1_ID_BITS_16BITS:
		gicv5_global_data.cpuif_id_bits = 16;
		break;
	case ICC_IDR0_EL1_ID_BITS_24BITS:
		gicv5_global_data.cpuif_id_bits = 24;
		break;
	default:
		pr_err("Unexpected ICC_IDR0_EL1_ID_BITS value, default to 16");
		gicv5_global_data.cpuif_id_bits = 16;
		break;
	}
}

#ifdef CONFIG_KVM
static struct gic_kvm_info gic_v5_kvm_info __initdata;

static void __init gic_of_setup_kvm_info(struct device_node *node)
{
	gic_v5_kvm_info.type = GIC_V5;

	/* GIC Virtual CPU interface maintenance interrupt */
	gic_v5_kvm_info.no_maint_irq_mask = false;
	gic_v5_kvm_info.maint_irq = irq_of_parse_and_map(node, 0);
	if (!gic_v5_kvm_info.maint_irq) {
		pr_warn("cannot find GICv5 virtual CPU interface maintenance interrupt\n");
		return;
	}

	vgic_set_kvm_info(&gic_v5_kvm_info);
}
#else
static inline void __init gic_of_setup_kvm_info(struct device_node *node)
{
}
#endif // CONFIG_KVM

static int __init gicv5_of_init(struct device_node *node, struct device_node *parent)
{
	int ret = gicv5_irs_of_probe(node);
	if (ret)
		return ret;

	ret = gicv5_init_domains(of_fwnode_handle(node));
	if (ret)
		goto out_irs;

	gicv5_set_cpuif_pribits();
	gicv5_set_cpuif_idbits();

	pri_bits = min_not_zero(gicv5_global_data.cpuif_pri_bits,
				gicv5_global_data.irs_pri_bits);

	ret = gicv5_starting_cpu(smp_processor_id());
	if (ret)
		goto out_dom;

	ret = set_handle_irq(gicv5_handle_irq);
	if (ret)
		goto out_int;

	ret = gicv5_irs_enable();
	if (ret)
		goto out_int;

	gicv5_smp_init();

	gicv5_irs_its_probe();

	gic_of_setup_kvm_info(node);

	return 0;

out_int:
	gicv5_cpu_disable_interrupts();
out_dom:
	gicv5_free_domains();
out_irs:
	gicv5_irs_remove();

	return ret;
}
IRQCHIP_DECLARE(gic_v5, "arm,gic-v5", gicv5_of_init);
