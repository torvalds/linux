/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Virtual Function Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
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
#include <net/ip6_checksum.h>
#include <net/udp.h>

#include "i40e_type.h"
#include "i40e_virtchnl.h"
#include "i40e_txrx.h"

#define DEFAULT_DEBUG_LEVEL_SHIFT 3
#define PFX "i40evf: "
#define DPRINTK(nlevel, klevel, fmt, args...) \
	((void)((NETIF_MSG_##nlevel & adapter->msg_enable) && \
	printk(KERN_##klevel PFX "%s: %s: " fmt, adapter->netdev->name, \
		__func__ , ## args)))

/* dummy struct to make common code less painful */
struct i40e_vsi {
	struct i40evf_adapter *back;
	struct net_device *netdev;
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	u16 seid;
	u16 id;
	unsigned long state;
	int base_vector;
	u16 work_limit;
	/* high bit set means dynamic, use accessor routines to read/write.
	 * hardware only supports 2us resolution for the ITR registers.
	 * these values always store the USER setting, and must be converted
	 * before programming to a register.
	 */
	u16 rx_itr_setting;
	u16 tx_itr_setting;
};

/* How many Rx Buffers do we bundle into one write to the hardware ? */
#define I40EVF_RX_BUFFER_WRITE	16	/* Must be power of 2 */
#define I40EVF_DEFAULT_TXD   512
#define I40EVF_DEFAULT_RXD   512
#define I40EVF_MAX_TXD       4096
#define I40EVF_MIN_TXD       64
#define I40EVF_MAX_RXD       4096
#define I40EVF_MIN_RXD       64
#define I40EVF_REQ_DESCRIPTOR_MULTIPLE  8

/* Supported Rx Buffer Sizes */
#define I40EVF_RXBUFFER_64    64     /* Used for packet split */
#define I40EVF_RXBUFFER_128   128    /* Used for packet split */
#define I40EVF_RXBUFFER_256   256    /* Used for packet split */
#define I40EVF_RXBUFFER_2048  2048
#define I40EVF_MAX_RXBUFFER   16384  /* largest size for single descriptor */
#define I40EVF_MAX_AQ_BUF_SIZE    4096
#define I40EVF_AQ_LEN             32
#define I40EVF_AQ_MAX_ERR         10 /* times to try before resetting AQ */

#define MAXIMUM_ETHERNET_VLAN_SIZE (VLAN_ETH_FRAME_LEN + ETH_FCS_LEN)

#define I40E_RX_DESC(R, i) (&(((union i40e_32byte_rx_desc *)((R)->desc))[i]))
#define I40E_TX_DESC(R, i) (&(((struct i40e_tx_desc *)((R)->desc))[i]))
#define I40E_TX_CTXTDESC(R, i) \
	(&(((struct i40e_tx_context_desc *)((R)->desc))[i]))
#define MAX_RX_QUEUES 8
#define MAX_TX_QUEUES MAX_RX_QUEUES

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
	int v_idx;	  /* vector index in list */
	char name[IFNAMSIZ + 9];
	cpumask_var_t affinity_mask;
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

#define I40EVF_RX_DESC_ADV(R, i)	    \
	(&(((union i40e_adv_rx_desc *)((R).desc))[i]))
#define I40EVF_TX_DESC_ADV(R, i)	    \
	(&(((union i40e_adv_tx_desc *)((R).desc))[i]))
#define I40EVF_TX_CTXTDESC_ADV(R, i)	    \
	(&(((struct i40e_adv_tx_context_desc *)((R).desc))[i]))

#define OTHER_VECTOR 1
#define NONQ_VECS (OTHER_VECTOR)

#define MAX_MSIX_Q_VECTORS 4
#define MAX_MSIX_COUNT 5

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
	__I40EVF_TESTING,		/* in ethtool self-test */
	__I40EVF_RUNNING,		/* opened, working */
};

enum i40evf_critical_section_t {
	__I40EVF_IN_CRITICAL_TASK,	/* cannot be interrupted */
};
/* make common code happy */
#define __I40E_DOWN __I40EVF_DOWN

/* board specific private data structure */
struct i40evf_adapter {
	struct timer_list watchdog_timer;
	struct work_struct reset_task;
	struct work_struct adminq_task;
	struct delayed_work init_task;
	struct i40e_q_vector *q_vector[MAX_MSIX_Q_VECTORS];
	struct list_head vlan_filter_list;
	char misc_vector_name[IFNAMSIZ + 9];

	/* TX */
	struct i40e_ring *tx_rings[I40E_MAX_VSI_QP];
	u32 tx_timeout_count;
	struct list_head mac_filter_list;

	/* RX */
	struct i40e_ring *rx_rings[I40E_MAX_VSI_QP];
	u64 hw_csum_rx_error;
	int num_msix_vectors;
	struct msix_entry *msix_entries;

	u32 flags;
#define I40EVF_FLAG_RX_CSUM_ENABLED              (u32)(1)
#define I40EVF_FLAG_RX_1BUF_CAPABLE              (u32)(1 << 1)
#define I40EVF_FLAG_RX_PS_CAPABLE                (u32)(1 << 2)
#define I40EVF_FLAG_RX_PS_ENABLED                (u32)(1 << 3)
#define I40EVF_FLAG_IN_NETPOLL                   (u32)(1 << 4)
#define I40EVF_FLAG_IMIR_ENABLED                 (u32)(1 << 5)
#define I40EVF_FLAG_MQ_CAPABLE                   (u32)(1 << 6)
#define I40EVF_FLAG_NEED_LINK_UPDATE             (u32)(1 << 7)
#define I40EVF_FLAG_PF_COMMS_FAILED              (u32)(1 << 8)
#define I40EVF_FLAG_RESET_PENDING                (u32)(1 << 9)
#define I40EVF_FLAG_RESET_NEEDED                 (u32)(1 << 10)
/* duplcates for common code */
#define I40E_FLAG_FDIR_ATR_ENABLED		 0
#define I40E_FLAG_DCB_ENABLED			 0
#define I40E_FLAG_IN_NETPOLL			 I40EVF_FLAG_IN_NETPOLL
#define I40E_FLAG_RX_CSUM_ENABLED                I40EVF_FLAG_RX_CSUM_ENABLED
	/* flags for admin queue service task */
	u32 aq_required;
	u32 aq_pending;
#define I40EVF_FLAG_AQ_ENABLE_QUEUES		(u32)(1)
#define I40EVF_FLAG_AQ_DISABLE_QUEUES		(u32)(1 << 1)
#define I40EVF_FLAG_AQ_ADD_MAC_FILTER		(u32)(1 << 2)
#define I40EVF_FLAG_AQ_ADD_VLAN_FILTER		(u32)(1 << 3)
#define I40EVF_FLAG_AQ_DEL_MAC_FILTER		(u32)(1 << 4)
#define I40EVF_FLAG_AQ_DEL_VLAN_FILTER		(u32)(1 << 5)
#define I40EVF_FLAG_AQ_CONFIGURE_QUEUES		(u32)(1 << 6)
#define I40EVF_FLAG_AQ_MAP_VECTORS		(u32)(1 << 7)
#define I40EVF_FLAG_AQ_HANDLE_RESET		(u32)(1 << 8)

	/* OS defined structs */
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct net_device_stats net_stats;

	struct i40e_hw hw; /* defined in i40e_type.h */

	enum i40evf_state_t state;
	volatile unsigned long crit_section;

	struct work_struct watchdog_task;
	bool netdev_registered;
	bool link_up;
	enum i40e_virtchnl_ops current_op;
	struct i40e_virtchnl_vf_resource *vf_res; /* incl. all VSIs */
	struct i40e_virtchnl_vsi_resource *vsi_res; /* our LAN VSI */
	u16 msg_enable;
	struct i40e_eth_stats current_stats;
	struct i40e_vsi vsi;
	u32 aq_wait_count;
};


/* needed by i40evf_ethtool.c */
extern char i40evf_driver_name[];
extern const char i40evf_driver_version[];

int i40evf_up(struct i40evf_adapter *adapter);
void i40evf_down(struct i40evf_adapter *adapter);
void i40evf_reinit_locked(struct i40evf_adapter *adapter);
void i40evf_reset(struct i40evf_adapter *adapter);
void i40evf_set_ethtool_ops(struct net_device *netdev);
void i40evf_update_stats(struct i40evf_adapter *adapter);
void i40evf_reset_interrupt_capability(struct i40evf_adapter *adapter);
int i40evf_init_interrupt_scheme(struct i40evf_adapter *adapter);
void i40evf_irq_enable_queues(struct i40evf_adapter *adapter, u32 mask);

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
void i40evf_add_ether_addrs(struct i40evf_adapter *adapter);
void i40evf_del_ether_addrs(struct i40evf_adapter *adapter);
void i40evf_add_vlans(struct i40evf_adapter *adapter);
void i40evf_del_vlans(struct i40evf_adapter *adapter);
void i40evf_set_promiscuous(struct i40evf_adapter *adapter, int flags);
void i40evf_request_stats(struct i40evf_adapter *adapter);
void i40evf_request_reset(struct i40evf_adapter *adapter);
void i40evf_virtchnl_completion(struct i40evf_adapter *adapter,
				enum i40e_virtchnl_ops v_opcode,
				i40e_status v_retval, u8 *msg, u16 msglen);
#endif /* _I40EVF_H_ */
