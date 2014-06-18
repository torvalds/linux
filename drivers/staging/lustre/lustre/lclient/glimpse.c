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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * glimpse code shared between vvp and liblustre (and other Lustre clients in
 * the future).
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 *   Author: Oleg Drokin <oleg.drokin@sun.com>
 */

#include <linux/libcfs/libcfs.h>
#include <obd_class.h>
#include <obd_support.h>
#include <obd.h>

# include <lustre_dlm.h>
# include <lustre_lite.h>
# include <lustre_mdc.h>
# include <linux/pagemap.h>
# include <linux/file.h>

#include "cl_object.h"
#include "lclient.h"
# include "../llite/llite_internal.h"

static const struct cl_lock_descr whole_file = {
	.cld_start = 0,
	.cld_end   = CL_PAGE_EOF,
	.cld_mode  = CLM_READ
};

/*
 * Check whether file has possible unwriten pages.
 *
 * \retval 1    file is mmap-ed or has dirty pages
 *	 0    otherwise
 */
blkcnt_t dirty_cnt(struct inode *inode)
{
	blkcnt_t cnt = 0;
	struct ccc_object *vob = cl_inode2ccc(inode);
	void	      *results[1];

	if (inode->i_mapping != NULL)
		cnt += radix_tree_gang_lookup_tag(&inode->i_mapping->page_tree,
						  results, 0, 1,
						  PAGECACHE_TAG_DIRTY);
	if (cnt == 0 && atomic_read(&vob->cob_mmap_cnt) > 0)
		cnt = 1;

	return (cnt > 0) ? 1 : 0;
}

int cl_glimpse_lock(const struct lu_env *env, struct cl_io *io,
		    struct inode *inode, struct cl_object *clob, int agl)
{
	struct cl_lock_descr *descr = &ccc_env_info(env)->cti_descr;
	struct cl_inode_info *lli   = cl_i2info(inode);
	const struct lu_fid  *fid   = lu_object_fid(&clob->co_lu);
	struct ccc_io	*cio   = ccc_env_io(env);
	struct cl_lock       *lock;
	int result;

	result = 0;
	if (!(lli->lli_flags & LLIF_MDS_SIZE_LOCK)) {
		CDEBUG(D_DLMTRACE, "Glimpsing inode "DFID"\n", PFID(fid));
		if (lli->lli_has_smd) {
			/* NOTE: this looks like DLM lock request, but it may
			 *       not be one. Due to CEF_ASYNC flag (translated
			 *       to LDLM_FL_HAS_INTENT by osc), this is
			 *       glimpse request, that won't revoke any
			 *       conflicting DLM locks held. Instead,
			 *       ll_glimpse_callback() will be called on each
			 *       client holding a DLM lock against this file,
			 *       and resulting size will be returned for each
			 *       stripe. DLM lock on [0, EOF] is acquired only
			 *       if there were no conflicting locks. If there
			 *       were conflicting locks, enqueuing or waiting
			 *       fails with -ENAVAIL, but valid inode
			 *       attributes are returned anyway. */
			*descr = whole_file;
			descr->cld_obj   = clob;
			descr->cld_mode  = CLM_PHANTOM;
			descr->cld_enq_flags = CEF_ASYNC | CEF_MUST;
			if (agl)
				descr->cld_enq_flags |= CEF_AGL;
			cio->cui_glimpse = 1;
			/*
			 * CEF_ASYNC is used because glimpse sub-locks cannot
			 * deadlock (because they never conflict with other
			 * locks) and, hence, can be enqueued out-of-order.
			 *
			 * CEF_MUST protects glimpse lock from conversion into
			 * a lockless mode.
			 */
			lock = cl_lock_request(env, io, descr, "glimpse",
					       current);
			cio->cui_glimpse = 0;

			if (lock == NULL)
				return 0;

			if (IS_ERR(lock))
				return PTR_ERR(lock);

			LASSERT(agl == 0);
			result = cl_wait(env, lock);
			if (result == 0) {
				cl_merge_lvb(env, inode);
				if (cl_isize_read(inode) > 0 &&
				    inode->i_blocks == 0) {
					/*
					 * LU-417: Add dirty pages block count
					 * lest i_blocks reports 0, some "cp" or
					 * "tar" may think it's a completely
					 * sparse file and skip it.
					 */
					inode->i_blocks = dirty_cnt(inode);
				}
				cl_unuse(env, lock);
			}
			cl_lock_release(env, lock, "glimpse", current);
		} else {
			CDEBUG(D_DLMTRACE, "No objects for inode\n");
			cl_merge_lvb(env, inode);
		}
	}

	return result;
}

static int cl_io_get(struct inode *inode, struct lu_env **envout,
		     struct cl_io **ioout, int *refcheck)
{
	struct lu_env	  *env;
	struct cl_io	   *io;
	struct cl_inode_info   *lli = cl_i2info(inode);
	struct cl_object       *clob = lli->lli_clob;
	int result;

	if (S_ISREG(cl_inode_mode(inode))) {
		env = cl_env_get(refcheck);
		if (!IS_ERR(env)) {
			io = ccc_env_thread_io(env);
			io->ci_obj = clob;
			*envout = env;
			*ioout  = io;
			result = 1;
		} else
			result = PTR_ERR(env);
	} else
		result = 0;
	return result;
}

int cl_glimpse_size0(struct inode *inode, int agl)
{
	/*
	 * We don't need ast_flags argument to cl_glimpse_size(), because
	 * osc_lock_enqueue() takes care of the possible deadlock that said
	 * argument was introduced to avoid.
	 */
	/*
	 * XXX but note that ll_file_seek() passes LDLM_FL_BLOCK_NOWAIT to
	 * cl_glimpse_size(), which doesn't make sense: glimpse locks are not
	 * blocking anyway.
	 */
	struct lu_env	  *env = NULL;
	struct cl_io	   *io  = NULL;
	int		     result;
	int		     refcheck;

	result = cl_io_get(inode, &env, &io, &refcheck);
	if (result > 0) {
again:
		io->ci_verify_layout = 1;
		result = cl_io_init(env, io, CIT_MISC, io->ci_obj);
		if (result > 0)
			/*
			 * nothing to do for this io. This currently happens
			 * when stripe sub-object's are not yet created.
			 */
			result = io->ci_result;
		else if (result == 0)
			result = cl_glimpse_lock(env, io, inode, io->ci_obj,
						 agl);

		OBD_FAIL_TIMEOUT(OBD_FAIL_GLIMPSE_DELAY, 2);
		cl_io_fini(env, io);
		if (unlikely(io->ci_need_restart))
			goto again;
		cl_env_put(env, &refcheck);
	}
	return result;
}

int cl_local_size(struct inode *inode)
{
	struct lu_env	   *env = NULL;
	struct cl_io	    *io  = NULL;
	struct ccc_thread_info  *cti;
	struct cl_object	*clob;
	struct cl_lock_descr    *descr;
	struct cl_lock	  *lock;
	int		      result;
	int		      refcheck;

	if (!cl_i2info(inode)->lli_has_smd)
		return 0;

	result = cl_io_get(inode, &env, &io, &refcheck);
	if (result <= 0)
		return result;

	clob = io->ci_obj;
	result = cl_io_init(env, io, CIT_MISC, clob);
	if (result > 0)
		result = io->ci_result;
	else if (result == 0) {
		cti = ccc_env_info(env);
		descr = &cti->cti_descr;

		*descr = whole_file;
		descr->cld_obj = clob;
		lock = cl_lock_peek(env, io, descr, "localsize", current);
		if (lock != NULL) {
			cl_merge_lvb(env, inode);
			cl_unuse(env, lock);
			cl_lock_release(env, lock, "localsize", current);
			result = 0;
		} else
			result = -ENODATA;
	}
	cl_io_fini(env, io);
	cl_env_put(env, &refcheck);
	return result;
}
