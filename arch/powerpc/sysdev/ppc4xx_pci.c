/*
 * PCI / PCI-X / PCI-Express support for 4xx parts
 *
 * Copyright 2007 Ben. Herrenschmidt <benh@kernel.crashing.org>, IBM Corp.
 *
 * Most PCI Express code is coming from Stefan Roese implementation for
 * arch/ppc in the Denx tree, slightly reworked by me.
 *
 * Copyright 2007 DENX Software Engineering, Stefan Roese <sr@denx.de>
 *
 * Some of that comes itself from a previous implementation for 440SPE only
 * by Roland Dreier:
 *
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
 * Roland Dreier <rolandd@cisco.com>
 *
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/bootmem.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/dcr.h>
#include <asm/dcr-regs.h>

#include "ppc4xx_pci.h"

static int dma_offset_set;

/* Move that to a useable header */
extern unsigned long total_memory;

#define U64_TO_U32_LOW(val)	((u32)((val) & 0x00000000ffffffffULL))
#define U64_TO_U32_HIGH(val)	((u32)((val) >> 32))

#define RES_TO_U32_LOW(val)	\
	((sizeof(resource_size_t) > sizeof(u32)) ? U64_TO_U32_LOW(val) : (val))
#define RES_TO_U32_HIGH(val)	\
	((sizeof(resource_size_t) > sizeof(u32)) ? U64_TO_U32_HIGH(val) : (0))

static inline int ppc440spe_revA(void)
{
	/* Catch both 440SPe variants, with and without RAID6 support */
        if ((mfspr(SPRN_PVR) & 0xffefffff) == 0x53421890)
                return 1;
        else
                return 0;
}

static void fixup_ppc4xx_pci_bridge(struct pci_dev *dev)
{
	struct pci_controller *hose;
	int i;

	if (dev->devfn != 0 || dev->bus->self != NULL)
		return;

	hose = pci_bus_to_host(dev->bus);
	if (hose == NULL)
		return;

	if (!of_device_is_compatible(hose->dn, "ibm,plb-pciex") &&
	    !of_device_is_compatible(hose->dn, "ibm,plb-pcix") &&
	    !of_device_is_compatible(hose->dn, "ibm,plb-pci"))
		return;

	if (of_device_is_compatible(hose->dn, "ibm,plb440epx-pci") ||
		of_device_is_compatible(hose->dn, "ibm,plb440grx-pci")) {
		hose->indirect_type |= PPC_INDIRECT_TYPE_BROKEN_MRM;
	}

	/* Hide the PCI host BARs from the kernel as their content doesn't
	 * fit well in the resource management
	 */
	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		dev->resource[i].start = dev->resource[i].end = 0;
		dev->resource[i].flags = 0;
	}

	printk(KERN_INFO "PCI: Hiding 4xx host bridge resources %s\n",
	       pci_name(dev));
}
DECLARE_PCI_FIXUP_HEADER(PCI_ANY_ID, PCI_ANY_ID, fixup_ppc4xx_pci_bridge);

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
		/* Beware of 32 bits resources */
		if (sizeof(resource_size_t) == sizeof(u32) &&
		    (pci_addr + size) > 0x100000000ull)
			res->end = 0xffffffff;
		else
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

static void __init ppc4xx_configure_pci_PMMs(struct pci_controller *hose,
					     void __iomem *reg)
{
	u32 la, ma, pcila, pciha;
	int i, j;

	/* Setup outbound memory windows */
	for (i = j = 0; i < 3; i++) {
		struct resource *res = &hose->mem_resources[i];

		/* we only care about memory windows */
		if (!(res->flags & IORESOURCE_MEM))
			continue;
		if (j > 2) {
			printk(KERN_WARNING "%s: Too many ranges\n",
			       hose->dn->full_name);
			break;
		}

		/* Calculate register values */
		la = res->start;
		pciha = RES_TO_U32_HIGH(res->start - hose->pci_mem_offset);
		pcila = RES_TO_U32_LOW(res->start - hose->pci_mem_offset);

		ma = res->end + 1 - res->start;
		if (!is_power_of_2(ma) || ma < 0x1000 || ma > 0xffffffffu) {
			printk(KERN_WARNING "%s: Resource out of range\n",
			       hose->dn->full_name);
			continue;
		}
		ma = (0xffffffffu << ilog2(ma)) | 0x1;
		if (res->flags & IORESOURCE_PREFETCH)
			ma |= 0x2;

		/* Program register values */
		writel(la, reg + PCIL0_PMM0LA + (0x10 * j));
		writel(pcila, reg + PCIL0_PMM0PCILA + (0x10 * j));
		writel(pciha, reg + PCIL0_PMM0PCIHA + (0x10 * j));
		writel(ma, reg + PCIL0_PMM0MA + (0x10 * j));
		j++;
	}
}

static void __init ppc4xx_configure_pci_PTMs(struct pci_controller *hose,
					     void __iomem *reg,
					     const struct resource *res)
{
	resource_size_t size = res->end - res->start + 1;
	u32 sa;

	/* Calculate window size */
	sa = (0xffffffffu << ilog2(size)) | 1;
	sa |= 0x1;

	/* RAM is always at 0 local for now */
	writel(0, reg + PCIL0_PTM1LA);
	writel(sa, reg + PCIL0_PTM1MS);

	/* Map on PCI side */
	early_write_config_dword(hose, hose->first_busno, 0,
				 PCI_BASE_ADDRESS_1, res->start);
	early_write_config_dword(hose, hose->first_busno, 0,
				 PCI_BASE_ADDRESS_2, 0x00000000);
	early_write_config_word(hose, hose->first_busno, 0,
				PCI_COMMAND, 0x0006);
}

static void __init ppc4xx_probe_pci_bridge(struct device_node *np)
{
	/* NYI */
	struct resource rsrc_cfg;
	struct resource rsrc_reg;
	struct resource dma_window;
	struct pci_controller *hose = NULL;
	void __iomem *reg = NULL;
	const int *bus_range;
	int primary = 0;

	/* Fetch config space registers address */
	if (of_address_to_resource(np, 0, &rsrc_cfg)) {
		printk(KERN_ERR "%s:Can't get PCI config register base !",
		       np->full_name);
		return;
	}
	/* Fetch host bridge internal registers address */
	if (of_address_to_resource(np, 3, &rsrc_reg)) {
		printk(KERN_ERR "%s: Can't get PCI internal register base !",
		       np->full_name);
		return;
	}

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
	writel(0, reg + PCIL0_PMM0MA);
	writel(0, reg + PCIL0_PMM1MA);
	writel(0, reg + PCIL0_PMM2MA);
	writel(0, reg + PCIL0_PTM1MS);
	writel(0, reg + PCIL0_PTM2MS);

	/* Parse outbound mapping resources */
	pci_process_bridge_OF_ranges(hose, np, primary);

	/* Parse inbound mapping resources */
	if (ppc4xx_parse_dma_ranges(hose, reg, &dma_window) != 0)
		goto fail;

	/* Configure outbound ranges POMs */
	ppc4xx_configure_pci_PMMs(hose, reg);

	/* Configure inbound ranges PIMs */
	ppc4xx_configure_pci_PTMs(hose, reg, &dma_window);

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
		lah = RES_TO_U32_HIGH(res->start);
		lal = RES_TO_U32_LOW(res->start);
		pciah = RES_TO_U32_HIGH(res->start - hose->pci_mem_offset);
		pcial = RES_TO_U32_LOW(res->start - hose->pci_mem_offset);
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

#ifdef CONFIG_PPC4xx_PCI_EXPRESS

/*
 * 4xx PCI-Express part
 *
 * We support 3 parts currently based on the compatible property:
 *
 * ibm,plb-pciex-440spe
 * ibm,plb-pciex-405ex
 * ibm,plb-pciex-460ex
 *
 * Anything else will be rejected for now as they are all subtly
 * different unfortunately.
 *
 */

#define MAX_PCIE_BUS_MAPPED	0x40

struct ppc4xx_pciex_port
{
	struct pci_controller	*hose;
	struct device_node	*node;
	unsigned int		index;
	int			endpoint;
	int			link;
	int			has_ibpre;
	unsigned int		sdr_base;
	dcr_host_t		dcrs;
	struct resource		cfg_space;
	struct resource		utl_regs;
	void __iomem		*utl_base;
};

static struct ppc4xx_pciex_port *ppc4xx_pciex_ports;
static unsigned int ppc4xx_pciex_port_count;

struct ppc4xx_pciex_hwops
{
	int (*core_init)(struct device_node *np);
	int (*port_init_hw)(struct ppc4xx_pciex_port *port);
	int (*setup_utl)(struct ppc4xx_pciex_port *port);
};

static struct ppc4xx_pciex_hwops *ppc4xx_pciex_hwops;

#ifdef CONFIG_44x

/* Check various reset bits of the 440SPe PCIe core */
static int __init ppc440spe_pciex_check_reset(struct device_node *np)
{
	u32 valPE0, valPE1, valPE2;
	int err = 0;

	/* SDR0_PEGPLLLCT1 reset */
	if (!(mfdcri(SDR0, PESDR0_PLLLCT1) & 0x01000000)) {
		/*
		 * the PCIe core was probably already initialised
		 * by firmware - let's re-reset RCSSET regs
		 *
		 * -- Shouldn't we also re-reset the whole thing ? -- BenH
		 */
		pr_debug("PCIE: SDR0_PLLLCT1 already reset.\n");
		mtdcri(SDR0, PESDR0_440SPE_RCSSET, 0x01010000);
		mtdcri(SDR0, PESDR1_440SPE_RCSSET, 0x01010000);
		mtdcri(SDR0, PESDR2_440SPE_RCSSET, 0x01010000);
	}

	valPE0 = mfdcri(SDR0, PESDR0_440SPE_RCSSET);
	valPE1 = mfdcri(SDR0, PESDR1_440SPE_RCSSET);
	valPE2 = mfdcri(SDR0, PESDR2_440SPE_RCSSET);

	/* SDR0_PExRCSSET rstgu */
	if (!(valPE0 & 0x01000000) ||
	    !(valPE1 & 0x01000000) ||
	    !(valPE2 & 0x01000000)) {
		printk(KERN_INFO "PCIE: SDR0_PExRCSSET rstgu error\n");
		err = -1;
	}

	/* SDR0_PExRCSSET rstdl */
	if (!(valPE0 & 0x00010000) ||
	    !(valPE1 & 0x00010000) ||
	    !(valPE2 & 0x00010000)) {
		printk(KERN_INFO "PCIE: SDR0_PExRCSSET rstdl error\n");
		err = -1;
	}

	/* SDR0_PExRCSSET rstpyn */
	if ((valPE0 & 0x00001000) ||
	    (valPE1 & 0x00001000) ||
	    (valPE2 & 0x00001000)) {
		printk(KERN_INFO "PCIE: SDR0_PExRCSSET rstpyn error\n");
		err = -1;
	}

	/* SDR0_PExRCSSET hldplb */
	if ((valPE0 & 0x10000000) ||
	    (valPE1 & 0x10000000) ||
	    (valPE2 & 0x10000000)) {
		printk(KERN_INFO "PCIE: SDR0_PExRCSSET hldplb error\n");
		err = -1;
	}

	/* SDR0_PExRCSSET rdy */
	if ((valPE0 & 0x00100000) ||
	    (valPE1 & 0x00100000) ||
	    (valPE2 & 0x00100000)) {
		printk(KERN_INFO "PCIE: SDR0_PExRCSSET rdy error\n");
		err = -1;
	}

	/* SDR0_PExRCSSET shutdown */
	if ((valPE0 & 0x00000100) ||
	    (valPE1 & 0x00000100) ||
	    (valPE2 & 0x00000100)) {
		printk(KERN_INFO "PCIE: SDR0_PExRCSSET shutdown error\n");
		err = -1;
	}

	return err;
}

/* Global PCIe core initializations for 440SPe core */
static int __init ppc440spe_pciex_core_init(struct device_node *np)
{
	int time_out = 20;

	/* Set PLL clock receiver to LVPECL */
	dcri_clrset(SDR0, PESDR0_PLLLCT1, 0, 1 << 28);

	/* Shouldn't we do all the calibration stuff etc... here ? */
	if (ppc440spe_pciex_check_reset(np))
		return -ENXIO;

	if (!(mfdcri(SDR0, PESDR0_PLLLCT2) & 0x10000)) {
		printk(KERN_INFO "PCIE: PESDR_PLLCT2 resistance calibration "
		       "failed (0x%08x)\n",
		       mfdcri(SDR0, PESDR0_PLLLCT2));
		return -1;
	}

	/* De-assert reset of PCIe PLL, wait for lock */
	dcri_clrset(SDR0, PESDR0_PLLLCT1, 1 << 24, 0);
	udelay(3);

	while (time_out) {
		if (!(mfdcri(SDR0, PESDR0_PLLLCT3) & 0x10000000)) {
			time_out--;
			udelay(1);
		} else
			break;
	}
	if (!time_out) {
		printk(KERN_INFO "PCIE: VCO output not locked\n");
		return -1;
	}

	pr_debug("PCIE initialization OK\n");

	return 3;
}

static int ppc440spe_pciex_init_port_hw(struct ppc4xx_pciex_port *port)
{
	u32 val = 1 << 24;

	if (port->endpoint)
		val = PTYPE_LEGACY_ENDPOINT << 20;
	else
		val = PTYPE_ROOT_PORT << 20;

	if (port->index == 0)
		val |= LNKW_X8 << 12;
	else
		val |= LNKW_X4 << 12;

	mtdcri(SDR0, port->sdr_base + PESDRn_DLPSET, val);
	mtdcri(SDR0, port->sdr_base + PESDRn_UTLSET1, 0x20222222);
	if (ppc440spe_revA())
		mtdcri(SDR0, port->sdr_base + PESDRn_UTLSET2, 0x11000000);
	mtdcri(SDR0, port->sdr_base + PESDRn_440SPE_HSSL0SET1, 0x35000000);
	mtdcri(SDR0, port->sdr_base + PESDRn_440SPE_HSSL1SET1, 0x35000000);
	mtdcri(SDR0, port->sdr_base + PESDRn_440SPE_HSSL2SET1, 0x35000000);
	mtdcri(SDR0, port->sdr_base + PESDRn_440SPE_HSSL3SET1, 0x35000000);
	if (port->index == 0) {
		mtdcri(SDR0, port->sdr_base + PESDRn_440SPE_HSSL4SET1,
		       0x35000000);
		mtdcri(SDR0, port->sdr_base + PESDRn_440SPE_HSSL5SET1,
		       0x35000000);
		mtdcri(SDR0, port->sdr_base + PESDRn_440SPE_HSSL6SET1,
		       0x35000000);
		mtdcri(SDR0, port->sdr_base + PESDRn_440SPE_HSSL7SET1,
		       0x35000000);
	}
	dcri_clrset(SDR0, port->sdr_base + PESDRn_RCSSET,
			(1 << 24) | (1 << 16), 1 << 12);

	return 0;
}

static int ppc440speA_pciex_init_port_hw(struct ppc4xx_pciex_port *port)
{
	return ppc440spe_pciex_init_port_hw(port);
}

static int ppc440speB_pciex_init_port_hw(struct ppc4xx_pciex_port *port)
{
	int rc = ppc440spe_pciex_init_port_hw(port);

	port->has_ibpre = 1;

	return rc;
}

static int ppc440speA_pciex_init_utl(struct ppc4xx_pciex_port *port)
{
	/* XXX Check what that value means... I hate magic */
	dcr_write(port->dcrs, DCRO_PEGPL_SPECIAL, 0x68782800);

	/*
	 * Set buffer allocations and then assert VRB and TXE.
	 */
	out_be32(port->utl_base + PEUTL_OUTTR,   0x08000000);
	out_be32(port->utl_base + PEUTL_INTR,    0x02000000);
	out_be32(port->utl_base + PEUTL_OPDBSZ,  0x10000000);
	out_be32(port->utl_base + PEUTL_PBBSZ,   0x53000000);
	out_be32(port->utl_base + PEUTL_IPHBSZ,  0x08000000);
	out_be32(port->utl_base + PEUTL_IPDBSZ,  0x10000000);
	out_be32(port->utl_base + PEUTL_RCIRQEN, 0x00f00000);
	out_be32(port->utl_base + PEUTL_PCTL,    0x80800066);

	return 0;
}

static int ppc440speB_pciex_init_utl(struct ppc4xx_pciex_port *port)
{
	/* Report CRS to the operating system */
	out_be32(port->utl_base + PEUTL_PBCTL,    0x08000000);

	return 0;
}

static struct ppc4xx_pciex_hwops ppc440speA_pcie_hwops __initdata =
{
	.core_init	= ppc440spe_pciex_core_init,
	.port_init_hw	= ppc440speA_pciex_init_port_hw,
	.setup_utl	= ppc440speA_pciex_init_utl,
};

static struct ppc4xx_pciex_hwops ppc440speB_pcie_hwops __initdata =
{
	.core_init	= ppc440spe_pciex_core_init,
	.port_init_hw	= ppc440speB_pciex_init_port_hw,
	.setup_utl	= ppc440speB_pciex_init_utl,
};

static int __init ppc460ex_pciex_core_init(struct device_node *np)
{
	/* Nothing to do, return 2 ports */
	return 2;
}

static int ppc460ex_pciex_init_port_hw(struct ppc4xx_pciex_port *port)
{
	u32 val;
	u32 utlset1;

	if (port->endpoint)
		val = PTYPE_LEGACY_ENDPOINT << 20;
	else
		val = PTYPE_ROOT_PORT << 20;

	if (port->index == 0) {
		val |= LNKW_X1 << 12;
		utlset1 = 0x20000000;
	} else {
		val |= LNKW_X4 << 12;
		utlset1 = 0x20101101;
	}

	mtdcri(SDR0, port->sdr_base + PESDRn_DLPSET, val);
	mtdcri(SDR0, port->sdr_base + PESDRn_UTLSET1, utlset1);
	mtdcri(SDR0, port->sdr_base + PESDRn_UTLSET2, 0x01210000);

	switch (port->index) {
	case 0:
		mtdcri(SDR0, PESDR0_460EX_L0CDRCTL, 0x00003230);
		mtdcri(SDR0, PESDR0_460EX_L0DRV, 0x00000136);
		mtdcri(SDR0, PESDR0_460EX_L0CLK, 0x00000006);

		mtdcri(SDR0, PESDR0_460EX_PHY_CTL_RST,0x10000000);
		break;

	case 1:
		mtdcri(SDR0, PESDR1_460EX_L0CDRCTL, 0x00003230);
		mtdcri(SDR0, PESDR1_460EX_L1CDRCTL, 0x00003230);
		mtdcri(SDR0, PESDR1_460EX_L2CDRCTL, 0x00003230);
		mtdcri(SDR0, PESDR1_460EX_L3CDRCTL, 0x00003230);
		mtdcri(SDR0, PESDR1_460EX_L0DRV, 0x00000136);
		mtdcri(SDR0, PESDR1_460EX_L1DRV, 0x00000136);
		mtdcri(SDR0, PESDR1_460EX_L2DRV, 0x00000136);
		mtdcri(SDR0, PESDR1_460EX_L3DRV, 0x00000136);
		mtdcri(SDR0, PESDR1_460EX_L0CLK, 0x00000006);
		mtdcri(SDR0, PESDR1_460EX_L1CLK, 0x00000006);
		mtdcri(SDR0, PESDR1_460EX_L2CLK, 0x00000006);
		mtdcri(SDR0, PESDR1_460EX_L3CLK, 0x00000006);

		mtdcri(SDR0, PESDR1_460EX_PHY_CTL_RST,0x10000000);
		break;
	}

	mtdcri(SDR0, port->sdr_base + PESDRn_RCSSET,
	       mfdcri(SDR0, port->sdr_base + PESDRn_RCSSET) |
	       (PESDRx_RCSSET_RSTGU | PESDRx_RCSSET_RSTPYN));

	/* Poll for PHY reset */
	/* XXX FIXME add timeout */
	switch (port->index) {
	case 0:
		while (!(mfdcri(SDR0, PESDR0_460EX_RSTSTA) & 0x1))
			udelay(10);
		break;
	case 1:
		while (!(mfdcri(SDR0, PESDR1_460EX_RSTSTA) & 0x1))
			udelay(10);
		break;
	}

	mtdcri(SDR0, port->sdr_base + PESDRn_RCSSET,
	       (mfdcri(SDR0, port->sdr_base + PESDRn_RCSSET) &
		~(PESDRx_RCSSET_RSTGU | PESDRx_RCSSET_RSTDL)) |
	       PESDRx_RCSSET_RSTPYN);

	port->has_ibpre = 1;

	return 0;
}

static int ppc460ex_pciex_init_utl(struct ppc4xx_pciex_port *port)
{
	dcr_write(port->dcrs, DCRO_PEGPL_SPECIAL, 0x0);

	/*
	 * Set buffer allocations and then assert VRB and TXE.
	 */
	out_be32(port->utl_base + PEUTL_PBCTL,	0x0800000c);
	out_be32(port->utl_base + PEUTL_OUTTR,	0x08000000);
	out_be32(port->utl_base + PEUTL_INTR,	0x02000000);
	out_be32(port->utl_base + PEUTL_OPDBSZ,	0x04000000);
	out_be32(port->utl_base + PEUTL_PBBSZ,	0x00000000);
	out_be32(port->utl_base + PEUTL_IPHBSZ,	0x02000000);
	out_be32(port->utl_base + PEUTL_IPDBSZ,	0x04000000);
	out_be32(port->utl_base + PEUTL_RCIRQEN,0x00f00000);
	out_be32(port->utl_base + PEUTL_PCTL,	0x80800066);

	return 0;
}

static struct ppc4xx_pciex_hwops ppc460ex_pcie_hwops __initdata =
{
	.core_init	= ppc460ex_pciex_core_init,
	.port_init_hw	= ppc460ex_pciex_init_port_hw,
	.setup_utl	= ppc460ex_pciex_init_utl,
};

#endif /* CONFIG_44x */

#ifdef CONFIG_40x

static int __init ppc405ex_pciex_core_init(struct device_node *np)
{
	/* Nothing to do, return 2 ports */
	return 2;
}

static void ppc405ex_pcie_phy_reset(struct ppc4xx_pciex_port *port)
{
	/* Assert the PE0_PHY reset */
	mtdcri(SDR0, port->sdr_base + PESDRn_RCSSET, 0x01010000);
	msleep(1);

	/* deassert the PE0_hotreset */
	if (port->endpoint)
		mtdcri(SDR0, port->sdr_base + PESDRn_RCSSET, 0x01111000);
	else
		mtdcri(SDR0, port->sdr_base + PESDRn_RCSSET, 0x01101000);

	/* poll for phy !reset */
	/* XXX FIXME add timeout */
	while (!(mfdcri(SDR0, port->sdr_base + PESDRn_405EX_PHYSTA) & 0x00001000))
		;

	/* deassert the PE0_gpl_utl_reset */
	mtdcri(SDR0, port->sdr_base + PESDRn_RCSSET, 0x00101000);
}

static int ppc405ex_pciex_init_port_hw(struct ppc4xx_pciex_port *port)
{
	u32 val;

	if (port->endpoint)
		val = PTYPE_LEGACY_ENDPOINT;
	else
		val = PTYPE_ROOT_PORT;

	mtdcri(SDR0, port->sdr_base + PESDRn_DLPSET,
	       1 << 24 | val << 20 | LNKW_X1 << 12);

	mtdcri(SDR0, port->sdr_base + PESDRn_UTLSET1, 0x00000000);
	mtdcri(SDR0, port->sdr_base + PESDRn_UTLSET2, 0x01010000);
	mtdcri(SDR0, port->sdr_base + PESDRn_405EX_PHYSET1, 0x720F0000);
	mtdcri(SDR0, port->sdr_base + PESDRn_405EX_PHYSET2, 0x70600003);

	/*
	 * Only reset the PHY when no link is currently established.
	 * This is for the Atheros PCIe board which has problems to establish
	 * the link (again) after this PHY reset. All other currently tested
	 * PCIe boards don't show this problem.
	 * This has to be re-tested and fixed in a later release!
	 */
	val = mfdcri(SDR0, port->sdr_base + PESDRn_LOOP);
	if (!(val & 0x00001000))
		ppc405ex_pcie_phy_reset(port);

	dcr_write(port->dcrs, DCRO_PEGPL_CFG, 0x10000000);  /* guarded on */

	port->has_ibpre = 1;

	return 0;
}

static int ppc405ex_pciex_init_utl(struct ppc4xx_pciex_port *port)
{
	dcr_write(port->dcrs, DCRO_PEGPL_SPECIAL, 0x0);

	/*
	 * Set buffer allocations and then assert VRB and TXE.
	 */
	out_be32(port->utl_base + PEUTL_OUTTR,   0x02000000);
	out_be32(port->utl_base + PEUTL_INTR,    0x02000000);
	out_be32(port->utl_base + PEUTL_OPDBSZ,  0x04000000);
	out_be32(port->utl_base + PEUTL_PBBSZ,   0x21000000);
	out_be32(port->utl_base + PEUTL_IPHBSZ,  0x02000000);
	out_be32(port->utl_base + PEUTL_IPDBSZ,  0x04000000);
	out_be32(port->utl_base + PEUTL_RCIRQEN, 0x00f00000);
	out_be32(port->utl_base + PEUTL_PCTL,    0x80800066);

	out_be32(port->utl_base + PEUTL_PBCTL,   0x08000000);

	return 0;
}

static struct ppc4xx_pciex_hwops ppc405ex_pcie_hwops __initdata =
{
	.core_init	= ppc405ex_pciex_core_init,
	.port_init_hw	= ppc405ex_pciex_init_port_hw,
	.setup_utl	= ppc405ex_pciex_init_utl,
};

#endif /* CONFIG_40x */


/* Check that the core has been initied and if not, do it */
static int __init ppc4xx_pciex_check_core_init(struct device_node *np)
{
	static int core_init;
	int count = -ENODEV;

	if (core_init++)
		return 0;

#ifdef CONFIG_44x
	if (of_device_is_compatible(np, "ibm,plb-pciex-440spe")) {
		if (ppc440spe_revA())
			ppc4xx_pciex_hwops = &ppc440speA_pcie_hwops;
		else
			ppc4xx_pciex_hwops = &ppc440speB_pcie_hwops;
	}
	if (of_device_is_compatible(np, "ibm,plb-pciex-460ex"))
		ppc4xx_pciex_hwops = &ppc460ex_pcie_hwops;
#endif /* CONFIG_44x    */
#ifdef CONFIG_40x
	if (of_device_is_compatible(np, "ibm,plb-pciex-405ex"))
		ppc4xx_pciex_hwops = &ppc405ex_pcie_hwops;
#endif
	if (ppc4xx_pciex_hwops == NULL) {
		printk(KERN_WARNING "PCIE: unknown host type %s\n",
		       np->full_name);
		return -ENODEV;
	}

	count = ppc4xx_pciex_hwops->core_init(np);
	if (count > 0) {
		ppc4xx_pciex_ports =
		       kzalloc(count * sizeof(struct ppc4xx_pciex_port),
			       GFP_KERNEL);
		if (ppc4xx_pciex_ports) {
			ppc4xx_pciex_port_count = count;
			return 0;
		}
		printk(KERN_WARNING "PCIE: failed to allocate ports array\n");
		return -ENOMEM;
	}
	return -ENODEV;
}

static void __init ppc4xx_pciex_port_init_mapping(struct ppc4xx_pciex_port *port)
{
	/* We map PCI Express configuration based on the reg property */
	dcr_write(port->dcrs, DCRO_PEGPL_CFGBAH,
		  RES_TO_U32_HIGH(port->cfg_space.start));
	dcr_write(port->dcrs, DCRO_PEGPL_CFGBAL,
		  RES_TO_U32_LOW(port->cfg_space.start));

	/* XXX FIXME: Use size from reg property. For now, map 512M */
	dcr_write(port->dcrs, DCRO_PEGPL_CFGMSK, 0xe0000001);

	/* We map UTL registers based on the reg property */
	dcr_write(port->dcrs, DCRO_PEGPL_REGBAH,
		  RES_TO_U32_HIGH(port->utl_regs.start));
	dcr_write(port->dcrs, DCRO_PEGPL_REGBAL,
		  RES_TO_U32_LOW(port->utl_regs.start));

	/* XXX FIXME: Use size from reg property */
	dcr_write(port->dcrs, DCRO_PEGPL_REGMSK, 0x00007001);

	/* Disable all other outbound windows */
	dcr_write(port->dcrs, DCRO_PEGPL_OMR1MSKL, 0);
	dcr_write(port->dcrs, DCRO_PEGPL_OMR2MSKL, 0);
	dcr_write(port->dcrs, DCRO_PEGPL_OMR3MSKL, 0);
	dcr_write(port->dcrs, DCRO_PEGPL_MSGMSK, 0);
}

static int __init ppc4xx_pciex_wait_on_sdr(struct ppc4xx_pciex_port *port,
					   unsigned int sdr_offset,
					   unsigned int mask,
					   unsigned int value,
					   int timeout_ms)
{
	u32 val;

	while(timeout_ms--) {
		val = mfdcri(SDR0, port->sdr_base + sdr_offset);
		if ((val & mask) == value) {
			pr_debug("PCIE%d: Wait on SDR %x success with tm %d (%08x)\n",
				 port->index, sdr_offset, timeout_ms, val);
			return 0;
		}
		msleep(1);
	}
	return -1;
}

static int __init ppc4xx_pciex_port_init(struct ppc4xx_pciex_port *port)
{
	int rc = 0;

	/* Init HW */
	if (ppc4xx_pciex_hwops->port_init_hw)
		rc = ppc4xx_pciex_hwops->port_init_hw(port);
	if (rc != 0)
		return rc;

	printk(KERN_INFO "PCIE%d: Checking link...\n",
	       port->index);

	/* Wait for reset to complete */
	if (ppc4xx_pciex_wait_on_sdr(port, PESDRn_RCSSTS, 1 << 20, 0, 10)) {
		printk(KERN_WARNING "PCIE%d: PGRST failed\n",
		       port->index);
		return -1;
	}

	/* Check for card presence detect if supported, if not, just wait for
	 * link unconditionally.
	 *
	 * note that we don't fail if there is no link, we just filter out
	 * config space accesses. That way, it will be easier to implement
	 * hotplug later on.
	 */
	if (!port->has_ibpre ||
	    !ppc4xx_pciex_wait_on_sdr(port, PESDRn_LOOP,
				      1 << 28, 1 << 28, 100)) {
		printk(KERN_INFO
		       "PCIE%d: Device detected, waiting for link...\n",
		       port->index);
		if (ppc4xx_pciex_wait_on_sdr(port, PESDRn_LOOP,
					     0x1000, 0x1000, 2000))
			printk(KERN_WARNING
			       "PCIE%d: Link up failed\n", port->index);
		else {
			printk(KERN_INFO
			       "PCIE%d: link is up !\n", port->index);
			port->link = 1;
		}
	} else
		printk(KERN_INFO "PCIE%d: No device detected.\n", port->index);

	/*
	 * Initialize mapping: disable all regions and configure
	 * CFG and REG regions based on resources in the device tree
	 */
	ppc4xx_pciex_port_init_mapping(port);

	/*
	 * Map UTL
	 */
	port->utl_base = ioremap(port->utl_regs.start, 0x100);
	BUG_ON(port->utl_base == NULL);

	/*
	 * Setup UTL registers --BenH.
	 */
	if (ppc4xx_pciex_hwops->setup_utl)
		ppc4xx_pciex_hwops->setup_utl(port);

	/*
	 * Check for VC0 active and assert RDY.
	 */
	if (port->link &&
	    ppc4xx_pciex_wait_on_sdr(port, PESDRn_RCSSTS,
				     1 << 16, 1 << 16, 5000)) {
		printk(KERN_INFO "PCIE%d: VC0 not active\n", port->index);
		port->link = 0;
	}

	dcri_clrset(SDR0, port->sdr_base + PESDRn_RCSSET, 0, 1 << 20);
	msleep(100);

	return 0;
}

static int ppc4xx_pciex_validate_bdf(struct ppc4xx_pciex_port *port,
				     struct pci_bus *bus,
				     unsigned int devfn)
{
	static int message;

	/* Endpoint can not generate upstream(remote) config cycles */
	if (port->endpoint && bus->number != port->hose->first_busno)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* Check we are within the mapped range */
	if (bus->number > port->hose->last_busno) {
		if (!message) {
			printk(KERN_WARNING "Warning! Probing bus %u"
			       " out of range !\n", bus->number);
			message++;
		}
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	/* The root complex has only one device / function */
	if (bus->number == port->hose->first_busno && devfn != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* The other side of the RC has only one device as well */
	if (bus->number == (port->hose->first_busno + 1) &&
	    PCI_SLOT(devfn) != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* Check if we have a link */
	if ((bus->number != port->hose->first_busno) && !port->link)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return 0;
}

static void __iomem *ppc4xx_pciex_get_config_base(struct ppc4xx_pciex_port *port,
						  struct pci_bus *bus,
						  unsigned int devfn)
{
	int relbus;

	/* Remove the casts when we finally remove the stupid volatile
	 * in struct pci_controller
	 */
	if (bus->number == port->hose->first_busno)
		return (void __iomem *)port->hose->cfg_addr;

	relbus = bus->number - (port->hose->first_busno + 1);
	return (void __iomem *)port->hose->cfg_data +
		((relbus  << 20) | (devfn << 12));
}

static int ppc4xx_pciex_read_config(struct pci_bus *bus, unsigned int devfn,
				    int offset, int len, u32 *val)
{
	struct pci_controller *hose = (struct pci_controller *) bus->sysdata;
	struct ppc4xx_pciex_port *port =
		&ppc4xx_pciex_ports[hose->indirect_type];
	void __iomem *addr;
	u32 gpl_cfg;

	BUG_ON(hose != port->hose);

	if (ppc4xx_pciex_validate_bdf(port, bus, devfn) != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = ppc4xx_pciex_get_config_base(port, bus, devfn);

	/*
	 * Reading from configuration space of non-existing device can
	 * generate transaction errors. For the read duration we suppress
	 * assertion of machine check exceptions to avoid those.
	 */
	gpl_cfg = dcr_read(port->dcrs, DCRO_PEGPL_CFG);
	dcr_write(port->dcrs, DCRO_PEGPL_CFG, gpl_cfg | GPL_DMER_MASK_DISA);

	/* Make sure no CRS is recorded */
	out_be32(port->utl_base + PEUTL_RCSTA, 0x00040000);

	switch (len) {
	case 1:
		*val = in_8((u8 *)(addr + offset));
		break;
	case 2:
		*val = in_le16((u16 *)(addr + offset));
		break;
	default:
		*val = in_le32((u32 *)(addr + offset));
		break;
	}

	pr_debug("pcie-config-read: bus=%3d [%3d..%3d] devfn=0x%04x"
		 " offset=0x%04x len=%d, addr=0x%p val=0x%08x\n",
		 bus->number, hose->first_busno, hose->last_busno,
		 devfn, offset, len, addr + offset, *val);

	/* Check for CRS (440SPe rev B does that for us but heh ..) */
	if (in_be32(port->utl_base + PEUTL_RCSTA) & 0x00040000) {
		pr_debug("Got CRS !\n");
		if (len != 4 || offset != 0)
			return PCIBIOS_DEVICE_NOT_FOUND;
		*val = 0xffff0001;
	}

	dcr_write(port->dcrs, DCRO_PEGPL_CFG, gpl_cfg);

	return PCIBIOS_SUCCESSFUL;
}

static int ppc4xx_pciex_write_config(struct pci_bus *bus, unsigned int devfn,
				     int offset, int len, u32 val)
{
	struct pci_controller *hose = (struct pci_controller *) bus->sysdata;
	struct ppc4xx_pciex_port *port =
		&ppc4xx_pciex_ports[hose->indirect_type];
	void __iomem *addr;
	u32 gpl_cfg;

	if (ppc4xx_pciex_validate_bdf(port, bus, devfn) != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = ppc4xx_pciex_get_config_base(port, bus, devfn);

	/*
	 * Reading from configuration space of non-existing device can
	 * generate transaction errors. For the read duration we suppress
	 * assertion of machine check exceptions to avoid those.
	 */
	gpl_cfg = dcr_read(port->dcrs, DCRO_PEGPL_CFG);
	dcr_write(port->dcrs, DCRO_PEGPL_CFG, gpl_cfg | GPL_DMER_MASK_DISA);

	pr_debug("pcie-config-write: bus=%3d [%3d..%3d] devfn=0x%04x"
		 " offset=0x%04x len=%d, addr=0x%p val=0x%08x\n",
		 bus->number, hose->first_busno, hose->last_busno,
		 devfn, offset, len, addr + offset, val);

	switch (len) {
	case 1:
		out_8((u8 *)(addr + offset), val);
		break;
	case 2:
		out_le16((u16 *)(addr + offset), val);
		break;
	default:
		out_le32((u32 *)(addr + offset), val);
		break;
	}

	dcr_write(port->dcrs, DCRO_PEGPL_CFG, gpl_cfg);

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops ppc4xx_pciex_pci_ops =
{
	.read  = ppc4xx_pciex_read_config,
	.write = ppc4xx_pciex_write_config,
};

static void __init ppc4xx_configure_pciex_POMs(struct ppc4xx_pciex_port *port,
					       struct pci_controller *hose,
					       void __iomem *mbase)
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
			       port->node->full_name);
			break;
		}

		/* Calculate register values */
		lah = RES_TO_U32_HIGH(res->start);
		lal = RES_TO_U32_LOW(res->start);
		pciah = RES_TO_U32_HIGH(res->start - hose->pci_mem_offset);
		pcial = RES_TO_U32_LOW(res->start - hose->pci_mem_offset);
		sa = res->end + 1 - res->start;
		if (!is_power_of_2(sa) || sa < 0x100000 ||
		    sa > 0xffffffffu) {
			printk(KERN_WARNING "%s: Resource out of range\n",
			       port->node->full_name);
			continue;
		}
		sa = (0xffffffffu << ilog2(sa)) | 0x1;

		/* Program register values */
		switch (j) {
		case 0:
			out_le32(mbase + PECFG_POM0LAH, pciah);
			out_le32(mbase + PECFG_POM0LAL, pcial);
			dcr_write(port->dcrs, DCRO_PEGPL_OMR1BAH, lah);
			dcr_write(port->dcrs, DCRO_PEGPL_OMR1BAL, lal);
			dcr_write(port->dcrs, DCRO_PEGPL_OMR1MSKH, 0x7fffffff);
			dcr_write(port->dcrs, DCRO_PEGPL_OMR1MSKL, sa | 3);
			break;
		case 1:
			out_le32(mbase + PECFG_POM1LAH, pciah);
			out_le32(mbase + PECFG_POM1LAL, pcial);
			dcr_write(port->dcrs, DCRO_PEGPL_OMR2BAH, lah);
			dcr_write(port->dcrs, DCRO_PEGPL_OMR2BAL, lal);
			dcr_write(port->dcrs, DCRO_PEGPL_OMR2MSKH, 0x7fffffff);
			dcr_write(port->dcrs, DCRO_PEGPL_OMR2MSKL, sa | 3);
			break;
		}
		j++;
	}

	/* Configure IO, always 64K starting at 0 */
	if (hose->io_resource.flags & IORESOURCE_IO) {
		lah = RES_TO_U32_HIGH(hose->io_base_phys);
		lal = RES_TO_U32_LOW(hose->io_base_phys);
		out_le32(mbase + PECFG_POM2LAH, 0);
		out_le32(mbase + PECFG_POM2LAL, 0);
		dcr_write(port->dcrs, DCRO_PEGPL_OMR3BAH, lah);
		dcr_write(port->dcrs, DCRO_PEGPL_OMR3BAL, lal);
		dcr_write(port->dcrs, DCRO_PEGPL_OMR3MSKH, 0x7fffffff);
		dcr_write(port->dcrs, DCRO_PEGPL_OMR3MSKL, 0xffff0000 | 3);
	}
}

static void __init ppc4xx_configure_pciex_PIMs(struct ppc4xx_pciex_port *port,
					       struct pci_controller *hose,
					       void __iomem *mbase,
					       struct resource *res)
{
	resource_size_t size = res->end - res->start + 1;
	u64 sa;

	if (port->endpoint) {
		resource_size_t ep_addr = 0;
		resource_size_t ep_size = 32 << 20;

		/* Currently we map a fixed 64MByte window to PLB address
		 * 0 (SDRAM). This should probably be configurable via a dts
		 * property.
		 */

		/* Calculate window size */
		sa = (0xffffffffffffffffull << ilog2(ep_size));;

		/* Setup BAR0 */
		out_le32(mbase + PECFG_BAR0HMPA, RES_TO_U32_HIGH(sa));
		out_le32(mbase + PECFG_BAR0LMPA, RES_TO_U32_LOW(sa) |
			 PCI_BASE_ADDRESS_MEM_TYPE_64);

		/* Disable BAR1 & BAR2 */
		out_le32(mbase + PECFG_BAR1MPA, 0);
		out_le32(mbase + PECFG_BAR2HMPA, 0);
		out_le32(mbase + PECFG_BAR2LMPA, 0);

		out_le32(mbase + PECFG_PIM01SAH, RES_TO_U32_HIGH(sa));
		out_le32(mbase + PECFG_PIM01SAL, RES_TO_U32_LOW(sa));

		out_le32(mbase + PCI_BASE_ADDRESS_0, RES_TO_U32_LOW(ep_addr));
		out_le32(mbase + PCI_BASE_ADDRESS_1, RES_TO_U32_HIGH(ep_addr));
	} else {
		/* Calculate window size */
		sa = (0xffffffffffffffffull << ilog2(size));;
		if (res->flags & IORESOURCE_PREFETCH)
			sa |= 0x8;

		out_le32(mbase + PECFG_BAR0HMPA, RES_TO_U32_HIGH(sa));
		out_le32(mbase + PECFG_BAR0LMPA, RES_TO_U32_LOW(sa));

		/* The setup of the split looks weird to me ... let's see
		 * if it works
		 */
		out_le32(mbase + PECFG_PIM0LAL, 0x00000000);
		out_le32(mbase + PECFG_PIM0LAH, 0x00000000);
		out_le32(mbase + PECFG_PIM1LAL, 0x00000000);
		out_le32(mbase + PECFG_PIM1LAH, 0x00000000);
		out_le32(mbase + PECFG_PIM01SAH, 0xffff0000);
		out_le32(mbase + PECFG_PIM01SAL, 0x00000000);

		out_le32(mbase + PCI_BASE_ADDRESS_0, RES_TO_U32_LOW(res->start));
		out_le32(mbase + PCI_BASE_ADDRESS_1, RES_TO_U32_HIGH(res->start));
	}

	/* Enable inbound mapping */
	out_le32(mbase + PECFG_PIMEN, 0x1);

	/* Enable I/O, Mem, and Busmaster cycles */
	out_le16(mbase + PCI_COMMAND,
		 in_le16(mbase + PCI_COMMAND) |
		 PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
}

static void __init ppc4xx_pciex_port_setup_hose(struct ppc4xx_pciex_port *port)
{
	struct resource dma_window;
	struct pci_controller *hose = NULL;
	const int *bus_range;
	int primary = 0, busses;
	void __iomem *mbase = NULL, *cfg_data = NULL;
	const u32 *pval;
	u32 val;

	/* Check if primary bridge */
	if (of_get_property(port->node, "primary", NULL))
		primary = 1;

	/* Get bus range if any */
	bus_range = of_get_property(port->node, "bus-range", NULL);

	/* Allocate the host controller data structure */
	hose = pcibios_alloc_controller(port->node);
	if (!hose)
		goto fail;

	/* We stick the port number in "indirect_type" so the config space
	 * ops can retrieve the port data structure easily
	 */
	hose->indirect_type = port->index;

	/* Get bus range */
	hose->first_busno = bus_range ? bus_range[0] : 0x0;
	hose->last_busno = bus_range ? bus_range[1] : 0xff;

	/* Because of how big mapping the config space is (1M per bus), we
	 * limit how many busses we support. In the long run, we could replace
	 * that with something akin to kmap_atomic instead. We set aside 1 bus
	 * for the host itself too.
	 */
	busses = hose->last_busno - hose->first_busno; /* This is off by 1 */
	if (busses > MAX_PCIE_BUS_MAPPED) {
		busses = MAX_PCIE_BUS_MAPPED;
		hose->last_busno = hose->first_busno + busses;
	}

	if (!port->endpoint) {
		/* Only map the external config space in cfg_data for
		 * PCIe root-complexes. External space is 1M per bus
		 */
		cfg_data = ioremap(port->cfg_space.start +
				   (hose->first_busno + 1) * 0x100000,
				   busses * 0x100000);
		if (cfg_data == NULL) {
			printk(KERN_ERR "%s: Can't map external config space !",
			       port->node->full_name);
			goto fail;
		}
		hose->cfg_data = cfg_data;
	}

	/* Always map the host config space in cfg_addr.
	 * Internal space is 4K
	 */
	mbase = ioremap(port->cfg_space.start + 0x10000000, 0x1000);
	if (mbase == NULL) {
		printk(KERN_ERR "%s: Can't map internal config space !",
		       port->node->full_name);
		goto fail;
	}
	hose->cfg_addr = mbase;

	pr_debug("PCIE %s, bus %d..%d\n", port->node->full_name,
		 hose->first_busno, hose->last_busno);
	pr_debug("     config space mapped at: root @0x%p, other @0x%p\n",
		 hose->cfg_addr, hose->cfg_data);

	/* Setup config space */
	hose->ops = &ppc4xx_pciex_pci_ops;
	port->hose = hose;
	mbase = (void __iomem *)hose->cfg_addr;

	if (!port->endpoint) {
		/*
		 * Set bus numbers on our root port
		 */
		out_8(mbase + PCI_PRIMARY_BUS, hose->first_busno);
		out_8(mbase + PCI_SECONDARY_BUS, hose->first_busno + 1);
		out_8(mbase + PCI_SUBORDINATE_BUS, hose->last_busno);
	}

	/*
	 * OMRs are already reset, also disable PIMs
	 */
	out_le32(mbase + PECFG_PIMEN, 0);

	/* Parse outbound mapping resources */
	pci_process_bridge_OF_ranges(hose, port->node, primary);

	/* Parse inbound mapping resources */
	if (ppc4xx_parse_dma_ranges(hose, mbase, &dma_window) != 0)
		goto fail;

	/* Configure outbound ranges POMs */
	ppc4xx_configure_pciex_POMs(port, hose, mbase);

	/* Configure inbound ranges PIMs */
	ppc4xx_configure_pciex_PIMs(port, hose, mbase, &dma_window);

	/* The root complex doesn't show up if we don't set some vendor
	 * and device IDs into it. The defaults below are the same bogus
	 * one that the initial code in arch/ppc had. This can be
	 * overwritten by setting the "vendor-id/device-id" properties
	 * in the pciex node.
	 */

	/* Get the (optional) vendor-/device-id from the device-tree */
	pval = of_get_property(port->node, "vendor-id", NULL);
	if (pval) {
		val = *pval;
	} else {
		if (!port->endpoint)
			val = 0xaaa0 + port->index;
		else
			val = 0xeee0 + port->index;
	}
	out_le16(mbase + 0x200, val);

	pval = of_get_property(port->node, "device-id", NULL);
	if (pval) {
		val = *pval;
	} else {
		if (!port->endpoint)
			val = 0xbed0 + port->index;
		else
			val = 0xfed0 + port->index;
	}
	out_le16(mbase + 0x202, val);

	if (!port->endpoint) {
		/* Set Class Code to PCI-PCI bridge and Revision Id to 1 */
		out_le32(mbase + 0x208, 0x06040001);

		printk(KERN_INFO "PCIE%d: successfully set as root-complex\n",
		       port->index);
	} else {
		/* Set Class Code to Processor/PPC */
		out_le32(mbase + 0x208, 0x0b200001);

		printk(KERN_INFO "PCIE%d: successfully set as endpoint\n",
		       port->index);
	}

	return;
 fail:
	if (hose)
		pcibios_free_controller(hose);
	if (cfg_data)
		iounmap(cfg_data);
	if (mbase)
		iounmap(mbase);
}

static void __init ppc4xx_probe_pciex_bridge(struct device_node *np)
{
	struct ppc4xx_pciex_port *port;
	const u32 *pval;
	int portno;
	unsigned int dcrs;
	const char *val;

	/* First, proceed to core initialization as we assume there's
	 * only one PCIe core in the system
	 */
	if (ppc4xx_pciex_check_core_init(np))
		return;

	/* Get the port number from the device-tree */
	pval = of_get_property(np, "port", NULL);
	if (pval == NULL) {
		printk(KERN_ERR "PCIE: Can't find port number for %s\n",
		       np->full_name);
		return;
	}
	portno = *pval;
	if (portno >= ppc4xx_pciex_port_count) {
		printk(KERN_ERR "PCIE: port number out of range for %s\n",
		       np->full_name);
		return;
	}
	port = &ppc4xx_pciex_ports[portno];
	port->index = portno;

	/*
	 * Check if device is enabled
	 */
	if (!of_device_is_available(np)) {
		printk(KERN_INFO "PCIE%d: Port disabled via device-tree\n", port->index);
		return;
	}

	port->node = of_node_get(np);
	pval = of_get_property(np, "sdr-base", NULL);
	if (pval == NULL) {
		printk(KERN_ERR "PCIE: missing sdr-base for %s\n",
		       np->full_name);
		return;
	}
	port->sdr_base = *pval;

	/* Check if device_type property is set to "pci" or "pci-endpoint".
	 * Resulting from this setup this PCIe port will be configured
	 * as root-complex or as endpoint.
	 */
	val = of_get_property(port->node, "device_type", NULL);
	if (!strcmp(val, "pci-endpoint")) {
		port->endpoint = 1;
	} else if (!strcmp(val, "pci")) {
		port->endpoint = 0;
	} else {
		printk(KERN_ERR "PCIE: missing or incorrect device_type for %s\n",
		       np->full_name);
		return;
	}

	/* Fetch config space registers address */
	if (of_address_to_resource(np, 0, &port->cfg_space)) {
		printk(KERN_ERR "%s: Can't get PCI-E config space !",
		       np->full_name);
		return;
	}
	/* Fetch host bridge internal registers address */
	if (of_address_to_resource(np, 1, &port->utl_regs)) {
		printk(KERN_ERR "%s: Can't get UTL register base !",
		       np->full_name);
		return;
	}

	/* Map DCRs */
	dcrs = dcr_resource_start(np, 0);
	if (dcrs == 0) {
		printk(KERN_ERR "%s: Can't get DCR register base !",
		       np->full_name);
		return;
	}
	port->dcrs = dcr_map(np, dcrs, dcr_resource_len(np, 0));

	/* Initialize the port specific registers */
	if (ppc4xx_pciex_port_init(port)) {
		printk(KERN_WARNING "PCIE%d: Port init failed\n", port->index);
		return;
	}

	/* Setup the linux hose data structure */
	ppc4xx_pciex_port_setup_hose(port);
}

#endif /* CONFIG_PPC4xx_PCI_EXPRESS */

static int __init ppc4xx_pci_find_bridges(void)
{
	struct device_node *np;

#ifdef CONFIG_PPC4xx_PCI_EXPRESS
	for_each_compatible_node(np, NULL, "ibm,plb-pciex")
		ppc4xx_probe_pciex_bridge(np);
#endif
	for_each_compatible_node(np, NULL, "ibm,plb-pcix")
		ppc4xx_probe_pcix_bridge(np);
	for_each_compatible_node(np, NULL, "ibm,plb-pci")
		ppc4xx_probe_pci_bridge(np);

	return 0;
}
arch_initcall(ppc4xx_pci_find_bridges);

