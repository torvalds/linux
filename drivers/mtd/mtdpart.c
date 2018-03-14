/*
 * Simple MTD partitioning layer
 *
 * Copyright © 2000 Nicolas Pitre <nico@fluxnic.net>
 * Copyright © 2002 Thomas Gleixner <gleixner@linutronix.de>
 * Copyright © 2000-2010 David Woodhouse <dwmw2@infradead.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/kmod.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/err.h>
#include <linux/of.h>

#include "mtdcore.h"

/* Our partition linked list */
static LIST_HEAD(mtd_partitions);
static DEFINE_MUTEX(mtd_partitions_mutex);

/**
 * struct mtd_part - our partition node structure
 *
 * @mtd: struct holding partition details
 * @parent: parent mtd - flash device or another partition
 * @offset: partition offset relative to the *flash device*
 */
struct mtd_part {
	struct mtd_info mtd;
	struct mtd_info *parent;
	uint64_t offset;
	struct list_head list;
};

/*
 * Given a pointer to the MTD object in the mtd_part structure, we can retrieve
 * the pointer to that structure.
 */
static inline struct mtd_part *mtd_to_part(const struct mtd_info *mtd)
{
	return container_of(mtd, struct mtd_part, mtd);
}


/*
 * MTD methods which simply translate the effective address and pass through
 * to the _real_ device.
 */

static int part_read(struct mtd_info *mtd, loff_t from, size_t len,
		size_t *retlen, u_char *buf)
{
	struct mtd_part *part = mtd_to_part(mtd);
	struct mtd_ecc_stats stats;
	int res;

	stats = part->parent->ecc_stats;
	res = part->parent->_read(part->parent, from + part->offset, len,
				  retlen, buf);
	if (unlikely(mtd_is_eccerr(res)))
		mtd->ecc_stats.failed +=
			part->parent->ecc_stats.failed - stats.failed;
	else
		mtd->ecc_stats.corrected +=
			part->parent->ecc_stats.corrected - stats.corrected;
	return res;
}

static int part_point(struct mtd_info *mtd, loff_t from, size_t len,
		size_t *retlen, void **virt, resource_size_t *phys)
{
	struct mtd_part *part = mtd_to_part(mtd);

	return part->parent->_point(part->parent, from + part->offset, len,
				    retlen, virt, phys);
}

static int part_unpoint(struct mtd_info *mtd, loff_t from, size_t len)
{
	struct mtd_part *part = mtd_to_part(mtd);

	return part->parent->_unpoint(part->parent, from + part->offset, len);
}

static int part_read_oob(struct mtd_info *mtd, loff_t from,
		struct mtd_oob_ops *ops)
{
	struct mtd_part *part = mtd_to_part(mtd);
	struct mtd_ecc_stats stats;
	int res;

	stats = part->parent->ecc_stats;
	res = part->parent->_read_oob(part->parent, from + part->offset, ops);
	if (unlikely(mtd_is_eccerr(res)))
		mtd->ecc_stats.failed +=
			part->parent->ecc_stats.failed - stats.failed;
	else
		mtd->ecc_stats.corrected +=
			part->parent->ecc_stats.corrected - stats.corrected;
	return res;
}

static int part_read_user_prot_reg(struct mtd_info *mtd, loff_t from,
		size_t len, size_t *retlen, u_char *buf)
{
	struct mtd_part *part = mtd_to_part(mtd);
	return part->parent->_read_user_prot_reg(part->parent, from, len,
						 retlen, buf);
}

static int part_get_user_prot_info(struct mtd_info *mtd, size_t len,
				   size_t *retlen, struct otp_info *buf)
{
	struct mtd_part *part = mtd_to_part(mtd);
	return part->parent->_get_user_prot_info(part->parent, len, retlen,
						 buf);
}

static int part_read_fact_prot_reg(struct mtd_info *mtd, loff_t from,
		size_t len, size_t *retlen, u_char *buf)
{
	struct mtd_part *part = mtd_to_part(mtd);
	return part->parent->_read_fact_prot_reg(part->parent, from, len,
						 retlen, buf);
}

static int part_get_fact_prot_info(struct mtd_info *mtd, size_t len,
				   size_t *retlen, struct otp_info *buf)
{
	struct mtd_part *part = mtd_to_part(mtd);
	return part->parent->_get_fact_prot_info(part->parent, len, retlen,
						 buf);
}

static int part_write(struct mtd_info *mtd, loff_t to, size_t len,
		size_t *retlen, const u_char *buf)
{
	struct mtd_part *part = mtd_to_part(mtd);
	return part->parent->_write(part->parent, to + part->offset, len,
				    retlen, buf);
}

static int part_panic_write(struct mtd_info *mtd, loff_t to, size_t len,
		size_t *retlen, const u_char *buf)
{
	struct mtd_part *part = mtd_to_part(mtd);
	return part->parent->_panic_write(part->parent, to + part->offset, len,
					  retlen, buf);
}

static int part_write_oob(struct mtd_info *mtd, loff_t to,
		struct mtd_oob_ops *ops)
{
	struct mtd_part *part = mtd_to_part(mtd);

	return part->parent->_write_oob(part->parent, to + part->offset, ops);
}

static int part_write_user_prot_reg(struct mtd_info *mtd, loff_t from,
		size_t len, size_t *retlen, u_char *buf)
{
	struct mtd_part *part = mtd_to_part(mtd);
	return part->parent->_write_user_prot_reg(part->parent, from, len,
						  retlen, buf);
}

static int part_lock_user_prot_reg(struct mtd_info *mtd, loff_t from,
		size_t len)
{
	struct mtd_part *part = mtd_to_part(mtd);
	return part->parent->_lock_user_prot_reg(part->parent, from, len);
}

static int part_writev(struct mtd_info *mtd, const struct kvec *vecs,
		unsigned long count, loff_t to, size_t *retlen)
{
	struct mtd_part *part = mtd_to_part(mtd);
	return part->parent->_writev(part->parent, vecs, count,
				     to + part->offset, retlen);
}

static int part_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct mtd_part *part = mtd_to_part(mtd);
	int ret;

	instr->addr += part->offset;
	ret = part->parent->_erase(part->parent, instr);
	if (instr->fail_addr != MTD_FAIL_ADDR_UNKNOWN)
		instr->fail_addr -= part->offset;
	instr->addr -= part->offset;

	return ret;
}

static int part_lock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct mtd_part *part = mtd_to_part(mtd);
	return part->parent->_lock(part->parent, ofs + part->offset, len);
}

static int part_unlock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct mtd_part *part = mtd_to_part(mtd);
	return part->parent->_unlock(part->parent, ofs + part->offset, len);
}

static int part_is_locked(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct mtd_part *part = mtd_to_part(mtd);
	return part->parent->_is_locked(part->parent, ofs + part->offset, len);
}

static void part_sync(struct mtd_info *mtd)
{
	struct mtd_part *part = mtd_to_part(mtd);
	part->parent->_sync(part->parent);
}

static int part_suspend(struct mtd_info *mtd)
{
	struct mtd_part *part = mtd_to_part(mtd);
	return part->parent->_suspend(part->parent);
}

static void part_resume(struct mtd_info *mtd)
{
	struct mtd_part *part = mtd_to_part(mtd);
	part->parent->_resume(part->parent);
}

static int part_block_isreserved(struct mtd_info *mtd, loff_t ofs)
{
	struct mtd_part *part = mtd_to_part(mtd);
	ofs += part->offset;
	return part->parent->_block_isreserved(part->parent, ofs);
}

static int part_block_isbad(struct mtd_info *mtd, loff_t ofs)
{
	struct mtd_part *part = mtd_to_part(mtd);
	ofs += part->offset;
	return part->parent->_block_isbad(part->parent, ofs);
}

static int part_block_markbad(struct mtd_info *mtd, loff_t ofs)
{
	struct mtd_part *part = mtd_to_part(mtd);
	int res;

	ofs += part->offset;
	res = part->parent->_block_markbad(part->parent, ofs);
	if (!res)
		mtd->ecc_stats.badblocks++;
	return res;
}

static int part_get_device(struct mtd_info *mtd)
{
	struct mtd_part *part = mtd_to_part(mtd);
	return part->parent->_get_device(part->parent);
}

static void part_put_device(struct mtd_info *mtd)
{
	struct mtd_part *part = mtd_to_part(mtd);
	part->parent->_put_device(part->parent);
}

static int part_ooblayout_ecc(struct mtd_info *mtd, int section,
			      struct mtd_oob_region *oobregion)
{
	struct mtd_part *part = mtd_to_part(mtd);

	return mtd_ooblayout_ecc(part->parent, section, oobregion);
}

static int part_ooblayout_free(struct mtd_info *mtd, int section,
			       struct mtd_oob_region *oobregion)
{
	struct mtd_part *part = mtd_to_part(mtd);

	return mtd_ooblayout_free(part->parent, section, oobregion);
}

static const struct mtd_ooblayout_ops part_ooblayout_ops = {
	.ecc = part_ooblayout_ecc,
	.free = part_ooblayout_free,
};

static int part_max_bad_blocks(struct mtd_info *mtd, loff_t ofs, size_t len)
{
	struct mtd_part *part = mtd_to_part(mtd);

	return part->parent->_max_bad_blocks(part->parent,
					     ofs + part->offset, len);
}

static inline void free_partition(struct mtd_part *p)
{
	kfree(p->mtd.name);
	kfree(p);
}

/**
 * mtd_parse_part - parse MTD partition looking for subpartitions
 *
 * @slave: part that is supposed to be a container and should be parsed
 * @types: NULL-terminated array with names of partition parsers to try
 *
 * Some partitions are kind of containers with extra subpartitions (volumes).
 * There can be various formats of such containers. This function tries to use
 * specified parsers to analyze given partition and registers found
 * subpartitions on success.
 */
static int mtd_parse_part(struct mtd_part *slave, const char *const *types)
{
	struct mtd_partitions parsed;
	int err;

	err = parse_mtd_partitions(&slave->mtd, types, &parsed, NULL);
	if (err)
		return err;
	else if (!parsed.nr_parts)
		return -ENOENT;

	err = add_mtd_partitions(&slave->mtd, parsed.parts, parsed.nr_parts);

	mtd_part_parser_cleanup(&parsed);

	return err;
}

static struct mtd_part *allocate_partition(struct mtd_info *parent,
			const struct mtd_partition *part, int partno,
			uint64_t cur_offset)
{
	int wr_alignment = (parent->flags & MTD_NO_ERASE) ? parent->writesize :
							    parent->erasesize;
	struct mtd_part *slave;
	u32 remainder;
	char *name;
	u64 tmp;

	/* allocate the partition structure */
	slave = kzalloc(sizeof(*slave), GFP_KERNEL);
	name = kstrdup(part->name, GFP_KERNEL);
	if (!name || !slave) {
		printk(KERN_ERR"memory allocation error while creating partitions for \"%s\"\n",
		       parent->name);
		kfree(name);
		kfree(slave);
		return ERR_PTR(-ENOMEM);
	}

	/* set up the MTD object for this partition */
	slave->mtd.type = parent->type;
	slave->mtd.flags = parent->flags & ~part->mask_flags;
	slave->mtd.size = part->size;
	slave->mtd.writesize = parent->writesize;
	slave->mtd.writebufsize = parent->writebufsize;
	slave->mtd.oobsize = parent->oobsize;
	slave->mtd.oobavail = parent->oobavail;
	slave->mtd.subpage_sft = parent->subpage_sft;
	slave->mtd.pairing = parent->pairing;

	slave->mtd.name = name;
	slave->mtd.owner = parent->owner;

	/* NOTE: Historically, we didn't arrange MTDs as a tree out of
	 * concern for showing the same data in multiple partitions.
	 * However, it is very useful to have the master node present,
	 * so the MTD_PARTITIONED_MASTER option allows that. The master
	 * will have device nodes etc only if this is set, so make the
	 * parent conditional on that option. Note, this is a way to
	 * distinguish between the master and the partition in sysfs.
	 */
	slave->mtd.dev.parent = IS_ENABLED(CONFIG_MTD_PARTITIONED_MASTER) || mtd_is_partition(parent) ?
				&parent->dev :
				parent->dev.parent;
	slave->mtd.dev.of_node = part->of_node;

	if (parent->_read)
		slave->mtd._read = part_read;
	if (parent->_write)
		slave->mtd._write = part_write;

	if (parent->_panic_write)
		slave->mtd._panic_write = part_panic_write;

	if (parent->_point && parent->_unpoint) {
		slave->mtd._point = part_point;
		slave->mtd._unpoint = part_unpoint;
	}

	if (parent->_read_oob)
		slave->mtd._read_oob = part_read_oob;
	if (parent->_write_oob)
		slave->mtd._write_oob = part_write_oob;
	if (parent->_read_user_prot_reg)
		slave->mtd._read_user_prot_reg = part_read_user_prot_reg;
	if (parent->_read_fact_prot_reg)
		slave->mtd._read_fact_prot_reg = part_read_fact_prot_reg;
	if (parent->_write_user_prot_reg)
		slave->mtd._write_user_prot_reg = part_write_user_prot_reg;
	if (parent->_lock_user_prot_reg)
		slave->mtd._lock_user_prot_reg = part_lock_user_prot_reg;
	if (parent->_get_user_prot_info)
		slave->mtd._get_user_prot_info = part_get_user_prot_info;
	if (parent->_get_fact_prot_info)
		slave->mtd._get_fact_prot_info = part_get_fact_prot_info;
	if (parent->_sync)
		slave->mtd._sync = part_sync;
	if (!partno && !parent->dev.class && parent->_suspend &&
	    parent->_resume) {
		slave->mtd._suspend = part_suspend;
		slave->mtd._resume = part_resume;
	}
	if (parent->_writev)
		slave->mtd._writev = part_writev;
	if (parent->_lock)
		slave->mtd._lock = part_lock;
	if (parent->_unlock)
		slave->mtd._unlock = part_unlock;
	if (parent->_is_locked)
		slave->mtd._is_locked = part_is_locked;
	if (parent->_block_isreserved)
		slave->mtd._block_isreserved = part_block_isreserved;
	if (parent->_block_isbad)
		slave->mtd._block_isbad = part_block_isbad;
	if (parent->_block_markbad)
		slave->mtd._block_markbad = part_block_markbad;
	if (parent->_max_bad_blocks)
		slave->mtd._max_bad_blocks = part_max_bad_blocks;

	if (parent->_get_device)
		slave->mtd._get_device = part_get_device;
	if (parent->_put_device)
		slave->mtd._put_device = part_put_device;

	slave->mtd._erase = part_erase;
	slave->parent = parent;
	slave->offset = part->offset;

	if (slave->offset == MTDPART_OFS_APPEND)
		slave->offset = cur_offset;
	if (slave->offset == MTDPART_OFS_NXTBLK) {
		tmp = cur_offset;
		slave->offset = cur_offset;
		remainder = do_div(tmp, wr_alignment);
		if (remainder) {
			slave->offset += wr_alignment - remainder;
			printk(KERN_NOTICE "Moving partition %d: "
			       "0x%012llx -> 0x%012llx\n", partno,
			       (unsigned long long)cur_offset, (unsigned long long)slave->offset);
		}
	}
	if (slave->offset == MTDPART_OFS_RETAIN) {
		slave->offset = cur_offset;
		if (parent->size - slave->offset >= slave->mtd.size) {
			slave->mtd.size = parent->size - slave->offset
							- slave->mtd.size;
		} else {
			printk(KERN_ERR "mtd partition \"%s\" doesn't have enough space: %#llx < %#llx, disabled\n",
				part->name, parent->size - slave->offset,
				slave->mtd.size);
			/* register to preserve ordering */
			goto out_register;
		}
	}
	if (slave->mtd.size == MTDPART_SIZ_FULL)
		slave->mtd.size = parent->size - slave->offset;

	printk(KERN_NOTICE "0x%012llx-0x%012llx : \"%s\"\n", (unsigned long long)slave->offset,
		(unsigned long long)(slave->offset + slave->mtd.size), slave->mtd.name);

	/* let's do some sanity checks */
	if (slave->offset >= parent->size) {
		/* let's register it anyway to preserve ordering */
		slave->offset = 0;
		slave->mtd.size = 0;
		printk(KERN_ERR"mtd: partition \"%s\" is out of reach -- disabled\n",
			part->name);
		goto out_register;
	}
	if (slave->offset + slave->mtd.size > parent->size) {
		slave->mtd.size = parent->size - slave->offset;
		printk(KERN_WARNING"mtd: partition \"%s\" extends beyond the end of device \"%s\" -- size truncated to %#llx\n",
			part->name, parent->name, (unsigned long long)slave->mtd.size);
	}
	if (parent->numeraseregions > 1) {
		/* Deal with variable erase size stuff */
		int i, max = parent->numeraseregions;
		u64 end = slave->offset + slave->mtd.size;
		struct mtd_erase_region_info *regions = parent->eraseregions;

		/* Find the first erase regions which is part of this
		 * partition. */
		for (i = 0; i < max && regions[i].offset <= slave->offset; i++)
			;
		/* The loop searched for the region _behind_ the first one */
		if (i > 0)
			i--;

		/* Pick biggest erasesize */
		for (; i < max && regions[i].offset < end; i++) {
			if (slave->mtd.erasesize < regions[i].erasesize) {
				slave->mtd.erasesize = regions[i].erasesize;
			}
		}
		BUG_ON(slave->mtd.erasesize == 0);
	} else {
		/* Single erase size */
		slave->mtd.erasesize = parent->erasesize;
	}

	/*
	 * Slave erasesize might differ from the master one if the master
	 * exposes several regions with different erasesize. Adjust
	 * wr_alignment accordingly.
	 */
	if (!(slave->mtd.flags & MTD_NO_ERASE))
		wr_alignment = slave->mtd.erasesize;

	tmp = slave->offset;
	remainder = do_div(tmp, wr_alignment);
	if ((slave->mtd.flags & MTD_WRITEABLE) && remainder) {
		/* Doesn't start on a boundary of major erase size */
		/* FIXME: Let it be writable if it is on a boundary of
		 * _minor_ erase size though */
		slave->mtd.flags &= ~MTD_WRITEABLE;
		printk(KERN_WARNING"mtd: partition \"%s\" doesn't start on an erase/write block boundary -- force read-only\n",
			part->name);
	}

	tmp = slave->mtd.size;
	remainder = do_div(tmp, wr_alignment);
	if ((slave->mtd.flags & MTD_WRITEABLE) && remainder) {
		slave->mtd.flags &= ~MTD_WRITEABLE;
		printk(KERN_WARNING"mtd: partition \"%s\" doesn't end on an erase/write block -- force read-only\n",
			part->name);
	}

	mtd_set_ooblayout(&slave->mtd, &part_ooblayout_ops);
	slave->mtd.ecc_step_size = parent->ecc_step_size;
	slave->mtd.ecc_strength = parent->ecc_strength;
	slave->mtd.bitflip_threshold = parent->bitflip_threshold;

	if (parent->_block_isbad) {
		uint64_t offs = 0;

		while (offs < slave->mtd.size) {
			if (mtd_block_isreserved(parent, offs + slave->offset))
				slave->mtd.ecc_stats.bbtblocks++;
			else if (mtd_block_isbad(parent, offs + slave->offset))
				slave->mtd.ecc_stats.badblocks++;
			offs += slave->mtd.erasesize;
		}
	}

out_register:
	return slave;
}

static ssize_t mtd_partition_offset_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	struct mtd_part *part = mtd_to_part(mtd);
	return snprintf(buf, PAGE_SIZE, "%lld\n", part->offset);
}

static DEVICE_ATTR(offset, S_IRUGO, mtd_partition_offset_show, NULL);

static const struct attribute *mtd_partition_attrs[] = {
	&dev_attr_offset.attr,
	NULL
};

static int mtd_add_partition_attrs(struct mtd_part *new)
{
	int ret = sysfs_create_files(&new->mtd.dev.kobj, mtd_partition_attrs);
	if (ret)
		printk(KERN_WARNING
		       "mtd: failed to create partition attrs, err=%d\n", ret);
	return ret;
}

int mtd_add_partition(struct mtd_info *parent, const char *name,
		      long long offset, long long length)
{
	struct mtd_partition part;
	struct mtd_part *new;
	int ret = 0;

	/* the direct offset is expected */
	if (offset == MTDPART_OFS_APPEND ||
	    offset == MTDPART_OFS_NXTBLK)
		return -EINVAL;

	if (length == MTDPART_SIZ_FULL)
		length = parent->size - offset;

	if (length <= 0)
		return -EINVAL;

	memset(&part, 0, sizeof(part));
	part.name = name;
	part.size = length;
	part.offset = offset;

	new = allocate_partition(parent, &part, -1, offset);
	if (IS_ERR(new))
		return PTR_ERR(new);

	mutex_lock(&mtd_partitions_mutex);
	list_add(&new->list, &mtd_partitions);
	mutex_unlock(&mtd_partitions_mutex);

	add_mtd_device(&new->mtd);

	mtd_add_partition_attrs(new);

	return ret;
}
EXPORT_SYMBOL_GPL(mtd_add_partition);

/**
 * __mtd_del_partition - delete MTD partition
 *
 * @priv: internal MTD struct for partition to be deleted
 *
 * This function must be called with the partitions mutex locked.
 */
static int __mtd_del_partition(struct mtd_part *priv)
{
	struct mtd_part *child, *next;
	int err;

	list_for_each_entry_safe(child, next, &mtd_partitions, list) {
		if (child->parent == &priv->mtd) {
			err = __mtd_del_partition(child);
			if (err)
				return err;
		}
	}

	sysfs_remove_files(&priv->mtd.dev.kobj, mtd_partition_attrs);

	err = del_mtd_device(&priv->mtd);
	if (err)
		return err;

	list_del(&priv->list);
	free_partition(priv);

	return 0;
}

/*
 * This function unregisters and destroy all slave MTD objects which are
 * attached to the given MTD object.
 */
int del_mtd_partitions(struct mtd_info *mtd)
{
	struct mtd_part *slave, *next;
	int ret, err = 0;

	mutex_lock(&mtd_partitions_mutex);
	list_for_each_entry_safe(slave, next, &mtd_partitions, list)
		if (slave->parent == mtd) {
			ret = __mtd_del_partition(slave);
			if (ret < 0)
				err = ret;
		}
	mutex_unlock(&mtd_partitions_mutex);

	return err;
}

int mtd_del_partition(struct mtd_info *mtd, int partno)
{
	struct mtd_part *slave, *next;
	int ret = -EINVAL;

	mutex_lock(&mtd_partitions_mutex);
	list_for_each_entry_safe(slave, next, &mtd_partitions, list)
		if ((slave->parent == mtd) &&
		    (slave->mtd.index == partno)) {
			ret = __mtd_del_partition(slave);
			break;
		}
	mutex_unlock(&mtd_partitions_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mtd_del_partition);

/*
 * This function, given a master MTD object and a partition table, creates
 * and registers slave MTD objects which are bound to the master according to
 * the partition definitions.
 *
 * For historical reasons, this function's caller only registers the master
 * if the MTD_PARTITIONED_MASTER config option is set.
 */

int add_mtd_partitions(struct mtd_info *master,
		       const struct mtd_partition *parts,
		       int nbparts)
{
	struct mtd_part *slave;
	uint64_t cur_offset = 0;
	int i;

	printk(KERN_NOTICE "Creating %d MTD partitions on \"%s\":\n", nbparts, master->name);

	for (i = 0; i < nbparts; i++) {
		slave = allocate_partition(master, parts + i, i, cur_offset);
		if (IS_ERR(slave)) {
			del_mtd_partitions(master);
			return PTR_ERR(slave);
		}

		mutex_lock(&mtd_partitions_mutex);
		list_add(&slave->list, &mtd_partitions);
		mutex_unlock(&mtd_partitions_mutex);

		add_mtd_device(&slave->mtd);
		mtd_add_partition_attrs(slave);
		if (parts[i].types)
			mtd_parse_part(slave, parts[i].types);

		cur_offset = slave->offset + slave->mtd.size;
	}

	return 0;
}

static DEFINE_SPINLOCK(part_parser_lock);
static LIST_HEAD(part_parsers);

static struct mtd_part_parser *mtd_part_parser_get(const char *name)
{
	struct mtd_part_parser *p, *ret = NULL;

	spin_lock(&part_parser_lock);

	list_for_each_entry(p, &part_parsers, list)
		if (!strcmp(p->name, name) && try_module_get(p->owner)) {
			ret = p;
			break;
		}

	spin_unlock(&part_parser_lock);

	return ret;
}

static inline void mtd_part_parser_put(const struct mtd_part_parser *p)
{
	module_put(p->owner);
}

/*
 * Many partition parsers just expected the core to kfree() all their data in
 * one chunk. Do that by default.
 */
static void mtd_part_parser_cleanup_default(const struct mtd_partition *pparts,
					    int nr_parts)
{
	kfree(pparts);
}

int __register_mtd_parser(struct mtd_part_parser *p, struct module *owner)
{
	p->owner = owner;

	if (!p->cleanup)
		p->cleanup = &mtd_part_parser_cleanup_default;

	spin_lock(&part_parser_lock);
	list_add(&p->list, &part_parsers);
	spin_unlock(&part_parser_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(__register_mtd_parser);

void deregister_mtd_parser(struct mtd_part_parser *p)
{
	spin_lock(&part_parser_lock);
	list_del(&p->list);
	spin_unlock(&part_parser_lock);
}
EXPORT_SYMBOL_GPL(deregister_mtd_parser);

/*
 * Do not forget to update 'parse_mtd_partitions()' kerneldoc comment if you
 * are changing this array!
 */
static const char * const default_mtd_part_types[] = {
	"cmdlinepart",
	"ofpart",
	NULL
};

static int mtd_part_do_parse(struct mtd_part_parser *parser,
			     struct mtd_info *master,
			     struct mtd_partitions *pparts,
			     struct mtd_part_parser_data *data)
{
	int ret;

	ret = (*parser->parse_fn)(master, &pparts->parts, data);
	pr_debug("%s: parser %s: %i\n", master->name, parser->name, ret);
	if (ret <= 0)
		return ret;

	pr_notice("%d %s partitions found on MTD device %s\n", ret,
		  parser->name, master->name);

	pparts->nr_parts = ret;
	pparts->parser = parser;

	return ret;
}

/**
 * mtd_part_get_compatible_parser - find MTD parser by a compatible string
 *
 * @compat: compatible string describing partitions in a device tree
 *
 * MTD parsers can specify supported partitions by providing a table of
 * compatibility strings. This function finds a parser that advertises support
 * for a passed value of "compatible".
 */
static struct mtd_part_parser *mtd_part_get_compatible_parser(const char *compat)
{
	struct mtd_part_parser *p, *ret = NULL;

	spin_lock(&part_parser_lock);

	list_for_each_entry(p, &part_parsers, list) {
		const struct of_device_id *matches;

		matches = p->of_match_table;
		if (!matches)
			continue;

		for (; matches->compatible[0]; matches++) {
			if (!strcmp(matches->compatible, compat) &&
			    try_module_get(p->owner)) {
				ret = p;
				break;
			}
		}

		if (ret)
			break;
	}

	spin_unlock(&part_parser_lock);

	return ret;
}

static int mtd_part_of_parse(struct mtd_info *master,
			     struct mtd_partitions *pparts)
{
	struct mtd_part_parser *parser;
	struct device_node *np;
	struct property *prop;
	const char *compat;
	const char *fixed = "ofpart";
	int ret, err = 0;

	np = of_get_child_by_name(mtd_get_of_node(master), "partitions");
	of_property_for_each_string(np, "compatible", prop, compat) {
		parser = mtd_part_get_compatible_parser(compat);
		if (!parser)
			continue;
		ret = mtd_part_do_parse(parser, master, pparts, NULL);
		if (ret > 0) {
			of_node_put(np);
			return ret;
		}
		mtd_part_parser_put(parser);
		if (ret < 0 && !err)
			err = ret;
	}
	of_node_put(np);

	/*
	 * For backward compatibility we have to try the "ofpart"
	 * parser. It supports old DT format with partitions specified as a
	 * direct subnodes of a flash device DT node without any compatibility
	 * specified we could match.
	 */
	parser = mtd_part_parser_get(fixed);
	if (!parser && !request_module("%s", fixed))
		parser = mtd_part_parser_get(fixed);
	if (parser) {
		ret = mtd_part_do_parse(parser, master, pparts, NULL);
		if (ret > 0)
			return ret;
		mtd_part_parser_put(parser);
		if (ret < 0 && !err)
			err = ret;
	}

	return err;
}

/**
 * parse_mtd_partitions - parse MTD partitions
 * @master: the master partition (describes whole MTD device)
 * @types: names of partition parsers to try or %NULL
 * @pparts: info about partitions found is returned here
 * @data: MTD partition parser-specific data
 *
 * This function tries to find partition on MTD device @master. It uses MTD
 * partition parsers, specified in @types. However, if @types is %NULL, then
 * the default list of parsers is used. The default list contains only the
 * "cmdlinepart" and "ofpart" parsers ATM.
 * Note: If there are more then one parser in @types, the kernel only takes the
 * partitions parsed out by the first parser.
 *
 * This function may return:
 * o a negative error code in case of failure
 * o zero otherwise, and @pparts will describe the partitions, number of
 *   partitions, and the parser which parsed them. Caller must release
 *   resources with mtd_part_parser_cleanup() when finished with the returned
 *   data.
 */
int parse_mtd_partitions(struct mtd_info *master, const char *const *types,
			 struct mtd_partitions *pparts,
			 struct mtd_part_parser_data *data)
{
	struct mtd_part_parser *parser;
	int ret, err = 0;

	if (!types)
		types = default_mtd_part_types;

	for ( ; *types; types++) {
		/*
		 * ofpart is a special type that means OF partitioning info
		 * should be used. It requires a bit different logic so it is
		 * handled in a separated function.
		 */
		if (!strcmp(*types, "ofpart")) {
			ret = mtd_part_of_parse(master, pparts);
		} else {
			pr_debug("%s: parsing partitions %s\n", master->name,
				 *types);
			parser = mtd_part_parser_get(*types);
			if (!parser && !request_module("%s", *types))
				parser = mtd_part_parser_get(*types);
			pr_debug("%s: got parser %s\n", master->name,
				parser ? parser->name : NULL);
			if (!parser)
				continue;
			ret = mtd_part_do_parse(parser, master, pparts, data);
			if (ret <= 0)
				mtd_part_parser_put(parser);
		}
		/* Found partitions! */
		if (ret > 0)
			return 0;
		/*
		 * Stash the first error we see; only report it if no parser
		 * succeeds
		 */
		if (ret < 0 && !err)
			err = ret;
	}
	return err;
}

void mtd_part_parser_cleanup(struct mtd_partitions *parts)
{
	const struct mtd_part_parser *parser;

	if (!parts)
		return;

	parser = parts->parser;
	if (parser) {
		if (parser->cleanup)
			parser->cleanup(parts->parts, parts->nr_parts);

		mtd_part_parser_put(parser);
	}
}

int mtd_is_partition(const struct mtd_info *mtd)
{
	struct mtd_part *part;
	int ispart = 0;

	mutex_lock(&mtd_partitions_mutex);
	list_for_each_entry(part, &mtd_partitions, list)
		if (&part->mtd == mtd) {
			ispart = 1;
			break;
		}
	mutex_unlock(&mtd_partitions_mutex);

	return ispart;
}
EXPORT_SYMBOL_GPL(mtd_is_partition);

/* Returns the size of the entire flash chip */
uint64_t mtd_get_device_size(const struct mtd_info *mtd)
{
	if (!mtd_is_partition(mtd))
		return mtd->size;

	return mtd_get_device_size(mtd_to_part(mtd)->parent);
}
EXPORT_SYMBOL_GPL(mtd_get_device_size);
