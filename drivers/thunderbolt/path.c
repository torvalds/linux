// SPDX-License-Identifier: GPL-2.0
/*
 * Thunderbolt Cactus Ridge driver - path/tunnel functionality
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 */

#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/ktime.h>

#include "tb.h"


static void tb_dump_hop(struct tb_port *port, struct tb_regs_hop *hop)
{
	tb_port_dbg(port, " Hop through port %d to hop %d (%s)\n",
		    hop->out_port, hop->next_hop,
		    hop->enable ? "enabled" : "disabled");
	tb_port_dbg(port, "  Weight: %d Priority: %d Credits: %d Drop: %d\n",
		    hop->weight, hop->priority,
		    hop->initial_credits, hop->drop_packages);
	tb_port_dbg(port, "   Counter enabled: %d Counter index: %d\n",
		    hop->counter_enable, hop->counter);
	tb_port_dbg(port, "  Flow Control (In/Eg): %d/%d Shared Buffer (In/Eg): %d/%d\n",
		    hop->ingress_fc, hop->egress_fc,
		    hop->ingress_shared_buffer, hop->egress_shared_buffer);
	tb_port_dbg(port, "  Unknown1: %#x Unknown2: %#x Unknown3: %#x\n",
		    hop->unknown1, hop->unknown2, hop->unknown3);
}

/**
 * tb_path_alloc() - allocate a thunderbolt path between two ports
 * @tb: Domain pointer
 * @src: Source port of the path
 * @src_hopid: HopID used for the first ingress port in the path
 * @dst: Destination port of the path
 * @dst_hopid: HopID used for the last egress port in the path
 * @link_nr: Preferred link if there are dual links on the path
 * @name: Name of the path
 *
 * Creates path between two ports starting with given @src_hopid. Reserves
 * HopIDs for each port (they can be different from @src_hopid depending on
 * how many HopIDs each port already have reserved). If there are dual
 * links on the path, prioritizes using @link_nr.
 *
 * Return: Returns a tb_path on success or NULL on failure.
 */
struct tb_path *tb_path_alloc(struct tb *tb, struct tb_port *src, int src_hopid,
			      struct tb_port *dst, int dst_hopid, int link_nr,
			      const char *name)
{
	struct tb_port *in_port, *out_port;
	int in_hopid, out_hopid;
	struct tb_path *path;
	size_t num_hops;
	int i, ret;

	path = kzalloc(sizeof(*path), GFP_KERNEL);
	if (!path)
		return NULL;

	/*
	 * Number of hops on a path is the distance between the two
	 * switches plus the source adapter port.
	 */
	num_hops = abs(tb_route_length(tb_route(src->sw)) -
		       tb_route_length(tb_route(dst->sw))) + 1;

	path->hops = kcalloc(num_hops, sizeof(*path->hops), GFP_KERNEL);
	if (!path->hops) {
		kfree(path);
		return NULL;
	}

	in_hopid = src_hopid;
	out_port = NULL;

	for (i = 0; i < num_hops; i++) {
		in_port = tb_next_port_on_path(src, dst, out_port);
		if (!in_port)
			goto err;

		if (in_port->dual_link_port && in_port->link_nr != link_nr)
			in_port = in_port->dual_link_port;

		ret = tb_port_alloc_in_hopid(in_port, in_hopid, in_hopid);
		if (ret < 0)
			goto err;
		in_hopid = ret;

		out_port = tb_next_port_on_path(src, dst, in_port);
		if (!out_port)
			goto err;

		if (out_port->dual_link_port && out_port->link_nr != link_nr)
			out_port = out_port->dual_link_port;

		if (i == num_hops - 1)
			ret = tb_port_alloc_out_hopid(out_port, dst_hopid,
						      dst_hopid);
		else
			ret = tb_port_alloc_out_hopid(out_port, -1, -1);

		if (ret < 0)
			goto err;
		out_hopid = ret;

		path->hops[i].in_hop_index = in_hopid;
		path->hops[i].in_port = in_port;
		path->hops[i].in_counter_index = -1;
		path->hops[i].out_port = out_port;
		path->hops[i].next_hop_index = out_hopid;

		in_hopid = out_hopid;
	}

	path->tb = tb;
	path->path_length = num_hops;
	path->name = name;

	return path;

err:
	tb_path_free(path);
	return NULL;
}

/**
 * tb_path_free() - free a deactivated path
 */
void tb_path_free(struct tb_path *path)
{
	int i;

	if (path->activated) {
		tb_WARN(path->tb, "trying to free an activated path\n")
		return;
	}

	for (i = 0; i < path->path_length; i++) {
		const struct tb_path_hop *hop = &path->hops[i];

		if (hop->in_port)
			tb_port_release_in_hopid(hop->in_port,
						 hop->in_hop_index);
		if (hop->out_port)
			tb_port_release_out_hopid(hop->out_port,
						  hop->next_hop_index);
	}

	kfree(path->hops);
	kfree(path);
}

static void __tb_path_deallocate_nfc(struct tb_path *path, int first_hop)
{
	int i, res;
	for (i = first_hop; i < path->path_length; i++) {
		res = tb_port_add_nfc_credits(path->hops[i].in_port,
					      -path->nfc_credits);
		if (res)
			tb_port_warn(path->hops[i].in_port,
				     "nfc credits deallocation failed for hop %d\n",
				     i);
	}
}

static int __tb_path_deactivate_hop(struct tb_port *port, int hop_index)
{
	struct tb_regs_hop hop;
	ktime_t timeout;
	int ret;

	/* Disable the path */
	ret = tb_port_read(port, &hop, TB_CFG_HOPS, 2 * hop_index, 2);
	if (ret)
		return ret;

	/* Already disabled */
	if (!hop.enable)
		return 0;

	hop.enable = 0;

	ret = tb_port_write(port, &hop, TB_CFG_HOPS, 2 * hop_index, 2);
	if (ret)
		return ret;

	/* Wait until it is drained */
	timeout = ktime_add_ms(ktime_get(), 500);
	do {
		ret = tb_port_read(port, &hop, TB_CFG_HOPS, 2 * hop_index, 2);
		if (ret)
			return ret;

		if (!hop.pending)
			return 0;

		usleep_range(10, 20);
	} while (ktime_before(ktime_get(), timeout));

	return -ETIMEDOUT;
}

static void __tb_path_deactivate_hops(struct tb_path *path, int first_hop)
{
	int i, res;

	for (i = first_hop; i < path->path_length; i++) {
		res = __tb_path_deactivate_hop(path->hops[i].in_port,
					       path->hops[i].in_hop_index);
		if (res && res != -ENODEV)
			tb_port_warn(path->hops[i].in_port,
				     "hop deactivation failed for hop %d, index %d\n",
				     i, path->hops[i].in_hop_index);
	}
}

void tb_path_deactivate(struct tb_path *path)
{
	if (!path->activated) {
		tb_WARN(path->tb, "trying to deactivate an inactive path\n");
		return;
	}
	tb_dbg(path->tb,
	       "deactivating %s path from %llx:%x to %llx:%x\n",
	       path->name, tb_route(path->hops[0].in_port->sw),
	       path->hops[0].in_port->port,
	       tb_route(path->hops[path->path_length - 1].out_port->sw),
	       path->hops[path->path_length - 1].out_port->port);
	__tb_path_deactivate_hops(path, 0);
	__tb_path_deallocate_nfc(path, 0);
	path->activated = false;
}

/**
 * tb_path_activate() - activate a path
 *
 * Activate a path starting with the last hop and iterating backwards. The
 * caller must fill path->hops before calling tb_path_activate().
 *
 * Return: Returns 0 on success or an error code on failure.
 */
int tb_path_activate(struct tb_path *path)
{
	int i, res;
	enum tb_path_port out_mask, in_mask;
	if (path->activated) {
		tb_WARN(path->tb, "trying to activate already activated path\n");
		return -EINVAL;
	}

	tb_dbg(path->tb,
	       "activating %s path from %llx:%x to %llx:%x\n",
	       path->name, tb_route(path->hops[0].in_port->sw),
	       path->hops[0].in_port->port,
	       tb_route(path->hops[path->path_length - 1].out_port->sw),
	       path->hops[path->path_length - 1].out_port->port);

	/* Clear counters. */
	for (i = path->path_length - 1; i >= 0; i--) {
		if (path->hops[i].in_counter_index == -1)
			continue;
		res = tb_port_clear_counter(path->hops[i].in_port,
					    path->hops[i].in_counter_index);
		if (res)
			goto err;
	}

	/* Add non flow controlled credits. */
	for (i = path->path_length - 1; i >= 0; i--) {
		res = tb_port_add_nfc_credits(path->hops[i].in_port,
					      path->nfc_credits);
		if (res) {
			__tb_path_deallocate_nfc(path, i);
			goto err;
		}
	}

	/* Activate hops. */
	for (i = path->path_length - 1; i >= 0; i--) {
		struct tb_regs_hop hop = { 0 };

		/*
		 * We do (currently) not tear down paths setup by the firmeware.
		 * If a firmware device is unplugged and plugged in again then
		 * it can happen that we reuse some of the hops from the (now
		 * defunct) firmeware path. This causes the hotplug operation to
		 * fail (the pci device does not show up). Clearing the hop
		 * before overwriting it fixes the problem.
		 *
		 * Should be removed once we discover and tear down firmeware
		 * paths.
		 */
		res = tb_port_write(path->hops[i].in_port, &hop, TB_CFG_HOPS,
				    2 * path->hops[i].in_hop_index, 2);
		if (res) {
			__tb_path_deactivate_hops(path, i);
			__tb_path_deallocate_nfc(path, 0);
			goto err;
		}

		/* dword 0 */
		hop.next_hop = path->hops[i].next_hop_index;
		hop.out_port = path->hops[i].out_port->port;
		/* TODO: figure out why these are good values */
		hop.initial_credits = (i == path->path_length - 1) ? 16 : 7;
		hop.unknown1 = 0;
		hop.enable = 1;

		/* dword 1 */
		out_mask = (i == path->path_length - 1) ?
				TB_PATH_DESTINATION : TB_PATH_INTERNAL;
		in_mask = (i == 0) ? TB_PATH_SOURCE : TB_PATH_INTERNAL;
		hop.weight = path->weight;
		hop.unknown2 = 0;
		hop.priority = path->priority;
		hop.drop_packages = path->drop_packages;
		hop.counter = path->hops[i].in_counter_index;
		hop.counter_enable = path->hops[i].in_counter_index != -1;
		hop.ingress_fc = path->ingress_fc_enable & in_mask;
		hop.egress_fc = path->egress_fc_enable & out_mask;
		hop.ingress_shared_buffer = path->ingress_shared_buffer
					    & in_mask;
		hop.egress_shared_buffer = path->egress_shared_buffer
					    & out_mask;
		hop.unknown3 = 0;

		tb_port_info(path->hops[i].in_port, "Writing hop %d, index %d",
			     i, path->hops[i].in_hop_index);
		tb_dump_hop(path->hops[i].in_port, &hop);
		res = tb_port_write(path->hops[i].in_port, &hop, TB_CFG_HOPS,
				    2 * path->hops[i].in_hop_index, 2);
		if (res) {
			__tb_path_deactivate_hops(path, i);
			__tb_path_deallocate_nfc(path, 0);
			goto err;
		}
	}
	path->activated = true;
	tb_info(path->tb, "path activation complete\n");
	return 0;
err:
	tb_WARN(path->tb, "path activation failed\n");
	return res;
}

/**
 * tb_path_is_invalid() - check whether any ports on the path are invalid
 *
 * Return: Returns true if the path is invalid, false otherwise.
 */
bool tb_path_is_invalid(struct tb_path *path)
{
	int i = 0;
	for (i = 0; i < path->path_length; i++) {
		if (path->hops[i].in_port->sw->is_unplugged)
			return true;
		if (path->hops[i].out_port->sw->is_unplugged)
			return true;
	}
	return false;
}
