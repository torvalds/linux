// SPDX-License-Identifier: GPL-2.0
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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/include/libcfs/libcfs_private.h
 *
 * Various defines for libcfs.
 *
 */

#ifndef __LIBCFS_PRIVATE_H__
#define __LIBCFS_PRIVATE_H__

#ifndef DEBUG_SUBSYSTEM
# define DEBUG_SUBSYSTEM S_UNDEFINED
#endif

#define LASSERTF(cond, fmt, ...)					\
do {									\
	if (unlikely(!(cond))) {					\
		LIBCFS_DEBUG_MSG_DATA_DECL(__msg_data, D_EMERG, NULL);	\
		libcfs_debug_msg(&__msg_data,				\
				 "ASSERTION( %s ) failed: " fmt, #cond,	\
				 ## __VA_ARGS__);			\
		lbug_with_loc(&__msg_data);				\
	}								\
} while (0)

#define LASSERT(cond) LASSERTF(cond, "\n")

#ifdef CONFIG_LUSTRE_DEBUG_EXPENSIVE_CHECK
/**
 * This is for more expensive checks that one doesn't want to be enabled all
 * the time. LINVRNT() has to be explicitly enabled by
 * CONFIG_LUSTRE_DEBUG_EXPENSIVE_CHECK option.
 */
# define LINVRNT(exp) LASSERT(exp)
#else
# define LINVRNT(exp) ((void)sizeof !!(exp))
#endif

void __noreturn lbug_with_loc(struct libcfs_debug_msg_data *msg);

#define LBUG()							  \
do {								    \
	LIBCFS_DEBUG_MSG_DATA_DECL(msgdata, D_EMERG, NULL);	     \
	lbug_with_loc(&msgdata);					\
} while (0)

/*
 * Use #define rather than inline, as lnet_cpt_table() might
 * not be defined yet
 */
#define kmalloc_cpt(size, flags, cpt) \
	kmalloc_node(size, flags,  cfs_cpt_spread_node(lnet_cpt_table(), cpt))

#define kzalloc_cpt(size, flags, cpt) \
	kmalloc_node(size, flags | __GFP_ZERO,				\
		     cfs_cpt_spread_node(lnet_cpt_table(), cpt))

#define kvmalloc_cpt(size, flags, cpt) \
	kvmalloc_node(size, flags,					\
		      cfs_cpt_spread_node(lnet_cpt_table(), cpt))

#define kvzalloc_cpt(size, flags, cpt) \
	kvmalloc_node(size, flags | __GFP_ZERO,				\
		      cfs_cpt_spread_node(lnet_cpt_table(), cpt))

/******************************************************************************/

void libcfs_debug_dumplog(void);
int libcfs_debug_init(unsigned long bufsize);
int libcfs_debug_cleanup(void);
int libcfs_debug_clear_buffer(void);
int libcfs_debug_mark_buffer(const char *text);

/*
 * allocate a variable array, returned value is an array of pointers.
 * Caller can specify length of array by count.
 */
void *cfs_array_alloc(int count, unsigned int size);
void  cfs_array_free(void *vars);

#define LASSERT_ATOMIC_ENABLED	  (1)

#if LASSERT_ATOMIC_ENABLED

/** assert value of @a is equal to @v */
#define LASSERT_ATOMIC_EQ(a, v)			\
	LASSERTF(atomic_read(a) == v, "value: %d\n", atomic_read((a)))

/** assert value of @a is unequal to @v */
#define LASSERT_ATOMIC_NE(a, v)		\
	LASSERTF(atomic_read(a) != v, "value: %d\n", atomic_read((a)))

/** assert value of @a is little than @v */
#define LASSERT_ATOMIC_LT(a, v)		\
	LASSERTF(atomic_read(a) < v, "value: %d\n", atomic_read((a)))

/** assert value of @a is little/equal to @v */
#define LASSERT_ATOMIC_LE(a, v)		\
	LASSERTF(atomic_read(a) <= v, "value: %d\n", atomic_read((a)))

/** assert value of @a is great than @v */
#define LASSERT_ATOMIC_GT(a, v)		\
	LASSERTF(atomic_read(a) > v, "value: %d\n", atomic_read((a)))

/** assert value of @a is great/equal to @v */
#define LASSERT_ATOMIC_GE(a, v)		\
	LASSERTF(atomic_read(a) >= v, "value: %d\n", atomic_read((a)))

/** assert value of @a is great than @v1 and little than @v2 */
#define LASSERT_ATOMIC_GT_LT(a, v1, v2)			 \
do {							    \
	int __v = atomic_read(a);			   \
	LASSERTF(__v > v1 && __v < v2, "value: %d\n", __v);     \
} while (0)

/** assert value of @a is great than @v1 and little/equal to @v2 */
#define LASSERT_ATOMIC_GT_LE(a, v1, v2)			 \
do {							    \
	int __v = atomic_read(a);			   \
	LASSERTF(__v > v1 && __v <= v2, "value: %d\n", __v);    \
} while (0)

/** assert value of @a is great/equal to @v1 and little than @v2 */
#define LASSERT_ATOMIC_GE_LT(a, v1, v2)			 \
do {							    \
	int __v = atomic_read(a);			   \
	LASSERTF(__v >= v1 && __v < v2, "value: %d\n", __v);    \
} while (0)

/** assert value of @a is great/equal to @v1 and little/equal to @v2 */
#define LASSERT_ATOMIC_GE_LE(a, v1, v2)			 \
do {							    \
	int __v = atomic_read(a);			   \
	LASSERTF(__v >= v1 && __v <= v2, "value: %d\n", __v);   \
} while (0)

#else /* !LASSERT_ATOMIC_ENABLED */

#define LASSERT_ATOMIC_EQ(a, v)		 do {} while (0)
#define LASSERT_ATOMIC_NE(a, v)		 do {} while (0)
#define LASSERT_ATOMIC_LT(a, v)		 do {} while (0)
#define LASSERT_ATOMIC_LE(a, v)		 do {} while (0)
#define LASSERT_ATOMIC_GT(a, v)		 do {} while (0)
#define LASSERT_ATOMIC_GE(a, v)		 do {} while (0)
#define LASSERT_ATOMIC_GT_LT(a, v1, v2)	 do {} while (0)
#define LASSERT_ATOMIC_GT_LE(a, v1, v2)	 do {} while (0)
#define LASSERT_ATOMIC_GE_LT(a, v1, v2)	 do {} while (0)
#define LASSERT_ATOMIC_GE_LE(a, v1, v2)	 do {} while (0)

#endif /* LASSERT_ATOMIC_ENABLED */

#define LASSERT_ATOMIC_ZERO(a)		  LASSERT_ATOMIC_EQ(a, 0)
#define LASSERT_ATOMIC_POS(a)		   LASSERT_ATOMIC_GT(a, 0)

/* implication */
#define ergo(a, b) (!(a) || (b))
/* logical equivalence */
#define equi(a, b) (!!(a) == !!(b))

#ifndef HAVE_CFS_SIZE_ROUND
static inline size_t cfs_size_round(int val)
{
	return round_up(val, 8);
}

#define HAVE_CFS_SIZE_ROUND
#endif

#endif
