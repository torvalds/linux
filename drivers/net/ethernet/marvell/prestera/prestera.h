/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2019-2020 Marvell International Ltd. All rights reserved. */

#ifndef _PRESTERA_H_
#define _PRESTERA_H_

#include <linux/notifier.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <net/devlink.h>
#include <uapi/linux/if_ether.h>

#define PRESTERA_DRV_NAME	"prestera"

#define PRESTERA_DEFAULT_VID    1

struct prestera_fw_rev {
	u16 maj;
	u16 min;
	u16 sub;
};

struct prestera_port_stats {
	u64 good_octets_received;
	u64 bad_octets_received;
	u64 mac_trans_error;
	u64 broadcast_frames_received;
	u64 multicast_frames_received;
	u64 frames_64_octets;
	u64 frames_65_to_127_octets;
	u64 frames_128_to_255_octets;
	u64 frames_256_to_511_octets;
	u64 frames_512_to_1023_octets;
	u64 frames_1024_to_max_octets;
	u64 excessive_collision;
	u64 multicast_frames_sent;
	u64 broadcast_frames_sent;
	u64 fc_sent;
	u64 fc_received;
	u64 buffer_overrun;
	u64 undersize;
	u64 fragments;
	u64 oversize;
	u64 jabber;
	u64 rx_error_frame_received;
	u64 bad_crc;
	u64 collisions;
	u64 late_collision;
	u64 unicast_frames_received;
	u64 unicast_frames_sent;
	u64 sent_multiple;
	u64 sent_deferred;
	u64 good_octets_sent;
};

#define PRESTERA_AP_PORT_MAX   (10)

struct prestera_port_caps {
	u64 supp_link_modes;
	u8 supp_fec;
	u8 type;
	u8 transceiver;
};

struct prestera_lag {
	struct net_device *dev;
	struct list_head members;
	u16 member_count;
	u16 lag_id;
};

struct prestera_flow_block;

struct prestera_port_mac_state {
	u32 mode;
	u32 speed;
	bool oper;
	u8 duplex;
	u8 fc;
	u8 fec;
};

struct prestera_port_phy_state {
	u64 lmode_bmap;
	struct {
		bool pause;
		bool asym_pause;
	} remote_fc;
	u8 mdix;
};

struct prestera_port_mac_config {
	u32 mode;
	u32 speed;
	bool admin;
	u8 inband;
	u8 duplex;
	u8 fec;
};

struct prestera_port_phy_config {
	u32 mode;
	bool admin;
	u8 mdix;
};

struct prestera_port {
	struct net_device *dev;
	struct prestera_switch *sw;
	struct prestera_flow_block *flow_block;
	struct devlink_port dl_port;
	struct list_head lag_member;
	struct prestera_lag *lag;
	u32 id;
	u32 hw_id;
	u32 dev_id;
	u16 fp_id;
	u16 pvid;
	bool autoneg;
	u64 adver_link_modes;
	u8 adver_fec;
	struct prestera_port_caps caps;
	struct list_head list;
	struct list_head vlans_list;
	struct {
		struct prestera_port_stats stats;
		struct delayed_work caching_dw;
	} cached_hw_stats;
	struct prestera_port_mac_config cfg_mac;
	struct prestera_port_phy_config cfg_phy;
	struct prestera_port_mac_state state_mac;
	struct prestera_port_phy_state state_phy;
};

struct prestera_device {
	struct device *dev;
	u8 __iomem *ctl_regs;
	u8 __iomem *pp_regs;
	struct prestera_fw_rev fw_rev;
	void *priv;

	/* called by device driver to handle received packets */
	void (*recv_pkt)(struct prestera_device *dev);

	/* called by device driver to pass event up to the higher layer */
	int (*recv_msg)(struct prestera_device *dev, void *msg, size_t size);

	/* called by higher layer to send request to the firmware */
	int (*send_req)(struct prestera_device *dev, int qid, void *in_msg,
			size_t in_size, void *out_msg, size_t out_size,
			unsigned int wait);
};

enum prestera_event_type {
	PRESTERA_EVENT_TYPE_UNSPEC,

	PRESTERA_EVENT_TYPE_PORT,
	PRESTERA_EVENT_TYPE_FDB,
	PRESTERA_EVENT_TYPE_RXTX,

	PRESTERA_EVENT_TYPE_MAX
};

enum prestera_rxtx_event_id {
	PRESTERA_RXTX_EVENT_UNSPEC,
	PRESTERA_RXTX_EVENT_RCV_PKT,
};

enum prestera_port_event_id {
	PRESTERA_PORT_EVENT_UNSPEC,
	PRESTERA_PORT_EVENT_MAC_STATE_CHANGED,
};

struct prestera_port_event {
	u32 port_id;
	union {
		struct {
			u32 mode;
			u32 speed;
			u8 oper;
			u8 duplex;
			u8 fc;
			u8 fec;
		} mac;
		struct {
			u64 lmode_bmap;
			struct {
				bool pause;
				bool asym_pause;
			} remote_fc;
			u8 mdix;
		} phy;
	} data;
};

enum prestera_fdb_entry_type {
	PRESTERA_FDB_ENTRY_TYPE_REG_PORT,
	PRESTERA_FDB_ENTRY_TYPE_LAG,
	PRESTERA_FDB_ENTRY_TYPE_MAX
};

enum prestera_fdb_event_id {
	PRESTERA_FDB_EVENT_UNSPEC,
	PRESTERA_FDB_EVENT_LEARNED,
	PRESTERA_FDB_EVENT_AGED,
};

struct prestera_fdb_event {
	enum prestera_fdb_entry_type type;
	union {
		u32 port_id;
		u16 lag_id;
	} dest;
	u32 vid;
	union {
		u8 mac[ETH_ALEN];
	} data;
};

struct prestera_event {
	u16 id;
	union {
		struct prestera_port_event port_evt;
		struct prestera_fdb_event fdb_evt;
	};
};

enum prestera_if_type {
	/* the interface is of port type (dev,port) */
	PRESTERA_IF_PORT_E = 0,

	/* the interface is of lag type (lag-id) */
	PRESTERA_IF_LAG_E = 1,

	/* the interface is of Vid type (vlan-id) */
	PRESTERA_IF_VID_E = 3,
};

struct prestera_iface {
	enum prestera_if_type type;
	struct {
		u32 hw_dev_num;
		u32 port_num;
	} dev_port;
	u32 hw_dev_num;
	u16 vr_id;
	u16 lag_id;
	u16 vlan_id;
};

struct prestera_switchdev;
struct prestera_span;
struct prestera_rxtx;
struct prestera_trap_data;
struct prestera_acl;

struct prestera_switch {
	struct prestera_device *dev;
	struct prestera_switchdev *swdev;
	struct prestera_rxtx *rxtx;
	struct prestera_acl *acl;
	struct prestera_span *span;
	struct list_head event_handlers;
	struct notifier_block netdev_nb;
	struct prestera_trap_data *trap_data;
	char base_mac[ETH_ALEN];
	struct list_head port_list;
	rwlock_t port_list_lock;
	u32 port_count;
	u32 mtu_min;
	u32 mtu_max;
	u8 id;
	struct prestera_router *router;
	struct prestera_lag *lags;
	struct prestera_counter *counter;
	u8 lag_member_max;
	u8 lag_max;
};

struct prestera_router {
	struct prestera_switch *sw;
	struct list_head vr_list;
	struct list_head rif_entry_list;
	struct rhashtable fib_ht;
	struct notifier_block inetaddr_nb;
	struct notifier_block inetaddr_valid_nb;
};

struct prestera_rxtx_params {
	bool use_sdma;
	u32 map_addr;
};

#define prestera_dev(sw)		((sw)->dev->dev)

static inline void prestera_write(const struct prestera_switch *sw,
				  unsigned int reg, u32 val)
{
	writel(val, sw->dev->pp_regs + reg);
}

static inline u32 prestera_read(const struct prestera_switch *sw,
				unsigned int reg)
{
	return readl(sw->dev->pp_regs + reg);
}

int prestera_device_register(struct prestera_device *dev);
void prestera_device_unregister(struct prestera_device *dev);

struct prestera_port *prestera_port_find_by_hwid(struct prestera_switch *sw,
						 u32 dev_id, u32 hw_id);

int prestera_port_autoneg_set(struct prestera_port *port, u64 link_modes);

int prestera_router_init(struct prestera_switch *sw);
void prestera_router_fini(struct prestera_switch *sw);

struct prestera_port *prestera_find_port(struct prestera_switch *sw, u32 id);

int prestera_port_cfg_mac_read(struct prestera_port *port,
			       struct prestera_port_mac_config *cfg);

int prestera_port_cfg_mac_write(struct prestera_port *port,
				struct prestera_port_mac_config *cfg);

struct prestera_port *prestera_port_dev_lower_find(struct net_device *dev);

int prestera_port_pvid_set(struct prestera_port *port, u16 vid);

bool prestera_netdev_check(const struct net_device *dev);

int prestera_is_valid_mac_addr(struct prestera_port *port, const u8 *addr);

bool prestera_port_is_lag_member(const struct prestera_port *port);

struct prestera_lag *prestera_lag_by_id(struct prestera_switch *sw, u16 id);

u16 prestera_port_lag_id(const struct prestera_port *port);

#endif /* _PRESTERA_H_ */
