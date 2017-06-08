/*
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
 */
/*
 * This file creates a memory allocation primitive for Lustre, that
 * allows to fallback to vmalloc allocations should regular kernel allocations
 * fail due to size or system memory fragmentation.
 *
 * Author: Oleg Drokin <green@linuxhacker.ru>
 *
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Seagate Technology.
 */
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "../../../include/linux/libcfs/libcfs.h"

void *libcfs_kvzalloc(size_t size, gfp_t flags)
{
	void *ret;

	ret = kzalloc(size, flags | __GFP_NOWARN);
	if (!ret)
		ret = __vmalloc(size, flags | __GFP_ZERO, PAGE_KERNEL);
	return ret;
}
EXPORT_SYMBOL(libcfs_kvzalloc);

void *libcfs_kvzalloc_cpt(struct cfs_cpt_table *cptab, int cpt, size_t size,
			  gfp_t flags)
{
	return kvzalloc_node(size, flags, cfs_cpt_spread_node(cptab, cpt));
}
EXPORT_SYMBOL(libcfs_kvzalloc_cpt);
