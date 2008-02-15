/* MTD-based superblock management
 *
 * Copyright © 2001-2007 Red Hat, Inc. All Rights Reserved.
 * Written by:  David Howells <dhowells@redhat.com>
 *              David Woodhouse <dwmw2@infradead.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/mtd/super.h>
#include <linux/namei.h>
#include <linux/ctype.h>

/*
 * compare superblocks to see if they're equivalent
 * - they are if the underlying MTD device is the same
 */
static int get_sb_mtd_compare(struct super_block *sb, void *_mtd)
{
	struct mtd_info *mtd = _mtd;

	if (sb->s_mtd == mtd) {
		DEBUG(2, "MTDSB: Match on device %d (\"%s\")\n",
		      mtd->index, mtd->name);
		return 1;
	}

	DEBUG(2, "MTDSB: No match, device %d (\"%s\"), device %d (\"%s\")\n",
	      sb->s_mtd->index, sb->s_mtd->name, mtd->index, mtd->name);
	return 0;
}

/*
 * mark the superblock by the MTD device it is using
 * - set the device number to be the correct MTD block device for pesuperstence
 *   of NFS exports
 */
static int get_sb_mtd_set(struct super_block *sb, void *_mtd)
{
	struct mtd_info *mtd = _mtd;

	sb->s_mtd = mtd;
	sb->s_dev = MKDEV(MTD_BLOCK_MAJOR, mtd->index);
	return 0;
}

/*
 * get a superblock on an MTD-backed filesystem
 */
static int get_sb_mtd_aux(struct file_system_type *fs_type, int flags,
			  const char *dev_name, void *data,
			  struct mtd_info *mtd,
			  int (*fill_super)(struct super_block *, void *, int),
			  struct vfsmount *mnt)
{
	struct super_block *sb;
	int ret;

	sb = sget(fs_type, get_sb_mtd_compare, get_sb_mtd_set, mtd);
	if (IS_ERR(sb))
		goto out_error;

	if (sb->s_root)
		goto already_mounted;

	/* fresh new superblock */
	DEBUG(1, "MTDSB: New superblock for device %d (\"%s\")\n",
	      mtd->index, mtd->name);

	sb->s_flags = flags;

	ret = fill_super(sb, data, flags & MS_SILENT ? 1 : 0);
	if (ret < 0) {
		up_write(&sb->s_umount);
		deactivate_super(sb);
		return ret;
	}

	/* go */
	sb->s_flags |= MS_ACTIVE;
	return simple_set_mnt(mnt, sb);

	/* new mountpoint for an already mounted superblock */
already_mounted:
	DEBUG(1, "MTDSB: Device %d (\"%s\") is already mounted\n",
	      mtd->index, mtd->name);
	ret = simple_set_mnt(mnt, sb);
	goto out_put;

out_error:
	ret = PTR_ERR(sb);
out_put:
	put_mtd_device(mtd);
	return ret;
}

/*
 * get a superblock on an MTD-backed filesystem by MTD device number
 */
static int get_sb_mtd_nr(struct file_system_type *fs_type, int flags,
			 const char *dev_name, void *data, int mtdnr,
			 int (*fill_super)(struct super_block *, void *, int),
			 struct vfsmount *mnt)
{
	struct mtd_info *mtd;

	mtd = get_mtd_device(NULL, mtdnr);
	if (IS_ERR(mtd)) {
		DEBUG(0, "MTDSB: Device #%u doesn't appear to exist\n", mtdnr);
		return PTR_ERR(mtd);
	}

	return get_sb_mtd_aux(fs_type, flags, dev_name, data, mtd, fill_super,
			      mnt);
}

/*
 * set up an MTD-based superblock
 */
int get_sb_mtd(struct file_system_type *fs_type, int flags,
	       const char *dev_name, void *data,
	       int (*fill_super)(struct super_block *, void *, int),
	       struct vfsmount *mnt)
{
	struct nameidata nd;
	int mtdnr, ret;

	if (!dev_name)
		return -EINVAL;

	DEBUG(2, "MTDSB: dev_name \"%s\"\n", dev_name);

	/* the preferred way of mounting in future; especially when
	 * CONFIG_BLOCK=n - we specify the underlying MTD device by number or
	 * by name, so that we don't require block device support to be present
	 * in the kernel. */
	if (dev_name[0] == 'm' && dev_name[1] == 't' && dev_name[2] == 'd') {
		if (dev_name[3] == ':') {
			struct mtd_info *mtd;

			/* mount by MTD device name */
			DEBUG(1, "MTDSB: mtd:%%s, name \"%s\"\n",
			      dev_name + 4);

			for (mtdnr = 0; mtdnr < MAX_MTD_DEVICES; mtdnr++) {
				mtd = get_mtd_device(NULL, mtdnr);
				if (!IS_ERR(mtd)) {
					if (!strcmp(mtd->name, dev_name + 4))
						return get_sb_mtd_aux(
							fs_type, flags,
							dev_name, data, mtd,
							fill_super, mnt);

					put_mtd_device(mtd);
				}
			}

			printk(KERN_NOTICE "MTD:"
			       " MTD device with name \"%s\" not found.\n",
			       dev_name + 4);

		} else if (isdigit(dev_name[3])) {
			/* mount by MTD device number name */
			char *endptr;

			mtdnr = simple_strtoul(dev_name + 3, &endptr, 0);
			if (!*endptr) {
				/* It was a valid number */
				DEBUG(1, "MTDSB: mtd%%d, mtdnr %d\n",
				      mtdnr);
				return get_sb_mtd_nr(fs_type, flags,
						     dev_name, data,
						     mtdnr, fill_super, mnt);
			}
		}
	}

	/* try the old way - the hack where we allowed users to mount
	 * /dev/mtdblock$(n) but didn't actually _use_ the blockdev
	 */
	ret = path_lookup(dev_name, LOOKUP_FOLLOW, &nd);

	DEBUG(1, "MTDSB: path_lookup() returned %d, inode %p\n",
	      ret, nd.path.dentry ? nd.path.dentry->d_inode : NULL);

	if (ret)
		return ret;

	ret = -EINVAL;

	if (!S_ISBLK(nd.path.dentry->d_inode->i_mode))
		goto out;

	if (nd.path.mnt->mnt_flags & MNT_NODEV) {
		ret = -EACCES;
		goto out;
	}

	if (imajor(nd.path.dentry->d_inode) != MTD_BLOCK_MAJOR)
		goto not_an_MTD_device;

	mtdnr = iminor(nd.path.dentry->d_inode);
	path_release(&nd);

	return get_sb_mtd_nr(fs_type, flags, dev_name, data, mtdnr, fill_super,
			     mnt);

not_an_MTD_device:
	if (!(flags & MS_SILENT))
		printk(KERN_NOTICE
		       "MTD: Attempt to mount non-MTD device \"%s\"\n",
		       dev_name);
out:
	path_release(&nd);
	return ret;

}

EXPORT_SYMBOL_GPL(get_sb_mtd);

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
