/*
 * util/storage/dnstree.h - support for rbtree types suitable for DNS code.
 *
 * Copyright (c) 2008, NLnet Labs. All rights reserved.
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
 */

/**
 * \file
 *
 * This file contains structures combining types and functions to
 * manipulate those structures that help building DNS lookup trees.
 */

#ifndef UTIL_STORAGE_DNSTREE_H
#define UTIL_STORAGE_DNSTREE_H
#include "util/rbtree.h"

/**
 * Tree of domain names.  Sorted first by class then by name.
 * This is not sorted canonically, but fast.
 * This can be looked up to obtain a closest encloser parent name.
 *
 * The tree itself is a rbtree_type.
 * This is the element node put as first entry in the client structure.
 */
struct name_tree_node {
	/** rbtree node, key is this struct : dclass and name */
	rbnode_type node;
	/** parent in tree */
	struct name_tree_node* parent;
	/** name in uncompressed wireformat */
	uint8_t* name;
	/** length of name */
	size_t len;
	/** labels in name */
	int labs;
	/** the class of the name (host order) */
	uint16_t dclass;
};

/**
 * Tree of IP addresses.  Sorted first by protocol, then by bits.
 * This can be looked up to obtain the enclosing subnet.
 *
 * The tree itself is a rbtree_type.
 * This is the element node put as first entry in the client structure.
 */
struct addr_tree_node {
	/** rbtree node, key is this struct : proto and subnet */
	rbnode_type node;
	/** parent in tree */
	struct addr_tree_node* parent;
	/** address */
	struct sockaddr_storage addr;
	/** length of addr */
	socklen_t addrlen;
	/** netblock size */
	int net;
};

/**
 * Init a name tree to be empty
 * @param tree: to init.
 */
void name_tree_init(rbtree_type* tree);

/**
 * insert element into name tree.
 * @param tree: name tree
 * @param node: node element (at start of a structure that caller
 *	has allocated).
 * @param name: name to insert (wireformat)
 *	this node has been allocated by the caller and it itself inserted.
 * @param len: length of name
 * @param labs: labels in name
 * @param dclass: class of name
 * @return false on error (duplicate element).
 */
int name_tree_insert(rbtree_type* tree, struct name_tree_node* node, 
	uint8_t* name, size_t len, int labs, uint16_t dclass);

/**
 * Initialize parent pointers in name tree.
 * Should be performed after insertions are done, before lookups
 * @param tree: name tree
 */
void name_tree_init_parents(rbtree_type* tree);

/**
 * Lookup exact match in name tree
 * @param tree: name tree
 * @param name: wireformat name
 * @param len: length of name
 * @param labs: labels in name
 * @param dclass: class of name
 * @return node or NULL if not found.
 */
struct name_tree_node* name_tree_find(rbtree_type* tree, uint8_t* name, 
	size_t len, int labs, uint16_t dclass);

/**
 * Lookup closest encloser in name tree.
 * @param tree: name tree
 * @param name: wireformat name
 * @param len: length of name
 * @param labs: labels in name
 * @param dclass: class of name
 * @return closest enclosing node (could be equal) or NULL if not found.
 */
struct name_tree_node* name_tree_lookup(rbtree_type* tree, uint8_t* name, 
	size_t len, int labs, uint16_t dclass);

/**
 * Find next root item in name tree.
 * @param tree: the nametree.
 * @param dclass: the class to look for next (or higher).
 * @return false if no classes found, true means class put into c.
 */
int name_tree_next_root(rbtree_type* tree, uint16_t* dclass);

/**
 * Init addr tree to be empty.
 * @param tree: to init.
 */
void addr_tree_init(rbtree_type* tree);

/**
 * insert element into addr tree.
 * @param tree: addr tree
 * @param node: node element (at start of a structure that caller
 *	has allocated).
 * @param addr: to insert (copied).
 * @param addrlen: length of addr
 * @param net: size of subnet. 
 * @return false on error (duplicate element).
 */
int addr_tree_insert(rbtree_type* tree, struct addr_tree_node* node, 
	struct sockaddr_storage* addr, socklen_t addrlen, int net);

/**
 * Initialize parent pointers in addr tree.
 * Should be performed after insertions are done, before lookups
 * @param tree: addr tree
 */
void addr_tree_init_parents(rbtree_type* tree);

/**
 * Lookup closest encloser in addr tree.
 * @param tree: addr tree
 * @param addr: to lookup.
 * @param addrlen: length of addr
 * @return closest enclosing node (could be equal) or NULL if not found.
 */
struct addr_tree_node* addr_tree_lookup(rbtree_type* tree, 
	struct sockaddr_storage* addr, socklen_t addrlen);

/**
 * Find element in addr tree.  (search a netblock, not a match for an address)
 * @param tree: addr tree
 * @param addr: netblock to lookup.
 * @param addrlen: length of addr
 * @param net: size of subnet
 * @return addr tree element, or NULL if not found.
 */
struct addr_tree_node* addr_tree_find(rbtree_type* tree, 
	struct sockaddr_storage* addr, socklen_t addrlen, int net);

/** compare name tree nodes */
int name_tree_compare(const void* k1, const void* k2);

/** compare addr tree nodes */
int addr_tree_compare(const void* k1, const void* k2);

#endif /* UTIL_STORAGE_DNSTREE_H */
