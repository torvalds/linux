// SPDX-License-Identifier: GPL-2.0
/*
 * linux/drivers/staging/erofs/super.c
 *
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             http://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of the Linux
 * distribution for more details.
 */
#include <linux/module.h>
#include <linux/buffer_head.h>
#include <linux/statfs.h>
#include <linux/parser.h>
#include <linux/seq_file.h>
#include "internal.h"

#define CREATE_TRACE_POINTS
#include <trace/events/erofs.h>

static struct kmem_cache *erofs_inode_cachep __read_mostly;

static void init_once(void *ptr)
{
	struct erofs_vnode *vi = ptr;

	inode_init_once(&vi->vfs_inode);
}

static int erofs_init_inode_cache(void)
{
	erofs_inode_cachep = kmem_cache_create("erofs_inode",
		sizeof(struct erofs_vnode), 0,
		SLAB_RECLAIM_ACCOUNT, init_once);

	return erofs_inode_cachep != NULL ? 0 : -ENOMEM;
}

static void erofs_exit_inode_cache(void)
{
	BUG_ON(erofs_inode_cachep == NULL);
	kmem_cache_destroy(erofs_inode_cachep);
}

static struct inode *alloc_inode(struct super_block *sb)
{
	struct erofs_vnode *vi =
		kmem_cache_alloc(erofs_inode_cachep, GFP_KERNEL);

	if (vi == NULL)
		return NULL;

	/* zero out everything except vfs_inode */
	memset(vi, 0, offsetof(struct erofs_vnode, vfs_inode));
	return &vi->vfs_inode;
}

static void i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	struct erofs_vnode *vi = EROFS_V(inode);

	/* be careful RCU symlink path (see ext4_inode_info->i_data)! */
	if (is_inode_fast_symlink(inode))
		kfree(inode->i_link);

	kfree(vi->xattr_shared_xattrs);

	kmem_cache_free(erofs_inode_cachep, vi);
}

static void destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, i_callback);
}

static int superblock_read(struct super_block *sb)
{
	struct erofs_sb_info *sbi;
	struct buffer_head *bh;
	struct erofs_super_block *layout;
	unsigned blkszbits;
	int ret;

	bh = sb_bread(sb, 0);

	if (bh == NULL) {
		errln("cannot read erofs superblock");
		return -EIO;
	}

	sbi = EROFS_SB(sb);
	layout = (struct erofs_super_block *)((u8 *)bh->b_data
		 + EROFS_SUPER_OFFSET);

	ret = -EINVAL;
	if (le32_to_cpu(layout->magic) != EROFS_SUPER_MAGIC_V1) {
		errln("cannot find valid erofs superblock");
		goto out;
	}

	blkszbits = layout->blkszbits;
	/* 9(512 bytes) + LOG_SECTORS_PER_BLOCK == LOG_BLOCK_SIZE */
	if (unlikely(blkszbits != LOG_BLOCK_SIZE)) {
		errln("blksize %u isn't supported on this platform",
			1 << blkszbits);
		goto out;
	}

	sbi->blocks = le32_to_cpu(layout->blocks);
	sbi->meta_blkaddr = le32_to_cpu(layout->meta_blkaddr);
#ifdef CONFIG_EROFS_FS_XATTR
	sbi->xattr_blkaddr = le32_to_cpu(layout->xattr_blkaddr);
#endif
	sbi->islotbits = ffs(sizeof(struct erofs_inode_v1)) - 1;
#ifdef CONFIG_EROFS_FS_ZIP
	sbi->clusterbits = 12;

	if (1 << (sbi->clusterbits - 12) > Z_EROFS_CLUSTER_MAX_PAGES)
		errln("clusterbits %u is not supported on this kernel",
			sbi->clusterbits);
#endif

	sbi->root_nid = le16_to_cpu(layout->root_nid);
	sbi->inos = le64_to_cpu(layout->inos);

	sbi->build_time = le64_to_cpu(layout->build_time);
	sbi->build_time_nsec = le32_to_cpu(layout->build_time_nsec);

	memcpy(&sb->s_uuid, layout->uuid, sizeof(layout->uuid));
	memcpy(sbi->volume_name, layout->volume_name,
		sizeof(layout->volume_name));

	ret = 0;
out:
	brelse(bh);
	return ret;
}

#ifdef CONFIG_EROFS_FAULT_INJECTION
char *erofs_fault_name[FAULT_MAX] = {
	[FAULT_KMALLOC]		= "kmalloc",
};

static void erofs_build_fault_attr(struct erofs_sb_info *sbi,
						unsigned int rate)
{
	struct erofs_fault_info *ffi = &sbi->fault_info;

	if (rate) {
		atomic_set(&ffi->inject_ops, 0);
		ffi->inject_rate = rate;
		ffi->inject_type = (1 << FAULT_MAX) - 1;
	} else {
		memset(ffi, 0, sizeof(struct erofs_fault_info));
	}
}
#endif

static void default_options(struct erofs_sb_info *sbi)
{
#ifdef CONFIG_EROFS_FS_XATTR
	set_opt(sbi, XATTR_USER);
#endif

#ifdef CONFIG_EROFS_FS_POSIX_ACL
	set_opt(sbi, POSIX_ACL);
#endif
}

enum {
	Opt_user_xattr,
	Opt_nouser_xattr,
	Opt_acl,
	Opt_noacl,
	Opt_fault_injection,
	Opt_err
};

static match_table_t erofs_tokens = {
	{Opt_user_xattr, "user_xattr"},
	{Opt_nouser_xattr, "nouser_xattr"},
	{Opt_acl, "acl"},
	{Opt_noacl, "noacl"},
	{Opt_fault_injection, "fault_injection=%u"},
	{Opt_err, NULL}
};

static int parse_options(struct super_block *sb, char *options)
{
	substring_t args[MAX_OPT_ARGS];
	char *p;
	int arg = 0;

	if (!options)
		return 0;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;

		if (!*p)
			continue;

		args[0].to = args[0].from = NULL;
		token = match_token(p, erofs_tokens, args);

		switch (token) {
#ifdef CONFIG_EROFS_FS_XATTR
		case Opt_user_xattr:
			set_opt(EROFS_SB(sb), XATTR_USER);
			break;
		case Opt_nouser_xattr:
			clear_opt(EROFS_SB(sb), XATTR_USER);
			break;
#else
		case Opt_user_xattr:
			infoln("user_xattr options not supported");
			break;
		case Opt_nouser_xattr:
			infoln("nouser_xattr options not supported");
			break;
#endif
#ifdef CONFIG_EROFS_FS_POSIX_ACL
		case Opt_acl:
			set_opt(EROFS_SB(sb), POSIX_ACL);
			break;
		case Opt_noacl:
			clear_opt(EROFS_SB(sb), POSIX_ACL);
			break;
#else
		case Opt_acl:
			infoln("acl options not supported");
			break;
		case Opt_noacl:
			infoln("noacl options not supported");
			break;
#endif
		case Opt_fault_injection:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
#ifdef CONFIG_EROFS_FAULT_INJECTION
			erofs_build_fault_attr(EROFS_SB(sb), arg);
			set_opt(EROFS_SB(sb), FAULT_INJECTION);
#else
			infoln("FAULT_INJECTION was not selected");
#endif
			break;
		default:
			errln("Unrecognized mount option \"%s\" "
					"or missing value", p);
			return -EINVAL;
		}
	}
	return 0;
}

static int erofs_read_super(struct super_block *sb,
	const char *dev_name, void *data, int silent)
{
	struct inode *inode;
	struct erofs_sb_info *sbi;
	int err = -EINVAL;

	infoln("read_super, device -> %s", dev_name);
	infoln("options -> %s", (char *)data);

	if (unlikely(!sb_set_blocksize(sb, EROFS_BLKSIZ))) {
		errln("failed to set erofs blksize");
		goto err;
	}

	sbi = kzalloc(sizeof(struct erofs_sb_info), GFP_KERNEL);
	if (unlikely(sbi == NULL)) {
		err = -ENOMEM;
		goto err;
	}
	sb->s_fs_info = sbi;

	err = superblock_read(sb);
	if (err)
		goto err_sbread;

	sb->s_magic = EROFS_SUPER_MAGIC;
	sb->s_flags |= MS_RDONLY | MS_NOATIME;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_time_gran = 1;

	sb->s_op = &erofs_sops;

#ifdef CONFIG_EROFS_FS_XATTR
	sb->s_xattr = erofs_xattr_handlers;
#endif

	/* set erofs default mount options */
	default_options(sbi);

	err = parse_options(sb, data);
	if (err)
		goto err_parseopt;

	if (!silent)
		infoln("root inode @ nid %llu", ROOT_NID(sbi));

#ifdef CONFIG_EROFS_FS_ZIP
	INIT_RADIX_TREE(&sbi->workstn_tree, GFP_ATOMIC);
#endif

	/* get the root inode */
	inode = erofs_iget(sb, ROOT_NID(sbi), true);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto err_iget;
	}

	if (!S_ISDIR(inode->i_mode)) {
		errln("rootino(nid %llu) is not a directory(i_mode %o)",
			ROOT_NID(sbi), inode->i_mode);
		err = -EINVAL;
		goto err_isdir;
	}

	sb->s_root = d_make_root(inode);
	if (sb->s_root == NULL) {
		err = -ENOMEM;
		goto err_makeroot;
	}

	/* save the device name to sbi */
	sbi->dev_name = __getname();
	if (sbi->dev_name == NULL) {
		err = -ENOMEM;
		goto err_devname;
	}

	snprintf(sbi->dev_name, PATH_MAX, "%s", dev_name);
	sbi->dev_name[PATH_MAX - 1] = '\0';

	erofs_register_super(sb);

	/*
	 * We already have a positive dentry, which was instantiated
	 * by d_make_root. Just need to d_rehash it.
	 */
	d_rehash(sb->s_root);

	if (!silent)
		infoln("mounted on %s with opts: %s.", dev_name,
			(char *)data);
	return 0;
	/*
	 * please add a label for each exit point and use
	 * the following name convention, thus new features
	 * can be integrated easily without renaming labels.
	 */
err_devname:
	dput(sb->s_root);
err_makeroot:
err_isdir:
	if (sb->s_root == NULL)
		iput(inode);
err_iget:
err_parseopt:
err_sbread:
	sb->s_fs_info = NULL;
	kfree(sbi);
err:
	return err;
}

/*
 * could be triggered after deactivate_locked_super()
 * is called, thus including umount and failed to initialize.
 */
static void erofs_put_super(struct super_block *sb)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);

	/* for cases which are failed in "read_super" */
	if (sbi == NULL)
		return;

	WARN_ON(sb->s_magic != EROFS_SUPER_MAGIC);

	infoln("unmounted for %s", sbi->dev_name);
	__putname(sbi->dev_name);

	mutex_lock(&sbi->umount_mutex);

#ifdef CONFIG_EROFS_FS_ZIP
	erofs_workstation_cleanup_all(sb);
#endif

	erofs_unregister_super(sb);
	mutex_unlock(&sbi->umount_mutex);

	kfree(sbi);
	sb->s_fs_info = NULL;
}


struct erofs_mount_private {
	const char *dev_name;
	char *options;
};

/* support mount_bdev() with options */
static int erofs_fill_super(struct super_block *sb,
	void *_priv, int silent)
{
	struct erofs_mount_private *priv = _priv;

	return erofs_read_super(sb, priv->dev_name,
		priv->options, silent);
}

static struct dentry *erofs_mount(
	struct file_system_type *fs_type, int flags,
	const char *dev_name, void *data)
{
	struct erofs_mount_private priv = {
		.dev_name = dev_name,
		.options = data
	};

	return mount_bdev(fs_type, flags, dev_name,
		&priv, erofs_fill_super);
}

static void erofs_kill_sb(struct super_block *sb)
{
	kill_block_super(sb);
}

static struct shrinker erofs_shrinker_info = {
	.scan_objects = erofs_shrink_scan,
	.count_objects = erofs_shrink_count,
	.seeks = DEFAULT_SEEKS,
};

static struct file_system_type erofs_fs_type = {
	.owner          = THIS_MODULE,
	.name           = "erofs",
	.mount          = erofs_mount,
	.kill_sb        = erofs_kill_sb,
	.fs_flags       = FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("erofs");

#ifdef CONFIG_EROFS_FS_ZIP
extern int z_erofs_init_zip_subsystem(void);
extern void z_erofs_exit_zip_subsystem(void);
#endif

static int __init erofs_module_init(void)
{
	int err;

	erofs_check_ondisk_layout_definitions();
	infoln("initializing erofs " EROFS_VERSION);

	err = erofs_init_inode_cache();
	if (err)
		goto icache_err;

	err = register_shrinker(&erofs_shrinker_info);
	if (err)
		goto shrinker_err;

#ifdef CONFIG_EROFS_FS_ZIP
	err = z_erofs_init_zip_subsystem();
	if (err)
		goto zip_err;
#endif

	err = register_filesystem(&erofs_fs_type);
	if (err)
		goto fs_err;

	infoln("successfully to initialize erofs");
	return 0;

fs_err:
#ifdef CONFIG_EROFS_FS_ZIP
	z_erofs_exit_zip_subsystem();
zip_err:
#endif
	unregister_shrinker(&erofs_shrinker_info);
shrinker_err:
	erofs_exit_inode_cache();
icache_err:
	return err;
}

static void __exit erofs_module_exit(void)
{
	unregister_filesystem(&erofs_fs_type);
#ifdef CONFIG_EROFS_FS_ZIP
	z_erofs_exit_zip_subsystem();
#endif
	unregister_shrinker(&erofs_shrinker_info);
	erofs_exit_inode_cache();
	infoln("successfully finalize erofs");
}

/* get filesystem statistics */
static int erofs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);

	buf->f_type = sb->s_magic;
	buf->f_bsize = EROFS_BLKSIZ;
	buf->f_blocks = sbi->blocks;
	buf->f_bfree = buf->f_bavail = 0;

	buf->f_files = ULLONG_MAX;
	buf->f_ffree = ULLONG_MAX - sbi->inos;

	buf->f_namelen = EROFS_NAME_LEN;

	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);
	return 0;
}

static int erofs_show_options(struct seq_file *seq, struct dentry *root)
{
	struct erofs_sb_info *sbi __maybe_unused = EROFS_SB(root->d_sb);

#ifdef CONFIG_EROFS_FS_XATTR
	if (test_opt(sbi, XATTR_USER))
		seq_puts(seq, ",user_xattr");
	else
		seq_puts(seq, ",nouser_xattr");
#endif
#ifdef CONFIG_EROFS_FS_POSIX_ACL
	if (test_opt(sbi, POSIX_ACL))
		seq_puts(seq, ",acl");
	else
		seq_puts(seq, ",noacl");
#endif
#ifdef CONFIG_EROFS_FAULT_INJECTION
	if (test_opt(sbi, FAULT_INJECTION))
		seq_printf(seq, ",fault_injection=%u",
				sbi->fault_info.inject_rate);
#endif
	return 0;
}

static int erofs_remount(struct super_block *sb, int *flags, char *data)
{
	BUG_ON(!sb_rdonly(sb));

	*flags |= MS_RDONLY;
	return 0;
}

const struct super_operations erofs_sops = {
	.put_super = erofs_put_super,
	.alloc_inode = alloc_inode,
	.destroy_inode = destroy_inode,
	.statfs = erofs_statfs,
	.show_options = erofs_show_options,
	.remount_fs = erofs_remount,
};

module_init(erofs_module_init);
module_exit(erofs_module_exit);

MODULE_DESCRIPTION("Enhanced ROM File System");
MODULE_AUTHOR("Gao Xiang, Yu Chao, Miao Xie, CONSUMER BG, HUAWEI Inc.");
MODULE_LICENSE("GPL");

