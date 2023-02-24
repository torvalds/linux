// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm Peripheral Image Loader helpers
 *
 * Copyright (C) 2016 Linaro Ltd
 * Copyright (C) 2015 Sony Mobile Communications Inc
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 */

#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/remoteproc.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/rpmsg/qcom_glink.h>
#include <linux/rpmsg/qcom_smd.h>
#include <linux/slab.h>
#include <linux/soc/qcom/mdt_loader.h>
#include <linux/soc/qcom/smem.h>

#include "remoteproc_internal.h"
#include "qcom_common.h"

#define to_glink_subdev(d) container_of(d, struct qcom_rproc_glink, subdev)
#define to_smd_subdev(d) container_of(d, struct qcom_rproc_subdev, subdev)
#define to_ssr_subdev(d) container_of(d, struct qcom_rproc_ssr, subdev)

#define MAX_NUM_OF_SS           10
#define MAX_REGION_NAME_LENGTH  16
#define SBL_MINIDUMP_SMEM_ID	602
#define MD_REGION_VALID		('V' << 24 | 'A' << 16 | 'L' << 8 | 'I' << 0)
#define MD_SS_ENCR_DONE		('D' << 24 | 'O' << 16 | 'N' << 8 | 'E' << 0)
#define MD_SS_ENABLED		('E' << 24 | 'N' << 16 | 'B' << 8 | 'L' << 0)

/**
 * struct minidump_region - Minidump region
 * @name		: Name of the region to be dumped
 * @seq_num:		: Use to differentiate regions with same name.
 * @valid		: This entry to be dumped (if set to 1)
 * @address		: Physical address of region to be dumped
 * @size		: Size of the region
 */
struct minidump_region {
	char	name[MAX_REGION_NAME_LENGTH];
	__le32	seq_num;
	__le32	valid;
	__le64	address;
	__le64	size;
};

/**
 * struct minidump_subsystem - Subsystem's SMEM Table of content
 * @status : Subsystem toc init status
 * @enabled : if set to 1, this region would be copied during coredump
 * @encryption_status: Encryption status for this subsystem
 * @encryption_required : Decides to encrypt the subsystem regions or not
 * @region_count : Number of regions added in this subsystem toc
 * @regions_baseptr : regions base pointer of the subsystem
 */
struct minidump_subsystem {
	__le32	status;
	__le32	enabled;
	__le32	encryption_status;
	__le32	encryption_required;
	__le32	region_count;
	__le64	regions_baseptr;
};

/**
 * struct minidump_global_toc - Global Table of Content
 * @status : Global Minidump init status
 * @md_revision : Minidump revision
 * @enabled : Minidump enable status
 * @subsystems : Array of subsystems toc
 */
struct minidump_global_toc {
	__le32				status;
	__le32				md_revision;
	__le32				enabled;
	struct minidump_subsystem	subsystems[MAX_NUM_OF_SS];
};

struct qcom_ssr_subsystem {
	const char *name;
	struct srcu_notifier_head notifier_list;
	struct list_head list;
};

static LIST_HEAD(qcom_ssr_subsystem_list);
static DEFINE_MUTEX(qcom_ssr_subsys_lock);

static void qcom_minidump_cleanup(struct rproc *rproc)
{
	struct rproc_dump_segment *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, &rproc->dump_segments, node) {
		list_del(&entry->node);
		kfree(entry->priv);
		kfree(entry);
	}
}

static int qcom_add_minidump_segments(struct rproc *rproc, struct minidump_subsystem *subsystem,
			void (*rproc_dumpfn_t)(struct rproc *rproc, struct rproc_dump_segment *segment,
				void *dest, size_t offset, size_t size))
{
	struct minidump_region __iomem *ptr;
	struct minidump_region region;
	int seg_cnt, i;
	dma_addr_t da;
	size_t size;
	char *name;

	if (WARN_ON(!list_empty(&rproc->dump_segments))) {
		dev_err(&rproc->dev, "dump segment list already populated\n");
		return -EUCLEAN;
	}

	seg_cnt = le32_to_cpu(subsystem->region_count);
	ptr = ioremap((unsigned long)le64_to_cpu(subsystem->regions_baseptr),
		      seg_cnt * sizeof(struct minidump_region));
	if (!ptr)
		return -EFAULT;

	for (i = 0; i < seg_cnt; i++) {
		memcpy_fromio(&region, ptr + i, sizeof(region));
		if (le32_to_cpu(region.valid) == MD_REGION_VALID) {
			name = kstrndup(region.name, MAX_REGION_NAME_LENGTH - 1, GFP_KERNEL);
			if (!name) {
				iounmap(ptr);
				return -ENOMEM;
			}
			da = le64_to_cpu(region.address);
			size = le64_to_cpu(region.size);
			rproc_coredump_add_custom_segment(rproc, da, size, rproc_dumpfn_t, name);
		}
	}

	iounmap(ptr);
	return 0;
}

void qcom_minidump(struct rproc *rproc, unsigned int minidump_id,
		void (*rproc_dumpfn_t)(struct rproc *rproc,
		struct rproc_dump_segment *segment, void *dest, size_t offset,
		size_t size))
{
	int ret;
	struct minidump_subsystem *subsystem;
	struct minidump_global_toc *toc;

	/* Get Global minidump ToC*/
	toc = qcom_smem_get(QCOM_SMEM_HOST_ANY, SBL_MINIDUMP_SMEM_ID, NULL);

	/* check if global table pointer exists and init is set */
	if (IS_ERR(toc) || !toc->status) {
		dev_err(&rproc->dev, "Minidump TOC not found in SMEM\n");
		return;
	}

	/* Get subsystem table of contents using the minidump id */
	subsystem = &toc->subsystems[minidump_id];

	/**
	 * Collect minidump if SS ToC is valid and segment table
	 * is initialized in memory and encryption status is set.
	 */
	if (subsystem->regions_baseptr == 0 ||
	    le32_to_cpu(subsystem->status) != 1 ||
	    le32_to_cpu(subsystem->enabled) != MD_SS_ENABLED) {
		return rproc_coredump(rproc);
	}

	if (le32_to_cpu(subsystem->encryption_status) != MD_SS_ENCR_DONE) {
		dev_err(&rproc->dev, "Minidump not ready, skipping\n");
		return;
	}

	/**
	 * Clear out the dump segments populated by parse_fw before
	 * re-populating them with minidump segments.
	 */
	rproc_coredump_cleanup(rproc);

	ret = qcom_add_minidump_segments(rproc, subsystem, rproc_dumpfn_t);
	if (ret) {
		dev_err(&rproc->dev, "Failed with error: %d while adding minidump entries\n", ret);
		goto clean_minidump;
	}
	rproc_coredump_using_sections(rproc);
clean_minidump:
	qcom_minidump_cleanup(rproc);
}
EXPORT_SYMBOL_GPL(qcom_minidump);

static int glink_subdev_start(struct rproc_subdev *subdev)
{
	struct qcom_rproc_glink *glink = to_glink_subdev(subdev);

	glink->edge = qcom_glink_smem_register(glink->dev, glink->node);

	return PTR_ERR_OR_ZERO(glink->edge);
}

static void glink_subdev_stop(struct rproc_subdev *subdev, bool crashed)
{
	struct qcom_rproc_glink *glink = to_glink_subdev(subdev);

	qcom_glink_smem_unregister(glink->edge);
	glink->edge = NULL;
}

static void glink_subdev_unprepare(struct rproc_subdev *subdev)
{
	struct qcom_rproc_glink *glink = to_glink_subdev(subdev);

	qcom_glink_ssr_notify(glink->ssr_name);
}

/**
 * qcom_add_glink_subdev() - try to add a GLINK subdevice to rproc
 * @rproc:	rproc handle to parent the subdevice
 * @glink:	reference to a GLINK subdev context
 * @ssr_name:	identifier of the associated remoteproc for ssr notifications
 */
void qcom_add_glink_subdev(struct rproc *rproc, struct qcom_rproc_glink *glink,
			   const char *ssr_name)
{
	struct device *dev = &rproc->dev;

	glink->node = of_get_child_by_name(dev->parent->of_node, "glink-edge");
	if (!glink->node)
		return;

	glink->ssr_name = kstrdup_const(ssr_name, GFP_KERNEL);
	if (!glink->ssr_name)
		return;

	glink->dev = dev;
	glink->subdev.start = glink_subdev_start;
	glink->subdev.stop = glink_subdev_stop;
	glink->subdev.unprepare = glink_subdev_unprepare;

	rproc_add_subdev(rproc, &glink->subdev);
}
EXPORT_SYMBOL_GPL(qcom_add_glink_subdev);

/**
 * qcom_remove_glink_subdev() - remove a GLINK subdevice from rproc
 * @rproc:	rproc handle
 * @glink:	reference to a GLINK subdev context
 */
void qcom_remove_glink_subdev(struct rproc *rproc, struct qcom_rproc_glink *glink)
{
	if (!glink->node)
		return;

	rproc_remove_subdev(rproc, &glink->subdev);
	kfree_const(glink->ssr_name);
	of_node_put(glink->node);
}
EXPORT_SYMBOL_GPL(qcom_remove_glink_subdev);

/**
 * qcom_register_dump_segments() - register segments for coredump
 * @rproc:	remoteproc handle
 * @fw:		firmware header
 *
 * Register all segments of the ELF in the remoteproc coredump segment list
 *
 * Return: 0 on success, negative errno on failure.
 */
int qcom_register_dump_segments(struct rproc *rproc,
				const struct firmware *fw)
{
	const struct elf32_phdr *phdrs;
	const struct elf32_phdr *phdr;
	const struct elf32_hdr *ehdr;
	int ret;
	int i;

	ehdr = (struct elf32_hdr *)fw->data;
	phdrs = (struct elf32_phdr *)(ehdr + 1);

	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr = &phdrs[i];

		if (phdr->p_type != PT_LOAD)
			continue;

		if ((phdr->p_flags & QCOM_MDT_TYPE_MASK) == QCOM_MDT_TYPE_HASH)
			continue;

		if (!phdr->p_memsz)
			continue;

		ret = rproc_coredump_add_segment(rproc, phdr->p_paddr,
						 phdr->p_memsz);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_register_dump_segments);

static int smd_subdev_start(struct rproc_subdev *subdev)
{
	struct qcom_rproc_subdev *smd = to_smd_subdev(subdev);

	smd->edge = qcom_smd_register_edge(smd->dev, smd->node);

	return PTR_ERR_OR_ZERO(smd->edge);
}

static void smd_subdev_stop(struct rproc_subdev *subdev, bool crashed)
{
	struct qcom_rproc_subdev *smd = to_smd_subdev(subdev);

	qcom_smd_unregister_edge(smd->edge);
	smd->edge = NULL;
}

/**
 * qcom_add_smd_subdev() - try to add a SMD subdevice to rproc
 * @rproc:	rproc handle to parent the subdevice
 * @smd:	reference to a Qualcomm subdev context
 */
void qcom_add_smd_subdev(struct rproc *rproc, struct qcom_rproc_subdev *smd)
{
	struct device *dev = &rproc->dev;

	smd->node = of_get_child_by_name(dev->parent->of_node, "smd-edge");
	if (!smd->node)
		return;

	smd->dev = dev;
	smd->subdev.start = smd_subdev_start;
	smd->subdev.stop = smd_subdev_stop;

	rproc_add_subdev(rproc, &smd->subdev);
}
EXPORT_SYMBOL_GPL(qcom_add_smd_subdev);

/**
 * qcom_remove_smd_subdev() - remove the smd subdevice from rproc
 * @rproc:	rproc handle
 * @smd:	the SMD subdevice to remove
 */
void qcom_remove_smd_subdev(struct rproc *rproc, struct qcom_rproc_subdev *smd)
{
	if (!smd->node)
		return;

	rproc_remove_subdev(rproc, &smd->subdev);
	of_node_put(smd->node);
}
EXPORT_SYMBOL_GPL(qcom_remove_smd_subdev);

static struct qcom_ssr_subsystem *qcom_ssr_get_subsys(const char *name)
{
	struct qcom_ssr_subsystem *info;

	mutex_lock(&qcom_ssr_subsys_lock);
	/* Match in the global qcom_ssr_subsystem_list with name */
	list_for_each_entry(info, &qcom_ssr_subsystem_list, list)
		if (!strcmp(info->name, name))
			goto out;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		info = ERR_PTR(-ENOMEM);
		goto out;
	}
	info->name = kstrdup_const(name, GFP_KERNEL);
	srcu_init_notifier_head(&info->notifier_list);

	/* Add to global notification list */
	list_add_tail(&info->list, &qcom_ssr_subsystem_list);

out:
	mutex_unlock(&qcom_ssr_subsys_lock);
	return info;
}

/**
 * qcom_register_ssr_notifier() - register SSR notification handler
 * @name:	Subsystem's SSR name
 * @nb:		notifier_block to be invoked upon subsystem's state change
 *
 * This registers the @nb notifier block as part the notifier chain for a
 * remoteproc associated with @name. The notifier block's callback
 * will be invoked when the remote processor's SSR events occur
 * (pre/post startup and pre/post shutdown).
 *
 * Return: a subsystem cookie on success, ERR_PTR on failure.
 */
void *qcom_register_ssr_notifier(const char *name, struct notifier_block *nb)
{
	struct qcom_ssr_subsystem *info;

	info = qcom_ssr_get_subsys(name);
	if (IS_ERR(info))
		return info;

	srcu_notifier_chain_register(&info->notifier_list, nb);

	return &info->notifier_list;
}
EXPORT_SYMBOL_GPL(qcom_register_ssr_notifier);

/**
 * qcom_unregister_ssr_notifier() - unregister SSR notification handler
 * @notify:	subsystem cookie returned from qcom_register_ssr_notifier
 * @nb:		notifier_block to unregister
 *
 * This function will unregister the notifier from the particular notifier
 * chain.
 *
 * Return: 0 on success, %ENOENT otherwise.
 */
int qcom_unregister_ssr_notifier(void *notify, struct notifier_block *nb)
{
	return srcu_notifier_chain_unregister(notify, nb);
}
EXPORT_SYMBOL_GPL(qcom_unregister_ssr_notifier);

static int ssr_notify_prepare(struct rproc_subdev *subdev)
{
	struct qcom_rproc_ssr *ssr = to_ssr_subdev(subdev);
	struct qcom_ssr_notify_data data = {
		.name = ssr->info->name,
		.crashed = false,
	};

	srcu_notifier_call_chain(&ssr->info->notifier_list,
				 QCOM_SSR_BEFORE_POWERUP, &data);
	return 0;
}

static int ssr_notify_start(struct rproc_subdev *subdev)
{
	struct qcom_rproc_ssr *ssr = to_ssr_subdev(subdev);
	struct qcom_ssr_notify_data data = {
		.name = ssr->info->name,
		.crashed = false,
	};

	srcu_notifier_call_chain(&ssr->info->notifier_list,
				 QCOM_SSR_AFTER_POWERUP, &data);
	return 0;
}

static void ssr_notify_stop(struct rproc_subdev *subdev, bool crashed)
{
	struct qcom_rproc_ssr *ssr = to_ssr_subdev(subdev);
	struct qcom_ssr_notify_data data = {
		.name = ssr->info->name,
		.crashed = crashed,
	};

	srcu_notifier_call_chain(&ssr->info->notifier_list,
				 QCOM_SSR_BEFORE_SHUTDOWN, &data);
}

static void ssr_notify_unprepare(struct rproc_subdev *subdev)
{
	struct qcom_rproc_ssr *ssr = to_ssr_subdev(subdev);
	struct qcom_ssr_notify_data data = {
		.name = ssr->info->name,
		.crashed = false,
	};

	srcu_notifier_call_chain(&ssr->info->notifier_list,
				 QCOM_SSR_AFTER_SHUTDOWN, &data);
}

/**
 * qcom_add_ssr_subdev() - register subdevice as restart notification source
 * @rproc:	rproc handle
 * @ssr:	SSR subdevice handle
 * @ssr_name:	identifier to use for notifications originating from @rproc
 *
 * As the @ssr is registered with the @rproc SSR events will be sent to all
 * registered listeners for the remoteproc when it's SSR events occur
 * (pre/post startup and pre/post shutdown).
 */
void qcom_add_ssr_subdev(struct rproc *rproc, struct qcom_rproc_ssr *ssr,
			 const char *ssr_name)
{
	struct qcom_ssr_subsystem *info;

	info = qcom_ssr_get_subsys(ssr_name);
	if (IS_ERR(info)) {
		dev_err(&rproc->dev, "Failed to add ssr subdevice\n");
		return;
	}

	ssr->info = info;
	ssr->subdev.prepare = ssr_notify_prepare;
	ssr->subdev.start = ssr_notify_start;
	ssr->subdev.stop = ssr_notify_stop;
	ssr->subdev.unprepare = ssr_notify_unprepare;

	rproc_add_subdev(rproc, &ssr->subdev);
}
EXPORT_SYMBOL_GPL(qcom_add_ssr_subdev);

/**
 * qcom_remove_ssr_subdev() - remove subdevice as restart notification source
 * @rproc:	rproc handle
 * @ssr:	SSR subdevice handle
 */
void qcom_remove_ssr_subdev(struct rproc *rproc, struct qcom_rproc_ssr *ssr)
{
	rproc_remove_subdev(rproc, &ssr->subdev);
	ssr->info = NULL;
}
EXPORT_SYMBOL_GPL(qcom_remove_ssr_subdev);

MODULE_DESCRIPTION("Qualcomm Remoteproc helper driver");
MODULE_LICENSE("GPL v2");
