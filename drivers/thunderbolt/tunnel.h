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

enum tb_tunnel_type {
	TB_TUNNEL_PCI,
	TB_TUNNEL_DP,
	TB_TUNNEL_DMA,
};

/**
 * struct tb_tunnel - Tunnel between two ports
 * @tb: Pointer to the domain
 * @src_port: Source port of the tunnel
 * @dst_port: Destination port of the tunnel. For discovered incomplete
 *	      tunnels may be %NULL or null adapter port instead.
 * @paths: All paths required by the tunnel
 * @npaths: Number of paths in @paths
 * @init: Optional tunnel specific initialization
 * @activate: Optional tunnel specific activation/deactivation
 * @consumed_bandwidth: Return how much bandwidth the tunnel consumes
 * @list: Tunnels are linked using this field
 * @type: Type of the tunnel
 * @max_bw: Maximum bandwidth (Mb/s) available for the tunnel (only for DP).
 *	    Only set if the bandwidth needs to be limited.
 */
struct tb_tunnel {
	struct tb *tb;
	struct tb_port *src_port;
	struct tb_port *dst_port;
	struct tb_path **paths;
	size_t npaths;
	int (*init)(struct tb_tunnel *tunnel);
	int (*activate)(struct tb_tunnel *tunnel, bool activate);
	int (*consumed_bandwidth)(struct tb_tunnel *tunnel);
	struct list_head list;
	enum tb_tunnel_type type;
	unsigned int max_bw;
};

struct tb_tunnel *tb_tunnel_discover_pci(struct tb *tb, struct tb_port *down);
struct tb_tunnel *tb_tunnel_alloc_pci(struct tb *tb, struct tb_port *up,
				      struct tb_port *down);
struct tb_tunnel *tb_tunnel_discover_dp(struct tb *tb, struct tb_port *in);
struct tb_tunnel *tb_tunnel_alloc_dp(struct tb *tb, struct tb_port *in,
				     struct tb_port *out, int max_bw);
struct tb_tunnel *tb_tunnel_alloc_dma(struct tb *tb, struct tb_port *nhi,
				      struct tb_port *dst, int transmit_ring,
				      int transmit_path, int receive_ring,
				      int receive_path);

void tb_tunnel_free(struct tb_tunnel *tunnel);
int tb_tunnel_activate(struct tb_tunnel *tunnel);
int tb_tunnel_restart(struct tb_tunnel *tunnel);
void tb_tunnel_deactivate(struct tb_tunnel *tunnel);
bool tb_tunnel_is_invalid(struct tb_tunnel *tunnel);
bool tb_tunnel_switch_on_path(const struct tb_tunnel *tunnel,
			      const struct tb_switch *sw);
int tb_tunnel_consumed_bandwidth(struct tb_tunnel *tunnel);

static inline bool tb_tunnel_is_pci(const struct tb_tunnel *tunnel)
{
	return tunnel->type == TB_TUNNEL_PCI;
}

static inline bool tb_tunnel_is_dp(const struct tb_tunnel *tunnel)
{
	return tunnel->type == TB_TUNNEL_DP;
}

static inline bool tb_tunnel_is_dma(const struct tb_tunnel *tunnel)
{
	return tunnel->type == TB_TUNNEL_DMA;
}

#endif

