extern void __cmx270_pci_init_irq(int irq_gpio);
extern void __cmx270_pci_suspend(void);
extern void __cmx270_pci_resume(void);

#ifdef CONFIG_PCI
#define cmx270_pci_init_irq(x) __cmx270_pci_init_irq(x)
#define cmx270_pci_suspend(x) __cmx270_pci_suspend(x)
#define cmx270_pci_resume(x) __cmx270_pci_resume(x)
#else
#define cmx270_pci_init_irq(x) do {} while (0)
#define cmx270_pci_suspend(x) do {} while (0)
#define cmx270_pci_resume(x) do {} while (0)
#endif
