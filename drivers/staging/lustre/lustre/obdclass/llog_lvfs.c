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
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/obdclass/llog_lvfs.c
 *
 * OST<->MDS recovery logging infrastructure.
 * Invariants in implementation:
 * - we do not share logs among different OST<->MDS connections, so that
 *   if an OST or MDS fails it need only look at log(s) relevant to itself
 *
 * Author: Andreas Dilger <adilger@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_LOG


#include "../include/obd.h"
#include "../include/obd_class.h"
#include "../include/lustre_log.h"
#include "../include/obd_ost.h"
#include <linux/list.h>
#include "../include/lvfs.h"
#include "../include/lustre_fsfilt.h"
#include "../include/lustre_disk.h"
#include "llog_internal.h"

#if  defined(LLOG_LVFS)

static int llog_lvfs_pad(struct obd_device *obd, struct l_file *file,
				int len, int index)
{
	struct llog_rec_hdr rec = { 0 };
	struct llog_rec_tail tail;
	int rc;

	LASSERT(len >= LLOG_MIN_REC_SIZE && (len & 0x7) == 0);

	tail.lrt_len = rec.lrh_len = len;
	tail.lrt_index = rec.lrh_index = index;
	rec.lrh_type = LLOG_PAD_MAGIC;

	rc = fsfilt_write_record(obd, file, &rec, sizeof(rec), &file->f_pos, 0);
	if (rc) {
		CERROR("error writing padding record: rc %d\n", rc);
		goto out;
	}

	file->f_pos += len - sizeof(rec) - sizeof(tail);
	rc = fsfilt_write_record(obd, file, &tail, sizeof(tail),&file->f_pos,0);
	if (rc) {
		CERROR("error writing padding record: rc %d\n", rc);
		goto out;
	}

 out:
	return rc;
}

static int llog_lvfs_write_blob(struct obd_device *obd, struct l_file *file,
				struct llog_rec_hdr *rec, void *buf, loff_t off)
{
	int rc;
	struct llog_rec_tail end;
	loff_t saved_off = file->f_pos;
	int buflen = rec->lrh_len;

	file->f_pos = off;

	if (buflen == 0)
		CWARN("0-length record\n");

	if (!buf) {
		rc = fsfilt_write_record(obd, file, rec, buflen,&file->f_pos,0);
		if (rc) {
			CERROR("error writing log record: rc %d\n", rc);
			goto out;
		}
		GOTO(out, rc = 0);
	}

	/* the buf case */
	rec->lrh_len = sizeof(*rec) + buflen + sizeof(end);
	rc = fsfilt_write_record(obd, file, rec, sizeof(*rec), &file->f_pos, 0);
	if (rc) {
		CERROR("error writing log hdr: rc %d\n", rc);
		goto out;
	}

	rc = fsfilt_write_record(obd, file, buf, buflen, &file->f_pos, 0);
	if (rc) {
		CERROR("error writing log buffer: rc %d\n", rc);
		goto out;
	}

	end.lrt_len = rec->lrh_len;
	end.lrt_index = rec->lrh_index;
	rc = fsfilt_write_record(obd, file, &end, sizeof(end), &file->f_pos, 0);
	if (rc) {
		CERROR("error writing log tail: rc %d\n", rc);
		goto out;
	}

	rc = 0;
 out:
	if (saved_off > file->f_pos)
		file->f_pos = saved_off;
	LASSERT(rc <= 0);
	return rc;
}

static int llog_lvfs_read_blob(struct obd_device *obd, struct l_file *file,
				void *buf, int size, loff_t off)
{
	loff_t offset = off;
	int rc;

	rc = fsfilt_read_record(obd, file, buf, size, &offset);
	if (rc) {
		CERROR("error reading log record: rc %d\n", rc);
		return rc;
	}
	return 0;
}

static int llog_lvfs_read_header(const struct lu_env *env,
				 struct llog_handle *handle)
{
	struct obd_device *obd;
	int rc;

	LASSERT(sizeof(*handle->lgh_hdr) == LLOG_CHUNK_SIZE);

	obd = handle->lgh_ctxt->loc_exp->exp_obd;

	if (i_size_read(handle->lgh_file->f_dentry->d_inode) == 0) {
		CDEBUG(D_HA, "not reading header from 0-byte log\n");
		return LLOG_EEMPTY;
	}

	rc = llog_lvfs_read_blob(obd, handle->lgh_file, handle->lgh_hdr,
				 LLOG_CHUNK_SIZE, 0);
	if (rc) {
		CERROR("error reading log header from %.*s\n",
		       handle->lgh_file->f_dentry->d_name.len,
		       handle->lgh_file->f_dentry->d_name.name);
	} else {
		struct llog_rec_hdr *llh_hdr = &handle->lgh_hdr->llh_hdr;

		if (LLOG_REC_HDR_NEEDS_SWABBING(llh_hdr))
			lustre_swab_llog_hdr(handle->lgh_hdr);

		if (llh_hdr->lrh_type != LLOG_HDR_MAGIC) {
			CERROR("bad log %.*s header magic: %#x (expected %#x)\n",
			       handle->lgh_file->f_dentry->d_name.len,
			       handle->lgh_file->f_dentry->d_name.name,
			       llh_hdr->lrh_type, LLOG_HDR_MAGIC);
			rc = -EIO;
		} else if (llh_hdr->lrh_len != LLOG_CHUNK_SIZE) {
			CERROR("incorrectly sized log %.*s header: %#x "
			       "(expected %#x)\n",
			       handle->lgh_file->f_dentry->d_name.len,
			       handle->lgh_file->f_dentry->d_name.name,
			       llh_hdr->lrh_len, LLOG_CHUNK_SIZE);
			CERROR("you may need to re-run lconf --write_conf.\n");
			rc = -EIO;
		}
	}

	handle->lgh_last_idx = handle->lgh_hdr->llh_tail.lrt_index;
	handle->lgh_file->f_pos = i_size_read(handle->lgh_file->f_dentry->d_inode);

	return rc;
}

/* returns negative in on error; 0 if success && reccookie == 0; 1 otherwise */
/* appends if idx == -1, otherwise overwrites record idx. */
static int llog_lvfs_write_rec(const struct lu_env *env,
			       struct llog_handle *loghandle,
			       struct llog_rec_hdr *rec,
			       struct llog_cookie *reccookie, int cookiecount,
			       void *buf, int idx, struct thandle *th)
{
	struct llog_log_hdr *llh;
	int reclen = rec->lrh_len, index, rc;
	struct llog_rec_tail *lrt;
	struct obd_device *obd;
	struct file *file;
	size_t left;

	llh = loghandle->lgh_hdr;
	file = loghandle->lgh_file;
	obd = loghandle->lgh_ctxt->loc_exp->exp_obd;

	/* record length should not bigger than LLOG_CHUNK_SIZE */
	if (buf)
		rc = (reclen > LLOG_CHUNK_SIZE - sizeof(struct llog_rec_hdr) -
		      sizeof(struct llog_rec_tail)) ? -E2BIG : 0;
	else
		rc = (reclen > LLOG_CHUNK_SIZE) ? -E2BIG : 0;
	if (rc)
		return rc;

	if (buf)
		/* write_blob adds header and tail to lrh_len. */
		reclen = sizeof(*rec) + rec->lrh_len +
			 sizeof(struct llog_rec_tail);

	if (idx != -1) {
		loff_t saved_offset;

		/* no header: only allowed to insert record 1 */
		if (idx != 1 && !i_size_read(file->f_dentry->d_inode)) {
			CERROR("idx != -1 in empty log\n");
			LBUG();
		}

		if (idx && llh->llh_size && llh->llh_size != rec->lrh_len)
			return -EINVAL;

		if (!ext2_test_bit(idx, llh->llh_bitmap))
			CERROR("Modify unset record %u\n", idx);
		if (idx != rec->lrh_index)
			CERROR("Index mismatch %d %u\n", idx, rec->lrh_index);

		rc = llog_lvfs_write_blob(obd, file, &llh->llh_hdr, NULL, 0);
		/* we are done if we only write the header or on error */
		if (rc || idx == 0)
			return rc;

		if (buf) {
			/* We assume that caller has set lgh_cur_* */
			saved_offset = loghandle->lgh_cur_offset;
			CDEBUG(D_OTHER,
			       "modify record "DOSTID": idx:%d/%u/%d, len:%u "
			       "offset %llu\n",
			       POSTID(&loghandle->lgh_id.lgl_oi), idx, rec->lrh_index,
			       loghandle->lgh_cur_idx, rec->lrh_len,
			       (long long)(saved_offset - sizeof(*llh)));
			if (rec->lrh_index != loghandle->lgh_cur_idx) {
				CERROR("modify idx mismatch %u/%d\n",
				       idx, loghandle->lgh_cur_idx);
				return -EFAULT;
			}
		} else {
			/* Assumes constant lrh_len */
			saved_offset = sizeof(*llh) + (idx - 1) * reclen;
		}

		rc = llog_lvfs_write_blob(obd, file, rec, buf, saved_offset);
		if (rc == 0 && reccookie) {
			reccookie->lgc_lgl = loghandle->lgh_id;
			reccookie->lgc_index = idx;
			rc = 1;
		}
		return rc;
	}

	/* Make sure that records don't cross a chunk boundary, so we can
	 * process them page-at-a-time if needed.  If it will cross a chunk
	 * boundary, write in a fake (but referenced) entry to pad the chunk.
	 *
	 * We know that llog_current_log() will return a loghandle that is
	 * big enough to hold reclen, so all we care about is padding here.
	 */
	left = LLOG_CHUNK_SIZE - (file->f_pos & (LLOG_CHUNK_SIZE - 1));

	/* NOTE: padding is a record, but no bit is set */
	if (left != 0 && left != reclen &&
	    left < (reclen + LLOG_MIN_REC_SIZE)) {
		 index = loghandle->lgh_last_idx + 1;
		 rc = llog_lvfs_pad(obd, file, left, index);
		 if (rc)
			 return rc;
		 loghandle->lgh_last_idx++; /*for pad rec*/
	 }
	 /* if it's the last idx in log file, then return -ENOSPC */
	 if (loghandle->lgh_last_idx >= LLOG_BITMAP_SIZE(llh) - 1)
		 return -ENOSPC;
	loghandle->lgh_last_idx++;
	index = loghandle->lgh_last_idx;
	LASSERT(index < LLOG_BITMAP_SIZE(llh));
	rec->lrh_index = index;
	if (buf == NULL) {
		lrt = (struct llog_rec_tail *)
			((char *)rec + rec->lrh_len - sizeof(*lrt));
		lrt->lrt_len = rec->lrh_len;
		lrt->lrt_index = rec->lrh_index;
	}
	/*The caller should make sure only 1 process access the lgh_last_idx,
	 *Otherwise it might hit the assert.*/
	LASSERT(index < LLOG_BITMAP_SIZE(llh));
	spin_lock(&loghandle->lgh_hdr_lock);
	if (ext2_set_bit(index, llh->llh_bitmap)) {
		CERROR("argh, index %u already set in log bitmap?\n", index);
		spin_unlock(&loghandle->lgh_hdr_lock);
		LBUG(); /* should never happen */
	}
	llh->llh_count++;
	spin_unlock(&loghandle->lgh_hdr_lock);
	llh->llh_tail.lrt_index = index;

	rc = llog_lvfs_write_blob(obd, file, &llh->llh_hdr, NULL, 0);
	if (rc)
		return rc;

	rc = llog_lvfs_write_blob(obd, file, rec, buf, file->f_pos);
	if (rc)
		return rc;

	CDEBUG(D_RPCTRACE, "added record "DOSTID": idx: %u, %u \n",
	       POSTID(&loghandle->lgh_id.lgl_oi), index, rec->lrh_len);
	if (rc == 0 && reccookie) {
		reccookie->lgc_lgl = loghandle->lgh_id;
		reccookie->lgc_index = index;
		if ((rec->lrh_type == MDS_UNLINK_REC) ||
		    (rec->lrh_type == MDS_SETATTR64_REC))
			reccookie->lgc_subsys = LLOG_MDS_OST_ORIG_CTXT;
		else if (rec->lrh_type == OST_SZ_REC)
			reccookie->lgc_subsys = LLOG_SIZE_ORIG_CTXT;
		else
			reccookie->lgc_subsys = -1;
		rc = 1;
	}
	if (rc == 0 && rec->lrh_type == LLOG_GEN_REC)
		rc = 1;

	return rc;
}

/* We can skip reading at least as many log blocks as the number of
* minimum sized log records we are skipping.  If it turns out
* that we are not far enough along the log (because the
* actual records are larger than minimum size) we just skip
* some more records. */

static void llog_skip_over(__u64 *off, int curr, int goal)
{
	if (goal <= curr)
		return;
	*off = (*off + (goal-curr-1) * LLOG_MIN_REC_SIZE) &
		~(LLOG_CHUNK_SIZE - 1);
}


/* sets:
 *  - cur_offset to the furthest point read in the log file
 *  - cur_idx to the log index preceding cur_offset
 * returns -EIO/-EINVAL on error
 */
static int llog_lvfs_next_block(const struct lu_env *env,
				struct llog_handle *loghandle, int *cur_idx,
				int next_idx, __u64 *cur_offset, void *buf,
				int len)
{
	int rc;

	if (len == 0 || len & (LLOG_CHUNK_SIZE - 1))
		return -EINVAL;

	CDEBUG(D_OTHER, "looking for log index %u (cur idx %u off "LPU64")\n",
	       next_idx, *cur_idx, *cur_offset);

	while (*cur_offset < i_size_read(loghandle->lgh_file->f_dentry->d_inode)) {
		struct llog_rec_hdr *rec, *last_rec;
		struct llog_rec_tail *tail;
		loff_t ppos;
		int llen;

		llog_skip_over(cur_offset, *cur_idx, next_idx);

		/* read up to next LLOG_CHUNK_SIZE block */
		ppos = *cur_offset;
		llen = LLOG_CHUNK_SIZE - (*cur_offset & (LLOG_CHUNK_SIZE - 1));
		rc = fsfilt_read_record(loghandle->lgh_ctxt->loc_exp->exp_obd,
					loghandle->lgh_file, buf, llen,
					cur_offset);
		if (rc < 0) {
			CERROR("Cant read llog block at log id "DOSTID
			       "/%u offset "LPU64"\n",
			       POSTID(&loghandle->lgh_id.lgl_oi),
			       loghandle->lgh_id.lgl_ogen,
			       *cur_offset);
			return rc;
		}

		/* put number of bytes read into rc to make code simpler */
		rc = *cur_offset - ppos;
		if (rc < len) {
			/* signal the end of the valid buffer to llog_process */
			memset(buf + rc, 0, len - rc);
		}

		if (rc == 0) /* end of file, nothing to do */
			return 0;

		if (rc < sizeof(*tail)) {
			CERROR("Invalid llog block at log id "DOSTID"/%u offset"
			       LPU64"\n", POSTID(&loghandle->lgh_id.lgl_oi),
			       loghandle->lgh_id.lgl_ogen, *cur_offset);
			return -EINVAL;
		}

		rec = buf;
		if (LLOG_REC_HDR_NEEDS_SWABBING(rec))
			lustre_swab_llog_rec(rec);

		tail = (struct llog_rec_tail *)(buf + rc -
						sizeof(struct llog_rec_tail));

		/* get the last record in block */
		last_rec = (struct llog_rec_hdr *)(buf + rc -
						   le32_to_cpu(tail->lrt_len));

		if (LLOG_REC_HDR_NEEDS_SWABBING(last_rec))
			lustre_swab_llog_rec(last_rec);
		LASSERT(last_rec->lrh_index == tail->lrt_index);

		*cur_idx = tail->lrt_index;

		/* this shouldn't happen */
		if (tail->lrt_index == 0) {
			CERROR("Invalid llog tail at log id "DOSTID"/%u offset "
			       LPU64"\n", POSTID(&loghandle->lgh_id.lgl_oi),
			       loghandle->lgh_id.lgl_ogen, *cur_offset);
			return -EINVAL;
		}
		if (tail->lrt_index < next_idx)
			continue;

		/* sanity check that the start of the new buffer is no farther
		 * than the record that we wanted.  This shouldn't happen. */
		if (rec->lrh_index > next_idx) {
			CERROR("missed desired record? %u > %u\n",
			       rec->lrh_index, next_idx);
			return -ENOENT;
		}
		return 0;
	}
	return -EIO;
}

static int llog_lvfs_prev_block(const struct lu_env *env,
				struct llog_handle *loghandle,
				int prev_idx, void *buf, int len)
{
	__u64 cur_offset;
	int rc;

	if (len == 0 || len & (LLOG_CHUNK_SIZE - 1))
		return -EINVAL;

	CDEBUG(D_OTHER, "looking for log index %u\n", prev_idx);

	cur_offset = LLOG_CHUNK_SIZE;
	llog_skip_over(&cur_offset, 0, prev_idx);

	while (cur_offset < i_size_read(loghandle->lgh_file->f_dentry->d_inode)) {
		struct llog_rec_hdr *rec, *last_rec;
		struct llog_rec_tail *tail;
		loff_t ppos = cur_offset;

		rc = fsfilt_read_record(loghandle->lgh_ctxt->loc_exp->exp_obd,
					loghandle->lgh_file, buf, len,
					&cur_offset);
		if (rc < 0) {
			CERROR("Cant read llog block at log id "DOSTID
			       "/%u offset "LPU64"\n",
			       POSTID(&loghandle->lgh_id.lgl_oi),
			       loghandle->lgh_id.lgl_ogen,
			       cur_offset);
			return rc;
		}

		/* put number of bytes read into rc to make code simpler */
		rc = cur_offset - ppos;

		if (rc == 0) /* end of file, nothing to do */
			return 0;

		if (rc < sizeof(*tail)) {
			CERROR("Invalid llog block at log id "DOSTID"/%u offset"
			       LPU64"\n", POSTID(&loghandle->lgh_id.lgl_oi),
			       loghandle->lgh_id.lgl_ogen, cur_offset);
			return -EINVAL;
		}

		rec = buf;
		if (LLOG_REC_HDR_NEEDS_SWABBING(rec))
			lustre_swab_llog_rec(rec);

		tail = (struct llog_rec_tail *)(buf + rc -
						sizeof(struct llog_rec_tail));

		/* get the last record in block */
		last_rec = (struct llog_rec_hdr *)(buf + rc -
						   le32_to_cpu(tail->lrt_len));

		if (LLOG_REC_HDR_NEEDS_SWABBING(last_rec))
			lustre_swab_llog_rec(last_rec);
		LASSERT(last_rec->lrh_index == tail->lrt_index);

		/* this shouldn't happen */
		if (tail->lrt_index == 0) {
			CERROR("Invalid llog tail at log id "DOSTID"/%u offset"
			       LPU64"\n", POSTID(&loghandle->lgh_id.lgl_oi),
			       loghandle->lgh_id.lgl_ogen, cur_offset);
			return -EINVAL;
		}
		if (tail->lrt_index < prev_idx)
			continue;

		/* sanity check that the start of the new buffer is no farther
		 * than the record that we wanted.  This shouldn't happen. */
		if (rec->lrh_index > prev_idx) {
			CERROR("missed desired record? %u > %u\n",
			       rec->lrh_index, prev_idx);
			return -ENOENT;
		}
		return 0;
	}
	return -EIO;
}

static struct file *llog_filp_open(char *dir, char *name, int flags, int mode)
{
	char *logname;
	struct file *filp;
	int len;

	OBD_ALLOC(logname, PATH_MAX);
	if (logname == NULL)
		return ERR_PTR(-ENOMEM);

	len = snprintf(logname, PATH_MAX, "%s/%s", dir, name);
	if (len >= PATH_MAX - 1) {
		filp = ERR_PTR(-ENAMETOOLONG);
	} else {
		filp = l_filp_open(logname, flags, mode);
		if (IS_ERR(filp) && PTR_ERR(filp) != -ENOENT)
			CERROR("logfile creation %s: %ld\n", logname,
			       PTR_ERR(filp));
	}
	OBD_FREE(logname, PATH_MAX);
	return filp;
}

static int llog_lvfs_open(const struct lu_env *env,  struct llog_handle *handle,
			  struct llog_logid *logid, char *name,
			  enum llog_open_param open_param)
{
	struct llog_ctxt	*ctxt = handle->lgh_ctxt;
	struct l_dentry		*dchild = NULL;
	struct obd_device	*obd;
	int			 rc = 0;

	LASSERT(ctxt);
	LASSERT(ctxt->loc_exp);
	LASSERT(ctxt->loc_exp->exp_obd);
	obd = ctxt->loc_exp->exp_obd;

	LASSERT(handle);
	if (logid != NULL) {
		dchild = obd_lvfs_fid2dentry(ctxt->loc_exp, &logid->lgl_oi,
					     logid->lgl_ogen);
		if (IS_ERR(dchild)) {
			rc = PTR_ERR(dchild);
			CERROR("%s: error looking up logfile #"DOSTID "#%08x:"
			       " rc = %d\n", ctxt->loc_obd->obd_name,
			       POSTID(&logid->lgl_oi), logid->lgl_ogen, rc);
			GOTO(out, rc);
		}
		if (dchild->d_inode == NULL) {
			l_dput(dchild);
			rc = -ENOENT;
			CERROR("%s: nonexistent llog #"DOSTID"#%08x:"
			       "rc = %d\n", ctxt->loc_obd->obd_name,
			       POSTID(&logid->lgl_oi), logid->lgl_ogen, rc);
			GOTO(out, rc);
		}
		handle->lgh_file = l_dentry_open(&obd->obd_lvfs_ctxt, dchild,
						 O_RDWR | O_LARGEFILE);
		l_dput(dchild);
		if (IS_ERR(handle->lgh_file)) {
			rc = PTR_ERR(handle->lgh_file);
			handle->lgh_file = NULL;
			CERROR("%s: error opening llog #"DOSTID"#%08x:"
			       "rc = %d\n", ctxt->loc_obd->obd_name,
			       POSTID(&logid->lgl_oi), logid->lgl_ogen, rc);
			GOTO(out, rc);
		}
		handle->lgh_id = *logid;
	} else if (name) {
		handle->lgh_file = llog_filp_open(MOUNT_CONFIGS_DIR, name,
						  O_RDWR | O_LARGEFILE, 0644);
		if (IS_ERR(handle->lgh_file)) {
			rc = PTR_ERR(handle->lgh_file);
			handle->lgh_file = NULL;
			if (rc == -ENOENT && open_param == LLOG_OPEN_NEW) {
				OBD_ALLOC(handle->lgh_name, strlen(name) + 1);
				if (handle->lgh_name)
					strcpy(handle->lgh_name, name);
				else
					GOTO(out, rc = -ENOMEM);
				rc = 0;
			} else {
				GOTO(out, rc);
			}
		} else {
			lustre_build_llog_lvfs_oid(&handle->lgh_id,
			    handle->lgh_file->f_dentry->d_inode->i_ino,
			    handle->lgh_file->f_dentry->d_inode->i_generation);
		}
	} else {
		LASSERTF(open_param == LLOG_OPEN_NEW, "%#x\n", open_param);
		handle->lgh_file = NULL;
	}

	/* No new llog is expected but doesn't exist */
	if (open_param != LLOG_OPEN_NEW && handle->lgh_file == NULL)
		GOTO(out_name, rc = -ENOENT);

	return 0;
out_name:
	if (handle->lgh_name != NULL)
		OBD_FREE(handle->lgh_name, strlen(name) + 1);
out:
	return rc;
}

static int llog_lvfs_exist(struct llog_handle *handle)
{
	return (handle->lgh_file != NULL);
}

/* This is a callback from the llog_* functions.
 * Assumes caller has already pushed us into the kernel context. */
static int llog_lvfs_create(const struct lu_env *env,
			    struct llog_handle *handle,
			    struct thandle *th)
{
	struct llog_ctxt	*ctxt = handle->lgh_ctxt;
	struct obd_device	*obd;
	struct l_dentry		*dchild = NULL;
	struct file		*file;
	struct obdo		*oa = NULL;
	int			 rc = 0;
	int			 open_flags = O_RDWR | O_CREAT | O_LARGEFILE;

	LASSERT(ctxt);
	LASSERT(ctxt->loc_exp);
	obd = ctxt->loc_exp->exp_obd;
	LASSERT(handle->lgh_file == NULL);

	if (handle->lgh_name) {
		file = llog_filp_open(MOUNT_CONFIGS_DIR, handle->lgh_name,
				      open_flags, 0644);
		if (IS_ERR(file))
			return PTR_ERR(file);

		lustre_build_llog_lvfs_oid(&handle->lgh_id,
				file->f_dentry->d_inode->i_ino,
				file->f_dentry->d_inode->i_generation);
		handle->lgh_file = file;
	} else {
		OBDO_ALLOC(oa);
		if (oa == NULL)
			return -ENOMEM;

		ostid_set_seq_llog(&oa->o_oi);
		oa->o_valid = OBD_MD_FLGENER | OBD_MD_FLGROUP;

		rc = obd_create(NULL, ctxt->loc_exp, oa, NULL, NULL);
		if (rc)
			GOTO(out, rc);

		/* FIXME: rationalize the misuse of o_generation in
		 *	this API along with mds_obd_{create,destroy}.
		 *	Hopefully it is only an internal API issue. */
#define o_generation o_parent_oid
		dchild = obd_lvfs_fid2dentry(ctxt->loc_exp, &oa->o_oi,
					     oa->o_generation);
		if (IS_ERR(dchild))
			GOTO(out, rc = PTR_ERR(dchild));

		file = l_dentry_open(&obd->obd_lvfs_ctxt, dchild, open_flags);
		l_dput(dchild);
		if (IS_ERR(file))
			GOTO(out, rc = PTR_ERR(file));
		handle->lgh_id.lgl_oi = oa->o_oi;
		handle->lgh_id.lgl_ogen = oa->o_generation;
		handle->lgh_file = file;
out:
		OBDO_FREE(oa);
	}
	return rc;
}

static int llog_lvfs_close(const struct lu_env *env,
			   struct llog_handle *handle)
{
	int rc;

	if (handle->lgh_file == NULL)
		return 0;
	rc = filp_close(handle->lgh_file, 0);
	if (rc)
		CERROR("%s: error closing llog #"DOSTID"#%08x: "
		       "rc = %d\n", handle->lgh_ctxt->loc_obd->obd_name,
		       POSTID(&handle->lgh_id.lgl_oi),
		       handle->lgh_id.lgl_ogen, rc);
	handle->lgh_file = NULL;
	if (handle->lgh_name) {
		OBD_FREE(handle->lgh_name, strlen(handle->lgh_name) + 1);
		handle->lgh_name = NULL;
	}
	return rc;
}

static int llog_lvfs_destroy(const struct lu_env *env,
			     struct llog_handle *handle)
{
	struct dentry *fdentry;
	struct obdo *oa;
	struct obd_device *obd = handle->lgh_ctxt->loc_exp->exp_obd;
	char *dir;
	void *th;
	struct inode *inode;
	int rc, rc1;

	dir = MOUNT_CONFIGS_DIR;

	LASSERT(handle->lgh_file);
	fdentry = handle->lgh_file->f_dentry;
	inode = fdentry->d_parent->d_inode;
	if (strcmp(fdentry->d_parent->d_name.name, dir) == 0) {
		struct lvfs_run_ctxt saved;
		struct vfsmount *mnt = mntget(handle->lgh_file->f_vfsmnt);

		push_ctxt(&saved, &obd->obd_lvfs_ctxt, NULL);
		dget(fdentry);
		rc = llog_lvfs_close(env, handle);
		if (rc == 0) {
			mutex_lock_nested(&inode->i_mutex, I_MUTEX_PARENT);
			rc = ll_vfs_unlink(inode, fdentry, mnt);
			mutex_unlock(&inode->i_mutex);
		}
		mntput(mnt);

		dput(fdentry);
		pop_ctxt(&saved, &obd->obd_lvfs_ctxt, NULL);
		return rc;
	}

	OBDO_ALLOC(oa);
	if (oa == NULL)
		return -ENOMEM;

	oa->o_oi = handle->lgh_id.lgl_oi;
	oa->o_generation = handle->lgh_id.lgl_ogen;
#undef o_generation
	oa->o_valid = OBD_MD_FLID | OBD_MD_FLGROUP | OBD_MD_FLGENER;

	rc = llog_lvfs_close(env, handle);
	if (rc)
		GOTO(out, rc);

	th = fsfilt_start_log(obd, inode, FSFILT_OP_UNLINK, NULL, 1);
	if (IS_ERR(th)) {
		CERROR("fsfilt_start failed: %ld\n", PTR_ERR(th));
		GOTO(out, rc = PTR_ERR(th));
	}

	rc = obd_destroy(NULL, handle->lgh_ctxt->loc_exp, oa,
			 NULL, NULL, NULL, NULL);

	rc1 = fsfilt_commit(obd, inode, th, 0);
	if (rc == 0 && rc1 != 0)
		rc = rc1;
 out:
	OBDO_FREE(oa);
	return rc;
}

static int llog_lvfs_declare_create(const struct lu_env *env,
				    struct llog_handle *res,
				    struct thandle *th)
{
	return 0;
}

static int llog_lvfs_declare_write_rec(const struct lu_env *env,
				       struct llog_handle *loghandle,
				       struct llog_rec_hdr *rec,
				       int idx, struct thandle *th)
{
	return 0;
}

struct llog_operations llog_lvfs_ops = {
	.lop_write_rec		= llog_lvfs_write_rec,
	.lop_next_block		= llog_lvfs_next_block,
	.lop_prev_block		= llog_lvfs_prev_block,
	.lop_read_header	= llog_lvfs_read_header,
	.lop_create		= llog_lvfs_create,
	.lop_destroy		= llog_lvfs_destroy,
	.lop_close		= llog_lvfs_close,
	.lop_open		= llog_lvfs_open,
	.lop_exist		= llog_lvfs_exist,
	.lop_declare_create	= llog_lvfs_declare_create,
	.lop_declare_write_rec	= llog_lvfs_declare_write_rec,
};
EXPORT_SYMBOL(llog_lvfs_ops);
#else /* !__KERNEL__ */
struct llog_operations llog_lvfs_ops = {};
#endif
