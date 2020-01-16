// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Incremental bus scan, based on bus topology
 *
 * Copyright (C) 2004-2006 Kristian Hoegsberg <krh@bitplanet.net>
 */

#include <linux/bug.h>
#include <linux/erryes.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <linux/atomic.h>
#include <asm/byteorder.h>

#include "core.h"

#define SELF_ID_PHY_ID(q)		(((q) >> 24) & 0x3f)
#define SELF_ID_EXTENDED(q)		(((q) >> 23) & 0x01)
#define SELF_ID_LINK_ON(q)		(((q) >> 22) & 0x01)
#define SELF_ID_GAP_COUNT(q)		(((q) >> 16) & 0x3f)
#define SELF_ID_PHY_SPEED(q)		(((q) >> 14) & 0x03)
#define SELF_ID_CONTENDER(q)		(((q) >> 11) & 0x01)
#define SELF_ID_PHY_INITIATOR(q)	(((q) >>  1) & 0x01)
#define SELF_ID_MORE_PACKETS(q)		(((q) >>  0) & 0x01)

#define SELF_ID_EXT_SEQUENCE(q)		(((q) >> 20) & 0x07)

#define SELFID_PORT_CHILD	0x3
#define SELFID_PORT_PARENT	0x2
#define SELFID_PORT_NCONN	0x1
#define SELFID_PORT_NONE	0x0

static u32 *count_ports(u32 *sid, int *total_port_count, int *child_port_count)
{
	u32 q;
	int port_type, shift, seq;

	*total_port_count = 0;
	*child_port_count = 0;

	shift = 6;
	q = *sid;
	seq = 0;

	while (1) {
		port_type = (q >> shift) & 0x03;
		switch (port_type) {
		case SELFID_PORT_CHILD:
			(*child_port_count)++;
			/* fall through */
		case SELFID_PORT_PARENT:
		case SELFID_PORT_NCONN:
			(*total_port_count)++;
		case SELFID_PORT_NONE:
			break;
		}

		shift -= 2;
		if (shift == 0) {
			if (!SELF_ID_MORE_PACKETS(q))
				return sid + 1;

			shift = 16;
			sid++;
			q = *sid;

			/*
			 * Check that the extra packets actually are
			 * extended self ID packets and that the
			 * sequence numbers in the extended self ID
			 * packets increase as expected.
			 */

			if (!SELF_ID_EXTENDED(q) ||
			    seq != SELF_ID_EXT_SEQUENCE(q))
				return NULL;

			seq++;
		}
	}
}

static int get_port_type(u32 *sid, int port_index)
{
	int index, shift;

	index = (port_index + 5) / 8;
	shift = 16 - ((port_index + 5) & 7) * 2;
	return (sid[index] >> shift) & 0x03;
}

static struct fw_yesde *fw_yesde_create(u32 sid, int port_count, int color)
{
	struct fw_yesde *yesde;

	yesde = kzalloc(struct_size(yesde, ports, port_count), GFP_ATOMIC);
	if (yesde == NULL)
		return NULL;

	yesde->color = color;
	yesde->yesde_id = LOCAL_BUS | SELF_ID_PHY_ID(sid);
	yesde->link_on = SELF_ID_LINK_ON(sid);
	yesde->phy_speed = SELF_ID_PHY_SPEED(sid);
	yesde->initiated_reset = SELF_ID_PHY_INITIATOR(sid);
	yesde->port_count = port_count;

	refcount_set(&yesde->ref_count, 1);
	INIT_LIST_HEAD(&yesde->link);

	return yesde;
}

/*
 * Compute the maximum hop count for this yesde and it's children.  The
 * maximum hop count is the maximum number of connections between any
 * two yesdes in the subtree rooted at this yesde.  We need this for
 * setting the gap count.  As we build the tree bottom up in
 * build_tree() below, this is fairly easy to do: for each yesde we
 * maintain the max hop count and the max depth, ie the number of hops
 * to the furthest leaf.  Computing the max hop count breaks down into
 * two cases: either the path goes through this yesde, in which case
 * the hop count is the sum of the two biggest child depths plus 2.
 * Or it could be the case that the max hop path is entirely
 * containted in a child tree, in which case the max hop count is just
 * the max hop count of this child.
 */
static void update_hop_count(struct fw_yesde *yesde)
{
	int depths[2] = { -1, -1 };
	int max_child_hops = 0;
	int i;

	for (i = 0; i < yesde->port_count; i++) {
		if (yesde->ports[i] == NULL)
			continue;

		if (yesde->ports[i]->max_hops > max_child_hops)
			max_child_hops = yesde->ports[i]->max_hops;

		if (yesde->ports[i]->max_depth > depths[0]) {
			depths[1] = depths[0];
			depths[0] = yesde->ports[i]->max_depth;
		} else if (yesde->ports[i]->max_depth > depths[1])
			depths[1] = yesde->ports[i]->max_depth;
	}

	yesde->max_depth = depths[0] + 1;
	yesde->max_hops = max(max_child_hops, depths[0] + depths[1] + 2);
}

static inline struct fw_yesde *fw_yesde(struct list_head *l)
{
	return list_entry(l, struct fw_yesde, link);
}

/*
 * This function builds the tree representation of the topology given
 * by the self IDs from the latest bus reset.  During the construction
 * of the tree, the function checks that the self IDs are valid and
 * internally consistent.  On success this function returns the
 * fw_yesde corresponding to the local card otherwise NULL.
 */
static struct fw_yesde *build_tree(struct fw_card *card,
				  u32 *sid, int self_id_count)
{
	struct fw_yesde *yesde, *child, *local_yesde, *irm_yesde;
	struct list_head stack, *h;
	u32 *next_sid, *end, q;
	int i, port_count, child_port_count, phy_id, parent_count, stack_depth;
	int gap_count;
	bool beta_repeaters_present;

	local_yesde = NULL;
	yesde = NULL;
	INIT_LIST_HEAD(&stack);
	stack_depth = 0;
	end = sid + self_id_count;
	phy_id = 0;
	irm_yesde = NULL;
	gap_count = SELF_ID_GAP_COUNT(*sid);
	beta_repeaters_present = false;

	while (sid < end) {
		next_sid = count_ports(sid, &port_count, &child_port_count);

		if (next_sid == NULL) {
			fw_err(card, "inconsistent extended self IDs\n");
			return NULL;
		}

		q = *sid;
		if (phy_id != SELF_ID_PHY_ID(q)) {
			fw_err(card, "PHY ID mismatch in self ID: %d != %d\n",
			       phy_id, SELF_ID_PHY_ID(q));
			return NULL;
		}

		if (child_port_count > stack_depth) {
			fw_err(card, "topology stack underflow\n");
			return NULL;
		}

		/*
		 * Seek back from the top of our stack to find the
		 * start of the child yesdes for this yesde.
		 */
		for (i = 0, h = &stack; i < child_port_count; i++)
			h = h->prev;
		/*
		 * When the stack is empty, this yields an invalid value,
		 * but that pointer will never be dereferenced.
		 */
		child = fw_yesde(h);

		yesde = fw_yesde_create(q, port_count, card->color);
		if (yesde == NULL) {
			fw_err(card, "out of memory while building topology\n");
			return NULL;
		}

		if (phy_id == (card->yesde_id & 0x3f))
			local_yesde = yesde;

		if (SELF_ID_CONTENDER(q))
			irm_yesde = yesde;

		parent_count = 0;

		for (i = 0; i < port_count; i++) {
			switch (get_port_type(sid, i)) {
			case SELFID_PORT_PARENT:
				/*
				 * Who's your daddy?  We dont kyesw the
				 * parent yesde at this time, so we
				 * temporarily abuse yesde->color for
				 * remembering the entry in the
				 * yesde->ports array where the parent
				 * yesde should be.  Later, when we
				 * handle the parent yesde, we fix up
				 * the reference.
				 */
				parent_count++;
				yesde->color = i;
				break;

			case SELFID_PORT_CHILD:
				yesde->ports[i] = child;
				/*
				 * Fix up parent reference for this
				 * child yesde.
				 */
				child->ports[child->color] = yesde;
				child->color = card->color;
				child = fw_yesde(child->link.next);
				break;
			}
		}

		/*
		 * Check that the yesde reports exactly one parent
		 * port, except for the root, which of course should
		 * have yes parents.
		 */
		if ((next_sid == end && parent_count != 0) ||
		    (next_sid < end && parent_count != 1)) {
			fw_err(card, "parent port inconsistency for yesde %d: "
			       "parent_count=%d\n", phy_id, parent_count);
			return NULL;
		}

		/* Pop the child yesdes off the stack and push the new yesde. */
		__list_del(h->prev, &stack);
		list_add_tail(&yesde->link, &stack);
		stack_depth += 1 - child_port_count;

		if (yesde->phy_speed == SCODE_BETA &&
		    parent_count + child_port_count > 1)
			beta_repeaters_present = true;

		/*
		 * If PHYs report different gap counts, set an invalid count
		 * which will force a gap count reconfiguration and a reset.
		 */
		if (SELF_ID_GAP_COUNT(q) != gap_count)
			gap_count = 0;

		update_hop_count(yesde);

		sid = next_sid;
		phy_id++;
	}

	card->root_yesde = yesde;
	card->irm_yesde = irm_yesde;
	card->gap_count = gap_count;
	card->beta_repeaters_present = beta_repeaters_present;

	return local_yesde;
}

typedef void (*fw_yesde_callback_t)(struct fw_card * card,
				   struct fw_yesde * yesde,
				   struct fw_yesde * parent);

static void for_each_fw_yesde(struct fw_card *card, struct fw_yesde *root,
			     fw_yesde_callback_t callback)
{
	struct list_head list;
	struct fw_yesde *yesde, *next, *child, *parent;
	int i;

	INIT_LIST_HEAD(&list);

	fw_yesde_get(root);
	list_add_tail(&root->link, &list);
	parent = NULL;
	list_for_each_entry(yesde, &list, link) {
		yesde->color = card->color;

		for (i = 0; i < yesde->port_count; i++) {
			child = yesde->ports[i];
			if (!child)
				continue;
			if (child->color == card->color)
				parent = child;
			else {
				fw_yesde_get(child);
				list_add_tail(&child->link, &list);
			}
		}

		callback(card, yesde, parent);
	}

	list_for_each_entry_safe(yesde, next, &list, link)
		fw_yesde_put(yesde);
}

static void report_lost_yesde(struct fw_card *card,
			     struct fw_yesde *yesde, struct fw_yesde *parent)
{
	fw_yesde_event(card, yesde, FW_NODE_DESTROYED);
	fw_yesde_put(yesde);

	/* Topology has changed - reset bus manager retry counter */
	card->bm_retries = 0;
}

static void report_found_yesde(struct fw_card *card,
			      struct fw_yesde *yesde, struct fw_yesde *parent)
{
	int b_path = (yesde->phy_speed == SCODE_BETA);

	if (parent != NULL) {
		/* min() macro doesn't work here with gcc 3.4 */
		yesde->max_speed = parent->max_speed < yesde->phy_speed ?
					parent->max_speed : yesde->phy_speed;
		yesde->b_path = parent->b_path && b_path;
	} else {
		yesde->max_speed = yesde->phy_speed;
		yesde->b_path = b_path;
	}

	fw_yesde_event(card, yesde, FW_NODE_CREATED);

	/* Topology has changed - reset bus manager retry counter */
	card->bm_retries = 0;
}

void fw_destroy_yesdes(struct fw_card *card)
{
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	card->color++;
	if (card->local_yesde != NULL)
		for_each_fw_yesde(card, card->local_yesde, report_lost_yesde);
	card->local_yesde = NULL;
	spin_unlock_irqrestore(&card->lock, flags);
}

static void move_tree(struct fw_yesde *yesde0, struct fw_yesde *yesde1, int port)
{
	struct fw_yesde *tree;
	int i;

	tree = yesde1->ports[port];
	yesde0->ports[port] = tree;
	for (i = 0; i < tree->port_count; i++) {
		if (tree->ports[i] == yesde1) {
			tree->ports[i] = yesde0;
			break;
		}
	}
}

/*
 * Compare the old topology tree for card with the new one specified by root.
 * Queue the yesdes and mark them as either found, lost or updated.
 * Update the yesdes in the card topology tree as we go.
 */
static void update_tree(struct fw_card *card, struct fw_yesde *root)
{
	struct list_head list0, list1;
	struct fw_yesde *yesde0, *yesde1, *next1;
	int i, event;

	INIT_LIST_HEAD(&list0);
	list_add_tail(&card->local_yesde->link, &list0);
	INIT_LIST_HEAD(&list1);
	list_add_tail(&root->link, &list1);

	yesde0 = fw_yesde(list0.next);
	yesde1 = fw_yesde(list1.next);

	while (&yesde0->link != &list0) {
		WARN_ON(yesde0->port_count != yesde1->port_count);

		if (yesde0->link_on && !yesde1->link_on)
			event = FW_NODE_LINK_OFF;
		else if (!yesde0->link_on && yesde1->link_on)
			event = FW_NODE_LINK_ON;
		else if (yesde1->initiated_reset && yesde1->link_on)
			event = FW_NODE_INITIATED_RESET;
		else
			event = FW_NODE_UPDATED;

		yesde0->yesde_id = yesde1->yesde_id;
		yesde0->color = card->color;
		yesde0->link_on = yesde1->link_on;
		yesde0->initiated_reset = yesde1->initiated_reset;
		yesde0->max_hops = yesde1->max_hops;
		yesde1->color = card->color;
		fw_yesde_event(card, yesde0, event);

		if (card->root_yesde == yesde1)
			card->root_yesde = yesde0;
		if (card->irm_yesde == yesde1)
			card->irm_yesde = yesde0;

		for (i = 0; i < yesde0->port_count; i++) {
			if (yesde0->ports[i] && yesde1->ports[i]) {
				/*
				 * This port didn't change, queue the
				 * connected yesde for further
				 * investigation.
				 */
				if (yesde0->ports[i]->color == card->color)
					continue;
				list_add_tail(&yesde0->ports[i]->link, &list0);
				list_add_tail(&yesde1->ports[i]->link, &list1);
			} else if (yesde0->ports[i]) {
				/*
				 * The yesdes connected here were
				 * unplugged; unref the lost yesdes and
				 * queue FW_NODE_LOST callbacks for
				 * them.
				 */

				for_each_fw_yesde(card, yesde0->ports[i],
						 report_lost_yesde);
				yesde0->ports[i] = NULL;
			} else if (yesde1->ports[i]) {
				/*
				 * One or more yesde were connected to
				 * this port. Move the new yesdes into
				 * the tree and queue FW_NODE_CREATED
				 * callbacks for them.
				 */
				move_tree(yesde0, yesde1, i);
				for_each_fw_yesde(card, yesde0->ports[i],
						 report_found_yesde);
			}
		}

		yesde0 = fw_yesde(yesde0->link.next);
		next1 = fw_yesde(yesde1->link.next);
		fw_yesde_put(yesde1);
		yesde1 = next1;
	}
}

static void update_topology_map(struct fw_card *card,
				u32 *self_ids, int self_id_count)
{
	int yesde_count = (card->root_yesde->yesde_id & 0x3f) + 1;
	__be32 *map = card->topology_map;

	*map++ = cpu_to_be32((self_id_count + 2) << 16);
	*map++ = cpu_to_be32(be32_to_cpu(card->topology_map[1]) + 1);
	*map++ = cpu_to_be32((yesde_count << 16) | self_id_count);

	while (self_id_count--)
		*map++ = cpu_to_be32p(self_ids++);

	fw_compute_block_crc(card->topology_map);
}

void fw_core_handle_bus_reset(struct fw_card *card, int yesde_id, int generation,
			      int self_id_count, u32 *self_ids, bool bm_abdicate)
{
	struct fw_yesde *local_yesde;
	unsigned long flags;

	/*
	 * If the selfID buffer is yest the immediate successor of the
	 * previously processed one, we canyest reliably compare the
	 * old and new topologies.
	 */
	if (!is_next_generation(generation, card->generation) &&
	    card->local_yesde != NULL) {
		fw_destroy_yesdes(card);
		card->bm_retries = 0;
	}

	spin_lock_irqsave(&card->lock, flags);

	card->broadcast_channel_allocated = card->broadcast_channel_auto_allocated;
	card->yesde_id = yesde_id;
	/*
	 * Update yesde_id before generation to prevent anybody from using
	 * a stale yesde_id together with a current generation.
	 */
	smp_wmb();
	card->generation = generation;
	card->reset_jiffies = get_jiffies_64();
	card->bm_yesde_id  = 0xffff;
	card->bm_abdicate = bm_abdicate;
	fw_schedule_bm_work(card, 0);

	local_yesde = build_tree(card, self_ids, self_id_count);

	update_topology_map(card, self_ids, self_id_count);

	card->color++;

	if (local_yesde == NULL) {
		fw_err(card, "topology build failed\n");
		/* FIXME: We need to issue a bus reset in this case. */
	} else if (card->local_yesde == NULL) {
		card->local_yesde = local_yesde;
		for_each_fw_yesde(card, local_yesde, report_found_yesde);
	} else {
		update_tree(card, local_yesde);
	}

	spin_unlock_irqrestore(&card->lock, flags);
}
EXPORT_SYMBOL(fw_core_handle_bus_reset);
