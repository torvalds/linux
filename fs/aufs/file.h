/*
 * Copyright (C) 2005-2014 Junjiro R. Okajima
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
 * file operations
 */

#ifndef __AUFS_FILE_H__
#define __AUFS_FILE_H__

#ifdef __KERNEL__

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include "rwsem.h"

struct au_branch;
struct au_hfile {
	struct file		*hf_file;
	struct au_branch	*hf_br;
};

struct au_vdir;
struct au_fidir {
	aufs_bindex_t		fd_bbot;
	aufs_bindex_t		fd_nent;
	struct au_vdir		*fd_vdir_cache;
	struct au_hfile		fd_hfile[];
};

static inline int au_fidir_sz(int nent)
{
	AuDebugOn(nent < 0);
	return sizeof(struct au_fidir) + sizeof(struct au_hfile) * nent;
}

struct au_finfo {
	atomic_t		fi_generation;

	struct au_rwsem		fi_rwsem;
	aufs_bindex_t		fi_btop;

	/* do not union them */
	struct {				/* for non-dir */
		struct au_hfile			fi_htop;
		atomic_t			fi_mmapped;
	};
	struct au_fidir		*fi_hdir;	/* for dir only */
} ____cacheline_aligned_in_smp;

/* ---------------------------------------------------------------------- */

/* file.c */
extern const struct address_space_operations aufs_aop;
unsigned int au_file_roflags(unsigned int flags);
struct file *au_h_open(struct dentry *dentry, aufs_bindex_t bindex, int flags,
		       struct file *file, int force_wr);
int au_do_open(struct file *file, int (*open)(struct file *file, int flags),
	       struct au_fidir *fidir);
int au_reopen_nondir(struct file *file);
struct au_pin;
int au_ready_to_write(struct file *file, loff_t len, struct au_pin *pin);
int au_reval_and_lock_fdi(struct file *file, int (*reopen)(struct file *file),
			  int wlock);
int au_do_flush(struct file *file, fl_owner_t id,
		int (*flush)(struct file *file, fl_owner_t id));

/* poll.c */
#ifdef CONFIG_AUFS_POLL
unsigned int aufs_poll(struct file *file, poll_table *wait);
#endif

#ifdef CONFIG_AUFS_BR_HFSPLUS
/* hfsplus.c */
struct file *au_h_open_pre(struct dentry *dentry, aufs_bindex_t bindex,
			   int force_wr);
void au_h_open_post(struct dentry *dentry, aufs_bindex_t bindex,
		    struct file *h_file);
#else
AuStub(struct file *, au_h_open_pre, return NULL, struct dentry *dentry,
       aufs_bindex_t bindex, int force_wr)
AuStubVoid(au_h_open_post, struct dentry *dentry, aufs_bindex_t bindex,
	   struct file *h_file);
#endif

/* f_op.c */
extern const struct file_operations aufs_file_fop;
int au_do_open_nondir(struct file *file, int flags);
int aufs_release_nondir(struct inode *inode __maybe_unused, struct file *file);

/* finfo.c */
void au_hfput(struct au_hfile *hf, struct file *file);
void au_set_h_fptr(struct file *file, aufs_bindex_t bindex,
		   struct file *h_file);

void au_update_figen(struct file *file);
struct au_fidir *au_fidir_alloc(struct super_block *sb);
int au_fidir_realloc(struct au_finfo *finfo, int nbr);

void au_fi_init_once(void *_fi);
void au_finfo_fin(struct file *file);
int au_finfo_init(struct file *file, struct au_fidir *fidir);

/* ioctl.c */
long aufs_ioctl_nondir(struct file *file, unsigned int cmd, unsigned long arg);
#ifdef CONFIG_COMPAT
long aufs_compat_ioctl_dir(struct file *file, unsigned int cmd,
			   unsigned long arg);
long aufs_compat_ioctl_nondir(struct file *file, unsigned int cmd,
			      unsigned long arg);
#endif

/* ---------------------------------------------------------------------- */

static inline struct au_finfo *au_fi(struct file *file)
{
	return file->private_data;
}

/* ---------------------------------------------------------------------- */

/*
 * fi_read_lock, fi_write_lock,
 * fi_read_unlock, fi_write_unlock, fi_downgrade_lock
 */
AuSimpleRwsemFuncs(fi, struct file *f, &au_fi(f)->fi_rwsem);

#define FiMustNoWaiters(f)	AuRwMustNoWaiters(&au_fi(f)->fi_rwsem)
#define FiMustAnyLock(f)	AuRwMustAnyLock(&au_fi(f)->fi_rwsem)
#define FiMustWriteLock(f)	AuRwMustWriteLock(&au_fi(f)->fi_rwsem)

/* ---------------------------------------------------------------------- */

/* todo: hard/soft set? */
static inline aufs_bindex_t au_fbstart(struct file *file)
{
	FiMustAnyLock(file);
	return au_fi(file)->fi_btop;
}

static inline aufs_bindex_t au_fbend_dir(struct file *file)
{
	FiMustAnyLock(file);
	AuDebugOn(!au_fi(file)->fi_hdir);
	return au_fi(file)->fi_hdir->fd_bbot;
}

static inline struct au_vdir *au_fvdir_cache(struct file *file)
{
	FiMustAnyLock(file);
	AuDebugOn(!au_fi(file)->fi_hdir);
	return au_fi(file)->fi_hdir->fd_vdir_cache;
}

static inline void au_set_fbstart(struct file *file, aufs_bindex_t bindex)
{
	FiMustWriteLock(file);
	au_fi(file)->fi_btop = bindex;
}

static inline void au_set_fbend_dir(struct file *file, aufs_bindex_t bindex)
{
	FiMustWriteLock(file);
	AuDebugOn(!au_fi(file)->fi_hdir);
	au_fi(file)->fi_hdir->fd_bbot = bindex;
}

static inline void au_set_fvdir_cache(struct file *file,
				      struct au_vdir *vdir_cache)
{
	FiMustWriteLock(file);
	AuDebugOn(!au_fi(file)->fi_hdir);
	au_fi(file)->fi_hdir->fd_vdir_cache = vdir_cache;
}

static inline struct file *au_hf_top(struct file *file)
{
	FiMustAnyLock(file);
	AuDebugOn(au_fi(file)->fi_hdir);
	return au_fi(file)->fi_htop.hf_file;
}

static inline struct file *au_hf_dir(struct file *file, aufs_bindex_t bindex)
{
	FiMustAnyLock(file);
	AuDebugOn(!au_fi(file)->fi_hdir);
	return au_fi(file)->fi_hdir->fd_hfile[0 + bindex].hf_file;
}

/* todo: memory barrier? */
static inline unsigned int au_figen(struct file *f)
{
	return atomic_read(&au_fi(f)->fi_generation);
}

static inline void au_set_mmapped(struct file *f)
{
	if (atomic_inc_return(&au_fi(f)->fi_mmapped))
		return;
	pr_warn("fi_mmapped wrapped around\n");
	while (!atomic_inc_return(&au_fi(f)->fi_mmapped))
		;
}

static inline void au_unset_mmapped(struct file *f)
{
	atomic_dec(&au_fi(f)->fi_mmapped);
}

static inline int au_test_mmapped(struct file *f)
{
	return atomic_read(&au_fi(f)->fi_mmapped);
}

/* customize vma->vm_file */

static inline void au_do_vm_file_reset(struct vm_area_struct *vma,
				       struct file *file)
{
	struct file *f;

	f = vma->vm_file;
	get_file(file);
	vma->vm_file = file;
	fput(f);
}

#ifdef CONFIG_MMU
#define AuDbgVmRegion(file, vma) do {} while (0)

static inline void au_vm_file_reset(struct vm_area_struct *vma,
				    struct file *file)
{
	au_do_vm_file_reset(vma, file);
}
#else
#define AuDbgVmRegion(file, vma) \
	AuDebugOn((vma)->vm_region && (vma)->vm_region->vm_file != (file))

static inline void au_vm_file_reset(struct vm_area_struct *vma,
				    struct file *file)
{
	struct file *f;

	au_do_vm_file_reset(vma, file);
	f = vma->vm_region->vm_file;
	get_file(file);
	vma->vm_region->vm_file = file;
	fput(f);
}
#endif /* CONFIG_MMU */

/* handle vma->vm_prfile */
static inline void au_vm_prfile_set(struct vm_area_struct *vma,
				    struct file *file)
{
	get_file(file);
	vma->vm_prfile = file;
#ifndef CONFIG_MMU
	get_file(file);
	vma->vm_region->vm_prfile = file;
#endif
}

#endif /* __KERNEL__ */
#endif /* __AUFS_FILE_H__ */
