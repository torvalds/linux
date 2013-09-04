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
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef LUSTRE_PATCHLESS_COMPAT_H
#define LUSTRE_PATCHLESS_COMPAT_H

#include <linux/fs.h>

#include <linux/list.h>
#include <linux/mm.h>
#include <linux/hash.h>


#define ll_delete_from_page_cache(page) delete_from_page_cache(page)

static inline void
truncate_complete_page(struct address_space *mapping, struct page *page)
{
	if (page->mapping != mapping)
		return;

	if (PagePrivate(page))
		page->mapping->a_ops->invalidatepage(page, 0, PAGE_CACHE_SIZE);

	cancel_dirty_page(page, PAGE_SIZE);
	ClearPageMappedToDisk(page);
	ll_delete_from_page_cache(page);
}

#ifdef ATTR_OPEN
# define ATTR_FROM_OPEN ATTR_OPEN
#else
# ifndef ATTR_FROM_OPEN
#  define ATTR_FROM_OPEN 0
# endif
#endif /* ATTR_OPEN */

#ifndef ATTR_RAW
#define ATTR_RAW 0
#endif

#ifndef ATTR_CTIME_SET
/*
 * set ATTR_CTIME_SET to a high value to avoid any risk of collision with other
 * ATTR_* attributes (see bug 13828)
 */
#define ATTR_CTIME_SET (1 << 28)
#endif

#endif /* LUSTRE_PATCHLESS_COMPAT_H */
