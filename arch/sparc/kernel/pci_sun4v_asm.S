/* pci_sun4v_asm: Hypervisor calls for PCI support.
 *
 * Copyright (C) 2006, 2008 David S. Miller <davem@davemloft.net>
 */

#include <linux/linkage.h>
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
ENTRY(pci_sun4v_iommu_map)
	mov	%o5, %g1
	mov	HV_FAST_PCI_IOMMU_MAP, %o5
	ta	HV_FAST_TRAP
	brnz,pn %o0, 1f
	 sub	%g0, %o0, %o0
	mov	%o1, %o0
1:	retl
	 nop
ENDPROC(pci_sun4v_iommu_map)

	/* %o0: devhandle
	 * %o1:	tsbid
	 * %o2:	num ttes
	 *
	 * returns %o0:	num ttes demapped
	 */
ENTRY(pci_sun4v_iommu_demap)
	mov	HV_FAST_PCI_IOMMU_DEMAP, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o1, %o0
ENDPROC(pci_sun4v_iommu_demap)

	/* %o0: devhandle
	 * %o1:	tsbid
	 * %o2:	&io_attributes
	 * %o3:	&real_address
	 *
	 * returns %o0:	status
	 */
ENTRY(pci_sun4v_iommu_getmap)
	mov	%o2, %o4
	mov	HV_FAST_PCI_IOMMU_GETMAP, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	stx	%o2, [%o3]
	retl
	 mov	%o0, %o0
ENDPROC(pci_sun4v_iommu_getmap)

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
ENTRY(pci_sun4v_config_get)
	mov	HV_FAST_PCI_CONFIG_GET, %o5
	ta	HV_FAST_TRAP
	brnz,a,pn %o1, 1f
	 mov	-1, %o2
1:	retl
	 mov	%o2, %o0
ENDPROC(pci_sun4v_config_get)

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
ENTRY(pci_sun4v_config_put)
	mov	HV_FAST_PCI_CONFIG_PUT, %o5
	ta	HV_FAST_TRAP
	brnz,a,pn %o1, 1f
	 mov	-1, %o1
1:	retl
	 mov	%o1, %o0
ENDPROC(pci_sun4v_config_put)

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
ENTRY(pci_sun4v_msiq_conf)
	mov	HV_FAST_PCI_MSIQ_CONF, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o0, %o0
ENDPROC(pci_sun4v_msiq_conf)

	/* %o0: devhandle
	 * %o1: msiqid
	 * %o2:	&msiq_phys_addr
	 * %o3:	&msiq_num_entries
	 *
	 * returns %o0: status
	 */
ENTRY(pci_sun4v_msiq_info)
	mov	%o2, %o4
	mov	HV_FAST_PCI_MSIQ_INFO, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	stx	%o2, [%o3]
	retl
	 mov	%o0, %o0
ENDPROC(pci_sun4v_msiq_info)

	/* %o0: devhandle
	 * %o1: msiqid
	 * %o2:	&valid
	 *
	 * returns %o0: status
	 */
ENTRY(pci_sun4v_msiq_getvalid)
	mov	HV_FAST_PCI_MSIQ_GETVALID, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o2]
	retl
	 mov	%o0, %o0
ENDPROC(pci_sun4v_msiq_getvalid)

	/* %o0: devhandle
	 * %o1: msiqid
	 * %o2:	valid
	 *
	 * returns %o0: status
	 */
ENTRY(pci_sun4v_msiq_setvalid)
	mov	HV_FAST_PCI_MSIQ_SETVALID, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o0, %o0
ENDPROC(pci_sun4v_msiq_setvalid)

	/* %o0: devhandle
	 * %o1: msiqid
	 * %o2:	&state
	 *
	 * returns %o0: status
	 */
ENTRY(pci_sun4v_msiq_getstate)
	mov	HV_FAST_PCI_MSIQ_GETSTATE, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o2]
	retl
	 mov	%o0, %o0
ENDPROC(pci_sun4v_msiq_getstate)

	/* %o0: devhandle
	 * %o1: msiqid
	 * %o2:	state
	 *
	 * returns %o0: status
	 */
ENTRY(pci_sun4v_msiq_setstate)
	mov	HV_FAST_PCI_MSIQ_SETSTATE, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o0, %o0
ENDPROC(pci_sun4v_msiq_setstate)

	/* %o0: devhandle
	 * %o1: msiqid
	 * %o2:	&head
	 *
	 * returns %o0: status
	 */
ENTRY(pci_sun4v_msiq_gethead)
	mov	HV_FAST_PCI_MSIQ_GETHEAD, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o2]
	retl
	 mov	%o0, %o0
ENDPROC(pci_sun4v_msiq_gethead)

	/* %o0: devhandle
	 * %o1: msiqid
	 * %o2:	head
	 *
	 * returns %o0: status
	 */
ENTRY(pci_sun4v_msiq_sethead)
	mov	HV_FAST_PCI_MSIQ_SETHEAD, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o0, %o0
ENDPROC(pci_sun4v_msiq_sethead)

	/* %o0: devhandle
	 * %o1: msiqid
	 * %o2:	&tail
	 *
	 * returns %o0: status
	 */
ENTRY(pci_sun4v_msiq_gettail)
	mov	HV_FAST_PCI_MSIQ_GETTAIL, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o2]
	retl
	 mov	%o0, %o0
ENDPROC(pci_sun4v_msiq_gettail)

	/* %o0: devhandle
	 * %o1: msinum
	 * %o2:	&valid
	 *
	 * returns %o0: status
	 */
ENTRY(pci_sun4v_msi_getvalid)
	mov	HV_FAST_PCI_MSI_GETVALID, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o2]
	retl
	 mov	%o0, %o0
ENDPROC(pci_sun4v_msi_getvalid)

	/* %o0: devhandle
	 * %o1: msinum
	 * %o2:	valid
	 *
	 * returns %o0: status
	 */
ENTRY(pci_sun4v_msi_setvalid)
	mov	HV_FAST_PCI_MSI_SETVALID, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o0, %o0
ENDPROC(pci_sun4v_msi_setvalid)

	/* %o0: devhandle
	 * %o1: msinum
	 * %o2:	&msiq
	 *
	 * returns %o0: status
	 */
ENTRY(pci_sun4v_msi_getmsiq)
	mov	HV_FAST_PCI_MSI_GETMSIQ, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o2]
	retl
	 mov	%o0, %o0
ENDPROC(pci_sun4v_msi_getmsiq)

	/* %o0: devhandle
	 * %o1: msinum
	 * %o2:	msitype
	 * %o3:	msiq
	 *
	 * returns %o0: status
	 */
ENTRY(pci_sun4v_msi_setmsiq)
	mov	HV_FAST_PCI_MSI_SETMSIQ, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o0, %o0
ENDPROC(pci_sun4v_msi_setmsiq)

	/* %o0: devhandle
	 * %o1: msinum
	 * %o2:	&state
	 *
	 * returns %o0: status
	 */
ENTRY(pci_sun4v_msi_getstate)
	mov	HV_FAST_PCI_MSI_GETSTATE, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o2]
	retl
	 mov	%o0, %o0
ENDPROC(pci_sun4v_msi_getstate)

	/* %o0: devhandle
	 * %o1: msinum
	 * %o2:	state
	 *
	 * returns %o0: status
	 */
ENTRY(pci_sun4v_msi_setstate)
	mov	HV_FAST_PCI_MSI_SETSTATE, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o0, %o0
ENDPROC(pci_sun4v_msi_setstate)

	/* %o0: devhandle
	 * %o1: msinum
	 * %o2:	&msiq
	 *
	 * returns %o0: status
	 */
ENTRY(pci_sun4v_msg_getmsiq)
	mov	HV_FAST_PCI_MSG_GETMSIQ, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o2]
	retl
	 mov	%o0, %o0
ENDPROC(pci_sun4v_msg_getmsiq)

	/* %o0: devhandle
	 * %o1: msinum
	 * %o2:	msiq
	 *
	 * returns %o0: status
	 */
ENTRY(pci_sun4v_msg_setmsiq)
	mov	HV_FAST_PCI_MSG_SETMSIQ, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o0, %o0
ENDPROC(pci_sun4v_msg_setmsiq)

	/* %o0: devhandle
	 * %o1: msinum
	 * %o2:	&valid
	 *
	 * returns %o0: status
	 */
ENTRY(pci_sun4v_msg_getvalid)
	mov	HV_FAST_PCI_MSG_GETVALID, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o2]
	retl
	 mov	%o0, %o0
ENDPROC(pci_sun4v_msg_getvalid)

	/* %o0: devhandle
	 * %o1: msinum
	 * %o2:	valid
	 *
	 * returns %o0: status
	 */
ENTRY(pci_sun4v_msg_setvalid)
	mov	HV_FAST_PCI_MSG_SETVALID, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o0, %o0
ENDPROC(pci_sun4v_msg_setvalid)

	/*
	 * %o0:	devhandle
	 * %o1:	r_addr
	 * %o2:	size
	 * %o3:	pagesize
	 * %o4:	virt
	 * %o5: &iotsb_num/&iotsb_handle
	 *
	 * returns %o0:	status
	 *         %o1:	iotsb_num/iotsb_handle
	 */
ENTRY(pci_sun4v_iotsb_conf)
	mov	%o5, %g1
	mov	HV_FAST_PCI_IOTSB_CONF, %o5
	ta	HV_FAST_TRAP
	retl
	 stx	%o1, [%g1]
ENDPROC(pci_sun4v_iotsb_conf)

	/*
	 * %o0:	devhandle
	 * %o1:	iotsb_num/iotsb_handle
	 * %o2:	pci_device
	 *
	 * returns %o0:	status
	 */
ENTRY(pci_sun4v_iotsb_bind)
	mov	HV_FAST_PCI_IOTSB_BIND, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(pci_sun4v_iotsb_bind)

	/*
	 * %o0:	devhandle
	 * %o1:	iotsb_num/iotsb_handle
	 * %o2:	index_count
	 * %o3:	iotte_attributes
	 * %o4:	io_page_list_p
	 * %o5: &mapped
	 *
	 * returns %o0:	status
	 *         %o1:	#mapped
	 */
ENTRY(pci_sun4v_iotsb_map)
	mov	%o5, %g1
	mov	HV_FAST_PCI_IOTSB_MAP, %o5
	ta	HV_FAST_TRAP
	retl
	 stx	%o1, [%g1]
ENDPROC(pci_sun4v_iotsb_map)

	/*
	 * %o0:	devhandle
	 * %o1:	iotsb_num/iotsb_handle
	 * %o2:	iotsb_index
	 * %o3:	#iottes
	 * %o4: &demapped
	 *
	 * returns %o0:	status
	 *         %o1:	#demapped
	 */
ENTRY(pci_sun4v_iotsb_demap)
	mov	HV_FAST_PCI_IOTSB_DEMAP, %o5
	ta	HV_FAST_TRAP
	retl
	 stx	%o1, [%o4]
ENDPROC(pci_sun4v_iotsb_demap)
