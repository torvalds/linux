/*
 * @file Header file describing the flow rings DHD interfaces.
 *
 * Flow rings are transmit traffic (=propagating towards antenna) related entities.
 *
 * Provides type definitions and function prototypes used to create, delete and manage flow rings at
 * high level.
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

/** XXX Twiki: [PCIeFullDongleArchitecture] */

/****************
 * Common types *
 */

#ifndef _dhd_flowrings_h_
#define _dhd_flowrings_h_

/* Max pkts held in a flow ring's backup queue */
#define FLOW_RING_QUEUE_THRESHOLD       (2048)

/* Number of H2D common rings */
#define FLOW_RING_COMMON                BCMPCIE_H2D_COMMON_MSGRINGS

#define FLOWID_INVALID                  (ID16_INVALID)
#define FLOWID_RESERVED                 (FLOW_RING_COMMON)

#define FLOW_RING_STATUS_OPEN           0
#define FLOW_RING_STATUS_CREATE_PENDING	1
#define FLOW_RING_STATUS_CLOSED         2
#define FLOW_RING_STATUS_DELETE_PENDING 3
#define FLOW_RING_STATUS_FLUSH_PENDING  4

#ifdef IDLE_TX_FLOW_MGMT
#define FLOW_RING_STATUS_SUSPENDED	5
#define FLOW_RING_STATUS_RESUME_PENDING	6
#endif /* IDLE_TX_FLOW_MGMT */
#define FLOW_RING_STATUS_STA_FREEING    7

#if defined(DHD_HTPUT_TUNABLES)
#define HTPUT_FLOW_RING_PRIO		PRIO_8021D_BE
#define HTPUT_NUM_STA_FLOW_RINGS	1u
#define HTPUT_NUM_CLIENT_FLOW_RINGS	3u
#define HTPUT_TOTAL_FLOW_RINGS		(HTPUT_NUM_STA_FLOW_RINGS + HTPUT_NUM_CLIENT_FLOW_RINGS)
#define DHD_IS_FLOWID_HTPUT(pub, flowid) \
	((flowid >= (pub)->htput_flow_ring_start) && \
	(flowid < ((pub)->htput_flow_ring_start + HTPUT_TOTAL_FLOW_RINGS)))
#endif /* DHD_HTPUT_TUNABLES */

#ifdef DHD_EFI
/*
 * Each lbuf is of size 2048 bytes. But the last 112 bytes is occupied for lbuf header.
 * Since lbuf is crucial data structure we want to avoid operations very close to lbuf.
 * so providing a pad of 136 bytes. so lbuf and pad together is 248 bytes.
 *
 * So the maximum usable lbuf size is 1800 bytes.
 *
 * These 1800 bytes is utilized for below purposes.
 *
 * 1. FW operating in mode2 requires 98 bytes for extra headers
 * like SNAP, PLCP etc. Whereas FW operating in mode4 requires 70 bytes.
 * So in EFI DHD we will consider 98 bytes which fits for chips operating in both mode2 and mode4.
 *
 * 2. For TPUT tests in EFI user can request a maximum payload of 1500 bytes.
 * To add ethernet header and TPUT header etc we are reserving 100bytes. So 1600 bytes are utilized
 * for headers and payload.
 *
 * so 1698(98 + 1600) bytes by are consumed by 1 and 2.
 * So we still have 112 bytes which can be utilized
 * if FW needs buffer for more headers in future.
 *
 * --Update-- 13Jul2018 (above comments preserved for history)
 * 3.  In case of 11ax chips more headroom is required, FW requires a min. of 1920 bytes for Rx
 * buffers, or it will trap. Therefore bumping up the size to 1920 bytes. Which leaves
 * only 16 bytes pad between data and lbuf header ! Further size increase may not be possible !!
 */
#define DHD_FLOWRING_RX_BUFPOST_PKTSZ	1920
#else
#define DHD_FLOWRING_RX_BUFPOST_PKTSZ	2048
#endif /* DHD_EFI */

#define DHD_FLOWRING_RX_BUFPOST_PKTSZ_MAX 4096

#define DHD_FLOWRING_TX_BIG_PKT_SIZE	(3700u)

#define DHD_FLOW_PRIO_AC_MAP		0
#define DHD_FLOW_PRIO_TID_MAP		1
/* Flow ring prority map for lossless roaming */
#define DHD_FLOW_PRIO_LLR_MAP		2

/* Hashing a MacAddress for lkup into a per interface flow hash table */
#define DHD_FLOWRING_HASH_SIZE    256
#define	DHD_FLOWRING_HASHINDEX(ea, prio) \
	       ((((uint8 *)(ea))[3] ^ ((uint8 *)(ea))[4] ^ ((uint8 *)(ea))[5] ^ ((uint8)(prio))) \
		% DHD_FLOWRING_HASH_SIZE)

#define DHD_IF_ROLE(pub, idx)		(((if_flow_lkup_t *)(pub)->if_flow_lkup)[idx].role)
#define DHD_IF_ROLE_AP(pub, idx)	(DHD_IF_ROLE(pub, idx) == WLC_E_IF_ROLE_AP)
#define DHD_IF_ROLE_STA(pub, idx)	(DHD_IF_ROLE(pub, idx) == WLC_E_IF_ROLE_STA)
#define DHD_IF_ROLE_P2PGC(pub, idx)	(DHD_IF_ROLE(pub, idx) == WLC_E_IF_ROLE_P2P_CLIENT)
#define DHD_IF_ROLE_P2PGO(pub, idx)	(DHD_IF_ROLE(pub, idx) == WLC_E_IF_ROLE_P2P_GO)
#define DHD_IF_ROLE_WDS(pub, idx)	(DHD_IF_ROLE(pub, idx) == WLC_E_IF_ROLE_WDS)
#define DHD_IF_ROLE_IBSS(pub, idx)	(DHD_IF_ROLE(pub, idx) == WLC_E_IF_ROLE_IBSS)
#define DHD_IF_ROLE_NAN(pub, idx)	(DHD_IF_ROLE(pub, idx) == WLC_E_IF_ROLE_NAN)

#define DHD_IF_ROLE_GENERIC_STA(pub, idx) \
	(DHD_IF_ROLE_STA(pub, idx) || DHD_IF_ROLE_P2PGC(pub, idx) || DHD_IF_ROLE_WDS(pub, idx))

#ifdef DHD_AWDL
#define DHD_IF_ROLE_AWDL(pub, idx)	(DHD_IF_ROLE(pub, idx) == WLC_E_IF_ROLE_AWDL)
#define DHD_IF_ROLE_MULTI_CLIENT(pub, idx) \
	(DHD_IF_ROLE_AP(pub, idx) || DHD_IF_ROLE_P2PGO(pub, idx) || DHD_IF_ROLE_AWDL(pub, idx) ||\
		DHD_IF_ROLE_NAN(pub, idx))
#else
#define DHD_IF_ROLE_MULTI_CLIENT(pub, idx) \
	(DHD_IF_ROLE_AP(pub, idx) || DHD_IF_ROLE_P2PGO(pub, idx) ||\
		DHD_IF_ROLE_NAN(pub, idx))
#endif /* DHD_AWDL */

#define DHD_FLOW_RING(dhdp, flowid) \
	(flow_ring_node_t *)&(((flow_ring_node_t *)((dhdp)->flow_ring_table))[flowid])

struct flow_queue;

/* Flow Ring Queue Enqueue overflow callback */
typedef int (*flow_queue_cb_t)(struct flow_queue * queue, void * pkt);

/**
 * Each flow ring has an associated (tx flow controlled) queue. 802.3 packets are transferred
 * between queue and ring. A packet from the host stack is first added to the queue, and in a later
 * stage transferred to the flow ring. Packets in the queue are dhd owned, whereas packets in the
 * flow ring are device owned.
 */
typedef struct flow_queue {
	dll_t  list;                /* manage a flowring queue in a double linked list */
	void * head;                /* first packet in the queue */
	void * tail;                /* last packet in the queue */
	uint16 len;                 /* number of packets in the queue */
	uint16 max;                 /* maximum or min budget (used in cumm) */
	uint32 threshold;           /* parent's cummulative length threshold */
	void * clen_ptr;            /* parent's cummulative length counter */
	uint32 failures;            /* enqueue failures due to queue overflow */
	flow_queue_cb_t cb;         /* callback invoked on threshold crossing */
	uint32 l2threshold;         /* grandparent's (level 2) cummulative length threshold */
	void * l2clen_ptr;          /* grandparent's (level 2) cummulative length counter */
} flow_queue_t;

#define DHD_FLOW_QUEUE_LEN(queue)       ((int)(queue)->len)
#define DHD_FLOW_QUEUE_MAX(queue)       ((int)(queue)->max)
#define DHD_FLOW_QUEUE_THRESHOLD(queue) ((int)(queue)->threshold)
#define DHD_FLOW_QUEUE_L2THRESHOLD(queue) ((int)(queue)->l2threshold)
#define DHD_FLOW_QUEUE_EMPTY(queue)     ((queue)->len == 0)
#define DHD_FLOW_QUEUE_FAILURES(queue)  ((queue)->failures)

#define DHD_FLOW_QUEUE_AVAIL(queue)     ((int)((queue)->max - (queue)->len))
#define DHD_FLOW_QUEUE_FULL(queue)      ((queue)->len >= (queue)->max)

#define DHD_FLOW_QUEUE_OVFL(queue, budget)  \
	(((queue)->len) > budget)

#define DHD_FLOW_QUEUE_SET_MAX(queue, budget) \
	((queue)->max) = ((budget) - 1)

/* Queue's cummulative threshold. */
#define DHD_FLOW_QUEUE_SET_THRESHOLD(queue, cumm_threshold) \
	((queue)->threshold) = ((cumm_threshold) - 1)

/* Queue's cummulative length object accessor. */
#define DHD_FLOW_QUEUE_CLEN_PTR(queue)  ((queue)->clen_ptr)

/* Set a queue's cumm_len point to a parent's cumm_ctr_t cummulative length */
#define DHD_FLOW_QUEUE_SET_CLEN(queue, parent_clen_ptr)  \
	((queue)->clen_ptr) = (void *)(parent_clen_ptr)

/* Queue's level 2 cummulative threshold. */
#define DHD_FLOW_QUEUE_SET_L2THRESHOLD(queue, l2cumm_threshold) \
	((queue)->l2threshold) = ((l2cumm_threshold) - 1)

/* Queue's level 2 cummulative length object accessor. */
#define DHD_FLOW_QUEUE_L2CLEN_PTR(queue)  ((queue)->l2clen_ptr)

/* Set a queue's level 2 cumm_len point to a grandparent's cumm_ctr_t cummulative length */
#define DHD_FLOW_QUEUE_SET_L2CLEN(queue, grandparent_clen_ptr)  \
	((queue)->l2clen_ptr) = (void *)(grandparent_clen_ptr)

#if defined(BCMDBG)
#define DHD_FLOWRING_TXSTATUS_CNT_UPDATE(bus, flowid, txstatus) \
		dhd_bus_flow_ring_cnt_update(bus, flowid, txstatus)
#else
#define DHD_FLOWRING_TXSTATUS_CNT_UPDATE(bus, flowid, txstatus)
#endif /* BCMDBG */

/* Pkttag not compatible with PROP_TXSTATUS or WLFC */
typedef struct dhd_pkttag_fr {
	uint16  flowid;
	uint16  ifid;
} dhd_pkttag_fr_t;

#define DHD_PKTTAG_SET_IFID(tag, idx)       ((tag)->ifid = (uint16)(idx))
#define DHD_PKTTAG_SET_PA(tag, pa)          ((tag)->physaddr = (pa))
#define DHD_PKTTAG_SET_PA_LEN(tag, palen)   ((tag)->pa_len = (palen))
#define DHD_PKTTAG_IFID(tag)                ((tag)->ifid)
#define DHD_PKTTAG_PA(tag)                  ((tag)->physaddr)
#define DHD_PKTTAG_PA_LEN(tag)              ((tag)->pa_len)

/** each flow ring is dedicated to a tid/sa/da combination */
typedef struct flow_info {
	uint8		tid;
	uint8		ifindex;
	uchar		sa[ETHER_ADDR_LEN];
	uchar		da[ETHER_ADDR_LEN];
#if defined(BCMDBG)
	uint32		tx_status[DHD_MAX_TX_STATUS_MSGS];
#endif
#ifdef TX_STATUS_LATENCY_STATS
	/* total number of tx_status received on this flowid */
	uint64           num_tx_status;
	/* cumulative tx_status latency for this flowid */
	uint64          cum_tx_status_latency;
	/* num tx packets sent on this flowring */
	uint64		num_tx_pkts;
#endif /* TX_STATUS_LATENCY_STATS */
} flow_info_t;

/** a flow ring is used for outbound (towards antenna) 802.3 packets */
typedef struct flow_ring_node {
	dll_t		list;  /* manage a constructed flowring in a dll, must be at first place */
	flow_queue_t	queue; /* queues packets before they enter the flow ring, flow control */
	bool		active;
	uint8		status;
	/*
	 * flowid: unique ID of a flow ring, which can either be unicast or broadcast/multicast. For
	 * unicast flow rings, the flow id accelerates ARM 802.3->802.11 header translation.
	 */
	uint16		flowid;
	flow_info_t	flow_info;
	void		*prot_info;
	void		*lock; /* lock for flowring access protection */

#ifdef IDLE_TX_FLOW_MGMT
	uint64		last_active_ts; /* contains last active timestamp */
#endif /* IDLE_TX_FLOW_MGMT */
#ifdef DEVICE_TX_STUCK_DETECT
	/* Time stamp(msec) when last time a Tx packet completion is received on this flow ring */
	uint32		tx_cmpl;
	/*
	 * Holds the tx_cmpl which was read during the previous
	 * iteration of the stuck detection algo
	 */
	uint32		tx_cmpl_prev;
	/* counter to decide if this particlur flow is stuck or not */
	uint32		stuck_count;
#endif /* DEVICE_TX_STUCK_DETECT */
#ifdef DHD_HP2P
	bool	hp2p_ring;
#endif /* DHD_HP2P */
} flow_ring_node_t;

typedef flow_ring_node_t flow_ring_table_t;

typedef struct flow_hash_info {
	uint16			flowid;
	flow_info_t		flow_info;
	struct flow_hash_info	*next;
} flow_hash_info_t;

typedef struct if_flow_lkup {
	bool		status;
	uint8		role; /* Interface role: STA/AP */
	flow_hash_info_t *fl_hash[DHD_FLOWRING_HASH_SIZE]; /* Lkup Hash table */
} if_flow_lkup_t;

static INLINE flow_ring_node_t *
dhd_constlist_to_flowring(dll_t *item)
{
	return ((flow_ring_node_t *)item);
}

/* Exported API */

/* Flow ring's queue management functions */
extern flow_ring_node_t * dhd_flow_ring_node(dhd_pub_t *dhdp, uint16 flowid);
extern flow_queue_t * dhd_flow_queue(dhd_pub_t *dhdp, uint16 flowid);

extern void dhd_flow_queue_init(dhd_pub_t *dhdp, flow_queue_t *queue, int max);
extern void dhd_flow_queue_reinit(dhd_pub_t *dhdp, flow_queue_t *queue, int max);
extern void dhd_flow_queue_register(flow_queue_t *queue, flow_queue_cb_t cb);
extern int  dhd_flow_queue_enqueue(dhd_pub_t *dhdp, flow_queue_t *queue, void *pkt);
extern int  dhd_flow_queue_enqueue_head(dhd_pub_t *dhdp, flow_queue_t *queue, void *pkt);
extern void * dhd_flow_queue_dequeue(dhd_pub_t *dhdp, flow_queue_t *queue);
extern void dhd_flow_queue_reinsert(dhd_pub_t *dhdp, flow_queue_t *queue, void *pkt);

extern void dhd_flow_ring_config_thresholds(dhd_pub_t *dhdp, uint16 flowid,
                          int queue_budget, int cumm_threshold, void *cumm_ctr,
                          int l2cumm_threshold, void *l2cumm_ctr);
extern int  dhd_flow_rings_init(dhd_pub_t *dhdp, uint32 num_h2d_rings);

extern void dhd_flow_rings_deinit(dhd_pub_t *dhdp);

extern int dhd_flowid_update(dhd_pub_t *dhdp, uint8 ifindex, uint8 prio,
                void *pktbuf);
extern int dhd_flowid_debug_create(dhd_pub_t *dhdp, uint8 ifindex,
	uint8 prio, char *sa, char *da, uint16 *flowid);
extern int dhd_flowid_find_by_ifidx(dhd_pub_t *dhdp, uint8 ifidex, uint16 flowid);

extern void dhd_flowid_free(dhd_pub_t *dhdp, uint8 ifindex, uint16 flowid);

extern void dhd_flow_rings_delete(dhd_pub_t *dhdp, uint8 ifindex);
extern void dhd_flow_rings_flush(dhd_pub_t *dhdp, uint8 ifindex);

extern void dhd_flow_rings_delete_for_peer(dhd_pub_t *dhdp, uint8 ifindex,
                char *addr);

/* Handle Interface ADD, DEL operations */
extern void dhd_update_interface_flow_info(dhd_pub_t *dhdp, uint8 ifindex,
                uint8 op, uint8 role);

/* Handle a STA interface link status update */
extern int dhd_update_interface_link_status(dhd_pub_t *dhdp, uint8 ifindex,
                uint8 status);
extern int dhd_flow_prio_map(dhd_pub_t *dhd, uint8 *map, bool set);
extern int dhd_update_flow_prio_map(dhd_pub_t *dhdp, uint8 map);
extern uint32 dhd_active_tx_flowring_bkpq_len(dhd_pub_t *dhdp);
#ifdef DHD_AWDL
/* DHD handler for awdl peer op IOVAR */
extern void dhd_awdl_peer_op(dhd_pub_t *dhdp, uint8 ifindex,
                void *buf, uint32 buflen);
#endif /* DHD_AWDL */
extern uint8 dhd_flow_rings_ifindex2role(dhd_pub_t *dhdp, uint8 ifindex);
#endif /* _dhd_flowrings_h_ */
