#include <linux/dma-mapping.h>
#include <linux/dmar.h>
#include <linux/bootmem.h>
#include <linux/pci.h>

#include <asm/proto.h>
#include <asm/dma.h>
#include <asm/gart.h>
#include <asm/calgary.h>

int forbid_dac __read_mostly;
EXPORT_SYMBOL(forbid_dac);

const struct dma_mapping_ops *dma_ops;
EXPORT_SYMBOL(dma_ops);

static int iommu_sac_force __read_mostly;

#ifdef CONFIG_IOMMU_DEBUG
int panic_on_overflow __read_mostly = 1;
int force_iommu __read_mostly = 1;
#else
int panic_on_overflow __read_mostly = 0;
int force_iommu __read_mostly = 0;
#endif

int iommu_merge __read_mostly = 0;

int no_iommu __read_mostly;
/* Set this to 1 if there is a HW IOMMU in the system */
int iommu_detected __read_mostly = 0;

/* This tells the BIO block layer to assume merging. Default to off
   because we cannot guarantee merging later. */
int iommu_bio_merge __read_mostly = 0;
EXPORT_SYMBOL(iommu_bio_merge);

dma_addr_t bad_dma_address __read_mostly = 0;
EXPORT_SYMBOL(bad_dma_address);

/* Dummy device used for NULL arguments (normally ISA). Better would
   be probably a smaller DMA mask, but this is bug-to-bug compatible
   to older i386. */
struct device fallback_dev = {
	.bus_id = "fallback device",
	.coherent_dma_mask = DMA_32BIT_MASK,
	.dma_mask = &fallback_dev.coherent_dma_mask,
};

int dma_set_mask(struct device *dev, u64 mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;

	*dev->dma_mask = mask;

	return 0;
}
EXPORT_SYMBOL(dma_set_mask);

#ifdef CONFIG_X86_64
static __initdata void *dma32_bootmem_ptr;
static unsigned long dma32_bootmem_size __initdata = (128ULL<<20);

static int __init parse_dma32_size_opt(char *p)
{
	if (!p)
		return -EINVAL;
	dma32_bootmem_size = memparse(p, &p);
	return 0;
}
early_param("dma32_size", parse_dma32_size_opt);

void __init dma32_reserve_bootmem(void)
{
	unsigned long size, align;
	if (end_pfn <= MAX_DMA32_PFN)
		return;

	align = 64ULL<<20;
	size = round_up(dma32_bootmem_size, align);
	dma32_bootmem_ptr = __alloc_bootmem_nopanic(size, align,
				 __pa(MAX_DMA_ADDRESS));
	if (dma32_bootmem_ptr)
		dma32_bootmem_size = size;
	else
		dma32_bootmem_size = 0;
}
static void __init dma32_free_bootmem(void)
{
	int node;

	if (end_pfn <= MAX_DMA32_PFN)
		return;

	if (!dma32_bootmem_ptr)
		return;

	for_each_online_node(node)
		free_bootmem_node(NODE_DATA(node), __pa(dma32_bootmem_ptr),
				  dma32_bootmem_size);

	dma32_bootmem_ptr = NULL;
	dma32_bootmem_size = 0;
}

void __init pci_iommu_alloc(void)
{
	/* free the range so iommu could get some range less than 4G */
	dma32_free_bootmem();
	/*
	 * The order of these functions is important for
	 * fall-back/fail-over reasons
	 */
#ifdef CONFIG_GART_IOMMU
	gart_iommu_hole_init();
#endif

#ifdef CONFIG_CALGARY_IOMMU
	detect_calgary();
#endif

	detect_intel_iommu();

#ifdef CONFIG_SWIOTLB
	pci_swiotlb_init();
#endif
}
#endif

/*
 * See <Documentation/x86_64/boot-options.txt> for the iommu kernel parameter
 * documentation.
 */
static __init int iommu_setup(char *p)
{
	iommu_merge = 1;

	if (!p)
		return -EINVAL;

	while (*p) {
		if (!strncmp(p, "off", 3))
			no_iommu = 1;
		/* gart_parse_options has more force support */
		if (!strncmp(p, "force", 5))
			force_iommu = 1;
		if (!strncmp(p, "noforce", 7)) {
			iommu_merge = 0;
			force_iommu = 0;
		}

		if (!strncmp(p, "biomerge", 8)) {
			iommu_bio_merge = 4096;
			iommu_merge = 1;
			force_iommu = 1;
		}
		if (!strncmp(p, "panic", 5))
			panic_on_overflow = 1;
		if (!strncmp(p, "nopanic", 7))
			panic_on_overflow = 0;
		if (!strncmp(p, "merge", 5)) {
			iommu_merge = 1;
			force_iommu = 1;
		}
		if (!strncmp(p, "nomerge", 7))
			iommu_merge = 0;
		if (!strncmp(p, "forcesac", 8))
			iommu_sac_force = 1;
		if (!strncmp(p, "allowdac", 8))
			forbid_dac = 0;
		if (!strncmp(p, "nodac", 5))
			forbid_dac = -1;
		if (!strncmp(p, "usedac", 6)) {
			forbid_dac = -1;
			return 1;
		}
#ifdef CONFIG_SWIOTLB
		if (!strncmp(p, "soft", 4))
			swiotlb = 1;
#endif

#ifdef CONFIG_GART_IOMMU
		gart_parse_options(p);
#endif

#ifdef CONFIG_CALGARY_IOMMU
		if (!strncmp(p, "calgary", 7))
			use_calgary = 1;
#endif /* CONFIG_CALGARY_IOMMU */

		p += strcspn(p, ",");
		if (*p == ',')
			++p;
	}
	return 0;
}
early_param("iommu", iommu_setup);

#ifdef CONFIG_X86_32
int dma_declare_coherent_memory(struct device *dev, dma_addr_t bus_addr,
				dma_addr_t device_addr, size_t size, int flags)
{
	void __iomem *mem_base = NULL;
	int pages = size >> PAGE_SHIFT;
	int bitmap_size = BITS_TO_LONGS(pages) * sizeof(long);

	if ((flags & (DMA_MEMORY_MAP | DMA_MEMORY_IO)) == 0)
		goto out;
	if (!size)
		goto out;
	if (dev->dma_mem)
		goto out;

	/* FIXME: this routine just ignores DMA_MEMORY_INCLUDES_CHILDREN */

	mem_base = ioremap(bus_addr, size);
	if (!mem_base)
		goto out;

	dev->dma_mem = kzalloc(sizeof(struct dma_coherent_mem), GFP_KERNEL);
	if (!dev->dma_mem)
		goto out;
	dev->dma_mem->bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!dev->dma_mem->bitmap)
		goto free1_out;

	dev->dma_mem->virt_base = mem_base;
	dev->dma_mem->device_base = device_addr;
	dev->dma_mem->size = pages;
	dev->dma_mem->flags = flags;

	if (flags & DMA_MEMORY_MAP)
		return DMA_MEMORY_MAP;

	return DMA_MEMORY_IO;

 free1_out:
	kfree(dev->dma_mem);
 out:
	if (mem_base)
		iounmap(mem_base);
	return 0;
}
EXPORT_SYMBOL(dma_declare_coherent_memory);

void dma_release_declared_memory(struct device *dev)
{
	struct dma_coherent_mem *mem = dev->dma_mem;

	if (!mem)
		return;
	dev->dma_mem = NULL;
	iounmap(mem->virt_base);
	kfree(mem->bitmap);
	kfree(mem);
}
EXPORT_SYMBOL(dma_release_declared_memory);

void *dma_mark_declared_memory_occupied(struct device *dev,
					dma_addr_t device_addr, size_t size)
{
	struct dma_coherent_mem *mem = dev->dma_mem;
	int pos, err;
	int pages = (size + (device_addr & ~PAGE_MASK) + PAGE_SIZE - 1);

	pages >>= PAGE_SHIFT;

	if (!mem)
		return ERR_PTR(-EINVAL);

	pos = (device_addr - mem->device_base) >> PAGE_SHIFT;
	err = bitmap_allocate_region(mem->bitmap, pos, get_order(pages));
	if (err != 0)
		return ERR_PTR(err);
	return mem->virt_base + (pos << PAGE_SHIFT);
}
EXPORT_SYMBOL(dma_mark_declared_memory_occupied);

static int dma_alloc_from_coherent_mem(struct device *dev, ssize_t size,
				       dma_addr_t *dma_handle, void **ret)
{
	struct dma_coherent_mem *mem = dev ? dev->dma_mem : NULL;
	int order = get_order(size);

	if (mem) {
		int page = bitmap_find_free_region(mem->bitmap, mem->size,
						     order);
		if (page >= 0) {
			*dma_handle = mem->device_base + (page << PAGE_SHIFT);
			*ret = mem->virt_base + (page << PAGE_SHIFT);
			memset(*ret, 0, size);
		}
		if (mem->flags & DMA_MEMORY_EXCLUSIVE)
			*ret = NULL;
	}
	return (mem != NULL);
}

static int dma_release_coherent(struct device *dev, int order, void *vaddr)
{
	struct dma_coherent_mem *mem = dev ? dev->dma_mem : NULL;

	if (mem && vaddr >= mem->virt_base && vaddr <
		   (mem->virt_base + (mem->size << PAGE_SHIFT))) {
		int page = (vaddr - mem->virt_base) >> PAGE_SHIFT;

		bitmap_release_region(mem->bitmap, page, order);
		return 1;
	}
	return 0;
}
#else
#define dma_alloc_from_coherent_mem(dev, size, handle, ret) (0)
#define dma_release_coherent(dev, order, vaddr) (0)
#endif /* CONFIG_X86_32 */

int dma_supported(struct device *dev, u64 mask)
{
#ifdef CONFIG_PCI
	if (mask > 0xffffffff && forbid_dac > 0) {
		printk(KERN_INFO "PCI: Disallowing DAC for device %s\n",
				 dev->bus_id);
		return 0;
	}
#endif

	if (dma_ops->dma_supported)
		return dma_ops->dma_supported(dev, mask);

	/* Copied from i386. Doesn't make much sense, because it will
	   only work for pci_alloc_coherent.
	   The caller just has to use GFP_DMA in this case. */
	if (mask < DMA_24BIT_MASK)
		return 0;

	/* Tell the device to use SAC when IOMMU force is on.  This
	   allows the driver to use cheaper accesses in some cases.

	   Problem with this is that if we overflow the IOMMU area and
	   return DAC as fallback address the device may not handle it
	   correctly.

	   As a special case some controllers have a 39bit address
	   mode that is as efficient as 32bit (aic79xx). Don't force
	   SAC for these.  Assume all masks <= 40 bits are of this
	   type. Normally this doesn't make any difference, but gives
	   more gentle handling of IOMMU overflow. */
	if (iommu_sac_force && (mask >= DMA_40BIT_MASK)) {
		printk(KERN_INFO "%s: Force SAC with mask %Lx\n",
				 dev->bus_id, mask);
		return 0;
	}

	return 1;
}
EXPORT_SYMBOL(dma_supported);

/* Allocate DMA memory on node near device */
noinline struct page *
dma_alloc_pages(struct device *dev, gfp_t gfp, unsigned order)
{
	int node;

	node = dev_to_node(dev);

	return alloc_pages_node(node, gfp, order);
}

/*
 * Allocate memory for a coherent mapping.
 */
void *
dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle,
		   gfp_t gfp)
{
	void *memory = NULL;
	struct page *page;
	unsigned long dma_mask = 0;
	dma_addr_t bus;

	/* ignore region specifiers */
	gfp &= ~(__GFP_DMA | __GFP_HIGHMEM | __GFP_DMA32);

	if (dma_alloc_from_coherent_mem(dev, size, dma_handle, &memory))
		return memory;

	if (!dev)
		dev = &fallback_dev;
	dma_mask = dev->coherent_dma_mask;
	if (dma_mask == 0)
		dma_mask = DMA_32BIT_MASK;

	/* Device not DMA able */
	if (dev->dma_mask == NULL)
		return NULL;

	/* Don't invoke OOM killer */
	gfp |= __GFP_NORETRY;

#ifdef CONFIG_X86_64
	/* Why <=? Even when the mask is smaller than 4GB it is often
	   larger than 16MB and in this case we have a chance of
	   finding fitting memory in the next higher zone first. If
	   not retry with true GFP_DMA. -AK */
	if (dma_mask <= DMA_32BIT_MASK)
		gfp |= GFP_DMA32;
#endif

 again:
	page = dma_alloc_pages(dev, gfp, get_order(size));
	if (page == NULL)
		return NULL;

	{
		int high, mmu;
		bus = page_to_phys(page);
		memory = page_address(page);
		high = (bus + size) >= dma_mask;
		mmu = high;
		if (force_iommu && !(gfp & GFP_DMA))
			mmu = 1;
		else if (high) {
			free_pages((unsigned long)memory,
				   get_order(size));

			/* Don't use the 16MB ZONE_DMA unless absolutely
			   needed. It's better to use remapping first. */
			if (dma_mask < DMA_32BIT_MASK && !(gfp & GFP_DMA)) {
				gfp = (gfp & ~GFP_DMA32) | GFP_DMA;
				goto again;
			}

			/* Let low level make its own zone decisions */
			gfp &= ~(GFP_DMA32|GFP_DMA);

			if (dma_ops->alloc_coherent)
				return dma_ops->alloc_coherent(dev, size,
							   dma_handle, gfp);
			return NULL;
		}

		memset(memory, 0, size);
		if (!mmu) {
			*dma_handle = bus;
			return memory;
		}
	}

	if (dma_ops->alloc_coherent) {
		free_pages((unsigned long)memory, get_order(size));
		gfp &= ~(GFP_DMA|GFP_DMA32);
		return dma_ops->alloc_coherent(dev, size, dma_handle, gfp);
	}

	if (dma_ops->map_simple) {
		*dma_handle = dma_ops->map_simple(dev, virt_to_phys(memory),
					      size,
					      PCI_DMA_BIDIRECTIONAL);
		if (*dma_handle != bad_dma_address)
			return memory;
	}

	if (panic_on_overflow)
		panic("dma_alloc_coherent: IOMMU overflow by %lu bytes\n",
		      (unsigned long)size);
	free_pages((unsigned long)memory, get_order(size));
	return NULL;
}
EXPORT_SYMBOL(dma_alloc_coherent);

/*
 * Unmap coherent memory.
 * The caller must ensure that the device has finished accessing the mapping.
 */
void dma_free_coherent(struct device *dev, size_t size,
			 void *vaddr, dma_addr_t bus)
{
	int order = get_order(size);
	WARN_ON(irqs_disabled());	/* for portability */
	if (dma_release_coherent(dev, order, vaddr))
		return;
	if (dma_ops->unmap_single)
		dma_ops->unmap_single(dev, bus, size, 0);
	free_pages((unsigned long)vaddr, order);
}
EXPORT_SYMBOL(dma_free_coherent);

static int __init pci_iommu_init(void)
{
#ifdef CONFIG_CALGARY_IOMMU
	calgary_iommu_init();
#endif

	intel_iommu_init();

#ifdef CONFIG_GART_IOMMU
	gart_iommu_init();
#endif

	no_iommu_init();
	return 0;
}

void pci_iommu_shutdown(void)
{
	gart_iommu_shutdown();
}
/* Must execute after PCI subsystem */
fs_initcall(pci_iommu_init);

#ifdef CONFIG_PCI
/* Many VIA bridges seem to corrupt data for DAC. Disable it here */

static __devinit void via_no_dac(struct pci_dev *dev)
{
	if ((dev->class >> 8) == PCI_CLASS_BRIDGE_PCI && forbid_dac == 0) {
		printk(KERN_INFO "PCI: VIA PCI bridge detected."
				 "Disabling DAC.\n");
		forbid_dac = 1;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA, PCI_ANY_ID, via_no_dac);
#endif
