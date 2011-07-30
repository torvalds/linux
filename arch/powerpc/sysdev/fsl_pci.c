/*
 * MPC83xx/85xx/86xx PCI/PCIE support routing.
 *
 * Copyright 2007-2009 Freescale Semiconductor, Inc.
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
#include <linux/lmb.h>
#include <linux/log2.h>

#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>

static int fsl_pcie_bus_fixup;

static void __init quirk_fsl_pcie_header(struct pci_dev *dev)
{
	/* if we aren't a PCIe don't bother */
	if (!pci_find_capability(dev, PCI_CAP_ID_EXP))
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

#if defined(CONFIG_PPC_85xx) || defined(CONFIG_PPC_86xx)
static int __init setup_one_atmu(struct ccsr_pci __iomem *pci,
	unsigned int index, const struct resource *res,
	resource_size_t offset)
{
	resource_size_t pci_addr = res->start - offset;
	resource_size_t phys_addr = res->start;
	resource_size_t size = res->end - res->start + 1;
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
	int i, j, n, mem_log, win_idx = 2;
	u64 mem, sz, paddr_hi = 0;
	u64 paddr_lo = ULLONG_MAX;
	u32 pcicsrbar = 0, pcicsrbar_sz;
	u32 piwar = PIWAR_EN | PIWAR_PF | PIWAR_TGI_LOCAL |
			PIWAR_READ_SNOOP | PIWAR_WRITE_SNOOP;
	char *name = hose->dn->full_name;

	pr_debug("PCI memory map start 0x%016llx, size 0x%016llx\n",
		    (u64)rsrc->start, (u64)rsrc->end - (u64)rsrc->start + 1);
	pci = ioremap(rsrc->start, rsrc->end - rsrc->start + 1);
	if (!pci) {
	    dev_err(hose->parent, "Unable to map ATMU registers\n");
	    return;
	}

	/* Disable all windows (except powar0 since it's ignored) */
	for(i = 1; i < 5; i++)
		out_be32(&pci->pow[i].powar, 0);
	for(i = 0; i < 3; i++)
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
				(u64)hose->io_resource.end - (u64)hose->io_resource.start + 1,
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
		return ;
	}

	if (paddr_lo == 0) {
		pr_err("%s: No space for inbound window\n", name);
		return ;
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
	mem = lmb_end_of_DRAM();
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

		piwar |= (mem_log - 1);

		/* Setup inbound memory window */
		out_be32(&pci->piw[win_idx].pitar,  0x00000000);
		out_be32(&pci->piw[win_idx].piwbar, 0x00000000);
		out_be32(&pci->piw[win_idx].piwar,  piwar);
		win_idx--;

		hose->dma_window_base_cur = 0x00000000;
		hose->dma_window_size = (resource_size_t)sz;
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
	int i;

	if ((bus->parent == hose->bus) &&
	    ((fsl_pcie_bus_fixup &&
	      early_find_capability(hose, 0, 0, PCI_CAP_ID_EXP)) ||
	     (hose->indirect_type & PPC_INDIRECT_TYPE_NO_PCIE_LINK)))
	{
		for (i = 0; i < 4; ++i) {
			struct resource *res = bus->resource[i];
			struct resource *par = bus->parent->resource[i];
			if (res) {
				res->start = 0;
				res->end   = 0;
				res->flags = 0;
			}
			if (res && par) {
				res->start = par->start;
				res->end   = par->end;
				res->flags = par->flags;
			}
		}
	}
}

int __init fsl_add_bridge(struct device_node *dev, int is_primary)
{
	int len;
	struct pci_controller *hose;
	struct resource rsrc;
	const int *bus_range;

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

	ppc_pci_add_flags(PPC_PCI_REASSIGN_ALL_BUS);
	hose = pcibios_alloc_controller(dev);
	if (!hose)
		return -ENOMEM;

	hose->first_busno = bus_range ? bus_range[0] : 0x0;
	hose->last_busno = bus_range ? bus_range[1] : 0xff;

	setup_indirect_pci(hose, rsrc.start, rsrc.start + 0x4,
		PPC_INDIRECT_TYPE_BIG_ENDIAN);
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
}

DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8548E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8548, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8543E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8543, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8547E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8545E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8545, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8569E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8569, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8568E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8568, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8567E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8567, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8533E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8533, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8544E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8544, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8572E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8572, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8536E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8536, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8641, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8641D, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8610, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_P2020E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_P2020, quirk_fsl_pcie_header);
#endif /* CONFIG_PPC_85xx || CONFIG_PPC_86xx */

#if defined(CONFIG_PPC_83xx) || defined(CONFIG_PPC_MPC512x)
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8314E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8314, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8315E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8315, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8377E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8377, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8378E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8378, quirk_fsl_pcie_header);

struct mpc83xx_pcie_priv {
	void __iomem *cfg_type0;
	void __iomem *cfg_type1;
	u32 dev_base;
};

/*
 * With the convention of u-boot, the PCIE outbound window 0 serves
 * as configuration transactions outbound.
 */
#define PEX_OUTWIN0_BAR		0xCA4
#define PEX_OUTWIN0_TAL		0xCA8
#define PEX_OUTWIN0_TAH		0xCAC

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
	u8 bus_no = bus->number - hose->first_busno;
	u32 dev_base = bus_no << 24 | devfn << 16;
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
	void __iomem *cfg_addr;

	cfg_addr = mpc83xx_pcie_remap_cfg(bus, devfn, offset);
	if (!cfg_addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

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

	ppc_pci_add_flags(PPC_PCI_REASSIGN_ALL_BUS);
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
