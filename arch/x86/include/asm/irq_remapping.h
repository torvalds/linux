#ifndef _ASM_X86_IRQ_REMAPPING_H
#define _ASM_X86_IRQ_REMAPPING_H

#define IRTE_DEST(dest) ((x2apic_mode) ? dest : dest << 8)

#ifdef CONFIG_IRQ_REMAP
static void irq_remap_modify_chip_defaults(struct irq_chip *chip);
static inline bool irq_remapped(struct irq_cfg *cfg)
{
	return cfg->irq_2_iommu.iommu != NULL;
}
#else
static inline bool irq_remapped(struct irq_cfg *cfg)
{
	return false;
}
static inline void irq_remap_modify_chip_defaults(struct irq_chip *chip)
{
}
#endif

#endif	/* _ASM_X86_IRQ_REMAPPING_H */
