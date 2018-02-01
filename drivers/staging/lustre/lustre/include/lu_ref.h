// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 *
 *   This file is part of Lustre, http://www.lustre.org.
 *
 *   Lustre is free software; you can redistribute it and/or
 *   modify it under the terms of version 2 of the GNU General Public
 *   License as published by the Free Software Foundation.
 *
 *   Lustre is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 */

#ifndef __LUSTRE_LU_REF_H
#define __LUSTRE_LU_REF_H

#include <linux/list.h>

/** \defgroup lu_ref lu_ref
 *
 * An interface to track references between objects. Mostly for debugging.
 *
 * Suppose there is a reference counted data-structure struct foo. To track
 * who acquired references to instance of struct foo, add lu_ref field to it:
 *
 * \code
 *	 struct foo {
 *		 atomic_t      foo_refcount;
 *		 struct lu_ref foo_reference;
 *		 ...
 *	 };
 * \endcode
 *
 * foo::foo_reference has to be initialized by calling
 * lu_ref_init(). Typically there will be functions or macros to increment and
 * decrement foo::foo_refcount, let's say they are foo_get(struct foo *foo)
 * and foo_put(struct foo *foo), respectively.
 *
 * Whenever foo_get() is called to acquire a reference on a foo, lu_ref_add()
 * has to be called to insert into foo::foo_reference a record, describing
 * acquired reference. Dually, lu_ref_del() removes matching record. Typical
 * usages are:
 *
 * \code
 *	struct bar *bar;
 *
 *	// bar owns a reference to foo.
 *	bar->bar_foo = foo_get(foo);
 *	lu_ref_add(&foo->foo_reference, "bar", bar);
 *
 *	...
 *
 *	// reference from bar to foo is released.
 *	lu_ref_del(&foo->foo_reference, "bar", bar);
 *	foo_put(bar->bar_foo);
 *
 *
 *	// current thread acquired a temporary reference to foo.
 *	foo_get(foo);
 *	lu_ref_add(&foo->reference, __func__, current);
 *
 *	...
 *
 *	// temporary reference is released.
 *	lu_ref_del(&foo->reference, __func__, current);
 *	foo_put(foo);
 * \endcode
 *
 * \e Et \e cetera. Often it makes sense to include lu_ref_add() and
 * lu_ref_del() calls into foo_get() and foo_put(). When an instance of struct
 * foo is destroyed, lu_ref_fini() has to be called that checks that no
 * pending references remain. lu_ref_print() can be used to dump a list of
 * pending references, while hunting down a leak.
 *
 * For objects to which a large number of references can be acquired,
 * lu_ref_del() can become cpu consuming, as it has to scan the list of
 * references. To work around this, remember result of lu_ref_add() (usually
 * in the same place where pointer to struct foo is stored), and use
 * lu_ref_del_at():
 *
 * \code
 *	// There is a large number of bar's for a single foo.
 *	bar->bar_foo     = foo_get(foo);
 *	bar->bar_foo_ref = lu_ref_add(&foo->foo_reference, "bar", bar);
 *
 *	...
 *
 *	// reference from bar to foo is released.
 *	lu_ref_del_at(&foo->foo_reference, bar->bar_foo_ref, "bar", bar);
 *	foo_put(bar->bar_foo);
 * \endcode
 *
 * lu_ref interface degrades gracefully in case of memory shortages.
 *
 * @{
 */

/*
 * dummy data structures/functions to pass compile for now.
 * We need to reimplement them with kref.
 */
struct lu_ref {};
struct lu_ref_link {};

static inline void lu_ref_init(struct lu_ref *ref)
{
}

static inline void lu_ref_fini(struct lu_ref *ref)
{
}

static inline struct lu_ref_link *lu_ref_add(struct lu_ref *ref,
					     const char *scope,
					     const void *source)
{
	return NULL;
}

static inline struct lu_ref_link *lu_ref_add_atomic(struct lu_ref *ref,
						    const char *scope,
						    const void *source)
{
	return NULL;
}

static inline void lu_ref_add_at(struct lu_ref *ref,
				 struct lu_ref_link *link,
				 const char *scope,
				 const void *source)
{
}

static inline void lu_ref_del(struct lu_ref *ref, const char *scope,
			      const void *source)
{
}

static inline void lu_ref_set_at(struct lu_ref *ref, struct lu_ref_link *link,
				 const char *scope, const void *source0,
				 const void *source1)
{
}

static inline void lu_ref_del_at(struct lu_ref *ref, struct lu_ref_link *link,
				 const char *scope, const void *source)
{
}

static inline int lu_ref_global_init(void)
{
	return 0;
}

static inline void lu_ref_global_fini(void)
{
}

static inline void lu_ref_print(const struct lu_ref *ref)
{
}

static inline void lu_ref_print_all(void)
{
}

/** @} lu */

#endif /* __LUSTRE_LU_REF_H */
