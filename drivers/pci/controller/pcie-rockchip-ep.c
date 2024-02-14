// SPDX-License-Identifier: GPL-2.0+
/*
 * Rockchip AXI PCIe endpoint controller driver
 *
 * Copyright (c) 2018 Rockchip, Inc.
 *
 * Author: Shawn Lin <shawn.lin@rock-chips.com>
 *         Simon Xue <xxm@rock-chips.com>
 */

#include <linux/configfs.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/pci-epc.h>
#include <linux/platform_device.h>
#include <linux/pci-epf.h>
#include <linux/sizes.h>

#include "pcie-rockchip.h"

/**
 * struct rockchip_pcie_ep - private data for PCIe endpoint controller driver
 * @rockchip: Rockchip PCIe controller
 * @epc: PCI EPC device
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
 */
struct rockchip_pcie_ep {
	struct rockchip_pcie	rockchip;
	struct pci_epc		*epc;
	u32			max_regions;
	unsigned long		ob_region_map;
	phys_addr_t		*ob_addr;
	phys_addr_t		irq_phys_addr;
	void __iomem		*irq_cpu_addr;
	u64			irq_pci_addr;
	u8			irq_pci_fn;
	u8			irq_pending;
};

static void rockchip_pcie_clear_ep_ob_atu(struct rockchip_pcie *rockchip,
					  u32 region)
{
	rockchip_pcie_write(rockchip, 0,
			    ROCKCHIP_PCIE_AT_OB_REGION_PCI_ADDR0(region));
	rockchip_pcie_write(rockchip, 0,
			    ROCKCHIP_PCIE_AT_OB_REGION_PCI_ADDR1(region));
	rockchip_pcie_write(rockchip, 0,
			    ROCKCHIP_PCIE_AT_OB_REGION_DESC0(region));
	rockchip_pcie_write(rockchip, 0,
			    ROCKCHIP_PCIE_AT_OB_REGION_DESC1(region));
}

static void rockchip_pcie_prog_ep_ob_atu(struct rockchip_pcie *rockchip, u8 fn,
					 u32 r, u64 cpu_addr, u64 pci_addr,
					 size_t size)
{
	int num_pass_bits = fls64(size - 1);
	u32 addr0, addr1, desc0;

	if (num_pass_bits < 8)
		num_pass_bits = 8;

	addr0 = ((num_pass_bits - 1) & PCIE_CORE_OB_REGION_ADDR0_NUM_BITS) |
		(lower_32_bits(pci_addr) & PCIE_CORE_OB_REGION_ADDR0_LO_ADDR);
	addr1 = upper_32_bits(pci_addr);
	desc0 = ROCKCHIP_PCIE_AT_OB_REGION_DESC0_DEVFN(fn) | AXI_WRAPPER_MEM_WRITE;

	/* PCI bus address region */
	rockchip_pcie_write(rockchip, addr0,
			    ROCKCHIP_PCIE_AT_OB_REGION_PCI_ADDR0(r));
	rockchip_pcie_write(rockchip, addr1,
			    ROCKCHIP_PCIE_AT_OB_REGION_PCI_ADDR1(r));
	rockchip_pcie_write(rockchip, desc0,
			    ROCKCHIP_PCIE_AT_OB_REGION_DESC0(r));
	rockchip_pcie_write(rockchip, 0,
			    ROCKCHIP_PCIE_AT_OB_REGION_DESC1(r));
}

static int rockchip_pcie_ep_write_header(struct pci_epc *epc, u8 fn, u8 vfn,
					 struct pci_epf_header *hdr)
{
	u32 reg;
	struct rockchip_pcie_ep *ep = epc_get_drvdata(epc);
	struct rockchip_pcie *rockchip = &ep->rockchip;

	/* All functions share the same vendor ID with function 0 */
	if (fn == 0) {
		u32 vid_regs = (hdr->vendorid & GENMASK(15, 0)) |
			       (hdr->subsys_vendor_id & GENMASK(31, 16)) << 16;

		rockchip_pcie_write(rockchip, vid_regs,
				    PCIE_CORE_CONFIG_VENDOR);
	}

	reg = rockchip_pcie_read(rockchip, PCIE_EP_CONFIG_DID_VID);
	reg = (reg & 0xFFFF) | (hdr->deviceid << 16);
	rockchip_pcie_write(rockchip, reg, PCIE_EP_CONFIG_DID_VID);

	rockchip_pcie_write(rockchip,
			    hdr->revid |
			    hdr->progif_code << 8 |
			    hdr->subclass_code << 16 |
			    hdr->baseclass_code << 24,
			    ROCKCHIP_PCIE_EP_FUNC_BASE(fn) + PCI_REVISION_ID);
	rockchip_pcie_write(rockchip, hdr->cache_line_size,
			    ROCKCHIP_PCIE_EP_FUNC_BASE(fn) +
			    PCI_CACHE_LINE_SIZE);
	rockchip_pcie_write(rockchip, hdr->subsys_id << 16,
			    ROCKCHIP_PCIE_EP_FUNC_BASE(fn) +
			    PCI_SUBSYSTEM_VENDOR_ID);
	rockchip_pcie_write(rockchip, hdr->interrupt_pin << 8,
			    ROCKCHIP_PCIE_EP_FUNC_BASE(fn) +
			    PCI_INTERRUPT_LINE);

	return 0;
}

static int rockchip_pcie_ep_set_bar(struct pci_epc *epc, u8 fn, u8 vfn,
				    struct pci_epf_bar *epf_bar)
{
	struct rockchip_pcie_ep *ep = epc_get_drvdata(epc);
	struct rockchip_pcie *rockchip = &ep->rockchip;
	dma_addr_t bar_phys = epf_bar->phys_addr;
	enum pci_barno bar = epf_bar->barno;
	int flags = epf_bar->flags;
	u32 addr0, addr1, reg, cfg, b, aperture, ctrl;
	u64 sz;

	/* BAR size is 2^(aperture + 7) */
	sz = max_t(size_t, epf_bar->size, MIN_EP_APERTURE);

	/*
	 * roundup_pow_of_two() returns an unsigned long, which is not suited
	 * for 64bit values.
	 */
	sz = 1ULL << fls64(sz - 1);
	aperture = ilog2(sz) - 7; /* 128B -> 0, 256B -> 1, 512B -> 2, ... */

	if ((flags & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_IO) {
		ctrl = ROCKCHIP_PCIE_CORE_BAR_CFG_CTRL_IO_32BITS;
	} else {
		bool is_prefetch = !!(flags & PCI_BASE_ADDRESS_MEM_PREFETCH);
		bool is_64bits = sz > SZ_2G;

		if (is_64bits && (bar & 1))
			return -EINVAL;

		if (is_64bits && is_prefetch)
			ctrl =
			    ROCKCHIP_PCIE_CORE_BAR_CFG_CTRL_PREFETCH_MEM_64BITS;
		else if (is_prefetch)
			ctrl =
			    ROCKCHIP_PCIE_CORE_BAR_CFG_CTRL_PREFETCH_MEM_32BITS;
		else if (is_64bits)
			ctrl = ROCKCHIP_PCIE_CORE_BAR_CFG_CTRL_MEM_64BITS;
		else
			ctrl = ROCKCHIP_PCIE_CORE_BAR_CFG_CTRL_MEM_32BITS;
	}

	if (bar < BAR_4) {
		reg = ROCKCHIP_PCIE_CORE_EP_FUNC_BAR_CFG0(fn);
		b = bar;
	} else {
		reg = ROCKCHIP_PCIE_CORE_EP_FUNC_BAR_CFG1(fn);
		b = bar - BAR_4;
	}

	addr0 = lower_32_bits(bar_phys);
	addr1 = upper_32_bits(bar_phys);

	cfg = rockchip_pcie_read(rockchip, reg);
	cfg &= ~(ROCKCHIP_PCIE_CORE_EP_FUNC_BAR_CFG_BAR_APERTURE_MASK(b) |
		 ROCKCHIP_PCIE_CORE_EP_FUNC_BAR_CFG_BAR_CTRL_MASK(b));
	cfg |= (ROCKCHIP_PCIE_CORE_EP_FUNC_BAR_CFG_BAR_APERTURE(b, aperture) |
		ROCKCHIP_PCIE_CORE_EP_FUNC_BAR_CFG_BAR_CTRL(b, ctrl));

	rockchip_pcie_write(rockchip, cfg, reg);
	rockchip_pcie_write(rockchip, addr0,
			    ROCKCHIP_PCIE_AT_IB_EP_FUNC_BAR_ADDR0(fn, bar));
	rockchip_pcie_write(rockchip, addr1,
			    ROCKCHIP_PCIE_AT_IB_EP_FUNC_BAR_ADDR1(fn, bar));

	return 0;
}

static void rockchip_pcie_ep_clear_bar(struct pci_epc *epc, u8 fn, u8 vfn,
				       struct pci_epf_bar *epf_bar)
{
	struct rockchip_pcie_ep *ep = epc_get_drvdata(epc);
	struct rockchip_pcie *rockchip = &ep->rockchip;
	u32 reg, cfg, b, ctrl;
	enum pci_barno bar = epf_bar->barno;

	if (bar < BAR_4) {
		reg = ROCKCHIP_PCIE_CORE_EP_FUNC_BAR_CFG0(fn);
		b = bar;
	} else {
		reg = ROCKCHIP_PCIE_CORE_EP_FUNC_BAR_CFG1(fn);
		b = bar - BAR_4;
	}

	ctrl = ROCKCHIP_PCIE_CORE_BAR_CFG_CTRL_DISABLED;
	cfg = rockchip_pcie_read(rockchip, reg);
	cfg &= ~(ROCKCHIP_PCIE_CORE_EP_FUNC_BAR_CFG_BAR_APERTURE_MASK(b) |
		 ROCKCHIP_PCIE_CORE_EP_FUNC_BAR_CFG_BAR_CTRL_MASK(b));
	cfg |= ROCKCHIP_PCIE_CORE_EP_FUNC_BAR_CFG_BAR_CTRL(b, ctrl);

	rockchip_pcie_write(rockchip, cfg, reg);
	rockchip_pcie_write(rockchip, 0x0,
			    ROCKCHIP_PCIE_AT_IB_EP_FUNC_BAR_ADDR0(fn, bar));
	rockchip_pcie_write(rockchip, 0x0,
			    ROCKCHIP_PCIE_AT_IB_EP_FUNC_BAR_ADDR1(fn, bar));
}

static inline u32 rockchip_ob_region(phys_addr_t addr)
{
	return (addr >> ilog2(SZ_1M)) & 0x1f;
}

static int rockchip_pcie_ep_map_addr(struct pci_epc *epc, u8 fn, u8 vfn,
				     phys_addr_t addr, u64 pci_addr,
				     size_t size)
{
	struct rockchip_pcie_ep *ep = epc_get_drvdata(epc);
	struct rockchip_pcie *pcie = &ep->rockchip;
	u32 r = rockchip_ob_region(addr);

	rockchip_pcie_prog_ep_ob_atu(pcie, fn, r, addr, pci_addr, size);

	set_bit(r, &ep->ob_region_map);
	ep->ob_addr[r] = addr;

	return 0;
}

static void rockchip_pcie_ep_unmap_addr(struct pci_epc *epc, u8 fn, u8 vfn,
					phys_addr_t addr)
{
	struct rockchip_pcie_ep *ep = epc_get_drvdata(epc);
	struct rockchip_pcie *rockchip = &ep->rockchip;
	u32 r;

	for (r = 0; r < ep->max_regions; r++)
		if (ep->ob_addr[r] == addr)
			break;

	if (r == ep->max_regions)
		return;

	rockchip_pcie_clear_ep_ob_atu(rockchip, r);

	ep->ob_addr[r] = 0;
	clear_bit(r, &ep->ob_region_map);
}

static int rockchip_pcie_ep_set_msi(struct pci_epc *epc, u8 fn, u8 vfn,
				    u8 multi_msg_cap)
{
	struct rockchip_pcie_ep *ep = epc_get_drvdata(epc);
	struct rockchip_pcie *rockchip = &ep->rockchip;
	u32 flags;

	flags = rockchip_pcie_read(rockchip,
				   ROCKCHIP_PCIE_EP_FUNC_BASE(fn) +
				   ROCKCHIP_PCIE_EP_MSI_CTRL_REG);
	flags &= ~ROCKCHIP_PCIE_EP_MSI_CTRL_MMC_MASK;
	flags |=
	   (multi_msg_cap << ROCKCHIP_PCIE_EP_MSI_CTRL_MMC_OFFSET) |
	   (PCI_MSI_FLAGS_64BIT << ROCKCHIP_PCIE_EP_MSI_FLAGS_OFFSET);
	flags &= ~ROCKCHIP_PCIE_EP_MSI_CTRL_MASK_MSI_CAP;
	rockchip_pcie_write(rockchip, flags,
			    ROCKCHIP_PCIE_EP_FUNC_BASE(fn) +
			    ROCKCHIP_PCIE_EP_MSI_CTRL_REG);
	return 0;
}

static int rockchip_pcie_ep_get_msi(struct pci_epc *epc, u8 fn, u8 vfn)
{
	struct rockchip_pcie_ep *ep = epc_get_drvdata(epc);
	struct rockchip_pcie *rockchip = &ep->rockchip;
	u32 flags;

	flags = rockchip_pcie_read(rockchip,
				   ROCKCHIP_PCIE_EP_FUNC_BASE(fn) +
				   ROCKCHIP_PCIE_EP_MSI_CTRL_REG);
	if (!(flags & ROCKCHIP_PCIE_EP_MSI_CTRL_ME))
		return -EINVAL;

	return ((flags & ROCKCHIP_PCIE_EP_MSI_CTRL_MME_MASK) >>
			ROCKCHIP_PCIE_EP_MSI_CTRL_MME_OFFSET);
}

static void rockchip_pcie_ep_assert_intx(struct rockchip_pcie_ep *ep, u8 fn,
					 u8 intx, bool do_assert)
{
	struct rockchip_pcie *rockchip = &ep->rockchip;

	intx &= 3;

	if (do_assert) {
		ep->irq_pending |= BIT(intx);
		rockchip_pcie_write(rockchip,
				    PCIE_CLIENT_INT_IN_ASSERT |
				    PCIE_CLIENT_INT_PEND_ST_PEND,
				    PCIE_CLIENT_LEGACY_INT_CTRL);
	} else {
		ep->irq_pending &= ~BIT(intx);
		rockchip_pcie_write(rockchip,
				    PCIE_CLIENT_INT_IN_DEASSERT |
				    PCIE_CLIENT_INT_PEND_ST_NORMAL,
				    PCIE_CLIENT_LEGACY_INT_CTRL);
	}
}

static int rockchip_pcie_ep_send_legacy_irq(struct rockchip_pcie_ep *ep, u8 fn,
					    u8 intx)
{
	u16 cmd;

	cmd = rockchip_pcie_read(&ep->rockchip,
				 ROCKCHIP_PCIE_EP_FUNC_BASE(fn) +
				 ROCKCHIP_PCIE_EP_CMD_STATUS);

	if (cmd & PCI_COMMAND_INTX_DISABLE)
		return -EINVAL;

	/*
	 * Should add some delay between toggling INTx per TRM vaguely saying
	 * it depends on some cycles of the AHB bus clock to function it. So
	 * add sufficient 1ms here.
	 */
	rockchip_pcie_ep_assert_intx(ep, fn, intx, true);
	mdelay(1);
	rockchip_pcie_ep_assert_intx(ep, fn, intx, false);
	return 0;
}

static int rockchip_pcie_ep_send_msi_irq(struct rockchip_pcie_ep *ep, u8 fn,
					 u8 interrupt_num)
{
	struct rockchip_pcie *rockchip = &ep->rockchip;
	u32 flags, mme, data, data_mask;
	u8 msi_count;
	u64 pci_addr;
	u32 r;

	/* Check MSI enable bit */
	flags = rockchip_pcie_read(&ep->rockchip,
				   ROCKCHIP_PCIE_EP_FUNC_BASE(fn) +
				   ROCKCHIP_PCIE_EP_MSI_CTRL_REG);
	if (!(flags & ROCKCHIP_PCIE_EP_MSI_CTRL_ME))
		return -EINVAL;

	/* Get MSI numbers from MME */
	mme = ((flags & ROCKCHIP_PCIE_EP_MSI_CTRL_MME_MASK) >>
			ROCKCHIP_PCIE_EP_MSI_CTRL_MME_OFFSET);
	msi_count = 1 << mme;
	if (!interrupt_num || interrupt_num > msi_count)
		return -EINVAL;

	/* Set MSI private data */
	data_mask = msi_count - 1;
	data = rockchip_pcie_read(rockchip,
				  ROCKCHIP_PCIE_EP_FUNC_BASE(fn) +
				  ROCKCHIP_PCIE_EP_MSI_CTRL_REG +
				  PCI_MSI_DATA_64);
	data = (data & ~data_mask) | ((interrupt_num - 1) & data_mask);

	/* Get MSI PCI address */
	pci_addr = rockchip_pcie_read(rockchip,
				      ROCKCHIP_PCIE_EP_FUNC_BASE(fn) +
				      ROCKCHIP_PCIE_EP_MSI_CTRL_REG +
				      PCI_MSI_ADDRESS_HI);
	pci_addr <<= 32;
	pci_addr |= rockchip_pcie_read(rockchip,
				       ROCKCHIP_PCIE_EP_FUNC_BASE(fn) +
				       ROCKCHIP_PCIE_EP_MSI_CTRL_REG +
				       PCI_MSI_ADDRESS_LO);

	/* Set the outbound region if needed. */
	if (unlikely(ep->irq_pci_addr != (pci_addr & PCIE_ADDR_MASK) ||
		     ep->irq_pci_fn != fn)) {
		r = rockchip_ob_region(ep->irq_phys_addr);
		rockchip_pcie_prog_ep_ob_atu(rockchip, fn, r,
					     ep->irq_phys_addr,
					     pci_addr & PCIE_ADDR_MASK,
					     ~PCIE_ADDR_MASK + 1);
		ep->irq_pci_addr = (pci_addr & PCIE_ADDR_MASK);
		ep->irq_pci_fn = fn;
	}

	writew(data, ep->irq_cpu_addr + (pci_addr & ~PCIE_ADDR_MASK));
	return 0;
}

static int rockchip_pcie_ep_raise_irq(struct pci_epc *epc, u8 fn, u8 vfn,
				      enum pci_epc_irq_type type,
				      u16 interrupt_num)
{
	struct rockchip_pcie_ep *ep = epc_get_drvdata(epc);

	switch (type) {
	case PCI_EPC_IRQ_LEGACY:
		return rockchip_pcie_ep_send_legacy_irq(ep, fn, 0);
	case PCI_EPC_IRQ_MSI:
		return rockchip_pcie_ep_send_msi_irq(ep, fn, interrupt_num);
	default:
		return -EINVAL;
	}
}

static int rockchip_pcie_ep_start(struct pci_epc *epc)
{
	struct rockchip_pcie_ep *ep = epc_get_drvdata(epc);
	struct rockchip_pcie *rockchip = &ep->rockchip;
	struct pci_epf *epf;
	u32 cfg;

	cfg = BIT(0);
	list_for_each_entry(epf, &epc->pci_epf, list)
		cfg |= BIT(epf->func_no);

	rockchip_pcie_write(rockchip, cfg, PCIE_CORE_PHY_FUNC_CFG);

	return 0;
}

static const struct pci_epc_features rockchip_pcie_epc_features = {
	.linkup_notifier = false,
	.msi_capable = true,
	.msix_capable = false,
	.align = 256,
};

static const struct pci_epc_features*
rockchip_pcie_ep_get_features(struct pci_epc *epc, u8 func_no, u8 vfunc_no)
{
	return &rockchip_pcie_epc_features;
}

static const struct pci_epc_ops rockchip_pcie_epc_ops = {
	.write_header	= rockchip_pcie_ep_write_header,
	.set_bar	= rockchip_pcie_ep_set_bar,
	.clear_bar	= rockchip_pcie_ep_clear_bar,
	.map_addr	= rockchip_pcie_ep_map_addr,
	.unmap_addr	= rockchip_pcie_ep_unmap_addr,
	.set_msi	= rockchip_pcie_ep_set_msi,
	.get_msi	= rockchip_pcie_ep_get_msi,
	.raise_irq	= rockchip_pcie_ep_raise_irq,
	.start		= rockchip_pcie_ep_start,
	.get_features	= rockchip_pcie_ep_get_features,
};

static int rockchip_pcie_parse_ep_dt(struct rockchip_pcie *rockchip,
				     struct rockchip_pcie_ep *ep)
{
	struct device *dev = rockchip->dev;
	int err;

	err = rockchip_pcie_parse_dt(rockchip);
	if (err)
		return err;

	err = rockchip_pcie_get_phys(rockchip);
	if (err)
		return err;

	err = of_property_read_u32(dev->of_node,
				   "rockchip,max-outbound-regions",
				   &ep->max_regions);
	if (err < 0 || ep->max_regions > MAX_REGION_LIMIT)
		ep->max_regions = MAX_REGION_LIMIT;

	ep->ob_region_map = 0;

	err = of_property_read_u8(dev->of_node, "max-functions",
				  &ep->epc->max_functions);
	if (err < 0)
		ep->epc->max_functions = 1;

	return 0;
}

static const struct of_device_id rockchip_pcie_ep_of_match[] = {
	{ .compatible = "rockchip,rk3399-pcie-ep"},
	{},
};

static int rockchip_pcie_ep_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_pcie_ep *ep;
	struct rockchip_pcie *rockchip;
	struct pci_epc *epc;
	size_t max_regions;
	struct pci_epc_mem_window *windows = NULL;
	int err, i;
	u32 cfg_msi, cfg_msix_cp;

	ep = devm_kzalloc(dev, sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return -ENOMEM;

	rockchip = &ep->rockchip;
	rockchip->is_rc = false;
	rockchip->dev = dev;

	epc = devm_pci_epc_create(dev, &rockchip_pcie_epc_ops);
	if (IS_ERR(epc)) {
		dev_err(dev, "failed to create epc device\n");
		return PTR_ERR(epc);
	}

	ep->epc = epc;
	epc_set_drvdata(epc, ep);

	err = rockchip_pcie_parse_ep_dt(rockchip, ep);
	if (err)
		return err;

	err = rockchip_pcie_enable_clocks(rockchip);
	if (err)
		return err;

	err = rockchip_pcie_init_port(rockchip);
	if (err)
		goto err_disable_clocks;

	/* Establish the link automatically */
	rockchip_pcie_write(rockchip, PCIE_CLIENT_LINK_TRAIN_ENABLE,
			    PCIE_CLIENT_CONFIG);

	max_regions = ep->max_regions;
	ep->ob_addr = devm_kcalloc(dev, max_regions, sizeof(*ep->ob_addr),
				   GFP_KERNEL);

	if (!ep->ob_addr) {
		err = -ENOMEM;
		goto err_uninit_port;
	}

	/* Only enable function 0 by default */
	rockchip_pcie_write(rockchip, BIT(0), PCIE_CORE_PHY_FUNC_CFG);

	windows = devm_kcalloc(dev, ep->max_regions,
			       sizeof(struct pci_epc_mem_window), GFP_KERNEL);
	if (!windows) {
		err = -ENOMEM;
		goto err_uninit_port;
	}
	for (i = 0; i < ep->max_regions; i++) {
		windows[i].phys_base = rockchip->mem_res->start + (SZ_1M * i);
		windows[i].size = SZ_1M;
		windows[i].page_size = SZ_1M;
	}
	err = pci_epc_multi_mem_init(epc, windows, ep->max_regions);
	devm_kfree(dev, windows);

	if (err < 0) {
		dev_err(dev, "failed to initialize the memory space\n");
		goto err_uninit_port;
	}

	ep->irq_cpu_addr = pci_epc_mem_alloc_addr(epc, &ep->irq_phys_addr,
						  SZ_1M);
	if (!ep->irq_cpu_addr) {
		dev_err(dev, "failed to reserve memory space for MSI\n");
		err = -ENOMEM;
		goto err_epc_mem_exit;
	}

	ep->irq_pci_addr = ROCKCHIP_PCIE_EP_DUMMY_IRQ_ADDR;

	/*
	 * MSI-X is not supported but the controller still advertises the MSI-X
	 * capability by default, which can lead to the Root Complex side
	 * allocating MSI-X vectors which cannot be used. Avoid this by skipping
	 * the MSI-X capability entry in the PCIe capabilities linked-list: get
	 * the next pointer from the MSI-X entry and set that in the MSI
	 * capability entry (which is the previous entry). This way the MSI-X
	 * entry is skipped (left out of the linked-list) and not advertised.
	 */
	cfg_msi = rockchip_pcie_read(rockchip, PCIE_EP_CONFIG_BASE +
				     ROCKCHIP_PCIE_EP_MSI_CTRL_REG);

	cfg_msi &= ~ROCKCHIP_PCIE_EP_MSI_CP1_MASK;

	cfg_msix_cp = rockchip_pcie_read(rockchip, PCIE_EP_CONFIG_BASE +
					 ROCKCHIP_PCIE_EP_MSIX_CAP_REG) &
					 ROCKCHIP_PCIE_EP_MSIX_CAP_CP_MASK;

	cfg_msi |= cfg_msix_cp;

	rockchip_pcie_write(rockchip, cfg_msi,
			    PCIE_EP_CONFIG_BASE + ROCKCHIP_PCIE_EP_MSI_CTRL_REG);

	rockchip_pcie_write(rockchip, PCIE_CLIENT_CONF_ENABLE,
			    PCIE_CLIENT_CONFIG);

	return 0;
err_epc_mem_exit:
	pci_epc_mem_exit(epc);
err_uninit_port:
	rockchip_pcie_deinit_phys(rockchip);
err_disable_clocks:
	rockchip_pcie_disable_clocks(rockchip);
	return err;
}

static struct platform_driver rockchip_pcie_ep_driver = {
	.driver = {
		.name = "rockchip-pcie-ep",
		.of_match_table = rockchip_pcie_ep_of_match,
	},
	.probe = rockchip_pcie_ep_probe,
};

builtin_platform_driver(rockchip_pcie_ep_driver);
