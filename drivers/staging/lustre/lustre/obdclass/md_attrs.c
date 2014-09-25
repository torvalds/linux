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
 * version 2 along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2012, Intel Corporation.
 * Use is subject to license terms.
 *
 * Author: Johann Lombardi <johann.lombardi@intel.com>
 */

#include "../include/lustre/lustre_idl.h"
#include "../include/obd.h"
#include "../include/md_object.h"

/**
 * Initialize new \a lma. Only fid is stored.
 *
 * \param lma - is the new LMA structure to be initialized
 * \param fid - is the FID of the object this LMA belongs to
 * \param incompat - features that MDS must understand to access object
 */
void lustre_lma_init(struct lustre_mdt_attrs *lma, const struct lu_fid *fid,
		     __u32 incompat)
{
	lma->lma_compat   = 0;
	lma->lma_incompat = incompat;
	lma->lma_self_fid = *fid;

	/* If a field is added in struct lustre_mdt_attrs, zero it explicitly
	 * and change the test below. */
	LASSERT(sizeof(*lma) ==
		(offsetof(struct lustre_mdt_attrs, lma_self_fid) +
		 sizeof(lma->lma_self_fid)));
};
EXPORT_SYMBOL(lustre_lma_init);

/**
 * Swab, if needed, LMA structure which is stored on-disk in little-endian order.
 *
 * \param lma - is a pointer to the LMA structure to be swabbed.
 */
void lustre_lma_swab(struct lustre_mdt_attrs *lma)
{
	/* Use LUSTRE_MSG_MAGIC to detect local endianness. */
	if (LUSTRE_MSG_MAGIC != cpu_to_le32(LUSTRE_MSG_MAGIC)) {
		__swab32s(&lma->lma_compat);
		__swab32s(&lma->lma_incompat);
		lustre_swab_lu_fid(&lma->lma_self_fid);
	}
};
EXPORT_SYMBOL(lustre_lma_swab);

/**
 * Swab, if needed, SOM structure which is stored on-disk in little-endian
 * order.
 *
 * \param attrs - is a pointer to the SOM structure to be swabbed.
 */
void lustre_som_swab(struct som_attrs *attrs)
{
	/* Use LUSTRE_MSG_MAGIC to detect local endianness. */
	if (LUSTRE_MSG_MAGIC != cpu_to_le32(LUSTRE_MSG_MAGIC)) {
		__swab32s(&attrs->som_compat);
		__swab32s(&attrs->som_incompat);
		__swab64s(&attrs->som_ioepoch);
		__swab64s(&attrs->som_size);
		__swab64s(&attrs->som_blocks);
		__swab64s(&attrs->som_mountid);
	}
};
EXPORT_SYMBOL(lustre_som_swab);

/*
 * Swab and extract SOM attributes from on-disk xattr.
 *
 * \param buf - is a buffer containing the on-disk SOM extended attribute.
 * \param rc  - is the SOM xattr stored in \a buf
 * \param msd - is the md_som_data structure where to extract SOM attributes.
 */
int lustre_buf2som(void *buf, int rc, struct md_som_data *msd)
{
	struct som_attrs *attrs = (struct som_attrs *)buf;

	if (rc == 0 ||  rc == -ENODATA)
		/* no SOM attributes */
		return -ENODATA;

	if (rc < 0)
		/* error hit while fetching xattr */
		return rc;

	/* check SOM compatibility */
	if (attrs->som_incompat & ~cpu_to_le32(SOM_INCOMPAT_SUPP))
		return -ENODATA;

	/* unpack SOM attributes */
	lustre_som_swab(attrs);

	/* fill in-memory msd structure */
	msd->msd_compat   = attrs->som_compat;
	msd->msd_incompat = attrs->som_incompat;
	msd->msd_ioepoch  = attrs->som_ioepoch;
	msd->msd_size     = attrs->som_size;
	msd->msd_blocks   = attrs->som_blocks;
	msd->msd_mountid  = attrs->som_mountid;

	return 0;
}
EXPORT_SYMBOL(lustre_buf2som);

/**
 * Swab, if needed, HSM structure which is stored on-disk in little-endian
 * order.
 *
 * \param attrs - is a pointer to the HSM structure to be swabbed.
 */
void lustre_hsm_swab(struct hsm_attrs *attrs)
{
	/* Use LUSTRE_MSG_MAGIC to detect local endianness. */
	if (LUSTRE_MSG_MAGIC != cpu_to_le32(LUSTRE_MSG_MAGIC)) {
		__swab32s(&attrs->hsm_compat);
		__swab32s(&attrs->hsm_flags);
		__swab64s(&attrs->hsm_arch_id);
		__swab64s(&attrs->hsm_arch_ver);
	}
};
EXPORT_SYMBOL(lustre_hsm_swab);

/*
 * Swab and extract HSM attributes from on-disk xattr.
 *
 * \param buf - is a buffer containing the on-disk HSM extended attribute.
 * \param rc  - is the HSM xattr stored in \a buf
 * \param mh  - is the md_hsm structure where to extract HSM attributes.
 */
int lustre_buf2hsm(void *buf, int rc, struct md_hsm *mh)
{
	struct hsm_attrs *attrs = (struct hsm_attrs *)buf;

	if (rc == 0 ||  rc == -ENODATA)
		/* no HSM attributes */
		return -ENODATA;

	if (rc < 0)
		/* error hit while fetching xattr */
		return rc;

	/* unpack HSM attributes */
	lustre_hsm_swab(attrs);

	/* fill md_hsm structure */
	mh->mh_compat   = attrs->hsm_compat;
	mh->mh_flags    = attrs->hsm_flags;
	mh->mh_arch_id  = attrs->hsm_arch_id;
	mh->mh_arch_ver = attrs->hsm_arch_ver;

	return 0;
}
EXPORT_SYMBOL(lustre_buf2hsm);

/*
 * Pack HSM attributes.
 *
 * \param buf - is the output buffer where to pack the on-disk HSM xattr.
 * \param mh  - is the md_hsm structure to pack.
 */
void lustre_hsm2buf(void *buf, struct md_hsm *mh)
{
	struct hsm_attrs *attrs = (struct hsm_attrs *)buf;

	/* copy HSM attributes */
	attrs->hsm_compat   = mh->mh_compat;
	attrs->hsm_flags    = mh->mh_flags;
	attrs->hsm_arch_id  = mh->mh_arch_id;
	attrs->hsm_arch_ver = mh->mh_arch_ver;

	/* pack xattr */
	lustre_hsm_swab(attrs);
}
EXPORT_SYMBOL(lustre_hsm2buf);
