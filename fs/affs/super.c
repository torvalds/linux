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
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/sched.h>
#include "affs.h"

extern struct timezone sys_tz;

static int affs_statfs(struct dentry *dentry, struct kstatfs *buf);
static int affs_remount (struct super_block *sb, int *flags, char *data);

static void
affs_put_super(struct super_block *sb)
{
	struct affs_sb_info *sbi = AFFS_SB(sb);
	pr_debug("AFFS: put_super()\n");

	if (!(sb->s_flags & MS_RDONLY)) {
		AFFS_ROOT_TAIL(sb, sbi->s_root_bh)->bm_flag = cpu_to_be32(1);
		secs_to_datestamp(get_seconds(),
				  &AFFS_ROOT_TAIL(sb, sbi->s_root_bh)->disk_change);
		affs_fix_checksum(sb, sbi->s_root_bh);
		mark_buffer_dirty(sbi->s_root_bh);
	}

	kfree(sbi->s_prefix);
	affs_free_bitmap(sb);
	affs_brelse(sbi->s_root_bh);
	kfree(sbi);
	sb->s_fs_info = NULL;
	return;
}

static void
affs_write_super(struct super_block *sb)
{
	int clean = 2;
	struct affs_sb_info *sbi = AFFS_SB(sb);

	if (!(sb->s_flags & MS_RDONLY)) {
		//	if (sbi->s_bitmap[i].bm_bh) {
		//		if (buffer_dirty(sbi->s_bitmap[i].bm_bh)) {
		//			clean = 0;
		AFFS_ROOT_TAIL(sb, sbi->s_root_bh)->bm_flag = cpu_to_be32(clean);
		secs_to_datestamp(get_seconds(),
				  &AFFS_ROOT_TAIL(sb, sbi->s_root_bh)->disk_change);
		affs_fix_checksum(sb, sbi->s_root_bh);
		mark_buffer_dirty(sbi->s_root_bh);
		sb->s_dirt = !clean;	/* redo until bitmap synced */
	} else
		sb->s_dirt = 0;

	pr_debug("AFFS: write_super() at %lu, clean=%d\n", get_seconds(), clean);
}

static struct kmem_cache * affs_inode_cachep;

static struct inode *affs_alloc_inode(struct super_block *sb)
{
	struct affs_inode_info *i;

	i = kmem_cache_alloc(affs_inode_cachep, GFP_KERNEL);
	if (!i)
		return NULL;

	i->vfs_inode.i_version = 1;
	i->i_lc = NULL;
	i->i_ext_bh = NULL;
	i->i_pa_cnt = 0;

	return &i->vfs_inode;
}

static void affs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(affs_inode_cachep, AFFS_I(inode));
}

static void init_once(void *foo)
{
	struct affs_inode_info *ei = (struct affs_inode_info *) foo;

	init_MUTEX(&ei->i_link_lock);
	init_MUTEX(&ei->i_ext_lock);
	inode_init_once(&ei->vfs_inode);
}

static int init_inodecache(void)
{
	affs_inode_cachep = kmem_cache_create("affs_inode_cache",
					     sizeof(struct affs_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     init_once);
	if (affs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	kmem_cache_destroy(affs_inode_cachep);
}

static const struct super_operations affs_sops = {
	.alloc_inode	= affs_alloc_inode,
	.destroy_inode	= affs_destroy_inode,
	.write_inode	= affs_write_inode,
	.delete_inode	= affs_delete_inode,
	.clear_inode	= affs_clear_inode,
	.put_super	= affs_put_super,
	.write_super	= affs_write_super,
	.statfs		= affs_statfs,
	.remount_fs	= affs_remount,
	.show_options	= generic_show_options,
};

enum {
	Opt_bs, Opt_mode, Opt_mufs, Opt_prefix, Opt_protect,
	Opt_reserved, Opt_root, Opt_setgid, Opt_setuid,
	Opt_verbose, Opt_volume, Opt_ignore, Opt_err,
};

static const match_table_t tokens = {
	{Opt_bs, "bs=%u"},
	{Opt_mode, "mode=%o"},
	{Opt_mufs, "mufs"},
	{Opt_prefix, "prefix=%s"},
	{Opt_protect, "protect"},
	{Opt_reserved, "reserved=%u"},
	{Opt_root, "root=%u"},
	{Opt_setgid, "setgid=%u"},
	{Opt_setuid, "setuid=%u"},
	{Opt_verbose, "verbose"},
	{Opt_volume, "volume=%s"},
	{Opt_ignore, "grpquota"},
	{Opt_ignore, "noquota"},
	{Opt_ignore, "quota"},
	{Opt_ignore, "usrquota"},
	{Opt_err, NULL},
};

static int
parse_options(char *options, uid_t *uid, gid_t *gid, int *mode, int *reserved, s32 *root,
		int *blocksize, char **prefix, char *volume, unsigned long *mount_opts)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];

	/* Fill in defaults */

	*uid        = current_uid();
	*gid        = current_gid();
	*reserved   = 2;
	*root       = -1;
	*blocksize  = -1;
	volume[0]   = ':';
	volume[1]   = 0;
	*mount_opts = 0;
	if (!options)
		return 1;

	while ((p = strsep(&options, ",")) != NULL) {
		int token, n, option;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_bs:
			if (match_int(&args[0], &n))
				return -EINVAL;
			if (n != 512 && n != 1024 && n != 2048
			    && n != 4096) {
				printk ("AFFS: Invalid blocksize (512, 1024, 2048, 4096 allowed)\n");
				return 0;
			}
			*blocksize = n;
			break;
		case Opt_mode:
			if (match_octal(&args[0], &option))
				return 1;
			*mode = option & 0777;
			*mount_opts |= SF_SETMODE;
			break;
		case Opt_mufs:
			*mount_opts |= SF_MUFS;
			break;
		case Opt_prefix:
			/* Free any previous prefix */
			kfree(*prefix);
			*prefix = match_strdup(&args[0]);
			if (!*prefix)
				return 0;
			*mount_opts |= SF_PREFIX;
			break;
		case Opt_protect:
			*mount_opts |= SF_IMMUTABLE;
			break;
		case Opt_reserved:
			if (match_int(&args[0], reserved))
				return 1;
			break;
		case Opt_root:
			if (match_int(&args[0], root))
				return 1;
			break;
		case Opt_setgid:
			if (match_int(&args[0], &option))
				return 1;
			*gid = option;
			*mount_opts |= SF_SETGID;
			break;
		case Opt_setuid:
			if (match_int(&args[0], &option))
				return -EINVAL;
			*uid = option;
			*mount_opts |= SF_SETUID;
			break;
		case Opt_verbose:
			*mount_opts |= SF_VERBOSE;
			break;
		case Opt_volume: {
			char *vol = match_strdup(&args[0]);
			if (!vol)
				return 0;
			strlcpy(volume, vol, 32);
			kfree(vol);
			break;
		}
		case Opt_ignore:
		 	/* Silently ignore the quota options */
			break;
		default:
			printk("AFFS: Unrecognized mount option \"%s\" "
					"or missing value\n", p);
			return 0;
		}
	}
	return 1;
}

/* This function definitely needs to be split up. Some fine day I'll
 * hopefully have the guts to do so. Until then: sorry for the mess.
 */

static int affs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct affs_sb_info	*sbi;
	struct buffer_head	*root_bh = NULL;
	struct buffer_head	*boot_bh;
	struct inode		*root_inode = NULL;
	s32			 root_block;
	int			 size, blocksize;
	u32			 chksum;
	int			 num_bm;
	int			 i, j;
	s32			 key;
	uid_t			 uid;
	gid_t			 gid;
	int			 reserved;
	unsigned long		 mount_flags;
	int			 tmp_flags;	/* fix remount prototype... */
	u8			 sig[4];
	int			 ret = -EINVAL;

	save_mount_options(sb, data);

	pr_debug("AFFS: read_super(%s)\n",data ? (const char *)data : "no options");

	sb->s_magic             = AFFS_SUPER_MAGIC;
	sb->s_op                = &affs_sops;
	sb->s_flags |= MS_NODIRATIME;

	sbi = kzalloc(sizeof(struct affs_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	sb->s_fs_info = sbi;
	mutex_init(&sbi->s_bmlock);

	if (!parse_options(data,&uid,&gid,&i,&reserved,&root_block,
				&blocksize,&sbi->s_prefix,
				sbi->s_volume, &mount_flags)) {
		printk(KERN_ERR "AFFS: Error parsing options\n");
		return -EINVAL;
	}
	/* N.B. after this point s_prefix must be released */

	sbi->s_flags   = mount_flags;
	sbi->s_mode    = i;
	sbi->s_uid     = uid;
	sbi->s_gid     = gid;
	sbi->s_reserved= reserved;

	/* Get the size of the device in 512-byte blocks.
	 * If we later see that the partition uses bigger
	 * blocks, we will have to change it.
	 */

	size = sb->s_bdev->bd_inode->i_size >> 9;
	pr_debug("AFFS: initial blocksize=%d, #blocks=%d\n", 512, size);

	affs_set_blocksize(sb, PAGE_SIZE);
	/* Try to find root block. Its location depends on the block size. */

	i = 512;
	j = 4096;
	if (blocksize > 0) {
		i = j = blocksize;
		size = size / (blocksize / 512);
	}
	for (blocksize = i, key = 0; blocksize <= j; blocksize <<= 1, size >>= 1) {
		sbi->s_root_block = root_block;
		if (root_block < 0)
			sbi->s_root_block = (reserved + size - 1) / 2;
		pr_debug("AFFS: setting blocksize to %d\n", blocksize);
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
			pr_debug("AFFS: Dev %s, trying root=%u, bs=%d, "
				"size=%d, reserved=%d\n",
				sb->s_id,
				sbi->s_root_block + num_bm,
				blocksize, size, reserved);
			root_bh = affs_bread(sb, sbi->s_root_block + num_bm);
			if (!root_bh)
				continue;
			if (!affs_checksum_block(sb, root_bh) &&
			    be32_to_cpu(AFFS_ROOT_HEAD(root_bh)->ptype) == T_SHORT &&
			    be32_to_cpu(AFFS_ROOT_TAIL(sb, root_bh)->stype) == ST_ROOT) {
				sbi->s_hashsize    = blocksize / 4 - 56;
				sbi->s_root_block += num_bm;
				key                        = 1;
				goto got_root;
			}
			affs_brelse(root_bh);
			root_bh = NULL;
		}
	}
	if (!silent)
		printk(KERN_ERR "AFFS: No valid root block on device %s\n",
			sb->s_id);
	goto out_error;

	/* N.B. after this point bh must be released */
got_root:
	root_block = sbi->s_root_block;

	/* Find out which kind of FS we have */
	boot_bh = sb_bread(sb, 0);
	if (!boot_bh) {
		printk(KERN_ERR "AFFS: Cannot read boot block\n");
		goto out_error;
	}
	memcpy(sig, boot_bh->b_data, 4);
	brelse(boot_bh);
	chksum = be32_to_cpu(*(__be32 *)sig);

	/* Dircache filesystems are compatible with non-dircache ones
	 * when reading. As long as they aren't supported, writing is
	 * not recommended.
	 */
	if ((chksum == FS_DCFFS || chksum == MUFS_DCFFS || chksum == FS_DCOFS
	     || chksum == MUFS_DCOFS) && !(sb->s_flags & MS_RDONLY)) {
		printk(KERN_NOTICE "AFFS: Dircache FS - mounting %s read only\n",
			sb->s_id);
		sb->s_flags |= MS_RDONLY;
	}
	switch (chksum) {
		case MUFS_FS:
		case MUFS_INTLFFS:
		case MUFS_DCFFS:
			sbi->s_flags |= SF_MUFS;
			/* fall thru */
		case FS_INTLFFS:
		case FS_DCFFS:
			sbi->s_flags |= SF_INTL;
			break;
		case MUFS_FFS:
			sbi->s_flags |= SF_MUFS;
			break;
		case FS_FFS:
			break;
		case MUFS_OFS:
			sbi->s_flags |= SF_MUFS;
			/* fall thru */
		case FS_OFS:
			sbi->s_flags |= SF_OFS;
			sb->s_flags |= MS_NOEXEC;
			break;
		case MUFS_DCOFS:
		case MUFS_INTLOFS:
			sbi->s_flags |= SF_MUFS;
		case FS_DCOFS:
		case FS_INTLOFS:
			sbi->s_flags |= SF_INTL | SF_OFS;
			sb->s_flags |= MS_NOEXEC;
			break;
		default:
			printk(KERN_ERR "AFFS: Unknown filesystem on device %s: %08X\n",
				sb->s_id, chksum);
			goto out_error;
	}

	if (mount_flags & SF_VERBOSE) {
		u8 len = AFFS_ROOT_TAIL(sb, root_bh)->disk_name[0];
		printk(KERN_NOTICE "AFFS: Mounting volume \"%.*s\": Type=%.3s\\%c, Blocksize=%d\n",
			len > 31 ? 31 : len,
			AFFS_ROOT_TAIL(sb, root_bh)->disk_name + 1,
			sig, sig[3] + '0', blocksize);
	}

	sb->s_flags |= MS_NODEV | MS_NOSUID;

	sbi->s_data_blksize = sb->s_blocksize;
	if (sbi->s_flags & SF_OFS)
		sbi->s_data_blksize -= 24;

	/* Keep super block in cache */
	sbi->s_root_bh = root_bh;
	/* N.B. after this point s_root_bh must be released */

	tmp_flags = sb->s_flags;
	if (affs_init_bitmap(sb, &tmp_flags))
		goto out_error;
	sb->s_flags = tmp_flags;

	/* set up enough so that it can read an inode */

	root_inode = affs_iget(sb, root_block);
	if (IS_ERR(root_inode)) {
		ret = PTR_ERR(root_inode);
		goto out_error_noinode;
	}

	sb->s_root = d_alloc_root(root_inode);
	if (!sb->s_root) {
		printk(KERN_ERR "AFFS: Get root inode failed\n");
		goto out_error;
	}
	sb->s_root->d_op = &affs_dentry_operations;

	pr_debug("AFFS: s_flags=%lX\n",sb->s_flags);
	return 0;

	/*
	 * Begin the cascaded cleanup ...
	 */
out_error:
	if (root_inode)
		iput(root_inode);
out_error_noinode:
	kfree(sbi->s_bitmap);
	affs_brelse(root_bh);
	kfree(sbi->s_prefix);
	kfree(sbi);
	sb->s_fs_info = NULL;
	return ret;
}

static int
affs_remount(struct super_block *sb, int *flags, char *data)
{
	struct affs_sb_info	*sbi = AFFS_SB(sb);
	int			 blocksize;
	uid_t			 uid;
	gid_t			 gid;
	int			 mode;
	int			 reserved;
	int			 root_block;
	unsigned long		 mount_flags;
	int			 res = 0;
	char			*new_opts = kstrdup(data, GFP_KERNEL);

	pr_debug("AFFS: remount(flags=0x%x,opts=\"%s\")\n",*flags,data);

	*flags |= MS_NODIRATIME;

	if (!parse_options(data, &uid, &gid, &mode, &reserved, &root_block,
			   &blocksize, &sbi->s_prefix, sbi->s_volume,
			   &mount_flags)) {
		kfree(new_opts);
		return -EINVAL;
	}
	replace_mount_options(sb, new_opts);

	sbi->s_flags = mount_flags;
	sbi->s_mode  = mode;
	sbi->s_uid   = uid;
	sbi->s_gid   = gid;

	if ((*flags & MS_RDONLY) == (sb->s_flags & MS_RDONLY))
		return 0;
	if (*flags & MS_RDONLY) {
		sb->s_dirt = 1;
		while (sb->s_dirt)
			affs_write_super(sb);
		affs_free_bitmap(sb);
	} else
		res = affs_init_bitmap(sb, flags);

	return res;
}

static int
affs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	int		 free;
	u64		 id = huge_encode_dev(sb->s_bdev->bd_dev);

	pr_debug("AFFS: statfs() partsize=%d, reserved=%d\n",AFFS_SB(sb)->s_partition_size,
	     AFFS_SB(sb)->s_reserved);

	free          = affs_count_free_blocks(sb);
	buf->f_type    = AFFS_SUPER_MAGIC;
	buf->f_bsize   = sb->s_blocksize;
	buf->f_blocks  = AFFS_SB(sb)->s_partition_size - AFFS_SB(sb)->s_reserved;
	buf->f_bfree   = free;
	buf->f_bavail  = free;
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);
	buf->f_namelen = 30;
	return 0;
}

static int affs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, affs_fill_super,
			   mnt);
}

static struct file_system_type affs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "affs",
	.get_sb		= affs_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

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
