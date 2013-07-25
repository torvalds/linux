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
 * Copyright (c) 2013, Intel Corporation.
 * Use is subject to license terms.
 *
 * Author: di wang <di.wang@intel.com>
 */

struct linkea_data {
	/**
	 * Buffer to keep link EA body.
	 */
	struct lu_buf		*ld_buf;
	/**
	 * The matched header, entry and its lenght in the EA
	 */
	struct link_ea_header	*ld_leh;
	struct link_ea_entry	*ld_lee;
	int			ld_reclen;
};

int linkea_data_new(struct linkea_data *ldata, struct lu_buf *buf);
int linkea_init(struct linkea_data *ldata);
void linkea_entry_unpack(const struct link_ea_entry *lee, int *reclen,
			 struct lu_name *lname, struct lu_fid *pfid);
int linkea_add_buf(struct linkea_data *ldata, const struct lu_name *lname,
		   const struct lu_fid *pfid);
void linkea_del_buf(struct linkea_data *ldata, const struct lu_name *lname);
int linkea_links_find(struct linkea_data *ldata, const struct lu_name *lname,
		      const struct lu_fid  *pfid);

#define LINKEA_NEXT_ENTRY(ldata)	\
	(struct link_ea_entry *)((char *)ldata.ld_lee + ldata.ld_reclen)

#define LINKEA_FIRST_ENTRY(ldata)	\
	(struct link_ea_entry *)(ldata.ld_leh + 1)
