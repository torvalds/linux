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
#include <linux/bitfield.h>

#include "tb_regs.h"
#include "ctl.h"
#include "dma_port.h"

/* Keep link controller awake during update */
#define QUIRK_FORCE_POWER_LINK_CONTROLLER		BIT(0)
/* Disable CLx if not supported */
#define QUIRK_NO_CLX					BIT(1)

/**
 * struct tb_nvm - Structure holding NVM information
 * @dev: Owner of the NVM
 * @major: Major version number of the active NVM portion
 * @minor: Minor version number of the active NVM portion
 * @id: Identifier used with both NVM portions
 * @active: Active portion NVMem device
 * @active_size: Size in bytes of the active NVM
 * @non_active: Non-active portion NVMem device
 * @buf: Buffer where the NVM image is stored before it is written to
 *	 the actual NVM flash device
 * @buf_data_start: Where the actual image starts after skipping
 *		    possible headers
 * @buf_data_size: Number of bytes actually consumed by the new NVM
 *		   image
 * @authenticating: The device is authenticating the new NVM
 * @flushed: The image has been flushed to the storage area
 * @vops: Router vendor specific NVM operations (optional)
 *
 * The user of this structure needs to handle serialization of possible
 * concurrent access.
 */
struct tb_nvm {
	struct device *dev;
	u32 major;
	u32 minor;
	int id;
	struct nvmem_device *active;
	size_t active_size;
	struct nvmem_device *non_active;
	void *buf;
	void *buf_data_start;
	size_t buf_data_size;
	bool authenticating;
	bool flushed;
	const struct tb_nvm_vendor_ops *vops;
};

enum tb_nvm_write_ops {
	WRITE_AND_AUTHENTICATE = 1,
	WRITE_ONLY = 2,
	AUTHENTICATE_ONLY = 3,
};

#define TB_SWITCH_KEY_SIZE		32
#define TB_SWITCH_MAX_DEPTH		6
#define USB4_SWITCH_MAX_DEPTH		5

/**
 * enum tb_switch_tmu_mode - TMU mode
 * @TB_SWITCH_TMU_MODE_OFF: TMU is off
 * @TB_SWITCH_TMU_MODE_LOWRES: Uni-directional, normal mode
 * @TB_SWITCH_TMU_MODE_HIFI_UNI: Uni-directional, HiFi mode
 * @TB_SWITCH_TMU_MODE_HIFI_BI: Bi-directional, HiFi mode
 * @TB_SWITCH_TMU_MODE_MEDRES_ENHANCED_UNI: Enhanced Uni-directional, MedRes mode
 *
 * Ordering is based on TMU accuracy level (highest last).
 */
enum tb_switch_tmu_mode {
	TB_SWITCH_TMU_MODE_OFF,
	TB_SWITCH_TMU_MODE_LOWRES,
	TB_SWITCH_TMU_MODE_HIFI_UNI,
	TB_SWITCH_TMU_MODE_HIFI_BI,
	TB_SWITCH_TMU_MODE_MEDRES_ENHANCED_UNI,
};

/**
 * struct tb_switch_tmu - Structure holding router TMU configuration
 * @cap: Offset to the TMU capability (%0 if not found)
 * @has_ucap: Does the switch support uni-directional mode
 * @mode: TMU mode related to the upstream router. Reflects the HW
 *	  setting. Don't care for host router.
 * @mode_request: TMU mode requested to set. Related to upstream router.
 *		   Don't care for host router.
 */
struct tb_switch_tmu {
	int cap;
	bool has_ucap;
	enum tb_switch_tmu_mode mode;
	enum tb_switch_tmu_mode mode_request;
};

/**
 * struct tb_switch - a thunderbolt switch
 * @dev: Device for the switch
 * @config: Switch configuration
 * @ports: Ports in this switch
 * @dma_port: If the switch has port supporting DMA configuration based
 *	      mailbox this will hold the pointer to that (%NULL
 *	      otherwise). If set it also means the switch has
 *	      upgradeable NVM.
 * @tmu: The switch TMU configuration
 * @tb: Pointer to the domain the switch belongs to
 * @uid: Unique ID of the switch
 * @uuid: UUID of the switch (or %NULL if not supported)
 * @vendor: Vendor ID of the switch
 * @device: Device ID of the switch
 * @vendor_name: Name of the vendor (or %NULL if not known)
 * @device_name: Name of the device (or %NULL if not known)
 * @link_speed: Speed of the link in Gb/s
 * @link_width: Width of the upstream facing link
 * @link_usb4: Upstream link is USB4
 * @generation: Switch Thunderbolt generation
 * @cap_plug_events: Offset to the plug events capability (%0 if not found)
 * @cap_vsec_tmu: Offset to the TMU vendor specific capability (%0 if not found)
 * @cap_lc: Offset to the link controller capability (%0 if not found)
 * @cap_lp: Offset to the low power (CLx for TBT) capability (%0 if not found)
 * @is_unplugged: The switch is going away
 * @drom: DROM of the switch (%NULL if not found)
 * @nvm: Pointer to the NVM if the switch has one (%NULL otherwise)
 * @no_nvm_upgrade: Prevent NVM upgrade of this switch
 * @safe_mode: The switch is in safe-mode
 * @boot: Whether the switch was already authorized on boot or not
 * @rpm: The switch supports runtime PM
 * @authorized: Whether the switch is authorized by user or policy
 * @security_level: Switch supported security level
 * @debugfs_dir: Pointer to the debugfs structure
 * @key: Contains the key used to challenge the device or %NULL if not
 *	 supported. Size of the key is %TB_SWITCH_KEY_SIZE.
 * @connection_id: Connection ID used with ICM messaging
 * @connection_key: Connection key used with ICM messaging
 * @link: Root switch link this switch is connected (ICM only)
 * @depth: Depth in the chain this switch is connected (ICM only)
 * @rpm_complete: Completion used to wait for runtime resume to
 *		  complete (ICM only)
 * @quirks: Quirks used for this Thunderbolt switch
 * @credit_allocation: Are the below buffer allocation parameters valid
 * @max_usb3_credits: Router preferred number of buffers for USB 3.x
 * @min_dp_aux_credits: Router preferred minimum number of buffers for DP AUX
 * @min_dp_main_credits: Router preferred minimum number of buffers for DP MAIN
 * @max_pcie_credits: Router preferred number of buffers for PCIe
 * @max_dma_credits: Router preferred number of buffers for DMA/P2P
 * @clx: CLx states on the upstream link of the router
 *
 * When the switch is being added or removed to the domain (other
 * switches) you need to have domain lock held.
 *
 * In USB4 terminology this structure represents a router.
 *
 * Note @link_width is not the same as whether link is bonded or not.
 * For Gen 4 links the link is also bonded when it is asymmetric. The
 * correct way to find out whether the link is bonded or not is to look
 * @bonded field of the upstream port.
 */
struct tb_switch {
	struct device dev;
	struct tb_regs_switch_header config;
	struct tb_port *ports;
	struct tb_dma_port *dma_port;
	struct tb_switch_tmu tmu;
	struct tb *tb;
	u64 uid;
	uuid_t *uuid;
	u16 vendor;
	u16 device;
	const char *vendor_name;
	const char *device_name;
	unsigned int link_speed;
	enum tb_link_width link_width;
	bool link_usb4;
	unsigned int generation;
	int cap_plug_events;
	int cap_vsec_tmu;
	int cap_lc;
	int cap_lp;
	bool is_unplugged;
	u8 *drom;
	struct tb_nvm *nvm;
	bool no_nvm_upgrade;
	bool safe_mode;
	bool boot;
	bool rpm;
	unsigned int authorized;
	enum tb_security_level security_level;
	struct dentry *debugfs_dir;
	u8 *key;
	u8 connection_id;
	u8 connection_key;
	u8 link;
	u8 depth;
	struct completion rpm_complete;
	unsigned long quirks;
	bool credit_allocation;
	unsigned int max_usb3_credits;
	unsigned int min_dp_aux_credits;
	unsigned int min_dp_main_credits;
	unsigned int max_pcie_credits;
	unsigned int max_dma_credits;
	unsigned int clx;
};

/**
 * struct tb_bandwidth_group - Bandwidth management group
 * @tb: Pointer to the domain the group belongs to
 * @index: Index of the group (aka Group_ID). Valid values %1-%7
 * @ports: DP IN adapters belonging to this group are linked here
 *
 * Any tunnel that requires isochronous bandwidth (that's DP for now) is
 * attached to a bandwidth group. All tunnels going through the same
 * USB4 links share the same group and can dynamically distribute the
 * bandwidth within the group.
 */
struct tb_bandwidth_group {
	struct tb *tb;
	int index;
	struct list_head ports;
};

/**
 * struct tb_port - a thunderbolt port, part of a tb_switch
 * @config: Cached port configuration read from registers
 * @sw: Switch the port belongs to
 * @remote: Remote port (%NULL if not connected)
 * @xdomain: Remote host (%NULL if not connected)
 * @cap_phy: Offset, zero if not found
 * @cap_tmu: Offset of the adapter specific TMU capability (%0 if not present)
 * @cap_adap: Offset of the adapter specific capability (%0 if not present)
 * @cap_usb4: Offset to the USB4 port capability (%0 if not present)
 * @usb4: Pointer to the USB4 port structure (only if @cap_usb4 is != %0)
 * @port: Port number on switch
 * @disabled: Disabled by eeprom or enabled but not implemented
 * @bonded: true if the port is bonded (two lanes combined as one)
 * @dual_link_port: If the switch is connected using two ports, points
 *		    to the other port.
 * @link_nr: Is this primary or secondary port on the dual_link.
 * @in_hopids: Currently allocated input HopIDs
 * @out_hopids: Currently allocated output HopIDs
 * @list: Used to link ports to DP resources list
 * @total_credits: Total number of buffers available for this port
 * @ctl_credits: Buffers reserved for control path
 * @dma_credits: Number of credits allocated for DMA tunneling for all
 *		 DMA paths through this port.
 * @group: Bandwidth allocation group the adapter is assigned to. Only
 *	   used for DP IN adapters for now.
 * @group_list: The adapter is linked to the group's list of ports through this
 * @max_bw: Maximum possible bandwidth through this adapter if set to
 *	    non-zero.
 *
 * In USB4 terminology this structure represents an adapter (protocol or
 * lane adapter).
 */
struct tb_port {
	struct tb_regs_port_header config;
	struct tb_switch *sw;
	struct tb_port *remote;
	struct tb_xdomain *xdomain;
	int cap_phy;
	int cap_tmu;
	int cap_adap;
	int cap_usb4;
	struct usb4_port *usb4;
	u8 port;
	bool disabled;
	bool bonded;
	struct tb_port *dual_link_port;
	u8 link_nr:1;
	struct ida in_hopids;
	struct ida out_hopids;
	struct list_head list;
	unsigned int total_credits;
	unsigned int ctl_credits;
	unsigned int dma_credits;
	struct tb_bandwidth_group *group;
	struct list_head group_list;
	unsigned int max_bw;
};

/**
 * struct usb4_port - USB4 port device
 * @dev: Device for the port
 * @port: Pointer to the lane 0 adapter
 * @can_offline: Does the port have necessary platform support to moved
 *		 it into offline mode and back
 * @offline: The port is currently in offline mode
 * @margining: Pointer to margining structure if enabled
 */
struct usb4_port {
	struct device dev;
	struct tb_port *port;
	bool can_offline;
	bool offline;
#ifdef CONFIG_USB4_DEBUGFS_MARGINING
	struct tb_margining *margining;
#endif
};

/**
 * tb_retimer: Thunderbolt retimer
 * @dev: Device for the retimer
 * @tb: Pointer to the domain the retimer belongs to
 * @index: Retimer index facing the router USB4 port
 * @vendor: Vendor ID of the retimer
 * @device: Device ID of the retimer
 * @port: Pointer to the lane 0 adapter
 * @nvm: Pointer to the NVM if the retimer has one (%NULL otherwise)
 * @no_nvm_upgrade: Prevent NVM upgrade of this retimer
 * @auth_status: Status of last NVM authentication
 */
struct tb_retimer {
	struct device dev;
	struct tb *tb;
	u8 index;
	u32 vendor;
	u32 device;
	struct tb_port *port;
	struct tb_nvm *nvm;
	bool no_nvm_upgrade;
	u32 auth_status;
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
 * @nfc_credits: Number of non-flow controlled buffers allocated for the
 *		 @in_port.
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
	unsigned int nfc_credits;
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
 * @alloc_hopid: Does this path consume port HopID
 *
 * A path consists of a number of hops (see &struct tb_path_hop). To
 * establish a PCIe tunnel two paths have to be created between the two
 * PCIe ports.
 */
struct tb_path {
	struct tb *tb;
	const char *name;
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
	bool alloc_hopid;
};

/* HopIDs 0-7 are reserved by the Thunderbolt protocol */
#define TB_PATH_MIN_HOPID	8
/*
 * Support paths from the farthest (depth 6) router to the host and back
 * to the same level (not necessarily to the same router).
 */
#define TB_PATH_MAX_HOPS	(7 * 2)

/* Possible wake types */
#define TB_WAKE_ON_CONNECT	BIT(0)
#define TB_WAKE_ON_DISCONNECT	BIT(1)
#define TB_WAKE_ON_USB4		BIT(2)
#define TB_WAKE_ON_USB3		BIT(3)
#define TB_WAKE_ON_PCIE		BIT(4)
#define TB_WAKE_ON_DP		BIT(5)

/* CL states */
#define TB_CL0S			BIT(0)
#define TB_CL1			BIT(1)
#define TB_CL2			BIT(2)

/**
 * struct tb_cm_ops - Connection manager specific operations vector
 * @driver_ready: Called right after control channel is started. Used by
 *		  ICM to send driver ready message to the firmware.
 * @start: Starts the domain
 * @stop: Stops the domain
 * @suspend_noirq: Connection manager specific suspend_noirq
 * @resume_noirq: Connection manager specific resume_noirq
 * @suspend: Connection manager specific suspend
 * @freeze_noirq: Connection manager specific freeze_noirq
 * @thaw_noirq: Connection manager specific thaw_noirq
 * @complete: Connection manager specific complete
 * @runtime_suspend: Connection manager specific runtime_suspend
 * @runtime_resume: Connection manager specific runtime_resume
 * @runtime_suspend_switch: Runtime suspend a switch
 * @runtime_resume_switch: Runtime resume a switch
 * @handle_event: Handle thunderbolt event
 * @get_boot_acl: Get boot ACL list
 * @set_boot_acl: Set boot ACL list
 * @disapprove_switch: Disapprove switch (disconnect PCIe tunnel)
 * @approve_switch: Approve switch
 * @add_switch_key: Add key to switch
 * @challenge_switch_key: Challenge switch using key
 * @disconnect_pcie_paths: Disconnects PCIe paths before NVM update
 * @approve_xdomain_paths: Approve (establish) XDomain DMA paths
 * @disconnect_xdomain_paths: Disconnect XDomain DMA paths
 * @usb4_switch_op: Optional proxy for USB4 router operations. If set
 *		    this will be called whenever USB4 router operation is
 *		    performed. If this returns %-EOPNOTSUPP then the
 *		    native USB4 router operation is called.
 * @usb4_switch_nvm_authenticate_status: Optional callback that the CM
 *					 implementation can be used to
 *					 return status of USB4 NVM_AUTH
 *					 router operation.
 */
struct tb_cm_ops {
	int (*driver_ready)(struct tb *tb);
	int (*start)(struct tb *tb);
	void (*stop)(struct tb *tb);
	int (*suspend_noirq)(struct tb *tb);
	int (*resume_noirq)(struct tb *tb);
	int (*suspend)(struct tb *tb);
	int (*freeze_noirq)(struct tb *tb);
	int (*thaw_noirq)(struct tb *tb);
	void (*complete)(struct tb *tb);
	int (*runtime_suspend)(struct tb *tb);
	int (*runtime_resume)(struct tb *tb);
	int (*runtime_suspend_switch)(struct tb_switch *sw);
	int (*runtime_resume_switch)(struct tb_switch *sw);
	void (*handle_event)(struct tb *tb, enum tb_cfg_pkg_type,
			     const void *buf, size_t size);
	int (*get_boot_acl)(struct tb *tb, uuid_t *uuids, size_t nuuids);
	int (*set_boot_acl)(struct tb *tb, const uuid_t *uuids, size_t nuuids);
	int (*disapprove_switch)(struct tb *tb, struct tb_switch *sw);
	int (*approve_switch)(struct tb *tb, struct tb_switch *sw);
	int (*add_switch_key)(struct tb *tb, struct tb_switch *sw);
	int (*challenge_switch_key)(struct tb *tb, struct tb_switch *sw,
				    const u8 *challenge, u8 *response);
	int (*disconnect_pcie_paths)(struct tb *tb);
	int (*approve_xdomain_paths)(struct tb *tb, struct tb_xdomain *xd,
				     int transmit_path, int transmit_ring,
				     int receive_path, int receive_ring);
	int (*disconnect_xdomain_paths)(struct tb *tb, struct tb_xdomain *xd,
					int transmit_path, int transmit_ring,
					int receive_path, int receive_ring);
	int (*usb4_switch_op)(struct tb_switch *sw, u16 opcode, u32 *metadata,
			      u8 *status, const void *tx_data, size_t tx_data_len,
			      void *rx_data, size_t rx_data_len);
	int (*usb4_switch_nvm_authenticate_status)(struct tb_switch *sw,
						   u32 *status);
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

static inline bool tb_port_is_nhi(const struct tb_port *port)
{
	return port && port->config.type == TB_TYPE_NHI;
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

static inline bool tb_port_is_usb3_down(const struct tb_port *port)
{
	return port && port->config.type == TB_TYPE_USB3_DOWN;
}

static inline bool tb_port_is_usb3_up(const struct tb_port *port)
{
	return port && port->config.type == TB_TYPE_USB3_UP;
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
		level(__port->sw->tb, "%llx:%u: " fmt,                  \
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
extern struct device_type tb_retimer_type;
extern struct device_type tb_switch_type;
extern struct device_type usb4_port_device_type;

int tb_domain_init(void);
void tb_domain_exit(void);
int tb_xdomain_init(void);
void tb_xdomain_exit(void);

struct tb *tb_domain_alloc(struct tb_nhi *nhi, int timeout_msec, size_t privsize);
int tb_domain_add(struct tb *tb);
void tb_domain_remove(struct tb *tb);
int tb_domain_suspend_noirq(struct tb *tb);
int tb_domain_resume_noirq(struct tb *tb);
int tb_domain_suspend(struct tb *tb);
int tb_domain_freeze_noirq(struct tb *tb);
int tb_domain_thaw_noirq(struct tb *tb);
void tb_domain_complete(struct tb *tb);
int tb_domain_runtime_suspend(struct tb *tb);
int tb_domain_runtime_resume(struct tb *tb);
int tb_domain_disapprove_switch(struct tb *tb, struct tb_switch *sw);
int tb_domain_approve_switch(struct tb *tb, struct tb_switch *sw);
int tb_domain_approve_switch_key(struct tb *tb, struct tb_switch *sw);
int tb_domain_challenge_switch_key(struct tb *tb, struct tb_switch *sw);
int tb_domain_disconnect_pcie_paths(struct tb *tb);
int tb_domain_approve_xdomain_paths(struct tb *tb, struct tb_xdomain *xd,
				    int transmit_path, int transmit_ring,
				    int receive_path, int receive_ring);
int tb_domain_disconnect_xdomain_paths(struct tb *tb, struct tb_xdomain *xd,
				       int transmit_path, int transmit_ring,
				       int receive_path, int receive_ring);
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

struct tb_nvm *tb_nvm_alloc(struct device *dev);
int tb_nvm_read_version(struct tb_nvm *nvm);
int tb_nvm_validate(struct tb_nvm *nvm);
int tb_nvm_write_headers(struct tb_nvm *nvm);
int tb_nvm_add_active(struct tb_nvm *nvm, nvmem_reg_read_t reg_read);
int tb_nvm_write_buf(struct tb_nvm *nvm, unsigned int offset, void *val,
		     size_t bytes);
int tb_nvm_add_non_active(struct tb_nvm *nvm, nvmem_reg_write_t reg_write);
void tb_nvm_free(struct tb_nvm *nvm);
void tb_nvm_exit(void);

typedef int (*read_block_fn)(void *, unsigned int, void *, size_t);
typedef int (*write_block_fn)(void *, unsigned int, const void *, size_t);

int tb_nvm_read_data(unsigned int address, void *buf, size_t size,
		     unsigned int retries, read_block_fn read_block,
		     void *read_block_data);
int tb_nvm_write_data(unsigned int address, const void *buf, size_t size,
		      unsigned int retries, write_block_fn write_next_block,
		      void *write_block_data);

int tb_switch_nvm_read(struct tb_switch *sw, unsigned int address, void *buf,
		       size_t size);
struct tb_switch *tb_switch_alloc(struct tb *tb, struct device *parent,
				  u64 route);
struct tb_switch *tb_switch_alloc_safe_mode(struct tb *tb,
			struct device *parent, u64 route);
int tb_switch_configure(struct tb_switch *sw);
int tb_switch_configuration_valid(struct tb_switch *sw);
int tb_switch_add(struct tb_switch *sw);
void tb_switch_remove(struct tb_switch *sw);
void tb_switch_suspend(struct tb_switch *sw, bool runtime);
int tb_switch_resume(struct tb_switch *sw);
int tb_switch_reset(struct tb_switch *sw);
int tb_switch_wait_for_bit(struct tb_switch *sw, u32 offset, u32 bit,
			   u32 value, int timeout_msec);
void tb_sw_set_unplugged(struct tb_switch *sw);
struct tb_port *tb_switch_find_port(struct tb_switch *sw,
				    enum tb_port_type type);
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

static inline struct tb_switch *tb_to_switch(const struct device *dev)
{
	if (tb_is_switch(dev))
		return container_of(dev, struct tb_switch, dev);
	return NULL;
}

static inline struct tb_switch *tb_switch_parent(struct tb_switch *sw)
{
	return tb_to_switch(sw->dev.parent);
}

/**
 * tb_switch_downstream_port() - Return downstream facing port of parent router
 * @sw: Device router pointer
 *
 * Only call for device routers. Returns the downstream facing port of
 * the parent router.
 */
static inline struct tb_port *tb_switch_downstream_port(struct tb_switch *sw)
{
	if (WARN_ON(!tb_route(sw)))
		return NULL;
	return tb_port_at(tb_route(sw), tb_switch_parent(sw));
}

static inline bool tb_switch_is_light_ridge(const struct tb_switch *sw)
{
	return sw->config.vendor_id == PCI_VENDOR_ID_INTEL &&
	       sw->config.device_id == PCI_DEVICE_ID_INTEL_LIGHT_RIDGE;
}

static inline bool tb_switch_is_eagle_ridge(const struct tb_switch *sw)
{
	return sw->config.vendor_id == PCI_VENDOR_ID_INTEL &&
	       sw->config.device_id == PCI_DEVICE_ID_INTEL_EAGLE_RIDGE;
}

static inline bool tb_switch_is_cactus_ridge(const struct tb_switch *sw)
{
	if (sw->config.vendor_id == PCI_VENDOR_ID_INTEL) {
		switch (sw->config.device_id) {
		case PCI_DEVICE_ID_INTEL_CACTUS_RIDGE_2C:
		case PCI_DEVICE_ID_INTEL_CACTUS_RIDGE_4C:
			return true;
		}
	}
	return false;
}

static inline bool tb_switch_is_falcon_ridge(const struct tb_switch *sw)
{
	if (sw->config.vendor_id == PCI_VENDOR_ID_INTEL) {
		switch (sw->config.device_id) {
		case PCI_DEVICE_ID_INTEL_FALCON_RIDGE_2C_BRIDGE:
		case PCI_DEVICE_ID_INTEL_FALCON_RIDGE_4C_BRIDGE:
			return true;
		}
	}
	return false;
}

static inline bool tb_switch_is_alpine_ridge(const struct tb_switch *sw)
{
	if (sw->config.vendor_id == PCI_VENDOR_ID_INTEL) {
		switch (sw->config.device_id) {
		case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_2C_BRIDGE:
		case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_4C_BRIDGE:
		case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_BRIDGE:
		case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_4C_BRIDGE:
		case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_2C_BRIDGE:
			return true;
		}
	}
	return false;
}

static inline bool tb_switch_is_titan_ridge(const struct tb_switch *sw)
{
	if (sw->config.vendor_id == PCI_VENDOR_ID_INTEL) {
		switch (sw->config.device_id) {
		case PCI_DEVICE_ID_INTEL_TITAN_RIDGE_2C_BRIDGE:
		case PCI_DEVICE_ID_INTEL_TITAN_RIDGE_4C_BRIDGE:
		case PCI_DEVICE_ID_INTEL_TITAN_RIDGE_DD_BRIDGE:
			return true;
		}
	}
	return false;
}

static inline bool tb_switch_is_tiger_lake(const struct tb_switch *sw)
{
	if (sw->config.vendor_id == PCI_VENDOR_ID_INTEL) {
		switch (sw->config.device_id) {
		case PCI_DEVICE_ID_INTEL_TGL_NHI0:
		case PCI_DEVICE_ID_INTEL_TGL_NHI1:
		case PCI_DEVICE_ID_INTEL_TGL_H_NHI0:
		case PCI_DEVICE_ID_INTEL_TGL_H_NHI1:
			return true;
		}
	}
	return false;
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
int tb_switch_configure_link(struct tb_switch *sw);
void tb_switch_unconfigure_link(struct tb_switch *sw);

bool tb_switch_query_dp_resource(struct tb_switch *sw, struct tb_port *in);
int tb_switch_alloc_dp_resource(struct tb_switch *sw, struct tb_port *in);
void tb_switch_dealloc_dp_resource(struct tb_switch *sw, struct tb_port *in);

int tb_switch_tmu_init(struct tb_switch *sw);
int tb_switch_tmu_post_time(struct tb_switch *sw);
int tb_switch_tmu_disable(struct tb_switch *sw);
int tb_switch_tmu_enable(struct tb_switch *sw);
int tb_switch_tmu_configure(struct tb_switch *sw, enum tb_switch_tmu_mode mode);

/**
 * tb_switch_tmu_is_configured() - Is given TMU mode configured
 * @sw: Router whose mode to check
 * @mode: Mode to check
 *
 * Checks if given router TMU mode is configured to @mode. Note the
 * router TMU might not be enabled to this mode.
 */
static inline bool tb_switch_tmu_is_configured(const struct tb_switch *sw,
					       enum tb_switch_tmu_mode mode)
{
	return sw->tmu.mode_request == mode;
}

/**
 * tb_switch_tmu_is_enabled() - Checks if the specified TMU mode is enabled
 * @sw: Router whose TMU mode to check
 *
 * Return true if hardware TMU configuration matches the requested
 * configuration (and is not %TB_SWITCH_TMU_MODE_OFF).
 */
static inline bool tb_switch_tmu_is_enabled(const struct tb_switch *sw)
{
	return sw->tmu.mode != TB_SWITCH_TMU_MODE_OFF &&
	       sw->tmu.mode == sw->tmu.mode_request;
}

bool tb_port_clx_is_enabled(struct tb_port *port, unsigned int clx);

int tb_switch_clx_init(struct tb_switch *sw);
int tb_switch_clx_enable(struct tb_switch *sw, unsigned int clx);
int tb_switch_clx_disable(struct tb_switch *sw);

/**
 * tb_switch_clx_is_enabled() - Checks if the CLx is enabled
 * @sw: Router to check for the CLx
 * @clx: The CLx states to check for
 *
 * Checks if the specified CLx is enabled on the router upstream link.
 * Returns true if any of the given states is enabled.
 *
 * Not applicable for a host router.
 */
static inline bool tb_switch_clx_is_enabled(const struct tb_switch *sw,
					    unsigned int clx)
{
	return sw->clx & clx;
}

int tb_switch_pcie_l1_enable(struct tb_switch *sw);

int tb_switch_xhci_connect(struct tb_switch *sw);
void tb_switch_xhci_disconnect(struct tb_switch *sw);

int tb_port_state(struct tb_port *port);
int tb_wait_for_port(struct tb_port *port, bool wait_if_unplugged);
int tb_port_add_nfc_credits(struct tb_port *port, int credits);
int tb_port_clear_counter(struct tb_port *port, int counter);
int tb_port_unlock(struct tb_port *port);
int tb_port_enable(struct tb_port *port);
int tb_port_disable(struct tb_port *port);
int tb_port_alloc_in_hopid(struct tb_port *port, int hopid, int max_hopid);
void tb_port_release_in_hopid(struct tb_port *port, int hopid);
int tb_port_alloc_out_hopid(struct tb_port *port, int hopid, int max_hopid);
void tb_port_release_out_hopid(struct tb_port *port, int hopid);
struct tb_port *tb_next_port_on_path(struct tb_port *start, struct tb_port *end,
				     struct tb_port *prev);

static inline bool tb_port_use_credit_allocation(const struct tb_port *port)
{
	return tb_port_is_null(port) && port->sw->credit_allocation;
}

/**
 * tb_for_each_port_on_path() - Iterate over each port on path
 * @src: Source port
 * @dst: Destination port
 * @p: Port used as iterator
 *
 * Walks over each port on path from @src to @dst.
 */
#define tb_for_each_port_on_path(src, dst, p)				\
	for ((p) = tb_next_port_on_path((src), (dst), NULL); (p);	\
	     (p) = tb_next_port_on_path((src), (dst), (p)))

int tb_port_get_link_speed(struct tb_port *port);
int tb_port_get_link_width(struct tb_port *port);
int tb_port_set_link_width(struct tb_port *port, enum tb_link_width width);
int tb_port_lane_bonding_enable(struct tb_port *port);
void tb_port_lane_bonding_disable(struct tb_port *port);
int tb_port_wait_for_link_width(struct tb_port *port, unsigned int width_mask,
				int timeout_msec);
int tb_port_update_credits(struct tb_port *port);

int tb_switch_find_vse_cap(struct tb_switch *sw, enum tb_switch_vse_cap vsec);
int tb_switch_find_cap(struct tb_switch *sw, enum tb_switch_cap cap);
int tb_switch_next_cap(struct tb_switch *sw, unsigned int offset);
int tb_port_find_cap(struct tb_port *port, enum tb_port_cap cap);
int tb_port_next_cap(struct tb_port *port, unsigned int offset);
bool tb_port_is_enabled(struct tb_port *port);

bool tb_usb3_port_is_enabled(struct tb_port *port);
int tb_usb3_port_enable(struct tb_port *port, bool enable);

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
				 struct tb_port **last, const char *name,
				 bool alloc_hopid);
struct tb_path *tb_path_alloc(struct tb *tb, struct tb_port *src, int src_hopid,
			      struct tb_port *dst, int dst_hopid, int link_nr,
			      const char *name);
void tb_path_free(struct tb_path *path);
int tb_path_activate(struct tb_path *path);
void tb_path_deactivate(struct tb_path *path);
bool tb_path_is_invalid(struct tb_path *path);
bool tb_path_port_on_path(const struct tb_path *path,
			  const struct tb_port *port);

/**
 * tb_path_for_each_hop() - Iterate over each hop on path
 * @path: Path whose hops to iterate
 * @hop: Hop used as iterator
 *
 * Iterates over each hop on path.
 */
#define tb_path_for_each_hop(path, hop)					\
	for ((hop) = &(path)->hops[0];					\
	     (hop) <= &(path)->hops[(path)->path_length - 1]; (hop)++)

int tb_drom_read(struct tb_switch *sw);
int tb_drom_read_uid_only(struct tb_switch *sw, u64 *uid);

int tb_lc_read_uuid(struct tb_switch *sw, u32 *uuid);
int tb_lc_configure_port(struct tb_port *port);
void tb_lc_unconfigure_port(struct tb_port *port);
int tb_lc_configure_xdomain(struct tb_port *port);
void tb_lc_unconfigure_xdomain(struct tb_port *port);
int tb_lc_start_lane_initialization(struct tb_port *port);
bool tb_lc_is_clx_supported(struct tb_port *port);
bool tb_lc_is_usb_plugged(struct tb_port *port);
bool tb_lc_is_xhci_connected(struct tb_port *port);
int tb_lc_xhci_connect(struct tb_port *port);
void tb_lc_xhci_disconnect(struct tb_port *port);
int tb_lc_set_wake(struct tb_switch *sw, unsigned int flags);
int tb_lc_set_sleep(struct tb_switch *sw);
bool tb_lc_lane_bonding_possible(struct tb_switch *sw);
bool tb_lc_dp_sink_query(struct tb_switch *sw, struct tb_port *in);
int tb_lc_dp_sink_alloc(struct tb_switch *sw, struct tb_port *in);
int tb_lc_dp_sink_dealloc(struct tb_switch *sw, struct tb_port *in);
int tb_lc_force_power(struct tb_switch *sw);

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

bool tb_is_xdomain_enabled(void);
bool tb_xdomain_handle_request(struct tb *tb, enum tb_cfg_pkg_type type,
			       const void *buf, size_t size);
struct tb_xdomain *tb_xdomain_alloc(struct tb *tb, struct device *parent,
				    u64 route, const uuid_t *local_uuid,
				    const uuid_t *remote_uuid);
void tb_xdomain_add(struct tb_xdomain *xd);
void tb_xdomain_remove(struct tb_xdomain *xd);
struct tb_xdomain *tb_xdomain_find_by_link_depth(struct tb *tb, u8 link,
						 u8 depth);

static inline struct tb_switch *tb_xdomain_parent(struct tb_xdomain *xd)
{
	return tb_to_switch(xd->dev.parent);
}

/**
 * tb_xdomain_downstream_port() - Return downstream facing port of parent router
 * @xd: Xdomain pointer
 *
 * Returns the downstream port the XDomain is connected to.
 */
static inline struct tb_port *tb_xdomain_downstream_port(struct tb_xdomain *xd)
{
	return tb_port_at(xd->route, tb_xdomain_parent(xd));
}

int tb_retimer_nvm_read(struct tb_retimer *rt, unsigned int address, void *buf,
			size_t size);
int tb_retimer_scan(struct tb_port *port, bool add);
void tb_retimer_remove_all(struct tb_port *port);

static inline bool tb_is_retimer(const struct device *dev)
{
	return dev->type == &tb_retimer_type;
}

static inline struct tb_retimer *tb_to_retimer(struct device *dev)
{
	if (tb_is_retimer(dev))
		return container_of(dev, struct tb_retimer, dev);
	return NULL;
}

/**
 * usb4_switch_version() - Returns USB4 version of the router
 * @sw: Router to check
 *
 * Returns major version of USB4 router (%1 for v1, %2 for v2 and so
 * on). Can be called to pre-USB4 router too and in that case returns %0.
 */
static inline unsigned int usb4_switch_version(const struct tb_switch *sw)
{
	return FIELD_GET(USB4_VERSION_MAJOR_MASK, sw->config.thunderbolt_version);
}

/**
 * tb_switch_is_usb4() - Is the switch USB4 compliant
 * @sw: Switch to check
 *
 * Returns true if the @sw is USB4 compliant router, false otherwise.
 */
static inline bool tb_switch_is_usb4(const struct tb_switch *sw)
{
	return usb4_switch_version(sw) > 0;
}

int usb4_switch_setup(struct tb_switch *sw);
int usb4_switch_configuration_valid(struct tb_switch *sw);
int usb4_switch_read_uid(struct tb_switch *sw, u64 *uid);
int usb4_switch_drom_read(struct tb_switch *sw, unsigned int address, void *buf,
			  size_t size);
bool usb4_switch_lane_bonding_possible(struct tb_switch *sw);
int usb4_switch_set_wake(struct tb_switch *sw, unsigned int flags);
int usb4_switch_set_sleep(struct tb_switch *sw);
int usb4_switch_nvm_sector_size(struct tb_switch *sw);
int usb4_switch_nvm_read(struct tb_switch *sw, unsigned int address, void *buf,
			 size_t size);
int usb4_switch_nvm_set_offset(struct tb_switch *sw, unsigned int address);
int usb4_switch_nvm_write(struct tb_switch *sw, unsigned int address,
			  const void *buf, size_t size);
int usb4_switch_nvm_authenticate(struct tb_switch *sw);
int usb4_switch_nvm_authenticate_status(struct tb_switch *sw, u32 *status);
int usb4_switch_credits_init(struct tb_switch *sw);
bool usb4_switch_query_dp_resource(struct tb_switch *sw, struct tb_port *in);
int usb4_switch_alloc_dp_resource(struct tb_switch *sw, struct tb_port *in);
int usb4_switch_dealloc_dp_resource(struct tb_switch *sw, struct tb_port *in);
struct tb_port *usb4_switch_map_pcie_down(struct tb_switch *sw,
					  const struct tb_port *port);
struct tb_port *usb4_switch_map_usb3_down(struct tb_switch *sw,
					  const struct tb_port *port);
int usb4_switch_add_ports(struct tb_switch *sw);
void usb4_switch_remove_ports(struct tb_switch *sw);

int usb4_port_unlock(struct tb_port *port);
int usb4_port_hotplug_enable(struct tb_port *port);
int usb4_port_configure(struct tb_port *port);
void usb4_port_unconfigure(struct tb_port *port);
int usb4_port_configure_xdomain(struct tb_port *port, struct tb_xdomain *xd);
void usb4_port_unconfigure_xdomain(struct tb_port *port);
int usb4_port_router_offline(struct tb_port *port);
int usb4_port_router_online(struct tb_port *port);
int usb4_port_enumerate_retimers(struct tb_port *port);
bool usb4_port_clx_supported(struct tb_port *port);
int usb4_port_margining_caps(struct tb_port *port, u32 *caps);
int usb4_port_hw_margin(struct tb_port *port, unsigned int lanes,
			unsigned int ber_level, bool timing, bool right_high,
			u32 *results);
int usb4_port_sw_margin(struct tb_port *port, unsigned int lanes, bool timing,
			bool right_high, u32 counter);
int usb4_port_sw_margin_errors(struct tb_port *port, u32 *errors);

int usb4_port_retimer_set_inbound_sbtx(struct tb_port *port, u8 index);
int usb4_port_retimer_unset_inbound_sbtx(struct tb_port *port, u8 index);
int usb4_port_retimer_read(struct tb_port *port, u8 index, u8 reg, void *buf,
			   u8 size);
int usb4_port_retimer_write(struct tb_port *port, u8 index, u8 reg,
			    const void *buf, u8 size);
int usb4_port_retimer_is_last(struct tb_port *port, u8 index);
int usb4_port_retimer_nvm_sector_size(struct tb_port *port, u8 index);
int usb4_port_retimer_nvm_set_offset(struct tb_port *port, u8 index,
				     unsigned int address);
int usb4_port_retimer_nvm_write(struct tb_port *port, u8 index,
				unsigned int address, const void *buf,
				size_t size);
int usb4_port_retimer_nvm_authenticate(struct tb_port *port, u8 index);
int usb4_port_retimer_nvm_authenticate_status(struct tb_port *port, u8 index,
					      u32 *status);
int usb4_port_retimer_nvm_read(struct tb_port *port, u8 index,
			       unsigned int address, void *buf, size_t size);

int usb4_usb3_port_max_link_rate(struct tb_port *port);
int usb4_usb3_port_allocated_bandwidth(struct tb_port *port, int *upstream_bw,
				       int *downstream_bw);
int usb4_usb3_port_allocate_bandwidth(struct tb_port *port, int *upstream_bw,
				      int *downstream_bw);
int usb4_usb3_port_release_bandwidth(struct tb_port *port, int *upstream_bw,
				     int *downstream_bw);

int usb4_dp_port_set_cm_id(struct tb_port *port, int cm_id);
bool usb4_dp_port_bandwidth_mode_supported(struct tb_port *port);
bool usb4_dp_port_bandwidth_mode_enabled(struct tb_port *port);
int usb4_dp_port_set_cm_bandwidth_mode_supported(struct tb_port *port,
						 bool supported);
int usb4_dp_port_group_id(struct tb_port *port);
int usb4_dp_port_set_group_id(struct tb_port *port, int group_id);
int usb4_dp_port_nrd(struct tb_port *port, int *rate, int *lanes);
int usb4_dp_port_set_nrd(struct tb_port *port, int rate, int lanes);
int usb4_dp_port_granularity(struct tb_port *port);
int usb4_dp_port_set_granularity(struct tb_port *port, int granularity);
int usb4_dp_port_set_estimated_bandwidth(struct tb_port *port, int bw);
int usb4_dp_port_allocated_bandwidth(struct tb_port *port);
int usb4_dp_port_allocate_bandwidth(struct tb_port *port, int bw);
int usb4_dp_port_requested_bandwidth(struct tb_port *port);

int usb4_pci_port_set_ext_encapsulation(struct tb_port *port, bool enable);

static inline bool tb_is_usb4_port_device(const struct device *dev)
{
	return dev->type == &usb4_port_device_type;
}

static inline struct usb4_port *tb_to_usb4_port_device(struct device *dev)
{
	if (tb_is_usb4_port_device(dev))
		return container_of(dev, struct usb4_port, dev);
	return NULL;
}

struct usb4_port *usb4_port_device_add(struct tb_port *port);
void usb4_port_device_remove(struct usb4_port *usb4);
int usb4_port_device_resume(struct usb4_port *usb4);

static inline bool usb4_port_device_is_offline(const struct usb4_port *usb4)
{
	return usb4->offline;
}

void tb_check_quirks(struct tb_switch *sw);

#ifdef CONFIG_ACPI
bool tb_acpi_add_links(struct tb_nhi *nhi);

bool tb_acpi_is_native(void);
bool tb_acpi_may_tunnel_usb3(void);
bool tb_acpi_may_tunnel_dp(void);
bool tb_acpi_may_tunnel_pcie(void);
bool tb_acpi_is_xdomain_allowed(void);

int tb_acpi_init(void);
void tb_acpi_exit(void);
int tb_acpi_power_on_retimers(struct tb_port *port);
int tb_acpi_power_off_retimers(struct tb_port *port);
#else
static inline bool tb_acpi_add_links(struct tb_nhi *nhi) { return false; }

static inline bool tb_acpi_is_native(void) { return true; }
static inline bool tb_acpi_may_tunnel_usb3(void) { return true; }
static inline bool tb_acpi_may_tunnel_dp(void) { return true; }
static inline bool tb_acpi_may_tunnel_pcie(void) { return true; }
static inline bool tb_acpi_is_xdomain_allowed(void) { return true; }

static inline int tb_acpi_init(void) { return 0; }
static inline void tb_acpi_exit(void) { }
static inline int tb_acpi_power_on_retimers(struct tb_port *port) { return 0; }
static inline int tb_acpi_power_off_retimers(struct tb_port *port) { return 0; }
#endif

#ifdef CONFIG_DEBUG_FS
void tb_debugfs_init(void);
void tb_debugfs_exit(void);
void tb_switch_debugfs_init(struct tb_switch *sw);
void tb_switch_debugfs_remove(struct tb_switch *sw);
void tb_xdomain_debugfs_init(struct tb_xdomain *xd);
void tb_xdomain_debugfs_remove(struct tb_xdomain *xd);
void tb_service_debugfs_init(struct tb_service *svc);
void tb_service_debugfs_remove(struct tb_service *svc);
#else
static inline void tb_debugfs_init(void) { }
static inline void tb_debugfs_exit(void) { }
static inline void tb_switch_debugfs_init(struct tb_switch *sw) { }
static inline void tb_switch_debugfs_remove(struct tb_switch *sw) { }
static inline void tb_xdomain_debugfs_init(struct tb_xdomain *xd) { }
static inline void tb_xdomain_debugfs_remove(struct tb_xdomain *xd) { }
static inline void tb_service_debugfs_init(struct tb_service *svc) { }
static inline void tb_service_debugfs_remove(struct tb_service *svc) { }
#endif

#endif
