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
#include <linux/spinlock.h>

extern struct fscache_netfs v9fs_cache_netfs;
extern const struct fscache_cookie_def v9fs_cache_session_index_def;
extern const struct fscache_cookie_def v9fs_cache_iyesde_index_def;

extern void v9fs_cache_session_get_cookie(struct v9fs_session_info *v9ses);
extern void v9fs_cache_session_put_cookie(struct v9fs_session_info *v9ses);

extern void v9fs_cache_iyesde_get_cookie(struct iyesde *iyesde);
extern void v9fs_cache_iyesde_put_cookie(struct iyesde *iyesde);
extern void v9fs_cache_iyesde_flush_cookie(struct iyesde *iyesde);
extern void v9fs_cache_iyesde_set_cookie(struct iyesde *iyesde, struct file *filp);
extern void v9fs_cache_iyesde_reset_cookie(struct iyesde *iyesde);

extern int __v9fs_cache_register(void);
extern void __v9fs_cache_unregister(void);

extern int __v9fs_fscache_release_page(struct page *page, gfp_t gfp);
extern void __v9fs_fscache_invalidate_page(struct page *page);
extern int __v9fs_readpage_from_fscache(struct iyesde *iyesde,
					struct page *page);
extern int __v9fs_readpages_from_fscache(struct iyesde *iyesde,
					 struct address_space *mapping,
					 struct list_head *pages,
					 unsigned *nr_pages);
extern void __v9fs_readpage_to_fscache(struct iyesde *iyesde, struct page *page);
extern void __v9fs_fscache_wait_on_page_write(struct iyesde *iyesde,
					      struct page *page);

static inline int v9fs_fscache_release_page(struct page *page,
					    gfp_t gfp)
{
	return __v9fs_fscache_release_page(page, gfp);
}

static inline void v9fs_fscache_invalidate_page(struct page *page)
{
	__v9fs_fscache_invalidate_page(page);
}

static inline int v9fs_readpage_from_fscache(struct iyesde *iyesde,
					     struct page *page)
{
	return __v9fs_readpage_from_fscache(iyesde, page);
}

static inline int v9fs_readpages_from_fscache(struct iyesde *iyesde,
					      struct address_space *mapping,
					      struct list_head *pages,
					      unsigned *nr_pages)
{
	return __v9fs_readpages_from_fscache(iyesde, mapping, pages,
					     nr_pages);
}

static inline void v9fs_readpage_to_fscache(struct iyesde *iyesde,
					    struct page *page)
{
	if (PageFsCache(page))
		__v9fs_readpage_to_fscache(iyesde, page);
}

static inline void v9fs_uncache_page(struct iyesde *iyesde, struct page *page)
{
	struct v9fs_iyesde *v9iyesde = V9FS_I(iyesde);
	fscache_uncache_page(v9iyesde->fscache, page);
	BUG_ON(PageFsCache(page));
}

static inline void v9fs_fscache_wait_on_page_write(struct iyesde *iyesde,
						   struct page *page)
{
	return __v9fs_fscache_wait_on_page_write(iyesde, page);
}

#else /* CONFIG_9P_FSCACHE */

static inline void v9fs_cache_iyesde_get_cookie(struct iyesde *iyesde)
{
}

static inline void v9fs_cache_iyesde_put_cookie(struct iyesde *iyesde)
{
}

static inline void v9fs_cache_iyesde_set_cookie(struct iyesde *iyesde, struct file *file)
{
}

static inline int v9fs_fscache_release_page(struct page *page,
					    gfp_t gfp) {
	return 1;
}

static inline void v9fs_fscache_invalidate_page(struct page *page) {}

static inline int v9fs_readpage_from_fscache(struct iyesde *iyesde,
					     struct page *page)
{
	return -ENOBUFS;
}

static inline int v9fs_readpages_from_fscache(struct iyesde *iyesde,
					      struct address_space *mapping,
					      struct list_head *pages,
					      unsigned *nr_pages)
{
	return -ENOBUFS;
}

static inline void v9fs_readpage_to_fscache(struct iyesde *iyesde,
					    struct page *page)
{}

static inline void v9fs_uncache_page(struct iyesde *iyesde, struct page *page)
{}

static inline void v9fs_fscache_wait_on_page_write(struct iyesde *iyesde,
						   struct page *page)
{
	return;
}

#endif /* CONFIG_9P_FSCACHE */
#endif /* _9P_CACHE_H */
