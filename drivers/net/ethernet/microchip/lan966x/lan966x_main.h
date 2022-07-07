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

#define READL_SLEEP_US			10
#define READL_TIMEOUT_US		100000000

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

#define NUM_PHYS_PORTS			8
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
#define LAN966X_PHC_PINS_NUM		7

#define IFH_REW_OP_NOOP			0x0
#define IFH_REW_OP_ONE_STEP_PTP		0x3
#define IFH_REW_OP_TWO_STEP_PTP		0x4

#define FDMA_RX_DCB_MAX_DBS		1
#define FDMA_TX_DCB_MAX_DBS		1
#define FDMA_DCB_INFO_DATAL(x)		((x) & GENMASK(15, 0))

#define FDMA_DCB_STATUS_BLOCKL(x)	((x) & GENMASK(15, 0))
#define FDMA_DCB_STATUS_SOF		BIT(16)
#define FDMA_DCB_STATUS_EOF		BIT(17)
#define FDMA_DCB_STATUS_INTR		BIT(18)
#define FDMA_DCB_STATUS_DONE		BIT(19)
#define FDMA_DCB_STATUS_BLOCKO(x)	(((x) << 20) & GENMASK(31, 20))
#define FDMA_DCB_INVALID_DATA		0x1

#define FDMA_XTR_CHANNEL		6
#define FDMA_INJ_CHANNEL		0
#define FDMA_DCB_MAX			512

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

struct lan966x_db {
	u64 dataptr;
	u64 status;
};

struct lan966x_rx_dcb {
	u64 nextptr;
	u64 info;
	struct lan966x_db db[FDMA_RX_DCB_MAX_DBS];
};

struct lan966x_tx_dcb {
	u64 nextptr;
	u64 info;
	struct lan966x_db db[FDMA_TX_DCB_MAX_DBS];
};

struct lan966x_rx {
	struct lan966x *lan966x;

	/* Pointer to the array of hardware dcbs. */
	struct lan966x_rx_dcb *dcbs;

	/* Pointer to the last address in the dcbs. */
	struct lan966x_rx_dcb *last_entry;

	/* For each DB, there is a page */
	struct page *page[FDMA_DCB_MAX][FDMA_RX_DCB_MAX_DBS];

	/* Represents the db_index, it can have a value between 0 and
	 * FDMA_RX_DCB_MAX_DBS, once it reaches the value of FDMA_RX_DCB_MAX_DBS
	 * it means that the DCB can be reused.
	 */
	int db_index;

	/* Represents the index in the dcbs. It has a value between 0 and
	 * FDMA_DCB_MAX
	 */
	int dcb_index;

	/* Represents the dma address to the dcbs array */
	dma_addr_t dma;

	/* Represents the page order that is used to allocate the pages for the
	 * RX buffers. This value is calculated based on max MTU of the devices.
	 */
	u8 page_order;

	u8 channel_id;
};

struct lan966x_tx_dcb_buf {
	struct net_device *dev;
	struct sk_buff *skb;
	dma_addr_t dma_addr;
	bool used;
	bool ptp;
};

struct lan966x_tx {
	struct lan966x *lan966x;

	/* Pointer to the dcb list */
	struct lan966x_tx_dcb *dcbs;
	u16 last_in_use;

	/* Represents the DMA address to the first entry of the dcb entries. */
	dma_addr_t dma;

	/* Array of dcbs that are given to the HW */
	struct lan966x_tx_dcb_buf *dcbs_buf;

	u8 channel_id;

	bool activated;
};

struct lan966x_stat_layout {
	u32 offset;
	char name[ETH_GSTRING_LEN];
};

struct lan966x_phc {
	struct ptp_clock *clock;
	struct ptp_clock_info info;
	struct ptp_pin_desc pins[LAN966X_PHC_PINS_NUM];
	struct hwtstamp_config hwtstamp_config;
	struct lan966x *lan966x;
	u8 index;
};

struct lan966x_skb_cb {
	u8 rew_op;
	u16 ts_id;
	unsigned long jiffies;
};

#define LAN966X_PTP_TIMEOUT		msecs_to_jiffies(10)
#define LAN966X_SKB_CB(skb) \
	((struct lan966x_skb_cb *)((skb)->cb))

struct lan966x {
	struct device *dev;

	u8 num_phys_ports;
	struct lan966x_port **ports;

	void __iomem *regs[NUM_TARGETS];

	int shared_queue_sz;

	u8 base_mac[ETH_ALEN];

	spinlock_t tx_lock; /* lock for frame transmition */

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
	int ptp_irq;
	int fdma_irq;
	int ptp_ext_irq;

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
	spinlock_t ptp_ts_id_lock; /* lock for ts_id */
	struct mutex ptp_lock; /* lock for ptp interface state */
	u16 ptp_skbs;

	/* fdma */
	bool fdma;
	struct net_device *fdma_ndev;
	struct lan966x_rx rx;
	struct lan966x_tx tx;
	struct napi_struct napi;
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
	bool mcast_ena;

	struct phylink_config phylink_config;
	struct phylink_pcs phylink_pcs;
	struct lan966x_port_config config;
	struct phylink *phylink;
	struct phy *serdes;
	struct fwnode_handle *fwnode;

	u8 ptp_cmd;
	u16 ts_id;
	struct sk_buff_head tx_skbs;
};

extern const struct phylink_mac_ops lan966x_phylink_mac_ops;
extern const struct phylink_pcs_ops lan966x_phylink_pcs_ops;
extern const struct ethtool_ops lan966x_ethtool_ops;

bool lan966x_netdevice_check(const struct net_device *dev);

void lan966x_register_notifier_blocks(void);
void lan966x_unregister_notifier_blocks(void);

bool lan966x_hw_offload(struct lan966x *lan966x, u32 port, struct sk_buff *skb);

void lan966x_ifh_get_src_port(void *ifh, u64 *src_port);
void lan966x_ifh_get_timestamp(void *ifh, u64 *timestamp);

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
void lan966x_mdb_clear_entries(struct lan966x *lan966x);
void lan966x_mdb_restore_entries(struct lan966x *lan966x);

int lan966x_ptp_init(struct lan966x *lan966x);
void lan966x_ptp_deinit(struct lan966x *lan966x);
int lan966x_ptp_hwtstamp_set(struct lan966x_port *port, struct ifreq *ifr);
int lan966x_ptp_hwtstamp_get(struct lan966x_port *port, struct ifreq *ifr);
void lan966x_ptp_rxtstamp(struct lan966x *lan966x, struct sk_buff *skb,
			  u64 timestamp);
int lan966x_ptp_txtstamp_request(struct lan966x_port *port,
				 struct sk_buff *skb);
void lan966x_ptp_txtstamp_release(struct lan966x_port *port,
				  struct sk_buff *skb);
irqreturn_t lan966x_ptp_irq_handler(int irq, void *args);
irqreturn_t lan966x_ptp_ext_irq_handler(int irq, void *args);

int lan966x_fdma_xmit(struct sk_buff *skb, __be32 *ifh, struct net_device *dev);
int lan966x_fdma_change_mtu(struct lan966x *lan966x);
void lan966x_fdma_netdev_init(struct lan966x *lan966x, struct net_device *dev);
void lan966x_fdma_netdev_deinit(struct lan966x *lan966x, struct net_device *dev);
int lan966x_fdma_init(struct lan966x *lan966x);
void lan966x_fdma_deinit(struct lan966x *lan966x);
irqreturn_t lan966x_fdma_irq_handler(int irq, void *args);

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
