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
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/include/lustre/lustre_idl.h
 *
 * Lustre wire protocol definitions.
 */

/** \defgroup lustreidl lustreidl
 *
 * Lustre wire protocol definitions.
 *
 * ALL structs passing over the wire should be declared here.  Structs
 * that are used in interfaces with userspace should go in lustre_user.h.
 *
 * All structs being declared here should be built from simple fixed-size
 * types (__u8, __u16, __u32, __u64) or be built from other types or
 * structs also declared in this file.  Similarly, all flags and magic
 * values in those structs should also be declared here.  This ensures
 * that the Lustre wire protocol is not influenced by external dependencies.
 *
 * The only other acceptable items in this file are VERY SIMPLE accessor
 * functions to avoid callers grubbing inside the structures, and the
 * prototypes of the swabber functions for each struct.  Nothing that
 * depends on external functions or definitions should be in here.
 *
 * Structs must be properly aligned to put 64-bit values on an 8-byte
 * boundary.  Any structs being added here must also be added to
 * utils/wirecheck.c and "make newwiretest" run to regenerate the
 * utils/wiretest.c sources.  This allows us to verify that wire structs
 * have the proper alignment/size on all architectures.
 *
 * DO NOT CHANGE any of the structs, flags, values declared here and used
 * in released Lustre versions.  Some structs may have padding fields that
 * can be used.  Some structs might allow addition at the end (verify this
 * in the code to ensure that new/old clients that see this larger struct
 * do not fail, otherwise you need to implement protocol compatibility).
 *
 * We assume all nodes are either little-endian or big-endian, and we
 * always send messages in the sender's native format.  The receiver
 * detects the message format by checking the 'magic' field of the message
 * (see lustre_msg_swabbed() below).
 *
 * Each wire type has corresponding 'lustre_swab_xxxtypexxx()' routines,
 * implemented either here, inline (trivial implementations) or in
 * ptlrpc/pack_generic.c.  These 'swabbers' convert the type from "other"
 * endian, in-place in the message buffer.
 *
 * A swabber takes a single pointer argument.  The caller must already have
 * verified that the length of the message buffer >= sizeof (type).
 *
 * For variable length types, a second 'lustre_swab_v_xxxtypexxx()' routine
 * may be defined that swabs just the variable part, after the caller has
 * verified that the message buffer is large enough.
 *
 * @{
 */

#ifndef _LUSTRE_IDL_H_
#define _LUSTRE_IDL_H_

#include "../../../include/linux/libcfs/libcfs.h"
#include "../../../include/linux/lnet/types.h"

/* Defn's shared with user-space. */
#include "lustre_user.h"
#include "lustre_errno.h"
#include "../lustre_ver.h"

/*
 *  GENERAL STUFF
 */
/* FOO_REQUEST_PORTAL is for incoming requests on the FOO
 * FOO_REPLY_PORTAL   is for incoming replies on the FOO
 * FOO_BULK_PORTAL    is for incoming bulk on the FOO
 */

/* Lustre service names are following the format
 * service name + MDT + seq name
 */
#define LUSTRE_MDT_MAXNAMELEN	80

#define CONNMGR_REQUEST_PORTAL	  1
#define CONNMGR_REPLY_PORTAL	    2
/*#define OSC_REQUEST_PORTAL	    3 */
#define OSC_REPLY_PORTAL		4
/*#define OSC_BULK_PORTAL	       5 */
#define OST_IO_PORTAL		   6
#define OST_CREATE_PORTAL	       7
#define OST_BULK_PORTAL		 8
/*#define MDC_REQUEST_PORTAL	    9 */
#define MDC_REPLY_PORTAL	       10
/*#define MDC_BULK_PORTAL	      11 */
#define MDS_REQUEST_PORTAL	     12
/*#define MDS_REPLY_PORTAL	     13 */
#define MDS_BULK_PORTAL		14
#define LDLM_CB_REQUEST_PORTAL	 15
#define LDLM_CB_REPLY_PORTAL	   16
#define LDLM_CANCEL_REQUEST_PORTAL     17
#define LDLM_CANCEL_REPLY_PORTAL       18
/*#define PTLBD_REQUEST_PORTAL	   19 */
/*#define PTLBD_REPLY_PORTAL	     20 */
/*#define PTLBD_BULK_PORTAL	      21 */
#define MDS_SETATTR_PORTAL	     22
#define MDS_READPAGE_PORTAL	    23
#define OUT_PORTAL		    24

#define MGC_REPLY_PORTAL	       25
#define MGS_REQUEST_PORTAL	     26
#define MGS_REPLY_PORTAL	       27
#define OST_REQUEST_PORTAL	     28
#define FLD_REQUEST_PORTAL	     29
#define SEQ_METADATA_PORTAL	    30
#define SEQ_DATA_PORTAL		31
#define SEQ_CONTROLLER_PORTAL	  32
#define MGS_BULK_PORTAL		33

/* Portal 63 is reserved for the Cray Inc DVS - nic@cray.com, roe@cray.com,
 *						n8851@cray.com
 */

/* packet types */
#define PTL_RPC_MSG_REQUEST 4711
#define PTL_RPC_MSG_ERR     4712
#define PTL_RPC_MSG_REPLY   4713

/* DON'T use swabbed values of MAGIC as magic! */
#define LUSTRE_MSG_MAGIC_V2 0x0BD00BD3
#define LUSTRE_MSG_MAGIC_V2_SWABBED 0xD30BD00B

#define LUSTRE_MSG_MAGIC LUSTRE_MSG_MAGIC_V2

#define PTLRPC_MSG_VERSION  0x00000003
#define LUSTRE_VERSION_MASK 0xffff0000
#define LUSTRE_OBD_VERSION  0x00010000
#define LUSTRE_MDS_VERSION  0x00020000
#define LUSTRE_OST_VERSION  0x00030000
#define LUSTRE_DLM_VERSION  0x00040000
#define LUSTRE_LOG_VERSION  0x00050000
#define LUSTRE_MGS_VERSION  0x00060000

/**
 * Describes a range of sequence, lsr_start is included but lsr_end is
 * not in the range.
 * Same structure is used in fld module where lsr_index field holds mdt id
 * of the home mdt.
 */
struct lu_seq_range {
	__u64 lsr_start;
	__u64 lsr_end;
	__u32 lsr_index;
	__u32 lsr_flags;
};

struct lu_seq_range_array {
	__u32 lsra_count;
	__u32 lsra_padding;
	struct lu_seq_range lsra_lsr[0];
};

#define LU_SEQ_RANGE_MDT	0x0
#define LU_SEQ_RANGE_OST	0x1
#define LU_SEQ_RANGE_ANY	0x3

#define LU_SEQ_RANGE_MASK	0x3

static inline unsigned fld_range_type(const struct lu_seq_range *range)
{
	return range->lsr_flags & LU_SEQ_RANGE_MASK;
}

static inline bool fld_range_is_ost(const struct lu_seq_range *range)
{
	return fld_range_type(range) == LU_SEQ_RANGE_OST;
}

static inline bool fld_range_is_mdt(const struct lu_seq_range *range)
{
	return fld_range_type(range) == LU_SEQ_RANGE_MDT;
}

/**
 * This all range is only being used when fld client sends fld query request,
 * but it does not know whether the seq is MDT or OST, so it will send req
 * with ALL type, which means either seq type gotten from lookup can be
 * expected.
 */
static inline unsigned fld_range_is_any(const struct lu_seq_range *range)
{
	return fld_range_type(range) == LU_SEQ_RANGE_ANY;
}

static inline void fld_range_set_type(struct lu_seq_range *range,
				      unsigned flags)
{
	range->lsr_flags |= flags;
}

static inline void fld_range_set_mdt(struct lu_seq_range *range)
{
	fld_range_set_type(range, LU_SEQ_RANGE_MDT);
}

static inline void fld_range_set_ost(struct lu_seq_range *range)
{
	fld_range_set_type(range, LU_SEQ_RANGE_OST);
}

static inline void fld_range_set_any(struct lu_seq_range *range)
{
	fld_range_set_type(range, LU_SEQ_RANGE_ANY);
}

/**
 * returns  width of given range \a r
 */

static inline __u64 range_space(const struct lu_seq_range *range)
{
	return range->lsr_end - range->lsr_start;
}

/**
 * initialize range to zero
 */

static inline void range_init(struct lu_seq_range *range)
{
	memset(range, 0, sizeof(*range));
}

/**
 * check if given seq id \a s is within given range \a r
 */

static inline bool range_within(const struct lu_seq_range *range,
				__u64 s)
{
	return s >= range->lsr_start && s < range->lsr_end;
}

static inline bool range_is_sane(const struct lu_seq_range *range)
{
	return (range->lsr_end >= range->lsr_start);
}

static inline bool range_is_zero(const struct lu_seq_range *range)
{
	return (range->lsr_start == 0 && range->lsr_end == 0);
}

static inline bool range_is_exhausted(const struct lu_seq_range *range)

{
	return range_space(range) == 0;
}

/* return 0 if two range have the same location */
static inline int range_compare_loc(const struct lu_seq_range *r1,
				    const struct lu_seq_range *r2)
{
	return r1->lsr_index != r2->lsr_index ||
	       r1->lsr_flags != r2->lsr_flags;
}

#define DRANGE "[%#16.16Lx-%#16.16Lx):%x:%s"

#define PRANGE(range)		\
	(range)->lsr_start,	\
	(range)->lsr_end,	\
	(range)->lsr_index,	\
	fld_range_is_mdt(range) ? "mdt" : "ost"

/** \defgroup lu_fid lu_fid
 * @{
 */

/**
 * Flags for lustre_mdt_attrs::lma_compat and lustre_mdt_attrs::lma_incompat.
 * Deprecated since HSM and SOM attributes are now stored in separate on-disk
 * xattr.
 */
enum lma_compat {
	LMAC_HSM	= 0x00000001,
	LMAC_SOM	= 0x00000002,
	LMAC_NOT_IN_OI	= 0x00000004, /* the object does NOT need OI mapping */
	LMAC_FID_ON_OST = 0x00000008, /* For OST-object, its OI mapping is
				       * under /O/<seq>/d<x>.
				       */
};

/**
 * Masks for all features that should be supported by a Lustre version to
 * access a specific file.
 * This information is stored in lustre_mdt_attrs::lma_incompat.
 */
enum lma_incompat {
	LMAI_RELEASED		= 0x00000001, /* file is released */
	LMAI_AGENT		= 0x00000002, /* agent inode */
	LMAI_REMOTE_PARENT	= 0x00000004, /* the parent of the object
					       * is on the remote MDT
					       */
};

#define LMA_INCOMPAT_SUPP	(LMAI_AGENT | LMAI_REMOTE_PARENT)

/**
 * fid constants
 */
enum {
	/** LASTID file has zero OID */
	LUSTRE_FID_LASTID_OID = 0UL,
	/** initial fid id value */
	LUSTRE_FID_INIT_OID  = 1UL
};

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
	return ((__u64)fid_ver(fid) << 32 | fid_oid(fid));
}

/* copytool uses a 32b bitmask field to encode archive-Ids during register
 * with MDT thru kuc.
 * archive num = 0 => all
 * archive num from 1 to 32
 */
#define LL_HSM_MAX_ARCHIVE (sizeof(__u32) * 8)

/**
 * Note that reserved SEQ numbers below 12 will conflict with ldiskfs
 * inodes in the IGIF namespace, so these reserved SEQ numbers can be
 * used for other purposes and not risk collisions with existing inodes.
 *
 * Different FID Format
 * http://wiki.old.lustre.org/index.php/Architecture_-_Interoperability_fids_zfs
 */
enum fid_seq {
	FID_SEQ_OST_MDT0	= 0,
	FID_SEQ_LLOG		= 1, /* unnamed llogs */
	FID_SEQ_ECHO		= 2,
	FID_SEQ_OST_MDT1	= 3,
	FID_SEQ_OST_MAX		= 9, /* Max MDT count before OST_on_FID */
	FID_SEQ_LLOG_NAME	= 10, /* named llogs */
	FID_SEQ_RSVD		= 11,
	FID_SEQ_IGIF		= 12,
	FID_SEQ_IGIF_MAX	= 0x0ffffffffULL,
	FID_SEQ_IDIF		= 0x100000000ULL,
	FID_SEQ_IDIF_MAX	= 0x1ffffffffULL,
	/* Normal FID sequence starts from this value, i.e. 1<<33 */
	FID_SEQ_START		= 0x200000000ULL,
	/* sequence for local pre-defined FIDs listed in local_oid */
	FID_SEQ_LOCAL_FILE	= 0x200000001ULL,
	FID_SEQ_DOT_LUSTRE	= 0x200000002ULL,
	/* sequence is used for local named objects FIDs generated
	 * by local_object_storage library
	 */
	FID_SEQ_LOCAL_NAME	= 0x200000003ULL,
	/* Because current FLD will only cache the fid sequence, instead
	 * of oid on the client side, if the FID needs to be exposed to
	 * clients sides, it needs to make sure all of fids under one
	 * sequence will be located in one MDT.
	 */
	FID_SEQ_SPECIAL		= 0x200000004ULL,
	FID_SEQ_QUOTA		= 0x200000005ULL,
	FID_SEQ_QUOTA_GLB	= 0x200000006ULL,
	FID_SEQ_ROOT		= 0x200000007ULL,  /* Located on MDT0 */
	FID_SEQ_NORMAL		= 0x200000400ULL,
	FID_SEQ_LOV_DEFAULT	= 0xffffffffffffffffULL
};

#define OBIF_OID_MAX_BITS	   32
#define OBIF_MAX_OID		(1ULL << OBIF_OID_MAX_BITS)
#define OBIF_OID_MASK	       ((1ULL << OBIF_OID_MAX_BITS) - 1)
#define IDIF_OID_MAX_BITS	   48
#define IDIF_MAX_OID		(1ULL << IDIF_OID_MAX_BITS)
#define IDIF_OID_MASK	       ((1ULL << IDIF_OID_MAX_BITS) - 1)

/** OID for FID_SEQ_SPECIAL */
enum special_oid {
	/* Big Filesystem Lock to serialize rename operations */
	FID_OID_SPECIAL_BFL     = 1UL,
};

/** OID for FID_SEQ_DOT_LUSTRE */
enum dot_lustre_oid {
	FID_OID_DOT_LUSTRE  = 1UL,
	FID_OID_DOT_LUSTRE_OBF = 2UL,
};

static inline bool fid_seq_is_mdt0(__u64 seq)
{
	return (seq == FID_SEQ_OST_MDT0);
}

static inline bool fid_seq_is_mdt(__u64 seq)
{
	return seq == FID_SEQ_OST_MDT0 || seq >= FID_SEQ_NORMAL;
};

static inline bool fid_seq_is_echo(__u64 seq)
{
	return (seq == FID_SEQ_ECHO);
}

static inline bool fid_is_echo(const struct lu_fid *fid)
{
	return fid_seq_is_echo(fid_seq(fid));
}

static inline bool fid_seq_is_llog(__u64 seq)
{
	return (seq == FID_SEQ_LLOG);
}

static inline bool fid_is_llog(const struct lu_fid *fid)
{
	/* file with OID == 0 is not llog but contains last oid */
	return fid_seq_is_llog(fid_seq(fid)) && fid_oid(fid) > 0;
}

static inline bool fid_seq_is_rsvd(__u64 seq)
{
	return (seq > FID_SEQ_OST_MDT0 && seq <= FID_SEQ_RSVD);
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

static inline void lu_root_fid(struct lu_fid *fid)
{
	fid->f_seq = FID_SEQ_ROOT;
	fid->f_oid = 1;
	fid->f_ver = 0;
}

/**
 * Check if a fid is igif or not.
 * \param fid the fid to be tested.
 * \return true if the fid is a igif; otherwise false.
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
 * \return true if the fid is a idif; otherwise false.
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

/* extract ost index from IDIF FID */
static inline __u32 fid_idif_ost_idx(const struct lu_fid *fid)
{
	return (fid_seq(fid) >> 16) & 0xffff;
}

/* extract OST sequence (group) from a wire ost_id (id/seq) pair */
static inline __u64 ostid_seq(const struct ost_id *ostid)
{
	if (fid_seq_is_mdt0(ostid->oi.oi_seq))
		return FID_SEQ_OST_MDT0;

	if (unlikely(fid_seq_is_default(ostid->oi.oi_seq)))
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

	if (unlikely(fid_seq_is_default(ostid->oi.oi_seq)))
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
		/* Note: if f_oid + f_ver is zero, we need init it
		 * to be 1, otherwise, ostid_seq will treat this
		 * as old ostid (oi_seq == 0)
		 */
		if (oi->oi_fid.f_oid == 0 && oi->oi_fid.f_ver == 0)
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

/**
 * Note: we need check oi_seq to decide where to set oi_id,
 * so oi_seq should always be set ahead of oi_id.
 */
static inline void ostid_set_id(struct ost_id *oi, __u64 oid)
{
	if (fid_seq_is_mdt0(oi->oi.oi_seq)) {
		if (oid >= IDIF_MAX_OID) {
			CERROR("Bad %llu to set " DOSTID "\n", oid, POSTID(oi));
			return;
		}
		oi->oi.oi_id = oid;
	} else if (fid_is_idif(&oi->oi_fid)) {
		if (oid >= IDIF_MAX_OID) {
			CERROR("Bad %llu to set "DOSTID"\n",
			       oid, POSTID(oi));
			return;
		}
		oi->oi_fid.f_seq = fid_idif_seq(oid,
						fid_idif_ost_idx(&oi->oi_fid));
		oi->oi_fid.f_oid = oid;
		oi->oi_fid.f_ver = oid >> 48;
	} else {
		if (oid > OBIF_MAX_OID) {
			CERROR("Bad %llu to set " DOSTID "\n", oid, POSTID(oi));
			return;
		}
		oi->oi_fid.f_oid = oid;
	}
}

static inline int fid_set_id(struct lu_fid *fid, __u64 oid)
{
	if (unlikely(fid_seq_is_igif(fid->f_seq))) {
		CERROR("bad IGIF, "DFID"\n", PFID(fid));
		return -EBADF;
	}

	if (fid_is_idif(fid)) {
		if (oid >= IDIF_MAX_OID) {
			CERROR("Too large OID %#llx to set IDIF "DFID"\n",
			       (unsigned long long)oid, PFID(fid));
			return -EBADF;
		}
		fid->f_seq = fid_idif_seq(oid, fid_idif_ost_idx(fid));
		fid->f_oid = oid;
		fid->f_ver = oid >> 48;
	} else {
		if (oid > OBIF_MAX_OID) {
			CERROR("Too large OID %#llx to set REG "DFID"\n",
			       (unsigned long long)oid, PFID(fid));
			return -EBADF;
		}
		fid->f_oid = oid;
	}
	return 0;
}

/**
 * Unpack an OST object id/seq (group) into a FID.  This is needed for
 * converting all obdo, lmm, lsm, etc. 64-bit id/seq pairs into proper
 * FIDs.  Note that if an id/seq is already in FID/IDIF format it will
 * be passed through unchanged.  Only legacy OST objects in "group 0"
 * will be mapped into the IDIF namespace so that they can fit into the
 * struct lu_fid fields without loss.  For reference see:
 * http://wiki.old.lustre.org/index.php/Architecture_-_Interoperability_fids_zfs
 */
static inline int ostid_to_fid(struct lu_fid *fid, struct ost_id *ostid,
			       __u32 ost_idx)
{
	__u64 seq = ostid_seq(ostid);

	if (ost_idx > 0xffff) {
		CERROR("bad ost_idx, "DOSTID" ost_idx:%u\n", POSTID(ostid),
		       ost_idx);
		return -EBADF;
	}

	if (fid_seq_is_mdt0(seq)) {
		__u64 oid = ostid_id(ostid);

		/* This is a "legacy" (old 1.x/2.early) OST object in "group 0"
		 * that we map into the IDIF namespace.  It allows up to 2^48
		 * objects per OST, as this is the object namespace that has
		 * been in production for years.  This can handle create rates
		 * of 1M objects/s/OST for 9 years, or combinations thereof.
		 */
		if (oid >= IDIF_MAX_OID) {
			CERROR("bad MDT0 id, " DOSTID " ost_idx:%u\n",
			       POSTID(ostid), ost_idx);
			return -EBADF;
		}
		fid->f_seq = fid_idif_seq(oid, ost_idx);
		/* truncate to 32 bits by assignment */
		fid->f_oid = oid;
		/* in theory, not currently used */
		fid->f_ver = oid >> 48;
	} else if (likely(!fid_seq_is_default(seq))) {
	       /* This is either an IDIF object, which identifies objects across
		* all OSTs, or a regular FID.  The IDIF namespace maps legacy
		* OST objects into the FID namespace.  In both cases, we just
		* pass the FID through, no conversion needed.
		*/
		if (ostid->oi_fid.f_ver != 0) {
			CERROR("bad MDT0 id, " DOSTID " ost_idx:%u\n",
			       POSTID(ostid), ost_idx);
			return -EBADF;
		}
		*fid = ostid->oi_fid;
	}

	return 0;
}

/* pack any OST FID into an ostid (id/seq) for the wire/disk */
static inline int fid_to_ostid(const struct lu_fid *fid, struct ost_id *ostid)
{
	if (unlikely(fid_seq_is_igif(fid->f_seq))) {
		CERROR("bad IGIF, "DFID"\n", PFID(fid));
		return -EBADF;
	}

	if (fid_is_idif(fid)) {
		ostid_set_seq_mdt0(ostid);
		ostid_set_id(ostid, fid_idif_id(fid_seq(fid), fid_oid(fid),
						fid_ver(fid)));
	} else {
		ostid->oi_fid = *fid;
	}

	return 0;
}

/* Check whether the fid is for LAST_ID */
static inline bool fid_is_last_id(const struct lu_fid *fid)
{
	return (fid_oid(fid) == 0);
}

/**
 * Get inode number from a igif.
 * \param fid a igif to get inode number from.
 * \return inode number for the igif.
 */
static inline ino_t lu_igif_ino(const struct lu_fid *fid)
{
	return fid_seq(fid);
}

void lustre_swab_ost_id(struct ost_id *oid);

/**
 * Get inode generation from a igif.
 * \param fid a igif to get inode generation from.
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
	dst->f_seq = cpu_to_le64(fid_seq(src));
	dst->f_oid = cpu_to_le32(fid_oid(src));
	dst->f_ver = cpu_to_le32(fid_ver(src));
}

static inline void fid_le_to_cpu(struct lu_fid *dst, const struct lu_fid *src)
{
	dst->f_seq = le64_to_cpu(fid_seq(src));
	dst->f_oid = le32_to_cpu(fid_oid(src));
	dst->f_ver = le32_to_cpu(fid_ver(src));
}

static inline void fid_cpu_to_be(struct lu_fid *dst, const struct lu_fid *src)
{
	dst->f_seq = cpu_to_be64(fid_seq(src));
	dst->f_oid = cpu_to_be32(fid_oid(src));
	dst->f_ver = cpu_to_be32(fid_ver(src));
}

static inline void fid_be_to_cpu(struct lu_fid *dst, const struct lu_fid *src)
{
	dst->f_seq = be64_to_cpu(fid_seq(src));
	dst->f_oid = be32_to_cpu(fid_oid(src));
	dst->f_ver = be32_to_cpu(fid_ver(src));
}

static inline bool fid_is_sane(const struct lu_fid *fid)
{
	return fid &&
	       ((fid_seq(fid) >= FID_SEQ_START && fid_ver(fid) == 0) ||
		fid_is_igif(fid) || fid_is_idif(fid) ||
		fid_seq_is_rsvd(fid_seq(fid)));
}

void lustre_swab_lu_fid(struct lu_fid *fid);
void lustre_swab_lu_seq_range(struct lu_seq_range *range);

static inline bool lu_fid_eq(const struct lu_fid *f0, const struct lu_fid *f1)
{
	return memcmp(f0, f1, sizeof(*f0)) == 0;
}

#define __diff_normalize(val0, val1)			    \
({							      \
	typeof(val0) __val0 = (val0);			   \
	typeof(val1) __val1 = (val1);			   \
								\
	(__val0 == __val1 ? 0 : __val0 > __val1 ? 1 : -1);     \
})

static inline int lu_fid_cmp(const struct lu_fid *f0,
			     const struct lu_fid *f1)
{
	return
		__diff_normalize(fid_seq(f0), fid_seq(f1)) ?:
		__diff_normalize(fid_oid(f0), fid_oid(f1)) ?:
		__diff_normalize(fid_ver(f0), fid_ver(f1));
}

static inline void ostid_cpu_to_le(const struct ost_id *src_oi,
				   struct ost_id *dst_oi)
{
	if (fid_seq_is_mdt0(ostid_seq(src_oi))) {
		dst_oi->oi.oi_id = cpu_to_le64(src_oi->oi.oi_id);
		dst_oi->oi.oi_seq = cpu_to_le64(src_oi->oi.oi_seq);
	} else {
		fid_cpu_to_le(&dst_oi->oi_fid, &src_oi->oi_fid);
	}
}

static inline void ostid_le_to_cpu(const struct ost_id *src_oi,
				   struct ost_id *dst_oi)
{
	if (fid_seq_is_mdt0(ostid_seq(src_oi))) {
		dst_oi->oi.oi_id = le64_to_cpu(src_oi->oi.oi_id);
		dst_oi->oi.oi_seq = le64_to_cpu(src_oi->oi.oi_seq);
	} else {
		fid_le_to_cpu(&dst_oi->oi_fid, &src_oi->oi_fid);
	}
}

/** @} lu_fid */

/** \defgroup lu_dir lu_dir
 * @{
 */

/**
 * Enumeration of possible directory entry attributes.
 *
 * Attributes follow directory entry header in the order they appear in this
 * enumeration.
 */
enum lu_dirent_attrs {
	LUDA_FID		= 0x0001,
	LUDA_TYPE		= 0x0002,
	LUDA_64BITHASH		= 0x0004,
};

/**
 * Layout of readdir pages, as transmitted on wire.
 */
struct lu_dirent {
	/** valid if LUDA_FID is set. */
	struct lu_fid lde_fid;
	/** a unique entry identifier: a hash or an offset. */
	__u64	 lde_hash;
	/** total record length, including all attributes. */
	__u16	 lde_reclen;
	/** name length */
	__u16	 lde_namelen;
	/** optional variable size attributes following this entry.
	 *  taken from enum lu_dirent_attrs.
	 */
	__u32	 lde_attrs;
	/** name is followed by the attributes indicated in ->ldp_attrs, in
	 *  their natural order. After the last attribute, padding bytes are
	 *  added to make ->lde_reclen a multiple of 8.
	 */
	char	  lde_name[0];
};

/*
 * Definitions of optional directory entry attributes formats.
 *
 * Individual attributes do not have their length encoded in a generic way. It
 * is assumed that consumer of an attribute knows its format. This means that
 * it is impossible to skip over an unknown attribute, except by skipping over all
 * remaining attributes (by using ->lde_reclen), which is not too
 * constraining, because new server versions will append new attributes at
 * the end of an entry.
 */

/**
 * Fid directory attribute: a fid of an object referenced by the entry. This
 * will be almost always requested by the client and supplied by the server.
 *
 * Aligned to 8 bytes.
 */
/* To have compatibility with 1.8, lets have fid in lu_dirent struct. */

/**
 * File type.
 *
 * Aligned to 2 bytes.
 */
struct luda_type {
	__u16 lt_type;
};

#ifndef IFSHIFT
#define IFSHIFT                 12
#endif

#ifndef IFTODT
#define IFTODT(type)		(((type) & S_IFMT) >> IFSHIFT)
#endif
#ifndef DTTOIF
#define DTTOIF(dirtype)		((dirtype) << IFSHIFT)
#endif

struct lu_dirpage {
	__u64	    ldp_hash_start;
	__u64	    ldp_hash_end;
	__u32	    ldp_flags;
	__u32	    ldp_pad0;
	struct lu_dirent ldp_entries[0];
};

enum lu_dirpage_flags {
	/**
	 * dirpage contains no entry.
	 */
	LDF_EMPTY   = 1 << 0,
	/**
	 * last entry's lde_hash equals ldp_hash_end.
	 */
	LDF_COLLIDE = 1 << 1
};

static inline struct lu_dirent *lu_dirent_start(struct lu_dirpage *dp)
{
	if (le32_to_cpu(dp->ldp_flags) & LDF_EMPTY)
		return NULL;
	else
		return dp->ldp_entries;
}

static inline struct lu_dirent *lu_dirent_next(struct lu_dirent *ent)
{
	struct lu_dirent *next;

	if (le16_to_cpu(ent->lde_reclen) != 0)
		next = ((void *)ent) + le16_to_cpu(ent->lde_reclen);
	else
		next = NULL;

	return next;
}

static inline int lu_dirent_calc_size(int namelen, __u16 attr)
{
	int size;

	if (attr & LUDA_TYPE) {
		const unsigned align = sizeof(struct luda_type) - 1;

		size = (sizeof(struct lu_dirent) + namelen + align) & ~align;
		size += sizeof(struct luda_type);
	} else {
		size = sizeof(struct lu_dirent) + namelen;
	}

	return (size + 7) & ~7;
}

static inline int lu_dirent_size(const struct lu_dirent *ent)
{
	if (le16_to_cpu(ent->lde_reclen) == 0) {
		return lu_dirent_calc_size(le16_to_cpu(ent->lde_namelen),
					   le32_to_cpu(ent->lde_attrs));
	}
	return le16_to_cpu(ent->lde_reclen);
}

#define MDS_DIR_END_OFF 0xfffffffffffffffeULL

/**
 * MDS_READPAGE page size
 *
 * This is the directory page size packed in MDS_READPAGE RPC.
 * It's different than PAGE_SIZE because the client needs to
 * access the struct lu_dirpage header packed at the beginning of
 * the "page" and without this there isn't any way to know find the
 * lu_dirpage header is if client and server PAGE_SIZE differ.
 */
#define LU_PAGE_SHIFT 12
#define LU_PAGE_SIZE  (1UL << LU_PAGE_SHIFT)
#define LU_PAGE_MASK  (~(LU_PAGE_SIZE - 1))

#define LU_PAGE_COUNT (1 << (PAGE_SHIFT - LU_PAGE_SHIFT))

/** @} lu_dir */

struct lustre_handle {
	__u64 cookie;
};

#define DEAD_HANDLE_MAGIC 0xdeadbeefcafebabeULL

static inline bool lustre_handle_is_used(const struct lustre_handle *lh)
{
	return lh->cookie != 0ull;
}

static inline bool lustre_handle_equal(const struct lustre_handle *lh1,
				       const struct lustre_handle *lh2)
{
	return lh1->cookie == lh2->cookie;
}

static inline void lustre_handle_copy(struct lustre_handle *tgt,
				      const struct lustre_handle *src)
{
	tgt->cookie = src->cookie;
}

/* flags for lm_flags */
#define MSGHDR_AT_SUPPORT	       0x1
#define MSGHDR_CKSUM_INCOMPAT18	 0x2

#define lustre_msg lustre_msg_v2
/* we depend on this structure to be 8-byte aligned */
/* this type is only endian-adjusted in lustre_unpack_msg() */
struct lustre_msg_v2 {
	__u32 lm_bufcount;
	__u32 lm_secflvr;
	__u32 lm_magic;
	__u32 lm_repsize;
	__u32 lm_cksum;
	__u32 lm_flags;
	__u32 lm_padding_2;
	__u32 lm_padding_3;
	__u32 lm_buflens[0];
};

/* without gss, ptlrpc_body is put at the first buffer. */
#define PTLRPC_NUM_VERSIONS     4
#define JOBSTATS_JOBID_SIZE     32  /* 32 bytes string */
struct ptlrpc_body_v3 {
	struct lustre_handle pb_handle;
	__u32 pb_type;
	__u32 pb_version;
	__u32 pb_opc;
	__u32 pb_status;
	__u64 pb_last_xid;
	__u64 pb_last_seen;
	__u64 pb_last_committed;
	__u64 pb_transno;
	__u32 pb_flags;
	__u32 pb_op_flags;
	__u32 pb_conn_cnt;
	__u32 pb_timeout;  /* for req, the deadline, for rep, the service est */
	__u32 pb_service_time; /* for rep, actual service time */
	__u32 pb_limit;
	__u64 pb_slv;
	/* VBR: pre-versions */
	__u64 pb_pre_versions[PTLRPC_NUM_VERSIONS];
	/* padding for future needs */
	__u64 pb_padding[4];
	char  pb_jobid[JOBSTATS_JOBID_SIZE];
};

#define ptlrpc_body     ptlrpc_body_v3

struct ptlrpc_body_v2 {
	struct lustre_handle pb_handle;
	__u32 pb_type;
	__u32 pb_version;
	__u32 pb_opc;
	__u32 pb_status;
	__u64 pb_last_xid;
	__u64 pb_last_seen;
	__u64 pb_last_committed;
	__u64 pb_transno;
	__u32 pb_flags;
	__u32 pb_op_flags;
	__u32 pb_conn_cnt;
	__u32 pb_timeout;  /* for req, the deadline, for rep, the service est */
	__u32 pb_service_time; /* for rep, actual service time, also used for
				* net_latency of req
				*/
	__u32 pb_limit;
	__u64 pb_slv;
	/* VBR: pre-versions */
	__u64 pb_pre_versions[PTLRPC_NUM_VERSIONS];
	/* padding for future needs */
	__u64 pb_padding[4];
};

void lustre_swab_ptlrpc_body(struct ptlrpc_body *pb);

/* message body offset for lustre_msg_v2 */
/* ptlrpc body offset in all request/reply messages */
#define MSG_PTLRPC_BODY_OFF	     0

/* normal request/reply message record offset */
#define REQ_REC_OFF		     1
#define REPLY_REC_OFF		   1

/* ldlm request message body offset */
#define DLM_LOCKREQ_OFF		 1 /* lockreq offset */
#define DLM_REQ_REC_OFF		 2 /* normal dlm request record offset */

/* ldlm intent lock message body offset */
#define DLM_INTENT_IT_OFF	       2 /* intent lock it offset */
#define DLM_INTENT_REC_OFF	      3 /* intent lock record offset */

/* ldlm reply message body offset */
#define DLM_LOCKREPLY_OFF	       1 /* lockrep offset */
#define DLM_REPLY_REC_OFF	       2 /* reply record offset */

/** only use in req->rq_{req,rep}_swab_mask */
#define MSG_PTLRPC_HEADER_OFF	   31

/* Flags that are operation-specific go in the top 16 bits. */
#define MSG_OP_FLAG_MASK   0xffff0000
#define MSG_OP_FLAG_SHIFT  16

/* Flags that apply to all requests are in the bottom 16 bits */
#define MSG_GEN_FLAG_MASK     0x0000ffff
#define MSG_LAST_REPLAY	   0x0001
#define MSG_RESENT		0x0002
#define MSG_REPLAY		0x0004
/* #define MSG_AT_SUPPORT	 0x0008
 * This was used in early prototypes of adaptive timeouts, and while there
 * shouldn't be any users of that code there also isn't a need for using this
 * bits. Defer usage until at least 1.10 to avoid potential conflict.
 */
#define MSG_DELAY_REPLAY	  0x0010
#define MSG_VERSION_REPLAY	0x0020
#define MSG_REQ_REPLAY_DONE       0x0040
#define MSG_LOCK_REPLAY_DONE      0x0080

/*
 * Flags for all connect opcodes (MDS_CONNECT, OST_CONNECT)
 */

#define MSG_CONNECT_RECOVERING  0x00000001
#define MSG_CONNECT_RECONNECT   0x00000002
#define MSG_CONNECT_REPLAYABLE  0x00000004
/*#define MSG_CONNECT_PEER	0x8 */
#define MSG_CONNECT_LIBCLIENT   0x00000010
#define MSG_CONNECT_INITIAL     0x00000020
#define MSG_CONNECT_ASYNC       0x00000040
#define MSG_CONNECT_NEXT_VER    0x00000080 /* use next version of lustre_msg */
#define MSG_CONNECT_TRANSNO     0x00000100 /* report transno */

/* Connect flags */
#define OBD_CONNECT_RDONLY		  0x1ULL /*client has read-only access*/
#define OBD_CONNECT_INDEX		  0x2ULL /*connect specific LOV idx */
#define OBD_CONNECT_MDS			  0x4ULL /*connect from MDT to OST */
#define OBD_CONNECT_GRANT		  0x8ULL /*OSC gets grant at connect */
#define OBD_CONNECT_SRVLOCK		 0x10ULL /*server takes locks for cli */
#define OBD_CONNECT_VERSION		 0x20ULL /*Lustre versions in ocd */
#define OBD_CONNECT_REQPORTAL		 0x40ULL /*Separate non-IO req portal */
#define OBD_CONNECT_ACL			 0x80ULL /*access control lists */
#define OBD_CONNECT_XATTR		0x100ULL /*client use extended attr */
#define OBD_CONNECT_CROW		0x200ULL /*MDS+OST create obj on write*/
#define OBD_CONNECT_TRUNCLOCK		0x400ULL /*locks on server for punch */
#define OBD_CONNECT_TRANSNO		0x800ULL /*replay sends init transno */
#define OBD_CONNECT_IBITS	       0x1000ULL /*support for inodebits locks*/
#define OBD_CONNECT_JOIN	       0x2000ULL /*files can be concatenated.
						  *We do not support JOIN FILE
						  *anymore, reserve this flags
						  *just for preventing such bit
						  *to be reused.
						  */
#define OBD_CONNECT_ATTRFID	       0x4000ULL /*Server can GetAttr By Fid*/
#define OBD_CONNECT_NODEVOH	       0x8000ULL /*No open hndl on specl nodes*/
#define OBD_CONNECT_RMT_CLIENT	      0x10000ULL /* Remote client, never used
						  * in production. Removed in
						  * 2.9. Keep this flag to
						  * avoid reuse.
						  */
#define OBD_CONNECT_RMT_CLIENT_FORCE  0x20000ULL /* Remote client by force,
						  * never used in production.
						  * Removed in 2.9. Keep this
						  * flag to avoid reuse
						  */
#define OBD_CONNECT_BRW_SIZE	      0x40000ULL /*Max bytes per rpc */
#define OBD_CONNECT_QUOTA64	      0x80000ULL /*Not used since 2.4 */
#define OBD_CONNECT_MDS_CAPA	     0x100000ULL /*MDS capability */
#define OBD_CONNECT_OSS_CAPA	     0x200000ULL /*OSS capability */
#define OBD_CONNECT_CANCELSET	     0x400000ULL /*Early batched cancels. */
#define OBD_CONNECT_SOM		     0x800000ULL /*Size on MDS */
#define OBD_CONNECT_AT		    0x1000000ULL /*client uses AT */
#define OBD_CONNECT_LRU_RESIZE      0x2000000ULL /*LRU resize feature. */
#define OBD_CONNECT_MDS_MDS	    0x4000000ULL /*MDS-MDS connection */
#define OBD_CONNECT_REAL	    0x8000000ULL /*real connection */
#define OBD_CONNECT_CHANGE_QS      0x10000000ULL /*Not used since 2.4 */
#define OBD_CONNECT_CKSUM	   0x20000000ULL /*support several cksum algos*/
#define OBD_CONNECT_FID		   0x40000000ULL /*FID is supported by server */
#define OBD_CONNECT_VBR		   0x80000000ULL /*version based recovery */
#define OBD_CONNECT_LOV_V3	  0x100000000ULL /*client supports LOV v3 EA */
#define OBD_CONNECT_GRANT_SHRINK  0x200000000ULL /* support grant shrink */
#define OBD_CONNECT_SKIP_ORPHAN   0x400000000ULL /* don't reuse orphan objids */
#define OBD_CONNECT_MAX_EASIZE    0x800000000ULL /* preserved for large EA */
#define OBD_CONNECT_FULL20       0x1000000000ULL /* it is 2.0 client */
#define OBD_CONNECT_LAYOUTLOCK   0x2000000000ULL /* client uses layout lock */
#define OBD_CONNECT_64BITHASH    0x4000000000ULL /* client supports 64-bits
						  * directory hash
						  */
#define OBD_CONNECT_MAXBYTES     0x8000000000ULL /* max stripe size */
#define OBD_CONNECT_IMP_RECOV   0x10000000000ULL /* imp recovery support */
#define OBD_CONNECT_JOBSTATS    0x20000000000ULL /* jobid in ptlrpc_body */
#define OBD_CONNECT_UMASK       0x40000000000ULL /* create uses client umask */
#define OBD_CONNECT_EINPROGRESS 0x80000000000ULL /* client handles -EINPROGRESS
						  * RPC error properly
						  */
#define OBD_CONNECT_GRANT_PARAM 0x100000000000ULL/* extra grant params used for
						  * finer space reservation
						  */
#define OBD_CONNECT_FLOCK_OWNER 0x200000000000ULL /* for the fixed 1.8
						   * policy and 2.x server
						   */
#define OBD_CONNECT_LVB_TYPE	0x400000000000ULL /* variable type of LVB */
#define OBD_CONNECT_NANOSEC_TIME 0x800000000000ULL /* nanosecond timestamps */
#define OBD_CONNECT_LIGHTWEIGHT 0x1000000000000ULL/* lightweight connection */
#define OBD_CONNECT_SHORTIO     0x2000000000000ULL/* short io */
#define OBD_CONNECT_PINGLESS	0x4000000000000ULL/* pings not required */
#define OBD_CONNECT_FLOCK_DEAD	0x8000000000000ULL/* flock deadlock detection */
#define OBD_CONNECT_DISP_STRIPE 0x10000000000000ULL/*create stripe disposition*/
#define OBD_CONNECT_OPEN_BY_FID	0x20000000000000ULL	/* open by fid won't pack
							 * name in request
							 */
#define OBD_CONNECT_LFSCK	0x40000000000000ULL/* support online LFSCK */

/* XXX README XXX:
 * Please DO NOT add flag values here before first ensuring that this same
 * flag value is not in use on some other branch.  Please clear any such
 * changes with senior engineers before starting to use a new flag.  Then,
 * submit a small patch against EVERY branch that ONLY adds the new flag,
 * updates obd_connect_names[] for lprocfs_rd_connect_flags(), adds the
 * flag to check_obd_connect_data(), and updates wiretests accordingly, so it
 * can be approved and landed easily to reserve the flag for future use.
 */

/* The MNE_SWAB flag is overloading the MDS_MDS bit only for the MGS
 * connection.  It is a temporary bug fix for Imperative Recovery interop
 * between 2.2 and 2.3 x86/ppc nodes, and can be removed when interop for
 * 2.2 clients/servers is no longer needed.  LU-1252/LU-1644.
 */
#define OBD_CONNECT_MNE_SWAB		 OBD_CONNECT_MDS_MDS

#define OCD_HAS_FLAG(ocd, flg)  \
	(!!((ocd)->ocd_connect_flags & OBD_CONNECT_##flg))

/* Features required for this version of the client to work with server */
#define CLIENT_CONNECT_MDT_REQD (OBD_CONNECT_IBITS | OBD_CONNECT_FID | \
				 OBD_CONNECT_FULL20)

/* This structure is used for both request and reply.
 *
 * If we eventually have separate connect data for different types, which we
 * almost certainly will, then perhaps we stick a union in here.
 */
struct obd_connect_data_v1 {
	__u64 ocd_connect_flags; /* OBD_CONNECT_* per above */
	__u32 ocd_version;	 /* lustre release version number */
	__u32 ocd_grant;	 /* initial cache grant amount (bytes) */
	__u32 ocd_index;	 /* LOV index to connect to */
	__u32 ocd_brw_size;	 /* Maximum BRW size in bytes, must be 2^n */
	__u64 ocd_ibits_known;   /* inode bits this client understands */
	__u8  ocd_blocksize;     /* log2 of the backend filesystem blocksize */
	__u8  ocd_inodespace;    /* log2 of the per-inode space consumption */
	__u16 ocd_grant_extent;  /* per-extent grant overhead, in 1K blocks */
	__u32 ocd_unused;	/* also fix lustre_swab_connect */
	__u64 ocd_transno;       /* first transno from client to be replayed */
	__u32 ocd_group;	 /* MDS group on OST */
	__u32 ocd_cksum_types;   /* supported checksum algorithms */
	__u32 ocd_max_easize;    /* How big LOV EA can be on MDS */
	__u32 ocd_instance;      /* also fix lustre_swab_connect */
	__u64 ocd_maxbytes;      /* Maximum stripe size in bytes */
};

struct obd_connect_data {
	__u64 ocd_connect_flags; /* OBD_CONNECT_* per above */
	__u32 ocd_version;	 /* lustre release version number */
	__u32 ocd_grant;	 /* initial cache grant amount (bytes) */
	__u32 ocd_index;	 /* LOV index to connect to */
	__u32 ocd_brw_size;	 /* Maximum BRW size in bytes */
	__u64 ocd_ibits_known;   /* inode bits this client understands */
	__u8  ocd_blocksize;     /* log2 of the backend filesystem blocksize */
	__u8  ocd_inodespace;    /* log2 of the per-inode space consumption */
	__u16 ocd_grant_extent;  /* per-extent grant overhead, in 1K blocks */
	__u32 ocd_unused;	 /* also fix lustre_swab_connect */
	__u64 ocd_transno;       /* first transno from client to be replayed */
	__u32 ocd_group;	 /* MDS group on OST */
	__u32 ocd_cksum_types;   /* supported checksum algorithms */
	__u32 ocd_max_easize;    /* How big LOV EA can be on MDS */
	__u32 ocd_instance;      /* instance # of this target */
	__u64 ocd_maxbytes;      /* Maximum stripe size in bytes */
	/* Fields after ocd_maxbytes are only accessible by the receiver
	 * if the corresponding flag in ocd_connect_flags is set. Accessing
	 * any field after ocd_maxbytes on the receiver without a valid flag
	 * may result in out-of-bound memory access and kernel oops.
	 */
	__u64 padding1;	  /* added 2.1.0. also fix lustre_swab_connect */
	__u64 padding2;	  /* added 2.1.0. also fix lustre_swab_connect */
	__u64 padding3;	  /* added 2.1.0. also fix lustre_swab_connect */
	__u64 padding4;	  /* added 2.1.0. also fix lustre_swab_connect */
	__u64 padding5;	  /* added 2.1.0. also fix lustre_swab_connect */
	__u64 padding6;	  /* added 2.1.0. also fix lustre_swab_connect */
	__u64 padding7;	  /* added 2.1.0. also fix lustre_swab_connect */
	__u64 padding8;	  /* added 2.1.0. also fix lustre_swab_connect */
	__u64 padding9;	  /* added 2.1.0. also fix lustre_swab_connect */
	__u64 paddingA;	  /* added 2.1.0. also fix lustre_swab_connect */
	__u64 paddingB;	  /* added 2.1.0. also fix lustre_swab_connect */
	__u64 paddingC;	  /* added 2.1.0. also fix lustre_swab_connect */
	__u64 paddingD;	  /* added 2.1.0. also fix lustre_swab_connect */
	__u64 paddingE;	  /* added 2.1.0. also fix lustre_swab_connect */
	__u64 paddingF;	  /* added 2.1.0. also fix lustre_swab_connect */
};

/* XXX README XXX:
 * Please DO NOT use any fields here before first ensuring that this same
 * field is not in use on some other branch.  Please clear any such changes
 * with senior engineers before starting to use a new field.  Then, submit
 * a small patch against EVERY branch that ONLY adds the new field along with
 * the matching OBD_CONNECT flag, so that can be approved and landed easily to
 * reserve the flag for future use.
 */

void lustre_swab_connect(struct obd_connect_data *ocd);

/*
 * Supported checksum algorithms. Up to 32 checksum types are supported.
 * (32-bit mask stored in obd_connect_data::ocd_cksum_types)
 * Please update DECLARE_CKSUM_NAME/OBD_CKSUM_ALL in obd.h when adding a new
 * algorithm and also the OBD_FL_CKSUM* flags.
 */
enum cksum_type {
	OBD_CKSUM_CRC32  = 0x00000001,
	OBD_CKSUM_ADLER  = 0x00000002,
	OBD_CKSUM_CRC32C = 0x00000004,
};

/*
 *   OST requests: OBDO & OBD request records
 */

/* opcodes */
enum ost_cmd {
	OST_REPLY      =  0,       /* reply ? */
	OST_GETATTR    =  1,
	OST_SETATTR    =  2,
	OST_READ       =  3,
	OST_WRITE      =  4,
	OST_CREATE     =  5,
	OST_DESTROY    =  6,
	OST_GET_INFO   =  7,
	OST_CONNECT    =  8,
	OST_DISCONNECT =  9,
	OST_PUNCH      = 10,
	OST_OPEN       = 11,
	OST_CLOSE      = 12,
	OST_STATFS     = 13,
	OST_SYNC       = 16,
	OST_SET_INFO   = 17,
	OST_QUOTACHECK = 18,
	OST_QUOTACTL   = 19,
	OST_QUOTA_ADJUST_QUNIT = 20, /* not used since 2.4 */
	OST_LAST_OPC
};
#define OST_FIRST_OPC  OST_REPLY

enum obdo_flags {
	OBD_FL_INLINEDATA   = 0x00000001,
	OBD_FL_OBDMDEXISTS  = 0x00000002,
	OBD_FL_DELORPHAN    = 0x00000004, /* if set in o_flags delete orphans */
	OBD_FL_NORPC	    = 0x00000008, /* set in o_flags do in OSC not OST */
	OBD_FL_IDONLY       = 0x00000010, /* set in o_flags only adjust obj id*/
	OBD_FL_RECREATE_OBJS = 0x00000020, /* recreate missing obj */
	OBD_FL_DEBUG_CHECK  = 0x00000040, /* echo client/server debug check */
	OBD_FL_NO_USRQUOTA  = 0x00000100, /* the object's owner is over quota */
	OBD_FL_NO_GRPQUOTA  = 0x00000200, /* the object's group is over quota */
	OBD_FL_CREATE_CROW  = 0x00000400, /* object should be create on write */
	OBD_FL_SRVLOCK      = 0x00000800, /* delegate DLM locking to server */
	OBD_FL_CKSUM_CRC32  = 0x00001000, /* CRC32 checksum type */
	OBD_FL_CKSUM_ADLER  = 0x00002000, /* ADLER checksum type */
	OBD_FL_CKSUM_CRC32C = 0x00004000, /* CRC32C checksum type */
	OBD_FL_CKSUM_RSVD2  = 0x00008000, /* for future cksum types */
	OBD_FL_CKSUM_RSVD3  = 0x00010000, /* for future cksum types */
	OBD_FL_SHRINK_GRANT = 0x00020000, /* object shrink the grant */
	OBD_FL_MMAP	    = 0x00040000, /* object is mmapped on the client.
					   * XXX: obsoleted - reserved for old
					   * clients prior than 2.2
					   */
	OBD_FL_RECOV_RESEND = 0x00080000, /* recoverable resent */
	OBD_FL_NOSPC_BLK    = 0x00100000, /* no more block space on OST */
	OBD_FL_FLUSH	    = 0x00200000, /* flush pages on the OST */
	OBD_FL_SHORT_IO	    = 0x00400000, /* short io request */

	/* Note that while these checksum values are currently separate bits,
	 * in 2.x we can actually allow all values from 1-31 if we wanted.
	 */
	OBD_FL_CKSUM_ALL    = OBD_FL_CKSUM_CRC32 | OBD_FL_CKSUM_ADLER |
			      OBD_FL_CKSUM_CRC32C,

	/* mask for local-only flag, which won't be sent over network */
	OBD_FL_LOCAL_MASK   = 0xF0000000,
};

/*
 * All LOV EA magics should have the same postfix, if some new version
 * Lustre instroduces new LOV EA magic, then when down-grade to an old
 * Lustre, even though the old version system does not recognizes such
 * new magic, it still can distinguish the corrupted cases by checking
 * the magic's postfix.
 */
#define LOV_MAGIC_MAGIC 0x0BD0
#define LOV_MAGIC_MASK  0xFFFF

#define LOV_MAGIC_V1		(0x0BD10000 | LOV_MAGIC_MAGIC)
#define LOV_MAGIC_JOIN_V1	(0x0BD20000 | LOV_MAGIC_MAGIC)
#define LOV_MAGIC_V3		(0x0BD30000 | LOV_MAGIC_MAGIC)
#define LOV_MAGIC_MIGRATE	(0x0BD40000 | LOV_MAGIC_MAGIC)
#define LOV_MAGIC		LOV_MAGIC_V1

/*
 * magic for fully defined striping
 * the idea is that we should have different magics for striping "hints"
 * (struct lov_user_md_v[13]) and defined ready-to-use striping (struct
 * lov_mds_md_v[13]). at the moment the magics are used in wire protocol,
 * we can't just change it w/o long way preparation, but we still need a
 * mechanism to allow LOD to differentiate hint versus ready striping.
 * so, at the moment we do a trick: MDT knows what to expect from request
 * depending on the case (replay uses ready striping, non-replay req uses
 * hints), so MDT replaces magic with appropriate one and now LOD can
 * easily understand what's inside -bzzz
 */
#define LOV_MAGIC_V1_DEF  0x0CD10BD0
#define LOV_MAGIC_V3_DEF  0x0CD30BD0

#define lov_pattern(pattern)		(pattern & ~LOV_PATTERN_F_MASK)
#define lov_pattern_flags(pattern)	(pattern & LOV_PATTERN_F_MASK)

#define lov_ost_data lov_ost_data_v1
struct lov_ost_data_v1 {	  /* per-stripe data structure (little-endian)*/
	struct ost_id l_ost_oi;	  /* OST object ID */
	__u32 l_ost_gen;	  /* generation of this l_ost_idx */
	__u32 l_ost_idx;	  /* OST index in LOV (lov_tgt_desc->tgts) */
};

#define lov_mds_md lov_mds_md_v1
struct lov_mds_md_v1 {	    /* LOV EA mds/wire data (little-endian) */
	__u32 lmm_magic;	  /* magic number = LOV_MAGIC_V1 */
	__u32 lmm_pattern;	/* LOV_PATTERN_RAID0, LOV_PATTERN_RAID1 */
	struct ost_id	lmm_oi;	  /* LOV object ID */
	__u32 lmm_stripe_size;    /* size of stripe in bytes */
	/* lmm_stripe_count used to be __u32 */
	__u16 lmm_stripe_count;   /* num stripes in use for this object */
	__u16 lmm_layout_gen;     /* layout generation number */
	struct lov_ost_data_v1 lmm_objects[0]; /* per-stripe data */
};

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

static inline void lmm_oi_set_seq(struct ost_id *oi, __u64 seq)
{
	oi->oi.oi_seq = seq;
}

static inline void lmm_oi_set_id(struct ost_id *oi, __u64 oid)
{
	oi->oi.oi_id = oid;
}

static inline __u64 lmm_oi_id(const struct ost_id *oi)
{
	return oi->oi.oi_id;
}

static inline __u64 lmm_oi_seq(const struct ost_id *oi)
{
	return oi->oi.oi_seq;
}

static inline void lmm_oi_le_to_cpu(struct ost_id *dst_oi,
				    const struct ost_id *src_oi)
{
	dst_oi->oi.oi_id = le64_to_cpu(src_oi->oi.oi_id);
	dst_oi->oi.oi_seq = le64_to_cpu(src_oi->oi.oi_seq);
}

static inline void lmm_oi_cpu_to_le(struct ost_id *dst_oi,
				    const struct ost_id *src_oi)
{
	dst_oi->oi.oi_id = cpu_to_le64(src_oi->oi.oi_id);
	dst_oi->oi.oi_seq = cpu_to_le64(src_oi->oi.oi_seq);
}

/* extern void lustre_swab_lov_mds_md(struct lov_mds_md *llm); */

#define MAX_MD_SIZE							\
	(sizeof(struct lov_mds_md) + 4 * sizeof(struct lov_ost_data))
#define MIN_MD_SIZE							\
	(sizeof(struct lov_mds_md) + 1 * sizeof(struct lov_ost_data))

#define XATTR_NAME_ACL_ACCESS   "system.posix_acl_access"
#define XATTR_NAME_ACL_DEFAULT  "system.posix_acl_default"
#define XATTR_USER_PREFIX       "user."
#define XATTR_TRUSTED_PREFIX    "trusted."
#define XATTR_SECURITY_PREFIX   "security."
#define XATTR_LUSTRE_PREFIX     "lustre."

#define XATTR_NAME_LOV	  "trusted.lov"
#define XATTR_NAME_LMA	  "trusted.lma"
#define XATTR_NAME_LMV	  "trusted.lmv"
#define XATTR_NAME_DEFAULT_LMV	"trusted.dmv"
#define XATTR_NAME_LINK	 "trusted.link"
#define XATTR_NAME_FID	  "trusted.fid"
#define XATTR_NAME_VERSION      "trusted.version"
#define XATTR_NAME_SOM		"trusted.som"
#define XATTR_NAME_HSM		"trusted.hsm"
#define XATTR_NAME_LFSCK_NAMESPACE "trusted.lfsck_namespace"

struct lov_mds_md_v3 {	    /* LOV EA mds/wire data (little-endian) */
	__u32 lmm_magic;	  /* magic number = LOV_MAGIC_V3 */
	__u32 lmm_pattern;	/* LOV_PATTERN_RAID0, LOV_PATTERN_RAID1 */
	struct ost_id	lmm_oi;	  /* LOV object ID */
	__u32 lmm_stripe_size;    /* size of stripe in bytes */
	/* lmm_stripe_count used to be __u32 */
	__u16 lmm_stripe_count;   /* num stripes in use for this object */
	__u16 lmm_layout_gen;     /* layout generation number */
	char  lmm_pool_name[LOV_MAXPOOLNAME]; /* must be 32bit aligned */
	struct lov_ost_data_v1 lmm_objects[0]; /* per-stripe data */
};

static inline __u32 lov_mds_md_size(__u16 stripes, __u32 lmm_magic)
{
	if (lmm_magic == LOV_MAGIC_V3)
		return sizeof(struct lov_mds_md_v3) +
				stripes * sizeof(struct lov_ost_data_v1);
	else
		return sizeof(struct lov_mds_md_v1) +
				stripes * sizeof(struct lov_ost_data_v1);
}

static inline __u32
lov_mds_md_max_stripe_count(size_t buf_size, __u32 lmm_magic)
{
	switch (lmm_magic) {
	case LOV_MAGIC_V1: {
		struct lov_mds_md_v1 lmm;

		if (buf_size < sizeof(lmm))
			return 0;

		return (buf_size - sizeof(lmm)) / sizeof(lmm.lmm_objects[0]);
	}
	case LOV_MAGIC_V3: {
		struct lov_mds_md_v3 lmm;

		if (buf_size < sizeof(lmm))
			return 0;

		return (buf_size - sizeof(lmm)) / sizeof(lmm.lmm_objects[0]);
	}
	default:
		return 0;
	}
}

#define OBD_MD_FLID	   (0x00000001ULL) /* object ID */
#define OBD_MD_FLATIME     (0x00000002ULL) /* access time */
#define OBD_MD_FLMTIME     (0x00000004ULL) /* data modification time */
#define OBD_MD_FLCTIME     (0x00000008ULL) /* change time */
#define OBD_MD_FLSIZE      (0x00000010ULL) /* size */
#define OBD_MD_FLBLOCKS    (0x00000020ULL) /* allocated blocks count */
#define OBD_MD_FLBLKSZ     (0x00000040ULL) /* block size */
#define OBD_MD_FLMODE      (0x00000080ULL) /* access bits (mode & ~S_IFMT) */
#define OBD_MD_FLTYPE      (0x00000100ULL) /* object type (mode & S_IFMT) */
#define OBD_MD_FLUID       (0x00000200ULL) /* user ID */
#define OBD_MD_FLGID       (0x00000400ULL) /* group ID */
#define OBD_MD_FLFLAGS     (0x00000800ULL) /* flags word */
#define OBD_MD_FLNLINK     (0x00002000ULL) /* link count */
#define OBD_MD_FLGENER     (0x00004000ULL) /* generation number */
/*#define OBD_MD_FLINLINE    (0x00008000ULL)  inline data. used until 1.6.5 */
#define OBD_MD_FLRDEV      (0x00010000ULL) /* device number */
#define OBD_MD_FLEASIZE    (0x00020000ULL) /* extended attribute data */
#define OBD_MD_LINKNAME    (0x00040000ULL) /* symbolic link target */
#define OBD_MD_FLHANDLE    (0x00080000ULL) /* file/lock handle */
#define OBD_MD_FLCKSUM     (0x00100000ULL) /* bulk data checksum */
#define OBD_MD_FLQOS       (0x00200000ULL) /* quality of service stats */
/*#define OBD_MD_FLOSCOPQ    (0x00400000ULL) osc opaque data, never used */
#define OBD_MD_FLCOOKIE    (0x00800000ULL) /* log cancellation cookie */
#define OBD_MD_FLGROUP     (0x01000000ULL) /* group */
#define OBD_MD_FLFID       (0x02000000ULL) /* ->ost write inline fid */
#define OBD_MD_FLEPOCH     (0x04000000ULL) /* ->ost write with ioepoch */
					   /* ->mds if epoch opens or closes
					    */
#define OBD_MD_FLGRANT     (0x08000000ULL) /* ost preallocation space grant */
#define OBD_MD_FLDIREA     (0x10000000ULL) /* dir's extended attribute data */
#define OBD_MD_FLUSRQUOTA  (0x20000000ULL) /* over quota flags sent from ost */
#define OBD_MD_FLGRPQUOTA  (0x40000000ULL) /* over quota flags sent from ost */
#define OBD_MD_FLMODEASIZE (0x80000000ULL) /* EA size will be changed */

#define OBD_MD_MDS	   (0x0000000100000000ULL) /* where an inode lives on */
#define OBD_MD_REINT       (0x0000000200000000ULL) /* reintegrate oa */
#define OBD_MD_MEA	   (0x0000000400000000ULL) /* CMD split EA  */
#define OBD_MD_TSTATE      (0x0000000800000000ULL) /* transient state field */

#define OBD_MD_FLXATTR       (0x0000001000000000ULL) /* xattr */
#define OBD_MD_FLXATTRLS     (0x0000002000000000ULL) /* xattr list */
#define OBD_MD_FLXATTRRM     (0x0000004000000000ULL) /* xattr remove */
#define OBD_MD_FLACL	     (0x0000008000000000ULL) /* ACL */
/*	OBD_MD_FLRMTPERM     (0x0000010000000000ULL) remote perm, obsolete */
#define OBD_MD_FLMDSCAPA     (0x0000020000000000ULL) /* MDS capability */
#define OBD_MD_FLOSSCAPA     (0x0000040000000000ULL) /* OSS capability */
#define OBD_MD_FLCKSPLIT     (0x0000080000000000ULL) /* Check split on server */
#define OBD_MD_FLCROSSREF    (0x0000100000000000ULL) /* Cross-ref case */
#define OBD_MD_FLGETATTRLOCK (0x0000200000000000ULL) /* Get IOEpoch attributes
						      * under lock; for xattr
						      * requests means the
						      * client holds the lock
						      */
#define OBD_MD_FLOBJCOUNT    (0x0000400000000000ULL) /* for multiple destroy */

/*	OBD_MD_FLRMTLSETFACL (0x0001000000000000ULL) lfs lsetfacl, obsolete */
/*	OBD_MD_FLRMTLGETFACL (0x0002000000000000ULL) lfs lgetfacl, obsolete */
/*	OBD_MD_FLRMTRSETFACL (0x0004000000000000ULL) lfs rsetfacl, obsolete */
/*	OBD_MD_FLRMTRGETFACL (0x0008000000000000ULL) lfs rgetfacl, obsolete */

#define OBD_MD_FLDATAVERSION (0x0010000000000000ULL) /* iversion sum */
#define OBD_MD_FLRELEASED    (0x0020000000000000ULL) /* file released */

#define OBD_MD_DEFAULT_MEA   (0x0040000000000000ULL) /* default MEA */

#define OBD_MD_FLGETATTR (OBD_MD_FLID    | OBD_MD_FLATIME | OBD_MD_FLMTIME | \
			  OBD_MD_FLCTIME | OBD_MD_FLSIZE  | OBD_MD_FLBLKSZ | \
			  OBD_MD_FLMODE  | OBD_MD_FLTYPE  | OBD_MD_FLUID   | \
			  OBD_MD_FLGID   | OBD_MD_FLFLAGS | OBD_MD_FLNLINK | \
			  OBD_MD_FLGENER | OBD_MD_FLRDEV  | OBD_MD_FLGROUP)

#define OBD_MD_FLXATTRALL (OBD_MD_FLXATTR | OBD_MD_FLXATTRLS)

/* don't forget obdo_fid which is way down at the bottom so it can
 * come after the definition of llog_cookie
 */

enum hss_valid {
	HSS_SETMASK	= 0x01,
	HSS_CLEARMASK	= 0x02,
	HSS_ARCHIVE_ID	= 0x04,
};

struct hsm_state_set {
	__u32	hss_valid;
	__u32	hss_archive_id;
	__u64	hss_setmask;
	__u64	hss_clearmask;
};

void lustre_swab_hsm_user_state(struct hsm_user_state *hus);
void lustre_swab_hsm_state_set(struct hsm_state_set *hss);

void lustre_swab_obd_statfs(struct obd_statfs *os);

/* ost_body.data values for OST_BRW */

#define OBD_BRW_READ		0x01
#define OBD_BRW_WRITE		0x02
#define OBD_BRW_RWMASK		(OBD_BRW_READ | OBD_BRW_WRITE)
#define OBD_BRW_SYNC		0x08 /* this page is a part of synchronous
				      * transfer and is not accounted in
				      * the grant.
				      */
#define OBD_BRW_CHECK		0x10
#define OBD_BRW_FROM_GRANT      0x20 /* the osc manages this under llite */
#define OBD_BRW_GRANTED		0x40 /* the ost manages this */
#define OBD_BRW_NOCACHE		0x80 /* this page is a part of non-cached IO */
#define OBD_BRW_NOQUOTA	       0x100
#define OBD_BRW_SRVLOCK	       0x200 /* Client holds no lock over this page */
#define OBD_BRW_ASYNC	       0x400 /* Server may delay commit to disk */
#define OBD_BRW_MEMALLOC       0x800 /* Client runs in the "kswapd" context */
#define OBD_BRW_OVER_USRQUOTA 0x1000 /* Running out of user quota */
#define OBD_BRW_OVER_GRPQUOTA 0x2000 /* Running out of group quota */
#define OBD_BRW_SOFT_SYNC     0x4000 /* This flag notifies the server
				      * that the client is running low on
				      * space for unstable pages; asking
				      * it to sync quickly
				      */

#define OBD_OBJECT_EOF	LUSTRE_EOF

#define OST_MIN_PRECREATE 32
#define OST_MAX_PRECREATE 20000

struct obd_ioobj {
	struct ost_id	ioo_oid;	/* object ID, if multi-obj BRW */
	__u32		ioo_max_brw;	/* low 16 bits were o_mode before 2.4,
					 * now (PTLRPC_BULK_OPS_COUNT - 1) in
					 * high 16 bits in 2.4 and later
					 */
	__u32		ioo_bufcnt;	/* number of niobufs for this object */
};

#define IOOBJ_MAX_BRW_BITS	16
#define IOOBJ_TYPE_MASK		((1U << IOOBJ_MAX_BRW_BITS) - 1)
#define ioobj_max_brw_get(ioo)	(((ioo)->ioo_max_brw >> IOOBJ_MAX_BRW_BITS) + 1)
#define ioobj_max_brw_set(ioo, num)					\
do { (ioo)->ioo_max_brw = ((num) - 1) << IOOBJ_MAX_BRW_BITS; } while (0)

void lustre_swab_obd_ioobj(struct obd_ioobj *ioo);

/* multiple of 8 bytes => can array */
struct niobuf_remote {
	__u64 offset;
	__u32 len;
	__u32 flags;
};

void lustre_swab_niobuf_remote(struct niobuf_remote *nbr);

/* lock value block communicated between the filter and llite */

/* OST_LVB_ERR_INIT is needed because the return code in rc is
 * negative, i.e. because ((MASK + rc) & MASK) != MASK.
 */
#define OST_LVB_ERR_INIT 0xffbadbad80000000ULL
#define OST_LVB_ERR_MASK 0xffbadbad00000000ULL
#define OST_LVB_IS_ERR(blocks)					  \
	((blocks & OST_LVB_ERR_MASK) == OST_LVB_ERR_MASK)
#define OST_LVB_SET_ERR(blocks, rc)				     \
	do { blocks = OST_LVB_ERR_INIT + rc; } while (0)
#define OST_LVB_GET_ERR(blocks)    (int)(blocks - OST_LVB_ERR_INIT)

struct ost_lvb_v1 {
	__u64		lvb_size;
	__s64		lvb_mtime;
	__s64		lvb_atime;
	__s64		lvb_ctime;
	__u64		lvb_blocks;
};

void lustre_swab_ost_lvb_v1(struct ost_lvb_v1 *lvb);

struct ost_lvb {
	__u64		lvb_size;
	__s64		lvb_mtime;
	__s64		lvb_atime;
	__s64		lvb_ctime;
	__u64		lvb_blocks;
	__u32		lvb_mtime_ns;
	__u32		lvb_atime_ns;
	__u32		lvb_ctime_ns;
	__u32		lvb_padding;
};

void lustre_swab_ost_lvb(struct ost_lvb *lvb);

/*
 *   lquota data structures
 */

/* The lquota_id structure is an union of all the possible identifier types that
 * can be used with quota, this includes:
 * - 64-bit user ID
 * - 64-bit group ID
 * - a FID which can be used for per-directory quota in the future
 */
union lquota_id {
	struct lu_fid	qid_fid; /* FID for per-directory quota */
	__u64		qid_uid; /* user identifier */
	__u64		qid_gid; /* group identifier */
};

/* quotactl management */
struct obd_quotactl {
	__u32			qc_cmd;
	__u32			qc_type; /* see Q_* flag below */
	__u32			qc_id;
	__u32			qc_stat;
	struct obd_dqinfo	qc_dqinfo;
	struct obd_dqblk	qc_dqblk;
};

void lustre_swab_obd_quotactl(struct obd_quotactl *q);

#define Q_COPY(out, in, member) (out)->member = (in)->member

#define QCTL_COPY(out, in)		\
do {					\
	Q_COPY(out, in, qc_cmd);	\
	Q_COPY(out, in, qc_type);	\
	Q_COPY(out, in, qc_id);		\
	Q_COPY(out, in, qc_stat);	\
	Q_COPY(out, in, qc_dqinfo);	\
	Q_COPY(out, in, qc_dqblk);	\
} while (0)

/* Data structures associated with the quota locks */

/* Glimpse descriptor used for the index & per-ID quota locks */
struct ldlm_gl_lquota_desc {
	union lquota_id	gl_id;    /* quota ID subject to the glimpse */
	__u64		gl_flags; /* see LQUOTA_FL* below */
	__u64		gl_ver;   /* new index version */
	__u64		gl_hardlimit; /* new hardlimit or qunit value */
	__u64		gl_softlimit; /* new softlimit */
	__u64		gl_time;
	__u64		gl_pad2;
};

/* quota glimpse flags */
#define LQUOTA_FL_EDQUOT 0x1 /* user/group out of quota space on QMT */

/* LVB used with quota (global and per-ID) locks */
struct lquota_lvb {
	__u64	lvb_flags;	/* see LQUOTA_FL* above */
	__u64	lvb_id_may_rel; /* space that might be released later */
	__u64	lvb_id_rel;     /* space released by the slave for this ID */
	__u64	lvb_id_qunit;   /* current qunit value */
	__u64	lvb_pad1;
};

void lustre_swab_lquota_lvb(struct lquota_lvb *lvb);

/* op codes */
enum quota_cmd {
	QUOTA_DQACQ	= 601,
	QUOTA_DQREL	= 602,
	QUOTA_LAST_OPC
};
#define QUOTA_FIRST_OPC	QUOTA_DQACQ

/*
 *   MDS REQ RECORDS
 */

/* opcodes */
enum mds_cmd {
	MDS_GETATTR		= 33,
	MDS_GETATTR_NAME	= 34,
	MDS_CLOSE		= 35,
	MDS_REINT		= 36,
	MDS_READPAGE		= 37,
	MDS_CONNECT		= 38,
	MDS_DISCONNECT		= 39,
	MDS_GETSTATUS		= 40,
	MDS_STATFS		= 41,
	MDS_PIN			= 42,
	MDS_UNPIN		= 43,
	MDS_SYNC		= 44,
	MDS_DONE_WRITING	= 45,
	MDS_SET_INFO		= 46,
	MDS_QUOTACHECK		= 47,
	MDS_QUOTACTL		= 48,
	MDS_GETXATTR		= 49,
	MDS_SETXATTR		= 50, /* obsolete, now it's MDS_REINT op */
	MDS_WRITEPAGE		= 51,
	MDS_IS_SUBDIR		= 52,
	MDS_GET_INFO		= 53,
	MDS_HSM_STATE_GET	= 54,
	MDS_HSM_STATE_SET	= 55,
	MDS_HSM_ACTION		= 56,
	MDS_HSM_PROGRESS	= 57,
	MDS_HSM_REQUEST		= 58,
	MDS_HSM_CT_REGISTER	= 59,
	MDS_HSM_CT_UNREGISTER	= 60,
	MDS_SWAP_LAYOUTS	= 61,
	MDS_LAST_OPC
};

#define MDS_FIRST_OPC    MDS_GETATTR

/*
 * Do not exceed 63
 */

enum mdt_reint_cmd {
	REINT_SETATTR  = 1,
	REINT_CREATE   = 2,
	REINT_LINK     = 3,
	REINT_UNLINK   = 4,
	REINT_RENAME   = 5,
	REINT_OPEN     = 6,
	REINT_SETXATTR = 7,
	REINT_RMENTRY  = 8,
	REINT_MIGRATE  = 9,
	REINT_MAX
};

void lustre_swab_generic_32s(__u32 *val);

/* the disposition of the intent outlines what was executed */
#define DISP_IT_EXECD	0x00000001
#define DISP_LOOKUP_EXECD    0x00000002
#define DISP_LOOKUP_NEG      0x00000004
#define DISP_LOOKUP_POS      0x00000008
#define DISP_OPEN_CREATE     0x00000010
#define DISP_OPEN_OPEN       0x00000020
#define DISP_ENQ_COMPLETE    0x00400000		/* obsolete and unused */
#define DISP_ENQ_OPEN_REF    0x00800000
#define DISP_ENQ_CREATE_REF  0x01000000
#define DISP_OPEN_LOCK       0x02000000
#define DISP_OPEN_LEASE      0x04000000
#define DISP_OPEN_STRIPE     0x08000000

/* INODE LOCK PARTS */
#define MDS_INODELOCK_LOOKUP 0x000001	/* For namespace, dentry etc, and also
					 * was used to protect permission (mode,
					 * owner, group etc) before 2.4.
					 */
#define MDS_INODELOCK_UPDATE 0x000002	/* size, links, timestamps */
#define MDS_INODELOCK_OPEN   0x000004	/* For opened files */
#define MDS_INODELOCK_LAYOUT 0x000008	/* for layout */

/* The PERM bit is added int 2.4, and it is used to protect permission(mode,
 * owner, group, acl etc), so to separate the permission from LOOKUP lock.
 * Because for remote directories(in DNE), these locks will be granted by
 * different MDTs(different ldlm namespace).
 *
 * For local directory, MDT will always grant UPDATE_LOCK|PERM_LOCK together.
 * For Remote directory, the master MDT, where the remote directory is, will
 * grant UPDATE_LOCK|PERM_LOCK, and the remote MDT, where the name entry is,
 * will grant LOOKUP_LOCK.
 */
#define MDS_INODELOCK_PERM   0x000010
#define MDS_INODELOCK_XATTR  0x000020	/* extended attributes */

#define MDS_INODELOCK_MAXSHIFT 5
/* This FULL lock is useful to take on unlink sort of operations */
#define MDS_INODELOCK_FULL ((1<<(MDS_INODELOCK_MAXSHIFT+1))-1)

/* NOTE: until Lustre 1.8.7/2.1.1 the fid_ver() was packed into name[2],
 * but was moved into name[1] along with the OID to avoid consuming the
 * name[2,3] fields that need to be used for the quota id (also a FID).
 */
enum {
	LUSTRE_RES_ID_SEQ_OFF = 0,
	LUSTRE_RES_ID_VER_OID_OFF = 1,
	LUSTRE_RES_ID_WAS_VER_OFF = 2, /* see note above */
	LUSTRE_RES_ID_QUOTA_SEQ_OFF = 2,
	LUSTRE_RES_ID_QUOTA_VER_OID_OFF = 3,
	LUSTRE_RES_ID_HSH_OFF = 3
};

#define MDS_STATUS_CONN 1
#define MDS_STATUS_LOV 2

/* mdt_thread_info.mti_flags. */
enum md_op_flags {
	/* The flag indicates Size-on-MDS attributes are changed. */
	MF_SOM_CHANGE	   = (1 << 0),
	/* Flags indicates an epoch opens or closes. */
	MF_EPOCH_OPEN	   = (1 << 1),
	MF_EPOCH_CLOSE	  = (1 << 2),
	MF_MDC_CANCEL_FID1      = (1 << 3),
	MF_MDC_CANCEL_FID2      = (1 << 4),
	MF_MDC_CANCEL_FID3      = (1 << 5),
	MF_MDC_CANCEL_FID4      = (1 << 6),
	/* There is a pending attribute update. */
	MF_SOM_AU	       = (1 << 7),
	/* Cancel OST locks while getattr OST attributes. */
	MF_GETATTR_LOCK	 = (1 << 8),
	MF_GET_MDT_IDX	  = (1 << 9),
};

#define MF_SOM_LOCAL_FLAGS (MF_SOM_CHANGE | MF_EPOCH_OPEN | MF_EPOCH_CLOSE)

#define LUSTRE_BFLAG_UNCOMMITTED_WRITES   0x1

/* these should be identical to their EXT4_*_FL counterparts, they are
 * redefined here only to avoid dragging in fs/ext4/ext4.h
 */
#define LUSTRE_SYNC_FL	 0x00000008 /* Synchronous updates */
#define LUSTRE_IMMUTABLE_FL    0x00000010 /* Immutable file */
#define LUSTRE_APPEND_FL       0x00000020 /* writes to file may only append */
#define LUSTRE_NOATIME_FL      0x00000080 /* do not update atime */
#define LUSTRE_DIRSYNC_FL      0x00010000 /* dirsync behaviour (dir only) */

/* Convert wire LUSTRE_*_FL to corresponding client local VFS S_* values
 * for the client inode i_flags.  The LUSTRE_*_FL are the Lustre wire
 * protocol equivalents of LDISKFS_*_FL values stored on disk, while
 * the S_* flags are kernel-internal values that change between kernel
 * versions.  These flags are set/cleared via FSFILT_IOC_{GET,SET}_FLAGS.
 * See b=16526 for a full history.
 */
static inline int ll_ext_to_inode_flags(int flags)
{
	return (((flags & LUSTRE_SYNC_FL)      ? S_SYNC      : 0) |
		((flags & LUSTRE_NOATIME_FL)   ? S_NOATIME   : 0) |
		((flags & LUSTRE_APPEND_FL)    ? S_APPEND    : 0) |
		((flags & LUSTRE_DIRSYNC_FL)   ? S_DIRSYNC   : 0) |
		((flags & LUSTRE_IMMUTABLE_FL) ? S_IMMUTABLE : 0));
}

static inline int ll_inode_to_ext_flags(int iflags)
{
	return (((iflags & S_SYNC)      ? LUSTRE_SYNC_FL      : 0) |
		((iflags & S_NOATIME)   ? LUSTRE_NOATIME_FL   : 0) |
		((iflags & S_APPEND)    ? LUSTRE_APPEND_FL    : 0) |
		((iflags & S_DIRSYNC)   ? LUSTRE_DIRSYNC_FL   : 0) |
		((iflags & S_IMMUTABLE) ? LUSTRE_IMMUTABLE_FL : 0));
}

/* 64 possible states */
enum md_transient_state {
	MS_RESTORE	= (1 << 0),	/* restore is running */
};

struct mdt_body {
	struct lu_fid  fid1;
	struct lu_fid  fid2;
	struct lustre_handle handle;
	__u64	  valid;
	__u64	  size;   /* Offset, in the case of MDS_READPAGE */
	__s64	  mtime;
	__s64	  atime;
	__s64	  ctime;
	__u64	  blocks; /* XID, in the case of MDS_READPAGE */
	__u64	  ioepoch;
	__u64	  t_state; /* transient file state defined in
			    * enum md_transient_state
			    * was "ino" until 2.4.0
			    */
	__u32	  fsuid;
	__u32	  fsgid;
	__u32	  capability;
	__u32	  mode;
	__u32	  uid;
	__u32	  gid;
	__u32	  flags; /* from vfs for pin/unpin, LUSTRE_BFLAG close */
	__u32	  rdev;
	__u32	  nlink; /* #bytes to read in the case of MDS_READPAGE */
	__u32	  unused2; /* was "generation" until 2.4.0 */
	__u32	  suppgid;
	__u32	  eadatasize;
	__u32	  aclsize;
	__u32	  max_mdsize;
	__u32	  max_cookiesize;
	__u32	  uid_h; /* high 32-bits of uid, for FUID */
	__u32	  gid_h; /* high 32-bits of gid, for FUID */
	__u32	  padding_5; /* also fix lustre_swab_mdt_body */
	__u64	  padding_6;
	__u64	  padding_7;
	__u64	  padding_8;
	__u64	  padding_9;
	__u64	  padding_10;
}; /* 216 */

void lustre_swab_mdt_body(struct mdt_body *b);

struct mdt_ioepoch {
	struct lustre_handle handle;
	__u64  ioepoch;
	__u32  flags;
	__u32  padding;
};

void lustre_swab_mdt_ioepoch(struct mdt_ioepoch *b);

/* permissions for md_perm.mp_perm */
enum {
	CFS_SETUID_PERM = 0x01,
	CFS_SETGID_PERM = 0x02,
	CFS_SETGRP_PERM = 0x04,
};

struct mdt_rec_setattr {
	__u32	   sa_opcode;
	__u32	   sa_cap;
	__u32	   sa_fsuid;
	__u32	   sa_fsuid_h;
	__u32	   sa_fsgid;
	__u32	   sa_fsgid_h;
	__u32	   sa_suppgid;
	__u32	   sa_suppgid_h;
	__u32	   sa_padding_1;
	__u32	   sa_padding_1_h;
	struct lu_fid   sa_fid;
	__u64	   sa_valid;
	__u32	   sa_uid;
	__u32	   sa_gid;
	__u64	   sa_size;
	__u64	   sa_blocks;
	__s64	   sa_mtime;
	__s64	   sa_atime;
	__s64	   sa_ctime;
	__u32	   sa_attr_flags;
	__u32	   sa_mode;
	__u32	   sa_bias;      /* some operation flags */
	__u32	   sa_padding_3;
	__u32	   sa_padding_4;
	__u32	   sa_padding_5;
};

void lustre_swab_mdt_rec_setattr(struct mdt_rec_setattr *sa);

/*
 * Attribute flags used in mdt_rec_setattr::sa_valid.
 * The kernel's #defines for ATTR_* should not be used over the network
 * since the client and MDS may run different kernels (see bug 13828)
 * Therefore, we should only use MDS_ATTR_* attributes for sa_valid.
 */
#define MDS_ATTR_MODE	       0x1ULL /* = 1 */
#define MDS_ATTR_UID	       0x2ULL /* = 2 */
#define MDS_ATTR_GID	       0x4ULL /* = 4 */
#define MDS_ATTR_SIZE	       0x8ULL /* = 8 */
#define MDS_ATTR_ATIME	      0x10ULL /* = 16 */
#define MDS_ATTR_MTIME	      0x20ULL /* = 32 */
#define MDS_ATTR_CTIME	      0x40ULL /* = 64 */
#define MDS_ATTR_ATIME_SET    0x80ULL /* = 128 */
#define MDS_ATTR_MTIME_SET   0x100ULL /* = 256 */
#define MDS_ATTR_FORCE       0x200ULL /* = 512, Not a change, but a change it */
#define MDS_ATTR_ATTR_FLAG   0x400ULL /* = 1024 */
#define MDS_ATTR_KILL_SUID   0x800ULL /* = 2048 */
#define MDS_ATTR_KILL_SGID  0x1000ULL /* = 4096 */
#define MDS_ATTR_CTIME_SET  0x2000ULL /* = 8192 */
#define MDS_ATTR_FROM_OPEN  0x4000ULL /* = 16384, called from open path,
				       * ie O_TRUNC
				       */
#define MDS_ATTR_BLOCKS     0x8000ULL /* = 32768 */

#define MDS_FMODE_CLOSED	 00000000
#define MDS_FMODE_EXEC	   00000004
/* IO Epoch is opened on a closed file. */
#define MDS_FMODE_EPOCH	  01000000
/* IO Epoch is opened on a file truncate. */
#define MDS_FMODE_TRUNC	  02000000
/* Size-on-MDS Attribute Update is pending. */
#define MDS_FMODE_SOM	    04000000

#define MDS_OPEN_CREATED	 00000010
#define MDS_OPEN_CROSS	   00000020

#define MDS_OPEN_CREAT	   00000100
#define MDS_OPEN_EXCL	    00000200
#define MDS_OPEN_TRUNC	   00001000
#define MDS_OPEN_APPEND	  00002000
#define MDS_OPEN_SYNC	    00010000
#define MDS_OPEN_DIRECTORY       00200000

#define MDS_OPEN_BY_FID		040000000 /* open_by_fid for known object */
#define MDS_OPEN_DELAY_CREATE  0100000000 /* delay initial object create */
#define MDS_OPEN_OWNEROVERRIDE 0200000000 /* NFSD rw-reopen ro file for owner */
#define MDS_OPEN_JOIN_FILE     0400000000 /* open for join file.
					   * We do not support JOIN FILE
					   * anymore, reserve this flags
					   * just for preventing such bit
					   * to be reused.
					   */

#define MDS_OPEN_LOCK	      04000000000 /* This open requires open lock */
#define MDS_OPEN_HAS_EA      010000000000 /* specify object create pattern */
#define MDS_OPEN_HAS_OBJS    020000000000 /* Just set the EA the obj exist */
#define MDS_OPEN_NORESTORE  0100000000000ULL /* Do not restore file at open */
#define MDS_OPEN_NEWSTRIPE  0200000000000ULL /* New stripe needed (restripe or
					      * hsm restore) */
#define MDS_OPEN_VOLATILE   0400000000000ULL /* File is volatile = created
						unlinked */
#define MDS_OPEN_LEASE	   01000000000000ULL /* Open the file and grant lease
					      * delegation, succeed if it's not
					      * being opened with conflict mode.
					      */
#define MDS_OPEN_RELEASE   02000000000000ULL /* Open the file for HSM release */

enum mds_op_bias {
	MDS_CHECK_SPLIT		= 1 << 0,
	MDS_CROSS_REF		= 1 << 1,
	MDS_VTX_BYPASS		= 1 << 2,
	MDS_PERM_BYPASS		= 1 << 3,
	MDS_SOM			= 1 << 4,
	MDS_QUOTA_IGNORE	= 1 << 5,
	MDS_CLOSE_CLEANUP	= 1 << 6,
	MDS_KEEP_ORPHAN		= 1 << 7,
	MDS_RECOV_OPEN		= 1 << 8,
	MDS_DATA_MODIFIED	= 1 << 9,
	MDS_CREATE_VOLATILE	= 1 << 10,
	MDS_OWNEROVERRIDE	= 1 << 11,
	MDS_HSM_RELEASE		= 1 << 12,
	MDS_RENAME_MIGRATE	= BIT(13),
};

/* instance of mdt_reint_rec */
struct mdt_rec_create {
	__u32	   cr_opcode;
	__u32	   cr_cap;
	__u32	   cr_fsuid;
	__u32	   cr_fsuid_h;
	__u32	   cr_fsgid;
	__u32	   cr_fsgid_h;
	__u32	   cr_suppgid1;
	__u32	   cr_suppgid1_h;
	__u32	   cr_suppgid2;
	__u32	   cr_suppgid2_h;
	struct lu_fid   cr_fid1;
	struct lu_fid   cr_fid2;
	struct lustre_handle cr_old_handle; /* handle in case of open replay */
	__s64	   cr_time;
	__u64	   cr_rdev;
	__u64	   cr_ioepoch;
	__u64	   cr_padding_1;   /* rr_blocks */
	__u32	   cr_mode;
	__u32	   cr_bias;
	/* use of helpers set/get_mrc_cr_flags() is needed to access
	 * 64 bits cr_flags [cr_flags_l, cr_flags_h], this is done to
	 * extend cr_flags size without breaking 1.8 compat
	 */
	__u32	   cr_flags_l;     /* for use with open, low  32 bits  */
	__u32	   cr_flags_h;     /* for use with open, high 32 bits */
	__u32	   cr_umask;       /* umask for create */
	__u32	   cr_padding_4;   /* rr_padding_4 */
};

static inline void set_mrc_cr_flags(struct mdt_rec_create *mrc, __u64 flags)
{
	mrc->cr_flags_l = (__u32)(flags & 0xFFFFFFFFUll);
	mrc->cr_flags_h = (__u32)(flags >> 32);
}

static inline __u64 get_mrc_cr_flags(struct mdt_rec_create *mrc)
{
	return ((__u64)(mrc->cr_flags_l) | ((__u64)mrc->cr_flags_h << 32));
}

/* instance of mdt_reint_rec */
struct mdt_rec_link {
	__u32	   lk_opcode;
	__u32	   lk_cap;
	__u32	   lk_fsuid;
	__u32	   lk_fsuid_h;
	__u32	   lk_fsgid;
	__u32	   lk_fsgid_h;
	__u32	   lk_suppgid1;
	__u32	   lk_suppgid1_h;
	__u32	   lk_suppgid2;
	__u32	   lk_suppgid2_h;
	struct lu_fid   lk_fid1;
	struct lu_fid   lk_fid2;
	__s64	   lk_time;
	__u64	   lk_padding_1;   /* rr_atime */
	__u64	   lk_padding_2;   /* rr_ctime */
	__u64	   lk_padding_3;   /* rr_size */
	__u64	   lk_padding_4;   /* rr_blocks */
	__u32	   lk_bias;
	__u32	   lk_padding_5;   /* rr_mode */
	__u32	   lk_padding_6;   /* rr_flags */
	__u32	   lk_padding_7;   /* rr_padding_2 */
	__u32	   lk_padding_8;   /* rr_padding_3 */
	__u32	   lk_padding_9;   /* rr_padding_4 */
};

/* instance of mdt_reint_rec */
struct mdt_rec_unlink {
	__u32	   ul_opcode;
	__u32	   ul_cap;
	__u32	   ul_fsuid;
	__u32	   ul_fsuid_h;
	__u32	   ul_fsgid;
	__u32	   ul_fsgid_h;
	__u32	   ul_suppgid1;
	__u32	   ul_suppgid1_h;
	__u32	   ul_suppgid2;
	__u32	   ul_suppgid2_h;
	struct lu_fid   ul_fid1;
	struct lu_fid   ul_fid2;
	__s64	   ul_time;
	__u64	   ul_padding_2;   /* rr_atime */
	__u64	   ul_padding_3;   /* rr_ctime */
	__u64	   ul_padding_4;   /* rr_size */
	__u64	   ul_padding_5;   /* rr_blocks */
	__u32	   ul_bias;
	__u32	   ul_mode;
	__u32	   ul_padding_6;   /* rr_flags */
	__u32	   ul_padding_7;   /* rr_padding_2 */
	__u32	   ul_padding_8;   /* rr_padding_3 */
	__u32	   ul_padding_9;   /* rr_padding_4 */
};

/* instance of mdt_reint_rec */
struct mdt_rec_rename {
	__u32	   rn_opcode;
	__u32	   rn_cap;
	__u32	   rn_fsuid;
	__u32	   rn_fsuid_h;
	__u32	   rn_fsgid;
	__u32	   rn_fsgid_h;
	__u32	   rn_suppgid1;
	__u32	   rn_suppgid1_h;
	__u32	   rn_suppgid2;
	__u32	   rn_suppgid2_h;
	struct lu_fid   rn_fid1;
	struct lu_fid   rn_fid2;
	__s64	   rn_time;
	__u64	   rn_padding_1;   /* rr_atime */
	__u64	   rn_padding_2;   /* rr_ctime */
	__u64	   rn_padding_3;   /* rr_size */
	__u64	   rn_padding_4;   /* rr_blocks */
	__u32	   rn_bias;	/* some operation flags */
	__u32	   rn_mode;	/* cross-ref rename has mode */
	__u32	   rn_padding_5;   /* rr_flags */
	__u32	   rn_padding_6;   /* rr_padding_2 */
	__u32	   rn_padding_7;   /* rr_padding_3 */
	__u32	   rn_padding_8;   /* rr_padding_4 */
};

/* instance of mdt_reint_rec */
struct mdt_rec_setxattr {
	__u32	   sx_opcode;
	__u32	   sx_cap;
	__u32	   sx_fsuid;
	__u32	   sx_fsuid_h;
	__u32	   sx_fsgid;
	__u32	   sx_fsgid_h;
	__u32	   sx_suppgid1;
	__u32	   sx_suppgid1_h;
	__u32	   sx_suppgid2;
	__u32	   sx_suppgid2_h;
	struct lu_fid   sx_fid;
	__u64	   sx_padding_1;   /* These three are rr_fid2 */
	__u32	   sx_padding_2;
	__u32	   sx_padding_3;
	__u64	   sx_valid;
	__s64	   sx_time;
	__u64	   sx_padding_5;   /* rr_ctime */
	__u64	   sx_padding_6;   /* rr_size */
	__u64	   sx_padding_7;   /* rr_blocks */
	__u32	   sx_size;
	__u32	   sx_flags;
	__u32	   sx_padding_8;   /* rr_flags */
	__u32	   sx_padding_9;   /* rr_padding_2 */
	__u32	   sx_padding_10;  /* rr_padding_3 */
	__u32	   sx_padding_11;  /* rr_padding_4 */
};

/*
 * mdt_rec_reint is the template for all mdt_reint_xxx structures.
 * Do NOT change the size of various members, otherwise the value
 * will be broken in lustre_swab_mdt_rec_reint().
 *
 * If you add new members in other mdt_reint_xxx structures and need to use the
 * rr_padding_x fields, then update lustre_swab_mdt_rec_reint() also.
 */
struct mdt_rec_reint {
	__u32	   rr_opcode;
	__u32	   rr_cap;
	__u32	   rr_fsuid;
	__u32	   rr_fsuid_h;
	__u32	   rr_fsgid;
	__u32	   rr_fsgid_h;
	__u32	   rr_suppgid1;
	__u32	   rr_suppgid1_h;
	__u32	   rr_suppgid2;
	__u32	   rr_suppgid2_h;
	struct lu_fid   rr_fid1;
	struct lu_fid   rr_fid2;
	__s64	   rr_mtime;
	__s64	   rr_atime;
	__s64	   rr_ctime;
	__u64	   rr_size;
	__u64	   rr_blocks;
	__u32	   rr_bias;
	__u32	   rr_mode;
	__u32	   rr_flags;
	__u32	   rr_flags_h;
	__u32	   rr_umask;
	__u32	   rr_padding_4; /* also fix lustre_swab_mdt_rec_reint */
};

void lustre_swab_mdt_rec_reint(struct mdt_rec_reint *rr);

/* lmv structures */
struct lmv_desc {
	__u32 ld_tgt_count;		/* how many MDS's */
	__u32 ld_active_tgt_count;	 /* how many active */
	__u32 ld_default_stripe_count;     /* how many objects are used */
	__u32 ld_pattern;		  /* default hash pattern */
	__u64 ld_default_hash_size;
	__u64 ld_padding_1;		/* also fix lustre_swab_lmv_desc */
	__u32 ld_padding_2;		/* also fix lustre_swab_lmv_desc */
	__u32 ld_qos_maxage;	       /* in second */
	__u32 ld_padding_3;		/* also fix lustre_swab_lmv_desc */
	__u32 ld_padding_4;		/* also fix lustre_swab_lmv_desc */
	struct obd_uuid ld_uuid;
};

/* LMV layout EA, and it will be stored both in master and slave object */
struct lmv_mds_md_v1 {
	__u32 lmv_magic;
	__u32 lmv_stripe_count;
	__u32 lmv_master_mdt_index;	/* On master object, it is master
					 * MDT index, on slave object, it
					 * is stripe index of the slave obj
					 */
	__u32 lmv_hash_type;		/* dir stripe policy, i.e. indicate
					 * which hash function to be used,
					 * Note: only lower 16 bits is being
					 * used for now. Higher 16 bits will
					 * be used to mark the object status,
					 * for example migrating or dead.
					 */
	__u32 lmv_layout_version;	/* Used for directory restriping */
	__u32 lmv_padding;
	struct lu_fid lmv_master_fid;	/* The FID of the master object, which
					 * is the namespace-visible dir FID
					 */
	char lmv_pool_name[LOV_MAXPOOLNAME];	/* pool name */
	struct lu_fid lmv_stripe_fids[0];	/* FIDs for each stripe */
};

#define LMV_MAGIC_V1	 0x0CD20CD0	/* normal stripe lmv magic */
#define LMV_MAGIC	 LMV_MAGIC_V1

/* #define LMV_USER_MAGIC 0x0CD30CD0 */
#define LMV_MAGIC_STRIPE 0x0CD40CD0	/* magic for dir sub_stripe */

/*
 *Right now only the lower part(0-16bits) of lmv_hash_type is being used,
 * and the higher part will be the flag to indicate the status of object,
 * for example the object is being migrated. And the hash function
 * might be interpreted differently with different flags.
 */
#define LMV_HASH_TYPE_MASK		0x0000ffff

#define LMV_HASH_FLAG_MIGRATION		0x80000000
#define LMV_HASH_FLAG_DEAD		0x40000000

/**
 * The FNV-1a hash algorithm is as follows:
 *     hash = FNV_offset_basis
 *     for each octet_of_data to be hashed
 *             hash = hash XOR octet_of_data
 *             hash = hash × FNV_prime
 *     return hash
 * http://en.wikipedia.org/wiki/Fowler–Noll–Vo_hash_function#FNV-1a_hash
 *
 * http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-reference-source
 * FNV_prime is 2^40 + 2^8 + 0xb3 = 0x100000001b3ULL
 **/
#define LUSTRE_FNV_1A_64_PRIME		0x100000001b3ULL
#define LUSTRE_FNV_1A_64_OFFSET_BIAS	0xcbf29ce484222325ULL
static inline __u64 lustre_hash_fnv_1a_64(const void *buf, size_t size)
{
	__u64 hash = LUSTRE_FNV_1A_64_OFFSET_BIAS;
	const unsigned char *p = buf;
	size_t i;

	for (i = 0; i < size; i++) {
		hash ^= p[i];
		hash *= LUSTRE_FNV_1A_64_PRIME;
	}

	return hash;
}

union lmv_mds_md {
	__u32			lmv_magic;
	struct lmv_mds_md_v1	lmv_md_v1;
	struct lmv_user_md	lmv_user_md;
};

void lustre_swab_lmv_mds_md(union lmv_mds_md *lmm);

static inline ssize_t lmv_mds_md_size(int stripe_count, unsigned int lmm_magic)
{
	ssize_t len = -EINVAL;

	switch (lmm_magic) {
	case LMV_MAGIC_V1: {
		struct lmv_mds_md_v1 *lmm1;

		len = sizeof(*lmm1);
		len += stripe_count * sizeof(lmm1->lmv_stripe_fids[0]);
		break; }
	default:
		break;
	}
	return len;
}

static inline int lmv_mds_md_stripe_count_get(const union lmv_mds_md *lmm)
{
	switch (le32_to_cpu(lmm->lmv_magic)) {
	case LMV_MAGIC_V1:
		return le32_to_cpu(lmm->lmv_md_v1.lmv_stripe_count);
	case LMV_USER_MAGIC:
		return le32_to_cpu(lmm->lmv_user_md.lum_stripe_count);
	default:
		return -EINVAL;
	}
}

static inline int lmv_mds_md_stripe_count_set(union lmv_mds_md *lmm,
					      unsigned int stripe_count)
{
	int rc = 0;

	switch (le32_to_cpu(lmm->lmv_magic)) {
	case LMV_MAGIC_V1:
		lmm->lmv_md_v1.lmv_stripe_count = cpu_to_le32(stripe_count);
		break;
	case LMV_USER_MAGIC:
		lmm->lmv_user_md.lum_stripe_count = cpu_to_le32(stripe_count);
		break;
	default:
		rc = -EINVAL;
		break;
	}
	return rc;
}

enum fld_rpc_opc {
	FLD_QUERY	= 900,
	FLD_READ	= 901,
	FLD_LAST_OPC,
	FLD_FIRST_OPC	= FLD_QUERY
};

enum seq_rpc_opc {
	SEQ_QUERY		       = 700,
	SEQ_LAST_OPC,
	SEQ_FIRST_OPC		   = SEQ_QUERY
};

enum seq_op {
	SEQ_ALLOC_SUPER = 0,
	SEQ_ALLOC_META = 1
};

enum fld_op {
	FLD_CREATE = 0,
	FLD_DELETE = 1,
	FLD_LOOKUP = 2,
};

/*
 *  LOV data structures
 */

#define LOV_MAX_UUID_BUFFER_SIZE  8192
/* The size of the buffer the lov/mdc reserves for the
 * array of UUIDs returned by the MDS.  With the current
 * protocol, this will limit the max number of OSTs per LOV
 */

#define LOV_DESC_MAGIC 0xB0CCDE5C
#define LOV_DESC_QOS_MAXAGE_DEFAULT 5  /* Seconds */
#define LOV_DESC_STRIPE_SIZE_DEFAULT (1 << LNET_MTU_BITS)

/* LOV settings descriptor (should only contain static info) */
struct lov_desc {
	__u32 ld_tgt_count;		/* how many OBD's */
	__u32 ld_active_tgt_count;	/* how many active */
	__u32 ld_default_stripe_count;  /* how many objects are used */
	__u32 ld_pattern;		/* default PATTERN_RAID0 */
	__u64 ld_default_stripe_size;   /* in bytes */
	__u64 ld_default_stripe_offset; /* in bytes */
	__u32 ld_padding_0;		/* unused */
	__u32 ld_qos_maxage;		/* in second */
	__u32 ld_padding_1;		/* also fix lustre_swab_lov_desc */
	__u32 ld_padding_2;		/* also fix lustre_swab_lov_desc */
	struct obd_uuid ld_uuid;
};

#define ld_magic ld_active_tgt_count       /* for swabbing from llogs */

void lustre_swab_lov_desc(struct lov_desc *ld);

/*
 *   LDLM requests:
 */
/* opcodes -- MUST be distinct from OST/MDS opcodes */
enum ldlm_cmd {
	LDLM_ENQUEUE     = 101,
	LDLM_CONVERT     = 102,
	LDLM_CANCEL      = 103,
	LDLM_BL_CALLBACK = 104,
	LDLM_CP_CALLBACK = 105,
	LDLM_GL_CALLBACK = 106,
	LDLM_SET_INFO    = 107,
	LDLM_LAST_OPC
};
#define LDLM_FIRST_OPC LDLM_ENQUEUE

#define RES_NAME_SIZE 4
struct ldlm_res_id {
	__u64 name[RES_NAME_SIZE];
};

#define DLDLMRES	"[%#llx:%#llx:%#llx].%llx"
#define PLDLMRES(res)	(res)->lr_name.name[0], (res)->lr_name.name[1], \
			(res)->lr_name.name[2], (res)->lr_name.name[3]

static inline bool ldlm_res_eq(const struct ldlm_res_id *res0,
			       const struct ldlm_res_id *res1)
{
	return !memcmp(res0, res1, sizeof(*res0));
}

/* lock types */
enum ldlm_mode {
	LCK_MINMODE = 0,
	LCK_EX      = 1,
	LCK_PW      = 2,
	LCK_PR      = 4,
	LCK_CW      = 8,
	LCK_CR      = 16,
	LCK_NL      = 32,
	LCK_GROUP   = 64,
	LCK_COS     = 128,
	LCK_MAXMODE
};

#define LCK_MODE_NUM    8

enum ldlm_type {
	LDLM_PLAIN     = 10,
	LDLM_EXTENT    = 11,
	LDLM_FLOCK     = 12,
	LDLM_IBITS     = 13,
	LDLM_MAX_TYPE
};

#define LDLM_MIN_TYPE LDLM_PLAIN

struct ldlm_extent {
	__u64 start;
	__u64 end;
	__u64 gid;
};

#define LDLM_GID_ANY ((__u64)-1)

static inline int ldlm_extent_overlap(const struct ldlm_extent *ex1,
				      const struct ldlm_extent *ex2)
{
	return (ex1->start <= ex2->end) && (ex2->start <= ex1->end);
}

/* check if @ex1 contains @ex2 */
static inline int ldlm_extent_contain(const struct ldlm_extent *ex1,
				      const struct ldlm_extent *ex2)
{
	return (ex1->start <= ex2->start) && (ex1->end >= ex2->end);
}

struct ldlm_inodebits {
	__u64 bits;
};

struct ldlm_flock_wire {
	__u64 lfw_start;
	__u64 lfw_end;
	__u64 lfw_owner;
	__u32 lfw_padding;
	__u32 lfw_pid;
};

/* it's important that the fields of the ldlm_extent structure match
 * the first fields of the ldlm_flock structure because there is only
 * one ldlm_swab routine to process the ldlm_policy_data_t union. if
 * this ever changes we will need to swab the union differently based
 * on the resource type.
 */

typedef union {
	struct ldlm_extent l_extent;
	struct ldlm_flock_wire l_flock;
	struct ldlm_inodebits l_inodebits;
} ldlm_wire_policy_data_t;

union ldlm_gl_desc {
	struct ldlm_gl_lquota_desc	lquota_desc;
};

void lustre_swab_gl_desc(union ldlm_gl_desc *);

struct ldlm_intent {
	__u64 opc;
};

void lustre_swab_ldlm_intent(struct ldlm_intent *i);

struct ldlm_resource_desc {
	enum ldlm_type lr_type;
	__u32 lr_padding;       /* also fix lustre_swab_ldlm_resource_desc */
	struct ldlm_res_id lr_name;
};

struct ldlm_lock_desc {
	struct ldlm_resource_desc l_resource;
	enum ldlm_mode l_req_mode;
	enum ldlm_mode l_granted_mode;
	ldlm_wire_policy_data_t l_policy_data;
};

#define LDLM_LOCKREQ_HANDLES 2
#define LDLM_ENQUEUE_CANCEL_OFF 1

struct ldlm_request {
	__u32 lock_flags;
	__u32 lock_count;
	struct ldlm_lock_desc lock_desc;
	struct lustre_handle lock_handle[LDLM_LOCKREQ_HANDLES];
};

void lustre_swab_ldlm_request(struct ldlm_request *rq);

/* If LDLM_ENQUEUE, 1 slot is already occupied, 1 is available.
 * Otherwise, 2 are available.
 */
#define ldlm_request_bufsize(count, type)				\
({								      \
	int _avail = LDLM_LOCKREQ_HANDLES;			      \
	_avail -= (type == LDLM_ENQUEUE ? LDLM_ENQUEUE_CANCEL_OFF : 0); \
	sizeof(struct ldlm_request) +				   \
	(count > _avail ? count - _avail : 0) *			 \
	sizeof(struct lustre_handle);				   \
})

struct ldlm_reply {
	__u32 lock_flags;
	__u32 lock_padding;     /* also fix lustre_swab_ldlm_reply */
	struct ldlm_lock_desc lock_desc;
	struct lustre_handle lock_handle;
	__u64  lock_policy_res1;
	__u64  lock_policy_res2;
};

void lustre_swab_ldlm_reply(struct ldlm_reply *r);

#define ldlm_flags_to_wire(flags)    ((__u32)(flags))
#define ldlm_flags_from_wire(flags)  ((__u64)(flags))

/*
 * Opcodes for mountconf (mgs and mgc)
 */
enum mgs_cmd {
	MGS_CONNECT = 250,
	MGS_DISCONNECT,
	MGS_EXCEPTION,	 /* node died, etc. */
	MGS_TARGET_REG,	/* whenever target starts up */
	MGS_TARGET_DEL,
	MGS_SET_INFO,
	MGS_CONFIG_READ,
	MGS_LAST_OPC
};
#define MGS_FIRST_OPC MGS_CONNECT

#define MGS_PARAM_MAXLEN 1024
#define KEY_SET_INFO "set_info"

struct mgs_send_param {
	char	     mgs_param[MGS_PARAM_MAXLEN];
};

/* We pass this info to the MGS so it can write config logs */
#define MTI_NAME_MAXLEN  64
#define MTI_PARAM_MAXLEN 4096
#define MTI_NIDS_MAX     32
struct mgs_target_info {
	__u32	    mti_lustre_ver;
	__u32	    mti_stripe_index;
	__u32	    mti_config_ver;
	__u32	    mti_flags;
	__u32	    mti_nid_count;
	__u32	    mti_instance; /* Running instance of target */
	char	     mti_fsname[MTI_NAME_MAXLEN];
	char	     mti_svname[MTI_NAME_MAXLEN];
	char	     mti_uuid[sizeof(struct obd_uuid)];
	__u64	    mti_nids[MTI_NIDS_MAX];     /* host nids (lnet_nid_t)*/
	char	     mti_params[MTI_PARAM_MAXLEN];
};

void lustre_swab_mgs_target_info(struct mgs_target_info *oinfo);

struct mgs_nidtbl_entry {
	__u64	   mne_version;    /* table version of this entry */
	__u32	   mne_instance;   /* target instance # */
	__u32	   mne_index;      /* target index */
	__u32	   mne_length;     /* length of this entry - by bytes */
	__u8	    mne_type;       /* target type LDD_F_SV_TYPE_OST/MDT */
	__u8	    mne_nid_type;   /* type of nid(mbz). for ipv6. */
	__u8	    mne_nid_size;   /* size of each NID, by bytes */
	__u8	    mne_nid_count;  /* # of NIDs in buffer */
	union {
		lnet_nid_t nids[0];     /* variable size buffer for NIDs. */
	} u;
};

void lustre_swab_mgs_nidtbl_entry(struct mgs_nidtbl_entry *oinfo);

struct mgs_config_body {
	char     mcb_name[MTI_NAME_MAXLEN]; /* logname */
	__u64    mcb_offset;    /* next index of config log to request */
	__u16    mcb_type;      /* type of log: CONFIG_T_[CONFIG|RECOVER] */
	__u8     mcb_reserved;
	__u8     mcb_bits;      /* bits unit size of config log */
	__u32    mcb_units;     /* # of units for bulk transfer */
};

void lustre_swab_mgs_config_body(struct mgs_config_body *body);

struct mgs_config_res {
	__u64    mcr_offset;    /* index of last config log */
	__u64    mcr_size;      /* size of the log */
};

void lustre_swab_mgs_config_res(struct mgs_config_res *body);

/* Config marker flags (in config log) */
#define CM_START       0x01
#define CM_END	 0x02
#define CM_SKIP	0x04
#define CM_UPGRADE146  0x08
#define CM_EXCLUDE     0x10
#define CM_START_SKIP (CM_START | CM_SKIP)

struct cfg_marker {
	__u32	     cm_step;       /* aka config version */
	__u32	     cm_flags;
	__u32	     cm_vers;       /* lustre release version number */
	__u32	     cm_padding;    /* 64 bit align */
	__s64	     cm_createtime; /*when this record was first created */
	__s64	     cm_canceltime; /*when this record is no longer valid*/
	char	      cm_tgtname[MTI_NAME_MAXLEN];
	char	      cm_comment[MTI_NAME_MAXLEN];
};

void lustre_swab_cfg_marker(struct cfg_marker *marker, int swab, int size);

/*
 * Opcodes for multiple servers.
 */

enum obd_cmd {
	OBD_PING = 400,
	OBD_LOG_CANCEL,
	OBD_QC_CALLBACK,
	OBD_IDX_READ,
	OBD_LAST_OPC
};
#define OBD_FIRST_OPC OBD_PING

/* catalog of log objects */

/** Identifier for a single log object */
struct llog_logid {
	struct ost_id		lgl_oi;
	__u32		   lgl_ogen;
} __packed;

/** Records written to the CATALOGS list */
#define CATLIST "CATALOGS"
struct llog_catid {
	struct llog_logid       lci_logid;
	__u32		   lci_padding1;
	__u32		   lci_padding2;
	__u32		   lci_padding3;
} __packed;

/* Log data record types - there is no specific reason that these need to
 * be related to the RPC opcodes, but no reason not to (may be handy later?)
 */
#define LLOG_OP_MAGIC 0x10600000
#define LLOG_OP_MASK  0xfff00000

enum llog_op_type {
	LLOG_PAD_MAGIC		= LLOG_OP_MAGIC | 0x00000,
	OST_SZ_REC		= LLOG_OP_MAGIC | 0x00f00,
	/* OST_RAID1_REC	= LLOG_OP_MAGIC | 0x01000, never used */
	MDS_UNLINK_REC		= LLOG_OP_MAGIC | 0x10000 | (MDS_REINT << 8) |
				  REINT_UNLINK, /* obsolete after 2.5.0 */
	MDS_UNLINK64_REC	= LLOG_OP_MAGIC | 0x90000 | (MDS_REINT << 8) |
				  REINT_UNLINK,
	/* MDS_SETATTR_REC	= LLOG_OP_MAGIC | 0x12401, obsolete 1.8.0 */
	MDS_SETATTR64_REC	= LLOG_OP_MAGIC | 0x90000 | (MDS_REINT << 8) |
				  REINT_SETATTR,
	OBD_CFG_REC		= LLOG_OP_MAGIC | 0x20000,
	/* PTL_CFG_REC		= LLOG_OP_MAGIC | 0x30000, obsolete 1.4.0 */
	LLOG_GEN_REC		= LLOG_OP_MAGIC | 0x40000,
	/* LLOG_JOIN_REC	= LLOG_OP_MAGIC | 0x50000, obsolete  1.8.0 */
	CHANGELOG_REC		= LLOG_OP_MAGIC | 0x60000,
	CHANGELOG_USER_REC	= LLOG_OP_MAGIC | 0x70000,
	HSM_AGENT_REC		= LLOG_OP_MAGIC | 0x80000,
	LLOG_HDR_MAGIC		= LLOG_OP_MAGIC | 0x45539,
	LLOG_LOGID_MAGIC	= LLOG_OP_MAGIC | 0x4553b,
};

#define LLOG_REC_HDR_NEEDS_SWABBING(r) \
	(((r)->lrh_type & __swab32(LLOG_OP_MASK)) == __swab32(LLOG_OP_MAGIC))

/** Log record header - stored in little endian order.
 * Each record must start with this struct, end with a llog_rec_tail,
 * and be a multiple of 256 bits in size.
 */
struct llog_rec_hdr {
	__u32	lrh_len;
	__u32	lrh_index;
	__u32	lrh_type;
	__u32	lrh_id;
};

struct llog_rec_tail {
	__u32	lrt_len;
	__u32	lrt_index;
};

/* Where data follow just after header */
#define REC_DATA(ptr)						\
	((void *)((char *)ptr + sizeof(struct llog_rec_hdr)))

#define REC_DATA_LEN(rec)					\
	(rec->lrh_len - sizeof(struct llog_rec_hdr) -		\
	 sizeof(struct llog_rec_tail))

struct llog_logid_rec {
	struct llog_rec_hdr	lid_hdr;
	struct llog_logid	lid_id;
	__u32			lid_padding1;
	__u64			lid_padding2;
	__u64			lid_padding3;
	struct llog_rec_tail	lid_tail;
} __packed;

struct llog_unlink_rec {
	struct llog_rec_hdr	lur_hdr;
	__u64			lur_oid;
	__u32			lur_oseq;
	__u32			lur_count;
	struct llog_rec_tail	lur_tail;
} __packed;

struct llog_unlink64_rec {
	struct llog_rec_hdr	lur_hdr;
	struct lu_fid		lur_fid;
	__u32			lur_count; /* to destroy the lost precreated */
	__u32			lur_padding1;
	__u64			lur_padding2;
	__u64			lur_padding3;
	struct llog_rec_tail    lur_tail;
} __packed;

struct llog_setattr64_rec {
	struct llog_rec_hdr	lsr_hdr;
	struct ost_id		lsr_oi;
	__u32			lsr_uid;
	__u32			lsr_uid_h;
	__u32			lsr_gid;
	__u32			lsr_gid_h;
	__u64			lsr_padding;
	struct llog_rec_tail    lsr_tail;
} __packed;

struct llog_size_change_rec {
	struct llog_rec_hdr	lsc_hdr;
	struct ll_fid		lsc_fid;
	__u32			lsc_ioepoch;
	__u32			lsc_padding1;
	__u64			lsc_padding2;
	__u64			lsc_padding3;
	struct llog_rec_tail	lsc_tail;
} __packed;

/* changelog llog name, needed by client replicators */
#define CHANGELOG_CATALOG "changelog_catalog"

struct changelog_setinfo {
	__u64 cs_recno;
	__u32 cs_id;
} __packed;

/** changelog record */
struct llog_changelog_rec {
	struct llog_rec_hdr  cr_hdr;
	struct changelog_rec cr;
	struct llog_rec_tail cr_tail; /**< for_sizezof_only */
} __packed;

struct llog_changelog_ext_rec {
	struct llog_rec_hdr      cr_hdr;
	struct changelog_ext_rec cr;
	struct llog_rec_tail     cr_tail; /**< for_sizezof_only */
} __packed;

struct llog_changelog_user_rec {
	struct llog_rec_hdr   cur_hdr;
	__u32		 cur_id;
	__u32		 cur_padding;
	__u64		 cur_endrec;
	struct llog_rec_tail  cur_tail;
} __packed;

enum agent_req_status {
	ARS_WAITING,
	ARS_STARTED,
	ARS_FAILED,
	ARS_CANCELED,
	ARS_SUCCEED,
};

static inline const char *agent_req_status2name(const enum agent_req_status ars)
{
	switch (ars) {
	case ARS_WAITING:
		return "WAITING";
	case ARS_STARTED:
		return "STARTED";
	case ARS_FAILED:
		return "FAILED";
	case ARS_CANCELED:
		return "CANCELED";
	case ARS_SUCCEED:
		return "SUCCEED";
	default:
		return "UNKNOWN";
	}
}

static inline bool agent_req_in_final_state(enum agent_req_status ars)
{
	return ((ars == ARS_SUCCEED) || (ars == ARS_FAILED) ||
		(ars == ARS_CANCELED));
}

struct llog_agent_req_rec {
	struct llog_rec_hdr	arr_hdr;	/**< record header */
	__u32			arr_status;	/**< status of the request */
						/* must match enum
						 * agent_req_status
						 */
	__u32			arr_archive_id;	/**< backend archive number */
	__u64			arr_flags;	/**< req flags */
	__u64			arr_compound_id;/**< compound cookie */
	__u64			arr_req_create;	/**< req. creation time */
	__u64			arr_req_change;	/**< req. status change time */
	struct hsm_action_item	arr_hai;	/**< req. to the agent */
	struct llog_rec_tail	arr_tail;   /**< record tail for_sizezof_only */
} __packed;

/* Old llog gen for compatibility */
struct llog_gen {
	__u64 mnt_cnt;
	__u64 conn_cnt;
} __packed;

struct llog_gen_rec {
	struct llog_rec_hdr	lgr_hdr;
	struct llog_gen		lgr_gen;
	__u64			padding1;
	__u64			padding2;
	__u64			padding3;
	struct llog_rec_tail	lgr_tail;
};

/* On-disk header structure of each log object, stored in little endian order */
#define LLOG_CHUNK_SIZE	 8192
#define LLOG_HEADER_SIZE	(96)
#define LLOG_BITMAP_BYTES       (LLOG_CHUNK_SIZE - LLOG_HEADER_SIZE)

#define LLOG_MIN_REC_SIZE       (24) /* round(llog_rec_hdr + llog_rec_tail) */

/* flags for the logs */
enum llog_flag {
	LLOG_F_ZAP_WHEN_EMPTY	= 0x1,
	LLOG_F_IS_CAT		= 0x2,
	LLOG_F_IS_PLAIN		= 0x4,
};

struct llog_log_hdr {
	struct llog_rec_hdr     llh_hdr;
	__s64		   llh_timestamp;
	__u32		   llh_count;
	__u32		   llh_bitmap_offset;
	__u32		   llh_size;
	__u32		   llh_flags;
	__u32		   llh_cat_idx;
	/* for a catalog the first plain slot is next to it */
	struct obd_uuid	 llh_tgtuuid;
	__u32		   llh_reserved[LLOG_HEADER_SIZE/sizeof(__u32) - 23];
	__u32		   llh_bitmap[LLOG_BITMAP_BYTES/sizeof(__u32)];
	struct llog_rec_tail    llh_tail;
} __packed;

#define LLOG_BITMAP_SIZE(llh)  (__u32)((llh->llh_hdr.lrh_len -		\
					llh->llh_bitmap_offset -	\
					sizeof(llh->llh_tail)) * 8)

/** log cookies are used to reference a specific log file and a record
 * therein
 */
struct llog_cookie {
	struct llog_logid       lgc_lgl;
	__u32		   lgc_subsys;
	__u32		   lgc_index;
	__u32		   lgc_padding;
} __packed;

/** llog protocol */
enum llogd_rpc_ops {
	LLOG_ORIGIN_HANDLE_CREATE       = 501,
	LLOG_ORIGIN_HANDLE_NEXT_BLOCK   = 502,
	LLOG_ORIGIN_HANDLE_READ_HEADER  = 503,
	LLOG_ORIGIN_HANDLE_WRITE_REC    = 504,
	LLOG_ORIGIN_HANDLE_CLOSE	= 505,
	LLOG_ORIGIN_CONNECT		= 506,
	LLOG_CATINFO			= 507,  /* deprecated */
	LLOG_ORIGIN_HANDLE_PREV_BLOCK   = 508,
	LLOG_ORIGIN_HANDLE_DESTROY      = 509,  /* for destroy llog object*/
	LLOG_LAST_OPC,
	LLOG_FIRST_OPC		  = LLOG_ORIGIN_HANDLE_CREATE
};

struct llogd_body {
	struct llog_logid  lgd_logid;
	__u32 lgd_ctxt_idx;
	__u32 lgd_llh_flags;
	__u32 lgd_index;
	__u32 lgd_saved_index;
	__u32 lgd_len;
	__u64 lgd_cur_offset;
} __packed;

struct llogd_conn_body {
	struct llog_gen	 lgdc_gen;
	struct llog_logid       lgdc_logid;
	__u32		   lgdc_ctxt_idx;
} __packed;

/* Note: 64-bit types are 64-bit aligned in structure */
struct obdo {
	__u64		o_valid;	/* hot fields in this obdo */
	struct ost_id	o_oi;
	__u64		o_parent_seq;
	__u64		o_size;	 /* o_size-o_blocks == ost_lvb */
	__s64		o_mtime;
	__s64		o_atime;
	__s64		o_ctime;
	__u64		o_blocks;       /* brw: cli sent cached bytes */
	__u64		o_grant;

	/* 32-bit fields start here: keep an even number of them via padding */
	__u32		o_blksize;      /* optimal IO blocksize */
	__u32		o_mode;	 /* brw: cli sent cache remain */
	__u32		o_uid;
	__u32		o_gid;
	__u32		o_flags;
	__u32		o_nlink;	/* brw: checksum */
	__u32		o_parent_oid;
	__u32		o_misc;		/* brw: o_dropped */

	__u64		   o_ioepoch;      /* epoch in ost writes */
	__u32		   o_stripe_idx;   /* holds stripe idx */
	__u32		   o_parent_ver;
	struct lustre_handle    o_handle;  /* brw: lock handle to prolong locks
					    */
	struct llog_cookie      o_lcookie; /* destroy: unlink cookie from MDS
					    */
	__u32			o_uid_h;
	__u32			o_gid_h;

	__u64			o_data_version; /* getattr: sum of iversion for
						 * each stripe.
						 * brw: grant space consumed on
						 * the client for the write
						 */
	__u64			o_padding_4;
	__u64			o_padding_5;
	__u64			o_padding_6;
};

#define o_dirty   o_blocks
#define o_undirty o_mode
#define o_dropped o_misc
#define o_cksum   o_nlink
#define o_grant_used o_data_version

static inline void lustre_set_wire_obdo(const struct obd_connect_data *ocd,
					struct obdo *wobdo,
					const struct obdo *lobdo)
{
	*wobdo = *lobdo;
	wobdo->o_flags &= ~OBD_FL_LOCAL_MASK;
	if (!ocd)
		return;

	if (unlikely(!(ocd->ocd_connect_flags & OBD_CONNECT_FID)) &&
	    fid_seq_is_echo(ostid_seq(&lobdo->o_oi))) {
		/* Currently OBD_FL_OSTID will only be used when 2.4 echo
		 * client communicate with pre-2.4 server
		 */
		wobdo->o_oi.oi.oi_id = fid_oid(&lobdo->o_oi.oi_fid);
		wobdo->o_oi.oi.oi_seq = fid_seq(&lobdo->o_oi.oi_fid);
	}
}

static inline void lustre_get_wire_obdo(const struct obd_connect_data *ocd,
					struct obdo *lobdo,
					const struct obdo *wobdo)
{
	__u32 local_flags = 0;

	if (lobdo->o_valid & OBD_MD_FLFLAGS)
		local_flags = lobdo->o_flags & OBD_FL_LOCAL_MASK;

	*lobdo = *wobdo;
	if (local_flags != 0) {
		lobdo->o_valid |= OBD_MD_FLFLAGS;
		lobdo->o_flags &= ~OBD_FL_LOCAL_MASK;
		lobdo->o_flags |= local_flags;
	}
	if (!ocd)
		return;

	if (unlikely(!(ocd->ocd_connect_flags & OBD_CONNECT_FID)) &&
	    fid_seq_is_echo(wobdo->o_oi.oi.oi_seq)) {
		/* see above */
		lobdo->o_oi.oi_fid.f_seq = wobdo->o_oi.oi.oi_seq;
		lobdo->o_oi.oi_fid.f_oid = wobdo->o_oi.oi.oi_id;
		lobdo->o_oi.oi_fid.f_ver = 0;
	}
}

/* request structure for OST's */
struct ost_body {
	struct  obdo oa;
};

/* Key for FIEMAP to be used in get_info calls */
struct ll_fiemap_info_key {
	char    name[8];
	struct  obdo oa;
	struct  ll_user_fiemap fiemap;
};

void lustre_swab_ost_body(struct ost_body *b);
void lustre_swab_ost_last_id(__u64 *id);
void lustre_swab_fiemap(struct ll_user_fiemap *fiemap);

void lustre_swab_lov_user_md_v1(struct lov_user_md_v1 *lum);
void lustre_swab_lov_user_md_v3(struct lov_user_md_v3 *lum);
void lustre_swab_lov_user_md_objects(struct lov_user_ost_data *lod,
				     int stripe_count);
void lustre_swab_lov_mds_md(struct lov_mds_md *lmm);

/* llog_swab.c */
void lustre_swab_llogd_body(struct llogd_body *d);
void lustre_swab_llog_hdr(struct llog_log_hdr *h);
void lustre_swab_llogd_conn_body(struct llogd_conn_body *d);
void lustre_swab_llog_rec(struct llog_rec_hdr *rec);

struct lustre_cfg;
void lustre_swab_lustre_cfg(struct lustre_cfg *lcfg);

/* Functions for dumping PTLRPC fields */
void dump_rniobuf(struct niobuf_remote *rnb);
void dump_ioo(struct obd_ioobj *nb);
void dump_ost_body(struct ost_body *ob);
void dump_rcs(__u32 *rc);

/* security opcodes */
enum sec_cmd {
	SEC_CTX_INIT	    = 801,
	SEC_CTX_INIT_CONT       = 802,
	SEC_CTX_FINI	    = 803,
	SEC_LAST_OPC,
	SEC_FIRST_OPC	   = SEC_CTX_INIT
};

/*
 * capa related definitions
 */
#define CAPA_HMAC_MAX_LEN       64
#define CAPA_HMAC_KEY_MAX_LEN   56

/* NB take care when changing the sequence of elements this struct,
 * because the offset info is used in find_capa()
 */
struct lustre_capa {
	struct lu_fid   lc_fid;	 /** fid */
	__u64	   lc_opc;	 /** operations allowed */
	__u64	   lc_uid;	 /** file owner */
	__u64	   lc_gid;	 /** file group */
	__u32	   lc_flags;       /** HMAC algorithm & flags */
	__u32	   lc_keyid;       /** key# used for the capability */
	__u32	   lc_timeout;     /** capa timeout value (sec) */
/* FIXME: y2038 time_t overflow: */
	__u32	   lc_expiry;      /** expiry time (sec) */
	__u8	    lc_hmac[CAPA_HMAC_MAX_LEN];   /** HMAC */
} __packed;

void lustre_swab_lustre_capa(struct lustre_capa *c);

/** lustre_capa::lc_opc */
enum {
	CAPA_OPC_BODY_WRITE   = 1<<0,  /**< write object data */
	CAPA_OPC_BODY_READ    = 1<<1,  /**< read object data */
	CAPA_OPC_INDEX_LOOKUP = 1<<2,  /**< lookup object fid */
	CAPA_OPC_INDEX_INSERT = 1<<3,  /**< insert object fid */
	CAPA_OPC_INDEX_DELETE = 1<<4,  /**< delete object fid */
	CAPA_OPC_OSS_WRITE    = 1<<5,  /**< write oss object data */
	CAPA_OPC_OSS_READ     = 1<<6,  /**< read oss object data */
	CAPA_OPC_OSS_TRUNC    = 1<<7,  /**< truncate oss object */
	CAPA_OPC_OSS_DESTROY  = 1<<8,  /**< destroy oss object */
	CAPA_OPC_META_WRITE   = 1<<9,  /**< write object meta data */
	CAPA_OPC_META_READ    = 1<<10, /**< read object meta data */
};

#define CAPA_OPC_OSS_RW (CAPA_OPC_OSS_READ | CAPA_OPC_OSS_WRITE)
#define CAPA_OPC_MDS_ONLY						   \
	(CAPA_OPC_BODY_WRITE | CAPA_OPC_BODY_READ | CAPA_OPC_INDEX_LOOKUP | \
	 CAPA_OPC_INDEX_INSERT | CAPA_OPC_INDEX_DELETE)
#define CAPA_OPC_OSS_ONLY						   \
	(CAPA_OPC_OSS_WRITE | CAPA_OPC_OSS_READ | CAPA_OPC_OSS_TRUNC |      \
	 CAPA_OPC_OSS_DESTROY)
#define CAPA_OPC_MDS_DEFAULT ~CAPA_OPC_OSS_ONLY
#define CAPA_OPC_OSS_DEFAULT ~(CAPA_OPC_MDS_ONLY | CAPA_OPC_OSS_ONLY)

struct lustre_capa_key {
	__u64   lk_seq;       /**< mds# */
	__u32   lk_keyid;     /**< key# */
	__u32   lk_padding;
	__u8    lk_key[CAPA_HMAC_KEY_MAX_LEN];    /**< key */
} __packed;

/** The link ea holds 1 \a link_ea_entry for each hardlink */
#define LINK_EA_MAGIC 0x11EAF1DFUL
struct link_ea_header {
	__u32 leh_magic;
	__u32 leh_reccount;
	__u64 leh_len;      /* total size */
	/* future use */
	__u32 padding1;
	__u32 padding2;
};

/** Hardlink data is name and parent fid.
 * Stored in this crazy struct for maximum packing and endian-neutrality
 */
struct link_ea_entry {
	/** __u16 stored big-endian, unaligned */
	unsigned char      lee_reclen[2];
	unsigned char      lee_parent_fid[sizeof(struct lu_fid)];
	char	       lee_name[0];
} __packed;

/** fid2path request/reply structure */
struct getinfo_fid2path {
	struct lu_fid   gf_fid;
	__u64	   gf_recno;
	__u32	   gf_linkno;
	__u32	   gf_pathlen;
	char	    gf_path[0];
} __packed;

void lustre_swab_fid2path(struct getinfo_fid2path *gf);

enum {
	LAYOUT_INTENT_ACCESS    = 0,
	LAYOUT_INTENT_READ      = 1,
	LAYOUT_INTENT_WRITE     = 2,
	LAYOUT_INTENT_GLIMPSE   = 3,
	LAYOUT_INTENT_TRUNC     = 4,
	LAYOUT_INTENT_RELEASE   = 5,
	LAYOUT_INTENT_RESTORE   = 6
};

/* enqueue layout lock with intent */
struct layout_intent {
	__u32 li_opc; /* intent operation for enqueue, read, write etc */
	__u32 li_flags;
	__u64 li_start;
	__u64 li_end;
};

void lustre_swab_layout_intent(struct layout_intent *li);

/**
 * On the wire version of hsm_progress structure.
 *
 * Contains the userspace hsm_progress and some internal fields.
 */
struct hsm_progress_kernel {
	/* Field taken from struct hsm_progress */
	struct lu_fid		hpk_fid;
	__u64			hpk_cookie;
	struct hsm_extent	hpk_extent;
	__u16			hpk_flags;
	__u16			hpk_errval; /* positive val */
	__u32			hpk_padding1;
	/* Additional fields */
	__u64			hpk_data_version;
	__u64			hpk_padding2;
} __packed;

void lustre_swab_hsm_user_state(struct hsm_user_state *hus);
void lustre_swab_hsm_current_action(struct hsm_current_action *action);
void lustre_swab_hsm_progress_kernel(struct hsm_progress_kernel *hpk);
void lustre_swab_hsm_user_state(struct hsm_user_state *hus);
void lustre_swab_hsm_user_item(struct hsm_user_item *hui);
void lustre_swab_hsm_request(struct hsm_request *hr);

/** layout swap request structure
 * fid1 and fid2 are in mdt_body
 */
struct mdc_swap_layouts {
	__u64	   msl_flags;
} __packed;

void lustre_swab_swap_layouts(struct mdc_swap_layouts *msl);

struct close_data {
	struct lustre_handle	cd_handle;
	struct lu_fid		cd_fid;
	__u64			cd_data_version;
	__u64			cd_reserved[8];
};

void lustre_swab_close_data(struct close_data *data);

#endif
/** @} lustreidl */
