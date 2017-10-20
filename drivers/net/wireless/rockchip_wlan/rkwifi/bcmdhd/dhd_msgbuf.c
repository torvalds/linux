/**
 * @file definition of host message ring functionality
 * Provides type definitions and function prototypes used to link the
 * DHD OS, bus, and protocol modules.
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
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
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dhd_msgbuf.c 704361 2017-06-13 08:50:38Z $
 */


#include <typedefs.h>
#include <osl.h>

#include <bcmutils.h>
#include <bcmmsgbuf.h>
#include <bcmendian.h>

#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_proto.h>

#include <dhd_bus.h>

#include <dhd_dbg.h>
#include <siutils.h>
#include <dhd_debug.h>

#include <dhd_flowring.h>

#include <pcie_core.h>
#include <bcmpcie.h>
#include <dhd_pcie.h>
#ifdef DHD_TIMESYNC
#include <dhd_timesync.h>
#endif /* DHD_TIMESYNC */

#if defined(DHD_LB)
#include <linux/cpu.h>
#include <bcm_ring.h>
#define DHD_LB_WORKQ_SZ			    (8192)
#define DHD_LB_WORKQ_SYNC           (16)
#define DHD_LB_WORK_SCHED           (DHD_LB_WORKQ_SYNC * 2)
#endif /* DHD_LB */

#include <hnd_debug.h>
#include <hnd_armtrap.h>

#ifdef DHD_PKT_LOGGING
#include <dhd_pktlog.h>
#endif /* DHD_PKT_LOGGING */

extern char dhd_version[];
extern char fw_version[];

/**
 * Host configures a soft doorbell for d2h rings, by specifying a 32bit host
 * address where a value must be written. Host may also interrupt coalescing
 * on this soft doorbell.
 * Use Case: Hosts with network processors, may register with the dongle the
 * network processor's thread wakeup register and a value corresponding to the
 * core/thread context. Dongle will issue a write transaction <address,value>
 * to the PCIE RC which will need to be routed to the mapped register space, by
 * the host.
 */
/* #define DHD_D2H_SOFT_DOORBELL_SUPPORT */

/* Dependency Check */
#if defined(IOCTLRESP_USE_CONSTMEM) && defined(DHD_USE_STATIC_CTRLBUF)
#error "DHD_USE_STATIC_CTRLBUF is NOT working with DHD_USE_OSLPKT_FOR_RESPBUF"
#endif /* IOCTLRESP_USE_CONSTMEM && DHD_USE_STATIC_CTRLBUF */

#define RETRIES 2		/* # of retries to retrieve matching ioctl response */

#define DEFAULT_RX_BUFFERS_TO_POST	256
#define RXBUFPOST_THRESHOLD			32
#define RX_BUF_BURST				32 /* Rx buffers for MSDU Data */

#define DHD_STOP_QUEUE_THRESHOLD	200
#define DHD_START_QUEUE_THRESHOLD	100

#define RX_DMA_OFFSET		8 /* Mem2mem DMA inserts an extra 8 */
#define IOCT_RETBUF_SIZE	(RX_DMA_OFFSET + WLC_IOCTL_MAXLEN)

/* flags for ioctl pending status */
#define MSGBUF_IOCTL_ACK_PENDING	(1<<0)
#define MSGBUF_IOCTL_RESP_PENDING	(1<<1)

#define DMA_ALIGN_LEN		4

#define DMA_D2H_SCRATCH_BUF_LEN	8
#define DMA_XFER_LEN_LIMIT	0x400000

#ifdef BCM_HOST_BUF
#ifndef DMA_HOST_BUFFER_LEN
#define DMA_HOST_BUFFER_LEN	0x200000
#endif
#endif /* BCM_HOST_BUF */

#define DHD_FLOWRING_IOCTL_BUFPOST_PKTSZ		8192

#define DHD_FLOWRING_DEFAULT_NITEMS_POSTED_H2D		1
#define DHD_FLOWRING_MAX_EVENTBUF_POST			32
#define DHD_FLOWRING_MAX_IOCTLRESPBUF_POST		8
#define DHD_H2D_INFORING_MAX_BUF_POST			32
#define DHD_MAX_TSBUF_POST			8

#define DHD_PROT_FUNCS	41

/* Length of buffer in host for bus throughput measurement */
#define DHD_BUS_TPUT_BUF_LEN 2048

#define TXP_FLUSH_NITEMS

/* optimization to write "n" tx items at a time to ring */
#define TXP_FLUSH_MAX_ITEMS_FLUSH_CNT	48

#define RING_NAME_MAX_LENGTH		24
#define CTRLSUB_HOSTTS_MEESAGE_SIZE		1024
/* Giving room before ioctl_trans_id rollsover. */
#define BUFFER_BEFORE_ROLLOVER 300

struct msgbuf_ring; /* ring context for common and flow rings */

/**
 * PCIE D2H DMA Complete Sync Modes
 *
 * Firmware may interrupt the host, prior to the D2H Mem2Mem DMA completes into
 * Host system memory. A WAR using one of 3 approaches is needed:
 * 1. Dongle places a modulo-253 seqnum in last word of each D2H message
 * 2. XOR Checksum, with epoch# in each work item. Dongle builds an XOR checksum
 *    writes in the last word of each work item. Each work item has a seqnum
 *    number = sequence num % 253.
 *
 * 3. Read Barrier: Dongle does a host memory read access prior to posting an
 *    interrupt, ensuring that D2H data transfer indeed completed.
 * 4. Dongle DMA's all indices after producing items in the D2H ring, flushing
 *    ring contents before the indices.
 *
 * Host does not sync for DMA to complete with option #3 or #4, and a noop sync
 * callback (see dhd_prot_d2h_sync_none) may be bound.
 *
 * Dongle advertizes host side sync mechanism requirements.
 */

#define PCIE_D2H_SYNC_WAIT_TRIES    (512UL)
#define PCIE_D2H_SYNC_NUM_OF_STEPS  (5UL)
#define PCIE_D2H_SYNC_DELAY         (100UL)	/* in terms of usecs */

/**
 * Custom callback attached based upon D2H DMA Sync mode advertized by dongle.
 *
 * On success: return cmn_msg_hdr_t::msg_type
 * On failure: return 0 (invalid msg_type)
 */
typedef uint8 (* d2h_sync_cb_t)(dhd_pub_t *dhd, struct msgbuf_ring *ring,
                                volatile cmn_msg_hdr_t *msg, int msglen);

/*
 * +----------------------------------------------------------------------------
 *
 * RingIds and FlowId are not equivalent as ringids include D2H rings whereas
 * flowids do not.
 *
 * Dongle advertizes the max H2D rings, as max_sub_queues = 'N' which includes
 * the H2D common rings as well as the (N-BCMPCIE_H2D_COMMON_MSGRINGS) flowrings
 *
 * Here is a sample mapping for (based on PCIE Full Dongle Rev5) where,
 *  BCMPCIE_H2D_COMMON_MSGRINGS = 2, i.e. 2 H2D common rings,
 *  BCMPCIE_COMMON_MSGRINGS     = 5, i.e. include 3 D2H common rings.
 *
 *  H2D Control  Submit   RingId = 0        FlowId = 0 reserved never allocated
 *  H2D RxPost   Submit   RingId = 1        FlowId = 1 reserved never allocated
 *
 *  D2H Control  Complete RingId = 2
 *  D2H Transmit Complete RingId = 3
 *  D2H Receive  Complete RingId = 4
 *
 *  H2D TxPost   FLOWRING RingId = 5         FlowId = 2     (1st flowring)
 *  H2D TxPost   FLOWRING RingId = 6         FlowId = 3     (2nd flowring)
 *  H2D TxPost   FLOWRING RingId = 5 + (N-1) FlowId = (N-1) (Nth flowring)
 *
 * When TxPost FlowId(s) are allocated, the FlowIds [0..FLOWID_RESERVED) are
 * unused, where FLOWID_RESERVED is BCMPCIE_H2D_COMMON_MSGRINGS.
 *
 * Example: when a system supports 4 bc/mc and 128 uc flowrings, with
 * BCMPCIE_H2D_COMMON_MSGRINGS = 2, and BCMPCIE_H2D_COMMON_MSGRINGS = 5, and the
 * FlowId values would be in the range [2..133] and the corresponding
 * RingId values would be in the range [5..136].
 *
 * The flowId allocator, may chose to, allocate Flowids:
 *   bc/mc (per virtual interface) in one consecutive range [2..(2+VIFS))
 *   X# of uc flowids in consecutive ranges (per station Id), where X is the
 *   packet's access category (e.g. 4 uc flowids per station).
 *
 * CAUTION:
 * When DMA indices array feature is used, RingId=5, corresponding to the 0th
 * FLOWRING, will actually use the FlowId as index into the H2D DMA index,
 * since the FlowId truly represents the index in the H2D DMA indices array.
 *
 * Likewise, in the D2H direction, the RingId - BCMPCIE_H2D_COMMON_MSGRINGS,
 * will represent the index in the D2H DMA indices array.
 *
 * +----------------------------------------------------------------------------
 */

/* First TxPost Flowring Id */
#define DHD_FLOWRING_START_FLOWID   BCMPCIE_H2D_COMMON_MSGRINGS

/* Determine whether a ringid belongs to a TxPost flowring */
#define DHD_IS_FLOWRING(ringid, max_flow_rings) \
	((ringid) >= BCMPCIE_COMMON_MSGRINGS && \
	(ringid) < ((max_flow_rings) + BCMPCIE_COMMON_MSGRINGS))

/* Convert a H2D TxPost FlowId to a MsgBuf RingId */
#define DHD_FLOWID_TO_RINGID(flowid) \
	(BCMPCIE_COMMON_MSGRINGS + ((flowid) - BCMPCIE_H2D_COMMON_MSGRINGS))

/* Convert a MsgBuf RingId to a H2D TxPost FlowId */
#define DHD_RINGID_TO_FLOWID(ringid) \
	(BCMPCIE_H2D_COMMON_MSGRINGS + ((ringid) - BCMPCIE_COMMON_MSGRINGS))

/* Convert a H2D MsgBuf RingId to an offset index into the H2D DMA indices array
 * This may be used for the H2D DMA WR index array or H2D DMA RD index array or
 * any array of H2D rings.
 */
#define DHD_H2D_RING_OFFSET(ringid) \
	(((ringid) >= BCMPCIE_COMMON_MSGRINGS) ? DHD_RINGID_TO_FLOWID(ringid) : (ringid))

/* Convert a H2D MsgBuf Flowring Id to an offset index into the H2D DMA indices array
 * This may be used for IFRM.
 */
#define DHD_H2D_FRM_FLOW_RING_OFFSET(ringid) \
	((ringid) - BCMPCIE_COMMON_MSGRINGS)

/* Convert a D2H MsgBuf RingId to an offset index into the D2H DMA indices array
 * This may be used for the D2H DMA WR index array or D2H DMA RD index array or
 * any array of D2H rings.
 * d2h debug ring is located at the end, i.e. after all the tx flow rings and h2d debug ring
 * max_h2d_rings: total number of h2d rings
 */
#define DHD_D2H_RING_OFFSET(ringid, max_h2d_rings) \
	((ringid) > (max_h2d_rings) ? \
		((ringid) - max_h2d_rings) : \
		((ringid) - BCMPCIE_H2D_COMMON_MSGRINGS))

/* Convert a D2H DMA Indices Offset to a RingId */
#define DHD_D2H_RINGID(offset) \
	((offset) + BCMPCIE_H2D_COMMON_MSGRINGS)


#define DHD_DMAH_NULL      ((void*)NULL)

/*
 * Pad a DMA-able buffer by an additional cachline. If the end of the DMA-able
 * buffer does not occupy the entire cacheline, and another object is placed
 * following the DMA-able buffer, data corruption may occur if the DMA-able
 * buffer is used to DMAing into (e.g. D2H direction), when HW cache coherency
 * is not available.
 */
#if defined(L1_CACHE_BYTES)
#define DHD_DMA_PAD        (L1_CACHE_BYTES)
#else
#define DHD_DMA_PAD        (128)
#endif

/* Used in loopback tests */
typedef struct dhd_dmaxfer {
	dhd_dma_buf_t srcmem;
	dhd_dma_buf_t dstmem;
	uint32        srcdelay;
	uint32        destdelay;
	uint32        len;
	bool          in_progress;
	uint64        start_usec;
	uint32	      d11_lpbk;
} dhd_dmaxfer_t;

/**
 * msgbuf_ring : This object manages the host side ring that includes a DMA-able
 * buffer, the WR and RD indices, ring parameters such as max number of items
 * an length of each items, and other miscellaneous runtime state.
 * A msgbuf_ring may be used to represent a H2D or D2H common ring or a
 * H2D TxPost ring as specified in the PCIE FullDongle Spec.
 * Ring parameters are conveyed to the dongle, which maintains its own peer end
 * ring state. Depending on whether the DMA Indices feature is supported, the
 * host will update the WR/RD index in the DMA indices array in host memory or
 * directly in dongle memory.
 */
typedef struct msgbuf_ring {
	bool           inited;
	uint16         idx;       /* ring id */
	uint16         rd;        /* read index */
	uint16         curr_rd;   /* read index for debug */
	uint16         wr;        /* write index */
	uint16         max_items; /* maximum number of items in ring */
	uint16         item_len;  /* length of each item in the ring */
	sh_addr_t      base_addr; /* LITTLE ENDIAN formatted: base address */
	dhd_dma_buf_t  dma_buf;   /* DMA-able buffer: pa, va, len, dmah, secdma */
	uint32         seqnum;    /* next expected item's sequence number */
#ifdef TXP_FLUSH_NITEMS
	void           *start_addr;
	/* # of messages on ring not yet announced to dongle */
	uint16         pend_items_count;
#endif /* TXP_FLUSH_NITEMS */

	uint8   ring_type;
	uint8   n_completion_ids;
	bool    create_pending;
	uint16  create_req_id;
	uint8   current_phase;
	uint16	compeltion_ring_ids[MAX_COMPLETION_RING_IDS_ASSOCIATED];
	uchar		name[RING_NAME_MAX_LENGTH];
	uint32		ring_mem_allocated;
} msgbuf_ring_t;

#define DHD_RING_BGN_VA(ring)           ((ring)->dma_buf.va)
#define DHD_RING_END_VA(ring) \
	((uint8 *)(DHD_RING_BGN_VA((ring))) + \
	 (((ring)->max_items - 1) * (ring)->item_len))



/* This can be overwritten by module parameter defined in dhd_linux.c
 * or by dhd iovar h2d_max_txpost.
 */
int h2d_max_txpost = H2DRING_TXPOST_MAX_ITEM;

/** DHD protocol handle. Is an opaque type to other DHD software layers. */
typedef struct dhd_prot {
	osl_t *osh;		/* OSL handle */
	uint16 rxbufpost;
	uint16 max_rxbufpost;
	uint16 max_eventbufpost;
	uint16 max_ioctlrespbufpost;
	uint16 max_tsbufpost;
	uint16 max_infobufpost;
	uint16 infobufpost;
	uint16 cur_event_bufs_posted;
	uint16 cur_ioctlresp_bufs_posted;
	uint16 cur_ts_bufs_posted;

	/* Flow control mechanism based on active transmits pending */
	uint16 active_tx_count; /* increments on every packet tx, and decrements on tx_status */
	uint16 h2d_max_txpost;
	uint16 txp_threshold;  /* optimization to write "n" tx items at a time to ring */

	/* MsgBuf Ring info: has a dhd_dma_buf that is dynamically allocated */
	msgbuf_ring_t h2dring_ctrl_subn; /* H2D ctrl message submission ring */
	msgbuf_ring_t h2dring_rxp_subn; /* H2D RxBuf post ring */
	msgbuf_ring_t d2hring_ctrl_cpln; /* D2H ctrl completion ring */
	msgbuf_ring_t d2hring_tx_cpln; /* D2H Tx complete message ring */
	msgbuf_ring_t d2hring_rx_cpln; /* D2H Rx complete message ring */
	msgbuf_ring_t *h2dring_info_subn; /* H2D info submission ring */
	msgbuf_ring_t *d2hring_info_cpln; /* D2H info completion ring */

	msgbuf_ring_t *h2d_flowrings_pool; /* Pool of preallocated flowings */
	dhd_dma_buf_t flowrings_dma_buf; /* Contiguous DMA buffer for flowrings */
	uint16        h2d_rings_total; /* total H2D (common rings + flowrings) */

	uint32		rx_dataoffset;

	dhd_mb_ring_t	mb_ring_fn;	/* called when dongle needs to be notified of new msg */
	dhd_mb_ring_2_t	mb_2_ring_fn;	/* called when dongle needs to be notified of new msg */

	/* ioctl related resources */
	uint8 ioctl_state;
	int16 ioctl_status;		/* status returned from dongle */
	uint16 ioctl_resplen;
	dhd_ioctl_recieved_status_t ioctl_received;
	uint curr_ioctl_cmd;
	dhd_dma_buf_t	retbuf;		/* For holding ioctl response */
	dhd_dma_buf_t	ioctbuf;	/* For holding ioctl request */

	dhd_dma_buf_t	d2h_dma_scratch_buf;	/* For holding d2h scratch */

	/* DMA-able arrays for holding WR and RD indices */
	uint32          rw_index_sz; /* Size of a RD or WR index in dongle */
	dhd_dma_buf_t   h2d_dma_indx_wr_buf;	/* Array of H2D WR indices */
	dhd_dma_buf_t	h2d_dma_indx_rd_buf;	/* Array of H2D RD indices */
	dhd_dma_buf_t	d2h_dma_indx_wr_buf;	/* Array of D2H WR indices */
	dhd_dma_buf_t	d2h_dma_indx_rd_buf;	/* Array of D2H RD indices */
	dhd_dma_buf_t h2d_ifrm_indx_wr_buf;	/* Array of H2D WR indices for ifrm */

	dhd_dma_buf_t	host_bus_throughput_buf; /* bus throughput measure buffer */

	dhd_dma_buf_t   *flowring_buf;    /* pool of flow ring buf */
	uint32			flowring_num;

	d2h_sync_cb_t d2h_sync_cb; /* Sync on D2H DMA done: SEQNUM or XORCSUM */
	ulong d2h_sync_wait_max; /* max number of wait loops to receive one msg */
	ulong d2h_sync_wait_tot; /* total wait loops */

	dhd_dmaxfer_t	dmaxfer; /* for test/DMA loopback */

	uint16		ioctl_seq_no;
	uint16		data_seq_no;
	uint16		ioctl_trans_id;
	void		*pktid_ctrl_map; /* a pktid maps to a packet and its metadata */
	void		*pktid_rx_map;	/* pktid map for rx path */
	void		*pktid_tx_map;	/* pktid map for tx path */
	void		*rx_lock;	/* rx pktid map and rings access protection */
	bool		metadata_dbg;
	void		*pktid_map_handle_ioctl;

	/* Applications/utilities can read tx and rx metadata using IOVARs */
	uint16		rx_metadata_offset;
	uint16		tx_metadata_offset;


#if defined(DHD_D2H_SOFT_DOORBELL_SUPPORT)
	/* Host's soft doorbell configuration */
	bcmpcie_soft_doorbell_t soft_doorbell[BCMPCIE_D2H_COMMON_MSGRINGS];
#endif /* DHD_D2H_SOFT_DOORBELL_SUPPORT */

	/* Work Queues to be used by the producer and the consumer, and threshold
	 * when the WRITE index must be synced to consumer's workq
	 */
#if defined(DHD_LB_TXC)
	uint32 tx_compl_prod_sync ____cacheline_aligned;
	bcm_workq_t tx_compl_prod, tx_compl_cons;
#endif /* DHD_LB_TXC */
#if defined(DHD_LB_RXC)
	uint32 rx_compl_prod_sync ____cacheline_aligned;
	bcm_workq_t rx_compl_prod, rx_compl_cons;
#endif /* DHD_LB_RXC */

	dhd_dma_buf_t	fw_trap_buf; /* firmware trap buffer */

    uint32  host_ipc_version; /* Host sypported IPC rev */
	uint32  device_ipc_version; /* FW supported IPC rev */
	uint32  active_ipc_version; /* Host advertised IPC rev */
	dhd_dma_buf_t   hostts_req_buf; /* For holding host timestamp request buf */
	bool    hostts_req_buf_inuse;
	bool    rx_ts_log_enabled;
	bool    tx_ts_log_enabled;
} dhd_prot_t;

extern void dhd_schedule_dmaxfer_free(dhd_pub_t* dhdp, dmaxref_mem_map_t *dmmap);

static atomic_t dhd_msgbuf_rxbuf_post_event_bufs_running = ATOMIC_INIT(0);

/* Convert a dmaaddr_t to a base_addr with htol operations */
static INLINE void dhd_base_addr_htolpa(sh_addr_t *base_addr, dmaaddr_t pa);

/* APIs for managing a DMA-able buffer */
static int  dhd_dma_buf_audit(dhd_pub_t *dhd, dhd_dma_buf_t *dma_buf);
static int  dhd_dma_buf_alloc(dhd_pub_t *dhd, dhd_dma_buf_t *dma_buf, uint32 buf_len);
static void dhd_dma_buf_reset(dhd_pub_t *dhd, dhd_dma_buf_t *dma_buf);
static void dhd_dma_buf_free(dhd_pub_t *dhd, dhd_dma_buf_t *dma_buf);

/* msgbuf ring management */
static int dhd_prot_ring_attach(dhd_pub_t *dhd, msgbuf_ring_t *ring,
	const char *name, uint16 max_items, uint16 len_item, uint16 ringid);
static void dhd_prot_ring_init(dhd_pub_t *dhd, msgbuf_ring_t *ring);
static void dhd_prot_ring_reset(dhd_pub_t *dhd, msgbuf_ring_t *ring);
static void dhd_prot_ring_detach(dhd_pub_t *dhd, msgbuf_ring_t *ring);
static void dhd_prot_process_fw_timestamp(dhd_pub_t *dhd, void* buf);

/* Pool of pre-allocated msgbuf_ring_t with DMA-able buffers for Flowrings */
static int  dhd_prot_flowrings_pool_attach(dhd_pub_t *dhd);
static void dhd_prot_flowrings_pool_reset(dhd_pub_t *dhd);
static void dhd_prot_flowrings_pool_detach(dhd_pub_t *dhd);

/* Fetch and Release a flowring msgbuf_ring from flowring  pool */
static msgbuf_ring_t *dhd_prot_flowrings_pool_fetch(dhd_pub_t *dhd,
	uint16 flowid);
/* see also dhd_prot_flowrings_pool_release() in dhd_prot.h */

/* Producer: Allocate space in a msgbuf ring */
static void* dhd_prot_alloc_ring_space(dhd_pub_t *dhd, msgbuf_ring_t *ring,
	uint16 nitems, uint16 *alloced, bool exactly_nitems);
static void* dhd_prot_get_ring_space(msgbuf_ring_t *ring, uint16 nitems,
	uint16 *alloced, bool exactly_nitems);

/* Consumer: Determine the location where the next message may be consumed */
static uint8* dhd_prot_get_read_addr(dhd_pub_t *dhd, msgbuf_ring_t *ring,
	uint32 *available_len);

/* Producer (WR index update) or Consumer (RD index update) indication */
static void dhd_prot_ring_write_complete(dhd_pub_t *dhd, msgbuf_ring_t *ring,
	void *p, uint16 len);
static void dhd_prot_upd_read_idx(dhd_pub_t *dhd, msgbuf_ring_t *ring);

static INLINE int dhd_prot_dma_indx_alloc(dhd_pub_t *dhd, uint8 type,
	dhd_dma_buf_t *dma_buf, uint32 bufsz);

/* Set/Get a RD or WR index in the array of indices */
/* See also: dhd_prot_dma_indx_init() */
void dhd_prot_dma_indx_set(dhd_pub_t *dhd, uint16 new_index, uint8 type,
	uint16 ringid);
static uint16 dhd_prot_dma_indx_get(dhd_pub_t *dhd, uint8 type, uint16 ringid);

/* Locate a packet given a pktid */
static INLINE void *dhd_prot_packet_get(dhd_pub_t *dhd, uint32 pktid, uint8 pkttype,
	bool free_pktid);
/* Locate a packet given a PktId and free it. */
static INLINE void dhd_prot_packet_free(dhd_pub_t *dhd, void *pkt, uint8 pkttype, bool send);

static int dhd_msgbuf_query_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd,
	void *buf, uint len, uint8 action);
static int dhd_msgbuf_set_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd,
	void *buf, uint len, uint8 action);
static int dhd_msgbuf_wait_ioctl_cmplt(dhd_pub_t *dhd, uint32 len, void *buf);
static int dhd_fillup_ioct_reqst(dhd_pub_t *dhd, uint16 len, uint cmd,
	void *buf, int ifidx);

/* Post buffers for Rx, control ioctl response and events */
static uint16 dhd_msgbuf_rxbuf_post_ctrlpath(dhd_pub_t *dhd, uint8 msgid, uint32 max_to_post);
static void dhd_msgbuf_rxbuf_post_ioctlresp_bufs(dhd_pub_t *pub);
static void dhd_msgbuf_rxbuf_post_event_bufs(dhd_pub_t *pub);
static void dhd_msgbuf_rxbuf_post(dhd_pub_t *dhd, bool use_rsv_pktid);
static int dhd_prot_rxbuf_post(dhd_pub_t *dhd, uint16 count, bool use_rsv_pktid);
static int dhd_msgbuf_rxbuf_post_ts_bufs(dhd_pub_t *pub);

static void dhd_prot_return_rxbuf(dhd_pub_t *dhd, uint32 pktid, uint32 rxcnt);


/* D2H Message handling */
static int dhd_prot_process_msgtype(dhd_pub_t *dhd, msgbuf_ring_t *ring, uint8 *buf, uint32 len);

/* D2H Message handlers */
static void dhd_prot_noop(dhd_pub_t *dhd, void *msg);
static void dhd_prot_txstatus_process(dhd_pub_t *dhd, void *msg);
static void dhd_prot_ioctcmplt_process(dhd_pub_t *dhd, void *msg);
static void dhd_prot_ioctack_process(dhd_pub_t *dhd, void *msg);
static void dhd_prot_ringstatus_process(dhd_pub_t *dhd, void *msg);
static void dhd_prot_genstatus_process(dhd_pub_t *dhd, void *msg);
static void dhd_prot_event_process(dhd_pub_t *dhd, void *msg);

/* Loopback test with dongle */
static void dmaxfer_free_dmaaddr(dhd_pub_t *dhd, dhd_dmaxfer_t *dma);
static int dmaxfer_prepare_dmaaddr(dhd_pub_t *dhd, uint len, uint srcdelay,
	uint destdelay, dhd_dmaxfer_t *dma);
static void dhd_msgbuf_dmaxfer_process(dhd_pub_t *dhd, void *msg);

/* Flowring management communication with dongle */
static void dhd_prot_flow_ring_create_response_process(dhd_pub_t *dhd, void *msg);
static void dhd_prot_flow_ring_delete_response_process(dhd_pub_t *dhd, void *msg);
static void dhd_prot_flow_ring_flush_response_process(dhd_pub_t *dhd, void *msg);
static void dhd_prot_process_flow_ring_resume_response(dhd_pub_t *dhd, void* msg);
static void dhd_prot_process_flow_ring_suspend_response(dhd_pub_t *dhd, void* msg);

/* Monitor Mode */
#ifdef WL_MONITOR
extern bool dhd_monitor_enabled(dhd_pub_t *dhd, int ifidx);
extern void dhd_rx_mon_pkt(dhd_pub_t *dhdp, host_rxbuf_cmpl_t* msg, void *pkt, int ifidx);
#endif /* WL_MONITOR */

/* Configure a soft doorbell per D2H ring */
static void dhd_msgbuf_ring_config_d2h_soft_doorbell(dhd_pub_t *dhd);
static void dhd_prot_process_d2h_ring_config_complete(dhd_pub_t *dhd, void *msg);
static void dhd_prot_process_d2h_ring_create_complete(dhd_pub_t *dhd, void *buf);
static void dhd_prot_process_h2d_ring_create_complete(dhd_pub_t *dhd, void *buf);
static void dhd_prot_process_d2h_mb_data(dhd_pub_t *dhd, void* buf);
static void dhd_prot_process_infobuf_complete(dhd_pub_t *dhd, void* buf);
static void dhd_prot_detach_info_rings(dhd_pub_t *dhd);
static void dhd_prot_process_d2h_host_ts_complete(dhd_pub_t *dhd, void* buf);

typedef void (*dhd_msgbuf_func_t)(dhd_pub_t *dhd, void *msg);

/** callback functions for messages generated by the dongle */
#define MSG_TYPE_INVALID 0

static dhd_msgbuf_func_t table_lookup[DHD_PROT_FUNCS] = {
	dhd_prot_noop, /* 0 is MSG_TYPE_INVALID */
	dhd_prot_genstatus_process, /* MSG_TYPE_GEN_STATUS */
	dhd_prot_ringstatus_process, /* MSG_TYPE_RING_STATUS */
	NULL,
	dhd_prot_flow_ring_create_response_process, /* MSG_TYPE_FLOW_RING_CREATE_CMPLT */
	NULL,
	dhd_prot_flow_ring_delete_response_process, /* MSG_TYPE_FLOW_RING_DELETE_CMPLT */
	NULL,
	dhd_prot_flow_ring_flush_response_process, /* MSG_TYPE_FLOW_RING_FLUSH_CMPLT */
	NULL,
	dhd_prot_ioctack_process, /* MSG_TYPE_IOCTLPTR_REQ_ACK */
	NULL,
	dhd_prot_ioctcmplt_process, /* MSG_TYPE_IOCTL_CMPLT */
	NULL,
	dhd_prot_event_process, /* MSG_TYPE_WL_EVENT */
	NULL,
	dhd_prot_txstatus_process, /* MSG_TYPE_TX_STATUS */
	NULL,
	NULL,	/* MSG_TYPE_RX_CMPLT use dedicated handler */
	NULL,
	dhd_msgbuf_dmaxfer_process, /* MSG_TYPE_LPBK_DMAXFER_CMPLT */
	NULL, /* MSG_TYPE_FLOW_RING_RESUME */
	dhd_prot_process_flow_ring_resume_response, /* MSG_TYPE_FLOW_RING_RESUME_CMPLT */
	NULL, /* MSG_TYPE_FLOW_RING_SUSPEND */
	dhd_prot_process_flow_ring_suspend_response, /* MSG_TYPE_FLOW_RING_SUSPEND_CMPLT */
	NULL, /* MSG_TYPE_INFO_BUF_POST */
	dhd_prot_process_infobuf_complete, /* MSG_TYPE_INFO_BUF_CMPLT */
	NULL, /* MSG_TYPE_H2D_RING_CREATE */
	NULL, /* MSG_TYPE_D2H_RING_CREATE */
	dhd_prot_process_h2d_ring_create_complete, /* MSG_TYPE_H2D_RING_CREATE_CMPLT */
	dhd_prot_process_d2h_ring_create_complete, /* MSG_TYPE_D2H_RING_CREATE_CMPLT */
	NULL, /* MSG_TYPE_H2D_RING_CONFIG */
	NULL, /* MSG_TYPE_D2H_RING_CONFIG */
	NULL, /* MSG_TYPE_H2D_RING_CONFIG_CMPLT */
	dhd_prot_process_d2h_ring_config_complete, /* MSG_TYPE_D2H_RING_CONFIG_CMPLT */
	NULL, /* MSG_TYPE_H2D_MAILBOX_DATA */
	dhd_prot_process_d2h_mb_data, /* MSG_TYPE_D2H_MAILBOX_DATA */
	NULL,	/* MSG_TYPE_TIMSTAMP_BUFPOST */
	NULL,	/* MSG_TYPE_HOSTTIMSTAMP */
	dhd_prot_process_d2h_host_ts_complete,	/* MSG_TYPE_HOSTTIMSTAMP_CMPLT */
	dhd_prot_process_fw_timestamp,	/* MSG_TYPE_FIRMWARE_TIMESTAMP */
};


#ifdef DHD_RX_CHAINING

#define PKT_CTF_CHAINABLE(dhd, ifidx, evh, prio, h_sa, h_da, h_prio) \
	(dhd_wet_chainable(dhd) && \
	dhd_rx_pkt_chainable((dhd), (ifidx)) && \
	!ETHER_ISNULLDEST(((struct ether_header *)(evh))->ether_dhost) && \
	!ETHER_ISMULTI(((struct ether_header *)(evh))->ether_dhost) && \
	!eacmp((h_da), ((struct ether_header *)(evh))->ether_dhost) && \
	!eacmp((h_sa), ((struct ether_header *)(evh))->ether_shost) && \
	((h_prio) == (prio)) && (dhd_ctf_hotbrc_check((dhd), (evh), (ifidx))) && \
	((((struct ether_header *)(evh))->ether_type == HTON16(ETHER_TYPE_IP)) || \
	(((struct ether_header *)(evh))->ether_type == HTON16(ETHER_TYPE_IPV6))))

static INLINE void BCMFASTPATH dhd_rxchain_reset(rxchain_info_t *rxchain);
static void BCMFASTPATH dhd_rxchain_frame(dhd_pub_t *dhd, void *pkt, uint ifidx);
static void BCMFASTPATH dhd_rxchain_commit(dhd_pub_t *dhd);

#define DHD_PKT_CTF_MAX_CHAIN_LEN	64

#endif /* DHD_RX_CHAINING */

static void dhd_prot_h2d_sync_init(dhd_pub_t *dhd);

/**
 * D2H DMA to completion callback handlers. Based on the mode advertised by the
 * dongle through the PCIE shared region, the appropriate callback will be
 * registered in the proto layer to be invoked prior to precessing any message
 * from a D2H DMA ring. If the dongle uses a read barrier or another mode that
 * does not require host participation, then a noop callback handler will be
 * bound that simply returns the msg_type.
 */
static void dhd_prot_d2h_sync_livelock(dhd_pub_t *dhd, uint32 msg_seqnum, msgbuf_ring_t *ring,
                                       uint32 tries, volatile uchar *msg, int msglen);
static uint8 dhd_prot_d2h_sync_seqnum(dhd_pub_t *dhd, msgbuf_ring_t *ring,
                                      volatile cmn_msg_hdr_t *msg, int msglen);
static uint8 dhd_prot_d2h_sync_xorcsum(dhd_pub_t *dhd, msgbuf_ring_t *ring,
                                       volatile cmn_msg_hdr_t *msg, int msglen);
static uint8 dhd_prot_d2h_sync_none(dhd_pub_t *dhd, msgbuf_ring_t *ring,
                                    volatile cmn_msg_hdr_t *msg, int msglen);
static void dhd_prot_d2h_sync_init(dhd_pub_t *dhd);
static int dhd_send_d2h_ringcreate(dhd_pub_t *dhd, msgbuf_ring_t *ring_to_create);
static int dhd_send_h2d_ringcreate(dhd_pub_t *dhd, msgbuf_ring_t *ring_to_create);
static uint16 dhd_get_max_flow_rings(dhd_pub_t *dhd);

bool
dhd_prot_is_cmpl_ring_empty(dhd_pub_t *dhd, void *prot_info)
{
	msgbuf_ring_t *flow_ring = (msgbuf_ring_t *)prot_info;
	uint16 rd, wr;
	bool ret;

	if (dhd->dma_d2h_ring_upd_support) {
		wr = flow_ring->wr;
	} else {
		dhd_bus_cmn_readshared(dhd->bus, &wr, RING_WR_UPD, flow_ring->idx);
	}
	if (dhd->dma_h2d_ring_upd_support) {
		rd = flow_ring->rd;
	} else {
		dhd_bus_cmn_readshared(dhd->bus, &rd, RING_RD_UPD, flow_ring->idx);
	}
	ret = (wr == rd) ? TRUE : FALSE;
	return ret;
}
uint16
dhd_prot_get_h2d_max_txpost(dhd_pub_t *dhd)
{
	return (uint16)h2d_max_txpost;
}
void
dhd_prot_set_h2d_max_txpost(dhd_pub_t *dhd, uint16 max_txpost)
{
	h2d_max_txpost = max_txpost;
}
/**
 * dhd_prot_d2h_sync_livelock - when the host determines that a DMA transfer has
 * not completed, a livelock condition occurs. Host will avert this livelock by
 * dropping this message and moving to the next. This dropped message can lead
 * to a packet leak, or even something disastrous in the case the dropped
 * message happens to be a control response.
 * Here we will log this condition. One may choose to reboot the dongle.
 *
 */
static void
dhd_prot_d2h_sync_livelock(dhd_pub_t *dhd, uint32 msg_seqnum, msgbuf_ring_t *ring, uint32 tries,
                           volatile uchar *msg, int msglen)
{
	uint32 ring_seqnum = ring->seqnum;
	DHD_ERROR((
		"LIVELOCK DHD<%p> ring<%s> msg_seqnum<%u> ring_seqnum<%u:%u> tries<%u> max<%lu>"
		" tot<%lu> dma_buf va<%p> msg<%p> curr_rd<%d>\n",
		dhd, ring->name, msg_seqnum, ring_seqnum, ring_seqnum% D2H_EPOCH_MODULO, tries,
		dhd->prot->d2h_sync_wait_max, dhd->prot->d2h_sync_wait_tot,
		ring->dma_buf.va, msg, ring->curr_rd));
	prhex("D2H MsgBuf Failure", (volatile uchar *)msg, msglen);

	dhd_bus_dump_console_buffer(dhd->bus);
	dhd_prot_debug_info_print(dhd);

#ifdef DHD_FW_COREDUMP
	if (dhd->memdump_enabled) {
		/* collect core dump */
		dhd->memdump_type = DUMP_TYPE_BY_LIVELOCK;
		dhd_bus_mem_dump(dhd);
	}
#endif /* DHD_FW_COREDUMP */

	dhd_schedule_reset(dhd);

#ifdef SUPPORT_LINKDOWN_RECOVERY
#ifdef CONFIG_ARCH_MSM
	dhd->bus->no_cfg_restore = 1;
#endif /* CONFIG_ARCH_MSM */
	dhd->hang_reason = HANG_REASON_MSGBUF_LIVELOCK;
	dhd_os_send_hang_message(dhd);
#endif /* SUPPORT_LINKDOWN_RECOVERY */
}

/**
 * dhd_prot_d2h_sync_seqnum - Sync on a D2H DMA completion using the SEQNUM
 * mode. Sequence number is always in the last word of a message.
 */
static uint8 BCMFASTPATH
dhd_prot_d2h_sync_seqnum(dhd_pub_t *dhd, msgbuf_ring_t *ring,
                         volatile cmn_msg_hdr_t *msg, int msglen)
{
	uint32 tries;
	uint32 ring_seqnum = ring->seqnum % D2H_EPOCH_MODULO;
	int num_words = msglen / sizeof(uint32); /* num of 32bit words */
	volatile uint32 *marker = (volatile uint32 *)msg + (num_words - 1); /* last word */
	dhd_prot_t *prot = dhd->prot;
	uint32 msg_seqnum;
	uint32 step = 0;
	uint32 delay = PCIE_D2H_SYNC_DELAY;
	uint32 total_tries = 0;

	ASSERT(msglen == ring->item_len);

	BCM_REFERENCE(delay);
	/*
	 * For retries we have to make some sort of stepper algorithm.
	 * We see that every time when the Dongle comes out of the D3
	 * Cold state, the first D2H mem2mem DMA takes more time to
	 * complete, leading to livelock issues.
	 *
	 * Case 1 - Apart from Host CPU some other bus master is
	 * accessing the DDR port, probably page close to the ring
	 * so, PCIE does not get a change to update the memory.
	 * Solution - Increase the number of tries.
	 *
	 * Case 2 - The 50usec delay given by the Host CPU is not
	 * sufficient for the PCIe RC to start its work.
	 * In this case the breathing time of 50usec given by
	 * the Host CPU is not sufficient.
	 * Solution: Increase the delay in a stepper fashion.
	 * This is done to ensure that there are no
	 * unwanted extra delay introdcued in normal conditions.
	 */
	for (step = 1; step <= PCIE_D2H_SYNC_NUM_OF_STEPS; step++) {
		for (tries = 0; tries < PCIE_D2H_SYNC_WAIT_TRIES; tries++) {
			msg_seqnum = *marker;
			if (ltoh32(msg_seqnum) == ring_seqnum) { /* dma upto last word done */
				ring->seqnum++; /* next expected sequence number */
				goto dma_completed;
			}

			total_tries = ((step-1) * PCIE_D2H_SYNC_WAIT_TRIES) + tries;

			if (total_tries > prot->d2h_sync_wait_max)
				prot->d2h_sync_wait_max = total_tries;

			OSL_CACHE_INV(msg, msglen); /* invalidate and try again */
			OSL_CPU_RELAX(); /* CPU relax for msg_seqnum  value to update */
			OSL_DELAY(delay * step); /* Add stepper delay */

		} /* for PCIE_D2H_SYNC_WAIT_TRIES */
	} /* for PCIE_D2H_SYNC_NUM_OF_STEPS */

	dhd_prot_d2h_sync_livelock(dhd, msg_seqnum, ring, total_tries,
		(volatile uchar *) msg, msglen);

	ring->seqnum++; /* skip this message ... leak of a pktid */
	return MSG_TYPE_INVALID; /* invalid msg_type 0 -> noop callback */

dma_completed:

	prot->d2h_sync_wait_tot += tries;
	return msg->msg_type;
}

/**
 * dhd_prot_d2h_sync_xorcsum - Sync on a D2H DMA completion using the XORCSUM
 * mode. The xorcsum is placed in the last word of a message. Dongle will also
 * place a seqnum in the epoch field of the cmn_msg_hdr.
 */
static uint8 BCMFASTPATH
dhd_prot_d2h_sync_xorcsum(dhd_pub_t *dhd, msgbuf_ring_t *ring,
                          volatile cmn_msg_hdr_t *msg, int msglen)
{
	uint32 tries;
	uint32 prot_checksum = 0; /* computed checksum */
	int num_words = msglen / sizeof(uint32); /* num of 32bit words */
	uint8 ring_seqnum = ring->seqnum % D2H_EPOCH_MODULO;
	dhd_prot_t *prot = dhd->prot;
	uint32 step = 0;
	uint32 delay = PCIE_D2H_SYNC_DELAY;
	uint32 total_tries = 0;

	ASSERT(msglen == ring->item_len);

	BCM_REFERENCE(delay);
	/*
	 * For retries we have to make some sort of stepper algorithm.
	 * We see that every time when the Dongle comes out of the D3
	 * Cold state, the first D2H mem2mem DMA takes more time to
	 * complete, leading to livelock issues.
	 *
	 * Case 1 - Apart from Host CPU some other bus master is
	 * accessing the DDR port, probably page close to the ring
	 * so, PCIE does not get a change to update the memory.
	 * Solution - Increase the number of tries.
	 *
	 * Case 2 - The 50usec delay given by the Host CPU is not
	 * sufficient for the PCIe RC to start its work.
	 * In this case the breathing time of 50usec given by
	 * the Host CPU is not sufficient.
	 * Solution: Increase the delay in a stepper fashion.
	 * This is done to ensure that there are no
	 * unwanted extra delay introdcued in normal conditions.
	 */
	for (step = 1; step <= PCIE_D2H_SYNC_NUM_OF_STEPS; step++) {
		for (tries = 0; tries < PCIE_D2H_SYNC_WAIT_TRIES; tries++) {
			prot_checksum = bcm_compute_xor32((volatile uint32 *)msg, num_words);
			if (prot_checksum == 0U) { /* checksum is OK */
				if (msg->epoch == ring_seqnum) {
					ring->seqnum++; /* next expected sequence number */
					goto dma_completed;
				}
			}

			total_tries = ((step-1) * PCIE_D2H_SYNC_WAIT_TRIES) + tries;

			if (total_tries > prot->d2h_sync_wait_max)
				prot->d2h_sync_wait_max = total_tries;

			OSL_CACHE_INV(msg, msglen); /* invalidate and try again */
			OSL_CPU_RELAX(); /* CPU relax for msg_seqnum  value to update */
			OSL_DELAY(delay * step); /* Add stepper delay */

		} /* for PCIE_D2H_SYNC_WAIT_TRIES */
	} /* for PCIE_D2H_SYNC_NUM_OF_STEPS */

	DHD_ERROR(("%s: prot_checksum = 0x%x\n", __FUNCTION__, prot_checksum));
	dhd_prot_d2h_sync_livelock(dhd, msg->epoch, ring, total_tries,
		(volatile uchar *) msg, msglen);

	ring->seqnum++; /* skip this message ... leak of a pktid */
	return MSG_TYPE_INVALID; /* invalid msg_type 0 -> noop callback */

dma_completed:

	prot->d2h_sync_wait_tot += tries;
	return msg->msg_type;
}

/**
 * dhd_prot_d2h_sync_none - Dongle ensure that the DMA will complete and host
 * need to try to sync. This noop sync handler will be bound when the dongle
 * advertises that neither the SEQNUM nor XORCSUM mode of DMA sync is required.
 */
static uint8 BCMFASTPATH
dhd_prot_d2h_sync_none(dhd_pub_t *dhd, msgbuf_ring_t *ring,
                       volatile cmn_msg_hdr_t *msg, int msglen)
{
	return msg->msg_type;
}

INLINE void
dhd_wakeup_ioctl_event(dhd_pub_t *dhd, dhd_ioctl_recieved_status_t reason)
{
	/* To synchronize with the previous memory operations call wmb() */
	OSL_SMP_WMB();
	dhd->prot->ioctl_received = reason;
	/* Call another wmb() to make sure before waking up the other event value gets updated */
	OSL_SMP_WMB();
	dhd_os_ioctl_resp_wake(dhd);
}

/**
 * dhd_prot_d2h_sync_init - Setup the host side DMA sync mode based on what
 * dongle advertizes.
 */
static void
dhd_prot_d2h_sync_init(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	prot->d2h_sync_wait_max = 0UL;
	prot->d2h_sync_wait_tot = 0UL;

	prot->d2hring_ctrl_cpln.seqnum = D2H_EPOCH_INIT_VAL;
	prot->d2hring_ctrl_cpln.current_phase = BCMPCIE_CMNHDR_PHASE_BIT_INIT;

	prot->d2hring_tx_cpln.seqnum = D2H_EPOCH_INIT_VAL;
	prot->d2hring_tx_cpln.current_phase = BCMPCIE_CMNHDR_PHASE_BIT_INIT;

	prot->d2hring_rx_cpln.seqnum = D2H_EPOCH_INIT_VAL;
	prot->d2hring_rx_cpln.current_phase = BCMPCIE_CMNHDR_PHASE_BIT_INIT;

	if (dhd->d2h_sync_mode & PCIE_SHARED_D2H_SYNC_SEQNUM) {
		prot->d2h_sync_cb = dhd_prot_d2h_sync_seqnum;
		DHD_ERROR(("%s(): D2H sync mechanism is SEQNUM \r\n", __FUNCTION__));
	} else if (dhd->d2h_sync_mode & PCIE_SHARED_D2H_SYNC_XORCSUM) {
		prot->d2h_sync_cb = dhd_prot_d2h_sync_xorcsum;
		DHD_ERROR(("%s(): D2H sync mechanism is XORCSUM \r\n", __FUNCTION__));
	} else {
		prot->d2h_sync_cb = dhd_prot_d2h_sync_none;
		DHD_ERROR(("%s(): D2H sync mechanism is NONE \r\n", __FUNCTION__));
	}
}

/**
 * dhd_prot_h2d_sync_init - Per H2D common ring, setup the msgbuf ring seqnum
 */
static void
dhd_prot_h2d_sync_init(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	prot->h2dring_rxp_subn.seqnum = H2D_EPOCH_INIT_VAL;
	prot->h2dring_rxp_subn.current_phase = 0;

	prot->h2dring_ctrl_subn.seqnum = H2D_EPOCH_INIT_VAL;
	prot->h2dring_ctrl_subn.current_phase = 0;
}

/* +-----------------  End of PCIE DHD H2D DMA SYNC ------------------------+ */


/*
 * +---------------------------------------------------------------------------+
 * PCIE DMA-able buffer. Sets up a dhd_dma_buf_t object, which includes the
 * virtual and physical address, the buffer lenght and the DMA handler.
 * A secdma handler is also included in the dhd_dma_buf object.
 * +---------------------------------------------------------------------------+
 */

static INLINE void
dhd_base_addr_htolpa(sh_addr_t *base_addr, dmaaddr_t pa)
{
	base_addr->low_addr = htol32(PHYSADDRLO(pa));
	base_addr->high_addr = htol32(PHYSADDRHI(pa));
}


/**
 * dhd_dma_buf_audit - Any audits on a DHD DMA Buffer.
 */
static int
dhd_dma_buf_audit(dhd_pub_t *dhd, dhd_dma_buf_t *dma_buf)
{
	uint32 pa_lowaddr, end; /* dongle uses 32bit ptr arithmetic */
	ASSERT(dma_buf);
	pa_lowaddr = PHYSADDRLO(dma_buf->pa);
	ASSERT(PHYSADDRLO(dma_buf->pa) || PHYSADDRHI(dma_buf->pa));
	ASSERT(ISALIGNED(pa_lowaddr, DMA_ALIGN_LEN));
	ASSERT(dma_buf->len != 0);

	/* test 32bit offset arithmetic over dma buffer for loss of carry-over */
	end = (pa_lowaddr + dma_buf->len); /* end address */

	if ((end & 0xFFFFFFFF) < (pa_lowaddr & 0xFFFFFFFF)) { /* exclude carryover */
		DHD_ERROR(("%s: dma_buf %x len %d spans dongle 32bit ptr arithmetic\n",
			__FUNCTION__, pa_lowaddr, dma_buf->len));
		return BCME_ERROR;
	}

	return BCME_OK;
}

/**
 * dhd_dma_buf_alloc - Allocate a cache coherent DMA-able buffer.
 * returns BCME_OK=0 on success
 * returns non-zero negative error value on failure.
 */
static int
dhd_dma_buf_alloc(dhd_pub_t *dhd, dhd_dma_buf_t *dma_buf, uint32 buf_len)
{
	uint32 dma_pad = 0;
	osl_t *osh = dhd->osh;
	uint16 dma_align = DMA_ALIGN_LEN;


	ASSERT(dma_buf != NULL);
	ASSERT(dma_buf->va == NULL);
	ASSERT(dma_buf->len == 0);

	/* Pad the buffer length by one extra cacheline size.
	 * Required for D2H direction.
	 */
	dma_pad = (buf_len % DHD_DMA_PAD) ? DHD_DMA_PAD : 0;
	dma_buf->va = DMA_ALLOC_CONSISTENT(osh, buf_len + dma_pad,
		dma_align, &dma_buf->_alloced, &dma_buf->pa, &dma_buf->dmah);

	if (dma_buf->va == NULL) {
		DHD_ERROR(("%s: buf_len %d, no memory available\n",
			__FUNCTION__, buf_len));
		return BCME_NOMEM;
	}

	dma_buf->len = buf_len; /* not including padded len */

	if (dhd_dma_buf_audit(dhd, dma_buf) != BCME_OK) { /* audit dma buf */
		dhd_dma_buf_free(dhd, dma_buf);
		return BCME_ERROR;
	}

	dhd_dma_buf_reset(dhd, dma_buf); /* zero out and cache flush */

	return BCME_OK;
}

/**
 * dhd_dma_buf_reset - Reset a cache coherent DMA-able buffer.
 */
static void
dhd_dma_buf_reset(dhd_pub_t *dhd, dhd_dma_buf_t *dma_buf)
{
	if ((dma_buf == NULL) || (dma_buf->va == NULL))
		return;

	(void)dhd_dma_buf_audit(dhd, dma_buf);

	/* Zero out the entire buffer and cache flush */
	memset((void*)dma_buf->va, 0, dma_buf->len);
	OSL_CACHE_FLUSH((void *)dma_buf->va, dma_buf->len);
}

/**
 * dhd_dma_buf_free - Free a DMA-able buffer that was previously allocated using
 * dhd_dma_buf_alloc().
 */
static void
dhd_dma_buf_free(dhd_pub_t *dhd, dhd_dma_buf_t *dma_buf)
{
	osl_t *osh = dhd->osh;

	ASSERT(dma_buf);

	if (dma_buf->va == NULL)
		return; /* Allow for free invocation, when alloc failed */

	/* DEBUG: dhd_dma_buf_reset(dhd, dma_buf) */
	(void)dhd_dma_buf_audit(dhd, dma_buf);

	/* dma buffer may have been padded at allocation */
	DMA_FREE_CONSISTENT(osh, dma_buf->va, dma_buf->_alloced,
		dma_buf->pa, dma_buf->dmah);

	memset(dma_buf, 0, sizeof(dhd_dma_buf_t));
}

/**
 * dhd_dma_buf_init - Initialize a dhd_dma_buf with speicifed values.
 * Do not use dhd_dma_buf_init to zero out a dhd_dma_buf_t object. Use memset 0.
 */
void
dhd_dma_buf_init(dhd_pub_t *dhd, void *dhd_dma_buf,
	void *va, uint32 len, dmaaddr_t pa, void *dmah, void *secdma)
{
	dhd_dma_buf_t *dma_buf;
	ASSERT(dhd_dma_buf);
	dma_buf = (dhd_dma_buf_t *)dhd_dma_buf;
	dma_buf->va = va;
	dma_buf->len = len;
	dma_buf->pa = pa;
	dma_buf->dmah = dmah;
	dma_buf->secdma = secdma;

	/* Audit user defined configuration */
	(void)dhd_dma_buf_audit(dhd, dma_buf);
}

/* +------------------  End of PCIE DHD DMA BUF ADT ------------------------+ */

/*
 * +---------------------------------------------------------------------------+
 * PktId Map: Provides a native packet pointer to unique 32bit PktId mapping.
 * Main purpose is to save memory on the dongle, has other purposes as well.
 * The packet id map, also includes storage for some packet parameters that
 * may be saved. A native packet pointer along with the parameters may be saved
 * and a unique 32bit pkt id will be returned. Later, the saved packet pointer
 * and the metadata may be retrieved using the previously allocated packet id.
 * +---------------------------------------------------------------------------+
 */
#define DHD_PCIE_PKTID
#define MAX_CTRL_PKTID		(1024) /* Maximum number of pktids supported */
#define MAX_RX_PKTID		(1024)
#define MAX_TX_PKTID		(3072 * 2)

/* On Router, the pktptr serves as a pktid. */


#if defined(PROP_TXSTATUS) && !defined(DHD_PCIE_PKTID)
#error "PKTIDMAP must be supported with PROP_TXSTATUS/WLFC"
#endif

/* Enum for marking the buffer color based on usage */
typedef enum dhd_pkttype {
	PKTTYPE_DATA_TX = 0,
	PKTTYPE_DATA_RX,
	PKTTYPE_IOCTL_RX,
	PKTTYPE_EVENT_RX,
	PKTTYPE_INFO_RX,
	/* dhd_prot_pkt_free no check, if pktid reserved and no space avail case */
	PKTTYPE_NO_CHECK,
	PKTTYPE_TSBUF_RX
} dhd_pkttype_t;

#define DHD_PKTID_INVALID               (0U)
#define DHD_IOCTL_REQ_PKTID             (0xFFFE)
#define DHD_FAKE_PKTID                  (0xFACE)
#define DHD_H2D_DBGRING_REQ_PKTID	0xFFFD
#define DHD_D2H_DBGRING_REQ_PKTID	0xFFFC
#define DHD_H2D_HOSTTS_REQ_PKTID	0xFFFB

#define IS_FLOWRING(ring) \
	((strncmp(ring->name, "h2dflr", sizeof("h2dflr"))) == (0))

typedef void * dhd_pktid_map_handle_t; /* opaque handle to a pktid map */

/* Construct a packet id mapping table, returning an opaque map handle */
static dhd_pktid_map_handle_t *dhd_pktid_map_init(dhd_pub_t *dhd, uint32 num_items);

/* Destroy a packet id mapping table, freeing all packets active in the table */
static void dhd_pktid_map_fini(dhd_pub_t *dhd, dhd_pktid_map_handle_t *map);

#define DHD_NATIVE_TO_PKTID_INIT(dhd, items) dhd_pktid_map_init((dhd), (items))
#define DHD_NATIVE_TO_PKTID_RESET(dhd, map)  dhd_pktid_map_reset((dhd), (map))
#define DHD_NATIVE_TO_PKTID_FINI(dhd, map)   dhd_pktid_map_fini((dhd), (map))
#define DHD_NATIVE_TO_PKTID_FINI_IOCTL(osh, map)  dhd_pktid_map_fini_ioctl((osh), (map))

#ifdef MACOSX_DHD
#undef DHD_PCIE_PKTID
#define DHD_PCIE_PKTID 1
#endif /* MACOSX_DHD */

#if defined(DHD_PCIE_PKTID)
#if defined(MACOSX_DHD) || defined(DHD_EFI)
#define IOCTLRESP_USE_CONSTMEM
static void free_ioctl_return_buffer(dhd_pub_t *dhd, dhd_dma_buf_t *retbuf);
static int  alloc_ioctl_return_buffer(dhd_pub_t *dhd, dhd_dma_buf_t *retbuf);
#endif 

/* Determine number of pktids that are available */
static INLINE uint32 dhd_pktid_map_avail_cnt(dhd_pktid_map_handle_t *handle);

/* Allocate a unique pktid against which a pkt and some metadata is saved */
static INLINE uint32 dhd_pktid_map_reserve(dhd_pub_t *dhd, dhd_pktid_map_handle_t *handle,
	void *pkt, dhd_pkttype_t pkttype);
static INLINE void dhd_pktid_map_save(dhd_pub_t *dhd, dhd_pktid_map_handle_t *handle,
	void *pkt, uint32 nkey, dmaaddr_t pa, uint32 len, uint8 dma,
	void *dmah, void *secdma, dhd_pkttype_t pkttype);
static uint32 dhd_pktid_map_alloc(dhd_pub_t *dhd, dhd_pktid_map_handle_t *map,
	void *pkt, dmaaddr_t pa, uint32 len, uint8 dma,
	void *dmah, void *secdma, dhd_pkttype_t pkttype);

/* Return an allocated pktid, retrieving previously saved pkt and metadata */
static void *dhd_pktid_map_free(dhd_pub_t *dhd, dhd_pktid_map_handle_t *map,
	uint32 id, dmaaddr_t *pa, uint32 *len, void **dmah,
	void **secdma, dhd_pkttype_t pkttype, bool rsv_locker);

/*
 * DHD_PKTID_AUDIT_ENABLED: Audit of PktIds in DHD for duplicate alloc and frees
 *
 * DHD_PKTID_AUDIT_MAP: Audit the LIFO or FIFO PktIdMap allocator
 * DHD_PKTID_AUDIT_RING: Audit the pktid during producer/consumer ring operation
 *
 * CAUTION: When DHD_PKTID_AUDIT_ENABLED is defined,
 *    either DHD_PKTID_AUDIT_MAP or DHD_PKTID_AUDIT_RING may be selected.
 */
#if defined(DHD_PKTID_AUDIT_ENABLED)
#define USE_DHD_PKTID_AUDIT_LOCK 1
/* Audit the pktidmap allocator */
/* #define DHD_PKTID_AUDIT_MAP */

/* Audit the pktid during production/consumption of workitems */
#define DHD_PKTID_AUDIT_RING

#if defined(DHD_PKTID_AUDIT_MAP) && defined(DHD_PKTID_AUDIT_RING)
#error "May only enabled audit of MAP or RING, at a time."
#endif /* DHD_PKTID_AUDIT_MAP && DHD_PKTID_AUDIT_RING */

#define DHD_DUPLICATE_ALLOC     1
#define DHD_DUPLICATE_FREE      2
#define DHD_TEST_IS_ALLOC       3
#define DHD_TEST_IS_FREE        4

#ifdef USE_DHD_PKTID_AUDIT_LOCK
#define DHD_PKTID_AUDIT_LOCK_INIT(osh)          dhd_os_spin_lock_init(osh)
#define DHD_PKTID_AUDIT_LOCK_DEINIT(osh, lock)  dhd_os_spin_lock_deinit(osh, lock)
#define DHD_PKTID_AUDIT_LOCK(lock)              dhd_os_spin_lock(lock)
#define DHD_PKTID_AUDIT_UNLOCK(lock, flags)     dhd_os_spin_unlock(lock, flags)
#else
#define DHD_PKTID_AUDIT_LOCK_INIT(osh)          (void *)(1)
#define DHD_PKTID_AUDIT_LOCK_DEINIT(osh, lock)  do { /* noop */ } while (0)
#define DHD_PKTID_AUDIT_LOCK(lock)              0
#define DHD_PKTID_AUDIT_UNLOCK(lock, flags)     do { /* noop */ } while (0)
#endif /* !USE_DHD_PKTID_AUDIT_LOCK */

#endif /* DHD_PKTID_AUDIT_ENABLED */

/* #define USE_DHD_PKTID_LOCK   1 */

#ifdef USE_DHD_PKTID_LOCK
#define DHD_PKTID_LOCK_INIT(osh)                dhd_os_spin_lock_init(osh)
#define DHD_PKTID_LOCK_DEINIT(osh, lock)        dhd_os_spin_lock_deinit(osh, lock)
#define DHD_PKTID_LOCK(lock)                    dhd_os_spin_lock(lock)
#define DHD_PKTID_UNLOCK(lock, flags)           dhd_os_spin_unlock(lock, flags)
#else
#define DHD_PKTID_LOCK_INIT(osh)                (void *)(1)
#define DHD_PKTID_LOCK_DEINIT(osh, lock)	\
	do { \
		BCM_REFERENCE(osh); \
		BCM_REFERENCE(lock); \
	} while (0)
#define DHD_PKTID_LOCK(lock)                    0
#define DHD_PKTID_UNLOCK(lock, flags)           \
	do { \
		BCM_REFERENCE(lock); \
		BCM_REFERENCE(flags); \
	} while (0)
#endif /* !USE_DHD_PKTID_LOCK */

typedef enum dhd_locker_state {
	LOCKER_IS_FREE,
	LOCKER_IS_BUSY,
	LOCKER_IS_RSVD
} dhd_locker_state_t;

/* Packet metadata saved in packet id mapper */

typedef struct dhd_pktid_item {
	dhd_locker_state_t state;  /* tag a locker to be free, busy or reserved */
	uint8       dir;      /* dma map direction (Tx=flush or Rx=invalidate) */
	dhd_pkttype_t pkttype; /* pktlists are maintained based on pkttype */
	uint16      len;      /* length of mapped packet's buffer */
	void        *pkt;     /* opaque native pointer to a packet */
	dmaaddr_t   pa;       /* physical address of mapped packet's buffer */
	void        *dmah;    /* handle to OS specific DMA map */
	void		*secdma;
} dhd_pktid_item_t;

typedef uint32 dhd_pktid_key_t;

typedef struct dhd_pktid_map {
	uint32      items;    /* total items in map */
	uint32      avail;    /* total available items */
	int         failures; /* lockers unavailable count */
#if defined(DHD_PKTID_AUDIT_ENABLED)
	void		*pktid_audit_lock;
	struct bcm_mwbmap *pktid_audit; /* multi word bitmap based audit */
#endif /* DHD_PKTID_AUDIT_ENABLED */
	dhd_pktid_key_t	*keys; /* map_items +1 unique pkt ids */
	dhd_pktid_item_t lockers[0];           /* metadata storage */
} dhd_pktid_map_t;

/*
 * PktId (Locker) #0 is never allocated and is considered invalid.
 *
 * On request for a pktid, a value DHD_PKTID_INVALID must be treated as a
 * depleted pktid pool and must not be used by the caller.
 *
 * Likewise, a caller must never free a pktid of value DHD_PKTID_INVALID.
 */

#define DHD_PKTID_FREE_LOCKER           (FALSE)
#define DHD_PKTID_RSV_LOCKER            (TRUE)

#define DHD_PKTID_ITEM_SZ               (sizeof(dhd_pktid_item_t))
#define DHD_PKIDMAP_ITEMS(items)        (items)
#define DHD_PKTID_MAP_SZ(items)         (sizeof(dhd_pktid_map_t) + \
	                                     (DHD_PKTID_ITEM_SZ * ((items) + 1)))
#define DHD_PKTIDMAP_KEYS_SZ(items)     (sizeof(dhd_pktid_key_t) * ((items) + 1))

#define DHD_NATIVE_TO_PKTID_RESET_IOCTL(dhd, map)  dhd_pktid_map_reset_ioctl((dhd), (map))

/* Convert a packet to a pktid, and save pkt pointer in busy locker */
#define DHD_NATIVE_TO_PKTID_RSV(dhd, map, pkt, pkttype)    \
	dhd_pktid_map_reserve((dhd), (map), (pkt), (pkttype))
/* Reuse a previously reserved locker to save packet params */
#define DHD_NATIVE_TO_PKTID_SAVE(dhd, map, pkt, nkey, pa, len, dir, dmah, secdma, pkttype) \
	dhd_pktid_map_save((dhd), (map), (void *)(pkt), (nkey), (pa), (uint32)(len), \
		(uint8)(dir), (void *)(dmah), (void *)(secdma), \
		(dhd_pkttype_t)(pkttype))
/* Convert a packet to a pktid, and save packet params in locker */
#define DHD_NATIVE_TO_PKTID(dhd, map, pkt, pa, len, dir, dmah, secdma, pkttype) \
	dhd_pktid_map_alloc((dhd), (map), (void *)(pkt), (pa), (uint32)(len), \
		(uint8)(dir), (void *)(dmah), (void *)(secdma), \
		(dhd_pkttype_t)(pkttype))

/* Convert pktid to a packet, and free the locker */
#define DHD_PKTID_TO_NATIVE(dhd, map, pktid, pa, len, dmah, secdma, pkttype) \
	dhd_pktid_map_free((dhd), (map), (uint32)(pktid), \
		(dmaaddr_t *)&(pa), (uint32 *)&(len), (void **)&(dmah), \
		(void **)&(secdma), (dhd_pkttype_t)(pkttype), DHD_PKTID_FREE_LOCKER)

/* Convert the pktid to a packet, empty locker, but keep it reserved */
#define DHD_PKTID_TO_NATIVE_RSV(dhd, map, pktid, pa, len, dmah, secdma, pkttype) \
	dhd_pktid_map_free((dhd), (map), (uint32)(pktid), \
	                   (dmaaddr_t *)&(pa), (uint32 *)&(len), (void **)&(dmah), \
	                   (void **)&(secdma), (dhd_pkttype_t)(pkttype), DHD_PKTID_RSV_LOCKER)

#define DHD_PKTID_AVAIL(map)                 dhd_pktid_map_avail_cnt(map)

#if defined(DHD_PKTID_AUDIT_ENABLED)
/**
* dhd_pktid_audit - Use the mwbmap to audit validity of a pktid.
*/
static int
dhd_pktid_audit(dhd_pub_t *dhd, dhd_pktid_map_t *pktid_map, uint32 pktid,
	const int test_for, const char *errmsg)
{
#define DHD_PKT_AUDIT_STR "ERROR: %16s Host PktId Audit: "
	struct bcm_mwbmap *handle;
	uint32	flags;
	bool ignore_audit;

	if (pktid_map == (dhd_pktid_map_t *)NULL) {
		DHD_ERROR((DHD_PKT_AUDIT_STR "Pkt id map NULL\n", errmsg));
		return BCME_OK;
	}

	flags = DHD_PKTID_AUDIT_LOCK(pktid_map->pktid_audit_lock);

	handle = pktid_map->pktid_audit;
	if (handle == (struct bcm_mwbmap *)NULL) {
		DHD_ERROR((DHD_PKT_AUDIT_STR "Handle NULL\n", errmsg));
		DHD_PKTID_AUDIT_UNLOCK(pktid_map->pktid_audit_lock, flags);
		return BCME_OK;
	}

	/* Exclude special pktids from audit */
	ignore_audit = (pktid == DHD_IOCTL_REQ_PKTID) | (pktid == DHD_FAKE_PKTID);
	if (ignore_audit) {
		DHD_PKTID_AUDIT_UNLOCK(pktid_map->pktid_audit_lock, flags);
		return BCME_OK;
	}

	if ((pktid == DHD_PKTID_INVALID) || (pktid > pktid_map->items)) {
		DHD_ERROR((DHD_PKT_AUDIT_STR "PktId<%d> invalid\n", errmsg, pktid));
		/* lock is released in "error" */
		goto error;
	}

	/* Perform audit */
	switch (test_for) {
		case DHD_DUPLICATE_ALLOC:
			if (!bcm_mwbmap_isfree(handle, pktid)) {
				DHD_ERROR((DHD_PKT_AUDIT_STR "PktId<%d> alloc duplicate\n",
				           errmsg, pktid));
				goto error;
			}
			bcm_mwbmap_force(handle, pktid);
			break;

		case DHD_DUPLICATE_FREE:
			if (bcm_mwbmap_isfree(handle, pktid)) {
				DHD_ERROR((DHD_PKT_AUDIT_STR "PktId<%d> free duplicate\n",
				           errmsg, pktid));
				goto error;
			}
			bcm_mwbmap_free(handle, pktid);
			break;

		case DHD_TEST_IS_ALLOC:
			if (bcm_mwbmap_isfree(handle, pktid)) {
				DHD_ERROR((DHD_PKT_AUDIT_STR "PktId<%d> is not allocated\n",
				           errmsg, pktid));
				goto error;
			}
			break;

		case DHD_TEST_IS_FREE:
			if (!bcm_mwbmap_isfree(handle, pktid)) {
				DHD_ERROR((DHD_PKT_AUDIT_STR "PktId<%d> is not free",
				           errmsg, pktid));
				goto error;
			}
			break;

		default:
			goto error;
	}

	DHD_PKTID_AUDIT_UNLOCK(pktid_map->pktid_audit_lock, flags);
	return BCME_OK;

error:

	DHD_PKTID_AUDIT_UNLOCK(pktid_map->pktid_audit_lock, flags);
	/* May insert any trap mechanism here ! */
	dhd_pktid_error_handler(dhd);

	return BCME_ERROR;
}

#define DHD_PKTID_AUDIT(dhdp, map, pktid, test_for) \
	dhd_pktid_audit((dhdp), (dhd_pktid_map_t *)(map), (pktid), (test_for), __FUNCTION__)

static int
dhd_pktid_audit_ring_debug(dhd_pub_t *dhdp, dhd_pktid_map_t *map, uint32 pktid,
	const int test_for, void *msg, uint32 msg_len, const char * func)
{
	int ret = 0;
	ret = DHD_PKTID_AUDIT(dhdp, map, pktid, test_for);
	if (ret == BCME_ERROR) {
		prhex(func, (uchar *)msg, msg_len);
	}
	return ret;
}
#define DHD_PKTID_AUDIT_RING_DEBUG(dhdp, map, pktid, test_for, msg, msg_len) \
	dhd_pktid_audit_ring_debug((dhdp), (dhd_pktid_map_t *)(map), \
		(pktid), (test_for), msg, msg_len, __FUNCTION__)

#endif /* DHD_PKTID_AUDIT_ENABLED */


/**
 * +---------------------------------------------------------------------------+
 * Packet to Packet Id mapper using a <numbered_key, locker> paradigm.
 *
 * dhd_pktid_map manages a set of unique Packet Ids range[1..MAX_xxx_PKTID].
 *
 * dhd_pktid_map_alloc() may be used to save some packet metadata, and a unique
 * packet id is returned. This unique packet id may be used to retrieve the
 * previously saved packet metadata, using dhd_pktid_map_free(). On invocation
 * of dhd_pktid_map_free(), the unique packet id is essentially freed. A
 * subsequent call to dhd_pktid_map_alloc() may reuse this packet id.
 *
 * Implementation Note:
 * Convert this into a <key,locker> abstraction and place into bcmutils !
 * Locker abstraction should treat contents as opaque storage, and a
 * callback should be registered to handle busy lockers on destructor.
 *
 * +---------------------------------------------------------------------------+
 */

/** Allocate and initialize a mapper of num_items <numbered_key, locker> */

static dhd_pktid_map_handle_t *
dhd_pktid_map_init(dhd_pub_t *dhd, uint32 num_items)
{
	void* osh;
	uint32 nkey;
	dhd_pktid_map_t *map;
	uint32 dhd_pktid_map_sz;
	uint32 map_items;
	uint32 map_keys_sz;
	osh = dhd->osh;

	dhd_pktid_map_sz = DHD_PKTID_MAP_SZ(num_items);

	map = (dhd_pktid_map_t *)VMALLOC(osh, dhd_pktid_map_sz);
	if (map == NULL) {
		DHD_ERROR(("%s:%d: MALLOC failed for size %d\n",
			__FUNCTION__, __LINE__, dhd_pktid_map_sz));
		return (dhd_pktid_map_handle_t *)NULL;
	}

	/* Initialize the lock that protects this structure */
	map->items = num_items;
	map->avail = num_items;

	map_items = DHD_PKIDMAP_ITEMS(map->items);

	map_keys_sz = DHD_PKTIDMAP_KEYS_SZ(map->items);
	map->keys = (dhd_pktid_key_t *)MALLOC(osh, map_keys_sz);
	if (map->keys == NULL) {
		DHD_ERROR(("%s:%d: MALLOC failed for map->keys size %d\n",
			__FUNCTION__, __LINE__, map_keys_sz));
		goto error;
	}

#if defined(DHD_PKTID_AUDIT_ENABLED)
		/* Incarnate a hierarchical multiword bitmap for auditing pktid allocator */
		map->pktid_audit = bcm_mwbmap_init(osh, map_items + 1);
		if (map->pktid_audit == (struct bcm_mwbmap *)NULL) {
			DHD_ERROR(("%s:%d: pktid_audit init failed\r\n", __FUNCTION__, __LINE__));
			goto error;
		} else {
			DHD_ERROR(("%s:%d: pktid_audit init succeeded %d\n",
				__FUNCTION__, __LINE__, map_items + 1));
		}
		map->pktid_audit_lock = DHD_PKTID_AUDIT_LOCK_INIT(osh);
#endif /* DHD_PKTID_AUDIT_ENABLED */

	for (nkey = 1; nkey <= map_items; nkey++) { /* locker #0 is reserved */
		map->keys[nkey] = nkey; /* populate with unique keys */
		map->lockers[nkey].state = LOCKER_IS_FREE;
		map->lockers[nkey].pkt   = NULL; /* bzero: redundant */
		map->lockers[nkey].len   = 0;
	}

	/* Reserve pktid #0, i.e. DHD_PKTID_INVALID to be inuse */
	map->lockers[DHD_PKTID_INVALID].state = LOCKER_IS_BUSY; /* tag locker #0 as inuse */
	map->lockers[DHD_PKTID_INVALID].pkt   = NULL; /* bzero: redundant */
	map->lockers[DHD_PKTID_INVALID].len   = 0;

#if defined(DHD_PKTID_AUDIT_ENABLED)
	/* do not use dhd_pktid_audit() here, use bcm_mwbmap_force directly */
	bcm_mwbmap_force(map->pktid_audit, DHD_PKTID_INVALID);
#endif /* DHD_PKTID_AUDIT_ENABLED */

	return (dhd_pktid_map_handle_t *)map; /* opaque handle */

error:
	if (map) {
#if defined(DHD_PKTID_AUDIT_ENABLED)
		if (map->pktid_audit != (struct bcm_mwbmap *)NULL) {
			bcm_mwbmap_fini(osh, map->pktid_audit); /* Destruct pktid_audit */
			map->pktid_audit = (struct bcm_mwbmap *)NULL;
			if (map->pktid_audit_lock)
				DHD_PKTID_AUDIT_LOCK_DEINIT(osh, map->pktid_audit_lock);
		}
#endif /* DHD_PKTID_AUDIT_ENABLED */
		if (map->keys) {
			MFREE(osh, map->keys, map_keys_sz);
		}
		VMFREE(osh, map, dhd_pktid_map_sz);
	}
	return (dhd_pktid_map_handle_t *)NULL;
}

/**
 * Retrieve all allocated keys and free all <numbered_key, locker>.
 * Freeing implies: unmapping the buffers and freeing the native packet
 * This could have been a callback registered with the pktid mapper.
 */
static void
dhd_pktid_map_reset(dhd_pub_t *dhd, dhd_pktid_map_handle_t *handle)
{
	void *osh;
	uint32 nkey;
	dhd_pktid_map_t *map;
	dhd_pktid_item_t *locker;
	uint32 map_items;
	uint32 flags;
	bool data_tx = FALSE;

	map = (dhd_pktid_map_t *)handle;
	DHD_GENERAL_LOCK(dhd, flags);
	osh = dhd->osh;

	map_items = DHD_PKIDMAP_ITEMS(map->items);
	/* skip reserved KEY #0, and start from 1 */

	for (nkey = 1; nkey <= map_items; nkey++) {
		if (map->lockers[nkey].state == LOCKER_IS_BUSY) {
			locker = &map->lockers[nkey];
			locker->state = LOCKER_IS_FREE;
			data_tx = (locker->pkttype == PKTTYPE_DATA_TX);
			if (data_tx) {
				dhd->prot->active_tx_count--;
			}

#ifdef DHD_PKTID_AUDIT_RING
			DHD_PKTID_AUDIT(dhd, map, nkey, DHD_DUPLICATE_FREE); /* duplicate frees */
#endif /* DHD_PKTID_AUDIT_RING */

			{
				if (SECURE_DMA_ENAB(dhd->osh))
					SECURE_DMA_UNMAP(osh, locker->pa,
						locker->len, locker->dir, 0,
						locker->dmah, locker->secdma, 0);
				else
					DMA_UNMAP(osh, locker->pa, locker->len,
						locker->dir, 0, locker->dmah);
			}
			dhd_prot_packet_free(dhd, (ulong*)locker->pkt,
				locker->pkttype, data_tx);
		}
		else {
#ifdef DHD_PKTID_AUDIT_RING
			DHD_PKTID_AUDIT(dhd, map, nkey, DHD_TEST_IS_FREE);
#endif /* DHD_PKTID_AUDIT_RING */
		}
		map->keys[nkey] = nkey; /* populate with unique keys */
	}

	map->avail = map_items;
	memset(&map->lockers[1], 0, sizeof(dhd_pktid_item_t) * map_items);
	DHD_GENERAL_UNLOCK(dhd, flags);
}

#ifdef IOCTLRESP_USE_CONSTMEM
/** Called in detach scenario. Releasing IOCTL buffers. */
static void
dhd_pktid_map_reset_ioctl(dhd_pub_t *dhd, dhd_pktid_map_handle_t *handle)
{
	uint32 nkey;
	dhd_pktid_map_t *map;
	dhd_pktid_item_t *locker;
	uint32 map_items;
	uint32 flags;

	map = (dhd_pktid_map_t *)handle;
	DHD_GENERAL_LOCK(dhd, flags);

	map_items = DHD_PKIDMAP_ITEMS(map->items);
	/* skip reserved KEY #0, and start from 1 */
	for (nkey = 1; nkey <= map_items; nkey++) {
		if (map->lockers[nkey].state == LOCKER_IS_BUSY) {
			dhd_dma_buf_t retbuf;

#ifdef DHD_PKTID_AUDIT_RING
			DHD_PKTID_AUDIT(dhd, map, nkey, DHD_DUPLICATE_FREE); /* duplicate frees */
#endif /* DHD_PKTID_AUDIT_RING */

			locker = &map->lockers[nkey];
			retbuf.va = locker->pkt;
			retbuf.len = locker->len;
			retbuf.pa = locker->pa;
			retbuf.dmah = locker->dmah;
			retbuf.secdma = locker->secdma;

			/* This could be a callback registered with dhd_pktid_map */
			DHD_GENERAL_UNLOCK(dhd, flags);
			free_ioctl_return_buffer(dhd, &retbuf);
			DHD_GENERAL_LOCK(dhd, flags);
		}
		else {
#ifdef DHD_PKTID_AUDIT_RING
			DHD_PKTID_AUDIT(dhd, map, nkey, DHD_TEST_IS_FREE);
#endif /* DHD_PKTID_AUDIT_RING */
		}
		map->keys[nkey] = nkey; /* populate with unique keys */
	}

	map->avail = map_items;
	memset(&map->lockers[1], 0, sizeof(dhd_pktid_item_t) * map_items);
	DHD_GENERAL_UNLOCK(dhd, flags);
}
#endif /* IOCTLRESP_USE_CONSTMEM */


/**
 * Free the pktid map.
 */
static void
dhd_pktid_map_fini(dhd_pub_t *dhd, dhd_pktid_map_handle_t *handle)
{
	dhd_pktid_map_t *map;
	uint32 dhd_pktid_map_sz;
	uint32 map_keys_sz;

	/* Free any pending packets */
	dhd_pktid_map_reset(dhd, handle);

	map = (dhd_pktid_map_t *)handle;
	dhd_pktid_map_sz = DHD_PKTID_MAP_SZ(map->items);
	map_keys_sz = DHD_PKTIDMAP_KEYS_SZ(map->items);

#if defined(DHD_PKTID_AUDIT_ENABLED)
	if (map->pktid_audit != (struct bcm_mwbmap *)NULL) {
		bcm_mwbmap_fini(dhd->osh, map->pktid_audit); /* Destruct pktid_audit */
		map->pktid_audit = (struct bcm_mwbmap *)NULL;
		if (map->pktid_audit_lock) {
			DHD_PKTID_AUDIT_LOCK_DEINIT(dhd->osh, map->pktid_audit_lock);
		}
	}
#endif /* DHD_PKTID_AUDIT_ENABLED */
	MFREE(dhd->osh, map->keys, map_keys_sz);
	VMFREE(dhd->osh, handle, dhd_pktid_map_sz);
}
#ifdef IOCTLRESP_USE_CONSTMEM
static void
dhd_pktid_map_fini_ioctl(dhd_pub_t *dhd, dhd_pktid_map_handle_t *handle)
{
	dhd_pktid_map_t *map;
	uint32 dhd_pktid_map_sz;
	uint32 map_keys_sz;

	/* Free any pending packets */
	dhd_pktid_map_reset_ioctl(dhd, handle);

	map = (dhd_pktid_map_t *)handle;
	dhd_pktid_map_sz = DHD_PKTID_MAP_SZ(map->items);
	map_keys_sz = DHD_PKTIDMAP_KEYS_SZ(map->items);

#if defined(DHD_PKTID_AUDIT_ENABLED)
	if (map->pktid_audit != (struct bcm_mwbmap *)NULL) {
		bcm_mwbmap_fini(dhd->osh, map->pktid_audit); /* Destruct pktid_audit */
		map->pktid_audit = (struct bcm_mwbmap *)NULL;
		if (map->pktid_audit_lock) {
			DHD_PKTID_AUDIT_LOCK_DEINIT(dhd->osh, map->pktid_audit_lock);
		}
	}
#endif /* DHD_PKTID_AUDIT_ENABLED */

	MFREE(dhd->osh, map->keys, map_keys_sz);
	VMFREE(dhd->osh, handle, dhd_pktid_map_sz);
}
#endif /* IOCTLRESP_USE_CONSTMEM */

/** Get the pktid free count */
static INLINE uint32 BCMFASTPATH
dhd_pktid_map_avail_cnt(dhd_pktid_map_handle_t *handle)
{
	dhd_pktid_map_t *map;
	uint32	avail;

	ASSERT(handle != NULL);
	map = (dhd_pktid_map_t *)handle;

	avail = map->avail;

	return avail;
}

/**
 * dhd_pktid_map_reserve - reserve a unique numbered key. Reserved locker is not
 * yet populated. Invoke the pktid save api to populate the packet parameters
 * into the locker. This function is not reentrant, and is the caller's
 * responsibility. Caller must treat a returned value DHD_PKTID_INVALID as
 * a failure case, implying a depleted pool of pktids.
 */
static INLINE uint32
dhd_pktid_map_reserve(dhd_pub_t *dhd, dhd_pktid_map_handle_t *handle,
	void *pkt, dhd_pkttype_t pkttype)
{
	uint32 nkey;
	dhd_pktid_map_t *map;
	dhd_pktid_item_t *locker;

	ASSERT(handle != NULL);
	map = (dhd_pktid_map_t *)handle;

	if ((int)(map->avail) <= 0) { /* no more pktids to allocate */
		map->failures++;
		DHD_INFO(("%s:%d: failed, no free keys\n", __FUNCTION__, __LINE__));
		return DHD_PKTID_INVALID; /* failed alloc request */
	}

	ASSERT(map->avail <= map->items);
	nkey = map->keys[map->avail]; /* fetch a free locker, pop stack */

	if ((map->avail > map->items) || (nkey > map->items)) {
		map->failures++;
		DHD_ERROR(("%s:%d: failed to allocate a new pktid,"
			" map->avail<%u>, nkey<%u>, pkttype<%u>\n",
			__FUNCTION__, __LINE__, map->avail, nkey,
			pkttype));
		return DHD_PKTID_INVALID; /* failed alloc request */
	}

	locker = &map->lockers[nkey]; /* save packet metadata in locker */
	map->avail--;
	locker->pkt = pkt; /* pkt is saved, other params not yet saved. */
	locker->len = 0;
	locker->state = LOCKER_IS_BUSY; /* reserve this locker */

	ASSERT(nkey != DHD_PKTID_INVALID);
	return nkey; /* return locker's numbered key */
}

/*
 * dhd_pktid_map_save - Save a packet's parameters into a locker
 * corresponding to a previously reserved unique numbered key.
 */
static INLINE void
dhd_pktid_map_save(dhd_pub_t *dhd, dhd_pktid_map_handle_t *handle, void *pkt,
	uint32 nkey, dmaaddr_t pa, uint32 len, uint8 dir, void *dmah, void *secdma,
	dhd_pkttype_t pkttype)
{
	dhd_pktid_map_t *map;
	dhd_pktid_item_t *locker;

	ASSERT(handle != NULL);
	map = (dhd_pktid_map_t *)handle;

	if ((nkey == DHD_PKTID_INVALID) || (nkey > DHD_PKIDMAP_ITEMS(map->items))) {
		DHD_ERROR(("%s:%d: Error! saving invalid pktid<%u> pkttype<%u>\n",
			__FUNCTION__, __LINE__, nkey, pkttype));
#ifdef DHD_FW_COREDUMP
		if (dhd->memdump_enabled) {
			/* collect core dump */
			dhd->memdump_type = DUMP_TYPE_PKTID_INVALID;
			dhd_bus_mem_dump(dhd);
		}
#else
		ASSERT(0);
#endif /* DHD_FW_COREDUMP */
		return;
	}

	locker = &map->lockers[nkey];

	ASSERT(((locker->state == LOCKER_IS_BUSY) && (locker->pkt == pkt)) ||
		((locker->state == LOCKER_IS_RSVD) && (locker->pkt == NULL)));

	/* store contents in locker */
	locker->dir = dir;
	locker->pa = pa;
	locker->len = (uint16)len; /* 16bit len */
	locker->dmah = dmah; /* 16bit len */
	locker->secdma = secdma;
	locker->pkttype = pkttype;
	locker->pkt = pkt;
	locker->state = LOCKER_IS_BUSY; /* make this locker busy */
}

/**
 * dhd_pktid_map_alloc - Allocate a unique numbered key and save the packet
 * contents into the corresponding locker. Return the numbered key.
 */
static uint32 BCMFASTPATH
dhd_pktid_map_alloc(dhd_pub_t *dhd, dhd_pktid_map_handle_t *handle, void *pkt,
	dmaaddr_t pa, uint32 len, uint8 dir, void *dmah, void *secdma,
	dhd_pkttype_t pkttype)
{
	uint32 nkey;

	nkey = dhd_pktid_map_reserve(dhd, handle, pkt, pkttype);
	if (nkey != DHD_PKTID_INVALID) {
		dhd_pktid_map_save(dhd, handle, pkt, nkey, pa,
			len, dir, dmah, secdma, pkttype);
	}

	return nkey;
}

/**
 * dhd_pktid_map_free - Given a numbered key, return the locker contents.
 * dhd_pktid_map_free() is not reentrant, and is the caller's responsibility.
 * Caller may not free a pktid value DHD_PKTID_INVALID or an arbitrary pktid
 * value. Only a previously allocated pktid may be freed.
 */
static void * BCMFASTPATH
dhd_pktid_map_free(dhd_pub_t *dhd, dhd_pktid_map_handle_t *handle, uint32 nkey,
	dmaaddr_t *pa, uint32 *len, void **dmah, void **secdma, dhd_pkttype_t pkttype,
	bool rsv_locker)
{
	dhd_pktid_map_t *map;
	dhd_pktid_item_t *locker;
	void * pkt;
	unsigned long long locker_addr;

	ASSERT(handle != NULL);

	map = (dhd_pktid_map_t *)handle;

	if ((nkey == DHD_PKTID_INVALID) || (nkey > DHD_PKIDMAP_ITEMS(map->items))) {
		DHD_ERROR(("%s:%d: Error! Try to free invalid pktid<%u>, pkttype<%d>\n",
		           __FUNCTION__, __LINE__, nkey, pkttype));
#ifdef DHD_FW_COREDUMP
		if (dhd->memdump_enabled) {
			/* collect core dump */
			dhd->memdump_type = DUMP_TYPE_PKTID_INVALID;
			dhd_bus_mem_dump(dhd);
		}
#else
		ASSERT(0);
#endif /* DHD_FW_COREDUMP */
		return NULL;
	}

	locker = &map->lockers[nkey];

#if defined(DHD_PKTID_AUDIT_MAP)
	DHD_PKTID_AUDIT(dhd, map, nkey, DHD_DUPLICATE_FREE); /* Audit duplicate FREE */
#endif /* DHD_PKTID_AUDIT_MAP */

	/* Debug check for cloned numbered key */
	if (locker->state == LOCKER_IS_FREE) {
		DHD_ERROR(("%s:%d: Error! freeing already freed invalid pktid<%u>\n",
		           __FUNCTION__, __LINE__, nkey));
#ifdef DHD_FW_COREDUMP
		if (dhd->memdump_enabled) {
			/* collect core dump */
			dhd->memdump_type = DUMP_TYPE_PKTID_INVALID;
			dhd_bus_mem_dump(dhd);
		}
#else
		ASSERT(0);
#endif /* DHD_FW_COREDUMP */
		return NULL;
	}

	/* Check for the colour of the buffer i.e The buffer posted for TX,
	 * should be freed for TX completion. Similarly the buffer posted for
	 * IOCTL should be freed for IOCT completion etc.
	 */
	if ((pkttype != PKTTYPE_NO_CHECK) && (locker->pkttype != pkttype)) {

		DHD_ERROR(("%s:%d: Error! Invalid Buffer Free for pktid<%u> \n",
			__FUNCTION__, __LINE__, nkey));
#ifdef BCMDMA64OSL
		PHYSADDRTOULONG(locker->pa, locker_addr);
#else
		locker_addr = PHYSADDRLO(locker->pa);
#endif /* BCMDMA64OSL */
		DHD_ERROR(("%s:%d: locker->state <%d>, locker->pkttype <%d>,"
			"pkttype <%d> locker->pa <0x%llx> \n",
			__FUNCTION__, __LINE__, locker->state, locker->pkttype,
			pkttype, locker_addr));
#ifdef DHD_FW_COREDUMP
		if (dhd->memdump_enabled) {
			/* collect core dump */
			dhd->memdump_type = DUMP_TYPE_PKTID_INVALID;
			dhd_bus_mem_dump(dhd);
		}
#else
		ASSERT(0);
#endif /* DHD_FW_COREDUMP */
		return NULL;
	}

	if (rsv_locker == DHD_PKTID_FREE_LOCKER) {
		map->avail++;
		map->keys[map->avail] = nkey; /* make this numbered key available */
		locker->state = LOCKER_IS_FREE; /* open and free Locker */
	} else {
		/* pktid will be reused, but the locker does not have a valid pkt */
		locker->state = LOCKER_IS_RSVD;
	}

#if defined(DHD_PKTID_AUDIT_MAP)
	DHD_PKTID_AUDIT(dhd, map, nkey, DHD_TEST_IS_FREE);
#endif /* DHD_PKTID_AUDIT_MAP */

	*pa = locker->pa; /* return contents of locker */
	*len = (uint32)locker->len;
	*dmah = locker->dmah;
	*secdma = locker->secdma;

	pkt = locker->pkt;
	locker->pkt = NULL; /* Clear pkt */
	locker->len = 0;

	return pkt;
}

#else /* ! DHD_PCIE_PKTID */


typedef struct pktlist {
	PKT_LIST *tx_pkt_list;		/* list for tx packets */
	PKT_LIST *rx_pkt_list;		/* list for rx packets */
	PKT_LIST *ctrl_pkt_list;	/* list for ioctl/event buf post */
} pktlists_t;

/*
 * Given that each workitem only uses a 32bit pktid, only 32bit hosts may avail
 * of a one to one mapping 32bit pktptr and a 32bit pktid.
 *
 * - When PKTIDMAP is not used, DHD_NATIVE_TO_PKTID variants will never fail.
 * - Neither DHD_NATIVE_TO_PKTID nor DHD_PKTID_TO_NATIVE need to be protected by
 *   a lock.
 * - Hence DHD_PKTID_INVALID is not defined when DHD_PCIE_PKTID is undefined.
 */
#define DHD_PKTID32(pktptr32)	((uint32)(pktptr32))
#define DHD_PKTPTR32(pktid32)	((void *)(pktid32))


static INLINE uint32 dhd_native_to_pktid(dhd_pktid_map_handle_t *map, void *pktptr32,
	dmaaddr_t pa, uint32 dma_len, void *dmah, void *secdma,
	dhd_pkttype_t pkttype);
static INLINE void * dhd_pktid_to_native(dhd_pktid_map_handle_t *map, uint32 pktid32,
	dmaaddr_t *pa, uint32 *dma_len, void **dmah, void **secdma,
	dhd_pkttype_t pkttype);

static dhd_pktid_map_handle_t *
dhd_pktid_map_init(dhd_pub_t *dhd, uint32 num_items)
{
	osl_t *osh = dhd->osh;
	pktlists_t *handle = NULL;

	if ((handle = (pktlists_t *) MALLOCZ(osh, sizeof(pktlists_t))) == NULL) {
		DHD_ERROR(("%s:%d: MALLOC failed for lists allocation, size=%d\n",
		           __FUNCTION__, __LINE__, sizeof(pktlists_t)));
		goto error_done;
	}

	if ((handle->tx_pkt_list = (PKT_LIST *) MALLOC(osh, sizeof(PKT_LIST))) == NULL) {
		DHD_ERROR(("%s:%d: MALLOC failed for list allocation, size=%d\n",
		           __FUNCTION__, __LINE__, sizeof(PKT_LIST)));
		goto error;
	}

	if ((handle->rx_pkt_list = (PKT_LIST *) MALLOC(osh, sizeof(PKT_LIST))) == NULL) {
		DHD_ERROR(("%s:%d: MALLOC failed for list allocation, size=%d\n",
		           __FUNCTION__, __LINE__, sizeof(PKT_LIST)));
		goto error;
	}

	if ((handle->ctrl_pkt_list = (PKT_LIST *) MALLOC(osh, sizeof(PKT_LIST))) == NULL) {
		DHD_ERROR(("%s:%d: MALLOC failed for list allocation, size=%d\n",
		           __FUNCTION__, __LINE__, sizeof(PKT_LIST)));
		goto error;
	}

	PKTLIST_INIT(handle->tx_pkt_list);
	PKTLIST_INIT(handle->rx_pkt_list);
	PKTLIST_INIT(handle->ctrl_pkt_list);

	return (dhd_pktid_map_handle_t *) handle;

error:
	if (handle->ctrl_pkt_list) {
		MFREE(osh, handle->ctrl_pkt_list, sizeof(PKT_LIST));
	}

	if (handle->rx_pkt_list) {
		MFREE(osh, handle->rx_pkt_list, sizeof(PKT_LIST));
	}

	if (handle->tx_pkt_list) {
		MFREE(osh, handle->tx_pkt_list, sizeof(PKT_LIST));
	}

	if (handle) {
		MFREE(osh, handle, sizeof(pktlists_t));
	}

error_done:
	return (dhd_pktid_map_handle_t *)NULL;
}

static void
dhd_pktid_map_fini(dhd_pub_t *dhd, dhd_pktid_map_handle_t *map)
{
	osl_t *osh = dhd->osh;
	pktlists_t *handle = (pktlists_t *) map;

	ASSERT(handle != NULL);
	if (handle == (pktlists_t *)NULL)
		return;

	if (handle->ctrl_pkt_list) {
		PKTLIST_FINI(handle->ctrl_pkt_list);
		MFREE(osh, handle->ctrl_pkt_list, sizeof(PKT_LIST));
	}

	if (handle->rx_pkt_list) {
		PKTLIST_FINI(handle->rx_pkt_list);
		MFREE(osh, handle->rx_pkt_list, sizeof(PKT_LIST));
	}

	if (handle->tx_pkt_list) {
		PKTLIST_FINI(handle->tx_pkt_list);
		MFREE(osh, handle->tx_pkt_list, sizeof(PKT_LIST));
	}

	if (handle) {
		MFREE(osh, handle, sizeof(pktlists_t));
	}
}

/** Save dma parameters into the packet's pkttag and convert a pktptr to pktid */
static INLINE uint32
dhd_native_to_pktid(dhd_pktid_map_handle_t *map, void *pktptr32,
	dmaaddr_t pa, uint32 dma_len, void *dmah, void *secdma,
	dhd_pkttype_t pkttype)
{
	pktlists_t *handle = (pktlists_t *) map;
	ASSERT(pktptr32 != NULL);
	DHD_PKT_SET_DMA_LEN(pktptr32, dma_len);
	DHD_PKT_SET_DMAH(pktptr32, dmah);
	DHD_PKT_SET_PA(pktptr32, pa);
	DHD_PKT_SET_SECDMA(pktptr32, secdma);

	if (pkttype == PKTTYPE_DATA_TX) {
		PKTLIST_ENQ(handle->tx_pkt_list,  pktptr32);
	} else if (pkttype == PKTTYPE_DATA_RX) {
		PKTLIST_ENQ(handle->rx_pkt_list,  pktptr32);
	} else {
		PKTLIST_ENQ(handle->ctrl_pkt_list,  pktptr32);
	}

	return DHD_PKTID32(pktptr32);
}

/** Convert a pktid to pktptr and retrieve saved dma parameters from packet */
static INLINE void *
dhd_pktid_to_native(dhd_pktid_map_handle_t *map, uint32 pktid32,
	dmaaddr_t *pa, uint32 *dma_len, void **dmah, void **secdma,
	dhd_pkttype_t pkttype)
{
	pktlists_t *handle = (pktlists_t *) map;
	void *pktptr32;

	ASSERT(pktid32 != 0U);
	pktptr32 = DHD_PKTPTR32(pktid32);
	*dma_len = DHD_PKT_GET_DMA_LEN(pktptr32);
	*dmah = DHD_PKT_GET_DMAH(pktptr32);
	*pa = DHD_PKT_GET_PA(pktptr32);
	*secdma = DHD_PKT_GET_SECDMA(pktptr32);

	if (pkttype == PKTTYPE_DATA_TX) {
		PKTLIST_UNLINK(handle->tx_pkt_list,  pktptr32);
	} else if (pkttype == PKTTYPE_DATA_RX) {
		PKTLIST_UNLINK(handle->rx_pkt_list,  pktptr32);
	} else {
		PKTLIST_UNLINK(handle->ctrl_pkt_list,  pktptr32);
	}

	return pktptr32;
}

#define DHD_NATIVE_TO_PKTID_RSV(dhd, map, pkt, pkttype)  DHD_PKTID32(pkt)

#define DHD_NATIVE_TO_PKTID_SAVE(dhd, map, pkt, nkey, pa, len, dma_dir, dmah, secdma, pkttype) \
	({ BCM_REFERENCE(dhd); BCM_REFERENCE(nkey); BCM_REFERENCE(dma_dir); \
	   dhd_native_to_pktid((dhd_pktid_map_handle_t *) map, (pkt), (pa), (len), \
			   (dmah), (secdma), (dhd_pkttype_t)(pkttype)); \
	})

#define DHD_NATIVE_TO_PKTID(dhd, map, pkt, pa, len, dma_dir, dmah, secdma, pkttype) \
	({ BCM_REFERENCE(dhd); BCM_REFERENCE(dma_dir); \
	   dhd_native_to_pktid((dhd_pktid_map_handle_t *) map, (pkt), (pa), (len), \
			   (dmah), (secdma), (dhd_pkttype_t)(pkttype)); \
	})

#define DHD_PKTID_TO_NATIVE(dhd, map, pktid, pa, len, dmah, secdma, pkttype) \
	({ BCM_REFERENCE(dhd); BCM_REFERENCE(pkttype);	\
		dhd_pktid_to_native((dhd_pktid_map_handle_t *) map, (uint32)(pktid), \
				(dmaaddr_t *)&(pa), (uint32 *)&(len), (void **)&(dmah), \
				(void **)&secdma, (dhd_pkttype_t)(pkttype)); \
	})

#define DHD_PKTID_AVAIL(map)  (~0)

#endif /* ! DHD_PCIE_PKTID */

/* +------------------ End of PCIE DHD PKTID MAPPER  -----------------------+ */


/**
 * The PCIE FD protocol layer is constructed in two phases:
 *    Phase 1. dhd_prot_attach()
 *    Phase 2. dhd_prot_init()
 *
 * dhd_prot_attach() - Allocates a dhd_prot_t object and resets all its fields.
 * All Common rings are allose attached (msgbuf_ring_t objects are allocated
 * with DMA-able buffers).
 * All dhd_dma_buf_t objects are also allocated here.
 *
 * As dhd_prot_attach is invoked prior to the pcie_shared object is read, any
 * initialization of objects that requires information advertized by the dongle
 * may not be performed here.
 * E.g. the number of TxPost flowrings is not know at this point, neither do
 * we know shich form of D2H DMA sync mechanism is advertized by the dongle, or
 * whether the dongle supports DMA-ing of WR/RD indices for the H2D and/or D2H
 * rings (common + flow).
 *
 * dhd_prot_init() is invoked after the bus layer has fetched the information
 * advertized by the dongle in the pcie_shared_t.
 */
int
dhd_prot_attach(dhd_pub_t *dhd)
{
	osl_t *osh = dhd->osh;
	dhd_prot_t *prot;

	/* Allocate prot structure */
	if (!(prot = (dhd_prot_t *)DHD_OS_PREALLOC(dhd, DHD_PREALLOC_PROT,
		sizeof(dhd_prot_t)))) {
		DHD_ERROR(("%s: kmalloc failed\n", __FUNCTION__));
		goto fail;
	}
	memset(prot, 0, sizeof(*prot));

	prot->osh = osh;
	dhd->prot = prot;

	/* DMAing ring completes supported? FALSE by default  */
	dhd->dma_d2h_ring_upd_support = FALSE;
	dhd->dma_h2d_ring_upd_support = FALSE;
	dhd->dma_ring_upd_overwrite = FALSE;

	dhd->idma_inited = 0;
	dhd->ifrm_inited = 0;

	/* Common Ring Allocations */

	/* Ring  0: H2D Control Submission */
	if (dhd_prot_ring_attach(dhd, &prot->h2dring_ctrl_subn, "h2dctrl",
	        H2DRING_CTRL_SUB_MAX_ITEM, H2DRING_CTRL_SUB_ITEMSIZE,
	        BCMPCIE_H2D_MSGRING_CONTROL_SUBMIT) != BCME_OK) {
		DHD_ERROR(("%s: dhd_prot_ring_attach H2D Ctrl Submission failed\n",
			__FUNCTION__));
		goto fail;
	}

	/* Ring  1: H2D Receive Buffer Post */
	if (dhd_prot_ring_attach(dhd, &prot->h2dring_rxp_subn, "h2drxp",
	        H2DRING_RXPOST_MAX_ITEM, H2DRING_RXPOST_ITEMSIZE,
	        BCMPCIE_H2D_MSGRING_RXPOST_SUBMIT) != BCME_OK) {
		DHD_ERROR(("%s: dhd_prot_ring_attach H2D RxPost failed\n",
			__FUNCTION__));
		goto fail;
	}

	/* Ring  2: D2H Control Completion */
	if (dhd_prot_ring_attach(dhd, &prot->d2hring_ctrl_cpln, "d2hctrl",
	        D2HRING_CTRL_CMPLT_MAX_ITEM, D2HRING_CTRL_CMPLT_ITEMSIZE,
	        BCMPCIE_D2H_MSGRING_CONTROL_COMPLETE) != BCME_OK) {
		DHD_ERROR(("%s: dhd_prot_ring_attach D2H Ctrl Completion failed\n",
			__FUNCTION__));
		goto fail;
	}

	/* Ring  3: D2H Transmit Complete */
	if (dhd_prot_ring_attach(dhd, &prot->d2hring_tx_cpln, "d2htxcpl",
	        D2HRING_TXCMPLT_MAX_ITEM, D2HRING_TXCMPLT_ITEMSIZE,
	        BCMPCIE_D2H_MSGRING_TX_COMPLETE) != BCME_OK) {
		DHD_ERROR(("%s: dhd_prot_ring_attach D2H Tx Completion failed\n",
			__FUNCTION__));
		goto fail;

	}

	/* Ring  4: D2H Receive Complete */
	if (dhd_prot_ring_attach(dhd, &prot->d2hring_rx_cpln, "d2hrxcpl",
	        D2HRING_RXCMPLT_MAX_ITEM, D2HRING_RXCMPLT_ITEMSIZE,
	        BCMPCIE_D2H_MSGRING_RX_COMPLETE) != BCME_OK) {
		DHD_ERROR(("%s: dhd_prot_ring_attach D2H Rx Completion failed\n",
			__FUNCTION__));
		goto fail;

	}

	/*
	 * Max number of flowrings is not yet known. msgbuf_ring_t with DMA-able
	 * buffers for flowrings will be instantiated, in dhd_prot_init() .
	 * See dhd_prot_flowrings_pool_attach()
	 */
	/* ioctl response buffer */
	if (dhd_dma_buf_alloc(dhd, &prot->retbuf, IOCT_RETBUF_SIZE)) {
		goto fail;
	}

	/* IOCTL request buffer */
	if (dhd_dma_buf_alloc(dhd, &prot->ioctbuf, IOCT_RETBUF_SIZE)) {
		goto fail;
	}

	/* Host TS request buffer one buffer for now */
	if (dhd_dma_buf_alloc(dhd, &prot->hostts_req_buf, CTRLSUB_HOSTTS_MEESAGE_SIZE)) {
		goto fail;
	}
	prot->hostts_req_buf_inuse = FALSE;

	/* Scratch buffer for dma rx offset */
#ifdef BCM_HOST_BUF
	if (dhd_dma_buf_alloc(dhd, &prot->d2h_dma_scratch_buf,
		ROUNDUP(DMA_D2H_SCRATCH_BUF_LEN, 16) + DMA_HOST_BUFFER_LEN))
#else
	if (dhd_dma_buf_alloc(dhd, &prot->d2h_dma_scratch_buf, DMA_D2H_SCRATCH_BUF_LEN))
#endif /* BCM_HOST_BUF */
	{
		goto fail;
	}

	/* scratch buffer bus throughput measurement */
	if (dhd_dma_buf_alloc(dhd, &prot->host_bus_throughput_buf, DHD_BUS_TPUT_BUF_LEN)) {
		goto fail;
	}

#ifdef DHD_RX_CHAINING
	dhd_rxchain_reset(&prot->rxchain);
#endif

	prot->rx_lock = dhd_os_spin_lock_init(dhd->osh);

	prot->pktid_ctrl_map = DHD_NATIVE_TO_PKTID_INIT(dhd, MAX_CTRL_PKTID);
	if (prot->pktid_ctrl_map == NULL) {
		goto fail;
	}

	prot->pktid_rx_map = DHD_NATIVE_TO_PKTID_INIT(dhd, MAX_RX_PKTID);
	if (prot->pktid_rx_map == NULL)
		goto fail;

	prot->pktid_tx_map = DHD_NATIVE_TO_PKTID_INIT(dhd, MAX_TX_PKTID);
	if (prot->pktid_rx_map == NULL)
		goto fail;

#ifdef IOCTLRESP_USE_CONSTMEM
	prot->pktid_map_handle_ioctl = DHD_NATIVE_TO_PKTID_INIT(dhd,
		DHD_FLOWRING_MAX_IOCTLRESPBUF_POST);
	if (prot->pktid_map_handle_ioctl == NULL) {
		goto fail;
	}
#endif /* IOCTLRESP_USE_CONSTMEM */

	   /* Initialize the work queues to be used by the Load Balancing logic */
#if defined(DHD_LB_TXC)
	{
		void *buffer;
		buffer = MALLOC(dhd->osh, sizeof(void*) * DHD_LB_WORKQ_SZ);
		bcm_workq_init(&prot->tx_compl_prod, &prot->tx_compl_cons,
			buffer, DHD_LB_WORKQ_SZ);
		prot->tx_compl_prod_sync = 0;
		DHD_INFO(("%s: created tx_compl_workq <%p,%d>\n",
			__FUNCTION__, buffer, DHD_LB_WORKQ_SZ));
	}
#endif /* DHD_LB_TXC */

#if defined(DHD_LB_RXC)
	{
		void *buffer;
		buffer = MALLOC(dhd->osh, sizeof(void*) * DHD_LB_WORKQ_SZ);
		bcm_workq_init(&prot->rx_compl_prod, &prot->rx_compl_cons,
			buffer, DHD_LB_WORKQ_SZ);
		prot->rx_compl_prod_sync = 0;
		DHD_INFO(("%s: created rx_compl_workq <%p,%d>\n",
			__FUNCTION__, buffer, DHD_LB_WORKQ_SZ));
	}
#endif /* DHD_LB_RXC */
	/* Initialize trap buffer */
	if (dhd_dma_buf_alloc(dhd, &dhd->prot->fw_trap_buf, BCMPCIE_EXT_TRAP_DATA_MAXLEN)) {
		DHD_ERROR(("%s: dhd_init_trap_buffer falied\n", __FUNCTION__));
		goto fail;
	}

	return BCME_OK;

fail:

#ifndef CONFIG_DHD_USE_STATIC_BUF
	if (prot != NULL) {
		dhd_prot_detach(dhd);
	}
#endif /* CONFIG_DHD_USE_STATIC_BUF */

	return BCME_NOMEM;
} /* dhd_prot_attach */

void
dhd_set_host_cap(dhd_pub_t *dhd)
{
	uint32 data = 0;
	dhd_prot_t *prot = dhd->prot;

	if (dhd->bus->api.fw_rev >= PCIE_SHARED_VERSION_6) {
		if (dhd->h2d_phase_supported) {

			data |= HOSTCAP_H2D_VALID_PHASE;

			if (dhd->force_dongletrap_on_bad_h2d_phase) {
				data |= HOSTCAP_H2D_ENABLE_TRAP_ON_BADPHASE;
			}
		}
		if (prot->host_ipc_version > prot->device_ipc_version) {
			prot->active_ipc_version = prot->device_ipc_version;
		} else {
			prot->active_ipc_version = prot->host_ipc_version;
		}

		data |= prot->active_ipc_version;

		if (dhdpcie_bus_get_pcie_hostready_supported(dhd->bus)) {

			DHD_INFO(("Advertise Hostready Capability\n"));

			data |= HOSTCAP_H2D_ENABLE_HOSTRDY;
		}
#ifdef PCIE_INB_DW
		if (dhdpcie_bus_get_pcie_inband_dw_supported(dhd->bus)) {
			DHD_INFO(("Advertise Inband-DW Capability\n"));
			data |= HOSTCAP_DS_INBAND_DW;
			data |= HOSTCAP_DS_NO_OOB_DW;
			dhdpcie_bus_enab_pcie_dw(dhd->bus, DEVICE_WAKE_INB);
		} else
#endif /* PCIE_INB_DW */
#ifdef PCIE_OOB
		if (dhdpcie_bus_get_pcie_oob_dw_supported(dhd->bus)) {
			dhdpcie_bus_enab_pcie_dw(dhd->bus, DEVICE_WAKE_OOB);
		} else
#endif /* PCIE_OOB */
		{
			/* Disable DS altogether */
			data |= HOSTCAP_DS_NO_OOB_DW;
			dhdpcie_bus_enab_pcie_dw(dhd->bus, DEVICE_WAKE_NONE);
		}

		if (dhdpcie_bus_get_pcie_idma_supported(dhd->bus)) {

			DHD_ERROR(("IDMA inited\n"));
			data |= HOSTCAP_H2D_IDMA;
			dhd->idma_inited = TRUE;
		}

		if (dhdpcie_bus_get_pcie_ifrm_supported(dhd->bus)) {
			DHD_ERROR(("IFRM Inited\n"));
			data |= HOSTCAP_H2D_IFRM;
			dhd->ifrm_inited = TRUE;
			dhd->dma_h2d_ring_upd_support = FALSE;
			dhd_prot_dma_indx_free(dhd);
		}

		/* Indicate support for TX status metadata */
		data |= HOSTCAP_TXSTATUS_METADATA;

		/* Indicate support for extended trap data */
		data |= HOSTCAP_EXTENDED_TRAP_DATA;

		DHD_INFO(("%s:Active Ver:%d, Host Ver:%d, FW Ver:%d\n",
			__FUNCTION__,
			prot->active_ipc_version, prot->host_ipc_version,
			prot->device_ipc_version));

		dhd_bus_cmn_writeshared(dhd->bus, &data, sizeof(uint32), HOST_API_VERSION, 0);
		dhd_bus_cmn_writeshared(dhd->bus, &prot->fw_trap_buf.pa,
			sizeof(prot->fw_trap_buf.pa), DNGL_TO_HOST_TRAP_ADDR, 0);
	}
#ifdef HOFFLOAD_MODULES
		dhd_bus_cmn_writeshared(dhd->bus, &dhd->hmem.data_addr,
			sizeof(dhd->hmem.data_addr), WRT_HOST_MODULE_ADDR, 0);
#endif

#ifdef DHD_TIMESYNC
	dhd_timesync_notify_ipc_rev(dhd->ts, prot->active_ipc_version);
#endif /* DHD_TIMESYNC */
}

/**
 * dhd_prot_init - second stage of dhd_prot_attach. Now that the dongle has
 * completed it's initialization of the pcie_shared structure, we may now fetch
 * the dongle advertized features and adjust the protocol layer accordingly.
 *
 * dhd_prot_init() may be invoked again after a dhd_prot_reset().
 */
int
dhd_prot_init(dhd_pub_t *dhd)
{
	sh_addr_t base_addr;
	dhd_prot_t *prot = dhd->prot;
	int ret = 0;

	/**
	 * A user defined value can be assigned to global variable h2d_max_txpost via
	 * 1. DHD IOVAR h2d_max_txpost, before firmware download
	 * 2. module parameter h2d_max_txpost
	 * prot->h2d_max_txpost is assigned with H2DRING_TXPOST_MAX_ITEM,
	 * if user has not defined any buffers by one of the above methods.
	 */
	prot->h2d_max_txpost = (uint16)h2d_max_txpost;

	DHD_ERROR(("%s:%d: h2d_max_txpost = %d\n", __FUNCTION__, __LINE__, prot->h2d_max_txpost));

	/* Read max rx packets supported by dongle */
	dhd_bus_cmn_readshared(dhd->bus, &prot->max_rxbufpost, MAX_HOST_RXBUFS, 0);
	if (prot->max_rxbufpost == 0) {
		/* This would happen if the dongle firmware is not */
		/* using the latest shared structure template */
		prot->max_rxbufpost = DEFAULT_RX_BUFFERS_TO_POST;
	}
	DHD_INFO(("%s:%d: MAX_RXBUFPOST = %d\n", __FUNCTION__, __LINE__, prot->max_rxbufpost));

	/* Initialize.  bzero() would blow away the dma pointers. */
	prot->max_eventbufpost = DHD_FLOWRING_MAX_EVENTBUF_POST;
	prot->max_ioctlrespbufpost = DHD_FLOWRING_MAX_IOCTLRESPBUF_POST;
	prot->max_infobufpost = DHD_H2D_INFORING_MAX_BUF_POST;
	prot->max_tsbufpost = DHD_MAX_TSBUF_POST;

	prot->cur_ioctlresp_bufs_posted = 0;
	prot->active_tx_count = 0;
	prot->data_seq_no = 0;
	prot->ioctl_seq_no = 0;
	prot->rxbufpost = 0;
	prot->cur_event_bufs_posted = 0;
	prot->ioctl_state = 0;
	prot->curr_ioctl_cmd = 0;
	prot->cur_ts_bufs_posted = 0;
	prot->infobufpost = 0;

	prot->dmaxfer.srcmem.va = NULL;
	prot->dmaxfer.dstmem.va = NULL;
	prot->dmaxfer.in_progress = FALSE;

	prot->metadata_dbg = FALSE;
	prot->rx_metadata_offset = 0;
	prot->tx_metadata_offset = 0;
	prot->txp_threshold = TXP_FLUSH_MAX_ITEMS_FLUSH_CNT;

	/* To catch any rollover issues fast, starting with higher ioctl_trans_id */
	prot->ioctl_trans_id = MAXBITVAL(NBITS(prot->ioctl_trans_id)) - BUFFER_BEFORE_ROLLOVER;
	prot->ioctl_state = 0;
	prot->ioctl_status = 0;
	prot->ioctl_resplen = 0;
	prot->ioctl_received = IOCTL_WAIT;

	/* Register the interrupt function upfront */
	/* remove corerev checks in data path */
	prot->mb_ring_fn = dhd_bus_get_mbintr_fn(dhd->bus);

	prot->mb_2_ring_fn = dhd_bus_get_mbintr_2_fn(dhd->bus);

	/* Initialize Common MsgBuf Rings */

	prot->device_ipc_version = dhd->bus->api.fw_rev;
	prot->host_ipc_version = PCIE_SHARED_VERSION;

	/* Init the host API version */
	dhd_set_host_cap(dhd);

	dhd_prot_ring_init(dhd, &prot->h2dring_ctrl_subn);
	dhd_prot_ring_init(dhd, &prot->h2dring_rxp_subn);
	dhd_prot_ring_init(dhd, &prot->d2hring_ctrl_cpln);

	/* Make it compatibile with pre-rev7 Firmware */
	if (prot->active_ipc_version < PCIE_SHARED_VERSION_7) {
		prot->d2hring_tx_cpln.item_len =
			D2HRING_TXCMPLT_ITEMSIZE_PREREV7;
		prot->d2hring_rx_cpln.item_len =
			D2HRING_RXCMPLT_ITEMSIZE_PREREV7;
	}
	dhd_prot_ring_init(dhd, &prot->d2hring_tx_cpln);
	dhd_prot_ring_init(dhd, &prot->d2hring_rx_cpln);

	dhd_prot_d2h_sync_init(dhd);

	dhd_prot_h2d_sync_init(dhd);

#ifdef PCIE_INB_DW
	/* Set the initial DS state */
	if (INBAND_DW_ENAB(dhd->bus)) {
		dhdpcie_bus_set_pcie_inband_dw_state(dhd->bus,
			DW_DEVICE_DS_ACTIVE);
	}
#endif /* PCIE_INB_DW */

	/* init the scratch buffer */
	dhd_base_addr_htolpa(&base_addr, prot->d2h_dma_scratch_buf.pa);
	dhd_bus_cmn_writeshared(dhd->bus, &base_addr, sizeof(base_addr),
		D2H_DMA_SCRATCH_BUF, 0);
	dhd_bus_cmn_writeshared(dhd->bus, &prot->d2h_dma_scratch_buf.len,
		sizeof(prot->d2h_dma_scratch_buf.len), D2H_DMA_SCRATCH_BUF_LEN, 0);

	/* If supported by the host, indicate the memory block
	 * for completion writes / submission reads to shared space
	 */
	if (dhd->dma_d2h_ring_upd_support) {
		dhd_base_addr_htolpa(&base_addr, prot->d2h_dma_indx_wr_buf.pa);
		dhd_bus_cmn_writeshared(dhd->bus, &base_addr, sizeof(base_addr),
			D2H_DMA_INDX_WR_BUF, 0);
		dhd_base_addr_htolpa(&base_addr, prot->h2d_dma_indx_rd_buf.pa);
		dhd_bus_cmn_writeshared(dhd->bus, &base_addr, sizeof(base_addr),
			H2D_DMA_INDX_RD_BUF, 0);
	}

	if (dhd->dma_h2d_ring_upd_support || IDMA_ENAB(dhd)) {
		dhd_base_addr_htolpa(&base_addr, prot->h2d_dma_indx_wr_buf.pa);
		dhd_bus_cmn_writeshared(dhd->bus, &base_addr, sizeof(base_addr),
			H2D_DMA_INDX_WR_BUF, 0);
		dhd_base_addr_htolpa(&base_addr, prot->d2h_dma_indx_rd_buf.pa);
		dhd_bus_cmn_writeshared(dhd->bus, &base_addr, sizeof(base_addr),
			D2H_DMA_INDX_RD_BUF, 0);

	}

	/* Signal to the dongle that common ring init is complete */
	dhd_bus_hostready(dhd->bus);

	/*
	 * If the DMA-able buffers for flowring needs to come from a specific
	 * contiguous memory region, then setup prot->flowrings_dma_buf here.
	 * dhd_prot_flowrings_pool_attach() will carve out DMA-able buffers from
	 * this contiguous memory region, for each of the flowrings.
	 */

	/* Pre-allocate pool of msgbuf_ring for flowrings */
	if (dhd_prot_flowrings_pool_attach(dhd) != BCME_OK) {
		return BCME_ERROR;
	}

	/* If IFRM is enabled, wait for FW to setup the DMA channel */
	if (IFRM_ENAB(dhd)) {
		dhd_base_addr_htolpa(&base_addr, prot->h2d_ifrm_indx_wr_buf.pa);
		dhd_bus_cmn_writeshared(dhd->bus, &base_addr, sizeof(base_addr),
			H2D_IFRM_INDX_WR_BUF, 0);
	}

	/* See if info rings could be created */
	if (dhd->bus->api.fw_rev >= PCIE_SHARED_VERSION_6) {
		if ((ret = dhd_prot_init_info_rings(dhd)) != BCME_OK) {
			/* For now log and proceed, further clean up action maybe necessary
			 * when we have more clarity.
			 */
			DHD_ERROR(("%s Info rings couldn't be created: Err Code%d",
				__FUNCTION__, ret));
		}
	}

	/* Host should configure soft doorbells if needed ... here */

	/* Post to dongle host configured soft doorbells */
	dhd_msgbuf_ring_config_d2h_soft_doorbell(dhd);

	/* Post buffers for packet reception and ioctl/event responses */
	dhd_msgbuf_rxbuf_post(dhd, FALSE); /* alloc pkt ids */
	dhd_msgbuf_rxbuf_post_ioctlresp_bufs(dhd);
	/* Fix re-entry problem without general lock */
	atomic_set(&dhd_msgbuf_rxbuf_post_event_bufs_running, 0);
	dhd_msgbuf_rxbuf_post_event_bufs(dhd);

	return BCME_OK;
} /* dhd_prot_init */


/**
 * dhd_prot_detach - PCIE FD protocol layer destructor.
 * Unlink, frees allocated protocol memory (including dhd_prot)
 */
void dhd_prot_detach(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;

	/* Stop the protocol module */
	if (prot) {

		/* free up all DMA-able buffers allocated during prot attach/init */

		dhd_dma_buf_free(dhd, &prot->d2h_dma_scratch_buf);
		dhd_dma_buf_free(dhd, &prot->retbuf);
		dhd_dma_buf_free(dhd, &prot->ioctbuf);
		dhd_dma_buf_free(dhd, &prot->host_bus_throughput_buf);
		dhd_dma_buf_free(dhd, &prot->hostts_req_buf);
		dhd_dma_buf_free(dhd, &prot->fw_trap_buf);

		/* DMA-able buffers for DMAing H2D/D2H WR/RD indices */
		dhd_dma_buf_free(dhd, &prot->h2d_dma_indx_wr_buf);
		dhd_dma_buf_free(dhd, &prot->h2d_dma_indx_rd_buf);
		dhd_dma_buf_free(dhd, &prot->d2h_dma_indx_wr_buf);
		dhd_dma_buf_free(dhd, &prot->d2h_dma_indx_rd_buf);

		dhd_dma_buf_free(dhd, &prot->h2d_ifrm_indx_wr_buf);

		/* Common MsgBuf Rings */
		dhd_prot_ring_detach(dhd, &prot->h2dring_ctrl_subn);
		dhd_prot_ring_detach(dhd, &prot->h2dring_rxp_subn);
		dhd_prot_ring_detach(dhd, &prot->d2hring_ctrl_cpln);
		dhd_prot_ring_detach(dhd, &prot->d2hring_tx_cpln);
		dhd_prot_ring_detach(dhd, &prot->d2hring_rx_cpln);

		/* Detach each DMA-able buffer and free the pool of msgbuf_ring_t */
		dhd_prot_flowrings_pool_detach(dhd);

		/* detach info rings */
		dhd_prot_detach_info_rings(dhd);

		/* if IOCTLRESP_USE_CONSTMEM is defined IOCTL PKTs use pktid_map_handle_ioctl
		 * handler and PKT memory is allocated using alloc_ioctl_return_buffer(), Otherwise
		 * they will be part of pktid_ctrl_map handler and PKT memory is allocated using
		 * PKTGET_STATIC (if DHD_USE_STATIC_CTRLBUF is defined) OR PKGET.
		 * Similarly for freeing PKT buffers DHD_NATIVE_TO_PKTID_FINI will be used
		 * which calls PKTFREE_STATIC (if DHD_USE_STATIC_CTRLBUF is defined) OR PKFREE.
		 * Else if IOCTLRESP_USE_CONSTMEM is defined IOCTL PKTs will be freed using
		 * DHD_NATIVE_TO_PKTID_FINI_IOCTL which calls free_ioctl_return_buffer.
		 */
		DHD_NATIVE_TO_PKTID_FINI(dhd, prot->pktid_ctrl_map);
		DHD_NATIVE_TO_PKTID_FINI(dhd, prot->pktid_rx_map);
		DHD_NATIVE_TO_PKTID_FINI(dhd, prot->pktid_tx_map);
#ifdef IOCTLRESP_USE_CONSTMEM
		DHD_NATIVE_TO_PKTID_FINI_IOCTL(dhd, prot->pktid_map_handle_ioctl);
#endif

		dhd_os_spin_lock_deinit(dhd->osh, prot->rx_lock);

#ifndef CONFIG_DHD_USE_STATIC_BUF
		MFREE(dhd->osh, dhd->prot, sizeof(dhd_prot_t));
#endif /* CONFIG_DHD_USE_STATIC_BUF */

#if defined(DHD_LB_TXC)
		if (prot->tx_compl_prod.buffer)
			MFREE(dhd->osh, prot->tx_compl_prod.buffer,
			      sizeof(void*) * DHD_LB_WORKQ_SZ);
#endif /* DHD_LB_TXC */
#if defined(DHD_LB_RXC)
		if (prot->rx_compl_prod.buffer)
			MFREE(dhd->osh, prot->rx_compl_prod.buffer,
			      sizeof(void*) * DHD_LB_WORKQ_SZ);
#endif /* DHD_LB_RXC */

		dhd->prot = NULL;
	}
} /* dhd_prot_detach */


/**
 * dhd_prot_reset - Reset the protocol layer without freeing any objects.
 * This may be invoked to soft reboot the dongle, without having to
 * detach and attach the entire protocol layer.
 *
 * After dhd_prot_reset(), dhd_prot_init() may be invoked
 * without going througha dhd_prot_attach() phase.
 */
void
dhd_prot_reset(dhd_pub_t *dhd)
{
	struct dhd_prot *prot = dhd->prot;

	DHD_TRACE(("%s\n", __FUNCTION__));

	if (prot == NULL) {
		return;
	}

	dhd_prot_flowrings_pool_reset(dhd);

	/* Reset Common MsgBuf Rings */
	dhd_prot_ring_reset(dhd, &prot->h2dring_ctrl_subn);
	dhd_prot_ring_reset(dhd, &prot->h2dring_rxp_subn);
	dhd_prot_ring_reset(dhd, &prot->d2hring_ctrl_cpln);
	dhd_prot_ring_reset(dhd, &prot->d2hring_tx_cpln);
	dhd_prot_ring_reset(dhd, &prot->d2hring_rx_cpln);

	/* Reset info rings */
	if (prot->h2dring_info_subn) {
		dhd_prot_ring_reset(dhd, prot->h2dring_info_subn);
	}

	if (prot->d2hring_info_cpln) {
		dhd_prot_ring_reset(dhd, prot->d2hring_info_cpln);
	}

	/* Reset all DMA-able buffers allocated during prot attach */
	dhd_dma_buf_reset(dhd, &prot->d2h_dma_scratch_buf);
	dhd_dma_buf_reset(dhd, &prot->retbuf);
	dhd_dma_buf_reset(dhd, &prot->ioctbuf);
	dhd_dma_buf_reset(dhd, &prot->host_bus_throughput_buf);
	dhd_dma_buf_reset(dhd, &prot->hostts_req_buf);
	dhd_dma_buf_reset(dhd, &prot->fw_trap_buf);

	dhd_dma_buf_reset(dhd, &prot->h2d_ifrm_indx_wr_buf);

	/* Reset all DMA-able buffers for DMAing H2D/D2H WR/RD indices */
	dhd_dma_buf_reset(dhd, &prot->h2d_dma_indx_rd_buf);
	dhd_dma_buf_reset(dhd, &prot->h2d_dma_indx_wr_buf);
	dhd_dma_buf_reset(dhd, &prot->d2h_dma_indx_rd_buf);
	dhd_dma_buf_reset(dhd, &prot->d2h_dma_indx_wr_buf);


	prot->rx_metadata_offset = 0;
	prot->tx_metadata_offset = 0;

	prot->rxbufpost = 0;
	prot->cur_event_bufs_posted = 0;
	prot->cur_ioctlresp_bufs_posted = 0;

	prot->active_tx_count = 0;
	prot->data_seq_no = 0;
	prot->ioctl_seq_no = 0;
	prot->ioctl_state = 0;
	prot->curr_ioctl_cmd = 0;
	prot->ioctl_received = IOCTL_WAIT;
	/* To catch any rollover issues fast, starting with higher ioctl_trans_id */
	prot->ioctl_trans_id = MAXBITVAL(NBITS(prot->ioctl_trans_id)) - BUFFER_BEFORE_ROLLOVER;

	/* dhd_flow_rings_init is located at dhd_bus_start,
	 * so when stopping bus, flowrings shall be deleted
	 */
	if (dhd->flow_rings_inited) {
		dhd_flow_rings_deinit(dhd);
	}

	/* Reset PKTID map */
	DHD_NATIVE_TO_PKTID_RESET(dhd, prot->pktid_ctrl_map);
	DHD_NATIVE_TO_PKTID_RESET(dhd, prot->pktid_rx_map);
	DHD_NATIVE_TO_PKTID_RESET(dhd, prot->pktid_tx_map);
#ifdef IOCTLRESP_USE_CONSTMEM
	DHD_NATIVE_TO_PKTID_RESET_IOCTL(dhd, prot->pktid_map_handle_ioctl);
#endif /* IOCTLRESP_USE_CONSTMEM */
#ifdef DMAMAP_STATS
	dhd->dma_stats.txdata = dhd->dma_stats.txdata_sz = 0;
	dhd->dma_stats.rxdata = dhd->dma_stats.rxdata_sz = 0;
#ifndef IOCTLRESP_USE_CONSTMEM
	dhd->dma_stats.ioctl_rx = dhd->dma_stats.ioctl_rx_sz = 0;
#endif /* IOCTLRESP_USE_CONSTMEM */
	dhd->dma_stats.event_rx = dhd->dma_stats.event_rx_sz = 0;
	dhd->dma_stats.info_rx = dhd->dma_stats.info_rx_sz = 0;
	dhd->dma_stats.tsbuf_rx = dhd->dma_stats.tsbuf_rx_sz = 0;
#endif /* DMAMAP_STATS */
} /* dhd_prot_reset */

#if defined(DHD_LB_RXP)
#define DHD_LB_DISPATCH_RX_PROCESS(dhdp)	dhd_lb_dispatch_rx_process(dhdp)
#else /* !DHD_LB_RXP */
#define DHD_LB_DISPATCH_RX_PROCESS(dhdp)	do { /* noop */ } while (0)
#endif /* !DHD_LB_RXP */

#if defined(DHD_LB_RXC)
#define DHD_LB_DISPATCH_RX_COMPL(dhdp)	dhd_lb_dispatch_rx_compl(dhdp)
#else /* !DHD_LB_RXC */
#define DHD_LB_DISPATCH_RX_COMPL(dhdp)	do { /* noop */ } while (0)
#endif /* !DHD_LB_RXC */

#if defined(DHD_LB_TXC)
#define DHD_LB_DISPATCH_TX_COMPL(dhdp)	dhd_lb_dispatch_tx_compl(dhdp)
#else /* !DHD_LB_TXC */
#define DHD_LB_DISPATCH_TX_COMPL(dhdp)	do { /* noop */ } while (0)
#endif /* !DHD_LB_TXC */


#if defined(DHD_LB)
/* DHD load balancing: deferral of work to another online CPU */
/* DHD_LB_TXC DHD_LB_RXC DHD_LB_RXP dispatchers, in dhd_linux.c */
extern void dhd_lb_tx_compl_dispatch(dhd_pub_t *dhdp);
extern void dhd_lb_rx_compl_dispatch(dhd_pub_t *dhdp);
extern void dhd_lb_rx_napi_dispatch(dhd_pub_t *dhdp);
extern void dhd_lb_rx_pkt_enqueue(dhd_pub_t *dhdp, void *pkt, int ifidx);

#if defined(DHD_LB_RXP)
/**
 * dhd_lb_dispatch_rx_process - load balance by dispatch Rx processing work
 * to other CPU cores
 */
static INLINE void
dhd_lb_dispatch_rx_process(dhd_pub_t *dhdp)
{
	dhd_lb_rx_napi_dispatch(dhdp); /* dispatch rx_process_napi */
}
#endif /* DHD_LB_RXP */

#if defined(DHD_LB_TXC)
/**
 * dhd_lb_dispatch_tx_compl - load balance by dispatch Tx complition work
 * to other CPU cores
 */
static INLINE void
dhd_lb_dispatch_tx_compl(dhd_pub_t *dhdp, uint16 ring_idx)
{
	bcm_workq_prod_sync(&dhdp->prot->tx_compl_prod); /* flush WR index */
	dhd_lb_tx_compl_dispatch(dhdp); /* dispatch tx_compl_tasklet */
}

/**
 * DHD load balanced tx completion tasklet handler, that will perform the
 * freeing of packets on the selected CPU. Packet pointers are delivered to
 * this tasklet via the tx complete workq.
 */
void
dhd_lb_tx_compl_handler(unsigned long data)
{
	int elem_ix;
	void *pkt, **elem;
	dmaaddr_t pa;
	uint32 pa_len;
	dhd_pub_t *dhd = (dhd_pub_t *)data;
	dhd_prot_t *prot = dhd->prot;
	bcm_workq_t *workq = &prot->tx_compl_cons;
	uint32 count = 0;

	int curr_cpu;
	curr_cpu = get_cpu();
	put_cpu();

	DHD_LB_STATS_TXC_PERCPU_CNT_INCR(dhd);

	while (1) {
		elem_ix = bcm_ring_cons(WORKQ_RING(workq), DHD_LB_WORKQ_SZ);

		if (elem_ix == BCM_RING_EMPTY) {
			break;
		}

		elem = WORKQ_ELEMENT(void *, workq, elem_ix);
		pkt = *elem;

		DHD_INFO(("%s: tx_compl_cons pkt<%p>\n", __FUNCTION__, pkt));

		OSL_PREFETCH(PKTTAG(pkt));
		OSL_PREFETCH(pkt);

		pa = DHD_PKTTAG_PA((dhd_pkttag_fr_t *)PKTTAG(pkt));
		pa_len = DHD_PKTTAG_PA_LEN((dhd_pkttag_fr_t *)PKTTAG(pkt));

		DMA_UNMAP(dhd->osh, pa, pa_len, DMA_RX, 0, 0);
#if defined(BCMPCIE)
		dhd_txcomplete(dhd, pkt, true);
#endif 

		PKTFREE(dhd->osh, pkt, TRUE);
		count++;
	}

	/* smp_wmb(); */
	bcm_workq_cons_sync(workq);
	DHD_LB_STATS_UPDATE_TXC_HISTO(dhd, count);
}
#endif /* DHD_LB_TXC */

#if defined(DHD_LB_RXC)

/**
 * dhd_lb_dispatch_rx_compl - load balance by dispatch rx complition work
 * to other CPU cores
 */
static INLINE void
dhd_lb_dispatch_rx_compl(dhd_pub_t *dhdp)
{
	dhd_prot_t *prot = dhdp->prot;
	/* Schedule the takslet only if we have to */
	if (prot->rxbufpost <= (prot->max_rxbufpost - RXBUFPOST_THRESHOLD)) {
		/* flush WR index */
		bcm_workq_prod_sync(&dhdp->prot->rx_compl_prod);
		dhd_lb_rx_compl_dispatch(dhdp); /* dispatch rx_compl_tasklet */
	}
}

void
dhd_lb_rx_compl_handler(unsigned long data)
{
	dhd_pub_t *dhd = (dhd_pub_t *)data;
	bcm_workq_t *workq = &dhd->prot->rx_compl_cons;

	DHD_LB_STATS_RXC_PERCPU_CNT_INCR(dhd);

	dhd_msgbuf_rxbuf_post(dhd, TRUE); /* re-use pktids */
	bcm_workq_cons_sync(workq);
}
#endif /* DHD_LB_RXC */
#endif /* DHD_LB */

void
dhd_prot_rx_dataoffset(dhd_pub_t *dhd, uint32 rx_offset)
{
	dhd_prot_t *prot = dhd->prot;
	prot->rx_dataoffset = rx_offset;
}

static int
dhd_check_create_info_rings(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	int ret = BCME_ERROR;
	uint16 ringid = dhd->bus->max_tx_flowrings + BCMPCIE_COMMON_MSGRINGS;

	if (prot->h2dring_info_subn && prot->d2hring_info_cpln) {
		return BCME_OK; /* dhd_prot_init rentry after a dhd_prot_reset */
	}

	if (prot->h2dring_info_subn == NULL) {
		prot->h2dring_info_subn = MALLOCZ(prot->osh, sizeof(msgbuf_ring_t));

		if (prot->h2dring_info_subn == NULL) {
			DHD_ERROR(("%s: couldn't alloc memory for h2dring_info_subn\n",
				__FUNCTION__));
			return BCME_NOMEM;
		}

		DHD_INFO(("%s: about to create debug submit ring\n", __FUNCTION__));
		ret = dhd_prot_ring_attach(dhd, prot->h2dring_info_subn, "h2dinfo",
			H2DRING_DYNAMIC_INFO_MAX_ITEM, H2DRING_INFO_BUFPOST_ITEMSIZE,
			ringid);
		if (ret != BCME_OK) {
			DHD_ERROR(("%s: couldn't alloc resources for dbg submit ring\n",
				__FUNCTION__));
			goto err;
		}
	}

	if (prot->d2hring_info_cpln == NULL) {
		prot->d2hring_info_cpln = MALLOCZ(prot->osh, sizeof(msgbuf_ring_t));

		if (prot->d2hring_info_cpln == NULL) {
			DHD_ERROR(("%s: couldn't alloc memory for h2dring_info_subn\n",
				__FUNCTION__));
			return BCME_NOMEM;
		}

		/* create the debug info completion ring next to debug info submit ring
		* ringid = id next to debug info submit ring
		*/
		ringid = ringid + 1;

		DHD_INFO(("%s: about to create debug cpl ring\n", __FUNCTION__));
		ret = dhd_prot_ring_attach(dhd, prot->d2hring_info_cpln, "d2hinfo",
			D2HRING_DYNAMIC_INFO_MAX_ITEM, D2HRING_INFO_BUFCMPLT_ITEMSIZE,
			ringid);
		if (ret != BCME_OK) {
			DHD_ERROR(("%s: couldn't alloc resources for dbg cpl ring\n",
				__FUNCTION__));
			dhd_prot_ring_detach(dhd, prot->h2dring_info_subn);
			goto err;
		}
	}

	return ret;
err:
	MFREE(prot->osh, prot->h2dring_info_subn, sizeof(msgbuf_ring_t));
	prot->h2dring_info_subn = NULL;

	if (prot->d2hring_info_cpln) {
		MFREE(prot->osh, prot->d2hring_info_cpln, sizeof(msgbuf_ring_t));
		prot->d2hring_info_cpln = NULL;
	}
	return ret;
} /* dhd_check_create_info_rings */

int
dhd_prot_init_info_rings(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	int ret = BCME_OK;

	if ((ret = dhd_check_create_info_rings(dhd)) != BCME_OK) {
		DHD_ERROR(("%s: info rings aren't created! \n",
			__FUNCTION__));
		return ret;
	}

	if ((prot->d2hring_info_cpln->inited) || (prot->d2hring_info_cpln->create_pending)) {
		DHD_INFO(("Info completion ring was created!\n"));
		return ret;
	}

	DHD_TRACE(("trying to send create d2h info ring: id %d\n", prot->d2hring_info_cpln->idx));
	ret = dhd_send_d2h_ringcreate(dhd, prot->d2hring_info_cpln);
	if (ret != BCME_OK)
		return ret;

	prot->d2hring_info_cpln->seqnum = D2H_EPOCH_INIT_VAL;

	DHD_TRACE(("trying to send create h2d info ring id %d\n", prot->h2dring_info_subn->idx));
	prot->h2dring_info_subn->n_completion_ids = 1;
	prot->h2dring_info_subn->compeltion_ring_ids[0] = prot->d2hring_info_cpln->idx;

	ret = dhd_send_h2d_ringcreate(dhd, prot->h2dring_info_subn);

	/* Note that there is no way to delete d2h or h2d ring deletion incase either fails,
	 * so can not cleanup if one ring was created while the other failed
	 */
	return ret;
} /* dhd_prot_init_info_rings */

static void
dhd_prot_detach_info_rings(dhd_pub_t *dhd)
{
	if (dhd->prot->h2dring_info_subn) {
		dhd_prot_ring_detach(dhd, dhd->prot->h2dring_info_subn);
		MFREE(dhd->prot->osh, dhd->prot->h2dring_info_subn, sizeof(msgbuf_ring_t));
		dhd->prot->h2dring_info_subn = NULL;
	}
	if (dhd->prot->d2hring_info_cpln) {
		dhd_prot_ring_detach(dhd, dhd->prot->d2hring_info_cpln);
		MFREE(dhd->prot->osh, dhd->prot->d2hring_info_cpln, sizeof(msgbuf_ring_t));
		dhd->prot->d2hring_info_cpln = NULL;
	}
}

/**
 * Initialize protocol: sync w/dongle state.
 * Sets dongle media info (iswl, drv_version, mac address).
 */
int dhd_sync_with_dongle(dhd_pub_t *dhd)
{
	int ret = 0;
	wlc_rev_info_t revinfo;


	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	dhd_os_set_ioctl_resp_timeout(IOCTL_RESP_TIMEOUT);

	/* Post ts buffer after shim layer is attached */
	ret = dhd_msgbuf_rxbuf_post_ts_bufs(dhd);


#ifdef DHD_FW_COREDUMP
	/* Check the memdump capability */
	dhd_get_memdump_info(dhd);
#endif /* DHD_FW_COREDUMP */
#ifdef BCMASSERT_LOG
	dhd_get_assert_info(dhd);
#endif /* BCMASSERT_LOG */

	/* Get the device rev info */
	memset(&revinfo, 0, sizeof(revinfo));
	ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_REVINFO, &revinfo, sizeof(revinfo), FALSE, 0);
	if (ret < 0) {
		DHD_ERROR(("%s: GET revinfo FAILED\n", __FUNCTION__));
		goto done;
	}
	DHD_ERROR(("%s: GET_REVINFO device 0x%x, vendor 0x%x, chipnum 0x%x\n", __FUNCTION__,
		revinfo.deviceid, revinfo.vendorid, revinfo.chipnum));

	DHD_SSSR_DUMP_INIT(dhd);

	dhd_process_cid_mac(dhd, TRUE);
	ret = dhd_preinit_ioctls(dhd);
	dhd_process_cid_mac(dhd, FALSE);

	/* Always assumes wl for now */
	dhd->iswl = TRUE;
done:
	return ret;
} /* dhd_sync_with_dongle */


#define DHD_DBG_SHOW_METADATA	0

#if DHD_DBG_SHOW_METADATA
static void BCMFASTPATH
dhd_prot_print_metadata(dhd_pub_t *dhd, void *ptr, int len)
{
	uint8 tlv_t;
	uint8 tlv_l;
	uint8 *tlv_v = (uint8 *)ptr;

	if (len <= BCMPCIE_D2H_METADATA_HDRLEN)
		return;

	len -= BCMPCIE_D2H_METADATA_HDRLEN;
	tlv_v += BCMPCIE_D2H_METADATA_HDRLEN;

	while (len > TLV_HDR_LEN) {
		tlv_t = tlv_v[TLV_TAG_OFF];
		tlv_l = tlv_v[TLV_LEN_OFF];

		len -= TLV_HDR_LEN;
		tlv_v += TLV_HDR_LEN;
		if (len < tlv_l)
			break;
		if ((tlv_t == 0) || (tlv_t == WLFC_CTL_TYPE_FILLER))
			break;

		switch (tlv_t) {
		case WLFC_CTL_TYPE_TXSTATUS: {
			uint32 txs;
			memcpy(&txs, tlv_v, sizeof(uint32));
			if (tlv_l < (sizeof(wl_txstatus_additional_info_t) + sizeof(uint32))) {
				printf("METADATA TX_STATUS: %08x\n", txs);
			} else {
				wl_txstatus_additional_info_t tx_add_info;
				memcpy(&tx_add_info, tlv_v + sizeof(uint32),
					sizeof(wl_txstatus_additional_info_t));
				printf("METADATA TX_STATUS: %08x WLFCTS[%04x | %08x - %08x - %08x]"
					" rate = %08x tries = %d - %d\n", txs,
					tx_add_info.seq, tx_add_info.entry_ts,
					tx_add_info.enq_ts, tx_add_info.last_ts,
					tx_add_info.rspec, tx_add_info.rts_cnt,
					tx_add_info.tx_cnt);
			}
			} break;

		case WLFC_CTL_TYPE_RSSI: {
			if (tlv_l == 1)
				printf("METADATA RX_RSSI: rssi = %d\n", *tlv_v);
			else
				printf("METADATA RX_RSSI[%04x]: rssi = %d snr = %d\n",
					(*(tlv_v + 3) << 8) | *(tlv_v + 2),
					(int8)(*tlv_v), *(tlv_v + 1));
			} break;

		case WLFC_CTL_TYPE_FIFO_CREDITBACK:
			bcm_print_bytes("METADATA FIFO_CREDITBACK", tlv_v, tlv_l);
			break;

		case WLFC_CTL_TYPE_TX_ENTRY_STAMP:
			bcm_print_bytes("METADATA TX_ENTRY", tlv_v, tlv_l);
			break;

		case WLFC_CTL_TYPE_RX_STAMP: {
			struct {
				uint32 rspec;
				uint32 bus_time;
				uint32 wlan_time;
			} rx_tmstamp;
			memcpy(&rx_tmstamp, tlv_v, sizeof(rx_tmstamp));
			printf("METADATA RX TIMESTMAP: WLFCTS[%08x - %08x] rate = %08x\n",
				rx_tmstamp.wlan_time, rx_tmstamp.bus_time, rx_tmstamp.rspec);
			} break;

		case WLFC_CTL_TYPE_TRANS_ID:
			bcm_print_bytes("METADATA TRANS_ID", tlv_v, tlv_l);
			break;

		case WLFC_CTL_TYPE_COMP_TXSTATUS:
			bcm_print_bytes("METADATA COMP_TXSTATUS", tlv_v, tlv_l);
			break;

		default:
			bcm_print_bytes("METADATA UNKNOWN", tlv_v, tlv_l);
			break;
		}

		len -= tlv_l;
		tlv_v += tlv_l;
	}
}
#endif /* DHD_DBG_SHOW_METADATA */

static INLINE void BCMFASTPATH
dhd_prot_packet_free(dhd_pub_t *dhd, void *pkt, uint8 pkttype, bool send)
{
	if (pkt) {
		if (pkttype == PKTTYPE_IOCTL_RX ||
			pkttype == PKTTYPE_EVENT_RX ||
			pkttype == PKTTYPE_INFO_RX ||
			pkttype == PKTTYPE_TSBUF_RX) {
#ifdef DHD_USE_STATIC_CTRLBUF
			PKTFREE_STATIC(dhd->osh, pkt, send);
#else
			PKTFREE(dhd->osh, pkt, send);
#endif /* DHD_USE_STATIC_CTRLBUF */
		} else {
			PKTFREE(dhd->osh, pkt, send);
		}
	}
}

/* dhd_prot_packet_get should be called only for items having pktid_ctrl_map handle */
static INLINE void * BCMFASTPATH
dhd_prot_packet_get(dhd_pub_t *dhd, uint32 pktid, uint8 pkttype, bool free_pktid)
{
	void *PKTBUF;
	dmaaddr_t pa;
	uint32 len;
	void *dmah;
	void *secdma;

#ifdef DHD_PCIE_PKTID
	if (free_pktid) {
		PKTBUF = DHD_PKTID_TO_NATIVE(dhd, dhd->prot->pktid_ctrl_map,
			pktid, pa, len, dmah, secdma, pkttype);
	} else {
		PKTBUF = DHD_PKTID_TO_NATIVE_RSV(dhd, dhd->prot->pktid_ctrl_map,
			pktid, pa, len, dmah, secdma, pkttype);
	}
#else
	PKTBUF = DHD_PKTID_TO_NATIVE(dhd, dhd->prot->pktid_ctrl_map, pktid, pa,
		len, dmah, secdma, pkttype);
#endif /* DHD_PCIE_PKTID */
	if (PKTBUF) {
		{
			if (SECURE_DMA_ENAB(dhd->osh))
				SECURE_DMA_UNMAP(dhd->osh, pa, (uint) len, DMA_RX, 0, dmah,
					secdma, 0);
			else
				DMA_UNMAP(dhd->osh, pa, (uint) len, DMA_RX, 0, dmah);
#ifdef DMAMAP_STATS
			switch (pkttype) {
#ifndef IOCTLRESP_USE_CONSTMEM
				case PKTTYPE_IOCTL_RX:
					dhd->dma_stats.ioctl_rx--;
					dhd->dma_stats.ioctl_rx_sz -= len;
					break;
#endif /* IOCTLRESP_USE_CONSTMEM */
				case PKTTYPE_EVENT_RX:
					dhd->dma_stats.event_rx--;
					dhd->dma_stats.event_rx_sz -= len;
					break;
				case PKTTYPE_INFO_RX:
					dhd->dma_stats.info_rx--;
					dhd->dma_stats.info_rx_sz -= len;
					break;
				case PKTTYPE_TSBUF_RX:
					dhd->dma_stats.tsbuf_rx--;
					dhd->dma_stats.tsbuf_rx_sz -= len;
					break;
			}
#endif /* DMAMAP_STATS */
		}
	}

	return PKTBUF;
}

#ifdef IOCTLRESP_USE_CONSTMEM
static INLINE void BCMFASTPATH
dhd_prot_ioctl_ret_buffer_get(dhd_pub_t *dhd, uint32 pktid, dhd_dma_buf_t *retbuf)
{
	memset(retbuf, 0, sizeof(dhd_dma_buf_t));
	retbuf->va = DHD_PKTID_TO_NATIVE(dhd, dhd->prot->pktid_map_handle_ioctl, pktid,
		retbuf->pa, retbuf->len, retbuf->dmah, retbuf->secdma, PKTTYPE_IOCTL_RX);

	return;
}
#endif

#ifdef PCIE_INB_DW
static int
dhd_prot_inc_hostactive_devwake_assert(dhd_bus_t *bus)
{
	unsigned long flags = 0;

	if (INBAND_DW_ENAB(bus)) {
		DHD_BUS_INB_DW_LOCK(bus->inb_lock, flags);
		bus->host_active_cnt++;
		DHD_BUS_INB_DW_UNLOCK(bus->inb_lock, flags);
		if (dhd_bus_set_device_wake(bus, TRUE) != BCME_OK) {
			DHD_BUS_INB_DW_LOCK(bus->inb_lock, flags);
			bus->host_active_cnt--;
			dhd_bus_inb_ack_pending_ds_req(bus);
			DHD_BUS_INB_DW_UNLOCK(bus->inb_lock, flags);
			return BCME_ERROR;
		}
	}

	return BCME_OK;
}

static void
dhd_prot_dec_hostactive_ack_pending_dsreq(dhd_bus_t *bus)
{
	unsigned long flags = 0;
	if (INBAND_DW_ENAB(bus)) {
		DHD_BUS_INB_DW_LOCK(bus->inb_lock, flags);
		bus->host_active_cnt--;
		dhd_bus_inb_ack_pending_ds_req(bus);
		DHD_BUS_INB_DW_UNLOCK(bus->inb_lock, flags);
	}
}
#endif /* PCIE_INB_DW */

static void BCMFASTPATH
dhd_msgbuf_rxbuf_post(dhd_pub_t *dhd, bool use_rsv_pktid)
{
	dhd_prot_t *prot = dhd->prot;
	int16 fillbufs;
	uint16 cnt = 256;
	int retcount = 0;

	fillbufs = prot->max_rxbufpost - prot->rxbufpost;
	while (fillbufs >= RX_BUF_BURST) {
		cnt--;
		if (cnt == 0) {
			/* find a better way to reschedule rx buf post if space not available */
			DHD_ERROR(("h2d rx post ring not available to post host buffers \n"));
			DHD_ERROR(("Current posted host buf count %d \n", prot->rxbufpost));
			break;
		}

		/* Post in a burst of 32 buffers at a time */
		fillbufs = MIN(fillbufs, RX_BUF_BURST);

		/* Post buffers */
		retcount = dhd_prot_rxbuf_post(dhd, fillbufs, use_rsv_pktid);

		if (retcount >= 0) {
			prot->rxbufpost += (uint16)retcount;
#ifdef DHD_LB_RXC
			/* dhd_prot_rxbuf_post returns the number of buffers posted */
			DHD_LB_STATS_UPDATE_RXC_HISTO(dhd, retcount);
#endif /* DHD_LB_RXC */
			/* how many more to post */
			fillbufs = prot->max_rxbufpost - prot->rxbufpost;
		} else {
			/* Make sure we don't run loop any further */
			fillbufs = 0;
		}
	}
}

/** Post 'count' no of rx buffers to dongle */
static int BCMFASTPATH
dhd_prot_rxbuf_post(dhd_pub_t *dhd, uint16 count, bool use_rsv_pktid)
{
	void *p, **pktbuf;
	uint16 pktsz = DHD_FLOWRING_RX_BUFPOST_PKTSZ;
	uint8 *rxbuf_post_tmp;
	host_rxbuf_post_t *rxbuf_post;
	void *msg_start;
	dmaaddr_t pa, *pktbuf_pa;
	uint32 *pktlen;
	uint16 i = 0, alloced = 0;
	unsigned long flags;
	uint32 pktid;
	dhd_prot_t *prot = dhd->prot;
	msgbuf_ring_t *ring = &prot->h2dring_rxp_subn;
	void *lcl_buf;
	uint16 lcl_buf_size;

#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */

	/* allocate a local buffer to store pkt buffer va, pa and length */
	lcl_buf_size = (sizeof(void *) + sizeof(dmaaddr_t) + sizeof(uint32)) *
		RX_BUF_BURST;
	lcl_buf = MALLOC(dhd->osh, lcl_buf_size);
	if (!lcl_buf) {
		DHD_ERROR(("%s: local scratch buffer allocation failed\n", __FUNCTION__));
#ifdef PCIE_INB_DW
		dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
		return 0;
	}
	pktbuf = lcl_buf;
	pktbuf_pa = (dmaaddr_t *)((uint8 *)pktbuf + sizeof(void *) * RX_BUF_BURST);
	pktlen = (uint32 *)((uint8 *)pktbuf_pa + sizeof(dmaaddr_t) * RX_BUF_BURST);

	for (i = 0; i < count; i++) {
		if ((p = PKTGET(dhd->osh, pktsz, FALSE)) == NULL) {
			DHD_ERROR(("%s:%d: PKTGET for rxbuf failed\n", __FUNCTION__, __LINE__));
			dhd->rx_pktgetfail++;
			break;
		}

		pktlen[i] = PKTLEN(dhd->osh, p);
		if (SECURE_DMA_ENAB(dhd->osh)) {
			pa = SECURE_DMA_MAP(dhd->osh, PKTDATA(dhd->osh, p), pktlen[i],
				DMA_RX, p, 0, ring->dma_buf.secdma, 0);
		}
#ifndef BCM_SECURE_DMA
		else
			pa = DMA_MAP(dhd->osh, PKTDATA(dhd->osh, p), pktlen[i], DMA_RX, p, 0);
#endif /* #ifndef BCM_SECURE_DMA */

		if (PHYSADDRISZERO(pa)) {
			PKTFREE(dhd->osh, p, FALSE);
			DHD_ERROR(("Invalid phyaddr 0\n"));
			ASSERT(0);
			break;
		}
#ifdef DMAMAP_STATS
		dhd->dma_stats.rxdata++;
		dhd->dma_stats.rxdata_sz += pktlen[i];
#endif /* DMAMAP_STATS */

		PKTPULL(dhd->osh, p, prot->rx_metadata_offset);
		pktlen[i] = PKTLEN(dhd->osh, p);
		pktbuf[i] = p;
		pktbuf_pa[i] = pa;
	}

	/* only post what we have */
	count = i;

	/* grab the rx lock to allocate pktid and post on ring */
	DHD_SPIN_LOCK(prot->rx_lock, flags);

	/* Claim space for exactly 'count' no of messages, for mitigation purpose */
	msg_start = (void *)
		dhd_prot_alloc_ring_space(dhd, ring, count, &alloced, TRUE);
	if (msg_start == NULL) {
		DHD_INFO(("%s:%d: Rxbufpost Msgbuf Not available\n", __FUNCTION__, __LINE__));
		goto cleanup;
	}
	/* if msg_start !=  NULL, we should have alloced space for atleast 1 item */
	ASSERT(alloced > 0);

	rxbuf_post_tmp = (uint8*)msg_start;

	for (i = 0; i < alloced; i++) {
		rxbuf_post = (host_rxbuf_post_t *)rxbuf_post_tmp;
		p = pktbuf[i];
		pa = pktbuf_pa[i];

#if defined(DHD_LB_RXC)
		if (use_rsv_pktid == TRUE) {
			bcm_workq_t *workq = &prot->rx_compl_cons;
			int elem_ix = bcm_ring_cons(WORKQ_RING(workq), DHD_LB_WORKQ_SZ);

			if (elem_ix == BCM_RING_EMPTY) {
				DHD_INFO(("%s rx_compl_cons ring is empty\n", __FUNCTION__));
				pktid = DHD_PKTID_INVALID;
				goto alloc_pkt_id;
			} else {
				uint32 *elem = WORKQ_ELEMENT(uint32, workq, elem_ix);
				pktid = *elem;
			}

			rxbuf_post->cmn_hdr.request_id = htol32(pktid);

			/* Now populate the previous locker with valid information */
			if (pktid != DHD_PKTID_INVALID) {
				DHD_NATIVE_TO_PKTID_SAVE(dhd, dhd->prot->pktid_rx_map,
					p, pktid, pa, pktlen[i], DMA_RX, NULL, NULL,
					PKTTYPE_DATA_RX);
			}
		} else
#endif /* ! DHD_LB_RXC */
		{
#if defined(DHD_LB_RXC)
alloc_pkt_id:
#endif /* DHD_LB_RXC */
		pktid = DHD_NATIVE_TO_PKTID(dhd, dhd->prot->pktid_rx_map, p, pa,
			pktlen[i], DMA_RX, NULL, ring->dma_buf.secdma, PKTTYPE_DATA_RX);
#if defined(DHD_PCIE_PKTID)
		if (pktid == DHD_PKTID_INVALID) {
			break;
		}
#endif /* DHD_PCIE_PKTID */
		}

		/* Common msg header */
		rxbuf_post->cmn_hdr.msg_type = MSG_TYPE_RXBUF_POST;
		rxbuf_post->cmn_hdr.if_id = 0;
		rxbuf_post->cmn_hdr.epoch = ring->seqnum % H2D_EPOCH_MODULO;
		rxbuf_post->cmn_hdr.flags = ring->current_phase;
		ring->seqnum++;
		rxbuf_post->data_buf_len = htol16((uint16)pktlen[i]);
		rxbuf_post->data_buf_addr.high_addr = htol32(PHYSADDRHI(pa));
		rxbuf_post->data_buf_addr.low_addr =
			htol32(PHYSADDRLO(pa) + prot->rx_metadata_offset);

		if (prot->rx_metadata_offset) {
			rxbuf_post->metadata_buf_len = prot->rx_metadata_offset;
			rxbuf_post->metadata_buf_addr.high_addr = htol32(PHYSADDRHI(pa));
			rxbuf_post->metadata_buf_addr.low_addr  = htol32(PHYSADDRLO(pa));
		} else {
			rxbuf_post->metadata_buf_len = 0;
			rxbuf_post->metadata_buf_addr.high_addr = 0;
			rxbuf_post->metadata_buf_addr.low_addr  = 0;
		}

#ifdef DHD_PKTID_AUDIT_RING
		DHD_PKTID_AUDIT(dhd, prot->pktid_rx_map, pktid, DHD_DUPLICATE_ALLOC);
#endif /* DHD_PKTID_AUDIT_RING */

		rxbuf_post->cmn_hdr.request_id = htol32(pktid);

		/* Move rxbuf_post_tmp to next item */
		rxbuf_post_tmp = rxbuf_post_tmp + ring->item_len;
	}

	if (i < alloced) {
		if (ring->wr < (alloced - i))
			ring->wr = ring->max_items - (alloced - i);
		else
			ring->wr -= (alloced - i);

		if (ring->wr == 0) {
			DHD_INFO(("%s: flipping the phase now\n", ring->name));
				ring->current_phase = ring->current_phase ?
				0 : BCMPCIE_CMNHDR_PHASE_BIT_INIT;
		}

		alloced = i;
	}

	/* update ring's WR index and ring doorbell to dongle */
	if (alloced > 0) {
		unsigned long flags1;
		DHD_GENERAL_LOCK(dhd, flags1);
		dhd_prot_ring_write_complete(dhd, ring, msg_start, alloced);
		DHD_GENERAL_UNLOCK(dhd, flags1);
	}

	DHD_SPIN_UNLOCK(prot->rx_lock, flags);

cleanup:
	for (i = alloced; i < count; i++) {
		p = pktbuf[i];
		pa = pktbuf_pa[i];

		if (SECURE_DMA_ENAB(dhd->osh))
			SECURE_DMA_UNMAP(dhd->osh, pa, pktlen[i], DMA_RX, 0,
				DHD_DMAH_NULL, ring->dma_buf.secdma, 0);
		else
			DMA_UNMAP(dhd->osh, pa, pktlen[i], DMA_RX, 0, DHD_DMAH_NULL);
		PKTFREE(dhd->osh, p, FALSE);
	}

	MFREE(dhd->osh, lcl_buf, lcl_buf_size);
#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
	return alloced;
} /* dhd_prot_rxbufpost */

static int
dhd_prot_infobufpost(dhd_pub_t *dhd)
{
	unsigned long flags;
	uint32 pktid;
	dhd_prot_t *prot = dhd->prot;
	msgbuf_ring_t *ring = prot->h2dring_info_subn;
	uint16 alloced = 0;
	uint16 pktsz = DHD_FLOWRING_RX_BUFPOST_PKTSZ;
	uint32 pktlen;
	info_buf_post_msg_t *infobuf_post;
	uint8 *infobuf_post_tmp;
	void *p;
	void* msg_start;
	uint8 i = 0;
	dmaaddr_t pa;
	int16 count;

	if (ring == NULL)
		return 0;

	if (ring->inited != TRUE)
		return 0;
	if (prot->max_infobufpost == 0)
		return 0;

	count = prot->max_infobufpost - prot->infobufpost;

	if (count <= 0) {
		DHD_INFO(("%s: Cannot post more than max info resp buffers\n",
			__FUNCTION__));
		return 0;
	}

#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */

	DHD_GENERAL_LOCK(dhd, flags);
	/* Claim space for exactly 'count' no of messages, for mitigation purpose */
	msg_start = (void *) dhd_prot_alloc_ring_space(dhd, ring, count, &alloced, FALSE);
	DHD_GENERAL_UNLOCK(dhd, flags);

	if (msg_start == NULL) {
		DHD_INFO(("%s:%d: infobufpost Msgbuf Not available\n", __FUNCTION__, __LINE__));
#ifdef PCIE_INB_DW
		dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
		return -1;
	}

	/* if msg_start !=  NULL, we should have alloced space for atleast 1 item */
	ASSERT(alloced > 0);

	infobuf_post_tmp = (uint8*) msg_start;

	/* loop through each allocated message in the host ring */
	for (i = 0; i < alloced; i++) {
		infobuf_post = (info_buf_post_msg_t *) infobuf_post_tmp;
		/* Create a rx buffer */
#ifdef DHD_USE_STATIC_CTRLBUF
		p = PKTGET_STATIC(dhd->osh, pktsz, FALSE);
#else
		p = PKTGET(dhd->osh, pktsz, FALSE);
#endif /* DHD_USE_STATIC_CTRLBUF */
		if (p == NULL) {
			DHD_ERROR(("%s:%d: PKTGET for infobuf failed\n", __FUNCTION__, __LINE__));
			dhd->rx_pktgetfail++;
			break;
		}
		pktlen = PKTLEN(dhd->osh, p);
		if (SECURE_DMA_ENAB(dhd->osh)) {
			pa = SECURE_DMA_MAP(dhd->osh, PKTDATA(dhd->osh, p), pktlen,
				DMA_RX, p, 0, ring->dma_buf.secdma, 0);
		}
#ifndef BCM_SECURE_DMA
		else
			pa = DMA_MAP(dhd->osh, PKTDATA(dhd->osh, p), pktlen, DMA_RX, p, 0);
#endif /* #ifndef BCM_SECURE_DMA */
		if (PHYSADDRISZERO(pa)) {
			if (SECURE_DMA_ENAB(dhd->osh)) {
				SECURE_DMA_UNMAP(dhd->osh, pa, pktlen, DMA_RX, 0, DHD_DMAH_NULL,
					ring->dma_buf.secdma, 0);
			}
			else
				DMA_UNMAP(dhd->osh, pa, pktlen, DMA_RX, 0, DHD_DMAH_NULL);
#ifdef DHD_USE_STATIC_CTRLBUF
			PKTFREE_STATIC(dhd->osh, p, FALSE);
#else
			PKTFREE(dhd->osh, p, FALSE);
#endif /* DHD_USE_STATIC_CTRLBUF */
			DHD_ERROR(("Invalid phyaddr 0\n"));
			ASSERT(0);
			break;
		}
#ifdef DMAMAP_STATS
		dhd->dma_stats.info_rx++;
		dhd->dma_stats.info_rx_sz += pktlen;
#endif /* DMAMAP_STATS */
		pktlen = PKTLEN(dhd->osh, p);

		/* Common msg header */
		infobuf_post->cmn_hdr.msg_type = MSG_TYPE_INFO_BUF_POST;
		infobuf_post->cmn_hdr.if_id = 0;
		infobuf_post->cmn_hdr.epoch = ring->seqnum % H2D_EPOCH_MODULO;
		infobuf_post->cmn_hdr.flags = ring->current_phase;
		ring->seqnum++;

#if defined(DHD_PCIE_PKTID)
		/* get the lock before calling DHD_NATIVE_TO_PKTID */
		DHD_GENERAL_LOCK(dhd, flags);
#endif /* DHD_PCIE_PKTID */

		pktid = DHD_NATIVE_TO_PKTID(dhd, dhd->prot->pktid_ctrl_map, p, pa,
			pktlen, DMA_RX, NULL, ring->dma_buf.secdma, PKTTYPE_INFO_RX);


#if defined(DHD_PCIE_PKTID)
		/* free lock */
		DHD_GENERAL_UNLOCK(dhd, flags);

		if (pktid == DHD_PKTID_INVALID) {
			if (SECURE_DMA_ENAB(dhd->osh)) {
				DHD_GENERAL_LOCK(dhd, flags);
				SECURE_DMA_UNMAP(dhd->osh, pa, pktlen, DMA_RX, 0, 0,
					ring->dma_buf.secdma, 0);
				DHD_GENERAL_UNLOCK(dhd, flags);
			} else
				DMA_UNMAP(dhd->osh, pa, pktlen, DMA_RX, 0, 0);

#ifdef DHD_USE_STATIC_CTRLBUF
			PKTFREE_STATIC(dhd->osh, p, FALSE);
#else
			PKTFREE(dhd->osh, p, FALSE);
#endif /* DHD_USE_STATIC_CTRLBUF */
			DHD_ERROR(("%s: Pktid pool depleted.\n", __FUNCTION__));
			break;
		}
#endif /* DHD_PCIE_PKTID */

		infobuf_post->host_buf_len = htol16((uint16)pktlen);
		infobuf_post->host_buf_addr.high_addr = htol32(PHYSADDRHI(pa));
		infobuf_post->host_buf_addr.low_addr = htol32(PHYSADDRLO(pa));

#ifdef DHD_PKTID_AUDIT_RING
		DHD_PKTID_AUDIT(dhd, prot->pktid_ctrl_map, pktid, DHD_DUPLICATE_ALLOC);
#endif /* DHD_PKTID_AUDIT_RING */

		DHD_INFO(("ID %d, low_addr 0x%08x, high_addr 0x%08x\n",
			infobuf_post->cmn_hdr.request_id,  infobuf_post->host_buf_addr.low_addr,
			infobuf_post->host_buf_addr.high_addr));

		infobuf_post->cmn_hdr.request_id = htol32(pktid);
		/* Move rxbuf_post_tmp to next item */
		infobuf_post_tmp = infobuf_post_tmp + ring->item_len;
	}

	if (i < alloced) {
		if (ring->wr < (alloced - i))
			ring->wr = ring->max_items - (alloced - i);
		else
			ring->wr -= (alloced - i);

		alloced = i;
		if (alloced && ring->wr == 0) {
			DHD_INFO(("%s: flipping the phase now\n", ring->name));
			ring->current_phase = ring->current_phase ?
				0 : BCMPCIE_CMNHDR_PHASE_BIT_INIT;
		}
	}

	/* Update the write pointer in TCM & ring bell */
	if (alloced > 0) {
		prot->infobufpost += alloced;
		DHD_INFO(("allocated %d buffers for info ring\n",  alloced));
		DHD_GENERAL_LOCK(dhd, flags);
		dhd_prot_ring_write_complete(dhd, ring, msg_start, alloced);
		DHD_GENERAL_UNLOCK(dhd, flags);
	}
#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
	return alloced;
} /* dhd_prot_infobufpost */

#ifdef IOCTLRESP_USE_CONSTMEM
static int
alloc_ioctl_return_buffer(dhd_pub_t *dhd, dhd_dma_buf_t *retbuf)
{
	int err;
	memset(retbuf, 0, sizeof(dhd_dma_buf_t));

	if ((err = dhd_dma_buf_alloc(dhd, retbuf, IOCT_RETBUF_SIZE)) != BCME_OK) {
		DHD_ERROR(("%s: dhd_dma_buf_alloc err %d\n", __FUNCTION__, err));
		ASSERT(0);
		return BCME_NOMEM;
	}

	return BCME_OK;
}

static void
free_ioctl_return_buffer(dhd_pub_t *dhd, dhd_dma_buf_t *retbuf)
{
	/* retbuf (declared on stack) not fully populated ...  */
	if (retbuf->va) {
		uint32 dma_pad;
		dma_pad = (IOCT_RETBUF_SIZE % DHD_DMA_PAD) ? DHD_DMA_PAD : 0;
		retbuf->len = IOCT_RETBUF_SIZE;
		retbuf->_alloced = retbuf->len + dma_pad;
	}

	dhd_dma_buf_free(dhd, retbuf);
	return;
}
#endif /* IOCTLRESP_USE_CONSTMEM */

static int
dhd_prot_rxbufpost_ctrl(dhd_pub_t *dhd, uint8 msg_type)
{
	void *p;
	uint16 pktsz;
	ioctl_resp_evt_buf_post_msg_t *rxbuf_post;
	dmaaddr_t pa;
	uint32 pktlen;
	dhd_prot_t *prot = dhd->prot;
	uint16 alloced = 0;
	unsigned long flags;
	dhd_dma_buf_t retbuf;
	void *dmah = NULL;
	uint32 pktid;
	void *map_handle;
	msgbuf_ring_t *ring = &prot->h2dring_ctrl_subn;
	bool non_ioctl_resp_buf = 0;
	dhd_pkttype_t buf_type;

	if (dhd->busstate == DHD_BUS_DOWN) {
		DHD_ERROR(("%s: bus is already down.\n", __FUNCTION__));
		return -1;
	}
	memset(&retbuf, 0, sizeof(dhd_dma_buf_t));

	if (msg_type == MSG_TYPE_IOCTLRESP_BUF_POST)
		buf_type = PKTTYPE_IOCTL_RX;
	else if (msg_type == MSG_TYPE_EVENT_BUF_POST)
		buf_type = PKTTYPE_EVENT_RX;
	else if (msg_type == MSG_TYPE_TIMSTAMP_BUFPOST)
		buf_type = PKTTYPE_TSBUF_RX;
	else {
		DHD_ERROR(("invalid message type to be posted to Ctrl ring %d\n", msg_type));
		return -1;
	}


	if ((msg_type == MSG_TYPE_EVENT_BUF_POST) || (msg_type == MSG_TYPE_TIMSTAMP_BUFPOST))
		non_ioctl_resp_buf = TRUE;
	else
		non_ioctl_resp_buf = FALSE;

	if (non_ioctl_resp_buf) {
		/* Allocate packet for not ioctl resp buffer post */
		pktsz = DHD_FLOWRING_RX_BUFPOST_PKTSZ;
	} else {
		/* Allocate packet for ctrl/ioctl buffer post */
		pktsz = DHD_FLOWRING_IOCTL_BUFPOST_PKTSZ;
	}

#ifdef IOCTLRESP_USE_CONSTMEM
	if (!non_ioctl_resp_buf) {
		if (alloc_ioctl_return_buffer(dhd, &retbuf) != BCME_OK) {
			DHD_ERROR(("Could not allocate IOCTL response buffer\n"));
			return -1;
		}
		ASSERT(retbuf.len == IOCT_RETBUF_SIZE);
		p = retbuf.va;
		pktlen = retbuf.len;
		pa = retbuf.pa;
		dmah = retbuf.dmah;
	} else
#endif /* IOCTLRESP_USE_CONSTMEM */
	{
#ifdef DHD_USE_STATIC_CTRLBUF
		p = PKTGET_STATIC(dhd->osh, pktsz, FALSE);
#else
		p = PKTGET(dhd->osh, pktsz, FALSE);
#endif /* DHD_USE_STATIC_CTRLBUF */
		if (p == NULL) {
			DHD_ERROR(("%s:%d: PKTGET for %s buf failed\n",
				__FUNCTION__, __LINE__, non_ioctl_resp_buf ?
				"EVENT" : "IOCTL RESP"));
			dhd->rx_pktgetfail++;
			return -1;
		}

		pktlen = PKTLEN(dhd->osh, p);

		if (SECURE_DMA_ENAB(dhd->osh)) {
			DHD_GENERAL_LOCK(dhd, flags);
			pa = SECURE_DMA_MAP(dhd->osh, PKTDATA(dhd->osh, p), pktlen,
				DMA_RX, p, 0, ring->dma_buf.secdma, 0);
			DHD_GENERAL_UNLOCK(dhd, flags);
		}
#ifndef BCM_SECURE_DMA
		else
			pa = DMA_MAP(dhd->osh, PKTDATA(dhd->osh, p), pktlen, DMA_RX, p, 0);
#endif /* #ifndef BCM_SECURE_DMA */

		if (PHYSADDRISZERO(pa)) {
			DHD_ERROR(("Invalid physaddr 0\n"));
			ASSERT(0);
			goto free_pkt_return;
		}

#ifdef DMAMAP_STATS
		switch (buf_type) {
#ifndef IOCTLRESP_USE_CONSTMEM
			case PKTTYPE_IOCTL_RX:
				dhd->dma_stats.ioctl_rx++;
				dhd->dma_stats.ioctl_rx_sz += pktlen;
				break;
#endif /* !IOCTLRESP_USE_CONSTMEM */
			case PKTTYPE_EVENT_RX:
				dhd->dma_stats.event_rx++;
				dhd->dma_stats.event_rx_sz += pktlen;
				break;
			case PKTTYPE_TSBUF_RX:
				dhd->dma_stats.tsbuf_rx++;
				dhd->dma_stats.tsbuf_rx_sz += pktlen;
				break;
			default:
				break;
		}
#endif /* DMAMAP_STATS */

	}
#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */

	DHD_GENERAL_LOCK(dhd, flags);

	rxbuf_post = (ioctl_resp_evt_buf_post_msg_t *)
		dhd_prot_alloc_ring_space(dhd, ring, 1, &alloced, FALSE);

	if (rxbuf_post == NULL) {
		DHD_GENERAL_UNLOCK(dhd, flags);
		DHD_ERROR(("%s:%d: Ctrl submit Msgbuf Not available to post buffer \n",
			__FUNCTION__, __LINE__));

#ifdef IOCTLRESP_USE_CONSTMEM
		if (non_ioctl_resp_buf)
#endif /* IOCTLRESP_USE_CONSTMEM */
		{
			if (SECURE_DMA_ENAB(dhd->osh)) {
				DHD_GENERAL_LOCK(dhd, flags);
				SECURE_DMA_UNMAP(dhd->osh, pa, pktlen, DMA_RX, 0, DHD_DMAH_NULL,
					ring->dma_buf.secdma, 0);
				DHD_GENERAL_UNLOCK(dhd, flags);
			} else {
				DMA_UNMAP(dhd->osh, pa, pktlen, DMA_RX, 0, DHD_DMAH_NULL);
			}
		}
		goto free_pkt_return;
	}

	/* CMN msg header */
	rxbuf_post->cmn_hdr.msg_type = msg_type;

#ifdef IOCTLRESP_USE_CONSTMEM
	if (!non_ioctl_resp_buf) {
		map_handle = dhd->prot->pktid_map_handle_ioctl;
		pktid = DHD_NATIVE_TO_PKTID(dhd, map_handle, p, pa, pktlen, DMA_RX, dmah,
			ring->dma_buf.secdma, buf_type);
	} else
#endif /* IOCTLRESP_USE_CONSTMEM */
	{
		map_handle = dhd->prot->pktid_ctrl_map;
		pktid = DHD_NATIVE_TO_PKTID(dhd, map_handle,
			p, pa, pktlen, DMA_RX, dmah, ring->dma_buf.secdma,
			buf_type);
	}

	if (pktid == DHD_PKTID_INVALID) {
		if (ring->wr == 0) {
			ring->wr = ring->max_items - 1;
		} else {
			ring->wr--;
			if (ring->wr == 0) {
				ring->current_phase = ring->current_phase ? 0 :
					BCMPCIE_CMNHDR_PHASE_BIT_INIT;
			}
		}
		DHD_GENERAL_UNLOCK(dhd, flags);
		DMA_UNMAP(dhd->osh, pa, pktlen, DMA_RX, 0, DHD_DMAH_NULL);
		DHD_ERROR(("%s: Pktid pool depleted.\n", __FUNCTION__));
		goto free_pkt_return;
	}

#ifdef DHD_PKTID_AUDIT_RING
	DHD_PKTID_AUDIT(dhd, map_handle, pktid, DHD_DUPLICATE_ALLOC);
#endif /* DHD_PKTID_AUDIT_RING */

	rxbuf_post->cmn_hdr.request_id = htol32(pktid);
	rxbuf_post->cmn_hdr.if_id = 0;
	rxbuf_post->cmn_hdr.epoch =  ring->seqnum % H2D_EPOCH_MODULO;
	ring->seqnum++;
	rxbuf_post->cmn_hdr.flags = ring->current_phase;

#if defined(DHD_PCIE_PKTID)
	if (rxbuf_post->cmn_hdr.request_id == DHD_PKTID_INVALID) {
		if (ring->wr == 0) {
			ring->wr = ring->max_items - 1;
		} else {
			if (ring->wr == 0) {
				ring->current_phase = ring->current_phase ? 0 :
					BCMPCIE_CMNHDR_PHASE_BIT_INIT;
			}
		}
		DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef IOCTLRESP_USE_CONSTMEM
		if (non_ioctl_resp_buf)
#endif /* IOCTLRESP_USE_CONSTMEM */
		{
			if (SECURE_DMA_ENAB(dhd->osh)) {
				DHD_GENERAL_LOCK(dhd, flags);
				SECURE_DMA_UNMAP(dhd->osh, pa, pktlen, DMA_RX, 0, DHD_DMAH_NULL,
					ring->dma_buf.secdma, 0);
				DHD_GENERAL_UNLOCK(dhd, flags);
			} else
				DMA_UNMAP(dhd->osh, pa, pktlen, DMA_RX, 0, DHD_DMAH_NULL);
		}
		goto free_pkt_return;
	}
#endif /* DHD_PCIE_PKTID */

#ifndef IOCTLRESP_USE_CONSTMEM
	rxbuf_post->host_buf_len = htol16((uint16)PKTLEN(dhd->osh, p));
#else
	rxbuf_post->host_buf_len = htol16((uint16)pktlen);
#endif /* IOCTLRESP_USE_CONSTMEM */
	rxbuf_post->host_buf_addr.high_addr = htol32(PHYSADDRHI(pa));
	rxbuf_post->host_buf_addr.low_addr  = htol32(PHYSADDRLO(pa));

	/* update ring's WR index and ring doorbell to dongle */
	dhd_prot_ring_write_complete(dhd, ring, rxbuf_post, 1);
	DHD_GENERAL_UNLOCK(dhd, flags);

#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif

	return 1;

free_pkt_return:
#ifdef IOCTLRESP_USE_CONSTMEM
	if (!non_ioctl_resp_buf) {
		free_ioctl_return_buffer(dhd, &retbuf);
	} else
#endif
	{
		dhd_prot_packet_free(dhd, p, buf_type, FALSE);
	}

#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif

	return -1;
} /* dhd_prot_rxbufpost_ctrl */

static uint16
dhd_msgbuf_rxbuf_post_ctrlpath(dhd_pub_t *dhd, uint8 msg_type, uint32 max_to_post)
{
	uint32 i = 0;
	int32 ret_val;

	DHD_INFO(("max to post %d, event %d \n", max_to_post, msg_type));

	if (dhd->busstate == DHD_BUS_DOWN) {
		DHD_ERROR(("%s: bus is already down.\n", __FUNCTION__));
		return 0;
	}

	while (i < max_to_post) {
		ret_val  = dhd_prot_rxbufpost_ctrl(dhd, msg_type);
		if (ret_val < 0)
			break;
		i++;
	}
	DHD_INFO(("posted %d buffers of type %d\n", i, msg_type));
	return (uint16)i;
}

static void
dhd_msgbuf_rxbuf_post_ioctlresp_bufs(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	int max_to_post;

	DHD_INFO(("ioctl resp buf post\n"));
	max_to_post = prot->max_ioctlrespbufpost - prot->cur_ioctlresp_bufs_posted;
	if (max_to_post <= 0) {
		DHD_INFO(("%s: Cannot post more than max IOCTL resp buffers\n",
			__FUNCTION__));
		return;
	}
	prot->cur_ioctlresp_bufs_posted += dhd_msgbuf_rxbuf_post_ctrlpath(dhd,
		MSG_TYPE_IOCTLRESP_BUF_POST, max_to_post);
}

static void
dhd_msgbuf_rxbuf_post_event_bufs(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	int max_to_post;

	/* Use atomic variable to avoid re-entry */
	if (atomic_read(&dhd_msgbuf_rxbuf_post_event_bufs_running) > 0) {
		return;
	}
	atomic_inc(&dhd_msgbuf_rxbuf_post_event_bufs_running);

	max_to_post = prot->max_eventbufpost - prot->cur_event_bufs_posted;
	if (max_to_post <= 0) {
		DHD_ERROR(("%s: Cannot post more than max event buffers\n",
			__FUNCTION__));
		return;
	}
	prot->cur_event_bufs_posted += dhd_msgbuf_rxbuf_post_ctrlpath(dhd,
		MSG_TYPE_EVENT_BUF_POST, max_to_post);

	atomic_dec(&dhd_msgbuf_rxbuf_post_event_bufs_running);
}

static int
dhd_msgbuf_rxbuf_post_ts_bufs(dhd_pub_t *dhd)
{
#ifdef DHD_TIMESYNC
	dhd_prot_t *prot = dhd->prot;
	int max_to_post;

	if (prot->active_ipc_version < 7) {
		DHD_ERROR(("no ts buffers to device ipc rev is %d, needs to be atleast 7\n",
			prot->active_ipc_version));
		return 0;
	}

	max_to_post = prot->max_tsbufpost - prot->cur_ts_bufs_posted;
	if (max_to_post <= 0) {
		DHD_INFO(("%s: Cannot post more than max ts buffers\n",
			__FUNCTION__));
		return 0;
	}

	prot->cur_ts_bufs_posted += dhd_msgbuf_rxbuf_post_ctrlpath(dhd,
		MSG_TYPE_TIMSTAMP_BUFPOST, max_to_post);
#endif /* DHD_TIMESYNC */
	return 0;
}

bool BCMFASTPATH
dhd_prot_process_msgbuf_infocpl(dhd_pub_t *dhd, uint bound)
{
	dhd_prot_t *prot = dhd->prot;
	bool more = TRUE;
	uint n = 0;
	msgbuf_ring_t *ring = prot->d2hring_info_cpln;

	if (ring == NULL)
		return FALSE;
	if (ring->inited != TRUE)
		return FALSE;

	/* Process all the messages - DTOH direction */
	while (!dhd_is_device_removed(dhd)) {
		uint8 *msg_addr;
		uint32 msg_len;

		if (dhd->hang_was_sent) {
			more = FALSE;
			break;
		}

		/* Get the message from ring */
		msg_addr = dhd_prot_get_read_addr(dhd, ring, &msg_len);
		if (msg_addr == NULL) {
			more = FALSE;
			break;
		}

		/* Prefetch data to populate the cache */
		OSL_PREFETCH(msg_addr);

		if (dhd_prot_process_msgtype(dhd, ring, msg_addr, msg_len) != BCME_OK) {
			DHD_ERROR(("%s: Error at  process rxpl msgbuf of len %d\n",
				__FUNCTION__, msg_len));
		}

		/* Update read pointer */
		dhd_prot_upd_read_idx(dhd, ring);

		/* After batch processing, check RX bound */
		n += msg_len / ring->item_len;
		if (n >= bound) {
			break;
		}
	}

	return more;
}

/** called when DHD needs to check for 'receive complete' messages from the dongle */
bool BCMFASTPATH
dhd_prot_process_msgbuf_rxcpl(dhd_pub_t *dhd, uint bound)
{
	bool more = FALSE;
	uint n = 0;
	dhd_prot_t *prot = dhd->prot;
	msgbuf_ring_t *ring = &prot->d2hring_rx_cpln;
	uint16 item_len = ring->item_len;
	host_rxbuf_cmpl_t *msg = NULL;
	uint8 *msg_addr;
	uint32 msg_len;
	uint16 pkt_cnt, pkt_cnt_newidx;
	unsigned long flags;
	dmaaddr_t pa;
	uint32 len;
	void *dmah;
	void *secdma;
	int ifidx = 0, if_newidx = 0;
	void *pkt, *pktqhead = NULL, *prevpkt = NULL, *pkt_newidx, *nextpkt;
	uint32 pktid;
	int i;
	uint8 sync;

	while (1) {
		if (dhd_is_device_removed(dhd))
			break;

		if (dhd->hang_was_sent)
			break;

		pkt_cnt = 0;
		pktqhead = pkt_newidx = NULL;
		pkt_cnt_newidx = 0;

		DHD_SPIN_LOCK(prot->rx_lock, flags);

		/* Get the address of the next message to be read from ring */
		msg_addr = dhd_prot_get_read_addr(dhd, ring, &msg_len);
		if (msg_addr == NULL) {
			DHD_SPIN_UNLOCK(prot->rx_lock, flags);
			break;
		}

		while (msg_len > 0) {
			msg = (host_rxbuf_cmpl_t *)msg_addr;

			/* Wait until DMA completes, then fetch msg_type */
			sync = prot->d2h_sync_cb(dhd, ring, &msg->cmn_hdr, item_len);
			/*
			 * Update the curr_rd to the current index in the ring, from where
			 * the work item is fetched. This way if the fetched work item
			 * fails in LIVELOCK, we can print the exact read index in the ring
			 * that shows up the corrupted work item.
			 */
			if ((ring->curr_rd + 1) >= ring->max_items) {
				ring->curr_rd = 0;
			} else {
				ring->curr_rd += 1;
			}

			if (!sync) {
				msg_len -= item_len;
				msg_addr += item_len;
				continue;
			}

			pktid = ltoh32(msg->cmn_hdr.request_id);

#ifdef DHD_PKTID_AUDIT_RING
			DHD_PKTID_AUDIT_RING_DEBUG(dhd, dhd->prot->pktid_rx_map, pktid,
				DHD_DUPLICATE_FREE, msg, D2HRING_RXCMPLT_ITEMSIZE);
#endif /* DHD_PKTID_AUDIT_RING */

			pkt = DHD_PKTID_TO_NATIVE(dhd, prot->pktid_rx_map, pktid, pa,
			        len, dmah, secdma, PKTTYPE_DATA_RX);
			if (!pkt) {
				msg_len -= item_len;
				msg_addr += item_len;
				continue;
			}

			if (SECURE_DMA_ENAB(dhd->osh))
				SECURE_DMA_UNMAP(dhd->osh, pa, (uint) len, DMA_RX, 0,
				    dmah, secdma, 0);
			else
				DMA_UNMAP(dhd->osh, pa, (uint) len, DMA_RX, 0, dmah);

#ifdef DMAMAP_STATS
			dhd->dma_stats.rxdata--;
			dhd->dma_stats.rxdata_sz -= len;
#endif /* DMAMAP_STATS */
			DHD_INFO(("id 0x%04x, offset %d, len %d, idx %d, phase 0x%02x, "
				"pktdata %p, metalen %d\n",
				ltoh32(msg->cmn_hdr.request_id),
				ltoh16(msg->data_offset),
				ltoh16(msg->data_len), msg->cmn_hdr.if_id,
				msg->cmn_hdr.flags, PKTDATA(dhd->osh, pkt),
				ltoh16(msg->metadata_len)));

			pkt_cnt++;
			msg_len -= item_len;
			msg_addr += item_len;

#if DHD_DBG_SHOW_METADATA
			if (prot->metadata_dbg && prot->rx_metadata_offset &&
			        msg->metadata_len) {
				uchar *ptr;
				ptr = PKTDATA(dhd->osh, pkt) - (prot->rx_metadata_offset);
				/* header followed by data */
				bcm_print_bytes("rxmetadata", ptr, msg->metadata_len);
				dhd_prot_print_metadata(dhd, ptr, msg->metadata_len);
			}
#endif /* DHD_DBG_SHOW_METADATA */

			/* data_offset from buf start */
			if (ltoh16(msg->data_offset)) {
				/* data offset given from dongle after split rx */
				PKTPULL(dhd->osh, pkt, ltoh16(msg->data_offset));
			}
			else if (prot->rx_dataoffset) {
				/* DMA RX offset updated through shared area */
				PKTPULL(dhd->osh, pkt, prot->rx_dataoffset);
			}
			/* Actual length of the packet */
			PKTSETLEN(dhd->osh, pkt, ltoh16(msg->data_len));
#if defined(WL_MONITOR)
			if (dhd_monitor_enabled(dhd, ifidx) &&
				(msg->flags & BCMPCIE_PKT_FLAGS_FRAME_802_11)) {
				dhd_rx_mon_pkt(dhd, msg, pkt, ifidx);
				continue;
			}
#endif 

			if (!pktqhead) {
				pktqhead = prevpkt = pkt;
				ifidx = msg->cmn_hdr.if_id;
			} else {
				if (ifidx != msg->cmn_hdr.if_id) {
					pkt_newidx = pkt;
					if_newidx = msg->cmn_hdr.if_id;
					pkt_cnt--;
					pkt_cnt_newidx = 1;
					break;
				} else {
					PKTSETNEXT(dhd->osh, prevpkt, pkt);
					prevpkt = pkt;
				}
			}

#ifdef DHD_TIMESYNC
			if (dhd->prot->rx_ts_log_enabled) {
				ts_timestamp_t *ts = (ts_timestamp_t *)&msg->ts;
				dhd_timesync_log_rx_timestamp(dhd->ts, ifidx, ts->low, ts->high);
			}
#endif /* DHD_TIMESYNC */
		}

		/* roll back read pointer for unprocessed message */
		if (msg_len > 0) {
			if (ring->rd < msg_len / item_len)
				ring->rd = ring->max_items - msg_len / item_len;
			else
				ring->rd -= msg_len / item_len;
		}

		/* Update read pointer */
		dhd_prot_upd_read_idx(dhd, ring);

		DHD_SPIN_UNLOCK(prot->rx_lock, flags);

		pkt = pktqhead;
		for (i = 0; pkt && i < pkt_cnt; i++, pkt = nextpkt) {
			nextpkt = PKTNEXT(dhd->osh, pkt);
			PKTSETNEXT(dhd->osh, pkt, NULL);
#ifdef DHD_LB_RXP
			dhd_lb_rx_pkt_enqueue(dhd, pkt, ifidx);
#elif defined(DHD_RX_CHAINING)
			dhd_rxchain_frame(dhd, pkt, ifidx);
#else
			dhd_bus_rx_frame(dhd->bus, pkt, ifidx, 1);
#endif /* DHD_LB_RXP */
		}

		if (pkt_newidx) {
#ifdef DHD_LB_RXP
			dhd_lb_rx_pkt_enqueue(dhd, pkt_newidx, if_newidx);
#elif defined(DHD_RX_CHAINING)
			dhd_rxchain_frame(dhd, pkt_newidx, if_newidx);
#else
			dhd_bus_rx_frame(dhd->bus, pkt_newidx, if_newidx, 1);
#endif /* DHD_LB_RXP */
		}

		pkt_cnt += pkt_cnt_newidx;

		/* Post another set of rxbufs to the device */
		dhd_prot_return_rxbuf(dhd, 0, pkt_cnt);

		/* After batch processing, check RX bound */
		n += pkt_cnt;
		if (n >= bound) {
			more = TRUE;
			break;
		}
	}

	/* Call lb_dispatch only if packets are queued */
	if (n) {
		DHD_LB_DISPATCH_RX_COMPL(dhd);
		DHD_LB_DISPATCH_RX_PROCESS(dhd);
	}

	return more;
}

/**
 * Hands transmit packets (with a caller provided flow_id) over to dongle territory (the flow ring)
 */
void
dhd_prot_update_txflowring(dhd_pub_t *dhd, uint16 flowid, void *msgring)
{
	msgbuf_ring_t *ring = (msgbuf_ring_t *)msgring;

	if (ring == NULL) {
		DHD_ERROR(("%s: NULL txflowring. exiting...\n",  __FUNCTION__));
		return;
	}
	/* Update read pointer */
	if (dhd->dma_d2h_ring_upd_support) {
		ring->rd = dhd_prot_dma_indx_get(dhd, H2D_DMA_INDX_RD_UPD, ring->idx);
	}

	DHD_TRACE(("ringid %d flowid %d write %d read %d \n\n",
		ring->idx, flowid, ring->wr, ring->rd));

	/* Need more logic here, but for now use it directly */
	dhd_bus_schedule_queue(dhd->bus, flowid, TRUE); /* from queue to flowring */
}

/** called when DHD needs to check for 'transmit complete' messages from the dongle */
bool BCMFASTPATH
dhd_prot_process_msgbuf_txcpl(dhd_pub_t *dhd, uint bound)
{
	bool more = TRUE;
	uint n = 0;
	msgbuf_ring_t *ring = &dhd->prot->d2hring_tx_cpln;

	/* Process all the messages - DTOH direction */
	while (!dhd_is_device_removed(dhd)) {
		uint8 *msg_addr;
		uint32 msg_len;

		if (dhd->hang_was_sent) {
			more = FALSE;
			break;
		}

		/* Get the address of the next message to be read from ring */
		msg_addr = dhd_prot_get_read_addr(dhd, ring, &msg_len);
		if (msg_addr == NULL) {
			more = FALSE;
			break;
		}

		/* Prefetch data to populate the cache */
		OSL_PREFETCH(msg_addr);

		if (dhd_prot_process_msgtype(dhd, ring, msg_addr, msg_len) != BCME_OK) {
			DHD_ERROR(("%s: process %s msg addr %p len %d\n",
				__FUNCTION__, ring->name, msg_addr, msg_len));
		}

		/* Write to dngl rd ptr */
		dhd_prot_upd_read_idx(dhd, ring);

		/* After batch processing, check bound */
		n += msg_len / ring->item_len;
		if (n >= bound) {
			break;
		}
	}

	DHD_LB_DISPATCH_TX_COMPL(dhd);

	return more;
}

int BCMFASTPATH
dhd_prot_process_trapbuf(dhd_pub_t *dhd)
{
	uint32 data;
	dhd_dma_buf_t *trap_addr = &dhd->prot->fw_trap_buf;

	/* Interrupts can come in before this struct
	 *  has been initialized.
	 */
	if (trap_addr->va == NULL) {
		DHD_ERROR(("%s: trap_addr->va is NULL\n", __FUNCTION__));
		return 0;
	}

	OSL_CACHE_INV((void *)trap_addr->va, sizeof(uint32));
	data = *(uint32 *)(trap_addr->va);

	if (data & D2H_DEV_FWHALT) {
		DHD_ERROR(("Firmware trapped and trap_data is 0x%04x\n", data));
		if (data & D2H_DEV_EXT_TRAP_DATA)
		{
			if (dhd->extended_trap_data) {
				OSL_CACHE_INV((void *)trap_addr->va,
				       BCMPCIE_EXT_TRAP_DATA_MAXLEN);
				memcpy(dhd->extended_trap_data, (uint32 *)trap_addr->va,
				       BCMPCIE_EXT_TRAP_DATA_MAXLEN);
			}
			DHD_ERROR(("Extended trap data available\n"));
		}
		return data;
	}
	return 0;
}

/** called when DHD needs to check for 'ioctl complete' messages from the dongle */
int BCMFASTPATH
dhd_prot_process_ctrlbuf(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	msgbuf_ring_t *ring = &prot->d2hring_ctrl_cpln;

	/* Process all the messages - DTOH direction */
	while (!dhd_is_device_removed(dhd)) {
		uint8 *msg_addr;
		uint32 msg_len;

		if (dhd->hang_was_sent) {
			break;
		}

		/* Get the address of the next message to be read from ring */
		msg_addr = dhd_prot_get_read_addr(dhd, ring, &msg_len);
		if (msg_addr == NULL) {
			break;
		}

		/* Prefetch data to populate the cache */
		OSL_PREFETCH(msg_addr);
		if (dhd_prot_process_msgtype(dhd, ring, msg_addr, msg_len) != BCME_OK) {
			DHD_ERROR(("%s: process %s msg addr %p len %d\n",
				__FUNCTION__, ring->name, msg_addr, msg_len));
		}

		/* Write to dngl rd ptr */
		dhd_prot_upd_read_idx(dhd, ring);
	}

	return 0;
}

/**
 * Consume messages out of the D2H ring. Ensure that the message's DMA to host
 * memory has completed, before invoking the message handler via a table lookup
 * of the cmn_msg_hdr::msg_type.
 */
static int BCMFASTPATH
dhd_prot_process_msgtype(dhd_pub_t *dhd, msgbuf_ring_t *ring, uint8 *buf, uint32 len)
{
	uint32 buf_len = len;
	uint16 item_len;
	uint8 msg_type;
	cmn_msg_hdr_t *msg = NULL;
	int ret = BCME_OK;

	ASSERT(ring);
	item_len = ring->item_len;
	if (item_len == 0) {
		DHD_ERROR(("%s: ringidx %d, item_len %d buf_len %d \n",
			__FUNCTION__, ring->idx, item_len, buf_len));
		return BCME_ERROR;
	}

	while (buf_len > 0) {
		if (dhd->hang_was_sent) {
			ret = BCME_ERROR;
			goto done;
		}

		msg = (cmn_msg_hdr_t *)buf;

		/* Wait until DMA completes, then fetch msg_type */
		msg_type = dhd->prot->d2h_sync_cb(dhd, ring, msg, item_len);

		/*
		 * Update the curr_rd to the current index in the ring, from where
		 * the work item is fetched. This way if the fetched work item
		 * fails in LIVELOCK, we can print the exact read index in the ring
		 * that shows up the corrupted work item.
		 */
		if ((ring->curr_rd + 1) >= ring->max_items) {
			ring->curr_rd = 0;
		} else {
			ring->curr_rd += 1;
		}

		/* Prefetch data to populate the cache */
		OSL_PREFETCH(buf + item_len);

		DHD_INFO(("msg_type %d item_len %d buf_len %d\n",
			msg_type, item_len, buf_len));

		if (msg_type == MSG_TYPE_LOOPBACK) {
			bcm_print_bytes("LPBK RESP: ", (uint8 *)msg, item_len);
			DHD_ERROR((" MSG_TYPE_LOOPBACK, len %d\n", item_len));
		}

		ASSERT(msg_type < DHD_PROT_FUNCS);
		if (msg_type >= DHD_PROT_FUNCS) {
			DHD_ERROR(("%s: msg_type %d, item_len %d buf_len %d\n",
				__FUNCTION__, msg_type, item_len, buf_len));
			ret = BCME_ERROR;
			goto done;
		}

		if (table_lookup[msg_type]) {
			table_lookup[msg_type](dhd, buf);
		}

		if (buf_len < item_len) {
			ret = BCME_ERROR;
			goto done;
		}
		buf_len = buf_len - item_len;
		buf = buf + item_len;
	}

done:

#ifdef DHD_RX_CHAINING
	dhd_rxchain_commit(dhd);
#endif

	return ret;
} /* dhd_prot_process_msgtype */

static void
dhd_prot_noop(dhd_pub_t *dhd, void *msg)
{
	return;
}

/** called on MSG_TYPE_RING_STATUS message received from dongle */
static void
dhd_prot_ringstatus_process(dhd_pub_t *dhd, void *msg)
{
	pcie_ring_status_t *ring_status = (pcie_ring_status_t *) msg;
	uint32 request_id = ltoh32(ring_status->cmn_hdr.request_id);
	uint16 status = ltoh16(ring_status->compl_hdr.status);
	uint16 ring_id = ltoh16(ring_status->compl_hdr.flow_ring_id);

	DHD_ERROR(("ring status: request_id %d, status 0x%04x, flow ring %d, write_idx %d \n",
		request_id, status, ring_id, ltoh16(ring_status->write_idx)));

	if (ltoh16(ring_status->compl_hdr.ring_id) != BCMPCIE_H2D_MSGRING_CONTROL_SUBMIT)
		return;
	if (status == BCMPCIE_BAD_PHASE) {
		/* bad phase report from */
		DHD_ERROR(("Bad phase\n"));
	}
	if (status != BCMPCIE_BADOPTION)
		return;

	if (request_id == DHD_H2D_DBGRING_REQ_PKTID) {
		if (dhd->prot->h2dring_info_subn != NULL) {
			if (dhd->prot->h2dring_info_subn->create_pending == TRUE) {
				DHD_ERROR(("H2D ring create failed for info ring\n"));
				dhd->prot->h2dring_info_subn->create_pending = FALSE;
			}
			else
				DHD_ERROR(("ring create ID for a ring, create not pending\n"));
		} else {
			DHD_ERROR(("%s info submit ring doesn't exist\n", __FUNCTION__));
		}
	}
	else if (request_id == DHD_D2H_DBGRING_REQ_PKTID) {
		if (dhd->prot->d2hring_info_cpln != NULL) {
			if (dhd->prot->d2hring_info_cpln->create_pending == TRUE) {
				DHD_ERROR(("D2H ring create failed for info ring\n"));
				dhd->prot->d2hring_info_cpln->create_pending = FALSE;
			}
			else
				DHD_ERROR(("ring create ID for info ring, create not pending\n"));
		} else {
			DHD_ERROR(("%s info cpl ring doesn't exist\n", __FUNCTION__));
		}
	}
	else {
		DHD_ERROR(("don;t know how to pair with original request\n"));
	}
	/* How do we track this to pair it with ??? */
	return;
}

/** called on MSG_TYPE_GEN_STATUS ('general status') message received from dongle */
static void
dhd_prot_genstatus_process(dhd_pub_t *dhd, void *msg)
{
	pcie_gen_status_t *gen_status = (pcie_gen_status_t *)msg;
	DHD_ERROR(("ERROR: gen status: request_id %d, STATUS 0x%04x, flow ring %d \n",
		gen_status->cmn_hdr.request_id, gen_status->compl_hdr.status,
		gen_status->compl_hdr.flow_ring_id));

	/* How do we track this to pair it with ??? */
	return;
}

/**
 * Called on MSG_TYPE_IOCTLPTR_REQ_ACK ('ioctl ack') message received from dongle, meaning that the
 * dongle received the ioctl message in dongle memory.
 */
static void
dhd_prot_ioctack_process(dhd_pub_t *dhd, void *msg)
{
	ioctl_req_ack_msg_t *ioct_ack = (ioctl_req_ack_msg_t *)msg;
	unsigned long flags;
#ifdef DHD_PKTID_AUDIT_RING
	uint32 pktid = ltoh32(ioct_ack->cmn_hdr.request_id);

	/* Skip audit for ADHD_IOCTL_REQ_PKTID = 0xFFFE */
	if (pktid != DHD_IOCTL_REQ_PKTID) {
#ifndef IOCTLRESP_USE_CONSTMEM
		DHD_PKTID_AUDIT_RING_DEBUG(dhd, dhd->prot->pktid_ctrl_map, pktid,
			DHD_TEST_IS_ALLOC, msg, D2HRING_CTRL_CMPLT_ITEMSIZE);
#else
		DHD_PKTID_AUDIT_RING_DEBUG(dhd, dhd->prot->pktid_map_handle_ioctl, pktid,
			DHD_TEST_IS_ALLOC, msg, D2HRING_CTRL_CMPLT_ITEMSIZE);
#endif /* !IOCTLRESP_USE_CONSTMEM */
	}
#endif /* DHD_PKTID_AUDIT_RING */

	DHD_GENERAL_LOCK(dhd, flags);
	if ((dhd->prot->ioctl_state & MSGBUF_IOCTL_ACK_PENDING) &&
		(dhd->prot->ioctl_state & MSGBUF_IOCTL_RESP_PENDING)) {
		dhd->prot->ioctl_state &= ~MSGBUF_IOCTL_ACK_PENDING;
	} else {
		DHD_ERROR(("%s: received ioctl ACK with state %02x trans_id = %d\n",
			__FUNCTION__, dhd->prot->ioctl_state, dhd->prot->ioctl_trans_id));
		prhex("dhd_prot_ioctack_process:",
			(uchar *)msg, D2HRING_CTRL_CMPLT_ITEMSIZE);
	}
	DHD_GENERAL_UNLOCK(dhd, flags);

	DHD_CTL(("ioctl req ack: request_id %d, status 0x%04x, flow ring %d \n",
		ioct_ack->cmn_hdr.request_id, ioct_ack->compl_hdr.status,
		ioct_ack->compl_hdr.flow_ring_id));
	if (ioct_ack->compl_hdr.status != 0)  {
		DHD_ERROR(("got an error status for the ioctl request...need to handle that\n"));
	}
#ifdef REPORT_FATAL_TIMEOUTS
	else {
		dhd_stop_bus_timer(dhd);
	}
#endif /* REPORT_FATAL_TIMEOUTS */
}

/** called on MSG_TYPE_IOCTL_CMPLT message received from dongle */
static void
dhd_prot_ioctcmplt_process(dhd_pub_t *dhd, void *msg)
{
	dhd_prot_t *prot = dhd->prot;
	uint32 pkt_id, xt_id;
	ioctl_comp_resp_msg_t *ioct_resp = (ioctl_comp_resp_msg_t *)msg;
	void *pkt;
	unsigned long flags;
	dhd_dma_buf_t retbuf;
#ifdef REPORT_FATAL_TIMEOUTS
	uint16	dhd_xt_id;
#endif

	memset(&retbuf, 0, sizeof(dhd_dma_buf_t));

	pkt_id = ltoh32(ioct_resp->cmn_hdr.request_id);

#ifdef DHD_PKTID_AUDIT_RING
#ifndef IOCTLRESP_USE_CONSTMEM
	DHD_PKTID_AUDIT_RING_DEBUG(dhd, prot->pktid_ctrl_map, pkt_id,
		DHD_DUPLICATE_FREE, msg, D2HRING_CTRL_CMPLT_ITEMSIZE);
#else
	DHD_PKTID_AUDIT_RING_DEBUG(dhd, prot->pktid_map_handle_ioctl, pkt_id,
		DHD_DUPLICATE_FREE, msg, D2HRING_CTRL_CMPLT_ITEMSIZE);
#endif /* !IOCTLRESP_USE_CONSTMEM */
#endif /* DHD_PKTID_AUDIT_RING */

	DHD_GENERAL_LOCK(dhd, flags);
	if ((prot->ioctl_state & MSGBUF_IOCTL_ACK_PENDING) ||
		!(prot->ioctl_state & MSGBUF_IOCTL_RESP_PENDING)) {
		DHD_ERROR(("%s: received ioctl response with state %02x trans_id = %d\n",
			__FUNCTION__, dhd->prot->ioctl_state, dhd->prot->ioctl_trans_id));
		prhex("dhd_prot_ioctcmplt_process:",
			(uchar *)msg, D2HRING_CTRL_CMPLT_ITEMSIZE);
		DHD_GENERAL_UNLOCK(dhd, flags);
		return;
	}

	/* Clear Response pending bit */
	prot->ioctl_state &= ~MSGBUF_IOCTL_RESP_PENDING;

#ifndef IOCTLRESP_USE_CONSTMEM
	pkt = dhd_prot_packet_get(dhd, pkt_id, PKTTYPE_IOCTL_RX, TRUE);
#else
	dhd_prot_ioctl_ret_buffer_get(dhd, pkt_id, &retbuf);
	pkt = retbuf.va;
#endif /* !IOCTLRESP_USE_CONSTMEM */
	if (!pkt) {
		DHD_GENERAL_UNLOCK(dhd, flags);
		DHD_ERROR(("%s: received ioctl response with NULL pkt\n", __FUNCTION__));
		prhex("dhd_prot_ioctcmplt_process:",
			(uchar *)msg, D2HRING_CTRL_CMPLT_ITEMSIZE);
		return;
	}
	DHD_GENERAL_UNLOCK(dhd, flags);

	prot->ioctl_resplen = ltoh16(ioct_resp->resp_len);
	prot->ioctl_status = ltoh16(ioct_resp->compl_hdr.status);
	xt_id = ltoh16(ioct_resp->trans_id);

	if (xt_id != prot->ioctl_trans_id || prot->curr_ioctl_cmd != ioct_resp->cmd) {
		DHD_ERROR(("%s: transaction id(%d %d) or cmd(%d %d) mismatch\n",
			__FUNCTION__, xt_id, prot->ioctl_trans_id,
			prot->curr_ioctl_cmd, ioct_resp->cmd));
#ifdef REPORT_FATAL_TIMEOUTS
		dhd_stop_cmd_timer(dhd);
#endif /* REPORT_FATAL_TIMEOUTS */
		dhd_wakeup_ioctl_event(dhd, IOCTL_RETURN_ON_ERROR);
		dhd_prot_debug_info_print(dhd);
#ifdef DHD_FW_COREDUMP
		if (dhd->memdump_enabled) {
			/* collect core dump */
			dhd->memdump_type = DUMP_TYPE_TRANS_ID_MISMATCH;
			dhd_bus_mem_dump(dhd);
		}
#else
		ASSERT(0);
#endif /* DHD_FW_COREDUMP */
		dhd_schedule_reset(dhd);
		goto exit;
	}
#ifdef REPORT_FATAL_TIMEOUTS
	dhd_xt_id = dhd_get_request_id(dhd);
	if (xt_id == dhd_xt_id) {
		dhd_stop_cmd_timer(dhd);
	} else {
		DHD_ERROR(("%s: Cmd timer not stopped received xt_id %d stored xt_id %d",
			__FUNCTION__, xt_id, dhd_xt_id));
	}
#endif /* REPORT_FATAL_TIMEOUTS */
	DHD_CTL(("IOCTL_COMPLETE: req_id %x transid %d status %x resplen %d\n",
		pkt_id, xt_id, prot->ioctl_status, prot->ioctl_resplen));

	if (prot->ioctl_resplen > 0) {
#ifndef IOCTLRESP_USE_CONSTMEM
		bcopy(PKTDATA(dhd->osh, pkt), prot->retbuf.va, prot->ioctl_resplen);
#else
		bcopy(pkt, prot->retbuf.va, prot->ioctl_resplen);
#endif /* !IOCTLRESP_USE_CONSTMEM */
	}

	/* wake up any dhd_os_ioctl_resp_wait() */
	dhd_wakeup_ioctl_event(dhd, IOCTL_RETURN_ON_SUCCESS);

exit:
#ifndef IOCTLRESP_USE_CONSTMEM
	dhd_prot_packet_free(dhd, pkt,
		PKTTYPE_IOCTL_RX, FALSE);
#else
	free_ioctl_return_buffer(dhd, &retbuf);
#endif /* !IOCTLRESP_USE_CONSTMEM */

	/* Post another ioctl buf to the device */
	if (prot->cur_ioctlresp_bufs_posted > 0) {
		prot->cur_ioctlresp_bufs_posted--;
	}

	dhd_msgbuf_rxbuf_post_ioctlresp_bufs(dhd);
}

/** called on MSG_TYPE_TX_STATUS message received from dongle */
static void BCMFASTPATH
dhd_prot_txstatus_process(dhd_pub_t *dhd, void *msg)
{
	dhd_prot_t *prot = dhd->prot;
	host_txbuf_cmpl_t * txstatus;
	unsigned long flags;
	uint32 pktid;
	void *pkt;
	dmaaddr_t pa;
	uint32 len;
	void *dmah;
	void *secdma;
	bool pkt_fate;
#ifdef DEVICE_TX_STUCK_DETECT
	flow_ring_node_t *flow_ring_node;
	uint16 flowid;
#endif /* DEVICE_TX_STUCK_DETECT */


	txstatus = (host_txbuf_cmpl_t *)msg;
#ifdef DEVICE_TX_STUCK_DETECT
	flowid = txstatus->compl_hdr.flow_ring_id;
	flow_ring_node = DHD_FLOW_RING(dhd, flowid);
	/**
	 * Since we got a completion message on this flowid,
	 * update tx_cmpl time stamp
	 */
	flow_ring_node->tx_cmpl = OSL_SYSUPTIME();
#endif /* DEVICE_TX_STUCK_DETECT */

	/* locks required to protect circular buffer accesses */
	DHD_GENERAL_LOCK(dhd, flags);
	pktid = ltoh32(txstatus->cmn_hdr.request_id);
	pkt_fate = TRUE;

#ifdef DHD_PKTID_AUDIT_RING
	DHD_PKTID_AUDIT_RING_DEBUG(dhd, dhd->prot->pktid_tx_map, pktid,
			DHD_DUPLICATE_FREE, msg, D2HRING_TXCMPLT_ITEMSIZE);
#endif /* DHD_PKTID_AUDIT_RING */

	DHD_INFO(("txstatus for pktid 0x%04x\n", pktid));
	if (prot->active_tx_count) {
		prot->active_tx_count--;

		/* Release the Lock when no more tx packets are pending */
		if (prot->active_tx_count == 0)
			 DHD_TXFL_WAKE_UNLOCK(dhd);
	} else {
		DHD_ERROR(("Extra packets are freed\n"));
	}

	ASSERT(pktid != 0);
#if defined(DHD_LB_TXC) && !defined(BCM_SECURE_DMA)
	{
		int elem_ix;
		void **elem;
		bcm_workq_t *workq;
		dmaaddr_t pa;
		uint32 pa_len;

		pkt = DHD_PKTID_TO_NATIVE(dhd, dhd->prot->pktid_tx_map,
			pktid, pa, pa_len, dmah, secdma, PKTTYPE_DATA_TX);

		workq = &prot->tx_compl_prod;
		/*
		 * Produce the packet into the tx_compl workq for the tx compl tasklet
		 * to consume.
		 */
		OSL_PREFETCH(PKTTAG(pkt));

		/* fetch next available slot in workq */
		elem_ix = bcm_ring_prod(WORKQ_RING(workq), DHD_LB_WORKQ_SZ);

		DHD_PKTTAG_SET_PA((dhd_pkttag_fr_t *)PKTTAG(pkt), pa);
		DHD_PKTTAG_SET_PA_LEN((dhd_pkttag_fr_t *)PKTTAG(pkt), pa_len);

		if (elem_ix == BCM_RING_FULL) {
			DHD_ERROR(("tx_compl_prod BCM_RING_FULL\n"));
			goto workq_ring_full;
		}

		elem = WORKQ_ELEMENT(void *, &prot->tx_compl_prod, elem_ix);
		*elem = pkt;

		smp_wmb();

		/* Sync WR index to consumer if the SYNC threshold has been reached */
		if (++prot->tx_compl_prod_sync >= DHD_LB_WORKQ_SYNC) {
			bcm_workq_prod_sync(workq);
			prot->tx_compl_prod_sync = 0;
		}

		DHD_INFO(("%s: tx_compl_prod pkt<%p> sync<%d>\n",
			__FUNCTION__, pkt, prot->tx_compl_prod_sync));

		DHD_GENERAL_UNLOCK(dhd, flags);

		return;
	}

workq_ring_full:

#endif /* !DHD_LB_TXC */

	pkt = DHD_PKTID_TO_NATIVE(dhd, dhd->prot->pktid_tx_map, pktid,
		pa, len, dmah, secdma, PKTTYPE_DATA_TX);

	if (pkt) {
		if (SECURE_DMA_ENAB(dhd->osh)) {
			int offset = 0;
			BCM_REFERENCE(offset);

			if (dhd->prot->tx_metadata_offset)
				offset = dhd->prot->tx_metadata_offset + ETHER_HDR_LEN;
			SECURE_DMA_UNMAP(dhd->osh, (uint) pa,
				(uint) dhd->prot->tx_metadata_offset, DMA_RX, 0, dmah,
				secdma, offset);
		} else
			DMA_UNMAP(dhd->osh, pa, (uint) len, DMA_RX, 0, dmah);
#ifdef DMAMAP_STATS
		dhd->dma_stats.txdata--;
		dhd->dma_stats.txdata_sz -= len;
#endif /* DMAMAP_STATS */
#if defined(DBG_PKT_MON) || defined(DHD_PKT_LOGGING)
		if (dhd->d11_tx_status) {
			uint16 tx_status;

			tx_status = ltoh16(txstatus->compl_hdr.status) &
				WLFC_CTL_PKTFLAG_MASK;
			pkt_fate = (tx_status == WLFC_CTL_PKTFLAG_DISCARD) ? TRUE : FALSE;

			DHD_DBG_PKT_MON_TX_STATUS(dhd, pkt, pktid, tx_status);
#ifdef DHD_PKT_LOGGING
			DHD_PKTLOG_TXS(dhd, pkt, pktid, tx_status);
#endif /* DHD_PKT_LOGGING */
		}
#endif /* DBG_PKT_MON || DHD_PKT_LOGGING */
#if defined(BCMPCIE)
		dhd_txcomplete(dhd, pkt, pkt_fate);
#endif 

#if DHD_DBG_SHOW_METADATA
		if (dhd->prot->metadata_dbg &&
		    dhd->prot->tx_metadata_offset && txstatus->metadata_len) {
			uchar *ptr;
			/* The Ethernet header of TX frame was copied and removed.
			 * Here, move the data pointer forward by Ethernet header size.
			 */
			PKTPULL(dhd->osh, pkt, ETHER_HDR_LEN);
			ptr = PKTDATA(dhd->osh, pkt)  - (dhd->prot->tx_metadata_offset);
			bcm_print_bytes("txmetadata", ptr, txstatus->metadata_len);
			dhd_prot_print_metadata(dhd, ptr, txstatus->metadata_len);
		}
#endif /* DHD_DBG_SHOW_METADATA */
		DHD_GENERAL_UNLOCK(dhd, flags);
		PKTFREE(dhd->osh, pkt, TRUE);
		DHD_GENERAL_LOCK(dhd, flags);
		DHD_FLOWRING_TXSTATUS_CNT_UPDATE(dhd->bus, txstatus->compl_hdr.flow_ring_id,
		txstatus->tx_status);

#ifdef DHD_TIMESYNC
		if (dhd->prot->tx_ts_log_enabled) {
			ts_timestamp_t *ts = (ts_timestamp_t *)&(txstatus->ts);
			dhd_timesync_log_tx_timestamp(dhd->ts,
				txstatus->compl_hdr.flow_ring_id,
				txstatus->cmn_hdr.if_id,
				ts->low, ts->high);
		}
#endif /* DHD_TIMESYNC */
	}

	DHD_GENERAL_UNLOCK(dhd, flags);

	return;
} /* dhd_prot_txstatus_process */

/** called on MSG_TYPE_WL_EVENT message received from dongle */
static void
dhd_prot_event_process(dhd_pub_t *dhd, void *msg)
{
	wlevent_req_msg_t *evnt;
	uint32 bufid;
	uint16 buflen;
	int ifidx = 0;
	void* pkt;
	unsigned long flags;
	dhd_prot_t *prot = dhd->prot;

	/* Event complete header */
	evnt = (wlevent_req_msg_t *)msg;
	bufid = ltoh32(evnt->cmn_hdr.request_id);

#ifdef DHD_PKTID_AUDIT_RING
	DHD_PKTID_AUDIT_RING_DEBUG(dhd, dhd->prot->pktid_ctrl_map, bufid,
			DHD_DUPLICATE_FREE, msg, D2HRING_CTRL_CMPLT_ITEMSIZE);
#endif /* DHD_PKTID_AUDIT_RING */

	buflen = ltoh16(evnt->event_data_len);

	ifidx = BCMMSGBUF_API_IFIDX(&evnt->cmn_hdr);

	/* Post another rxbuf to the device */
	if (prot->cur_event_bufs_posted)
		prot->cur_event_bufs_posted--;
	dhd_msgbuf_rxbuf_post_event_bufs(dhd);

	/* locks required to protect pktid_map */
	DHD_GENERAL_LOCK(dhd, flags);
	pkt = dhd_prot_packet_get(dhd, bufid, PKTTYPE_EVENT_RX, TRUE);
	DHD_GENERAL_UNLOCK(dhd, flags);

	if (!pkt) {
		DHD_ERROR(("%s: pkt is NULL for pktid %d\n", __FUNCTION__, bufid));
		return;
	}

	/* DMA RX offset updated through shared area */
	if (dhd->prot->rx_dataoffset)
		PKTPULL(dhd->osh, pkt, dhd->prot->rx_dataoffset);

	PKTSETLEN(dhd->osh, pkt, buflen);

	dhd_bus_rx_frame(dhd->bus, pkt, ifidx, 1);
}

/** called on MSG_TYPE_INFO_BUF_CMPLT message received from dongle */
static void BCMFASTPATH
dhd_prot_process_infobuf_complete(dhd_pub_t *dhd, void* buf)
{
	info_buf_resp_t *resp;
	uint32 pktid;
	uint16 buflen;
	void * pkt;
	unsigned long flags;

	resp = (info_buf_resp_t *)buf;
	pktid = ltoh32(resp->cmn_hdr.request_id);
	buflen = ltoh16(resp->info_data_len);

#ifdef DHD_PKTID_AUDIT_RING
	DHD_PKTID_AUDIT_RING_DEBUG(dhd, dhd->prot->pktid_ctrl_map, pktid,
			DHD_DUPLICATE_FREE, buf, D2HRING_INFO_BUFCMPLT_ITEMSIZE);
#endif /* DHD_PKTID_AUDIT_RING */

	DHD_INFO(("id 0x%04x, len %d, phase 0x%02x, seqnum %d, rx_dataoffset %d\n",
		pktid, buflen, resp->cmn_hdr.flags, ltoh16(resp->seqnum),
		dhd->prot->rx_dataoffset));

	if (!dhd->prot->infobufpost) {
		DHD_ERROR(("infobuf posted are zero, but there is a completion\n"));
		return;
	}

	dhd->prot->infobufpost--;
	dhd_prot_infobufpost(dhd);

	DHD_GENERAL_LOCK(dhd, flags);
	pkt = dhd_prot_packet_get(dhd, pktid, PKTTYPE_INFO_RX, TRUE);
	DHD_GENERAL_UNLOCK(dhd, flags);

	if (!pkt)
		return;

	/* DMA RX offset updated through shared area */
	if (dhd->prot->rx_dataoffset)
		PKTPULL(dhd->osh, pkt, dhd->prot->rx_dataoffset);

	PKTSETLEN(dhd->osh, pkt, buflen);

	/* info ring "debug" data, which is not a 802.3 frame, is sent/hacked with a
	 * special ifidx of -1.  This is just internal to dhd to get the data to
	 * dhd_linux.c:dhd_rx_frame() from here (dhd_prot_infobuf_cmplt_process).
	 */
	dhd_bus_rx_frame(dhd->bus, pkt, DHD_EVENT_IF /* ifidx HACK */, 1);
}

/** Stop protocol: sync w/dongle state. */
void dhd_prot_stop(dhd_pub_t *dhd)
{
	ASSERT(dhd);
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

}

/* Add any protocol-specific data header.
 * Caller must reserve prot_hdrlen prepend space.
 */
void BCMFASTPATH
dhd_prot_hdrpush(dhd_pub_t *dhd, int ifidx, void *PKTBUF)
{
	return;
}

uint
dhd_prot_hdrlen(dhd_pub_t *dhd, void *PKTBUF)
{
	return 0;
}


#define PKTBUF pktbuf

/**
 * Called when a tx ethernet packet has been dequeued from a flow queue, and has to be inserted in
 * the corresponding flow ring.
 */
int BCMFASTPATH
dhd_prot_txdata(dhd_pub_t *dhd, void *PKTBUF, uint8 ifidx)
{
	unsigned long flags;
	dhd_prot_t *prot = dhd->prot;
	host_txbuf_post_t *txdesc = NULL;
	dmaaddr_t pa, meta_pa;
	uint8 *pktdata;
	uint32 pktlen;
	uint32 pktid;
	uint8	prio;
	uint16 flowid = 0;
	uint16 alloced = 0;
	uint16	headroom;
	msgbuf_ring_t *ring;
	flow_ring_table_t *flow_ring_table;
	flow_ring_node_t *flow_ring_node;

	if (dhd->flow_ring_table == NULL) {
		return BCME_NORESOURCE;
	}

	flowid = DHD_PKT_GET_FLOWID(PKTBUF);
	flow_ring_table = (flow_ring_table_t *)dhd->flow_ring_table;
	flow_ring_node = (flow_ring_node_t *)&flow_ring_table[flowid];

	ring = (msgbuf_ring_t *)flow_ring_node->prot_info;

#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */

	DHD_GENERAL_LOCK(dhd, flags);

	/* Create a unique 32-bit packet id */
	pktid = DHD_NATIVE_TO_PKTID_RSV(dhd, dhd->prot->pktid_tx_map,
		PKTBUF, PKTTYPE_DATA_TX);
#if defined(DHD_PCIE_PKTID)
	if (pktid == DHD_PKTID_INVALID) {
		DHD_ERROR(("%s: Pktid pool depleted.\n", __FUNCTION__));
		/*
		 * If we return error here, the caller would queue the packet
		 * again. So we'll just free the skb allocated in DMA Zone.
		 * Since we have not freed the original SKB yet the caller would
		 * requeue the same.
		 */
		goto err_no_res_pktfree;
	}
#endif /* DHD_PCIE_PKTID */

	/* Reserve space in the circular buffer */
	txdesc = (host_txbuf_post_t *)
		dhd_prot_alloc_ring_space(dhd, ring, 1, &alloced, FALSE);
	if (txdesc == NULL) {
		DHD_INFO(("%s:%d: HTOD Msgbuf Not available TxCount = %d\n",
			__FUNCTION__, __LINE__, prot->active_tx_count));
		goto err_free_pktid;
	}

#ifdef DBG_PKT_MON
	DHD_DBG_PKT_MON_TX(dhd, PKTBUF, pktid);
#endif /* DBG_PKT_MON */
#ifdef DHD_PKT_LOGGING
	DHD_PKTLOG_TX(dhd, PKTBUF, pktid);
#endif /* DHD_PKT_LOGGING */


	/* Extract the data pointer and length information */
	pktdata = PKTDATA(dhd->osh, PKTBUF);
	pktlen  = PKTLEN(dhd->osh, PKTBUF);

	/* Ethernet header: Copy before we cache flush packet using DMA_MAP */
	bcopy(pktdata, txdesc->txhdr, ETHER_HDR_LEN);

	/* Extract the ethernet header and adjust the data pointer and length */
	pktdata = PKTPULL(dhd->osh, PKTBUF, ETHER_HDR_LEN);
	pktlen -= ETHER_HDR_LEN;

	/* Map the data pointer to a DMA-able address */
	if (SECURE_DMA_ENAB(dhd->osh)) {
		int offset = 0;
		BCM_REFERENCE(offset);

		if (prot->tx_metadata_offset)
			offset = prot->tx_metadata_offset + ETHER_HDR_LEN;

		pa = SECURE_DMA_MAP(dhd->osh, PKTDATA(dhd->osh, PKTBUF), pktlen,
			DMA_TX, PKTBUF, 0, ring->dma_buf.secdma, offset);
	}
#ifndef BCM_SECURE_DMA
	else
		pa = DMA_MAP(dhd->osh, PKTDATA(dhd->osh, PKTBUF), pktlen, DMA_TX, PKTBUF, 0);
#endif /* #ifndef BCM_SECURE_DMA */

	if (PHYSADDRISZERO(pa)) {
		DHD_ERROR(("%s: Something really bad, unless 0 is "
			"a valid phyaddr for pa\n", __FUNCTION__));
		ASSERT(0);
		goto err_rollback_idx;
	}

#ifdef DMAMAP_STATS
	dhd->dma_stats.txdata++;
	dhd->dma_stats.txdata_sz += pktlen;
#endif /* DMAMAP_STATS */
	/* No need to lock. Save the rest of the packet's metadata */
	DHD_NATIVE_TO_PKTID_SAVE(dhd, dhd->prot->pktid_tx_map, PKTBUF, pktid,
	    pa, pktlen, DMA_TX, NULL, ring->dma_buf.secdma, PKTTYPE_DATA_TX);

#ifdef TXP_FLUSH_NITEMS
	if (ring->pend_items_count == 0)
		ring->start_addr = (void *)txdesc;
	ring->pend_items_count++;
#endif

	/* Form the Tx descriptor message buffer */

	/* Common message hdr */
	txdesc->cmn_hdr.msg_type = MSG_TYPE_TX_POST;
	txdesc->cmn_hdr.if_id = ifidx;
	txdesc->cmn_hdr.flags = ring->current_phase;

	txdesc->flags = BCMPCIE_PKT_FLAGS_FRAME_802_3;
	prio = (uint8)PKTPRIO(PKTBUF);


	txdesc->flags |= (prio & 0x7) << BCMPCIE_PKT_FLAGS_PRIO_SHIFT;
	txdesc->seg_cnt = 1;

	txdesc->data_len = htol16((uint16) pktlen);
	txdesc->data_buf_addr.high_addr = htol32(PHYSADDRHI(pa));
	txdesc->data_buf_addr.low_addr  = htol32(PHYSADDRLO(pa));

	/* Move data pointer to keep ether header in local PKTBUF for later reference */
	PKTPUSH(dhd->osh, PKTBUF, ETHER_HDR_LEN);

	/* Handle Tx metadata */
	headroom = (uint16)PKTHEADROOM(dhd->osh, PKTBUF);
	if (prot->tx_metadata_offset && (headroom < prot->tx_metadata_offset))
		DHD_ERROR(("No headroom for Metadata tx %d %d\n",
		prot->tx_metadata_offset, headroom));

	if (prot->tx_metadata_offset && (headroom >= prot->tx_metadata_offset)) {
		DHD_TRACE(("Metadata in tx %d\n", prot->tx_metadata_offset));

		/* Adjust the data pointer to account for meta data in DMA_MAP */
		PKTPUSH(dhd->osh, PKTBUF, prot->tx_metadata_offset);

		if (SECURE_DMA_ENAB(dhd->osh)) {
			meta_pa = SECURE_DMA_MAP_TXMETA(dhd->osh, PKTDATA(dhd->osh, PKTBUF),
				prot->tx_metadata_offset + ETHER_HDR_LEN, DMA_RX, PKTBUF,
				0, ring->dma_buf.secdma);
		}
#ifndef BCM_SECURE_DMA
		else
			meta_pa = DMA_MAP(dhd->osh, PKTDATA(dhd->osh, PKTBUF),
				prot->tx_metadata_offset, DMA_RX, PKTBUF, 0);
#endif /* #ifndef BCM_SECURE_DMA */

		if (PHYSADDRISZERO(meta_pa)) {
			/* Unmap the data pointer to a DMA-able address */
			if (SECURE_DMA_ENAB(dhd->osh)) {

				int offset = 0;
				BCM_REFERENCE(offset);

				if (prot->tx_metadata_offset) {
					offset = prot->tx_metadata_offset + ETHER_HDR_LEN;
				}

				SECURE_DMA_UNMAP(dhd->osh, pa, pktlen,
					DMA_TX, 0, DHD_DMAH_NULL, ring->dma_buf.secdma, offset);
			}
#ifndef BCM_SECURE_DMA
			else {
				DMA_UNMAP(dhd->osh, pa, pktlen, DMA_TX, 0, DHD_DMAH_NULL);
			}
#endif /* #ifndef BCM_SECURE_DMA */
#ifdef TXP_FLUSH_NITEMS
			/* update pend_items_count */
			ring->pend_items_count--;
#endif /* TXP_FLUSH_NITEMS */

			DHD_ERROR(("%s: Something really bad, unless 0 is "
				"a valid phyaddr for meta_pa\n", __FUNCTION__));
			ASSERT(0);
			goto err_rollback_idx;
		}

		/* Adjust the data pointer back to original value */
		PKTPULL(dhd->osh, PKTBUF, prot->tx_metadata_offset);

		txdesc->metadata_buf_len = prot->tx_metadata_offset;
		txdesc->metadata_buf_addr.high_addr = htol32(PHYSADDRHI(meta_pa));
		txdesc->metadata_buf_addr.low_addr = htol32(PHYSADDRLO(meta_pa));
	} else {
		txdesc->metadata_buf_len = htol16(0);
		txdesc->metadata_buf_addr.high_addr = 0;
		txdesc->metadata_buf_addr.low_addr = 0;
	}

#ifdef DHD_PKTID_AUDIT_RING
	DHD_PKTID_AUDIT(dhd, prot->pktid_tx_map, pktid, DHD_DUPLICATE_ALLOC);
#endif /* DHD_PKTID_AUDIT_RING */

	txdesc->cmn_hdr.request_id = htol32(pktid);

	DHD_TRACE(("txpost: data_len %d, pktid 0x%04x\n", txdesc->data_len,
		txdesc->cmn_hdr.request_id));

	/* Update the write pointer in TCM & ring bell */
#ifdef TXP_FLUSH_NITEMS
	/* Flush if we have either hit the txp_threshold or if this msg is */
	/* occupying the last slot in the flow_ring - before wrap around.  */
	if ((ring->pend_items_count == prot->txp_threshold) ||
		((uint8 *) txdesc == (uint8 *) DHD_RING_END_VA(ring))) {
		dhd_prot_txdata_write_flush(dhd, flowid, TRUE);
	}
#else
	/* update ring's WR index and ring doorbell to dongle */
	dhd_prot_ring_write_complete(dhd, ring, txdesc, 1);
#endif

	prot->active_tx_count++;

	/*
	 * Take a wake lock, do not sleep if we have atleast one packet
	 * to finish.
	 */
	if (prot->active_tx_count >= 1)
		DHD_TXFL_WAKE_LOCK_TIMEOUT(dhd, MAX_TX_TIMEOUT);

	DHD_GENERAL_UNLOCK(dhd, flags);

#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif

	return BCME_OK;

err_rollback_idx:
	/* roll back write pointer for unprocessed message */
	if (ring->wr == 0) {
		ring->wr = ring->max_items - 1;
	} else {
		ring->wr--;
		if (ring->wr == 0) {
			DHD_INFO(("%s: flipping the phase now\n", ring->name));
			ring->current_phase = ring->current_phase ?
				0 : BCMPCIE_CMNHDR_PHASE_BIT_INIT;
		}
	}

err_free_pktid:
#if defined(DHD_PCIE_PKTID)
	{
		void *dmah;
		void *secdma;
		/* Free up the PKTID. physaddr and pktlen will be garbage. */
		DHD_PKTID_TO_NATIVE(dhd, dhd->prot->pktid_tx_map, pktid,
			pa, pktlen, dmah, secdma, PKTTYPE_NO_CHECK);
	}

err_no_res_pktfree:
#endif /* DHD_PCIE_PKTID */



	DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
	return BCME_NORESOURCE;
} /* dhd_prot_txdata */

/* called with a lock */
/** optimization to write "n" tx items at a time to ring */
void BCMFASTPATH
dhd_prot_txdata_write_flush(dhd_pub_t *dhd, uint16 flowid, bool in_lock)
{
#ifdef TXP_FLUSH_NITEMS
	unsigned long flags = 0;
	flow_ring_table_t *flow_ring_table;
	flow_ring_node_t *flow_ring_node;
	msgbuf_ring_t *ring;

	if (dhd->flow_ring_table == NULL) {
		return;
	}

	if (!in_lock) {
		DHD_GENERAL_LOCK(dhd, flags);
	}

	flow_ring_table = (flow_ring_table_t *)dhd->flow_ring_table;
	flow_ring_node = (flow_ring_node_t *)&flow_ring_table[flowid];
	ring = (msgbuf_ring_t *)flow_ring_node->prot_info;

	if (ring->pend_items_count) {
		/* update ring's WR index and ring doorbell to dongle */
		dhd_prot_ring_write_complete(dhd, ring, ring->start_addr,
			ring->pend_items_count);
		ring->pend_items_count = 0;
		ring->start_addr = NULL;
	}

	if (!in_lock) {
		DHD_GENERAL_UNLOCK(dhd, flags);
	}
#endif /* TXP_FLUSH_NITEMS */
}

#undef PKTBUF	/* Only defined in the above routine */

int BCMFASTPATH
dhd_prot_hdrpull(dhd_pub_t *dhd, int *ifidx, void *pkt, uchar *buf, uint *len)
{
	return 0;
}

/** post a set of receive buffers to the dongle */
static void BCMFASTPATH
dhd_prot_return_rxbuf(dhd_pub_t *dhd, uint32 pktid, uint32 rxcnt)
{
	dhd_prot_t *prot = dhd->prot;
#if defined(DHD_LB_RXC)
	int elem_ix;
	uint32 *elem;
	bcm_workq_t *workq;

	workq = &prot->rx_compl_prod;

	/* Produce the work item */
	elem_ix = bcm_ring_prod(WORKQ_RING(workq), DHD_LB_WORKQ_SZ);
	if (elem_ix == BCM_RING_FULL) {
		DHD_ERROR(("%s LB RxCompl workQ is full\n", __FUNCTION__));
		ASSERT(0);
		return;
	}

	elem = WORKQ_ELEMENT(uint32, workq, elem_ix);
	*elem = pktid;

	smp_wmb();

	/* Sync WR index to consumer if the SYNC threshold has been reached */
	if (++prot->rx_compl_prod_sync >= DHD_LB_WORKQ_SYNC) {
		bcm_workq_prod_sync(workq);
		prot->rx_compl_prod_sync = 0;
	}

	DHD_INFO(("%s: rx_compl_prod pktid<%u> sync<%d>\n",
		__FUNCTION__, pktid, prot->rx_compl_prod_sync));

#endif /* DHD_LB_RXC */

	if (prot->rxbufpost >= rxcnt) {
		prot->rxbufpost -= (uint16)rxcnt;
	} else {
		/* ASSERT(0); */
		prot->rxbufpost = 0;
	}

#if !defined(DHD_LB_RXC)
	if (prot->rxbufpost <= (prot->max_rxbufpost - RXBUFPOST_THRESHOLD))
		dhd_msgbuf_rxbuf_post(dhd, FALSE); /* alloc pkt ids */
#endif /* !DHD_LB_RXC */
	return;
}

/* called before an ioctl is sent to the dongle */
static void
dhd_prot_wlioctl_intercept(dhd_pub_t *dhd, wl_ioctl_t * ioc, void * buf)
{
	dhd_prot_t *prot = dhd->prot;

	if (ioc->cmd == WLC_SET_VAR && buf != NULL && !strcmp(buf, "pcie_bus_tput")) {
		int slen = 0;
		pcie_bus_tput_params_t *tput_params;

		slen = strlen("pcie_bus_tput") + 1;
		tput_params = (pcie_bus_tput_params_t*)((char *)buf + slen);
		bcopy(&prot->host_bus_throughput_buf.pa, &tput_params->host_buf_addr,
			sizeof(tput_params->host_buf_addr));
		tput_params->host_buf_len = DHD_BUS_TPUT_BUF_LEN;
	}
}


/** Use protocol to issue ioctl to dongle. Only one ioctl may be in transit. */
int dhd_prot_ioctl(dhd_pub_t *dhd, int ifidx, wl_ioctl_t * ioc, void * buf, int len)
{
	int ret = -1;
	uint8 action;

	if ((dhd->busstate == DHD_BUS_DOWN) || dhd->hang_was_sent) {
		DHD_ERROR(("%s : bus is down. we have nothing to do - bs: %d, has: %d\n",
				__FUNCTION__, dhd->busstate, dhd->hang_was_sent));
		goto done;
	}

	if (dhd->busstate == DHD_BUS_SUSPEND) {
		DHD_ERROR(("%s : bus is suspended\n", __FUNCTION__));
		goto done;
	}

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (ioc->cmd == WLC_SET_PM) {
		DHD_TRACE_HW4(("%s: SET PM to %d\n", __FUNCTION__, buf ? *(char *)buf : 0));
	}

	ASSERT(len <= WLC_IOCTL_MAXLEN);

	if (len > WLC_IOCTL_MAXLEN)
		goto done;

	action = ioc->set;

	dhd_prot_wlioctl_intercept(dhd, ioc, buf);

	if (action & WL_IOCTL_ACTION_SET) {
		ret = dhd_msgbuf_set_ioctl(dhd, ifidx, ioc->cmd, buf, len, action);
	} else {
		ret = dhd_msgbuf_query_ioctl(dhd, ifidx, ioc->cmd, buf, len, action);
		if (ret > 0)
			ioc->used = ret;
	}

	/* Too many programs assume ioctl() returns 0 on success */
	if (ret >= 0) {
		ret = 0;
	} else {
		DHD_INFO(("%s: status ret value is %d \n", __FUNCTION__, ret));
		dhd->dongle_error = ret;
	}

	if (!ret && ioc->cmd == WLC_SET_VAR && buf != NULL) {
		/* Intercept the wme_dp ioctl here */
		if (!strcmp(buf, "wme_dp")) {
			int slen, val = 0;

			slen = strlen("wme_dp") + 1;
			if (len >= (int)(slen + sizeof(int)))
				bcopy(((char *)buf + slen), &val, sizeof(int));
			dhd->wme_dp = (uint8) ltoh32(val);
		}

	}

done:
	return ret;

} /* dhd_prot_ioctl */

/** test / loopback */

int
dhdmsgbuf_lpbk_req(dhd_pub_t *dhd, uint len)
{
	unsigned long flags;
	dhd_prot_t *prot = dhd->prot;
	uint16 alloced = 0;

	ioct_reqst_hdr_t *ioct_rqst;

	uint16 hdrlen = sizeof(ioct_reqst_hdr_t);
	uint16 msglen = len + hdrlen;
	msgbuf_ring_t *ring = &prot->h2dring_ctrl_subn;

	msglen = ALIGN_SIZE(msglen, DMA_ALIGN_LEN);
	msglen = LIMIT_TO_MAX(msglen, MSGBUF_MAX_MSG_SIZE);

#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */

	DHD_GENERAL_LOCK(dhd, flags);

	ioct_rqst = (ioct_reqst_hdr_t *)
		dhd_prot_alloc_ring_space(dhd, ring, 1, &alloced, FALSE);

	if (ioct_rqst == NULL) {
		DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef PCIE_INB_DW
		dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
		return 0;
	}

	{
		uint8 *ptr;
		uint16 i;

		ptr = (uint8 *)ioct_rqst;
		for (i = 0; i < msglen; i++) {
			ptr[i] = i % 256;
		}
	}

	/* Common msg buf hdr */
	ioct_rqst->msg.epoch = ring->seqnum % H2D_EPOCH_MODULO;
	ring->seqnum++;

	ioct_rqst->msg.msg_type = MSG_TYPE_LOOPBACK;
	ioct_rqst->msg.if_id = 0;
	ioct_rqst->msg.flags = ring->current_phase;

	bcm_print_bytes("LPBK REQ: ", (uint8 *)ioct_rqst, msglen);

	/* update ring's WR index and ring doorbell to dongle */
	dhd_prot_ring_write_complete(dhd, ring, ioct_rqst, 1);
	DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif

	return 0;
}

/** test / loopback */
void dmaxfer_free_dmaaddr(dhd_pub_t *dhd, dhd_dmaxfer_t *dmaxfer)
{
	if (dmaxfer == NULL)
		return;

	dhd_dma_buf_free(dhd, &dmaxfer->srcmem);
	dhd_dma_buf_free(dhd, &dmaxfer->dstmem);
}

/** test / loopback */
int
dhd_prepare_schedule_dmaxfer_free(dhd_pub_t *dhdp)
{
	dhd_prot_t *prot = dhdp->prot;
	dhd_dmaxfer_t *dmaxfer = &prot->dmaxfer;
	dmaxref_mem_map_t *dmap = NULL;

	dmap = MALLOCZ(dhdp->osh, sizeof(dmaxref_mem_map_t));
	if (!dmap) {
		DHD_ERROR(("%s: dmap alloc failed\n", __FUNCTION__));
		goto mem_alloc_fail;
	}
	dmap->srcmem = &(dmaxfer->srcmem);
	dmap->dstmem = &(dmaxfer->dstmem);

	DMAXFER_FREE(dhdp, dmap);
	return BCME_OK;

mem_alloc_fail:
	if (dmap) {
		MFREE(dhdp->osh, dmap, sizeof(dmaxref_mem_map_t));
		dmap = NULL;
	}
	return BCME_NOMEM;
} /* dhd_prepare_schedule_dmaxfer_free */


/** test / loopback */
void
dmaxfer_free_prev_dmaaddr(dhd_pub_t *dhdp, dmaxref_mem_map_t *dmmap)
{

	dhd_dma_buf_free(dhdp, dmmap->srcmem);
	dhd_dma_buf_free(dhdp, dmmap->dstmem);

	MFREE(dhdp->osh, dmmap, sizeof(dmaxref_mem_map_t));
	dmmap = NULL;

} /* dmaxfer_free_prev_dmaaddr */


/** test / loopback */
int dmaxfer_prepare_dmaaddr(dhd_pub_t *dhd, uint len,
	uint srcdelay, uint destdelay, dhd_dmaxfer_t *dmaxfer)
{
	uint i;
	if (!dmaxfer)
		return BCME_ERROR;

	/* First free up existing buffers */
	dmaxfer_free_dmaaddr(dhd, dmaxfer);

	if (dhd_dma_buf_alloc(dhd, &dmaxfer->srcmem, len)) {
		return BCME_NOMEM;
	}

	if (dhd_dma_buf_alloc(dhd, &dmaxfer->dstmem, len + 8)) {
		dhd_dma_buf_free(dhd, &dmaxfer->srcmem);
		return BCME_NOMEM;
	}

	dmaxfer->len = len;

	/* Populate source with a pattern */
	for (i = 0; i < dmaxfer->len; i++) {
		((uint8*)dmaxfer->srcmem.va)[i] = i % 256;
	}
	OSL_CACHE_FLUSH(dmaxfer->srcmem.va, dmaxfer->len);

	dmaxfer->srcdelay = srcdelay;
	dmaxfer->destdelay = destdelay;

	return BCME_OK;
} /* dmaxfer_prepare_dmaaddr */

static void
dhd_msgbuf_dmaxfer_process(dhd_pub_t *dhd, void *msg)
{
	dhd_prot_t *prot = dhd->prot;
	uint64 end_usec;
	pcie_dmaxfer_cmplt_t *cmplt = (pcie_dmaxfer_cmplt_t *)msg;

	BCM_REFERENCE(cmplt);
	DHD_INFO(("DMA status: %d\n", cmplt->compl_hdr.status));
	OSL_CACHE_INV(prot->dmaxfer.dstmem.va, prot->dmaxfer.len);
	if (prot->dmaxfer.srcmem.va && prot->dmaxfer.dstmem.va) {
		if (memcmp(prot->dmaxfer.srcmem.va,
		        prot->dmaxfer.dstmem.va, prot->dmaxfer.len)) {
			prhex("XFER SRC: ",
			    prot->dmaxfer.srcmem.va, prot->dmaxfer.len);
			prhex("XFER DST: ",
			    prot->dmaxfer.dstmem.va, prot->dmaxfer.len);
		        DHD_ERROR(("DMA failed\n"));
		}
		else {
			if (prot->dmaxfer.d11_lpbk) {
				DHD_ERROR(("DMA successful with d11 loopback\n"));
			} else {
				DHD_ERROR(("DMA successful without d11 loopback\n"));
			}
		}
	}
	end_usec = OSL_SYSUPTIME_US();
	dhd_prepare_schedule_dmaxfer_free(dhd);
	end_usec -= prot->dmaxfer.start_usec;
	DHD_ERROR(("DMA loopback %d bytes in %llu usec, %u kBps\n",
		prot->dmaxfer.len, end_usec,
		(prot->dmaxfer.len * (1000 * 1000 / 1024) / (uint32)(end_usec + 1))));
	dhd->prot->dmaxfer.in_progress = FALSE;
}

/** Test functionality.
 * Transfers bytes from host to dongle and to host again using DMA
 * This function is not reentrant, as prot->dmaxfer.in_progress is not protected
 * by a spinlock.
 */
int
dhdmsgbuf_dmaxfer_req(dhd_pub_t *dhd, uint len, uint srcdelay, uint destdelay, uint d11_lpbk)
{
	unsigned long flags;
	int ret = BCME_OK;
	dhd_prot_t *prot = dhd->prot;
	pcie_dma_xfer_params_t *dmap;
	uint32 xferlen = LIMIT_TO_MAX(len, DMA_XFER_LEN_LIMIT);
	uint16 alloced = 0;
	msgbuf_ring_t *ring = &prot->h2dring_ctrl_subn;

	if (prot->dmaxfer.in_progress) {
		DHD_ERROR(("DMA is in progress...\n"));
		return ret;
	}

	prot->dmaxfer.in_progress = TRUE;
	if ((ret = dmaxfer_prepare_dmaaddr(dhd, xferlen, srcdelay, destdelay,
	        &prot->dmaxfer)) != BCME_OK) {
		prot->dmaxfer.in_progress = FALSE;
		return ret;
	}

#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */

	DHD_GENERAL_LOCK(dhd, flags);

	dmap = (pcie_dma_xfer_params_t *)
		dhd_prot_alloc_ring_space(dhd, ring, 1, &alloced, FALSE);

	if (dmap == NULL) {
		dmaxfer_free_dmaaddr(dhd, &prot->dmaxfer);
		prot->dmaxfer.in_progress = FALSE;
		DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef PCIE_INB_DW
		dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
		return BCME_NOMEM;
	}

	/* Common msg buf hdr */
	dmap->cmn_hdr.msg_type = MSG_TYPE_LPBK_DMAXFER;
	dmap->cmn_hdr.request_id = htol32(DHD_FAKE_PKTID);
	dmap->cmn_hdr.epoch = ring->seqnum % H2D_EPOCH_MODULO;
	dmap->cmn_hdr.flags = ring->current_phase;
	ring->seqnum++;

	dmap->host_input_buf_addr.high = htol32(PHYSADDRHI(prot->dmaxfer.srcmem.pa));
	dmap->host_input_buf_addr.low = htol32(PHYSADDRLO(prot->dmaxfer.srcmem.pa));
	dmap->host_ouput_buf_addr.high = htol32(PHYSADDRHI(prot->dmaxfer.dstmem.pa));
	dmap->host_ouput_buf_addr.low = htol32(PHYSADDRLO(prot->dmaxfer.dstmem.pa));
	dmap->xfer_len = htol32(prot->dmaxfer.len);
	dmap->srcdelay = htol32(prot->dmaxfer.srcdelay);
	dmap->destdelay = htol32(prot->dmaxfer.destdelay);
	prot->dmaxfer.d11_lpbk = d11_lpbk ? 1 : 0;
	dmap->flags = (prot->dmaxfer.d11_lpbk << PCIE_DMA_XFER_FLG_D11_LPBK_SHIFT)
			& PCIE_DMA_XFER_FLG_D11_LPBK_MASK;

	/* update ring's WR index and ring doorbell to dongle */
	prot->dmaxfer.start_usec = OSL_SYSUPTIME_US();
	dhd_prot_ring_write_complete(dhd, ring, dmap, 1);
	DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif

	DHD_INFO(("DMA Started...\n"));

	return BCME_OK;
} /* dhdmsgbuf_dmaxfer_req */

/** Called in the process of submitting an ioctl to the dongle */
static int
dhd_msgbuf_query_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf, uint len, uint8 action)
{
	int ret = 0;
	uint copylen = 0;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (cmd == WLC_GET_VAR && buf)
	{
		if (!len || !*(uint8 *)buf) {
			DHD_ERROR(("%s(): Zero length bailing\n", __FUNCTION__));
			ret = BCME_BADARG;
			goto done;
		}

		/* Respond "bcmerror" and "bcmerrorstr" with local cache */
		copylen = MIN(len, BCME_STRLEN);

		if ((len >= strlen("bcmerrorstr")) &&
			(!strcmp((char *)buf, "bcmerrorstr"))) {

			strncpy((char *)buf, bcmerrorstr(dhd->dongle_error), copylen);
			*(uint8 *)((uint8 *)buf + (copylen - 1)) = '\0';

			goto done;
		} else if ((len >= strlen("bcmerror")) &&
			!strcmp((char *)buf, "bcmerror")) {

			*(uint32 *)(uint32 *)buf = dhd->dongle_error;

			goto done;
		}
	}


	DHD_CTL(("query_ioctl: ACTION %d ifdix %d cmd %d len %d \n",
	    action, ifidx, cmd, len));
#ifdef REPORT_FATAL_TIMEOUTS
	/*
	 * These timers "should" be started before sending H2D interrupt.
	 * Think of the scenario where H2D interrupt is fired and the Dongle
	 * responds back immediately. From the DPC we would stop the cmd, bus
	 * timers. But the process context could have switched out leading to
	 * a situation where the timers are Not started yet, but are actually stopped.
	 *
	 * Disable preemption from the time we start the timer until we are done
	 * with seding H2D interrupts.
	 */
	OSL_DISABLE_PREEMPTION(dhd->osh);
	dhd_set_request_id(dhd, dhd->prot->ioctl_trans_id+1, cmd);
	dhd_start_cmd_timer(dhd);
	dhd_start_bus_timer(dhd);
#endif /* REPORT_FATAL_TIMEOUTS */

	ret = dhd_fillup_ioct_reqst(dhd, (uint16)len, cmd, buf, ifidx);

#ifdef REPORT_FATAL_TIMEOUTS
	/* For some reason if we fail to ring door bell, stop the timers */
	if (ret < 0) {
		DHD_ERROR(("%s(): dhd_fillup_ioct_reqst failed \r\n", __FUNCTION__));
		dhd_stop_cmd_timer(dhd);
		dhd_stop_bus_timer(dhd);
		OSL_ENABLE_PREEMPTION(dhd->osh);
		goto done;
	}
	OSL_ENABLE_PREEMPTION(dhd->osh);
#else
	if (ret < 0) {
		DHD_ERROR(("%s(): dhd_fillup_ioct_reqst failed \r\n", __FUNCTION__));
		goto done;
	}
#endif /* REPORT_FATAL_TIMEOUTS */

	/* wait for IOCTL completion message from dongle and get first fragment */
	ret = dhd_msgbuf_wait_ioctl_cmplt(dhd, len, buf);

done:
	return ret;
}

/**
 * Waits for IOCTL completion message from the dongle, copies this into caller
 * provided parameter 'buf'.
 */
static int
dhd_msgbuf_wait_ioctl_cmplt(dhd_pub_t *dhd, uint32 len, void *buf)
{
	dhd_prot_t *prot = dhd->prot;
	int timeleft;
	unsigned long flags;
	int ret = 0;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (dhd_query_bus_erros(dhd)) {
		ret = -EIO;
		goto out;
	}

	timeleft = dhd_os_ioctl_resp_wait(dhd, (uint *)&prot->ioctl_received);

#ifdef DHD_RECOVER_TIMEOUT
	if (prot->ioctl_received == 0) {
		uint32 intstatus = 0;
		uint32 intmask = 0;
		intstatus = si_corereg(dhd->bus->sih,
			dhd->bus->sih->buscoreidx, PCIMailBoxInt, 0, 0);
		intmask = si_corereg(dhd->bus->sih,
			dhd->bus->sih->buscoreidx, PCIMailBoxMask, 0, 0);
		if ((intstatus) && (!intmask) && (timeleft == 0) && (!dhd_query_bus_erros(dhd)))
		{
			DHD_ERROR(("%s: iovar timeout trying again intstatus=%x intmask=%x\n",
				__FUNCTION__, intstatus, intmask));
			DHD_ERROR(("\n ------- DUMPING INTR enable/disable counters\r\n"));
			DHD_ERROR(("resume_intr_enable_count=%lu dpc_intr_enable_count=%lu\n"
				"isr_intr_disable_count=%lu suspend_intr_disable_count=%lu\n"
				"dpc_return_busdown_count=%lu\n",
				dhd->bus->resume_intr_enable_count, dhd->bus->dpc_intr_enable_count,
				dhd->bus->isr_intr_disable_count,
				dhd->bus->suspend_intr_disable_count,
				dhd->bus->dpc_return_busdown_count));

			dhd_prot_process_ctrlbuf(dhd);

			timeleft = dhd_os_ioctl_resp_wait(dhd, (uint *)&prot->ioctl_received);
			/* Enable Back Interrupts using IntMask */
			dhdpcie_bus_intr_enable(dhd->bus);
		}
	}
#endif /* DHD_RECOVER_TIMEOUT */

	if (timeleft == 0 && (!dhd_query_bus_erros(dhd))) {
		uint32 intstatus;

		dhd->rxcnt_timeout++;
		dhd->rx_ctlerrs++;
		dhd->iovar_timeout_occured = TRUE;
		DHD_ERROR(("%s: resumed on timeout rxcnt_timeout %d ioctl_cmd %d "
			"trans_id %d state %d busstate=%d ioctl_received=%d\n",
			__FUNCTION__, dhd->rxcnt_timeout, prot->curr_ioctl_cmd,
			prot->ioctl_trans_id, prot->ioctl_state,
			dhd->busstate, prot->ioctl_received));
		if (prot->curr_ioctl_cmd == WLC_SET_VAR ||
			prot->curr_ioctl_cmd == WLC_GET_VAR) {
			char iovbuf[32];
			int i;
			int dump_size = 128;
			uint8 *ioctl_buf = (uint8 *)prot->ioctbuf.va;
			memset(iovbuf, 0, sizeof(iovbuf));
			strncpy(iovbuf, ioctl_buf, sizeof(iovbuf) - 1);
			iovbuf[sizeof(iovbuf) - 1] = '\0';
			DHD_ERROR(("Current IOVAR (%s): %s\n",
				prot->curr_ioctl_cmd == WLC_SET_VAR ?
				"WLC_SET_VAR" : "WLC_GET_VAR", iovbuf));
			DHD_ERROR(("========== START IOCTL REQBUF DUMP ==========\n"));
			for (i = 0; i < dump_size; i++) {
				DHD_ERROR(("%02X ", ioctl_buf[i]));
				if ((i % 32) == 31) {
					DHD_ERROR(("\n"));
				}
			}
			DHD_ERROR(("\n========== END IOCTL REQBUF DUMP ==========\n"));
		}

		/* Check the PCIe link status by reading intstatus register */
		intstatus = si_corereg(dhd->bus->sih,
			dhd->bus->sih->buscoreidx, PCIMailBoxInt, 0, 0);
		if (intstatus == (uint32)-1) {
			DHD_ERROR(("%s : PCIe link might be down\n", __FUNCTION__));
			dhd->bus->is_linkdown = TRUE;
		}

		dhd_bus_dump_console_buffer(dhd->bus);
		dhd_prot_debug_info_print(dhd);

#ifdef DHD_FW_COREDUMP
		/* Collect socram dump */
		if (dhd->memdump_enabled) {
			/* collect core dump */
			dhd->memdump_type = DUMP_TYPE_RESUMED_ON_TIMEOUT;
			dhd_bus_mem_dump(dhd);
		}
#endif /* DHD_FW_COREDUMP */
#ifdef SUPPORT_LINKDOWN_RECOVERY
#ifdef CONFIG_ARCH_MSM
		dhd->bus->no_cfg_restore = 1;
#endif /* CONFIG_ARCH_MSM */
#endif /* SUPPORT_LINKDOWN_RECOVERY */
		ret = -ETIMEDOUT;
		goto out;
	} else {
		if (prot->ioctl_received != IOCTL_RETURN_ON_SUCCESS) {
			DHD_ERROR(("%s: IOCTL failure due to ioctl_received = %d\n",
				__FUNCTION__, prot->ioctl_received));
			ret = -EINVAL;
			goto out;
		}
		dhd->rxcnt_timeout = 0;
		dhd->rx_ctlpkts++;
		DHD_CTL(("%s: ioctl resp resumed, got %d\n",
			__FUNCTION__, prot->ioctl_resplen));
	}

	if (dhd->prot->ioctl_resplen > len)
		dhd->prot->ioctl_resplen = (uint16)len;
	if (buf)
		bcopy(dhd->prot->retbuf.va, buf, dhd->prot->ioctl_resplen);

	ret = (int)(dhd->prot->ioctl_status);

out:
	DHD_GENERAL_LOCK(dhd, flags);
	dhd->prot->ioctl_state = 0;
	dhd->prot->ioctl_resplen = 0;
	dhd->prot->ioctl_received = IOCTL_WAIT;
	dhd->prot->curr_ioctl_cmd = 0;
	DHD_GENERAL_UNLOCK(dhd, flags);

	return ret;
} /* dhd_msgbuf_wait_ioctl_cmplt */

static int
dhd_msgbuf_set_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf, uint len, uint8 action)
{
	int ret = 0;

	DHD_TRACE(("%s: Enter \n", __FUNCTION__));

	if (dhd->busstate == DHD_BUS_DOWN) {
		DHD_ERROR(("%s : bus is down. we have nothing to do\n", __FUNCTION__));
		return -EIO;
	}

	/* don't talk to the dongle if fw is about to be reloaded */
	if (dhd->hang_was_sent) {
		DHD_ERROR(("%s: HANG was sent up earlier. Not talking to the chip\n",
			__FUNCTION__));
		return -EIO;
	}

	DHD_CTL(("ACTION %d ifdix %d cmd %d len %d \n",
		action, ifidx, cmd, len));

#ifdef REPORT_FATAL_TIMEOUTS
	/*
	 * These timers "should" be started before sending H2D interrupt.
	 * Think of the scenario where H2D interrupt is fired and the Dongle
	 * responds back immediately. From the DPC we would stop the cmd, bus
	 * timers. But the process context could have switched out leading to
	 * a situation where the timers are Not started yet, but are actually stopped.
	 *
	 * Disable preemption from the time we start the timer until we are done
	 * with seding H2D interrupts.
	 */
	OSL_DISABLE_PREEMPTION(dhd->osh);
	dhd_set_request_id(dhd, dhd->prot->ioctl_trans_id+1, cmd);
	dhd_start_cmd_timer(dhd);
	dhd_start_bus_timer(dhd);
#endif /* REPORT_FATAL_TIMEOUTS */

	/* Fill up msgbuf for ioctl req */
	ret = dhd_fillup_ioct_reqst(dhd, (uint16)len, cmd, buf, ifidx);

#ifdef REPORT_FATAL_TIMEOUTS
	/* For some reason if we fail to ring door bell, stop the timers */
	if (ret < 0) {
		DHD_ERROR(("%s(): dhd_fillup_ioct_reqst failed \r\n", __FUNCTION__));
		dhd_stop_cmd_timer(dhd);
		dhd_stop_bus_timer(dhd);
		OSL_ENABLE_PREEMPTION(dhd->osh);
		goto done;
	}

	OSL_ENABLE_PREEMPTION(dhd->osh);
#else
	if (ret < 0) {
		DHD_ERROR(("%s(): dhd_fillup_ioct_reqst failed \r\n", __FUNCTION__));
		goto done;
	}
#endif /* REPORT_FATAL_TIMEOUTS */

	ret = dhd_msgbuf_wait_ioctl_cmplt(dhd, len, buf);

done:
	return ret;
}

/** Called by upper DHD layer. Handles a protocol control response asynchronously. */
int dhd_prot_ctl_complete(dhd_pub_t *dhd)
{
	return 0;
}

/** Called by upper DHD layer. Check for and handle local prot-specific iovar commands */
int dhd_prot_iovar_op(dhd_pub_t *dhd, const char *name,
                             void *params, int plen, void *arg, int len, bool set)
{
	return BCME_UNSUPPORTED;
}

/** Add prot dump output to a buffer */
void dhd_prot_dump(dhd_pub_t *dhd, struct bcmstrbuf *b)
{

	if (dhd->d2h_sync_mode & PCIE_SHARED_D2H_SYNC_SEQNUM)
		bcm_bprintf(b, "\nd2h_sync: SEQNUM:");
	else if (dhd->d2h_sync_mode & PCIE_SHARED_D2H_SYNC_XORCSUM)
		bcm_bprintf(b, "\nd2h_sync: XORCSUM:");
	else
		bcm_bprintf(b, "\nd2h_sync: NONE:");
	bcm_bprintf(b, " d2h_sync_wait max<%lu> tot<%lu>\n",
		dhd->prot->d2h_sync_wait_max, dhd->prot->d2h_sync_wait_tot);

	bcm_bprintf(b, "\nDongle DMA Indices: h2d %d  d2h %d index size %d bytes\n",
		dhd->dma_h2d_ring_upd_support,
		dhd->dma_d2h_ring_upd_support,
		dhd->prot->rw_index_sz);
	bcm_bprintf(b, "h2d_max_txpost: %d, prot->h2d_max_txpost: %d\n",
		h2d_max_txpost, dhd->prot->h2d_max_txpost);
}

/* Update local copy of dongle statistics */
void dhd_prot_dstats(dhd_pub_t *dhd)
{
	return;
}

/** Called by upper DHD layer */
int dhd_process_pkt_reorder_info(dhd_pub_t *dhd, uchar *reorder_info_buf,
	uint reorder_info_len, void **pkt, uint32 *free_buf_count)
{
	return 0;
}

/** Debug related, post a dummy message to interrupt dongle. Used to process cons commands. */
int
dhd_post_dummy_msg(dhd_pub_t *dhd)
{
	unsigned long flags;
	hostevent_hdr_t *hevent = NULL;
	uint16 alloced = 0;

	dhd_prot_t *prot = dhd->prot;
	msgbuf_ring_t *ring = &prot->h2dring_ctrl_subn;

#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */

	DHD_GENERAL_LOCK(dhd, flags);

	hevent = (hostevent_hdr_t *)
		dhd_prot_alloc_ring_space(dhd, ring, 1, &alloced, FALSE);

	if (hevent == NULL) {
		DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef PCIE_INB_DW
		dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
		return -1;
	}

	/* CMN msg header */
	hevent->msg.epoch = ring->seqnum % H2D_EPOCH_MODULO;
	ring->seqnum++;
	hevent->msg.msg_type = MSG_TYPE_HOST_EVNT;
	hevent->msg.if_id = 0;
	hevent->msg.flags = ring->current_phase;

	/* Event payload */
	hevent->evnt_pyld = htol32(HOST_EVENT_CONS_CMD);

	/* Since, we are filling the data directly into the bufptr obtained
	 * from the msgbuf, we can directly call the write_complete
	 */
	dhd_prot_ring_write_complete(dhd, ring, hevent, 1);
	DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif

	return 0;
}

/**
 * If exactly_nitems is true, this function will allocate space for nitems or fail
 * If exactly_nitems is false, this function will allocate space for nitems or less
 */
static void * BCMFASTPATH
dhd_prot_alloc_ring_space(dhd_pub_t *dhd, msgbuf_ring_t *ring,
	uint16 nitems, uint16 * alloced, bool exactly_nitems)
{
	void * ret_buf;

	/* Alloc space for nitems in the ring */
	ret_buf = dhd_prot_get_ring_space(ring, nitems, alloced, exactly_nitems);

	if (ret_buf == NULL) {
		/* if alloc failed , invalidate cached read ptr */
		if (dhd->dma_d2h_ring_upd_support) {
			ring->rd = dhd_prot_dma_indx_get(dhd, H2D_DMA_INDX_RD_UPD, ring->idx);
		} else {
			dhd_bus_cmn_readshared(dhd->bus, &(ring->rd), RING_RD_UPD, ring->idx);
#ifdef SUPPORT_LINKDOWN_RECOVERY
			/* Check if ring->rd is valid */
			if (ring->rd >= ring->max_items) {
				dhd->bus->read_shm_fail = TRUE;
				DHD_ERROR(("%s: Invalid rd idx=%d\n", ring->name, ring->rd));
				return NULL;
			}
#endif /* SUPPORT_LINKDOWN_RECOVERY */
		}

		/* Try allocating once more */
		ret_buf = dhd_prot_get_ring_space(ring, nitems, alloced, exactly_nitems);

		if (ret_buf == NULL) {
			DHD_INFO(("%s: Ring space not available  \n", ring->name));
			return NULL;
		}
	}

	if (ret_buf == HOST_RING_BASE(ring)) {
		DHD_INFO(("%s: setting the phase now\n", ring->name));
		ring->current_phase = ring->current_phase ? 0 : BCMPCIE_CMNHDR_PHASE_BIT_INIT;
	}

	/* Return alloced space */
	return ret_buf;
}

/**
 * Non inline ioct request.
 * Form a ioctl request first as per ioctptr_reqst_hdr_t header in the circular buffer
 * Form a separate request buffer where a 4 byte cmn header is added in the front
 * buf contents from parent function is copied to remaining section of this buffer
 */
static int
dhd_fillup_ioct_reqst(dhd_pub_t *dhd, uint16 len, uint cmd, void* buf, int ifidx)
{
	dhd_prot_t *prot = dhd->prot;
	ioctl_req_msg_t *ioct_rqst;
	void * ioct_buf;	/* For ioctl payload */
	uint16  rqstlen, resplen;
	unsigned long flags;
	uint16 alloced = 0;
	msgbuf_ring_t *ring = &prot->h2dring_ctrl_subn;

	if (dhd_query_bus_erros(dhd)) {
		return -EIO;
	}

	rqstlen = len;
	resplen = len;

	/* Limit ioct request to MSGBUF_MAX_MSG_SIZE bytes including hdrs */
	/* 8K allocation of dongle buffer fails */
	/* dhd doesnt give separate input & output buf lens */
	/* so making the assumption that input length can never be more than 1.5k */
	rqstlen = MIN(rqstlen, MSGBUF_MAX_MSG_SIZE);

#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */

	DHD_GENERAL_LOCK(dhd, flags);

	if (prot->ioctl_state) {
		DHD_ERROR(("%s: pending ioctl %02x\n", __FUNCTION__, prot->ioctl_state));
		DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef PCIE_INB_DW
		dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
		return BCME_BUSY;
	} else {
		prot->ioctl_state = MSGBUF_IOCTL_ACK_PENDING | MSGBUF_IOCTL_RESP_PENDING;
	}

	/* Request for cbuf space */
	ioct_rqst = (ioctl_req_msg_t*)
		dhd_prot_alloc_ring_space(dhd, ring, 1, &alloced, FALSE);
	if (ioct_rqst == NULL) {
		DHD_ERROR(("couldn't allocate space on msgring to send ioctl request\n"));
		prot->ioctl_state = 0;
		prot->curr_ioctl_cmd = 0;
		prot->ioctl_received = IOCTL_WAIT;
		DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef PCIE_INB_DW
		dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
		return -1;
	}

	/* Common msg buf hdr */
	ioct_rqst->cmn_hdr.msg_type = MSG_TYPE_IOCTLPTR_REQ;
	ioct_rqst->cmn_hdr.if_id = (uint8)ifidx;
	ioct_rqst->cmn_hdr.flags = ring->current_phase;
	ioct_rqst->cmn_hdr.request_id = htol32(DHD_IOCTL_REQ_PKTID);
	ioct_rqst->cmn_hdr.epoch = ring->seqnum % H2D_EPOCH_MODULO;
	ring->seqnum++;

	ioct_rqst->cmd = htol32(cmd);
	prot->curr_ioctl_cmd = cmd;
	ioct_rqst->output_buf_len = htol16(resplen);
	prot->ioctl_trans_id++;
	ioct_rqst->trans_id = prot->ioctl_trans_id;

	/* populate ioctl buffer info */
	ioct_rqst->input_buf_len = htol16(rqstlen);
	ioct_rqst->host_input_buf_addr.high = htol32(PHYSADDRHI(prot->ioctbuf.pa));
	ioct_rqst->host_input_buf_addr.low = htol32(PHYSADDRLO(prot->ioctbuf.pa));
	/* copy ioct payload */
	ioct_buf = (void *) prot->ioctbuf.va;

	if (buf)
		memcpy(ioct_buf, buf, len);

	OSL_CACHE_FLUSH((void *) prot->ioctbuf.va, len);

	if (!ISALIGNED(ioct_buf, DMA_ALIGN_LEN))
		DHD_ERROR(("host ioct address unaligned !!!!! \n"));

	DHD_CTL(("submitted IOCTL request request_id %d, cmd %d, output_buf_len %d, tx_id %d\n",
		ioct_rqst->cmn_hdr.request_id, cmd, ioct_rqst->output_buf_len,
		ioct_rqst->trans_id));

	/* update ring's WR index and ring doorbell to dongle */
	dhd_prot_ring_write_complete(dhd, ring, ioct_rqst, 1);
	DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif

	return 0;
} /* dhd_fillup_ioct_reqst */


/**
 * dhd_prot_ring_attach - Initialize the msgbuf_ring object and attach a
 * DMA-able buffer to it. The ring is NOT tagged as inited until all the ring
 * information is posted to the dongle.
 *
 * Invoked in dhd_prot_attach for the common rings, and in dhd_prot_init for
 * each flowring in pool of flowrings.
 *
 * returns BCME_OK=0 on success
 * returns non-zero negative error value on failure.
 */
static int
dhd_prot_ring_attach(dhd_pub_t *dhd, msgbuf_ring_t *ring, const char *name,
	uint16 max_items, uint16 item_len, uint16 ringid)
{
	int dma_buf_alloced = BCME_NOMEM;
	uint32 dma_buf_len = max_items * item_len;
	dhd_prot_t *prot = dhd->prot;
	uint16 max_flowrings = dhd->bus->max_tx_flowrings;

	ASSERT(ring);
	ASSERT(name);
	ASSERT((max_items < 0xFFFF) && (item_len < 0xFFFF) && (ringid < 0xFFFF));

	/* Init name */
	strncpy(ring->name, name, RING_NAME_MAX_LENGTH);
	ring->name[RING_NAME_MAX_LENGTH - 1] = '\0';

	ring->idx = ringid;

	ring->max_items = max_items;
	ring->item_len = item_len;

	/* A contiguous space may be reserved for all flowrings */
	if (DHD_IS_FLOWRING(ringid, max_flowrings) && (prot->flowrings_dma_buf.va)) {
		/* Carve out from the contiguous DMA-able flowring buffer */
		uint16 flowid;
		uint32 base_offset;

		dhd_dma_buf_t *dma_buf = &ring->dma_buf;
		dhd_dma_buf_t *rsv_buf = &prot->flowrings_dma_buf;

		flowid = DHD_RINGID_TO_FLOWID(ringid);
		base_offset = (flowid - BCMPCIE_H2D_COMMON_MSGRINGS) * dma_buf_len;

		ASSERT(base_offset + dma_buf_len <= rsv_buf->len);

		dma_buf->len = dma_buf_len;
		dma_buf->va = (void *)((uintptr)rsv_buf->va + base_offset);
		PHYSADDRHISET(dma_buf->pa, PHYSADDRHI(rsv_buf->pa));
		PHYSADDRLOSET(dma_buf->pa, PHYSADDRLO(rsv_buf->pa) + base_offset);

		/* On 64bit, contiguous space may not span across 0x00000000FFFFFFFF */
		ASSERT(PHYSADDRLO(dma_buf->pa) >= PHYSADDRLO(rsv_buf->pa));

		dma_buf->dmah   = rsv_buf->dmah;
		dma_buf->secdma = rsv_buf->secdma;

		(void)dhd_dma_buf_audit(dhd, &ring->dma_buf);
	} else {
		/* Allocate a dhd_dma_buf */
		dma_buf_alloced = dhd_dma_buf_alloc(dhd, &ring->dma_buf, dma_buf_len);
		if (dma_buf_alloced != BCME_OK) {
			return BCME_NOMEM;
		}
	}

	/* CAUTION: Save ring::base_addr in little endian format! */
	dhd_base_addr_htolpa(&ring->base_addr, ring->dma_buf.pa);

#ifdef BCM_SECURE_DMA
	if (SECURE_DMA_ENAB(prot->osh)) {
		ring->dma_buf.secdma = MALLOCZ(prot->osh, sizeof(sec_cma_info_t));
		if (ring->dma_buf.secdma == NULL) {
			goto free_dma_buf;
		}
	}
#endif /* BCM_SECURE_DMA */

	DHD_INFO(("RING_ATTACH : %s Max item %d len item %d total size %d "
		"ring start %p buf phys addr  %x:%x \n",
		ring->name, ring->max_items, ring->item_len,
		dma_buf_len, ring->dma_buf.va, ltoh32(ring->base_addr.high_addr),
		ltoh32(ring->base_addr.low_addr)));

	return BCME_OK;

#ifdef BCM_SECURE_DMA
free_dma_buf:
	if (dma_buf_alloced == BCME_OK) {
		dhd_dma_buf_free(dhd, &ring->dma_buf);
	}
#endif /* BCM_SECURE_DMA */

	return BCME_NOMEM;

} /* dhd_prot_ring_attach */


/**
 * dhd_prot_ring_init - Post the common ring information to dongle.
 *
 * Used only for common rings.
 *
 * The flowrings information is passed via the create flowring control message
 * (tx_flowring_create_request_t) sent over the H2D control submission common
 * ring.
 */
static void
dhd_prot_ring_init(dhd_pub_t *dhd, msgbuf_ring_t *ring)
{
	ring->wr = 0;
	ring->rd = 0;
	ring->curr_rd = 0;

	/* CAUTION: ring::base_addr already in Little Endian */
	dhd_bus_cmn_writeshared(dhd->bus, &ring->base_addr,
		sizeof(sh_addr_t), RING_BUF_ADDR, ring->idx);
	dhd_bus_cmn_writeshared(dhd->bus, &ring->max_items,
		sizeof(uint16), RING_MAX_ITEMS, ring->idx);
	dhd_bus_cmn_writeshared(dhd->bus, &ring->item_len,
		sizeof(uint16), RING_ITEM_LEN, ring->idx);

	dhd_bus_cmn_writeshared(dhd->bus, &(ring->wr),
		sizeof(uint16), RING_WR_UPD, ring->idx);
	dhd_bus_cmn_writeshared(dhd->bus, &(ring->rd),
		sizeof(uint16), RING_RD_UPD, ring->idx);

	/* ring inited */
	ring->inited = TRUE;

} /* dhd_prot_ring_init */


/**
 * dhd_prot_ring_reset - bzero a ring's DMA-ble buffer and cache flush
 * Reset WR and RD indices to 0.
 */
static void
dhd_prot_ring_reset(dhd_pub_t *dhd, msgbuf_ring_t *ring)
{
	DHD_TRACE(("%s\n", __FUNCTION__));

	dhd_dma_buf_reset(dhd, &ring->dma_buf);

	ring->rd = ring->wr = 0;
	ring->curr_rd = 0;
	ring->inited = FALSE;
	ring->create_pending = FALSE;
}


/**
 * dhd_prot_ring_detach - Detach the DMA-able buffer and any other objects
 * hanging off the msgbuf_ring.
 */
static void
dhd_prot_ring_detach(dhd_pub_t *dhd, msgbuf_ring_t *ring)
{
	dhd_prot_t *prot = dhd->prot;
	uint16 max_flowrings = dhd->bus->max_tx_flowrings;
	ASSERT(ring);

	ring->inited = FALSE;
	/* rd = ~0, wr = ring->rd - 1, max_items = 0, len_item = ~0 */

#ifdef BCM_SECURE_DMA
	if (SECURE_DMA_ENAB(prot->osh)) {
		if (ring->dma_buf.secdma) {
			SECURE_DMA_UNMAP_ALL(prot->osh, ring->dma_buf.secdma);
			MFREE(prot->osh, ring->dma_buf.secdma, sizeof(sec_cma_info_t));
			ring->dma_buf.secdma = NULL;
		}
	}
#endif /* BCM_SECURE_DMA */

	/* If the DMA-able buffer was carved out of a pre-reserved contiguous
	 * memory, then simply stop using it.
	 */
	if (DHD_IS_FLOWRING(ring->idx, max_flowrings) && (prot->flowrings_dma_buf.va)) {
		(void)dhd_dma_buf_audit(dhd, &ring->dma_buf);
		memset(&ring->dma_buf, 0, sizeof(dhd_dma_buf_t));
	} else {
		dhd_dma_buf_free(dhd, &ring->dma_buf);
	}

} /* dhd_prot_ring_detach */


/*
 * +----------------------------------------------------------------------------
 * Flowring Pool
 *
 * Unlike common rings, which are attached very early on (dhd_prot_attach),
 * flowrings are dynamically instantiated. Moreover, flowrings may require a
 * larger DMA-able buffer. To avoid issues with fragmented cache coherent
 * DMA-able memory, a pre-allocated pool of msgbuf_ring_t is allocated once.
 * The DMA-able buffers are attached to these pre-allocated msgbuf_ring.
 *
 * Each DMA-able buffer may be allocated independently, or may be carved out
 * of a single large contiguous region that is registered with the protocol
 * layer into flowrings_dma_buf. On a 64bit platform, this contiguous region
 * may not span 0x00000000FFFFFFFF (avoid dongle side 64bit ptr arithmetic).
 *
 * No flowring pool action is performed in dhd_prot_attach(), as the number
 * of h2d rings is not yet known.
 *
 * In dhd_prot_init(), the dongle advertized number of h2d rings is used to
 * determine the number of flowrings required, and a pool of msgbuf_rings are
 * allocated and a DMA-able buffer (carved or allocated) is attached.
 * See: dhd_prot_flowrings_pool_attach()
 *
 * A flowring msgbuf_ring object may be fetched from this pool during flowring
 * creation, using the flowid. Likewise, flowrings may be freed back into the
 * pool on flowring deletion.
 * See: dhd_prot_flowrings_pool_fetch(), dhd_prot_flowrings_pool_release()
 *
 * In dhd_prot_detach(), the flowring pool is detached. The DMA-able buffers
 * are detached (returned back to the carved region or freed), and the pool of
 * msgbuf_ring and any objects allocated against it are freed.
 * See: dhd_prot_flowrings_pool_detach()
 *
 * In dhd_prot_reset(), the flowring pool is simply reset by returning it to a
 * state as-if upon an attach. All DMA-able buffers are retained.
 * Following a dhd_prot_reset(), in a subsequent dhd_prot_init(), the flowring
 * pool attach will notice that the pool persists and continue to use it. This
 * will avoid the case of a fragmented DMA-able region.
 *
 * +----------------------------------------------------------------------------
 */

/* Conversion of a flowid to a flowring pool index */
#define DHD_FLOWRINGS_POOL_OFFSET(flowid) \
	((flowid) - BCMPCIE_H2D_COMMON_MSGRINGS)

/* Fetch the msgbuf_ring_t from the flowring pool given a flowid */
#define DHD_RING_IN_FLOWRINGS_POOL(prot, flowid) \
	(msgbuf_ring_t*)((prot)->h2d_flowrings_pool) + \
	    DHD_FLOWRINGS_POOL_OFFSET(flowid)

/* Traverse each flowring in the flowring pool, assigning ring and flowid */
#define FOREACH_RING_IN_FLOWRINGS_POOL(prot, ring, flowid, total_flowrings) \
	for ((flowid) = DHD_FLOWRING_START_FLOWID, \
		(ring) = DHD_RING_IN_FLOWRINGS_POOL(prot, flowid); \
		 (flowid) < ((total_flowrings) + DHD_FLOWRING_START_FLOWID); \
		 (ring)++, (flowid)++)

/* Fetch number of H2D flowrings given the total number of h2d rings */
static uint16
dhd_get_max_flow_rings(dhd_pub_t *dhd)
{
	if (dhd->bus->api.fw_rev >= PCIE_SHARED_VERSION_6)
		return dhd->bus->max_tx_flowrings;
	else
		return (dhd->bus->max_tx_flowrings - BCMPCIE_H2D_COMMON_MSGRINGS);
}

/**
 * dhd_prot_flowrings_pool_attach - Initialize a pool of flowring msgbuf_ring_t.
 *
 * Allocate a pool of msgbuf_ring along with DMA-able buffers for flowrings.
 * Dongle includes common rings when it advertizes the number of H2D rings.
 * Allocates a pool of msgbuf_ring_t and invokes dhd_prot_ring_attach to
 * allocate the DMA-able buffer and initialize each msgbuf_ring_t object.
 *
 * dhd_prot_ring_attach is invoked to perform the actual initialization and
 * attaching the DMA-able buffer.
 *
 * Later dhd_prot_flowrings_pool_fetch() may be used to fetch a preallocated and
 * initialized msgbuf_ring_t object.
 *
 * returns BCME_OK=0 on success
 * returns non-zero negative error value on failure.
 */
static int
dhd_prot_flowrings_pool_attach(dhd_pub_t *dhd)
{
	uint16 flowid;
	msgbuf_ring_t *ring;
	uint16 h2d_flowrings_total; /* exclude H2D common rings */
	dhd_prot_t *prot = dhd->prot;
	char ring_name[RING_NAME_MAX_LENGTH];

	if (prot->h2d_flowrings_pool != NULL)
		return BCME_OK; /* dhd_prot_init rentry after a dhd_prot_reset */

	ASSERT(prot->h2d_rings_total == 0);

	/* h2d_rings_total includes H2D common rings: ctrl and rxbuf subn */
	prot->h2d_rings_total = (uint16)dhd_bus_max_h2d_queues(dhd->bus);

	if (prot->h2d_rings_total < BCMPCIE_H2D_COMMON_MSGRINGS) {
		DHD_ERROR(("%s: h2d_rings_total advertized as %u\n",
			__FUNCTION__, prot->h2d_rings_total));
		return BCME_ERROR;
	}

	/* Subtract number of H2D common rings, to determine number of flowrings */
	h2d_flowrings_total = dhd_get_max_flow_rings(dhd);

	DHD_ERROR(("Attach flowrings pool for %d rings\n", h2d_flowrings_total));

	/* Allocate pool of msgbuf_ring_t objects for all flowrings */
	prot->h2d_flowrings_pool = (msgbuf_ring_t *)MALLOCZ(prot->osh,
		(h2d_flowrings_total * sizeof(msgbuf_ring_t)));

	if (prot->h2d_flowrings_pool == NULL) {
		DHD_ERROR(("%s: flowrings pool for %d flowrings, alloc failure\n",
			__FUNCTION__, h2d_flowrings_total));
		goto fail;
	}

	/* Setup & Attach a DMA-able buffer to each flowring in the flowring pool */
	FOREACH_RING_IN_FLOWRINGS_POOL(prot, ring, flowid, h2d_flowrings_total) {
		snprintf(ring_name, sizeof(ring_name), "h2dflr_%03u", flowid);
		if (dhd_prot_ring_attach(dhd, ring, ring_name,
		        prot->h2d_max_txpost, H2DRING_TXPOST_ITEMSIZE,
		        DHD_FLOWID_TO_RINGID(flowid)) != BCME_OK) {
			goto attach_fail;
		}
	}

	return BCME_OK;

attach_fail:
	dhd_prot_flowrings_pool_detach(dhd); /* Free entire pool of flowrings */

fail:
	prot->h2d_rings_total = 0;
	return BCME_NOMEM;

} /* dhd_prot_flowrings_pool_attach */


/**
 * dhd_prot_flowrings_pool_reset - Reset all msgbuf_ring_t objects in the pool.
 * Invokes dhd_prot_ring_reset to perform the actual reset.
 *
 * The DMA-able buffer is not freed during reset and neither is the flowring
 * pool freed.
 *
 * dhd_prot_flowrings_pool_reset will be invoked in dhd_prot_reset. Following
 * the dhd_prot_reset, dhd_prot_init will be re-invoked, and the flowring pool
 * from a previous flowring pool instantiation will be reused.
 *
 * This will avoid a fragmented DMA-able memory condition, if multiple
 * dhd_prot_reset were invoked to reboot the dongle without a full detach/attach
 * cycle.
 */
static void
dhd_prot_flowrings_pool_reset(dhd_pub_t *dhd)
{
	uint16 flowid, h2d_flowrings_total;
	msgbuf_ring_t *ring;
	dhd_prot_t *prot = dhd->prot;

	if (prot->h2d_flowrings_pool == NULL) {
		ASSERT(prot->h2d_rings_total == 0);
		return;
	}
	h2d_flowrings_total = dhd_get_max_flow_rings(dhd);
	/* Reset each flowring in the flowring pool */
	FOREACH_RING_IN_FLOWRINGS_POOL(prot, ring, flowid, h2d_flowrings_total) {
		dhd_prot_ring_reset(dhd, ring);
		ring->inited = FALSE;
	}

	/* Flowring pool state must be as-if dhd_prot_flowrings_pool_attach */
}


/**
 * dhd_prot_flowrings_pool_detach - Free pool of msgbuf_ring along with
 * DMA-able buffers for flowrings.
 * dhd_prot_ring_detach is invoked to free the DMA-able buffer and perform any
 * de-initialization of each msgbuf_ring_t.
 */
static void
dhd_prot_flowrings_pool_detach(dhd_pub_t *dhd)
{
	int flowid;
	msgbuf_ring_t *ring;
	uint16 h2d_flowrings_total; /* exclude H2D common rings */
	dhd_prot_t *prot = dhd->prot;

	if (prot->h2d_flowrings_pool == NULL) {
		ASSERT(prot->h2d_rings_total == 0);
		return;
	}

	h2d_flowrings_total = dhd_get_max_flow_rings(dhd);
	/* Detach the DMA-able buffer for each flowring in the flowring pool */
	FOREACH_RING_IN_FLOWRINGS_POOL(prot, ring, flowid, h2d_flowrings_total) {
		dhd_prot_ring_detach(dhd, ring);
	}


	MFREE(prot->osh, prot->h2d_flowrings_pool,
		(h2d_flowrings_total * sizeof(msgbuf_ring_t)));

	prot->h2d_flowrings_pool = (msgbuf_ring_t*)NULL;
	prot->h2d_rings_total = 0;

} /* dhd_prot_flowrings_pool_detach */


/**
 * dhd_prot_flowrings_pool_fetch - Fetch a preallocated and initialized
 * msgbuf_ring from the flowring pool, and assign it.
 *
 * Unlike common rings, which uses a dhd_prot_ring_init() to pass the common
 * ring information to the dongle, a flowring's information is passed via a
 * flowring create control message.
 *
 * Only the ring state (WR, RD) index are initialized.
 */
static msgbuf_ring_t *
dhd_prot_flowrings_pool_fetch(dhd_pub_t *dhd, uint16 flowid)
{
	msgbuf_ring_t *ring;
	dhd_prot_t *prot = dhd->prot;

	ASSERT(flowid >= DHD_FLOWRING_START_FLOWID);
	ASSERT(flowid < prot->h2d_rings_total);
	ASSERT(prot->h2d_flowrings_pool != NULL);

	ring = DHD_RING_IN_FLOWRINGS_POOL(prot, flowid);

	/* ASSERT flow_ring->inited == FALSE */

	ring->wr = 0;
	ring->rd = 0;
	ring->curr_rd = 0;
	ring->inited = TRUE;
	/**
	 * Every time a flowring starts dynamically, initialize current_phase with 0
	 * then flip to BCMPCIE_CMNHDR_PHASE_BIT_INIT
	 */
	ring->current_phase = 0;
	return ring;
}


/**
 * dhd_prot_flowrings_pool_release - release a previously fetched flowring's
 * msgbuf_ring back to the flow_ring pool.
 */
void
dhd_prot_flowrings_pool_release(dhd_pub_t *dhd, uint16 flowid, void *flow_ring)
{
	msgbuf_ring_t *ring;
	dhd_prot_t *prot = dhd->prot;

	ASSERT(flowid >= DHD_FLOWRING_START_FLOWID);
	ASSERT(flowid < prot->h2d_rings_total);
	ASSERT(prot->h2d_flowrings_pool != NULL);

	ring = DHD_RING_IN_FLOWRINGS_POOL(prot, flowid);

	ASSERT(ring == (msgbuf_ring_t*)flow_ring);
	/* ASSERT flow_ring->inited == TRUE */

	(void)dhd_dma_buf_audit(dhd, &ring->dma_buf);

	ring->wr = 0;
	ring->rd = 0;
	ring->inited = FALSE;

	ring->curr_rd = 0;
}


/* Assumes only one index is updated at a time */
/* If exactly_nitems is true, this function will allocate space for nitems or fail */
/*    Exception: when wrap around is encountered, to prevent hangup (last nitems of ring buffer) */
/* If exactly_nitems is false, this function will allocate space for nitems or less */
static void *BCMFASTPATH
dhd_prot_get_ring_space(msgbuf_ring_t *ring, uint16 nitems, uint16 * alloced,
	bool exactly_nitems)
{
	void *ret_ptr = NULL;
	uint16 ring_avail_cnt;

	ASSERT(nitems <= ring->max_items);

	ring_avail_cnt = CHECK_WRITE_SPACE(ring->rd, ring->wr, ring->max_items);

	if ((ring_avail_cnt == 0) ||
	       (exactly_nitems && (ring_avail_cnt < nitems) &&
	       ((ring->max_items - ring->wr) >= nitems))) {
		DHD_INFO(("Space not available: ring %s items %d write %d read %d\n",
			ring->name, nitems, ring->wr, ring->rd));
		return NULL;
	}
	*alloced = MIN(nitems, ring_avail_cnt);

	/* Return next available space */
	ret_ptr = (char *)DHD_RING_BGN_VA(ring) + (ring->wr * ring->item_len);

	/* Update write index */
	if ((ring->wr + *alloced) == ring->max_items)
		ring->wr = 0;
	else if ((ring->wr + *alloced) < ring->max_items)
		ring->wr += *alloced;
	else {
		/* Should never hit this */
		ASSERT(0);
		return NULL;
	}

	return ret_ptr;
} /* dhd_prot_get_ring_space */


/**
 * dhd_prot_ring_write_complete - Host updates the new WR index on producing
 * new messages in a H2D ring. The messages are flushed from cache prior to
 * posting the new WR index. The new WR index will be updated in the DMA index
 * array or directly in the dongle's ring state memory.
 * A PCIE doorbell will be generated to wake up the dongle.
 * This is a non-atomic function, make sure the callers
 * always hold appropriate locks.
 */
static void BCMFASTPATH
dhd_prot_ring_write_complete(dhd_pub_t *dhd, msgbuf_ring_t * ring, void* p,
	uint16 nitems)
{
	dhd_prot_t *prot = dhd->prot;
	uint8 db_index;
	uint16 max_flowrings = dhd->bus->max_tx_flowrings;

	/* cache flush */
	OSL_CACHE_FLUSH(p, ring->item_len * nitems);

	if (IDMA_DS_ACTIVE(dhd) && IDMA_ACTIVE(dhd)) {
		dhd_bus_cmn_writeshared(dhd->bus, &(ring->wr),
			sizeof(uint16), RING_WR_UPD, ring->idx);
	} else if (IDMA_ACTIVE(dhd) || dhd->dma_h2d_ring_upd_support) {
			dhd_prot_dma_indx_set(dhd, ring->wr,
			                      H2D_DMA_INDX_WR_UPD, ring->idx);
	} else if (IFRM_ACTIVE(dhd) && DHD_IS_FLOWRING(ring->idx, max_flowrings)) {
			dhd_prot_dma_indx_set(dhd, ring->wr,
			H2D_IFRM_INDX_WR_UPD, ring->idx);
	} else {
			dhd_bus_cmn_writeshared(dhd->bus, &(ring->wr),
				sizeof(uint16), RING_WR_UPD, ring->idx);
	}

	/* raise h2d interrupt */
	if (IDMA_ACTIVE(dhd) ||
		(IFRM_ACTIVE(dhd) && DHD_IS_FLOWRING(ring->idx, max_flowrings))) {
		if (IDMA_DS_ACTIVE(dhd)) {
			prot->mb_ring_fn(dhd->bus, ring->wr);
		} else {
			db_index = IDMA_IDX0;
			prot->mb_2_ring_fn(dhd->bus, db_index, TRUE);
		}
	} else {
		prot->mb_ring_fn(dhd->bus, ring->wr);
	}
}

/**
 * dhd_prot_upd_read_idx - Host updates the new RD index on consuming messages
 * from a D2H ring. The new RD index will be updated in the DMA Index array or
 * directly in dongle's ring state memory.
 */
static void
dhd_prot_upd_read_idx(dhd_pub_t *dhd, msgbuf_ring_t * ring)
{
	dhd_prot_t *prot = dhd->prot;
	uint8 db_index;

	/* update read index */
	/* If dma'ing h2d indices supported
	 * update the r -indices in the
	 * host memory o/w in TCM
	 */
	if (IDMA_ACTIVE(dhd)) {
		dhd_prot_dma_indx_set(dhd, ring->rd,
		                      D2H_DMA_INDX_RD_UPD, ring->idx);
		if (IDMA_DS_ACTIVE(dhd)) {
			dhd_bus_cmn_writeshared(dhd->bus, &(ring->rd),
				sizeof(uint16), RING_RD_UPD, ring->idx);
		} else {
			db_index = IDMA_IDX1;
			prot->mb_2_ring_fn(dhd->bus, db_index, FALSE);
		}
	} else if (dhd->dma_h2d_ring_upd_support) {
		dhd_prot_dma_indx_set(dhd, ring->rd,
		                      D2H_DMA_INDX_RD_UPD, ring->idx);
	} else {
		dhd_bus_cmn_writeshared(dhd->bus, &(ring->rd),
			sizeof(uint16), RING_RD_UPD, ring->idx);
	}
}

static int
dhd_send_d2h_ringcreate(dhd_pub_t *dhd, msgbuf_ring_t *ring_to_create)
{
	unsigned long flags;
	d2h_ring_create_req_t  *d2h_ring;
	uint16 alloced = 0;
	int ret = BCME_OK;
	uint16 max_h2d_rings = dhd->bus->max_submission_rings;

#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */
	DHD_GENERAL_LOCK(dhd, flags);

	DHD_TRACE(("%s trying to send D2H ring create Req\n", __FUNCTION__));

	if (ring_to_create == NULL) {
		DHD_ERROR(("%s: FATAL: ring_to_create is NULL\n", __FUNCTION__));
		ret = BCME_ERROR;
		goto err;
	}

	/* Request for ring buffer space */
	d2h_ring = (d2h_ring_create_req_t *) dhd_prot_alloc_ring_space(dhd,
		&dhd->prot->h2dring_ctrl_subn, DHD_FLOWRING_DEFAULT_NITEMS_POSTED_H2D,
		&alloced, FALSE);

	if (d2h_ring == NULL) {
		DHD_ERROR(("%s: FATAL: No space in control ring to send D2H ring create\n",
			__FUNCTION__));
		ret = BCME_NOMEM;
		goto err;
	}
	ring_to_create->create_req_id = DHD_D2H_DBGRING_REQ_PKTID;
	ring_to_create->create_pending = TRUE;

	/* Common msg buf hdr */
	d2h_ring->msg.msg_type = MSG_TYPE_D2H_RING_CREATE;
	d2h_ring->msg.if_id = 0;
	d2h_ring->msg.flags = dhd->prot->h2dring_ctrl_subn.current_phase;
	d2h_ring->msg.request_id = htol32(ring_to_create->create_req_id);
	d2h_ring->ring_id = htol16(DHD_D2H_RING_OFFSET(ring_to_create->idx, max_h2d_rings));
	d2h_ring->ring_type = BCMPCIE_D2H_RING_TYPE_DBGBUF_CPL;
	d2h_ring->max_items = htol16(D2HRING_DYNAMIC_INFO_MAX_ITEM);
	d2h_ring->len_item = htol16(D2HRING_INFO_BUFCMPLT_ITEMSIZE);
	d2h_ring->ring_ptr.low_addr = ring_to_create->base_addr.low_addr;
	d2h_ring->ring_ptr.high_addr = ring_to_create->base_addr.high_addr;

	d2h_ring->flags = 0;
	d2h_ring->msg.epoch =
		dhd->prot->h2dring_ctrl_subn.seqnum % H2D_EPOCH_MODULO;
	dhd->prot->h2dring_ctrl_subn.seqnum++;

	/* Update the flow_ring's WRITE index */
	dhd_prot_ring_write_complete(dhd, &dhd->prot->h2dring_ctrl_subn, d2h_ring,
		DHD_FLOWRING_DEFAULT_NITEMS_POSTED_H2D);

err:
	DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
	return ret;
}

static int
dhd_send_h2d_ringcreate(dhd_pub_t *dhd, msgbuf_ring_t *ring_to_create)
{
	unsigned long flags;
	h2d_ring_create_req_t  *h2d_ring;
	uint16 alloced = 0;
	uint8 i = 0;
	int ret = BCME_OK;


#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */
	DHD_GENERAL_LOCK(dhd, flags);

	DHD_TRACE(("%s trying to send H2D ring create Req\n", __FUNCTION__));

	if (ring_to_create == NULL) {
		DHD_ERROR(("%s: FATAL: ring_to_create is NULL\n", __FUNCTION__));
		ret = BCME_ERROR;
		goto err;
	}

	/* Request for ring buffer space */
	h2d_ring = (h2d_ring_create_req_t *)dhd_prot_alloc_ring_space(dhd,
		&dhd->prot->h2dring_ctrl_subn, DHD_FLOWRING_DEFAULT_NITEMS_POSTED_H2D,
		&alloced, FALSE);

	if (h2d_ring == NULL) {
		DHD_ERROR(("%s: FATAL: No space in control ring to send H2D ring create\n",
			__FUNCTION__));
		ret = BCME_NOMEM;
		goto err;
	}
	ring_to_create->create_req_id = DHD_H2D_DBGRING_REQ_PKTID;
	ring_to_create->create_pending = TRUE;

	/* Common msg buf hdr */
	h2d_ring->msg.msg_type = MSG_TYPE_H2D_RING_CREATE;
	h2d_ring->msg.if_id = 0;
	h2d_ring->msg.request_id = htol32(ring_to_create->create_req_id);
	h2d_ring->msg.flags = dhd->prot->h2dring_ctrl_subn.current_phase;
	h2d_ring->ring_id = htol16(DHD_H2D_RING_OFFSET(ring_to_create->idx));
	h2d_ring->ring_type = BCMPCIE_H2D_RING_TYPE_DBGBUF_SUBMIT;
	h2d_ring->max_items = htol16(H2DRING_DYNAMIC_INFO_MAX_ITEM);
	h2d_ring->n_completion_ids = ring_to_create->n_completion_ids;
	h2d_ring->len_item = htol16(H2DRING_INFO_BUFPOST_ITEMSIZE);
	h2d_ring->ring_ptr.low_addr = ring_to_create->base_addr.low_addr;
	h2d_ring->ring_ptr.high_addr = ring_to_create->base_addr.high_addr;

	for (i = 0; i < ring_to_create->n_completion_ids; i++) {
		h2d_ring->completion_ring_ids[i] = htol16(ring_to_create->compeltion_ring_ids[i]);
	}

	h2d_ring->flags = 0;
	h2d_ring->msg.epoch =
		dhd->prot->h2dring_ctrl_subn.seqnum % H2D_EPOCH_MODULO;
	dhd->prot->h2dring_ctrl_subn.seqnum++;

	/* Update the flow_ring's WRITE index */
	dhd_prot_ring_write_complete(dhd, &dhd->prot->h2dring_ctrl_subn, h2d_ring,
		DHD_FLOWRING_DEFAULT_NITEMS_POSTED_H2D);

err:
	DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
	return ret;
}

/**
 * dhd_prot_dma_indx_set - set a new WR or RD index in the DMA index array.
 * Dongle will DMA the entire array (if DMA_INDX feature is enabled).
 * See dhd_prot_dma_indx_init()
 */
void
dhd_prot_dma_indx_set(dhd_pub_t *dhd, uint16 new_index, uint8 type, uint16 ringid)
{
	uint8 *ptr;
	uint16 offset;
	dhd_prot_t *prot = dhd->prot;
	uint16 max_h2d_rings = dhd->bus->max_submission_rings;

	switch (type) {
		case H2D_DMA_INDX_WR_UPD:
			ptr = (uint8 *)(prot->h2d_dma_indx_wr_buf.va);
			offset = DHD_H2D_RING_OFFSET(ringid);
			break;

		case D2H_DMA_INDX_RD_UPD:
			ptr = (uint8 *)(prot->d2h_dma_indx_rd_buf.va);
			offset = DHD_D2H_RING_OFFSET(ringid, max_h2d_rings);
			break;

		case H2D_IFRM_INDX_WR_UPD:
			ptr = (uint8 *)(prot->h2d_ifrm_indx_wr_buf.va);
			offset = DHD_H2D_FRM_FLOW_RING_OFFSET(ringid);
			break;

		default:
			DHD_ERROR(("%s: Invalid option for DMAing read/write index\n",
				__FUNCTION__));
			return;
	}

	ASSERT(prot->rw_index_sz != 0);
	ptr += offset * prot->rw_index_sz;

	*(uint16*)ptr = htol16(new_index);

	OSL_CACHE_FLUSH((void *)ptr, prot->rw_index_sz);

	DHD_TRACE(("%s: data %d type %d ringid %d ptr 0x%p offset %d\n",
		__FUNCTION__, new_index, type, ringid, ptr, offset));

} /* dhd_prot_dma_indx_set */


/**
 * dhd_prot_dma_indx_get - Fetch a WR or RD index from the dongle DMA-ed index
 * array.
 * Dongle DMAes an entire array to host memory (if the feature is enabled).
 * See dhd_prot_dma_indx_init()
 */
static uint16
dhd_prot_dma_indx_get(dhd_pub_t *dhd, uint8 type, uint16 ringid)
{
	uint8 *ptr;
	uint16 data;
	uint16 offset;
	dhd_prot_t *prot = dhd->prot;
	uint16 max_h2d_rings = dhd->bus->max_submission_rings;

	switch (type) {
		case H2D_DMA_INDX_WR_UPD:
			ptr = (uint8 *)(prot->h2d_dma_indx_wr_buf.va);
			offset = DHD_H2D_RING_OFFSET(ringid);
			break;

		case H2D_DMA_INDX_RD_UPD:
			ptr = (uint8 *)(prot->h2d_dma_indx_rd_buf.va);
			offset = DHD_H2D_RING_OFFSET(ringid);
			break;

		case D2H_DMA_INDX_WR_UPD:
			ptr = (uint8 *)(prot->d2h_dma_indx_wr_buf.va);
			offset = DHD_D2H_RING_OFFSET(ringid, max_h2d_rings);
			break;

		case D2H_DMA_INDX_RD_UPD:
			ptr = (uint8 *)(prot->d2h_dma_indx_rd_buf.va);
			offset = DHD_D2H_RING_OFFSET(ringid, max_h2d_rings);
			break;

		default:
			DHD_ERROR(("%s: Invalid option for DMAing read/write index\n",
				__FUNCTION__));
			return 0;
	}

	ASSERT(prot->rw_index_sz != 0);
	ptr += offset * prot->rw_index_sz;

	OSL_CACHE_INV((void *)ptr, prot->rw_index_sz);

	data = LTOH16(*((uint16*)ptr));

	DHD_TRACE(("%s: data %d type %d ringid %d ptr 0x%p offset %d\n",
		__FUNCTION__, data, type, ringid, ptr, offset));

	return (data);

} /* dhd_prot_dma_indx_get */

/**
 * An array of DMA read/write indices, containing information about host rings, can be maintained
 * either in host memory or in device memory, dependent on preprocessor options. This function is,
 * dependent on these options, called during driver initialization. It reserves and initializes
 * blocks of DMA'able host memory containing an array of DMA read or DMA write indices. The physical
 * address of these host memory blocks are communicated to the dongle later on. By reading this host
 * memory, the dongle learns about the state of the host rings.
 */

static INLINE int
dhd_prot_dma_indx_alloc(dhd_pub_t *dhd, uint8 type,
	dhd_dma_buf_t *dma_buf, uint32 bufsz)
{
	int rc;

	if ((dma_buf->len == bufsz) || (dma_buf->va != NULL))
		return BCME_OK;

	rc = dhd_dma_buf_alloc(dhd, dma_buf, bufsz);

	return rc;
}

int
dhd_prot_dma_indx_init(dhd_pub_t *dhd, uint32 rw_index_sz, uint8 type, uint32 length)
{
	uint32 bufsz;
	dhd_prot_t *prot = dhd->prot;
	dhd_dma_buf_t *dma_buf;

	if (prot == NULL) {
		DHD_ERROR(("prot is not inited\n"));
		return BCME_ERROR;
	}

	/* Dongle advertizes 2B or 4B RW index size */
	ASSERT(rw_index_sz != 0);
	prot->rw_index_sz = rw_index_sz;

	bufsz = rw_index_sz * length;

	switch (type) {
		case H2D_DMA_INDX_WR_BUF:
			dma_buf = &prot->h2d_dma_indx_wr_buf;
			if (dhd_prot_dma_indx_alloc(dhd, type, dma_buf, bufsz))
				goto ret_no_mem;
			DHD_ERROR(("H2D DMA WR INDX : array size %d = %d * %d\n",
				dma_buf->len, rw_index_sz, length));
			break;

		case H2D_DMA_INDX_RD_BUF:
			dma_buf = &prot->h2d_dma_indx_rd_buf;
			if (dhd_prot_dma_indx_alloc(dhd, type, dma_buf, bufsz))
				goto ret_no_mem;
			DHD_ERROR(("H2D DMA RD INDX : array size %d = %d * %d\n",
				dma_buf->len, rw_index_sz, length));
			break;

		case D2H_DMA_INDX_WR_BUF:
			dma_buf = &prot->d2h_dma_indx_wr_buf;
			if (dhd_prot_dma_indx_alloc(dhd, type, dma_buf, bufsz))
				goto ret_no_mem;
			DHD_ERROR(("D2H DMA WR INDX : array size %d = %d * %d\n",
				dma_buf->len, rw_index_sz, length));
			break;

		case D2H_DMA_INDX_RD_BUF:
			dma_buf = &prot->d2h_dma_indx_rd_buf;
			if (dhd_prot_dma_indx_alloc(dhd, type, dma_buf, bufsz))
				goto ret_no_mem;
			DHD_ERROR(("D2H DMA RD INDX : array size %d = %d * %d\n",
				dma_buf->len, rw_index_sz, length));
			break;

		case H2D_IFRM_INDX_WR_BUF:
			dma_buf = &prot->h2d_ifrm_indx_wr_buf;
			if (dhd_prot_dma_indx_alloc(dhd, type, dma_buf, bufsz))
				goto ret_no_mem;
			DHD_ERROR(("H2D IFRM WR INDX : array size %d = %d * %d\n",
				dma_buf->len, rw_index_sz, length));
			break;

		default:
			DHD_ERROR(("%s: Unexpected option\n", __FUNCTION__));
			return BCME_BADOPTION;
	}

	return BCME_OK;

ret_no_mem:
	DHD_ERROR(("%s: dhd_prot_dma_indx_alloc type %d buf_sz %d failure\n",
		__FUNCTION__, type, bufsz));
	return BCME_NOMEM;

} /* dhd_prot_dma_indx_init */


/**
 * Called on checking for 'completion' messages from the dongle. Returns next host buffer to read
 * from, or NULL if there are no more messages to read.
 */
static uint8*
dhd_prot_get_read_addr(dhd_pub_t *dhd, msgbuf_ring_t *ring, uint32 *available_len)
{
	uint16 wr;
	uint16 rd;
	uint16 depth;
	uint16 items;
	void  *read_addr = NULL; /* address of next msg to be read in ring */
	uint16 d2h_wr = 0;

	DHD_TRACE(("%s: d2h_dma_indx_rd_buf %p, d2h_dma_indx_wr_buf %p\n",
		__FUNCTION__, (uint32 *)(dhd->prot->d2h_dma_indx_rd_buf.va),
		(uint32 *)(dhd->prot->d2h_dma_indx_wr_buf.va)));

	/* Remember the read index in a variable.
	 * This is becuase ring->rd gets updated in the end of this function
	 * So if we have to print the exact read index from which the
	 * message is read its not possible.
	 */
	ring->curr_rd = ring->rd;

	/* update write pointer */
	if (dhd->dma_d2h_ring_upd_support) {
		/* DMAing write/read indices supported */
		d2h_wr = dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_WR_UPD, ring->idx);
		ring->wr = d2h_wr;
	} else {
		dhd_bus_cmn_readshared(dhd->bus, &(ring->wr), RING_WR_UPD, ring->idx);
	}

	wr = ring->wr;
	rd = ring->rd;
	depth = ring->max_items;

	/* check for avail space, in number of ring items */
	items = READ_AVAIL_SPACE(wr, rd, depth);
	if (items == 0)
		return NULL;

	/*
	 * Note that there are builds where Assert translates to just printk
	 * so, even if we had hit this condition we would never halt. Now
	 * dhd_prot_process_msgtype can get into an big loop if this
	 * happens.
	 */
	if (items > ring->max_items) {
		DHD_ERROR(("\r\n======================= \r\n"));
		DHD_ERROR(("%s(): ring %p, ring->name %s, ring->max_items %d, items %d \r\n",
			__FUNCTION__, ring, ring->name, ring->max_items, items));
		DHD_ERROR(("wr: %d,  rd: %d,  depth: %d  \r\n", wr, rd, depth));
		DHD_ERROR(("dhd->busstate %d bus->wait_for_d3_ack %d \r\n",
			dhd->busstate, dhd->bus->wait_for_d3_ack));
		DHD_ERROR(("\r\n======================= \r\n"));
#ifdef SUPPORT_LINKDOWN_RECOVERY
		if (wr >= ring->max_items) {
			dhd->bus->read_shm_fail = TRUE;
		}
#else
#ifdef DHD_FW_COREDUMP
		if (dhd->memdump_enabled) {
			/* collect core dump */
			dhd->memdump_type = DUMP_TYPE_RESUMED_ON_INVALID_RING_RDWR;
			dhd_bus_mem_dump(dhd);

		}
#endif /* DHD_FW_COREDUMP */
#endif /* SUPPORT_LINKDOWN_RECOVERY */

		*available_len = 0;
		dhd_schedule_reset(dhd);

		return NULL;
	}

	/* if space is available, calculate address to be read */
	read_addr = (char*)ring->dma_buf.va + (rd * ring->item_len);

	/* update read pointer */
	if ((ring->rd + items) >= ring->max_items)
		ring->rd = 0;
	else
		ring->rd += items;

	ASSERT(ring->rd < ring->max_items);

	/* convert items to bytes : available_len must be 32bits */
	*available_len = (uint32)(items * ring->item_len);

	OSL_CACHE_INV(read_addr, *available_len);

	/* return read address */
	return read_addr;

} /* dhd_prot_get_read_addr */

/**
 * dhd_prot_h2d_mbdata_send_ctrlmsg is a non-atomic function,
 * make sure the callers always hold appropriate locks.
 */
int dhd_prot_h2d_mbdata_send_ctrlmsg(dhd_pub_t *dhd, uint32 mb_data)
{
	h2d_mailbox_data_t *h2d_mb_data;
	uint16 alloced = 0;
	int num_post = 1;
	int i;

	DHD_INFO(("%s Sending H2D MB data Req data 0x%04x\n",
		__FUNCTION__, mb_data));
	if (!dhd->prot->h2dring_ctrl_subn.inited) {
		DHD_ERROR(("%s: Ctrl Submit Ring: not inited\n", __FUNCTION__));
		return BCME_ERROR;
	}
#ifdef PCIE_INB_DW
	if ((INBAND_DW_ENAB(dhd->bus)) &&
		(dhdpcie_bus_get_pcie_inband_dw_state(dhd->bus) ==
			DW_DEVICE_DS_DEV_SLEEP)) {
		if (mb_data == H2D_HOST_CONS_INT) {
			/* One additional device_wake post needed */
			num_post = 2;
		}
	}
#endif /* PCIE_INB_DW */

	for (i = 0; i < num_post; i ++) {
		/* Request for ring buffer space */
		h2d_mb_data = (h2d_mailbox_data_t *)dhd_prot_alloc_ring_space(dhd,
			&dhd->prot->h2dring_ctrl_subn, DHD_FLOWRING_DEFAULT_NITEMS_POSTED_H2D,
			&alloced, FALSE);

		if (h2d_mb_data == NULL) {
			DHD_ERROR(("%s: FATAL: No space in control ring to send H2D Mb data\n",
				__FUNCTION__));
			return BCME_NOMEM;
		}

		memset(h2d_mb_data, 0, sizeof(h2d_mailbox_data_t));
	/* Common msg buf hdr */
		h2d_mb_data->msg.msg_type = MSG_TYPE_H2D_MAILBOX_DATA;
		h2d_mb_data->msg.flags = dhd->prot->h2dring_ctrl_subn.current_phase;

		h2d_mb_data->msg.epoch =
			dhd->prot->h2dring_ctrl_subn.seqnum % H2D_EPOCH_MODULO;
		dhd->prot->h2dring_ctrl_subn.seqnum++;

#ifdef PCIE_INB_DW
		/* post device_wake first */
		if ((num_post == 2) && (i == 0)) {
			h2d_mb_data->mail_box_data = htol32(H2DMB_DS_DEVICE_WAKE);
		} else
#endif /* PCIE_INB_DW */
		{
			h2d_mb_data->mail_box_data = htol32(mb_data);
		}

		DHD_INFO(("%s Send H2D MB data Req data 0x%04x\n", __FUNCTION__, mb_data));

		/* upd wrt ptr and raise interrupt */
		/* caller of dhd_prot_h2d_mbdata_send_ctrlmsg already holding general lock */
		dhd_prot_ring_write_complete(dhd, &dhd->prot->h2dring_ctrl_subn, h2d_mb_data,
			DHD_FLOWRING_DEFAULT_NITEMS_POSTED_H2D);
#ifdef PCIE_INB_DW
		/* Add a delay if device_wake is posted */
		if ((num_post == 2) && (i == 0)) {
			OSL_DELAY(1000);
		}
#endif /* PCIE_INB_DW */
	}

	return 0;
}

/** Creates a flow ring and informs dongle of this event */
int
dhd_prot_flow_ring_create(dhd_pub_t *dhd, flow_ring_node_t *flow_ring_node)
{
	tx_flowring_create_request_t *flow_create_rqst;
	msgbuf_ring_t *flow_ring;
	dhd_prot_t *prot = dhd->prot;
	unsigned long flags;
	uint16 alloced = 0;
	msgbuf_ring_t *ctrl_ring = &prot->h2dring_ctrl_subn;
	uint16 max_flowrings = dhd->bus->max_tx_flowrings;

	/* Fetch a pre-initialized msgbuf_ring from the flowring pool */
	flow_ring = dhd_prot_flowrings_pool_fetch(dhd, flow_ring_node->flowid);
	if (flow_ring == NULL) {
		DHD_ERROR(("%s: dhd_prot_flowrings_pool_fetch TX Flowid %d failed\n",
			__FUNCTION__, flow_ring_node->flowid));
		return BCME_NOMEM;
	}

#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */
	DHD_GENERAL_LOCK(dhd, flags);

	/* Request for ctrl_ring buffer space */
	flow_create_rqst = (tx_flowring_create_request_t *)
		dhd_prot_alloc_ring_space(dhd, ctrl_ring, 1, &alloced, FALSE);

	if (flow_create_rqst == NULL) {
		dhd_prot_flowrings_pool_release(dhd, flow_ring_node->flowid, flow_ring);
		DHD_ERROR(("%s: Flow Create Req flowid %d - failure ring space\n",
			__FUNCTION__, flow_ring_node->flowid));
		DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef PCIE_INB_DW
		dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
		return BCME_NOMEM;
	}

	flow_ring_node->prot_info = (void *)flow_ring;

	/* Common msg buf hdr */
	flow_create_rqst->msg.msg_type = MSG_TYPE_FLOW_RING_CREATE;
	flow_create_rqst->msg.if_id = (uint8)flow_ring_node->flow_info.ifindex;
	flow_create_rqst->msg.request_id = htol32(0); /* TBD */
	flow_create_rqst->msg.flags = ctrl_ring->current_phase;

	flow_create_rqst->msg.epoch = ctrl_ring->seqnum % H2D_EPOCH_MODULO;
	ctrl_ring->seqnum++;

	/* Update flow create message */
	flow_create_rqst->tid = flow_ring_node->flow_info.tid;
	flow_create_rqst->flow_ring_id = htol16((uint16)flow_ring_node->flowid);
	memcpy(flow_create_rqst->sa, flow_ring_node->flow_info.sa, sizeof(flow_create_rqst->sa));
	memcpy(flow_create_rqst->da, flow_ring_node->flow_info.da, sizeof(flow_create_rqst->da));
	/* CAUTION: ring::base_addr already in Little Endian */
	flow_create_rqst->flow_ring_ptr.low_addr = flow_ring->base_addr.low_addr;
	flow_create_rqst->flow_ring_ptr.high_addr = flow_ring->base_addr.high_addr;
	flow_create_rqst->max_items = htol16(prot->h2d_max_txpost);
	flow_create_rqst->len_item = htol16(H2DRING_TXPOST_ITEMSIZE);

	/* definition for ifrm mask : bit0:d11ac core, bit1:d11ad core
	 * currently it is not used for priority. so uses solely for ifrm mask
	 */
	if (IFRM_ACTIVE(dhd))
		flow_create_rqst->priority_ifrmmask = (1 << IFRM_DEV_0);

	DHD_ERROR(("%s: Send Flow Create Req flow ID %d for peer " MACDBG
		" prio %d ifindex %d\n", __FUNCTION__, flow_ring_node->flowid,
		MAC2STRDBG(flow_ring_node->flow_info.da), flow_ring_node->flow_info.tid,
		flow_ring_node->flow_info.ifindex));

	/* Update the flow_ring's WRITE index */
	if (IDMA_ACTIVE(dhd) || dhd->dma_h2d_ring_upd_support) {
		dhd_prot_dma_indx_set(dhd, flow_ring->wr,
		                      H2D_DMA_INDX_WR_UPD, flow_ring->idx);
	} else if (IFRM_ACTIVE(dhd) && DHD_IS_FLOWRING(flow_ring->idx, max_flowrings)) {
		dhd_prot_dma_indx_set(dhd, flow_ring->wr,
			H2D_IFRM_INDX_WR_UPD, flow_ring->idx);
	} else {
		dhd_bus_cmn_writeshared(dhd->bus, &(flow_ring->wr),
			sizeof(uint16), RING_WR_UPD, flow_ring->idx);
	}

	/* update control subn ring's WR index and ring doorbell to dongle */
	dhd_prot_ring_write_complete(dhd, ctrl_ring, flow_create_rqst, 1);

	DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif

	return BCME_OK;
} /* dhd_prot_flow_ring_create */

/** called on receiving MSG_TYPE_FLOW_RING_CREATE_CMPLT message from dongle */
static void
dhd_prot_flow_ring_create_response_process(dhd_pub_t *dhd, void *msg)
{
	tx_flowring_create_response_t *flow_create_resp = (tx_flowring_create_response_t *)msg;

	DHD_ERROR(("%s: Flow Create Response status = %d Flow %d\n", __FUNCTION__,
		ltoh16(flow_create_resp->cmplt.status),
		ltoh16(flow_create_resp->cmplt.flow_ring_id)));

	dhd_bus_flow_ring_create_response(dhd->bus,
		ltoh16(flow_create_resp->cmplt.flow_ring_id),
		ltoh16(flow_create_resp->cmplt.status));
}

static void
dhd_prot_process_h2d_ring_create_complete(dhd_pub_t *dhd, void *buf)
{
	h2d_ring_create_response_t *resp = (h2d_ring_create_response_t *)buf;
	DHD_INFO(("%s ring create Response status = %d ring %d, id 0x%04x\n", __FUNCTION__,
		ltoh16(resp->cmplt.status),
		ltoh16(resp->cmplt.ring_id),
		ltoh32(resp->cmn_hdr.request_id)));
	if (ltoh32(resp->cmn_hdr.request_id) != DHD_H2D_DBGRING_REQ_PKTID) {
		DHD_ERROR(("invalid request ID with h2d ring create complete\n"));
		return;
	}
	if (!dhd->prot->h2dring_info_subn->create_pending) {
		DHD_ERROR(("info ring create status for not pending submit ring\n"));
	}

	if (ltoh16(resp->cmplt.status) != BCMPCIE_SUCCESS) {
		DHD_ERROR(("info ring create failed with status %d\n",
			ltoh16(resp->cmplt.status)));
		return;
	}
	dhd->prot->h2dring_info_subn->create_pending = FALSE;
	dhd->prot->h2dring_info_subn->inited = TRUE;
	dhd_prot_infobufpost(dhd);
}

static void
dhd_prot_process_d2h_ring_create_complete(dhd_pub_t *dhd, void *buf)
{
	d2h_ring_create_response_t *resp = (d2h_ring_create_response_t *)buf;
	DHD_INFO(("%s ring create Response status = %d ring %d, id 0x%04x\n", __FUNCTION__,
		ltoh16(resp->cmplt.status),
		ltoh16(resp->cmplt.ring_id),
		ltoh32(resp->cmn_hdr.request_id)));
	if (ltoh32(resp->cmn_hdr.request_id) != DHD_D2H_DBGRING_REQ_PKTID) {
		DHD_ERROR(("invalid request ID with d2h ring create complete\n"));
		return;
	}
	if (!dhd->prot->d2hring_info_cpln->create_pending) {
		DHD_ERROR(("info ring create status for not pending cpl ring\n"));
		return;
	}

	if (ltoh16(resp->cmplt.status) != BCMPCIE_SUCCESS) {
		DHD_ERROR(("info cpl ring create failed with status %d\n",
			ltoh16(resp->cmplt.status)));
		return;
	}
	dhd->prot->d2hring_info_cpln->create_pending = FALSE;
	dhd->prot->d2hring_info_cpln->inited = TRUE;
}

static void
dhd_prot_process_d2h_mb_data(dhd_pub_t *dhd, void* buf)
{
	d2h_mailbox_data_t *d2h_data;

	d2h_data = (d2h_mailbox_data_t *)buf;
	DHD_INFO(("%s dhd_prot_process_d2h_mb_data, 0x%04x\n", __FUNCTION__,
		d2h_data->d2h_mailbox_data));
	dhd_bus_handle_mb_data(dhd->bus, d2h_data->d2h_mailbox_data);
}

static void
dhd_prot_process_d2h_host_ts_complete(dhd_pub_t *dhd, void* buf)
{
#ifdef DHD_TIMESYNC
	host_timestamp_msg_cpl_t  *host_ts_cpl;
	uint32 pktid;
	dhd_prot_t *prot = dhd->prot;

	host_ts_cpl = (host_timestamp_msg_cpl_t *)buf;
	DHD_INFO(("%s host TS cpl: status %d, req_ID: 0x%04x, xt_id %d \n", __FUNCTION__,
		host_ts_cpl->cmplt.status, host_ts_cpl->msg.request_id, host_ts_cpl->xt_id));

	pktid = ltoh32(host_ts_cpl->msg.request_id);
	if (prot->hostts_req_buf_inuse == FALSE) {
		DHD_ERROR(("No Pending Host TS req, but completion\n"));
		return;
	}
	prot->hostts_req_buf_inuse = FALSE;
	if (pktid != DHD_H2D_HOSTTS_REQ_PKTID) {
		DHD_ERROR(("Host TS req CPL, but req ID different 0x%04x, exp 0x%04x\n",
			pktid, DHD_H2D_HOSTTS_REQ_PKTID));
		return;
	}
	dhd_timesync_handle_host_ts_complete(dhd->ts, host_ts_cpl->xt_id,
		host_ts_cpl->cmplt.status);
#else /* DHD_TIMESYNC */
	DHD_ERROR(("Timesunc feature not compiled in but GOT HOST_TS_COMPLETE\n"));
#endif /* DHD_TIMESYNC */

}

/** called on e.g. flow ring delete */
void dhd_prot_clean_flow_ring(dhd_pub_t *dhd, void *msgbuf_flow_info)
{
	msgbuf_ring_t *flow_ring = (msgbuf_ring_t *)msgbuf_flow_info;
	dhd_prot_ring_detach(dhd, flow_ring);
	DHD_INFO(("%s Cleaning up Flow \n", __FUNCTION__));
}

void dhd_prot_print_flow_ring(dhd_pub_t *dhd, void *msgbuf_flow_info,
	struct bcmstrbuf *strbuf, const char * fmt)
{
	const char *default_fmt = "RD %d WR %d BASE(VA) %p BASE(PA) %x:%x"
		" WORK ITEM SIZE %d MAX WORK ITEMS %d SIZE %d\n";
	msgbuf_ring_t *flow_ring = (msgbuf_ring_t *)msgbuf_flow_info;
	uint16 rd, wr;
	uint32 dma_buf_len = flow_ring->max_items * flow_ring->item_len;

	if (fmt == NULL) {
		fmt = default_fmt;
	}
	dhd_bus_cmn_readshared(dhd->bus, &rd, RING_RD_UPD, flow_ring->idx);
	dhd_bus_cmn_readshared(dhd->bus, &wr, RING_WR_UPD, flow_ring->idx);
	bcm_bprintf(strbuf, fmt, rd, wr, flow_ring->dma_buf.va,
		ltoh32(flow_ring->base_addr.high_addr),
		ltoh32(flow_ring->base_addr.low_addr),
		flow_ring->item_len, flow_ring->max_items, dma_buf_len);
}

void dhd_prot_print_info(dhd_pub_t *dhd, struct bcmstrbuf *strbuf)
{
	dhd_prot_t *prot = dhd->prot;
	bcm_bprintf(strbuf, "IPCrevs: Dev %d, \t Host %d, \tactive %d\n",
		dhd->prot->device_ipc_version,
		dhd->prot->host_ipc_version,
		dhd->prot->active_ipc_version);

	bcm_bprintf(strbuf, "max Host TS bufs to post: %d, \t posted %d \n",
		dhd->prot->max_tsbufpost, dhd->prot->cur_ts_bufs_posted);
	bcm_bprintf(strbuf, "max INFO bufs to post: %d, \t posted %d \n",
		dhd->prot->max_infobufpost, dhd->prot->infobufpost);
	bcm_bprintf(strbuf, "max event bufs to post: %d, \t posted %d \n",
		dhd->prot->max_eventbufpost, dhd->prot->cur_event_bufs_posted);
	bcm_bprintf(strbuf, "max ioctlresp bufs to post: %d, \t posted %d \n",
		dhd->prot->max_ioctlrespbufpost, dhd->prot->cur_ioctlresp_bufs_posted);
	bcm_bprintf(strbuf, "max RX bufs to post: %d, \t posted %d \n",
		dhd->prot->max_rxbufpost, dhd->prot->rxbufpost);

	bcm_bprintf(strbuf,
		"%14s %5s %5s %17s %17s %14s %14s %10s\n",
		"Type", "RD", "WR", "BASE(VA)", "BASE(PA)",
		"WORK_ITEM_SIZE", "MAX_WORK_ITEMS", "TOTAL_SIZE");
	bcm_bprintf(strbuf, "%14s", "H2DCtrlPost");
	dhd_prot_print_flow_ring(dhd, &prot->h2dring_ctrl_subn, strbuf,
		" %5d %5d %17p %8x:%8x %14d %14d %10d\n");
	bcm_bprintf(strbuf, "%14s", "D2HCtrlCpl");
	dhd_prot_print_flow_ring(dhd, &prot->d2hring_ctrl_cpln, strbuf,
		" %5d %5d %17p %8x:%8x %14d %14d %10d\n");
	bcm_bprintf(strbuf, "%14s", "H2DRxPost", prot->rxbufpost);
	dhd_prot_print_flow_ring(dhd, &prot->h2dring_rxp_subn, strbuf,
		" %5d %5d %17p %8x:%8x %14d %14d %10d\n");
	bcm_bprintf(strbuf, "%14s", "D2HRxCpl");
	dhd_prot_print_flow_ring(dhd, &prot->d2hring_rx_cpln, strbuf,
		" %5d %5d %17p %8x:%8x %14d %14d %10d\n");
	bcm_bprintf(strbuf, "%14s", "D2HTxCpl");
	dhd_prot_print_flow_ring(dhd, &prot->d2hring_tx_cpln, strbuf,
		" %5d %5d %17p %8x:%8x %14d %14d %10d\n");
	if (dhd->prot->h2dring_info_subn != NULL && dhd->prot->d2hring_info_cpln != NULL) {
		bcm_bprintf(strbuf, "%14s", "H2DRingInfoSub");
		dhd_prot_print_flow_ring(dhd, prot->h2dring_info_subn, strbuf,
			" %5d %5d %17p %8x:%8x %14d %14d %10d\n");
		bcm_bprintf(strbuf, "%14s", "D2HRingInfoCpl");
		dhd_prot_print_flow_ring(dhd, prot->d2hring_info_cpln, strbuf,
			" %5d %5d %17p %8x:%8x %14d %14d %10d\n");
	}

	bcm_bprintf(strbuf, "active_tx_count %d	 pktidmap_avail(ctrl/rx/tx) %d %d %d\n",
		dhd->prot->active_tx_count,
		DHD_PKTID_AVAIL(dhd->prot->pktid_ctrl_map),
		DHD_PKTID_AVAIL(dhd->prot->pktid_rx_map),
		DHD_PKTID_AVAIL(dhd->prot->pktid_tx_map));

}

int
dhd_prot_flow_ring_delete(dhd_pub_t *dhd, flow_ring_node_t *flow_ring_node)
{
	tx_flowring_delete_request_t *flow_delete_rqst;
	dhd_prot_t *prot = dhd->prot;
	unsigned long flags;
	uint16 alloced = 0;
	msgbuf_ring_t *ring = &prot->h2dring_ctrl_subn;

#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */

	DHD_GENERAL_LOCK(dhd, flags);

	/* Request for ring buffer space */
	flow_delete_rqst = (tx_flowring_delete_request_t *)
		dhd_prot_alloc_ring_space(dhd, ring, 1, &alloced, FALSE);

	if (flow_delete_rqst == NULL) {
		DHD_GENERAL_UNLOCK(dhd, flags);
		DHD_ERROR(("%s: Flow Delete Req - failure ring space\n", __FUNCTION__));
#ifdef PCIE_INB_DW
		dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
		return BCME_NOMEM;
	}

	/* Common msg buf hdr */
	flow_delete_rqst->msg.msg_type = MSG_TYPE_FLOW_RING_DELETE;
	flow_delete_rqst->msg.if_id = (uint8)flow_ring_node->flow_info.ifindex;
	flow_delete_rqst->msg.request_id = htol32(0); /* TBD */
	flow_delete_rqst->msg.flags = ring->current_phase;

	flow_delete_rqst->msg.epoch = ring->seqnum % H2D_EPOCH_MODULO;
	ring->seqnum++;

	/* Update Delete info */
	flow_delete_rqst->flow_ring_id = htol16((uint16)flow_ring_node->flowid);
	flow_delete_rqst->reason = htol16(BCME_OK);

	DHD_ERROR(("%s: Send Flow Delete Req RING ID %d for peer " MACDBG
		" prio %d ifindex %d\n", __FUNCTION__, flow_ring_node->flowid,
		MAC2STRDBG(flow_ring_node->flow_info.da), flow_ring_node->flow_info.tid,
		flow_ring_node->flow_info.ifindex));

	/* update ring's WR index and ring doorbell to dongle */
	dhd_prot_ring_write_complete(dhd, ring, flow_delete_rqst, 1);
	DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif

	return BCME_OK;
}

static void
dhd_prot_flow_ring_delete_response_process(dhd_pub_t *dhd, void *msg)
{
	tx_flowring_delete_response_t *flow_delete_resp = (tx_flowring_delete_response_t *)msg;

	DHD_ERROR(("%s: Flow Delete Response status = %d Flow %d\n", __FUNCTION__,
		flow_delete_resp->cmplt.status, flow_delete_resp->cmplt.flow_ring_id));

	dhd_bus_flow_ring_delete_response(dhd->bus, flow_delete_resp->cmplt.flow_ring_id,
		flow_delete_resp->cmplt.status);
}

static void
dhd_prot_process_flow_ring_resume_response(dhd_pub_t *dhd, void* msg)
{
#ifdef IDLE_TX_FLOW_MGMT
	tx_idle_flowring_resume_response_t	*flow_resume_resp =
		(tx_idle_flowring_resume_response_t *)msg;

	DHD_ERROR(("%s Flow resume Response status = %d Flow %d\n", __FUNCTION__,
		flow_resume_resp->cmplt.status, flow_resume_resp->cmplt.flow_ring_id));

	dhd_bus_flow_ring_resume_response(dhd->bus, flow_resume_resp->cmplt.flow_ring_id,
		flow_resume_resp->cmplt.status);
#endif /* IDLE_TX_FLOW_MGMT */
}

static void
dhd_prot_process_flow_ring_suspend_response(dhd_pub_t *dhd, void* msg)
{
#ifdef IDLE_TX_FLOW_MGMT
	int16 status;
	tx_idle_flowring_suspend_response_t	*flow_suspend_resp =
		(tx_idle_flowring_suspend_response_t *)msg;
	status = flow_suspend_resp->cmplt.status;

	DHD_ERROR(("%s Flow id %d suspend Response status = %d\n",
		__FUNCTION__, flow_suspend_resp->cmplt.flow_ring_id,
		status));

	if (status != BCME_OK) {

		DHD_ERROR(("%s Error in Suspending Flow rings!!"
			"Dongle will still be polling idle rings!!Status = %d \n",
			__FUNCTION__, status));
	}
#endif /* IDLE_TX_FLOW_MGMT */
}

int
dhd_prot_flow_ring_flush(dhd_pub_t *dhd, flow_ring_node_t *flow_ring_node)
{
	tx_flowring_flush_request_t *flow_flush_rqst;
	dhd_prot_t *prot = dhd->prot;
	unsigned long flags;
	uint16 alloced = 0;
	msgbuf_ring_t *ring = &prot->h2dring_ctrl_subn;

#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */

	DHD_GENERAL_LOCK(dhd, flags);

	/* Request for ring buffer space */
	flow_flush_rqst = (tx_flowring_flush_request_t *)
		dhd_prot_alloc_ring_space(dhd, ring, 1, &alloced, FALSE);
	if (flow_flush_rqst == NULL) {
		DHD_GENERAL_UNLOCK(dhd, flags);
		DHD_ERROR(("%s: Flow Flush Req - failure ring space\n", __FUNCTION__));
#ifdef PCIE_INB_DW
		dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
		return BCME_NOMEM;
	}

	/* Common msg buf hdr */
	flow_flush_rqst->msg.msg_type = MSG_TYPE_FLOW_RING_FLUSH;
	flow_flush_rqst->msg.if_id = (uint8)flow_ring_node->flow_info.ifindex;
	flow_flush_rqst->msg.request_id = htol32(0); /* TBD */
	flow_flush_rqst->msg.flags = ring->current_phase;
	flow_flush_rqst->msg.epoch = ring->seqnum % H2D_EPOCH_MODULO;
	ring->seqnum++;

	flow_flush_rqst->flow_ring_id = htol16((uint16)flow_ring_node->flowid);
	flow_flush_rqst->reason = htol16(BCME_OK);

	DHD_INFO(("%s: Send Flow Flush Req\n", __FUNCTION__));

	/* update ring's WR index and ring doorbell to dongle */
	dhd_prot_ring_write_complete(dhd, ring, flow_flush_rqst, 1);
	DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif

	return BCME_OK;
} /* dhd_prot_flow_ring_flush */

static void
dhd_prot_flow_ring_flush_response_process(dhd_pub_t *dhd, void *msg)
{
	tx_flowring_flush_response_t *flow_flush_resp = (tx_flowring_flush_response_t *)msg;

	DHD_INFO(("%s: Flow Flush Response status = %d\n", __FUNCTION__,
		flow_flush_resp->cmplt.status));

	dhd_bus_flow_ring_flush_response(dhd->bus, flow_flush_resp->cmplt.flow_ring_id,
		flow_flush_resp->cmplt.status);
}

/**
 * Request dongle to configure soft doorbells for D2H rings. Host populated soft
 * doorbell information is transferred to dongle via the d2h ring config control
 * message.
 */
void
dhd_msgbuf_ring_config_d2h_soft_doorbell(dhd_pub_t *dhd)
{
#if defined(DHD_D2H_SOFT_DOORBELL_SUPPORT)
	uint16 ring_idx;
	uint8 *msg_next;
	void *msg_start;
	uint16 alloced = 0;
	unsigned long flags;
	dhd_prot_t *prot = dhd->prot;
	ring_config_req_t *ring_config_req;
	bcmpcie_soft_doorbell_t *soft_doorbell;
	msgbuf_ring_t *ctrl_ring = &prot->h2dring_ctrl_subn;
	const uint16 d2h_rings = BCMPCIE_D2H_COMMON_MSGRINGS;

#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */
	/* Claim space for d2h_ring number of d2h_ring_config_req_t messages */
	DHD_GENERAL_LOCK(dhd, flags);

	msg_start = dhd_prot_alloc_ring_space(dhd, ctrl_ring, d2h_rings, &alloced, TRUE);

	if (msg_start == NULL) {
		DHD_ERROR(("%s Msgbuf no space for %d D2H ring config soft doorbells\n",
			__FUNCTION__, d2h_rings));
		DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef PCIE_INB_DW
		dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
		return;
	}

	msg_next = (uint8*)msg_start;

	for (ring_idx = 0; ring_idx < d2h_rings; ring_idx++) {

		/* position the ring_config_req into the ctrl subm ring */
		ring_config_req = (ring_config_req_t *)msg_next;

		/* Common msg header */
		ring_config_req->msg.msg_type = MSG_TYPE_D2H_RING_CONFIG;
		ring_config_req->msg.if_id = 0;
		ring_config_req->msg.flags = 0;

		ring_config_req->msg.epoch = ctrl_ring->seqnum % H2D_EPOCH_MODULO;
		ctrl_ring->seqnum++;

		ring_config_req->msg.request_id = htol32(DHD_FAKE_PKTID); /* unused */

		/* Ring Config subtype and d2h ring_id */
		ring_config_req->subtype = htol16(D2H_RING_CONFIG_SUBTYPE_SOFT_DOORBELL);
		ring_config_req->ring_id = htol16(DHD_D2H_RINGID(ring_idx));

		/* Host soft doorbell configuration */
		soft_doorbell = &prot->soft_doorbell[ring_idx];

		ring_config_req->soft_doorbell.value = htol32(soft_doorbell->value);
		ring_config_req->soft_doorbell.haddr.high =
			htol32(soft_doorbell->haddr.high);
		ring_config_req->soft_doorbell.haddr.low =
			htol32(soft_doorbell->haddr.low);
		ring_config_req->soft_doorbell.items = htol16(soft_doorbell->items);
		ring_config_req->soft_doorbell.msecs = htol16(soft_doorbell->msecs);

		DHD_INFO(("%s: Soft doorbell haddr 0x%08x 0x%08x value 0x%08x\n",
			__FUNCTION__, ring_config_req->soft_doorbell.haddr.high,
			ring_config_req->soft_doorbell.haddr.low,
			ring_config_req->soft_doorbell.value));

		msg_next = msg_next + ctrl_ring->item_len;
	}

	/* update control subn ring's WR index and ring doorbell to dongle */
	dhd_prot_ring_write_complete(dhd, ctrl_ring, msg_start, d2h_rings);
	DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
#endif /* DHD_D2H_SOFT_DOORBELL_SUPPORT */
}

static void
dhd_prot_process_d2h_ring_config_complete(dhd_pub_t *dhd, void *msg)
{
	DHD_INFO(("%s: Ring Config Response - status %d ringid %d\n",
		__FUNCTION__, ltoh16(((ring_config_resp_t *)msg)->compl_hdr.status),
		ltoh16(((ring_config_resp_t *)msg)->compl_hdr.flow_ring_id)));
}

int
dhd_prot_debug_dma_info_print(dhd_pub_t *dhd)
{
	if (dhd->bus->is_linkdown) {
		DHD_ERROR(("\n ------- SKIP DUMPING DMA Registers "
			"due to PCIe link down ------- \r\n"));
		return 0;
	}

	DHD_ERROR(("\n ------- DUMPING DMA Registers ------- \r\n"));

	//HostToDev
	DHD_ERROR(("HostToDev TX: XmtCtrl=0x%08x XmtPtr=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x200, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x204, 0, 0)));
	DHD_ERROR(("            : XmtAddrLow=0x%08x XmtAddrHigh=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x208, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x20C, 0, 0)));
	DHD_ERROR(("            : XmtStatus0=0x%08x XmtStatus1=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x210, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x214, 0, 0)));

	DHD_ERROR(("HostToDev RX: RcvCtrl=0x%08x RcvPtr=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x220, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x224, 0, 0)));
	DHD_ERROR(("            : RcvAddrLow=0x%08x RcvAddrHigh=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x228, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x22C, 0, 0)));
	DHD_ERROR(("            : RcvStatus0=0x%08x RcvStatus1=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x230, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x234, 0, 0)));

	//DevToHost
	DHD_ERROR(("DevToHost TX: XmtCtrl=0x%08x XmtPtr=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x240, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x244, 0, 0)));
	DHD_ERROR(("            : XmtAddrLow=0x%08x XmtAddrHigh=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x248, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x24C, 0, 0)));
	DHD_ERROR(("            : XmtStatus0=0x%08x XmtStatus1=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x250, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x254, 0, 0)));

	DHD_ERROR(("DevToHost RX: RcvCtrl=0x%08x RcvPtr=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x260, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x264, 0, 0)));
	DHD_ERROR(("            : RcvAddrLow=0x%08x RcvAddrHigh=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x268, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x26C, 0, 0)));
	DHD_ERROR(("            : RcvStatus0=0x%08x RcvStatus1=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x270, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x274, 0, 0)));

	return 0;
}

int
dhd_prot_debug_info_print(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	msgbuf_ring_t *ring;
	uint16 rd, wr;
	uint32 intstatus = 0;
	uint32 intmask = 0;
	uint32 mbintstatus = 0;
	uint32 d2h_mb_data = 0;
	uint32 dma_buf_len;

	DHD_ERROR(("\n ------- DUMPING VERSION INFORMATION ------- \r\n"));
	DHD_ERROR(("DHD: %s\n", dhd_version));
	DHD_ERROR(("Firmware: %s\n", fw_version));

	DHD_ERROR(("\n ------- DUMPING PROTOCOL INFORMATION ------- \r\n"));
	DHD_ERROR(("ICPrevs: Dev %d, Host %d, active %d\n",
		prot->device_ipc_version,
		prot->host_ipc_version,
		prot->active_ipc_version));
	DHD_ERROR(("max Host TS bufs to post: %d, posted %d\n",
		prot->max_tsbufpost, prot->cur_ts_bufs_posted));
	DHD_ERROR(("max INFO bufs to post: %d, posted %d\n",
		prot->max_infobufpost, prot->infobufpost));
	DHD_ERROR(("max event bufs to post: %d, posted %d\n",
		prot->max_eventbufpost, prot->cur_event_bufs_posted));
	DHD_ERROR(("max ioctlresp bufs to post: %d, posted %d\n",
		prot->max_ioctlrespbufpost, prot->cur_ioctlresp_bufs_posted));
	DHD_ERROR(("max RX bufs to post: %d, posted %d\n",
		prot->max_rxbufpost, prot->rxbufpost));
	DHD_ERROR(("h2d_max_txpost: %d, prot->h2d_max_txpost: %d\n",
		h2d_max_txpost, prot->h2d_max_txpost));

	DHD_ERROR(("\n ------- DUMPING IOCTL RING RD WR Pointers ------- \r\n"));

	ring = &prot->h2dring_ctrl_subn;
	dma_buf_len = ring->max_items * ring->item_len;
	DHD_ERROR(("CtrlPost: Mem Info: BASE(VA) %p BASE(PA) %x:%x SIZE %d \r\n",
		ring->dma_buf.va, ltoh32(ring->base_addr.high_addr),
		ltoh32(ring->base_addr.low_addr), dma_buf_len));
	DHD_ERROR(("CtrlPost: From Host mem: RD: %d WR %d \r\n", ring->rd, ring->wr));
	dhd_bus_cmn_readshared(dhd->bus, &rd, RING_RD_UPD, ring->idx);
	dhd_bus_cmn_readshared(dhd->bus, &wr, RING_WR_UPD, ring->idx);
	DHD_ERROR(("CtrlPost: From Shared Mem: RD: %d WR %d \r\n", rd, wr));
	DHD_ERROR(("CtrlPost: seq num: %d \r\n", ring->seqnum % H2D_EPOCH_MODULO));

	ring = &prot->d2hring_ctrl_cpln;
	dma_buf_len = ring->max_items * ring->item_len;
	DHD_ERROR(("CtrlCpl: Mem Info: BASE(VA) %p BASE(PA) %x:%x SIZE %d \r\n",
		ring->dma_buf.va, ltoh32(ring->base_addr.high_addr),
		ltoh32(ring->base_addr.low_addr), dma_buf_len));
	DHD_ERROR(("CtrlCpl: From Host mem: RD: %d WR %d \r\n", ring->rd, ring->wr));
	dhd_bus_cmn_readshared(dhd->bus, &rd, RING_RD_UPD, ring->idx);
	dhd_bus_cmn_readshared(dhd->bus, &wr, RING_WR_UPD, ring->idx);
	DHD_ERROR(("CtrlCpl: From Shared Mem: RD: %d WR %d \r\n", rd, wr));
	DHD_ERROR(("CtrlCpl: Expected seq num: %d \r\n", ring->seqnum % H2D_EPOCH_MODULO));

	ring = prot->h2dring_info_subn;
	if (ring) {
		dma_buf_len = ring->max_items * ring->item_len;
		DHD_ERROR(("InfoSub: Mem Info: BASE(VA) %p BASE(PA) %x:%x SIZE %d \r\n",
			ring->dma_buf.va, ltoh32(ring->base_addr.high_addr),
			ltoh32(ring->base_addr.low_addr), dma_buf_len));
		DHD_ERROR(("InfoSub: From Host mem: RD: %d WR %d \r\n", ring->rd, ring->wr));
		dhd_bus_cmn_readshared(dhd->bus, &rd, RING_RD_UPD, ring->idx);
		dhd_bus_cmn_readshared(dhd->bus, &wr, RING_WR_UPD, ring->idx);
		DHD_ERROR(("InfoSub: From Shared Mem: RD: %d WR %d \r\n", rd, wr));
		DHD_ERROR(("InfoSub: seq num: %d \r\n", ring->seqnum % H2D_EPOCH_MODULO));
	}
	ring = prot->d2hring_info_cpln;
	if (ring) {
		dma_buf_len = ring->max_items * ring->item_len;
		DHD_ERROR(("InfoCpl: Mem Info: BASE(VA) %p BASE(PA) %x:%x SIZE %d \r\n",
			ring->dma_buf.va, ltoh32(ring->base_addr.high_addr),
			ltoh32(ring->base_addr.low_addr), dma_buf_len));
		DHD_ERROR(("InfoCpl: From Host mem: RD: %d WR %d \r\n", ring->rd, ring->wr));
		dhd_bus_cmn_readshared(dhd->bus, &rd, RING_RD_UPD, ring->idx);
		dhd_bus_cmn_readshared(dhd->bus, &wr, RING_WR_UPD, ring->idx);
		DHD_ERROR(("InfoCpl: From Shared Mem: RD: %d WR %d \r\n", rd, wr));
		DHD_ERROR(("InfoCpl: Expected seq num: %d \r\n", ring->seqnum % H2D_EPOCH_MODULO));
	}

	DHD_ERROR(("%s: cur_ioctlresp_bufs_posted %d cur_event_bufs_posted %d\n",
		__FUNCTION__, prot->cur_ioctlresp_bufs_posted, prot->cur_event_bufs_posted));

	if (!dhd->bus->is_linkdown && dhd->bus->intstatus != (uint32)-1) {
		DHD_ERROR(("\n ------- DUMPING INTR Status and Masks ------- \r\n"));
		intstatus = si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
			PCIMailBoxInt, 0, 0);
		intmask = si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
			PCIMailBoxMask, 0, 0);
		mbintstatus = si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
			PCID2H_MailBox, 0, 0);
		dhd_bus_cmn_readshared(dhd->bus, &d2h_mb_data, D2H_MB_DATA, 0);

		DHD_ERROR(("intstatus=0x%x intmask=0x%x mbintstatus=0x%x\n",
			intstatus, intmask, mbintstatus));
		DHD_ERROR(("d2h_mb_data=0x%x def_intmask=0x%x \r\n", d2h_mb_data,
			dhd->bus->def_intmask));

		DHD_ERROR(("host pcie_irq enabled = %d\n", dhdpcie_irq_enabled(dhd->bus)));

		DHD_ERROR(("\n ------- DUMPING PCIE Registers ------- \r\n"));
		/* hwnbu-twiki.sj.broadcom.com/bin/view/Mwgroup/CurrentPcieGen2ProgramGuide */
		DHD_ERROR(("Status Command(0x%x)=0x%x, BaseAddress0(0x%x)=0x%x\n",
			PCIECFGREG_STATUS_CMD,
			dhd_pcie_config_read(dhd->bus->osh, PCIECFGREG_STATUS_CMD, sizeof(uint32)),
			PCIECFGREG_BASEADDR0,
			dhd_pcie_config_read(dhd->bus->osh, PCIECFGREG_BASEADDR0, sizeof(uint32))));
		DHD_ERROR(("LinkCtl(0x%x)=0x%x DeviceStatusControl2(0x%x)=0x%x "
			"L1SSControl(0x%x)=0x%x\n", PCIECFGREG_LINK_STATUS_CTRL,
			dhd_pcie_config_read(dhd->bus->osh, PCIECFGREG_LINK_STATUS_CTRL,
			sizeof(uint32)), PCIECFGGEN_DEV_STATUS_CTRL2,
			dhd_pcie_config_read(dhd->bus->osh, PCIECFGGEN_DEV_STATUS_CTRL2,
			sizeof(uint32)), PCIECFGREG_PML1_SUB_CTRL1,
			dhd_pcie_config_read(dhd->bus->osh, PCIECFGREG_PML1_SUB_CTRL1,
			sizeof(uint32))));

		/* hwnbu-twiki.sj.broadcom.com/twiki/pub/Mwgroup/
		 * CurrentPcieGen2ProgramGuide/pcie_ep.htm
		 */
		DHD_ERROR(("ClkReq0(0x%x)=0x%x ClkReq1(0x%x)=0x%x ClkReq2(0x%x)=0x%x "
			"ClkReq3(0x%x)=0x%x\n", PCIECFGREG_PHY_DBG_CLKREQ0,
			dhd_pcie_corereg_read(dhd->bus->sih, PCIECFGREG_PHY_DBG_CLKREQ0),
			PCIECFGREG_PHY_DBG_CLKREQ1,
			dhd_pcie_corereg_read(dhd->bus->sih, PCIECFGREG_PHY_DBG_CLKREQ1),
			PCIECFGREG_PHY_DBG_CLKREQ2,
			dhd_pcie_corereg_read(dhd->bus->sih, PCIECFGREG_PHY_DBG_CLKREQ2),
			PCIECFGREG_PHY_DBG_CLKREQ3,
			dhd_pcie_corereg_read(dhd->bus->sih, PCIECFGREG_PHY_DBG_CLKREQ3)));

#if defined(PCIE_RC_VENDOR_ID) && defined(PCIE_RC_DEVICE_ID)
		DHD_ERROR(("Pcie RC Error Status Val=0x%x\n",
			dhdpcie_rc_access_cap(dhd->bus, PCIE_EXTCAP_ID_ERR,
			PCIE_EXTCAP_AER_UCERR_OFFSET, TRUE, FALSE, 0)));

		DHD_ERROR(("RootPort PCIe linkcap=0x%08x\n",
			dhd_debug_get_rc_linkcap(dhd->bus)));
#endif

		DHD_ERROR(("\n ------- DUMPING INTR enable/disable counters  ------- \r\n"));
		DHD_ERROR(("resume_intr_enable_count=%lu dpc_intr_enable_count=%lu\n"
			"isr_intr_disable_count=%lu suspend_intr_disable_count=%lu\n"
			"dpc_return_busdown_count=%lu\n",
			dhd->bus->resume_intr_enable_count, dhd->bus->dpc_intr_enable_count,
			dhd->bus->isr_intr_disable_count, dhd->bus->suspend_intr_disable_count,
			dhd->bus->dpc_return_busdown_count));

	}
	dhd_prot_debug_dma_info_print(dhd);
#ifdef DHD_FW_COREDUMP
	if (dhd->memdump_enabled) {
#ifdef DHD_SSSR_DUMP
		if (dhd->sssr_inited) {
			dhdpcie_sssr_dump(dhd);
		}
#endif /* DHD_SSSR_DUMP */
	}
#endif /* DHD_FW_COREDUMP */
	return 0;
}

int
dhd_prot_ringupd_dump(dhd_pub_t *dhd, struct bcmstrbuf *b)
{
	uint32 *ptr;
	uint32 value;
	uint32 i;
	uint32 max_h2d_queues = dhd_bus_max_h2d_queues(dhd->bus);

	OSL_CACHE_INV((void *)dhd->prot->d2h_dma_indx_wr_buf.va,
		dhd->prot->d2h_dma_indx_wr_buf.len);

	ptr = (uint32 *)(dhd->prot->d2h_dma_indx_wr_buf.va);

	bcm_bprintf(b, "\n max_tx_queues %d\n", max_h2d_queues);

	bcm_bprintf(b, "\nRPTR block H2D common rings, 0x%04x\n", ptr);
	value = ltoh32(*ptr);
	bcm_bprintf(b, "\tH2D CTRL: value 0x%04x\n", value);
	ptr++;
	value = ltoh32(*ptr);
	bcm_bprintf(b, "\tH2D RXPOST: value 0x%04x\n", value);

	ptr++;
	bcm_bprintf(b, "RPTR block Flow rings , 0x%04x\n", ptr);
	for (i = BCMPCIE_H2D_COMMON_MSGRINGS; i < max_h2d_queues; i++) {
		value = ltoh32(*ptr);
		bcm_bprintf(b, "\tflowring ID %d: value 0x%04x\n", i, value);
		ptr++;
	}

	OSL_CACHE_INV((void *)dhd->prot->h2d_dma_indx_rd_buf.va,
		dhd->prot->h2d_dma_indx_rd_buf.len);

	ptr = (uint32 *)(dhd->prot->h2d_dma_indx_rd_buf.va);

	bcm_bprintf(b, "\nWPTR block D2H common rings, 0x%04x\n", ptr);
	value = ltoh32(*ptr);
	bcm_bprintf(b, "\tD2H CTRLCPLT: value 0x%04x\n", value);
	ptr++;
	value = ltoh32(*ptr);
	bcm_bprintf(b, "\tD2H TXCPLT: value 0x%04x\n", value);
	ptr++;
	value = ltoh32(*ptr);
	bcm_bprintf(b, "\tD2H RXCPLT: value 0x%04x\n", value);

	return 0;
}

uint32
dhd_prot_metadata_dbg_set(dhd_pub_t *dhd, bool val)
{
	dhd_prot_t *prot = dhd->prot;
#if DHD_DBG_SHOW_METADATA
	prot->metadata_dbg = val;
#endif
	return (uint32)prot->metadata_dbg;
}

uint32
dhd_prot_metadata_dbg_get(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	return (uint32)prot->metadata_dbg;
}

uint32
dhd_prot_metadatalen_set(dhd_pub_t *dhd, uint32 val, bool rx)
{
	dhd_prot_t *prot = dhd->prot;
	if (rx)
		prot->rx_metadata_offset = (uint16)val;
	else
		prot->tx_metadata_offset = (uint16)val;
	return dhd_prot_metadatalen_get(dhd, rx);
}

uint32
dhd_prot_metadatalen_get(dhd_pub_t *dhd, bool rx)
{
	dhd_prot_t *prot = dhd->prot;
	if (rx)
		return prot->rx_metadata_offset;
	else
		return prot->tx_metadata_offset;
}

/** optimization to write "n" tx items at a time to ring */
uint32
dhd_prot_txp_threshold(dhd_pub_t *dhd, bool set, uint32 val)
{
	dhd_prot_t *prot = dhd->prot;
	if (set)
		prot->txp_threshold = (uint16)val;
	val = prot->txp_threshold;
	return val;
}

#ifdef DHD_RX_CHAINING

static INLINE void BCMFASTPATH
dhd_rxchain_reset(rxchain_info_t *rxchain)
{
	rxchain->pkt_count = 0;
}

static void BCMFASTPATH
dhd_rxchain_frame(dhd_pub_t *dhd, void *pkt, uint ifidx)
{
	uint8 *eh;
	uint8 prio;
	dhd_prot_t *prot = dhd->prot;
	rxchain_info_t *rxchain = &prot->rxchain;

	ASSERT(!PKTISCHAINED(pkt));
	ASSERT(PKTCLINK(pkt) == NULL);
	ASSERT(PKTCGETATTR(pkt) == 0);

	eh = PKTDATA(dhd->osh, pkt);
	prio = IP_TOS46(eh + ETHER_HDR_LEN) >> IPV4_TOS_PREC_SHIFT;

	if (rxchain->pkt_count && !(PKT_CTF_CHAINABLE(dhd, ifidx, eh, prio, rxchain->h_sa,
		rxchain->h_da, rxchain->h_prio))) {
		/* Different flow - First release the existing chain */
		dhd_rxchain_commit(dhd);
	}

	/* For routers, with HNDCTF, link the packets using PKTSETCLINK, */
	/* so that the chain can be handed off to CTF bridge as is. */
	if (rxchain->pkt_count == 0) {
		/* First packet in chain */
		rxchain->pkthead = rxchain->pkttail = pkt;

		/* Keep a copy of ptr to ether_da, ether_sa and prio */
		rxchain->h_da = ((struct ether_header *)eh)->ether_dhost;
		rxchain->h_sa = ((struct ether_header *)eh)->ether_shost;
		rxchain->h_prio = prio;
		rxchain->ifidx = ifidx;
		rxchain->pkt_count++;
	} else {
		/* Same flow - keep chaining */
		PKTSETCLINK(rxchain->pkttail, pkt);
		rxchain->pkttail = pkt;
		rxchain->pkt_count++;
	}

	if ((dhd_rx_pkt_chainable(dhd, ifidx)) && (!ETHER_ISMULTI(rxchain->h_da)) &&
		((((struct ether_header *)eh)->ether_type == HTON16(ETHER_TYPE_IP)) ||
		(((struct ether_header *)eh)->ether_type == HTON16(ETHER_TYPE_IPV6)))) {
		PKTSETCHAINED(dhd->osh, pkt);
		PKTCINCRCNT(rxchain->pkthead);
		PKTCADDLEN(rxchain->pkthead, PKTLEN(dhd->osh, pkt));
	} else {
		dhd_rxchain_commit(dhd);
		return;
	}

	/* If we have hit the max chain length, dispatch the chain and reset */
	if (rxchain->pkt_count >= DHD_PKT_CTF_MAX_CHAIN_LEN) {
		dhd_rxchain_commit(dhd);
	}
}

static void BCMFASTPATH
dhd_rxchain_commit(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	rxchain_info_t *rxchain = &prot->rxchain;

	if (rxchain->pkt_count == 0)
		return;

	/* Release the packets to dhd_linux */
	dhd_bus_rx_frame(dhd->bus, rxchain->pkthead, rxchain->ifidx, rxchain->pkt_count);

	/* Reset the chain */
	dhd_rxchain_reset(rxchain);
}

#endif /* DHD_RX_CHAINING */


#ifdef IDLE_TX_FLOW_MGMT
int
dhd_prot_flow_ring_resume(dhd_pub_t *dhd, flow_ring_node_t *flow_ring_node)
{
	tx_idle_flowring_resume_request_t *flow_resume_rqst;
	msgbuf_ring_t *flow_ring;
	dhd_prot_t *prot = dhd->prot;
	unsigned long flags;
	uint16 alloced = 0;
	msgbuf_ring_t *ctrl_ring = &prot->h2dring_ctrl_subn;

	/* Fetch a pre-initialized msgbuf_ring from the flowring pool */
	flow_ring = dhd_prot_flowrings_pool_fetch(dhd, flow_ring_node->flowid);
	if (flow_ring == NULL) {
		DHD_ERROR(("%s: dhd_prot_flowrings_pool_fetch TX Flowid %d failed\n",
			__FUNCTION__, flow_ring_node->flowid));
		return BCME_NOMEM;
	}
#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */

	DHD_GENERAL_LOCK(dhd, flags);

	/* Request for ctrl_ring buffer space */
	flow_resume_rqst = (tx_idle_flowring_resume_request_t *)
		dhd_prot_alloc_ring_space(dhd, ctrl_ring, 1, &alloced, FALSE);

	if (flow_resume_rqst == NULL) {
		dhd_prot_flowrings_pool_release(dhd, flow_ring_node->flowid, flow_ring);
		DHD_ERROR(("%s: Flow resume Req flowid %d - failure ring space\n",
			__FUNCTION__, flow_ring_node->flowid));
		DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef PCIE_INB_DW
		dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
		return BCME_NOMEM;
	}

	flow_ring_node->prot_info = (void *)flow_ring;

	/* Common msg buf hdr */
	flow_resume_rqst->msg.msg_type = MSG_TYPE_FLOW_RING_RESUME;
	flow_resume_rqst->msg.if_id = (uint8)flow_ring_node->flow_info.ifindex;
	flow_resume_rqst->msg.request_id = htol32(0); /* TBD */

	flow_resume_rqst->msg.epoch = ctrl_ring->seqnum % H2D_EPOCH_MODULO;
	ctrl_ring->seqnum++;

	flow_resume_rqst->flow_ring_id = htol16((uint16)flow_ring_node->flowid);
	DHD_ERROR(("%s Send Flow resume Req flow ID %d\n",
		__FUNCTION__, flow_ring_node->flowid));

	/* Update the flow_ring's WRITE index */
	if (IDMA_ACTIVE(dhd) || dhd->dma_h2d_ring_upd_support) {
		dhd_prot_dma_indx_set(dhd, flow_ring->wr,
		                      H2D_DMA_INDX_WR_UPD, flow_ring->idx);
	} else if (IFRM_ACTIVE(dhd) && (flow_ring->idx >= BCMPCIE_H2D_MSGRING_TXFLOW_IDX_START)) {
		dhd_prot_dma_indx_set(dhd, flow_ring->wr,
			H2D_IFRM_INDX_WR_UPD,
			(flow_ring->idx - BCMPCIE_H2D_MSGRING_TXFLOW_IDX_START));
	} else {
		dhd_bus_cmn_writeshared(dhd->bus, &(flow_ring->wr),
			sizeof(uint16), RING_WR_UPD, flow_ring->idx);
	}

	/* update control subn ring's WR index and ring doorbell to dongle */
	dhd_prot_ring_write_complete(dhd, ctrl_ring, flow_resume_rqst, 1);

	DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif

	return BCME_OK;
} /* dhd_prot_flow_ring_create */

int
dhd_prot_flow_ring_batch_suspend_request(dhd_pub_t *dhd, uint16 *ringid, uint16 count)
{
	tx_idle_flowring_suspend_request_t *flow_suspend_rqst;
	dhd_prot_t *prot = dhd->prot;
	unsigned long flags;
	uint16 index;
	uint16 alloced = 0;
	msgbuf_ring_t *ring = &prot->h2dring_ctrl_subn;

#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */

	DHD_GENERAL_LOCK(dhd, flags);

	/* Request for ring buffer space */
	flow_suspend_rqst = (tx_idle_flowring_suspend_request_t *)
		dhd_prot_alloc_ring_space(dhd, ring, 1, &alloced, FALSE);

	if (flow_suspend_rqst == NULL) {
		DHD_GENERAL_UNLOCK(dhd, flags);
		DHD_ERROR(("%s: Flow suspend Req - failure ring space\n", __FUNCTION__));
#ifdef PCIE_INB_DW
		dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
		return BCME_NOMEM;
	}

	/* Common msg buf hdr */
	flow_suspend_rqst->msg.msg_type = MSG_TYPE_FLOW_RING_SUSPEND;
	/* flow_suspend_rqst->msg.if_id = (uint8)flow_ring_node->flow_info.ifindex; */
	flow_suspend_rqst->msg.request_id = htol32(0); /* TBD */

	flow_suspend_rqst->msg.epoch = ring->seqnum % H2D_EPOCH_MODULO;
	ring->seqnum++;

	/* Update flow id  info */
	for (index = 0; index < count; index++)
	{
		flow_suspend_rqst->ring_id[index] = ringid[index];
	}
	flow_suspend_rqst->num = count;

	DHD_ERROR(("%s sending batch suspend!! count is %d\n", __FUNCTION__, count));

	/* update ring's WR index and ring doorbell to dongle */
	dhd_prot_ring_write_complete(dhd, ring, flow_suspend_rqst, 1);
	DHD_GENERAL_UNLOCK(dhd, flags);
#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif

	return BCME_OK;
}
#endif /* IDLE_TX_FLOW_MGMT */


int dhd_prot_dump_extended_trap(dhd_pub_t *dhdp, struct bcmstrbuf *b, bool raw)
{
	uint32 i;
	uint32 *ext_data;
	hnd_ext_trap_hdr_t *hdr;
	bcm_tlv_t *tlv;
	trap_t *tr;
	uint32 *stack;
	hnd_ext_trap_bp_err_t *bpe;
	uint32 raw_len;

	ext_data = dhdp->extended_trap_data;

	/* return if there is no extended trap data */
	if (!ext_data || !(dhdp->dongle_trap_data & D2H_DEV_EXT_TRAP_DATA))
	{
		bcm_bprintf(b, "%d (0x%x)", dhdp->dongle_trap_data, dhdp->dongle_trap_data);
		return BCME_OK;
	}

	bcm_bprintf(b, "Extended trap data\n");

	/* First word is original trap_data */
	bcm_bprintf(b, "trap_data = 0x%08x\n", *ext_data);
	ext_data++;

	/* Followed by the extended trap data header */
	hdr = (hnd_ext_trap_hdr_t *)ext_data;
	bcm_bprintf(b, "version: %d, len: %d\n", hdr->version, hdr->len);

	if (raw)
	{
		raw_len = sizeof(hnd_ext_trap_hdr_t) + (hdr->len / 4) + (hdr->len % 4 ? 1 : 0);
		for (i = 0; i < raw_len; i++)
		{
			bcm_bprintf(b, "0x%08x ", ext_data[i]);
			if (i % 4 == 3)
				bcm_bprintf(b, "\n");
		}
		return BCME_OK;
	}

	/* Extract the various supported TLVs from the extended trap data */
	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_SIGNATURE);
	if (tlv)
	{
		bcm_bprintf(b, "\nTAG_TRAP_SIGNATURE len: %d\n", tlv->len);
		tr = (trap_t *)tlv->data;

		bcm_bprintf(b, "TRAP %x: pc %x, lr %x, sp %x, cpsr %x, spsr %x\n",
		       tr->type, tr->pc, tr->r14, tr->r13, tr->cpsr, tr->spsr);
		bcm_bprintf(b, "  r0 %x, r1 %x, r2 %x, r3 %x, r4 %x, r5 %x, r6 %x\n",
		       tr->r0, tr->r1, tr->r2, tr->r3, tr->r4, tr->r5, tr->r6);
		bcm_bprintf(b, "  r7 %x, r8 %x, r9 %x, r10 %x, r11 %x, r12 %x\n",
		       tr->r7, tr->r8, tr->r9, tr->r10, tr->r11, tr->r12);
	}

	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_STACK);
	if (tlv)
	{
		bcm_bprintf(b, "\nTAG_TRAP_STACK len: %d\n", tlv->len);
		stack = (uint32 *)tlv->data;
		for (i = 0; i < (uint32)(tlv->len / 4); i++)
		{
			bcm_bprintf(b, "  0x%08x\n", *stack);
			stack++;
		}
	}

	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_BACKPLANE);
	if (tlv)
	{
		bcm_bprintf(b, "\nTAG_TRAP_BACKPLANE len: %d\n", tlv->len);
		bpe = (hnd_ext_trap_bp_err_t *)tlv->data;
		bcm_bprintf(b, " error: %x\n", bpe->error);
		bcm_bprintf(b, " coreid: %x\n", bpe->coreid);
		bcm_bprintf(b, " baseaddr: %x\n", bpe->baseaddr);
		bcm_bprintf(b, " ioctrl: %x\n", bpe->ioctrl);
		bcm_bprintf(b, " iostatus: %x\n", bpe->iostatus);
		bcm_bprintf(b, " resetctrl: %x\n", bpe->resetctrl);
		bcm_bprintf(b, " resetstatus: %x\n", bpe->resetstatus);
		bcm_bprintf(b, " errlogctrl: %x\n", bpe->errlogctrl);
		bcm_bprintf(b, " errlogdone: %x\n", bpe->errlogdone);
		bcm_bprintf(b, " errlogstatus: %x\n", bpe->errlogstatus);
		bcm_bprintf(b, " errlogaddrlo: %x\n", bpe->errlogaddrlo);
		bcm_bprintf(b, " errlogaddrhi: %x\n", bpe->errlogaddrhi);
		bcm_bprintf(b, " errlogid: %x\n", bpe->errlogid);
		bcm_bprintf(b, " errloguser: %x\n", bpe->errloguser);
		bcm_bprintf(b, " errlogflags: %x\n", bpe->errlogflags);
	}

	return BCME_OK;
}


#ifdef BCMPCIE
int
dhd_prot_send_host_timestamp(dhd_pub_t *dhdp, uchar *tlvs, uint16 tlv_len,
	uint16 seqnum, uint16 xt_id)
{
	dhd_prot_t *prot = dhdp->prot;
	host_timestamp_msg_t *ts_req;
	unsigned long flags;
	uint16 alloced = 0;
	uchar *ts_tlv_buf;

	if ((tlvs == NULL) || (tlv_len == 0)) {
		DHD_ERROR(("%s: argument error tlv: %p, tlv_len %d\n",
			__FUNCTION__, tlvs, tlv_len));
		return -1;
	}
#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhdp->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */

	DHD_GENERAL_LOCK(dhdp, flags);

	/* if Host TS req already pending go away */
	if (prot->hostts_req_buf_inuse == TRUE) {
		DHD_ERROR(("one host TS request already pending at device\n"));
		DHD_GENERAL_UNLOCK(dhdp, flags);
#ifdef PCIE_INB_DW
		dhd_prot_dec_hostactive_ack_pending_dsreq(dhdp->bus);
#endif
		return -1;
	}

	/* Request for cbuf space */
	ts_req = (host_timestamp_msg_t*)dhd_prot_alloc_ring_space(dhdp, &prot->h2dring_ctrl_subn,
		DHD_FLOWRING_DEFAULT_NITEMS_POSTED_H2D,	&alloced, FALSE);
	if (ts_req == NULL) {
		DHD_ERROR(("couldn't allocate space on msgring to send host TS request\n"));
		DHD_GENERAL_UNLOCK(dhdp, flags);
#ifdef PCIE_INB_DW
		dhd_prot_dec_hostactive_ack_pending_dsreq(dhdp->bus);
#endif
		return -1;
	}

	/* Common msg buf hdr */
	ts_req->msg.msg_type = MSG_TYPE_HOSTTIMSTAMP;
	ts_req->msg.if_id = 0;
	ts_req->msg.flags =  prot->h2dring_ctrl_subn.current_phase;
	ts_req->msg.request_id = DHD_H2D_HOSTTS_REQ_PKTID;

	ts_req->msg.epoch = prot->h2dring_ctrl_subn.seqnum % H2D_EPOCH_MODULO;
	prot->h2dring_ctrl_subn.seqnum++;

	ts_req->xt_id = xt_id;
	ts_req->seqnum = seqnum;
	/* populate TS req buffer info */
	ts_req->input_data_len = htol16(tlv_len);
	ts_req->host_buf_addr.high = htol32(PHYSADDRHI(prot->hostts_req_buf.pa));
	ts_req->host_buf_addr.low = htol32(PHYSADDRLO(prot->hostts_req_buf.pa));
	/* copy ioct payload */
	ts_tlv_buf = (void *) prot->hostts_req_buf.va;
	prot->hostts_req_buf_inuse = TRUE;
	memcpy(ts_tlv_buf, tlvs, tlv_len);

	OSL_CACHE_FLUSH((void *) prot->hostts_req_buf.va, tlv_len);

	if (ISALIGNED(ts_tlv_buf, DMA_ALIGN_LEN) == FALSE) {
		DHD_ERROR(("host TS req buffer address unaligned !!!!! \n"));
	}

	DHD_CTL(("submitted Host TS request request_id %d, data_len %d, tx_id %d, seq %d\n",
		ts_req->msg.request_id, ts_req->input_data_len,
		ts_req->xt_id, ts_req->seqnum));


	/* upd wrt ptr and raise interrupt */
	dhd_prot_ring_write_complete(dhdp, &prot->h2dring_ctrl_subn, ts_req,
		DHD_FLOWRING_DEFAULT_NITEMS_POSTED_H2D);
	DHD_GENERAL_UNLOCK(dhdp, flags);
#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhdp->bus);
#endif

	return 0;
} /* dhd_prot_send_host_timestamp */


bool
dhd_prot_data_path_tx_timestamp_logging(dhd_pub_t *dhd,  bool enable, bool set)
{
	if (set)
		dhd->prot->tx_ts_log_enabled = enable;

	return dhd->prot->tx_ts_log_enabled;
}

bool
dhd_prot_data_path_rx_timestamp_logging(dhd_pub_t *dhd,  bool enable, bool set)
{
	if (set)
		dhd->prot->rx_ts_log_enabled = enable;

	return dhd->prot->rx_ts_log_enabled;
}
#endif /* BCMPCIE */

void
dhd_prot_dma_indx_free(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;

	dhd_dma_buf_free(dhd, &prot->h2d_dma_indx_wr_buf);
	dhd_dma_buf_free(dhd, &prot->d2h_dma_indx_rd_buf);
}

static void BCMFASTPATH
dhd_prot_process_fw_timestamp(dhd_pub_t *dhd, void* buf)
{
#ifdef DHD_TIMESYNC
	fw_timestamp_event_msg_t *resp;
	uint32 pktid;
	uint16 buflen, seqnum;
	void * pkt;
	unsigned long flags;

	resp = (fw_timestamp_event_msg_t *)buf;
	pktid = ltoh32(resp->msg.request_id);
	buflen = ltoh16(resp->buf_len);
	seqnum = ltoh16(resp->seqnum);

#if defined(DHD_PKTID_AUDIT_RING)
	DHD_PKTID_AUDIT(dhd, dhd->prot->pktid_ctrl_map, pktid,
		DHD_DUPLICATE_FREE);
#endif /* DHD_PKTID_AUDIT_RING */

	DHD_INFO(("id 0x%04x, len %d, phase 0x%02x, seqnum %d\n",
		pktid, buflen, resp->msg.flags, ltoh16(resp->seqnum)));

	if (!dhd->prot->cur_ts_bufs_posted) {
		DHD_ERROR(("tsbuf posted are zero, but there is a completion\n"));
		return;
	}

	dhd->prot->cur_ts_bufs_posted--;
	if (dhd->prot->max_tsbufpost > 0)
		dhd_msgbuf_rxbuf_post_ts_bufs(dhd);

	DHD_GENERAL_LOCK(dhd, flags);
	pkt = dhd_prot_packet_get(dhd, pktid, PKTTYPE_TSBUF_RX, TRUE);
	DHD_GENERAL_UNLOCK(dhd, flags);

	if (!pkt) {
		DHD_ERROR(("no ts buffer associated with pktid 0x%04x\n", pktid));
		return;
	}

	PKTSETLEN(dhd->osh, pkt, buflen);
	dhd_timesync_handle_fw_timestamp(dhd->ts, PKTDATA(dhd->osh, pkt), buflen, seqnum);
#ifdef DHD_USE_STATIC_CTRLBUF
	PKTFREE_STATIC(dhd->osh, pkt, TRUE);
#else
	PKTFREE(dhd->osh, pkt, TRUE);
#endif /* DHD_USE_STATIC_CTRLBUF */
#else /* DHD_TIMESYNC */
	DHD_ERROR(("Timesunc feature not compiled in but GOT FW TS message\n"));
#endif /* DHD_TIMESYNC */

}
