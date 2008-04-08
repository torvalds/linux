#include <linux/dma-mapping.h>
#include <linux/dmar.h>
#include <linux/bootmem.h>

#include <asm/proto.h>
#include <asm/dma.h>
#include <asm/gart.h>
#include <asm/calgary.h>

const struct dma_mapping_ops *dma_ops;
EXPORT_SYMBOL(dma_ops);

#ifdef CONFIG_IOMMU_DEBUG
int panic_on_overflow __read_mostly = 1;
int force_iommu __read_mostly = 1;
#else
int panic_on_overflow __read_mostly = 0;
int force_iommu __read_mostly = 0;
#endif

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
