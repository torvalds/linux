// SPDX-License-Identifier: GPL-2.0
/*
 * Thunderbolt driver - path/tunnel functionality
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2019, Intel Corporation
 */

#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/ktime.h>

#include "tb.h"

static void tb_dump_hop(const struct tb_path_hop *hop, const struct tb_regs_hop *regs)
{
	const struct tb_port *port = hop->in_port;

	tb_port_dbg(port, " In HopID: %d => Out port: %d Out HopID: %d\n",
		    hop->in_hop_index, regs->out_port, regs->next_hop);
	tb_port_dbg(port, "  Weight: %d Priority: %d Credits: %d Drop: %d\n",
		    regs->weight, regs->priority,
		    regs->initial_credits, regs->drop_packages);
	tb_port_dbg(port, "   Counter enabled: %d Counter index: %d\n",
		    regs->counter_enable, regs->counter);
	tb_port_dbg(port, "  Flow Control (In/Eg): %d/%d Shared Buffer (In/Eg): %d/%d\n",
		    regs->ingress_fc, regs->egress_fc,
		    regs->ingress_shared_buffer, regs->egress_shared_buffer);
	tb_port_dbg(port, "  Unknown1: %#x Unknown2: %#x Unknown3: %#x\n",
		    regs->unknown1, regs->unknown2, regs->unknown3);
}

static struct tb_port *tb_path_find_dst_port(struct tb_port *src, int src_hopid,
					     int dst_hopid)
{
	struct tb_port *port, *out_port = NULL;
	struct tb_regs_hop hop;
	struct tb_switch *sw;
	int i, ret, hopid;

	hopid = src_hopid;
	port = src;

	for (i = 0; port && i < TB_PATH_MAX_HOPS; i++) {
		sw = port->sw;

		ret = tb_port_read(port, &hop, TB_CFG_HOPS, 2 * hopid, 2);
		if (ret) {
			tb_port_warn(port, "failed to read path at %d\n", hopid);
			return NULL;
		}

		if (!hop.enable)
			return NULL;

		out_port = &sw->ports[hop.out_port];
		hopid = hop.next_hop;
		port = out_port->remote;
	}

	return out_port && hopid == dst_hopid ? out_port : NULL;
}

static int tb_path_find_src_hopid(struct tb_port *src,
	const struct tb_port *dst, int dst_hopid)
{
	struct tb_port *out;
	int i;

	for (i = TB_PATH_MIN_HOPID; i <= src->config.max_in_hop_id; i++) {
		out = tb_path_find_dst_port(src, i, dst_hopid);
		if (out == dst)
			return i;
	}

	return 0;
}

/**
 * tb_path_discover() - Discover a path
 * @src: First input port of a path
 * @src_hopid: Starting HopID of a path (%-1 if don't care)
 * @dst: Expected destination port of the path (%NULL if don't care)
 * @dst_hopid: HopID to the @dst (%-1 if don't care)
 * @last: Last port is filled here if not %NULL
 * @name: Name of the path
 *
 * Follows a path starting from @src and @src_hopid to the last output
 * port of the path. Allocates HopIDs for the visited ports. Call
 * tb_path_free() to release the path and allocated HopIDs when the path
 * is not needed anymore.
 *
 * Note function discovers also incomplete paths so caller should check
 * that the @dst port is the expected one. If it is not, the path can be
 * cleaned up by calling tb_path_deactivate() before tb_path_free().
 *
 * Return: Discovered path on success, %NULL in case of failure
 */
struct tb_path *tb_path_discover(struct tb_port *src, int src_hopid,
				 struct tb_port *dst, int dst_hopid,
				 struct tb_port **last, const char *name)
{
	struct tb_port *out_port;
	struct tb_regs_hop hop;
	struct tb_path *path;
	struct tb_switch *sw;
	struct tb_port *p;
	size_t num_hops;
	int ret, i, h;

	if (src_hopid < 0 && dst) {
		/*
		 * For incomplete paths the intermediate HopID can be
		 * different from the one used by the protocol adapter
		 * so in that case find a path that ends on @dst with
		 * matching @dst_hopid. That should give us the correct
		 * HopID for the @src.
		 */
		src_hopid = tb_path_find_src_hopid(src, dst, dst_hopid);
		if (!src_hopid)
			return NULL;
	}

	p = src;
	h = src_hopid;
	num_hops = 0;

	for (i = 0; p && i < TB_PATH_MAX_HOPS; i++) {
		sw = p->sw;

		ret = tb_port_read(p, &hop, TB_CFG_HOPS, 2 * h, 2);
		if (ret) {
			tb_port_warn(p, "failed to read path at %d\n", h);
			return NULL;
		}

		/* If the hop is not enabled we got an incomplete path */
		if (!hop.enable)
			break;

		out_port = &sw->ports[hop.out_port];
		if (last)
			*last = out_port;

		h = hop.next_hop;
		p = out_port->remote;
		num_hops++;
	}

	path = kzalloc(sizeof(*path), GFP_KERNEL);
	if (!path)
		return NULL;

	path->name = name;
	path->tb = src->sw->tb;
	path->path_length = num_hops;
	path->activated = true;

	path->hops = kcalloc(num_hops, sizeof(*path->hops), GFP_KERNEL);
	if (!path->hops) {
		kfree(path);
		return NULL;
	}

	p = src;
	h = src_hopid;

	for (i = 0; i < num_hops; i++) {
		int next_hop;

		sw = p->sw;

		ret = tb_port_read(p, &hop, TB_CFG_HOPS, 2 * h, 2);
		if (ret) {
			tb_port_warn(p, "failed to read path at %d\n", h);
			goto err;
		}

		if (tb_port_alloc_in_hopid(p, h, h) < 0)
			goto err;

		out_port = &sw->ports[hop.out_port];
		next_hop = hop.next_hop;

		if (tb_port_alloc_out_hopid(out_port, next_hop, next_hop) < 0) {
			tb_port_release_in_hopid(p, h);
			goto err;
		}

		path->hops[i].in_port = p;
		path->hops[i].in_hop_index = h;
		path->hops[i].in_counter_index = -1;
		path->hops[i].out_port = out_port;
		path->hops[i].next_hop_index = next_hop;

		h = next_hop;
		p = out_port->remote;
	}

	return path;

err:
	tb_port_warn(src, "failed to discover path starting at HopID %d\n",
		     src_hopid);
	tb_path_free(path);
	return NULL;
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
 * links on the path, prioritizes using @link_nr but takes into account
 * that the lanes may be bonded.
 *
 * Return: Returns a tb_path on success or NULL on failure.
 */
struct tb_path *tb_path_alloc(struct tb *tb, struct tb_port *src, int src_hopid,
			      struct tb_port *dst, int dst_hopid, int link_nr,
			      const char *name)
{
	struct tb_port *in_port, *out_port, *first_port, *last_port;
	int in_hopid, out_hopid;
	struct tb_path *path;
	size_t num_hops;
	int i, ret;

	path = kzalloc(sizeof(*path), GFP_KERNEL);
	if (!path)
		return NULL;

	first_port = last_port = NULL;
	i = 0;
	tb_for_each_port_on_path(src, dst, in_port) {
		if (!first_port)
			first_port = in_port;
		last_port = in_port;
		i++;
	}

	/* Check that src and dst are reachable */
	if (first_port != src || last_port != dst) {
		kfree(path);
		return NULL;
	}

	/* Each hop takes two ports */
	num_hops = i / 2;

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

		/* When lanes are bonded primary link must be used */
		if (!in_port->bonded && in_port->dual_link_port &&
		    in_port->link_nr != link_nr)
			in_port = in_port->dual_link_port;

		ret = tb_port_alloc_in_hopid(in_port, in_hopid, in_hopid);
		if (ret < 0)
			goto err;
		in_hopid = ret;

		out_port = tb_next_port_on_path(src, dst, in_port);
		if (!out_port)
			goto err;

		/*
		 * Pick up right port when going from non-bonded to
		 * bonded or from bonded to non-bonded.
		 */
		if (out_port->dual_link_port) {
			if (!in_port->bonded && out_port->bonded &&
			    out_port->link_nr) {
				/*
				 * Use primary link when going from
				 * non-bonded to bonded.
				 */
				out_port = out_port->dual_link_port;
			} else if (!out_port->bonded &&
				   out_port->link_nr != link_nr) {
				/*
				 * If out port is not bonded follow
				 * link_nr.
				 */
				out_port = out_port->dual_link_port;
			}
		}

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
 * tb_path_free() - free a path
 * @path: Path to free
 *
 * Frees a path. The path does not need to be deactivated.
 */
void tb_path_free(struct tb_path *path)
{
	int i;

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

static int __tb_path_deactivate_hop(struct tb_port *port, int hop_index,
				    bool clear_fc)
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

		if (!hop.pending) {
			if (clear_fc) {
				/*
				 * Clear flow control. Protocol adapters
				 * IFC and ISE bits are vendor defined
				 * in the USB4 spec so we clear them
				 * only for pre-USB4 adapters.
				 */
				if (!tb_switch_is_usb4(port->sw)) {
					hop.ingress_fc = 0;
					hop.ingress_shared_buffer = 0;
				}
				hop.egress_fc = 0;
				hop.egress_shared_buffer = 0;

				return tb_port_write(port, &hop, TB_CFG_HOPS,
						     2 * hop_index, 2);
			}

			return 0;
		}

		usleep_range(10, 20);
	} while (ktime_before(ktime_get(), timeout));

	return -ETIMEDOUT;
}

static void __tb_path_deactivate_hops(struct tb_path *path, int first_hop)
{
	int i, res;

	for (i = first_hop; i < path->path_length; i++) {
		res = __tb_path_deactivate_hop(path->hops[i].in_port,
					       path->hops[i].in_hop_index,
					       path->clear_fc);
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
	       "deactivating %s path from %llx:%u to %llx:%u\n",
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
 * @path: Path to activate
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
	       "activating %s path from %llx:%u to %llx:%u\n",
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

		/* If it is left active deactivate it first */
		__tb_path_deactivate_hop(path->hops[i].in_port,
				path->hops[i].in_hop_index, path->clear_fc);

		/* dword 0 */
		hop.next_hop = path->hops[i].next_hop_index;
		hop.out_port = path->hops[i].out_port->port;
		hop.initial_credits = path->hops[i].initial_credits;
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

		tb_port_dbg(path->hops[i].in_port, "Writing hop %d\n", i);
		tb_dump_hop(&path->hops[i], &hop);
		res = tb_port_write(path->hops[i].in_port, &hop, TB_CFG_HOPS,
				    2 * path->hops[i].in_hop_index, 2);
		if (res) {
			__tb_path_deactivate_hops(path, i);
			__tb_path_deallocate_nfc(path, 0);
			goto err;
		}
	}
	path->activated = true;
	tb_dbg(path->tb, "path activation complete\n");
	return 0;
err:
	tb_WARN(path->tb, "path activation failed\n");
	return res;
}

/**
 * tb_path_is_invalid() - check whether any ports on the path are invalid
 * @path: Path to check
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

/**
 * tb_path_port_on_path() - Does the path go through certain port
 * @path: Path to check
 * @port: Switch to check
 *
 * Goes over all hops on path and checks if @port is any of them.
 * Direction does not matter.
 */
bool tb_path_port_on_path(const struct tb_path *path, const struct tb_port *port)
{
	int i;

	for (i = 0; i < path->path_length; i++) {
		if (path->hops[i].in_port == port ||
		    path->hops[i].out_port == port)
			return true;
	}

	return false;
}
