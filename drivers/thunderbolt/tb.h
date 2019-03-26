/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Thunderbolt driver - bus logic (NHI independent)
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2018, Intel Corporation
 */

#ifndef TB_H_
#define TB_H_

#include <linux/nvmem-provider.h>
#include <linux/pci.h>
#include <linux/thunderbolt.h>
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

#define TB_SWITCH_KEY_SIZE		32
#define TB_SWITCH_MAX_DEPTH		6

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
 * @link_speed: Speed of the link in Gb/s
 * @link_width: Width of the link (1 or 2)
 * @generation: Switch Thunderbolt generation
 * @cap_plug_events: Offset to the plug events capability (%0 if not found)
 * @cap_lc: Offset to the link controller capability (%0 if not found)
 * @is_unplugged: The switch is going away
 * @drom: DROM of the switch (%NULL if not found)
 * @nvm: Pointer to the NVM if the switch has one (%NULL otherwise)
 * @no_nvm_upgrade: Prevent NVM upgrade of this switch
 * @safe_mode: The switch is in safe-mode
 * @boot: Whether the switch was already authorized on boot or not
 * @rpm: The switch supports runtime PM
 * @authorized: Whether the switch is authorized by user or policy
 * @security_level: Switch supported security level
 * @key: Contains the key used to challenge the device or %NULL if not
 *	 supported. Size of the key is %TB_SWITCH_KEY_SIZE.
 * @connection_id: Connection ID used with ICM messaging
 * @connection_key: Connection key used with ICM messaging
 * @link: Root switch link this switch is connected (ICM only)
 * @depth: Depth in the chain this switch is connected (ICM only)
 * @rpm_complete: Completion used to wait for runtime resume to
 *		  complete (ICM only)
 *
 * When the switch is being added or removed to the domain (other
 * switches) you need to have domain lock held.
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
	unsigned int link_speed;
	unsigned int link_width;
	unsigned int generation;
	int cap_plug_events;
	int cap_lc;
	bool is_unplugged;
	u8 *drom;
	struct tb_switch_nvm *nvm;
	bool no_nvm_upgrade;
	bool safe_mode;
	bool boot;
	bool rpm;
	unsigned int authorized;
	enum tb_security_level security_level;
	u8 *key;
	u8 connection_id;
	u8 connection_key;
	u8 link;
	u8 depth;
	struct completion rpm_complete;
};

/**
 * struct tb_port - a thunderbolt port, part of a tb_switch
 * @config: Cached port configuration read from registers
 * @sw: Switch the port belongs to
 * @remote: Remote port (%NULL if not connected)
 * @xdomain: Remote host (%NULL if not connected)
 * @cap_phy: Offset, zero if not found
 * @cap_adap: Offset of the adapter specific capability (%0 if not present)
 * @port: Port number on switch
 * @disabled: Disabled by eeprom
 * @bonded: true if the port is bonded (two lanes combined as one)
 * @dual_link_port: If the switch is connected using two ports, points
 *		    to the other port.
 * @link_nr: Is this primary or secondary port on the dual_link.
 * @in_hopids: Currently allocated input HopIDs
 * @out_hopids: Currently allocated output HopIDs
 * @list: Used to link ports to DP resources list
 */
struct tb_port {
	struct tb_regs_port_header config;
	struct tb_switch *sw;
	struct tb_port *remote;
	struct tb_xdomain *xdomain;
	int cap_phy;
	int cap_adap;
	u8 port;
	bool disabled;
	bool bonded;
	struct tb_port *dual_link_port;
	u8 link_nr:1;
	struct ida in_hopids;
	struct ida out_hopids;
	struct list_head list;
};

/**
 * struct tb_path_hop - routing information for a tb_path
 * @in_port: Ingress port of a switch
 * @out_port: Egress port of a switch where the packet is routed out
 *	      (must be on the same switch than @in_port)
 * @in_hop_index: HopID where the path configuration entry is placed in
 *		  the path config space of @in_port.
 * @in_counter_index: Used counter index (not used in the driver
 *		      currently, %-1 to disable)
 * @next_hop_index: HopID of the packet when it is routed out from @out_port
 * @initial_credits: Number of initial flow control credits allocated for
 *		     the path
 *
 * Hop configuration is always done on the IN port of a switch.
 * in_port and out_port have to be on the same switch. Packets arriving on
 * in_port with "hop" = in_hop_index will get routed to through out_port. The
 * next hop to take (on out_port->remote) is determined by
 * next_hop_index. When routing packet to another switch (out->remote is
 * set) the @next_hop_index must match the @in_hop_index of that next
 * hop to make routing possible.
 *
 * in_counter_index is the index of a counter (in TB_CFG_COUNTERS) on the in
 * port.
 */
struct tb_path_hop {
	struct tb_port *in_port;
	struct tb_port *out_port;
	int in_hop_index;
	int in_counter_index;
	int next_hop_index;
	unsigned int initial_credits;
};

/**
 * enum tb_path_port - path options mask
 * @TB_PATH_NONE: Do not activate on any hop on path
 * @TB_PATH_SOURCE: Activate on the first hop (out of src)
 * @TB_PATH_INTERNAL: Activate on the intermediate hops (not the first/last)
 * @TB_PATH_DESTINATION: Activate on the last hop (into dst)
 * @TB_PATH_ALL: Activate on all hops on the path
 */
enum tb_path_port {
	TB_PATH_NONE = 0,
	TB_PATH_SOURCE = 1,
	TB_PATH_INTERNAL = 2,
	TB_PATH_DESTINATION = 4,
	TB_PATH_ALL = 7,
};

/**
 * struct tb_path - a unidirectional path between two ports
 * @tb: Pointer to the domain structure
 * @name: Name of the path (used for debugging)
 * @nfc_credits: Number of non flow controlled credits allocated for the path
 * @ingress_shared_buffer: Shared buffering used for ingress ports on the path
 * @egress_shared_buffer: Shared buffering used for egress ports on the path
 * @ingress_fc_enable: Flow control for ingress ports on the path
 * @egress_fc_enable: Flow control for egress ports on the path
 * @priority: Priority group if the path
 * @weight: Weight of the path inside the priority group
 * @drop_packages: Drop packages from queue tail or head
 * @activated: Is the path active
 * @clear_fc: Clear all flow control from the path config space entries
 *	      when deactivating this path
 * @hops: Path hops
 * @path_length: How many hops the path uses
 *
 * A path consists of a number of hops (see &struct tb_path_hop). To
 * establish a PCIe tunnel two paths have to be created between the two
 * PCIe ports.
 */
struct tb_path {
	struct tb *tb;
	const char *name;
	int nfc_credits;
	enum tb_path_port ingress_shared_buffer;
	enum tb_path_port egress_shared_buffer;
	enum tb_path_port ingress_fc_enable;
	enum tb_path_port egress_fc_enable;

	unsigned int priority:3;
	int weight:4;
	bool drop_packages;
	bool activated;
	bool clear_fc;
	struct tb_path_hop *hops;
	int path_length;
};

/* HopIDs 0-7 are reserved by the Thunderbolt protocol */
#define TB_PATH_MIN_HOPID	8
#define TB_PATH_MAX_HOPS	7

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
 * @runtime_suspend: Connection manager specific runtime_suspend
 * @runtime_resume: Connection manager specific runtime_resume
 * @runtime_suspend_switch: Runtime suspend a switch
 * @runtime_resume_switch: Runtime resume a switch
 * @handle_event: Handle thunderbolt event
 * @get_boot_acl: Get boot ACL list
 * @set_boot_acl: Set boot ACL list
 * @approve_switch: Approve switch
 * @add_switch_key: Add key to switch
 * @challenge_switch_key: Challenge switch using key
 * @disconnect_pcie_paths: Disconnects PCIe paths before NVM update
 * @approve_xdomain_paths: Approve (establish) XDomain DMA paths
 * @disconnect_xdomain_paths: Disconnect XDomain DMA paths
 */
struct tb_cm_ops {
	int (*driver_ready)(struct tb *tb);
	int (*start)(struct tb *tb);
	void (*stop)(struct tb *tb);
	int (*suspend_noirq)(struct tb *tb);
	int (*resume_noirq)(struct tb *tb);
	int (*suspend)(struct tb *tb);
	void (*complete)(struct tb *tb);
	int (*runtime_suspend)(struct tb *tb);
	int (*runtime_resume)(struct tb *tb);
	int (*runtime_suspend_switch)(struct tb_switch *sw);
	int (*runtime_resume_switch)(struct tb_switch *sw);
	void (*handle_event)(struct tb *tb, enum tb_cfg_pkg_type,
			     const void *buf, size_t size);
	int (*get_boot_acl)(struct tb *tb, uuid_t *uuids, size_t nuuids);
	int (*set_boot_acl)(struct tb *tb, const uuid_t *uuids, size_t nuuids);
	int (*approve_switch)(struct tb *tb, struct tb_switch *sw);
	int (*add_switch_key)(struct tb *tb, struct tb_switch *sw);
	int (*challenge_switch_key)(struct tb *tb, struct tb_switch *sw,
				    const u8 *challenge, u8 *response);
	int (*disconnect_pcie_paths)(struct tb *tb);
	int (*approve_xdomain_paths)(struct tb *tb, struct tb_xdomain *xd);
	int (*disconnect_xdomain_paths)(struct tb *tb, struct tb_xdomain *xd);
};

static inline void *tb_priv(struct tb *tb)
{
	return (void *)tb->privdata;
}

#define TB_AUTOSUSPEND_DELAY		15000 /* ms */

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

/**
 * tb_is_upstream_port() - Is the port upstream facing
 * @port: Port to check
 *
 * Returns true if @port is upstream facing port. In case of dual link
 * ports both return true.
 */
static inline bool tb_is_upstream_port(const struct tb_port *port)
{
	const struct tb_port *upstream_port = tb_upstream_port(port->sw);
	return port == upstream_port || port->dual_link_port == upstream_port;
}

static inline u64 tb_route(const struct tb_switch *sw)
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

/**
 * tb_port_has_remote() - Does the port have switch connected downstream
 * @port: Port to check
 *
 * Returns true only when the port is primary port and has remote set.
 */
static inline bool tb_port_has_remote(const struct tb_port *port)
{
	if (tb_is_upstream_port(port))
		return false;
	if (!port->remote)
		return false;
	if (port->dual_link_port && port->link_nr)
		return false;

	return true;
}

static inline bool tb_port_is_null(const struct tb_port *port)
{
	return port && port->port && port->config.type == TB_TYPE_PORT;
}

static inline bool tb_port_is_pcie_down(const struct tb_port *port)
{
	return port && port->config.type == TB_TYPE_PCIE_DOWN;
}

static inline bool tb_port_is_pcie_up(const struct tb_port *port)
{
	return port && port->config.type == TB_TYPE_PCIE_UP;
}

static inline bool tb_port_is_dpin(const struct tb_port *port)
{
	return port && port->config.type == TB_TYPE_DP_HDMI_IN;
}

static inline bool tb_port_is_dpout(const struct tb_port *port)
{
	return port && port->config.type == TB_TYPE_DP_HDMI_OUT;
}

static inline int tb_sw_read(struct tb_switch *sw, void *buffer,
			     enum tb_cfg_space space, u32 offset, u32 length)
{
	if (sw->is_unplugged)
		return -ENODEV;
	return tb_cfg_read(sw->tb->ctl,
			   buffer,
			   tb_route(sw),
			   0,
			   space,
			   offset,
			   length);
}

static inline int tb_sw_write(struct tb_switch *sw, const void *buffer,
			      enum tb_cfg_space space, u32 offset, u32 length)
{
	if (sw->is_unplugged)
		return -ENODEV;
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
	if (port->sw->is_unplugged)
		return -ENODEV;
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
	if (port->sw->is_unplugged)
		return -ENODEV;
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
#define tb_dbg(tb, fmt, arg...) dev_dbg(&(tb)->nhi->pdev->dev, fmt, ## arg)

#define __TB_SW_PRINT(level, sw, fmt, arg...)           \
	do {                                            \
		const struct tb_switch *__sw = (sw);    \
		level(__sw->tb, "%llx: " fmt,           \
		      tb_route(__sw), ## arg);          \
	} while (0)
#define tb_sw_WARN(sw, fmt, arg...) __TB_SW_PRINT(tb_WARN, sw, fmt, ##arg)
#define tb_sw_warn(sw, fmt, arg...) __TB_SW_PRINT(tb_warn, sw, fmt, ##arg)
#define tb_sw_info(sw, fmt, arg...) __TB_SW_PRINT(tb_info, sw, fmt, ##arg)
#define tb_sw_dbg(sw, fmt, arg...) __TB_SW_PRINT(tb_dbg, sw, fmt, ##arg)

#define __TB_PORT_PRINT(level, _port, fmt, arg...)                      \
	do {                                                            \
		const struct tb_port *__port = (_port);                 \
		level(__port->sw->tb, "%llx:%x: " fmt,                  \
		      tb_route(__port->sw), __port->port, ## arg);      \
	} while (0)
#define tb_port_WARN(port, fmt, arg...) \
	__TB_PORT_PRINT(tb_WARN, port, fmt, ##arg)
#define tb_port_warn(port, fmt, arg...) \
	__TB_PORT_PRINT(tb_warn, port, fmt, ##arg)
#define tb_port_info(port, fmt, arg...) \
	__TB_PORT_PRINT(tb_info, port, fmt, ##arg)
#define tb_port_dbg(port, fmt, arg...) \
	__TB_PORT_PRINT(tb_dbg, port, fmt, ##arg)

struct tb *icm_probe(struct tb_nhi *nhi);
struct tb *tb_probe(struct tb_nhi *nhi);

extern struct device_type tb_domain_type;
extern struct device_type tb_switch_type;

int tb_domain_init(void);
void tb_domain_exit(void);
void tb_switch_exit(void);
int tb_xdomain_init(void);
void tb_xdomain_exit(void);

struct tb *tb_domain_alloc(struct tb_nhi *nhi, size_t privsize);
int tb_domain_add(struct tb *tb);
void tb_domain_remove(struct tb *tb);
int tb_domain_suspend_noirq(struct tb *tb);
int tb_domain_resume_noirq(struct tb *tb);
int tb_domain_suspend(struct tb *tb);
void tb_domain_complete(struct tb *tb);
int tb_domain_runtime_suspend(struct tb *tb);
int tb_domain_runtime_resume(struct tb *tb);
int tb_domain_approve_switch(struct tb *tb, struct tb_switch *sw);
int tb_domain_approve_switch_key(struct tb *tb, struct tb_switch *sw);
int tb_domain_challenge_switch_key(struct tb *tb, struct tb_switch *sw);
int tb_domain_disconnect_pcie_paths(struct tb *tb);
int tb_domain_approve_xdomain_paths(struct tb *tb, struct tb_xdomain *xd);
int tb_domain_disconnect_xdomain_paths(struct tb *tb, struct tb_xdomain *xd);
int tb_domain_disconnect_all_paths(struct tb *tb);

static inline struct tb *tb_domain_get(struct tb *tb)
{
	if (tb)
		get_device(&tb->dev);
	return tb;
}

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
struct tb_switch *tb_switch_find_by_link_depth(struct tb *tb, u8 link,
					       u8 depth);
struct tb_switch *tb_switch_find_by_uuid(struct tb *tb, const uuid_t *uuid);
struct tb_switch *tb_switch_find_by_route(struct tb *tb, u64 route);

/**
 * tb_switch_for_each_port() - Iterate over each switch port
 * @sw: Switch whose ports to iterate
 * @p: Port used as iterator
 *
 * Iterates over each switch port skipping the control port (port %0).
 */
#define tb_switch_for_each_port(sw, p)					\
	for ((p) = &(sw)->ports[1];					\
	     (p) <= &(sw)->ports[(sw)->config.max_port_number]; (p)++)

static inline struct tb_switch *tb_switch_get(struct tb_switch *sw)
{
	if (sw)
		get_device(&sw->dev);
	return sw;
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

static inline struct tb_switch *tb_switch_parent(struct tb_switch *sw)
{
	return tb_to_switch(sw->dev.parent);
}

static inline bool tb_switch_is_light_ridge(const struct tb_switch *sw)
{
	return sw->config.device_id == PCI_DEVICE_ID_INTEL_LIGHT_RIDGE;
}

static inline bool tb_switch_is_eagle_ridge(const struct tb_switch *sw)
{
	return sw->config.device_id == PCI_DEVICE_ID_INTEL_EAGLE_RIDGE;
}

static inline bool tb_switch_is_cactus_ridge(const struct tb_switch *sw)
{
	switch (sw->config.device_id) {
	case PCI_DEVICE_ID_INTEL_CACTUS_RIDGE_2C:
	case PCI_DEVICE_ID_INTEL_CACTUS_RIDGE_4C:
		return true;
	default:
		return false;
	}
}

static inline bool tb_switch_is_falcon_ridge(const struct tb_switch *sw)
{
	switch (sw->config.device_id) {
	case PCI_DEVICE_ID_INTEL_FALCON_RIDGE_2C_BRIDGE:
	case PCI_DEVICE_ID_INTEL_FALCON_RIDGE_4C_BRIDGE:
		return true;
	default:
		return false;
	}
}

static inline bool tb_switch_is_alpine_ridge(const struct tb_switch *sw)
{
	switch (sw->config.device_id) {
	case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_2C_BRIDGE:
	case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_BRIDGE:
	case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_4C_BRIDGE:
	case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_2C_BRIDGE:
		return true;
	default:
		return false;
	}
}

static inline bool tb_switch_is_titan_ridge(const struct tb_switch *sw)
{
	switch (sw->config.device_id) {
	case PCI_DEVICE_ID_INTEL_TITAN_RIDGE_2C_BRIDGE:
	case PCI_DEVICE_ID_INTEL_TITAN_RIDGE_4C_BRIDGE:
	case PCI_DEVICE_ID_INTEL_TITAN_RIDGE_DD_BRIDGE:
		return true;
	default:
		return false;
	}
}

/**
 * tb_switch_is_icm() - Is the switch handled by ICM firmware
 * @sw: Switch to check
 *
 * In case there is a need to differentiate whether ICM firmware or SW CM
 * is handling @sw this function can be called. It is valid to call this
 * after tb_switch_alloc() and tb_switch_configure() has been called
 * (latter only for SW CM case).
 */
static inline bool tb_switch_is_icm(const struct tb_switch *sw)
{
	return !sw->config.enabled;
}

int tb_switch_lane_bonding_enable(struct tb_switch *sw);
void tb_switch_lane_bonding_disable(struct tb_switch *sw);

bool tb_switch_query_dp_resource(struct tb_switch *sw, struct tb_port *in);
int tb_switch_alloc_dp_resource(struct tb_switch *sw, struct tb_port *in);
void tb_switch_dealloc_dp_resource(struct tb_switch *sw, struct tb_port *in);

int tb_wait_for_port(struct tb_port *port, bool wait_if_unplugged);
int tb_port_add_nfc_credits(struct tb_port *port, int credits);
int tb_port_set_initial_credits(struct tb_port *port, u32 credits);
int tb_port_clear_counter(struct tb_port *port, int counter);
int tb_port_alloc_in_hopid(struct tb_port *port, int hopid, int max_hopid);
void tb_port_release_in_hopid(struct tb_port *port, int hopid);
int tb_port_alloc_out_hopid(struct tb_port *port, int hopid, int max_hopid);
void tb_port_release_out_hopid(struct tb_port *port, int hopid);
struct tb_port *tb_next_port_on_path(struct tb_port *start, struct tb_port *end,
				     struct tb_port *prev);

int tb_switch_find_vse_cap(struct tb_switch *sw, enum tb_switch_vse_cap vsec);
int tb_port_find_cap(struct tb_port *port, enum tb_port_cap cap);
bool tb_port_is_enabled(struct tb_port *port);

bool tb_pci_port_is_enabled(struct tb_port *port);
int tb_pci_port_enable(struct tb_port *port, bool enable);

int tb_dp_port_hpd_is_active(struct tb_port *port);
int tb_dp_port_hpd_clear(struct tb_port *port);
int tb_dp_port_set_hops(struct tb_port *port, unsigned int video,
			unsigned int aux_tx, unsigned int aux_rx);
bool tb_dp_port_is_enabled(struct tb_port *port);
int tb_dp_port_enable(struct tb_port *port, bool enable);

struct tb_path *tb_path_discover(struct tb_port *src, int src_hopid,
				 struct tb_port *dst, int dst_hopid,
				 struct tb_port **last, const char *name);
struct tb_path *tb_path_alloc(struct tb *tb, struct tb_port *src, int src_hopid,
			      struct tb_port *dst, int dst_hopid, int link_nr,
			      const char *name);
void tb_path_free(struct tb_path *path);
int tb_path_activate(struct tb_path *path);
void tb_path_deactivate(struct tb_path *path);
bool tb_path_is_invalid(struct tb_path *path);
bool tb_path_switch_on_path(const struct tb_path *path,
			    const struct tb_switch *sw);

int tb_drom_read(struct tb_switch *sw);
int tb_drom_read_uid_only(struct tb_switch *sw, u64 *uid);

int tb_lc_read_uuid(struct tb_switch *sw, u32 *uuid);
int tb_lc_configure_link(struct tb_switch *sw);
void tb_lc_unconfigure_link(struct tb_switch *sw);
int tb_lc_set_sleep(struct tb_switch *sw);
bool tb_lc_lane_bonding_possible(struct tb_switch *sw);
bool tb_lc_dp_sink_query(struct tb_switch *sw, struct tb_port *in);
int tb_lc_dp_sink_alloc(struct tb_switch *sw, struct tb_port *in);
int tb_lc_dp_sink_dealloc(struct tb_switch *sw, struct tb_port *in);

static inline int tb_route_length(u64 route)
{
	return (fls64(route) + TB_ROUTE_SHIFT - 1) / TB_ROUTE_SHIFT;
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

bool tb_xdomain_handle_request(struct tb *tb, enum tb_cfg_pkg_type type,
			       const void *buf, size_t size);
struct tb_xdomain *tb_xdomain_alloc(struct tb *tb, struct device *parent,
				    u64 route, const uuid_t *local_uuid,
				    const uuid_t *remote_uuid);
void tb_xdomain_add(struct tb_xdomain *xd);
void tb_xdomain_remove(struct tb_xdomain *xd);
struct tb_xdomain *tb_xdomain_find_by_link_depth(struct tb *tb, u8 link,
						 u8 depth);

#endif
