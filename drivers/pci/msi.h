/*
 * Copyright (C) 2003-2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 */

#ifndef MSI_H
#define MSI_H

/*
 * MSI operation vector.  Used by the msi core code (drivers/pci/msi.c)
 * to abstract platform-specific tasks relating to MSI address generation
 * and resource management.
 */
struct msi_ops {
	/**
	 * setup - generate an MSI bus address and data for a given vector
	 * @pdev: PCI device context (in)
	 * @vector: vector allocated by the msi core (in)
	 * @addr_hi: upper 32 bits of PCI bus MSI address (out)
	 * @addr_lo: lower 32 bits of PCI bus MSI address (out)
	 * @data: MSI data payload (out)
	 *
	 * Description: The setup op is used to generate a PCI bus addres and
	 * data which the msi core will program into the card MSI capability
	 * registers.  The setup routine is responsible for picking an initial
	 * cpu to target the MSI at.  The setup routine is responsible for
	 * examining pdev to determine the MSI capabilities of the card and
	 * generating a suitable address/data.  The setup routine is
	 * responsible for allocating and tracking any system resources it
	 * needs to route the MSI to the cpu it picks, and for associating
	 * those resources with the passed in vector.
	 *
	 * Returns 0 if the MSI address/data was successfully setup.
	 **/

	int	(*setup)    (struct pci_dev *pdev, unsigned int vector,
			     u32 *addr_hi, u32 *addr_lo, u32 *data);

	/**
	 * teardown - release resources allocated by setup
	 * @vector: vector context for resources (in)
	 *
	 * Description:  The teardown op is used to release any resources
	 * that were allocated in the setup routine associated with the passed
	 * in vector.
	 **/

	void	(*teardown) (unsigned int vector);

	/**
	 * target - retarget an MSI at a different cpu
	 * @vector: vector context for resources (in)
	 * @cpu:  new cpu to direct vector at (in)
	 * @addr_hi: new value of PCI bus upper 32 bits (in/out)
	 * @addr_lo: new value of PCI bus lower 32 bits (in/out)
	 *
	 * Description:  The target op is used to redirect an MSI vector
	 * at a different cpu.  addr_hi/addr_lo coming in are the existing
	 * values that the MSI core has programmed into the card.  The
	 * target code is responsible for freeing any resources (if any)
	 * associated with the old address, and generating a new PCI bus
	 * addr_hi/addr_lo that will redirect the vector at the indicated cpu.
	 **/

	void	(*target)   (unsigned int vector, unsigned int cpu,
			     u32 *addr_hi, u32 *addr_lo);
};

extern int msi_register(struct msi_ops *ops);

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

struct msi_desc {
	struct {
		__u8	type	: 5; 	/* {0: unused, 5h:MSI, 11h:MSI-X} */
		__u8	maskbit	: 1; 	/* mask-pending bit supported ?   */
		__u8	state	: 1; 	/* {0: free, 1: busy}		  */
		__u8	reserved: 1; 	/* reserved			  */
		__u8	entry_nr;    	/* specific enabled entry 	  */
		__u8	default_vector; /* default pre-assigned vector    */
		__u8	unused; 	/* formerly unused destination cpu*/
	}msi_attrib;

	struct {
		__u16	head;
		__u16	tail;
	}link;

	void __iomem *mask_base;
	struct pci_dev *dev;

#ifdef CONFIG_PM
	/* PM save area for MSIX address/data */

	u32	address_hi_save;
	u32	address_lo_save;
	u32	data_save;
#endif
};

#endif /* MSI_H */
