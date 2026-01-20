/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (c) 2017 Cadence
// Cadence PCIe controller driver.
// Author: Cyrille Pitchen <cyrille.pitchen@free-electrons.com>

#ifndef _PCIE_CADENCE_H
#define _PCIE_CADENCE_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci-epf.h>
#include <linux/phy/phy.h>
#include "pcie-cadence-lga-regs.h"
#include "pcie-cadence-hpa-regs.h"

enum cdns_pcie_rp_bar {
	RP_BAR_UNDEFINED = -1,
	RP_BAR0,
	RP_BAR1,
	RP_NO_BAR
};

struct cdns_pcie_rp_ib_bar {
	u64 size;
	bool free;
};

struct cdns_pcie;
struct cdns_pcie_rc;

enum cdns_pcie_reg_bank {
	REG_BANK_RP,
	REG_BANK_IP_REG,
	REG_BANK_IP_CFG_CTRL_REG,
	REG_BANK_AXI_MASTER_COMMON,
	REG_BANK_AXI_MASTER,
	REG_BANK_AXI_SLAVE,
	REG_BANK_AXI_HLS,
	REG_BANK_AXI_RAS,
	REG_BANK_AXI_DTI,
	REG_BANKS_MAX,
};

struct cdns_pcie_ops {
	int     (*start_link)(struct cdns_pcie *pcie);
	void    (*stop_link)(struct cdns_pcie *pcie);
	bool    (*link_up)(struct cdns_pcie *pcie);
	u64     (*cpu_addr_fixup)(struct cdns_pcie *pcie, u64 cpu_addr);
};

/**
 * struct cdns_plat_pcie_of_data - Register bank offset for a platform
 * @is_rc: controller is a RC
 * @ip_reg_bank_offset: ip register bank start offset
 * @ip_cfg_ctrl_reg_offset: ip config control register start offset
 * @axi_mstr_common_offset: AXI master common register start offset
 * @axi_slave_offset: AXI slave start offset
 * @axi_master_offset: AXI master start offset
 * @axi_hls_offset: AXI HLS offset start
 * @axi_ras_offset: AXI RAS offset
 * @axi_dti_offset: AXI DTI offset
 */
struct cdns_plat_pcie_of_data {
	u32 is_rc:1;
	u32 ip_reg_bank_offset;
	u32 ip_cfg_ctrl_reg_offset;
	u32 axi_mstr_common_offset;
	u32 axi_slave_offset;
	u32 axi_master_offset;
	u32 axi_hls_offset;
	u32 axi_ras_offset;
	u32 axi_dti_offset;
};

/**
 * struct cdns_pcie - private data for Cadence PCIe controller drivers
 * @reg_base: IO mapped register base
 * @mem_res: start/end offsets in the physical system memory to map PCI accesses
 * @msg_res: Region for send message to map PCI accesses
 * @dev: PCIe controller
 * @is_rc: tell whether the PCIe controller mode is Root Complex or Endpoint.
 * @phy_count: number of supported PHY devices
 * @phy: list of pointers to specific PHY control blocks
 * @link: list of pointers to corresponding device link representations
 * @ops: Platform-specific ops to control various inputs from Cadence PCIe
 *       wrapper
 * @cdns_pcie_reg_offsets: Register bank offsets for different SoC
 */
struct cdns_pcie {
	void __iomem		             *reg_base;
	struct resource		             *mem_res;
	struct resource                      *msg_res;
	struct device		             *dev;
	bool			             is_rc;
	int			             phy_count;
	struct phy		             **phy;
	struct device_link	             **link;
	const  struct cdns_pcie_ops          *ops;
	const  struct cdns_plat_pcie_of_data *cdns_pcie_reg_offsets;
};

/**
 * struct cdns_pcie_rc - private data for this PCIe Root Complex driver
 * @pcie: Cadence PCIe controller
 * @cfg_res: start/end offsets in the physical system memory to map PCI
 *           configuration space accesses
 * @cfg_base: IO mapped window to access the PCI configuration space of a
 *            single function at a time
 * @vendor_id: PCI vendor ID
 * @device_id: PCI device ID
 * @avail_ib_bar: Status of RP_BAR0, RP_BAR1 and RP_NO_BAR if it's free or
 *                available
 * @quirk_retrain_flag: Retrain link as quirk for PCIe Gen2
 * @quirk_detect_quiet_flag: LTSSM Detect Quiet min delay set as quirk
 * @ecam_supported: Whether the ECAM is supported
 * @no_inbound_map: Whether inbound mapping is supported
 */
struct cdns_pcie_rc {
	struct cdns_pcie	pcie;
	struct resource		*cfg_res;
	void __iomem		*cfg_base;
	u32			vendor_id;
	u32			device_id;
	bool			avail_ib_bar[CDNS_PCIE_RP_MAX_IB];
	unsigned int		quirk_retrain_flag:1;
	unsigned int		quirk_detect_quiet_flag:1;
	unsigned int            ecam_supported:1;
	unsigned int            no_inbound_map:1;
};

/**
 * struct cdns_pcie_epf - Structure to hold info about endpoint function
 * @epf: Info about virtual functions attached to the physical function
 * @epf_bar: reference to the pci_epf_bar for the six Base Address Registers
 */
struct cdns_pcie_epf {
	struct cdns_pcie_epf *epf;
	struct pci_epf_bar *epf_bar[PCI_STD_NUM_BARS];
};

/**
 * struct cdns_pcie_ep - private data for this PCIe endpoint controller driver
 * @pcie: Cadence PCIe controller
 * @max_regions: maximum number of regions supported by hardware
 * @ob_region_map: bitmask of mapped outbound regions
 * @ob_addr: base addresses in the AXI bus where the outbound regions start
 * @irq_phys_addr: base address on the AXI bus where the MSI/INTX IRQ
 *		   dedicated outbound regions is mapped.
 * @irq_cpu_addr: base address in the CPU space where a write access triggers
 *		  the sending of a memory write (MSI) / normal message (INTX
 *		  IRQ) TLP through the PCIe bus.
 * @irq_pci_addr: used to save the current mapping of the MSI/INTX IRQ
 *		  dedicated outbound region.
 * @irq_pci_fn: the latest PCI function that has updated the mapping of
 *		the MSI/INTX IRQ dedicated outbound region.
 * @irq_pending: bitmask of asserted INTX IRQs.
 * @lock: spin lock to disable interrupts while modifying PCIe controller
 *        registers fields (RMW) accessible by both remote RC and EP to
 *        minimize time between read and write
 * @epf: Structure to hold info about endpoint function
 * @quirk_detect_quiet_flag: LTSSM Detect Quiet min delay set as quirk
 * @quirk_disable_flr: Disable FLR (Function Level Reset) quirk flag
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
	/* protect writing to PCI_STATUS while raising INTX interrupts */
	spinlock_t		lock;
	struct cdns_pcie_epf	*epf;
	unsigned int		quirk_detect_quiet_flag:1;
	unsigned int		quirk_disable_flr:1;
};

static inline u32 cdns_reg_bank_to_off(struct cdns_pcie *pcie, enum cdns_pcie_reg_bank bank)
{
	u32 offset = 0x0;

	switch (bank) {
	case REG_BANK_RP:
		offset = 0;
		break;
	case REG_BANK_IP_REG:
		offset = pcie->cdns_pcie_reg_offsets->ip_reg_bank_offset;
		break;
	case REG_BANK_IP_CFG_CTRL_REG:
		offset = pcie->cdns_pcie_reg_offsets->ip_cfg_ctrl_reg_offset;
		break;
	case REG_BANK_AXI_MASTER_COMMON:
		offset = pcie->cdns_pcie_reg_offsets->axi_mstr_common_offset;
		break;
	case REG_BANK_AXI_MASTER:
		offset = pcie->cdns_pcie_reg_offsets->axi_master_offset;
		break;
	case REG_BANK_AXI_SLAVE:
		offset = pcie->cdns_pcie_reg_offsets->axi_slave_offset;
		break;
	case REG_BANK_AXI_HLS:
		offset = pcie->cdns_pcie_reg_offsets->axi_hls_offset;
		break;
	case REG_BANK_AXI_RAS:
		offset = pcie->cdns_pcie_reg_offsets->axi_ras_offset;
		break;
	case REG_BANK_AXI_DTI:
		offset = pcie->cdns_pcie_reg_offsets->axi_dti_offset;
		break;
	default:
		break;
	}
	return offset;
}

/* Register access */
static inline void cdns_pcie_writel(struct cdns_pcie *pcie, u32 reg, u32 value)
{
	writel(value, pcie->reg_base + reg);
}

static inline u32 cdns_pcie_readl(struct cdns_pcie *pcie, u32 reg)
{
	return readl(pcie->reg_base + reg);
}

static inline void cdns_pcie_hpa_writel(struct cdns_pcie *pcie,
					enum cdns_pcie_reg_bank bank,
					u32 reg,
					u32 value)
{
	u32 offset = cdns_reg_bank_to_off(pcie, bank);

	reg += offset;
	writel(value, pcie->reg_base + reg);
}

static inline u32 cdns_pcie_hpa_readl(struct cdns_pcie *pcie,
				      enum cdns_pcie_reg_bank bank,
				      u32 reg)
{
	u32 offset = cdns_reg_bank_to_off(pcie, bank);

	reg += offset;
	return readl(pcie->reg_base + reg);
}

static inline u16 cdns_pcie_readw(struct cdns_pcie *pcie, u32 reg)
{
	return readw(pcie->reg_base + reg);
}

static inline u8 cdns_pcie_readb(struct cdns_pcie *pcie, u32 reg)
{
	return readb(pcie->reg_base + reg);
}

static inline int cdns_pcie_read_cfg_byte(struct cdns_pcie *pcie, int where,
					  u8 *val)
{
	*val = cdns_pcie_readb(pcie, where);
	return PCIBIOS_SUCCESSFUL;
}

static inline int cdns_pcie_read_cfg_word(struct cdns_pcie *pcie, int where,
					  u16 *val)
{
	*val = cdns_pcie_readw(pcie, where);
	return PCIBIOS_SUCCESSFUL;
}

static inline int cdns_pcie_read_cfg_dword(struct cdns_pcie *pcie, int where,
					   u32 *val)
{
	*val = cdns_pcie_readl(pcie, where);
	return PCIBIOS_SUCCESSFUL;
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

static inline void cdns_pcie_hpa_rp_writeb(struct cdns_pcie *pcie,
					   u32 reg, u8 value)
{
	void __iomem *addr = pcie->reg_base + CDNS_PCIE_HPA_RP_BASE + reg;

	cdns_pcie_write_sz(addr, 0x1, value);
}

static inline void cdns_pcie_hpa_rp_writew(struct cdns_pcie *pcie,
					   u32 reg, u16 value)
{
	void __iomem *addr = pcie->reg_base + CDNS_PCIE_HPA_RP_BASE + reg;

	cdns_pcie_write_sz(addr, 0x2, value);
}

static inline u16 cdns_pcie_hpa_rp_readw(struct cdns_pcie *pcie, u32 reg)
{
	void __iomem *addr = pcie->reg_base + CDNS_PCIE_HPA_RP_BASE + reg;

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
	if (pcie->ops && pcie->ops->start_link)
		return pcie->ops->start_link(pcie);

	return 0;
}

static inline void cdns_pcie_stop_link(struct cdns_pcie *pcie)
{
	if (pcie->ops && pcie->ops->stop_link)
		pcie->ops->stop_link(pcie);
}

static inline bool cdns_pcie_link_up(struct cdns_pcie *pcie)
{
	if (pcie->ops && pcie->ops->link_up)
		return pcie->ops->link_up(pcie);

	return true;
}

#if IS_ENABLED(CONFIG_PCIE_CADENCE_HOST)
int cdns_pcie_host_link_setup(struct cdns_pcie_rc *rc);
int cdns_pcie_host_init(struct cdns_pcie_rc *rc);
int cdns_pcie_host_setup(struct cdns_pcie_rc *rc);
void cdns_pcie_host_disable(struct cdns_pcie_rc *rc);
void __iomem *cdns_pci_map_bus(struct pci_bus *bus, unsigned int devfn,
			       int where);
int cdns_pcie_hpa_host_setup(struct cdns_pcie_rc *rc);
#else
static inline int cdns_pcie_host_link_setup(struct cdns_pcie_rc *rc)
{
	return 0;
}

static inline int cdns_pcie_host_init(struct cdns_pcie_rc *rc)
{
	return 0;
}

static inline int cdns_pcie_host_setup(struct cdns_pcie_rc *rc)
{
	return 0;
}

static inline int cdns_pcie_hpa_host_setup(struct cdns_pcie_rc *rc)
{
	return 0;
}

static inline void cdns_pcie_host_disable(struct cdns_pcie_rc *rc)
{
}

static inline void __iomem *cdns_pci_map_bus(struct pci_bus *bus, unsigned int devfn,
					     int where)
{
	return NULL;
}
#endif

#if IS_ENABLED(CONFIG_PCIE_CADENCE_EP)
int cdns_pcie_ep_setup(struct cdns_pcie_ep *ep);
void cdns_pcie_ep_disable(struct cdns_pcie_ep *ep);
int cdns_pcie_hpa_ep_setup(struct cdns_pcie_ep *ep);
#else
static inline int cdns_pcie_ep_setup(struct cdns_pcie_ep *ep)
{
	return 0;
}

static inline void cdns_pcie_ep_disable(struct cdns_pcie_ep *ep)
{
}

static inline int cdns_pcie_hpa_ep_setup(struct cdns_pcie_ep *ep)
{
	return 0;
}

#endif

u8   cdns_pcie_find_capability(struct cdns_pcie *pcie, u8 cap);
u16  cdns_pcie_find_ext_capability(struct cdns_pcie *pcie, u8 cap);
bool cdns_pcie_linkup(struct cdns_pcie *pcie);

void cdns_pcie_detect_quiet_min_delay_set(struct cdns_pcie *pcie);

void cdns_pcie_set_outbound_region(struct cdns_pcie *pcie, u8 busnr, u8 fn,
				   u32 r, bool is_io,
				   u64 cpu_addr, u64 pci_addr, size_t size);

void cdns_pcie_set_outbound_region_for_normal_msg(struct cdns_pcie *pcie,
						  u8 busnr, u8 fn,
						  u32 r, u64 cpu_addr);

void cdns_pcie_reset_outbound_region(struct cdns_pcie *pcie, u32 r);
void cdns_pcie_disable_phy(struct cdns_pcie *pcie);
int  cdns_pcie_enable_phy(struct cdns_pcie *pcie);
int  cdns_pcie_init_phy(struct device *dev, struct cdns_pcie *pcie);
void cdns_pcie_hpa_detect_quiet_min_delay_set(struct cdns_pcie *pcie);
void cdns_pcie_hpa_set_outbound_region(struct cdns_pcie *pcie, u8 busnr, u8 fn,
				       u32 r, bool is_io,
				       u64 cpu_addr, u64 pci_addr, size_t size);
void cdns_pcie_hpa_set_outbound_region_for_normal_msg(struct cdns_pcie *pcie,
						      u8 busnr, u8 fn,
						      u32 r, u64 cpu_addr);
int  cdns_pcie_hpa_host_link_setup(struct cdns_pcie_rc *rc);
void __iomem *cdns_pci_hpa_map_bus(struct pci_bus *bus, unsigned int devfn,
				   int where);
int  cdns_pcie_hpa_host_start_link(struct cdns_pcie_rc *rc);
int  cdns_pcie_hpa_start_link(struct cdns_pcie *pcie);
void cdns_pcie_hpa_stop_link(struct cdns_pcie *pcie);
bool cdns_pcie_hpa_link_up(struct cdns_pcie *pcie);

extern const struct dev_pm_ops cdns_pcie_pm_ops;

#endif /* _PCIE_CADENCE_H */
