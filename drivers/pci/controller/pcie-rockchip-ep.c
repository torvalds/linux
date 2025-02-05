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
#include <linux/gpio/consumer.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/pci-epc.h>
#include <linux/platform_device.h>
#include <linux/pci-epf.h>
#include <linux/sizes.h>
#include <linux/workqueue.h>

#include "pcie-rockchip.h"

/**
 * struct rockchip_pcie_ep - private data for PCIe endpoint controller driver
 * @rockchip: Rockchip PCIe controller
 * @epc: PCI EPC device
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
 * @perst_irq: IRQ used for the PERST# signal.
 * @perst_asserted: True if the PERST# signal was asserted.
 * @link_up: True if the PCI link is up.
 * @link_training: Work item to execute PCI link training.
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
	int			perst_irq;
	bool			perst_asserted;
	bool			link_up;
	struct delayed_work	link_training;
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

static int rockchip_pcie_ep_ob_atu_num_bits(struct rockchip_pcie *rockchip,
					    u64 pci_addr, size_t size)
{
	int num_pass_bits = fls64(pci_addr ^ (pci_addr + size - 1));

	return clamp(num_pass_bits,
		     ROCKCHIP_PCIE_AT_MIN_NUM_BITS,
		     ROCKCHIP_PCIE_AT_MAX_NUM_BITS);
}

static void rockchip_pcie_prog_ep_ob_atu(struct rockchip_pcie *rockchip, u8 fn,
					 u32 r, u64 cpu_addr, u64 pci_addr,
					 size_t size)
{
	int num_pass_bits;
	u32 addr0, addr1, desc0;

	num_pass_bits = rockchip_pcie_ep_ob_atu_num_bits(rockchip,
							 pci_addr, size);

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
		rockchip_pcie_write(rockchip,
				    hdr->vendorid | hdr->subsys_vendor_id << 16,
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
		bool is_64bits = !!(flags & PCI_BASE_ADDRESS_MEM_TYPE_64);

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

static u64 rockchip_pcie_ep_align_addr(struct pci_epc *epc, u64 pci_addr,
				       size_t *pci_size, size_t *addr_offset)
{
	struct rockchip_pcie_ep *ep = epc_get_drvdata(epc);
	size_t size = *pci_size;
	u64 offset, mask;
	int num_bits;

	num_bits = rockchip_pcie_ep_ob_atu_num_bits(&ep->rockchip,
						    pci_addr, size);
	mask = (1ULL << num_bits) - 1;

	offset = pci_addr & mask;
	if (size + offset > SZ_1M)
		size = SZ_1M - offset;

	*pci_size = ALIGN(offset + size, ROCKCHIP_PCIE_AT_SIZE_ALIGN);
	*addr_offset = offset;

	return pci_addr & ~mask;
}

static int rockchip_pcie_ep_map_addr(struct pci_epc *epc, u8 fn, u8 vfn,
				     phys_addr_t addr, u64 pci_addr,
				     size_t size)
{
	struct rockchip_pcie_ep *ep = epc_get_drvdata(epc);
	struct rockchip_pcie *pcie = &ep->rockchip;
	u32 r = rockchip_ob_region(addr);

	if (test_bit(r, &ep->ob_region_map))
		return -EBUSY;

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
	u32 r = rockchip_ob_region(addr);

	if (addr != ep->ob_addr[r] || !test_bit(r, &ep->ob_region_map))
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

static int rockchip_pcie_ep_send_intx_irq(struct rockchip_pcie_ep *ep, u8 fn,
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
	size_t irq_pci_size, offset;
	u64 irq_pci_addr;
	u8 msi_count;
	u64 pci_addr;

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
	irq_pci_size = ~PCIE_ADDR_MASK + 1;
	irq_pci_addr = rockchip_pcie_ep_align_addr(ep->epc,
						   pci_addr & PCIE_ADDR_MASK,
						   &irq_pci_size, &offset);
	if (unlikely(ep->irq_pci_addr != irq_pci_addr ||
		     ep->irq_pci_fn != fn)) {
		rockchip_pcie_prog_ep_ob_atu(rockchip, fn,
					rockchip_ob_region(ep->irq_phys_addr),
					ep->irq_phys_addr,
					irq_pci_addr, irq_pci_size);
		ep->irq_pci_addr = irq_pci_addr;
		ep->irq_pci_fn = fn;
	}

	writew(data, ep->irq_cpu_addr + offset + (pci_addr & ~PCIE_ADDR_MASK));
	return 0;
}

static int rockchip_pcie_ep_raise_irq(struct pci_epc *epc, u8 fn, u8 vfn,
				      unsigned int type, u16 interrupt_num)
{
	struct rockchip_pcie_ep *ep = epc_get_drvdata(epc);

	switch (type) {
	case PCI_IRQ_INTX:
		return rockchip_pcie_ep_send_intx_irq(ep, fn, 0);
	case PCI_IRQ_MSI:
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

	if (rockchip->perst_gpio)
		enable_irq(ep->perst_irq);

	/* Enable configuration and start link training */
	rockchip_pcie_write(rockchip,
			    PCIE_CLIENT_LINK_TRAIN_ENABLE |
			    PCIE_CLIENT_CONF_ENABLE,
			    PCIE_CLIENT_CONFIG);

	if (!rockchip->perst_gpio)
		schedule_delayed_work(&ep->link_training, 0);

	return 0;
}

static void rockchip_pcie_ep_stop(struct pci_epc *epc)
{
	struct rockchip_pcie_ep *ep = epc_get_drvdata(epc);
	struct rockchip_pcie *rockchip = &ep->rockchip;

	if (rockchip->perst_gpio) {
		ep->perst_asserted = true;
		disable_irq(ep->perst_irq);
	}

	cancel_delayed_work_sync(&ep->link_training);

	/* Stop link training and disable configuration */
	rockchip_pcie_write(rockchip,
			    PCIE_CLIENT_CONF_DISABLE |
			    PCIE_CLIENT_LINK_TRAIN_DISABLE,
			    PCIE_CLIENT_CONFIG);
}

static void rockchip_pcie_ep_retrain_link(struct rockchip_pcie *rockchip)
{
	u32 status;

	status = rockchip_pcie_read(rockchip, PCIE_EP_CONFIG_LCS);
	status |= PCI_EXP_LNKCTL_RL;
	rockchip_pcie_write(rockchip, status, PCIE_EP_CONFIG_LCS);
}

static bool rockchip_pcie_ep_link_up(struct rockchip_pcie *rockchip)
{
	u32 val = rockchip_pcie_read(rockchip, PCIE_CLIENT_BASIC_STATUS1);

	return PCIE_LINK_UP(val);
}

static void rockchip_pcie_ep_link_training(struct work_struct *work)
{
	struct rockchip_pcie_ep *ep =
		container_of(work, struct rockchip_pcie_ep, link_training.work);
	struct rockchip_pcie *rockchip = &ep->rockchip;
	struct device *dev = rockchip->dev;
	u32 val;
	int ret;

	/* Enable Gen1 training and wait for its completion */
	ret = readl_poll_timeout(rockchip->apb_base + PCIE_CORE_CTRL,
				 val, PCIE_LINK_TRAINING_DONE(val), 50,
				 LINK_TRAIN_TIMEOUT);
	if (ret)
		goto again;

	/* Make sure that the link is up */
	ret = readl_poll_timeout(rockchip->apb_base + PCIE_CLIENT_BASIC_STATUS1,
				 val, PCIE_LINK_UP(val), 50,
				 LINK_TRAIN_TIMEOUT);
	if (ret)
		goto again;

	/*
	 * Check the current speed: if gen2 speed was requested and we are not
	 * at gen2 speed yet, retrain again for gen2.
	 */
	val = rockchip_pcie_read(rockchip, PCIE_CORE_CTRL);
	if (!PCIE_LINK_IS_GEN2(val) && rockchip->link_gen == 2) {
		/* Enable retrain for gen2 */
		rockchip_pcie_ep_retrain_link(rockchip);
		readl_poll_timeout(rockchip->apb_base + PCIE_CORE_CTRL,
				   val, PCIE_LINK_IS_GEN2(val), 50,
				   LINK_TRAIN_TIMEOUT);
	}

	/* Check again that the link is up */
	if (!rockchip_pcie_ep_link_up(rockchip))
		goto again;

	/*
	 * If PERST# was asserted while polling the link, do not notify
	 * the function.
	 */
	if (ep->perst_asserted)
		return;

	val = rockchip_pcie_read(rockchip, PCIE_CLIENT_BASIC_STATUS0);
	dev_info(dev,
		 "link up (negotiated speed: %sGT/s, width: x%lu)\n",
		 (val & PCIE_CLIENT_NEG_LINK_SPEED) ? "5" : "2.5",
		 ((val & PCIE_CLIENT_NEG_LINK_WIDTH_MASK) >>
		  PCIE_CLIENT_NEG_LINK_WIDTH_SHIFT) << 1);

	/* Notify the function */
	pci_epc_linkup(ep->epc);
	ep->link_up = true;

	return;

again:
	schedule_delayed_work(&ep->link_training, msecs_to_jiffies(5));
}

static void rockchip_pcie_ep_perst_assert(struct rockchip_pcie_ep *ep)
{
	struct rockchip_pcie *rockchip = &ep->rockchip;

	dev_dbg(rockchip->dev, "PERST# asserted, link down\n");

	if (ep->perst_asserted)
		return;

	ep->perst_asserted = true;

	cancel_delayed_work_sync(&ep->link_training);

	if (ep->link_up) {
		pci_epc_linkdown(ep->epc);
		ep->link_up = false;
	}
}

static void rockchip_pcie_ep_perst_deassert(struct rockchip_pcie_ep *ep)
{
	struct rockchip_pcie *rockchip = &ep->rockchip;

	dev_dbg(rockchip->dev, "PERST# de-asserted, starting link training\n");

	if (!ep->perst_asserted)
		return;

	ep->perst_asserted = false;

	/* Enable link re-training */
	rockchip_pcie_ep_retrain_link(rockchip);

	/* Start link training */
	schedule_delayed_work(&ep->link_training, 0);
}

static irqreturn_t rockchip_pcie_ep_perst_irq_thread(int irq, void *data)
{
	struct pci_epc *epc = data;
	struct rockchip_pcie_ep *ep = epc_get_drvdata(epc);
	struct rockchip_pcie *rockchip = &ep->rockchip;
	u32 perst = gpiod_get_value(rockchip->perst_gpio);

	if (perst)
		rockchip_pcie_ep_perst_assert(ep);
	else
		rockchip_pcie_ep_perst_deassert(ep);

	irq_set_irq_type(ep->perst_irq,
			 (perst ? IRQF_TRIGGER_HIGH : IRQF_TRIGGER_LOW));

	return IRQ_HANDLED;
}

static int rockchip_pcie_ep_setup_irq(struct pci_epc *epc)
{
	struct rockchip_pcie_ep *ep = epc_get_drvdata(epc);
	struct rockchip_pcie *rockchip = &ep->rockchip;
	struct device *dev = rockchip->dev;
	int ret;

	if (!rockchip->perst_gpio)
		return 0;

	/* PCIe reset interrupt */
	ep->perst_irq = gpiod_to_irq(rockchip->perst_gpio);
	if (ep->perst_irq < 0) {
		dev_err(dev,
			"failed to get IRQ for PERST# GPIO: %d\n",
			ep->perst_irq);

		return ep->perst_irq;
	}

	/*
	 * The perst_gpio is active low, so when it is inactive on start, it
	 * is high and will trigger the perst_irq handler. So treat this initial
	 * IRQ as a dummy one by faking the host asserting PERST#.
	 */
	ep->perst_asserted = true;
	irq_set_status_flags(ep->perst_irq, IRQ_NOAUTOEN);
	ret = devm_request_threaded_irq(dev, ep->perst_irq, NULL,
					rockchip_pcie_ep_perst_irq_thread,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					"pcie-ep-perst", epc);
	if (ret) {
		dev_err(dev,
			"failed to request IRQ for PERST# GPIO: %d\n",
			ret);

		return ret;
	}

	return 0;
}

static const struct pci_epc_features rockchip_pcie_epc_features = {
	.linkup_notifier = true,
	.msi_capable = true,
	.msix_capable = false,
	.align = ROCKCHIP_PCIE_AT_SIZE_ALIGN,
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
	.align_addr	= rockchip_pcie_ep_align_addr,
	.map_addr	= rockchip_pcie_ep_map_addr,
	.unmap_addr	= rockchip_pcie_ep_unmap_addr,
	.set_msi	= rockchip_pcie_ep_set_msi,
	.get_msi	= rockchip_pcie_ep_get_msi,
	.raise_irq	= rockchip_pcie_ep_raise_irq,
	.start		= rockchip_pcie_ep_start,
	.stop		= rockchip_pcie_ep_stop,
	.get_features	= rockchip_pcie_ep_get_features,
};

static int rockchip_pcie_ep_get_resources(struct rockchip_pcie *rockchip,
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

static int rockchip_pcie_ep_init_ob_mem(struct rockchip_pcie_ep *ep)
{
	struct rockchip_pcie *rockchip = &ep->rockchip;
	struct device *dev = rockchip->dev;
	struct pci_epc_mem_window *windows = NULL;
	int err, i;

	ep->ob_addr = devm_kcalloc(dev, ep->max_regions, sizeof(*ep->ob_addr),
				   GFP_KERNEL);

	if (!ep->ob_addr)
		return -ENOMEM;

	windows = devm_kcalloc(dev, ep->max_regions,
			       sizeof(struct pci_epc_mem_window), GFP_KERNEL);
	if (!windows)
		return -ENOMEM;

	for (i = 0; i < ep->max_regions; i++) {
		windows[i].phys_base = rockchip->mem_res->start + (SZ_1M * i);
		windows[i].size = SZ_1M;
		windows[i].page_size = SZ_1M;
	}
	err = pci_epc_multi_mem_init(ep->epc, windows, ep->max_regions);
	devm_kfree(dev, windows);

	if (err < 0) {
		dev_err(dev, "failed to initialize the memory space\n");
		return err;
	}

	ep->irq_cpu_addr = pci_epc_mem_alloc_addr(ep->epc, &ep->irq_phys_addr,
						  SZ_1M);
	if (!ep->irq_cpu_addr) {
		dev_err(dev, "failed to reserve memory space for MSI\n");
		err = -ENOMEM;
		goto err_epc_mem_exit;
	}

	ep->irq_pci_addr = ROCKCHIP_PCIE_EP_DUMMY_IRQ_ADDR;

	return 0;

err_epc_mem_exit:
	pci_epc_mem_exit(ep->epc);

	return err;
}

static void rockchip_pcie_ep_exit_ob_mem(struct rockchip_pcie_ep *ep)
{
	pci_epc_mem_exit(ep->epc);
}

static void rockchip_pcie_ep_hide_broken_msix_cap(struct rockchip_pcie *rockchip)
{
	u32 cfg_msi, cfg_msix_cp;

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
}

static int rockchip_pcie_ep_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_pcie_ep *ep;
	struct rockchip_pcie *rockchip;
	struct pci_epc *epc;
	int err;

	ep = devm_kzalloc(dev, sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return -ENOMEM;

	rockchip = &ep->rockchip;
	rockchip->is_rc = false;
	rockchip->dev = dev;
	INIT_DELAYED_WORK(&ep->link_training, rockchip_pcie_ep_link_training);

	epc = devm_pci_epc_create(dev, &rockchip_pcie_epc_ops);
	if (IS_ERR(epc)) {
		dev_err(dev, "failed to create EPC device\n");
		return PTR_ERR(epc);
	}

	ep->epc = epc;
	epc_set_drvdata(epc, ep);

	err = rockchip_pcie_ep_get_resources(rockchip, ep);
	if (err)
		return err;

	err = rockchip_pcie_ep_init_ob_mem(ep);
	if (err)
		return err;

	err = rockchip_pcie_enable_clocks(rockchip);
	if (err)
		goto err_exit_ob_mem;

	err = rockchip_pcie_init_port(rockchip);
	if (err)
		goto err_disable_clocks;

	rockchip_pcie_ep_hide_broken_msix_cap(rockchip);

	/* Only enable function 0 by default */
	rockchip_pcie_write(rockchip, BIT(0), PCIE_CORE_PHY_FUNC_CFG);

	pci_epc_init_notify(epc);

	err = rockchip_pcie_ep_setup_irq(epc);
	if (err < 0)
		goto err_uninit_port;

	return 0;
err_uninit_port:
	rockchip_pcie_deinit_phys(rockchip);
err_disable_clocks:
	rockchip_pcie_disable_clocks(rockchip);
err_exit_ob_mem:
	rockchip_pcie_ep_exit_ob_mem(ep);
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
