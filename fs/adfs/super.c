// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/adfs/super.c
 *
 *  Copyright (C) 1997-1999 Russell King
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs_parser.h>
#include <linux/fs_context.h>
#include <linux/mount.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/user_namespace.h>
#include <linux/blkdev.h>
#include "adfs.h"
#include "dir_f.h"
#include "dir_fplus.h"

#define ADFS_SB_FLAGS SB_NOATIME

#define ADFS_DEFAULT_OWNER_MASK S_IRWXU
#define ADFS_DEFAULT_OTHER_MASK (S_IRWXG | S_IRWXO)

void __adfs_error(struct super_block *sb, const char *function, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;

	printk(KERN_CRIT "ADFS-fs error (device %s)%s%s: %pV\n",
		sb->s_id, function ? ": " : "",
		function ? function : "", &vaf);

	va_end(args);
}

void adfs_msg(struct super_block *sb, const char *pfx, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	printk("%sADFS-fs (%s): %pV\n", pfx, sb->s_id, &vaf);
	va_end(args);
}

static int adfs_checkdiscrecord(struct adfs_discrecord *dr)
{
	unsigned int max_idlen;
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

	/*
	 * Maximum idlen is limited to 16 bits for new directories by
	 * the three-byte storage of an indirect disc address.  For
	 * big directories, idlen must be no greater than 19 v2 [1.0]
	 */
	max_idlen = dr->format_version ? 19 : 16;
	if (dr->idlen > max_idlen)
		return 1;

	/* reserved bytes should be zero */
	for (i = 0; i < sizeof(dr->unused52); i++)
		if (dr->unused52[i] != 0)
			return 1;

	return 0;
}

static void adfs_put_super(struct super_block *sb)
{
	struct adfs_sb_info *asb = ADFS_SB(sb);

	adfs_free_map(sb);
	kfree_rcu(asb, rcu);
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

enum {Opt_uid, Opt_gid, Opt_ownmask, Opt_othmask, Opt_ftsuffix};

static const struct fs_parameter_spec adfs_param_spec[] = {
	fsparam_uid	("uid",		Opt_uid),
	fsparam_gid	("gid",		Opt_gid),
	fsparam_u32oct	("ownmask",	Opt_ownmask),
	fsparam_u32oct	("othmask",	Opt_othmask),
	fsparam_u32	("ftsuffix",	Opt_ftsuffix),
	{}
};

static int adfs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct adfs_sb_info *asb = fc->s_fs_info;
	struct fs_parse_result result;
	int opt;

	opt = fs_parse(fc, adfs_param_spec, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_uid:
		asb->s_uid = result.uid;
		break;
	case Opt_gid:
		asb->s_gid = result.gid;
		break;
	case Opt_ownmask:
		asb->s_owner_mask = result.uint_32;
		break;
	case Opt_othmask:
		asb->s_other_mask = result.uint_32;
		break;
	case Opt_ftsuffix:
		asb->s_ftsuffix = result.uint_32;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int adfs_reconfigure(struct fs_context *fc)
{
	struct adfs_sb_info *new_asb = fc->s_fs_info;
	struct adfs_sb_info *asb = ADFS_SB(fc->root->d_sb);

	sync_filesystem(fc->root->d_sb);
	fc->sb_flags |= ADFS_SB_FLAGS;

	/* Structure copy newly parsed options */
	*asb = *new_asb;

	return 0;
}

static int adfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct adfs_sb_info *sbi = ADFS_SB(sb);
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);

	adfs_map_statfs(sb, buf);

	buf->f_type    = ADFS_SUPER_MAGIC;
	buf->f_namelen = sbi->s_namelen;
	buf->f_bsize   = sb->s_blocksize;
	buf->f_ffree   = (long)(buf->f_bfree * buf->f_files) / (long)buf->f_blocks;
	buf->f_fsid    = u64_to_fsid(id);

	return 0;
}

static struct kmem_cache *adfs_inode_cachep;

static struct inode *adfs_alloc_inode(struct super_block *sb)
{
	struct adfs_inode_info *ei;
	ei = alloc_inode_sb(sb, adfs_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void adfs_free_inode(struct inode *inode)
{
	kmem_cache_free(adfs_inode_cachep, ADFS_I(inode));
}

static int adfs_drop_inode(struct inode *inode)
{
	/* always drop inodes if we are read-only */
	return !IS_ENABLED(CONFIG_ADFS_FS_RW) || IS_RDONLY(inode);
}

static void init_once(void *foo)
{
	struct adfs_inode_info *ei = (struct adfs_inode_info *) foo;

	inode_init_once(&ei->vfs_inode);
}

static int __init init_inodecache(void)
{
	adfs_inode_cachep = kmem_cache_create("adfs_inode_cache",
					     sizeof(struct adfs_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_ACCOUNT),
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
	.free_inode	= adfs_free_inode,
	.drop_inode	= adfs_drop_inode,
	.write_inode	= adfs_write_inode,
	.put_super	= adfs_put_super,
	.statfs		= adfs_statfs,
	.show_options	= adfs_show_options,
};

static int adfs_probe(struct super_block *sb, unsigned int offset, int silent,
		      int (*validate)(struct super_block *sb,
				      struct buffer_head *bh,
				      struct adfs_discrecord **bhp))
{
	struct adfs_sb_info *asb = ADFS_SB(sb);
	struct adfs_discrecord *dr;
	struct buffer_head *bh;
	unsigned int blocksize = BLOCK_SIZE;
	int ret, try;

	for (try = 0; try < 2; try++) {
		/* try to set the requested block size */
		if (sb->s_blocksize != blocksize &&
		    !sb_set_blocksize(sb, blocksize)) {
			if (!silent)
				adfs_msg(sb, KERN_ERR,
					 "error: unsupported blocksize");
			return -EINVAL;
		}

		/* read the buffer */
		bh = sb_bread(sb, offset >> sb->s_blocksize_bits);
		if (!bh) {
			adfs_msg(sb, KERN_ERR,
				 "error: unable to read block %u, try %d",
				 offset >> sb->s_blocksize_bits, try);
			return -EIO;
		}

		/* validate it */
		ret = validate(sb, bh, &dr);
		if (ret) {
			brelse(bh);
			return ret;
		}

		/* does the block size match the filesystem block size? */
		blocksize = 1 << dr->log2secsize;
		if (sb->s_blocksize == blocksize) {
			asb->s_map = adfs_read_map(sb, dr);
			brelse(bh);
			return PTR_ERR_OR_ZERO(asb->s_map);
		}

		brelse(bh);
	}

	return -EIO;
}

static int adfs_validate_bblk(struct super_block *sb, struct buffer_head *bh,
			      struct adfs_discrecord **drp)
{
	struct adfs_discrecord *dr;
	unsigned char *b_data;

	b_data = bh->b_data + (ADFS_DISCRECORD % sb->s_blocksize);
	if (adfs_checkbblk(b_data))
		return -EILSEQ;

	/* Do some sanity checks on the ADFS disc record */
	dr = (struct adfs_discrecord *)(b_data + ADFS_DR_OFFSET);
	if (adfs_checkdiscrecord(dr))
		return -EILSEQ;

	*drp = dr;
	return 0;
}

static int adfs_validate_dr0(struct super_block *sb, struct buffer_head *bh,
			      struct adfs_discrecord **drp)
{
	struct adfs_discrecord *dr;

	/* Do some sanity checks on the ADFS disc record */
	dr = (struct adfs_discrecord *)(bh->b_data + 4);
	if (adfs_checkdiscrecord(dr) || dr->nzones_high || dr->nzones != 1)
		return -EILSEQ;

	*drp = dr;
	return 0;
}

static int adfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct adfs_discrecord *dr;
	struct object_info root_obj;
	struct adfs_sb_info *asb = sb->s_fs_info;
	struct inode *root;
	int ret = -EINVAL;
	int silent = fc->sb_flags & SB_SILENT;

	sb->s_flags |= ADFS_SB_FLAGS;

	sb->s_fs_info = asb;
	sb->s_magic = ADFS_SUPER_MAGIC;
	sb->s_time_gran = 10000000;

	/* Try to probe the filesystem boot block */
	ret = adfs_probe(sb, ADFS_DISCRECORD, 1, adfs_validate_bblk);
	if (ret == -EILSEQ)
		ret = adfs_probe(sb, 0, silent, adfs_validate_dr0);
	if (ret == -EILSEQ) {
		if (!silent)
			adfs_msg(sb, KERN_ERR,
				 "error: can't find an ADFS filesystem on dev %s.",
				 sb->s_id);
		ret = -EINVAL;
	}
	if (ret)
		goto error;

	/* set up enough so that we can read an inode */
	sb->s_op = &adfs_sops;

	dr = adfs_map_discrecord(asb->s_map);

	root_obj.parent_id = root_obj.indaddr = le32_to_cpu(dr->root);
	root_obj.name_len  = 0;
	/* Set root object date as 01 Jan 1987 00:00:00 */
	root_obj.loadaddr  = 0xfff0003f;
	root_obj.execaddr  = 0xec22c000;
	root_obj.size	   = ADFS_NEWDIR_SIZE;
	root_obj.attr	   = ADFS_NDA_DIRECTORY   | ADFS_NDA_OWNER_READ |
			     ADFS_NDA_OWNER_WRITE | ADFS_NDA_PUBLIC_READ;

	/*
	 * If this is a F+ disk with variable length directories,
	 * get the root_size from the disc record.
	 */
	if (dr->format_version) {
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

	set_default_d_op(sb, &adfs_dentry_operations);
	root = adfs_iget(sb, &root_obj);
	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		adfs_free_map(sb);
		adfs_error(sb, "get root inode failed\n");
		ret = -EIO;
		goto error;
	}
	return 0;

error:
	sb->s_fs_info = NULL;
	kfree(asb);
	return ret;
}

static int adfs_get_tree(struct fs_context *fc)
{
	return get_tree_bdev(fc, adfs_fill_super);
}

static void adfs_free_fc(struct fs_context *fc)
{
	struct adfs_context *asb = fc->s_fs_info;

	kfree(asb);
}

static const struct fs_context_operations adfs_context_ops = {
	.parse_param	= adfs_parse_param,
	.get_tree	= adfs_get_tree,
	.reconfigure	= adfs_reconfigure,
	.free		= adfs_free_fc,
};

static int adfs_init_fs_context(struct fs_context *fc)
{
	struct adfs_sb_info *asb;

	asb = kzalloc(sizeof(struct adfs_sb_info), GFP_KERNEL);
	if (!asb)
		return -ENOMEM;

	if (fc->purpose == FS_CONTEXT_FOR_RECONFIGURE) {
		struct super_block *sb = fc->root->d_sb;
		struct adfs_sb_info *old_asb = ADFS_SB(sb);

		/* structure copy existing options before parsing */
		*asb = *old_asb;
	} else {
		/* set default options */
		asb->s_uid = GLOBAL_ROOT_UID;
		asb->s_gid = GLOBAL_ROOT_GID;
		asb->s_owner_mask = ADFS_DEFAULT_OWNER_MASK;
		asb->s_other_mask = ADFS_DEFAULT_OTHER_MASK;
		asb->s_ftsuffix = 0;
	}

	fc->ops = &adfs_context_ops;
	fc->s_fs_info = asb;

	return 0;
}

static struct file_system_type adfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "adfs",
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
	.init_fs_context = adfs_init_fs_context,
	.parameters	= adfs_param_spec,
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
MODULE_DESCRIPTION("Acorn Disc Filing System");
MODULE_LICENSE("GPL");
