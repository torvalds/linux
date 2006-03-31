/* pci_sun4v.h: SUN4V specific PCI controller support.
 *
 * Copyright (C) 2006 David S. Miller (davem@davemloft.net)
 */

#ifndef _PCI_SUN4V_H
#define _PCI_SUN4V_H

extern long pci_sun4v_iommu_map(unsigned long devhandle,
				unsigned long tsbid,
				unsigned long num_ttes,
				unsigned long io_attributes,
				unsigned long io_page_list_pa);
extern unsigned long pci_sun4v_iommu_demap(unsigned long devhandle,
					   unsigned long tsbid,
					   unsigned long num_ttes);
extern unsigned long pci_sun4v_iommu_getmap(unsigned long devhandle,
					    unsigned long tsbid,
					    unsigned long *io_attributes,
					    unsigned long *real_address);
extern unsigned long pci_sun4v_config_get(unsigned long devhandle,
					  unsigned long pci_device,
					  unsigned long config_offset,
					  unsigned long size);
extern int pci_sun4v_config_put(unsigned long devhandle,
				unsigned long pci_device,
				unsigned long config_offset,
				unsigned long size,
				unsigned long data);

#endif /* !(_PCI_SUN4V_H) */
