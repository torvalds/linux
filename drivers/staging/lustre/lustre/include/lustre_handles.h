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

#ifndef __LUSTRE_HANDLES_H_
#define __LUSTRE_HANDLES_H_

/** \defgroup handles handles
 *
 * @{
 */

#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "../../include/linux/libcfs/libcfs.h"


struct portals_handle_ops {
	void (*hop_addref)(void *object);
	void (*hop_free)(void *object, int size);
};

/* These handles are most easily used by having them appear at the very top of
 * whatever object that you want to make handles for.  ie:
 *
 * struct ldlm_lock {
 *	 struct portals_handle handle;
 *	 ...
 * };
 *
 * Now you're able to assign the results of cookie2handle directly to an
 * ldlm_lock.  If it's not at the top, you'll want to use container_of()
 * to compute the start of the structure based on the handle field. */
struct portals_handle {
	struct list_head			h_link;
	__u64				h_cookie;
	struct portals_handle_ops	*h_ops;

	/* newly added fields to handle the RCU issue. -jxiong */
	struct rcu_head			h_rcu;
	spinlock_t			h_lock;
	unsigned int			h_size:31;
	unsigned int			h_in:1;
};
#define RCU2HANDLE(rcu)    container_of(rcu, struct portals_handle, h_rcu)

/* handles.c */

/* Add a handle to the hash table */
void class_handle_hash(struct portals_handle *,
		       struct portals_handle_ops *ops);
void class_handle_unhash(struct portals_handle *);
void class_handle_hash_back(struct portals_handle *);
void *class_handle2object(__u64 cookie);
void class_handle_free_cb(struct rcu_head *rcu);
int class_handle_init(void);
void class_handle_cleanup(void);

/** @} handles */

#endif
