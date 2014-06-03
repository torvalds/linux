/*
 * Thunderbolt Cactus Ridge driver - bus logic (NHI independent)
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 */

#ifndef TB_H_
#define TB_H_

#include <linux/pci.h>

#include "tb_regs.h"
#include "ctl.h"

/**
 * struct tb_switch - a thunderbolt switch
 */
struct tb_switch {
	struct tb_regs_switch_header config;
	struct tb_port *ports;
	struct tb *tb;
	u64 uid;
	int cap_plug_events; /* offset, zero if not found */
	bool is_unplugged; /* unplugged, will go away */
};

/**
 * struct tb_port - a thunderbolt port, part of a tb_switch
 */
struct tb_port {
	struct tb_regs_port_header config;
	struct tb_switch *sw;
	struct tb_port *remote; /* remote port, NULL if not connected */
	int cap_phy; /* offset, zero if not found */
	u8 port; /* port number on switch */
};

/**
 * struct tb_path_hop - routing information for a tb_path
 *
 * Hop configuration is always done on the IN port of a switch.
 * in_port and out_port have to be on the same switch. Packets arriving on
 * in_port with "hop" = in_hop_index will get routed to through out_port. The
 * next hop to take (on out_port->remote) is determined by next_hop_index.
 *
 * in_counter_index is the index of a counter (in TB_CFG_COUNTERS) on the in
 * port.
 */
struct tb_path_hop {
	struct tb_port *in_port;
	struct tb_port *out_port;
	int in_hop_index;
	int in_counter_index; /* write -1 to disable counters for this hop. */
	int next_hop_index;
};

/**
 * enum tb_path_port - path options mask
 */
enum tb_path_port {
	TB_PATH_NONE = 0,
	TB_PATH_SOURCE = 1, /* activate on the first hop (out of src) */
	TB_PATH_INTERNAL = 2, /* activate on other hops (not the first/last) */
	TB_PATH_DESTINATION = 4, /* activate on the last hop (into dst) */
	TB_PATH_ALL = 7,
};

/**
 * struct tb_path - a unidirectional path between two ports
 *
 * A path consists of a number of hops (see tb_path_hop). To establish a PCIe
 * tunnel two paths have to be created between the two PCIe ports.
 *
 */
struct tb_path {
	struct tb *tb;
	int nfc_credits; /* non flow controlled credits */
	enum tb_path_port ingress_shared_buffer;
	enum tb_path_port egress_shared_buffer;
	enum tb_path_port ingress_fc_enable;
	enum tb_path_port egress_fc_enable;

	int priority:3;
	int weight:4;
	bool drop_packages;
	bool activated;
	struct tb_path_hop *hops;
	int path_length; /* number of hops */
};


/**
 * struct tb - main thunderbolt bus structure
 */
struct tb {
	struct mutex lock;	/*
				 * Big lock. Must be held when accessing cfg or
				 * any struct tb_switch / struct tb_port.
				 */
	struct tb_nhi *nhi;
	struct tb_ctl *ctl;
	struct workqueue_struct *wq; /* ordered workqueue for plug events */
	struct tb_switch *root_switch;
	struct list_head tunnel_list; /* list of active PCIe tunnels */
	bool hotplug_active; /*
			      * tb_handle_hotplug will stop progressing plug
			      * events and exit if this is not set (it needs to
			      * acquire the lock one more time). Used to drain
			      * wq after cfg has been paused.
			      */

};

/* helper functions & macros */

/**
 * tb_upstream_port() - return the upstream port of a switch
 *
 * Every switch has an upstream port (for the root switch it is the NHI).
 *
 * During switch alloc/init tb_upstream_port()->remote may be NULL, even for
 * non root switches (on the NHI port remote is always NULL).
 *
 * Return: Returns the upstream port of the switch.
 */
static inline struct tb_port *tb_upstream_port(struct tb_switch *sw)
{
	return &sw->ports[sw->config.upstream_port_number];
}

static inline u64 tb_route(struct tb_switch *sw)
{
	return ((u64) sw->config.route_hi) << 32 | sw->config.route_lo;
}

static inline int tb_sw_read(struct tb_switch *sw, void *buffer,
			     enum tb_cfg_space space, u32 offset, u32 length)
{
	return tb_cfg_read(sw->tb->ctl,
			   buffer,
			   tb_route(sw),
			   0,
			   space,
			   offset,
			   length);
}

static inline int tb_sw_write(struct tb_switch *sw, void *buffer,
			      enum tb_cfg_space space, u32 offset, u32 length)
{
	return tb_cfg_write(sw->tb->ctl,
			    buffer,
			    tb_route(sw),
			    0,
			    space,
			    offset,
			    length);
}

static inline int tb_port_read(struct tb_port *port, void *buffer,
			       enum tb_cfg_space space, u32 offset, u32 length)
{
	return tb_cfg_read(port->sw->tb->ctl,
			   buffer,
			   tb_route(port->sw),
			   port->port,
			   space,
			   offset,
			   length);
}

static inline int tb_port_write(struct tb_port *port, void *buffer,
				enum tb_cfg_space space, u32 offset, u32 length)
{
	return tb_cfg_write(port->sw->tb->ctl,
			    buffer,
			    tb_route(port->sw),
			    port->port,
			    space,
			    offset,
			    length);
}

#define tb_err(tb, fmt, arg...) dev_err(&(tb)->nhi->pdev->dev, fmt, ## arg)
#define tb_WARN(tb, fmt, arg...) dev_WARN(&(tb)->nhi->pdev->dev, fmt, ## arg)
#define tb_warn(tb, fmt, arg...) dev_warn(&(tb)->nhi->pdev->dev, fmt, ## arg)
#define tb_info(tb, fmt, arg...) dev_info(&(tb)->nhi->pdev->dev, fmt, ## arg)


#define __TB_SW_PRINT(level, sw, fmt, arg...)           \
	do {                                            \
		struct tb_switch *__sw = (sw);          \
		level(__sw->tb, "%llx: " fmt,           \
		      tb_route(__sw), ## arg);          \
	} while (0)
#define tb_sw_WARN(sw, fmt, arg...) __TB_SW_PRINT(tb_WARN, sw, fmt, ##arg)
#define tb_sw_warn(sw, fmt, arg...) __TB_SW_PRINT(tb_warn, sw, fmt, ##arg)
#define tb_sw_info(sw, fmt, arg...) __TB_SW_PRINT(tb_info, sw, fmt, ##arg)


#define __TB_PORT_PRINT(level, _port, fmt, arg...)                      \
	do {                                                            \
		struct tb_port *__port = (_port);                       \
		level(__port->sw->tb, "%llx:%x: " fmt,                  \
		      tb_route(__port->sw), __port->port, ## arg);      \
	} while (0)
#define tb_port_WARN(port, fmt, arg...) \
	__TB_PORT_PRINT(tb_WARN, port, fmt, ##arg)
#define tb_port_warn(port, fmt, arg...) \
	__TB_PORT_PRINT(tb_warn, port, fmt, ##arg)
#define tb_port_info(port, fmt, arg...) \
	__TB_PORT_PRINT(tb_info, port, fmt, ##arg)


struct tb *thunderbolt_alloc_and_start(struct tb_nhi *nhi);
void thunderbolt_shutdown_and_free(struct tb *tb);

struct tb_switch *tb_switch_alloc(struct tb *tb, u64 route);
void tb_switch_free(struct tb_switch *sw);
void tb_sw_set_unpplugged(struct tb_switch *sw);
struct tb_switch *get_switch_at_route(struct tb_switch *sw, u64 route);

int tb_wait_for_port(struct tb_port *port, bool wait_if_unplugged);
int tb_port_add_nfc_credits(struct tb_port *port, int credits);
int tb_port_clear_counter(struct tb_port *port, int counter);

int tb_find_cap(struct tb_port *port, enum tb_cfg_space space, u32 value);

struct tb_path *tb_path_alloc(struct tb *tb, int num_hops);
void tb_path_free(struct tb_path *path);
int tb_path_activate(struct tb_path *path);
void tb_path_deactivate(struct tb_path *path);
bool tb_path_is_invalid(struct tb_path *path);

int tb_eeprom_read_uid(struct tb_switch *sw, u64 *uid);


static inline int tb_route_length(u64 route)
{
	return (fls64(route) + TB_ROUTE_SHIFT - 1) / TB_ROUTE_SHIFT;
}

static inline bool tb_is_upstream_port(struct tb_port *port)
{
	return port == tb_upstream_port(port->sw);
}

/**
 * tb_downstream_route() - get route to downstream switch
 *
 * Port must not be the upstream port (otherwise a loop is created).
 *
 * Return: Returns a route to the switch behind @port.
 */
static inline u64 tb_downstream_route(struct tb_port *port)
{
	return tb_route(port->sw)
	       | ((u64) port->port << (port->sw->config.depth * 8));
}

#endif
