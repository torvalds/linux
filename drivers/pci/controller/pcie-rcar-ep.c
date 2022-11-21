// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe endpoint driver for Renesas R-Car SoCs
 *  Copyright (c) 2020 Renesas Electronics Europe GmbH
 *
 * Author: Lad Prabhakar <prabhakar.mahadev-lad.rj@bp.renesas.com>
 */

#include <linux/delay.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/pci-epc.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "pcie-rcar.h"

#define RCAR_EPC_MAX_FUNCTIONS		1

/* Structure representing the PCIe interface */
struct rcar_pcie_endpoint {
	struct rcar_pcie	pcie;
	phys_addr_t		*ob_mapped_addr;
	struct pci_epc_mem_window *ob_window;
	u8			max_functions;
	unsigned int		bar_to_atu[MAX_NR_INBOUND_MAPS];
	unsigned long		*ib_window_map;
	u32			num_ib_windows;
	u32			num_ob_windows;
};

static void rcar_pcie_ep_hw_init(struct rcar_pcie *pcie)
{
	u32 val;

	rcar_pci_write_reg(pcie, 0, PCIETCTLR);

	/* Set endpoint mode */
	rcar_pci_write_reg(pcie, 0, PCIEMSR);

	/* Initialize default capabilities. */
	rcar_rmw32(pcie, REXPCAP(0), 0xff, PCI_CAP_ID_EXP);
	rcar_rmw32(pcie, REXPCAP(PCI_EXP_FLAGS),
		   PCI_EXP_FLAGS_TYPE, PCI_EXP_TYPE_ENDPOINT << 4);
	rcar_rmw32(pcie, RCONF(PCI_HEADER_TYPE), 0x7f,
		   PCI_HEADER_TYPE_NORMAL);

	/* Write out the physical slot number = 0 */
	rcar_rmw32(pcie, REXPCAP(PCI_EXP_SLTCAP), PCI_EXP_SLTCAP_PSN, 0);

	val = rcar_pci_read_reg(pcie, EXPCAP(1));
	/* device supports fixed 128 bytes MPSS */
	val &= ~GENMASK(2, 0);
	rcar_pci_write_reg(pcie, val, EXPCAP(1));

	val = rcar_pci_read_reg(pcie, EXPCAP(2));
	/* read requests size 128 bytes */
	val &= ~GENMASK(14, 12);
	/* payload size 128 bytes */
	val &= ~GENMASK(7, 5);
	rcar_pci_write_reg(pcie, val, EXPCAP(2));

	/* Set target link speed to 5.0 GT/s */
	rcar_rmw32(pcie, EXPCAP(12), PCI_EXP_LNKSTA_CLS,
		   PCI_EXP_LNKSTA_CLS_5_0GB);

	/* Set the completion timer timeout to the maximum 50ms. */
	rcar_rmw32(pcie, TLCTLR + 1, 0x3f, 50);

	/* Terminate list of capabilities (Next Capability Offset=0) */
	rcar_rmw32(pcie, RVCCAP(0), 0xfff00000, 0);

	/* flush modifications */
	wmb();
}

static int rcar_pcie_ep_get_window(struct rcar_pcie_endpoint *ep,
				   phys_addr_t addr)
{
	int i;

	for (i = 0; i < ep->num_ob_windows; i++)
		if (ep->ob_window[i].phys_base == addr)
			return i;

	return -EINVAL;
}

static int rcar_pcie_parse_outbound_ranges(struct rcar_pcie_endpoint *ep,
					   struct platform_device *pdev)
{
	struct rcar_pcie *pcie = &ep->pcie;
	char outbound_name[10];
	struct resource *res;
	unsigned int i = 0;

	ep->num_ob_windows = 0;
	for (i = 0; i < RCAR_PCI_MAX_RESOURCES; i++) {
		sprintf(outbound_name, "memory%u", i);
		res = platform_get_resource_byname(pdev,
						   IORESOURCE_MEM,
						   outbound_name);
		if (!res) {
			dev_err(pcie->dev, "missing outbound window %u\n", i);
			return -EINVAL;
		}
		if (!devm_request_mem_region(&pdev->dev, res->start,
					     resource_size(res),
					     outbound_name)) {
			dev_err(pcie->dev, "Cannot request memory region %s.\n",
				outbound_name);
			return -EIO;
		}

		ep->ob_window[i].phys_base = res->start;
		ep->ob_window[i].size = resource_size(res);
		/* controller doesn't support multiple allocation
		 * from same window, so set page_size to window size
		 */
		ep->ob_window[i].page_size = resource_size(res);
	}
	ep->num_ob_windows = i;

	return 0;
}

static int rcar_pcie_ep_get_pdata(struct rcar_pcie_endpoint *ep,
				  struct platform_device *pdev)
{
	struct rcar_pcie *pcie = &ep->pcie;
	struct pci_epc_mem_window *window;
	struct device *dev = pcie->dev;
	struct resource res;
	int err;

	err = of_address_to_resource(dev->of_node, 0, &res);
	if (err)
		return err;
	pcie->base = devm_ioremap_resource(dev, &res);
	if (IS_ERR(pcie->base))
		return PTR_ERR(pcie->base);

	ep->ob_window = devm_kcalloc(dev, RCAR_PCI_MAX_RESOURCES,
				     sizeof(*window), GFP_KERNEL);
	if (!ep->ob_window)
		return -ENOMEM;

	rcar_pcie_parse_outbound_ranges(ep, pdev);

	err = of_property_read_u8(dev->of_node, "max-functions",
				  &ep->max_functions);
	if (err < 0 || ep->max_functions > RCAR_EPC_MAX_FUNCTIONS)
		ep->max_functions = RCAR_EPC_MAX_FUNCTIONS;

	return 0;
}

static int rcar_pcie_ep_write_header(struct pci_epc *epc, u8 fn, u8 vfn,
				     struct pci_epf_header *hdr)
{
	struct rcar_pcie_endpoint *ep = epc_get_drvdata(epc);
	struct rcar_pcie *pcie = &ep->pcie;
	u32 val;

	if (!fn)
		val = hdr->vendorid;
	else
		val = rcar_pci_read_reg(pcie, IDSETR0);
	val |= hdr->deviceid << 16;
	rcar_pci_write_reg(pcie, val, IDSETR0);

	val = hdr->revid;
	val |= hdr->progif_code << 8;
	val |= hdr->subclass_code << 16;
	val |= hdr->baseclass_code << 24;
	rcar_pci_write_reg(pcie, val, IDSETR1);

	if (!fn)
		val = hdr->subsys_vendor_id;
	else
		val = rcar_pci_read_reg(pcie, SUBIDSETR);
	val |= hdr->subsys_id << 16;
	rcar_pci_write_reg(pcie, val, SUBIDSETR);

	if (hdr->interrupt_pin > PCI_INTERRUPT_INTA)
		return -EINVAL;
	val = rcar_pci_read_reg(pcie, PCICONF(15));
	val |= (hdr->interrupt_pin << 8);
	rcar_pci_write_reg(pcie, val, PCICONF(15));

	return 0;
}

static int rcar_pcie_ep_set_bar(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				struct pci_epf_bar *epf_bar)
{
	int flags = epf_bar->flags | LAR_ENABLE | LAM_64BIT;
	struct rcar_pcie_endpoint *ep = epc_get_drvdata(epc);
	u64 size = 1ULL << fls64(epf_bar->size - 1);
	dma_addr_t cpu_addr = epf_bar->phys_addr;
	enum pci_barno bar = epf_bar->barno;
	struct rcar_pcie *pcie = &ep->pcie;
	u32 mask;
	int idx;
	int err;

	idx = find_first_zero_bit(ep->ib_window_map, ep->num_ib_windows);
	if (idx >= ep->num_ib_windows) {
		dev_err(pcie->dev, "no free inbound window\n");
		return -EINVAL;
	}

	if ((flags & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_IO)
		flags |= IO_SPACE;

	ep->bar_to_atu[bar] = idx;
	/* use 64-bit BARs */
	set_bit(idx, ep->ib_window_map);
	set_bit(idx + 1, ep->ib_window_map);

	if (cpu_addr > 0) {
		unsigned long nr_zeros = __ffs64(cpu_addr);
		u64 alignment = 1ULL << nr_zeros;

		size = min(size, alignment);
	}

	size = min(size, 1ULL << 32);

	mask = roundup_pow_of_two(size) - 1;
	mask &= ~0xf;

	rcar_pcie_set_inbound(pcie, cpu_addr,
			      0x0, mask | flags, idx, false);

	err = rcar_pcie_wait_for_phyrdy(pcie);
	if (err) {
		dev_err(pcie->dev, "phy not ready\n");
		return -EINVAL;
	}

	return 0;
}

static void rcar_pcie_ep_clear_bar(struct pci_epc *epc, u8 fn, u8 vfn,
				   struct pci_epf_bar *epf_bar)
{
	struct rcar_pcie_endpoint *ep = epc_get_drvdata(epc);
	enum pci_barno bar = epf_bar->barno;
	u32 atu_index = ep->bar_to_atu[bar];

	rcar_pcie_set_inbound(&ep->pcie, 0x0, 0x0, 0x0, bar, false);

	clear_bit(atu_index, ep->ib_window_map);
	clear_bit(atu_index + 1, ep->ib_window_map);
}

static int rcar_pcie_ep_set_msi(struct pci_epc *epc, u8 fn, u8 vfn,
				u8 interrupts)
{
	struct rcar_pcie_endpoint *ep = epc_get_drvdata(epc);
	struct rcar_pcie *pcie = &ep->pcie;
	u32 flags;

	flags = rcar_pci_read_reg(pcie, MSICAP(fn));
	flags |= interrupts << MSICAP0_MMESCAP_OFFSET;
	rcar_pci_write_reg(pcie, flags, MSICAP(fn));

	return 0;
}

static int rcar_pcie_ep_get_msi(struct pci_epc *epc, u8 fn, u8 vfn)
{
	struct rcar_pcie_endpoint *ep = epc_get_drvdata(epc);
	struct rcar_pcie *pcie = &ep->pcie;
	u32 flags;

	flags = rcar_pci_read_reg(pcie, MSICAP(fn));
	if (!(flags & MSICAP0_MSIE))
		return -EINVAL;

	return ((flags & MSICAP0_MMESE_MASK) >> MSICAP0_MMESE_OFFSET);
}

static int rcar_pcie_ep_map_addr(struct pci_epc *epc, u8 fn, u8 vfn,
				 phys_addr_t addr, u64 pci_addr, size_t size)
{
	struct rcar_pcie_endpoint *ep = epc_get_drvdata(epc);
	struct rcar_pcie *pcie = &ep->pcie;
	struct resource_entry win;
	struct resource res;
	int window;
	int err;

	/* check if we have a link. */
	err = rcar_pcie_wait_for_dl(pcie);
	if (err) {
		dev_err(pcie->dev, "link not up\n");
		return err;
	}

	window = rcar_pcie_ep_get_window(ep, addr);
	if (window < 0) {
		dev_err(pcie->dev, "failed to get corresponding window\n");
		return -EINVAL;
	}

	memset(&win, 0x0, sizeof(win));
	memset(&res, 0x0, sizeof(res));
	res.start = pci_addr;
	res.end = pci_addr + size - 1;
	res.flags = IORESOURCE_MEM;
	win.res = &res;

	rcar_pcie_set_outbound(pcie, window, &win);

	ep->ob_mapped_addr[window] = addr;

	return 0;
}

static void rcar_pcie_ep_unmap_addr(struct pci_epc *epc, u8 fn, u8 vfn,
				    phys_addr_t addr)
{
	struct rcar_pcie_endpoint *ep = epc_get_drvdata(epc);
	struct resource_entry win;
	struct resource res;
	int idx;

	for (idx = 0; idx < ep->num_ob_windows; idx++)
		if (ep->ob_mapped_addr[idx] == addr)
			break;

	if (idx >= ep->num_ob_windows)
		return;

	memset(&win, 0x0, sizeof(win));
	memset(&res, 0x0, sizeof(res));
	win.res = &res;
	rcar_pcie_set_outbound(&ep->pcie, idx, &win);

	ep->ob_mapped_addr[idx] = 0;
}

static int rcar_pcie_ep_assert_intx(struct rcar_pcie_endpoint *ep,
				    u8 fn, u8 intx)
{
	struct rcar_pcie *pcie = &ep->pcie;
	u32 val;

	val = rcar_pci_read_reg(pcie, PCIEMSITXR);
	if ((val & PCI_MSI_FLAGS_ENABLE)) {
		dev_err(pcie->dev, "MSI is enabled, cannot assert INTx\n");
		return -EINVAL;
	}

	val = rcar_pci_read_reg(pcie, PCICONF(1));
	if ((val & INTDIS)) {
		dev_err(pcie->dev, "INTx message transmission is disabled\n");
		return -EINVAL;
	}

	val = rcar_pci_read_reg(pcie, PCIEINTXR);
	if ((val & ASTINTX)) {
		dev_err(pcie->dev, "INTx is already asserted\n");
		return -EINVAL;
	}

	val |= ASTINTX;
	rcar_pci_write_reg(pcie, val, PCIEINTXR);
	usleep_range(1000, 1001);
	val = rcar_pci_read_reg(pcie, PCIEINTXR);
	val &= ~ASTINTX;
	rcar_pci_write_reg(pcie, val, PCIEINTXR);

	return 0;
}

static int rcar_pcie_ep_assert_msi(struct rcar_pcie *pcie,
				   u8 fn, u8 interrupt_num)
{
	u16 msi_count;
	u32 val;

	/* Check MSI enable bit */
	val = rcar_pci_read_reg(pcie, MSICAP(fn));
	if (!(val & MSICAP0_MSIE))
		return -EINVAL;

	/* Get MSI numbers from MME */
	msi_count = ((val & MSICAP0_MMESE_MASK) >> MSICAP0_MMESE_OFFSET);
	msi_count = 1 << msi_count;

	if (!interrupt_num || interrupt_num > msi_count)
		return -EINVAL;

	val = rcar_pci_read_reg(pcie, PCIEMSITXR);
	rcar_pci_write_reg(pcie, val | (interrupt_num - 1), PCIEMSITXR);

	return 0;
}

static int rcar_pcie_ep_raise_irq(struct pci_epc *epc, u8 fn, u8 vfn,
				  enum pci_epc_irq_type type,
				  u16 interrupt_num)
{
	struct rcar_pcie_endpoint *ep = epc_get_drvdata(epc);

	switch (type) {
	case PCI_EPC_IRQ_LEGACY:
		return rcar_pcie_ep_assert_intx(ep, fn, 0);

	case PCI_EPC_IRQ_MSI:
		return rcar_pcie_ep_assert_msi(&ep->pcie, fn, interrupt_num);

	default:
		return -EINVAL;
	}
}

static int rcar_pcie_ep_start(struct pci_epc *epc)
{
	struct rcar_pcie_endpoint *ep = epc_get_drvdata(epc);

	rcar_pci_write_reg(&ep->pcie, MACCTLR_INIT_VAL, MACCTLR);
	rcar_pci_write_reg(&ep->pcie, CFINIT, PCIETCTLR);

	return 0;
}

static void rcar_pcie_ep_stop(struct pci_epc *epc)
{
	struct rcar_pcie_endpoint *ep = epc_get_drvdata(epc);

	rcar_pci_write_reg(&ep->pcie, 0, PCIETCTLR);
}

static const struct pci_epc_features rcar_pcie_epc_features = {
	.linkup_notifier = false,
	.msi_capable = true,
	.msix_capable = false,
	/* use 64-bit BARs so mark BAR[1,3,5] as reserved */
	.reserved_bar = 1 << BAR_1 | 1 << BAR_3 | 1 << BAR_5,
	.bar_fixed_64bit = 1 << BAR_0 | 1 << BAR_2 | 1 << BAR_4,
	.bar_fixed_size[0] = 128,
	.bar_fixed_size[2] = 256,
	.bar_fixed_size[4] = 256,
};

static const struct pci_epc_features*
rcar_pcie_ep_get_features(struct pci_epc *epc, u8 func_no, u8 vfunc_no)
{
	return &rcar_pcie_epc_features;
}

static const struct pci_epc_ops rcar_pcie_epc_ops = {
	.write_header	= rcar_pcie_ep_write_header,
	.set_bar	= rcar_pcie_ep_set_bar,
	.clear_bar	= rcar_pcie_ep_clear_bar,
	.set_msi	= rcar_pcie_ep_set_msi,
	.get_msi	= rcar_pcie_ep_get_msi,
	.map_addr	= rcar_pcie_ep_map_addr,
	.unmap_addr	= rcar_pcie_ep_unmap_addr,
	.raise_irq	= rcar_pcie_ep_raise_irq,
	.start		= rcar_pcie_ep_start,
	.stop		= rcar_pcie_ep_stop,
	.get_features	= rcar_pcie_ep_get_features,
};

static const struct of_device_id rcar_pcie_ep_of_match[] = {
	{ .compatible = "renesas,r8a774c0-pcie-ep", },
	{ .compatible = "renesas,rcar-gen3-pcie-ep" },
	{ },
};

static int rcar_pcie_ep_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rcar_pcie_endpoint *ep;
	struct rcar_pcie *pcie;
	struct pci_epc *epc;
	int err;

	ep = devm_kzalloc(dev, sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return -ENOMEM;

	pcie = &ep->pcie;
	pcie->dev = dev;

	pm_runtime_enable(dev);
	err = pm_runtime_resume_and_get(dev);
	if (err < 0) {
		dev_err(dev, "pm_runtime_resume_and_get failed\n");
		goto err_pm_disable;
	}

	err = rcar_pcie_ep_get_pdata(ep, pdev);
	if (err < 0) {
		dev_err(dev, "failed to request resources: %d\n", err);
		goto err_pm_put;
	}

	ep->num_ib_windows = MAX_NR_INBOUND_MAPS;
	ep->ib_window_map =
			devm_kcalloc(dev, BITS_TO_LONGS(ep->num_ib_windows),
				     sizeof(long), GFP_KERNEL);
	if (!ep->ib_window_map) {
		err = -ENOMEM;
		dev_err(dev, "failed to allocate memory for inbound map\n");
		goto err_pm_put;
	}

	ep->ob_mapped_addr = devm_kcalloc(dev, ep->num_ob_windows,
					  sizeof(*ep->ob_mapped_addr),
					  GFP_KERNEL);
	if (!ep->ob_mapped_addr) {
		err = -ENOMEM;
		dev_err(dev, "failed to allocate memory for outbound memory pointers\n");
		goto err_pm_put;
	}

	epc = devm_pci_epc_create(dev, &rcar_pcie_epc_ops);
	if (IS_ERR(epc)) {
		dev_err(dev, "failed to create epc device\n");
		err = PTR_ERR(epc);
		goto err_pm_put;
	}

	epc->max_functions = ep->max_functions;
	epc_set_drvdata(epc, ep);

	rcar_pcie_ep_hw_init(pcie);

	err = pci_epc_multi_mem_init(epc, ep->ob_window, ep->num_ob_windows);
	if (err < 0) {
		dev_err(dev, "failed to initialize the epc memory space\n");
		goto err_pm_put;
	}

	return 0;

err_pm_put:
	pm_runtime_put(dev);

err_pm_disable:
	pm_runtime_disable(dev);

	return err;
}

static struct platform_driver rcar_pcie_ep_driver = {
	.driver = {
		.name = "rcar-pcie-ep",
		.of_match_table = rcar_pcie_ep_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = rcar_pcie_ep_probe,
};
builtin_platform_driver(rcar_pcie_ep_driver);
