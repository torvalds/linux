/*
 * rbtree.h -- generic red-black tree
 *
 * Copyright (c) 2001-2007, NLnet Labs. All rights reserved.
 * 
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/**
 * \file
 * Red black tree. Implementation taken from NSD 3.0.5, adjusted for use
 * in unbound (memory allocation, logging and so on).
 */

#ifndef UTIL_RBTREE_H_
#define	UTIL_RBTREE_H_

/**
 * This structure must be the first member of the data structure in
 * the rbtree.  This allows easy casting between an rbnode_type and the
 * user data (poor man's inheritance).
 */
typedef struct rbnode_type rbnode_type;
/**
 * The rbnode_type struct definition.
 */
struct rbnode_type {
	/** parent in rbtree, RBTREE_NULL for root */
	rbnode_type   *parent;
	/** left node (smaller items) */
	rbnode_type   *left;
	/** right node (larger items) */
	rbnode_type   *right;
	/** pointer to sorting key */
	const void    *key;
	/** colour of this node */
	uint8_t	       color;
};

/** The nullpointer, points to empty node */
#define	RBTREE_NULL &rbtree_null_node
/** the global empty node */
extern	rbnode_type	rbtree_null_node;

/** An entire red black tree */
typedef struct rbtree_type rbtree_type;
/** definition for tree struct */
struct rbtree_type {
	/** The root of the red-black tree */
	rbnode_type    *root;

	/** The number of the nodes in the tree */
	size_t          count;

	/** 
	 * Key compare function. <0,0,>0 like strcmp. 
	 * Return 0 on two NULL ptrs. 
	 */
	int (*cmp) (const void *, const void *);
};

/** 
 * Create new tree (malloced) with given key compare function. 
 * @param cmpf: compare function (like strcmp) takes pointers to two keys.
 * @return: new tree, empty.
 */
rbtree_type *rbtree_create(int (*cmpf)(const void *, const void *));

/** 
 * Init a new tree (malloced by caller) with given key compare function. 
 * @param rbtree: uninitialised memory for new tree, returned empty.
 * @param cmpf: compare function (like strcmp) takes pointers to two keys.
 */
void rbtree_init(rbtree_type *rbtree, int (*cmpf)(const void *, const void *));

/** 
 * Insert data into the tree. 
 * @param rbtree: tree to insert to.
 * @param data: element to insert. 
 * @return: data ptr or NULL if key already present. 
 */
rbnode_type *rbtree_insert(rbtree_type *rbtree, rbnode_type *data);

/**
 * Delete element from tree.
 * @param rbtree: tree to delete from.
 * @param key: key of item to delete.
 * @return: node that is now unlinked from the tree. User to delete it. 
 * returns 0 if node not present 
 */
rbnode_type *rbtree_delete(rbtree_type *rbtree, const void *key);

/**
 * Find key in tree. Returns NULL if not found.
 * @param rbtree: tree to find in.
 * @param key: key that must match.
 * @return: node that fits or NULL.
 */
rbnode_type *rbtree_search(rbtree_type *rbtree, const void *key);

/**
 * Find, but match does not have to be exact.
 * @param rbtree: tree to find in.
 * @param key: key to find position of.
 * @param result: set to the exact node if present, otherwise to element that
 *   precedes the position of key in the tree. NULL if no smaller element.
 * @return: true if exact match in result. Else result points to <= element,
 * or NULL if key is smaller than the smallest key. 
 */
int rbtree_find_less_equal(rbtree_type *rbtree, const void *key, 
	rbnode_type **result);

/**
 * Returns first (smallest) node in the tree
 * @param rbtree: tree
 * @return: smallest element or NULL if tree empty.
 */
rbnode_type *rbtree_first(rbtree_type *rbtree);

/**
 * Returns last (largest) node in the tree
 * @param rbtree: tree
 * @return: largest element or NULL if tree empty.
 */
rbnode_type *rbtree_last(rbtree_type *rbtree);

/**
 * Returns next larger node in the tree
 * @param rbtree: tree
 * @return: next larger element or NULL if no larger in tree.
 */
rbnode_type *rbtree_next(rbnode_type *rbtree);

/**
 * Returns previous smaller node in the tree
 * @param rbtree: tree
 * @return: previous smaller element or NULL if no previous in tree.
 */
rbnode_type *rbtree_previous(rbnode_type *rbtree);

/**
 * Call with node=variable of struct* with rbnode_type as first element.
 * with type is the type of a pointer to that struct. 
 */
#define RBTREE_FOR(node, type, rbtree) \
	for(node=(type)rbtree_first(rbtree); \
		(rbnode_type*)node != RBTREE_NULL; \
		node = (type)rbtree_next((rbnode_type*)node))

/**
 * Call function for all elements in the redblack tree, such that
 * leaf elements are called before parent elements. So that all
 * elements can be safely free()d.
 * Note that your function must not remove the nodes from the tree.
 * Since that may trigger rebalances of the rbtree.
 * @param tree: the tree
 * @param func: function called with element and user arg.
 * 	The function must not alter the rbtree.
 * @param arg: user argument.
 */
void traverse_postorder(rbtree_type* tree, void (*func)(rbnode_type*, void*),
	void* arg);

#endif /* UTIL_RBTREE_H_ */
