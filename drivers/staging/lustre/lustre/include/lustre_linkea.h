// SPDX-License-Identifier: GPL-2.0
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
 * Author: di wang <di.wang@intel.com>
 */

/* There are several reasons to restrict the linkEA size:
 *
 * 1. Under DNE mode, if we do not restrict the linkEA size, and if there
 *    are too many cross-MDTs hard links to the same object, then it will
 *    casue the llog overflow.
 *
 * 2. Some backend has limited size for EA. For example, if without large
 *    EA enabled, the ldiskfs will make all EAs to share one (4K) EA block.
 *
 * 3. Too many entries in linkEA will seriously affect linkEA performance
 *    because we only support to locate linkEA entry consecutively.
 */
#define MAX_LINKEA_SIZE		4096

struct linkea_data {
	/**
	 * Buffer to keep link EA body.
	 */
	struct lu_buf		*ld_buf;
	/**
	 * The matched header, entry and its length in the EA
	 */
	struct link_ea_header	*ld_leh;
	struct link_ea_entry	*ld_lee;
	int			ld_reclen;
};

int linkea_data_new(struct linkea_data *ldata, struct lu_buf *buf);
int linkea_init(struct linkea_data *ldata);
int linkea_init_with_rec(struct linkea_data *ldata);
void linkea_entry_unpack(const struct link_ea_entry *lee, int *reclen,
			 struct lu_name *lname, struct lu_fid *pfid);
int linkea_entry_pack(struct link_ea_entry *lee, const struct lu_name *lname,
		      const struct lu_fid *pfid);
int linkea_add_buf(struct linkea_data *ldata, const struct lu_name *lname,
		   const struct lu_fid *pfid);
void linkea_del_buf(struct linkea_data *ldata, const struct lu_name *lname);
int linkea_links_find(struct linkea_data *ldata, const struct lu_name *lname,
		      const struct lu_fid  *pfid);

static inline void linkea_first_entry(struct linkea_data *ldata)
{
	LASSERT(ldata);
	LASSERT(ldata->ld_leh);

	if (ldata->ld_leh->leh_reccount == 0)
		ldata->ld_lee = NULL;
	else
		ldata->ld_lee = (struct link_ea_entry *)(ldata->ld_leh + 1);
}

static inline void linkea_next_entry(struct linkea_data *ldata)
{
	LASSERT(ldata);
	LASSERT(ldata->ld_leh);

	if (ldata->ld_lee) {
		ldata->ld_lee = (struct link_ea_entry *)((char *)ldata->ld_lee +
							 ldata->ld_reclen);
		if ((char *)ldata->ld_lee >= ((char *)ldata->ld_leh +
					      ldata->ld_leh->leh_len))
			ldata->ld_lee = NULL;
	}
}
