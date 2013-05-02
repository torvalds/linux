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


#include <obd_class.h>
#include <lustre_log.h>
#include "llog_internal.h"

/*
 * Allocate a new log or catalog handle
 * Used inside llog_open().
 */
struct llog_handle *llog_alloc_handle(void)
{
	struct llog_handle *loghandle;

	OBD_ALLOC_PTR(loghandle);
	if (loghandle == NULL)
		return ERR_PTR(-ENOMEM);

	init_rwsem(&loghandle->lgh_lock);
	spin_lock_init(&loghandle->lgh_hdr_lock);
	INIT_LIST_HEAD(&loghandle->u.phd.phd_entry);
	atomic_set(&loghandle->lgh_refcount, 1);

	return loghandle;
}

/*
 * Free llog handle and header data if exists. Used in llog_close() only
 */
void llog_free_handle(struct llog_handle *loghandle)
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
	ENTRY;

	CDEBUG(D_RPCTRACE, "Canceling %d in log "DOSTID"\n",
	       index, POSTID(&loghandle->lgh_id.lgl_oi));

	if (index == 0) {
		CERROR("Can't cancel index 0 which is header\n");
		RETURN(-EINVAL);
	}

	spin_lock(&loghandle->lgh_hdr_lock);
	if (!ext2_clear_bit(index, llh->llh_bitmap)) {
		spin_unlock(&loghandle->lgh_hdr_lock);
		CDEBUG(D_RPCTRACE, "Catalog index %u already clear?\n", index);
		RETURN(-ENOENT);
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
			GOTO(out_err, rc);
		}
		RETURN(1);
	}
	spin_unlock(&loghandle->lgh_hdr_lock);

	rc = llog_write(env, loghandle, &llh->llh_hdr, NULL, 0, NULL, 0);
	if (rc < 0) {
		CERROR("%s: fail to write header for llog #"DOSTID
		       "#%08x: rc = %d\n",
		       loghandle->lgh_ctxt->loc_obd->obd_name,
		       POSTID(&loghandle->lgh_id.lgl_oi),
		       loghandle->lgh_id.lgl_ogen, rc);
		GOTO(out_err, rc);
	}
	RETURN(0);
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
		RETURN(rc);

	if (lop->lop_read_header == NULL)
		RETURN(-EOPNOTSUPP);

	rc = lop->lop_read_header(env, handle);
	if (rc == LLOG_EEMPTY) {
		struct llog_log_hdr *llh = handle->lgh_hdr;

		handle->lgh_last_idx = 0; /* header is record with index 0 */
		llh->llh_count = 1;	 /* for the header record */
		llh->llh_hdr.lrh_type = LLOG_HDR_MAGIC;
		llh->llh_hdr.lrh_len = llh->llh_tail.lrt_len = LLOG_CHUNK_SIZE;
		llh->llh_hdr.lrh_index = llh->llh_tail.lrt_index = 0;
		llh->llh_timestamp = cfs_time_current_sec();
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

	ENTRY;
	LASSERT(handle->lgh_hdr == NULL);

	OBD_ALLOC_PTR(llh);
	if (llh == NULL)
		RETURN(-ENOMEM);
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
			GOTO(out, rc = -EINVAL);
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
			GOTO(out, rc = -EINVAL);
		}
		if (unlikely(uuid &&
			     !obd_uuid_equals(uuid, &llh->llh_tgtuuid))) {
			CERROR("%s: llog uuid mismatch: %s/%s\n",
			       handle->lgh_ctxt->loc_obd->obd_name,
			       (char *)uuid->uuid,
			       (char *)llh->llh_tgtuuid.uuid);
			GOTO(out, rc = -EEXIST);
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
	RETURN(rc);
}
EXPORT_SYMBOL(llog_init_handle);

int llog_copy_handler(const struct lu_env *env,
		      struct llog_handle *llh,
		      struct llog_rec_hdr *rec,
		      void *data)
{
	struct llog_rec_hdr local_rec = *rec;
	struct llog_handle *local_llh = (struct llog_handle *)data;
	char *cfg_buf = (char*) (rec + 1);
	struct lustre_cfg *lcfg;
	int rc = 0;
	ENTRY;

	/* Append all records */
	local_rec.lrh_len -= sizeof(*rec) + sizeof(struct llog_rec_tail);
	rc = llog_write(env, local_llh, &local_rec, NULL, 0,
			(void *)cfg_buf, -1);

	lcfg = (struct lustre_cfg *)cfg_buf;
	CDEBUG(D_INFO, "idx=%d, rc=%d, len=%d, cmd %x %s %s\n",
	       rec->lrh_index, rc, rec->lrh_len, lcfg->lcfg_command,
	       lustre_cfg_string(lcfg, 0), lustre_cfg_string(lcfg, 1));

	RETURN(rc);
}
EXPORT_SYMBOL(llog_copy_handler);

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

	ENTRY;

	LASSERT(llh);

	OBD_ALLOC(buf, LLOG_CHUNK_SIZE);
	if (!buf) {
		lpi->lpi_rc = -ENOMEM;
		RETURN(0);
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
			GOTO(out, rc);

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
				if (index <= loghandle->lgh_last_idx)
					GOTO(repeat, rc = 0);
				GOTO(out, rc = 0); /* no more records */
			}
			if (rec->lrh_len == 0 ||
			    rec->lrh_len > LLOG_CHUNK_SIZE) {
				CWARN("invalid length %d in llog record for "
				      "index %d/%d\n", rec->lrh_len,
				      rec->lrh_index, index);
				GOTO(out, rc = -EINVAL);
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
					GOTO(out, rc);
				} else if (rc == LLOG_DEL_RECORD) {
					llog_cancel_rec(lpi->lpi_env,
							loghandle,
							rec->lrh_index);
					rc = 0;
				}
				if (rc)
					GOTO(out, rc);
			} else {
				CDEBUG(D_OTHER, "Skipped index %d\n", index);
			}

			/* next record, still in buffer? */
			++index;
			if (index > last_index)
				GOTO(out, rc = 0);
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

	ENTRY;

	OBD_ALLOC_PTR(lpi);
	if (lpi == NULL) {
		CERROR("cannot alloc pointer\n");
		RETURN(-ENOMEM);
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
			RETURN(rc);
		}
		wait_for_completion(&lpi->lpi_completion);
	} else {
		lpi->lpi_env = env;
		llog_process_thread(lpi);
	}
	rc = lpi->lpi_rc;
	OBD_FREE_PTR(lpi);
	RETURN(rc);
}
EXPORT_SYMBOL(llog_process_or_fork);

int llog_process(const struct lu_env *env, struct llog_handle *loghandle,
		 llog_cb_t cb, void *data, void *catdata)
{
	return llog_process_or_fork(env, loghandle, cb, data, catdata, true);
}
EXPORT_SYMBOL(llog_process);

inline int llog_get_size(struct llog_handle *loghandle)
{
	if (loghandle && loghandle->lgh_hdr)
		return loghandle->lgh_hdr->llh_count;
	return 0;
}
EXPORT_SYMBOL(llog_get_size);

int llog_reverse_process(const struct lu_env *env,
			 struct llog_handle *loghandle, llog_cb_t cb,
			 void *data, void *catdata)
{
	struct llog_log_hdr *llh = loghandle->lgh_hdr;
	struct llog_process_cat_data *cd = catdata;
	void *buf;
	int rc = 0, first_index = 1, index, idx;
	ENTRY;

	OBD_ALLOC(buf, LLOG_CHUNK_SIZE);
	if (!buf)
		RETURN(-ENOMEM);

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
			GOTO(out, rc);

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
			if (tail->lrt_index == 0)
				GOTO(out, rc = 0); /* no more records */

			/* if set, process the callback on this record */
			if (ext2_test_bit(index, llh->llh_bitmap)) {
				rec = (void *)tail - tail->lrt_len +
				      sizeof(*tail);

				rc = cb(env, loghandle, rec, data);
				if (rc == LLOG_PROC_BREAK) {
					GOTO(out, rc);
				} else if (rc == LLOG_DEL_RECORD) {
					llog_cancel_rec(env, loghandle,
							tail->lrt_index);
					rc = 0;
				}
				if (rc)
					GOTO(out, rc);
			}

			/* previous record, still in buffer? */
			--index;
			if (index < first_index)
				GOTO(out, rc = 0);
			tail = (void *)tail - tail->lrt_len;
		}
	}

out:
	if (buf)
		OBD_FREE(buf, LLOG_CHUNK_SIZE);
	RETURN(rc);
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

	ENTRY;

	rc = llog_handle2ops(loghandle, &lop);
	if (rc)
		RETURN(rc);
	if (lop->lop_exist == NULL)
		RETURN(-EOPNOTSUPP);

	rc = lop->lop_exist(loghandle);
	RETURN(rc);
}
EXPORT_SYMBOL(llog_exist);

int llog_declare_create(const struct lu_env *env,
			struct llog_handle *loghandle, struct thandle *th)
{
	struct llog_operations	*lop;
	int			 raised, rc;

	ENTRY;

	rc = llog_handle2ops(loghandle, &lop);
	if (rc)
		RETURN(rc);
	if (lop->lop_declare_create == NULL)
		RETURN(-EOPNOTSUPP);

	raised = cfs_cap_raised(CFS_CAP_SYS_RESOURCE);
	if (!raised)
		cfs_cap_raise(CFS_CAP_SYS_RESOURCE);
	rc = lop->lop_declare_create(env, loghandle, th);
	if (!raised)
		cfs_cap_lower(CFS_CAP_SYS_RESOURCE);
	RETURN(rc);
}
EXPORT_SYMBOL(llog_declare_create);

int llog_create(const struct lu_env *env, struct llog_handle *handle,
		struct thandle *th)
{
	struct llog_operations	*lop;
	int			 raised, rc;

	ENTRY;

	rc = llog_handle2ops(handle, &lop);
	if (rc)
		RETURN(rc);
	if (lop->lop_create == NULL)
		RETURN(-EOPNOTSUPP);

	raised = cfs_cap_raised(CFS_CAP_SYS_RESOURCE);
	if (!raised)
		cfs_cap_raise(CFS_CAP_SYS_RESOURCE);
	rc = lop->lop_create(env, handle, th);
	if (!raised)
		cfs_cap_lower(CFS_CAP_SYS_RESOURCE);
	RETURN(rc);
}
EXPORT_SYMBOL(llog_create);

int llog_declare_write_rec(const struct lu_env *env,
			   struct llog_handle *handle,
			   struct llog_rec_hdr *rec, int idx,
			   struct thandle *th)
{
	struct llog_operations	*lop;
	int			 raised, rc;

	ENTRY;

	rc = llog_handle2ops(handle, &lop);
	if (rc)
		RETURN(rc);
	LASSERT(lop);
	if (lop->lop_declare_write_rec == NULL)
		RETURN(-EOPNOTSUPP);

	raised = cfs_cap_raised(CFS_CAP_SYS_RESOURCE);
	if (!raised)
		cfs_cap_raise(CFS_CAP_SYS_RESOURCE);
	rc = lop->lop_declare_write_rec(env, handle, rec, idx, th);
	if (!raised)
		cfs_cap_lower(CFS_CAP_SYS_RESOURCE);
	RETURN(rc);
}
EXPORT_SYMBOL(llog_declare_write_rec);

int llog_write_rec(const struct lu_env *env, struct llog_handle *handle,
		   struct llog_rec_hdr *rec, struct llog_cookie *logcookies,
		   int numcookies, void *buf, int idx, struct thandle *th)
{
	struct llog_operations	*lop;
	int			 raised, rc, buflen;

	ENTRY;

	rc = llog_handle2ops(handle, &lop);
	if (rc)
		RETURN(rc);

	LASSERT(lop);
	if (lop->lop_write_rec == NULL)
		RETURN(-EOPNOTSUPP);

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
	RETURN(rc);
}
EXPORT_SYMBOL(llog_write_rec);

int llog_add(const struct lu_env *env, struct llog_handle *lgh,
	     struct llog_rec_hdr *rec, struct llog_cookie *logcookies,
	     void *buf, struct thandle *th)
{
	int raised, rc;

	ENTRY;

	if (lgh->lgh_logops->lop_add == NULL)
		RETURN(-EOPNOTSUPP);

	raised = cfs_cap_raised(CFS_CAP_SYS_RESOURCE);
	if (!raised)
		cfs_cap_raise(CFS_CAP_SYS_RESOURCE);
	rc = lgh->lgh_logops->lop_add(env, lgh, rec, logcookies, buf, th);
	if (!raised)
		cfs_cap_lower(CFS_CAP_SYS_RESOURCE);
	RETURN(rc);
}
EXPORT_SYMBOL(llog_add);

int llog_declare_add(const struct lu_env *env, struct llog_handle *lgh,
		     struct llog_rec_hdr *rec, struct thandle *th)
{
	int raised, rc;

	ENTRY;

	if (lgh->lgh_logops->lop_declare_add == NULL)
		RETURN(-EOPNOTSUPP);

	raised = cfs_cap_raised(CFS_CAP_SYS_RESOURCE);
	if (!raised)
		cfs_cap_raise(CFS_CAP_SYS_RESOURCE);
	rc = lgh->lgh_logops->lop_declare_add(env, lgh, rec, th);
	if (!raised)
		cfs_cap_lower(CFS_CAP_SYS_RESOURCE);
	RETURN(rc);
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
	struct thandle	*th;
	int		 rc;

	ENTRY;

	rc = llog_open(env, ctxt, res, logid, name, LLOG_OPEN_NEW);
	if (rc)
		RETURN(rc);

	if (llog_exist(*res))
		RETURN(0);

	if ((*res)->lgh_obj != NULL) {
		struct dt_device *d;

		d = lu2dt_dev((*res)->lgh_obj->do_lu.lo_dev);

		th = dt_trans_create(env, d);
		if (IS_ERR(th))
			GOTO(out, rc = PTR_ERR(th));

		rc = llog_declare_create(env, *res, th);
		if (rc == 0) {
			rc = dt_trans_start_local(env, d, th);
			if (rc == 0)
				rc = llog_create(env, *res, th);
		}
		dt_trans_stop(env, d, th);
	} else {
		/* lvfs compat code */
		LASSERT((*res)->lgh_file == NULL);
		rc = llog_create(env, *res, NULL);
	}
out:
	if (rc)
		llog_close(env, *res);
	RETURN(rc);
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

	ENTRY;

	/* nothing to erase */
	if (name == NULL && logid == NULL)
		RETURN(0);

	rc = llog_open(env, ctxt, &handle, logid, name, LLOG_OPEN_EXISTS);
	if (rc < 0)
		RETURN(rc);

	rc = llog_init_handle(env, handle, LLOG_F_IS_PLAIN, NULL);
	if (rc == 0)
		rc = llog_destroy(env, handle);

	rc2 = llog_close(env, handle);
	if (rc == 0)
		rc = rc2;
	RETURN(rc);
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
	int rc;

	ENTRY;

	LASSERT(loghandle);
	LASSERT(loghandle->lgh_ctxt);

	if (loghandle->lgh_obj != NULL) {
		struct dt_device	*dt;
		struct thandle		*th;

		dt = lu2dt_dev(loghandle->lgh_obj->do_lu.lo_dev);

		th = dt_trans_create(env, dt);
		if (IS_ERR(th))
			RETURN(PTR_ERR(th));

		rc = llog_declare_write_rec(env, loghandle, rec, idx, th);
		if (rc)
			GOTO(out_trans, rc);

		rc = dt_trans_start_local(env, dt, th);
		if (rc)
			GOTO(out_trans, rc);

		down_write(&loghandle->lgh_lock);
		rc = llog_write_rec(env, loghandle, rec, reccookie,
				    cookiecount, buf, idx, th);
		up_write(&loghandle->lgh_lock);
out_trans:
		dt_trans_stop(env, dt, th);
	} else { /* lvfs compatibility */
		down_write(&loghandle->lgh_lock);
		rc = llog_write_rec(env, loghandle, rec, reccookie,
				    cookiecount, buf, idx, NULL);
		up_write(&loghandle->lgh_lock);
	}
	RETURN(rc);
}
EXPORT_SYMBOL(llog_write);

int llog_open(const struct lu_env *env, struct llog_ctxt *ctxt,
	      struct llog_handle **lgh, struct llog_logid *logid,
	      char *name, enum llog_open_param open_param)
{
	int	 raised;
	int	 rc;

	ENTRY;

	LASSERT(ctxt);
	LASSERT(ctxt->loc_logops);

	if (ctxt->loc_logops->lop_open == NULL) {
		*lgh = NULL;
		RETURN(-EOPNOTSUPP);
	}

	*lgh = llog_alloc_handle();
	if (*lgh == NULL)
		RETURN(-ENOMEM);
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
	RETURN(rc);
}
EXPORT_SYMBOL(llog_open);

int llog_close(const struct lu_env *env, struct llog_handle *loghandle)
{
	struct llog_operations	*lop;
	int			 rc;

	ENTRY;

	rc = llog_handle2ops(loghandle, &lop);
	if (rc)
		GOTO(out, rc);
	if (lop->lop_close == NULL)
		GOTO(out, rc = -EOPNOTSUPP);
	rc = lop->lop_close(env, loghandle);
out:
	llog_handle_put(loghandle);
	RETURN(rc);
}
EXPORT_SYMBOL(llog_close);
