// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm Peripheral Image Loader helpers
 *
 * Copyright (C) 2016 Linaro Ltd
 * Copyright (C) 2015 Sony Mobile Communications Inc
 * Copyright (c) 2012-2013, 2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/remoteproc.h>
#include <linux/rpmsg/qcom_glink.h>
#include <linux/rpmsg/qcom_smd.h>
#include <linux/slab.h>
#include <linux/soc/qcom/mdt_loader.h>
#include <linux/soc/qcom/smem.h>
#include <linux/devcoredump.h>
#include <trace/hooks/remoteproc.h>
#include <trace/events/rproc_qcom.h>

#include "remoteproc_elf_helpers.h"
#include "remoteproc_internal.h"
#include "qcom_common.h"

#define SSR_NOTIF_TIMEOUT CONFIG_RPROC_SSR_NOTIF_TIMEOUT

#define to_glink_subdev(d) container_of(d, struct qcom_rproc_glink, subdev)
#define to_smd_subdev(d) container_of(d, struct qcom_rproc_subdev, subdev)
#define to_ssr_subdev(d) container_of(d, struct qcom_rproc_ssr, subdev)

#define GLINK_SUBDEV_NAME	"glink"
#define SMD_SUBDEV_NAME		"smd"
#define SSR_SUBDEV_NAME		"ssr"
#define QMP_MSG_LEN	64

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
	struct srcu_notifier_head early_notifier_list;
	struct list_head list;
};

static struct kobject *sysfs_kobject;
bool qcom_device_shutdown_in_progress;
EXPORT_SYMBOL(qcom_device_shutdown_in_progress);

static bool qcom_collect_both_coredumps;

static LIST_HEAD(qcom_ssr_subsystem_list);
static DEFINE_MUTEX(qcom_ssr_subsys_lock);

static const char * const ssr_timeout_msg = "srcu notifier chain for %s:%s taking too long";

static ssize_t qcom_rproc_shutdown_request_store(struct kobject *kobj, struct kobj_attribute *attr,
						 const char *buf, size_t count)
{
	bool val;
	int ret;

	ret = kstrtobool(buf, &val);
	if (ret)
		return ret;

	qcom_device_shutdown_in_progress = val;
	pr_info("qcom rproc: Device shutdown requested: %s\n", val ? "true" : "false");
	return count;
}
static struct kobj_attribute shutdown_requested_attr = __ATTR(shutdown_in_progress, 0220, NULL,
							  qcom_rproc_shutdown_request_store);

static ssize_t qcom_collect_both_coredumps_show(struct kobject *kobj, struct kobj_attribute *attr,
						char *buf)
{
	return scnprintf(buf, 3, "%u\n", qcom_collect_both_coredumps);
}

static ssize_t qcom_collect_both_coredumps_store(struct kobject *kobj, struct kobj_attribute *attr,
						 const char *buf, size_t count)
{
	bool val;
	int ret;

	ret = kstrtobool(buf, &val);
	if (ret)
		return ret;

	qcom_collect_both_coredumps = val;
	pr_info("qcom rproc: Collect both coredumps: %s\n", val ? "true" : "false");
	return count;
}

static struct kobj_attribute both_coredumps_attr =
	__ATTR(collect_both_coredumps, 0644, qcom_collect_both_coredumps_show,
	       qcom_collect_both_coredumps_store);

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
				      rproc_dumpfn_t dumpfn)
{
	struct minidump_region __iomem *ptr;
	struct minidump_region region;
	int seg_cnt, i;
	dma_addr_t da;
	size_t size;
	char *name, *dbg_buf_name = "md_dbg_buf";
	int len = strlen(dbg_buf_name);

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
		if (region.valid == MD_REGION_VALID) {
			name = kstrdup(region.name, GFP_KERNEL);
			if (!name) {
				iounmap(ptr);
				return -ENOMEM;
			}
			da = le64_to_cpu(region.address);
			size = le32_to_cpu(region.size);
			if (le32_to_cpu(subsystem->encryption_status) != MD_SS_ENCR_DONE) {
				if (!i && len < MAX_REGION_NAME_LENGTH &&
				    !strcmp(name, dbg_buf_name))
					rproc_coredump_add_custom_segment(rproc, da, size, dumpfn,
									  name);
				break;
			}
			rproc_coredump_add_custom_segment(rproc, da, size, dumpfn, name);
		}
	}

	iounmap(ptr);
	return 0;
}

static void qcom_rproc_minidump(struct rproc *rproc, struct device *md_dev)
{
	struct rproc_dump_segment *segment;
	void *shdr;
	void *ehdr;
	size_t data_size;
	size_t strtbl_size = 0;
	size_t strtbl_index = 1;
	size_t offset;
	void *data;
	u8 class = rproc->elf_class;
	int shnum;
	unsigned int dump_conf = rproc->dump_conf;
	char *str_tbl = "STR_TBL";

	if (list_empty(&rproc->dump_segments) ||
	    dump_conf == RPROC_COREDUMP_DISABLED)
		return;

	if (class == ELFCLASSNONE) {
		dev_err(&rproc->dev, "Elf class is not set\n");
		return;
	}

	/*
	 * We allocate two extra section headers. The first one is null.
	 * Second section header is for the string table. Also space is
	 * allocated for string table.
	 */
	data_size = elf_size_of_hdr(class) + 2 * elf_size_of_shdr(class);
	shnum = 2;

	/* the extra byte is for the null character at index 0 */
	strtbl_size += strlen(str_tbl) + 2;

	list_for_each_entry(segment, &rproc->dump_segments, node) {
		data_size += elf_size_of_shdr(class);
		strtbl_size += strlen(segment->priv) + 1;
		data_size += segment->size;
		shnum++;
	}

	data_size += strtbl_size;

	data = vmalloc(data_size);
	if (!data)
		return;

	ehdr = data;
	memset(ehdr, 0, elf_size_of_hdr(class));
	/* e_ident field is common for both elf32 and elf64 */
	elf_hdr_init_ident(ehdr, class);
	elf_hdr_set_e_type(class, ehdr, ET_CORE);
	elf_hdr_set_e_machine(class, ehdr, rproc->elf_machine);
	elf_hdr_set_e_version(class, ehdr, EV_CURRENT);
	elf_hdr_set_e_entry(class, ehdr, rproc->bootaddr);
	elf_hdr_set_e_shoff(class, ehdr, elf_size_of_hdr(class));
	elf_hdr_set_e_ehsize(class, ehdr, elf_size_of_hdr(class));
	elf_hdr_set_e_shentsize(class, ehdr, elf_size_of_shdr(class));
	elf_hdr_set_e_shnum(class, ehdr, shnum);
	elf_hdr_set_e_shstrndx(class, ehdr, 1);

	/*
	 * The zeroth index of the section header is reserved and is rarely used.
	 * Set the section header as null (SHN_UNDEF) and move to the next one.
	 */
	shdr = data + elf_hdr_get_e_shoff(class, ehdr);
	memset(shdr, 0, elf_size_of_shdr(class));
	shdr += elf_size_of_shdr(class);

	/* Initialize the string table. */
	offset = elf_hdr_get_e_shoff(class, ehdr) +
		 elf_size_of_shdr(class) * elf_hdr_get_e_shnum(class, ehdr);
	memset(data + offset, 0, strtbl_size);

	/* Fill in the string table section header. */
	memset(shdr, 0, elf_size_of_shdr(class));
	elf_shdr_set_sh_type(class, shdr, SHT_STRTAB);
	elf_shdr_set_sh_offset(class, shdr, offset);
	elf_shdr_set_sh_size(class, shdr, strtbl_size);
	elf_shdr_set_sh_entsize(class, shdr, 0);
	elf_shdr_set_sh_flags(class, shdr, 0);
	elf_shdr_set_sh_name(class, shdr, elf_strtbl_add(str_tbl, ehdr, class, &strtbl_index));
	offset += elf_shdr_get_sh_size(class, shdr);
	shdr += elf_size_of_shdr(class);

	list_for_each_entry(segment, &rproc->dump_segments, node) {
		memset(shdr, 0, elf_size_of_shdr(class));
		elf_shdr_set_sh_type(class, shdr, SHT_PROGBITS);
		elf_shdr_set_sh_offset(class, shdr, offset);
		elf_shdr_set_sh_addr(class, shdr, segment->da);
		elf_shdr_set_sh_size(class, shdr, segment->size);
		elf_shdr_set_sh_entsize(class, shdr, 0);
		elf_shdr_set_sh_flags(class, shdr, SHF_WRITE);
		elf_shdr_set_sh_name(class, shdr,
				     elf_strtbl_add(segment->priv, ehdr, class, &strtbl_index));

		/* No need to copy segments for inline dumps */
		segment->dump(rproc, segment, data + offset, 0, segment->size);
		offset += elf_shdr_get_sh_size(class, shdr);
		shdr += elf_size_of_shdr(class);
	}

	dev_coredumpv(md_dev, data, data_size, GFP_KERNEL);
}

int qcom_rproc_toggle_load_state(struct qmp *qmp, const char *name, bool enable)
{
	char buf[QMP_MSG_LEN] = {};

	snprintf(buf, sizeof(buf),
		 "{class: image, res: load_state, name: %s, val: %s}",
		 name, enable ? "on" : "off");
	return qmp_send(qmp, buf, sizeof(buf));
}
EXPORT_SYMBOL_GPL(qcom_rproc_toggle_load_state);

void qcom_minidump(struct rproc *rproc, struct device *md_dev, unsigned int minidump_id,
		   rproc_dumpfn_t dumpfn, bool both_dumps)
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


	if (both_dumps && IS_ENABLED(CONFIG_QCOM_RPROC_BOTH_DUMPS) &&
	    qcom_collect_both_coredumps)
		rproc_coredump(rproc);

	if (le32_to_cpu(subsystem->encryption_status) != MD_SS_ENCR_DONE)
		dev_err(&rproc->dev, "encryption_status != MD_SS_ENCR_DONE\n");

	rproc_coredump_cleanup(rproc);

	ret = qcom_add_minidump_segments(rproc, subsystem, dumpfn);
	if (ret) {
		dev_err(&rproc->dev, "Failed with error: %d while adding minidump entries\n", ret);
		goto clean_minidump;
	}

	if (rproc->elf_class == ELFCLASS64)
		qcom_rproc_minidump(rproc, md_dev);
	else
		rproc_coredump(rproc);

clean_minidump:
	qcom_minidump_cleanup(rproc);
}
EXPORT_SYMBOL_GPL(qcom_minidump);

static int glink_early_ssr_notifier_event(struct notifier_block *this,
					   unsigned long code, void *data)
{
	struct qcom_rproc_glink *glink = container_of(this, struct qcom_rproc_glink, nb);

	qcom_glink_early_ssr_notify(glink->edge);
	return NOTIFY_DONE;
}

static int glink_subdev_prepare(struct rproc_subdev *subdev)
{
	struct qcom_rproc_glink *glink = to_glink_subdev(subdev);

	trace_rproc_qcom_event(dev_name(glink->dev->parent), GLINK_SUBDEV_NAME, "prepare");

	glink->edge = qcom_glink_smem_register(glink->dev, glink->node);

	return PTR_ERR_OR_ZERO(glink->edge);
}

static int glink_subdev_start(struct rproc_subdev *subdev)
{
	struct qcom_rproc_glink *glink = to_glink_subdev(subdev);

	trace_rproc_qcom_event(dev_name(glink->dev->parent), GLINK_SUBDEV_NAME, "start");

	glink->nb.notifier_call = glink_early_ssr_notifier_event;

	glink->notifier_handle = qcom_register_early_ssr_notifier(glink->ssr_name, &glink->nb);
	if (IS_ERR(glink->notifier_handle)) {
		dev_err(glink->dev, "Failed to register for SSR notifier\n");
		glink->notifier_handle = NULL;
	}

	return qcom_glink_smem_start(glink->edge);
}

static void glink_subdev_stop(struct rproc_subdev *subdev, bool crashed)
{
	struct qcom_rproc_glink *glink = to_glink_subdev(subdev);
	struct rproc *rproc = container_of(glink->dev, struct rproc, dev);
	int ret;

	if (!glink->edge  || (crashed && rproc->recovery_disabled))
		return;
	trace_rproc_qcom_event(dev_name(glink->dev->parent), GLINK_SUBDEV_NAME,
			       crashed ? "crash stop" : "stop");

	ret = qcom_unregister_early_ssr_notifier(glink->notifier_handle, &glink->nb);
	if (ret)
		dev_err(glink->dev, "Error in unregistering notifier\n");
	glink->notifier_handle = NULL;

	qcom_glink_smem_unregister(glink->edge);
	glink->edge = NULL;
}

static void glink_subdev_unprepare(struct rproc_subdev *subdev)
{
	struct qcom_rproc_glink *glink = to_glink_subdev(subdev);

	trace_rproc_qcom_event(dev_name(glink->dev->parent), GLINK_SUBDEV_NAME, "unprepare");

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
	glink->subdev.prepare = glink_subdev_prepare;
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

	trace_rproc_qcom_event(dev_name(smd->dev->parent), SMD_SUBDEV_NAME, "start");

	smd->edge = qcom_smd_register_edge(smd->dev, smd->node);

	return PTR_ERR_OR_ZERO(smd->edge);
}

static void smd_subdev_stop(struct rproc_subdev *subdev, bool crashed)
{
	struct qcom_rproc_subdev *smd = to_smd_subdev(subdev);

	if (!smd->edge)
		return;

	trace_rproc_qcom_event(dev_name(smd->dev->parent), SMD_SUBDEV_NAME,
			       crashed ? "crash stop" : "stop");

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

struct qcom_ssr_subsystem *qcom_ssr_get_subsys(const char *name)
{
	struct qcom_ssr_subsystem *info;

	if (!name)
		return ERR_PTR(-EINVAL);

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
	srcu_init_notifier_head(&info->early_notifier_list);

	/* Add to global notification list */
	list_add_tail(&info->list, &qcom_ssr_subsystem_list);

out:
	mutex_unlock(&qcom_ssr_subsys_lock);
	return info;
}
EXPORT_SYMBOL(qcom_ssr_get_subsys);

void *qcom_register_early_ssr_notifier(const char *name, struct notifier_block *nb)
{
	struct qcom_ssr_subsystem *info;

	info = qcom_ssr_get_subsys(name);
	if (IS_ERR(info))
		return info;

	srcu_notifier_chain_register(&info->early_notifier_list, nb);

	return &info->early_notifier_list;
}
EXPORT_SYMBOL(qcom_register_early_ssr_notifier);

int qcom_unregister_early_ssr_notifier(void *notify, struct notifier_block *nb)
{
	return srcu_notifier_chain_unregister(notify, nb);
}
EXPORT_SYMBOL(qcom_unregister_early_ssr_notifier);

void qcom_notify_early_ssr_clients(struct rproc_subdev *subdev)
{
	struct qcom_rproc_ssr *ssr = to_ssr_subdev(subdev);

	srcu_notifier_call_chain(&ssr->info->early_notifier_list, QCOM_SSR_BEFORE_SHUTDOWN, NULL);
}
EXPORT_SYMBOL(qcom_notify_early_ssr_clients);

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

static void ssr_notif_timeout_handler(struct timer_list *t)
{
	struct qcom_rproc_ssr *ssr = from_timer(ssr, t, timer);

	if (IS_ENABLED(CONFIG_QCOM_PANIC_ON_NOTIF_TIMEOUT) &&
	    system_state != SYSTEM_RESTART &&
	    system_state != SYSTEM_POWER_OFF &&
	    system_state != SYSTEM_HALT &&
	    !qcom_device_shutdown_in_progress)
		panic(ssr_timeout_msg, ssr->info->name, subdevice_state_string[ssr->notification]);
	else
		WARN(1, ssr_timeout_msg, ssr->info->name,
		     subdevice_state_string[ssr->notification]);
}

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

int qcom_notify_ssr_clients(struct qcom_ssr_subsystem *info, int state,
			struct qcom_ssr_notify_data *data)
{
	struct qcom_ssr_subsystem *subsys = info;

	if (!subsys)
		return -EINVAL;

	if (state < 0)
		return -EINVAL;

	return srcu_notifier_call_chain(&info->notifier_list, state, data);
}
EXPORT_SYMBOL(qcom_notify_ssr_clients);

static inline void notify_ssr_clients(struct qcom_rproc_ssr *ssr, struct qcom_ssr_notify_data *data)
{
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(SSR_NOTIF_TIMEOUT);
	mod_timer(&ssr->timer, timeout);
	srcu_notifier_call_chain(&ssr->info->notifier_list, ssr->notification, data);
	del_timer_sync(&ssr->timer);
}

static int ssr_notify_prepare(struct rproc_subdev *subdev)
{
	struct qcom_rproc_ssr *ssr = to_ssr_subdev(subdev);
	struct qcom_ssr_notify_data data = {
		.name = ssr->info->name,
		.crashed = false,
	};

	trace_rproc_qcom_event(ssr->info->name, SSR_SUBDEV_NAME, "prepare");

	ssr->notification = QCOM_SSR_BEFORE_POWERUP;
	notify_ssr_clients(ssr, &data);
	return 0;
}

static int ssr_notify_start(struct rproc_subdev *subdev)
{
	struct qcom_rproc_ssr *ssr = to_ssr_subdev(subdev);
	struct qcom_ssr_notify_data data = {
		.name = ssr->info->name,
		.crashed = false,
	};

	trace_rproc_qcom_event(ssr->info->name, SSR_SUBDEV_NAME, "start");

	ssr->notification = QCOM_SSR_AFTER_POWERUP;
	notify_ssr_clients(ssr, &data);
	return 0;
}

static void ssr_notify_stop(struct rproc_subdev *subdev, bool crashed)
{
	struct qcom_rproc_ssr *ssr = to_ssr_subdev(subdev);
	struct qcom_ssr_notify_data data = {
		.name = ssr->info->name,
		.crashed = crashed,
	};

	trace_rproc_qcom_event(ssr->info->name, SSR_SUBDEV_NAME, crashed ? "crash stop" : "stop");

	ssr->notification = QCOM_SSR_BEFORE_SHUTDOWN;
	notify_ssr_clients(ssr, &data);
}

static void ssr_notify_unprepare(struct rproc_subdev *subdev)
{
	struct qcom_rproc_ssr *ssr = to_ssr_subdev(subdev);
	struct qcom_ssr_notify_data data = {
		.name = ssr->info->name,
		.crashed = false,
	};

	trace_rproc_qcom_event(ssr->info->name, SSR_SUBDEV_NAME, "unprepare");

	ssr->notification = QCOM_SSR_AFTER_SHUTDOWN;
	notify_ssr_clients(ssr, &data);
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

	timer_setup(&ssr->timer, ssr_notif_timeout_handler, 0);

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

static void qcom_check_ssr_status(void *data, struct rproc *rproc)
{
	if (!atomic_read(&rproc->power) ||
	    rproc->state == RPROC_RUNNING ||
	    qcom_device_shutdown_in_progress ||
	    system_state == SYSTEM_RESTART ||
	    system_state == SYSTEM_POWER_OFF ||
	    system_state == SYSTEM_HALT)
		return;

	panic("Panicking, remoteproc %s failed to recover!\n", rproc->name);
}

static void rproc_recovery_notifier(void *data, struct rproc *rproc)
{
	const char *recovery = rproc->recovery_disabled ? "disabled" : "enabled";

	trace_rproc_qcom_event(rproc->name, "recovery", recovery);
	pr_info("qcom rproc: %s: recovery %s\n", rproc->name, recovery);
}

static int __init qcom_common_init(void)
{
	int ret = 0;

	qcom_device_shutdown_in_progress = false;

	sysfs_kobject = kobject_create_and_add("qcom_rproc", kernel_kobj);
	if (!sysfs_kobject) {
		pr_err("qcom rproc: failed to create sysfs kobject\n");
		return -EINVAL;
	}

	ret = sysfs_create_file(sysfs_kobject, &shutdown_requested_attr.attr);
	if (ret) {
		pr_err("qcom rproc: failed to create sysfs file\n");
		goto remove_kobject;
	}

	ret = sysfs_create_file(sysfs_kobject, &both_coredumps_attr.attr);
	if (ret) {
		pr_err("qcom rproc: failed to create both_coredumps sysfs file\n");
		goto remove_shutdown_sysfs;
	}

	ret = register_trace_android_vh_rproc_recovery(qcom_check_ssr_status, NULL);
	if (ret) {
		pr_err("qcom rproc: failed to register trace hooks\n");
		goto remove_coredump_sysfs;
	}

	ret = register_trace_android_vh_rproc_recovery_set(rproc_recovery_notifier, NULL);
	if (ret) {
		pr_err("qcom rproc: failed to register recovery_set vendor hook\n");
		goto unregister_rproc_recovery_vh;
	}

	return 0;

unregister_rproc_recovery_vh:
	unregister_trace_android_vh_rproc_recovery(qcom_check_ssr_status, NULL);
remove_coredump_sysfs:
	sysfs_remove_file(sysfs_kobject, &both_coredumps_attr.attr);
remove_shutdown_sysfs:
	sysfs_remove_file(sysfs_kobject, &shutdown_requested_attr.attr);
remove_kobject:
	kobject_put(sysfs_kobject);
	return ret;

}
module_init(qcom_common_init);

static void __exit qcom_common_exit(void)
{
	unregister_trace_android_vh_rproc_recovery_set(rproc_recovery_notifier, NULL);
	sysfs_remove_file(sysfs_kobject, &both_coredumps_attr.attr);
	sysfs_remove_file(sysfs_kobject, &shutdown_requested_attr.attr);
	kobject_put(sysfs_kobject);
	unregister_trace_android_vh_rproc_recovery(qcom_check_ssr_status, NULL);
}
module_exit(qcom_common_exit);

MODULE_DESCRIPTION("Qualcomm Remoteproc helper driver");
MODULE_LICENSE("GPL v2");
