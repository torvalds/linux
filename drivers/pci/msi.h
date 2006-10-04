/*
 * Copyright (C) 2003-2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 */

#ifndef MSI_H
#define MSI_H

#include <asm/msi.h>

/*
 * MSI-X Address Register
 */
#define PCI_MSIX_FLAGS_QSIZE		0x7FF
#define PCI_MSIX_FLAGS_ENABLE		(1 << 15)
#define PCI_MSIX_FLAGS_BIRMASK		(7 << 0)
#define PCI_MSIX_FLAGS_BITMASK		(1 << 0)

#define PCI_MSIX_ENTRY_SIZE			16
#define  PCI_MSIX_ENTRY_LOWER_ADDR_OFFSET	0
#define  PCI_MSIX_ENTRY_UPPER_ADDR_OFFSET	4
#define  PCI_MSIX_ENTRY_DATA_OFFSET		8
#define  PCI_MSIX_ENTRY_VECTOR_CTRL_OFFSET	12

#define msi_control_reg(base)		(base + PCI_MSI_FLAGS)
#define msi_lower_address_reg(base)	(base + PCI_MSI_ADDRESS_LO)
#define msi_upper_address_reg(base)	(base + PCI_MSI_ADDRESS_HI)
#define msi_data_reg(base, is64bit)	\
	( (is64bit == 1) ? base+PCI_MSI_DATA_64 : base+PCI_MSI_DATA_32 )
#define msi_mask_bits_reg(base, is64bit) \
	( (is64bit == 1) ? base+PCI_MSI_MASK_BIT : base+PCI_MSI_MASK_BIT-4)
#define msi_disable(control)		control &= ~PCI_MSI_FLAGS_ENABLE
#define multi_msi_capable(control) \
	(1 << ((control & PCI_MSI_FLAGS_QMASK) >> 1))
#define multi_msi_enable(control, num) \
	control |= (((num >> 1) << 4) & PCI_MSI_FLAGS_QSIZE);
#define is_64bit_address(control)	(!!(control & PCI_MSI_FLAGS_64BIT))
#define is_mask_bit_support(control)	(!!(control & PCI_MSI_FLAGS_MASKBIT))
#define msi_enable(control, num) multi_msi_enable(control, num); \
	control |= PCI_MSI_FLAGS_ENABLE

#define msix_table_offset_reg(base)	(base + 0x04)
#define msix_pba_offset_reg(base)	(base + 0x08)
#define msix_enable(control)	 	control |= PCI_MSIX_FLAGS_ENABLE
#define msix_disable(control)	 	control &= ~PCI_MSIX_FLAGS_ENABLE
#define msix_table_size(control) 	((control & PCI_MSIX_FLAGS_QSIZE)+1)
#define multi_msix_capable		msix_table_size
#define msix_unmask(address)	 	(address & ~PCI_MSIX_FLAGS_BITMASK)
#define msix_mask(address)		(address | PCI_MSIX_FLAGS_BITMASK)
#define msix_is_pending(address) 	(address & PCI_MSIX_FLAGS_PENDMASK)

struct msi_desc {
	struct {
		__u8	type	: 5; 	/* {0: unused, 5h:MSI, 11h:MSI-X} */
		__u8	maskbit	: 1; 	/* mask-pending bit supported ?   */
		__u8	state	: 1; 	/* {0: free, 1: busy}		  */
		__u8	is_64	: 1;	/* Address size: 0=32bit 1=64bit  */
		__u8	pos;	 	/* Location of the msi capability */
		__u16	entry_nr;    	/* specific enabled entry 	  */
		unsigned default_irq;	/* default pre-assigned irq	  */
	}msi_attrib;

	struct {
		__u16	head;
		__u16	tail;
	}link;

	void __iomem *mask_base;
	struct pci_dev *dev;

#ifdef CONFIG_PM
	/* PM save area for MSIX address/data */
	struct msi_msg msg_save;
#endif
};

#endif /* MSI_H */
