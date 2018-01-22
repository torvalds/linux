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
 * Copyright 2016 Cray Inc, all rights reserved.
 * Author: Ben Evans.
 *
 * all fid manipulation functions go here
 *
 * FIDS are globally unique within a Lustre filessytem, and are made up
 * of three parts: sequence, Object ID, and version.
 *
 */
#ifndef _UAPI_LUSTRE_FID_H_
#define _UAPI_LUSTRE_FID_H_

#include <uapi/linux/lustre/lustre_idl.h>

/** returns fid object sequence */
static inline __u64 fid_seq(const struct lu_fid *fid)
{
	return fid->f_seq;
}

/** returns fid object id */
static inline __u32 fid_oid(const struct lu_fid *fid)
{
	return fid->f_oid;
}

/** returns fid object version */
static inline __u32 fid_ver(const struct lu_fid *fid)
{
	return fid->f_ver;
}

static inline void fid_zero(struct lu_fid *fid)
{
	memset(fid, 0, sizeof(*fid));
}

static inline __u64 fid_ver_oid(const struct lu_fid *fid)
{
	return (__u64)fid_ver(fid) << 32 | fid_oid(fid);
}

static inline bool fid_seq_is_mdt0(__u64 seq)
{
	return seq == FID_SEQ_OST_MDT0;
}

static inline bool fid_seq_is_mdt(__u64 seq)
{
	return seq == FID_SEQ_OST_MDT0 || seq >= FID_SEQ_NORMAL;
};

static inline bool fid_seq_is_echo(__u64 seq)
{
	return seq == FID_SEQ_ECHO;
}

static inline bool fid_is_echo(const struct lu_fid *fid)
{
	return fid_seq_is_echo(fid_seq(fid));
}

static inline bool fid_seq_is_llog(__u64 seq)
{
	return seq == FID_SEQ_LLOG;
}

static inline bool fid_is_llog(const struct lu_fid *fid)
{
	/* file with OID == 0 is not llog but contains last oid */
	return fid_seq_is_llog(fid_seq(fid)) && fid_oid(fid) > 0;
}

static inline bool fid_seq_is_rsvd(__u64 seq)
{
	return seq > FID_SEQ_OST_MDT0 && seq <= FID_SEQ_RSVD;
};

static inline bool fid_seq_is_special(__u64 seq)
{
	return seq == FID_SEQ_SPECIAL;
};

static inline bool fid_seq_is_local_file(__u64 seq)
{
	return seq == FID_SEQ_LOCAL_FILE ||
	       seq == FID_SEQ_LOCAL_NAME;
};

static inline bool fid_seq_is_root(__u64 seq)
{
	return seq == FID_SEQ_ROOT;
}

static inline bool fid_seq_is_dot(__u64 seq)
{
	return seq == FID_SEQ_DOT_LUSTRE;
}

static inline bool fid_seq_is_default(__u64 seq)
{
	return seq == FID_SEQ_LOV_DEFAULT;
}

static inline bool fid_is_mdt0(const struct lu_fid *fid)
{
	return fid_seq_is_mdt0(fid_seq(fid));
}

/**
 * Check if a fid is igif or not.
 * \param fid the fid to be tested.
 * \return true if the fid is an igif; otherwise false.
 */
static inline bool fid_seq_is_igif(__u64 seq)
{
	return seq >= FID_SEQ_IGIF && seq <= FID_SEQ_IGIF_MAX;
}

static inline bool fid_is_igif(const struct lu_fid *fid)
{
	return fid_seq_is_igif(fid_seq(fid));
}

/**
 * Check if a fid is idif or not.
 * \param fid the fid to be tested.
 * \return true if the fid is an idif; otherwise false.
 */
static inline bool fid_seq_is_idif(__u64 seq)
{
	return seq >= FID_SEQ_IDIF && seq <= FID_SEQ_IDIF_MAX;
}

static inline bool fid_is_idif(const struct lu_fid *fid)
{
	return fid_seq_is_idif(fid_seq(fid));
}

static inline bool fid_is_local_file(const struct lu_fid *fid)
{
	return fid_seq_is_local_file(fid_seq(fid));
}

static inline bool fid_seq_is_norm(__u64 seq)
{
	return (seq >= FID_SEQ_NORMAL);
}

static inline bool fid_is_norm(const struct lu_fid *fid)
{
	return fid_seq_is_norm(fid_seq(fid));
}

/* convert an OST objid into an IDIF FID SEQ number */
static inline __u64 fid_idif_seq(__u64 id, __u32 ost_idx)
{
	return FID_SEQ_IDIF | (ost_idx << 16) | ((id >> 32) & 0xffff);
}

/* convert a packed IDIF FID into an OST objid */
static inline __u64 fid_idif_id(__u64 seq, __u32 oid, __u32 ver)
{
	return ((__u64)ver << 48) | ((seq & 0xffff) << 32) | oid;
}

static inline __u32 idif_ost_idx(__u64 seq)
{
	return (seq >> 16) & 0xffff;
}

/* extract ost index from IDIF FID */
static inline __u32 fid_idif_ost_idx(const struct lu_fid *fid)
{
	return idif_ost_idx(fid_seq(fid));
}

/**
 * Get inode number from an igif.
 * \param fid an igif to get inode number from.
 * \return inode number for the igif.
 */
static inline ino_t lu_igif_ino(const struct lu_fid *fid)
{
	return fid_seq(fid);
}

/**
 * Get inode generation from an igif.
 * \param fid an igif to get inode generation from.
 * \return inode generation for the igif.
 */
static inline __u32 lu_igif_gen(const struct lu_fid *fid)
{
	return fid_oid(fid);
}

/**
 * Build igif from the inode number/generation.
 */
static inline void lu_igif_build(struct lu_fid *fid, __u32 ino, __u32 gen)
{
	fid->f_seq = ino;
	fid->f_oid = gen;
	fid->f_ver = 0;
}

/*
 * Fids are transmitted across network (in the sender byte-ordering),
 * and stored on disk in big-endian order.
 */
static inline void fid_cpu_to_le(struct lu_fid *dst, const struct lu_fid *src)
{
	dst->f_seq = __cpu_to_le64(fid_seq(src));
	dst->f_oid = __cpu_to_le32(fid_oid(src));
	dst->f_ver = __cpu_to_le32(fid_ver(src));
}

static inline void fid_le_to_cpu(struct lu_fid *dst, const struct lu_fid *src)
{
	dst->f_seq = __le64_to_cpu(fid_seq(src));
	dst->f_oid = __le32_to_cpu(fid_oid(src));
	dst->f_ver = __le32_to_cpu(fid_ver(src));
}

static inline void fid_cpu_to_be(struct lu_fid *dst, const struct lu_fid *src)
{
	dst->f_seq = __cpu_to_be64(fid_seq(src));
	dst->f_oid = __cpu_to_be32(fid_oid(src));
	dst->f_ver = __cpu_to_be32(fid_ver(src));
}

static inline void fid_be_to_cpu(struct lu_fid *dst, const struct lu_fid *src)
{
	dst->f_seq = __be64_to_cpu(fid_seq(src));
	dst->f_oid = __be32_to_cpu(fid_oid(src));
	dst->f_ver = __be32_to_cpu(fid_ver(src));
}

static inline bool fid_is_sane(const struct lu_fid *fid)
{
	return fid && ((fid_seq(fid) >= FID_SEQ_START && !fid_ver(fid)) ||
			fid_is_igif(fid) || fid_is_idif(fid) ||
			fid_seq_is_rsvd(fid_seq(fid)));
}

static inline bool lu_fid_eq(const struct lu_fid *f0, const struct lu_fid *f1)
{
	return !memcmp(f0, f1, sizeof(*f0));
}

static inline int lu_fid_cmp(const struct lu_fid *f0,
			     const struct lu_fid *f1)
{
	if (fid_seq(f0) != fid_seq(f1))
		return fid_seq(f0) > fid_seq(f1) ? 1 : -1;

	if (fid_oid(f0) != fid_oid(f1))
		return fid_oid(f0) > fid_oid(f1) ? 1 : -1;

	if (fid_ver(f0) != fid_ver(f1))
		return fid_ver(f0) > fid_ver(f1) ? 1 : -1;

	return 0;
}
#endif
