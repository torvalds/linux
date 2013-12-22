/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
 * Copyright(c) 2013 Intel Corporation.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
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
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/netdevice.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/sctp.h>
#include <linux/pkt_sched.h>
#include <linux/ipv6.h>
#include <net/checksum.h>
#include <net/ip6_checksum.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include "i40e_type.h"
#include "i40e_prototype.h"
#include "i40e_virtchnl.h"
#include "i40e_virtchnl_pf.h"
#include "i40e_txrx.h"

/* Useful i40e defaults */
#define I40E_BASE_PF_SEID     16
#define I40E_BASE_VSI_SEID    512
#define I40E_BASE_VEB_SEID    288
#define I40E_MAX_VEB          16

#define I40E_MAX_NUM_DESCRIPTORS      4096
#define I40E_MAX_REGISTER     0x0038FFFF
#define I40E_DEFAULT_NUM_DESCRIPTORS  512
#define I40E_REQ_DESCRIPTOR_MULTIPLE  32
#define I40E_MIN_NUM_DESCRIPTORS      64
#define I40E_MIN_MSIX                 2
#define I40E_DEFAULT_NUM_VMDQ_VSI     8 /* max 256 VSIs */
#define I40E_DEFAULT_QUEUES_PER_VMDQ  2 /* max 16 qps */
#define I40E_DEFAULT_QUEUES_PER_VF    4
#define I40E_DEFAULT_QUEUES_PER_TC    1 /* should be a power of 2 */
#define I40E_FDIR_RING                0
#define I40E_FDIR_RING_COUNT          32
#define I40E_MAX_AQ_BUF_SIZE          4096
#define I40E_AQ_LEN                   32
#define I40E_AQ_WORK_LIMIT            16
#define I40E_MAX_USER_PRIORITY        8
#define I40E_DEFAULT_MSG_ENABLE       4

#define I40E_NVM_VERSION_LO_SHIFT  0
#define I40E_NVM_VERSION_LO_MASK   (0xf << I40E_NVM_VERSION_LO_SHIFT)
#define I40E_NVM_VERSION_MID_SHIFT 4
#define I40E_NVM_VERSION_MID_MASK  (0xff << I40E_NVM_VERSION_MID_SHIFT)
#define I40E_NVM_VERSION_HI_SHIFT  12
#define I40E_NVM_VERSION_HI_MASK   (0xf << I40E_NVM_VERSION_HI_SHIFT)

/* magic for getting defines into strings */
#define STRINGIFY(foo)  #foo
#define XSTRINGIFY(bar) STRINGIFY(bar)

#ifndef ARCH_HAS_PREFETCH
#define prefetch(X)
#endif

#define I40E_RX_DESC(R, i)			\
	((ring_is_16byte_desc_enabled(R))	\
		? (union i40e_32byte_rx_desc *)	\
			(&(((union i40e_16byte_rx_desc *)((R)->desc))[i])) \
		: (&(((union i40e_32byte_rx_desc *)((R)->desc))[i])))
#define I40E_TX_DESC(R, i)			\
	(&(((struct i40e_tx_desc *)((R)->desc))[i]))
#define I40E_TX_CTXTDESC(R, i)			\
	(&(((struct i40e_tx_context_desc *)((R)->desc))[i]))
#define I40E_TX_FDIRDESC(R, i)			\
	(&(((struct i40e_filter_program_desc *)((R)->desc))[i]))

/* default to trying for four seconds */
#define I40E_TRY_LINK_TIMEOUT (4 * HZ)

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
	__I40E_FILTER_OVERFLOW_PROMISC,
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
};

#define I40E_DEFAULT_ATR_SAMPLE_RATE	20
#define I40E_FDIR_MAX_RAW_PACKET_LOOKUP 512
struct i40e_fdir_data {
	u16 q_index;
	u8  flex_off;
	u8  pctype;
	u16 dest_vsi;
	u8  dest_ctl;
	u8  fd_status;
	u16 cnt_index;
	u32 fd_id;
	u8  *raw_packet;
};

#define I40E_DCB_PRIO_TYPE_STRICT	0
#define I40E_DCB_PRIO_TYPE_ETS		1
#define I40E_DCB_STRICT_PRIO_CREDITS	127
#define I40E_MAX_USER_PRIORITY	8
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

/* struct that defines the Ethernet device */
struct i40e_pf {
	struct pci_dev *pdev;
	struct i40e_hw hw;
	unsigned long state;
	unsigned long link_check_timeout;
	struct msix_entry *msix_entries;
	u16 num_msix_entries;
	bool fc_autoneg_status;

	u16 eeprom_version;
	u16 num_vmdq_vsis;         /* num vmdq pools this pf has set up */
	u16 num_vmdq_qps;          /* num queue pairs per vmdq pool */
	u16 num_vmdq_msix;         /* num queue vectors per vmdq pool */
	u16 num_req_vfs;           /* num vfs requested for this vf */
	u16 num_vf_qps;            /* num queue pairs per vf */
	u16 num_tc_qps;            /* num queue pairs per TC */
	u16 num_lan_qps;           /* num lan queues this pf has set up */
	u16 num_lan_msix;          /* num queue vectors for the base pf vsi */
	u16 rss_size;              /* num queues in the RSS array */
	u16 rss_size_max;          /* HW defined max RSS queues */
	u16 fdir_pf_filter_count;  /* num of guaranteed filters for this PF */
	u8 atr_sample_rate;

	enum i40e_interrupt_policy int_policy;
	u16 rx_itr_default;
	u16 tx_itr_default;
	u16 msg_enable;
	char misc_int_name[IFNAMSIZ + 9];
	u16 adminq_work_limit; /* num of admin receive queue desc to process */
	int service_timer_period;
	struct timer_list service_timer;
	struct work_struct service_task;

	u64 flags;
#define I40E_FLAG_RX_CSUM_ENABLED              (u64)(1 << 1)
#define I40E_FLAG_MSI_ENABLED                  (u64)(1 << 2)
#define I40E_FLAG_MSIX_ENABLED                 (u64)(1 << 3)
#define I40E_FLAG_RX_1BUF_ENABLED              (u64)(1 << 4)
#define I40E_FLAG_RX_PS_ENABLED                (u64)(1 << 5)
#define I40E_FLAG_RSS_ENABLED                  (u64)(1 << 6)
#define I40E_FLAG_MQ_ENABLED                   (u64)(1 << 7)
#define I40E_FLAG_VMDQ_ENABLED                 (u64)(1 << 8)
#define I40E_FLAG_FDIR_REQUIRES_REINIT         (u64)(1 << 9)
#define I40E_FLAG_NEED_LINK_UPDATE             (u64)(1 << 10)
#define I40E_FLAG_IN_NETPOLL                   (u64)(1 << 13)
#define I40E_FLAG_16BYTE_RX_DESC_ENABLED       (u64)(1 << 14)
#define I40E_FLAG_CLEAN_ADMINQ                 (u64)(1 << 15)
#define I40E_FLAG_FILTER_SYNC                  (u64)(1 << 16)
#define I40E_FLAG_PROCESS_MDD_EVENT            (u64)(1 << 18)
#define I40E_FLAG_PROCESS_VFLR_EVENT           (u64)(1 << 19)
#define I40E_FLAG_SRIOV_ENABLED                (u64)(1 << 20)
#define I40E_FLAG_DCB_ENABLED                  (u64)(1 << 21)
#define I40E_FLAG_FDIR_ENABLED                 (u64)(1 << 22)
#define I40E_FLAG_FDIR_ATR_ENABLED             (u64)(1 << 23)
#define I40E_FLAG_MFP_ENABLED                  (u64)(1 << 27)

	u16 num_tx_queues;
	u16 num_rx_queues;

	bool stat_offsets_loaded;
	struct i40e_hw_port_stats stats;
	struct i40e_hw_port_stats stats_offsets;
	u32 tx_timeout_count;
	u32 tx_timeout_recovery_level;
	unsigned long tx_timeout_last_recovery;
	u32 hw_csum_rx_error;
	u32 led_status;
	u16 corer_count; /* Core reset count */
	u16 globr_count; /* Global reset count */
	u16 empr_count; /* EMP reset count */
	u16 pfr_count; /* PF reset count */

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
	struct i40e_aqc_get_switch_config_data *sw_config;
	struct kobject *switch_kobj;
#ifdef CONFIG_DEBUG_FS
	struct dentry *i40e_dbg_pf;
#endif /* CONFIG_DEBUG_FS */

	/* sr-iov config info */
	struct i40e_vf *vf;
	int num_alloc_vfs;	/* actual number of VFs allocated */
	u32 vf_aq_requests;

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
};

struct i40e_mac_filter {
	struct list_head list;
	u8 macaddr[ETH_ALEN];
#define I40E_VLAN_ANY -1
	s16 vlan;
	u8 counter;		/* number of instances of this filter */
	bool is_vf;		/* filter belongs to a VF */
	bool is_netdev;		/* filter belongs to a netdev */
	bool changed;		/* filter needs to be sync'd to the HW */
};

struct i40e_veb {
	struct i40e_pf *pf;
	u16 idx;
	u16 veb_idx;           /* index of VEB parent */
	u16 seid;
	u16 uplink_seid;
	u16 stats_idx;           /* index of VEB parent */
	u8  enabled_tc;
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
};

/* struct that defines a VSI, associated with a dev */
struct i40e_vsi {
	struct net_device *netdev;
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	bool netdev_registered;
	bool stat_offsets_loaded;

	u32 current_netdev_flags;
	unsigned long state;
#define I40E_VSI_FLAG_FILTER_CHANGED  (1<<0)
#define I40E_VSI_FLAG_VEB_OWNER       (1<<1)
	unsigned long flags;

	struct list_head mac_filter_list;

	/* VSI stats */
	struct rtnl_link_stats64 net_stats;
	struct rtnl_link_stats64 net_stats_offsets;
	struct i40e_eth_stats eth_stats;
	struct i40e_eth_stats eth_stats_offsets;
	u32 tx_restart;
	u32 tx_busy;
	u32 rx_buf_failed;
	u32 rx_page_failed;

	/* These are containers of ring pointers, allocated at run-time */
	struct i40e_ring **rx_rings;
	struct i40e_ring **tx_rings;

	u16 work_limit;
	/* high bit set means dynamic, use accessor routines to read/write.
	 * hardware only supports 2us resolution for the ITR registers.
	 * these values always store the USER setting, and must be converted
	 * before programming to a register.
	 */
	u16 rx_itr_setting;
	u16 tx_itr_setting;

	u16 max_frame;
	u16 rx_hdr_len;
	u16 rx_buf_len;
	u8  dtype;

	/* List of q_vectors allocated to this VSI */
	struct i40e_q_vector **q_vectors;
	int num_q_vectors;
	int base_vector;

	u16 seid;            /* HW index of this VSI (absolute index) */
	u16 id;              /* VSI number */
	u16 uplink_seid;

	u16 base_queue;      /* vsi's first queue in hw array */
	u16 alloc_queue_pairs; /* Allocated Tx/Rx queues */
	u16 num_queue_pairs; /* Used tx and rx pairs */
	u16 num_desc;
	enum i40e_vsi_type type;  /* VSI type, e.g., LAN, FCoE, etc */
	u16 vf_id;		/* Virtual function ID for SRIOV VSIs */

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

	cpumask_t affinity_mask;
	struct rcu_head rcu;	/* to avoid race with update stats on free */
	char name[IFNAMSIZ + 9];
} ____cacheline_internodealigned_in_smp;

/* lan device */
struct i40e_device {
	struct list_head list;
	struct i40e_pf *pf;
};

/**
 * i40e_fw_version_str - format the FW and NVM version strings
 * @hw: ptr to the hardware info
 **/
static inline char *i40e_fw_version_str(struct i40e_hw *hw)
{
	static char buf[32];

	snprintf(buf, sizeof(buf),
		 "f%d.%d a%d.%d n%02d.%02d.%02d e%08x",
		 hw->aq.fw_maj_ver, hw->aq.fw_min_ver,
		 hw->aq.api_maj_ver, hw->aq.api_min_ver,
		 (hw->nvm.version & I40E_NVM_VERSION_HI_MASK)
						>> I40E_NVM_VERSION_HI_SHIFT,
		 (hw->nvm.version & I40E_NVM_VERSION_MID_MASK)
						>> I40E_NVM_VERSION_MID_SHIFT,
		 (hw->nvm.version & I40E_NVM_VERSION_LO_MASK)
						>> I40E_NVM_VERSION_LO_SHIFT,
		 hw->nvm.eetrack);

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

/* needed by i40e_ethtool.c */
int i40e_up(struct i40e_vsi *vsi);
void i40e_down(struct i40e_vsi *vsi);
extern const char i40e_driver_name[];
extern const char i40e_driver_version_str[];
void i40e_do_reset(struct i40e_pf *pf, u32 reset_flags);
void i40e_update_stats(struct i40e_vsi *vsi);
void i40e_update_eth_stats(struct i40e_vsi *vsi);
struct rtnl_link_stats64 *i40e_get_vsi_stats_struct(struct i40e_vsi *vsi);
int i40e_fetch_switch_configuration(struct i40e_pf *pf,
				    bool printconfig);

/* needed by i40e_main.c */
void i40e_add_fdir_filter(struct i40e_fdir_data fdir_data,
			  struct i40e_ring *tx_ring);
void i40e_add_remove_filter(struct i40e_fdir_data fdir_data,
			    struct i40e_ring *tx_ring);
void i40e_update_fdir_filter(struct i40e_fdir_data fdir_data,
			     struct i40e_ring *tx_ring);
int i40e_program_fdir_filter(struct i40e_fdir_data *fdir_data,
			     struct i40e_pf *pf, bool add);

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
struct i40e_veb *i40e_veb_setup(struct i40e_pf *pf, u16 flags, u16 uplink_seid,
				u16 downlink_seid, u8 enabled_tc);
void i40e_veb_release(struct i40e_veb *veb);

i40e_status i40e_vsi_add_pvid(struct i40e_vsi *vsi, u16 vid);
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
void i40e_irq_dynamic_enable(struct i40e_vsi *vsi, int vector);
void i40e_irq_dynamic_enable_icr0(struct i40e_pf *pf);
int i40e_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd);
void i40e_vlan_stripping_disable(struct i40e_vsi *vsi);
int i40e_vsi_add_vlan(struct i40e_vsi *vsi, s16 vid);
int i40e_vsi_kill_vlan(struct i40e_vsi *vsi, s16 vid);
struct i40e_mac_filter *i40e_put_mac_in_vlan(struct i40e_vsi *vsi, u8 *macaddr,
					     bool is_vf, bool is_netdev);
bool i40e_is_vsi_in_vlan(struct i40e_vsi *vsi);
struct i40e_mac_filter *i40e_find_mac(struct i40e_vsi *vsi, u8 *macaddr,
				      bool is_vf, bool is_netdev);
void i40e_vlan_stripping_enable(struct i40e_vsi *vsi);

#endif /* _I40E_H_ */
