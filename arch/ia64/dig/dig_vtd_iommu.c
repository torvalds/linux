#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/intel-iommu.h>

void *
vtd_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle,
		 gfp_t flags)
{
	return intel_alloc_coherent(dev, size, dma_handle, flags);
}
EXPORT_SYMBOL_GPL(vtd_alloc_coherent);

void
vtd_free_coherent(struct device *dev, size_t size, void *vaddr,
		 dma_addr_t dma_handle)
{
	intel_free_coherent(dev, size, vaddr, dma_handle);
}
EXPORT_SYMBOL_GPL(vtd_free_coherent);

dma_addr_t
vtd_map_single_attrs(struct device *dev, void *addr, size_t size,
		     int dir, struct dma_attrs *attrs)
{
	return intel_map_single(dev, (phys_addr_t)addr, size, dir);
}
EXPORT_SYMBOL_GPL(vtd_map_single_attrs);

void
vtd_unmap_single_attrs(struct device *dev, dma_addr_t iova, size_t size,
		       int dir, struct dma_attrs *attrs)
{
	intel_unmap_single(dev, iova, size, dir);
}
EXPORT_SYMBOL_GPL(vtd_unmap_single_attrs);

int
vtd_map_sg_attrs(struct device *dev, struct scatterlist *sglist, int nents,
		 int dir, struct dma_attrs *attrs)
{
	return intel_map_sg(dev, sglist, nents, dir);
}
EXPORT_SYMBOL_GPL(vtd_map_sg_attrs);

void
vtd_unmap_sg_attrs(struct device *dev, struct scatterlist *sglist,
		   int nents, int dir, struct dma_attrs *attrs)
{
	intel_unmap_sg(dev, sglist, nents, dir);
}
EXPORT_SYMBOL_GPL(vtd_unmap_sg_attrs);

int
vtd_dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return 0;
}
EXPORT_SYMBOL_GPL(vtd_dma_mapping_error);
