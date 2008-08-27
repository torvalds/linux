/*
 * ioport.c:  Simple io mapping allocator.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *
 * 1996: sparc_free_io, 1999: ioremap()/iounmap() by Pete Zaitcev.
 *
 * 2000/01/29
 * <rth> zait: as long as pci_alloc_consistent produces something addressable, 
 *	things are ok.
 * <zaitcev> rth: no, it is relevant, because get_free_pages returns you a
 *	pointer into the big page mapping
 * <rth> zait: so what?
 * <rth> zait: remap_it_my_way(virt_to_phys(get_free_page()))
 * <zaitcev> Hmm
 * <zaitcev> Suppose I did this remap_it_my_way(virt_to_phys(get_free_page())).
 *	So far so good.
 * <zaitcev> Now, driver calls pci_free_consistent(with result of
 *	remap_it_my_way()).
 * <zaitcev> How do you find the address to pass to free_pages()?
 * <rth> zait: walk the page tables?  It's only two or three level after all.
 * <rth> zait: you have to walk them anyway to remove the mapping.
 * <zaitcev> Hmm
 * <zaitcev> Sounds reasonable
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/pci.h>		/* struct pci_dev */
#include <linux/proc_fs.h>
#include <linux/scatterlist.h>
#include <linux/of_device.h>

#include <asm/io.h>
#include <asm/vaddrs.h>
#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/sbus.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/dma.h>
#include <asm/iommu.h>
#include <asm/io-unit.h>

#include "dma.h"

#define mmu_inval_dma_area(p, l)	/* Anton pulled it out for 2.4.0-xx */

static struct resource *_sparc_find_resource(struct resource *r,
					     unsigned long);

static void __iomem *_sparc_ioremap(struct resource *res, u32 bus, u32 pa, int sz);
static void __iomem *_sparc_alloc_io(unsigned int busno, unsigned long phys,
    unsigned long size, char *name);
static void _sparc_free_io(struct resource *res);

static void register_proc_sparc_ioport(void);

/* This points to the next to use virtual memory for DVMA mappings */
static struct resource _sparc_dvma = {
	.name = "sparc_dvma", .start = DVMA_VADDR, .end = DVMA_END - 1
};
/* This points to the start of I/O mappings, cluable from outside. */
/*ext*/ struct resource sparc_iomap = {
	.name = "sparc_iomap", .start = IOBASE_VADDR, .end = IOBASE_END - 1
};

/*
 * Our mini-allocator...
 * Boy this is gross! We need it because we must map I/O for
 * timers and interrupt controller before the kmalloc is available.
 */

#define XNMLN  15
#define XNRES  10	/* SS-10 uses 8 */

struct xresource {
	struct resource xres;	/* Must be first */
	int xflag;		/* 1 == used */
	char xname[XNMLN+1];
};

static struct xresource xresv[XNRES];

static struct xresource *xres_alloc(void) {
	struct xresource *xrp;
	int n;

	xrp = xresv;
	for (n = 0; n < XNRES; n++) {
		if (xrp->xflag == 0) {
			xrp->xflag = 1;
			return xrp;
		}
		xrp++;
	}
	return NULL;
}

static void xres_free(struct xresource *xrp) {
	xrp->xflag = 0;
}

/*
 * These are typically used in PCI drivers
 * which are trying to be cross-platform.
 *
 * Bus type is always zero on IIep.
 */
void __iomem *ioremap(unsigned long offset, unsigned long size)
{
	char name[14];

	sprintf(name, "phys_%08x", (u32)offset);
	return _sparc_alloc_io(0, offset, size, name);
}

/*
 * Comlimentary to ioremap().
 */
void iounmap(volatile void __iomem *virtual)
{
	unsigned long vaddr = (unsigned long) virtual & PAGE_MASK;
	struct resource *res;

	if ((res = _sparc_find_resource(&sparc_iomap, vaddr)) == NULL) {
		printk("free_io/iounmap: cannot free %lx\n", vaddr);
		return;
	}
	_sparc_free_io(res);

	if ((char *)res >= (char*)xresv && (char *)res < (char *)&xresv[XNRES]) {
		xres_free((struct xresource *)res);
	} else {
		kfree(res);
	}
}

/*
 */
void __iomem *sbus_ioremap(struct resource *phyres, unsigned long offset,
    unsigned long size, char *name)
{
	return _sparc_alloc_io(phyres->flags & 0xF,
	    phyres->start + offset, size, name);
}

void __iomem *of_ioremap(struct resource *res, unsigned long offset,
			 unsigned long size, char *name)
{
	return _sparc_alloc_io(res->flags & 0xF,
			       res->start + offset,
			       size, name);
}
EXPORT_SYMBOL(of_ioremap);

void of_iounmap(struct resource *res, void __iomem *base, unsigned long size)
{
	iounmap(base);
}
EXPORT_SYMBOL(of_iounmap);

/*
 */
void sbus_iounmap(volatile void __iomem *addr, unsigned long size)
{
	iounmap(addr);
}

/*
 * Meat of mapping
 */
static void __iomem *_sparc_alloc_io(unsigned int busno, unsigned long phys,
    unsigned long size, char *name)
{
	static int printed_full;
	struct xresource *xres;
	struct resource *res;
	char *tack;
	int tlen;
	void __iomem *va;	/* P3 diag */

	if (name == NULL) name = "???";

	if ((xres = xres_alloc()) != 0) {
		tack = xres->xname;
		res = &xres->xres;
	} else {
		if (!printed_full) {
			printk("ioremap: done with statics, switching to malloc\n");
			printed_full = 1;
		}
		tlen = strlen(name);
		tack = kmalloc(sizeof (struct resource) + tlen + 1, GFP_KERNEL);
		if (tack == NULL) return NULL;
		memset(tack, 0, sizeof(struct resource));
		res = (struct resource *) tack;
		tack += sizeof (struct resource);
	}

	strlcpy(tack, name, XNMLN+1);
	res->name = tack;

	va = _sparc_ioremap(res, busno, phys, size);
	/* printk("ioremap(0x%x:%08lx[0x%lx])=%p\n", busno, phys, size, va); */ /* P3 diag */
	return va;
}

/*
 */
static void __iomem *
_sparc_ioremap(struct resource *res, u32 bus, u32 pa, int sz)
{
	unsigned long offset = ((unsigned long) pa) & (~PAGE_MASK);

	if (allocate_resource(&sparc_iomap, res,
	    (offset + sz + PAGE_SIZE-1) & PAGE_MASK,
	    sparc_iomap.start, sparc_iomap.end, PAGE_SIZE, NULL, NULL) != 0) {
		/* Usually we cannot see printks in this case. */
		prom_printf("alloc_io_res(%s): cannot occupy\n",
		    (res->name != NULL)? res->name: "???");
		prom_halt();
	}

	pa &= PAGE_MASK;
	sparc_mapiorange(bus, pa, res->start, res->end - res->start + 1);

	return (void __iomem *)(unsigned long)(res->start + offset);
}

/*
 * Comlimentary to _sparc_ioremap().
 */
static void _sparc_free_io(struct resource *res)
{
	unsigned long plen;

	plen = res->end - res->start + 1;
	BUG_ON((plen & (PAGE_SIZE-1)) != 0);
	sparc_unmapiorange(res->start, plen);
	release_resource(res);
}

#ifdef CONFIG_SBUS

void sbus_set_sbus64(struct device *dev, int x)
{
	printk("sbus_set_sbus64: unsupported\n");
}

/*
 * Allocate a chunk of memory suitable for DMA.
 * Typically devices use them for control blocks.
 * CPU may access them without any explicit flushing.
 */
void *sbus_alloc_consistent(struct device *dev, long len, u32 *dma_addrp)
{
	struct of_device *op = to_of_device(dev);
	unsigned long len_total = (len + PAGE_SIZE-1) & PAGE_MASK;
	unsigned long va;
	struct resource *res;
	int order;

	/* XXX why are some lengths signed, others unsigned? */
	if (len <= 0) {
		return NULL;
	}
	/* XXX So what is maxphys for us and how do drivers know it? */
	if (len > 256*1024) {			/* __get_free_pages() limit */
		return NULL;
	}

	order = get_order(len_total);
	if ((va = __get_free_pages(GFP_KERNEL|__GFP_COMP, order)) == 0)
		goto err_nopages;

	if ((res = kzalloc(sizeof(struct resource), GFP_KERNEL)) == NULL)
		goto err_nomem;

	if (allocate_resource(&_sparc_dvma, res, len_total,
	    _sparc_dvma.start, _sparc_dvma.end, PAGE_SIZE, NULL, NULL) != 0) {
		printk("sbus_alloc_consistent: cannot occupy 0x%lx", len_total);
		goto err_nova;
	}
	mmu_inval_dma_area(va, len_total);
	// XXX The mmu_map_dma_area does this for us below, see comments.
	// sparc_mapiorange(0, virt_to_phys(va), res->start, len_total);
	/*
	 * XXX That's where sdev would be used. Currently we load
	 * all iommu tables with the same translations.
	 */
	if (mmu_map_dma_area(dev, dma_addrp, va, res->start, len_total) != 0)
		goto err_noiommu;

	res->name = op->node->name;

	return (void *)(unsigned long)res->start;

err_noiommu:
	release_resource(res);
err_nova:
	free_pages(va, order);
err_nomem:
	kfree(res);
err_nopages:
	return NULL;
}

void sbus_free_consistent(struct device *dev, long n, void *p, u32 ba)
{
	struct resource *res;
	struct page *pgv;

	if ((res = _sparc_find_resource(&_sparc_dvma,
	    (unsigned long)p)) == NULL) {
		printk("sbus_free_consistent: cannot free %p\n", p);
		return;
	}

	if (((unsigned long)p & (PAGE_SIZE-1)) != 0) {
		printk("sbus_free_consistent: unaligned va %p\n", p);
		return;
	}

	n = (n + PAGE_SIZE-1) & PAGE_MASK;
	if ((res->end-res->start)+1 != n) {
		printk("sbus_free_consistent: region 0x%lx asked 0x%lx\n",
		    (long)((res->end-res->start)+1), n);
		return;
	}

	release_resource(res);
	kfree(res);

	/* mmu_inval_dma_area(va, n); */ /* it's consistent, isn't it */
	pgv = virt_to_page(p);
	mmu_unmap_dma_area(dev, ba, n);

	__free_pages(pgv, get_order(n));
}

/*
 * Map a chunk of memory so that devices can see it.
 * CPU view of this memory may be inconsistent with
 * a device view and explicit flushing is necessary.
 */
dma_addr_t sbus_map_single(struct device *dev, void *va, size_t len, int direction)
{
	/* XXX why are some lengths signed, others unsigned? */
	if (len <= 0) {
		return 0;
	}
	/* XXX So what is maxphys for us and how do drivers know it? */
	if (len > 256*1024) {			/* __get_free_pages() limit */
		return 0;
	}
	return mmu_get_scsi_one(dev, va, len);
}

void sbus_unmap_single(struct device *dev, dma_addr_t ba, size_t n, int direction)
{
	mmu_release_scsi_one(dev, ba, n);
}

int sbus_map_sg(struct device *dev, struct scatterlist *sg, int n, int direction)
{
	mmu_get_scsi_sgl(dev, sg, n);

	/*
	 * XXX sparc64 can return a partial length here. sun4c should do this
	 * but it currently panics if it can't fulfill the request - Anton
	 */
	return n;
}

void sbus_unmap_sg(struct device *dev, struct scatterlist *sg, int n, int direction)
{
	mmu_release_scsi_sgl(dev, sg, n);
}

void sbus_dma_sync_single_for_cpu(struct device *dev, dma_addr_t ba, size_t size, int direction)
{
}

void sbus_dma_sync_single_for_device(struct device *dev, dma_addr_t ba, size_t size, int direction)
{
}

/* Support code for sbus_init().  */
/*
 * XXX This functions appears to be a distorted version of
 * prom_sbus_ranges_init(), with all sun4d stuff cut away.
 * Ask DaveM what is going on here, how is sun4d supposed to work... XXX
 */
/* added back sun4d patch from Thomas Bogendoerfer - should be OK (crn) */
void __init sbus_arch_bus_ranges_init(struct device_node *pn, struct sbus_bus *sbus)
{
	int parent_node = pn->node;

	if (sparc_cpu_model == sun4d) {
		struct linux_prom_ranges iounit_ranges[PROMREG_MAX];
		int num_iounit_ranges, len;

		len = prom_getproperty(parent_node, "ranges",
				       (char *) iounit_ranges,
				       sizeof (iounit_ranges));
		if (len != -1) {
			num_iounit_ranges =
				(len / sizeof(struct linux_prom_ranges));
			prom_adjust_ranges(sbus->sbus_ranges,
					   sbus->num_sbus_ranges,
					   iounit_ranges, num_iounit_ranges);
		}
	}
}

void __init sbus_setup_iommu(struct sbus_bus *sbus, struct device_node *dp)
{
#ifndef CONFIG_SUN4
	struct device_node *parent = dp->parent;

	if (sparc_cpu_model != sun4d &&
	    parent != NULL &&
	    !strcmp(parent->name, "iommu"))
		iommu_init(parent, sbus);

	if (sparc_cpu_model == sun4d)
		iounit_init(sbus);
#endif
}

int __init sbus_arch_preinit(void)
{
	register_proc_sparc_ioport();

#ifdef CONFIG_SUN4
	{
		extern void sun4_dvma_init(void);
		sun4_dvma_init();
	}
	return 1;
#else
	return 0;
#endif
}

void __init sbus_arch_postinit(void)
{
	if (sparc_cpu_model == sun4d) {
		extern void sun4d_init_sbi_irq(void);
		sun4d_init_sbi_irq();
	}
}
#endif /* CONFIG_SBUS */

#ifdef CONFIG_PCI

/* Allocate and map kernel buffer using consistent mode DMA for a device.
 * hwdev should be valid struct pci_dev pointer for PCI devices.
 */
void *pci_alloc_consistent(struct pci_dev *pdev, size_t len, dma_addr_t *pba)
{
	unsigned long len_total = (len + PAGE_SIZE-1) & PAGE_MASK;
	unsigned long va;
	struct resource *res;
	int order;

	if (len == 0) {
		return NULL;
	}
	if (len > 256*1024) {			/* __get_free_pages() limit */
		return NULL;
	}

	order = get_order(len_total);
	va = __get_free_pages(GFP_KERNEL, order);
	if (va == 0) {
		printk("pci_alloc_consistent: no %ld pages\n", len_total>>PAGE_SHIFT);
		return NULL;
	}

	if ((res = kzalloc(sizeof(struct resource), GFP_KERNEL)) == NULL) {
		free_pages(va, order);
		printk("pci_alloc_consistent: no core\n");
		return NULL;
	}

	if (allocate_resource(&_sparc_dvma, res, len_total,
	    _sparc_dvma.start, _sparc_dvma.end, PAGE_SIZE, NULL, NULL) != 0) {
		printk("pci_alloc_consistent: cannot occupy 0x%lx", len_total);
		free_pages(va, order);
		kfree(res);
		return NULL;
	}
	mmu_inval_dma_area(va, len_total);
#if 0
/* P3 */ printk("pci_alloc_consistent: kva %lx uncva %lx phys %lx size %lx\n",
  (long)va, (long)res->start, (long)virt_to_phys(va), len_total);
#endif
	sparc_mapiorange(0, virt_to_phys(va), res->start, len_total);

	*pba = virt_to_phys(va); /* equals virt_to_bus (R.I.P.) for us. */
	return (void *) res->start;
}

/* Free and unmap a consistent DMA buffer.
 * cpu_addr is what was returned from pci_alloc_consistent,
 * size must be the same as what as passed into pci_alloc_consistent,
 * and likewise dma_addr must be the same as what *dma_addrp was set to.
 *
 * References to the memory and mappings associated with cpu_addr/dma_addr
 * past this call are illegal.
 */
void pci_free_consistent(struct pci_dev *pdev, size_t n, void *p, dma_addr_t ba)
{
	struct resource *res;
	unsigned long pgp;

	if ((res = _sparc_find_resource(&_sparc_dvma,
	    (unsigned long)p)) == NULL) {
		printk("pci_free_consistent: cannot free %p\n", p);
		return;
	}

	if (((unsigned long)p & (PAGE_SIZE-1)) != 0) {
		printk("pci_free_consistent: unaligned va %p\n", p);
		return;
	}

	n = (n + PAGE_SIZE-1) & PAGE_MASK;
	if ((res->end-res->start)+1 != n) {
		printk("pci_free_consistent: region 0x%lx asked 0x%lx\n",
		    (long)((res->end-res->start)+1), (long)n);
		return;
	}

	pgp = (unsigned long) phys_to_virt(ba);	/* bus_to_virt actually */
	mmu_inval_dma_area(pgp, n);
	sparc_unmapiorange((unsigned long)p, n);

	release_resource(res);
	kfree(res);

	free_pages(pgp, get_order(n));
}

/* Map a single buffer of the indicated size for DMA in streaming mode.
 * The 32-bit bus address to use is returned.
 *
 * Once the device is given the dma address, the device owns this memory
 * until either pci_unmap_single or pci_dma_sync_single_* is performed.
 */
dma_addr_t pci_map_single(struct pci_dev *hwdev, void *ptr, size_t size,
    int direction)
{
	BUG_ON(direction == PCI_DMA_NONE);
	/* IIep is write-through, not flushing. */
	return virt_to_phys(ptr);
}

/* Unmap a single streaming mode DMA translation.  The dma_addr and size
 * must match what was provided for in a previous pci_map_single call.  All
 * other usages are undefined.
 *
 * After this call, reads by the cpu to the buffer are guaranteed to see
 * whatever the device wrote there.
 */
void pci_unmap_single(struct pci_dev *hwdev, dma_addr_t ba, size_t size,
    int direction)
{
	BUG_ON(direction == PCI_DMA_NONE);
	if (direction != PCI_DMA_TODEVICE) {
		mmu_inval_dma_area((unsigned long)phys_to_virt(ba),
		    (size + PAGE_SIZE-1) & PAGE_MASK);
	}
}

/*
 * Same as pci_map_single, but with pages.
 */
dma_addr_t pci_map_page(struct pci_dev *hwdev, struct page *page,
			unsigned long offset, size_t size, int direction)
{
	BUG_ON(direction == PCI_DMA_NONE);
	/* IIep is write-through, not flushing. */
	return page_to_phys(page) + offset;
}

void pci_unmap_page(struct pci_dev *hwdev,
			dma_addr_t dma_address, size_t size, int direction)
{
	BUG_ON(direction == PCI_DMA_NONE);
	/* mmu_inval_dma_area XXX */
}

/* Map a set of buffers described by scatterlist in streaming
 * mode for DMA.  This is the scather-gather version of the
 * above pci_map_single interface.  Here the scatter gather list
 * elements are each tagged with the appropriate dma address
 * and length.  They are obtained via sg_dma_{address,length}(SG).
 *
 * NOTE: An implementation may be able to use a smaller number of
 *       DMA address/length pairs than there are SG table elements.
 *       (for example via virtual mapping capabilities)
 *       The routine returns the number of addr/length pairs actually
 *       used, at most nents.
 *
 * Device ownership issues as mentioned above for pci_map_single are
 * the same here.
 */
int pci_map_sg(struct pci_dev *hwdev, struct scatterlist *sgl, int nents,
    int direction)
{
	struct scatterlist *sg;
	int n;

	BUG_ON(direction == PCI_DMA_NONE);
	/* IIep is write-through, not flushing. */
	for_each_sg(sgl, sg, nents, n) {
		BUG_ON(page_address(sg_page(sg)) == NULL);
		sg->dvma_address = virt_to_phys(sg_virt(sg));
		sg->dvma_length = sg->length;
	}
	return nents;
}

/* Unmap a set of streaming mode DMA translations.
 * Again, cpu read rules concerning calls here are the same as for
 * pci_unmap_single() above.
 */
void pci_unmap_sg(struct pci_dev *hwdev, struct scatterlist *sgl, int nents,
    int direction)
{
	struct scatterlist *sg;
	int n;

	BUG_ON(direction == PCI_DMA_NONE);
	if (direction != PCI_DMA_TODEVICE) {
		for_each_sg(sgl, sg, nents, n) {
			BUG_ON(page_address(sg_page(sg)) == NULL);
			mmu_inval_dma_area(
			    (unsigned long) page_address(sg_page(sg)),
			    (sg->length + PAGE_SIZE-1) & PAGE_MASK);
		}
	}
}

/* Make physical memory consistent for a single
 * streaming mode DMA translation before or after a transfer.
 *
 * If you perform a pci_map_single() but wish to interrogate the
 * buffer using the cpu, yet do not wish to teardown the PCI dma
 * mapping, you must call this function before doing so.  At the
 * next point you give the PCI dma address back to the card, you
 * must first perform a pci_dma_sync_for_device, and then the
 * device again owns the buffer.
 */
void pci_dma_sync_single_for_cpu(struct pci_dev *hwdev, dma_addr_t ba, size_t size, int direction)
{
	BUG_ON(direction == PCI_DMA_NONE);
	if (direction != PCI_DMA_TODEVICE) {
		mmu_inval_dma_area((unsigned long)phys_to_virt(ba),
		    (size + PAGE_SIZE-1) & PAGE_MASK);
	}
}

void pci_dma_sync_single_for_device(struct pci_dev *hwdev, dma_addr_t ba, size_t size, int direction)
{
	BUG_ON(direction == PCI_DMA_NONE);
	if (direction != PCI_DMA_TODEVICE) {
		mmu_inval_dma_area((unsigned long)phys_to_virt(ba),
		    (size + PAGE_SIZE-1) & PAGE_MASK);
	}
}

/* Make physical memory consistent for a set of streaming
 * mode DMA translations after a transfer.
 *
 * The same as pci_dma_sync_single_* but for a scatter-gather list,
 * same rules and usage.
 */
void pci_dma_sync_sg_for_cpu(struct pci_dev *hwdev, struct scatterlist *sgl, int nents, int direction)
{
	struct scatterlist *sg;
	int n;

	BUG_ON(direction == PCI_DMA_NONE);
	if (direction != PCI_DMA_TODEVICE) {
		for_each_sg(sgl, sg, nents, n) {
			BUG_ON(page_address(sg_page(sg)) == NULL);
			mmu_inval_dma_area(
			    (unsigned long) page_address(sg_page(sg)),
			    (sg->length + PAGE_SIZE-1) & PAGE_MASK);
		}
	}
}

void pci_dma_sync_sg_for_device(struct pci_dev *hwdev, struct scatterlist *sgl, int nents, int direction)
{
	struct scatterlist *sg;
	int n;

	BUG_ON(direction == PCI_DMA_NONE);
	if (direction != PCI_DMA_TODEVICE) {
		for_each_sg(sgl, sg, nents, n) {
			BUG_ON(page_address(sg_page(sg)) == NULL);
			mmu_inval_dma_area(
			    (unsigned long) page_address(sg_page(sg)),
			    (sg->length + PAGE_SIZE-1) & PAGE_MASK);
		}
	}
}
#endif /* CONFIG_PCI */

#ifdef CONFIG_PROC_FS

static int
_sparc_io_get_info(char *buf, char **start, off_t fpos, int length, int *eof,
    void *data)
{
	char *p = buf, *e = buf + length;
	struct resource *r;
	const char *nm;

	for (r = ((struct resource *)data)->child; r != NULL; r = r->sibling) {
		if (p + 32 >= e)	/* Better than nothing */
			break;
		if ((nm = r->name) == 0) nm = "???";
		p += sprintf(p, "%016llx-%016llx: %s\n",
				(unsigned long long)r->start,
				(unsigned long long)r->end, nm);
	}

	return p-buf;
}

#endif /* CONFIG_PROC_FS */

/*
 * This is a version of find_resource and it belongs to kernel/resource.c.
 * Until we have agreement with Linus and Martin, it lingers here.
 *
 * XXX Too slow. Can have 8192 DVMA pages on sun4m in the worst case.
 * This probably warrants some sort of hashing.
 */
static struct resource *_sparc_find_resource(struct resource *root,
					     unsigned long hit)
{
        struct resource *tmp;

	for (tmp = root->child; tmp != 0; tmp = tmp->sibling) {
		if (tmp->start <= hit && tmp->end >= hit)
			return tmp;
	}
	return NULL;
}

static void register_proc_sparc_ioport(void)
{
#ifdef CONFIG_PROC_FS
	create_proc_read_entry("io_map",0,NULL,_sparc_io_get_info,&sparc_iomap);
	create_proc_read_entry("dvma_map",0,NULL,_sparc_io_get_info,&_sparc_dvma);
#endif
}
