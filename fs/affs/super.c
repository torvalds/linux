// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/affs/inode.c
 *
 *  (c) 1996  Hans-Joachim Widmaier - Rewritten
 *
 *  (C) 1993  Ray Burr - Modified for Amiga FFS filesystem.
 *
 *  (C) 1992  Eric Youngdale Modified for ISO 9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/statfs.h>
#include <linux/fs_parser.h>
#include <linux/fs_context.h>
#include <linux/magic.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/slab.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/seq_file.h>
#include <linux/iversion.h>
#include "affs.h"

static int affs_statfs(struct dentry *dentry, struct kstatfs *buf);
static int affs_show_options(struct seq_file *m, struct dentry *root);

static void
affs_commit_super(struct super_block *sb, int wait)
{
	struct affs_sb_info *sbi = AFFS_SB(sb);
	struct buffer_head *bh = sbi->s_root_bh;
	struct affs_root_tail *tail = AFFS_ROOT_TAIL(sb, bh);

	lock_buffer(bh);
	affs_secs_to_datestamp(ktime_get_real_seconds(), &tail->disk_change);
	affs_fix_checksum(sb, bh);
	unlock_buffer(bh);

	mark_buffer_dirty(bh);
	if (wait)
		sync_dirty_buffer(bh);
}

static void
affs_put_super(struct super_block *sb)
{
	struct affs_sb_info *sbi = AFFS_SB(sb);
	pr_debug("%s()\n", __func__);

	cancel_delayed_work_sync(&sbi->sb_work);
}

static int
affs_sync_fs(struct super_block *sb, int wait)
{
	affs_commit_super(sb, wait);
	return 0;
}

static void flush_superblock(struct work_struct *work)
{
	struct affs_sb_info *sbi;
	struct super_block *sb;

	sbi = container_of(work, struct affs_sb_info, sb_work.work);
	sb = sbi->sb;

	spin_lock(&sbi->work_lock);
	sbi->work_queued = 0;
	spin_unlock(&sbi->work_lock);

	affs_commit_super(sb, 1);
}

void affs_mark_sb_dirty(struct super_block *sb)
{
	struct affs_sb_info *sbi = AFFS_SB(sb);
	unsigned long delay;

	if (sb_rdonly(sb))
	       return;

	spin_lock(&sbi->work_lock);
	if (!sbi->work_queued) {
	       delay = msecs_to_jiffies(dirty_writeback_interval * 10);
	       queue_delayed_work(system_long_wq, &sbi->sb_work, delay);
	       sbi->work_queued = 1;
	}
	spin_unlock(&sbi->work_lock);
}

static struct kmem_cache * affs_inode_cachep;

static struct inode *affs_alloc_inode(struct super_block *sb)
{
	struct affs_inode_info *i;

	i = alloc_inode_sb(sb, affs_inode_cachep, GFP_KERNEL);
	if (!i)
		return NULL;

	inode_set_iversion(&i->vfs_inode, 1);
	i->i_lc = NULL;
	i->i_ext_bh = NULL;
	i->i_pa_cnt = 0;

	return &i->vfs_inode;
}

static void affs_free_inode(struct inode *inode)
{
	kmem_cache_free(affs_inode_cachep, AFFS_I(inode));
}

static void init_once(void *foo)
{
	struct affs_inode_info *ei = (struct affs_inode_info *) foo;

	mutex_init(&ei->i_link_lock);
	mutex_init(&ei->i_ext_lock);
	inode_init_once(&ei->vfs_inode);
}

static int __init init_inodecache(void)
{
	affs_inode_cachep = kmem_cache_create("affs_inode_cache",
					     sizeof(struct affs_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT | SLAB_ACCOUNT),
					     init_once);
	if (affs_inode_cachep == NULL)
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
	kmem_cache_destroy(affs_inode_cachep);
}

static const struct super_operations affs_sops = {
	.alloc_inode	= affs_alloc_inode,
	.free_inode	= affs_free_inode,
	.write_inode	= affs_write_inode,
	.evict_inode	= affs_evict_inode,
	.put_super	= affs_put_super,
	.sync_fs	= affs_sync_fs,
	.statfs		= affs_statfs,
	.show_options	= affs_show_options,
};

enum {
	Opt_bs, Opt_mode, Opt_mufs, Opt_notruncate, Opt_prefix, Opt_protect,
	Opt_reserved, Opt_root, Opt_setgid, Opt_setuid,
	Opt_verbose, Opt_volume, Opt_ignore,
};

struct affs_context {
	kuid_t		uid;		/* uid to override */
	kgid_t		gid;		/* gid to override */
	unsigned int	mode;		/* mode to override */
	unsigned int	reserved;	/* Number of reserved blocks */
	int		root_block;	/* FFS root block number */
	int		blocksize;	/* Initial device blksize */
	char		*prefix;	/* Prefix for volumes and assigns */
	char		volume[32];	/* Vol. prefix for absolute symlinks */
	unsigned long	mount_flags;	/* Options */
};

static const struct fs_parameter_spec affs_param_spec[] = {
	fsparam_u32	("bs",		Opt_bs),
	fsparam_u32oct	("mode",	Opt_mode),
	fsparam_flag	("mufs",	Opt_mufs),
	fsparam_flag	("nofilenametruncate",	Opt_notruncate),
	fsparam_string	("prefix",	Opt_prefix),
	fsparam_flag	("protect",	Opt_protect),
	fsparam_u32	("reserved",	Opt_reserved),
	fsparam_u32	("root",	Opt_root),
	fsparam_gid	("setgid",	Opt_setgid),
	fsparam_uid	("setuid",	Opt_setuid),
	fsparam_flag	("verbose",	Opt_verbose),
	fsparam_string	("volume",	Opt_volume),
	fsparam_flag	("grpquota",	Opt_ignore),
	fsparam_flag	("noquota",	Opt_ignore),
	fsparam_flag	("quota",	Opt_ignore),
	fsparam_flag	("usrquota",	Opt_ignore),
	{},
};

static int affs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct affs_context *ctx = fc->fs_private;
	struct fs_parse_result result;
	int n;
	int opt;

	opt = fs_parse(fc, affs_param_spec, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_bs:
		n = result.uint_32;
		if (n != 512 && n != 1024 && n != 2048
		    && n != 4096) {
			pr_warn("Invalid blocksize (512, 1024, 2048, 4096 allowed)\n");
			return -EINVAL;
		}
		ctx->blocksize = n;
		break;
	case Opt_mode:
		ctx->mode = result.uint_32 & 0777;
		affs_set_opt(ctx->mount_flags, SF_SETMODE);
		break;
	case Opt_mufs:
		affs_set_opt(ctx->mount_flags, SF_MUFS);
		break;
	case Opt_notruncate:
		affs_set_opt(ctx->mount_flags, SF_NO_TRUNCATE);
		break;
	case Opt_prefix:
		kfree(ctx->prefix);
		ctx->prefix = param->string;
		param->string = NULL;
		affs_set_opt(ctx->mount_flags, SF_PREFIX);
		break;
	case Opt_protect:
		affs_set_opt(ctx->mount_flags, SF_IMMUTABLE);
		break;
	case Opt_reserved:
		ctx->reserved = result.uint_32;
		break;
	case Opt_root:
		ctx->root_block = result.uint_32;
		break;
	case Opt_setgid:
		ctx->gid = result.gid;
		affs_set_opt(ctx->mount_flags, SF_SETGID);
		break;
	case Opt_setuid:
		ctx->uid = result.uid;
		affs_set_opt(ctx->mount_flags, SF_SETUID);
		break;
	case Opt_verbose:
		affs_set_opt(ctx->mount_flags, SF_VERBOSE);
		break;
	case Opt_volume:
		strscpy(ctx->volume, param->string, 32);
		break;
	case Opt_ignore:
		/* Silently ignore the quota options */
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int affs_show_options(struct seq_file *m, struct dentry *root)
{
	struct super_block *sb = root->d_sb;
	struct affs_sb_info *sbi = AFFS_SB(sb);

	if (sb->s_blocksize)
		seq_printf(m, ",bs=%lu", sb->s_blocksize);
	if (affs_test_opt(sbi->s_flags, SF_SETMODE))
		seq_printf(m, ",mode=%o", sbi->s_mode);
	if (affs_test_opt(sbi->s_flags, SF_MUFS))
		seq_puts(m, ",mufs");
	if (affs_test_opt(sbi->s_flags, SF_NO_TRUNCATE))
		seq_puts(m, ",nofilenametruncate");
	if (affs_test_opt(sbi->s_flags, SF_PREFIX))
		seq_printf(m, ",prefix=%s", sbi->s_prefix);
	if (affs_test_opt(sbi->s_flags, SF_IMMUTABLE))
		seq_puts(m, ",protect");
	if (sbi->s_reserved != 2)
		seq_printf(m, ",reserved=%u", sbi->s_reserved);
	if (sbi->s_root_block != (sbi->s_reserved + sbi->s_partition_size - 1) / 2)
		seq_printf(m, ",root=%u", sbi->s_root_block);
	if (affs_test_opt(sbi->s_flags, SF_SETGID))
		seq_printf(m, ",setgid=%u",
			   from_kgid_munged(&init_user_ns, sbi->s_gid));
	if (affs_test_opt(sbi->s_flags, SF_SETUID))
		seq_printf(m, ",setuid=%u",
			   from_kuid_munged(&init_user_ns, sbi->s_uid));
	if (affs_test_opt(sbi->s_flags, SF_VERBOSE))
		seq_puts(m, ",verbose");
	if (sbi->s_volume[0])
		seq_printf(m, ",volume=%s", sbi->s_volume);
	return 0;
}

/* This function definitely needs to be split up. Some fine day I'll
 * hopefully have the guts to do so. Until then: sorry for the mess.
 */

static int affs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct affs_sb_info	*sbi;
	struct affs_context	*ctx = fc->fs_private;
	struct buffer_head	*root_bh = NULL;
	struct buffer_head	*boot_bh;
	struct inode		*root_inode = NULL;
	int			 silent = fc->sb_flags & SB_SILENT;
	int			 size, blocksize;
	u32			 chksum;
	int			 num_bm;
	int			 i, j;
	int			 tmp_flags;	/* fix remount prototype... */
	u8			 sig[4];
	int			 ret;

	sb->s_magic             = AFFS_SUPER_MAGIC;
	sb->s_op                = &affs_sops;
	sb->s_flags |= SB_NODIRATIME;

	sb->s_time_gran = NSEC_PER_SEC;
	sb->s_time_min = sys_tz.tz_minuteswest * 60 + AFFS_EPOCH_DELTA;
	sb->s_time_max = 86400LL * U32_MAX + 86400 + sb->s_time_min;

	sbi = kzalloc(sizeof(struct affs_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;

	sb->s_fs_info = sbi;
	sbi->sb = sb;
	mutex_init(&sbi->s_bmlock);
	spin_lock_init(&sbi->symlink_lock);
	spin_lock_init(&sbi->work_lock);
	INIT_DELAYED_WORK(&sbi->sb_work, flush_superblock);

	sbi->s_flags	= ctx->mount_flags;
	sbi->s_mode	= ctx->mode;
	sbi->s_uid	= ctx->uid;
	sbi->s_gid	= ctx->gid;
	sbi->s_reserved	= ctx->reserved;
	sbi->s_prefix	= ctx->prefix;
	ctx->prefix	= NULL;
	memcpy(sbi->s_volume, ctx->volume, 32);

	/* N.B. after this point s_prefix must be released */

	/* Get the size of the device in 512-byte blocks.
	 * If we later see that the partition uses bigger
	 * blocks, we will have to change it.
	 */

	size = bdev_nr_sectors(sb->s_bdev);
	pr_debug("initial blocksize=%d, #blocks=%d\n", 512, size);

	affs_set_blocksize(sb, PAGE_SIZE);
	/* Try to find root block. Its location depends on the block size. */

	i = bdev_logical_block_size(sb->s_bdev);
	j = PAGE_SIZE;
	blocksize = ctx->blocksize;
	if (blocksize > 0) {
		i = j = blocksize;
		size = size / (blocksize / 512);
	}

	for (blocksize = i; blocksize <= j; blocksize <<= 1, size >>= 1) {
		sbi->s_root_block = ctx->root_block;
		if (ctx->root_block < 0)
			sbi->s_root_block = (ctx->reserved + size - 1) / 2;
		pr_debug("setting blocksize to %d\n", blocksize);
		affs_set_blocksize(sb, blocksize);
		sbi->s_partition_size = size;

		/* The root block location that was calculated above is not
		 * correct if the partition size is an odd number of 512-
		 * byte blocks, which will be rounded down to a number of
		 * 1024-byte blocks, and if there were an even number of
		 * reserved blocks. Ideally, all partition checkers should
		 * report the real number of blocks of the real blocksize,
		 * but since this just cannot be done, we have to try to
		 * find the root block anyways. In the above case, it is one
		 * block behind the calculated one. So we check this one, too.
		 */
		for (num_bm = 0; num_bm < 2; num_bm++) {
			pr_debug("Dev %s, trying root=%u, bs=%d, "
				"size=%d, reserved=%d\n",
				sb->s_id,
				sbi->s_root_block + num_bm,
				ctx->blocksize, size, ctx->reserved);
			root_bh = affs_bread(sb, sbi->s_root_block + num_bm);
			if (!root_bh)
				continue;
			if (!affs_checksum_block(sb, root_bh) &&
			    be32_to_cpu(AFFS_ROOT_HEAD(root_bh)->ptype) == T_SHORT &&
			    be32_to_cpu(AFFS_ROOT_TAIL(sb, root_bh)->stype) == ST_ROOT) {
				sbi->s_hashsize    = blocksize / 4 - 56;
				sbi->s_root_block += num_bm;
				goto got_root;
			}
			affs_brelse(root_bh);
			root_bh = NULL;
		}
	}
	if (!silent)
		pr_err("No valid root block on device %s\n", sb->s_id);
	return -EINVAL;

	/* N.B. after this point bh must be released */
got_root:
	/* Keep super block in cache */
	sbi->s_root_bh = root_bh;
	ctx->root_block = sbi->s_root_block;

	/* Find out which kind of FS we have */
	boot_bh = sb_bread(sb, 0);
	if (!boot_bh) {
		pr_err("Cannot read boot block\n");
		return -EINVAL;
	}
	memcpy(sig, boot_bh->b_data, 4);
	brelse(boot_bh);
	chksum = be32_to_cpu(*(__be32 *)sig);

	/* Dircache filesystems are compatible with non-dircache ones
	 * when reading. As long as they aren't supported, writing is
	 * not recommended.
	 */
	if ((chksum == FS_DCFFS || chksum == MUFS_DCFFS || chksum == FS_DCOFS
	     || chksum == MUFS_DCOFS) && !sb_rdonly(sb)) {
		pr_notice("Dircache FS - mounting %s read only\n", sb->s_id);
		sb->s_flags |= SB_RDONLY;
	}
	switch (chksum) {
	case MUFS_FS:
	case MUFS_INTLFFS:
	case MUFS_DCFFS:
		affs_set_opt(sbi->s_flags, SF_MUFS);
		fallthrough;
	case FS_INTLFFS:
	case FS_DCFFS:
		affs_set_opt(sbi->s_flags, SF_INTL);
		break;
	case MUFS_FFS:
		affs_set_opt(sbi->s_flags, SF_MUFS);
		break;
	case FS_FFS:
		break;
	case MUFS_OFS:
		affs_set_opt(sbi->s_flags, SF_MUFS);
		fallthrough;
	case FS_OFS:
		affs_set_opt(sbi->s_flags, SF_OFS);
		sb->s_flags |= SB_NOEXEC;
		break;
	case MUFS_DCOFS:
	case MUFS_INTLOFS:
		affs_set_opt(sbi->s_flags, SF_MUFS);
		fallthrough;
	case FS_DCOFS:
	case FS_INTLOFS:
		affs_set_opt(sbi->s_flags, SF_INTL);
		affs_set_opt(sbi->s_flags, SF_OFS);
		sb->s_flags |= SB_NOEXEC;
		break;
	default:
		pr_err("Unknown filesystem on device %s: %08X\n",
		       sb->s_id, chksum);
		return -EINVAL;
	}

	if (affs_test_opt(ctx->mount_flags, SF_VERBOSE)) {
		u8 len = AFFS_ROOT_TAIL(sb, root_bh)->disk_name[0];
		pr_notice("Mounting volume \"%.*s\": Type=%.3s\\%c, Blocksize=%d\n",
			len > 31 ? 31 : len,
			AFFS_ROOT_TAIL(sb, root_bh)->disk_name + 1,
			sig, sig[3] + '0', blocksize);
	}

	sb->s_flags |= SB_NODEV | SB_NOSUID;

	sbi->s_data_blksize = sb->s_blocksize;
	if (affs_test_opt(sbi->s_flags, SF_OFS))
		sbi->s_data_blksize -= 24;

	tmp_flags = sb->s_flags;
	ret = affs_init_bitmap(sb, &tmp_flags);
	if (ret)
		return ret;
	sb->s_flags = tmp_flags;

	/* set up enough so that it can read an inode */

	root_inode = affs_iget(sb, ctx->root_block);
	if (IS_ERR(root_inode))
		return PTR_ERR(root_inode);

	if (affs_test_opt(AFFS_SB(sb)->s_flags, SF_INTL))
		sb->s_d_op = &affs_intl_dentry_operations;
	else
		sb->s_d_op = &affs_dentry_operations;

	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root) {
		pr_err("AFFS: Get root inode failed\n");
		return -ENOMEM;
	}

	sb->s_export_op = &affs_export_ops;
	pr_debug("s_flags=%lX\n", sb->s_flags);
	return 0;
}

static int affs_reconfigure(struct fs_context *fc)
{
	struct super_block	*sb = fc->root->d_sb;
	struct affs_context	*ctx = fc->fs_private;
	struct affs_sb_info	*sbi = AFFS_SB(sb);
	int			 res = 0;

	sync_filesystem(sb);
	fc->sb_flags |= SB_NODIRATIME;

	flush_delayed_work(&sbi->sb_work);

	/*
	 * NB: Historically, only mount_flags, mode, uid, gic, prefix,
	 * and volume are accepted during remount.
	 */
	sbi->s_flags = ctx->mount_flags;
	sbi->s_mode  = ctx->mode;
	sbi->s_uid   = ctx->uid;
	sbi->s_gid   = ctx->gid;
	/* protect against readers */
	spin_lock(&sbi->symlink_lock);
	if (ctx->prefix) {
		kfree(sbi->s_prefix);
		sbi->s_prefix = ctx->prefix;
		ctx->prefix = NULL;
	}
	memcpy(sbi->s_volume, ctx->volume, 32);
	spin_unlock(&sbi->symlink_lock);

	if ((bool)(fc->sb_flags & SB_RDONLY) == sb_rdonly(sb))
		return 0;

	if (fc->sb_flags & SB_RDONLY)
		affs_free_bitmap(sb);
	else
		res = affs_init_bitmap(sb, &fc->sb_flags);

	return res;
}

static int
affs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	int		 free;
	u64		 id = huge_encode_dev(sb->s_bdev->bd_dev);

	pr_debug("%s() partsize=%d, reserved=%d\n",
		 __func__, AFFS_SB(sb)->s_partition_size,
		 AFFS_SB(sb)->s_reserved);

	free          = affs_count_free_blocks(sb);
	buf->f_type    = AFFS_SUPER_MAGIC;
	buf->f_bsize   = sb->s_blocksize;
	buf->f_blocks  = AFFS_SB(sb)->s_partition_size - AFFS_SB(sb)->s_reserved;
	buf->f_bfree   = free;
	buf->f_bavail  = free;
	buf->f_fsid    = u64_to_fsid(id);
	buf->f_namelen = AFFSNAMEMAX;
	return 0;
}

static int affs_get_tree(struct fs_context *fc)
{
	return get_tree_bdev(fc, affs_fill_super);
}

static void affs_kill_sb(struct super_block *sb)
{
	struct affs_sb_info *sbi = AFFS_SB(sb);
	kill_block_super(sb);
	if (sbi) {
		affs_free_bitmap(sb);
		affs_brelse(sbi->s_root_bh);
		kfree(sbi->s_prefix);
		mutex_destroy(&sbi->s_bmlock);
		kfree_rcu(sbi, rcu);
	}
}

static void affs_free_fc(struct fs_context *fc)
{
	struct affs_context *ctx = fc->fs_private;

	kfree(ctx->prefix);
	kfree(ctx);
}

static const struct fs_context_operations affs_context_ops = {
	.parse_param	= affs_parse_param,
	.get_tree	= affs_get_tree,
	.reconfigure	= affs_reconfigure,
	.free		= affs_free_fc,
};

static int affs_init_fs_context(struct fs_context *fc)
{
	struct affs_context *ctx;

	ctx = kzalloc(sizeof(struct affs_context), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (fc->purpose == FS_CONTEXT_FOR_RECONFIGURE) {
		struct super_block *sb = fc->root->d_sb;
		struct affs_sb_info *sbi = AFFS_SB(sb);

		/*
		 * NB: historically, no options other than volume were
		 * preserved across a remount unless they were explicitly
		 * passed in.
		 */
		memcpy(ctx->volume, sbi->s_volume, 32);
	} else {
		ctx->uid	= current_uid();
		ctx->gid	= current_gid();
		ctx->reserved	= 2;
		ctx->root_block	= -1;
		ctx->blocksize	= -1;
		ctx->volume[0]	= ':';
	}

	fc->ops = &affs_context_ops;
	fc->fs_private = ctx;

	return 0;
}

static struct file_system_type affs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "affs",
	.kill_sb	= affs_kill_sb,
	.fs_flags	= FS_REQUIRES_DEV,
	.init_fs_context = affs_init_fs_context,
	.parameters	= affs_param_spec,
};
MODULE_ALIAS_FS("affs");

static int __init init_affs_fs(void)
{
	int err = init_inodecache();
	if (err)
		goto out1;
	err = register_filesystem(&affs_fs_type);
	if (err)
		goto out;
	return 0;
out:
	destroy_inodecache();
out1:
	return err;
}

static void __exit exit_affs_fs(void)
{
	unregister_filesystem(&affs_fs_type);
	destroy_inodecache();
}

MODULE_DESCRIPTION("Amiga filesystem support for Linux");
MODULE_LICENSE("GPL");

module_init(init_affs_fs)
module_exit(exit_affs_fs)
