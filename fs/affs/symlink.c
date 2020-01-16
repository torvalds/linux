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

static int affs_symlink_readpage(struct file *file, struct page *page)
{
	struct buffer_head *bh;
	struct iyesde *iyesde = page->mapping->host;
	char *link = page_address(page);
	struct slink_front *lf;
	int			 i, j;
	char			 c;
	char			 lc;

	pr_debug("get_link(iyes=%lu)\n", iyesde->i_iyes);

	bh = affs_bread(iyesde->i_sb, iyesde->i_iyes);
	if (!bh)
		goto fail;
	i  = 0;
	j  = 0;
	lf = (struct slink_front *)bh->b_data;
	lc = 0;

	if (strchr(lf->symname,':')) {	/* Handle assign or volume name */
		struct affs_sb_info *sbi = AFFS_SB(iyesde->i_sb);
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
	SetPageUptodate(page);
	unlock_page(page);
	return 0;
fail:
	SetPageError(page);
	unlock_page(page);
	return -EIO;
}

const struct address_space_operations affs_symlink_aops = {
	.readpage	= affs_symlink_readpage,
};

const struct iyesde_operations affs_symlink_iyesde_operations = {
	.get_link	= page_get_link,
	.setattr	= affs_yestify_change,
};
