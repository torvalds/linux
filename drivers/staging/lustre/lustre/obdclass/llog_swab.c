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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/obdclass/llog_swab.c
 *
 * Swabbing of llog datatypes (from disk or over the wire).
 *
 * Author: jacob berkman  <jacob@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_LOG

#include "../include/lustre_log.h"

static void print_llogd_body(struct llogd_body *d)
{
	CDEBUG(D_OTHER, "llogd body: %p\n", d);
	CDEBUG(D_OTHER, "\tlgd_logid.lgl_oi: "DOSTID"\n",
	       POSTID(&d->lgd_logid.lgl_oi));
	CDEBUG(D_OTHER, "\tlgd_logid.lgl_ogen: %#x\n", d->lgd_logid.lgl_ogen);
	CDEBUG(D_OTHER, "\tlgd_ctxt_idx: %#x\n", d->lgd_ctxt_idx);
	CDEBUG(D_OTHER, "\tlgd_llh_flags: %#x\n", d->lgd_llh_flags);
	CDEBUG(D_OTHER, "\tlgd_index: %#x\n", d->lgd_index);
	CDEBUG(D_OTHER, "\tlgd_saved_index: %#x\n", d->lgd_saved_index);
	CDEBUG(D_OTHER, "\tlgd_len: %#x\n", d->lgd_len);
	CDEBUG(D_OTHER, "\tlgd_cur_offset: %#llx\n", d->lgd_cur_offset);
}

void lustre_swab_lu_fid(struct lu_fid *fid)
{
	__swab64s(&fid->f_seq);
	__swab32s(&fid->f_oid);
	__swab32s(&fid->f_ver);
}
EXPORT_SYMBOL(lustre_swab_lu_fid);

void lustre_swab_ost_id(struct ost_id *oid)
{
	if (fid_seq_is_mdt0(oid->oi.oi_seq)) {
		__swab64s(&oid->oi.oi_id);
		__swab64s(&oid->oi.oi_seq);
	} else {
		lustre_swab_lu_fid(&oid->oi_fid);
	}
}
EXPORT_SYMBOL(lustre_swab_ost_id);

static void lustre_swab_llog_id(struct llog_logid *log_id)
{
	__swab64s(&log_id->lgl_oi.oi.oi_id);
	__swab64s(&log_id->lgl_oi.oi.oi_seq);
	__swab32s(&log_id->lgl_ogen);
}

void lustre_swab_llogd_body(struct llogd_body *d)
{
	print_llogd_body(d);
	lustre_swab_llog_id(&d->lgd_logid);
	__swab32s(&d->lgd_ctxt_idx);
	__swab32s(&d->lgd_llh_flags);
	__swab32s(&d->lgd_index);
	__swab32s(&d->lgd_saved_index);
	__swab32s(&d->lgd_len);
	__swab64s(&d->lgd_cur_offset);
	print_llogd_body(d);
}
EXPORT_SYMBOL(lustre_swab_llogd_body);

void lustre_swab_llogd_conn_body(struct llogd_conn_body *d)
{
	__swab64s(&d->lgdc_gen.mnt_cnt);
	__swab64s(&d->lgdc_gen.conn_cnt);
	lustre_swab_llog_id(&d->lgdc_logid);
	__swab32s(&d->lgdc_ctxt_idx);
}
EXPORT_SYMBOL(lustre_swab_llogd_conn_body);

static void lustre_swab_ll_fid(struct ll_fid *fid)
{
	__swab64s(&fid->id);
	__swab32s(&fid->generation);
	__swab32s(&fid->f_type);
}

void lustre_swab_lu_seq_range(struct lu_seq_range *range)
{
	__swab64s(&range->lsr_start);
	__swab64s(&range->lsr_end);
	__swab32s(&range->lsr_index);
	__swab32s(&range->lsr_flags);
}
EXPORT_SYMBOL(lustre_swab_lu_seq_range);

void lustre_swab_llog_rec(struct llog_rec_hdr *rec)
{
	struct llog_rec_tail *tail = NULL;

	__swab32s(&rec->lrh_len);
	__swab32s(&rec->lrh_index);
	__swab32s(&rec->lrh_type);
	__swab32s(&rec->lrh_id);

	switch (rec->lrh_type) {
	case OST_SZ_REC:
	{
		struct llog_size_change_rec *lsc =
			(struct llog_size_change_rec *)rec;

		lustre_swab_ll_fid(&lsc->lsc_fid);
		__swab32s(&lsc->lsc_ioepoch);
		tail = &lsc->lsc_tail;
		break;
	}
	case MDS_UNLINK_REC:
	{
		struct llog_unlink_rec *lur = (struct llog_unlink_rec *)rec;

		__swab64s(&lur->lur_oid);
		__swab32s(&lur->lur_oseq);
		__swab32s(&lur->lur_count);
		tail = &lur->lur_tail;
		break;
	}
	case MDS_UNLINK64_REC:
	{
		struct llog_unlink64_rec *lur =
			(struct llog_unlink64_rec *)rec;

		lustre_swab_lu_fid(&lur->lur_fid);
		__swab32s(&lur->lur_count);
		tail = &lur->lur_tail;
		break;
	}
	case CHANGELOG_REC:
	{
		struct llog_changelog_rec *cr =
			(struct llog_changelog_rec *)rec;

		__swab16s(&cr->cr.cr_namelen);
		__swab16s(&cr->cr.cr_flags);
		__swab32s(&cr->cr.cr_type);
		__swab64s(&cr->cr.cr_index);
		__swab64s(&cr->cr.cr_prev);
		__swab64s(&cr->cr.cr_time);
		lustre_swab_lu_fid(&cr->cr.cr_tfid);
		lustre_swab_lu_fid(&cr->cr.cr_pfid);
		if (CHANGELOG_REC_EXTENDED(&cr->cr)) {
			struct llog_changelog_ext_rec *ext =
				(struct llog_changelog_ext_rec *)rec;

			lustre_swab_lu_fid(&ext->cr.cr_sfid);
			lustre_swab_lu_fid(&ext->cr.cr_spfid);
			tail = &ext->cr_tail;
		} else {
			tail = &cr->cr_tail;
		}
		tail = (struct llog_rec_tail *)((char *)tail +
						cr->cr.cr_namelen);
		break;
	}
	case CHANGELOG_USER_REC:
	{
		struct llog_changelog_user_rec *cur =
			(struct llog_changelog_user_rec *)rec;

		__swab32s(&cur->cur_id);
		__swab64s(&cur->cur_endrec);
		tail = &cur->cur_tail;
		break;
	}

	case HSM_AGENT_REC: {
		struct llog_agent_req_rec *arr =
			(struct llog_agent_req_rec *)rec;

		__swab32s(&arr->arr_hai.hai_len);
		__swab32s(&arr->arr_hai.hai_action);
		lustre_swab_lu_fid(&arr->arr_hai.hai_fid);
		lustre_swab_lu_fid(&arr->arr_hai.hai_dfid);
		__swab64s(&arr->arr_hai.hai_cookie);
		__swab64s(&arr->arr_hai.hai_extent.offset);
		__swab64s(&arr->arr_hai.hai_extent.length);
		__swab64s(&arr->arr_hai.hai_gid);
		/* no swabing for opaque data */
		/* hai_data[0]; */
		break;
	}

	case MDS_SETATTR64_REC:
	{
		struct llog_setattr64_rec *lsr =
			(struct llog_setattr64_rec *)rec;

		lustre_swab_ost_id(&lsr->lsr_oi);
		__swab32s(&lsr->lsr_uid);
		__swab32s(&lsr->lsr_uid_h);
		__swab32s(&lsr->lsr_gid);
		__swab32s(&lsr->lsr_gid_h);
		__swab64s(&lsr->lsr_valid);
		tail = &lsr->lsr_tail;
		break;
	}
	case OBD_CFG_REC:
		/* these are swabbed as they are consumed */
		break;
	case LLOG_HDR_MAGIC:
	{
		struct llog_log_hdr *llh = (struct llog_log_hdr *)rec;

		__swab64s(&llh->llh_timestamp);
		__swab32s(&llh->llh_count);
		__swab32s(&llh->llh_bitmap_offset);
		__swab32s(&llh->llh_flags);
		__swab32s(&llh->llh_size);
		__swab32s(&llh->llh_cat_idx);
		tail = &llh->llh_tail;
		break;
	}
	case LLOG_LOGID_MAGIC:
	{
		struct llog_logid_rec *lid = (struct llog_logid_rec *)rec;

		lustre_swab_llog_id(&lid->lid_id);
		tail = &lid->lid_tail;
		break;
	}
	case LLOG_GEN_REC:
	{
		struct llog_gen_rec *lgr = (struct llog_gen_rec *)rec;

		__swab64s(&lgr->lgr_gen.mnt_cnt);
		__swab64s(&lgr->lgr_gen.conn_cnt);
		tail = &lgr->lgr_tail;
		break;
	}
	case LLOG_PAD_MAGIC:
		break;
	default:
		CERROR("Unknown llog rec type %#x swabbing rec %p\n",
		       rec->lrh_type, rec);
	}

	if (tail) {
		__swab32s(&tail->lrt_len);
		__swab32s(&tail->lrt_index);
	}
}
EXPORT_SYMBOL(lustre_swab_llog_rec);

static void print_llog_hdr(struct llog_log_hdr *h)
{
	CDEBUG(D_OTHER, "llog header: %p\n", h);
	CDEBUG(D_OTHER, "\tllh_hdr.lrh_index: %#x\n", h->llh_hdr.lrh_index);
	CDEBUG(D_OTHER, "\tllh_hdr.lrh_len: %#x\n", h->llh_hdr.lrh_len);
	CDEBUG(D_OTHER, "\tllh_hdr.lrh_type: %#x\n", h->llh_hdr.lrh_type);
	CDEBUG(D_OTHER, "\tllh_timestamp: %#llx\n", h->llh_timestamp);
	CDEBUG(D_OTHER, "\tllh_count: %#x\n", h->llh_count);
	CDEBUG(D_OTHER, "\tllh_bitmap_offset: %#x\n", h->llh_bitmap_offset);
	CDEBUG(D_OTHER, "\tllh_flags: %#x\n", h->llh_flags);
	CDEBUG(D_OTHER, "\tllh_size: %#x\n", h->llh_size);
	CDEBUG(D_OTHER, "\tllh_cat_idx: %#x\n", h->llh_cat_idx);
	CDEBUG(D_OTHER, "\tllh_tail.lrt_index: %#x\n", h->llh_tail.lrt_index);
	CDEBUG(D_OTHER, "\tllh_tail.lrt_len: %#x\n", h->llh_tail.lrt_len);
}

void lustre_swab_llog_hdr(struct llog_log_hdr *h)
{
	print_llog_hdr(h);

	lustre_swab_llog_rec(&h->llh_hdr);

	print_llog_hdr(h);
}
EXPORT_SYMBOL(lustre_swab_llog_hdr);

static void print_lustre_cfg(struct lustre_cfg *lcfg)
{
	int i;

	if (!(libcfs_debug & D_OTHER)) /* don't loop on nothing */
		return;
	CDEBUG(D_OTHER, "lustre_cfg: %p\n", lcfg);
	CDEBUG(D_OTHER, "\tlcfg->lcfg_version: %#x\n", lcfg->lcfg_version);

	CDEBUG(D_OTHER, "\tlcfg->lcfg_command: %#x\n", lcfg->lcfg_command);
	CDEBUG(D_OTHER, "\tlcfg->lcfg_num: %#x\n", lcfg->lcfg_num);
	CDEBUG(D_OTHER, "\tlcfg->lcfg_flags: %#x\n", lcfg->lcfg_flags);
	CDEBUG(D_OTHER, "\tlcfg->lcfg_nid: %s\n", libcfs_nid2str(lcfg->lcfg_nid));

	CDEBUG(D_OTHER, "\tlcfg->lcfg_bufcount: %d\n", lcfg->lcfg_bufcount);
	if (lcfg->lcfg_bufcount < LUSTRE_CFG_MAX_BUFCOUNT)
		for (i = 0; i < lcfg->lcfg_bufcount; i++)
			CDEBUG(D_OTHER, "\tlcfg->lcfg_buflens[%d]: %d\n",
			       i, lcfg->lcfg_buflens[i]);
}

void lustre_swab_lustre_cfg(struct lustre_cfg *lcfg)
{
	int i;

	__swab32s(&lcfg->lcfg_version);

	if (lcfg->lcfg_version != LUSTRE_CFG_VERSION) {
		CERROR("not swabbing lustre_cfg version %#x (expecting %#x)\n",
		       lcfg->lcfg_version, LUSTRE_CFG_VERSION);
		return;
	}

	__swab32s(&lcfg->lcfg_command);
	__swab32s(&lcfg->lcfg_num);
	__swab32s(&lcfg->lcfg_flags);
	__swab64s(&lcfg->lcfg_nid);
	__swab32s(&lcfg->lcfg_bufcount);
	for (i = 0; i < lcfg->lcfg_bufcount && i < LUSTRE_CFG_MAX_BUFCOUNT; i++)
		__swab32s(&lcfg->lcfg_buflens[i]);

	print_lustre_cfg(lcfg);
}
EXPORT_SYMBOL(lustre_swab_lustre_cfg);

/* used only for compatibility with old on-disk cfg_marker data */
struct cfg_marker32 {
	__u32   cm_step;
	__u32   cm_flags;
	__u32   cm_vers;
	__u32   padding;
	__u32   cm_createtime;
	__u32   cm_canceltime;
	char    cm_tgtname[MTI_NAME_MAXLEN];
	char    cm_comment[MTI_NAME_MAXLEN];
};

#define MTI_NAMELEN32    (MTI_NAME_MAXLEN - \
	(sizeof(struct cfg_marker) - sizeof(struct cfg_marker32)))

void lustre_swab_cfg_marker(struct cfg_marker *marker, int swab, int size)
{
	struct cfg_marker32 *cm32 = (struct cfg_marker32 *)marker;

	if (swab) {
		__swab32s(&marker->cm_step);
		__swab32s(&marker->cm_flags);
		__swab32s(&marker->cm_vers);
	}
	if (size == sizeof(*cm32)) {
		__u32 createtime, canceltime;
		/* There was a problem with the original declaration of
		 * cfg_marker on 32-bit systems because it used time_t as
		 * a wire protocol structure, and didn't verify this in
		 * wirecheck.  We now have to convert the offsets of the
		 * later fields in order to work on 32- and 64-bit systems.
		 *
		 * Fortunately, the cm_comment field has no functional use
		 * so can be sacrificed when converting the timestamp size.
		 *
		 * Overwrite fields from the end first, so they are not
		 * clobbered, and use memmove() instead of memcpy() because
		 * the source and target buffers overlap.  bug 16771
		 */
		createtime = cm32->cm_createtime;
		canceltime = cm32->cm_canceltime;
		memmove(marker->cm_comment, cm32->cm_comment, MTI_NAMELEN32);
		marker->cm_comment[MTI_NAMELEN32 - 1] = '\0';
		memmove(marker->cm_tgtname, cm32->cm_tgtname,
			sizeof(marker->cm_tgtname));
		if (swab) {
			__swab32s(&createtime);
			__swab32s(&canceltime);
		}
		marker->cm_createtime = createtime;
		marker->cm_canceltime = canceltime;
		CDEBUG(D_CONFIG, "Find old cfg_marker(Srv32b,Clt64b) for target %s, converting\n",
		       marker->cm_tgtname);
	} else if (swab) {
		__swab64s(&marker->cm_createtime);
		__swab64s(&marker->cm_canceltime);
	}
}
EXPORT_SYMBOL(lustre_swab_cfg_marker);
