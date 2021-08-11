/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (c) 2017 Cadence
// Cadence PCIe controller driver.
// Author: Cyrille Pitchen <cyrille.pitchen@free-electrons.com>

#ifndef _PCIE_CADENCE_H
#define _PCIE_CADENCE_H

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>

/* Parameters for the waiting for link up routine */
#define LINK_WAIT_MAX_RETRIES	10
#define LINK_WAIT_USLEEP_MIN	90000
#define LINK_WAIT_USLEEP_MAX	100000

/*
 * Local Management Registers
 */
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

/* Root Port Requestor ID Register */
#define CDNS_PCIE_LM_RP_RID	(CDNS_PCIE_LM_BASE + 0x0228)
#define  CDNS_PCIE_LM_RP_RID_MASK	GENMASK(15, 0)
#define  CDNS_PCIE_LM_RP_RID_SHIFT	0
#define  CDNS_PCIE_LM_RP_RID_(rid) \
	(((rid) << CDNS_PCIE_LM_RP_RID_SHIFT) & CDNS_PCIE_LM_RP_RID_MASK)

/* Endpoint Bus and Device Number Register */
#define CDNS_PCIE_LM_EP_ID	(CDNS_PCIE_LM_BASE + 0x022c)
#define  CDNS_PCIE_LM_EP_ID_DEV_MASK	GENMASK(4, 0)
#define  CDNS_PCIE_LM_EP_ID_DEV_SHIFT	0
#define  CDNS_PCIE_LM_EP_ID_BUS_MASK	GENMASK(15, 8)
#define  CDNS_PCIE_LM_EP_ID_BUS_SHIFT	8

/* Endpoint Function f BAR b Configuration Registers */
#define CDNS_PCIE_LM_EP_FUNC_BAR_CFG0(fn) \
	(CDNS_PCIE_LM_BASE + 0x0240 + (fn) * 0x0008)
#define CDNS_PCIE_LM_EP_FUNC_BAR_CFG1(fn) \
	(CDNS_PCIE_LM_BASE + 0x0244 + (fn) * 0x0008)
#define  CDNS_PCIE_LM_EP_FUNC_BAR_CFG_BAR_APERTURE_MASK(b) \
	(GENMASK(4, 0) << ((b) * 8))
#define  CDNS_PCIE_LM_EP_FUNC_BAR_CFG_BAR_APERTURE(b, a) \
	(((a) << ((b) * 8)) & CDNS_PCIE_LM_EP_FUNC_BAR_CFG_BAR_APERTURE_MASK(b))
#define  CDNS_PCIE_LM_EP_FUNC_BAR_CFG_BAR_CTRL_MASK(b) \
	(GENMASK(7, 5) << ((b) * 8))
#define  CDNS_PCIE_LM_EP_FUNC_BAR_CFG_BAR_CTRL(b, c) \
	(((c) << ((b) * 8 + 5)) & CDNS_PCIE_LM_EP_FUNC_BAR_CFG_BAR_CTRL_MASK(b))

/* Endpoint Function Configuration Register */
#define CDNS_PCIE_LM_EP_FUNC_CFG	(CDNS_PCIE_LM_BASE + 0x02c0)

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

/*
 * Endpoint Function Registers (PCI configuration space for endpoint functions)
 */
#define CDNS_PCIE_EP_FUNC_BASE(fn)	(((fn) << 12) & GENMASK(19, 12))

#define CDNS_PCIE_EP_FUNC_MSI_CAP_OFFSET	0x90
#define CDNS_PCIE_EP_FUNC_MSIX_CAP_OFFSET	0xb0

/*
 * Root Port Registers (PCI configuration space for the root port function)
 */
#define CDNS_PCIE_RP_BASE	0x00200000
#define CDNS_PCIE_RP_CAP_OFFSET 0xc0

/*
 * Address Translation Registers
 */
#define CDNS_PCIE_AT_BASE	0x00400000

/* Region r Outbound AXI to PCIe Address Translation Register 0 */
#define CDNS_PCIE_AT_OB_REGION_PCI_ADDR0(r) \
	(CDNS_PCIE_AT_BASE + 0x0000 + ((r) & 0x1f) * 0x0020)
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
	(CDNS_PCIE_AT_BASE + 0x0004 + ((r) & 0x1f) * 0x0020)

/* Region r Outbound PCIe Descriptor Register 0 */
#define CDNS_PCIE_AT_OB_REGION_DESC0(r) \
	(CDNS_PCIE_AT_BASE + 0x0008 + ((r) & 0x1f) * 0x0020)
#define  CDNS_PCIE_AT_OB_REGION_DESC0_TYPE_MASK		GENMASK(3, 0)
#define  CDNS_PCIE_AT_OB_REGION_DESC0_TYPE_MEM		0x2
#define  CDNS_PCIE_AT_OB_REGION_DESC0_TYPE_IO		0x6
#define  CDNS_PCIE_AT_OB_REGION_DESC0_TYPE_CONF_TYPE0	0xa
#define  CDNS_PCIE_AT_OB_REGION_DESC0_TYPE_CONF_TYPE1	0xb
#define  CDNS_PCIE_AT_OB_REGION_DESC0_TYPE_NORMAL_MSG	0xc
#define  CDNS_PCIE_AT_OB_REGION_DESC0_TYPE_VENDOR_MSG	0xd
/* Bit 23 MUST be set in RC mode. */
#define  CDNS_PCIE_AT_OB_REGION_DESC0_HARDCODED_RID	BIT(23)
#define  CDNS_PCIE_AT_OB_REGION_DESC0_DEVFN_MASK	GENMASK(31, 24)
#define  CDNS_PCIE_AT_OB_REGION_DESC0_DEVFN(devfn) \
	(((devfn) << 24) & CDNS_PCIE_AT_OB_REGION_DESC0_DEVFN_MASK)

/* Region r Outbound PCIe Descriptor Register 1 */
#define CDNS_PCIE_AT_OB_REGION_DESC1(r)	\
	(CDNS_PCIE_AT_BASE + 0x000c + ((r) & 0x1f) * 0x0020)
#define  CDNS_PCIE_AT_OB_REGION_DESC1_BUS_MASK	GENMASK(7, 0)
#define  CDNS_PCIE_AT_OB_REGION_DESC1_BUS(bus) \
	((bus) & CDNS_PCIE_AT_OB_REGION_DESC1_BUS_MASK)

/* Region r AXI Region Base Address Register 0 */
#define CDNS_PCIE_AT_OB_REGION_CPU_ADDR0(r) \
	(CDNS_PCIE_AT_BASE + 0x0018 + ((r) & 0x1f) * 0x0020)
#define  CDNS_PCIE_AT_OB_REGION_CPU_ADDR0_NBITS_MASK	GENMASK(5, 0)
#define  CDNS_PCIE_AT_OB_REGION_CPU_ADDR0_NBITS(nbits) \
	(((nbits) - 1) & CDNS_PCIE_AT_OB_REGION_CPU_ADDR0_NBITS_MASK)

/* Region r AXI Region Base Address Register 1 */
#define CDNS_PCIE_AT_OB_REGION_CPU_ADDR1(r) \
	(CDNS_PCIE_AT_BASE + 0x001c + ((r) & 0x1f) * 0x0020)

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

enum cdns_pcie_rp_bar {
	RP_BAR_UNDEFINED = -1,
	RP_BAR0,
	RP_BAR1,
	RP_NO_BAR
};

#define CDNS_PCIE_RP_MAX_IB	0x3
#define CDNS_PCIE_MAX_OB	32

struct cdns_pcie_rp_ib_bar {
	u64 size;
	bool free;
};

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
#define CDNS_PCIE_MSG_NO_DATA			BIT(16)

struct cdns_pcie;

enum cdns_pcie_msg_code {
	MSG_CODE_ASSERT_INTA	= 0x20,
	MSG_CODE_ASSERT_INTB	= 0x21,
	MSG_CODE_ASSERT_INTC	= 0x22,
	MSG_CODE_ASSERT_INTD	= 0x23,
	MSG_CODE_DEASSERT_INTA	= 0x24,
	MSG_CODE_DEASSERT_INTB	= 0x25,
	MSG_CODE_DEASSERT_INTC	= 0x26,
	MSG_CODE_DEASSERT_INTD	= 0x27,
};

enum cdns_pcie_msg_routing {
	/* Route to Root Complex */
	MSG_ROUTING_TO_RC,

	/* Use Address Routing */
	MSG_ROUTING_BY_ADDR,

	/* Use ID Routing */
	MSG_ROUTING_BY_ID,

	/* Route as Broadcast Message from Root Complex */
	MSG_ROUTING_BCAST,

	/* Local message; terminate at receiver (INTx messages) */
	MSG_ROUTING_LOCAL,

	/* Gather & route to Root Complex (PME_TO_Ack message) */
	MSG_ROUTING_GATHER,
};

struct cdns_pcie_ops {
	int	(*start_link)(struct cdns_pcie *pcie);
	void	(*stop_link)(struct cdns_pcie *pcie);
	bool	(*link_up)(struct cdns_pcie *pcie);
	u64     (*cpu_addr_fixup)(struct cdns_pcie *pcie, u64 cpu_addr);
};

/**
 * struct cdns_pcie - private data for Cadence PCIe controller drivers
 * @reg_base: IO mapped register base
 * @mem_res: start/end offsets in the physical system memory to map PCI accesses
 * @dev: PCIe controller
 * @is_rc: tell whether the PCIe controller mode is Root Complex or Endpoint.
 * @phy_count: number of supported PHY devices
 * @phy: list of pointers to specific PHY control blocks
 * @link: list of pointers to corresponding device link representations
 * @ops: Platform-specific ops to control various inputs from Cadence PCIe
 *       wrapper
 */
struct cdns_pcie {
	void __iomem		*reg_base;
	struct resource		*mem_res;
	struct device		*dev;
	bool			is_rc;
	int			phy_count;
	struct phy		**phy;
	struct device_link	**link;
	const struct cdns_pcie_ops *ops;
};

/**
 * struct cdns_pcie_rc - private data for this PCIe Root Complex driver
 * @pcie: Cadence PCIe controller
 * @dev: pointer to PCIe device
 * @cfg_res: start/end offsets in the physical system memory to map PCI
 *           configuration space accesses
 * @cfg_base: IO mapped window to access the PCI configuration space of a
 *            single function at a time
 * @vendor_id: PCI vendor ID
 * @device_id: PCI device ID
 * @avail_ib_bar: Satus of RP_BAR0, RP_BAR1 and	RP_NO_BAR if it's free or
 *                available
 * @quirk_retrain_flag: Retrain link as quirk for PCIe Gen2
 */
struct cdns_pcie_rc {
	struct cdns_pcie	pcie;
	struct resource		*cfg_res;
	void __iomem		*cfg_base;
	u32			vendor_id;
	u32			device_id;
	bool			avail_ib_bar[CDNS_PCIE_RP_MAX_IB];
	unsigned int		quirk_retrain_flag:1;
};

/**
 * struct cdns_pcie_epf - Structure to hold info about endpoint function
 * @epf_bar: reference to the pci_epf_bar for the six Base Address Registers
 */
struct cdns_pcie_epf {
	struct pci_epf_bar *epf_bar[PCI_STD_NUM_BARS];
};

/**
 * struct cdns_pcie_ep - private data for this PCIe endpoint controller driver
 * @pcie: Cadence PCIe controller
 * @max_regions: maximum number of regions supported by hardware
 * @ob_region_map: bitmask of mapped outbound regions
 * @ob_addr: base addresses in the AXI bus where the outbound regions start
 * @irq_phys_addr: base address on the AXI bus where the MSI/legacy IRQ
 *		   dedicated outbound regions is mapped.
 * @irq_cpu_addr: base address in the CPU space where a write access triggers
 *		  the sending of a memory write (MSI) / normal message (legacy
 *		  IRQ) TLP through the PCIe bus.
 * @irq_pci_addr: used to save the current mapping of the MSI/legacy IRQ
 *		  dedicated outbound region.
 * @irq_pci_fn: the latest PCI function that has updated the mapping of
 *		the MSI/legacy IRQ dedicated outbound region.
 * @irq_pending: bitmask of asserted legacy IRQs.
 * @lock: spin lock to disable interrupts while modifying PCIe controller
 *        registers fields (RMW) accessible by both remote RC and EP to
 *        minimize time between read and write
 * @epf: Structure to hold info about endpoint function
 */
struct cdns_pcie_ep {
	struct cdns_pcie	pcie;
	u32			max_regions;
	unsigned long		ob_region_map;
	phys_addr_t		*ob_addr;
	phys_addr_t		irq_phys_addr;
	void __iomem		*irq_cpu_addr;
	u64			irq_pci_addr;
	u8			irq_pci_fn;
	u8			irq_pending;
	/* protect writing to PCI_STATUS while raising legacy interrupts */
	spinlock_t		lock;
	struct cdns_pcie_epf	*epf;
};


/* Register access */
static inline void cdns_pcie_writel(struct cdns_pcie *pcie, u32 reg, u32 value)
{
	writel(value, pcie->reg_base + reg);
}

static inline u32 cdns_pcie_readl(struct cdns_pcie *pcie, u32 reg)
{
	return readl(pcie->reg_base + reg);
}

static inline u32 cdns_pcie_read_sz(void __iomem *addr, int size)
{
	void __iomem *aligned_addr = PTR_ALIGN_DOWN(addr, 0x4);
	unsigned int offset = (unsigned long)addr & 0x3;
	u32 val = readl(aligned_addr);

	if (!IS_ALIGNED((uintptr_t)addr, size)) {
		pr_warn("Address %p and size %d are not aligned\n", addr, size);
		return 0;
	}

	if (size > 2)
		return val;

	return (val >> (8 * offset)) & ((1 << (size * 8)) - 1);
}

static inline void cdns_pcie_write_sz(void __iomem *addr, int size, u32 value)
{
	void __iomem *aligned_addr = PTR_ALIGN_DOWN(addr, 0x4);
	unsigned int offset = (unsigned long)addr & 0x3;
	u32 mask;
	u32 val;

	if (!IS_ALIGNED((uintptr_t)addr, size)) {
		pr_warn("Address %p and size %d are not aligned\n", addr, size);
		return;
	}

	if (size > 2) {
		writel(value, addr);
		return;
	}

	mask = ~(((1 << (size * 8)) - 1) << (offset * 8));
	val = readl(aligned_addr) & mask;
	val |= value << (offset * 8);
	writel(val, aligned_addr);
}

/* Root Port register access */
static inline void cdns_pcie_rp_writeb(struct cdns_pcie *pcie,
				       u32 reg, u8 value)
{
	void __iomem *addr = pcie->reg_base + CDNS_PCIE_RP_BASE + reg;

	cdns_pcie_write_sz(addr, 0x1, value);
}

static inline void cdns_pcie_rp_writew(struct cdns_pcie *pcie,
				       u32 reg, u16 value)
{
	void __iomem *addr = pcie->reg_base + CDNS_PCIE_RP_BASE + reg;

	cdns_pcie_write_sz(addr, 0x2, value);
}

static inline u16 cdns_pcie_rp_readw(struct cdns_pcie *pcie, u32 reg)
{
	void __iomem *addr = pcie->reg_base + CDNS_PCIE_RP_BASE + reg;

	return cdns_pcie_read_sz(addr, 0x2);
}

/* Endpoint Function register access */
static inline void cdns_pcie_ep_fn_writeb(struct cdns_pcie *pcie, u8 fn,
					  u32 reg, u8 value)
{
	void __iomem *addr = pcie->reg_base + CDNS_PCIE_EP_FUNC_BASE(fn) + reg;

	cdns_pcie_write_sz(addr, 0x1, value);
}

static inline void cdns_pcie_ep_fn_writew(struct cdns_pcie *pcie, u8 fn,
					  u32 reg, u16 value)
{
	void __iomem *addr = pcie->reg_base + CDNS_PCIE_EP_FUNC_BASE(fn) + reg;

	cdns_pcie_write_sz(addr, 0x2, value);
}

static inline void cdns_pcie_ep_fn_writel(struct cdns_pcie *pcie, u8 fn,
					  u32 reg, u32 value)
{
	writel(value, pcie->reg_base + CDNS_PCIE_EP_FUNC_BASE(fn) + reg);
}

static inline u16 cdns_pcie_ep_fn_readw(struct cdns_pcie *pcie, u8 fn, u32 reg)
{
	void __iomem *addr = pcie->reg_base + CDNS_PCIE_EP_FUNC_BASE(fn) + reg;

	return cdns_pcie_read_sz(addr, 0x2);
}

static inline u32 cdns_pcie_ep_fn_readl(struct cdns_pcie *pcie, u8 fn, u32 reg)
{
	return readl(pcie->reg_base + CDNS_PCIE_EP_FUNC_BASE(fn) + reg);
}

static inline int cdns_pcie_start_link(struct cdns_pcie *pcie)
{
	if (pcie->ops->start_link)
		return pcie->ops->start_link(pcie);

	return 0;
}

static inline void cdns_pcie_stop_link(struct cdns_pcie *pcie)
{
	if (pcie->ops->stop_link)
		pcie->ops->stop_link(pcie);
}

static inline bool cdns_pcie_link_up(struct cdns_pcie *pcie)
{
	if (pcie->ops->link_up)
		return pcie->ops->link_up(pcie);

	return true;
}

#ifdef CONFIG_PCIE_CADENCE_HOST
int cdns_pcie_host_setup(struct cdns_pcie_rc *rc);
void __iomem *cdns_pci_map_bus(struct pci_bus *bus, unsigned int devfn,
			       int where);
#else
static inline int cdns_pcie_host_setup(struct cdns_pcie_rc *rc)
{
	return 0;
}

static inline void __iomem *cdns_pci_map_bus(struct pci_bus *bus, unsigned int devfn,
					     int where)
{
	return NULL;
}
#endif

#ifdef CONFIG_PCIE_CADENCE_EP
int cdns_pcie_ep_setup(struct cdns_pcie_ep *ep);
#else
static inline int cdns_pcie_ep_setup(struct cdns_pcie_ep *ep)
{
	return 0;
}
#endif
void cdns_pcie_set_outbound_region(struct cdns_pcie *pcie, u8 busnr, u8 fn,
				   u32 r, bool is_io,
				   u64 cpu_addr, u64 pci_addr, size_t size);

void cdns_pcie_set_outbound_region_for_normal_msg(struct cdns_pcie *pcie,
						  u8 busnr, u8 fn,
						  u32 r, u64 cpu_addr);

void cdns_pcie_reset_outbound_region(struct cdns_pcie *pcie, u32 r);
void cdns_pcie_disable_phy(struct cdns_pcie *pcie);
int cdns_pcie_enable_phy(struct cdns_pcie *pcie);
int cdns_pcie_init_phy(struct device *dev, struct cdns_pcie *pcie);
extern const struct dev_pm_ops cdns_pcie_pm_ops;

#endif /* _PCIE_CADENCE_H */
