// SPDX-License-Identifier: GPL-2.0-or-later
/* MTD-based superblock management
 *
 * Copyright © 2001-2007 Red Hat, Inc. All Rights Reserved.
 * Copyright © 2001-2010 David Woodhouse <dwmw2@infradead.org>
 *
 * Written by:  David Howells <dhowells@redhat.com>
 *              David Woodhouse <dwmw2@infradead.org>
 */

#include <linux/mtd/super.h>
#include <linux/namei.h>
#include <linux/export.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/major.h>
#include <linux/backing-dev.h>
#include <linux/fs_context.h>
#include "mtdcore.h"

/*
 * compare superblocks to see if they're equivalent
 * - they are if the underlying MTD device is the same
 */
static int mtd_test_super(struct super_block *sb, struct fs_context *fc)
{
	struct mtd_info *mtd = fc->sget_key;

	if (sb->s_mtd == fc->sget_key) {
		pr_debug("MTDSB: Match on device %d (\"%s\")\n",
			 mtd->index, mtd->name);
		return 1;
	}

	pr_debug("MTDSB: No match, device %d (\"%s\"), device %d (\"%s\")\n",
		 sb->s_mtd->index, sb->s_mtd->name, mtd->index, mtd->name);
	return 0;
}

/*
 * mark the superblock by the MTD device it is using
 * - set the device number to be the correct MTD block device for pesuperstence
 *   of NFS exports
 */
static int mtd_set_super(struct super_block *sb, struct fs_context *fc)
{
	sb->s_mtd = fc->sget_key;
	sb->s_dev = MKDEV(MTD_BLOCK_MAJOR, sb->s_mtd->index);
	sb->s_bdi = bdi_get(mtd_bdi);
	return 0;
}

/*
 * get a superblock on an MTD-backed filesystem
 */
static int mtd_get_sb(struct fs_context *fc,
		      struct mtd_info *mtd,
		      int (*fill_super)(struct super_block *,
					struct fs_context *))
{
	struct super_block *sb;
	int ret;

	fc->sget_key = mtd;
	sb = sget_fc(fc, mtd_test_super, mtd_set_super);
	if (IS_ERR(sb))
		return PTR_ERR(sb);

	if (sb->s_root) {
		/* new mountpoint for an already mounted superblock */
		pr_debug("MTDSB: Device %d (\"%s\") is already mounted\n",
			 mtd->index, mtd->name);
		put_mtd_device(mtd);
	} else {
		/* fresh new superblock */
		pr_debug("MTDSB: New superblock for device %d (\"%s\")\n",
			 mtd->index, mtd->name);

		ret = fill_super(sb, fc);
		if (ret < 0)
			goto error_sb;

		sb->s_flags |= SB_ACTIVE;
	}

	BUG_ON(fc->root);
	fc->root = dget(sb->s_root);
	return 0;

error_sb:
	deactivate_locked_super(sb);
	return ret;
}

/*
 * get a superblock on an MTD-backed filesystem by MTD device number
 */
static int mtd_get_sb_by_nr(struct fs_context *fc, int mtdnr,
			    int (*fill_super)(struct super_block *,
					      struct fs_context *))
{
	struct mtd_info *mtd;

	mtd = get_mtd_device(NULL, mtdnr);
	if (IS_ERR(mtd)) {
		errorf(fc, "MTDSB: Device #%u doesn't appear to exist\n", mtdnr);
		return PTR_ERR(mtd);
	}

	return mtd_get_sb(fc, mtd, fill_super);
}

/**
 * get_tree_mtd - Get a superblock based on a single MTD device
 * @fc: The filesystem context holding the parameters
 * @fill_super: Helper to initialise a new superblock
 */
int get_tree_mtd(struct fs_context *fc,
	      int (*fill_super)(struct super_block *sb,
				struct fs_context *fc))
{
#ifdef CONFIG_BLOCK
	struct block_device *bdev;
	int ret, major;
#endif
	int mtdnr;

	if (!fc->source)
		return invalf(fc, "No source specified");

	pr_debug("MTDSB: dev_name \"%s\"\n", fc->source);

	/* the preferred way of mounting in future; especially when
	 * CONFIG_BLOCK=n - we specify the underlying MTD device by number or
	 * by name, so that we don't require block device support to be present
	 * in the kernel.
	 */
	if (fc->source[0] == 'm' &&
	    fc->source[1] == 't' &&
	    fc->source[2] == 'd') {
		if (fc->source[3] == ':') {
			struct mtd_info *mtd;

			/* mount by MTD device name */
			pr_debug("MTDSB: mtd:%%s, name \"%s\"\n",
				 fc->source + 4);

			mtd = get_mtd_device_nm(fc->source + 4);
			if (!IS_ERR(mtd))
				return mtd_get_sb(fc, mtd, fill_super);

			errorf(fc, "MTD: MTD device with name \"%s\" not found",
			       fc->source + 4);

		} else if (isdigit(fc->source[3])) {
			/* mount by MTD device number name */
			char *endptr;

			mtdnr = simple_strtoul(fc->source + 3, &endptr, 0);
			if (!*endptr) {
				/* It was a valid number */
				pr_debug("MTDSB: mtd%%d, mtdnr %d\n", mtdnr);
				return mtd_get_sb_by_nr(fc, mtdnr, fill_super);
			}
		}
	}

#ifdef CONFIG_BLOCK
	/* try the old way - the hack where we allowed users to mount
	 * /dev/mtdblock$(n) but didn't actually _use_ the blockdev
	 */
	bdev = lookup_bdev(fc->source);
	if (IS_ERR(bdev)) {
		ret = PTR_ERR(bdev);
		errorf(fc, "MTD: Couldn't look up '%s': %d", fc->source, ret);
		return ret;
	}
	pr_debug("MTDSB: lookup_bdev() returned 0\n");

	major = MAJOR(bdev->bd_dev);
	mtdnr = MINOR(bdev->bd_dev);
	bdput(bdev);

	if (major == MTD_BLOCK_MAJOR)
		return mtd_get_sb_by_nr(fc, mtdnr, fill_super);

#endif /* CONFIG_BLOCK */

	if (!(fc->sb_flags & SB_SILENT))
		errorf(fc, "MTD: Attempt to mount non-MTD device \"%s\"",
		       fc->source);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(get_tree_mtd);

/*
 * destroy an MTD-based superblock
 */
void kill_mtd_super(struct super_block *sb)
{
	generic_shutdown_super(sb);
	put_mtd_device(sb->s_mtd);
	sb->s_mtd = NULL;
}

EXPORT_SYMBOL_GPL(kill_mtd_super);
