/*
 * Copyright (C) 2009-2010 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#include "main.h"
#include "gateway_client.h"
#include "gateway_common.h"
#include "hard-interface.h"

static void gw_node_free_ref(struct kref *refcount)
{
	struct gw_node *gw_node;

	gw_node = container_of(refcount, struct gw_node, refcount);
	kfree(gw_node);
}

static void gw_node_free_rcu(struct rcu_head *rcu)
{
	struct gw_node *gw_node;

	gw_node = container_of(rcu, struct gw_node, rcu);
	kref_put(&gw_node->refcount, gw_node_free_ref);
}

void gw_deselect(struct bat_priv *bat_priv)
{
	struct gw_node *gw_node = bat_priv->curr_gw;

	bat_priv->curr_gw = NULL;

	if (gw_node)
		kref_put(&gw_node->refcount, gw_node_free_ref);
}

static struct gw_node *gw_select(struct bat_priv *bat_priv,
			  struct gw_node *new_gw_node)
{
	struct gw_node *curr_gw_node = bat_priv->curr_gw;

	if (new_gw_node)
		kref_get(&new_gw_node->refcount);

	bat_priv->curr_gw = new_gw_node;
	return curr_gw_node;
}

void gw_election(struct bat_priv *bat_priv)
{
	struct hlist_node *node;
	struct gw_node *gw_node, *curr_gw_tmp = NULL, *old_gw_node = NULL;
	uint8_t max_tq = 0;
	uint32_t max_gw_factor = 0, tmp_gw_factor = 0;
	int down, up;

	/**
	 * The batman daemon checks here if we already passed a full originator
	 * cycle in order to make sure we don't choose the first gateway we
	 * hear about. This check is based on the daemon's uptime which we
	 * don't have.
	 **/
	if (atomic_read(&bat_priv->gw_mode) != GW_MODE_CLIENT)
		return;

	if (bat_priv->curr_gw)
		return;

	rcu_read_lock();
	if (hlist_empty(&bat_priv->gw_list)) {
		rcu_read_unlock();

		if (bat_priv->curr_gw) {
			bat_dbg(DBG_BATMAN, bat_priv,
				"Removing selected gateway - "
				"no gateway in range\n");
			gw_deselect(bat_priv);
		}

		return;
	}

	hlist_for_each_entry_rcu(gw_node, node, &bat_priv->gw_list, list) {
		if (!gw_node->orig_node->router)
			continue;

		if (gw_node->deleted)
			continue;

		switch (atomic_read(&bat_priv->gw_sel_class)) {
		case 1: /* fast connection */
			gw_bandwidth_to_kbit(gw_node->orig_node->gw_flags,
					     &down, &up);

			tmp_gw_factor = (gw_node->orig_node->router->tq_avg *
					 gw_node->orig_node->router->tq_avg *
					 down * 100 * 100) /
					 (TQ_LOCAL_WINDOW_SIZE *
					 TQ_LOCAL_WINDOW_SIZE * 64);

			if ((tmp_gw_factor > max_gw_factor) ||
			    ((tmp_gw_factor == max_gw_factor) &&
			     (gw_node->orig_node->router->tq_avg > max_tq)))
				curr_gw_tmp = gw_node;
			break;

		default: /**
			  * 2:  stable connection (use best statistic)
			  * 3:  fast-switch (use best statistic but change as
			  *     soon as a better gateway appears)
			  * XX: late-switch (use best statistic but change as
			  *     soon as a better gateway appears which has
			  *     $routing_class more tq points)
			  **/
			if (gw_node->orig_node->router->tq_avg > max_tq)
				curr_gw_tmp = gw_node;
			break;
		}

		if (gw_node->orig_node->router->tq_avg > max_tq)
			max_tq = gw_node->orig_node->router->tq_avg;

		if (tmp_gw_factor > max_gw_factor)
			max_gw_factor = tmp_gw_factor;
	}

	if (bat_priv->curr_gw != curr_gw_tmp) {
		if ((bat_priv->curr_gw) && (!curr_gw_tmp))
			bat_dbg(DBG_BATMAN, bat_priv,
				"Removing selected gateway - "
				"no gateway in range\n");
		else if ((!bat_priv->curr_gw) && (curr_gw_tmp))
			bat_dbg(DBG_BATMAN, bat_priv,
				"Adding route to gateway %pM "
				"(gw_flags: %i, tq: %i)\n",
				curr_gw_tmp->orig_node->orig,
				curr_gw_tmp->orig_node->gw_flags,
				curr_gw_tmp->orig_node->router->tq_avg);
		else
			bat_dbg(DBG_BATMAN, bat_priv,
				"Changing route to gateway %pM "
				"(gw_flags: %i, tq: %i)\n",
				curr_gw_tmp->orig_node->orig,
				curr_gw_tmp->orig_node->gw_flags,
				curr_gw_tmp->orig_node->router->tq_avg);

		old_gw_node = gw_select(bat_priv, curr_gw_tmp);
	}

	rcu_read_unlock();

	/* the kfree() has to be outside of the rcu lock */
	if (old_gw_node)
		kref_put(&old_gw_node->refcount, gw_node_free_ref);
}

void gw_check_election(struct bat_priv *bat_priv, struct orig_node *orig_node)
{
	struct gw_node *curr_gateway_tmp = bat_priv->curr_gw;
	uint8_t gw_tq_avg, orig_tq_avg;

	if (!curr_gateway_tmp)
		return;

	if (!curr_gateway_tmp->orig_node)
		goto deselect;

	if (!curr_gateway_tmp->orig_node->router)
		goto deselect;

	/* this node already is the gateway */
	if (curr_gateway_tmp->orig_node == orig_node)
		return;

	if (!orig_node->router)
		return;

	gw_tq_avg = curr_gateway_tmp->orig_node->router->tq_avg;
	orig_tq_avg = orig_node->router->tq_avg;

	/* the TQ value has to be better */
	if (orig_tq_avg < gw_tq_avg)
		return;

	/**
	 * if the routing class is greater than 3 the value tells us how much
	 * greater the TQ value of the new gateway must be
	 **/
	if ((atomic_read(&bat_priv->gw_sel_class) > 3) &&
	    (orig_tq_avg - gw_tq_avg < atomic_read(&bat_priv->gw_sel_class)))
		return;

	bat_dbg(DBG_BATMAN, bat_priv,
		"Restarting gateway selection: better gateway found (tq curr: "
		"%i, tq new: %i)\n",
		gw_tq_avg, orig_tq_avg);

deselect:
	gw_deselect(bat_priv);
}

static void gw_node_add(struct bat_priv *bat_priv,
			struct orig_node *orig_node, uint8_t new_gwflags)
{
	struct gw_node *gw_node;
	int down, up;

	gw_node = kmalloc(sizeof(struct gw_node), GFP_ATOMIC);
	if (!gw_node)
		return;

	memset(gw_node, 0, sizeof(struct gw_node));
	INIT_HLIST_NODE(&gw_node->list);
	gw_node->orig_node = orig_node;
	kref_init(&gw_node->refcount);

	spin_lock_bh(&bat_priv->gw_list_lock);
	hlist_add_head_rcu(&gw_node->list, &bat_priv->gw_list);
	spin_unlock_bh(&bat_priv->gw_list_lock);

	gw_bandwidth_to_kbit(new_gwflags, &down, &up);
	bat_dbg(DBG_BATMAN, bat_priv,
		"Found new gateway %pM -> gw_class: %i - %i%s/%i%s\n",
		orig_node->orig, new_gwflags,
		(down > 2048 ? down / 1024 : down),
		(down > 2048 ? "MBit" : "KBit"),
		(up > 2048 ? up / 1024 : up),
		(up > 2048 ? "MBit" : "KBit"));
}

void gw_node_update(struct bat_priv *bat_priv,
		    struct orig_node *orig_node, uint8_t new_gwflags)
{
	struct hlist_node *node;
	struct gw_node *gw_node;

	rcu_read_lock();
	hlist_for_each_entry_rcu(gw_node, node, &bat_priv->gw_list, list) {
		if (gw_node->orig_node != orig_node)
			continue;

		bat_dbg(DBG_BATMAN, bat_priv,
			"Gateway class of originator %pM changed from "
			"%i to %i\n",
			orig_node->orig, gw_node->orig_node->gw_flags,
			new_gwflags);

		gw_node->deleted = 0;

		if (new_gwflags == 0) {
			gw_node->deleted = jiffies;
			bat_dbg(DBG_BATMAN, bat_priv,
				"Gateway %pM removed from gateway list\n",
				orig_node->orig);

			if (gw_node == bat_priv->curr_gw) {
				rcu_read_unlock();
				gw_deselect(bat_priv);
				return;
			}
		}

		rcu_read_unlock();
		return;
	}
	rcu_read_unlock();

	if (new_gwflags == 0)
		return;

	gw_node_add(bat_priv, orig_node, new_gwflags);
}

void gw_node_delete(struct bat_priv *bat_priv, struct orig_node *orig_node)
{
	return gw_node_update(bat_priv, orig_node, 0);
}

void gw_node_purge(struct bat_priv *bat_priv)
{
	struct gw_node *gw_node;
	struct hlist_node *node, *node_tmp;
	unsigned long timeout = 2 * PURGE_TIMEOUT * HZ;

	spin_lock_bh(&bat_priv->gw_list_lock);

	hlist_for_each_entry_safe(gw_node, node, node_tmp,
				  &bat_priv->gw_list, list) {
		if (((!gw_node->deleted) ||
		     (time_before(jiffies, gw_node->deleted + timeout))) &&
		    atomic_read(&bat_priv->mesh_state) == MESH_ACTIVE)
			continue;

		if (bat_priv->curr_gw == gw_node)
			gw_deselect(bat_priv);

		hlist_del_rcu(&gw_node->list);
		call_rcu(&gw_node->rcu, gw_node_free_rcu);
	}


	spin_unlock_bh(&bat_priv->gw_list_lock);
}

static int _write_buffer_text(struct bat_priv *bat_priv,
			      struct seq_file *seq, struct gw_node *gw_node)
{
	int down, up;

	gw_bandwidth_to_kbit(gw_node->orig_node->gw_flags, &down, &up);

	return seq_printf(seq, "%s %pM (%3i) %pM [%10s]: %3i - %i%s/%i%s\n",
		       (bat_priv->curr_gw == gw_node ? "=>" : "  "),
		       gw_node->orig_node->orig,
		       gw_node->orig_node->router->tq_avg,
		       gw_node->orig_node->router->addr,
		       gw_node->orig_node->router->if_incoming->net_dev->name,
		       gw_node->orig_node->gw_flags,
		       (down > 2048 ? down / 1024 : down),
		       (down > 2048 ? "MBit" : "KBit"),
		       (up > 2048 ? up / 1024 : up),
		       (up > 2048 ? "MBit" : "KBit"));
}

int gw_client_seq_print_text(struct seq_file *seq, void *offset)
{
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	struct gw_node *gw_node;
	struct hlist_node *node;
	int gw_count = 0;

	if (!bat_priv->primary_if) {

		return seq_printf(seq, "BATMAN mesh %s disabled - please "
				  "specify interfaces to enable it\n",
				  net_dev->name);
	}

	if (bat_priv->primary_if->if_status != IF_ACTIVE) {

		return seq_printf(seq, "BATMAN mesh %s disabled - "
				       "primary interface not active\n",
				       net_dev->name);
	}

	seq_printf(seq, "      %-12s (%s/%i) %17s [%10s]: gw_class ... "
		   "[B.A.T.M.A.N. adv %s%s, MainIF/MAC: %s/%pM (%s)]\n",
		   "Gateway", "#", TQ_MAX_VALUE, "Nexthop",
		   "outgoingIF", SOURCE_VERSION, REVISION_VERSION_STR,
		   bat_priv->primary_if->net_dev->name,
		   bat_priv->primary_if->net_dev->dev_addr, net_dev->name);

	rcu_read_lock();
	hlist_for_each_entry_rcu(gw_node, node, &bat_priv->gw_list, list) {
		if (gw_node->deleted)
			continue;

		if (!gw_node->orig_node->router)
			continue;

		_write_buffer_text(bat_priv, seq, gw_node);
		gw_count++;
	}
	rcu_read_unlock();

	if (gw_count == 0)
		seq_printf(seq, "No gateways in range ...\n");

	return 0;
}
