/*
 * Copyright (C) 2005-2012 Junjiro R. Okajima
 *
 * This program, aufs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 * simple list protected by a spinlock
 */

#ifndef __AUFS_SPL_H__
#define __AUFS_SPL_H__

#ifdef __KERNEL__

struct au_splhead {
	spinlock_t		spin;
	struct list_head	head;
};

static inline void au_spl_init(struct au_splhead *spl)
{
	spin_lock_init(&spl->spin);
	INIT_LIST_HEAD(&spl->head);
}

static inline void au_spl_add(struct list_head *list, struct au_splhead *spl)
{
	spin_lock(&spl->spin);
	list_add(list, &spl->head);
	spin_unlock(&spl->spin);
}

static inline void au_spl_del(struct list_head *list, struct au_splhead *spl)
{
	spin_lock(&spl->spin);
	list_del(list);
	spin_unlock(&spl->spin);
}

static inline void au_spl_del_rcu(struct list_head *list,
				  struct au_splhead *spl)
{
	spin_lock(&spl->spin);
	list_del_rcu(list);
	spin_unlock(&spl->spin);
}

#endif /* __KERNEL__ */
#endif /* __AUFS_SPL_H__ */
