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
	TB_TUNNEL_USB3,
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
 * @deinit: Optional tunnel specific de-initialization
 * @activate: Optional tunnel specific activation/deactivation
 * @maximum_bandwidth: Returns maximum possible bandwidth for this tunnel
 * @allocated_bandwidth: Return how much bandwidth is allocated for the tunnel
 * @alloc_bandwidth: Change tunnel bandwidth allocation
 * @consumed_bandwidth: Return how much bandwidth the tunnel consumes
 * @release_unused_bandwidth: Release all unused bandwidth
 * @reclaim_available_bandwidth: Reclaim back available bandwidth
 * @list: Tunnels are linked using this field
 * @type: Type of the tunnel
 * @max_up: Maximum upstream bandwidth (Mb/s) available for the tunnel.
 *	    Only set if the bandwidth needs to be limited.
 * @max_down: Maximum downstream bandwidth (Mb/s) available for the tunnel.
 *	      Only set if the bandwidth needs to be limited.
 * @allocated_up: Allocated upstream bandwidth (only for USB3)
 * @allocated_down: Allocated downstream bandwidth (only for USB3)
 * @bw_mode: DP bandwidth allocation mode registers can be used to
 *	     determine consumed and allocated bandwidth
 */
struct tb_tunnel {
	struct tb *tb;
	struct tb_port *src_port;
	struct tb_port *dst_port;
	struct tb_path **paths;
	size_t npaths;
	int (*init)(struct tb_tunnel *tunnel);
	void (*deinit)(struct tb_tunnel *tunnel);
	int (*activate)(struct tb_tunnel *tunnel, bool activate);
	int (*maximum_bandwidth)(struct tb_tunnel *tunnel, int *max_up,
				 int *max_down);
	int (*allocated_bandwidth)(struct tb_tunnel *tunnel, int *allocated_up,
				   int *allocated_down);
	int (*alloc_bandwidth)(struct tb_tunnel *tunnel, int *alloc_up,
			       int *alloc_down);
	int (*consumed_bandwidth)(struct tb_tunnel *tunnel, int *consumed_up,
				  int *consumed_down);
	int (*release_unused_bandwidth)(struct tb_tunnel *tunnel);
	void (*reclaim_available_bandwidth)(struct tb_tunnel *tunnel,
					    int *available_up,
					    int *available_down);
	struct list_head list;
	enum tb_tunnel_type type;
	int max_up;
	int max_down;
	int allocated_up;
	int allocated_down;
	bool bw_mode;
};

struct tb_tunnel *tb_tunnel_discover_pci(struct tb *tb, struct tb_port *down,
					 bool alloc_hopid);
struct tb_tunnel *tb_tunnel_alloc_pci(struct tb *tb, struct tb_port *up,
				      struct tb_port *down);
struct tb_tunnel *tb_tunnel_discover_dp(struct tb *tb, struct tb_port *in,
					bool alloc_hopid);
struct tb_tunnel *tb_tunnel_alloc_dp(struct tb *tb, struct tb_port *in,
				     struct tb_port *out, int link_nr,
				     int max_up, int max_down);
struct tb_tunnel *tb_tunnel_alloc_dma(struct tb *tb, struct tb_port *nhi,
				      struct tb_port *dst, int transmit_path,
				      int transmit_ring, int receive_path,
				      int receive_ring);
bool tb_tunnel_match_dma(const struct tb_tunnel *tunnel, int transmit_path,
			 int transmit_ring, int receive_path, int receive_ring);
struct tb_tunnel *tb_tunnel_discover_usb3(struct tb *tb, struct tb_port *down,
					  bool alloc_hopid);
struct tb_tunnel *tb_tunnel_alloc_usb3(struct tb *tb, struct tb_port *up,
				       struct tb_port *down, int max_up,
				       int max_down);

void tb_tunnel_free(struct tb_tunnel *tunnel);
int tb_tunnel_activate(struct tb_tunnel *tunnel);
int tb_tunnel_restart(struct tb_tunnel *tunnel);
void tb_tunnel_deactivate(struct tb_tunnel *tunnel);
bool tb_tunnel_is_invalid(struct tb_tunnel *tunnel);
bool tb_tunnel_port_on_path(const struct tb_tunnel *tunnel,
			    const struct tb_port *port);
int tb_tunnel_maximum_bandwidth(struct tb_tunnel *tunnel, int *max_up,
				int *max_down);
int tb_tunnel_allocated_bandwidth(struct tb_tunnel *tunnel, int *allocated_up,
				  int *allocated_down);
int tb_tunnel_alloc_bandwidth(struct tb_tunnel *tunnel, int *alloc_up,
			      int *alloc_down);
int tb_tunnel_consumed_bandwidth(struct tb_tunnel *tunnel, int *consumed_up,
				 int *consumed_down);
int tb_tunnel_release_unused_bandwidth(struct tb_tunnel *tunnel);
void tb_tunnel_reclaim_available_bandwidth(struct tb_tunnel *tunnel,
					   int *available_up,
					   int *available_down);

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

static inline bool tb_tunnel_is_usb3(const struct tb_tunnel *tunnel)
{
	return tunnel->type == TB_TUNNEL_USB3;
}

#endif

