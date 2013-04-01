/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * nodemanager.h
 *
 * Function prototypes
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 */

#ifndef R2CLUSTER_NODEMANAGER_H
#define R2CLUSTER_NODEMANAGER_H

#include "ramster_nodemanager.h"

/* This totally doesn't belong here. */
#include <linux/configfs.h>
#include <linux/rbtree.h>

enum r2nm_fence_method {
	R2NM_FENCE_RESET	= 0,
	R2NM_FENCE_PANIC,
	R2NM_FENCE_METHODS,	/* Number of fence methods */
};

struct r2nm_node {
	spinlock_t		nd_lock;
	struct config_item	nd_item;
	char			nd_name[R2NM_MAX_NAME_LEN+1]; /* replace? */
	__u8			nd_num;
	/* only one address per node, as attributes, for now. */
	__be32			nd_ipv4_address;
	__be16			nd_ipv4_port;
	struct rb_node		nd_ip_node;
	/* there can be only one local node for now */
	int			nd_local;

	unsigned long		nd_set_attributes;
};

struct r2nm_cluster {
	struct config_group	cl_group;
	unsigned		cl_has_local:1;
	u8			cl_local_node;
	rwlock_t		cl_nodes_lock;
	struct r2nm_node	*cl_nodes[R2NM_MAX_NODES];
	struct rb_root		cl_node_ip_tree;
	unsigned int		cl_idle_timeout_ms;
	unsigned int		cl_keepalive_delay_ms;
	unsigned int		cl_reconnect_delay_ms;
	enum r2nm_fence_method	cl_fence_method;

	/* part of a hack for disk bitmap.. will go eventually. - zab */
	unsigned long	cl_nodes_bitmap[BITS_TO_LONGS(R2NM_MAX_NODES)];
};

extern struct r2nm_cluster *r2nm_single_cluster;

u8 r2nm_this_node(void);

int r2nm_configured_node_map(unsigned long *map, unsigned bytes);
struct r2nm_node *r2nm_get_node_by_num(u8 node_num);
struct r2nm_node *r2nm_get_node_by_ip(__be32 addr);
void r2nm_node_get(struct r2nm_node *node);
void r2nm_node_put(struct r2nm_node *node);

int r2nm_depend_item(struct config_item *item);
void r2nm_undepend_item(struct config_item *item);
int r2nm_depend_this_node(void);
void r2nm_undepend_this_node(void);

#endif /* R2CLUSTER_NODEMANAGER_H */
