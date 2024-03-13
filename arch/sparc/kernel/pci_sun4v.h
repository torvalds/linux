/* SPDX-License-Identifier: GPL-2.0 */
/* pci_sun4v.h: SUN4V specific PCI controller support.
 *
 * Copyright (C) 2006 David S. Miller (davem@davemloft.net)
 */

#ifndef _PCI_SUN4V_H
#define _PCI_SUN4V_H

long pci_sun4v_iommu_map(unsigned long devhandle,
			 unsigned long tsbid,
			 unsigned long num_ttes,
			 unsigned long io_attributes,
			 unsigned long io_page_list_pa);
unsigned long pci_sun4v_iommu_demap(unsigned long devhandle,
				    unsigned long tsbid,
				    unsigned long num_ttes);
unsigned long pci_sun4v_iommu_getmap(unsigned long devhandle,
				     unsigned long tsbid,
				     unsigned long *io_attributes,
				     unsigned long *real_address);
unsigned long pci_sun4v_config_get(unsigned long devhandle,
				   unsigned long pci_device,
				   unsigned long config_offset,
				   unsigned long size);
int pci_sun4v_config_put(unsigned long devhandle,
			 unsigned long pci_device,
			 unsigned long config_offset,
			 unsigned long size,
			 unsigned long data);

unsigned long pci_sun4v_msiq_conf(unsigned long devhandle,
					 unsigned long msiqid,
					 unsigned long msiq_paddr,
					 unsigned long num_entries);
unsigned long pci_sun4v_msiq_info(unsigned long devhandle,
				  unsigned long msiqid,
				  unsigned long *msiq_paddr,
				  unsigned long *num_entries);
unsigned long pci_sun4v_msiq_getvalid(unsigned long devhandle,
				      unsigned long msiqid,
				      unsigned long *valid);
unsigned long pci_sun4v_msiq_setvalid(unsigned long devhandle,
				      unsigned long msiqid,
				      unsigned long valid);
unsigned long pci_sun4v_msiq_getstate(unsigned long devhandle,
				      unsigned long msiqid,
				      unsigned long *state);
unsigned long pci_sun4v_msiq_setstate(unsigned long devhandle,
				      unsigned long msiqid,
				      unsigned long state);
unsigned long pci_sun4v_msiq_gethead(unsigned long devhandle,
				     unsigned long msiqid,
				     unsigned long *head);
unsigned long pci_sun4v_msiq_sethead(unsigned long devhandle,
				     unsigned long msiqid,
				     unsigned long head);
unsigned long pci_sun4v_msiq_gettail(unsigned long devhandle,
				      unsigned long msiqid,
				      unsigned long *head);
unsigned long pci_sun4v_msi_getvalid(unsigned long devhandle,
				     unsigned long msinum,
				     unsigned long *valid);
unsigned long pci_sun4v_msi_setvalid(unsigned long devhandle,
				     unsigned long msinum,
				     unsigned long valid);
unsigned long pci_sun4v_msi_getmsiq(unsigned long devhandle,
				    unsigned long msinum,
				    unsigned long *msiq);
unsigned long pci_sun4v_msi_setmsiq(unsigned long devhandle,
				    unsigned long msinum,
				    unsigned long msiq,
				    unsigned long msitype);
unsigned long pci_sun4v_msi_getstate(unsigned long devhandle,
				     unsigned long msinum,
				     unsigned long *state);
unsigned long pci_sun4v_msi_setstate(unsigned long devhandle,
				     unsigned long msinum,
				     unsigned long state);
unsigned long pci_sun4v_msg_getmsiq(unsigned long devhandle,
				    unsigned long msinum,
				    unsigned long *msiq);
unsigned long pci_sun4v_msg_setmsiq(unsigned long devhandle,
				    unsigned long msinum,
				    unsigned long msiq);
unsigned long pci_sun4v_msg_getvalid(unsigned long devhandle,
				     unsigned long msinum,
				     unsigned long *valid);
unsigned long pci_sun4v_msg_setvalid(unsigned long devhandle,
				     unsigned long msinum,
				     unsigned long valid);

/* Sun4v HV IOMMU v2 APIs */
unsigned long pci_sun4v_iotsb_conf(unsigned long devhandle,
				   unsigned long ra,
				   unsigned long table_size,
				   unsigned long page_size,
				   unsigned long dvma_base,
				   u64 *iotsb_num);
unsigned long pci_sun4v_iotsb_bind(unsigned long devhandle,
				   unsigned long iotsb_num,
				   unsigned int pci_device);
unsigned long pci_sun4v_iotsb_map(unsigned long devhandle,
				  unsigned long iotsb_num,
				  unsigned long iotsb_index_iottes,
				  unsigned long io_attributes,
				  unsigned long io_page_list_pa,
				  long *mapped);
unsigned long pci_sun4v_iotsb_demap(unsigned long devhandle,
				    unsigned long iotsb_num,
				    unsigned long iotsb_index,
				    unsigned long iottes,
				    unsigned long *demapped);
#endif /* !(_PCI_SUN4V_H) */
