/*
 * Copyright (C) 2010-2012 Junjiro R. Okajima
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
 * dynamically customizable operations (for regular files only)
 */

#ifndef __AUFS_DYNOP_H__
#define __AUFS_DYNOP_H__

#ifdef __KERNEL__

#include "inode.h"

enum {AuDy_AOP, AuDyLast};

struct au_dynop {
	int						dy_type;
	union {
		const void				*dy_hop;
		const struct address_space_operations	*dy_haop;
	};
};

struct au_dykey {
	union {
		struct list_head	dk_list;
		struct rcu_head		dk_rcu;
	};
	struct au_dynop		dk_op;

	/*
	 * during I am in the branch local array, kref is gotten. when the
	 * branch is removed, kref is put.
	 */
	struct kref		dk_kref;
};

/* stop unioning since their sizes are very different from each other */
struct au_dyaop {
	struct au_dykey			da_key;
	struct address_space_operations	da_op; /* not const */
	int (*da_get_xip_mem)(struct address_space *, pgoff_t, int,
			      void **, unsigned long *);
};

/* ---------------------------------------------------------------------- */

/* dynop.c */
struct au_branch;
void au_dy_put(struct au_dykey *key);
int au_dy_iaop(struct inode *inode, aufs_bindex_t bindex,
		struct inode *h_inode);
int au_dy_irefresh(struct inode *inode);
void au_dy_arefresh(int do_dio);

void __init au_dy_init(void);
void au_dy_fin(void);

#endif /* __KERNEL__ */
#endif /* __AUFS_DYNOP_H__ */
