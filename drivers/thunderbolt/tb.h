/*
 * Thunderbolt Cactus Ridge driver - bus logic (NHI independent)
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 */

#ifndef TB_H_
#define TB_H_

#include <linux/nvmem-provider.h>
#include <linux/pci.h>
#include <linux/uuid.h>

#include "tb_regs.h"
#include "ctl.h"
#include "dma_port.h"

/**
 * struct tb_switch_nvm - Structure holding switch NVM information
 * @major: Major version number of the active NVM portion
 * @minor: Minor version number of the active NVM portion
 * @id: Identifier used with both NVM portions
 * @active: Active portion NVMem device
 * @non_active: Non-active portion NVMem device
 * @buf: Buffer where the NVM image is stored before it is written to
 *	 the actual NVM flash device
 * @buf_data_size: Number of bytes actually consumed by the new NVM
 *		   image
 * @authenticating: The switch is authenticating the new NVM
 */
struct tb_switch_nvm {
	u8 major;
	u8 minor;
	int id;
	struct nvmem_device *active;
	struct nvmem_device *non_active;
	void *buf;
	size_t buf_data_size;
	bool authenticating;
};

/**
 * enum tb_security_level - Thunderbolt security level
 * @TB_SECURITY_NONE: No security, legacy mode
 * @TB_SECURITY_USER: User approval required at minimum
 * @TB_SECURITY_SECURE: One time saved key required at minimum
 * @TB_SECURITY_DPONLY: Only tunnel Display port (and USB)
 */
enum tb_security_level {
	TB_SECURITY_NONE,
	TB_SECURITY_USER,
	TB_SECURITY_SECURE,
	TB_SECURITY_DPONLY,
};

#define TB_SWITCH_KEY_SIZE		32
/* Each physical port contains 2 links on modern controllers */
#define TB_SWITCH_LINKS_PER_PHY_PORT	2

/**
 * struct tb_switch - a thunderbolt switch
 * @dev: Device for the switch
 * @config: Switch configuration
 * @ports: Ports in this switch
 * @dma_port: If the switch has port supporting DMA configuration based
 *	      mailbox this will hold the pointer to that (%NULL
 *	      otherwise). If set it also means the switch has
 *	      upgradeable NVM.
 * @tb: Pointer to the domain the switch belongs to
 * @uid: Unique ID of the switch
 * @uuid: UUID of the switch (or %NULL if not supported)
 * @vendor: Vendor ID of the switch
 * @device: Device ID of the switch
 * @vendor_name: Name of the vendor (or %NULL if not known)
 * @device_name: Name of the device (or %NULL if not known)
 * @generation: Switch Thunderbolt generation
 * @cap_plug_events: Offset to the plug events capability (%0 if not found)
 * @is_unplugged: The switch is going away
 * @drom: DROM of the switch (%NULL if not found)
 * @nvm: Pointer to the NVM if the switch has one (%NULL otherwise)
 * @no_nvm_upgrade: Prevent NVM upgrade of this switch
 * @safe_mode: The switch is in safe-mode
 * @authorized: Whether the switch is authorized by user or policy
 * @work: Work used to automatically authorize a switch
 * @security_level: Switch supported security level
 * @key: Contains the key used to challenge the device or %NULL if not
 *	 supported. Size of the key is %TB_SWITCH_KEY_SIZE.
 * @connection_id: Connection ID used with ICM messaging
 * @connection_key: Connection key used with ICM messaging
 * @link: Root switch link this switch is connected (ICM only)
 * @depth: Depth in the chain this switch is connected (ICM only)
 *
 * When the switch is being added or removed to the domain (other
 * switches) you need to have domain lock held. For switch authorization
 * internal switch_lock is enough.
 */
struct tb_switch {
	struct device dev;
	struct tb_regs_switch_header config;
	struct tb_port *ports;
	struct tb_dma_port *dma_port;
	struct tb *tb;
	u64 uid;
	uuid_t *uuid;
	u16 vendor;
	u16 device;
	const char *vendor_name;
	const char *device_name;
	unsigned int generation;
	int cap_plug_events;
	bool is_unplugged;
	u8 *drom;
	struct tb_switch_nvm *nvm;
	bool no_nvm_upgrade;
	bool safe_mode;
	unsigned int authorized;
	struct work_struct work;
	enum tb_security_level security_level;
	u8 *key;
	u8 connection_id;
	u8 connection_key;
	u8 link;
	u8 depth;
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
	bool disabled; /* disabled by eeprom */
	struct tb_port *dual_link_port;
	u8 link_nr:1;
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
 * struct tb_cm_ops - Connection manager specific operations vector
 * @driver_ready: Called right after control channel is started. Used by
 *		  ICM to send driver ready message to the firmware.
 * @start: Starts the domain
 * @stop: Stops the domain
 * @suspend_noirq: Connection manager specific suspend_noirq
 * @resume_noirq: Connection manager specific resume_noirq
 * @suspend: Connection manager specific suspend
 * @complete: Connection manager specific complete
 * @handle_event: Handle thunderbolt event
 * @approve_switch: Approve switch
 * @add_switch_key: Add key to switch
 * @challenge_switch_key: Challenge switch using key
 * @disconnect_pcie_paths: Disconnects PCIe paths before NVM update
 */
struct tb_cm_ops {
	int (*driver_ready)(struct tb *tb);
	int (*start)(struct tb *tb);
	void (*stop)(struct tb *tb);
	int (*suspend_noirq)(struct tb *tb);
	int (*resume_noirq)(struct tb *tb);
	int (*suspend)(struct tb *tb);
	void (*complete)(struct tb *tb);
	void (*handle_event)(struct tb *tb, enum tb_cfg_pkg_type,
			     const void *buf, size_t size);
	int (*approve_switch)(struct tb *tb, struct tb_switch *sw);
	int (*add_switch_key)(struct tb *tb, struct tb_switch *sw);
	int (*challenge_switch_key)(struct tb *tb, struct tb_switch *sw,
				    const u8 *challenge, u8 *response);
	int (*disconnect_pcie_paths)(struct tb *tb);
};

/**
 * struct tb - main thunderbolt bus structure
 * @dev: Domain device
 * @lock: Big lock. Must be held when accessing any struct
 *	  tb_switch / struct tb_port.
 * @nhi: Pointer to the NHI structure
 * @ctl: Control channel for this domain
 * @wq: Ordered workqueue for all domain specific work
 * @root_switch: Root switch of this domain
 * @cm_ops: Connection manager specific operations vector
 * @index: Linux assigned domain number
 * @security_level: Current security level
 * @privdata: Private connection manager specific data
 */
struct tb {
	struct device dev;
	struct mutex lock;
	struct tb_nhi *nhi;
	struct tb_ctl *ctl;
	struct workqueue_struct *wq;
	struct tb_switch *root_switch;
	const struct tb_cm_ops *cm_ops;
	int index;
	enum tb_security_level security_level;
	unsigned long privdata[0];
};

static inline void *tb_priv(struct tb *tb)
{
	return (void *)tb->privdata;
}

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

static inline struct tb_port *tb_port_at(u64 route, struct tb_switch *sw)
{
	u8 port;

	port = route >> (sw->config.depth * 8);
	if (WARN_ON(port > sw->config.max_port_number))
		return NULL;
	return &sw->ports[port];
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

static inline int tb_port_write(struct tb_port *port, const void *buffer,
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

struct tb *icm_probe(struct tb_nhi *nhi);
struct tb *tb_probe(struct tb_nhi *nhi);

extern struct bus_type tb_bus_type;
extern struct device_type tb_domain_type;
extern struct device_type tb_switch_type;

int tb_domain_init(void);
void tb_domain_exit(void);
void tb_switch_exit(void);

struct tb *tb_domain_alloc(struct tb_nhi *nhi, size_t privsize);
int tb_domain_add(struct tb *tb);
void tb_domain_remove(struct tb *tb);
int tb_domain_suspend_noirq(struct tb *tb);
int tb_domain_resume_noirq(struct tb *tb);
int tb_domain_suspend(struct tb *tb);
void tb_domain_complete(struct tb *tb);
int tb_domain_approve_switch(struct tb *tb, struct tb_switch *sw);
int tb_domain_approve_switch_key(struct tb *tb, struct tb_switch *sw);
int tb_domain_challenge_switch_key(struct tb *tb, struct tb_switch *sw);
int tb_domain_disconnect_pcie_paths(struct tb *tb);

static inline void tb_domain_put(struct tb *tb)
{
	put_device(&tb->dev);
}

struct tb_switch *tb_switch_alloc(struct tb *tb, struct device *parent,
				  u64 route);
struct tb_switch *tb_switch_alloc_safe_mode(struct tb *tb,
			struct device *parent, u64 route);
int tb_switch_configure(struct tb_switch *sw);
int tb_switch_add(struct tb_switch *sw);
void tb_switch_remove(struct tb_switch *sw);
void tb_switch_suspend(struct tb_switch *sw);
int tb_switch_resume(struct tb_switch *sw);
int tb_switch_reset(struct tb *tb, u64 route);
void tb_sw_set_unplugged(struct tb_switch *sw);
struct tb_switch *get_switch_at_route(struct tb_switch *sw, u64 route);
struct tb_switch *tb_switch_find_by_link_depth(struct tb *tb, u8 link,
					       u8 depth);
struct tb_switch *tb_switch_find_by_uuid(struct tb *tb, const uuid_t *uuid);

static inline unsigned int tb_switch_phy_port_from_link(unsigned int link)
{
	return (link - 1) / TB_SWITCH_LINKS_PER_PHY_PORT;
}

static inline void tb_switch_put(struct tb_switch *sw)
{
	put_device(&sw->dev);
}

static inline bool tb_is_switch(const struct device *dev)
{
	return dev->type == &tb_switch_type;
}

static inline struct tb_switch *tb_to_switch(struct device *dev)
{
	if (tb_is_switch(dev))
		return container_of(dev, struct tb_switch, dev);
	return NULL;
}

int tb_wait_for_port(struct tb_port *port, bool wait_if_unplugged);
int tb_port_add_nfc_credits(struct tb_port *port, int credits);
int tb_port_clear_counter(struct tb_port *port, int counter);

int tb_switch_find_vse_cap(struct tb_switch *sw, enum tb_switch_vse_cap vsec);
int tb_port_find_cap(struct tb_port *port, enum tb_port_cap cap);

struct tb_path *tb_path_alloc(struct tb *tb, int num_hops);
void tb_path_free(struct tb_path *path);
int tb_path_activate(struct tb_path *path);
void tb_path_deactivate(struct tb_path *path);
bool tb_path_is_invalid(struct tb_path *path);

int tb_drom_read(struct tb_switch *sw);
int tb_drom_read_uid_only(struct tb_switch *sw, u64 *uid);


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
