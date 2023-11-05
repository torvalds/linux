// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/virtio_pci_modern.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

/*
 * vp_modern_map_capability - map a part of virtio pci capability
 * @mdev: the modern virtio-pci device
 * @off: offset of the capability
 * @minlen: minimal length of the capability
 * @align: align requirement
 * @start: start from the capability
 * @size: map size
 * @len: the length that is actually mapped
 * @pa: physical address of the capability
 *
 * Returns the io address of for the part of the capability
 */
static void __iomem *
vp_modern_map_capability(struct virtio_pci_modern_device *mdev, int off,
			 size_t minlen, u32 align, u32 start, u32 size,
			 size_t *len, resource_size_t *pa)
{
	struct pci_dev *dev = mdev->pci_dev;
	u8 bar;
	u32 offset, length;
	void __iomem *p;

	pci_read_config_byte(dev, off + offsetof(struct virtio_pci_cap,
						 bar),
			     &bar);
	pci_read_config_dword(dev, off + offsetof(struct virtio_pci_cap, offset),
			     &offset);
	pci_read_config_dword(dev, off + offsetof(struct virtio_pci_cap, length),
			      &length);

	/* Check if the BAR may have changed since we requested the region. */
	if (bar >= PCI_STD_NUM_BARS || !(mdev->modern_bars & (1 << bar))) {
		dev_err(&dev->dev,
			"virtio_pci: bar unexpectedly changed to %u\n", bar);
		return NULL;
	}

	if (length <= start) {
		dev_err(&dev->dev,
			"virtio_pci: bad capability len %u (>%u expected)\n",
			length, start);
		return NULL;
	}

	if (length - start < minlen) {
		dev_err(&dev->dev,
			"virtio_pci: bad capability len %u (>=%zu expected)\n",
			length, minlen);
		return NULL;
	}

	length -= start;

	if (start + offset < offset) {
		dev_err(&dev->dev,
			"virtio_pci: map wrap-around %u+%u\n",
			start, offset);
		return NULL;
	}

	offset += start;

	if (offset & (align - 1)) {
		dev_err(&dev->dev,
			"virtio_pci: offset %u not aligned to %u\n",
			offset, align);
		return NULL;
	}

	if (length > size)
		length = size;

	if (len)
		*len = length;

	if (minlen + offset < minlen ||
	    minlen + offset > pci_resource_len(dev, bar)) {
		dev_err(&dev->dev,
			"virtio_pci: map virtio %zu@%u "
			"out of range on bar %i length %lu\n",
			minlen, offset,
			bar, (unsigned long)pci_resource_len(dev, bar));
		return NULL;
	}

	p = pci_iomap_range(dev, bar, offset, length);
	if (!p)
		dev_err(&dev->dev,
			"virtio_pci: unable to map virtio %u@%u on bar %i\n",
			length, offset, bar);
	else if (pa)
		*pa = pci_resource_start(dev, bar) + offset;

	return p;
}

/**
 * virtio_pci_find_capability - walk capabilities to find device info.
 * @dev: the pci device
 * @cfg_type: the VIRTIO_PCI_CAP_* value we seek
 * @ioresource_types: IORESOURCE_MEM and/or IORESOURCE_IO.
 * @bars: the bitmask of BARs
 *
 * Returns offset of the capability, or 0.
 */
static inline int virtio_pci_find_capability(struct pci_dev *dev, u8 cfg_type,
					     u32 ioresource_types, int *bars)
{
	int pos;

	for (pos = pci_find_capability(dev, PCI_CAP_ID_VNDR);
	     pos > 0;
	     pos = pci_find_next_capability(dev, pos, PCI_CAP_ID_VNDR)) {
		u8 type, bar;
		pci_read_config_byte(dev, pos + offsetof(struct virtio_pci_cap,
							 cfg_type),
				     &type);
		pci_read_config_byte(dev, pos + offsetof(struct virtio_pci_cap,
							 bar),
				     &bar);

		/* Ignore structures with reserved BAR values */
		if (bar >= PCI_STD_NUM_BARS)
			continue;

		if (type == cfg_type) {
			if (pci_resource_len(dev, bar) &&
			    pci_resource_flags(dev, bar) & ioresource_types) {
				*bars |= (1 << bar);
				return pos;
			}
		}
	}
	return 0;
}

/* This is part of the ABI.  Don't screw with it. */
static inline void check_offsets(void)
{
	/* Note: disk space was harmed in compilation of this function. */
	BUILD_BUG_ON(VIRTIO_PCI_CAP_VNDR !=
		     offsetof(struct virtio_pci_cap, cap_vndr));
	BUILD_BUG_ON(VIRTIO_PCI_CAP_NEXT !=
		     offsetof(struct virtio_pci_cap, cap_next));
	BUILD_BUG_ON(VIRTIO_PCI_CAP_LEN !=
		     offsetof(struct virtio_pci_cap, cap_len));
	BUILD_BUG_ON(VIRTIO_PCI_CAP_CFG_TYPE !=
		     offsetof(struct virtio_pci_cap, cfg_type));
	BUILD_BUG_ON(VIRTIO_PCI_CAP_BAR !=
		     offsetof(struct virtio_pci_cap, bar));
	BUILD_BUG_ON(VIRTIO_PCI_CAP_OFFSET !=
		     offsetof(struct virtio_pci_cap, offset));
	BUILD_BUG_ON(VIRTIO_PCI_CAP_LENGTH !=
		     offsetof(struct virtio_pci_cap, length));
	BUILD_BUG_ON(VIRTIO_PCI_NOTIFY_CAP_MULT !=
		     offsetof(struct virtio_pci_notify_cap,
			      notify_off_multiplier));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_DFSELECT !=
		     offsetof(struct virtio_pci_common_cfg,
			      device_feature_select));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_DF !=
		     offsetof(struct virtio_pci_common_cfg, device_feature));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_GFSELECT !=
		     offsetof(struct virtio_pci_common_cfg,
			      guest_feature_select));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_GF !=
		     offsetof(struct virtio_pci_common_cfg, guest_feature));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_MSIX !=
		     offsetof(struct virtio_pci_common_cfg, msix_config));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_NUMQ !=
		     offsetof(struct virtio_pci_common_cfg, num_queues));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_STATUS !=
		     offsetof(struct virtio_pci_common_cfg, device_status));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_CFGGENERATION !=
		     offsetof(struct virtio_pci_common_cfg, config_generation));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_SELECT !=
		     offsetof(struct virtio_pci_common_cfg, queue_select));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_SIZE !=
		     offsetof(struct virtio_pci_common_cfg, queue_size));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_MSIX !=
		     offsetof(struct virtio_pci_common_cfg, queue_msix_vector));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_ENABLE !=
		     offsetof(struct virtio_pci_common_cfg, queue_enable));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_NOFF !=
		     offsetof(struct virtio_pci_common_cfg, queue_notify_off));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_DESCLO !=
		     offsetof(struct virtio_pci_common_cfg, queue_desc_lo));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_DESCHI !=
		     offsetof(struct virtio_pci_common_cfg, queue_desc_hi));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_AVAILLO !=
		     offsetof(struct virtio_pci_common_cfg, queue_avail_lo));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_AVAILHI !=
		     offsetof(struct virtio_pci_common_cfg, queue_avail_hi));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_USEDLO !=
		     offsetof(struct virtio_pci_common_cfg, queue_used_lo));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_USEDHI !=
		     offsetof(struct virtio_pci_common_cfg, queue_used_hi));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_NDATA !=
		     offsetof(struct virtio_pci_modern_common_cfg, queue_notify_data));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_RESET !=
		     offsetof(struct virtio_pci_modern_common_cfg, queue_reset));
}

/*
 * vp_modern_probe: probe the modern virtio pci device, note that the
 * caller is required to enable PCI device before calling this function.
 * @mdev: the modern virtio-pci device
 *
 * Return 0 on succeed otherwise fail
 */
int vp_modern_probe(struct virtio_pci_modern_device *mdev)
{
	struct pci_dev *pci_dev = mdev->pci_dev;
	int err, common, isr, notify, device;
	u32 notify_length;
	u32 notify_offset;
	int devid;

	check_offsets();

	if (mdev->device_id_check) {
		devid = mdev->device_id_check(pci_dev);
		if (devid < 0)
			return devid;
		mdev->id.device = devid;
	} else {
		/* We only own devices >= 0x1000 and <= 0x107f: leave the rest. */
		if (pci_dev->device < 0x1000 || pci_dev->device > 0x107f)
			return -ENODEV;

		if (pci_dev->device < 0x1040) {
			/* Transitional devices: use the PCI subsystem device id as
			 * virtio device id, same as legacy driver always did.
			 */
			mdev->id.device = pci_dev->subsystem_device;
		} else {
			/* Modern devices: simply use PCI device id, but start from 0x1040. */
			mdev->id.device = pci_dev->device - 0x1040;
		}
	}
	mdev->id.vendor = pci_dev->subsystem_vendor;

	/* check for a common config: if not, use legacy mode (bar 0). */
	common = virtio_pci_find_capability(pci_dev, VIRTIO_PCI_CAP_COMMON_CFG,
					    IORESOURCE_IO | IORESOURCE_MEM,
					    &mdev->modern_bars);
	if (!common) {
		dev_info(&pci_dev->dev,
			 "virtio_pci: leaving for legacy driver\n");
		return -ENODEV;
	}

	/* If common is there, these should be too... */
	isr = virtio_pci_find_capability(pci_dev, VIRTIO_PCI_CAP_ISR_CFG,
					 IORESOURCE_IO | IORESOURCE_MEM,
					 &mdev->modern_bars);
	notify = virtio_pci_find_capability(pci_dev, VIRTIO_PCI_CAP_NOTIFY_CFG,
					    IORESOURCE_IO | IORESOURCE_MEM,
					    &mdev->modern_bars);
	if (!isr || !notify) {
		dev_err(&pci_dev->dev,
			"virtio_pci: missing capabilities %i/%i/%i\n",
			common, isr, notify);
		return -EINVAL;
	}

	err = dma_set_mask_and_coherent(&pci_dev->dev,
					mdev->dma_mask ? : DMA_BIT_MASK(64));
	if (err)
		err = dma_set_mask_and_coherent(&pci_dev->dev,
						DMA_BIT_MASK(32));
	if (err)
		dev_warn(&pci_dev->dev, "Failed to enable 64-bit or 32-bit DMA.  Trying to continue, but this might not work.\n");

	/* Device capability is only mandatory for devices that have
	 * device-specific configuration.
	 */
	device = virtio_pci_find_capability(pci_dev, VIRTIO_PCI_CAP_DEVICE_CFG,
					    IORESOURCE_IO | IORESOURCE_MEM,
					    &mdev->modern_bars);

	err = pci_request_selected_regions(pci_dev, mdev->modern_bars,
					   "virtio-pci-modern");
	if (err)
		return err;

	err = -EINVAL;
	mdev->common = vp_modern_map_capability(mdev, common,
				      sizeof(struct virtio_pci_common_cfg), 4,
				      0, sizeof(struct virtio_pci_modern_common_cfg),
				      &mdev->common_len, NULL);
	if (!mdev->common)
		goto err_map_common;
	mdev->isr = vp_modern_map_capability(mdev, isr, sizeof(u8), 1,
					     0, 1,
					     NULL, NULL);
	if (!mdev->isr)
		goto err_map_isr;

	/* Read notify_off_multiplier from config space. */
	pci_read_config_dword(pci_dev,
			      notify + offsetof(struct virtio_pci_notify_cap,
						notify_off_multiplier),
			      &mdev->notify_offset_multiplier);
	/* Read notify length and offset from config space. */
	pci_read_config_dword(pci_dev,
			      notify + offsetof(struct virtio_pci_notify_cap,
						cap.length),
			      &notify_length);

	pci_read_config_dword(pci_dev,
			      notify + offsetof(struct virtio_pci_notify_cap,
						cap.offset),
			      &notify_offset);

	/* We don't know how many VQs we'll map, ahead of the time.
	 * If notify length is small, map it all now.
	 * Otherwise, map each VQ individually later.
	 */
	if ((u64)notify_length + (notify_offset % PAGE_SIZE) <= PAGE_SIZE) {
		mdev->notify_base = vp_modern_map_capability(mdev, notify,
							     2, 2,
							     0, notify_length,
							     &mdev->notify_len,
							     &mdev->notify_pa);
		if (!mdev->notify_base)
			goto err_map_notify;
	} else {
		mdev->notify_map_cap = notify;
	}

	/* Again, we don't know how much we should map, but PAGE_SIZE
	 * is more than enough for all existing devices.
	 */
	if (device) {
		mdev->device = vp_modern_map_capability(mdev, device, 0, 4,
							0, PAGE_SIZE,
							&mdev->device_len,
							NULL);
		if (!mdev->device)
			goto err_map_device;
	}

	return 0;

err_map_device:
	if (mdev->notify_base)
		pci_iounmap(pci_dev, mdev->notify_base);
err_map_notify:
	pci_iounmap(pci_dev, mdev->isr);
err_map_isr:
	pci_iounmap(pci_dev, mdev->common);
err_map_common:
	pci_release_selected_regions(pci_dev, mdev->modern_bars);
	return err;
}
EXPORT_SYMBOL_GPL(vp_modern_probe);

/*
 * vp_modern_remove: remove and cleanup the modern virtio pci device
 * @mdev: the modern virtio-pci device
 */
void vp_modern_remove(struct virtio_pci_modern_device *mdev)
{
	struct pci_dev *pci_dev = mdev->pci_dev;

	if (mdev->device)
		pci_iounmap(pci_dev, mdev->device);
	if (mdev->notify_base)
		pci_iounmap(pci_dev, mdev->notify_base);
	pci_iounmap(pci_dev, mdev->isr);
	pci_iounmap(pci_dev, mdev->common);
	pci_release_selected_regions(pci_dev, mdev->modern_bars);
}
EXPORT_SYMBOL_GPL(vp_modern_remove);

/*
 * vp_modern_get_features - get features from device
 * @mdev: the modern virtio-pci device
 *
 * Returns the features read from the device
 */
u64 vp_modern_get_features(struct virtio_pci_modern_device *mdev)
{
	struct virtio_pci_common_cfg __iomem *cfg = mdev->common;

	u64 features;

	vp_iowrite32(0, &cfg->device_feature_select);
	features = vp_ioread32(&cfg->device_feature);
	vp_iowrite32(1, &cfg->device_feature_select);
	features |= ((u64)vp_ioread32(&cfg->device_feature) << 32);

	return features;
}
EXPORT_SYMBOL_GPL(vp_modern_get_features);

/*
 * vp_modern_get_driver_features - get driver features from device
 * @mdev: the modern virtio-pci device
 *
 * Returns the driver features read from the device
 */
u64 vp_modern_get_driver_features(struct virtio_pci_modern_device *mdev)
{
	struct virtio_pci_common_cfg __iomem *cfg = mdev->common;

	u64 features;

	vp_iowrite32(0, &cfg->guest_feature_select);
	features = vp_ioread32(&cfg->guest_feature);
	vp_iowrite32(1, &cfg->guest_feature_select);
	features |= ((u64)vp_ioread32(&cfg->guest_feature) << 32);

	return features;
}
EXPORT_SYMBOL_GPL(vp_modern_get_driver_features);

/*
 * vp_modern_set_features - set features to device
 * @mdev: the modern virtio-pci device
 * @features: the features set to device
 */
void vp_modern_set_features(struct virtio_pci_modern_device *mdev,
			    u64 features)
{
	struct virtio_pci_common_cfg __iomem *cfg = mdev->common;

	vp_iowrite32(0, &cfg->guest_feature_select);
	vp_iowrite32((u32)features, &cfg->guest_feature);
	vp_iowrite32(1, &cfg->guest_feature_select);
	vp_iowrite32(features >> 32, &cfg->guest_feature);
}
EXPORT_SYMBOL_GPL(vp_modern_set_features);

/*
 * vp_modern_generation - get the device genreation
 * @mdev: the modern virtio-pci device
 *
 * Returns the genreation read from device
 */
u32 vp_modern_generation(struct virtio_pci_modern_device *mdev)
{
	struct virtio_pci_common_cfg __iomem *cfg = mdev->common;

	return vp_ioread8(&cfg->config_generation);
}
EXPORT_SYMBOL_GPL(vp_modern_generation);

/*
 * vp_modern_get_status - get the device status
 * @mdev: the modern virtio-pci device
 *
 * Returns the status read from device
 */
u8 vp_modern_get_status(struct virtio_pci_modern_device *mdev)
{
	struct virtio_pci_common_cfg __iomem *cfg = mdev->common;

	return vp_ioread8(&cfg->device_status);
}
EXPORT_SYMBOL_GPL(vp_modern_get_status);

/*
 * vp_modern_set_status - set status to device
 * @mdev: the modern virtio-pci device
 * @status: the status set to device
 */
void vp_modern_set_status(struct virtio_pci_modern_device *mdev,
				 u8 status)
{
	struct virtio_pci_common_cfg __iomem *cfg = mdev->common;

	/*
	 * Per memory-barriers.txt, wmb() is not needed to guarantee
	 * that the cache coherent memory writes have completed
	 * before writing to the MMIO region.
	 */
	vp_iowrite8(status, &cfg->device_status);
}
EXPORT_SYMBOL_GPL(vp_modern_set_status);

/*
 * vp_modern_get_queue_reset - get the queue reset status
 * @mdev: the modern virtio-pci device
 * @index: queue index
 */
int vp_modern_get_queue_reset(struct virtio_pci_modern_device *mdev, u16 index)
{
	struct virtio_pci_modern_common_cfg __iomem *cfg;

	cfg = (struct virtio_pci_modern_common_cfg __iomem *)mdev->common;

	vp_iowrite16(index, &cfg->cfg.queue_select);
	return vp_ioread16(&cfg->queue_reset);
}
EXPORT_SYMBOL_GPL(vp_modern_get_queue_reset);

/*
 * vp_modern_set_queue_reset - reset the queue
 * @mdev: the modern virtio-pci device
 * @index: queue index
 */
void vp_modern_set_queue_reset(struct virtio_pci_modern_device *mdev, u16 index)
{
	struct virtio_pci_modern_common_cfg __iomem *cfg;

	cfg = (struct virtio_pci_modern_common_cfg __iomem *)mdev->common;

	vp_iowrite16(index, &cfg->cfg.queue_select);
	vp_iowrite16(1, &cfg->queue_reset);

	while (vp_ioread16(&cfg->queue_reset))
		msleep(1);

	while (vp_ioread16(&cfg->cfg.queue_enable))
		msleep(1);
}
EXPORT_SYMBOL_GPL(vp_modern_set_queue_reset);

/*
 * vp_modern_queue_vector - set the MSIX vector for a specific virtqueue
 * @mdev: the modern virtio-pci device
 * @index: queue index
 * @vector: the config vector
 *
 * Returns the config vector read from the device
 */
u16 vp_modern_queue_vector(struct virtio_pci_modern_device *mdev,
			   u16 index, u16 vector)
{
	struct virtio_pci_common_cfg __iomem *cfg = mdev->common;

	vp_iowrite16(index, &cfg->queue_select);
	vp_iowrite16(vector, &cfg->queue_msix_vector);
	/* Flush the write out to device */
	return vp_ioread16(&cfg->queue_msix_vector);
}
EXPORT_SYMBOL_GPL(vp_modern_queue_vector);

/*
 * vp_modern_config_vector - set the vector for config interrupt
 * @mdev: the modern virtio-pci device
 * @vector: the config vector
 *
 * Returns the config vector read from the device
 */
u16 vp_modern_config_vector(struct virtio_pci_modern_device *mdev,
			    u16 vector)
{
	struct virtio_pci_common_cfg __iomem *cfg = mdev->common;

	/* Setup the vector used for configuration events */
	vp_iowrite16(vector, &cfg->msix_config);
	/* Verify we had enough resources to assign the vector */
	/* Will also flush the write out to device */
	return vp_ioread16(&cfg->msix_config);
}
EXPORT_SYMBOL_GPL(vp_modern_config_vector);

/*
 * vp_modern_queue_address - set the virtqueue address
 * @mdev: the modern virtio-pci device
 * @index: the queue index
 * @desc_addr: address of the descriptor area
 * @driver_addr: address of the driver area
 * @device_addr: address of the device area
 */
void vp_modern_queue_address(struct virtio_pci_modern_device *mdev,
			     u16 index, u64 desc_addr, u64 driver_addr,
			     u64 device_addr)
{
	struct virtio_pci_common_cfg __iomem *cfg = mdev->common;

	vp_iowrite16(index, &cfg->queue_select);

	vp_iowrite64_twopart(desc_addr, &cfg->queue_desc_lo,
			     &cfg->queue_desc_hi);
	vp_iowrite64_twopart(driver_addr, &cfg->queue_avail_lo,
			     &cfg->queue_avail_hi);
	vp_iowrite64_twopart(device_addr, &cfg->queue_used_lo,
			     &cfg->queue_used_hi);
}
EXPORT_SYMBOL_GPL(vp_modern_queue_address);

/*
 * vp_modern_set_queue_enable - enable a virtqueue
 * @mdev: the modern virtio-pci device
 * @index: the queue index
 * @enable: whether the virtqueue is enable or not
 */
void vp_modern_set_queue_enable(struct virtio_pci_modern_device *mdev,
				u16 index, bool enable)
{
	vp_iowrite16(index, &mdev->common->queue_select);
	vp_iowrite16(enable, &mdev->common->queue_enable);
}
EXPORT_SYMBOL_GPL(vp_modern_set_queue_enable);

/*
 * vp_modern_get_queue_enable - enable a virtqueue
 * @mdev: the modern virtio-pci device
 * @index: the queue index
 *
 * Returns whether a virtqueue is enabled or not
 */
bool vp_modern_get_queue_enable(struct virtio_pci_modern_device *mdev,
				u16 index)
{
	vp_iowrite16(index, &mdev->common->queue_select);

	return vp_ioread16(&mdev->common->queue_enable);
}
EXPORT_SYMBOL_GPL(vp_modern_get_queue_enable);

/*
 * vp_modern_set_queue_size - set size for a virtqueue
 * @mdev: the modern virtio-pci device
 * @index: the queue index
 * @size: the size of the virtqueue
 */
void vp_modern_set_queue_size(struct virtio_pci_modern_device *mdev,
			      u16 index, u16 size)
{
	vp_iowrite16(index, &mdev->common->queue_select);
	vp_iowrite16(size, &mdev->common->queue_size);

}
EXPORT_SYMBOL_GPL(vp_modern_set_queue_size);

/*
 * vp_modern_get_queue_size - get size for a virtqueue
 * @mdev: the modern virtio-pci device
 * @index: the queue index
 *
 * Returns the size of the virtqueue
 */
u16 vp_modern_get_queue_size(struct virtio_pci_modern_device *mdev,
			     u16 index)
{
	vp_iowrite16(index, &mdev->common->queue_select);

	return vp_ioread16(&mdev->common->queue_size);

}
EXPORT_SYMBOL_GPL(vp_modern_get_queue_size);

/*
 * vp_modern_get_num_queues - get the number of virtqueues
 * @mdev: the modern virtio-pci device
 *
 * Returns the number of virtqueues
 */
u16 vp_modern_get_num_queues(struct virtio_pci_modern_device *mdev)
{
	return vp_ioread16(&mdev->common->num_queues);
}
EXPORT_SYMBOL_GPL(vp_modern_get_num_queues);

/*
 * vp_modern_get_queue_notify_off - get notification offset for a virtqueue
 * @mdev: the modern virtio-pci device
 * @index: the queue index
 *
 * Returns the notification offset for a virtqueue
 */
static u16 vp_modern_get_queue_notify_off(struct virtio_pci_modern_device *mdev,
					  u16 index)
{
	vp_iowrite16(index, &mdev->common->queue_select);

	return vp_ioread16(&mdev->common->queue_notify_off);
}

/*
 * vp_modern_map_vq_notify - map notification area for a
 * specific virtqueue
 * @mdev: the modern virtio-pci device
 * @index: the queue index
 * @pa: the pointer to the physical address of the nofity area
 *
 * Returns the address of the notification area
 */
void __iomem *vp_modern_map_vq_notify(struct virtio_pci_modern_device *mdev,
				      u16 index, resource_size_t *pa)
{
	u16 off = vp_modern_get_queue_notify_off(mdev, index);

	if (mdev->notify_base) {
		/* offset should not wrap */
		if ((u64)off * mdev->notify_offset_multiplier + 2
			> mdev->notify_len) {
			dev_warn(&mdev->pci_dev->dev,
				 "bad notification offset %u (x %u) "
				 "for queue %u > %zd",
				 off, mdev->notify_offset_multiplier,
				 index, mdev->notify_len);
			return NULL;
		}
		if (pa)
			*pa = mdev->notify_pa +
			      off * mdev->notify_offset_multiplier;
		return mdev->notify_base + off * mdev->notify_offset_multiplier;
	} else {
		return vp_modern_map_capability(mdev,
				       mdev->notify_map_cap, 2, 2,
				       off * mdev->notify_offset_multiplier, 2,
				       NULL, pa);
	}
}
EXPORT_SYMBOL_GPL(vp_modern_map_vq_notify);

MODULE_VERSION("0.1");
MODULE_DESCRIPTION("Modern Virtio PCI Device");
MODULE_AUTHOR("Jason Wang <jasowang@redhat.com>");
MODULE_LICENSE("GPL");
