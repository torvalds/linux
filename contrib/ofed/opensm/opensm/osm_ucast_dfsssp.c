/*
 * Copyright (c) 2004-2008 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2015 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009-2015 ZIH, TU Dresden, Federal Republic of Germany. All rights reserved.
 * Copyright (C) 2012-2017 Tokyo Institute of Technology. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * Abstract:
 *    Implementation of OpenSM (deadlock-free) single-source-shortest-path routing
 *    (with dijkstra algorithm)
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_UCAST_DFSSSP_C
#include <opensm/osm_ucast_mgr.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_node.h>
#include <opensm/osm_multicast.h>
#include <opensm/osm_mcast_mgr.h>

/* "infinity" for dijkstra */
#define INF      0x7FFFFFFF

enum {
	UNDISCOVERED = 0,
	DISCOVERED
};

enum {
	UNKNOWN = 0,
	GRAY,
	BLACK,
};

typedef struct link {
	uint64_t guid;		/* guid of the neighbor behind the link */
	uint32_t from;		/* base_index in the adjazenz list (start of the link) */
	uint8_t from_port;	/* port on the base_side (needed for weight update to identify the correct link for multigraphs) */
	uint32_t to;		/* index of the neighbor in the adjazenz list (end of the link) */
	uint8_t to_port;	/* port on the side of the neighbor (needed for the LFT) */
	uint64_t weight;	/* link weight */
	struct link *next;
} link_t;

typedef struct vertex {
	/* informations of the fabric */
	uint64_t guid;
	uint16_t lid;		/* for lft filling */
	uint32_t num_hca;	/* numbers of Hca/LIDs on the switch, for weight calculation */
	link_t *links;
	uint8_t hops;
	/* for dijkstra routing */
	link_t *used_link;	/* link between the vertex discovered before and this vertex */
	uint64_t distance;	/* distance from source to this vertex */
	uint8_t state;
	/* for the binary heap */
	uint32_t heap_id;
	/* for LFT writing and debug */
	osm_switch_t *sw;	/* selfpointer */
	boolean_t dropped;	/* indicate dropped switches (w/ ucast cache) */
} vertex_t;

typedef struct binary_heap {
	uint32_t size;		/* size of the heap */
	vertex_t **nodes;	/* array with pointers to elements of the adj_list */
} binary_heap_t;

typedef struct vltable {
	uint64_t num_lids;	/* size of the lids array */
	uint16_t *lids;		/* sorted array of all lids in the subnet */
	uint8_t *vls;		/* matrix form assignment lid X lid -> virtual lane */
} vltable_t;

typedef struct cdg_link {
	struct cdg_node *node;
	uint32_t num_pairs;	/* number of src->dest pairs incremented in path adding step */
	uint32_t max_len;	/* length of the srcdest array */
	uint32_t removed;	/* number of pairs removed in path deletion step */
	uint32_t *srcdest_pairs;
	struct cdg_link *next;
} cdg_link_t;

/* struct for a node of a binary tree with additional parent pointer */
typedef struct cdg_node {
	uint64_t channelID;	/* unique key consist of src lid + port + dest lid + port */
	cdg_link_t *linklist;	/* edges to adjazent nodes */
	uint8_t status;		/* node status in cycle search to avoid recursive function */
	uint8_t visited;	/* needed to traverse the binary tree */
	struct cdg_node *pre;	/* to save the path in cycle detection algorithm */
	struct cdg_node *left, *right, *parent;
} cdg_node_t;

typedef struct dfsssp_context {
	osm_routing_engine_type_t routing_type;
	osm_ucast_mgr_t *p_mgr;
	vertex_t *adj_list;
	uint32_t adj_list_size;
	vltable_t *srcdest2vl_table;
	uint8_t *vl_split_count;
} dfsssp_context_t;

/**************** set initial values for structs **********************
 **********************************************************************/
static inline void set_default_link(link_t * link)
{
	link->guid = 0;
	link->from = 0;
	link->from_port = 0;
	link->to = 0;
	link->to_port = 0;
	link->weight = 0;
	link->next = NULL;
}

static inline void set_default_vertex(vertex_t * vertex)
{
	vertex->guid = 0;
	vertex->lid = 0;
	vertex->num_hca = 0;
	vertex->links = NULL;
	vertex->hops = 0;
	vertex->used_link = NULL;
	vertex->distance = 0;
	vertex->state = UNDISCOVERED;
	vertex->heap_id = 0;
	vertex->sw = NULL;
	vertex->dropped = FALSE;
}

static inline void set_default_cdg_node(cdg_node_t * node)
{
	node->channelID = 0;
	node->linklist = NULL;
	node->status = UNKNOWN;
	node->visited = 0;
	node->pre = NULL;
	node->left = NULL;
	node->right = NULL;
	node->parent = NULL;
}

/**********************************************************************
 **********************************************************************/

/************ helper functions for heap in dijkstra *******************
 **********************************************************************/
/* returns true if element 1 is smaller than element 2 */
static inline uint32_t heap_smaller(binary_heap_t * heap, uint32_t i,
				    uint32_t j)
{
	return (heap->nodes[i]->distance < heap->nodes[j]->distance) ? 1 : 0;
}

/* swap two elements */
static void heap_exchange(binary_heap_t * heap, uint32_t i, uint32_t j)
{
	uint32_t tmp_heap_id = 0;
	vertex_t *tmp_node = NULL;

	/* 1. swap the heap_id */
	tmp_heap_id = heap->nodes[i]->heap_id;
	heap->nodes[i]->heap_id = heap->nodes[j]->heap_id;
	heap->nodes[j]->heap_id = tmp_heap_id;
	/* 2. swap pointers */
	tmp_node = heap->nodes[i];
	heap->nodes[i] = heap->nodes[j];
	heap->nodes[j] = tmp_node;
}

/* changes position of element with parent until children are bigger */
static uint32_t heap_up(binary_heap_t * heap, uint32_t i)
{
	uint32_t curr = i, father = 0;

	if (curr > 0) {
		father = (curr - 1) >> 1;
		while (heap_smaller(heap, curr, father)) {
			heap_exchange(heap, curr, father);
			/* try to go up when we arent already root */
			curr = father;
			if (curr > 0)
				father = (curr - 1) >> 1;
		}
	}

	return curr;
}

/* changes position of element with children until parent is smaller */
static uint32_t heap_down(binary_heap_t * heap, uint32_t i)
{
	uint32_t curr = i;
	uint32_t son1 = 0, son2 = 0, smaller_son = 0;
	uint32_t exchanged = 0;

	do {
		son1 = ((curr + 1) << 1) - 1;
		son2 = (curr + 1) << 1;
		exchanged = 0;

		/* exchange with smaller son */
		if (son1 < heap->size && son2 < heap->size) {
			if (heap_smaller(heap, son1, son2))
				smaller_son = son1;
			else
				smaller_son = son2;
		} else if (son1 < heap->size) {
			/* only one son */
			smaller_son = son1;
		} else {
			/* finished */
			break;
		}

		/* only exchange when smaller */
		if (heap_smaller(heap, smaller_son, curr)) {
			heap_exchange(heap, curr, smaller_son);
			exchanged = 1;
			curr = smaller_son;
		}
	} while (exchanged);

	return curr;
}

/* reheapify element */
static inline void heap_heapify(binary_heap_t * heap, uint32_t i)
{
	heap_down(heap, heap_up(heap, i));
}

/* creates heap for graph */
static int heap_create(vertex_t * adj_list, uint32_t adj_list_size,
		       binary_heap_t ** binheap)
{
	binary_heap_t *heap = NULL;
	uint32_t i = 0;

	/* allocate the memory for the heap object */
	heap = (binary_heap_t *) malloc(sizeof(binary_heap_t));
	if (!heap)
		return 1;

	/* the heap size is equivalent to the size of the adj_list */
	heap->size = adj_list_size;

	/* allocate the pointer array, fill with the pointers to the elements of the adj_list and set the initial heap_id */
	heap->nodes = (vertex_t **) malloc(heap->size * sizeof(vertex_t *));
	if (!heap->nodes) {
		free(heap);
		return 1;
	}
	for (i = 0; i < heap->size; i++) {
		heap->nodes[i] = &adj_list[i];
		heap->nodes[i]->heap_id = i;
	}

	/* sort elements */
	for (i = heap->size; i > 0; i--)
		heap_down(heap, i - 1);

	*binheap = heap;
	return 0;
}

/* returns current minimum and removes it from heap */
static vertex_t *heap_getmin(binary_heap_t * heap)
{
	vertex_t *min = NULL;

	if (heap->size > 0)
		min = heap->nodes[0];

	if (min == NULL)
		return min;

	if (heap->size > 0) {
		if (heap->size > 1) {
			heap_exchange(heap, 0, heap->size - 1);
			heap->size--;
			heap_down(heap, 0);
		} else {
			heap->size--;
		}
	}

	return min;
}

/* cleanup heap */
static void heap_free(binary_heap_t * heap)
{
	if (heap) {
		if (heap->nodes) {
			free(heap->nodes);
			heap->nodes = NULL;
		}
		free(heap);
	}
}

/**********************************************************************
 **********************************************************************/

/************ helper functions to save src/dest X vl combination ******
 **********************************************************************/
/* compare function of two lids for stdlib qsort */
static int cmp_lids(const void *l1, const void *l2)
{
	ib_net16_t lid1 = *((ib_net16_t *) l1), lid2 = *((ib_net16_t *) l2);

	if (lid1 < lid2)
		return -1;
	else if (lid1 > lid2)
		return 1;
	else
		return 0;
}

/* use stdlib to sort the lid array */
static inline void vltable_sort_lids(vltable_t * vltable)
{
	qsort(vltable->lids, vltable->num_lids, sizeof(ib_net16_t), cmp_lids);
}

/* use stdlib to get index of key in lid array;
   return -1 if lid isn't found in lids array
*/
static inline int64_t vltable_get_lidindex(ib_net16_t * key, vltable_t * vltable)
{
	ib_net16_t *found_lid = NULL;

	found_lid =
	    (ib_net16_t *) bsearch(key, vltable->lids, vltable->num_lids,
				   sizeof(ib_net16_t), cmp_lids);
	if (found_lid)
		return found_lid - vltable->lids;
	else
		return -1;
}

/* get virtual lane from src lid X dest lid combination;
   return -1 for invalid lids
*/
static int32_t vltable_get_vl(vltable_t * vltable, ib_net16_t slid, ib_net16_t dlid)
{
	int64_t ind1 = vltable_get_lidindex(&slid, vltable);
	int64_t ind2 = vltable_get_lidindex(&dlid, vltable);

	if (ind1 > -1 && ind2 > -1)
		return (int32_t) (vltable->
				  vls[ind1 + ind2 * vltable->num_lids]);
	else
		return -1;
}

/* set a virtual lane in the matrix */
static inline void vltable_insert(vltable_t * vltable, ib_net16_t slid,
				  ib_net16_t dlid, uint8_t vl)
{
	int64_t ind1 = vltable_get_lidindex(&slid, vltable);
	int64_t ind2 = vltable_get_lidindex(&dlid, vltable);

	if (ind1 > -1 && ind2 > -1)
		vltable->vls[ind1 + ind2 * vltable->num_lids] = vl;
}

/* change a number of lanes from lane xy to lane yz */
static void vltable_change_vl(vltable_t * vltable, uint8_t from, uint8_t to,
			      uint64_t count)
{
	uint64_t set = 0, stop = 0;
	uint64_t ind1 = 0, ind2 = 0;

	for (ind1 = 0; ind1 < vltable->num_lids; ind1++) {
		for (ind2 = 0; ind2 < vltable->num_lids; ind2++) {
			if (set == count) {
				stop = 1;
				break;
			}
			if (ind1 != ind2) {
				if (vltable->
				    vls[ind1 + ind2 * vltable->num_lids] ==
				    from) {
					vltable->vls[ind1 +
						     ind2 * vltable->num_lids] =
					    to;
					set++;
				}
			}
		}
		if (stop)
			break;
	}
}

static void vltable_print(osm_ucast_mgr_t * p_mgr, vltable_t * vltable)
{
	uint64_t ind1 = 0, ind2 = 0;

	for (ind1 = 0; ind1 < vltable->num_lids; ind1++) {
		for (ind2 = 0; ind2 < vltable->num_lids; ind2++) {
			if (ind1 != ind2) {
				OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
					"   route from src_lid=%" PRIu16
					" to dest_lid=%" PRIu16 " on vl=%" PRIu8
					"\n", cl_ntoh16(vltable->lids[ind1]),
					cl_ntoh16(vltable->lids[ind2]),
					vltable->vls[ind1 +
						     ind2 * vltable->num_lids]);
			}
		}
	}
}

static void vltable_dealloc(vltable_t ** vltable)
{
	if (*vltable) {
		if ((*vltable)->lids)
			free((*vltable)->lids);
		if ((*vltable)->vls)
			free((*vltable)->vls);
		free(*vltable);
		*vltable = NULL;
	}
}

static int vltable_alloc(vltable_t ** vltable, uint64_t size)
{
	/* allocate VL table and indexing array */
	*vltable = (vltable_t *) malloc(sizeof(vltable_t));
	if (!(*vltable))
		goto ERROR;
	(*vltable)->num_lids = size;
	(*vltable)->lids = (ib_net16_t *) malloc(size * sizeof(ib_net16_t));
	if (!((*vltable)->lids))
		goto ERROR;
	(*vltable)->vls = (uint8_t *) malloc(size * size * sizeof(uint8_t));
	if (!((*vltable)->vls))
		goto ERROR;
	memset((*vltable)->vls, OSM_DEFAULT_SL, size * size);

	return 0;

ERROR:
	vltable_dealloc(vltable);

	return 1;
}

/**********************************************************************
 **********************************************************************/

/************ helper functions to save/manage the channel dep. graph **
 **********************************************************************/
/* update the srcdest array;
   realloc array (double the size) if size is not large enough
*/
static void set_next_srcdest_pair(cdg_link_t * link, uint32_t srcdest)
{
	uint32_t new_size = 0, start_size = 2;
	uint32_t *tmp = NULL, *tmp2 = NULL;

	if (link->num_pairs == 0) {
		link->srcdest_pairs =
		    (uint32_t *) malloc(start_size * sizeof(uint32_t));
		link->srcdest_pairs[link->num_pairs] = srcdest;
		link->max_len = start_size;
		link->removed = 0;
	} else if (link->num_pairs == link->max_len) {
		new_size = link->max_len << 1;
		tmp = (uint32_t *) malloc(new_size * sizeof(uint32_t));
		tmp =
		    memcpy(tmp, link->srcdest_pairs,
			   link->max_len * sizeof(uint32_t));
		tmp2 = link->srcdest_pairs;
		link->srcdest_pairs = tmp;
		link->srcdest_pairs[link->num_pairs] = srcdest;
		free(tmp2);
		link->max_len = new_size;
	} else {
		link->srcdest_pairs[link->num_pairs] = srcdest;
	}
	link->num_pairs++;
}

static inline uint32_t get_next_srcdest_pair(cdg_link_t * link, uint32_t index)
{
	return link->srcdest_pairs[index];
}

/* traverse binary tree to find a node */
static cdg_node_t *cdg_search(cdg_node_t * root, uint64_t channelID)
{
	while (root) {
		if (channelID < root->channelID)
			root = root->left;
		else if (channelID > root->channelID)
			root = root->right;
		else if (channelID == root->channelID)
			return root;
	}
	return NULL;
}

/* insert new node into the binary tree */
static void cdg_insert(cdg_node_t ** root, cdg_node_t * new_node)
{
	cdg_node_t *current = *root;

	if (!current) {
		current = new_node;
		*root = current;
		return;
	}

	while (current) {
		if (new_node->channelID < current->channelID) {
			if (current->left) {
				current = current->left;
			} else {
				current->left = new_node;
				new_node->parent = current;
				break;
			}
		} else if (new_node->channelID > current->channelID) {
			if (current->right) {
				current = current->right;
			} else {
				current->right = new_node;
				new_node->parent = current;
				break;
			}
		} else if (new_node->channelID == current->channelID) {
			/* not really possible, maybe programming error */
			break;
		}
	}
}

static void cdg_node_dealloc(cdg_node_t * node)
{
	cdg_link_t *link = node->linklist, *tmp = NULL;

	/* dealloc linklist */
	while (link) {
		tmp = link;
		link = link->next;

		if (tmp->num_pairs)
			free(tmp->srcdest_pairs);
		free(tmp);
	}
	/* dealloc node */
	free(node);
}

static void cdg_dealloc(cdg_node_t ** root)
{
	cdg_node_t *current = *root;

	while (current) {
		if (current->left) {
			current = current->left;
		} else if (current->right) {
			current = current->right;
		} else {
			if (current->parent == NULL) {
				cdg_node_dealloc(current);
				*root = NULL;
				break;
			}
			if (current->parent->left == current) {
				current = current->parent;
				cdg_node_dealloc(current->left);
				current->left = NULL;
			} else if (current->parent->right == current) {
				current = current->parent;
				cdg_node_dealloc(current->right);
				current->right = NULL;
			}
		}
	}
}

/* search for a edge in the cdg which should be removed to break a cycle */
static cdg_link_t *get_weakest_link_in_cycle(cdg_node_t * cycle)
{
	cdg_node_t *current = cycle, *node_with_weakest_link = NULL;
	cdg_link_t *link = NULL, *weakest_link = NULL;

	link = current->linklist;
	while (link) {
		if (link->node->status == GRAY) {
			weakest_link = link;
			node_with_weakest_link = current;
			current = link->node;
			break;
		}
		link = link->next;
	}

	while (1) {
		current->status = UNKNOWN;
		link = current->linklist;
		while (link) {
			if (link->node->status == GRAY) {
				if ((link->num_pairs - link->removed) <
				    (weakest_link->num_pairs -
				     weakest_link->removed)) {
					weakest_link = link;
					node_with_weakest_link = current;
				}
				current = link->node;
				break;
			}
			link = link->next;
		}
		/* if complete cycle is traversed */
		if (current == cycle) {
			current->status = UNKNOWN;
			break;
		}
	}

	if (node_with_weakest_link->linklist == weakest_link) {
		node_with_weakest_link->linklist = weakest_link->next;
	} else {
		link = node_with_weakest_link->linklist;
		while (link) {
			if (link->next == weakest_link) {
				link->next = weakest_link->next;
				break;
			}
			link = link->next;
		}
	}

	return weakest_link;
}

/* search for nodes in the cdg not yet reached in the cycle search process;
   (some nodes are unreachable, e.g. a node is a source or the cdg has not connected parts)
*/
static cdg_node_t *get_next_cdg_node(cdg_node_t * root)
{
	cdg_node_t *current = root, *res = NULL;

	while (current) {
		current->visited = 1;
		if (current->status == UNKNOWN) {
			res = current;
			break;
		}
		if (current->left && !current->left->visited) {
			current = current->left;
		} else if (current->right && !current->right->visited) {
			current = current->right;
		} else {
			if (current->left)
				current->left->visited = 0;
			if (current->right)
				current->right->visited = 0;
			if (current->parent == NULL)
				break;
			else
				current = current->parent;
		}
	}

	/* Clean up */
	while (current) {
		current->visited = 0;
		if (current->left)
			current->left->visited = 0;
		if (current->right)
			current->right->visited = 0;
		current = current->parent;
	}

	return res;
}

/* make a DFS on the cdg to check for a cycle */
static cdg_node_t *search_cycle_in_channel_dep_graph(cdg_node_t * cdg,
						     cdg_node_t * start_node)
{
	cdg_node_t *cycle = NULL;
	cdg_node_t *current = start_node, *next_node = NULL, *tmp = NULL;
	cdg_link_t *link = NULL;

	while (current) {
		current->status = GRAY;
		link = current->linklist;
		next_node = NULL;
		while (link) {
			if (link->node->status == UNKNOWN) {
				next_node = link->node;
				break;
			}
			if (link->node->status == GRAY) {
				cycle = link->node;
				goto Exit;
			}
			link = link->next;
		}
		if (next_node) {
			next_node->pre = current;
			current = next_node;
		} else {
			/* found a sink in the graph, go to last node */
			current->status = BLACK;

			/* srcdest_pairs of this node aren't relevant, free the allocated memory */
			link = current->linklist;
			while (link) {
				if (link->num_pairs)
					free(link->srcdest_pairs);
				link->srcdest_pairs = NULL;
				link->num_pairs = 0;
				link->removed = 0;
				link = link->next;
			}

			if (current->pre) {
				tmp = current;
				current = current->pre;
				tmp->pre = NULL;
			} else {
				/* search for other subgraphs in cdg */
				current = get_next_cdg_node(cdg);
				if (!current)
					break;	/* all relevant nodes traversed, no more cycles found */
			}
		}
	}

Exit:
	return cycle;
}

/* calculate the path from source to destination port;
   new channels are added directly to the cdg
*/
static int update_channel_dep_graph(cdg_node_t ** cdg_root,
				    osm_port_t * src_port, uint16_t slid,
				    osm_port_t * dest_port, uint16_t dlid)
{
	osm_node_t *local_node = NULL, *remote_node = NULL;
	uint16_t local_lid = 0, remote_lid = 0;
	uint32_t srcdest = 0;
	uint8_t local_port = 0, remote_port = 0;
	uint64_t channelID = 0;

	cdg_node_t *channel_head = NULL, *channel = NULL, *last_channel = NULL;
	cdg_link_t *linklist = NULL;

	/* set the identifier for the src/dest pair to save this on each edge of the cdg */
	srcdest = (((uint32_t) slid) << 16) + ((uint32_t) dlid);

	channel_head = (cdg_node_t *) malloc(sizeof(cdg_node_t));
	if (!channel_head)
		goto ERROR;
	set_default_cdg_node(channel_head);
	last_channel = channel_head;

	/* if src is a Hca, then the channel from Hca to switch would be a source in the graph
	   sources can't be part of a cycle -> skip this channel
	 */
	remote_node =
	    osm_node_get_remote_node(src_port->p_node,
				     src_port->p_physp->port_num, &remote_port);

	while (remote_node && remote_node->sw) {
		local_node = remote_node;
		local_port = local_node->sw->new_lft[dlid];
		/* sanity check: local_port must be set or routing is broken */
		if (local_port == OSM_NO_PATH)
			goto ERROR;
		local_lid = cl_ntoh16(osm_node_get_base_lid(local_node, 0));
		/* each port belonging to a switch has lmc==0 -> get_base_lid is fine
		   (local/remote port in this function are always part of a switch)
		 */

		remote_node =
		    osm_node_get_remote_node(local_node, local_port,
					     &remote_port);
		/* if remote_node is a Hca, then the last channel from switch to Hca would be a sink in the cdg -> skip */
		if (!remote_node || !remote_node->sw)
			break;
		remote_lid = cl_ntoh16(osm_node_get_base_lid(remote_node, 0));

		channelID =
		    (((uint64_t) local_lid) << 48) +
		    (((uint64_t) local_port) << 32) +
		    (((uint64_t) remote_lid) << 16) + ((uint64_t) remote_port);
		channel = cdg_search(*cdg_root, channelID);
		if (channel) {
			/* check whether last channel has connection to this channel, i.e. subpath already exists in cdg */
			linklist = last_channel->linklist;
			while (linklist && linklist->node != channel
			       && linklist->next)
				linklist = linklist->next;
			/* if there is no connection, add one */
			if (linklist) {
				if (linklist->node == channel) {
					set_next_srcdest_pair(linklist,
							      srcdest);
				} else {
					linklist->next =
					    (cdg_link_t *)
					    malloc(sizeof(cdg_link_t));
					if (!linklist->next)
						goto ERROR;
					linklist = linklist->next;
					linklist->node = channel;
					linklist->num_pairs = 0;
					linklist->srcdest_pairs = NULL;
					set_next_srcdest_pair(linklist,
							      srcdest);
					linklist->next = NULL;
				}
			} else {
				/* either this is the first channel of the path, or the last channel was a new channel, or last channel was a sink */
				last_channel->linklist =
				    (cdg_link_t *) malloc(sizeof(cdg_link_t));
				if (!last_channel->linklist)
					goto ERROR;
				last_channel->linklist->node = channel;
				last_channel->linklist->num_pairs = 0;
				last_channel->linklist->srcdest_pairs = NULL;
				set_next_srcdest_pair(last_channel->linklist,
						      srcdest);
				last_channel->linklist->next = NULL;
			}
		} else {
			/* create new channel */
			channel = (cdg_node_t *) malloc(sizeof(cdg_node_t));
			if (!channel)
				goto ERROR;
			set_default_cdg_node(channel);
			channel->channelID = channelID;
			cdg_insert(cdg_root, channel);

			/* go to end of link list of last channel */
			linklist = last_channel->linklist;
			while (linklist && linklist->next)
				linklist = linklist->next;
			if (linklist) {
				/* update last link of an existing channel */
				linklist->next =
				    (cdg_link_t *) malloc(sizeof(cdg_link_t));
				if (!linklist->next)
					goto ERROR;
				linklist = linklist->next;
				linklist->node = channel;
				linklist->num_pairs = 0;
				linklist->srcdest_pairs = NULL;
				set_next_srcdest_pair(linklist, srcdest);
				linklist->next = NULL;
			} else {
				/* either this is the first channel of the path, or the last channel was a new channel, or last channel was a sink */
				last_channel->linklist =
				    (cdg_link_t *) malloc(sizeof(cdg_link_t));
				if (!last_channel->linklist)
					goto ERROR;
				last_channel->linklist->node = channel;
				last_channel->linklist->num_pairs = 0;
				last_channel->linklist->srcdest_pairs = NULL;
				set_next_srcdest_pair(last_channel->linklist,
						      srcdest);
				last_channel->linklist->next = NULL;
			}
		}
		last_channel = channel;
	}

	if (channel_head->linklist) {
		if (channel_head->linklist->srcdest_pairs)
			free(channel_head->linklist->srcdest_pairs);
		free(channel_head->linklist);
	}
	free(channel_head);

	return 0;

ERROR:
	/* cleanup data and exit */
	if (channel_head) {
		if (channel_head->linklist)
			free(channel_head->linklist);
		free(channel_head);
	}

	return 1;
}

/* calculate the path from source to destination port;
   the links in the cdg representing this path are decremented to simulate the removal
*/
static int remove_path_from_cdg(cdg_node_t ** cdg_root, osm_port_t * src_port,
				uint16_t slid, osm_port_t * dest_port,
				uint16_t dlid)
{
	osm_node_t *local_node = NULL, *remote_node = NULL;
	uint16_t local_lid = 0, remote_lid = 0;
	uint8_t local_port = 0, remote_port = 0;
	uint64_t channelID = 0;

	cdg_node_t *channel_head = NULL, *channel = NULL, *last_channel = NULL;
	cdg_link_t *linklist = NULL;

	channel_head = (cdg_node_t *) malloc(sizeof(cdg_node_t));
	if (!channel_head)
		goto ERROR;
	set_default_cdg_node(channel_head);
	last_channel = channel_head;

	/* if src is a Hca, then the channel from Hca to switch would be a source in the graph
	   sources can't be part of a cycle -> skip this channel
	 */
	remote_node =
	    osm_node_get_remote_node(src_port->p_node,
				     src_port->p_physp->port_num, &remote_port);

	while (remote_node && remote_node->sw) {
		local_node = remote_node;
		local_port = local_node->sw->new_lft[dlid];
		/* sanity check: local_port must be set or routing is broken */
		if (local_port == OSM_NO_PATH)
			goto ERROR;
		local_lid = cl_ntoh16(osm_node_get_base_lid(local_node, 0));

		remote_node =
		    osm_node_get_remote_node(local_node, local_port,
					     &remote_port);
		/* if remote_node is a Hca, then the last channel from switch to Hca would be a sink in the cdg -> skip */
		if (!remote_node || !remote_node->sw)
			break;
		remote_lid = cl_ntoh16(osm_node_get_base_lid(remote_node, 0));

		channelID =
		    (((uint64_t) local_lid) << 48) +
		    (((uint64_t) local_port) << 32) +
		    (((uint64_t) remote_lid) << 16) + ((uint64_t) remote_port);
		channel = cdg_search(*cdg_root, channelID);
		if (channel) {
			/* check whether last channel has connection to this channel, i.e. subpath already exists in cdg */
			linklist = last_channel->linklist;
			while (linklist && linklist->node != channel
			       && linklist->next)
				linklist = linklist->next;
			/* remove the srcdest from the link */
			if (linklist) {
				if (linklist->node == channel) {
					linklist->removed++;
				} else {
					/* may happen if the link is missing (thru cycle detect algorithm) */
				}
			} else {
				/* may happen if the link is missing (thru cycle detect algorithm or last_channel==channel_head (dummy channel)) */
			}
		} else {
			/* must be an error, channels for the path are added before, so a missing channel would be a corrupt data structure */
			goto ERROR;
		}
		last_channel = channel;
	}

	if (channel_head->linklist)
		free(channel_head->linklist);
	free(channel_head);

	return 0;

ERROR:
	/* cleanup data and exit */
	if (channel_head) {
		if (channel_head->linklist)
			free(channel_head->linklist);
		free(channel_head);
	}

	return 1;
}

/**********************************************************************
 **********************************************************************/

/************ helper functions to generate an ordered list of ports ***
 ************ (functions copied from osm_ucast_mgr.c and modified) ****
 **********************************************************************/
static void add_sw_endports_to_order_list(osm_switch_t * sw,
					  osm_ucast_mgr_t * m,
					  cl_qmap_t * guid_tbl,
					  boolean_t add_guids)
{
	osm_port_t *port;
	ib_net64_t port_guid;
	uint64_t sw_guid;
	osm_physp_t *p;
	int i;
	boolean_t found;

	for (i = 1; i < sw->num_ports; i++) {
		p = osm_node_get_physp_ptr(sw->p_node, i);
		if (p && p->p_remote_physp && !p->p_remote_physp->p_node->sw) {
			port_guid = p->p_remote_physp->port_guid;
			/* check if link is healthy, otherwise ignore CA */
			if (!osm_link_is_healthy(p)) {
				sw_guid =
				    cl_ntoh64(osm_node_get_node_guid
					      (sw->p_node));
				OSM_LOG(m->p_log, OSM_LOG_INFO,
					"WRN AD40: ignoring CA due to unhealthy"
					" link from switch 0x%016" PRIx64
					" port %" PRIu8 " to CA 0x%016" PRIx64
					"\n", sw_guid, i, cl_ntoh64(port_guid));
			}
			port = osm_get_port_by_guid(m->p_subn, port_guid);
			if (!port)
				continue;
			if (!cl_is_qmap_empty(guid_tbl)) {
				found = (cl_qmap_get(guid_tbl, port_guid)
					 != cl_qmap_end(guid_tbl));
				if ((add_guids && !found)
				    || (!add_guids && found))
					continue;
			}
			if (!cl_is_item_in_qlist(&m->port_order_list,
						 &port->list_item))
				cl_qlist_insert_tail(&m->port_order_list,
						     &port->list_item);
			else
				OSM_LOG(m->p_log, OSM_LOG_INFO,
					"WRN AD37: guid 0x%016" PRIx64
					" already in list\n", port_guid);
		}
	}
}

static void add_guid_to_order_list(uint64_t guid, osm_ucast_mgr_t * m)
{
	osm_port_t *port = osm_get_port_by_guid(m->p_subn, cl_hton64(guid));

	if (!port) {
		 OSM_LOG(m->p_log, OSM_LOG_DEBUG,
			 "port guid not found: 0x%016" PRIx64 "\n", guid);
	}

	if (!cl_is_item_in_qlist(&m->port_order_list, &port->list_item))
		cl_qlist_insert_tail(&m->port_order_list, &port->list_item);
	else
		OSM_LOG(m->p_log, OSM_LOG_INFO,
			"WRN AD38: guid 0x%016" PRIx64 " already in list\n",
			guid);
}

/* compare function of #Hca attached to a switch for stdlib qsort */
static int cmp_num_hca(const void * l1, const void * l2)
{
	vertex_t *sw1 = *((vertex_t **) l1);
	vertex_t *sw2 = *((vertex_t **) l2);
	uint32_t num_hca1 = 0, num_hca2 = 0;

	if (sw1)
		num_hca1 = sw1->num_hca;
	if (sw2)
		num_hca2 = sw2->num_hca;

	if (num_hca1 > num_hca2)
		return -1;
	else if (num_hca1 < num_hca2)
		return 1;
	else
		return 0;
}

/* use stdlib to sort the switch array depending on num_hca */
static inline void sw_list_sort_by_num_hca(vertex_t ** sw_list,
					   uint32_t sw_list_size)
{
	qsort(sw_list, sw_list_size, sizeof(vertex_t *), cmp_num_hca);
}

/**********************************************************************
 **********************************************************************/

/************ helper functions to manage a map of CN and I/O guids ****
 **********************************************************************/
static int add_guid_to_map(void * cxt, uint64_t guid, char * p)
{
	cl_qmap_t *map = cxt;
	name_map_item_t *item;
	name_map_item_t *inserted_item;

	item = malloc(sizeof(*item));
	if (!item)
		return -1;

	item->guid = cl_hton64(guid);	/* internal: network byte order */
	item->name = NULL;		/* name isn't needed */
	inserted_item = (name_map_item_t *) cl_qmap_insert(map, item->guid, &item->item);
	if (inserted_item != item)
                free(item);

	return 0;
}

static void destroy_guid_map(cl_qmap_t * guid_tbl)
{
	name_map_item_t *p_guid = NULL, *p_next_guid = NULL;

	p_next_guid = (name_map_item_t *) cl_qmap_head(guid_tbl);
	while (p_next_guid != (name_map_item_t *) cl_qmap_end(guid_tbl)) {
		p_guid = p_next_guid;
		p_next_guid = (name_map_item_t *) cl_qmap_next(&p_guid->item);
		free(p_guid);
	}
	cl_qmap_remove_all(guid_tbl);
}

/**********************************************************************
 **********************************************************************/

static void dfsssp_print_graph(osm_ucast_mgr_t * p_mgr, vertex_t * adj_list,
			       uint32_t size)
{
	uint32_t i = 0, c = 0;
	link_t *link = NULL;

	/* index 0 is for the source in dijkstra -> ignore */
	for (i = 1; i < size; i++) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG, "adj_list[%" PRIu32 "]:\n",
			i);
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"   guid = 0x%" PRIx64 " lid = %" PRIu16 " (%s)\n",
			adj_list[i].guid, adj_list[i].lid,
			adj_list[i].sw->p_node->print_desc);
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"   num_hca = %" PRIu32 "\n", adj_list[i].num_hca);

		c = 1;
		for (link = adj_list[i].links; link != NULL;
		     link = link->next, c++) {
			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"   link[%" PRIu32 "]:\n", c);
			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"      to guid = 0x%" PRIx64 " (%s) port %"
				PRIu8 "\n", link->guid,
				adj_list[link->to].sw->p_node->print_desc,
				link->to_port);
			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"      weight on this link = %" PRIu64 "\n",
				link->weight);
		}
	}
}

/* predefine, to use this in next function */
static void dfsssp_context_destroy(void *context);
static int dijkstra(osm_ucast_mgr_t * p_mgr, vertex_t * adj_list,
		    uint32_t adj_list_size, osm_port_t * port, uint16_t lid);

/* traverse subnet to gather information about the connected switches */
static int dfsssp_build_graph(void *context)
{
	dfsssp_context_t *dfsssp_ctx = (dfsssp_context_t *) context;
	osm_ucast_mgr_t *p_mgr = (osm_ucast_mgr_t *) (dfsssp_ctx->p_mgr);

	cl_qmap_t *port_tbl = &p_mgr->p_subn->port_guid_tbl;	/* 1 management port per switch + 1 or 2 ports for each Hca */
	osm_port_t *p_port = NULL;
	cl_qmap_t *sw_tbl = &p_mgr->p_subn->sw_guid_tbl;
	cl_map_item_t *item = NULL;
	osm_switch_t *sw = NULL;
	osm_node_t *remote_node = NULL;
	uint8_t port = 0, remote_port = 0;
	uint32_t i = 0, j = 0, err = 0, undiscov = 0, max_num_undiscov = 0;
	uint64_t total_num_hca = 0;
	vertex_t *adj_list = NULL;
	osm_physp_t *p_physp = NULL;
	link_t *link = NULL, *head = NULL;
	uint32_t num_sw = 0, adj_list_size = 0;
	uint8_t lmc = 0;
	uint16_t sm_lid = 0;

	OSM_LOG_ENTER(p_mgr->p_log);
	OSM_LOG(p_mgr->p_log, OSM_LOG_VERBOSE,
		"Building graph for df-/sssp routing\n");

	/* if this pointer isn't NULL, this is a reroute step;
	   old context will be destroyed (adj_list and srcdest2vl_table)
	 */
	if (dfsssp_ctx->adj_list)
		dfsssp_context_destroy(context);

	num_sw = cl_qmap_count(sw_tbl);
	adj_list_size = num_sw + 1;
	/* allocate an adjazenz list (array), 0. element is reserved for the source (Hca) in the routing algo, others are switches */
	adj_list = (vertex_t *) malloc(adj_list_size * sizeof(vertex_t));
	if (!adj_list) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
			"ERR AD02: cannot allocate memory for adj_list\n");
		goto ERROR;
	}
	for (i = 0; i < adj_list_size; i++)
		set_default_vertex(&adj_list[i]);

	dfsssp_ctx->adj_list = adj_list;
	dfsssp_ctx->adj_list_size = adj_list_size;

	/* count the total number of Hca / LIDs (for lmc>0) in the fabric;
	   even include base/enhanced switch port 0; base SP0 will have lmc=0
	 */
	for (item = cl_qmap_head(port_tbl); item != cl_qmap_end(port_tbl);
	     item = cl_qmap_next(item)) {
		p_port = (osm_port_t *) item;
		if (osm_node_get_type(p_port->p_node) == IB_NODE_TYPE_CA ||
		    osm_node_get_type(p_port->p_node) == IB_NODE_TYPE_SWITCH) {
			lmc = osm_port_get_lmc(p_port);
			total_num_hca += (1 << lmc);
		}
	}

	i = 1;			/* fill adj_list -> start with index 1 */
	for (item = cl_qmap_head(sw_tbl); item != cl_qmap_end(sw_tbl);
	     item = cl_qmap_next(item), i++) {
		sw = (osm_switch_t *) item;
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"Processing switch with GUID 0x%" PRIx64 "\n",
			cl_ntoh64(osm_node_get_node_guid(sw->p_node)));

		adj_list[i].guid =
		    cl_ntoh64(osm_node_get_node_guid(sw->p_node));
		adj_list[i].lid =
		    cl_ntoh16(osm_node_get_base_lid(sw->p_node, 0));
		adj_list[i].sw = sw;

		link = (link_t *) malloc(sizeof(link_t));
		if (!link) {
			OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
				"ERR AD03: cannot allocate memory for a link\n");
			goto ERROR;
		}
		head = link;
		head->next = NULL;

		/* add SP0 to number of CA connected to a switch */
		lmc = osm_node_get_lmc(sw->p_node, 0);
		adj_list[i].num_hca += (1 << lmc);

		/* iterate over all ports in the switch, start with port 1 (port 0 is a management port) */
		for (port = 1; port < sw->num_ports; port++) {
			/* get the node behind the port */
			remote_node =
			    osm_node_get_remote_node(sw->p_node, port,
						     &remote_port);
			/* if there is no remote node on this port or it's the same switch -> try next port */
			if (!remote_node || remote_node->sw == sw)
				continue;
			/* make sure the link is healthy */
			p_physp = osm_node_get_physp_ptr(sw->p_node, port);
			if (!p_physp || !osm_link_is_healthy(p_physp))
				continue;
			/* if there is a Hca connected -> count and cycle */
			if (!remote_node->sw) {
				lmc = osm_node_get_lmc(remote_node, (uint32_t)remote_port);
				adj_list[i].num_hca += (1 << lmc);
				continue;
			}
			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"Node 0x%" PRIx64 ", remote node 0x%" PRIx64
				", port %" PRIu8 ", remote port %" PRIu8 "\n",
				cl_ntoh64(osm_node_get_node_guid(sw->p_node)),
				cl_ntoh64(osm_node_get_node_guid(remote_node)),
				port, remote_port);

			link->next = (link_t *) malloc(sizeof(link_t));
			if (!link->next) {
				OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
					"ERR AD08: cannot allocate memory for a link\n");
				while (head) {
					link = head;
					head = head->next;
					free(link);
				}
				goto ERROR;
			}
			link = link->next;
			set_default_link(link);
			link->guid =
			    cl_ntoh64(osm_node_get_node_guid(remote_node));
			link->from = i;
			link->from_port = port;
			link->to_port = remote_port;
			link->weight = total_num_hca * total_num_hca;	/* initialize with P^2 to force shortest paths */
		}

		adj_list[i].links = head->next;
		free(head);
	}
	/* connect the links with it's second adjacent node in the list */
	for (i = 1; i < adj_list_size; i++) {
		link = adj_list[i].links;
		while (link) {
			for (j = 1; j < adj_list_size; j++) {
				if (link->guid == adj_list[j].guid) {
					link->to = j;
					break;
				}
			}
			link = link->next;
		}
	}
	/* do one dry run to determine connectivity issues */
	sm_lid = p_mgr->p_subn->master_sm_base_lid;
	p_port = osm_get_port_by_lid(p_mgr->p_subn, sm_lid);
	err = dijkstra(p_mgr, adj_list, adj_list_size, p_port, sm_lid);
	if (err) {
		goto ERROR;
	} else {
		/* if sm is running on a switch, then dijkstra doesn't
		   initialize the used_link for this switch
		 */
		if (osm_node_get_type(p_port->p_node) != IB_NODE_TYPE_CA)
			max_num_undiscov = 1;
		for (i = 1; i < adj_list_size; i++)
			undiscov += (adj_list[i].used_link) ? 0 : 1;
		if (max_num_undiscov < undiscov) {
			OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
				"ERR AD0C: unsupported network state (detached"
				" and inaccessible switches found; gracefully"
				" shutdown this routing engine)\n");
			goto ERROR;
		}
	}
	/* print the discovered graph */
	if (OSM_LOG_IS_ACTIVE_V2(p_mgr->p_log, OSM_LOG_DEBUG))
		dfsssp_print_graph(p_mgr, adj_list, adj_list_size);

	OSM_LOG_EXIT(p_mgr->p_log);
	return 0;

ERROR:
	dfsssp_context_destroy(context);
	return -1;
}

static void print_routes(osm_ucast_mgr_t * p_mgr, vertex_t * adj_list,
			 uint32_t adj_list_size, osm_port_t * port)
{
	uint32_t i = 0, j = 0;

	for (i = 1; i < adj_list_size; i++) {
		if (adj_list[i].state == DISCOVERED) {
			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"Route from 0x%" PRIx64 " (%s) to 0x%" PRIx64
				" (%s):\n", adj_list[i].guid,
				adj_list[i].sw->p_node->print_desc,
				cl_ntoh64(osm_node_get_node_guid(port->p_node)),
				port->p_node->print_desc);
			j = i;
			while (adj_list[j].used_link) {
				if (j > 0) {
					OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
						"   0x%" PRIx64
						" (%s) routes thru port %" PRIu8
						"\n", adj_list[j].guid,
						adj_list[j].sw->p_node->
						print_desc,
						adj_list[j].used_link->to_port);
				} else {
					OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
						"   0x%" PRIx64
						" (%s) routes thru port %" PRIu8
						"\n", adj_list[j].guid,
						port->p_node->print_desc,
						adj_list[j].used_link->to_port);
				}
				j = adj_list[j].used_link->from;
			}
		}
	}
}

/* dijkstra step from one source to all switches in the df-/sssp graph */
static int dijkstra(osm_ucast_mgr_t * p_mgr, vertex_t * adj_list,
		    uint32_t adj_list_size, osm_port_t * port, uint16_t lid)
{
	uint32_t i = 0, j = 0, index = 0;
	osm_node_t *remote_node = NULL;
	uint8_t remote_port = 0;
	vertex_t *current = NULL;
	link_t *link = NULL;
	uint64_t guid = 0;
	binary_heap_t *heap = NULL;
	int err = 0;

	OSM_LOG_ENTER(p_mgr->p_log);

	/* reset all switches for new round with a new source for dijkstra */
	for (i = 1; i < adj_list_size; i++) {
		adj_list[i].hops = 0;
		adj_list[i].used_link = NULL;
		adj_list[i].distance = INF;
		adj_list[i].state = UNDISCOVERED;
	}

	/* if behind port is a Hca -> set adj_list[0] */
	if (osm_node_get_type(port->p_node) == IB_NODE_TYPE_CA) {
		/* save old link to prevent many mallocs after set_default_... */
		link = adj_list[0].links;
		/* initialize adj_list[0] (the source for the routing, a Hca) */
		set_default_vertex(&adj_list[0]);
		adj_list[0].guid =
		    cl_ntoh64(osm_node_get_node_guid(port->p_node));
		adj_list[0].lid = lid;
		index = 0;
		/* write saved link back to new adj_list[0] */
		adj_list[0].links = link;

		/* initialize link to neighbor for adj_list[0];
		   make sure the link is healthy
		 */
		if (port->p_physp && osm_link_is_healthy(port->p_physp)) {
			remote_node =
			    osm_node_get_remote_node(port->p_node,
						     port->p_physp->port_num,
						     &remote_port);
			/* if there is no remote node on this port or it's the same Hca -> ignore */
			if (remote_node
			    && (osm_node_get_type(remote_node) ==
				IB_NODE_TYPE_SWITCH)) {
				if (!(adj_list[0].links)) {
					adj_list[0].links =
					    (link_t *) malloc(sizeof(link_t));
					if (!(adj_list[0].links)) {
						OSM_LOG(p_mgr->p_log,
							OSM_LOG_ERROR,
							"ERR AD07: cannot allocate memory for a link\n");
						return 1;
					}
				}
				set_default_link(adj_list[0].links);
				adj_list[0].links->guid =
				    cl_ntoh64(osm_node_get_node_guid
					      (remote_node));
				adj_list[0].links->from_port =
				    port->p_physp->port_num;
				adj_list[0].links->to_port = remote_port;
				adj_list[0].links->weight = 1;
				for (j = 1; j < adj_list_size; j++) {
					if (adj_list[0].links->guid ==
					    adj_list[j].guid) {
						adj_list[0].links->to = j;
						break;
					}
				}
			}
		} else {
			/* if link is unhealthy then there's a severe issue */
			OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
				"ERR AD0B: unsupported network state (CA with"
				" unhealthy link state discovered; should have"
				" been filtered out before already; gracefully"
				" shutdown this routing engine)\n");
			return 1;
		}
		/* if behind port is a switch -> search switch in adj_list */
	} else {
		/* reset adj_list[0], if links=NULL reset was done before, then skip */
		if (adj_list[0].links) {
			free(adj_list[0].links);
			set_default_vertex(&adj_list[0]);
		}
		/* search for the switch which is the source in this round */
		guid = cl_ntoh64(osm_node_get_node_guid(port->p_node));
		for (i = 1; i < adj_list_size; i++) {
			if (guid == adj_list[i].guid) {
				index = i;
				break;
			}
		}
	}

	/* source in dijkstra */
	adj_list[index].distance = 0;
	adj_list[index].state = DISCOVERED;
	adj_list[index].hops = 0;	/* the source has hop count = 0 */

	/* create a heap to find (efficient) the node with the smallest distance */
	if (osm_node_get_type(port->p_node) == IB_NODE_TYPE_CA)
		err = heap_create(adj_list, adj_list_size, &heap);
	else
		err = heap_create(&adj_list[1], adj_list_size - 1, &heap);
	if (err) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
			"ERR AD09: cannot allocate memory for heap or heap->node in heap_create(...)\n");
		return err;
	}

	current = heap_getmin(heap);
	while (current) {
		current->state = DISCOVERED;
		if (current->used_link)	/* increment the number of hops to the source for each new node */
			current->hops =
			    adj_list[current->used_link->from].hops + 1;

		/* add/update nodes which aren't discovered but accessible */
		for (link = current->links; link != NULL; link = link->next) {
			if ((adj_list[link->to].state != DISCOVERED)
			    && (current->distance + link->weight <
				adj_list[link->to].distance)) {
				adj_list[link->to].used_link = link;
				adj_list[link->to].distance =
				    current->distance + link->weight;
				heap_heapify(heap, adj_list[link->to].heap_id);
			}
		}

		current = heap_getmin(heap);
	}

	/* destroy the heap */
	heap_free(heap);
	heap = NULL;

	OSM_LOG_EXIT(p_mgr->p_log);
	return 0;
}

/* update the linear forwarding tables of all switches with the informations
   from the last dijsktra step
*/
static int update_lft(osm_ucast_mgr_t * p_mgr, vertex_t * adj_list,
		      uint32_t adj_list_size, osm_port_t * p_port, uint16_t lid)
{
	uint32_t i = 0;
	uint8_t port = 0;
	uint8_t hops = 0;
	osm_switch_t *p_sw = NULL;
	boolean_t is_ignored_by_port_prof = FALSE;
	osm_physp_t *p = NULL;
	cl_status_t ret;

	OSM_LOG_ENTER(p_mgr->p_log);

	for (i = 1; i < adj_list_size; i++) {
		/* if no route goes thru this switch -> cycle */
		if (!(adj_list[i].used_link))
			continue;

		p_sw = adj_list[i].sw;
		hops = adj_list[i].hops;
		port = adj_list[i].used_link->to_port;
		/* the used_link is the link that was used in dijkstra to reach this node,
		   so the to_port is the local port on this node
		 */

		if (port == OSM_NO_PATH) {	/* if clause shouldn't be possible in this routing, but who cares */
			OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
				"ERR AD06: No path to get to LID %" PRIu16
				" from switch 0x%" PRIx64 "\n", lid,
				cl_ntoh64(osm_node_get_node_guid
					  (p_sw->p_node)));

			/* do not try to overwrite the ppro of non existing port ... */
			is_ignored_by_port_prof = TRUE;
			return 1;
		} else {
			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"Routing LID %" PRIu16 " to port %" PRIu8
				" for switch 0x%" PRIx64 "\n", lid, port,
				cl_ntoh64(osm_node_get_node_guid
					  (p_sw->p_node)));

			p = osm_node_get_physp_ptr(p_sw->p_node, port);
			if (!p) {
				OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
					"ERR AD0A: Physical port %d of Node GUID 0x%"
					PRIx64 "not found\n", port,
					cl_ntoh64(osm_node_get_node_guid(p_sw->p_node)));
				return 1;
			}

			/* we would like to optionally ignore this port in equalization
			   as in the case of the Mellanox Anafa Internal PCI TCA port
			 */
			is_ignored_by_port_prof = p->is_prof_ignored;

			/* We also would ignore this route if the target lid is of
			   a switch and the port_profile_switch_node is not TRUE
			 */
			if (!p_mgr->p_subn->opt.port_profile_switch_nodes)
				is_ignored_by_port_prof |=
				    (osm_node_get_type(p_port->p_node) ==
				     IB_NODE_TYPE_SWITCH);
		}

		/* to support lmc > 0 the functions alloc_ports_priv, free_ports_priv, find_and_add_remote_sys
		   from minhop aren't needed cause osm_switch_recommend_path is implicitly calculated
		   for each LID pair thru dijkstra;
		   for each port the dijkstra algorithm calculates (max_lid_ho - min_lid_ho)-times maybe
		   disjoint routes to spread the bandwidth -> diffent routes for one port and lmc>0
		 */

		/* set port in LFT */
		p_sw->new_lft[lid] = port;
		if (!is_ignored_by_port_prof) {
			/* update the number of path routing thru this port */
			osm_switch_count_path(p_sw, port);
		}
		/* set the hop count from this switch to the lid */
		ret = osm_switch_set_hops(p_sw, lid, port, hops);
		if (ret != CL_SUCCESS)
			OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
				"ERR AD05: cannot set hops for LID %" PRIu16
				" at switch 0x%" PRIx64 "\n", lid,
				cl_ntoh64(osm_node_get_node_guid
					  (p_sw->p_node)));
	}

	OSM_LOG_EXIT(p_mgr->p_log);
	return 0;
}

/* the function updates the multicast group membership information
   similar to create_mgrp_switch_map (osm_mcast_mgr.c)
   => with it we can identify if a switch needs to be processed
   or not in update_mcft
*/
static void update_mgrp_membership(cl_qlist_t * port_list)
{
	osm_mcast_work_obj_t *wobj = NULL;
	osm_port_t *port = NULL;
	osm_switch_t *sw = NULL;
	cl_list_item_t *i = NULL;

	for (i = cl_qlist_head(port_list); i != cl_qlist_end(port_list);
	     i = cl_qlist_next(i)) {
		wobj = cl_item_obj(i, wobj, list_item);
		port = wobj->p_port;
		if (port->p_node->sw) {
			sw = port->p_node->sw;
			sw->is_mc_member = 1;
		} else {
			sw = port->p_physp->p_remote_physp->p_node->sw;
			sw->num_of_mcm++;
		}
	}
}

/* reset is_mc_member and num_of_mcm for future computations */
static void reset_mgrp_membership(vertex_t * adj_list, uint32_t adj_list_size)
{
	uint32_t i = 0;

	for (i = 1; i < adj_list_size; i++) {
		if (adj_list[i].dropped)
			continue;

		adj_list[i].sw->is_mc_member = 0;
		adj_list[i].sw->num_of_mcm = 0;
	}
}

/* update the multicast forwarding tables of all switches with the informations
   from the previous dijsktra step for the current mlid
*/
static int update_mcft(osm_sm_t * p_sm, vertex_t * adj_list,
		       uint32_t adj_list_size, uint16_t mlid_ho,
		       cl_qmap_t * port_map, osm_switch_t * root_sw)
{
	uint32_t i = 0;
	uint8_t port = 0, remote_port = 0;
	uint8_t upstream_port = 0, downstream_port = 0;
	ib_net64_t guid = 0;
	osm_switch_t *p_sw = NULL;
	osm_node_t *remote_node = NULL;
	osm_physp_t *p_physp = NULL;
	osm_mcast_tbl_t *p_tbl = NULL;
	vertex_t *curr_adj = NULL;

	OSM_LOG_ENTER(p_sm->p_log);

	for (i = 1; i < adj_list_size; i++) {
		if (adj_list[i].dropped)
			continue;

		p_sw = adj_list[i].sw;
		OSM_LOG(p_sm->p_log, OSM_LOG_VERBOSE,
			"Processing switch 0x%016" PRIx64
			" (%s) for MLID 0x%X\n", cl_ntoh64(adj_list[i].guid),
			p_sw->p_node->print_desc, mlid_ho);

		/* if a) the switch does not support mcast  or
		      b) no ports of this switch are part or the mcast group
		   then cycle
		 */
		if (osm_switch_supports_mcast(p_sw) == FALSE ||
		    (p_sw->num_of_mcm == 0 && !(p_sw->is_mc_member)))
			continue;

		p_tbl = osm_switch_get_mcast_tbl_ptr(p_sw);

		/* add all ports of this sw to the mcast table,
		   if they are part of the mcast grp
		 */
		if (p_sw->is_mc_member)
			osm_mcast_tbl_set(p_tbl, mlid_ho, 0);
		for (port = 1; port < p_sw->num_ports; port++) {
			/* get the node behind the port */
			remote_node =
				osm_node_get_remote_node(p_sw->p_node, port,
							 &remote_port);
			/* check if connected and its not the same switch */
			if (!remote_node || remote_node->sw == p_sw)
				continue;
			/* make sure the link is healthy */
			p_physp = osm_node_get_physp_ptr(p_sw->p_node, port);
			if (!p_physp || !osm_link_is_healthy(p_physp))
				continue;
			/* we don't add upstream ports in this step */
			if (osm_node_get_type(remote_node) != IB_NODE_TYPE_CA)
				continue;

			guid = osm_physp_get_port_guid(osm_node_get_physp_ptr(
						       remote_node,
						       remote_port));
			if (cl_qmap_get(port_map, guid)
			    != cl_qmap_end(port_map))
				osm_mcast_tbl_set(p_tbl, mlid_ho, port);
		}

		/* now we have to add the upstream port of 'this' switch and
		   the downstream port of the next switch to the mcast table
		   until we reach the root_sw
		 */
		curr_adj = &adj_list[i];
		while (curr_adj->sw != root_sw) {
			/* the used_link is the link that was used in dijkstra to reach this node,
			   so the to_port is the local (upstream) port on curr_adj->sw
			 */
			upstream_port = curr_adj->used_link->to_port;
			osm_mcast_tbl_set(p_tbl, mlid_ho, upstream_port);

			/* now we go one step in direction root_sw and add the
			   downstream port for the spanning tree
			 */
			downstream_port = curr_adj->used_link->from_port;
			p_tbl = osm_switch_get_mcast_tbl_ptr(
				adj_list[curr_adj->used_link->from].sw);
			osm_mcast_tbl_set(p_tbl, mlid_ho, downstream_port);

			curr_adj = &adj_list[curr_adj->used_link->from];
		}
	}

	OSM_LOG_EXIT(p_sm->p_log);
	return 0;
}

/* increment the edge weights of the df-/sssp graph which represent the number
   of paths on this link
*/
static void update_weights(osm_ucast_mgr_t * p_mgr, vertex_t * adj_list,
			   uint32_t adj_list_size)
{
	uint32_t i = 0, j = 0;
	uint32_t additional_weight = 0;

	OSM_LOG_ENTER(p_mgr->p_log);

	for (i = 1; i < adj_list_size; i++) {
		/* if no route goes thru this switch -> cycle */
		if (!(adj_list[i].used_link))
			continue;
		additional_weight = adj_list[i].num_hca;

		j = i;
		while (adj_list[j].used_link) {
			/* update the link from pre to this node */
			adj_list[j].used_link->weight += additional_weight;

			j = adj_list[j].used_link->from;
		}
	}

	OSM_LOG_EXIT(p_mgr->p_log);
}

/* get the largest number of virtual lanes which is supported by all switches
   in the subnet
*/
static uint8_t get_avail_vl_in_subn(osm_ucast_mgr_t * p_mgr)
{
	uint32_t i = 0;
	uint8_t vls_avail = 0xFF, port_vls_avail = 0;
	cl_qmap_t *sw_tbl = &p_mgr->p_subn->sw_guid_tbl;
	cl_map_item_t *item = NULL;
	osm_switch_t *sw = NULL;

	/* traverse all switches to get the number of available virtual lanes in the subnet */
	for (item = cl_qmap_head(sw_tbl); item != cl_qmap_end(sw_tbl);
	     item = cl_qmap_next(item)) {
		sw = (osm_switch_t *) item;

		/* ignore management port 0 */
		for (i = 1; i < osm_node_get_num_physp(sw->p_node); i++) {
			osm_physp_t *p_physp =
			    osm_node_get_physp_ptr(sw->p_node, i);

			if (p_physp && p_physp->p_remote_physp) {
				port_vls_avail =
				    ib_port_info_get_op_vls(&p_physp->
							    port_info);
				if (port_vls_avail
				    && port_vls_avail < vls_avail)
					vls_avail = port_vls_avail;
			}
		}
	}

	/* ib_port_info_get_op_vls gives values 1 ... 5 (s. IBAS 14.2.5.6) */
	vls_avail = 1 << (vls_avail - 1);

	/* set boundaries (s. IBAS 3.5.7) */
	if (vls_avail > 15)
		vls_avail = 15;
	if (vls_avail < 1)
		vls_avail = 1;

	return vls_avail;
}

/* search for cycles in the channel dependency graph to identify possible
   deadlocks in the network;
   assign new virtual lanes to some paths to break the deadlocks
*/
static int dfsssp_remove_deadlocks(dfsssp_context_t * dfsssp_ctx)
{
	osm_ucast_mgr_t *p_mgr = (osm_ucast_mgr_t *) dfsssp_ctx->p_mgr;

	cl_qlist_t *port_tbl = &p_mgr->port_order_list;	/* 1 management port per switch + 1 or 2 ports for each Hca */
	cl_list_item_t *item1 = NULL, *item2 = NULL;
	osm_port_t *src_port = NULL, *dest_port = NULL;

	uint32_t i = 0, j = 0, err = 0;
	uint8_t vl = 0, test_vl = 0, vl_avail = 0, vl_needed = 1;
	double most_avg_paths = 0.0;
	cdg_node_t **cdg = NULL, *start_here = NULL, *cycle = NULL;
	cdg_link_t *weakest_link = NULL;
	uint32_t srcdest = 0;

	vltable_t *srcdest2vl_table = NULL;
	uint8_t lmc = 0;
	uint16_t slid = 0, dlid = 0, min_lid_ho = 0, max_lid_ho =
	    0, min_lid_ho2 = 0, max_lid_ho2 = 0;;
	uint64_t *paths_per_vl = NULL;
	uint64_t from = 0, to = 0, count = 0;
	uint8_t *split_count = NULL;
	uint8_t ntype = 0;

	OSM_LOG_ENTER(p_mgr->p_log);
	OSM_LOG(p_mgr->p_log, OSM_LOG_VERBOSE,
		"Assign each src/dest pair a Virtual Lanes, to remove deadlocks in the routing\n");

	vl_avail = get_avail_vl_in_subn(p_mgr);
	OSM_LOG(p_mgr->p_log, OSM_LOG_INFO,
		"Virtual Lanes available: %" PRIu8 "\n", vl_avail);

	paths_per_vl = (uint64_t *) malloc(vl_avail * sizeof(uint64_t));
	if (!paths_per_vl) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
			"ERR AD22: cannot allocate memory for paths_per_vl\n");
		return 1;
	}
	memset(paths_per_vl, 0, vl_avail * sizeof(uint64_t));

	cdg = (cdg_node_t **) malloc(vl_avail * sizeof(cdg_node_t *));
	if (!cdg) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
			"ERR AD23: cannot allocate memory for cdg\n");
		free(paths_per_vl);
		return 1;
	}
	for (i = 0; i < vl_avail; i++)
		cdg[i] = NULL;

	count = 0;
	/* count all ports (also multiple LIDs) of type CA or SP0 for size of VL table */
	for (item1 = cl_qlist_head(port_tbl); item1 != cl_qlist_end(port_tbl);
	     item1 = cl_qlist_next(item1)) {
		dest_port = (osm_port_t *)cl_item_obj(item1, dest_port,
						      list_item);
		ntype = osm_node_get_type(dest_port->p_node);
		if (ntype == IB_NODE_TYPE_CA || ntype == IB_NODE_TYPE_SWITCH) {
			/* only SP0 with SLtoVLMapping support will be processed */
			if (ntype == IB_NODE_TYPE_SWITCH
			    && !(dest_port->p_physp->port_info.capability_mask
			    & IB_PORT_CAP_HAS_SL_MAP))
				continue;

			lmc = osm_port_get_lmc(dest_port);
			count += (1 << lmc);
		}
	}
	/* allocate VL table and indexing array */
	err = vltable_alloc(&srcdest2vl_table, count);
	if (err) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
			"ERR AD26: cannot allocate memory for srcdest2vl_table\n");
		goto ERROR;
	}

	i = 0;
	/* fill lids into indexing array */
	for (item1 = cl_qlist_head(port_tbl); item1 != cl_qlist_end(port_tbl);
	     item1 = cl_qlist_next(item1)) {
		dest_port = (osm_port_t *)cl_item_obj(item1, dest_port,
						      list_item);
		ntype = osm_node_get_type(dest_port->p_node);
		if (ntype == IB_NODE_TYPE_CA || ntype == IB_NODE_TYPE_SWITCH) {
			/* only SP0 with SLtoVLMapping support will be processed */
			if (ntype == IB_NODE_TYPE_SWITCH
			    && !(dest_port->p_physp->port_info.capability_mask
			    & IB_PORT_CAP_HAS_SL_MAP))
				continue;

			osm_port_get_lid_range_ho(dest_port, &min_lid_ho,
						  &max_lid_ho);
			for (dlid = min_lid_ho; dlid <= max_lid_ho; dlid++, i++)
				srcdest2vl_table->lids[i] = cl_hton16(dlid);
		}
	}
	/* sort lids */
	vltable_sort_lids(srcdest2vl_table);

	test_vl = 0;
	/* fill cdg[0] with routes from each src/dest port combination for all Hca/SP0 in the subnet */
	for (item1 = cl_qlist_head(port_tbl); item1 != cl_qlist_end(port_tbl);
	     item1 = cl_qlist_next(item1)) {
		dest_port = (osm_port_t *)cl_item_obj(item1, dest_port,
						      list_item);
		ntype = osm_node_get_type(dest_port->p_node);
		if ((ntype != IB_NODE_TYPE_CA && ntype != IB_NODE_TYPE_SWITCH)
		    || !(dest_port->p_physp->port_info.capability_mask
		    & IB_PORT_CAP_HAS_SL_MAP))
			continue;

		for (item2 = cl_qlist_head(port_tbl);
		     item2 != cl_qlist_end(port_tbl);
		     item2 = cl_qlist_next(item2)) {
			src_port = (osm_port_t *)cl_item_obj(item2, src_port,
							     list_item);
			ntype = osm_node_get_type(src_port->p_node);
			if ((ntype != IB_NODE_TYPE_CA
			    && ntype != IB_NODE_TYPE_SWITCH)
			    || !(src_port->p_physp->port_info.capability_mask
			    & IB_PORT_CAP_HAS_SL_MAP))
				continue;

			if (src_port != dest_port) {
				/* iterate over LIDs of src and dest port */
				osm_port_get_lid_range_ho(src_port, &min_lid_ho,
							  &max_lid_ho);
				for (slid = min_lid_ho; slid <= max_lid_ho;
				     slid++) {
					osm_port_get_lid_range_ho
					    (dest_port, &min_lid_ho2,
					     &max_lid_ho2);
					for (dlid = min_lid_ho2;
					     dlid <= max_lid_ho2;
					     dlid++) {

						/* try to add the path to cdg[0] */
						err =
						    update_channel_dep_graph
						    (&(cdg[test_vl]),
						     src_port, slid,
						     dest_port, dlid);
						if (err) {
							OSM_LOG(p_mgr->
								p_log,
								OSM_LOG_ERROR,
								"ERR AD14: cannot allocate memory for cdg node or link in update_channel_dep_graph(...)\n");
							goto ERROR;
						}
						/* add the <s,d> combination / corresponding virtual lane to the VL table */
						vltable_insert
						    (srcdest2vl_table,
						     cl_hton16(slid),
						     cl_hton16(dlid),
						     test_vl);
						paths_per_vl[test_vl]++;

					}

				}
			}

		}
	}
	dfsssp_ctx->srcdest2vl_table = srcdest2vl_table;

	/* test all cdg for cycles and break the cycles by moving paths on the weakest link to the next cdg */
	for (test_vl = 0; test_vl < vl_avail - 1; test_vl++) {
		start_here = cdg[test_vl];
		while (start_here) {
			cycle =
			    search_cycle_in_channel_dep_graph(cdg[test_vl],
							      start_here);

			if (cycle) {
				vl_needed = test_vl + 2;

				/* calc weakest link n cycle */
				weakest_link = get_weakest_link_in_cycle(cycle);
				if (!weakest_link) {
					OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
						"ERR AD27: something went wrong in get_weakest_link_in_cycle(...)\n");
					err = 1;
					goto ERROR;
				}

				paths_per_vl[test_vl] -=
				    weakest_link->num_pairs;
				paths_per_vl[test_vl + 1] +=
				    weakest_link->num_pairs;

				/* move all <s,d> paths on this link to the next cdg */
				for (i = 0; i < weakest_link->num_pairs; i++) {
					srcdest =
					    get_next_srcdest_pair(weakest_link,
								  i);
					slid = (uint16_t) (srcdest >> 16);
					dlid =
					    (uint16_t) ((srcdest << 16) >> 16);

					/* only move if not moved in a previous step */
					if (test_vl !=
					    (uint8_t)
					    vltable_get_vl(srcdest2vl_table,
							   cl_hton16(slid),
							   cl_hton16(dlid))) {
						/* this path has been moved
						   before -> don't count
						 */
						paths_per_vl[test_vl]++;
						paths_per_vl[test_vl + 1]--;
						continue;
					}

					src_port =
					    osm_get_port_by_lid(p_mgr->p_subn,
								cl_hton16
								(slid));
					dest_port =
					    osm_get_port_by_lid(p_mgr->p_subn,
								cl_hton16
								(dlid));

					/* remove path from current cdg / vl */
					err =
					    remove_path_from_cdg(&
								 (cdg[test_vl]),
								 src_port, slid,
								 dest_port,
								 dlid);
					if (err) {
						OSM_LOG(p_mgr->p_log,
							OSM_LOG_ERROR,
							"ERR AD44: something went wrong in remove_path_from_cdg(...)\n");
						goto ERROR;
					}

					/* add path to next cdg / vl */
					err =
					    update_channel_dep_graph(&
								     (cdg
								      [test_vl +
								       1]),
								     src_port,
								     slid,
								     dest_port,
								     dlid);
					if (err) {
						OSM_LOG(p_mgr->p_log,
							OSM_LOG_ERROR,
							"ERR AD14: cannot allocate memory for cdg node or link in update_channel_dep_graph(...)\n");
						goto ERROR;
					}
					vltable_insert(srcdest2vl_table,
						       cl_hton16(slid),
						       cl_hton16(dlid),
						       test_vl + 1);
				}

				if (weakest_link->num_pairs)
					free(weakest_link->srcdest_pairs);
				if (weakest_link)
					free(weakest_link);
			}

			start_here = cycle;
		}
	}

	/* test the last avail cdg for a cycle;
	   if there is one, than vl_needed > vl_avail
	 */
	start_here = cdg[vl_avail - 1];
	if (start_here) {
		cycle =
		    search_cycle_in_channel_dep_graph(cdg[vl_avail - 1],
						      start_here);
		if (cycle) {
			vl_needed = vl_avail + 1;
		}
	}

	OSM_LOG(p_mgr->p_log, OSM_LOG_INFO,
		"Virtual Lanes needed: %" PRIu8 "\n", vl_needed);
	if (OSM_LOG_IS_ACTIVE_V2(p_mgr->p_log, OSM_LOG_INFO)) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_INFO,
			"Paths per VL (before balancing):\n");
		for (i = 0; i < vl_avail; i++)
			OSM_LOG(p_mgr->p_log, OSM_LOG_INFO,
				"   %" PRIu32 ". lane: %" PRIu64 "\n", i,
				paths_per_vl[i]);
	}

	OSM_LOG(p_mgr->p_log, OSM_LOG_VERBOSE,
		"Balancing the paths on the available Virtual Lanes\n");

	/* optimal balancing virtual lanes, under condition: no additional cycle checks;
	   sl/vl != 0 might be assigned to loopback packets (i.e. slid/dlid on the
	   same port for lmc>0), but thats no problem, see IBAS 10.2.2.3
	 */
	split_count = (uint8_t *) calloc(vl_avail, sizeof(uint8_t));
	if (!split_count) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
			"ERR AD24: cannot allocate memory for split_count, skip balancing\n");
		err = 1;
		goto ERROR;
	}
	/* initial state: paths for VLs won't be separated */
	for (i = 0; i < ((vl_needed < vl_avail) ? vl_needed : vl_avail); i++)
		split_count[i] = 1;
	dfsssp_ctx->vl_split_count = split_count;
	/* balancing is necessary if we have empty VLs */
	if (vl_needed < vl_avail) {
		/* split paths of VLs until we find an equal distribution */
		for (i = vl_needed; i < vl_avail; i++) {
			/* find VL with most paths in it */
			vl = 0;
			most_avg_paths = 0.0;
			for (test_vl = 0; test_vl < vl_needed; test_vl++) {
				if (most_avg_paths <
				    ((double)paths_per_vl[test_vl] /
				    split_count[test_vl])) {
					vl = test_vl;
					most_avg_paths =
						(double)paths_per_vl[test_vl] /
						split_count[test_vl];
				}
			}
			split_count[vl]++;
		}
		/* change the VL assignment depending on split_count for
		   all VLs except VL 0
		 */
		for (from = vl_needed - 1; from > 0; from--) {
			/* how much space needed for others? */
			to = 0;
			for (i = 0; i < from; i++)
				to += split_count[i];
			count = paths_per_vl[from];
			vltable_change_vl(srcdest2vl_table, from, to, count);
			/* change also the information within the split_count
			   array; this is important for fast calculation later
			 */
			split_count[to] = split_count[from];
			split_count[from] = 0;
			paths_per_vl[to] = paths_per_vl[from];
			paths_per_vl[from] = 0;
		}
	} else if (vl_needed > vl_avail) {
		/* routing not possible, a further development would be the LASH-TOR approach (update: LASH-TOR isn't possible, there is a mistake in the theory) */
		OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
			"ERR AD25: Not enough VLs available (avail=%d, needed=%d); Stopping dfsssp routing!\n",
			vl_avail, vl_needed);
		err = 1;
		goto ERROR;
	}
	/* else { no balancing } */

	if (OSM_LOG_IS_ACTIVE_V2(p_mgr->p_log, OSM_LOG_DEBUG)) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"Virtual Lanes per src/dest combination after balancing:\n");
		vltable_print(p_mgr, srcdest2vl_table);
	}
	if (OSM_LOG_IS_ACTIVE_V2(p_mgr->p_log, OSM_LOG_INFO)) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_INFO,
			"Approx. #paths per VL (after balancing):\n");
		j = 0;
		count = 1; /* to prevent div. by 0 */
		for (i = 0; i < vl_avail; i++) {
			if (split_count[i] > 0) {
				j = i;
				count = split_count[i];
			}
			OSM_LOG(p_mgr->p_log, OSM_LOG_INFO,
				"   %" PRIu32 ". lane: %" PRIu64 "\n", i,
				paths_per_vl[j] / count);
		}
	}

	free(paths_per_vl);

	/* deallocate channel dependency graphs */
	for (i = 0; i < vl_avail; i++)
		cdg_dealloc(&cdg[i]);
	free(cdg);

	OSM_LOG_EXIT(p_mgr->p_log);
	return 0;

ERROR:
	free(paths_per_vl);

	for (i = 0; i < vl_avail; i++)
		cdg_dealloc(&cdg[i]);
	free(cdg);

	vltable_dealloc(&srcdest2vl_table);
	dfsssp_ctx->srcdest2vl_table = NULL;

	return err;
}

/* meta function which calls subfunctions for dijkstra, update lft and weights,
   (and remove deadlocks) to calculate the routing for the subnet
*/
static int dfsssp_do_dijkstra_routing(void *context)
{
	dfsssp_context_t *dfsssp_ctx = (dfsssp_context_t *) context;
	osm_ucast_mgr_t *p_mgr = (osm_ucast_mgr_t *) dfsssp_ctx->p_mgr;
	vertex_t *adj_list = (vertex_t *) dfsssp_ctx->adj_list;
	uint32_t adj_list_size = dfsssp_ctx->adj_list_size;

	vertex_t **sw_list = NULL;
	uint32_t sw_list_size = 0;
	uint64_t guid = 0;
	cl_qlist_t *qlist = NULL;
	cl_list_item_t *qlist_item = NULL;

	cl_qmap_t *sw_tbl = &p_mgr->p_subn->sw_guid_tbl;
	cl_qmap_t cn_tbl, io_tbl, *p_mixed_tbl = NULL;
	cl_map_item_t *item = NULL;
	osm_switch_t *sw = NULL;
	osm_port_t *port = NULL;
	uint32_t i = 0, err = 0;
	uint16_t lid = 0, min_lid_ho = 0, max_lid_ho = 0;
	uint8_t lmc = 0;
	boolean_t cn_nodes_provided = FALSE, io_nodes_provided = FALSE;

	OSM_LOG_ENTER(p_mgr->p_log);
	OSM_LOG(p_mgr->p_log, OSM_LOG_VERBOSE,
		"Calculating shortest path from all Hca/switches to all\n");

	cl_qmap_init(&cn_tbl);
	cl_qmap_init(&io_tbl);
	p_mixed_tbl = &cn_tbl;

	cl_qlist_init(&p_mgr->port_order_list);

	/* reset the new_lft for each switch */
	for (item = cl_qmap_head(sw_tbl); item != cl_qmap_end(sw_tbl);
	     item = cl_qmap_next(item)) {
		sw = (osm_switch_t *) item;
		/* initialize LIDs in buffer to invalid port number */
		memset(sw->new_lft, OSM_NO_PATH, sw->max_lid_ho + 1);
		/* initialize LFT and hop count for bsp0/esp0 of the switch */
		min_lid_ho = cl_ntoh16(osm_node_get_base_lid(sw->p_node, 0));
		lmc = osm_node_get_lmc(sw->p_node, 0);
		for (i = min_lid_ho; i < min_lid_ho + (1 << lmc); i++) {
			/* for each switch the port to the 'self'lid is the management port 0 */
			sw->new_lft[i] = 0;
			/* the hop count to the 'self'lid is 0 for each switch */
			osm_switch_set_hops(sw, i, 0, 0);
		}
	}

	/* we need an intermediate array of pointers to switches in adj_list;
	   this array will be sorted in respect to num_hca (descending)
	 */
	sw_list_size = adj_list_size - 1;
	sw_list = (vertex_t **)malloc(sw_list_size * sizeof(vertex_t *));
	if (!sw_list) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
			"ERR AD29: cannot allocate memory for sw_list in dfsssp_do_dijkstra_routing\n");
		goto ERROR;
	}
	memset(sw_list, 0, sw_list_size * sizeof(vertex_t *));

	/* fill the array with references to the 'real' sw in adj_list */
	for (i = 0; i < sw_list_size; i++)
		sw_list[i] = &(adj_list[i + 1]);

	/* sort the sw_list in descending order */
	sw_list_sort_by_num_hca(sw_list, sw_list_size);

	/* parse compute node guid file, if provided by the user */
	if (p_mgr->p_subn->opt.cn_guid_file) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"Parsing compute nodes from file %s\n",
			p_mgr->p_subn->opt.cn_guid_file);

		if (parse_node_map(p_mgr->p_subn->opt.cn_guid_file,
				   add_guid_to_map, &cn_tbl)) {
			OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
				"ERR AD33: Problem parsing compute node guid file\n");
			goto ERROR;
		}

		if (cl_is_qmap_empty(&cn_tbl))
			OSM_LOG(p_mgr->p_log, OSM_LOG_INFO,
				"WRN AD34: compute node guids file contains no valid guids\n");
		else
			cn_nodes_provided = TRUE;
	}

	/* parse I/O guid file, if provided by the user */
	if (p_mgr->p_subn->opt.io_guid_file) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"Parsing I/O nodes from file %s\n",
			p_mgr->p_subn->opt.io_guid_file);

		if (parse_node_map(p_mgr->p_subn->opt.io_guid_file,
				   add_guid_to_map, &io_tbl)) {
			OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
				"ERR AD35: Problem parsing I/O guid file\n");
			goto ERROR;
		}

		if (cl_is_qmap_empty(&io_tbl))
			OSM_LOG(p_mgr->p_log, OSM_LOG_INFO,
				"WRN AD36: I/O node guids file contains no valid guids\n");
		else
			io_nodes_provided = TRUE;
	}

	/* if we mix Hca/Tca/SP0 during the dijkstra routing, we might end up
	   in rare cases with a bad balancing for Hca<->Hca connections, i.e.
	   some inter-switch links get oversubscribed with paths;
	   therefore: add Hca ports first to ensure good Hca<->Hca balancing
	 */
	if (cn_nodes_provided) {
		for (i = 0; i < adj_list_size - 1; i++) {
			if (sw_list[i] && sw_list[i]->sw) {
				sw = (osm_switch_t *)(sw_list[i]->sw);
				add_sw_endports_to_order_list(sw, p_mgr,
							      &cn_tbl, TRUE);
			} else {
				OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
					"ERR AD30: corrupted sw_list array in dfsssp_do_dijkstra_routing\n");
				goto ERROR;
			}
		}
	}
	/* then: add Tca ports to ensure good Hca->Tca balancing and separate
	   paths towards I/O nodes on the same switch (if possible)
	 */
	if (io_nodes_provided) {
		for (i = 0; i < adj_list_size - 1; i++) {
			if (sw_list[i] && sw_list[i]->sw) {
				sw = (osm_switch_t *)(sw_list[i]->sw);
				add_sw_endports_to_order_list(sw, p_mgr,
							      &io_tbl, TRUE);
			} else {
				OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
					"ERR AD32: corrupted sw_list array in dfsssp_do_dijkstra_routing\n");
				goto ERROR;
			}
		}
	}
	/* then: add anything else, such as administration nodes, ... */
	if (cn_nodes_provided && io_nodes_provided) {
		cl_qmap_merge(&cn_tbl, &io_tbl);
	} else if (io_nodes_provided) {
		p_mixed_tbl = &io_tbl;
	}
	for (i = 0; i < adj_list_size - 1; i++) {
		if (sw_list[i] && sw_list[i]->sw) {
			sw = (osm_switch_t *)(sw_list[i]->sw);
			add_sw_endports_to_order_list(sw, p_mgr, p_mixed_tbl,
						      FALSE);
		} else {
			OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
				"ERR AD39: corrupted sw_list array in dfsssp_do_dijkstra_routing\n");
			goto ERROR;
		}
	}
	/* last: add SP0 afterwards which have lower priority for balancing */
	for (i = 0; i < sw_list_size; i++) {
		if (sw_list[i] && sw_list[i]->sw) {
			sw = (osm_switch_t *)(sw_list[i]->sw);
			guid = cl_ntoh64(osm_node_get_node_guid(sw->p_node));
			add_guid_to_order_list(guid, p_mgr);
		} else {
			OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
				"ERR AD31: corrupted sw_list array in dfsssp_do_dijkstra_routing\n");
			goto ERROR;
		}
	}

	/* the intermediate array lived long enough */
	free(sw_list);
	sw_list = NULL;
	/* same is true for the compute node and I/O guid map */
	destroy_guid_map(&cn_tbl);
	cn_nodes_provided = FALSE;
	destroy_guid_map(&io_tbl);
	io_nodes_provided = FALSE;

	/* do the routing for the each Hca in the subnet and each switch
	   in the subnet (to add the routes to base/enhanced SP0)
	 */
	qlist = &p_mgr->port_order_list;
	for (qlist_item = cl_qlist_head(qlist);
	     qlist_item != cl_qlist_end(qlist);
	     qlist_item = cl_qlist_next(qlist_item)) {
		port = (osm_port_t *)cl_item_obj(qlist_item, port, list_item);

		/* calculate shortest path with dijkstra from node to all switches/Hca */
		if (osm_node_get_type(port->p_node) == IB_NODE_TYPE_CA) {
			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"Processing Hca with GUID 0x%" PRIx64 "\n",
				cl_ntoh64(osm_node_get_node_guid
					  (port->p_node)));
		} else if (osm_node_get_type(port->p_node) == IB_NODE_TYPE_SWITCH) {
			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"Processing switch with GUID 0x%" PRIx64 "\n",
				cl_ntoh64(osm_node_get_node_guid
					  (port->p_node)));
		} else {
			/* we don't handle routers, in case they show up */
			continue;
		}

		/* distribute the LID range across the ports that can reach those LIDs
		   to have disjoint paths for one destination port with lmc>0;
		   for switches with bsp0: min=max; with esp0: max>min if lmc>0
		 */
		osm_port_get_lid_range_ho(port, &min_lid_ho,
					  &max_lid_ho);
		for (lid = min_lid_ho; lid <= max_lid_ho; lid++) {
			/* do dijkstra from this Hca/LID/SP0 to each switch */
			err =
			    dijkstra(p_mgr, adj_list, adj_list_size, port, lid);
			if (err)
				goto ERROR;
			if (OSM_LOG_IS_ACTIVE_V2(p_mgr->p_log, OSM_LOG_DEBUG))
				print_routes(p_mgr, adj_list, adj_list_size,
					     port);

			/* make an update for the linear forwarding tables of the switches */
			err =
			    update_lft(p_mgr, adj_list, adj_list_size, port, lid);
			if (err)
				goto ERROR;

			/* add weights for calculated routes to adjust the weights for the next cycle */
			update_weights(p_mgr, adj_list, adj_list_size);

			if (OSM_LOG_IS_ACTIVE_V2(p_mgr->p_log, OSM_LOG_DEBUG))
				dfsssp_print_graph(p_mgr, adj_list,
						   adj_list_size);
		}
	}

	/* try deadlock removal only for the dfsssp routing (not for the sssp case, which is a subset of the dfsssp algorithm) */
	if (dfsssp_ctx->routing_type == OSM_ROUTING_ENGINE_TYPE_DFSSSP) {
		/* remove potential deadlocks by assigning different virtual lanes to src/dest paths and balance the lanes */
		err = dfsssp_remove_deadlocks(dfsssp_ctx);
		if (err)
			goto ERROR;
	} else if (dfsssp_ctx->routing_type == OSM_ROUTING_ENGINE_TYPE_SSSP) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_INFO,
			"SSSP routing specified -> skipping deadlock removal thru dfsssp_remove_deadlocks(...)\n");
	} else {
		OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
			"ERR AD28: wrong routing engine specified in dfsssp_ctx\n");
		goto ERROR;
	}

	/* list not needed after the dijkstra steps and deadlock removal */
	cl_qlist_remove_all(&p_mgr->port_order_list);

	/* print the new_lft for each switch after routing is done */
	if (OSM_LOG_IS_ACTIVE_V2(p_mgr->p_log, OSM_LOG_DEBUG)) {
		for (item = cl_qmap_head(sw_tbl); item != cl_qmap_end(sw_tbl);
		     item = cl_qmap_next(item)) {
			sw = (osm_switch_t *) item;
			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"Summary of the (new) LFT for switch 0x%" PRIx64
				" (%s):\n",
				cl_ntoh64(osm_node_get_node_guid(sw->p_node)),
				sw->p_node->print_desc);
			for (i = 0; i < sw->max_lid_ho + 1; i++)
				if (sw->new_lft[i] != OSM_NO_PATH) {
					OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
						"   for LID=%" PRIu32
						" use port=%" PRIu8 "\n", i,
						sw->new_lft[i]);
				}
		}
	}

	OSM_LOG_EXIT(p_mgr->p_log);
	return 0;

ERROR:
	if (!cl_is_qlist_empty(&p_mgr->port_order_list))
		cl_qlist_remove_all(&p_mgr->port_order_list);
	if (cn_nodes_provided)
		destroy_guid_map(&cn_tbl);
	if (io_nodes_provided)
		destroy_guid_map(&io_tbl);
	if (sw_list)
		free(sw_list);
	return -1;
}

/* meta function which calls subfunctions for finding the optimal switch
   for the spanning tree, performing a dijkstra step with this sw as root,
   and calculating the mcast table for MLID
*/
static ib_api_status_t dfsssp_do_mcast_routing(void * context,
					       osm_mgrp_box_t * mbox)
{
	dfsssp_context_t *dfsssp_ctx = (dfsssp_context_t *) context;
	osm_ucast_mgr_t *p_mgr = (osm_ucast_mgr_t *) dfsssp_ctx->p_mgr;
	osm_sm_t *sm = (osm_sm_t *) p_mgr->sm;
	vertex_t *adj_list = (vertex_t *) dfsssp_ctx->adj_list;
	uint32_t adj_list_size = dfsssp_ctx->adj_list_size;
	cl_qlist_t mcastgrp_port_list;
	cl_qmap_t mcastgrp_port_map;
	osm_switch_t *root_sw = NULL, *p_sw = NULL;
	osm_port_t *port = NULL;
	ib_net16_t lid = 0;
	uint32_t err = 0, num_ports = 0, i = 0;
	ib_net64_t guid = 0;
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(sm->p_log);

	/* using the ucast cache feature with dfsssp might mean that a leaf sw
	   got removed (and got back) without calling dfsssp_build_graph
	   and therefore the adj_list (and pointers to osm's internal switches)
	   could be outdated (here we have no knowledge if it has happened, so
	   unfortunately a check is necessary... still better than rebuilding
	   adj_list every time we arrive here)
	 */
	if (p_mgr->p_subn->opt.use_ucast_cache && p_mgr->cache_valid) {
		for (i = 1; i < adj_list_size; i++) {
			guid = cl_hton64(adj_list[i].guid);
			p_sw = osm_get_switch_by_guid(p_mgr->p_subn, guid);
			if (p_sw) {
				/* check if switch came back from the dead */
				if (adj_list[i].dropped)
					adj_list[i].dropped = FALSE;

				/* verify that sw object has not been moved
				   (this can happen for a leaf switch, if it
				   was dropped and came back later without a
				   rerouting), otherwise we have to update
				   dfsssp's internal switch list with the new
				   sw pointer
				 */
				if (p_sw == adj_list[i].sw)
					continue;
				else
					adj_list[i].sw = p_sw;
			} else {
				/* if a switch from adj_list is not in the
				   sw_guid_tbl anymore, then the only reason is
				   that it was a leaf switch and opensm dropped
				   it without calling a rerouting
				   -> calling dijkstra is no problem, since it
				      is a leaf and different from root_sw
				   -> only update_mcft and reset_mgrp_membership
				      need to be aware of these dropped switches
				 */
				if (!adj_list[i].dropped)
					adj_list[i].dropped = TRUE;
			}
		}
	}

	/* create a map and a list of all ports which are member in the mcast
	   group; map for searching elements and list for iteration
	 */
	if (osm_mcast_make_port_list_and_map(&mcastgrp_port_list,
					     &mcastgrp_port_map, mbox)) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR AD50: "
			"Insufficient memory to make port list\n");
		status = IB_ERROR;
		goto Exit;
	}

	num_ports = cl_qlist_count(&mcastgrp_port_list);
	if (num_ports < 2) {
		OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
			"MLID 0x%X has %u members - nothing to do\n",
			mbox->mlid, num_ports);
		goto Exit;
	}

	/* find the root switch for the spanning tree, which has the smallest
	   hops count to all LIDs in the mcast group
	 */
	root_sw = osm_mcast_mgr_find_root_switch(sm, &mcastgrp_port_list);
	if (!root_sw) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR AD51: "
			"Unable to locate a suitable switch for group 0x%X\n",
			mbox->mlid);
		status = IB_ERROR;
		goto Exit;
	}

	/* a) start one dijkstra step from the root switch to generate a
	   spanning tree
	   b) this might be a bit of an overkill to span the whole
	   network, if there are only a few ports in the mcast group, but
	   its only one dijkstra step for each mcast group and we did many
	   steps before in the ucast routing for each LID in the subnet;
	   c) we can use the subnet structure from the ucast routing, and
	   don't even have to reset the link weights (=> therefore the mcast
	   spanning tree will use less 'growded' links in the network)
	   d) the mcast dfsssp algorithm will not change the link weights
	 */
	lid = osm_node_get_base_lid(root_sw->p_node, 0);
	port = osm_get_port_by_lid(sm->p_subn, lid);
	err = dijkstra(p_mgr, adj_list, adj_list_size, port, lid);
	if (err) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR AD52: "
			"Dijkstra step for mcast failed for group 0x%X\n",
			mbox->mlid);
		status = IB_ERROR;
		goto Exit;
	}

	/* set mcast group membership again for update_mcft
	   (unfortunately: osm_mcast_mgr_find_root_switch resets it)
	 */
	update_mgrp_membership(&mcastgrp_port_list);

	/* update the mcast forwarding tables of the switches */
	err = update_mcft(sm, adj_list, adj_list_size, mbox->mlid,
			  &mcastgrp_port_map, root_sw);
	if (err) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR AD53: "
			"Update of mcast forwarding tables failed for group 0x%X\n",
			mbox->mlid);
		status = IB_ERROR;
		goto Exit;
	}

Exit:
	reset_mgrp_membership(adj_list, adj_list_size);
	osm_mcast_drop_port_list(&mcastgrp_port_list);
	OSM_LOG_EXIT(sm->p_log);
	return status;
}

/* called from extern in QP creation process to gain the the service level and
   the virtual lane respectively for a <s,d> pair
*/
static uint8_t get_dfsssp_sl(void *context, uint8_t hint_for_default_sl,
			     const ib_net16_t slid, const ib_net16_t dlid)
{
	dfsssp_context_t *dfsssp_ctx = (dfsssp_context_t *) context;
	osm_port_t *src_port, *dest_port;
	vltable_t *srcdest2vl_table = NULL;
	uint8_t *vl_split_count = NULL;
	osm_ucast_mgr_t *p_mgr = NULL;
	int32_t res = 0;

	if (dfsssp_ctx
	    && dfsssp_ctx->routing_type == OSM_ROUTING_ENGINE_TYPE_DFSSSP) {
		p_mgr = (osm_ucast_mgr_t *) dfsssp_ctx->p_mgr;
		srcdest2vl_table = (vltable_t *) (dfsssp_ctx->srcdest2vl_table);
		vl_split_count = (uint8_t *) (dfsssp_ctx->vl_split_count);
	}
	else
		return hint_for_default_sl;

	src_port = osm_get_port_by_lid(p_mgr->p_subn, slid);
	if (!src_port)
		return hint_for_default_sl;

	dest_port = osm_get_port_by_lid(p_mgr->p_subn, dlid);
	if (!dest_port)
		return hint_for_default_sl;

	if (!srcdest2vl_table)
		return hint_for_default_sl;

	res = vltable_get_vl(srcdest2vl_table, slid, dlid);

	/* we will randomly distribute the traffic over multiple VLs if
	   necessary for good balancing; therefore vl_split_count provides
	   the number of VLs to use for certain traffic
	 */
	if (res > -1) {
		if (vl_split_count[res] > 1)
			return (uint8_t) (res + rand()%(vl_split_count[res]));
		else
			return (uint8_t) res;
	} else
		return hint_for_default_sl;
}

static dfsssp_context_t *dfsssp_context_create(osm_opensm_t * p_osm,
					       osm_routing_engine_type_t
					       routing_type)
{
	dfsssp_context_t *dfsssp_ctx = NULL;

	/* allocate memory */
	dfsssp_ctx = (dfsssp_context_t *) malloc(sizeof(dfsssp_context_t));
	if (dfsssp_ctx) {
		/* set initial values */
		dfsssp_ctx->routing_type = routing_type;
		dfsssp_ctx->p_mgr = (osm_ucast_mgr_t *) & (p_osm->sm.ucast_mgr);
		dfsssp_ctx->adj_list = NULL;
		dfsssp_ctx->adj_list_size = 0;
		dfsssp_ctx->srcdest2vl_table = NULL;
		dfsssp_ctx->vl_split_count = NULL;
	} else {
		OSM_LOG(p_osm->sm.ucast_mgr.p_log, OSM_LOG_ERROR,
			"ERR AD04: cannot allocate memory for dfsssp_ctx in dfsssp_context_create\n");
		return NULL;
	}

	return dfsssp_ctx;
}

static void dfsssp_context_destroy(void *context)
{
	dfsssp_context_t *dfsssp_ctx = (dfsssp_context_t *) context;
	vertex_t *adj_list = (vertex_t *) (dfsssp_ctx->adj_list);
	uint32_t i = 0;
	link_t *link = NULL, *tmp = NULL;

	/* free adj_list */
	for (i = 0; i < dfsssp_ctx->adj_list_size; i++) {
		link = adj_list[i].links;
		while (link) {
			tmp = link;
			link = link->next;
			free(tmp);
		}
	}
	free(adj_list);
	dfsssp_ctx->adj_list = NULL;
	dfsssp_ctx->adj_list_size = 0;

	/* free srcdest2vl table and the split count information table
	   (can be done, because dfsssp_context_destroy is called after
	    osm_get_dfsssp_sl)
	 */
	vltable_dealloc(&(dfsssp_ctx->srcdest2vl_table));
	dfsssp_ctx->srcdest2vl_table = NULL;

	if (dfsssp_ctx->vl_split_count) {
		free(dfsssp_ctx->vl_split_count);
		dfsssp_ctx->vl_split_count = NULL;
	}
}

static void delete(void *context)
{
	if (!context)
		return;
	dfsssp_context_destroy(context);

	free(context);
}

int osm_ucast_dfsssp_setup(struct osm_routing_engine *r, osm_opensm_t * p_osm)
{
	/* create context container and add ucast management object */
	dfsssp_context_t *dfsssp_context =
	    dfsssp_context_create(p_osm, OSM_ROUTING_ENGINE_TYPE_DFSSSP);
	if (!dfsssp_context) {
		return 1;	/* alloc failed -> skip this routing */
	}

	/* reset function pointers to dfsssp routines */
	r->context = (void *)dfsssp_context;
	r->build_lid_matrices = dfsssp_build_graph;
	r->ucast_build_fwd_tables = dfsssp_do_dijkstra_routing;
	r->mcast_build_stree = dfsssp_do_mcast_routing;
	r->path_sl = get_dfsssp_sl;
	r->destroy = delete;

	/* we initialize with the current time to achieve a 'good' randomized
	   assignment in get_dfsssp_sl(...)
	 */
	srand(time(NULL));

	return 0;
}

int osm_ucast_sssp_setup(struct osm_routing_engine *r, osm_opensm_t * p_osm)
{
	/* create context container and add ucast management object */
	dfsssp_context_t *dfsssp_context =
	    dfsssp_context_create(p_osm, OSM_ROUTING_ENGINE_TYPE_SSSP);
	if (!dfsssp_context) {
		return 1;	/* alloc failed -> skip this routing */
	}

	/* reset function pointers to sssp routines */
	r->context = (void *)dfsssp_context;
	r->build_lid_matrices = dfsssp_build_graph;
	r->ucast_build_fwd_tables = dfsssp_do_dijkstra_routing;
	r->mcast_build_stree = dfsssp_do_mcast_routing;
	r->destroy = delete;

	return 0;
}
