/*
 * c 2001 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_POWERPC_PPC_PCI_H
#define _ASM_POWERPC_PPC_PCI_H
#ifdef __KERNEL__

#ifdef CONFIG_PCI

#include <linux/pci.h>
#include <asm/pci-bridge.h>

extern unsigned long isa_io_base;

extern void pci_setup_phb_io(struct pci_controller *hose, int primary);
extern void pci_setup_phb_io_dynamic(struct pci_controller *hose, int primary);


extern struct list_head hose_list;

extern void find_and_init_phbs(void);

extern struct pci_dev *isa_bridge_pcidev;	/* may be NULL if no ISA bus */

/** Bus Unit ID macros; get low and hi 32-bits of the 64-bit BUID */
#define BUID_HI(buid) upper_32_bits(buid)
#define BUID_LO(buid) lower_32_bits(buid)

/* PCI device_node operations */
struct device_node;
typedef void *(*traverse_func)(struct device_node *me, void *data);
void *traverse_pci_devices(struct device_node *start, traverse_func pre,
		void *data);

extern void pci_devs_phb_init(void);
extern void pci_devs_phb_init_dynamic(struct pci_controller *phb);

/* From rtas_pci.h */
extern void init_pci_config_tokens (void);
extern unsigned long get_phb_buid (struct device_node *);
extern int rtas_setup_phb(struct pci_controller *phb);

#ifdef CONFIG_EEH

void pci_addr_cache_build(void);
void pci_addr_cache_insert_device(struct pci_dev *dev);
void pci_addr_cache_remove_device(struct pci_dev *dev);
struct pci_dev *pci_addr_cache_get_device(unsigned long addr);
void eeh_slot_error_detail(struct eeh_dev *edev, int severity);
int eeh_pci_enable(struct eeh_dev *edev, int function);
int eeh_reset_pe(struct eeh_dev *);
void eeh_restore_bars(struct eeh_dev *);
int rtas_write_config(struct pci_dn *, int where, int size, u32 val);
int rtas_read_config(struct pci_dn *, int where, int size, u32 *val);
void eeh_mark_slot(struct device_node *dn, int mode_flag);
void eeh_clear_slot(struct device_node *dn, int mode_flag);
struct device_node *eeh_find_device_pe(struct device_node *dn);

void eeh_sysfs_add_device(struct pci_dev *pdev);
void eeh_sysfs_remove_device(struct pci_dev *pdev);

static inline const char *eeh_pci_name(struct pci_dev *pdev) 
{ 
	return pdev ? pci_name(pdev) : "<null>";
} 

static inline const char *eeh_driver_name(struct pci_dev *pdev)
{
	return (pdev && pdev->driver) ? pdev->driver->name : "<null>";
}

#endif /* CONFIG_EEH */

#else /* CONFIG_PCI */
static inline void find_and_init_phbs(void) { }
static inline void init_pci_config_tokens(void) { }
#endif /* !CONFIG_PCI */

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_PPC_PCI_H */
