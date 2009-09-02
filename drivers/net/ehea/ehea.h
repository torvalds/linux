/*
 *  linux/drivers/net/ehea/ehea.h
 *
 *  eHEA ethernet device driver for IBM eServer System p
 *
 *  (C) Copyright IBM Corp. 2006
 *
 *  Authors:
 *       Christoph Raisch <raisch@de.ibm.com>
 *       Jan-Bernd Themann <themann@de.ibm.com>
 *       Thomas Klein <tklein@de.ibm.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __EHEA_H__
#define __EHEA_H__

#include <linux/module.h>
#include <linux/ethtool.h>
#include <linux/vmalloc.h>
#include <linux/if_vlan.h>
#include <linux/inet_lro.h>

#include <asm/ibmebus.h>
#include <asm/abs_addr.h>
#include <asm/io.h>

#define DRV_NAME	"ehea"
#define DRV_VERSION	"EHEA_0102"

/* eHEA capability flags */
#define DLPAR_PORT_ADD_REM 1
#define DLPAR_MEM_ADD      2
#define DLPAR_MEM_REM      4
#define EHEA_CAPABILITIES  (DLPAR_PORT_ADD_REM | DLPAR_MEM_ADD | DLPAR_MEM_REM)

#define EHEA_MSG_DEFAULT (NETIF_MSG_LINK | NETIF_MSG_TIMER \
	| NETIF_MSG_RX_ERR | NETIF_MSG_TX_ERR)

#define EHEA_MAX_ENTRIES_RQ1 32767
#define EHEA_MAX_ENTRIES_RQ2 16383
#define EHEA_MAX_ENTRIES_RQ3 16383
#define EHEA_MAX_ENTRIES_SQ  32767
#define EHEA_MIN_ENTRIES_QP  127

#define EHEA_SMALL_QUEUES
#define EHEA_NUM_TX_QP 1
#define EHEA_LRO_MAX_AGGR 64

#ifdef EHEA_SMALL_QUEUES
#define EHEA_MAX_CQE_COUNT      1023
#define EHEA_DEF_ENTRIES_SQ     1023
#define EHEA_DEF_ENTRIES_RQ1    4095
#define EHEA_DEF_ENTRIES_RQ2    1023
#define EHEA_DEF_ENTRIES_RQ3    1023
#else
#define EHEA_MAX_CQE_COUNT      4080
#define EHEA_DEF_ENTRIES_SQ     4080
#define EHEA_DEF_ENTRIES_RQ1    8160
#define EHEA_DEF_ENTRIES_RQ2    2040
#define EHEA_DEF_ENTRIES_RQ3    2040
#endif

#define EHEA_MAX_ENTRIES_EQ 20

#define EHEA_SG_SQ  2
#define EHEA_SG_RQ1 1
#define EHEA_SG_RQ2 0
#define EHEA_SG_RQ3 0

#define EHEA_MAX_PACKET_SIZE    9022	/* for jumbo frames */
#define EHEA_RQ2_PKT_SIZE       1522
#define EHEA_L_PKT_SIZE         256	/* low latency */

#define MAX_LRO_DESCRIPTORS 8

/* Send completion signaling */

/* Protection Domain Identifier */
#define EHEA_PD_ID        0xaabcdeff

#define EHEA_RQ2_THRESHOLD 	   1
#define EHEA_RQ3_THRESHOLD 	   9	/* use RQ3 threshold of 1522 bytes */

#define EHEA_SPEED_10G         10000
#define EHEA_SPEED_1G           1000
#define EHEA_SPEED_100M          100
#define EHEA_SPEED_10M            10
#define EHEA_SPEED_AUTONEG         0

/* Broadcast/Multicast registration types */
#define EHEA_BCMC_SCOPE_ALL	0x08
#define EHEA_BCMC_SCOPE_SINGLE	0x00
#define EHEA_BCMC_MULTICAST	0x04
#define EHEA_BCMC_BROADCAST	0x00
#define EHEA_BCMC_UNTAGGED	0x02
#define EHEA_BCMC_TAGGED	0x00
#define EHEA_BCMC_VLANID_ALL	0x01
#define EHEA_BCMC_VLANID_SINGLE	0x00

#define EHEA_CACHE_LINE          128

/* Memory Regions */
#define EHEA_MR_ACC_CTRL       0x00800000

#define EHEA_BUSMAP_START      0x8000000000000000ULL
#define EHEA_INVAL_ADDR        0xFFFFFFFFFFFFFFFFULL
#define EHEA_DIR_INDEX_SHIFT 13                   /* 8k Entries in 64k block */
#define EHEA_TOP_INDEX_SHIFT (EHEA_DIR_INDEX_SHIFT * 2)
#define EHEA_MAP_ENTRIES (1 << EHEA_DIR_INDEX_SHIFT)
#define EHEA_MAP_SIZE (0x10000)                   /* currently fixed map size */
#define EHEA_INDEX_MASK (EHEA_MAP_ENTRIES - 1)


#define EHEA_WATCH_DOG_TIMEOUT 10*HZ

/* utility functions */

#define ehea_info(fmt, args...) \
	printk(KERN_INFO DRV_NAME ": " fmt "\n", ## args)

#define ehea_error(fmt, args...) \
	printk(KERN_ERR DRV_NAME ": Error in %s: " fmt "\n", __func__, ## args)

#ifdef DEBUG
#define ehea_debug(fmt, args...) \
	printk(KERN_DEBUG DRV_NAME ": " fmt, ## args)
#else
#define ehea_debug(fmt, args...) do {} while (0)
#endif

void ehea_dump(void *adr, int len, char *msg);

#define EHEA_BMASK(pos, length) (((pos) << 16) + (length))

#define EHEA_BMASK_IBM(from, to) (((63 - to) << 16) + ((to) - (from) + 1))

#define EHEA_BMASK_SHIFTPOS(mask) (((mask) >> 16) & 0xffff)

#define EHEA_BMASK_MASK(mask) \
	(0xffffffffffffffffULL >> ((64 - (mask)) & 0xffff))

#define EHEA_BMASK_SET(mask, value) \
	((EHEA_BMASK_MASK(mask) & ((u64)(value))) << EHEA_BMASK_SHIFTPOS(mask))

#define EHEA_BMASK_GET(mask, value) \
	(EHEA_BMASK_MASK(mask) & (((u64)(value)) >> EHEA_BMASK_SHIFTPOS(mask)))

/*
 * Generic ehea page
 */
struct ehea_page {
	u8 entries[PAGE_SIZE];
};

/*
 * Generic queue in linux kernel virtual memory
 */
struct hw_queue {
	u64 current_q_offset;		/* current queue entry */
	struct ehea_page **queue_pages;	/* array of pages belonging to queue */
	u32 qe_size;			/* queue entry size */
	u32 queue_length;      		/* queue length allocated in bytes */
	u32 pagesize;
	u32 toggle_state;		/* toggle flag - per page */
	u32 reserved;			/* 64 bit alignment */
};

/*
 * For pSeries this is a 64bit memory address where
 * I/O memory is mapped into CPU address space
 */
struct h_epa {
	void __iomem *addr;
};

struct h_epa_user {
	u64 addr;
};

struct h_epas {
	struct h_epa kernel;	/* kernel space accessible resource,
				   set to 0 if unused */
	struct h_epa_user user;	/* user space accessible resource
				   set to 0 if unused */
};

/*
 * Memory map data structures
 */
struct ehea_dir_bmap
{
	u64 ent[EHEA_MAP_ENTRIES];
};
struct ehea_top_bmap
{
	struct ehea_dir_bmap *dir[EHEA_MAP_ENTRIES];
};
struct ehea_bmap
{
	struct ehea_top_bmap *top[EHEA_MAP_ENTRIES];
};

struct ehea_qp;
struct ehea_cq;
struct ehea_eq;
struct ehea_port;
struct ehea_av;

/*
 * Queue attributes passed to ehea_create_qp()
 */
struct ehea_qp_init_attr {
	/* input parameter */
	u32 qp_token;           /* queue token */
	u8 low_lat_rq1;
	u8 signalingtype;       /* cqe generation flag */
	u8 rq_count;            /* num of receive queues */
	u8 eqe_gen;             /* eqe generation flag */
	u16 max_nr_send_wqes;   /* max number of send wqes */
	u16 max_nr_rwqes_rq1;   /* max number of receive wqes */
	u16 max_nr_rwqes_rq2;
	u16 max_nr_rwqes_rq3;
	u8 wqe_size_enc_sq;
	u8 wqe_size_enc_rq1;
	u8 wqe_size_enc_rq2;
	u8 wqe_size_enc_rq3;
	u8 swqe_imm_data_len;   /* immediate data length for swqes */
	u16 port_nr;
	u16 rq2_threshold;
	u16 rq3_threshold;
	u64 send_cq_handle;
	u64 recv_cq_handle;
	u64 aff_eq_handle;

	/* output parameter */
	u32 qp_nr;
	u16 act_nr_send_wqes;
	u16 act_nr_rwqes_rq1;
	u16 act_nr_rwqes_rq2;
	u16 act_nr_rwqes_rq3;
	u8 act_wqe_size_enc_sq;
	u8 act_wqe_size_enc_rq1;
	u8 act_wqe_size_enc_rq2;
	u8 act_wqe_size_enc_rq3;
	u32 nr_sq_pages;
	u32 nr_rq1_pages;
	u32 nr_rq2_pages;
	u32 nr_rq3_pages;
	u32 liobn_sq;
	u32 liobn_rq1;
	u32 liobn_rq2;
	u32 liobn_rq3;
};

/*
 * Event Queue attributes, passed as parameter
 */
struct ehea_eq_attr {
	u32 type;
	u32 max_nr_of_eqes;
	u8 eqe_gen;        /* generate eqe flag */
	u64 eq_handle;
	u32 act_nr_of_eqes;
	u32 nr_pages;
	u32 ist1;          /* Interrupt service token */
	u32 ist2;
	u32 ist3;
	u32 ist4;
};


/*
 * Event Queue
 */
struct ehea_eq {
	struct ehea_adapter *adapter;
	struct hw_queue hw_queue;
	u64 fw_handle;
	struct h_epas epas;
	spinlock_t spinlock;
	struct ehea_eq_attr attr;
};

/*
 * HEA Queues
 */
struct ehea_qp {
	struct ehea_adapter *adapter;
	u64 fw_handle;			/* QP handle for firmware calls */
	struct hw_queue hw_squeue;
	struct hw_queue hw_rqueue1;
	struct hw_queue hw_rqueue2;
	struct hw_queue hw_rqueue3;
	struct h_epas epas;
	struct ehea_qp_init_attr init_attr;
};

/*
 * Completion Queue attributes
 */
struct ehea_cq_attr {
	/* input parameter */
	u32 max_nr_of_cqes;
	u32 cq_token;
	u64 eq_handle;

	/* output parameter */
	u32 act_nr_of_cqes;
	u32 nr_pages;
};

/*
 * Completion Queue
 */
struct ehea_cq {
	struct ehea_adapter *adapter;
	u64 fw_handle;
	struct hw_queue hw_queue;
	struct h_epas epas;
	struct ehea_cq_attr attr;
};

/*
 * Memory Region
 */
struct ehea_mr {
	struct ehea_adapter *adapter;
	u64 handle;
	u64 vaddr;
	u32 lkey;
};

/*
 * Port state information
 */
struct port_stats {
	int poll_receive_errors;
	int queue_stopped;
	int err_tcp_cksum;
	int err_ip_cksum;
	int err_frame_crc;
};

#define EHEA_IRQ_NAME_SIZE 20

/*
 * Queue SKB Array
 */
struct ehea_q_skb_arr {
	struct sk_buff **arr;		/* skb array for queue */
	int len;                	/* array length */
	int index;			/* array index */
	int os_skbs;			/* rq2/rq3 only: outstanding skbs */
};

/*
 * Port resources
 */
struct ehea_port_res {
	struct napi_struct napi;
	struct port_stats p_stats;
	struct ehea_mr send_mr;       	/* send memory region */
	struct ehea_mr recv_mr;       	/* receive memory region */
	spinlock_t xmit_lock;
	struct ehea_port *port;
	char int_recv_name[EHEA_IRQ_NAME_SIZE];
	char int_send_name[EHEA_IRQ_NAME_SIZE];
	struct ehea_qp *qp;
	struct ehea_cq *send_cq;
	struct ehea_cq *recv_cq;
	struct ehea_eq *eq;
	struct ehea_q_skb_arr rq1_skba;
	struct ehea_q_skb_arr rq2_skba;
	struct ehea_q_skb_arr rq3_skba;
	struct ehea_q_skb_arr sq_skba;
	int sq_skba_size;
	spinlock_t netif_queue;
	int queue_stopped;
	int swqe_refill_th;
	atomic_t swqe_avail;
	int swqe_ll_count;
	u32 swqe_id_counter;
	u64 tx_packets;
	u64 rx_packets;
	u32 poll_counter;
	struct net_lro_mgr lro_mgr;
	struct net_lro_desc lro_desc[MAX_LRO_DESCRIPTORS];
};


#define EHEA_MAX_PORTS 16

#define EHEA_NUM_PORTRES_FW_HANDLES    6  /* QP handle, SendCQ handle,
					     RecvCQ handle, EQ handle,
					     SendMR handle, RecvMR handle */
#define EHEA_NUM_PORT_FW_HANDLES       1  /* EQ handle */
#define EHEA_NUM_ADAPTER_FW_HANDLES    2  /* MR handle, NEQ handle */

struct ehea_adapter {
	u64 handle;
	struct of_device *ofdev;
	struct ehea_port *port[EHEA_MAX_PORTS];
	struct ehea_eq *neq;       /* notification event queue */
	struct tasklet_struct neq_tasklet;
	struct ehea_mr mr;
	u32 pd;                    /* protection domain */
	u64 max_mc_mac;            /* max number of multicast mac addresses */
	int active_ports;
	struct list_head list;
};


struct ehea_mc_list {
	struct list_head list;
	u64 macaddr;
};

/* kdump support */
struct ehea_fw_handle_entry {
	u64 adh;               /* Adapter Handle */
	u64 fwh;               /* Firmware Handle */
};

struct ehea_fw_handle_array {
	struct ehea_fw_handle_entry *arr;
	int num_entries;
	struct mutex lock;
};

struct ehea_bcmc_reg_entry {
	u64 adh;               /* Adapter Handle */
	u32 port_id;           /* Logical Port Id */
	u8 reg_type;           /* Registration Type */
	u64 macaddr;
};

struct ehea_bcmc_reg_array {
	struct ehea_bcmc_reg_entry *arr;
	int num_entries;
	spinlock_t lock;
};

#define EHEA_PORT_UP 1
#define EHEA_PORT_DOWN 0
#define EHEA_PHY_LINK_UP 1
#define EHEA_PHY_LINK_DOWN 0
#define EHEA_MAX_PORT_RES 16
struct ehea_port {
	struct ehea_adapter *adapter;	 /* adapter that owns this port */
	struct net_device *netdev;
	struct net_device_stats stats;
	struct ehea_port_res port_res[EHEA_MAX_PORT_RES];
	struct of_device  ofdev; /* Open Firmware Device */
	struct ehea_mc_list *mc_list;	 /* Multicast MAC addresses */
	struct vlan_group *vgrp;
	struct ehea_eq *qp_eq;
	struct work_struct reset_task;
	struct mutex port_lock;
	char int_aff_name[EHEA_IRQ_NAME_SIZE];
	int allmulti;			 /* Indicates IFF_ALLMULTI state */
	int promisc;		 	 /* Indicates IFF_PROMISC state */
	int num_tx_qps;
	int num_add_tx_qps;
	int num_mcs;
	int resets;
	unsigned long flags;
	u64 mac_addr;
	u32 logical_port_id;
	u32 port_speed;
	u32 msg_enable;
	u32 sig_comp_iv;
	u32 state;
	u32 lro_max_aggr;
	u8 phy_link;
	u8 full_duplex;
	u8 autoneg;
	u8 num_def_qps;
};

struct port_res_cfg {
	int max_entries_rcq;
	int max_entries_scq;
	int max_entries_sq;
	int max_entries_rq1;
	int max_entries_rq2;
	int max_entries_rq3;
};

enum ehea_flag_bits {
	__EHEA_STOP_XFER,
	__EHEA_DISABLE_PORT_RESET
};

void ehea_set_ethtool_ops(struct net_device *netdev);
int ehea_sense_port_attr(struct ehea_port *port);
int ehea_set_portspeed(struct ehea_port *port, u32 port_speed);

extern struct work_struct ehea_rereg_mr_task;

#endif	/* __EHEA_H__ */
