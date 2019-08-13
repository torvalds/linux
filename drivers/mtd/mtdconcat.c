// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MTD device concatenation layer
 *
 * Copyright © 2002 Robert Kaiser <rkaiser@sysgo.de>
 * Copyright © 2002-2010 David Woodhouse <dwmw2@infradead.org>
 *
 * NAND support by Christian Gan <cgan@iders.ca>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/backing-dev.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/concat.h>

#include <asm/div64.h>

/*
 * Our storage structure:
 * Subdev points to an array of pointers to struct mtd_info objects
 * which is allocated along with this structure
 *
 */
struct mtd_concat {
	struct mtd_info mtd;
	int num_subdev;
	struct mtd_info **subdev;
};

/*
 * how to calculate the size required for the above structure,
 * including the pointer array subdev points to:
 */
#define SIZEOF_STRUCT_MTD_CONCAT(num_subdev)	\
	((sizeof(struct mtd_concat) + (num_subdev) * sizeof(struct mtd_info *)))

/*
 * Given a pointer to the MTD object in the mtd_concat structure,
 * we can retrieve the pointer to that structure with this macro.
 */
#define CONCAT(x)  ((struct mtd_concat *)(x))

/*
 * MTD methods which look up the relevant subdevice, translate the
 * effective address and pass through to the subdevice.
 */

static int
concat_read(struct mtd_info *mtd, loff_t from, size_t len,
	    size_t * retlen, u_char * buf)
{
	struct mtd_concat *concat = CONCAT(mtd);
	int ret = 0, err;
	int i;

	for (i = 0; i < concat->num_subdev; i++) {
		struct mtd_info *subdev = concat->subdev[i];
		size_t size, retsize;

		if (from >= subdev->size) {
			/* Not destined for this subdev */
			size = 0;
			from -= subdev->size;
			continue;
		}
		if (from + len > subdev->size)
			/* First part goes into this subdev */
			size = subdev->size - from;
		else
			/* Entire transaction goes into this subdev */
			size = len;

		err = mtd_read(subdev, from, size, &retsize, buf);

		/* Save information about bitflips! */
		if (unlikely(err)) {
			if (mtd_is_eccerr(err)) {
				mtd->ecc_stats.failed++;
				ret = err;
			} else if (mtd_is_bitflip(err)) {
				mtd->ecc_stats.corrected++;
				/* Do not overwrite -EBADMSG !! */
				if (!ret)
					ret = err;
			} else
				return err;
		}

		*retlen += retsize;
		len -= size;
		if (len == 0)
			return ret;

		buf += size;
		from = 0;
	}
	return -EINVAL;
}

static int
concat_write(struct mtd_info *mtd, loff_t to, size_t len,
	     size_t * retlen, const u_char * buf)
{
	struct mtd_concat *concat = CONCAT(mtd);
	int err = -EINVAL;
	int i;

	for (i = 0; i < concat->num_subdev; i++) {
		struct mtd_info *subdev = concat->subdev[i];
		size_t size, retsize;

		if (to >= subdev->size) {
			size = 0;
			to -= subdev->size;
			continue;
		}
		if (to + len > subdev->size)
			size = subdev->size - to;
		else
			size = len;

		err = mtd_write(subdev, to, size, &retsize, buf);
		if (err)
			break;

		*retlen += retsize;
		len -= size;
		if (len == 0)
			break;

		err = -EINVAL;
		buf += size;
		to = 0;
	}
	return err;
}

static int
concat_writev(struct mtd_info *mtd, const struct kvec *vecs,
		unsigned long count, loff_t to, size_t * retlen)
{
	struct mtd_concat *concat = CONCAT(mtd);
	struct kvec *vecs_copy;
	unsigned long entry_low, entry_high;
	size_t total_len = 0;
	int i;
	int err = -EINVAL;

	/* Calculate total length of data */
	for (i = 0; i < count; i++)
		total_len += vecs[i].iov_len;

	/* Check alignment */
	if (mtd->writesize > 1) {
		uint64_t __to = to;
		if (do_div(__to, mtd->writesize) || (total_len % mtd->writesize))
			return -EINVAL;
	}

	/* make a copy of vecs */
	vecs_copy = kmemdup(vecs, sizeof(struct kvec) * count, GFP_KERNEL);
	if (!vecs_copy)
		return -ENOMEM;

	entry_low = 0;
	for (i = 0; i < concat->num_subdev; i++) {
		struct mtd_info *subdev = concat->subdev[i];
		size_t size, wsize, retsize, old_iov_len;

		if (to >= subdev->size) {
			to -= subdev->size;
			continue;
		}

		size = min_t(uint64_t, total_len, subdev->size - to);
		wsize = size; /* store for future use */

		entry_high = entry_low;
		while (entry_high < count) {
			if (size <= vecs_copy[entry_high].iov_len)
				break;
			size -= vecs_copy[entry_high++].iov_len;
		}

		old_iov_len = vecs_copy[entry_high].iov_len;
		vecs_copy[entry_high].iov_len = size;

		err = mtd_writev(subdev, &vecs_copy[entry_low],
				 entry_high - entry_low + 1, to, &retsize);

		vecs_copy[entry_high].iov_len = old_iov_len - size;
		vecs_copy[entry_high].iov_base += size;

		entry_low = entry_high;

		if (err)
			break;

		*retlen += retsize;
		total_len -= wsize;

		if (total_len == 0)
			break;

		err = -EINVAL;
		to = 0;
	}

	kfree(vecs_copy);
	return err;
}

static int
concat_read_oob(struct mtd_info *mtd, loff_t from, struct mtd_oob_ops *ops)
{
	struct mtd_concat *concat = CONCAT(mtd);
	struct mtd_oob_ops devops = *ops;
	int i, err, ret = 0;

	ops->retlen = ops->oobretlen = 0;

	for (i = 0; i < concat->num_subdev; i++) {
		struct mtd_info *subdev = concat->subdev[i];

		if (from >= subdev->size) {
			from -= subdev->size;
			continue;
		}

		/* partial read ? */
		if (from + devops.len > subdev->size)
			devops.len = subdev->size - from;

		err = mtd_read_oob(subdev, from, &devops);
		ops->retlen += devops.retlen;
		ops->oobretlen += devops.oobretlen;

		/* Save information about bitflips! */
		if (unlikely(err)) {
			if (mtd_is_eccerr(err)) {
				mtd->ecc_stats.failed++;
				ret = err;
			} else if (mtd_is_bitflip(err)) {
				mtd->ecc_stats.corrected++;
				/* Do not overwrite -EBADMSG !! */
				if (!ret)
					ret = err;
			} else
				return err;
		}

		if (devops.datbuf) {
			devops.len = ops->len - ops->retlen;
			if (!devops.len)
				return ret;
			devops.datbuf += devops.retlen;
		}
		if (devops.oobbuf) {
			devops.ooblen = ops->ooblen - ops->oobretlen;
			if (!devops.ooblen)
				return ret;
			devops.oobbuf += ops->oobretlen;
		}

		from = 0;
	}
	return -EINVAL;
}

static int
concat_write_oob(struct mtd_info *mtd, loff_t to, struct mtd_oob_ops *ops)
{
	struct mtd_concat *concat = CONCAT(mtd);
	struct mtd_oob_ops devops = *ops;
	int i, err;

	if (!(mtd->flags & MTD_WRITEABLE))
		return -EROFS;

	ops->retlen = ops->oobretlen = 0;

	for (i = 0; i < concat->num_subdev; i++) {
		struct mtd_info *subdev = concat->subdev[i];

		if (to >= subdev->size) {
			to -= subdev->size;
			continue;
		}

		/* partial write ? */
		if (to + devops.len > subdev->size)
			devops.len = subdev->size - to;

		err = mtd_write_oob(subdev, to, &devops);
		ops->retlen += devops.retlen;
		ops->oobretlen += devops.oobretlen;
		if (err)
			return err;

		if (devops.datbuf) {
			devops.len = ops->len - ops->retlen;
			if (!devops.len)
				return 0;
			devops.datbuf += devops.retlen;
		}
		if (devops.oobbuf) {
			devops.ooblen = ops->ooblen - ops->oobretlen;
			if (!devops.ooblen)
				return 0;
			devops.oobbuf += devops.oobretlen;
		}
		to = 0;
	}
	return -EINVAL;
}

static int concat_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct mtd_concat *concat = CONCAT(mtd);
	struct mtd_info *subdev;
	int i, err;
	uint64_t length, offset = 0;
	struct erase_info *erase;

	/*
	 * Check for proper erase block alignment of the to-be-erased area.
	 * It is easier to do this based on the super device's erase
	 * region info rather than looking at each particular sub-device
	 * in turn.
	 */
	if (!concat->mtd.numeraseregions) {
		/* the easy case: device has uniform erase block size */
		if (instr->addr & (concat->mtd.erasesize - 1))
			return -EINVAL;
		if (instr->len & (concat->mtd.erasesize - 1))
			return -EINVAL;
	} else {
		/* device has variable erase size */
		struct mtd_erase_region_info *erase_regions =
		    concat->mtd.eraseregions;

		/*
		 * Find the erase region where the to-be-erased area begins:
		 */
		for (i = 0; i < concat->mtd.numeraseregions &&
		     instr->addr >= erase_regions[i].offset; i++) ;
		--i;

		/*
		 * Now erase_regions[i] is the region in which the
		 * to-be-erased area begins. Verify that the starting
		 * offset is aligned to this region's erase size:
		 */
		if (i < 0 || instr->addr & (erase_regions[i].erasesize - 1))
			return -EINVAL;

		/*
		 * now find the erase region where the to-be-erased area ends:
		 */
		for (; i < concat->mtd.numeraseregions &&
		     (instr->addr + instr->len) >= erase_regions[i].offset;
		     ++i) ;
		--i;
		/*
		 * check if the ending offset is aligned to this region's erase size
		 */
		if (i < 0 || ((instr->addr + instr->len) &
					(erase_regions[i].erasesize - 1)))
			return -EINVAL;
	}

	/* make a local copy of instr to avoid modifying the caller's struct */
	erase = kmalloc(sizeof (struct erase_info), GFP_KERNEL);

	if (!erase)
		return -ENOMEM;

	*erase = *instr;
	length = instr->len;

	/*
	 * find the subdevice where the to-be-erased area begins, adjust
	 * starting offset to be relative to the subdevice start
	 */
	for (i = 0; i < concat->num_subdev; i++) {
		subdev = concat->subdev[i];
		if (subdev->size <= erase->addr) {
			erase->addr -= subdev->size;
			offset += subdev->size;
		} else {
			break;
		}
	}

	/* must never happen since size limit has been verified above */
	BUG_ON(i >= concat->num_subdev);

	/* now do the erase: */
	err = 0;
	for (; length > 0; i++) {
		/* loop for all subdevices affected by this request */
		subdev = concat->subdev[i];	/* get current subdevice */

		/* limit length to subdevice's size: */
		if (erase->addr + length > subdev->size)
			erase->len = subdev->size - erase->addr;
		else
			erase->len = length;

		length -= erase->len;
		if ((err = mtd_erase(subdev, erase))) {
			/* sanity check: should never happen since
			 * block alignment has been checked above */
			BUG_ON(err == -EINVAL);
			if (erase->fail_addr != MTD_FAIL_ADDR_UNKNOWN)
				instr->fail_addr = erase->fail_addr + offset;
			break;
		}
		/*
		 * erase->addr specifies the offset of the area to be
		 * erased *within the current subdevice*. It can be
		 * non-zero only the first time through this loop, i.e.
		 * for the first subdevice where blocks need to be erased.
		 * All the following erases must begin at the start of the
		 * current subdevice, i.e. at offset zero.
		 */
		erase->addr = 0;
		offset += subdev->size;
	}
	kfree(erase);

	return err;
}

static int concat_xxlock(struct mtd_info *mtd, loff_t ofs, uint64_t len,
			 bool is_lock)
{
	struct mtd_concat *concat = CONCAT(mtd);
	int i, err = -EINVAL;

	for (i = 0; i < concat->num_subdev; i++) {
		struct mtd_info *subdev = concat->subdev[i];
		uint64_t size;

		if (ofs >= subdev->size) {
			size = 0;
			ofs -= subdev->size;
			continue;
		}
		if (ofs + len > subdev->size)
			size = subdev->size - ofs;
		else
			size = len;

		if (is_lock)
			err = mtd_lock(subdev, ofs, size);
		else
			err = mtd_unlock(subdev, ofs, size);
		if (err)
			break;

		len -= size;
		if (len == 0)
			break;

		err = -EINVAL;
		ofs = 0;
	}

	return err;
}

static int concat_lock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	return concat_xxlock(mtd, ofs, len, true);
}

static int concat_unlock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	return concat_xxlock(mtd, ofs, len, false);
}

static int concat_is_locked(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct mtd_concat *concat = CONCAT(mtd);
	int i, err = -EINVAL;

	for (i = 0; i < concat->num_subdev; i++) {
		struct mtd_info *subdev = concat->subdev[i];

		if (ofs >= subdev->size) {
			ofs -= subdev->size;
			continue;
		}

		if (ofs + len > subdev->size)
			break;

		return mtd_is_locked(subdev, ofs, len);
	}

	return err;
}

static void concat_sync(struct mtd_info *mtd)
{
	struct mtd_concat *concat = CONCAT(mtd);
	int i;

	for (i = 0; i < concat->num_subdev; i++) {
		struct mtd_info *subdev = concat->subdev[i];
		mtd_sync(subdev);
	}
}

static int concat_suspend(struct mtd_info *mtd)
{
	struct mtd_concat *concat = CONCAT(mtd);
	int i, rc = 0;

	for (i = 0; i < concat->num_subdev; i++) {
		struct mtd_info *subdev = concat->subdev[i];
		if ((rc = mtd_suspend(subdev)) < 0)
			return rc;
	}
	return rc;
}

static void concat_resume(struct mtd_info *mtd)
{
	struct mtd_concat *concat = CONCAT(mtd);
	int i;

	for (i = 0; i < concat->num_subdev; i++) {
		struct mtd_info *subdev = concat->subdev[i];
		mtd_resume(subdev);
	}
}

static int concat_block_isbad(struct mtd_info *mtd, loff_t ofs)
{
	struct mtd_concat *concat = CONCAT(mtd);
	int i, res = 0;

	if (!mtd_can_have_bb(concat->subdev[0]))
		return res;

	for (i = 0; i < concat->num_subdev; i++) {
		struct mtd_info *subdev = concat->subdev[i];

		if (ofs >= subdev->size) {
			ofs -= subdev->size;
			continue;
		}

		res = mtd_block_isbad(subdev, ofs);
		break;
	}

	return res;
}

static int concat_block_markbad(struct mtd_info *mtd, loff_t ofs)
{
	struct mtd_concat *concat = CONCAT(mtd);
	int i, err = -EINVAL;

	for (i = 0; i < concat->num_subdev; i++) {
		struct mtd_info *subdev = concat->subdev[i];

		if (ofs >= subdev->size) {
			ofs -= subdev->size;
			continue;
		}

		err = mtd_block_markbad(subdev, ofs);
		if (!err)
			mtd->ecc_stats.badblocks++;
		break;
	}

	return err;
}

/*
 * This function constructs a virtual MTD device by concatenating
 * num_devs MTD devices. A pointer to the new device object is
 * stored to *new_dev upon success. This function does _not_
 * register any devices: this is the caller's responsibility.
 */
struct mtd_info *mtd_concat_create(struct mtd_info *subdev[],	/* subdevices to concatenate */
				   int num_devs,	/* number of subdevices      */
				   const char *name)
{				/* name for the new device   */
	int i;
	size_t size;
	struct mtd_concat *concat;
	uint32_t max_erasesize, curr_erasesize;
	int num_erase_region;
	int max_writebufsize = 0;

	printk(KERN_NOTICE "Concatenating MTD devices:\n");
	for (i = 0; i < num_devs; i++)
		printk(KERN_NOTICE "(%d): \"%s\"\n", i, subdev[i]->name);
	printk(KERN_NOTICE "into device \"%s\"\n", name);

	/* allocate the device structure */
	size = SIZEOF_STRUCT_MTD_CONCAT(num_devs);
	concat = kzalloc(size, GFP_KERNEL);
	if (!concat) {
		printk
		    ("memory allocation error while creating concatenated device \"%s\"\n",
		     name);
		return NULL;
	}
	concat->subdev = (struct mtd_info **) (concat + 1);

	/*
	 * Set up the new "super" device's MTD object structure, check for
	 * incompatibilities between the subdevices.
	 */
	concat->mtd.type = subdev[0]->type;
	concat->mtd.flags = subdev[0]->flags;
	concat->mtd.size = subdev[0]->size;
	concat->mtd.erasesize = subdev[0]->erasesize;
	concat->mtd.writesize = subdev[0]->writesize;

	for (i = 0; i < num_devs; i++)
		if (max_writebufsize < subdev[i]->writebufsize)
			max_writebufsize = subdev[i]->writebufsize;
	concat->mtd.writebufsize = max_writebufsize;

	concat->mtd.subpage_sft = subdev[0]->subpage_sft;
	concat->mtd.oobsize = subdev[0]->oobsize;
	concat->mtd.oobavail = subdev[0]->oobavail;
	if (subdev[0]->_writev)
		concat->mtd._writev = concat_writev;
	if (subdev[0]->_read_oob)
		concat->mtd._read_oob = concat_read_oob;
	if (subdev[0]->_write_oob)
		concat->mtd._write_oob = concat_write_oob;
	if (subdev[0]->_block_isbad)
		concat->mtd._block_isbad = concat_block_isbad;
	if (subdev[0]->_block_markbad)
		concat->mtd._block_markbad = concat_block_markbad;

	concat->mtd.ecc_stats.badblocks = subdev[0]->ecc_stats.badblocks;

	concat->subdev[0] = subdev[0];

	for (i = 1; i < num_devs; i++) {
		if (concat->mtd.type != subdev[i]->type) {
			kfree(concat);
			printk("Incompatible device type on \"%s\"\n",
			       subdev[i]->name);
			return NULL;
		}
		if (concat->mtd.flags != subdev[i]->flags) {
			/*
			 * Expect all flags except MTD_WRITEABLE to be
			 * equal on all subdevices.
			 */
			if ((concat->mtd.flags ^ subdev[i]->
			     flags) & ~MTD_WRITEABLE) {
				kfree(concat);
				printk("Incompatible device flags on \"%s\"\n",
				       subdev[i]->name);
				return NULL;
			} else
				/* if writeable attribute differs,
				   make super device writeable */
				concat->mtd.flags |=
				    subdev[i]->flags & MTD_WRITEABLE;
		}

		concat->mtd.size += subdev[i]->size;
		concat->mtd.ecc_stats.badblocks +=
			subdev[i]->ecc_stats.badblocks;
		if (concat->mtd.writesize   !=  subdev[i]->writesize ||
		    concat->mtd.subpage_sft != subdev[i]->subpage_sft ||
		    concat->mtd.oobsize    !=  subdev[i]->oobsize ||
		    !concat->mtd._read_oob  != !subdev[i]->_read_oob ||
		    !concat->mtd._write_oob != !subdev[i]->_write_oob) {
			kfree(concat);
			printk("Incompatible OOB or ECC data on \"%s\"\n",
			       subdev[i]->name);
			return NULL;
		}
		concat->subdev[i] = subdev[i];

	}

	mtd_set_ooblayout(&concat->mtd, subdev[0]->ooblayout);

	concat->num_subdev = num_devs;
	concat->mtd.name = name;

	concat->mtd._erase = concat_erase;
	concat->mtd._read = concat_read;
	concat->mtd._write = concat_write;
	concat->mtd._sync = concat_sync;
	concat->mtd._lock = concat_lock;
	concat->mtd._unlock = concat_unlock;
	concat->mtd._is_locked = concat_is_locked;
	concat->mtd._suspend = concat_suspend;
	concat->mtd._resume = concat_resume;

	/*
	 * Combine the erase block size info of the subdevices:
	 *
	 * first, walk the map of the new device and see how
	 * many changes in erase size we have
	 */
	max_erasesize = curr_erasesize = subdev[0]->erasesize;
	num_erase_region = 1;
	for (i = 0; i < num_devs; i++) {
		if (subdev[i]->numeraseregions == 0) {
			/* current subdevice has uniform erase size */
			if (subdev[i]->erasesize != curr_erasesize) {
				/* if it differs from the last subdevice's erase size, count it */
				++num_erase_region;
				curr_erasesize = subdev[i]->erasesize;
				if (curr_erasesize > max_erasesize)
					max_erasesize = curr_erasesize;
			}
		} else {
			/* current subdevice has variable erase size */
			int j;
			for (j = 0; j < subdev[i]->numeraseregions; j++) {

				/* walk the list of erase regions, count any changes */
				if (subdev[i]->eraseregions[j].erasesize !=
				    curr_erasesize) {
					++num_erase_region;
					curr_erasesize =
					    subdev[i]->eraseregions[j].
					    erasesize;
					if (curr_erasesize > max_erasesize)
						max_erasesize = curr_erasesize;
				}
			}
		}
	}

	if (num_erase_region == 1) {
		/*
		 * All subdevices have the same uniform erase size.
		 * This is easy:
		 */
		concat->mtd.erasesize = curr_erasesize;
		concat->mtd.numeraseregions = 0;
	} else {
		uint64_t tmp64;

		/*
		 * erase block size varies across the subdevices: allocate
		 * space to store the data describing the variable erase regions
		 */
		struct mtd_erase_region_info *erase_region_p;
		uint64_t begin, position;

		concat->mtd.erasesize = max_erasesize;
		concat->mtd.numeraseregions = num_erase_region;
		concat->mtd.eraseregions = erase_region_p =
		    kmalloc_array(num_erase_region,
				  sizeof(struct mtd_erase_region_info),
				  GFP_KERNEL);
		if (!erase_region_p) {
			kfree(concat);
			printk
			    ("memory allocation error while creating erase region list"
			     " for device \"%s\"\n", name);
			return NULL;
		}

		/*
		 * walk the map of the new device once more and fill in
		 * in erase region info:
		 */
		curr_erasesize = subdev[0]->erasesize;
		begin = position = 0;
		for (i = 0; i < num_devs; i++) {
			if (subdev[i]->numeraseregions == 0) {
				/* current subdevice has uniform erase size */
				if (subdev[i]->erasesize != curr_erasesize) {
					/*
					 *  fill in an mtd_erase_region_info structure for the area
					 *  we have walked so far:
					 */
					erase_region_p->offset = begin;
					erase_region_p->erasesize =
					    curr_erasesize;
					tmp64 = position - begin;
					do_div(tmp64, curr_erasesize);
					erase_region_p->numblocks = tmp64;
					begin = position;

					curr_erasesize = subdev[i]->erasesize;
					++erase_region_p;
				}
				position += subdev[i]->size;
			} else {
				/* current subdevice has variable erase size */
				int j;
				for (j = 0; j < subdev[i]->numeraseregions; j++) {
					/* walk the list of erase regions, count any changes */
					if (subdev[i]->eraseregions[j].
					    erasesize != curr_erasesize) {
						erase_region_p->offset = begin;
						erase_region_p->erasesize =
						    curr_erasesize;
						tmp64 = position - begin;
						do_div(tmp64, curr_erasesize);
						erase_region_p->numblocks = tmp64;
						begin = position;

						curr_erasesize =
						    subdev[i]->eraseregions[j].
						    erasesize;
						++erase_region_p;
					}
					position +=
					    subdev[i]->eraseregions[j].
					    numblocks * (uint64_t)curr_erasesize;
				}
			}
		}
		/* Now write the final entry */
		erase_region_p->offset = begin;
		erase_region_p->erasesize = curr_erasesize;
		tmp64 = position - begin;
		do_div(tmp64, curr_erasesize);
		erase_region_p->numblocks = tmp64;
	}

	return &concat->mtd;
}

/*
 * This function destroys an MTD object obtained from concat_mtd_devs()
 */

void mtd_concat_destroy(struct mtd_info *mtd)
{
	struct mtd_concat *concat = CONCAT(mtd);
	if (concat->mtd.numeraseregions)
		kfree(concat->mtd.eraseregions);
	kfree(concat);
}

EXPORT_SYMBOL(mtd_concat_create);
EXPORT_SYMBOL(mtd_concat_destroy);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Robert Kaiser <rkaiser@sysgo.de>");
MODULE_DESCRIPTION("Generic support for concatenating of MTD devices");
