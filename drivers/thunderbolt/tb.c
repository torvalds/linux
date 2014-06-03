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
	mutex_lock(&tb->lock);

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

	/* Allow tb_handle_hotplug to progress events */
	tb->hotplug_active = true;
	mutex_unlock(&tb->lock);
	return tb;

err_locked:
	mutex_unlock(&tb->lock);
	thunderbolt_shutdown_and_free(tb);
	return NULL;
}

