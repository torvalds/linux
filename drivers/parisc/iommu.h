#ifndef _IOMMU_H
#define _IOMMU_H 1

#include <linux/pci.h>

struct parisc_device;
struct ioc;

static inline struct pci_hba_data *parisc_walk_tree(struct device *dev)
{
	struct device *otherdev;

	if (likely(dev->platform_data))
		return dev->platform_data;

	/* OK, just traverse the bus to find it */
	for (otherdev = dev->parent;
	     otherdev;
	     otherdev = otherdev->parent) {
		if (otherdev->platform_data) {
			dev->platform_data = otherdev->platform_data;
			break;
		}
	}

	return dev->platform_data;
}

static inline struct ioc *GET_IOC(struct device *dev)
{
	struct pci_hba_data *pdata = parisc_walk_tree(dev);

	if (!pdata)
		return NULL;
	return pdata->iommu;
}

#ifdef CONFIG_IOMMU_CCIO
void *ccio_get_iommu(const struct parisc_device *dev);
int ccio_request_resource(const struct parisc_device *dev,
		struct resource *res);
int ccio_allocate_resource(const struct parisc_device *dev,
		struct resource *res, unsigned long size,
		unsigned long min, unsigned long max, unsigned long align);
#else /* !CONFIG_IOMMU_CCIO */
#define ccio_get_iommu(dev) NULL
#define ccio_request_resource(dev, res) insert_resource(&iomem_resource, res)
#define ccio_allocate_resource(dev, res, size, min, max, align) \
		allocate_resource(&iomem_resource, res, size, min, max, \
				align, NULL, NULL)
#endif /* !CONFIG_IOMMU_CCIO */

void *sba_get_iommu(struct parisc_device *dev);

#endif /* _IOMMU_H */
