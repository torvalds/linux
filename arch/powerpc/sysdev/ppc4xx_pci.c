/*
 * PCI / PCI-X / PCI-Express support for 4xx parts
 *
 * Copyright 2007 Ben. Herrenschmidt <benh@kernel.crashing.org>, IBM Corp.
 *
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/of.h>

#include <asm/io.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>

#include "ppc4xx_pci.h"

static int dma_offset_set;

/* Move that to a useable header */
extern unsigned long total_memory;

static int __init ppc4xx_parse_dma_ranges(struct pci_controller *hose,
					  void __iomem *reg,
					  struct resource *res)
{
	u64 size;
	const u32 *ranges;
	int rlen;
	int pna = of_n_addr_cells(hose->dn);
	int np = pna + 5;

	/* Default */
	res->start = 0;
	res->end = size = 0x80000000;
	res->flags = IORESOURCE_MEM | IORESOURCE_PREFETCH;

	/* Get dma-ranges property */
	ranges = of_get_property(hose->dn, "dma-ranges", &rlen);
	if (ranges == NULL)
		goto out;

	/* Walk it */
	while ((rlen -= np * 4) >= 0) {
		u32 pci_space = ranges[0];
		u64 pci_addr = of_read_number(ranges + 1, 2);
		u64 cpu_addr = of_translate_dma_address(hose->dn, ranges + 3);
		size = of_read_number(ranges + pna + 3, 2);
		ranges += np;
		if (cpu_addr == OF_BAD_ADDR || size == 0)
			continue;

		/* We only care about memory */
		if ((pci_space & 0x03000000) != 0x02000000)
			continue;

		/* We currently only support memory at 0, and pci_addr
		 * within 32 bits space
		 */
		if (cpu_addr != 0 || pci_addr > 0xffffffff) {
			printk(KERN_WARNING "%s: Ignored unsupported dma range"
			       " 0x%016llx...0x%016llx -> 0x%016llx\n",
			       hose->dn->full_name,
			       pci_addr, pci_addr + size - 1, cpu_addr);
			continue;
		}

		/* Check if not prefetchable */
		if (!(pci_space & 0x40000000))
			res->flags &= ~IORESOURCE_PREFETCH;


		/* Use that */
		res->start = pci_addr;
#ifndef CONFIG_RESOURCES_64BIT
		/* Beware of 32 bits resources */
		if ((pci_addr + size) > 0x100000000ull)
			res->end = 0xffffffff;
		else
#endif
			res->end = res->start + size - 1;
		break;
	}

	/* We only support one global DMA offset */
	if (dma_offset_set && pci_dram_offset != res->start) {
		printk(KERN_ERR "%s: dma-ranges(s) mismatch\n",
		       hose->dn->full_name);
		return -ENXIO;
	}

	/* Check that we can fit all of memory as we don't support
	 * DMA bounce buffers
	 */
	if (size < total_memory) {
		printk(KERN_ERR "%s: dma-ranges too small "
		       "(size=%llx total_memory=%lx)\n",
		       hose->dn->full_name, size, total_memory);
		return -ENXIO;
	}

	/* Check we are a power of 2 size and that base is a multiple of size*/
	if (!is_power_of_2(size) ||
	    (res->start & (size - 1)) != 0) {
		printk(KERN_ERR "%s: dma-ranges unaligned\n",
		       hose->dn->full_name);
		return -ENXIO;
	}

	/* Check that we are fully contained within 32 bits space */
	if (res->end > 0xffffffff) {
		printk(KERN_ERR "%s: dma-ranges outside of 32 bits space\n",
		       hose->dn->full_name);
		return -ENXIO;
	}
 out:
	dma_offset_set = 1;
	pci_dram_offset = res->start;

	printk(KERN_INFO "4xx PCI DMA offset set to 0x%08lx\n",
	       pci_dram_offset);
	return 0;
}

/*
 * 4xx PCI 2.x part
 */
static void __init ppc4xx_probe_pci_bridge(struct device_node *np)
{
	/* NYI */
}

/*
 * 4xx PCI-X part
 */

static void __init ppc4xx_configure_pcix_POMs(struct pci_controller *hose,
					      void __iomem *reg)
{
	u32 lah, lal, pciah, pcial, sa;
	int i, j;

	/* Setup outbound memory windows */
	for (i = j = 0; i < 3; i++) {
		struct resource *res = &hose->mem_resources[i];

		/* we only care about memory windows */
		if (!(res->flags & IORESOURCE_MEM))
			continue;
		if (j > 1) {
			printk(KERN_WARNING "%s: Too many ranges\n",
			       hose->dn->full_name);
			break;
		}

		/* Calculate register values */
#ifdef CONFIG_PTE_64BIT
		lah = res->start >> 32;
		lal = res->start & 0xffffffffu;
		pciah = (res->start - hose->pci_mem_offset) >> 32;
		pcial = (res->start - hose->pci_mem_offset) & 0xffffffffu;
#else
		lah = pciah = 0;
		lal = res->start;
		pcial = res->start - hose->pci_mem_offset;
#endif
		sa = res->end + 1 - res->start;
		if (!is_power_of_2(sa) || sa < 0x100000 ||
		    sa > 0xffffffffu) {
			printk(KERN_WARNING "%s: Resource out of range\n",
			       hose->dn->full_name);
			continue;
		}
		sa = (0xffffffffu << ilog2(sa)) | 0x1;

		/* Program register values */
		if (j == 0) {
			writel(lah, reg + PCIX0_POM0LAH);
			writel(lal, reg + PCIX0_POM0LAL);
			writel(pciah, reg + PCIX0_POM0PCIAH);
			writel(pcial, reg + PCIX0_POM0PCIAL);
			writel(sa, reg + PCIX0_POM0SA);
		} else {
			writel(lah, reg + PCIX0_POM1LAH);
			writel(lal, reg + PCIX0_POM1LAL);
			writel(pciah, reg + PCIX0_POM1PCIAH);
			writel(pcial, reg + PCIX0_POM1PCIAL);
			writel(sa, reg + PCIX0_POM1SA);
		}
		j++;
	}
}

static void __init ppc4xx_configure_pcix_PIMs(struct pci_controller *hose,
					      void __iomem *reg,
					      const struct resource *res,
					      int big_pim,
					      int enable_msi_hole)
{
	resource_size_t size = res->end - res->start + 1;
	u32 sa;

	/* RAM is always at 0 */
	writel(0x00000000, reg + PCIX0_PIM0LAH);
	writel(0x00000000, reg + PCIX0_PIM0LAL);

	/* Calculate window size */
	sa = (0xffffffffu << ilog2(size)) | 1;
	sa |= 0x1;
	if (res->flags & IORESOURCE_PREFETCH)
		sa |= 0x2;
	if (enable_msi_hole)
		sa |= 0x4;
	writel(sa, reg + PCIX0_PIM0SA);
	if (big_pim)
		writel(0xffffffff, reg + PCIX0_PIM0SAH);

	/* Map on PCI side */
	writel(0x00000000, reg + PCIX0_BAR0H);
	writel(res->start, reg + PCIX0_BAR0L);
	writew(0x0006, reg + PCIX0_COMMAND);
}

static void __init ppc4xx_probe_pcix_bridge(struct device_node *np)
{
	struct resource rsrc_cfg;
	struct resource rsrc_reg;
	struct resource dma_window;
	struct pci_controller *hose = NULL;
	void __iomem *reg = NULL;
	const int *bus_range;
	int big_pim = 0, msi = 0, primary = 0;

	/* Fetch config space registers address */
	if (of_address_to_resource(np, 0, &rsrc_cfg)) {
		printk(KERN_ERR "%s:Can't get PCI-X config register base !",
		       np->full_name);
		return;
	}
	/* Fetch host bridge internal registers address */
	if (of_address_to_resource(np, 3, &rsrc_reg)) {
		printk(KERN_ERR "%s: Can't get PCI-X internal register base !",
		       np->full_name);
		return;
	}

	/* Check if it supports large PIMs (440GX) */
	if (of_get_property(np, "large-inbound-windows", NULL))
		big_pim = 1;

	/* Check if we should enable MSIs inbound hole */
	if (of_get_property(np, "enable-msi-hole", NULL))
		msi = 1;

	/* Check if primary bridge */
	if (of_get_property(np, "primary", NULL))
		primary = 1;

	/* Get bus range if any */
	bus_range = of_get_property(np, "bus-range", NULL);

	/* Map registers */
	reg = ioremap(rsrc_reg.start, rsrc_reg.end + 1 - rsrc_reg.start);
	if (reg == NULL) {
		printk(KERN_ERR "%s: Can't map registers !", np->full_name);
		goto fail;
	}

	/* Allocate the host controller data structure */
	hose = pcibios_alloc_controller(np);
	if (!hose)
		goto fail;

	hose->first_busno = bus_range ? bus_range[0] : 0x0;
	hose->last_busno = bus_range ? bus_range[1] : 0xff;

	/* Setup config space */
	setup_indirect_pci(hose, rsrc_cfg.start, rsrc_cfg.start + 0x4, 0);

	/* Disable all windows */
	writel(0, reg + PCIX0_POM0SA);
	writel(0, reg + PCIX0_POM1SA);
	writel(0, reg + PCIX0_POM2SA);
	writel(0, reg + PCIX0_PIM0SA);
	writel(0, reg + PCIX0_PIM1SA);
	writel(0, reg + PCIX0_PIM2SA);
	if (big_pim) {
		writel(0, reg + PCIX0_PIM0SAH);
		writel(0, reg + PCIX0_PIM2SAH);
	}

	/* Parse outbound mapping resources */
	pci_process_bridge_OF_ranges(hose, np, primary);

	/* Parse inbound mapping resources */
	if (ppc4xx_parse_dma_ranges(hose, reg, &dma_window) != 0)
		goto fail;

	/* Configure outbound ranges POMs */
	ppc4xx_configure_pcix_POMs(hose, reg);

	/* Configure inbound ranges PIMs */
	ppc4xx_configure_pcix_PIMs(hose, reg, &dma_window, big_pim, msi);

	/* We don't need the registers anymore */
	iounmap(reg);
	return;

 fail:
	if (hose)
		pcibios_free_controller(hose);
	if (reg)
		iounmap(reg);
}

/*
 * 4xx PCI-Express part
 */
static void __init ppc4xx_probe_pciex_bridge(struct device_node *np)
{
	/* NYI */
}

static int __init ppc4xx_pci_find_bridges(void)
{
	struct device_node *np;

	for_each_compatible_node(np, NULL, "ibm,plb-pciex")
		ppc4xx_probe_pciex_bridge(np);
	for_each_compatible_node(np, NULL, "ibm,plb-pcix")
		ppc4xx_probe_pcix_bridge(np);
	for_each_compatible_node(np, NULL, "ibm,plb-pci")
		ppc4xx_probe_pci_bridge(np);

	return 0;
}
arch_initcall(ppc4xx_pci_find_bridges);

