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
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * cl_device and cl_device_type implementation for VVP layer.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 */

#define DEBUG_SUBSYSTEM S_LLITE


#include "../include/obd.h"
#include "../include/lustre_lite.h"
#include "llite_internal.h"
#include "vvp_internal.h"

/*****************************************************************************
 *
 * Vvp device and device type functions.
 *
 */

/*
 * vvp_ prefix stands for "Vfs Vm Posix". It corresponds to historical
 * "llite_" (var. "ll_") prefix.
 */

static struct kmem_cache *vvp_thread_kmem;
static struct kmem_cache *vvp_session_kmem;
static struct lu_kmem_descr vvp_caches[] = {
	{
		.ckd_cache = &vvp_thread_kmem,
		.ckd_name  = "vvp_thread_kmem",
		.ckd_size  = sizeof (struct vvp_thread_info),
	},
	{
		.ckd_cache = &vvp_session_kmem,
		.ckd_name  = "vvp_session_kmem",
		.ckd_size  = sizeof (struct vvp_session)
	},
	{
		.ckd_cache = NULL
	}
};

static void *vvp_key_init(const struct lu_context *ctx,
			  struct lu_context_key *key)
{
	struct vvp_thread_info *info;

	OBD_SLAB_ALLOC_PTR_GFP(info, vvp_thread_kmem, GFP_NOFS);
	if (info == NULL)
		info = ERR_PTR(-ENOMEM);
	return info;
}

static void vvp_key_fini(const struct lu_context *ctx,
			 struct lu_context_key *key, void *data)
{
	struct vvp_thread_info *info = data;
	OBD_SLAB_FREE_PTR(info, vvp_thread_kmem);
}

static void *vvp_session_key_init(const struct lu_context *ctx,
				  struct lu_context_key *key)
{
	struct vvp_session *session;

	OBD_SLAB_ALLOC_PTR_GFP(session, vvp_session_kmem, GFP_NOFS);
	if (session == NULL)
		session = ERR_PTR(-ENOMEM);
	return session;
}

static void vvp_session_key_fini(const struct lu_context *ctx,
				 struct lu_context_key *key, void *data)
{
	struct vvp_session *session = data;
	OBD_SLAB_FREE_PTR(session, vvp_session_kmem);
}


struct lu_context_key vvp_key = {
	.lct_tags = LCT_CL_THREAD,
	.lct_init = vvp_key_init,
	.lct_fini = vvp_key_fini
};

struct lu_context_key vvp_session_key = {
	.lct_tags = LCT_SESSION,
	.lct_init = vvp_session_key_init,
	.lct_fini = vvp_session_key_fini
};

/* type constructor/destructor: vvp_type_{init,fini,start,stop}(). */
LU_TYPE_INIT_FINI(vvp, &ccc_key, &ccc_session_key, &vvp_key, &vvp_session_key);

static const struct lu_device_operations vvp_lu_ops = {
	.ldo_object_alloc      = vvp_object_alloc
};

static const struct cl_device_operations vvp_cl_ops = {
	.cdo_req_init = ccc_req_init
};

static struct lu_device *vvp_device_alloc(const struct lu_env *env,
					  struct lu_device_type *t,
					  struct lustre_cfg *cfg)
{
	return ccc_device_alloc(env, t, cfg, &vvp_lu_ops, &vvp_cl_ops);
}

static const struct lu_device_type_operations vvp_device_type_ops = {
	.ldto_init = vvp_type_init,
	.ldto_fini = vvp_type_fini,

	.ldto_start = vvp_type_start,
	.ldto_stop  = vvp_type_stop,

	.ldto_device_alloc = vvp_device_alloc,
	.ldto_device_free  = ccc_device_free,
	.ldto_device_init  = ccc_device_init,
	.ldto_device_fini  = ccc_device_fini
};

struct lu_device_type vvp_device_type = {
	.ldt_tags     = LU_DEVICE_CL,
	.ldt_name     = LUSTRE_VVP_NAME,
	.ldt_ops      = &vvp_device_type_ops,
	.ldt_ctx_tags = LCT_CL_THREAD
};

/**
 * A mutex serializing calls to vvp_inode_fini() under extreme memory
 * pressure, when environments cannot be allocated.
 */
int vvp_global_init(void)
{
	int result;

	result = lu_kmem_init(vvp_caches);
	if (result == 0) {
		result = ccc_global_init(&vvp_device_type);
		if (result != 0)
			lu_kmem_fini(vvp_caches);
	}
	return result;
}

void vvp_global_fini(void)
{
	ccc_global_fini(&vvp_device_type);
	lu_kmem_fini(vvp_caches);
}


/*****************************************************************************
 *
 * mirror obd-devices into cl devices.
 *
 */

int cl_sb_init(struct super_block *sb)
{
	struct ll_sb_info *sbi;
	struct cl_device  *cl;
	struct lu_env     *env;
	int rc = 0;
	int refcheck;

	sbi  = ll_s2sbi(sb);
	env = cl_env_get(&refcheck);
	if (!IS_ERR(env)) {
		cl = cl_type_setup(env, NULL, &vvp_device_type,
				   sbi->ll_dt_exp->exp_obd->obd_lu_dev);
		if (!IS_ERR(cl)) {
			cl2ccc_dev(cl)->cdv_sb = sb;
			sbi->ll_cl = cl;
			sbi->ll_site = cl2lu_dev(cl)->ld_site;
		}
		cl_env_put(env, &refcheck);
	} else
		rc = PTR_ERR(env);
	return rc;
}

int cl_sb_fini(struct super_block *sb)
{
	struct ll_sb_info *sbi;
	struct lu_env     *env;
	struct cl_device  *cld;
	int		refcheck;
	int		result;

	sbi = ll_s2sbi(sb);
	env = cl_env_get(&refcheck);
	if (!IS_ERR(env)) {
		cld = sbi->ll_cl;

		if (cld != NULL) {
			cl_stack_fini(env, cld);
			sbi->ll_cl = NULL;
			sbi->ll_site = NULL;
		}
		cl_env_put(env, &refcheck);
		result = 0;
	} else {
		CERROR("Cannot cleanup cl-stack due to memory shortage.\n");
		result = PTR_ERR(env);
	}
	/*
	 * If mount failed (sbi->ll_cl == NULL), and this there are no other
	 * mounts, stop device types manually (this usually happens
	 * automatically when last device is destroyed).
	 */
	lu_types_stop();
	return result;
}

/****************************************************************************
 *
 * /proc/fs/lustre/llite/$MNT/dump_page_cache
 *
 ****************************************************************************/

/*
 * To represent contents of a page cache as a byte stream, following
 * information if encoded in 64bit offset:
 *
 *       - file hash bucket in lu_site::ls_hash[]       28bits
 *
 *       - how far file is from bucket head	      4bits
 *
 *       - page index				   32bits
 *
 * First two data identify a file in the cache uniquely.
 */

#define PGC_OBJ_SHIFT (32 + 4)
#define PGC_DEPTH_SHIFT (32)

struct vvp_pgcache_id {
	unsigned		 vpi_bucket;
	unsigned		 vpi_depth;
	uint32_t		 vpi_index;

	unsigned		 vpi_curdep;
	struct lu_object_header *vpi_obj;
};

static void vvp_pgcache_id_unpack(loff_t pos, struct vvp_pgcache_id *id)
{
	CLASSERT(sizeof(pos) == sizeof(__u64));

	id->vpi_index  = pos & 0xffffffff;
	id->vpi_depth  = (pos >> PGC_DEPTH_SHIFT) & 0xf;
	id->vpi_bucket = ((unsigned long long)pos >> PGC_OBJ_SHIFT);
}

static loff_t vvp_pgcache_id_pack(struct vvp_pgcache_id *id)
{
	return
		((__u64)id->vpi_index) |
		((__u64)id->vpi_depth  << PGC_DEPTH_SHIFT) |
		((__u64)id->vpi_bucket << PGC_OBJ_SHIFT);
}

static int vvp_pgcache_obj_get(struct cfs_hash *hs, struct cfs_hash_bd *bd,
			       struct hlist_node *hnode, void *data)
{
	struct vvp_pgcache_id   *id  = data;
	struct lu_object_header *hdr = cfs_hash_object(hs, hnode);

	if (id->vpi_curdep-- > 0)
		return 0; /* continue */

	if (lu_object_is_dying(hdr))
		return 1;

	cfs_hash_get(hs, hnode);
	id->vpi_obj = hdr;
	return 1;
}

static struct cl_object *vvp_pgcache_obj(const struct lu_env *env,
					 struct lu_device *dev,
					 struct vvp_pgcache_id *id)
{
	LASSERT(lu_device_is_cl(dev));

	id->vpi_depth &= 0xf;
	id->vpi_obj    = NULL;
	id->vpi_curdep = id->vpi_depth;

	cfs_hash_hlist_for_each(dev->ld_site->ls_obj_hash, id->vpi_bucket,
				vvp_pgcache_obj_get, id);
	if (id->vpi_obj != NULL) {
		struct lu_object *lu_obj;

		lu_obj = lu_object_locate(id->vpi_obj, dev->ld_type);
		if (lu_obj != NULL) {
			lu_object_ref_add(lu_obj, "dump", current);
			return lu2cl(lu_obj);
		}
		lu_object_put(env, lu_object_top(id->vpi_obj));

	} else if (id->vpi_curdep > 0) {
		id->vpi_depth = 0xf;
	}
	return NULL;
}

static loff_t vvp_pgcache_find(const struct lu_env *env,
			       struct lu_device *dev, loff_t pos)
{
	struct cl_object     *clob;
	struct lu_site       *site;
	struct vvp_pgcache_id id;

	site = dev->ld_site;
	vvp_pgcache_id_unpack(pos, &id);

	while (1) {
		if (id.vpi_bucket >= CFS_HASH_NHLIST(site->ls_obj_hash))
			return ~0ULL;
		clob = vvp_pgcache_obj(env, dev, &id);
		if (clob != NULL) {
			struct cl_object_header *hdr;
			int		      nr;
			struct cl_page	  *pg;

			/* got an object. Find next page. */
			hdr = cl_object_header(clob);

			spin_lock(&hdr->coh_page_guard);
			nr = radix_tree_gang_lookup(&hdr->coh_tree,
						    (void **)&pg,
						    id.vpi_index, 1);
			if (nr > 0) {
				id.vpi_index = pg->cp_index;
				/* Cant support over 16T file */
				nr = !(pg->cp_index > 0xffffffff);
			}
			spin_unlock(&hdr->coh_page_guard);

			lu_object_ref_del(&clob->co_lu, "dump", current);
			cl_object_put(env, clob);
			if (nr > 0)
				return vvp_pgcache_id_pack(&id);
		}
		/* to the next object. */
		++id.vpi_depth;
		id.vpi_depth &= 0xf;
		if (id.vpi_depth == 0 && ++id.vpi_bucket == 0)
			return ~0ULL;
		id.vpi_index = 0;
	}
}

#define seq_page_flag(seq, page, flag, has_flags) do {		  \
	if (test_bit(PG_##flag, &(page)->flags)) {		  \
		seq_printf(seq, "%s"#flag, has_flags ? "|" : "");       \
		has_flags = 1;					  \
	}							       \
} while(0)

static void vvp_pgcache_page_show(const struct lu_env *env,
				  struct seq_file *seq, struct cl_page *page)
{
	struct ccc_page *cpg;
	struct page      *vmpage;
	int	      has_flags;

	cpg = cl2ccc_page(cl_page_at(page, &vvp_device_type));
	vmpage = cpg->cpg_page;
	seq_printf(seq," %5i | %p %p %s %s %s %s | %p %lu/%u(%p) %lu %u [",
		   0 /* gen */,
		   cpg, page,
		   "none",
		   cpg->cpg_write_queued ? "wq" : "- ",
		   cpg->cpg_defer_uptodate ? "du" : "- ",
		   PageWriteback(vmpage) ? "wb" : "-",
		   vmpage, vmpage->mapping->host->i_ino,
		   vmpage->mapping->host->i_generation,
		   vmpage->mapping->host, vmpage->index,
		   page_count(vmpage));
	has_flags = 0;
	seq_page_flag(seq, vmpage, locked, has_flags);
	seq_page_flag(seq, vmpage, error, has_flags);
	seq_page_flag(seq, vmpage, referenced, has_flags);
	seq_page_flag(seq, vmpage, uptodate, has_flags);
	seq_page_flag(seq, vmpage, dirty, has_flags);
	seq_page_flag(seq, vmpage, writeback, has_flags);
	seq_printf(seq, "%s]\n", has_flags ? "" : "-");
}

static int vvp_pgcache_show(struct seq_file *f, void *v)
{
	loff_t		   pos;
	struct ll_sb_info       *sbi;
	struct cl_object	*clob;
	struct lu_env	   *env;
	struct cl_page	  *page;
	struct cl_object_header *hdr;
	struct vvp_pgcache_id    id;
	int		      refcheck;
	int		      result;

	env = cl_env_get(&refcheck);
	if (!IS_ERR(env)) {
		pos = *(loff_t *) v;
		vvp_pgcache_id_unpack(pos, &id);
		sbi = f->private;
		clob = vvp_pgcache_obj(env, &sbi->ll_cl->cd_lu_dev, &id);
		if (clob != NULL) {
			hdr = cl_object_header(clob);

			spin_lock(&hdr->coh_page_guard);
			page = cl_page_lookup(hdr, id.vpi_index);
			spin_unlock(&hdr->coh_page_guard);

			seq_printf(f, "%8x@"DFID": ",
				   id.vpi_index, PFID(&hdr->coh_lu.loh_fid));
			if (page != NULL) {
				vvp_pgcache_page_show(env, f, page);
				cl_page_put(env, page);
			} else
				seq_puts(f, "missing\n");
			lu_object_ref_del(&clob->co_lu, "dump", current);
			cl_object_put(env, clob);
		} else
			seq_printf(f, "%llx missing\n", pos);
		cl_env_put(env, &refcheck);
		result = 0;
	} else
		result = PTR_ERR(env);
	return result;
}

static void *vvp_pgcache_start(struct seq_file *f, loff_t *pos)
{
	struct ll_sb_info *sbi;
	struct lu_env     *env;
	int		refcheck;

	sbi = f->private;

	env = cl_env_get(&refcheck);
	if (!IS_ERR(env)) {
		sbi = f->private;
		if (sbi->ll_site->ls_obj_hash->hs_cur_bits > 64 - PGC_OBJ_SHIFT)
			pos = ERR_PTR(-EFBIG);
		else {
			*pos = vvp_pgcache_find(env, &sbi->ll_cl->cd_lu_dev,
						*pos);
			if (*pos == ~0ULL)
				pos = NULL;
		}
		cl_env_put(env, &refcheck);
	}
	return pos;
}

static void *vvp_pgcache_next(struct seq_file *f, void *v, loff_t *pos)
{
	struct ll_sb_info *sbi;
	struct lu_env     *env;
	int		refcheck;

	env = cl_env_get(&refcheck);
	if (!IS_ERR(env)) {
		sbi = f->private;
		*pos = vvp_pgcache_find(env, &sbi->ll_cl->cd_lu_dev, *pos + 1);
		if (*pos == ~0ULL)
			pos = NULL;
		cl_env_put(env, &refcheck);
	}
	return pos;
}

static void vvp_pgcache_stop(struct seq_file *f, void *v)
{
	/* Nothing to do */
}

static struct seq_operations vvp_pgcache_ops = {
	.start = vvp_pgcache_start,
	.next  = vvp_pgcache_next,
	.stop  = vvp_pgcache_stop,
	.show  = vvp_pgcache_show
};

static int vvp_dump_pgcache_seq_open(struct inode *inode, struct file *filp)
{
	struct ll_sb_info     *sbi = PDE_DATA(inode);
	struct seq_file       *seq;
	int		    result;

	result = seq_open(filp, &vvp_pgcache_ops);
	if (result == 0) {
		seq = filp->private_data;
		seq->private = sbi;
	}
	return result;
}

const struct file_operations vvp_dump_pgcache_file_ops = {
	.owner   = THIS_MODULE,
	.open    = vvp_dump_pgcache_seq_open,
	.read    = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};
