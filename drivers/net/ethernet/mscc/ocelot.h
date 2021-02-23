/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 */

#ifndef _MSCC_OCELOT_H_
#define _MSCC_OCELOT_H_

#include <linux/bitops.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/net_tstamp.h>
#include <linux/phy.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <soc/mscc/ocelot_qsys.h>
#include <soc/mscc/ocelot_sys.h>
#include <soc/mscc/ocelot_dev.h>
#include <soc/mscc/ocelot_ana.h>
#include <soc/mscc/ocelot_ptp.h>
#include <soc/mscc/ocelot.h>
#include "ocelot_rew.h"
#include "ocelot_qs.h"

#define OCELOT_BUFFER_CELL_SZ 60

#define OCELOT_STATS_CHECK_DELAY (2 * HZ)

#define OCELOT_PTP_QUEUE_SZ	128

struct frame_info {
	u32 len;
	u16 port;
	u16 vid;
	u8 tag_type;
	u16 rew_op;
	u32 timestamp;	/* rew_val */
};

struct ocelot_port_tc {
	bool block_shared;
	unsigned long offload_cnt;

	unsigned long police_id;
};

struct ocelot_port_private {
	struct ocelot_port port;
	struct net_device *dev;
	struct phy_device *phy;
	u8 chip_port;

	struct phy *serdes;

	struct ocelot_port_tc tc;
};

struct ocelot_dump_ctx {
	struct net_device *dev;
	struct sk_buff *skb;
	struct netlink_callback *cb;
	int idx;
};

/* MAC table entry types.
 * ENTRYTYPE_NORMAL is subject to aging.
 * ENTRYTYPE_LOCKED is not subject to aging.
 * ENTRYTYPE_MACv4 is not subject to aging. For IPv4 multicast.
 * ENTRYTYPE_MACv6 is not subject to aging. For IPv6 multicast.
 */
enum macaccess_entry_type {
	ENTRYTYPE_NORMAL = 0,
	ENTRYTYPE_LOCKED,
	ENTRYTYPE_MACv4,
	ENTRYTYPE_MACv6,
};

/* A (PGID) port mask structure, encoding the 2^ocelot->num_phys_ports
 * possibilities of egress port masks for L2 multicast traffic.
 * For a switch with 9 user ports, there are 512 possible port masks, but the
 * hardware only has 46 individual PGIDs that it can forward multicast traffic
 * to. So we need a structure that maps the limited PGID indices to the port
 * destinations requested by the user for L2 multicast.
 */
struct ocelot_pgid {
	unsigned long ports;
	int index;
	refcount_t refcount;
	struct list_head list;
};

struct ocelot_multicast {
	struct list_head list;
	enum macaccess_entry_type entry_type;
	unsigned char addr[ETH_ALEN];
	u16 vid;
	u16 ports;
	struct ocelot_pgid *pgid;
};

int ocelot_port_fdb_do_dump(const unsigned char *addr, u16 vid,
			    bool is_static, void *data);
int ocelot_mact_learn(struct ocelot *ocelot, int port,
		      const unsigned char mac[ETH_ALEN],
		      unsigned int vid, enum macaccess_entry_type type);
int ocelot_mact_forget(struct ocelot *ocelot,
		       const unsigned char mac[ETH_ALEN], unsigned int vid);
int ocelot_port_lag_join(struct ocelot *ocelot, int port,
			 struct net_device *bond);
void ocelot_port_lag_leave(struct ocelot *ocelot, int port,
			   struct net_device *bond);
struct net_device *ocelot_port_to_netdev(struct ocelot *ocelot, int port);
int ocelot_netdev_to_port(struct net_device *dev);

u32 ocelot_port_readl(struct ocelot_port *port, u32 reg);
void ocelot_port_writel(struct ocelot_port *port, u32 val, u32 reg);

int ocelot_probe_port(struct ocelot *ocelot, int port, struct regmap *target,
		      struct phy_device *phy);

void ocelot_set_cpu_port(struct ocelot *ocelot, int cpu,
			 enum ocelot_tag_prefix injection,
			 enum ocelot_tag_prefix extraction);

extern struct notifier_block ocelot_netdevice_nb;
extern struct notifier_block ocelot_switchdev_nb;
extern struct notifier_block ocelot_switchdev_blocking_nb;

#endif
