// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/drivers/staging/erofs/super.c
 *
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             http://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 */
#include <linux/module.h>
#include <linux/buffer_head.h>
#include <linux/statfs.h>
#include <linux/parser.h>
#include <linux/seq_file.h>
#include "xattr.h"

#define CREATE_TRACE_POINTS
#include <trace/events/erofs.h>

static struct kmem_cache *erofs_inode_cachep __read_mostly;

static void init_once(void *ptr)
{
	struct erofs_vnode *vi = ptr;

	inode_init_once(&vi->vfs_inode);
}

static int __init erofs_init_inode_cache(void)
{
	erofs_inode_cachep = kmem_cache_create("erofs_inode",
					       sizeof(struct erofs_vnode), 0,
					       SLAB_RECLAIM_ACCOUNT,
					       init_once);

	return erofs_inode_cachep ? 0 : -ENOMEM;
}

static void erofs_exit_inode_cache(void)
{
	kmem_cache_destroy(erofs_inode_cachep);
}

static struct inode *alloc_inode(struct super_block *sb)
{
	struct erofs_vnode *vi =
		kmem_cache_alloc(erofs_inode_cachep, GFP_KERNEL);

	if (!vi)
		return NULL;

	/* zero out everything except vfs_inode */
	memset(vi, 0, offsetof(struct erofs_vnode, vfs_inode));
	return &vi->vfs_inode;
}

static void free_inode(struct inode *inode)
{
	struct erofs_vnode *vi = EROFS_V(inode);

	/* be careful RCU symlink path (see ext4_inode_info->i_data)! */
	if (is_inode_fast_symlink(inode))
		kfree(inode->i_link);

	kfree(vi->xattr_shared_xattrs);

	kmem_cache_free(erofs_inode_cachep, vi);
}

static bool check_layout_compatibility(struct super_block *sb,
				       struct erofs_super_block *layout)
{
	const unsigned int requirements = le32_to_cpu(layout->requirements);

	EROFS_SB(sb)->requirements = requirements;

	/* check if current kernel meets all mandatory requirements */
	if (requirements & (~EROFS_ALL_REQUIREMENTS)) {
		errln("unidentified requirements %x, please upgrade kernel version",
		      requirements & ~EROFS_ALL_REQUIREMENTS);
		return false;
	}
	return true;
}

static int superblock_read(struct super_block *sb)
{
	struct erofs_sb_info *sbi;
	struct buffer_head *bh;
	struct erofs_super_block *layout;
	unsigned int blkszbits;
	int ret;

	bh = sb_bread(sb, 0);

	if (!bh) {
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

	if (!check_layout_compatibility(sb, layout))
		goto out;

	sbi->blocks = le32_to_cpu(layout->blocks);
	sbi->meta_blkaddr = le32_to_cpu(layout->meta_blkaddr);
#ifdef CONFIG_EROFS_FS_XATTR
	sbi->xattr_blkaddr = le32_to_cpu(layout->xattr_blkaddr);
#endif
	sbi->islotbits = ffs(sizeof(struct erofs_inode_v1)) - 1;
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
const char *erofs_fault_name[FAULT_MAX] = {
	[FAULT_KMALLOC]		= "kmalloc",
	[FAULT_READ_IO]		= "read IO error",
};

static void __erofs_build_fault_attr(struct erofs_sb_info *sbi,
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

	set_opt(sbi, FAULT_INJECTION);
}

static int erofs_build_fault_attr(struct erofs_sb_info *sbi,
				  substring_t *args)
{
	int rate = 0;

	if (args->from && match_int(args, &rate))
		return -EINVAL;

	__erofs_build_fault_attr(sbi, rate);
	return 0;
}

static unsigned int erofs_get_fault_rate(struct erofs_sb_info *sbi)
{
	return sbi->fault_info.inject_rate;
}
#else
static void __erofs_build_fault_attr(struct erofs_sb_info *sbi,
				     unsigned int rate)
{
}

static int erofs_build_fault_attr(struct erofs_sb_info *sbi,
				  substring_t *args)
{
	infoln("fault_injection options not supported");
	return 0;
}

static unsigned int erofs_get_fault_rate(struct erofs_sb_info *sbi)
{
	return 0;
}
#endif

#ifdef CONFIG_EROFS_FS_ZIP
static int erofs_build_cache_strategy(struct erofs_sb_info *sbi,
				      substring_t *args)
{
	const char *cs = match_strdup(args);
	int err = 0;

	if (!cs) {
		errln("Not enough memory to store cache strategy");
		return -ENOMEM;
	}

	if (!strcmp(cs, "disabled")) {
		sbi->cache_strategy = EROFS_ZIP_CACHE_DISABLED;
	} else if (!strcmp(cs, "readahead")) {
		sbi->cache_strategy = EROFS_ZIP_CACHE_READAHEAD;
	} else if (!strcmp(cs, "readaround")) {
		sbi->cache_strategy = EROFS_ZIP_CACHE_READAROUND;
	} else {
		errln("Unrecognized cache strategy \"%s\"", cs);
		err = -EINVAL;
	}
	kfree(cs);
	return err;
}
#else
static int erofs_build_cache_strategy(struct erofs_sb_info *sbi,
				      substring_t *args)
{
	infoln("EROFS compression is disabled, so cache strategy is ignored");
	return 0;
}
#endif

/* set up default EROFS parameters */
static void default_options(struct erofs_sb_info *sbi)
{
#ifdef CONFIG_EROFS_FS_ZIP
	sbi->cache_strategy = EROFS_ZIP_CACHE_READAROUND;
	sbi->max_sync_decompress_pages = 3;
#endif
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
	Opt_cache_strategy,
	Opt_err
};

static match_table_t erofs_tokens = {
	{Opt_user_xattr, "user_xattr"},
	{Opt_nouser_xattr, "nouser_xattr"},
	{Opt_acl, "acl"},
	{Opt_noacl, "noacl"},
	{Opt_fault_injection, "fault_injection=%u"},
	{Opt_cache_strategy, "cache_strategy=%s"},
	{Opt_err, NULL}
};

static int parse_options(struct super_block *sb, char *options)
{
	substring_t args[MAX_OPT_ARGS];
	char *p;
	int err;

	if (!options)
		return 0;

	while ((p = strsep(&options, ","))) {
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
			err = erofs_build_fault_attr(EROFS_SB(sb), args);
			if (err)
				return err;
			break;
		case Opt_cache_strategy:
			err = erofs_build_cache_strategy(EROFS_SB(sb), args);
			if (err)
				return err;
			break;
		default:
			errln("Unrecognized mount option \"%s\" or missing value", p);
			return -EINVAL;
		}
	}
	return 0;
}

#ifdef CONFIG_EROFS_FS_ZIP
static const struct address_space_operations managed_cache_aops;

static int managed_cache_releasepage(struct page *page, gfp_t gfp_mask)
{
	int ret = 1;	/* 0 - busy */
	struct address_space *const mapping = page->mapping;

	DBG_BUGON(!PageLocked(page));
	DBG_BUGON(mapping->a_ops != &managed_cache_aops);

	if (PagePrivate(page))
		ret = erofs_try_to_free_cached_page(mapping, page);

	return ret;
}

static void managed_cache_invalidatepage(struct page *page,
					 unsigned int offset,
					 unsigned int length)
{
	const unsigned int stop = length + offset;

	DBG_BUGON(!PageLocked(page));

	/* Check for potential overflow in debug mode */
	DBG_BUGON(stop > PAGE_SIZE || stop < length);

	if (offset == 0 && stop == PAGE_SIZE)
		while (!managed_cache_releasepage(page, GFP_NOFS))
			cond_resched();
}

static const struct address_space_operations managed_cache_aops = {
	.releasepage = managed_cache_releasepage,
	.invalidatepage = managed_cache_invalidatepage,
};

static int erofs_init_managed_cache(struct super_block *sb)
{
	struct erofs_sb_info *const sbi = EROFS_SB(sb);
	struct inode *const inode = new_inode(sb);

	if (unlikely(!inode))
		return -ENOMEM;

	set_nlink(inode, 1);
	inode->i_size = OFFSET_MAX;

	inode->i_mapping->a_ops = &managed_cache_aops;
	mapping_set_gfp_mask(inode->i_mapping,
			     GFP_NOFS | __GFP_HIGHMEM | __GFP_MOVABLE);
	sbi->managed_cache = inode;
	return 0;
}
#else
static int erofs_init_managed_cache(struct super_block *sb) { return 0; }
#endif

static int erofs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *inode;
	struct erofs_sb_info *sbi;
	int err;

	infoln("fill_super, device -> %s", sb->s_id);
	infoln("options -> %s", (char *)data);

	sb->s_magic = EROFS_SUPER_MAGIC;

	if (unlikely(!sb_set_blocksize(sb, EROFS_BLKSIZ))) {
		errln("failed to set erofs blksize");
		return -EINVAL;
	}

	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (unlikely(!sbi))
		return -ENOMEM;

	sb->s_fs_info = sbi;
	err = superblock_read(sb);
	if (err)
		return err;

	sb->s_flags |= SB_RDONLY | SB_NOATIME;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_time_gran = 1;

	sb->s_op = &erofs_sops;

#ifdef CONFIG_EROFS_FS_XATTR
	sb->s_xattr = erofs_xattr_handlers;
#endif
	/* set erofs default mount options */
	default_options(sbi);

	err = parse_options(sb, data);
	if (unlikely(err))
		return err;

	if (!silent)
		infoln("root inode @ nid %llu", ROOT_NID(sbi));

	if (test_opt(sbi, POSIX_ACL))
		sb->s_flags |= SB_POSIXACL;
	else
		sb->s_flags &= ~SB_POSIXACL;

#ifdef CONFIG_EROFS_FS_ZIP
	INIT_RADIX_TREE(&sbi->workstn_tree, GFP_ATOMIC);
#endif

	/* get the root inode */
	inode = erofs_iget(sb, ROOT_NID(sbi), true);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	if (unlikely(!S_ISDIR(inode->i_mode))) {
		errln("rootino(nid %llu) is not a directory(i_mode %o)",
		      ROOT_NID(sbi), inode->i_mode);
		iput(inode);
		return -EINVAL;
	}

	sb->s_root = d_make_root(inode);
	if (unlikely(!sb->s_root))
		return -ENOMEM;

	erofs_shrinker_register(sb);
	/* sb->s_umount is already locked, SB_ACTIVE and SB_BORN are not set */
	err = erofs_init_managed_cache(sb);
	if (unlikely(err))
		return err;

	if (!silent)
		infoln("mounted on %s with opts: %s.", sb->s_id, (char *)data);
	return 0;
}

static struct dentry *erofs_mount(struct file_system_type *fs_type, int flags,
				  const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, erofs_fill_super);
}

/*
 * could be triggered after deactivate_locked_super()
 * is called, thus including umount and failed to initialize.
 */
static void erofs_kill_sb(struct super_block *sb)
{
	struct erofs_sb_info *sbi;

	WARN_ON(sb->s_magic != EROFS_SUPER_MAGIC);
	infoln("unmounting for %s", sb->s_id);

	kill_block_super(sb);

	sbi = EROFS_SB(sb);
	if (!sbi)
		return;
	kfree(sbi);
	sb->s_fs_info = NULL;
}

/* called when ->s_root is non-NULL */
static void erofs_put_super(struct super_block *sb)
{
	struct erofs_sb_info *const sbi = EROFS_SB(sb);

	DBG_BUGON(!sbi);

	erofs_shrinker_unregister(sb);
#ifdef CONFIG_EROFS_FS_ZIP
	iput(sbi->managed_cache);
	sbi->managed_cache = NULL;
#endif
}

static struct file_system_type erofs_fs_type = {
	.owner          = THIS_MODULE,
	.name           = "erofs",
	.mount          = erofs_mount,
	.kill_sb        = erofs_kill_sb,
	.fs_flags       = FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("erofs");

static int __init erofs_module_init(void)
{
	int err;

	erofs_check_ondisk_layout_definitions();
	infoln("initializing erofs " EROFS_VERSION);

	err = erofs_init_inode_cache();
	if (err)
		goto icache_err;

	err = erofs_init_shrinker();
	if (err)
		goto shrinker_err;

	err = z_erofs_init_zip_subsystem();
	if (err)
		goto zip_err;

	err = register_filesystem(&erofs_fs_type);
	if (err)
		goto fs_err;

	infoln("successfully to initialize erofs");
	return 0;

fs_err:
	z_erofs_exit_zip_subsystem();
zip_err:
	erofs_exit_shrinker();
shrinker_err:
	erofs_exit_inode_cache();
icache_err:
	return err;
}

static void __exit erofs_module_exit(void)
{
	unregister_filesystem(&erofs_fs_type);
	z_erofs_exit_zip_subsystem();
	erofs_exit_shrinker();
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
	if (test_opt(sbi, FAULT_INJECTION))
		seq_printf(seq, ",fault_injection=%u",
			   erofs_get_fault_rate(sbi));
#ifdef CONFIG_EROFS_FS_ZIP
	if (sbi->cache_strategy == EROFS_ZIP_CACHE_DISABLED) {
		seq_puts(seq, ",cache_strategy=disabled");
	} else if (sbi->cache_strategy == EROFS_ZIP_CACHE_READAHEAD) {
		seq_puts(seq, ",cache_strategy=readahead");
	} else if (sbi->cache_strategy == EROFS_ZIP_CACHE_READAROUND) {
		seq_puts(seq, ",cache_strategy=readaround");
	} else {
		seq_puts(seq, ",cache_strategy=(unknown)");
		DBG_BUGON(1);
	}
#endif
	return 0;
}

static int erofs_remount(struct super_block *sb, int *flags, char *data)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	unsigned int org_mnt_opt = sbi->mount_opt;
	unsigned int org_inject_rate = erofs_get_fault_rate(sbi);
	int err;

	DBG_BUGON(!sb_rdonly(sb));
	err = parse_options(sb, data);
	if (err)
		goto out;

	if (test_opt(sbi, POSIX_ACL))
		sb->s_flags |= SB_POSIXACL;
	else
		sb->s_flags &= ~SB_POSIXACL;

	*flags |= SB_RDONLY;
	return 0;
out:
	__erofs_build_fault_attr(sbi, org_inject_rate);
	sbi->mount_opt = org_mnt_opt;

	return err;
}

const struct super_operations erofs_sops = {
	.put_super = erofs_put_super,
	.alloc_inode = alloc_inode,
	.free_inode = free_inode,
	.statfs = erofs_statfs,
	.show_options = erofs_show_options,
	.remount_fs = erofs_remount,
};

module_init(erofs_module_init);
module_exit(erofs_module_exit);

MODULE_DESCRIPTION("Enhanced ROM File System");
MODULE_AUTHOR("Gao Xiang, Chao Yu, Miao Xie, CONSUMER BG, HUAWEI Inc.");
MODULE_LICENSE("GPL");

