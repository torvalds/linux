/*
 * MTD device concatenation layer
 *
 * (C) 2002 Robert Kaiser <rkaiser@sysgo.de>
 *
 * NAND support by Christian Gan <cgan@iders.ca>
 *
 * This code is GPL
 *
 * $Id: mtdconcat.c,v 1.9 2004/06/30 15:17:41 dbrown Exp $
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sched.h>	/* TASK_* */
#include <linux/mtd/mtd.h>
#include <linux/mtd/concat.h>

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
	int err = -EINVAL;
	int i;

	*retlen = 0;

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

		err = subdev->read(subdev, from, size, &retsize, buf);

		if (err)
			break;

		*retlen += retsize;
		len -= size;
		if (len == 0)
			break;

		err = -EINVAL;
		buf += size;
		from = 0;
	}
	return err;
}

static int
concat_write(struct mtd_info *mtd, loff_t to, size_t len,
	     size_t * retlen, const u_char * buf)
{
	struct mtd_concat *concat = CONCAT(mtd);
	int err = -EINVAL;
	int i;

	if (!(mtd->flags & MTD_WRITEABLE))
		return -EROFS;

	*retlen = 0;

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

		if (!(subdev->flags & MTD_WRITEABLE))
			err = -EROFS;
		else
			err = subdev->write(subdev, to, size, &retsize, buf);

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
concat_read_ecc(struct mtd_info *mtd, loff_t from, size_t len,
		size_t * retlen, u_char * buf, u_char * eccbuf,
		struct nand_oobinfo *oobsel)
{
	struct mtd_concat *concat = CONCAT(mtd);
	int err = -EINVAL;
	int i;

	*retlen = 0;

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

		if (subdev->read_ecc)
			err = subdev->read_ecc(subdev, from, size,
					       &retsize, buf, eccbuf, oobsel);
		else
			err = -EINVAL;

		if (err)
			break;

		*retlen += retsize;
		len -= size;
		if (len == 0)
			break;

		err = -EINVAL;
		buf += size;
		if (eccbuf) {
			eccbuf += subdev->oobsize;
			/* in nand.c at least, eccbufs are
			   tagged with 2 (int)eccstatus'; we
			   must account for these */
			eccbuf += 2 * (sizeof (int));
		}
		from = 0;
	}
	return err;
}

static int
concat_write_ecc(struct mtd_info *mtd, loff_t to, size_t len,
		 size_t * retlen, const u_char * buf, u_char * eccbuf,
		 struct nand_oobinfo *oobsel)
{
	struct mtd_concat *concat = CONCAT(mtd);
	int err = -EINVAL;
	int i;

	if (!(mtd->flags & MTD_WRITEABLE))
		return -EROFS;

	*retlen = 0;

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

		if (!(subdev->flags & MTD_WRITEABLE))
			err = -EROFS;
		else if (subdev->write_ecc)
			err = subdev->write_ecc(subdev, to, size,
						&retsize, buf, eccbuf, oobsel);
		else
			err = -EINVAL;

		if (err)
			break;

		*retlen += retsize;
		len -= size;
		if (len == 0)
			break;

		err = -EINVAL;
		buf += size;
		if (eccbuf)
			eccbuf += subdev->oobsize;
		to = 0;
	}
	return err;
}

static int
concat_read_oob(struct mtd_info *mtd, loff_t from, size_t len,
		size_t * retlen, u_char * buf)
{
	struct mtd_concat *concat = CONCAT(mtd);
	int err = -EINVAL;
	int i;

	*retlen = 0;

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

		if (subdev->read_oob)
			err = subdev->read_oob(subdev, from, size,
					       &retsize, buf);
		else
			err = -EINVAL;

		if (err)
			break;

		*retlen += retsize;
		len -= size;
		if (len == 0)
			break;

		err = -EINVAL;
		buf += size;
		from = 0;
	}
	return err;
}

static int
concat_write_oob(struct mtd_info *mtd, loff_t to, size_t len,
		 size_t * retlen, const u_char * buf)
{
	struct mtd_concat *concat = CONCAT(mtd);
	int err = -EINVAL;
	int i;

	if (!(mtd->flags & MTD_WRITEABLE))
		return -EROFS;

	*retlen = 0;

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

		if (!(subdev->flags & MTD_WRITEABLE))
			err = -EROFS;
		else if (subdev->write_oob)
			err = subdev->write_oob(subdev, to, size, &retsize,
						buf);
		else
			err = -EINVAL;

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

static void concat_erase_callback(struct erase_info *instr)
{
	wake_up((wait_queue_head_t *) instr->priv);
}

static int concat_dev_erase(struct mtd_info *mtd, struct erase_info *erase)
{
	int err;
	wait_queue_head_t waitq;
	DECLARE_WAITQUEUE(wait, current);

	/*
	 * This code was stol^H^H^H^Hinspired by mtdchar.c
	 */
	init_waitqueue_head(&waitq);

	erase->mtd = mtd;
	erase->callback = concat_erase_callback;
	erase->priv = (unsigned long) &waitq;

	/*
	 * FIXME: Allow INTERRUPTIBLE. Which means
	 * not having the wait_queue head on the stack.
	 */
	err = mtd->erase(mtd, erase);
	if (!err) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&waitq, &wait);
		if (erase->state != MTD_ERASE_DONE
		    && erase->state != MTD_ERASE_FAILED)
			schedule();
		remove_wait_queue(&waitq, &wait);
		set_current_state(TASK_RUNNING);

		err = (erase->state == MTD_ERASE_FAILED) ? -EIO : 0;
	}
	return err;
}

static int concat_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct mtd_concat *concat = CONCAT(mtd);
	struct mtd_info *subdev;
	int i, err;
	u_int32_t length, offset = 0;
	struct erase_info *erase;

	if (!(mtd->flags & MTD_WRITEABLE))
		return -EROFS;

	if (instr->addr > concat->mtd.size)
		return -EINVAL;

	if (instr->len + instr->addr > concat->mtd.size)
		return -EINVAL;

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
		if (instr->addr & (erase_regions[i].erasesize - 1))
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
		if ((instr->addr + instr->len) & (erase_regions[i].erasesize -
						  1))
			return -EINVAL;
	}

	instr->fail_addr = 0xffffffff;

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
	if (i >= concat->num_subdev)
		BUG();

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

		if (!(subdev->flags & MTD_WRITEABLE)) {
			err = -EROFS;
			break;
		}
		length -= erase->len;
		if ((err = concat_dev_erase(subdev, erase))) {
			/* sanity check: should never happen since
			 * block alignment has been checked above */
			if (err == -EINVAL)
				BUG();
			if (erase->fail_addr != 0xffffffff)
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
	instr->state = erase->state;
	kfree(erase);
	if (err)
		return err;

	if (instr->callback)
		instr->callback(instr);
	return 0;
}

static int concat_lock(struct mtd_info *mtd, loff_t ofs, size_t len)
{
	struct mtd_concat *concat = CONCAT(mtd);
	int i, err = -EINVAL;

	if ((len + ofs) > mtd->size)
		return -EINVAL;

	for (i = 0; i < concat->num_subdev; i++) {
		struct mtd_info *subdev = concat->subdev[i];
		size_t size;

		if (ofs >= subdev->size) {
			size = 0;
			ofs -= subdev->size;
			continue;
		}
		if (ofs + len > subdev->size)
			size = subdev->size - ofs;
		else
			size = len;

		err = subdev->lock(subdev, ofs, size);

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

static int concat_unlock(struct mtd_info *mtd, loff_t ofs, size_t len)
{
	struct mtd_concat *concat = CONCAT(mtd);
	int i, err = 0;

	if ((len + ofs) > mtd->size)
		return -EINVAL;

	for (i = 0; i < concat->num_subdev; i++) {
		struct mtd_info *subdev = concat->subdev[i];
		size_t size;

		if (ofs >= subdev->size) {
			size = 0;
			ofs -= subdev->size;
			continue;
		}
		if (ofs + len > subdev->size)
			size = subdev->size - ofs;
		else
			size = len;

		err = subdev->unlock(subdev, ofs, size);

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

static void concat_sync(struct mtd_info *mtd)
{
	struct mtd_concat *concat = CONCAT(mtd);
	int i;

	for (i = 0; i < concat->num_subdev; i++) {
		struct mtd_info *subdev = concat->subdev[i];
		subdev->sync(subdev);
	}
}

static int concat_suspend(struct mtd_info *mtd)
{
	struct mtd_concat *concat = CONCAT(mtd);
	int i, rc = 0;

	for (i = 0; i < concat->num_subdev; i++) {
		struct mtd_info *subdev = concat->subdev[i];
		if ((rc = subdev->suspend(subdev)) < 0)
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
		subdev->resume(subdev);
	}
}

/*
 * This function constructs a virtual MTD device by concatenating
 * num_devs MTD devices. A pointer to the new device object is
 * stored to *new_dev upon success. This function does _not_
 * register any devices: this is the caller's responsibility.
 */
struct mtd_info *mtd_concat_create(struct mtd_info *subdev[],	/* subdevices to concatenate */
				   int num_devs,	/* number of subdevices      */
				   char *name)
{				/* name for the new device   */
	int i;
	size_t size;
	struct mtd_concat *concat;
	u_int32_t max_erasesize, curr_erasesize;
	int num_erase_region;

	printk(KERN_NOTICE "Concatenating MTD devices:\n");
	for (i = 0; i < num_devs; i++)
		printk(KERN_NOTICE "(%d): \"%s\"\n", i, subdev[i]->name);
	printk(KERN_NOTICE "into device \"%s\"\n", name);

	/* allocate the device structure */
	size = SIZEOF_STRUCT_MTD_CONCAT(num_devs);
	concat = kmalloc(size, GFP_KERNEL);
	if (!concat) {
		printk
		    ("memory allocation error while creating concatenated device \"%s\"\n",
		     name);
		return NULL;
	}
	memset(concat, 0, size);
	concat->subdev = (struct mtd_info **) (concat + 1);

	/*
	 * Set up the new "super" device's MTD object structure, check for
	 * incompatibilites between the subdevices.
	 */
	concat->mtd.type = subdev[0]->type;
	concat->mtd.flags = subdev[0]->flags;
	concat->mtd.size = subdev[0]->size;
	concat->mtd.erasesize = subdev[0]->erasesize;
	concat->mtd.oobblock = subdev[0]->oobblock;
	concat->mtd.oobsize = subdev[0]->oobsize;
	concat->mtd.ecctype = subdev[0]->ecctype;
	concat->mtd.eccsize = subdev[0]->eccsize;
	if (subdev[0]->read_ecc)
		concat->mtd.read_ecc = concat_read_ecc;
	if (subdev[0]->write_ecc)
		concat->mtd.write_ecc = concat_write_ecc;
	if (subdev[0]->read_oob)
		concat->mtd.read_oob = concat_read_oob;
	if (subdev[0]->write_oob)
		concat->mtd.write_oob = concat_write_oob;

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
		if (concat->mtd.oobblock   !=  subdev[i]->oobblock ||
		    concat->mtd.oobsize    !=  subdev[i]->oobsize ||
		    concat->mtd.ecctype    !=  subdev[i]->ecctype ||
		    concat->mtd.eccsize    !=  subdev[i]->eccsize ||
		    !concat->mtd.read_ecc  != !subdev[i]->read_ecc ||
		    !concat->mtd.write_ecc != !subdev[i]->write_ecc ||
		    !concat->mtd.read_oob  != !subdev[i]->read_oob ||
		    !concat->mtd.write_oob != !subdev[i]->write_oob) {
			kfree(concat);
			printk("Incompatible OOB or ECC data on \"%s\"\n",
			       subdev[i]->name);
			return NULL;
		}
		concat->subdev[i] = subdev[i];

	}

	concat->num_subdev = num_devs;
	concat->mtd.name = name;

	/*
	 * NOTE: for now, we do not provide any readv()/writev() methods
	 *       because they are messy to implement and they are not
	 *       used to a great extent anyway.
	 */
	concat->mtd.erase = concat_erase;
	concat->mtd.read = concat_read;
	concat->mtd.write = concat_write;
	concat->mtd.sync = concat_sync;
	concat->mtd.lock = concat_lock;
	concat->mtd.unlock = concat_unlock;
	concat->mtd.suspend = concat_suspend;
	concat->mtd.resume = concat_resume;

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
		/*
		 * erase block size varies across the subdevices: allocate
		 * space to store the data describing the variable erase regions
		 */
		struct mtd_erase_region_info *erase_region_p;
		u_int32_t begin, position;

		concat->mtd.erasesize = max_erasesize;
		concat->mtd.numeraseregions = num_erase_region;
		concat->mtd.eraseregions = erase_region_p =
		    kmalloc(num_erase_region *
			    sizeof (struct mtd_erase_region_info), GFP_KERNEL);
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
					erase_region_p->numblocks =
					    (position - begin) / curr_erasesize;
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
						erase_region_p->numblocks =
						    (position -
						     begin) / curr_erasesize;
						begin = position;

						curr_erasesize =
						    subdev[i]->eraseregions[j].
						    erasesize;
						++erase_region_p;
					}
					position +=
					    subdev[i]->eraseregions[j].
					    numblocks * curr_erasesize;
				}
			}
		}
		/* Now write the final entry */
		erase_region_p->offset = begin;
		erase_region_p->erasesize = curr_erasesize;
		erase_region_p->numblocks = (position - begin) / curr_erasesize;
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
