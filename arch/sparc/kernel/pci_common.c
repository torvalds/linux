/* pci_common.c: PCI controller common support.
 *
 * Copyright (C) 1999, 2007 David S. Miller (davem@davemloft.net)
 */

#include <linux/string.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/of_device.h>

#include <asm/prom.h>
#include <asm/oplib.h>

#include "pci_impl.h"
#include "pci_sun4v.h"

static int config_out_of_range(struct pci_pbm_info *pbm,
			       unsigned long bus,
			       unsigned long devfn,
			       unsigned long reg)
{
	if (bus < pbm->pci_first_busno ||
	    bus > pbm->pci_last_busno)
		return 1;
	return 0;
}

static void *sun4u_config_mkaddr(struct pci_pbm_info *pbm,
				 unsigned long bus,
				 unsigned long devfn,
				 unsigned long reg)
{
	unsigned long rbits = pbm->config_space_reg_bits;

	if (config_out_of_range(pbm, bus, devfn, reg))
		return NULL;

	reg = (reg & ((1 << rbits) - 1));
	devfn <<= rbits;
	bus <<= rbits + 8;

	return (void *)	(pbm->config_space | bus | devfn | reg);
}

/* At least on Sabre, it is necessary to access all PCI host controller
 * registers at their natural size, otherwise zeros are returned.
 * Strange but true, and I see no language in the UltraSPARC-IIi
 * programmer's manual that mentions this even indirectly.
 */
static int sun4u_read_pci_cfg_host(struct pci_pbm_info *pbm,
				   unsigned char bus, unsigned int devfn,
				   int where, int size, u32 *value)
{
	u32 tmp32, *addr;
	u16 tmp16;
	u8 tmp8;

	addr = sun4u_config_mkaddr(pbm, bus, devfn, where);
	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	switch (size) {
	case 1:
		if (where < 8) {
			unsigned long align = (unsigned long) addr;

			align &= ~1;
			pci_config_read16((u16 *)align, &tmp16);
			if (where & 1)
				*value = tmp16 >> 8;
			else
				*value = tmp16 & 0xff;
		} else {
			pci_config_read8((u8 *)addr, &tmp8);
			*value = (u32) tmp8;
		}
		break;

	case 2:
		if (where < 8) {
			pci_config_read16((u16 *)addr, &tmp16);
			*value = (u32) tmp16;
		} else {
			pci_config_read8((u8 *)addr, &tmp8);
			*value = (u32) tmp8;
			pci_config_read8(((u8 *)addr) + 1, &tmp8);
			*value |= ((u32) tmp8) << 8;
		}
		break;

	case 4:
		tmp32 = 0xffffffff;
		sun4u_read_pci_cfg_host(pbm, bus, devfn,
					where, 2, &tmp32);
		*value = tmp32;

		tmp32 = 0xffffffff;
		sun4u_read_pci_cfg_host(pbm, bus, devfn,
					where + 2, 2, &tmp32);
		*value |= tmp32 << 16;
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int sun4u_read_pci_cfg(struct pci_bus *bus_dev, unsigned int devfn,
			      int where, int size, u32 *value)
{
	struct pci_pbm_info *pbm = bus_dev->sysdata;
	unsigned char bus = bus_dev->number;
	u32 *addr;
	u16 tmp16;
	u8 tmp8;

	switch (size) {
	case 1:
		*value = 0xff;
		break;
	case 2:
		*value = 0xffff;
		break;
	case 4:
		*value = 0xffffffff;
		break;
	}

	if (!bus_dev->number && !PCI_SLOT(devfn))
		return sun4u_read_pci_cfg_host(pbm, bus, devfn, where,
					       size, value);

	addr = sun4u_config_mkaddr(pbm, bus, devfn, where);
	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	switch (size) {
	case 1:
		pci_config_read8((u8 *)addr, &tmp8);
		*value = (u32) tmp8;
		break;

	case 2:
		if (where & 0x01) {
			printk("pci_read_config_word: misaligned reg [%x]\n",
			       where);
			return PCIBIOS_SUCCESSFUL;
		}
		pci_config_read16((u16 *)addr, &tmp16);
		*value = (u32) tmp16;
		break;

	case 4:
		if (where & 0x03) {
			printk("pci_read_config_dword: misaligned reg [%x]\n",
			       where);
			return PCIBIOS_SUCCESSFUL;
		}
		pci_config_read32(addr, value);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int sun4u_write_pci_cfg_host(struct pci_pbm_info *pbm,
				    unsigned char bus, unsigned int devfn,
				    int where, int size, u32 value)
{
	u32 *addr;

	addr = sun4u_config_mkaddr(pbm, bus, devfn, where);
	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	switch (size) {
	case 1:
		if (where < 8) {
			unsigned long align = (unsigned long) addr;
			u16 tmp16;

			align &= ~1;
			pci_config_read16((u16 *)align, &tmp16);
			if (where & 1) {
				tmp16 &= 0x00ff;
				tmp16 |= value << 8;
			} else {
				tmp16 &= 0xff00;
				tmp16 |= value;
			}
			pci_config_write16((u16 *)align, tmp16);
		} else
			pci_config_write8((u8 *)addr, value);
		break;
	case 2:
		if (where < 8) {
			pci_config_write16((u16 *)addr, value);
		} else {
			pci_config_write8((u8 *)addr, value & 0xff);
			pci_config_write8(((u8 *)addr) + 1, value >> 8);
		}
		break;
	case 4:
		sun4u_write_pci_cfg_host(pbm, bus, devfn,
					 where, 2, value & 0xffff);
		sun4u_write_pci_cfg_host(pbm, bus, devfn,
					 where + 2, 2, value >> 16);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int sun4u_write_pci_cfg(struct pci_bus *bus_dev, unsigned int devfn,
			       int where, int size, u32 value)
{
	struct pci_pbm_info *pbm = bus_dev->sysdata;
	unsigned char bus = bus_dev->number;
	u32 *addr;

	if (!bus_dev->number && !PCI_SLOT(devfn))
		return sun4u_write_pci_cfg_host(pbm, bus, devfn, where,
						size, value);

	addr = sun4u_config_mkaddr(pbm, bus, devfn, where);
	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	switch (size) {
	case 1:
		pci_config_write8((u8 *)addr, value);
		break;

	case 2:
		if (where & 0x01) {
			printk("pci_write_config_word: misaligned reg [%x]\n",
			       where);
			return PCIBIOS_SUCCESSFUL;
		}
		pci_config_write16((u16 *)addr, value);
		break;

	case 4:
		if (where & 0x03) {
			printk("pci_write_config_dword: misaligned reg [%x]\n",
			       where);
			return PCIBIOS_SUCCESSFUL;
		}
		pci_config_write32(addr, value);
	}
	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops sun4u_pci_ops = {
	.read =		sun4u_read_pci_cfg,
	.write =	sun4u_write_pci_cfg,
};

static int sun4v_read_pci_cfg(struct pci_bus *bus_dev, unsigned int devfn,
			      int where, int size, u32 *value)
{
	struct pci_pbm_info *pbm = bus_dev->sysdata;
	u32 devhandle = pbm->devhandle;
	unsigned int bus = bus_dev->number;
	unsigned int device = PCI_SLOT(devfn);
	unsigned int func = PCI_FUNC(devfn);
	unsigned long ret;

	if (config_out_of_range(pbm, bus, devfn, where)) {
		ret = ~0UL;
	} else {
		ret = pci_sun4v_config_get(devhandle,
				HV_PCI_DEVICE_BUILD(bus, device, func),
				where, size);
	}
	switch (size) {
	case 1:
		*value = ret & 0xff;
		break;
	case 2:
		*value = ret & 0xffff;
		break;
	case 4:
		*value = ret & 0xffffffff;
		break;
	};


	return PCIBIOS_SUCCESSFUL;
}

static int sun4v_write_pci_cfg(struct pci_bus *bus_dev, unsigned int devfn,
			       int where, int size, u32 value)
{
	struct pci_pbm_info *pbm = bus_dev->sysdata;
	u32 devhandle = pbm->devhandle;
	unsigned int bus = bus_dev->number;
	unsigned int device = PCI_SLOT(devfn);
	unsigned int func = PCI_FUNC(devfn);
	unsigned long ret;

	if (config_out_of_range(pbm, bus, devfn, where)) {
		/* Do nothing. */
	} else {
		ret = pci_sun4v_config_put(devhandle,
				HV_PCI_DEVICE_BUILD(bus, device, func),
				where, size, value);
	}
	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops sun4v_pci_ops = {
	.read =		sun4v_read_pci_cfg,
	.write =	sun4v_write_pci_cfg,
};

void pci_get_pbm_props(struct pci_pbm_info *pbm)
{
	const u32 *val = of_get_property(pbm->op->node, "bus-range", NULL);

	pbm->pci_first_busno = val[0];
	pbm->pci_last_busno = val[1];

	val = of_get_property(pbm->op->node, "ino-bitmap", NULL);
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
	const u32 *vdma = of_get_property(pbm->op->node, "virtual-dma", NULL);

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
	pbm_ranges = of_get_property(pbm->op->node, "ranges", &i);
	if (!pbm_ranges) {
		prom_printf("PCI: Fatal error, missing PBM ranges property "
			    " for %s\n",
			    pbm->name);
		prom_halt();
	}

	num_pbm_ranges = i / sizeof(*pbm_ranges);

	for (i = 0; i < num_pbm_ranges; i++) {
		const struct linux_prom_pci_ranges *pr = &pbm_ranges[i];
		unsigned long a, size;
		u32 parent_phys_hi, parent_phys_lo;
		u32 size_hi, size_lo;
		int type;

		parent_phys_hi = pr->parent_phys_hi;
		parent_phys_lo = pr->parent_phys_lo;
		if (tlb_type == hypervisor)
			parent_phys_hi &= 0x0fffffff;

		size_hi = pr->size_hi;
		size_lo = pr->size_lo;

		type = (pr->child_phys_hi >> 24) & 0x3;
		a = (((unsigned long)parent_phys_hi << 32UL) |
		     ((unsigned long)parent_phys_lo  <<  0UL));
		size = (((unsigned long)size_hi << 32UL) |
			((unsigned long)size_lo  <<  0UL));

		switch (type) {
		case 0:
			/* PCI config space, 16MB */
			pbm->config_space = a;
			break;

		case 1:
			/* 16-bit IO space, 16MB */
			pbm->io_space.start = a;
			pbm->io_space.end = a + size - 1UL;
			pbm->io_space.flags = IORESOURCE_IO;
			saw_io = 1;
			break;

		case 2:
			/* 32-bit MEM space, 2GB */
			pbm->mem_space.start = a;
			pbm->mem_space.end = a + size - 1UL;
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

	printk("%s: PCI IO[%llx] MEM[%llx]\n",
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
