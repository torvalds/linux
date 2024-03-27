// SPDX-License-Identifier: GPL-2.0
/*
 * Synopsys DesignWare PCIe Endpoint controller driver
 *
 * Copyright (C) 2017 Texas Instruments
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

#include <linux/align.h>
#include <linux/bitfield.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "pcie-designware.h"
#include <linux/pci-epc.h>
#include <linux/pci-epf.h>

void dw_pcie_ep_linkup(struct dw_pcie_ep *ep)
{
	struct pci_epc *epc = ep->epc;

	pci_epc_linkup(epc);
}
EXPORT_SYMBOL_GPL(dw_pcie_ep_linkup);

void dw_pcie_ep_init_notify(struct dw_pcie_ep *ep)
{
	struct pci_epc *epc = ep->epc;

	pci_epc_init_notify(epc);
}
EXPORT_SYMBOL_GPL(dw_pcie_ep_init_notify);

struct dw_pcie_ep_func *
dw_pcie_ep_get_func_from_ep(struct dw_pcie_ep *ep, u8 func_no)
{
	struct dw_pcie_ep_func *ep_func;

	list_for_each_entry(ep_func, &ep->func_list, list) {
		if (ep_func->func_no == func_no)
			return ep_func;
	}

	return NULL;
}

static void __dw_pcie_ep_reset_bar(struct dw_pcie *pci, u8 func_no,
				   enum pci_barno bar, int flags)
{
	struct dw_pcie_ep *ep = &pci->ep;
	u32 reg;

	reg = PCI_BASE_ADDRESS_0 + (4 * bar);
	dw_pcie_dbi_ro_wr_en(pci);
	dw_pcie_ep_writel_dbi2(ep, func_no, reg, 0x0);
	dw_pcie_ep_writel_dbi(ep, func_no, reg, 0x0);
	if (flags & PCI_BASE_ADDRESS_MEM_TYPE_64) {
		dw_pcie_ep_writel_dbi2(ep, func_no, reg + 4, 0x0);
		dw_pcie_ep_writel_dbi(ep, func_no, reg + 4, 0x0);
	}
	dw_pcie_dbi_ro_wr_dis(pci);
}

void dw_pcie_ep_reset_bar(struct dw_pcie *pci, enum pci_barno bar)
{
	u8 func_no, funcs;

	funcs = pci->ep.epc->max_functions;

	for (func_no = 0; func_no < funcs; func_no++)
		__dw_pcie_ep_reset_bar(pci, func_no, bar, 0);
}
EXPORT_SYMBOL_GPL(dw_pcie_ep_reset_bar);

static u8 __dw_pcie_ep_find_next_cap(struct dw_pcie_ep *ep, u8 func_no,
				     u8 cap_ptr, u8 cap)
{
	u8 cap_id, next_cap_ptr;
	u16 reg;

	if (!cap_ptr)
		return 0;

	reg = dw_pcie_ep_readw_dbi(ep, func_no, cap_ptr);
	cap_id = (reg & 0x00ff);

	if (cap_id > PCI_CAP_ID_MAX)
		return 0;

	if (cap_id == cap)
		return cap_ptr;

	next_cap_ptr = (reg & 0xff00) >> 8;
	return __dw_pcie_ep_find_next_cap(ep, func_no, next_cap_ptr, cap);
}

static u8 dw_pcie_ep_find_capability(struct dw_pcie_ep *ep, u8 func_no, u8 cap)
{
	u8 next_cap_ptr;
	u16 reg;

	reg = dw_pcie_ep_readw_dbi(ep, func_no, PCI_CAPABILITY_LIST);
	next_cap_ptr = (reg & 0x00ff);

	return __dw_pcie_ep_find_next_cap(ep, func_no, next_cap_ptr, cap);
}

static int dw_pcie_ep_write_header(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				   struct pci_epf_header *hdr)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	dw_pcie_dbi_ro_wr_en(pci);
	dw_pcie_ep_writew_dbi(ep, func_no, PCI_VENDOR_ID, hdr->vendorid);
	dw_pcie_ep_writew_dbi(ep, func_no, PCI_DEVICE_ID, hdr->deviceid);
	dw_pcie_ep_writeb_dbi(ep, func_no, PCI_REVISION_ID, hdr->revid);
	dw_pcie_ep_writeb_dbi(ep, func_no, PCI_CLASS_PROG, hdr->progif_code);
	dw_pcie_ep_writew_dbi(ep, func_no, PCI_CLASS_DEVICE,
			      hdr->subclass_code | hdr->baseclass_code << 8);
	dw_pcie_ep_writeb_dbi(ep, func_no, PCI_CACHE_LINE_SIZE,
			      hdr->cache_line_size);
	dw_pcie_ep_writew_dbi(ep, func_no, PCI_SUBSYSTEM_VENDOR_ID,
			      hdr->subsys_vendor_id);
	dw_pcie_ep_writew_dbi(ep, func_no, PCI_SUBSYSTEM_ID, hdr->subsys_id);
	dw_pcie_ep_writeb_dbi(ep, func_no, PCI_INTERRUPT_PIN,
			      hdr->interrupt_pin);
	dw_pcie_dbi_ro_wr_dis(pci);

	return 0;
}

static int dw_pcie_ep_inbound_atu(struct dw_pcie_ep *ep, u8 func_no, int type,
				  dma_addr_t cpu_addr, enum pci_barno bar)
{
	int ret;
	u32 free_win;
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	if (!ep->bar_to_atu[bar])
		free_win = find_first_zero_bit(ep->ib_window_map, pci->num_ib_windows);
	else
		free_win = ep->bar_to_atu[bar];

	if (free_win >= pci->num_ib_windows) {
		dev_err(pci->dev, "No free inbound window\n");
		return -EINVAL;
	}

	ret = dw_pcie_prog_ep_inbound_atu(pci, func_no, free_win, type,
					  cpu_addr, bar);
	if (ret < 0) {
		dev_err(pci->dev, "Failed to program IB window\n");
		return ret;
	}

	ep->bar_to_atu[bar] = free_win;
	set_bit(free_win, ep->ib_window_map);

	return 0;
}

static int dw_pcie_ep_outbound_atu(struct dw_pcie_ep *ep, u8 func_no,
				   phys_addr_t phys_addr,
				   u64 pci_addr, size_t size)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	u32 free_win;
	int ret;

	free_win = find_first_zero_bit(ep->ob_window_map, pci->num_ob_windows);
	if (free_win >= pci->num_ob_windows) {
		dev_err(pci->dev, "No free outbound window\n");
		return -EINVAL;
	}

	ret = dw_pcie_prog_ep_outbound_atu(pci, func_no, free_win, PCIE_ATU_TYPE_MEM,
					   phys_addr, pci_addr, size);
	if (ret)
		return ret;

	set_bit(free_win, ep->ob_window_map);
	ep->outbound_addr[free_win] = phys_addr;

	return 0;
}

static void dw_pcie_ep_clear_bar(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				 struct pci_epf_bar *epf_bar)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	enum pci_barno bar = epf_bar->barno;
	u32 atu_index = ep->bar_to_atu[bar];

	__dw_pcie_ep_reset_bar(pci, func_no, bar, epf_bar->flags);

	dw_pcie_disable_atu(pci, PCIE_ATU_REGION_DIR_IB, atu_index);
	clear_bit(atu_index, ep->ib_window_map);
	ep->epf_bar[bar] = NULL;
	ep->bar_to_atu[bar] = 0;
}

static int dw_pcie_ep_set_bar(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			      struct pci_epf_bar *epf_bar)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	enum pci_barno bar = epf_bar->barno;
	size_t size = epf_bar->size;
	int flags = epf_bar->flags;
	int ret, type;
	u32 reg;

	reg = PCI_BASE_ADDRESS_0 + (4 * bar);

	if (!(flags & PCI_BASE_ADDRESS_SPACE))
		type = PCIE_ATU_TYPE_MEM;
	else
		type = PCIE_ATU_TYPE_IO;

	ret = dw_pcie_ep_inbound_atu(ep, func_no, type, epf_bar->phys_addr, bar);
	if (ret)
		return ret;

	if (ep->epf_bar[bar])
		return 0;

	dw_pcie_dbi_ro_wr_en(pci);

	dw_pcie_ep_writel_dbi2(ep, func_no, reg, lower_32_bits(size - 1));
	dw_pcie_ep_writel_dbi(ep, func_no, reg, flags);

	if (flags & PCI_BASE_ADDRESS_MEM_TYPE_64) {
		dw_pcie_ep_writel_dbi2(ep, func_no, reg + 4, upper_32_bits(size - 1));
		dw_pcie_ep_writel_dbi(ep, func_no, reg + 4, 0);
	}

	ep->epf_bar[bar] = epf_bar;
	dw_pcie_dbi_ro_wr_dis(pci);

	return 0;
}

static int dw_pcie_find_index(struct dw_pcie_ep *ep, phys_addr_t addr,
			      u32 *atu_index)
{
	u32 index;
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	for (index = 0; index < pci->num_ob_windows; index++) {
		if (ep->outbound_addr[index] != addr)
			continue;
		*atu_index = index;
		return 0;
	}

	return -EINVAL;
}

static void dw_pcie_ep_unmap_addr(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				  phys_addr_t addr)
{
	int ret;
	u32 atu_index;
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	ret = dw_pcie_find_index(ep, addr, &atu_index);
	if (ret < 0)
		return;

	dw_pcie_disable_atu(pci, PCIE_ATU_REGION_DIR_OB, atu_index);
	clear_bit(atu_index, ep->ob_window_map);
}

static int dw_pcie_ep_map_addr(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			       phys_addr_t addr, u64 pci_addr, size_t size)
{
	int ret;
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	ret = dw_pcie_ep_outbound_atu(ep, func_no, addr, pci_addr, size);
	if (ret) {
		dev_err(pci->dev, "Failed to enable address\n");
		return ret;
	}

	return 0;
}

static int dw_pcie_ep_get_msi(struct pci_epc *epc, u8 func_no, u8 vfunc_no)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie_ep_func *ep_func;
	u32 val, reg;

	ep_func = dw_pcie_ep_get_func_from_ep(ep, func_no);
	if (!ep_func || !ep_func->msi_cap)
		return -EINVAL;

	reg = ep_func->msi_cap + PCI_MSI_FLAGS;
	val = dw_pcie_ep_readw_dbi(ep, func_no, reg);
	if (!(val & PCI_MSI_FLAGS_ENABLE))
		return -EINVAL;

	val = FIELD_GET(PCI_MSI_FLAGS_QSIZE, val);

	return val;
}

static int dw_pcie_ep_set_msi(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			      u8 interrupts)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct dw_pcie_ep_func *ep_func;
	u32 val, reg;

	ep_func = dw_pcie_ep_get_func_from_ep(ep, func_no);
	if (!ep_func || !ep_func->msi_cap)
		return -EINVAL;

	reg = ep_func->msi_cap + PCI_MSI_FLAGS;
	val = dw_pcie_ep_readw_dbi(ep, func_no, reg);
	val &= ~PCI_MSI_FLAGS_QMASK;
	val |= FIELD_PREP(PCI_MSI_FLAGS_QMASK, interrupts);
	dw_pcie_dbi_ro_wr_en(pci);
	dw_pcie_ep_writew_dbi(ep, func_no, reg, val);
	dw_pcie_dbi_ro_wr_dis(pci);

	return 0;
}

static int dw_pcie_ep_get_msix(struct pci_epc *epc, u8 func_no, u8 vfunc_no)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie_ep_func *ep_func;
	u32 val, reg;

	ep_func = dw_pcie_ep_get_func_from_ep(ep, func_no);
	if (!ep_func || !ep_func->msix_cap)
		return -EINVAL;

	reg = ep_func->msix_cap + PCI_MSIX_FLAGS;
	val = dw_pcie_ep_readw_dbi(ep, func_no, reg);
	if (!(val & PCI_MSIX_FLAGS_ENABLE))
		return -EINVAL;

	val &= PCI_MSIX_FLAGS_QSIZE;

	return val;
}

static int dw_pcie_ep_set_msix(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			       u16 interrupts, enum pci_barno bir, u32 offset)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct dw_pcie_ep_func *ep_func;
	u32 val, reg;

	ep_func = dw_pcie_ep_get_func_from_ep(ep, func_no);
	if (!ep_func || !ep_func->msix_cap)
		return -EINVAL;

	dw_pcie_dbi_ro_wr_en(pci);

	reg = ep_func->msix_cap + PCI_MSIX_FLAGS;
	val = dw_pcie_ep_readw_dbi(ep, func_no, reg);
	val &= ~PCI_MSIX_FLAGS_QSIZE;
	val |= interrupts;
	dw_pcie_writew_dbi(pci, reg, val);

	reg = ep_func->msix_cap + PCI_MSIX_TABLE;
	val = offset | bir;
	dw_pcie_ep_writel_dbi(ep, func_no, reg, val);

	reg = ep_func->msix_cap + PCI_MSIX_PBA;
	val = (offset + (interrupts * PCI_MSIX_ENTRY_SIZE)) | bir;
	dw_pcie_ep_writel_dbi(ep, func_no, reg, val);

	dw_pcie_dbi_ro_wr_dis(pci);

	return 0;
}

static int dw_pcie_ep_raise_irq(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				unsigned int type, u16 interrupt_num)
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

	dw_pcie_stop_link(pci);
}

static int dw_pcie_ep_start(struct pci_epc *epc)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	return dw_pcie_start_link(pci);
}

static const struct pci_epc_features*
dw_pcie_ep_get_features(struct pci_epc *epc, u8 func_no, u8 vfunc_no)
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

int dw_pcie_ep_raise_intx_irq(struct dw_pcie_ep *ep, u8 func_no)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct device *dev = pci->dev;

	dev_err(dev, "EP cannot raise INTX IRQs\n");

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(dw_pcie_ep_raise_intx_irq);

int dw_pcie_ep_raise_msi_irq(struct dw_pcie_ep *ep, u8 func_no,
			     u8 interrupt_num)
{
	u32 msg_addr_lower, msg_addr_upper, reg;
	struct dw_pcie_ep_func *ep_func;
	struct pci_epc *epc = ep->epc;
	unsigned int aligned_offset;
	u16 msg_ctrl, msg_data;
	bool has_upper;
	u64 msg_addr;
	int ret;

	ep_func = dw_pcie_ep_get_func_from_ep(ep, func_no);
	if (!ep_func || !ep_func->msi_cap)
		return -EINVAL;

	/* Raise MSI per the PCI Local Bus Specification Revision 3.0, 6.8.1. */
	reg = ep_func->msi_cap + PCI_MSI_FLAGS;
	msg_ctrl = dw_pcie_ep_readw_dbi(ep, func_no, reg);
	has_upper = !!(msg_ctrl & PCI_MSI_FLAGS_64BIT);
	reg = ep_func->msi_cap + PCI_MSI_ADDRESS_LO;
	msg_addr_lower = dw_pcie_ep_readl_dbi(ep, func_no, reg);
	if (has_upper) {
		reg = ep_func->msi_cap + PCI_MSI_ADDRESS_HI;
		msg_addr_upper = dw_pcie_ep_readl_dbi(ep, func_no, reg);
		reg = ep_func->msi_cap + PCI_MSI_DATA_64;
		msg_data = dw_pcie_ep_readw_dbi(ep, func_no, reg);
	} else {
		msg_addr_upper = 0;
		reg = ep_func->msi_cap + PCI_MSI_DATA_32;
		msg_data = dw_pcie_ep_readw_dbi(ep, func_no, reg);
	}
	msg_addr = ((u64)msg_addr_upper) << 32 | msg_addr_lower;

	aligned_offset = msg_addr & (epc->mem->window.page_size - 1);
	msg_addr = ALIGN_DOWN(msg_addr, epc->mem->window.page_size);
	ret = dw_pcie_ep_map_addr(epc, func_no, 0, ep->msi_mem_phys, msg_addr,
				  epc->mem->window.page_size);
	if (ret)
		return ret;

	writel(msg_data | (interrupt_num - 1), ep->msi_mem + aligned_offset);

	dw_pcie_ep_unmap_addr(epc, func_no, 0, ep->msi_mem_phys);

	return 0;
}
EXPORT_SYMBOL_GPL(dw_pcie_ep_raise_msi_irq);

int dw_pcie_ep_raise_msix_irq_doorbell(struct dw_pcie_ep *ep, u8 func_no,
				       u16 interrupt_num)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct dw_pcie_ep_func *ep_func;
	u32 msg_data;

	ep_func = dw_pcie_ep_get_func_from_ep(ep, func_no);
	if (!ep_func || !ep_func->msix_cap)
		return -EINVAL;

	msg_data = (func_no << PCIE_MSIX_DOORBELL_PF_SHIFT) |
		   (interrupt_num - 1);

	dw_pcie_writel_dbi(pci, PCIE_MSIX_DOORBELL, msg_data);

	return 0;
}

int dw_pcie_ep_raise_msix_irq(struct dw_pcie_ep *ep, u8 func_no,
			      u16 interrupt_num)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct pci_epf_msix_tbl *msix_tbl;
	struct dw_pcie_ep_func *ep_func;
	struct pci_epc *epc = ep->epc;
	u32 reg, msg_data, vec_ctrl;
	unsigned int aligned_offset;
	u32 tbl_offset;
	u64 msg_addr;
	int ret;
	u8 bir;

	ep_func = dw_pcie_ep_get_func_from_ep(ep, func_no);
	if (!ep_func || !ep_func->msix_cap)
		return -EINVAL;

	reg = ep_func->msix_cap + PCI_MSIX_TABLE;
	tbl_offset = dw_pcie_ep_readl_dbi(ep, func_no, reg);
	bir = FIELD_GET(PCI_MSIX_TABLE_BIR, tbl_offset);
	tbl_offset &= PCI_MSIX_TABLE_OFFSET;

	msix_tbl = ep->epf_bar[bir]->addr + tbl_offset;
	msg_addr = msix_tbl[(interrupt_num - 1)].msg_addr;
	msg_data = msix_tbl[(interrupt_num - 1)].msg_data;
	vec_ctrl = msix_tbl[(interrupt_num - 1)].vector_ctrl;

	if (vec_ctrl & PCI_MSIX_ENTRY_CTRL_MASKBIT) {
		dev_dbg(pci->dev, "MSI-X entry ctrl set\n");
		return -EPERM;
	}

	aligned_offset = msg_addr & (epc->mem->window.page_size - 1);
	msg_addr = ALIGN_DOWN(msg_addr, epc->mem->window.page_size);
	ret = dw_pcie_ep_map_addr(epc, func_no, 0, ep->msi_mem_phys, msg_addr,
				  epc->mem->window.page_size);
	if (ret)
		return ret;

	writel(msg_data, ep->msi_mem + aligned_offset);

	dw_pcie_ep_unmap_addr(epc, func_no, 0, ep->msi_mem_phys);

	return 0;
}

void dw_pcie_ep_exit(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct pci_epc *epc = ep->epc;

	dw_pcie_edma_remove(pci);

	pci_epc_mem_free_addr(epc, ep->msi_mem_phys, ep->msi_mem,
			      epc->mem->window.page_size);

	pci_epc_mem_exit(epc);

	if (ep->ops->deinit)
		ep->ops->deinit(ep);
}
EXPORT_SYMBOL_GPL(dw_pcie_ep_exit);

static unsigned int dw_pcie_ep_find_ext_capability(struct dw_pcie *pci, int cap)
{
	u32 header;
	int pos = PCI_CFG_SPACE_SIZE;

	while (pos) {
		header = dw_pcie_readl_dbi(pci, pos);
		if (PCI_EXT_CAP_ID(header) == cap)
			return pos;

		pos = PCI_EXT_CAP_NEXT(header);
		if (!pos)
			break;
	}

	return 0;
}

int dw_pcie_ep_init_complete(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct dw_pcie_ep_func *ep_func;
	struct device *dev = pci->dev;
	struct pci_epc *epc = ep->epc;
	unsigned int offset, ptm_cap_base;
	unsigned int nbars;
	u8 hdr_type;
	u8 func_no;
	int i, ret;
	void *addr;
	u32 reg;

	hdr_type = dw_pcie_readb_dbi(pci, PCI_HEADER_TYPE) &
		   PCI_HEADER_TYPE_MASK;
	if (hdr_type != PCI_HEADER_TYPE_NORMAL) {
		dev_err(pci->dev,
			"PCIe controller is not set to EP mode (hdr_type:0x%x)!\n",
			hdr_type);
		return -EIO;
	}

	dw_pcie_version_detect(pci);

	dw_pcie_iatu_detect(pci);

	ret = dw_pcie_edma_detect(pci);
	if (ret)
		return ret;

	if (!ep->ib_window_map) {
		ep->ib_window_map = devm_bitmap_zalloc(dev, pci->num_ib_windows,
						       GFP_KERNEL);
		if (!ep->ib_window_map)
			goto err_remove_edma;
	}

	if (!ep->ob_window_map) {
		ep->ob_window_map = devm_bitmap_zalloc(dev, pci->num_ob_windows,
						       GFP_KERNEL);
		if (!ep->ob_window_map)
			goto err_remove_edma;
	}

	if (!ep->outbound_addr) {
		addr = devm_kcalloc(dev, pci->num_ob_windows, sizeof(phys_addr_t),
				    GFP_KERNEL);
		if (!addr)
			goto err_remove_edma;
		ep->outbound_addr = addr;
	}

	for (func_no = 0; func_no < epc->max_functions; func_no++) {

		ep_func = dw_pcie_ep_get_func_from_ep(ep, func_no);
		if (ep_func)
			continue;

		ep_func = devm_kzalloc(dev, sizeof(*ep_func), GFP_KERNEL);
		if (!ep_func)
			goto err_remove_edma;

		ep_func->func_no = func_no;
		ep_func->msi_cap = dw_pcie_ep_find_capability(ep, func_no,
							      PCI_CAP_ID_MSI);
		ep_func->msix_cap = dw_pcie_ep_find_capability(ep, func_no,
							       PCI_CAP_ID_MSIX);

		list_add_tail(&ep_func->list, &ep->func_list);
	}

	if (ep->ops->init)
		ep->ops->init(ep);

	offset = dw_pcie_ep_find_ext_capability(pci, PCI_EXT_CAP_ID_REBAR);
	ptm_cap_base = dw_pcie_ep_find_ext_capability(pci, PCI_EXT_CAP_ID_PTM);

	dw_pcie_dbi_ro_wr_en(pci);

	if (offset) {
		reg = dw_pcie_readl_dbi(pci, offset + PCI_REBAR_CTRL);
		nbars = (reg & PCI_REBAR_CTRL_NBAR_MASK) >>
			PCI_REBAR_CTRL_NBAR_SHIFT;

		/*
		 * PCIe r6.0, sec 7.8.6.2 require us to support at least one
		 * size in the range from 1 MB to 512 GB. Advertise support
		 * for 1 MB BAR size only.
		 */
		for (i = 0; i < nbars; i++, offset += PCI_REBAR_CTRL)
			dw_pcie_writel_dbi(pci, offset + PCI_REBAR_CAP, BIT(4));
	}

	/*
	 * PTM responder capability can be disabled only after disabling
	 * PTM root capability.
	 */
	if (ptm_cap_base) {
		dw_pcie_dbi_ro_wr_en(pci);
		reg = dw_pcie_readl_dbi(pci, ptm_cap_base + PCI_PTM_CAP);
		reg &= ~PCI_PTM_CAP_ROOT;
		dw_pcie_writel_dbi(pci, ptm_cap_base + PCI_PTM_CAP, reg);

		reg = dw_pcie_readl_dbi(pci, ptm_cap_base + PCI_PTM_CAP);
		reg &= ~(PCI_PTM_CAP_RES | PCI_PTM_GRANULARITY_MASK);
		dw_pcie_writel_dbi(pci, ptm_cap_base + PCI_PTM_CAP, reg);
		dw_pcie_dbi_ro_wr_dis(pci);
	}

	dw_pcie_setup(pci);
	dw_pcie_dbi_ro_wr_dis(pci);

	return 0;

err_remove_edma:
	dw_pcie_edma_remove(pci);

	return ret;
}
EXPORT_SYMBOL_GPL(dw_pcie_ep_init_complete);

int dw_pcie_ep_init(struct dw_pcie_ep *ep)
{
	int ret;
	struct resource *res;
	struct pci_epc *epc;
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct device *dev = pci->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct device_node *np = dev->of_node;
	const struct pci_epc_features *epc_features;

	INIT_LIST_HEAD(&ep->func_list);

	ret = dw_pcie_get_resources(pci);
	if (ret)
		return ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "addr_space");
	if (!res)
		return -EINVAL;

	ep->phys_base = res->start;
	ep->addr_size = resource_size(res);

	if (ep->ops->pre_init)
		ep->ops->pre_init(ep);

	epc = devm_pci_epc_create(dev, &epc_ops);
	if (IS_ERR(epc)) {
		dev_err(dev, "Failed to create epc device\n");
		return PTR_ERR(epc);
	}

	ep->epc = epc;
	epc_set_drvdata(epc, ep);

	ret = of_property_read_u8(np, "max-functions", &epc->max_functions);
	if (ret < 0)
		epc->max_functions = 1;

	ret = pci_epc_mem_init(epc, ep->phys_base, ep->addr_size,
			       ep->page_size);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize address space\n");
		goto err_ep_deinit;
	}

	ep->msi_mem = pci_epc_mem_alloc_addr(epc, &ep->msi_mem_phys,
					     epc->mem->window.page_size);
	if (!ep->msi_mem) {
		ret = -ENOMEM;
		dev_err(dev, "Failed to reserve memory for MSI/MSI-X\n");
		goto err_exit_epc_mem;
	}

	if (ep->ops->get_features) {
		epc_features = ep->ops->get_features(ep);
		if (epc_features->core_init_notifier)
			return 0;
	}

	/*
	 * NOTE:- Avoid accessing the hardware (Ex:- DBI space) before this
	 * step as platforms that implement 'core_init_notifier' feature may
	 * not have the hardware ready (i.e. core initialized) for access
	 * (Ex: tegra194). Any hardware access on such platforms result
	 * in system hang.
	 */
	ret = dw_pcie_ep_init_complete(ep);
	if (ret)
		goto err_free_epc_mem;

	return 0;

err_free_epc_mem:
	pci_epc_mem_free_addr(epc, ep->msi_mem_phys, ep->msi_mem,
			      epc->mem->window.page_size);

err_exit_epc_mem:
	pci_epc_mem_exit(epc);

err_ep_deinit:
	if (ep->ops->deinit)
		ep->ops->deinit(ep);

	return ret;
}
EXPORT_SYMBOL_GPL(dw_pcie_ep_init);
