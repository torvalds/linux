// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/affs/symlink.c
 *
 *  1995  Hans-Joachim Widmaier - Modified for affs.
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  affs symlink handling code
 */

#include "affs.h"

static int affs_symlink_read_folio(struct file *file, struct folio *folio)
{
	struct buffer_head *bh;
	struct inode *inode = folio->mapping->host;
	char *link = folio_address(folio);
	struct slink_front *lf;
	int			 i, j;
	char			 c;
	char			 lc;

	pr_debug("get_link(ino=%lu)\n", inode->i_ino);

	bh = affs_bread(inode->i_sb, inode->i_ino);
	if (!bh)
		goto fail;
	i  = 0;
	j  = 0;
	lf = (struct slink_front *)bh->b_data;
	lc = 0;

	if (strchr(lf->symname,':')) {	/* Handle assign or volume name */
		struct affs_sb_info *sbi = AFFS_SB(inode->i_sb);
		char *pf;
		spin_lock(&sbi->symlink_lock);
		pf = sbi->s_prefix ? sbi->s_prefix : "/";
		while (i < 1023 && (c = pf[i]))
			link[i++] = c;
		spin_unlock(&sbi->symlink_lock);
		while (i < 1023 && lf->symname[j] != ':')
			link[i++] = lf->symname[j++];
		if (i < 1023)
			link[i++] = '/';
		j++;
		lc = '/';
	}
	while (i < 1023 && (c = lf->symname[j])) {
		if (c == '/' && lc == '/' && i < 1020) {	/* parent dir */
			link[i++] = '.';
			link[i++] = '.';
		}
		link[i++] = c;
		lc = c;
		j++;
	}
	link[i] = '\0';
	affs_brelse(bh);
	folio_mark_uptodate(folio);
	folio_unlock(folio);
	return 0;
fail:
	folio_unlock(folio);
	return -EIO;
}

const struct address_space_operations affs_symlink_aops = {
	.read_folio	= affs_symlink_read_folio,
};

const struct inode_operations affs_symlink_inode_operations = {
	.get_link	= page_get_link,
	.setattr	= affs_notify_change,
};
