/*
 * Copyright (C) 2003-2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 */

#ifndef MSI_H
#define MSI_H

#include <asm/msi.h>

/*
 * Assume the maximum number of hot plug slots supported by the system is about
 * ten. The worstcase is that each of these slots is hot-added with a device,
 * which has two MSI/MSI-X capable functions. To avoid any MSI-X driver, which
 * attempts to request all available vectors, NR_HP_RESERVED_VECTORS is defined
 * as below to ensure at least one message is assigned to each detected MSI/
 * MSI-X device function.
 */
#define NR_HP_RESERVED_VECTORS 	20

extern int vector_irq[NR_VECTORS];
extern void (*interrupt[NR_IRQS])(void);
extern int pci_vector_resources(int last, int nr_released);

#ifdef CONFIG_SMP
#define set_msi_irq_affinity	set_msi_affinity
#else
#define set_msi_irq_affinity	NULL
#endif

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
#define is_64bit_address(control)	(control & PCI_MSI_FLAGS_64BIT)
#define is_mask_bit_support(control)	(control & PCI_MSI_FLAGS_MASKBIT)
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

/*
 * MSI Defined Data Structures
 */
#define MSI_ADDRESS_HEADER		0xfee
#define MSI_ADDRESS_HEADER_SHIFT	12
#define MSI_ADDRESS_HEADER_MASK		0xfff000
#define MSI_ADDRESS_DEST_ID_MASK	0xfff0000f
#define MSI_TARGET_CPU_MASK		0xff
#define MSI_DELIVERY_MODE		0
#define MSI_LEVEL_MODE			1	/* Edge always assert */
#define MSI_TRIGGER_MODE		0	/* MSI is edge sensitive */
#define MSI_PHYSICAL_MODE		0
#define MSI_LOGICAL_MODE		1
#define MSI_REDIRECTION_HINT_MODE	0

struct msg_data {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u32	vector		:  8;
	__u32	delivery_mode	:  3;	/* 000b: FIXED | 001b: lowest prior */
	__u32	reserved_1	:  3;
	__u32	level		:  1;	/* 0: deassert | 1: assert */
	__u32	trigger		:  1;	/* 0: edge | 1: level */
	__u32	reserved_2	: 16;
#elif defined(__BIG_ENDIAN_BITFIELD)
	__u32	reserved_2	: 16;
	__u32	trigger		:  1;	/* 0: edge | 1: level */
	__u32	level		:  1;	/* 0: deassert | 1: assert */
	__u32	reserved_1	:  3;
	__u32	delivery_mode	:  3;	/* 000b: FIXED | 001b: lowest prior */
	__u32	vector		:  8;
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
} __attribute__ ((packed));

struct msg_address {
	union {
		struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
			__u32	reserved_1	:  2;
			__u32	dest_mode	:  1;	/*0:physic | 1:logic */
			__u32	redirection_hint:  1;  	/*0: dedicated CPU
							  1: lowest priority */
			__u32	reserved_2	:  4;
 			__u32	dest_id		: 24;	/* Destination ID */
#elif defined(__BIG_ENDIAN_BITFIELD)
 			__u32	dest_id		: 24;	/* Destination ID */
			__u32	reserved_2	:  4;
			__u32	redirection_hint:  1;  	/*0: dedicated CPU
							  1: lowest priority */
			__u32	dest_mode	:  1;	/*0:physic | 1:logic */
			__u32	reserved_1	:  2;
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
      		}u;
       		__u32  value;
	}lo_address;
	__u32 	hi_address;
} __attribute__ ((packed));

struct msi_desc {
	struct {
		__u8	type	: 5; 	/* {0: unused, 5h:MSI, 11h:MSI-X} */
		__u8	maskbit	: 1; 	/* mask-pending bit supported ?   */
		__u8	state	: 1; 	/* {0: free, 1: busy}		  */
		__u8	reserved: 1; 	/* reserved			  */
		__u8	entry_nr;    	/* specific enabled entry 	  */
		__u8	default_vector; /* default pre-assigned vector    */
		__u8	current_cpu; 	/* current destination cpu	  */
	}msi_attrib;

	struct {
		__u16	head;
		__u16	tail;
	}link;

	void __iomem *mask_base;
	struct pci_dev *dev;
};

#endif /* MSI_H */
