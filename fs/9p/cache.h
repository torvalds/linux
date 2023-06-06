/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * V9FS cache definitions.
 *
 *  Copyright (C) 2009 by Abhishek Kulkarni <adkulkar@umail.iu.edu>
 */

#ifndef _9P_CACHE_H
#define _9P_CACHE_H

#ifdef CONFIG_9P_FSCACHE
#include <linux/fscache.h>

extern int v9fs_cache_session_get_cookie(struct v9fs_session_info *v9ses,
					  const char *dev_name);

extern void v9fs_cache_inode_get_cookie(struct inode *inode);

#else /* CONFIG_9P_FSCACHE */

static inline void v9fs_cache_inode_get_cookie(struct inode *inode)
{
}

#endif /* CONFIG_9P_FSCACHE */
#endif /* _9P_CACHE_H */
