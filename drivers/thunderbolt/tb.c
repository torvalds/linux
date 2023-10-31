// SPDX-License-Identifier: GPL-2.0
/*
 * Thunderbolt driver - bus logic (NHI independent)
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2019, Intel Corporation
 */

#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/platform_data/x86/apple.h>

#include "tb.h"
#include "tb_regs.h"
#include "tunnel.h"

#define TB_TIMEOUT	100	/* ms */
#define MAX_GROUPS	7	/* max Group_ID is 7 */

/**
 * struct tb_cm - Simple Thunderbolt connection manager
 * @tunnel_list: List of active tunnels
 * @dp_resources: List of available DP resources for DP tunneling
 * @hotplug_active: tb_handle_hotplug will stop progressing plug
 *		    events and exit if this is not set (it needs to
 *		    acquire the lock one more time). Used to drain wq
 *		    after cfg has been paused.
 * @remove_work: Work used to remove any unplugged routers after
 *		 runtime resume
 * @groups: Bandwidth groups used in this domain.
 */
struct tb_cm {
	struct list_head tunnel_list;
	struct list_head dp_resources;
	bool hotplug_active;
	struct delayed_work remove_work;
	struct tb_bandwidth_group groups[MAX_GROUPS];
};

static inline struct tb *tcm_to_tb(struct tb_cm *tcm)
{
	return ((void *)tcm - sizeof(struct tb));
}

struct tb_hotplug_event {
	struct work_struct work;
	struct tb *tb;
	u64 route;
	u8 port;
	bool unplug;
};

static void tb_init_bandwidth_groups(struct tb_cm *tcm)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tcm->groups); i++) {
		struct tb_bandwidth_group *group = &tcm->groups[i];

		group->tb = tcm_to_tb(tcm);
		group->index = i + 1;
		INIT_LIST_HEAD(&group->ports);
	}
}

static void tb_bandwidth_group_attach_port(struct tb_bandwidth_group *group,
					   struct tb_port *in)
{
	if (!group || WARN_ON(in->group))
		return;

	in->group = group;
	list_add_tail(&in->group_list, &group->ports);

	tb_port_dbg(in, "attached to bandwidth group %d\n", group->index);
}

static struct tb_bandwidth_group *tb_find_free_bandwidth_group(struct tb_cm *tcm)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tcm->groups); i++) {
		struct tb_bandwidth_group *group = &tcm->groups[i];

		if (list_empty(&group->ports))
			return group;
	}

	return NULL;
}

static struct tb_bandwidth_group *
tb_attach_bandwidth_group(struct tb_cm *tcm, struct tb_port *in,
			  struct tb_port *out)
{
	struct tb_bandwidth_group *group;
	struct tb_tunnel *tunnel;

	/*
	 * Find all DP tunnels that go through all the same USB4 links
	 * as this one. Because we always setup tunnels the same way we
	 * can just check for the routers at both ends of the tunnels
	 * and if they are the same we have a match.
	 */
	list_for_each_entry(tunnel, &tcm->tunnel_list, list) {
		if (!tb_tunnel_is_dp(tunnel))
			continue;

		if (tunnel->src_port->sw == in->sw &&
		    tunnel->dst_port->sw == out->sw) {
			group = tunnel->src_port->group;
			if (group) {
				tb_bandwidth_group_attach_port(group, in);
				return group;
			}
		}
	}

	/* Pick up next available group then */
	group = tb_find_free_bandwidth_group(tcm);
	if (group)
		tb_bandwidth_group_attach_port(group, in);
	else
		tb_port_warn(in, "no available bandwidth groups\n");

	return group;
}

static void tb_discover_bandwidth_group(struct tb_cm *tcm, struct tb_port *in,
					struct tb_port *out)
{
	if (usb4_dp_port_bandwidth_mode_enabled(in)) {
		int index, i;

		index = usb4_dp_port_group_id(in);
		for (i = 0; i < ARRAY_SIZE(tcm->groups); i++) {
			if (tcm->groups[i].index == index) {
				tb_bandwidth_group_attach_port(&tcm->groups[i], in);
				return;
			}
		}
	}

	tb_attach_bandwidth_group(tcm, in, out);
}

static void tb_detach_bandwidth_group(struct tb_port *in)
{
	struct tb_bandwidth_group *group = in->group;

	if (group) {
		in->group = NULL;
		list_del_init(&in->group_list);

		tb_port_dbg(in, "detached from bandwidth group %d\n", group->index);
	}
}

static void tb_handle_hotplug(struct work_struct *work);

static void tb_queue_hotplug(struct tb *tb, u64 route, u8 port, bool unplug)
{
	struct tb_hotplug_event *ev;

	ev = kmalloc(sizeof(*ev), GFP_KERNEL);
	if (!ev)
		return;

	ev->tb = tb;
	ev->route = route;
	ev->port = port;
	ev->unplug = unplug;
	INIT_WORK(&ev->work, tb_handle_hotplug);
	queue_work(tb->wq, &ev->work);
}

/* enumeration & hot plug handling */

static void tb_add_dp_resources(struct tb_switch *sw)
{
	struct tb_cm *tcm = tb_priv(sw->tb);
	struct tb_port *port;

	tb_switch_for_each_port(sw, port) {
		if (!tb_port_is_dpin(port))
			continue;

		if (!tb_switch_query_dp_resource(sw, port))
			continue;

		list_add_tail(&port->list, &tcm->dp_resources);
		tb_port_dbg(port, "DP IN resource available\n");
	}
}

static void tb_remove_dp_resources(struct tb_switch *sw)
{
	struct tb_cm *tcm = tb_priv(sw->tb);
	struct tb_port *port, *tmp;

	/* Clear children resources first */
	tb_switch_for_each_port(sw, port) {
		if (tb_port_has_remote(port))
			tb_remove_dp_resources(port->remote->sw);
	}

	list_for_each_entry_safe(port, tmp, &tcm->dp_resources, list) {
		if (port->sw == sw) {
			tb_port_dbg(port, "DP OUT resource unavailable\n");
			list_del_init(&port->list);
		}
	}
}

static void tb_discover_dp_resource(struct tb *tb, struct tb_port *port)
{
	struct tb_cm *tcm = tb_priv(tb);
	struct tb_port *p;

	list_for_each_entry(p, &tcm->dp_resources, list) {
		if (p == port)
			return;
	}

	tb_port_dbg(port, "DP %s resource available discovered\n",
		    tb_port_is_dpin(port) ? "IN" : "OUT");
	list_add_tail(&port->list, &tcm->dp_resources);
}

static void tb_discover_dp_resources(struct tb *tb)
{
	struct tb_cm *tcm = tb_priv(tb);
	struct tb_tunnel *tunnel;

	list_for_each_entry(tunnel, &tcm->tunnel_list, list) {
		if (tb_tunnel_is_dp(tunnel))
			tb_discover_dp_resource(tb, tunnel->dst_port);
	}
}

/* Enables CL states up to host router */
static int tb_enable_clx(struct tb_switch *sw)
{
	struct tb_cm *tcm = tb_priv(sw->tb);
	unsigned int clx = TB_CL0S | TB_CL1;
	const struct tb_tunnel *tunnel;
	int ret;

	/*
	 * Currently only enable CLx for the first link. This is enough
	 * to allow the CPU to save energy at least on Intel hardware
	 * and makes it slightly simpler to implement. We may change
	 * this in the future to cover the whole topology if it turns
	 * out to be beneficial.
	 */
	while (sw && sw->config.depth > 1)
		sw = tb_switch_parent(sw);

	if (!sw)
		return 0;

	if (sw->config.depth != 1)
		return 0;

	/*
	 * If we are re-enabling then check if there is an active DMA
	 * tunnel and in that case bail out.
	 */
	list_for_each_entry(tunnel, &tcm->tunnel_list, list) {
		if (tb_tunnel_is_dma(tunnel)) {
			if (tb_tunnel_port_on_path(tunnel, tb_upstream_port(sw)))
				return 0;
		}
	}

	/*
	 * Initially try with CL2. If that's not supported by the
	 * topology try with CL0s and CL1 and then give up.
	 */
	ret = tb_switch_clx_enable(sw, clx | TB_CL2);
	if (ret == -EOPNOTSUPP)
		ret = tb_switch_clx_enable(sw, clx);
	return ret == -EOPNOTSUPP ? 0 : ret;
}

/* Disables CL states up to the host router */
static void tb_disable_clx(struct tb_switch *sw)
{
	do {
		if (tb_switch_clx_disable(sw) < 0)
			tb_sw_warn(sw, "failed to disable CL states\n");
		sw = tb_switch_parent(sw);
	} while (sw);
}

static int tb_increase_switch_tmu_accuracy(struct device *dev, void *data)
{
	struct tb_switch *sw;

	sw = tb_to_switch(dev);
	if (!sw)
		return 0;

	if (tb_switch_tmu_is_configured(sw, TB_SWITCH_TMU_MODE_LOWRES)) {
		enum tb_switch_tmu_mode mode;
		int ret;

		if (tb_switch_clx_is_enabled(sw, TB_CL1))
			mode = TB_SWITCH_TMU_MODE_HIFI_UNI;
		else
			mode = TB_SWITCH_TMU_MODE_HIFI_BI;

		ret = tb_switch_tmu_configure(sw, mode);
		if (ret)
			return ret;

		return tb_switch_tmu_enable(sw);
	}

	return 0;
}

static void tb_increase_tmu_accuracy(struct tb_tunnel *tunnel)
{
	struct tb_switch *sw;

	if (!tunnel)
		return;

	/*
	 * Once first DP tunnel is established we change the TMU
	 * accuracy of first depth child routers (and the host router)
	 * to the highest. This is needed for the DP tunneling to work
	 * but also allows CL0s.
	 *
	 * If both routers are v2 then we don't need to do anything as
	 * they are using enhanced TMU mode that allows all CLx.
	 */
	sw = tunnel->tb->root_switch;
	device_for_each_child(&sw->dev, NULL, tb_increase_switch_tmu_accuracy);
}

static int tb_enable_tmu(struct tb_switch *sw)
{
	int ret;

	/*
	 * If both routers at the end of the link are v2 we simply
	 * enable the enhanched uni-directional mode. That covers all
	 * the CL states. For v1 and before we need to use the normal
	 * rate to allow CL1 (when supported). Otherwise we keep the TMU
	 * running at the highest accuracy.
	 */
	ret = tb_switch_tmu_configure(sw,
			TB_SWITCH_TMU_MODE_MEDRES_ENHANCED_UNI);
	if (ret == -EOPNOTSUPP) {
		if (tb_switch_clx_is_enabled(sw, TB_CL1))
			ret = tb_switch_tmu_configure(sw,
					TB_SWITCH_TMU_MODE_LOWRES);
		else
			ret = tb_switch_tmu_configure(sw,
					TB_SWITCH_TMU_MODE_HIFI_BI);
	}
	if (ret)
		return ret;

	/* If it is already enabled in correct mode, don't touch it */
	if (tb_switch_tmu_is_enabled(sw))
		return 0;

	ret = tb_switch_tmu_disable(sw);
	if (ret)
		return ret;

	ret = tb_switch_tmu_post_time(sw);
	if (ret)
		return ret;

	return tb_switch_tmu_enable(sw);
}

static void tb_switch_discover_tunnels(struct tb_switch *sw,
				       struct list_head *list,
				       bool alloc_hopids)
{
	struct tb *tb = sw->tb;
	struct tb_port *port;

	tb_switch_for_each_port(sw, port) {
		struct tb_tunnel *tunnel = NULL;

		switch (port->config.type) {
		case TB_TYPE_DP_HDMI_IN:
			tunnel = tb_tunnel_discover_dp(tb, port, alloc_hopids);
			tb_increase_tmu_accuracy(tunnel);
			break;

		case TB_TYPE_PCIE_DOWN:
			tunnel = tb_tunnel_discover_pci(tb, port, alloc_hopids);
			break;

		case TB_TYPE_USB3_DOWN:
			tunnel = tb_tunnel_discover_usb3(tb, port, alloc_hopids);
			break;

		default:
			break;
		}

		if (tunnel)
			list_add_tail(&tunnel->list, list);
	}

	tb_switch_for_each_port(sw, port) {
		if (tb_port_has_remote(port)) {
			tb_switch_discover_tunnels(port->remote->sw, list,
						   alloc_hopids);
		}
	}
}

static void tb_discover_tunnels(struct tb *tb)
{
	struct tb_cm *tcm = tb_priv(tb);
	struct tb_tunnel *tunnel;

	tb_switch_discover_tunnels(tb->root_switch, &tcm->tunnel_list, true);

	list_for_each_entry(tunnel, &tcm->tunnel_list, list) {
		if (tb_tunnel_is_pci(tunnel)) {
			struct tb_switch *parent = tunnel->dst_port->sw;

			while (parent != tunnel->src_port->sw) {
				parent->boot = true;
				parent = tb_switch_parent(parent);
			}
		} else if (tb_tunnel_is_dp(tunnel)) {
			struct tb_port *in = tunnel->src_port;
			struct tb_port *out = tunnel->dst_port;

			/* Keep the domain from powering down */
			pm_runtime_get_sync(&in->sw->dev);
			pm_runtime_get_sync(&out->sw->dev);

			tb_discover_bandwidth_group(tcm, in, out);
		}
	}
}

static int tb_port_configure_xdomain(struct tb_port *port, struct tb_xdomain *xd)
{
	if (tb_switch_is_usb4(port->sw))
		return usb4_port_configure_xdomain(port, xd);
	return tb_lc_configure_xdomain(port);
}

static void tb_port_unconfigure_xdomain(struct tb_port *port)
{
	if (tb_switch_is_usb4(port->sw))
		usb4_port_unconfigure_xdomain(port);
	else
		tb_lc_unconfigure_xdomain(port);

	tb_port_enable(port->dual_link_port);
}

static void tb_scan_xdomain(struct tb_port *port)
{
	struct tb_switch *sw = port->sw;
	struct tb *tb = sw->tb;
	struct tb_xdomain *xd;
	u64 route;

	if (!tb_is_xdomain_enabled())
		return;

	route = tb_downstream_route(port);
	xd = tb_xdomain_find_by_route(tb, route);
	if (xd) {
		tb_xdomain_put(xd);
		return;
	}

	xd = tb_xdomain_alloc(tb, &sw->dev, route, tb->root_switch->uuid,
			      NULL);
	if (xd) {
		tb_port_at(route, sw)->xdomain = xd;
		tb_port_configure_xdomain(port, xd);
		tb_xdomain_add(xd);
	}
}

/**
 * tb_find_unused_port() - return the first inactive port on @sw
 * @sw: Switch to find the port on
 * @type: Port type to look for
 */
static struct tb_port *tb_find_unused_port(struct tb_switch *sw,
					   enum tb_port_type type)
{
	struct tb_port *port;

	tb_switch_for_each_port(sw, port) {
		if (tb_is_upstream_port(port))
			continue;
		if (port->config.type != type)
			continue;
		if (!port->cap_adap)
			continue;
		if (tb_port_is_enabled(port))
			continue;
		return port;
	}
	return NULL;
}

static struct tb_port *tb_find_usb3_down(struct tb_switch *sw,
					 const struct tb_port *port)
{
	struct tb_port *down;

	down = usb4_switch_map_usb3_down(sw, port);
	if (down && !tb_usb3_port_is_enabled(down))
		return down;
	return NULL;
}

static struct tb_tunnel *tb_find_tunnel(struct tb *tb, enum tb_tunnel_type type,
					struct tb_port *src_port,
					struct tb_port *dst_port)
{
	struct tb_cm *tcm = tb_priv(tb);
	struct tb_tunnel *tunnel;

	list_for_each_entry(tunnel, &tcm->tunnel_list, list) {
		if (tunnel->type == type &&
		    ((src_port && src_port == tunnel->src_port) ||
		     (dst_port && dst_port == tunnel->dst_port))) {
			return tunnel;
		}
	}

	return NULL;
}

static struct tb_tunnel *tb_find_first_usb3_tunnel(struct tb *tb,
						   struct tb_port *src_port,
						   struct tb_port *dst_port)
{
	struct tb_port *port, *usb3_down;
	struct tb_switch *sw;

	/* Pick the router that is deepest in the topology */
	if (dst_port->sw->config.depth > src_port->sw->config.depth)
		sw = dst_port->sw;
	else
		sw = src_port->sw;

	/* Can't be the host router */
	if (sw == tb->root_switch)
		return NULL;

	/* Find the downstream USB4 port that leads to this router */
	port = tb_port_at(tb_route(sw), tb->root_switch);
	/* Find the corresponding host router USB3 downstream port */
	usb3_down = usb4_switch_map_usb3_down(tb->root_switch, port);
	if (!usb3_down)
		return NULL;

	return tb_find_tunnel(tb, TB_TUNNEL_USB3, usb3_down, NULL);
}

static int tb_available_bandwidth(struct tb *tb, struct tb_port *src_port,
	struct tb_port *dst_port, int *available_up, int *available_down)
{
	int usb3_consumed_up, usb3_consumed_down, ret;
	struct tb_cm *tcm = tb_priv(tb);
	struct tb_tunnel *tunnel;
	struct tb_port *port;

	tb_dbg(tb, "calculating available bandwidth between %llx:%u <-> %llx:%u\n",
	       tb_route(src_port->sw), src_port->port, tb_route(dst_port->sw),
	       dst_port->port);

	tunnel = tb_find_first_usb3_tunnel(tb, src_port, dst_port);
	if (tunnel && tunnel->src_port != src_port &&
	    tunnel->dst_port != dst_port) {
		ret = tb_tunnel_consumed_bandwidth(tunnel, &usb3_consumed_up,
						   &usb3_consumed_down);
		if (ret)
			return ret;
	} else {
		usb3_consumed_up = 0;
		usb3_consumed_down = 0;
	}

	/* Maximum possible bandwidth asymmetric Gen 4 link is 120 Gb/s */
	*available_up = *available_down = 120000;

	/* Find the minimum available bandwidth over all links */
	tb_for_each_port_on_path(src_port, dst_port, port) {
		int link_speed, link_width, up_bw, down_bw;

		if (!tb_port_is_null(port))
			continue;

		if (tb_is_upstream_port(port)) {
			link_speed = port->sw->link_speed;
			/*
			 * sw->link_width is from upstream perspective
			 * so we use the opposite for downstream of the
			 * host router.
			 */
			if (port->sw->link_width == TB_LINK_WIDTH_ASYM_TX) {
				up_bw = link_speed * 3 * 1000;
				down_bw = link_speed * 1 * 1000;
			} else if (port->sw->link_width == TB_LINK_WIDTH_ASYM_RX) {
				up_bw = link_speed * 1 * 1000;
				down_bw = link_speed * 3 * 1000;
			} else {
				up_bw = link_speed * port->sw->link_width * 1000;
				down_bw = up_bw;
			}
		} else {
			link_speed = tb_port_get_link_speed(port);
			if (link_speed < 0)
				return link_speed;

			link_width = tb_port_get_link_width(port);
			if (link_width < 0)
				return link_width;

			if (link_width == TB_LINK_WIDTH_ASYM_TX) {
				up_bw = link_speed * 1 * 1000;
				down_bw = link_speed * 3 * 1000;
			} else if (link_width == TB_LINK_WIDTH_ASYM_RX) {
				up_bw = link_speed * 3 * 1000;
				down_bw = link_speed * 1 * 1000;
			} else {
				up_bw = link_speed * link_width * 1000;
				down_bw = up_bw;
			}
		}

		/* Leave 10% guard band */
		up_bw -= up_bw / 10;
		down_bw -= down_bw / 10;

		tb_port_dbg(port, "link total bandwidth %d/%d Mb/s\n", up_bw,
			    down_bw);

		/*
		 * Find all DP tunnels that cross the port and reduce
		 * their consumed bandwidth from the available.
		 */
		list_for_each_entry(tunnel, &tcm->tunnel_list, list) {
			int dp_consumed_up, dp_consumed_down;

			if (tb_tunnel_is_invalid(tunnel))
				continue;

			if (!tb_tunnel_is_dp(tunnel))
				continue;

			if (!tb_tunnel_port_on_path(tunnel, port))
				continue;

			/*
			 * Ignore the DP tunnel between src_port and
			 * dst_port because it is the same tunnel and we
			 * may be re-calculating estimated bandwidth.
			 */
			if (tunnel->src_port == src_port &&
			    tunnel->dst_port == dst_port)
				continue;

			ret = tb_tunnel_consumed_bandwidth(tunnel,
							   &dp_consumed_up,
							   &dp_consumed_down);
			if (ret)
				return ret;

			up_bw -= dp_consumed_up;
			down_bw -= dp_consumed_down;
		}

		/*
		 * If USB3 is tunneled from the host router down to the
		 * branch leading to port we need to take USB3 consumed
		 * bandwidth into account regardless whether it actually
		 * crosses the port.
		 */
		up_bw -= usb3_consumed_up;
		down_bw -= usb3_consumed_down;

		if (up_bw < *available_up)
			*available_up = up_bw;
		if (down_bw < *available_down)
			*available_down = down_bw;
	}

	if (*available_up < 0)
		*available_up = 0;
	if (*available_down < 0)
		*available_down = 0;

	return 0;
}

static int tb_release_unused_usb3_bandwidth(struct tb *tb,
					    struct tb_port *src_port,
					    struct tb_port *dst_port)
{
	struct tb_tunnel *tunnel;

	tunnel = tb_find_first_usb3_tunnel(tb, src_port, dst_port);
	return tunnel ? tb_tunnel_release_unused_bandwidth(tunnel) : 0;
}

static void tb_reclaim_usb3_bandwidth(struct tb *tb, struct tb_port *src_port,
				      struct tb_port *dst_port)
{
	int ret, available_up, available_down;
	struct tb_tunnel *tunnel;

	tunnel = tb_find_first_usb3_tunnel(tb, src_port, dst_port);
	if (!tunnel)
		return;

	tb_dbg(tb, "reclaiming unused bandwidth for USB3\n");

	/*
	 * Calculate available bandwidth for the first hop USB3 tunnel.
	 * That determines the whole USB3 bandwidth for this branch.
	 */
	ret = tb_available_bandwidth(tb, tunnel->src_port, tunnel->dst_port,
				     &available_up, &available_down);
	if (ret) {
		tb_warn(tb, "failed to calculate available bandwidth\n");
		return;
	}

	tb_dbg(tb, "available bandwidth for USB3 %d/%d Mb/s\n",
	       available_up, available_down);

	tb_tunnel_reclaim_available_bandwidth(tunnel, &available_up, &available_down);
}

static int tb_tunnel_usb3(struct tb *tb, struct tb_switch *sw)
{
	struct tb_switch *parent = tb_switch_parent(sw);
	int ret, available_up, available_down;
	struct tb_port *up, *down, *port;
	struct tb_cm *tcm = tb_priv(tb);
	struct tb_tunnel *tunnel;

	if (!tb_acpi_may_tunnel_usb3()) {
		tb_dbg(tb, "USB3 tunneling disabled, not creating tunnel\n");
		return 0;
	}

	up = tb_switch_find_port(sw, TB_TYPE_USB3_UP);
	if (!up)
		return 0;

	if (!sw->link_usb4)
		return 0;

	/*
	 * Look up available down port. Since we are chaining it should
	 * be found right above this switch.
	 */
	port = tb_switch_downstream_port(sw);
	down = tb_find_usb3_down(parent, port);
	if (!down)
		return 0;

	if (tb_route(parent)) {
		struct tb_port *parent_up;
		/*
		 * Check first that the parent switch has its upstream USB3
		 * port enabled. Otherwise the chain is not complete and
		 * there is no point setting up a new tunnel.
		 */
		parent_up = tb_switch_find_port(parent, TB_TYPE_USB3_UP);
		if (!parent_up || !tb_port_is_enabled(parent_up))
			return 0;

		/* Make all unused bandwidth available for the new tunnel */
		ret = tb_release_unused_usb3_bandwidth(tb, down, up);
		if (ret)
			return ret;
	}

	ret = tb_available_bandwidth(tb, down, up, &available_up,
				     &available_down);
	if (ret)
		goto err_reclaim;

	tb_port_dbg(up, "available bandwidth for new USB3 tunnel %d/%d Mb/s\n",
		    available_up, available_down);

	tunnel = tb_tunnel_alloc_usb3(tb, up, down, available_up,
				      available_down);
	if (!tunnel) {
		ret = -ENOMEM;
		goto err_reclaim;
	}

	if (tb_tunnel_activate(tunnel)) {
		tb_port_info(up,
			     "USB3 tunnel activation failed, aborting\n");
		ret = -EIO;
		goto err_free;
	}

	list_add_tail(&tunnel->list, &tcm->tunnel_list);
	if (tb_route(parent))
		tb_reclaim_usb3_bandwidth(tb, down, up);

	return 0;

err_free:
	tb_tunnel_free(tunnel);
err_reclaim:
	if (tb_route(parent))
		tb_reclaim_usb3_bandwidth(tb, down, up);

	return ret;
}

static int tb_create_usb3_tunnels(struct tb_switch *sw)
{
	struct tb_port *port;
	int ret;

	if (!tb_acpi_may_tunnel_usb3())
		return 0;

	if (tb_route(sw)) {
		ret = tb_tunnel_usb3(sw->tb, sw);
		if (ret)
			return ret;
	}

	tb_switch_for_each_port(sw, port) {
		if (!tb_port_has_remote(port))
			continue;
		ret = tb_create_usb3_tunnels(port->remote->sw);
		if (ret)
			return ret;
	}

	return 0;
}

static void tb_scan_port(struct tb_port *port);

/*
 * tb_scan_switch() - scan for and initialize downstream switches
 */
static void tb_scan_switch(struct tb_switch *sw)
{
	struct tb_port *port;

	pm_runtime_get_sync(&sw->dev);

	tb_switch_for_each_port(sw, port)
		tb_scan_port(port);

	pm_runtime_mark_last_busy(&sw->dev);
	pm_runtime_put_autosuspend(&sw->dev);
}

/*
 * tb_scan_port() - check for and initialize switches below port
 */
static void tb_scan_port(struct tb_port *port)
{
	struct tb_cm *tcm = tb_priv(port->sw->tb);
	struct tb_port *upstream_port;
	bool discovery = false;
	struct tb_switch *sw;

	if (tb_is_upstream_port(port))
		return;

	if (tb_port_is_dpout(port) && tb_dp_port_hpd_is_active(port) == 1 &&
	    !tb_dp_port_is_enabled(port)) {
		tb_port_dbg(port, "DP adapter HPD set, queuing hotplug\n");
		tb_queue_hotplug(port->sw->tb, tb_route(port->sw), port->port,
				 false);
		return;
	}

	if (port->config.type != TB_TYPE_PORT)
		return;
	if (port->dual_link_port && port->link_nr)
		return; /*
			 * Downstream switch is reachable through two ports.
			 * Only scan on the primary port (link_nr == 0).
			 */

	if (port->usb4)
		pm_runtime_get_sync(&port->usb4->dev);

	if (tb_wait_for_port(port, false) <= 0)
		goto out_rpm_put;
	if (port->remote) {
		tb_port_dbg(port, "port already has a remote\n");
		goto out_rpm_put;
	}

	tb_retimer_scan(port, true);

	sw = tb_switch_alloc(port->sw->tb, &port->sw->dev,
			     tb_downstream_route(port));
	if (IS_ERR(sw)) {
		/*
		 * If there is an error accessing the connected switch
		 * it may be connected to another domain. Also we allow
		 * the other domain to be connected to a max depth switch.
		 */
		if (PTR_ERR(sw) == -EIO || PTR_ERR(sw) == -EADDRNOTAVAIL)
			tb_scan_xdomain(port);
		goto out_rpm_put;
	}

	if (tb_switch_configure(sw)) {
		tb_switch_put(sw);
		goto out_rpm_put;
	}

	/*
	 * If there was previously another domain connected remove it
	 * first.
	 */
	if (port->xdomain) {
		tb_xdomain_remove(port->xdomain);
		tb_port_unconfigure_xdomain(port);
		port->xdomain = NULL;
	}

	/*
	 * Do not send uevents until we have discovered all existing
	 * tunnels and know which switches were authorized already by
	 * the boot firmware.
	 */
	if (!tcm->hotplug_active) {
		dev_set_uevent_suppress(&sw->dev, true);
		discovery = true;
	}

	/*
	 * At the moment Thunderbolt 2 and beyond (devices with LC) we
	 * can support runtime PM.
	 */
	sw->rpm = sw->generation > 1;

	if (tb_switch_add(sw)) {
		tb_switch_put(sw);
		goto out_rpm_put;
	}

	/* Link the switches using both links if available */
	upstream_port = tb_upstream_port(sw);
	port->remote = upstream_port;
	upstream_port->remote = port;
	if (port->dual_link_port && upstream_port->dual_link_port) {
		port->dual_link_port->remote = upstream_port->dual_link_port;
		upstream_port->dual_link_port->remote = port->dual_link_port;
	}

	/* Enable lane bonding if supported */
	tb_switch_lane_bonding_enable(sw);
	/* Set the link configured */
	tb_switch_configure_link(sw);
	/*
	 * CL0s and CL1 are enabled and supported together.
	 * Silently ignore CLx enabling in case CLx is not supported.
	 */
	if (discovery)
		tb_sw_dbg(sw, "discovery, not touching CL states\n");
	else if (tb_enable_clx(sw))
		tb_sw_warn(sw, "failed to enable CL states\n");

	if (tb_enable_tmu(sw))
		tb_sw_warn(sw, "failed to enable TMU\n");

	/*
	 * Configuration valid needs to be set after the TMU has been
	 * enabled for the upstream port of the router so we do it here.
	 */
	tb_switch_configuration_valid(sw);

	/* Scan upstream retimers */
	tb_retimer_scan(upstream_port, true);

	/*
	 * Create USB 3.x tunnels only when the switch is plugged to the
	 * domain. This is because we scan the domain also during discovery
	 * and want to discover existing USB 3.x tunnels before we create
	 * any new.
	 */
	if (tcm->hotplug_active && tb_tunnel_usb3(sw->tb, sw))
		tb_sw_warn(sw, "USB3 tunnel creation failed\n");

	tb_add_dp_resources(sw);
	tb_scan_switch(sw);

out_rpm_put:
	if (port->usb4) {
		pm_runtime_mark_last_busy(&port->usb4->dev);
		pm_runtime_put_autosuspend(&port->usb4->dev);
	}
}

static void tb_deactivate_and_free_tunnel(struct tb_tunnel *tunnel)
{
	struct tb_port *src_port, *dst_port;
	struct tb *tb;

	if (!tunnel)
		return;

	tb_tunnel_deactivate(tunnel);
	list_del(&tunnel->list);

	tb = tunnel->tb;
	src_port = tunnel->src_port;
	dst_port = tunnel->dst_port;

	switch (tunnel->type) {
	case TB_TUNNEL_DP:
		tb_detach_bandwidth_group(src_port);
		/*
		 * In case of DP tunnel make sure the DP IN resource is
		 * deallocated properly.
		 */
		tb_switch_dealloc_dp_resource(src_port->sw, src_port);
		/* Now we can allow the domain to runtime suspend again */
		pm_runtime_mark_last_busy(&dst_port->sw->dev);
		pm_runtime_put_autosuspend(&dst_port->sw->dev);
		pm_runtime_mark_last_busy(&src_port->sw->dev);
		pm_runtime_put_autosuspend(&src_port->sw->dev);
		fallthrough;

	case TB_TUNNEL_USB3:
		tb_reclaim_usb3_bandwidth(tb, src_port, dst_port);
		break;

	default:
		/*
		 * PCIe and DMA tunnels do not consume guaranteed
		 * bandwidth.
		 */
		break;
	}

	tb_tunnel_free(tunnel);
}

/*
 * tb_free_invalid_tunnels() - destroy tunnels of devices that have gone away
 */
static void tb_free_invalid_tunnels(struct tb *tb)
{
	struct tb_cm *tcm = tb_priv(tb);
	struct tb_tunnel *tunnel;
	struct tb_tunnel *n;

	list_for_each_entry_safe(tunnel, n, &tcm->tunnel_list, list) {
		if (tb_tunnel_is_invalid(tunnel))
			tb_deactivate_and_free_tunnel(tunnel);
	}
}

/*
 * tb_free_unplugged_children() - traverse hierarchy and free unplugged switches
 */
static void tb_free_unplugged_children(struct tb_switch *sw)
{
	struct tb_port *port;

	tb_switch_for_each_port(sw, port) {
		if (!tb_port_has_remote(port))
			continue;

		if (port->remote->sw->is_unplugged) {
			tb_retimer_remove_all(port);
			tb_remove_dp_resources(port->remote->sw);
			tb_switch_unconfigure_link(port->remote->sw);
			tb_switch_lane_bonding_disable(port->remote->sw);
			tb_switch_remove(port->remote->sw);
			port->remote = NULL;
			if (port->dual_link_port)
				port->dual_link_port->remote = NULL;
		} else {
			tb_free_unplugged_children(port->remote->sw);
		}
	}
}

static struct tb_port *tb_find_pcie_down(struct tb_switch *sw,
					 const struct tb_port *port)
{
	struct tb_port *down = NULL;

	/*
	 * To keep plugging devices consistently in the same PCIe
	 * hierarchy, do mapping here for switch downstream PCIe ports.
	 */
	if (tb_switch_is_usb4(sw)) {
		down = usb4_switch_map_pcie_down(sw, port);
	} else if (!tb_route(sw)) {
		int phy_port = tb_phy_port_from_link(port->port);
		int index;

		/*
		 * Hard-coded Thunderbolt port to PCIe down port mapping
		 * per controller.
		 */
		if (tb_switch_is_cactus_ridge(sw) ||
		    tb_switch_is_alpine_ridge(sw))
			index = !phy_port ? 6 : 7;
		else if (tb_switch_is_falcon_ridge(sw))
			index = !phy_port ? 6 : 8;
		else if (tb_switch_is_titan_ridge(sw))
			index = !phy_port ? 8 : 9;
		else
			goto out;

		/* Validate the hard-coding */
		if (WARN_ON(index > sw->config.max_port_number))
			goto out;

		down = &sw->ports[index];
	}

	if (down) {
		if (WARN_ON(!tb_port_is_pcie_down(down)))
			goto out;
		if (tb_pci_port_is_enabled(down))
			goto out;

		return down;
	}

out:
	return tb_find_unused_port(sw, TB_TYPE_PCIE_DOWN);
}

static void
tb_recalc_estimated_bandwidth_for_group(struct tb_bandwidth_group *group)
{
	struct tb_tunnel *first_tunnel;
	struct tb *tb = group->tb;
	struct tb_port *in;
	int ret;

	tb_dbg(tb, "re-calculating bandwidth estimation for group %u\n",
	       group->index);

	first_tunnel = NULL;
	list_for_each_entry(in, &group->ports, group_list) {
		int estimated_bw, estimated_up, estimated_down;
		struct tb_tunnel *tunnel;
		struct tb_port *out;

		if (!usb4_dp_port_bandwidth_mode_enabled(in))
			continue;

		tunnel = tb_find_tunnel(tb, TB_TUNNEL_DP, in, NULL);
		if (WARN_ON(!tunnel))
			break;

		if (!first_tunnel) {
			/*
			 * Since USB3 bandwidth is shared by all DP
			 * tunnels under the host router USB4 port, even
			 * if they do not begin from the host router, we
			 * can release USB3 bandwidth just once and not
			 * for each tunnel separately.
			 */
			first_tunnel = tunnel;
			ret = tb_release_unused_usb3_bandwidth(tb,
				first_tunnel->src_port, first_tunnel->dst_port);
			if (ret) {
				tb_port_warn(in,
					"failed to release unused bandwidth\n");
				break;
			}
		}

		out = tunnel->dst_port;
		ret = tb_available_bandwidth(tb, in, out, &estimated_up,
					     &estimated_down);
		if (ret) {
			tb_port_warn(in,
				"failed to re-calculate estimated bandwidth\n");
			break;
		}

		/*
		 * Estimated bandwidth includes:
		 *  - already allocated bandwidth for the DP tunnel
		 *  - available bandwidth along the path
		 *  - bandwidth allocated for USB 3.x but not used.
		 */
		tb_port_dbg(in, "re-calculated estimated bandwidth %u/%u Mb/s\n",
			    estimated_up, estimated_down);

		if (in->sw->config.depth < out->sw->config.depth)
			estimated_bw = estimated_down;
		else
			estimated_bw = estimated_up;

		if (usb4_dp_port_set_estimated_bandwidth(in, estimated_bw))
			tb_port_warn(in, "failed to update estimated bandwidth\n");
	}

	if (first_tunnel)
		tb_reclaim_usb3_bandwidth(tb, first_tunnel->src_port,
					  first_tunnel->dst_port);

	tb_dbg(tb, "bandwidth estimation for group %u done\n", group->index);
}

static void tb_recalc_estimated_bandwidth(struct tb *tb)
{
	struct tb_cm *tcm = tb_priv(tb);
	int i;

	tb_dbg(tb, "bandwidth consumption changed, re-calculating estimated bandwidth\n");

	for (i = 0; i < ARRAY_SIZE(tcm->groups); i++) {
		struct tb_bandwidth_group *group = &tcm->groups[i];

		if (!list_empty(&group->ports))
			tb_recalc_estimated_bandwidth_for_group(group);
	}

	tb_dbg(tb, "bandwidth re-calculation done\n");
}

static struct tb_port *tb_find_dp_out(struct tb *tb, struct tb_port *in)
{
	struct tb_port *host_port, *port;
	struct tb_cm *tcm = tb_priv(tb);

	host_port = tb_route(in->sw) ?
		tb_port_at(tb_route(in->sw), tb->root_switch) : NULL;

	list_for_each_entry(port, &tcm->dp_resources, list) {
		if (!tb_port_is_dpout(port))
			continue;

		if (tb_port_is_enabled(port)) {
			tb_port_dbg(port, "DP OUT in use\n");
			continue;
		}

		tb_port_dbg(port, "DP OUT available\n");

		/*
		 * Keep the DP tunnel under the topology starting from
		 * the same host router downstream port.
		 */
		if (host_port && tb_route(port->sw)) {
			struct tb_port *p;

			p = tb_port_at(tb_route(port->sw), tb->root_switch);
			if (p != host_port)
				continue;
		}

		return port;
	}

	return NULL;
}

static void tb_tunnel_dp(struct tb *tb)
{
	int available_up, available_down, ret, link_nr;
	struct tb_cm *tcm = tb_priv(tb);
	struct tb_port *port, *in, *out;
	struct tb_tunnel *tunnel;

	if (!tb_acpi_may_tunnel_dp()) {
		tb_dbg(tb, "DP tunneling disabled, not creating tunnel\n");
		return;
	}

	/*
	 * Find pair of inactive DP IN and DP OUT adapters and then
	 * establish a DP tunnel between them.
	 */
	tb_dbg(tb, "looking for DP IN <-> DP OUT pairs:\n");

	in = NULL;
	out = NULL;
	list_for_each_entry(port, &tcm->dp_resources, list) {
		if (!tb_port_is_dpin(port))
			continue;

		if (tb_port_is_enabled(port)) {
			tb_port_dbg(port, "DP IN in use\n");
			continue;
		}

		tb_port_dbg(port, "DP IN available\n");

		out = tb_find_dp_out(tb, port);
		if (out) {
			in = port;
			break;
		}
	}

	if (!in) {
		tb_dbg(tb, "no suitable DP IN adapter available, not tunneling\n");
		return;
	}
	if (!out) {
		tb_dbg(tb, "no suitable DP OUT adapter available, not tunneling\n");
		return;
	}

	/*
	 * This is only applicable to links that are not bonded (so
	 * when Thunderbolt 1 hardware is involved somewhere in the
	 * topology). For these try to share the DP bandwidth between
	 * the two lanes.
	 */
	link_nr = 1;
	list_for_each_entry(tunnel, &tcm->tunnel_list, list) {
		if (tb_tunnel_is_dp(tunnel)) {
			link_nr = 0;
			break;
		}
	}

	/*
	 * DP stream needs the domain to be active so runtime resume
	 * both ends of the tunnel.
	 *
	 * This should bring the routers in the middle active as well
	 * and keeps the domain from runtime suspending while the DP
	 * tunnel is active.
	 */
	pm_runtime_get_sync(&in->sw->dev);
	pm_runtime_get_sync(&out->sw->dev);

	if (tb_switch_alloc_dp_resource(in->sw, in)) {
		tb_port_dbg(in, "no resource available for DP IN, not tunneling\n");
		goto err_rpm_put;
	}

	if (!tb_attach_bandwidth_group(tcm, in, out))
		goto err_dealloc_dp;

	/* Make all unused USB3 bandwidth available for the new DP tunnel */
	ret = tb_release_unused_usb3_bandwidth(tb, in, out);
	if (ret) {
		tb_warn(tb, "failed to release unused bandwidth\n");
		goto err_detach_group;
	}

	ret = tb_available_bandwidth(tb, in, out, &available_up, &available_down);
	if (ret)
		goto err_reclaim_usb;

	tb_dbg(tb, "available bandwidth for new DP tunnel %u/%u Mb/s\n",
	       available_up, available_down);

	tunnel = tb_tunnel_alloc_dp(tb, in, out, link_nr, available_up,
				    available_down);
	if (!tunnel) {
		tb_port_dbg(out, "could not allocate DP tunnel\n");
		goto err_reclaim_usb;
	}

	if (tb_tunnel_activate(tunnel)) {
		tb_port_info(out, "DP tunnel activation failed, aborting\n");
		goto err_free;
	}

	list_add_tail(&tunnel->list, &tcm->tunnel_list);
	tb_reclaim_usb3_bandwidth(tb, in, out);

	/* Update the domain with the new bandwidth estimation */
	tb_recalc_estimated_bandwidth(tb);

	/*
	 * In case of DP tunnel exists, change host router's 1st children
	 * TMU mode to HiFi for CL0s to work.
	 */
	tb_increase_tmu_accuracy(tunnel);
	return;

err_free:
	tb_tunnel_free(tunnel);
err_reclaim_usb:
	tb_reclaim_usb3_bandwidth(tb, in, out);
err_detach_group:
	tb_detach_bandwidth_group(in);
err_dealloc_dp:
	tb_switch_dealloc_dp_resource(in->sw, in);
err_rpm_put:
	pm_runtime_mark_last_busy(&out->sw->dev);
	pm_runtime_put_autosuspend(&out->sw->dev);
	pm_runtime_mark_last_busy(&in->sw->dev);
	pm_runtime_put_autosuspend(&in->sw->dev);
}

static void tb_dp_resource_unavailable(struct tb *tb, struct tb_port *port)
{
	struct tb_port *in, *out;
	struct tb_tunnel *tunnel;

	if (tb_port_is_dpin(port)) {
		tb_port_dbg(port, "DP IN resource unavailable\n");
		in = port;
		out = NULL;
	} else {
		tb_port_dbg(port, "DP OUT resource unavailable\n");
		in = NULL;
		out = port;
	}

	tunnel = tb_find_tunnel(tb, TB_TUNNEL_DP, in, out);
	tb_deactivate_and_free_tunnel(tunnel);
	list_del_init(&port->list);

	/*
	 * See if there is another DP OUT port that can be used for
	 * to create another tunnel.
	 */
	tb_recalc_estimated_bandwidth(tb);
	tb_tunnel_dp(tb);
}

static void tb_dp_resource_available(struct tb *tb, struct tb_port *port)
{
	struct tb_cm *tcm = tb_priv(tb);
	struct tb_port *p;

	if (tb_port_is_enabled(port))
		return;

	list_for_each_entry(p, &tcm->dp_resources, list) {
		if (p == port)
			return;
	}

	tb_port_dbg(port, "DP %s resource available\n",
		    tb_port_is_dpin(port) ? "IN" : "OUT");
	list_add_tail(&port->list, &tcm->dp_resources);

	/* Look for suitable DP IN <-> DP OUT pairs now */
	tb_tunnel_dp(tb);
}

static void tb_disconnect_and_release_dp(struct tb *tb)
{
	struct tb_cm *tcm = tb_priv(tb);
	struct tb_tunnel *tunnel, *n;

	/*
	 * Tear down all DP tunnels and release their resources. They
	 * will be re-established after resume based on plug events.
	 */
	list_for_each_entry_safe_reverse(tunnel, n, &tcm->tunnel_list, list) {
		if (tb_tunnel_is_dp(tunnel))
			tb_deactivate_and_free_tunnel(tunnel);
	}

	while (!list_empty(&tcm->dp_resources)) {
		struct tb_port *port;

		port = list_first_entry(&tcm->dp_resources,
					struct tb_port, list);
		list_del_init(&port->list);
	}
}

static int tb_disconnect_pci(struct tb *tb, struct tb_switch *sw)
{
	struct tb_tunnel *tunnel;
	struct tb_port *up;

	up = tb_switch_find_port(sw, TB_TYPE_PCIE_UP);
	if (WARN_ON(!up))
		return -ENODEV;

	tunnel = tb_find_tunnel(tb, TB_TUNNEL_PCI, NULL, up);
	if (WARN_ON(!tunnel))
		return -ENODEV;

	tb_switch_xhci_disconnect(sw);

	tb_tunnel_deactivate(tunnel);
	list_del(&tunnel->list);
	tb_tunnel_free(tunnel);
	return 0;
}

static int tb_tunnel_pci(struct tb *tb, struct tb_switch *sw)
{
	struct tb_port *up, *down, *port;
	struct tb_cm *tcm = tb_priv(tb);
	struct tb_tunnel *tunnel;

	up = tb_switch_find_port(sw, TB_TYPE_PCIE_UP);
	if (!up)
		return 0;

	/*
	 * Look up available down port. Since we are chaining it should
	 * be found right above this switch.
	 */
	port = tb_switch_downstream_port(sw);
	down = tb_find_pcie_down(tb_switch_parent(sw), port);
	if (!down)
		return 0;

	tunnel = tb_tunnel_alloc_pci(tb, up, down);
	if (!tunnel)
		return -ENOMEM;

	if (tb_tunnel_activate(tunnel)) {
		tb_port_info(up,
			     "PCIe tunnel activation failed, aborting\n");
		tb_tunnel_free(tunnel);
		return -EIO;
	}

	/*
	 * PCIe L1 is needed to enable CL0s for Titan Ridge so enable it
	 * here.
	 */
	if (tb_switch_pcie_l1_enable(sw))
		tb_sw_warn(sw, "failed to enable PCIe L1 for Titan Ridge\n");

	if (tb_switch_xhci_connect(sw))
		tb_sw_warn(sw, "failed to connect xHCI\n");

	list_add_tail(&tunnel->list, &tcm->tunnel_list);
	return 0;
}

static int tb_approve_xdomain_paths(struct tb *tb, struct tb_xdomain *xd,
				    int transmit_path, int transmit_ring,
				    int receive_path, int receive_ring)
{
	struct tb_cm *tcm = tb_priv(tb);
	struct tb_port *nhi_port, *dst_port;
	struct tb_tunnel *tunnel;
	struct tb_switch *sw;
	int ret;

	sw = tb_to_switch(xd->dev.parent);
	dst_port = tb_port_at(xd->route, sw);
	nhi_port = tb_switch_find_port(tb->root_switch, TB_TYPE_NHI);

	mutex_lock(&tb->lock);

	/*
	 * When tunneling DMA paths the link should not enter CL states
	 * so disable them now.
	 */
	tb_disable_clx(sw);

	tunnel = tb_tunnel_alloc_dma(tb, nhi_port, dst_port, transmit_path,
				     transmit_ring, receive_path, receive_ring);
	if (!tunnel) {
		ret = -ENOMEM;
		goto err_clx;
	}

	if (tb_tunnel_activate(tunnel)) {
		tb_port_info(nhi_port,
			     "DMA tunnel activation failed, aborting\n");
		ret = -EIO;
		goto err_free;
	}

	list_add_tail(&tunnel->list, &tcm->tunnel_list);
	mutex_unlock(&tb->lock);
	return 0;

err_free:
	tb_tunnel_free(tunnel);
err_clx:
	tb_enable_clx(sw);
	mutex_unlock(&tb->lock);

	return ret;
}

static void __tb_disconnect_xdomain_paths(struct tb *tb, struct tb_xdomain *xd,
					  int transmit_path, int transmit_ring,
					  int receive_path, int receive_ring)
{
	struct tb_cm *tcm = tb_priv(tb);
	struct tb_port *nhi_port, *dst_port;
	struct tb_tunnel *tunnel, *n;
	struct tb_switch *sw;

	sw = tb_to_switch(xd->dev.parent);
	dst_port = tb_port_at(xd->route, sw);
	nhi_port = tb_switch_find_port(tb->root_switch, TB_TYPE_NHI);

	list_for_each_entry_safe(tunnel, n, &tcm->tunnel_list, list) {
		if (!tb_tunnel_is_dma(tunnel))
			continue;
		if (tunnel->src_port != nhi_port || tunnel->dst_port != dst_port)
			continue;

		if (tb_tunnel_match_dma(tunnel, transmit_path, transmit_ring,
					receive_path, receive_ring))
			tb_deactivate_and_free_tunnel(tunnel);
	}

	/*
	 * Try to re-enable CL states now, it is OK if this fails
	 * because we may still have another DMA tunnel active through
	 * the same host router USB4 downstream port.
	 */
	tb_enable_clx(sw);
}

static int tb_disconnect_xdomain_paths(struct tb *tb, struct tb_xdomain *xd,
				       int transmit_path, int transmit_ring,
				       int receive_path, int receive_ring)
{
	if (!xd->is_unplugged) {
		mutex_lock(&tb->lock);
		__tb_disconnect_xdomain_paths(tb, xd, transmit_path,
					      transmit_ring, receive_path,
					      receive_ring);
		mutex_unlock(&tb->lock);
	}
	return 0;
}

/* hotplug handling */

/*
 * tb_handle_hotplug() - handle hotplug event
 *
 * Executes on tb->wq.
 */
static void tb_handle_hotplug(struct work_struct *work)
{
	struct tb_hotplug_event *ev = container_of(work, typeof(*ev), work);
	struct tb *tb = ev->tb;
	struct tb_cm *tcm = tb_priv(tb);
	struct tb_switch *sw;
	struct tb_port *port;

	/* Bring the domain back from sleep if it was suspended */
	pm_runtime_get_sync(&tb->dev);

	mutex_lock(&tb->lock);
	if (!tcm->hotplug_active)
		goto out; /* during init, suspend or shutdown */

	sw = tb_switch_find_by_route(tb, ev->route);
	if (!sw) {
		tb_warn(tb,
			"hotplug event from non existent switch %llx:%x (unplug: %d)\n",
			ev->route, ev->port, ev->unplug);
		goto out;
	}
	if (ev->port > sw->config.max_port_number) {
		tb_warn(tb,
			"hotplug event from non existent port %llx:%x (unplug: %d)\n",
			ev->route, ev->port, ev->unplug);
		goto put_sw;
	}
	port = &sw->ports[ev->port];
	if (tb_is_upstream_port(port)) {
		tb_dbg(tb, "hotplug event for upstream port %llx:%x (unplug: %d)\n",
		       ev->route, ev->port, ev->unplug);
		goto put_sw;
	}

	pm_runtime_get_sync(&sw->dev);

	if (ev->unplug) {
		tb_retimer_remove_all(port);

		if (tb_port_has_remote(port)) {
			tb_port_dbg(port, "switch unplugged\n");
			tb_sw_set_unplugged(port->remote->sw);
			tb_free_invalid_tunnels(tb);
			tb_remove_dp_resources(port->remote->sw);
			tb_switch_tmu_disable(port->remote->sw);
			tb_switch_unconfigure_link(port->remote->sw);
			tb_switch_lane_bonding_disable(port->remote->sw);
			tb_switch_remove(port->remote->sw);
			port->remote = NULL;
			if (port->dual_link_port)
				port->dual_link_port->remote = NULL;
			/* Maybe we can create another DP tunnel */
			tb_recalc_estimated_bandwidth(tb);
			tb_tunnel_dp(tb);
		} else if (port->xdomain) {
			struct tb_xdomain *xd = tb_xdomain_get(port->xdomain);

			tb_port_dbg(port, "xdomain unplugged\n");
			/*
			 * Service drivers are unbound during
			 * tb_xdomain_remove() so setting XDomain as
			 * unplugged here prevents deadlock if they call
			 * tb_xdomain_disable_paths(). We will tear down
			 * all the tunnels below.
			 */
			xd->is_unplugged = true;
			tb_xdomain_remove(xd);
			port->xdomain = NULL;
			__tb_disconnect_xdomain_paths(tb, xd, -1, -1, -1, -1);
			tb_xdomain_put(xd);
			tb_port_unconfigure_xdomain(port);
		} else if (tb_port_is_dpout(port) || tb_port_is_dpin(port)) {
			tb_dp_resource_unavailable(tb, port);
		} else if (!port->port) {
			tb_sw_dbg(sw, "xHCI disconnect request\n");
			tb_switch_xhci_disconnect(sw);
		} else {
			tb_port_dbg(port,
				   "got unplug event for disconnected port, ignoring\n");
		}
	} else if (port->remote) {
		tb_port_dbg(port, "got plug event for connected port, ignoring\n");
	} else if (!port->port && sw->authorized) {
		tb_sw_dbg(sw, "xHCI connect request\n");
		tb_switch_xhci_connect(sw);
	} else {
		if (tb_port_is_null(port)) {
			tb_port_dbg(port, "hotplug: scanning\n");
			tb_scan_port(port);
			if (!port->remote)
				tb_port_dbg(port, "hotplug: no switch found\n");
		} else if (tb_port_is_dpout(port) || tb_port_is_dpin(port)) {
			tb_dp_resource_available(tb, port);
		}
	}

	pm_runtime_mark_last_busy(&sw->dev);
	pm_runtime_put_autosuspend(&sw->dev);

put_sw:
	tb_switch_put(sw);
out:
	mutex_unlock(&tb->lock);

	pm_runtime_mark_last_busy(&tb->dev);
	pm_runtime_put_autosuspend(&tb->dev);

	kfree(ev);
}

static int tb_alloc_dp_bandwidth(struct tb_tunnel *tunnel, int *requested_up,
				 int *requested_down)
{
	int allocated_up, allocated_down, available_up, available_down, ret;
	int requested_up_corrected, requested_down_corrected, granularity;
	int max_up, max_down, max_up_rounded, max_down_rounded;
	struct tb *tb = tunnel->tb;
	struct tb_port *in, *out;

	ret = tb_tunnel_allocated_bandwidth(tunnel, &allocated_up, &allocated_down);
	if (ret)
		return ret;

	in = tunnel->src_port;
	out = tunnel->dst_port;

	tb_port_dbg(in, "bandwidth allocated currently %d/%d Mb/s\n",
		    allocated_up, allocated_down);

	/*
	 * If we get rounded up request from graphics side, say HBR2 x 4
	 * that is 17500 instead of 17280 (this is because of the
	 * granularity), we allow it too. Here the graphics has already
	 * negotiated with the DPRX the maximum possible rates (which is
	 * 17280 in this case).
	 *
	 * Since the link cannot go higher than 17280 we use that in our
	 * calculations but the DP IN adapter Allocated BW write must be
	 * the same value (17500) otherwise the adapter will mark it as
	 * failed for graphics.
	 */
	ret = tb_tunnel_maximum_bandwidth(tunnel, &max_up, &max_down);
	if (ret)
		return ret;

	ret = usb4_dp_port_granularity(in);
	if (ret < 0)
		return ret;
	granularity = ret;

	max_up_rounded = roundup(max_up, granularity);
	max_down_rounded = roundup(max_down, granularity);

	/*
	 * This will "fix" the request down to the maximum supported
	 * rate * lanes if it is at the maximum rounded up level.
	 */
	requested_up_corrected = *requested_up;
	if (requested_up_corrected == max_up_rounded)
		requested_up_corrected = max_up;
	else if (requested_up_corrected < 0)
		requested_up_corrected = 0;
	requested_down_corrected = *requested_down;
	if (requested_down_corrected == max_down_rounded)
		requested_down_corrected = max_down;
	else if (requested_down_corrected < 0)
		requested_down_corrected = 0;

	tb_port_dbg(in, "corrected bandwidth request %d/%d Mb/s\n",
		    requested_up_corrected, requested_down_corrected);

	if ((*requested_up >= 0 && requested_up_corrected > max_up_rounded) ||
	    (*requested_down >= 0 && requested_down_corrected > max_down_rounded)) {
		tb_port_dbg(in, "bandwidth request too high (%d/%d Mb/s > %d/%d Mb/s)\n",
			    requested_up_corrected, requested_down_corrected,
			    max_up_rounded, max_down_rounded);
		return -ENOBUFS;
	}

	if ((*requested_up >= 0 && requested_up_corrected <= allocated_up) ||
	    (*requested_down >= 0 && requested_down_corrected <= allocated_down)) {
		/*
		 * If requested bandwidth is less or equal than what is
		 * currently allocated to that tunnel we simply change
		 * the reservation of the tunnel. Since all the tunnels
		 * going out from the same USB4 port are in the same
		 * group the released bandwidth will be taken into
		 * account for the other tunnels automatically below.
		 */
		return tb_tunnel_alloc_bandwidth(tunnel, requested_up,
						 requested_down);
	}

	/*
	 * More bandwidth is requested. Release all the potential
	 * bandwidth from USB3 first.
	 */
	ret = tb_release_unused_usb3_bandwidth(tb, in, out);
	if (ret)
		return ret;

	/*
	 * Then go over all tunnels that cross the same USB4 ports (they
	 * are also in the same group but we use the same function here
	 * that we use with the normal bandwidth allocation).
	 */
	ret = tb_available_bandwidth(tb, in, out, &available_up, &available_down);
	if (ret)
		goto reclaim;

	tb_port_dbg(in, "bandwidth available for allocation %d/%d Mb/s\n",
		    available_up, available_down);

	if ((*requested_up >= 0 && available_up >= requested_up_corrected) ||
	    (*requested_down >= 0 && available_down >= requested_down_corrected)) {
		ret = tb_tunnel_alloc_bandwidth(tunnel, requested_up,
						requested_down);
	} else {
		ret = -ENOBUFS;
	}

reclaim:
	tb_reclaim_usb3_bandwidth(tb, in, out);
	return ret;
}

static void tb_handle_dp_bandwidth_request(struct work_struct *work)
{
	struct tb_hotplug_event *ev = container_of(work, typeof(*ev), work);
	int requested_bw, requested_up, requested_down, ret;
	struct tb_port *in, *out;
	struct tb_tunnel *tunnel;
	struct tb *tb = ev->tb;
	struct tb_cm *tcm = tb_priv(tb);
	struct tb_switch *sw;

	pm_runtime_get_sync(&tb->dev);

	mutex_lock(&tb->lock);
	if (!tcm->hotplug_active)
		goto unlock;

	sw = tb_switch_find_by_route(tb, ev->route);
	if (!sw) {
		tb_warn(tb, "bandwidth request from non-existent router %llx\n",
			ev->route);
		goto unlock;
	}

	in = &sw->ports[ev->port];
	if (!tb_port_is_dpin(in)) {
		tb_port_warn(in, "bandwidth request to non-DP IN adapter\n");
		goto put_sw;
	}

	tb_port_dbg(in, "handling bandwidth allocation request\n");

	if (!usb4_dp_port_bandwidth_mode_enabled(in)) {
		tb_port_warn(in, "bandwidth allocation mode not enabled\n");
		goto put_sw;
	}

	ret = usb4_dp_port_requested_bandwidth(in);
	if (ret < 0) {
		if (ret == -ENODATA)
			tb_port_dbg(in, "no bandwidth request active\n");
		else
			tb_port_warn(in, "failed to read requested bandwidth\n");
		goto put_sw;
	}
	requested_bw = ret;

	tb_port_dbg(in, "requested bandwidth %d Mb/s\n", requested_bw);

	tunnel = tb_find_tunnel(tb, TB_TUNNEL_DP, in, NULL);
	if (!tunnel) {
		tb_port_warn(in, "failed to find tunnel\n");
		goto put_sw;
	}

	out = tunnel->dst_port;

	if (in->sw->config.depth < out->sw->config.depth) {
		requested_up = -1;
		requested_down = requested_bw;
	} else {
		requested_up = requested_bw;
		requested_down = -1;
	}

	ret = tb_alloc_dp_bandwidth(tunnel, &requested_up, &requested_down);
	if (ret) {
		if (ret == -ENOBUFS)
			tb_port_warn(in, "not enough bandwidth available\n");
		else
			tb_port_warn(in, "failed to change bandwidth allocation\n");
	} else {
		tb_port_dbg(in, "bandwidth allocation changed to %d/%d Mb/s\n",
			    requested_up, requested_down);

		/* Update other clients about the allocation change */
		tb_recalc_estimated_bandwidth(tb);
	}

put_sw:
	tb_switch_put(sw);
unlock:
	mutex_unlock(&tb->lock);

	pm_runtime_mark_last_busy(&tb->dev);
	pm_runtime_put_autosuspend(&tb->dev);

	kfree(ev);
}

static void tb_queue_dp_bandwidth_request(struct tb *tb, u64 route, u8 port)
{
	struct tb_hotplug_event *ev;

	ev = kmalloc(sizeof(*ev), GFP_KERNEL);
	if (!ev)
		return;

	ev->tb = tb;
	ev->route = route;
	ev->port = port;
	INIT_WORK(&ev->work, tb_handle_dp_bandwidth_request);
	queue_work(tb->wq, &ev->work);
}

static void tb_handle_notification(struct tb *tb, u64 route,
				   const struct cfg_error_pkg *error)
{

	switch (error->error) {
	case TB_CFG_ERROR_PCIE_WAKE:
	case TB_CFG_ERROR_DP_CON_CHANGE:
	case TB_CFG_ERROR_DPTX_DISCOVERY:
		if (tb_cfg_ack_notification(tb->ctl, route, error))
			tb_warn(tb, "could not ack notification on %llx\n",
				route);
		break;

	case TB_CFG_ERROR_DP_BW:
		if (tb_cfg_ack_notification(tb->ctl, route, error))
			tb_warn(tb, "could not ack notification on %llx\n",
				route);
		tb_queue_dp_bandwidth_request(tb, route, error->port);
		break;

	default:
		/* Ignore for now */
		break;
	}
}

/*
 * tb_schedule_hotplug_handler() - callback function for the control channel
 *
 * Delegates to tb_handle_hotplug.
 */
static void tb_handle_event(struct tb *tb, enum tb_cfg_pkg_type type,
			    const void *buf, size_t size)
{
	const struct cfg_event_pkg *pkg = buf;
	u64 route = tb_cfg_get_route(&pkg->header);

	switch (type) {
	case TB_CFG_PKG_ERROR:
		tb_handle_notification(tb, route, (const struct cfg_error_pkg *)buf);
		return;
	case TB_CFG_PKG_EVENT:
		break;
	default:
		tb_warn(tb, "unexpected event %#x, ignoring\n", type);
		return;
	}

	if (tb_cfg_ack_plug(tb->ctl, route, pkg->port, pkg->unplug)) {
		tb_warn(tb, "could not ack plug event on %llx:%x\n", route,
			pkg->port);
	}

	tb_queue_hotplug(tb, route, pkg->port, pkg->unplug);
}

static void tb_stop(struct tb *tb)
{
	struct tb_cm *tcm = tb_priv(tb);
	struct tb_tunnel *tunnel;
	struct tb_tunnel *n;

	cancel_delayed_work(&tcm->remove_work);
	/* tunnels are only present after everything has been initialized */
	list_for_each_entry_safe(tunnel, n, &tcm->tunnel_list, list) {
		/*
		 * DMA tunnels require the driver to be functional so we
		 * tear them down. Other protocol tunnels can be left
		 * intact.
		 */
		if (tb_tunnel_is_dma(tunnel))
			tb_tunnel_deactivate(tunnel);
		tb_tunnel_free(tunnel);
	}
	tb_switch_remove(tb->root_switch);
	tcm->hotplug_active = false; /* signal tb_handle_hotplug to quit */
}

static int tb_scan_finalize_switch(struct device *dev, void *data)
{
	if (tb_is_switch(dev)) {
		struct tb_switch *sw = tb_to_switch(dev);

		/*
		 * If we found that the switch was already setup by the
		 * boot firmware, mark it as authorized now before we
		 * send uevent to userspace.
		 */
		if (sw->boot)
			sw->authorized = 1;

		dev_set_uevent_suppress(dev, false);
		kobject_uevent(&dev->kobj, KOBJ_ADD);
		device_for_each_child(dev, NULL, tb_scan_finalize_switch);
	}

	return 0;
}

static int tb_start(struct tb *tb)
{
	struct tb_cm *tcm = tb_priv(tb);
	int ret;

	tb->root_switch = tb_switch_alloc(tb, &tb->dev, 0);
	if (IS_ERR(tb->root_switch))
		return PTR_ERR(tb->root_switch);

	/*
	 * ICM firmware upgrade needs running firmware and in native
	 * mode that is not available so disable firmware upgrade of the
	 * root switch.
	 *
	 * However, USB4 routers support NVM firmware upgrade if they
	 * implement the necessary router operations.
	 */
	tb->root_switch->no_nvm_upgrade = !tb_switch_is_usb4(tb->root_switch);
	/* All USB4 routers support runtime PM */
	tb->root_switch->rpm = tb_switch_is_usb4(tb->root_switch);

	ret = tb_switch_configure(tb->root_switch);
	if (ret) {
		tb_switch_put(tb->root_switch);
		return ret;
	}

	/* Announce the switch to the world */
	ret = tb_switch_add(tb->root_switch);
	if (ret) {
		tb_switch_put(tb->root_switch);
		return ret;
	}

	/*
	 * To support highest CLx state, we set host router's TMU to
	 * Normal mode.
	 */
	tb_switch_tmu_configure(tb->root_switch, TB_SWITCH_TMU_MODE_LOWRES);
	/* Enable TMU if it is off */
	tb_switch_tmu_enable(tb->root_switch);
	/* Full scan to discover devices added before the driver was loaded. */
	tb_scan_switch(tb->root_switch);
	/* Find out tunnels created by the boot firmware */
	tb_discover_tunnels(tb);
	/* Add DP resources from the DP tunnels created by the boot firmware */
	tb_discover_dp_resources(tb);
	/*
	 * If the boot firmware did not create USB 3.x tunnels create them
	 * now for the whole topology.
	 */
	tb_create_usb3_tunnels(tb->root_switch);
	/* Add DP IN resources for the root switch */
	tb_add_dp_resources(tb->root_switch);
	/* Make the discovered switches available to the userspace */
	device_for_each_child(&tb->root_switch->dev, NULL,
			      tb_scan_finalize_switch);

	/* Allow tb_handle_hotplug to progress events */
	tcm->hotplug_active = true;
	return 0;
}

static int tb_suspend_noirq(struct tb *tb)
{
	struct tb_cm *tcm = tb_priv(tb);

	tb_dbg(tb, "suspending...\n");
	tb_disconnect_and_release_dp(tb);
	tb_switch_suspend(tb->root_switch, false);
	tcm->hotplug_active = false; /* signal tb_handle_hotplug to quit */
	tb_dbg(tb, "suspend finished\n");

	return 0;
}

static void tb_restore_children(struct tb_switch *sw)
{
	struct tb_port *port;

	/* No need to restore if the router is already unplugged */
	if (sw->is_unplugged)
		return;

	if (tb_enable_clx(sw))
		tb_sw_warn(sw, "failed to re-enable CL states\n");

	if (tb_enable_tmu(sw))
		tb_sw_warn(sw, "failed to restore TMU configuration\n");

	tb_switch_configuration_valid(sw);

	tb_switch_for_each_port(sw, port) {
		if (!tb_port_has_remote(port) && !port->xdomain)
			continue;

		if (port->remote) {
			tb_switch_lane_bonding_enable(port->remote->sw);
			tb_switch_configure_link(port->remote->sw);

			tb_restore_children(port->remote->sw);
		} else if (port->xdomain) {
			tb_port_configure_xdomain(port, port->xdomain);
		}
	}
}

static int tb_resume_noirq(struct tb *tb)
{
	struct tb_cm *tcm = tb_priv(tb);
	struct tb_tunnel *tunnel, *n;
	unsigned int usb3_delay = 0;
	LIST_HEAD(tunnels);

	tb_dbg(tb, "resuming...\n");

	/* remove any pci devices the firmware might have setup */
	tb_switch_reset(tb->root_switch);

	tb_switch_resume(tb->root_switch);
	tb_free_invalid_tunnels(tb);
	tb_free_unplugged_children(tb->root_switch);
	tb_restore_children(tb->root_switch);

	/*
	 * If we get here from suspend to disk the boot firmware or the
	 * restore kernel might have created tunnels of its own. Since
	 * we cannot be sure they are usable for us we find and tear
	 * them down.
	 */
	tb_switch_discover_tunnels(tb->root_switch, &tunnels, false);
	list_for_each_entry_safe_reverse(tunnel, n, &tunnels, list) {
		if (tb_tunnel_is_usb3(tunnel))
			usb3_delay = 500;
		tb_tunnel_deactivate(tunnel);
		tb_tunnel_free(tunnel);
	}

	/* Re-create our tunnels now */
	list_for_each_entry_safe(tunnel, n, &tcm->tunnel_list, list) {
		/* USB3 requires delay before it can be re-activated */
		if (tb_tunnel_is_usb3(tunnel)) {
			msleep(usb3_delay);
			/* Only need to do it once */
			usb3_delay = 0;
		}
		tb_tunnel_restart(tunnel);
	}
	if (!list_empty(&tcm->tunnel_list)) {
		/*
		 * the pcie links need some time to get going.
		 * 100ms works for me...
		 */
		tb_dbg(tb, "tunnels restarted, sleeping for 100ms\n");
		msleep(100);
	}
	 /* Allow tb_handle_hotplug to progress events */
	tcm->hotplug_active = true;
	tb_dbg(tb, "resume finished\n");

	return 0;
}

static int tb_free_unplugged_xdomains(struct tb_switch *sw)
{
	struct tb_port *port;
	int ret = 0;

	tb_switch_for_each_port(sw, port) {
		if (tb_is_upstream_port(port))
			continue;
		if (port->xdomain && port->xdomain->is_unplugged) {
			tb_retimer_remove_all(port);
			tb_xdomain_remove(port->xdomain);
			tb_port_unconfigure_xdomain(port);
			port->xdomain = NULL;
			ret++;
		} else if (port->remote) {
			ret += tb_free_unplugged_xdomains(port->remote->sw);
		}
	}

	return ret;
}

static int tb_freeze_noirq(struct tb *tb)
{
	struct tb_cm *tcm = tb_priv(tb);

	tcm->hotplug_active = false;
	return 0;
}

static int tb_thaw_noirq(struct tb *tb)
{
	struct tb_cm *tcm = tb_priv(tb);

	tcm->hotplug_active = true;
	return 0;
}

static void tb_complete(struct tb *tb)
{
	/*
	 * Release any unplugged XDomains and if there is a case where
	 * another domain is swapped in place of unplugged XDomain we
	 * need to run another rescan.
	 */
	mutex_lock(&tb->lock);
	if (tb_free_unplugged_xdomains(tb->root_switch))
		tb_scan_switch(tb->root_switch);
	mutex_unlock(&tb->lock);
}

static int tb_runtime_suspend(struct tb *tb)
{
	struct tb_cm *tcm = tb_priv(tb);

	mutex_lock(&tb->lock);
	tb_switch_suspend(tb->root_switch, true);
	tcm->hotplug_active = false;
	mutex_unlock(&tb->lock);

	return 0;
}

static void tb_remove_work(struct work_struct *work)
{
	struct tb_cm *tcm = container_of(work, struct tb_cm, remove_work.work);
	struct tb *tb = tcm_to_tb(tcm);

	mutex_lock(&tb->lock);
	if (tb->root_switch) {
		tb_free_unplugged_children(tb->root_switch);
		tb_free_unplugged_xdomains(tb->root_switch);
	}
	mutex_unlock(&tb->lock);
}

static int tb_runtime_resume(struct tb *tb)
{
	struct tb_cm *tcm = tb_priv(tb);
	struct tb_tunnel *tunnel, *n;

	mutex_lock(&tb->lock);
	tb_switch_resume(tb->root_switch);
	tb_free_invalid_tunnels(tb);
	tb_restore_children(tb->root_switch);
	list_for_each_entry_safe(tunnel, n, &tcm->tunnel_list, list)
		tb_tunnel_restart(tunnel);
	tcm->hotplug_active = true;
	mutex_unlock(&tb->lock);

	/*
	 * Schedule cleanup of any unplugged devices. Run this in a
	 * separate thread to avoid possible deadlock if the device
	 * removal runtime resumes the unplugged device.
	 */
	queue_delayed_work(tb->wq, &tcm->remove_work, msecs_to_jiffies(50));
	return 0;
}

static const struct tb_cm_ops tb_cm_ops = {
	.start = tb_start,
	.stop = tb_stop,
	.suspend_noirq = tb_suspend_noirq,
	.resume_noirq = tb_resume_noirq,
	.freeze_noirq = tb_freeze_noirq,
	.thaw_noirq = tb_thaw_noirq,
	.complete = tb_complete,
	.runtime_suspend = tb_runtime_suspend,
	.runtime_resume = tb_runtime_resume,
	.handle_event = tb_handle_event,
	.disapprove_switch = tb_disconnect_pci,
	.approve_switch = tb_tunnel_pci,
	.approve_xdomain_paths = tb_approve_xdomain_paths,
	.disconnect_xdomain_paths = tb_disconnect_xdomain_paths,
};

/*
 * During suspend the Thunderbolt controller is reset and all PCIe
 * tunnels are lost. The NHI driver will try to reestablish all tunnels
 * during resume. This adds device links between the tunneled PCIe
 * downstream ports and the NHI so that the device core will make sure
 * NHI is resumed first before the rest.
 */
static bool tb_apple_add_links(struct tb_nhi *nhi)
{
	struct pci_dev *upstream, *pdev;
	bool ret;

	if (!x86_apple_machine)
		return false;

	switch (nhi->pdev->device) {
	case PCI_DEVICE_ID_INTEL_LIGHT_RIDGE:
	case PCI_DEVICE_ID_INTEL_CACTUS_RIDGE_4C:
	case PCI_DEVICE_ID_INTEL_FALCON_RIDGE_2C_NHI:
	case PCI_DEVICE_ID_INTEL_FALCON_RIDGE_4C_NHI:
		break;
	default:
		return false;
	}

	upstream = pci_upstream_bridge(nhi->pdev);
	while (upstream) {
		if (!pci_is_pcie(upstream))
			return false;
		if (pci_pcie_type(upstream) == PCI_EXP_TYPE_UPSTREAM)
			break;
		upstream = pci_upstream_bridge(upstream);
	}

	if (!upstream)
		return false;

	/*
	 * For each hotplug downstream port, create add device link
	 * back to NHI so that PCIe tunnels can be re-established after
	 * sleep.
	 */
	ret = false;
	for_each_pci_bridge(pdev, upstream->subordinate) {
		const struct device_link *link;

		if (!pci_is_pcie(pdev))
			continue;
		if (pci_pcie_type(pdev) != PCI_EXP_TYPE_DOWNSTREAM ||
		    !pdev->is_hotplug_bridge)
			continue;

		link = device_link_add(&pdev->dev, &nhi->pdev->dev,
				       DL_FLAG_AUTOREMOVE_SUPPLIER |
				       DL_FLAG_PM_RUNTIME);
		if (link) {
			dev_dbg(&nhi->pdev->dev, "created link from %s\n",
				dev_name(&pdev->dev));
			ret = true;
		} else {
			dev_warn(&nhi->pdev->dev, "device link creation from %s failed\n",
				 dev_name(&pdev->dev));
		}
	}

	return ret;
}

struct tb *tb_probe(struct tb_nhi *nhi)
{
	struct tb_cm *tcm;
	struct tb *tb;

	tb = tb_domain_alloc(nhi, TB_TIMEOUT, sizeof(*tcm));
	if (!tb)
		return NULL;

	if (tb_acpi_may_tunnel_pcie())
		tb->security_level = TB_SECURITY_USER;
	else
		tb->security_level = TB_SECURITY_NOPCIE;

	tb->cm_ops = &tb_cm_ops;

	tcm = tb_priv(tb);
	INIT_LIST_HEAD(&tcm->tunnel_list);
	INIT_LIST_HEAD(&tcm->dp_resources);
	INIT_DELAYED_WORK(&tcm->remove_work, tb_remove_work);
	tb_init_bandwidth_groups(tcm);

	tb_dbg(tb, "using software connection manager\n");

	/*
	 * Device links are needed to make sure we establish tunnels
	 * before the PCIe/USB stack is resumed so complain here if we
	 * found them missing.
	 */
	if (!tb_apple_add_links(nhi) && !tb_acpi_add_links(nhi))
		tb_warn(tb, "device links to tunneled native ports are missing!\n");

	return tb;
}
