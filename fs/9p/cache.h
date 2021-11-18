/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * V9FS cache definitions.
 *
 *  Copyright (C) 2009 by Abhishek Kulkarni <adkulkar@umail.iu.edu>
 */

#ifndef _9P_CACHE_H
#define _9P_CACHE_H
#define FSCACHE_USE_NEW_IO_API
#include <linux/fscache.h>

#ifdef CONFIG_9P_FSCACHE

extern struct fscache_netfs v9fs_cache_netfs;
extern const struct fscache_cookie_def v9fs_cache_session_index_def;
extern const struct fscache_cookie_def v9fs_cache_inode_index_def;

extern void v9fs_cache_session_get_cookie(struct v9fs_session_info *v9ses);
extern void v9fs_cache_session_put_cookie(struct v9fs_session_info *v9ses);

extern void v9fs_cache_inode_get_cookie(struct inode *inode);
extern void v9fs_cache_inode_put_cookie(struct inode *inode);
extern void v9fs_cache_inode_flush_cookie(struct inode *inode);
extern void v9fs_cache_inode_set_cookie(struct inode *inode, struct file *filp);
extern void v9fs_cache_inode_reset_cookie(struct inode *inode);

extern int __v9fs_cache_register(void);
extern void __v9fs_cache_unregister(void);

#else /* CONFIG_9P_FSCACHE */

static inline void v9fs_cache_inode_get_cookie(struct inode *inode)
{
}

static inline void v9fs_cache_inode_put_cookie(struct inode *inode)
{
}

static inline void v9fs_cache_inode_set_cookie(struct inode *inode, struct file *file)
{
}

#endif /* CONFIG_9P_FSCACHE */
#endif /* _9P_CACHE_H */
