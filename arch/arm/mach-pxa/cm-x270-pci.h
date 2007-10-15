extern void __cmx270_pci_init_irq(void);
extern void __cmx270_pci_suspend(void);
extern void __cmx270_pci_resume(void);

#ifdef CONFIG_PCI
#define cmx270_pci_init_irq __cmx270_pci_init_irq
#define cmx270_pci_suspend __cmx270_pci_suspend
#define cmx270_pci_resume __cmx270_pci_resume
#else
#define cmx270_pci_init_irq() do {} while (0)
#define cmx270_pci_suspend() do {} while (0)
#define cmx270_pci_resume() do {} while (0)
#endif
