// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017-2018 SiFive
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 */

#define pr_fmt(fmt) "riscv-intc: " fmt
#include <linux/acpi.h>
#include <linux/atomic.h>
#include <linux/bits.h>
#include <linux/cpu.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/smp.h>
#include <linux/soc/andes/irq.h>

#include <asm/hwcap.h>

static struct irq_domain *intc_domain;
static unsigned int riscv_intc_nr_irqs __ro_after_init = BITS_PER_LONG;
static unsigned int riscv_intc_custom_base __ro_after_init = BITS_PER_LONG;
static unsigned int riscv_intc_custom_nr_irqs __ro_after_init;

static asmlinkage void riscv_intc_irq(struct pt_regs *regs)
{
	unsigned long cause = regs->cause & ~CAUSE_IRQ_FLAG;

	if (generic_handle_domain_irq(intc_domain, cause))
		pr_warn_ratelimited("Failed to handle interrupt (cause: %ld)\n", cause);
}

static asmlinkage void riscv_intc_aia_irq(struct pt_regs *regs)
{
	unsigned long topi;

	while ((topi = csr_read(CSR_TOPI)))
		generic_handle_domain_irq(intc_domain, topi >> TOPI_IID_SHIFT);
}

/*
 * On RISC-V systems local interrupts are masked or unmasked by writing
 * the SIE (Supervisor Interrupt Enable) CSR.  As CSRs can only be written
 * on the local hart, these functions can only be called on the hart that
 * corresponds to the IRQ chip.
 */

static void riscv_intc_irq_mask(struct irq_data *d)
{
	if (IS_ENABLED(CONFIG_32BIT) && d->hwirq >= BITS_PER_LONG)
		csr_clear(CSR_IEH, BIT(d->hwirq - BITS_PER_LONG));
	else
		csr_clear(CSR_IE, BIT(d->hwirq));
}

static void riscv_intc_irq_unmask(struct irq_data *d)
{
	if (IS_ENABLED(CONFIG_32BIT) && d->hwirq >= BITS_PER_LONG)
		csr_set(CSR_IEH, BIT(d->hwirq - BITS_PER_LONG));
	else
		csr_set(CSR_IE, BIT(d->hwirq));
}

static void andes_intc_irq_mask(struct irq_data *d)
{
	/*
	 * Andes specific S-mode local interrupt causes (hwirq)
	 * are defined as (256 + n) and controlled by n-th bit
	 * of SLIE.
	 */
	unsigned int mask = BIT(d->hwirq % BITS_PER_LONG);

	if (d->hwirq < ANDES_SLI_CAUSE_BASE)
		csr_clear(CSR_IE, mask);
	else
		csr_clear(ANDES_CSR_SLIE, mask);
}

static void andes_intc_irq_unmask(struct irq_data *d)
{
	unsigned int mask = BIT(d->hwirq % BITS_PER_LONG);

	if (d->hwirq < ANDES_SLI_CAUSE_BASE)
		csr_set(CSR_IE, mask);
	else
		csr_set(ANDES_CSR_SLIE, mask);
}

static void riscv_intc_irq_eoi(struct irq_data *d)
{
	/*
	 * The RISC-V INTC driver uses handle_percpu_devid_irq() flow
	 * for the per-HART local interrupts and child irqchip drivers
	 * (such as PLIC, SBI IPI, CLINT, APLIC, IMSIC, etc) implement
	 * chained handlers for the per-HART local interrupts.
	 *
	 * In the absence of irq_eoi(), the chained_irq_enter() and
	 * chained_irq_exit() functions (used by child irqchip drivers)
	 * will do unnecessary mask/unmask of per-HART local interrupts
	 * at the time of handling interrupts. To avoid this, we provide
	 * an empty irq_eoi() callback for RISC-V INTC irqchip.
	 */
}

static struct irq_chip riscv_intc_chip = {
	.name = "RISC-V INTC",
	.irq_mask = riscv_intc_irq_mask,
	.irq_unmask = riscv_intc_irq_unmask,
	.irq_eoi = riscv_intc_irq_eoi,
};

static struct irq_chip andes_intc_chip = {
	.name		= "RISC-V INTC",
	.irq_mask	= andes_intc_irq_mask,
	.irq_unmask	= andes_intc_irq_unmask,
	.irq_eoi	= riscv_intc_irq_eoi,
};

static int riscv_intc_domain_map(struct irq_domain *d, unsigned int irq,
				 irq_hw_number_t hwirq)
{
	struct irq_chip *chip = d->host_data;

	irq_set_percpu_devid(irq);
	irq_domain_set_info(d, irq, hwirq, chip, NULL, handle_percpu_devid_irq,
			    NULL, NULL);

	return 0;
}

static int riscv_intc_domain_alloc(struct irq_domain *domain,
				   unsigned int virq, unsigned int nr_irqs,
				   void *arg)
{
	int i, ret;
	irq_hw_number_t hwirq;
	unsigned int type = IRQ_TYPE_NONE;
	struct irq_fwspec *fwspec = arg;

	ret = irq_domain_translate_onecell(domain, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	/*
	 * Only allow hwirq for which we have corresponding standard or
	 * custom interrupt enable register.
	 */
	if (hwirq >= riscv_intc_nr_irqs &&
	    (hwirq < riscv_intc_custom_base ||
	     hwirq >= riscv_intc_custom_base + riscv_intc_custom_nr_irqs))
		return -EINVAL;

	for (i = 0; i < nr_irqs; i++) {
		ret = riscv_intc_domain_map(domain, virq + i, hwirq + i);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct irq_domain_ops riscv_intc_domain_ops = {
	.map	= riscv_intc_domain_map,
	.xlate	= irq_domain_xlate_onecell,
	.alloc	= riscv_intc_domain_alloc
};

static struct fwnode_handle *riscv_intc_hwnode(void)
{
	return intc_domain->fwnode;
}

static int __init riscv_intc_init_common(struct fwnode_handle *fn, struct irq_chip *chip)
{
	int rc;

	intc_domain = irq_domain_create_tree(fn, &riscv_intc_domain_ops, chip);
	if (!intc_domain) {
		pr_err("unable to add IRQ domain\n");
		return -ENXIO;
	}

	if (riscv_isa_extension_available(NULL, SxAIA)) {
		riscv_intc_nr_irqs = 64;
		rc = set_handle_irq(&riscv_intc_aia_irq);
	} else {
		rc = set_handle_irq(&riscv_intc_irq);
	}
	if (rc) {
		pr_err("failed to set irq handler\n");
		return rc;
	}

	riscv_set_intc_hwnode_fn(riscv_intc_hwnode);

	pr_info("%d local interrupts mapped%s\n",
		riscv_intc_nr_irqs,
		riscv_isa_extension_available(NULL, SxAIA) ? " using AIA" : "");
	if (riscv_intc_custom_nr_irqs)
		pr_info("%d custom local interrupts mapped\n", riscv_intc_custom_nr_irqs);

	return 0;
}

static int __init riscv_intc_init(struct device_node *node,
				  struct device_node *parent)
{
	struct irq_chip *chip = &riscv_intc_chip;
	unsigned long hartid;
	int rc;

	rc = riscv_of_parent_hartid(node, &hartid);
	if (rc < 0) {
		pr_warn("unable to find hart id for %pOF\n", node);
		return 0;
	}

	/*
	 * The DT will have one INTC DT node under each CPU (or HART)
	 * DT node so riscv_intc_init() function will be called once
	 * for each INTC DT node. We only need to do INTC initialization
	 * for the INTC DT node belonging to boot CPU (or boot HART).
	 */
	if (riscv_hartid_to_cpuid(hartid) != smp_processor_id()) {
		/*
		 * The INTC nodes of each CPU are suppliers for downstream
		 * interrupt controllers (such as PLIC, IMSIC and APLIC
		 * direct-mode) so we should mark an INTC node as initialized
		 * if we are not creating IRQ domain for it.
		 */
		fwnode_dev_initialized(of_fwnode_handle(node), true);
		return 0;
	}

	if (of_device_is_compatible(node, "andestech,cpu-intc")) {
		riscv_intc_custom_base = ANDES_SLI_CAUSE_BASE;
		riscv_intc_custom_nr_irqs = ANDES_RV_IRQ_LAST;
		chip = &andes_intc_chip;
	}

	return riscv_intc_init_common(of_node_to_fwnode(node), chip);
}

IRQCHIP_DECLARE(riscv, "riscv,cpu-intc", riscv_intc_init);
IRQCHIP_DECLARE(andes, "andestech,cpu-intc", riscv_intc_init);

#ifdef CONFIG_ACPI

static int __init riscv_intc_acpi_init(union acpi_subtable_headers *header,
				       const unsigned long end)
{
	struct fwnode_handle *fn;
	struct acpi_madt_rintc *rintc;

	rintc = (struct acpi_madt_rintc *)header;

	/*
	 * The ACPI MADT will have one INTC for each CPU (or HART)
	 * so riscv_intc_acpi_init() function will be called once
	 * for each INTC. We only do INTC initialization
	 * for the INTC belonging to the boot CPU (or boot HART).
	 */
	if (riscv_hartid_to_cpuid(rintc->hart_id) != smp_processor_id())
		return 0;

	fn = irq_domain_alloc_named_fwnode("RISCV-INTC");
	if (!fn) {
		pr_err("unable to allocate INTC FW node\n");
		return -ENOMEM;
	}

	return riscv_intc_init_common(fn, &riscv_intc_chip);
}

IRQCHIP_ACPI_DECLARE(riscv_intc, ACPI_MADT_TYPE_RINTC, NULL,
		     ACPI_MADT_RINTC_VERSION_V1, riscv_intc_acpi_init);
#endif
