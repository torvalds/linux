/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __LAN966X_MAIN_H__
#define __LAN966X_MAIN_H__

#include <linux/debugfs.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/jiffies.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <linux/ptp_clock_kernel.h>
#include <net/page_pool/types.h>
#include <net/pkt_cls.h>
#include <net/pkt_sched.h>
#include <net/switchdev.h>
#include <net/xdp.h>

#include <fdma_api.h>
#include <vcap_api.h>
#include <vcap_api_client.h>

#include "lan966x_regs.h"
#include "lan966x_ifh.h"

#define TABLE_UPDATE_SLEEP_US		10
#define TABLE_UPDATE_TIMEOUT_US		100000

#define READL_SLEEP_US			10
#define READL_TIMEOUT_US		100000000

#define LAN966X_BUFFER_CELL_SZ		64
#define LAN966X_BUFFER_MEMORY		(160 * 1024)
#define LAN966X_BUFFER_MIN_SZ		60

#define LAN966X_HW_MTU(mtu)		((mtu) + ETH_HLEN + ETH_FCS_LEN)

#define PGID_AGGR			64
#define PGID_SRC			80
#define PGID_ENTRIES			89

#define UNAWARE_PVID			0
#define HOST_PVID			4095

/* Reserved amount for (SRC, PRIO) at index 8*SRC + PRIO */
#define QSYS_Q_RSRV			95

#define NUM_PHYS_PORTS			8
#define CPU_PORT			8
#define NUM_PRIO_QUEUES			8

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

#define FDMA_XTR_CHANNEL		6
#define FDMA_INJ_CHANNEL		0
#define FDMA_DCB_MAX			512

#define SE_IDX_QUEUE			0  /* 0-79 : Queue scheduler elements */
#define SE_IDX_PORT			80 /* 80-89 : Port schedular elements */

#define LAN966X_VCAP_CID_IS1_L0 VCAP_CID_INGRESS_L0 /* IS1 lookup 0 */
#define LAN966X_VCAP_CID_IS1_L1 VCAP_CID_INGRESS_L1 /* IS1 lookup 1 */
#define LAN966X_VCAP_CID_IS1_L2 VCAP_CID_INGRESS_L2 /* IS1 lookup 2 */
#define LAN966X_VCAP_CID_IS1_MAX (VCAP_CID_INGRESS_L3 - 1) /* IS1 Max */

#define LAN966X_VCAP_CID_IS2_L0 VCAP_CID_INGRESS_STAGE2_L0 /* IS2 lookup 0 */
#define LAN966X_VCAP_CID_IS2_L1 VCAP_CID_INGRESS_STAGE2_L1 /* IS2 lookup 1 */
#define LAN966X_VCAP_CID_IS2_MAX (VCAP_CID_INGRESS_STAGE2_L2 - 1) /* IS2 Max */

#define LAN966X_VCAP_CID_ES0_L0 VCAP_CID_EGRESS_L0 /* ES0 lookup 0 */
#define LAN966X_VCAP_CID_ES0_MAX (VCAP_CID_EGRESS_L1 - 1) /* ES0 Max */

#define LAN966X_PORT_QOS_PCP_COUNT	8
#define LAN966X_PORT_QOS_DEI_COUNT	8
#define LAN966X_PORT_QOS_PCP_DEI_COUNT \
	(LAN966X_PORT_QOS_PCP_COUNT + LAN966X_PORT_QOS_DEI_COUNT)

#define LAN966X_PORT_QOS_DSCP_COUNT	64

/* Port PCP rewrite mode */
#define LAN966X_PORT_REW_TAG_CTRL_CLASSIFIED	0
#define LAN966X_PORT_REW_TAG_CTRL_MAPPED	2

/* Port DSCP rewrite mode */
#define LAN966X_PORT_REW_DSCP_FRAME		0
#define LAN966X_PORT_REW_DSCP_ANALIZER		1
#define LAN966X_PORT_QOS_REWR_DSCP_ALL		3

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

/* FDMA return action codes for checking if the frame is valid
 * FDMA_PASS, frame is valid and can be used
 * FDMA_ERROR, something went wrong, stop getting more frames
 * FDMA_DROP, frame is dropped, but continue to get more frames
 * FDMA_TX, frame is given to TX, but continue to get more frames
 * FDMA_REDIRECT, frame is given to TX, but continue to get more frames
 */
enum lan966x_fdma_action {
	FDMA_PASS = 0,
	FDMA_ERROR,
	FDMA_DROP,
	FDMA_TX,
	FDMA_REDIRECT,
};

/* Controls how PORT_MASK is applied */
enum LAN966X_PORT_MASK_MODE {
	LAN966X_PMM_NO_ACTION,
	LAN966X_PMM_REPLACE,
	LAN966X_PMM_FORWARDING,
	LAN966X_PMM_REDIRECT,
};

enum vcap_is2_port_sel_ipv6 {
	VCAP_IS2_PS_IPV6_TCPUDP_OTHER,
	VCAP_IS2_PS_IPV6_STD,
	VCAP_IS2_PS_IPV6_IP4_TCPUDP_IP4_OTHER,
	VCAP_IS2_PS_IPV6_MAC_ETYPE,
};

enum vcap_is1_port_sel_other {
	VCAP_IS1_PS_OTHER_NORMAL,
	VCAP_IS1_PS_OTHER_7TUPLE,
	VCAP_IS1_PS_OTHER_DBL_VID,
	VCAP_IS1_PS_OTHER_DMAC_VID,
};

enum vcap_is1_port_sel_ipv4 {
	VCAP_IS1_PS_IPV4_NORMAL,
	VCAP_IS1_PS_IPV4_7TUPLE,
	VCAP_IS1_PS_IPV4_5TUPLE_IP4,
	VCAP_IS1_PS_IPV4_DBL_VID,
	VCAP_IS1_PS_IPV4_DMAC_VID,
};

enum vcap_is1_port_sel_ipv6 {
	VCAP_IS1_PS_IPV6_NORMAL,
	VCAP_IS1_PS_IPV6_7TUPLE,
	VCAP_IS1_PS_IPV6_5TUPLE_IP4,
	VCAP_IS1_PS_IPV6_NORMAL_IP6,
	VCAP_IS1_PS_IPV6_5TUPLE_IP6,
	VCAP_IS1_PS_IPV6_DBL_VID,
	VCAP_IS1_PS_IPV6_DMAC_VID,
};

enum vcap_is1_port_sel_rt {
	VCAP_IS1_PS_RT_NORMAL,
	VCAP_IS1_PS_RT_7TUPLE,
	VCAP_IS1_PS_RT_DBL_VID,
	VCAP_IS1_PS_RT_DMAC_VID,
	VCAP_IS1_PS_RT_FOLLOW_OTHER = 7,
};

struct lan966x_port;

struct lan966x_rx {
	struct lan966x *lan966x;

	struct fdma fdma;

	/* For each DB, there is a page */
	struct page *page[FDMA_DCB_MAX][FDMA_RX_DCB_MAX_DBS];

	/* Represents the page order that is used to allocate the pages for the
	 * RX buffers. This value is calculated based on max MTU of the devices.
	 */
	u8 page_order;

	/* Represents the max size frame that it can receive to the CPU. This
	 * includes the IFH + VLAN tags + frame + skb_shared_info
	 */
	u32 max_mtu;

	struct page_pool *page_pool;
};

struct lan966x_tx_dcb_buf {
	dma_addr_t dma_addr;
	struct net_device *dev;
	union {
		struct sk_buff *skb;
		struct xdp_frame *xdpf;
		struct page *page;
	} data;
	u32 len;
	u32 used : 1;
	u32 ptp : 1;
	u32 use_skb : 1;
	u32 xdp_ndo : 1;
};

struct lan966x_tx {
	struct lan966x *lan966x;

	struct fdma fdma;

	/* Array of dcbs that are given to the HW */
	struct lan966x_tx_dcb_buf *dcbs_buf;

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
	struct kernel_hwtstamp_config hwtstamp_config;
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

	spinlock_t tx_lock; /* lock for frame transmission */

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

	/* Mirror */
	struct lan966x_port *mirror_monitor;
	u32 mirror_mask[2];
	u32 mirror_count;

	/* vcap */
	struct vcap_control *vcap_ctrl;

	/* debugfs */
	struct dentry *debugfs_root;
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

struct lan966x_port_tc {
	bool ingress_shared_block;
	unsigned long police_id;
	unsigned long ingress_mirror_id;
	unsigned long egress_mirror_id;
	struct flow_stats police_stat;
	struct flow_stats mirror_stat;
};

struct lan966x_port_qos_pcp {
	u8 map[LAN966X_PORT_QOS_PCP_DEI_COUNT];
	bool enable;
};

struct lan966x_port_qos_dscp {
	u8 map[LAN966X_PORT_QOS_DSCP_COUNT];
	bool enable;
};

struct lan966x_port_qos_pcp_rewr {
	u16 map[NUM_PRIO_QUEUES];
	bool enable;
};

struct lan966x_port_qos_dscp_rewr {
	u16 map[LAN966X_PORT_QOS_DSCP_COUNT];
	bool enable;
};

struct lan966x_port_qos {
	struct lan966x_port_qos_pcp pcp;
	struct lan966x_port_qos_dscp dscp;
	struct lan966x_port_qos_pcp_rewr pcp_rewr;
	struct lan966x_port_qos_dscp_rewr dscp_rewr;
	u8 default_prio;
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

	u8 ptp_tx_cmd;
	bool ptp_rx_cmd;
	u16 ts_id;
	struct sk_buff_head tx_skbs;

	struct net_device *bond;
	bool lag_tx_active;
	enum netdev_lag_hash hash_type;

	struct lan966x_port_tc tc;

	struct bpf_prog *xdp_prog;
	struct xdp_rxq_info xdp_rxq;
};

extern const struct phylink_mac_ops lan966x_phylink_mac_ops;
extern const struct phylink_pcs_ops lan966x_phylink_pcs_ops;
extern const struct ethtool_ops lan966x_ethtool_ops;
extern struct notifier_block lan966x_switchdev_nb __read_mostly;
extern struct notifier_block lan966x_switchdev_blocking_nb __read_mostly;

bool lan966x_netdevice_check(const struct net_device *dev);

void lan966x_register_notifier_blocks(void);
void lan966x_unregister_notifier_blocks(void);

bool lan966x_hw_offload(struct lan966x *lan966x, u32 port, struct sk_buff *skb);

void lan966x_ifh_get_src_port(void *ifh, u64 *src_port);
void lan966x_ifh_get_timestamp(void *ifh, u64 *timestamp);
void lan966x_ifh_set_bypass(void *ifh, u64 bypass);
void lan966x_ifh_set_port(void *ifh, u64 bypass);

void lan966x_stats_get(struct net_device *dev,
		       struct rtnl_link_stats64 *stats);
int lan966x_stats_init(struct lan966x *lan966x);

void lan966x_port_config_down(struct lan966x_port *port);
void lan966x_port_config_up(struct lan966x_port *port);
void lan966x_port_status_get(struct lan966x_port *port, unsigned int neg_mode,
			     struct phylink_link_state *state);
int lan966x_port_pcs_set(struct lan966x_port *port,
			 struct lan966x_port_config *config);
void lan966x_port_init(struct lan966x_port *port);

void lan966x_port_qos_set(struct lan966x_port *port,
			  struct lan966x_port_qos *qos);
void lan966x_port_qos_dscp_rewr_mode_set(struct lan966x_port *port,
					 int mode);

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
void lan966x_mac_lag_replace_port_entry(struct lan966x *lan966x,
					struct lan966x_port *src,
					struct lan966x_port *dst);
void lan966x_mac_lag_remove_port_entry(struct lan966x *lan966x,
				       struct lan966x_port *src);
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
void lan966x_fdb_flush_workqueue(struct lan966x *lan966x);
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
int lan966x_ptp_hwtstamp_set(struct lan966x_port *port,
			     struct kernel_hwtstamp_config *cfg,
			     struct netlink_ext_ack *extack);
void lan966x_ptp_hwtstamp_get(struct lan966x_port *port,
			      struct kernel_hwtstamp_config *cfg);
void lan966x_ptp_rxtstamp(struct lan966x *lan966x, struct sk_buff *skb,
			  u64 src_port, u64 timestamp);
int lan966x_ptp_txtstamp_request(struct lan966x_port *port,
				 struct sk_buff *skb);
void lan966x_ptp_txtstamp_release(struct lan966x_port *port,
				  struct sk_buff *skb);
irqreturn_t lan966x_ptp_irq_handler(int irq, void *args);
irqreturn_t lan966x_ptp_ext_irq_handler(int irq, void *args);
u32 lan966x_ptp_get_period_ps(void);
int lan966x_ptp_gettime64(struct ptp_clock_info *ptp, struct timespec64 *ts);
int lan966x_ptp_setup_traps(struct lan966x_port *port,
			    struct kernel_hwtstamp_config *cfg);
int lan966x_ptp_del_traps(struct lan966x_port *port);

int lan966x_fdma_xmit(struct sk_buff *skb, __be32 *ifh, struct net_device *dev);
int lan966x_fdma_xmit_xdpf(struct lan966x_port *port, void *ptr, u32 len);
int lan966x_fdma_change_mtu(struct lan966x *lan966x);
void lan966x_fdma_netdev_init(struct lan966x *lan966x, struct net_device *dev);
void lan966x_fdma_netdev_deinit(struct lan966x *lan966x, struct net_device *dev);
int lan966x_fdma_init(struct lan966x *lan966x);
void lan966x_fdma_deinit(struct lan966x *lan966x);
irqreturn_t lan966x_fdma_irq_handler(int irq, void *args);
int lan966x_fdma_reload_page_pool(struct lan966x *lan966x);

int lan966x_lag_port_join(struct lan966x_port *port,
			  struct net_device *brport_dev,
			  struct net_device *bond,
			  struct netlink_ext_ack *extack);
void lan966x_lag_port_leave(struct lan966x_port *port, struct net_device *bond);
int lan966x_lag_port_prechangeupper(struct net_device *dev,
				    struct netdev_notifier_changeupper_info *info);
int lan966x_lag_port_changelowerstate(struct net_device *dev,
				      struct netdev_notifier_changelowerstate_info *info);
int lan966x_lag_netdev_prechangeupper(struct net_device *dev,
				      struct netdev_notifier_changeupper_info *info);
int lan966x_lag_netdev_changeupper(struct net_device *dev,
				   struct netdev_notifier_changeupper_info *info);
bool lan966x_lag_first_port(struct net_device *lag, struct net_device *dev);
u32 lan966x_lag_get_mask(struct lan966x *lan966x, struct net_device *bond);

int lan966x_port_changeupper(struct net_device *dev,
			     struct net_device *brport_dev,
			     struct netdev_notifier_changeupper_info *info);
int lan966x_port_prechangeupper(struct net_device *dev,
				struct net_device *brport_dev,
				struct netdev_notifier_changeupper_info *info);
void lan966x_port_stp_state_set(struct lan966x_port *port, u8 state);
void lan966x_port_ageing_set(struct lan966x_port *port,
			     unsigned long ageing_clock_t);
void lan966x_update_fwd_mask(struct lan966x *lan966x);

int lan966x_tc_setup(struct net_device *dev, enum tc_setup_type type,
		     void *type_data);

int lan966x_mqprio_add(struct lan966x_port *port, u8 num_tc);
int lan966x_mqprio_del(struct lan966x_port *port);

void lan966x_taprio_init(struct lan966x *lan966x);
void lan966x_taprio_deinit(struct lan966x *lan966x);
int lan966x_taprio_add(struct lan966x_port *port,
		       struct tc_taprio_qopt_offload *qopt);
int lan966x_taprio_del(struct lan966x_port *port);
int lan966x_taprio_speed_set(struct lan966x_port *port, int speed);

int lan966x_tbf_add(struct lan966x_port *port,
		    struct tc_tbf_qopt_offload *qopt);
int lan966x_tbf_del(struct lan966x_port *port,
		    struct tc_tbf_qopt_offload *qopt);

int lan966x_cbs_add(struct lan966x_port *port,
		    struct tc_cbs_qopt_offload *qopt);
int lan966x_cbs_del(struct lan966x_port *port,
		    struct tc_cbs_qopt_offload *qopt);

int lan966x_ets_add(struct lan966x_port *port,
		    struct tc_ets_qopt_offload *qopt);
int lan966x_ets_del(struct lan966x_port *port,
		    struct tc_ets_qopt_offload *qopt);

int lan966x_tc_matchall(struct lan966x_port *port,
			struct tc_cls_matchall_offload *f,
			bool ingress);

int lan966x_police_port_add(struct lan966x_port *port,
			    struct flow_action *action,
			    struct flow_action_entry *act,
			    unsigned long police_id,
			    bool ingress,
			    struct netlink_ext_ack *extack);
int lan966x_police_port_del(struct lan966x_port *port,
			    unsigned long police_id,
			    struct netlink_ext_ack *extack);
void lan966x_police_port_stats(struct lan966x_port *port,
			       struct flow_stats *stats);

int lan966x_mirror_port_add(struct lan966x_port *port,
			    struct flow_action_entry *action,
			    unsigned long mirror_id,
			    bool ingress,
			    struct netlink_ext_ack *extack);
int lan966x_mirror_port_del(struct lan966x_port *port,
			    bool ingress,
			    struct netlink_ext_ack *extack);
void lan966x_mirror_port_stats(struct lan966x_port *port,
			       struct flow_stats *stats,
			       bool ingress);

int lan966x_xdp_port_init(struct lan966x_port *port);
void lan966x_xdp_port_deinit(struct lan966x_port *port);
int lan966x_xdp(struct net_device *dev, struct netdev_bpf *xdp);
int lan966x_xdp_run(struct lan966x_port *port,
		    struct page *page,
		    u32 data_len);
int lan966x_xdp_xmit(struct net_device *dev,
		     int n,
		     struct xdp_frame **frames,
		     u32 flags);
bool lan966x_xdp_present(struct lan966x *lan966x);
static inline bool lan966x_xdp_port_present(struct lan966x_port *port)
{
	return !!port->xdp_prog;
}

int lan966x_vcap_init(struct lan966x *lan966x);
void lan966x_vcap_deinit(struct lan966x *lan966x);
#if defined(CONFIG_DEBUG_FS)
int lan966x_vcap_port_info(struct net_device *dev,
			   struct vcap_admin *admin,
			   struct vcap_output_print *out);
#else
static inline int lan966x_vcap_port_info(struct net_device *dev,
					 struct vcap_admin *admin,
					 struct vcap_output_print *out)
{
	return 0;
}
#endif

int lan966x_tc_flower(struct lan966x_port *port,
		      struct flow_cls_offload *f,
		      bool ingress);

int lan966x_goto_port_add(struct lan966x_port *port,
			  int from_cid, int to_cid,
			  unsigned long goto_id,
			  struct netlink_ext_ack *extack);
int lan966x_goto_port_del(struct lan966x_port *port,
			  unsigned long goto_id,
			  struct netlink_ext_ack *extack);

#ifdef CONFIG_LAN966X_DCB
void lan966x_dcb_init(struct lan966x *lan966x);
#else
static inline void lan966x_dcb_init(struct lan966x *lan966x)
{
}
#endif

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
