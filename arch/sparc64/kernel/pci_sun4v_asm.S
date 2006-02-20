/* pci_sun4v_asm: Hypervisor calls for PCI support.
 *
 * Copyright (C) 2006 David S. Miller <davem@davemloft.net>
 */

#include <asm/hypervisor.h>

	/* %o0: devhandle
	 * %o1:	tsbid
	 * %o2:	num ttes
	 * %o3:	io_attributes
	 * %o4:	io_page_list phys address
	 *
	 * returns %o0:	-status if status was non-zero, else
	 *         %o0:	num pages mapped
	 */
	.globl	pci_sun4v_iommu_map
pci_sun4v_iommu_map:
	mov	%o5, %g1
	mov	HV_FAST_PCI_IOMMU_MAP, %o5
	ta	HV_FAST_TRAP
	brnz,pn %o0, 1f
	 sub	%g0, %o0, %o0
	mov	%o1, %o0
1:	retl
	 nop

	/* %o0: devhandle
	 * %o1:	tsbid
	 * %o2:	num ttes
	 *
	 * returns %o0:	num ttes demapped
	 */
	.globl	pci_sun4v_iommu_demap
pci_sun4v_iommu_demap:
	mov	HV_FAST_PCI_IOMMU_DEMAP, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o1, %o0

	/* %o0: devhandle
	 * %o1:	tsbid
	 * %o2:	&io_attributes
	 * %o3:	&real_address
	 *
	 * returns %o0:	status
	 */
	.globl	pci_sun4v_iommu_getmap
pci_sun4v_iommu_getmap:
	mov	%o2, %o4
	mov	HV_FAST_PCI_IOMMU_GETMAP, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	stx	%o2, [%o3]
	retl
	 mov	%o0, %o0

	/* %o0: devhandle
	 * %o1:	pci_device
	 * %o2:	pci_config_offset
	 * %o3:	size
	 *
	 * returns %o0:	data
	 *
	 * If there is an error, the data will be returned
	 * as all 1's.
	 */
	.globl	pci_sun4v_config_get
pci_sun4v_config_get:
	mov	HV_FAST_PCI_CONFIG_GET, %o5
	ta	HV_FAST_TRAP
	brnz,a,pn %o1, 1f
	 mov	-1, %o2
1:	retl
	 mov	%o2, %o0

	/* %o0: devhandle
	 * %o1:	pci_device
	 * %o2:	pci_config_offset
	 * %o3:	size
	 * %o4:	data
	 *
	 * returns %o0:	status
	 *
	 * status will be zero if the operation completed
	 * successfully, else -1 if not
	 */
	.globl	pci_sun4v_config_put
pci_sun4v_config_put:
	mov	HV_FAST_PCI_CONFIG_PUT, %o5
	ta	HV_FAST_TRAP
	brnz,a,pn %o1, 1f
	 mov	-1, %o1
1:	retl
	 mov	%o1, %o0
