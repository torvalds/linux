// SPDX-License-Identifier: GPL-2.0
/**
 * Synopsys DesignWare PCIe Endpoint controller driver
 *
 * Copyright (C) 2017 Texas Instruments
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

#include <linux/of.h>

#include "pcie-designware.h"
#include <linux/pci-epc.h>
#include <linux/pci-epf.h>

void dw_pcie_ep_linkup(struct dw_pcie_ep *ep)
{
	struct pci_epc *epc = ep->epc;

	pci_epc_linkup(epc);
}

static void __dw_pcie_ep_reset_bar(struct dw_pcie *pci, enum pci_barno bar,
				   int flags)
{
	u32 reg;

	reg = PCI_BASE_ADDRESS_0 + (4 * bar);
	dw_pcie_dbi_ro_wr_en(pci);
	dw_pcie_writel_dbi2(pci, reg, 0x0);
	dw_pcie_writel_dbi(pci, reg, 0x0);
	if (flags & PCI_BASE_ADDRESS_MEM_TYPE_64) {
		dw_pcie_writel_dbi2(pci, reg + 4, 0x0);
		dw_pcie_writel_dbi(pci, reg + 4, 0x0);
	}
	dw_pcie_dbi_ro_wr_dis(pci);
}

void dw_pcie_ep_reset_bar(struct dw_pcie *pci, enum pci_barno bar)
{
	__dw_pcie_ep_reset_bar(pci, bar, 0);
}

static u8 __dw_pcie_ep_find_next_cap(struct dw_pcie *pci, u8 cap_ptr,
			      u8 cap)
{
	u8 cap_id, next_cap_ptr;
	u16 reg;

	reg = dw_pcie_readw_dbi(pci, cap_ptr);
	next_cap_ptr = (reg & 0xff00) >> 8;
	cap_id = (reg & 0x00ff);

	if (!next_cap_ptr || cap_id > PCI_CAP_ID_MAX)
		return 0;

	if (cap_id == cap)
		return cap_ptr;

	return __dw_pcie_ep_find_next_cap(pci, next_cap_ptr, cap);
}

static u8 dw_pcie_ep_find_capability(struct dw_pcie *pci, u8 cap)
{
	u8 next_cap_ptr;
	u16 reg;

	reg = dw_pcie_readw_dbi(pci, PCI_CAPABILITY_LIST);
	next_cap_ptr = (reg & 0x00ff);

	if (!next_cap_ptr)
		return 0;

	return __dw_pcie_ep_find_next_cap(pci, next_cap_ptr, cap);
}

static int dw_pcie_ep_write_header(struct pci_epc *epc, u8 func_no,
				   struct pci_epf_header *hdr)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	dw_pcie_dbi_ro_wr_en(pci);
	dw_pcie_writew_dbi(pci, PCI_VENDOR_ID, hdr->vendorid);
	dw_pcie_writew_dbi(pci, PCI_DEVICE_ID, hdr->deviceid);
	dw_pcie_writeb_dbi(pci, PCI_REVISION_ID, hdr->revid);
	dw_pcie_writeb_dbi(pci, PCI_CLASS_PROG, hdr->progif_code);
	dw_pcie_writew_dbi(pci, PCI_CLASS_DEVICE,
			   hdr->subclass_code | hdr->baseclass_code << 8);
	dw_pcie_writeb_dbi(pci, PCI_CACHE_LINE_SIZE,
			   hdr->cache_line_size);
	dw_pcie_writew_dbi(pci, PCI_SUBSYSTEM_VENDOR_ID,
			   hdr->subsys_vendor_id);
	dw_pcie_writew_dbi(pci, PCI_SUBSYSTEM_ID, hdr->subsys_id);
	dw_pcie_writeb_dbi(pci, PCI_INTERRUPT_PIN,
			   hdr->interrupt_pin);
	dw_pcie_dbi_ro_wr_dis(pci);

	return 0;
}

static int dw_pcie_ep_inbound_atu(struct dw_pcie_ep *ep, enum pci_barno bar,
				  dma_addr_t cpu_addr,
				  enum dw_pcie_as_type as_type)
{
	int ret;
	u32 free_win;
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	free_win = find_first_zero_bit(ep->ib_window_map, ep->num_ib_windows);
	if (free_win >= ep->num_ib_windows) {
		dev_err(pci->dev, "No free inbound window\n");
		return -EINVAL;
	}

	ret = dw_pcie_prog_inbound_atu(pci, free_win, bar, cpu_addr,
				       as_type);
	if (ret < 0) {
		dev_err(pci->dev, "Failed to program IB window\n");
		return ret;
	}

	ep->bar_to_atu[bar] = free_win;
	set_bit(free_win, ep->ib_window_map);

	return 0;
}

static int dw_pcie_ep_outbound_atu(struct dw_pcie_ep *ep, phys_addr_t phys_addr,
				   u64 pci_addr, size_t size)
{
	u32 free_win;
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	free_win = find_first_zero_bit(ep->ob_window_map, ep->num_ob_windows);
	if (free_win >= ep->num_ob_windows) {
		dev_err(pci->dev, "No free outbound window\n");
		return -EINVAL;
	}

	dw_pcie_prog_outbound_atu(pci, free_win, PCIE_ATU_TYPE_MEM,
				  phys_addr, pci_addr, size);

	set_bit(free_win, ep->ob_window_map);
	ep->outbound_addr[free_win] = phys_addr;

	return 0;
}

static void dw_pcie_ep_clear_bar(struct pci_epc *epc, u8 func_no,
				 struct pci_epf_bar *epf_bar)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	enum pci_barno bar = epf_bar->barno;
	u32 atu_index = ep->bar_to_atu[bar];

	__dw_pcie_ep_reset_bar(pci, bar, epf_bar->flags);

	dw_pcie_disable_atu(pci, atu_index, DW_PCIE_REGION_INBOUND);
	clear_bit(atu_index, ep->ib_window_map);
}

static int dw_pcie_ep_set_bar(struct pci_epc *epc, u8 func_no,
			      struct pci_epf_bar *epf_bar)
{
	int ret;
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	enum pci_barno bar = epf_bar->barno;
	size_t size = epf_bar->size;
	int flags = epf_bar->flags;
	enum dw_pcie_as_type as_type;
	u32 reg = PCI_BASE_ADDRESS_0 + (4 * bar);

	if (!(flags & PCI_BASE_ADDRESS_SPACE))
		as_type = DW_PCIE_AS_MEM;
	else
		as_type = DW_PCIE_AS_IO;

	ret = dw_pcie_ep_inbound_atu(ep, bar, epf_bar->phys_addr, as_type);
	if (ret)
		return ret;

	dw_pcie_dbi_ro_wr_en(pci);

	dw_pcie_writel_dbi2(pci, reg, lower_32_bits(size - 1));
	dw_pcie_writel_dbi(pci, reg, flags);

	if (flags & PCI_BASE_ADDRESS_MEM_TYPE_64) {
		dw_pcie_writel_dbi2(pci, reg + 4, upper_32_bits(size - 1));
		dw_pcie_writel_dbi(pci, reg + 4, 0);
	}

	dw_pcie_dbi_ro_wr_dis(pci);

	return 0;
}

static int dw_pcie_find_index(struct dw_pcie_ep *ep, phys_addr_t addr,
			      u32 *atu_index)
{
	u32 index;

	for (index = 0; index < ep->num_ob_windows; index++) {
		if (ep->outbound_addr[index] != addr)
			continue;
		*atu_index = index;
		return 0;
	}

	return -EINVAL;
}

static void dw_pcie_ep_unmap_addr(struct pci_epc *epc, u8 func_no,
				  phys_addr_t addr)
{
	int ret;
	u32 atu_index;
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	ret = dw_pcie_find_index(ep, addr, &atu_index);
	if (ret < 0)
		return;

	dw_pcie_disable_atu(pci, atu_index, DW_PCIE_REGION_OUTBOUND);
	clear_bit(atu_index, ep->ob_window_map);
}

static int dw_pcie_ep_map_addr(struct pci_epc *epc, u8 func_no,
			       phys_addr_t addr,
			       u64 pci_addr, size_t size)
{
	int ret;
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	ret = dw_pcie_ep_outbound_atu(ep, addr, pci_addr, size);
	if (ret) {
		dev_err(pci->dev, "Failed to enable address\n");
		return ret;
	}

	return 0;
}

static int dw_pcie_ep_get_msi(struct pci_epc *epc, u8 func_no)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	u32 val, reg;

	if (!ep->msi_cap)
		return -EINVAL;

	reg = ep->msi_cap + PCI_MSI_FLAGS;
	val = dw_pcie_readw_dbi(pci, reg);
	if (!(val & PCI_MSI_FLAGS_ENABLE))
		return -EINVAL;

	val = (val & PCI_MSI_FLAGS_QSIZE) >> 4;

	return val;
}

static int dw_pcie_ep_set_msi(struct pci_epc *epc, u8 func_no, u8 interrupts)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	u32 val, reg;

	if (!ep->msi_cap)
		return -EINVAL;

	reg = ep->msi_cap + PCI_MSI_FLAGS;
	val = dw_pcie_readw_dbi(pci, reg);
	val &= ~PCI_MSI_FLAGS_QMASK;
	val |= (interrupts << 1) & PCI_MSI_FLAGS_QMASK;
	dw_pcie_dbi_ro_wr_en(pci);
	dw_pcie_writew_dbi(pci, reg, val);
	dw_pcie_dbi_ro_wr_dis(pci);

	return 0;
}

static int dw_pcie_ep_get_msix(struct pci_epc *epc, u8 func_no)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	u32 val, reg;

	if (!ep->msix_cap)
		return -EINVAL;

	reg = ep->msix_cap + PCI_MSIX_FLAGS;
	val = dw_pcie_readw_dbi(pci, reg);
	if (!(val & PCI_MSIX_FLAGS_ENABLE))
		return -EINVAL;

	val &= PCI_MSIX_FLAGS_QSIZE;

	return val;
}

static int dw_pcie_ep_set_msix(struct pci_epc *epc, u8 func_no, u16 interrupts)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	u32 val, reg;

	if (!ep->msix_cap)
		return -EINVAL;

	reg = ep->msix_cap + PCI_MSIX_FLAGS;
	val = dw_pcie_readw_dbi(pci, reg);
	val &= ~PCI_MSIX_FLAGS_QSIZE;
	val |= interrupts;
	dw_pcie_dbi_ro_wr_en(pci);
	dw_pcie_writew_dbi(pci, reg, val);
	dw_pcie_dbi_ro_wr_dis(pci);

	return 0;
}

static int dw_pcie_ep_raise_irq(struct pci_epc *epc, u8 func_no,
				enum pci_epc_irq_type type, u16 interrupt_num)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);

	if (!ep->ops->raise_irq)
		return -EINVAL;

	return ep->ops->raise_irq(ep, func_no, type, interrupt_num);
}

static void dw_pcie_ep_stop(struct pci_epc *epc)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	if (!pci->ops->stop_link)
		return;

	pci->ops->stop_link(pci);
}

static int dw_pcie_ep_start(struct pci_epc *epc)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	if (!pci->ops->start_link)
		return -EINVAL;

	return pci->ops->start_link(pci);
}

static const struct pci_epc_features*
dw_pcie_ep_get_features(struct pci_epc *epc, u8 func_no)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);

	if (!ep->ops->get_features)
		return NULL;

	return ep->ops->get_features(ep);
}

static const struct pci_epc_ops epc_ops = {
	.write_header		= dw_pcie_ep_write_header,
	.set_bar		= dw_pcie_ep_set_bar,
	.clear_bar		= dw_pcie_ep_clear_bar,
	.map_addr		= dw_pcie_ep_map_addr,
	.unmap_addr		= dw_pcie_ep_unmap_addr,
	.set_msi		= dw_pcie_ep_set_msi,
	.get_msi		= dw_pcie_ep_get_msi,
	.set_msix		= dw_pcie_ep_set_msix,
	.get_msix		= dw_pcie_ep_get_msix,
	.raise_irq		= dw_pcie_ep_raise_irq,
	.start			= dw_pcie_ep_start,
	.stop			= dw_pcie_ep_stop,
	.get_features		= dw_pcie_ep_get_features,
};

int dw_pcie_ep_raise_legacy_irq(struct dw_pcie_ep *ep, u8 func_no)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct device *dev = pci->dev;

	dev_err(dev, "EP cannot trigger legacy IRQs\n");

	return -EINVAL;
}

int dw_pcie_ep_raise_msi_irq(struct dw_pcie_ep *ep, u8 func_no,
			     u8 interrupt_num)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct pci_epc *epc = ep->epc;
	u16 msg_ctrl, msg_data;
	u32 msg_addr_lower, msg_addr_upper, reg;
	u64 msg_addr;
	bool has_upper;
	int ret;

	if (!ep->msi_cap)
		return -EINVAL;

	/* Raise MSI per the PCI Local Bus Specification Revision 3.0, 6.8.1. */
	reg = ep->msi_cap + PCI_MSI_FLAGS;
	msg_ctrl = dw_pcie_readw_dbi(pci, reg);
	has_upper = !!(msg_ctrl & PCI_MSI_FLAGS_64BIT);
	reg = ep->msi_cap + PCI_MSI_ADDRESS_LO;
	msg_addr_lower = dw_pcie_readl_dbi(pci, reg);
	if (has_upper) {
		reg = ep->msi_cap + PCI_MSI_ADDRESS_HI;
		msg_addr_upper = dw_pcie_readl_dbi(pci, reg);
		reg = ep->msi_cap + PCI_MSI_DATA_64;
		msg_data = dw_pcie_readw_dbi(pci, reg);
	} else {
		msg_addr_upper = 0;
		reg = ep->msi_cap + PCI_MSI_DATA_32;
		msg_data = dw_pcie_readw_dbi(pci, reg);
	}
	msg_addr = ((u64) msg_addr_upper) << 32 | msg_addr_lower;
	ret = dw_pcie_ep_map_addr(epc, func_no, ep->msi_mem_phys, msg_addr,
				  epc->mem->page_size);
	if (ret)
		return ret;

	writel(msg_data | (interrupt_num - 1), ep->msi_mem);

	dw_pcie_ep_unmap_addr(epc, func_no, ep->msi_mem_phys);

	return 0;
}

int dw_pcie_ep_raise_msix_irq(struct dw_pcie_ep *ep, u8 func_no,
			     u16 interrupt_num)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct pci_epc *epc = ep->epc;
	u16 tbl_offset, bir;
	u32 bar_addr_upper, bar_addr_lower;
	u32 msg_addr_upper, msg_addr_lower;
	u32 reg, msg_data, vec_ctrl;
	u64 tbl_addr, msg_addr, reg_u64;
	void __iomem *msix_tbl;
	int ret;

	reg = ep->msix_cap + PCI_MSIX_TABLE;
	tbl_offset = dw_pcie_readl_dbi(pci, reg);
	bir = (tbl_offset & PCI_MSIX_TABLE_BIR);
	tbl_offset &= PCI_MSIX_TABLE_OFFSET;

	reg = PCI_BASE_ADDRESS_0 + (4 * bir);
	bar_addr_upper = 0;
	bar_addr_lower = dw_pcie_readl_dbi(pci, reg);
	reg_u64 = (bar_addr_lower & PCI_BASE_ADDRESS_MEM_TYPE_MASK);
	if (reg_u64 == PCI_BASE_ADDRESS_MEM_TYPE_64)
		bar_addr_upper = dw_pcie_readl_dbi(pci, reg + 4);

	tbl_addr = ((u64) bar_addr_upper) << 32 | bar_addr_lower;
	tbl_addr += (tbl_offset + ((interrupt_num - 1) * PCI_MSIX_ENTRY_SIZE));
	tbl_addr &= PCI_BASE_ADDRESS_MEM_MASK;

	msix_tbl = ioremap_nocache(ep->phys_base + tbl_addr,
				   PCI_MSIX_ENTRY_SIZE);
	if (!msix_tbl)
		return -EINVAL;

	msg_addr_lower = readl(msix_tbl + PCI_MSIX_ENTRY_LOWER_ADDR);
	msg_addr_upper = readl(msix_tbl + PCI_MSIX_ENTRY_UPPER_ADDR);
	msg_addr = ((u64) msg_addr_upper) << 32 | msg_addr_lower;
	msg_data = readl(msix_tbl + PCI_MSIX_ENTRY_DATA);
	vec_ctrl = readl(msix_tbl + PCI_MSIX_ENTRY_VECTOR_CTRL);

	iounmap(msix_tbl);

	if (vec_ctrl & PCI_MSIX_ENTRY_CTRL_MASKBIT) {
		dev_dbg(pci->dev, "MSI-X entry ctrl set\n");
		return -EPERM;
	}

	ret = dw_pcie_ep_map_addr(epc, func_no, ep->msi_mem_phys, msg_addr,
				  epc->mem->page_size);
	if (ret)
		return ret;

	writel(msg_data, ep->msi_mem);

	dw_pcie_ep_unmap_addr(epc, func_no, ep->msi_mem_phys);

	return 0;
}

void dw_pcie_ep_exit(struct dw_pcie_ep *ep)
{
	struct pci_epc *epc = ep->epc;

	pci_epc_mem_free_addr(epc, ep->msi_mem_phys, ep->msi_mem,
			      epc->mem->page_size);

	pci_epc_mem_exit(epc);
}

int dw_pcie_ep_init(struct dw_pcie_ep *ep)
{
	int ret;
	void *addr;
	struct pci_epc *epc;
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct device *dev = pci->dev;
	struct device_node *np = dev->of_node;

	if (!pci->dbi_base || !pci->dbi_base2) {
		dev_err(dev, "dbi_base/dbi_base2 is not populated\n");
		return -EINVAL;
	}
	if (pci->iatu_unroll_enabled && !pci->atu_base) {
		dev_err(dev, "atu_base is not populated\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(np, "num-ib-windows", &ep->num_ib_windows);
	if (ret < 0) {
		dev_err(dev, "Unable to read *num-ib-windows* property\n");
		return ret;
	}
	if (ep->num_ib_windows > MAX_IATU_IN) {
		dev_err(dev, "Invalid *num-ib-windows*\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(np, "num-ob-windows", &ep->num_ob_windows);
	if (ret < 0) {
		dev_err(dev, "Unable to read *num-ob-windows* property\n");
		return ret;
	}
	if (ep->num_ob_windows > MAX_IATU_OUT) {
		dev_err(dev, "Invalid *num-ob-windows*\n");
		return -EINVAL;
	}

	ep->ib_window_map = devm_kcalloc(dev,
					 BITS_TO_LONGS(ep->num_ib_windows),
					 sizeof(long),
					 GFP_KERNEL);
	if (!ep->ib_window_map)
		return -ENOMEM;

	ep->ob_window_map = devm_kcalloc(dev,
					 BITS_TO_LONGS(ep->num_ob_windows),
					 sizeof(long),
					 GFP_KERNEL);
	if (!ep->ob_window_map)
		return -ENOMEM;

	addr = devm_kcalloc(dev, ep->num_ob_windows, sizeof(phys_addr_t),
			    GFP_KERNEL);
	if (!addr)
		return -ENOMEM;
	ep->outbound_addr = addr;

	epc = devm_pci_epc_create(dev, &epc_ops);
	if (IS_ERR(epc)) {
		dev_err(dev, "Failed to create epc device\n");
		return PTR_ERR(epc);
	}

	ep->epc = epc;
	epc_set_drvdata(epc, ep);

	if (ep->ops->ep_init)
		ep->ops->ep_init(ep);

	ret = of_property_read_u8(np, "max-functions", &epc->max_functions);
	if (ret < 0)
		epc->max_functions = 1;

	ret = __pci_epc_mem_init(epc, ep->phys_base, ep->addr_size,
				 ep->page_size);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize address space\n");
		return ret;
	}

	ep->msi_mem = pci_epc_mem_alloc_addr(epc, &ep->msi_mem_phys,
					     epc->mem->page_size);
	if (!ep->msi_mem) {
		dev_err(dev, "Failed to reserve memory for MSI/MSI-X\n");
		return -ENOMEM;
	}
	ep->msi_cap = dw_pcie_ep_find_capability(pci, PCI_CAP_ID_MSI);

	ep->msix_cap = dw_pcie_ep_find_capability(pci, PCI_CAP_ID_MSIX);

	dw_pcie_setup(pci);

	return 0;
}
