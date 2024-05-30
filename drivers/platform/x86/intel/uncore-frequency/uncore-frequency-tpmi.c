// SPDX-License-Identifier: GPL-2.0-only
/*
 * uncore-frquency-tpmi: Uncore frequency scaling using TPMI
 *
 * Copyright (c) 2023, Intel Corporation.
 * All Rights Reserved.
 *
 * The hardware interface to read/write is basically substitution of
 * MSR 0x620 and 0x621.
 * There are specific MMIO offset and bits to get/set minimum and
 * maximum uncore ratio, similar to MSRs.
 * The scope of the uncore MSRs was package scope. But TPMI allows
 * new gen CPUs to have multiple uncore controls at uncore-cluster
 * level. Each package can have multiple power domains which further
 * can have multiple clusters.
 * Here number of power domains = number of resources in this aux
 * device. There are offsets and bits to discover number of clusters
 * and offset for each cluster level controls.
 *
 */

#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/intel_tpmi.h>

#include "uncore-frequency-common.h"

#define	UNCORE_MAJOR_VERSION		0
#define	UNCORE_MINOR_VERSION		2
#define UNCORE_HEADER_INDEX		0
#define UNCORE_FABRIC_CLUSTER_OFFSET	8

/* status + control + adv_ctl1 + adv_ctl2 */
#define UNCORE_FABRIC_CLUSTER_SIZE	(4 * 8)

#define UNCORE_STATUS_INDEX		0
#define UNCORE_CONTROL_INDEX		8

#define UNCORE_FREQ_KHZ_MULTIPLIER	100000

struct tpmi_uncore_struct;

/* Information for each cluster */
struct tpmi_uncore_cluster_info {
	bool root_domain;
	u8 __iomem *cluster_base;
	struct uncore_data uncore_data;
	struct tpmi_uncore_struct *uncore_root;
};

/* Information for each power domain */
struct tpmi_uncore_power_domain_info {
	u8 __iomem *uncore_base;
	int ufs_header_ver;
	int cluster_count;
	struct tpmi_uncore_cluster_info *cluster_infos;
};

/* Information for all power domains in a package */
struct tpmi_uncore_struct {
	int power_domain_count;
	int max_ratio;
	int min_ratio;
	struct tpmi_uncore_power_domain_info *pd_info;
	struct tpmi_uncore_cluster_info root_cluster;
	bool write_blocked;
};

#define UNCORE_GENMASK_MIN_RATIO	GENMASK_ULL(21, 15)
#define UNCORE_GENMASK_MAX_RATIO	GENMASK_ULL(14, 8)
#define UNCORE_GENMASK_CURRENT_RATIO	GENMASK_ULL(6, 0)

/* Helper function to read MMIO offset for max/min control frequency */
static void read_control_freq(struct tpmi_uncore_cluster_info *cluster_info,
			     unsigned int *min, unsigned int *max)
{
	u64 control;

	control = readq(cluster_info->cluster_base + UNCORE_CONTROL_INDEX);
	*max = FIELD_GET(UNCORE_GENMASK_MAX_RATIO, control) * UNCORE_FREQ_KHZ_MULTIPLIER;
	*min = FIELD_GET(UNCORE_GENMASK_MIN_RATIO, control) * UNCORE_FREQ_KHZ_MULTIPLIER;
}

#define UNCORE_MAX_RATIO	FIELD_MAX(UNCORE_GENMASK_MAX_RATIO)

/* Callback for sysfs read for max/min frequencies. Called under mutex locks */
static int uncore_read_control_freq(struct uncore_data *data, unsigned int *min,
				    unsigned int *max)
{
	struct tpmi_uncore_cluster_info *cluster_info;

	cluster_info = container_of(data, struct tpmi_uncore_cluster_info, uncore_data);

	if (cluster_info->root_domain) {
		struct tpmi_uncore_struct *uncore_root = cluster_info->uncore_root;
		int i, _min = 0, _max = 0;

		*min = UNCORE_MAX_RATIO * UNCORE_FREQ_KHZ_MULTIPLIER;
		*max = 0;

		/*
		 * Get the max/min by looking at each cluster. Get the lowest
		 * min and highest max.
		 */
		for (i = 0; i < uncore_root->power_domain_count; ++i) {
			int j;

			for (j = 0; j < uncore_root->pd_info[i].cluster_count; ++j) {
				read_control_freq(&uncore_root->pd_info[i].cluster_infos[j],
						  &_min, &_max);
				if (*min > _min)
					*min = _min;
				if (*max < _max)
					*max = _max;
			}
		}
		return 0;
	}

	read_control_freq(cluster_info, min, max);

	return 0;
}

/* Helper function to write MMIO offset for max/min control frequency */
static void write_control_freq(struct tpmi_uncore_cluster_info *cluster_info, unsigned int input,
			      unsigned int min_max)
{
	u64 control;

	control = readq(cluster_info->cluster_base + UNCORE_CONTROL_INDEX);

	if (min_max) {
		control &= ~UNCORE_GENMASK_MAX_RATIO;
		control |= FIELD_PREP(UNCORE_GENMASK_MAX_RATIO, input);
	} else {
		control &= ~UNCORE_GENMASK_MIN_RATIO;
		control |= FIELD_PREP(UNCORE_GENMASK_MIN_RATIO, input);
	}

	writeq(control, (cluster_info->cluster_base + UNCORE_CONTROL_INDEX));
}

/* Callback for sysfs write for max/min frequencies. Called under mutex locks */
static int uncore_write_control_freq(struct uncore_data *data, unsigned int input,
				     unsigned int min_max)
{
	struct tpmi_uncore_cluster_info *cluster_info;
	struct tpmi_uncore_struct *uncore_root;

	input /= UNCORE_FREQ_KHZ_MULTIPLIER;
	if (!input || input > UNCORE_MAX_RATIO)
		return -EINVAL;

	cluster_info = container_of(data, struct tpmi_uncore_cluster_info, uncore_data);
	uncore_root = cluster_info->uncore_root;

	if (uncore_root->write_blocked)
		return -EPERM;

	/* Update each cluster in a package */
	if (cluster_info->root_domain) {
		struct tpmi_uncore_struct *uncore_root = cluster_info->uncore_root;
		int i;

		for (i = 0; i < uncore_root->power_domain_count; ++i) {
			int j;

			for (j = 0; j < uncore_root->pd_info[i].cluster_count; ++j)
				write_control_freq(&uncore_root->pd_info[i].cluster_infos[j],
						  input, min_max);
		}

		if (min_max)
			uncore_root->max_ratio = input;
		else
			uncore_root->min_ratio = input;

		return 0;
	}

	if (min_max && uncore_root->max_ratio && uncore_root->max_ratio < input)
		return -EINVAL;

	if (!min_max && uncore_root->min_ratio && uncore_root->min_ratio > input)
		return -EINVAL;

	write_control_freq(cluster_info, input, min_max);

	return 0;
}

/* Callback for sysfs read for the current uncore frequency. Called under mutex locks */
static int uncore_read_freq(struct uncore_data *data, unsigned int *freq)
{
	struct tpmi_uncore_cluster_info *cluster_info;
	u64 status;

	cluster_info = container_of(data, struct tpmi_uncore_cluster_info, uncore_data);
	if (cluster_info->root_domain)
		return -ENODATA;

	status = readq((u8 __iomem *)cluster_info->cluster_base + UNCORE_STATUS_INDEX);
	*freq = FIELD_GET(UNCORE_GENMASK_CURRENT_RATIO, status) * UNCORE_FREQ_KHZ_MULTIPLIER;

	return 0;
}

static void remove_cluster_entries(struct tpmi_uncore_struct *tpmi_uncore)
{
	int i;

	for (i = 0; i < tpmi_uncore->power_domain_count; ++i) {
		struct tpmi_uncore_power_domain_info *pd_info;
		int j;

		pd_info = &tpmi_uncore->pd_info[i];
		if (!pd_info->uncore_base)
			continue;

		for (j = 0; j < pd_info->cluster_count; ++j) {
			struct tpmi_uncore_cluster_info *cluster_info;

			cluster_info = &pd_info->cluster_infos[j];
			uncore_freq_remove_die_entry(&cluster_info->uncore_data);
		}
	}
}

#define UNCORE_VERSION_MASK			GENMASK_ULL(7, 0)
#define UNCORE_LOCAL_FABRIC_CLUSTER_ID_MASK	GENMASK_ULL(15, 8)
#define UNCORE_CLUSTER_OFF_MASK			GENMASK_ULL(7, 0)
#define UNCORE_MAX_CLUSTER_PER_DOMAIN		8

static int uncore_probe(struct auxiliary_device *auxdev, const struct auxiliary_device_id *id)
{
	bool read_blocked = 0, write_blocked = 0;
	struct intel_tpmi_plat_info *plat_info;
	struct tpmi_uncore_struct *tpmi_uncore;
	bool uncore_sysfs_added = false;
	int ret, i, pkg = 0;
	int num_resources;

	ret = tpmi_get_feature_status(auxdev, TPMI_ID_UNCORE, &read_blocked, &write_blocked);
	if (ret)
		dev_info(&auxdev->dev, "Can't read feature status: ignoring blocked status\n");

	if (read_blocked) {
		dev_info(&auxdev->dev, "Firmware has blocked reads, exiting\n");
		return -ENODEV;
	}

	/* Get number of power domains, which is equal to number of resources */
	num_resources = tpmi_get_resource_count(auxdev);
	if (!num_resources)
		return -EINVAL;

	/* Register callbacks to uncore core */
	ret = uncore_freq_common_init(uncore_read_control_freq, uncore_write_control_freq,
				      uncore_read_freq);
	if (ret)
		return ret;

	/* Allocate uncore instance per package */
	tpmi_uncore = devm_kzalloc(&auxdev->dev, sizeof(*tpmi_uncore), GFP_KERNEL);
	if (!tpmi_uncore) {
		ret = -ENOMEM;
		goto err_rem_common;
	}

	/* Allocate memory for all power domains in a package */
	tpmi_uncore->pd_info = devm_kcalloc(&auxdev->dev, num_resources,
					    sizeof(*tpmi_uncore->pd_info),
					    GFP_KERNEL);
	if (!tpmi_uncore->pd_info) {
		ret = -ENOMEM;
		goto err_rem_common;
	}

	tpmi_uncore->power_domain_count = num_resources;
	tpmi_uncore->write_blocked = write_blocked;

	/* Get the package ID from the TPMI core */
	plat_info = tpmi_get_platform_data(auxdev);
	if (plat_info)
		pkg = plat_info->package_id;
	else
		dev_info(&auxdev->dev, "Platform information is NULL\n");

	for (i = 0; i < num_resources; ++i) {
		struct tpmi_uncore_power_domain_info *pd_info;
		struct resource *res;
		u64 cluster_offset;
		u8 cluster_mask;
		int mask, j;
		u64 header;

		res = tpmi_get_resource_at_index(auxdev, i);
		if (!res)
			continue;

		pd_info = &tpmi_uncore->pd_info[i];

		pd_info->uncore_base = devm_ioremap_resource(&auxdev->dev, res);
		if (IS_ERR(pd_info->uncore_base)) {
			ret = PTR_ERR(pd_info->uncore_base);
			/*
			 * Set to NULL so that clean up can still remove other
			 * entries already created if any by
			 * remove_cluster_entries()
			 */
			pd_info->uncore_base = NULL;
			goto remove_clusters;
		}

		/* Check for version and skip this resource if there is mismatch */
		header = readq(pd_info->uncore_base);
		pd_info->ufs_header_ver = header & UNCORE_VERSION_MASK;

		if (pd_info->ufs_header_ver == TPMI_VERSION_INVALID)
			continue;

		if (TPMI_MAJOR_VERSION(pd_info->ufs_header_ver) != UNCORE_MAJOR_VERSION) {
			dev_err(&auxdev->dev, "Uncore: Unsupported major version:%lx\n",
				TPMI_MAJOR_VERSION(pd_info->ufs_header_ver));
			ret = -ENODEV;
			goto remove_clusters;
		}

		if (TPMI_MINOR_VERSION(pd_info->ufs_header_ver) > UNCORE_MINOR_VERSION)
			dev_info(&auxdev->dev, "Uncore: Ignore: Unsupported minor version:%lx\n",
				 TPMI_MINOR_VERSION(pd_info->ufs_header_ver));

		/* Get Cluster ID Mask */
		cluster_mask = FIELD_GET(UNCORE_LOCAL_FABRIC_CLUSTER_ID_MASK, header);
		if (!cluster_mask) {
			dev_info(&auxdev->dev, "Uncore: Invalid cluster mask:%x\n", cluster_mask);
			continue;
		}

		/* Find out number of clusters in this resource */
		pd_info->cluster_count = hweight8(cluster_mask);

		pd_info->cluster_infos = devm_kcalloc(&auxdev->dev, pd_info->cluster_count,
						      sizeof(struct tpmi_uncore_cluster_info),
						      GFP_KERNEL);
		if (!pd_info->cluster_infos) {
			ret = -ENOMEM;
			goto remove_clusters;
		}
		/*
		 * Each byte in the register point to status and control
		 * registers belonging to cluster id 0-8.
		 */
		cluster_offset = readq(pd_info->uncore_base +
					UNCORE_FABRIC_CLUSTER_OFFSET);

		for (j = 0; j < pd_info->cluster_count; ++j) {
			struct tpmi_uncore_cluster_info *cluster_info;

			/* Get the offset for this cluster */
			mask = (cluster_offset & UNCORE_CLUSTER_OFF_MASK);
			/* Offset in QWORD, so change to bytes */
			mask <<= 3;

			cluster_info = &pd_info->cluster_infos[j];

			cluster_info->cluster_base = pd_info->uncore_base + mask;

			cluster_info->uncore_data.package_id = pkg;
			/* There are no dies like Cascade Lake */
			cluster_info->uncore_data.die_id = 0;
			cluster_info->uncore_data.domain_id = i;
			cluster_info->uncore_data.cluster_id = j;

			cluster_info->uncore_root = tpmi_uncore;

			ret = uncore_freq_add_entry(&cluster_info->uncore_data, 0);
			if (ret) {
				cluster_info->cluster_base = NULL;
				goto remove_clusters;
			}
			/* Point to next cluster offset */
			cluster_offset >>= UNCORE_MAX_CLUSTER_PER_DOMAIN;
			uncore_sysfs_added = true;
		}
	}

	if (!uncore_sysfs_added) {
		ret = -ENODEV;
		goto remove_clusters;
	}

	auxiliary_set_drvdata(auxdev, tpmi_uncore);

	tpmi_uncore->root_cluster.root_domain = true;
	tpmi_uncore->root_cluster.uncore_root = tpmi_uncore;

	tpmi_uncore->root_cluster.uncore_data.package_id = pkg;
	tpmi_uncore->root_cluster.uncore_data.domain_id = UNCORE_DOMAIN_ID_INVALID;
	ret = uncore_freq_add_entry(&tpmi_uncore->root_cluster.uncore_data, 0);
	if (ret)
		goto remove_clusters;

	return 0;

remove_clusters:
	remove_cluster_entries(tpmi_uncore);
err_rem_common:
	uncore_freq_common_exit();

	return ret;
}

static void uncore_remove(struct auxiliary_device *auxdev)
{
	struct tpmi_uncore_struct *tpmi_uncore = auxiliary_get_drvdata(auxdev);

	uncore_freq_remove_die_entry(&tpmi_uncore->root_cluster.uncore_data);
	remove_cluster_entries(tpmi_uncore);

	uncore_freq_common_exit();
}

static const struct auxiliary_device_id intel_uncore_id_table[] = {
	{ .name = "intel_vsec.tpmi-uncore" },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, intel_uncore_id_table);

static struct auxiliary_driver intel_uncore_aux_driver = {
	.id_table       = intel_uncore_id_table,
	.remove         = uncore_remove,
	.probe          = uncore_probe,
};

module_auxiliary_driver(intel_uncore_aux_driver);

MODULE_IMPORT_NS(INTEL_TPMI);
MODULE_IMPORT_NS(INTEL_UNCORE_FREQUENCY);
MODULE_DESCRIPTION("Intel TPMI UFS Driver");
MODULE_LICENSE("GPL");
