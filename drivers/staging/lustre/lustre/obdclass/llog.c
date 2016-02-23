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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/obdclass/llog.c
 *
 * OST<->MDS recovery logging infrastructure.
 * Invariants in implementation:
 * - we do not share logs among different OST<->MDS connections, so that
 *   if an OST or MDS fails it need only look at log(s) relevant to itself
 *
 * Author: Andreas Dilger <adilger@clusterfs.com>
 * Author: Alex Zhuravlev <bzzz@whamcloud.com>
 * Author: Mikhail Pershin <tappro@whamcloud.com>
 */

#define DEBUG_SUBSYSTEM S_LOG

#include "../include/obd_class.h"
#include "../include/lustre_log.h"
#include "llog_internal.h"

/*
 * Allocate a new log or catalog handle
 * Used inside llog_open().
 */
static struct llog_handle *llog_alloc_handle(void)
{
	struct llog_handle *loghandle;

	loghandle = kzalloc(sizeof(*loghandle), GFP_NOFS);
	if (!loghandle)
		return NULL;

	init_rwsem(&loghandle->lgh_lock);
	spin_lock_init(&loghandle->lgh_hdr_lock);
	INIT_LIST_HEAD(&loghandle->u.phd.phd_entry);
	atomic_set(&loghandle->lgh_refcount, 1);

	return loghandle;
}

/*
 * Free llog handle and header data if exists. Used in llog_close() only
 */
static void llog_free_handle(struct llog_handle *loghandle)
{
	LASSERT(loghandle != NULL);

	/* failed llog_init_handle */
	if (!loghandle->lgh_hdr)
		goto out;

	if (loghandle->lgh_hdr->llh_flags & LLOG_F_IS_PLAIN)
		LASSERT(list_empty(&loghandle->u.phd.phd_entry));
	else if (loghandle->lgh_hdr->llh_flags & LLOG_F_IS_CAT)
		LASSERT(list_empty(&loghandle->u.chd.chd_head));
	LASSERT(sizeof(*(loghandle->lgh_hdr)) == LLOG_CHUNK_SIZE);
	kfree(loghandle->lgh_hdr);
out:
	kfree(loghandle);
}

void llog_handle_get(struct llog_handle *loghandle)
{
	atomic_inc(&loghandle->lgh_refcount);
}

void llog_handle_put(struct llog_handle *loghandle)
{
	LASSERT(atomic_read(&loghandle->lgh_refcount) > 0);
	if (atomic_dec_and_test(&loghandle->lgh_refcount))
		llog_free_handle(loghandle);
}

static int llog_read_header(const struct lu_env *env,
			    struct llog_handle *handle,
			    struct obd_uuid *uuid)
{
	struct llog_operations *lop;
	int rc;

	rc = llog_handle2ops(handle, &lop);
	if (rc)
		return rc;

	if (lop->lop_read_header == NULL)
		return -EOPNOTSUPP;

	rc = lop->lop_read_header(env, handle);
	if (rc == LLOG_EEMPTY) {
		struct llog_log_hdr *llh = handle->lgh_hdr;

		handle->lgh_last_idx = 0; /* header is record with index 0 */
		llh->llh_count = 1;	 /* for the header record */
		llh->llh_hdr.lrh_type = LLOG_HDR_MAGIC;
		llh->llh_hdr.lrh_len = llh->llh_tail.lrt_len = LLOG_CHUNK_SIZE;
		llh->llh_hdr.lrh_index = llh->llh_tail.lrt_index = 0;
		llh->llh_timestamp = ktime_get_real_seconds();
		if (uuid)
			memcpy(&llh->llh_tgtuuid, uuid,
			       sizeof(llh->llh_tgtuuid));
		llh->llh_bitmap_offset = offsetof(typeof(*llh), llh_bitmap);
		ext2_set_bit(0, llh->llh_bitmap);
		rc = 0;
	}
	return rc;
}

int llog_init_handle(const struct lu_env *env, struct llog_handle *handle,
		     int flags, struct obd_uuid *uuid)
{
	struct llog_log_hdr	*llh;
	int			 rc;

	LASSERT(handle->lgh_hdr == NULL);

	llh = kzalloc(sizeof(*llh), GFP_NOFS);
	if (!llh)
		return -ENOMEM;
	handle->lgh_hdr = llh;
	/* first assign flags to use llog_client_ops */
	llh->llh_flags = flags;
	rc = llog_read_header(env, handle, uuid);
	if (rc == 0) {
		if (unlikely((llh->llh_flags & LLOG_F_IS_PLAIN &&
			      flags & LLOG_F_IS_CAT) ||
			     (llh->llh_flags & LLOG_F_IS_CAT &&
			      flags & LLOG_F_IS_PLAIN))) {
			CERROR("%s: llog type is %s but initializing %s\n",
			       handle->lgh_ctxt->loc_obd->obd_name,
			       llh->llh_flags & LLOG_F_IS_CAT ?
			       "catalog" : "plain",
			       flags & LLOG_F_IS_CAT ? "catalog" : "plain");
			rc = -EINVAL;
			goto out;
		} else if (llh->llh_flags &
			   (LLOG_F_IS_PLAIN | LLOG_F_IS_CAT)) {
			/*
			 * it is possible to open llog without specifying llog
			 * type so it is taken from llh_flags
			 */
			flags = llh->llh_flags;
		} else {
			/* for some reason the llh_flags has no type set */
			CERROR("llog type is not specified!\n");
			rc = -EINVAL;
			goto out;
		}
		if (unlikely(uuid &&
			     !obd_uuid_equals(uuid, &llh->llh_tgtuuid))) {
			CERROR("%s: llog uuid mismatch: %s/%s\n",
			       handle->lgh_ctxt->loc_obd->obd_name,
			       (char *)uuid->uuid,
			       (char *)llh->llh_tgtuuid.uuid);
			rc = -EEXIST;
			goto out;
		}
	}
	if (flags & LLOG_F_IS_CAT) {
		LASSERT(list_empty(&handle->u.chd.chd_head));
		INIT_LIST_HEAD(&handle->u.chd.chd_head);
		llh->llh_size = sizeof(struct llog_logid_rec);
	} else if (!(flags & LLOG_F_IS_PLAIN)) {
		CERROR("%s: unknown flags: %#x (expected %#x or %#x)\n",
		       handle->lgh_ctxt->loc_obd->obd_name,
		       flags, LLOG_F_IS_CAT, LLOG_F_IS_PLAIN);
		rc = -EINVAL;
	}
out:
	if (rc) {
		kfree(llh);
		handle->lgh_hdr = NULL;
	}
	return rc;
}
EXPORT_SYMBOL(llog_init_handle);

static int llog_process_thread(void *arg)
{
	struct llog_process_info	*lpi = arg;
	struct llog_handle		*loghandle = lpi->lpi_loghandle;
	struct llog_log_hdr		*llh = loghandle->lgh_hdr;
	struct llog_process_cat_data	*cd  = lpi->lpi_catdata;
	char				*buf;
	__u64				 cur_offset = LLOG_CHUNK_SIZE;
	__u64				 last_offset;
	int				 rc = 0, index = 1, last_index;
	int				 saved_index = 0;
	int				 last_called_index = 0;

	LASSERT(llh);

	buf = kzalloc(LLOG_CHUNK_SIZE, GFP_NOFS);
	if (!buf) {
		lpi->lpi_rc = -ENOMEM;
		return 0;
	}

	if (cd != NULL) {
		last_called_index = cd->lpcd_first_idx;
		index = cd->lpcd_first_idx + 1;
	}
	if (cd != NULL && cd->lpcd_last_idx)
		last_index = cd->lpcd_last_idx;
	else
		last_index = LLOG_BITMAP_BYTES * 8 - 1;

	while (rc == 0) {
		struct llog_rec_hdr *rec;

		/* skip records not set in bitmap */
		while (index <= last_index &&
		       !ext2_test_bit(index, llh->llh_bitmap))
			++index;

		LASSERT(index <= last_index + 1);
		if (index == last_index + 1)
			break;
repeat:
		CDEBUG(D_OTHER, "index: %d last_index %d\n",
		       index, last_index);

		/* get the buf with our target record; avoid old garbage */
		memset(buf, 0, LLOG_CHUNK_SIZE);
		last_offset = cur_offset;
		rc = llog_next_block(lpi->lpi_env, loghandle, &saved_index,
				     index, &cur_offset, buf, LLOG_CHUNK_SIZE);
		if (rc)
			goto out;

		/* NB: when rec->lrh_len is accessed it is already swabbed
		 * since it is used at the "end" of the loop and the rec
		 * swabbing is done at the beginning of the loop. */
		for (rec = (struct llog_rec_hdr *)buf;
		     (char *)rec < buf + LLOG_CHUNK_SIZE;
		     rec = (struct llog_rec_hdr *)((char *)rec + rec->lrh_len)) {

			CDEBUG(D_OTHER, "processing rec 0x%p type %#x\n",
			       rec, rec->lrh_type);

			if (LLOG_REC_HDR_NEEDS_SWABBING(rec))
				lustre_swab_llog_rec(rec);

			CDEBUG(D_OTHER, "after swabbing, type=%#x idx=%d\n",
			       rec->lrh_type, rec->lrh_index);

			if (rec->lrh_index == 0) {
				/* probably another rec just got added? */
				rc = 0;
				if (index <= loghandle->lgh_last_idx)
					goto repeat;
				goto out; /* no more records */
			}
			if (rec->lrh_len == 0 ||
			    rec->lrh_len > LLOG_CHUNK_SIZE) {
				CWARN("invalid length %d in llog record for index %d/%d\n",
				      rec->lrh_len,
				      rec->lrh_index, index);
				rc = -EINVAL;
				goto out;
			}

			if (rec->lrh_index < index) {
				CDEBUG(D_OTHER, "skipping lrh_index %d\n",
				       rec->lrh_index);
				continue;
			}

			CDEBUG(D_OTHER,
			       "lrh_index: %d lrh_len: %d (%d remains)\n",
			       rec->lrh_index, rec->lrh_len,
			       (int)(buf + LLOG_CHUNK_SIZE - (char *)rec));

			loghandle->lgh_cur_idx = rec->lrh_index;
			loghandle->lgh_cur_offset = (char *)rec - (char *)buf +
						    last_offset;

			/* if set, process the callback on this record */
			if (ext2_test_bit(index, llh->llh_bitmap)) {
				rc = lpi->lpi_cb(lpi->lpi_env, loghandle, rec,
						 lpi->lpi_cbdata);
				last_called_index = index;
				if (rc)
					goto out;
			} else {
				CDEBUG(D_OTHER, "Skipped index %d\n", index);
			}

			/* next record, still in buffer? */
			++index;
			if (index > last_index) {
				rc = 0;
				goto out;
			}
		}
	}

out:
	if (cd != NULL)
		cd->lpcd_last_idx = last_called_index;

	kfree(buf);
	lpi->lpi_rc = rc;
	return 0;
}

static int llog_process_thread_daemonize(void *arg)
{
	struct llog_process_info	*lpi = arg;
	struct lu_env			 env;
	int				 rc;

	unshare_fs_struct();

	/* client env has no keys, tags is just 0 */
	rc = lu_env_init(&env, LCT_LOCAL | LCT_MG_THREAD);
	if (rc)
		goto out;
	lpi->lpi_env = &env;

	rc = llog_process_thread(arg);

	lu_env_fini(&env);
out:
	complete(&lpi->lpi_completion);
	return rc;
}

int llog_process_or_fork(const struct lu_env *env,
			 struct llog_handle *loghandle,
			 llog_cb_t cb, void *data, void *catdata, bool fork)
{
	struct llog_process_info *lpi;
	int		      rc;

	lpi = kzalloc(sizeof(*lpi), GFP_NOFS);
	if (!lpi) {
		CERROR("cannot alloc pointer\n");
		return -ENOMEM;
	}
	lpi->lpi_loghandle = loghandle;
	lpi->lpi_cb	= cb;
	lpi->lpi_cbdata    = data;
	lpi->lpi_catdata   = catdata;

	if (fork) {
		/* The new thread can't use parent env,
		 * init the new one in llog_process_thread_daemonize. */
		lpi->lpi_env = NULL;
		init_completion(&lpi->lpi_completion);
		rc = PTR_ERR(kthread_run(llog_process_thread_daemonize, lpi,
					     "llog_process_thread"));
		if (IS_ERR_VALUE(rc)) {
			CERROR("%s: cannot start thread: rc = %d\n",
			       loghandle->lgh_ctxt->loc_obd->obd_name, rc);
			kfree(lpi);
			return rc;
		}
		wait_for_completion(&lpi->lpi_completion);
	} else {
		lpi->lpi_env = env;
		llog_process_thread(lpi);
	}
	rc = lpi->lpi_rc;
	kfree(lpi);
	return rc;
}
EXPORT_SYMBOL(llog_process_or_fork);

int llog_process(const struct lu_env *env, struct llog_handle *loghandle,
		 llog_cb_t cb, void *data, void *catdata)
{
	return llog_process_or_fork(env, loghandle, cb, data, catdata, true);
}
EXPORT_SYMBOL(llog_process);

int llog_open(const struct lu_env *env, struct llog_ctxt *ctxt,
	      struct llog_handle **lgh, struct llog_logid *logid,
	      char *name, enum llog_open_param open_param)
{
	int	 raised;
	int	 rc;

	LASSERT(ctxt);
	LASSERT(ctxt->loc_logops);

	if (ctxt->loc_logops->lop_open == NULL) {
		*lgh = NULL;
		return -EOPNOTSUPP;
	}

	*lgh = llog_alloc_handle();
	if (*lgh == NULL)
		return -ENOMEM;
	(*lgh)->lgh_ctxt = ctxt;
	(*lgh)->lgh_logops = ctxt->loc_logops;

	raised = cfs_cap_raised(CFS_CAP_SYS_RESOURCE);
	if (!raised)
		cfs_cap_raise(CFS_CAP_SYS_RESOURCE);
	rc = ctxt->loc_logops->lop_open(env, *lgh, logid, name, open_param);
	if (!raised)
		cfs_cap_lower(CFS_CAP_SYS_RESOURCE);
	if (rc) {
		llog_free_handle(*lgh);
		*lgh = NULL;
	}
	return rc;
}
EXPORT_SYMBOL(llog_open);

int llog_close(const struct lu_env *env, struct llog_handle *loghandle)
{
	struct llog_operations	*lop;
	int			 rc;

	rc = llog_handle2ops(loghandle, &lop);
	if (rc)
		goto out;
	if (lop->lop_close == NULL) {
		rc = -EOPNOTSUPP;
		goto out;
	}
	rc = lop->lop_close(env, loghandle);
out:
	llog_handle_put(loghandle);
	return rc;
}
EXPORT_SYMBOL(llog_close);
