/* pci_sun4v.c: SUN4V specific PCI controller support.
 *
 * Copyright (C) 2006 David S. Miller (davem@davemloft.net)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#include <asm/pbm.h>
#include <asm/iommu.h>
#include <asm/irq.h>
#include <asm/upa.h>
#include <asm/pstate.h>
#include <asm/oplib.h>
#include <asm/hypervisor.h>

#include "pci_impl.h"
#include "iommu_common.h"

#include "pci_sun4v.h"

static void *pci_4v_alloc_consistent(struct pci_dev *pdev, size_t size, dma_addr_t *dma_addrp)
{
	return NULL;
}

static void pci_4v_free_consistent(struct pci_dev *pdev, size_t size, void *cpu, dma_addr_t dvma)
{
}

static dma_addr_t pci_4v_map_single(struct pci_dev *pdev, void *ptr, size_t sz, int direction)
{
	return 0;
}

static void pci_4v_unmap_single(struct pci_dev *pdev, dma_addr_t bus_addr, size_t sz, int direction)
{
}

static int pci_4v_map_sg(struct pci_dev *pdev, struct scatterlist *sglist, int nelems, int direction)
{
	return nelems;
}

static void pci_4v_unmap_sg(struct pci_dev *pdev, struct scatterlist *sglist, int nelems, int direction)
{
}

static void pci_4v_dma_sync_single_for_cpu(struct pci_dev *pdev, dma_addr_t bus_addr, size_t sz, int direction)
{
}

static void pci_4v_dma_sync_sg_for_cpu(struct pci_dev *pdev, struct scatterlist *sglist, int nelems, int direction)
{
}

struct pci_iommu_ops pci_sun4v_iommu_ops = {
	.alloc_consistent		= pci_4v_alloc_consistent,
	.free_consistent		= pci_4v_free_consistent,
	.map_single			= pci_4v_map_single,
	.unmap_single			= pci_4v_unmap_single,
	.map_sg				= pci_4v_map_sg,
	.unmap_sg			= pci_4v_unmap_sg,
	.dma_sync_single_for_cpu	= pci_4v_dma_sync_single_for_cpu,
	.dma_sync_sg_for_cpu		= pci_4v_dma_sync_sg_for_cpu,
};

/* SUN4V PCI configuration space accessors. */

static int pci_sun4v_read_pci_cfg(struct pci_bus *bus_dev, unsigned int devfn,
				  int where, int size, u32 *value)
{
	struct pci_pbm_info *pbm = bus_dev->sysdata;
	unsigned long devhandle = pbm->devhandle;
	unsigned int bus = bus_dev->number;
	unsigned int device = PCI_SLOT(devfn);
	unsigned int func = PCI_FUNC(devfn);
	unsigned long ret;

	ret = pci_sun4v_config_get(devhandle,
				   HV_PCI_DEVICE_BUILD(bus, device, func),
				   where, size);
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

static int pci_sun4v_write_pci_cfg(struct pci_bus *bus_dev, unsigned int devfn,
				   int where, int size, u32 value)
{
	struct pci_pbm_info *pbm = bus_dev->sysdata;
	unsigned long devhandle = pbm->devhandle;
	unsigned int bus = bus_dev->number;
	unsigned int device = PCI_SLOT(devfn);
	unsigned int func = PCI_FUNC(devfn);
	unsigned long ret;

	ret = pci_sun4v_config_put(devhandle,
				   HV_PCI_DEVICE_BUILD(bus, device, func),
				   where, size, value);

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops pci_sun4v_ops = {
	.read =		pci_sun4v_read_pci_cfg,
	.write =	pci_sun4v_write_pci_cfg,
};


static void pci_sun4v_scan_bus(struct pci_controller_info *p)
{
	/* XXX Implement me! XXX */
}

static unsigned int pci_sun4v_irq_build(struct pci_pbm_info *pbm,
					struct pci_dev *pdev,
					unsigned int ino)
{
	/* XXX Implement me! XXX */
	return 0;
}

/* XXX correct? XXX */
static void pci_sun4v_base_address_update(struct pci_dev *pdev, int resource)
{
	struct pcidev_cookie *pcp = pdev->sysdata;
	struct pci_pbm_info *pbm = pcp->pbm;
	struct resource *res, *root;
	u32 reg;
	int where, size, is_64bit;

	res = &pdev->resource[resource];
	if (resource < 6) {
		where = PCI_BASE_ADDRESS_0 + (resource * 4);
	} else if (resource == PCI_ROM_RESOURCE) {
		where = pdev->rom_base_reg;
	} else {
		/* Somebody might have asked allocation of a non-standard resource */
		return;
	}

	is_64bit = 0;
	if (res->flags & IORESOURCE_IO)
		root = &pbm->io_space;
	else {
		root = &pbm->mem_space;
		if ((res->flags & PCI_BASE_ADDRESS_MEM_TYPE_MASK)
		    == PCI_BASE_ADDRESS_MEM_TYPE_64)
			is_64bit = 1;
	}

	size = res->end - res->start;
	pci_read_config_dword(pdev, where, &reg);
	reg = ((reg & size) |
	       (((u32)(res->start - root->start)) & ~size));
	if (resource == PCI_ROM_RESOURCE) {
		reg |= PCI_ROM_ADDRESS_ENABLE;
		res->flags |= IORESOURCE_ROM_ENABLE;
	}
	pci_write_config_dword(pdev, where, reg);

	/* This knows that the upper 32-bits of the address
	 * must be zero.  Our PCI common layer enforces this.
	 */
	if (is_64bit)
		pci_write_config_dword(pdev, where + 4, 0);
}

/* XXX correct? XXX */
static void pci_sun4v_resource_adjust(struct pci_dev *pdev,
				      struct resource *res,
				      struct resource *root)
{
	res->start += root->start;
	res->end += root->start;
}

/* Use ranges property to determine where PCI MEM, I/O, and Config
 * space are for this PCI bus module.
 */
static void pci_sun4v_determine_mem_io_space(struct pci_pbm_info *pbm)
{
	int i, saw_cfg, saw_mem, saw_io;

	saw_cfg = saw_mem = saw_io = 0;
	for (i = 0; i < pbm->num_pbm_ranges; i++) {
		struct linux_prom_pci_ranges *pr = &pbm->pbm_ranges[i];
		unsigned long a;
		int type;

		type = (pr->child_phys_hi >> 24) & 0x3;
		a = (((unsigned long)pr->parent_phys_hi << 32UL) |
		     ((unsigned long)pr->parent_phys_lo  <<  0UL));

		switch (type) {
		case 0:
			/* PCI config space, 16MB */
			pbm->config_space = a;
			saw_cfg = 1;
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

		default:
			break;
		};
	}

	if (!saw_cfg || !saw_io || !saw_mem) {
		prom_printf("%s: Fatal error, missing %s PBM range.\n",
			    pbm->name,
			    ((!saw_cfg ?
			      "CFG" :
			      (!saw_io ?
			       "IO" : "MEM"))));
		prom_halt();
	}

	printk("%s: PCI CFG[%lx] IO[%lx] MEM[%lx]\n",
	       pbm->name,
	       pbm->config_space,
	       pbm->io_space.start,
	       pbm->mem_space.start);
}

static void pbm_register_toplevel_resources(struct pci_controller_info *p,
					    struct pci_pbm_info *pbm)
{
	pbm->io_space.name = pbm->mem_space.name = pbm->name;

	request_resource(&ioport_resource, &pbm->io_space);
	request_resource(&iomem_resource, &pbm->mem_space);
	pci_register_legacy_regions(&pbm->io_space,
				    &pbm->mem_space);
}

static void pci_sun4v_iommu_init(struct pci_pbm_info *pbm)
{
	/* XXX Implement me! XXX */
}

static void pci_sun4v_pbm_init(struct pci_controller_info *p, int prom_node)
{
	struct pci_pbm_info *pbm;
	struct linux_prom64_registers regs;
	unsigned int busrange[2];
	int err;

	/* XXX */
	pbm = &p->pbm_A;

	pbm->parent = p;
	pbm->prom_node = prom_node;
	pbm->pci_first_slot = 1;

	prom_getproperty(prom_node, "reg", (char *)&regs, sizeof(regs));
	pbm->devhandle = (regs.phys_addr >> 32UL) & 0x0fffffff;

	sprintf(pbm->name, "SUN4V-PCI%d PBM%c",
		p->index, (pbm == &p->pbm_A ? 'A' : 'B'));

	printk("%s: devhandle[%x]\n", pbm->name, pbm->devhandle);

	prom_getstring(prom_node, "name",
		       pbm->prom_name, sizeof(pbm->prom_name));

	err = prom_getproperty(prom_node, "ranges",
			       (char *) pbm->pbm_ranges,
			       sizeof(pbm->pbm_ranges));
	if (err == 0 || err == -1) {
		prom_printf("%s: Fatal error, no ranges property.\n",
			    pbm->name);
		prom_halt();
	}

	pbm->num_pbm_ranges =
		(err / sizeof(struct linux_prom_pci_ranges));

	pci_sun4v_determine_mem_io_space(pbm);
	pbm_register_toplevel_resources(p, pbm);

	err = prom_getproperty(prom_node, "interrupt-map",
			       (char *)pbm->pbm_intmap,
			       sizeof(pbm->pbm_intmap));
	if (err != -1) {
		pbm->num_pbm_intmap = (err / sizeof(struct linux_prom_pci_intmap));
		err = prom_getproperty(prom_node, "interrupt-map-mask",
				       (char *)&pbm->pbm_intmask,
				       sizeof(pbm->pbm_intmask));
		if (err == -1) {
			prom_printf("%s: Fatal error, no "
				    "interrupt-map-mask.\n", pbm->name);
			prom_halt();
		}
	} else {
		pbm->num_pbm_intmap = 0;
		memset(&pbm->pbm_intmask, 0, sizeof(pbm->pbm_intmask));
	}

	err = prom_getproperty(prom_node, "bus-range",
			       (char *)&busrange[0],
			       sizeof(busrange));
	if (err == 0 || err == -1) {
		prom_printf("%s: Fatal error, no bus-range.\n", pbm->name);
		prom_halt();
	}
	pbm->pci_first_busno = busrange[0];
	pbm->pci_last_busno = busrange[1];

	pci_sun4v_iommu_init(pbm);
}

void sun4v_pci_init(int node, char *model_name)
{
	struct pci_controller_info *p;
	struct pci_iommu *iommu;

	p = kmalloc(sizeof(struct pci_controller_info), GFP_ATOMIC);
	if (!p) {
		prom_printf("SUN4V_PCI: Fatal memory allocation error.\n");
		prom_halt();
	}
	memset(p, 0, sizeof(*p));

	iommu = kmalloc(sizeof(struct pci_iommu), GFP_ATOMIC);
	if (!iommu) {
		prom_printf("SCHIZO: Fatal memory allocation error.\n");
		prom_halt();
	}
	memset(iommu, 0, sizeof(*iommu));
	p->pbm_A.iommu = iommu;

	iommu = kmalloc(sizeof(struct pci_iommu), GFP_ATOMIC);
	if (!iommu) {
		prom_printf("SCHIZO: Fatal memory allocation error.\n");
		prom_halt();
	}
	memset(iommu, 0, sizeof(*iommu));
	p->pbm_B.iommu = iommu;

	p->next = pci_controller_root;
	pci_controller_root = p;

	p->index = pci_num_controllers++;
	p->pbms_same_domain = 0;

	p->scan_bus = pci_sun4v_scan_bus;
	p->irq_build = pci_sun4v_irq_build;
	p->base_address_update = pci_sun4v_base_address_update;
	p->resource_adjust = pci_sun4v_resource_adjust;
	p->pci_ops = &pci_sun4v_ops;

	/* Like PSYCHO and SCHIZO we have a 2GB aligned area
	 * for memory space.
	 */
	pci_memspace_mask = 0x7fffffffUL;

	pci_sun4v_pbm_init(p, node);

	prom_printf("sun4v_pci_init: Implement me.\n");
	prom_halt();
}
