/*
 * Thunderbolt Cactus Ridge driver - bus logic (NHI independent)
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 */

#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/delay.h>

#include "tb.h"
#include "tb_regs.h"
#include "tunnel_pci.h"


/* enumeration & hot plug handling */


static void tb_scan_port(struct tb_port *port);

/**
 * tb_scan_switch() - scan for and initialize downstream switches
 */
static void tb_scan_switch(struct tb_switch *sw)
{
	int i;
	for (i = 1; i <= sw->config.max_port_number; i++)
		tb_scan_port(&sw->ports[i]);
}

/**
 * tb_scan_port() - check for and initialize switches below port
 */
static void tb_scan_port(struct tb_port *port)
{
	struct tb_switch *sw;
	if (tb_is_upstream_port(port))
		return;
	if (port->config.type != TB_TYPE_PORT)
		return;
	if (port->dual_link_port && port->link_nr)
		return; /*
			 * Downstream switch is reachable through two ports.
			 * Only scan on the primary port (link_nr == 0).
			 */
	if (tb_wait_for_port(port, false) <= 0)
		return;
	if (port->remote) {
		tb_port_WARN(port, "port already has a remote!\n");
		return;
	}
	sw = tb_switch_alloc(port->sw->tb, tb_downstream_route(port));
	if (!sw)
		return;
	port->remote = tb_upstream_port(sw);
	tb_upstream_port(sw)->remote = port;
	tb_scan_switch(sw);
}

/**
 * tb_free_invalid_tunnels() - destroy tunnels of devices that have gone away
 */
static void tb_free_invalid_tunnels(struct tb *tb)
{
	struct tb_pci_tunnel *tunnel;
	struct tb_pci_tunnel *n;
	list_for_each_entry_safe(tunnel, n, &tb->tunnel_list, list)
	{
		if (tb_pci_is_invalid(tunnel)) {
			tb_pci_deactivate(tunnel);
			tb_pci_free(tunnel);
		}
	}
}

/**
 * tb_free_unplugged_children() - traverse hierarchy and free unplugged switches
 */
static void tb_free_unplugged_children(struct tb_switch *sw)
{
	int i;
	for (i = 1; i <= sw->config.max_port_number; i++) {
		struct tb_port *port = &sw->ports[i];
		if (tb_is_upstream_port(port))
			continue;
		if (!port->remote)
			continue;
		if (port->remote->sw->is_unplugged) {
			tb_switch_free(port->remote->sw);
			port->remote = NULL;
		} else {
			tb_free_unplugged_children(port->remote->sw);
		}
	}
}


/**
 * find_pci_up_port() - return the first PCIe up port on @sw or NULL
 */
static struct tb_port *tb_find_pci_up_port(struct tb_switch *sw)
{
	int i;
	for (i = 1; i <= sw->config.max_port_number; i++)
		if (sw->ports[i].config.type == TB_TYPE_PCIE_UP)
			return &sw->ports[i];
	return NULL;
}

/**
 * find_unused_down_port() - return the first inactive PCIe down port on @sw
 */
static struct tb_port *tb_find_unused_down_port(struct tb_switch *sw)
{
	int i;
	int cap;
	int res;
	int data;
	for (i = 1; i <= sw->config.max_port_number; i++) {
		if (tb_is_upstream_port(&sw->ports[i]))
			continue;
		if (sw->ports[i].config.type != TB_TYPE_PCIE_DOWN)
			continue;
		cap = tb_find_cap(&sw->ports[i], TB_CFG_PORT, TB_CAP_PCIE);
		if (cap <= 0)
			continue;
		res = tb_port_read(&sw->ports[i], &data, TB_CFG_PORT, cap, 1);
		if (res < 0)
			continue;
		if (data & 0x80000000)
			continue;
		return &sw->ports[i];
	}
	return NULL;
}

/**
 * tb_activate_pcie_devices() - scan for and activate PCIe devices
 *
 * This method is somewhat ad hoc. For now it only supports one device
 * per port and only devices at depth 1.
 */
static void tb_activate_pcie_devices(struct tb *tb)
{
	int i;
	int cap;
	u32 data;
	struct tb_switch *sw;
	struct tb_port *up_port;
	struct tb_port *down_port;
	struct tb_pci_tunnel *tunnel;
	/* scan for pcie devices at depth 1*/
	for (i = 1; i <= tb->root_switch->config.max_port_number; i++) {
		if (tb_is_upstream_port(&tb->root_switch->ports[i]))
			continue;
		if (tb->root_switch->ports[i].config.type != TB_TYPE_PORT)
			continue;
		if (!tb->root_switch->ports[i].remote)
			continue;
		sw = tb->root_switch->ports[i].remote->sw;
		up_port = tb_find_pci_up_port(sw);
		if (!up_port) {
			tb_sw_info(sw, "no PCIe devices found, aborting\n");
			continue;
		}

		/* check whether port is already activated */
		cap = tb_find_cap(up_port, TB_CFG_PORT, TB_CAP_PCIE);
		if (cap <= 0)
			continue;
		if (tb_port_read(up_port, &data, TB_CFG_PORT, cap, 1))
			continue;
		if (data & 0x80000000) {
			tb_port_info(up_port,
				     "PCIe port already activated, aborting\n");
			continue;
		}

		down_port = tb_find_unused_down_port(tb->root_switch);
		if (!down_port) {
			tb_port_info(up_port,
				     "All PCIe down ports are occupied, aborting\n");
			continue;
		}
		tunnel = tb_pci_alloc(tb, up_port, down_port);
		if (!tunnel) {
			tb_port_info(up_port,
				     "PCIe tunnel allocation failed, aborting\n");
			continue;
		}

		if (tb_pci_activate(tunnel)) {
			tb_port_info(up_port,
				     "PCIe tunnel activation failed, aborting\n");
			tb_pci_free(tunnel);
		}

	}
}

/* hotplug handling */

struct tb_hotplug_event {
	struct work_struct work;
	struct tb *tb;
	u64 route;
	u8 port;
	bool unplug;
};

/**
 * tb_handle_hotplug() - handle hotplug event
 *
 * Executes on tb->wq.
 */
static void tb_handle_hotplug(struct work_struct *work)
{
	struct tb_hotplug_event *ev = container_of(work, typeof(*ev), work);
	struct tb *tb = ev->tb;
	struct tb_switch *sw;
	struct tb_port *port;
	mutex_lock(&tb->lock);
	if (!tb->hotplug_active)
		goto out; /* during init, suspend or shutdown */

	sw = get_switch_at_route(tb->root_switch, ev->route);
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
		goto out;
	}
	port = &sw->ports[ev->port];
	if (tb_is_upstream_port(port)) {
		tb_warn(tb,
			"hotplug event for upstream port %llx:%x (unplug: %d)\n",
			ev->route, ev->port, ev->unplug);
		goto out;
	}
	if (ev->unplug) {
		if (port->remote) {
			tb_port_info(port, "unplugged\n");
			tb_sw_set_unpplugged(port->remote->sw);
			tb_free_invalid_tunnels(tb);
			tb_switch_free(port->remote->sw);
			port->remote = NULL;
		} else {
			tb_port_info(port,
				     "got unplug event for disconnected port, ignoring\n");
		}
	} else if (port->remote) {
		tb_port_info(port,
			     "got plug event for connected port, ignoring\n");
	} else {
		tb_port_info(port, "hotplug: scanning\n");
		tb_scan_port(port);
		if (!port->remote) {
			tb_port_info(port, "hotplug: no switch found\n");
		} else if (port->remote->sw->config.depth > 1) {
			tb_sw_warn(port->remote->sw,
				   "hotplug: chaining not supported\n");
		} else {
			tb_sw_info(port->remote->sw,
				   "hotplug: activating pcie devices\n");
			tb_activate_pcie_devices(tb);
		}
	}
out:
	mutex_unlock(&tb->lock);
	kfree(ev);
}

/**
 * tb_schedule_hotplug_handler() - callback function for the control channel
 *
 * Delegates to tb_handle_hotplug.
 */
static void tb_schedule_hotplug_handler(void *data, u64 route, u8 port,
					bool unplug)
{
	struct tb *tb = data;
	struct tb_hotplug_event *ev = kmalloc(sizeof(*ev), GFP_KERNEL);
	if (!ev)
		return;
	INIT_WORK(&ev->work, tb_handle_hotplug);
	ev->tb = tb;
	ev->route = route;
	ev->port = port;
	ev->unplug = unplug;
	queue_work(tb->wq, &ev->work);
}

/**
 * thunderbolt_shutdown_and_free() - shutdown everything
 *
 * Free all switches and the config channel.
 *
 * Used in the error path of thunderbolt_alloc_and_start.
 */
void thunderbolt_shutdown_and_free(struct tb *tb)
{
	struct tb_pci_tunnel *tunnel;
	struct tb_pci_tunnel *n;

	mutex_lock(&tb->lock);

	/* tunnels are only present after everything has been initialized */
	list_for_each_entry_safe(tunnel, n, &tb->tunnel_list, list) {
		tb_pci_deactivate(tunnel);
		tb_pci_free(tunnel);
	}

	if (tb->root_switch)
		tb_switch_free(tb->root_switch);
	tb->root_switch = NULL;

	if (tb->ctl) {
		tb_ctl_stop(tb->ctl);
		tb_ctl_free(tb->ctl);
	}
	tb->ctl = NULL;
	tb->hotplug_active = false; /* signal tb_handle_hotplug to quit */

	/* allow tb_handle_hotplug to acquire the lock */
	mutex_unlock(&tb->lock);
	if (tb->wq) {
		flush_workqueue(tb->wq);
		destroy_workqueue(tb->wq);
		tb->wq = NULL;
	}
	mutex_destroy(&tb->lock);
	kfree(tb);
}

/**
 * thunderbolt_alloc_and_start() - setup the thunderbolt bus
 *
 * Allocates a tb_cfg control channel, initializes the root switch, enables
 * plug events and activates pci devices.
 *
 * Return: Returns NULL on error.
 */
struct tb *thunderbolt_alloc_and_start(struct tb_nhi *nhi)
{
	struct tb *tb;

	BUILD_BUG_ON(sizeof(struct tb_regs_switch_header) != 5 * 4);
	BUILD_BUG_ON(sizeof(struct tb_regs_port_header) != 8 * 4);
	BUILD_BUG_ON(sizeof(struct tb_regs_hop) != 2 * 4);

	tb = kzalloc(sizeof(*tb), GFP_KERNEL);
	if (!tb)
		return NULL;

	tb->nhi = nhi;
	mutex_init(&tb->lock);
	mutex_lock(&tb->lock);
	INIT_LIST_HEAD(&tb->tunnel_list);

	tb->wq = alloc_ordered_workqueue("thunderbolt", 0);
	if (!tb->wq)
		goto err_locked;

	tb->ctl = tb_ctl_alloc(tb->nhi, tb_schedule_hotplug_handler, tb);
	if (!tb->ctl)
		goto err_locked;
	/*
	 * tb_schedule_hotplug_handler may be called as soon as the config
	 * channel is started. Thats why we have to hold the lock here.
	 */
	tb_ctl_start(tb->ctl);

	tb->root_switch = tb_switch_alloc(tb, 0);
	if (!tb->root_switch)
		goto err_locked;

	/* Full scan to discover devices added before the driver was loaded. */
	tb_scan_switch(tb->root_switch);
	tb_activate_pcie_devices(tb);

	/* Allow tb_handle_hotplug to progress events */
	tb->hotplug_active = true;
	mutex_unlock(&tb->lock);
	return tb;

err_locked:
	mutex_unlock(&tb->lock);
	thunderbolt_shutdown_and_free(tb);
	return NULL;
}

void thunderbolt_suspend(struct tb *tb)
{
	tb_info(tb, "suspending...\n");
	mutex_lock(&tb->lock);
	tb_switch_suspend(tb->root_switch);
	tb_ctl_stop(tb->ctl);
	tb->hotplug_active = false; /* signal tb_handle_hotplug to quit */
	mutex_unlock(&tb->lock);
	tb_info(tb, "suspend finished\n");
}

void thunderbolt_resume(struct tb *tb)
{
	struct tb_pci_tunnel *tunnel, *n;
	tb_info(tb, "resuming...\n");
	mutex_lock(&tb->lock);
	tb_ctl_start(tb->ctl);

	/* remove any pci devices the firmware might have setup */
	tb_switch_reset(tb, 0);

	tb_switch_resume(tb->root_switch);
	tb_free_invalid_tunnels(tb);
	tb_free_unplugged_children(tb->root_switch);
	list_for_each_entry_safe(tunnel, n, &tb->tunnel_list, list)
		tb_pci_restart(tunnel);
	if (!list_empty(&tb->tunnel_list)) {
		/*
		 * the pcie links need some time to get going.
		 * 100ms works for me...
		 */
		tb_info(tb, "tunnels restarted, sleeping for 100ms\n");
		msleep(100);
	}
	 /* Allow tb_handle_hotplug to progress events */
	tb->hotplug_active = true;
	mutex_unlock(&tb->lock);
	tb_info(tb, "resume finished\n");
}
