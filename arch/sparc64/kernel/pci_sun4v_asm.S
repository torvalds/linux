/* pci_sun4v_asm: Hypervisor calls for PCI support.
 *
 * Copyright (C) 2006 David S. Miller <davem@davemloft.net>
 */

#include <asm/hypervisor.h>

	/* %o0: devhandle
	 * %o1:	devino
	 *
	 * returns %o0: sysino
	 */
	.globl	pci_sun4v_devino_to_sysino
pci_sun4v_devino_to_sysino:
	mov	%o1, %o2
	mov	%o0, %o1
	mov	HV_FAST_INTR_DEVINO2SYSINO, %o0
	ta	HV_FAST_TRAP
	retl
	 mov	%o1, %o0

	/* %o0: devhandle
	 * %o1:	tsbid
	 * %o2:	num ttes
	 * %o3:	io_attributes
	 * %o4:	io_page_list phys address
	 *
	 * returns %o0:	num ttes mapped
	 */
	.globl	pci_sun4v_iommu_map
pci_sun4v_iommu_map:
	mov	%o4, %o5
	mov	%o3, %o4
	mov	%o2, %o3
	mov	%o1, %o2
	mov	%o0, %o1
	mov	HV_FAST_PCI_IOMMU_MAP, %o0
	ta	HV_FAST_TRAP
	retl
	 mov	%o1, %o0

	/* %o0: devhandle
	 * %o1:	tsbid
	 * %o2:	num ttes
	 *
	 * returns %o0:	num ttes demapped
	 */
	.globl	pci_sun4v_iommu_demap
pci_sun4v_iommu_demap:
	mov	%o2, %o3
	mov	%o1, %o2
	mov	%o0, %o1
	mov	HV_FAST_PCI_IOMMU_DEMAP, %o0
	ta	HV_FAST_TRAP
	retl
	 mov	%o1, %o0
