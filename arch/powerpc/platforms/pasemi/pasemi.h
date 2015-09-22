#ifndef _PASEMI_PASEMI_H
#define _PASEMI_PASEMI_H

extern unsigned long pas_get_boot_time(void);
extern void pas_pci_init(void);
extern void pas_pci_irq_fixup(struct pci_dev *dev);
extern void pas_pci_dma_dev_setup(struct pci_dev *dev);

extern void __iomem *pasemi_pci_getcfgaddr(struct pci_dev *dev, int offset);

extern void __init alloc_iobmap_l2(void);
extern void __init pasemi_map_registers(void);

/* Power savings modes, implemented in asm */
extern void idle_spin(void);
extern void idle_doze(void);

/* Restore astate to last set */
#ifdef CONFIG_PPC_PASEMI_CPUFREQ
extern int check_astate(void);
extern void restore_astate(int cpu);
#else
static inline int check_astate(void)
{
	/* Always return >0 so we never power save */
	return 1;
}
static inline void restore_astate(int cpu)
{
}
#endif

extern struct pci_controller_ops pasemi_pci_controller_ops;

#endif /* _PASEMI_PASEMI_H */
