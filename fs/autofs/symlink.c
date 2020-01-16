// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 */

#include "autofs_i.h"

static const char *autofs_get_link(struct dentry *dentry,
				   struct iyesde *iyesde,
				   struct delayed_call *done)
{
	struct autofs_sb_info *sbi;
	struct autofs_info *iyes;

	if (!dentry)
		return ERR_PTR(-ECHILD);
	sbi = autofs_sbi(dentry->d_sb);
	iyes = autofs_dentry_iyes(dentry);
	if (iyes && !autofs_oz_mode(sbi))
		iyes->last_used = jiffies;
	return d_iyesde(dentry)->i_private;
}

const struct iyesde_operations autofs_symlink_iyesde_operations = {
	.get_link	= autofs_get_link
};
