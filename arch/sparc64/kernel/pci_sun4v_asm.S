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

	/* %o0: devhandle
	 * %o1: msiqid
	 * %o2: msiq phys address
	 * %o3: num entries
	 *
	 * returns %o0: status
	 *
	 * status will be zero if the operation completed
	 * successfully, else -1 if not
	 */
	.globl	pci_sun4v_msiq_conf
pci_sun4v_msiq_conf:
	mov	HV_FAST_PCI_MSIQ_CONF, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o0, %o0

	/* %o0: devhandle
	 * %o1: msiqid
	 * %o2:	&msiq_phys_addr
	 * %o3:	&msiq_num_entries
	 *
	 * returns %o0: status
	 */
	.globl	pci_sun4v_msiq_info
pci_sun4v_msiq_info:
	mov	%o2, %o4
	mov	HV_FAST_PCI_MSIQ_INFO, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	stx	%o2, [%o3]
	retl
	 mov	%o0, %o0

	/* %o0: devhandle
	 * %o1: msiqid
	 * %o2:	&valid
	 *
	 * returns %o0: status
	 */
	.globl	pci_sun4v_msiq_getvalid
pci_sun4v_msiq_getvalid:
	mov	HV_FAST_PCI_MSIQ_GETVALID, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o2]
	retl
	 mov	%o0, %o0

	/* %o0: devhandle
	 * %o1: msiqid
	 * %o2:	valid
	 *
	 * returns %o0: status
	 */
	.globl	pci_sun4v_msiq_setvalid
pci_sun4v_msiq_setvalid:
	mov	HV_FAST_PCI_MSIQ_SETVALID, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o0, %o0

	/* %o0: devhandle
	 * %o1: msiqid
	 * %o2:	&state
	 *
	 * returns %o0: status
	 */
	.globl	pci_sun4v_msiq_getstate
pci_sun4v_msiq_getstate:
	mov	HV_FAST_PCI_MSIQ_GETSTATE, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o2]
	retl
	 mov	%o0, %o0

	/* %o0: devhandle
	 * %o1: msiqid
	 * %o2:	state
	 *
	 * returns %o0: status
	 */
	.globl	pci_sun4v_msiq_setstate
pci_sun4v_msiq_setstate:
	mov	HV_FAST_PCI_MSIQ_SETSTATE, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o0, %o0

	/* %o0: devhandle
	 * %o1: msiqid
	 * %o2:	&head
	 *
	 * returns %o0: status
	 */
	.globl	pci_sun4v_msiq_gethead
pci_sun4v_msiq_gethead:
	mov	HV_FAST_PCI_MSIQ_GETHEAD, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o2]
	retl
	 mov	%o0, %o0

	/* %o0: devhandle
	 * %o1: msiqid
	 * %o2:	head
	 *
	 * returns %o0: status
	 */
	.globl	pci_sun4v_msiq_sethead
pci_sun4v_msiq_sethead:
	mov	HV_FAST_PCI_MSIQ_SETHEAD, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o0, %o0

	/* %o0: devhandle
	 * %o1: msiqid
	 * %o2:	&tail
	 *
	 * returns %o0: status
	 */
	.globl	pci_sun4v_msiq_gettail
pci_sun4v_msiq_gettail:
	mov	HV_FAST_PCI_MSIQ_GETTAIL, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o2]
	retl
	 mov	%o0, %o0

	/* %o0: devhandle
	 * %o1: msinum
	 * %o2:	&valid
	 *
	 * returns %o0: status
	 */
	.globl	pci_sun4v_msi_getvalid
pci_sun4v_msi_getvalid:
	mov	HV_FAST_PCI_MSI_GETVALID, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o2]
	retl
	 mov	%o0, %o0

	/* %o0: devhandle
	 * %o1: msinum
	 * %o2:	valid
	 *
	 * returns %o0: status
	 */
	.globl	pci_sun4v_msi_setvalid
pci_sun4v_msi_setvalid:
	mov	HV_FAST_PCI_MSI_SETVALID, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o0, %o0

	/* %o0: devhandle
	 * %o1: msinum
	 * %o2:	&msiq
	 *
	 * returns %o0: status
	 */
	.globl	pci_sun4v_msi_getmsiq
pci_sun4v_msi_getmsiq:
	mov	HV_FAST_PCI_MSI_GETMSIQ, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o2]
	retl
	 mov	%o0, %o0

	/* %o0: devhandle
	 * %o1: msinum
	 * %o2:	msitype
	 * %o3:	msiq
	 *
	 * returns %o0: status
	 */
	.globl	pci_sun4v_msi_setmsiq
pci_sun4v_msi_setmsiq:
	mov	HV_FAST_PCI_MSI_SETMSIQ, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o0, %o0

	/* %o0: devhandle
	 * %o1: msinum
	 * %o2:	&state
	 *
	 * returns %o0: status
	 */
	.globl	pci_sun4v_msi_getstate
pci_sun4v_msi_getstate:
	mov	HV_FAST_PCI_MSI_GETSTATE, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o2]
	retl
	 mov	%o0, %o0

	/* %o0: devhandle
	 * %o1: msinum
	 * %o2:	state
	 *
	 * returns %o0: status
	 */
	.globl	pci_sun4v_msi_setstate
pci_sun4v_msi_setstate:
	mov	HV_FAST_PCI_MSI_SETSTATE, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o0, %o0

	/* %o0: devhandle
	 * %o1: msinum
	 * %o2:	&msiq
	 *
	 * returns %o0: status
	 */
	.globl	pci_sun4v_msg_getmsiq
pci_sun4v_msg_getmsiq:
	mov	HV_FAST_PCI_MSG_GETMSIQ, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o2]
	retl
	 mov	%o0, %o0

	/* %o0: devhandle
	 * %o1: msinum
	 * %o2:	msiq
	 *
	 * returns %o0: status
	 */
	.globl	pci_sun4v_msg_setmsiq
pci_sun4v_msg_setmsiq:
	mov	HV_FAST_PCI_MSG_SETMSIQ, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o0, %o0

	/* %o0: devhandle
	 * %o1: msinum
	 * %o2:	&valid
	 *
	 * returns %o0: status
	 */
	.globl	pci_sun4v_msg_getvalid
pci_sun4v_msg_getvalid:
	mov	HV_FAST_PCI_MSG_GETVALID, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o2]
	retl
	 mov	%o0, %o0

	/* %o0: devhandle
	 * %o1: msinum
	 * %o2:	valid
	 *
	 * returns %o0: status
	 */
	.globl	pci_sun4v_msg_setvalid
pci_sun4v_msg_setvalid:
	mov	HV_FAST_PCI_MSG_SETVALID, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o0, %o0

