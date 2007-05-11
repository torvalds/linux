/*
 * Copyright (C) 2003-2006 Kristian Hoegsberg <krh@bitplanet.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __fw_topology_h
#define __fw_topology_h

enum {
	FW_TOPOLOGY_A =		0x01,
	FW_TOPOLOGY_B =		0x02,
	FW_TOPOLOGY_MIXED =	0x03,
};

enum {
	FW_NODE_CREATED =   0x00,
	FW_NODE_UPDATED =   0x01,
	FW_NODE_DESTROYED = 0x02,
	FW_NODE_LINK_ON =   0x03,
	FW_NODE_LINK_OFF =  0x04,
};

struct fw_port {
	struct fw_node *node;
	unsigned speed : 3; /* S100, S200, ... S3200 */
};

struct fw_node {
	u16 node_id;
	u8 color;
	u8 port_count;
	unsigned link_on : 1;
	unsigned initiated_reset : 1;
	unsigned b_path : 1;
	u8 phy_speed : 3; /* As in the self ID packet. */
	u8 max_speed : 5; /* Minimum of all phy-speeds and port speeds on
			   * the path from the local node to this node. */
	u8 max_depth : 4; /* Maximum depth to any leaf node */
	u8 max_hops : 4;  /* Max hops in this sub tree */
	atomic_t ref_count;

	/* For serializing node topology into a list. */
	struct list_head link;

	/* Upper layer specific data. */
	void *data;

	struct fw_port ports[0];
};

static inline struct fw_node *
fw_node(struct list_head *l)
{
	return list_entry(l, struct fw_node, link);
}

static inline struct fw_node *
fw_node_get(struct fw_node *node)
{
	atomic_inc(&node->ref_count);

	return node;
}

static inline void
fw_node_put(struct fw_node *node)
{
	if (atomic_dec_and_test(&node->ref_count))
		kfree(node);
}

void
fw_destroy_nodes(struct fw_card *card);

int
fw_compute_block_crc(u32 *block);


#endif /* __fw_topology_h */
