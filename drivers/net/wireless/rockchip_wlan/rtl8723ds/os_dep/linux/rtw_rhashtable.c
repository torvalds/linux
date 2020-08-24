/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/

#include <drv_types.h>

#if defined(CONFIG_RTW_WDS) || defined(CONFIG_RTW_MESH) /* for now, only promised for kernel versions we support mesh */

int rtw_rhashtable_walk_enter(rtw_rhashtable *ht, rtw_rhashtable_iter *iter)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0))
	rhashtable_walk_enter((ht), (iter));
	return 0;
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0))
	return rhashtable_walk_init((ht), (iter), GFP_ATOMIC);
#else
	/* kernel >= 4.4.0 rhashtable_walk_init use GFP_KERNEL to alloc, spin_lock for assignment */
	iter->ht = ht;
	iter->p = NULL;
	iter->slot = 0;
	iter->skip = 0;

	iter->walker = kmalloc(sizeof(*iter->walker), GFP_ATOMIC);
	if (!iter->walker)
		return -ENOMEM;

	spin_lock(&ht->lock);
	iter->walker->tbl =
		rcu_dereference_protected(ht->tbl, lockdep_is_held(&ht->lock));
	list_add(&iter->walker->list, &iter->walker->tbl->walkers);
	spin_unlock(&ht->lock);

	return 0;
#endif
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0))
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 25))
static inline int is_vmalloc_addr(const void *x)
{
#ifdef CONFIG_MMU
	unsigned long addr = (unsigned long)x;

	return addr >= VMALLOC_START && addr < VMALLOC_END;
#else
	return 0;
#endif
}
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 25)) */

void kvfree(const void *addr)
{
	if (is_vmalloc_addr(addr))
		vfree(addr);
	else
		kfree(addr);
}
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0)) */

#include "rhashtable.c"

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)) */

#endif /* defined(CONFIG_RTW_WDS) || defined(CONFIG_RTW_MESH) */

