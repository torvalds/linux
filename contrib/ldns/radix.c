/*
 * radix.c -- generic radix tree
 *
 * Taken from NSD4, modified for ldns
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
 * Implementation of a radix tree.
 */

#include <ldns/config.h>
#include <ldns/radix.h>
#include <ldns/util.h>
#include <stdlib.h>

/** Helper functions */
static ldns_radix_node_t* ldns_radix_new_node(void* data, uint8_t* key,
	radix_strlen_t len);
static int ldns_radix_find_prefix(ldns_radix_t* tree, uint8_t* key,
	radix_strlen_t len, ldns_radix_node_t** result, radix_strlen_t* pos);
static int ldns_radix_array_space(ldns_radix_node_t* node, uint8_t byte);
static int ldns_radix_array_grow(ldns_radix_node_t* node, unsigned need);
static int ldns_radix_str_create(ldns_radix_array_t* array, uint8_t* key,
	radix_strlen_t pos, radix_strlen_t len);
static int ldns_radix_prefix_remainder(radix_strlen_t prefix_len,
	uint8_t* longer_str, radix_strlen_t longer_len, uint8_t** split_str,
	radix_strlen_t* split_len);
static int ldns_radix_array_split(ldns_radix_array_t* array, uint8_t* key,
	radix_strlen_t pos, radix_strlen_t len, ldns_radix_node_t* add);
static int ldns_radix_str_is_prefix(uint8_t* str1, radix_strlen_t len1,
	uint8_t* str2, radix_strlen_t len2);
static radix_strlen_t ldns_radix_str_common(uint8_t* str1, radix_strlen_t len1,
	uint8_t* str2, radix_strlen_t len2);
static ldns_radix_node_t* ldns_radix_next_in_subtree(ldns_radix_node_t* node);
static ldns_radix_node_t* ldns_radix_prev_from_index(ldns_radix_node_t* node,
	uint8_t index);
static ldns_radix_node_t* ldns_radix_last_in_subtree_incl_self(
	ldns_radix_node_t* node);
static ldns_radix_node_t* ldns_radix_last_in_subtree(ldns_radix_node_t* node);
static void ldns_radix_del_fix(ldns_radix_t* tree, ldns_radix_node_t* node);
static void ldns_radix_cleanup_onechild(ldns_radix_node_t* node);
static void ldns_radix_cleanup_leaf(ldns_radix_node_t* node);
static void ldns_radix_node_free(ldns_radix_node_t* node, void* arg);
static void ldns_radix_node_array_free(ldns_radix_node_t* node);
static void ldns_radix_node_array_free_front(ldns_radix_node_t* node);
static void ldns_radix_node_array_free_end(ldns_radix_node_t* node);
static void ldns_radix_array_reduce(ldns_radix_node_t* node);
static void ldns_radix_self_or_prev(ldns_radix_node_t* node,
	ldns_radix_node_t** result);


/**
 * Create a new radix node.
 *
 */
static ldns_radix_node_t*
ldns_radix_new_node(void* data, uint8_t* key, radix_strlen_t len)
{
	ldns_radix_node_t* node = LDNS_MALLOC(ldns_radix_node_t);
	if (!node) {
		return NULL;
	}
	node->data = data;
	node->key = key;
	node->klen = len;
	node->parent = NULL;
	node->parent_index = 0;
	node->len = 0;
	node->offset = 0;
	node->capacity = 0;
	node->array = NULL;
	return node;
}


/**
 * Create a new radix tree.
 *
 */
ldns_radix_t *
ldns_radix_create(void)
{
	ldns_radix_t* tree;

	/** Allocate memory for it */
	tree = (ldns_radix_t *) LDNS_MALLOC(ldns_radix_t);
	if (!tree) {
		return NULL;
	}
	/** Initialize it */
	ldns_radix_init(tree);
	return tree;
}


/**
 * Initialize radix tree.
 *
 */
void
ldns_radix_init(ldns_radix_t* tree)
{
	/** Initialize it */
	if (tree) {
		tree->root = NULL;
		tree->count = 0;
	}
	return;
}


/**
 * Free radix tree.
 *
 */
void
ldns_radix_free(ldns_radix_t* tree)
{
	if (tree) {
		if (tree->root) {
			ldns_radix_traverse_postorder(tree->root,
				ldns_radix_node_free, NULL);
		}
		LDNS_FREE(tree);
	}
	return;
}


/**
 * Insert data into the tree.
 *
 */
ldns_status
ldns_radix_insert(ldns_radix_t* tree, uint8_t* key, radix_strlen_t len,
	void* data)
{
	radix_strlen_t pos = 0;
	ldns_radix_node_t* add = NULL;
	ldns_radix_node_t* prefix = NULL;

	if (!tree || !key || !data) {
		return LDNS_STATUS_NULL;
	}
	add = ldns_radix_new_node(data, key, len);
	if (!add) {
		return LDNS_STATUS_MEM_ERR;
	}
	/** Search the trie until we can make no further process. */
	if (!ldns_radix_find_prefix(tree, key, len, &prefix, &pos)) {
		/** No prefix found */
		assert(tree->root == NULL);
		if (len == 0) {
			/**
			 * Example 1: The root:
			 * | [0]
			 **/
			tree->root = add;
		} else {
			/** Example 2: 'dns':
			 * | [0]
			 * --| [d+ns] dns
			 **/
			prefix = ldns_radix_new_node(NULL, (uint8_t*)"", 0);
			if (!prefix) {
				LDNS_FREE(add);
				return LDNS_STATUS_MEM_ERR;
			}
			/** Find some space in the array for the first byte */
			if (!ldns_radix_array_space(prefix, key[0])) {
				LDNS_FREE(add);
				LDNS_FREE(prefix->array);
				LDNS_FREE(prefix);
				return LDNS_STATUS_MEM_ERR;
			}
			/** Set relational pointers */
			add->parent = prefix;
			add->parent_index = 0;
			prefix->array[0].edge = add;
			if (len > 1) {
				/** Store the remainder of the prefix */
				if (!ldns_radix_prefix_remainder(1, key,
					len, &prefix->array[0].str,
					&prefix->array[0].len)) {
					LDNS_FREE(add);
					LDNS_FREE(prefix->array);
					LDNS_FREE(prefix);
					return LDNS_STATUS_MEM_ERR;
				}
			}
			tree->root = prefix;
		}
	} else if (pos == len) {
		/** Exact match found */
		if (prefix->data) {
			/* Element already exists */
			LDNS_FREE(add);
			return LDNS_STATUS_EXISTS_ERR;
		}
		prefix->data = data;
		prefix->key = key;
		prefix->klen = len; /* redundant */
	} else {
		/** Prefix found */
		uint8_t byte = key[pos];
		assert(pos < len);
		if (byte < prefix->offset ||
			(byte - prefix->offset) >= prefix->len) {
			/** Find some space in the array for the byte. */
			/**
			 * Example 3: 'ldns'
			 * | [0]
			 * --| [d+ns] dns
			 * --| [l+dns] ldns
			 **/
			if (!ldns_radix_array_space(prefix, byte)) {
				LDNS_FREE(add);
				return LDNS_STATUS_MEM_ERR;
			}
			assert(byte >= prefix->offset);
			assert((byte - prefix->offset) <= prefix->len);
			byte -= prefix->offset;
			if (pos+1 < len) {
				/** Create remainder of the string. */
				if (!ldns_radix_str_create(
					&prefix->array[byte], key, pos+1,
					len)) {
					LDNS_FREE(add);
					return LDNS_STATUS_MEM_ERR;
				}
			}
			/** Add new node. */
			add->parent = prefix;
			add->parent_index = byte;
			prefix->array[byte].edge = add;
		} else if (prefix->array[byte-prefix->offset].edge == NULL) {
			/** Use existing element. */
			/**
			 * Example 4: 'edns'
			 * | [0]
			 * --| [d+ns] dns
			 * --| [e+dns] edns
			 * --| [l+dns] ldns
			 **/
			byte -= prefix->offset;
			if (pos+1 < len) {
				/** Create remainder of the string. */
				if (!ldns_radix_str_create(
					&prefix->array[byte], key, pos+1,
					len)) {
					LDNS_FREE(add);
					return LDNS_STATUS_MEM_ERR;
				}
			}
			/** Add new node. */
			add->parent = prefix;
			add->parent_index = byte;
			prefix->array[byte].edge = add;
		} else {
			/**
			 * Use existing element, but it has a shared prefix,
			 * we need a split.
			 */
			if (!ldns_radix_array_split(&prefix->array[byte-(prefix->offset)],
				key, pos+1, len, add)) {
				LDNS_FREE(add);
				return LDNS_STATUS_MEM_ERR;
			}
		}
	}

	tree->count ++;
	return LDNS_STATUS_OK;
}


/**
 * Delete data from the tree.
 *
 */
void* ldns_radix_delete(ldns_radix_t* tree, const uint8_t* key, radix_strlen_t len)
{
    ldns_radix_node_t* del = ldns_radix_search(tree, key, len);
    void* data = NULL;
    if (del) {
        tree->count--;
        data = del->data;
        del->data = NULL;
        ldns_radix_del_fix(tree, del);
        return data;
    }
    return NULL;
}


/**
 * Search data in the tree.
 *
 */
ldns_radix_node_t*
ldns_radix_search(ldns_radix_t* tree, const uint8_t* key, radix_strlen_t len)
{
	ldns_radix_node_t* node = NULL;
	radix_strlen_t pos = 0;
	uint8_t byte = 0;

	if (!tree || !key) {
		return NULL;
	}
	node = tree->root;
	while (node) {
		if (pos == len) {
			return node->data?node:NULL;
		}
		byte = key[pos];
		if (byte < node->offset) {
			return NULL;
		}
		byte -= node->offset;
		if (byte >= node->len) {
			return NULL;
		}
		pos++;
		if (node->array[byte].len > 0) {
			/** Must match additional string. */
			if (pos + node->array[byte].len > len) {
				return NULL;
			}
			if (memcmp(&key[pos], node->array[byte].str,
				node->array[byte].len) != 0) {
				return NULL;
			}
			pos += node->array[byte].len;
		}
		node = node->array[byte].edge;
	}
	return NULL;
}


/**
 * Search data in the tree, and if not found, find the closest smaller
 * element in the tree.
 *
 */
int
ldns_radix_find_less_equal(ldns_radix_t* tree, const uint8_t* key,
	radix_strlen_t len, ldns_radix_node_t** result)
{
	ldns_radix_node_t* node = NULL;
	radix_strlen_t pos = 0;
	uint8_t byte;
	int memcmp_res = 0;

	if (!tree || !tree->root || !key) {
		*result = NULL;
		return 0;
	}

	node = tree->root;
	while (pos < len) {
		byte = key[pos];
		if (byte < node->offset) {
			/**
			 * No exact match. The lesser is in this or the
			 * previous node.
			 */
			ldns_radix_self_or_prev(node, result);
			return 0;
		}
		byte -= node->offset;
		if (byte >= node->len) {
			/**
			 * No exact match. The lesser is in this node or the
			 * last of this array, or something before this node.
			 */
			*result = ldns_radix_last_in_subtree_incl_self(node);
			if (*result == NULL) {
				*result = ldns_radix_prev(node);
			}
			return 0;
		}
		pos++;
		if (!node->array[byte].edge) {
			/**
			 * No exact match. Find the previous in the array
			 * from this index.
			 */
			*result = ldns_radix_prev_from_index(node, byte);
			if (*result == NULL) {
				ldns_radix_self_or_prev(node, result);
			}
			return 0;
		}
		if (node->array[byte].len != 0) {
			/** Must match additional string. */
			if (pos + node->array[byte].len > len) {
				/** Additional string is longer than key. */
				if (memcmp(&key[pos], node->array[byte].str,
					len-pos) <= 0) {
					/** Key is before this node. */
					*result = ldns_radix_prev(
						node->array[byte].edge);
				} else {
					/** Key is after additional string. */
					*result = ldns_radix_last_in_subtree_incl_self(node->array[byte].edge);
					if (*result == NULL) {
						 *result = ldns_radix_prev(node->array[byte].edge);
					}
				}
				return 0;
			}
			memcmp_res = memcmp(&key[pos], node->array[byte].str,
				node->array[byte].len);
			if (memcmp_res < 0) {
				*result = ldns_radix_prev(
					node->array[byte].edge);
				return 0;
			} else if (memcmp_res > 0) {
				*result = ldns_radix_last_in_subtree_incl_self(node->array[byte].edge);
				if (*result == NULL) {
					 *result = ldns_radix_prev(node->array[byte].edge);
				}
				return 0;
			}

			pos += node->array[byte].len;
		}
		node = node->array[byte].edge;
	}
	if (node->data) {
		/** Exact match. */
		*result = node;
		return 1;
	}
	/** There is a node which is an exact match, but has no element. */
	*result = ldns_radix_prev(node);
	return 0;
}


/**
 * Get the first element in the tree.
 *
 */
ldns_radix_node_t*
ldns_radix_first(const ldns_radix_t* tree)
{
	ldns_radix_node_t* first = NULL;
	if (!tree || !tree->root) {
		return NULL;
	}
	first = tree->root;
	if (first->data) {
		return first;
	}
	return ldns_radix_next(first);
}


/**
 * Get the last element in the tree.
 *
 */
ldns_radix_node_t*
ldns_radix_last(const ldns_radix_t* tree)
{
	if (!tree || !tree->root) {
		return NULL;
	}
	return ldns_radix_last_in_subtree_incl_self(tree->root);
}


/**
 * Next element.
 *
 */
ldns_radix_node_t*
ldns_radix_next(ldns_radix_node_t* node)
{
	if (!node) {
		return NULL;
	}
	if (node->len) {
		/** Go down: most-left child is the next. */
		ldns_radix_node_t* next = ldns_radix_next_in_subtree(node);
		if (next) {
			return next;
		}
	}
	/** No elements in subtree, get to parent and go down next branch. */
	while (node->parent) {
		uint8_t index = node->parent_index;
		node = node->parent;
		index++;
		for (; index < node->len; index++) {
			if (node->array[index].edge) {
				ldns_radix_node_t* next;
				/** Node itself. */
				if (node->array[index].edge->data) {
					return node->array[index].edge;
				}
				/** Dive into subtree. */
				next = ldns_radix_next_in_subtree(node);
				if (next) {
					return next;
				}
			}
		}
	}
	return NULL;
}


/**
 * Previous element.
 *
 */
ldns_radix_node_t*
ldns_radix_prev(ldns_radix_node_t* node)
{
	if (!node) {
		return NULL;
	}

	/** Get to parent and go down previous branch. */
	while (node->parent) {
		uint8_t index = node->parent_index;
		ldns_radix_node_t* prev;
		node = node->parent;
		assert(node->len > 0);
		prev = ldns_radix_prev_from_index(node, index);
		if (prev) {
			return prev;
		}
		if (node->data) {
			return node;
		}
	}
	return NULL;
}


/**
 * Print node.
 *
 */
static void
ldns_radix_node_print(FILE* fd, ldns_radix_node_t* node,
	uint8_t i, uint8_t* str, radix_strlen_t len, unsigned d)
{
	uint8_t j;
	if (!node) {
		return;
	}
	for (j = 0; j < d; j++) {
		fprintf(fd, "--");
	}
	if (str) {
		radix_strlen_t l;
		fprintf(fd, "| [%u+", (unsigned) i);
		for (l=0; l < len; l++) {
			fprintf(fd, "%c", (char) str[l]);
		}
		fprintf(fd, "]%u", (unsigned) len);
	} else {
		fprintf(fd, "| [%u]", (unsigned) i);
	}

	if (node->data) {
		fprintf(fd, " %s", (char*) node->data);
	}
	fprintf(fd, "\n");

	for (j = 0; j < node->len; j++) {
		if (node->array[j].edge) {
			ldns_radix_node_print(fd, node->array[j].edge, j,
				node->array[j].str, node->array[j].len, d+1);
		}
	}
	return;
}


/**
 * Print radix tree.
 *
 */
void
ldns_radix_printf(FILE* fd, const ldns_radix_t* tree)
{
	if (!fd || !tree) {
		return;
	}
	if (!tree->root) {
		fprintf(fd, "; empty radix tree\n");
		return;
	}
	ldns_radix_node_print(fd, tree->root, 0, NULL, 0, 0);
	return;
}


/**
 * Join two radix trees.
 *
 */
ldns_status
ldns_radix_join(ldns_radix_t* tree1, ldns_radix_t* tree2)
{
	ldns_radix_node_t* cur_node, *next_node;
	ldns_status status;
	if (!tree2 || !tree2->root) {
		return LDNS_STATUS_OK;
	}
	/** Add all elements from tree2 into tree1. */

	cur_node = ldns_radix_first(tree2);
	while (cur_node) {
		status = LDNS_STATUS_NO_DATA;
		/** Insert current node into tree1 */
		if (cur_node->data) {
			status = ldns_radix_insert(tree1, cur_node->key,
				cur_node->klen, cur_node->data);
			/** Exist errors may occur */
			if (status != LDNS_STATUS_OK &&
			    status != LDNS_STATUS_EXISTS_ERR) {
				return status;
			}
		}
		next_node = ldns_radix_next(cur_node);
		if (status == LDNS_STATUS_OK) {
			(void) ldns_radix_delete(tree2, cur_node->key,
				cur_node->klen);
		}
		cur_node = next_node;
	}

	return LDNS_STATUS_OK;
}


/**
 * Split a radix tree intwo.
 *
 */
ldns_status
ldns_radix_split(ldns_radix_t* tree1, size_t num, ldns_radix_t** tree2)
{
	size_t count = 0;
	ldns_radix_node_t* cur_node;
	ldns_status status = LDNS_STATUS_OK;
	if (!tree1 || !tree1->root || num == 0) {
		return LDNS_STATUS_OK;
	}
	if (!tree2) {
		return LDNS_STATUS_NULL;
	}
	if (!*tree2) {
		*tree2 = ldns_radix_create();
		if (!*tree2) {
			return LDNS_STATUS_MEM_ERR;
		}
	}
	cur_node = ldns_radix_first(tree1);
	while (count < num && cur_node) {
		if (cur_node->data) {
			/** Delete current node from tree1. */
			uint8_t* cur_key = cur_node->key;
			radix_strlen_t cur_len = cur_node->klen;
			void* cur_data = ldns_radix_delete(tree1, cur_key,
				cur_len);
			/** Insert current node into tree2/ */
			if (!cur_data) {
				return LDNS_STATUS_NO_DATA;
			}
			status = ldns_radix_insert(*tree2, cur_key, cur_len,
				cur_data);
			if (status != LDNS_STATUS_OK &&
			    status != LDNS_STATUS_EXISTS_ERR) {
				return status;
			}
/*
			if (status == LDNS_STATUS_OK) {
				cur_node->key = NULL;
				cur_node->klen = 0;
			}
*/
			/** Update count; get first element from tree1 again. */
			count++;
			cur_node = ldns_radix_first(tree1);
		} else {
			cur_node = ldns_radix_next(cur_node);
		}
	}
	return LDNS_STATUS_OK;
}


/**
 * Call function for all nodes in the tree, such that leaf nodes are
 * called before parent nodes.
 *
 */
void
ldns_radix_traverse_postorder(ldns_radix_node_t* node,
	void (*func)(ldns_radix_node_t*, void*), void* arg)
{
	uint8_t i;
	if (!node) {
		return;
	}
	for (i=0; i < node->len; i++) {
		ldns_radix_traverse_postorder(node->array[i].edge,
			func, arg);
	}
	/** Call user function */
	(*func)(node, arg);
	return;
}


/** Static helper functions */

/**
 * Find a prefix of the key.
 * @param tree:   tree.
 * @param key:    key.
 * @param len:    length of key.
 * @param result: the longest prefix, the entry itself if *pos==len,
 *                otherwise an array entry.
 * @param pos:    position in string where next unmatched byte is.
 *                If *pos==len, an exact match is found.
 *                If *pos== 0, a "" match was found.
 * @return 0 (false) if no prefix found.
 *
 */
static int
ldns_radix_find_prefix(ldns_radix_t* tree, uint8_t* key,
	radix_strlen_t len, ldns_radix_node_t** result, radix_strlen_t* respos)
{
	/** Start searching at the root node */
	ldns_radix_node_t* n = tree->root;
	radix_strlen_t pos = 0;
	uint8_t byte;
	*respos = 0;
	*result = n;
        if (!n) {
		/** No root, no prefix found */
		return 0;
	}
	/** For each node, look if we can make further progress */
	while (n) {
		if (pos == len) {
			/** Exact match */
			return 1;
		}
		byte = key[pos];
		if (byte < n->offset) {
			/** key < node */
			return 1;
		}
		byte -= n->offset;
		if (byte >= n->len) {
			/** key > node */
			return 1;
		}
		/** So far, the trie matches */
		pos++;
		if (n->array[byte].len != 0) {
			/** Must match additional string */
			if (pos + n->array[byte].len > len) {
				return 1; /* no match at child node */
			}
			if (memcmp(&key[pos], n->array[byte].str,
				n->array[byte].len) != 0) {
				return 1; /* no match at child node */
			}
			pos += n->array[byte].len;
		}
		/** Continue searching prefix at this child node */
		n = n->array[byte].edge;
		if (!n) {
			return 1;
		}
		/** Update the prefix node */
		*respos = pos;
		*result = n;
	}
	/** Done */
	return 1;
}


/**
 * Make space in the node's array for another byte.
 * @param node: node.
 * @param byte: byte.
 * @return 1 if successful, 0 otherwise.
 *
 */
static int
ldns_radix_array_space(ldns_radix_node_t* node, uint8_t byte)
{
	/** Is there an array? */
	if (!node->array) {
		assert(node->capacity == 0);
		/** No array, create new array */
		node->array = LDNS_MALLOC(ldns_radix_array_t);
		if (!node->array) {
			return 0;
		}
		memset(&node->array[0], 0, sizeof(ldns_radix_array_t));
		node->len = 1;
		node->capacity = 1;
		node->offset = byte;
		return 1;
	}
	/** Array exist */
	assert(node->array != NULL);
	assert(node->capacity > 0);

	if (node->len == 0) {
		/** Unused array */
		node->len = 1;
		node->offset = byte;
	} else if (byte < node->offset) {
		/** Byte is below the offset */
		uint8_t index;
		uint16_t need = node->offset - byte;
		/** Is there enough capacity? */
		if (node->len + need > node->capacity) {
			/** Not enough capacity, grow array */
			if (!ldns_radix_array_grow(node,
				(unsigned) (node->len + need))) {
				return 0; /* failed to grow array */
			}
		}
		/** Move items to the end */
		memmove(&node->array[need], &node->array[0],
			node->len*sizeof(ldns_radix_array_t));
		/** Fix parent index */
		for (index = 0; index < node->len; index++) {
			if (node->array[index+need].edge) {
				node->array[index+need].edge->parent_index =
					index + need;
			}
		}
		/** Zero the first */
		memset(&node->array[0], 0, need*sizeof(ldns_radix_array_t));
		node->len += need;
		node->offset = byte;
	} else if (byte - node->offset >= node->len) {
		/** Byte does not fit in array */
		uint16_t need = (byte - node->offset) - node->len + 1;
		/** Is there enough capacity? */
		if (node->len + need > node->capacity) {
			/** Not enough capacity, grow array */
			if (!ldns_radix_array_grow(node,
				(unsigned) (node->len + need))) {
				return 0; /* failed to grow array */
			}
		}
		/** Zero the added items */
		memset(&node->array[node->len], 0,
			need*sizeof(ldns_radix_array_t));
		node->len += need;
	}
	return 1;
}


/**
 * Grow the array.
 * @param node: node.
 * @param need: number of elements the array at least need to grow.
 *              Can't be bigger than 256.
 * @return: 0 if failed, 1 if was successful.
 *
 */
static int
ldns_radix_array_grow(ldns_radix_node_t* node, unsigned need)
{
	unsigned size = ((unsigned)node->capacity)*2;
	ldns_radix_array_t* a = NULL;
	if (need > size) {
		size = need;
	}
	if (size > 256) {
		size = 256;
	}
	a = LDNS_XMALLOC(ldns_radix_array_t, size);
	if (!a) {
		return 0;
	}
	assert(node->len <= node->capacity);
	assert(node->capacity < size);
	memcpy(&a[0], &node->array[0], node->len*sizeof(ldns_radix_array_t));
	LDNS_FREE(node->array);
	node->array = a;
	node->capacity = size;
	return 1;
}


/**
 * Create a prefix in the array string.
 * @param array: array.
 * @param key:   key.
 * @param pos:   start position in key.
 * @param len:   length of key.
 * @return 0 if failed, 1 if was successful.
 *
 */
static int
ldns_radix_str_create(ldns_radix_array_t* array, uint8_t* key,
	radix_strlen_t pos, radix_strlen_t len)
{
	array->str = LDNS_XMALLOC(uint8_t, (len-pos));
	if (!array->str) {
		return 0;
	}
	memmove(array->str, key+pos, len-pos);
	array->len = (len-pos);
	return 1;
}


/**
 * Allocate remainder from prefixes for a split.
 * @param prefixlen:  length of prefix.
 * @param longer_str: the longer string.
 * @param longer_len: the longer string length.
 * @param split_str:  the split string.
 * @param split_len:  the split string length.
 * @return 0 if failed, 1 if successful.
 *
 */
static int
ldns_radix_prefix_remainder(radix_strlen_t prefix_len,
	uint8_t* longer_str, radix_strlen_t longer_len,
	uint8_t** split_str, radix_strlen_t* split_len)
{
	*split_len = longer_len - prefix_len;
	*split_str = LDNS_XMALLOC(uint8_t, (*split_len));
	if (!*split_str) {
		return 0;
	}
	memmove(*split_str, longer_str+prefix_len, longer_len-prefix_len);
	return 1;
}


/**
 * Create a split when two nodes have a shared prefix.
 * @param array: array.
 * @param key:   key.
 * @param pos:   start position in key.
 * @param len:   length of the key.
 * @param add:   node to be added.
 * @return 0 if failed, 1 if was successful.
 *
 */
static int
ldns_radix_array_split(ldns_radix_array_t* array, uint8_t* key,
	radix_strlen_t pos, radix_strlen_t len, ldns_radix_node_t* add)
{
	uint8_t* str_to_add = key + pos;
	radix_strlen_t strlen_to_add = len - pos;

	if (ldns_radix_str_is_prefix(str_to_add, strlen_to_add,
		array->str, array->len)) {
		/** The string to add is a prefix of the existing string */
		uint8_t* split_str = NULL, *dup_str = NULL;
		radix_strlen_t split_len = 0;
		/**
		 * Example 5: 'ld'
		 * | [0]
		 * --| [d+ns] dns
		 * --| [e+dns] edns
		 * --| [l+d] ld
		 * ----| [n+s] ldns
		 **/
		assert(strlen_to_add < array->len);
		/** Store the remainder in the split string */
		if (array->len - strlen_to_add > 1) {
			if (!ldns_radix_prefix_remainder(strlen_to_add+1,
				array->str, array->len, &split_str,
				&split_len)) {
				return 0;
			}
		}
		/** Duplicate the string to add */
		if (strlen_to_add != 0) {
			dup_str = LDNS_XMALLOC(uint8_t, strlen_to_add);
			if (!dup_str) {
				LDNS_FREE(split_str);
				return 0;
			}
			memcpy(dup_str, str_to_add, strlen_to_add);
		}
		/** Make space in array for the new node */
		if (!ldns_radix_array_space(add,
			array->str[strlen_to_add])) {
			LDNS_FREE(split_str);
			LDNS_FREE(dup_str);
			return 0;
		}
		/**
		 * The added node should go direct under the existing parent.
		 * The existing node should go under the added node.
		 */
		add->parent = array->edge->parent;
		add->parent_index = array->edge->parent_index;
		add->array[0].edge = array->edge;
		add->array[0].str = split_str;
		add->array[0].len = split_len;
		array->edge->parent = add;
		array->edge->parent_index = 0;
		LDNS_FREE(array->str);
		array->edge = add;
		array->str = dup_str;
		array->len = strlen_to_add;
	} else if (ldns_radix_str_is_prefix(array->str, array->len,
		str_to_add, strlen_to_add)) {
		/** The existing string is a prefix of the string to add */
		/**
		 * Example 6: 'dns-ng'
		 * | [0]
		 * --| [d+ns] dns
		 * ----| [-+ng] dns-ng
		 * --| [e+dns] edns
		 * --| [l+d] ld
		 * ----| [n+s] ldns
		 **/
		uint8_t* split_str = NULL;
		radix_strlen_t split_len = 0;
		assert(array->len < strlen_to_add);
		if (strlen_to_add - array->len > 1) {
			if (!ldns_radix_prefix_remainder(array->len+1,
				str_to_add, strlen_to_add, &split_str,
				&split_len)) {
				return 0;
			}
		}
		/** Make space in array for the new node */
		if (!ldns_radix_array_space(array->edge,
			str_to_add[array->len])) {
			LDNS_FREE(split_str);
			return 0;
		}
		/**
		 * The added node should go direct under the existing node.
		 */
		add->parent = array->edge;
		add->parent_index = str_to_add[array->len] -
							array->edge->offset;
		array->edge->array[add->parent_index].edge = add;
		array->edge->array[add->parent_index].str = split_str;
		array->edge->array[add->parent_index].len = split_len;
	} else {
		/** Create a new split node. */
		/**
		 * Example 7: 'dndns'
		 * | [0]
		 * --| [d+n]
		 * ----| [d+ns] dndns
		 * ----| [s] dns
		 * ------| [-+ng] dns-ng
		 * --| [e+dns] edns
		 * --| [l+d] ld
		 * ----| [n+s] ldns
		 **/
		ldns_radix_node_t* common = NULL;
		uint8_t* common_str = NULL, *s1 = NULL, *s2 = NULL;
		radix_strlen_t common_len = 0, l1 = 0, l2 = 0;
		common_len = ldns_radix_str_common(array->str, array->len,
			str_to_add, strlen_to_add);
		assert(common_len < array->len);
		assert(common_len < strlen_to_add);
		/** Create the new common node. */
		common = ldns_radix_new_node(NULL, (uint8_t*)"", 0);
		if (!common) {
			return 0;
		}
		if (array->len - common_len > 1) {
			if (!ldns_radix_prefix_remainder(common_len+1,
				array->str, array->len, &s1, &l1)) {
				return 0;
			}
		}
		if (strlen_to_add - common_len > 1) {
			if (!ldns_radix_prefix_remainder(common_len+1,
				str_to_add, strlen_to_add, &s2, &l2)) {
				return 0;
			}
		}
		/** Create the shared prefix. */
		if (common_len > 0) {
			common_str = LDNS_XMALLOC(uint8_t, common_len);
			if (!common_str) {
				LDNS_FREE(common);
				LDNS_FREE(s1);
				LDNS_FREE(s2);
				return 0;
			}
			memcpy(common_str, str_to_add, common_len);
		}
		/** Make space in the common node array. */
		if (!ldns_radix_array_space(common, array->str[common_len]) ||
		    !ldns_radix_array_space(common, str_to_add[common_len])) {
			LDNS_FREE(common->array);
			LDNS_FREE(common);
			LDNS_FREE(common_str);
			LDNS_FREE(s1);
			LDNS_FREE(s2);
			return 0;
		}
		/**
		 * The common node should go direct under the parent node.
		 * The added and existing nodes go under the common node.
		 */
		common->parent = array->edge->parent;
		common->parent_index = array->edge->parent_index;
		array->edge->parent = common;
		array->edge->parent_index = array->str[common_len] -
								common->offset;
		add->parent = common;
		add->parent_index = str_to_add[common_len] - common->offset;
		common->array[array->edge->parent_index].edge = array->edge;
		common->array[array->edge->parent_index].str = s1;
		common->array[array->edge->parent_index].len = l1;
		common->array[add->parent_index].edge = add;
		common->array[add->parent_index].str = s2;
		common->array[add->parent_index].len = l2;
		LDNS_FREE(array->str);
		array->edge = common;
		array->str = common_str;
		array->len = common_len;
	}
	return 1;
}


/**
 * Check if one string prefix of other string.
 * @param str1: one string.
 * @param len1: one string length.
 * @param str2: other string.
 * @param len2: other string length.
 * @return 1 if prefix, 0 otherwise.
 *
 */
static int
ldns_radix_str_is_prefix(uint8_t* str1, radix_strlen_t len1,
	uint8_t* str2, radix_strlen_t len2)
{
	if (len1 == 0) {
		return 1; /* empty prefix is also a prefix */
	}
	if (len1 > len2) {
		return 0; /* len1 is longer so str1 cannot be a prefix */
	}
	return (memcmp(str1, str2, len1) == 0);
}


/**
 * Return the number of bytes in common for the two strings.
 * @param str1: one string.
 * @param len1: one string length.
 * @param str2: other string.
 * @param len2: other string length.
 * @return length of substring that the two strings have in common.
 *
 */
static radix_strlen_t
ldns_radix_str_common(uint8_t* str1, radix_strlen_t len1,
	uint8_t* str2, radix_strlen_t len2)
{
	radix_strlen_t i, max = (len1<len2)?len1:len2;
	for (i=0; i<max; i++) {
		if (str1[i] != str2[i]) {
			return i;
		}
	}
	return max;
}


/**
 * Find the next element in the subtree of this node.
 * @param node: node.
 * @return: node with next element.
 *
 */
static ldns_radix_node_t*
ldns_radix_next_in_subtree(ldns_radix_node_t* node)
{
	uint16_t i;
	ldns_radix_node_t* next;
	/** Try every subnode. */
	for (i = 0; i < node->len; i++) {
		if (node->array[i].edge) {
			/** Node itself. */
			if (node->array[i].edge->data) {
				return node->array[i].edge;
			}
			/** Dive into subtree. */
			next = ldns_radix_next_in_subtree(node->array[i].edge);
			if (next) {
				return next;
			}
		}
	}
	return NULL;
}


/**
 * Find the previous element in the array of this node, from index.
 * @param node: node.
 * @param index: index.
 * @return previous node from index.
 *
 */
static ldns_radix_node_t*
ldns_radix_prev_from_index(ldns_radix_node_t* node, uint8_t index)
{
	uint8_t i = index;
	while (i > 0) {
		i--;
		if (node->array[i].edge) {
			ldns_radix_node_t* prev =
				ldns_radix_last_in_subtree_incl_self(node);
			if (prev) {
				return prev;
			}
		}
	}
	return NULL;
}


/**
 * Find last node in subtree, or this node (if have data).
 * @param node: node.
 * @return last node in subtree, or this node, or NULL.
 *
 */
static ldns_radix_node_t*
ldns_radix_last_in_subtree_incl_self(ldns_radix_node_t* node)
{
	ldns_radix_node_t* last = ldns_radix_last_in_subtree(node);
	if (last) {
		return last;
	} else if (node->data) {
		return node;
	}
	return NULL;
}


/**
 * Find last node in subtree.
 * @param node: node.
 * @return last node in subtree.
 *
 */
static ldns_radix_node_t*
ldns_radix_last_in_subtree(ldns_radix_node_t* node)
{
	int i;
	/** Look for the most right leaf node. */
	for (i=(int)(node->len)-1; i >= 0; i--) {
		if (node->array[i].edge) {
			/** Keep looking for the most right leaf node. */
			if (node->array[i].edge->len > 0) {
				ldns_radix_node_t* last =
					ldns_radix_last_in_subtree(
					node->array[i].edge);
				if (last) {
					return last;
				}
			}
			/** Could this be the most right leaf node? */
			if (node->array[i].edge->data) {
				return node->array[i].edge;
			}
		}
	}
	return NULL;
}


/**
 * Fix tree after deleting element.
 * @param tree: tree.
 * @param node: node with deleted element.
 *
 */
static void
ldns_radix_del_fix(ldns_radix_t* tree, ldns_radix_node_t* node)
{
	while (node) {
		if (node->data) {
			/** Thou should not delete nodes with data attached. */
			return;
		} else if (node->len == 1 && node->parent) {
			/** Node with one child is fold back into. */
			ldns_radix_cleanup_onechild(node);
			return;
		} else if (node->len == 0) {
			/** Leaf node. */
			ldns_radix_node_t* parent = node->parent;
			if (!parent) {
				/** The root is a leaf node. */
				ldns_radix_node_free(node, NULL);
				tree->root = NULL;
				return;
			}
			/** Cleanup leaf node and continue with parent. */
			ldns_radix_cleanup_leaf(node);
			node = parent;
		} else {
			/**
			 * Node cannot be deleted, because it has edge nodes
			 * and no parent to fix up to.
			 */
			return;
		}
	}
	/** Not reached. */
	return;
}


/**
 * Clean up a node with one child.
 * @param node: node with one child.
 *
 */
static void
ldns_radix_cleanup_onechild(ldns_radix_node_t* node)
{
	uint8_t* join_str;
	radix_strlen_t join_len;
	uint8_t parent_index = node->parent_index;
	ldns_radix_node_t* child = node->array[0].edge;
	ldns_radix_node_t* parent = node->parent;

	/** Node has one child, merge the child node into the parent node. */
	assert(parent_index < parent->len);
	join_len = parent->array[parent_index].len + node->array[0].len + 1;

	join_str = LDNS_XMALLOC(uint8_t, join_len);
	if (!join_str) {
		/**
		 * Cleanup failed due to out of memory.
		 * This tree is now inefficient, with the empty node still
		 * existing, but it is still valid.
		 */
		return;
	}

	memcpy(join_str, parent->array[parent_index].str,
		parent->array[parent_index].len);
	join_str[parent->array[parent_index].len] = child->parent_index +
		node->offset;
	memmove(join_str + parent->array[parent_index].len+1,
		node->array[0].str, node->array[0].len);

	LDNS_FREE(parent->array[parent_index].str);
	parent->array[parent_index].str = join_str;
	parent->array[parent_index].len = join_len;
	parent->array[parent_index].edge = child;
	child->parent = parent;
	child->parent_index = parent_index;
	ldns_radix_node_free(node, NULL);
	return;
}


/**
 * Clean up a leaf node.
 * @param node: leaf node.
 *
 */
static void
ldns_radix_cleanup_leaf(ldns_radix_node_t* node)
{
	uint8_t parent_index = node->parent_index;
	ldns_radix_node_t* parent = node->parent;
	/** Delete lead node and fix parent array. */
	assert(parent_index < parent->len);
	ldns_radix_node_free(node, NULL);
	LDNS_FREE(parent->array[parent_index].str);
	parent->array[parent_index].str = NULL;
	parent->array[parent_index].len = 0;
	parent->array[parent_index].edge = NULL;
	/** Fix array in parent. */
	if (parent->len == 1) {
		ldns_radix_node_array_free(parent);
	} else if (parent_index == 0) {
		ldns_radix_node_array_free_front(parent);
	} else {
		ldns_radix_node_array_free_end(parent);
	}
	return;
}


/**
 * Free a radix node.
 * @param node: node.
 * @param arg: user argument.
 *
 */
static void
ldns_radix_node_free(ldns_radix_node_t* node, void* arg)
{
	uint16_t i;
	(void) arg;
	if (!node) {
		return;
	}
	for (i=0; i < node->len; i++) {
		LDNS_FREE(node->array[i].str);
	}
	node->key = NULL;
	node->klen = 0;
	LDNS_FREE(node->array);
	LDNS_FREE(node);
	return;
}


/**
 * Free select edge array.
 * @param node: node.
 *
 */
static void
ldns_radix_node_array_free(ldns_radix_node_t* node)
{
	node->offset = 0;
	node->len = 0;
	LDNS_FREE(node->array);
	node->array = NULL;
	node->capacity = 0;
	return;
}


/**
 * Free front of select edge array.
 * @param node: node.
 *
 */
static void
ldns_radix_node_array_free_front(ldns_radix_node_t* node)
{
	uint16_t i, n = 0;
	/** Remove until a non NULL entry. */
   	while (n < node->len && node->array[n].edge == NULL) {
		n++;
	}
	if (n == 0) {
		return;
	}
	if (n == node->len) {
		ldns_radix_node_array_free(node);
		return;
	}
	assert(n < node->len);
	assert((int) n <= (255 - (int) node->offset));
	memmove(&node->array[0], &node->array[n],
		(node->len - n)*sizeof(ldns_radix_array_t));
	node->offset += n;
	node->len -= n;
	for (i=0; i < node->len; i++) {
		if (node->array[i].edge) {
			node->array[i].edge->parent_index = i;
		}
	}
	ldns_radix_array_reduce(node);
	return;
}


/**
 * Free front of select edge array.
 * @param node: node.
 *
 */
static void
ldns_radix_node_array_free_end(ldns_radix_node_t* node)
{
	uint16_t n = 0;
	/** Shorten array. */
	while (n < node->len && node->array[node->len-1-n].edge == NULL) {
		n++;
	}
	if (n == 0) {
		return;
	}
	if (n == node->len) {
		ldns_radix_node_array_free(node);
		return;
	}
	assert(n < node->len);
	node->len -= n;
	ldns_radix_array_reduce(node);
	return;
}


/**
 * Reduce the capacity of the array if needed.
 * @param node: node.
 *
 */
static void
ldns_radix_array_reduce(ldns_radix_node_t* node)
{
	if (node->len <= node->capacity/2 && node->len != node->capacity) {
		ldns_radix_array_t* a = LDNS_XMALLOC(ldns_radix_array_t,
								node->len);
		if (!a) {
			return;
		}
		memcpy(a, node->array, sizeof(ldns_radix_array_t)*node->len);
		LDNS_FREE(node->array);
		node->array = a;
		node->capacity = node->len;
	}
	return;
}


/**
 * Return this element if it exists, the previous otherwise.
 * @param node: from this node.
 * @param result: result node.
 *
 */
static void
ldns_radix_self_or_prev(ldns_radix_node_t* node, ldns_radix_node_t** result)
{
	if (node->data) {
		*result = node;
	} else {
		*result = ldns_radix_prev(node);
	}
	return;
}
