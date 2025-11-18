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
 * enum tb_tunnel_state - State of a tunnel
 * @TB_TUNNEL_INACTIVE: tb_tunnel_activate() is not called for the tunnel
 * @TB_TUNNEL_ACTIVATING: tb_tunnel_activate() returned successfully for the tunnel
 * @TB_TUNNEL_ACTIVE: The tunnel is fully active
 */
enum tb_tunnel_state {
	TB_TUNNEL_INACTIVE,
	TB_TUNNEL_ACTIVATING,
	TB_TUNNEL_ACTIVE,
};

/**
 * struct tb_tunnel - Tunnel between two ports
 * @kref: Reference count
 * @tb: Pointer to the domain
 * @src_port: Source port of the tunnel
 * @dst_port: Destination port of the tunnel. For discovered incomplete
 *	      tunnels may be %NULL or null adapter port instead.
 * @paths: All paths required by the tunnel
 * @npaths: Number of paths in @paths
 * @pre_activate: Optional tunnel specific initialization called before
 *		  activation. Can touch hardware.
 * @activate: Optional tunnel specific activation/deactivation
 * @post_deactivate: Optional tunnel specific de-initialization called
 *		     after deactivation. Can touch hardware.
 * @destroy: Optional tunnel specific callback called when the tunnel
 *	     memory is being released. Should not touch hardware.
 * @maximum_bandwidth: Returns maximum possible bandwidth for this tunnel
 * @allocated_bandwidth: Return how much bandwidth is allocated for the tunnel
 * @alloc_bandwidth: Change tunnel bandwidth allocation
 * @consumed_bandwidth: Return how much bandwidth the tunnel consumes
 * @release_unused_bandwidth: Release all unused bandwidth
 * @reclaim_available_bandwidth: Reclaim back available bandwidth
 * @list: Tunnels are linked using this field
 * @type: Type of the tunnel
 * @state: Current state of the tunnel
 * @max_up: Maximum upstream bandwidth (Mb/s) available for the tunnel.
 *	    Only set if the bandwidth needs to be limited.
 * @max_down: Maximum downstream bandwidth (Mb/s) available for the tunnel.
 *	      Only set if the bandwidth needs to be limited.
 * @allocated_up: Allocated upstream bandwidth (only for USB3)
 * @allocated_down: Allocated downstream bandwidth (only for USB3)
 * @bw_mode: DP bandwidth allocation mode registers can be used to
 *	     determine consumed and allocated bandwidth
 * @dprx_started: DPRX negotiation was started (tb_dp_dprx_start() was called for it)
 * @dprx_canceled: Was DPRX capabilities read poll canceled
 * @dprx_timeout: If set DPRX capabilities read poll work will timeout after this passes
 * @dprx_work: Worker that is scheduled to poll completion of DPRX capabilities read
 * @callback: Optional callback called when DP tunnel is fully activated
 * @callback_data: Optional data for @callback
 */
struct tb_tunnel {
	struct kref kref;
	struct tb *tb;
	struct tb_port *src_port;
	struct tb_port *dst_port;
	struct tb_path **paths;
	size_t npaths;
	int (*pre_activate)(struct tb_tunnel *tunnel);
	int (*activate)(struct tb_tunnel *tunnel, bool activate);
	void (*post_deactivate)(struct tb_tunnel *tunnel);
	void (*destroy)(struct tb_tunnel *tunnel);
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
	enum tb_tunnel_state state;
	int max_up;
	int max_down;
	int allocated_up;
	int allocated_down;
	bool bw_mode;
	bool dprx_started;
	bool dprx_canceled;
	ktime_t dprx_timeout;
	struct delayed_work dprx_work;
	void (*callback)(struct tb_tunnel *tunnel, void *data);
	void *callback_data;
};

struct tb_tunnel *tb_tunnel_discover_pci(struct tb *tb, struct tb_port *down,
					 bool alloc_hopid);
struct tb_tunnel *tb_tunnel_alloc_pci(struct tb *tb, struct tb_port *up,
				      struct tb_port *down);
bool tb_tunnel_reserved_pci(struct tb_port *port, int *reserved_up,
			    int *reserved_down);
struct tb_tunnel *tb_tunnel_discover_dp(struct tb *tb, struct tb_port *in,
					bool alloc_hopid);
struct tb_tunnel *tb_tunnel_alloc_dp(struct tb *tb, struct tb_port *in,
				     struct tb_port *out, int link_nr,
				     int max_up, int max_down,
				     void (*callback)(struct tb_tunnel *, void *),
				     void *callback_data);
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

void tb_tunnel_put(struct tb_tunnel *tunnel);
int tb_tunnel_activate(struct tb_tunnel *tunnel);
void tb_tunnel_deactivate(struct tb_tunnel *tunnel);

/**
 * tb_tunnel_is_active() - Is tunnel fully activated
 * @tunnel: Tunnel to check
 *
 * Return: %true if @tunnel is fully activated.
 *
 * Note for DP tunnels this returns %true only once the DPRX capabilities
 * read has been issued successfully. For other tunnels, this function
 * returns %true pretty much once tb_tunnel_activate() returns successfully.
 */
static inline bool tb_tunnel_is_active(const struct tb_tunnel *tunnel)
{
	return tunnel->state == TB_TUNNEL_ACTIVE;
}

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

static inline bool tb_tunnel_direction_downstream(const struct tb_tunnel *tunnel)
{
	return tb_port_path_direction_downstream(tunnel->src_port,
						 tunnel->dst_port);
}

/**
 * enum tb_tunnel_event - Tunnel related events
 * @TB_TUNNEL_ACTIVATED: A tunnel was activated
 * @TB_TUNNEL_CHANGED: There is a tunneling change in the domain. Includes
 *		       full %TUNNEL_DETAILS if the tunnel in question is known
 *		       (ICM does not provide that information).
 * @TB_TUNNEL_DEACTIVATED: A tunnel was torn down
 * @TB_TUNNEL_LOW_BANDWIDTH: Tunnel bandwidth is not optimal
 * @TB_TUNNEL_NO_BANDWIDTH: There is not enough bandwidth for a tunnel
 */
enum tb_tunnel_event {
	TB_TUNNEL_ACTIVATED,
	TB_TUNNEL_CHANGED,
	TB_TUNNEL_DEACTIVATED,
	TB_TUNNEL_LOW_BANDWIDTH,
	TB_TUNNEL_NO_BANDWIDTH,
};

void tb_tunnel_event(struct tb *tb, enum tb_tunnel_event event,
		     enum tb_tunnel_type type,
		     const struct tb_port *src_port,
		     const struct tb_port *dst_port);

const char *tb_tunnel_type_name(const struct tb_tunnel *tunnel);

#define __TB_TUNNEL_PRINT(level, tunnel, fmt, arg...)                   \
	do {                                                            \
		struct tb_tunnel *__tunnel = (tunnel);                  \
		level(__tunnel->tb, "%llx:%u <-> %llx:%u (%s): " fmt,   \
		      tb_route(__tunnel->src_port->sw),                 \
		      __tunnel->src_port->port,                         \
		      tb_route(__tunnel->dst_port->sw),                 \
		      __tunnel->dst_port->port,                         \
		      tb_tunnel_type_name(__tunnel),			\
		      ## arg);                                          \
	} while (0)

#define tb_tunnel_WARN(tunnel, fmt, arg...) \
	__TB_TUNNEL_PRINT(tb_WARN, tunnel, fmt, ##arg)
#define tb_tunnel_warn(tunnel, fmt, arg...) \
	__TB_TUNNEL_PRINT(tb_warn, tunnel, fmt, ##arg)
#define tb_tunnel_info(tunnel, fmt, arg...) \
	__TB_TUNNEL_PRINT(tb_info, tunnel, fmt, ##arg)
#define tb_tunnel_dbg(tunnel, fmt, arg...) \
	__TB_TUNNEL_PRINT(tb_dbg, tunnel, fmt, ##arg)

#endif
