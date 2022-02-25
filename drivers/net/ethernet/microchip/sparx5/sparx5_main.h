/* SPDX-License-Identifier: GPL-2.0+ */
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2021 Microchip Technology Inc. and its subsidiaries.
 */

#ifndef __SPARX5_MAIN_H__
#define __SPARX5_MAIN_H__

#include <linux/types.h>
#include <linux/phy/phy.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/if_vlan.h>
#include <linux/bitmap.h>
#include <linux/phylink.h>
#include <linux/hrtimer.h>

#include "sparx5_main_regs.h"

/* Target chip type */
enum spx5_target_chiptype {
	SPX5_TARGET_CT_7546    = 0x7546,  /* SparX-5-64  Enterprise */
	SPX5_TARGET_CT_7549    = 0x7549,  /* SparX-5-90  Enterprise */
	SPX5_TARGET_CT_7552    = 0x7552,  /* SparX-5-128 Enterprise */
	SPX5_TARGET_CT_7556    = 0x7556,  /* SparX-5-160 Enterprise */
	SPX5_TARGET_CT_7558    = 0x7558,  /* SparX-5-200 Enterprise */
	SPX5_TARGET_CT_7546TSN = 0x47546, /* SparX-5-64i Industrial */
	SPX5_TARGET_CT_7549TSN = 0x47549, /* SparX-5-90i Industrial */
	SPX5_TARGET_CT_7552TSN = 0x47552, /* SparX-5-128i Industrial */
	SPX5_TARGET_CT_7556TSN = 0x47556, /* SparX-5-160i Industrial */
	SPX5_TARGET_CT_7558TSN = 0x47558, /* SparX-5-200i Industrial */
};

enum sparx5_port_max_tags {
	SPX5_PORT_MAX_TAGS_NONE,  /* No extra tags allowed */
	SPX5_PORT_MAX_TAGS_ONE,   /* Single tag allowed */
	SPX5_PORT_MAX_TAGS_TWO    /* Single and double tag allowed */
};

enum sparx5_vlan_port_type {
	SPX5_VLAN_PORT_TYPE_UNAWARE, /* VLAN unaware port */
	SPX5_VLAN_PORT_TYPE_C,       /* C-port */
	SPX5_VLAN_PORT_TYPE_S,       /* S-port */
	SPX5_VLAN_PORT_TYPE_S_CUSTOM /* S-port using custom type */
};

#define SPX5_PORTS             65
#define SPX5_PORT_CPU          (SPX5_PORTS)  /* Next port is CPU port */
#define SPX5_PORT_CPU_0        (SPX5_PORT_CPU + 0) /* CPU Port 65 */
#define SPX5_PORT_CPU_1        (SPX5_PORT_CPU + 1) /* CPU Port 66 */
#define SPX5_PORT_VD0          (SPX5_PORT_CPU + 2) /* VD0/Port 67 used for IPMC */
#define SPX5_PORT_VD1          (SPX5_PORT_CPU + 3) /* VD1/Port 68 used for AFI/OAM */
#define SPX5_PORT_VD2          (SPX5_PORT_CPU + 4) /* VD2/Port 69 used for IPinIP*/
#define SPX5_PORTS_ALL         (SPX5_PORT_CPU + 5) /* Total number of ports */

#define PGID_BASE              SPX5_PORTS /* Starts after port PGIDs */
#define PGID_UC_FLOOD          (PGID_BASE + 0)
#define PGID_MC_FLOOD          (PGID_BASE + 1)
#define PGID_IPV4_MC_DATA      (PGID_BASE + 2)
#define PGID_IPV4_MC_CTRL      (PGID_BASE + 3)
#define PGID_IPV6_MC_DATA      (PGID_BASE + 4)
#define PGID_IPV6_MC_CTRL      (PGID_BASE + 5)
#define PGID_BCAST	       (PGID_BASE + 6)
#define PGID_CPU	       (PGID_BASE + 7)

#define IFH_LEN                9 /* 36 bytes */
#define NULL_VID               0
#define SPX5_MACT_PULL_DELAY   (2 * HZ)
#define SPX5_STATS_CHECK_DELAY (1 * HZ)
#define SPX5_PRIOS             8     /* Number of priority queues */
#define SPX5_BUFFER_CELL_SZ    184   /* Cell size  */
#define SPX5_BUFFER_MEMORY     4194280 /* 22795 words * 184 bytes */

#define XTR_QUEUE     0
#define INJ_QUEUE     0

#define FDMA_DCB_MAX			64
#define FDMA_RX_DCB_MAX_DBS		15
#define FDMA_TX_DCB_MAX_DBS		1

struct sparx5;

struct sparx5_db_hw {
	u64 dataptr;
	u64 status;
};

struct sparx5_rx_dcb_hw {
	u64 nextptr;
	u64 info;
	struct sparx5_db_hw db[FDMA_RX_DCB_MAX_DBS];
};

struct sparx5_tx_dcb_hw {
	u64 nextptr;
	u64 info;
	struct sparx5_db_hw db[FDMA_TX_DCB_MAX_DBS];
};

/* Frame DMA receive state:
 * For each DB, there is a SKB, and the skb data pointer is mapped in
 * the DB. Once a frame is received the skb is given to the upper layers
 * and a new skb is added to the dcb.
 * When the db_index reached FDMA_RX_DCB_MAX_DBS the DB is reused.
 */
struct sparx5_rx {
	struct sparx5_rx_dcb_hw *dcb_entries;
	struct sparx5_rx_dcb_hw *last_entry;
	struct sk_buff *skb[FDMA_DCB_MAX][FDMA_RX_DCB_MAX_DBS];
	int db_index;
	int dcb_index;
	dma_addr_t dma;
	struct napi_struct napi;
	u32 channel_id;
	struct net_device *ndev;
	u64 packets;
};

/* Frame DMA transmit state:
 * DCBs are chained using the DCBs nextptr field.
 */
struct sparx5_tx {
	struct sparx5_tx_dcb_hw *curr_entry;
	struct sparx5_tx_dcb_hw *first_entry;
	struct list_head db_list;
	dma_addr_t dma;
	u32 channel_id;
	u64 packets;
	u64 dropped;
};

struct sparx5_port_config {
	phy_interface_t portmode;
	u32 bandwidth;
	int speed;
	int duplex;
	enum phy_media media;
	bool inband;
	bool power_down;
	bool autoneg;
	bool serdes_reset;
	u32 pause;
	u32 pause_adv;
	phy_interface_t phy_mode;
	u32 sd_sgpio;
};

struct sparx5_port {
	struct net_device *ndev;
	struct sparx5 *sparx5;
	struct device_node *of_node;
	struct phy *serdes;
	struct sparx5_port_config conf;
	struct phylink_config phylink_config;
	struct phylink *phylink;
	struct phylink_pcs phylink_pcs;
	u16 portno;
	/* Ingress default VLAN (pvid) */
	u16 pvid;
	/* Egress default VLAN (vid) */
	u16 vid;
	bool signd_internal;
	bool signd_active_high;
	bool signd_enable;
	bool flow_control;
	enum sparx5_port_max_tags max_vlan_tags;
	enum sparx5_vlan_port_type vlan_type;
	u32 custom_etype;
	u32 ifh[IFH_LEN];
	bool vlan_aware;
	struct hrtimer inj_timer;
};

enum sparx5_core_clockfreq {
	SPX5_CORE_CLOCK_DEFAULT,  /* Defaults to the highest supported frequency */
	SPX5_CORE_CLOCK_250MHZ,   /* 250MHZ core clock frequency */
	SPX5_CORE_CLOCK_500MHZ,   /* 500MHZ core clock frequency */
	SPX5_CORE_CLOCK_625MHZ,   /* 625MHZ core clock frequency */
};

struct sparx5 {
	struct platform_device *pdev;
	struct device *dev;
	u32 chip_id;
	enum spx5_target_chiptype target_ct;
	void __iomem *regs[NUM_TARGETS];
	int port_count;
	struct mutex lock; /* MAC reg lock */
	/* port structures are in net device */
	struct sparx5_port *ports[SPX5_PORTS];
	enum sparx5_core_clockfreq coreclock;
	/* Statistics */
	u32 num_stats;
	u32 num_ethtool_stats;
	const char * const *stats_layout;
	u64 *stats;
	/* Workqueue for reading stats */
	struct mutex queue_stats_lock;
	struct delayed_work stats_work;
	struct workqueue_struct *stats_queue;
	/* Notifiers */
	struct notifier_block netdevice_nb;
	struct notifier_block switchdev_nb;
	struct notifier_block switchdev_blocking_nb;
	/* Switch state */
	u8 base_mac[ETH_ALEN];
	/* Associated bridge device (when bridged) */
	struct net_device *hw_bridge_dev;
	/* Bridged interfaces */
	DECLARE_BITMAP(bridge_mask, SPX5_PORTS);
	DECLARE_BITMAP(bridge_fwd_mask, SPX5_PORTS);
	DECLARE_BITMAP(bridge_lrn_mask, SPX5_PORTS);
	DECLARE_BITMAP(vlan_mask[VLAN_N_VID], SPX5_PORTS);
	/* SW MAC table */
	struct list_head mact_entries;
	/* mac table list (mact_entries) mutex */
	struct mutex mact_lock;
	struct delayed_work mact_work;
	struct workqueue_struct *mact_queue;
	/* Board specifics */
	bool sd_sgpio_remapping;
	/* Register based inj/xtr */
	int xtr_irq;
	/* Frame DMA */
	int fdma_irq;
	struct sparx5_rx rx;
	struct sparx5_tx tx;
};

/* sparx5_switchdev.c */
int sparx5_register_notifier_blocks(struct sparx5 *sparx5);
void sparx5_unregister_notifier_blocks(struct sparx5 *sparx5);

/* sparx5_packet.c */
struct frame_info {
	int src_port;
};

void sparx5_xtr_flush(struct sparx5 *sparx5, u8 grp);
void sparx5_ifh_parse(u32 *ifh, struct frame_info *info);
irqreturn_t sparx5_xtr_handler(int irq, void *_priv);
int sparx5_port_xmit_impl(struct sk_buff *skb, struct net_device *dev);
int sparx5_manual_injection_mode(struct sparx5 *sparx5);
void sparx5_port_inj_timer_setup(struct sparx5_port *port);

/* sparx5_fdma.c */
int sparx5_fdma_start(struct sparx5 *sparx5);
int sparx5_fdma_stop(struct sparx5 *sparx5);
int sparx5_fdma_xmit(struct sparx5 *sparx5, u32 *ifh, struct sk_buff *skb);
irqreturn_t sparx5_fdma_handler(int irq, void *args);

/* sparx5_mactable.c */
void sparx5_mact_pull_work(struct work_struct *work);
int sparx5_mact_learn(struct sparx5 *sparx5, int port,
		      const unsigned char mac[ETH_ALEN], u16 vid);
bool sparx5_mact_getnext(struct sparx5 *sparx5,
			 unsigned char mac[ETH_ALEN], u16 *vid, u32 *pcfg2);
int sparx5_mact_forget(struct sparx5 *sparx5,
		       const unsigned char mac[ETH_ALEN], u16 vid);
int sparx5_add_mact_entry(struct sparx5 *sparx5,
			  struct sparx5_port *port,
			  const unsigned char *addr, u16 vid);
int sparx5_del_mact_entry(struct sparx5 *sparx5,
			  const unsigned char *addr,
			  u16 vid);
int sparx5_mc_sync(struct net_device *dev, const unsigned char *addr);
int sparx5_mc_unsync(struct net_device *dev, const unsigned char *addr);
void sparx5_set_ageing(struct sparx5 *sparx5, int msecs);
void sparx5_mact_init(struct sparx5 *sparx5);

/* sparx5_vlan.c */
void sparx5_pgid_update_mask(struct sparx5_port *port, int pgid, bool enable);
void sparx5_update_fwd(struct sparx5 *sparx5);
void sparx5_vlan_init(struct sparx5 *sparx5);
void sparx5_vlan_port_setup(struct sparx5 *sparx5, int portno);
int sparx5_vlan_vid_add(struct sparx5_port *port, u16 vid, bool pvid,
			bool untagged);
int sparx5_vlan_vid_del(struct sparx5_port *port, u16 vid);
void sparx5_vlan_port_apply(struct sparx5 *sparx5, struct sparx5_port *port);

/* sparx5_calendar.c */
int sparx5_config_auto_calendar(struct sparx5 *sparx5);
int sparx5_config_dsm_calendar(struct sparx5 *sparx5);

/* sparx5_ethtool.c */
void sparx5_get_stats64(struct net_device *ndev, struct rtnl_link_stats64 *stats);
int sparx_stats_init(struct sparx5 *sparx5);

/* sparx5_netdev.c */
bool sparx5_netdevice_check(const struct net_device *dev);
struct net_device *sparx5_create_netdev(struct sparx5 *sparx5, u32 portno);
int sparx5_register_netdevs(struct sparx5 *sparx5);
void sparx5_destroy_netdevs(struct sparx5 *sparx5);
void sparx5_unregister_netdevs(struct sparx5 *sparx5);

/* Clock period in picoseconds */
static inline u32 sparx5_clk_period(enum sparx5_core_clockfreq cclock)
{
	switch (cclock) {
	case SPX5_CORE_CLOCK_250MHZ:
		return 4000;
	case SPX5_CORE_CLOCK_500MHZ:
		return 2000;
	case SPX5_CORE_CLOCK_625MHZ:
	default:
		return 1600;
	}
}

static inline bool sparx5_is_baser(phy_interface_t interface)
{
	return interface == PHY_INTERFACE_MODE_5GBASER ||
		   interface == PHY_INTERFACE_MODE_10GBASER ||
		   interface == PHY_INTERFACE_MODE_25GBASER;
}

extern const struct phylink_mac_ops sparx5_phylink_mac_ops;
extern const struct phylink_pcs_ops sparx5_phylink_pcs_ops;
extern const struct ethtool_ops sparx5_ethtool_ops;

/* Calculate raw offset */
static inline __pure int spx5_offset(int id, int tinst, int tcnt,
				     int gbase, int ginst,
				     int gcnt, int gwidth,
				     int raddr, int rinst,
				     int rcnt, int rwidth)
{
	WARN_ON((tinst) >= tcnt);
	WARN_ON((ginst) >= gcnt);
	WARN_ON((rinst) >= rcnt);
	return gbase + ((ginst) * gwidth) +
		raddr + ((rinst) * rwidth);
}

/* Read, Write and modify registers content.
 * The register definition macros start at the id
 */
static inline void __iomem *spx5_addr(void __iomem *base[],
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

static inline void __iomem *spx5_inst_addr(void __iomem *base,
					   int gbase, int ginst,
					   int gcnt, int gwidth,
					   int raddr, int rinst,
					   int rcnt, int rwidth)
{
	WARN_ON((ginst) >= gcnt);
	WARN_ON((rinst) >= rcnt);
	return base +
		gbase + ((ginst) * gwidth) +
		raddr + ((rinst) * rwidth);
}

static inline u32 spx5_rd(struct sparx5 *sparx5, int id, int tinst, int tcnt,
			  int gbase, int ginst, int gcnt, int gwidth,
			  int raddr, int rinst, int rcnt, int rwidth)
{
	return readl(spx5_addr(sparx5->regs, id, tinst, tcnt, gbase, ginst,
			       gcnt, gwidth, raddr, rinst, rcnt, rwidth));
}

static inline u32 spx5_inst_rd(void __iomem *iomem, int id, int tinst, int tcnt,
			       int gbase, int ginst, int gcnt, int gwidth,
			       int raddr, int rinst, int rcnt, int rwidth)
{
	return readl(spx5_inst_addr(iomem, gbase, ginst,
				     gcnt, gwidth, raddr, rinst, rcnt, rwidth));
}

static inline void spx5_wr(u32 val, struct sparx5 *sparx5,
			   int id, int tinst, int tcnt,
			   int gbase, int ginst, int gcnt, int gwidth,
			   int raddr, int rinst, int rcnt, int rwidth)
{
	writel(val, spx5_addr(sparx5->regs, id, tinst, tcnt,
			      gbase, ginst, gcnt, gwidth,
			      raddr, rinst, rcnt, rwidth));
}

static inline void spx5_inst_wr(u32 val, void __iomem *iomem,
				int id, int tinst, int tcnt,
				int gbase, int ginst, int gcnt, int gwidth,
				int raddr, int rinst, int rcnt, int rwidth)
{
	writel(val, spx5_inst_addr(iomem,
				   gbase, ginst, gcnt, gwidth,
				   raddr, rinst, rcnt, rwidth));
}

static inline void spx5_rmw(u32 val, u32 mask, struct sparx5 *sparx5,
			    int id, int tinst, int tcnt,
			    int gbase, int ginst, int gcnt, int gwidth,
			    int raddr, int rinst, int rcnt, int rwidth)
{
	u32 nval;

	nval = readl(spx5_addr(sparx5->regs, id, tinst, tcnt, gbase, ginst,
			       gcnt, gwidth, raddr, rinst, rcnt, rwidth));
	nval = (nval & ~mask) | (val & mask);
	writel(nval, spx5_addr(sparx5->regs, id, tinst, tcnt, gbase, ginst,
			       gcnt, gwidth, raddr, rinst, rcnt, rwidth));
}

static inline void spx5_inst_rmw(u32 val, u32 mask, void __iomem *iomem,
				 int id, int tinst, int tcnt,
				 int gbase, int ginst, int gcnt, int gwidth,
				 int raddr, int rinst, int rcnt, int rwidth)
{
	u32 nval;

	nval = readl(spx5_inst_addr(iomem, gbase, ginst, gcnt, gwidth, raddr,
				    rinst, rcnt, rwidth));
	nval = (nval & ~mask) | (val & mask);
	writel(nval, spx5_inst_addr(iomem, gbase, ginst, gcnt, gwidth, raddr,
				    rinst, rcnt, rwidth));
}

static inline void __iomem *spx5_inst_get(struct sparx5 *sparx5, int id, int tinst)
{
	return sparx5->regs[id + tinst];
}

static inline void __iomem *spx5_reg_get(struct sparx5 *sparx5,
					 int id, int tinst, int tcnt,
					 int gbase, int ginst, int gcnt, int gwidth,
					 int raddr, int rinst, int rcnt, int rwidth)
{
	return spx5_addr(sparx5->regs, id, tinst, tcnt,
			 gbase, ginst, gcnt, gwidth,
			 raddr, rinst, rcnt, rwidth);
}

#endif	/* __SPARX5_MAIN_H__ */
