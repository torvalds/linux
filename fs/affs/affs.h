#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/amigaffs.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

/* Ugly macros make the code more pretty. */

#define GET_END_PTR(st,p,sz)		 ((st *)((char *)(p)+((sz)-sizeof(st))))
#define AFFS_GET_HASHENTRY(data,hashkey) be32_to_cpu(((struct dir_front *)data)->hashtable[hashkey])
#define AFFS_BLOCK(sb, bh, blk)		(AFFS_HEAD(bh)->table[AFFS_SB(sb)->s_hashsize-1-(blk)])

#define AFFS_HEAD(bh)		((struct affs_head *)(bh)->b_data)
#define AFFS_TAIL(sb, bh)	((struct affs_tail *)((bh)->b_data+(sb)->s_blocksize-sizeof(struct affs_tail)))
#define AFFS_ROOT_HEAD(bh)	((struct affs_root_head *)(bh)->b_data)
#define AFFS_ROOT_TAIL(sb, bh)	((struct affs_root_tail *)((bh)->b_data+(sb)->s_blocksize-sizeof(struct affs_root_tail)))
#define AFFS_DATA_HEAD(bh)	((struct affs_data_head *)(bh)->b_data)
#define AFFS_DATA(bh)		(((struct affs_data_head *)(bh)->b_data)->data)

#define AFFS_CACHE_SIZE		PAGE_SIZE

#define AFFS_LC_SIZE		(AFFS_CACHE_SIZE/sizeof(u32)/2)
#define AFFS_AC_SIZE		(AFFS_CACHE_SIZE/sizeof(struct affs_ext_key)/2)
#define AFFS_AC_MASK		(AFFS_AC_SIZE-1)

#define AFFSNAMEMAX 30U

struct affs_ext_key {
	u32	ext;				/* idx of the extended block */
	u32	key;				/* block number */
};

/*
 * affs fs inode data in memory
 */
struct affs_inode_info {
	atomic_t i_opencnt;
	struct semaphore i_link_lock;		/* Protects internal inode access. */
	struct semaphore i_ext_lock;		/* Protects internal inode access. */
#define i_hash_lock i_ext_lock
	u32	 i_blkcnt;			/* block count */
	u32	 i_extcnt;			/* extended block count */
	u32	*i_lc;				/* linear cache of extended blocks */
	u32	 i_lc_size;
	u32	 i_lc_shift;
	u32	 i_lc_mask;
	struct affs_ext_key *i_ac;		/* associative cache of extended blocks */
	u32	 i_ext_last;			/* last accessed extended block */
	struct buffer_head *i_ext_bh;		/* bh of last extended block */
	loff_t	 mmu_private;
	u32	 i_protect;			/* unused attribute bits */
	u32	 i_lastalloc;			/* last allocated block */
	int	 i_pa_cnt;			/* number of preallocated blocks */
	struct inode vfs_inode;
};

/* short cut to get to the affs specific inode data */
static inline struct affs_inode_info *AFFS_I(struct inode *inode)
{
	return list_entry(inode, struct affs_inode_info, vfs_inode);
}

/*
 * super-block data in memory
 *
 * Block numbers are adjusted for their actual size
 *
 */

struct affs_bm_info {
	u32 bm_key;			/* Disk block number */
	u32 bm_free;			/* Free blocks in here */
};

struct affs_sb_info {
	int s_partition_size;		/* Partition size in blocks. */
	int s_reserved;			/* Number of reserved blocks. */
	//u32 s_blksize;			/* Initial device blksize */
	u32 s_data_blksize;		/* size of the data block w/o header */
	u32 s_root_block;		/* FFS root block number. */
	int s_hashsize;			/* Size of hash table. */
	unsigned long s_flags;		/* See below. */
	kuid_t s_uid;			/* uid to override */
	kgid_t s_gid;			/* gid to override */
	umode_t s_mode;			/* mode to override */
	struct buffer_head *s_root_bh;	/* Cached root block. */
	struct mutex s_bmlock;		/* Protects bitmap access. */
	struct affs_bm_info *s_bitmap;	/* Bitmap infos. */
	u32 s_bmap_count;		/* # of bitmap blocks. */
	u32 s_bmap_bits;		/* # of bits in one bitmap blocks */
	u32 s_last_bmap;
	struct buffer_head *s_bmap_bh;
	char *s_prefix;			/* Prefix for volumes and assigns. */
	char s_volume[32];		/* Volume prefix for absolute symlinks. */
	spinlock_t symlink_lock;	/* protects the previous two */
	struct super_block *sb;		/* the VFS superblock object */
	int work_queued;		/* non-zero delayed work is queued */
	struct delayed_work sb_work;	/* superblock flush delayed work */
	spinlock_t work_lock;		/* protects sb_work and work_queued */
};

#define SF_INTL		0x0001		/* International filesystem. */
#define SF_BM_VALID	0x0002		/* Bitmap is valid. */
#define SF_IMMUTABLE	0x0004		/* Protection bits cannot be changed */
#define SF_QUIET	0x0008		/* chmod errors will be not reported */
#define SF_SETUID	0x0010		/* Ignore Amiga uid */
#define SF_SETGID	0x0020		/* Ignore Amiga gid */
#define SF_SETMODE	0x0040		/* Ignore Amiga protection bits */
#define SF_MUFS		0x0100		/* Use MUFS uid/gid mapping */
#define SF_OFS		0x0200		/* Old filesystem */
#define SF_PREFIX	0x0400		/* Buffer for prefix is allocated */
#define SF_VERBOSE	0x0800		/* Talk about fs when mounting */
#define SF_NO_TRUNCATE	0x1000		/* Don't truncate filenames */

/* short cut to get to the affs specific sb data */
static inline struct affs_sb_info *AFFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

void affs_mark_sb_dirty(struct super_block *sb);

/* amigaffs.c */

extern int	affs_insert_hash(struct inode *inode, struct buffer_head *bh);
extern int	affs_remove_hash(struct inode *dir, struct buffer_head *rem_bh);
extern int	affs_remove_header(struct dentry *dentry);
extern u32	affs_checksum_block(struct super_block *sb, struct buffer_head *bh);
extern void	affs_fix_checksum(struct super_block *sb, struct buffer_head *bh);
extern void	secs_to_datestamp(time_t secs, struct affs_date *ds);
extern umode_t	prot_to_mode(u32 prot);
extern void	mode_to_prot(struct inode *inode);
__printf(3, 4)
extern void	affs_error(struct super_block *sb, const char *function,
			   const char *fmt, ...);
__printf(3, 4)
extern void	affs_warning(struct super_block *sb, const char *function,
			     const char *fmt, ...);
extern bool	affs_nofilenametruncate(const struct dentry *dentry);
extern int	affs_check_name(const unsigned char *name, int len,
				bool notruncate);
extern int	affs_copy_name(unsigned char *bstr, struct dentry *dentry);

/* bitmap. c */

extern u32	affs_count_free_blocks(struct super_block *s);
extern void	affs_free_block(struct super_block *sb, u32 block);
extern u32	affs_alloc_block(struct inode *inode, u32 goal);
extern int	affs_init_bitmap(struct super_block *sb, int *flags);
extern void	affs_free_bitmap(struct super_block *sb);

/* namei.c */

extern int	affs_hash_name(struct super_block *sb, const u8 *name, unsigned int len);
extern struct dentry *affs_lookup(struct inode *dir, struct dentry *dentry, unsigned int);
extern int	affs_unlink(struct inode *dir, struct dentry *dentry);
extern int	affs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool);
extern int	affs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
extern int	affs_rmdir(struct inode *dir, struct dentry *dentry);
extern int	affs_link(struct dentry *olddentry, struct inode *dir,
			  struct dentry *dentry);
extern int	affs_symlink(struct inode *dir, struct dentry *dentry,
			     const char *symname);
extern int	affs_rename(struct inode *old_dir, struct dentry *old_dentry,
			    struct inode *new_dir, struct dentry *new_dentry);

/* inode.c */

extern unsigned long		 affs_parent_ino(struct inode *dir);
extern struct inode		*affs_new_inode(struct inode *dir);
extern int			 affs_notify_change(struct dentry *dentry, struct iattr *attr);
extern void			 affs_evict_inode(struct inode *inode);
extern struct inode		*affs_iget(struct super_block *sb,
					unsigned long ino);
extern int			 affs_write_inode(struct inode *inode,
					struct writeback_control *wbc);
extern int			 affs_add_entry(struct inode *dir, struct inode *inode, struct dentry *dentry, s32 type);

/* file.c */

void		affs_free_prealloc(struct inode *inode);
extern void	affs_truncate(struct inode *);
int		affs_file_fsync(struct file *, loff_t, loff_t, int);

/* dir.c */

extern void   affs_dir_truncate(struct inode *);

/* jump tables */

extern const struct inode_operations	 affs_file_inode_operations;
extern const struct inode_operations	 affs_dir_inode_operations;
extern const struct inode_operations   affs_symlink_inode_operations;
extern const struct file_operations	 affs_file_operations;
extern const struct file_operations	 affs_file_operations_ofs;
extern const struct file_operations	 affs_dir_operations;
extern const struct address_space_operations	 affs_symlink_aops;
extern const struct address_space_operations	 affs_aops;
extern const struct address_space_operations	 affs_aops_ofs;

extern const struct dentry_operations	 affs_dentry_operations;
extern const struct dentry_operations	 affs_intl_dentry_operations;

static inline void
affs_set_blocksize(struct super_block *sb, int size)
{
	sb_set_blocksize(sb, size);
}
static inline struct buffer_head *
affs_bread(struct super_block *sb, int block)
{
	pr_debug("%s: %d\n", __func__, block);
	if (block >= AFFS_SB(sb)->s_reserved && block < AFFS_SB(sb)->s_partition_size)
		return sb_bread(sb, block);
	return NULL;
}
static inline struct buffer_head *
affs_getblk(struct super_block *sb, int block)
{
	pr_debug("%s: %d\n", __func__, block);
	if (block >= AFFS_SB(sb)->s_reserved && block < AFFS_SB(sb)->s_partition_size)
		return sb_getblk(sb, block);
	return NULL;
}
static inline struct buffer_head *
affs_getzeroblk(struct super_block *sb, int block)
{
	struct buffer_head *bh;
	pr_debug("%s: %d\n", __func__, block);
	if (block >= AFFS_SB(sb)->s_reserved && block < AFFS_SB(sb)->s_partition_size) {
		bh = sb_getblk(sb, block);
		lock_buffer(bh);
		memset(bh->b_data, 0 , sb->s_blocksize);
		set_buffer_uptodate(bh);
		unlock_buffer(bh);
		return bh;
	}
	return NULL;
}
static inline struct buffer_head *
affs_getemptyblk(struct super_block *sb, int block)
{
	struct buffer_head *bh;
	pr_debug("%s: %d\n", __func__, block);
	if (block >= AFFS_SB(sb)->s_reserved && block < AFFS_SB(sb)->s_partition_size) {
		bh = sb_getblk(sb, block);
		wait_on_buffer(bh);
		set_buffer_uptodate(bh);
		return bh;
	}
	return NULL;
}
static inline void
affs_brelse(struct buffer_head *bh)
{
	if (bh)
		pr_debug("%s: %lld\n", __func__, (long long) bh->b_blocknr);
	brelse(bh);
}

static inline void
affs_adjust_checksum(struct buffer_head *bh, u32 val)
{
	u32 tmp = be32_to_cpu(((__be32 *)bh->b_data)[5]);
	((__be32 *)bh->b_data)[5] = cpu_to_be32(tmp - val);
}
static inline void
affs_adjust_bitmapchecksum(struct buffer_head *bh, u32 val)
{
	u32 tmp = be32_to_cpu(((__be32 *)bh->b_data)[0]);
	((__be32 *)bh->b_data)[0] = cpu_to_be32(tmp - val);
}

static inline void
affs_lock_link(struct inode *inode)
{
	down(&AFFS_I(inode)->i_link_lock);
}
static inline void
affs_unlock_link(struct inode *inode)
{
	up(&AFFS_I(inode)->i_link_lock);
}
static inline void
affs_lock_dir(struct inode *inode)
{
	down(&AFFS_I(inode)->i_hash_lock);
}
static inline void
affs_unlock_dir(struct inode *inode)
{
	up(&AFFS_I(inode)->i_hash_lock);
}
static inline void
affs_lock_ext(struct inode *inode)
{
	down(&AFFS_I(inode)->i_ext_lock);
}
static inline void
affs_unlock_ext(struct inode *inode)
{
	up(&AFFS_I(inode)->i_ext_lock);
}
