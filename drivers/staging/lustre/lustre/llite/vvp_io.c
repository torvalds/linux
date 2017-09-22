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
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * Implementation of cl_io for VVP layer.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 *   Author: Jinshan Xiong <jinshan.xiong@whamcloud.com>
 */

#define DEBUG_SUBSYSTEM S_LLITE

#include <obd.h>

#include "llite_internal.h"
#include "vvp_internal.h"

static struct vvp_io *cl2vvp_io(const struct lu_env *env,
				const struct cl_io_slice *slice)
{
	struct vvp_io *vio;

	vio = container_of(slice, struct vvp_io, vui_cl);
	LASSERT(vio == vvp_env_io(env));

	return vio;
}

/**
 * For swapping layout. The file's layout may have changed.
 * To avoid populating pages to a wrong stripe, we have to verify the
 * correctness of layout. It works because swapping layout processes
 * have to acquire group lock.
 */
static bool can_populate_pages(const struct lu_env *env, struct cl_io *io,
			       struct inode *inode)
{
	struct ll_inode_info	*lli = ll_i2info(inode);
	struct vvp_io		*vio = vvp_env_io(env);
	bool rc = true;

	switch (io->ci_type) {
	case CIT_READ:
	case CIT_WRITE:
		/* don't need lock here to check lli_layout_gen as we have held
		 * extent lock and GROUP lock has to hold to swap layout
		 */
		if (ll_layout_version_get(lli) != vio->vui_layout_gen ||
		    OBD_FAIL_CHECK_RESET(OBD_FAIL_LLITE_LOST_LAYOUT, 0)) {
			io->ci_need_restart = 1;
			/* this will cause a short read/write */
			io->ci_continue = 0;
			rc = false;
		}
	case CIT_FAULT:
		/* fault is okay because we've already had a page. */
	default:
		break;
	}

	return rc;
}

static void vvp_object_size_lock(struct cl_object *obj)
{
	struct inode *inode = vvp_object_inode(obj);

	ll_inode_size_lock(inode);
	cl_object_attr_lock(obj);
}

static void vvp_object_size_unlock(struct cl_object *obj)
{
	struct inode *inode = vvp_object_inode(obj);

	cl_object_attr_unlock(obj);
	ll_inode_size_unlock(inode);
}

/**
 * Helper function that if necessary adjusts file size (inode->i_size), when
 * position at the offset \a pos is accessed. File size can be arbitrary stale
 * on a Lustre client, but client at least knows KMS. If accessed area is
 * inside [0, KMS], set file size to KMS, otherwise glimpse file size.
 *
 * Locking: cl_isize_lock is used to serialize changes to inode size and to
 * protect consistency between inode size and cl_object
 * attributes. cl_object_size_lock() protects consistency between cl_attr's of
 * top-object and sub-objects.
 */
static int vvp_prep_size(const struct lu_env *env, struct cl_object *obj,
			 struct cl_io *io, loff_t start, size_t count,
			 int *exceed)
{
	struct cl_attr *attr  = vvp_env_thread_attr(env);
	struct inode   *inode = vvp_object_inode(obj);
	loff_t	  pos   = start + count - 1;
	loff_t kms;
	int result;

	/*
	 * Consistency guarantees: following possibilities exist for the
	 * relation between region being accessed and real file size at this
	 * moment:
	 *
	 *  (A): the region is completely inside of the file;
	 *
	 *  (B-x): x bytes of region are inside of the file, the rest is
	 *  outside;
	 *
	 *  (C): the region is completely outside of the file.
	 *
	 * This classification is stable under DLM lock already acquired by
	 * the caller, because to change the class, other client has to take
	 * DLM lock conflicting with our lock. Also, any updates to ->i_size
	 * by other threads on this client are serialized by
	 * ll_inode_size_lock(). This guarantees that short reads are handled
	 * correctly in the face of concurrent writes and truncates.
	 */
	vvp_object_size_lock(obj);
	result = cl_object_attr_get(env, obj, attr);
	if (result == 0) {
		kms = attr->cat_kms;
		if (pos > kms) {
			/*
			 * A glimpse is necessary to determine whether we
			 * return a short read (B) or some zeroes at the end
			 * of the buffer (C)
			 */
			vvp_object_size_unlock(obj);
			result = cl_glimpse_lock(env, io, inode, obj, 0);
			if (result == 0 && exceed) {
				/* If objective page index exceed end-of-file
				 * page index, return directly. Do not expect
				 * kernel will check such case correctly.
				 * linux-2.6.18-128.1.1 miss to do that.
				 * --bug 17336
				 */
				loff_t size = i_size_read(inode);
				loff_t cur_index = start >> PAGE_SHIFT;
				loff_t size_index = (size - 1) >> PAGE_SHIFT;

				if ((size == 0 && cur_index != 0) ||
				    size_index < cur_index)
					*exceed = 1;
			}
			return result;
		}
		/*
		 * region is within kms and, hence, within real file
		 * size (A). We need to increase i_size to cover the
		 * read region so that generic_file_read() will do its
		 * job, but that doesn't mean the kms size is
		 * _correct_, it is only the _minimum_ size. If
		 * someone does a stat they will get the correct size
		 * which will always be >= the kms value here.
		 * b=11081
		 */
		if (i_size_read(inode) < kms) {
			i_size_write(inode, kms);
			CDEBUG(D_VFSTRACE, DFID " updating i_size %llu\n",
			       PFID(lu_object_fid(&obj->co_lu)),
			       (__u64)i_size_read(inode));
		}
	}

	vvp_object_size_unlock(obj);

	return result;
}

/*****************************************************************************
 *
 * io operations.
 *
 */

static int vvp_io_one_lock_index(const struct lu_env *env, struct cl_io *io,
				 __u32 enqflags, enum cl_lock_mode mode,
			  pgoff_t start, pgoff_t end)
{
	struct vvp_io          *vio   = vvp_env_io(env);
	struct cl_lock_descr   *descr = &vio->vui_link.cill_descr;
	struct cl_object       *obj   = io->ci_obj;

	CLOBINVRNT(env, obj, vvp_object_invariant(obj));

	CDEBUG(D_VFSTRACE, "lock: %d [%lu, %lu]\n", mode, start, end);

	memset(&vio->vui_link, 0, sizeof(vio->vui_link));

	if (vio->vui_fd && (vio->vui_fd->fd_flags & LL_FILE_GROUP_LOCKED)) {
		descr->cld_mode = CLM_GROUP;
		descr->cld_gid  = vio->vui_fd->fd_grouplock.lg_gid;
		enqflags |= CEF_LOCK_MATCH;
	} else {
		descr->cld_mode  = mode;
	}
	descr->cld_obj   = obj;
	descr->cld_start = start;
	descr->cld_end   = end;
	descr->cld_enq_flags = enqflags;

	cl_io_lock_add(env, io, &vio->vui_link);
	return 0;
}

static int vvp_io_one_lock(const struct lu_env *env, struct cl_io *io,
			   __u32 enqflags, enum cl_lock_mode mode,
			   loff_t start, loff_t end)
{
	struct cl_object *obj = io->ci_obj;

	return vvp_io_one_lock_index(env, io, enqflags, mode,
				     cl_index(obj, start), cl_index(obj, end));
}

static int vvp_io_write_iter_init(const struct lu_env *env,
				  const struct cl_io_slice *ios)
{
	struct vvp_io *vio = cl2vvp_io(env, ios);

	cl_page_list_init(&vio->u.write.vui_queue);
	vio->u.write.vui_written = 0;
	vio->u.write.vui_from = 0;
	vio->u.write.vui_to = PAGE_SIZE;

	return 0;
}

static void vvp_io_write_iter_fini(const struct lu_env *env,
				   const struct cl_io_slice *ios)
{
	struct vvp_io *vio = cl2vvp_io(env, ios);

	LASSERT(vio->u.write.vui_queue.pl_nr == 0);
}

static int vvp_io_fault_iter_init(const struct lu_env *env,
				  const struct cl_io_slice *ios)
{
	struct vvp_io *vio   = cl2vvp_io(env, ios);
	struct inode  *inode = vvp_object_inode(ios->cis_obj);

	LASSERT(inode == file_inode(vio->vui_fd->fd_file));
	vio->u.fault.ft_mtime = inode->i_mtime.tv_sec;
	return 0;
}

static void vvp_io_fini(const struct lu_env *env, const struct cl_io_slice *ios)
{
	struct cl_io     *io  = ios->cis_io;
	struct cl_object *obj = io->ci_obj;
	struct vvp_io    *vio = cl2vvp_io(env, ios);
	struct inode *inode = vvp_object_inode(obj);

	CLOBINVRNT(env, obj, vvp_object_invariant(obj));

	CDEBUG(D_VFSTRACE, DFID
	       " ignore/verify layout %d/%d, layout version %d restore needed %d\n",
	       PFID(lu_object_fid(&obj->co_lu)),
	       io->ci_ignore_layout, io->ci_verify_layout,
	       vio->vui_layout_gen, io->ci_restore_needed);

	if (io->ci_restore_needed) {
		int	rc;

		/* file was detected release, we need to restore it
		 * before finishing the io
		 */
		rc = ll_layout_restore(inode, 0, OBD_OBJECT_EOF);
		/* if restore registration failed, no restart,
		 * we will return -ENODATA
		 */
		/* The layout will change after restore, so we need to
		 * block on layout lock hold by the MDT
		 * as MDT will not send new layout in lvb (see LU-3124)
		 * we have to explicitly fetch it, all this will be done
		 * by ll_layout_refresh()
		 */
		if (rc == 0) {
			io->ci_restore_needed = 0;
			io->ci_need_restart = 1;
			io->ci_verify_layout = 1;
		} else {
			io->ci_restore_needed = 1;
			io->ci_need_restart = 0;
			io->ci_verify_layout = 0;
			io->ci_result = rc;
		}
	}

	if (!io->ci_ignore_layout && io->ci_verify_layout) {
		__u32 gen = 0;

		/* check layout version */
		ll_layout_refresh(inode, &gen);
		io->ci_need_restart = vio->vui_layout_gen != gen;
		if (io->ci_need_restart) {
			CDEBUG(D_VFSTRACE,
			       DFID " layout changed from %d to %d.\n",
			       PFID(lu_object_fid(&obj->co_lu)),
			       vio->vui_layout_gen, gen);
			/* today successful restore is the only possible case */
			/* restore was done, clear restoring state */
			clear_bit(LLIF_FILE_RESTORING,
				  &ll_i2info(inode)->lli_flags);
		}
	}
}

static void vvp_io_fault_fini(const struct lu_env *env,
			      const struct cl_io_slice *ios)
{
	struct cl_io   *io   = ios->cis_io;
	struct cl_page *page = io->u.ci_fault.ft_page;

	CLOBINVRNT(env, io->ci_obj, vvp_object_invariant(io->ci_obj));

	if (page) {
		lu_ref_del(&page->cp_reference, "fault", io);
		cl_page_put(env, page);
		io->u.ci_fault.ft_page = NULL;
	}
	vvp_io_fini(env, ios);
}

static enum cl_lock_mode vvp_mode_from_vma(struct vm_area_struct *vma)
{
	/*
	 * we only want to hold PW locks if the mmap() can generate
	 * writes back to the file and that only happens in shared
	 * writable vmas
	 */
	if ((vma->vm_flags & VM_SHARED) && (vma->vm_flags & VM_WRITE))
		return CLM_WRITE;
	return CLM_READ;
}

static int vvp_mmap_locks(const struct lu_env *env,
			  struct vvp_io *vio, struct cl_io *io)
{
	struct vvp_thread_info *cti = vvp_env_info(env);
	struct mm_struct       *mm = current->mm;
	struct vm_area_struct  *vma;
	struct cl_lock_descr   *descr = &cti->vti_descr;
	union ldlm_policy_data policy;
	unsigned long	   addr;
	ssize_t		 count;
	int		 result = 0;
	struct iov_iter i;
	struct iovec iov;

	LASSERT(io->ci_type == CIT_READ || io->ci_type == CIT_WRITE);

	if (!vio->vui_iter) /* nfs or loop back device write */
		return 0;

	/* No MM (e.g. NFS)? No vmas too. */
	if (!mm)
		return 0;

	iov_for_each(iov, i, *vio->vui_iter) {
		addr = (unsigned long)iov.iov_base;
		count = iov.iov_len;
		if (count == 0)
			continue;

		count += addr & (~PAGE_MASK);
		addr &= PAGE_MASK;

		down_read(&mm->mmap_sem);
		while ((vma = our_vma(mm, addr, count)) != NULL) {
			struct inode *inode = file_inode(vma->vm_file);
			int flags = CEF_MUST;

			if (ll_file_nolock(vma->vm_file)) {
				/*
				 * For no lock case is not allowed for mmap
				 */
				result = -EINVAL;
				break;
			}

			/*
			 * XXX: Required lock mode can be weakened: CIT_WRITE
			 * io only ever reads user level buffer, and CIT_READ
			 * only writes on it.
			 */
			policy_from_vma(&policy, vma, addr, count);
			descr->cld_mode = vvp_mode_from_vma(vma);
			descr->cld_obj = ll_i2info(inode)->lli_clob;
			descr->cld_start = cl_index(descr->cld_obj,
						    policy.l_extent.start);
			descr->cld_end = cl_index(descr->cld_obj,
						  policy.l_extent.end);
			descr->cld_enq_flags = flags;
			result = cl_io_lock_alloc_add(env, io, descr);

			CDEBUG(D_VFSTRACE, "lock: %d: [%lu, %lu]\n",
			       descr->cld_mode, descr->cld_start,
			       descr->cld_end);

			if (result < 0)
				break;

			if (vma->vm_end - addr >= count)
				break;

			count -= vma->vm_end - addr;
			addr = vma->vm_end;
		}
		up_read(&mm->mmap_sem);
		if (result < 0)
			break;
	}
	return result;
}

static void vvp_io_advance(const struct lu_env *env,
			   const struct cl_io_slice *ios,
			   size_t nob)
{
	struct cl_object *obj = ios->cis_io->ci_obj;
	struct vvp_io	 *vio = cl2vvp_io(env, ios);

	CLOBINVRNT(env, obj, vvp_object_invariant(obj));

	vio->vui_tot_count -= nob;
	iov_iter_reexpand(vio->vui_iter, vio->vui_tot_count);
}

static void vvp_io_update_iov(const struct lu_env *env,
			      struct vvp_io *vio, struct cl_io *io)
{
	size_t size = io->u.ci_rw.crw_count;

	if (!vio->vui_iter)
		return;

	iov_iter_truncate(vio->vui_iter, size);
}

static int vvp_io_rw_lock(const struct lu_env *env, struct cl_io *io,
			  enum cl_lock_mode mode, loff_t start, loff_t end)
{
	struct vvp_io *vio = vvp_env_io(env);
	int result;
	int ast_flags = 0;

	LASSERT(io->ci_type == CIT_READ || io->ci_type == CIT_WRITE);

	vvp_io_update_iov(env, vio, io);

	if (io->u.ci_rw.crw_nonblock)
		ast_flags |= CEF_NONBLOCK;
	result = vvp_mmap_locks(env, vio, io);
	if (result == 0)
		result = vvp_io_one_lock(env, io, ast_flags, mode, start, end);
	return result;
}

static int vvp_io_read_lock(const struct lu_env *env,
			    const struct cl_io_slice *ios)
{
	struct cl_io	 *io = ios->cis_io;
	struct cl_io_rw_common *rd = &io->u.ci_rd.rd;
	int result;

	result = vvp_io_rw_lock(env, io, CLM_READ, rd->crw_pos,
				rd->crw_pos + rd->crw_count - 1);

	return result;
}

static int vvp_io_fault_lock(const struct lu_env *env,
			     const struct cl_io_slice *ios)
{
	struct cl_io *io   = ios->cis_io;
	struct vvp_io *vio = cl2vvp_io(env, ios);
	/*
	 * XXX LDLM_FL_CBPENDING
	 */
	return vvp_io_one_lock_index(env,
				     io, 0,
				     vvp_mode_from_vma(vio->u.fault.ft_vma),
				     io->u.ci_fault.ft_index,
				     io->u.ci_fault.ft_index);
}

static int vvp_io_write_lock(const struct lu_env *env,
			     const struct cl_io_slice *ios)
{
	struct cl_io *io = ios->cis_io;
	loff_t start;
	loff_t end;

	if (io->u.ci_wr.wr_append) {
		start = 0;
		end   = OBD_OBJECT_EOF;
	} else {
		start = io->u.ci_wr.wr.crw_pos;
		end   = start + io->u.ci_wr.wr.crw_count - 1;
	}
	return vvp_io_rw_lock(env, io, CLM_WRITE, start, end);
}

static int vvp_io_setattr_iter_init(const struct lu_env *env,
				    const struct cl_io_slice *ios)
{
	return 0;
}

/**
 * Implementation of cl_io_operations::vio_lock() method for CIT_SETATTR io.
 *
 * Handles "lockless io" mode when extent locking is done by server.
 */
static int vvp_io_setattr_lock(const struct lu_env *env,
			       const struct cl_io_slice *ios)
{
	struct cl_io  *io  = ios->cis_io;
	__u64 new_size;
	__u32 enqflags = 0;

	if (cl_io_is_trunc(io)) {
		new_size = io->u.ci_setattr.sa_attr.lvb_size;
		if (new_size == 0)
			enqflags = CEF_DISCARD_DATA;
	} else {
		unsigned int valid = io->u.ci_setattr.sa_valid;

		if (!(valid & TIMES_SET_FLAGS))
			return 0;

		if ((!(valid & ATTR_MTIME) ||
		     io->u.ci_setattr.sa_attr.lvb_mtime >=
		     io->u.ci_setattr.sa_attr.lvb_ctime) &&
		    (!(valid & ATTR_ATIME) ||
		     io->u.ci_setattr.sa_attr.lvb_atime >=
		     io->u.ci_setattr.sa_attr.lvb_ctime))
			return 0;
		new_size = 0;
	}

	return vvp_io_one_lock(env, io, enqflags, CLM_WRITE,
			       new_size, OBD_OBJECT_EOF);
}

static int vvp_do_vmtruncate(struct inode *inode, size_t size)
{
	int     result;
	/*
	 * Only ll_inode_size_lock is taken at this level.
	 */
	ll_inode_size_lock(inode);
	result = inode_newsize_ok(inode, size);
	if (result < 0) {
		ll_inode_size_unlock(inode);
		return result;
	}
	truncate_setsize(inode, size);
	ll_inode_size_unlock(inode);
	return result;
}

static int vvp_io_setattr_time(const struct lu_env *env,
			       const struct cl_io_slice *ios)
{
	struct cl_io       *io    = ios->cis_io;
	struct cl_object   *obj   = io->ci_obj;
	struct cl_attr     *attr  = vvp_env_thread_attr(env);
	int result;
	unsigned valid = CAT_CTIME;

	cl_object_attr_lock(obj);
	attr->cat_ctime = io->u.ci_setattr.sa_attr.lvb_ctime;
	if (io->u.ci_setattr.sa_valid & ATTR_ATIME_SET) {
		attr->cat_atime = io->u.ci_setattr.sa_attr.lvb_atime;
		valid |= CAT_ATIME;
	}
	if (io->u.ci_setattr.sa_valid & ATTR_MTIME_SET) {
		attr->cat_mtime = io->u.ci_setattr.sa_attr.lvb_mtime;
		valid |= CAT_MTIME;
	}
	result = cl_object_attr_update(env, obj, attr, valid);
	cl_object_attr_unlock(obj);

	return result;
}

static int vvp_io_setattr_start(const struct lu_env *env,
				const struct cl_io_slice *ios)
{
	struct cl_io	*io    = ios->cis_io;
	struct inode	*inode = vvp_object_inode(io->ci_obj);
	struct ll_inode_info *lli = ll_i2info(inode);

	if (cl_io_is_trunc(io)) {
		down_write(&lli->lli_trunc_sem);
		inode_lock(inode);
		inode_dio_wait(inode);
	} else {
		inode_lock(inode);
	}

	if (io->u.ci_setattr.sa_valid & TIMES_SET_FLAGS)
		return vvp_io_setattr_time(env, ios);

	return 0;
}

static void vvp_io_setattr_end(const struct lu_env *env,
			       const struct cl_io_slice *ios)
{
	struct cl_io *io    = ios->cis_io;
	struct inode *inode = vvp_object_inode(io->ci_obj);
	struct ll_inode_info *lli = ll_i2info(inode);

	if (cl_io_is_trunc(io)) {
		/* Truncate in memory pages - they must be clean pages
		 * because osc has already notified to destroy osc_extents.
		 */
		vvp_do_vmtruncate(inode, io->u.ci_setattr.sa_attr.lvb_size);
		inode_unlock(inode);
		up_write(&lli->lli_trunc_sem);
	} else {
		inode_unlock(inode);
	}
}

static void vvp_io_setattr_fini(const struct lu_env *env,
				const struct cl_io_slice *ios)
{
	bool restore_needed = ios->cis_io->ci_restore_needed;
	struct inode *inode = vvp_object_inode(ios->cis_obj);

	vvp_io_fini(env, ios);

	if (restore_needed && !ios->cis_io->ci_restore_needed) {
		/* restore finished, set data modified flag for HSM */
		set_bit(LLIF_DATA_MODIFIED, &(ll_i2info(inode))->lli_flags);
	}
}

static int vvp_io_read_start(const struct lu_env *env,
			     const struct cl_io_slice *ios)
{
	struct vvp_io     *vio   = cl2vvp_io(env, ios);
	struct cl_io      *io    = ios->cis_io;
	struct cl_object  *obj   = io->ci_obj;
	struct inode      *inode = vvp_object_inode(obj);
	struct ll_inode_info *lli = ll_i2info(inode);
	struct file       *file  = vio->vui_fd->fd_file;

	int     result;
	loff_t  pos = io->u.ci_rd.rd.crw_pos;
	long    cnt = io->u.ci_rd.rd.crw_count;
	long    tot = vio->vui_tot_count;
	int     exceed = 0;

	CLOBINVRNT(env, obj, vvp_object_invariant(obj));

	CDEBUG(D_VFSTRACE, "read: -> [%lli, %lli)\n", pos, pos + cnt);

	down_read(&lli->lli_trunc_sem);

	if (!can_populate_pages(env, io, inode))
		return 0;

	result = vvp_prep_size(env, obj, io, pos, tot, &exceed);
	if (result != 0)
		return result;
	else if (exceed != 0)
		goto out;

	LU_OBJECT_HEADER(D_INODE, env, &obj->co_lu,
			 "Read ino %lu, %lu bytes, offset %lld, size %llu\n",
			 inode->i_ino, cnt, pos, i_size_read(inode));

	/* turn off the kernel's read-ahead */
	vio->vui_fd->fd_file->f_ra.ra_pages = 0;

	/* initialize read-ahead window once per syscall */
	if (!vio->vui_ra_valid) {
		vio->vui_ra_valid = true;
		vio->vui_ra_start = cl_index(obj, pos);
		vio->vui_ra_count = cl_index(obj, tot + PAGE_SIZE - 1);
		ll_ras_enter(file);
	}

	/* BUG: 5972 */
	file_accessed(file);
	LASSERT(vio->vui_iocb->ki_pos == pos);
	result = generic_file_read_iter(vio->vui_iocb, vio->vui_iter);

out:
	if (result >= 0) {
		if (result < cnt)
			io->ci_continue = 0;
		io->ci_nob += result;
		ll_rw_stats_tally(ll_i2sbi(inode), current->pid,
				  vio->vui_fd, pos, result, READ);
		result = 0;
	}
	return result;
}

static int vvp_io_commit_sync(const struct lu_env *env, struct cl_io *io,
			      struct cl_page_list *plist, int from, int to)
{
	struct cl_2queue *queue = &io->ci_queue;
	struct cl_page *page;
	unsigned int bytes = 0;
	int rc = 0;

	if (plist->pl_nr == 0)
		return 0;

	if (from > 0 || to != PAGE_SIZE) {
		page = cl_page_list_first(plist);
		if (plist->pl_nr == 1) {
			cl_page_clip(env, page, from, to);
		} else {
			if (from > 0)
				cl_page_clip(env, page, from, PAGE_SIZE);
			if (to != PAGE_SIZE) {
				page = cl_page_list_last(plist);
				cl_page_clip(env, page, 0, to);
			}
		}
	}

	cl_2queue_init(queue);
	cl_page_list_splice(plist, &queue->c2_qin);
	rc = cl_io_submit_sync(env, io, CRT_WRITE, queue, 0);

	/* plist is not sorted any more */
	cl_page_list_splice(&queue->c2_qin, plist);
	cl_page_list_splice(&queue->c2_qout, plist);
	cl_2queue_fini(env, queue);

	if (rc == 0) {
		/* calculate bytes */
		bytes = plist->pl_nr << PAGE_SHIFT;
		bytes -= from + PAGE_SIZE - to;

		while (plist->pl_nr > 0) {
			page = cl_page_list_first(plist);
			cl_page_list_del(env, plist, page);

			cl_page_clip(env, page, 0, PAGE_SIZE);

			SetPageUptodate(cl_page_vmpage(page));
			cl_page_disown(env, io, page);

			/* held in ll_cl_init() */
			lu_ref_del(&page->cp_reference, "cl_io", io);
			cl_page_put(env, page);
		}
	}

	return bytes > 0 ? bytes : rc;
}

static void write_commit_callback(const struct lu_env *env, struct cl_io *io,
				  struct cl_page *page)
{
	struct page *vmpage = page->cp_vmpage;

	SetPageUptodate(vmpage);
	set_page_dirty(vmpage);

	cl_page_disown(env, io, page);

	/* held in ll_cl_init() */
	lu_ref_del(&page->cp_reference, "cl_io", cl_io_top(io));
	cl_page_put(env, page);
}

/* make sure the page list is contiguous */
static bool page_list_sanity_check(struct cl_object *obj,
				   struct cl_page_list *plist)
{
	struct cl_page *page;
	pgoff_t index = CL_PAGE_EOF;

	cl_page_list_for_each(page, plist) {
		struct vvp_page *vpg = cl_object_page_slice(obj, page);

		if (index == CL_PAGE_EOF) {
			index = vvp_index(vpg);
			continue;
		}

		++index;
		if (index == vvp_index(vpg))
			continue;

		return false;
	}
	return true;
}

/* Return how many bytes have queued or written */
int vvp_io_write_commit(const struct lu_env *env, struct cl_io *io)
{
	struct cl_object *obj = io->ci_obj;
	struct inode *inode = vvp_object_inode(obj);
	struct vvp_io *vio = vvp_env_io(env);
	struct cl_page_list *queue = &vio->u.write.vui_queue;
	struct cl_page *page;
	int rc = 0;
	int bytes = 0;
	unsigned int npages = vio->u.write.vui_queue.pl_nr;

	if (npages == 0)
		return 0;

	CDEBUG(D_VFSTRACE, "commit async pages: %d, from %d, to %d\n",
	       npages, vio->u.write.vui_from, vio->u.write.vui_to);

	LASSERT(page_list_sanity_check(obj, queue));

	/* submit IO with async write */
	rc = cl_io_commit_async(env, io, queue,
				vio->u.write.vui_from, vio->u.write.vui_to,
				write_commit_callback);
	npages -= queue->pl_nr; /* already committed pages */
	if (npages > 0) {
		/* calculate how many bytes were written */
		bytes = npages << PAGE_SHIFT;

		/* first page */
		bytes -= vio->u.write.vui_from;
		if (queue->pl_nr == 0) /* last page */
			bytes -= PAGE_SIZE - vio->u.write.vui_to;
		LASSERTF(bytes > 0, "bytes = %d, pages = %d\n", bytes, npages);

		vio->u.write.vui_written += bytes;

		CDEBUG(D_VFSTRACE, "Committed %d pages %d bytes, tot: %ld\n",
		       npages, bytes, vio->u.write.vui_written);

		/* the first page must have been written. */
		vio->u.write.vui_from = 0;
	}
	LASSERT(page_list_sanity_check(obj, queue));
	LASSERT(ergo(rc == 0, queue->pl_nr == 0));

	/* out of quota, try sync write */
	if (rc == -EDQUOT && !cl_io_is_mkwrite(io)) {
		rc = vvp_io_commit_sync(env, io, queue,
					vio->u.write.vui_from,
					vio->u.write.vui_to);
		if (rc > 0) {
			vio->u.write.vui_written += rc;
			rc = 0;
		}
	}

	/* update inode size */
	ll_merge_attr(env, inode);

	/* Now the pages in queue were failed to commit, discard them
	 * unless they were dirtied before.
	 */
	while (queue->pl_nr > 0) {
		page = cl_page_list_first(queue);
		cl_page_list_del(env, queue, page);

		if (!PageDirty(cl_page_vmpage(page)))
			cl_page_discard(env, io, page);

		cl_page_disown(env, io, page);

		/* held in ll_cl_init() */
		lu_ref_del(&page->cp_reference, "cl_io", io);
		cl_page_put(env, page);
	}
	cl_page_list_fini(env, queue);

	return rc;
}

static int vvp_io_write_start(const struct lu_env *env,
			      const struct cl_io_slice *ios)
{
	struct vvp_io      *vio   = cl2vvp_io(env, ios);
	struct cl_io       *io    = ios->cis_io;
	struct cl_object   *obj   = io->ci_obj;
	struct inode       *inode = vvp_object_inode(obj);
	struct ll_inode_info *lli = ll_i2info(inode);
	ssize_t result = 0;
	loff_t pos = io->u.ci_wr.wr.crw_pos;
	size_t cnt = io->u.ci_wr.wr.crw_count;

	down_read(&lli->lli_trunc_sem);

	if (!can_populate_pages(env, io, inode))
		return 0;

	if (cl_io_is_append(io)) {
		/*
		 * PARALLEL IO This has to be changed for parallel IO doing
		 * out-of-order writes.
		 */
		ll_merge_attr(env, inode);
		pos = i_size_read(inode);
		io->u.ci_wr.wr.crw_pos = pos;
		vio->vui_iocb->ki_pos = pos;
	} else {
		LASSERT(vio->vui_iocb->ki_pos == pos);
	}

	CDEBUG(D_VFSTRACE, "write: [%lli, %lli)\n", pos, pos + (long long)cnt);

	/*
	 * The maximum Lustre file size is variable, based on the OST maximum
	 * object size and number of stripes.  This needs another check in
	 * addition to the VFS checks earlier.
	 */
	if (pos + cnt > ll_file_maxbytes(inode)) {
		CDEBUG(D_INODE,
		       "%s: file " DFID " offset %llu > maxbytes %llu\n",
		       ll_get_fsname(inode->i_sb, NULL, 0),
		       PFID(ll_inode2fid(inode)), pos + cnt,
		       ll_file_maxbytes(inode));
		return -EFBIG;
	}

	if (!vio->vui_iter) {
		/* from a temp io in ll_cl_init(). */
		result = 0;
	} else {
		/*
		 * When using the locked AIO function (generic_file_aio_write())
		 * testing has shown the inode mutex to be a limiting factor
		 * with multi-threaded single shared file performance. To get
		 * around this, we now use the lockless version. To maintain
		 * consistency, proper locking to protect against writes,
		 * trucates, etc. is handled in the higher layers of lustre.
		 */
		bool lock_node = !IS_NOSEC(inode);

		if (lock_node)
			inode_lock(inode);
		result = __generic_file_write_iter(vio->vui_iocb,
						   vio->vui_iter);
		if (lock_node)
			inode_unlock(inode);

		if (result > 0 || result == -EIOCBQUEUED)
			result = generic_write_sync(vio->vui_iocb, result);
	}

	if (result > 0) {
		result = vvp_io_write_commit(env, io);
		if (vio->u.write.vui_written > 0) {
			result = vio->u.write.vui_written;
			io->ci_nob += result;

			CDEBUG(D_VFSTRACE, "write: nob %zd, result: %zd\n",
			       io->ci_nob, result);
		}
	}
	if (result > 0) {
		set_bit(LLIF_DATA_MODIFIED, &(ll_i2info(inode))->lli_flags);

		if (result < cnt)
			io->ci_continue = 0;
		ll_rw_stats_tally(ll_i2sbi(inode), current->pid,
				  vio->vui_fd, pos, result, WRITE);
		result = 0;
	}
	return result;
}

static void vvp_io_rw_end(const struct lu_env *env,
			  const struct cl_io_slice *ios)
{
	struct inode *inode = vvp_object_inode(ios->cis_obj);
	struct ll_inode_info *lli = ll_i2info(inode);

	up_read(&lli->lli_trunc_sem);
}

static int vvp_io_kernel_fault(struct vvp_fault_io *cfio)
{
	struct vm_fault *vmf = cfio->ft_vmf;

	cfio->ft_flags = filemap_fault(vmf);
	cfio->ft_flags_valid = 1;

	if (vmf->page) {
		CDEBUG(D_PAGE,
		       "page %p map %p index %lu flags %lx count %u priv %0lx: got addr %p type NOPAGE\n",
		       vmf->page, vmf->page->mapping, vmf->page->index,
		       (long)vmf->page->flags, page_count(vmf->page),
		       page_private(vmf->page), (void *)vmf->address);
		if (unlikely(!(cfio->ft_flags & VM_FAULT_LOCKED))) {
			lock_page(vmf->page);
			cfio->ft_flags |= VM_FAULT_LOCKED;
		}

		cfio->ft_vmpage = vmf->page;
		return 0;
	}

	if (cfio->ft_flags & (VM_FAULT_SIGBUS | VM_FAULT_SIGSEGV)) {
		CDEBUG(D_PAGE, "got addr %p - SIGBUS\n", (void *)vmf->address);
		return -EFAULT;
	}

	if (cfio->ft_flags & VM_FAULT_OOM) {
		CDEBUG(D_PAGE, "got addr %p - OOM\n", (void *)vmf->address);
		return -ENOMEM;
	}

	if (cfio->ft_flags & VM_FAULT_RETRY)
		return -EAGAIN;

	CERROR("Unknown error in page fault %d!\n", cfio->ft_flags);
	return -EINVAL;
}

static void mkwrite_commit_callback(const struct lu_env *env, struct cl_io *io,
				    struct cl_page *page)
{
	set_page_dirty(page->cp_vmpage);
}

static int vvp_io_fault_start(const struct lu_env *env,
			      const struct cl_io_slice *ios)
{
	struct vvp_io       *vio     = cl2vvp_io(env, ios);
	struct cl_io	*io      = ios->cis_io;
	struct cl_object    *obj     = io->ci_obj;
	struct inode        *inode   = vvp_object_inode(obj);
	struct ll_inode_info *lli = ll_i2info(inode);
	struct cl_fault_io  *fio     = &io->u.ci_fault;
	struct vvp_fault_io *cfio    = &vio->u.fault;
	loff_t	       offset;
	int		  result  = 0;
	struct page	  *vmpage  = NULL;
	struct cl_page      *page;
	loff_t	       size;
	pgoff_t		     last_index;

	down_read(&lli->lli_trunc_sem);

	/* offset of the last byte on the page */
	offset = cl_offset(obj, fio->ft_index + 1) - 1;
	LASSERT(cl_index(obj, offset) == fio->ft_index);
	result = vvp_prep_size(env, obj, io, 0, offset + 1, NULL);
	if (result != 0)
		return result;

	/* must return locked page */
	if (fio->ft_mkwrite) {
		LASSERT(cfio->ft_vmpage);
		lock_page(cfio->ft_vmpage);
	} else {
		result = vvp_io_kernel_fault(cfio);
		if (result != 0)
			return result;
	}

	vmpage = cfio->ft_vmpage;
	LASSERT(PageLocked(vmpage));

	if (OBD_FAIL_CHECK(OBD_FAIL_LLITE_FAULT_TRUNC_RACE))
		ll_invalidate_page(vmpage);

	size = i_size_read(inode);
	/* Though we have already held a cl_lock upon this page, but
	 * it still can be truncated locally.
	 */
	if (unlikely((vmpage->mapping != inode->i_mapping) ||
		     (page_offset(vmpage) > size))) {
		CDEBUG(D_PAGE, "llite: fault and truncate race happened!\n");

		/* return +1 to stop cl_io_loop() and ll_fault() will catch
		 * and retry.
		 */
		result = 1;
		goto out;
	}

	last_index = cl_index(obj, size - 1);

	if (fio->ft_mkwrite) {
		/*
		 * Capture the size while holding the lli_trunc_sem from above
		 * we want to make sure that we complete the mkwrite action
		 * while holding this lock. We need to make sure that we are
		 * not past the end of the file.
		 */
		if (last_index < fio->ft_index) {
			CDEBUG(D_PAGE,
			       "llite: mkwrite and truncate race happened: %p: 0x%lx 0x%lx\n",
			       vmpage->mapping, fio->ft_index, last_index);
			/*
			 * We need to return if we are
			 * passed the end of the file. This will propagate
			 * up the call stack to ll_page_mkwrite where
			 * we will return VM_FAULT_NOPAGE. Any non-negative
			 * value returned here will be silently
			 * converted to 0. If the vmpage->mapping is null
			 * the error code would be converted back to ENODATA
			 * in ll_page_mkwrite0. Thus we return -ENODATA
			 * to handle both cases
			 */
			result = -ENODATA;
			goto out;
		}
	}

	page = cl_page_find(env, obj, fio->ft_index, vmpage, CPT_CACHEABLE);
	if (IS_ERR(page)) {
		result = PTR_ERR(page);
		goto out;
	}

	/* if page is going to be written, we should add this page into cache
	 * earlier.
	 */
	if (fio->ft_mkwrite) {
		wait_on_page_writeback(vmpage);
		if (!PageDirty(vmpage)) {
			struct cl_page_list *plist = &io->ci_queue.c2_qin;
			struct vvp_page *vpg = cl_object_page_slice(obj, page);
			int to = PAGE_SIZE;

			/* vvp_page_assume() calls wait_on_page_writeback(). */
			cl_page_assume(env, io, page);

			cl_page_list_init(plist);
			cl_page_list_add(plist, page);

			/* size fixup */
			if (last_index == vvp_index(vpg))
				to = size & ~PAGE_MASK;

			/* Do not set Dirty bit here so that in case IO is
			 * started before the page is really made dirty, we
			 * still have chance to detect it.
			 */
			result = cl_io_commit_async(env, io, plist, 0, to,
						    mkwrite_commit_callback);
			LASSERT(cl_page_is_owned(page, io));
			cl_page_list_fini(env, plist);

			vmpage = NULL;
			if (result < 0) {
				cl_page_discard(env, io, page);
				cl_page_disown(env, io, page);

				cl_page_put(env, page);

				/* we're in big trouble, what can we do now? */
				if (result == -EDQUOT)
					result = -ENOSPC;
				goto out;
			} else {
				cl_page_disown(env, io, page);
			}
		}
	}

	/*
	 * The ft_index is only used in the case of
	 * a mkwrite action. We need to check
	 * our assertions are correct, since
	 * we should have caught this above
	 */
	LASSERT(!fio->ft_mkwrite || fio->ft_index <= last_index);
	if (fio->ft_index == last_index)
		/*
		 * Last page is mapped partially.
		 */
		fio->ft_nob = size - cl_offset(obj, fio->ft_index);
	else
		fio->ft_nob = cl_page_size(obj);

	lu_ref_add(&page->cp_reference, "fault", io);
	fio->ft_page = page;

out:
	/* return unlocked vmpage to avoid deadlocking */
	if (vmpage)
		unlock_page(vmpage);

	cfio->ft_flags &= ~VM_FAULT_LOCKED;

	return result;
}

static void vvp_io_fault_end(const struct lu_env *env,
			     const struct cl_io_slice *ios)
{
	struct inode *inode = vvp_object_inode(ios->cis_obj);
	struct ll_inode_info *lli = ll_i2info(inode);

	CLOBINVRNT(env, ios->cis_io->ci_obj,
		   vvp_object_invariant(ios->cis_io->ci_obj));
	up_read(&lli->lli_trunc_sem);
}

static int vvp_io_fsync_start(const struct lu_env *env,
			      const struct cl_io_slice *ios)
{
	/* we should mark TOWRITE bit to each dirty page in radix tree to
	 * verify pages have been written, but this is difficult because of
	 * race.
	 */
	return 0;
}

static int vvp_io_read_ahead(const struct lu_env *env,
			     const struct cl_io_slice *ios,
			     pgoff_t start, struct cl_read_ahead *ra)
{
	int result = 0;

	if (ios->cis_io->ci_type == CIT_READ ||
	    ios->cis_io->ci_type == CIT_FAULT) {
		struct vvp_io *vio = cl2vvp_io(env, ios);

		if (unlikely(vio->vui_fd->fd_flags & LL_FILE_GROUP_LOCKED)) {
			ra->cra_end = CL_PAGE_EOF;
			result = 1; /* no need to call down */
		}
	}

	return result;
}

static const struct cl_io_operations vvp_io_ops = {
	.op = {
		[CIT_READ] = {
			.cio_fini	= vvp_io_fini,
			.cio_lock      = vvp_io_read_lock,
			.cio_start     = vvp_io_read_start,
			.cio_end	= vvp_io_rw_end,
			.cio_advance	= vvp_io_advance,
		},
		[CIT_WRITE] = {
			.cio_fini      = vvp_io_fini,
			.cio_iter_init = vvp_io_write_iter_init,
			.cio_iter_fini = vvp_io_write_iter_fini,
			.cio_lock      = vvp_io_write_lock,
			.cio_start     = vvp_io_write_start,
			.cio_end	= vvp_io_rw_end,
			.cio_advance   = vvp_io_advance,
		},
		[CIT_SETATTR] = {
			.cio_fini       = vvp_io_setattr_fini,
			.cio_iter_init  = vvp_io_setattr_iter_init,
			.cio_lock       = vvp_io_setattr_lock,
			.cio_start      = vvp_io_setattr_start,
			.cio_end	= vvp_io_setattr_end
		},
		[CIT_FAULT] = {
			.cio_fini      = vvp_io_fault_fini,
			.cio_iter_init = vvp_io_fault_iter_init,
			.cio_lock      = vvp_io_fault_lock,
			.cio_start     = vvp_io_fault_start,
			.cio_end       = vvp_io_fault_end,
		},
		[CIT_FSYNC] = {
			.cio_start  = vvp_io_fsync_start,
			.cio_fini   = vvp_io_fini
		},
		[CIT_MISC] = {
			.cio_fini   = vvp_io_fini
		}
	},
	.cio_read_ahead	= vvp_io_read_ahead,
};

int vvp_io_init(const struct lu_env *env, struct cl_object *obj,
		struct cl_io *io)
{
	struct vvp_io      *vio   = vvp_env_io(env);
	struct inode       *inode = vvp_object_inode(obj);
	int		 result;

	CLOBINVRNT(env, obj, vvp_object_invariant(obj));

	CDEBUG(D_VFSTRACE, DFID
	       " ignore/verify layout %d/%d, layout version %d restore needed %d\n",
	       PFID(lu_object_fid(&obj->co_lu)),
	       io->ci_ignore_layout, io->ci_verify_layout,
	       vio->vui_layout_gen, io->ci_restore_needed);

	CL_IO_SLICE_CLEAN(vio, vui_cl);
	cl_io_slice_add(io, &vio->vui_cl, obj, &vvp_io_ops);
	vio->vui_ra_valid = false;
	result = 0;
	if (io->ci_type == CIT_READ || io->ci_type == CIT_WRITE) {
		size_t count;
		struct ll_inode_info *lli = ll_i2info(inode);

		count = io->u.ci_rw.crw_count;
		/* "If nbyte is 0, read() will return 0 and have no other
		 *  results."  -- Single Unix Spec
		 */
		if (count == 0)
			result = 1;
		else
			vio->vui_tot_count = count;

		/* for read/write, we store the jobid in the inode, and
		 * it'll be fetched by osc when building RPC.
		 *
		 * it's not accurate if the file is shared by different
		 * jobs.
		 */
		lustre_get_jobid(lli->lli_jobid);
	} else if (io->ci_type == CIT_SETATTR) {
		if (!cl_io_is_trunc(io))
			io->ci_lockreq = CILR_MANDATORY;
	}

	/* Enqueue layout lock and get layout version. We need to do this
	 * even for operations requiring to open file, such as read and write,
	 * because it might not grant layout lock in IT_OPEN.
	 */
	if (result == 0 && !io->ci_ignore_layout) {
		result = ll_layout_refresh(inode, &vio->vui_layout_gen);
		if (result == -ENOENT)
			/* If the inode on MDS has been removed, but the objects
			 * on OSTs haven't been destroyed (async unlink), layout
			 * fetch will return -ENOENT, we'd ignore this error
			 * and continue with dirty flush. LU-3230.
			 */
			result = 0;
		if (result < 0)
			CERROR("%s: refresh file layout " DFID " error %d.\n",
			       ll_get_fsname(inode->i_sb, NULL, 0),
			       PFID(lu_object_fid(&obj->co_lu)), result);
	}

	return result;
}
