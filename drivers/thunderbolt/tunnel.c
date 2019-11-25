// SPDX-License-Identifier: GPL-2.0
/*
 * Thunderbolt driver - Tunneling support
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2019, Intel Corporation
 */

#include <linux/slab.h>
#include <linux/list.h>

#include "tunnel.h"
#include "tb.h"

/* PCIe adapters use always HopID of 8 for both directions */
#define TB_PCI_HOPID			8

#define TB_PCI_PATH_DOWN		0
#define TB_PCI_PATH_UP			1

/* DP adapters use HopID 8 for AUX and 9 for Video */
#define TB_DP_AUX_TX_HOPID		8
#define TB_DP_AUX_RX_HOPID		8
#define TB_DP_VIDEO_HOPID		9

#define TB_DP_VIDEO_PATH_OUT		0
#define TB_DP_AUX_PATH_OUT		1
#define TB_DP_AUX_PATH_IN		2

#define TB_DMA_PATH_OUT			0
#define TB_DMA_PATH_IN			1

static const char * const tb_tunnel_names[] = { "PCI", "DP", "DMA" };

#define __TB_TUNNEL_PRINT(level, tunnel, fmt, arg...)                   \
	do {                                                            \
		struct tb_tunnel *__tunnel = (tunnel);                  \
		level(__tunnel->tb, "%llx:%x <-> %llx:%x (%s): " fmt,   \
		      tb_route(__tunnel->src_port->sw),                 \
		      __tunnel->src_port->port,                         \
		      tb_route(__tunnel->dst_port->sw),                 \
		      __tunnel->dst_port->port,                         \
		      tb_tunnel_names[__tunnel->type],			\
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

static struct tb_tunnel *tb_tunnel_alloc(struct tb *tb, size_t npaths,
					 enum tb_tunnel_type type)
{
	struct tb_tunnel *tunnel;

	tunnel = kzalloc(sizeof(*tunnel), GFP_KERNEL);
	if (!tunnel)
		return NULL;

	tunnel->paths = kcalloc(npaths, sizeof(tunnel->paths[0]), GFP_KERNEL);
	if (!tunnel->paths) {
		tb_tunnel_free(tunnel);
		return NULL;
	}

	INIT_LIST_HEAD(&tunnel->list);
	tunnel->tb = tb;
	tunnel->npaths = npaths;
	tunnel->type = type;

	return tunnel;
}

static int tb_pci_activate(struct tb_tunnel *tunnel, bool activate)
{
	int res;

	res = tb_pci_port_enable(tunnel->src_port, activate);
	if (res)
		return res;

	if (tb_port_is_pcie_up(tunnel->dst_port))
		return tb_pci_port_enable(tunnel->dst_port, activate);

	return 0;
}

static void tb_pci_init_path(struct tb_path *path)
{
	path->egress_fc_enable = TB_PATH_SOURCE | TB_PATH_INTERNAL;
	path->egress_shared_buffer = TB_PATH_NONE;
	path->ingress_fc_enable = TB_PATH_ALL;
	path->ingress_shared_buffer = TB_PATH_NONE;
	path->priority = 3;
	path->weight = 1;
	path->drop_packages = 0;
	path->nfc_credits = 0;
	path->hops[0].initial_credits = 7;
	path->hops[1].initial_credits = 16;
}

/**
 * tb_tunnel_discover_pci() - Discover existing PCIe tunnels
 * @tb: Pointer to the domain structure
 * @down: PCIe downstream adapter
 *
 * If @down adapter is active, follows the tunnel to the PCIe upstream
 * adapter and back. Returns the discovered tunnel or %NULL if there was
 * no tunnel.
 */
struct tb_tunnel *tb_tunnel_discover_pci(struct tb *tb, struct tb_port *down)
{
	struct tb_tunnel *tunnel;
	struct tb_path *path;

	if (!tb_pci_port_is_enabled(down))
		return NULL;

	tunnel = tb_tunnel_alloc(tb, 2, TB_TUNNEL_PCI);
	if (!tunnel)
		return NULL;

	tunnel->activate = tb_pci_activate;
	tunnel->src_port = down;

	/*
	 * Discover both paths even if they are not complete. We will
	 * clean them up by calling tb_tunnel_deactivate() below in that
	 * case.
	 */
	path = tb_path_discover(down, TB_PCI_HOPID, NULL, -1,
				&tunnel->dst_port, "PCIe Up");
	if (!path) {
		/* Just disable the downstream port */
		tb_pci_port_enable(down, false);
		goto err_free;
	}
	tunnel->paths[TB_PCI_PATH_UP] = path;
	tb_pci_init_path(tunnel->paths[TB_PCI_PATH_UP]);

	path = tb_path_discover(tunnel->dst_port, -1, down, TB_PCI_HOPID, NULL,
				"PCIe Down");
	if (!path)
		goto err_deactivate;
	tunnel->paths[TB_PCI_PATH_DOWN] = path;
	tb_pci_init_path(tunnel->paths[TB_PCI_PATH_DOWN]);

	/* Validate that the tunnel is complete */
	if (!tb_port_is_pcie_up(tunnel->dst_port)) {
		tb_port_warn(tunnel->dst_port,
			     "path does not end on a PCIe adapter, cleaning up\n");
		goto err_deactivate;
	}

	if (down != tunnel->src_port) {
		tb_tunnel_warn(tunnel, "path is not complete, cleaning up\n");
		goto err_deactivate;
	}

	if (!tb_pci_port_is_enabled(tunnel->dst_port)) {
		tb_tunnel_warn(tunnel,
			       "tunnel is not fully activated, cleaning up\n");
		goto err_deactivate;
	}

	tb_tunnel_dbg(tunnel, "discovered\n");
	return tunnel;

err_deactivate:
	tb_tunnel_deactivate(tunnel);
err_free:
	tb_tunnel_free(tunnel);

	return NULL;
}

/**
 * tb_tunnel_alloc_pci() - allocate a pci tunnel
 * @tb: Pointer to the domain structure
 * @up: PCIe upstream adapter port
 * @down: PCIe downstream adapter port
 *
 * Allocate a PCI tunnel. The ports must be of type TB_TYPE_PCIE_UP and
 * TB_TYPE_PCIE_DOWN.
 *
 * Return: Returns a tb_tunnel on success or NULL on failure.
 */
struct tb_tunnel *tb_tunnel_alloc_pci(struct tb *tb, struct tb_port *up,
				      struct tb_port *down)
{
	struct tb_tunnel *tunnel;
	struct tb_path *path;

	tunnel = tb_tunnel_alloc(tb, 2, TB_TUNNEL_PCI);
	if (!tunnel)
		return NULL;

	tunnel->activate = tb_pci_activate;
	tunnel->src_port = down;
	tunnel->dst_port = up;

	path = tb_path_alloc(tb, down, TB_PCI_HOPID, up, TB_PCI_HOPID, 0,
			     "PCIe Down");
	if (!path) {
		tb_tunnel_free(tunnel);
		return NULL;
	}
	tb_pci_init_path(path);
	tunnel->paths[TB_PCI_PATH_DOWN] = path;

	path = tb_path_alloc(tb, up, TB_PCI_HOPID, down, TB_PCI_HOPID, 0,
			     "PCIe Up");
	if (!path) {
		tb_tunnel_free(tunnel);
		return NULL;
	}
	tb_pci_init_path(path);
	tunnel->paths[TB_PCI_PATH_UP] = path;

	return tunnel;
}

static int tb_dp_xchg_caps(struct tb_tunnel *tunnel)
{
	struct tb_port *out = tunnel->dst_port;
	struct tb_port *in = tunnel->src_port;
	u32 in_dp_cap, out_dp_cap;
	int ret;

	/*
	 * Copy DP_LOCAL_CAP register to DP_REMOTE_CAP register for
	 * newer generation hardware.
	 */
	if (in->sw->generation < 2 || out->sw->generation < 2)
		return 0;

	/* Read both DP_LOCAL_CAP registers */
	ret = tb_port_read(in, &in_dp_cap, TB_CFG_PORT,
			   in->cap_adap + TB_DP_LOCAL_CAP, 1);
	if (ret)
		return ret;

	ret = tb_port_read(out, &out_dp_cap, TB_CFG_PORT,
			   out->cap_adap + TB_DP_LOCAL_CAP, 1);
	if (ret)
		return ret;

	/* Write IN local caps to OUT remote caps */
	ret = tb_port_write(out, &in_dp_cap, TB_CFG_PORT,
			    out->cap_adap + TB_DP_REMOTE_CAP, 1);
	if (ret)
		return ret;

	return tb_port_write(in, &out_dp_cap, TB_CFG_PORT,
			     in->cap_adap + TB_DP_REMOTE_CAP, 1);
}

static int tb_dp_activate(struct tb_tunnel *tunnel, bool active)
{
	int ret;

	if (active) {
		struct tb_path **paths;
		int last;

		paths = tunnel->paths;
		last = paths[TB_DP_VIDEO_PATH_OUT]->path_length - 1;

		tb_dp_port_set_hops(tunnel->src_port,
			paths[TB_DP_VIDEO_PATH_OUT]->hops[0].in_hop_index,
			paths[TB_DP_AUX_PATH_OUT]->hops[0].in_hop_index,
			paths[TB_DP_AUX_PATH_IN]->hops[last].next_hop_index);

		tb_dp_port_set_hops(tunnel->dst_port,
			paths[TB_DP_VIDEO_PATH_OUT]->hops[last].next_hop_index,
			paths[TB_DP_AUX_PATH_IN]->hops[0].in_hop_index,
			paths[TB_DP_AUX_PATH_OUT]->hops[last].next_hop_index);
	} else {
		tb_dp_port_hpd_clear(tunnel->src_port);
		tb_dp_port_set_hops(tunnel->src_port, 0, 0, 0);
		if (tb_port_is_dpout(tunnel->dst_port))
			tb_dp_port_set_hops(tunnel->dst_port, 0, 0, 0);
	}

	ret = tb_dp_port_enable(tunnel->src_port, active);
	if (ret)
		return ret;

	if (tb_port_is_dpout(tunnel->dst_port))
		return tb_dp_port_enable(tunnel->dst_port, active);

	return 0;
}

static void tb_dp_init_aux_path(struct tb_path *path)
{
	int i;

	path->egress_fc_enable = TB_PATH_SOURCE | TB_PATH_INTERNAL;
	path->egress_shared_buffer = TB_PATH_NONE;
	path->ingress_fc_enable = TB_PATH_ALL;
	path->ingress_shared_buffer = TB_PATH_NONE;
	path->priority = 2;
	path->weight = 1;

	for (i = 0; i < path->path_length; i++)
		path->hops[i].initial_credits = 1;
}

static void tb_dp_init_video_path(struct tb_path *path, bool discover)
{
	u32 nfc_credits = path->hops[0].in_port->config.nfc_credits;

	path->egress_fc_enable = TB_PATH_NONE;
	path->egress_shared_buffer = TB_PATH_NONE;
	path->ingress_fc_enable = TB_PATH_NONE;
	path->ingress_shared_buffer = TB_PATH_NONE;
	path->priority = 1;
	path->weight = 1;

	if (discover) {
		path->nfc_credits = nfc_credits & TB_PORT_NFC_CREDITS_MASK;
	} else {
		u32 max_credits;

		max_credits = (nfc_credits & TB_PORT_MAX_CREDITS_MASK) >>
			TB_PORT_MAX_CREDITS_SHIFT;
		/* Leave some credits for AUX path */
		path->nfc_credits = min(max_credits - 2, 12U);
	}
}

/**
 * tb_tunnel_discover_dp() - Discover existing Display Port tunnels
 * @tb: Pointer to the domain structure
 * @in: DP in adapter
 *
 * If @in adapter is active, follows the tunnel to the DP out adapter
 * and back. Returns the discovered tunnel or %NULL if there was no
 * tunnel.
 *
 * Return: DP tunnel or %NULL if no tunnel found.
 */
struct tb_tunnel *tb_tunnel_discover_dp(struct tb *tb, struct tb_port *in)
{
	struct tb_tunnel *tunnel;
	struct tb_port *port;
	struct tb_path *path;

	if (!tb_dp_port_is_enabled(in))
		return NULL;

	tunnel = tb_tunnel_alloc(tb, 3, TB_TUNNEL_DP);
	if (!tunnel)
		return NULL;

	tunnel->init = tb_dp_xchg_caps;
	tunnel->activate = tb_dp_activate;
	tunnel->src_port = in;

	path = tb_path_discover(in, TB_DP_VIDEO_HOPID, NULL, -1,
				&tunnel->dst_port, "Video");
	if (!path) {
		/* Just disable the DP IN port */
		tb_dp_port_enable(in, false);
		goto err_free;
	}
	tunnel->paths[TB_DP_VIDEO_PATH_OUT] = path;
	tb_dp_init_video_path(tunnel->paths[TB_DP_VIDEO_PATH_OUT], true);

	path = tb_path_discover(in, TB_DP_AUX_TX_HOPID, NULL, -1, NULL, "AUX TX");
	if (!path)
		goto err_deactivate;
	tunnel->paths[TB_DP_AUX_PATH_OUT] = path;
	tb_dp_init_aux_path(tunnel->paths[TB_DP_AUX_PATH_OUT]);

	path = tb_path_discover(tunnel->dst_port, -1, in, TB_DP_AUX_RX_HOPID,
				&port, "AUX RX");
	if (!path)
		goto err_deactivate;
	tunnel->paths[TB_DP_AUX_PATH_IN] = path;
	tb_dp_init_aux_path(tunnel->paths[TB_DP_AUX_PATH_IN]);

	/* Validate that the tunnel is complete */
	if (!tb_port_is_dpout(tunnel->dst_port)) {
		tb_port_warn(in, "path does not end on a DP adapter, cleaning up\n");
		goto err_deactivate;
	}

	if (!tb_dp_port_is_enabled(tunnel->dst_port))
		goto err_deactivate;

	if (!tb_dp_port_hpd_is_active(tunnel->dst_port))
		goto err_deactivate;

	if (port != tunnel->src_port) {
		tb_tunnel_warn(tunnel, "path is not complete, cleaning up\n");
		goto err_deactivate;
	}

	tb_tunnel_dbg(tunnel, "discovered\n");
	return tunnel;

err_deactivate:
	tb_tunnel_deactivate(tunnel);
err_free:
	tb_tunnel_free(tunnel);

	return NULL;
}

/**
 * tb_tunnel_alloc_dp() - allocate a Display Port tunnel
 * @tb: Pointer to the domain structure
 * @in: DP in adapter port
 * @out: DP out adapter port
 *
 * Allocates a tunnel between @in and @out that is capable of tunneling
 * Display Port traffic.
 *
 * Return: Returns a tb_tunnel on success or NULL on failure.
 */
struct tb_tunnel *tb_tunnel_alloc_dp(struct tb *tb, struct tb_port *in,
				     struct tb_port *out)
{
	struct tb_tunnel *tunnel;
	struct tb_path **paths;
	struct tb_path *path;

	if (WARN_ON(!in->cap_adap || !out->cap_adap))
		return NULL;

	tunnel = tb_tunnel_alloc(tb, 3, TB_TUNNEL_DP);
	if (!tunnel)
		return NULL;

	tunnel->init = tb_dp_xchg_caps;
	tunnel->activate = tb_dp_activate;
	tunnel->src_port = in;
	tunnel->dst_port = out;

	paths = tunnel->paths;

	path = tb_path_alloc(tb, in, TB_DP_VIDEO_HOPID, out, TB_DP_VIDEO_HOPID,
			     1, "Video");
	if (!path)
		goto err_free;
	tb_dp_init_video_path(path, false);
	paths[TB_DP_VIDEO_PATH_OUT] = path;

	path = tb_path_alloc(tb, in, TB_DP_AUX_TX_HOPID, out,
			     TB_DP_AUX_TX_HOPID, 1, "AUX TX");
	if (!path)
		goto err_free;
	tb_dp_init_aux_path(path);
	paths[TB_DP_AUX_PATH_OUT] = path;

	path = tb_path_alloc(tb, out, TB_DP_AUX_RX_HOPID, in,
			     TB_DP_AUX_RX_HOPID, 1, "AUX RX");
	if (!path)
		goto err_free;
	tb_dp_init_aux_path(path);
	paths[TB_DP_AUX_PATH_IN] = path;

	return tunnel;

err_free:
	tb_tunnel_free(tunnel);
	return NULL;
}

static u32 tb_dma_credits(struct tb_port *nhi)
{
	u32 max_credits;

	max_credits = (nhi->config.nfc_credits & TB_PORT_MAX_CREDITS_MASK) >>
		TB_PORT_MAX_CREDITS_SHIFT;
	return min(max_credits, 13U);
}

static int tb_dma_activate(struct tb_tunnel *tunnel, bool active)
{
	struct tb_port *nhi = tunnel->src_port;
	u32 credits;

	credits = active ? tb_dma_credits(nhi) : 0;
	return tb_port_set_initial_credits(nhi, credits);
}

static void tb_dma_init_path(struct tb_path *path, unsigned int isb,
			     unsigned int efc, u32 credits)
{
	int i;

	path->egress_fc_enable = efc;
	path->ingress_fc_enable = TB_PATH_ALL;
	path->egress_shared_buffer = TB_PATH_NONE;
	path->ingress_shared_buffer = isb;
	path->priority = 5;
	path->weight = 1;
	path->clear_fc = true;

	for (i = 0; i < path->path_length; i++)
		path->hops[i].initial_credits = credits;
}

/**
 * tb_tunnel_alloc_dma() - allocate a DMA tunnel
 * @tb: Pointer to the domain structure
 * @nhi: Host controller port
 * @dst: Destination null port which the other domain is connected to
 * @transmit_ring: NHI ring number used to send packets towards the
 *		   other domain
 * @transmit_path: HopID used for transmitting packets
 * @receive_ring: NHI ring number used to receive packets from the
 *		  other domain
 * @reveive_path: HopID used for receiving packets
 *
 * Return: Returns a tb_tunnel on success or NULL on failure.
 */
struct tb_tunnel *tb_tunnel_alloc_dma(struct tb *tb, struct tb_port *nhi,
				      struct tb_port *dst, int transmit_ring,
				      int transmit_path, int receive_ring,
				      int receive_path)
{
	struct tb_tunnel *tunnel;
	struct tb_path *path;
	u32 credits;

	tunnel = tb_tunnel_alloc(tb, 2, TB_TUNNEL_DMA);
	if (!tunnel)
		return NULL;

	tunnel->activate = tb_dma_activate;
	tunnel->src_port = nhi;
	tunnel->dst_port = dst;

	credits = tb_dma_credits(nhi);

	path = tb_path_alloc(tb, dst, receive_path, nhi, receive_ring, 0, "DMA RX");
	if (!path) {
		tb_tunnel_free(tunnel);
		return NULL;
	}
	tb_dma_init_path(path, TB_PATH_NONE, TB_PATH_SOURCE | TB_PATH_INTERNAL,
			 credits);
	tunnel->paths[TB_DMA_PATH_IN] = path;

	path = tb_path_alloc(tb, nhi, transmit_ring, dst, transmit_path, 0, "DMA TX");
	if (!path) {
		tb_tunnel_free(tunnel);
		return NULL;
	}
	tb_dma_init_path(path, TB_PATH_SOURCE, TB_PATH_ALL, credits);
	tunnel->paths[TB_DMA_PATH_OUT] = path;

	return tunnel;
}

/**
 * tb_tunnel_free() - free a tunnel
 * @tunnel: Tunnel to be freed
 *
 * Frees a tunnel. The tunnel does not need to be deactivated.
 */
void tb_tunnel_free(struct tb_tunnel *tunnel)
{
	int i;

	if (!tunnel)
		return;

	for (i = 0; i < tunnel->npaths; i++) {
		if (tunnel->paths[i])
			tb_path_free(tunnel->paths[i]);
	}

	kfree(tunnel->paths);
	kfree(tunnel);
}

/**
 * tb_tunnel_is_invalid - check whether an activated path is still valid
 * @tunnel: Tunnel to check
 */
bool tb_tunnel_is_invalid(struct tb_tunnel *tunnel)
{
	int i;

	for (i = 0; i < tunnel->npaths; i++) {
		WARN_ON(!tunnel->paths[i]->activated);
		if (tb_path_is_invalid(tunnel->paths[i]))
			return true;
	}

	return false;
}

/**
 * tb_tunnel_restart() - activate a tunnel after a hardware reset
 * @tunnel: Tunnel to restart
 *
 * Return: 0 on success and negative errno in case if failure
 */
int tb_tunnel_restart(struct tb_tunnel *tunnel)
{
	int res, i;

	tb_tunnel_dbg(tunnel, "activating\n");

	/*
	 * Make sure all paths are properly disabled before enabling
	 * them again.
	 */
	for (i = 0; i < tunnel->npaths; i++) {
		if (tunnel->paths[i]->activated) {
			tb_path_deactivate(tunnel->paths[i]);
			tunnel->paths[i]->activated = false;
		}
	}

	if (tunnel->init) {
		res = tunnel->init(tunnel);
		if (res)
			return res;
	}

	for (i = 0; i < tunnel->npaths; i++) {
		res = tb_path_activate(tunnel->paths[i]);
		if (res)
			goto err;
	}

	if (tunnel->activate) {
		res = tunnel->activate(tunnel, true);
		if (res)
			goto err;
	}

	return 0;

err:
	tb_tunnel_warn(tunnel, "activation failed\n");
	tb_tunnel_deactivate(tunnel);
	return res;
}

/**
 * tb_tunnel_activate() - activate a tunnel
 * @tunnel: Tunnel to activate
 *
 * Return: Returns 0 on success or an error code on failure.
 */
int tb_tunnel_activate(struct tb_tunnel *tunnel)
{
	int i;

	for (i = 0; i < tunnel->npaths; i++) {
		if (tunnel->paths[i]->activated) {
			tb_tunnel_WARN(tunnel,
				       "trying to activate an already activated tunnel\n");
			return -EINVAL;
		}
	}

	return tb_tunnel_restart(tunnel);
}

/**
 * tb_tunnel_deactivate() - deactivate a tunnel
 * @tunnel: Tunnel to deactivate
 */
void tb_tunnel_deactivate(struct tb_tunnel *tunnel)
{
	int i;

	tb_tunnel_dbg(tunnel, "deactivating\n");

	if (tunnel->activate)
		tunnel->activate(tunnel, false);

	for (i = 0; i < tunnel->npaths; i++) {
		if (tunnel->paths[i] && tunnel->paths[i]->activated)
			tb_path_deactivate(tunnel->paths[i]);
	}
}
