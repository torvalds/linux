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
 * Copyright (c) 2013, 2014, Intel Corporation.
 * Use is subject to license terms.
 *
 * Author: Di Wang <di.wang@intel.com>
 */

#include "../include/lustre/lustre_idl.h"
#include "../include/obd.h"
#include "../include/lustre_linkea.h"

int linkea_data_new(struct linkea_data *ldata, struct lu_buf *buf)
{
	ldata->ld_buf = lu_buf_check_and_alloc(buf, PAGE_SIZE);
	if (!ldata->ld_buf->lb_buf)
		return -ENOMEM;
	ldata->ld_leh = ldata->ld_buf->lb_buf;
	ldata->ld_leh->leh_magic = LINK_EA_MAGIC;
	ldata->ld_leh->leh_len = sizeof(struct link_ea_header);
	ldata->ld_leh->leh_reccount = 0;
	return 0;
}
EXPORT_SYMBOL(linkea_data_new);

int linkea_init(struct linkea_data *ldata)
{
	struct link_ea_header *leh;

	LASSERT(ldata->ld_buf);
	leh = ldata->ld_buf->lb_buf;
	if (leh->leh_magic == __swab32(LINK_EA_MAGIC)) {
		leh->leh_magic = LINK_EA_MAGIC;
		leh->leh_reccount = __swab32(leh->leh_reccount);
		leh->leh_len = __swab64(leh->leh_len);
		/* entries are swabbed by linkea_entry_unpack */
	}
	if (leh->leh_magic != LINK_EA_MAGIC)
		return -EINVAL;
	if (leh->leh_reccount == 0)
		return -ENODATA;

	ldata->ld_leh = leh;
	return 0;
}
EXPORT_SYMBOL(linkea_init);

/**
 * Pack a link_ea_entry.
 * All elements are stored as chars to avoid alignment issues.
 * Numbers are always big-endian
 * \retval record length
 */
int linkea_entry_pack(struct link_ea_entry *lee, const struct lu_name *lname,
		      const struct lu_fid *pfid)
{
	struct lu_fid   tmpfid;
	int             reclen;

	tmpfid = *pfid;
	if (OBD_FAIL_CHECK(OBD_FAIL_LFSCK_LINKEA_CRASH))
		tmpfid.f_ver = ~0;
	fid_cpu_to_be(&tmpfid, &tmpfid);
	memcpy(&lee->lee_parent_fid, &tmpfid, sizeof(tmpfid));
	memcpy(lee->lee_name, lname->ln_name, lname->ln_namelen);
	reclen = sizeof(struct link_ea_entry) + lname->ln_namelen;

	lee->lee_reclen[0] = (reclen >> 8) & 0xff;
	lee->lee_reclen[1] = reclen & 0xff;
	return reclen;
}
EXPORT_SYMBOL(linkea_entry_pack);

void linkea_entry_unpack(const struct link_ea_entry *lee, int *reclen,
			 struct lu_name *lname, struct lu_fid *pfid)
{
	*reclen = (lee->lee_reclen[0] << 8) | lee->lee_reclen[1];
	memcpy(pfid, &lee->lee_parent_fid, sizeof(*pfid));
	fid_be_to_cpu(pfid, pfid);
	if (lname) {
		lname->ln_name = lee->lee_name;
		lname->ln_namelen = *reclen - sizeof(struct link_ea_entry);
	}
}
EXPORT_SYMBOL(linkea_entry_unpack);

/**
 * Add a record to the end of link ea buf
 **/
int linkea_add_buf(struct linkea_data *ldata, const struct lu_name *lname,
		   const struct lu_fid *pfid)
{
	LASSERT(ldata->ld_leh);

	if (!lname || !pfid)
		return -EINVAL;

	ldata->ld_reclen = lname->ln_namelen + sizeof(struct link_ea_entry);
	if (ldata->ld_leh->leh_len + ldata->ld_reclen >
	    ldata->ld_buf->lb_len) {
		if (lu_buf_check_and_grow(ldata->ld_buf,
					  ldata->ld_leh->leh_len +
					  ldata->ld_reclen) < 0)
			return -ENOMEM;
	}

	ldata->ld_leh = ldata->ld_buf->lb_buf;
	ldata->ld_lee = ldata->ld_buf->lb_buf + ldata->ld_leh->leh_len;
	ldata->ld_reclen = linkea_entry_pack(ldata->ld_lee, lname, pfid);
	ldata->ld_leh->leh_len += ldata->ld_reclen;
	ldata->ld_leh->leh_reccount++;
	CDEBUG(D_INODE, "New link_ea name '" DFID ":%.*s' is added\n",
	       PFID(pfid), lname->ln_namelen, lname->ln_name);
	return 0;
}
EXPORT_SYMBOL(linkea_add_buf);

/** Del the current record from the link ea buf */
void linkea_del_buf(struct linkea_data *ldata, const struct lu_name *lname)
{
	LASSERT(ldata->ld_leh && ldata->ld_lee);

	ldata->ld_leh->leh_reccount--;
	ldata->ld_leh->leh_len -= ldata->ld_reclen;
	memmove(ldata->ld_lee, (char *)ldata->ld_lee + ldata->ld_reclen,
		(char *)ldata->ld_leh + ldata->ld_leh->leh_len -
		(char *)ldata->ld_lee);
	CDEBUG(D_INODE, "Old link_ea name '%.*s' is removed\n",
	       lname->ln_namelen, lname->ln_name);

	if ((char *)ldata->ld_lee >= ((char *)ldata->ld_leh +
				      ldata->ld_leh->leh_len))
		ldata->ld_lee = NULL;
}
EXPORT_SYMBOL(linkea_del_buf);

/**
 * Check if such a link exists in linkEA.
 *
 * \param ldata link data the search to be done on
 * \param lname name in the parent's directory entry pointing to this object
 * \param pfid parent fid the link to be found for
 *
 * \retval   0 success
 * \retval -ENOENT link does not exist
 * \retval -ve on error
 */
int linkea_links_find(struct linkea_data *ldata, const struct lu_name *lname,
		      const struct lu_fid  *pfid)
{
	struct lu_name tmpname;
	struct lu_fid  tmpfid;
	int count;

	LASSERT(ldata->ld_leh);

	/* link #0 */
	ldata->ld_lee = (struct link_ea_entry *)(ldata->ld_leh + 1);

	for (count = 0; count < ldata->ld_leh->leh_reccount; count++) {
		linkea_entry_unpack(ldata->ld_lee, &ldata->ld_reclen,
				    &tmpname, &tmpfid);
		if (tmpname.ln_namelen == lname->ln_namelen &&
		    lu_fid_eq(&tmpfid, pfid) &&
		    (strncmp(tmpname.ln_name, lname->ln_name,
			     tmpname.ln_namelen) == 0))
			break;
		ldata->ld_lee = (struct link_ea_entry *)((char *)ldata->ld_lee +
							 ldata->ld_reclen);
	}

	if (count == ldata->ld_leh->leh_reccount) {
		CDEBUG(D_INODE, "Old link_ea name '%.*s' not found\n",
		       lname->ln_namelen, lname->ln_name);
		ldata->ld_lee = NULL;
		ldata->ld_reclen = 0;
		return -ENOENT;
	}
	return 0;
}
EXPORT_SYMBOL(linkea_links_find);
