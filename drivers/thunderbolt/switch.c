/*
 * Thunderbolt Cactus Ridge driver - switch/port utility functions
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 */

#include <linux/delay.h>

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
 * tb_init_port() - initialize a port
 *
 * This is a helper method for tb_switch_alloc. Does not check or initialize
 * any downstream switches.
 *
 * Return: Returns 0 on success or an error code on failure.
 */
static int tb_init_port(struct tb_switch *sw, u8 port_nr)
{
	int res;
	struct tb_port *port = &sw->ports[port_nr];
	port->sw = sw;
	port->port = port_nr;
	port->remote = NULL;
	res = tb_port_read(port, &port->config, TB_CFG_PORT, 0, 8);
	if (res)
		return res;

	tb_dump_port(sw->tb, &port->config);

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

	tb_plug_events_active(sw, false);

	kfree(sw->ports);
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
		if (tb_init_port(sw, i))
			goto err;
		/* TODO: check if port is disabled (EEPROM) */
	}

	/* TODO: I2C, IECS, EEPROM, link controller */

	cap = tb_find_cap(&sw->ports[0], TB_CFG_SWITCH, TB_CAP_PLUG_EVENTS);
	if (cap < 0) {
		tb_sw_warn(sw, "cannot find TB_CAP_PLUG_EVENTS aborting\n");
		goto err;
	}
	sw->cap_plug_events = cap;

	if (tb_plug_events_active(sw, true))
		goto err;

	return sw;
err:
	kfree(sw->ports);
	kfree(sw);
	return NULL;
}

