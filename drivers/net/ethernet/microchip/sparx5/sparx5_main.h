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
#include <linux/net_tstamp.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/hrtimer.h>
#include <linux/debugfs.h>
#include <net/flow_offload.h>

#include <fdma_api.h>

#include "sparx5_main_regs.h"

/* Target chip type */
enum spx5_target_chiptype {
	SPX5_TARGET_CT_7546       = 0x7546,  /* SparX-5-64  Enterprise */
	SPX5_TARGET_CT_7549       = 0x7549,  /* SparX-5-90  Enterprise */
	SPX5_TARGET_CT_7552       = 0x7552,  /* SparX-5-128 Enterprise */
	SPX5_TARGET_CT_7556       = 0x7556,  /* SparX-5-160 Enterprise */
	SPX5_TARGET_CT_7558       = 0x7558,  /* SparX-5-200 Enterprise */
	SPX5_TARGET_CT_7546TSN    = 0x47546, /* SparX-5-64i Industrial */
	SPX5_TARGET_CT_7549TSN    = 0x47549, /* SparX-5-90i Industrial */
	SPX5_TARGET_CT_7552TSN    = 0x47552, /* SparX-5-128i Industrial */
	SPX5_TARGET_CT_7556TSN    = 0x47556, /* SparX-5-160i Industrial */
	SPX5_TARGET_CT_7558TSN    = 0x47558, /* SparX-5-200i Industrial */
	SPX5_TARGET_CT_LAN9694    = 0x9694,  /* lan969x-40 */
	SPX5_TARGET_CT_LAN9691VAO = 0x9691,  /* lan969x-40-VAO */
	SPX5_TARGET_CT_LAN9694TSN = 0x9695,  /* lan969x-40-TSN */
	SPX5_TARGET_CT_LAN9694RED = 0x969A,  /* lan969x-40-RED */
	SPX5_TARGET_CT_LAN9696    = 0x9696,  /* lan969x-60 */
	SPX5_TARGET_CT_LAN9692VAO = 0x9692,  /* lan969x-65-VAO */
	SPX5_TARGET_CT_LAN9696TSN = 0x9697,  /* lan969x-60-TSN */
	SPX5_TARGET_CT_LAN9696RED = 0x969B,  /* lan969x-60-RED */
	SPX5_TARGET_CT_LAN9698    = 0x9698,  /* lan969x-100 */
	SPX5_TARGET_CT_LAN9693VAO = 0x9693,  /* lan969x-100-VAO */
	SPX5_TARGET_CT_LAN9698TSN = 0x9699,  /* lan969x-100-TSN */
	SPX5_TARGET_CT_LAN9698RED = 0x969C,  /* lan969x-100-RED */
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

/* This is used in calendar configuration */
enum sparx5_cal_bw {
	SPX5_CAL_SPEED_NONE = 0,
	SPX5_CAL_SPEED_1G   = 1,
	SPX5_CAL_SPEED_2G5  = 2,
	SPX5_CAL_SPEED_5G   = 3,
	SPX5_CAL_SPEED_10G  = 4,
	SPX5_CAL_SPEED_25G  = 5,
	SPX5_CAL_SPEED_0G5  = 6,
	SPX5_CAL_SPEED_12G5 = 7
};

enum sparx5_feature {
	SPX5_FEATURE_PSFP = BIT(0),
	SPX5_FEATURE_PTP  = BIT(1),
};

#define SPX5_PORTS             65
#define SPX5_PORTS_ALL         70 /* Total number of ports */

#define SPX5_PORT_CPU_0        0 /* CPU Port 0 */
#define SPX5_PORT_CPU_1        1 /* CPU Port 1 */
#define SPX5_PORT_VD0          2 /* VD0/Port used for IPMC */
#define SPX5_PORT_VD1          3 /* VD1/Port used for AFI/OAM */
#define SPX5_PORT_VD2          4 /* VD2/Port used for IPinIP*/

#define PGID_UC_FLOOD          0
#define PGID_MC_FLOOD          1
#define PGID_IPV4_MC_DATA      2
#define PGID_IPV4_MC_CTRL      3
#define PGID_IPV6_MC_DATA      4
#define PGID_IPV6_MC_CTRL      5
#define PGID_BCAST             6
#define PGID_CPU               7
#define PGID_MCAST_START       8

#define PGID_TABLE_SIZE	       3290

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

#define SPARX5_PHC_COUNT		3
#define SPARX5_PHC_PORT			0

#define IFH_REW_OP_NOOP			0x0
#define IFH_REW_OP_ONE_STEP_PTP		0x3
#define IFH_REW_OP_TWO_STEP_PTP		0x4

#define IFH_PDU_TYPE_NONE		0x0
#define IFH_PDU_TYPE_PTP		0x5
#define IFH_PDU_TYPE_IPV4_UDP_PTP	0x6
#define IFH_PDU_TYPE_IPV6_UDP_PTP	0x7

#define SPX5_DSM_CAL_LEN               64
#define SPX5_DSM_CAL_MAX_DEVS_PER_TAXI 13
#define SPX5_DSM_CAL_EMPTY             0xFFFF

#define SPARX5_MAX_PTP_ID	512

struct sparx5;

struct sparx5_calendar_data {
	u32 schedule[SPX5_DSM_CAL_LEN];
	u32 avg_dist[SPX5_DSM_CAL_MAX_DEVS_PER_TAXI];
	u32 taxi_ports[SPX5_DSM_CAL_MAX_DEVS_PER_TAXI];
	u32 taxi_speeds[SPX5_DSM_CAL_MAX_DEVS_PER_TAXI];
	u32 dev_slots[SPX5_DSM_CAL_MAX_DEVS_PER_TAXI];
	u32 new_slots[SPX5_DSM_CAL_LEN];
	u32 temp_sched[SPX5_DSM_CAL_LEN];
	u32 indices[SPX5_DSM_CAL_LEN];
	u32 short_list[SPX5_DSM_CAL_LEN];
	u32 long_list[SPX5_DSM_CAL_LEN];
};

/* Frame DMA receive state:
 * For each DB, there is a SKB, and the skb data pointer is mapped in
 * the DB. Once a frame is received the skb is given to the upper layers
 * and a new skb is added to the dcb.
 * When the db_index reached FDMA_RX_DCB_MAX_DBS the DB is reused.
 */
struct sparx5_rx {
	struct fdma fdma;
	struct sk_buff *skb[FDMA_DCB_MAX][FDMA_RX_DCB_MAX_DBS];
	dma_addr_t dma;
	struct napi_struct napi;
	struct net_device *ndev;
	u64 packets;
};

/* Frame DMA transmit state:
 * DCBs are chained using the DCBs nextptr field.
 */
struct sparx5_tx {
	struct fdma fdma;
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
	struct flow_stats mirror_stats;
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
	bool vlan_aware;
	struct hrtimer inj_timer;
	/* ptp */
	u8 ptp_cmd;
	u16 ts_id;
	struct sk_buff_head tx_skbs;
	bool is_mrouter;
	struct list_head tc_templates; /* list of TC templates on this port */
};

enum sparx5_core_clockfreq {
	SPX5_CORE_CLOCK_DEFAULT,  /* Defaults to the highest supported frequency */
	SPX5_CORE_CLOCK_250MHZ,   /* 250MHZ core clock frequency */
	SPX5_CORE_CLOCK_328MHZ,   /* 328MHZ core clock frequency */
	SPX5_CORE_CLOCK_500MHZ,   /* 500MHZ core clock frequency */
	SPX5_CORE_CLOCK_625MHZ,   /* 625MHZ core clock frequency */
};

struct sparx5_phc {
	struct ptp_clock *clock;
	struct ptp_clock_info info;
	struct kernel_hwtstamp_config hwtstamp_config;
	struct sparx5 *sparx5;
	u8 index;
};

struct sparx5_skb_cb {
	u8 rew_op;
	u8 pdu_type;
	u8 pdu_w16_offset;
	u16 ts_id;
	unsigned long jiffies;
};

struct sparx5_mdb_entry {
	struct list_head list;
	DECLARE_BITMAP(port_mask, SPX5_PORTS);
	unsigned char addr[ETH_ALEN];
	bool cpu_copy;
	u16 vid;
	u16 pgid_idx;
};

struct sparx5_mall_mirror_entry {
	u32 idx;
	struct sparx5_port *port;
};

struct sparx5_mall_entry {
	struct list_head list;
	struct sparx5_port *port;
	unsigned long cookie;
	enum flow_action_id type;
	bool ingress;
	union {
		struct sparx5_mall_mirror_entry mirror;
	};
};

#define SPARX5_PTP_TIMEOUT		msecs_to_jiffies(10)
#define SPARX5_SKB_CB(skb) \
	((struct sparx5_skb_cb *)((skb)->cb))

struct sparx5_regs {
	const unsigned int *tsize;
	const unsigned int *gaddr;
	const unsigned int *gcnt;
	const unsigned int *gsize;
	const unsigned int *raddr;
	const unsigned int *rcnt;
	const unsigned int *fpos;
	const unsigned int *fsize;
};

struct sparx5_consts {
	u32 n_ports;             /* Number of front ports */
	u32 n_ports_all;         /* Number of front ports + internal ports */
	u32 n_hsch_l1_elems;     /* Number of HSCH layer 1 elements */
	u32 n_hsch_queues;       /* Number of HSCH queues */
	u32 n_lb_groups;         /* Number of leacky bucket groupd */
	u32 n_pgids;             /* Number of PGID's */
	u32 n_sio_clks;          /* Number of serial IO clocks */
	u32 n_own_upsids;        /* Number of own UPSID's */
	u32 n_auto_cals;         /* Number of auto calendars */
	u32 n_filters;           /* Number of PSFP filters */
	u32 n_gates;             /* Number of PSFP gates */
	u32 n_sdlbs;             /* Number of service dual leaky buckets */
	u32 n_dsm_cal_taxis;     /* Number of DSM calendar taxis */
	u32 buf_size;            /* Amount of QLIM watermark memory */
	u32 qres_max_prio_idx;   /* Maximum QRES prio index */
	u32 qres_max_colour_idx; /* Maximum QRES colour index */
	u32 tod_pin;             /* PTP TOD pin */
	const struct sparx5_vcap_inst *vcaps_cfg;
	const struct vcap_info *vcaps;
	const struct vcap_statistics *vcap_stats;
};

struct sparx5_ops {
	bool (*is_port_2g5)(int portno);
	bool (*is_port_5g)(int portno);
	bool (*is_port_10g)(int portno);
	bool (*is_port_25g)(int portno);
	u32  (*get_port_dev_index)(struct sparx5 *sparx5, int port);
	u32  (*get_port_dev_bit)(struct sparx5 *sparx5, int port);
	u32  (*get_hsch_max_group_rate)(int grp);
	struct sparx5_sdlb_group *(*get_sdlb_group)(int idx);
	int (*set_port_mux)(struct sparx5 *sparx5, struct sparx5_port *port,
			    struct sparx5_port_config *conf);

	irqreturn_t (*ptp_irq_handler)(int irq, void *args);
	int (*dsm_calendar_calc)(struct sparx5 *sparx5, u32 taxi,
				 struct sparx5_calendar_data *data);
};

struct sparx5_main_io_resource {
	enum sparx5_target id;
	phys_addr_t offset;
	int range;
};

struct sparx5_match_data {
	const struct sparx5_regs *regs;
	const struct sparx5_consts *consts;
	const struct sparx5_ops *ops;
	const struct sparx5_main_io_resource *iomap;
	int ioranges;
	int iomap_size;
};

struct sparx5 {
	struct platform_device *pdev;
	struct device *dev;
	u32 chip_id;
	enum spx5_target_chiptype target_ct;
	u32 features;
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
	/* SW MDB table */
	struct list_head mdb_entries;
	/* mdb list mutex */
	struct mutex mdb_lock;
	struct delayed_work mact_work;
	struct workqueue_struct *mact_queue;
	/* Board specifics */
	bool sd_sgpio_remapping;
	/* Register based inj/xtr */
	int xtr_irq;
	/* Frame DMA */
	int fdma_irq;
	spinlock_t tx_lock; /* lock for frame transmission */
	struct sparx5_rx rx;
	struct sparx5_tx tx;
	/* PTP */
	bool ptp;
	struct sparx5_phc phc[SPARX5_PHC_COUNT];
	spinlock_t ptp_clock_lock; /* lock for phc */
	spinlock_t ptp_ts_id_lock; /* lock for ts_id */
	struct mutex ptp_lock; /* lock for ptp interface state */
	u16 ptp_skbs;
	int ptp_irq;
	/* VCAP */
	struct vcap_control *vcap_ctrl;
	/* PGID allocation map */
	u8 pgid_map[PGID_TABLE_SIZE];
	struct list_head mall_entries;
	/* Common root for debugfs */
	struct dentry *debugfs_root;
	const struct sparx5_match_data *data;
};

/* sparx5_main.c */
bool is_sparx5(struct sparx5 *sparx5);
bool sparx5_has_feature(struct sparx5 *sparx5, enum sparx5_feature feature);

/* sparx5_switchdev.c */
int sparx5_register_notifier_blocks(struct sparx5 *sparx5);
void sparx5_unregister_notifier_blocks(struct sparx5 *sparx5);

/* sparx5_packet.c */
struct frame_info {
	int src_port;
	u32 timestamp;
};

void sparx5_xtr_flush(struct sparx5 *sparx5, u8 grp);
void sparx5_ifh_parse(struct sparx5 *sparx5, u32 *ifh, struct frame_info *info);
irqreturn_t sparx5_xtr_handler(int irq, void *_priv);
netdev_tx_t sparx5_port_xmit_impl(struct sk_buff *skb, struct net_device *dev);
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
int sparx5_mact_find(struct sparx5 *sparx5,
		     const unsigned char mac[ETH_ALEN], u16 vid, u32 *pcfg2);
int sparx5_mact_forget(struct sparx5 *sparx5,
		       const unsigned char mac[ETH_ALEN], u16 vid);
int sparx5_add_mact_entry(struct sparx5 *sparx5,
			  struct net_device *dev,
			  u16 portno,
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
void sparx5_pgid_clear(struct sparx5 *spx5, int pgid);
void sparx5_pgid_read_mask(struct sparx5 *sparx5, int pgid, u32 portmask[3]);
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
int sparx5_dsm_calendar_calc(struct sparx5 *sparx5, u32 taxi,
			     struct sparx5_calendar_data *data);
u32 sparx5_cal_speed_to_value(enum sparx5_cal_bw speed);
enum sparx5_cal_bw sparx5_get_port_cal_speed(struct sparx5 *sparx5, u32 portno);


/* sparx5_ethtool.c */
void sparx5_get_stats64(struct net_device *ndev, struct rtnl_link_stats64 *stats);
int sparx_stats_init(struct sparx5 *sparx5);

/* sparx5_dcb.c */
#ifdef CONFIG_SPARX5_DCB
int sparx5_dcb_init(struct sparx5 *sparx5);
#else
static inline int sparx5_dcb_init(struct sparx5 *sparx5)
{
	return 0;
}
#endif

/* sparx5_netdev.c */
void sparx5_set_port_ifh_timestamp(struct sparx5 *sparx5, void *ifh_hdr,
				   u64 timestamp);
void sparx5_set_port_ifh_rew_op(void *ifh_hdr, u32 rew_op);
void sparx5_set_port_ifh_pdu_type(struct sparx5 *sparx5, void *ifh_hdr,
				  u32 pdu_type);
void sparx5_set_port_ifh_pdu_w16_offset(struct sparx5 *sparx5, void *ifh_hdr,
					u32 pdu_w16_offset);
void sparx5_set_port_ifh(struct sparx5 *sparx5, void *ifh_hdr, u16 portno);
bool sparx5_netdevice_check(const struct net_device *dev);
struct net_device *sparx5_create_netdev(struct sparx5 *sparx5, u32 portno);
int sparx5_register_netdevs(struct sparx5 *sparx5);
void sparx5_destroy_netdevs(struct sparx5 *sparx5);
void sparx5_unregister_netdevs(struct sparx5 *sparx5);

/* sparx5_ptp.c */
int sparx5_ptp_init(struct sparx5 *sparx5);
void sparx5_ptp_deinit(struct sparx5 *sparx5);
int sparx5_ptp_hwtstamp_set(struct sparx5_port *port,
			    struct kernel_hwtstamp_config *cfg,
			    struct netlink_ext_ack *extack);
void sparx5_ptp_hwtstamp_get(struct sparx5_port *port,
			     struct kernel_hwtstamp_config *cfg);
void sparx5_ptp_rxtstamp(struct sparx5 *sparx5, struct sk_buff *skb,
			 u64 timestamp);
int sparx5_ptp_txtstamp_request(struct sparx5_port *port,
				struct sk_buff *skb);
void sparx5_ptp_txtstamp_release(struct sparx5_port *port,
				 struct sk_buff *skb);
irqreturn_t sparx5_ptp_irq_handler(int irq, void *args);
int sparx5_ptp_gettime64(struct ptp_clock_info *ptp, struct timespec64 *ts);
void sparx5_get_hwtimestamp(struct sparx5 *sparx5,
			    struct timespec64 *ts,
			    u32 nsec);

/* sparx5_vcap_impl.c */
int sparx5_vcap_init(struct sparx5 *sparx5);
void sparx5_vcap_destroy(struct sparx5 *sparx5);

/* sparx5_pgid.c */
enum sparx5_pgid_type {
	SPX5_PGID_FREE,
	SPX5_PGID_RESERVED,
	SPX5_PGID_MULTICAST,
};

void sparx5_pgid_init(struct sparx5 *spx5);
int sparx5_pgid_alloc_mcast(struct sparx5 *spx5, u16 *idx);
int sparx5_pgid_free(struct sparx5 *spx5, u16 idx);
int sparx5_get_pgid(struct sparx5 *sparx5, int pgid);

/* sparx5_pool.c */
struct sparx5_pool_entry {
	u16 ref_cnt;
	u32 idx; /* tc index */
};

u32 sparx5_pool_idx_to_id(u32 idx);
int sparx5_pool_put(struct sparx5_pool_entry *pool, int size, u32 id);
int sparx5_pool_get(struct sparx5_pool_entry *pool, int size, u32 *id);
int sparx5_pool_get_with_idx(struct sparx5_pool_entry *pool, int size, u32 idx,
			     u32 *id);

/* sparx5_port.c */
int sparx5_port_mux_set(struct sparx5 *sparx5, struct sparx5_port *port,
			struct sparx5_port_config *conf);
int sparx5_get_internal_port(struct sparx5 *sparx5, int port);

/* sparx5_sdlb.c */
#define SPX5_SDLB_PUP_TOKEN_DISABLE 0x1FFF
#define SPX5_SDLB_PUP_TOKEN_MAX (SPX5_SDLB_PUP_TOKEN_DISABLE - 1)
#define SPX5_SDLB_GROUP_RATE_MAX 25000000000ULL
#define SPX5_SDLB_2CYCLES_TYPE2_THRES_OFFSET 13
#define SPX5_SDLB_CNT 4096
#define SPX5_SDLB_GROUP_CNT 10
#define SPX5_CLK_PER_100PS_DEFAULT 16

struct sparx5_sdlb_group {
	u64 max_rate;
	u32 min_burst;
	u32 frame_size;
	u32 pup_interval;
	u32 nsets;
};

extern struct sparx5_sdlb_group sdlb_groups[SPX5_SDLB_GROUP_CNT];
struct sparx5_sdlb_group *sparx5_get_sdlb_group(int idx);
int sparx5_sdlb_pup_token_get(struct sparx5 *sparx5, u32 pup_interval,
			      u64 rate);

u64 sparx5_sdlb_clk_hz_get(struct sparx5 *sparx5);
int sparx5_sdlb_group_get_by_rate(struct sparx5 *sparx5, u32 rate, u32 burst);
int sparx5_sdlb_group_get_by_index(struct sparx5 *sparx5, u32 idx, u32 *group);

int sparx5_sdlb_group_add(struct sparx5 *sparx5, u32 group, u32 idx);
int sparx5_sdlb_group_del(struct sparx5 *sparx5, u32 group, u32 idx);

void sparx5_sdlb_group_init(struct sparx5 *sparx5, u64 max_rate, u32 min_burst,
			    u32 frame_size, u32 idx);

/* sparx5_police.c */
enum {
	/* More policer types will be added later */
	SPX5_POL_SERVICE
};

struct sparx5_policer {
	u32 type;
	u32 idx;
	u64 rate;
	u32 burst;
	u32 group;
	u8 event_mask;
};

int sparx5_policer_conf_set(struct sparx5 *sparx5, struct sparx5_policer *pol);

/* sparx5_psfp.c */
#define SPX5_PSFP_GCE_CNT 4
#define SPX5_PSFP_SG_CNT 1024
#define SPX5_PSFP_SG_MIN_CYCLE_TIME_NS (1 * NSEC_PER_USEC)
#define SPX5_PSFP_SG_MAX_CYCLE_TIME_NS ((1 * NSEC_PER_SEC) - 1)
#define SPX5_PSFP_SG_MAX_IPV (SPX5_PRIOS - 1)
#define SPX5_PSFP_SG_OPEN (SPX5_PSFP_SG_CNT - 1)
#define SPX5_PSFP_SG_CYCLE_TIME_DEFAULT 1000000
#define SPX5_PSFP_SF_MAX_SDU 16383

struct sparx5_psfp_fm {
	struct sparx5_policer pol;
};

struct sparx5_psfp_gce {
	bool gate_state;            /* StreamGateState */
	u32 interval;               /* TimeInterval */
	u32 ipv;                    /* InternalPriorityValue */
	u32 maxoctets;              /* IntervalOctetMax */
};

struct sparx5_psfp_sg {
	bool gate_state;            /* PSFPAdminGateStates */
	bool gate_enabled;          /* PSFPGateEnabled */
	u32 ipv;                    /* PSFPAdminIPV */
	struct timespec64 basetime; /* PSFPAdminBaseTime */
	u32 cycletime;              /* PSFPAdminCycleTime */
	u32 cycletimeext;           /* PSFPAdminCycleTimeExtension */
	u32 num_entries;            /* PSFPAdminControlListLength */
	struct sparx5_psfp_gce gce[SPX5_PSFP_GCE_CNT];
};

struct sparx5_psfp_sf {
	bool sblock_osize_ena;
	bool sblock_osize;
	u32 max_sdu;
	u32 sgid; /* Gate id */
	u32 fmid; /* Flow meter id */
};

int sparx5_psfp_fm_add(struct sparx5 *sparx5, u32 uidx,
		       struct sparx5_psfp_fm *fm, u32 *id);
int sparx5_psfp_fm_del(struct sparx5 *sparx5, u32 id);

int sparx5_psfp_sg_add(struct sparx5 *sparx5, u32 uidx,
		       struct sparx5_psfp_sg *sg, u32 *id);
int sparx5_psfp_sg_del(struct sparx5 *sparx5, u32 id);

int sparx5_psfp_sf_add(struct sparx5 *sparx5, const struct sparx5_psfp_sf *sf,
		       u32 *id);
int sparx5_psfp_sf_del(struct sparx5 *sparx5, u32 id);

u32 sparx5_psfp_isdx_get_sf(struct sparx5 *sparx5, u32 isdx);
u32 sparx5_psfp_isdx_get_fm(struct sparx5 *sparx5, u32 isdx);
u32 sparx5_psfp_sf_get_sg(struct sparx5 *sparx5, u32 sfid);
void sparx5_isdx_conf_set(struct sparx5 *sparx5, u32 isdx, u32 sfid, u32 fmid);

void sparx5_psfp_init(struct sparx5 *sparx5);

/* sparx5_qos.c */
void sparx5_new_base_time(struct sparx5 *sparx5, const u32 cycle_time,
			  const ktime_t org_base_time, ktime_t *new_base_time);

/* sparx5_mirror.c */
int sparx5_mirror_add(struct sparx5_mall_entry *entry);
void sparx5_mirror_del(struct sparx5_mall_entry *entry);
void sparx5_mirror_stats(struct sparx5_mall_entry *entry,
			 struct flow_stats *fstats);

/* Clock period in picoseconds */
static inline u32 sparx5_clk_period(enum sparx5_core_clockfreq cclock)
{
	switch (cclock) {
	case SPX5_CORE_CLOCK_250MHZ:
		return 4000;
	case SPX5_CORE_CLOCK_328MHZ:
		return 3048;
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
extern const struct dcbnl_rtnl_ops sparx5_dcbnl_ops;

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
