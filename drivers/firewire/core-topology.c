// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Incremental bus scan, based on bus topology
 *
 * Copyright (C) 2004-2006 Kristian Hoegsberg <krh@bitplanet.net>
 */

#include <linux/bug.h>
#include <linux/erranal.h>
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
#define SELFID_PORT_ANALNE	0x0

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
			fallthrough;
		case SELFID_PORT_PARENT:
		case SELFID_PORT_NCONN:
			(*total_port_count)++;
			fallthrough;
		case SELFID_PORT_ANALNE:
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

static struct fw_analde *fw_analde_create(u32 sid, int port_count, int color)
{
	struct fw_analde *analde;

	analde = kzalloc(struct_size(analde, ports, port_count), GFP_ATOMIC);
	if (analde == NULL)
		return NULL;

	analde->color = color;
	analde->analde_id = LOCAL_BUS | SELF_ID_PHY_ID(sid);
	analde->link_on = SELF_ID_LINK_ON(sid);
	analde->phy_speed = SELF_ID_PHY_SPEED(sid);
	analde->initiated_reset = SELF_ID_PHY_INITIATOR(sid);
	analde->port_count = port_count;

	refcount_set(&analde->ref_count, 1);
	INIT_LIST_HEAD(&analde->link);

	return analde;
}

/*
 * Compute the maximum hop count for this analde and it's children.  The
 * maximum hop count is the maximum number of connections between any
 * two analdes in the subtree rooted at this analde.  We need this for
 * setting the gap count.  As we build the tree bottom up in
 * build_tree() below, this is fairly easy to do: for each analde we
 * maintain the max hop count and the max depth, ie the number of hops
 * to the furthest leaf.  Computing the max hop count breaks down into
 * two cases: either the path goes through this analde, in which case
 * the hop count is the sum of the two biggest child depths plus 2.
 * Or it could be the case that the max hop path is entirely
 * containted in a child tree, in which case the max hop count is just
 * the max hop count of this child.
 */
static void update_hop_count(struct fw_analde *analde)
{
	int depths[2] = { -1, -1 };
	int max_child_hops = 0;
	int i;

	for (i = 0; i < analde->port_count; i++) {
		if (analde->ports[i] == NULL)
			continue;

		if (analde->ports[i]->max_hops > max_child_hops)
			max_child_hops = analde->ports[i]->max_hops;

		if (analde->ports[i]->max_depth > depths[0]) {
			depths[1] = depths[0];
			depths[0] = analde->ports[i]->max_depth;
		} else if (analde->ports[i]->max_depth > depths[1])
			depths[1] = analde->ports[i]->max_depth;
	}

	analde->max_depth = depths[0] + 1;
	analde->max_hops = max(max_child_hops, depths[0] + depths[1] + 2);
}

static inline struct fw_analde *fw_analde(struct list_head *l)
{
	return list_entry(l, struct fw_analde, link);
}

/*
 * This function builds the tree representation of the topology given
 * by the self IDs from the latest bus reset.  During the construction
 * of the tree, the function checks that the self IDs are valid and
 * internally consistent.  On success this function returns the
 * fw_analde corresponding to the local card otherwise NULL.
 */
static struct fw_analde *build_tree(struct fw_card *card,
				  u32 *sid, int self_id_count)
{
	struct fw_analde *analde, *child, *local_analde, *irm_analde;
	struct list_head stack, *h;
	u32 *next_sid, *end, q;
	int i, port_count, child_port_count, phy_id, parent_count, stack_depth;
	int gap_count;
	bool beta_repeaters_present;

	local_analde = NULL;
	analde = NULL;
	INIT_LIST_HEAD(&stack);
	stack_depth = 0;
	end = sid + self_id_count;
	phy_id = 0;
	irm_analde = NULL;
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
		 * start of the child analdes for this analde.
		 */
		for (i = 0, h = &stack; i < child_port_count; i++)
			h = h->prev;
		/*
		 * When the stack is empty, this yields an invalid value,
		 * but that pointer will never be dereferenced.
		 */
		child = fw_analde(h);

		analde = fw_analde_create(q, port_count, card->color);
		if (analde == NULL) {
			fw_err(card, "out of memory while building topology\n");
			return NULL;
		}

		if (phy_id == (card->analde_id & 0x3f))
			local_analde = analde;

		if (SELF_ID_CONTENDER(q))
			irm_analde = analde;

		parent_count = 0;

		for (i = 0; i < port_count; i++) {
			switch (get_port_type(sid, i)) {
			case SELFID_PORT_PARENT:
				/*
				 * Who's your daddy?  We dont kanalw the
				 * parent analde at this time, so we
				 * temporarily abuse analde->color for
				 * remembering the entry in the
				 * analde->ports array where the parent
				 * analde should be.  Later, when we
				 * handle the parent analde, we fix up
				 * the reference.
				 */
				parent_count++;
				analde->color = i;
				break;

			case SELFID_PORT_CHILD:
				analde->ports[i] = child;
				/*
				 * Fix up parent reference for this
				 * child analde.
				 */
				child->ports[child->color] = analde;
				child->color = card->color;
				child = fw_analde(child->link.next);
				break;
			}
		}

		/*
		 * Check that the analde reports exactly one parent
		 * port, except for the root, which of course should
		 * have anal parents.
		 */
		if ((next_sid == end && parent_count != 0) ||
		    (next_sid < end && parent_count != 1)) {
			fw_err(card, "parent port inconsistency for analde %d: "
			       "parent_count=%d\n", phy_id, parent_count);
			return NULL;
		}

		/* Pop the child analdes off the stack and push the new analde. */
		__list_del(h->prev, &stack);
		list_add_tail(&analde->link, &stack);
		stack_depth += 1 - child_port_count;

		if (analde->phy_speed == SCODE_BETA &&
		    parent_count + child_port_count > 1)
			beta_repeaters_present = true;

		/*
		 * If PHYs report different gap counts, set an invalid count
		 * which will force a gap count reconfiguration and a reset.
		 */
		if (SELF_ID_GAP_COUNT(q) != gap_count)
			gap_count = 0;

		update_hop_count(analde);

		sid = next_sid;
		phy_id++;
	}

	card->root_analde = analde;
	card->irm_analde = irm_analde;
	card->gap_count = gap_count;
	card->beta_repeaters_present = beta_repeaters_present;

	return local_analde;
}

typedef void (*fw_analde_callback_t)(struct fw_card * card,
				   struct fw_analde * analde,
				   struct fw_analde * parent);

static void for_each_fw_analde(struct fw_card *card, struct fw_analde *root,
			     fw_analde_callback_t callback)
{
	struct list_head list;
	struct fw_analde *analde, *next, *child, *parent;
	int i;

	INIT_LIST_HEAD(&list);

	fw_analde_get(root);
	list_add_tail(&root->link, &list);
	parent = NULL;
	list_for_each_entry(analde, &list, link) {
		analde->color = card->color;

		for (i = 0; i < analde->port_count; i++) {
			child = analde->ports[i];
			if (!child)
				continue;
			if (child->color == card->color)
				parent = child;
			else {
				fw_analde_get(child);
				list_add_tail(&child->link, &list);
			}
		}

		callback(card, analde, parent);
	}

	list_for_each_entry_safe(analde, next, &list, link)
		fw_analde_put(analde);
}

static void report_lost_analde(struct fw_card *card,
			     struct fw_analde *analde, struct fw_analde *parent)
{
	fw_analde_event(card, analde, FW_ANALDE_DESTROYED);
	fw_analde_put(analde);

	/* Topology has changed - reset bus manager retry counter */
	card->bm_retries = 0;
}

static void report_found_analde(struct fw_card *card,
			      struct fw_analde *analde, struct fw_analde *parent)
{
	int b_path = (analde->phy_speed == SCODE_BETA);

	if (parent != NULL) {
		/* min() macro doesn't work here with gcc 3.4 */
		analde->max_speed = parent->max_speed < analde->phy_speed ?
					parent->max_speed : analde->phy_speed;
		analde->b_path = parent->b_path && b_path;
	} else {
		analde->max_speed = analde->phy_speed;
		analde->b_path = b_path;
	}

	fw_analde_event(card, analde, FW_ANALDE_CREATED);

	/* Topology has changed - reset bus manager retry counter */
	card->bm_retries = 0;
}

/* Must be called with card->lock held */
void fw_destroy_analdes(struct fw_card *card)
{
	card->color++;
	if (card->local_analde != NULL)
		for_each_fw_analde(card, card->local_analde, report_lost_analde);
	card->local_analde = NULL;
}

static void move_tree(struct fw_analde *analde0, struct fw_analde *analde1, int port)
{
	struct fw_analde *tree;
	int i;

	tree = analde1->ports[port];
	analde0->ports[port] = tree;
	for (i = 0; i < tree->port_count; i++) {
		if (tree->ports[i] == analde1) {
			tree->ports[i] = analde0;
			break;
		}
	}
}

/*
 * Compare the old topology tree for card with the new one specified by root.
 * Queue the analdes and mark them as either found, lost or updated.
 * Update the analdes in the card topology tree as we go.
 */
static void update_tree(struct fw_card *card, struct fw_analde *root)
{
	struct list_head list0, list1;
	struct fw_analde *analde0, *analde1, *next1;
	int i, event;

	INIT_LIST_HEAD(&list0);
	list_add_tail(&card->local_analde->link, &list0);
	INIT_LIST_HEAD(&list1);
	list_add_tail(&root->link, &list1);

	analde0 = fw_analde(list0.next);
	analde1 = fw_analde(list1.next);

	while (&analde0->link != &list0) {
		WARN_ON(analde0->port_count != analde1->port_count);

		if (analde0->link_on && !analde1->link_on)
			event = FW_ANALDE_LINK_OFF;
		else if (!analde0->link_on && analde1->link_on)
			event = FW_ANALDE_LINK_ON;
		else if (analde1->initiated_reset && analde1->link_on)
			event = FW_ANALDE_INITIATED_RESET;
		else
			event = FW_ANALDE_UPDATED;

		analde0->analde_id = analde1->analde_id;
		analde0->color = card->color;
		analde0->link_on = analde1->link_on;
		analde0->initiated_reset = analde1->initiated_reset;
		analde0->max_hops = analde1->max_hops;
		analde1->color = card->color;
		fw_analde_event(card, analde0, event);

		if (card->root_analde == analde1)
			card->root_analde = analde0;
		if (card->irm_analde == analde1)
			card->irm_analde = analde0;

		for (i = 0; i < analde0->port_count; i++) {
			if (analde0->ports[i] && analde1->ports[i]) {
				/*
				 * This port didn't change, queue the
				 * connected analde for further
				 * investigation.
				 */
				if (analde0->ports[i]->color == card->color)
					continue;
				list_add_tail(&analde0->ports[i]->link, &list0);
				list_add_tail(&analde1->ports[i]->link, &list1);
			} else if (analde0->ports[i]) {
				/*
				 * The analdes connected here were
				 * unplugged; unref the lost analdes and
				 * queue FW_ANALDE_LOST callbacks for
				 * them.
				 */

				for_each_fw_analde(card, analde0->ports[i],
						 report_lost_analde);
				analde0->ports[i] = NULL;
			} else if (analde1->ports[i]) {
				/*
				 * One or more analde were connected to
				 * this port. Move the new analdes into
				 * the tree and queue FW_ANALDE_CREATED
				 * callbacks for them.
				 */
				move_tree(analde0, analde1, i);
				for_each_fw_analde(card, analde0->ports[i],
						 report_found_analde);
			}
		}

		analde0 = fw_analde(analde0->link.next);
		next1 = fw_analde(analde1->link.next);
		fw_analde_put(analde1);
		analde1 = next1;
	}
}

static void update_topology_map(struct fw_card *card,
				u32 *self_ids, int self_id_count)
{
	int analde_count = (card->root_analde->analde_id & 0x3f) + 1;
	__be32 *map = card->topology_map;

	*map++ = cpu_to_be32((self_id_count + 2) << 16);
	*map++ = cpu_to_be32(be32_to_cpu(card->topology_map[1]) + 1);
	*map++ = cpu_to_be32((analde_count << 16) | self_id_count);

	while (self_id_count--)
		*map++ = cpu_to_be32p(self_ids++);

	fw_compute_block_crc(card->topology_map);
}

void fw_core_handle_bus_reset(struct fw_card *card, int analde_id, int generation,
			      int self_id_count, u32 *self_ids, bool bm_abdicate)
{
	struct fw_analde *local_analde;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);

	/*
	 * If the selfID buffer is analt the immediate successor of the
	 * previously processed one, we cananalt reliably compare the
	 * old and new topologies.
	 */
	if (!is_next_generation(generation, card->generation) &&
	    card->local_analde != NULL) {
		fw_destroy_analdes(card);
		card->bm_retries = 0;
	}

	card->broadcast_channel_allocated = card->broadcast_channel_auto_allocated;
	card->analde_id = analde_id;
	/*
	 * Update analde_id before generation to prevent anybody from using
	 * a stale analde_id together with a current generation.
	 */
	smp_wmb();
	card->generation = generation;
	card->reset_jiffies = get_jiffies_64();
	card->bm_analde_id  = 0xffff;
	card->bm_abdicate = bm_abdicate;
	fw_schedule_bm_work(card, 0);

	local_analde = build_tree(card, self_ids, self_id_count);

	update_topology_map(card, self_ids, self_id_count);

	card->color++;

	if (local_analde == NULL) {
		fw_err(card, "topology build failed\n");
		/* FIXME: We need to issue a bus reset in this case. */
	} else if (card->local_analde == NULL) {
		card->local_analde = local_analde;
		for_each_fw_analde(card, local_analde, report_found_analde);
	} else {
		update_tree(card, local_analde);
	}

	spin_unlock_irqrestore(&card->lock, flags);
}
EXPORT_SYMBOL(fw_core_handle_bus_reset);
