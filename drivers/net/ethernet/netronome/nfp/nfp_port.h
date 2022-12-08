/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#ifndef _NFP_PORT_H_
#define _NFP_PORT_H_

#include <net/devlink.h>

struct net_device;
struct netdev_phys_item_id;
struct nfp_app;
struct nfp_pf;
struct nfp_port;

/**
 * enum nfp_port_type - type of port NFP can switch traffic to
 * @NFP_PORT_INVALID:	port is invalid, %NFP_PORT_PHYS_PORT transitions to this
 *			state when port disappears because of FW fault or config
 *			change
 * @NFP_PORT_PHYS_PORT:	external NIC port
 * @NFP_PORT_PF_PORT:	logical port of PCI PF
 * @NFP_PORT_VF_PORT:	logical port of PCI VF
 */
enum nfp_port_type {
	NFP_PORT_INVALID,
	NFP_PORT_PHYS_PORT,
	NFP_PORT_PF_PORT,
	NFP_PORT_VF_PORT,
};

/**
 * enum nfp_port_flags - port flags (can be type-specific)
 * @NFP_PORT_CHANGED:	port state has changed since last eth table refresh;
 *			for NFP_PORT_PHYS_PORT, never set otherwise; must hold
 *			rtnl_lock to clear
 */
enum nfp_port_flags {
	NFP_PORT_CHANGED = 0,
};

/**
 * struct nfp_port - structure representing NFP port
 * @netdev:	backpointer to associated netdev
 * @type:	what port type does the entity represent
 * @flags:	port flags
 * @tc_offload_cnt:	number of active TC offloads, how offloads are counted
 *			is not defined, use as a boolean
 * @app:	backpointer to the app structure
 * @link_cb:	callback when link status changed
 * @dl_port:	devlink port structure
 * @eth_id:	for %NFP_PORT_PHYS_PORT port ID in NFP enumeration scheme
 * @eth_forced:	for %NFP_PORT_PHYS_PORT port is forced UP or DOWN, don't change
 * @eth_port:	for %NFP_PORT_PHYS_PORT translated ETH Table port entry
 * @eth_stats:	for %NFP_PORT_PHYS_PORT MAC stats if available
 * @pf_id:	for %NFP_PORT_PF_PORT, %NFP_PORT_VF_PORT ID of the PCI PF (0-3)
 * @vf_id:	for %NFP_PORT_VF_PORT ID of the PCI VF within @pf_id
 * @pf_split:	for %NFP_PORT_PF_PORT %true if PCI PF has more than one vNIC
 * @pf_split_id:for %NFP_PORT_PF_PORT ID of PCI PF vNIC (valid if @pf_split)
 * @vnic:	for %NFP_PORT_PF_PORT, %NFP_PORT_VF_PORT vNIC ctrl memory
 * @port_list:	entry on pf's list of ports
 */
struct nfp_port {
	struct net_device *netdev;
	enum nfp_port_type type;

	unsigned long flags;
	unsigned long tc_offload_cnt;

	struct nfp_app *app;
	void (*link_cb)(struct nfp_port *port);

	struct devlink_port dl_port;

	union {
		/* NFP_PORT_PHYS_PORT */
		struct {
			unsigned int eth_id;
			bool eth_forced;
			struct nfp_eth_table_port *eth_port;
			u8 __iomem *eth_stats;
		};
		/* NFP_PORT_PF_PORT, NFP_PORT_VF_PORT */
		struct {
			unsigned int pf_id;
			unsigned int vf_id;
			bool pf_split;
			unsigned int pf_split_id;
			u8 __iomem *vnic;
		};
	};

	struct list_head port_list;
};

extern const struct ethtool_ops nfp_port_ethtool_ops;

int nfp_port_setup_tc(struct net_device *netdev, enum tc_setup_type type,
		      void *type_data);

static inline bool nfp_port_is_vnic(const struct nfp_port *port)
{
	return port->type == NFP_PORT_PF_PORT || port->type == NFP_PORT_VF_PORT;
}

int
nfp_port_set_features(struct net_device *netdev, netdev_features_t features);

struct nfp_port *nfp_port_from_netdev(struct net_device *netdev);
int nfp_port_get_port_parent_id(struct net_device *netdev,
				struct netdev_phys_item_id *ppid);
struct nfp_eth_table_port *__nfp_port_get_eth_port(struct nfp_port *port);
struct nfp_eth_table_port *nfp_port_get_eth_port(struct nfp_port *port);

int
nfp_port_get_phys_port_name(struct net_device *netdev, char *name, size_t len);
int nfp_port_configure(struct net_device *netdev, bool configed);

struct nfp_port *
nfp_port_alloc(struct nfp_app *app, enum nfp_port_type type,
	       struct net_device *netdev);
void nfp_port_free(struct nfp_port *port);

int nfp_port_init_phy_port(struct nfp_pf *pf, struct nfp_app *app,
			   struct nfp_port *port, unsigned int id);

int nfp_net_refresh_eth_port(struct nfp_port *port);
void nfp_net_refresh_port_table(struct nfp_port *port);
int nfp_net_refresh_port_table_sync(struct nfp_pf *pf);

int nfp_devlink_port_register(struct nfp_app *app, struct nfp_port *port);
void nfp_devlink_port_unregister(struct nfp_port *port);
void nfp_devlink_port_type_eth_set(struct nfp_port *port);
void nfp_devlink_port_type_clear(struct nfp_port *port);

/* Mac stats (0x0000 - 0x0200)
 * all counters are 64bit.
 */
#define NFP_MAC_STATS_BASE                0x0000
#define NFP_MAC_STATS_SIZE                0x0200

#define NFP_MAC_STATS_RX_IN_OCTETS			(NFP_MAC_STATS_BASE + 0x000)
							/* unused 0x008 */
#define NFP_MAC_STATS_RX_FRAME_TOO_LONG_ERRORS		(NFP_MAC_STATS_BASE + 0x010)
#define NFP_MAC_STATS_RX_RANGE_LENGTH_ERRORS		(NFP_MAC_STATS_BASE + 0x018)
#define NFP_MAC_STATS_RX_VLAN_RECEIVED_OK		(NFP_MAC_STATS_BASE + 0x020)
#define NFP_MAC_STATS_RX_IN_ERRORS			(NFP_MAC_STATS_BASE + 0x028)
#define NFP_MAC_STATS_RX_IN_BROADCAST_PKTS		(NFP_MAC_STATS_BASE + 0x030)
#define NFP_MAC_STATS_RX_DROP_EVENTS			(NFP_MAC_STATS_BASE + 0x038)
#define NFP_MAC_STATS_RX_ALIGNMENT_ERRORS		(NFP_MAC_STATS_BASE + 0x040)
#define NFP_MAC_STATS_RX_PAUSE_MAC_CTRL_FRAMES		(NFP_MAC_STATS_BASE + 0x048)
#define NFP_MAC_STATS_RX_FRAMES_RECEIVED_OK		(NFP_MAC_STATS_BASE + 0x050)
#define NFP_MAC_STATS_RX_FRAME_CHECK_SEQUENCE_ERRORS	(NFP_MAC_STATS_BASE + 0x058)
#define NFP_MAC_STATS_RX_UNICAST_PKTS			(NFP_MAC_STATS_BASE + 0x060)
#define NFP_MAC_STATS_RX_MULTICAST_PKTS			(NFP_MAC_STATS_BASE + 0x068)
#define NFP_MAC_STATS_RX_PKTS				(NFP_MAC_STATS_BASE + 0x070)
#define NFP_MAC_STATS_RX_UNDERSIZE_PKTS			(NFP_MAC_STATS_BASE + 0x078)
#define NFP_MAC_STATS_RX_PKTS_64_OCTETS			(NFP_MAC_STATS_BASE + 0x080)
#define NFP_MAC_STATS_RX_PKTS_65_TO_127_OCTETS		(NFP_MAC_STATS_BASE + 0x088)
#define NFP_MAC_STATS_RX_PKTS_512_TO_1023_OCTETS	(NFP_MAC_STATS_BASE + 0x090)
#define NFP_MAC_STATS_RX_PKTS_1024_TO_1518_OCTETS	(NFP_MAC_STATS_BASE + 0x098)
#define NFP_MAC_STATS_RX_JABBERS			(NFP_MAC_STATS_BASE + 0x0a0)
#define NFP_MAC_STATS_RX_FRAGMENTS			(NFP_MAC_STATS_BASE + 0x0a8)
#define NFP_MAC_STATS_RX_PAUSE_FRAMES_CLASS2		(NFP_MAC_STATS_BASE + 0x0b0)
#define NFP_MAC_STATS_RX_PAUSE_FRAMES_CLASS3		(NFP_MAC_STATS_BASE + 0x0b8)
#define NFP_MAC_STATS_RX_PKTS_128_TO_255_OCTETS		(NFP_MAC_STATS_BASE + 0x0c0)
#define NFP_MAC_STATS_RX_PKTS_256_TO_511_OCTETS		(NFP_MAC_STATS_BASE + 0x0c8)
#define NFP_MAC_STATS_RX_PKTS_1519_TO_MAX_OCTETS	(NFP_MAC_STATS_BASE + 0x0d0)
#define NFP_MAC_STATS_RX_OVERSIZE_PKTS			(NFP_MAC_STATS_BASE + 0x0d8)
#define NFP_MAC_STATS_RX_PAUSE_FRAMES_CLASS0		(NFP_MAC_STATS_BASE + 0x0e0)
#define NFP_MAC_STATS_RX_PAUSE_FRAMES_CLASS1		(NFP_MAC_STATS_BASE + 0x0e8)
#define NFP_MAC_STATS_RX_PAUSE_FRAMES_CLASS4		(NFP_MAC_STATS_BASE + 0x0f0)
#define NFP_MAC_STATS_RX_PAUSE_FRAMES_CLASS5		(NFP_MAC_STATS_BASE + 0x0f8)
#define NFP_MAC_STATS_RX_PAUSE_FRAMES_CLASS6		(NFP_MAC_STATS_BASE + 0x100)
#define NFP_MAC_STATS_RX_PAUSE_FRAMES_CLASS7		(NFP_MAC_STATS_BASE + 0x108)
#define NFP_MAC_STATS_RX_MAC_CTRL_FRAMES_RECEIVED	(NFP_MAC_STATS_BASE + 0x110)
#define NFP_MAC_STATS_RX_MAC_HEAD_DROP			(NFP_MAC_STATS_BASE + 0x118)
							/* unused 0x120 */
							/* unused 0x128 */
							/* unused 0x130 */
#define NFP_MAC_STATS_TX_QUEUE_DROP			(NFP_MAC_STATS_BASE + 0x138)
#define NFP_MAC_STATS_TX_OUT_OCTETS			(NFP_MAC_STATS_BASE + 0x140)
							/* unused 0x148 */
#define NFP_MAC_STATS_TX_VLAN_TRANSMITTED_OK		(NFP_MAC_STATS_BASE + 0x150)
#define NFP_MAC_STATS_TX_OUT_ERRORS			(NFP_MAC_STATS_BASE + 0x158)
#define NFP_MAC_STATS_TX_BROADCAST_PKTS			(NFP_MAC_STATS_BASE + 0x160)
#define NFP_MAC_STATS_TX_PKTS_64_OCTETS			(NFP_MAC_STATS_BASE + 0x168)
#define NFP_MAC_STATS_TX_PKTS_256_TO_511_OCTETS		(NFP_MAC_STATS_BASE + 0x170)
#define NFP_MAC_STATS_TX_PKTS_512_TO_1023_OCTETS	(NFP_MAC_STATS_BASE + 0x178)
#define NFP_MAC_STATS_TX_PAUSE_MAC_CTRL_FRAMES		(NFP_MAC_STATS_BASE + 0x180)
#define NFP_MAC_STATS_TX_FRAMES_TRANSMITTED_OK		(NFP_MAC_STATS_BASE + 0x188)
#define NFP_MAC_STATS_TX_UNICAST_PKTS			(NFP_MAC_STATS_BASE + 0x190)
#define NFP_MAC_STATS_TX_MULTICAST_PKTS			(NFP_MAC_STATS_BASE + 0x198)
#define NFP_MAC_STATS_TX_PKTS_65_TO_127_OCTETS		(NFP_MAC_STATS_BASE + 0x1a0)
#define NFP_MAC_STATS_TX_PKTS_128_TO_255_OCTETS		(NFP_MAC_STATS_BASE + 0x1a8)
#define NFP_MAC_STATS_TX_PKTS_1024_TO_1518_OCTETS	(NFP_MAC_STATS_BASE + 0x1b0)
#define NFP_MAC_STATS_TX_PKTS_1519_TO_MAX_OCTETS	(NFP_MAC_STATS_BASE + 0x1b8)
#define NFP_MAC_STATS_TX_PAUSE_FRAMES_CLASS0		(NFP_MAC_STATS_BASE + 0x1c0)
#define NFP_MAC_STATS_TX_PAUSE_FRAMES_CLASS1		(NFP_MAC_STATS_BASE + 0x1c8)
#define NFP_MAC_STATS_TX_PAUSE_FRAMES_CLASS4		(NFP_MAC_STATS_BASE + 0x1d0)
#define NFP_MAC_STATS_TX_PAUSE_FRAMES_CLASS5		(NFP_MAC_STATS_BASE + 0x1d8)
#define NFP_MAC_STATS_TX_PAUSE_FRAMES_CLASS2		(NFP_MAC_STATS_BASE + 0x1e0)
#define NFP_MAC_STATS_TX_PAUSE_FRAMES_CLASS3		(NFP_MAC_STATS_BASE + 0x1e8)
#define NFP_MAC_STATS_TX_PAUSE_FRAMES_CLASS6		(NFP_MAC_STATS_BASE + 0x1f0)
#define NFP_MAC_STATS_TX_PAUSE_FRAMES_CLASS7		(NFP_MAC_STATS_BASE + 0x1f8)

#endif
