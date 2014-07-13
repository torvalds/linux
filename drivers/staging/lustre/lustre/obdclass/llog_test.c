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
 * lustre/obdclass/llog_test.c
 *
 * Author: Phil Schwan <phil@clusterfs.com>
 * Author: Mikhail Pershin <mike.pershin@intel.com>
 */

#define DEBUG_SUBSYSTEM S_CLASS

#include <linux/module.h>
#include <linux/init.h>

#include "../include/obd_class.h"
#include "../include/lustre_fid.h"
#include "../include/lustre_log.h"

/* This is slightly more than the number of records that can fit into a
 * single llog file, because the llog_log_header takes up some of the
 * space in the first block that cannot be used for the bitmap. */
#define LLOG_TEST_RECNUM  (LLOG_CHUNK_SIZE * 8)

static int llog_test_rand;
static struct obd_uuid uuid = { .uuid = "test_uuid" };
static struct llog_logid cat_logid;

struct llog_mini_rec {
	struct llog_rec_hdr     lmr_hdr;
	struct llog_rec_tail    lmr_tail;
} __attribute__((packed));

static int verify_handle(char *test, struct llog_handle *llh, int num_recs)
{
	int i;
	int last_idx = 0;
	int active_recs = 0;

	for (i = 0; i < LLOG_BITMAP_BYTES * 8; i++) {
		if (ext2_test_bit(i, llh->lgh_hdr->llh_bitmap)) {
			last_idx = i;
			active_recs++;
		}
	}

	if (active_recs != num_recs) {
		CERROR("%s: expected %d active recs after write, found %d\n",
		       test, num_recs, active_recs);
		return -ERANGE;
	}

	if (llh->lgh_hdr->llh_count != num_recs) {
		CERROR("%s: handle->count is %d, expected %d after write\n",
		       test, llh->lgh_hdr->llh_count, num_recs);
		return -ERANGE;
	}

	if (llh->lgh_last_idx < last_idx) {
		CERROR("%s: handle->last_idx is %d, expected %d after write\n",
		       test, llh->lgh_last_idx, last_idx);
		return -ERANGE;
	}

	return 0;
}

/* Test named-log create/open, close */
static int llog_test_1(const struct lu_env *env,
		       struct obd_device *obd, char *name)
{
	struct llog_handle	*llh;
	struct llog_ctxt	*ctxt;
	int rc;
	int rc2;

	CWARN("1a: create a log with name: %s\n", name);
	ctxt = llog_get_context(obd, LLOG_TEST_ORIG_CTXT);
	LASSERT(ctxt);

	rc = llog_open_create(env, ctxt, &llh, NULL, name);
	if (rc) {
		CERROR("1a: llog_create with name %s failed: %d\n", name, rc);
		GOTO(out, rc);
	}
	rc = llog_init_handle(env, llh, LLOG_F_IS_PLAIN, &uuid);
	if (rc) {
		CERROR("1a: can't init llog handle: %d\n", rc);
		GOTO(out_close, rc);
	}

	rc = verify_handle("1", llh, 1);

	CWARN("1b: close newly-created log\n");
out_close:
	rc2 = llog_close(env, llh);
	if (rc2) {
		CERROR("1b: close log %s failed: %d\n", name, rc2);
		if (rc == 0)
			rc = rc2;
	}
out:
	llog_ctxt_put(ctxt);
	return rc;
}

/* Test named-log reopen; returns opened log on success */
static int llog_test_2(const struct lu_env *env, struct obd_device *obd,
		       char *name, struct llog_handle **llh)
{
	struct llog_ctxt	*ctxt;
	struct llog_handle	*loghandle;
	struct llog_logid	 logid;
	int			 rc;

	CWARN("2a: re-open a log with name: %s\n", name);
	ctxt = llog_get_context(obd, LLOG_TEST_ORIG_CTXT);
	LASSERT(ctxt);

	rc = llog_open(env, ctxt, llh, NULL, name, LLOG_OPEN_EXISTS);
	if (rc) {
		CERROR("2a: re-open log with name %s failed: %d\n", name, rc);
		GOTO(out_put, rc);
	}

	rc = llog_init_handle(env, *llh, LLOG_F_IS_PLAIN, &uuid);
	if (rc) {
		CERROR("2a: can't init llog handle: %d\n", rc);
		GOTO(out_close_llh, rc);
	}

	rc = verify_handle("2", *llh, 1);
	if (rc)
		GOTO(out_close_llh, rc);

	/* XXX: there is known issue with tests 2b, MGS is not able to create
	 * anonymous llog, exit now to allow following tests run.
	 * It is fixed in upcoming llog over OSD code */
	GOTO(out_put, rc);

	CWARN("2b: create a log without specified NAME & LOGID\n");
	rc = llog_open_create(env, ctxt, &loghandle, NULL, NULL);
	if (rc) {
		CERROR("2b: create log failed\n");
		GOTO(out_close_llh, rc);
	}
	rc = llog_init_handle(env, loghandle, LLOG_F_IS_PLAIN, &uuid);
	if (rc) {
		CERROR("2b: can't init llog handle: %d\n", rc);
		GOTO(out_close, rc);
	}

	logid = loghandle->lgh_id;
	llog_close(env, loghandle);

	CWARN("2c: re-open the log by LOGID\n");
	rc = llog_open(env, ctxt, &loghandle, &logid, NULL, LLOG_OPEN_EXISTS);
	if (rc) {
		CERROR("2c: re-open log by LOGID failed\n");
		GOTO(out_close_llh, rc);
	}

	rc = llog_init_handle(env, loghandle, LLOG_F_IS_PLAIN, &uuid);
	if (rc) {
		CERROR("2c: can't init llog handle: %d\n", rc);
		GOTO(out_close, rc);
	}

	CWARN("2b: destroy this log\n");
	rc = llog_destroy(env, loghandle);
	if (rc)
		CERROR("2d: destroy log failed\n");
out_close:
	llog_close(env, loghandle);
out_close_llh:
	if (rc)
		llog_close(env, *llh);
out_put:
	llog_ctxt_put(ctxt);

	return rc;
}

/* Test record writing, single and in bulk */
static int llog_test_3(const struct lu_env *env, struct obd_device *obd,
		       struct llog_handle *llh)
{
	struct llog_gen_rec	 lgr;
	int			 rc, i;
	int			 num_recs = 1; /* 1 for the header */

	lgr.lgr_hdr.lrh_len = lgr.lgr_tail.lrt_len = sizeof(lgr);
	lgr.lgr_hdr.lrh_type = LLOG_GEN_REC;

	CWARN("3a: write one create_rec\n");
	rc = llog_write(env, llh,  &lgr.lgr_hdr, NULL, 0, NULL, -1);
	num_recs++;
	if (rc < 0) {
		CERROR("3a: write one log record failed: %d\n", rc);
		return rc;
	}

	rc = verify_handle("3a", llh, num_recs);
	if (rc)
		return rc;

	CWARN("3b: write 10 cfg log records with 8 bytes bufs\n");
	for (i = 0; i < 10; i++) {
		struct llog_rec_hdr	hdr;
		char			buf[8];

		hdr.lrh_len = 8;
		hdr.lrh_type = OBD_CFG_REC;
		memset(buf, 0, sizeof(buf));
		rc = llog_write(env, llh, &hdr, NULL, 0, buf, -1);
		if (rc < 0) {
			CERROR("3b: write 10 records failed at #%d: %d\n",
			       i + 1, rc);
			return rc;
		}
		num_recs++;
	}

	rc = verify_handle("3b", llh, num_recs);
	if (rc)
		return rc;

	CWARN("3c: write 1000 more log records\n");
	for (i = 0; i < 1000; i++) {
		rc = llog_write(env, llh, &lgr.lgr_hdr, NULL, 0, NULL, -1);
		if (rc < 0) {
			CERROR("3c: write 1000 records failed at #%d: %d\n",
			       i + 1, rc);
			return rc;
		}
		num_recs++;
	}

	rc = verify_handle("3c", llh, num_recs);
	if (rc)
		return rc;

	CWARN("3d: write log more than BITMAP_SIZE, return -ENOSPC\n");
	for (i = 0; i < LLOG_BITMAP_SIZE(llh->lgh_hdr) + 1; i++) {
		struct llog_rec_hdr	hdr;
		char			buf_even[24];
		char			buf_odd[32];

		memset(buf_odd, 0, sizeof(buf_odd));
		memset(buf_even, 0, sizeof(buf_even));
		if ((i % 2) == 0) {
			hdr.lrh_len = 24;
			hdr.lrh_type = OBD_CFG_REC;
			rc = llog_write(env, llh, &hdr, NULL, 0, buf_even, -1);
		} else {
			hdr.lrh_len = 32;
			hdr.lrh_type = OBD_CFG_REC;
			rc = llog_write(env, llh, &hdr, NULL, 0, buf_odd, -1);
		}
		if (rc == -ENOSPC) {
			break;
		} else if (rc < 0) {
			CERROR("3d: write recs failed at #%d: %d\n",
			       i + 1, rc);
			return rc;
		}
		num_recs++;
	}
	if (rc != -ENOSPC) {
		CWARN("3d: write record more than BITMAP size!\n");
		return -EINVAL;
	}
	CWARN("3d: wrote %d more records before end of llog is reached\n",
	      num_recs);

	rc = verify_handle("3d", llh, num_recs);

	return rc;
}

/* Test catalogue additions */
static int llog_test_4(const struct lu_env *env, struct obd_device *obd)
{
	struct llog_handle	*cath;
	char			 name[10];
	int			 rc, rc2, i, buflen;
	struct llog_mini_rec	 lmr;
	struct llog_cookie	 cookie;
	struct llog_ctxt	*ctxt;
	int			 num_recs = 0;
	char			*buf;
	struct llog_rec_hdr	 rec;

	ctxt = llog_get_context(obd, LLOG_TEST_ORIG_CTXT);
	LASSERT(ctxt);

	lmr.lmr_hdr.lrh_len = lmr.lmr_tail.lrt_len = LLOG_MIN_REC_SIZE;
	lmr.lmr_hdr.lrh_type = 0xf00f00;

	sprintf(name, "%x", llog_test_rand + 1);
	CWARN("4a: create a catalog log with name: %s\n", name);
	rc = llog_open_create(env, ctxt, &cath, NULL, name);
	if (rc) {
		CERROR("4a: llog_create with name %s failed: %d\n", name, rc);
		GOTO(ctxt_release, rc);
	}
	rc = llog_init_handle(env, cath, LLOG_F_IS_CAT, &uuid);
	if (rc) {
		CERROR("4a: can't init llog handle: %d\n", rc);
		GOTO(out, rc);
	}

	num_recs++;
	cat_logid = cath->lgh_id;

	CWARN("4b: write 1 record into the catalog\n");
	rc = llog_cat_add(env, cath, &lmr.lmr_hdr, &cookie, NULL);
	if (rc != 1) {
		CERROR("4b: write 1 catalog record failed at: %d\n", rc);
		GOTO(out, rc);
	}
	num_recs++;
	rc = verify_handle("4b", cath, 2);
	if (rc)
		GOTO(out, rc);

	rc = verify_handle("4b", cath->u.chd.chd_current_log, num_recs);
	if (rc)
		GOTO(out, rc);

	CWARN("4c: cancel 1 log record\n");
	rc = llog_cat_cancel_records(env, cath, 1, &cookie);
	if (rc) {
		CERROR("4c: cancel 1 catalog based record failed: %d\n", rc);
		GOTO(out, rc);
	}
	num_recs--;

	rc = verify_handle("4c", cath->u.chd.chd_current_log, num_recs);
	if (rc)
		GOTO(out, rc);

	CWARN("4d: write %d more log records\n", LLOG_TEST_RECNUM);
	for (i = 0; i < LLOG_TEST_RECNUM; i++) {
		rc = llog_cat_add(env, cath, &lmr.lmr_hdr, NULL, NULL);
		if (rc) {
			CERROR("4d: write %d records failed at #%d: %d\n",
			       LLOG_TEST_RECNUM, i + 1, rc);
			GOTO(out, rc);
		}
		num_recs++;
	}

	/* make sure new plain llog appears */
	rc = verify_handle("4d", cath, 3);
	if (rc)
		GOTO(out, rc);

	CWARN("4e: add 5 large records, one record per block\n");
	buflen = LLOG_CHUNK_SIZE - sizeof(struct llog_rec_hdr) -
		 sizeof(struct llog_rec_tail);
	OBD_ALLOC(buf, buflen);
	if (buf == NULL)
		GOTO(out, rc = -ENOMEM);
	for (i = 0; i < 5; i++) {
		rec.lrh_len = buflen;
		rec.lrh_type = OBD_CFG_REC;
		rc = llog_cat_add(env, cath, &rec, NULL, buf);
		if (rc) {
			CERROR("4e: write 5 records failed at #%d: %d\n",
			       i + 1, rc);
			GOTO(out_free, rc);
		}
		num_recs++;
	}
out_free:
	OBD_FREE(buf, buflen);
out:
	CWARN("4f: put newly-created catalog\n");
	rc2 = llog_cat_close(env, cath);
	if (rc2) {
		CERROR("4: close log %s failed: %d\n", name, rc2);
		if (rc == 0)
			rc = rc2;
	}
ctxt_release:
	llog_ctxt_put(ctxt);
	return rc;
}

static int cat_counter;

static int cat_print_cb(const struct lu_env *env, struct llog_handle *llh,
			struct llog_rec_hdr *rec, void *data)
{
	struct llog_logid_rec	*lir = (struct llog_logid_rec *)rec;
	struct lu_fid		 fid = {0};

	if (rec->lrh_type != LLOG_LOGID_MAGIC) {
		CERROR("invalid record in catalog\n");
		return -EINVAL;
	}

	logid_to_fid(&lir->lid_id, &fid);

	CWARN("seeing record at index %d - "DFID" in log "DFID"\n",
	      rec->lrh_index, PFID(&fid),
	      PFID(lu_object_fid(&llh->lgh_obj->do_lu)));

	cat_counter++;

	return 0;
}

static int plain_counter;

static int plain_print_cb(const struct lu_env *env, struct llog_handle *llh,
			  struct llog_rec_hdr *rec, void *data)
{
	struct lu_fid fid = {0};

	if (!(llh->lgh_hdr->llh_flags & LLOG_F_IS_PLAIN)) {
		CERROR("log is not plain\n");
		return -EINVAL;
	}

	logid_to_fid(&llh->lgh_id, &fid);

	CDEBUG(D_INFO, "seeing record at index %d in log "DFID"\n",
	       rec->lrh_index, PFID(&fid));

	plain_counter++;

	return 0;
}

static int cancel_count;

static int llog_cancel_rec_cb(const struct lu_env *env,
			      struct llog_handle *llh,
			      struct llog_rec_hdr *rec, void *data)
{
	struct llog_cookie cookie;

	if (!(llh->lgh_hdr->llh_flags & LLOG_F_IS_PLAIN)) {
		CERROR("log is not plain\n");
		return -EINVAL;
	}

	cookie.lgc_lgl = llh->lgh_id;
	cookie.lgc_index = rec->lrh_index;

	llog_cat_cancel_records(env, llh->u.phd.phd_cat_handle, 1, &cookie);
	cancel_count++;
	if (cancel_count == LLOG_TEST_RECNUM)
		return -LLOG_EEMPTY;
	return 0;
}

/* Test log and catalogue processing */
static int llog_test_5(const struct lu_env *env, struct obd_device *obd)
{
	struct llog_handle	*llh = NULL;
	char			 name[10];
	int			 rc, rc2;
	struct llog_mini_rec	 lmr;
	struct llog_ctxt	*ctxt;

	ctxt = llog_get_context(obd, LLOG_TEST_ORIG_CTXT);
	LASSERT(ctxt);

	lmr.lmr_hdr.lrh_len = lmr.lmr_tail.lrt_len = LLOG_MIN_REC_SIZE;
	lmr.lmr_hdr.lrh_type = 0xf00f00;

	CWARN("5a: re-open catalog by id\n");
	rc = llog_open(env, ctxt, &llh, &cat_logid, NULL, LLOG_OPEN_EXISTS);
	if (rc) {
		CERROR("5a: llog_create with logid failed: %d\n", rc);
		GOTO(out_put, rc);
	}

	rc = llog_init_handle(env, llh, LLOG_F_IS_CAT, &uuid);
	if (rc) {
		CERROR("5a: can't init llog handle: %d\n", rc);
		GOTO(out, rc);
	}

	CWARN("5b: print the catalog entries.. we expect 2\n");
	cat_counter = 0;
	rc = llog_process(env, llh, cat_print_cb, "test 5", NULL);
	if (rc) {
		CERROR("5b: process with cat_print_cb failed: %d\n", rc);
		GOTO(out, rc);
	}
	if (cat_counter != 2) {
		CERROR("5b: %d entries in catalog\n", cat_counter);
		GOTO(out, rc = -EINVAL);
	}

	CWARN("5c: Cancel %d records, see one log zapped\n", LLOG_TEST_RECNUM);
	cancel_count = 0;
	rc = llog_cat_process(env, llh, llog_cancel_rec_cb, "foobar", 0, 0);
	if (rc != -LLOG_EEMPTY) {
		CERROR("5c: process with cat_cancel_cb failed: %d\n", rc);
		GOTO(out, rc);
	}

	CWARN("5c: print the catalog entries.. we expect 1\n");
	cat_counter = 0;
	rc = llog_process(env, llh, cat_print_cb, "test 5", NULL);
	if (rc) {
		CERROR("5c: process with cat_print_cb failed: %d\n", rc);
		GOTO(out, rc);
	}
	if (cat_counter != 1) {
		CERROR("5c: %d entries in catalog\n", cat_counter);
		GOTO(out, rc = -EINVAL);
	}

	CWARN("5d: add 1 record to the log with many canceled empty pages\n");
	rc = llog_cat_add(env, llh, &lmr.lmr_hdr, NULL, NULL);
	if (rc) {
		CERROR("5d: add record to the log with many canceled empty "
		       "pages failed\n");
		GOTO(out, rc);
	}

	CWARN("5e: print plain log entries.. expect 6\n");
	plain_counter = 0;
	rc = llog_cat_process(env, llh, plain_print_cb, "foobar", 0, 0);
	if (rc) {
		CERROR("5e: process with plain_print_cb failed: %d\n", rc);
		GOTO(out, rc);
	}
	if (plain_counter != 6) {
		CERROR("5e: found %d records\n", plain_counter);
		GOTO(out, rc = -EINVAL);
	}

	CWARN("5f: print plain log entries reversely.. expect 6\n");
	plain_counter = 0;
	rc = llog_cat_reverse_process(env, llh, plain_print_cb, "foobar");
	if (rc) {
		CERROR("5f: reversely process with plain_print_cb failed:"
		       "%d\n", rc);
		GOTO(out, rc);
	}
	if (plain_counter != 6) {
		CERROR("5f: found %d records\n", plain_counter);
		GOTO(out, rc = -EINVAL);
	}

out:
	CWARN("5g: close re-opened catalog\n");
	rc2 = llog_cat_close(env, llh);
	if (rc2) {
		CERROR("5g: close log %s failed: %d\n", name, rc2);
		if (rc == 0)
			rc = rc2;
	}
out_put:
	llog_ctxt_put(ctxt);

	return rc;
}

/* Test client api; open log by name and process */
static int llog_test_6(const struct lu_env *env, struct obd_device *obd,
		       char *name)
{
	struct obd_device	*mgc_obd;
	struct llog_ctxt	*ctxt;
	struct obd_uuid		*mgs_uuid;
	struct obd_export	*exp;
	struct obd_uuid		 uuid = { "LLOG_TEST6_UUID" };
	struct llog_handle	*llh = NULL;
	struct llog_ctxt	*nctxt;
	int			 rc, rc2;

	ctxt = llog_get_context(obd, LLOG_TEST_ORIG_CTXT);
	LASSERT(ctxt);
	mgs_uuid = &ctxt->loc_exp->exp_obd->obd_uuid;

	CWARN("6a: re-open log %s using client API\n", name);
	mgc_obd = class_find_client_obd(mgs_uuid, LUSTRE_MGC_NAME, NULL);
	if (mgc_obd == NULL) {
		CERROR("6a: no MGC devices connected to %s found.\n",
		       mgs_uuid->uuid);
		GOTO(ctxt_release, rc = -ENOENT);
	}

	rc = obd_connect(NULL, &exp, mgc_obd, &uuid,
			 NULL /* obd_connect_data */, NULL);
	if (rc != -EALREADY) {
		CERROR("6a: connect on connected MGC (%s) failed to return"
		       " -EALREADY", mgc_obd->obd_name);
		if (rc == 0)
			obd_disconnect(exp);
		GOTO(ctxt_release, rc = -EINVAL);
	}

	nctxt = llog_get_context(mgc_obd, LLOG_CONFIG_REPL_CTXT);
	rc = llog_open(env, nctxt, &llh, NULL, name, LLOG_OPEN_EXISTS);
	if (rc) {
		CERROR("6a: llog_open failed %d\n", rc);
		GOTO(nctxt_put, rc);
	}

	rc = llog_init_handle(env, llh, LLOG_F_IS_PLAIN, NULL);
	if (rc) {
		CERROR("6a: llog_init_handle failed %d\n", rc);
		GOTO(parse_out, rc);
	}

	plain_counter = 1; /* llog header is first record */
	CWARN("6b: process log %s using client API\n", name);
	rc = llog_process(env, llh, plain_print_cb, NULL, NULL);
	if (rc)
		CERROR("6b: llog_process failed %d\n", rc);
	CWARN("6b: processed %d records\n", plain_counter);

	rc = verify_handle("6b", llh, plain_counter);
	if (rc)
		GOTO(parse_out, rc);

	plain_counter = 1; /* llog header is first record */
	CWARN("6c: process log %s reversely using client API\n", name);
	rc = llog_reverse_process(env, llh, plain_print_cb, NULL, NULL);
	if (rc)
		CERROR("6c: llog_reverse_process failed %d\n", rc);
	CWARN("6c: processed %d records\n", plain_counter);

	rc = verify_handle("6c", llh, plain_counter);
	if (rc)
		GOTO(parse_out, rc);

parse_out:
	rc2 = llog_close(env, llh);
	if (rc2) {
		CERROR("6: llog_close failed: rc = %d\n", rc2);
		if (rc == 0)
			rc = rc2;
	}
nctxt_put:
	llog_ctxt_put(nctxt);
ctxt_release:
	llog_ctxt_put(ctxt);
	return rc;
}

static union {
	struct llog_rec_hdr		lrh;   /* common header */
	struct llog_logid_rec		llr;   /* LLOG_LOGID_MAGIC */
	struct llog_unlink64_rec	lur;   /* MDS_UNLINK64_REC */
	struct llog_setattr64_rec	lsr64; /* MDS_SETATTR64_REC */
	struct llog_size_change_rec	lscr;  /* OST_SZ_REC */
	struct llog_changelog_rec	lcr;   /* CHANGELOG_REC */
	struct llog_changelog_user_rec	lcur;  /* CHANGELOG_USER_REC */
	struct llog_gen_rec		lgr;   /* LLOG_GEN_REC */
} llog_records;

static int test_7_print_cb(const struct lu_env *env, struct llog_handle *llh,
			   struct llog_rec_hdr *rec, void *data)
{
	struct lu_fid fid = {0};

	logid_to_fid(&llh->lgh_id, &fid);

	CDEBUG(D_OTHER, "record type %#x at index %d in log "DFID"\n",
	       rec->lrh_type, rec->lrh_index, PFID(&fid));

	plain_counter++;
	return 0;
}

static int test_7_cancel_cb(const struct lu_env *env, struct llog_handle *llh,
			    struct llog_rec_hdr *rec, void *data)
{
	plain_counter++;
	/* test LLOG_DEL_RECORD is working */
	return LLOG_DEL_RECORD;
}

static int llog_test_7_sub(const struct lu_env *env, struct llog_ctxt *ctxt)
{
	struct llog_handle	*llh;
	int			 rc = 0, i, process_count;
	int			 num_recs = 0;

	rc = llog_open_create(env, ctxt, &llh, NULL, NULL);
	if (rc) {
		CERROR("7_sub: create log failed\n");
		return rc;
	}

	rc = llog_init_handle(env, llh,
			      LLOG_F_IS_PLAIN | LLOG_F_ZAP_WHEN_EMPTY,
			      &uuid);
	if (rc) {
		CERROR("7_sub: can't init llog handle: %d\n", rc);
		GOTO(out_close, rc);
	}
	for (i = 0; i < LLOG_BITMAP_SIZE(llh->lgh_hdr); i++) {
		rc = llog_write(env, llh, &llog_records.lrh, NULL, 0,
				NULL, -1);
		if (rc == -ENOSPC) {
			break;
		} else if (rc < 0) {
			CERROR("7_sub: write recs failed at #%d: %d\n",
			       i + 1, rc);
			GOTO(out_close, rc);
		}
		num_recs++;
	}
	if (rc != -ENOSPC) {
		CWARN("7_sub: write record more than BITMAP size!\n");
		GOTO(out_close, rc = -EINVAL);
	}

	rc = verify_handle("7_sub", llh, num_recs + 1);
	if (rc) {
		CERROR("7_sub: verify handle failed: %d\n", rc);
		GOTO(out_close, rc);
	}
	if (num_recs < LLOG_BITMAP_SIZE(llh->lgh_hdr) - 1)
		CWARN("7_sub: records are not aligned, written %d from %u\n",
		      num_recs, LLOG_BITMAP_SIZE(llh->lgh_hdr) - 1);

	plain_counter = 0;
	rc = llog_process(env, llh, test_7_print_cb, "test 7", NULL);
	if (rc) {
		CERROR("7_sub: llog process failed: %d\n", rc);
		GOTO(out_close, rc);
	}
	process_count = plain_counter;
	if (process_count != num_recs) {
		CERROR("7_sub: processed %d records from %d total\n",
		       process_count, num_recs);
		GOTO(out_close, rc = -EINVAL);
	}

	plain_counter = 0;
	rc = llog_reverse_process(env, llh, test_7_cancel_cb, "test 7", NULL);
	if (rc) {
		CERROR("7_sub: reverse llog process failed: %d\n", rc);
		GOTO(out_close, rc);
	}
	if (process_count != plain_counter) {
		CERROR("7_sub: Reverse/direct processing found different"
		       "number of records: %d/%d\n",
		       plain_counter, process_count);
		GOTO(out_close, rc = -EINVAL);
	}
	if (llog_exist(llh)) {
		CERROR("7_sub: llog exists but should be zapped\n");
		GOTO(out_close, rc = -EEXIST);
	}

	rc = verify_handle("7_sub", llh, 1);
out_close:
	if (rc)
		llog_destroy(env, llh);
	llog_close(env, llh);
	return rc;
}

/* Test all llog records writing and processing */
static int llog_test_7(const struct lu_env *env, struct obd_device *obd)
{
	struct llog_ctxt	*ctxt;
	int			 rc;

	ctxt = llog_get_context(obd, LLOG_TEST_ORIG_CTXT);

	CWARN("7a: test llog_logid_rec\n");
	llog_records.llr.lid_hdr.lrh_len = sizeof(llog_records.llr);
	llog_records.llr.lid_tail.lrt_len = sizeof(llog_records.llr);
	llog_records.llr.lid_hdr.lrh_type = LLOG_LOGID_MAGIC;

	rc = llog_test_7_sub(env, ctxt);
	if (rc) {
		CERROR("7a: llog_logid_rec test failed\n");
		GOTO(out, rc);
	}

	CWARN("7b: test llog_unlink64_rec\n");
	llog_records.lur.lur_hdr.lrh_len = sizeof(llog_records.lur);
	llog_records.lur.lur_tail.lrt_len = sizeof(llog_records.lur);
	llog_records.lur.lur_hdr.lrh_type = MDS_UNLINK64_REC;

	rc = llog_test_7_sub(env, ctxt);
	if (rc) {
		CERROR("7b: llog_unlink_rec test failed\n");
		GOTO(out, rc);
	}

	CWARN("7c: test llog_setattr64_rec\n");
	llog_records.lsr64.lsr_hdr.lrh_len = sizeof(llog_records.lsr64);
	llog_records.lsr64.lsr_tail.lrt_len = sizeof(llog_records.lsr64);
	llog_records.lsr64.lsr_hdr.lrh_type = MDS_SETATTR64_REC;

	rc = llog_test_7_sub(env, ctxt);
	if (rc) {
		CERROR("7c: llog_setattr64_rec test failed\n");
		GOTO(out, rc);
	}

	CWARN("7d: test llog_size_change_rec\n");
	llog_records.lscr.lsc_hdr.lrh_len = sizeof(llog_records.lscr);
	llog_records.lscr.lsc_tail.lrt_len = sizeof(llog_records.lscr);
	llog_records.lscr.lsc_hdr.lrh_type = OST_SZ_REC;

	rc = llog_test_7_sub(env, ctxt);
	if (rc) {
		CERROR("7d: llog_size_change_rec test failed\n");
		GOTO(out, rc);
	}

	CWARN("7e: test llog_changelog_rec\n");
	llog_records.lcr.cr_hdr.lrh_len = sizeof(llog_records.lcr);
	llog_records.lcr.cr_tail.lrt_len = sizeof(llog_records.lcr);
	llog_records.lcr.cr_hdr.lrh_type = CHANGELOG_REC;

	rc = llog_test_7_sub(env, ctxt);
	if (rc) {
		CERROR("7e: llog_changelog_rec test failed\n");
		GOTO(out, rc);
	}

	CWARN("7f: test llog_changelog_user_rec\n");
	llog_records.lcur.cur_hdr.lrh_len = sizeof(llog_records.lcur);
	llog_records.lcur.cur_tail.lrt_len = sizeof(llog_records.lcur);
	llog_records.lcur.cur_hdr.lrh_type = CHANGELOG_USER_REC;

	rc = llog_test_7_sub(env, ctxt);
	if (rc) {
		CERROR("7f: llog_changelog_user_rec test failed\n");
		GOTO(out, rc);
	}

	CWARN("7g: test llog_gen_rec\n");
	llog_records.lgr.lgr_hdr.lrh_len = sizeof(llog_records.lgr);
	llog_records.lgr.lgr_tail.lrt_len = sizeof(llog_records.lgr);
	llog_records.lgr.lgr_hdr.lrh_type = LLOG_GEN_REC;

	rc = llog_test_7_sub(env, ctxt);
	if (rc) {
		CERROR("7g: llog_size_change_rec test failed\n");
		GOTO(out, rc);
	}
out:
	llog_ctxt_put(ctxt);
	return rc;
}

/* -------------------------------------------------------------------------
 * Tests above, boring obd functions below
 * ------------------------------------------------------------------------- */
static int llog_run_tests(const struct lu_env *env, struct obd_device *obd)
{
	struct llog_handle	*llh = NULL;
	struct llog_ctxt	*ctxt;
	int			 rc, err;
	char			 name[10];

	ctxt = llog_get_context(obd, LLOG_TEST_ORIG_CTXT);
	LASSERT(ctxt);

	sprintf(name, "%x", llog_test_rand);

	rc = llog_test_1(env, obd, name);
	if (rc)
		GOTO(cleanup_ctxt, rc);

	rc = llog_test_2(env, obd, name, &llh);
	if (rc)
		GOTO(cleanup_ctxt, rc);

	rc = llog_test_3(env, obd, llh);
	if (rc)
		GOTO(cleanup, rc);

	rc = llog_test_4(env, obd);
	if (rc)
		GOTO(cleanup, rc);

	rc = llog_test_5(env, obd);
	if (rc)
		GOTO(cleanup, rc);

	rc = llog_test_6(env, obd, name);
	if (rc)
		GOTO(cleanup, rc);

	rc = llog_test_7(env, obd);
	if (rc)
		GOTO(cleanup, rc);

cleanup:
	err = llog_destroy(env, llh);
	if (err)
		CERROR("cleanup: llog_destroy failed: %d\n", err);
	llog_close(env, llh);
	if (rc == 0)
		rc = err;
cleanup_ctxt:
	llog_ctxt_put(ctxt);
	return rc;
}

#if defined (CONFIG_PROC_FS)
static struct lprocfs_vars lprocfs_llog_test_obd_vars[] = { {0} };
static struct lprocfs_vars lprocfs_llog_test_module_vars[] = { {0} };
static void lprocfs_llog_test_init_vars(struct lprocfs_static_vars *lvars)
{
    lvars->module_vars  = lprocfs_llog_test_module_vars;
    lvars->obd_vars     = lprocfs_llog_test_obd_vars;
}
#else
static void lprocfs_llog_test_init_vars(struct lprocfs_static_vars *lvars)
{
}
#endif

static int llog_test_cleanup(struct obd_device *obd)
{
	struct obd_device	*tgt;
	struct lu_env		 env;
	int			 rc;

	rc = lu_env_init(&env, LCT_LOCAL | LCT_MG_THREAD);
	if (rc)
		return rc;

	tgt = obd->obd_lvfs_ctxt.dt->dd_lu_dev.ld_obd;
	rc = llog_cleanup(&env, llog_get_context(tgt, LLOG_TEST_ORIG_CTXT));
	if (rc)
		CERROR("failed to llog_test_llog_finish: %d\n", rc);
	lu_env_fini(&env);
	return rc;
}

static int llog_test_setup(struct obd_device *obd, struct lustre_cfg *lcfg)
{
	struct obd_device	*tgt;
	struct llog_ctxt	*ctxt;
	struct dt_object	*o;
	struct lu_env		 env;
	struct lu_context	 test_session;
	int			 rc;

	if (lcfg->lcfg_bufcount < 2) {
		CERROR("requires a TARGET OBD name\n");
		return -EINVAL;
	}

	if (lcfg->lcfg_buflens[1] < 1) {
		CERROR("requires a TARGET OBD name\n");
		return -EINVAL;
	}

	/* disk obd */
	tgt = class_name2obd(lustre_cfg_string(lcfg, 1));
	if (!tgt || !tgt->obd_attached || !tgt->obd_set_up) {
		CERROR("target device not attached or not set up (%s)\n",
		       lustre_cfg_string(lcfg, 1));
		return -EINVAL;
	}

	rc = lu_env_init(&env, LCT_LOCAL | LCT_MG_THREAD);
	if (rc)
		return rc;

	rc = lu_context_init(&test_session, LCT_SESSION);
	if (rc)
		GOTO(cleanup_env, rc);
	test_session.lc_thread = (struct ptlrpc_thread *)current;
	lu_context_enter(&test_session);
	env.le_ses = &test_session;

	CWARN("Setup llog-test device over %s device\n",
	      lustre_cfg_string(lcfg, 1));

	OBD_SET_CTXT_MAGIC(&obd->obd_lvfs_ctxt);
	obd->obd_lvfs_ctxt.dt = lu2dt_dev(tgt->obd_lu_dev);

	rc = llog_setup(&env, tgt, &tgt->obd_olg, LLOG_TEST_ORIG_CTXT, tgt,
			&llog_osd_ops);
	if (rc)
		GOTO(cleanup_session, rc);

	/* use MGS llog dir for tests */
	ctxt = llog_get_context(tgt, LLOG_CONFIG_ORIG_CTXT);
	LASSERT(ctxt);
	o = ctxt->loc_dir;
	llog_ctxt_put(ctxt);

	ctxt = llog_get_context(tgt, LLOG_TEST_ORIG_CTXT);
	LASSERT(ctxt);
	ctxt->loc_dir = o;
	llog_ctxt_put(ctxt);

	llog_test_rand = cfs_rand();

	rc = llog_run_tests(&env, tgt);
	if (rc)
		llog_test_cleanup(obd);
cleanup_session:
	lu_context_exit(&test_session);
	lu_context_fini(&test_session);
cleanup_env:
	lu_env_fini(&env);
	return rc;
}

static struct obd_ops llog_obd_ops = {
	.o_owner       = THIS_MODULE,
	.o_setup       = llog_test_setup,
	.o_cleanup     = llog_test_cleanup,
};

static int __init llog_test_init(void)
{
	struct lprocfs_static_vars uninitialized_var(lvars);

	lprocfs_llog_test_init_vars(&lvars);
	return class_register_type(&llog_obd_ops, NULL,
				   lvars.module_vars, "llog_test", NULL);
}

static void __exit llog_test_exit(void)
{
	class_unregister_type("llog_test");
}

MODULE_AUTHOR("Sun Microsystems, Inc. <http://www.lustre.org/>");
MODULE_DESCRIPTION("llog test module");
MODULE_LICENSE("GPL");

module_init(llog_test_init);
module_exit(llog_test_exit);
