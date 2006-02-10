/* pci_sun4v.h: SUN4V specific PCI controller support.
 *
 * Copyright (C) 2006 David S. Miller (davem@davemloft.net)
 */

#ifndef _PCI_SUN4V_H
#define _PCI_SUN4V_H

extern unsigned long pci_sun4v_devino_to_sysino(unsigned long devhandle,
						unsigned long deino);
extern unsigned long pci_sun4v_iommu_map(unsigned long devhandle,
					 unsigned long tsbid,
					 unsigned long num_ttes,
					 unsigned long io_attributes,
					 unsigned long io_page_list_pa);
extern unsigned long pci_sun4v_iommu_demap(unsigned long devhandle,
					   unsigned long tsbid,
					   unsigned long num_ttes);

#endif /* !(_PCI_SUN4V_H) */
