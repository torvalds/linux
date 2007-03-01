/* $Id: pci_common.c,v 1.29 2002/02/01 00:56:03 davem Exp $
 * pci_common.c: PCI controller common support.
 *
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 */

#include <linux/string.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/device.h>

#include <asm/pbm.h>
#include <asm/prom.h>
#include <asm/of_device.h>

#include "pci_impl.h"

void pci_register_legacy_regions(struct resource *io_res,
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

/* Generic helper routines for PCI error reporting. */
void pci_scan_for_target_abort(struct pci_controller_info *p,
			       struct pci_pbm_info *pbm,
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
			printk("PCI%d(PBM%c): Device [%s] saw Target Abort [%016x]\n",
			       p->index, ((pbm == &p->pbm_A) ? 'A' : 'B'),
			       pci_name(pdev), status);
		}
	}

	list_for_each_entry(bus, &pbus->children, node)
		pci_scan_for_target_abort(p, pbm, bus);
}

void pci_scan_for_master_abort(struct pci_controller_info *p,
			       struct pci_pbm_info *pbm,
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
			printk("PCI%d(PBM%c): Device [%s] received Master Abort [%016x]\n",
			       p->index, ((pbm == &p->pbm_A) ? 'A' : 'B'),
			       pci_name(pdev), status);
		}
	}

	list_for_each_entry(bus, &pbus->children, node)
		pci_scan_for_master_abort(p, pbm, bus);
}

void pci_scan_for_parity_error(struct pci_controller_info *p,
			       struct pci_pbm_info *pbm,
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
			printk("PCI%d(PBM%c): Device [%s] saw Parity Error [%016x]\n",
			       p->index, ((pbm == &p->pbm_A) ? 'A' : 'B'),
			       pci_name(pdev), status);
		}
	}

	list_for_each_entry(bus, &pbus->children, node)
		pci_scan_for_parity_error(p, pbm, bus);
}
