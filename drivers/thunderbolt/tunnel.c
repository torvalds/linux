// SPDX-License-Identifier: GPL-2.0
/*
 * Thunderbolt driver - Tunneling support
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2019, Intel Corporation
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/list.h>

#include "tunnel.h"
#include "tb.h"

/* PCIe adapters use always HopID of 8 for both directions */
#define TB_PCI_HOPID			8

#define TB_PCI_PATH_DOWN		0
#define TB_PCI_PATH_UP			1

/* USB3 adapters use always HopID of 8 for both directions */
#define TB_USB3_HOPID			8

#define TB_USB3_PATH_DOWN		0
#define TB_USB3_PATH_UP			1

/* DP adapters use HopID 8 for AUX and 9 for Video */
#define TB_DP_AUX_TX_HOPID		8
#define TB_DP_AUX_RX_HOPID		8
#define TB_DP_VIDEO_HOPID		9

#define TB_DP_VIDEO_PATH_OUT		0
#define TB_DP_AUX_PATH_OUT		1
#define TB_DP_AUX_PATH_IN		2

#define TB_DMA_PATH_OUT			0
#define TB_DMA_PATH_IN			1

static const char * const tb_tunnel_names[] = { "PCI", "DP", "DMA", "USB3" };

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

static int tb_initial_credits(const struct tb_switch *sw)
{
	/* If the path is complete sw is not NULL */
	if (sw) {
		/* More credits for faster link */
		switch (sw->link_speed * sw->link_width) {
		case 40:
			return 32;
		case 20:
			return 24;
		}
	}

	return 16;
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
	if (path->path_length > 1)
		path->hops[1].initial_credits =
			tb_initial_credits(path->hops[1].in_port->sw);
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

static bool tb_dp_is_usb4(const struct tb_switch *sw)
{
	/* Titan Ridge DP adapters need the same treatment as USB4 */
	return tb_switch_is_usb4(sw) || tb_switch_is_titan_ridge(sw);
}

static int tb_dp_cm_handshake(struct tb_port *in, struct tb_port *out)
{
	int timeout = 10;
	u32 val;
	int ret;

	/* Both ends need to support this */
	if (!tb_dp_is_usb4(in->sw) || !tb_dp_is_usb4(out->sw))
		return 0;

	ret = tb_port_read(out, &val, TB_CFG_PORT,
			   out->cap_adap + DP_STATUS_CTRL, 1);
	if (ret)
		return ret;

	val |= DP_STATUS_CTRL_UF | DP_STATUS_CTRL_CMHS;

	ret = tb_port_write(out, &val, TB_CFG_PORT,
			    out->cap_adap + DP_STATUS_CTRL, 1);
	if (ret)
		return ret;

	do {
		ret = tb_port_read(out, &val, TB_CFG_PORT,
				   out->cap_adap + DP_STATUS_CTRL, 1);
		if (ret)
			return ret;
		if (!(val & DP_STATUS_CTRL_CMHS))
			return 0;
		usleep_range(10, 100);
	} while (timeout--);

	return -ETIMEDOUT;
}

static inline u32 tb_dp_cap_get_rate(u32 val)
{
	u32 rate = (val & DP_COMMON_CAP_RATE_MASK) >> DP_COMMON_CAP_RATE_SHIFT;

	switch (rate) {
	case DP_COMMON_CAP_RATE_RBR:
		return 1620;
	case DP_COMMON_CAP_RATE_HBR:
		return 2700;
	case DP_COMMON_CAP_RATE_HBR2:
		return 5400;
	case DP_COMMON_CAP_RATE_HBR3:
		return 8100;
	default:
		return 0;
	}
}

static inline u32 tb_dp_cap_set_rate(u32 val, u32 rate)
{
	val &= ~DP_COMMON_CAP_RATE_MASK;
	switch (rate) {
	default:
		WARN(1, "invalid rate %u passed, defaulting to 1620 MB/s\n", rate);
		fallthrough;
	case 1620:
		val |= DP_COMMON_CAP_RATE_RBR << DP_COMMON_CAP_RATE_SHIFT;
		break;
	case 2700:
		val |= DP_COMMON_CAP_RATE_HBR << DP_COMMON_CAP_RATE_SHIFT;
		break;
	case 5400:
		val |= DP_COMMON_CAP_RATE_HBR2 << DP_COMMON_CAP_RATE_SHIFT;
		break;
	case 8100:
		val |= DP_COMMON_CAP_RATE_HBR3 << DP_COMMON_CAP_RATE_SHIFT;
		break;
	}
	return val;
}

static inline u32 tb_dp_cap_get_lanes(u32 val)
{
	u32 lanes = (val & DP_COMMON_CAP_LANES_MASK) >> DP_COMMON_CAP_LANES_SHIFT;

	switch (lanes) {
	case DP_COMMON_CAP_1_LANE:
		return 1;
	case DP_COMMON_CAP_2_LANES:
		return 2;
	case DP_COMMON_CAP_4_LANES:
		return 4;
	default:
		return 0;
	}
}

static inline u32 tb_dp_cap_set_lanes(u32 val, u32 lanes)
{
	val &= ~DP_COMMON_CAP_LANES_MASK;
	switch (lanes) {
	default:
		WARN(1, "invalid number of lanes %u passed, defaulting to 1\n",
		     lanes);
		fallthrough;
	case 1:
		val |= DP_COMMON_CAP_1_LANE << DP_COMMON_CAP_LANES_SHIFT;
		break;
	case 2:
		val |= DP_COMMON_CAP_2_LANES << DP_COMMON_CAP_LANES_SHIFT;
		break;
	case 4:
		val |= DP_COMMON_CAP_4_LANES << DP_COMMON_CAP_LANES_SHIFT;
		break;
	}
	return val;
}

static unsigned int tb_dp_bandwidth(unsigned int rate, unsigned int lanes)
{
	/* Tunneling removes the DP 8b/10b encoding */
	return rate * lanes * 8 / 10;
}

static int tb_dp_reduce_bandwidth(int max_bw, u32 in_rate, u32 in_lanes,
				  u32 out_rate, u32 out_lanes, u32 *new_rate,
				  u32 *new_lanes)
{
	static const u32 dp_bw[][2] = {
		/* Mb/s, lanes */
		{ 8100, 4 }, /* 25920 Mb/s */
		{ 5400, 4 }, /* 17280 Mb/s */
		{ 8100, 2 }, /* 12960 Mb/s */
		{ 2700, 4 }, /* 8640 Mb/s */
		{ 5400, 2 }, /* 8640 Mb/s */
		{ 8100, 1 }, /* 6480 Mb/s */
		{ 1620, 4 }, /* 5184 Mb/s */
		{ 5400, 1 }, /* 4320 Mb/s */
		{ 2700, 2 }, /* 4320 Mb/s */
		{ 1620, 2 }, /* 2592 Mb/s */
		{ 2700, 1 }, /* 2160 Mb/s */
		{ 1620, 1 }, /* 1296 Mb/s */
	};
	unsigned int i;

	/*
	 * Find a combination that can fit into max_bw and does not
	 * exceed the maximum rate and lanes supported by the DP OUT and
	 * DP IN adapters.
	 */
	for (i = 0; i < ARRAY_SIZE(dp_bw); i++) {
		if (dp_bw[i][0] > out_rate || dp_bw[i][1] > out_lanes)
			continue;

		if (dp_bw[i][0] > in_rate || dp_bw[i][1] > in_lanes)
			continue;

		if (tb_dp_bandwidth(dp_bw[i][0], dp_bw[i][1]) <= max_bw) {
			*new_rate = dp_bw[i][0];
			*new_lanes = dp_bw[i][1];
			return 0;
		}
	}

	return -ENOSR;
}

static int tb_dp_xchg_caps(struct tb_tunnel *tunnel)
{
	u32 out_dp_cap, out_rate, out_lanes, in_dp_cap, in_rate, in_lanes, bw;
	struct tb_port *out = tunnel->dst_port;
	struct tb_port *in = tunnel->src_port;
	int ret, max_bw;

	/*
	 * Copy DP_LOCAL_CAP register to DP_REMOTE_CAP register for
	 * newer generation hardware.
	 */
	if (in->sw->generation < 2 || out->sw->generation < 2)
		return 0;

	/*
	 * Perform connection manager handshake between IN and OUT ports
	 * before capabilities exchange can take place.
	 */
	ret = tb_dp_cm_handshake(in, out);
	if (ret)
		return ret;

	/* Read both DP_LOCAL_CAP registers */
	ret = tb_port_read(in, &in_dp_cap, TB_CFG_PORT,
			   in->cap_adap + DP_LOCAL_CAP, 1);
	if (ret)
		return ret;

	ret = tb_port_read(out, &out_dp_cap, TB_CFG_PORT,
			   out->cap_adap + DP_LOCAL_CAP, 1);
	if (ret)
		return ret;

	/* Write IN local caps to OUT remote caps */
	ret = tb_port_write(out, &in_dp_cap, TB_CFG_PORT,
			    out->cap_adap + DP_REMOTE_CAP, 1);
	if (ret)
		return ret;

	in_rate = tb_dp_cap_get_rate(in_dp_cap);
	in_lanes = tb_dp_cap_get_lanes(in_dp_cap);
	tb_port_dbg(in, "maximum supported bandwidth %u Mb/s x%u = %u Mb/s\n",
		    in_rate, in_lanes, tb_dp_bandwidth(in_rate, in_lanes));

	/*
	 * If the tunnel bandwidth is limited (max_bw is set) then see
	 * if we need to reduce bandwidth to fit there.
	 */
	out_rate = tb_dp_cap_get_rate(out_dp_cap);
	out_lanes = tb_dp_cap_get_lanes(out_dp_cap);
	bw = tb_dp_bandwidth(out_rate, out_lanes);
	tb_port_dbg(out, "maximum supported bandwidth %u Mb/s x%u = %u Mb/s\n",
		    out_rate, out_lanes, bw);

	if (in->sw->config.depth < out->sw->config.depth)
		max_bw = tunnel->max_down;
	else
		max_bw = tunnel->max_up;

	if (max_bw && bw > max_bw) {
		u32 new_rate, new_lanes, new_bw;

		ret = tb_dp_reduce_bandwidth(max_bw, in_rate, in_lanes,
					     out_rate, out_lanes, &new_rate,
					     &new_lanes);
		if (ret) {
			tb_port_info(out, "not enough bandwidth for DP tunnel\n");
			return ret;
		}

		new_bw = tb_dp_bandwidth(new_rate, new_lanes);
		tb_port_dbg(out, "bandwidth reduced to %u Mb/s x%u = %u Mb/s\n",
			    new_rate, new_lanes, new_bw);

		/*
		 * Set new rate and number of lanes before writing it to
		 * the IN port remote caps.
		 */
		out_dp_cap = tb_dp_cap_set_rate(out_dp_cap, new_rate);
		out_dp_cap = tb_dp_cap_set_lanes(out_dp_cap, new_lanes);
	}

	return tb_port_write(in, &out_dp_cap, TB_CFG_PORT,
			     in->cap_adap + DP_REMOTE_CAP, 1);
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

static int tb_dp_consumed_bandwidth(struct tb_tunnel *tunnel, int *consumed_up,
				    int *consumed_down)
{
	struct tb_port *in = tunnel->src_port;
	const struct tb_switch *sw = in->sw;
	u32 val, rate = 0, lanes = 0;
	int ret;

	if (tb_dp_is_usb4(sw)) {
		int timeout = 20;

		/*
		 * Wait for DPRX done. Normally it should be already set
		 * for active tunnel.
		 */
		do {
			ret = tb_port_read(in, &val, TB_CFG_PORT,
					   in->cap_adap + DP_COMMON_CAP, 1);
			if (ret)
				return ret;

			if (val & DP_COMMON_CAP_DPRX_DONE) {
				rate = tb_dp_cap_get_rate(val);
				lanes = tb_dp_cap_get_lanes(val);
				break;
			}
			msleep(250);
		} while (timeout--);

		if (!timeout)
			return -ETIMEDOUT;
	} else if (sw->generation >= 2) {
		/*
		 * Read from the copied remote cap so that we take into
		 * account if capabilities were reduced during exchange.
		 */
		ret = tb_port_read(in, &val, TB_CFG_PORT,
				   in->cap_adap + DP_REMOTE_CAP, 1);
		if (ret)
			return ret;

		rate = tb_dp_cap_get_rate(val);
		lanes = tb_dp_cap_get_lanes(val);
	} else {
		/* No bandwidth management for legacy devices  */
		*consumed_up = 0;
		*consumed_down = 0;
		return 0;
	}

	if (in->sw->config.depth < tunnel->dst_port->sw->config.depth) {
		*consumed_up = 0;
		*consumed_down = tb_dp_bandwidth(rate, lanes);
	} else {
		*consumed_up = tb_dp_bandwidth(rate, lanes);
		*consumed_down = 0;
	}

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
		path->nfc_credits = nfc_credits & ADP_CS_4_NFC_BUFFERS_MASK;
	} else {
		u32 max_credits;

		max_credits = (nfc_credits & ADP_CS_4_TOTAL_BUFFERS_MASK) >>
			ADP_CS_4_TOTAL_BUFFERS_SHIFT;
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
	tunnel->consumed_bandwidth = tb_dp_consumed_bandwidth;
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
 * @max_up: Maximum available upstream bandwidth for the DP tunnel (%0
 *	    if not limited)
 * @max_down: Maximum available downstream bandwidth for the DP tunnel
 *	      (%0 if not limited)
 *
 * Allocates a tunnel between @in and @out that is capable of tunneling
 * Display Port traffic.
 *
 * Return: Returns a tb_tunnel on success or NULL on failure.
 */
struct tb_tunnel *tb_tunnel_alloc_dp(struct tb *tb, struct tb_port *in,
				     struct tb_port *out, int max_up,
				     int max_down)
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
	tunnel->consumed_bandwidth = tb_dp_consumed_bandwidth;
	tunnel->src_port = in;
	tunnel->dst_port = out;
	tunnel->max_up = max_up;
	tunnel->max_down = max_down;

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

	max_credits = (nhi->config.nfc_credits & ADP_CS_4_TOTAL_BUFFERS_MASK) >>
		ADP_CS_4_TOTAL_BUFFERS_SHIFT;
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

static int tb_usb3_max_link_rate(struct tb_port *up, struct tb_port *down)
{
	int ret, up_max_rate, down_max_rate;

	ret = usb4_usb3_port_max_link_rate(up);
	if (ret < 0)
		return ret;
	up_max_rate = ret;

	ret = usb4_usb3_port_max_link_rate(down);
	if (ret < 0)
		return ret;
	down_max_rate = ret;

	return min(up_max_rate, down_max_rate);
}

static int tb_usb3_init(struct tb_tunnel *tunnel)
{
	tb_tunnel_dbg(tunnel, "allocating initial bandwidth %d/%d Mb/s\n",
		      tunnel->allocated_up, tunnel->allocated_down);

	return usb4_usb3_port_allocate_bandwidth(tunnel->src_port,
						 &tunnel->allocated_up,
						 &tunnel->allocated_down);
}

static int tb_usb3_activate(struct tb_tunnel *tunnel, bool activate)
{
	int res;

	res = tb_usb3_port_enable(tunnel->src_port, activate);
	if (res)
		return res;

	if (tb_port_is_usb3_up(tunnel->dst_port))
		return tb_usb3_port_enable(tunnel->dst_port, activate);

	return 0;
}

static int tb_usb3_consumed_bandwidth(struct tb_tunnel *tunnel,
		int *consumed_up, int *consumed_down)
{
	/*
	 * PCIe tunneling affects the USB3 bandwidth so take that it
	 * into account here.
	 */
	*consumed_up = tunnel->allocated_up * (3 + 1) / 3;
	*consumed_down = tunnel->allocated_down * (3 + 1) / 3;
	return 0;
}

static int tb_usb3_release_unused_bandwidth(struct tb_tunnel *tunnel)
{
	int ret;

	ret = usb4_usb3_port_release_bandwidth(tunnel->src_port,
					       &tunnel->allocated_up,
					       &tunnel->allocated_down);
	if (ret)
		return ret;

	tb_tunnel_dbg(tunnel, "decreased bandwidth allocation to %d/%d Mb/s\n",
		      tunnel->allocated_up, tunnel->allocated_down);
	return 0;
}

static void tb_usb3_reclaim_available_bandwidth(struct tb_tunnel *tunnel,
						int *available_up,
						int *available_down)
{
	int ret, max_rate, allocate_up, allocate_down;

	ret = usb4_usb3_port_actual_link_rate(tunnel->src_port);
	if (ret <= 0) {
		tb_tunnel_warn(tunnel, "tunnel is not up\n");
		return;
	}
	/*
	 * 90% of the max rate can be allocated for isochronous
	 * transfers.
	 */
	max_rate = ret * 90 / 100;

	/* No need to reclaim if already at maximum */
	if (tunnel->allocated_up >= max_rate &&
	    tunnel->allocated_down >= max_rate)
		return;

	/* Don't go lower than what is already allocated */
	allocate_up = min(max_rate, *available_up);
	if (allocate_up < tunnel->allocated_up)
		allocate_up = tunnel->allocated_up;

	allocate_down = min(max_rate, *available_down);
	if (allocate_down < tunnel->allocated_down)
		allocate_down = tunnel->allocated_down;

	/* If no changes no need to do more */
	if (allocate_up == tunnel->allocated_up &&
	    allocate_down == tunnel->allocated_down)
		return;

	ret = usb4_usb3_port_allocate_bandwidth(tunnel->src_port, &allocate_up,
						&allocate_down);
	if (ret) {
		tb_tunnel_info(tunnel, "failed to allocate bandwidth\n");
		return;
	}

	tunnel->allocated_up = allocate_up;
	*available_up -= tunnel->allocated_up;

	tunnel->allocated_down = allocate_down;
	*available_down -= tunnel->allocated_down;

	tb_tunnel_dbg(tunnel, "increased bandwidth allocation to %d/%d Mb/s\n",
		      tunnel->allocated_up, tunnel->allocated_down);
}

static void tb_usb3_init_path(struct tb_path *path)
{
	path->egress_fc_enable = TB_PATH_SOURCE | TB_PATH_INTERNAL;
	path->egress_shared_buffer = TB_PATH_NONE;
	path->ingress_fc_enable = TB_PATH_ALL;
	path->ingress_shared_buffer = TB_PATH_NONE;
	path->priority = 3;
	path->weight = 3;
	path->drop_packages = 0;
	path->nfc_credits = 0;
	path->hops[0].initial_credits = 7;
	if (path->path_length > 1)
		path->hops[1].initial_credits =
			tb_initial_credits(path->hops[1].in_port->sw);
}

/**
 * tb_tunnel_discover_usb3() - Discover existing USB3 tunnels
 * @tb: Pointer to the domain structure
 * @down: USB3 downstream adapter
 *
 * If @down adapter is active, follows the tunnel to the USB3 upstream
 * adapter and back. Returns the discovered tunnel or %NULL if there was
 * no tunnel.
 */
struct tb_tunnel *tb_tunnel_discover_usb3(struct tb *tb, struct tb_port *down)
{
	struct tb_tunnel *tunnel;
	struct tb_path *path;

	if (!tb_usb3_port_is_enabled(down))
		return NULL;

	tunnel = tb_tunnel_alloc(tb, 2, TB_TUNNEL_USB3);
	if (!tunnel)
		return NULL;

	tunnel->activate = tb_usb3_activate;
	tunnel->src_port = down;

	/*
	 * Discover both paths even if they are not complete. We will
	 * clean them up by calling tb_tunnel_deactivate() below in that
	 * case.
	 */
	path = tb_path_discover(down, TB_USB3_HOPID, NULL, -1,
				&tunnel->dst_port, "USB3 Down");
	if (!path) {
		/* Just disable the downstream port */
		tb_usb3_port_enable(down, false);
		goto err_free;
	}
	tunnel->paths[TB_USB3_PATH_DOWN] = path;
	tb_usb3_init_path(tunnel->paths[TB_USB3_PATH_DOWN]);

	path = tb_path_discover(tunnel->dst_port, -1, down, TB_USB3_HOPID, NULL,
				"USB3 Up");
	if (!path)
		goto err_deactivate;
	tunnel->paths[TB_USB3_PATH_UP] = path;
	tb_usb3_init_path(tunnel->paths[TB_USB3_PATH_UP]);

	/* Validate that the tunnel is complete */
	if (!tb_port_is_usb3_up(tunnel->dst_port)) {
		tb_port_warn(tunnel->dst_port,
			     "path does not end on an USB3 adapter, cleaning up\n");
		goto err_deactivate;
	}

	if (down != tunnel->src_port) {
		tb_tunnel_warn(tunnel, "path is not complete, cleaning up\n");
		goto err_deactivate;
	}

	if (!tb_usb3_port_is_enabled(tunnel->dst_port)) {
		tb_tunnel_warn(tunnel,
			       "tunnel is not fully activated, cleaning up\n");
		goto err_deactivate;
	}

	if (!tb_route(down->sw)) {
		int ret;

		/*
		 * Read the initial bandwidth allocation for the first
		 * hop tunnel.
		 */
		ret = usb4_usb3_port_allocated_bandwidth(down,
			&tunnel->allocated_up, &tunnel->allocated_down);
		if (ret)
			goto err_deactivate;

		tb_tunnel_dbg(tunnel, "currently allocated bandwidth %d/%d Mb/s\n",
			      tunnel->allocated_up, tunnel->allocated_down);

		tunnel->init = tb_usb3_init;
		tunnel->consumed_bandwidth = tb_usb3_consumed_bandwidth;
		tunnel->release_unused_bandwidth =
			tb_usb3_release_unused_bandwidth;
		tunnel->reclaim_available_bandwidth =
			tb_usb3_reclaim_available_bandwidth;
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
 * tb_tunnel_alloc_usb3() - allocate a USB3 tunnel
 * @tb: Pointer to the domain structure
 * @up: USB3 upstream adapter port
 * @down: USB3 downstream adapter port
 * @max_up: Maximum available upstream bandwidth for the USB3 tunnel (%0
 *	    if not limited).
 * @max_down: Maximum available downstream bandwidth for the USB3 tunnel
 *	      (%0 if not limited).
 *
 * Allocate an USB3 tunnel. The ports must be of type @TB_TYPE_USB3_UP and
 * @TB_TYPE_USB3_DOWN.
 *
 * Return: Returns a tb_tunnel on success or %NULL on failure.
 */
struct tb_tunnel *tb_tunnel_alloc_usb3(struct tb *tb, struct tb_port *up,
				       struct tb_port *down, int max_up,
				       int max_down)
{
	struct tb_tunnel *tunnel;
	struct tb_path *path;
	int max_rate = 0;

	/*
	 * Check that we have enough bandwidth available for the new
	 * USB3 tunnel.
	 */
	if (max_up > 0 || max_down > 0) {
		max_rate = tb_usb3_max_link_rate(down, up);
		if (max_rate < 0)
			return NULL;

		/* Only 90% can be allocated for USB3 isochronous transfers */
		max_rate = max_rate * 90 / 100;
		tb_port_dbg(up, "required bandwidth for USB3 tunnel %d Mb/s\n",
			    max_rate);

		if (max_rate > max_up || max_rate > max_down) {
			tb_port_warn(up, "not enough bandwidth for USB3 tunnel\n");
			return NULL;
		}
	}

	tunnel = tb_tunnel_alloc(tb, 2, TB_TUNNEL_USB3);
	if (!tunnel)
		return NULL;

	tunnel->activate = tb_usb3_activate;
	tunnel->src_port = down;
	tunnel->dst_port = up;
	tunnel->max_up = max_up;
	tunnel->max_down = max_down;

	path = tb_path_alloc(tb, down, TB_USB3_HOPID, up, TB_USB3_HOPID, 0,
			     "USB3 Down");
	if (!path) {
		tb_tunnel_free(tunnel);
		return NULL;
	}
	tb_usb3_init_path(path);
	tunnel->paths[TB_USB3_PATH_DOWN] = path;

	path = tb_path_alloc(tb, up, TB_USB3_HOPID, down, TB_USB3_HOPID, 0,
			     "USB3 Up");
	if (!path) {
		tb_tunnel_free(tunnel);
		return NULL;
	}
	tb_usb3_init_path(path);
	tunnel->paths[TB_USB3_PATH_UP] = path;

	if (!tb_route(down->sw)) {
		tunnel->allocated_up = max_rate;
		tunnel->allocated_down = max_rate;

		tunnel->init = tb_usb3_init;
		tunnel->consumed_bandwidth = tb_usb3_consumed_bandwidth;
		tunnel->release_unused_bandwidth =
			tb_usb3_release_unused_bandwidth;
		tunnel->reclaim_available_bandwidth =
			tb_usb3_reclaim_available_bandwidth;
	}

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

/**
 * tb_tunnel_port_on_path() - Does the tunnel go through port
 * @tunnel: Tunnel to check
 * @port: Port to check
 *
 * Returns true if @tunnel goes through @port (direction does not matter),
 * false otherwise.
 */
bool tb_tunnel_port_on_path(const struct tb_tunnel *tunnel,
			    const struct tb_port *port)
{
	int i;

	for (i = 0; i < tunnel->npaths; i++) {
		if (!tunnel->paths[i])
			continue;

		if (tb_path_port_on_path(tunnel->paths[i], port))
			return true;
	}

	return false;
}

static bool tb_tunnel_is_active(const struct tb_tunnel *tunnel)
{
	int i;

	for (i = 0; i < tunnel->npaths; i++) {
		if (!tunnel->paths[i])
			return false;
		if (!tunnel->paths[i]->activated)
			return false;
	}

	return true;
}

/**
 * tb_tunnel_consumed_bandwidth() - Return bandwidth consumed by the tunnel
 * @tunnel: Tunnel to check
 * @consumed_up: Consumed bandwidth in Mb/s from @dst_port to @src_port.
 *		 Can be %NULL.
 * @consumed_down: Consumed bandwidth in Mb/s from @src_port to @dst_port.
 *		   Can be %NULL.
 *
 * Stores the amount of isochronous bandwidth @tunnel consumes in
 * @consumed_up and @consumed_down. In case of success returns %0,
 * negative errno otherwise.
 */
int tb_tunnel_consumed_bandwidth(struct tb_tunnel *tunnel, int *consumed_up,
				 int *consumed_down)
{
	int up_bw = 0, down_bw = 0;

	if (!tb_tunnel_is_active(tunnel))
		goto out;

	if (tunnel->consumed_bandwidth) {
		int ret;

		ret = tunnel->consumed_bandwidth(tunnel, &up_bw, &down_bw);
		if (ret)
			return ret;

		tb_tunnel_dbg(tunnel, "consumed bandwidth %d/%d Mb/s\n", up_bw,
			      down_bw);
	}

out:
	if (consumed_up)
		*consumed_up = up_bw;
	if (consumed_down)
		*consumed_down = down_bw;

	return 0;
}

/**
 * tb_tunnel_release_unused_bandwidth() - Release unused bandwidth
 * @tunnel: Tunnel whose unused bandwidth to release
 *
 * If tunnel supports dynamic bandwidth management (USB3 tunnels at the
 * moment) this function makes it to release all the unused bandwidth.
 *
 * Returns %0 in case of success and negative errno otherwise.
 */
int tb_tunnel_release_unused_bandwidth(struct tb_tunnel *tunnel)
{
	if (!tb_tunnel_is_active(tunnel))
		return 0;

	if (tunnel->release_unused_bandwidth) {
		int ret;

		ret = tunnel->release_unused_bandwidth(tunnel);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * tb_tunnel_reclaim_available_bandwidth() - Reclaim available bandwidth
 * @tunnel: Tunnel reclaiming available bandwidth
 * @available_up: Available upstream bandwidth (in Mb/s)
 * @available_down: Available downstream bandwidth (in Mb/s)
 *
 * Reclaims bandwidth from @available_up and @available_down and updates
 * the variables accordingly (e.g decreases both according to what was
 * reclaimed by the tunnel). If nothing was reclaimed the values are
 * kept as is.
 */
void tb_tunnel_reclaim_available_bandwidth(struct tb_tunnel *tunnel,
					   int *available_up,
					   int *available_down)
{
	if (!tb_tunnel_is_active(tunnel))
		return;

	if (tunnel->reclaim_available_bandwidth)
		tunnel->reclaim_available_bandwidth(tunnel, available_up,
						    available_down);
}
