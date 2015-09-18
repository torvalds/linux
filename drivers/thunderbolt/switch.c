/*
 * Thunderbolt Cactus Ridge driver - switch/port utility functions
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 */

#include <linux/delay.h>
#include <linux/slab.h>

#include "tb.h"

/* port utility functions */

static const char *tb_port_type(struct tb_regs_port_header *port)
{
	switch (port->type >> 16) {
	case 0:
		switch ((u8) port->type) {
		case 0:
			return "Inactive";
		case 1:
			return "Port";
		case 2:
			return "NHI";
		default:
			return "unknown";
		}
	case 0x2:
		return "Ethernet";
	case 0x8:
		return "SATA";
	case 0xe:
		return "DP/HDMI";
	case 0x10:
		return "PCIe";
	case 0x20:
		return "USB";
	default:
		return "unknown";
	}
}

static void tb_dump_port(struct tb *tb, struct tb_regs_port_header *port)
{
	tb_info(tb,
		" Port %d: %x:%x (Revision: %d, TB Version: %d, Type: %s (%#x))\n",
		port->port_number, port->vendor_id, port->device_id,
		port->revision, port->thunderbolt_version, tb_port_type(port),
		port->type);
	tb_info(tb, "  Max hop id (in/out): %d/%d\n",
		port->max_in_hop_id, port->max_out_hop_id);
	tb_info(tb, "  Max counters: %d\n", port->max_counters);
	tb_info(tb, "  NFC Credits: %#x\n", port->nfc_credits);
}

/**
 * tb_port_state() - get connectedness state of a port
 *
 * The port must have a TB_CAP_PHY (i.e. it should be a real port).
 *
 * Return: Returns an enum tb_port_state on success or an error code on failure.
 */
static int tb_port_state(struct tb_port *port)
{
	struct tb_cap_phy phy;
	int res;
	if (port->cap_phy == 0) {
		tb_port_WARN(port, "does not have a PHY\n");
		return -EINVAL;
	}
	res = tb_port_read(port, &phy, TB_CFG_PORT, port->cap_phy, 2);
	if (res)
		return res;
	return phy.state;
}

/**
 * tb_wait_for_port() - wait for a port to become ready
 *
 * Wait up to 1 second for a port to reach state TB_PORT_UP. If
 * wait_if_unplugged is set then we also wait if the port is in state
 * TB_PORT_UNPLUGGED (it takes a while for the device to be registered after
 * switch resume). Otherwise we only wait if a device is registered but the link
 * has not yet been established.
 *
 * Return: Returns an error code on failure. Returns 0 if the port is not
 * connected or failed to reach state TB_PORT_UP within one second. Returns 1
 * if the port is connected and in state TB_PORT_UP.
 */
int tb_wait_for_port(struct tb_port *port, bool wait_if_unplugged)
{
	int retries = 10;
	int state;
	if (!port->cap_phy) {
		tb_port_WARN(port, "does not have PHY\n");
		return -EINVAL;
	}
	if (tb_is_upstream_port(port)) {
		tb_port_WARN(port, "is the upstream port\n");
		return -EINVAL;
	}

	while (retries--) {
		state = tb_port_state(port);
		if (state < 0)
			return state;
		if (state == TB_PORT_DISABLED) {
			tb_port_info(port, "is disabled (state: 0)\n");
			return 0;
		}
		if (state == TB_PORT_UNPLUGGED) {
			if (wait_if_unplugged) {
				/* used during resume */
				tb_port_info(port,
					     "is unplugged (state: 7), retrying...\n");
				msleep(100);
				continue;
			}
			tb_port_info(port, "is unplugged (state: 7)\n");
			return 0;
		}
		if (state == TB_PORT_UP) {
			tb_port_info(port,
				     "is connected, link is up (state: 2)\n");
			return 1;
		}

		/*
		 * After plug-in the state is TB_PORT_CONNECTING. Give it some
		 * time.
		 */
		tb_port_info(port,
			     "is connected, link is not up (state: %d), retrying...\n",
			     state);
		msleep(100);
	}
	tb_port_warn(port,
		     "failed to reach state TB_PORT_UP. Ignoring port...\n");
	return 0;
}

/**
 * tb_port_add_nfc_credits() - add/remove non flow controlled credits to port
 *
 * Change the number of NFC credits allocated to @port by @credits. To remove
 * NFC credits pass a negative amount of credits.
 *
 * Return: Returns 0 on success or an error code on failure.
 */
int tb_port_add_nfc_credits(struct tb_port *port, int credits)
{
	if (credits == 0)
		return 0;
	tb_port_info(port,
		     "adding %#x NFC credits (%#x -> %#x)",
		     credits,
		     port->config.nfc_credits,
		     port->config.nfc_credits + credits);
	port->config.nfc_credits += credits;
	return tb_port_write(port, &port->config.nfc_credits,
			     TB_CFG_PORT, 4, 1);
}

/**
 * tb_port_clear_counter() - clear a counter in TB_CFG_COUNTER
 *
 * Return: Returns 0 on success or an error code on failure.
 */
int tb_port_clear_counter(struct tb_port *port, int counter)
{
	u32 zero[3] = { 0, 0, 0 };
	tb_port_info(port, "clearing counter %d\n", counter);
	return tb_port_write(port, zero, TB_CFG_COUNTERS, 3 * counter, 3);
}

/**
 * tb_init_port() - initialize a port
 *
 * This is a helper method for tb_switch_alloc. Does not check or initialize
 * any downstream switches.
 *
 * Return: Returns 0 on success or an error code on failure.
 */
static int tb_init_port(struct tb_port *port)
{
	int res;
	int cap;

	res = tb_port_read(port, &port->config, TB_CFG_PORT, 0, 8);
	if (res)
		return res;

	/* Port 0 is the switch itself and has no PHY. */
	if (port->config.type == TB_TYPE_PORT && port->port != 0) {
		cap = tb_find_cap(port, TB_CFG_PORT, TB_CAP_PHY);

		if (cap > 0)
			port->cap_phy = cap;
		else
			tb_port_WARN(port, "non switch port without a PHY\n");
	}

	tb_dump_port(port->sw->tb, &port->config);

	/* TODO: Read dual link port, DP port and more from EEPROM. */
	return 0;

}

/* switch utility functions */

static void tb_dump_switch(struct tb *tb, struct tb_regs_switch_header *sw)
{
	tb_info(tb,
		" Switch: %x:%x (Revision: %d, TB Version: %d)\n",
		sw->vendor_id, sw->device_id, sw->revision,
		sw->thunderbolt_version);
	tb_info(tb, "  Max Port Number: %d\n", sw->max_port_number);
	tb_info(tb, "  Config:\n");
	tb_info(tb,
		"   Upstream Port Number: %d Depth: %d Route String: %#llx Enabled: %d, PlugEventsDelay: %dms\n",
		sw->upstream_port_number, sw->depth,
		(((u64) sw->route_hi) << 32) | sw->route_lo,
		sw->enabled, sw->plug_events_delay);
	tb_info(tb,
		"   unknown1: %#x unknown4: %#x\n",
		sw->__unknown1, sw->__unknown4);
}

/**
 * reset_switch() - reconfigure route, enable and send TB_CFG_PKG_RESET
 *
 * Return: Returns 0 on success or an error code on failure.
 */
int tb_switch_reset(struct tb *tb, u64 route)
{
	struct tb_cfg_result res;
	struct tb_regs_switch_header header = {
		header.route_hi = route >> 32,
		header.route_lo = route,
		header.enabled = true,
	};
	tb_info(tb, "resetting switch at %llx\n", route);
	res.err = tb_cfg_write(tb->ctl, ((u32 *) &header) + 2, route,
			0, 2, 2, 2);
	if (res.err)
		return res.err;
	res = tb_cfg_reset(tb->ctl, route, TB_CFG_DEFAULT_TIMEOUT);
	if (res.err > 0)
		return -EIO;
	return res.err;
}

struct tb_switch *get_switch_at_route(struct tb_switch *sw, u64 route)
{
	u8 next_port = route; /*
			       * Routes use a stride of 8 bits,
			       * eventhough a port index has 6 bits at most.
			       * */
	if (route == 0)
		return sw;
	if (next_port > sw->config.max_port_number)
		return NULL;
	if (tb_is_upstream_port(&sw->ports[next_port]))
		return NULL;
	if (!sw->ports[next_port].remote)
		return NULL;
	return get_switch_at_route(sw->ports[next_port].remote->sw,
				   route >> TB_ROUTE_SHIFT);
}

/**
 * tb_plug_events_active() - enable/disable plug events on a switch
 *
 * Also configures a sane plug_events_delay of 255ms.
 *
 * Return: Returns 0 on success or an error code on failure.
 */
static int tb_plug_events_active(struct tb_switch *sw, bool active)
{
	u32 data;
	int res;

	sw->config.plug_events_delay = 0xff;
	res = tb_sw_write(sw, ((u32 *) &sw->config) + 4, TB_CFG_SWITCH, 4, 1);
	if (res)
		return res;

	res = tb_sw_read(sw, &data, TB_CFG_SWITCH, sw->cap_plug_events + 1, 1);
	if (res)
		return res;

	if (active) {
		data = data & 0xFFFFFF83;
		switch (sw->config.device_id) {
		case 0x1513:
		case 0x151a:
		case 0x1549:
			break;
		default:
			data |= 4;
		}
	} else {
		data = data | 0x7c;
	}
	return tb_sw_write(sw, &data, TB_CFG_SWITCH,
			   sw->cap_plug_events + 1, 1);
}


/**
 * tb_switch_free() - free a tb_switch and all downstream switches
 */
void tb_switch_free(struct tb_switch *sw)
{
	int i;
	/* port 0 is the switch itself and never has a remote */
	for (i = 1; i <= sw->config.max_port_number; i++) {
		if (tb_is_upstream_port(&sw->ports[i]))
			continue;
		if (sw->ports[i].remote)
			tb_switch_free(sw->ports[i].remote->sw);
		sw->ports[i].remote = NULL;
	}

	if (!sw->is_unplugged)
		tb_plug_events_active(sw, false);

	kfree(sw->ports);
	kfree(sw->drom);
	kfree(sw);
}

/**
 * tb_switch_alloc() - allocate and initialize a switch
 *
 * Return: Returns a NULL on failure.
 */
struct tb_switch *tb_switch_alloc(struct tb *tb, u64 route)
{
	int i;
	int cap;
	struct tb_switch *sw;
	int upstream_port = tb_cfg_get_upstream_port(tb->ctl, route);
	if (upstream_port < 0)
		return NULL;

	sw = kzalloc(sizeof(*sw), GFP_KERNEL);
	if (!sw)
		return NULL;

	sw->tb = tb;
	if (tb_cfg_read(tb->ctl, &sw->config, route, 0, 2, 0, 5))
		goto err;
	tb_info(tb,
		"initializing Switch at %#llx (depth: %d, up port: %d)\n",
		route, tb_route_length(route), upstream_port);
	tb_info(tb, "old switch config:\n");
	tb_dump_switch(tb, &sw->config);

	/* configure switch */
	sw->config.upstream_port_number = upstream_port;
	sw->config.depth = tb_route_length(route);
	sw->config.route_lo = route;
	sw->config.route_hi = route >> 32;
	sw->config.enabled = 1;
	/* from here on we may use the tb_sw_* functions & macros */

	if (sw->config.vendor_id != 0x8086)
		tb_sw_warn(sw, "unknown switch vendor id %#x\n",
			   sw->config.vendor_id);

	if (sw->config.device_id != 0x1547 && sw->config.device_id != 0x1549)
		tb_sw_warn(sw, "unsupported switch device id %#x\n",
			   sw->config.device_id);

	/* upload configuration */
	if (tb_sw_write(sw, 1 + (u32 *) &sw->config, TB_CFG_SWITCH, 1, 3))
		goto err;

	/* initialize ports */
	sw->ports = kcalloc(sw->config.max_port_number + 1, sizeof(*sw->ports),
				GFP_KERNEL);
	if (!sw->ports)
		goto err;

	for (i = 0; i <= sw->config.max_port_number; i++) {
		/* minimum setup for tb_find_cap and tb_drom_read to work */
		sw->ports[i].sw = sw;
		sw->ports[i].port = i;
	}

	cap = tb_find_cap(&sw->ports[0], TB_CFG_SWITCH, TB_CAP_PLUG_EVENTS);
	if (cap < 0) {
		tb_sw_warn(sw, "cannot find TB_CAP_PLUG_EVENTS aborting\n");
		goto err;
	}
	sw->cap_plug_events = cap;

	/* read drom */
	if (tb_drom_read(sw))
		tb_sw_warn(sw, "tb_eeprom_read_rom failed, continuing\n");
	tb_sw_info(sw, "uid: %#llx\n", sw->uid);

	for (i = 0; i <= sw->config.max_port_number; i++) {
		if (sw->ports[i].disabled) {
			tb_port_info(&sw->ports[i], "disabled by eeprom\n");
			continue;
		}
		if (tb_init_port(&sw->ports[i]))
			goto err;
	}

	/* TODO: I2C, IECS, link controller */

	if (tb_plug_events_active(sw, true))
		goto err;

	return sw;
err:
	kfree(sw->ports);
	kfree(sw->drom);
	kfree(sw);
	return NULL;
}

/**
 * tb_sw_set_unpplugged() - set is_unplugged on switch and downstream switches
 */
void tb_sw_set_unpplugged(struct tb_switch *sw)
{
	int i;
	if (sw == sw->tb->root_switch) {
		tb_sw_WARN(sw, "cannot unplug root switch\n");
		return;
	}
	if (sw->is_unplugged) {
		tb_sw_WARN(sw, "is_unplugged already set\n");
		return;
	}
	sw->is_unplugged = true;
	for (i = 0; i <= sw->config.max_port_number; i++) {
		if (!tb_is_upstream_port(&sw->ports[i]) && sw->ports[i].remote)
			tb_sw_set_unpplugged(sw->ports[i].remote->sw);
	}
}

int tb_switch_resume(struct tb_switch *sw)
{
	int i, err;
	u64 uid;
	tb_sw_info(sw, "resuming switch\n");

	err = tb_drom_read_uid_only(sw, &uid);
	if (err) {
		tb_sw_warn(sw, "uid read failed\n");
		return err;
	}
	if (sw->uid != uid) {
		tb_sw_info(sw,
			"changed while suspended (uid %#llx -> %#llx)\n",
			sw->uid, uid);
		return -ENODEV;
	}

	/* upload configuration */
	err = tb_sw_write(sw, 1 + (u32 *) &sw->config, TB_CFG_SWITCH, 1, 3);
	if (err)
		return err;

	err = tb_plug_events_active(sw, true);
	if (err)
		return err;

	/* check for surviving downstream switches */
	for (i = 1; i <= sw->config.max_port_number; i++) {
		struct tb_port *port = &sw->ports[i];
		if (tb_is_upstream_port(port))
			continue;
		if (!port->remote)
			continue;
		if (tb_wait_for_port(port, true) <= 0
			|| tb_switch_resume(port->remote->sw)) {
			tb_port_warn(port,
				     "lost during suspend, disconnecting\n");
			tb_sw_set_unpplugged(port->remote->sw);
		}
	}
	return 0;
}

void tb_switch_suspend(struct tb_switch *sw)
{
	int i, err;
	err = tb_plug_events_active(sw, false);
	if (err)
		return;

	for (i = 1; i <= sw->config.max_port_number; i++) {
		if (!tb_is_upstream_port(&sw->ports[i]) && sw->ports[i].remote)
			tb_switch_suspend(sw->ports[i].remote->sw);
	}
	/*
	 * TODO: invoke tb_cfg_prepare_to_sleep here? does not seem to have any
	 * effect?
	 */
}
