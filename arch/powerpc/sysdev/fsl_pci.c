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
#include <linux/fsl/edac.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/memblock.h>
#include <linux/log2.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/uaccess.h>

#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/ppc-pci.h>
#include <asm/machdep.h>
#include <asm/mpc85xx.h>
#include <asm/disassemble.h>
#include <asm/ppc-opcode.h>
#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>

static int fsl_pcie_bus_fixup, is_mpc83xx_pci;

static void quirk_fsl_pcie_early(struct pci_dev *dev)
{
	u8 hdr_type;

	/* if we aren't a PCIe don't bother */
	if (!pci_is_pcie(dev))
		return;

	/* if we aren't in host mode don't bother */
	pci_read_config_byte(dev, PCI_HEADER_TYPE, &hdr_type);
	if ((hdr_type & 0x7f) != PCI_HEADER_TYPE_BRIDGE)
		return;

	dev->class = PCI_CLASS_BRIDGE_PCI << 8;
	fsl_pcie_bus_fixup = 1;
	return;
}

static int fsl_indirect_read_config(struct pci_bus *, unsigned int,
				    int, int, u32 *);

static int fsl_pcie_check_link(struct pci_controller *hose)
{
	u32 val = 0;

	if (hose->indirect_type & PPC_INDIRECT_TYPE_FSL_CFG_REG_LINK) {
		if (hose->ops->read == fsl_indirect_read_config)
			__indirect_read_config(hose, hose->first_busno, 0,
					       PCIE_LTSSM, 4, &val);
		else
			early_read_config_dword(hose, 0, 0, PCIE_LTSSM, &val);
		if (val < PCIE_LTSSM_L0)
			return 1;
	} else {
		struct ccsr_pci __iomem *pci = hose->private_data;
		/* for PCIe IP rev 3.0 or greater use CSR0 for link state */
		val = (in_be32(&pci->pex_csr0) & PEX_CSR0_LTSSM_MASK)
				>> PEX_CSR0_LTSSM_SHIFT;
		if (val != PEX_CSR0_LTSSM_L0)
			return 1;
	}

	return 0;
}

static int fsl_indirect_read_config(struct pci_bus *bus, unsigned int devfn,
				    int offset, int len, u32 *val)
{
	struct pci_controller *hose = pci_bus_to_host(bus);

	if (fsl_pcie_check_link(hose))
		hose->indirect_type |= PPC_INDIRECT_TYPE_NO_PCIE_LINK;
	else
		hose->indirect_type &= ~PPC_INDIRECT_TYPE_NO_PCIE_LINK;

	return indirect_read_config(bus, devfn, offset, len, val);
}

#if defined(CONFIG_FSL_SOC_BOOKE) || defined(CONFIG_PPC_86xx)

static struct pci_ops fsl_indirect_pcie_ops =
{
	.read = fsl_indirect_read_config,
	.write = indirect_write_config,
};

static u64 pci64_dma_offset;

#ifdef CONFIG_SWIOTLB
static void setup_swiotlb_ops(struct pci_controller *hose)
{
	if (ppc_swiotlb_enable) {
		hose->controller_ops.dma_dev_setup = pci_dma_dev_setup_swiotlb;
		set_pci_dma_ops(&swiotlb_dma_ops);
	}
}
#else
static inline void setup_swiotlb_ops(struct pci_controller *hose) {}
#endif

static int fsl_pci_dma_set_mask(struct device *dev, u64 dma_mask)
{
	if (!dev->dma_mask || !dma_supported(dev, dma_mask))
		return -EIO;

	/*
	 * Fix up PCI devices that are able to DMA to the large inbound
	 * mapping that allows addressing any RAM address from across PCI.
	 */
	if (dev_is_pci(dev) && dma_mask >= pci64_dma_offset * 2 - 1) {
		set_dma_ops(dev, &dma_direct_ops);
		set_dma_offset(dev, pci64_dma_offset);
	}

	*dev->dma_mask = dma_mask;
	return 0;
}

static int setup_one_atmu(struct ccsr_pci __iomem *pci,
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
		unsigned int bits = min_t(u32, ilog2(size),
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

static bool is_kdump(void)
{
	struct device_node *node;

	node = of_find_node_by_type(NULL, "memory");
	if (!node) {
		WARN_ON_ONCE(1);
		return false;
	}

	return of_property_read_bool(node, "linux,usable-memory");
}

/* atmu setup for fsl pci/pcie controller */
static void setup_pci_atmu(struct pci_controller *hose)
{
	struct ccsr_pci __iomem *pci = hose->private_data;
	int i, j, n, mem_log, win_idx = 3, start_idx = 1, end_idx = 4;
	u64 mem, sz, paddr_hi = 0;
	u64 offset = 0, paddr_lo = ULLONG_MAX;
	u32 pcicsrbar = 0, pcicsrbar_sz;
	u32 piwar = PIWAR_EN | PIWAR_PF | PIWAR_TGI_LOCAL |
			PIWAR_READ_SNOOP | PIWAR_WRITE_SNOOP;
	const char *name = hose->dn->full_name;
	const u64 *reg;
	int len;
	bool setup_inbound;

	/*
	 * If this is kdump, we don't want to trigger a bunch of PCI
	 * errors by closing the window on in-flight DMA.
	 *
	 * We still run most of the function's logic so that things like
	 * hose->dma_window_size still get set.
	 */
	setup_inbound = !is_kdump();

	if (of_device_is_compatible(hose->dn, "fsl,bsc9132-pcie")) {
		/*
		 * BSC9132 Rev1.0 has an issue where all the PEX inbound
		 * windows have implemented the default target value as 0xf
		 * for CCSR space.In all Freescale legacy devices the target
		 * of 0xf is reserved for local memory space. 9132 Rev1.0
		 * now has local mempry space mapped to target 0x0 instead of
		 * 0xf. Hence adding a workaround to remove the target 0xf
		 * defined for memory space from Inbound window attributes.
		 */
		piwar &= ~PIWAR_TGI_LOCAL;
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

	if (setup_inbound) {
		for (i = start_idx; i < end_idx; i++)
			out_be32(&pci->piw[i].piwar, 0);
	}

	/* Setup outbound MEM window */
	for(i = 0, j = 1; i < 3; i++) {
		if (!(hose->mem_resources[i].flags & IORESOURCE_MEM))
			continue;

		paddr_lo = min(paddr_lo, (u64)hose->mem_resources[i].start);
		paddr_hi = max(paddr_hi, (u64)hose->mem_resources[i].end);

		/* We assume all memory resources have the same offset */
		offset = hose->mem_offset[i];
		n = setup_one_atmu(pci, j, &hose->mem_resources[i], offset);

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
				| (ilog2(hose->io_resource.end
				- hose->io_resource.start + 1) - 1));
		}
	}

	/* convert to pci address space */
	paddr_hi -= offset;
	paddr_lo -= offset;

	if (paddr_hi == paddr_lo) {
		pr_err("%s: No outbound window space\n", name);
		return;
	}

	if (paddr_lo == 0) {
		pr_err("%s: No space for inbound window\n", name);
		return;
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
	pr_info("%s: end of DRAM %llx\n", __func__, mem);

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
	mem_log = ilog2(sz);

	/* PCIe can overmap inbound & outbound since RX & TX are separated */
	if (early_find_capability(hose, 0, 0, PCI_CAP_ID_EXP)) {
		/* Size window to exact size if power-of-two or one size up */
		if ((1ull << mem_log) != mem) {
			mem_log++;
			if ((1ull << mem_log) > mem)
				pr_info("%s: Setting PCI inbound window "
					"greater than memory size\n", name);
		}

		piwar |= ((mem_log - 1) & PIWAR_SZ_MASK);

		if (setup_inbound) {
			/* Setup inbound memory window */
			out_be32(&pci->piw[win_idx].pitar,  0x00000000);
			out_be32(&pci->piw[win_idx].piwbar, 0x00000000);
			out_be32(&pci->piw[win_idx].piwar,  piwar);
		}

		win_idx--;
		hose->dma_window_base_cur = 0x00000000;
		hose->dma_window_size = (resource_size_t)sz;

		/*
		 * if we have >4G of memory setup second PCI inbound window to
		 * let devices that are 64-bit address capable to work w/o
		 * SWIOTLB and access the full range of memory
		 */
		if (sz != mem) {
			mem_log = ilog2(mem);

			/* Size window up if we dont fit in exact power-of-2 */
			if ((1ull << mem_log) != mem)
				mem_log++;

			piwar = (piwar & ~PIWAR_SZ_MASK) | (mem_log - 1);
			pci64_dma_offset = 1ULL << mem_log;

			if (setup_inbound) {
				/* Setup inbound memory window */
				out_be32(&pci->piw[win_idx].pitar,  0x00000000);
				out_be32(&pci->piw[win_idx].piwbear,
						pci64_dma_offset >> 44);
				out_be32(&pci->piw[win_idx].piwbar,
						pci64_dma_offset >> 12);
				out_be32(&pci->piw[win_idx].piwar,  piwar);
			}

			/*
			 * install our own dma_set_mask handler to fixup dma_ops
			 * and dma_offset
			 */
			ppc_md.dma_set_mask = fsl_pci_dma_set_mask;

			pr_info("%s: Setup 64-bit PCI DMA window\n", name);
		}
	} else {
		u64 paddr = 0;

		if (setup_inbound) {
			/* Setup inbound memory window */
			out_be32(&pci->piw[win_idx].pitar,  paddr >> 12);
			out_be32(&pci->piw[win_idx].piwbar, paddr >> 12);
			out_be32(&pci->piw[win_idx].piwar,
				 (piwar | (mem_log - 1)));
		}

		win_idx--;
		paddr += 1ull << mem_log;
		sz -= 1ull << mem_log;

		if (sz) {
			mem_log = ilog2(sz);
			piwar |= (mem_log - 1);

			if (setup_inbound) {
				out_be32(&pci->piw[win_idx].pitar,
					 paddr >> 12);
				out_be32(&pci->piw[win_idx].piwbar,
					 paddr >> 12);
				out_be32(&pci->piw[win_idx].piwar, piwar);
			}

			win_idx--;
			paddr += 1ull << mem_log;
		}

		hose->dma_window_base_cur = 0x00000000;
		hose->dma_window_size = (resource_size_t)paddr;
	}

	if (hose->dma_window_size < mem) {
#ifdef CONFIG_SWIOTLB
		ppc_swiotlb_enable = 1;
#else
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

int fsl_add_bridge(struct platform_device *pdev, int is_primary)
{
	int len;
	struct pci_controller *hose;
	struct resource rsrc;
	const int *bus_range;
	u8 hdr_type, progif;
	struct device_node *dev;
	struct ccsr_pci __iomem *pci;
	u16 temp;
	u32 svr = mfspr(SPRN_SVR);

	dev = pdev->dev.of_node;

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

	/* set platform device as the parent */
	hose->parent = &pdev->dev;
	hose->first_busno = bus_range ? bus_range[0] : 0x0;
	hose->last_busno = bus_range ? bus_range[1] : 0xff;

	pr_debug("PCI memory map start 0x%016llx, size 0x%016llx\n",
		 (u64)rsrc.start, (u64)resource_size(&rsrc));

	pci = hose->private_data = ioremap(rsrc.start, resource_size(&rsrc));
	if (!hose->private_data)
		goto no_bridge;

	setup_indirect_pci(hose, rsrc.start, rsrc.start + 0x4,
			   PPC_INDIRECT_TYPE_BIG_ENDIAN);

	if (in_be32(&pci->block_rev1) < PCIE_IP_REV_3_0)
		hose->indirect_type |= PPC_INDIRECT_TYPE_FSL_CFG_REG_LINK;

	if (early_find_capability(hose, 0, 0, PCI_CAP_ID_EXP)) {
		/* use fsl_indirect_read_config for PCIe */
		hose->ops = &fsl_indirect_pcie_ops;
		/* For PCIE read HEADER_TYPE to identify controller mode */
		early_read_config_byte(hose, 0, 0, PCI_HEADER_TYPE, &hdr_type);
		if ((hdr_type & 0x7f) != PCI_HEADER_TYPE_BRIDGE)
			goto no_bridge;

	} else {
		/* For PCI read PROG to identify controller mode */
		early_read_config_byte(hose, 0, 0, PCI_CLASS_PROG, &progif);
		if ((progif & 1) &&
		    !of_property_read_bool(dev, "fsl,pci-agent-force-enum"))
			goto no_bridge;
	}

	setup_pci_cmd(hose);

	/* check PCI express link status */
	if (early_find_capability(hose, 0, 0, PCI_CAP_ID_EXP)) {
		hose->indirect_type |= PPC_INDIRECT_TYPE_EXT_REG |
			PPC_INDIRECT_TYPE_SURPRESS_PRIMARY_BUS;
		if (fsl_pcie_check_link(hose))
			hose->indirect_type |= PPC_INDIRECT_TYPE_NO_PCIE_LINK;
	} else {
		/*
		 * Set PBFR(PCI Bus Function Register)[10] = 1 to
		 * disable the combining of crossing cacheline
		 * boundary requests into one burst transaction.
		 * PCI-X operation is not affected.
		 * Fix erratum PCI 5 on MPC8548
		 */
#define PCI_BUS_FUNCTION 0x44
#define PCI_BUS_FUNCTION_MDS 0x400	/* Master disable streaming */
		if (((SVR_SOC_VER(svr) == SVR_8543) ||
		     (SVR_SOC_VER(svr) == SVR_8545) ||
		     (SVR_SOC_VER(svr) == SVR_8547) ||
		     (SVR_SOC_VER(svr) == SVR_8548)) &&
		    !early_find_capability(hose, 0, 0, PCI_CAP_ID_PCIX)) {
			early_read_config_word(hose, 0, 0,
					PCI_BUS_FUNCTION, &temp);
			temp |= PCI_BUS_FUNCTION_MDS;
			early_write_config_word(hose, 0, 0,
					PCI_BUS_FUNCTION, temp);
		}
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
	setup_pci_atmu(hose);

	/* Set up controller operations */
	setup_swiotlb_ops(hose);

	return 0;

no_bridge:
	iounmap(hose->private_data);
	/* unmap cfg_data & cfg_addr separately if not on same page */
	if (((unsigned long)hose->cfg_data & PAGE_MASK) !=
	    ((unsigned long)hose->cfg_addr & PAGE_MASK))
		iounmap(hose->cfg_data);
	iounmap(hose->cfg_addr);
	pcibios_free_controller(hose);
	return -ENODEV;
}
#endif /* CONFIG_FSL_SOC_BOOKE || CONFIG_PPC_86xx */

DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_FREESCALE, PCI_ANY_ID,
			quirk_fsl_pcie_early);

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

static int mpc83xx_pcie_write_config(struct pci_bus *bus, unsigned int devfn,
				     int offset, int len, u32 val)
{
	struct pci_controller *hose = pci_bus_to_host(bus);

	/* PPC_INDIRECT_TYPE_SURPRESS_PRIMARY_BUS */
	if (offset == PCI_PRIMARY_BUS && bus->number == hose->first_busno)
		val &= 0xffffff00;

	return pci_generic_config_write(bus, devfn, offset, len, val);
}

static struct pci_ops mpc83xx_pcie_ops = {
	.map_bus = mpc83xx_pcie_remap_cfg,
	.read = pci_generic_config_read,
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
	hose->indirect_type |= PPC_INDIRECT_TYPE_FSL_CFG_REG_LINK;

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
			if (!(in_le32(&in[i].ar) & PEX_RCIWARn_EN))
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

		/*
		 * For PEXCSRBAR, bit 3-0 indicate prefetchable and
		 * address type. So when getting base address, these
		 * bits should be masked
		 */
		base &= PCI_BASE_ADDRESS_MEM_MASK;

		return base;
	}
#endif

	return 0;
}

#ifdef CONFIG_E500
static int mcheck_handle_load(struct pt_regs *regs, u32 inst)
{
	unsigned int rd, ra, rb, d;

	rd = get_rt(inst);
	ra = get_ra(inst);
	rb = get_rb(inst);
	d = get_d(inst);

	switch (get_op(inst)) {
	case 31:
		switch (get_xop(inst)) {
		case OP_31_XOP_LWZX:
		case OP_31_XOP_LWBRX:
			regs->gpr[rd] = 0xffffffff;
			break;

		case OP_31_XOP_LWZUX:
			regs->gpr[rd] = 0xffffffff;
			regs->gpr[ra] += regs->gpr[rb];
			break;

		case OP_31_XOP_LBZX:
			regs->gpr[rd] = 0xff;
			break;

		case OP_31_XOP_LBZUX:
			regs->gpr[rd] = 0xff;
			regs->gpr[ra] += regs->gpr[rb];
			break;

		case OP_31_XOP_LHZX:
		case OP_31_XOP_LHBRX:
			regs->gpr[rd] = 0xffff;
			break;

		case OP_31_XOP_LHZUX:
			regs->gpr[rd] = 0xffff;
			regs->gpr[ra] += regs->gpr[rb];
			break;

		case OP_31_XOP_LHAX:
			regs->gpr[rd] = ~0UL;
			break;

		case OP_31_XOP_LHAUX:
			regs->gpr[rd] = ~0UL;
			regs->gpr[ra] += regs->gpr[rb];
			break;

		default:
			return 0;
		}
		break;

	case OP_LWZ:
		regs->gpr[rd] = 0xffffffff;
		break;

	case OP_LWZU:
		regs->gpr[rd] = 0xffffffff;
		regs->gpr[ra] += (s16)d;
		break;

	case OP_LBZ:
		regs->gpr[rd] = 0xff;
		break;

	case OP_LBZU:
		regs->gpr[rd] = 0xff;
		regs->gpr[ra] += (s16)d;
		break;

	case OP_LHZ:
		regs->gpr[rd] = 0xffff;
		break;

	case OP_LHZU:
		regs->gpr[rd] = 0xffff;
		regs->gpr[ra] += (s16)d;
		break;

	case OP_LHA:
		regs->gpr[rd] = ~0UL;
		break;

	case OP_LHAU:
		regs->gpr[rd] = ~0UL;
		regs->gpr[ra] += (s16)d;
		break;

	default:
		return 0;
	}

	return 1;
}

static int is_in_pci_mem_space(phys_addr_t addr)
{
	struct pci_controller *hose;
	struct resource *res;
	int i;

	list_for_each_entry(hose, &hose_list, list_node) {
		if (!(hose->indirect_type & PPC_INDIRECT_TYPE_EXT_REG))
			continue;

		for (i = 0; i < 3; i++) {
			res = &hose->mem_resources[i];
			if ((res->flags & IORESOURCE_MEM) &&
				addr >= res->start && addr <= res->end)
				return 1;
		}
	}
	return 0;
}

int fsl_pci_mcheck_exception(struct pt_regs *regs)
{
	u32 inst;
	int ret;
	phys_addr_t addr = 0;

	/* Let KVM/QEMU deal with the exception */
	if (regs->msr & MSR_GS)
		return 0;

#ifdef CONFIG_PHYS_64BIT
	addr = mfspr(SPRN_MCARU);
	addr <<= 32;
#endif
	addr += mfspr(SPRN_MCAR);

	if (is_in_pci_mem_space(addr)) {
		if (user_mode(regs)) {
			pagefault_disable();
			ret = get_user(regs->nip, &inst);
			pagefault_enable();
		} else {
			ret = probe_kernel_address((void *)regs->nip, inst);
		}

		if (!ret && mcheck_handle_load(regs, inst)) {
			regs->nip += 4;
			return 1;
		}
	}

	return 0;
}
#endif

#if defined(CONFIG_FSL_SOC_BOOKE) || defined(CONFIG_PPC_86xx)
static const struct of_device_id pci_ids[] = {
	{ .compatible = "fsl,mpc8540-pci", },
	{ .compatible = "fsl,mpc8548-pcie", },
	{ .compatible = "fsl,mpc8610-pci", },
	{ .compatible = "fsl,mpc8641-pcie", },
	{ .compatible = "fsl,qoriq-pcie", },
	{ .compatible = "fsl,qoriq-pcie-v2.1", },
	{ .compatible = "fsl,qoriq-pcie-v2.2", },
	{ .compatible = "fsl,qoriq-pcie-v2.3", },
	{ .compatible = "fsl,qoriq-pcie-v2.4", },
	{ .compatible = "fsl,qoriq-pcie-v3.0", },

	/*
	 * The following entries are for compatibility with older device
	 * trees.
	 */
	{ .compatible = "fsl,p1022-pcie", },
	{ .compatible = "fsl,p4080-pcie", },

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

#ifdef CONFIG_PM_SLEEP
static irqreturn_t fsl_pci_pme_handle(int irq, void *dev_id)
{
	struct pci_controller *hose = dev_id;
	struct ccsr_pci __iomem *pci = hose->private_data;
	u32 dr;

	dr = in_be32(&pci->pex_pme_mes_dr);
	if (!dr)
		return IRQ_NONE;

	out_be32(&pci->pex_pme_mes_dr, dr);

	return IRQ_HANDLED;
}

static int fsl_pci_pme_probe(struct pci_controller *hose)
{
	struct ccsr_pci __iomem *pci;
	struct pci_dev *dev;
	int pme_irq;
	int res;
	u16 pms;

	/* Get hose's pci_dev */
	dev = list_first_entry(&hose->bus->devices, typeof(*dev), bus_list);

	/* PME Disable */
	pci_read_config_word(dev, dev->pm_cap + PCI_PM_CTRL, &pms);
	pms &= ~PCI_PM_CTRL_PME_ENABLE;
	pci_write_config_word(dev, dev->pm_cap + PCI_PM_CTRL, pms);

	pme_irq = irq_of_parse_and_map(hose->dn, 0);
	if (!pme_irq) {
		dev_err(&dev->dev, "Failed to map PME interrupt.\n");

		return -ENXIO;
	}

	res = devm_request_irq(hose->parent, pme_irq,
			fsl_pci_pme_handle,
			IRQF_SHARED,
			"[PCI] PME", hose);
	if (res < 0) {
		dev_err(&dev->dev, "Unable to request irq %d for PME\n", pme_irq);
		irq_dispose_mapping(pme_irq);

		return -ENODEV;
	}

	pci = hose->private_data;

	/* Enable PTOD, ENL23D & EXL23D */
	clrbits32(&pci->pex_pme_mes_disr,
		  PME_DISR_EN_PTOD | PME_DISR_EN_ENL23D | PME_DISR_EN_EXL23D);

	out_be32(&pci->pex_pme_mes_ier, 0);
	setbits32(&pci->pex_pme_mes_ier,
		  PME_DISR_EN_PTOD | PME_DISR_EN_ENL23D | PME_DISR_EN_EXL23D);

	/* PME Enable */
	pci_read_config_word(dev, dev->pm_cap + PCI_PM_CTRL, &pms);
	pms |= PCI_PM_CTRL_PME_ENABLE;
	pci_write_config_word(dev, dev->pm_cap + PCI_PM_CTRL, pms);

	return 0;
}

static void send_pme_turnoff_message(struct pci_controller *hose)
{
	struct ccsr_pci __iomem *pci = hose->private_data;
	u32 dr;
	int i;

	/* Send PME_Turn_Off Message Request */
	setbits32(&pci->pex_pmcr, PEX_PMCR_PTOMR);

	/* Wait trun off done */
	for (i = 0; i < 150; i++) {
		dr = in_be32(&pci->pex_pme_mes_dr);
		if (dr) {
			out_be32(&pci->pex_pme_mes_dr, dr);
			break;
		}

		udelay(1000);
	}
}

static void fsl_pci_syscore_do_suspend(struct pci_controller *hose)
{
	send_pme_turnoff_message(hose);
}

static int fsl_pci_syscore_suspend(void)
{
	struct pci_controller *hose, *tmp;

	list_for_each_entry_safe(hose, tmp, &hose_list, list_node)
		fsl_pci_syscore_do_suspend(hose);

	return 0;
}

static void fsl_pci_syscore_do_resume(struct pci_controller *hose)
{
	struct ccsr_pci __iomem *pci = hose->private_data;
	u32 dr;
	int i;

	/* Send Exit L2 State Message */
	setbits32(&pci->pex_pmcr, PEX_PMCR_EXL2S);

	/* Wait exit done */
	for (i = 0; i < 150; i++) {
		dr = in_be32(&pci->pex_pme_mes_dr);
		if (dr) {
			out_be32(&pci->pex_pme_mes_dr, dr);
			break;
		}

		udelay(1000);
	}

	setup_pci_atmu(hose);
}

static void fsl_pci_syscore_resume(void)
{
	struct pci_controller *hose, *tmp;

	list_for_each_entry_safe(hose, tmp, &hose_list, list_node)
		fsl_pci_syscore_do_resume(hose);
}

static struct syscore_ops pci_syscore_pm_ops = {
	.suspend = fsl_pci_syscore_suspend,
	.resume = fsl_pci_syscore_resume,
};
#endif

void fsl_pcibios_fixup_phb(struct pci_controller *phb)
{
#ifdef CONFIG_PM_SLEEP
	fsl_pci_pme_probe(phb);
#endif
}

static int add_err_dev(struct platform_device *pdev)
{
	struct platform_device *errdev;
	struct mpc85xx_edac_pci_plat_data pd = {
		.of_node = pdev->dev.of_node
	};

	errdev = platform_device_register_resndata(&pdev->dev,
						   "mpc85xx-pci-edac",
						   PLATFORM_DEVID_AUTO,
						   pdev->resource,
						   pdev->num_resources,
						   &pd, sizeof(pd));
	if (IS_ERR(errdev))
		return PTR_ERR(errdev);

	return 0;
}

static int fsl_pci_probe(struct platform_device *pdev)
{
	struct device_node *node;
	int ret;

	node = pdev->dev.of_node;
	ret = fsl_add_bridge(pdev, fsl_pci_primary == node);
	if (ret)
		return ret;

	ret = add_err_dev(pdev);
	if (ret)
		dev_err(&pdev->dev, "couldn't register error device: %d\n",
			ret);

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
#ifdef CONFIG_PM_SLEEP
	register_syscore_ops(&pci_syscore_pm_ops);
#endif
	return platform_driver_register(&fsl_pci_driver);
}
arch_initcall(fsl_pci_init);
#endif
