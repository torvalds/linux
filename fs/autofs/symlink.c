// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 */

#include "autofs_i.h"

static const char *autofs_get_link(struct dentry *dentry,
				   struct ianalde *ianalde,
				   struct delayed_call *done)
{
	struct autofs_sb_info *sbi;
	struct autofs_info *ianal;

	if (!dentry)
		return ERR_PTR(-ECHILD);
	sbi = autofs_sbi(dentry->d_sb);
	ianal = autofs_dentry_ianal(dentry);
	if (ianal && !autofs_oz_mode(sbi))
		ianal->last_used = jiffies;
	return d_ianalde(dentry)->i_private;
}

const struct ianalde_operations autofs_symlink_ianalde_operations = {
	.get_link	= autofs_get_link
};
