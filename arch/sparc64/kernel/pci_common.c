/* pci_common.c: PCI controller common support.
 *
 * Copyright (C) 1999, 2007 David S. Miller (davem@davemloft.net)
 */

#include <linux/string.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/device.h>

#include <asm/prom.h>
#include <asm/of_device.h>
#include <asm/oplib.h>

#include "pci_impl.h"

void pci_get_pbm_props(struct pci_pbm_info *pbm)
{
	const u32 *val = of_get_property(pbm->prom_node, "bus-range", NULL);

	pbm->pci_first_busno = val[0];
	pbm->pci_last_busno = val[1];

	val = of_get_property(pbm->prom_node, "ino-bitmap", NULL);
	if (val) {
		pbm->ino_bitmap = (((u64)val[1] << 32UL) |
				   ((u64)val[0] <<  0UL));
	}
}

static void pci_register_legacy_regions(struct resource *io_res,
					struct resource *mem_res)
{
	struct resource *p;

	/* VGA Video RAM. */
	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return;

	p->name = "Video RAM area";
	p->start = mem_res->start + 0xa0000UL;
	p->end = p->start + 0x1ffffUL;
	p->flags = IORESOURCE_BUSY;
	request_resource(mem_res, p);

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return;

	p->name = "System ROM";
	p->start = mem_res->start + 0xf0000UL;
	p->end = p->start + 0xffffUL;
	p->flags = IORESOURCE_BUSY;
	request_resource(mem_res, p);

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return;

	p->name = "Video ROM";
	p->start = mem_res->start + 0xc0000UL;
	p->end = p->start + 0x7fffUL;
	p->flags = IORESOURCE_BUSY;
	request_resource(mem_res, p);
}

static void pci_register_iommu_region(struct pci_pbm_info *pbm)
{
	const u32 *vdma = of_get_property(pbm->prom_node, "virtual-dma", NULL);

	if (vdma) {
		struct resource *rp = kmalloc(sizeof(*rp), GFP_KERNEL);

		if (!rp) {
			prom_printf("Cannot allocate IOMMU resource.\n");
			prom_halt();
		}
		rp->name = "IOMMU";
		rp->start = pbm->mem_space.start + (unsigned long) vdma[0];
		rp->end = rp->start + (unsigned long) vdma[1] - 1UL;
		rp->flags = IORESOURCE_BUSY;
		request_resource(&pbm->mem_space, rp);
	}
}

void pci_determine_mem_io_space(struct pci_pbm_info *pbm)
{
	const struct linux_prom_pci_ranges *pbm_ranges;
	int i, saw_mem, saw_io;
	int num_pbm_ranges;

	saw_mem = saw_io = 0;
	pbm_ranges = of_get_property(pbm->prom_node, "ranges", &i);
	num_pbm_ranges = i / sizeof(*pbm_ranges);

	for (i = 0; i < num_pbm_ranges; i++) {
		const struct linux_prom_pci_ranges *pr = &pbm_ranges[i];
		unsigned long a;
		u32 parent_phys_hi, parent_phys_lo;
		int type;

		parent_phys_hi = pr->parent_phys_hi;
		parent_phys_lo = pr->parent_phys_lo;
		if (tlb_type == hypervisor)
			parent_phys_hi &= 0x0fffffff;

		type = (pr->child_phys_hi >> 24) & 0x3;
		a = (((unsigned long)parent_phys_hi << 32UL) |
		     ((unsigned long)parent_phys_lo  <<  0UL));

		switch (type) {
		case 0:
			/* PCI config space, 16MB */
			pbm->config_space = a;
			break;

		case 1:
			/* 16-bit IO space, 16MB */
			pbm->io_space.start = a;
			pbm->io_space.end = a + ((16UL*1024UL*1024UL) - 1UL);
			pbm->io_space.flags = IORESOURCE_IO;
			saw_io = 1;
			break;

		case 2:
			/* 32-bit MEM space, 2GB */
			pbm->mem_space.start = a;
			pbm->mem_space.end = a + (0x80000000UL - 1UL);
			pbm->mem_space.flags = IORESOURCE_MEM;
			saw_mem = 1;
			break;

		case 3:
			/* XXX 64-bit MEM handling XXX */

		default:
			break;
		};
	}

	if (!saw_io || !saw_mem) {
		prom_printf("%s: Fatal error, missing %s PBM range.\n",
			    pbm->name,
			    (!saw_io ? "IO" : "MEM"));
		prom_halt();
	}

	printk("%s: PCI IO[%lx] MEM[%lx]\n",
	       pbm->name,
	       pbm->io_space.start,
	       pbm->mem_space.start);

	pbm->io_space.name = pbm->mem_space.name = pbm->name;

	request_resource(&ioport_resource, &pbm->io_space);
	request_resource(&iomem_resource, &pbm->mem_space);

	pci_register_legacy_regions(&pbm->io_space,
				    &pbm->mem_space);
	pci_register_iommu_region(pbm);
}

/* Generic helper routines for PCI error reporting. */
void pci_scan_for_target_abort(struct pci_pbm_info *pbm,
			       struct pci_bus *pbus)
{
	struct pci_dev *pdev;
	struct pci_bus *bus;

	list_for_each_entry(pdev, &pbus->devices, bus_list) {
		u16 status, error_bits;

		pci_read_config_word(pdev, PCI_STATUS, &status);
		error_bits =
			(status & (PCI_STATUS_SIG_TARGET_ABORT |
				   PCI_STATUS_REC_TARGET_ABORT));
		if (error_bits) {
			pci_write_config_word(pdev, PCI_STATUS, error_bits);
			printk("%s: Device %s saw Target Abort [%016x]\n",
			       pbm->name, pci_name(pdev), status);
		}
	}

	list_for_each_entry(bus, &pbus->children, node)
		pci_scan_for_target_abort(pbm, bus);
}

void pci_scan_for_master_abort(struct pci_pbm_info *pbm,
			       struct pci_bus *pbus)
{
	struct pci_dev *pdev;
	struct pci_bus *bus;

	list_for_each_entry(pdev, &pbus->devices, bus_list) {
		u16 status, error_bits;

		pci_read_config_word(pdev, PCI_STATUS, &status);
		error_bits =
			(status & (PCI_STATUS_REC_MASTER_ABORT));
		if (error_bits) {
			pci_write_config_word(pdev, PCI_STATUS, error_bits);
			printk("%s: Device %s received Master Abort [%016x]\n",
			       pbm->name, pci_name(pdev), status);
		}
	}

	list_for_each_entry(bus, &pbus->children, node)
		pci_scan_for_master_abort(pbm, bus);
}

void pci_scan_for_parity_error(struct pci_pbm_info *pbm,
			       struct pci_bus *pbus)
{
	struct pci_dev *pdev;
	struct pci_bus *bus;

	list_for_each_entry(pdev, &pbus->devices, bus_list) {
		u16 status, error_bits;

		pci_read_config_word(pdev, PCI_STATUS, &status);
		error_bits =
			(status & (PCI_STATUS_PARITY |
				   PCI_STATUS_DETECTED_PARITY));
		if (error_bits) {
			pci_write_config_word(pdev, PCI_STATUS, error_bits);
			printk("%s: Device %s saw Parity Error [%016x]\n",
			       pbm->name, pci_name(pdev), status);
		}
	}

	list_for_each_entry(bus, &pbus->children, node)
		pci_scan_for_parity_error(pbm, bus);
}
