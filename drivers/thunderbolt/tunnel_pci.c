/*
 * Thunderbolt Cactus Ridge driver - PCIe tunnel
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 */

#include <linux/slab.h>
#include <linux/list.h>

#include "tunnel_pci.h"
#include "tb.h"

#define __TB_TUNNEL_PRINT(level, tunnel, fmt, arg...)                   \
	do {                                                            \
		struct tb_pci_tunnel *__tunnel = (tunnel);              \
		level(__tunnel->tb, "%llx:%x <-> %llx:%x (PCI): " fmt,  \
		      tb_route(__tunnel->down_port->sw),                \
		      __tunnel->down_port->port,                        \
		      tb_route(__tunnel->up_port->sw),                  \
		      __tunnel->up_port->port,                          \
		      ## arg);                                          \
	} while (0)

#define tb_tunnel_WARN(tunnel, fmt, arg...) \
	__TB_TUNNEL_PRINT(tb_WARN, tunnel, fmt, ##arg)
#define tb_tunnel_warn(tunnel, fmt, arg...) \
	__TB_TUNNEL_PRINT(tb_warn, tunnel, fmt, ##arg)
#define tb_tunnel_info(tunnel, fmt, arg...) \
	__TB_TUNNEL_PRINT(tb_info, tunnel, fmt, ##arg)

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
}

/**
 * tb_pci_alloc() - allocate a pci tunnel
 *
 * Allocate a PCI tunnel. The ports must be of type TB_TYPE_PCIE_UP and
 * TB_TYPE_PCIE_DOWN.
 *
 * Currently only paths consisting of two hops are supported (that is the
 * ports must be on "adjacent" switches).
 *
 * The paths are hard-coded to use hop 8 (the only working hop id available on
 * my thunderbolt devices). Therefore at most ONE path per device may be
 * activated.
 *
 * Return: Returns a tb_pci_tunnel on success or NULL on failure.
 */
struct tb_pci_tunnel *tb_pci_alloc(struct tb *tb, struct tb_port *up,
				   struct tb_port *down)
{
	struct tb_pci_tunnel *tunnel = kzalloc(sizeof(*tunnel), GFP_KERNEL);
	if (!tunnel)
		goto err;
	tunnel->tb = tb;
	tunnel->down_port = down;
	tunnel->up_port = up;
	INIT_LIST_HEAD(&tunnel->list);
	tunnel->path_to_up = tb_path_alloc(up->sw->tb, 2);
	if (!tunnel->path_to_up)
		goto err;
	tunnel->path_to_down = tb_path_alloc(up->sw->tb, 2);
	if (!tunnel->path_to_down)
		goto err;
	tb_pci_init_path(tunnel->path_to_up);
	tb_pci_init_path(tunnel->path_to_down);

	tunnel->path_to_up->hops[0].in_port = down;
	tunnel->path_to_up->hops[0].in_hop_index = 8;
	tunnel->path_to_up->hops[0].in_counter_index = -1;
	tunnel->path_to_up->hops[0].out_port = tb_upstream_port(up->sw)->remote;
	tunnel->path_to_up->hops[0].next_hop_index = 8;

	tunnel->path_to_up->hops[1].in_port = tb_upstream_port(up->sw);
	tunnel->path_to_up->hops[1].in_hop_index = 8;
	tunnel->path_to_up->hops[1].in_counter_index = -1;
	tunnel->path_to_up->hops[1].out_port = up;
	tunnel->path_to_up->hops[1].next_hop_index = 8;

	tunnel->path_to_down->hops[0].in_port = up;
	tunnel->path_to_down->hops[0].in_hop_index = 8;
	tunnel->path_to_down->hops[0].in_counter_index = -1;
	tunnel->path_to_down->hops[0].out_port = tb_upstream_port(up->sw);
	tunnel->path_to_down->hops[0].next_hop_index = 8;

	tunnel->path_to_down->hops[1].in_port =
		tb_upstream_port(up->sw)->remote;
	tunnel->path_to_down->hops[1].in_hop_index = 8;
	tunnel->path_to_down->hops[1].in_counter_index = -1;
	tunnel->path_to_down->hops[1].out_port = down;
	tunnel->path_to_down->hops[1].next_hop_index = 8;
	return tunnel;

err:
	if (tunnel) {
		if (tunnel->path_to_down)
			tb_path_free(tunnel->path_to_down);
		if (tunnel->path_to_up)
			tb_path_free(tunnel->path_to_up);
		kfree(tunnel);
	}
	return NULL;
}

/**
 * tb_pci_free() - free a tunnel
 *
 * The tunnel must have been deactivated.
 */
void tb_pci_free(struct tb_pci_tunnel *tunnel)
{
	if (tunnel->path_to_up->activated || tunnel->path_to_down->activated) {
		tb_tunnel_WARN(tunnel, "trying to free an activated tunnel\n");
		return;
	}
	tb_path_free(tunnel->path_to_up);
	tb_path_free(tunnel->path_to_down);
	kfree(tunnel);
}

/**
 * tb_pci_is_invalid - check whether an activated path is still valid
 */
bool tb_pci_is_invalid(struct tb_pci_tunnel *tunnel)
{
	WARN_ON(!tunnel->path_to_up->activated);
	WARN_ON(!tunnel->path_to_down->activated);

	return tb_path_is_invalid(tunnel->path_to_up)
	       || tb_path_is_invalid(tunnel->path_to_down);
}

/**
 * tb_pci_port_active() - activate/deactivate PCI capability
 *
 * Return: Returns 0 on success or an error code on failure.
 */
static int tb_pci_port_active(struct tb_port *port, bool active)
{
	u32 word = active ? 0x80000000 : 0x0;
	int cap = tb_find_cap(port, TB_CFG_PORT, TB_CAP_PCIE);
	if (cap <= 0) {
		tb_port_warn(port, "TB_CAP_PCIE not found: %d\n", cap);
		return cap ? cap : -ENXIO;
	}
	return tb_port_write(port, &word, TB_CFG_PORT, cap, 1);
}

/**
 * tb_pci_restart() - activate a tunnel after a hardware reset
 */
int tb_pci_restart(struct tb_pci_tunnel *tunnel)
{
	int res;
	tunnel->path_to_up->activated = false;
	tunnel->path_to_down->activated = false;

	tb_tunnel_info(tunnel, "activating\n");

	res = tb_path_activate(tunnel->path_to_up);
	if (res)
		goto err;
	res = tb_path_activate(tunnel->path_to_down);
	if (res)
		goto err;

	res = tb_pci_port_active(tunnel->down_port, true);
	if (res)
		goto err;

	res = tb_pci_port_active(tunnel->up_port, true);
	if (res)
		goto err;
	return 0;
err:
	tb_tunnel_warn(tunnel, "activation failed\n");
	tb_pci_deactivate(tunnel);
	return res;
}

/**
 * tb_pci_activate() - activate a tunnel
 *
 * Return: Returns 0 on success or an error code on failure.
 */
int tb_pci_activate(struct tb_pci_tunnel *tunnel)
{
	int res;
	if (tunnel->path_to_up->activated || tunnel->path_to_down->activated) {
		tb_tunnel_WARN(tunnel,
			       "trying to activate an already activated tunnel\n");
		return -EINVAL;
	}

	res = tb_pci_restart(tunnel);
	if (res)
		return res;

	list_add(&tunnel->list, &tunnel->tb->tunnel_list);
	return 0;
}



/**
 * tb_pci_deactivate() - deactivate a tunnel
 */
void tb_pci_deactivate(struct tb_pci_tunnel *tunnel)
{
	tb_tunnel_info(tunnel, "deactivating\n");
	/*
	 * TODO: enable reset by writing 0x04000000 to TB_CAP_PCIE + 1 on up
	 * port. Seems to have no effect?
	 */
	tb_pci_port_active(tunnel->up_port, false);
	tb_pci_port_active(tunnel->down_port, false);
	if (tunnel->path_to_down->activated)
		tb_path_deactivate(tunnel->path_to_down);
	if (tunnel->path_to_up->activated)
		tb_path_deactivate(tunnel->path_to_up);
	list_del_init(&tunnel->list);
}

