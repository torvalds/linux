// SPDX-License-Identifier: GPL-2.0
/* mm/ashmem.c
 *
 * Anonymous Shared Memory Subsystem, ashmem
 *
 * Copyright (C) 2008 Google, Inc.
 *
 * Robert Love <rlove@google.com>
 */

#define pr_fmt(fmt) "ashmem: " fmt

#include <linux/init.h>
#include <linux/export.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/falloc.h>
#include <linux/miscdevice.h>
#include <linux/security.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/uaccess.h>
#include <linux/personality.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/shmem_fs.h>
#include "ashmem.h"

#define ASHMEM_NAME_PREFIX "dev/ashmem/"
#define ASHMEM_NAME_PREFIX_LEN (sizeof(ASHMEM_NAME_PREFIX) - 1)
#define ASHMEM_FULL_NAME_LEN (ASHMEM_NAME_LEN + ASHMEM_NAME_PREFIX_LEN)

/**
 * struct ashmem_area - The anonymous shared memory area
 * @name:		The optional name in /proc/pid/maps
 * @unpinned_list:	The list of all ashmem areas
 * @file:		The shmem-based backing file
 * @size:		The size of the mapping, in bytes
 * @prot_mask:		The allowed protection bits, as vm_flags
 *
 * The lifecycle of this structure is from our parent file's open() until
 * its release(). It is also protected by 'ashmem_mutex'
 *
 * Warning: Mappings do NOT pin this structure; It dies on close()
 */
struct ashmem_area {
	char name[ASHMEM_FULL_NAME_LEN];
	struct list_head unpinned_list;
	struct file *file;
	size_t size;
	unsigned long prot_mask;
};

/**
 * struct ashmem_range - A range of unpinned/evictable pages
 * @lru:	         The entry in the LRU list
 * @unpinned:	         The entry in its area's unpinned list
 * @asma:	         The associated anonymous shared memory area.
 * @pgstart:	         The starting page (inclusive)
 * @pgend:	         The ending page (inclusive)
 * @purged:	         The purge status (ASHMEM_NOT or ASHMEM_WAS_PURGED)
 *
 * The lifecycle of this structure is from unpin to pin.
 * It is protected by 'ashmem_mutex'
 */
struct ashmem_range {
	struct list_head lru;
	struct list_head unpinned;
	struct ashmem_area *asma;
	size_t pgstart;
	size_t pgend;
	unsigned int purged;
};

/* LRU list of unpinned pages, protected by ashmem_mutex */
static LIST_HEAD(ashmem_lru_list);

static atomic_t ashmem_shrink_inflight = ATOMIC_INIT(0);
static DECLARE_WAIT_QUEUE_HEAD(ashmem_shrink_wait);

/*
 * long lru_count - The count of pages on our LRU list.
 *
 * This is protected by ashmem_mutex.
 */
static unsigned long lru_count;

/*
 * ashmem_mutex - protects the list of and each individual ashmem_area
 *
 * Lock Ordering: ashmex_mutex -> i_mutex -> i_alloc_sem
 */
static DEFINE_MUTEX(ashmem_mutex);

static struct kmem_cache *ashmem_area_cachep __read_mostly;
static struct kmem_cache *ashmem_range_cachep __read_mostly;

static inline unsigned long range_size(struct ashmem_range *range)
{
	return range->pgend - range->pgstart + 1;
}

static inline bool range_on_lru(struct ashmem_range *range)
{
	return range->purged == ASHMEM_NOT_PURGED;
}

static inline bool page_range_subsumes_range(struct ashmem_range *range,
					     size_t start, size_t end)
{
	return (range->pgstart >= start) && (range->pgend <= end);
}

static inline bool page_range_subsumed_by_range(struct ashmem_range *range,
						size_t start, size_t end)
{
	return (range->pgstart <= start) && (range->pgend >= end);
}

static inline bool page_in_range(struct ashmem_range *range, size_t page)
{
	return (range->pgstart <= page) && (range->pgend >= page);
}

static inline bool page_range_in_range(struct ashmem_range *range,
				       size_t start, size_t end)
{
	return page_in_range(range, start) || page_in_range(range, end) ||
		page_range_subsumes_range(range, start, end);
}

static inline bool range_before_page(struct ashmem_range *range, size_t page)
{
	return range->pgend < page;
}

#define PROT_MASK		(PROT_EXEC | PROT_READ | PROT_WRITE)

/**
 * lru_add() - Adds a range of memory to the LRU list
 * @range:     The memory range being added.
 *
 * The range is first added to the end (tail) of the LRU list.
 * After this, the size of the range is added to @lru_count
 */
static inline void lru_add(struct ashmem_range *range)
{
	list_add_tail(&range->lru, &ashmem_lru_list);
	lru_count += range_size(range);
}

/**
 * lru_del() - Removes a range of memory from the LRU list
 * @range:     The memory range being removed
 *
 * The range is first deleted from the LRU list.
 * After this, the size of the range is removed from @lru_count
 */
static inline void lru_del(struct ashmem_range *range)
{
	list_del(&range->lru);
	lru_count -= range_size(range);
}

/**
 * range_alloc() - Allocates and initializes a new ashmem_range structure
 * @asma:	   The associated ashmem_area
 * @prev_range:	   The previous ashmem_range in the sorted asma->unpinned list
 * @purged:	   Initial purge status (ASMEM_NOT_PURGED or ASHMEM_WAS_PURGED)
 * @start:	   The starting page (inclusive)
 * @end:	   The ending page (inclusive)
 *
 * This function is protected by ashmem_mutex.
 */
static void range_alloc(struct ashmem_area *asma,
			struct ashmem_range *prev_range, unsigned int purged,
			size_t start, size_t end,
			struct ashmem_range **new_range)
{
	struct ashmem_range *range = *new_range;

	*new_range = NULL;
	range->asma = asma;
	range->pgstart = start;
	range->pgend = end;
	range->purged = purged;

	list_add_tail(&range->unpinned, &prev_range->unpinned);

	if (range_on_lru(range))
		lru_add(range);
}

/**
 * range_del() - Deletes and dealloctes an ashmem_range structure
 * @range:	 The associated ashmem_range that has previously been allocated
 */
static void range_del(struct ashmem_range *range)
{
	list_del(&range->unpinned);
	if (range_on_lru(range))
		lru_del(range);
	kmem_cache_free(ashmem_range_cachep, range);
}

/**
 * range_shrink() - Shrinks an ashmem_range
 * @range:	    The associated ashmem_range being shrunk
 * @start:	    The starting byte of the new range
 * @end:	    The ending byte of the new range
 *
 * This does not modify the data inside the existing range in any way - It
 * simply shrinks the boundaries of the range.
 *
 * Theoretically, with a little tweaking, this could eventually be changed
 * to range_resize, and expand the lru_count if the new range is larger.
 */
static inline void range_shrink(struct ashmem_range *range,
				size_t start, size_t end)
{
	size_t pre = range_size(range);

	range->pgstart = start;
	range->pgend = end;

	if (range_on_lru(range))
		lru_count -= pre - range_size(range);
}

/**
 * ashmem_open() - Opens an Anonymous Shared Memory structure
 * @inode:	   The backing file's index node(?)
 * @file:	   The backing file
 *
 * Please note that the ashmem_area is not returned by this function - It is
 * instead written to "file->private_data".
 *
 * Return: 0 if successful, or another code if unsuccessful.
 */
static int ashmem_open(struct inode *inode, struct file *file)
{
	struct ashmem_area *asma;
	int ret;

	ret = generic_file_open(inode, file);
	if (ret)
		return ret;

	asma = kmem_cache_zalloc(ashmem_area_cachep, GFP_KERNEL);
	if (!asma)
		return -ENOMEM;

	INIT_LIST_HEAD(&asma->unpinned_list);
	memcpy(asma->name, ASHMEM_NAME_PREFIX, ASHMEM_NAME_PREFIX_LEN);
	asma->prot_mask = PROT_MASK;
	file->private_data = asma;

	return 0;
}

/**
 * ashmem_release() - Releases an Anonymous Shared Memory structure
 * @ignored:	      The backing file's Index Node(?) - It is ignored here.
 * @file:	      The backing file
 *
 * Return: 0 if successful. If it is anything else, go have a coffee and
 * try again.
 */
static int ashmem_release(struct inode *ignored, struct file *file)
{
	struct ashmem_area *asma = file->private_data;
	struct ashmem_range *range, *next;

	mutex_lock(&ashmem_mutex);
	list_for_each_entry_safe(range, next, &asma->unpinned_list, unpinned)
		range_del(range);
	mutex_unlock(&ashmem_mutex);

	if (asma->file)
		fput(asma->file);
	kmem_cache_free(ashmem_area_cachep, asma);

	return 0;
}

static ssize_t ashmem_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct ashmem_area *asma = iocb->ki_filp->private_data;
	int ret = 0;

	mutex_lock(&ashmem_mutex);

	/* If size is not set, or set to 0, always return EOF. */
	if (asma->size == 0)
		goto out_unlock;

	if (!asma->file) {
		ret = -EBADF;
		goto out_unlock;
	}

	/*
	 * asma and asma->file are used outside the lock here.  We assume
	 * once asma->file is set it will never be changed, and will not
	 * be destroyed until all references to the file are dropped and
	 * ashmem_release is called.
	 */
	mutex_unlock(&ashmem_mutex);
	ret = vfs_iter_read(asma->file, iter, &iocb->ki_pos, 0);
	mutex_lock(&ashmem_mutex);
	if (ret > 0)
		asma->file->f_pos = iocb->ki_pos;
out_unlock:
	mutex_unlock(&ashmem_mutex);
	return ret;
}

static loff_t ashmem_llseek(struct file *file, loff_t offset, int origin)
{
	struct ashmem_area *asma = file->private_data;
	loff_t ret;

	mutex_lock(&ashmem_mutex);

	if (asma->size == 0) {
		mutex_unlock(&ashmem_mutex);
		return -EINVAL;
	}

	if (!asma->file) {
		mutex_unlock(&ashmem_mutex);
		return -EBADF;
	}

	mutex_unlock(&ashmem_mutex);

	ret = vfs_llseek(asma->file, offset, origin);
	if (ret < 0)
		return ret;

	/** Copy f_pos from backing file, since f_ops->llseek() sets it */
	file->f_pos = asma->file->f_pos;
	return ret;
}

static inline vm_flags_t calc_vm_may_flags(unsigned long prot)
{
	return _calc_vm_trans(prot, PROT_READ,  VM_MAYREAD) |
	       _calc_vm_trans(prot, PROT_WRITE, VM_MAYWRITE) |
	       _calc_vm_trans(prot, PROT_EXEC,  VM_MAYEXEC);
}

static int ashmem_vmfile_mmap(struct file *file, struct vm_area_struct *vma)
{
	/* do not allow to mmap ashmem backing shmem file directly */
	return -EPERM;
}

static unsigned long
ashmem_vmfile_get_unmapped_area(struct file *file, unsigned long addr,
				unsigned long len, unsigned long pgoff,
				unsigned long flags)
{
	return current->mm->get_unmapped_area(file, addr, len, pgoff, flags);
}

static int ashmem_mmap(struct file *file, struct vm_area_struct *vma)
{
	static struct file_operations vmfile_fops;
	struct ashmem_area *asma = file->private_data;
	int ret = 0;

	mutex_lock(&ashmem_mutex);

	/* user needs to SET_SIZE before mapping */
	if (!asma->size) {
		ret = -EINVAL;
		goto out;
	}

	/* requested mapping size larger than object size */
	if (vma->vm_end - vma->vm_start > PAGE_ALIGN(asma->size)) {
		ret = -EINVAL;
		goto out;
	}

	/* requested protection bits must match our allowed protection mask */
	if ((vma->vm_flags & ~calc_vm_prot_bits(asma->prot_mask, 0)) &
	    calc_vm_prot_bits(PROT_MASK, 0)) {
		ret = -EPERM;
		goto out;
	}
	vma->vm_flags &= ~calc_vm_may_flags(~asma->prot_mask);

	if (!asma->file) {
		char *name = ASHMEM_NAME_DEF;
		struct file *vmfile;

		if (asma->name[ASHMEM_NAME_PREFIX_LEN] != '\0')
			name = asma->name;

		/* ... and allocate the backing shmem file */
		vmfile = shmem_file_setup(name, asma->size, vma->vm_flags);
		if (IS_ERR(vmfile)) {
			ret = PTR_ERR(vmfile);
			goto out;
		}
		vmfile->f_mode |= FMODE_LSEEK;
		asma->file = vmfile;
		/*
		 * override mmap operation of the vmfile so that it can't be
		 * remapped which would lead to creation of a new vma with no
		 * asma permission checks. Have to override get_unmapped_area
		 * as well to prevent VM_BUG_ON check for f_ops modification.
		 */
		if (!vmfile_fops.mmap) {
			vmfile_fops = *vmfile->f_op;
			vmfile_fops.mmap = ashmem_vmfile_mmap;
			vmfile_fops.get_unmapped_area =
					ashmem_vmfile_get_unmapped_area;
		}
		vmfile->f_op = &vmfile_fops;
	}
	get_file(asma->file);

	/*
	 * XXX - Reworked to use shmem_zero_setup() instead of
	 * shmem_set_file while we're in staging. -jstultz
	 */
	if (vma->vm_flags & VM_SHARED) {
		ret = shmem_zero_setup(vma);
		if (ret) {
			fput(asma->file);
			goto out;
		}
	} else {
		vma_set_anonymous(vma);
	}

	if (vma->vm_file)
		fput(vma->vm_file);
	vma->vm_file = asma->file;

out:
	mutex_unlock(&ashmem_mutex);
	return ret;
}

/*
 * ashmem_shrink - our cache shrinker, called from mm/vmscan.c
 *
 * 'nr_to_scan' is the number of objects to scan for freeing.
 *
 * 'gfp_mask' is the mask of the allocation that got us into this mess.
 *
 * Return value is the number of objects freed or -1 if we cannot
 * proceed without risk of deadlock (due to gfp_mask).
 *
 * We approximate LRU via least-recently-unpinned, jettisoning unpinned partial
 * chunks of ashmem regions LRU-wise one-at-a-time until we hit 'nr_to_scan'
 * pages freed.
 */
static unsigned long
ashmem_shrink_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	unsigned long freed = 0;

	/* We might recurse into filesystem code, so bail out if necessary */
	if (!(sc->gfp_mask & __GFP_FS))
		return SHRINK_STOP;

	if (!mutex_trylock(&ashmem_mutex))
		return -1;

	while (!list_empty(&ashmem_lru_list)) {
		struct ashmem_range *range =
			list_first_entry(&ashmem_lru_list, typeof(*range), lru);
		loff_t start = range->pgstart * PAGE_SIZE;
		loff_t end = (range->pgend + 1) * PAGE_SIZE;
		struct file *f = range->asma->file;

		get_file(f);
		atomic_inc(&ashmem_shrink_inflight);
		range->purged = ASHMEM_WAS_PURGED;
		lru_del(range);

		freed += range_size(range);
		mutex_unlock(&ashmem_mutex);
		f->f_op->fallocate(f,
				   FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
				   start, end - start);
		fput(f);
		if (atomic_dec_and_test(&ashmem_shrink_inflight))
			wake_up_all(&ashmem_shrink_wait);
		if (!mutex_trylock(&ashmem_mutex))
			goto out;
		if (--sc->nr_to_scan <= 0)
			break;
	}
	mutex_unlock(&ashmem_mutex);
out:
	return freed;
}

static unsigned long
ashmem_shrink_count(struct shrinker *shrink, struct shrink_control *sc)
{
	/*
	 * note that lru_count is count of pages on the lru, not a count of
	 * objects on the list. This means the scan function needs to return the
	 * number of pages freed, not the number of objects scanned.
	 */
	return lru_count;
}

static struct shrinker ashmem_shrinker = {
	.count_objects = ashmem_shrink_count,
	.scan_objects = ashmem_shrink_scan,
	/*
	 * XXX (dchinner): I wish people would comment on why they need on
	 * significant changes to the default value here
	 */
	.seeks = DEFAULT_SEEKS * 4,
};

static int set_prot_mask(struct ashmem_area *asma, unsigned long prot)
{
	int ret = 0;

	mutex_lock(&ashmem_mutex);

	/* the user can only remove, not add, protection bits */
	if ((asma->prot_mask & prot) != prot) {
		ret = -EINVAL;
		goto out;
	}

	/* does the application expect PROT_READ to imply PROT_EXEC? */
	if ((prot & PROT_READ) && (current->personality & READ_IMPLIES_EXEC))
		prot |= PROT_EXEC;

	asma->prot_mask = prot;

out:
	mutex_unlock(&ashmem_mutex);
	return ret;
}

static int set_name(struct ashmem_area *asma, void __user *name)
{
	int len;
	int ret = 0;
	char local_name[ASHMEM_NAME_LEN];

	/*
	 * Holding the ashmem_mutex while doing a copy_from_user might cause
	 * an data abort which would try to access mmap_sem. If another
	 * thread has invoked ashmem_mmap then it will be holding the
	 * semaphore and will be waiting for ashmem_mutex, there by leading to
	 * deadlock. We'll release the mutex  and take the name to a local
	 * variable that does not need protection and later copy the local
	 * variable to the structure member with lock held.
	 */
	len = strncpy_from_user(local_name, name, ASHMEM_NAME_LEN);
	if (len < 0)
		return len;
	if (len == ASHMEM_NAME_LEN)
		local_name[ASHMEM_NAME_LEN - 1] = '\0';
	mutex_lock(&ashmem_mutex);
	/* cannot change an existing mapping's name */
	if (asma->file)
		ret = -EINVAL;
	else
		strcpy(asma->name + ASHMEM_NAME_PREFIX_LEN, local_name);

	mutex_unlock(&ashmem_mutex);
	return ret;
}

static int get_name(struct ashmem_area *asma, void __user *name)
{
	int ret = 0;
	size_t len;
	/*
	 * Have a local variable to which we'll copy the content
	 * from asma with the lock held. Later we can copy this to the user
	 * space safely without holding any locks. So even if we proceed to
	 * wait for mmap_sem, it won't lead to deadlock.
	 */
	char local_name[ASHMEM_NAME_LEN];

	mutex_lock(&ashmem_mutex);
	if (asma->name[ASHMEM_NAME_PREFIX_LEN] != '\0') {
		/*
		 * Copying only `len', instead of ASHMEM_NAME_LEN, bytes
		 * prevents us from revealing one user's stack to another.
		 */
		len = strlen(asma->name + ASHMEM_NAME_PREFIX_LEN) + 1;
		memcpy(local_name, asma->name + ASHMEM_NAME_PREFIX_LEN, len);
	} else {
		len = sizeof(ASHMEM_NAME_DEF);
		memcpy(local_name, ASHMEM_NAME_DEF, len);
	}
	mutex_unlock(&ashmem_mutex);

	/*
	 * Now we are just copying from the stack variable to userland
	 * No lock held
	 */
	if (copy_to_user(name, local_name, len))
		ret = -EFAULT;
	return ret;
}

/*
 * ashmem_pin - pin the given ashmem region, returning whether it was
 * previously purged (ASHMEM_WAS_PURGED) or not (ASHMEM_NOT_PURGED).
 *
 * Caller must hold ashmem_mutex.
 */
static int ashmem_pin(struct ashmem_area *asma, size_t pgstart, size_t pgend,
		      struct ashmem_range **new_range)
{
	struct ashmem_range *range, *next;
	int ret = ASHMEM_NOT_PURGED;

	list_for_each_entry_safe(range, next, &asma->unpinned_list, unpinned) {
		/* moved past last applicable page; we can short circuit */
		if (range_before_page(range, pgstart))
			break;

		/*
		 * The user can ask us to pin pages that span multiple ranges,
		 * or to pin pages that aren't even unpinned, so this is messy.
		 *
		 * Four cases:
		 * 1. The requested range subsumes an existing range, so we
		 *    just remove the entire matching range.
		 * 2. The requested range overlaps the start of an existing
		 *    range, so we just update that range.
		 * 3. The requested range overlaps the end of an existing
		 *    range, so we just update that range.
		 * 4. The requested range punches a hole in an existing range,
		 *    so we have to update one side of the range and then
		 *    create a new range for the other side.
		 */
		if (page_range_in_range(range, pgstart, pgend)) {
			ret |= range->purged;

			/* Case #1: Easy. Just nuke the whole thing. */
			if (page_range_subsumes_range(range, pgstart, pgend)) {
				range_del(range);
				continue;
			}

			/* Case #2: We overlap from the start, so adjust it */
			if (range->pgstart >= pgstart) {
				range_shrink(range, pgend + 1, range->pgend);
				continue;
			}

			/* Case #3: We overlap from the rear, so adjust it */
			if (range->pgend <= pgend) {
				range_shrink(range, range->pgstart,
					     pgstart - 1);
				continue;
			}

			/*
			 * Case #4: We eat a chunk out of the middle. A bit
			 * more complicated, we allocate a new range for the
			 * second half and adjust the first chunk's endpoint.
			 */
			range_alloc(asma, range, range->purged,
				    pgend + 1, range->pgend, new_range);
			range_shrink(range, range->pgstart, pgstart - 1);
			break;
		}
	}

	return ret;
}

/*
 * ashmem_unpin - unpin the given range of pages. Returns zero on success.
 *
 * Caller must hold ashmem_mutex.
 */
static int ashmem_unpin(struct ashmem_area *asma, size_t pgstart, size_t pgend,
			struct ashmem_range **new_range)
{
	struct ashmem_range *range, *next;
	unsigned int purged = ASHMEM_NOT_PURGED;

restart:
	list_for_each_entry_safe(range, next, &asma->unpinned_list, unpinned) {
		/* short circuit: this is our insertion point */
		if (range_before_page(range, pgstart))
			break;

		/*
		 * The user can ask us to unpin pages that are already entirely
		 * or partially pinned. We handle those two cases here.
		 */
		if (page_range_subsumed_by_range(range, pgstart, pgend))
			return 0;
		if (page_range_in_range(range, pgstart, pgend)) {
			pgstart = min(range->pgstart, pgstart);
			pgend = max(range->pgend, pgend);
			purged |= range->purged;
			range_del(range);
			goto restart;
		}
	}

	range_alloc(asma, range, purged, pgstart, pgend, new_range);
	return 0;
}

/*
 * ashmem_get_pin_status - Returns ASHMEM_IS_UNPINNED if _any_ pages in the
 * given interval are unpinned and ASHMEM_IS_PINNED otherwise.
 *
 * Caller must hold ashmem_mutex.
 */
static int ashmem_get_pin_status(struct ashmem_area *asma, size_t pgstart,
				 size_t pgend)
{
	struct ashmem_range *range;
	int ret = ASHMEM_IS_PINNED;

	list_for_each_entry(range, &asma->unpinned_list, unpinned) {
		if (range_before_page(range, pgstart))
			break;
		if (page_range_in_range(range, pgstart, pgend)) {
			ret = ASHMEM_IS_UNPINNED;
			break;
		}
	}

	return ret;
}

static int ashmem_pin_unpin(struct ashmem_area *asma, unsigned long cmd,
			    void __user *p)
{
	struct ashmem_pin pin;
	size_t pgstart, pgend;
	int ret = -EINVAL;
	struct ashmem_range *range = NULL;

	if (copy_from_user(&pin, p, sizeof(pin)))
		return -EFAULT;

	if (cmd == ASHMEM_PIN || cmd == ASHMEM_UNPIN) {
		range = kmem_cache_zalloc(ashmem_range_cachep, GFP_KERNEL);
		if (!range)
			return -ENOMEM;
	}

	mutex_lock(&ashmem_mutex);
	wait_event(ashmem_shrink_wait, !atomic_read(&ashmem_shrink_inflight));

	if (!asma->file)
		goto out_unlock;

	/* per custom, you can pass zero for len to mean "everything onward" */
	if (!pin.len)
		pin.len = PAGE_ALIGN(asma->size) - pin.offset;

	if ((pin.offset | pin.len) & ~PAGE_MASK)
		goto out_unlock;

	if (((__u32)-1) - pin.offset < pin.len)
		goto out_unlock;

	if (PAGE_ALIGN(asma->size) < pin.offset + pin.len)
		goto out_unlock;

	pgstart = pin.offset / PAGE_SIZE;
	pgend = pgstart + (pin.len / PAGE_SIZE) - 1;

	switch (cmd) {
	case ASHMEM_PIN:
		ret = ashmem_pin(asma, pgstart, pgend, &range);
		break;
	case ASHMEM_UNPIN:
		ret = ashmem_unpin(asma, pgstart, pgend, &range);
		break;
	case ASHMEM_GET_PIN_STATUS:
		ret = ashmem_get_pin_status(asma, pgstart, pgend);
		break;
	}

out_unlock:
	mutex_unlock(&ashmem_mutex);
	if (range)
		kmem_cache_free(ashmem_range_cachep, range);

	return ret;
}

static long ashmem_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ashmem_area *asma = file->private_data;
	long ret = -ENOTTY;

	switch (cmd) {
	case ASHMEM_SET_NAME:
		ret = set_name(asma, (void __user *)arg);
		break;
	case ASHMEM_GET_NAME:
		ret = get_name(asma, (void __user *)arg);
		break;
	case ASHMEM_SET_SIZE:
		ret = -EINVAL;
		mutex_lock(&ashmem_mutex);
		if (!asma->file) {
			ret = 0;
			asma->size = (size_t)arg;
		}
		mutex_unlock(&ashmem_mutex);
		break;
	case ASHMEM_GET_SIZE:
		ret = asma->size;
		break;
	case ASHMEM_SET_PROT_MASK:
		ret = set_prot_mask(asma, arg);
		break;
	case ASHMEM_GET_PROT_MASK:
		ret = asma->prot_mask;
		break;
	case ASHMEM_PIN:
	case ASHMEM_UNPIN:
	case ASHMEM_GET_PIN_STATUS:
		ret = ashmem_pin_unpin(asma, cmd, (void __user *)arg);
		break;
	case ASHMEM_PURGE_ALL_CACHES:
		ret = -EPERM;
		if (capable(CAP_SYS_ADMIN)) {
			struct shrink_control sc = {
				.gfp_mask = GFP_KERNEL,
				.nr_to_scan = LONG_MAX,
			};
			ret = ashmem_shrink_count(&ashmem_shrinker, &sc);
			ashmem_shrink_scan(&ashmem_shrinker, &sc);
		}
		break;
	}

	return ret;
}

/* support of 32bit userspace on 64bit platforms */
#ifdef CONFIG_COMPAT
static long compat_ashmem_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	switch (cmd) {
	case COMPAT_ASHMEM_SET_SIZE:
		cmd = ASHMEM_SET_SIZE;
		break;
	case COMPAT_ASHMEM_SET_PROT_MASK:
		cmd = ASHMEM_SET_PROT_MASK;
		break;
	}
	return ashmem_ioctl(file, cmd, arg);
}
#endif
#ifdef CONFIG_PROC_FS
static void ashmem_show_fdinfo(struct seq_file *m, struct file *file)
{
	struct ashmem_area *asma = file->private_data;

	mutex_lock(&ashmem_mutex);

	if (asma->file)
		seq_printf(m, "inode:\t%ld\n", file_inode(asma->file)->i_ino);

	if (asma->name[ASHMEM_NAME_PREFIX_LEN] != '\0')
		seq_printf(m, "name:\t%s\n",
			   asma->name + ASHMEM_NAME_PREFIX_LEN);

	mutex_unlock(&ashmem_mutex);
}
#endif
static const struct file_operations ashmem_fops = {
	.owner = THIS_MODULE,
	.open = ashmem_open,
	.release = ashmem_release,
	.read_iter = ashmem_read_iter,
	.llseek = ashmem_llseek,
	.mmap = ashmem_mmap,
	.unlocked_ioctl = ashmem_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_ashmem_ioctl,
#endif
#ifdef CONFIG_PROC_FS
	.show_fdinfo = ashmem_show_fdinfo,
#endif
};

static struct miscdevice ashmem_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ashmem",
	.fops = &ashmem_fops,
};

static int __init ashmem_init(void)
{
	int ret = -ENOMEM;

	ashmem_area_cachep = kmem_cache_create("ashmem_area_cache",
					       sizeof(struct ashmem_area),
					       0, 0, NULL);
	if (!ashmem_area_cachep) {
		pr_err("failed to create slab cache\n");
		goto out;
	}

	ashmem_range_cachep = kmem_cache_create("ashmem_range_cache",
						sizeof(struct ashmem_range),
						0, 0, NULL);
	if (!ashmem_range_cachep) {
		pr_err("failed to create slab cache\n");
		goto out_free1;
	}

	ret = misc_register(&ashmem_misc);
	if (ret) {
		pr_err("failed to register misc device!\n");
		goto out_free2;
	}

	ret = register_shrinker(&ashmem_shrinker);
	if (ret) {
		pr_err("failed to register shrinker!\n");
		goto out_demisc;
	}

	pr_info("initialized\n");

	return 0;

out_demisc:
	misc_deregister(&ashmem_misc);
out_free2:
	kmem_cache_destroy(ashmem_range_cachep);
out_free1:
	kmem_cache_destroy(ashmem_area_cachep);
out:
	return ret;
}
device_initcall(ashmem_init);
