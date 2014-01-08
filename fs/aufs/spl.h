/*
 * Copyright (C) 2005-2014 Junjiro R. Okajima
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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

/* ---------------------------------------------------------------------- */

struct au_sphlhead {
	spinlock_t		spin;
	struct hlist_head	head;
};

static inline void au_sphl_init(struct au_sphlhead *sphl)
{
	spin_lock_init(&sphl->spin);
	INIT_HLIST_HEAD(&sphl->head);
}

static inline void au_sphl_add(struct hlist_node *hlist,
			       struct au_sphlhead *sphl)
{
	spin_lock(&sphl->spin);
	hlist_add_head(hlist, &sphl->head);
	spin_unlock(&sphl->spin);
}

static inline void au_sphl_del(struct hlist_node *hlist,
			       struct au_sphlhead *sphl)
{
	spin_lock(&sphl->spin);
	hlist_del(hlist);
	spin_unlock(&sphl->spin);
}

static inline void au_sphl_del_rcu(struct hlist_node *hlist,
				   struct au_sphlhead *sphl)
{
	spin_lock(&sphl->spin);
	hlist_del_rcu(hlist);
	spin_unlock(&sphl->spin);
}

static inline unsigned long au_sphl_count(struct au_sphlhead *sphl)
{
	unsigned long cnt;
	struct hlist_node *pos;

	cnt = 0;
	spin_lock(&sphl->spin);
	hlist_for_each(pos, &sphl->head)
		cnt++;
	spin_unlock(&sphl->spin);
	return cnt;
}

#endif /* __KERNEL__ */
#endif /* __AUFS_SPL_H__ */
