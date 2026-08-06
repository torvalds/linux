// SPDX-License-Identifier: GPL-2.0-only
/*
 * ZTE DingHai Ethernet driver
 * Copyright (c) 2022-2026, ZTE Corporation.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <net/devlink.h>
#include <linux/dma-mapping.h>
#include "en_pf.h"
#include "dh_queue.h"

MODULE_AUTHOR("Junyang Han <han.junyang@zte.com.cn>");
MODULE_DESCRIPTION("ZTE DingHai series Ethernet driver");
MODULE_LICENSE("GPL");

static const struct devlink_ops zxdh_pf_devlink_ops = {};

static const struct pci_device_id zxdh_pf_pci_table[] = {
	{ PCI_DEVICE(ZXDH_PF_VENDOR_ID, ZXDH_PF_DEVICE_ID) },
	{ PCI_DEVICE(ZXDH_PF_VENDOR_ID, ZXDH_VF_DEVICE_ID) },
	{ }
};

MODULE_DEVICE_TABLE(pci, zxdh_pf_pci_table);

void *zxdh_core_alloc_priv(struct zxdh_core_dev *zxdh_dev, size_t size)
{
	void *priv = kzalloc(size, GFP_KERNEL);

	if (priv)
		zxdh_dev->priv = priv;
	return priv;
}

void zxdh_core_free_priv(struct zxdh_core_dev *zxdh_dev)
{
	kfree(zxdh_dev->priv);
}

static int zxdh_pf_pci_init(struct zxdh_core_dev *zxdh_dev)
{
	struct zxdh_pf_dev *pf_dev = zxdh_dev->priv;
	int ret;

	pci_set_drvdata(zxdh_dev->pdev, zxdh_dev);

	ret = pci_enable_device(zxdh_dev->pdev);
	if (ret) {
		dev_err(zxdh_dev->device, "pci_enable_device failed: %d\n", ret);
		return ret;
	}

	dma_set_mask_and_coherent(zxdh_dev->device, DMA_BIT_MASK(64));

	ret = pci_request_selected_regions(zxdh_dev->pdev,
					   pci_select_bars(zxdh_dev->pdev, IORESOURCE_MEM),
					   "dh-pf");
	if (ret) {
		dev_err(zxdh_dev->device, "pci_request_selected_regions failed: %d\n", ret);
		goto err_pci;
	}

	pci_set_master(zxdh_dev->pdev);
	ret = pci_save_state(zxdh_dev->pdev);
	if (ret) {
		dev_err(zxdh_dev->device, "pci_save_state failed: %d\n", ret);
		goto err_pci_save_state;
	}

	if (!(pci_resource_flags(zxdh_dev->pdev, 0) & IORESOURCE_MEM)) {
		ret = -ENODEV;
		dev_err(zxdh_dev->device, "BAR 0 is not an MMIO resource\n");
		goto err_pci_save_state;
	}

	pf_dev->pci_ioremap_addr[0] =
		ioremap(pci_resource_start(zxdh_dev->pdev, 0),
			pci_resource_len(zxdh_dev->pdev, 0));
	if (!pf_dev->pci_ioremap_addr[0]) {
		ret = -ENOMEM;
		dev_err(zxdh_dev->device, "dh pf pci ioremap failed\n");
		goto err_pci_save_state;
	}

	return 0;

err_pci_save_state:
	pci_release_selected_regions(zxdh_dev->pdev,
				     pci_select_bars(zxdh_dev->pdev, IORESOURCE_MEM));
err_pci:
	pci_disable_device(zxdh_dev->pdev);
	return ret;
}

void zxdh_pf_pci_close(struct zxdh_core_dev *zxdh_dev)
{
	struct zxdh_pf_dev *pf_dev = zxdh_dev->priv;

	iounmap(pf_dev->pci_ioremap_addr[0]);
	pci_release_selected_regions(zxdh_dev->pdev,
				     pci_select_bars(zxdh_dev->pdev, IORESOURCE_MEM));
	pci_disable_device(zxdh_dev->pdev);
}

int zxdh_pf_pci_find_capability(struct pci_dev *pdev, u8 cfg_type,
				u32 ioresource_types, int *bars)
{
	int pos;
	u8 type;
	u8 bar;

	for (pos = pci_find_capability(pdev, PCI_CAP_ID_VNDR); pos > 0;
	     pos = pci_find_next_capability(pdev, pos, PCI_CAP_ID_VNDR)) {
		pci_read_config_byte(pdev,
				     pos + offsetof(struct zxdh_pf_pci_cap,
							cfg_type), &type);
		pci_read_config_byte(pdev,
				     pos + offsetof(struct zxdh_pf_pci_cap, bar), &bar);

		/* ignore structures with reserved BAR values */
		if (bar > ZXDH_PF_MAX_BAR_VAL)
			continue;

		if (type == cfg_type) {
			if (pci_resource_len(pdev, bar) &&
			    pci_resource_flags(pdev, bar) & ioresource_types) {
				*bars |= (1 << bar);
				return pos;
			}
		}
	}

	return 0;
}

void __iomem *zxdh_pf_map_capability(struct zxdh_core_dev *zxdh_dev, int off,
				     size_t minlen, u32 align,
				     u32 start, u32 size,
				     size_t *len, resource_size_t *pa,
				     u32 *bar_off)
{
	struct pci_dev *pdev = zxdh_dev->pdev;
	void __iomem *p;
	u32 offset;
	u32 length;
	u8 bar;

	pci_read_config_byte(pdev,
			     off + offsetof(struct zxdh_pf_pci_cap, bar), &bar);

	if (bar > ZXDH_PF_MAX_BAR_VAL) {
		dev_err(zxdh_dev->device, "invalid bar %u\n", bar);
		return NULL;
	}

	pci_read_config_dword(pdev,
			      off + offsetof(struct zxdh_pf_pci_cap,
						offset), &offset);
	pci_read_config_dword(pdev,
			      off + offsetof(struct zxdh_pf_pci_cap,
						length), &length);

	if (bar_off)
		*bar_off = offset;

	if (length <= start) {
		dev_err(zxdh_dev->device, "bad capability len %u (>%u expected)\n",
			length, start);
		return NULL;
	}

	if (length - start < minlen) {
		dev_err(zxdh_dev->device, "bad capability len %u (>=%zu expected)\n",
			length, minlen);
		return NULL;
	}

	length -= start;
	if (start + offset < offset) {
		dev_err(zxdh_dev->device, "map wrap-around %u+%u\n", start, offset);
		return NULL;
	}

	offset += start;
	if (offset & (align - 1)) {
		dev_err(zxdh_dev->device, "offset %u not aligned to %u\n", offset, align);
		return NULL;
	}

	if (length > size)
		length = size;

	if (len)
		*len = length;

	if (length + offset < offset ||
	    length + offset > pci_resource_len(pdev, bar)) {
		dev_err(zxdh_dev->device,
			"map %u@%u out of range on bar %u length %lu\n",
			length, offset, bar,
			(unsigned long)pci_resource_len(pdev, bar));
		return NULL;
	}

	p = pci_iomap_range(pdev, bar, offset, length);
	if (!p) {
		dev_err(zxdh_dev->device, "unable to map custom queue %u@%u on bar %u\n",
			length, offset, bar);
	} else if (pa) {
		*pa = pci_resource_start(pdev, bar) + offset;
	}

	return p;
}

int zxdh_pf_common_cfg_init(struct zxdh_core_dev *zxdh_dev)
{
	struct zxdh_pf_dev *pf_dev = zxdh_dev->priv;
	struct pci_dev *pdev = zxdh_dev->pdev;
	int common;

	/* check for a common config: if not, use legacy mode (bar 0). */
	common = zxdh_pf_pci_find_capability(pdev, ZXDH_PCI_CAP_COMMON_CFG,
					     IORESOURCE_MEM,
					     &pf_dev->modern_bars);
	if (!common) {
		dev_err(zxdh_dev->device,
			"missing capabilities, leaving for legacy driver\n");
		return -ENODEV;
	}

	pf_dev->common = zxdh_pf_map_capability(zxdh_dev, common,
						sizeof(struct zxdh_pf_pci_common_cfg),
						ZXDH_PF_ALIGN4, 0,
						sizeof(struct zxdh_pf_pci_common_cfg),
						NULL, NULL, NULL);
	if (!pf_dev->common) {
		dev_err(zxdh_dev->device, "pf_dev->common is null\n");
		return -EINVAL;
	}

	return 0;
}

int zxdh_pf_notify_cfg_init(struct zxdh_core_dev *zxdh_dev)
{
	struct zxdh_pf_dev *pf_dev = zxdh_dev->priv;
	struct pci_dev *pdev = zxdh_dev->pdev;
	u32 notify_length;
	u32 notify_offset;
	int notify;

	/* If common is there, these should be too... */
	notify = zxdh_pf_pci_find_capability(pdev, ZXDH_PCI_CAP_NOTIFY_CFG,
					     IORESOURCE_MEM,
					     &pf_dev->modern_bars);
	if (!notify) {
		dev_err(zxdh_dev->device, "missing notify cfg capability\n");
		return -EINVAL;
	}

	pci_read_config_dword(pdev,
			      notify + offsetof(struct zxdh_pf_pci_notify_cap,
				notify_off_multiplier),
		&pf_dev->notify_offset_multiplier);
	pci_read_config_dword(pdev,
			      notify + offsetof(struct zxdh_pf_pci_notify_cap,
				cap.length), &notify_length);
	pci_read_config_dword(pdev,
			      notify + offsetof(struct zxdh_pf_pci_notify_cap,
				cap.offset), &notify_offset);

	/* We don't know how many VQs we'll map, ahead of the time.
	 * If notify length is small, map it all now. Otherwise,
	 * map each VQ individually later.
	 */
	if (notify_length <= PAGE_SIZE - (notify_offset % PAGE_SIZE)) {
		pf_dev->notify_base = zxdh_pf_map_capability(zxdh_dev, notify,
							     ZXDH_PF_MAP_MINLEN2,
							    ZXDH_PF_ALIGN2, 0,
							    notify_length,
							    &pf_dev->notify_len,
							    &pf_dev->notify_pa, NULL);
		if (!pf_dev->notify_base) {
			dev_err(zxdh_dev->device, "pf_dev->notify_base is null\n");
			return -EINVAL;
		}
	} else {
		pf_dev->notify_map_cap = notify;
	}

	return 0;
}

int zxdh_pf_device_cfg_init(struct zxdh_core_dev *zxdh_dev)
{
	struct zxdh_pf_dev *pf_dev = zxdh_dev->priv;
	struct pci_dev *pdev = zxdh_dev->pdev;
	int device;

	/* Device capability is only mandatory for
	 * devices that have device-specific configuration.
	 */
	device = zxdh_pf_pci_find_capability(pdev, ZXDH_PCI_CAP_DEVICE_CFG,
					     IORESOURCE_MEM,
					     &pf_dev->modern_bars);

	/* we don't know how much we should map,
	 * but PAGE_SIZE is more than enough for all existing devices.
	 */
	if (device) {
		pf_dev->device = zxdh_pf_map_capability(zxdh_dev, device, 0,
							ZXDH_PF_ALIGN4, 0, PAGE_SIZE,
						       &pf_dev->device_len, NULL,
						       &pf_dev->dev_cfg_bar_off);
		if (!pf_dev->device) {
			dev_err(zxdh_dev->device, "pf_dev->device is null\n");
			return -EINVAL;
		}
	}
	return 0;
}

void zxdh_pf_modern_cfg_uninit(struct zxdh_core_dev *zxdh_dev)
{
	struct zxdh_pf_dev *pf_dev = zxdh_dev->priv;
	struct pci_dev *pdev = zxdh_dev->pdev;

	if (pf_dev->device)
		pci_iounmap(pdev, pf_dev->device);
	if (pf_dev->notify_base)
		pci_iounmap(pdev, pf_dev->notify_base);
	pci_iounmap(pdev, pf_dev->common);
}

int zxdh_pf_modern_cfg_init(struct zxdh_core_dev *zxdh_dev)
{
	struct zxdh_pf_dev *pf_dev = zxdh_dev->priv;
	struct pci_dev *pdev = zxdh_dev->pdev;
	int ret;

	ret = zxdh_pf_common_cfg_init(zxdh_dev);
	if (ret) {
		dev_err(zxdh_dev->device, "zxdh_pf_common_cfg_init failed: %d\n", ret);
		return ret;
	}

	ret = zxdh_pf_notify_cfg_init(zxdh_dev);
	if (ret) {
		dev_err(zxdh_dev->device, "zxdh_pf_notify_cfg_init failed: %d\n", ret);
		goto err_map_notify;
	}

	ret = zxdh_pf_device_cfg_init(zxdh_dev);
	if (ret) {
		dev_err(zxdh_dev->device, "zxdh_pf_device_cfg_init failed: %d\n", ret);
		goto err_map_device;
	}

	return 0;

err_map_device:
	if (pf_dev->notify_base)
		pci_iounmap(pdev, pf_dev->notify_base);
err_map_notify:
	pci_iounmap(pdev, pf_dev->common);
	return ret;
}

static int zxdh_pf_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct zxdh_core_dev *zxdh_dev;
	struct zxdh_pf_dev *pf_dev;
	struct devlink *devlink;
	int ret;

	devlink = devlink_alloc(&zxdh_pf_devlink_ops, sizeof(struct zxdh_core_dev),
				&pdev->dev);
	if (!devlink)
		return -ENOMEM;

	zxdh_dev = devlink_priv(devlink);
	zxdh_dev->device = &pdev->dev;
	zxdh_dev->pdev = pdev;
	zxdh_dev->devlink = devlink;

	pf_dev = zxdh_core_alloc_priv(zxdh_dev, sizeof(*pf_dev));
	if (!pf_dev) {
		dev_err(&pdev->dev, "zxdh_pf_dev alloc failed\n");
		ret = -ENOMEM;
		goto err_pf_dev;
	}

	ret = zxdh_pf_pci_init(zxdh_dev);
	if (ret) {
		dev_err(&pdev->dev, "zxdh_pf_pci_init failed: %d\n", ret);
		goto err_pci_init;
	}

	ret = zxdh_pf_modern_cfg_init(zxdh_dev);
	if (ret) {
		dev_err(&pdev->dev, "zxdh_pf_modern_cfg_init failed: %d\n", ret);
		goto err_cfg_init;
	}

	devlink_register(devlink);

	return 0;

err_cfg_init:
	zxdh_pf_pci_close(zxdh_dev);
err_pci_init:
	zxdh_core_free_priv(zxdh_dev);
err_pf_dev:
	devlink_free(devlink);
	return ret;
}

static void zxdh_pf_remove(struct pci_dev *pdev)
{
	struct zxdh_core_dev *zxdh_dev = pci_get_drvdata(pdev);
	struct devlink *devlink = priv_to_devlink(zxdh_dev);

	devlink_unregister(devlink);
	zxdh_pf_modern_cfg_uninit(zxdh_dev);
	zxdh_pf_pci_close(zxdh_dev);
	zxdh_core_free_priv(zxdh_dev);
	devlink_free(devlink);
	pci_set_drvdata(pdev, NULL);
}

static void zxdh_pf_shutdown(struct pci_dev *pdev)
{
	if (system_state == SYSTEM_POWER_OFF)
		pci_set_power_state(pdev, PCI_D3hot);
	pci_disable_device(pdev);
}

static struct pci_driver zxdh_pf_driver = {
	.name = "dinghai10e",
	.id_table = zxdh_pf_pci_table,
	.probe = zxdh_pf_probe,
	.remove = zxdh_pf_remove,
	.shutdown = zxdh_pf_shutdown,
};

module_pci_driver(zxdh_pf_driver);
