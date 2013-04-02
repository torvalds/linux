/*
 *  linux/fs/adfs/super.c
 *
 *  Copyright (C) 1997-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/buffer_head.h>
#include <linux/parser.h>
#include <linux/mount.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/user_namespace.h>
#include "adfs.h"
#include "dir_f.h"
#include "dir_fplus.h"

#define ADFS_DEFAULT_OWNER_MASK S_IRWXU
#define ADFS_DEFAULT_OTHER_MASK (S_IRWXG | S_IRWXO)

void __adfs_error(struct super_block *sb, const char *function, const char *fmt, ...)
{
	char error_buf[128];
	va_list args;

	va_start(args, fmt);
	vsnprintf(error_buf, sizeof(error_buf), fmt, args);
	va_end(args);

	printk(KERN_CRIT "ADFS-fs error (device %s)%s%s: %s\n",
		sb->s_id, function ? ": " : "",
		function ? function : "", error_buf);
}

static int adfs_checkdiscrecord(struct adfs_discrecord *dr)
{
	int i;

	/* sector size must be 256, 512 or 1024 bytes */
	if (dr->log2secsize != 8 &&
	    dr->log2secsize != 9 &&
	    dr->log2secsize != 10)
		return 1;

	/* idlen must be at least log2secsize + 3 */
	if (dr->idlen < dr->log2secsize + 3)
		return 1;

	/* we cannot have such a large disc that we
	 * are unable to represent sector offsets in
	 * 32 bits.  This works out at 2.0 TB.
	 */
	if (le32_to_cpu(dr->disc_size_high) >> dr->log2secsize)
		return 1;

	/* idlen must be no greater than 19 v2 [1.0] */
	if (dr->idlen > 19)
		return 1;

	/* reserved bytes should be zero */
	for (i = 0; i < sizeof(dr->unused52); i++)
		if (dr->unused52[i] != 0)
			return 1;

	return 0;
}

static unsigned char adfs_calczonecheck(struct super_block *sb, unsigned char *map)
{
	unsigned int v0, v1, v2, v3;
	int i;

	v0 = v1 = v2 = v3 = 0;
	for (i = sb->s_blocksize - 4; i; i -= 4) {
		v0 += map[i]     + (v3 >> 8);
		v3 &= 0xff;
		v1 += map[i + 1] + (v0 >> 8);
		v0 &= 0xff;
		v2 += map[i + 2] + (v1 >> 8);
		v1 &= 0xff;
		v3 += map[i + 3] + (v2 >> 8);
		v2 &= 0xff;
	}
	v0 +=           v3 >> 8;
	v1 += map[1] + (v0 >> 8);
	v2 += map[2] + (v1 >> 8);
	v3 += map[3] + (v2 >> 8);

	return v0 ^ v1 ^ v2 ^ v3;
}

static int adfs_checkmap(struct super_block *sb, struct adfs_discmap *dm)
{
	unsigned char crosscheck = 0, zonecheck = 1;
	int i;

	for (i = 0; i < ADFS_SB(sb)->s_map_size; i++) {
		unsigned char *map;

		map = dm[i].dm_bh->b_data;

		if (adfs_calczonecheck(sb, map) != map[0]) {
			adfs_error(sb, "zone %d fails zonecheck", i);
			zonecheck = 0;
		}
		crosscheck ^= map[3];
	}
	if (crosscheck != 0xff)
		adfs_error(sb, "crosscheck != 0xff");
	return crosscheck == 0xff && zonecheck;
}

static void adfs_put_super(struct super_block *sb)
{
	int i;
	struct adfs_sb_info *asb = ADFS_SB(sb);

	for (i = 0; i < asb->s_map_size; i++)
		brelse(asb->s_map[i].dm_bh);
	kfree(asb->s_map);
	kfree(asb);
	sb->s_fs_info = NULL;
}

static int adfs_show_options(struct seq_file *seq, struct dentry *root)
{
	struct adfs_sb_info *asb = ADFS_SB(root->d_sb);

	if (!uid_eq(asb->s_uid, GLOBAL_ROOT_UID))
		seq_printf(seq, ",uid=%u", from_kuid_munged(&init_user_ns, asb->s_uid));
	if (!gid_eq(asb->s_gid, GLOBAL_ROOT_GID))
		seq_printf(seq, ",gid=%u", from_kgid_munged(&init_user_ns, asb->s_gid));
	if (asb->s_owner_mask != ADFS_DEFAULT_OWNER_MASK)
		seq_printf(seq, ",ownmask=%o", asb->s_owner_mask);
	if (asb->s_other_mask != ADFS_DEFAULT_OTHER_MASK)
		seq_printf(seq, ",othmask=%o", asb->s_other_mask);
	if (asb->s_ftsuffix != 0)
		seq_printf(seq, ",ftsuffix=%u", asb->s_ftsuffix);

	return 0;
}

enum {Opt_uid, Opt_gid, Opt_ownmask, Opt_othmask, Opt_ftsuffix, Opt_err};

static const match_table_t tokens = {
	{Opt_uid, "uid=%u"},
	{Opt_gid, "gid=%u"},
	{Opt_ownmask, "ownmask=%o"},
	{Opt_othmask, "othmask=%o"},
	{Opt_ftsuffix, "ftsuffix=%u"},
	{Opt_err, NULL}
};

static int parse_options(struct super_block *sb, char *options)
{
	char *p;
	struct adfs_sb_info *asb = ADFS_SB(sb);
	int option;

	if (!options)
		return 0;

	while ((p = strsep(&options, ",")) != NULL) {
		substring_t args[MAX_OPT_ARGS];
		int token;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_uid:
			if (match_int(args, &option))
				return -EINVAL;
			asb->s_uid = make_kuid(current_user_ns(), option);
			if (!uid_valid(asb->s_uid))
				return -EINVAL;
			break;
		case Opt_gid:
			if (match_int(args, &option))
				return -EINVAL;
			asb->s_gid = make_kgid(current_user_ns(), option);
			if (!gid_valid(asb->s_gid))
				return -EINVAL;
			break;
		case Opt_ownmask:
			if (match_octal(args, &option))
				return -EINVAL;
			asb->s_owner_mask = option;
			break;
		case Opt_othmask:
			if (match_octal(args, &option))
				return -EINVAL;
			asb->s_other_mask = option;
			break;
		case Opt_ftsuffix:
			if (match_int(args, &option))
				return -EINVAL;
			asb->s_ftsuffix = option;
			break;
		default:
			printk("ADFS-fs: unrecognised mount option \"%s\" "
					"or missing value\n", p);
			return -EINVAL;
		}
	}
	return 0;
}

static int adfs_remount(struct super_block *sb, int *flags, char *data)
{
	*flags |= MS_NODIRATIME;
	return parse_options(sb, data);
}

static int adfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct adfs_sb_info *sbi = ADFS_SB(sb);
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);

	buf->f_type    = ADFS_SUPER_MAGIC;
	buf->f_namelen = sbi->s_namelen;
	buf->f_bsize   = sb->s_blocksize;
	buf->f_blocks  = sbi->s_size;
	buf->f_files   = sbi->s_ids_per_zone * sbi->s_map_size;
	buf->f_bavail  =
	buf->f_bfree   = adfs_map_free(sb);
	buf->f_ffree   = (long)(buf->f_bfree * buf->f_files) / (long)buf->f_blocks;
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);

	return 0;
}

static struct kmem_cache *adfs_inode_cachep;

static struct inode *adfs_alloc_inode(struct super_block *sb)
{
	struct adfs_inode_info *ei;
	ei = (struct adfs_inode_info *)kmem_cache_alloc(adfs_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void adfs_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kmem_cache_free(adfs_inode_cachep, ADFS_I(inode));
}

static void adfs_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, adfs_i_callback);
}

static void init_once(void *foo)
{
	struct adfs_inode_info *ei = (struct adfs_inode_info *) foo;

	inode_init_once(&ei->vfs_inode);
}

static int init_inodecache(void)
{
	adfs_inode_cachep = kmem_cache_create("adfs_inode_cache",
					     sizeof(struct adfs_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     init_once);
	if (adfs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(adfs_inode_cachep);
}

static const struct super_operations adfs_sops = {
	.alloc_inode	= adfs_alloc_inode,
	.destroy_inode	= adfs_destroy_inode,
	.write_inode	= adfs_write_inode,
	.put_super	= adfs_put_super,
	.statfs		= adfs_statfs,
	.remount_fs	= adfs_remount,
	.show_options	= adfs_show_options,
};

static struct adfs_discmap *adfs_read_map(struct super_block *sb, struct adfs_discrecord *dr)
{
	struct adfs_discmap *dm;
	unsigned int map_addr, zone_size, nzones;
	int i, zone;
	struct adfs_sb_info *asb = ADFS_SB(sb);

	nzones    = asb->s_map_size;
	zone_size = (8 << dr->log2secsize) - le16_to_cpu(dr->zone_spare);
	map_addr  = (nzones >> 1) * zone_size -
		     ((nzones > 1) ? ADFS_DR_SIZE_BITS : 0);
	map_addr  = signed_asl(map_addr, asb->s_map2blk);

	asb->s_ids_per_zone = zone_size / (asb->s_idlen + 1);

	dm = kmalloc(nzones * sizeof(*dm), GFP_KERNEL);
	if (dm == NULL) {
		adfs_error(sb, "not enough memory");
		return NULL;
	}

	for (zone = 0; zone < nzones; zone++, map_addr++) {
		dm[zone].dm_startbit = 0;
		dm[zone].dm_endbit   = zone_size;
		dm[zone].dm_startblk = zone * zone_size - ADFS_DR_SIZE_BITS;
		dm[zone].dm_bh       = sb_bread(sb, map_addr);

		if (!dm[zone].dm_bh) {
			adfs_error(sb, "unable to read map");
			goto error_free;
		}
	}

	/* adjust the limits for the first and last map zones */
	i = zone - 1;
	dm[0].dm_startblk = 0;
	dm[0].dm_startbit = ADFS_DR_SIZE_BITS;
	dm[i].dm_endbit   = (le32_to_cpu(dr->disc_size_high) << (32 - dr->log2bpmb)) +
			    (le32_to_cpu(dr->disc_size) >> dr->log2bpmb) +
			    (ADFS_DR_SIZE_BITS - i * zone_size);

	if (adfs_checkmap(sb, dm))
		return dm;

	adfs_error(sb, "map corrupted");

error_free:
	while (--zone >= 0)
		brelse(dm[zone].dm_bh);

	kfree(dm);
	return NULL;
}

static inline unsigned long adfs_discsize(struct adfs_discrecord *dr, int block_bits)
{
	unsigned long discsize;

	discsize  = le32_to_cpu(dr->disc_size_high) << (32 - block_bits);
	discsize |= le32_to_cpu(dr->disc_size) >> block_bits;

	return discsize;
}

static int adfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct adfs_discrecord *dr;
	struct buffer_head *bh;
	struct object_info root_obj;
	unsigned char *b_data;
	struct adfs_sb_info *asb;
	struct inode *root;

	sb->s_flags |= MS_NODIRATIME;

	asb = kzalloc(sizeof(*asb), GFP_KERNEL);
	if (!asb)
		return -ENOMEM;
	sb->s_fs_info = asb;

	/* set default options */
	asb->s_uid = GLOBAL_ROOT_UID;
	asb->s_gid = GLOBAL_ROOT_GID;
	asb->s_owner_mask = ADFS_DEFAULT_OWNER_MASK;
	asb->s_other_mask = ADFS_DEFAULT_OTHER_MASK;
	asb->s_ftsuffix = 0;

	if (parse_options(sb, data))
		goto error;

	sb_set_blocksize(sb, BLOCK_SIZE);
	if (!(bh = sb_bread(sb, ADFS_DISCRECORD / BLOCK_SIZE))) {
		adfs_error(sb, "unable to read superblock");
		goto error;
	}

	b_data = bh->b_data + (ADFS_DISCRECORD % BLOCK_SIZE);

	if (adfs_checkbblk(b_data)) {
		if (!silent)
			printk("VFS: Can't find an adfs filesystem on dev "
				"%s.\n", sb->s_id);
		goto error_free_bh;
	}

	dr = (struct adfs_discrecord *)(b_data + ADFS_DR_OFFSET);

	/*
	 * Do some sanity checks on the ADFS disc record
	 */
	if (adfs_checkdiscrecord(dr)) {
		if (!silent)
			printk("VPS: Can't find an adfs filesystem on dev "
				"%s.\n", sb->s_id);
		goto error_free_bh;
	}

	brelse(bh);
	if (sb_set_blocksize(sb, 1 << dr->log2secsize)) {
		bh = sb_bread(sb, ADFS_DISCRECORD / sb->s_blocksize);
		if (!bh) {
			adfs_error(sb, "couldn't read superblock on "
				"2nd try.");
			goto error;
		}
		b_data = bh->b_data + (ADFS_DISCRECORD % sb->s_blocksize);
		if (adfs_checkbblk(b_data)) {
			adfs_error(sb, "disc record mismatch, very weird!");
			goto error_free_bh;
		}
		dr = (struct adfs_discrecord *)(b_data + ADFS_DR_OFFSET);
	} else {
		if (!silent)
			printk(KERN_ERR "VFS: Unsupported blocksize on dev "
				"%s.\n", sb->s_id);
		goto error;
	}

	/*
	 * blocksize on this device should now be set to the ADFS log2secsize
	 */

	sb->s_magic		= ADFS_SUPER_MAGIC;
	asb->s_idlen		= dr->idlen;
	asb->s_map_size		= dr->nzones | (dr->nzones_high << 8);
	asb->s_map2blk		= dr->log2bpmb - dr->log2secsize;
	asb->s_size    		= adfs_discsize(dr, sb->s_blocksize_bits);
	asb->s_version 		= dr->format_version;
	asb->s_log2sharesize	= dr->log2sharesize;
	
	asb->s_map = adfs_read_map(sb, dr);
	if (!asb->s_map)
		goto error_free_bh;

	brelse(bh);

	/*
	 * set up enough so that we can read an inode
	 */
	sb->s_op = &adfs_sops;

	dr = (struct adfs_discrecord *)(asb->s_map[0].dm_bh->b_data + 4);

	root_obj.parent_id = root_obj.file_id = le32_to_cpu(dr->root);
	root_obj.name_len  = 0;
	/* Set root object date as 01 Jan 1987 00:00:00 */
	root_obj.loadaddr  = 0xfff0003f;
	root_obj.execaddr  = 0xec22c000;
	root_obj.size	   = ADFS_NEWDIR_SIZE;
	root_obj.attr	   = ADFS_NDA_DIRECTORY   | ADFS_NDA_OWNER_READ |
			     ADFS_NDA_OWNER_WRITE | ADFS_NDA_PUBLIC_READ;
	root_obj.filetype  = -1;

	/*
	 * If this is a F+ disk with variable length directories,
	 * get the root_size from the disc record.
	 */
	if (asb->s_version) {
		root_obj.size = le32_to_cpu(dr->root_size);
		asb->s_dir     = &adfs_fplus_dir_ops;
		asb->s_namelen = ADFS_FPLUS_NAME_LEN;
	} else {
		asb->s_dir     = &adfs_f_dir_ops;
		asb->s_namelen = ADFS_F_NAME_LEN;
	}
	/*
	 * ,xyz hex filetype suffix may be added by driver
	 * to files that have valid RISC OS filetype
	 */
	if (asb->s_ftsuffix)
		asb->s_namelen += 4;

	sb->s_d_op = &adfs_dentry_operations;
	root = adfs_iget(sb, &root_obj);
	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		int i;
		for (i = 0; i < asb->s_map_size; i++)
			brelse(asb->s_map[i].dm_bh);
		kfree(asb->s_map);
		adfs_error(sb, "get root inode failed\n");
		goto error;
	}
	return 0;

error_free_bh:
	brelse(bh);
error:
	sb->s_fs_info = NULL;
	kfree(asb);
	return -EINVAL;
}

static struct dentry *adfs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, adfs_fill_super);
}

static struct file_system_type adfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "adfs",
	.mount		= adfs_mount,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("adfs");

static int __init init_adfs_fs(void)
{
	int err = init_inodecache();
	if (err)
		goto out1;
	err = register_filesystem(&adfs_fs_type);
	if (err)
		goto out;
	return 0;
out:
	destroy_inodecache();
out1:
	return err;
}

static void __exit exit_adfs_fs(void)
{
	unregister_filesystem(&adfs_fs_type);
	destroy_inodecache();
}

module_init(init_adfs_fs)
module_exit(exit_adfs_fs)
MODULE_LICENSE("GPL");
