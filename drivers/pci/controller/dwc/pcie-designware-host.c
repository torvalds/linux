// SPDX-License-Identifier: GPL-2.0
/*
 * Synopsys DesignWare PCIe host controller driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		https://www.samsung.com
 *
 * Author: Jingoo Han <jg1.han@samsung.com>
 */

#include <linux/align.h>
#include <linux/iopoll.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqchip/irq-msi-lib.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/pci_regs.h>
#include <linux/platform_device.h>

#include "../../pci.h"
#include "pcie-designware.h"

static struct pci_ops dw_pcie_ops;
static struct pci_ops dw_pcie_ecam_ops;
static struct pci_ops dw_child_pcie_ops;

#ifdef CONFIG_SMP
static void dw_irq_noop(struct irq_data *d) { }
#endif

static bool dw_pcie_init_dev_msi_info(struct device *dev, struct irq_domain *domain,
				      struct irq_domain *real_parent, struct msi_domain_info *info)
{
	if (!msi_lib_init_dev_msi_info(dev, domain, real_parent, info))
		return false;

#ifdef CONFIG_SMP
	info->chip->irq_ack = dw_irq_noop;
	info->chip->irq_pre_redirect = irq_chip_pre_redirect_parent;
#else
	info->chip->irq_ack = irq_chip_ack_parent;
#endif
	return true;
}

#define DW_PCIE_MSI_FLAGS_REQUIRED (MSI_FLAG_USE_DEF_DOM_OPS		| \
				    MSI_FLAG_USE_DEF_CHIP_OPS		| \
				    MSI_FLAG_PCI_MSI_MASK_PARENT)
#define DW_PCIE_MSI_FLAGS_SUPPORTED (MSI_FLAG_MULTI_PCI_MSI		| \
				     MSI_FLAG_PCI_MSIX			| \
				     MSI_GENERIC_FLAGS_MASK)

#define IS_256MB_ALIGNED(x) IS_ALIGNED(x, SZ_256M)

static const struct msi_parent_ops dw_pcie_msi_parent_ops = {
	.required_flags		= DW_PCIE_MSI_FLAGS_REQUIRED,
	.supported_flags	= DW_PCIE_MSI_FLAGS_SUPPORTED,
	.bus_select_token	= DOMAIN_BUS_PCI_MSI,
	.prefix			= "DW-",
	.init_dev_msi_info	= dw_pcie_init_dev_msi_info,
};

/* MSI int handler */
void dw_handle_msi_irq(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	unsigned int i, num_ctrls;

	num_ctrls = pp->num_vectors / MAX_MSI_IRQS_PER_CTRL;

	for (i = 0; i < num_ctrls; i++) {
		unsigned int reg_off = i * MSI_REG_CTRL_BLOCK_SIZE;
		unsigned int irq_off = i * MAX_MSI_IRQS_PER_CTRL;
		unsigned long status, pos;

		status = dw_pcie_readl_dbi(pci, PCIE_MSI_INTR0_STATUS + reg_off);
		if (!status)
			continue;

		for_each_set_bit(pos, &status, MAX_MSI_IRQS_PER_CTRL)
			generic_handle_demux_domain_irq(pp->irq_domain, irq_off + pos);
	}
}

/* Chained MSI interrupt service routine */
static void dw_chained_msi_isr(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct dw_pcie_rp *pp;

	chained_irq_enter(chip, desc);

	pp = irq_desc_get_handler_data(desc);
	dw_handle_msi_irq(pp);

	chained_irq_exit(chip, desc);
}

static void dw_pci_setup_msi_msg(struct irq_data *d, struct msi_msg *msg)
{
	struct dw_pcie_rp *pp = irq_data_get_irq_chip_data(d);
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	u64 msi_target = (u64)pp->msi_data;

	msg->address_lo = lower_32_bits(msi_target);
	msg->address_hi = upper_32_bits(msi_target);
	msg->data = d->hwirq;

	dev_dbg(pci->dev, "msi#%d address_hi %#x address_lo %#x\n",
		(int)d->hwirq, msg->address_hi, msg->address_lo);
}

static void dw_pci_bottom_mask(struct irq_data *d)
{
	struct dw_pcie_rp *pp = irq_data_get_irq_chip_data(d);
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	unsigned int res, bit, ctrl;

	guard(raw_spinlock)(&pp->lock);
	ctrl = d->hwirq / MAX_MSI_IRQS_PER_CTRL;
	res = ctrl * MSI_REG_CTRL_BLOCK_SIZE;
	bit = d->hwirq % MAX_MSI_IRQS_PER_CTRL;

	pp->irq_mask[ctrl] |= BIT(bit);
	dw_pcie_writel_dbi(pci, PCIE_MSI_INTR0_MASK + res, pp->irq_mask[ctrl]);
}

static void dw_pci_bottom_unmask(struct irq_data *d)
{
	struct dw_pcie_rp *pp = irq_data_get_irq_chip_data(d);
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	unsigned int res, bit, ctrl;

	guard(raw_spinlock)(&pp->lock);
	ctrl = d->hwirq / MAX_MSI_IRQS_PER_CTRL;
	res = ctrl * MSI_REG_CTRL_BLOCK_SIZE;
	bit = d->hwirq % MAX_MSI_IRQS_PER_CTRL;

	pp->irq_mask[ctrl] &= ~BIT(bit);
	dw_pcie_writel_dbi(pci, PCIE_MSI_INTR0_MASK + res, pp->irq_mask[ctrl]);
}

static void dw_pci_bottom_ack(struct irq_data *d)
{
	struct dw_pcie_rp *pp  = irq_data_get_irq_chip_data(d);
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	unsigned int res, bit, ctrl;

	ctrl = d->hwirq / MAX_MSI_IRQS_PER_CTRL;
	res = ctrl * MSI_REG_CTRL_BLOCK_SIZE;
	bit = d->hwirq % MAX_MSI_IRQS_PER_CTRL;

	dw_pcie_writel_dbi(pci, PCIE_MSI_INTR0_STATUS + res, BIT(bit));
}

static struct irq_chip dw_pci_msi_bottom_irq_chip = {
	.name			= "DWPCI-MSI",
	.irq_compose_msi_msg	= dw_pci_setup_msi_msg,
	.irq_mask		= dw_pci_bottom_mask,
	.irq_unmask		= dw_pci_bottom_unmask,
#ifdef CONFIG_SMP
	.irq_ack		= dw_irq_noop,
	.irq_pre_redirect	= dw_pci_bottom_ack,
	.irq_set_affinity	= irq_chip_redirect_set_affinity,
#else
	.irq_ack		= dw_pci_bottom_ack,
#endif
};

static int dw_pcie_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				    unsigned int nr_irqs, void *args)
{
	struct dw_pcie_rp *pp = domain->host_data;
	int bit;

	scoped_guard (raw_spinlock_irq, &pp->lock) {
		bit = bitmap_find_free_region(pp->msi_irq_in_use, pp->num_vectors,
					      order_base_2(nr_irqs));
	}

	if (bit < 0)
		return -ENOSPC;

	for (unsigned int i = 0; i < nr_irqs; i++) {
		irq_domain_set_info(domain, virq + i, bit + i, pp->msi_irq_chip,
				    pp, handle_edge_irq, NULL, NULL);
	}
	return 0;
}

static void dw_pcie_irq_domain_free(struct irq_domain *domain, unsigned int virq,
				    unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct dw_pcie_rp *pp = domain->host_data;

	guard(raw_spinlock_irq)(&pp->lock);
	bitmap_release_region(pp->msi_irq_in_use, d->hwirq, order_base_2(nr_irqs));
}

static const struct irq_domain_ops dw_pcie_msi_domain_ops = {
	.alloc	= dw_pcie_irq_domain_alloc,
	.free	= dw_pcie_irq_domain_free,
};

int dw_pcie_allocate_domains(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct irq_domain_info info = {
		.fwnode		= dev_fwnode(pci->dev),
		.ops		= &dw_pcie_msi_domain_ops,
		.size		= pp->num_vectors,
		.host_data	= pp,
	};

	pp->irq_domain = msi_create_parent_irq_domain(&info, &dw_pcie_msi_parent_ops);
	if (!pp->irq_domain) {
		dev_err(pci->dev, "Failed to create IRQ domain\n");
		return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dw_pcie_allocate_domains);

void dw_pcie_free_msi(struct dw_pcie_rp *pp)
{
	u32 ctrl;

	for (ctrl = 0; ctrl < MAX_MSI_CTRLS; ctrl++) {
		if (pp->msi_irq[ctrl] > 0)
			irq_set_chained_handler_and_data(pp->msi_irq[ctrl], NULL, NULL);
	}

	irq_domain_remove(pp->irq_domain);
}
EXPORT_SYMBOL_GPL(dw_pcie_free_msi);

void dw_pcie_msi_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	u64 msi_target = (u64)pp->msi_data;
	u32 ctrl, num_ctrls;

	if (!pci_msi_enabled() || !pp->use_imsi_rx)
		return;

	num_ctrls = pp->num_vectors / MAX_MSI_IRQS_PER_CTRL;

	/* Initialize IRQ Status array */
	for (ctrl = 0; ctrl < num_ctrls; ctrl++) {
		dw_pcie_writel_dbi(pci, PCIE_MSI_INTR0_MASK +
				    (ctrl * MSI_REG_CTRL_BLOCK_SIZE),
				    pp->irq_mask[ctrl]);
		dw_pcie_writel_dbi(pci, PCIE_MSI_INTR0_ENABLE +
				    (ctrl * MSI_REG_CTRL_BLOCK_SIZE),
				    ~0);
	}

	/* Program the msi_data */
	dw_pcie_writel_dbi(pci, PCIE_MSI_ADDR_LO, lower_32_bits(msi_target));
	dw_pcie_writel_dbi(pci, PCIE_MSI_ADDR_HI, upper_32_bits(msi_target));
}
EXPORT_SYMBOL_GPL(dw_pcie_msi_init);

static int dw_pcie_parse_split_msi_irq(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct device *dev = pci->dev;
	struct platform_device *pdev = to_platform_device(dev);
	u32 ctrl, max_vectors;
	int irq;

	/* Parse any "msiX" IRQs described in the devicetree */
	for (ctrl = 0; ctrl < MAX_MSI_CTRLS; ctrl++) {
		char msi_name[] = "msiX";

		msi_name[3] = '0' + ctrl;
		irq = platform_get_irq_byname_optional(pdev, msi_name);
		if (irq == -ENXIO)
			break;
		if (irq < 0)
			return dev_err_probe(dev, irq,
					     "Failed to parse MSI IRQ '%s'\n",
					     msi_name);

		pp->msi_irq[ctrl] = irq;
	}

	/* If no "msiX" IRQs, caller should fallback to "msi" IRQ */
	if (ctrl == 0)
		return -ENXIO;

	max_vectors = ctrl * MAX_MSI_IRQS_PER_CTRL;
	if (pp->num_vectors > max_vectors) {
		dev_warn(dev, "Exceeding number of MSI vectors, limiting to %u\n",
			 max_vectors);
		pp->num_vectors = max_vectors;
	}
	if (!pp->num_vectors)
		pp->num_vectors = max_vectors;

	return 0;
}

int dw_pcie_msi_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct device *dev = pci->dev;
	struct platform_device *pdev = to_platform_device(dev);
	u64 *msi_vaddr = NULL;
	int ret;
	u32 ctrl, num_ctrls;

	for (ctrl = 0; ctrl < MAX_MSI_CTRLS; ctrl++)
		pp->irq_mask[ctrl] = ~0;

	if (!pp->msi_irq[0]) {
		ret = dw_pcie_parse_split_msi_irq(pp);
		if (ret < 0 && ret != -ENXIO)
			return ret;
	}

	if (!pp->num_vectors)
		pp->num_vectors = MSI_DEF_NUM_VECTORS;
	num_ctrls = pp->num_vectors / MAX_MSI_IRQS_PER_CTRL;

	if (!pp->msi_irq[0]) {
		pp->msi_irq[0] = platform_get_irq_byname_optional(pdev, "msi");
		if (pp->msi_irq[0] < 0) {
			pp->msi_irq[0] = platform_get_irq(pdev, 0);
			if (pp->msi_irq[0] < 0)
				return pp->msi_irq[0];
		}
	}

	dev_dbg(dev, "Using %d MSI vectors\n", pp->num_vectors);

	pp->msi_irq_chip = &dw_pci_msi_bottom_irq_chip;

	ret = dw_pcie_allocate_domains(pp);
	if (ret)
		return ret;

	for (ctrl = 0; ctrl < num_ctrls; ctrl++) {
		if (pp->msi_irq[ctrl] > 0)
			irq_set_chained_handler_and_data(pp->msi_irq[ctrl],
						    dw_chained_msi_isr, pp);
	}

	/*
	 * Even though the iMSI-RX Module supports 64-bit addresses some
	 * peripheral PCIe devices may lack 64-bit message support. In
	 * order not to miss MSI TLPs from those devices the MSI target
	 * address has to be within the lowest 4GB.
	 *
	 * Per DWC databook r6.21a, section 3.10.2.3, the incoming MWr TLP
	 * targeting the MSI_CTRL_ADDR is terminated by the iMSI-RX and never
	 * appears on the AXI bus. So MSI_CTRL_ADDR address doesn't need to be
	 * mapped and can be any memory that doesn't get allocated for the BAR
	 * memory. Since most of the platforms provide 32-bit address for
	 * 'config' region, try cfg0_base as the first option for the MSI target
	 * address if it's a 32-bit address. Otherwise, try 32-bit and 64-bit
	 * coherent memory allocation one by one.
	 */
	if (!(pp->cfg0_base & GENMASK_ULL(63, 32))) {
		pp->msi_data = pp->cfg0_base;
		return 0;
	}

	ret = dma_set_coherent_mask(dev, DMA_BIT_MASK(32));
	if (!ret)
		msi_vaddr = dmam_alloc_coherent(dev, sizeof(u64), &pp->msi_data,
						GFP_KERNEL);

	if (!msi_vaddr) {
		dev_warn(dev, "Failed to allocate 32-bit MSI address\n");
		dma_set_coherent_mask(dev, DMA_BIT_MASK(64));
		msi_vaddr = dmam_alloc_coherent(dev, sizeof(u64), &pp->msi_data,
						GFP_KERNEL);
		if (!msi_vaddr) {
			dev_err(dev, "Failed to allocate MSI address\n");
			dw_pcie_free_msi(pp);
			return -ENOMEM;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dw_pcie_msi_host_init);

static void dw_pcie_host_request_msg_tlp_res(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct resource_entry *win;
	struct resource *res;

	win = resource_list_first_type(&pp->bridge->windows, IORESOURCE_MEM);
	if (win) {
		res = devm_kzalloc(pci->dev, sizeof(*res), GFP_KERNEL);
		if (!res)
			return;

		/*
		 * Allocate MSG TLP region of size 'region_align' at the end of
		 * the host bridge window.
		 */
		res->start = win->res->end - pci->region_align + 1;
		res->end = win->res->end;
		res->name = "msg";
		res->flags = win->res->flags | IORESOURCE_BUSY;

		if (!devm_request_resource(pci->dev, win->res, res))
			pp->msg_res = res;
	}
}

static int dw_pcie_config_ecam_iatu(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct dw_pcie_ob_atu_cfg atu = {0};
	resource_size_t bus_range_max;
	struct resource_entry *bus;
	int ret;

	bus = resource_list_first_type(&pp->bridge->windows, IORESOURCE_BUS);

	/*
	 * Root bus under the host bridge doesn't require any iATU configuration
	 * as DBI region will be used to access root bus config space.
	 * Immediate bus under Root Bus, needs type 0 iATU configuration and
	 * remaining buses need type 1 iATU configuration.
	 */
	atu.index = 0;
	atu.type = PCIE_ATU_TYPE_CFG0;
	atu.parent_bus_addr = pp->cfg0_base + SZ_1M;
	/* 1MiB is to cover 1 (bus) * 32 (devices) * 8 (functions) */
	atu.size = SZ_1M;
	atu.ctrl2 = PCIE_ATU_CFG_SHIFT_MODE_ENABLE;
	ret = dw_pcie_prog_outbound_atu(pci, &atu);
	if (ret)
		return ret;

	bus_range_max = resource_size(bus->res);

	if (bus_range_max < 2)
		return 0;

	/* Configure remaining buses in type 1 iATU configuration */
	atu.index = 1;
	atu.type = PCIE_ATU_TYPE_CFG1;
	atu.parent_bus_addr = pp->cfg0_base + SZ_2M;
	atu.size = (SZ_1M * bus_range_max) - SZ_2M;
	atu.ctrl2 = PCIE_ATU_CFG_SHIFT_MODE_ENABLE;

	return dw_pcie_prog_outbound_atu(pci, &atu);
}

static int dw_pcie_create_ecam_window(struct dw_pcie_rp *pp, struct resource *res)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct device *dev = pci->dev;
	struct resource_entry *bus;

	bus = resource_list_first_type(&pp->bridge->windows, IORESOURCE_BUS);
	if (!bus)
		return -ENODEV;

	pp->cfg = pci_ecam_create(dev, res, bus->res, &pci_generic_ecam_ops);
	if (IS_ERR(pp->cfg))
		return PTR_ERR(pp->cfg);

	return 0;
}

static bool dw_pcie_ecam_enabled(struct dw_pcie_rp *pp, struct resource *config_res)
{
	struct resource *bus_range;
	u64 nr_buses;

	/* Vendor glue drivers may implement their own ECAM mechanism */
	if (pp->native_ecam)
		return false;

	/*
	 * PCIe spec r6.0, sec 7.2.2 mandates the base address used for ECAM to
	 * be aligned on a 2^(n+20) byte boundary, where n is the number of bits
	 * used for representing 'bus' in BDF. Since the DWC cores always use 8
	 * bits for representing 'bus', the base address has to be aligned to
	 * 2^28 byte boundary, which is 256 MiB.
	 */
	if (!IS_256MB_ALIGNED(config_res->start))
		return false;

	bus_range = resource_list_first_type(&pp->bridge->windows, IORESOURCE_BUS)->res;
	if (!bus_range)
		return false;

	nr_buses = resource_size(config_res) >> PCIE_ECAM_BUS_SHIFT;

	return nr_buses >= resource_size(bus_range);
}

static int dw_pcie_host_get_resources(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct device *dev = pci->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource_entry *win;
	struct resource *res;
	int ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "config");
	if (!res) {
		dev_err(dev, "Missing \"config\" reg space\n");
		return -ENODEV;
	}

	pp->cfg0_size = resource_size(res);
	pp->cfg0_base = res->start;

	pp->ecam_enabled = dw_pcie_ecam_enabled(pp, res);
	if (pp->ecam_enabled) {
		ret = dw_pcie_create_ecam_window(pp, res);
		if (ret)
			return ret;

		pp->bridge->ops = &dw_pcie_ecam_ops;
		pp->bridge->sysdata = pp->cfg;
		pp->cfg->priv = pp;
	} else {
		pp->va_cfg0_base = devm_pci_remap_cfg_resource(dev, res);
		if (IS_ERR(pp->va_cfg0_base))
			return PTR_ERR(pp->va_cfg0_base);

		/* Set default bus ops */
		pp->bridge->ops = &dw_pcie_ops;
		pp->bridge->child_ops = &dw_child_pcie_ops;
		pp->bridge->sysdata = pp;
	}

	ret = dw_pcie_get_resources(pci);
	if (ret) {
		if (pp->cfg)
			pci_ecam_free(pp->cfg);
		return ret;
	}

	/* Get the I/O range from DT */
	win = resource_list_first_type(&pp->bridge->windows, IORESOURCE_IO);
	if (win) {
		pp->io_size = resource_size(win->res);
		pp->io_bus_addr = win->res->start - win->offset;
		pp->io_base = pci_pio_to_address(win->res->start);
	}

	/*
	 * visconti_pcie_cpu_addr_fixup() uses pp->io_base, so we have to
	 * call dw_pcie_parent_bus_offset() after setting pp->io_base.
	 */
	pci->parent_bus_offset = dw_pcie_parent_bus_offset(pci, "config",
							   pp->cfg0_base);
	return 0;
}

int dw_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct device *dev = pci->dev;
	struct device_node *np = dev->of_node;
	struct pci_host_bridge *bridge;
	int ret;

	raw_spin_lock_init(&pp->lock);

	bridge = devm_pci_alloc_host_bridge(dev, 0);
	if (!bridge)
		return -ENOMEM;

	pp->bridge = bridge;

	ret = dw_pcie_host_get_resources(pp);
	if (ret)
		return ret;

	if (pp->ops->init) {
		ret = pp->ops->init(pp);
		if (ret)
			goto err_free_ecam;
	}

	if (pci_msi_enabled()) {
		pp->use_imsi_rx = !(pp->ops->msi_init ||
				     of_property_present(np, "msi-parent") ||
				     of_property_present(np, "msi-map"));

		/*
		 * For the use_imsi_rx case the default assignment is handled
		 * in the dw_pcie_msi_host_init().
		 */
		if (!pp->use_imsi_rx && !pp->num_vectors) {
			pp->num_vectors = MSI_DEF_NUM_VECTORS;
		} else if (pp->num_vectors > MAX_MSI_IRQS) {
			dev_err(dev, "Invalid number of vectors\n");
			ret = -EINVAL;
			goto err_deinit_host;
		}

		if (pp->ops->msi_init) {
			ret = pp->ops->msi_init(pp);
			if (ret < 0)
				goto err_deinit_host;
		} else if (pp->use_imsi_rx) {
			ret = dw_pcie_msi_host_init(pp);
			if (ret < 0)
				goto err_deinit_host;
		}
	}

	dw_pcie_version_detect(pci);

	dw_pcie_iatu_detect(pci);

	if (pci->num_lanes < 1)
		pci->num_lanes = dw_pcie_link_get_max_link_width(pci);

	ret = of_pci_get_equalization_presets(dev, &pp->presets, pci->num_lanes);
	if (ret)
		goto err_free_msi;

	/*
	 * Allocate the resource for MSG TLP before programming the iATU
	 * outbound window in dw_pcie_setup_rc(). Since the allocation depends
	 * on the value of 'region_align', this has to be done after
	 * dw_pcie_iatu_detect().
	 *
	 * Glue drivers need to set 'use_atu_msg' before dw_pcie_host_init() to
	 * make use of the generic MSG TLP implementation.
	 */
	if (pp->use_atu_msg)
		dw_pcie_host_request_msg_tlp_res(pp);

	ret = dw_pcie_edma_detect(pci);
	if (ret)
		goto err_free_msi;

	ret = dw_pcie_setup_rc(pp);
	if (ret)
		goto err_remove_edma;

	if (!dw_pcie_link_up(pci)) {
		ret = dw_pcie_start_link(pci);
		if (ret)
			goto err_remove_edma;
	}

	/*
	 * Only fail on timeout error. Other errors indicate the device may
	 * become available later, so continue without failing.
	 */
	ret = dw_pcie_wait_for_link(pci);
	if (ret == -ETIMEDOUT)
		goto err_stop_link;

	ret = pci_host_probe(bridge);
	if (ret)
		goto err_stop_link;

	if (pp->ops->post_init)
		pp->ops->post_init(pp);

	dwc_pcie_debugfs_init(pci, DW_PCIE_RC_TYPE);

	return 0;

err_stop_link:
	dw_pcie_stop_link(pci);

err_remove_edma:
	dw_pcie_edma_remove(pci);

err_free_msi:
	if (pp->use_imsi_rx)
		dw_pcie_free_msi(pp);

err_deinit_host:
	if (pp->ops->deinit)
		pp->ops->deinit(pp);

err_free_ecam:
	if (pp->cfg)
		pci_ecam_free(pp->cfg);

	return ret;
}
EXPORT_SYMBOL_GPL(dw_pcie_host_init);

void dw_pcie_host_deinit(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	dwc_pcie_debugfs_deinit(pci);

	pci_stop_root_bus(pp->bridge->bus);
	pci_remove_root_bus(pp->bridge->bus);

	dw_pcie_stop_link(pci);

	dw_pcie_edma_remove(pci);

	if (pp->use_imsi_rx)
		dw_pcie_free_msi(pp);

	if (pp->ops->deinit)
		pp->ops->deinit(pp);

	if (pp->cfg)
		pci_ecam_free(pp->cfg);
}
EXPORT_SYMBOL_GPL(dw_pcie_host_deinit);

static void __iomem *dw_pcie_other_conf_map_bus(struct pci_bus *bus,
						unsigned int devfn, int where)
{
	struct dw_pcie_rp *pp = bus->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct dw_pcie_ob_atu_cfg atu = { 0 };
	int type, ret;
	u32 busdev;

	/*
	 * Checking whether the link is up here is a last line of defense
	 * against platforms that forward errors on the system bus as
	 * SError upon PCI configuration transactions issued when the link
	 * is down. This check is racy by definition and does not stop
	 * the system from triggering an SError if the link goes down
	 * after this check is performed.
	 */
	if (!dw_pcie_link_up(pci))
		return NULL;

	busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) |
		 PCIE_ATU_FUNC(PCI_FUNC(devfn));

	if (pci_is_root_bus(bus->parent))
		type = PCIE_ATU_TYPE_CFG0;
	else
		type = PCIE_ATU_TYPE_CFG1;

	atu.type = type;
	atu.parent_bus_addr = pp->cfg0_base - pci->parent_bus_offset;
	atu.pci_addr = busdev;
	atu.size = pp->cfg0_size;

	ret = dw_pcie_prog_outbound_atu(pci, &atu);
	if (ret)
		return NULL;

	return pp->va_cfg0_base + where;
}

static int dw_pcie_rd_other_conf(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 *val)
{
	struct dw_pcie_rp *pp = bus->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct dw_pcie_ob_atu_cfg atu = { 0 };
	int ret;

	ret = pci_generic_config_read(bus, devfn, where, size, val);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	if (pp->cfg0_io_shared) {
		atu.type = PCIE_ATU_TYPE_IO;
		atu.parent_bus_addr = pp->io_base - pci->parent_bus_offset;
		atu.pci_addr = pp->io_bus_addr;
		atu.size = pp->io_size;

		ret = dw_pcie_prog_outbound_atu(pci, &atu);
		if (ret)
			return PCIBIOS_SET_FAILED;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int dw_pcie_wr_other_conf(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 val)
{
	struct dw_pcie_rp *pp = bus->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct dw_pcie_ob_atu_cfg atu = { 0 };
	int ret;

	ret = pci_generic_config_write(bus, devfn, where, size, val);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	if (pp->cfg0_io_shared) {
		atu.type = PCIE_ATU_TYPE_IO;
		atu.parent_bus_addr = pp->io_base - pci->parent_bus_offset;
		atu.pci_addr = pp->io_bus_addr;
		atu.size = pp->io_size;

		ret = dw_pcie_prog_outbound_atu(pci, &atu);
		if (ret)
			return PCIBIOS_SET_FAILED;
	}

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops dw_child_pcie_ops = {
	.map_bus = dw_pcie_other_conf_map_bus,
	.read = dw_pcie_rd_other_conf,
	.write = dw_pcie_wr_other_conf,
};

void __iomem *dw_pcie_own_conf_map_bus(struct pci_bus *bus, unsigned int devfn, int where)
{
	struct dw_pcie_rp *pp = bus->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	if (PCI_SLOT(devfn) > 0)
		return NULL;

	return pci->dbi_base + where;
}
EXPORT_SYMBOL_GPL(dw_pcie_own_conf_map_bus);

static void __iomem *dw_pcie_ecam_conf_map_bus(struct pci_bus *bus, unsigned int devfn, int where)
{
	struct pci_config_window *cfg = bus->sysdata;
	struct dw_pcie_rp *pp = cfg->priv;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	unsigned int busn = bus->number;

	if (busn > 0)
		return pci_ecam_map_bus(bus, devfn, where);

	if (PCI_SLOT(devfn) > 0)
		return NULL;

	return pci->dbi_base + where;
}

static struct pci_ops dw_pcie_ops = {
	.map_bus = dw_pcie_own_conf_map_bus,
	.read = pci_generic_config_read,
	.write = pci_generic_config_write,
};

static struct pci_ops dw_pcie_ecam_ops = {
	.map_bus = dw_pcie_ecam_conf_map_bus,
	.read = pci_generic_config_read,
	.write = pci_generic_config_write,
};

static int dw_pcie_iatu_setup(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct dw_pcie_ob_atu_cfg atu = { 0 };
	struct resource_entry *entry;
	int ob_iatu_index;
	int ib_iatu_index;
	int i, ret;

	if (!pci->num_ob_windows) {
		dev_err(pci->dev, "No outbound iATU found\n");
		return -EINVAL;
	}

	/*
	 * Ensure all out/inbound windows are disabled before proceeding with
	 * the MEM/IO (dma-)ranges setups.
	 */
	for (i = 0; i < pci->num_ob_windows; i++)
		dw_pcie_disable_atu(pci, PCIE_ATU_REGION_DIR_OB, i);

	for (i = 0; i < pci->num_ib_windows; i++)
		dw_pcie_disable_atu(pci, PCIE_ATU_REGION_DIR_IB, i);

	/*
	 * NOTE: For outbound address translation, outbound iATU at index 0 is
	 * reserved for CFG IOs (dw_pcie_other_conf_map_bus()), thus start at
	 * index 1.
	 *
	 * If using ECAM, outbound iATU at index 0 and index 1 is reserved for
	 * CFG IOs.
	 */
	if (pp->ecam_enabled) {
		ob_iatu_index = 2;
		ret = dw_pcie_config_ecam_iatu(pp);
		if (ret) {
			dev_err(pci->dev, "Failed to configure iATU in ECAM mode\n");
			return ret;
		}
	} else {
		ob_iatu_index = 1;
	}

	resource_list_for_each_entry(entry, &pp->bridge->windows) {
		resource_size_t res_size;

		if (resource_type(entry->res) != IORESOURCE_MEM)
			continue;

		atu.type = PCIE_ATU_TYPE_MEM;
		atu.parent_bus_addr = entry->res->start - pci->parent_bus_offset;
		atu.pci_addr = entry->res->start - entry->offset;

		/* Adjust iATU size if MSG TLP region was allocated before */
		if (pp->msg_res && pp->msg_res->parent == entry->res)
			res_size = resource_size(entry->res) -
					resource_size(pp->msg_res);
		else
			res_size = resource_size(entry->res);

		while (res_size > 0) {
			/*
			 * Return failure if we run out of windows in the
			 * middle. Otherwise, we would end up only partially
			 * mapping a single resource.
			 */
			if (ob_iatu_index >= pci->num_ob_windows) {
				dev_err(pci->dev, "Cannot add outbound window for region: %pr\n",
					entry->res);
				return -ENOMEM;
			}

			atu.index = ob_iatu_index;
			atu.size = MIN(pci->region_limit + 1, res_size);

			ret = dw_pcie_prog_outbound_atu(pci, &atu);
			if (ret) {
				dev_err(pci->dev, "Failed to set MEM range %pr\n",
					entry->res);
				return ret;
			}

			ob_iatu_index++;
			atu.parent_bus_addr += atu.size;
			atu.pci_addr += atu.size;
			res_size -= atu.size;
		}
	}

	if (pp->io_size) {
		if (ob_iatu_index < pci->num_ob_windows) {
			atu.index = ob_iatu_index;
			atu.type = PCIE_ATU_TYPE_IO;
			atu.parent_bus_addr = pp->io_base - pci->parent_bus_offset;
			atu.pci_addr = pp->io_bus_addr;
			atu.size = pp->io_size;

			ret = dw_pcie_prog_outbound_atu(pci, &atu);
			if (ret) {
				dev_err(pci->dev, "Failed to set IO range %pr\n",
					entry->res);
				return ret;
			}
			ob_iatu_index++;
		} else {
			/*
			 * If there are not enough outbound windows to give I/O
			 * space its own iATU, the outbound iATU at index 0 will
			 * be shared between I/O space and CFG IOs, by
			 * temporarily reconfiguring the iATU to CFG space, in
			 * order to do a CFG IO, and then immediately restoring
			 * it to I/O space. This is only implemented when using
			 * dw_pcie_other_conf_map_bus(), which is not the case
			 * when using ECAM.
			 */
			if (pp->ecam_enabled) {
				dev_err(pci->dev, "Cannot add outbound window for I/O\n");
				return -ENOMEM;
			}
			pp->cfg0_io_shared = true;
		}
	}

	if (pp->use_atu_msg) {
		if (ob_iatu_index >= pci->num_ob_windows) {
			dev_err(pci->dev, "Cannot add outbound window for MSG TLP\n");
			return -ENOMEM;
		}
		pp->msg_atu_index = ob_iatu_index++;
	}

	ib_iatu_index = 0;
	resource_list_for_each_entry(entry, &pp->bridge->dma_ranges) {
		resource_size_t res_start, res_size, window_size;

		if (resource_type(entry->res) != IORESOURCE_MEM)
			continue;

		res_size = resource_size(entry->res);
		res_start = entry->res->start;
		while (res_size > 0) {
			/*
			 * Return failure if we run out of windows in the
			 * middle. Otherwise, we would end up only partially
			 * mapping a single resource.
			 */
			if (ib_iatu_index >= pci->num_ib_windows) {
				dev_err(pci->dev, "Cannot add inbound window for region: %pr\n",
					entry->res);
				return -ENOMEM;
			}

			window_size = MIN(pci->region_limit + 1, res_size);
			ret = dw_pcie_prog_inbound_atu(pci, ib_iatu_index,
						       PCIE_ATU_TYPE_MEM, res_start,
						       res_start - entry->offset, window_size);
			if (ret) {
				dev_err(pci->dev, "Failed to set DMA range %pr\n",
					entry->res);
				return ret;
			}

			ib_iatu_index++;
			res_start += window_size;
			res_size -= window_size;
		}
	}

	return 0;
}

static void dw_pcie_program_presets(struct dw_pcie_rp *pp, enum pci_bus_speed speed)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	u8 lane_eq_offset, lane_reg_size, cap_id;
	u8 *presets;
	u32 cap;
	int i;

	if (speed == PCIE_SPEED_8_0GT) {
		presets = (u8 *)pp->presets.eq_presets_8gts;
		lane_eq_offset =  PCI_SECPCI_LE_CTRL;
		cap_id = PCI_EXT_CAP_ID_SECPCI;
		/* For data rate of 8 GT/S each lane equalization control is 16bits wide*/
		lane_reg_size = 0x2;
	} else if (speed == PCIE_SPEED_16_0GT) {
		presets = pp->presets.eq_presets_Ngts[EQ_PRESET_TYPE_16GTS - 1];
		lane_eq_offset = PCI_PL_16GT_LE_CTRL;
		cap_id = PCI_EXT_CAP_ID_PL_16GT;
		lane_reg_size = 0x1;
	} else if (speed == PCIE_SPEED_32_0GT) {
		presets =  pp->presets.eq_presets_Ngts[EQ_PRESET_TYPE_32GTS - 1];
		lane_eq_offset = PCI_PL_32GT_LE_CTRL;
		cap_id = PCI_EXT_CAP_ID_PL_32GT;
		lane_reg_size = 0x1;
	} else if (speed == PCIE_SPEED_64_0GT) {
		presets =  pp->presets.eq_presets_Ngts[EQ_PRESET_TYPE_64GTS - 1];
		lane_eq_offset = PCI_PL_64GT_LE_CTRL;
		cap_id = PCI_EXT_CAP_ID_PL_64GT;
		lane_reg_size = 0x1;
	} else {
		return;
	}

	if (presets[0] == PCI_EQ_RESV)
		return;

	cap = dw_pcie_find_ext_capability(pci, cap_id);
	if (!cap)
		return;

	/*
	 * Write preset values to the registers byte-by-byte for the given
	 * number of lanes and register size.
	 */
	for (i = 0; i < pci->num_lanes * lane_reg_size; i++)
		dw_pcie_writeb_dbi(pci, cap + lane_eq_offset + i, presets[i]);
}

static void dw_pcie_config_presets(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	enum pci_bus_speed speed = pcie_link_speed[pci->max_link_speed];

	/*
	 * Lane equalization settings need to be applied for all data rates the
	 * controller supports and for all supported lanes.
	 */

	if (speed >= PCIE_SPEED_8_0GT)
		dw_pcie_program_presets(pp, PCIE_SPEED_8_0GT);

	if (speed >= PCIE_SPEED_16_0GT)
		dw_pcie_program_presets(pp, PCIE_SPEED_16_0GT);

	if (speed >= PCIE_SPEED_32_0GT)
		dw_pcie_program_presets(pp, PCIE_SPEED_32_0GT);

	if (speed >= PCIE_SPEED_64_0GT)
		dw_pcie_program_presets(pp, PCIE_SPEED_64_0GT);
}

int dw_pcie_setup_rc(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	u32 val;
	int ret;

	/*
	 * Enable DBI read-only registers for writing/updating configuration.
	 * Write permission gets disabled towards the end of this function.
	 */
	dw_pcie_dbi_ro_wr_en(pci);

	dw_pcie_setup(pci);

	dw_pcie_msi_init(pp);

	/* Setup RC BARs */
	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_0, 0x00000004);
	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_1, 0x00000000);

	/* Setup interrupt pins */
	val = dw_pcie_readl_dbi(pci, PCI_INTERRUPT_LINE);
	val &= 0xffff00ff;
	val |= 0x00000100;
	dw_pcie_writel_dbi(pci, PCI_INTERRUPT_LINE, val);

	/* Setup bus numbers */
	val = dw_pcie_readl_dbi(pci, PCI_PRIMARY_BUS);
	val &= 0xff000000;
	val |= 0x00ff0100;
	dw_pcie_writel_dbi(pci, PCI_PRIMARY_BUS, val);

	/* Setup command register */
	val = dw_pcie_readl_dbi(pci, PCI_COMMAND);
	val &= 0xffff0000;
	val |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
		PCI_COMMAND_MASTER | PCI_COMMAND_SERR;
	dw_pcie_writel_dbi(pci, PCI_COMMAND, val);

	dw_pcie_hide_unsupported_l1ss(pci);

	dw_pcie_config_presets(pp);
	/*
	 * If the platform provides its own child bus config accesses, it means
	 * the platform uses its own address translation component rather than
	 * ATU, so we should not program the ATU here.
	 */
	if (pp->bridge->child_ops == &dw_child_pcie_ops || pp->ecam_enabled) {
		ret = dw_pcie_iatu_setup(pp);
		if (ret)
			return ret;
	}

	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_0, 0);

	/* Program correct class for RC */
	dw_pcie_writew_dbi(pci, PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_PCI);

	val = dw_pcie_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
	val |= PORT_LOGIC_SPEED_CHANGE;
	dw_pcie_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, val);

	dw_pcie_dbi_ro_wr_dis(pci);

	/*
	 * The iMSI-RX module does not support receiving MSI or MSI-X generated
	 * by the Root Port. If iMSI-RX is used as the MSI controller, remove
	 * the MSI and MSI-X capabilities of the Root Port to allow the drivers
	 * to fall back to INTx instead.
	 */
	if (pp->use_imsi_rx) {
		dw_pcie_remove_capability(pci, PCI_CAP_ID_MSI);
		dw_pcie_remove_capability(pci, PCI_CAP_ID_MSIX);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dw_pcie_setup_rc);

static int dw_pcie_pme_turn_off(struct dw_pcie *pci)
{
	struct dw_pcie_ob_atu_cfg atu = { 0 };
	void __iomem *mem;
	int ret;

	if (pci->num_ob_windows <= pci->pp.msg_atu_index)
		return -ENOSPC;

	if (!pci->pp.msg_res)
		return -ENOSPC;

	atu.code = PCIE_MSG_CODE_PME_TURN_OFF;
	atu.routing = PCIE_MSG_TYPE_R_BC;
	atu.type = PCIE_ATU_TYPE_MSG;
	atu.size = resource_size(pci->pp.msg_res);
	atu.index = pci->pp.msg_atu_index;

	atu.parent_bus_addr = pci->pp.msg_res->start - pci->parent_bus_offset;

	ret = dw_pcie_prog_outbound_atu(pci, &atu);
	if (ret)
		return ret;

	mem = ioremap(pci->pp.msg_res->start, pci->region_align);
	if (!mem)
		return -ENOMEM;

	/* A dummy write is converted to a Msg TLP */
	writel(0, mem);

	iounmap(mem);

	return 0;
}

int dw_pcie_suspend_noirq(struct dw_pcie *pci)
{
	u8 offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	int ret = 0;
	u32 val;

	if (!dw_pcie_link_up(pci))
		goto stop_link;

	/*
	 * If L1SS is supported, then do not put the link into L2 as some
	 * devices such as NVMe expect low resume latency.
	 */
	if (dw_pcie_readw_dbi(pci, offset + PCI_EXP_LNKCTL) & PCI_EXP_LNKCTL_ASPM_L1)
		return 0;

	if (pci->pp.ops->pme_turn_off) {
		pci->pp.ops->pme_turn_off(&pci->pp);
	} else {
		ret = dw_pcie_pme_turn_off(pci);
		if (ret)
			return ret;
	}

	/*
	 * Some SoCs do not support reading the LTSSM register after
	 * PME_Turn_Off broadcast. For those SoCs, skip waiting for L2/L3 Ready
	 * state and wait 10ms as recommended in PCIe spec r6.0, sec 5.3.3.2.1.
	 */
	if (pci->pp.skip_l23_ready) {
		mdelay(PCIE_PME_TO_L2_TIMEOUT_US/1000);
		goto stop_link;
	}

	ret = read_poll_timeout(dw_pcie_get_ltssm, val,
				val == DW_PCIE_LTSSM_L2_IDLE ||
				val <= DW_PCIE_LTSSM_DETECT_WAIT,
				PCIE_PME_TO_L2_TIMEOUT_US/10,
				PCIE_PME_TO_L2_TIMEOUT_US, false, pci);
	if (ret) {
		/* Only log message when LTSSM isn't in DETECT or POLL */
		dev_err(pci->dev, "Timeout waiting for L2 entry! LTSSM: 0x%x\n", val);
		return ret;
	}

	/*
	 * Per PCIe r6.0, sec 5.3.3.2.1, software should wait at least
	 * 100ns after L2/L3 Ready before turning off refclock and
	 * main power. This is harmless when no endpoint is connected.
	 */
	udelay(1);

stop_link:
	dw_pcie_stop_link(pci);
	if (pci->pp.ops->deinit)
		pci->pp.ops->deinit(&pci->pp);

	pci->suspended = true;

	return ret;
}
EXPORT_SYMBOL_GPL(dw_pcie_suspend_noirq);

int dw_pcie_resume_noirq(struct dw_pcie *pci)
{
	int ret;

	if (!pci->suspended)
		return 0;

	pci->suspended = false;

	if (pci->pp.ops->init) {
		ret = pci->pp.ops->init(&pci->pp);
		if (ret) {
			dev_err(pci->dev, "Host init failed: %d\n", ret);
			return ret;
		}
	}

	dw_pcie_setup_rc(&pci->pp);

	ret = dw_pcie_start_link(pci);
	if (ret)
		return ret;

	ret = dw_pcie_wait_for_link(pci);
	if (ret)
		return ret;

	if (pci->pp.ops->post_init)
		pci->pp.ops->post_init(&pci->pp);

	return ret;
}
EXPORT_SYMBOL_GPL(dw_pcie_resume_noirq);
