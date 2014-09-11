/*
 * Thunderbolt Cactus Ridge driver - path/tunnel functionality
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 */

#include <linux/slab.h>
#include <linux/errno.h>

#include "tb.h"


static void tb_dump_hop(struct tb_port *port, struct tb_regs_hop *hop)
{
	tb_port_info(port, " Hop through port %d to hop %d (%s)\n",
		     hop->out_port, hop->next_hop,
		     hop->enable ? "enabled" : "disabled");
	tb_port_info(port, "  Weight: %d Priority: %d Credits: %d Drop: %d\n",
		     hop->weight, hop->priority,
		     hop->initial_credits, hop->drop_packages);
	tb_port_info(port, "   Counter enabled: %d Counter index: %d\n",
		     hop->counter_enable, hop->counter);
	tb_port_info(port, "  Flow Control (In/Eg): %d/%d Shared Buffer (In/Eg): %d/%d\n",
		     hop->ingress_fc, hop->egress_fc,
		     hop->ingress_shared_buffer, hop->egress_shared_buffer);
	tb_port_info(port, "  Unknown1: %#x Unknown2: %#x Unknown3: %#x\n",
		     hop->unknown1, hop->unknown2, hop->unknown3);
}

/**
 * tb_path_alloc() - allocate a thunderbolt path
 *
 * Return: Returns a tb_path on success or NULL on failure.
 */
struct tb_path *tb_path_alloc(struct tb *tb, int num_hops)
{
	struct tb_path *path = kzalloc(sizeof(*path), GFP_KERNEL);
	if (!path)
		return NULL;
	path->hops = kcalloc(num_hops, sizeof(*path->hops), GFP_KERNEL);
	if (!path->hops) {
		kfree(path);
		return NULL;
	}
	path->tb = tb;
	path->path_length = num_hops;
	return path;
}

/**
 * tb_path_free() - free a deactivated path
 */
void tb_path_free(struct tb_path *path)
{
	if (path->activated) {
		tb_WARN(path->tb, "trying to free an activated path\n")
		return;
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

static void __tb_path_deactivate_hops(struct tb_path *path, int first_hop)
{
	int i, res;
	struct tb_regs_hop hop = { };
	for (i = first_hop; i < path->path_length; i++) {
		res = tb_port_write(path->hops[i].in_port, &hop, TB_CFG_HOPS,
				    2 * path->hops[i].in_hop_index, 2);
		if (res)
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
	tb_info(path->tb,
		"deactivating path from %llx:%x to %llx:%x\n",
		tb_route(path->hops[0].in_port->sw),
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

	tb_info(path->tb,
		"activating path from %llx:%x to %llx:%x\n",
		tb_route(path->hops[0].in_port->sw),
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
		struct tb_regs_hop hop;

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
