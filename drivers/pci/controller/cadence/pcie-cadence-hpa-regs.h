/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cadence PCIe controller driver.
 *
 * Copyright (c) 2024, Cadence Design Systems
 * Author: Manikandan K Pillai <mpillai@cadence.com>
 */
#ifndef _PCIE_CADENCE_HPA_REGS_H
#define _PCIE_CADENCE_HPA_REGS_H

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pci-epf.h>
#include <linux/phy/phy.h>
#include <linux/bitfield.h>

/* High Performance Architecture (HPA) PCIe controller registers */
#define CDNS_PCIE_HPA_IP_REG_BANK		0x01000000
#define CDNS_PCIE_HPA_IP_CFG_CTRL_REG_BANK	0x01003C00
#define CDNS_PCIE_HPA_IP_AXI_MASTER_COMMON	0x02020000

/* Address Translation Registers */
#define CDNS_PCIE_HPA_AXI_SLAVE                 0x03000000
#define CDNS_PCIE_HPA_AXI_MASTER                0x03002000

/* Root Port register base address */
#define CDNS_PCIE_HPA_RP_BASE			0x0

#define CDNS_PCIE_HPA_LM_ID			0x1420

/* Endpoint Function BARs */
#define CDNS_PCIE_HPA_LM_EP_FUNC_BAR_CFG(bar, fn) \
	(((bar) < BAR_3) ? CDNS_PCIE_HPA_LM_EP_FUNC_BAR_CFG0(fn) : \
			CDNS_PCIE_HPA_LM_EP_FUNC_BAR_CFG1(fn))
#define CDNS_PCIE_HPA_LM_EP_FUNC_BAR_CFG0(pfn) (0x4000 * (pfn))
#define CDNS_PCIE_HPA_LM_EP_FUNC_BAR_CFG1(pfn) ((0x4000 * (pfn)) + 0x04)
#define CDNS_PCIE_HPA_LM_EP_VFUNC_BAR_CFG(bar, fn) \
	(((bar) < BAR_3) ? CDNS_PCIE_HPA_LM_EP_VFUNC_BAR_CFG0(fn) : \
			CDNS_PCIE_HPA_LM_EP_VFUNC_BAR_CFG1(fn))
#define CDNS_PCIE_HPA_LM_EP_VFUNC_BAR_CFG0(vfn) ((0x4000 * (vfn)) + 0x08)
#define CDNS_PCIE_HPA_LM_EP_VFUNC_BAR_CFG1(vfn) ((0x4000 * (vfn)) + 0x0C)
#define CDNS_PCIE_HPA_LM_EP_FUNC_BAR_CFG_BAR_APERTURE_MASK(f) \
	(GENMASK(5, 0) << (0x4 + (f) * 10))
#define CDNS_PCIE_HPA_LM_EP_FUNC_BAR_CFG_BAR_APERTURE(b, a) \
	(((a) << (4 + ((b) * 10))) & (CDNS_PCIE_HPA_LM_EP_FUNC_BAR_CFG_BAR_APERTURE_MASK(b)))
#define CDNS_PCIE_HPA_LM_EP_FUNC_BAR_CFG_BAR_CTRL_MASK(f) \
	(GENMASK(3, 0) << ((f) * 10))
#define CDNS_PCIE_HPA_LM_EP_FUNC_BAR_CFG_BAR_CTRL(b, c) \
	(((c) << ((b) * 10)) & (CDNS_PCIE_HPA_LM_EP_FUNC_BAR_CFG_BAR_CTRL_MASK(b)))

/* Endpoint Function Configuration Register */
#define CDNS_PCIE_HPA_LM_EP_FUNC_CFG		0x02C0

/* Root Complex BAR Configuration Register */
#define CDNS_PCIE_HPA_LM_RC_BAR_CFG                        0x14
#define CDNS_PCIE_HPA_LM_RC_BAR_CFG_BAR0_APERTURE_MASK     GENMASK(9, 4)
#define CDNS_PCIE_HPA_LM_RC_BAR_CFG_BAR0_APERTURE(a) \
	FIELD_PREP(CDNS_PCIE_HPA_LM_RC_BAR_CFG_BAR0_APERTURE_MASK, a)
#define CDNS_PCIE_HPA_LM_RC_BAR_CFG_BAR0_CTRL_MASK         GENMASK(3, 0)
#define CDNS_PCIE_HPA_LM_RC_BAR_CFG_BAR0_CTRL(c) \
	FIELD_PREP(CDNS_PCIE_HPA_LM_RC_BAR_CFG_BAR0_CTRL_MASK, c)
#define CDNS_PCIE_HPA_LM_RC_BAR_CFG_BAR1_APERTURE_MASK     GENMASK(19, 14)
#define CDNS_PCIE_HPA_LM_RC_BAR_CFG_BAR1_APERTURE(a) \
	FIELD_PREP(CDNS_PCIE_HPA_LM_RC_BAR_CFG_BAR1_APERTURE_MASK, a)
#define CDNS_PCIE_HPA_LM_RC_BAR_CFG_BAR1_CTRL_MASK         GENMASK(13, 10)
#define CDNS_PCIE_HPA_LM_RC_BAR_CFG_BAR1_CTRL(c) \
	FIELD_PREP(CDNS_PCIE_HPA_LM_RC_BAR_CFG_BAR1_CTRL_MASK, c)

#define CDNS_PCIE_HPA_LM_RC_BAR_CFG_PREFETCH_MEM_ENABLE BIT(20)
#define CDNS_PCIE_HPA_LM_RC_BAR_CFG_PREFETCH_MEM_64BITS BIT(21)
#define CDNS_PCIE_HPA_LM_RC_BAR_CFG_IO_ENABLE           BIT(22)
#define CDNS_PCIE_HPA_LM_RC_BAR_CFG_IO_32BITS           BIT(23)

/* BAR control values applicable to both Endpoint Function and Root Complex */
#define CDNS_PCIE_HPA_LM_BAR_CFG_CTRL_DISABLED              0x0
#define CDNS_PCIE_HPA_LM_BAR_CFG_CTRL_IO_32BITS             0x3
#define CDNS_PCIE_HPA_LM_BAR_CFG_CTRL_MEM_32BITS            0x1
#define CDNS_PCIE_HPA_LM_BAR_CFG_CTRL_PREFETCH_MEM_32BITS   0x9
#define CDNS_PCIE_HPA_LM_BAR_CFG_CTRL_MEM_64BITS            0x5
#define CDNS_PCIE_HPA_LM_BAR_CFG_CTRL_PREFETCH_MEM_64BITS   0xD

#define HPA_LM_RC_BAR_CFG_CTRL_DISABLED(bar)                \
		(CDNS_PCIE_HPA_LM_BAR_CFG_CTRL_DISABLED << ((bar) * 10))
#define HPA_LM_RC_BAR_CFG_CTRL_IO_32BITS(bar)               \
		(CDNS_PCIE_HPA_LM_BAR_CFG_CTRL_IO_32BITS << ((bar) * 10))
#define HPA_LM_RC_BAR_CFG_CTRL_MEM_32BITS(bar)              \
		(CDNS_PCIE_HPA_LM_BAR_CFG_CTRL_MEM_32BITS << ((bar) * 10))
#define HPA_LM_RC_BAR_CFG_CTRL_PREF_MEM_32BITS(bar) \
		(CDNS_PCIE_HPA_LM_BAR_CFG_CTRL_PREFETCH_MEM_32BITS << ((bar) * 10))
#define HPA_LM_RC_BAR_CFG_CTRL_MEM_64BITS(bar)              \
		(CDNS_PCIE_HPA_LM_BAR_CFG_CTRL_MEM_64BITS << ((bar) * 10))
#define HPA_LM_RC_BAR_CFG_CTRL_PREF_MEM_64BITS(bar) \
		(CDNS_PCIE_HPA_LM_BAR_CFG_CTRL_PREFETCH_MEM_64BITS << ((bar) * 10))
#define HPA_LM_RC_BAR_CFG_APERTURE(bar, aperture)           \
		(((aperture) - 7) << (((bar) * 10) + 4))

#define CDNS_PCIE_HPA_LM_PTM_CTRL		0x0520
#define CDNS_PCIE_HPA_LM_PTM_CTRL_PTMRSEN	BIT(17)

/* Root Port Registers PCI config space for root port function */
#define CDNS_PCIE_HPA_RP_CAP_OFFSET	0xC0

/* Region r Outbound AXI to PCIe Address Translation Register 0 */
#define CDNS_PCIE_HPA_AT_OB_REGION_PCI_ADDR0(r)            (0x1010 + ((r) & 0x1F) * 0x0080)
#define CDNS_PCIE_HPA_AT_OB_REGION_PCI_ADDR0_NBITS_MASK    GENMASK(5, 0)
#define CDNS_PCIE_HPA_AT_OB_REGION_PCI_ADDR0_NBITS(nbits) \
	(((nbits) - 1) & CDNS_PCIE_HPA_AT_OB_REGION_PCI_ADDR0_NBITS_MASK)
#define CDNS_PCIE_HPA_AT_OB_REGION_PCI_ADDR0_DEVFN_MASK    GENMASK(23, 16)
#define CDNS_PCIE_HPA_AT_OB_REGION_PCI_ADDR0_DEVFN(devfn) \
	FIELD_PREP(CDNS_PCIE_HPA_AT_OB_REGION_PCI_ADDR0_DEVFN_MASK, devfn)
#define CDNS_PCIE_HPA_AT_OB_REGION_PCI_ADDR0_BUS_MASK      GENMASK(31, 24)
#define CDNS_PCIE_HPA_AT_OB_REGION_PCI_ADDR0_BUS(bus) \
	FIELD_PREP(CDNS_PCIE_HPA_AT_OB_REGION_PCI_ADDR0_BUS_MASK, bus)

/* Region r Outbound AXI to PCIe Address Translation Register 1 */
#define CDNS_PCIE_HPA_AT_OB_REGION_PCI_ADDR1(r)            (0x1014 + ((r) & 0x1F) * 0x0080)

/* Region r Outbound PCIe Descriptor Register */
#define CDNS_PCIE_HPA_AT_OB_REGION_DESC0(r)                (0x1008 + ((r) & 0x1F) * 0x0080)
#define CDNS_PCIE_HPA_AT_OB_REGION_DESC0_TYPE_MASK         GENMASK(28, 24)
#define CDNS_PCIE_HPA_AT_OB_REGION_DESC0_TYPE_MEM  \
	FIELD_PREP(CDNS_PCIE_HPA_AT_OB_REGION_DESC0_TYPE_MASK, 0x0)
#define CDNS_PCIE_HPA_AT_OB_REGION_DESC0_TYPE_IO   \
	FIELD_PREP(CDNS_PCIE_HPA_AT_OB_REGION_DESC0_TYPE_MASK, 0x2)
#define CDNS_PCIE_HPA_AT_OB_REGION_DESC0_TYPE_CONF_TYPE0  \
	FIELD_PREP(CDNS_PCIE_HPA_AT_OB_REGION_DESC0_TYPE_MASK, 0x4)
#define CDNS_PCIE_HPA_AT_OB_REGION_DESC0_TYPE_CONF_TYPE1  \
	FIELD_PREP(CDNS_PCIE_HPA_AT_OB_REGION_DESC0_TYPE_MASK, 0x5)
#define CDNS_PCIE_HPA_AT_OB_REGION_DESC0_TYPE_NORMAL_MSG  \
	FIELD_PREP(CDNS_PCIE_HPA_AT_OB_REGION_DESC0_TYPE_MASK, 0x10)

/* Region r Outbound PCIe Descriptor Register */
#define CDNS_PCIE_HPA_AT_OB_REGION_DESC1(r)        (0x100C + ((r) & 0x1F) * 0x0080)
#define CDNS_PCIE_HPA_AT_OB_REGION_DESC1_BUS_MASK  GENMASK(31, 24)
#define CDNS_PCIE_HPA_AT_OB_REGION_DESC1_BUS(bus) \
	FIELD_PREP(CDNS_PCIE_HPA_AT_OB_REGION_DESC1_BUS_MASK, bus)
#define CDNS_PCIE_HPA_AT_OB_REGION_DESC1_DEVFN_MASK    GENMASK(23, 16)
#define CDNS_PCIE_HPA_AT_OB_REGION_DESC1_DEVFN(devfn) \
	FIELD_PREP(CDNS_PCIE_HPA_AT_OB_REGION_DESC1_DEVFN_MASK, devfn)

#define CDNS_PCIE_HPA_AT_OB_REGION_CTRL0(r)         (0x1018 + ((r) & 0x1F) * 0x0080)
#define CDNS_PCIE_HPA_AT_OB_REGION_CTRL0_SUPPLY_BUS BIT(26)
#define CDNS_PCIE_HPA_AT_OB_REGION_CTRL0_SUPPLY_DEV_FN BIT(25)

/* Region r AXI Region Base Address Register 0 */
#define CDNS_PCIE_HPA_AT_OB_REGION_CPU_ADDR0(r)     (0x1000 + ((r) & 0x1F) * 0x0080)
#define CDNS_PCIE_HPA_AT_OB_REGION_CPU_ADDR0_NBITS_MASK    GENMASK(5, 0)
#define CDNS_PCIE_HPA_AT_OB_REGION_CPU_ADDR0_NBITS(nbits) \
	(((nbits) - 1) & CDNS_PCIE_HPA_AT_OB_REGION_CPU_ADDR0_NBITS_MASK)

/* Region r AXI Region Base Address Register 1 */
#define CDNS_PCIE_HPA_AT_OB_REGION_CPU_ADDR1(r)     (0x1004 + ((r) & 0x1F) * 0x0080)

/* Root Port BAR Inbound PCIe to AXI Address Translation Register */
#define CDNS_PCIE_HPA_AT_IB_RP_BAR_ADDR0(bar)              (((bar) * 0x0008))
#define CDNS_PCIE_HPA_AT_IB_RP_BAR_ADDR0_NBITS_MASK        GENMASK(5, 0)
#define CDNS_PCIE_HPA_AT_IB_RP_BAR_ADDR0_NBITS(nbits) \
	(((nbits) - 1) & CDNS_PCIE_HPA_AT_IB_RP_BAR_ADDR0_NBITS_MASK)
#define CDNS_PCIE_HPA_AT_IB_RP_BAR_ADDR1(bar)              (0x04 + ((bar) * 0x0008))

/* AXI link down register */
#define CDNS_PCIE_HPA_AT_LINKDOWN 0x04

/*
 * Physical Layer Configuration Register 0
 * This register contains the parameters required for functional setup
 * of Physical Layer.
 */
#define CDNS_PCIE_HPA_PHY_LAYER_CFG0               0x0400
#define CDNS_PCIE_HPA_DETECT_QUIET_MIN_DELAY_MASK  GENMASK(26, 24)
#define CDNS_PCIE_HPA_DETECT_QUIET_MIN_DELAY(delay) \
	FIELD_PREP(CDNS_PCIE_HPA_DETECT_QUIET_MIN_DELAY_MASK, delay)
#define CDNS_PCIE_HPA_LINK_TRNG_EN_MASK  GENMASK(27, 27)

#define CDNS_PCIE_HPA_PHY_DBG_STS_REG0             0x0420

#define CDNS_PCIE_HPA_RP_MAX_IB     0x3
#define CDNS_PCIE_HPA_MAX_OB        15

/* Endpoint Function BAR Inbound PCIe to AXI Address Translation Register */
#define CDNS_PCIE_HPA_AT_IB_EP_FUNC_BAR_ADDR0(fn, bar) (((fn) * 0x0080) + ((bar) * 0x0008))
#define CDNS_PCIE_HPA_AT_IB_EP_FUNC_BAR_ADDR1(fn, bar) (0x4 + ((fn) * 0x0080) + ((bar) * 0x0008))

/* Miscellaneous offsets definitions */
#define CDNS_PCIE_HPA_TAG_MANAGEMENT        0x0
#define CDNS_PCIE_HPA_SLAVE_RESP            0x100

#define I_ROOT_PORT_REQ_ID_REG              0x141c
#define LM_HAL_SBSA_CTRL                    0x1170

#define I_PCIE_BUS_NUMBERS                  (CDNS_PCIE_HPA_RP_BASE + 0x18)
#define CDNS_PCIE_EROM                      0x18
#endif /* _PCIE_CADENCE_HPA_REGS_H */
