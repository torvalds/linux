// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010-2018 Junjiro R. Okajima
 *
 * This program, aufs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * special support for filesystems which acquires an inode mutex
 * at final closing a file, eg, hfsplus.
 *
 * This trick is very simple and stupid, just to open the file before really
 * necessary open to tell hfsplus that this is not the final closing.
 * The caller should call au_h_open_pre() after acquiring the inode mutex,
 * and au_h_open_post() after releasing it.
 */

#include "aufs.h"

struct file *au_h_open_pre(struct dentry *dentry, aufs_bindex_t bindex,
			   int force_wr)
{
	struct file *h_file;
	struct dentry *h_dentry;

	h_dentry = au_h_dptr(dentry, bindex);
	AuDebugOn(!h_dentry);
	AuDebugOn(d_is_negative(h_dentry));

	h_file = NULL;
	if (au_test_hfsplus(h_dentry->d_sb)
	    && d_is_reg(h_dentry))
		h_file = au_h_open(dentry, bindex,
				   O_RDONLY | O_NOATIME | O_LARGEFILE,
				   /*file*/NULL, force_wr);
	return h_file;
}

void au_h_open_post(struct dentry *dentry, aufs_bindex_t bindex,
		    struct file *h_file)
{
	struct au_branch *br;

	if (h_file) {
		fput(h_file);
		br = au_sbr(dentry->d_sb, bindex);
		au_lcnt_dec(&br->br_nfiles);
	}
}
