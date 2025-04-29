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

/**
 * dw_pcie_ep_get_func_from_ep - Get the struct dw_pcie_ep_func corresponding to
 *				 the endpoint function
 * @ep: DWC EP device
 * @func_no: Function number of the endpoint device
 *
 * Return: struct dw_pcie_ep_func if success, NULL otherwise.
 */
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

/**
 * dw_pcie_ep_reset_bar - Reset endpoint BAR
 * @pci: DWC PCI device
 * @bar: BAR number of the endpoint
 */
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

/**
 * dw_pcie_ep_hide_ext_capability - Hide a capability from the linked list
 * @pci: DWC PCI device
 * @prev_cap: Capability preceding the capability that should be hidden
 * @cap: Capability that should be hidden
 *
 * Return: 0 if success, errno otherwise.
 */
int dw_pcie_ep_hide_ext_capability(struct dw_pcie *pci, u8 prev_cap, u8 cap)
{
	u16 prev_cap_offset, cap_offset;
	u32 prev_cap_header, cap_header;

	prev_cap_offset = dw_pcie_find_ext_capability(pci, prev_cap);
	if (!prev_cap_offset)
		return -EINVAL;

	prev_cap_header = dw_pcie_readl_dbi(pci, prev_cap_offset);
	cap_offset = PCI_EXT_CAP_NEXT(prev_cap_header);
	cap_header = dw_pcie_readl_dbi(pci, cap_offset);

	/* cap must immediately follow prev_cap. */
	if (PCI_EXT_CAP_ID(cap_header) != cap)
		return -EINVAL;

	/* Clear next ptr. */
	prev_cap_header &= ~GENMASK(31, 20);

	/* Set next ptr to next ptr of cap. */
	prev_cap_header |= cap_header & GENMASK(31, 20);

	dw_pcie_dbi_ro_wr_en(pci);
	dw_pcie_writel_dbi(pci, prev_cap_offset, prev_cap_header);
	dw_pcie_dbi_ro_wr_dis(pci);

	return 0;
}
EXPORT_SYMBOL_GPL(dw_pcie_ep_hide_ext_capability);

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
				  dma_addr_t parent_bus_addr, enum pci_barno bar,
				  size_t size)
{
	int ret;
	u32 free_win;
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	if (!ep->bar_to_atu[bar])
		free_win = find_first_zero_bit(ep->ib_window_map, pci->num_ib_windows);
	else
		free_win = ep->bar_to_atu[bar] - 1;

	if (free_win >= pci->num_ib_windows) {
		dev_err(pci->dev, "No free inbound window\n");
		return -EINVAL;
	}

	ret = dw_pcie_prog_ep_inbound_atu(pci, func_no, free_win, type,
					  parent_bus_addr, bar, size);
	if (ret < 0) {
		dev_err(pci->dev, "Failed to program IB window\n");
		return ret;
	}

	/*
	 * Always increment free_win before assignment, since value 0 is used to identify
	 * unallocated mapping.
	 */
	ep->bar_to_atu[bar] = free_win + 1;
	set_bit(free_win, ep->ib_window_map);

	return 0;
}

static int dw_pcie_ep_outbound_atu(struct dw_pcie_ep *ep,
				   struct dw_pcie_ob_atu_cfg *atu)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	u32 free_win;
	int ret;

	free_win = find_first_zero_bit(ep->ob_window_map, pci->num_ob_windows);
	if (free_win >= pci->num_ob_windows) {
		dev_err(pci->dev, "No free outbound window\n");
		return -EINVAL;
	}

	atu->index = free_win;
	ret = dw_pcie_prog_outbound_atu(pci, atu);
	if (ret)
		return ret;

	set_bit(free_win, ep->ob_window_map);
	ep->outbound_addr[free_win] = atu->parent_bus_addr;

	return 0;
}

static void dw_pcie_ep_clear_bar(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				 struct pci_epf_bar *epf_bar)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	enum pci_barno bar = epf_bar->barno;
	u32 atu_index = ep->bar_to_atu[bar] - 1;

	if (!ep->bar_to_atu[bar])
		return;

	__dw_pcie_ep_reset_bar(pci, func_no, bar, epf_bar->flags);

	dw_pcie_disable_atu(pci, PCIE_ATU_REGION_DIR_IB, atu_index);
	clear_bit(atu_index, ep->ib_window_map);
	ep->epf_bar[bar] = NULL;
	ep->bar_to_atu[bar] = 0;
}

static unsigned int dw_pcie_ep_get_rebar_offset(struct dw_pcie *pci,
						enum pci_barno bar)
{
	u32 reg, bar_index;
	unsigned int offset, nbars;
	int i;

	offset = dw_pcie_find_ext_capability(pci, PCI_EXT_CAP_ID_REBAR);
	if (!offset)
		return offset;

	reg = dw_pcie_readl_dbi(pci, offset + PCI_REBAR_CTRL);
	nbars = (reg & PCI_REBAR_CTRL_NBAR_MASK) >> PCI_REBAR_CTRL_NBAR_SHIFT;

	for (i = 0; i < nbars; i++, offset += PCI_REBAR_CTRL) {
		reg = dw_pcie_readl_dbi(pci, offset + PCI_REBAR_CTRL);
		bar_index = reg & PCI_REBAR_CTRL_BAR_IDX;
		if (bar_index == bar)
			return offset;
	}

	return 0;
}

static int dw_pcie_ep_set_bar_resizable(struct dw_pcie_ep *ep, u8 func_no,
					struct pci_epf_bar *epf_bar)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	enum pci_barno bar = epf_bar->barno;
	size_t size = epf_bar->size;
	int flags = epf_bar->flags;
	u32 reg = PCI_BASE_ADDRESS_0 + (4 * bar);
	unsigned int rebar_offset;
	u32 rebar_cap, rebar_ctrl;
	int ret;

	rebar_offset = dw_pcie_ep_get_rebar_offset(pci, bar);
	if (!rebar_offset)
		return -EINVAL;

	ret = pci_epc_bar_size_to_rebar_cap(size, &rebar_cap);
	if (ret)
		return ret;

	dw_pcie_dbi_ro_wr_en(pci);

	/*
	 * A BAR mask should not be written for a resizable BAR. The BAR mask
	 * is automatically derived by the controller every time the "selected
	 * size" bits are updated, see "Figure 3-26 Resizable BAR Example for
	 * 32-bit Memory BAR0" in DWC EP databook 5.96a. We simply need to write
	 * BIT(0) to set the BAR enable bit.
	 */
	dw_pcie_ep_writel_dbi2(ep, func_no, reg, BIT(0));
	dw_pcie_ep_writel_dbi(ep, func_no, reg, flags);

	if (flags & PCI_BASE_ADDRESS_MEM_TYPE_64) {
		dw_pcie_ep_writel_dbi2(ep, func_no, reg + 4, 0);
		dw_pcie_ep_writel_dbi(ep, func_no, reg + 4, 0);
	}

	/*
	 * Bits 31:0 in PCI_REBAR_CAP define "supported sizes" bits for sizes
	 * 1 MB to 128 TB. Bits 31:16 in PCI_REBAR_CTRL define "supported sizes"
	 * bits for sizes 256 TB to 8 EB. Disallow sizes 256 TB to 8 EB.
	 */
	rebar_ctrl = dw_pcie_readl_dbi(pci, rebar_offset + PCI_REBAR_CTRL);
	rebar_ctrl &= ~GENMASK(31, 16);
	dw_pcie_writel_dbi(pci, rebar_offset + PCI_REBAR_CTRL, rebar_ctrl);

	/*
	 * The "selected size" (bits 13:8) in PCI_REBAR_CTRL are automatically
	 * updated when writing PCI_REBAR_CAP, see "Figure 3-26 Resizable BAR
	 * Example for 32-bit Memory BAR0" in DWC EP databook 5.96a.
	 */
	dw_pcie_writel_dbi(pci, rebar_offset + PCI_REBAR_CAP, rebar_cap);

	dw_pcie_dbi_ro_wr_dis(pci);

	return 0;
}

static int dw_pcie_ep_set_bar_programmable(struct dw_pcie_ep *ep, u8 func_no,
					   struct pci_epf_bar *epf_bar)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	enum pci_barno bar = epf_bar->barno;
	size_t size = epf_bar->size;
	int flags = epf_bar->flags;
	u32 reg = PCI_BASE_ADDRESS_0 + (4 * bar);

	dw_pcie_dbi_ro_wr_en(pci);

	dw_pcie_ep_writel_dbi2(ep, func_no, reg, lower_32_bits(size - 1));
	dw_pcie_ep_writel_dbi(ep, func_no, reg, flags);

	if (flags & PCI_BASE_ADDRESS_MEM_TYPE_64) {
		dw_pcie_ep_writel_dbi2(ep, func_no, reg + 4, upper_32_bits(size - 1));
		dw_pcie_ep_writel_dbi(ep, func_no, reg + 4, 0);
	}

	dw_pcie_dbi_ro_wr_dis(pci);

	return 0;
}

static enum pci_epc_bar_type dw_pcie_ep_get_bar_type(struct dw_pcie_ep *ep,
						     enum pci_barno bar)
{
	const struct pci_epc_features *epc_features;

	if (!ep->ops->get_features)
		return BAR_PROGRAMMABLE;

	epc_features = ep->ops->get_features(ep);

	return epc_features->bar[bar].type;
}

static int dw_pcie_ep_set_bar(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			      struct pci_epf_bar *epf_bar)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	enum pci_barno bar = epf_bar->barno;
	size_t size = epf_bar->size;
	enum pci_epc_bar_type bar_type;
	int flags = epf_bar->flags;
	int ret, type;

	/*
	 * DWC does not allow BAR pairs to overlap, e.g. you cannot combine BARs
	 * 1 and 2 to form a 64-bit BAR.
	 */
	if ((flags & PCI_BASE_ADDRESS_MEM_TYPE_64) && (bar & 1))
		return -EINVAL;

	/*
	 * Certain EPF drivers dynamically change the physical address of a BAR
	 * (i.e. they call set_bar() twice, without ever calling clear_bar(), as
	 * calling clear_bar() would clear the BAR's PCI address assigned by the
	 * host).
	 */
	if (ep->epf_bar[bar]) {
		/*
		 * We can only dynamically change a BAR if the new BAR size and
		 * BAR flags do not differ from the existing configuration.
		 */
		if (ep->epf_bar[bar]->barno != bar ||
		    ep->epf_bar[bar]->size != size ||
		    ep->epf_bar[bar]->flags != flags)
			return -EINVAL;

		/*
		 * When dynamically changing a BAR, skip writing the BAR reg, as
		 * that would clear the BAR's PCI address assigned by the host.
		 */
		goto config_atu;
	}

	bar_type = dw_pcie_ep_get_bar_type(ep, bar);
	switch (bar_type) {
	case BAR_FIXED:
		/*
		 * There is no need to write a BAR mask for a fixed BAR (except
		 * to write 1 to the LSB of the BAR mask register, to enable the
		 * BAR). Write the BAR mask regardless. (The fixed bits in the
		 * BAR mask register will be read-only anyway.)
		 */
		fallthrough;
	case BAR_PROGRAMMABLE:
		ret = dw_pcie_ep_set_bar_programmable(ep, func_no, epf_bar);
		break;
	case BAR_RESIZABLE:
		ret = dw_pcie_ep_set_bar_resizable(ep, func_no, epf_bar);
		break;
	default:
		ret = -EINVAL;
		dev_err(pci->dev, "Invalid BAR type\n");
		break;
	}

	if (ret)
		return ret;

config_atu:
	if (!(flags & PCI_BASE_ADDRESS_SPACE))
		type = PCIE_ATU_TYPE_MEM;
	else
		type = PCIE_ATU_TYPE_IO;

	ret = dw_pcie_ep_inbound_atu(ep, func_no, type, epf_bar->phys_addr, bar,
				     size);
	if (ret)
		return ret;

	ep->epf_bar[bar] = epf_bar;

	return 0;
}

static int dw_pcie_find_index(struct dw_pcie_ep *ep, phys_addr_t addr,
			      u32 *atu_index)
{
	u32 index;
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	for_each_set_bit(index, ep->ob_window_map, pci->num_ob_windows) {
		if (ep->outbound_addr[index] != addr)
			continue;
		*atu_index = index;
		return 0;
	}

	return -EINVAL;
}

static u64 dw_pcie_ep_align_addr(struct pci_epc *epc, u64 pci_addr,
				 size_t *pci_size, size_t *offset)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	u64 mask = pci->region_align - 1;
	size_t ofst = pci_addr & mask;

	*pci_size = ALIGN(ofst + *pci_size, epc->mem->window.page_size);
	*offset = ofst;

	return pci_addr & ~mask;
}

static void dw_pcie_ep_unmap_addr(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				  phys_addr_t addr)
{
	int ret;
	u32 atu_index;
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	ret = dw_pcie_find_index(ep, addr - pci->parent_bus_offset,
				 &atu_index);
	if (ret < 0)
		return;

	ep->outbound_addr[atu_index] = 0;
	dw_pcie_disable_atu(pci, PCIE_ATU_REGION_DIR_OB, atu_index);
	clear_bit(atu_index, ep->ob_window_map);
}

static int dw_pcie_ep_map_addr(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			       phys_addr_t addr, u64 pci_addr, size_t size)
{
	int ret;
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct dw_pcie_ob_atu_cfg atu = { 0 };

	atu.func_no = func_no;
	atu.type = PCIE_ATU_TYPE_MEM;
	atu.parent_bus_addr = addr - pci->parent_bus_offset;
	atu.pci_addr = pci_addr;
	atu.size = size;
	ret = dw_pcie_ep_outbound_atu(ep, &atu);
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
	.align_addr		= dw_pcie_ep_align_addr,
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

/**
 * dw_pcie_ep_raise_intx_irq - Raise INTx IRQ to the host
 * @ep: DWC EP device
 * @func_no: Function number of the endpoint
 *
 * Return: 0 if success, errono otherwise.
 */
int dw_pcie_ep_raise_intx_irq(struct dw_pcie_ep *ep, u8 func_no)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct device *dev = pci->dev;

	dev_err(dev, "EP cannot raise INTX IRQs\n");

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(dw_pcie_ep_raise_intx_irq);

/**
 * dw_pcie_ep_raise_msi_irq - Raise MSI IRQ to the host
 * @ep: DWC EP device
 * @func_no: Function number of the endpoint
 * @interrupt_num: Interrupt number to be raised
 *
 * Return: 0 if success, errono otherwise.
 */
int dw_pcie_ep_raise_msi_irq(struct dw_pcie_ep *ep, u8 func_no,
			     u8 interrupt_num)
{
	u32 msg_addr_lower, msg_addr_upper, reg;
	struct dw_pcie_ep_func *ep_func;
	struct pci_epc *epc = ep->epc;
	size_t map_size = sizeof(u32);
	size_t offset;
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

	msg_addr = dw_pcie_ep_align_addr(epc, msg_addr, &map_size, &offset);
	ret = dw_pcie_ep_map_addr(epc, func_no, 0, ep->msi_mem_phys, msg_addr,
				  map_size);
	if (ret)
		return ret;

	writel(msg_data | (interrupt_num - 1), ep->msi_mem + offset);

	dw_pcie_ep_unmap_addr(epc, func_no, 0, ep->msi_mem_phys);

	return 0;
}
EXPORT_SYMBOL_GPL(dw_pcie_ep_raise_msi_irq);

/**
 * dw_pcie_ep_raise_msix_irq_doorbell - Raise MSI-X to the host using Doorbell
 *					method
 * @ep: DWC EP device
 * @func_no: Function number of the endpoint device
 * @interrupt_num: Interrupt number to be raised
 *
 * Return: 0 if success, errno otherwise.
 */
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

/**
 * dw_pcie_ep_raise_msix_irq - Raise MSI-X to the host
 * @ep: DWC EP device
 * @func_no: Function number of the endpoint device
 * @interrupt_num: Interrupt number to be raised
 *
 * Return: 0 if success, errno otherwise.
 */
int dw_pcie_ep_raise_msix_irq(struct dw_pcie_ep *ep, u8 func_no,
			      u16 interrupt_num)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct pci_epf_msix_tbl *msix_tbl;
	struct dw_pcie_ep_func *ep_func;
	struct pci_epc *epc = ep->epc;
	size_t map_size = sizeof(u32);
	size_t offset;
	u32 reg, msg_data, vec_ctrl;
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

	msg_addr = dw_pcie_ep_align_addr(epc, msg_addr, &map_size, &offset);
	ret = dw_pcie_ep_map_addr(epc, func_no, 0, ep->msi_mem_phys, msg_addr,
				  map_size);
	if (ret)
		return ret;

	writel(msg_data, ep->msi_mem + offset);

	dw_pcie_ep_unmap_addr(epc, func_no, 0, ep->msi_mem_phys);

	return 0;
}

/**
 * dw_pcie_ep_cleanup - Cleanup DWC EP resources after fundamental reset
 * @ep: DWC EP device
 *
 * Cleans up the DWC EP specific resources like eDMA etc... after fundamental
 * reset like PERST#. Note that this API is only applicable for drivers
 * supporting PERST# or any other methods of fundamental reset.
 */
void dw_pcie_ep_cleanup(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	dwc_pcie_debugfs_deinit(pci);
	dw_pcie_edma_remove(pci);
}
EXPORT_SYMBOL_GPL(dw_pcie_ep_cleanup);

/**
 * dw_pcie_ep_deinit - Deinitialize the endpoint device
 * @ep: DWC EP device
 *
 * Deinitialize the endpoint device. EPC device is not destroyed since that will
 * be taken care by Devres.
 */
void dw_pcie_ep_deinit(struct dw_pcie_ep *ep)
{
	struct pci_epc *epc = ep->epc;

	dw_pcie_ep_cleanup(ep);

	pci_epc_mem_free_addr(epc, ep->msi_mem_phys, ep->msi_mem,
			      epc->mem->window.page_size);

	pci_epc_mem_exit(epc);
}
EXPORT_SYMBOL_GPL(dw_pcie_ep_deinit);

static void dw_pcie_ep_init_non_sticky_registers(struct dw_pcie *pci)
{
	struct dw_pcie_ep *ep = &pci->ep;
	unsigned int offset;
	unsigned int nbars;
	enum pci_barno bar;
	u32 reg, i, val;

	offset = dw_pcie_find_ext_capability(pci, PCI_EXT_CAP_ID_REBAR);

	dw_pcie_dbi_ro_wr_en(pci);

	if (offset) {
		reg = dw_pcie_readl_dbi(pci, offset + PCI_REBAR_CTRL);
		nbars = (reg & PCI_REBAR_CTRL_NBAR_MASK) >>
			PCI_REBAR_CTRL_NBAR_SHIFT;

		/*
		 * PCIe r6.0, sec 7.8.6.2 require us to support at least one
		 * size in the range from 1 MB to 512 GB. Advertise support
		 * for 1 MB BAR size only.
		 *
		 * For a BAR that has been configured via dw_pcie_ep_set_bar(),
		 * advertise support for only that size instead.
		 */
		for (i = 0; i < nbars; i++, offset += PCI_REBAR_CTRL) {
			/*
			 * While the RESBAR_CAP_REG_* fields are sticky, the
			 * RESBAR_CTRL_REG_BAR_SIZE field is non-sticky (it is
			 * sticky in certain versions of DWC PCIe, but not all).
			 *
			 * RESBAR_CTRL_REG_BAR_SIZE is updated automatically by
			 * the controller when RESBAR_CAP_REG is written, which
			 * is why RESBAR_CAP_REG is written here.
			 */
			val = dw_pcie_readl_dbi(pci, offset + PCI_REBAR_CTRL);
			bar = val & PCI_REBAR_CTRL_BAR_IDX;
			if (ep->epf_bar[bar])
				pci_epc_bar_size_to_rebar_cap(ep->epf_bar[bar]->size, &val);
			else
				val = BIT(4);

			dw_pcie_writel_dbi(pci, offset + PCI_REBAR_CAP, val);
		}
	}

	dw_pcie_setup(pci);
	dw_pcie_dbi_ro_wr_dis(pci);
}

/**
 * dw_pcie_ep_init_registers - Initialize DWC EP specific registers
 * @ep: DWC EP device
 *
 * Initialize the registers (CSRs) specific to DWC EP. This API should be called
 * only when the endpoint receives an active refclk (either from host or
 * generated locally).
 */
int dw_pcie_ep_init_registers(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct dw_pcie_ep_func *ep_func;
	struct device *dev = pci->dev;
	struct pci_epc *epc = ep->epc;
	u32 ptm_cap_base, reg;
	u8 hdr_type;
	u8 func_no;
	void *addr;
	int ret;

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

	ret = -ENOMEM;
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

	ptm_cap_base = dw_pcie_find_ext_capability(pci, PCI_EXT_CAP_ID_PTM);

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

	dw_pcie_ep_init_non_sticky_registers(pci);

	dwc_pcie_debugfs_init(pci);

	return 0;

err_remove_edma:
	dw_pcie_edma_remove(pci);

	return ret;
}
EXPORT_SYMBOL_GPL(dw_pcie_ep_init_registers);

/**
 * dw_pcie_ep_linkup - Notify EPF drivers about Link Up event
 * @ep: DWC EP device
 */
void dw_pcie_ep_linkup(struct dw_pcie_ep *ep)
{
	struct pci_epc *epc = ep->epc;

	pci_epc_linkup(epc);
}
EXPORT_SYMBOL_GPL(dw_pcie_ep_linkup);

/**
 * dw_pcie_ep_linkdown - Notify EPF drivers about Link Down event
 * @ep: DWC EP device
 *
 * Non-sticky registers are also initialized before sending the notification to
 * the EPF drivers. This is needed since the registers need to be initialized
 * before the link comes back again.
 */
void dw_pcie_ep_linkdown(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct pci_epc *epc = ep->epc;

	/*
	 * Initialize the non-sticky DWC registers as they would've reset post
	 * Link Down. This is specifically needed for drivers not supporting
	 * PERST# as they have no way to reinitialize the registers before the
	 * link comes back again.
	 */
	dw_pcie_ep_init_non_sticky_registers(pci);

	pci_epc_linkdown(epc);
}
EXPORT_SYMBOL_GPL(dw_pcie_ep_linkdown);

static int dw_pcie_ep_get_resources(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct device *dev = pci->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct device_node *np = dev->of_node;
	struct pci_epc *epc = ep->epc;
	struct resource *res;
	int ret;

	ret = dw_pcie_get_resources(pci);
	if (ret)
		return ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "addr_space");
	if (!res)
		return -EINVAL;

	ep->phys_base = res->start;
	ep->addr_size = resource_size(res);

	/*
	 * artpec6_pcie_cpu_addr_fixup() uses ep->phys_base, so call
	 * dw_pcie_parent_bus_offset() after setting ep->phys_base.
	 */
	pci->parent_bus_offset = dw_pcie_parent_bus_offset(pci, "addr_space",
							   ep->phys_base);

	ret = of_property_read_u8(np, "max-functions", &epc->max_functions);
	if (ret < 0)
		epc->max_functions = 1;

	return 0;
}

/**
 * dw_pcie_ep_init - Initialize the endpoint device
 * @ep: DWC EP device
 *
 * Initialize the endpoint device. Allocate resources and create the EPC
 * device with the endpoint framework.
 *
 * Return: 0 if success, errno otherwise.
 */
int dw_pcie_ep_init(struct dw_pcie_ep *ep)
{
	int ret;
	struct pci_epc *epc;
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct device *dev = pci->dev;

	INIT_LIST_HEAD(&ep->func_list);

	epc = devm_pci_epc_create(dev, &epc_ops);
	if (IS_ERR(epc)) {
		dev_err(dev, "Failed to create epc device\n");
		return PTR_ERR(epc);
	}

	ep->epc = epc;
	epc_set_drvdata(epc, ep);

	ret = dw_pcie_ep_get_resources(ep);
	if (ret)
		return ret;

	if (ep->ops->pre_init)
		ep->ops->pre_init(ep);

	ret = pci_epc_mem_init(epc, ep->phys_base, ep->addr_size,
			       ep->page_size);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize address space\n");
		return ret;
	}

	ep->msi_mem = pci_epc_mem_alloc_addr(epc, &ep->msi_mem_phys,
					     epc->mem->window.page_size);
	if (!ep->msi_mem) {
		ret = -ENOMEM;
		dev_err(dev, "Failed to reserve memory for MSI/MSI-X\n");
		goto err_exit_epc_mem;
	}

	return 0;

err_exit_epc_mem:
	pci_epc_mem_exit(epc);

	return ret;
}
EXPORT_SYMBOL_GPL(dw_pcie_ep_init);
