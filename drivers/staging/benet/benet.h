/*
 * Copyright (C) 2005 - 2008 ServerEngines
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Contact Information:
 * linux-drivers@serverengines.com
 *
 * ServerEngines
 * 209 N. Fair Oaks Ave
 * Sunnyvale, CA 94085
 */
#ifndef _BENET_H_
#define _BENET_H_

#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/inet_lro.h>
#include "hwlib.h"

#define _SA_MODULE_NAME "net-driver"

#define VLAN_VALID_BIT		0x8000
#define BE_NUM_VLAN_SUPPORTED	32
#define BE_PORT_LINK_DOWN       0000
#define BE_PORT_LINK_UP         0001
#define	BE_MAX_TX_FRAG_COUNT		(30)

/* Flag bits for send operation */
#define IPCS            (1 << 0)	/* Enable IP checksum offload */
#define UDPCS           (1 << 1)	/* Enable UDP checksum offload */
#define TCPCS           (1 << 2)	/* Enable TCP checksum offload */
#define LSO             (1 << 3)	/* Enable Large Segment  offload */
#define ETHVLAN         (1 << 4)	/* Enable VLAN insert */
#define ETHEVENT        (1 << 5)	/* Generate  event on completion */
#define ETHCOMPLETE     (1 << 6)	/* Generate completion when done */
#define IPSEC           (1 << 7)	/* Enable IPSEC */
#define FORWARD         (1 << 8)	/* Send the packet in forwarding path */
#define FIN             (1 << 9)	/* Issue FIN segment */

#define BE_MAX_MTU	8974

#define BE_MAX_LRO_DESCRIPTORS			8
#define BE_LRO_MAX_PKTS				64
#define BE_MAX_FRAGS_PER_FRAME			6

extern const char be_drvr_ver[];
extern char be_fw_ver[];
extern char be_driver_name[];

extern struct ethtool_ops be_ethtool_ops;

#define BE_DEV_STATE_NONE 0
#define BE_DEV_STATE_INIT 1
#define BE_DEV_STATE_OPEN 2
#define BE_DEV_STATE_SUSPEND 3

/* This structure is used to describe physical fragments to use
 * for DMAing data from NIC.
 */
struct be_recv_buffer {
	struct list_head rxb_list;	/* for maintaining a linked list */
	void *rxb_va;		/* buffer virtual address */
	u32 rxb_pa_lo;		/* low part of physical address */
	u32 rxb_pa_hi;		/* high part of physical address */
	u32 rxb_len;		/* length of recv buffer */
	void *rxb_ctxt;		/* context for OSM driver to use */
};

/*
 * fragment list to describe scattered data.
 */
struct be_tx_frag_list {
	u32 txb_len;		/* Size of this fragment */
	u32 txb_pa_lo;		/* Lower 32 bits of 64 bit physical addr */
	u32 txb_pa_hi;		/* Higher 32 bits of 64 bit physical addr */
};

struct be_rx_page_info {
	struct page *page;
	dma_addr_t bus;
	u16 page_offset;
};

/*
 *  This structure is the main tracking structure for a NIC interface.
 */
struct be_net_object {
	/* MCC Ring - used to send fwcmds to embedded ARM processor */
	struct MCC_WRB_AMAP *mcc_q;	/* VA of the start of the ring */
	u32 mcc_q_len;			/* # of WRB entries in this ring */
	u32 mcc_q_size;
	u32 mcc_q_hd;			/* MCC ring head */
	u8 mcc_q_created;		/* flag to help cleanup */
	struct be_mcc_object mcc_q_obj;	/* BECLIB's MCC ring Object */
	dma_addr_t mcc_q_bus;		/* DMA'ble bus address */

	/* MCC Completion Ring - FW responses to fwcmds sent from MCC ring */
	struct MCC_CQ_ENTRY_AMAP *mcc_cq; /* VA of the start of the ring */
	u32 mcc_cq_len;			/* # of compl. entries in this ring */
	u32 mcc_cq_size;
	u32 mcc_cq_tl;			/* compl. ring tail */
	u8 mcc_cq_created;		/* flag to help cleanup */
	struct be_cq_object mcc_cq_obj;	/* BECLIB's MCC compl. ring object */
	u32 mcc_cq_id;			/* MCC ring ID */
	dma_addr_t mcc_cq_bus;		/* DMA'ble bus address */

	struct ring_desc mb_rd;		/* RD for MCC_MAIL_BOX */
	void *mb_ptr;			/* mailbox ptr to be freed  */
	dma_addr_t mb_bus;		/* DMA'ble bus address */
	u32 mb_size;

	/* BEClib uses an array of context objects to track outstanding
	 * requests to the MCC.  We need allocate the same number of
	 * conext entries as the number of entries in the MCC WRB ring
	 */
	u32 mcc_wrb_ctxt_size;
	void *mcc_wrb_ctxt;		/* pointer to the context area */
	u32 mcc_wrb_ctxtLen;		/* Number of entries in the context */
	/*
	 * NIC send request ring - used for xmitting raw ether frames.
	 */
	struct ETH_WRB_AMAP *tx_q;	/* VA of the start of the ring */
	u32 tx_q_len;			/* # if entries in the send ring */
	u32 tx_q_size;
	u32 tx_q_hd;			/* Head index. Next req. goes here */
	u32 tx_q_tl;			/* Tail indx. oldest outstanding req. */
	u8 tx_q_created;		/* flag to help cleanup */
	struct be_ethsq_object tx_q_obj;/* BECLIB's send Q handle */
	dma_addr_t tx_q_bus;		/* DMA'ble bus address */
	u32 tx_q_id;			/* send queue ring ID */
	u32 tx_q_port;			/* 0 no binding, 1 port A,  2 port B */
	atomic_t tx_q_used;		/* # of WRBs used */
	/* ptr to an array in which we store context info for each send req. */
	void **tx_ctxt;
	/*
	 * NIC Send compl. ring - completion status for all NIC frames xmitted.
	 */
	struct ETH_TX_COMPL_AMAP *tx_cq;/* VA of start of the ring */
	u32 txcq_len;			/* # of entries in the ring */
	u32 tx_cq_size;
	/*
	 * index into compl ring where the host expects next completion entry
	 */
	u32 tx_cq_tl;
	u32 tx_cq_id;			/* completion queue id */
	u8 tx_cq_created;		/* flag to help cleanup */
	struct be_cq_object tx_cq_obj;
	dma_addr_t tx_cq_bus;		/* DMA'ble bus address */
	/*
	 * Event Queue - all completion entries post events here.
	 */
	struct EQ_ENTRY_AMAP *event_q;	/* VA of start of event queue */
	u32 event_q_len;		/* # of entries */
	u32 event_q_size;
	u32 event_q_tl;			/* Tail of the event queue */
	u32 event_q_id;			/* Event queue ID */
	u8 event_q_created;		/* flag to help cleanup */
	struct be_eq_object event_q_obj; /* Queue handle */
	dma_addr_t event_q_bus;		/* DMA'ble bus address */
	/*
	 * NIC receive queue - Data buffers to be used for receiving unicast,
	 * broadcast and multi-cast frames  are posted here.
	 */
	struct ETH_RX_D_AMAP *rx_q;	/* VA of start of the queue */
	u32 rx_q_len;			/* # of entries */
	u32 rx_q_size;
	u32 rx_q_hd;			/* Head of the queue */
	atomic_t rx_q_posted;		/* number of posted buffers */
	u32 rx_q_id;			/* queue ID */
	u8 rx_q_created;		/* flag to help cleanup */
	struct be_ethrq_object rx_q_obj;	/* NIC RX queue handle */
	dma_addr_t rx_q_bus;		/* DMA'ble bus address */
	/*
	 * Pointer to an array of opaque context object for use by OSM driver
	 */
	void **rx_ctxt;
	/*
	 * NIC unicast RX completion queue - all unicast ether frame completion
	 * statuses from BE come here.
	 */
	struct ETH_RX_COMPL_AMAP *rx_cq;	/* VA of start of the queue */
	u32 rx_cq_len;		/* # of entries */
	u32 rx_cq_size;
	u32 rx_cq_tl;			/* Tail of the queue */
	u32 rx_cq_id;			/* queue ID */
	u8 rx_cq_created;		/* flag to help cleanup */
	struct be_cq_object rx_cq_obj;	/* queue handle */
	dma_addr_t rx_cq_bus;		/* DMA'ble bus address */
	struct be_function_object fn_obj;	/* function object   */
	bool	fn_obj_created;
	u32 rx_buf_size;		/* Size of the RX buffers */

	struct net_device *netdev;
	struct be_recv_buffer eth_rx_bufs[256];	/* to pass Rx buffer
							   addresses */
	struct be_adapter *adapter;	/* Pointer to OSM adapter */
	u32 devno;		/* OSM, network dev no. */
	u32 use_port;		/* Current active port */
	struct be_rx_page_info *rx_page_info;	/* Array of Rx buf pages */
	u32 rx_pg_info_hd;	/* Head of queue */
	int rxbuf_post_fail;	/* RxBuff posting fail count */
	bool rx_pg_shared;	/* Is an allocsted page shared as two frags ? */
	struct vlan_group *vlan_grp;
	u32 num_vlans;		/* Number of vlans in BE's filter */
	u16 vlan_tag[BE_NUM_VLAN_SUPPORTED]; /* vlans currently configured */
	struct napi_struct napi;
	struct net_lro_mgr lro_mgr;
	struct net_lro_desc lro_desc[BE_MAX_LRO_DESCRIPTORS];
};

#define NET_FH(np)       (&(np)->fn_obj)

/*
 * BE driver statistics.
 */
struct be_drvr_stat {
	u32 bes_tx_reqs;	/* number of TX requests initiated */
	u32 bes_tx_fails;	/* number of TX requests that failed */
	u32 bes_fwd_reqs;	/* number of send reqs through forwarding i/f */
	u32 bes_tx_wrbs;	/* number of tx WRBs used */

	u32 bes_ints;		/* number of interrupts */
	u32 bes_polls;		/* number of times NAPI called poll function */
	u32 bes_events;		/* total evet entries processed */
	u32 bes_tx_events;	/* number of tx completion events  */
	u32 bes_rx_events;	/* number of ucast rx completion events  */
	u32 bes_tx_compl;	/* number of tx completion entries processed */
	u32 bes_rx_compl;	/* number of rx completion entries
				   processed */
	u32 bes_ethrx_post_fail;	/* number of ethrx buffer alloc
					   failures */
	/*
	 * number of non ether type II frames dropped where
	 * frame len > length field of Mac Hdr
	 */
	u32 bes_802_3_dropped_frames;
	/*
	 * number of non ether type II frames malformed where
	 * in frame len < length field of Mac Hdr
	 */
	u32 bes_802_3_malformed_frames;
	u32 bes_ips;		/*  interrupts / sec */
	u32 bes_prev_ints;	/* bes_ints at last IPS calculation  */
	u16 bes_eth_tx_rate;	/*  ETH TX rate - Mb/sec */
	u16 bes_eth_rx_rate;	/*  ETH RX rate - Mb/sec */
	u32 bes_rx_coal;	/* Num pkts coalasced */
	u32 bes_rx_flush;	/* Num times coalasced */
	u32 bes_link_change_physical;	/*Num of times physical link changed */
	u32 bes_link_change_virtual;	/*Num of times virtual link changed */
	u32 bes_rx_misc_pkts;	/* Misc pkts received */
};

/* Maximum interrupt delay (in microseconds) allowed */
#define MAX_EQD				120

/*
 * timer to prevent system shutdown hang for ever if h/w stops responding
 */
struct be_timer_ctxt {
	atomic_t get_stat_flag;
	struct timer_list get_stats_timer;
	unsigned long get_stat_sem_addr;
} ;

/* This structure is the main BladeEngine driver context.  */
struct be_adapter {
	struct net_device *netdevp;
	struct be_drvr_stat be_stat;
	struct net_device_stats benet_stats;

	/* PCI BAR mapped addresses */
	u8 __iomem *csr_va;	/* CSR */
	u8 __iomem *db_va;	/* Door  Bell  */
	u8 __iomem *pci_va;	/* PCI Config */

	struct tasklet_struct sts_handler;
	struct timer_list cq_timer;
	spinlock_t int_lock;	/* to protect the isr field in adapter */

	struct FWCMD_ETH_GET_STATISTICS *eth_statsp;
	/*
	 * This will enable the use of ethtool to enable or disable
	 * Checksum on Rx pkts to be obeyed or disobeyed.
	 * If this is true = 1, then whatever is the checksum on the
	 * Received pkt as per BE, it will be given to the stack.
	 * Else the stack will re calculate it.
	 */
	bool rx_csum;
	/*
	 * This will enable the use of ethtool to enable or disable
	 * Coalese on Rx pkts to be obeyed or disobeyed.
	 * If this is grater than 0 and less than 16 then coalascing
	 * is enabled else it is disabled
	 */
	u32 max_rx_coal;
	struct pci_dev *pdev;	/* Pointer to OS's PCI dvice */

	spinlock_t txq_lock;	/* to stop/wake queue based on tx_q_used */

	u32 isr;		/* copy of Intr status reg. */

	u32 port0_link_sts;	/* Port 0 link status */
	u32 port1_link_sts;	/* port 1 list status */
	struct BE_LINK_STATUS *be_link_sts;

	/* pointer to the first netobject of this adapter */
	struct be_net_object *net_obj;

	/*  Flags to indicate what to clean up */
	bool tasklet_started;
	bool isr_registered;
	/*
	 * adaptive interrupt coalescing (AIC) related
	 */
	bool enable_aic;	/* 1 if AIC is enabled */
	u16 min_eqd;		/* minimum EQ delay in usec */
	u16 max_eqd;		/* minimum EQ delay in usec */
	u16 cur_eqd;		/* current EQ delay in usec */
	/*
	 * book keeping for interrupt / sec and TX/RX rate calculation
	 */
	ulong ips_jiffies;	/* jiffies at last IPS calc */
	u32 eth_tx_bytes;
	ulong eth_tx_jiffies;
	u32 eth_rx_bytes;
	ulong eth_rx_jiffies;

	struct semaphore get_eth_stat_sem;

	/* timer ctxt to prevent shutdown hanging due to un-responsive BE */
	struct be_timer_ctxt timer_ctxt;

#define BE_MAX_MSIX_VECTORS             32
#define BE_MAX_REQ_MSIX_VECTORS         1 /* only one EQ in Linux driver */
	struct msix_entry msix_entries[BE_MAX_MSIX_VECTORS];
	bool msix_enabled;
	bool dma_64bit_cap;	/* the Device DAC capable  or not */
	u8 dev_state;	/* The current state of the device */
	u8 dev_pm_state; /* The State of device before going to suspend */
};

/*
 * Every second we look at the ints/sec and adjust eq_delay
 * between adapter->min_eqd and adapter->max_eqd to keep the ints/sec between
 * IPS_HI_WM and IPS_LO_WM.
 */
#define IPS_HI_WM	18000
#define IPS_LO_WM	8000


static inline void index_adv(u32 *index, u32 val,  u32 limit)
{
	BUG_ON(limit & (limit-1));
	*index = (*index + val) & (limit - 1);
}

static inline void index_inc(u32 *index, u32 limit)
{
	BUG_ON(limit & (limit-1));
	*index = (*index + 1) & (limit - 1);
}

static inline void be_adv_eq_tl(struct be_net_object *pnob)
{
	index_inc(&pnob->event_q_tl, pnob->event_q_len);
}

static inline void be_adv_txq_hd(struct be_net_object *pnob)
{
	index_inc(&pnob->tx_q_hd, pnob->tx_q_len);
}

static inline void be_adv_txq_tl(struct be_net_object *pnob)
{
	index_inc(&pnob->tx_q_tl, pnob->tx_q_len);
}

static inline void be_adv_txcq_tl(struct be_net_object *pnob)
{
	index_inc(&pnob->tx_cq_tl, pnob->txcq_len);
}

static inline void be_adv_rxq_hd(struct be_net_object *pnob)
{
	index_inc(&pnob->rx_q_hd, pnob->rx_q_len);
}

static inline void be_adv_rxcq_tl(struct be_net_object *pnob)
{
	index_inc(&pnob->rx_cq_tl, pnob->rx_cq_len);
}

static inline u32 tx_compl_lastwrb_idx_get(struct be_net_object *pnob)
{
	return (pnob->tx_q_tl + *(u32 *)&pnob->tx_ctxt[pnob->tx_q_tl] - 1)
		    & (pnob->tx_q_len - 1);
}

int benet_init(struct net_device *);
int be_ethtool_ioctl(struct net_device *, struct ifreq *);
struct net_device_stats *benet_get_stats(struct net_device *);
void be_process_intr(unsigned long context);
irqreturn_t be_int(int irq, void *dev);
void be_post_eth_rx_buffs(struct be_net_object *);
void be_get_stat_cb(void *, int, struct MCC_WRB_AMAP *);
void be_get_stats_timer_handler(unsigned long);
void be_wait_nic_tx_cmplx_cmpl(struct be_net_object *);
void be_print_link_info(struct BE_LINK_STATUS *);
void be_update_link_status(struct be_adapter *);
void be_init_procfs(struct be_adapter *);
void be_cleanup_procfs(struct be_adapter *);
int be_poll(struct napi_struct *, int);
struct ETH_RX_COMPL_AMAP *be_get_rx_cmpl(struct be_net_object *);
void be_notify_cmpl(struct be_net_object *, int, int, int);
void be_enable_intr(struct be_net_object *);
void be_enable_eq_intr(struct be_net_object *);
void be_disable_intr(struct be_net_object *);
void be_disable_eq_intr(struct be_net_object *);
int be_set_uc_mac_adr(struct be_net_object *, u8, u8, u8,
		    u8 *, mcc_wrb_cqe_callback, void *);
int be_get_flow_ctl(struct be_function_object *pFnObj, bool *, bool *);
void process_one_tx_compl(struct be_net_object *pnob, u32 end_idx);

#endif /* _BENET_H_ */
