/*
 * Authors: Frank Rowand <frank_rowand@mvista.com>,
 * Debbie Chu <debbie_chu@mvista.com>, or source@mvista.com
 * Further modifications by Armin Kuster <akuster@mvista.com>
 *
 * 2000 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Based on arch/ppc/kernel/indirect.c, Copyright (C) 1998 Gabriel Paubert.
 */

#include <linux/pci.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/machdep.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <asm/ocp.h>
#include <asm/ibm4xx.h>
#include <asm/pci-bridge.h>
#include <asm/ibm_ocp_pci.h>


extern void bios_fixup(struct pci_controller *, struct pcil0_regs *);
extern int ppc405_map_irq(struct pci_dev *dev, unsigned char idsel,
			  unsigned char pin);

void
ppc405_pcibios_fixup_resources(struct pci_dev *dev)
{
	int i;
	unsigned long max_host_addr;
	unsigned long min_host_addr;
	struct resource *res;

	/*
	 * openbios puts some graphics cards in the same range as the host
	 * controller uses to map to SDRAM.  Fix it.
	 */

	min_host_addr = 0;
	max_host_addr = PPC405_PCI_MEM_BASE - 1;

	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		res = dev->resource + i;
		if (!res->start)
			continue;
		if ((res->flags & IORESOURCE_MEM) &&
		    (((res->start >= min_host_addr)
		      && (res->start <= max_host_addr))
		     || ((res->end >= min_host_addr)
			 && (res->end <= max_host_addr))
		     || ((res->start < min_host_addr)
			 && (res->end > max_host_addr))
		    )
		    ) {

			/* force pcibios_assign_resources() to assign a new address */
			res->end -= res->start;
			res->start = 0;
		}
	}
}

static int
ppc4xx_exclude_device(unsigned char bus, unsigned char devfn)
{
	/* We prevent us from seeing ourselves to avoid having
	 * the kernel try to remap our BAR #1 and fuck up bus
	 * master from external PCI devices
	 */
	return (bus == 0 && devfn == 0);
}

void
ppc4xx_find_bridges(void)
{
	struct pci_controller *hose_a;
	struct pcil0_regs *pcip;
	unsigned int tmp_addr;
	unsigned int tmp_size;
	unsigned int reg_index;
	unsigned int new_pmm_max = 0;
	unsigned int new_pmm_min = 0;

	isa_io_base = 0;
	isa_mem_base = 0;
	pci_dram_offset = 0;

	/* Setup PCI32 hose */
	hose_a = pcibios_alloc_controller();
	if (!hose_a)
		return;
	setup_indirect_pci(hose_a, PPC405_PCI_CONFIG_ADDR,
			   PPC405_PCI_CONFIG_DATA);

	pcip = ioremap(PPC4xx_PCI_LCFG_PADDR, PAGE_SIZE);
	if (pcip != NULL) {

#if defined(CONFIG_BIOS_FIXUP)
		bios_fixup(hose_a, pcip);
#endif
		new_pmm_min = 0xffffffff;
		for (reg_index = 0; reg_index < 3; reg_index++) {
			tmp_size = in_le32(&pcip->pmm[reg_index].ma);	// mask & attrs
			/* test the enable bit */
			if ((tmp_size & 0x1) == 0)
				continue;
			tmp_addr = in_le32(&pcip->pmm[reg_index].pcila);	// PCI addr
			if (tmp_addr < PPC405_PCI_PHY_MEM_BASE) {
				printk(KERN_DEBUG
				       "Disabling mapping to PCI mem addr 0x%8.8x\n",
				       tmp_addr);
				out_le32(&pcip->pmm[reg_index].ma, tmp_size & ~1);	// *_PMMOMA
				continue;
			}
			tmp_addr = in_le32(&pcip->pmm[reg_index].la);	// *_PMMOLA
			if (tmp_addr < new_pmm_min)
				new_pmm_min = tmp_addr;
			tmp_addr = tmp_addr +
				(0xffffffff - (tmp_size & 0xffffc000));
			if (tmp_addr > PPC405_PCI_UPPER_MEM) {
				new_pmm_max = tmp_addr;	// PPC405_PCI_UPPER_MEM
			} else {
				new_pmm_max = PPC405_PCI_UPPER_MEM;
			}

		}		// for

		iounmap(pcip);
	}

	hose_a->first_busno = 0;
	hose_a->last_busno = 0xff;
	hose_a->pci_mem_offset = 0;

	/* Setup bridge memory/IO ranges & resources
	 * TODO: Handle firmwares setting up a legacy ISA mem base
	 */
	hose_a->io_space.start = PPC405_PCI_LOWER_IO;
	hose_a->io_space.end = PPC405_PCI_UPPER_IO;
	hose_a->mem_space.start = new_pmm_min;
	hose_a->mem_space.end = new_pmm_max;
	hose_a->io_base_phys = PPC405_PCI_PHY_IO_BASE;
	hose_a->io_base_virt = ioremap(hose_a->io_base_phys, 0x10000);
	hose_a->io_resource.start = 0;
	hose_a->io_resource.end = PPC405_PCI_UPPER_IO - PPC405_PCI_LOWER_IO;
	hose_a->io_resource.flags = IORESOURCE_IO;
	hose_a->io_resource.name = "PCI I/O";
	hose_a->mem_resources[0].start = new_pmm_min;
	hose_a->mem_resources[0].end = new_pmm_max;
	hose_a->mem_resources[0].flags = IORESOURCE_MEM;
	hose_a->mem_resources[0].name = "PCI Memory";
	isa_io_base = (int) hose_a->io_base_virt;
	isa_mem_base = 0;	/*     ISA not implemented */
	ISA_DMA_THRESHOLD = 0x00ffffff;	/* ??? ISA not implemented */

	/* Scan busses & initial setup by pci_auto */
	hose_a->last_busno = pciauto_bus_scan(hose_a, hose_a->first_busno);
	hose_a->last_busno = 0;

	/* Setup ppc_md */
	ppc_md.pcibios_fixup = NULL;
	ppc_md.pci_exclude_device = ppc4xx_exclude_device;
	ppc_md.pcibios_fixup_resources = ppc405_pcibios_fixup_resources;
	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = ppc405_map_irq;
}
