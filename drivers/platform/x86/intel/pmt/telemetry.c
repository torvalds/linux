// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Platform Monitory Technology Telemetry driver
 *
 * Copyright (c) 2020, Intel Corporation.
 * All Rights Reserved.
 *
 * Author: "David E. Box" <david.e.box@linux.intel.com>
 */

#include <linux/auxiliary_bus.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/intel_pmt_features.h>
#include <linux/intel_vsec.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/overflow.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/xarray.h>

#include "class.h"

#define TELEM_SIZE_OFFSET	0x0
#define TELEM_GUID_OFFSET	0x4
#define TELEM_BASE_OFFSET	0x8
#define TELEM_ACCESS(v)		((v) & GENMASK(3, 0))
#define TELEM_TYPE(v)		(((v) & GENMASK(7, 4)) >> 4)
/* size is in bytes */
#define TELEM_SIZE(v)		(((v) & GENMASK(27, 12)) >> 10)

/* Used by client hardware to identify a fixed telemetry entry*/
#define TELEM_CLIENT_FIXED_BLOCK_GUID	0x10000000

#define NUM_BYTES_QWORD(v)	((v) << 3)
#define SAMPLE_ID_OFFSET(v)	((v) << 3)

#define NUM_BYTES_DWORD(v)	((v) << 2)
#define SAMPLE_ID_OFFSET32(v)	((v) << 2)

/* Protects access to the xarray of telemetry endpoint handles */
static DEFINE_MUTEX(ep_lock);

enum telem_type {
	TELEM_TYPE_PUNIT = 0,
	TELEM_TYPE_CRASHLOG,
	TELEM_TYPE_PUNIT_FIXED,
};

struct pmt_telem_priv {
	int				num_entries;
	struct intel_pmt_entry		entry[];
};

static bool pmt_telem_region_overlaps(struct intel_pmt_entry *entry,
				      struct device *dev)
{
	u32 guid = readl(entry->disc_table + TELEM_GUID_OFFSET);

	if (intel_pmt_is_early_client_hw(dev)) {
		u32 type = TELEM_TYPE(readl(entry->disc_table));

		if ((type == TELEM_TYPE_PUNIT_FIXED) ||
		    (guid == TELEM_CLIENT_FIXED_BLOCK_GUID))
			return true;
	}

	return false;
}

static int pmt_telem_header_decode(struct intel_pmt_entry *entry,
				   struct device *dev)
{
	void __iomem *disc_table = entry->disc_table;
	struct intel_pmt_header *header = &entry->header;

	if (pmt_telem_region_overlaps(entry, dev))
		return 1;

	header->access_type = TELEM_ACCESS(readl(disc_table));
	header->guid = readl(disc_table + TELEM_GUID_OFFSET);
	header->base_offset = readl(disc_table + TELEM_BASE_OFFSET);

	/* Size is measured in DWORDS, but accessor returns bytes */
	header->size = TELEM_SIZE(readl(disc_table));

	/*
	 * Some devices may expose non-functioning entries that are
	 * reserved for future use. They have zero size. Do not fail
	 * probe for these. Just ignore them.
	 */
	if (header->size == 0 || header->access_type == 0xF)
		return 1;

	return 0;
}

static int pmt_telem_add_endpoint(struct intel_vsec_device *ivdev,
				  struct intel_pmt_entry *entry)
{
	struct telem_endpoint *ep;

	/* Endpoint lifetimes are managed by kref, not devres */
	entry->ep = kzalloc(sizeof(*(entry->ep)), GFP_KERNEL);
	if (!entry->ep)
		return -ENOMEM;

	ep = entry->ep;
	ep->pcidev = ivdev->pcidev;
	ep->header.access_type = entry->header.access_type;
	ep->header.guid = entry->header.guid;
	ep->header.base_offset = entry->header.base_offset;
	ep->header.size = entry->header.size;
	ep->base = entry->base;
	ep->present = true;
	ep->cb = ivdev->priv_data;

	kref_init(&ep->kref);

	return 0;
}

static DEFINE_XARRAY_ALLOC(telem_array);
static struct intel_pmt_namespace pmt_telem_ns = {
	.name = "telem",
	.xa = &telem_array,
	.pmt_header_decode = pmt_telem_header_decode,
	.pmt_add_endpoint = pmt_telem_add_endpoint,
};

/* Called when all users unregister and the device is removed */
static void pmt_telem_ep_release(struct kref *kref)
{
	struct telem_endpoint *ep;

	ep = container_of(kref, struct telem_endpoint, kref);
	kfree(ep);
}

unsigned long pmt_telem_get_next_endpoint(unsigned long start)
{
	struct intel_pmt_entry *entry;
	unsigned long found_idx;

	mutex_lock(&ep_lock);
	xa_for_each_start(&telem_array, found_idx, entry, start) {
		/*
		 * Return first found index after start.
		 * 0 is not valid id.
		 */
		if (found_idx > start)
			break;
	}
	mutex_unlock(&ep_lock);

	return found_idx == start ? 0 : found_idx;
}
EXPORT_SYMBOL_NS_GPL(pmt_telem_get_next_endpoint, "INTEL_PMT_TELEMETRY");

struct telem_endpoint *pmt_telem_register_endpoint(int devid)
{
	struct intel_pmt_entry *entry;
	unsigned long index = devid;

	mutex_lock(&ep_lock);
	entry = xa_find(&telem_array, &index, index, XA_PRESENT);
	if (!entry) {
		mutex_unlock(&ep_lock);
		return ERR_PTR(-ENXIO);
	}

	kref_get(&entry->ep->kref);
	mutex_unlock(&ep_lock);

	return entry->ep;
}
EXPORT_SYMBOL_NS_GPL(pmt_telem_register_endpoint, "INTEL_PMT_TELEMETRY");

void pmt_telem_unregister_endpoint(struct telem_endpoint *ep)
{
	kref_put(&ep->kref, pmt_telem_ep_release);
}
EXPORT_SYMBOL_NS_GPL(pmt_telem_unregister_endpoint, "INTEL_PMT_TELEMETRY");

int pmt_telem_get_endpoint_info(int devid, struct telem_endpoint_info *info)
{
	struct intel_pmt_entry *entry;
	unsigned long index = devid;
	int err = 0;

	if (!info)
		return -EINVAL;

	mutex_lock(&ep_lock);
	entry = xa_find(&telem_array, &index, index, XA_PRESENT);
	if (!entry) {
		err = -ENXIO;
		goto unlock;
	}

	info->pdev = entry->ep->pcidev;
	info->header = entry->ep->header;

unlock:
	mutex_unlock(&ep_lock);
	return err;

}
EXPORT_SYMBOL_NS_GPL(pmt_telem_get_endpoint_info, "INTEL_PMT_TELEMETRY");

static int pmt_copy_region(struct telemetry_region *region,
			   struct intel_pmt_entry *entry)
{

	struct oobmsm_plat_info *plat_info;

	plat_info = intel_vsec_get_mapping(entry->ep->pcidev);
	if (IS_ERR(plat_info))
		return PTR_ERR(plat_info);

	region->plat_info = *plat_info;
	region->guid = entry->guid;
	region->addr = entry->ep->base;
	region->size = entry->size;
	region->num_rmids = entry->num_rmids;

	return 0;
}

static void pmt_feature_group_release(struct kref *kref)
{
	struct pmt_feature_group *feature_group;

	feature_group = container_of(kref, struct pmt_feature_group, kref);
	kfree(feature_group);
}

struct pmt_feature_group *intel_pmt_get_regions_by_feature(enum pmt_feature_id id)
{
	struct pmt_feature_group *feature_group __free(kfree) = NULL;
	struct telemetry_region *region;
	struct intel_pmt_entry *entry;
	unsigned long idx;
	int count = 0;
	size_t size;

	if (!pmt_feature_id_is_valid(id))
		return ERR_PTR(-EINVAL);

	guard(mutex)(&ep_lock);
	xa_for_each(&telem_array, idx, entry) {
		if (entry->feature_flags & BIT(id))
			count++;
	}

	if (!count)
		return ERR_PTR(-ENOENT);

	size = struct_size(feature_group, regions, count);
	feature_group = kzalloc(size, GFP_KERNEL);
	if (!feature_group)
		return ERR_PTR(-ENOMEM);

	feature_group->count = count;

	region = feature_group->regions;
	xa_for_each(&telem_array, idx, entry) {
		int ret;

		if (!(entry->feature_flags & BIT(id)))
			continue;

		ret = pmt_copy_region(region, entry);
		if (ret)
			return ERR_PTR(ret);

		region++;
	}

	kref_init(&feature_group->kref);

	return no_free_ptr(feature_group);
}
EXPORT_SYMBOL(intel_pmt_get_regions_by_feature);

void intel_pmt_put_feature_group(struct pmt_feature_group *feature_group)
{
	kref_put(&feature_group->kref, pmt_feature_group_release);
}
EXPORT_SYMBOL(intel_pmt_put_feature_group);

int pmt_telem_read(struct telem_endpoint *ep, u32 id, u64 *data, u32 count)
{
	u32 offset, size;

	if (!ep->present)
		return -ENODEV;

	offset = SAMPLE_ID_OFFSET(id);
	size = ep->header.size;

	if (offset + NUM_BYTES_QWORD(count) > size)
		return -EINVAL;

	pmt_telem_read_mmio(ep->pcidev, ep->cb, ep->header.guid, data, ep->base, offset,
			    NUM_BYTES_QWORD(count));

	return ep->present ? 0 : -EPIPE;
}
EXPORT_SYMBOL_NS_GPL(pmt_telem_read, "INTEL_PMT_TELEMETRY");

int pmt_telem_read32(struct telem_endpoint *ep, u32 id, u32 *data, u32 count)
{
	u32 offset, size;

	if (!ep->present)
		return -ENODEV;

	offset = SAMPLE_ID_OFFSET32(id);
	size = ep->header.size;

	if (offset + NUM_BYTES_DWORD(count) > size)
		return -EINVAL;

	memcpy_fromio(data, ep->base + offset, NUM_BYTES_DWORD(count));

	return ep->present ? 0 : -EPIPE;
}
EXPORT_SYMBOL_NS_GPL(pmt_telem_read32, "INTEL_PMT_TELEMETRY");

struct telem_endpoint *
pmt_telem_find_and_register_endpoint(struct pci_dev *pcidev, u32 guid, u16 pos)
{
	int devid = 0;
	int inst = 0;
	int err = 0;

	while ((devid = pmt_telem_get_next_endpoint(devid))) {
		struct telem_endpoint_info ep_info;

		err = pmt_telem_get_endpoint_info(devid, &ep_info);
		if (err)
			return ERR_PTR(err);

		if (ep_info.header.guid == guid && ep_info.pdev == pcidev) {
			if (inst == pos)
				return pmt_telem_register_endpoint(devid);
			++inst;
		}
	}

	return ERR_PTR(-ENXIO);
}
EXPORT_SYMBOL_NS_GPL(pmt_telem_find_and_register_endpoint, "INTEL_PMT_TELEMETRY");

static void pmt_telem_remove(struct auxiliary_device *auxdev)
{
	struct pmt_telem_priv *priv = auxiliary_get_drvdata(auxdev);
	int i;

	mutex_lock(&ep_lock);
	for (i = 0; i < priv->num_entries; i++) {
		struct intel_pmt_entry *entry = &priv->entry[i];

		kref_put(&entry->ep->kref, pmt_telem_ep_release);
		intel_pmt_dev_destroy(entry, &pmt_telem_ns);
	}
	mutex_unlock(&ep_lock);
};

static int pmt_telem_probe(struct auxiliary_device *auxdev, const struct auxiliary_device_id *id)
{
	struct intel_vsec_device *intel_vsec_dev = auxdev_to_ivdev(auxdev);
	struct pmt_telem_priv *priv;
	size_t size;
	int i, ret;

	size = struct_size(priv, entry, intel_vsec_dev->num_resources);
	priv = devm_kzalloc(&auxdev->dev, size, GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	auxiliary_set_drvdata(auxdev, priv);

	for (i = 0; i < intel_vsec_dev->num_resources; i++) {
		struct intel_pmt_entry *entry = &priv->entry[priv->num_entries];

		mutex_lock(&ep_lock);
		ret = intel_pmt_dev_create(entry, &pmt_telem_ns, intel_vsec_dev, i);
		mutex_unlock(&ep_lock);
		if (ret < 0)
			goto abort_probe;
		if (ret)
			continue;

		priv->num_entries++;

		intel_pmt_get_features(entry);
	}

	return 0;
abort_probe:
	pmt_telem_remove(auxdev);
	return ret;
}

static const struct auxiliary_device_id pmt_telem_id_table[] = {
	{ .name = "intel_vsec.telemetry" },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, pmt_telem_id_table);

static struct auxiliary_driver pmt_telem_aux_driver = {
	.id_table	= pmt_telem_id_table,
	.remove		= pmt_telem_remove,
	.probe		= pmt_telem_probe,
};

static int __init pmt_telem_init(void)
{
	return auxiliary_driver_register(&pmt_telem_aux_driver);
}
module_init(pmt_telem_init);

static void __exit pmt_telem_exit(void)
{
	auxiliary_driver_unregister(&pmt_telem_aux_driver);
	xa_destroy(&telem_array);
}
module_exit(pmt_telem_exit);

MODULE_AUTHOR("David E. Box <david.e.box@linux.intel.com>");
MODULE_DESCRIPTION("Intel PMT Telemetry driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("INTEL_PMT");
MODULE_IMPORT_NS("INTEL_VSEC");
