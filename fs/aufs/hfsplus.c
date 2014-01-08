/*
 * Copyright (C) 2010-2014 Junjiro R. Okajima
 */

/*
 * special support for filesystems which aqucires an inode mutex
 * at final closing a file, eg, hfsplus.
 *
 * This trick is very simple and stupid, just to open the file before really
 * neceeary open to tell hfsplus that this is not the final closing.
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
	AuDebugOn(!h_dentry->d_inode);

	h_file = NULL;
	if (au_test_hfsplus(h_dentry->d_sb)
	    && S_ISREG(h_dentry->d_inode->i_mode))
		h_file = au_h_open(dentry, bindex,
				   O_RDONLY | O_NOATIME | O_LARGEFILE,
				   /*file*/NULL, force_wr);
	return h_file;
}

void au_h_open_post(struct dentry *dentry, aufs_bindex_t bindex,
		    struct file *h_file)
{
	if (h_file) {
		fput(h_file);
		au_sbr_put(dentry->d_sb, bindex);
	}
}
