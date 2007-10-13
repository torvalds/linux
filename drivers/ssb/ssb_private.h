#ifndef LINUX_SSB_PRIVATE_H_
#define LINUX_SSB_PRIVATE_H_

#include <linux/ssb/ssb.h>
#include <linux/types.h>


#define PFX	"ssb: "

#ifdef CONFIG_SSB_SILENT
# define ssb_printk(fmt, x...)	do { /* nothing */ } while (0)
#else
# define ssb_printk		printk
#endif /* CONFIG_SSB_SILENT */

/* dprintk: Debugging printk; vanishes for non-debug compilation */
#ifdef CONFIG_SSB_DEBUG
# define ssb_dprintk(fmt, x...)	ssb_printk(fmt , ##x)
#else
# define ssb_dprintk(fmt, x...)	do { /* nothing */ } while (0)
#endif

#ifdef CONFIG_SSB_DEBUG
# define SSB_WARN_ON(x)		WARN_ON(x)
# define SSB_BUG_ON(x)		BUG_ON(x)
#else
static inline int __ssb_do_nothing(int x) { return x; }
# define SSB_WARN_ON(x)		__ssb_do_nothing(unlikely(!!(x)))
# define SSB_BUG_ON(x)		__ssb_do_nothing(unlikely(!!(x)))
#endif


/* pci.c */
#ifdef CONFIG_SSB_PCIHOST
extern int ssb_pci_switch_core(struct ssb_bus *bus,
			       struct ssb_device *dev);
extern int ssb_pci_switch_coreidx(struct ssb_bus *bus,
				  u8 coreidx);
extern int ssb_pci_xtal(struct ssb_bus *bus, u32 what,
			int turn_on);
extern int ssb_pci_get_invariants(struct ssb_bus *bus,
				  struct ssb_init_invariants *iv);
extern void ssb_pci_exit(struct ssb_bus *bus);
extern int ssb_pci_init(struct ssb_bus *bus);
extern const struct ssb_bus_ops ssb_pci_ops;

#else /* CONFIG_SSB_PCIHOST */

static inline int ssb_pci_switch_core(struct ssb_bus *bus,
				      struct ssb_device *dev)
{
	return 0;
}
static inline int ssb_pci_switch_coreidx(struct ssb_bus *bus,
					 u8 coreidx)
{
	return 0;
}
static inline int ssb_pci_xtal(struct ssb_bus *bus, u32 what,
			       int turn_on)
{
	return 0;
}
static inline void ssb_pci_exit(struct ssb_bus *bus)
{
}
static inline int ssb_pci_init(struct ssb_bus *bus)
{
	return 0;
}
#endif /* CONFIG_SSB_PCIHOST */


/* pcmcia.c */
#ifdef CONFIG_SSB_PCMCIAHOST
extern int ssb_pcmcia_switch_core(struct ssb_bus *bus,
				  struct ssb_device *dev);
extern int ssb_pcmcia_switch_coreidx(struct ssb_bus *bus,
				     u8 coreidx);
extern int ssb_pcmcia_switch_segment(struct ssb_bus *bus,
				     u8 seg);
extern int ssb_pcmcia_get_invariants(struct ssb_bus *bus,
				     struct ssb_init_invariants *iv);
extern int ssb_pcmcia_init(struct ssb_bus *bus);
extern const struct ssb_bus_ops ssb_pcmcia_ops;
#else /* CONFIG_SSB_PCMCIAHOST */
static inline int ssb_pcmcia_switch_core(struct ssb_bus *bus,
					 struct ssb_device *dev)
{
	return 0;
}
static inline int ssb_pcmcia_switch_coreidx(struct ssb_bus *bus,
					    u8 coreidx)
{
	return 0;
}
static inline int ssb_pcmcia_switch_segment(struct ssb_bus *bus,
					    u8 seg)
{
	return 0;
}
static inline int ssb_pcmcia_init(struct ssb_bus *bus)
{
	return 0;
}
#endif /* CONFIG_SSB_PCMCIAHOST */


/* scan.c */
extern const char *ssb_core_name(u16 coreid);
extern int ssb_bus_scan(struct ssb_bus *bus,
			unsigned long baseaddr);
extern void ssb_iounmap(struct ssb_bus *ssb);


/* core.c */
extern u32 ssb_calc_clock_rate(u32 plltype, u32 n, u32 m);
extern int ssb_devices_freeze(struct ssb_bus *bus);
extern int ssb_devices_thaw(struct ssb_bus *bus);
extern struct ssb_bus *ssb_pci_dev_to_bus(struct pci_dev *pdev);

/* b43_pci_bridge.c */
#ifdef CONFIG_SSB_PCIHOST
extern int __init b43_pci_ssb_bridge_init(void);
extern void __exit b43_pci_ssb_bridge_exit(void);
#else /* CONFIG_SSB_PCIHOST */
static inline int b43_pci_ssb_bridge_init(void)
{
	return 0;
}
static inline void b43_pci_ssb_bridge_exit(void)
{
}
#endif /* CONFIG_SSB_PCIHOST */

#endif /* LINUX_SSB_PRIVATE_H_ */
