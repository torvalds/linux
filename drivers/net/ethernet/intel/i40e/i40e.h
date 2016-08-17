/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
 * Copyright(c) 2013 - 2016 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#ifndef _I40E_H_
#define _I40E_H_

#include <net/tcp.h>
#include <net/udp.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/netdevice.h>
#include <linux/ioport.h>
#include <linux/iommu.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/sctp.h>
#include <linux/pkt_sched.h>
#include <linux/ipv6.h>
#include <net/checksum.h>
#include <net/ip6_checksum.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/if_bridge.h>
#include <linux/clocksource.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_clock_kernel.h>
#include "i40e_type.h"
#include "i40e_prototype.h"
#ifdef I40E_FCOE
#include "i40e_fcoe.h"
#endif
#include "i40e_client.h"
#include "i40e_virtchnl.h"
#include "i40e_virtchnl_pf.h"
#include "i40e_txrx.h"
#include "i40e_dcb.h"

/* Useful i40e defaults */
#define I40E_MAX_VEB          16

#define I40E_MAX_NUM_DESCRIPTORS      4096
#define I40E_MAX_CSR_SPACE (4 * 1024 * 1024 - 64 * 1024)
#define I40E_DEFAULT_NUM_DESCRIPTORS  512
#define I40E_REQ_DESCRIPTOR_MULTIPLE  32
#define I40E_MIN_NUM_DESCRIPTORS      64
#define I40E_MIN_MSIX                 2
#define I40E_DEFAULT_NUM_VMDQ_VSI     8 /* max 256 VSIs */
#define I40E_MIN_VSI_ALLOC            51 /* LAN, ATR, FCOE, 32 VF, 16 VMDQ */
/* max 16 qps */
#define i40e_default_queues_per_vmdq(pf) \
		(((pf)->flags & I40E_FLAG_RSS_AQ_CAPABLE) ? 4 : 1)
#define I40E_DEFAULT_QUEUES_PER_VF    4
#define I40E_DEFAULT_QUEUES_PER_TC    1 /* should be a power of 2 */
#define i40e_pf_get_max_q_per_tc(pf) \
		(((pf)->flags & I40E_FLAG_128_QP_RSS_CAPABLE) ? 128 : 64)
#define I40E_FDIR_RING                0
#define I40E_FDIR_RING_COUNT          32
#ifdef I40E_FCOE
#define I40E_DEFAULT_FCOE             8 /* default number of QPs for FCoE */
#define I40E_MINIMUM_FCOE             1 /* minimum number of QPs for FCoE */
#endif /* I40E_FCOE */
#define I40E_MAX_AQ_BUF_SIZE          4096
#define I40E_AQ_LEN                   256
#define I40E_AQ_WORK_LIMIT            66 /* max number of VFs + a little */
#define I40E_MAX_USER_PRIORITY        8
#define I40E_DEFAULT_MSG_ENABLE       4
#define I40E_QUEUE_WAIT_RETRY_LIMIT   10
#define I40E_INT_NAME_STR_LEN        (IFNAMSIZ + 16)

/* Ethtool Private Flags */
#define	I40E_PRIV_FLAGS_MFP_FLAG		BIT(0)
#define	I40E_PRIV_FLAGS_LINKPOLL_FLAG		BIT(1)
#define I40E_PRIV_FLAGS_FD_ATR			BIT(2)
#define I40E_PRIV_FLAGS_VEB_STATS		BIT(3)
#define I40E_PRIV_FLAGS_HW_ATR_EVICT		BIT(4)
#define I40E_PRIV_FLAGS_TRUE_PROMISC_SUPPORT	BIT(5)

#define I40E_NVM_VERSION_LO_SHIFT  0
#define I40E_NVM_VERSION_LO_MASK   (0xff << I40E_NVM_VERSION_LO_SHIFT)
#define I40E_NVM_VERSION_HI_SHIFT  12
#define I40E_NVM_VERSION_HI_MASK   (0xf << I40E_NVM_VERSION_HI_SHIFT)
#define I40E_OEM_VER_BUILD_MASK    0xffff
#define I40E_OEM_VER_PATCH_MASK    0xff
#define I40E_OEM_VER_BUILD_SHIFT   8
#define I40E_OEM_VER_SHIFT         24
#define I40E_PHY_DEBUG_ALL \
	(I40E_AQ_PHY_DEBUG_DISABLE_LINK_FW | \
	I40E_AQ_PHY_DEBUG_DISABLE_ALL_LINK_FW)

/* The values in here are decimal coded as hex as is the case in the NVM map*/
#define I40E_CURRENT_NVM_VERSION_HI 0x2
#define I40E_CURRENT_NVM_VERSION_LO 0x40

#define I40E_RX_DESC(R, i)			\
	(&(((union i40e_32byte_rx_desc *)((R)->desc))[i]))
#define I40E_TX_DESC(R, i)			\
	(&(((struct i40e_tx_desc *)((R)->desc))[i]))
#define I40E_TX_CTXTDESC(R, i)			\
	(&(((struct i40e_tx_context_desc *)((R)->desc))[i]))
#define I40E_TX_FDIRDESC(R, i)			\
	(&(((struct i40e_filter_program_desc *)((R)->desc))[i]))

/* default to trying for four seconds */
#define I40E_TRY_LINK_TIMEOUT (4 * HZ)

/**
 * i40e_is_mac_710 - Return true if MAC is X710/XL710
 * @hw: ptr to the hardware info
 **/
static inline bool i40e_is_mac_710(struct i40e_hw *hw)
{
	if ((hw->mac.type == I40E_MAC_X710) ||
	    (hw->mac.type == I40E_MAC_XL710))
		return true;

	return false;
}

/* driver state flags */
enum i40e_state_t {
	__I40E_TESTING,
	__I40E_CONFIG_BUSY,
	__I40E_CONFIG_DONE,
	__I40E_DOWN,
	__I40E_NEEDS_RESTART,
	__I40E_SERVICE_SCHED,
	__I40E_ADMINQ_EVENT_PENDING,
	__I40E_MDD_EVENT_PENDING,
	__I40E_VFLR_EVENT_PENDING,
	__I40E_RESET_RECOVERY_PENDING,
	__I40E_RESET_INTR_RECEIVED,
	__I40E_REINIT_REQUESTED,
	__I40E_PF_RESET_REQUESTED,
	__I40E_CORE_RESET_REQUESTED,
	__I40E_GLOBAL_RESET_REQUESTED,
	__I40E_EMP_RESET_REQUESTED,
	__I40E_EMP_RESET_INTR_RECEIVED,
	__I40E_FILTER_OVERFLOW_PROMISC,
	__I40E_SUSPENDED,
	__I40E_PTP_TX_IN_PROGRESS,
	__I40E_BAD_EEPROM,
	__I40E_DOWN_REQUESTED,
	__I40E_FD_FLUSH_REQUESTED,
	__I40E_RESET_FAILED,
	__I40E_PORT_TX_SUSPENDED,
	__I40E_VF_DISABLE,
};

enum i40e_interrupt_policy {
	I40E_INTERRUPT_BEST_CASE,
	I40E_INTERRUPT_MEDIUM,
	I40E_INTERRUPT_LOWEST
};

struct i40e_lump_tracking {
	u16 num_entries;
	u16 search_hint;
	u16 list[0];
#define I40E_PILE_VALID_BIT  0x8000
#define I40E_IWARP_IRQ_PILE_ID  (I40E_PILE_VALID_BIT - 2)
};

#define I40E_DEFAULT_ATR_SAMPLE_RATE	20
#define I40E_FDIR_MAX_RAW_PACKET_SIZE	512
#define I40E_FDIR_BUFFER_FULL_MARGIN	10
#define I40E_FDIR_BUFFER_HEAD_ROOM	32
#define I40E_FDIR_BUFFER_HEAD_ROOM_FOR_ATR (I40E_FDIR_BUFFER_HEAD_ROOM * 4)

#define I40E_HKEY_ARRAY_SIZE ((I40E_PFQF_HKEY_MAX_INDEX + 1) * 4)
#define I40E_HLUT_ARRAY_SIZE ((I40E_PFQF_HLUT_MAX_INDEX + 1) * 4)
#define I40E_VF_HLUT_ARRAY_SIZE ((I40E_VFQF_HLUT1_MAX_INDEX + 1) * 4)

enum i40e_fd_stat_idx {
	I40E_FD_STAT_ATR,
	I40E_FD_STAT_SB,
	I40E_FD_STAT_ATR_TUNNEL,
	I40E_FD_STAT_PF_COUNT
};
#define I40E_FD_STAT_PF_IDX(pf_id) ((pf_id) * I40E_FD_STAT_PF_COUNT)
#define I40E_FD_ATR_STAT_IDX(pf_id) \
			(I40E_FD_STAT_PF_IDX(pf_id) + I40E_FD_STAT_ATR)
#define I40E_FD_SB_STAT_IDX(pf_id)  \
			(I40E_FD_STAT_PF_IDX(pf_id) + I40E_FD_STAT_SB)
#define I40E_FD_ATR_TUNNEL_STAT_IDX(pf_id) \
			(I40E_FD_STAT_PF_IDX(pf_id) + I40E_FD_STAT_ATR_TUNNEL)

struct i40e_fdir_filter {
	struct hlist_node fdir_node;
	/* filter ipnut set */
	u8 flow_type;
	u8 ip4_proto;
	/* TX packet view of src and dst */
	__be32 dst_ip[4];
	__be32 src_ip[4];
	__be16 src_port;
	__be16 dst_port;
	__be32 sctp_v_tag;
	/* filter control */
	u16 q_index;
	u8  flex_off;
	u8  pctype;
	u16 dest_vsi;
	u8  dest_ctl;
	u8  fd_status;
	u16 cnt_index;
	u32 fd_id;
};

#define I40E_ETH_P_LLDP			0x88cc

#define I40E_DCB_PRIO_TYPE_STRICT	0
#define I40E_DCB_PRIO_TYPE_ETS		1
#define I40E_DCB_STRICT_PRIO_CREDITS	127
/* DCB per TC information data structure */
struct i40e_tc_info {
	u16	qoffset;	/* Queue offset from base queue */
	u16	qcount;		/* Total Queues */
	u8	netdev_tc;	/* Netdev TC index if netdev associated */
};

/* TC configuration data structure */
struct i40e_tc_configuration {
	u8	numtc;		/* Total number of enabled TCs */
	u8	enabled_tc;	/* TC map */
	struct i40e_tc_info tc_info[I40E_MAX_TRAFFIC_CLASS];
};

struct i40e_udp_port_config {
	__be16 index;
	u8 type;
};

/* struct that defines the Ethernet device */
struct i40e_pf {
	struct pci_dev *pdev;
	struct i40e_hw hw;
	unsigned long state;
	struct msix_entry *msix_entries;
	bool fc_autoneg_status;

	u16 eeprom_version;
	u16 num_vmdq_vsis;         /* num vmdq vsis this PF has set up */
	u16 num_vmdq_qps;          /* num queue pairs per vmdq pool */
	u16 num_vmdq_msix;         /* num queue vectors per vmdq pool */
	u16 num_req_vfs;           /* num VFs requested for this VF */
	u16 num_vf_qps;            /* num queue pairs per VF */
#ifdef I40E_FCOE
	u16 num_fcoe_qps;          /* num fcoe queues this PF has set up */
	u16 num_fcoe_msix;         /* num queue vectors per fcoe pool */
#endif /* I40E_FCOE */
	u16 num_lan_qps;           /* num lan queues this PF has set up */
	u16 num_lan_msix;          /* num queue vectors for the base PF vsi */
	u16 num_fdsb_msix;         /* num queue vectors for sideband Fdir */
	u16 num_iwarp_msix;        /* num of iwarp vectors for this PF */
	int iwarp_base_vector;
	int queues_left;           /* queues left unclaimed */
	u16 alloc_rss_size;        /* allocated RSS queues */
	u16 rss_size_max;          /* HW defined max RSS queues */
	u16 fdir_pf_filter_count;  /* num of guaranteed filters for this PF */
	u16 num_alloc_vsi;         /* num VSIs this driver supports */
	u8 atr_sample_rate;
	bool wol_en;

	struct hlist_head fdir_filter_list;
	u16 fdir_pf_active_filters;
	unsigned long fd_flush_timestamp;
	u32 fd_flush_cnt;
	u32 fd_add_err;
	u32 fd_atr_cnt;
	u32 fd_tcp_rule;

	struct i40e_udp_port_config udp_ports[I40E_MAX_PF_UDP_OFFLOAD_PORTS];
	u16 pending_udp_bitmap;

	enum i40e_interrupt_policy int_policy;
	u16 rx_itr_default;
	u16 tx_itr_default;
	u32 msg_enable;
	char int_name[I40E_INT_NAME_STR_LEN];
	u16 adminq_work_limit; /* num of admin receive queue desc to process */
	unsigned long service_timer_period;
	unsigned long service_timer_previous;
	struct timer_list service_timer;
	struct work_struct service_task;

	u64 flags;
#define I40E_FLAG_RX_CSUM_ENABLED		BIT_ULL(1)
#define I40E_FLAG_MSI_ENABLED			BIT_ULL(2)
#define I40E_FLAG_MSIX_ENABLED			BIT_ULL(3)
#define I40E_FLAG_RSS_ENABLED			BIT_ULL(6)
#define I40E_FLAG_VMDQ_ENABLED			BIT_ULL(7)
#define I40E_FLAG_FDIR_REQUIRES_REINIT		BIT_ULL(8)
#define I40E_FLAG_NEED_LINK_UPDATE		BIT_ULL(9)
#define I40E_FLAG_IWARP_ENABLED			BIT_ULL(10)
#ifdef I40E_FCOE
#define I40E_FLAG_FCOE_ENABLED			BIT_ULL(11)
#endif /* I40E_FCOE */
#define I40E_FLAG_CLEAN_ADMINQ			BIT_ULL(14)
#define I40E_FLAG_FILTER_SYNC			BIT_ULL(15)
#define I40E_FLAG_SERVICE_CLIENT_REQUESTED	BIT_ULL(16)
#define I40E_FLAG_PROCESS_MDD_EVENT		BIT_ULL(17)
#define I40E_FLAG_PROCESS_VFLR_EVENT		BIT_ULL(18)
#define I40E_FLAG_SRIOV_ENABLED			BIT_ULL(19)
#define I40E_FLAG_DCB_ENABLED			BIT_ULL(20)
#define I40E_FLAG_FD_SB_ENABLED			BIT_ULL(21)
#define I40E_FLAG_FD_ATR_ENABLED		BIT_ULL(22)
#define I40E_FLAG_PTP				BIT_ULL(25)
#define I40E_FLAG_MFP_ENABLED			BIT_ULL(26)
#define I40E_FLAG_UDP_FILTER_SYNC		BIT_ULL(27)
#define I40E_FLAG_PORT_ID_VALID			BIT_ULL(28)
#define I40E_FLAG_DCB_CAPABLE			BIT_ULL(29)
#define I40E_FLAG_RSS_AQ_CAPABLE		BIT_ULL(31)
#define I40E_FLAG_HW_ATR_EVICT_CAPABLE		BIT_ULL(32)
#define I40E_FLAG_OUTER_UDP_CSUM_CAPABLE	BIT_ULL(33)
#define I40E_FLAG_128_QP_RSS_CAPABLE		BIT_ULL(34)
#define I40E_FLAG_WB_ON_ITR_CAPABLE		BIT_ULL(35)
#define I40E_FLAG_VEB_STATS_ENABLED		BIT_ULL(37)
#define I40E_FLAG_MULTIPLE_TCP_UDP_RSS_PCTYPE	BIT_ULL(38)
#define I40E_FLAG_LINK_POLLING_ENABLED		BIT_ULL(39)
#define I40E_FLAG_VEB_MODE_ENABLED		BIT_ULL(40)
#define I40E_FLAG_GENEVE_OFFLOAD_CAPABLE	BIT_ULL(41)
#define I40E_FLAG_NO_PCI_LINK_CHECK		BIT_ULL(42)
#define I40E_FLAG_100M_SGMII_CAPABLE		BIT_ULL(43)
#define I40E_FLAG_RESTART_AUTONEG		BIT_ULL(44)
#define I40E_FLAG_NO_DCB_SUPPORT		BIT_ULL(45)
#define I40E_FLAG_USE_SET_LLDP_MIB		BIT_ULL(46)
#define I40E_FLAG_STOP_FW_LLDP			BIT_ULL(47)
#define I40E_FLAG_HAVE_10GBASET_PHY		BIT_ULL(48)
#define I40E_FLAG_PF_MAC			BIT_ULL(50)
#define I40E_FLAG_TRUE_PROMISC_SUPPORT		BIT_ULL(51)

	/* tracks features that get auto disabled by errors */
	u64 auto_disable_flags;

#ifdef I40E_FCOE
	struct i40e_fcoe fcoe;

#endif /* I40E_FCOE */
	bool stat_offsets_loaded;
	struct i40e_hw_port_stats stats;
	struct i40e_hw_port_stats stats_offsets;
	u32 tx_timeout_count;
	u32 tx_timeout_recovery_level;
	unsigned long tx_timeout_last_recovery;
	u32 tx_sluggish_count;
	u32 hw_csum_rx_error;
	u32 led_status;
	u16 corer_count; /* Core reset count */
	u16 globr_count; /* Global reset count */
	u16 empr_count; /* EMP reset count */
	u16 pfr_count; /* PF reset count */
	u16 sw_int_count; /* SW interrupt count */

	struct mutex switch_mutex;
	u16 lan_vsi;       /* our default LAN VSI */
	u16 lan_veb;       /* initial relay, if exists */
#define I40E_NO_VEB   0xffff
#define I40E_NO_VSI   0xffff
	u16 next_vsi;      /* Next unallocated VSI - 0-based! */
	struct i40e_vsi **vsi;
	struct i40e_veb *veb[I40E_MAX_VEB];

	struct i40e_lump_tracking *qp_pile;
	struct i40e_lump_tracking *irq_pile;

	/* switch config info */
	u16 pf_seid;
	u16 main_vsi_seid;
	u16 mac_seid;
	struct kobject *switch_kobj;
#ifdef CONFIG_DEBUG_FS
	struct dentry *i40e_dbg_pf;
#endif /* CONFIG_DEBUG_FS */
	bool cur_promisc;

	u16 instance; /* A unique number per i40e_pf instance in the system */

	/* sr-iov config info */
	struct i40e_vf *vf;
	int num_alloc_vfs;	/* actual number of VFs allocated */
	u32 vf_aq_requests;
	u32 arq_overflows;	/* Not fatal, possibly indicative of problems */

	/* DCBx/DCBNL capability for PF that indicates
	 * whether DCBx is managed by firmware or host
	 * based agent (LLDPAD). Also, indicates what
	 * flavor of DCBx protocol (IEEE/CEE) is supported
	 * by the device. For now we're supporting IEEE
	 * mode only.
	 */
	u16 dcbx_cap;

	u32	fcoe_hmc_filt_num;
	u32	fcoe_hmc_cntx_num;
	struct i40e_filter_control_settings filter_settings;

	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_caps;
	struct sk_buff *ptp_tx_skb;
	struct hwtstamp_config tstamp_config;
	unsigned long last_rx_ptp_check;
	spinlock_t tmreg_lock; /* Used to protect the device time registers. */
	u64 ptp_base_adj;
	u32 tx_hwtstamp_timeouts;
	u32 rx_hwtstamp_cleared;
	bool ptp_tx;
	bool ptp_rx;
	u16 rss_table_size; /* HW RSS table size */
	/* These are only valid in NPAR modes */
	u32 npar_max_bw;
	u32 npar_min_bw;

	u32 ioremap_len;
	u32 fd_inv;
	u16 phy_led_val;
};

enum i40e_filter_state {
	I40E_FILTER_INVALID = 0,	/* Invalid state */
	I40E_FILTER_NEW,		/* New, not sent to FW yet */
	I40E_FILTER_ACTIVE,		/* Added to switch by FW */
	I40E_FILTER_FAILED,		/* Rejected by FW */
	I40E_FILTER_REMOVE,		/* To be removed */
/* There is no 'removed' state; the filter struct is freed */
};
struct i40e_mac_filter {
	struct list_head list;
	u8 macaddr[ETH_ALEN];
#define I40E_VLAN_ANY -1
	s16 vlan;
	u8 counter;		/* number of instances of this filter */
	bool is_vf;		/* filter belongs to a VF */
	bool is_netdev;		/* filter belongs to a netdev */
	enum i40e_filter_state state;
};

struct i40e_veb {
	struct i40e_pf *pf;
	u16 idx;
	u16 veb_idx;           /* index of VEB parent */
	u16 seid;
	u16 uplink_seid;
	u16 stats_idx;           /* index of VEB parent */
	u8  enabled_tc;
	u16 bridge_mode;	/* Bridge Mode (VEB/VEPA) */
	u16 flags;
	u16 bw_limit;
	u8  bw_max_quanta;
	bool is_abs_credits;
	u8  bw_tc_share_credits[I40E_MAX_TRAFFIC_CLASS];
	u16 bw_tc_limit_credits[I40E_MAX_TRAFFIC_CLASS];
	u8  bw_tc_max_quanta[I40E_MAX_TRAFFIC_CLASS];
	struct kobject *kobj;
	bool stat_offsets_loaded;
	struct i40e_eth_stats stats;
	struct i40e_eth_stats stats_offsets;
	struct i40e_veb_tc_stats tc_stats;
	struct i40e_veb_tc_stats tc_stats_offsets;
};

/* struct that defines a VSI, associated with a dev */
struct i40e_vsi {
	struct net_device *netdev;
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	bool netdev_registered;
	bool stat_offsets_loaded;

	u32 current_netdev_flags;
	unsigned long state;
#define I40E_VSI_FLAG_FILTER_CHANGED	BIT(0)
#define I40E_VSI_FLAG_VEB_OWNER		BIT(1)
	unsigned long flags;

	/* Per VSI lock to protect elements/list (MAC filter) */
	spinlock_t mac_filter_list_lock;
	struct list_head mac_filter_list;

	/* VSI stats */
	struct rtnl_link_stats64 net_stats;
	struct rtnl_link_stats64 net_stats_offsets;
	struct i40e_eth_stats eth_stats;
	struct i40e_eth_stats eth_stats_offsets;
#ifdef I40E_FCOE
	struct i40e_fcoe_stats fcoe_stats;
	struct i40e_fcoe_stats fcoe_stats_offsets;
	bool fcoe_stat_offsets_loaded;
#endif
	u32 tx_restart;
	u32 tx_busy;
	u64 tx_linearize;
	u64 tx_force_wb;
	u64 tx_lost_interrupt;
	u32 rx_buf_failed;
	u32 rx_page_failed;

	/* These are containers of ring pointers, allocated at run-time */
	struct i40e_ring **rx_rings;
	struct i40e_ring **tx_rings;

	u32  active_filters;
	u32  promisc_threshold;

	u16 work_limit;
	u16 int_rate_limit;  /* value in usecs */

	u16 rss_table_size; /* HW RSS table size */
	u16 rss_size;       /* Allocated RSS queues */
	u8  *rss_hkey_user; /* User configured hash keys */
	u8  *rss_lut_user;  /* User configured lookup table entries */

	u16 max_frame;
	u16 rx_buf_len;

	/* List of q_vectors allocated to this VSI */
	struct i40e_q_vector **q_vectors;
	int num_q_vectors;
	int base_vector;
	bool irqs_ready;

	u16 seid;            /* HW index of this VSI (absolute index) */
	u16 id;              /* VSI number */
	u16 uplink_seid;

	u16 base_queue;      /* vsi's first queue in hw array */
	u16 alloc_queue_pairs; /* Allocated Tx/Rx queues */
	u16 req_queue_pairs; /* User requested queue pairs */
	u16 num_queue_pairs; /* Used tx and rx pairs */
	u16 num_desc;
	enum i40e_vsi_type type;  /* VSI type, e.g., LAN, FCoE, etc */
	s16 vf_id;		/* Virtual function ID for SRIOV VSIs */

	struct i40e_tc_configuration tc_config;
	struct i40e_aqc_vsi_properties_data info;

	/* VSI BW limit (absolute across all TCs) */
	u16 bw_limit;		/* VSI BW Limit (0 = disabled) */
	u8  bw_max_quanta;	/* Max Quanta when BW limit is enabled */

	/* Relative TC credits across VSIs */
	u8  bw_ets_share_credits[I40E_MAX_TRAFFIC_CLASS];
	/* TC BW limit credits within VSI */
	u16  bw_ets_limit_credits[I40E_MAX_TRAFFIC_CLASS];
	/* TC BW limit max quanta within VSI */
	u8  bw_ets_max_quanta[I40E_MAX_TRAFFIC_CLASS];

	struct i40e_pf *back;  /* Backreference to associated PF */
	u16 idx;               /* index in pf->vsi[] */
	u16 veb_idx;           /* index of VEB parent */
	struct kobject *kobj;  /* sysfs object */
	bool current_isup;     /* Sync 'link up' logging */

	void *priv;	/* client driver data reference. */

	/* VSI specific handlers */
	irqreturn_t (*irq_handler)(int irq, void *data);
} ____cacheline_internodealigned_in_smp;

struct i40e_netdev_priv {
	struct i40e_vsi *vsi;
};

/* struct that defines an interrupt vector */
struct i40e_q_vector {
	struct i40e_vsi *vsi;

	u16 v_idx;		/* index in the vsi->q_vector array. */
	u16 reg_idx;		/* register index of the interrupt */

	struct napi_struct napi;

	struct i40e_ring_container rx;
	struct i40e_ring_container tx;

	u8 num_ringpairs;	/* total number of ring pairs in vector */

#define I40E_Q_VECTOR_HUNG_DETECT 0 /* Bit Index for hung detection logic */
	unsigned long hung_detected; /* Set/Reset for hung_detection logic */

	cpumask_t affinity_mask;
	struct rcu_head rcu;	/* to avoid race with update stats on free */
	char name[I40E_INT_NAME_STR_LEN];
	bool arm_wb_state;
#define ITR_COUNTDOWN_START 100
	u8 itr_countdown;	/* when 0 should adjust ITR */
} ____cacheline_internodealigned_in_smp;

/* lan device */
struct i40e_device {
	struct list_head list;
	struct i40e_pf *pf;
};

/**
 * i40e_nvm_version_str - format the NVM version strings
 * @hw: ptr to the hardware info
 **/
static inline char *i40e_nvm_version_str(struct i40e_hw *hw)
{
	static char buf[32];
	u32 full_ver;
	u8 ver, patch;
	u16 build;

	full_ver = hw->nvm.oem_ver;
	ver = (u8)(full_ver >> I40E_OEM_VER_SHIFT);
	build = (u16)((full_ver >> I40E_OEM_VER_BUILD_SHIFT) &
		 I40E_OEM_VER_BUILD_MASK);
	patch = (u8)(full_ver & I40E_OEM_VER_PATCH_MASK);

	snprintf(buf, sizeof(buf),
		 "%x.%02x 0x%x %d.%d.%d",
		 (hw->nvm.version & I40E_NVM_VERSION_HI_MASK) >>
			I40E_NVM_VERSION_HI_SHIFT,
		 (hw->nvm.version & I40E_NVM_VERSION_LO_MASK) >>
			I40E_NVM_VERSION_LO_SHIFT,
		 hw->nvm.eetrack, ver, build, patch);

	return buf;
}

/**
 * i40e_netdev_to_pf: Retrieve the PF struct for given netdev
 * @netdev: the corresponding netdev
 *
 * Return the PF struct for the given netdev
 **/
static inline struct i40e_pf *i40e_netdev_to_pf(struct net_device *netdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;

	return vsi->back;
}

static inline void i40e_vsi_setup_irqhandler(struct i40e_vsi *vsi,
				irqreturn_t (*irq_handler)(int, void *))
{
	vsi->irq_handler = irq_handler;
}

/**
 * i40e_rx_is_programming_status - check for programming status descriptor
 * @qw: the first quad word of the program status descriptor
 *
 * The value of in the descriptor length field indicate if this
 * is a programming status descriptor for flow director or FCoE
 * by the value of I40E_RX_PROG_STATUS_DESC_LENGTH, otherwise
 * it is a packet descriptor.
 **/
static inline bool i40e_rx_is_programming_status(u64 qw)
{
	return I40E_RX_PROG_STATUS_DESC_LENGTH ==
		(qw >> I40E_RX_PROG_STATUS_DESC_LENGTH_SHIFT);
}

/**
 * i40e_get_fd_cnt_all - get the total FD filter space available
 * @pf: pointer to the PF struct
 **/
static inline int i40e_get_fd_cnt_all(struct i40e_pf *pf)
{
	return pf->hw.fdir_shared_filter_count + pf->fdir_pf_filter_count;
}

/* needed by i40e_ethtool.c */
int i40e_up(struct i40e_vsi *vsi);
void i40e_down(struct i40e_vsi *vsi);
extern const char i40e_driver_name[];
extern const char i40e_driver_version_str[];
void i40e_do_reset_safe(struct i40e_pf *pf, u32 reset_flags);
void i40e_do_reset(struct i40e_pf *pf, u32 reset_flags);
int i40e_config_rss(struct i40e_vsi *vsi, u8 *seed, u8 *lut, u16 lut_size);
int i40e_get_rss(struct i40e_vsi *vsi, u8 *seed, u8 *lut, u16 lut_size);
struct i40e_vsi *i40e_find_vsi_from_id(struct i40e_pf *pf, u16 id);
void i40e_update_stats(struct i40e_vsi *vsi);
void i40e_update_eth_stats(struct i40e_vsi *vsi);
struct rtnl_link_stats64 *i40e_get_vsi_stats_struct(struct i40e_vsi *vsi);
int i40e_fetch_switch_configuration(struct i40e_pf *pf,
				    bool printconfig);

int i40e_program_fdir_filter(struct i40e_fdir_filter *fdir_data, u8 *raw_packet,
			     struct i40e_pf *pf, bool add);
int i40e_add_del_fdir(struct i40e_vsi *vsi,
		      struct i40e_fdir_filter *input, bool add);
void i40e_fdir_check_and_reenable(struct i40e_pf *pf);
u32 i40e_get_current_fd_count(struct i40e_pf *pf);
u32 i40e_get_cur_guaranteed_fd_count(struct i40e_pf *pf);
u32 i40e_get_current_atr_cnt(struct i40e_pf *pf);
u32 i40e_get_global_fd_count(struct i40e_pf *pf);
bool i40e_set_ntuple(struct i40e_pf *pf, netdev_features_t features);
void i40e_set_ethtool_ops(struct net_device *netdev);
struct i40e_mac_filter *i40e_add_filter(struct i40e_vsi *vsi,
					u8 *macaddr, s16 vlan,
					bool is_vf, bool is_netdev);
void i40e_del_filter(struct i40e_vsi *vsi, u8 *macaddr, s16 vlan,
		     bool is_vf, bool is_netdev);
int i40e_sync_vsi_filters(struct i40e_vsi *vsi);
struct i40e_vsi *i40e_vsi_setup(struct i40e_pf *pf, u8 type,
				u16 uplink, u32 param1);
int i40e_vsi_release(struct i40e_vsi *vsi);
struct i40e_vsi *i40e_vsi_lookup(struct i40e_pf *pf, enum i40e_vsi_type type,
				 struct i40e_vsi *start_vsi);
#ifdef I40E_FCOE
void i40e_vsi_setup_queue_map(struct i40e_vsi *vsi,
			      struct i40e_vsi_context *ctxt,
			      u8 enabled_tc, bool is_add);
#endif
void i40e_service_event_schedule(struct i40e_pf *pf);
void i40e_notify_client_of_vf_msg(struct i40e_vsi *vsi, u32 vf_id,
				  u8 *msg, u16 len);

int i40e_vsi_control_rings(struct i40e_vsi *vsi, bool enable);
int i40e_reconfig_rss_queues(struct i40e_pf *pf, int queue_count);
struct i40e_veb *i40e_veb_setup(struct i40e_pf *pf, u16 flags, u16 uplink_seid,
				u16 downlink_seid, u8 enabled_tc);
void i40e_veb_release(struct i40e_veb *veb);

int i40e_veb_config_tc(struct i40e_veb *veb, u8 enabled_tc);
int i40e_vsi_add_pvid(struct i40e_vsi *vsi, u16 vid);
void i40e_vsi_remove_pvid(struct i40e_vsi *vsi);
void i40e_vsi_reset_stats(struct i40e_vsi *vsi);
void i40e_pf_reset_stats(struct i40e_pf *pf);
#ifdef CONFIG_DEBUG_FS
void i40e_dbg_pf_init(struct i40e_pf *pf);
void i40e_dbg_pf_exit(struct i40e_pf *pf);
void i40e_dbg_init(void);
void i40e_dbg_exit(void);
#else
static inline void i40e_dbg_pf_init(struct i40e_pf *pf) {}
static inline void i40e_dbg_pf_exit(struct i40e_pf *pf) {}
static inline void i40e_dbg_init(void) {}
static inline void i40e_dbg_exit(void) {}
#endif /* CONFIG_DEBUG_FS*/
/* needed by client drivers */
int i40e_lan_add_device(struct i40e_pf *pf);
int i40e_lan_del_device(struct i40e_pf *pf);
void i40e_client_subtask(struct i40e_pf *pf);
void i40e_notify_client_of_l2_param_changes(struct i40e_vsi *vsi);
void i40e_notify_client_of_netdev_open(struct i40e_vsi *vsi);
void i40e_notify_client_of_netdev_close(struct i40e_vsi *vsi, bool reset);
void i40e_notify_client_of_vf_enable(struct i40e_pf *pf, u32 num_vfs);
void i40e_notify_client_of_vf_reset(struct i40e_pf *pf, u32 vf_id);
int i40e_vf_client_capable(struct i40e_pf *pf, u32 vf_id,
			   enum i40e_client_type type);
/**
 * i40e_irq_dynamic_enable - Enable default interrupt generation settings
 * @vsi: pointer to a vsi
 * @vector: enable a particular Hw Interrupt vector, without base_vector
 **/
static inline void i40e_irq_dynamic_enable(struct i40e_vsi *vsi, int vector)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	u32 val;

	/* definitely clear the PBA here, as this function is meant to
	 * clean out all previous interrupts AND enable the interrupt
	 */
	val = I40E_PFINT_DYN_CTLN_INTENA_MASK |
	      I40E_PFINT_DYN_CTLN_CLEARPBA_MASK |
	      (I40E_ITR_NONE << I40E_PFINT_DYN_CTLN_ITR_INDX_SHIFT);
	wr32(hw, I40E_PFINT_DYN_CTLN(vector + vsi->base_vector - 1), val);
	/* skip the flush */
}

void i40e_irq_dynamic_disable_icr0(struct i40e_pf *pf);
void i40e_irq_dynamic_enable_icr0(struct i40e_pf *pf, bool clearpba);
#ifdef I40E_FCOE
struct rtnl_link_stats64 *i40e_get_netdev_stats_struct(
					     struct net_device *netdev,
					     struct rtnl_link_stats64 *storage);
int i40e_set_mac(struct net_device *netdev, void *p);
void i40e_set_rx_mode(struct net_device *netdev);
#endif
int i40e_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd);
#ifdef I40E_FCOE
void i40e_tx_timeout(struct net_device *netdev);
int i40e_vlan_rx_add_vid(struct net_device *netdev,
			 __always_unused __be16 proto, u16 vid);
int i40e_vlan_rx_kill_vid(struct net_device *netdev,
			  __always_unused __be16 proto, u16 vid);
#endif
int i40e_open(struct net_device *netdev);
int i40e_close(struct net_device *netdev);
int i40e_vsi_open(struct i40e_vsi *vsi);
void i40e_vlan_stripping_disable(struct i40e_vsi *vsi);
int i40e_vsi_add_vlan(struct i40e_vsi *vsi, s16 vid);
int i40e_vsi_kill_vlan(struct i40e_vsi *vsi, s16 vid);
struct i40e_mac_filter *i40e_put_mac_in_vlan(struct i40e_vsi *vsi, u8 *macaddr,
					     bool is_vf, bool is_netdev);
int i40e_del_mac_all_vlan(struct i40e_vsi *vsi, u8 *macaddr,
			  bool is_vf, bool is_netdev);
bool i40e_is_vsi_in_vlan(struct i40e_vsi *vsi);
struct i40e_mac_filter *i40e_find_mac(struct i40e_vsi *vsi, u8 *macaddr,
				      bool is_vf, bool is_netdev);
#ifdef I40E_FCOE
int __i40e_setup_tc(struct net_device *netdev, u32 handle, __be16 proto,
		    struct tc_to_netdev *tc);
void i40e_netpoll(struct net_device *netdev);
int i40e_fcoe_enable(struct net_device *netdev);
int i40e_fcoe_disable(struct net_device *netdev);
int i40e_fcoe_vsi_init(struct i40e_vsi *vsi, struct i40e_vsi_context *ctxt);
u8 i40e_get_fcoe_tc_map(struct i40e_pf *pf);
void i40e_fcoe_config_netdev(struct net_device *netdev, struct i40e_vsi *vsi);
void i40e_fcoe_vsi_setup(struct i40e_pf *pf);
void i40e_init_pf_fcoe(struct i40e_pf *pf);
int i40e_fcoe_setup_ddp_resources(struct i40e_vsi *vsi);
void i40e_fcoe_free_ddp_resources(struct i40e_vsi *vsi);
int i40e_fcoe_handle_offload(struct i40e_ring *rx_ring,
			     union i40e_rx_desc *rx_desc,
			     struct sk_buff *skb);
void i40e_fcoe_handle_status(struct i40e_ring *rx_ring,
			     union i40e_rx_desc *rx_desc, u8 prog_id);
#endif /* I40E_FCOE */
void i40e_vlan_stripping_enable(struct i40e_vsi *vsi);
#ifdef CONFIG_I40E_DCB
void i40e_dcbnl_flush_apps(struct i40e_pf *pf,
			   struct i40e_dcbx_config *old_cfg,
			   struct i40e_dcbx_config *new_cfg);
void i40e_dcbnl_set_all(struct i40e_vsi *vsi);
void i40e_dcbnl_setup(struct i40e_vsi *vsi);
bool i40e_dcb_need_reconfig(struct i40e_pf *pf,
			    struct i40e_dcbx_config *old_cfg,
			    struct i40e_dcbx_config *new_cfg);
#endif /* CONFIG_I40E_DCB */
void i40e_ptp_rx_hang(struct i40e_vsi *vsi);
void i40e_ptp_tx_hwtstamp(struct i40e_pf *pf);
void i40e_ptp_rx_hwtstamp(struct i40e_pf *pf, struct sk_buff *skb, u8 index);
void i40e_ptp_set_increment(struct i40e_pf *pf);
int i40e_ptp_set_ts_config(struct i40e_pf *pf, struct ifreq *ifr);
int i40e_ptp_get_ts_config(struct i40e_pf *pf, struct ifreq *ifr);
void i40e_ptp_init(struct i40e_pf *pf);
void i40e_ptp_stop(struct i40e_pf *pf);
int i40e_is_vsi_uplink_mode_veb(struct i40e_vsi *vsi);
i40e_status i40e_get_npar_bw_setting(struct i40e_pf *pf);
i40e_status i40e_set_npar_bw_setting(struct i40e_pf *pf);
i40e_status i40e_commit_npar_bw_setting(struct i40e_pf *pf);
void i40e_print_link_message(struct i40e_vsi *vsi, bool isup);
#endif /* _I40E_H_ */
