#ifndef _ASM_X86_IRQ_REMAPPING_H
#define _ASM_X86_IRQ_REMAPPING_H

#define IRTE_DEST(dest) ((x2apic_mode) ? dest : dest << 8)

#ifdef CONFIG_INTR_REMAP
static inline void prepare_irte(struct irte *irte, int vector,
			        unsigned int dest)
{
	memset(irte, 0, sizeof(*irte));

	irte->present = 1;
	irte->dst_mode = apic->irq_dest_mode;
	/*
	 * Trigger mode in the IRTE will always be edge, and for IO-APIC, the
	 * actual level or edge trigger will be setup in the IO-APIC
	 * RTE. This will help simplify level triggered irq migration.
	 * For more details, see the comments (in io_apic.c) explainig IO-APIC
	 * irq migration in the presence of interrupt-remapping.
	*/
	irte->trigger_mode = 0;
	irte->dlvry_mode = apic->irq_delivery_mode;
	irte->vector = vector;
	irte->dest_id = IRTE_DEST(dest);
	irte->redir_hint = 1;
}
static inline bool irq_remapped(struct irq_cfg *cfg)
{
	return cfg->irq_2_iommu.iommu != NULL;
}
#else
static void prepare_irte(struct irte *irte, int vector, unsigned int dest)
{
}
static inline bool irq_remapped(struct irq_cfg *cfg)
{
	return false;
}
#endif

#endif	/* _ASM_X86_IRQ_REMAPPING_H */
