/*
 * Copyright (C) 2005-2017 Junjiro R. Okajima
 *
 * This program, aufs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * super_block operations
 */

#ifndef __AUFS_SUPER_H__
#define __AUFS_SUPER_H__

#ifdef __KERNEL__

#include <linux/fs.h>
#include <linux/kobject.h>
#include "hbl.h"
#include "rwsem.h"
#include "wkq.h"

/* policies to select one among multiple writable branches */
struct au_wbr_copyup_operations {
	int (*copyup)(struct dentry *dentry);
};

#define AuWbr_DIR	1		/* target is a dir */
#define AuWbr_PARENT	(1 << 1)	/* always require a parent */

#define au_ftest_wbr(flags, name)	((flags) & AuWbr_##name)
#define au_fset_wbr(flags, name)	{ (flags) |= AuWbr_##name; }
#define au_fclr_wbr(flags, name)	{ (flags) &= ~AuWbr_##name; }

struct au_wbr_create_operations {
	int (*create)(struct dentry *dentry, unsigned int flags);
	int (*init)(struct super_block *sb);
	int (*fin)(struct super_block *sb);
};

struct au_wbr_mfs {
	struct mutex	mfs_lock; /* protect this structure */
	unsigned long	mfs_jiffy;
	unsigned long	mfs_expire;
	aufs_bindex_t	mfs_bindex;

	unsigned long long	mfsrr_bytes;
	unsigned long long	mfsrr_watermark;
};

#define AuPlink_NHASH 100
static inline int au_plink_hash(ino_t ino)
{
	return ino % AuPlink_NHASH;
}

/* File-based Hierarchical Storage Management */
struct au_fhsm {
#ifdef CONFIG_AUFS_FHSM
	/* allow only one process who can receive the notification */
	spinlock_t		fhsm_spin;
	pid_t			fhsm_pid;
	wait_queue_head_t	fhsm_wqh;
	atomic_t		fhsm_readable;

	/* these are protected by si_rwsem */
	unsigned long		fhsm_expire;
	aufs_bindex_t		fhsm_bottom;
#endif
};

struct au_branch;
struct au_sbinfo {
	/* nowait tasks in the system-wide workqueue */
	struct au_nowait_tasks	si_nowait;

	/*
	 * tried sb->s_umount, but failed due to the dependecy between i_mutex.
	 * rwsem for au_sbinfo is necessary.
	 */
	struct au_rwsem		si_rwsem;

	/*
	 * dirty approach to protect sb->sb_inodes and ->s_files (gone) from
	 * remount.
	 */
	struct percpu_counter	si_ninodes, si_nfiles;

	/* branch management */
	unsigned int		si_generation;

	/* see AuSi_ flags */
	unsigned char		au_si_status;

	aufs_bindex_t		si_bbot;

	/* dirty trick to keep br_id plus */
	unsigned int		si_last_br_id :
				sizeof(aufs_bindex_t) * BITS_PER_BYTE - 1;
	struct au_branch	**si_branch;

	/* policy to select a writable branch */
	unsigned char		si_wbr_copyup;
	unsigned char		si_wbr_create;
	struct au_wbr_copyup_operations *si_wbr_copyup_ops;
	struct au_wbr_create_operations *si_wbr_create_ops;

	/* round robin */
	atomic_t		si_wbr_rr_next;

	/* most free space */
	struct au_wbr_mfs	si_wbr_mfs;

	/* File-based Hierarchical Storage Management */
	struct au_fhsm		si_fhsm;

	/* mount flags */
	/* include/asm-ia64/siginfo.h defines a macro named si_flags */
	unsigned int		si_mntflags;

	/* external inode number (bitmap and translation table) */
	vfs_readf_t		si_xread;
	vfs_writef_t		si_xwrite;
	struct file		*si_xib;
	struct mutex		si_xib_mtx; /* protect xib members */
	unsigned long		*si_xib_buf;
	unsigned long		si_xib_last_pindex;
	int			si_xib_next_bit;
	aufs_bindex_t		si_xino_brid;
	unsigned long		si_xino_jiffy;
	unsigned long		si_xino_expire;
	/* reserved for future use */
	/* unsigned long long	si_xib_limit; */	/* Max xib file size */

#ifdef CONFIG_AUFS_EXPORT
	/* i_generation */
	struct file		*si_xigen;
	atomic_t		si_xigen_next;
#endif

	/* dirty trick to suppoer atomic_open */
	struct hlist_bl_head	si_aopen;

	/* vdir parameters */
	unsigned long		si_rdcache;	/* max cache time in jiffies */
	unsigned int		si_rdblk;	/* deblk size */
	unsigned int		si_rdhash;	/* hash size */

	/*
	 * If the number of whiteouts are larger than si_dirwh, leave all of
	 * them after au_whtmp_ren to reduce the cost of rmdir(2).
	 * future fsck.aufs or kernel thread will remove them later.
	 * Otherwise, remove all whiteouts and the dir in rmdir(2).
	 */
	unsigned int		si_dirwh;

	/* pseudo_link list */
	struct hlist_bl_head	si_plink[AuPlink_NHASH];
	wait_queue_head_t	si_plink_wq;
	spinlock_t		si_plink_maint_lock;
	pid_t			si_plink_maint_pid;

	/* file list */
	struct hlist_bl_head	si_files;

	/* with/without getattr, brother of sb->s_d_op */
	struct inode_operations *si_iop_array;

	/*
	 * sysfs and lifetime management.
	 * this is not a small structure and it may be a waste of memory in case
	 * of sysfs is disabled, particulary when many aufs-es are mounted.
	 * but using sysfs is majority.
	 */
	struct kobject		si_kobj;
#ifdef CONFIG_DEBUG_FS
	struct dentry		 *si_dbgaufs;
	struct dentry		 *si_dbgaufs_plink;
	struct dentry		 *si_dbgaufs_xib;
#ifdef CONFIG_AUFS_EXPORT
	struct dentry		 *si_dbgaufs_xigen;
#endif
#endif

#ifdef CONFIG_AUFS_SBILIST
	struct hlist_bl_node	si_list;
#endif

	/* dirty, necessary for unmounting, sysfs and sysrq */
	struct super_block	*si_sb;
};

/* sbinfo status flags */
/*
 * set true when refresh_dirs() failed at remount time.
 * then try refreshing dirs at access time again.
 * if it is false, refreshing dirs at access time is unnecesary
 */
#define AuSi_FAILED_REFRESH_DIR	1
#define AuSi_FHSM		(1 << 1)	/* fhsm is active now */
#define AuSi_NO_DREVAL		(1 << 2)	/* disable all d_revalidate */

#ifndef CONFIG_AUFS_FHSM
#undef AuSi_FHSM
#define AuSi_FHSM		0
#endif

static inline unsigned char au_do_ftest_si(struct au_sbinfo *sbi,
					   unsigned int flag)
{
	AuRwMustAnyLock(&sbi->si_rwsem);
	return sbi->au_si_status & flag;
}
#define au_ftest_si(sbinfo, name)	au_do_ftest_si(sbinfo, AuSi_##name)
#define au_fset_si(sbinfo, name) do { \
	AuRwMustWriteLock(&(sbinfo)->si_rwsem); \
	(sbinfo)->au_si_status |= AuSi_##name; \
} while (0)
#define au_fclr_si(sbinfo, name) do { \
	AuRwMustWriteLock(&(sbinfo)->si_rwsem); \
	(sbinfo)->au_si_status &= ~AuSi_##name; \
} while (0)

/* ---------------------------------------------------------------------- */

/* policy to select one among writable branches */
#define AuWbrCopyup(sbinfo, ...) \
	((sbinfo)->si_wbr_copyup_ops->copyup(__VA_ARGS__))
#define AuWbrCreate(sbinfo, ...) \
	((sbinfo)->si_wbr_create_ops->create(__VA_ARGS__))

/* flags for si_read_lock()/aufs_read_lock()/di_read_lock() */
#define AuLock_DW		1		/* write-lock dentry */
#define AuLock_IR		(1 << 1)	/* read-lock inode */
#define AuLock_IW		(1 << 2)	/* write-lock inode */
#define AuLock_FLUSH		(1 << 3)	/* wait for 'nowait' tasks */
#define AuLock_DIRS		(1 << 4)	/* target is a pair of dirs */
						/* except RENAME_EXCHANGE */
#define AuLock_NOPLM		(1 << 5)	/* return err in plm mode */
#define AuLock_NOPLMW		(1 << 6)	/* wait for plm mode ends */
#define AuLock_GEN		(1 << 7)	/* test digen/iigen */
#define au_ftest_lock(flags, name)	((flags) & AuLock_##name)
#define au_fset_lock(flags, name) \
	do { (flags) |= AuLock_##name; } while (0)
#define au_fclr_lock(flags, name) \
	do { (flags) &= ~AuLock_##name; } while (0)

/* ---------------------------------------------------------------------- */

/* super.c */
extern struct file_system_type aufs_fs_type;
struct inode *au_iget_locked(struct super_block *sb, ino_t ino);
typedef unsigned long long (*au_arraycb_t)(struct super_block *sb, void *array,
					   unsigned long long max, void *arg);
void *au_array_alloc(unsigned long long *hint, au_arraycb_t cb,
		     struct super_block *sb, void *arg);
struct inode **au_iarray_alloc(struct super_block *sb, unsigned long long *max);
void au_iarray_free(struct inode **a, unsigned long long max);

/* sbinfo.c */
void au_si_free(struct kobject *kobj);
int au_si_alloc(struct super_block *sb);
int au_sbr_realloc(struct au_sbinfo *sbinfo, int nbr, int may_shrink);

unsigned int au_sigen_inc(struct super_block *sb);
aufs_bindex_t au_new_br_id(struct super_block *sb);

int si_read_lock(struct super_block *sb, int flags);
int si_write_lock(struct super_block *sb, int flags);
int aufs_read_lock(struct dentry *dentry, int flags);
void aufs_read_unlock(struct dentry *dentry, int flags);
void aufs_write_lock(struct dentry *dentry);
void aufs_write_unlock(struct dentry *dentry);
int aufs_read_and_write_lock2(struct dentry *d1, struct dentry *d2, int flags);
void aufs_read_and_write_unlock2(struct dentry *d1, struct dentry *d2);

/* wbr_policy.c */
extern struct au_wbr_copyup_operations au_wbr_copyup_ops[];
extern struct au_wbr_create_operations au_wbr_create_ops[];
int au_cpdown_dirs(struct dentry *dentry, aufs_bindex_t bdst);
int au_wbr_nonopq(struct dentry *dentry, aufs_bindex_t bindex);
int au_wbr_do_copyup_bu(struct dentry *dentry, aufs_bindex_t btop);

/* mvdown.c */
int au_mvdown(struct dentry *dentry, struct aufs_mvdown __user *arg);

#ifdef CONFIG_AUFS_FHSM
/* fhsm.c */

static inline pid_t au_fhsm_pid(struct au_fhsm *fhsm)
{
	pid_t pid;

	spin_lock(&fhsm->fhsm_spin);
	pid = fhsm->fhsm_pid;
	spin_unlock(&fhsm->fhsm_spin);

	return pid;
}

void au_fhsm_wrote(struct super_block *sb, aufs_bindex_t bindex, int force);
void au_fhsm_wrote_all(struct super_block *sb, int force);
int au_fhsm_fd(struct super_block *sb, int oflags);
int au_fhsm_br_alloc(struct au_branch *br);
void au_fhsm_set_bottom(struct super_block *sb, aufs_bindex_t bindex);
void au_fhsm_fin(struct super_block *sb);
void au_fhsm_init(struct au_sbinfo *sbinfo);
void au_fhsm_set(struct au_sbinfo *sbinfo, unsigned int sec);
void au_fhsm_show(struct seq_file *seq, struct au_sbinfo *sbinfo);
#else
AuStubVoid(au_fhsm_wrote, struct super_block *sb, aufs_bindex_t bindex,
	   int force)
AuStubVoid(au_fhsm_wrote_all, struct super_block *sb, int force)
AuStub(int, au_fhsm_fd, return -EOPNOTSUPP, struct super_block *sb, int oflags)
AuStub(pid_t, au_fhsm_pid, return 0, struct au_fhsm *fhsm)
AuStubInt0(au_fhsm_br_alloc, struct au_branch *br)
AuStubVoid(au_fhsm_set_bottom, struct super_block *sb, aufs_bindex_t bindex)
AuStubVoid(au_fhsm_fin, struct super_block *sb)
AuStubVoid(au_fhsm_init, struct au_sbinfo *sbinfo)
AuStubVoid(au_fhsm_set, struct au_sbinfo *sbinfo, unsigned int sec)
AuStubVoid(au_fhsm_show, struct seq_file *seq, struct au_sbinfo *sbinfo)
#endif

/* ---------------------------------------------------------------------- */

static inline struct au_sbinfo *au_sbi(struct super_block *sb)
{
	return sb->s_fs_info;
}

/* ---------------------------------------------------------------------- */

#ifdef CONFIG_AUFS_EXPORT
int au_test_nfsd(void);
void au_export_init(struct super_block *sb);
void au_xigen_inc(struct inode *inode);
int au_xigen_new(struct inode *inode);
int au_xigen_set(struct super_block *sb, struct file *base);
void au_xigen_clr(struct super_block *sb);

static inline int au_busy_or_stale(void)
{
	if (!au_test_nfsd())
		return -EBUSY;
	return -ESTALE;
}
#else
AuStubInt0(au_test_nfsd, void)
AuStubVoid(au_export_init, struct super_block *sb)
AuStubVoid(au_xigen_inc, struct inode *inode)
AuStubInt0(au_xigen_new, struct inode *inode)
AuStubInt0(au_xigen_set, struct super_block *sb, struct file *base)
AuStubVoid(au_xigen_clr, struct super_block *sb)
AuStub(int, au_busy_or_stale, return -EBUSY, void)
#endif /* CONFIG_AUFS_EXPORT */

/* ---------------------------------------------------------------------- */

#ifdef CONFIG_AUFS_SBILIST
/* module.c */
extern struct hlist_bl_head au_sbilist;

static inline void au_sbilist_init(void)
{
	INIT_HLIST_BL_HEAD(&au_sbilist);
}

static inline void au_sbilist_add(struct super_block *sb)
{
	au_hbl_add(&au_sbi(sb)->si_list, &au_sbilist);
}

static inline void au_sbilist_del(struct super_block *sb)
{
	au_hbl_del(&au_sbi(sb)->si_list, &au_sbilist);
}

#ifdef CONFIG_AUFS_MAGIC_SYSRQ
static inline void au_sbilist_lock(void)
{
	hlist_bl_lock(&au_sbilist);
}

static inline void au_sbilist_unlock(void)
{
	hlist_bl_unlock(&au_sbilist);
}
#define AuGFP_SBILIST	GFP_ATOMIC
#else
AuStubVoid(au_sbilist_lock, void)
AuStubVoid(au_sbilist_unlock, void)
#define AuGFP_SBILIST	GFP_NOFS
#endif /* CONFIG_AUFS_MAGIC_SYSRQ */
#else
AuStubVoid(au_sbilist_init, void)
AuStubVoid(au_sbilist_add, struct super_block *sb)
AuStubVoid(au_sbilist_del, struct super_block *sb)
AuStubVoid(au_sbilist_lock, void)
AuStubVoid(au_sbilist_unlock, void)
#define AuGFP_SBILIST	GFP_NOFS
#endif

/* ---------------------------------------------------------------------- */

static inline void dbgaufs_si_null(struct au_sbinfo *sbinfo)
{
	/*
	 * This function is a dynamic '__init' function actually,
	 * so the tiny check for si_rwsem is unnecessary.
	 */
	/* AuRwMustWriteLock(&sbinfo->si_rwsem); */
#ifdef CONFIG_DEBUG_FS
	sbinfo->si_dbgaufs = NULL;
	sbinfo->si_dbgaufs_plink = NULL;
	sbinfo->si_dbgaufs_xib = NULL;
#ifdef CONFIG_AUFS_EXPORT
	sbinfo->si_dbgaufs_xigen = NULL;
#endif
#endif
}

/* ---------------------------------------------------------------------- */

/* current->atomic_flags */
/* this value should never corrupt the ones defined in linux/sched.h */
#define PFA_AUFS	7

TASK_PFA_TEST(AUFS, test_aufs)	/* task_test_aufs */
TASK_PFA_SET(AUFS, aufs)	/* task_set_aufs */
TASK_PFA_CLEAR(AUFS, aufs)	/* task_clear_aufs */

static inline int si_pid_test(struct super_block *sb)
{
	return !!task_test_aufs(current);
}

static inline void si_pid_clr(struct super_block *sb)
{
	AuDebugOn(!task_test_aufs(current));
	task_clear_aufs(current);
}

static inline void si_pid_set(struct super_block *sb)
{
	AuDebugOn(task_test_aufs(current));
	task_set_aufs(current);
}

/* ---------------------------------------------------------------------- */

/* lock superblock. mainly for entry point functions */
#define __si_read_lock(sb)	au_rw_read_lock(&au_sbi(sb)->si_rwsem)
#define __si_write_lock(sb)	au_rw_write_lock(&au_sbi(sb)->si_rwsem)
#define __si_read_trylock(sb)	au_rw_read_trylock(&au_sbi(sb)->si_rwsem)
#define __si_write_trylock(sb)	au_rw_write_trylock(&au_sbi(sb)->si_rwsem)
/*
#define __si_read_trylock_nested(sb) \
	au_rw_read_trylock_nested(&au_sbi(sb)->si_rwsem)
#define __si_write_trylock_nested(sb) \
	au_rw_write_trylock_nested(&au_sbi(sb)->si_rwsem)
*/

#define __si_read_unlock(sb)	au_rw_read_unlock(&au_sbi(sb)->si_rwsem)
#define __si_write_unlock(sb)	au_rw_write_unlock(&au_sbi(sb)->si_rwsem)
#define __si_downgrade_lock(sb)	au_rw_dgrade_lock(&au_sbi(sb)->si_rwsem)

#define SiMustNoWaiters(sb)	AuRwMustNoWaiters(&au_sbi(sb)->si_rwsem)
#define SiMustAnyLock(sb)	AuRwMustAnyLock(&au_sbi(sb)->si_rwsem)
#define SiMustWriteLock(sb)	AuRwMustWriteLock(&au_sbi(sb)->si_rwsem)

static inline void si_noflush_read_lock(struct super_block *sb)
{
	__si_read_lock(sb);
	si_pid_set(sb);
}

static inline int si_noflush_read_trylock(struct super_block *sb)
{
	int locked;

	locked = __si_read_trylock(sb);
	if (locked)
		si_pid_set(sb);
	return locked;
}

static inline void si_noflush_write_lock(struct super_block *sb)
{
	__si_write_lock(sb);
	si_pid_set(sb);
}

static inline int si_noflush_write_trylock(struct super_block *sb)
{
	int locked;

	locked = __si_write_trylock(sb);
	if (locked)
		si_pid_set(sb);
	return locked;
}

#if 0 /* reserved */
static inline int si_read_trylock(struct super_block *sb, int flags)
{
	if (au_ftest_lock(flags, FLUSH))
		au_nwt_flush(&au_sbi(sb)->si_nowait);
	return si_noflush_read_trylock(sb);
}
#endif

static inline void si_read_unlock(struct super_block *sb)
{
	si_pid_clr(sb);
	__si_read_unlock(sb);
}

#if 0 /* reserved */
static inline int si_write_trylock(struct super_block *sb, int flags)
{
	if (au_ftest_lock(flags, FLUSH))
		au_nwt_flush(&au_sbi(sb)->si_nowait);
	return si_noflush_write_trylock(sb);
}
#endif

static inline void si_write_unlock(struct super_block *sb)
{
	si_pid_clr(sb);
	__si_write_unlock(sb);
}

#if 0 /* reserved */
static inline void si_downgrade_lock(struct super_block *sb)
{
	__si_downgrade_lock(sb);
}
#endif

/* ---------------------------------------------------------------------- */

static inline aufs_bindex_t au_sbbot(struct super_block *sb)
{
	SiMustAnyLock(sb);
	return au_sbi(sb)->si_bbot;
}

static inline unsigned int au_mntflags(struct super_block *sb)
{
	SiMustAnyLock(sb);
	return au_sbi(sb)->si_mntflags;
}

static inline unsigned int au_sigen(struct super_block *sb)
{
	SiMustAnyLock(sb);
	return au_sbi(sb)->si_generation;
}

static inline unsigned long long au_ninodes(struct super_block *sb)
{
	s64 n = percpu_counter_sum(&au_sbi(sb)->si_ninodes);

	BUG_ON(n < 0);
	return n;
}

static inline void au_ninodes_inc(struct super_block *sb)
{
	percpu_counter_inc(&au_sbi(sb)->si_ninodes);
}

static inline void au_ninodes_dec(struct super_block *sb)
{
	percpu_counter_dec(&au_sbi(sb)->si_ninodes);
}

static inline unsigned long long au_nfiles(struct super_block *sb)
{
	s64 n = percpu_counter_sum(&au_sbi(sb)->si_nfiles);

	BUG_ON(n < 0);
	return n;
}

static inline void au_nfiles_inc(struct super_block *sb)
{
	percpu_counter_inc(&au_sbi(sb)->si_nfiles);
}

static inline void au_nfiles_dec(struct super_block *sb)
{
	percpu_counter_dec(&au_sbi(sb)->si_nfiles);
}

static inline struct au_branch *au_sbr(struct super_block *sb,
				       aufs_bindex_t bindex)
{
	SiMustAnyLock(sb);
	return au_sbi(sb)->si_branch[0 + bindex];
}

static inline void au_xino_brid_set(struct super_block *sb, aufs_bindex_t brid)
{
	SiMustWriteLock(sb);
	au_sbi(sb)->si_xino_brid = brid;
}

static inline aufs_bindex_t au_xino_brid(struct super_block *sb)
{
	SiMustAnyLock(sb);
	return au_sbi(sb)->si_xino_brid;
}

#endif /* __KERNEL__ */
#endif /* __AUFS_SUPER_H__ */
