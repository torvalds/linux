/*
 * Copyright (C) 2007, 2008  Internet Systems Consortium, Inc. ("ISC")
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: radix.h,v 1.13 2008/12/01 23:47:45 tbox Exp $ */

/*
 * This source was adapted from MRT's RCS Ids:
 * Id: radix.h,v 1.6 1999/08/03 03:32:53 masaki Exp
 * Id: mrt.h,v 1.57.2.6 1999/12/28 23:41:27 labovit Exp
 * Id: defs.h,v 1.5.2.2 2000/01/15 14:19:16 masaki Exp
 */

#include <isc/magic.h>
#include <isc/types.h>
#include <isc/mutex.h>
#include <isc/net.h>
#include <isc/refcount.h>

#include <string.h>

#ifndef _RADIX_H
#define _RADIX_H

#define NETADDR_TO_PREFIX_T(na,pt,bits) \
	do { \
		memset(&(pt), 0, sizeof(pt)); \
		if((na) != NULL) { \
			(pt).family = (na)->family; \
			(pt).bitlen = (bits); \
			if ((pt).family == AF_INET6) { \
				memcpy(&(pt).add.sin6, &(na)->type.in6, \
				       ((bits)+7)/8); \
			} else \
				memcpy(&(pt).add.sin, &(na)->type.in, \
				       ((bits)+7)/8); \
		} else { \
			(pt).family = AF_UNSPEC; \
			(pt).bitlen = 0; \
		} \
		isc_refcount_init(&(pt).refcount, 0); \
	} while(0)

typedef struct isc_prefix {
    unsigned int family;	/* AF_INET | AF_INET6, or AF_UNSPEC for "any" */
    unsigned int bitlen;	/* 0 for "any" */
    isc_refcount_t refcount;
    union {
		struct in_addr sin;
		struct in6_addr sin6;
    } add;
} isc_prefix_t;

typedef void (*isc_radix_destroyfunc_t)(void *);
typedef void (*isc_radix_processfunc_t)(isc_prefix_t *, void **);

#define isc_prefix_tochar(prefix) ((char *)&(prefix)->add.sin)
#define isc_prefix_touchar(prefix) ((u_char *)&(prefix)->add.sin)

#define BIT_TEST(f, b)  ((f) & (b))

/*
 * We need "first match" when we search the radix tree to preserve
 * compatibility with the existing ACL implementation. Radix trees
 * naturally lend themselves to "best match". In order to get "first match"
 * behavior, we keep track of the order in which entries are added to the
 * tree--and when a search is made, we find all matching entries, and
 * return the one that was added first.
 *
 * An IPv4 prefix and an IPv6 prefix may share a radix tree node if they
 * have the same length and bit pattern (e.g., 127/8 and 7f::/8).  To
 * disambiguate between them, node_num and data are two-element arrays;
 * node_num[0] and data[0] are used for IPv4 addresses, node_num[1]
 * and data[1] for IPv6 addresses.  The only exception is a prefix of
 * 0/0 (aka "any" or "none"), which is always stored as IPv4 but matches
 * IPv6 addresses too.
 */

#define ISC_IS6(family) ((family) == AF_INET6 ? 1 : 0)
typedef struct isc_radix_node {
   isc_uint32_t bit;			/* bit length of the prefix */
   isc_prefix_t *prefix;		/* who we are in radix tree */
   struct isc_radix_node *l, *r;	/* left and right children */
   struct isc_radix_node *parent;	/* may be used */
   void *data[2];			/* pointers to IPv4 and IPV6 data */
   int node_num[2];			/* which node this was in the tree,
					   or -1 for glue nodes */
} isc_radix_node_t;

#define RADIX_TREE_MAGIC         ISC_MAGIC('R','d','x','T');
#define RADIX_TREE_VALID(a)      ISC_MAGIC_VALID(a, RADIX_TREE_MAGIC);

typedef struct isc_radix_tree {
   unsigned int		magic;
   isc_mem_t		*mctx;
   isc_radix_node_t 	*head;
   isc_uint32_t		maxbits;	/* for IP, 32 bit addresses */
   int num_active_node;			/* for debugging purposes */
   int num_added_node;			/* total number of nodes */
} isc_radix_tree_t;

isc_result_t
isc_radix_search(isc_radix_tree_t *radix, isc_radix_node_t **target,
		 isc_prefix_t *prefix);
/*%<
 * Search 'radix' for the best match to 'prefix'.
 * Return the node found in '*target'.
 *
 * Requires:
 * \li	'radix' to be valid.
 * \li	'target' is not NULL and "*target" is NULL.
 * \li	'prefix' to be valid.
 *
 * Returns:
 * \li	ISC_R_NOTFOUND
 * \li	ISC_R_SUCCESS
 */

isc_result_t
isc_radix_insert(isc_radix_tree_t *radix, isc_radix_node_t **target,
		 isc_radix_node_t *source, isc_prefix_t *prefix);
/*%<
 * Insert 'source' or 'prefix' into the radix tree 'radix'.
 * Return the node added in 'target'.
 *
 * Requires:
 * \li	'radix' to be valid.
 * \li	'target' is not NULL and "*target" is NULL.
 * \li	'prefix' to be valid or 'source' to be non NULL and contain
 *	a valid prefix.
 *
 * Returns:
 * \li	ISC_R_NOMEMORY
 * \li	ISC_R_SUCCESS
 */

void
isc_radix_remove(isc_radix_tree_t *radix, isc_radix_node_t *node);
/*%<
 * Remove the node 'node' from the radix tree 'radix'.
 *
 * Requires:
 * \li	'radix' to be valid.
 * \li	'node' to be valid.
 */

isc_result_t
isc_radix_create(isc_mem_t *mctx, isc_radix_tree_t **target, int maxbits);
/*%<
 * Create a radix tree with a maximum depth of 'maxbits';
 *
 * Requires:
 * \li	'mctx' to be valid.
 * \li	'target' to be non NULL and '*target' to be NULL.
 * \li	'maxbits' to be less than or equal to RADIX_MAXBITS.
 *
 * Returns:
 * \li	ISC_R_NOMEMORY
 * \li	ISC_R_SUCCESS
 */

void
isc_radix_destroy(isc_radix_tree_t *radix, isc_radix_destroyfunc_t func);
/*%<
 * Destroy a radix tree optionally calling 'func' to clean up node data.
 *
 * Requires:
 * \li	'radix' to be valid.
 */

void
isc_radix_process(isc_radix_tree_t *radix, isc_radix_processfunc_t func);
/*%<
 * Walk a radix tree calling 'func' to process node data.
 *
 * Requires:
 * \li	'radix' to be valid.
 * \li	'func' to point to a function.
 */

#define RADIX_MAXBITS 128
#define RADIX_NBIT(x)        (0x80 >> ((x) & 0x7f))
#define RADIX_NBYTE(x)       ((x) >> 3)

#define RADIX_DATA_GET(node, type) (type *)((node)->data)
#define RADIX_DATA_SET(node, value) ((node)->data = (void *)(value))

#define RADIX_WALK(Xhead, Xnode) \
    do { \
	isc_radix_node_t *Xstack[RADIX_MAXBITS+1]; \
	isc_radix_node_t **Xsp = Xstack; \
	isc_radix_node_t *Xrn = (Xhead); \
	while ((Xnode = Xrn)) { \
	    if (Xnode->prefix)

#define RADIX_WALK_ALL(Xhead, Xnode) \
do { \
	isc_radix_node_t *Xstack[RADIX_MAXBITS+1]; \
	isc_radix_node_t **Xsp = Xstack; \
	isc_radix_node_t *Xrn = (Xhead); \
	while ((Xnode = Xrn)) { \
	    if (1)

#define RADIX_WALK_BREAK { \
	    if (Xsp != Xstack) { \
		Xrn = *(--Xsp); \
	     } else { \
		Xrn = (radix_node_t *) 0; \
	    } \
	    continue; }

#define RADIX_WALK_END \
	    if (Xrn->l) { \
		if (Xrn->r) { \
		    *Xsp++ = Xrn->r; \
		} \
		Xrn = Xrn->l; \
	    } else if (Xrn->r) { \
		Xrn = Xrn->r; \
	    } else if (Xsp != Xstack) { \
		Xrn = *(--Xsp); \
	    } else { \
		Xrn = (isc_radix_node_t *) 0; \
	    } \
	} \
    } while (0)

#endif /* _RADIX_H */
