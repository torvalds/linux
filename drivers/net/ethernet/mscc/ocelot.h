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
#include <linux/phylink.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <soc/mscc/ocelot_qsys.h>
#include <soc/mscc/ocelot_sys.h>
#include <soc/mscc/ocelot_dev.h>
#include <soc/mscc/ocelot_ana.h>
#include <soc/mscc/ocelot_ptp.h>
#include <soc/mscc/ocelot_vcap.h>
#include <soc/mscc/ocelot.h>
#include "ocelot_rew.h"
#include "ocelot_qs.h"

#define OCELOT_STANDALONE_PVID 0
#define OCELOT_BUFFER_CELL_SZ 60

#define OCELOT_STATS_CHECK_DELAY (2 * HZ)

#define OCELOT_PTP_QUEUE_SZ	128

#define OCELOT_JUMBO_MTU	9000

struct ocelot_port_tc {
	bool block_shared;
	unsigned long offload_cnt;
	unsigned long ingress_mirred_id;
	unsigned long egress_mirred_id;
	unsigned long police_id;
};

struct ocelot_port_private {
	struct ocelot_port port;
	struct net_device *dev;
	struct phylink *phylink;
	struct phylink_config phylink_config;
	u8 chip_port;
	struct ocelot_port_tc tc;
};

struct ocelot_dump_ctx {
	struct net_device *dev;
	struct sk_buff *skb;
	struct netlink_callback *cb;
	int idx;
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

int ocelot_bridge_num_find(struct ocelot *ocelot,
			   const struct net_device *bridge);

int ocelot_port_fdb_do_dump(const unsigned char *addr, u16 vid,
			    bool is_static, void *data);
int ocelot_mact_learn(struct ocelot *ocelot, int port,
		      const unsigned char mac[ETH_ALEN],
		      unsigned int vid, enum macaccess_entry_type type);
int ocelot_mact_forget(struct ocelot *ocelot,
		       const unsigned char mac[ETH_ALEN], unsigned int vid);
struct net_device *ocelot_port_to_netdev(struct ocelot *ocelot, int port);
int ocelot_netdev_to_port(struct net_device *dev);

u32 ocelot_port_readl(struct ocelot_port *port, u32 reg);
void ocelot_port_writel(struct ocelot_port *port, u32 val, u32 reg);

int ocelot_probe_port(struct ocelot *ocelot, int port, struct regmap *target,
		      struct device_node *portnp);
void ocelot_release_port(struct ocelot_port *ocelot_port);
int ocelot_devlink_init(struct ocelot *ocelot);
void ocelot_devlink_teardown(struct ocelot *ocelot);
int ocelot_port_devlink_init(struct ocelot *ocelot, int port,
			     enum devlink_port_flavour flavour);
void ocelot_port_devlink_teardown(struct ocelot *ocelot, int port);

int ocelot_trap_add(struct ocelot *ocelot, int port,
		    unsigned long cookie, bool take_ts,
		    void (*populate)(struct ocelot_vcap_filter *f));
int ocelot_trap_del(struct ocelot *ocelot, int port, unsigned long cookie);

struct ocelot_mirror *ocelot_mirror_get(struct ocelot *ocelot, int to,
					struct netlink_ext_ack *extack);
void ocelot_mirror_put(struct ocelot *ocelot);

extern struct notifier_block ocelot_netdevice_nb;
extern struct notifier_block ocelot_switchdev_nb;
extern struct notifier_block ocelot_switchdev_blocking_nb;
extern const struct devlink_ops ocelot_devlink_ops;

#endif
