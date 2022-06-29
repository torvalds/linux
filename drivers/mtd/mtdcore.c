// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Core registration and callback routines for MTD
 * drivers and users.
 *
 * Copyright © 1999-2010 David Woodhouse <dwmw2@infradead.org>
 * Copyright © 2006      Red Hat UK Limited 
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/ioctl.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/proc_fs.h>
#include <linux/idr.h>
#include <linux/backing-dev.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/reboot.h>
#include <linux/leds.h>
#include <linux/debugfs.h>
#include <linux/nvmem-provider.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include "mtdcore.h"

struct backing_dev_info *mtd_bdi;

#ifdef CONFIG_PM_SLEEP

static int mtd_cls_suspend(struct device *dev)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return mtd ? mtd_suspend(mtd) : 0;
}

static int mtd_cls_resume(struct device *dev)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	if (mtd)
		mtd_resume(mtd);
	return 0;
}

static SIMPLE_DEV_PM_OPS(mtd_cls_pm_ops, mtd_cls_suspend, mtd_cls_resume);
#define MTD_CLS_PM_OPS (&mtd_cls_pm_ops)
#else
#define MTD_CLS_PM_OPS NULL
#endif

static struct class mtd_class = {
	.name = "mtd",
	.owner = THIS_MODULE,
	.pm = MTD_CLS_PM_OPS,
};

static DEFINE_IDR(mtd_idr);

/* These are exported solely for the purpose of mtd_blkdevs.c. You
   should not use them for _anything_ else */
DEFINE_MUTEX(mtd_table_mutex);
EXPORT_SYMBOL_GPL(mtd_table_mutex);

struct mtd_info *__mtd_next_device(int i)
{
	return idr_get_next(&mtd_idr, &i);
}
EXPORT_SYMBOL_GPL(__mtd_next_device);

static LIST_HEAD(mtd_notifiers);


#define MTD_DEVT(index) MKDEV(MTD_CHAR_MAJOR, (index)*2)

/* REVISIT once MTD uses the driver model better, whoever allocates
 * the mtd_info will probably want to use the release() hook...
 */
static void mtd_release(struct device *dev)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	dev_t index = MTD_DEVT(mtd->index);

	/* remove /dev/mtdXro node */
	device_destroy(&mtd_class, index + 1);
}

#define MTD_DEVICE_ATTR_RO(name) \
static DEVICE_ATTR(name, 0444, mtd_##name##_show, NULL)

#define MTD_DEVICE_ATTR_RW(name) \
static DEVICE_ATTR(name, 0644, mtd_##name##_show, mtd_##name##_store)

static ssize_t mtd_type_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	char *type;

	switch (mtd->type) {
	case MTD_ABSENT:
		type = "absent";
		break;
	case MTD_RAM:
		type = "ram";
		break;
	case MTD_ROM:
		type = "rom";
		break;
	case MTD_NORFLASH:
		type = "nor";
		break;
	case MTD_NANDFLASH:
		type = "nand";
		break;
	case MTD_DATAFLASH:
		type = "dataflash";
		break;
	case MTD_UBIVOLUME:
		type = "ubi";
		break;
	case MTD_MLCNANDFLASH:
		type = "mlc-nand";
		break;
	default:
		type = "unknown";
	}

	return sysfs_emit(buf, "%s\n", type);
}
MTD_DEVICE_ATTR_RO(type);

static ssize_t mtd_flags_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%lx\n", (unsigned long)mtd->flags);
}
MTD_DEVICE_ATTR_RO(flags);

static ssize_t mtd_size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%llu\n", (unsigned long long)mtd->size);
}
MTD_DEVICE_ATTR_RO(size);

static ssize_t mtd_erasesize_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lu\n", (unsigned long)mtd->erasesize);
}
MTD_DEVICE_ATTR_RO(erasesize);

static ssize_t mtd_writesize_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lu\n", (unsigned long)mtd->writesize);
}
MTD_DEVICE_ATTR_RO(writesize);

static ssize_t mtd_subpagesize_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	unsigned int subpagesize = mtd->writesize >> mtd->subpage_sft;

	return sysfs_emit(buf, "%u\n", subpagesize);
}
MTD_DEVICE_ATTR_RO(subpagesize);

static ssize_t mtd_oobsize_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lu\n", (unsigned long)mtd->oobsize);
}
MTD_DEVICE_ATTR_RO(oobsize);

static ssize_t mtd_oobavail_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", mtd->oobavail);
}
MTD_DEVICE_ATTR_RO(oobavail);

static ssize_t mtd_numeraseregions_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", mtd->numeraseregions);
}
MTD_DEVICE_ATTR_RO(numeraseregions);

static ssize_t mtd_name_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", mtd->name);
}
MTD_DEVICE_ATTR_RO(name);

static ssize_t mtd_ecc_strength_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", mtd->ecc_strength);
}
MTD_DEVICE_ATTR_RO(ecc_strength);

static ssize_t mtd_bitflip_threshold_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", mtd->bitflip_threshold);
}

static ssize_t mtd_bitflip_threshold_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	unsigned int bitflip_threshold;
	int retval;

	retval = kstrtouint(buf, 0, &bitflip_threshold);
	if (retval)
		return retval;

	mtd->bitflip_threshold = bitflip_threshold;
	return count;
}
MTD_DEVICE_ATTR_RW(bitflip_threshold);

static ssize_t mtd_ecc_step_size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", mtd->ecc_step_size);

}
MTD_DEVICE_ATTR_RO(ecc_step_size);

static ssize_t mtd_corrected_bits_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	struct mtd_ecc_stats *ecc_stats = &mtd->ecc_stats;

	return sysfs_emit(buf, "%u\n", ecc_stats->corrected);
}
MTD_DEVICE_ATTR_RO(corrected_bits);	/* ecc stats corrected */

static ssize_t mtd_ecc_failures_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	struct mtd_ecc_stats *ecc_stats = &mtd->ecc_stats;

	return sysfs_emit(buf, "%u\n", ecc_stats->failed);
}
MTD_DEVICE_ATTR_RO(ecc_failures);	/* ecc stats errors */

static ssize_t mtd_bad_blocks_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	struct mtd_ecc_stats *ecc_stats = &mtd->ecc_stats;

	return sysfs_emit(buf, "%u\n", ecc_stats->badblocks);
}
MTD_DEVICE_ATTR_RO(bad_blocks);

static ssize_t mtd_bbt_blocks_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	struct mtd_ecc_stats *ecc_stats = &mtd->ecc_stats;

	return sysfs_emit(buf, "%u\n", ecc_stats->bbtblocks);
}
MTD_DEVICE_ATTR_RO(bbt_blocks);

static struct attribute *mtd_attrs[] = {
	&dev_attr_type.attr,
	&dev_attr_flags.attr,
	&dev_attr_size.attr,
	&dev_attr_erasesize.attr,
	&dev_attr_writesize.attr,
	&dev_attr_subpagesize.attr,
	&dev_attr_oobsize.attr,
	&dev_attr_oobavail.attr,
	&dev_attr_numeraseregions.attr,
	&dev_attr_name.attr,
	&dev_attr_ecc_strength.attr,
	&dev_attr_ecc_step_size.attr,
	&dev_attr_corrected_bits.attr,
	&dev_attr_ecc_failures.attr,
	&dev_attr_bad_blocks.attr,
	&dev_attr_bbt_blocks.attr,
	&dev_attr_bitflip_threshold.attr,
	NULL,
};
ATTRIBUTE_GROUPS(mtd);

static const struct device_type mtd_devtype = {
	.name		= "mtd",
	.groups		= mtd_groups,
	.release	= mtd_release,
};

static bool mtd_expert_analysis_mode;

#ifdef CONFIG_DEBUG_FS
bool mtd_check_expert_analysis_mode(void)
{
	const char *mtd_expert_analysis_warning =
		"Bad block checks have been entirely disabled.\n"
		"This is only reserved for post-mortem forensics and debug purposes.\n"
		"Never enable this mode if you do not know what you are doing!\n";

	return WARN_ONCE(mtd_expert_analysis_mode, mtd_expert_analysis_warning);
}
EXPORT_SYMBOL_GPL(mtd_check_expert_analysis_mode);
#endif

static struct dentry *dfs_dir_mtd;

static void mtd_debugfs_populate(struct mtd_info *mtd)
{
	struct device *dev = &mtd->dev;

	if (IS_ERR_OR_NULL(dfs_dir_mtd))
		return;

	mtd->dbg.dfs_dir = debugfs_create_dir(dev_name(dev), dfs_dir_mtd);
}

#ifndef CONFIG_MMU
unsigned mtd_mmap_capabilities(struct mtd_info *mtd)
{
	switch (mtd->type) {
	case MTD_RAM:
		return NOMMU_MAP_COPY | NOMMU_MAP_DIRECT | NOMMU_MAP_EXEC |
			NOMMU_MAP_READ | NOMMU_MAP_WRITE;
	case MTD_ROM:
		return NOMMU_MAP_COPY | NOMMU_MAP_DIRECT | NOMMU_MAP_EXEC |
			NOMMU_MAP_READ;
	default:
		return NOMMU_MAP_COPY;
	}
}
EXPORT_SYMBOL_GPL(mtd_mmap_capabilities);
#endif

static int mtd_reboot_notifier(struct notifier_block *n, unsigned long state,
			       void *cmd)
{
	struct mtd_info *mtd;

	mtd = container_of(n, struct mtd_info, reboot_notifier);
	mtd->_reboot(mtd);

	return NOTIFY_DONE;
}

/**
 * mtd_wunit_to_pairing_info - get pairing information of a wunit
 * @mtd: pointer to new MTD device info structure
 * @wunit: write unit we are interested in
 * @info: returned pairing information
 *
 * Retrieve pairing information associated to the wunit.
 * This is mainly useful when dealing with MLC/TLC NANDs where pages can be
 * paired together, and where programming a page may influence the page it is
 * paired with.
 * The notion of page is replaced by the term wunit (write-unit) to stay
 * consistent with the ->writesize field.
 *
 * The @wunit argument can be extracted from an absolute offset using
 * mtd_offset_to_wunit(). @info is filled with the pairing information attached
 * to @wunit.
 *
 * From the pairing info the MTD user can find all the wunits paired with
 * @wunit using the following loop:
 *
 * for (i = 0; i < mtd_pairing_groups(mtd); i++) {
 *	info.pair = i;
 *	mtd_pairing_info_to_wunit(mtd, &info);
 *	...
 * }
 */
int mtd_wunit_to_pairing_info(struct mtd_info *mtd, int wunit,
			      struct mtd_pairing_info *info)
{
	struct mtd_info *master = mtd_get_master(mtd);
	int npairs = mtd_wunit_per_eb(master) / mtd_pairing_groups(master);

	if (wunit < 0 || wunit >= npairs)
		return -EINVAL;

	if (master->pairing && master->pairing->get_info)
		return master->pairing->get_info(master, wunit, info);

	info->group = 0;
	info->pair = wunit;

	return 0;
}
EXPORT_SYMBOL_GPL(mtd_wunit_to_pairing_info);

/**
 * mtd_pairing_info_to_wunit - get wunit from pairing information
 * @mtd: pointer to new MTD device info structure
 * @info: pairing information struct
 *
 * Returns a positive number representing the wunit associated to the info
 * struct, or a negative error code.
 *
 * This is the reverse of mtd_wunit_to_pairing_info(), and can help one to
 * iterate over all wunits of a given pair (see mtd_wunit_to_pairing_info()
 * doc).
 *
 * It can also be used to only program the first page of each pair (i.e.
 * page attached to group 0), which allows one to use an MLC NAND in
 * software-emulated SLC mode:
 *
 * info.group = 0;
 * npairs = mtd_wunit_per_eb(mtd) / mtd_pairing_groups(mtd);
 * for (info.pair = 0; info.pair < npairs; info.pair++) {
 *	wunit = mtd_pairing_info_to_wunit(mtd, &info);
 *	mtd_write(mtd, mtd_wunit_to_offset(mtd, blkoffs, wunit),
 *		  mtd->writesize, &retlen, buf + (i * mtd->writesize));
 * }
 */
int mtd_pairing_info_to_wunit(struct mtd_info *mtd,
			      const struct mtd_pairing_info *info)
{
	struct mtd_info *master = mtd_get_master(mtd);
	int ngroups = mtd_pairing_groups(master);
	int npairs = mtd_wunit_per_eb(master) / ngroups;

	if (!info || info->pair < 0 || info->pair >= npairs ||
	    info->group < 0 || info->group >= ngroups)
		return -EINVAL;

	if (master->pairing && master->pairing->get_wunit)
		return mtd->pairing->get_wunit(master, info);

	return info->pair;
}
EXPORT_SYMBOL_GPL(mtd_pairing_info_to_wunit);

/**
 * mtd_pairing_groups - get the number of pairing groups
 * @mtd: pointer to new MTD device info structure
 *
 * Returns the number of pairing groups.
 *
 * This number is usually equal to the number of bits exposed by a single
 * cell, and can be used in conjunction with mtd_pairing_info_to_wunit()
 * to iterate over all pages of a given pair.
 */
int mtd_pairing_groups(struct mtd_info *mtd)
{
	struct mtd_info *master = mtd_get_master(mtd);

	if (!master->pairing || !master->pairing->ngroups)
		return 1;

	return master->pairing->ngroups;
}
EXPORT_SYMBOL_GPL(mtd_pairing_groups);

static int mtd_nvmem_reg_read(void *priv, unsigned int offset,
			      void *val, size_t bytes)
{
	struct mtd_info *mtd = priv;
	size_t retlen;
	int err;

	err = mtd_read(mtd, offset, bytes, &retlen, val);
	if (err && err != -EUCLEAN)
		return err;

	return retlen == bytes ? 0 : -EIO;
}

static int mtd_nvmem_add(struct mtd_info *mtd)
{
	struct device_node *node = mtd_get_of_node(mtd);
	struct nvmem_config config = {};

	config.id = -1;
	config.dev = &mtd->dev;
	config.name = dev_name(&mtd->dev);
	config.owner = THIS_MODULE;
	config.reg_read = mtd_nvmem_reg_read;
	config.size = mtd->size;
	config.word_size = 1;
	config.stride = 1;
	config.read_only = true;
	config.root_only = true;
	config.ignore_wp = true;
	config.no_of_node = !of_device_is_compatible(node, "nvmem-cells");
	config.priv = mtd;

	mtd->nvmem = nvmem_register(&config);
	if (IS_ERR(mtd->nvmem)) {
		/* Just ignore if there is no NVMEM support in the kernel */
		if (PTR_ERR(mtd->nvmem) == -EOPNOTSUPP) {
			mtd->nvmem = NULL;
		} else {
			dev_err(&mtd->dev, "Failed to register NVMEM device\n");
			return PTR_ERR(mtd->nvmem);
		}
	}

	return 0;
}

static void mtd_check_of_node(struct mtd_info *mtd)
{
	struct device_node *partitions, *parent_dn, *mtd_dn = NULL;
	const char *pname, *prefix = "partition-";
	int plen, mtd_name_len, offset, prefix_len;
	struct mtd_info *parent;
	bool found = false;

	/* Check if MTD already has a device node */
	if (dev_of_node(&mtd->dev))
		return;

	/* Check if a partitions node exist */
	if (!mtd_is_partition(mtd))
		return;
	parent = mtd->parent;
	parent_dn = dev_of_node(&parent->dev);
	if (!parent_dn)
		return;

	partitions = of_get_child_by_name(parent_dn, "partitions");
	if (!partitions)
		goto exit_parent;

	prefix_len = strlen(prefix);
	mtd_name_len = strlen(mtd->name);

	/* Search if a partition is defined with the same name */
	for_each_child_of_node(partitions, mtd_dn) {
		offset = 0;

		/* Skip partition with no/wrong prefix */
		if (!of_node_name_prefix(mtd_dn, "partition-"))
			continue;

		/* Label have priority. Check that first */
		if (of_property_read_string(mtd_dn, "label", &pname)) {
			of_property_read_string(mtd_dn, "name", &pname);
			offset = prefix_len;
		}

		plen = strlen(pname) - offset;
		if (plen == mtd_name_len &&
		    !strncmp(mtd->name, pname + offset, plen)) {
			found = true;
			break;
		}
	}

	if (!found)
		goto exit_partitions;

	/* Set of_node only for nvmem */
	if (of_device_is_compatible(mtd_dn, "nvmem-cells"))
		mtd_set_of_node(mtd, mtd_dn);

exit_partitions:
	of_node_put(partitions);
exit_parent:
	of_node_put(parent_dn);
}

/**
 *	add_mtd_device - register an MTD device
 *	@mtd: pointer to new MTD device info structure
 *
 *	Add a device to the list of MTD devices present in the system, and
 *	notify each currently active MTD 'user' of its arrival. Returns
 *	zero on success or non-zero on failure.
 */

int add_mtd_device(struct mtd_info *mtd)
{
	struct device_node *np = mtd_get_of_node(mtd);
	struct mtd_info *master = mtd_get_master(mtd);
	struct mtd_notifier *not;
	int i, error, ofidx;

	/*
	 * May occur, for instance, on buggy drivers which call
	 * mtd_device_parse_register() multiple times on the same master MTD,
	 * especially with CONFIG_MTD_PARTITIONED_MASTER=y.
	 */
	if (WARN_ONCE(mtd->dev.type, "MTD already registered\n"))
		return -EEXIST;

	BUG_ON(mtd->writesize == 0);

	/*
	 * MTD drivers should implement ->_{write,read}() or
	 * ->_{write,read}_oob(), but not both.
	 */
	if (WARN_ON((mtd->_write && mtd->_write_oob) ||
		    (mtd->_read && mtd->_read_oob)))
		return -EINVAL;

	if (WARN_ON((!mtd->erasesize || !master->_erase) &&
		    !(mtd->flags & MTD_NO_ERASE)))
		return -EINVAL;

	/*
	 * MTD_SLC_ON_MLC_EMULATION can only be set on partitions, when the
	 * master is an MLC NAND and has a proper pairing scheme defined.
	 * We also reject masters that implement ->_writev() for now, because
	 * NAND controller drivers don't implement this hook, and adding the
	 * SLC -> MLC address/length conversion to this path is useless if we
	 * don't have a user.
	 */
	if (mtd->flags & MTD_SLC_ON_MLC_EMULATION &&
	    (!mtd_is_partition(mtd) || master->type != MTD_MLCNANDFLASH ||
	     !master->pairing || master->_writev))
		return -EINVAL;

	mutex_lock(&mtd_table_mutex);

	ofidx = -1;
	if (np)
		ofidx = of_alias_get_id(np, "mtd");
	if (ofidx >= 0)
		i = idr_alloc(&mtd_idr, mtd, ofidx, ofidx + 1, GFP_KERNEL);
	else
		i = idr_alloc(&mtd_idr, mtd, 0, 0, GFP_KERNEL);
	if (i < 0) {
		error = i;
		goto fail_locked;
	}

	mtd->index = i;
	mtd->usecount = 0;

	/* default value if not set by driver */
	if (mtd->bitflip_threshold == 0)
		mtd->bitflip_threshold = mtd->ecc_strength;

	if (mtd->flags & MTD_SLC_ON_MLC_EMULATION) {
		int ngroups = mtd_pairing_groups(master);

		mtd->erasesize /= ngroups;
		mtd->size = (u64)mtd_div_by_eb(mtd->size, master) *
			    mtd->erasesize;
	}

	if (is_power_of_2(mtd->erasesize))
		mtd->erasesize_shift = ffs(mtd->erasesize) - 1;
	else
		mtd->erasesize_shift = 0;

	if (is_power_of_2(mtd->writesize))
		mtd->writesize_shift = ffs(mtd->writesize) - 1;
	else
		mtd->writesize_shift = 0;

	mtd->erasesize_mask = (1 << mtd->erasesize_shift) - 1;
	mtd->writesize_mask = (1 << mtd->writesize_shift) - 1;

	/* Some chips always power up locked. Unlock them now */
	if ((mtd->flags & MTD_WRITEABLE) && (mtd->flags & MTD_POWERUP_LOCK)) {
		error = mtd_unlock(mtd, 0, mtd->size);
		if (error && error != -EOPNOTSUPP)
			printk(KERN_WARNING
			       "%s: unlock failed, writes may not work\n",
			       mtd->name);
		/* Ignore unlock failures? */
		error = 0;
	}

	/* Caller should have set dev.parent to match the
	 * physical device, if appropriate.
	 */
	mtd->dev.type = &mtd_devtype;
	mtd->dev.class = &mtd_class;
	mtd->dev.devt = MTD_DEVT(i);
	dev_set_name(&mtd->dev, "mtd%d", i);
	dev_set_drvdata(&mtd->dev, mtd);
	mtd_check_of_node(mtd);
	of_node_get(mtd_get_of_node(mtd));
	error = device_register(&mtd->dev);
	if (error)
		goto fail_added;

	/* Add the nvmem provider */
	error = mtd_nvmem_add(mtd);
	if (error)
		goto fail_nvmem_add;

	mtd_debugfs_populate(mtd);

	device_create(&mtd_class, mtd->dev.parent, MTD_DEVT(i) + 1, NULL,
		      "mtd%dro", i);

	pr_debug("mtd: Giving out device %d to %s\n", i, mtd->name);
	/* No need to get a refcount on the module containing
	   the notifier, since we hold the mtd_table_mutex */
	list_for_each_entry(not, &mtd_notifiers, list)
		not->add(mtd);

	mutex_unlock(&mtd_table_mutex);
	/* We _know_ we aren't being removed, because
	   our caller is still holding us here. So none
	   of this try_ nonsense, and no bitching about it
	   either. :) */
	__module_get(THIS_MODULE);
	return 0;

fail_nvmem_add:
	device_unregister(&mtd->dev);
fail_added:
	of_node_put(mtd_get_of_node(mtd));
	idr_remove(&mtd_idr, i);
fail_locked:
	mutex_unlock(&mtd_table_mutex);
	return error;
}

/**
 *	del_mtd_device - unregister an MTD device
 *	@mtd: pointer to MTD device info structure
 *
 *	Remove a device from the list of MTD devices present in the system,
 *	and notify each currently active MTD 'user' of its departure.
 *	Returns zero on success or 1 on failure, which currently will happen
 *	if the requested device does not appear to be present in the list.
 */

int del_mtd_device(struct mtd_info *mtd)
{
	int ret;
	struct mtd_notifier *not;

	mutex_lock(&mtd_table_mutex);

	if (idr_find(&mtd_idr, mtd->index) != mtd) {
		ret = -ENODEV;
		goto out_error;
	}

	/* No need to get a refcount on the module containing
		the notifier, since we hold the mtd_table_mutex */
	list_for_each_entry(not, &mtd_notifiers, list)
		not->remove(mtd);

	if (mtd->usecount) {
		printk(KERN_NOTICE "Removing MTD device #%d (%s) with use count %d\n",
		       mtd->index, mtd->name, mtd->usecount);
		ret = -EBUSY;
	} else {
		debugfs_remove_recursive(mtd->dbg.dfs_dir);

		/* Try to remove the NVMEM provider */
		nvmem_unregister(mtd->nvmem);

		device_unregister(&mtd->dev);

		/* Clear dev so mtd can be safely re-registered later if desired */
		memset(&mtd->dev, 0, sizeof(mtd->dev));

		idr_remove(&mtd_idr, mtd->index);
		of_node_put(mtd_get_of_node(mtd));

		module_put(THIS_MODULE);
		ret = 0;
	}

out_error:
	mutex_unlock(&mtd_table_mutex);
	return ret;
}

/*
 * Set a few defaults based on the parent devices, if not provided by the
 * driver
 */
static void mtd_set_dev_defaults(struct mtd_info *mtd)
{
	if (mtd->dev.parent) {
		if (!mtd->owner && mtd->dev.parent->driver)
			mtd->owner = mtd->dev.parent->driver->owner;
		if (!mtd->name)
			mtd->name = dev_name(mtd->dev.parent);
	} else {
		pr_debug("mtd device won't show a device symlink in sysfs\n");
	}

	INIT_LIST_HEAD(&mtd->partitions);
	mutex_init(&mtd->master.partitions_lock);
	mutex_init(&mtd->master.chrdev_lock);
}

static ssize_t mtd_otp_size(struct mtd_info *mtd, bool is_user)
{
	struct otp_info *info;
	ssize_t size = 0;
	unsigned int i;
	size_t retlen;
	int ret;

	info = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	if (is_user)
		ret = mtd_get_user_prot_info(mtd, PAGE_SIZE, &retlen, info);
	else
		ret = mtd_get_fact_prot_info(mtd, PAGE_SIZE, &retlen, info);
	if (ret)
		goto err;

	for (i = 0; i < retlen / sizeof(*info); i++)
		size += info[i].length;

	kfree(info);
	return size;

err:
	kfree(info);

	/* ENODATA means there is no OTP region. */
	return ret == -ENODATA ? 0 : ret;
}

static struct nvmem_device *mtd_otp_nvmem_register(struct mtd_info *mtd,
						   const char *compatible,
						   int size,
						   nvmem_reg_read_t reg_read)
{
	struct nvmem_device *nvmem = NULL;
	struct nvmem_config config = {};
	struct device_node *np;

	/* DT binding is optional */
	np = of_get_compatible_child(mtd->dev.of_node, compatible);

	/* OTP nvmem will be registered on the physical device */
	config.dev = mtd->dev.parent;
	config.name = kasprintf(GFP_KERNEL, "%s-%s", dev_name(&mtd->dev), compatible);
	config.id = NVMEM_DEVID_NONE;
	config.owner = THIS_MODULE;
	config.type = NVMEM_TYPE_OTP;
	config.root_only = true;
	config.ignore_wp = true;
	config.reg_read = reg_read;
	config.size = size;
	config.of_node = np;
	config.priv = mtd;

	nvmem = nvmem_register(&config);
	/* Just ignore if there is no NVMEM support in the kernel */
	if (IS_ERR(nvmem) && PTR_ERR(nvmem) == -EOPNOTSUPP)
		nvmem = NULL;

	of_node_put(np);
	kfree(config.name);

	return nvmem;
}

static int mtd_nvmem_user_otp_reg_read(void *priv, unsigned int offset,
				       void *val, size_t bytes)
{
	struct mtd_info *mtd = priv;
	size_t retlen;
	int ret;

	ret = mtd_read_user_prot_reg(mtd, offset, bytes, &retlen, val);
	if (ret)
		return ret;

	return retlen == bytes ? 0 : -EIO;
}

static int mtd_nvmem_fact_otp_reg_read(void *priv, unsigned int offset,
				       void *val, size_t bytes)
{
	struct mtd_info *mtd = priv;
	size_t retlen;
	int ret;

	ret = mtd_read_fact_prot_reg(mtd, offset, bytes, &retlen, val);
	if (ret)
		return ret;

	return retlen == bytes ? 0 : -EIO;
}

static int mtd_otp_nvmem_add(struct mtd_info *mtd)
{
	struct nvmem_device *nvmem;
	ssize_t size;
	int err;

	if (mtd->_get_user_prot_info && mtd->_read_user_prot_reg) {
		size = mtd_otp_size(mtd, true);
		if (size < 0)
			return size;

		if (size > 0) {
			nvmem = mtd_otp_nvmem_register(mtd, "user-otp", size,
						       mtd_nvmem_user_otp_reg_read);
			if (IS_ERR(nvmem)) {
				dev_err(&mtd->dev, "Failed to register OTP NVMEM device\n");
				return PTR_ERR(nvmem);
			}
			mtd->otp_user_nvmem = nvmem;
		}
	}

	if (mtd->_get_fact_prot_info && mtd->_read_fact_prot_reg) {
		size = mtd_otp_size(mtd, false);
		if (size < 0) {
			err = size;
			goto err;
		}

		if (size > 0) {
			nvmem = mtd_otp_nvmem_register(mtd, "factory-otp", size,
						       mtd_nvmem_fact_otp_reg_read);
			if (IS_ERR(nvmem)) {
				dev_err(&mtd->dev, "Failed to register OTP NVMEM device\n");
				err = PTR_ERR(nvmem);
				goto err;
			}
			mtd->otp_factory_nvmem = nvmem;
		}
	}

	return 0;

err:
	nvmem_unregister(mtd->otp_user_nvmem);
	return err;
}

/**
 * mtd_device_parse_register - parse partitions and register an MTD device.
 *
 * @mtd: the MTD device to register
 * @types: the list of MTD partition probes to try, see
 *         'parse_mtd_partitions()' for more information
 * @parser_data: MTD partition parser-specific data
 * @parts: fallback partition information to register, if parsing fails;
 *         only valid if %nr_parts > %0
 * @nr_parts: the number of partitions in parts, if zero then the full
 *            MTD device is registered if no partition info is found
 *
 * This function aggregates MTD partitions parsing (done by
 * 'parse_mtd_partitions()') and MTD device and partitions registering. It
 * basically follows the most common pattern found in many MTD drivers:
 *
 * * If the MTD_PARTITIONED_MASTER option is set, then the device as a whole is
 *   registered first.
 * * Then It tries to probe partitions on MTD device @mtd using parsers
 *   specified in @types (if @types is %NULL, then the default list of parsers
 *   is used, see 'parse_mtd_partitions()' for more information). If none are
 *   found this functions tries to fallback to information specified in
 *   @parts/@nr_parts.
 * * If no partitions were found this function just registers the MTD device
 *   @mtd and exits.
 *
 * Returns zero in case of success and a negative error code in case of failure.
 */
int mtd_device_parse_register(struct mtd_info *mtd, const char * const *types,
			      struct mtd_part_parser_data *parser_data,
			      const struct mtd_partition *parts,
			      int nr_parts)
{
	int ret;

	mtd_set_dev_defaults(mtd);

	if (IS_ENABLED(CONFIG_MTD_PARTITIONED_MASTER)) {
		ret = add_mtd_device(mtd);
		if (ret)
			return ret;
	}

	/* Prefer parsed partitions over driver-provided fallback */
	ret = parse_mtd_partitions(mtd, types, parser_data);
	if (ret == -EPROBE_DEFER)
		goto out;

	if (ret > 0)
		ret = 0;
	else if (nr_parts)
		ret = add_mtd_partitions(mtd, parts, nr_parts);
	else if (!device_is_registered(&mtd->dev))
		ret = add_mtd_device(mtd);
	else
		ret = 0;

	if (ret)
		goto out;

	/*
	 * FIXME: some drivers unfortunately call this function more than once.
	 * So we have to check if we've already assigned the reboot notifier.
	 *
	 * Generally, we can make multiple calls work for most cases, but it
	 * does cause problems with parse_mtd_partitions() above (e.g.,
	 * cmdlineparts will register partitions more than once).
	 */
	WARN_ONCE(mtd->_reboot && mtd->reboot_notifier.notifier_call,
		  "MTD already registered\n");
	if (mtd->_reboot && !mtd->reboot_notifier.notifier_call) {
		mtd->reboot_notifier.notifier_call = mtd_reboot_notifier;
		register_reboot_notifier(&mtd->reboot_notifier);
	}

	ret = mtd_otp_nvmem_add(mtd);

out:
	if (ret && device_is_registered(&mtd->dev))
		del_mtd_device(mtd);

	return ret;
}
EXPORT_SYMBOL_GPL(mtd_device_parse_register);

/**
 * mtd_device_unregister - unregister an existing MTD device.
 *
 * @master: the MTD device to unregister.  This will unregister both the master
 *          and any partitions if registered.
 */
int mtd_device_unregister(struct mtd_info *master)
{
	int err;

	if (master->_reboot) {
		unregister_reboot_notifier(&master->reboot_notifier);
		memset(&master->reboot_notifier, 0, sizeof(master->reboot_notifier));
	}

	nvmem_unregister(master->otp_user_nvmem);
	nvmem_unregister(master->otp_factory_nvmem);

	err = del_mtd_partitions(master);
	if (err)
		return err;

	if (!device_is_registered(&master->dev))
		return 0;

	return del_mtd_device(master);
}
EXPORT_SYMBOL_GPL(mtd_device_unregister);

/**
 *	register_mtd_user - register a 'user' of MTD devices.
 *	@new: pointer to notifier info structure
 *
 *	Registers a pair of callbacks function to be called upon addition
 *	or removal of MTD devices. Causes the 'add' callback to be immediately
 *	invoked for each MTD device currently present in the system.
 */
void register_mtd_user (struct mtd_notifier *new)
{
	struct mtd_info *mtd;

	mutex_lock(&mtd_table_mutex);

	list_add(&new->list, &mtd_notifiers);

	__module_get(THIS_MODULE);

	mtd_for_each_device(mtd)
		new->add(mtd);

	mutex_unlock(&mtd_table_mutex);
}
EXPORT_SYMBOL_GPL(register_mtd_user);

/**
 *	unregister_mtd_user - unregister a 'user' of MTD devices.
 *	@old: pointer to notifier info structure
 *
 *	Removes a callback function pair from the list of 'users' to be
 *	notified upon addition or removal of MTD devices. Causes the
 *	'remove' callback to be immediately invoked for each MTD device
 *	currently present in the system.
 */
int unregister_mtd_user (struct mtd_notifier *old)
{
	struct mtd_info *mtd;

	mutex_lock(&mtd_table_mutex);

	module_put(THIS_MODULE);

	mtd_for_each_device(mtd)
		old->remove(mtd);

	list_del(&old->list);
	mutex_unlock(&mtd_table_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(unregister_mtd_user);

/**
 *	get_mtd_device - obtain a validated handle for an MTD device
 *	@mtd: last known address of the required MTD device
 *	@num: internal device number of the required MTD device
 *
 *	Given a number and NULL address, return the num'th entry in the device
 *	table, if any.	Given an address and num == -1, search the device table
 *	for a device with that address and return if it's still present. Given
 *	both, return the num'th driver only if its address matches. Return
 *	error code if not.
 */
struct mtd_info *get_mtd_device(struct mtd_info *mtd, int num)
{
	struct mtd_info *ret = NULL, *other;
	int err = -ENODEV;

	mutex_lock(&mtd_table_mutex);

	if (num == -1) {
		mtd_for_each_device(other) {
			if (other == mtd) {
				ret = mtd;
				break;
			}
		}
	} else if (num >= 0) {
		ret = idr_find(&mtd_idr, num);
		if (mtd && mtd != ret)
			ret = NULL;
	}

	if (!ret) {
		ret = ERR_PTR(err);
		goto out;
	}

	err = __get_mtd_device(ret);
	if (err)
		ret = ERR_PTR(err);
out:
	mutex_unlock(&mtd_table_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(get_mtd_device);


int __get_mtd_device(struct mtd_info *mtd)
{
	struct mtd_info *master = mtd_get_master(mtd);
	int err;

	if (!try_module_get(master->owner))
		return -ENODEV;

	if (master->_get_device) {
		err = master->_get_device(mtd);

		if (err) {
			module_put(master->owner);
			return err;
		}
	}

	master->usecount++;

	while (mtd->parent) {
		mtd->usecount++;
		mtd = mtd->parent;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(__get_mtd_device);

/**
 *	get_mtd_device_nm - obtain a validated handle for an MTD device by
 *	device name
 *	@name: MTD device name to open
 *
 * 	This function returns MTD device description structure in case of
 * 	success and an error code in case of failure.
 */
struct mtd_info *get_mtd_device_nm(const char *name)
{
	int err = -ENODEV;
	struct mtd_info *mtd = NULL, *other;

	mutex_lock(&mtd_table_mutex);

	mtd_for_each_device(other) {
		if (!strcmp(name, other->name)) {
			mtd = other;
			break;
		}
	}

	if (!mtd)
		goto out_unlock;

	err = __get_mtd_device(mtd);
	if (err)
		goto out_unlock;

	mutex_unlock(&mtd_table_mutex);
	return mtd;

out_unlock:
	mutex_unlock(&mtd_table_mutex);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(get_mtd_device_nm);

void put_mtd_device(struct mtd_info *mtd)
{
	mutex_lock(&mtd_table_mutex);
	__put_mtd_device(mtd);
	mutex_unlock(&mtd_table_mutex);

}
EXPORT_SYMBOL_GPL(put_mtd_device);

void __put_mtd_device(struct mtd_info *mtd)
{
	struct mtd_info *master = mtd_get_master(mtd);

	while (mtd->parent) {
		--mtd->usecount;
		BUG_ON(mtd->usecount < 0);
		mtd = mtd->parent;
	}

	master->usecount--;

	if (master->_put_device)
		master->_put_device(master);

	module_put(master->owner);
}
EXPORT_SYMBOL_GPL(__put_mtd_device);

/*
 * Erase is an synchronous operation. Device drivers are epected to return a
 * negative error code if the operation failed and update instr->fail_addr
 * to point the portion that was not properly erased.
 */
int mtd_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct mtd_info *master = mtd_get_master(mtd);
	u64 mst_ofs = mtd_get_master_ofs(mtd, 0);
	struct erase_info adjinstr;
	int ret;

	instr->fail_addr = MTD_FAIL_ADDR_UNKNOWN;
	adjinstr = *instr;

	if (!mtd->erasesize || !master->_erase)
		return -ENOTSUPP;

	if (instr->addr >= mtd->size || instr->len > mtd->size - instr->addr)
		return -EINVAL;
	if (!(mtd->flags & MTD_WRITEABLE))
		return -EROFS;

	if (!instr->len)
		return 0;

	ledtrig_mtd_activity();

	if (mtd->flags & MTD_SLC_ON_MLC_EMULATION) {
		adjinstr.addr = (loff_t)mtd_div_by_eb(instr->addr, mtd) *
				master->erasesize;
		adjinstr.len = ((u64)mtd_div_by_eb(instr->addr + instr->len, mtd) *
				master->erasesize) -
			       adjinstr.addr;
	}

	adjinstr.addr += mst_ofs;

	ret = master->_erase(master, &adjinstr);

	if (adjinstr.fail_addr != MTD_FAIL_ADDR_UNKNOWN) {
		instr->fail_addr = adjinstr.fail_addr - mst_ofs;
		if (mtd->flags & MTD_SLC_ON_MLC_EMULATION) {
			instr->fail_addr = mtd_div_by_eb(instr->fail_addr,
							 master);
			instr->fail_addr *= mtd->erasesize;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(mtd_erase);

/*
 * This stuff for eXecute-In-Place. phys is optional and may be set to NULL.
 */
int mtd_point(struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen,
	      void **virt, resource_size_t *phys)
{
	struct mtd_info *master = mtd_get_master(mtd);

	*retlen = 0;
	*virt = NULL;
	if (phys)
		*phys = 0;
	if (!master->_point)
		return -EOPNOTSUPP;
	if (from < 0 || from >= mtd->size || len > mtd->size - from)
		return -EINVAL;
	if (!len)
		return 0;

	from = mtd_get_master_ofs(mtd, from);
	return master->_point(master, from, len, retlen, virt, phys);
}
EXPORT_SYMBOL_GPL(mtd_point);

/* We probably shouldn't allow XIP if the unpoint isn't a NULL */
int mtd_unpoint(struct mtd_info *mtd, loff_t from, size_t len)
{
	struct mtd_info *master = mtd_get_master(mtd);

	if (!master->_unpoint)
		return -EOPNOTSUPP;
	if (from < 0 || from >= mtd->size || len > mtd->size - from)
		return -EINVAL;
	if (!len)
		return 0;
	return master->_unpoint(master, mtd_get_master_ofs(mtd, from), len);
}
EXPORT_SYMBOL_GPL(mtd_unpoint);

/*
 * Allow NOMMU mmap() to directly map the device (if not NULL)
 * - return the address to which the offset maps
 * - return -ENOSYS to indicate refusal to do the mapping
 */
unsigned long mtd_get_unmapped_area(struct mtd_info *mtd, unsigned long len,
				    unsigned long offset, unsigned long flags)
{
	size_t retlen;
	void *virt;
	int ret;

	ret = mtd_point(mtd, offset, len, &retlen, &virt, NULL);
	if (ret)
		return ret;
	if (retlen != len) {
		mtd_unpoint(mtd, offset, retlen);
		return -ENOSYS;
	}
	return (unsigned long)virt;
}
EXPORT_SYMBOL_GPL(mtd_get_unmapped_area);

static void mtd_update_ecc_stats(struct mtd_info *mtd, struct mtd_info *master,
				 const struct mtd_ecc_stats *old_stats)
{
	struct mtd_ecc_stats diff;

	if (master == mtd)
		return;

	diff = master->ecc_stats;
	diff.failed -= old_stats->failed;
	diff.corrected -= old_stats->corrected;

	while (mtd->parent) {
		mtd->ecc_stats.failed += diff.failed;
		mtd->ecc_stats.corrected += diff.corrected;
		mtd = mtd->parent;
	}
}

int mtd_read(struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen,
	     u_char *buf)
{
	struct mtd_oob_ops ops = {
		.len = len,
		.datbuf = buf,
	};
	int ret;

	ret = mtd_read_oob(mtd, from, &ops);
	*retlen = ops.retlen;

	return ret;
}
EXPORT_SYMBOL_GPL(mtd_read);

int mtd_write(struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen,
	      const u_char *buf)
{
	struct mtd_oob_ops ops = {
		.len = len,
		.datbuf = (u8 *)buf,
	};
	int ret;

	ret = mtd_write_oob(mtd, to, &ops);
	*retlen = ops.retlen;

	return ret;
}
EXPORT_SYMBOL_GPL(mtd_write);

/*
 * In blackbox flight recorder like scenarios we want to make successful writes
 * in interrupt context. panic_write() is only intended to be called when its
 * known the kernel is about to panic and we need the write to succeed. Since
 * the kernel is not going to be running for much longer, this function can
 * break locks and delay to ensure the write succeeds (but not sleep).
 */
int mtd_panic_write(struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen,
		    const u_char *buf)
{
	struct mtd_info *master = mtd_get_master(mtd);

	*retlen = 0;
	if (!master->_panic_write)
		return -EOPNOTSUPP;
	if (to < 0 || to >= mtd->size || len > mtd->size - to)
		return -EINVAL;
	if (!(mtd->flags & MTD_WRITEABLE))
		return -EROFS;
	if (!len)
		return 0;
	if (!master->oops_panic_write)
		master->oops_panic_write = true;

	return master->_panic_write(master, mtd_get_master_ofs(mtd, to), len,
				    retlen, buf);
}
EXPORT_SYMBOL_GPL(mtd_panic_write);

static int mtd_check_oob_ops(struct mtd_info *mtd, loff_t offs,
			     struct mtd_oob_ops *ops)
{
	/*
	 * Some users are setting ->datbuf or ->oobbuf to NULL, but are leaving
	 * ->len or ->ooblen uninitialized. Force ->len and ->ooblen to 0 in
	 *  this case.
	 */
	if (!ops->datbuf)
		ops->len = 0;

	if (!ops->oobbuf)
		ops->ooblen = 0;

	if (offs < 0 || offs + ops->len > mtd->size)
		return -EINVAL;

	if (ops->ooblen) {
		size_t maxooblen;

		if (ops->ooboffs >= mtd_oobavail(mtd, ops))
			return -EINVAL;

		maxooblen = ((size_t)(mtd_div_by_ws(mtd->size, mtd) -
				      mtd_div_by_ws(offs, mtd)) *
			     mtd_oobavail(mtd, ops)) - ops->ooboffs;
		if (ops->ooblen > maxooblen)
			return -EINVAL;
	}

	return 0;
}

static int mtd_read_oob_std(struct mtd_info *mtd, loff_t from,
			    struct mtd_oob_ops *ops)
{
	struct mtd_info *master = mtd_get_master(mtd);
	int ret;

	from = mtd_get_master_ofs(mtd, from);
	if (master->_read_oob)
		ret = master->_read_oob(master, from, ops);
	else
		ret = master->_read(master, from, ops->len, &ops->retlen,
				    ops->datbuf);

	return ret;
}

static int mtd_write_oob_std(struct mtd_info *mtd, loff_t to,
			     struct mtd_oob_ops *ops)
{
	struct mtd_info *master = mtd_get_master(mtd);
	int ret;

	to = mtd_get_master_ofs(mtd, to);
	if (master->_write_oob)
		ret = master->_write_oob(master, to, ops);
	else
		ret = master->_write(master, to, ops->len, &ops->retlen,
				     ops->datbuf);

	return ret;
}

static int mtd_io_emulated_slc(struct mtd_info *mtd, loff_t start, bool read,
			       struct mtd_oob_ops *ops)
{
	struct mtd_info *master = mtd_get_master(mtd);
	int ngroups = mtd_pairing_groups(master);
	int npairs = mtd_wunit_per_eb(master) / ngroups;
	struct mtd_oob_ops adjops = *ops;
	unsigned int wunit, oobavail;
	struct mtd_pairing_info info;
	int max_bitflips = 0;
	u32 ebofs, pageofs;
	loff_t base, pos;

	ebofs = mtd_mod_by_eb(start, mtd);
	base = (loff_t)mtd_div_by_eb(start, mtd) * master->erasesize;
	info.group = 0;
	info.pair = mtd_div_by_ws(ebofs, mtd);
	pageofs = mtd_mod_by_ws(ebofs, mtd);
	oobavail = mtd_oobavail(mtd, ops);

	while (ops->retlen < ops->len || ops->oobretlen < ops->ooblen) {
		int ret;

		if (info.pair >= npairs) {
			info.pair = 0;
			base += master->erasesize;
		}

		wunit = mtd_pairing_info_to_wunit(master, &info);
		pos = mtd_wunit_to_offset(mtd, base, wunit);

		adjops.len = ops->len - ops->retlen;
		if (adjops.len > mtd->writesize - pageofs)
			adjops.len = mtd->writesize - pageofs;

		adjops.ooblen = ops->ooblen - ops->oobretlen;
		if (adjops.ooblen > oobavail - adjops.ooboffs)
			adjops.ooblen = oobavail - adjops.ooboffs;

		if (read) {
			ret = mtd_read_oob_std(mtd, pos + pageofs, &adjops);
			if (ret > 0)
				max_bitflips = max(max_bitflips, ret);
		} else {
			ret = mtd_write_oob_std(mtd, pos + pageofs, &adjops);
		}

		if (ret < 0)
			return ret;

		max_bitflips = max(max_bitflips, ret);
		ops->retlen += adjops.retlen;
		ops->oobretlen += adjops.oobretlen;
		adjops.datbuf += adjops.retlen;
		adjops.oobbuf += adjops.oobretlen;
		adjops.ooboffs = 0;
		pageofs = 0;
		info.pair++;
	}

	return max_bitflips;
}

int mtd_read_oob(struct mtd_info *mtd, loff_t from, struct mtd_oob_ops *ops)
{
	struct mtd_info *master = mtd_get_master(mtd);
	struct mtd_ecc_stats old_stats = master->ecc_stats;
	int ret_code;

	ops->retlen = ops->oobretlen = 0;

	ret_code = mtd_check_oob_ops(mtd, from, ops);
	if (ret_code)
		return ret_code;

	ledtrig_mtd_activity();

	/* Check the validity of a potential fallback on mtd->_read */
	if (!master->_read_oob && (!master->_read || ops->oobbuf))
		return -EOPNOTSUPP;

	if (ops->stats)
		memset(ops->stats, 0, sizeof(*ops->stats));

	if (mtd->flags & MTD_SLC_ON_MLC_EMULATION)
		ret_code = mtd_io_emulated_slc(mtd, from, true, ops);
	else
		ret_code = mtd_read_oob_std(mtd, from, ops);

	mtd_update_ecc_stats(mtd, master, &old_stats);

	/*
	 * In cases where ops->datbuf != NULL, mtd->_read_oob() has semantics
	 * similar to mtd->_read(), returning a non-negative integer
	 * representing max bitflips. In other cases, mtd->_read_oob() may
	 * return -EUCLEAN. In all cases, perform similar logic to mtd_read().
	 */
	if (unlikely(ret_code < 0))
		return ret_code;
	if (mtd->ecc_strength == 0)
		return 0;	/* device lacks ecc */
	if (ops->stats)
		ops->stats->max_bitflips = ret_code;
	return ret_code >= mtd->bitflip_threshold ? -EUCLEAN : 0;
}
EXPORT_SYMBOL_GPL(mtd_read_oob);

int mtd_write_oob(struct mtd_info *mtd, loff_t to,
				struct mtd_oob_ops *ops)
{
	struct mtd_info *master = mtd_get_master(mtd);
	int ret;

	ops->retlen = ops->oobretlen = 0;

	if (!(mtd->flags & MTD_WRITEABLE))
		return -EROFS;

	ret = mtd_check_oob_ops(mtd, to, ops);
	if (ret)
		return ret;

	ledtrig_mtd_activity();

	/* Check the validity of a potential fallback on mtd->_write */
	if (!master->_write_oob && (!master->_write || ops->oobbuf))
		return -EOPNOTSUPP;

	if (mtd->flags & MTD_SLC_ON_MLC_EMULATION)
		return mtd_io_emulated_slc(mtd, to, false, ops);

	return mtd_write_oob_std(mtd, to, ops);
}
EXPORT_SYMBOL_GPL(mtd_write_oob);

/**
 * mtd_ooblayout_ecc - Get the OOB region definition of a specific ECC section
 * @mtd: MTD device structure
 * @section: ECC section. Depending on the layout you may have all the ECC
 *	     bytes stored in a single contiguous section, or one section
 *	     per ECC chunk (and sometime several sections for a single ECC
 *	     ECC chunk)
 * @oobecc: OOB region struct filled with the appropriate ECC position
 *	    information
 *
 * This function returns ECC section information in the OOB area. If you want
 * to get all the ECC bytes information, then you should call
 * mtd_ooblayout_ecc(mtd, section++, oobecc) until it returns -ERANGE.
 *
 * Returns zero on success, a negative error code otherwise.
 */
int mtd_ooblayout_ecc(struct mtd_info *mtd, int section,
		      struct mtd_oob_region *oobecc)
{
	struct mtd_info *master = mtd_get_master(mtd);

	memset(oobecc, 0, sizeof(*oobecc));

	if (!master || section < 0)
		return -EINVAL;

	if (!master->ooblayout || !master->ooblayout->ecc)
		return -ENOTSUPP;

	return master->ooblayout->ecc(master, section, oobecc);
}
EXPORT_SYMBOL_GPL(mtd_ooblayout_ecc);

/**
 * mtd_ooblayout_free - Get the OOB region definition of a specific free
 *			section
 * @mtd: MTD device structure
 * @section: Free section you are interested in. Depending on the layout
 *	     you may have all the free bytes stored in a single contiguous
 *	     section, or one section per ECC chunk plus an extra section
 *	     for the remaining bytes (or other funky layout).
 * @oobfree: OOB region struct filled with the appropriate free position
 *	     information
 *
 * This function returns free bytes position in the OOB area. If you want
 * to get all the free bytes information, then you should call
 * mtd_ooblayout_free(mtd, section++, oobfree) until it returns -ERANGE.
 *
 * Returns zero on success, a negative error code otherwise.
 */
int mtd_ooblayout_free(struct mtd_info *mtd, int section,
		       struct mtd_oob_region *oobfree)
{
	struct mtd_info *master = mtd_get_master(mtd);

	memset(oobfree, 0, sizeof(*oobfree));

	if (!master || section < 0)
		return -EINVAL;

	if (!master->ooblayout || !master->ooblayout->free)
		return -ENOTSUPP;

	return master->ooblayout->free(master, section, oobfree);
}
EXPORT_SYMBOL_GPL(mtd_ooblayout_free);

/**
 * mtd_ooblayout_find_region - Find the region attached to a specific byte
 * @mtd: mtd info structure
 * @byte: the byte we are searching for
 * @sectionp: pointer where the section id will be stored
 * @oobregion: used to retrieve the ECC position
 * @iter: iterator function. Should be either mtd_ooblayout_free or
 *	  mtd_ooblayout_ecc depending on the region type you're searching for
 *
 * This function returns the section id and oobregion information of a
 * specific byte. For example, say you want to know where the 4th ECC byte is
 * stored, you'll use:
 *
 * mtd_ooblayout_find_region(mtd, 3, &section, &oobregion, mtd_ooblayout_ecc);
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int mtd_ooblayout_find_region(struct mtd_info *mtd, int byte,
				int *sectionp, struct mtd_oob_region *oobregion,
				int (*iter)(struct mtd_info *,
					    int section,
					    struct mtd_oob_region *oobregion))
{
	int pos = 0, ret, section = 0;

	memset(oobregion, 0, sizeof(*oobregion));

	while (1) {
		ret = iter(mtd, section, oobregion);
		if (ret)
			return ret;

		if (pos + oobregion->length > byte)
			break;

		pos += oobregion->length;
		section++;
	}

	/*
	 * Adjust region info to make it start at the beginning at the
	 * 'start' ECC byte.
	 */
	oobregion->offset += byte - pos;
	oobregion->length -= byte - pos;
	*sectionp = section;

	return 0;
}

/**
 * mtd_ooblayout_find_eccregion - Find the ECC region attached to a specific
 *				  ECC byte
 * @mtd: mtd info structure
 * @eccbyte: the byte we are searching for
 * @section: pointer where the section id will be stored
 * @oobregion: OOB region information
 *
 * Works like mtd_ooblayout_find_region() except it searches for a specific ECC
 * byte.
 *
 * Returns zero on success, a negative error code otherwise.
 */
int mtd_ooblayout_find_eccregion(struct mtd_info *mtd, int eccbyte,
				 int *section,
				 struct mtd_oob_region *oobregion)
{
	return mtd_ooblayout_find_region(mtd, eccbyte, section, oobregion,
					 mtd_ooblayout_ecc);
}
EXPORT_SYMBOL_GPL(mtd_ooblayout_find_eccregion);

/**
 * mtd_ooblayout_get_bytes - Extract OOB bytes from the oob buffer
 * @mtd: mtd info structure
 * @buf: destination buffer to store OOB bytes
 * @oobbuf: OOB buffer
 * @start: first byte to retrieve
 * @nbytes: number of bytes to retrieve
 * @iter: section iterator
 *
 * Extract bytes attached to a specific category (ECC or free)
 * from the OOB buffer and copy them into buf.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int mtd_ooblayout_get_bytes(struct mtd_info *mtd, u8 *buf,
				const u8 *oobbuf, int start, int nbytes,
				int (*iter)(struct mtd_info *,
					    int section,
					    struct mtd_oob_region *oobregion))
{
	struct mtd_oob_region oobregion;
	int section, ret;

	ret = mtd_ooblayout_find_region(mtd, start, &section,
					&oobregion, iter);

	while (!ret) {
		int cnt;

		cnt = min_t(int, nbytes, oobregion.length);
		memcpy(buf, oobbuf + oobregion.offset, cnt);
		buf += cnt;
		nbytes -= cnt;

		if (!nbytes)
			break;

		ret = iter(mtd, ++section, &oobregion);
	}

	return ret;
}

/**
 * mtd_ooblayout_set_bytes - put OOB bytes into the oob buffer
 * @mtd: mtd info structure
 * @buf: source buffer to get OOB bytes from
 * @oobbuf: OOB buffer
 * @start: first OOB byte to set
 * @nbytes: number of OOB bytes to set
 * @iter: section iterator
 *
 * Fill the OOB buffer with data provided in buf. The category (ECC or free)
 * is selected by passing the appropriate iterator.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int mtd_ooblayout_set_bytes(struct mtd_info *mtd, const u8 *buf,
				u8 *oobbuf, int start, int nbytes,
				int (*iter)(struct mtd_info *,
					    int section,
					    struct mtd_oob_region *oobregion))
{
	struct mtd_oob_region oobregion;
	int section, ret;

	ret = mtd_ooblayout_find_region(mtd, start, &section,
					&oobregion, iter);

	while (!ret) {
		int cnt;

		cnt = min_t(int, nbytes, oobregion.length);
		memcpy(oobbuf + oobregion.offset, buf, cnt);
		buf += cnt;
		nbytes -= cnt;

		if (!nbytes)
			break;

		ret = iter(mtd, ++section, &oobregion);
	}

	return ret;
}

/**
 * mtd_ooblayout_count_bytes - count the number of bytes in a OOB category
 * @mtd: mtd info structure
 * @iter: category iterator
 *
 * Count the number of bytes in a given category.
 *
 * Returns a positive value on success, a negative error code otherwise.
 */
static int mtd_ooblayout_count_bytes(struct mtd_info *mtd,
				int (*iter)(struct mtd_info *,
					    int section,
					    struct mtd_oob_region *oobregion))
{
	struct mtd_oob_region oobregion;
	int section = 0, ret, nbytes = 0;

	while (1) {
		ret = iter(mtd, section++, &oobregion);
		if (ret) {
			if (ret == -ERANGE)
				ret = nbytes;
			break;
		}

		nbytes += oobregion.length;
	}

	return ret;
}

/**
 * mtd_ooblayout_get_eccbytes - extract ECC bytes from the oob buffer
 * @mtd: mtd info structure
 * @eccbuf: destination buffer to store ECC bytes
 * @oobbuf: OOB buffer
 * @start: first ECC byte to retrieve
 * @nbytes: number of ECC bytes to retrieve
 *
 * Works like mtd_ooblayout_get_bytes(), except it acts on ECC bytes.
 *
 * Returns zero on success, a negative error code otherwise.
 */
int mtd_ooblayout_get_eccbytes(struct mtd_info *mtd, u8 *eccbuf,
			       const u8 *oobbuf, int start, int nbytes)
{
	return mtd_ooblayout_get_bytes(mtd, eccbuf, oobbuf, start, nbytes,
				       mtd_ooblayout_ecc);
}
EXPORT_SYMBOL_GPL(mtd_ooblayout_get_eccbytes);

/**
 * mtd_ooblayout_set_eccbytes - set ECC bytes into the oob buffer
 * @mtd: mtd info structure
 * @eccbuf: source buffer to get ECC bytes from
 * @oobbuf: OOB buffer
 * @start: first ECC byte to set
 * @nbytes: number of ECC bytes to set
 *
 * Works like mtd_ooblayout_set_bytes(), except it acts on ECC bytes.
 *
 * Returns zero on success, a negative error code otherwise.
 */
int mtd_ooblayout_set_eccbytes(struct mtd_info *mtd, const u8 *eccbuf,
			       u8 *oobbuf, int start, int nbytes)
{
	return mtd_ooblayout_set_bytes(mtd, eccbuf, oobbuf, start, nbytes,
				       mtd_ooblayout_ecc);
}
EXPORT_SYMBOL_GPL(mtd_ooblayout_set_eccbytes);

/**
 * mtd_ooblayout_get_databytes - extract data bytes from the oob buffer
 * @mtd: mtd info structure
 * @databuf: destination buffer to store ECC bytes
 * @oobbuf: OOB buffer
 * @start: first ECC byte to retrieve
 * @nbytes: number of ECC bytes to retrieve
 *
 * Works like mtd_ooblayout_get_bytes(), except it acts on free bytes.
 *
 * Returns zero on success, a negative error code otherwise.
 */
int mtd_ooblayout_get_databytes(struct mtd_info *mtd, u8 *databuf,
				const u8 *oobbuf, int start, int nbytes)
{
	return mtd_ooblayout_get_bytes(mtd, databuf, oobbuf, start, nbytes,
				       mtd_ooblayout_free);
}
EXPORT_SYMBOL_GPL(mtd_ooblayout_get_databytes);

/**
 * mtd_ooblayout_set_databytes - set data bytes into the oob buffer
 * @mtd: mtd info structure
 * @databuf: source buffer to get data bytes from
 * @oobbuf: OOB buffer
 * @start: first ECC byte to set
 * @nbytes: number of ECC bytes to set
 *
 * Works like mtd_ooblayout_set_bytes(), except it acts on free bytes.
 *
 * Returns zero on success, a negative error code otherwise.
 */
int mtd_ooblayout_set_databytes(struct mtd_info *mtd, const u8 *databuf,
				u8 *oobbuf, int start, int nbytes)
{
	return mtd_ooblayout_set_bytes(mtd, databuf, oobbuf, start, nbytes,
				       mtd_ooblayout_free);
}
EXPORT_SYMBOL_GPL(mtd_ooblayout_set_databytes);

/**
 * mtd_ooblayout_count_freebytes - count the number of free bytes in OOB
 * @mtd: mtd info structure
 *
 * Works like mtd_ooblayout_count_bytes(), except it count free bytes.
 *
 * Returns zero on success, a negative error code otherwise.
 */
int mtd_ooblayout_count_freebytes(struct mtd_info *mtd)
{
	return mtd_ooblayout_count_bytes(mtd, mtd_ooblayout_free);
}
EXPORT_SYMBOL_GPL(mtd_ooblayout_count_freebytes);

/**
 * mtd_ooblayout_count_eccbytes - count the number of ECC bytes in OOB
 * @mtd: mtd info structure
 *
 * Works like mtd_ooblayout_count_bytes(), except it count ECC bytes.
 *
 * Returns zero on success, a negative error code otherwise.
 */
int mtd_ooblayout_count_eccbytes(struct mtd_info *mtd)
{
	return mtd_ooblayout_count_bytes(mtd, mtd_ooblayout_ecc);
}
EXPORT_SYMBOL_GPL(mtd_ooblayout_count_eccbytes);

/*
 * Method to access the protection register area, present in some flash
 * devices. The user data is one time programmable but the factory data is read
 * only.
 */
int mtd_get_fact_prot_info(struct mtd_info *mtd, size_t len, size_t *retlen,
			   struct otp_info *buf)
{
	struct mtd_info *master = mtd_get_master(mtd);

	if (!master->_get_fact_prot_info)
		return -EOPNOTSUPP;
	if (!len)
		return 0;
	return master->_get_fact_prot_info(master, len, retlen, buf);
}
EXPORT_SYMBOL_GPL(mtd_get_fact_prot_info);

int mtd_read_fact_prot_reg(struct mtd_info *mtd, loff_t from, size_t len,
			   size_t *retlen, u_char *buf)
{
	struct mtd_info *master = mtd_get_master(mtd);

	*retlen = 0;
	if (!master->_read_fact_prot_reg)
		return -EOPNOTSUPP;
	if (!len)
		return 0;
	return master->_read_fact_prot_reg(master, from, len, retlen, buf);
}
EXPORT_SYMBOL_GPL(mtd_read_fact_prot_reg);

int mtd_get_user_prot_info(struct mtd_info *mtd, size_t len, size_t *retlen,
			   struct otp_info *buf)
{
	struct mtd_info *master = mtd_get_master(mtd);

	if (!master->_get_user_prot_info)
		return -EOPNOTSUPP;
	if (!len)
		return 0;
	return master->_get_user_prot_info(master, len, retlen, buf);
}
EXPORT_SYMBOL_GPL(mtd_get_user_prot_info);

int mtd_read_user_prot_reg(struct mtd_info *mtd, loff_t from, size_t len,
			   size_t *retlen, u_char *buf)
{
	struct mtd_info *master = mtd_get_master(mtd);

	*retlen = 0;
	if (!master->_read_user_prot_reg)
		return -EOPNOTSUPP;
	if (!len)
		return 0;
	return master->_read_user_prot_reg(master, from, len, retlen, buf);
}
EXPORT_SYMBOL_GPL(mtd_read_user_prot_reg);

int mtd_write_user_prot_reg(struct mtd_info *mtd, loff_t to, size_t len,
			    size_t *retlen, const u_char *buf)
{
	struct mtd_info *master = mtd_get_master(mtd);
	int ret;

	*retlen = 0;
	if (!master->_write_user_prot_reg)
		return -EOPNOTSUPP;
	if (!len)
		return 0;
	ret = master->_write_user_prot_reg(master, to, len, retlen, buf);
	if (ret)
		return ret;

	/*
	 * If no data could be written at all, we are out of memory and
	 * must return -ENOSPC.
	 */
	return (*retlen) ? 0 : -ENOSPC;
}
EXPORT_SYMBOL_GPL(mtd_write_user_prot_reg);

int mtd_lock_user_prot_reg(struct mtd_info *mtd, loff_t from, size_t len)
{
	struct mtd_info *master = mtd_get_master(mtd);

	if (!master->_lock_user_prot_reg)
		return -EOPNOTSUPP;
	if (!len)
		return 0;
	return master->_lock_user_prot_reg(master, from, len);
}
EXPORT_SYMBOL_GPL(mtd_lock_user_prot_reg);

int mtd_erase_user_prot_reg(struct mtd_info *mtd, loff_t from, size_t len)
{
	struct mtd_info *master = mtd_get_master(mtd);

	if (!master->_erase_user_prot_reg)
		return -EOPNOTSUPP;
	if (!len)
		return 0;
	return master->_erase_user_prot_reg(master, from, len);
}
EXPORT_SYMBOL_GPL(mtd_erase_user_prot_reg);

/* Chip-supported device locking */
int mtd_lock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct mtd_info *master = mtd_get_master(mtd);

	if (!master->_lock)
		return -EOPNOTSUPP;
	if (ofs < 0 || ofs >= mtd->size || len > mtd->size - ofs)
		return -EINVAL;
	if (!len)
		return 0;

	if (mtd->flags & MTD_SLC_ON_MLC_EMULATION) {
		ofs = (loff_t)mtd_div_by_eb(ofs, mtd) * master->erasesize;
		len = (u64)mtd_div_by_eb(len, mtd) * master->erasesize;
	}

	return master->_lock(master, mtd_get_master_ofs(mtd, ofs), len);
}
EXPORT_SYMBOL_GPL(mtd_lock);

int mtd_unlock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct mtd_info *master = mtd_get_master(mtd);

	if (!master->_unlock)
		return -EOPNOTSUPP;
	if (ofs < 0 || ofs >= mtd->size || len > mtd->size - ofs)
		return -EINVAL;
	if (!len)
		return 0;

	if (mtd->flags & MTD_SLC_ON_MLC_EMULATION) {
		ofs = (loff_t)mtd_div_by_eb(ofs, mtd) * master->erasesize;
		len = (u64)mtd_div_by_eb(len, mtd) * master->erasesize;
	}

	return master->_unlock(master, mtd_get_master_ofs(mtd, ofs), len);
}
EXPORT_SYMBOL_GPL(mtd_unlock);

int mtd_is_locked(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct mtd_info *master = mtd_get_master(mtd);

	if (!master->_is_locked)
		return -EOPNOTSUPP;
	if (ofs < 0 || ofs >= mtd->size || len > mtd->size - ofs)
		return -EINVAL;
	if (!len)
		return 0;

	if (mtd->flags & MTD_SLC_ON_MLC_EMULATION) {
		ofs = (loff_t)mtd_div_by_eb(ofs, mtd) * master->erasesize;
		len = (u64)mtd_div_by_eb(len, mtd) * master->erasesize;
	}

	return master->_is_locked(master, mtd_get_master_ofs(mtd, ofs), len);
}
EXPORT_SYMBOL_GPL(mtd_is_locked);

int mtd_block_isreserved(struct mtd_info *mtd, loff_t ofs)
{
	struct mtd_info *master = mtd_get_master(mtd);

	if (ofs < 0 || ofs >= mtd->size)
		return -EINVAL;
	if (!master->_block_isreserved)
		return 0;

	if (mtd->flags & MTD_SLC_ON_MLC_EMULATION)
		ofs = (loff_t)mtd_div_by_eb(ofs, mtd) * master->erasesize;

	return master->_block_isreserved(master, mtd_get_master_ofs(mtd, ofs));
}
EXPORT_SYMBOL_GPL(mtd_block_isreserved);

int mtd_block_isbad(struct mtd_info *mtd, loff_t ofs)
{
	struct mtd_info *master = mtd_get_master(mtd);

	if (ofs < 0 || ofs >= mtd->size)
		return -EINVAL;
	if (!master->_block_isbad)
		return 0;

	if (mtd->flags & MTD_SLC_ON_MLC_EMULATION)
		ofs = (loff_t)mtd_div_by_eb(ofs, mtd) * master->erasesize;

	return master->_block_isbad(master, mtd_get_master_ofs(mtd, ofs));
}
EXPORT_SYMBOL_GPL(mtd_block_isbad);

int mtd_block_markbad(struct mtd_info *mtd, loff_t ofs)
{
	struct mtd_info *master = mtd_get_master(mtd);
	int ret;

	if (!master->_block_markbad)
		return -EOPNOTSUPP;
	if (ofs < 0 || ofs >= mtd->size)
		return -EINVAL;
	if (!(mtd->flags & MTD_WRITEABLE))
		return -EROFS;

	if (mtd->flags & MTD_SLC_ON_MLC_EMULATION)
		ofs = (loff_t)mtd_div_by_eb(ofs, mtd) * master->erasesize;

	ret = master->_block_markbad(master, mtd_get_master_ofs(mtd, ofs));
	if (ret)
		return ret;

	while (mtd->parent) {
		mtd->ecc_stats.badblocks++;
		mtd = mtd->parent;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mtd_block_markbad);

/*
 * default_mtd_writev - the default writev method
 * @mtd: mtd device description object pointer
 * @vecs: the vectors to write
 * @count: count of vectors in @vecs
 * @to: the MTD device offset to write to
 * @retlen: on exit contains the count of bytes written to the MTD device.
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 */
static int default_mtd_writev(struct mtd_info *mtd, const struct kvec *vecs,
			      unsigned long count, loff_t to, size_t *retlen)
{
	unsigned long i;
	size_t totlen = 0, thislen;
	int ret = 0;

	for (i = 0; i < count; i++) {
		if (!vecs[i].iov_len)
			continue;
		ret = mtd_write(mtd, to, vecs[i].iov_len, &thislen,
				vecs[i].iov_base);
		totlen += thislen;
		if (ret || thislen != vecs[i].iov_len)
			break;
		to += vecs[i].iov_len;
	}
	*retlen = totlen;
	return ret;
}

/*
 * mtd_writev - the vector-based MTD write method
 * @mtd: mtd device description object pointer
 * @vecs: the vectors to write
 * @count: count of vectors in @vecs
 * @to: the MTD device offset to write to
 * @retlen: on exit contains the count of bytes written to the MTD device.
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 */
int mtd_writev(struct mtd_info *mtd, const struct kvec *vecs,
	       unsigned long count, loff_t to, size_t *retlen)
{
	struct mtd_info *master = mtd_get_master(mtd);

	*retlen = 0;
	if (!(mtd->flags & MTD_WRITEABLE))
		return -EROFS;

	if (!master->_writev)
		return default_mtd_writev(mtd, vecs, count, to, retlen);

	return master->_writev(master, vecs, count,
			       mtd_get_master_ofs(mtd, to), retlen);
}
EXPORT_SYMBOL_GPL(mtd_writev);

/**
 * mtd_kmalloc_up_to - allocate a contiguous buffer up to the specified size
 * @mtd: mtd device description object pointer
 * @size: a pointer to the ideal or maximum size of the allocation, points
 *        to the actual allocation size on success.
 *
 * This routine attempts to allocate a contiguous kernel buffer up to
 * the specified size, backing off the size of the request exponentially
 * until the request succeeds or until the allocation size falls below
 * the system page size. This attempts to make sure it does not adversely
 * impact system performance, so when allocating more than one page, we
 * ask the memory allocator to avoid re-trying, swapping, writing back
 * or performing I/O.
 *
 * Note, this function also makes sure that the allocated buffer is aligned to
 * the MTD device's min. I/O unit, i.e. the "mtd->writesize" value.
 *
 * This is called, for example by mtd_{read,write} and jffs2_scan_medium,
 * to handle smaller (i.e. degraded) buffer allocations under low- or
 * fragmented-memory situations where such reduced allocations, from a
 * requested ideal, are allowed.
 *
 * Returns a pointer to the allocated buffer on success; otherwise, NULL.
 */
void *mtd_kmalloc_up_to(const struct mtd_info *mtd, size_t *size)
{
	gfp_t flags = __GFP_NOWARN | __GFP_DIRECT_RECLAIM | __GFP_NORETRY;
	size_t min_alloc = max_t(size_t, mtd->writesize, PAGE_SIZE);
	void *kbuf;

	*size = min_t(size_t, *size, KMALLOC_MAX_SIZE);

	while (*size > min_alloc) {
		kbuf = kmalloc(*size, flags);
		if (kbuf)
			return kbuf;

		*size >>= 1;
		*size = ALIGN(*size, mtd->writesize);
	}

	/*
	 * For the last resort allocation allow 'kmalloc()' to do all sorts of
	 * things (write-back, dropping caches, etc) by using GFP_KERNEL.
	 */
	return kmalloc(*size, GFP_KERNEL);
}
EXPORT_SYMBOL_GPL(mtd_kmalloc_up_to);

#ifdef CONFIG_PROC_FS

/*====================================================================*/
/* Support for /proc/mtd */

static int mtd_proc_show(struct seq_file *m, void *v)
{
	struct mtd_info *mtd;

	seq_puts(m, "dev:    size   erasesize  name\n");
	mutex_lock(&mtd_table_mutex);
	mtd_for_each_device(mtd) {
		seq_printf(m, "mtd%d: %8.8llx %8.8x \"%s\"\n",
			   mtd->index, (unsigned long long)mtd->size,
			   mtd->erasesize, mtd->name);
	}
	mutex_unlock(&mtd_table_mutex);
	return 0;
}
#endif /* CONFIG_PROC_FS */

/*====================================================================*/
/* Init code */

static struct backing_dev_info * __init mtd_bdi_init(const char *name)
{
	struct backing_dev_info *bdi;
	int ret;

	bdi = bdi_alloc(NUMA_NO_NODE);
	if (!bdi)
		return ERR_PTR(-ENOMEM);
	bdi->ra_pages = 0;
	bdi->io_pages = 0;

	/*
	 * We put '-0' suffix to the name to get the same name format as we
	 * used to get. Since this is called only once, we get a unique name. 
	 */
	ret = bdi_register(bdi, "%.28s-0", name);
	if (ret)
		bdi_put(bdi);

	return ret ? ERR_PTR(ret) : bdi;
}

static struct proc_dir_entry *proc_mtd;

static int __init init_mtd(void)
{
	int ret;

	ret = class_register(&mtd_class);
	if (ret)
		goto err_reg;

	mtd_bdi = mtd_bdi_init("mtd");
	if (IS_ERR(mtd_bdi)) {
		ret = PTR_ERR(mtd_bdi);
		goto err_bdi;
	}

	proc_mtd = proc_create_single("mtd", 0, NULL, mtd_proc_show);

	ret = init_mtdchar();
	if (ret)
		goto out_procfs;

	dfs_dir_mtd = debugfs_create_dir("mtd", NULL);
	debugfs_create_bool("expert_analysis_mode", 0600, dfs_dir_mtd,
			    &mtd_expert_analysis_mode);

	return 0;

out_procfs:
	if (proc_mtd)
		remove_proc_entry("mtd", NULL);
	bdi_put(mtd_bdi);
err_bdi:
	class_unregister(&mtd_class);
err_reg:
	pr_err("Error registering mtd class or bdi: %d\n", ret);
	return ret;
}

static void __exit cleanup_mtd(void)
{
	debugfs_remove_recursive(dfs_dir_mtd);
	cleanup_mtdchar();
	if (proc_mtd)
		remove_proc_entry("mtd", NULL);
	class_unregister(&mtd_class);
	bdi_unregister(mtd_bdi);
	bdi_put(mtd_bdi);
	idr_destroy(&mtd_idr);
}

module_init(init_mtd);
module_exit(cleanup_mtd);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("Core MTD registration and access routines");
