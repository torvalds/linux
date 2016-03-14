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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/obdclass/acl.c
 *
 * Lustre Access Control List.
 *
 * Author: Fan Yong <fanyong@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_SEC
#include "../include/lu_object.h"
#include "../include/lustre_acl.h"
#include "../include/lustre_eacl.h"
#include "../include/obd_support.h"

#ifdef CONFIG_FS_POSIX_ACL

#define CFS_ACL_XATTR_VERSION POSIX_ACL_XATTR_VERSION

enum {
	ES_UNK  = 0,    /* unknown stat */
	ES_UNC  = 1,    /* ACL entry is not changed */
	ES_MOD  = 2,    /* ACL entry is modified */
	ES_ADD  = 3,    /* ACL entry is added */
	ES_DEL  = 4     /* ACL entry is deleted */
};

static inline void lustre_ext_acl_le_to_cpu(ext_acl_xattr_entry *d,
					    ext_acl_xattr_entry *s)
{
	d->e_tag	= le16_to_cpu(s->e_tag);
	d->e_perm       = le16_to_cpu(s->e_perm);
	d->e_id	 = le32_to_cpu(s->e_id);
	d->e_stat       = le32_to_cpu(s->e_stat);
}

static inline void lustre_ext_acl_cpu_to_le(ext_acl_xattr_entry *d,
					    ext_acl_xattr_entry *s)
{
	d->e_tag	= cpu_to_le16(s->e_tag);
	d->e_perm       = cpu_to_le16(s->e_perm);
	d->e_id	 = cpu_to_le32(s->e_id);
	d->e_stat       = cpu_to_le32(s->e_stat);
}

static inline void lustre_posix_acl_le_to_cpu(posix_acl_xattr_entry *d,
					      posix_acl_xattr_entry *s)
{
	d->e_tag	= le16_to_cpu(s->e_tag);
	d->e_perm       = le16_to_cpu(s->e_perm);
	d->e_id	 = le32_to_cpu(s->e_id);
}

static inline void lustre_posix_acl_cpu_to_le(posix_acl_xattr_entry *d,
					      posix_acl_xattr_entry *s)
{
	d->e_tag	= cpu_to_le16(s->e_tag);
	d->e_perm       = cpu_to_le16(s->e_perm);
	d->e_id	 = cpu_to_le32(s->e_id);
}

/* if "new_count == 0", then "new = {a_version, NULL}", NOT NULL. */
static int lustre_posix_acl_xattr_reduce_space(posix_acl_xattr_header **header,
					       int old_count, int new_count)
{
	int old_size = CFS_ACL_XATTR_SIZE(old_count, posix_acl_xattr);
	int new_size = CFS_ACL_XATTR_SIZE(new_count, posix_acl_xattr);
	posix_acl_xattr_header *new;

	if (unlikely(old_count <= new_count))
		return old_size;

	new = kmemdup(*header, new_size, GFP_NOFS);
	if (unlikely(new == NULL))
		return -ENOMEM;

	kfree(*header);
	*header = new;
	return new_size;
}

/* if "new_count == 0", then "new = {0, NULL}", NOT NULL. */
static int lustre_ext_acl_xattr_reduce_space(ext_acl_xattr_header **header,
					     int old_count)
{
	int ext_count = le32_to_cpu((*header)->a_count);
	int ext_size = CFS_ACL_XATTR_SIZE(ext_count, ext_acl_xattr);
	ext_acl_xattr_header *new;

	if (unlikely(old_count <= ext_count))
		return 0;

	new = kmemdup(*header, ext_size, GFP_NOFS);
	if (unlikely(new == NULL))
		return -ENOMEM;

	kfree(*header);
	*header = new;
	return 0;
}

/*
 * Generate new extended ACL based on the posix ACL.
 */
ext_acl_xattr_header *
lustre_posix_acl_xattr_2ext(posix_acl_xattr_header *header, int size)
{
	int count, i, esize;
	ext_acl_xattr_header *new;

	if (unlikely(size < 0))
		return ERR_PTR(-EINVAL);
	else if (!size)
		count = 0;
	else
		count = CFS_ACL_XATTR_COUNT(size, posix_acl_xattr);
	esize = CFS_ACL_XATTR_SIZE(count, ext_acl_xattr);
	new = kzalloc(esize, GFP_NOFS);
	if (unlikely(new == NULL))
		return ERR_PTR(-ENOMEM);

	new->a_count = cpu_to_le32(count);
	for (i = 0; i < count; i++) {
		new->a_entries[i].e_tag  = header->a_entries[i].e_tag;
		new->a_entries[i].e_perm = header->a_entries[i].e_perm;
		new->a_entries[i].e_id   = header->a_entries[i].e_id;
		new->a_entries[i].e_stat = cpu_to_le32(ES_UNK);
	}

	return new;
}
EXPORT_SYMBOL(lustre_posix_acl_xattr_2ext);

/*
 * Filter out the "nobody" entries in the posix ACL.
 */
int lustre_posix_acl_xattr_filter(posix_acl_xattr_header *header, size_t size,
				  posix_acl_xattr_header **out)
{
	int count, i, j, rc = 0;
	__u32 id;
	posix_acl_xattr_header *new;

	if (!size)
		return 0;
	if (size < sizeof(*new))
		return -EINVAL;

	new = kzalloc(size, GFP_NOFS);
	if (unlikely(new == NULL))
		return -ENOMEM;

	new->a_version = cpu_to_le32(CFS_ACL_XATTR_VERSION);
	count = CFS_ACL_XATTR_COUNT(size, posix_acl_xattr);
	for (i = 0, j = 0; i < count; i++) {
		id = le32_to_cpu(header->a_entries[i].e_id);
		switch (le16_to_cpu(header->a_entries[i].e_tag)) {
		case ACL_USER_OBJ:
		case ACL_GROUP_OBJ:
		case ACL_MASK:
		case ACL_OTHER:
			if (id != ACL_UNDEFINED_ID) {
				rc = -EIO;
				goto _out;
			}

			memcpy(&new->a_entries[j++], &header->a_entries[i],
			       sizeof(posix_acl_xattr_entry));
			break;
		case ACL_USER:
			if (id != NOBODY_UID)
				memcpy(&new->a_entries[j++],
				       &header->a_entries[i],
				       sizeof(posix_acl_xattr_entry));
			break;
		case ACL_GROUP:
			if (id != NOBODY_GID)
				memcpy(&new->a_entries[j++],
				       &header->a_entries[i],
				       sizeof(posix_acl_xattr_entry));
			break;
		default:
			rc = -EIO;
			goto _out;
		}
	}

	/* free unused space. */
	rc = lustre_posix_acl_xattr_reduce_space(&new, count, j);
	if (rc >= 0) {
		size = rc;
		*out = new;
		rc = 0;
	}

_out:
	if (rc) {
		kfree(new);
		size = rc;
	}
	return size;
}
EXPORT_SYMBOL(lustre_posix_acl_xattr_filter);

/*
 * Release the extended ACL space.
 */
void lustre_ext_acl_xattr_free(ext_acl_xattr_header *header)
{
	kfree(header);
}
EXPORT_SYMBOL(lustre_ext_acl_xattr_free);

static ext_acl_xattr_entry *
lustre_ext_acl_xattr_search(ext_acl_xattr_header *header,
			    posix_acl_xattr_entry *entry, int *pos)
{
	int once, start, end, i, j, count = le32_to_cpu(header->a_count);

	once = 0;
	start = *pos;
	end = count;

again:
	for (i = start; i < end; i++) {
		if (header->a_entries[i].e_tag == entry->e_tag &&
		    header->a_entries[i].e_id == entry->e_id) {
			j = i;
			if (++i >= count)
				i = 0;
			*pos = i;
			return &header->a_entries[j];
		}
	}

	if (!once) {
		once = 1;
		start = 0;
		end = *pos;
		goto again;
	}

	return NULL;
}

/*
 * Merge the posix ACL and the extended ACL into new extended ACL.
 */
ext_acl_xattr_header *
lustre_acl_xattr_merge2ext(posix_acl_xattr_header *posix_header, int size,
			   ext_acl_xattr_header *ext_header)
{
	int ori_ext_count, posix_count, ext_count, ext_size;
	int i, j, pos = 0, rc = 0;
	posix_acl_xattr_entry pae;
	ext_acl_xattr_header *new;
	ext_acl_xattr_entry *ee, eae;

	if (unlikely(size < 0))
		return ERR_PTR(-EINVAL);
	else if (!size)
		posix_count = 0;
	else
		posix_count = CFS_ACL_XATTR_COUNT(size, posix_acl_xattr);
	ori_ext_count = le32_to_cpu(ext_header->a_count);
	ext_count = posix_count + ori_ext_count;
	ext_size = CFS_ACL_XATTR_SIZE(ext_count, ext_acl_xattr);

	new = kzalloc(ext_size, GFP_NOFS);
	if (unlikely(new == NULL))
		return ERR_PTR(-ENOMEM);

	for (i = 0, j = 0; i < posix_count; i++) {
		lustre_posix_acl_le_to_cpu(&pae, &posix_header->a_entries[i]);
		switch (pae.e_tag) {
		case ACL_USER_OBJ:
		case ACL_GROUP_OBJ:
		case ACL_MASK:
		case ACL_OTHER:
			if (pae.e_id != ACL_UNDEFINED_ID) {
				rc = -EIO;
				goto out;
		}
		case ACL_USER:
			/* ignore "nobody" entry. */
			if (pae.e_id == NOBODY_UID)
				break;

			new->a_entries[j].e_tag =
					posix_header->a_entries[i].e_tag;
			new->a_entries[j].e_perm =
					posix_header->a_entries[i].e_perm;
			new->a_entries[j].e_id =
					posix_header->a_entries[i].e_id;
			ee = lustre_ext_acl_xattr_search(ext_header,
					&posix_header->a_entries[i], &pos);
			if (ee) {
				if (posix_header->a_entries[i].e_perm !=
								ee->e_perm)
					/* entry modified. */
					ee->e_stat =
					new->a_entries[j++].e_stat =
							cpu_to_le32(ES_MOD);
				else
					/* entry unchanged. */
					ee->e_stat =
					new->a_entries[j++].e_stat =
							cpu_to_le32(ES_UNC);
			} else {
				/* new entry. */
				new->a_entries[j++].e_stat =
							cpu_to_le32(ES_ADD);
			}
			break;
		case ACL_GROUP:
			/* ignore "nobody" entry. */
			if (pae.e_id == NOBODY_GID)
				break;
			new->a_entries[j].e_tag =
					posix_header->a_entries[i].e_tag;
			new->a_entries[j].e_perm =
					posix_header->a_entries[i].e_perm;
			new->a_entries[j].e_id =
					posix_header->a_entries[i].e_id;
			ee = lustre_ext_acl_xattr_search(ext_header,
					&posix_header->a_entries[i], &pos);
			if (ee) {
				if (posix_header->a_entries[i].e_perm !=
								ee->e_perm)
					/* entry modified. */
					ee->e_stat =
					new->a_entries[j++].e_stat =
							cpu_to_le32(ES_MOD);
				else
					/* entry unchanged. */
					ee->e_stat =
					new->a_entries[j++].e_stat =
							cpu_to_le32(ES_UNC);
			} else {
				/* new entry. */
				new->a_entries[j++].e_stat =
							cpu_to_le32(ES_ADD);
			}
			break;
		default:
			rc = -EIO;
			goto out;
		}
	}

	/* process deleted entries. */
	for (i = 0; i < ori_ext_count; i++) {
		lustre_ext_acl_le_to_cpu(&eae, &ext_header->a_entries[i]);
		if (eae.e_stat == ES_UNK) {
			/* ignore "nobody" entry. */
			if ((eae.e_tag == ACL_USER && eae.e_id == NOBODY_UID) ||
			    (eae.e_tag == ACL_GROUP && eae.e_id == NOBODY_GID))
				continue;

			new->a_entries[j].e_tag =
						ext_header->a_entries[i].e_tag;
			new->a_entries[j].e_perm =
						ext_header->a_entries[i].e_perm;
			new->a_entries[j].e_id = ext_header->a_entries[i].e_id;
			new->a_entries[j++].e_stat = cpu_to_le32(ES_DEL);
		}
	}

	new->a_count = cpu_to_le32(j);
	/* free unused space. */
	rc = lustre_ext_acl_xattr_reduce_space(&new, ext_count);

out:
	if (rc) {
		kfree(new);
		new = ERR_PTR(rc);
	}
	return new;
}
EXPORT_SYMBOL(lustre_acl_xattr_merge2ext);

#endif
