/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef _ASM_PCI_H
#define _ASM_PCI_H

#include <linux/mm.h>

#ifdef __KERNEL__

/*
 * This file essentially defines the interface between board
 * specific PCI code and MIPS common PCI code.	Should potentially put
 * into include/asm/pci.h file.
 */

#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/of.h>

#ifdef CONFIG_PCI_DRIVERS_LEGACY

/*
 * Each pci channel is a top-level PCI bus seem by CPU.	 A machine  with
 * multiple PCI channels may have multiple PCI host controllers or a
 * single controller supporting multiple channels.
 */
struct pci_controller {
	struct list_head list;
	struct pci_bus *bus;
	struct device_node *of_node;

	struct pci_ops *pci_ops;
	struct resource *mem_resource;
	unsigned long mem_offset;
	struct resource *io_resource;
	unsigned long io_offset;
	unsigned long io_map_base;
	struct resource *busn_resource;

#ifndef CONFIG_PCI_DOMAINS_GENERIC
	unsigned int index;
	/* For compatibility with current (as of July 2003) pciutils
	   and XFree86. Eventually will be removed. */
	unsigned int need_domain_info;
#endif

	/* Optional access methods for reading/writing the bus number
	   of the PCI controller */
	int (*get_busno)(void);
	void (*set_busno)(int busno);
};

/*
 * Used by boards to register their PCI busses before the actual scanning.
 */
extern void register_pci_controller(struct pci_controller *hose);

/*
 * board supplied pci irq fixup routine
 */
extern int pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin);

/* Do platform specific device initialization at pci_enable_device() time */
extern int pcibios_plat_dev_init(struct pci_dev *dev);

extern char * (*pcibios_plat_setup)(char *str);

#ifdef CONFIG_OF
/* this function parses memory ranges from a device node */
extern void pci_load_of_ranges(struct pci_controller *hose,
			       struct device_node *node);
#else
static inline void pci_load_of_ranges(struct pci_controller *hose,
				      struct device_node *node) {}
#endif

#ifdef CONFIG_PCI_DOMAINS_GENERIC
static inline void set_pci_need_domain_info(struct pci_controller *hose,
					    int need_domain_info)
{
	/* nothing to do */
}
#elif defined(CONFIG_PCI_DOMAINS)
static inline void set_pci_need_domain_info(struct pci_controller *hose,
					    int need_domain_info)
{
	hose->need_domain_info = need_domain_info;
}
#endif /* CONFIG_PCI_DOMAINS */

#endif

/* Can be used to override the logic in pci_scan_bus for skipping
   already-configured bus numbers - to be used for buggy BIOSes
   or architectures with incomplete PCI setup by the loader */
static inline unsigned int pcibios_assign_all_busses(void)
{
	return 1;
}

extern unsigned long PCIBIOS_MIN_IO;
extern unsigned long PCIBIOS_MIN_MEM;

#define PCIBIOS_MIN_CARDBUS_IO	0x4000

extern void pcibios_set_master(struct pci_dev *dev);

#define HAVE_PCI_MMAP
#define ARCH_GENERIC_PCI_MMAP_RESOURCE
#define HAVE_ARCH_PCI_RESOURCE_TO_USER

/*
 * Dynamic DMA mapping stuff.
 * MIPS has everything mapped statically.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/string.h>
#include <asm/io.h>

struct pci_dev;

/*
 * The PCI address space does equal the physical memory address space.
 * The networking and block device layers use this boolean for bounce
 * buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS     (1)

#ifdef CONFIG_PCI_DOMAINS_GENERIC
static inline int pci_proc_domain(struct pci_bus *bus)
{
	return pci_domain_nr(bus);
}
#elif defined(CONFIG_PCI_DOMAINS)
#define pci_domain_nr(bus) ((struct pci_controller *)(bus)->sysdata)->index

static inline int pci_proc_domain(struct pci_bus *bus)
{
	struct pci_controller *hose = bus->sysdata;
	return hose->need_domain_info;
}
#endif /* CONFIG_PCI_DOMAINS */

#endif /* __KERNEL__ */

/* Do platform specific device initialization at pci_enable_device() time */
extern int pcibios_plat_dev_init(struct pci_dev *dev);

/* Chances are this interrupt is wired PC-style ...  */
static inline int pci_get_legacy_ide_irq(struct pci_dev *dev, int channel)
{
	return channel ? 15 : 14;
}

#endif /* _ASM_PCI_H */
