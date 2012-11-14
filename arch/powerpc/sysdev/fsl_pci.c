/*
 * MPC83xx/85xx/86xx PCI/PCIE support routing.
 *
 * Copyright 2007-2012 Freescale Semiconductor, Inc.
 * Copyright 2008-2009 MontaVista Software, Inc.
 *
 * Initial author: Xianghua Xiao <x.xiao@freescale.com>
 * Recode: ZHANG WEI <wei.zhang@freescale.com>
 * Rewrite the routing for Frescale PCI and PCI Express
 * 	Roy Zang <tie-fei.zang@freescale.com>
 * MPC83xx PCI-Express support:
 * 	Tony Li <tony.li@freescale.com>
 * 	Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/memblock.h>
#include <linux/log2.h>
#include <linux/slab.h>

#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>

static int fsl_pcie_bus_fixup, is_mpc83xx_pci;

static void __devinit quirk_fsl_pcie_header(struct pci_dev *dev)
{
	u8 hdr_type;

	/* if we aren't a PCIe don't bother */
	if (!pci_find_capability(dev, PCI_CAP_ID_EXP))
		return;

	/* if we aren't in host mode don't bother */
	pci_read_config_byte(dev, PCI_HEADER_TYPE, &hdr_type);
	if ((hdr_type & 0x7f) != PCI_HEADER_TYPE_BRIDGE)
		return;

	dev->class = PCI_CLASS_BRIDGE_PCI << 8;
	fsl_pcie_bus_fixup = 1;
	return;
}

static int __init fsl_pcie_check_link(struct pci_controller *hose)
{
	u32 val;

	early_read_config_dword(hose, 0, 0, PCIE_LTSSM, &val);
	if (val < PCIE_LTSSM_L0)
		return 1;
	return 0;
}

#if defined(CONFIG_FSL_SOC_BOOKE) || defined(CONFIG_PPC_86xx)

#define MAX_PHYS_ADDR_BITS	40
static u64 pci64_dma_offset = 1ull << MAX_PHYS_ADDR_BITS;

static int fsl_pci_dma_set_mask(struct device *dev, u64 dma_mask)
{
	if (!dev->dma_mask || !dma_supported(dev, dma_mask))
		return -EIO;

	/*
	 * Fixup PCI devices that are able to DMA to above the physical
	 * address width of the SoC such that we can address any internal
	 * SoC address from across PCI if needed
	 */
	if ((dev->bus == &pci_bus_type) &&
	    dma_mask >= DMA_BIT_MASK(MAX_PHYS_ADDR_BITS)) {
		set_dma_ops(dev, &dma_direct_ops);
		set_dma_offset(dev, pci64_dma_offset);
	}

	*dev->dma_mask = dma_mask;
	return 0;
}

static int __init setup_one_atmu(struct ccsr_pci __iomem *pci,
	unsigned int index, const struct resource *res,
	resource_size_t offset)
{
	resource_size_t pci_addr = res->start - offset;
	resource_size_t phys_addr = res->start;
	resource_size_t size = resource_size(res);
	u32 flags = 0x80044000; /* enable & mem R/W */
	unsigned int i;

	pr_debug("PCI MEM resource start 0x%016llx, size 0x%016llx.\n",
		(u64)res->start, (u64)size);

	if (res->flags & IORESOURCE_PREFETCH)
		flags |= 0x10000000; /* enable relaxed ordering */

	for (i = 0; size > 0; i++) {
		unsigned int bits = min(__ilog2(size),
					__ffs(pci_addr | phys_addr));

		if (index + i >= 5)
			return -1;

		out_be32(&pci->pow[index + i].potar, pci_addr >> 12);
		out_be32(&pci->pow[index + i].potear, (u64)pci_addr >> 44);
		out_be32(&pci->pow[index + i].powbar, phys_addr >> 12);
		out_be32(&pci->pow[index + i].powar, flags | (bits - 1));

		pci_addr += (resource_size_t)1U << bits;
		phys_addr += (resource_size_t)1U << bits;
		size -= (resource_size_t)1U << bits;
	}

	return i;
}

/* atmu setup for fsl pci/pcie controller */
static void __init setup_pci_atmu(struct pci_controller *hose,
				  struct resource *rsrc)
{
	struct ccsr_pci __iomem *pci;
	int i, j, n, mem_log, win_idx = 3, start_idx = 1, end_idx = 4;
	u64 mem, sz, paddr_hi = 0;
	u64 paddr_lo = ULLONG_MAX;
	u32 pcicsrbar = 0, pcicsrbar_sz;
	u32 piwar = PIWAR_EN | PIWAR_PF | PIWAR_TGI_LOCAL |
			PIWAR_READ_SNOOP | PIWAR_WRITE_SNOOP;
	const char *name = hose->dn->full_name;
	const u64 *reg;
	int len;

	pr_debug("PCI memory map start 0x%016llx, size 0x%016llx\n",
		 (u64)rsrc->start, (u64)resource_size(rsrc));

	pci = ioremap(rsrc->start, resource_size(rsrc));
	if (!pci) {
	    dev_err(hose->parent, "Unable to map ATMU registers\n");
	    return;
	}

	if (early_find_capability(hose, 0, 0, PCI_CAP_ID_EXP)) {
		if (in_be32(&pci->block_rev1) >= PCIE_IP_REV_2_2) {
			win_idx = 2;
			start_idx = 0;
			end_idx = 3;
		}
	}

	/* Disable all windows (except powar0 since it's ignored) */
	for(i = 1; i < 5; i++)
		out_be32(&pci->pow[i].powar, 0);
	for (i = start_idx; i < end_idx; i++)
		out_be32(&pci->piw[i].piwar, 0);

	/* Setup outbound MEM window */
	for(i = 0, j = 1; i < 3; i++) {
		if (!(hose->mem_resources[i].flags & IORESOURCE_MEM))
			continue;

		paddr_lo = min(paddr_lo, (u64)hose->mem_resources[i].start);
		paddr_hi = max(paddr_hi, (u64)hose->mem_resources[i].end);

		n = setup_one_atmu(pci, j, &hose->mem_resources[i],
				   hose->pci_mem_offset);

		if (n < 0 || j >= 5) {
			pr_err("Ran out of outbound PCI ATMUs for resource %d!\n", i);
			hose->mem_resources[i].flags |= IORESOURCE_DISABLED;
		} else
			j += n;
	}

	/* Setup outbound IO window */
	if (hose->io_resource.flags & IORESOURCE_IO) {
		if (j >= 5) {
			pr_err("Ran out of outbound PCI ATMUs for IO resource\n");
		} else {
			pr_debug("PCI IO resource start 0x%016llx, size 0x%016llx, "
				 "phy base 0x%016llx.\n",
				 (u64)hose->io_resource.start,
				 (u64)resource_size(&hose->io_resource),
				 (u64)hose->io_base_phys);
			out_be32(&pci->pow[j].potar, (hose->io_resource.start >> 12));
			out_be32(&pci->pow[j].potear, 0);
			out_be32(&pci->pow[j].powbar, (hose->io_base_phys >> 12));
			/* Enable, IO R/W */
			out_be32(&pci->pow[j].powar, 0x80088000
				| (__ilog2(hose->io_resource.end
				- hose->io_resource.start + 1) - 1));
		}
	}

	/* convert to pci address space */
	paddr_hi -= hose->pci_mem_offset;
	paddr_lo -= hose->pci_mem_offset;

	if (paddr_hi == paddr_lo) {
		pr_err("%s: No outbound window space\n", name);
		goto out;
	}

	if (paddr_lo == 0) {
		pr_err("%s: No space for inbound window\n", name);
		goto out;
	}

	/* setup PCSRBAR/PEXCSRBAR */
	early_write_config_dword(hose, 0, 0, PCI_BASE_ADDRESS_0, 0xffffffff);
	early_read_config_dword(hose, 0, 0, PCI_BASE_ADDRESS_0, &pcicsrbar_sz);
	pcicsrbar_sz = ~pcicsrbar_sz + 1;

	if (paddr_hi < (0x100000000ull - pcicsrbar_sz) ||
		(paddr_lo > 0x100000000ull))
		pcicsrbar = 0x100000000ull - pcicsrbar_sz;
	else
		pcicsrbar = (paddr_lo - pcicsrbar_sz) & -pcicsrbar_sz;
	early_write_config_dword(hose, 0, 0, PCI_BASE_ADDRESS_0, pcicsrbar);

	paddr_lo = min(paddr_lo, (u64)pcicsrbar);

	pr_info("%s: PCICSRBAR @ 0x%x\n", name, pcicsrbar);

	/* Setup inbound mem window */
	mem = memblock_end_of_DRAM();

	/*
	 * The msi-address-64 property, if it exists, indicates the physical
	 * address of the MSIIR register.  Normally, this register is located
	 * inside CCSR, so the ATMU that covers all of CCSR is used. But if
	 * this property exists, then we normally need to create a new ATMU
	 * for it.  For now, however, we cheat.  The only entity that creates
	 * this property is the Freescale hypervisor, and the address is
	 * specified in the partition configuration.  Typically, the address
	 * is located in the page immediately after the end of DDR.  If so, we
	 * can avoid allocating a new ATMU by extending the DDR ATMU by one
	 * page.
	 */
	reg = of_get_property(hose->dn, "msi-address-64", &len);
	if (reg && (len == sizeof(u64))) {
		u64 address = be64_to_cpup(reg);

		if ((address >= mem) && (address < (mem + PAGE_SIZE))) {
			pr_info("%s: extending DDR ATMU to cover MSIIR", name);
			mem += PAGE_SIZE;
		} else {
			/* TODO: Create a new ATMU for MSIIR */
			pr_warn("%s: msi-address-64 address of %llx is "
				"unsupported\n", name, address);
		}
	}

	sz = min(mem, paddr_lo);
	mem_log = __ilog2_u64(sz);

	/* PCIe can overmap inbound & outbound since RX & TX are separated */
	if (early_find_capability(hose, 0, 0, PCI_CAP_ID_EXP)) {
		/* Size window to exact size if power-of-two or one size up */
		if ((1ull << mem_log) != mem) {
			if ((1ull << mem_log) > mem)
				pr_info("%s: Setting PCI inbound window "
					"greater than memory size\n", name);
			mem_log++;
		}

		piwar |= ((mem_log - 1) & PIWAR_SZ_MASK);

		/* Setup inbound memory window */
		out_be32(&pci->piw[win_idx].pitar,  0x00000000);
		out_be32(&pci->piw[win_idx].piwbar, 0x00000000);
		out_be32(&pci->piw[win_idx].piwar,  piwar);
		win_idx--;

		hose->dma_window_base_cur = 0x00000000;
		hose->dma_window_size = (resource_size_t)sz;

		/*
		 * if we have >4G of memory setup second PCI inbound window to
		 * let devices that are 64-bit address capable to work w/o
		 * SWIOTLB and access the full range of memory
		 */
		if (sz != mem) {
			mem_log = __ilog2_u64(mem);

			/* Size window up if we dont fit in exact power-of-2 */
			if ((1ull << mem_log) != mem)
				mem_log++;

			piwar = (piwar & ~PIWAR_SZ_MASK) | (mem_log - 1);

			/* Setup inbound memory window */
			out_be32(&pci->piw[win_idx].pitar,  0x00000000);
			out_be32(&pci->piw[win_idx].piwbear,
					pci64_dma_offset >> 44);
			out_be32(&pci->piw[win_idx].piwbar,
					pci64_dma_offset >> 12);
			out_be32(&pci->piw[win_idx].piwar,  piwar);

			/*
			 * install our own dma_set_mask handler to fixup dma_ops
			 * and dma_offset
			 */
			ppc_md.dma_set_mask = fsl_pci_dma_set_mask;

			pr_info("%s: Setup 64-bit PCI DMA window\n", name);
		}
	} else {
		u64 paddr = 0;

		/* Setup inbound memory window */
		out_be32(&pci->piw[win_idx].pitar,  paddr >> 12);
		out_be32(&pci->piw[win_idx].piwbar, paddr >> 12);
		out_be32(&pci->piw[win_idx].piwar,  (piwar | (mem_log - 1)));
		win_idx--;

		paddr += 1ull << mem_log;
		sz -= 1ull << mem_log;

		if (sz) {
			mem_log = __ilog2_u64(sz);
			piwar |= (mem_log - 1);

			out_be32(&pci->piw[win_idx].pitar,  paddr >> 12);
			out_be32(&pci->piw[win_idx].piwbar, paddr >> 12);
			out_be32(&pci->piw[win_idx].piwar,  piwar);
			win_idx--;

			paddr += 1ull << mem_log;
		}

		hose->dma_window_base_cur = 0x00000000;
		hose->dma_window_size = (resource_size_t)paddr;
	}

	if (hose->dma_window_size < mem) {
#ifndef CONFIG_SWIOTLB
		pr_err("%s: ERROR: Memory size exceeds PCI ATMU ability to "
			"map - enable CONFIG_SWIOTLB to avoid dma errors.\n",
			 name);
#endif
		/* adjusting outbound windows could reclaim space in mem map */
		if (paddr_hi < 0xffffffffull)
			pr_warning("%s: WARNING: Outbound window cfg leaves "
				"gaps in memory map. Adjusting the memory map "
				"could reduce unnecessary bounce buffering.\n",
				name);

		pr_info("%s: DMA window size is 0x%llx\n", name,
			(u64)hose->dma_window_size);
	}

out:
	iounmap(pci);
}

static void __init setup_pci_cmd(struct pci_controller *hose)
{
	u16 cmd;
	int cap_x;

	early_read_config_word(hose, 0, 0, PCI_COMMAND, &cmd);
	cmd |= PCI_COMMAND_SERR | PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY
		| PCI_COMMAND_IO;
	early_write_config_word(hose, 0, 0, PCI_COMMAND, cmd);

	cap_x = early_find_capability(hose, 0, 0, PCI_CAP_ID_PCIX);
	if (cap_x) {
		int pci_x_cmd = cap_x + PCI_X_CMD;
		cmd = PCI_X_CMD_MAX_SPLIT | PCI_X_CMD_MAX_READ
			| PCI_X_CMD_ERO | PCI_X_CMD_DPERR_E;
		early_write_config_word(hose, 0, 0, pci_x_cmd, cmd);
	} else {
		early_write_config_byte(hose, 0, 0, PCI_LATENCY_TIMER, 0x80);
	}
}

void fsl_pcibios_fixup_bus(struct pci_bus *bus)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
	int i, is_pcie = 0, no_link;

	/* The root complex bridge comes up with bogus resources,
	 * we copy the PHB ones in.
	 *
	 * With the current generic PCI code, the PHB bus no longer
	 * has bus->resource[0..4] set, so things are a bit more
	 * tricky.
	 */

	if (fsl_pcie_bus_fixup)
		is_pcie = early_find_capability(hose, 0, 0, PCI_CAP_ID_EXP);
	no_link = !!(hose->indirect_type & PPC_INDIRECT_TYPE_NO_PCIE_LINK);

	if (bus->parent == hose->bus && (is_pcie || no_link)) {
		for (i = 0; i < PCI_BRIDGE_RESOURCE_NUM; ++i) {
			struct resource *res = bus->resource[i];
			struct resource *par;

			if (!res)
				continue;
			if (i == 0)
				par = &hose->io_resource;
			else if (i < 4)
				par = &hose->mem_resources[i-1];
			else par = NULL;

			res->start = par ? par->start : 0;
			res->end   = par ? par->end   : 0;
			res->flags = par ? par->flags : 0;
		}
	}
}

int __init fsl_add_bridge(struct device_node *dev, int is_primary)
{
	int len;
	struct pci_controller *hose;
	struct resource rsrc;
	const int *bus_range;
	u8 hdr_type, progif;

	if (!of_device_is_available(dev)) {
		pr_warning("%s: disabled\n", dev->full_name);
		return -ENODEV;
	}

	pr_debug("Adding PCI host bridge %s\n", dev->full_name);

	/* Fetch host bridge registers address */
	if (of_address_to_resource(dev, 0, &rsrc)) {
		printk(KERN_WARNING "Can't get pci register base!");
		return -ENOMEM;
	}

	/* Get bus range if any */
	bus_range = of_get_property(dev, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int))
		printk(KERN_WARNING "Can't get bus-range for %s, assume"
			" bus 0\n", dev->full_name);

	pci_add_flags(PCI_REASSIGN_ALL_BUS);
	hose = pcibios_alloc_controller(dev);
	if (!hose)
		return -ENOMEM;

	hose->first_busno = bus_range ? bus_range[0] : 0x0;
	hose->last_busno = bus_range ? bus_range[1] : 0xff;

	setup_indirect_pci(hose, rsrc.start, rsrc.start + 0x4,
		PPC_INDIRECT_TYPE_BIG_ENDIAN);

	if (early_find_capability(hose, 0, 0, PCI_CAP_ID_EXP)) {
		/* For PCIE read HEADER_TYPE to identify controler mode */
		early_read_config_byte(hose, 0, 0, PCI_HEADER_TYPE, &hdr_type);
		if ((hdr_type & 0x7f) != PCI_HEADER_TYPE_BRIDGE)
			goto no_bridge;

	} else {
		/* For PCI read PROG to identify controller mode */
		early_read_config_byte(hose, 0, 0, PCI_CLASS_PROG, &progif);
		if ((progif & 1) == 1)
			goto no_bridge;
	}

	setup_pci_cmd(hose);

	/* check PCI express link status */
	if (early_find_capability(hose, 0, 0, PCI_CAP_ID_EXP)) {
		hose->indirect_type |= PPC_INDIRECT_TYPE_EXT_REG |
			PPC_INDIRECT_TYPE_SURPRESS_PRIMARY_BUS;
		if (fsl_pcie_check_link(hose))
			hose->indirect_type |= PPC_INDIRECT_TYPE_NO_PCIE_LINK;
	}

	printk(KERN_INFO "Found FSL PCI host bridge at 0x%016llx. "
		"Firmware bus number: %d->%d\n",
		(unsigned long long)rsrc.start, hose->first_busno,
		hose->last_busno);

	pr_debug(" ->Hose at 0x%p, cfg_addr=0x%p,cfg_data=0x%p\n",
		hose, hose->cfg_addr, hose->cfg_data);

	/* Interpret the "ranges" property */
	/* This also maps the I/O region and sets isa_io/mem_base */
	pci_process_bridge_OF_ranges(hose, dev, is_primary);

	/* Setup PEX window registers */
	setup_pci_atmu(hose, &rsrc);

	return 0;

no_bridge:
	/* unmap cfg_data & cfg_addr separately if not on same page */
	if (((unsigned long)hose->cfg_data & PAGE_MASK) !=
	    ((unsigned long)hose->cfg_addr & PAGE_MASK))
		iounmap(hose->cfg_data);
	iounmap(hose->cfg_addr);
	pcibios_free_controller(hose);
	return -ENODEV;
}
#endif /* CONFIG_FSL_SOC_BOOKE || CONFIG_PPC_86xx */

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_FREESCALE, PCI_ANY_ID, quirk_fsl_pcie_header);

#if defined(CONFIG_PPC_83xx) || defined(CONFIG_PPC_MPC512x)
struct mpc83xx_pcie_priv {
	void __iomem *cfg_type0;
	void __iomem *cfg_type1;
	u32 dev_base;
};

struct pex_inbound_window {
	u32 ar;
	u32 tar;
	u32 barl;
	u32 barh;
};

/*
 * With the convention of u-boot, the PCIE outbound window 0 serves
 * as configuration transactions outbound.
 */
#define PEX_OUTWIN0_BAR		0xCA4
#define PEX_OUTWIN0_TAL		0xCA8
#define PEX_OUTWIN0_TAH		0xCAC
#define PEX_RC_INWIN_BASE	0xE60
#define PEX_RCIWARn_EN		0x1

static int mpc83xx_pcie_exclude_device(struct pci_bus *bus, unsigned int devfn)
{
	struct pci_controller *hose = pci_bus_to_host(bus);

	if (hose->indirect_type & PPC_INDIRECT_TYPE_NO_PCIE_LINK)
		return PCIBIOS_DEVICE_NOT_FOUND;
	/*
	 * Workaround for the HW bug: for Type 0 configure transactions the
	 * PCI-E controller does not check the device number bits and just
	 * assumes that the device number bits are 0.
	 */
	if (bus->number == hose->first_busno ||
			bus->primary == hose->first_busno) {
		if (devfn & 0xf8)
			return PCIBIOS_DEVICE_NOT_FOUND;
	}

	if (ppc_md.pci_exclude_device) {
		if (ppc_md.pci_exclude_device(hose, bus->number, devfn))
			return PCIBIOS_DEVICE_NOT_FOUND;
	}

	return PCIBIOS_SUCCESSFUL;
}

static void __iomem *mpc83xx_pcie_remap_cfg(struct pci_bus *bus,
					    unsigned int devfn, int offset)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
	struct mpc83xx_pcie_priv *pcie = hose->dn->data;
	u32 dev_base = bus->number << 24 | devfn << 16;
	int ret;

	ret = mpc83xx_pcie_exclude_device(bus, devfn);
	if (ret)
		return NULL;

	offset &= 0xfff;

	/* Type 0 */
	if (bus->number == hose->first_busno)
		return pcie->cfg_type0 + offset;

	if (pcie->dev_base == dev_base)
		goto mapped;

	out_le32(pcie->cfg_type0 + PEX_OUTWIN0_TAL, dev_base);

	pcie->dev_base = dev_base;
mapped:
	return pcie->cfg_type1 + offset;
}

static int mpc83xx_pcie_read_config(struct pci_bus *bus, unsigned int devfn,
				    int offset, int len, u32 *val)
{
	void __iomem *cfg_addr;

	cfg_addr = mpc83xx_pcie_remap_cfg(bus, devfn, offset);
	if (!cfg_addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (len) {
	case 1:
		*val = in_8(cfg_addr);
		break;
	case 2:
		*val = in_le16(cfg_addr);
		break;
	default:
		*val = in_le32(cfg_addr);
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int mpc83xx_pcie_write_config(struct pci_bus *bus, unsigned int devfn,
				     int offset, int len, u32 val)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
	void __iomem *cfg_addr;

	cfg_addr = mpc83xx_pcie_remap_cfg(bus, devfn, offset);
	if (!cfg_addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* PPC_INDIRECT_TYPE_SURPRESS_PRIMARY_BUS */
	if (offset == PCI_PRIMARY_BUS && bus->number == hose->first_busno)
		val &= 0xffffff00;

	switch (len) {
	case 1:
		out_8(cfg_addr, val);
		break;
	case 2:
		out_le16(cfg_addr, val);
		break;
	default:
		out_le32(cfg_addr, val);
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops mpc83xx_pcie_ops = {
	.read = mpc83xx_pcie_read_config,
	.write = mpc83xx_pcie_write_config,
};

static int __init mpc83xx_pcie_setup(struct pci_controller *hose,
				     struct resource *reg)
{
	struct mpc83xx_pcie_priv *pcie;
	u32 cfg_bar;
	int ret = -ENOMEM;

	pcie = zalloc_maybe_bootmem(sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return ret;

	pcie->cfg_type0 = ioremap(reg->start, resource_size(reg));
	if (!pcie->cfg_type0)
		goto err0;

	cfg_bar = in_le32(pcie->cfg_type0 + PEX_OUTWIN0_BAR);
	if (!cfg_bar) {
		/* PCI-E isn't configured. */
		ret = -ENODEV;
		goto err1;
	}

	pcie->cfg_type1 = ioremap(cfg_bar, 0x1000);
	if (!pcie->cfg_type1)
		goto err1;

	WARN_ON(hose->dn->data);
	hose->dn->data = pcie;
	hose->ops = &mpc83xx_pcie_ops;

	out_le32(pcie->cfg_type0 + PEX_OUTWIN0_TAH, 0);
	out_le32(pcie->cfg_type0 + PEX_OUTWIN0_TAL, 0);

	if (fsl_pcie_check_link(hose))
		hose->indirect_type |= PPC_INDIRECT_TYPE_NO_PCIE_LINK;

	return 0;
err1:
	iounmap(pcie->cfg_type0);
err0:
	kfree(pcie);
	return ret;

}

int __init mpc83xx_add_bridge(struct device_node *dev)
{
	int ret;
	int len;
	struct pci_controller *hose;
	struct resource rsrc_reg;
	struct resource rsrc_cfg;
	const int *bus_range;
	int primary;

	is_mpc83xx_pci = 1;

	if (!of_device_is_available(dev)) {
		pr_warning("%s: disabled by the firmware.\n",
			   dev->full_name);
		return -ENODEV;
	}
	pr_debug("Adding PCI host bridge %s\n", dev->full_name);

	/* Fetch host bridge registers address */
	if (of_address_to_resource(dev, 0, &rsrc_reg)) {
		printk(KERN_WARNING "Can't get pci register base!\n");
		return -ENOMEM;
	}

	memset(&rsrc_cfg, 0, sizeof(rsrc_cfg));

	if (of_address_to_resource(dev, 1, &rsrc_cfg)) {
		printk(KERN_WARNING
			"No pci config register base in dev tree, "
			"using default\n");
		/*
		 * MPC83xx supports up to two host controllers
		 * 	one at 0x8500 has config space registers at 0x8300
		 * 	one at 0x8600 has config space registers at 0x8380
		 */
		if ((rsrc_reg.start & 0xfffff) == 0x8500)
			rsrc_cfg.start = (rsrc_reg.start & 0xfff00000) + 0x8300;
		else if ((rsrc_reg.start & 0xfffff) == 0x8600)
			rsrc_cfg.start = (rsrc_reg.start & 0xfff00000) + 0x8380;
	}
	/*
	 * Controller at offset 0x8500 is primary
	 */
	if ((rsrc_reg.start & 0xfffff) == 0x8500)
		primary = 1;
	else
		primary = 0;

	/* Get bus range if any */
	bus_range = of_get_property(dev, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int)) {
		printk(KERN_WARNING "Can't get bus-range for %s, assume"
		       " bus 0\n", dev->full_name);
	}

	pci_add_flags(PCI_REASSIGN_ALL_BUS);
	hose = pcibios_alloc_controller(dev);
	if (!hose)
		return -ENOMEM;

	hose->first_busno = bus_range ? bus_range[0] : 0;
	hose->last_busno = bus_range ? bus_range[1] : 0xff;

	if (of_device_is_compatible(dev, "fsl,mpc8314-pcie")) {
		ret = mpc83xx_pcie_setup(hose, &rsrc_reg);
		if (ret)
			goto err0;
	} else {
		setup_indirect_pci(hose, rsrc_cfg.start,
				   rsrc_cfg.start + 4, 0);
	}

	printk(KERN_INFO "Found FSL PCI host bridge at 0x%016llx. "
	       "Firmware bus number: %d->%d\n",
	       (unsigned long long)rsrc_reg.start, hose->first_busno,
	       hose->last_busno);

	pr_debug(" ->Hose at 0x%p, cfg_addr=0x%p,cfg_data=0x%p\n",
	    hose, hose->cfg_addr, hose->cfg_data);

	/* Interpret the "ranges" property */
	/* This also maps the I/O region and sets isa_io/mem_base */
	pci_process_bridge_OF_ranges(hose, dev, primary);

	return 0;
err0:
	pcibios_free_controller(hose);
	return ret;
}
#endif /* CONFIG_PPC_83xx */

u64 fsl_pci_immrbar_base(struct pci_controller *hose)
{
#ifdef CONFIG_PPC_83xx
	if (is_mpc83xx_pci) {
		struct mpc83xx_pcie_priv *pcie = hose->dn->data;
		struct pex_inbound_window *in;
		int i;

		/* Walk the Root Complex Inbound windows to match IMMR base */
		in = pcie->cfg_type0 + PEX_RC_INWIN_BASE;
		for (i = 0; i < 4; i++) {
			/* not enabled, skip */
			if (!in_le32(&in[i].ar) & PEX_RCIWARn_EN)
				 continue;

			if (get_immrbase() == in_le32(&in[i].tar))
				return (u64)in_le32(&in[i].barh) << 32 |
					    in_le32(&in[i].barl);
		}

		printk(KERN_WARNING "could not find PCI BAR matching IMMR\n");
	}
#endif

#if defined(CONFIG_FSL_SOC_BOOKE) || defined(CONFIG_PPC_86xx)
	if (!is_mpc83xx_pci) {
		u32 base;

		pci_bus_read_config_dword(hose->bus,
			PCI_DEVFN(0, 0), PCI_BASE_ADDRESS_0, &base);
		return base;
	}
#endif

	return 0;
}

#if defined(CONFIG_FSL_SOC_BOOKE) || defined(CONFIG_PPC_86xx)
static const struct of_device_id pci_ids[] = {
	{ .compatible = "fsl,mpc8540-pci", },
	{ .compatible = "fsl,mpc8548-pcie", },
	{ .compatible = "fsl,mpc8610-pci", },
	{ .compatible = "fsl,mpc8641-pcie", },
	{ .compatible = "fsl,p1022-pcie", },
	{ .compatible = "fsl,p1010-pcie", },
	{ .compatible = "fsl,p1023-pcie", },
	{ .compatible = "fsl,p4080-pcie", },
	{ .compatible = "fsl,qoriq-pcie-v2.4", },
	{ .compatible = "fsl,qoriq-pcie-v2.3", },
	{ .compatible = "fsl,qoriq-pcie-v2.2", },
	{},
};

struct device_node *fsl_pci_primary;

void fsl_pci_assign_primary(void)
{
	struct device_node *np;

	/* Callers can specify the primary bus using other means. */
	if (fsl_pci_primary)
		return;

	/* If a PCI host bridge contains an ISA node, it's primary. */
	np = of_find_node_by_type(NULL, "isa");
	while ((fsl_pci_primary = of_get_parent(np))) {
		of_node_put(np);
		np = fsl_pci_primary;

		if (of_match_node(pci_ids, np) && of_device_is_available(np))
			return;
	}

	/*
	 * If there's no PCI host bridge with ISA, arbitrarily
	 * designate one as primary.  This can go away once
	 * various bugs with primary-less systems are fixed.
	 */
	for_each_matching_node(np, pci_ids) {
		if (of_device_is_available(np)) {
			fsl_pci_primary = np;
			of_node_put(np);
			return;
		}
	}
}

static int __devinit fsl_pci_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *node;
#ifdef CONFIG_SWIOTLB
	struct pci_controller *hose;
#endif

	node = pdev->dev.of_node;
	ret = fsl_add_bridge(node, fsl_pci_primary == node);

#ifdef CONFIG_SWIOTLB
	if (ret == 0) {
		hose = pci_find_hose_for_OF_device(pdev->dev.of_node);

		/*
		 * if we couldn't map all of DRAM via the dma windows
		 * we need SWIOTLB to handle buffers located outside of
		 * dma capable memory region
		 */
		if (memblock_end_of_DRAM() - 1 > hose->dma_window_base_cur +
				hose->dma_window_size)
			ppc_swiotlb_enable = 1;
	}
#endif

	mpc85xx_pci_err_probe(pdev);

	return 0;
}

static struct platform_driver fsl_pci_driver = {
	.driver = {
		.name = "fsl-pci",
		.of_match_table = pci_ids,
	},
	.probe = fsl_pci_probe,
};

static int __init fsl_pci_init(void)
{
	return platform_driver_register(&fsl_pci_driver);
}
arch_initcall(fsl_pci_init);
#endif
