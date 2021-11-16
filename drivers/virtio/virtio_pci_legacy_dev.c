// SPDX-License-Identifier: GPL-2.0-or-later

#include "linux/virtio_pci.h"
#include <linux/virtio_pci_legacy.h>
#include <linux/module.h>
#include <linux/pci.h>


/*
 * vp_legacy_probe: probe the legacy virtio pci device, note that the
 * caller is required to enable PCI device before calling this function.
 * @ldev: the legacy virtio-pci device
 *
 * Return 0 on succeed otherwise fail
 */
int vp_legacy_probe(struct virtio_pci_legacy_device *ldev)
{
	struct pci_dev *pci_dev = ldev->pci_dev;
	int rc;

	/* We only own devices >= 0x1000 and <= 0x103f: leave the rest. */
	if (pci_dev->device < 0x1000 || pci_dev->device > 0x103f)
		return -ENODEV;

	if (pci_dev->revision != VIRTIO_PCI_ABI_VERSION)
		return -ENODEV;

	rc = dma_set_mask(&pci_dev->dev, DMA_BIT_MASK(64));
	if (rc) {
		rc = dma_set_mask_and_coherent(&pci_dev->dev, DMA_BIT_MASK(32));
	} else {
		/*
		 * The virtio ring base address is expressed as a 32-bit PFN,
		 * with a page size of 1 << VIRTIO_PCI_QUEUE_ADDR_SHIFT.
		 */
		dma_set_coherent_mask(&pci_dev->dev,
				DMA_BIT_MASK(32 + VIRTIO_PCI_QUEUE_ADDR_SHIFT));
	}

	if (rc)
		dev_warn(&pci_dev->dev, "Failed to enable 64-bit or 32-bit DMA.  Trying to continue, but this might not work.\n");

	rc = pci_request_region(pci_dev, 0, "virtio-pci-legacy");
	if (rc)
		return rc;

	ldev->ioaddr = pci_iomap(pci_dev, 0, 0);
	if (!ldev->ioaddr)
		goto err_iomap;

	ldev->isr = ldev->ioaddr + VIRTIO_PCI_ISR;

	ldev->id.vendor = pci_dev->subsystem_vendor;
	ldev->id.device = pci_dev->subsystem_device;

	return 0;
err_iomap:
	pci_release_region(pci_dev, 0);
	return rc;
}
EXPORT_SYMBOL_GPL(vp_legacy_probe);

/*
 * vp_legacy_probe: remove and cleanup the legacy virtio pci device
 * @ldev: the legacy virtio-pci device
 */
void vp_legacy_remove(struct virtio_pci_legacy_device *ldev)
{
	struct pci_dev *pci_dev = ldev->pci_dev;

	pci_iounmap(pci_dev, ldev->ioaddr);
	pci_release_region(pci_dev, 0);
}
EXPORT_SYMBOL_GPL(vp_legacy_remove);

/*
 * vp_legacy_get_features - get features from device
 * @ldev: the legacy virtio-pci device
 *
 * Returns the features read from the device
 */
u64 vp_legacy_get_features(struct virtio_pci_legacy_device *ldev)
{

	return ioread32(ldev->ioaddr + VIRTIO_PCI_HOST_FEATURES);
}
EXPORT_SYMBOL_GPL(vp_legacy_get_features);

/*
 * vp_legacy_get_driver_features - get driver features from device
 * @ldev: the legacy virtio-pci device
 *
 * Returns the driver features read from the device
 */
u64 vp_legacy_get_driver_features(struct virtio_pci_legacy_device *ldev)
{
	return ioread32(ldev->ioaddr + VIRTIO_PCI_GUEST_FEATURES);
}
EXPORT_SYMBOL_GPL(vp_legacy_get_driver_features);

/*
 * vp_legacy_set_features - set features to device
 * @ldev: the legacy virtio-pci device
 * @features: the features set to device
 */
void vp_legacy_set_features(struct virtio_pci_legacy_device *ldev,
			    u32 features)
{
	iowrite32(features, ldev->ioaddr + VIRTIO_PCI_GUEST_FEATURES);
}
EXPORT_SYMBOL_GPL(vp_legacy_set_features);

/*
 * vp_legacy_get_status - get the device status
 * @ldev: the legacy virtio-pci device
 *
 * Returns the status read from device
 */
u8 vp_legacy_get_status(struct virtio_pci_legacy_device *ldev)
{
	return ioread8(ldev->ioaddr + VIRTIO_PCI_STATUS);
}
EXPORT_SYMBOL_GPL(vp_legacy_get_status);

/*
 * vp_legacy_set_status - set status to device
 * @ldev: the legacy virtio-pci device
 * @status: the status set to device
 */
void vp_legacy_set_status(struct virtio_pci_legacy_device *ldev,
				 u8 status)
{
	iowrite8(status, ldev->ioaddr + VIRTIO_PCI_STATUS);
}
EXPORT_SYMBOL_GPL(vp_legacy_set_status);

/*
 * vp_legacy_queue_vector - set the MSIX vector for a specific virtqueue
 * @ldev: the legacy virtio-pci device
 * @index: queue index
 * @vector: the config vector
 *
 * Returns the config vector read from the device
 */
u16 vp_legacy_queue_vector(struct virtio_pci_legacy_device *ldev,
			   u16 index, u16 vector)
{
	iowrite16(index, ldev->ioaddr + VIRTIO_PCI_QUEUE_SEL);
	iowrite16(vector, ldev->ioaddr + VIRTIO_MSI_QUEUE_VECTOR);
	/* Flush the write out to device */
	return ioread16(ldev->ioaddr + VIRTIO_MSI_QUEUE_VECTOR);
}
EXPORT_SYMBOL_GPL(vp_legacy_queue_vector);

/*
 * vp_legacy_config_vector - set the vector for config interrupt
 * @ldev: the legacy virtio-pci device
 * @vector: the config vector
 *
 * Returns the config vector read from the device
 */
u16 vp_legacy_config_vector(struct virtio_pci_legacy_device *ldev,
			    u16 vector)
{
	/* Setup the vector used for configuration events */
	iowrite16(vector, ldev->ioaddr + VIRTIO_MSI_CONFIG_VECTOR);
	/* Verify we had enough resources to assign the vector */
	/* Will also flush the write out to device */
	return ioread16(ldev->ioaddr + VIRTIO_MSI_CONFIG_VECTOR);
}
EXPORT_SYMBOL_GPL(vp_legacy_config_vector);

/*
 * vp_legacy_set_queue_address - set the virtqueue address
 * @ldev: the legacy virtio-pci device
 * @index: the queue index
 * @queue_pfn: pfn of the virtqueue
 */
void vp_legacy_set_queue_address(struct virtio_pci_legacy_device *ldev,
			     u16 index, u32 queue_pfn)
{
	iowrite16(index, ldev->ioaddr + VIRTIO_PCI_QUEUE_SEL);
	iowrite32(queue_pfn, ldev->ioaddr + VIRTIO_PCI_QUEUE_PFN);
}
EXPORT_SYMBOL_GPL(vp_legacy_set_queue_address);

/*
 * vp_legacy_get_queue_enable - enable a virtqueue
 * @ldev: the legacy virtio-pci device
 * @index: the queue index
 *
 * Returns whether a virtqueue is enabled or not
 */
bool vp_legacy_get_queue_enable(struct virtio_pci_legacy_device *ldev,
				u16 index)
{
	iowrite16(index, ldev->ioaddr + VIRTIO_PCI_QUEUE_SEL);
	return ioread32(ldev->ioaddr + VIRTIO_PCI_QUEUE_PFN);
}
EXPORT_SYMBOL_GPL(vp_legacy_get_queue_enable);

/*
 * vp_legacy_get_queue_size - get size for a virtqueue
 * @ldev: the legacy virtio-pci device
 * @index: the queue index
 *
 * Returns the size of the virtqueue
 */
u16 vp_legacy_get_queue_size(struct virtio_pci_legacy_device *ldev,
			     u16 index)
{
	iowrite16(index, ldev->ioaddr + VIRTIO_PCI_QUEUE_SEL);
	return ioread16(ldev->ioaddr + VIRTIO_PCI_QUEUE_NUM);
}
EXPORT_SYMBOL_GPL(vp_legacy_get_queue_size);

MODULE_VERSION("0.1");
MODULE_DESCRIPTION("Legacy Virtio PCI Device");
MODULE_AUTHOR("Wu Zongyong <wuzongyong@linux.alibaba.com>");
MODULE_LICENSE("GPL");
