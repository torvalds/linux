/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __LAN966X_MAIN_H__
#define __LAN966X_MAIN_H__

#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/jiffies.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <linux/ptp_clock_kernel.h>
#include <net/switchdev.h>

#include "lan966x_regs.h"
#include "lan966x_ifh.h"

#define TABLE_UPDATE_SLEEP_US		10
#define TABLE_UPDATE_TIMEOUT_US		100000

#define LAN966X_BUFFER_CELL_SZ		64
#define LAN966X_BUFFER_MEMORY		(160 * 1024)
#define LAN966X_BUFFER_MIN_SZ		60

#define PGID_AGGR			64
#define PGID_SRC			80
#define PGID_ENTRIES			89

#define UNAWARE_PVID			0
#define HOST_PVID			4095

/* Reserved amount for (SRC, PRIO) at index 8*SRC + PRIO */
#define QSYS_Q_RSRV			95

#define CPU_PORT			8

/* Reserved PGIDs */
#define PGID_CPU			(PGID_AGGR - 6)
#define PGID_UC				(PGID_AGGR - 5)
#define PGID_BC				(PGID_AGGR - 4)
#define PGID_MC				(PGID_AGGR - 3)
#define PGID_MCIPV4			(PGID_AGGR - 2)
#define PGID_MCIPV6			(PGID_AGGR - 1)

/* Non-reserved PGIDs, used for general purpose */
#define PGID_GP_START			(CPU_PORT + 1)
#define PGID_GP_END			PGID_CPU

#define LAN966X_SPEED_NONE		0
#define LAN966X_SPEED_2500		1
#define LAN966X_SPEED_1000		1
#define LAN966X_SPEED_100		2
#define LAN966X_SPEED_10		3

#define LAN966X_PHC_COUNT		3
#define LAN966X_PHC_PORT		0

/* MAC table entry types.
 * ENTRYTYPE_NORMAL is subject to aging.
 * ENTRYTYPE_LOCKED is not subject to aging.
 * ENTRYTYPE_MACv4 is not subject to aging. For IPv4 multicast.
 * ENTRYTYPE_MACv6 is not subject to aging. For IPv6 multicast.
 */
enum macaccess_entry_type {
	ENTRYTYPE_NORMAL = 0,
	ENTRYTYPE_LOCKED,
	ENTRYTYPE_MACV4,
	ENTRYTYPE_MACV6,
};

struct lan966x_port;

struct lan966x_stat_layout {
	u32 offset;
	char name[ETH_GSTRING_LEN];
};

struct lan966x_phc {
	struct ptp_clock *clock;
	struct ptp_clock_info info;
	struct hwtstamp_config hwtstamp_config;
	struct lan966x *lan966x;
	u8 index;
};

struct lan966x {
	struct device *dev;

	u8 num_phys_ports;
	struct lan966x_port **ports;

	void __iomem *regs[NUM_TARGETS];

	int shared_queue_sz;

	u8 base_mac[ETH_ALEN];

	struct net_device *bridge;
	u16 bridge_mask;
	u16 bridge_fwd_mask;

	struct list_head mac_entries;
	spinlock_t mac_lock; /* lock for mac_entries list */

	u16 vlan_mask[VLAN_N_VID];
	DECLARE_BITMAP(cpu_vlan_mask, VLAN_N_VID);

	/* stats */
	const struct lan966x_stat_layout *stats_layout;
	u32 num_stats;

	/* workqueue for reading stats */
	struct mutex stats_lock;
	u64 *stats;
	struct delayed_work stats_work;
	struct workqueue_struct *stats_queue;

	/* interrupts */
	int xtr_irq;
	int ana_irq;

	/* worqueue for fdb */
	struct workqueue_struct *fdb_work;
	struct list_head fdb_entries;

	/* mdb */
	struct list_head mdb_entries;
	struct list_head pgid_entries;

	/* ptp */
	bool ptp;
	struct lan966x_phc phc[LAN966X_PHC_COUNT];
	spinlock_t ptp_clock_lock; /* lock for phc */
};

struct lan966x_port_config {
	phy_interface_t portmode;
	const unsigned long *advertising;
	int speed;
	int duplex;
	u32 pause;
	bool inband;
	bool autoneg;
};

struct lan966x_port {
	struct net_device *dev;
	struct lan966x *lan966x;

	u8 chip_port;
	u16 pvid;
	u16 vid;
	bool vlan_aware;

	bool learn_ena;

	struct phylink_config phylink_config;
	struct phylink_pcs phylink_pcs;
	struct lan966x_port_config config;
	struct phylink *phylink;
	struct phy *serdes;
	struct fwnode_handle *fwnode;
};

extern const struct phylink_mac_ops lan966x_phylink_mac_ops;
extern const struct phylink_pcs_ops lan966x_phylink_pcs_ops;
extern const struct ethtool_ops lan966x_ethtool_ops;

bool lan966x_netdevice_check(const struct net_device *dev);

void lan966x_register_notifier_blocks(void);
void lan966x_unregister_notifier_blocks(void);

void lan966x_stats_get(struct net_device *dev,
		       struct rtnl_link_stats64 *stats);
int lan966x_stats_init(struct lan966x *lan966x);

void lan966x_port_config_down(struct lan966x_port *port);
void lan966x_port_config_up(struct lan966x_port *port);
void lan966x_port_status_get(struct lan966x_port *port,
			     struct phylink_link_state *state);
int lan966x_port_pcs_set(struct lan966x_port *port,
			 struct lan966x_port_config *config);
void lan966x_port_init(struct lan966x_port *port);

int lan966x_mac_ip_learn(struct lan966x *lan966x,
			 bool cpu_copy,
			 const unsigned char mac[ETH_ALEN],
			 unsigned int vid,
			 enum macaccess_entry_type type);
int lan966x_mac_learn(struct lan966x *lan966x, int port,
		      const unsigned char mac[ETH_ALEN],
		      unsigned int vid,
		      enum macaccess_entry_type type);
int lan966x_mac_forget(struct lan966x *lan966x,
		       const unsigned char mac[ETH_ALEN],
		       unsigned int vid,
		       enum macaccess_entry_type type);
int lan966x_mac_cpu_learn(struct lan966x *lan966x, const char *addr, u16 vid);
int lan966x_mac_cpu_forget(struct lan966x *lan966x, const char *addr, u16 vid);
void lan966x_mac_init(struct lan966x *lan966x);
void lan966x_mac_set_ageing(struct lan966x *lan966x,
			    u32 ageing);
int lan966x_mac_del_entry(struct lan966x *lan966x,
			  const unsigned char *addr,
			  u16 vid);
int lan966x_mac_add_entry(struct lan966x *lan966x,
			  struct lan966x_port *port,
			  const unsigned char *addr,
			  u16 vid);
void lan966x_mac_purge_entries(struct lan966x *lan966x);
irqreturn_t lan966x_mac_irq_handler(struct lan966x *lan966x);

void lan966x_vlan_init(struct lan966x *lan966x);
void lan966x_vlan_port_apply(struct lan966x_port *port);
bool lan966x_vlan_cpu_member_cpu_vlan_mask(struct lan966x *lan966x, u16 vid);
void lan966x_vlan_port_set_vlan_aware(struct lan966x_port *port,
				      bool vlan_aware);
int lan966x_vlan_port_set_vid(struct lan966x_port *port,
			      u16 vid,
			      bool pvid,
			      bool untagged);
void lan966x_vlan_port_add_vlan(struct lan966x_port *port,
				u16 vid,
				bool pvid,
				bool untagged);
void lan966x_vlan_port_del_vlan(struct lan966x_port *port, u16 vid);
void lan966x_vlan_cpu_add_vlan(struct lan966x *lan966x, u16 vid);
void lan966x_vlan_cpu_del_vlan(struct lan966x *lan966x, u16 vid);

void lan966x_fdb_write_entries(struct lan966x *lan966x, u16 vid);
void lan966x_fdb_erase_entries(struct lan966x *lan966x, u16 vid);
int lan966x_fdb_init(struct lan966x *lan966x);
void lan966x_fdb_deinit(struct lan966x *lan966x);
int lan966x_handle_fdb(struct net_device *dev,
		       struct net_device *orig_dev,
		       unsigned long event, const void *ctx,
		       const struct switchdev_notifier_fdb_info *fdb_info);

void lan966x_mdb_init(struct lan966x *lan966x);
void lan966x_mdb_deinit(struct lan966x *lan966x);
int lan966x_handle_port_mdb_add(struct lan966x_port *port,
				const struct switchdev_obj *obj);
int lan966x_handle_port_mdb_del(struct lan966x_port *port,
				const struct switchdev_obj *obj);
void lan966x_mdb_erase_entries(struct lan966x *lan966x, u16 vid);
void lan966x_mdb_write_entries(struct lan966x *lan966x, u16 vid);

int lan966x_ptp_init(struct lan966x *lan966x);
void lan966x_ptp_deinit(struct lan966x *lan966x);

static inline void __iomem *lan_addr(void __iomem *base[],
				     int id, int tinst, int tcnt,
				     int gbase, int ginst,
				     int gcnt, int gwidth,
				     int raddr, int rinst,
				     int rcnt, int rwidth)
{
	WARN_ON((tinst) >= tcnt);
	WARN_ON((ginst) >= gcnt);
	WARN_ON((rinst) >= rcnt);
	return base[id + (tinst)] +
		gbase + ((ginst) * gwidth) +
		raddr + ((rinst) * rwidth);
}

static inline u32 lan_rd(struct lan966x *lan966x, int id, int tinst, int tcnt,
			 int gbase, int ginst, int gcnt, int gwidth,
			 int raddr, int rinst, int rcnt, int rwidth)
{
	return readl(lan_addr(lan966x->regs, id, tinst, tcnt, gbase, ginst,
			      gcnt, gwidth, raddr, rinst, rcnt, rwidth));
}

static inline void lan_wr(u32 val, struct lan966x *lan966x,
			  int id, int tinst, int tcnt,
			  int gbase, int ginst, int gcnt, int gwidth,
			  int raddr, int rinst, int rcnt, int rwidth)
{
	writel(val, lan_addr(lan966x->regs, id, tinst, tcnt,
			     gbase, ginst, gcnt, gwidth,
			     raddr, rinst, rcnt, rwidth));
}

static inline void lan_rmw(u32 val, u32 mask, struct lan966x *lan966x,
			   int id, int tinst, int tcnt,
			   int gbase, int ginst, int gcnt, int gwidth,
			   int raddr, int rinst, int rcnt, int rwidth)
{
	u32 nval;

	nval = readl(lan_addr(lan966x->regs, id, tinst, tcnt, gbase, ginst,
			      gcnt, gwidth, raddr, rinst, rcnt, rwidth));
	nval = (nval & ~mask) | (val & mask);
	writel(nval, lan_addr(lan966x->regs, id, tinst, tcnt, gbase, ginst,
			      gcnt, gwidth, raddr, rinst, rcnt, rwidth));
}

#endif /* __LAN966X_MAIN_H__ */
