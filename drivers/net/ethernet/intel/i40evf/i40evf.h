/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Virtual Function Driver
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

#ifndef _I40EVF_H_
#define _I40EVF_H_

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/sctp.h>
#include <linux/ipv6.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/skbuff.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/socket.h>
#include <linux/jiffies.h>
#include <net/ip6_checksum.h>
#include <net/udp.h>

#include "i40e_type.h"
#include <linux/avf/virtchnl.h>
#include "i40e_txrx.h"

#define DEFAULT_DEBUG_LEVEL_SHIFT 3
#define PFX "i40evf: "

/* VSI state flags shared with common code */
enum i40evf_vsi_state_t {
	__I40E_VSI_DOWN,
	/* This must be last as it determines the size of the BITMAP */
	__I40E_VSI_STATE_SIZE__,
};

/* dummy struct to make common code less painful */
struct i40e_vsi {
	struct i40evf_adapter *back;
	struct net_device *netdev;
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	u16 seid;
	u16 id;
	DECLARE_BITMAP(state, __I40E_VSI_STATE_SIZE__);
	int base_vector;
	u16 work_limit;
	u16 qs_handle;
	void *priv;     /* client driver data reference. */
};

/* How many Rx Buffers do we bundle into one write to the hardware ? */
#define I40EVF_RX_BUFFER_WRITE	16	/* Must be power of 2 */
#define I40EVF_DEFAULT_TXD	512
#define I40EVF_DEFAULT_RXD	512
#define I40EVF_MAX_TXD		4096
#define I40EVF_MIN_TXD		64
#define I40EVF_MAX_RXD		4096
#define I40EVF_MIN_RXD		64
#define I40EVF_REQ_DESCRIPTOR_MULTIPLE	32
#define I40EVF_MAX_AQ_BUF_SIZE	4096
#define I40EVF_AQ_LEN		32
#define I40EVF_AQ_MAX_ERR	20 /* times to try before resetting AQ */

#define MAXIMUM_ETHERNET_VLAN_SIZE (VLAN_ETH_FRAME_LEN + ETH_FCS_LEN)

#define I40E_RX_DESC(R, i) (&(((union i40e_32byte_rx_desc *)((R)->desc))[i]))
#define I40E_TX_DESC(R, i) (&(((struct i40e_tx_desc *)((R)->desc))[i]))
#define I40E_TX_CTXTDESC(R, i) \
	(&(((struct i40e_tx_context_desc *)((R)->desc))[i]))
#define MAX_QUEUES 16
#define I40EVF_MAX_REQ_QUEUES 4

#define I40EVF_HKEY_ARRAY_SIZE ((I40E_VFQF_HKEY_MAX_INDEX + 1) * 4)
#define I40EVF_HLUT_ARRAY_SIZE ((I40E_VFQF_HLUT_MAX_INDEX + 1) * 4)

/* MAX_MSIX_Q_VECTORS of these are allocated,
 * but we only use one per queue-specific vector.
 */
struct i40e_q_vector {
	struct i40evf_adapter *adapter;
	struct i40e_vsi *vsi;
	struct napi_struct napi;
	unsigned long reg_idx;
	struct i40e_ring_container rx;
	struct i40e_ring_container tx;
	u32 ring_mask;
	u8 num_ringpairs;	/* total number of ring pairs in vector */
#define ITR_COUNTDOWN_START 100
	u8 itr_countdown;	/* when 0 or 1 update ITR */
	int v_idx;	/* vector index in list */
	char name[IFNAMSIZ + 15];
	bool arm_wb_state;
	cpumask_t affinity_mask;
	struct irq_affinity_notify affinity_notify;
};

/* Helper macros to switch between ints/sec and what the register uses.
 * And yes, it's the same math going both ways.  The lowest value
 * supported by all of the i40e hardware is 8.
 */
#define EITR_INTS_PER_SEC_TO_REG(_eitr) \
	((_eitr) ? (1000000000 / ((_eitr) * 256)) : 8)
#define EITR_REG_TO_INTS_PER_SEC EITR_INTS_PER_SEC_TO_REG

#define I40EVF_DESC_UNUSED(R) \
	((((R)->next_to_clean > (R)->next_to_use) ? 0 : (R)->count) + \
	(R)->next_to_clean - (R)->next_to_use - 1)

#define I40EVF_RX_DESC_ADV(R, i)	\
	(&(((union i40e_adv_rx_desc *)((R).desc))[i]))
#define I40EVF_TX_DESC_ADV(R, i)	\
	(&(((union i40e_adv_tx_desc *)((R).desc))[i]))
#define I40EVF_TX_CTXTDESC_ADV(R, i)	\
	(&(((struct i40e_adv_tx_context_desc *)((R).desc))[i]))

#define OTHER_VECTOR 1
#define NONQ_VECS (OTHER_VECTOR)

#define MIN_MSIX_Q_VECTORS 1
#define MIN_MSIX_COUNT (MIN_MSIX_Q_VECTORS + NONQ_VECS)

#define I40EVF_QUEUE_END_OF_LIST 0x7FF
#define I40EVF_FREE_VECTOR 0x7FFF
struct i40evf_mac_filter {
	struct list_head list;
	u8 macaddr[ETH_ALEN];
	bool remove;		/* filter needs to be removed */
	bool add;		/* filter needs to be added */
};

struct i40evf_vlan_filter {
	struct list_head list;
	u16 vlan;
	bool remove;		/* filter needs to be removed */
	bool add;		/* filter needs to be added */
};

/* Driver state. The order of these is important! */
enum i40evf_state_t {
	__I40EVF_STARTUP,		/* driver loaded, probe complete */
	__I40EVF_REMOVE,		/* driver is being unloaded */
	__I40EVF_INIT_VERSION_CHECK,	/* aq msg sent, awaiting reply */
	__I40EVF_INIT_GET_RESOURCES,	/* aq msg sent, awaiting reply */
	__I40EVF_INIT_SW,		/* got resources, setting up structs */
	__I40EVF_RESETTING,		/* in reset */
	/* Below here, watchdog is running */
	__I40EVF_DOWN,			/* ready, can be opened */
	__I40EVF_DOWN_PENDING,		/* descending, waiting for watchdog */
	__I40EVF_TESTING,		/* in ethtool self-test */
	__I40EVF_RUNNING,		/* opened, working */
};

enum i40evf_critical_section_t {
	__I40EVF_IN_CRITICAL_TASK,	/* cannot be interrupted */
	__I40EVF_IN_CLIENT_TASK,
};

/* board specific private data structure */
struct i40evf_adapter {
	struct timer_list watchdog_timer;
	struct work_struct reset_task;
	struct work_struct adminq_task;
	struct delayed_work client_task;
	struct delayed_work init_task;
	wait_queue_head_t down_waitqueue;
	struct i40e_q_vector *q_vectors;
	struct list_head vlan_filter_list;
	struct list_head mac_filter_list;
	/* Lock to protect accesses to MAC and VLAN lists */
	spinlock_t mac_vlan_list_lock;
	char misc_vector_name[IFNAMSIZ + 9];
	int num_active_queues;
	int num_req_queues;

	/* TX */
	struct i40e_ring *tx_rings;
	u32 tx_timeout_count;
	u32 tx_desc_count;

	/* RX */
	struct i40e_ring *rx_rings;
	u64 hw_csum_rx_error;
	u32 rx_desc_count;
	int num_msix_vectors;
	int num_iwarp_msix;
	int iwarp_base_vector;
	u32 client_pending;
	struct i40e_client_instance *cinst;
	struct msix_entry *msix_entries;

	u32 flags;
#define I40EVF_FLAG_RX_CSUM_ENABLED		BIT(0)
#define I40EVF_FLAG_IMIR_ENABLED		BIT(1)
#define I40EVF_FLAG_MQ_CAPABLE			BIT(2)
#define I40EVF_FLAG_PF_COMMS_FAILED		BIT(3)
#define I40EVF_FLAG_RESET_PENDING		BIT(4)
#define I40EVF_FLAG_RESET_NEEDED		BIT(5)
#define I40EVF_FLAG_WB_ON_ITR_CAPABLE		BIT(6)
#define I40EVF_FLAG_OUTER_UDP_CSUM_CAPABLE	BIT(7)
#define I40EVF_FLAG_ADDR_SET_BY_PF		BIT(8)
#define I40EVF_FLAG_SERVICE_CLIENT_REQUESTED	BIT(9)
#define I40EVF_FLAG_CLIENT_NEEDS_OPEN		BIT(10)
#define I40EVF_FLAG_CLIENT_NEEDS_CLOSE		BIT(11)
#define I40EVF_FLAG_CLIENT_NEEDS_L2_PARAMS	BIT(12)
#define I40EVF_FLAG_PROMISC_ON			BIT(13)
#define I40EVF_FLAG_ALLMULTI_ON			BIT(14)
#define I40EVF_FLAG_LEGACY_RX			BIT(15)
#define I40EVF_FLAG_REINIT_ITR_NEEDED		BIT(16)
/* duplicates for common code */
#define I40E_FLAG_DCB_ENABLED			0
#define I40E_FLAG_RX_CSUM_ENABLED		I40EVF_FLAG_RX_CSUM_ENABLED
#define I40E_FLAG_LEGACY_RX			I40EVF_FLAG_LEGACY_RX
	/* flags for admin queue service task */
	u32 aq_required;
#define I40EVF_FLAG_AQ_ENABLE_QUEUES		BIT(0)
#define I40EVF_FLAG_AQ_DISABLE_QUEUES		BIT(1)
#define I40EVF_FLAG_AQ_ADD_MAC_FILTER		BIT(2)
#define I40EVF_FLAG_AQ_ADD_VLAN_FILTER		BIT(3)
#define I40EVF_FLAG_AQ_DEL_MAC_FILTER		BIT(4)
#define I40EVF_FLAG_AQ_DEL_VLAN_FILTER		BIT(5)
#define I40EVF_FLAG_AQ_CONFIGURE_QUEUES		BIT(6)
#define I40EVF_FLAG_AQ_MAP_VECTORS		BIT(7)
#define I40EVF_FLAG_AQ_HANDLE_RESET		BIT(8)
#define I40EVF_FLAG_AQ_CONFIGURE_RSS		BIT(9) /* direct AQ config */
#define I40EVF_FLAG_AQ_GET_CONFIG		BIT(10)
/* Newer style, RSS done by the PF so we can ignore hardware vagaries. */
#define I40EVF_FLAG_AQ_GET_HENA			BIT(11)
#define I40EVF_FLAG_AQ_SET_HENA			BIT(12)
#define I40EVF_FLAG_AQ_SET_RSS_KEY		BIT(13)
#define I40EVF_FLAG_AQ_SET_RSS_LUT		BIT(14)
#define I40EVF_FLAG_AQ_REQUEST_PROMISC		BIT(15)
#define I40EVF_FLAG_AQ_RELEASE_PROMISC		BIT(16)
#define I40EVF_FLAG_AQ_REQUEST_ALLMULTI		BIT(17)
#define I40EVF_FLAG_AQ_RELEASE_ALLMULTI		BIT(18)
#define I40EVF_FLAG_AQ_ENABLE_VLAN_STRIPPING	BIT(19)
#define I40EVF_FLAG_AQ_DISABLE_VLAN_STRIPPING	BIT(20)

	/* OS defined structs */
	struct net_device *netdev;
	struct pci_dev *pdev;

	struct i40e_hw hw; /* defined in i40e_type.h */

	enum i40evf_state_t state;
	unsigned long crit_section;

	struct work_struct watchdog_task;
	bool netdev_registered;
	bool link_up;
	enum virtchnl_link_speed link_speed;
	enum virtchnl_ops current_op;
#define CLIENT_ALLOWED(_a) ((_a)->vf_res ? \
			    (_a)->vf_res->vf_cap_flags & \
				VIRTCHNL_VF_OFFLOAD_IWARP : \
			    0)
#define CLIENT_ENABLED(_a) ((_a)->cinst)
/* RSS by the PF should be preferred over RSS via other methods. */
#define RSS_PF(_a) ((_a)->vf_res->vf_cap_flags & \
		    VIRTCHNL_VF_OFFLOAD_RSS_PF)
#define RSS_AQ(_a) ((_a)->vf_res->vf_cap_flags & \
		    VIRTCHNL_VF_OFFLOAD_RSS_AQ)
#define RSS_REG(_a) (!((_a)->vf_res->vf_cap_flags & \
		       (VIRTCHNL_VF_OFFLOAD_RSS_AQ | \
			VIRTCHNL_VF_OFFLOAD_RSS_PF)))
#define VLAN_ALLOWED(_a) ((_a)->vf_res->vf_cap_flags & \
			  VIRTCHNL_VF_OFFLOAD_VLAN)
	struct virtchnl_vf_resource *vf_res; /* incl. all VSIs */
	struct virtchnl_vsi_resource *vsi_res; /* our LAN VSI */
	struct virtchnl_version_info pf_version;
#define PF_IS_V11(_a) (((_a)->pf_version.major == 1) && \
		       ((_a)->pf_version.minor == 1))
	u16 msg_enable;
	struct i40e_eth_stats current_stats;
	struct i40e_vsi vsi;
	u32 aq_wait_count;
	/* RSS stuff */
	u64 hena;
	u16 rss_key_size;
	u16 rss_lut_size;
	u8 *rss_key;
	u8 *rss_lut;
};


/* Ethtool Private Flags */

/* lan device */
struct i40e_device {
	struct list_head list;
	struct i40evf_adapter *vf;
};

/* needed by i40evf_ethtool.c */
extern char i40evf_driver_name[];
extern const char i40evf_driver_version[];

int i40evf_up(struct i40evf_adapter *adapter);
void i40evf_down(struct i40evf_adapter *adapter);
int i40evf_process_config(struct i40evf_adapter *adapter);
void i40evf_schedule_reset(struct i40evf_adapter *adapter);
void i40evf_reset(struct i40evf_adapter *adapter);
void i40evf_set_ethtool_ops(struct net_device *netdev);
void i40evf_update_stats(struct i40evf_adapter *adapter);
void i40evf_reset_interrupt_capability(struct i40evf_adapter *adapter);
int i40evf_init_interrupt_scheme(struct i40evf_adapter *adapter);
void i40evf_irq_enable_queues(struct i40evf_adapter *adapter, u32 mask);
void i40evf_free_all_tx_resources(struct i40evf_adapter *adapter);
void i40evf_free_all_rx_resources(struct i40evf_adapter *adapter);

void i40e_napi_add_all(struct i40evf_adapter *adapter);
void i40e_napi_del_all(struct i40evf_adapter *adapter);

int i40evf_send_api_ver(struct i40evf_adapter *adapter);
int i40evf_verify_api_ver(struct i40evf_adapter *adapter);
int i40evf_send_vf_config_msg(struct i40evf_adapter *adapter);
int i40evf_get_vf_config(struct i40evf_adapter *adapter);
void i40evf_irq_enable(struct i40evf_adapter *adapter, bool flush);
void i40evf_configure_queues(struct i40evf_adapter *adapter);
void i40evf_deconfigure_queues(struct i40evf_adapter *adapter);
void i40evf_enable_queues(struct i40evf_adapter *adapter);
void i40evf_disable_queues(struct i40evf_adapter *adapter);
void i40evf_map_queues(struct i40evf_adapter *adapter);
int i40evf_request_queues(struct i40evf_adapter *adapter, int num);
void i40evf_add_ether_addrs(struct i40evf_adapter *adapter);
void i40evf_del_ether_addrs(struct i40evf_adapter *adapter);
void i40evf_add_vlans(struct i40evf_adapter *adapter);
void i40evf_del_vlans(struct i40evf_adapter *adapter);
void i40evf_set_promiscuous(struct i40evf_adapter *adapter, int flags);
void i40evf_request_stats(struct i40evf_adapter *adapter);
void i40evf_request_reset(struct i40evf_adapter *adapter);
void i40evf_get_hena(struct i40evf_adapter *adapter);
void i40evf_set_hena(struct i40evf_adapter *adapter);
void i40evf_set_rss_key(struct i40evf_adapter *adapter);
void i40evf_set_rss_lut(struct i40evf_adapter *adapter);
void i40evf_enable_vlan_stripping(struct i40evf_adapter *adapter);
void i40evf_disable_vlan_stripping(struct i40evf_adapter *adapter);
void i40evf_virtchnl_completion(struct i40evf_adapter *adapter,
				enum virtchnl_ops v_opcode,
				i40e_status v_retval, u8 *msg, u16 msglen);
int i40evf_config_rss(struct i40evf_adapter *adapter);
int i40evf_lan_add_device(struct i40evf_adapter *adapter);
int i40evf_lan_del_device(struct i40evf_adapter *adapter);
void i40evf_client_subtask(struct i40evf_adapter *adapter);
void i40evf_notify_client_message(struct i40e_vsi *vsi, u8 *msg, u16 len);
void i40evf_notify_client_l2_params(struct i40e_vsi *vsi);
void i40evf_notify_client_open(struct i40e_vsi *vsi);
void i40evf_notify_client_close(struct i40e_vsi *vsi, bool reset);
#endif /* _I40EVF_H_ */
