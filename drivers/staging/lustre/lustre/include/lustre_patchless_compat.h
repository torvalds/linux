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
 * http://www.gnu.org/licenses/gpl-2.0.html
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
		page->mapping->a_ops->invalidatepage(page, 0, PAGE_SIZE);

	cancel_dirty_page(page);
	ClearPageMappedToDisk(page);
	ll_delete_from_page_cache(page);
}

#ifndef ATTR_CTIME_SET
/*
 * set ATTR_CTIME_SET to a high value to avoid any risk of collision with other
 * ATTR_* attributes (see bug 13828)
 */
#define ATTR_CTIME_SET (1 << 28)
#endif

#endif /* LUSTRE_PATCHLESS_COMPAT_H */
