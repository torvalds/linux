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
 * Copyright (c) 2012, Intel Corporation.
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

	OBD_ALLOC_PTR(loghandle);
	if (loghandle == NULL)
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
	OBD_FREE(loghandle->lgh_hdr, LLOG_CHUNK_SIZE);
out:
	OBD_FREE_PTR(loghandle);
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

/* returns negative on error; 0 if success; 1 if success & log destroyed */
int llog_cancel_rec(const struct lu_env *env, struct llog_handle *loghandle,
		    int index)
{
	struct llog_log_hdr *llh = loghandle->lgh_hdr;
	int rc = 0;

	CDEBUG(D_RPCTRACE, "Canceling %d in log "DOSTID"\n",
	       index, POSTID(&loghandle->lgh_id.lgl_oi));

	if (index == 0) {
		CERROR("Can't cancel index 0 which is header\n");
		return -EINVAL;
	}

	spin_lock(&loghandle->lgh_hdr_lock);
	if (!ext2_clear_bit(index, llh->llh_bitmap)) {
		spin_unlock(&loghandle->lgh_hdr_lock);
		CDEBUG(D_RPCTRACE, "Catalog index %u already clear?\n", index);
		return -ENOENT;
	}

	llh->llh_count--;

	if ((llh->llh_flags & LLOG_F_ZAP_WHEN_EMPTY) &&
	    (llh->llh_count == 1) &&
	    (loghandle->lgh_last_idx == (LLOG_BITMAP_BYTES * 8) - 1)) {
		spin_unlock(&loghandle->lgh_hdr_lock);
		rc = llog_destroy(env, loghandle);
		if (rc < 0) {
			CERROR("%s: can't destroy empty llog #"DOSTID
			       "#%08x: rc = %d\n",
			       loghandle->lgh_ctxt->loc_obd->obd_name,
			       POSTID(&loghandle->lgh_id.lgl_oi),
			       loghandle->lgh_id.lgl_ogen, rc);
			goto out_err;
		}
		return 1;
	}
	spin_unlock(&loghandle->lgh_hdr_lock);

	rc = llog_write(env, loghandle, &llh->llh_hdr, NULL, 0, NULL, 0);
	if (rc < 0) {
		CERROR("%s: fail to write header for llog #"DOSTID
		       "#%08x: rc = %d\n",
		       loghandle->lgh_ctxt->loc_obd->obd_name,
		       POSTID(&loghandle->lgh_id.lgl_oi),
		       loghandle->lgh_id.lgl_ogen, rc);
		goto out_err;
	}
	return 0;
out_err:
	spin_lock(&loghandle->lgh_hdr_lock);
	ext2_set_bit(index, llh->llh_bitmap);
	llh->llh_count++;
	spin_unlock(&loghandle->lgh_hdr_lock);
	return rc;
}
EXPORT_SYMBOL(llog_cancel_rec);

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
		llh->llh_timestamp = get_seconds();
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

	OBD_ALLOC_PTR(llh);
	if (llh == NULL)
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
		OBD_FREE_PTR(llh);
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

	OBD_ALLOC(buf, LLOG_CHUNK_SIZE);
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
		     rec = (struct llog_rec_hdr *)((char *)rec + rec->lrh_len)){

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
				CWARN("invalid length %d in llog record for "
				      "index %d/%d\n", rec->lrh_len,
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
				if (rc == LLOG_PROC_BREAK) {
					goto out;
				} else if (rc == LLOG_DEL_RECORD) {
					llog_cancel_rec(lpi->lpi_env,
							loghandle,
							rec->lrh_index);
					rc = 0;
				}
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

	OBD_FREE(buf, LLOG_CHUNK_SIZE);
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

	OBD_ALLOC_PTR(lpi);
	if (lpi == NULL) {
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
			OBD_FREE_PTR(lpi);
			return rc;
		}
		wait_for_completion(&lpi->lpi_completion);
	} else {
		lpi->lpi_env = env;
		llog_process_thread(lpi);
	}
	rc = lpi->lpi_rc;
	OBD_FREE_PTR(lpi);
	return rc;
}
EXPORT_SYMBOL(llog_process_or_fork);

int llog_process(const struct lu_env *env, struct llog_handle *loghandle,
		 llog_cb_t cb, void *data, void *catdata)
{
	return llog_process_or_fork(env, loghandle, cb, data, catdata, true);
}
EXPORT_SYMBOL(llog_process);

int llog_reverse_process(const struct lu_env *env,
			 struct llog_handle *loghandle, llog_cb_t cb,
			 void *data, void *catdata)
{
	struct llog_log_hdr *llh = loghandle->lgh_hdr;
	struct llog_process_cat_data *cd = catdata;
	void *buf;
	int rc = 0, first_index = 1, index, idx;

	OBD_ALLOC(buf, LLOG_CHUNK_SIZE);
	if (!buf)
		return -ENOMEM;

	if (cd != NULL)
		first_index = cd->lpcd_first_idx + 1;
	if (cd != NULL && cd->lpcd_last_idx)
		index = cd->lpcd_last_idx;
	else
		index = LLOG_BITMAP_BYTES * 8 - 1;

	while (rc == 0) {
		struct llog_rec_hdr *rec;
		struct llog_rec_tail *tail;

		/* skip records not set in bitmap */
		while (index >= first_index &&
		       !ext2_test_bit(index, llh->llh_bitmap))
			--index;

		LASSERT(index >= first_index - 1);
		if (index == first_index - 1)
			break;

		/* get the buf with our target record; avoid old garbage */
		memset(buf, 0, LLOG_CHUNK_SIZE);
		rc = llog_prev_block(env, loghandle, index, buf,
				     LLOG_CHUNK_SIZE);
		if (rc)
			goto out;

		rec = buf;
		idx = rec->lrh_index;
		CDEBUG(D_RPCTRACE, "index %u : idx %u\n", index, idx);
		while (idx < index) {
			rec = (void *)rec + rec->lrh_len;
			if (LLOG_REC_HDR_NEEDS_SWABBING(rec))
				lustre_swab_llog_rec(rec);
			idx ++;
		}
		LASSERT(idx == index);
		tail = (void *)rec + rec->lrh_len - sizeof(*tail);

		/* process records in buffer, starting where we found one */
		while ((void *)tail > buf) {
			if (tail->lrt_index == 0) {
				/* no more records */
				rc = 0;
				goto out;
			}

			/* if set, process the callback on this record */
			if (ext2_test_bit(index, llh->llh_bitmap)) {
				rec = (void *)tail - tail->lrt_len +
				      sizeof(*tail);

				rc = cb(env, loghandle, rec, data);
				if (rc == LLOG_PROC_BREAK) {
					goto out;
				} else if (rc == LLOG_DEL_RECORD) {
					llog_cancel_rec(env, loghandle,
							tail->lrt_index);
					rc = 0;
				}
				if (rc)
					goto out;
			}

			/* previous record, still in buffer? */
			--index;
			if (index < first_index) {
				rc = 0;
				goto out;
			}
			tail = (void *)tail - tail->lrt_len;
		}
	}

out:
	if (buf)
		OBD_FREE(buf, LLOG_CHUNK_SIZE);
	return rc;
}
EXPORT_SYMBOL(llog_reverse_process);

/**
 * new llog API
 *
 * API functions:
 *      llog_open - open llog, may not exist
 *      llog_exist - check if llog exists
 *      llog_close - close opened llog, pair for open, frees llog_handle
 *      llog_declare_create - declare llog creation
 *      llog_create - create new llog on disk, need transaction handle
 *      llog_declare_write_rec - declaration of llog write
 *      llog_write_rec - write llog record on disk, need transaction handle
 *      llog_declare_add - declare llog catalog record addition
 *      llog_add - add llog record in catalog, need transaction handle
 */
int llog_exist(struct llog_handle *loghandle)
{
	struct llog_operations	*lop;
	int			 rc;

	rc = llog_handle2ops(loghandle, &lop);
	if (rc)
		return rc;
	if (lop->lop_exist == NULL)
		return -EOPNOTSUPP;

	rc = lop->lop_exist(loghandle);
	return rc;
}
EXPORT_SYMBOL(llog_exist);

int llog_declare_create(const struct lu_env *env,
			struct llog_handle *loghandle, struct thandle *th)
{
	struct llog_operations	*lop;
	int			 raised, rc;

	rc = llog_handle2ops(loghandle, &lop);
	if (rc)
		return rc;
	if (lop->lop_declare_create == NULL)
		return -EOPNOTSUPP;

	raised = cfs_cap_raised(CFS_CAP_SYS_RESOURCE);
	if (!raised)
		cfs_cap_raise(CFS_CAP_SYS_RESOURCE);
	rc = lop->lop_declare_create(env, loghandle, th);
	if (!raised)
		cfs_cap_lower(CFS_CAP_SYS_RESOURCE);
	return rc;
}
EXPORT_SYMBOL(llog_declare_create);

int llog_create(const struct lu_env *env, struct llog_handle *handle,
		struct thandle *th)
{
	struct llog_operations	*lop;
	int			 raised, rc;

	rc = llog_handle2ops(handle, &lop);
	if (rc)
		return rc;
	if (lop->lop_create == NULL)
		return -EOPNOTSUPP;

	raised = cfs_cap_raised(CFS_CAP_SYS_RESOURCE);
	if (!raised)
		cfs_cap_raise(CFS_CAP_SYS_RESOURCE);
	rc = lop->lop_create(env, handle, th);
	if (!raised)
		cfs_cap_lower(CFS_CAP_SYS_RESOURCE);
	return rc;
}
EXPORT_SYMBOL(llog_create);

int llog_declare_write_rec(const struct lu_env *env,
			   struct llog_handle *handle,
			   struct llog_rec_hdr *rec, int idx,
			   struct thandle *th)
{
	struct llog_operations	*lop;
	int			 raised, rc;

	rc = llog_handle2ops(handle, &lop);
	if (rc)
		return rc;
	LASSERT(lop);
	if (lop->lop_declare_write_rec == NULL)
		return -EOPNOTSUPP;

	raised = cfs_cap_raised(CFS_CAP_SYS_RESOURCE);
	if (!raised)
		cfs_cap_raise(CFS_CAP_SYS_RESOURCE);
	rc = lop->lop_declare_write_rec(env, handle, rec, idx, th);
	if (!raised)
		cfs_cap_lower(CFS_CAP_SYS_RESOURCE);
	return rc;
}
EXPORT_SYMBOL(llog_declare_write_rec);

int llog_write_rec(const struct lu_env *env, struct llog_handle *handle,
		   struct llog_rec_hdr *rec, struct llog_cookie *logcookies,
		   int numcookies, void *buf, int idx, struct thandle *th)
{
	struct llog_operations	*lop;
	int			 raised, rc, buflen;

	rc = llog_handle2ops(handle, &lop);
	if (rc)
		return rc;

	LASSERT(lop);
	if (lop->lop_write_rec == NULL)
		return -EOPNOTSUPP;

	if (buf)
		buflen = rec->lrh_len + sizeof(struct llog_rec_hdr) +
			 sizeof(struct llog_rec_tail);
	else
		buflen = rec->lrh_len;
	LASSERT(cfs_size_round(buflen) == buflen);

	raised = cfs_cap_raised(CFS_CAP_SYS_RESOURCE);
	if (!raised)
		cfs_cap_raise(CFS_CAP_SYS_RESOURCE);
	rc = lop->lop_write_rec(env, handle, rec, logcookies, numcookies,
				buf, idx, th);
	if (!raised)
		cfs_cap_lower(CFS_CAP_SYS_RESOURCE);
	return rc;
}
EXPORT_SYMBOL(llog_write_rec);

int llog_add(const struct lu_env *env, struct llog_handle *lgh,
	     struct llog_rec_hdr *rec, struct llog_cookie *logcookies,
	     void *buf, struct thandle *th)
{
	int raised, rc;

	if (lgh->lgh_logops->lop_add == NULL)
		return -EOPNOTSUPP;

	raised = cfs_cap_raised(CFS_CAP_SYS_RESOURCE);
	if (!raised)
		cfs_cap_raise(CFS_CAP_SYS_RESOURCE);
	rc = lgh->lgh_logops->lop_add(env, lgh, rec, logcookies, buf, th);
	if (!raised)
		cfs_cap_lower(CFS_CAP_SYS_RESOURCE);
	return rc;
}
EXPORT_SYMBOL(llog_add);

int llog_declare_add(const struct lu_env *env, struct llog_handle *lgh,
		     struct llog_rec_hdr *rec, struct thandle *th)
{
	int raised, rc;

	if (lgh->lgh_logops->lop_declare_add == NULL)
		return -EOPNOTSUPP;

	raised = cfs_cap_raised(CFS_CAP_SYS_RESOURCE);
	if (!raised)
		cfs_cap_raise(CFS_CAP_SYS_RESOURCE);
	rc = lgh->lgh_logops->lop_declare_add(env, lgh, rec, th);
	if (!raised)
		cfs_cap_lower(CFS_CAP_SYS_RESOURCE);
	return rc;
}
EXPORT_SYMBOL(llog_declare_add);

/**
 * Helper function to open llog or create it if doesn't exist.
 * It hides all transaction handling from caller.
 */
int llog_open_create(const struct lu_env *env, struct llog_ctxt *ctxt,
		     struct llog_handle **res, struct llog_logid *logid,
		     char *name)
{
	struct dt_device	*d;
	struct thandle		*th;
	int			 rc;

	rc = llog_open(env, ctxt, res, logid, name, LLOG_OPEN_NEW);
	if (rc)
		return rc;

	if (llog_exist(*res))
		return 0;

	LASSERT((*res)->lgh_obj != NULL);

	d = lu2dt_dev((*res)->lgh_obj->do_lu.lo_dev);

	th = dt_trans_create(env, d);
	if (IS_ERR(th)) {
		rc = PTR_ERR(th);
		goto out;
	}

	rc = llog_declare_create(env, *res, th);
	if (rc == 0) {
		rc = dt_trans_start_local(env, d, th);
		if (rc == 0)
			rc = llog_create(env, *res, th);
	}
	dt_trans_stop(env, d, th);
out:
	if (rc)
		llog_close(env, *res);
	return rc;
}
EXPORT_SYMBOL(llog_open_create);

/**
 * Helper function to delete existent llog.
 */
int llog_erase(const struct lu_env *env, struct llog_ctxt *ctxt,
	       struct llog_logid *logid, char *name)
{
	struct llog_handle	*handle;
	int			 rc = 0, rc2;

	/* nothing to erase */
	if (name == NULL && logid == NULL)
		return 0;

	rc = llog_open(env, ctxt, &handle, logid, name, LLOG_OPEN_EXISTS);
	if (rc < 0)
		return rc;

	rc = llog_init_handle(env, handle, LLOG_F_IS_PLAIN, NULL);
	if (rc == 0)
		rc = llog_destroy(env, handle);

	rc2 = llog_close(env, handle);
	if (rc == 0)
		rc = rc2;
	return rc;
}
EXPORT_SYMBOL(llog_erase);

/*
 * Helper function for write record in llog.
 * It hides all transaction handling from caller.
 * Valid only with local llog.
 */
int llog_write(const struct lu_env *env, struct llog_handle *loghandle,
	       struct llog_rec_hdr *rec, struct llog_cookie *reccookie,
	       int cookiecount, void *buf, int idx)
{
	struct dt_device	*dt;
	struct thandle		*th;
	int			 rc;

	LASSERT(loghandle);
	LASSERT(loghandle->lgh_ctxt);
	LASSERT(loghandle->lgh_obj != NULL);

	dt = lu2dt_dev(loghandle->lgh_obj->do_lu.lo_dev);

	th = dt_trans_create(env, dt);
	if (IS_ERR(th))
		return PTR_ERR(th);

	rc = llog_declare_write_rec(env, loghandle, rec, idx, th);
	if (rc)
		goto out_trans;

	rc = dt_trans_start_local(env, dt, th);
	if (rc)
		goto out_trans;

	down_write(&loghandle->lgh_lock);
	rc = llog_write_rec(env, loghandle, rec, reccookie,
			    cookiecount, buf, idx, th);
	up_write(&loghandle->lgh_lock);
out_trans:
	dt_trans_stop(env, dt, th);
	return rc;
}
EXPORT_SYMBOL(llog_write);

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

int llog_is_empty(const struct lu_env *env, struct llog_ctxt *ctxt,
		  char *name)
{
	struct llog_handle	*llh;
	int			 rc = 0;

	rc = llog_open(env, ctxt, &llh, NULL, name, LLOG_OPEN_EXISTS);
	if (rc < 0) {
		if (likely(rc == -ENOENT))
			rc = 0;
		goto out;
	}

	rc = llog_init_handle(env, llh, LLOG_F_IS_PLAIN, NULL);
	if (rc)
		goto out_close;
	rc = llog_get_size(llh);

out_close:
	llog_close(env, llh);
out:
	/* header is record 1 */
	return rc <= 1;
}
EXPORT_SYMBOL(llog_is_empty);

int llog_copy_handler(const struct lu_env *env, struct llog_handle *llh,
		      struct llog_rec_hdr *rec, void *data)
{
	struct llog_handle	*copy_llh = data;

	/* Append all records */
	return llog_write(env, copy_llh, rec, NULL, 0, NULL, -1);
}
EXPORT_SYMBOL(llog_copy_handler);

/* backup plain llog */
int llog_backup(const struct lu_env *env, struct obd_device *obd,
		struct llog_ctxt *ctxt, struct llog_ctxt *bctxt,
		char *name, char *backup)
{
	struct llog_handle	*llh, *bllh;
	int			 rc;



	/* open original log */
	rc = llog_open(env, ctxt, &llh, NULL, name, LLOG_OPEN_EXISTS);
	if (rc < 0) {
		/* the -ENOENT case is also reported to the caller
		 * but silently so it should handle that if needed.
		 */
		if (rc != -ENOENT)
			CERROR("%s: failed to open log %s: rc = %d\n",
			       obd->obd_name, name, rc);
		return rc;
	}

	rc = llog_init_handle(env, llh, LLOG_F_IS_PLAIN, NULL);
	if (rc)
		goto out_close;

	/* Make sure there's no old backup log */
	rc = llog_erase(env, bctxt, NULL, backup);
	if (rc < 0 && rc != -ENOENT)
		goto out_close;

	/* open backup log */
	rc = llog_open_create(env, bctxt, &bllh, NULL, backup);
	if (rc) {
		CERROR("%s: failed to open backup logfile %s: rc = %d\n",
		       obd->obd_name, backup, rc);
		goto out_close;
	}

	/* check that backup llog is not the same object as original one */
	if (llh->lgh_obj == bllh->lgh_obj) {
		CERROR("%s: backup llog %s to itself (%s), objects %p/%p\n",
		       obd->obd_name, name, backup, llh->lgh_obj,
		       bllh->lgh_obj);
		rc = -EEXIST;
		goto out_backup;
	}

	rc = llog_init_handle(env, bllh, LLOG_F_IS_PLAIN, NULL);
	if (rc)
		goto out_backup;

	/* Copy log record by record */
	rc = llog_process_or_fork(env, llh, llog_copy_handler, (void *)bllh,
				  NULL, false);
	if (rc)
		CERROR("%s: failed to backup log %s: rc = %d\n",
		       obd->obd_name, name, rc);
out_backup:
	llog_close(env, bllh);
out_close:
	llog_close(env, llh);
	return rc;
}
EXPORT_SYMBOL(llog_backup);
