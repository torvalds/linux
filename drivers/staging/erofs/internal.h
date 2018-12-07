/* SPDX-License-Identifier: GPL-2.0
 *
 * linux/drivers/staging/erofs/internal.h
 *
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             http://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of the Linux
 * distribution for more details.
 */
#ifndef __INTERNAL_H
#define __INTERNAL_H

#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/bio.h>
#include <linux/buffer_head.h>
#include <linux/cleancache.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "erofs_fs.h"

/* redefine pr_fmt "erofs: " */
#undef pr_fmt
#define pr_fmt(fmt) "erofs: " fmt

#define errln(x, ...)   pr_err(x "\n", ##__VA_ARGS__)
#define infoln(x, ...)  pr_info(x "\n", ##__VA_ARGS__)
#ifdef CONFIG_EROFS_FS_DEBUG
#define debugln(x, ...) pr_debug(x "\n", ##__VA_ARGS__)

#define dbg_might_sleep         might_sleep
#define DBG_BUGON               BUG_ON
#else
#define debugln(x, ...)         ((void)0)

#define dbg_might_sleep()       ((void)0)
#define DBG_BUGON(x)            ((void)(x))
#endif

enum {
	FAULT_KMALLOC,
	FAULT_MAX,
};

#ifdef CONFIG_EROFS_FAULT_INJECTION
extern char *erofs_fault_name[FAULT_MAX];
#define IS_FAULT_SET(fi, type) ((fi)->inject_type & (1 << (type)))

struct erofs_fault_info {
	atomic_t inject_ops;
	unsigned int inject_rate;
	unsigned int inject_type;
};
#endif

#ifdef CONFIG_EROFS_FS_ZIP_CACHE_BIPOLAR
#define EROFS_FS_ZIP_CACHE_LVL	(2)
#elif defined(EROFS_FS_ZIP_CACHE_UNIPOLAR)
#define EROFS_FS_ZIP_CACHE_LVL	(1)
#else
#define EROFS_FS_ZIP_CACHE_LVL	(0)
#endif

#if (!defined(EROFS_FS_HAS_MANAGED_CACHE) && (EROFS_FS_ZIP_CACHE_LVL > 0))
#define EROFS_FS_HAS_MANAGED_CACHE
#endif

/* EROFS_SUPER_MAGIC_V1 to represent the whole file system */
#define EROFS_SUPER_MAGIC   EROFS_SUPER_MAGIC_V1

typedef u64 erofs_nid_t;

struct erofs_sb_info {
	/* list for all registered superblocks, mainly for shrinker */
	struct list_head list;
	struct mutex umount_mutex;

	u32 blocks;
	u32 meta_blkaddr;
#ifdef CONFIG_EROFS_FS_XATTR
	u32 xattr_blkaddr;
#endif

	/* inode slot unit size in bit shift */
	unsigned char islotbits;
#ifdef CONFIG_EROFS_FS_ZIP
	/* cluster size in bit shift */
	unsigned char clusterbits;

	/* the dedicated workstation for compression */
	struct radix_tree_root workstn_tree;

	/* threshold for decompression synchronously */
	unsigned int max_sync_decompress_pages;

#ifdef EROFS_FS_HAS_MANAGED_CACHE
	struct inode *managed_cache;
#endif

#endif

	u32 build_time_nsec;
	u64 build_time;

	/* what we really care is nid, rather than ino.. */
	erofs_nid_t root_nid;
	/* used for statfs, f_files - f_favail */
	u64 inos;

	u8 uuid[16];                    /* 128-bit uuid for volume */
	u8 volume_name[16];             /* volume name */
	char *dev_name;

	unsigned int mount_opt;
	unsigned int shrinker_run_no;

#ifdef CONFIG_EROFS_FAULT_INJECTION
	struct erofs_fault_info fault_info;	/* For fault injection */
#endif
};

#ifdef CONFIG_EROFS_FAULT_INJECTION
#define erofs_show_injection_info(type)					\
	infoln("inject %s in %s of %pS", erofs_fault_name[type],        \
		__func__, __builtin_return_address(0))

static inline bool time_to_inject(struct erofs_sb_info *sbi, int type)
{
	struct erofs_fault_info *ffi = &sbi->fault_info;

	if (!ffi->inject_rate)
		return false;

	if (!IS_FAULT_SET(ffi, type))
		return false;

	atomic_inc(&ffi->inject_ops);
	if (atomic_read(&ffi->inject_ops) >= ffi->inject_rate) {
		atomic_set(&ffi->inject_ops, 0);
		return true;
	}
	return false;
}
#else
static inline bool time_to_inject(struct erofs_sb_info *sbi, int type)
{
	return false;
}

static inline void erofs_show_injection_info(int type)
{
}
#endif

static inline void *erofs_kmalloc(struct erofs_sb_info *sbi,
					size_t size, gfp_t flags)
{
	if (time_to_inject(sbi, FAULT_KMALLOC)) {
		erofs_show_injection_info(FAULT_KMALLOC);
		return NULL;
	}
	return kmalloc(size, flags);
}

#define EROFS_SB(sb) ((struct erofs_sb_info *)(sb)->s_fs_info)
#define EROFS_I_SB(inode) ((struct erofs_sb_info *)(inode)->i_sb->s_fs_info)

/* Mount flags set via mount options or defaults */
#define EROFS_MOUNT_XATTR_USER		0x00000010
#define EROFS_MOUNT_POSIX_ACL		0x00000020
#define EROFS_MOUNT_FAULT_INJECTION	0x00000040

#define clear_opt(sbi, option)	((sbi)->mount_opt &= ~EROFS_MOUNT_##option)
#define set_opt(sbi, option)	((sbi)->mount_opt |= EROFS_MOUNT_##option)
#define test_opt(sbi, option)	((sbi)->mount_opt & EROFS_MOUNT_##option)

#ifdef CONFIG_EROFS_FS_ZIP
#define erofs_workstn_lock(sbi)         xa_lock(&(sbi)->workstn_tree)
#define erofs_workstn_unlock(sbi)       xa_unlock(&(sbi)->workstn_tree)

/* basic unit of the workstation of a super_block */
struct erofs_workgroup {
	/* the workgroup index in the workstation */
	pgoff_t index;

	/* overall workgroup reference count */
	atomic_t refcount;
};

#define EROFS_LOCKED_MAGIC     (INT_MIN | 0xE0F510CCL)

#if defined(CONFIG_SMP)
static inline bool erofs_workgroup_try_to_freeze(struct erofs_workgroup *grp,
						 int val)
{
	preempt_disable();
	if (val != atomic_cmpxchg(&grp->refcount, val, EROFS_LOCKED_MAGIC)) {
		preempt_enable();
		return false;
	}
	return true;
}

static inline void erofs_workgroup_unfreeze(struct erofs_workgroup *grp,
					    int orig_val)
{
	/*
	 * other observers should notice all modifications
	 * in the freezing period.
	 */
	smp_mb();
	atomic_set(&grp->refcount, orig_val);
	preempt_enable();
}

static inline int erofs_wait_on_workgroup_freezed(struct erofs_workgroup *grp)
{
	return atomic_cond_read_relaxed(&grp->refcount,
					VAL != EROFS_LOCKED_MAGIC);
}
#else
static inline bool erofs_workgroup_try_to_freeze(struct erofs_workgroup *grp,
						 int val)
{
	preempt_disable();
	/* no need to spin on UP platforms, let's just disable preemption. */
	if (val != atomic_read(&grp->refcount)) {
		preempt_enable();
		return false;
	}
	return true;
}

static inline void erofs_workgroup_unfreeze(struct erofs_workgroup *grp,
					    int orig_val)
{
	preempt_enable();
}

static inline int erofs_wait_on_workgroup_freezed(struct erofs_workgroup *grp)
{
	int v = atomic_read(&grp->refcount);

	/* workgroup is never freezed on uniprocessor systems */
	DBG_BUGON(v == EROFS_LOCKED_MAGIC);
	return v;
}
#endif

static inline bool erofs_workgroup_get(struct erofs_workgroup *grp, int *ocnt)
{
	int o;

repeat:
	o = erofs_wait_on_workgroup_freezed(grp);

	if (unlikely(o <= 0))
		return -1;

	if (unlikely(atomic_cmpxchg(&grp->refcount, o, o + 1) != o))
		goto repeat;

	*ocnt = o;
	return 0;
}

#define __erofs_workgroup_get(grp)	atomic_inc(&(grp)->refcount)
#define __erofs_workgroup_put(grp)	atomic_dec(&(grp)->refcount)

extern int erofs_workgroup_put(struct erofs_workgroup *grp);

extern struct erofs_workgroup *erofs_find_workgroup(
	struct super_block *sb, pgoff_t index, bool *tag);

extern int erofs_register_workgroup(struct super_block *sb,
	struct erofs_workgroup *grp, bool tag);

extern unsigned long erofs_shrink_workstation(struct erofs_sb_info *sbi,
	unsigned long nr_shrink, bool cleanup);

static inline void erofs_workstation_cleanup_all(struct super_block *sb)
{
	erofs_shrink_workstation(EROFS_SB(sb), ~0UL, true);
}

#ifdef EROFS_FS_HAS_MANAGED_CACHE
extern int erofs_try_to_free_all_cached_pages(struct erofs_sb_info *sbi,
	struct erofs_workgroup *egrp);
extern int erofs_try_to_free_cached_page(struct address_space *mapping,
	struct page *page);

#define MNGD_MAPPING(sbi)	((sbi)->managed_cache->i_mapping)
#else
#define MNGD_MAPPING(sbi)	(NULL)
#endif

#define DEFAULT_MAX_SYNC_DECOMPRESS_PAGES	3

static inline bool __should_decompress_synchronously(struct erofs_sb_info *sbi,
						     unsigned int nr)
{
	return nr <= sbi->max_sync_decompress_pages;
}

int __init z_erofs_init_zip_subsystem(void);
void z_erofs_exit_zip_subsystem(void);
#else
/* dummy initializer/finalizer for the decompression subsystem */
static inline int z_erofs_init_zip_subsystem(void) { return 0; }
static inline void z_erofs_exit_zip_subsystem(void) {}
#endif

/* we strictly follow PAGE_SIZE and no buffer head yet */
#define LOG_BLOCK_SIZE		PAGE_SHIFT

#undef LOG_SECTORS_PER_BLOCK
#define LOG_SECTORS_PER_BLOCK	(PAGE_SHIFT - 9)

#undef SECTORS_PER_BLOCK
#define SECTORS_PER_BLOCK	(1 << SECTORS_PER_BLOCK)

#define EROFS_BLKSIZ		(1 << LOG_BLOCK_SIZE)

#if (EROFS_BLKSIZ % 4096 || !EROFS_BLKSIZ)
#error erofs cannot be used in this platform
#endif

#define ROOT_NID(sb)		((sb)->root_nid)

#ifdef CONFIG_EROFS_FS_ZIP
/* hard limit of pages per compressed cluster */
#define Z_EROFS_CLUSTER_MAX_PAGES       (CONFIG_EROFS_FS_CLUSTER_PAGE_LIMIT)

/* page count of a compressed cluster */
#define erofs_clusterpages(sbi)         ((1 << (sbi)->clusterbits) / PAGE_SIZE)
#endif

typedef u64 erofs_off_t;

/* data type for filesystem-wide blocks number */
typedef u32 erofs_blk_t;

#define erofs_blknr(addr)       ((addr) / EROFS_BLKSIZ)
#define erofs_blkoff(addr)      ((addr) % EROFS_BLKSIZ)
#define blknr_to_addr(nr)       ((erofs_off_t)(nr) * EROFS_BLKSIZ)

static inline erofs_off_t iloc(struct erofs_sb_info *sbi, erofs_nid_t nid)
{
	return blknr_to_addr(sbi->meta_blkaddr) + (nid << sbi->islotbits);
}

#define inode_set_inited_xattr(inode)   (EROFS_V(inode)->flags |= 1)
#define inode_has_inited_xattr(inode)   (EROFS_V(inode)->flags & 1)

struct erofs_vnode {
	erofs_nid_t nid;
	unsigned int flags;

	unsigned char data_mapping_mode;
	/* inline size in bytes */
	unsigned char inode_isize;
	unsigned short xattr_isize;

	unsigned xattr_shared_count;
	unsigned *xattr_shared_xattrs;

	erofs_blk_t raw_blkaddr;

	/* the corresponding vfs inode */
	struct inode vfs_inode;
};

#define EROFS_V(ptr)	\
	container_of(ptr, struct erofs_vnode, vfs_inode)

#define __inode_advise(x, bit, bits) \
	(((x) >> (bit)) & ((1 << (bits)) - 1))

#define __inode_version(advise)	\
	__inode_advise(advise, EROFS_I_VERSION_BIT,	\
		EROFS_I_VERSION_BITS)

#define __inode_data_mapping(advise)	\
	__inode_advise(advise, EROFS_I_DATA_MAPPING_BIT,\
		EROFS_I_DATA_MAPPING_BITS)

static inline unsigned long inode_datablocks(struct inode *inode)
{
	/* since i_size cannot be changed */
	return DIV_ROUND_UP(inode->i_size, EROFS_BLKSIZ);
}

static inline bool is_inode_layout_plain(struct inode *inode)
{
	return EROFS_V(inode)->data_mapping_mode == EROFS_INODE_LAYOUT_PLAIN;
}

static inline bool is_inode_layout_compression(struct inode *inode)
{
	return EROFS_V(inode)->data_mapping_mode ==
					EROFS_INODE_LAYOUT_COMPRESSION;
}

static inline bool is_inode_layout_inline(struct inode *inode)
{
	return EROFS_V(inode)->data_mapping_mode == EROFS_INODE_LAYOUT_INLINE;
}

extern const struct super_operations erofs_sops;
extern const struct inode_operations erofs_dir_iops;
extern const struct file_operations erofs_dir_fops;

extern const struct address_space_operations erofs_raw_access_aops;
#ifdef CONFIG_EROFS_FS_ZIP
extern const struct address_space_operations z_erofs_vle_normalaccess_aops;
#endif

/*
 * Logical to physical block mapping, used by erofs_map_blocks()
 *
 * Different with other file systems, it is used for 2 access modes:
 *
 * 1) RAW access mode:
 *
 * Users pass a valid (m_lblk, m_lofs -- usually 0) pair,
 * and get the valid m_pblk, m_pofs and the longest m_len(in bytes).
 *
 * Note that m_lblk in the RAW access mode refers to the number of
 * the compressed ondisk block rather than the uncompressed
 * in-memory block for the compressed file.
 *
 * m_pofs equals to m_lofs except for the inline data page.
 *
 * 2) Normal access mode:
 *
 * If the inode is not compressed, it has no difference with
 * the RAW access mode. However, if the inode is compressed,
 * users should pass a valid (m_lblk, m_lofs) pair, and get
 * the needed m_pblk, m_pofs, m_len to get the compressed data
 * and the updated m_lblk, m_lofs which indicates the start
 * of the corresponding uncompressed data in the file.
 */
enum {
	BH_Zipped = BH_PrivateStart,
};

/* Has a disk mapping */
#define EROFS_MAP_MAPPED	(1 << BH_Mapped)
/* Located in metadata (could be copied from bd_inode) */
#define EROFS_MAP_META		(1 << BH_Meta)
/* The extent has been compressed */
#define EROFS_MAP_ZIPPED	(1 << BH_Zipped)

struct erofs_map_blocks {
	erofs_off_t m_pa, m_la;
	u64 m_plen, m_llen;

	unsigned int m_flags;
};

/* Flags used by erofs_map_blocks() */
#define EROFS_GET_BLOCKS_RAW    0x0001

/* data.c */
static inline struct bio *
erofs_grab_bio(struct super_block *sb,
	       erofs_blk_t blkaddr, unsigned int nr_pages,
	       bio_end_io_t endio, bool nofail)
{
	const gfp_t gfp = GFP_NOIO;
	struct bio *bio;

	do {
		if (nr_pages == 1) {
			bio = bio_alloc(gfp | (nofail ? __GFP_NOFAIL : 0), 1);
			if (unlikely(bio == NULL)) {
				DBG_BUGON(nofail);
				return ERR_PTR(-ENOMEM);
			}
			break;
		}
		bio = bio_alloc(gfp, nr_pages);
		nr_pages /= 2;
	} while (unlikely(bio == NULL));

	bio->bi_end_io = endio;
	bio_set_dev(bio, sb->s_bdev);
	bio->bi_iter.bi_sector = (sector_t)blkaddr << LOG_SECTORS_PER_BLOCK;
	return bio;
}

static inline void __submit_bio(struct bio *bio, unsigned op, unsigned op_flags)
{
	bio_set_op_attrs(bio, op, op_flags);
	submit_bio(bio);
}

#ifndef CONFIG_EROFS_FS_IO_MAX_RETRIES
#define EROFS_IO_MAX_RETRIES_NOFAIL	0
#else
#define EROFS_IO_MAX_RETRIES_NOFAIL	CONFIG_EROFS_FS_IO_MAX_RETRIES
#endif

extern struct page *__erofs_get_meta_page(struct super_block *sb,
	erofs_blk_t blkaddr, bool prio, bool nofail);

static inline struct page *erofs_get_meta_page(struct super_block *sb,
	erofs_blk_t blkaddr, bool prio)
{
	return __erofs_get_meta_page(sb, blkaddr, prio, false);
}

static inline struct page *erofs_get_meta_page_nofail(struct super_block *sb,
	erofs_blk_t blkaddr, bool prio)
{
	return __erofs_get_meta_page(sb, blkaddr, prio, true);
}

extern int erofs_map_blocks(struct inode *, struct erofs_map_blocks *, int);
extern int erofs_map_blocks_iter(struct inode *, struct erofs_map_blocks *,
	struct page **, int);

struct erofs_map_blocks_iter {
	struct erofs_map_blocks map;
	struct page *mpage;
};


static inline struct page *
erofs_get_inline_page(struct inode *inode,
		      erofs_blk_t blkaddr)
{
	return erofs_get_meta_page(inode->i_sb,
		blkaddr, S_ISDIR(inode->i_mode));
}

/* inode.c */
static inline unsigned long erofs_inode_hash(erofs_nid_t nid)
{
#if BITS_PER_LONG == 32
	return (nid >> 32) ^ (nid & 0xffffffff);
#else
	return nid;
#endif
}

extern struct inode *erofs_iget(struct super_block *sb,
	erofs_nid_t nid, bool dir);

/* dir.c */
int erofs_namei(struct inode *dir, struct qstr *name,
	erofs_nid_t *nid, unsigned *d_type);

#ifdef CONFIG_EROFS_FS_XATTR
/* xattr.c */
extern const struct xattr_handler *erofs_xattr_handlers[];

/* symlink and special inode */
extern const struct inode_operations erofs_symlink_xattr_iops;
extern const struct inode_operations erofs_fast_symlink_xattr_iops;
extern const struct inode_operations erofs_special_inode_operations;
#endif

static inline void set_inode_fast_symlink(struct inode *inode)
{
#ifdef CONFIG_EROFS_FS_XATTR
	inode->i_op = &erofs_fast_symlink_xattr_iops;
#else
	inode->i_op = &simple_symlink_inode_operations;
#endif
}

static inline bool is_inode_fast_symlink(struct inode *inode)
{
#ifdef CONFIG_EROFS_FS_XATTR
	return inode->i_op == &erofs_fast_symlink_xattr_iops;
#else
	return inode->i_op == &simple_symlink_inode_operations;
#endif
}

static inline void *erofs_vmap(struct page **pages, unsigned int count)
{
#ifdef CONFIG_EROFS_FS_USE_VM_MAP_RAM
	int i = 0;

	while (1) {
		void *addr = vm_map_ram(pages, count, -1, PAGE_KERNEL);
		/* retry two more times (totally 3 times) */
		if (addr != NULL || ++i >= 3)
			return addr;
		vm_unmap_aliases();
	}
	return NULL;
#else
	return vmap(pages, count, VM_MAP, PAGE_KERNEL);
#endif
}

static inline void erofs_vunmap(const void *mem, unsigned int count)
{
#ifdef CONFIG_EROFS_FS_USE_VM_MAP_RAM
	vm_unmap_ram(mem, count);
#else
	vunmap(mem);
#endif
}

/* utils.c */
extern struct page *erofs_allocpage(struct list_head *pool, gfp_t gfp);

extern void erofs_register_super(struct super_block *sb);
extern void erofs_unregister_super(struct super_block *sb);

extern unsigned long erofs_shrink_count(struct shrinker *shrink,
	struct shrink_control *sc);
extern unsigned long erofs_shrink_scan(struct shrinker *shrink,
	struct shrink_control *sc);

#ifndef lru_to_page
#define lru_to_page(head) (list_entry((head)->prev, struct page, lru))
#endif

#endif

