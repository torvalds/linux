/*
 * Dynamic DMA mapping support.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/dmar.h>
#include <asm/io.h>
#include <asm/gart.h>
#include <asm/calgary.h>

int iommu_merge __read_mostly = 0;
EXPORT_SYMBOL(iommu_merge);

dma_addr_t bad_dma_address __read_mostly;
EXPORT_SYMBOL(bad_dma_address);

/* This tells the BIO block layer to assume merging. Default to off
   because we cannot guarantee merging later. */
int iommu_bio_merge __read_mostly = 0;
EXPORT_SYMBOL(iommu_bio_merge);

static int iommu_sac_force __read_mostly = 0;

int no_iommu __read_mostly;
#ifdef CONFIG_IOMMU_DEBUG
int panic_on_overflow __read_mostly = 1;
int force_iommu __read_mostly = 1;
#else
int panic_on_overflow __read_mostly = 0;
int force_iommu __read_mostly= 0;
#endif

/* Set this to 1 if there is a HW IOMMU in the system */
int iommu_detected __read_mostly = 0;

/* Dummy device used for NULL arguments (normally ISA). Better would
   be probably a smaller DMA mask, but this is bug-to-bug compatible
   to i386. */
struct device fallback_dev = {
	.bus_id = "fallback device",
	.coherent_dma_mask = DMA_32BIT_MASK,
	.dma_mask = &fallback_dev.coherent_dma_mask,
};

/* Allocate DMA memory on node near device */
noinline static void *
dma_alloc_pages(struct device *dev, gfp_t gfp, unsigned order)
{
	struct page *page;
	int node;

	node = dev_to_node(dev);
	if (node == -1)
		node = numa_node_id();

	if (node < first_node(node_online_map))
		node = first_node(node_online_map);

	page = alloc_pages_node(node, gfp, order);
	return page ? page_address(page) : NULL;
}

/*
 * Allocate memory for a coherent mapping.
 */
void *
dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle,
		   gfp_t gfp)
{
	void *memory;
	unsigned long dma_mask = 0;
	u64 bus;

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

	/* Kludge to make it bug-to-bug compatible with i386. i386
	   uses the normal dma_mask for alloc_coherent. */
	dma_mask &= *dev->dma_mask;

	/* Why <=? Even when the mask is smaller than 4GB it is often
	   larger than 16MB and in this case we have a chance of
	   finding fitting memory in the next higher zone first. If
	   not retry with true GFP_DMA. -AK */
	if (dma_mask <= DMA_32BIT_MASK)
		gfp |= GFP_DMA32;

 again:
	memory = dma_alloc_pages(dev, gfp, get_order(size));
	if (memory == NULL)
		return NULL;

	{
		int high, mmu;
		bus = virt_to_bus(memory);
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
			*dma_handle = virt_to_bus(memory);
			return memory;
		}
	}

	if (dma_ops->alloc_coherent) {
		free_pages((unsigned long)memory, get_order(size));
		gfp &= ~(GFP_DMA|GFP_DMA32);
		return dma_ops->alloc_coherent(dev, size, dma_handle, gfp);
	}

	if (dma_ops->map_simple) {
		*dma_handle = dma_ops->map_simple(dev, memory,
					      size,
					      PCI_DMA_BIDIRECTIONAL);
		if (*dma_handle != bad_dma_address)
			return memory;
	}

	if (panic_on_overflow)
		panic("dma_alloc_coherent: IOMMU overflow by %lu bytes\n",size);
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
	WARN_ON(irqs_disabled());	/* for portability */
	if (dma_ops->unmap_single)
		dma_ops->unmap_single(dev, bus, size, 0);
	free_pages((unsigned long)vaddr, get_order(size));
}
EXPORT_SYMBOL(dma_free_coherent);

static int forbid_dac __read_mostly;

int dma_supported(struct device *dev, u64 mask)
{
#ifdef CONFIG_PCI
	if (mask > 0xffffffff && forbid_dac > 0) {



		printk(KERN_INFO "PCI: Disallowing DAC for device %s\n", dev->bus_id);
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
		printk(KERN_INFO "%s: Force SAC with mask %Lx\n", dev->bus_id,mask);
		return 0;
	}

	return 1;
}
EXPORT_SYMBOL(dma_supported);

int dma_set_mask(struct device *dev, u64 mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;
	*dev->dma_mask = mask;
	return 0;
}
EXPORT_SYMBOL(dma_set_mask);

/*
 * See <Documentation/x86_64/boot-options.txt> for the iommu kernel parameter
 * documentation.
 */
__init int iommu_setup(char *p)
{
	iommu_merge = 1;

	if (!p)
		return -EINVAL;

	while (*p) {
		if (!strncmp(p,"off",3))
			no_iommu = 1;
		/* gart_parse_options has more force support */
		if (!strncmp(p,"force",5))
			force_iommu = 1;
		if (!strncmp(p,"noforce",7)) {
			iommu_merge = 0;
			force_iommu = 0;
		}

		if (!strncmp(p, "biomerge",8)) {
			iommu_bio_merge = 4096;
			iommu_merge = 1;
			force_iommu = 1;
		}
		if (!strncmp(p, "panic",5))
			panic_on_overflow = 1;
		if (!strncmp(p, "nopanic",7))
			panic_on_overflow = 0;
		if (!strncmp(p, "merge",5)) {
			iommu_merge = 1;
			force_iommu = 1;
		}
		if (!strncmp(p, "nomerge",7))
			iommu_merge = 0;
		if (!strncmp(p, "forcesac",8))
			iommu_sac_force = 1;
		if (!strncmp(p, "allowdac", 8))
			forbid_dac = 0;
		if (!strncmp(p, "nodac", 5))
			forbid_dac = -1;

#ifdef CONFIG_SWIOTLB
		if (!strncmp(p, "soft",4))
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

void __init pci_iommu_alloc(void)
{
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

#ifdef CONFIG_PCI
/* Many VIA bridges seem to corrupt data for DAC. Disable it here */

static __devinit void via_no_dac(struct pci_dev *dev)
{
	if ((dev->class >> 8) == PCI_CLASS_BRIDGE_PCI && forbid_dac == 0) {
		printk(KERN_INFO "PCI: VIA PCI bridge detected. Disabling DAC.\n");
		forbid_dac = 1;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA, PCI_ANY_ID, via_no_dac);
#endif
/* Must execute after PCI subsystem */
fs_initcall(pci_iommu_init);
