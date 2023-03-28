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
#include <linux/ktime.h>

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

/* Minimum number of credits needed for PCIe path */
#define TB_MIN_PCIE_CREDITS		6U
/*
 * Number of credits we try to allocate for each DMA path if not limited
 * by the host router baMaxHI.
 */
#define TB_DMA_CREDITS			14U
/* Minimum number of credits for DMA path */
#define TB_MIN_DMA_CREDITS		1U

static bool bw_alloc_mode = true;
module_param(bw_alloc_mode, bool, 0444);
MODULE_PARM_DESC(bw_alloc_mode,
		 "enable bandwidth allocation mode if supported (default: true)");

static const char * const tb_tunnel_names[] = { "PCI", "DP", "DMA", "USB3" };

#define __TB_TUNNEL_PRINT(level, tunnel, fmt, arg...)                   \
	do {                                                            \
		struct tb_tunnel *__tunnel = (tunnel);                  \
		level(__tunnel->tb, "%llx:%u <-> %llx:%u (%s): " fmt,   \
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

static inline unsigned int tb_usable_credits(const struct tb_port *port)
{
	return port->total_credits - port->ctl_credits;
}

/**
 * tb_available_credits() - Available credits for PCIe and DMA
 * @port: Lane adapter to check
 * @max_dp_streams: If non-%NULL stores maximum number of simultaneous DP
 *		    streams possible through this lane adapter
 */
static unsigned int tb_available_credits(const struct tb_port *port,
					 size_t *max_dp_streams)
{
	const struct tb_switch *sw = port->sw;
	int credits, usb3, pcie, spare;
	size_t ndp;

	usb3 = tb_acpi_may_tunnel_usb3() ? sw->max_usb3_credits : 0;
	pcie = tb_acpi_may_tunnel_pcie() ? sw->max_pcie_credits : 0;

	if (tb_acpi_is_xdomain_allowed()) {
		spare = min_not_zero(sw->max_dma_credits, TB_DMA_CREDITS);
		/* Add some credits for potential second DMA tunnel */
		spare += TB_MIN_DMA_CREDITS;
	} else {
		spare = 0;
	}

	credits = tb_usable_credits(port);
	if (tb_acpi_may_tunnel_dp()) {
		/*
		 * Maximum number of DP streams possible through the
		 * lane adapter.
		 */
		if (sw->min_dp_aux_credits + sw->min_dp_main_credits)
			ndp = (credits - (usb3 + pcie + spare)) /
			      (sw->min_dp_aux_credits + sw->min_dp_main_credits);
		else
			ndp = 0;
	} else {
		ndp = 0;
	}
	credits -= ndp * (sw->min_dp_aux_credits + sw->min_dp_main_credits);
	credits -= usb3;

	if (max_dp_streams)
		*max_dp_streams = ndp;

	return credits > 0 ? credits : 0;
}

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

static int tb_pci_init_credits(struct tb_path_hop *hop)
{
	struct tb_port *port = hop->in_port;
	struct tb_switch *sw = port->sw;
	unsigned int credits;

	if (tb_port_use_credit_allocation(port)) {
		unsigned int available;

		available = tb_available_credits(port, NULL);
		credits = min(sw->max_pcie_credits, available);

		if (credits < TB_MIN_PCIE_CREDITS)
			return -ENOSPC;

		credits = max(TB_MIN_PCIE_CREDITS, credits);
	} else {
		if (tb_port_is_null(port))
			credits = port->bonded ? 32 : 16;
		else
			credits = 7;
	}

	hop->initial_credits = credits;
	return 0;
}

static int tb_pci_init_path(struct tb_path *path)
{
	struct tb_path_hop *hop;

	path->egress_fc_enable = TB_PATH_SOURCE | TB_PATH_INTERNAL;
	path->egress_shared_buffer = TB_PATH_NONE;
	path->ingress_fc_enable = TB_PATH_ALL;
	path->ingress_shared_buffer = TB_PATH_NONE;
	path->priority = 3;
	path->weight = 1;
	path->drop_packages = 0;

	tb_path_for_each_hop(path, hop) {
		int ret;

		ret = tb_pci_init_credits(hop);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * tb_tunnel_discover_pci() - Discover existing PCIe tunnels
 * @tb: Pointer to the domain structure
 * @down: PCIe downstream adapter
 * @alloc_hopid: Allocate HopIDs from visited ports
 *
 * If @down adapter is active, follows the tunnel to the PCIe upstream
 * adapter and back. Returns the discovered tunnel or %NULL if there was
 * no tunnel.
 */
struct tb_tunnel *tb_tunnel_discover_pci(struct tb *tb, struct tb_port *down,
					 bool alloc_hopid)
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
				&tunnel->dst_port, "PCIe Up", alloc_hopid);
	if (!path) {
		/* Just disable the downstream port */
		tb_pci_port_enable(down, false);
		goto err_free;
	}
	tunnel->paths[TB_PCI_PATH_UP] = path;
	if (tb_pci_init_path(tunnel->paths[TB_PCI_PATH_UP]))
		goto err_free;

	path = tb_path_discover(tunnel->dst_port, -1, down, TB_PCI_HOPID, NULL,
				"PCIe Down", alloc_hopid);
	if (!path)
		goto err_deactivate;
	tunnel->paths[TB_PCI_PATH_DOWN] = path;
	if (tb_pci_init_path(tunnel->paths[TB_PCI_PATH_DOWN]))
		goto err_deactivate;

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
	if (!path)
		goto err_free;
	tunnel->paths[TB_PCI_PATH_DOWN] = path;
	if (tb_pci_init_path(path))
		goto err_free;

	path = tb_path_alloc(tb, up, TB_PCI_HOPID, down, TB_PCI_HOPID, 0,
			     "PCIe Up");
	if (!path)
		goto err_free;
	tunnel->paths[TB_PCI_PATH_UP] = path;
	if (tb_pci_init_path(path))
		goto err_free;

	return tunnel;

err_free:
	tb_tunnel_free(tunnel);
	return NULL;
}

static bool tb_dp_is_usb4(const struct tb_switch *sw)
{
	/* Titan Ridge DP adapters need the same treatment as USB4 */
	return tb_switch_is_usb4(sw) || tb_switch_is_titan_ridge(sw);
}

static int tb_dp_cm_handshake(struct tb_port *in, struct tb_port *out,
			      int timeout_msec)
{
	ktime_t timeout = ktime_add_ms(ktime_get(), timeout_msec);
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
		usleep_range(100, 150);
	} while (ktime_before(ktime_get(), timeout));

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
	ret = tb_dp_cm_handshake(in, out, 1500);
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

	/*
	 * Titan Ridge does not disable AUX timers when it gets
	 * SET_CONFIG with SET_LTTPR_MODE set. This causes problems with
	 * DP tunneling.
	 */
	if (tb_route(out->sw) && tb_switch_is_titan_ridge(out->sw)) {
		out_dp_cap |= DP_COMMON_CAP_LTTPR_NS;
		tb_port_dbg(out, "disabling LTTPR\n");
	}

	return tb_port_write(in, &out_dp_cap, TB_CFG_PORT,
			     in->cap_adap + DP_REMOTE_CAP, 1);
}

static int tb_dp_bw_alloc_mode_enable(struct tb_tunnel *tunnel)
{
	int ret, estimated_bw, granularity, tmp;
	struct tb_port *out = tunnel->dst_port;
	struct tb_port *in = tunnel->src_port;
	u32 out_dp_cap, out_rate, out_lanes;
	u32 in_dp_cap, in_rate, in_lanes;
	u32 rate, lanes;

	if (!bw_alloc_mode)
		return 0;

	ret = usb4_dp_port_set_cm_bw_mode_supported(in, true);
	if (ret)
		return ret;

	ret = usb4_dp_port_set_group_id(in, in->group->index);
	if (ret)
		return ret;

	/*
	 * Get the non-reduced rate and lanes based on the lowest
	 * capability of both adapters.
	 */
	ret = tb_port_read(in, &in_dp_cap, TB_CFG_PORT,
			   in->cap_adap + DP_LOCAL_CAP, 1);
	if (ret)
		return ret;

	ret = tb_port_read(out, &out_dp_cap, TB_CFG_PORT,
			   out->cap_adap + DP_LOCAL_CAP, 1);
	if (ret)
		return ret;

	in_rate = tb_dp_cap_get_rate(in_dp_cap);
	in_lanes = tb_dp_cap_get_lanes(in_dp_cap);
	out_rate = tb_dp_cap_get_rate(out_dp_cap);
	out_lanes = tb_dp_cap_get_lanes(out_dp_cap);

	rate = min(in_rate, out_rate);
	lanes = min(in_lanes, out_lanes);
	tmp = tb_dp_bandwidth(rate, lanes);

	tb_port_dbg(in, "non-reduced bandwidth %u Mb/s x%u = %u Mb/s\n", rate,
		    lanes, tmp);

	ret = usb4_dp_port_set_nrd(in, rate, lanes);
	if (ret)
		return ret;

	for (granularity = 250; tmp / granularity > 255 && granularity <= 1000;
	     granularity *= 2)
		;

	tb_port_dbg(in, "granularity %d Mb/s\n", granularity);

	/*
	 * Returns -EINVAL if granularity above is outside of the
	 * accepted ranges.
	 */
	ret = usb4_dp_port_set_granularity(in, granularity);
	if (ret)
		return ret;

	/*
	 * Bandwidth estimation is pretty much what we have in
	 * max_up/down fields. For discovery we just read what the
	 * estimation was set to.
	 */
	if (in->sw->config.depth < out->sw->config.depth)
		estimated_bw = tunnel->max_down;
	else
		estimated_bw = tunnel->max_up;

	tb_port_dbg(in, "estimated bandwidth %d Mb/s\n", estimated_bw);

	ret = usb4_dp_port_set_estimated_bw(in, estimated_bw);
	if (ret)
		return ret;

	/* Initial allocation should be 0 according the spec */
	ret = usb4_dp_port_allocate_bw(in, 0);
	if (ret)
		return ret;

	tb_port_dbg(in, "bandwidth allocation mode enabled\n");
	return 0;
}

static int tb_dp_init(struct tb_tunnel *tunnel)
{
	struct tb_port *in = tunnel->src_port;
	struct tb_switch *sw = in->sw;
	struct tb *tb = in->sw->tb;
	int ret;

	ret = tb_dp_xchg_caps(tunnel);
	if (ret)
		return ret;

	if (!tb_switch_is_usb4(sw))
		return 0;

	if (!usb4_dp_port_bw_mode_supported(in))
		return 0;

	tb_port_dbg(in, "bandwidth allocation mode supported\n");

	ret = usb4_dp_port_set_cm_id(in, tb->index);
	if (ret)
		return ret;

	return tb_dp_bw_alloc_mode_enable(tunnel);
}

static void tb_dp_deinit(struct tb_tunnel *tunnel)
{
	struct tb_port *in = tunnel->src_port;

	if (!usb4_dp_port_bw_mode_supported(in))
		return;
	if (usb4_dp_port_bw_mode_enabled(in)) {
		usb4_dp_port_set_cm_bw_mode_supported(in, false);
		tb_port_dbg(in, "bandwidth allocation mode disabled\n");
	}
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

/* max_bw is rounded up to next granularity */
static int tb_dp_nrd_bandwidth(struct tb_tunnel *tunnel, int *max_bw)
{
	struct tb_port *in = tunnel->src_port;
	int ret, rate, lanes, nrd_bw;

	ret = usb4_dp_port_nrd(in, &rate, &lanes);
	if (ret)
		return ret;

	nrd_bw = tb_dp_bandwidth(rate, lanes);

	if (max_bw) {
		ret = usb4_dp_port_granularity(in);
		if (ret < 0)
			return ret;
		*max_bw = roundup(nrd_bw, ret);
	}

	return nrd_bw;
}

static int tb_dp_bw_mode_consumed_bandwidth(struct tb_tunnel *tunnel,
					    int *consumed_up, int *consumed_down)
{
	struct tb_port *out = tunnel->dst_port;
	struct tb_port *in = tunnel->src_port;
	int ret, allocated_bw, max_bw;

	if (!usb4_dp_port_bw_mode_enabled(in))
		return -EOPNOTSUPP;

	if (!tunnel->bw_mode)
		return -EOPNOTSUPP;

	/* Read what was allocated previously if any */
	ret = usb4_dp_port_allocated_bw(in);
	if (ret < 0)
		return ret;
	allocated_bw = ret;

	ret = tb_dp_nrd_bandwidth(tunnel, &max_bw);
	if (ret < 0)
		return ret;
	if (allocated_bw == max_bw)
		allocated_bw = ret;

	tb_port_dbg(in, "consumed bandwidth through allocation mode %d Mb/s\n",
		    allocated_bw);

	if (in->sw->config.depth < out->sw->config.depth) {
		*consumed_up = 0;
		*consumed_down = allocated_bw;
	} else {
		*consumed_up = allocated_bw;
		*consumed_down = 0;
	}

	return 0;
}

static int tb_dp_allocated_bandwidth(struct tb_tunnel *tunnel, int *allocated_up,
				     int *allocated_down)
{
	struct tb_port *out = tunnel->dst_port;
	struct tb_port *in = tunnel->src_port;

	/*
	 * If we have already set the allocated bandwidth then use that.
	 * Otherwise we read it from the DPRX.
	 */
	if (usb4_dp_port_bw_mode_enabled(in) && tunnel->bw_mode) {
		int ret, allocated_bw, max_bw;

		ret = usb4_dp_port_allocated_bw(in);
		if (ret < 0)
			return ret;
		allocated_bw = ret;

		ret = tb_dp_nrd_bandwidth(tunnel, &max_bw);
		if (ret < 0)
			return ret;
		if (allocated_bw == max_bw)
			allocated_bw = ret;

		if (in->sw->config.depth < out->sw->config.depth) {
			*allocated_up = 0;
			*allocated_down = allocated_bw;
		} else {
			*allocated_up = allocated_bw;
			*allocated_down = 0;
		}
		return 0;
	}

	return tunnel->consumed_bandwidth(tunnel, allocated_up,
					  allocated_down);
}

static int tb_dp_alloc_bandwidth(struct tb_tunnel *tunnel, int *alloc_up,
				 int *alloc_down)
{
	struct tb_port *out = tunnel->dst_port;
	struct tb_port *in = tunnel->src_port;
	int max_bw, ret, tmp;

	if (!usb4_dp_port_bw_mode_enabled(in))
		return -EOPNOTSUPP;

	ret = tb_dp_nrd_bandwidth(tunnel, &max_bw);
	if (ret < 0)
		return ret;

	if (in->sw->config.depth < out->sw->config.depth) {
		tmp = min(*alloc_down, max_bw);
		ret = usb4_dp_port_allocate_bw(in, tmp);
		if (ret)
			return ret;
		*alloc_down = tmp;
		*alloc_up = 0;
	} else {
		tmp = min(*alloc_up, max_bw);
		ret = usb4_dp_port_allocate_bw(in, tmp);
		if (ret)
			return ret;
		*alloc_down = 0;
		*alloc_up = tmp;
	}

	/* Now we can use BW mode registers to figure out the bandwidth */
	/* TODO: need to handle discovery too */
	tunnel->bw_mode = true;
	return 0;
}

static int tb_dp_read_dprx(struct tb_tunnel *tunnel, u32 *rate, u32 *lanes,
			   int timeout_msec)
{
	ktime_t timeout = ktime_add_ms(ktime_get(), timeout_msec);
	struct tb_port *in = tunnel->src_port;

	/*
	 * Wait for DPRX done. Normally it should be already set for
	 * active tunnel.
	 */
	do {
		u32 val;
		int ret;

		ret = tb_port_read(in, &val, TB_CFG_PORT,
				   in->cap_adap + DP_COMMON_CAP, 1);
		if (ret)
			return ret;

		if (val & DP_COMMON_CAP_DPRX_DONE) {
			*rate = tb_dp_cap_get_rate(val);
			*lanes = tb_dp_cap_get_lanes(val);

			tb_port_dbg(in, "consumed bandwidth through DPRX %d Mb/s\n",
				    tb_dp_bandwidth(*rate, *lanes));
			return 0;
		}
		usleep_range(100, 150);
	} while (ktime_before(ktime_get(), timeout));

	return -ETIMEDOUT;
}

/* Read cap from tunnel DP IN */
static int tb_dp_read_cap(struct tb_tunnel *tunnel, unsigned int cap, u32 *rate,
			  u32 *lanes)
{
	struct tb_port *in = tunnel->src_port;
	u32 val;
	int ret;

	switch (cap) {
	case DP_LOCAL_CAP:
	case DP_REMOTE_CAP:
		break;

	default:
		tb_tunnel_WARN(tunnel, "invalid capability index %#x\n", cap);
		return -EINVAL;
	}

	/*
	 * Read from the copied remote cap so that we take into account
	 * if capabilities were reduced during exchange.
	 */
	ret = tb_port_read(in, &val, TB_CFG_PORT, in->cap_adap + cap, 1);
	if (ret)
		return ret;

	*rate = tb_dp_cap_get_rate(val);
	*lanes = tb_dp_cap_get_lanes(val);

	tb_port_dbg(in, "bandwidth from %#x capability %d Mb/s\n", cap,
		    tb_dp_bandwidth(*rate, *lanes));
	return 0;
}

static int tb_dp_maximum_bandwidth(struct tb_tunnel *tunnel, int *max_up,
				   int *max_down)
{
	struct tb_port *in = tunnel->src_port;
	u32 rate, lanes;
	int ret;

	/*
	 * DP IN adapter DP_LOCAL_CAP gets updated to the lowest AUX read
	 * parameter values so this so we can use this to determine the
	 * maximum possible bandwidth over this link.
	 */
	ret = tb_dp_read_cap(tunnel, DP_LOCAL_CAP, &rate, &lanes);
	if (ret)
		return ret;

	if (in->sw->config.depth < tunnel->dst_port->sw->config.depth) {
		*max_up = 0;
		*max_down = tb_dp_bandwidth(rate, lanes);
	} else {
		*max_up = tb_dp_bandwidth(rate, lanes);
		*max_down = 0;
	}

	return 0;
}

static int tb_dp_consumed_bandwidth(struct tb_tunnel *tunnel, int *consumed_up,
				    int *consumed_down)
{
	struct tb_port *in = tunnel->src_port;
	const struct tb_switch *sw = in->sw;
	u32 rate = 0, lanes = 0;
	int ret;

	if (tb_dp_is_usb4(sw)) {
		/*
		 * On USB4 routers check if the bandwidth allocation
		 * mode is enabled first and then read the bandwidth
		 * through those registers.
		 */
		ret = tb_dp_bw_mode_consumed_bandwidth(tunnel, consumed_up,
						       consumed_down);
		if (ret < 0) {
			if (ret != -EOPNOTSUPP)
				return ret;
		} else if (!ret) {
			return 0;
		}
		/*
		 * Then see if the DPRX negotiation is ready and if yes
		 * return that bandwidth (it may be smaller than the
		 * reduced one). Otherwise return the remote (possibly
		 * reduced) caps.
		 */
		ret = tb_dp_read_dprx(tunnel, &rate, &lanes, 150);
		if (ret) {
			if (ret == -ETIMEDOUT)
				ret = tb_dp_read_cap(tunnel, DP_REMOTE_CAP,
						     &rate, &lanes);
			if (ret)
				return ret;
		}
	} else if (sw->generation >= 2) {
		ret = tb_dp_read_cap(tunnel, DP_REMOTE_CAP, &rate, &lanes);
		if (ret)
			return ret;
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

static void tb_dp_init_aux_credits(struct tb_path_hop *hop)
{
	struct tb_port *port = hop->in_port;
	struct tb_switch *sw = port->sw;

	if (tb_port_use_credit_allocation(port))
		hop->initial_credits = sw->min_dp_aux_credits;
	else
		hop->initial_credits = 1;
}

static void tb_dp_init_aux_path(struct tb_path *path)
{
	struct tb_path_hop *hop;

	path->egress_fc_enable = TB_PATH_SOURCE | TB_PATH_INTERNAL;
	path->egress_shared_buffer = TB_PATH_NONE;
	path->ingress_fc_enable = TB_PATH_ALL;
	path->ingress_shared_buffer = TB_PATH_NONE;
	path->priority = 2;
	path->weight = 1;

	tb_path_for_each_hop(path, hop)
		tb_dp_init_aux_credits(hop);
}

static int tb_dp_init_video_credits(struct tb_path_hop *hop)
{
	struct tb_port *port = hop->in_port;
	struct tb_switch *sw = port->sw;

	if (tb_port_use_credit_allocation(port)) {
		unsigned int nfc_credits;
		size_t max_dp_streams;

		tb_available_credits(port, &max_dp_streams);
		/*
		 * Read the number of currently allocated NFC credits
		 * from the lane adapter. Since we only use them for DP
		 * tunneling we can use that to figure out how many DP
		 * tunnels already go through the lane adapter.
		 */
		nfc_credits = port->config.nfc_credits &
				ADP_CS_4_NFC_BUFFERS_MASK;
		if (nfc_credits / sw->min_dp_main_credits > max_dp_streams)
			return -ENOSPC;

		hop->nfc_credits = sw->min_dp_main_credits;
	} else {
		hop->nfc_credits = min(port->total_credits - 2, 12U);
	}

	return 0;
}

static int tb_dp_init_video_path(struct tb_path *path)
{
	struct tb_path_hop *hop;

	path->egress_fc_enable = TB_PATH_NONE;
	path->egress_shared_buffer = TB_PATH_NONE;
	path->ingress_fc_enable = TB_PATH_NONE;
	path->ingress_shared_buffer = TB_PATH_NONE;
	path->priority = 1;
	path->weight = 1;

	tb_path_for_each_hop(path, hop) {
		int ret;

		ret = tb_dp_init_video_credits(hop);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * tb_tunnel_discover_dp() - Discover existing Display Port tunnels
 * @tb: Pointer to the domain structure
 * @in: DP in adapter
 * @alloc_hopid: Allocate HopIDs from visited ports
 *
 * If @in adapter is active, follows the tunnel to the DP out adapter
 * and back. Returns the discovered tunnel or %NULL if there was no
 * tunnel.
 *
 * Return: DP tunnel or %NULL if no tunnel found.
 */
struct tb_tunnel *tb_tunnel_discover_dp(struct tb *tb, struct tb_port *in,
					bool alloc_hopid)
{
	struct tb_tunnel *tunnel;
	struct tb_port *port;
	struct tb_path *path;

	if (!tb_dp_port_is_enabled(in))
		return NULL;

	tunnel = tb_tunnel_alloc(tb, 3, TB_TUNNEL_DP);
	if (!tunnel)
		return NULL;

	tunnel->init = tb_dp_init;
	tunnel->deinit = tb_dp_deinit;
	tunnel->activate = tb_dp_activate;
	tunnel->maximum_bandwidth = tb_dp_maximum_bandwidth;
	tunnel->allocated_bandwidth = tb_dp_allocated_bandwidth;
	tunnel->alloc_bandwidth = tb_dp_alloc_bandwidth;
	tunnel->consumed_bandwidth = tb_dp_consumed_bandwidth;
	tunnel->src_port = in;

	path = tb_path_discover(in, TB_DP_VIDEO_HOPID, NULL, -1,
				&tunnel->dst_port, "Video", alloc_hopid);
	if (!path) {
		/* Just disable the DP IN port */
		tb_dp_port_enable(in, false);
		goto err_free;
	}
	tunnel->paths[TB_DP_VIDEO_PATH_OUT] = path;
	if (tb_dp_init_video_path(tunnel->paths[TB_DP_VIDEO_PATH_OUT]))
		goto err_free;

	path = tb_path_discover(in, TB_DP_AUX_TX_HOPID, NULL, -1, NULL, "AUX TX",
				alloc_hopid);
	if (!path)
		goto err_deactivate;
	tunnel->paths[TB_DP_AUX_PATH_OUT] = path;
	tb_dp_init_aux_path(tunnel->paths[TB_DP_AUX_PATH_OUT]);

	path = tb_path_discover(tunnel->dst_port, -1, in, TB_DP_AUX_RX_HOPID,
				&port, "AUX RX", alloc_hopid);
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
 * @link_nr: Preferred lane adapter when the link is not bonded
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
				     struct tb_port *out, int link_nr,
				     int max_up, int max_down)
{
	struct tb_tunnel *tunnel;
	struct tb_path **paths;
	struct tb_path *path;

	if (WARN_ON(!in->cap_adap || !out->cap_adap))
		return NULL;

	tunnel = tb_tunnel_alloc(tb, 3, TB_TUNNEL_DP);
	if (!tunnel)
		return NULL;

	tunnel->init = tb_dp_init;
	tunnel->deinit = tb_dp_deinit;
	tunnel->activate = tb_dp_activate;
	tunnel->maximum_bandwidth = tb_dp_maximum_bandwidth;
	tunnel->allocated_bandwidth = tb_dp_allocated_bandwidth;
	tunnel->alloc_bandwidth = tb_dp_alloc_bandwidth;
	tunnel->consumed_bandwidth = tb_dp_consumed_bandwidth;
	tunnel->src_port = in;
	tunnel->dst_port = out;
	tunnel->max_up = max_up;
	tunnel->max_down = max_down;

	paths = tunnel->paths;

	path = tb_path_alloc(tb, in, TB_DP_VIDEO_HOPID, out, TB_DP_VIDEO_HOPID,
			     link_nr, "Video");
	if (!path)
		goto err_free;
	tb_dp_init_video_path(path);
	paths[TB_DP_VIDEO_PATH_OUT] = path;

	path = tb_path_alloc(tb, in, TB_DP_AUX_TX_HOPID, out,
			     TB_DP_AUX_TX_HOPID, link_nr, "AUX TX");
	if (!path)
		goto err_free;
	tb_dp_init_aux_path(path);
	paths[TB_DP_AUX_PATH_OUT] = path;

	path = tb_path_alloc(tb, out, TB_DP_AUX_RX_HOPID, in,
			     TB_DP_AUX_RX_HOPID, link_nr, "AUX RX");
	if (!path)
		goto err_free;
	tb_dp_init_aux_path(path);
	paths[TB_DP_AUX_PATH_IN] = path;

	return tunnel;

err_free:
	tb_tunnel_free(tunnel);
	return NULL;
}

static unsigned int tb_dma_available_credits(const struct tb_port *port)
{
	const struct tb_switch *sw = port->sw;
	int credits;

	credits = tb_available_credits(port, NULL);
	if (tb_acpi_may_tunnel_pcie())
		credits -= sw->max_pcie_credits;
	credits -= port->dma_credits;

	return credits > 0 ? credits : 0;
}

static int tb_dma_reserve_credits(struct tb_path_hop *hop, unsigned int credits)
{
	struct tb_port *port = hop->in_port;

	if (tb_port_use_credit_allocation(port)) {
		unsigned int available = tb_dma_available_credits(port);

		/*
		 * Need to have at least TB_MIN_DMA_CREDITS, otherwise
		 * DMA path cannot be established.
		 */
		if (available < TB_MIN_DMA_CREDITS)
			return -ENOSPC;

		while (credits > available)
			credits--;

		tb_port_dbg(port, "reserving %u credits for DMA path\n",
			    credits);

		port->dma_credits += credits;
	} else {
		if (tb_port_is_null(port))
			credits = port->bonded ? 14 : 6;
		else
			credits = min(port->total_credits, credits);
	}

	hop->initial_credits = credits;
	return 0;
}

/* Path from lane adapter to NHI */
static int tb_dma_init_rx_path(struct tb_path *path, unsigned int credits)
{
	struct tb_path_hop *hop;
	unsigned int i, tmp;

	path->egress_fc_enable = TB_PATH_SOURCE | TB_PATH_INTERNAL;
	path->ingress_fc_enable = TB_PATH_ALL;
	path->egress_shared_buffer = TB_PATH_NONE;
	path->ingress_shared_buffer = TB_PATH_NONE;
	path->priority = 5;
	path->weight = 1;
	path->clear_fc = true;

	/*
	 * First lane adapter is the one connected to the remote host.
	 * We don't tunnel other traffic over this link so can use all
	 * the credits (except the ones reserved for control traffic).
	 */
	hop = &path->hops[0];
	tmp = min(tb_usable_credits(hop->in_port), credits);
	hop->initial_credits = tmp;
	hop->in_port->dma_credits += tmp;

	for (i = 1; i < path->path_length; i++) {
		int ret;

		ret = tb_dma_reserve_credits(&path->hops[i], credits);
		if (ret)
			return ret;
	}

	return 0;
}

/* Path from NHI to lane adapter */
static int tb_dma_init_tx_path(struct tb_path *path, unsigned int credits)
{
	struct tb_path_hop *hop;

	path->egress_fc_enable = TB_PATH_ALL;
	path->ingress_fc_enable = TB_PATH_ALL;
	path->egress_shared_buffer = TB_PATH_NONE;
	path->ingress_shared_buffer = TB_PATH_NONE;
	path->priority = 5;
	path->weight = 1;
	path->clear_fc = true;

	tb_path_for_each_hop(path, hop) {
		int ret;

		ret = tb_dma_reserve_credits(hop, credits);
		if (ret)
			return ret;
	}

	return 0;
}

static void tb_dma_release_credits(struct tb_path_hop *hop)
{
	struct tb_port *port = hop->in_port;

	if (tb_port_use_credit_allocation(port)) {
		port->dma_credits -= hop->initial_credits;

		tb_port_dbg(port, "released %u DMA path credits\n",
			    hop->initial_credits);
	}
}

static void tb_dma_deinit_path(struct tb_path *path)
{
	struct tb_path_hop *hop;

	tb_path_for_each_hop(path, hop)
		tb_dma_release_credits(hop);
}

static void tb_dma_deinit(struct tb_tunnel *tunnel)
{
	int i;

	for (i = 0; i < tunnel->npaths; i++) {
		if (!tunnel->paths[i])
			continue;
		tb_dma_deinit_path(tunnel->paths[i]);
	}
}

/**
 * tb_tunnel_alloc_dma() - allocate a DMA tunnel
 * @tb: Pointer to the domain structure
 * @nhi: Host controller port
 * @dst: Destination null port which the other domain is connected to
 * @transmit_path: HopID used for transmitting packets
 * @transmit_ring: NHI ring number used to send packets towards the
 *		   other domain. Set to %-1 if TX path is not needed.
 * @receive_path: HopID used for receiving packets
 * @receive_ring: NHI ring number used to receive packets from the
 *		  other domain. Set to %-1 if RX path is not needed.
 *
 * Return: Returns a tb_tunnel on success or NULL on failure.
 */
struct tb_tunnel *tb_tunnel_alloc_dma(struct tb *tb, struct tb_port *nhi,
				      struct tb_port *dst, int transmit_path,
				      int transmit_ring, int receive_path,
				      int receive_ring)
{
	struct tb_tunnel *tunnel;
	size_t npaths = 0, i = 0;
	struct tb_path *path;
	int credits;

	if (receive_ring > 0)
		npaths++;
	if (transmit_ring > 0)
		npaths++;

	if (WARN_ON(!npaths))
		return NULL;

	tunnel = tb_tunnel_alloc(tb, npaths, TB_TUNNEL_DMA);
	if (!tunnel)
		return NULL;

	tunnel->src_port = nhi;
	tunnel->dst_port = dst;
	tunnel->deinit = tb_dma_deinit;

	credits = min_not_zero(TB_DMA_CREDITS, nhi->sw->max_dma_credits);

	if (receive_ring > 0) {
		path = tb_path_alloc(tb, dst, receive_path, nhi, receive_ring, 0,
				     "DMA RX");
		if (!path)
			goto err_free;
		tunnel->paths[i++] = path;
		if (tb_dma_init_rx_path(path, credits)) {
			tb_tunnel_dbg(tunnel, "not enough buffers for RX path\n");
			goto err_free;
		}
	}

	if (transmit_ring > 0) {
		path = tb_path_alloc(tb, nhi, transmit_ring, dst, transmit_path, 0,
				     "DMA TX");
		if (!path)
			goto err_free;
		tunnel->paths[i++] = path;
		if (tb_dma_init_tx_path(path, credits)) {
			tb_tunnel_dbg(tunnel, "not enough buffers for TX path\n");
			goto err_free;
		}
	}

	return tunnel;

err_free:
	tb_tunnel_free(tunnel);
	return NULL;
}

/**
 * tb_tunnel_match_dma() - Match DMA tunnel
 * @tunnel: Tunnel to match
 * @transmit_path: HopID used for transmitting packets. Pass %-1 to ignore.
 * @transmit_ring: NHI ring number used to send packets towards the
 *		   other domain. Pass %-1 to ignore.
 * @receive_path: HopID used for receiving packets. Pass %-1 to ignore.
 * @receive_ring: NHI ring number used to receive packets from the
 *		  other domain. Pass %-1 to ignore.
 *
 * This function can be used to match specific DMA tunnel, if there are
 * multiple DMA tunnels going through the same XDomain connection.
 * Returns true if there is match and false otherwise.
 */
bool tb_tunnel_match_dma(const struct tb_tunnel *tunnel, int transmit_path,
			 int transmit_ring, int receive_path, int receive_ring)
{
	const struct tb_path *tx_path = NULL, *rx_path = NULL;
	int i;

	if (!receive_ring || !transmit_ring)
		return false;

	for (i = 0; i < tunnel->npaths; i++) {
		const struct tb_path *path = tunnel->paths[i];

		if (!path)
			continue;

		if (tb_port_is_nhi(path->hops[0].in_port))
			tx_path = path;
		else if (tb_port_is_nhi(path->hops[path->path_length - 1].out_port))
			rx_path = path;
	}

	if (transmit_ring > 0 || transmit_path > 0) {
		if (!tx_path)
			return false;
		if (transmit_ring > 0 &&
		    (tx_path->hops[0].in_hop_index != transmit_ring))
			return false;
		if (transmit_path > 0 &&
		    (tx_path->hops[tx_path->path_length - 1].next_hop_index != transmit_path))
			return false;
	}

	if (receive_ring > 0 || receive_path > 0) {
		if (!rx_path)
			return false;
		if (receive_path > 0 &&
		    (rx_path->hops[0].in_hop_index != receive_path))
			return false;
		if (receive_ring > 0 &&
		    (rx_path->hops[rx_path->path_length - 1].next_hop_index != receive_ring))
			return false;
	}

	return true;
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
	int pcie_enabled = tb_acpi_may_tunnel_pcie();

	/*
	 * PCIe tunneling, if enabled, affects the USB3 bandwidth so
	 * take that it into account here.
	 */
	*consumed_up = tunnel->allocated_up * (3 + pcie_enabled) / 3;
	*consumed_down = tunnel->allocated_down * (3 + pcie_enabled) / 3;
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
	if (ret < 0) {
		tb_tunnel_warn(tunnel, "failed to read actual link rate\n");
		return;
	} else if (!ret) {
		/* Use maximum link rate if the link valid is not set */
		ret = tb_usb3_max_link_rate(tunnel->dst_port, tunnel->src_port);
		if (ret < 0) {
			tb_tunnel_warn(tunnel, "failed to read maximum link rate\n");
			return;
		}
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

static void tb_usb3_init_credits(struct tb_path_hop *hop)
{
	struct tb_port *port = hop->in_port;
	struct tb_switch *sw = port->sw;
	unsigned int credits;

	if (tb_port_use_credit_allocation(port)) {
		credits = sw->max_usb3_credits;
	} else {
		if (tb_port_is_null(port))
			credits = port->bonded ? 32 : 16;
		else
			credits = 7;
	}

	hop->initial_credits = credits;
}

static void tb_usb3_init_path(struct tb_path *path)
{
	struct tb_path_hop *hop;

	path->egress_fc_enable = TB_PATH_SOURCE | TB_PATH_INTERNAL;
	path->egress_shared_buffer = TB_PATH_NONE;
	path->ingress_fc_enable = TB_PATH_ALL;
	path->ingress_shared_buffer = TB_PATH_NONE;
	path->priority = 3;
	path->weight = 3;
	path->drop_packages = 0;

	tb_path_for_each_hop(path, hop)
		tb_usb3_init_credits(hop);
}

/**
 * tb_tunnel_discover_usb3() - Discover existing USB3 tunnels
 * @tb: Pointer to the domain structure
 * @down: USB3 downstream adapter
 * @alloc_hopid: Allocate HopIDs from visited ports
 *
 * If @down adapter is active, follows the tunnel to the USB3 upstream
 * adapter and back. Returns the discovered tunnel or %NULL if there was
 * no tunnel.
 */
struct tb_tunnel *tb_tunnel_discover_usb3(struct tb *tb, struct tb_port *down,
					  bool alloc_hopid)
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
				&tunnel->dst_port, "USB3 Down", alloc_hopid);
	if (!path) {
		/* Just disable the downstream port */
		tb_usb3_port_enable(down, false);
		goto err_free;
	}
	tunnel->paths[TB_USB3_PATH_DOWN] = path;
	tb_usb3_init_path(tunnel->paths[TB_USB3_PATH_DOWN]);

	path = tb_path_discover(tunnel->dst_port, -1, down, TB_USB3_HOPID, NULL,
				"USB3 Up", alloc_hopid);
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

	if (tunnel->deinit)
		tunnel->deinit(tunnel);

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
 * tb_tunnel_maximum_bandwidth() - Return maximum possible bandwidth
 * @tunnel: Tunnel to check
 * @max_up: Maximum upstream bandwidth in Mb/s
 * @max_down: Maximum downstream bandwidth in Mb/s
 *
 * Returns maximum possible bandwidth this tunnel can go if not limited
 * by other bandwidth clients. If the tunnel does not support this
 * returns %-EOPNOTSUPP.
 */
int tb_tunnel_maximum_bandwidth(struct tb_tunnel *tunnel, int *max_up,
				int *max_down)
{
	if (!tb_tunnel_is_active(tunnel))
		return -EINVAL;

	if (tunnel->maximum_bandwidth)
		return tunnel->maximum_bandwidth(tunnel, max_up, max_down);
	return -EOPNOTSUPP;
}

/**
 * tb_tunnel_allocated_bandwidth() - Return bandwidth allocated for the tunnel
 * @tunnel: Tunnel to check
 * @allocated_up: Currently allocated upstream bandwidth in Mb/s is stored here
 * @allocated_down: Currently allocated downstream bandwidth in Mb/s is
 *		    stored here
 *
 * Returns the bandwidth allocated for the tunnel. This may be higher
 * than what the tunnel actually consumes.
 */
int tb_tunnel_allocated_bandwidth(struct tb_tunnel *tunnel, int *allocated_up,
				  int *allocated_down)
{
	if (!tb_tunnel_is_active(tunnel))
		return -EINVAL;

	if (tunnel->allocated_bandwidth)
		return tunnel->allocated_bandwidth(tunnel, allocated_up,
						   allocated_down);
	return -EOPNOTSUPP;
}

/**
 * tb_tunnel_alloc_bandwidth() - Change tunnel bandwidth allocation
 * @tunnel: Tunnel whose bandwidth allocation to change
 * @alloc_up: New upstream bandwidth in Mb/s
 * @alloc_down: New downstream bandwidth in Mb/s
 *
 * Tries to change tunnel bandwidth allocation. If succeeds returns %0
 * and updates @alloc_up and @alloc_down to that was actually allocated
 * (it may not be the same as passed originally). Returns negative errno
 * in case of failure.
 */
int tb_tunnel_alloc_bandwidth(struct tb_tunnel *tunnel, int *alloc_up,
			      int *alloc_down)
{
	if (!tb_tunnel_is_active(tunnel))
		return -EINVAL;

	if (tunnel->alloc_bandwidth)
		return tunnel->alloc_bandwidth(tunnel, alloc_up, alloc_down);

	return -EOPNOTSUPP;
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
