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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2014, Intel Corporation.
 *
 * Copyright 2015 Cray Inc, all rights reserved.
 * Author: Ben Evans.
 *
 * Define ost_id  associated functions
 */

#ifndef _UAPI_LUSTRE_OSTID_H_
#define _UAPI_LUSTRE_OSTID_H_

#include <linux/errno.h>
#include <uapi/linux/lustre/lustre_fid.h>

static inline __u64 lmm_oi_id(const struct ost_id *oi)
{
	return oi->oi.oi_id;
}

static inline __u64 lmm_oi_seq(const struct ost_id *oi)
{
	return oi->oi.oi_seq;
}

static inline void lmm_oi_set_seq(struct ost_id *oi, __u64 seq)
{
	oi->oi.oi_seq = seq;
}

static inline void lmm_oi_set_id(struct ost_id *oi, __u64 oid)
{
	oi->oi.oi_id = oid;
}

static inline void lmm_oi_le_to_cpu(struct ost_id *dst_oi,
				    const struct ost_id *src_oi)
{
	dst_oi->oi.oi_id = __le64_to_cpu(src_oi->oi.oi_id);
	dst_oi->oi.oi_seq = __le64_to_cpu(src_oi->oi.oi_seq);
}

static inline void lmm_oi_cpu_to_le(struct ost_id *dst_oi,
				    const struct ost_id *src_oi)
{
	dst_oi->oi.oi_id = __cpu_to_le64(src_oi->oi.oi_id);
	dst_oi->oi.oi_seq = __cpu_to_le64(src_oi->oi.oi_seq);
}

/* extract OST sequence (group) from a wire ost_id (id/seq) pair */
static inline __u64 ostid_seq(const struct ost_id *ostid)
{
	if (fid_seq_is_mdt0(ostid->oi.oi_seq))
		return FID_SEQ_OST_MDT0;

	if (fid_seq_is_default(ostid->oi.oi_seq))
		return FID_SEQ_LOV_DEFAULT;

	if (fid_is_idif(&ostid->oi_fid))
		return FID_SEQ_OST_MDT0;

	return fid_seq(&ostid->oi_fid);
}

/* extract OST objid from a wire ost_id (id/seq) pair */
static inline __u64 ostid_id(const struct ost_id *ostid)
{
	if (fid_seq_is_mdt0(ostid->oi.oi_seq))
		return ostid->oi.oi_id & IDIF_OID_MASK;

	if (fid_seq_is_default(ostid->oi.oi_seq))
		return ostid->oi.oi_id;

	if (fid_is_idif(&ostid->oi_fid))
		return fid_idif_id(fid_seq(&ostid->oi_fid),
				   fid_oid(&ostid->oi_fid), 0);

	return fid_oid(&ostid->oi_fid);
}

static inline void ostid_set_seq(struct ost_id *oi, __u64 seq)
{
	if (fid_seq_is_mdt0(seq) || fid_seq_is_default(seq)) {
		oi->oi.oi_seq = seq;
	} else {
		oi->oi_fid.f_seq = seq;
		/*
		 * Note: if f_oid + f_ver is zero, we need init it
		 * to be 1, otherwise, ostid_seq will treat this
		 * as old ostid (oi_seq == 0)
		 */
		if (!oi->oi_fid.f_oid && !oi->oi_fid.f_ver)
			oi->oi_fid.f_oid = LUSTRE_FID_INIT_OID;
	}
}

static inline void ostid_set_seq_mdt0(struct ost_id *oi)
{
	ostid_set_seq(oi, FID_SEQ_OST_MDT0);
}

static inline void ostid_set_seq_echo(struct ost_id *oi)
{
	ostid_set_seq(oi, FID_SEQ_ECHO);
}

static inline void ostid_set_seq_llog(struct ost_id *oi)
{
	ostid_set_seq(oi, FID_SEQ_LLOG);
}

static inline void ostid_cpu_to_le(const struct ost_id *src_oi,
				   struct ost_id *dst_oi)
{
	if (fid_seq_is_mdt0(src_oi->oi.oi_seq)) {
		dst_oi->oi.oi_id = __cpu_to_le64(src_oi->oi.oi_id);
		dst_oi->oi.oi_seq = __cpu_to_le64(src_oi->oi.oi_seq);
	} else {
		fid_cpu_to_le(&dst_oi->oi_fid, &src_oi->oi_fid);
	}
}

static inline void ostid_le_to_cpu(const struct ost_id *src_oi,
				   struct ost_id *dst_oi)
{
	if (fid_seq_is_mdt0(src_oi->oi.oi_seq)) {
		dst_oi->oi.oi_id = __le64_to_cpu(src_oi->oi.oi_id);
		dst_oi->oi.oi_seq = __le64_to_cpu(src_oi->oi.oi_seq);
	} else {
		fid_le_to_cpu(&dst_oi->oi_fid, &src_oi->oi_fid);
	}
}

/**
 * Sigh, because pre-2.4 uses
 * struct lov_mds_md_v1 {
 *	........
 *	__u64 lmm_object_id;
 *	__u64 lmm_object_seq;
 *      ......
 *      }
 * to identify the LOV(MDT) object, and lmm_object_seq will
 * be normal_fid, which make it hard to combine these conversion
 * to ostid_to FID. so we will do lmm_oi/fid conversion separately
 *
 * We can tell the lmm_oi by this way,
 * 1.8: lmm_object_id = {inode}, lmm_object_gr = 0
 * 2.1: lmm_object_id = {oid < 128k}, lmm_object_seq = FID_SEQ_NORMAL
 * 2.4: lmm_oi.f_seq = FID_SEQ_NORMAL, lmm_oi.f_oid = {oid < 128k},
 *      lmm_oi.f_ver = 0
 *
 * But currently lmm_oi/lsm_oi does not have any "real" usages,
 * except for printing some information, and the user can always
 * get the real FID from LMA, besides this multiple case check might
 * make swab more complicate. So we will keep using id/seq for lmm_oi.
 */

static inline void fid_to_lmm_oi(const struct lu_fid *fid,
				 struct ost_id *oi)
{
	oi->oi.oi_id = fid_oid(fid);
	oi->oi.oi_seq = fid_seq(fid);
}

/**
 * Unpack an OST object id/seq (group) into a FID.  This is needed for
 * converting all obdo, lmm, lsm, etc. 64-bit id/seq pairs into proper
 * FIDs.  Note that if an id/seq is already in FID/IDIF format it will
 * be passed through unchanged.  Only legacy OST objects in "group 0"
 * will be mapped into the IDIF namespace so that they can fit into the
 * struct lu_fid fields without loss.
 */
static inline int ostid_to_fid(struct lu_fid *fid, const struct ost_id *ostid,
			       __u32 ost_idx)
{
	__u64 seq = ostid_seq(ostid);

	if (ost_idx > 0xffff)
		return -EBADF;

	if (fid_seq_is_mdt0(seq)) {
		__u64 oid = ostid_id(ostid);

		/* This is a "legacy" (old 1.x/2.early) OST object in "group 0"
		 * that we map into the IDIF namespace.  It allows up to 2^48
		 * objects per OST, as this is the object namespace that has
		 * been in production for years.  This can handle create rates
		 * of 1M objects/s/OST for 9 years, or combinations thereof.
		 */
		if (oid >= IDIF_MAX_OID)
			return -EBADF;

		fid->f_seq = fid_idif_seq(oid, ost_idx);
		/* truncate to 32 bits by assignment */
		fid->f_oid = oid;
		/* in theory, not currently used */
		fid->f_ver = oid >> 48;
	} else if (!fid_seq_is_default(seq)) {
		/* This is either an IDIF object, which identifies objects
		 * across all OSTs, or a regular FID.  The IDIF namespace
		 * maps legacy OST objects into the FID namespace.  In both
		 * cases, we just pass the FID through, no conversion needed.
		 */
		if (ostid->oi_fid.f_ver)
			return -EBADF;

		*fid = ostid->oi_fid;
	}

	return 0;
}
#endif /* _UAPI_LUSTRE_OSTID_H_ */
