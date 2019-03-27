/*
 * radix.h -- generic radix tree
 *
 * Copyright (c) 2012, NLnet Labs. All rights reserved.
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
 * Radix tree. Implementation taken from NSD 4, adjusted for use in ldns.
 *
 */

#ifndef LDNS_RADIX_H_
#define	LDNS_RADIX_H_

#include <ldns/error.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t radix_strlen_t;
typedef struct ldns_radix_array_t ldns_radix_array_t;
typedef struct ldns_radix_node_t ldns_radix_node_t;
typedef struct ldns_radix_t ldns_radix_t;

/** Radix node select edge array */
struct ldns_radix_array_t {
	/** Additional string after the selection byte for this edge. */
	uint8_t* str;
	/** Length of additional string for this edge. */
	radix_strlen_t len;
	/** Node that deals with byte+str. */
	ldns_radix_node_t* edge;
};

/** A node in a radix tree */
struct ldns_radix_node_t {
	/** Key corresponding to this node. */
	uint8_t* key;
	/** Key length corresponding to this node. */
	radix_strlen_t klen;
	/** Data corresponding to this node. */
	void* data;
	/** Parent node. */
	ldns_radix_node_t* parent;
	/** Index in the the parent node select edge array. */
	uint8_t parent_index;
	/** Length of the array. */
	uint16_t len;
	/** Offset of the array. */
	uint16_t offset;
	/** Capacity of the array. */
	uint16_t capacity;
	/** Select edge array. */
	ldns_radix_array_t* array;
};

/** An entire radix tree */
struct ldns_radix_t {
	/** Root. */
	ldns_radix_node_t* root;
	/** Number of nodes in tree. */
	size_t count;
};

/**
 * Create a new radix tree.
 * @return: new radix tree.
 *
 */
ldns_radix_t* ldns_radix_create(void);

/**
 * Initialize radix tree.
 * @param tree: uninitialized radix tree.
 *
 */
void ldns_radix_init(ldns_radix_t* tree);

/**
 * Free the radix tree.
 * @param tree: radix tree.
 *
 */
void ldns_radix_free(ldns_radix_t* tree);

/**
 * Insert data into the tree.
 * @param tree: tree to insert to.
 * @param key:  key.
 * @param len:  length of key.
 * @param data: data.
 * @return: status.
 *
 */
ldns_status ldns_radix_insert(ldns_radix_t* tree, uint8_t* key,
	radix_strlen_t len, void* data);

/**
 * Delete data from the tree.
 * @param tree: tree to insert to.
 * @param key:  key.
 * @param len:  length of key.
 * @return: unlinked data or NULL if not present.
 *
 */
void* ldns_radix_delete(ldns_radix_t* tree, const uint8_t* key, radix_strlen_t len);

/**
 * Search data in the tree.
 * @param tree: tree to insert to.
 * @param key:  key.
 * @param len:  length of key.
 * @return: the radix node or NULL if not found.
 *
 */
ldns_radix_node_t* ldns_radix_search(ldns_radix_t* tree, const uint8_t* key,
	radix_strlen_t len);

/**
 * Search data in the tree, and if not found, find the closest smaller
 * element in the tree.
 * @param tree: tree to insert to.
 * @param key:  key.
 * @param len:  length of key.
 * @param result: the radix node with the exact or closest match. NULL if
 *                the key is smaller than the smallest key in the tree.
 * @return 1 if exact match, 0 otherwise.
 *
 */
int ldns_radix_find_less_equal(ldns_radix_t* tree, const uint8_t* key,
	radix_strlen_t len, ldns_radix_node_t** result);

/**
 * Get the first element in the tree.
 * @param tree: tree.
 * @return: the radix node with the first element.
 *
 */
ldns_radix_node_t* ldns_radix_first(const ldns_radix_t* tree);

/**
 * Get the last element in the tree.
 * @param tree: tree.
 * @return: the radix node with the last element.
 *
 */
ldns_radix_node_t* ldns_radix_last(const ldns_radix_t* tree);

/**
 * Next element.
 * @param node: node.
 * @return: node with next element.
 *
 */
ldns_radix_node_t* ldns_radix_next(ldns_radix_node_t* node);

/**
 * Previous element.
 * @param node: node.
 * @return: node with previous element.
 *
 */
ldns_radix_node_t* ldns_radix_prev(ldns_radix_node_t* node);

/**
 * Split radix tree intwo.
 * @param tree1: one tree.
 * @param num: number of elements to split off.
 * @param tree2: another tree.
 * @return: status.
 *
 */
ldns_status ldns_radix_split(ldns_radix_t* tree1, size_t num,
	ldns_radix_t** tree2);

/**
 * Join two radix trees.
 * @param tree1: one tree.
 * @param tree2: another tree.
 * @return: status.
 *
 */
ldns_status ldns_radix_join(ldns_radix_t* tree1, ldns_radix_t* tree2);

/**
 * Call function for all nodes in the tree, such that leaf nodes are
 * called before parent nodes.
 * @param node: start node.
 * @param func: function.
 * @param arg: user argument.
 *
 */
void ldns_radix_traverse_postorder(ldns_radix_node_t* node,
        void (*func)(ldns_radix_node_t*, void*), void* arg);

/**
 * Print radix tree (for debugging purposes).
 * @param fd: file descriptor.
 * @param tree: tree.
 *
 */
void ldns_radix_printf(FILE* fd, const ldns_radix_t* tree);

#ifdef __cplusplus
}
#endif

#endif /* LDNS_RADIX_H_ */
