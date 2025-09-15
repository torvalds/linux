// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Platform Monitory Technology Discovery driver
 *
 * Copyright (c) 2025, Intel Corporation.
 * All Rights Reserved.
 */

#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/bug.h>
#include <linux/cleanup.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kdev_t.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/overflow.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string_choices.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include <linux/intel_pmt_features.h>
#include <linux/intel_vsec.h>

#include "class.h"

#define MAX_FEATURE_VERSION	0
#define DT_TBIR			GENMASK(2, 0)
#define FEAT_ATTR_SIZE(x)	((x) * sizeof(u32))
#define PMT_GUID_SIZE(x)	((x) * sizeof(u32))
#define PMT_ACCESS_TYPE_RSVD	0xF
#define SKIP_FEATURE		1

struct feature_discovery_table {
	u32	access_type:4;
	u32	version:8;
	u32	size:16;
	u32	reserved:4;
	u32	id;
	u32	offset;
	u32	reserved2;
};

/* Common feature table header */
struct feature_header {
	u32	attr_size:8;
	u32	num_guids:8;
	u32	reserved:16;
};

/* Feature attribute fields */
struct caps {
	u32		caps;
};

struct command {
	u32		max_stream_size:16;
	u32		max_command_size:16;
};

struct watcher {
	u32		reserved:21;
	u32		period:11;
	struct command	command;
};

struct rmid {
	u32		num_rmids:16;	/* Number of Resource Monitoring IDs */
	u32		reserved:16;
	struct watcher	watcher;
};

struct feature_table {
	struct feature_header	header;
	struct caps		caps;
	union {
		struct command command;
		struct watcher watcher;
		struct rmid rmid;
	};
	u32			*guids;
};

/* For backreference in struct feature */
struct pmt_features_priv;

struct feature {
	struct feature_table		table;
	struct kobject			kobj;
	struct pmt_features_priv	*priv;
	struct list_head		list;
	const struct attribute_group	*attr_group;
	enum pmt_feature_id		id;
};

struct pmt_features_priv {
	struct device		*parent;
	struct device		*dev;
	int			count;
	u32			mask;
	struct feature		feature[];
};

static LIST_HEAD(pmt_feature_list);
static DEFINE_MUTEX(feature_list_lock);

#define to_pmt_feature(x) container_of(x, struct feature, kobj)
static void pmt_feature_release(struct kobject *kobj)
{
}

static ssize_t caps_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct feature *feature = to_pmt_feature(kobj);
	struct pmt_cap **pmt_caps;
	u32 caps = feature->table.caps.caps;
	ssize_t ret = 0;

	switch (feature->id) {
	case FEATURE_PER_CORE_PERF_TELEM:
		pmt_caps = pmt_caps_pcpt;
		break;
	case FEATURE_PER_CORE_ENV_TELEM:
		pmt_caps = pmt_caps_pcet;
		break;
	case FEATURE_PER_RMID_PERF_TELEM:
		pmt_caps = pmt_caps_rmid_perf;
		break;
	case FEATURE_ACCEL_TELEM:
		pmt_caps = pmt_caps_accel;
		break;
	case FEATURE_UNCORE_TELEM:
		pmt_caps = pmt_caps_uncore;
		break;
	case FEATURE_CRASH_LOG:
		pmt_caps = pmt_caps_crashlog;
		break;
	case FEATURE_PETE_LOG:
		pmt_caps = pmt_caps_pete;
		break;
	case FEATURE_TPMI_CTRL:
		pmt_caps = pmt_caps_tpmi;
		break;
	case FEATURE_TRACING:
		pmt_caps = pmt_caps_tracing;
		break;
	case FEATURE_PER_RMID_ENERGY_TELEM:
		pmt_caps = pmt_caps_rmid_energy;
		break;
	default:
		return -EINVAL;
	}

	while (*pmt_caps) {
		struct pmt_cap *pmt_cap = *pmt_caps;

		while (pmt_cap->name) {
			ret += sysfs_emit_at(buf, ret, "%-40s Available: %s\n", pmt_cap->name,
					     str_yes_no(pmt_cap->mask & caps));
			pmt_cap++;
		}
		pmt_caps++;
	}

	return ret;
}
static struct kobj_attribute caps_attribute = __ATTR_RO(caps);

static struct watcher *get_watcher(struct feature *feature)
{
	switch (feature_layout[feature->id]) {
	case LAYOUT_RMID:
		return &feature->table.rmid.watcher;
	case LAYOUT_WATCHER:
		return &feature->table.watcher;
	default:
		return ERR_PTR(-EINVAL);
	}
}

static struct command *get_command(struct feature *feature)
{
	switch (feature_layout[feature->id]) {
	case LAYOUT_RMID:
		return &feature->table.rmid.watcher.command;
	case LAYOUT_WATCHER:
		return &feature->table.watcher.command;
	case LAYOUT_COMMAND:
		return &feature->table.command;
	default:
		return ERR_PTR(-EINVAL);
	}
}

static ssize_t num_rmids_show(struct kobject *kobj,
			      struct kobj_attribute *attr, char *buf)
{
	struct feature *feature = to_pmt_feature(kobj);

	return sysfs_emit(buf, "%u\n", feature->table.rmid.num_rmids);
}
static struct kobj_attribute num_rmids_attribute = __ATTR_RO(num_rmids);

static ssize_t min_watcher_period_ms_show(struct kobject *kobj,
					  struct kobj_attribute *attr, char *buf)
{
	struct feature *feature = to_pmt_feature(kobj);
	struct watcher *watcher = get_watcher(feature);

	if (IS_ERR(watcher))
		return PTR_ERR(watcher);

	return sysfs_emit(buf, "%u\n", watcher->period);
}
static struct kobj_attribute min_watcher_period_ms_attribute =
	__ATTR_RO(min_watcher_period_ms);

static ssize_t max_stream_size_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	struct feature *feature = to_pmt_feature(kobj);
	struct command *command = get_command(feature);

	if (IS_ERR(command))
		return PTR_ERR(command);

	return sysfs_emit(buf, "%u\n", command->max_stream_size);
}
static struct kobj_attribute max_stream_size_attribute =
	__ATTR_RO(max_stream_size);

static ssize_t max_command_size_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	struct feature *feature = to_pmt_feature(kobj);
	struct command *command = get_command(feature);

	if (IS_ERR(command))
		return PTR_ERR(command);

	return sysfs_emit(buf, "%u\n", command->max_command_size);
}
static struct kobj_attribute max_command_size_attribute =
	__ATTR_RO(max_command_size);

static ssize_t guids_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	struct feature *feature = to_pmt_feature(kobj);
	int i, count = 0;

	for (i = 0; i < feature->table.header.num_guids; i++)
		count += sysfs_emit_at(buf, count, "0x%x\n",
				       feature->table.guids[i]);

	return count;
}
static struct kobj_attribute guids_attribute = __ATTR_RO(guids);

static struct attribute *pmt_feature_rmid_attrs[] = {
	&caps_attribute.attr,
	&num_rmids_attribute.attr,
	&min_watcher_period_ms_attribute.attr,
	&max_stream_size_attribute.attr,
	&max_command_size_attribute.attr,
	&guids_attribute.attr,
	NULL
};
ATTRIBUTE_GROUPS(pmt_feature_rmid);

static const struct kobj_type pmt_feature_rmid_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.release = pmt_feature_release,
	.default_groups = pmt_feature_rmid_groups,
};

static struct attribute *pmt_feature_watcher_attrs[] = {
	&caps_attribute.attr,
	&min_watcher_period_ms_attribute.attr,
	&max_stream_size_attribute.attr,
	&max_command_size_attribute.attr,
	&guids_attribute.attr,
	NULL
};
ATTRIBUTE_GROUPS(pmt_feature_watcher);

static const struct kobj_type pmt_feature_watcher_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.release = pmt_feature_release,
	.default_groups = pmt_feature_watcher_groups,
};

static struct attribute *pmt_feature_command_attrs[] = {
	&caps_attribute.attr,
	&max_stream_size_attribute.attr,
	&max_command_size_attribute.attr,
	&guids_attribute.attr,
	NULL
};
ATTRIBUTE_GROUPS(pmt_feature_command);

static const struct kobj_type pmt_feature_command_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.release = pmt_feature_release,
	.default_groups = pmt_feature_command_groups,
};

static struct attribute *pmt_feature_guids_attrs[] = {
	&caps_attribute.attr,
	&guids_attribute.attr,
	NULL
};
ATTRIBUTE_GROUPS(pmt_feature_guids);

static const struct kobj_type pmt_feature_guids_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.release = pmt_feature_release,
	.default_groups = pmt_feature_guids_groups,
};

static int
pmt_feature_get_disc_table(struct pmt_features_priv *priv,
			   struct resource *disc_res,
			   struct feature_discovery_table *disc_tbl)
{
	void __iomem *disc_base;

	disc_base = devm_ioremap_resource(priv->dev, disc_res);
	if (IS_ERR(disc_base))
		return PTR_ERR(disc_base);

	memcpy_fromio(disc_tbl, disc_base, sizeof(*disc_tbl));

	devm_iounmap(priv->dev, disc_base);

	if (priv->mask & BIT(disc_tbl->id))
		return dev_err_probe(priv->dev, -EINVAL, "Duplicate feature: %s\n",
				     pmt_feature_names[disc_tbl->id]);

	/*
	 * Some devices may expose non-functioning entries that are
	 * reserved for future use. They have zero size. Do not fail
	 * probe for these. Just ignore them.
	 */
	if (disc_tbl->size == 0 || disc_tbl->access_type == PMT_ACCESS_TYPE_RSVD)
		return SKIP_FEATURE;

	if (disc_tbl->version > MAX_FEATURE_VERSION)
		return SKIP_FEATURE;

	if (!pmt_feature_id_is_valid(disc_tbl->id))
		return SKIP_FEATURE;

	priv->mask |= BIT(disc_tbl->id);

	return 0;
}

static int
pmt_feature_get_feature_table(struct pmt_features_priv *priv,
			      struct feature *feature,
			      struct feature_discovery_table *disc_tbl,
			      struct resource *disc_res)
{
	struct feature_table *feat_tbl = &feature->table;
	struct feature_header *header;
	struct resource res = {};
	resource_size_t res_size;
	void __iomem *feat_base, *feat_offset;
	void *tbl_offset;
	size_t size;
	u32 *guids;
	u8 tbir;

	tbir = FIELD_GET(DT_TBIR, disc_tbl->offset);

	switch (disc_tbl->access_type) {
	case ACCESS_LOCAL:
		if (tbir)
			return dev_err_probe(priv->dev, -EINVAL,
				"Unsupported BAR index %u for access type %u\n",
				tbir, disc_tbl->access_type);


		/*
		 * For access_type LOCAL, the base address is as follows:
		 * base address = end of discovery region + base offset + 1
		 */
		res = DEFINE_RES_MEM(disc_res->end + disc_tbl->offset + 1,
				     disc_tbl->size * sizeof(u32));
		break;

	default:
		return dev_err_probe(priv->dev, -EINVAL, "Unrecognized access_type %u\n",
				     disc_tbl->access_type);
	}

	feature->id = disc_tbl->id;

	/* Get the feature table */
	feat_base = devm_ioremap_resource(priv->dev, &res);
	if (IS_ERR(feat_base))
		return PTR_ERR(feat_base);

	feat_offset = feat_base;
	tbl_offset = feat_tbl;

	/* Get the header */
	header = &feat_tbl->header;
	memcpy_fromio(header, feat_offset, sizeof(*header));

	/* Validate fields fit within mapped resource */
	size = sizeof(*header) + FEAT_ATTR_SIZE(header->attr_size) +
	       PMT_GUID_SIZE(header->num_guids);
	res_size = resource_size(&res);
	if (WARN(size > res_size, "Bad table size %zu > %pa", size, &res_size))
		return -EINVAL;

	/* Get the feature attributes, including capability fields */
	tbl_offset += sizeof(*header);
	feat_offset += sizeof(*header);

	memcpy_fromio(tbl_offset, feat_offset, FEAT_ATTR_SIZE(header->attr_size));

	/* Finally, get the guids */
	guids = devm_kmalloc(priv->dev, PMT_GUID_SIZE(header->num_guids), GFP_KERNEL);
	if (!guids)
		return -ENOMEM;

	feat_offset += FEAT_ATTR_SIZE(header->attr_size);

	memcpy_fromio(guids, feat_offset, PMT_GUID_SIZE(header->num_guids));

	feat_tbl->guids = guids;

	devm_iounmap(priv->dev, feat_base);

	return 0;
}

static void pmt_features_add_feat(struct feature *feature)
{
	guard(mutex)(&feature_list_lock);
	list_add(&feature->list, &pmt_feature_list);
}

static void pmt_features_remove_feat(struct feature *feature)
{
	guard(mutex)(&feature_list_lock);
	list_del(&feature->list);
}

/* Get the discovery table and use it to get the feature table */
static int pmt_features_discovery(struct pmt_features_priv *priv,
				  struct feature *feature,
				  struct intel_vsec_device *ivdev,
				  int idx)
{
	struct feature_discovery_table disc_tbl = {}; /* Avoid false warning */
	struct resource *disc_res = &ivdev->resource[idx];
	const struct kobj_type *ktype;
	int ret;

	ret = pmt_feature_get_disc_table(priv, disc_res, &disc_tbl);
	if (ret)
		return ret;

	ret = pmt_feature_get_feature_table(priv, feature, &disc_tbl, disc_res);
	if (ret)
		return ret;

	switch (feature_layout[feature->id]) {
	case LAYOUT_RMID:
		ktype = &pmt_feature_rmid_ktype;
		feature->attr_group = &pmt_feature_rmid_group;
		break;
	case LAYOUT_WATCHER:
		ktype = &pmt_feature_watcher_ktype;
		feature->attr_group = &pmt_feature_watcher_group;
		break;
	case LAYOUT_COMMAND:
		ktype = &pmt_feature_command_ktype;
		feature->attr_group = &pmt_feature_command_group;
		break;
	case LAYOUT_CAPS_ONLY:
		ktype = &pmt_feature_guids_ktype;
		feature->attr_group = &pmt_feature_guids_group;
		break;
	default:
		return -EINVAL;
	}

	ret = kobject_init_and_add(&feature->kobj, ktype, &priv->dev->kobj,
				   "%s", pmt_feature_names[feature->id]);
	if (ret)
		return ret;

	kobject_uevent(&feature->kobj, KOBJ_ADD);
	pmt_features_add_feat(feature);

	return 0;
}

static void pmt_features_remove(struct auxiliary_device *auxdev)
{
	struct pmt_features_priv *priv = auxiliary_get_drvdata(auxdev);
	int i;

	for (i = 0; i < priv->count; i++) {
		struct feature *feature = &priv->feature[i];

		pmt_features_remove_feat(feature);
		sysfs_remove_group(&feature->kobj, feature->attr_group);
		kobject_put(&feature->kobj);
	}

	device_unregister(priv->dev);
}

static int pmt_features_probe(struct auxiliary_device *auxdev, const struct auxiliary_device_id *id)
{
	struct intel_vsec_device *ivdev = auxdev_to_ivdev(auxdev);
	struct pmt_features_priv *priv;
	size_t size;
	int ret, i;

	size = struct_size(priv, feature, ivdev->num_resources);
	priv = devm_kzalloc(&auxdev->dev, size, GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->parent = &ivdev->pcidev->dev;
	auxiliary_set_drvdata(auxdev, priv);

	priv->dev = device_create(&intel_pmt_class, &auxdev->dev, MKDEV(0, 0), priv,
				  "%s-%s", "features", dev_name(priv->parent));
	if (IS_ERR(priv->dev))
		return dev_err_probe(priv->dev, PTR_ERR(priv->dev),
				     "Could not create %s-%s device node\n",
				     "features", dev_name(priv->dev));

	/* Initialize each feature */
	for (i = 0; i < ivdev->num_resources; i++) {
		struct feature *feature = &priv->feature[priv->count];

		ret = pmt_features_discovery(priv, feature, ivdev, i);
		if (ret == SKIP_FEATURE)
			continue;
		if (ret != 0)
			goto abort_probe;

		feature->priv = priv;
		priv->count++;
	}

	return 0;

abort_probe:
	/*
	 * Only fully initialized features are tracked in priv->count, which is
	 * incremented only after a feature is completely set up (i.e., after
	 * discovery and sysfs registration). If feature initialization fails,
	 * the failing feature's state is local and does not require rollback.
	 *
	 * Therefore, on error, we can safely call the driver's remove() routine
	 * pmt_features_remove() to clean up only those features that were
	 * fully initialized and counted. All other resources are device-managed
	 * and will be cleaned up automatically during device_unregister().
	 */
	pmt_features_remove(auxdev);

	return ret;
}

static void pmt_get_features(struct intel_pmt_entry *entry, struct feature *f)
{
	int num_guids = f->table.header.num_guids;
	int i;

	for (i = 0; i < num_guids; i++) {
		if (f->table.guids[i] != entry->guid)
			continue;

		entry->feature_flags |= BIT(f->id);

		if (feature_layout[f->id] == LAYOUT_RMID)
			entry->num_rmids = f->table.rmid.num_rmids;
		else
			entry->num_rmids = 0; /* entry is kzalloc but set anyway */
	}
}

void intel_pmt_get_features(struct intel_pmt_entry *entry)
{
	struct feature *feature;

	mutex_lock(&feature_list_lock);
	list_for_each_entry(feature, &pmt_feature_list, list) {
		if (feature->priv->parent != &entry->ep->pcidev->dev)
			continue;

		pmt_get_features(entry, feature);
	}
	mutex_unlock(&feature_list_lock);
}
EXPORT_SYMBOL_NS_GPL(intel_pmt_get_features, "INTEL_PMT");

static const struct auxiliary_device_id pmt_features_id_table[] = {
	{ .name = "intel_vsec.discovery" },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, pmt_features_id_table);

static struct auxiliary_driver pmt_features_aux_driver = {
	.id_table	= pmt_features_id_table,
	.remove		= pmt_features_remove,
	.probe		= pmt_features_probe,
};
module_auxiliary_driver(pmt_features_aux_driver);

MODULE_AUTHOR("David E. Box <david.e.box@linux.intel.com>");
MODULE_DESCRIPTION("Intel PMT Discovery driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("INTEL_PMT");
