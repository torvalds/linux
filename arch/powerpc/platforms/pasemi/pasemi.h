#ifndef _PASEMI_PASEMI_H
#define _PASEMI_PASEMI_H

extern unsigned long pas_get_boot_time(void);
extern void pas_pci_init(void);
extern void __devinit pas_pci_irq_fixup(struct pci_dev *dev);
extern void __devinit pas_pci_dma_dev_setup(struct pci_dev *dev);

extern void __init alloc_iobmap_l2(void);

extern void __init pasemi_idle_init(void);

/* Power savings modes, implemented in asm */
extern void idle_spin(void);
extern void idle_doze(void);

/* Restore astate to last set */
#ifdef CONFIG_PPC_PASEMI_CPUFREQ
extern void restore_astate(int cpu);
#else
static inline void restore_astate(int cpu)
{
}
#endif


#endif /* _PASEMI_PASEMI_H */
