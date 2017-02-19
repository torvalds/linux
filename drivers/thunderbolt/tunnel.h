/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Thunderbolt driver - Tunneling support
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2019, Intel Corporation
 */

#ifndef TB_TUNNEL_H_
#define TB_TUNNEL_H_

#include "tb.h"

/**
 * struct tb_tunnel - Tunnel between two ports
 * @tb: Pointer to the domain
 * @src_port: Source port of the tunnel
 * @dst_port: Destination port of the tunnel. For discovered incomplete
 *	      tunnels may be %NULL or null adapter port instead.
 * @paths: All paths required by the tunnel
 * @npaths: Number of paths in @paths
 * @activate: Optional tunnel specific activation/deactivation
 * @list: Tunnels are linked using this field
 */
struct tb_tunnel {
	struct tb *tb;
	struct tb_port *src_port;
	struct tb_port *dst_port;
	struct tb_path **paths;
	size_t npaths;
	int (*activate)(struct tb_tunnel *tunnel, bool activate);
	struct list_head list;
};

struct tb_tunnel *tb_tunnel_discover_pci(struct tb *tb, struct tb_port *down);
struct tb_tunnel *tb_tunnel_alloc_pci(struct tb *tb, struct tb_port *up,
				      struct tb_port *down);
void tb_tunnel_free(struct tb_tunnel *tunnel);
int tb_tunnel_activate(struct tb_tunnel *tunnel);
int tb_tunnel_restart(struct tb_tunnel *tunnel);
void tb_tunnel_deactivate(struct tb_tunnel *tunnel);
bool tb_tunnel_is_invalid(struct tb_tunnel *tunnel);

#endif

