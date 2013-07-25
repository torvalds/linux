/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/version.h>
#include <asm/uaccess.h>

#include <linux/fs.h>
#include <linux/stat.h>
#include <asm/uaccess.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

#define DEBUG_SUBSYSTEM S_LLITE

#include <lustre_lite.h>
#include "llite_internal.h"
#include <linux/lustre_compat25.h>

struct page *ll_nopage(struct vm_area_struct *vma, unsigned long address,
		       int *type);

static struct vm_operations_struct ll_file_vm_ops;

void policy_from_vma(ldlm_policy_data_t *policy,
			    struct vm_area_struct *vma, unsigned long addr,
			    size_t count)
{
	policy->l_extent.start = ((addr - vma->vm_start) & CFS_PAGE_MASK) +
				 (vma->vm_pgoff << PAGE_CACHE_SHIFT);
	policy->l_extent.end = (policy->l_extent.start + count - 1) |
			       ~CFS_PAGE_MASK;
}

struct vm_area_struct *our_vma(struct mm_struct *mm, unsigned long addr,
			       size_t count)
{
	struct vm_area_struct *vma, *ret = NULL;
	ENTRY;

	/* mmap_sem must have been held by caller. */
	LASSERT(!down_write_trylock(&mm->mmap_sem));

	for(vma = find_vma(mm, addr);
	    vma != NULL && vma->vm_start < (addr + count); vma = vma->vm_next) {
		if (vma->vm_ops && vma->vm_ops == &ll_file_vm_ops &&
		    vma->vm_flags & VM_SHARED) {
			ret = vma;
			break;
		}
	}
	RETURN(ret);
}

/**
 * API independent part for page fault initialization.
 * \param vma - virtual memory area addressed to page fault
 * \param env - corespondent lu_env to processing
 * \param nest - nested level
 * \param index - page index corespondent to fault.
 * \parm ra_flags - vma readahead flags.
 *
 * \return allocated and initialized env for fault operation.
 * \retval EINVAL if env can't allocated
 * \return other error codes from cl_io_init.
 */
struct cl_io *ll_fault_io_init(struct vm_area_struct *vma,
			       struct lu_env **env_ret,
			       struct cl_env_nest *nest,
			       pgoff_t index, unsigned long *ra_flags)
{
	struct file       *file  = vma->vm_file;
	struct inode      *inode = file->f_dentry->d_inode;
	struct cl_io      *io;
	struct cl_fault_io *fio;
	struct lu_env     *env;
	ENTRY;

	*env_ret = NULL;
	if (ll_file_nolock(file))
		RETURN(ERR_PTR(-EOPNOTSUPP));

	/*
	 * page fault can be called when lustre IO is
	 * already active for the current thread, e.g., when doing read/write
	 * against user level buffer mapped from Lustre buffer. To avoid
	 * stomping on existing context, optionally force an allocation of a new
	 * one.
	 */
	env = cl_env_nested_get(nest);
	if (IS_ERR(env))
		 RETURN(ERR_PTR(-EINVAL));

	*env_ret = env;

	io = ccc_env_thread_io(env);
	io->ci_obj = ll_i2info(inode)->lli_clob;
	LASSERT(io->ci_obj != NULL);

	fio = &io->u.ci_fault;
	fio->ft_index      = index;
	fio->ft_executable = vma->vm_flags&VM_EXEC;

	/*
	 * disable VM_SEQ_READ and use VM_RAND_READ to make sure that
	 * the kernel will not read other pages not covered by ldlm in
	 * filemap_nopage. we do our readahead in ll_readpage.
	 */
	if (ra_flags != NULL)
		*ra_flags = vma->vm_flags & (VM_RAND_READ|VM_SEQ_READ);
	vma->vm_flags &= ~VM_SEQ_READ;
	vma->vm_flags |= VM_RAND_READ;

	CDEBUG(D_MMAP, "vm_flags: %lx (%lu %d)\n", vma->vm_flags,
	       fio->ft_index, fio->ft_executable);

	if (cl_io_init(env, io, CIT_FAULT, io->ci_obj) == 0) {
		struct ccc_io *cio = ccc_env_io(env);
		struct ll_file_data *fd = LUSTRE_FPRIVATE(file);

		LASSERT(cio->cui_cl.cis_io == io);

		/* mmap lock must be MANDATORY
		 * it has to cache pages. */
		io->ci_lockreq = CILR_MANDATORY;

		cio->cui_fd  = fd;
	}

	return io;
}

/* Sharing code of page_mkwrite method for rhel5 and rhel6 */
static int ll_page_mkwrite0(struct vm_area_struct *vma, struct page *vmpage,
			    bool *retry)
{
	struct lu_env	   *env;
	struct cl_io	    *io;
	struct vvp_io	   *vio;
	struct cl_env_nest       nest;
	int		      result;
	sigset_t	     set;
	struct inode	     *inode;
	struct ll_inode_info     *lli;
	ENTRY;

	LASSERT(vmpage != NULL);

	io = ll_fault_io_init(vma, &env,  &nest, vmpage->index, NULL);
	if (IS_ERR(io))
		GOTO(out, result = PTR_ERR(io));

	result = io->ci_result;
	if (result < 0)
		GOTO(out, result);

	io->u.ci_fault.ft_mkwrite = 1;
	io->u.ci_fault.ft_writable = 1;

	vio = vvp_env_io(env);
	vio->u.fault.ft_vma    = vma;
	vio->u.fault.ft_vmpage = vmpage;

	set = cfs_block_sigsinv(sigmask(SIGKILL) | sigmask(SIGTERM));

	/* we grab lli_trunc_sem to exclude truncate case.
	 * Otherwise, we could add dirty pages into osc cache
	 * while truncate is on-going. */
	inode = ccc_object_inode(io->ci_obj);
	lli = ll_i2info(inode);
	down_read(&lli->lli_trunc_sem);

	result = cl_io_loop(env, io);

	up_read(&lli->lli_trunc_sem);

	cfs_restore_sigs(set);

	if (result == 0) {
		struct inode *inode = vma->vm_file->f_dentry->d_inode;
		struct ll_inode_info *lli = ll_i2info(inode);

		lock_page(vmpage);
		if (vmpage->mapping == NULL) {
			unlock_page(vmpage);

			/* page was truncated and lock was cancelled, return
			 * ENODATA so that VM_FAULT_NOPAGE will be returned
			 * to handle_mm_fault(). */
			if (result == 0)
				result = -ENODATA;
		} else if (!PageDirty(vmpage)) {
			/* race, the page has been cleaned by ptlrpcd after
			 * it was unlocked, it has to be added into dirty
			 * cache again otherwise this soon-to-dirty page won't
			 * consume any grants, even worse if this page is being
			 * transferred because it will break RPC checksum.
			 */
			unlock_page(vmpage);

			CDEBUG(D_MMAP, "Race on page_mkwrite %p/%lu, page has "
			       "been written out, retry.\n",
			       vmpage, vmpage->index);

			*retry = true;
			result = -EAGAIN;
		}

		if (result == 0) {
			spin_lock(&lli->lli_lock);
			lli->lli_flags |= LLIF_DATA_MODIFIED;
			spin_unlock(&lli->lli_lock);
		}
	}
	EXIT;

out:
	cl_io_fini(env, io);
	cl_env_nested_put(&nest, env);

	CDEBUG(D_MMAP, "%s mkwrite with %d\n", current->comm, result);

	LASSERT(ergo(result == 0, PageLocked(vmpage)));
	return(result);
}



static inline int to_fault_error(int result)
{
	switch(result) {
	case 0:
		result = VM_FAULT_LOCKED;
		break;
	case -EFAULT:
		result = VM_FAULT_NOPAGE;
		break;
	case -ENOMEM:
		result = VM_FAULT_OOM;
		break;
	default:
		result = VM_FAULT_SIGBUS;
		break;
	}
	return result;
}

/**
 * Lustre implementation of a vm_operations_struct::fault() method, called by
 * VM to server page fault (both in kernel and user space).
 *
 * \param vma - is virtiual area struct related to page fault
 * \param vmf - structure which describe type and address where hit fault
 *
 * \return allocated and filled _locked_ page for address
 * \retval VM_FAULT_ERROR on general error
 * \retval NOPAGE_OOM not have memory for allocate new page
 */
static int ll_fault0(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct lu_env	   *env;
	struct cl_io	    *io;
	struct vvp_io	   *vio = NULL;
	struct page	     *vmpage;
	unsigned long	    ra_flags;
	struct cl_env_nest       nest;
	int		      result;
	int		      fault_ret = 0;
	ENTRY;

	io = ll_fault_io_init(vma, &env,  &nest, vmf->pgoff, &ra_flags);
	if (IS_ERR(io))
		RETURN(to_fault_error(PTR_ERR(io)));

	result = io->ci_result;
	if (result == 0) {
		vio = vvp_env_io(env);
		vio->u.fault.ft_vma       = vma;
		vio->u.fault.ft_vmpage    = NULL;
		vio->u.fault.fault.ft_vmf = vmf;

		result = cl_io_loop(env, io);

		fault_ret = vio->u.fault.fault.ft_flags;
		vmpage = vio->u.fault.ft_vmpage;
		if (result != 0 && vmpage != NULL) {
			page_cache_release(vmpage);
			vmf->page = NULL;
		}
	}
	cl_io_fini(env, io);
	cl_env_nested_put(&nest, env);

	vma->vm_flags |= ra_flags;
	if (result != 0 && !(fault_ret & VM_FAULT_RETRY))
		fault_ret |= to_fault_error(result);

	CDEBUG(D_MMAP, "%s fault %d/%d\n",
	       current->comm, fault_ret, result);
	RETURN(fault_ret);
}

static int ll_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	int count = 0;
	bool printed = false;
	int result;
	sigset_t set;

	/* Only SIGKILL and SIGTERM is allowed for fault/nopage/mkwrite
	 * so that it can be killed by admin but not cause segfault by
	 * other signals. */
	set = cfs_block_sigsinv(sigmask(SIGKILL) | sigmask(SIGTERM));

restart:
	result = ll_fault0(vma, vmf);
	LASSERT(!(result & VM_FAULT_LOCKED));
	if (result == 0) {
		struct page *vmpage = vmf->page;

		/* check if this page has been truncated */
		lock_page(vmpage);
		if (unlikely(vmpage->mapping == NULL)) { /* unlucky */
			unlock_page(vmpage);
			page_cache_release(vmpage);
			vmf->page = NULL;

			if (!printed && ++count > 16) {
				CWARN("the page is under heavy contention,"
				      "maybe your app(%s) needs revising :-)\n",
				      current->comm);
				printed = true;
			}

			goto restart;
		}

		result |= VM_FAULT_LOCKED;
	}
	cfs_restore_sigs(set);
	return result;
}

static int ll_page_mkwrite(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	int count = 0;
	bool printed = false;
	bool retry;
	int result;

	do {
		retry = false;
		result = ll_page_mkwrite0(vma, vmf->page, &retry);

		if (!printed && ++count > 16) {
			CWARN("app(%s): the page %lu of file %lu is under heavy"
			      " contention.\n",
			      current->comm, vmf->pgoff,
			      vma->vm_file->f_dentry->d_inode->i_ino);
			printed = true;
		}
	} while (retry);

	switch(result) {
	case 0:
		LASSERT(PageLocked(vmf->page));
		result = VM_FAULT_LOCKED;
		break;
	case -ENODATA:
	case -EFAULT:
		result = VM_FAULT_NOPAGE;
		break;
	case -ENOMEM:
		result = VM_FAULT_OOM;
		break;
	case -EAGAIN:
		result = VM_FAULT_RETRY;
		break;
	default:
		result = VM_FAULT_SIGBUS;
		break;
	}

	return result;
}

/**
 *  To avoid cancel the locks covering mmapped region for lock cache pressure,
 *  we track the mapped vma count in ccc_object::cob_mmap_cnt.
 */
static void ll_vm_open(struct vm_area_struct * vma)
{
	struct inode *inode    = vma->vm_file->f_dentry->d_inode;
	struct ccc_object *vob = cl_inode2ccc(inode);

	ENTRY;
	LASSERT(vma->vm_file);
	LASSERT(atomic_read(&vob->cob_mmap_cnt) >= 0);
	atomic_inc(&vob->cob_mmap_cnt);
	EXIT;
}

/**
 * Dual to ll_vm_open().
 */
static void ll_vm_close(struct vm_area_struct *vma)
{
	struct inode      *inode = vma->vm_file->f_dentry->d_inode;
	struct ccc_object *vob   = cl_inode2ccc(inode);

	ENTRY;
	LASSERT(vma->vm_file);
	atomic_dec(&vob->cob_mmap_cnt);
	LASSERT(atomic_read(&vob->cob_mmap_cnt) >= 0);
	EXIT;
}


/* return the user space pointer that maps to a file offset via a vma */
static inline unsigned long file_to_user(struct vm_area_struct *vma, __u64 byte)
{
	return vma->vm_start + (byte - ((__u64)vma->vm_pgoff << PAGE_CACHE_SHIFT));

}

/* XXX put nice comment here.  talk about __free_pte -> dirty pages and
 * nopage's reference passing to the pte */
int ll_teardown_mmaps(struct address_space *mapping, __u64 first, __u64 last)
{
	int rc = -ENOENT;
	ENTRY;

	LASSERTF(last > first, "last "LPU64" first "LPU64"\n", last, first);
	if (mapping_mapped(mapping)) {
		rc = 0;
		unmap_mapping_range(mapping, first + PAGE_CACHE_SIZE - 1,
				    last - first + 1, 0);
	}

	RETURN(rc);
}

static struct vm_operations_struct ll_file_vm_ops = {
	.fault			= ll_fault,
	.page_mkwrite		= ll_page_mkwrite,
	.open			= ll_vm_open,
	.close			= ll_vm_close,
};

int ll_file_mmap(struct file *file, struct vm_area_struct * vma)
{
	struct inode *inode = file->f_dentry->d_inode;
	int rc;
	ENTRY;

	if (ll_file_nolock(file))
		RETURN(-EOPNOTSUPP);

	ll_stats_ops_tally(ll_i2sbi(inode), LPROC_LL_MAP, 1);
	rc = generic_file_mmap(file, vma);
	if (rc == 0) {
		vma->vm_ops = &ll_file_vm_ops;
		vma->vm_ops->open(vma);
		/* update the inode's size and mtime */
		rc = ll_glimpse_size(inode);
	}

	RETURN(rc);
}
