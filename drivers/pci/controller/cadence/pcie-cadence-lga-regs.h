/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cadence PCIe controller driver.
 *
 * Copyright (c) 2017 Cadence
 * Author: Cyrille Pitchen <cyrille.pitchen@free-electrons.com>
 */
#ifndef _PCIE_CADENCE_LGA_REGS_H
#define _PCIE_CADENCE_LGA_REGS_H

#include <linux/bitfield.h>

/* Parameters for the waiting for link up routine */
#define LINK_WAIT_MAX_RETRIES	10
#define LINK_WAIT_USLEEP_MIN	90000
#define LINK_WAIT_USLEEP_MAX	100000

/* Local Management Registers */
#define CDNS_PCIE_LM_BASE	0x00100000

/* Vendor ID Register */
#define CDNS_PCIE_LM_ID		(CDNS_PCIE_LM_BASE + 0x0044)
#define  CDNS_PCIE_LM_ID_VENDOR_MASK	GENMASK(15, 0)
#define  CDNS_PCIE_LM_ID_VENDOR_SHIFT	0
#define  CDNS_PCIE_LM_ID_VENDOR(vid) \
	(((vid) << CDNS_PCIE_LM_ID_VENDOR_SHIFT) & CDNS_PCIE_LM_ID_VENDOR_MASK)
#define  CDNS_PCIE_LM_ID_SUBSYS_MASK	GENMASK(31, 16)
#define  CDNS_PCIE_LM_ID_SUBSYS_SHIFT	16
#define  CDNS_PCIE_LM_ID_SUBSYS(sub) \
	(((sub) << CDNS_PCIE_LM_ID_SUBSYS_SHIFT) & CDNS_PCIE_LM_ID_SUBSYS_MASK)

/* Root Port Requester ID Register */
#define  CDNS_PCIE_LM_RP_RID		(CDNS_PCIE_LM_BASE + 0x0228)
#define  CDNS_PCIE_LM_RP_RID_MASK	GENMASK(15, 0)
#define  CDNS_PCIE_LM_RP_RID_SHIFT	0
#define  CDNS_PCIE_LM_RP_RID_(rid) \
	(((rid) << CDNS_PCIE_LM_RP_RID_SHIFT) & CDNS_PCIE_LM_RP_RID_MASK)

/* Endpoint Bus and Device Number Register */
#define  CDNS_PCIE_LM_EP_ID		(CDNS_PCIE_LM_BASE + 0x022C)
#define  CDNS_PCIE_LM_EP_ID_DEV_MASK	GENMASK(4, 0)
#define  CDNS_PCIE_LM_EP_ID_DEV_SHIFT	0
#define  CDNS_PCIE_LM_EP_ID_BUS_MASK	GENMASK(15, 8)
#define  CDNS_PCIE_LM_EP_ID_BUS_SHIFT	8

/* Endpoint Function f BAR b Configuration Registers */
#define CDNS_PCIE_LM_EP_FUNC_BAR_CFG(bar, fn) \
	(((bar) < BAR_4) ? CDNS_PCIE_LM_EP_FUNC_BAR_CFG0(fn) : CDNS_PCIE_LM_EP_FUNC_BAR_CFG1(fn))
#define CDNS_PCIE_LM_EP_FUNC_BAR_CFG0(fn) \
	(CDNS_PCIE_LM_BASE + 0x0240 + (fn) * 0x0008)
#define CDNS_PCIE_LM_EP_FUNC_BAR_CFG1(fn) \
	(CDNS_PCIE_LM_BASE + 0x0244 + (fn) * 0x0008)
#define CDNS_PCIE_LM_EP_VFUNC_BAR_CFG(bar, fn) \
	(((bar) < BAR_4) ? CDNS_PCIE_LM_EP_VFUNC_BAR_CFG0(fn) : CDNS_PCIE_LM_EP_VFUNC_BAR_CFG1(fn))
#define CDNS_PCIE_LM_EP_VFUNC_BAR_CFG0(fn) \
	(CDNS_PCIE_LM_BASE + 0x0280 + (fn) * 0x0008)
#define CDNS_PCIE_LM_EP_VFUNC_BAR_CFG1(fn) \
	(CDNS_PCIE_LM_BASE + 0x0284 + (fn) * 0x0008)
#define  CDNS_PCIE_LM_EP_FUNC_BAR_CFG_BAR_APERTURE_MASK(b) \
	(GENMASK(4, 0) << ((b) * 8))
#define  CDNS_PCIE_LM_EP_FUNC_BAR_CFG_BAR_APERTURE(b, a) \
	(((a) << ((b) * 8)) & CDNS_PCIE_LM_EP_FUNC_BAR_CFG_BAR_APERTURE_MASK(b))
#define  CDNS_PCIE_LM_EP_FUNC_BAR_CFG_BAR_CTRL_MASK(b) \
	(GENMASK(7, 5) << ((b) * 8))
#define  CDNS_PCIE_LM_EP_FUNC_BAR_CFG_BAR_CTRL(b, c) \
	(((c) << ((b) * 8 + 5)) & CDNS_PCIE_LM_EP_FUNC_BAR_CFG_BAR_CTRL_MASK(b))

/* Endpoint Function Configuration Register */
#define CDNS_PCIE_LM_EP_FUNC_CFG	(CDNS_PCIE_LM_BASE + 0x02C0)

/* Root Complex BAR Configuration Register */
#define CDNS_PCIE_LM_RC_BAR_CFG	(CDNS_PCIE_LM_BASE + 0x0300)
#define  CDNS_PCIE_LM_RC_BAR_CFG_BAR0_APERTURE_MASK	GENMASK(5, 0)
#define  CDNS_PCIE_LM_RC_BAR_CFG_BAR0_APERTURE(a) \
	(((a) << 0) & CDNS_PCIE_LM_RC_BAR_CFG_BAR0_APERTURE_MASK)
#define  CDNS_PCIE_LM_RC_BAR_CFG_BAR0_CTRL_MASK		GENMASK(8, 6)
#define  CDNS_PCIE_LM_RC_BAR_CFG_BAR0_CTRL(c) \
	(((c) << 6) & CDNS_PCIE_LM_RC_BAR_CFG_BAR0_CTRL_MASK)
#define  CDNS_PCIE_LM_RC_BAR_CFG_BAR1_APERTURE_MASK	GENMASK(13, 9)
#define  CDNS_PCIE_LM_RC_BAR_CFG_BAR1_APERTURE(a) \
	(((a) << 9) & CDNS_PCIE_LM_RC_BAR_CFG_BAR1_APERTURE_MASK)
#define  CDNS_PCIE_LM_RC_BAR_CFG_BAR1_CTRL_MASK		GENMASK(16, 14)
#define  CDNS_PCIE_LM_RC_BAR_CFG_BAR1_CTRL(c) \
	(((c) << 14) & CDNS_PCIE_LM_RC_BAR_CFG_BAR1_CTRL_MASK)
#define  CDNS_PCIE_LM_RC_BAR_CFG_PREFETCH_MEM_ENABLE	BIT(17)
#define  CDNS_PCIE_LM_RC_BAR_CFG_PREFETCH_MEM_32BITS	0
#define  CDNS_PCIE_LM_RC_BAR_CFG_PREFETCH_MEM_64BITS	BIT(18)
#define  CDNS_PCIE_LM_RC_BAR_CFG_IO_ENABLE		BIT(19)
#define  CDNS_PCIE_LM_RC_BAR_CFG_IO_16BITS		0
#define  CDNS_PCIE_LM_RC_BAR_CFG_IO_32BITS		BIT(20)
#define  CDNS_PCIE_LM_RC_BAR_CFG_CHECK_ENABLE		BIT(31)

/* BAR control values applicable to both Endpoint Function and Root Complex */
#define  CDNS_PCIE_LM_BAR_CFG_CTRL_DISABLED		0x0
#define  CDNS_PCIE_LM_BAR_CFG_CTRL_IO_32BITS		0x1
#define  CDNS_PCIE_LM_BAR_CFG_CTRL_MEM_32BITS		0x4
#define  CDNS_PCIE_LM_BAR_CFG_CTRL_PREFETCH_MEM_32BITS	0x5
#define  CDNS_PCIE_LM_BAR_CFG_CTRL_MEM_64BITS		0x6
#define  CDNS_PCIE_LM_BAR_CFG_CTRL_PREFETCH_MEM_64BITS	0x7

#define LM_RC_BAR_CFG_CTRL_DISABLED(bar)		\
		(CDNS_PCIE_LM_BAR_CFG_CTRL_DISABLED << (((bar) * 8) + 6))
#define LM_RC_BAR_CFG_CTRL_IO_32BITS(bar)		\
		(CDNS_PCIE_LM_BAR_CFG_CTRL_IO_32BITS << (((bar) * 8) + 6))
#define LM_RC_BAR_CFG_CTRL_MEM_32BITS(bar)		\
		(CDNS_PCIE_LM_BAR_CFG_CTRL_MEM_32BITS << (((bar) * 8) + 6))
#define LM_RC_BAR_CFG_CTRL_PREF_MEM_32BITS(bar)	\
	(CDNS_PCIE_LM_BAR_CFG_CTRL_PREFETCH_MEM_32BITS << (((bar) * 8) + 6))
#define LM_RC_BAR_CFG_CTRL_MEM_64BITS(bar)		\
		(CDNS_PCIE_LM_BAR_CFG_CTRL_MEM_64BITS << (((bar) * 8) + 6))
#define LM_RC_BAR_CFG_CTRL_PREF_MEM_64BITS(bar)	\
	(CDNS_PCIE_LM_BAR_CFG_CTRL_PREFETCH_MEM_64BITS << (((bar) * 8) + 6))
#define LM_RC_BAR_CFG_APERTURE(bar, aperture)		\
					(((aperture) - 2) << ((bar) * 8))

/* PTM Control Register */
#define CDNS_PCIE_LM_PTM_CTRL		(CDNS_PCIE_LM_BASE + 0x0DA8)
#define CDNS_PCIE_LM_TPM_CTRL_PTMRSEN	BIT(17)

/*
 * Endpoint Function Registers (PCI configuration space for endpoint functions)
 */
#define CDNS_PCIE_EP_FUNC_BASE(fn)	(((fn) << 12) & GENMASK(19, 12))

#define CDNS_PCIE_EP_FUNC_MSI_CAP_OFFSET	0x90
#define CDNS_PCIE_EP_FUNC_MSIX_CAP_OFFSET	0xB0
#define CDNS_PCIE_EP_FUNC_DEV_CAP_OFFSET	0xC0
#define CDNS_PCIE_EP_FUNC_SRIOV_CAP_OFFSET	0x200

/* Endpoint PF Registers */
#define CDNS_PCIE_CORE_PF_I_ARI_CAP_AND_CTRL(fn)	(0x144 + (fn) * 0x1000)
#define CDNS_PCIE_ARI_CAP_NFN_MASK			GENMASK(15, 8)

/* Root Port Registers (PCI configuration space for the root port function) */
#define CDNS_PCIE_RP_BASE	0x00200000
#define CDNS_PCIE_RP_CAP_OFFSET 0xC0

/* Address Translation Registers */
#define CDNS_PCIE_AT_BASE	0x00400000

/* Region r Outbound AXI to PCIe Address Translation Register 0 */
#define CDNS_PCIE_AT_OB_REGION_PCI_ADDR0(r) \
	(CDNS_PCIE_AT_BASE + 0x0000 + ((r) & 0x1F) * 0x0020)
#define  CDNS_PCIE_AT_OB_REGION_PCI_ADDR0_NBITS_MASK	GENMASK(5, 0)
#define  CDNS_PCIE_AT_OB_REGION_PCI_ADDR0_NBITS(nbits) \
	(((nbits) - 1) & CDNS_PCIE_AT_OB_REGION_PCI_ADDR0_NBITS_MASK)
#define  CDNS_PCIE_AT_OB_REGION_PCI_ADDR0_DEVFN_MASK	GENMASK(19, 12)
#define  CDNS_PCIE_AT_OB_REGION_PCI_ADDR0_DEVFN(devfn) \
	(((devfn) << 12) & CDNS_PCIE_AT_OB_REGION_PCI_ADDR0_DEVFN_MASK)
#define  CDNS_PCIE_AT_OB_REGION_PCI_ADDR0_BUS_MASK	GENMASK(27, 20)
#define  CDNS_PCIE_AT_OB_REGION_PCI_ADDR0_BUS(bus) \
	(((bus) << 20) & CDNS_PCIE_AT_OB_REGION_PCI_ADDR0_BUS_MASK)

/* Region r Outbound AXI to PCIe Address Translation Register 1 */
#define CDNS_PCIE_AT_OB_REGION_PCI_ADDR1(r) \
	(CDNS_PCIE_AT_BASE + 0x0004 + ((r) & 0x1F) * 0x0020)

/* Region r Outbound PCIe Descriptor Register 0 */
#define CDNS_PCIE_AT_OB_REGION_DESC0(r) \
	(CDNS_PCIE_AT_BASE + 0x0008 + ((r) & 0x1F) * 0x0020)
#define  CDNS_PCIE_AT_OB_REGION_DESC0_TYPE_MASK		GENMASK(3, 0)
#define  CDNS_PCIE_AT_OB_REGION_DESC0_TYPE_MEM		0x2
#define  CDNS_PCIE_AT_OB_REGION_DESC0_TYPE_IO		0x6
#define  CDNS_PCIE_AT_OB_REGION_DESC0_TYPE_CONF_TYPE0	0xA
#define  CDNS_PCIE_AT_OB_REGION_DESC0_TYPE_CONF_TYPE1	0xB
#define  CDNS_PCIE_AT_OB_REGION_DESC0_TYPE_NORMAL_MSG	0xC
#define  CDNS_PCIE_AT_OB_REGION_DESC0_TYPE_VENDOR_MSG	0xD
/* Bit 23 MUST be set in RC mode. */
#define  CDNS_PCIE_AT_OB_REGION_DESC0_HARDCODED_RID	BIT(23)
#define  CDNS_PCIE_AT_OB_REGION_DESC0_DEVFN_MASK	GENMASK(31, 24)
#define  CDNS_PCIE_AT_OB_REGION_DESC0_DEVFN(devfn) \
	(((devfn) << 24) & CDNS_PCIE_AT_OB_REGION_DESC0_DEVFN_MASK)

/* Region r Outbound PCIe Descriptor Register 1 */
#define CDNS_PCIE_AT_OB_REGION_DESC1(r)	\
	(CDNS_PCIE_AT_BASE + 0x000C + ((r) & 0x1F) * 0x0020)
#define  CDNS_PCIE_AT_OB_REGION_DESC1_BUS_MASK	GENMASK(7, 0)
#define  CDNS_PCIE_AT_OB_REGION_DESC1_BUS(bus) \
	((bus) & CDNS_PCIE_AT_OB_REGION_DESC1_BUS_MASK)

/* Region r AXI Region Base Address Register 0 */
#define CDNS_PCIE_AT_OB_REGION_CPU_ADDR0(r) \
	(CDNS_PCIE_AT_BASE + 0x0018 + ((r) & 0x1F) * 0x0020)
#define  CDNS_PCIE_AT_OB_REGION_CPU_ADDR0_NBITS_MASK	GENMASK(5, 0)
#define  CDNS_PCIE_AT_OB_REGION_CPU_ADDR0_NBITS(nbits) \
	(((nbits) - 1) & CDNS_PCIE_AT_OB_REGION_CPU_ADDR0_NBITS_MASK)

/* Region r AXI Region Base Address Register 1 */
#define CDNS_PCIE_AT_OB_REGION_CPU_ADDR1(r) \
	(CDNS_PCIE_AT_BASE + 0x001C + ((r) & 0x1F) * 0x0020)

/* Root Port BAR Inbound PCIe to AXI Address Translation Register */
#define CDNS_PCIE_AT_IB_RP_BAR_ADDR0(bar) \
	(CDNS_PCIE_AT_BASE + 0x0800 + (bar) * 0x0008)
#define  CDNS_PCIE_AT_IB_RP_BAR_ADDR0_NBITS_MASK	GENMASK(5, 0)
#define  CDNS_PCIE_AT_IB_RP_BAR_ADDR0_NBITS(nbits) \
	(((nbits) - 1) & CDNS_PCIE_AT_IB_RP_BAR_ADDR0_NBITS_MASK)
#define CDNS_PCIE_AT_IB_RP_BAR_ADDR1(bar) \
	(CDNS_PCIE_AT_BASE + 0x0804 + (bar) * 0x0008)

/* AXI link down register */
#define CDNS_PCIE_AT_LINKDOWN (CDNS_PCIE_AT_BASE + 0x0824)

/* LTSSM Capabilities register */
#define CDNS_PCIE_LTSSM_CONTROL_CAP		(CDNS_PCIE_LM_BASE + 0x0054)
#define  CDNS_PCIE_DETECT_QUIET_MIN_DELAY_MASK	GENMASK(2, 1)
#define  CDNS_PCIE_DETECT_QUIET_MIN_DELAY_SHIFT 1
#define  CDNS_PCIE_DETECT_QUIET_MIN_DELAY(delay) \
	 (((delay) << CDNS_PCIE_DETECT_QUIET_MIN_DELAY_SHIFT) & \
	 CDNS_PCIE_DETECT_QUIET_MIN_DELAY_MASK)

#define CDNS_PCIE_RP_MAX_IB	0x3
#define CDNS_PCIE_MAX_OB	32

/* Endpoint Function BAR Inbound PCIe to AXI Address Translation Register */
#define CDNS_PCIE_AT_IB_EP_FUNC_BAR_ADDR0(fn, bar) \
	(CDNS_PCIE_AT_BASE + 0x0840 + (fn) * 0x0040 + (bar) * 0x0008)
#define CDNS_PCIE_AT_IB_EP_FUNC_BAR_ADDR1(fn, bar) \
	(CDNS_PCIE_AT_BASE + 0x0844 + (fn) * 0x0040 + (bar) * 0x0008)

/* Normal/Vendor specific message access: offset inside some outbound region */
#define CDNS_PCIE_NORMAL_MSG_ROUTING_MASK	GENMASK(7, 5)
#define CDNS_PCIE_NORMAL_MSG_ROUTING(route) \
	(((route) << 5) & CDNS_PCIE_NORMAL_MSG_ROUTING_MASK)
#define CDNS_PCIE_NORMAL_MSG_CODE_MASK		GENMASK(15, 8)
#define CDNS_PCIE_NORMAL_MSG_CODE(code) \
	(((code) << 8) & CDNS_PCIE_NORMAL_MSG_CODE_MASK)
#define CDNS_PCIE_MSG_NO_DATA                   BIT(16)

#endif /* _PCIE_CADENCE_LGA_REGS_H */
